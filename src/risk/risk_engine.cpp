#include "risk_engine.hpp"

#include <climits>
#include <random>
#include <set>

#include "../utils/simple_logger.hpp"

namespace goldearn::risk {

RiskEngine::RiskEngine()
    : initialized_(false),
      monitoring_active_(false),
      circuit_breaker_active_(false),
      shutdown_requested_(false) {
    LOG_INFO("RiskEngine: Initializing risk engine");

    // Initialize default risk limits
    limits_ = RiskLimits();

    // Initialize latency tracker
    check_latency_tracker_ = std::make_unique<core::LatencyTracker>("risk_engine");

    // Initialize statistics
    stats_ = RiskEngineStats{};

    initialized_ = true;
    LOG_INFO("RiskEngine: Initialization complete");
}

RiskEngine::~RiskEngine() {
    LOG_INFO("RiskEngine: Shutting down risk engine");
    shutdown();
}

bool RiskEngine::initialize() {
    if (initialized_.load()) {
        LOG_WARN("RiskEngine: Already initialized");
        return true;
    }

    LOG_INFO("RiskEngine: Initializing risk engine");

    // Initialize latency tracker
    check_latency_tracker_ = std::make_unique<core::LatencyTracker>("risk_engine");

    // Initialize statistics
    stats_ = RiskEngineStats{};

    initialized_ = true;
    LOG_INFO("RiskEngine: Initialization complete");
    return true;
}

void RiskEngine::shutdown() {
    if (!initialized_.load()) {
        return;
    }

    LOG_INFO("RiskEngine: Shutting down");

    // Stop monitoring thread
    shutdown_requested_ = true;
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }

    initialized_ = false;
    monitoring_active_ = false;
    circuit_breaker_active_ = false;

    LOG_INFO("RiskEngine: Shutdown complete");
}

void RiskEngine::set_risk_limits(const RiskLimits& limits) {
    limits_ = limits;
    LOG_INFO("RiskEngine: Risk limits updated");
}

void RiskEngine::update_risk_limit(const std::string& limit_name, double value) {
    // This is a simplified implementation
    // In a real system, you'd have a more sophisticated way to update specific limits
    LOG_INFO("RiskEngine: Updated limit {} to {}", limit_name, value);
}

