#include <gtest/gtest.h>
#include "../src/market_data/order_book.hpp"
#include "../src/core/latency_tracker.hpp"

using namespace goldearn::market_data;
using namespace goldearn::core;

class OrderBookTest : public ::testing::Test {
protected:
    void SetUp() override {
        symbol_id_ = 12345;
        tick_size_ = 0.01;
        order_book_ = std::make_unique<OrderBook>(symbol_id_, tick_size_);
    }
    
    void TearDown() override {
        order_book_.reset();
    }
    
    uint64_t symbol_id_;
    double tick_size_;
    std::unique_ptr<OrderBook> order_book_;
};

TEST_F(OrderBookTest, InitialState) {
    EXPECT_EQ(order_book_->get_best_bid(), 0.0);
    EXPECT_EQ(order_book_->get_best_ask(), 0.0);
    EXPECT_EQ(order_book_->get_bid_quantity(), 0UL);
    EXPECT_EQ(order_book_->get_ask_quantity(), 0UL);
    EXPECT_EQ(order_book_->get_total_volume(), 0UL);
    EXPECT_EQ(order_book_->get_trade_count(), 0UL);
}

TEST_F(OrderBookTest, AddBuyOrder) {
    auto timestamp = std::chrono::duration_cast<Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    
    order_book_->add_order(1001, 'B', 100.50, 1000, timestamp);
    
    EXPECT_EQ(order_book_->get_best_bid(), 100.50);
    EXPECT_EQ(order_book_->get_bid_quantity(), 1000UL);
    EXPECT_EQ(order_book_->get_best_ask(), 0.0); // No ask orders yet
}

TEST_F(OrderBookTest, AddSellOrder) {
    auto timestamp = std::chrono::duration_cast<Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    
    order_book_->add_order(1002, 'S', 100.60, 500, timestamp);
    
    EXPECT_EQ(order_book_->get_best_ask(), 100.60);
    EXPECT_EQ(order_book_->get_ask_quantity(), 500UL);
    EXPECT_EQ(order_book_->get_best_bid(), 0.0); // No bid orders yet
}

TEST_F(OrderBookTest, SpreadCalculation) {
    auto timestamp = std::chrono::duration_cast<Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    
    order_book_->add_order(1001, 'B', 100.50, 1000, timestamp);
    order_book_->add_order(1002, 'S', 100.60, 500, timestamp);
    
    EXPECT_DOUBLE_EQ(order_book_->get_spread(), 0.10);
    EXPECT_DOUBLE_EQ(order_book_->get_mid_price(), 100.55);
}

TEST_F(OrderBookTest, OrderCancellation) {
    auto timestamp = std::chrono::duration_cast<Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    
    order_book_->add_order(1001, 'B', 100.50, 1000, timestamp);
    EXPECT_EQ(order_book_->get_best_bid(), 100.50);
    
    order_book_->cancel_order(1001, timestamp);
    EXPECT_EQ(order_book_->get_best_bid(), 0.0);
    EXPECT_EQ(order_book_->get_bid_quantity(), 0UL);
}

TEST_F(OrderBookTest, OrderModification) {
    auto timestamp = std::chrono::duration_cast<Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    
    order_book_->add_order(1001, 'B', 100.50, 1000, timestamp);
    EXPECT_EQ(order_book_->get_bid_quantity(), 1000UL);
    
    order_book_->modify_order(1001, 1500, timestamp);
    EXPECT_EQ(order_book_->get_bid_quantity(), 1500UL);
}

TEST_F(OrderBookTest, TradeUpdate) {
    auto timestamp = std::chrono::duration_cast<Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    
    order_book_->update_trade(100.55, 500, timestamp);
    
    EXPECT_EQ(order_book_->get_total_volume(), 500UL);
    EXPECT_EQ(order_book_->get_trade_count(), 1UL);
}

TEST_F(OrderBookTest, QuoteUpdate) {
    QuoteMessage quote{};
    quote.header.timestamp = std::chrono::duration_cast<Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    quote.symbol_id = symbol_id_;
    quote.bid_price = 100.50;
    quote.bid_quantity = 1000;
    quote.ask_price = 100.60;
    quote.ask_quantity = 800;
    
    // Set up bid levels
    quote.bid_levels[0] = {100.50, 1000, 5};
    quote.bid_levels[1] = {100.49, 800, 3};
    quote.bid_levels[2] = {100.48, 1200, 7};
    
    // Set up ask levels
    quote.ask_levels[0] = {100.60, 800, 4};
    quote.ask_levels[1] = {100.61, 600, 2};
    quote.ask_levels[2] = {100.62, 1000, 6};
    
    order_book_->update_quote(quote);
    
    EXPECT_EQ(order_book_->get_best_bid(), 100.50);
    EXPECT_EQ(order_book_->get_best_ask(), 100.60);
    EXPECT_EQ(order_book_->get_bid_quantity(), 1000UL);
    EXPECT_EQ(order_book_->get_ask_quantity(), 800UL);
}

