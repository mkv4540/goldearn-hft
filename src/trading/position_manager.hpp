#pragma once

#include "trading_engine.hpp"
#include "../market_data/message_types.hpp"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>

namespace goldearn::trading {

// Position information for a single symbol
struct Position {
    uint64_t symbol_id;
    std::string strategy_id;
    double quantity;           // Positive for long, negative for short
    double avg_entry_price;    // Volume-weighted average entry price
    double realized_pnl;       // Realized profit/loss from closed trades
    double unrealized_pnl;     // Mark-to-market profit/loss
    double total_pnl;          // Realized + unrealized
    
    // Risk metrics
    double max_position_size;  // Maximum allowed position
    double var_1d;            // 1-day Value at Risk
    double exposure_value;     // Position value in base currency
    
    // Trade statistics
    uint64_t total_trades;
    uint64_t winning_trades;
    double largest_win;
    double largest_loss;
    double avg_trade_pnl;
    
    // Timestamps
    market_data::Timestamp first_trade_time;
    market_data::Timestamp last_trade_time;
    market_data::Timestamp last_update_time;
    
    Position() : symbol_id(0), quantity(0.0), avg_entry_price(0.0), 
                 realized_pnl(0.0), unrealized_pnl(0.0), total_pnl(0.0),
                 max_position_size(0.0), var_1d(0.0), exposure_value(0.0),
                 total_trades(0), winning_trades(0), largest_win(0.0),
                 largest_loss(0.0), avg_trade_pnl(0.0) {}
    
    bool is_long() const { return quantity > 0.0; }
    bool is_short() const { return quantity < 0.0; }
    bool is_flat() const { return std::abs(quantity) < 1e-8; }
    double get_notional_value(double current_price) const { return std::abs(quantity) * current_price; }
    double get_win_rate() const { return total_trades > 0 ? (double)winning_trades / total_trades : 0.0; }
};

// Aggregated portfolio position across all strategies
struct PortfolioPosition {
    uint64_t symbol_id;
    double net_quantity;       // Net position across all strategies
    double gross_quantity;     // Gross position (sum of absolute values)
    double weighted_avg_price; // Weighted average entry price
    double total_realized_pnl;
    double total_unrealized_pnl;
    double total_exposure;
    
    std::vector<std::string> strategy_ids; // Strategies holding this symbol
    
    PortfolioPosition() : symbol_id(0), net_quantity(0.0), gross_quantity(0.0),
                         weighted_avg_price(0.0), total_realized_pnl(0.0),
                         total_unrealized_pnl(0.0), total_exposure(0.0) {}
};

// Real-time position tracking and P&L calculation
class PositionManager {
public:
    PositionManager();
    ~PositionManager();
    
    // Lifecycle
    bool initialize();
    void shutdown();
    
    // Position updates from trade executions
    void update_position(const ExecutionReport& execution);
    void update_market_price(uint64_t symbol_id, double market_price);
    void update_all_market_prices(const std::unordered_map<uint64_t, double>& prices);
    
    // Position queries
    Position* get_position(const std::string& strategy_id, uint64_t symbol_id);
    const Position* get_position(const std::string& strategy_id, uint64_t symbol_id) const;
    std::vector<Position*> get_strategy_positions(const std::string& strategy_id);
    std::vector<Position*> get_symbol_positions(uint64_t symbol_id);
    
    // Portfolio-level queries
    PortfolioPosition get_portfolio_position(uint64_t symbol_id) const;
    std::vector<PortfolioPosition> get_all_portfolio_positions() const;
    
    // P&L calculations
    double get_strategy_pnl(const std::string& strategy_id) const;
    double get_symbol_pnl(uint64_t symbol_id) const;
    double get_total_portfolio_pnl() const;
    double get_realized_pnl() const;
    double get_unrealized_pnl() const;
    
    // Risk metrics
    double get_strategy_exposure(const std::string& strategy_id) const;
    double get_symbol_exposure(uint64_t symbol_id) const;
    double get_total_exposure() const;
    double get_gross_exposure() const;
    double get_net_exposure() const;
    
    // Position limits and risk controls
    void set_position_limit(const std::string& strategy_id, uint64_t symbol_id, double limit);
    void set_strategy_exposure_limit(const std::string& strategy_id, double limit);
    void set_symbol_exposure_limit(uint64_t symbol_id, double limit);
    void set_total_exposure_limit(double limit);
    
    bool check_position_limit(const std::string& strategy_id, uint64_t symbol_id, double trade_quantity) const;
    bool check_exposure_limits(const std::string& strategy_id, uint64_t symbol_id, double trade_value) const;
    
    // Position management operations
    bool flatten_position(const std::string& strategy_id, uint64_t symbol_id);
    bool flatten_strategy_positions(const std::string& strategy_id);
    bool flatten_all_positions();
    
    // Statistics and reporting
    struct PositionStats {
        uint64_t total_positions;
        uint64_t long_positions;
        uint64_t short_positions;
        double total_realized_pnl;
        double total_unrealized_pnl;
        double largest_position_value;
        double total_gross_exposure;
        double total_net_exposure;
        uint64_t total_trades_today;
        double avg_position_duration_minutes;
    };
    
    PositionStats get_position_statistics() const;
    
