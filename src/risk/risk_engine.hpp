#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../core/latency_tracker.hpp"
#include "../trading/position_manager.hpp"
#include "../trading/trading_engine.hpp"

// Custom hash function for std::pair
struct PairHash {
    template <typename T1, typename T2>
    std::size_t operator()(const std::pair<T1, T2>& p) const {
        auto h1 = std::hash<T1>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};

namespace goldearn::risk {

// Risk check result
enum class RiskCheckResult {
    APPROVED = 0,
    REJECTED_POSITION_LIMIT = 1,
    REJECTED_ORDER_SIZE = 2,
    REJECTED_PRICE_LIMIT = 3,
    REJECTED_EXPOSURE_LIMIT = 4,
    REJECTED_VAR_LIMIT = 5,
    REJECTED_VOLATILITY = 6,
    REJECTED_CORRELATION = 7,
    REJECTED_CIRCUIT_BREAKER = 8,
    REJECTED_BLACKLIST = 9,
    REJECTED_SYSTEM_ERROR = 10
};

// Risk violation severity
enum class ViolationSeverity { INFO = 0, WARNING = 1, CRITICAL = 2, EMERGENCY = 3 };

// Risk limits configuration
struct RiskLimits {
    // Position limits
    double max_position_size = 1000000.0;        // Maximum position per symbol
    double max_portfolio_exposure = 10000000.0;  // Maximum total exposure
    double max_strategy_exposure = 5000000.0;    // Maximum exposure per strategy
    double max_sector_concentration = 0.3;       // Maximum % in single sector

    // Order limits
    double max_order_size = 100000.0;       // Maximum single order size
    double max_order_value = 1000000.0;     // Maximum order value
    uint32_t max_orders_per_second = 100;   // Rate limit
    uint32_t max_orders_per_minute = 1000;  // Rate limit

    // Price limits
    double max_price_deviation = 0.05;  // 5% max deviation from fair value
    double min_spread_bps = 1.0;        // Minimum spread in basis points
    double max_market_impact = 0.1;     // Maximum expected market impact

    // Risk metrics limits
    double max_var_1d = 500000.0;            // 1-day Value at Risk
    double max_var_10d = 1500000.0;          // 10-day Value at Risk
    double max_portfolio_volatility = 0.25;  // Annual volatility limit
    double max_correlation_exposure = 0.7;   // Max exposure to correlated assets

    // Circuit breakers
    double max_daily_loss = 1000000.0;     // Maximum daily loss
    double max_drawdown = 2000000.0;       // Maximum drawdown
    uint32_t max_consecutive_losses = 10;  // Max consecutive losing trades

    // Time-based limits
    uint64_t position_hold_time_limit_ms = 86400000;  // 24 hours
    uint64_t order_lifetime_limit_ms = 300000;        // 5 minutes

    RiskLimits() = default;
};

// Risk violation details
struct RiskViolation {
    RiskCheckResult violation_type;
    ViolationSeverity severity;
    std::string description;
    std::string strategy_id;
    uint64_t symbol_id;
    double current_value;
    double limit_value;
    market_data::Timestamp timestamp;

    RiskViolation(RiskCheckResult type, ViolationSeverity sev, const std::string& desc)
        : violation_type(type),
          severity(sev),
          description(desc),
          symbol_id(0),
          current_value(0.0),
          limit_value(0.0) {
        timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::high_resolution_clock::now().time_since_epoch())
                        .count();
    }
};

// Pre-trade risk check context
struct PreTradeContext {
    const trading::Order* order;
    const trading::Position* current_position;
    double current_market_price;
    double estimated_fill_price;
    double portfolio_exposure;
    double strategy_exposure;
    std::unordered_map<uint64_t, double> correlated_positions;

