#include "config_manager.hpp"
#include "../utils/logger.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <cstdlib>

namespace goldearn::config {

bool ConfigManager::load_from_file(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    config_filename_ = filename;
    
    // Determine file format by extension
    if (filename.ends_with(".json")) {
        return parse_json_file(filename);
    } else {
        return parse_ini_file(filename);
    }
}

void ConfigManager::load_from_environment() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Load common environment variables
    const char* env_vars[] = {
        "GOLDEARN_ENVIRONMENT",
        "GOLDEARN_LOG_LEVEL", 
        "GOLDEARN_NSE_HOST",
        "GOLDEARN_NSE_PORT",
        "GOLDEARN_BSE_HOST",
        "GOLDEARN_BSE_PORT",
        "GOLDEARN_MAX_DAILY_LOSS",
        "GOLDEARN_MAX_POSITION_VALUE",
        "GOLDEARN_REDIS_HOST",
        "GOLDEARN_POSTGRES_HOST",
        "GOLDEARN_API_KEY",
        "GOLDEARN_SECRET_KEY"
    };
    
    auto env_section = get_section("environment");
    
    for (const char* var : env_vars) {
        const char* value = getenv(var);
        if (value) {
            std::string key = std::string(var);
            // Remove GOLDEARN_ prefix and convert to lowercase
            if (key.starts_with("GOLDEARN_")) {
                key = key.substr(9);
            }
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            
            env_section->set(key, ConfigValue(std::string(value)));
            LOG_INFO("ConfigManager: Loaded environment variable: {} = {}", key, value);
        }
    }
}

std::shared_ptr<ConfigSection> ConfigManager::get_section(const std::string& section_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sections_.find(section_name);
    if (it == sections_.end()) {
        sections_[section_name] = std::make_shared<ConfigSection>();
    }
    
    return sections_[section_name];
}

bool ConfigManager::reload() {
    if (config_filename_.empty()) {
        LOG_WARN("ConfigManager: No configuration file to reload");
        return false;
    }
    
    LOG_INFO("ConfigManager: Reloading configuration from {}", config_filename_);
    return load_from_file(config_filename_);
}

bool ConfigManager::validate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    bool valid = true;
    
    // Check required sections
    std::vector<std::string> required_sections = {"system", "market_data", "risk"};
    for (const auto& section : required_sections) {
        if (sections_.find(section) == sections_.end()) {
            LOG_ERROR("ConfigManager: Missing required section: {}", section);
            valid = false;
        }
    }
    
    // Validate system section
    if (auto sys = sections_.find("system"); sys != sections_.end()) {
        if (!sys->second->has("log_level")) {
            LOG_WARN("ConfigManager: Missing log_level in system section");
        }
        
        std::string env = sys->second->get("environment").as_string();
        if (env != "development" && env != "testing" && env != "production") {
            LOG_ERROR("ConfigManager: Invalid environment: {}", env);
            valid = false;
        }
    }
    
    // Validate market data section
    if (auto md = sections_.find("market_data"); md != sections_.end()) {
        if (!md->second->has("nse_host") || !md->second->has("nse_port")) {
            LOG_ERROR("ConfigManager: Missing NSE connection details");
            valid = false;
        }
        
        // Check for production endpoints
        std::string nse_host = md->second->get("nse_host").as_string();
        if (is_production() && (nse_host.find("example.com") != std::string::npos ||
                               nse_host.find("localhost") != std::string::npos ||
                               nse_host.find("127.0.0.1") != std::string::npos)) {
            LOG_ERROR("ConfigManager: Production mode cannot use test endpoints: {}", nse_host);
            valid = false;
        }
    }
    
    // Validate risk section
    if (auto risk = sections_.find("risk"); risk != sections_.end()) {
        double max_loss = risk->second->get("max_daily_loss").as_double();
        if (max_loss <= 0) {
            LOG_ERROR("ConfigManager: Invalid max_daily_loss: {}", max_loss);
            valid = false;
        }
    }
    
    return valid;
}

