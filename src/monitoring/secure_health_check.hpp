#pragma once

#include "health_check.hpp"
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <regex>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace goldearn::monitoring {

// Rate limiting for endpoints
class RateLimiter {
public:
    RateLimiter(int max_requests_per_minute = 60) 
        : max_requests_(max_requests_per_minute) {}
    
    bool allow_request(const std::string& client_ip);
    void cleanup_old_entries();
    
private:
    struct ClientInfo {
        std::vector<std::chrono::steady_clock::time_point> requests;
        std::chrono::steady_clock::time_point last_cleanup;
    };
    
    int max_requests_;
    std::unordered_map<std::string, ClientInfo> clients_;
    mutable std::mutex mutex_;
};

// Input validation utilities
class InputValidator {
public:
    static bool validate_http_path(const std::string& path);
    static bool validate_http_method(const std::string& method);
    static bool validate_ip_address(const std::string& ip);
    static bool validate_user_agent(const std::string& user_agent);
    static std::string sanitize_input(const std::string& input);
    static bool is_suspicious_request(const std::string& path, const std::string& user_agent);
    
private:
    static std::regex path_regex_;
    static std::regex ip_regex_;
    static std::vector<std::string> blocked_patterns_;
};

// Authentication for monitoring endpoints
class MonitoringAuth {
public:
    MonitoringAuth();
    
    bool authenticate_request(const std::string& auth_header);
    std::string generate_api_key();
    bool validate_api_key(const std::string& api_key);
    void revoke_api_key(const std::string& api_key);
    
    // JWT token support
    std::string generate_jwt_token(const std::string& user, std::chrono::seconds expiry);
    bool validate_jwt_token(const std::string& token);
    
private:
    std::string hash_api_key(const std::string& api_key);
    std::string generate_random_key();
    
    std::unordered_set<std::string> valid_api_keys_;
    std::string jwt_secret_;
    mutable std::mutex mutex_;
};

// Secure health check server with protection
class SecureHealthCheckServer : public HealthCheckServer {
public:
    SecureHealthCheckServer(uint16_t port = 8080);
    ~SecureHealthCheckServer();
    
    // Security configuration
    void enable_authentication(bool enabled = true) { auth_enabled_ = enabled; }
    void enable_rate_limiting(bool enabled = true) { rate_limiting_enabled_ = enabled; }
    void enable_input_validation(bool enabled = true) { input_validation_enabled_ = enabled; }
    void enable_https(const std::string& cert_path, const std::string& key_path);
    
    // Access control
    void add_allowed_ip(const std::string& ip) { allowed_ips_.insert(ip); }
    void remove_allowed_ip(const std::string& ip) { allowed_ips_.erase(ip); }
    void set_api_key(const std::string& api_key);
    
    // Security monitoring
    void log_security_event(const std::string& event, const std::string& client_ip);
    std::vector<std::string> get_security_events() const;
    
protected:
    std::string handle_request(const std::string& path, const std::string& method,
                              const std::string& client_ip = "",
                              const std::string& user_agent = "",
                              const std::string& auth_header = "");
    
private:
    bool is_request_allowed(const std::string& client_ip, const std::string& path,
                           const std::string& method, const std::string& user_agent,
                           const std::string& auth_header);
    
    std::string create_error_response(int status_code, const std::string& message);
    std::string get_client_ip_from_socket(int socket_fd);
    std::string extract_user_agent(const std::string& request);
    std::string extract_auth_header(const std::string& request);
    
private:
    // Security settings
    bool auth_enabled_ = true;
    bool rate_limiting_enabled_ = true;
    bool input_validation_enabled_ = true;
    bool https_enabled_ = false;
    
    // Security components
    std::unique_ptr<RateLimiter> rate_limiter_;
    std::unique_ptr<MonitoringAuth> auth_;
    std::unordered_set<std::string> allowed_ips_;
    
    // SSL context for HTTPS
    std::string ssl_cert_path_;
    std::string ssl_key_path_;
    
    // Security logging
    std::vector<std::string> security_events_;
    mutable std::mutex security_mutex_;
    
    // Configuration
    static constexpr int MAX_REQUEST_SIZE = 8192;
    static constexpr int MAX_HEADER_SIZE = 4096;
    static constexpr int CONNECTION_TIMEOUT_SEC = 30;
};

// Security audit utilities
class SecurityAuditor {
public:
    struct SecurityCheck {
        std::string check_name;
        bool passed;
        std::string description;
        std::string recommendation;
    };
    
    static std::vector<SecurityCheck> audit_health_check_server(const SecureHealthCheckServer& server);
    static std::vector<SecurityCheck> audit_configuration_security();
    static std::vector<SecurityCheck> audit_network_security();
    static std::vector<SecurityCheck> audit_authentication_security();
    
    static void generate_security_report(const std::vector<SecurityCheck>& checks,
                                       const std::string& output_file);
};

} // namespace goldearn::monitoring