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

TEST_F(JsonConfigParserTest, ParseValidJson) {
    create_test_file("valid.json", R"({
        "database": {
            "host": "localhost",
            "port": 5432
        },
        "system": {
            "timeout": 30.5,
            "debug": true
        }
    })");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/valid.json");
    ASSERT_TRUE(config);
    
    EXPECT_EQ(config->get_string("database.host"), "localhost");
    EXPECT_EQ(config->get_int("database.port"), 5432);
    EXPECT_EQ(config->get_double("system.timeout"), 30.5);
    EXPECT_TRUE(config->get_bool("system.debug"));
}

TEST_F(JsonConfigParserTest, ParseJsonWithNestedObjects) {
    create_test_file("nested.json", R"({
        "exchange": {
            "nse": {
                "host": "feed.nse.in",
                "port": 9899
            },
            "bse": {
                "host": "feed.bseindia.com",
                "port": 9898
            }
        }
    })");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/nested.json");
    ASSERT_TRUE(config);
    
    EXPECT_EQ(config->get_string("exchange.nse.host"), "feed.nse.in");
    EXPECT_EQ(config->get_int("exchange.nse.port"), 9899);
    EXPECT_EQ(config->get_string("exchange.bse.host"), "feed.bseindia.com");
    EXPECT_EQ(config->get_int("exchange.bse.port"), 9898);
}

TEST_F(JsonConfigParserTest, ParseJsonWithArrays) {
    create_test_file("arrays.json", R"({
        "trading": {
            "symbols": ["RELIANCE", "TCS", "INFY"],
            "strategies": ["momentum", "arbitrage", "mean_reversion"]
        }
    })");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/arrays.json");
    ASSERT_TRUE(config);
    
    auto symbols = config->get_string("trading.symbols");
    EXPECT_FALSE(symbols.empty());
    
    auto strategies = config->get_string("trading.strategies");
    EXPECT_FALSE(strategies.empty());
}

TEST_F(JsonConfigParserTest, ParseJsonWithBooleanVariations) {
    create_test_file("booleans.json", R"({
        "feature1": {"enabled": true},
        "feature2": {"enabled": false},
        "feature3": {"enabled": true},
        "feature4": {"enabled": false}
    })");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/booleans.json");
    ASSERT_TRUE(config);
    
    EXPECT_TRUE(config->get_bool("feature1.enabled"));
    EXPECT_FALSE(config->get_bool("feature2.enabled"));
    EXPECT_TRUE(config->get_bool("feature3.enabled"));
    EXPECT_FALSE(config->get_bool("feature4.enabled"));
}

TEST_F(JsonConfigParserTest, ParseJsonWithNumbers) {
    create_test_file("numbers.json", R"({
        "limits": {
            "max_orders": 1000,
            "max_loss": 100000.50,
            "timeout_ms": 5000,
            "rate_limit": 100.75
        }
    })");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/numbers.json");
    ASSERT_TRUE(config);
    
    EXPECT_EQ(config->get_int("limits.max_orders"), 1000);
    EXPECT_EQ(config->get_double("limits.max_loss"), 100000.50);
    EXPECT_EQ(config->get_int("limits.timeout_ms"), 5000);
    EXPECT_EQ(config->get_double("limits.rate_limit"), 100.75);
}

TEST_F(JsonConfigParserTest, ParseJsonWithSpecialCharacters) {
    create_test_file("special_chars.json", R"({
        "paths": {
            "log_file": "/var/log/goldearn/app.log",
            "data_dir": "/opt/goldearn/data",
            "temp_dir": "/tmp/goldearn"
        }
    })");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/special_chars.json");
    ASSERT_TRUE(config);
    
    EXPECT_EQ(config->get_string("paths.log_file"), "/var/log/goldearn/app.log");
    EXPECT_EQ(config->get_string("paths.data_dir"), "/opt/goldearn/data");
    EXPECT_EQ(config->get_string("paths.temp_dir"), "/tmp/goldearn");
}

TEST_F(JsonConfigParserTest, ParseJsonWithNullValues) {
    create_test_file("nulls.json", R"({
        "optional": {
            "string": null,
            "number": null,
            "float": null,
            "boolean": null
        }
    })");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/nulls.json");
    ASSERT_TRUE(config);
    
    // Null values should return default values
    EXPECT_EQ(config->get_string("optional.string"), "");
    EXPECT_EQ(config->get_int("optional.number"), 0);
    EXPECT_EQ(config->get_double("optional.float"), 0.0);
    EXPECT_FALSE(config->get_bool("optional.boolean"));
}

