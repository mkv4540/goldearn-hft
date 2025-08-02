#include <gtest/gtest.h>
#include "../src/market_data/nse_protocol.hpp"

class NSEProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser = std::make_unique<goldearn::market_data::nse::NSEProtocolParser>();
    }
    
    std::unique_ptr<goldearn::market_data::nse::NSEProtocolParser> parser;
};

TEST_F(NSEProtocolTest, BasicFunctionality) {
    EXPECT_NE(parser, nullptr);
    EXPECT_GE(parser->get_messages_processed(), 0);
    EXPECT_GE(parser->get_parse_errors(), 0);
}

TEST_F(NSEProtocolTest, MessageCounting) {
    uint64_t initial_count = parser->get_messages_processed();
    parser->increment_message_count();
    EXPECT_EQ(parser->get_messages_processed(), initial_count + 1);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}