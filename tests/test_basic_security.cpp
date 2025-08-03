#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <string>
#include <vector>

// Basic security functionality tests
class BasicSecurityTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize OpenSSL
        OpenSSL_add_all_algorithms();
    }
};

// Test OpenSSL random number generation
TEST_F(BasicSecurityTest, RandomGeneration) {
    unsigned char buffer[32];
    
    // Test that RAND_bytes works
    EXPECT_EQ(RAND_bytes(buffer, sizeof(buffer)), 1);
    
    // Test that we get different values
    unsigned char buffer2[32];
    EXPECT_EQ(RAND_bytes(buffer2, sizeof(buffer2)), 1);
    EXPECT_NE(memcmp(buffer, buffer2, sizeof(buffer)), 0);
}

// Test HMAC-SHA256 functionality
TEST_F(BasicSecurityTest, HMACGeneration) {
    std::string key = "test_secret_key";
    std::string data = "test_data_to_authenticate";
    
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;
    
    // Generate HMAC
    HMAC(EVP_sha256(), key.c_str(), key.length(),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         digest, &digest_len);
    
    EXPECT_EQ(digest_len, 32); // SHA256 produces 32 bytes
    
    // Verify same input produces same output
    unsigned char digest2[EVP_MAX_MD_SIZE];
    unsigned int digest_len2;
    
    HMAC(EVP_sha256(), key.c_str(), key.length(),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         digest2, &digest_len2);
    
    EXPECT_EQ(digest_len, digest_len2);
    EXPECT_EQ(memcmp(digest, digest2, digest_len), 0);
}

// Test AES encryption/decryption
TEST_F(BasicSecurityTest, AESEncryption) {
    // Generate key and IV
    unsigned char key[32]; // 256-bit key
    unsigned char iv[16];  // 128-bit IV
    
    EXPECT_EQ(RAND_bytes(key, sizeof(key)), 1);
    EXPECT_EQ(RAND_bytes(iv, sizeof(iv)), 1);
    
    std::string plaintext = "This is a test message for AES encryption";
    
    // Encrypt
    EVP_CIPHER_CTX* encrypt_ctx = EVP_CIPHER_CTX_new();
    ASSERT_NE(encrypt_ctx, nullptr);
    
    EXPECT_EQ(EVP_EncryptInit_ex(encrypt_ctx, EVP_aes_256_cbc(), nullptr, key, iv), 1);
    
    std::vector<unsigned char> ciphertext(plaintext.length() + 16); // Extra space for padding
    int len;
    int ciphertext_len;
    
    EXPECT_EQ(EVP_EncryptUpdate(encrypt_ctx, ciphertext.data(), &len,
                               reinterpret_cast<const unsigned char*>(plaintext.c_str()),
                               plaintext.length()), 1);
    ciphertext_len = len;
    
    EXPECT_EQ(EVP_EncryptFinal_ex(encrypt_ctx, ciphertext.data() + len, &len), 1);
    ciphertext_len += len;
    
    EVP_CIPHER_CTX_free(encrypt_ctx);
    
    // Decrypt
    EVP_CIPHER_CTX* decrypt_ctx = EVP_CIPHER_CTX_new();
    ASSERT_NE(decrypt_ctx, nullptr);
    
    EXPECT_EQ(EVP_DecryptInit_ex(decrypt_ctx, EVP_aes_256_cbc(), nullptr, key, iv), 1);
    
    std::vector<unsigned char> decrypted(ciphertext_len);
    int decrypted_len;
    
    EXPECT_EQ(EVP_DecryptUpdate(decrypt_ctx, decrypted.data(), &len,
                               ciphertext.data(), ciphertext_len), 1);
    decrypted_len = len;
    
    EXPECT_EQ(EVP_DecryptFinal_ex(decrypt_ctx, decrypted.data() + len, &len), 1);
    decrypted_len += len;
    
    EVP_CIPHER_CTX_free(decrypt_ctx);
    
    // Verify decryption
    std::string decrypted_text(reinterpret_cast<char*>(decrypted.data()), decrypted_len);
    EXPECT_EQ(plaintext, decrypted_text);
}

// Test security key derivation
TEST_F(BasicSecurityTest, KeyDerivation) {
    std::string password = "test_password";
    std::string salt = "test_salt_12345678"; // 16 bytes
    
    unsigned char derived_key[32];
    
    // Test PBKDF2
    int result = PKCS5_PBKDF2_HMAC(password.c_str(), password.length(),
                                   reinterpret_cast<const unsigned char*>(salt.c_str()), salt.length(),
                                   10000, // iterations
                                   EVP_sha256(),
                                   sizeof(derived_key),
                                   derived_key);
    
    EXPECT_EQ(result, 1);
    
    // Verify deterministic output
    unsigned char derived_key2[32];
    result = PKCS5_PBKDF2_HMAC(password.c_str(), password.length(),
                               reinterpret_cast<const unsigned char*>(salt.c_str()), salt.length(),
                               10000,
                               EVP_sha256(),
                               sizeof(derived_key2),
                               derived_key2);
    
    EXPECT_EQ(result, 1);
    EXPECT_EQ(memcmp(derived_key, derived_key2, sizeof(derived_key)), 0);
}

// Test input validation patterns
TEST_F(BasicSecurityTest, InputValidation) {
    // Test path validation patterns
    std::vector<std::string> valid_paths = {
        "/health",
        "/metrics",
        "/health/ready",
        "/api/v1/status"
    };
    
    std::vector<std::string> invalid_paths = {
        "../../../etc/passwd",
        "/health?<script>alert('xss')</script>",
        "/metrics?'; DROP TABLE users; --",
        "/admin/../../secret"
    };
    
    // Simple validation function
    auto is_valid_path = [](const std::string& path) {
        // Check for directory traversal
        if (path.find("..") != std::string::npos) return false;
        
        // Check for script injection
        if (path.find("<script") != std::string::npos) return false;
        if (path.find("javascript:") != std::string::npos) return false;
        
        // Check for SQL injection patterns
        if (path.find("DROP TABLE") != std::string::npos) return false;
        if (path.find("UNION SELECT") != std::string::npos) return false;
        
        return true;
    };
    
    // Test valid paths
    for (const auto& path : valid_paths) {
        EXPECT_TRUE(is_valid_path(path)) << "Valid path rejected: " << path;
    }
    
    // Test invalid paths
    for (const auto& path : invalid_paths) {
        EXPECT_FALSE(is_valid_path(path)) << "Invalid path accepted: " << path;
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}