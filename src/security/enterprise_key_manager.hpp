#pragma once

#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

namespace goldearn::security {

// Key types for different purposes
enum class KeyType {
    MASTER_KEY,        // Master encryption key
    DATA_ENCRYPTION,   // Data encryption keys
    API_SIGNING,       // API request signing
    JWT_SIGNING,       // JWT token signing
    HMAC_AUTH,         // HMAC authentication
    CERTIFICATE       // Certificate encryption
};

// Key metadata
struct KeyMetadata {
    std::string key_id;
    KeyType type;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
    std::chrono::system_clock::time_point last_used;
    uint32_t version;
    bool is_active;
    std::string algorithm;
    uint32_t key_length_bits;
};

// Secure key derivation
class KeyDerivation {
public:
    // PBKDF2 key derivation
    static std::string derive_key_pbkdf2(const std::string& password, 
                                        const std::string& salt,
                                        uint32_t iterations = 100000,
                                        uint32_t key_length = 32);
    
    // HKDF key derivation  
    static std::string derive_key_hkdf(const std::string& input_key_material,
                                      const std::string& salt,
                                      const std::string& info,
                                      uint32_t key_length = 32);
    
    // Scrypt key derivation (memory-hard)
    static std::string derive_key_scrypt(const std::string& password,
                                        const std::string& salt,
                                        uint32_t n = 32768,      // CPU/memory cost
                                        uint32_t r = 8,          // Block size
                                        uint32_t p = 1,          // Parallelization
                                        uint32_t key_length = 32);
    
    // Generate cryptographically secure salt
    static std::string generate_salt(uint32_t length = 32);
    
private:
    static void secure_zero_memory(void* ptr, size_t size);
};

// Enterprise key manager with HSM support
class EnterpriseKeyManager {
public:
    EnterpriseKeyManager();
    ~EnterpriseKeyManager();
    
    // Key lifecycle management
    std::string generate_key(KeyType type, uint32_t key_length_bits = 256);
    std::string derive_key(KeyType type, const std::string& master_password, 
                          const std::string& context);
    bool rotate_key(const std::string& key_id);
    bool revoke_key(const std::string& key_id);
    
    // Key access
    std::string get_key(const std::string& key_id);
    std::string get_active_key(KeyType type);
    KeyMetadata get_key_metadata(const std::string& key_id);
    
    // Key storage
    bool store_key(const std::string& key_id, const std::string& key_data, 
                   const KeyMetadata& metadata);
    bool export_key(const std::string& key_id, const std::string& export_path,
                    const std::string& encryption_password);
    bool import_key(const std::string& import_path, const std::string& decryption_password);
    
    // Automatic key rotation
    void enable_auto_rotation(KeyType type, std::chrono::hours rotation_interval);
    void disable_auto_rotation(KeyType type);
    
    // HSM integration
    void configure_hsm(const std::string& hsm_library_path, 
                      const std::string& hsm_slot,
                      const std::string& hsm_pin);
    bool is_hsm_available() const { return hsm_enabled_; }
    
    // Key escrow and recovery
    bool create_key_escrow(const std::string& key_id, 
                          const std::vector<std::string>& trustee_keys,
                          uint32_t threshold);
    bool recover_key_from_escrow(const std::string& key_id,
                                const std::vector<std::string>& recovery_shares);
    
    // Audit and compliance
    std::vector<std::string> get_key_audit_log() const;
    void log_key_access(const std::string& key_id, const std::string& operation);
    void export_compliance_report(const std::string& output_path);
    
private:
    void rotation_thread_func();
    void cleanup_expired_keys();
    std::string generate_key_id();
    bool validate_key_strength(const std::string& key_data, uint32_t expected_bits);
    
    // HSM operations
    std::string hsm_generate_key(KeyType type, uint32_t key_length_bits);
    std::string hsm_get_key(const std::string& key_id);
    bool hsm_store_key(const std::string& key_id, const std::string& key_data);
    
private:
    std::unordered_map<std::string, std::string> keys_;
    std::unordered_map<std::string, KeyMetadata> key_metadata_;
    std::unordered_map<KeyType, std::string> active_keys_;
    
    mutable std::mutex keys_mutex_;
    
    // Auto-rotation
    std::unordered_map<KeyType, std::chrono::hours> rotation_intervals_;
    std::atomic<bool> rotation_enabled_{false};
    std::thread rotation_thread_;
    
    // HSM configuration
    bool hsm_enabled_{false};
    std::string hsm_library_path_;
    std::string hsm_slot_;
    std::string hsm_pin_;
    
    // Audit logging
    std::vector<std::string> audit_log_;
    mutable std::mutex audit_mutex_;
    
    // Configuration
    static constexpr uint32_t MIN_KEY_LENGTH_BITS = 256;
    static constexpr uint32_t MAX_AUDIT_LOG_ENTRIES = 10000;
    static constexpr std::chrono::hours DEFAULT_KEY_LIFETIME{24 * 30}; // 30 days
};

// Key Management Service (KMS) integration
class KMSIntegration {
public:
    virtual ~KMSIntegration() = default;
    
    // Abstract interface for different KMS providers
    virtual std::string generate_data_key(const std::string& key_spec) = 0;
    virtual std::string encrypt(const std::string& key_id, const std::string& plaintext) = 0;
    virtual std::string decrypt(const std::string& key_id, const std::string& ciphertext) = 0;
    virtual bool rotate_key(const std::string& key_id) = 0;
    virtual std::vector<std::string> list_keys() = 0;
};

// AWS KMS integration
class AWSKMSIntegration : public KMSIntegration {
public:
    AWSKMSIntegration(const std::string& region, const std::string& access_key,
                      const std::string& secret_key);
    
    std::string generate_data_key(const std::string& key_spec) override;
    std::string encrypt(const std::string& key_id, const std::string& plaintext) override;
    std::string decrypt(const std::string& key_id, const std::string& ciphertext) override;
    bool rotate_key(const std::string& key_id) override;
    std::vector<std::string> list_keys() override;
    
private:
    std::string region_;
    std::string access_key_;
    std::string secret_key_;
    std::string session_token_;
};

// Azure Key Vault integration
class AzureKeyVaultIntegration : public KMSIntegration {
public:
    AzureKeyVaultIntegration(const std::string& vault_url, const std::string& client_id,
                            const std::string& client_secret, const std::string& tenant_id);
    
    std::string generate_data_key(const std::string& key_spec) override;
    std::string encrypt(const std::string& key_id, const std::string& plaintext) override;
    std::string decrypt(const std::string& key_id, const std::string& ciphertext) override;
    bool rotate_key(const std::string& key_id) override;
    std::vector<std::string> list_keys() override;
    
private:
    std::string vault_url_;
    std::string client_id_;
    std::string client_secret_;
    std::string tenant_id_;
    std::string access_token_;
    
    std::string authenticate();
};

// HashiCorp Vault integration
class HashiCorpVaultIntegration : public KMSIntegration {
public:
    HashiCorpVaultIntegration(const std::string& vault_addr, const std::string& token);
    
    std::string generate_data_key(const std::string& key_spec) override;
    std::string encrypt(const std::string& key_id, const std::string& plaintext) override;
    std::string decrypt(const std::string& key_id, const std::string& ciphertext) override;
    bool rotate_key(const std::string& key_id) override;
    std::vector<std::string> list_keys() override;
    
private:
    std::string vault_addr_;
    std::string token_;
    
    std::string make_vault_request(const std::string& method, const std::string& path,
                                  const std::string& data = "");
};

} // namespace goldearn::security