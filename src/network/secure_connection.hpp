#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <chrono>
#include <functional>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace goldearn::network {

enum class ConnectionState {
    DISCONNECTED = 0,
    CONNECTING = 1,
    CONNECTED = 2,
    RECONNECTING = 3,
    ERROR = 4
};

enum class SecurityLevel {
    NONE = 0,
    TLS_1_2 = 1,
    TLS_1_3 = 2,
    DTLS_1_2 = 3,
    CUSTOM = 4
};

struct ConnectionConfig {
    std::string host;
    uint16_t port;
    SecurityLevel security = SecurityLevel::TLS_1_3;
    uint32_t connect_timeout_ms = 30000;
    uint32_t read_timeout_ms = 5000;
    uint32_t heartbeat_interval_ms = 30000;
    uint32_t max_message_size = 65536;
    bool enable_compression = false;
    bool verify_certificates = true;
    std::string certificate_path;
    std::string private_key_path;
    std::string ca_bundle_path;
    
    // Rate limiting
    uint32_t max_messages_per_second = 10000;
    uint32_t max_bytes_per_second = 10 * 1024 * 1024; // 10MB/s
    
    // Reconnection policy
    bool auto_reconnect = true;
    uint32_t max_reconnect_attempts = 10;
    uint32_t reconnect_delay_ms = 1000;
    double reconnect_backoff_multiplier = 2.0;
    uint32_t max_reconnect_delay_ms = 30000;
};

struct ConnectionStats {
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<uint64_t> messages_sent{0};
    std::atomic<uint64_t> messages_received{0};
    std::atomic<uint64_t> connection_attempts{0};
    std::atomic<uint64_t> connection_failures{0};
    std::atomic<uint64_t> timeouts{0};
    std::atomic<uint64_t> protocol_errors{0};
    
    std::chrono::steady_clock::time_point last_connect_time;
    std::chrono::steady_clock::time_point last_message_time;
    std::atomic<uint32_t> current_latency_ms{0};
    std::atomic<uint32_t> avg_latency_ms{0};
};

class SecureConnection {
public:
    using DataCallback = std::function<void(const uint8_t*, size_t)>;
    using StateCallback = std::function<void(ConnectionState, const std::string&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    
    explicit SecureConnection(const ConnectionConfig& config);
    virtual ~SecureConnection();
    
    // Connection management
    bool connect();
    void disconnect();
    bool is_connected() const { return state_.load() == ConnectionState::CONNECTED; }
    ConnectionState get_state() const { return state_.load(); }
    
    // Data transmission
    bool send_data(const uint8_t* data, size_t length);
    bool send_message(const std::string& message);
    
    // Callback registration
    void set_data_callback(DataCallback callback) { data_callback_ = callback; }
    void set_state_callback(StateCallback callback) { state_callback_ = callback; }
    void set_error_callback(ErrorCallback callback) { error_callback_ = callback; }
    
    // Configuration
    void update_config(const ConnectionConfig& config);
    const ConnectionConfig& get_config() const { return config_; }
    
    // Statistics
    const ConnectionStats& get_stats() const { return stats_; }
    void reset_stats();
    
    // Health monitoring
    bool send_heartbeat();
    std::chrono::milliseconds get_last_activity_time() const;
    double get_connection_uptime_seconds() const;
    
    // Rate limiting
    bool check_rate_limits() const;
    void update_rate_counters();
    
protected:
    virtual bool establish_connection() = 0;
    virtual void close_connection() = 0;
    virtual bool send_raw_data(const uint8_t* data, size_t length) = 0;
    virtual size_t receive_data(uint8_t* buffer, size_t max_length) = 0;
    
    void set_state(ConnectionState new_state, const std::string& reason = "");
    void report_error(const std::string& error);
    void start_receive_loop();
    void stop_receive_loop();
    
    ConnectionConfig config_;
    std::atomic<ConnectionState> state_{ConnectionState::DISCONNECTED};
    ConnectionStats stats_;
    
private:
    DataCallback data_callback_;
    StateCallback state_callback_;
    ErrorCallback error_callback_;
    
    // Threading
    std::unique_ptr<std::thread> receive_thread_;
    std::atomic<bool> stop_requested_{false};
    
    // Reconnection
    std::unique_ptr<std::thread> reconnect_thread_;
    std::atomic<uint32_t> reconnect_attempts_{0};
    
    // Rate limiting
    mutable std::mutex rate_limit_mutex_;
    std::chrono::steady_clock::time_point last_rate_check_;
    uint32_t messages_this_second_;
    uint64_t bytes_this_second_;
    
    void receive_loop();
    void reconnect_loop();
    bool validate_message(const uint8_t* data, size_t length) const;
    void update_latency_stats(uint32_t latency_ms);
};

// TCP implementation with TLS
class SecureTCPConnection : public SecureConnection {
public:
    explicit SecureTCPConnection(const ConnectionConfig& config);
    ~SecureTCPConnection() override;
    
protected:
    bool establish_connection() override;
    void close_connection() override;
    bool send_raw_data(const uint8_t* data, size_t length) override;
    size_t receive_data(uint8_t* buffer, size_t max_length) override;
    
private:
    int socket_fd_;
    void* ssl_context_; // SSL_CTX* cast to void* to avoid OpenSSL dependency in header
    void* ssl_connection_; // SSL* cast to void*
    