bool ConfigManager::parse_ini_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("ConfigManager: Failed to open config file: {}", filename);
        return false;
    }
    
    sections_.clear();
    
    std::string line;
    std::string current_section;
    int line_number = 0;
    
    while (std::getline(file, line)) {
        line_number++;
        line = trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // Section header
        if (line[0] == '[' && line[line.length()-1] == ']') {
            current_section = line.substr(1, line.length()-2);
            continue;
        }
        
        // Key-value pair
        size_t equals_pos = line.find('=');
        if (equals_pos == std::string::npos) {
            LOG_WARN("ConfigManager: Invalid line {} in {}: {}", line_number, filename, line);
            continue;
        }
        
        if (current_section.empty()) {
            LOG_WARN("ConfigManager: Key-value pair outside section at line {}: {}", line_number, line);
            continue;
        }
        
        std::string key = trim(line.substr(0, equals_pos));
        std::string value = trim(line.substr(equals_pos + 1));
        
        // Remove quotes if present
        if ((value.front() == '"' && value.back() == '"') ||
            (value.front() == '\'' && value.back() == '\'')) {
            value = value.substr(1, value.length() - 2);
        }
        
        auto section = get_section(current_section);
        
        // Try to parse as different types
        ConfigValue config_value;
        
        // Boolean
        if (value == "true" || value == "false" || value == "yes" || value == "no" ||
            value == "1" || value == "0") {
            bool bool_val = (value == "true" || value == "yes" || value == "1");
            config_value = ConfigValue(bool_val);
        }
        // Integer
        else if (std::regex_match(value, std::regex("^-?\\d+$"))) {
            config_value = ConfigValue(std::stoll(value));
        }
        // Double
        else if (std::regex_match(value, std::regex("^-?\\d*\\.\\d+$"))) {
            config_value = ConfigValue(std::stod(value));
        }
        // Array (comma-separated)
        else if (value.find(',') != std::string::npos) {
            std::vector<std::string> array_values;
            std::stringstream ss(value);
            std::string item;
            while (std::getline(ss, item, ',')) {
                array_values.push_back(trim(item));
            }
            config_value = ConfigValue(array_values);
        }
        // String
        else {
            config_value = ConfigValue(value);
        }
        
        section->set(key, config_value);
    }
    
    LOG_INFO("ConfigManager: Loaded configuration from {}", filename);
    return true;
}

bool ConfigManager::parse_json_file(const std::string& filename) {
    // TODO: Implement JSON parsing
    LOG_ERROR("ConfigManager: JSON parsing not yet implemented");
    return false;
}

std::string ConfigManager::trim(const std::string& str) {
    const std::string whitespace = " \t\r\n";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string::npos) return "";
    
    size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, end - start + 1);
}

