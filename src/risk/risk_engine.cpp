#include "risk_engine.hpp"
#include "../utils/simple_logger.hpp"

namespace goldearn::risk {

RiskEngine::RiskEngine() 
    : initialized_(false)
    , monitoring_active_(false)
    , circuit_breaker_active_(false)
    , shutdown_requested_(false) {
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
    if (result != RiskCheckResult::APPROVED) return result;
    
    result = check_order_size_limits(context);
    if (result != RiskCheckResult::APPROVED) return result;
    
    result = check_price_limits(context);
    if (result != RiskCheckResult::APPROVED) return result;
    
    result = check_exposure_limits(context);
    if (result != RiskCheckResult::APPROVED) return result;
    
    result = check_var_limits(context);
    if (result != RiskCheckResult::APPROVED) return result;
    
    result = check_rate_limits(context);
    if (result != RiskCheckResult::APPROVED) return result;
    
    result = check_blacklists(context);
    if (result != RiskCheckResult::APPROVED) return result;
    
    result = check_circuit_breakers(context);
    if (result != RiskCheckResult::APPROVED) return result;
    
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
        auto violation_time = std::chrono::high_resolution_clock::time_point(violation.timestamp);
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

void RiskEngine::add_strategy_to_blacklist(const std::string& strategy_id, const std::string& reason) {
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
    // Simplified VaR calculation
    // In a real system, this would use historical data and proper statistical methods
    return 100000.0; // Placeholder
}

double RiskEngine::calculate_strategy_var(const std::string& strategy_id, uint32_t days) const {
    // Simplified strategy VaR calculation
    return 50000.0; // Placeholder
}

double RiskEngine::calculate_correlation_risk() const {
    // Simplified correlation risk calculation
    return 0.1; // Placeholder
}

double RiskEngine::calculate_concentration_risk() const {
    // Simplified concentration risk calculation
    return 0.05; // Placeholder
}

void RiskEngine::set_position_manager(std::shared_ptr<trading::PositionManager> pos_mgr) {
    position_manager_ = pos_mgr;
}

double RiskEngine::get_current_exposure() const {
    // Simplified exposure calculation
    return 5000000.0; // Placeholder
}

double RiskEngine::get_available_buying_power() const {
    // Simplified buying power calculation
    return 10000000.0; // Placeholder
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
    report.report_time = std::chrono::duration_cast<market_data::Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    
    return report;
}

// Private methods implementation
RiskCheckResult RiskEngine::check_position_limits(const PreTradeContext& context) {
    // Simplified position limit check
    return RiskCheckResult::APPROVED;
}

RiskCheckResult RiskEngine::check_order_size_limits(const PreTradeContext& context) {
    if (!context.order) return RiskCheckResult::APPROVED;
    
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
    if (!context.order) return RiskCheckResult::APPROVED;
    
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
    
    violations_.erase(
        std::remove_if(violations_.begin(), violations_.end(),
                      [cutoff_time](const RiskViolation& v) {
                          auto violation_time = std::chrono::high_resolution_clock::time_point(v.timestamp);
                          return violation_time < cutoff_time;
                      }),
        violations_.end()
    );
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

} // namespace goldearn::risk