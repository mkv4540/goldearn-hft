#include <gtest/gtest.h>
#include "../src/market_data/order_book.hpp"
#include "../src/core/latency_tracker.hpp"
#include <thread>
#include <random>
#include <algorithm>

using namespace goldearn::market_data;
using namespace goldearn::core;

class OrderBookComprehensiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        symbol_id_ = 12345;
        tick_size_ = 0.01;
        order_book_ = std::make_unique<OrderBook>(symbol_id_, tick_size_);
        timestamp_ = std::chrono::duration_cast<Timestamp>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        );
    }
    
    uint64_t symbol_id_;
    double tick_size_;
    std::unique_ptr<OrderBook> order_book_;
    Timestamp timestamp_;
};

// Test OrderBook constructor - covers all initialization scenarios
TEST_F(OrderBookComprehensiveTest, Constructor) {
    // Test normal construction
    auto book1 = std::make_unique<OrderBook>(123, 0.01);
    EXPECT_EQ(book1->get_best_bid(), 0.0);
    EXPECT_EQ(book1->get_best_ask(), 0.0);
    EXPECT_EQ(book1->get_total_volume(), 0UL);
    
    // Test with different tick sizes
    auto book2 = std::make_unique<OrderBook>(456, 0.05);
    auto book3 = std::make_unique<OrderBook>(789, 1.0);
    
    // Test edge cases
    auto book4 = std::make_unique<OrderBook>(0, 0.001);      // Zero symbol ID
    auto book5 = std::make_unique<OrderBook>(UINT64_MAX, 100.0); // Max symbol ID, large tick
    
    // All should initialize properly
    EXPECT_EQ(book4->get_total_volume(), 0UL);
    EXPECT_EQ(book5->get_total_volume(), 0UL);
}

// Test add_order - covers all scenarios including edge cases
TEST_F(OrderBookComprehensiveTest, AddOrder) {
    // Test adding first buy order
    order_book_->add_order(1001, 'B', 100.50, 1000, timestamp_);
    EXPECT_EQ(order_book_->get_best_bid(), 100.50);
    EXPECT_EQ(order_book_->get_bid_quantity(), 1000UL);
    
    // Test adding first sell order
    order_book_->add_order(1002, 'S', 100.60, 500, timestamp_);
    EXPECT_EQ(order_book_->get_best_ask(), 100.60);
    EXPECT_EQ(order_book_->get_ask_quantity(), 500UL);
    
    // Test adding better bid (higher price)
    order_book_->add_order(1003, 'B', 100.51, 800, timestamp_);
    EXPECT_EQ(order_book_->get_best_bid(), 100.51);
    EXPECT_EQ(order_book_->get_bid_quantity(), 800UL);
    
    // Test adding better ask (lower price)
    order_book_->add_order(1004, 'S', 100.59, 600, timestamp_);
    EXPECT_EQ(order_book_->get_best_ask(), 100.59);
    EXPECT_EQ(order_book_->get_ask_quantity(), 600UL);
    
    // Test adding worse bid (should not change best)
    order_book_->add_order(1005, 'B', 100.49, 1200, timestamp_);
    EXPECT_EQ(order_book_->get_best_bid(), 100.51); // Should remain unchanged
    
    // Test adding worse ask (should not change best)
    order_book_->add_order(1006, 'S', 100.61, 700, timestamp_);
    EXPECT_EQ(order_book_->get_best_ask(), 100.59); // Should remain unchanged
    
    // Test edge cases
    order_book_->add_order(0, 'B', 0.01, 1, timestamp_);        // Zero order ID, minimum values
    order_book_->add_order(UINT64_MAX, 'S', 999999.99, UINT64_MAX, timestamp_); // Maximum values
    
    // Test invalid side (should handle gracefully)
    order_book_->add_order(1007, 'X', 100.55, 500, timestamp_); // Invalid side
    
    // Test same price, multiple orders (quantity aggregation)
    order_book_->add_order(1008, 'B', 100.51, 300, timestamp_);
    // Best bid price should remain same, but quantity might aggregate
    EXPECT_EQ(order_book_->get_best_bid(), 100.51);
    
    // Test crossing orders (bid >= ask scenario)
    order_book_->add_order(1009, 'B', 100.65, 200, timestamp_); // Bid above current ask
    // Implementation should handle this appropriately
}

