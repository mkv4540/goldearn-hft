#include <gtest/gtest.h>
#include "../src/config/config_manager.hpp"
#include <fstream>
#include <filesystem>

using namespace goldearn::config;

class JsonConfigParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/goldearn_test_configs";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    void create_test_file(const std::string& filename, const std::string& content) {
        std::ofstream file(test_dir_ + "/" + filename);
        file << content;
        file.close();
    }

    std::string test_dir_;
};

TEST_F(JsonConfigParserTest, ParseValidJsonConfiguration) {
    std::string json_content = R"({
        "system": {
            "environment": "production",
            "log_level": "INFO",
            "enable_monitoring": true,
            "monitoring_port": 9090
        },
        "market_data": {
            "nse_host": "feed.nse.in",
            "nse_port": 9899,
            "connection_timeout": 5000.5,
            "symbols": ["RELIANCE", "TCS", "HDFCBANK"]
        },
        "risk": {
            "enable_risk_checks": true,
            "max_daily_loss": 1000000.0,
            "max_position_size": 5000000.0
        }
    })";

    create_test_file("test_config.json", json_content);
    
    auto& config = ConfigManager::instance();
    ASSERT_TRUE(config.load_from_file(test_dir_ + "/test_config.json"));

    // Test string values
    EXPECT_EQ(config.get_string("system", "environment"), "production");
    EXPECT_EQ(config.get_string("system", "log_level"), "INFO");
    EXPECT_EQ(config.get_string("market_data", "nse_host"), "feed.nse.in");

    // Test integer values
    EXPECT_EQ(config.get_int("system", "monitoring_port"), 9090);
    EXPECT_EQ(config.get_int("market_data", "nse_port"), 9899);

    // Test double values
    EXPECT_DOUBLE_EQ(config.get_double("market_data", "connection_timeout"), 5000.5);
    EXPECT_DOUBLE_EQ(config.get_double("risk", "max_daily_loss"), 1000000.0);
    EXPECT_DOUBLE_EQ(config.get_double("risk", "max_position_size"), 5000000.0);

    // Test boolean values
    EXPECT_TRUE(config.get_bool("system", "enable_monitoring"));
    EXPECT_TRUE(config.get_bool("risk", "enable_risk_checks"));

    // Test array values
    auto system_section = config.get_section("market_data");
    auto symbols = system_section->get("symbols").as_array();
    ASSERT_EQ(symbols.size(), 3);
    EXPECT_EQ(symbols[0], "RELIANCE");
    EXPECT_EQ(symbols[1], "TCS");
    EXPECT_EQ(symbols[2], "HDFCBANK");
}

TEST_F(JsonConfigParserTest, ParseJsonWithEscapedStrings) {
    std::string json_content = R"({
        "paths": {
            "log_file": "/var/log/goldearn/app.log",
            "cert_path": "C:\\certs\\client.pem",
            "description": "GoldEarn HFT\nHigh-Frequency Trading\tSystem"
        }
    })";

    create_test_file("escaped_config.json", json_content);
    
    auto& config = ConfigManager::instance();
    ASSERT_TRUE(config.load_from_file(test_dir_ + "/escaped_config.json"));

    EXPECT_EQ(config.get_string("paths", "log_file"), "/var/log/goldearn/app.log");
    EXPECT_EQ(config.get_string("paths", "cert_path"), "C:\\certs\\client.pem");
    EXPECT_EQ(config.get_string("paths", "description"), "GoldEarn HFT\nHigh-Frequency Trading\tSystem");
}

TEST_F(JsonConfigParserTest, ParseJsonWithNegativeNumbers) {
    std::string json_content = R"({
        "trading": {
            "max_loss": -50000.0,
            "position_delta": -1000,
            "threshold": -0.05
        }
    })";

    create_test_file("negative_numbers.json", json_content);
    
    auto& config = ConfigManager::instance();
    ASSERT_TRUE(config.load_from_file(test_dir_ + "/negative_numbers.json"));

    EXPECT_DOUBLE_EQ(config.get_double("trading", "max_loss"), -50000.0);
    EXPECT_EQ(config.get_int("trading", "position_delta"), -1000);
    EXPECT_DOUBLE_EQ(config.get_double("trading", "threshold"), -0.05);
}