    // Historical P&L tracking
    struct DailyPnL {
        std::string date;
        double realized_pnl;
        double unrealized_pnl;
        double total_pnl;
        double max_drawdown;
        double max_exposure;
        uint64_t total_trades;
    };
    
    std::vector<DailyPnL> get_daily_pnl_history(uint32_t days = 30) const;
    DailyPnL get_today_pnl() const;
    
    // Real-time position monitoring
    void set_position_change_callback(std::function<void(const Position&)> callback);
    void set_pnl_threshold_callback(double threshold, std::function<void(double)> callback);
    
    // Position reconciliation
    bool reconcile_positions(const std::unordered_map<std::string, std::unordered_map<uint64_t, double>>& external_positions);
    std::vector<std::pair<std::string, uint64_t>> get_position_discrepancies() const;
    
private:
    // Position storage - nested maps: strategy_id -> symbol_id -> Position
    std::unordered_map<std::string, std::unordered_map<uint64_t, std::unique_ptr<Position>>> positions_;
    mutable std::shared_mutex positions_mutex_;
    
    // Market prices cache for P&L calculation
    std::unordered_map<uint64_t, double> market_prices_;
    std::unordered_map<uint64_t, market_data::Timestamp> price_timestamps_;
    mutable std::shared_mutex prices_mutex_;
    
    // Risk limits
    std::unordered_map<std::pair<std::string, uint64_t>, double, 
                      boost::hash<std::pair<std::string, uint64_t>>> position_limits_;
    std::unordered_map<std::string, double> strategy_exposure_limits_;
    std::unordered_map<uint64_t, double> symbol_exposure_limits_;
    double total_exposure_limit_;
    mutable std::shared_mutex limits_mutex_;
    
    // Callbacks and monitoring
    std::function<void(const Position&)> position_change_callback_;
    std::function<void(double)> pnl_threshold_callback_;
    double pnl_threshold_;
    
    // Statistics tracking
    mutable std::mutex stats_mutex_;
    std::unordered_map<std::string, DailyPnL> daily_pnl_history_;
    
    // Internal helper methods
    Position* get_or_create_position(const std::string& strategy_id, uint64_t symbol_id);
    void update_position_pnl(Position& position, double market_price);
    void update_position_statistics(Position& position, const ExecutionReport& execution);
    void calculate_avg_entry_price(Position& position, double trade_price, double trade_quantity);
    
    // P&L calculation helpers
    double calculate_unrealized_pnl(const Position& position, double market_price) const;
    double calculate_realized_pnl_from_trade(const Position& position, double trade_price, double trade_quantity) const;
    
    // Risk calculation helpers
    double calculate_position_var(const Position& position, double volatility) const;
    double calculate_position_exposure(const Position& position, double market_price) const;
    
    // Historical data management
    void update_daily_pnl();
    void archive_old_pnl_data();
    std::string get_current_date_string() const;
    
    // Position validation and reconciliation
    bool validate_position_data(const Position& position) const;
    void fix_position_inconsistencies(Position& position);
};

// Advanced position analytics
class PositionAnalytics {
public:
    PositionAnalytics(const PositionManager* position_manager);
    
    // Performance attribution
    struct AttributionReport {
        std::string strategy_id;
        double selection_return;    // Return from security selection
        double timing_return;       // Return from timing decisions
        double interaction_return;  // Interaction between selection and timing
        double total_return;
        double information_ratio;
        double tracking_error;
    };
    
    std::vector<AttributionReport> calculate_performance_attribution() const;
    
    // Risk analytics
    struct RiskMetrics {
        double portfolio_var_1d;     // 1-day Value at Risk
        double portfolio_cvar_1d;    // 1-day Conditional VaR
        double portfolio_volatility; // Annualized volatility
        double sharpe_ratio;
        double max_drawdown;
        double correlation_risk;     // Risk from correlated positions
        double concentration_risk;   // Risk from concentrated positions
    };
    
    RiskMetrics calculate_portfolio_risk() const;
    
    // Position optimization
    struct OptimizationConstraints {
        double max_total_exposure;
        double max_single_position_weight;
        double max_sector_concentration;
        double target_volatility;
        std::unordered_map<uint64_t, double> min_position_sizes;
        std::unordered_map<uint64_t, double> max_position_sizes;
    };
    
    std::unordered_map<uint64_t, double> optimize_portfolio(const OptimizationConstraints& constraints) const;
    
    // Stress testing
    struct StressScenario {
        std::string scenario_name;
        std::unordered_map<uint64_t, double> price_shocks; // Symbol -> % change
        double portfolio_pnl_impact;
        double worst_position_impact;
        std::vector<std::pair<uint64_t, double>> position_impacts;
    };
    
    std::vector<StressScenario> run_stress_tests() const;
    
    // Correlation analysis
    std::unordered_map<std::pair<uint64_t, uint64_t>, double, 
                      boost::hash<std::pair<uint64_t, uint64_t>>> calculate_position_correlations() const;
    
private:
    const PositionManager* position_manager_;
    
    // Helper methods for analytics
    std::vector<double> get_historical_returns(uint64_t symbol_id, uint32_t days) const;
    double calculate_correlation(const std::vector<double>& returns1, const std::vector<double>& returns2) const;
    double calculate_volatility(const std::vector<double>& returns) const;
    double calculate_var(const std::vector<double>& returns, double confidence_level = 0.05) const;
};

} // namespace goldearn::trading