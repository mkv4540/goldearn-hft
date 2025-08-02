#include "config_manager.hpp"
#include "../utils/simple_logger.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace goldearn::config {

bool ConfigManager::load_from_file(const std::string& filename) {
    if (filename.empty()) {
        LOG_ERROR("ConfigManager: Empty filename provided");
        return false;
    }
    
    // Check file extension
    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos == std::string::npos) {
        LOG_ERROR("ConfigManager: No file extension found in {}", filename);
        return false;
    }
    
    std::string extension = filename.substr(dot_pos + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    bool result = false;
    if (extension == "ini" || extension == "conf") {
        result = parse_ini_file(filename);
    } else if (extension == "json") {
        result = parse_json_file(filename);
    } else {
        LOG_ERROR("ConfigManager: Unsupported file type: {}", extension);
        return false;
    }
    
    if (result) {
        filename_ = filename;
        last_modified_ = std::filesystem::last_write_time(filename);
        LOG_INFO("ConfigManager: Successfully loaded configuration from {}", filename);
    }
    
    return result;
}

void ConfigManager::load_from_environment() {
    // Load environment-specific overrides
    const char* env_vars[] = {
        "GOLDEARN_ENVIRONMENT",
        "GOLDEARN_LOG_LEVEL",
        "GOLDEARN_REDIS_HOST",
        "GOLDEARN_REDIS_PORT",
        "GOLDEARN_DB_HOST",
        "GOLDEARN_DB_PORT",
        "GOLDEARN_DB_NAME",
        "GOLDEARN_DB_USER",
        "GOLDEARN_DB_PASSWORD",
        "GOLDEARN_API_KEY",
        "GOLDEARN_API_SECRET",
        "GOLDEARN_NSE_HOST",
        "GOLDEARN_NSE_PORT",
        "GOLDEARN_BSE_HOST",
        "GOLDEARN_BSE_PORT"
    };
    
    auto env_section = get_section("environment");
    
    for (const char* var : env_vars) {
        const char* value = std::getenv(var);
        if (value) {
            std::string key = var + 9; // Skip "GOLDEARN_" prefix
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            env_section->set(key, ConfigValue(std::string(value)));
            LOG_DEBUG("ConfigManager: Loaded from environment", var);
        }
    }
}

std::shared_ptr<ConfigSection> ConfigManager::get_section(const std::string& section_name) {
    std::lock_guard<std::shared_mutex> lock(sections_mutex_);
    
    auto it = sections_.find(section_name);
    if (it != sections_.end()) {
        return it->second;
    }
    
    // Create new section if it doesn't exist
    auto section = std::make_shared<ConfigSection>();
    sections_[section_name] = section;
    return section;
}

bool ConfigManager::has_section(const std::string& section_name) const {
    std::shared_lock<std::shared_mutex> lock(sections_mutex_);
    return sections_.find(section_name) != sections_.end();
}

std::vector<std::string> ConfigManager::get_section_names() const {
    std::shared_lock<std::shared_mutex> lock(sections_mutex_);
    std::vector<std::string> names;
    names.reserve(sections_.size());
    
    for (const auto& pair : sections_) {
        names.push_back(pair.first);
    }
    
    return names;
}

std::string ConfigManager::get_string(const std::string& key, const std::string& default_value) const {
    size_t dot_pos = key.find('.');
    if (dot_pos != std::string::npos) {
        std::string section_name = key.substr(0, dot_pos);
        std::string key_name = key.substr(dot_pos + 1);
        
        std::shared_lock<std::shared_mutex> lock(sections_mutex_);
        auto it = sections_.find(section_name);
        if (it != sections_.end()) {
            return it->second->get(key_name, ConfigValue(default_value)).as_string();
        }
    }
    
    return default_value;
}

int64_t ConfigManager::get_int(const std::string& key, int64_t default_value) const {
    size_t dot_pos = key.find('.');
    if (dot_pos != std::string::npos) {
        std::string section_name = key.substr(0, dot_pos);
        std::string key_name = key.substr(dot_pos + 1);
        
        std::shared_lock<std::shared_mutex> lock(sections_mutex_);
        auto it = sections_.find(section_name);
        if (it != sections_.end()) {
            return it->second->get(key_name, ConfigValue(default_value)).as_int();
        }
    }
    
    return default_value;
}

double ConfigManager::get_double(const std::string& key, double default_value) const {
    size_t dot_pos = key.find('.');
    if (dot_pos != std::string::npos) {
        std::string section_name = key.substr(0, dot_pos);
        std::string key_name = key.substr(dot_pos + 1);
        
        std::shared_lock<std::shared_mutex> lock(sections_mutex_);
        auto it = sections_.find(section_name);
        if (it != sections_.end()) {
            return it->second->get(key_name, ConfigValue(default_value)).as_double();
        }
    }
    
    return default_value;
}

