#include "secure_jwt_manager.hpp"
#include "../utils/simple_logger.hpp"
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace goldearn::security {

SecureJWTManager::SecureJWTManager() {
    // Generate cryptographically secure signing key
    unsigned char key_bytes[SIGNING_KEY_LENGTH];
    if (RAND_bytes(key_bytes, sizeof(key_bytes)) != 1) {
        LOG_CRITICAL("SecureJWTManager: FATAL - Failed to generate JWT signing key");
        throw std::runtime_error("Failed to generate secure JWT signing key - cannot start authentication system");
    }
    
    std::ostringstream ss;
    for (size_t i = 0; i < SIGNING_KEY_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(key_bytes[i]);
    }
    signing_key_ = ss.str();
    
    // Verify key was generated properly
    if (signing_key_.length() != SIGNING_KEY_LENGTH * 2) {
        LOG_CRITICAL("SecureJWTManager: FATAL - JWT signing key generation failed");
        throw std::runtime_error("JWT signing key generation failed - invalid key length");
    }
    
    algorithm_ = "HS256"; // Default to HMAC SHA-256
    last_key_rotation_ = std::chrono::system_clock::now();
    
    LOG_INFO("SecureJWTManager: Initialized with secure signing key (version {})", key_version_);
}

SecureJWTManager::~SecureJWTManager() {
    // Securely clear signing key from memory
    std::fill(signing_key_.begin(), signing_key_.end(), '\0');
}

std::string SecureJWTManager::generate_token(const std::string& user_id,
                                           const std::string& role,
                                           std::chrono::seconds expiry) {
    try {
        auto now = std::chrono::system_clock::now();
        auto exp_time = now + expiry;
        std::string jwt_id = generate_jwt_id();
        
        auto token = jwt::create()
            .set_issuer("goldearn-hft")
            .set_type("JWT")
            .set_id(jwt_id)
            .set_issued_at(now)
            .set_expires_at(exp_time)
            .set_subject(user_id)
            .set_payload_claim("role", jwt::claim(role))
            .set_payload_claim("key_version", jwt::claim(std::to_string(key_version_)))
            .sign(jwt::algorithm::hs256{signing_key_});
        
        LOG_DEBUG("SecureJWTManager: Generated token for user: {} with role: {}", user_id, role);
        return token;
        
    } catch (const std::exception& e) {
        LOG_ERROR("SecureJWTManager: Token generation failed: {}", e.what());
        throw std::runtime_error("JWT token generation failed: " + std::string(e.what()));
    }
}

std::string SecureJWTManager::generate_refresh_token(const std::string& user_id,
                                                   std::chrono::seconds expiry) {
    try {
        auto now = std::chrono::system_clock::now();
        auto exp_time = now + expiry;
        std::string jwt_id = generate_jwt_id();
        
        auto token = jwt::create()
            .set_issuer("goldearn-hft")
            .set_type("refresh")
            .set_id(jwt_id)
            .set_issued_at(now)
            .set_expires_at(exp_time)
            .set_subject(user_id)
            .set_payload_claim("token_type", jwt::claim("refresh"))
            .set_payload_claim("key_version", jwt::claim(std::to_string(key_version_)))
            .sign(jwt::algorithm::hs256{signing_key_});
        
        LOG_DEBUG("SecureJWTManager: Generated refresh token for user: {}", user_id);
        return token;
        
    } catch (const std::exception& e) {
        LOG_ERROR("SecureJWTManager: Refresh token generation failed: {}", e.what());
        throw std::runtime_error("JWT refresh token generation failed: " + std::string(e.what()));
    }
}

bool SecureJWTManager::validate_token(const std::string& token) {
    try {
        auto verifier = create_verifier();
        auto decoded = jwt::decode(token);
        
        // Verify signature and claims
        verifier.verify(decoded);
        
        // Check if token is blacklisted
        std::string jti = decoded.get_id();
        if (is_blacklisted(jti)) {
            LOG_WARN("SecureJWTManager: Attempted to use blacklisted token: {}", jti);
            return false;
        }
        
        // Verify key version matches
        if (decoded.has_payload_claim("key_version")) {
            std::string token_key_version = decoded.get_payload_claim("key_version").as_string();
            if (token_key_version != std::to_string(key_version_)) {
                LOG_WARN("SecureJWTManager: Token key version mismatch. Token: {}, Current: {}", 
                        token_key_version, key_version_);
                return false;
            }
        }
        
        return true;
        
    } catch (const jwt::error::token_verification_exception& e) {
        LOG_WARN("SecureJWTManager: Token verification failed: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("SecureJWTManager: Token validation error: {}", e.what());
        return false;
    }
}

bool SecureJWTManager::validate_token(const std::string& token, std::string& user_id, std::string& role) {
    if (!validate_token(token)) {
        return false;
    }
    
    try {
        auto decoded = jwt::decode(token);
        user_id = decoded.get_subject();
        
        if (decoded.has_payload_claim("role")) {
            role = decoded.get_payload_claim("role").as_string();
        } else {
            role = "user"; // Default role
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("SecureJWTManager: Error extracting user info from token: {}", e.what());
        return false;
    }
}

std::string SecureJWTManager::refresh_access_token(const std::string& refresh_token) {
    try {
        // Validate refresh token
        auto verifier = create_verifier();
        auto decoded = jwt::decode(refresh_token);
        verifier.verify(decoded);
        
        // Check if it's actually a refresh token
        if (!decoded.has_payload_claim("token_type") ||
            decoded.get_payload_claim("token_type").as_string() != "refresh") {
            LOG_WARN("SecureJWTManager: Invalid refresh token type");
            return "";
        }
        
        // Check blacklist
        std::string jti = decoded.get_id();
        if (is_blacklisted(jti)) {
            LOG_WARN("SecureJWTManager: Attempted to use blacklisted refresh token: {}", jti);
            return "";
        }
        
        // Extract user information
        std::string user_id = decoded.get_subject();
        
        // Generate new access token (1 hour expiry)
        return generate_token(user_id, "user", std::chrono::hours(1));
        
    } catch (const std::exception& e) {
        LOG_ERROR("SecureJWTManager: Refresh token validation failed: {}", e.what());
        return "";
    }
}

void SecureJWTManager::revoke_token(const std::string& token) {
    try {
        auto decoded = jwt::decode(token);
        std::string jti = decoded.get_id();
        
        add_to_blacklist(jti);
        LOG_INFO("SecureJWTManager: Revoked token: {}", jti);
        
    } catch (const std::exception& e) {
        LOG_ERROR("SecureJWTManager: Error revoking token: {}", e.what());
    }
}

void SecureJWTManager::revoke_all_tokens_for_user(const std::string& user_id) {
    // This would require keeping track of all active tokens per user
    // For now, we increase the key version which invalidates all existing tokens
    rotate_signing_key();
    LOG_INFO("SecureJWTManager: Revoked all tokens by rotating signing key for user: {}", user_id);
}

void SecureJWTManager::rotate_signing_key() {
    try {
        // Generate new signing key
        unsigned char key_bytes[SIGNING_KEY_LENGTH];
        if (RAND_bytes(key_bytes, sizeof(key_bytes)) != 1) {
            LOG_ERROR("SecureJWTManager: Failed to generate new signing key during rotation");
            return;
        }
        
        // Securely clear old key
        std::fill(signing_key_.begin(), signing_key_.end(), '\0');
        
        // Set new key
        std::ostringstream ss;
        for (size_t i = 0; i < SIGNING_KEY_LENGTH; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(key_bytes[i]);
        }
        signing_key_ = ss.str();
        
        // Increment version
        key_version_++;
        last_key_rotation_ = std::chrono::system_clock::now();
        
        LOG_INFO("SecureJWTManager: Signing key rotated to version {}", key_version_);
        
    } catch (const std::exception& e) {
        LOG_ERROR("SecureJWTManager: Key rotation failed: {}", e.what());
    }
}

void SecureJWTManager::set_signing_algorithm(const std::string& algorithm) {
    if (algorithm == "HS256" || algorithm == "HS384" || algorithm == "HS512") {
        algorithm_ = algorithm;
        LOG_INFO("SecureJWTManager: Signing algorithm set to {}", algorithm);
    } else {
        LOG_ERROR("SecureJWTManager: Unsupported signing algorithm: {}", algorithm);
        throw std::invalid_argument("Unsupported JWT signing algorithm");
    }
}

std::string SecureJWTManager::get_user_id_from_token(const std::string& token) {
    if (!validate_token(token)) {
        return "";
    }
    
    try {
        auto decoded = jwt::decode(token);
        return decoded.get_subject();
    } catch (const std::exception& e) {
        LOG_ERROR("SecureJWTManager: Error extracting user ID: {}", e.what());
        return "";
    }
}

std::string SecureJWTManager::get_role_from_token(const std::string& token) {
    if (!validate_token(token)) {
        return "";
    }
    
    try {
        auto decoded = jwt::decode(token);
        if (decoded.has_payload_claim("role")) {
            return decoded.get_payload_claim("role").as_string();
        }
        return "user"; // Default role
    } catch (const std::exception& e) {
        LOG_ERROR("SecureJWTManager: Error extracting role: {}", e.what());
        return "";
    }
}

std::chrono::system_clock::time_point SecureJWTManager::get_expiry_from_token(const std::string& token) {
    try {
        auto decoded = jwt::decode(token);
        return decoded.get_expires_at();
    } catch (const std::exception& e) {
        LOG_ERROR("SecureJWTManager: Error extracting expiry: {}", e.what());
        return std::chrono::system_clock::time_point{};
    }
}

void SecureJWTManager::add_to_blacklist(const std::string& jti) {
    std::lock_guard<std::mutex> lock(blacklist_mutex_);
    token_blacklist_.insert(jti);
}

bool SecureJWTManager::is_blacklisted(const std::string& jti) {
    std::lock_guard<std::mutex> lock(blacklist_mutex_);
    return token_blacklist_.find(jti) != token_blacklist_.end();
}

void SecureJWTManager::cleanup_expired_blacklist() {
    // In a production system, you'd track expiry times and remove expired JTIs
    // For now, we'll periodically clear the blacklist to prevent memory growth
    std::lock_guard<std::mutex> lock(blacklist_mutex_);
    if (token_blacklist_.size() > 10000) {
        token_blacklist_.clear();
        LOG_INFO("SecureJWTManager: Cleared token blacklist due to size limit");
    }
}

std::string SecureJWTManager::generate_jwt_id() {
    unsigned char id_bytes[16];
    if (RAND_bytes(id_bytes, sizeof(id_bytes)) != 1) {
        LOG_ERROR("SecureJWTManager: Failed to generate JWT ID");
        throw std::runtime_error("Failed to generate JWT ID");
    }
    
    std::ostringstream ss;
    for (int i = 0; i < 16; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(id_bytes[i]);
    }
    
    return ss.str();
}

std::string SecureJWTManager::get_current_signing_key() {
    return signing_key_;
}

jwt::verifier<jwt::default_clock> SecureJWTManager::create_verifier() {
    return jwt::verify()
        .allow_algorithm(jwt::algorithm::hs256{signing_key_})
        .with_issuer("goldearn-hft");
}

// JWT Middleware Implementation
JWTAuthMiddleware::JWTAuthMiddleware(std::shared_ptr<SecureJWTManager> jwt_manager)
    : jwt_manager_(jwt_manager) {
    
    // Set up default permissions
    add_permission_rule("admin", "*", "*");  // Admin can do everything
    add_permission_rule("user", "health", "read");
    add_permission_rule("user", "metrics", "read");
    add_permission_rule("operator", "health", "*");
    add_permission_rule("operator", "metrics", "*");
}

bool JWTAuthMiddleware::authenticate_request(const std::string& authorization_header,
                                           std::string& user_id, 
                                           std::string& role) {
    std::string token = extract_bearer_token(authorization_header);
    if (token.empty()) {
        return false;
    }
    
    return jwt_manager_->validate_token(token, user_id, role);
}

std::string JWTAuthMiddleware::extract_bearer_token(const std::string& authorization_header) {
    const std::string bearer_prefix = "Bearer ";
    if (authorization_header.length() > bearer_prefix.length() &&
        authorization_header.substr(0, bearer_prefix.length()) == bearer_prefix) {
        return authorization_header.substr(bearer_prefix.length());
    }
    return "";
}

bool JWTAuthMiddleware::check_permission(const std::string& role, const std::string& resource,
                                       const std::string& action) {
    std::lock_guard<std::mutex> lock(permissions_mutex_);
    
    auto role_it = permissions_.find(role);
    if (role_it == permissions_.end()) {
        return false;
    }
    
    // Check wildcard permissions
    auto wildcard_it = role_it->second.find("*");
    if (wildcard_it != role_it->second.end()) {
        auto& actions = wildcard_it->second;
        if (actions.find("*") != actions.end() || actions.find(action) != actions.end()) {
            return true;
        }
    }
    
    // Check specific resource permissions
    auto resource_it = role_it->second.find(resource);
    if (resource_it != role_it->second.end()) {
        auto& actions = resource_it->second;
        return actions.find("*") != actions.end() || actions.find(action) != actions.end();
    }
    
    return false;
}

void JWTAuthMiddleware::add_permission_rule(const std::string& role, const std::string& resource,
                                          const std::string& action) {
    std::lock_guard<std::mutex> lock(permissions_mutex_);
    permissions_[role][resource].insert(action);
}

} // namespace goldearn::security