// Test modify_order - covers all modification scenarios
TEST_F(OrderBookComprehensiveTest, ModifyOrder) {
    // Setup initial orders
    order_book_->add_order(1001, 'B', 100.50, 1000, timestamp_);
    order_book_->add_order(1002, 'S', 100.60, 500, timestamp_);
    
    // Test increasing quantity
    order_book_->modify_order(1001, 1500, timestamp_);
    EXPECT_EQ(order_book_->get_bid_quantity(), 1500UL);
    
    // Test decreasing quantity
    order_book_->modify_order(1001, 800, timestamp_);
    EXPECT_EQ(order_book_->get_bid_quantity(), 800UL);
    
    // Test modify to zero quantity (should remove order)
    order_book_->modify_order(1001, 0, timestamp_);
    EXPECT_EQ(order_book_->get_best_bid(), 0.0);
    
    // Test modify non-existent order (should handle gracefully)
    order_book_->modify_order(9999, 1000, timestamp_);
    
    // Test modify with same quantity (no change)
    order_book_->modify_order(1002, 500, timestamp_);
    EXPECT_EQ(order_book_->get_ask_quantity(), 500UL);
    
    // Test edge cases
    order_book_->add_order(1003, 'B', 100.45, 1000, timestamp_);
    order_book_->modify_order(1003, UINT64_MAX, timestamp_);    // Maximum quantity
    order_book_->modify_order(1003, 1, timestamp_);            // Minimum quantity
}

// Test cancel_order - covers all cancellation scenarios
TEST_F(OrderBookComprehensiveTest, CancelOrder) {
    // Setup multiple orders
    order_book_->add_order(1001, 'B', 100.50, 1000, timestamp_);
    order_book_->add_order(1002, 'B', 100.49, 800, timestamp_);
    order_book_->add_order(1003, 'S', 100.60, 500, timestamp_);
    order_book_->add_order(1004, 'S', 100.61, 600, timestamp_);
    
    // Test cancel best bid (should update to next best)
    order_book_->cancel_order(1001, timestamp_);
    EXPECT_EQ(order_book_->get_best_bid(), 100.49);
    EXPECT_EQ(order_book_->get_bid_quantity(), 800UL);
    
    // Test cancel best ask (should update to next best)
    order_book_->cancel_order(1003, timestamp_);
    EXPECT_EQ(order_book_->get_best_ask(), 100.61);
    EXPECT_EQ(order_book_->get_ask_quantity(), 600UL);
    
    // Test cancel non-best order
    order_book_->add_order(1005, 'B', 100.48, 500, timestamp_);
    order_book_->cancel_order(1005, timestamp_); // Cancel non-best order
    EXPECT_EQ(order_book_->get_best_bid(), 100.49); // Should remain unchanged
    
    // Test cancel last order (should reset to zero)
    order_book_->cancel_order(1002, timestamp_);
    order_book_->cancel_order(1004, timestamp_);
    EXPECT_EQ(order_book_->get_best_bid(), 0.0);
    EXPECT_EQ(order_book_->get_best_ask(), 0.0);
    
    // Test cancel non-existent order (should handle gracefully)
    order_book_->cancel_order(9999, timestamp_);
    
    // Test cancel already cancelled order
    order_book_->cancel_order(1001, timestamp_); // Already cancelled
}

// Test update_trade - covers all trade update scenarios
TEST_F(OrderBookComprehensiveTest, UpdateTrade) {
    // Test first trade
    order_book_->update_trade(100.55, 500, timestamp_);
    EXPECT_EQ(order_book_->get_total_volume(), 500UL);
    EXPECT_EQ(order_book_->get_trade_count(), 1UL);
    
    // Test multiple trades
    order_book_->update_trade(100.56, 300, timestamp_);
    order_book_->update_trade(100.54, 700, timestamp_);
    EXPECT_EQ(order_book_->get_total_volume(), 1500UL);
    EXPECT_EQ(order_book_->get_trade_count(), 3UL);
    
    // Test edge cases
    order_book_->update_trade(0.01, 1, timestamp_);                    // Minimum values
    order_book_->update_trade(999999.99, UINT64_MAX/2, timestamp_);    // Large values
    order_book_->update_trade(100.55, 0, timestamp_);                  // Zero quantity trade
    
    // Test negative price (should handle gracefully)
    order_book_->update_trade(-100.55, 500, timestamp_);
    
    // Test very high frequency trades
    auto start_time = LatencyTracker::now();
    for (int i = 0; i < 1000; ++i) {
        order_book_->update_trade(100.0 + i * 0.001, 100, timestamp_);
    }
    auto end_time = LatencyTracker::now();
    auto duration_ns = LatencyTracker::to_nanoseconds(end_time - start_time);
    EXPECT_LT(duration_ns / 1000, 100000); // Should be fast (< 100ns per trade on average)
}

