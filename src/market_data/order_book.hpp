#pragma once

#include "message_types.hpp"
#include <map>
#include <memory>
#include <atomic>
#include <array>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace goldearn::market_data {

// Price level structure for order book
struct PriceLevel {
    double price;
    uint64_t total_quantity;
    uint32_t order_count;
    Timestamp last_update;
    
    PriceLevel() : price(0.0), total_quantity(0), order_count(0), last_update{} {}
    PriceLevel(double p, uint64_t q, uint32_t c, Timestamp t) 
        : price(p), total_quantity(q), order_count(c), last_update(t) {}
};

// High-performance order book implementation
class OrderBook {
public:
    static constexpr size_t MAX_DEPTH = 20; // Maximum depth levels to track
    
    OrderBook(uint64_t symbol_id, double tick_size);
    ~OrderBook();
    
    // Core order book operations
    void add_order(uint64_t order_id, char side, double price, uint64_t quantity, Timestamp timestamp);
    void modify_order(uint64_t order_id, uint64_t new_quantity, Timestamp timestamp);
    void cancel_order(uint64_t order_id, Timestamp timestamp);
    
    // Market data updates
    void update_trade(double price, uint64_t quantity, Timestamp timestamp);
    void update_quote(const QuoteMessage& quote);
    void full_refresh(const std::vector<PriceLevel>& bids, const std::vector<PriceLevel>& asks);
    
    // Order book queries
    double get_best_bid() const { return best_bid_; }
    double get_best_ask() const { return best_ask_; }
    uint64_t get_bid_quantity() const { return bid_quantity_; }
    uint64_t get_ask_quantity() const { return ask_quantity_; }
    
    // Market depth
    const std::array<PriceLevel, MAX_DEPTH>& get_bid_levels() const { return bid_levels_; }
    const std::array<PriceLevel, MAX_DEPTH>& get_ask_levels() const { return ask_levels_; }
    
    // Market microstructure metrics
    double get_spread() const { return best_ask_ - best_bid_; }
    double get_mid_price() const { return (best_bid_ + best_ask_) / 2.0; }
    double get_vwap(size_t depth = 5) const;
    double get_imbalance() const;
    
    // Statistics
    uint64_t get_total_volume() const { return total_volume_; }
    uint64_t get_trade_count() const { return trade_count_; }
    Timestamp get_last_update() const { return last_update_; }
    
    // Performance metrics
    double get_update_latency_ns() const { return avg_update_latency_ns_; }
    
private:
    uint64_t symbol_id_;
    double tick_size_;
    
    // Best bid/ask (atomic for lock-free reads)
    std::atomic<double> best_bid_;
    std::atomic<double> best_ask_;
    std::atomic<uint64_t> bid_quantity_;
    std::atomic<uint64_t> ask_quantity_;
    
    // Market depth arrays (lock-free for reads)
    alignas(64) std::array<PriceLevel, MAX_DEPTH> bid_levels_;
    alignas(64) std::array<PriceLevel, MAX_DEPTH> ask_levels_;
    
    // Order tracking
    struct OrderInfo {
        double price;
        uint64_t quantity;
        char side;
        Timestamp timestamp;
    };
    std::map<uint64_t, OrderInfo> active_orders_;
    
    // Statistics
    uint64_t total_volume_;
    uint64_t trade_count_;
    Timestamp last_update_;
    double last_trade_price_;
    
    // Performance tracking
    double avg_update_latency_ns_;
    uint64_t update_count_;
    
    // Internal methods
    void update_bid_levels(double price, int64_t quantity_delta, Timestamp timestamp);
    void update_ask_levels(double price, int64_t quantity_delta, Timestamp timestamp);
    void rebuild_depth();
    void update_best_prices();
    
    // Lock-free level updates
    void insert_bid_level(const PriceLevel& level);
    void insert_ask_level(const PriceLevel& level);
    void remove_bid_level(double price);
    void remove_ask_level(double price);
};

// Order book manager for multiple symbols
class OrderBookManager {
public:
    OrderBookManager();
    ~OrderBookManager();
    
    // Order book lifecycle
    bool add_symbol(uint64_t symbol_id, double tick_size);
    void remove_symbol(uint64_t symbol_id);
    OrderBook* get_order_book(uint64_t symbol_id);
    const OrderBook* get_order_book(uint64_t symbol_id) const;
    
    // Batch updates
    void process_market_data_batch(const std::vector<QuoteMessage>& quotes);
    void process_trade_batch(const std::vector<TradeMessage>& trades);
    
    // Statistics
    size_t get_symbol_count() const { return order_books_.size(); }
    uint64_t get_total_updates() const;
    double get_average_latency() const;
    
    // Market overview
    struct MarketSummary {
        uint64_t symbol_id;
        double last_price;
        double bid;
        double ask;
        uint64_t volume;
        double change_percent;
    };
    std::vector<MarketSummary> get_market_summary() const;
    
private:
    std::map<uint64_t, std::unique_ptr<OrderBook>> order_books_;
    mutable std::shared_mutex books_mutex_;
    
    // Performance tracking
    std::atomic<uint64_t> total_updates_;
    std::atomic<double> total_latency_ns_;
};

// Order book analytics and signals
class OrderBookAnalytics {
public:
    struct Signal {
        enum Type { BUY, SELL, NEUTRAL };
        Type signal_type;
        double confidence;
        double price_target;
        Timestamp timestamp;
    };
    
    OrderBookAnalytics(const OrderBook* book);
    
    // Signal generation
    Signal generate_signal() const;
    
    // Market microstructure analysis
    double calculate_price_impact(double quantity, char side) const;
    double estimate_execution_cost(double quantity, char side) const;
    double get_order_flow_imbalance() const;
    
    // Statistical measures
    double get_effective_spread() const;
    double get_quoted_spread() const;
    double get_depth_at_price(double price, char side) const;
    
private:
    const OrderBook* order_book_;
    
    // Analysis helpers
    double calculate_vpin() const; // Volume-synchronized Probability of Informed Trading
    double calculate_kyle_lambda() const; // Kyle's lambda (price impact)
    double get_book_pressure() const;
};

} // namespace goldearn::market_data