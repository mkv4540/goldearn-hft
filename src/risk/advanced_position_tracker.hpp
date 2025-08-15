#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../market_data/message_types.hpp"
#include "../trading/trading_engine.hpp"

namespace goldearn::risk {

// Fill struct for position tracking
struct Fill {
    uint64_t symbol_id;
    std::string symbol;
    double quantity;
    double price;
    trading::OrderSide side;
    std::string strategy_id;
    market_data::Timestamp fill_time;

    Fill() = default;
    Fill(const trading::ExecutionReport& exec,
         const std::string& symbol_name,
         const std::string& strat_id)
        : symbol_id(exec.symbol_id),
          symbol(symbol_name),
          quantity(exec.executed_quantity),
          price(exec.executed_price),
          side(exec.side),
          strategy_id(strat_id),
          fill_time(exec.execution_time) {
    }
    Fill(uint64_t sym_id,
         const std::string& sym,
         double qty,
         double prc,
         trading::OrderSide s,
         const std::string& strat_id)
        : symbol_id(sym_id),
          symbol(sym),
          quantity(qty),
          price(prc),
          side(s),
          strategy_id(strat_id),
          fill_time() {
    }
};

struct PositionInfo {
    uint64_t symbol_id;
    std::string symbol;
    double quantity;
    double avg_cost;
    double current_price;
    double unrealized_pnl;
    double realized_pnl;
    market_data::Timestamp last_update;
    std::string strategy_id;

    // Risk metrics
    double position_var_1d;
    double position_volatility;
    double beta;
    std::string sector;
    double market_value() const {
        return quantity * current_price;
    }
    double notional_exposure() const {
        return std::abs(market_value());
    }
};

struct PortfolioMetrics {
    double total_long_exposure;
    double total_short_exposure;
    double net_exposure;
    double gross_exposure;
    double total_unrealized_pnl;
    double total_realized_pnl;
    double portfolio_var_1d;
    double portfolio_beta;
    double max_drawdown;
    double sharpe_ratio;
    market_data::Timestamp last_update;
};

struct StrategyMetrics {
    std::string strategy_id;
    double strategy_pnl;
    double strategy_exposure;
    double strategy_var;
    double strategy_sharpe;
    uint32_t active_positions;
    market_data::Timestamp last_trade;
};

class AdvancedPositionTracker {
   public:
    AdvancedPositionTracker();
    ~AdvancedPositionTracker();

    // Initialization
    bool initialize();
    void shutdown();

    // Position updates
    void update_position(const Fill& fill);
    void update_market_data(uint64_t symbol_id, double price);
    void mark_to_market();

    // Position queries
    PositionInfo get_position(uint64_t symbol_id) const;
    std::vector<PositionInfo> get_all_positions() const;
    std::vector<PositionInfo> get_strategy_positions(const std::string& strategy_id) const;

    // Portfolio metrics
    PortfolioMetrics get_portfolio_metrics() const;
    StrategyMetrics get_strategy_metrics(const std::string& strategy_id) const;
    std::vector<StrategyMetrics> get_all_strategy_metrics() const;

    // Risk analytics
    double calculate_portfolio_var(uint32_t days = 1, double confidence = 0.95) const;
    double calculate_strategy_var(const std::string& strategy_id, uint32_t days = 1) const;
    double calculate_correlation_matrix() const;
    double calculate_concentration_risk() const;

    // Stress testing
    double stress_test_portfolio(const std::unordered_map<uint64_t, double>& shock_scenarios) const;
    std::vector<std::pair<std::string, double>> scenario_analysis() const;

    // Performance tracking
    void start_performance_tracking();
    void stop_performance_tracking();

    // Limits and alerts
    void set_position_limits(const std::unordered_map<std::string, double>& limits);
    void set_portfolio_limits(double max_gross_exposure, double max_var);
    std::vector<std::string> check_limit_violations() const;

   private:
    // Position storage
    std::unordered_map<uint64_t, PositionInfo> positions_;
    mutable std::shared_mutex positions_mutex_;

    // Strategy tracking
    std::unordered_map<std::string, StrategyMetrics> strategy_metrics_;
    mutable std::shared_mutex strategy_mutex_;

    // Market data cache
    std::unordered_map<uint64_t, double> current_prices_;
    mutable std::shared_mutex price_mutex_;

    // Portfolio state
    PortfolioMetrics portfolio_metrics_;
    mutable std::mutex portfolio_mutex_;

    // Performance tracking
    std::atomic<bool> tracking_active_;
    std::thread tracking_thread_;
    std::atomic<bool> shutdown_requested_;

    // Risk limits
    std::unordered_map<std::string, double> position_limits_;
    double max_gross_exposure_;
    double max_portfolio_var_;
    mutable std::mutex limits_mutex_;

    // Internal methods
    void update_portfolio_metrics();
    void update_strategy_metrics(const std::string& strategy_id);
    void calculate_position_risk_metrics(PositionInfo& position);
    void performance_tracking_worker();

    // Risk calculation helpers
    double calculate_position_var(const PositionInfo& position, uint32_t days) const;
    double get_symbol_volatility(uint64_t symbol_id) const;
    double get_symbol_beta(uint64_t symbol_id) const;
    std::string get_symbol_sector(uint64_t symbol_id) const;
    double calculate_correlation(uint64_t symbol1, uint64_t symbol2) const;
};

}  // namespace goldearn::risk