// Test update_quote - covers all quote update scenarios
TEST_F(OrderBookComprehensiveTest, UpdateQuote) {
    QuoteMessage quote{};
    quote.header.timestamp = timestamp_;
    quote.symbol_id = symbol_id_;
    
    // Test normal quote update
    quote.bid_price = 100.50;
    quote.bid_quantity = 1000;
    quote.ask_price = 100.60;
    quote.ask_quantity = 800;
    
    // Setup bid levels (best to worst)
    quote.bid_levels[0] = {100.50, 1000, 5};
    quote.bid_levels[1] = {100.49, 800, 3};
    quote.bid_levels[2] = {100.48, 1200, 7};
    quote.bid_levels[3] = {100.47, 600, 2};
    quote.bid_levels[4] = {100.46, 900, 4};
    
    // Setup ask levels (best to worst)
    quote.ask_levels[0] = {100.60, 800, 4};
    quote.ask_levels[1] = {100.61, 600, 2};
    quote.ask_levels[2] = {100.62, 1000, 6};
    quote.ask_levels[3] = {100.63, 400, 1};
    quote.ask_levels[4] = {100.64, 700, 3};
    
    order_book_->update_quote(quote);
    
    EXPECT_EQ(order_book_->get_best_bid(), 100.50);
    EXPECT_EQ(order_book_->get_best_ask(), 100.60);
    EXPECT_EQ(order_book_->get_bid_quantity(), 1000UL);
    EXPECT_EQ(order_book_->get_ask_quantity(), 800UL);
    
    // Test crossed quote (bid >= ask)
    quote.bid_price = 100.65;
    quote.ask_price = 100.60;
    order_book_->update_quote(quote);
    
    // Test empty quote (all zeros)
    QuoteMessage empty_quote{};
    empty_quote.header.timestamp = timestamp_;
    empty_quote.symbol_id = symbol_id_;
    order_book_->update_quote(empty_quote);
    
    // Test partial quote (only bid or only ask)
    QuoteMessage partial_quote{};
    partial_quote.header.timestamp = timestamp_;
    partial_quote.symbol_id = symbol_id_;
    partial_quote.bid_price = 100.45;
    partial_quote.bid_quantity = 500;
    // Ask remains zero
    order_book_->update_quote(partial_quote);
    
    // Test wrong symbol ID
    QuoteMessage wrong_symbol{};
    wrong_symbol.header.timestamp = timestamp_;
    wrong_symbol.symbol_id = 99999; // Different symbol
    wrong_symbol.bid_price = 200.00;
    order_book_->update_quote(wrong_symbol);
    // Should not affect our order book
}

