#include "secure_connection.hpp"
#include "../utils/logger.hpp"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

namespace goldearn::network {

// Static initialization for OpenSSL
static bool g_openssl_initialized = false;
static std::mutex g_openssl_mutex;

static void init_openssl() {
    std::lock_guard<std::mutex> lock(g_openssl_mutex);
    if (!g_openssl_initialized) {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        g_openssl_initialized = true;
    }
}

class SecureConnection::Impl {
public:
    Impl() : socket_fd_(-1), ssl_ctx_(nullptr), ssl_(nullptr), connected_(false) {
        init_openssl();
    }
    
    ~Impl() {
        disconnect();
        cleanup_ssl();
    }
    
    bool connect(const std::string& host, uint16_t port, const TLSConfig& config) {
        if (connected_) {
            LOG_WARN("SecureConnection: Already connected");
            return true;
        }
        
        // Create SSL context
        if (!create_ssl_context(config)) {
            return false;
        }
        
        // Create TCP socket
        socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd_ < 0) {
            LOG_ERROR("SecureConnection: Failed to create socket: {}", strerror(errno));
            return false;
        }
        
        // Set socket options for low latency
        int flag = 1;
        setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        
        // Set socket buffer sizes
        int buf_size = 1024 * 1024;  // 1MB
        setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
        setsockopt(socket_fd_, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
        
        // Resolve host
        struct addrinfo hints = {0};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        struct addrinfo* result = nullptr;
        std::string port_str = std::to_string(port);
        int ret = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
        if (ret != 0) {
            LOG_ERROR("SecureConnection: Failed to resolve host {}: {}", host, gai_strerror(ret));
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
        
        // Connect
        ret = ::connect(socket_fd_, result->ai_addr, result->ai_addrlen);
        freeaddrinfo(result);
        
        if (ret < 0) {
            LOG_ERROR("SecureConnection: Failed to connect: {}", strerror(errno));
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
        
        // Create SSL object and attach to socket
        ssl_ = SSL_new(ssl_ctx_);
        if (!ssl_) {
            LOG_ERROR("SecureConnection: Failed to create SSL object");
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
        
        SSL_set_fd(ssl_, socket_fd_);
        
        // Enable hostname verification
        if (!config.skip_hostname_verification) {
            SSL_set_hostflags(ssl_, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
            if (!SSL_set1_host(ssl_, host.c_str())) {
                LOG_ERROR("SecureConnection: Failed to set hostname for verification");
                cleanup_connection();
                return false;
            }
        }
        
        // Perform SSL handshake
        ret = SSL_connect(ssl_);
        if (ret <= 0) {
            int ssl_error = SSL_get_error(ssl_, ret);
            LOG_ERROR("SecureConnection: SSL handshake failed: {}", get_ssl_error_string(ssl_error));
            cleanup_connection();
            return false;
        }
        
        // Verify certificate
        if (!verify_certificate(host, config)) {
            cleanup_connection();
            return false;
        }
        
        host_ = host;
        port_ = port;
        connected_ = true;
        
        LOG_INFO("SecureConnection: Successfully connected to {}:{} using {}", 
                 host, port, SSL_get_cipher(ssl_));
        
        return true;
    }
    
    void disconnect() {
        if (connected_) {
            if (ssl_) {
                SSL_shutdown(ssl_);
            }
            connected_ = false;
        }
        cleanup_connection();
    }
    
    ssize_t send(const void* data, size_t length) {
        if (!connected_ || !ssl_) {
            errno = ENOTCONN;
            return -1;
        }
        
        int ret = SSL_write(ssl_, data, length);
        if (ret <= 0) {
            int ssl_error = SSL_get_error(ssl_, ret);
            if (ssl_error == SSL_ERROR_WANT_WRITE || ssl_error == SSL_ERROR_WANT_READ) {
                errno = EAGAIN;
            }
            return -1;
        }
        
        return ret;
    }
    
    ssize_t receive(void* buffer, size_t length) {
        if (!connected_ || !ssl_) {
            errno = ENOTCONN;
            return -1;
        }
        
        int ret = SSL_read(ssl_, buffer, length);
        if (ret <= 0) {
            int ssl_error = SSL_get_error(ssl_, ret);
            if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
                errno = EAGAIN;
            } else if (ssl_error == SSL_ERROR_ZERO_RETURN) {
                // Connection closed cleanly
                return 0;
            }
            return -1;
        }
        
        return ret;
    }
    
    bool is_connected() const {
        return connected_ && ssl_ && SSL_is_init_finished(ssl_);
    }
    
    std::string get_cipher_info() const {
        if (!ssl_) return "";
        
        const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl_);
        if (!cipher) return "";
        
        char buf[256];
        snprintf(buf, sizeof(buf), "%s %s %d bits",
                SSL_get_version(ssl_),
                SSL_CIPHER_get_name(cipher),
                SSL_CIPHER_get_bits(cipher, nullptr));
        
        return std::string(buf);
    }
    
private:
    bool create_ssl_context(const TLSConfig& config) {
        // Create context with TLS 1.2 minimum
        ssl_ctx_ = SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx_) {
            LOG_ERROR("SecureConnection: Failed to create SSL context");
            return false;
        }
        
        // Set minimum TLS version
        if (!SSL_CTX_set_min_proto_version(ssl_ctx_, 
            config.min_tls_version == TLSVersion::TLS_1_3 ? TLS1_3_VERSION : TLS1_2_VERSION)) {
            LOG_ERROR("SecureConnection: Failed to set minimum TLS version");
            return false;
        }
        
        // Set cipher suites - only strong ciphers
        const char* cipher_list = config.custom_cipher_list.empty() ?
            "ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:DHE+CHACHA20:!aNULL:!MD5:!DSS" :
            config.custom_cipher_list.c_str();
        
        if (!SSL_CTX_set_cipher_list(ssl_ctx_, cipher_list)) {
            LOG_ERROR("SecureConnection: Failed to set cipher list");
            return false;
        }
        
        // Load CA certificates
        if (!config.ca_cert_path.empty()) {
            if (!SSL_CTX_load_verify_locations(ssl_ctx_, config.ca_cert_path.c_str(), nullptr)) {
                LOG_ERROR("SecureConnection: Failed to load CA certificate: {}", config.ca_cert_path);
                return false;
            }
        } else {
            // Use system default CA certificates
            if (!SSL_CTX_set_default_verify_paths(ssl_ctx_)) {
                LOG_ERROR("SecureConnection: Failed to load default CA certificates");
                return false;
            }
        }
        
        // Load client certificate if provided
        if (!config.client_cert_path.empty()) {
            if (!SSL_CTX_use_certificate_file(ssl_ctx_, config.client_cert_path.c_str(), SSL_FILETYPE_PEM)) {
                LOG_ERROR("SecureConnection: Failed to load client certificate: {}", config.client_cert_path);
                return false;
            }
            
            if (!SSL_CTX_use_PrivateKey_file(ssl_ctx_, config.client_key_path.c_str(), SSL_FILETYPE_PEM)) {
                LOG_ERROR("SecureConnection: Failed to load client private key: {}", config.client_key_path);
                return false;
            }
            
            if (!SSL_CTX_check_private_key(ssl_ctx_)) {
                LOG_ERROR("SecureConnection: Client certificate and private key do not match");
                return false;
            }
        }
        
        // Set verification mode
        SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, nullptr);
        
        // Enable session caching for performance
        SSL_CTX_set_session_cache_mode(ssl_ctx_, SSL_SESS_CACHE_CLIENT);
        
        return true;
    }
    
