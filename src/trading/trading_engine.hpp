#pragma once

#include "../market_data/message_types.hpp"
#include "../market_data/order_book.hpp"
#include "../core/latency_tracker.hpp"
#include <memory>
#include <vector>
#include <atomic>
#include <functional>

namespace goldearn::trading {

// Forward declarations
class Strategy;
class OrderManager;
class PositionManager;
class RiskEngine;

// Order types supported by the trading engine
enum class OrderType : uint8_t {
    MARKET = 1,
    LIMIT = 2,
    STOP = 3,
    STOP_LIMIT = 4,
    IOC = 5,      // Immediate or Cancel
    FOK = 6,      // Fill or Kill
    GTC = 7,      // Good Till Cancel
    GTD = 8       // Good Till Date
};

// Order side
enum class OrderSide : uint8_t {
    BUY = 1,
    SELL = 2
};

// Order status
enum class OrderStatus : uint8_t {
    PENDING = 1,
    SUBMITTED = 2,
    PARTIALLY_FILLED = 3,
    FILLED = 4,
    CANCELLED = 5,
    REJECTED = 6,
    EXPIRED = 7
};

// Order structure for internal use
struct Order {
    uint64_t order_id;
    uint64_t client_order_id;
    uint64_t symbol_id;
    OrderType type;
    OrderSide side;
    double price;
    uint64_t quantity;
    uint64_t filled_quantity;
    OrderStatus status;
    std::string strategy_id;
    market_data::Timestamp created_time;
    market_data::Timestamp submitted_time;
    market_data::Timestamp last_update_time;
    
    // Execution details
    double avg_fill_price;
    uint64_t leaves_quantity() const { return quantity - filled_quantity; }
    bool is_complete() const { return status == OrderStatus::FILLED || status == OrderStatus::CANCELLED; }
};

// Trade execution report
struct ExecutionReport {
    uint64_t order_id;
    uint64_t execution_id;
    uint64_t symbol_id;
    OrderSide side;
    double executed_price;
    uint64_t executed_quantity;
    double commission;
    market_data::Timestamp execution_time;
    std::string execution_venue;
};

// Strategy interface - all trading strategies must implement this
class Strategy {
public:
    virtual ~Strategy() = default;
    
    // Strategy lifecycle
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    
    // Market data callbacks
    virtual void on_trade(const market_data::TradeMessage& trade) = 0;
    virtual void on_quote(const market_data::QuoteMessage& quote) = 0;
    virtual void on_order_book_update(uint64_t symbol_id, const market_data::OrderBook& book) = 0;
    
    // Order lifecycle callbacks
    virtual void on_order_acknowledgment(const Order& order) = 0;
    virtual void on_execution_report(const ExecutionReport& execution) = 0;
    virtual void on_order_rejection(const Order& order, const std::string& reason) = 0;
    
    // Strategy control
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    
    // Strategy information
    virtual std::string get_strategy_id() const = 0;
    virtual std::string get_strategy_name() const = 0;
    virtual double get_target_notional() const = 0;
    
    // Risk parameters
    virtual double get_max_position_size(uint64_t symbol_id) const = 0;
    virtual double get_max_order_value() const = 0;
    
protected:
    // Order submission interface (provided by engine)
    std::function<bool(const Order&)> submit_order_;
    std::function<bool(uint64_t)> cancel_order_;
    std::function<bool(uint64_t, double, uint64_t)> modify_order_;
    
    friend class TradingEngine;
};

// Core trading engine
class TradingEngine {
public:
    TradingEngine();
    ~TradingEngine();
    
    // Engine lifecycle
    bool initialize();
    void shutdown();
    
    // Strategy management
    bool register_strategy(std::unique_ptr<Strategy> strategy);
    bool unregister_strategy(const std::string& strategy_id);
    Strategy* get_strategy(const std::string& strategy_id);
    
    // Market data integration
    void connect_market_data(market_data::OrderBookManager* book_manager);
    void subscribe_symbol(uint64_t symbol_id, const std::string& strategy_id);
    void unsubscribe_symbol(uint64_t symbol_id, const std::string& strategy_id);
    