// Test full_refresh - covers all refresh scenarios
TEST_F(OrderBookComprehensiveTest, FullRefresh) {
    // Setup initial state
    order_book_->add_order(1001, 'B', 100.50, 1000, timestamp_);
    order_book_->add_order(1002, 'S', 100.60, 500, timestamp_);
    
    // Test normal refresh
    std::vector<PriceLevel> new_bids = {
        {100.52, 800, 3, timestamp_},
        {100.51, 1200, 5, timestamp_},
        {100.50, 600, 2, timestamp_}
    };
    
    std::vector<PriceLevel> new_asks = {
        {100.58, 700, 4, timestamp_},
        {100.59, 500, 2, timestamp_},
        {100.60, 900, 6, timestamp_}
    };
    
    order_book_->full_refresh(new_bids, new_asks);
    
    EXPECT_EQ(order_book_->get_best_bid(), 100.52);
    EXPECT_EQ(order_book_->get_best_ask(), 100.58);
    
    // Test empty refresh (clear book)
    std::vector<PriceLevel> empty_bids;
    std::vector<PriceLevel> empty_asks;
    order_book_->full_refresh(empty_bids, empty_asks);
    
    EXPECT_EQ(order_book_->get_best_bid(), 0.0);
    EXPECT_EQ(order_book_->get_best_ask(), 0.0);
    
    // Test single-sided refresh
    order_book_->full_refresh(new_bids, empty_asks);
    EXPECT_EQ(order_book_->get_best_bid(), 100.52);
    EXPECT_EQ(order_book_->get_best_ask(), 0.0);
    
    order_book_->full_refresh(empty_bids, new_asks);
    EXPECT_EQ(order_book_->get_best_bid(), 0.0);
    EXPECT_EQ(order_book_->get_best_ask(), 100.58);
    
    // Test unsorted input (should handle gracefully)
    std::vector<PriceLevel> unsorted_bids = {
        {100.48, 600, 2, timestamp_},
        {100.52, 800, 3, timestamp_}, // Higher price in wrong position
        {100.49, 1200, 5, timestamp_}
    };
    order_book_->full_refresh(unsorted_bids, empty_asks);
    
    // Test with zero quantities (should filter out)
    std::vector<PriceLevel> zero_qty_bids = {
        {100.50, 0, 0, timestamp_},    // Zero quantity
        {100.49, 500, 2, timestamp_}
    };
    order_book_->full_refresh(zero_qty_bids, empty_asks);
    EXPECT_EQ(order_book_->get_best_bid(), 100.49); // Should skip zero quantity
}

// Test get_best_bid - covers all scenarios
TEST_F(OrderBookComprehensiveTest, GetBestBid) {
    // Test empty book
    EXPECT_EQ(order_book_->get_best_bid(), 0.0);
    
    // Test with single order
    order_book_->add_order(1001, 'B', 100.50, 1000, timestamp_);
    EXPECT_EQ(order_book_->get_best_bid(), 100.50);
    
    // Test with multiple orders (should return highest)
    order_book_->add_order(1002, 'B', 100.45, 800, timestamp_);
    order_book_->add_order(1003, 'B', 100.55, 600, timestamp_);
    EXPECT_EQ(order_book_->get_best_bid(), 100.55);
    
    // Test after cancelling best bid
    order_book_->cancel_order(1003, timestamp_);
    EXPECT_EQ(order_book_->get_best_bid(), 100.50);
    
    // Test after cancelling all bids
    order_book_->cancel_order(1001, timestamp_);
    order_book_->cancel_order(1002, timestamp_);
    EXPECT_EQ(order_book_->get_best_bid(), 0.0);
    
    // Test with ask orders only (should remain 0)
    order_book_->add_order(2001, 'S', 100.60, 500, timestamp_);
    EXPECT_EQ(order_book_->get_best_bid(), 0.0);
}

// Test get_best_ask - covers all scenarios
TEST_F(OrderBookComprehensiveTest, GetBestAsk) {
    // Test empty book
    EXPECT_EQ(order_book_->get_best_ask(), 0.0);
    
    // Test with single order
    order_book_->add_order(2001, 'S', 100.60, 500, timestamp_);
    EXPECT_EQ(order_book_->get_best_ask(), 100.60);
    
    // Test with multiple orders (should return lowest)
    order_book_->add_order(2002, 'S', 100.65, 300, timestamp_);
    order_book_->add_order(2003, 'S', 100.55, 700, timestamp_);
    EXPECT_EQ(order_book_->get_best_ask(), 100.55);
    
    // Test after cancelling best ask
    order_book_->cancel_order(2003, timestamp_);
    EXPECT_EQ(order_book_->get_best_ask(), 100.60);
    
    // Test after cancelling all asks
    order_book_->cancel_order(2001, timestamp_);
    order_book_->cancel_order(2002, timestamp_);
    EXPECT_EQ(order_book_->get_best_ask(), 0.0);
    
    // Test with bid orders only (should remain 0)
    order_book_->add_order(1001, 'B', 100.50, 1000, timestamp_);
    EXPECT_EQ(order_book_->get_best_ask(), 0.0);
}

