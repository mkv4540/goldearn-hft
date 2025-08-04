#include <gtest/gtest.h>
#include "../src/market_data/order_book.hpp"

class MarketDataEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        order_book = std::make_unique<goldearn::market_data::OrderBook>("RELIANCE");
    }
    
    std::unique_ptr<goldearn::market_data::OrderBook> order_book;
};

TEST_F(MarketDataEngineTest, OrderBookCreation) {
    EXPECT_NE(order_book, nullptr);
}

TEST_F(MarketDataEngineTest, OrderBookOperations) {
    // Test adding orders with timestamp
    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
    order_book->add_order(1, 'B', 100.0, 100, timestamp);
    order_book->add_order(2, 'S', 101.0, 50, timestamp);
    
    // Test getting best bid/ask
    auto best_bid = order_book->get_best_bid();
    auto best_ask = order_book->get_best_ask();
    
    EXPECT_EQ(best_bid, 100.0);
    EXPECT_EQ(best_ask, 101.0);
}