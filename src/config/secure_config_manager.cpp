#include "config_manager.hpp"
#include "../utils/simple_logger.hpp"
#include <fstream>
#include <filesystem>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/aes.h>
#include <openssl/hmac.h>
#include <openssl/buffer.h>
#include <openssl/bio.h>
#include <nlohmann/json.hpp>

namespace goldearn::config {

// Secure JSON parsing using nlohmann/json
bool ConfigManager::parse_json_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("ConfigManager: Failed to open JSON config file: {}", filename);
        return false;
    }
    
    try {
        nlohmann::json json_data;
        file >> json_data;
        file.close();
        
        // Validate JSON structure before processing
        if (!validate_json_structure(json_data)) {
            LOG_ERROR("ConfigManager: Invalid JSON structure in file: {}", filename);
            return false;
        }
        
        sections_.clear();
        return parse_json_object(json_data, "");
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("ConfigManager: JSON parsing error in file {}: {}", filename, e.what());
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("ConfigManager: Error reading JSON file {}: {}", filename, e.what());
        return false;
    }
}

bool ConfigManager::parse_json_object(const nlohmann::json& json_obj, const std::string& prefix) {
    try {
        for (auto& [key, value] : json_obj.items()) {
            // Input validation for keys
            if (!validate_config_key(key)) {
                LOG_ERROR("ConfigManager: Invalid configuration key: {}", key);
                return false;
            }
            
            std::string full_key = prefix.empty() ? key : prefix + "." + key;
            
            if (value.is_object()) {
                // Recursively parse nested objects
                if (!parse_json_object(value, full_key)) {
                    return false;
                }
            } else {
                // Convert JSON value to ConfigValue with validation
                ConfigValue config_value = json_to_config_value(value);
                set_value(full_key, config_value.as_string());
            }
        }
        
        LOG_INFO("ConfigManager: Successfully parsed JSON configuration");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("ConfigManager: JSON parsing error: {}", e.what());
        return false;
    }
}

ConfigValue ConfigManager::json_to_config_value(const nlohmann::json& json_val) {
    try {
        if (json_val.is_string()) {
            std::string str_val = json_val.get<std::string>();
            // Validate string length and content
            if (str_val.length() > MAX_CONFIG_VALUE_LENGTH) {
                LOG_WARN("ConfigManager: String value truncated to {} characters", MAX_CONFIG_VALUE_LENGTH);
                str_val = str_val.substr(0, MAX_CONFIG_VALUE_LENGTH);
            }
            return ConfigValue(str_val);
        } else if (json_val.is_number_integer()) {
            return ConfigValue(json_val.get<int64_t>());
        } else if (json_val.is_number_float()) {
            return ConfigValue(json_val.get<double>());
        } else if (json_val.is_boolean()) {
            return ConfigValue(json_val.get<bool>());
        } else if (json_val.is_array()) {
            std::vector<std::string> array_val;
            for (const auto& item : json_val) {
                if (item.is_string()) {
                    std::string str_item = item.get<std::string>();
                    if (str_item.length() <= MAX_CONFIG_VALUE_LENGTH) {
                        array_val.push_back(str_item);
                    }
                }
            }
            return ConfigValue(array_val);
        } else {
            LOG_WARN("ConfigManager: Unsupported JSON value type, converting to string");
            return ConfigValue(json_val.dump());
        }
    } catch (const std::exception& e) {
        LOG_ERROR("ConfigManager: Error converting JSON value: {}", e.what());
        return ConfigValue("");
    }
}

bool ConfigManager::validate_json_structure(const nlohmann::json& json_data) {
    // Basic structure validation
    if (!json_data.is_object()) {
        LOG_ERROR("ConfigManager: Root JSON element must be an object");
        return false;
    }
    
    // Check for maximum depth to prevent stack overflow
    if (!validate_json_depth(json_data, 0, MAX_JSON_DEPTH)) {
        LOG_ERROR("ConfigManager: JSON nesting depth exceeds maximum allowed ({})", MAX_JSON_DEPTH);
        return false;
    }
    
    return true;
}

bool ConfigManager::validate_json_depth(const nlohmann::json& json_data, int current_depth, int max_depth) {
    if (current_depth > max_depth) {
        return false;
    }
    
    if (json_data.is_object()) {
        for (const auto& [key, value] : json_data.items()) {
            if (!validate_json_depth(value, current_depth + 1, max_depth)) {
                return false;
            }
        }
    } else if (json_data.is_array()) {
        for (const auto& item : json_data) {
            if (!validate_json_depth(item, current_depth + 1, max_depth)) {
                return false;
            }
        }
    }
    
    return true;
}

bool ConfigManager::validate_config_key(const std::string& key) {
    // Key validation rules
    if (key.empty() || key.length() > MAX_CONFIG_KEY_LENGTH) {
        return false;
    }
    
    // Allow alphanumeric, underscore, hyphen, and dot
    for (char c : key) {
        if (!std::isalnum(c) && c != '_' && c != '-' && c != '.') {
            return false;
        }
    }
    
    return true;
}