// ProductionConfig implementation
void ProductionConfig::create_production_template(const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("ProductionConfig: Failed to create template: {}", filename);
        return;
    }
    
    file << "# GoldEarn HFT Production Configuration\n\n";
    
    file << "[system]\n";
    file << "environment = production\n";
    file << "log_level = INFO\n";
    file << "log_file = /var/log/goldearn/goldearn.log\n";
    file << "enable_monitoring = true\n";
    file << "monitoring_port = 9090\n";
    file << "pid_file = /var/run/goldearn.pid\n\n";
    
    file << "[market_data]\n";
    file << "nse_host = " << NSE_PROD_HOST << "\n";
    file << "nse_port = " << NSE_PROD_PORT << "\n";
    file << "nse_backup_host = " << NSE_BACKUP_HOST << "\n";
    file << "bse_host = " << BSE_PROD_HOST << "\n";
    file << "bse_port = " << BSE_PROD_PORT << "\n";
    file << "enable_simulation = false\n";
    file << "connection_timeout = 5000\n";
    file << "heartbeat_interval = 30000\n";
    file << "enable_redundancy = true\n\n";
    
    file << "[trading]\n";
    file << "enable_paper_trading = false\n";
    file << "initial_capital = 100000000.0\n";
    file << "max_position_size = " << PROD_MAX_POSITION_VALUE << "\n";
    file << "max_order_value = " << PROD_MAX_ORDER_VALUE << "\n";
    file << "enable_strategies = true\n\n";
    
    file << "[risk]\n";
    file << "enable_risk_checks = true\n";
    file << "max_daily_loss = " << PROD_MAX_DAILY_LOSS << "\n";
    file << "max_exposure = " << PROD_MAX_POSITION_VALUE << "\n";
    file << "max_var_1d = 5000000.0\n";
    file << "position_concentration = 0.20\n";
    file << "sector_concentration = 0.40\n";
    file << "max_order_rate = " << PROD_MAX_ORDER_RATE << "\n";
    file << "max_message_rate = " << PROD_MAX_MESSAGE_RATE << "\n\n";
    
    file << "[database]\n";
    file << "redis_host = prod-redis.internal\n";
    file << "redis_port = 6379\n";
    file << "postgres_host = prod-postgres.internal\n";
    file << "postgres_port = 5432\n";
    file << "postgres_db = goldearn_hft\n";
    file << "postgres_user = hftuser\n";
    file << "clickhouse_host = prod-clickhouse.internal\n";
    file << "clickhouse_port = 9000\n\n";
    
    file << "[authentication]\n";
    file << "# NSE Authentication\n";
    file << "nse_api_key = ${NSE_API_KEY}\n";
    file << "nse_secret_key = ${NSE_SECRET_KEY}\n";
    file << "nse_certificate_path = /etc/goldearn/certs/nse_client.pem\n";
    file << "nse_private_key_path = /etc/goldearn/certs/nse_client.key\n";
    file << "# BSE Authentication\n";
    file << "bse_api_key = ${BSE_API_KEY}\n";
    file << "bse_secret_key = ${BSE_SECRET_KEY}\n";
    file << "bse_certificate_path = /etc/goldearn/certs/bse_client.pem\n";
    file << "bse_private_key_path = /etc/goldearn/certs/bse_client.key\n\n";
    
    file << "[security]\n";
    file << "enable_tls = true\n";
    file << "min_tls_version = 1.3\n";
    file << "ca_cert_path = /etc/goldearn/certs/ca.pem\n";
    file << "skip_hostname_verification = false\n";
    file << "skip_certificate_verification = false\n";
    file << "enable_rate_limiting = true\n\n";
    
    file << "[monitoring]\n";
    file << "prometheus_enabled = true\n";
    file << "prometheus_port = 9091\n";
    file << "health_check_port = 8080\n";
    file << "metrics_interval = 5\n";
    file << "log_performance_metrics = true\n\n";
    
    LOG_INFO("ProductionConfig: Created production template: {}", filename);
}

void ProductionConfig::create_development_template(const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("ProductionConfig: Failed to create template: {}", filename);
        return;
    }
    
    file << "# GoldEarn HFT Development Configuration\n\n";
    
    file << "[system]\n";
    file << "environment = development\n";
    file << "log_level = DEBUG\n";
    file << "log_file = logs/goldearn-dev.log\n";
    file << "enable_monitoring = true\n";
    file << "monitoring_port = 9090\n\n";
    
    file << "[market_data]\n";
    file << "nse_host = 127.0.0.1\n";
    file << "nse_port = 9899\n";
    file << "bse_host = 127.0.0.1\n";
    file << "bse_port = 9898\n";
    file << "enable_simulation = true\n";
    file << "simulation_file = data/market_data_sample.csv\n";
    file << "connection_timeout = 10000\n\n";
    
    file << "[trading]\n";
    file << "enable_paper_trading = true\n";
    file << "initial_capital = 1000000.0\n";
    file << "max_position_size = 100000.0\n";
    file << "max_order_value = 50000.0\n\n";
    
    file << "[risk]\n";
    file << "enable_risk_checks = true\n";
    file << "max_daily_loss = 50000.0\n";
    file << "max_exposure = 500000.0\n";
    file << "max_order_rate = 100\n";
    file << "max_message_rate = 1000\n\n";
    
    file << "[database]\n";
    file << "redis_host = localhost\n";
    file << "redis_port = 6379\n";
    file << "postgres_host = localhost\n";
    file << "postgres_port = 5432\n";
    file << "postgres_db = goldearn_hft_dev\n";
    file << "postgres_user = dev_user\n\n";
    
    file << "[security]\n";
    file << "enable_tls = false\n";
    file << "enable_rate_limiting = true\n\n";
    
    LOG_INFO("ProductionConfig: Created development template: {}", filename);
}

