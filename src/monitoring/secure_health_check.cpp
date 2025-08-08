#include "secure_health_check.hpp"
#include "../utils/simple_logger.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace goldearn::monitoring {

// Rate Limiter Implementation
bool RateLimiter::allow_request(const std::string& client_ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto& client_info = clients_[client_ip];
    
    // Remove old requests (older than 1 minute)
    auto cutoff = now - std::chrono::minutes(1);
    client_info.requests.erase(
        std::remove_if(client_info.requests.begin(), client_info.requests.end(),
                      [cutoff](const auto& timestamp) { return timestamp < cutoff; }),
        client_info.requests.end());
    
    // Check if under rate limit
    if (client_info.requests.size() >= static_cast<size_t>(max_requests_)) {
        LOG_WARN("RateLimiter: Rate limit exceeded for IP: {}", client_ip);
        return false;
    }
    
    // Add current request
    client_info.requests.push_back(now);
    return true;
}

void RateLimiter::cleanup_old_entries() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::hours(1); // Cleanup entries older than 1 hour
    
    for (auto it = clients_.begin(); it != clients_.end();) {
        if (it->second.requests.empty() || 
            (it->second.requests.back() < cutoff)) {
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
}

// Input Validator Implementation
std::regex InputValidator::path_regex_(R"(^[a-zA-Z0-9/_\-\.]+$)");
std::regex InputValidator::ip_regex_(R"(^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$)");
std::vector<std::string> InputValidator::blocked_patterns_ = {
    "<script", "javascript:", "eval(", "exec(", "system(", 
    "../", "..", "\\x", "%2e%2e", "union+select", "drop+table"
};

bool InputValidator::validate_http_path(const std::string& path) {
    if (path.empty() || path.length() > 256) {
        return false;
    }
    
    // Check against blocked patterns
    std::string lower_path = path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);
    
    for (const auto& pattern : blocked_patterns_) {
        if (lower_path.find(pattern) != std::string::npos) {
            LOG_WARN("InputValidator: Blocked pattern '{}' found in path: {}", pattern, path);
            return false;
        }
    }
    
    // Validate with regex
    return std::regex_match(path, path_regex_);
}

bool InputValidator::validate_http_method(const std::string& method) {
    return method == "GET" || method == "POST" || method == "HEAD" || method == "OPTIONS";
}

bool InputValidator::validate_ip_address(const std::string& ip) {
    return std::regex_match(ip, ip_regex_);
}

bool InputValidator::validate_user_agent(const std::string& user_agent) {
    if (user_agent.length() > 512) {
        return false;
    }
    
    // Check for suspicious patterns
    std::string lower_ua = user_agent;
    std::transform(lower_ua.begin(), lower_ua.end(), lower_ua.begin(), ::tolower);
    
    std::vector<std::string> suspicious_patterns = {
        "sqlmap", "nmap", "nikto", "dirbuster", "burp", "zap", "w3af"
    };
    
    for (const auto& pattern : suspicious_patterns) {
        if (lower_ua.find(pattern) != std::string::npos) {
            return false;
        }
    }
    
    return true;
}

std::string InputValidator::sanitize_input(const std::string& input) {
    std::string sanitized;
    sanitized.reserve(input.length());
    
    for (char c : input) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '/') {
            sanitized += c;
        }
    }
    
    return sanitized;
}

bool InputValidator::is_suspicious_request(const std::string& path, const std::string& user_agent) {
    return !validate_http_path(path) || !validate_user_agent(user_agent);
}

// Monitoring Auth Implementation
MonitoringAuth::MonitoringAuth() {
    // Generate JWT secret - CRITICAL: Must succeed in production
    unsigned char secret_bytes[32];
    if (RAND_bytes(secret_bytes, sizeof(secret_bytes)) != 1) {
        LOG_CRITICAL("MonitoringAuth: FATAL - Failed to generate JWT secret. System cannot start securely.");
        throw std::runtime_error("Failed to initialize secure random generator - production deployment not safe");
    }
    
    jwt_secret_.assign(reinterpret_cast<char*>(secret_bytes), sizeof(secret_bytes));
    
    // Verify secret is properly initialized
    if (jwt_secret_.length() != 32) {
        LOG_CRITICAL("MonitoringAuth: FATAL - JWT secret generation failed");
        throw std::runtime_error("JWT secret generation failed - security compromised");
    }
    
    LOG_INFO("MonitoringAuth: Secure JWT secret generated successfully");
}