    PreTradeContext()
        : order(nullptr),
          current_position(nullptr),
          current_market_price(0.0),
          estimated_fill_price(0.0),
          portfolio_exposure(0.0),
          strategy_exposure(0.0) {
    }
};

// Post-trade monitoring context
struct PostTradeContext {
    const trading::ExecutionReport* execution;
    const trading::Position* updated_position;
    double portfolio_pnl;
    double strategy_pnl;
    double var_impact;

    PostTradeContext()
        : execution(nullptr),
          updated_position(nullptr),
          portfolio_pnl(0.0),
          strategy_pnl(0.0),
          var_impact(0.0) {
    }
};

// Main risk engine
class RiskEngine {
   public:
    RiskEngine();
    ~RiskEngine();

    // Lifecycle
    bool initialize();
    void shutdown();

    // Configuration
    void set_risk_limits(const RiskLimits& limits);
    const RiskLimits& get_risk_limits() const {
        return limits_;
    }
    void update_risk_limit(const std::string& limit_name, double value);

    // Pre-trade risk checks (must complete in <10μs)
    RiskCheckResult check_pre_trade_risk(const PreTradeContext& context);
    RiskCheckResult quick_pre_trade_check(const trading::Order& order);

    // Post-trade monitoring
    void monitor_post_trade(const PostTradeContext& context);
    void update_portfolio_risk_metrics();

    // Real-time risk monitoring
    void start_risk_monitoring();
    void stop_risk_monitoring();
    bool is_monitoring_active() const {
        return monitoring_active_.load();
    }

    // Circuit breakers
    void trigger_circuit_breaker(const std::string& reason);
    void reset_circuit_breaker();
    bool is_circuit_breaker_active() const {
        return circuit_breaker_active_.load();
    }

    // Risk limit violations
    void set_violation_callback(std::function<void(const RiskViolation&)> callback);
    std::vector<RiskViolation> get_recent_violations(uint32_t hours = 24) const;
    uint32_t get_violation_count(ViolationSeverity min_severity = ViolationSeverity::WARNING) const;

    // Blacklist management
    void add_symbol_to_blacklist(uint64_t symbol_id, const std::string& reason);
    void remove_symbol_from_blacklist(uint64_t symbol_id);
    bool is_symbol_blacklisted(uint64_t symbol_id) const;

    void add_strategy_to_blacklist(const std::string& strategy_id, const std::string& reason);
    void remove_strategy_from_blacklist(const std::string& strategy_id);
    bool is_strategy_blacklisted(const std::string& strategy_id) const;

    // Risk metrics calculation
    double calculate_portfolio_var(uint32_t days = 1) const;
    double calculate_strategy_var(const std::string& strategy_id, uint32_t days = 1) const;
    double calculate_correlation_risk() const;
    double calculate_concentration_risk() const;

    // Position and exposure monitoring
    void set_position_manager(std::shared_ptr<trading::PositionManager> pos_mgr);
    double get_current_exposure() const;
    double get_available_buying_power() const;

    // Performance and statistics
    struct RiskEngineStats {
        uint64_t total_checks_performed;
        uint64_t checks_approved;
        uint64_t checks_rejected;
        double avg_check_latency_ns;
        double max_check_latency_ns;
        uint32_t violations_today;
        uint32_t circuit_breaker_triggers;
        market_data::Timestamp last_violation_time;
    };

    RiskEngineStats get_statistics() const;
    void reset_statistics();

    // Risk reporting
    struct RiskReport {
        double current_var_1d;
        double current_var_10d;
        double portfolio_exposure;
        double available_buying_power;
        double concentration_risk;
        double correlation_risk;
        std::vector<std::pair<uint64_t, double>> largest_positions;
        std::vector<RiskViolation> recent_violations;
        bool circuit_breaker_status;
        market_data::Timestamp report_time;
    };

    RiskReport generate_risk_report() const;

   private:
    RiskLimits limits_;
    std::shared_ptr<trading::PositionManager> position_manager_;

