#include <gtest/gtest.h>
#include "../src/network/exchange_auth.hpp"
#include "../src/config/config_manager.hpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace goldearn::network;
using namespace goldearn::config;

class ExchangeAuthTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/goldearn_auth_test";
        std::filesystem::create_directories(test_dir_);
        
        // Create test certificate files
        create_test_cert_file("test_cert.pem", "-----BEGIN CERTIFICATE-----\nTEST CERT DATA\n-----END CERTIFICATE-----");
        create_test_cert_file("test_key.pem", "-----BEGIN PRIVATE KEY-----\nTEST KEY DATA\n-----END PRIVATE KEY-----");
        
        // Create test config
        setup_test_config();
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    void create_test_cert_file(const std::string& filename, const std::string& content) {
        std::ofstream file(test_dir_ + "/" + filename);
        file << content;
        file.close();
    }

    void setup_test_config() {
        std::string config_content = R"([authentication]
nse_api_key = test_nse_api_key_1234567890abcdef
nse_secret_key = test_nse_secret_key_abcdef1234567890
nse_certificate_path = )" + test_dir_ + R"(/test_cert.pem
nse_private_key_path = )" + test_dir_ + R"(/test_key.pem
bse_api_key = test_bse_api_key_1234567890abcdef
bse_secret_key = test_bse_secret_key_abcdef1234567890
bse_certificate_path = )" + test_dir_ + R"(/test_cert.pem
bse_private_key_path = )" + test_dir_ + R"(/test_key.pem
)";

        std::ofstream config_file(test_dir_ + "/test_auth.conf");
        config_file << config_content;
        config_file.close();
    }

    std::string test_dir_;
};

TEST_F(ExchangeAuthTest, InitializeNSEWithAPIKey) {
    ExchangeAuthenticator nse_auth("NSE");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/test_auth.conf");
    ASSERT_TRUE(config);
    ASSERT_TRUE(nse_auth.initialize(*config));
    
    // Should prefer API key auth when both are available
    auto headers = nse_auth.get_auth_headers();
    EXPECT_FALSE(headers.empty());
}

TEST_F(ExchangeAuthTest, InitializeBSEWithAPIKey) {
    ExchangeAuthenticator bse_auth("BSE");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/test_auth.conf");
    ASSERT_TRUE(config);
    ASSERT_TRUE(bse_auth.initialize(*config));
    
    auto headers = bse_auth.get_auth_headers();
    EXPECT_FALSE(headers.empty());
}

TEST_F(ExchangeAuthTest, InitializeWithCertificateOnly) {
    // Create config with only certificate credentials
    std::string cert_only_config = R"([authentication]
nse_certificate_path = )" + test_dir_ + R"(/test_cert.pem
nse_private_key_path = )" + test_dir_ + R"(/test_key.pem
)";

    std::ofstream config_file(test_dir_ + "/cert_only.conf");
    config_file << cert_only_config;
    config_file.close();
    
    auto cert_config = ConfigManager::load_from_file(test_dir_ + "/cert_only.conf");
    
    ExchangeAuthenticator nse_auth("NSE");
    ASSERT_TRUE(nse_auth.initialize(*cert_config));
}

TEST_F(ExchangeAuthTest, InitializeWithMissingCredentials) {
    // Create config with missing credentials
    std::string empty_config = "[authentication]\n";
    
    std::ofstream config_file(test_dir_ + "/empty.conf");
    config_file << empty_config;
    config_file.close();
    
    auto empty_config_mgr = ConfigManager::load_from_file(test_dir_ + "/empty.conf");
    
    ExchangeAuthenticator nse_auth("NSE");
    EXPECT_FALSE(nse_auth.initialize(*empty_config_mgr));
}

TEST_F(ExchangeAuthTest, InitializeWithMissingCertificateFiles) {
    // Create config with certificate paths that don't exist
    std::string bad_cert_config = R"([authentication]
nse_certificate_path = /nonexistent/cert.pem
nse_private_key_path = /nonexistent/key.pem
)";

    std::ofstream config_file(test_dir_ + "/bad_cert.conf");
    config_file << bad_cert_config;
    config_file.close();
    
    auto bad_config = ConfigManager::load_from_file(test_dir_ + "/bad_cert.conf");
    
    ExchangeAuthenticator nse_auth("NSE");
    EXPECT_FALSE(nse_auth.initialize(*bad_config));
}