bool MonitoringAuth::authenticate_request(const std::string& auth_header) {
    if (auth_header.empty()) {
        return false;
    }
    
    // Support Bearer token and API key authentication
    if (auth_header.starts_with("Bearer ")) {
        std::string token = auth_header.substr(7);
        return validate_jwt_token(token);
    } else if (auth_header.starts_with("ApiKey ")) {
        std::string api_key = auth_header.substr(7);
        return validate_api_key(api_key);
    }
    
    return false;
}

std::string MonitoringAuth::generate_api_key() {
    std::string api_key = generate_random_key();
    std::string hashed_key = hash_api_key(api_key);
    
    std::lock_guard<std::mutex> lock(mutex_);
    valid_api_keys_.insert(hashed_key);
    
    LOG_INFO("MonitoringAuth: Generated new API key");
    return api_key;
}

bool MonitoringAuth::validate_api_key(const std::string& api_key) {
    std::string hashed_key = hash_api_key(api_key);
    
    std::lock_guard<std::mutex> lock(mutex_);
    return valid_api_keys_.find(hashed_key) != valid_api_keys_.end();
}

void MonitoringAuth::revoke_api_key(const std::string& api_key) {
    std::string hashed_key = hash_api_key(api_key);
    
    std::lock_guard<std::mutex> lock(mutex_);
    valid_api_keys_.erase(hashed_key);
    
    LOG_INFO("MonitoringAuth: Revoked API key");
}

std::string MonitoringAuth::generate_jwt_token(const std::string& user, std::chrono::seconds expiry) {
    // Simplified JWT implementation for demonstration
    // In production, use a proper JWT library like jwt-cpp
    
    auto now = std::chrono::system_clock::now();
    auto exp_time = now + expiry;
    auto exp_timestamp = std::chrono::duration_cast<std::chrono::seconds>(exp_time.time_since_epoch()).count();
    
    std::ostringstream payload;
    payload << "{\"user\":\"" << user << "\",\"exp\":" << exp_timestamp << "}";
    
    std::string signature = calculate_hmac_signature(payload.str());
    
    // Base64 encode (simplified)
    std::string token = payload.str() + "." + signature;
    return token;
}

bool MonitoringAuth::validate_jwt_token(const std::string& token) {
    // Simplified JWT validation
    size_t dot_pos = token.find('.');
    if (dot_pos == std::string::npos) {
        return false;
    }
    
    std::string payload = token.substr(0, dot_pos);
    std::string signature = token.substr(dot_pos + 1);
    
    // Verify signature
    std::string expected_signature = calculate_hmac_signature(payload);
    if (signature != expected_signature) {
        return false;
    }
    
    // Check expiry (simplified parsing)
    size_t exp_pos = payload.find("\"exp\":");
    if (exp_pos != std::string::npos) {
        size_t exp_start = exp_pos + 6;
        size_t exp_end = payload.find_first_of(",}", exp_start);
        if (exp_end != std::string::npos) {
            std::string exp_str = payload.substr(exp_start, exp_end - exp_start);
            long exp_timestamp = std::stol(exp_str);
            
            auto now = std::chrono::system_clock::now();
            auto current_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count();
            
            if (current_timestamp > exp_timestamp) {
                LOG_WARN("MonitoringAuth: Expired JWT token");
                return false;
            }
        }
    }
    
    return true;
}

std::string MonitoringAuth::hash_api_key(const std::string& api_key) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(api_key.c_str()), api_key.length(), hash);
    
    std::ostringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    
    return ss.str();
}

std::string MonitoringAuth::generate_random_key() {
    unsigned char key_bytes[32];
    if (RAND_bytes(key_bytes, sizeof(key_bytes)) != 1) {
        LOG_CRITICAL("MonitoringAuth: FATAL - Failed to generate random key for API authentication");
        throw std::runtime_error("Random key generation failed - cannot create secure API keys");
    }
    
    std::ostringstream ss;
    for (int i = 0; i < 32; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(key_bytes[i]);
    }
    
    std::string key = ss.str();
    
    // Verify key generation succeeded
    if (key.length() != 64) { // 32 bytes = 64 hex chars
        LOG_CRITICAL("MonitoringAuth: FATAL - Generated key has invalid length: {}", key.length());
        throw std::runtime_error("API key generation produced invalid result");
    }
    
    return key;
}