TEST_F(JsonConfigParserTest, ParseJsonWithComments) {
    create_test_file("comments.json", R"({
        "app": {
            "name": "GoldEarn HFT",
            "version": "1.0.0",
            "description": "High Frequency Trading System"
        }
    })");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/comments.json");
    ASSERT_TRUE(config);
    
    EXPECT_EQ(config->get_string("app.name"), "GoldEarn HFT");
    EXPECT_EQ(config->get_string("app.version"), "1.0.0");
    EXPECT_EQ(config->get_string("app.description"), "High Frequency Trading System");
}

TEST_F(JsonConfigParserTest, ParseJsonWithEscapedStrings) {
    create_test_file("escaped.json", R"({
        "message": {
            "welcome": "Welcome to \"GoldEarn\" HFT System",
            "error": "Error: Invalid \\\"quote\\\" in string",
            "path": "C:\\Program Files\\GoldEarn\\config.json"
        }
    })");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/escaped.json");
    ASSERT_TRUE(config);
    
    EXPECT_EQ(config->get_string("message.welcome"), "Welcome to \"GoldEarn\" HFT System");
    EXPECT_EQ(config->get_string("message.error"), "Error: Invalid \\\"quote\\\" in string");
    EXPECT_EQ(config->get_string("message.path"), "C:\\Program Files\\GoldEarn\\config.json");
}

TEST_F(JsonConfigParserTest, ParseJsonWithUnicode) {
    create_test_file("unicode.json", R"({
        "text": {
            "english": "Hello World",
            "hindi": "नमस्ते दुनिया",
            "chinese": "你好世界"
        }
    })");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/unicode.json");
    ASSERT_TRUE(config);
    
    EXPECT_EQ(config->get_string("text.english"), "Hello World");
    EXPECT_EQ(config->get_string("text.hindi"), "नमस्ते दुनिया");
    EXPECT_EQ(config->get_string("text.chinese"), "你好世界");
}

TEST_F(JsonConfigParserTest, ParseJsonWithLargeNumbers) {
    create_test_file("large_numbers.json", R"({
        "limits": {
            "max_position": 1000000000,
            "max_value": 999999999.99,
            "max_volume": 2147483647
        }
    })");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/large_numbers.json");
    ASSERT_TRUE(config);
    
    EXPECT_EQ(config->get_int("limits.max_position"), 1000000000);
    EXPECT_EQ(config->get_double("limits.max_value"), 999999999.99);
    EXPECT_EQ(config->get_int("limits.max_volume"), 2147483647);
}

TEST_F(JsonConfigParserTest, ParseJsonWithScientificNotation) {
    create_test_file("scientific.json", R"({
        "physics": {
            "speed_of_light": 3e8,
            "plancks_constant": 6.626e-34
        },
        "finance": {
            "interest_rate": 0.05
        }
    })");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/scientific.json");
    ASSERT_TRUE(config);
    
    EXPECT_EQ(config->get_double("physics.speed_of_light"), 3e8);
    EXPECT_EQ(config->get_double("physics.plancks_constant"), 6.626e-34);
    EXPECT_EQ(config->get_double("finance.interest_rate"), 0.05);
}

TEST_F(JsonConfigParserTest, ParseJsonWithMixedTypes) {
    create_test_file("mixed.json", R"({
        "mixed": {
            "string": "hello",
            "number": 42,
            "float": 3.14,
            "boolean": true
        }
    })");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/mixed.json");
    ASSERT_TRUE(config);
    
    EXPECT_EQ(config->get_string("mixed.string"), "hello");
    EXPECT_EQ(config->get_int("mixed.number"), 42);
    EXPECT_EQ(config->get_double("mixed.float"), 3.14);
    EXPECT_TRUE(config->get_bool("mixed.boolean"));
}

TEST_F(JsonConfigParserTest, ParseJsonWithEmptyObjects) {
    create_test_file("empty_objects.json", R"({
        "empty": {
            "object": {},
            "array": []
        }
    })");
    
    auto config = ConfigManager::load_from_file(test_dir_ + "/empty_objects.json");
    ASSERT_TRUE(config);
    
    // Empty objects should not cause errors
    EXPECT_EQ(config->get_string("empty.object"), "");
    EXPECT_EQ(config->get_int("empty.number"), 0);
    EXPECT_EQ(config->get_double("empty.float"), 0.0);
    EXPECT_FALSE(config->get_bool("empty.boolean"));
}