    bool verify_certificate(const std::string& host, const TLSConfig& config) {
        if (config.skip_certificate_verification) {
            LOG_WARN("SecureConnection: Certificate verification disabled (INSECURE)");
            return true;
        }
        
        X509* cert = SSL_get_peer_certificate(ssl_);
        if (!cert) {
            LOG_ERROR("SecureConnection: No certificate provided by server");
            return false;
        }
        
        // Verify certificate chain
        long verify_result = SSL_get_verify_result(ssl_);
        if (verify_result != X509_V_OK) {
            LOG_ERROR("SecureConnection: Certificate verification failed: {}",
                     X509_verify_cert_error_string(verify_result));
            X509_free(cert);
            return false;
        }
        
        // Additional certificate checks
        bool valid = true;
        
        // Check certificate validity period
        ASN1_TIME* not_before = X509_get_notBefore(cert);
        ASN1_TIME* not_after = X509_get_notAfter(cert);
        
        if (X509_cmp_current_time(not_before) > 0) {
            LOG_ERROR("SecureConnection: Certificate not yet valid");
            valid = false;
        }
        
        if (X509_cmp_current_time(not_after) < 0) {
            LOG_ERROR("SecureConnection: Certificate has expired");
            valid = false;
        }
        
        // Check key usage
        if (valid && config.check_key_usage) {
            uint32_t key_usage = X509_get_key_usage(cert);
            if (!(key_usage & (KU_DIGITAL_SIGNATURE | KU_KEY_AGREEMENT))) {
                LOG_ERROR("SecureConnection: Certificate key usage not suitable for TLS");
                valid = false;
            }
        }
        
        // Log certificate info
        if (valid) {
            char subject[256];
            X509_NAME_oneline(X509_get_subject_name(cert), subject, sizeof(subject));
            LOG_INFO("SecureConnection: Server certificate: {}", subject);
        }
        
        X509_free(cert);
        return valid;
    }
    
    void cleanup_connection() {
        if (ssl_) {
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
    }
    
    void cleanup_ssl() {
        if (ssl_ctx_) {
            SSL_CTX_free(ssl_ctx_);
            ssl_ctx_ = nullptr;
        }
    }
    
    std::string get_ssl_error_string(int ssl_error) {
        switch (ssl_error) {
            case SSL_ERROR_NONE: return "No error";
            case SSL_ERROR_SSL: return "SSL protocol error";
            case SSL_ERROR_WANT_READ: return "Want read";
            case SSL_ERROR_WANT_WRITE: return "Want write";
            case SSL_ERROR_SYSCALL: return std::string("System call error: ") + strerror(errno);
            case SSL_ERROR_ZERO_RETURN: return "Connection closed";
            case SSL_ERROR_WANT_CONNECT: return "Want connect";
            case SSL_ERROR_WANT_ACCEPT: return "Want accept";
            default: return "Unknown SSL error";
        }
    }
    
private:
    int socket_fd_;
    SSL_CTX* ssl_ctx_;
    SSL* ssl_;
    bool connected_;
    std::string host_;
    uint16_t port_;
};

// SecureConnection public interface implementation
SecureConnection::SecureConnection() : impl_(std::make_unique<Impl>()) {}

SecureConnection::~SecureConnection() = default;

bool SecureConnection::connect(const std::string& host, uint16_t port, const TLSConfig& config) {
    return impl_->connect(host, port, config);
}

void SecureConnection::disconnect() {
    impl_->disconnect();
}

ssize_t SecureConnection::send(const void* data, size_t length) {
    return impl_->send(data, length);
}

ssize_t SecureConnection::receive(void* buffer, size_t length) {
    return impl_->receive(buffer, length);
}

bool SecureConnection::is_connected() const {
    return impl_->is_connected();
}

std::string SecureConnection::get_cipher_info() const {
    return impl_->get_cipher_info();
}

} // namespace goldearn::network