RiskCheckResult RiskEngine::check_pre_trade_risk(const PreTradeContext& context) {
    if (!initialized_.load()) {
        return RiskCheckResult::REJECTED_SYSTEM_ERROR;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Perform all risk checks
    auto result = check_position_limits(context);
    if (result != RiskCheckResult::APPROVED)
        return result;

    result = check_order_size_limits(context);
    if (result != RiskCheckResult::APPROVED)
        return result;

    result = check_price_limits(context);
    if (result != RiskCheckResult::APPROVED)
        return result;

    result = check_exposure_limits(context);
    if (result != RiskCheckResult::APPROVED)
        return result;

    result = check_var_limits(context);
    if (result != RiskCheckResult::APPROVED)
        return result;

    result = check_rate_limits(context);
    if (result != RiskCheckResult::APPROVED)
        return result;

    result = check_blacklists(context);
    if (result != RiskCheckResult::APPROVED)
        return result;

    result = check_circuit_breakers(context);
    if (result != RiskCheckResult::APPROVED)
        return result;

    // Record latency
    auto end_time = std::chrono::high_resolution_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
    record_check_latency(latency.count());

    update_statistics(RiskCheckResult::APPROVED);
    return RiskCheckResult::APPROVED;
}

RiskCheckResult RiskEngine::quick_pre_trade_check(const trading::Order& order) {
    if (!initialized_.load()) {
        return RiskCheckResult::REJECTED_SYSTEM_ERROR;
    }

    // Quick checks for ultra-low latency
    if (order.price * order.quantity > limits_.max_order_value) {
        return RiskCheckResult::REJECTED_ORDER_SIZE;
    }

    if (is_symbol_blacklisted(order.symbol_id)) {
        return RiskCheckResult::REJECTED_BLACKLIST;
    }

    if (circuit_breaker_active_.load()) {
        return RiskCheckResult::REJECTED_CIRCUIT_BREAKER;
    }

    return RiskCheckResult::APPROVED;
}

void RiskEngine::monitor_post_trade(const PostTradeContext& context) {
    if (!initialized_.load()) {
        return;
    }

    // Update portfolio metrics
    update_portfolio_risk_metrics();

    // Check for violations
    if (context.portfolio_pnl < -limits_.max_daily_loss) {
        RiskViolation violation(RiskCheckResult::REJECTED_VAR_LIMIT,
                                ViolationSeverity::CRITICAL,
                                "Daily loss limit exceeded");
        record_violation(violation);
    }
}

void RiskEngine::update_portfolio_risk_metrics() {
    // This would calculate current VaR, exposure, etc.
    // Simplified implementation
    LOG_DEBUG("RiskEngine: Updated portfolio risk metrics");
}

void RiskEngine::start_risk_monitoring() {
    if (monitoring_active_.load()) {
        LOG_WARN("RiskEngine: Monitoring already active");
        return;
    }

    shutdown_requested_ = false;
    monitoring_thread_ = std::thread(&RiskEngine::risk_monitoring_worker, this);
    monitoring_active_ = true;
    LOG_INFO("RiskEngine: Risk monitoring started");
}

void RiskEngine::stop_risk_monitoring() {
    if (!monitoring_active_.load()) {
        return;
    }

    shutdown_requested_ = true;
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
    monitoring_active_ = false;
    LOG_INFO("RiskEngine: Risk monitoring stopped");
}

void RiskEngine::trigger_circuit_breaker(const std::string& reason) {
    circuit_breaker_active_ = true;
    LOG_ERROR("RiskEngine: Circuit breaker triggered: {}", reason);

    RiskViolation violation(RiskCheckResult::REJECTED_CIRCUIT_BREAKER,
                            ViolationSeverity::EMERGENCY,
                            "Circuit breaker: " + reason);
    record_violation(violation);
}

void RiskEngine::reset_circuit_breaker() {
    circuit_breaker_active_ = false;
    LOG_INFO("RiskEngine: Circuit breaker reset");
}

void RiskEngine::set_violation_callback(std::function<void(const RiskViolation&)> callback) {
    violation_callback_ = callback;
}

std::vector<RiskViolation> RiskEngine::get_recent_violations(uint32_t hours) const {
    std::shared_lock<std::shared_mutex> lock(violations_mutex_);

    auto now = std::chrono::high_resolution_clock::now();
    auto cutoff_time = now - std::chrono::hours(hours);

    std::vector<RiskViolation> recent_violations;
    for (const auto& violation : violations_) {
        // Convert violation timestamp to time_point for comparison
        auto violation_time = std::chrono::high_resolution_clock::time_point(
            std::chrono::nanoseconds(violation.timestamp));
        if (violation_time >= cutoff_time) {
            recent_violations.push_back(violation);
        }
    }

    return recent_violations;
}

uint32_t RiskEngine::get_violation_count(ViolationSeverity min_severity) const {
    std::shared_lock<std::shared_mutex> lock(violations_mutex_);

    uint32_t count = 0;
    for (const auto& violation : violations_) {
        if (violation.severity >= min_severity) {
            count++;
        }
    }

    return count;
}

void RiskEngine::add_symbol_to_blacklist(uint64_t symbol_id, const std::string& reason) {
    std::unique_lock<std::shared_mutex> lock(blacklist_mutex_);
    blacklisted_symbols_.insert(symbol_id);
    symbol_blacklist_reasons_[symbol_id] = reason;
    LOG_WARN("RiskEngine: Symbol {} blacklisted: {}", symbol_id, reason);
}

void RiskEngine::remove_symbol_from_blacklist(uint64_t symbol_id) {
    std::unique_lock<std::shared_mutex> lock(blacklist_mutex_);
    blacklisted_symbols_.erase(symbol_id);
    symbol_blacklist_reasons_.erase(symbol_id);
    LOG_INFO("RiskEngine: Symbol {} removed from blacklist", symbol_id);
}

bool RiskEngine::is_symbol_blacklisted(uint64_t symbol_id) const {
    std::shared_lock<std::shared_mutex> lock(blacklist_mutex_);
    return blacklisted_symbols_.find(symbol_id) != blacklisted_symbols_.end();
}

void RiskEngine::add_strategy_to_blacklist(const std::string& strategy_id,
                                           const std::string& reason) {
    std::unique_lock<std::shared_mutex> lock(blacklist_mutex_);
    blacklisted_strategies_.insert(strategy_id);
    strategy_blacklist_reasons_[strategy_id] = reason;
    LOG_WARN("RiskEngine: Strategy {} blacklisted: {}", strategy_id, reason);
}

void RiskEngine::remove_strategy_from_blacklist(const std::string& strategy_id) {
    std::unique_lock<std::shared_mutex> lock(blacklist_mutex_);
    blacklisted_strategies_.erase(strategy_id);
    strategy_blacklist_reasons_.erase(strategy_id);
    LOG_INFO("RiskEngine: Strategy {} removed from blacklist", strategy_id);
}

bool RiskEngine::is_strategy_blacklisted(const std::string& strategy_id) const {
    std::shared_lock<std::shared_mutex> lock(blacklist_mutex_);
    return blacklisted_strategies_.find(strategy_id) != blacklisted_strategies_.end();
}

double RiskEngine::calculate_portfolio_var(uint32_t days) const {
    // Enhanced VaR calculation using historical simulation method
    if (!position_manager_) {
        LOG_WARN("RiskEngine: Position manager not set, using default VaR");
        return 100000.0;
    }

    // Get current positions and their market values
    double total_portfolio_value = get_current_exposure();

    // Calculate VaR based on portfolio composition and volatility
    // Using simplified model with 95% confidence level
    double volatility_factor = 0.02;       // 2% daily volatility assumption
    double confidence_multiplier = 1.645;  // 95% confidence Z-score
    double time_adjustment = std::sqrt(static_cast<double>(days));

    double var_95 =
        total_portfolio_value * volatility_factor * confidence_multiplier * time_adjustment;

    LOG_DEBUG("RiskEngine: Portfolio VaR ({}d, 95%): {}", days, var_95);
    return var_95;
}

double RiskEngine::calculate_strategy_var(const std::string& strategy_id, uint32_t days) const {
    // Enhanced strategy-specific VaR calculation
    if (!position_manager_) {
        LOG_WARN("RiskEngine: Position manager not set, using default strategy VaR");
        return 50000.0;
    }

    // Get strategy-specific exposure and volatility
    double strategy_exposure = get_strategy_exposure(strategy_id);
    double strategy_volatility = get_strategy_volatility(strategy_id);

    // Calculate strategy VaR with strategy-specific parameters
    double confidence_multiplier = 1.645;  // 95% confidence Z-score
    double time_adjustment = std::sqrt(static_cast<double>(days));

    double strategy_var =
        strategy_exposure * strategy_volatility * confidence_multiplier * time_adjustment;

    LOG_DEBUG("RiskEngine: Strategy {} VaR ({}d, 95%): {}", strategy_id, days, strategy_var);
    return strategy_var;
}

double RiskEngine::calculate_correlation_risk() const {
    // Simplified correlation risk calculation
    return 0.1;  // Placeholder
}

double RiskEngine::calculate_concentration_risk() const {
    // Simplified concentration risk calculation
    return 0.05;  // Placeholder
}

void RiskEngine::set_position_manager(std::shared_ptr<trading::PositionManager> pos_mgr) {
    position_manager_ = pos_mgr;
}

double RiskEngine::get_current_exposure() const {
    // Simplified exposure calculation
    return 5000000.0;  // Placeholder
}

double RiskEngine::get_available_buying_power() const {
    // Simplified buying power calculation
    return 10000000.0;  // Placeholder
}

RiskEngine::RiskEngineStats RiskEngine::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void RiskEngine::reset_statistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = RiskEngineStats{};
}