    bool setup_ssl_context();
    void cleanup_ssl();
    bool verify_peer_certificate();
    std::string get_ssl_error_string() const;
};

// UDP implementation with DTLS
class SecureUDPConnection : public SecureConnection {
public:
    explicit SecureUDPConnection(const ConnectionConfig& config);
    ~SecureUDPConnection() override;
    
protected:
    bool establish_connection() override;
    void close_connection() override;
    bool send_raw_data(const uint8_t* data, size_t length) override;
    size_t receive_data(uint8_t* buffer, size_t max_length) override;
    
private:
    int socket_fd_;
    void* dtls_context_;
    
    bool setup_dtls_context();
    void cleanup_dtls();
};

// Factory for creating connections
class ConnectionFactory {
public:
    static std::unique_ptr<SecureConnection> create_connection(const ConnectionConfig& config);
    
    // Preset configurations for common use cases
    static ConnectionConfig nse_feed_config(const std::string& host, uint16_t port);
    static ConnectionConfig bse_feed_config(const std::string& host, uint16_t port);
    static ConnectionConfig oms_config(const std::string& host, uint16_t port);
};

// Connection pool for managing multiple connections
class ConnectionPool {
public:
    explicit ConnectionPool(size_t max_connections = 10);
    ~ConnectionPool();
    
    // Pool management
    std::shared_ptr<SecureConnection> get_connection(const std::string& connection_id);
    bool add_connection(const std::string& connection_id, std::unique_ptr<SecureConnection> connection);
    void remove_connection(const std::string& connection_id);
    void remove_all_connections();
    
    // Batch operations
    void connect_all();
    void disconnect_all();
    
    // Statistics
    size_t get_active_connections() const;
    size_t get_total_connections() const;
    std::vector<std::string> get_connection_ids() const;
    
    // Health monitoring
    void health_check();
    std::vector<std::pair<std::string, ConnectionState>> get_connection_states() const;
    
private:
    mutable std::shared_mutex pool_mutex_;
    std::unordered_map<std::string, std::shared_ptr<SecureConnection>> connections_;
    size_t max_connections_;
    
    std::unique_ptr<std::thread> health_check_thread_;
    std::atomic<bool> stop_health_check_{false};
    
    void health_check_loop();
};

// Network security utilities
namespace security {
    
    // Input validation
    bool validate_host(const std::string& host);
    bool validate_port(uint16_t port);
    bool validate_message_length(size_t length, size_t max_length);
    
    // Data sanitization
    std::string sanitize_host(const std::string& host);
    std::vector<uint8_t> sanitize_message_data(const uint8_t* data, size_t length);
    
    // Rate limiting
    class RateLimiter {
    public:
        RateLimiter(uint32_t max_requests_per_second, uint32_t max_bytes_per_second);
        
        bool allow_request();
        bool allow_bytes(size_t bytes);
        void reset_counters();
        
        uint32_t get_current_request_rate() const;
        uint32_t get_current_byte_rate() const;
        
    private:
        uint32_t max_requests_per_second_;
        uint32_t max_bytes_per_second_;
        
        mutable std::mutex mutex_;
        std::chrono::steady_clock::time_point last_reset_;
        uint32_t current_requests_;
        uint64_t current_bytes_;
    };
    
    // Certificate validation
    bool validate_certificate_chain(const std::string& cert_path);
    bool check_certificate_expiry(const std::string& cert_path, uint32_t warning_days = 30);
    std::string get_certificate_fingerprint(const std::string& cert_path);
    
} // namespace security

} // namespace goldearn::network