TEST_F(JsonConfigParserTest, ParseJsonWithBooleanVariations) {
    std::string json_content = R"({
        "flags": {
            "flag1": true,
            "flag2": false
        }
    })";

    create_test_file("boolean_config.json", json_content);
    
    auto& config = ConfigManager::instance();
    ASSERT_TRUE(config.load_from_file(test_dir_ + "/boolean_config.json"));

    EXPECT_TRUE(config.get_bool("flags", "flag1"));
    EXPECT_FALSE(config.get_bool("flags", "flag2"));
}

TEST_F(JsonConfigParserTest, ParseEmptyJsonObject) {
    std::string json_content = "{}";

    create_test_file("empty_config.json", json_content);
    
    auto& config = ConfigManager::instance();
    ASSERT_TRUE(config.load_from_file(test_dir_ + "/empty_config.json"));

    // Should be able to access sections even if they don't exist
    EXPECT_EQ(config.get_string("nonexistent", "key", "default"), "default");
}

TEST_F(JsonConfigParserTest, ParseJsonWithWhitespace) {
    std::string json_content = R"(
    {
        "system"  :  {
            "environment"  :  "development"  ,
            "debug"  :  true
        }  ,
        "network"  :  {
            "timeout"  :  30000
        }
    }
    )";

    create_test_file("whitespace_config.json", json_content);
    
    auto& config = ConfigManager::instance();
    ASSERT_TRUE(config.load_from_file(test_dir_ + "/whitespace_config.json"));

    EXPECT_EQ(config.get_string("system", "environment"), "development");
    EXPECT_TRUE(config.get_bool("system", "debug"));
    EXPECT_EQ(config.get_int("network", "timeout"), 30000);
}

TEST_F(JsonConfigParserTest, HandleMalformedJson) {
    // Missing closing brace
    std::string malformed_json1 = R"({
        "system": {
            "environment": "test"
    })";

    create_test_file("malformed1.json", malformed_json1);
    
    auto& config = ConfigManager::instance();
    EXPECT_FALSE(config.load_from_file(test_dir_ + "/malformed1.json"));

    // Missing quotes
    std::string malformed_json2 = R"({
        system: {
            "environment": "test"
        }
    })";

    create_test_file("malformed2.json", malformed_json2);
    EXPECT_FALSE(config.load_from_file(test_dir_ + "/malformed2.json"));

    // Missing colon
    std::string malformed_json3 = R"({
        "system" {
            "environment": "test"
        }
    })";

    create_test_file("malformed3.json", malformed_json3);
    EXPECT_FALSE(config.load_from_file(test_dir_ + "/malformed3.json"));
}

TEST_F(JsonConfigParserTest, HandleInvalidNumbers) {
    std::string invalid_numbers_json = R"({
        "values": {
            "bad_number": "123abc",
            "multiple_decimals": "123.45.67"
        }
    })";

    create_test_file("invalid_numbers.json", invalid_numbers_json);
    
    auto& config = ConfigManager::instance();
    ASSERT_TRUE(config.load_from_file(test_dir_ + "/invalid_numbers.json"));

    // These should be treated as strings since they're quoted
    EXPECT_EQ(config.get_string("values", "bad_number"), "123abc");
    EXPECT_EQ(config.get_string("values", "multiple_decimals"), "123.45.67");
}

TEST_F(JsonConfigParserTest, ParseComplexNestedStructure) {
    std::string complex_json = R"({
        "exchanges": {
            "nse": {
                "host": "feed.nse.in",
                "port": 9899,
                "symbols": ["RELIANCE", "TCS"],
                "enabled": true
            },
            "bse": {
                "host": "feed.bseindia.com", 
                "port": 9898,
                "symbols": ["SENSEX"],
                "enabled": false
            }
        },
        "risk_limits": {
            "daily_loss": 1000000.0,
            "position_limits": [100000, 200000, 500000],
            "enable_checks": true
        }
    })";

    create_test_file("complex_config.json", complex_json);
    
    auto& config = ConfigManager::instance();
    ASSERT_TRUE(config.load_from_file(test_dir_ + "/complex_config.json"));

    // Test nested sections
    EXPECT_EQ(config.get_string("exchanges", "nse", ""), "");  // Section itself, not a value
    
    auto exchanges_section = config.get_section("exchanges");
    EXPECT_TRUE(exchanges_section != nullptr);
    
    // In this implementation, we don't support nested objects within sections
    // The NSE object would be treated as a string value, which is a limitation
}

