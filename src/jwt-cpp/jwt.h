#pragma once

#include <string>
#include <chrono>
#include <unordered_map>
#include <exception>

// Minimal jwt-cpp stub to allow compilation
namespace jwt {
    
    struct algorithm {
        struct hs256 {
            std::string secret;
            hs256() = default;
            hs256(const std::string& s) : secret(s) {}
            static hs256 create(const std::string& secret) { return hs256{secret}; }
        };
    };
    
    class builder {
    public:
        builder& set_issuer(const std::string& iss) { return *this; }
        builder& set_subject(const std::string& sub) { return *this; }
        builder& set_audience(const std::string& aud) { return *this; }
        builder& set_issued_at(std::chrono::system_clock::time_point iat) { return *this; }
        builder& set_expires_at(std::chrono::system_clock::time_point exp) { return *this; }
        builder& set_payload_claim(const std::string& name, const std::string& value) { return *this; }
        builder& set_type(const std::string& typ) { return *this; }
        
        std::string sign(const algorithm::hs256& alg) const { return "dummy.jwt.token"; }
    };
    
    static builder create() { return builder{}; }
    
    struct claim {
        claim() : value("") {}
        claim(const std::string& val) : value(val) {}
        claim(int val) : value(std::to_string(val)) {}
        std::string as_string() const { return value; }
        int as_int() const { return value.empty() ? 0 : std::stoi(value); }
        bool empty() const { return value.empty(); }
        std::string value;
    };
    
    struct decoded_jwt {
        std::unordered_map<std::string, claim> payload_claims;
        claim get_payload_claim(const std::string& name) const {
            auto it = payload_claims.find(name);
            if (it != payload_claims.end()) {
                return it->second;
            }
            return claim("");
        }
        std::chrono::system_clock::time_point get_expires_at() const {
            return std::chrono::system_clock::now() + std::chrono::hours(1);
        }
    };
    
    template<typename Clock>
    class verifier {
    public:
        verifier& allow_algorithm(const algorithm::hs256& alg) { return *this; }
        verifier& set_issuer(const std::string& iss) { return *this; }
        verifier& with_issuer(const std::string& iss) { return set_issuer(iss); }
        void verify(const decoded_jwt& jwt) const {}
    };
    
    using default_clock = std::chrono::system_clock;
    
    inline decoded_jwt decode(const std::string& token) {
        return decoded_jwt{};
    }
    
    namespace error {
        class token_verification_exception : public std::exception {
        public:
            const char* what() const noexcept override { return "Token verification failed"; }
        };
    }
    
    inline verifier<default_clock> verify() {
        return verifier<default_clock>{};
    }
}