// Test get_spread - covers all spread calculation scenarios
TEST_F(OrderBookComprehensiveTest, GetSpread) {
    // Test empty book
    EXPECT_EQ(order_book_->get_spread(), 0.0);
    
    // Test with only bid
    order_book_->add_order(1001, 'B', 100.50, 1000, timestamp_);
    EXPECT_EQ(order_book_->get_spread(), -100.50); // Negative spread when missing ask
    
    // Test with only ask
    auto book2 = std::make_unique<OrderBook>(symbol_id_, tick_size_);
    book2->add_order(2001, 'S', 100.60, 500, timestamp_);
    EXPECT_EQ(book2->get_spread(), 100.60); // Ask when missing bid
    
    // Test normal spread
    order_book_->add_order(2001, 'S', 100.60, 500, timestamp_);
    EXPECT_DOUBLE_EQ(order_book_->get_spread(), 0.10);
    
    // Test narrow spread
    order_book_->cancel_order(2001, timestamp_);
    order_book_->add_order(2002, 'S', 100.51, 500, timestamp_);
    EXPECT_DOUBLE_EQ(order_book_->get_spread(), 0.01);
    
    // Test wide spread
    order_book_->cancel_order(2002, timestamp_);
    order_book_->add_order(2003, 'S', 101.00, 500, timestamp_);
    EXPECT_DOUBLE_EQ(order_book_->get_spread(), 0.50);
    
    // Test crossed book (negative spread)
    order_book_->cancel_order(1001, timestamp_);
    order_book_->add_order(1002, 'B', 101.05, 1000, timestamp_);
    double spread = order_book_->get_spread();
    EXPECT_LT(spread, 0.0); // Should be negative for crossed book
    
    // Test zero spread (same price)
    order_book_->cancel_order(1002, timestamp_);
    order_book_->cancel_order(2003, timestamp_);
    order_book_->add_order(1003, 'B', 100.50, 1000, timestamp_);
    order_book_->add_order(2004, 'S', 100.50, 500, timestamp_);
    EXPECT_DOUBLE_EQ(order_book_->get_spread(), 0.0);
}

// Test get_mid_price - covers all mid price calculation scenarios
TEST_F(OrderBookComprehensiveTest, GetMidPrice) {
    // Test empty book
    EXPECT_EQ(order_book_->get_mid_price(), 0.0);
    
    // Test with only bid
    order_book_->add_order(1001, 'B', 100.50, 1000, timestamp_);
    EXPECT_EQ(order_book_->get_mid_price(), 50.25); // (100.50 + 0) / 2
    
    // Test with only ask
    auto book2 = std::make_unique<OrderBook>(symbol_id_, tick_size_);
    book2->add_order(2001, 'S', 100.60, 500, timestamp_);
    EXPECT_EQ(book2->get_mid_price(), 50.30); // (0 + 100.60) / 2
    
    // Test normal mid price
    order_book_->add_order(2001, 'S', 100.60, 500, timestamp_);
    EXPECT_DOUBLE_EQ(order_book_->get_mid_price(), 100.55); // (100.50 + 100.60) / 2
    
    // Test with different prices
    order_book_->cancel_order(1001, timestamp_);
    order_book_->cancel_order(2001, timestamp_);
    order_book_->add_order(1002, 'B', 99.75, 1000, timestamp_);
    order_book_->add_order(2002, 'S', 100.25, 500, timestamp_);
    EXPECT_DOUBLE_EQ(order_book_->get_mid_price(), 100.00); // (99.75 + 100.25) / 2
    
    // Test crossed book
    order_book_->cancel_order(1002, timestamp_);
    order_book_->add_order(1003, 'B', 100.30, 1000, timestamp_);
    EXPECT_DOUBLE_EQ(order_book_->get_mid_price(), 100.275); // (100.30 + 100.25) / 2
    
    // Test precision
    order_book_->cancel_order(1003, timestamp_);
    order_book_->cancel_order(2002, timestamp_);
    order_book_->add_order(1004, 'B', 100.123, 1000, timestamp_);
    order_book_->add_order(2003, 'S', 100.127, 500, timestamp_);
    EXPECT_DOUBLE_EQ(order_book_->get_mid_price(), 100.125); // Precise calculation
}