TEST_F(ExchangeAuthTest, InitializeUnknownExchange) {
    ExchangeAuthenticator unknown_auth("UNKNOWN");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/test_auth.conf");
    ASSERT_TRUE(config);
    EXPECT_FALSE(unknown_auth.initialize(*config));
}

TEST_F(ExchangeAuthTest, AuthenticationStateManagement) {
    ExchangeAuthenticator nse_auth("NSE");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/test_auth.conf");
    ASSERT_TRUE(config);
    ASSERT_TRUE(nse_auth.initialize(*config));
    
    // Initially not authenticated
    EXPECT_FALSE(nse_auth.is_authenticated());
    
    // Note: We can't test actual authentication without a real server
    // So we'll test the state management logic
}

TEST_F(ExchangeAuthTest, AuthHeaderGeneration) {
    ExchangeAuthenticator nse_auth("NSE");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/test_auth.conf");
    ASSERT_TRUE(config);
    ASSERT_TRUE(nse_auth.initialize(*config));
    
    // Before authentication, headers should be minimal
    auto headers_before = nse_auth.get_auth_headers();
    
    // Common headers should always be present
    EXPECT_TRUE(headers_before.find("User-Agent") != headers_before.end());
    EXPECT_TRUE(headers_before.find("Accept") != headers_before.end());
    EXPECT_TRUE(headers_before.find("Content-Type") != headers_before.end());
}

TEST_F(ExchangeAuthTest, AuthStringGeneration) {
    ExchangeAuthenticator nse_auth("NSE");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/test_auth.conf");
    ASSERT_TRUE(config);
    ASSERT_TRUE(nse_auth.initialize(*config));
    
    // Before authentication, auth string should be empty
    std::string auth_string = nse_auth.get_auth_string();
    EXPECT_TRUE(auth_string.empty());
}

TEST_F(ExchangeAuthTest, CallbackRegistration) {
    ExchangeAuthenticator nse_auth("NSE");
    
    bool callback_called = false;
    bool callback_success = false;
    std::string callback_message;
    
    nse_auth.set_auth_callback([&](bool success, const std::string& message) {
        callback_called = true;
        callback_success = success;
        callback_message = message;
    });
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/test_auth.conf");
    ASSERT_TRUE(config);
    ASSERT_TRUE(nse_auth.initialize(*config));
    
    // Callback should be registered but not called yet
    EXPECT_FALSE(callback_called);
}

TEST_F(ExchangeAuthTest, MultiExchangeAuthManager) {
    MultiExchangeAuthManager manager;
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/test_auth.conf");
    ASSERT_TRUE(config);
    ASSERT_TRUE(manager.initialize(*config));
    
    // Should have authenticators for both NSE and BSE
    auto nse_auth = manager.get_authenticator("NSE");
    auto bse_auth = manager.get_authenticator("BSE");
    
    EXPECT_TRUE(nse_auth != nullptr);
    EXPECT_TRUE(bse_auth != nullptr);
    
    // Unknown exchange should return nullptr
    auto unknown_auth = manager.get_authenticator("UNKNOWN");
    EXPECT_TRUE(unknown_auth == nullptr);
}

TEST_F(ExchangeAuthTest, GlobalCallbackForMultiExchange) {
    MultiExchangeAuthManager manager;
    
    std::vector<std::string> callback_exchanges;
    std::vector<bool> callback_results;
    
    manager.set_global_callback([&](const std::string& exchange, bool success, const std::string& message) {
        callback_exchanges.push_back(exchange);
        callback_results.push_back(success);
    });
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/test_auth.conf");
    ASSERT_TRUE(config);
    ASSERT_TRUE(manager.initialize(*config));
    
    // Callbacks should be registered but not called during initialization
    EXPECT_TRUE(callback_exchanges.empty());
}