bool ConfigManager::get_bool(const std::string& key, bool default_value) const {
    size_t dot_pos = key.find('.');
    if (dot_pos != std::string::npos) {
        std::string section_name = key.substr(0, dot_pos);
        std::string key_name = key.substr(dot_pos + 1);
        
        std::shared_lock<std::shared_mutex> lock(sections_mutex_);
        auto it = sections_.find(section_name);
        if (it != sections_.end()) {
            return it->second->get(key_name, ConfigValue(default_value)).as_bool();
        }
    }
    
    return default_value;
}

void ConfigManager::set_value(const std::string& full_key, const std::string& value) {
    size_t dot_pos = full_key.find('.');
    if (dot_pos != std::string::npos) {
        std::string section_name = full_key.substr(0, dot_pos);
        std::string key = full_key.substr(dot_pos + 1);
        
        auto sec = get_section(section_name);
        sec->set(key, ConfigValue(value));
    }
}

bool ConfigManager::reload() {
    if (filename_.empty()) {
        LOG_ERROR("ConfigManager: No configuration file loaded");
        return false;
    }
    
    return load_from_file(filename_);
}

bool ConfigManager::validate() const {
    // Check required configuration parameters
    std::vector<std::string> required_params = {
        "system.name",
        "system.version",
        "system.environment",
        "network.listen_port",
        "logging.level"
    };
    
    for (const auto& param : required_params) {
        if (get_string(param).empty()) {
            LOG_ERROR("ConfigManager: Missing required parameter: {}", param);
            return false;
        }
    }
    
    // Validate numeric ranges
    int listen_port = get_int("network.listen_port");
    if (listen_port < 1 || listen_port > 65535) {
        LOG_ERROR("ConfigManager: Invalid port number: {}", listen_port);
        return false;
    }
    
    LOG_INFO("ConfigManager: Configuration validation passed");
    return true;
}

std::string ConfigManager::get_environment() const {
    // Check environment variable first
    const char* env = std::getenv("GOLDEARN_ENVIRONMENT");
    if (env) {
        return std::string(env);
    }
    
    // Then check config file  
    return get_string("system", "environment", "development");
}

bool ConfigManager::parse_ini_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("ConfigManager: Failed to open INI file: {}", filename);
        return false;
    }
    
    std::string line;
    std::string current_section = "global";
    auto section = get_section(current_section);
    
    while (std::getline(file, line)) {
        line = trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // Check for section
        if (line[0] == '[' && line.back() == ']') {
            current_section = line.substr(1, line.length() - 2);
            section = get_section(current_section);
            continue;
        }
        
        // Parse key-value pair
        size_t eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = trim(line.substr(0, eq_pos));
            std::string value = trim(line.substr(eq_pos + 1));
            
            // Remove quotes if present
            if (value.length() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.length() - 2);
            }
            
            section->set(key, ConfigValue(value));
        }
    }
    
    file.close();
    LOG_INFO("ConfigManager: Successfully parsed INI file: {}", filename);
    return true;
}

bool ConfigManager::parse_json_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("ConfigManager: Failed to open JSON file: {}", filename);
        return false;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    
    // Parse JSON using nlohmann/json
    try {
        nlohmann::json json_data = nlohmann::json::parse(content);
        return parse_json_object(json_data, "");
    } catch (const nlohmann::json::parse_error& e) {
        LOG_ERROR("ConfigManager: JSON parse error: {}", e.what());
        return false;
    }
}

bool ConfigManager::parse_json_object(const nlohmann::json& json_obj, const std::string& prefix) {
    try {
        for (auto& [key, value] : json_obj.items()) {
            std::string full_key = prefix.empty() ? key : prefix + "." + key;
            
            if (value.is_object()) {
                // Recursively parse nested objects
                parse_json_object(value, full_key);
            } else {
                // Store the value
                size_t dot_pos = full_key.find('.');
                if (dot_pos != std::string::npos) {
                    std::string section_name = full_key.substr(0, dot_pos);
                    std::string key_name = full_key.substr(dot_pos + 1);
                    
                    auto section = get_section(section_name);
                    
                    if (value.is_string()) {
                        section->set(key_name, ConfigValue(value.get<std::string>()));
                    } else if (value.is_number_integer()) {
                        section->set(key_name, ConfigValue(value.get<int64_t>()));
                    } else if (value.is_number_float()) {
                        section->set(key_name, ConfigValue(value.get<double>()));
                    } else if (value.is_boolean()) {
                        section->set(key_name, ConfigValue(value.get<bool>()));
                    } else if (value.is_array()) {
                        std::vector<std::string> arr;
                        for (const auto& item : value) {
                            if (item.is_string()) {
                                arr.push_back(item.get<std::string>());
                            } else {
                                arr.push_back(item.dump());
                            }
                        }
                        section->set(key_name, ConfigValue(arr));
                    }
                }
            }
        }
        
        LOG_INFO("ConfigManager: Successfully parsed JSON configuration");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("ConfigManager: JSON parsing error: {}", e.what());
        return false;
    }
}

std::string ConfigManager::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

} // namespace goldearn::config