TEST_F(OrderBookTest, VWAPCalculation) {
    auto timestamp = std::chrono::duration_cast<Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    
    // Add multiple levels
    order_book_->add_order(1001, 'B', 100.50, 1000, timestamp);
    order_book_->add_order(1002, 'B', 100.49, 800, timestamp);
    order_book_->add_order(1003, 'B', 100.48, 1200, timestamp);
    
    order_book_->add_order(2001, 'S', 100.60, 800, timestamp);
    order_book_->add_order(2002, 'S', 100.61, 600, timestamp);
    order_book_->add_order(2003, 'S', 100.62, 1000, timestamp);
    
    double vwap = order_book_->get_vwap(3);
    EXPECT_GT(vwap, 0.0); // Should have a valid VWAP
}

TEST_F(OrderBookTest, ImbalanceCalculation) {
    auto timestamp = std::chrono::duration_cast<Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    
    // Create imbalanced order book (more bids than asks)
    order_book_->add_order(1001, 'B', 100.50, 2000, timestamp);
    order_book_->add_order(2001, 'S', 100.60, 500, timestamp);
    
    double imbalance = order_book_->get_imbalance();
    EXPECT_GT(imbalance, 0.0); // Positive imbalance (more bids)
}

TEST_F(OrderBookTest, PerformanceMeasurement) {
    const size_t num_updates = 10000;
    auto start_time = LatencyTracker::now();
    
    auto timestamp = std::chrono::duration_cast<Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    
    // Simulate high-frequency updates
    for (size_t i = 0; i < num_updates; ++i) {
        double price = 100.0 + (i % 100) * 0.01;
        order_book_->add_order(i + 1000, (i % 2 == 0) ? 'B' : 'S', price, 100, timestamp);
    }
    
    auto end_time = LatencyTracker::now();
    auto duration_ns = LatencyTracker::to_nanoseconds(end_time - start_time);
    double avg_latency_ns = static_cast<double>(duration_ns) / num_updates;
    
    // Should process updates very quickly (target < 1000ns per update)
    EXPECT_LT(avg_latency_ns, 1000.0);
    
    // Verify final state
    EXPECT_GT(order_book_->get_update_latency_ns(), 0.0);
}

TEST_F(OrderBookTest, MarketDepthLevels) {
    auto timestamp = std::chrono::duration_cast<Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    
    // Add multiple price levels
    for (int i = 0; i < 10; ++i) {
        order_book_->add_order(1000 + i, 'B', 100.0 - i * 0.01, 100 * (i + 1), timestamp);
        order_book_->add_order(2000 + i, 'S', 100.1 + i * 0.01, 100 * (i + 1), timestamp);
    }
    
    const auto& bid_levels = order_book_->get_bid_levels();
    const auto& ask_levels = order_book_->get_ask_levels();
    
    // Check that levels are properly sorted (highest bid first, lowest ask first)
    for (size_t i = 1; i < OrderBook::MAX_DEPTH && bid_levels[i].price > 0; ++i) {
        EXPECT_LE(bid_levels[i].price, bid_levels[i-1].price);
    }
    
    for (size_t i = 1; i < OrderBook::MAX_DEPTH && ask_levels[i].price > 0; ++i) {
        EXPECT_GE(ask_levels[i].price, ask_levels[i-1].price);
    }
}

// Stress test for concurrent access
TEST_F(OrderBookTest, ConcurrentAccess) {
    const size_t num_threads = 4;
    const size_t updates_per_thread = 1000;
    std::vector<std::thread> threads;
    
    auto timestamp = std::chrono::duration_cast<Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    
    // Launch multiple threads to update the order book
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, t, updates_per_thread, timestamp]() {
            for (size_t i = 0; i < updates_per_thread; ++i) {
                uint64_t order_id = t * 10000 + i;
                double price = 100.0 + (i % 100) * 0.01;
                char side = (t % 2 == 0) ? 'B' : 'S';
                order_book_->add_order(order_id, side, price, 100, timestamp);
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify order book is still in a valid state
    double best_bid = order_book_->get_best_bid();
    double best_ask = order_book_->get_best_ask();
    
    if (best_bid > 0 && best_ask > 0) {
        EXPECT_LE(best_bid, best_ask); // Bid should not exceed ask
    }
}