    // Risk state
    std::atomic<bool> initialized_;
    std::atomic<bool> monitoring_active_;
    std::atomic<bool> circuit_breaker_active_;

    // Performance tracking
    std::unique_ptr<core::LatencyTracker> check_latency_tracker_;

    // Violation tracking
    std::vector<RiskViolation> violations_;
    mutable std::shared_mutex violations_mutex_;
    std::function<void(const RiskViolation&)> violation_callback_;

    // Blacklists
    std::unordered_set<uint64_t> blacklisted_symbols_;
    std::unordered_set<std::string> blacklisted_strategies_;
    std::unordered_map<uint64_t, std::string> symbol_blacklist_reasons_;
    std::unordered_map<std::string, std::string> strategy_blacklist_reasons_;
    mutable std::shared_mutex blacklist_mutex_;

    // Rate limiting
    std::unordered_map<std::string, std::deque<market_data::Timestamp>> strategy_order_times_;
    mutable std::mutex rate_limit_mutex_;

    // Statistics
    mutable std::mutex stats_mutex_;
    RiskEngineStats stats_;

    // Monitoring thread
    std::thread monitoring_thread_;
    std::atomic<bool> shutdown_requested_;

    // Individual risk check methods
    RiskCheckResult check_position_limits(const PreTradeContext& context);
    RiskCheckResult check_order_size_limits(const PreTradeContext& context);
    RiskCheckResult check_price_limits(const PreTradeContext& context);
    RiskCheckResult check_exposure_limits(const PreTradeContext& context);
    RiskCheckResult check_var_limits(const PreTradeContext& context);
    RiskCheckResult check_rate_limits(const PreTradeContext& context);
    RiskCheckResult check_blacklists(const PreTradeContext& context);
    RiskCheckResult check_circuit_breakers(const PreTradeContext& context);

    // Risk calculation helpers
    double calculate_order_impact_on_var(const trading::Order& order) const;
    double calculate_position_correlation(
        uint64_t symbol_id, const std::unordered_map<uint64_t, double>& positions) const;
    double estimate_market_impact(const trading::Order& order, double current_price) const;

    // Violation handling
    void record_violation(const RiskViolation& violation);
    void cleanup_old_violations();

    // Rate limiting helpers
    bool check_strategy_rate_limit(const std::string& strategy_id);
    void update_strategy_order_count(const std::string& strategy_id);

    // Monitoring worker
    void risk_monitoring_worker();
    void check_portfolio_risk_limits();
    void check_strategy_risk_limits();
    void check_correlation_limits();

    // Enhanced risk calculation helpers
    double get_strategy_exposure(const std::string& strategy_id) const;
    double get_strategy_volatility(const std::string& strategy_id) const;

    // Performance measurement
    void record_check_latency(uint64_t latency_ns);
    void update_statistics(RiskCheckResult result);
};

// Specialized pre-trade checks for ultra-low latency
class FastPreTradeChecker {
   public:
    FastPreTradeChecker(const RiskLimits& limits);

    // Ultra-fast checks (target <1μs)
    bool quick_position_check(uint64_t symbol_id, double quantity, double current_position) const;
    bool quick_order_size_check(double order_value) const;
    bool quick_exposure_check(double order_value, double current_exposure) const;
    bool quick_blacklist_check(uint64_t symbol_id, const std::string& strategy_id) const;

    // Batch checks for multiple orders
    std::vector<bool> batch_check_orders(const std::vector<trading::Order>& orders) const;

    // Update limits (thread-safe)
    void update_limits(const RiskLimits& limits);

   private:
    std::atomic<RiskLimits> limits_;  // Atomic for lock-free reads
    std::unordered_set<uint64_t> blacklisted_symbols_;
    std::unordered_set<std::string> blacklisted_strategies_;
    mutable std::shared_mutex blacklist_mutex_;
};

// VaR (Value at Risk) calculator
class VaRCalculator {
   public:
    VaRCalculator();
    ~VaRCalculator();