// Auth utility function tests
TEST(AuthUtilsTest, SessionIdGeneration) {
    std::string session_id1 = auth_utils::generate_session_id();
    std::string session_id2 = auth_utils::generate_session_id();
    
    // Should generate different IDs
    EXPECT_NE(session_id1, session_id2);
    
    // Should be reasonable length
    EXPECT_GT(session_id1.length(), 16);
    EXPECT_LT(session_id1.length(), 64);
}

TEST(AuthUtilsTest, HMACSignatureGeneration) {
    std::string message = "test_message";
    std::string key = "test_key";
    
    std::string sig1 = auth_utils::generate_hmac_signature(message, key);
    std::string sig2 = auth_utils::generate_hmac_signature(message, key);
    
    // Same input should produce same signature
    EXPECT_EQ(sig1, sig2);
    
    // Different message should produce different signature
    std::string sig3 = auth_utils::generate_hmac_signature("different_message", key);
    EXPECT_NE(sig1, sig3);
    
    // Different key should produce different signature
    std::string sig4 = auth_utils::generate_hmac_signature(message, "different_key");
    EXPECT_NE(sig1, sig4);
}

TEST(AuthUtilsTest, APIKeyValidation) {
    // Valid NSE API key
    EXPECT_TRUE(auth_utils::validate_api_key_format("1234567890abcdef1234567890abcdef", "NSE"));
    
    // Valid BSE API key
    EXPECT_TRUE(auth_utils::validate_api_key_format("abcdef1234567890abcdef1234567890", "BSE"));
    
    // Too short
    EXPECT_FALSE(auth_utils::validate_api_key_format("short", "NSE"));
    
    // Empty
    EXPECT_FALSE(auth_utils::validate_api_key_format("", "NSE"));
    
    // Too long
    std::string too_long(100, 'a');
    EXPECT_FALSE(auth_utils::validate_api_key_format(too_long, "NSE"));
}

TEST(AuthUtilsTest, CredentialEncryption) {
    std::string data = "sensitive_credential_data";
    std::string key = "encryption_key";
    
    std::string encrypted = auth_utils::encrypt_credentials(data, key);
    std::string decrypted = auth_utils::decrypt_credentials(encrypted, key);
    
    // Should be able to decrypt to original data
    EXPECT_EQ(data, decrypted);
    
    // Encrypted data should be different from original
    EXPECT_NE(data, encrypted);
    
    // Wrong key should produce different result
    std::string wrong_decrypted = auth_utils::decrypt_credentials(encrypted, "wrong_key");
    EXPECT_NE(data, wrong_decrypted);
}

// Performance tests
TEST_F(ExchangeAuthTest, AuthenticationPerformanceTest) {
    ExchangeAuthenticator nse_auth("NSE");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/test_auth.conf");
    ASSERT_TRUE(config);
    ASSERT_TRUE(nse_auth.initialize(*config));
    
    // Test header generation performance
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 1000; ++i) {
        auto headers = nse_auth.get_auth_headers();
        EXPECT_FALSE(headers.empty());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Should generate headers quickly (< 1ms per call)
    EXPECT_LT(duration.count() / 1000, 1000); // Average < 1ms per call
}