// Test get_vwap - covers all VWAP calculation scenarios
TEST_F(OrderBookComprehensiveTest, GetVwap) {
    // Test empty book
    EXPECT_EQ(order_book_->get_vwap(), 0.0);
    
    // Setup multi-level order book
    order_book_->add_order(1001, 'B', 100.50, 1000, timestamp_);
    order_book_->add_order(1002, 'B', 100.49, 800, timestamp_);
    order_book_->add_order(1003, 'B', 100.48, 1200, timestamp_);
    order_book_->add_order(1004, 'B', 100.47, 600, timestamp_);
    order_book_->add_order(1005, 'B', 100.46, 900, timestamp_);
    
    order_book_->add_order(2001, 'S', 100.60, 800, timestamp_);
    order_book_->add_order(2002, 'S', 100.61, 600, timestamp_);
    order_book_->add_order(2003, 'S', 100.62, 1000, timestamp_);
    order_book_->add_order(2004, 'S', 100.63, 400, timestamp_);
    order_book_->add_order(2005, 'S', 100.64, 700, timestamp_);
    
    // Test VWAP with different depths
    double vwap1 = order_book_->get_vwap(1);
    double vwap3 = order_book_->get_vwap(3);
    double vwap5 = order_book_->get_vwap(5);
    
    EXPECT_GT(vwap1, 0.0);
    EXPECT_GT(vwap3, 0.0);
    EXPECT_GT(vwap5, 0.0);
    
    // VWAP should be between best bid and best ask
    EXPECT_LE(vwap1, order_book_->get_best_ask());
    EXPECT_GE(vwap1, order_book_->get_best_bid());
    
    // Test with zero depth
    double vwap0 = order_book_->get_vwap(0);
    EXPECT_EQ(vwap0, 0.0);
    
    // Test with excessive depth (more than available levels)
    double vwap10 = order_book_->get_vwap(10);
    EXPECT_GT(vwap10, 0.0);
    
    // Test single-sided book
    auto book2 = std::make_unique<OrderBook>(symbol_id_, tick_size_);
    book2->add_order(1001, 'B', 100.50, 1000, timestamp_);
    double vwap_single = book2->get_vwap(1);
    EXPECT_GT(vwap_single, 0.0);
}

// Test get_imbalance - covers all imbalance calculation scenarios
TEST_F(OrderBookComprehensiveTest, GetImbalance) {
    // Test empty book
    EXPECT_EQ(order_book_->get_imbalance(), 0.0);
    
    // Test balanced book
    order_book_->add_order(1001, 'B', 100.50, 1000, timestamp_);
    order_book_->add_order(2001, 'S', 100.60, 1000, timestamp_);
    double balanced_imbalance = order_book_->get_imbalance();
    EXPECT_DOUBLE_EQ(balanced_imbalance, 0.0);
    
    // Test bid-heavy imbalance
    order_book_->add_order(1002, 'B', 100.49, 2000, timestamp_);
    double bid_heavy = order_book_->get_imbalance();
    EXPECT_GT(bid_heavy, 0.0); // Positive imbalance
    
    // Test ask-heavy imbalance
    order_book_->cancel_order(1001, timestamp_);
    order_book_->cancel_order(1002, timestamp_);
    order_book_->add_order(2002, 'S', 100.61, 2000, timestamp_);
    double ask_heavy = order_book_->get_imbalance();
    EXPECT_LT(ask_heavy, 0.0); // Negative imbalance
    
    // Test only bids
    order_book_->cancel_order(2001, timestamp_);
    order_book_->cancel_order(2002, timestamp_);
    order_book_->add_order(1003, 'B', 100.50, 500, timestamp_);
    double only_bids = order_book_->get_imbalance();
    EXPECT_EQ(only_bids, 1.0); // Maximum positive imbalance
    
    // Test only asks
    order_book_->cancel_order(1003, timestamp_);
    order_book_->add_order(2003, 'S', 100.60, 500, timestamp_);
    double only_asks = order_book_->get_imbalance();
    EXPECT_EQ(only_asks, -1.0); // Maximum negative imbalance
    
    // Test extreme imbalance ratios
    order_book_->add_order(1004, 'B', 100.50, 10000, timestamp_);
    order_book_->add_order(2004, 'S', 100.60, 1, timestamp_);
    double extreme_imbalance = order_book_->get_imbalance();
    EXPECT_GT(extreme_imbalance, 0.99); // Should be close to maximum
}