    // VaR calculation methods
    double calculate_parametric_var(
        const std::unordered_map<uint64_t, double>& positions,
        const std::unordered_map<uint64_t, double>& volatilities,
        const std::unordered_map<std::pair<uint64_t, uint64_t>, double, PairHash>& correlations,
        double confidence_level = 0.05,
        uint32_t time_horizon_days = 1) const;

    double calculate_historical_var(
        const std::unordered_map<uint64_t, double>& positions,
        const std::unordered_map<uint64_t, std::vector<double>>& historical_returns,
        double confidence_level = 0.05,
        uint32_t lookback_days = 252) const;

    double calculate_monte_carlo_var(
        const std::unordered_map<uint64_t, double>& positions,
        const std::unordered_map<uint64_t, double>& expected_returns,
        const std::unordered_map<uint64_t, double>& volatilities,
        const std::unordered_map<std::pair<uint64_t, uint64_t>, double, PairHash>& correlations,
        double confidence_level = 0.05,
        uint32_t time_horizon_days = 1,
        uint32_t num_simulations = 10000) const;

    // Component VaR (contribution of each position to total VaR)
    std::unordered_map<uint64_t, double> calculate_component_var(
        const std::unordered_map<uint64_t, double>& positions,
        const std::unordered_map<uint64_t, double>& volatilities,
        const std::unordered_map<std::pair<uint64_t, uint64_t>, double, PairHash>& correlations,
        double confidence_level = 0.05) const;

    // Marginal VaR (change in VaR from small position change)
    double calculate_marginal_var(
        uint64_t symbol_id,
        const std::unordered_map<uint64_t, double>& positions,
        const std::unordered_map<uint64_t, double>& volatilities,
        const std::unordered_map<std::pair<uint64_t, uint64_t>, double, PairHash>& correlations,
        double confidence_level = 0.05) const;

    // Incremental VaR (VaR change from adding new position)
    double calculate_incremental_var(
        uint64_t symbol_id,
        double new_position,
        const std::unordered_map<uint64_t, double>& existing_positions,
        const std::unordered_map<uint64_t, double>& volatilities,
        const std::unordered_map<std::pair<uint64_t, uint64_t>, double, PairHash>& correlations,
        double confidence_level = 0.05) const;

    // Risk decomposition
    struct RiskDecomposition {
        double individual_var;           // VaR of position in isolation
        double diversification_benefit;  // Reduction due to diversification
        double correlation_penalty;      // Increase due to correlations
        double total_contribution;       // Net contribution to portfolio VaR
    };

    std::unordered_map<uint64_t, RiskDecomposition> decompose_portfolio_risk(
        const std::unordered_map<uint64_t, double>& positions,
        const std::unordered_map<uint64_t, double>& volatilities,
        const std::unordered_map<std::pair<uint64_t, uint64_t>, double, PairHash>& correlations)
        const;

   private:
    // Helper methods
    std::vector<double> generate_portfolio_returns(
        const std::unordered_map<uint64_t, double>& positions,
        const std::unordered_map<uint64_t, std::vector<double>>& asset_returns) const;

    double calculate_portfolio_volatility(
        const std::unordered_map<uint64_t, double>& weights,
        const std::unordered_map<uint64_t, double>& volatilities,
        const std::unordered_map<std::pair<uint64_t, uint64_t>, double, PairHash>& correlations)
        const;

    std::vector<double> simulate_portfolio_returns(
        const std::unordered_map<uint64_t, double>& positions,
        const std::unordered_map<uint64_t, double>& expected_returns,
        const std::unordered_map<uint64_t, double>& volatilities,
        const std::unordered_map<std::pair<uint64_t, uint64_t>, double, PairHash>& correlations,
        uint32_t num_simulations) const;

    double quantile(std::vector<double> values, double percentile) const;
};

}  // namespace goldearn::risk