#pragma once

#include "trading_engine.hpp"
#include "../market_data/order_book.hpp"
#include "../core/latency_tracker.hpp"
#include <string>
#include <map>
#include <mutex>
#include <memory>

namespace goldearn::trading {

// Base class for all trading strategies with common functionality
class StrategyBase : public Strategy {
public:
    StrategyBase(const std::string& strategy_id, const std::string& strategy_name);
    virtual ~StrategyBase();
    
    // Strategy identification
    std::string get_strategy_id() const override { return strategy_id_; }
    std::string get_strategy_name() const override { return strategy_name_; }
    
    // Base implementation of Strategy interface
    bool initialize() override;
    void shutdown() override;
    void start() override;
    void stop() override;
    void pause() override;
    void resume() override;
    
    // Configuration
    void set_config_parameter(const std::string& key, double value);
    void set_config_parameter(const std::string& key, const std::string& value);
    double get_config_double(const std::string& key, double default_value = 0.0) const;
    std::string get_config_string(const std::string& key, const std::string& default_value = "") const;
    
    // Position and risk management
    double get_position(uint64_t symbol_id) const;
    double get_unrealized_pnl(uint64_t symbol_id) const;
    double get_realized_pnl() const;
    double get_total_pnl() const;
    
    // Order management helpers
    uint64_t generate_client_order_id();
    bool submit_market_order(uint64_t symbol_id, OrderSide side, uint64_t quantity);
    bool submit_limit_order(uint64_t symbol_id, OrderSide side, double price, uint64_t quantity);
    bool submit_ioc_order(uint64_t symbol_id, OrderSide side, double price, uint64_t quantity);
    
    // Market data access
    const market_data::OrderBook* get_order_book(uint64_t symbol_id) const;
    double get_last_price(uint64_t symbol_id) const;
    double get_mid_price(uint64_t symbol_id) const;
    double get_spread(uint64_t symbol_id) const;
    
    // Strategy state
    enum class StrategyState {
        INITIALIZED,
        STARTING,
        RUNNING,
        PAUSED,
        STOPPING,
        STOPPED,
        ERROR
    };
    
    StrategyState get_state() const { return state_; }
    bool is_active() const { return state_ == StrategyState::RUNNING; }
    
    // Performance tracking
    struct StrategyPerformance {
        double total_pnl;
        double sharpe_ratio;
        double max_drawdown;
        double win_rate;
        uint64_t total_trades;
        uint64_t winning_trades;
        double avg_trade_duration_ms;
        double avg_order_latency_ns;
    };
    
    StrategyPerformance get_performance() const;
    
protected:
    // Protected members accessible to derived strategies
    std::string strategy_id_;
    std::string strategy_name_;
    std::atomic<StrategyState> state_;
    
    // Configuration storage
    std::map<std::string, double> config_doubles_;
    std::map<std::string, std::string> config_strings_;
    mutable std::shared_mutex config_mutex_;
    
    // Position tracking
    std::map<uint64_t, double> positions_;
    std::map<uint64_t, double> avg_entry_prices_;
    double realized_pnl_;
    mutable std::shared_mutex position_mutex_;
    
    // Order tracking
    std::map<uint64_t, Order> pending_orders_;
    std::atomic<uint64_t> next_client_order_id_;
    mutable std::shared_mutex orders_mutex_;
    
    // Market data cache
    std::map<uint64_t, const market_data::OrderBook*> order_books_;
    std::map<uint64_t, double> last_prices_;
    mutable std::shared_mutex market_data_mutex_;
    
    // Performance tracking
    mutable std::mutex performance_mutex_;
    std::vector<double> trade_pnls_;
    std::vector<std::chrono::high_resolution_clock::time_point> trade_timestamps_;
    std::unique_ptr<core::LatencyTracker> order_latency_tracker_;
    
    // Virtual methods for derived strategies to implement
    virtual bool on_initialize() { return true; }
    virtual void on_shutdown() {}
    virtual void on_start() {}
    virtual void on_stop() {}
    virtual void on_pause() {}
    virtual void on_resume() {}
    
    // Helper methods for derived strategies
    void log_info(const std::string& message) const;
    void log_warning(const std::string& message) const;
    void log_error(const std::string& message) const;
    
    // Risk checks
    virtual bool check_risk_limits(const Order& order) const;
    bool check_position_limits(uint64_t symbol_id, OrderSide side, uint64_t quantity) const;
    bool check_order_value_limits(double order_value) const;
    
    // Strategy-specific order validation
    virtual bool validate_order(const Order& order) const { return true; }
    
    // Internal order callbacks (final implementations)
    void on_order_acknowledgment(const Order& order) final;
    void on_execution_report(const ExecutionReport& execution) final;
    void on_order_rejection(const Order& order, const std::string& reason) final;
    
