#pragma once

#include <jwt-cpp/jwt.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace goldearn::security {

// JWT token manager using proven jwt-cpp library
class SecureJWTManager {
   public:
    SecureJWTManager();
    ~SecureJWTManager();

    // Token generation
    std::string generate_token(const std::string& user_id,
                               const std::string& role = "user",
                               std::chrono::seconds expiry = std::chrono::hours(1));

    std::string generate_refresh_token(const std::string& user_id,
                                       std::chrono::seconds expiry = std::chrono::hours(24 * 7));

    // Token validation
    bool validate_token(const std::string& token);
    bool validate_token(const std::string& token, std::string& user_id, std::string& role);

    // Token refresh
    std::string refresh_access_token(const std::string& refresh_token);

    // Token revocation
    void revoke_token(const std::string& token);
    void revoke_all_tokens_for_user(const std::string& user_id);

    // Key management
    void rotate_signing_key();
    void set_signing_algorithm(const std::string& algorithm = "HS256");

    // Token introspection
    std::string get_user_id_from_token(const std::string& token);
    std::string get_role_from_token(const std::string& token);
    std::chrono::system_clock::time_point get_expiry_from_token(const std::string& token);

    // Token information extraction
    std::string get_user_id(const std::string& token);
    std::string get_user_role(const std::string& token);
    std::chrono::system_clock::time_point get_expiration_time(const std::string& token);

    // Blacklist management
    void add_to_blacklist(const std::string& jti);  // JWT ID
    bool is_blacklisted(const std::string& jti);
    void cleanup_expired_blacklist();

   private:
    std::string generate_jwt_id();
    std::string get_current_signing_key();
    jwt::verifier<jwt::default_clock> create_verifier();

   private:
    std::string signing_key_;
    std::string algorithm_;

    // Token blacklist for revoked tokens
    std::unordered_set<std::string> token_blacklist_;
    mutable std::mutex blacklist_mutex_;

    // Key rotation
    uint32_t key_version_{1};
    std::chrono::system_clock::time_point last_key_rotation_;

    // Configuration
    static constexpr std::chrono::hours KEY_ROTATION_INTERVAL{24 * 7};  // 7 days
    static constexpr size_t SIGNING_KEY_LENGTH = 64;                    // 512 bits
};

// JWT middleware for authentication
class JWTAuthMiddleware {
   public:
    JWTAuthMiddleware(std::shared_ptr<SecureJWTManager> jwt_manager);

    // HTTP request authentication
    bool authenticate_request(const std::string& authorization_header,
                              std::string& user_id,
                              std::string& role);

    // Extract token from different header formats
    std::string extract_bearer_token(const std::string& authorization_header);

    // Role-based access control
    bool check_permission(const std::string& role,
                          const std::string& resource,
                          const std::string& action);

    // Add permission rules
    void add_permission_rule(const std::string& role,
                             const std::string& resource,
                             const std::string& action);

   private:
    std::shared_ptr<SecureJWTManager> jwt_manager_;

    // Permission mapping: role -> resource -> actions
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::unordered_set<std::string>>>
        permissions_;
    mutable std::mutex permissions_mutex_;
};

}  // namespace goldearn::security