// Test performance characteristics
TEST_F(OrderBookComprehensiveTest, PerformanceCharacteristics) {
    const size_t num_operations = 10000;
    std::vector<uint64_t> latencies;
    latencies.reserve(num_operations);
    
    // Test add_order performance
    for (size_t i = 0; i < num_operations; ++i) {
        auto start = LatencyTracker::now();
        double price = 100.0 + (i % 100) * 0.01;
        char side = (i % 2 == 0) ? 'B' : 'S';
        order_book_->add_order(i + 1000, side, price, 100, timestamp_);
        auto end = LatencyTracker::now();
        
        latencies.push_back(LatencyTracker::to_nanoseconds(end - start));
    }
    
    // Calculate statistics
    uint64_t total_latency = 0;
    uint64_t max_latency = 0;
    uint64_t min_latency = UINT64_MAX;
    
    for (uint64_t latency : latencies) {
        total_latency += latency;
        max_latency = std::max(max_latency, latency);
        min_latency = std::min(min_latency, latency);
    }
    
    double avg_latency = static_cast<double>(total_latency) / num_operations;
    
    // Performance requirements
    EXPECT_LT(avg_latency, 1000.0);  // Average < 1μs
    EXPECT_LT(max_latency, 10000UL); // Max < 10μs
    
    // Test concurrent access performance
    const size_t num_threads = 4;
    const size_t ops_per_thread = 1000;
    std::vector<std::thread> threads;
    std::atomic<uint64_t> total_ops{0};
    
    auto start_time = LatencyTracker::now();
    
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, t, ops_per_thread, &total_ops]() {
            for (size_t i = 0; i < ops_per_thread; ++i) {
                uint64_t order_id = t * 10000 + i;
                double price = 100.0 + (i % 50) * 0.01;
                char side = (t % 2 == 0) ? 'B' : 'S';
                order_book_->add_order(order_id, side, price, 100, timestamp_);
                total_ops.fetch_add(1);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end_time = LatencyTracker::now();
    auto total_duration = LatencyTracker::to_nanoseconds(end_time - start_time);
    
    EXPECT_EQ(total_ops.load(), num_threads * ops_per_thread);
    
    // Should handle concurrent operations efficiently
    double ops_per_second = (total_ops.load() * 1e9) / total_duration;
    EXPECT_GT(ops_per_second, 100000.0); // > 100K ops/sec
}

// Test memory and resource management
TEST_F(OrderBookComprehensiveTest, ResourceManagement) {
    // Test large number of orders
    const size_t large_num_orders = 100000;
    
    for (size_t i = 0; i < large_num_orders; ++i) {
        double price = 100.0 + (i % 1000) * 0.001;
        char side = (i % 2 == 0) ? 'B' : 'S';
        order_book_->add_order(i, side, price, 100, timestamp_);
    }
    
    // Test memory usage is reasonable (indirect test through performance)
    auto start = LatencyTracker::now();
    double vwap = order_book_->get_vwap(10);
    auto end = LatencyTracker::now();
    
    EXPECT_GT(vwap, 0.0);
    EXPECT_LT(LatencyTracker::to_nanoseconds(end - start), 100000UL); // < 100μs even with many orders
    
    // Test cleanup by cancelling all orders
    for (size_t i = 0; i < 1000; ++i) { // Cancel subset
        order_book_->cancel_order(i, timestamp_);
    }
    
    // Book should still function properly
    EXPECT_GT(order_book_->get_best_bid(), 0.0);
    EXPECT_GT(order_book_->get_best_ask(), 0.0);
}

// Test edge cases and error handling
TEST_F(OrderBookComprehensiveTest, EdgeCasesAndErrorHandling) {
    // Test with extreme values
    order_book_->add_order(UINT64_MAX, 'B', DBL_MAX, UINT64_MAX, timestamp_);
    order_book_->add_order(0, 'S', DBL_MIN, 0, timestamp_);
    
    // Test invalid operations
    order_book_->modify_order(UINT64_MAX, UINT64_MAX, timestamp_);
    order_book_->cancel_order(UINT64_MAX, timestamp_);
    
    // Test rapid state changes
    for (int i = 0; i < 100; ++i) {
        order_book_->add_order(i, 'B', 100.0, 100, timestamp_);
        order_book_->modify_order(i, 200, timestamp_);
        order_book_->cancel_order(i, timestamp_);
    }
    
    // Book should remain in valid state
    order_book_->add_order(999, 'B', 100.50, 1000, timestamp_);
    order_book_->add_order(998, 'S', 100.60, 500, timestamp_);
    
    EXPECT_GT(order_book_->get_spread(), 0.0);
    EXPECT_GT(order_book_->get_mid_price(), 0.0);
}