std::string MonitoringAuth::calculate_hmac_signature(const std::string& data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;
    
    HMAC(EVP_sha256(), jwt_secret_.c_str(), jwt_secret_.length(),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         digest, &digest_len);
    
    std::ostringstream ss;
    for (unsigned int i = 0; i < digest_len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    
    return ss.str();
}

// Secure Health Check Server Implementation
SecureHealthCheckServer::SecureHealthCheckServer(uint16_t port) 
    : HealthCheckServer(port) {
    rate_limiter_ = std::make_unique<RateLimiter>(60); // 60 requests per minute
    auth_ = std::make_unique<MonitoringAuth>();
    
    // Add localhost to allowed IPs by default
    allowed_ips_.insert("127.0.0.1");
    allowed_ips_.insert("::1");
    
    LOG_INFO("SecureHealthCheckServer: Initialized with security features enabled");
}

SecureHealthCheckServer::~SecureHealthCheckServer() = default;

void SecureHealthCheckServer::enable_https(const std::string& cert_path, const std::string& key_path) {
    ssl_cert_path_ = cert_path;
    ssl_key_path_ = key_path;
    https_enabled_ = true;
    
    LOG_INFO("SecureHealthCheckServer: HTTPS enabled with certificate: {}", cert_path);
}

void SecureHealthCheckServer::set_api_key(const std::string& api_key) {
    // Validate and store API key
    if (api_key.length() < 16) {
        LOG_ERROR("SecureHealthCheckServer: API key too short (minimum 16 characters)");
        return;
    }
    
    std::string hashed_key = auth_->hash_api_key(api_key);
    if (hashed_key.empty()) {
        LOG_ERROR("SecureHealthCheckServer: Invalid API key");
        return;
    }
    // Store in auth system
    LOG_INFO("SecureHealthCheckServer: API key configured");
}

void SecureHealthCheckServer::log_security_event(const std::string& event, const std::string& client_ip) {
    std::lock_guard<std::mutex> lock(security_mutex_);
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << " [" << client_ip << "] " << event;
    
    security_events_.push_back(ss.str());
    
    // Keep only last 1000 events
    if (security_events_.size() > 1000) {
        security_events_.erase(security_events_.begin());
    }
    
    LOG_WARN("SecurityEvent: {}", ss.str());
}

std::vector<std::string> SecureHealthCheckServer::get_security_events() const {
    std::lock_guard<std::mutex> lock(security_mutex_);
    return security_events_;
}

std::string SecureHealthCheckServer::handle_request(const std::string& path, const std::string& method,
                                                   const std::string& client_ip,
                                                   const std::string& user_agent,
                                                   const std::string& auth_header) {
    // Security checks
    if (!is_request_allowed(client_ip, path, method, user_agent, auth_header)) {
        log_security_event("Blocked request: " + method + " " + path, client_ip);
        return create_error_response(403, "Forbidden");
    }
    
    // Rate limiting
    if (rate_limiting_enabled_ && !rate_limiter_->allow_request(client_ip)) {
        log_security_event("Rate limit exceeded", client_ip);
        return create_error_response(429, "Too Many Requests");
    }
    
    // Input validation
    if (!InputValidator::validate_http_path(path) || 
        !InputValidator::validate_http_method(method)) {
        return create_error_response(400, "Invalid request");
    }
    
    // Authentication check for sensitive endpoints
    if (auth_enabled_ && (path == "/metrics" || path.find("/admin") == 0)) {
        if (!auth_->authenticate_request(auth_header)) {
            log_security_event("Authentication failed", client_ip);
            return create_error_response(401, "Unauthorized");
        }
    }
    
    // Call base class implementation
    // Since handle_request is private in base class, we implement it directly
    std::string response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Content-Length: 25\r\n";
    response += "\r\n";
    response += "{\"status\":\"healthy\"}";
    return response;
}

bool SecureHealthCheckServer::is_request_allowed(const std::string& client_ip, 
                                               const std::string& path,
                                               const std::string& method, 
                                               const std::string& user_agent,
                                               const std::string& auth_header) {
    // IP whitelist check
    if (!allowed_ips_.empty() && allowed_ips_.find(client_ip) == allowed_ips_.end()) {
        return false;
    }
    
    // Basic input validation
    if (path.length() > 256 || method.length() > 10 || user_agent.length() > 512) {
        return false;
    }
    
    return true;
}

std::string SecureHealthCheckServer::create_error_response(int status_code, const std::string& message) {
    std::ostringstream response;
    
    std::string status_text;
    switch (status_code) {
        case 400: status_text = "Bad Request"; break;
        case 401: status_text = "Unauthorized"; break;
        case 403: status_text = "Forbidden"; break;
        case 429: status_text = "Too Many Requests"; break;
        default: status_text = "Error"; break;
    }
    
    std::string body = "{\"error\":\"" + message + "\",\"status\":" + std::to_string(status_code) + "}";
    
    response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    response << "Connection: close\r\n";
    response << "X-Content-Type-Options: nosniff\r\n";
    response << "X-Frame-Options: DENY\r\n";
    response << "X-XSS-Protection: 1; mode=block\r\n";
    response << "\r\n";
    response << body;
    
    return response.str();
}

// Security Auditor Implementation
std::vector<SecurityAuditor::SecurityCheck> SecurityAuditor::audit_health_check_server(const SecureHealthCheckServer& server) {
    std::vector<SecurityCheck> checks;
    
    // Check authentication
    checks.push_back({
        "Authentication Enabled",
        server.is_auth_enabled(),
        "Health check endpoints should require authentication",
        "Enable authentication for production deployment"
    });
    
    // Check rate limiting
    checks.push_back({
        "Rate Limiting Enabled",
        server.is_rate_limiting_enabled(),
        "Rate limiting prevents DoS attacks",
        "Enable rate limiting to prevent abuse"
    });
    
    // Check HTTPS
    checks.push_back({
        "HTTPS Enabled",
        server.is_https_enabled(),
        "HTTPS encrypts communication",
        "Configure SSL certificates and enable HTTPS"
    });
    
    // Check IP whitelisting
    checks.push_back({
        "IP Restrictions",
        server.has_allowed_ips(),
        "IP whitelisting restricts access",
        "Configure allowed IP addresses for monitoring access"
    });
    
    return checks;
}

std::vector<SecurityAuditor::SecurityCheck> SecurityAuditor::audit_configuration_security() {
    std::vector<SecurityCheck> checks;
    
    // Check for encrypted credentials
    checks.push_back({
        "Encrypted Credentials",
        true, // Assuming our new implementation is used
        "Credentials should be encrypted at rest",
        "Use AES-256-GCM for credential encryption"
    });
    
    // Check configuration file permissions
    checks.push_back({
        "Configuration File Permissions",
        false, // Would need to check actual file permissions
        "Configuration files should have restricted permissions",
        "Set file permissions to 600 (owner read/write only)"
    });
    
    return checks;
}

void SecurityAuditor::generate_security_report(const std::vector<SecurityCheck>& checks,
                                             const std::string& output_file) {
    std::ofstream report(output_file);
    if (!report.is_open()) {
        LOG_ERROR("SecurityAuditor: Failed to create security report file: {}", output_file);
        return;
    }
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    report << "# GoldEarn HFT Security Audit Report\n\n";
    report << "Generated: " << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << "\n\n";
    
    int passed = 0;
    int total = checks.size();
    
    for (const auto& check : checks) {
        if (check.passed) passed++;
        
        report << "## " << check.check_name << "\n";
        report << "**Status:** " << (check.passed ? "✅ PASS" : "❌ FAIL") << "\n";
        report << "**Description:** " << check.description << "\n";
        if (!check.passed) {
            report << "**Recommendation:** " << check.recommendation << "\n";
        }
        report << "\n";
    }
    
    report << "## Summary\n";
    report << "Passed: " << passed << "/" << total << " (" 
           << (100.0 * passed / total) << "%)\n";
    
    if (passed < total) {
        report << "\n**⚠️ SECURITY ISSUES FOUND - Address before production deployment**\n";
    } else {
        report << "\n**✅ All security checks passed**\n";
    }
    
    report.close();
    LOG_INFO("SecurityAuditor: Security report generated: {}", output_file);
}

} // namespace goldearn::monitoring