bool ProductionConfig::validate_production_config(const ConfigManager& config) {
    bool valid = true;
    
    // Check environment
    if (config.get_string("system", "environment") != "production") {
        LOG_ERROR("ProductionConfig: Not in production environment");
        valid = false;
    }
    
    // Check for test endpoints
    std::string nse_host = config.get_string("market_data", "nse_host");
    if (nse_host.find("example.com") != std::string::npos ||
        nse_host.find("localhost") != std::string::npos ||
        nse_host.find("127.0.0.1") != std::string::npos) {
        LOG_ERROR("ProductionConfig: Production cannot use test endpoints: {}", nse_host);
        valid = false;
    }
    
    // Check risk limits
    double max_loss = config.get_double("risk", "max_daily_loss");
    if (max_loss <= 0 || max_loss > PROD_MAX_DAILY_LOSS * 2) {
        LOG_ERROR("ProductionConfig: Invalid max_daily_loss: {}", max_loss);
        valid = false;
    }
    
    // Check authentication
    std::string api_key = config.get_string("authentication", "nse_api_key");
    if (api_key.empty() || api_key.find("${") != std::string::npos) {
        LOG_ERROR("ProductionConfig: Missing or template NSE API key");
        valid = false;
    }
    
    return valid;
}

// AuthConfig implementation
AuthConfig::ExchangeAuth AuthConfig::load_nse_auth(const ConfigManager& config) {
    ExchangeAuth auth;
    auto auth_section = config.get_section("authentication");
    
    auth.api_key = auth_section->get("nse_api_key").as_string();
    auth.secret_key = auth_section->get("nse_secret_key").as_string();
    auth.certificate_path = auth_section->get("nse_certificate_path").as_string();
    auth.private_key_path = auth_section->get("nse_private_key_path").as_string();
    auth.use_certificate_auth = !auth.certificate_path.empty();
    
    return auth;
}

AuthConfig::ExchangeAuth AuthConfig::load_bse_auth(const ConfigManager& config) {
    ExchangeAuth auth;
    auto auth_section = config.get_section("authentication");
    
    auth.api_key = auth_section->get("bse_api_key").as_string();
    auth.secret_key = auth_section->get("bse_secret_key").as_string();
    auth.certificate_path = auth_section->get("bse_certificate_path").as_string();
    auth.private_key_path = auth_section->get("bse_private_key_path").as_string();
    auth.use_certificate_auth = !auth.certificate_path.empty();
    
    return auth;
}

bool AuthConfig::validate_auth_config(const ExchangeAuth& auth) {
    if (auth.use_certificate_auth) {
        if (auth.certificate_path.empty() || auth.private_key_path.empty()) {
            LOG_ERROR("AuthConfig: Certificate authentication requires both certificate and private key paths");
            return false;
        }
        
        // Check if files exist
        std::ifstream cert_file(auth.certificate_path);
        if (!cert_file.good()) {
            LOG_ERROR("AuthConfig: Certificate file not found: {}", auth.certificate_path);
            return false;
        }
        
        std::ifstream key_file(auth.private_key_path);
        if (!key_file.good()) {
            LOG_ERROR("AuthConfig: Private key file not found: {}", auth.private_key_path);
            return false;
        }
    } else {
        if (auth.api_key.empty() || auth.secret_key.empty()) {
            LOG_ERROR("AuthConfig: API authentication requires both API key and secret key");
            return false;
        }
    }
    
    return true;
}

void AuthConfig::refresh_session_token(ExchangeAuth& auth) {
    // TODO: Implement token refresh logic
    LOG_INFO("AuthConfig: Refreshing session token for API key: {}...", 
             auth.api_key.substr(0, 8));
}

} // namespace goldearn::config