// Secure credential encryption using AES-256-GCM
bool ConfigManager::encrypt_credential(const std::string& plaintext, std::string& encrypted_data) {
    try {
        // Generate random key and IV
        unsigned char key[32]; // 256-bit key
        unsigned char iv[12];  // 96-bit IV for GCM
        
        if (RAND_bytes(key, sizeof(key)) != 1 || RAND_bytes(iv, sizeof(iv)) != 1) {
            LOG_ERROR("ConfigManager: Failed to generate random key/IV");
            return false;
        }
        
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            LOG_ERROR("ConfigManager: Failed to create cipher context");
            return false;
        }
        
        // Initialize encryption
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            LOG_ERROR("ConfigManager: Failed to initialize AES-256-GCM encryption");
            return false;
        }
        
        // Set IV length
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(iv), nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            LOG_ERROR("ConfigManager: Failed to set IV length");
            return false;
        }
        
        // Initialize with key and IV
        if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, iv) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            LOG_ERROR("ConfigManager: Failed to set key and IV");
            return false;
        }
        
        // Encrypt data
        std::vector<unsigned char> ciphertext(plaintext.length() + 16); // Extra space for tag
        int len;
        int ciphertext_len;
        
        if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, 
                             reinterpret_cast<const unsigned char*>(plaintext.c_str()), 
                             plaintext.length()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            LOG_ERROR("ConfigManager: Encryption failed");
            return false;
        }
        ciphertext_len = len;
        
        // Finalize encryption
        if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            LOG_ERROR("ConfigManager: Encryption finalization failed");
            return false;
        }
        ciphertext_len += len;
        
        // Get authentication tag
        unsigned char tag[16];
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            LOG_ERROR("ConfigManager: Failed to get authentication tag");
            return false;
        }
        
        EVP_CIPHER_CTX_free(ctx);
        
        // Encode result (key + iv + tag + ciphertext) as base64
        std::string combined;
        combined.append(reinterpret_cast<char*>(key), sizeof(key));
        combined.append(reinterpret_cast<char*>(iv), sizeof(iv));
        combined.append(reinterpret_cast<char*>(tag), sizeof(tag));
        combined.append(reinterpret_cast<char*>(ciphertext.data()), ciphertext_len);
        
        encrypted_data = base64_encode(combined);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("ConfigManager: Encryption error: {}", e.what());
        return false;
    }
}

bool ConfigManager::decrypt_credential(const std::string& encrypted_data, std::string& plaintext) {
    try {
        // Decode from base64
        std::string combined = base64_decode(encrypted_data);
        
        if (combined.length() < 32 + 12 + 16) { // key + iv + tag minimum
            LOG_ERROR("ConfigManager: Invalid encrypted data length");
            return false;
        }
        
        // Extract components
        const unsigned char* key = reinterpret_cast<const unsigned char*>(combined.data());
        const unsigned char* iv = key + 32;
        const unsigned char* tag = iv + 12;
        const unsigned char* ciphertext = tag + 16;
        int ciphertext_len = combined.length() - 32 - 12 - 16;
        
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            LOG_ERROR("ConfigManager: Failed to create cipher context");
            return false;
        }
        
        // Initialize decryption
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            LOG_ERROR("ConfigManager: Failed to initialize AES-256-GCM decryption");
            return false;
        }
        
        // Set IV length
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            LOG_ERROR("ConfigManager: Failed to set IV length");
            return false;
        }
        
        // Initialize with key and IV
        if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, iv) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            LOG_ERROR("ConfigManager: Failed to set key and IV");
            return false;
        }
        
        // Decrypt data
        std::vector<unsigned char> decrypted(ciphertext_len);
        int len;
        int plaintext_len;
        
        if (EVP_DecryptUpdate(ctx, decrypted.data(), &len, ciphertext, ciphertext_len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            LOG_ERROR("ConfigManager: Decryption failed");
            return false;
        }
        plaintext_len = len;
        
        // Set expected tag
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<unsigned char*>(tag)) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            LOG_ERROR("ConfigManager: Failed to set authentication tag");
            return false;
        }
        
        // Finalize decryption and verify tag
        if (EVP_DecryptFinal_ex(ctx, decrypted.data() + len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            LOG_ERROR("ConfigManager: Decryption verification failed - data may be tampered");
            return false;
        }
        plaintext_len += len;
        
        EVP_CIPHER_CTX_free(ctx);
        
        plaintext.assign(reinterpret_cast<char*>(decrypted.data()), plaintext_len);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("ConfigManager: Decryption error: {}", e.what());
        return false;
    }
}

// Secure HMAC using OpenSSL
std::string ConfigManager::calculate_secure_hmac(const std::string& data, const std::string& key) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;
    
    HMAC(EVP_sha256(), key.c_str(), key.length(),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         digest, &digest_len);
    
    // Convert to hex string
    std::ostringstream ss;
    for (unsigned int i = 0; i < digest_len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    
    return ss.str();
}

std::string ConfigManager::base64_encode(const std::string& input) {
    BIO* bio_mem = BIO_new(BIO_s_mem());
    BIO* bio_b64 = BIO_new(BIO_f_base64());
    bio_b64 = BIO_push(bio_b64, bio_mem);
    
    BIO_set_flags(bio_b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio_b64, input.c_str(), input.length());
    BIO_flush(bio_b64);
    
    BUF_MEM* buf_mem;
    BIO_get_mem_ptr(bio_b64, &buf_mem);
    
    std::string result(buf_mem->data, buf_mem->length);
    BIO_free_all(bio_b64);
    
    return result;
}

std::string ConfigManager::base64_decode(const std::string& input) {
    BIO* bio_mem = BIO_new_mem_buf(input.c_str(), input.length());
    BIO* bio_b64 = BIO_new(BIO_f_base64());
    bio_mem = BIO_push(bio_b64, bio_mem);
    
    BIO_set_flags(bio_mem, BIO_FLAGS_BASE64_NO_NL);
    
    std::vector<char> buffer(input.length());
    int decoded_length = BIO_read(bio_mem, buffer.data(), input.length());
    
    BIO_free_all(bio_mem);
    
    if (decoded_length > 0) {
        return std::string(buffer.data(), decoded_length);
    }
    return "";
}

} // namespace goldearn::config