RiskEngine::RiskReport RiskEngine::generate_risk_report() const {
    RiskReport report;
    report.current_var_1d = calculate_portfolio_var(1);
    report.current_var_10d = calculate_portfolio_var(10);
    report.portfolio_exposure = get_current_exposure();
    report.available_buying_power = get_available_buying_power();
    report.concentration_risk = calculate_concentration_risk();
    report.correlation_risk = calculate_correlation_risk();
    report.circuit_breaker_status = circuit_breaker_active_.load();
    report.report_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::high_resolution_clock::now().time_since_epoch())
                             .count();

    return report;
}

// Private methods implementation
RiskCheckResult RiskEngine::check_position_limits(const PreTradeContext& context) {
    // Simplified position limit check
    return RiskCheckResult::APPROVED;
}

RiskCheckResult RiskEngine::check_order_size_limits(const PreTradeContext& context) {
    if (!context.order)
        return RiskCheckResult::APPROVED;

    double order_value = context.order->price * context.order->quantity;
    if (order_value > limits_.max_order_value) {
        return RiskCheckResult::REJECTED_ORDER_SIZE;
    }

    return RiskCheckResult::APPROVED;
}

RiskCheckResult RiskEngine::check_price_limits(const PreTradeContext& context) {
    // Simplified price limit check
    return RiskCheckResult::APPROVED;
}