    // Virtual callbacks for derived strategies
    virtual void on_order_acknowledged(const Order& order) {}
    virtual void on_order_executed(const ExecutionReport& execution) {}
    virtual void on_order_rejected(const Order& order, const std::string& reason) {}
    
private:
    // Internal state management
    void update_position(const ExecutionReport& execution);
    void update_performance_metrics(const ExecutionReport& execution);
    void calculate_trade_pnl(const ExecutionReport& execution);
};

// Market making strategy base class
class MarketMakingStrategy : public StrategyBase {
public:
    MarketMakingStrategy(const std::string& strategy_id, uint64_t symbol_id);
    virtual ~MarketMakingStrategy();
    
    // Market making specific configuration
    void set_spread_bps(double spread_bps) { spread_bps_ = spread_bps; }
    void set_max_position(double max_position) { max_position_ = max_position; }
    void set_order_size(uint64_t order_size) { order_size_ = order_size; }
    void set_quote_refresh_interval_ms(uint64_t interval_ms) { quote_refresh_interval_ms_ = interval_ms; }
    
    // Strategy interface implementation
    void on_quote(const market_data::QuoteMessage& quote) override;
    void on_order_book_update(uint64_t symbol_id, const market_data::OrderBook& book) override;
    
    double get_target_notional() const override;
    double get_max_position_size(uint64_t symbol_id) const override;
    double get_max_order_value() const override;
    
protected:
    uint64_t symbol_id_;
    double spread_bps_;
    double max_position_;
    uint64_t order_size_;
    uint64_t quote_refresh_interval_ms_;
    
    // Market making state
    uint64_t current_bid_order_id_;
    uint64_t current_ask_order_id_;
    double current_bid_price_;
    double current_ask_price_;
    std::chrono::high_resolution_clock::time_point last_quote_time_;
    
    // Virtual methods for derived market making strategies
    virtual double calculate_fair_value(const market_data::OrderBook& book) const;
    virtual double calculate_bid_price(double fair_value) const;
    virtual double calculate_ask_price(double fair_value) const;
    virtual bool should_refresh_quotes(const market_data::OrderBook& book) const;
    
    // Quote management
    void refresh_quotes(const market_data::OrderBook& book);
    void cancel_existing_quotes();
    bool submit_new_quotes(double bid_price, double ask_price);
    
private:
    void on_order_executed(const ExecutionReport& execution) override;
};

// Statistical arbitrage strategy base class
class StatArbStrategy : public StrategyBase {
public:
    StatArbStrategy(const std::string& strategy_id, const std::vector<uint64_t>& symbol_ids);
    virtual ~StatArbStrategy();
    
    // Stat arb specific configuration
    void set_lookback_period(size_t periods) { lookback_period_ = periods; }
    void set_entry_threshold(double threshold) { entry_threshold_ = threshold; }
    void set_exit_threshold(double threshold) { exit_threshold_ = threshold; }
    void set_max_holding_period_ms(uint64_t period_ms) { max_holding_period_ms_ = period_ms; }
    
    // Strategy interface implementation
    void on_trade(const market_data::TradeMessage& trade) override;
    void on_quote(const market_data::QuoteMessage& quote) override;
    void on_order_book_update(uint64_t symbol_id, const market_data::OrderBook& book) override;
    
    double get_target_notional() const override;
    double get_max_position_size(uint64_t symbol_id) const override;
    double get_max_order_value() const override;
    
protected:
    std::vector<uint64_t> symbol_ids_;
    size_t lookback_period_;
    double entry_threshold_;
    double exit_threshold_;
    uint64_t max_holding_period_ms_;
    
    // Price history for statistical analysis
    std::map<uint64_t, std::vector<double>> price_history_;
    std::map<uint64_t, std::chrono::high_resolution_clock::time_point> last_update_times_;
    
    // Virtual methods for derived stat arb strategies
    virtual double calculate_signal() const = 0;
    virtual std::vector<std::pair<uint64_t, double>> get_target_weights() const = 0;
    virtual bool should_enter_position() const;
    virtual bool should_exit_position() const;
    
    // Statistical calculations
    double calculate_correlation(uint64_t symbol1, uint64_t symbol2) const;
    double calculate_cointegration_signal(uint64_t symbol1, uint64_t symbol2) const;
    double calculate_zscore(const std::vector<double>& values) const;
    
    // Portfolio management
    void rebalance_portfolio();
    void update_price_history(uint64_t symbol_id, double price);
    
private:
    void execute_rebalance(const std::vector<std::pair<uint64_t, double>>& target_weights);
};

} // namespace goldearn::trading