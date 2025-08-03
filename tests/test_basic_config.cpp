#include <gtest/gtest.h>
#include <fstream>
#include <string>

// Basic configuration functionality tests
class BasicConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test configuration file
        create_test_config();
    }
    
    void TearDown() override {
        // Cleanup test files
        std::remove("test_config.json");
        std::remove("test_config.ini");
    }
    
    void create_test_config() {
        // Create JSON config
        std::ofstream json_file("test_config.json");
        json_file << R"({
            "security": {
                "authentication_required": true,
                "rate_limiting_enabled": true,
                "max_requests_per_minute": 60
            },
            "monitoring": {
                "health_check_port": 8080,
                "metrics_enabled": true
            }
        })";
        json_file.close();
        
        // Create INI config
        std::ofstream ini_file("test_config.ini");
        ini_file << "[security]\n";
        ini_file << "authentication_required=true\n";
        ini_file << "rate_limiting_enabled=true\n";
        ini_file << "max_requests_per_minute=60\n";
        ini_file << "\n[monitoring]\n";
        ini_file << "health_check_port=8080\n";
        ini_file << "metrics_enabled=true\n";
        ini_file.close();
    }
};

// Test basic file reading
TEST_F(BasicConfigTest, FileExists) {
    std::ifstream json_file("test_config.json");
    EXPECT_TRUE(json_file.good());
    json_file.close();
    
    std::ifstream ini_file("test_config.ini");
    EXPECT_TRUE(ini_file.good());
    ini_file.close();
}

// Test configuration value parsing
TEST_F(BasicConfigTest, ValueParsing) {
    // Test boolean parsing
    auto parse_bool = [](const std::string& value) {
        std::string lower_value = value;
        std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
        return lower_value == "true" || lower_value == "1" || lower_value == "yes";
    };
    
    EXPECT_TRUE(parse_bool("true"));
    EXPECT_TRUE(parse_bool("TRUE"));
    EXPECT_TRUE(parse_bool("1"));
    EXPECT_FALSE(parse_bool("false"));
    EXPECT_FALSE(parse_bool("0"));
    
    // Test integer parsing
    auto parse_int = [](const std::string& value) {
        try {
            return std::stoi(value);
        } catch (...) {
            return -1;
        }
    };
    
    EXPECT_EQ(parse_int("8080"), 8080);
    EXPECT_EQ(parse_int("60"), 60);
    EXPECT_EQ(parse_int("invalid"), -1);
}

// Test security configuration validation
TEST_F(BasicConfigTest, SecurityValidation) {
    // Test rate limiting configuration
    struct RateLimitConfig {
        bool enabled = false;
        int max_requests = 60;
        
        bool is_valid() const {
            return max_requests > 0 && max_requests <= 10000;
        }
    };
    
    RateLimitConfig valid_config;
    valid_config.enabled = true;
    valid_config.max_requests = 60;
    EXPECT_TRUE(valid_config.is_valid());
    
    RateLimitConfig invalid_config;
    invalid_config.enabled = true;
    invalid_config.max_requests = -1;
    EXPECT_FALSE(invalid_config.is_valid());
}

// Test configuration key validation
TEST_F(BasicConfigTest, KeyValidation) {
    auto validate_key = [](const std::string& key) {
        if (key.empty() || key.length() > 128) return false;
        
        for (char c : key) {
            if (!std::isalnum(c) && c != '_' && c != '.' && c != '-') {
                return false;
            }
        }
        return true;
    };
    
    EXPECT_TRUE(validate_key("security.authentication_required"));
    EXPECT_TRUE(validate_key("monitoring.health_check_port"));
    EXPECT_FALSE(validate_key("security<script>"));
    EXPECT_FALSE(validate_key("../../../etc/passwd"));
}

// Test environment variable parsing
TEST_F(BasicConfigTest, EnvironmentVariables) {
    // Set test environment variable
    setenv("GOLDEARN_TEST_VALUE", "test123", 1);
    
    // Test reading environment variable
    const char* env_value = getenv("GOLDEARN_TEST_VALUE");
    ASSERT_NE(env_value, nullptr);
    EXPECT_STREQ(env_value, "test123");
    
    // Cleanup
    unsetenv("GOLDEARN_TEST_VALUE");
}

// Test configuration security patterns
TEST_F(BasicConfigTest, SecurityPatterns) {
    // Test that we can detect potentially dangerous configurations
    std::vector<std::string> dangerous_values = {
        "../../etc/passwd",
        "<script>alert('xss')</script>",
        "'; DROP TABLE config; --",
        "javascript:alert(1)"
    };
    
    auto is_safe_value = [](const std::string& value) {
        // Check for common attack patterns
        std::string lower_value = value;
        std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
        
        std::vector<std::string> blocked_patterns = {
            "../", "<script", "javascript:", "drop table", "union select"
        };
        
        for (const auto& pattern : blocked_patterns) {
            if (lower_value.find(pattern) != std::string::npos) {
                return false;
            }
        }
        
        return true;
    };
    
    for (const auto& value : dangerous_values) {
        EXPECT_FALSE(is_safe_value(value)) << "Dangerous value not detected: " << value;
    }
    
    // Test safe values
    EXPECT_TRUE(is_safe_value("8080"));
    EXPECT_TRUE(is_safe_value("true"));
    EXPECT_TRUE(is_safe_value("production"));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}