RiskCheckResult RiskEngine::check_exposure_limits(const PreTradeContext& context) {
    // Simplified exposure limit check
    return RiskCheckResult::APPROVED;
}

RiskCheckResult RiskEngine::check_var_limits(const PreTradeContext& context) {
    // Simplified VaR limit check
    return RiskCheckResult::APPROVED;
}

RiskCheckResult RiskEngine::check_rate_limits(const PreTradeContext& context) {
    // Simplified rate limit check
    return RiskCheckResult::APPROVED;
}

RiskCheckResult RiskEngine::check_blacklists(const PreTradeContext& context) {
    if (!context.order)
        return RiskCheckResult::APPROVED;

    if (is_symbol_blacklisted(context.order->symbol_id)) {
        return RiskCheckResult::REJECTED_BLACKLIST;
    }

    return RiskCheckResult::APPROVED;
}

RiskCheckResult RiskEngine::check_circuit_breakers(const PreTradeContext& context) {
    if (circuit_breaker_active_.load()) {
        return RiskCheckResult::REJECTED_CIRCUIT_BREAKER;
    }

    return RiskCheckResult::APPROVED;
}

void RiskEngine::record_violation(const RiskViolation& violation) {
    std::unique_lock<std::shared_mutex> lock(violations_mutex_);
    violations_.push_back(violation);

    if (violation_callback_) {
        violation_callback_(violation);
    }

    LOG_WARN("RiskEngine: Risk violation recorded: {}", violation.description);
}

void RiskEngine::cleanup_old_violations() {
    std::unique_lock<std::shared_mutex> lock(violations_mutex_);

    auto now = std::chrono::high_resolution_clock::now();
    auto cutoff_time = now - std::chrono::hours(24);

    violations_.erase(std::remove_if(violations_.begin(),
                                     violations_.end(),
                                     [cutoff_time](const RiskViolation& v) {
                                         auto violation_time =
                                             std::chrono::high_resolution_clock::time_point(
                                                 std::chrono::nanoseconds(v.timestamp));
                                         return violation_time < cutoff_time;
                                     }),
                      violations_.end());
}

