#pragma once

#include <string>
#include <memory>
#include <chrono>
#include <functional>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <filesystem>
#include "../config/config_manager.hpp"

namespace goldearn::network {

// Exchange authentication manager
class ExchangeAuthenticator {
public:
    enum class AuthMethod {
        API_KEY,
        CERTIFICATE,
        SESSION_TOKEN,
        OAUTH2
    };
    
    struct AuthCredentials {
        AuthMethod method = AuthMethod::API_KEY;
        std::string api_key;
        std::string secret_key;
        std::string session_token;
        std::string certificate_path;
        std::string private_key_path;
        std::string oauth_token;
        std::string oauth_refresh_token;
        std::chrono::system_clock::time_point token_expiry;
        bool auto_refresh = true;
    };
    
    ExchangeAuthenticator(const std::string& exchange_name);
    ~ExchangeAuthenticator();
    
    // Initialize with configuration
    bool initialize(const config::ConfigManager& config);
    
    // Authenticate with exchange
    bool authenticate();
    
    // Check if authenticated and token is valid
    bool is_authenticated() const;
    
    // Get authentication headers for HTTP requests
    std::unordered_map<std::string, std::string> get_auth_headers() const;
    
    // Get authentication parameters for socket connections
    std::string get_auth_string() const;
    
    // Refresh authentication token
    bool refresh_token();
    
    // Refresh session token specifically
    bool refresh_session_token() { return refresh_token(); }
    
    // Set callback for authentication events
    void set_auth_callback(std::function<void(bool, const std::string&)> callback);
    
    // Get current credentials (for testing)
    const AuthCredentials& get_credentials() const { return credentials_; }
    
private:
    // Exchange-specific authentication implementations
    bool authenticate_nse();
    bool authenticate_bse();
    bool authenticate_with_api_key();
    bool authenticate_with_certificate();
    bool authenticate_with_oauth2();
    
    // Helper methods for loading credentials
    void load_nse_credentials(const config::ConfigManager& config);
    void load_bse_credentials(const config::ConfigManager& config);
    
    // Token management
    bool is_token_expired() const;
    void schedule_token_refresh();
    
    // HTTP client for API calls
    bool make_auth_request(const std::string& url, const std::string& payload, std::string& response);
    
    // Helper methods
    bool validate_credentials() const;
    bool parse_auth_response(const std::string& response);
    bool parse_url(const std::string& url, std::string& host, uint16_t& port, std::string& path);
    bool refresh_oauth2_token();
    
private:
    std::string exchange_name_;
    AuthCredentials credentials_;
    bool authenticated_ = false;
    std::string session_id_;
    std::chrono::system_clock::time_point last_auth_time_;
    std::function<void(bool, const std::string&)> auth_callback_;
    
    // For automatic token refresh
    std::thread refresh_thread_;
    std::atomic<bool> stop_refresh_{false};
};

// NSE-specific authentication
class NSEAuthenticator : public ExchangeAuthenticator {
public:
    NSEAuthenticator() : ExchangeAuthenticator("NSE") {}
    
    // NSE login API endpoint
    static constexpr const char* NSE_LOGIN_URL = "https://api.nse.in/auth/login";
    static constexpr const char* NSE_TOKEN_REFRESH_URL = "https://api.nse.in/auth/refresh";
    
    // NSE-specific authentication flow
    bool login_with_credentials(const std::string& username, const std::string& password);
    bool login_with_certificate();
    
    // Get NSE session token
    std::string get_session_token() const;
    
    // Validate NSE session
    bool validate_session();
};

// BSE-specific authentication  
class BSEAuthenticator : public ExchangeAuthenticator {
public:
    BSEAuthenticator() : ExchangeAuthenticator("BSE") {}
    
    // BSE login API endpoint
    static constexpr const char* BSE_LOGIN_URL = "https://api.bseindia.com/auth/login";
    static constexpr const char* BSE_TOKEN_REFRESH_URL = "https://api.bseindia.com/auth/refresh";
    
    // BSE-specific authentication flow
    bool login_with_credentials(const std::string& username, const std::string& password);
    bool login_with_certificate();
    
    // Get BSE session token
    std::string get_session_token() const;
};

// Authentication manager for multiple exchanges
class MultiExchangeAuthManager {
public:
    MultiExchangeAuthManager();
    ~MultiExchangeAuthManager();
    
    // Initialize all configured exchanges
    bool initialize(const config::ConfigManager& config);
    
    // Get authenticator for specific exchange
    std::shared_ptr<ExchangeAuthenticator> get_authenticator(const std::string& exchange);
    
    // Authenticate all exchanges
    bool authenticate_all();
    
    // Check if all exchanges are authenticated
    bool all_authenticated() const;
    
    // Set global authentication callback
    void set_global_callback(std::function<void(const std::string&, bool, const std::string&)> callback);
    
private:
    std::unordered_map<std::string, std::shared_ptr<ExchangeAuthenticator>> authenticators_;
    std::function<void(const std::string&, bool, const std::string&)> global_callback_;
};

// Utility functions for secure credential handling
namespace auth_utils {
    // Encrypt/decrypt credentials for storage
    std::string encrypt_credentials(const std::string& data, const std::string& key);
    std::string decrypt_credentials(const std::string& encrypted_data, const std::string& key);
    
    // Generate secure random session ID
    std::string generate_session_id();
    
    // Generate HMAC signature
    std::string generate_hmac_signature(const std::string& message, const std::string& key);
    
    // Validate API key format
    bool validate_api_key_format(const std::string& api_key, const std::string& exchange);
    
    // Hash password securely
    std::string hash_password(const std::string& password, const std::string& salt);
    
    // Load credentials from secure storage
    bool load_credentials_from_keystore(const std::string& exchange, 
                                       ExchangeAuthenticator::AuthCredentials& credentials);
    
    // Store credentials in secure storage
    bool store_credentials_in_keystore(const std::string& exchange,
                                      const ExchangeAuthenticator::AuthCredentials& credentials);
}

} // namespace goldearn::network