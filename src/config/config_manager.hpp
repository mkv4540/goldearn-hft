#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace goldearn::config {

// Configuration value that can hold different types
class ConfigValue {
public:
    enum Type { STRING, INTEGER, DOUBLE, BOOLEAN, ARRAY };
    
    ConfigValue() : type_(STRING), str_value_("") {}
    explicit ConfigValue(const std::string& value) : type_(STRING), str_value_(value) {}
    explicit ConfigValue(int64_t value) : type_(INTEGER), int_value_(value) {}
    explicit ConfigValue(double value) : type_(DOUBLE), double_value_(value) {}
    explicit ConfigValue(bool value) : type_(BOOLEAN), bool_value_(value) {}
    explicit ConfigValue(const std::vector<std::string>& value) : type_(ARRAY), array_value_(value) {}
    
    // Getters with type conversion
    std::string as_string() const {
        switch (type_) {
            case STRING: return str_value_;
            case INTEGER: return std::to_string(int_value_);
            case DOUBLE: return std::to_string(double_value_);
            case BOOLEAN: return bool_value_ ? "true" : "false";
            case ARRAY: return array_value_.empty() ? "" : array_value_[0];
        }
        return "";
    }
    
    int64_t as_int() const {
        switch (type_) {
            case STRING: return std::stoll(str_value_);
            case INTEGER: return int_value_;
            case DOUBLE: return static_cast<int64_t>(double_value_);
            case BOOLEAN: return bool_value_ ? 1 : 0;
            case ARRAY: return array_value_.empty() ? 0 : std::stoll(array_value_[0]);
        }
        return 0;
    }
    
    double as_double() const {
        switch (type_) {
            case STRING: return std::stod(str_value_);
            case INTEGER: return static_cast<double>(int_value_);
            case DOUBLE: return double_value_;
            case BOOLEAN: return bool_value_ ? 1.0 : 0.0;
            case ARRAY: return array_value_.empty() ? 0.0 : std::stod(array_value_[0]);
        }
        return 0.0;
    }
    
    bool as_bool() const {
        switch (type_) {
            case STRING: return str_value_ == "true" || str_value_ == "1" || str_value_ == "yes";
            case INTEGER: return int_value_ != 0;
            case DOUBLE: return double_value_ != 0.0;
            case BOOLEAN: return bool_value_;
            case ARRAY: return !array_value_.empty();
        }
        return false;
    }
    
    std::vector<std::string> as_array() const {
        if (type_ == ARRAY) return array_value_;
        return {as_string()};
    }
    
private:
    Type type_;
    std::string str_value_;
    int64_t int_value_ = 0;
    double double_value_ = 0.0;
    bool bool_value_ = false;
    std::vector<std::string> array_value_;
};

// Configuration section
class ConfigSection {
public:
    void set(const std::string& key, const ConfigValue& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        values_[key] = value;
    }
    
    ConfigValue get(const std::string& key, const ConfigValue& default_value = ConfigValue()) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = values_.find(key);
        return it != values_.end() ? it->second : default_value;
    }
    
    bool has(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return values_.find(key) != values_.end();
    }
    
    std::vector<std::string> get_keys() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> keys;
        for (const auto& pair : values_) {
            keys.push_back(pair.first);
        }
        return keys;
    }
    
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ConfigValue> values_;
};

// Main configuration manager
class ConfigManager {
public:
    static ConfigManager& instance() {
        static ConfigManager instance;
        return instance;
    }
    
    // Load configuration from file
    bool load_from_file(const std::string& filename);
    
    // Load from environment variables
    void load_from_environment();
    
    // Get configuration section
    std::shared_ptr<ConfigSection> get_section(const std::string& section_name);
    
    // Additional methods for full key access
    std::string get_string(const std::string& full_key, const std::string& default_value = "") const;
    int64_t get_int(const std::string& full_key, int64_t default_value = 0) const;
    double get_double(const std::string& full_key, double default_value = 0.0) const;
    bool get_bool(const std::string& full_key, bool default_value = false) const;
    
    // Check if section exists
    bool has_section(const std::string& section_name) const;
    std::vector<std::string> get_section_names() const;
    
    // Convenience getters (non-const for compatibility)
    std::string get_string(const std::string& section, const std::string& key, 
                          const std::string& default_value = "") {
        auto sec = get_section(section);
        return sec->get(key, ConfigValue(default_value)).as_string();
    }
    
    // Const version needed by get_environment
    std::string get_string(const std::string& section, const std::string& key, 
                          const std::string& default_value = "") const {
        std::shared_lock<std::shared_mutex> lock(sections_mutex_);
        auto it = sections_.find(section);
        if (it != sections_.end()) {
            return it->second->get(key, ConfigValue(default_value)).as_string();
        }
        return default_value;
    }
    
    int64_t get_int(const std::string& section, const std::string& key, 
                   int64_t default_value = 0) const {
        std::shared_lock<std::shared_mutex> lock(sections_mutex_);
        auto it = sections_.find(section);
        if (it != sections_.end()) {
            return it->second->get(key, ConfigValue(default_value)).as_int();
        }
        return default_value;
    }
    
    double get_double(const std::string& section, const std::string& key, 
                     double default_value = 0.0) {
        auto sec = get_section(section);
        return sec->get(key, ConfigValue(default_value)).as_double();
    }
    
    bool get_bool(const std::string& section, const std::string& key, 
                 bool default_value = false) {
        auto sec = get_section(section);
        return sec->get(key, ConfigValue(default_value)).as_bool();
    }
    
    // Set values (dot notation support)
    void set_value(const std::string& full_key, const std::string& value);
    