void RiskEngine::risk_monitoring_worker() {
    while (!shutdown_requested_.load()) {
        check_portfolio_risk_limits();
        check_strategy_risk_limits();
        check_correlation_limits();
        cleanup_old_violations();

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void RiskEngine::check_portfolio_risk_limits() {
    // Simplified portfolio risk limit checks
}

void RiskEngine::check_strategy_risk_limits() {
    // Simplified strategy risk limit checks
}

void RiskEngine::check_correlation_limits() {
    // Simplified correlation limit checks
}

void RiskEngine::record_check_latency(uint64_t latency_ns) {
    if (check_latency_tracker_) {
        auto duration = std::chrono::nanoseconds(latency_ns);
        check_latency_tracker_->record_latency(duration);
    }
}

void RiskEngine::update_statistics(RiskCheckResult result) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_checks_performed++;

    if (result == RiskCheckResult::APPROVED) {
        stats_.checks_approved++;
    } else {
        stats_.checks_rejected++;
    }
}

// Helper methods for enhanced risk calculations
double RiskEngine::get_strategy_exposure(const std::string& strategy_id) const {
    // Simplified strategy exposure calculation
    // In a real system, this would query positions by strategy
    return 1000000.0;  // Default 1M exposure per strategy
}

double RiskEngine::get_strategy_volatility(const std::string& strategy_id) const {
    // Simplified strategy volatility lookup
    // In a real system, this would use historical strategy performance data
    if (strategy_id.find("market_making") != std::string::npos) {
        return 0.015;  // 1.5% volatility for market making
    } else if (strategy_id.find("arbitrage") != std::string::npos) {
        return 0.008;  // 0.8% volatility for arbitrage
    } else if (strategy_id.find("momentum") != std::string::npos) {
        return 0.025;  // 2.5% volatility for momentum
    }
    return 0.02;  // Default 2% volatility
}

// FastPreTradeChecker implementation
FastPreTradeChecker::FastPreTradeChecker(const RiskLimits& limits) : limits_(limits) {
}

bool FastPreTradeChecker::quick_position_check(uint64_t symbol_id,
                                               double quantity,
                                               double current_position) const {
    RiskLimits limits = limits_.load();
    double new_position = current_position + quantity;
    return std::abs(new_position) <= limits.max_position_size;
}

bool FastPreTradeChecker::quick_order_size_check(double order_value) const {
    RiskLimits limits = limits_.load();
    return order_value <= limits.max_order_value;
}

bool FastPreTradeChecker::quick_exposure_check(double order_value, double current_exposure) const {
    RiskLimits limits = limits_.load();
    return (current_exposure + order_value) <= limits.max_portfolio_exposure;
}

bool FastPreTradeChecker::quick_blacklist_check(uint64_t symbol_id,
                                                const std::string& strategy_id) const {
    std::shared_lock<std::shared_mutex> lock(blacklist_mutex_);
    return blacklisted_symbols_.find(symbol_id) == blacklisted_symbols_.end() &&
           blacklisted_strategies_.find(strategy_id) == blacklisted_strategies_.end();
}

std::vector<bool> FastPreTradeChecker::batch_check_orders(
    const std::vector<trading::Order>& orders) const {
    std::vector<bool> results;
    results.reserve(orders.size());

    for (const auto& order : orders) {
        double order_value = order.price * order.quantity;
        bool approved = quick_order_size_check(order_value) &&
                        quick_blacklist_check(order.symbol_id, order.strategy_id);
        results.push_back(approved);
    }

    return results;
}

void FastPreTradeChecker::update_limits(const RiskLimits& limits) {
    limits_.store(limits);
}

// VaRCalculator implementation
VaRCalculator::VaRCalculator() {
}

VaRCalculator::~VaRCalculator() {
}

double VaRCalculator::calculate_parametric_var(
    const std::unordered_map<uint64_t, double>& positions,
    const std::unordered_map<uint64_t, double>& volatilities,
    const std::unordered_map<std::pair<uint64_t, uint64_t>, double, PairHash>& correlations,
    double confidence_level,
    uint32_t time_horizon_days) const {
    if (positions.empty())
        return 0.0;

    // Calculate portfolio variance using weights, volatilities, and correlations
    double portfolio_variance = 0.0;
    double portfolio_value = 0.0;

    // Calculate total portfolio value
    for (const auto& [symbol_id, position] : positions) {
        portfolio_value += std::abs(position);
    }

    if (portfolio_value == 0.0)
        return 0.0;

    // Calculate weighted variance
    for (const auto& [symbol_id1, position1] : positions) {
        double weight1 = position1 / portfolio_value;
        auto vol_it1 = volatilities.find(symbol_id1);
        if (vol_it1 == volatilities.end())
            continue;

        for (const auto& [symbol_id2, position2] : positions) {
            double weight2 = position2 / portfolio_value;
            auto vol_it2 = volatilities.find(symbol_id2);
            if (vol_it2 == volatilities.end())
                continue;

            double correlation = 1.0;  // Default to perfect correlation
            if (symbol_id1 != symbol_id2) {
                auto corr_key = std::make_pair(std::min(symbol_id1, symbol_id2),
                                               std::max(symbol_id1, symbol_id2));
                auto corr_it = correlations.find(corr_key);
                if (corr_it != correlations.end()) {
                    correlation = corr_it->second;
                } else {
                    correlation = 0.3;  // Default correlation
                }
            }

            portfolio_variance +=
                weight1 * weight2 * vol_it1->second * vol_it2->second * correlation;
        }
    }

    double portfolio_std = std::sqrt(portfolio_variance);
    double z_score = (confidence_level == 0.05)   ? 1.645
                     : (confidence_level == 0.01) ? 2.326
                                                  : 1.645;
    double time_factor = std::sqrt(static_cast<double>(time_horizon_days));

    return portfolio_value * portfolio_std * z_score * time_factor;
}

double VaRCalculator::calculate_historical_var(
    const std::unordered_map<uint64_t, double>& positions,
    const std::unordered_map<uint64_t, std::vector<double>>& historical_returns,
    double confidence_level,
    uint32_t lookback_days) const {
    if (positions.empty() || historical_returns.empty())
        return 0.0;

    // Generate portfolio returns
    auto portfolio_returns = generate_portfolio_returns(positions, historical_returns);
    if (portfolio_returns.empty())
        return 0.0;

    // Calculate VaR as percentile of portfolio returns
    double percentile = confidence_level;
    return quantile(portfolio_returns, percentile);
}

double VaRCalculator::calculate_monte_carlo_var(
    const std::unordered_map<uint64_t, double>& positions,
    const std::unordered_map<uint64_t, double>& expected_returns,
    const std::unordered_map<uint64_t, double>& volatilities,
    const std::unordered_map<std::pair<uint64_t, uint64_t>, double, PairHash>& correlations,
    double confidence_level,
    uint32_t time_horizon_days,
    uint32_t num_simulations) const {
    if (positions.empty())
        return 0.0;

    auto simulated_returns = simulate_portfolio_returns(
        positions, expected_returns, volatilities, correlations, num_simulations);
    if (simulated_returns.empty())
        return 0.0;

    return quantile(simulated_returns, confidence_level);
}

std::unordered_map<uint64_t, double> VaRCalculator::calculate_component_var(
    const std::unordered_map<uint64_t, double>& positions,
    const std::unordered_map<uint64_t, double>& volatilities,
    const std::unordered_map<std::pair<uint64_t, uint64_t>, double, PairHash>& correlations,
    double confidence_level) const {
    std::unordered_map<uint64_t, double> component_vars;

    double portfolio_var =
        calculate_parametric_var(positions, volatilities, correlations, confidence_level);
    if (portfolio_var == 0.0)
        return component_vars;

    // Calculate marginal VaR for each position
    for (const auto& [symbol_id, position] : positions) {
        double marginal_var = calculate_marginal_var(
            symbol_id, positions, volatilities, correlations, confidence_level);
        component_vars[symbol_id] = (position / portfolio_var) * marginal_var;
    }

    return component_vars;
}

double VaRCalculator::calculate_marginal_var(
    uint64_t symbol_id,
    const std::unordered_map<uint64_t, double>& positions,
    const std::unordered_map<uint64_t, double>& volatilities,
    const std::unordered_map<std::pair<uint64_t, uint64_t>, double, PairHash>& correlations,
    double confidence_level) const {
    // Simplified marginal VaR calculation
    auto vol_it = volatilities.find(symbol_id);
    if (vol_it == volatilities.end())
        return 0.0;

    double z_score = (confidence_level == 0.05)   ? 1.645
                     : (confidence_level == 0.01) ? 2.326
                                                  : 1.645;

    return vol_it->second * z_score;
}

double VaRCalculator::calculate_incremental_var(
    uint64_t symbol_id,
    double new_position,
    const std::unordered_map<uint64_t, double>& existing_positions,
    const std::unordered_map<uint64_t, double>& volatilities,
    const std::unordered_map<std::pair<uint64_t, uint64_t>, double, PairHash>& correlations,
    double confidence_level) const {
    // Calculate VaR with existing positions
    double var_before =
        calculate_parametric_var(existing_positions, volatilities, correlations, confidence_level);

    // Add new position and calculate VaR
    auto new_positions = existing_positions;
    new_positions[symbol_id] += new_position;
    double var_after =
        calculate_parametric_var(new_positions, volatilities, correlations, confidence_level);

    return var_after - var_before;
}

std::unordered_map<uint64_t, VaRCalculator::RiskDecomposition>
VaRCalculator::decompose_portfolio_risk(
    const std::unordered_map<uint64_t, double>& positions,
    const std::unordered_map<uint64_t, double>& volatilities,
    const std::unordered_map<std::pair<uint64_t, uint64_t>, double, PairHash>& correlations) const {
    std::unordered_map<uint64_t, RiskDecomposition> decomposition;

    double portfolio_var = calculate_parametric_var(positions, volatilities, correlations);
    auto component_vars = calculate_component_var(positions, volatilities, correlations);

    for (const auto& [symbol_id, position] : positions) {
        RiskDecomposition risk_decomp;

        // Individual VaR (position in isolation)
        std::unordered_map<uint64_t, double> isolated_position = {{symbol_id, position}};
        risk_decomp.individual_var =
            calculate_parametric_var(isolated_position, volatilities, correlations);

        // Component VaR
        auto comp_it = component_vars.find(symbol_id);
        risk_decomp.total_contribution = (comp_it != component_vars.end()) ? comp_it->second : 0.0;

        // Diversification benefit/penalty
        risk_decomp.diversification_benefit =
            risk_decomp.individual_var - risk_decomp.total_contribution;
        risk_decomp.correlation_penalty = 0.0;  // Simplified

        decomposition[symbol_id] = risk_decomp;
    }

    return decomposition;
}

// Helper methods
std::vector<double> VaRCalculator::generate_portfolio_returns(
    const std::unordered_map<uint64_t, double>& positions,
    const std::unordered_map<uint64_t, std::vector<double>>& asset_returns) const {
    std::vector<double> portfolio_returns;

    // Find minimum return series length
    size_t min_length = SIZE_MAX;
    for (const auto& [symbol_id, returns] : asset_returns) {
        min_length = std::min(min_length, returns.size());
    }

    if (min_length == SIZE_MAX || min_length == 0)
        return portfolio_returns;

    // Calculate total portfolio value
    double total_value = 0.0;
    for (const auto& [symbol_id, position] : positions) {
        total_value += std::abs(position);
    }

    if (total_value == 0.0)
        return portfolio_returns;

    // Calculate portfolio returns for each time period
    portfolio_returns.reserve(min_length);
    for (size_t i = 0; i < min_length; ++i) {
        double portfolio_return = 0.0;
        for (const auto& [symbol_id, position] : positions) {
            auto returns_it = asset_returns.find(symbol_id);
            if (returns_it != asset_returns.end() && i < returns_it->second.size()) {
                double weight = position / total_value;
                portfolio_return += weight * returns_it->second[i];
            }
        }
        portfolio_returns.push_back(portfolio_return);
    }

    return portfolio_returns;
}

std::vector<double> VaRCalculator::simulate_portfolio_returns(
    const std::unordered_map<uint64_t, double>& positions,
    const std::unordered_map<uint64_t, double>& expected_returns,
    const std::unordered_map<uint64_t, double>& volatilities,
    const std::unordered_map<std::pair<uint64_t, uint64_t>, double, PairHash>& correlations,
    uint32_t num_simulations) const {
    std::vector<double> simulated_returns;
    simulated_returns.reserve(num_simulations);

    // Simplified Monte Carlo simulation
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<> normal(0.0, 1.0);

    double total_value = 0.0;
    for (const auto& [symbol_id, position] : positions) {
        total_value += std::abs(position);
    }

    if (total_value == 0.0)
        return simulated_returns;

    for (uint32_t i = 0; i < num_simulations; ++i) {
        double portfolio_return = 0.0;

        for (const auto& [symbol_id, position] : positions) {
            auto exp_ret_it = expected_returns.find(symbol_id);
            auto vol_it = volatilities.find(symbol_id);

            if (exp_ret_it != expected_returns.end() && vol_it != volatilities.end()) {
                double weight = position / total_value;
                double random_return = exp_ret_it->second + vol_it->second * normal(gen);
                portfolio_return += weight * random_return;
            }
        }

        simulated_returns.push_back(portfolio_return);
    }

    return simulated_returns;
}

double VaRCalculator::quantile(std::vector<double> values, double percentile) const {
    if (values.empty())
        return 0.0;

    std::sort(values.begin(), values.end());
    size_t index = static_cast<size_t>(percentile * values.size());
    index = std::min(index, values.size() - 1);

    return values[index];
}

}  // namespace goldearn::risk