    // Order management
    bool submit_order(const Order& order);
    bool cancel_order(uint64_t order_id);
    bool modify_order(uint64_t order_id, double new_price, uint64_t new_quantity);
    
    // Engine control
    void start_trading();
    void stop_trading();
    void emergency_stop();
    
    // Statistics and monitoring
    struct EngineStats {
        uint64_t orders_submitted;
        uint64_t orders_filled;
        uint64_t orders_cancelled;
        uint64_t orders_rejected;
        double total_notional_traded;
        double total_commission_paid;
        double avg_order_latency_ns;
        double avg_fill_latency_ns;
    };
    
    EngineStats get_statistics() const;
    bool is_trading_active() const { return trading_active_.load(); }
    
    // Risk integration
    void set_risk_engine(std::shared_ptr<RiskEngine> risk_engine);
    
private:
    // Core components
    std::unique_ptr<OrderManager> order_manager_;
    std::unique_ptr<PositionManager> position_manager_;
    std::shared_ptr<RiskEngine> risk_engine_;
    market_data::OrderBookManager* market_data_;
    
    // Strategy management
    std::map<std::string, std::unique_ptr<Strategy>> strategies_;
    std::map<uint64_t, std::vector<std::string>> symbol_subscriptions_;
    
    // Engine state
    std::atomic<bool> initialized_;
    std::atomic<bool> trading_active_;
    std::atomic<bool> emergency_stop_;
    
    // Performance tracking
    std::unique_ptr<core::LatencyTracker> order_latency_tracker_;
    std::unique_ptr<core::LatencyTracker> fill_latency_tracker_;
    
    // Internal event handlers
    void handle_market_data_update(uint64_t symbol_id, const market_data::OrderBook& book);
    void handle_trade_update(const market_data::TradeMessage& trade);
    void handle_quote_update(const market_data::QuoteMessage& quote);
    
    // Order processing
    bool validate_order(const Order& order);
    void process_execution_report(const ExecutionReport& execution);
    void update_strategy_callbacks();
    
    // Statistics
    mutable std::mutex stats_mutex_;
    EngineStats stats_;
};

// Execution venue interface
class ExecutionVenue {
public:
    virtual ~ExecutionVenue() = default;
    
    // Venue connection
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
    
    // Order operations
    virtual bool submit_order(const Order& order) = 0;
    virtual bool cancel_order(uint64_t order_id) = 0;
    virtual bool modify_order(uint64_t order_id, double price, uint64_t quantity) = 0;
    
    // Venue information
    virtual std::string get_venue_name() const = 0;
    virtual std::vector<uint64_t> get_supported_symbols() const = 0;
    
    // Execution callbacks
    std::function<void(const ExecutionReport&)> execution_callback_;
    std::function<void(const Order&, const std::string&)> rejection_callback_;
};

// Smart order router for multi-venue execution
class SmartOrderRouter {
public:
    SmartOrderRouter();
    ~SmartOrderRouter();
    
    // Venue management
    void add_venue(std::unique_ptr<ExecutionVenue> venue);
    void remove_venue(const std::string& venue_name);
    
    // Routing algorithms
    enum class RoutingAlgorithm {
        BEST_PRICE,      // Route to venue with best price
        LOWEST_LATENCY,  // Route to fastest venue
        SPLIT_ORDER,     // Split across multiple venues
        LIQUIDITY_SEEKING // Route to venue with most liquidity
    };
    
    // Order routing
    bool route_order(const Order& order, RoutingAlgorithm algorithm = RoutingAlgorithm::BEST_PRICE);
    
    // Venue statistics
    struct VenueStats {
        std::string venue_name;
        double avg_fill_rate;
        double avg_latency_ns;
        double avg_slippage_bps;
        uint64_t total_orders;
    };
    
    std::vector<VenueStats> get_venue_statistics() const;
    
private:
    std::vector<std::unique_ptr<ExecutionVenue>> venues_;
    std::map<std::string, VenueStats> venue_stats_;
    
    // Routing logic
    ExecutionVenue* select_best_venue(const Order& order, RoutingAlgorithm algorithm);
    std::vector<Order> split_order(const Order& order, size_t num_venues);
    void update_venue_statistics(const std::string& venue_name, const ExecutionReport& execution);
};

} // namespace goldearn::trading