    void set_string(const std::string& section, const std::string& key, 
                   const std::string& value) {
        auto sec = get_section(section);
        sec->set(key, ConfigValue(value));
    }
    
    // Get values with dot notation
    std::string get_value(const std::string& full_key, const std::string& default_value = "") {
        size_t dot_pos = full_key.find('.');
        if (dot_pos != std::string::npos) {
            std::string section_name = full_key.substr(0, dot_pos);
            std::string key = full_key.substr(dot_pos + 1);
            return get_string(section_name, key, default_value);
        } else {
            return get_string("default", full_key, default_value);
        }
    }
    
    int64_t get_int(const std::string& full_key, int64_t default_value = 0) {
        size_t dot_pos = full_key.find('.');
        if (dot_pos != std::string::npos) {
            std::string section_name = full_key.substr(0, dot_pos);
            std::string key = full_key.substr(dot_pos + 1);
            return get_int(section_name, key, default_value);
        } else {
            return get_int("default", full_key, default_value);
        }
    }
    
    double get_double(const std::string& full_key, double default_value = 0.0) {
        size_t dot_pos = full_key.find('.');
        if (dot_pos != std::string::npos) {
            std::string section_name = full_key.substr(0, dot_pos);
            std::string key = full_key.substr(dot_pos + 1);
            return get_double(section_name, key, default_value);
        } else {
            return get_double("default", full_key, default_value);
        }
    }
    
    bool get_bool(const std::string& full_key, bool default_value = false) {
        size_t dot_pos = full_key.find('.');
        if (dot_pos != std::string::npos) {
            std::string section_name = full_key.substr(0, dot_pos);
            std::string key = full_key.substr(dot_pos + 1);
            return get_bool(section_name, key, default_value);
        } else {
            return get_bool("default", full_key, default_value);
        }
    }
    
    // Check if section exists  
    bool has_section(const std::string& section_name) {
        std::shared_lock<std::shared_mutex> lock(sections_mutex_);
        return sections_.find(section_name) != sections_.end();
    }
    
    // Reload configuration
    bool reload();
    
    // Validate configuration
    bool validate() const;
    
    // Get environment (development, testing, production)
    std::string get_environment() const;
    
    // Check if in production mode
    bool is_production() const {
        return get_environment() == "production";
    }
    
private:
    ConfigManager() = default;
    
    bool parse_ini_file(const std::string& filename);
    bool parse_json_file(const std::string& filename);
    std::string trim(const std::string& str);
    
    // Secure JSON parsing methods using nlohmann/json
    bool parse_json_object(const nlohmann::json& json_obj, const std::string& prefix);
    
    // Add missing ConfigValue methods
    ConfigValue json_to_config_value(const nlohmann::json& json_val);
    bool validate_json_structure(const nlohmann::json& json_data);
    bool validate_json_depth(const nlohmann::json& json_data, int current_depth, int max_depth);
    bool validate_config_key(const std::string& key);
    
    // Secure credential encryption/decryption
    bool encrypt_credential(const std::string& plaintext, std::string& encrypted_data);
    bool decrypt_credential(const std::string& encrypted_data, std::string& plaintext);
    std::string calculate_secure_hmac(const std::string& data, const std::string& key);
    
    // Base64 encoding/decoding
    std::string base64_encode(const std::string& input);
    std::string base64_decode(const std::string& input);
    
    // Security constants
    static constexpr int MAX_JSON_DEPTH = 10;
    static constexpr size_t MAX_CONFIG_KEY_LENGTH = 128;
    static constexpr size_t MAX_CONFIG_VALUE_LENGTH = 8192;
    
    mutable std::shared_mutex sections_mutex_;
    std::unordered_map<std::string, std::shared_ptr<ConfigSection>> sections_;
    std::string filename_;
    std::filesystem::file_time_type last_modified_;
};

// Production configuration templates
class ProductionConfig {
public:
    // NSE production endpoints
    static constexpr const char* NSE_PROD_HOST = "feed.nse.in";
    static constexpr uint16_t NSE_PROD_PORT = 9899;
    static constexpr const char* NSE_BACKUP_HOST = "backup-feed.nse.in";
    
    // BSE production endpoints  
    static constexpr const char* BSE_PROD_HOST = "feed.bseindia.com";
    static constexpr uint16_t BSE_PROD_PORT = 9898;
    
    // Risk limits for production
    static constexpr double PROD_MAX_DAILY_LOSS = 10000000.0;  // 10M INR
    static constexpr double PROD_MAX_POSITION_VALUE = 50000000.0;  // 50M INR
    static constexpr double PROD_MAX_ORDER_VALUE = 5000000.0;   // 5M INR
    
    // Rate limits for production
    static constexpr uint32_t PROD_MAX_ORDER_RATE = 1000;      // orders/sec
    static constexpr uint32_t PROD_MAX_MESSAGE_RATE = 50000;   // messages/sec
    
    // Create production configuration template
    static void create_production_template(const std::string& filename);
    
    // Create development configuration template
    static void create_development_template(const std::string& filename);
    
    // Validate production configuration
    static bool validate_production_config(const ConfigManager& config);
};

// Authentication configuration
class AuthConfig {
public:
    struct ExchangeAuth {
        std::string username;
        std::string password;
        std::string api_key;
        std::string secret_key;
        std::string session_token;
        std::string certificate_path;
        std::string private_key_path;
        bool use_certificate_auth = false;
        int64_t token_expiry = 0;
    };
    
    static ExchangeAuth load_nse_auth(const ConfigManager& config);
    static ExchangeAuth load_bse_auth(const ConfigManager& config);
    static bool validate_auth_config(const ExchangeAuth& auth);
    static void refresh_session_token(ExchangeAuth& auth);
};

} // namespace goldearn::config