TEST_F(ExchangeAuthTest, ConcurrentAuthHeaderGeneration) {
    ExchangeAuthenticator nse_auth("NSE");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/test_auth.conf");
    ASSERT_TRUE(config);
    ASSERT_TRUE(nse_auth.initialize(*config));
    
    std::atomic<int> successful_calls{0};
    std::atomic<bool> test_running{true};
    
    // Start multiple threads generating auth headers
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            while (test_running) {
                auto headers = nse_auth.get_auth_headers();
                if (!headers.empty()) {
                    successful_calls++;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }
    
    // Let threads run for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    test_running = false;
    
    // Wait for all threads to finish
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Should have successful concurrent calls
    EXPECT_GT(successful_calls.load(), 0);
}

// Edge case tests
TEST_F(ExchangeAuthTest, TokenExpiryEdgeCases) {
    ExchangeAuthenticator nse_auth("NSE");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/test_auth.conf");
    ASSERT_TRUE(config);
    ASSERT_TRUE(nse_auth.initialize(*config));
    
    // Test with certificate auth (should not expire)
    // This would require modifying the authenticator to use certificate auth
    // For now, just test that the method doesn't crash
    EXPECT_FALSE(nse_auth.is_authenticated()); // Not authenticated yet
}

TEST_F(ExchangeAuthTest, AuthenticatorDestructorSafety) {
    auto config = ConfigManager::load_from_file(test_dir_ + "/test_auth.conf");
    ASSERT_TRUE(config);
    
    // Create authenticator in scope
    {
        ExchangeAuthenticator nse_auth("NSE");
        ASSERT_TRUE(nse_auth.initialize(*config));
        // Destructor should handle cleanup safely
    }
    
    // Should reach here without crashes
    SUCCEED();
}

TEST_F(ExchangeAuthTest, InvalidExchangeNameHandling) {
    // Test various invalid exchange names
    std::vector<std::string> invalid_names = {"", "nse", "bse", "NYSE", "NASDAQ", "123", "NSE_TEST"};
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/test_auth.conf");
    ASSERT_TRUE(config);
    
    for (const auto& name : invalid_names) {
        ExchangeAuthenticator auth(name);
        EXPECT_FALSE(auth.initialize(*config)) << "Should fail for exchange name: " << name;
    }
}

TEST_F(ExchangeAuthTest, InitializeWithValidConfig) {
    // Create a valid configuration
    auto config = ConfigManager::load_from_file(test_dir_ + "/valid_auth.conf");
    ASSERT_TRUE(config);
    
    goldearn::network::ExchangeAuthenticator nse_auth("NSE");
    EXPECT_TRUE(nse_auth.initialize(*config));
}

TEST_F(ExchangeAuthTest, InitializeWithEmptyConfig) {
    // Create an empty configuration
    auto config = ConfigManager::load_from_file(test_dir_ + "/empty.conf");
    ASSERT_TRUE(config);
    
    goldearn::network::ExchangeAuthenticator nse_auth("NSE");
    EXPECT_FALSE(nse_auth.initialize(*config));
}

TEST_F(ExchangeAuthTest, InitializeWithInvalidConfig) {
    // Create an invalid configuration
    auto config = ConfigManager::load_from_file(test_dir_ + "/invalid_auth.conf");
    ASSERT_TRUE(config);
    
    goldearn::network::ExchangeAuthenticator nse_auth("NSE");
    EXPECT_FALSE(nse_auth.initialize(*config));
}

TEST_F(ExchangeAuthTest, TestNSECredentials) {
    auto config = ConfigManager::load_from_file(test_dir_ + "/nse_auth.conf");
    ASSERT_TRUE(config);
    
    goldearn::network::ExchangeAuthenticator nse_auth("NSE");
    EXPECT_TRUE(nse_auth.initialize(*config));
    
    // Test credential retrieval
    auto credentials = nse_auth.get_credentials();
    EXPECT_EQ(credentials.api_key, "test_nse_api_key_1234567890abcdef");
    EXPECT_EQ(credentials.secret_key, "test_nse_secret_key_abcdef1234567890");
}

TEST_F(ExchangeAuthTest, TestBSECredentials) {
    auto config = ConfigManager::load_from_file(test_dir_ + "/bse_auth.conf");
    ASSERT_TRUE(config);
    
    goldearn::network::ExchangeAuthenticator bse_auth("BSE");
    EXPECT_TRUE(bse_auth.initialize(*config));
    
    // Test credential retrieval
    auto credentials = bse_auth.get_credentials();
    EXPECT_EQ(credentials.api_key, "test_bse_api_key_1234567890abcdef");
    EXPECT_EQ(credentials.secret_key, "test_bse_secret_key_abcdef1234567890");
}

TEST_F(ExchangeAuthTest, TestCertificateAuthentication) {
    auto config = ConfigManager::load_from_file(test_dir_ + "/cert_auth.conf");
    ASSERT_TRUE(config);
    
    goldearn::network::ExchangeAuthenticator cert_auth("NSE");
    EXPECT_TRUE(cert_auth.initialize(*config));
    
    auto credentials = cert_auth.get_credentials();
    EXPECT_EQ(credentials.method, goldearn::network::ExchangeAuthenticator::AuthMethod::CERTIFICATE);
    EXPECT_EQ(credentials.certificate_path, test_dir_ + "/test_cert.pem");
    EXPECT_EQ(credentials.private_key_path, test_dir_ + "/test_key.pem");
}

TEST_F(ExchangeAuthTest, TestSessionTokenManagement) {
    auto config = ConfigManager::load_from_file(test_dir_ + "/session_auth.conf");
    ASSERT_TRUE(config);
    
    goldearn::network::ExchangeAuthenticator session_auth("NSE");
    EXPECT_TRUE(session_auth.initialize(*config));
    
    // Test session token operations
    EXPECT_TRUE(session_auth.refresh_session_token());
    
    auto credentials = session_auth.get_credentials();
    EXPECT_FALSE(credentials.session_token.empty());
    auto now = std::chrono::system_clock::now();
    EXPECT_GT(credentials.token_expiry, now);
}

TEST_F(ExchangeAuthTest, TestAuthenticationFailure) {
    auto config = ConfigManager::load_from_file(test_dir_ + "/bad_auth.conf");
    ASSERT_TRUE(config);
    
    goldearn::network::ExchangeAuthenticator bad_auth("NSE");
    EXPECT_FALSE(bad_auth.initialize(*config));
}

TEST_F(ExchangeAuthTest, TestMultipleExchanges) {
    auto config = ConfigManager::load_from_file(test_dir_ + "/multi_auth.conf");
    ASSERT_TRUE(config);
    
    goldearn::network::ExchangeAuthenticator nse_auth("NSE");
    goldearn::network::ExchangeAuthenticator bse_auth("BSE");
    
    EXPECT_TRUE(nse_auth.initialize(*config));
    EXPECT_TRUE(bse_auth.initialize(*config));
    
    auto nse_creds = nse_auth.get_credentials();
    auto bse_creds = bse_auth.get_credentials();
    
    EXPECT_NE(nse_creds.api_key, bse_creds.api_key);
    EXPECT_NE(nse_creds.secret_key, bse_creds.secret_key);
}

TEST_F(ExchangeAuthTest, TestInvalidExchangeName) {
    auto config = ConfigManager::load_from_file(test_dir_ + "/valid_auth.conf");
    ASSERT_TRUE(config);
    
    goldearn::network::ExchangeAuthenticator invalid_auth("INVALID");
    EXPECT_FALSE(invalid_auth.initialize(*config));
}

TEST_F(ExchangeAuthTest, TestMissingCredentials) {
    auto config = ConfigManager::load_from_file(test_dir_ + "/missing_auth.conf");
    ASSERT_TRUE(config);
    
    goldearn::network::ExchangeAuthenticator missing_auth("NSE");
    EXPECT_FALSE(missing_auth.initialize(*config));
}

TEST_F(ExchangeAuthTest, TestCredentialEncryption) {
    auto config = ConfigManager::load_from_file(test_dir_ + "/encrypted_auth.conf");
    ASSERT_TRUE(config);
    
    goldearn::network::ExchangeAuthenticator encrypted_auth("NSE");
    EXPECT_TRUE(encrypted_auth.initialize(*config));
    
    auto credentials = encrypted_auth.get_credentials();
    // Test that credentials were loaded (actual encryption test would need implementation)
    EXPECT_FALSE(credentials.api_key.empty());
}

TEST_F(ExchangeAuthTest, TestTokenExpiry) {
    auto config = ConfigManager::load_from_file(test_dir_ + "/expired_auth.conf");
    ASSERT_TRUE(config);
    
    goldearn::network::ExchangeAuthenticator expired_auth("NSE");
    EXPECT_TRUE(expired_auth.initialize(*config));
    
    auto credentials = expired_auth.get_credentials();
    auto now = std::chrono::system_clock::now();
    EXPECT_LT(credentials.token_expiry, now);
    
    // Should trigger token refresh
    EXPECT_TRUE(expired_auth.refresh_session_token());
}