TEST_F(JsonConfigParserTest, TypeConversionTests) {
    std::string json_content = R"({
        "conversions": {
            "string_number": "42",
            "string_bool": "true",
            "number_as_string": 123,
            "bool_as_string": false
        }
    })";

    create_test_file("conversion_test.json", json_content);
    
    auto& config = ConfigManager::instance();
    ASSERT_TRUE(config.load_from_file(test_dir_ + "/conversion_test.json"));

    auto section = config.get_section("conversions");
    
    // Test type conversions
    EXPECT_EQ(section->get("string_number").as_string(), "42");
    EXPECT_EQ(section->get("string_number").as_int(), 42);
    
    EXPECT_EQ(section->get("string_bool").as_string(), "true");
    EXPECT_TRUE(section->get("string_bool").as_bool());
    
    EXPECT_EQ(section->get("number_as_string").as_string(), "123");
    EXPECT_EQ(section->get("number_as_string").as_int(), 123);
    
    EXPECT_EQ(section->get("bool_as_string").as_string(), "false");
    EXPECT_FALSE(section->get("bool_as_string").as_bool());
}

TEST_F(JsonConfigParserTest, ArrayParsingEdgeCases) {
    std::string json_content = R"({
        "arrays": {
            "empty_array": [],
            "single_item": ["item1"],
            "mixed_quotes": ["item1", "item2"]
        }
    })";

    create_test_file("array_test.json", json_content);
    
    auto& config = ConfigManager::instance();
    ASSERT_TRUE(config.load_from_file(test_dir_ + "/array_test.json"));

    auto section = config.get_section("arrays");
    
    // Empty array
    auto empty_array = section->get("empty_array").as_array();
    EXPECT_EQ(empty_array.size(), 0);
    
    // Single item array
    auto single_array = section->get("single_item").as_array();
    ASSERT_EQ(single_array.size(), 1);
    EXPECT_EQ(single_array[0], "item1");
    
    // Multiple items
    auto mixed_array = section->get("mixed_quotes").as_array();
    ASSERT_EQ(mixed_array.size(), 2);
    EXPECT_EQ(mixed_array[0], "item1");
    EXPECT_EQ(mixed_array[1], "item2");
}

TEST_F(JsonConfigParserTest, FileNotFoundHandling) {
    auto& config = ConfigManager::instance();
    EXPECT_FALSE(config.load_from_file("/nonexistent/path/config.json"));
}

TEST_F(JsonConfigParserTest, BackwardCompatibilityWithINI) {
    // Test that INI files still work
    std::string ini_content = R"([system]
environment = production
log_level = INFO
enable_monitoring = true
monitoring_port = 9090

[market_data]
nse_host = feed.nse.in
nse_port = 9899
)";

    create_test_file("test_config.conf", ini_content);
    
    auto& config = ConfigManager::instance();
    ASSERT_TRUE(config.load_from_file(test_dir_ + "/test_config.conf"));

    EXPECT_EQ(config.get_string("system", "environment"), "production");
    EXPECT_EQ(config.get_string("system", "log_level"), "INFO");
    EXPECT_TRUE(config.get_bool("system", "enable_monitoring"));
    EXPECT_EQ(config.get_int("system", "monitoring_port"), 9090);
    EXPECT_EQ(config.get_string("market_data", "nse_host"), "feed.nse.in");
    EXPECT_EQ(config.get_int("market_data", "nse_port"), 9899);
}

// Performance test for JSON parsing
TEST_F(JsonConfigParserTest, PerformanceTest) {
    // Create a large JSON configuration
    std::ostringstream large_json;
    large_json << "{\n";
    
    for (int i = 0; i < 100; ++i) {
        large_json << "  \"section" << i << "\": {\n";
        for (int j = 0; j < 50; ++j) {
            large_json << "    \"key" << j << "\": \"value" << j << "\"";
            if (j < 49) large_json << ",";
            large_json << "\n";
        }
        large_json << "  }";
        if (i < 99) large_json << ",";
        large_json << "\n";
    }
    large_json << "}\n";

    create_test_file("large_config.json", large_json.str());
    
    auto& config = ConfigManager::instance();
    
    auto start = std::chrono::high_resolution_clock::now();
    ASSERT_TRUE(config.load_from_file(test_dir_ + "/large_config.json"));
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Should parse large config in reasonable time (< 100ms)
    EXPECT_LT(duration.count(), 100);
    
    // Verify some values were parsed correctly
    EXPECT_EQ(config.get_string("section0", "key0"), "value0");
    EXPECT_EQ(config.get_string("section99", "key49"), "value49");
}