#include "advanced_position_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "../utils/simple_logger.hpp"

namespace goldearn::risk {

AdvancedPositionTracker::AdvancedPositionTracker()
    : tracking_active_(false),
      shutdown_requested_(false),
      max_gross_exposure_(10000000.0)  // 10M default
      ,
      max_portfolio_var_(500000.0)  // 500K default
{
    LOG_INFO("AdvancedPositionTracker: Initializing position tracker");
    portfolio_metrics_ = PortfolioMetrics{};
}

AdvancedPositionTracker::~AdvancedPositionTracker() {
    LOG_INFO("AdvancedPositionTracker: Shutting down position tracker");
    shutdown();
}

bool AdvancedPositionTracker::initialize() {
    LOG_INFO("AdvancedPositionTracker: Initialization complete");
    return true;
}

void AdvancedPositionTracker::shutdown() {
    stop_performance_tracking();
    LOG_INFO("AdvancedPositionTracker: Shutdown complete");
}

void AdvancedPositionTracker::update_position(const Fill& fill) {
    std::unique_lock<std::shared_mutex> lock(positions_mutex_);

    auto it = positions_.find(fill.symbol_id);
    if (it == positions_.end()) {
        // New position
        PositionInfo position{};
        position.symbol_id = fill.symbol_id;
        position.symbol = fill.symbol;
        position.quantity = (fill.side == trading::OrderSide::BUY) ? fill.quantity : -fill.quantity;
        position.avg_cost = fill.price;
        position.current_price = fill.price;
        position.strategy_id = fill.strategy_id;
        position.last_update = std::chrono::duration_cast<market_data::Timestamp>(
            std::chrono::high_resolution_clock::now().time_since_epoch());

        calculate_position_risk_metrics(position);
        positions_[fill.symbol_id] = position;

        LOG_DEBUG("AdvancedPositionTracker: New position - Symbol: {}, Qty: {}, Price: {}",
                  fill.symbol,
                  position.quantity,
                  fill.price);
    } else {
        // Update existing position
        auto& position = it->second;
        double old_quantity = position.quantity;
        double fill_qty = (fill.side == trading::OrderSide::BUY) ? fill.quantity : -fill.quantity;

        if (std::copysign(1.0, old_quantity) == std::copysign(1.0, fill_qty)) {
            // Same direction - update average cost
            double total_cost = (old_quantity * position.avg_cost) + (fill_qty * fill.price);
            position.quantity = old_quantity + fill_qty;
            if (position.quantity != 0) {
                position.avg_cost = total_cost / position.quantity;
            }
        } else {
            // Opposite direction - realize P&L
            double qty_to_realize = std::min(std::abs(old_quantity), std::abs(fill_qty));
            double realized_pnl = qty_to_realize * (fill.price - position.avg_cost) *
                                  std::copysign(1.0, old_quantity);
            position.realized_pnl += realized_pnl;
            position.quantity = old_quantity + fill_qty;
        }

        position.current_price = fill.price;
        position.last_update = std::chrono::duration_cast<market_data::Timestamp>(
            std::chrono::high_resolution_clock::now().time_since_epoch());

        calculate_position_risk_metrics(position);

        LOG_DEBUG("AdvancedPositionTracker: Updated position - Symbol: {}, New Qty: {}, Price: {}",
                  fill.symbol,
                  position.quantity,
                  fill.price);
    }

    // Update strategy metrics
    update_strategy_metrics(fill.strategy_id);
    update_portfolio_metrics();
}

void AdvancedPositionTracker::update_market_data(uint64_t symbol_id, double price) {
    {
        std::unique_lock<std::shared_mutex> price_lock(price_mutex_);
        current_prices_[symbol_id] = price;
    }

    std::unique_lock<std::shared_mutex> pos_lock(positions_mutex_);
    auto it = positions_.find(symbol_id);
    if (it != positions_.end()) {
        it->second.current_price = price;
        it->second.unrealized_pnl = it->second.quantity * (price - it->second.avg_cost);
        it->second.last_update = std::chrono::duration_cast<market_data::Timestamp>(
            std::chrono::high_resolution_clock::now().time_since_epoch());

        calculate_position_risk_metrics(it->second);
    }
}

void AdvancedPositionTracker::mark_to_market() {
    std::unique_lock<std::shared_mutex> lock(positions_mutex_);

    for (auto& [symbol_id, position] : positions_) {
        position.unrealized_pnl = position.quantity * (position.current_price - position.avg_cost);
        calculate_position_risk_metrics(position);
    }

    update_portfolio_metrics();
    LOG_DEBUG("AdvancedPositionTracker: Mark-to-market update complete");
}

PositionInfo AdvancedPositionTracker::get_position(uint64_t symbol_id) const {
    std::shared_lock<std::shared_mutex> lock(positions_mutex_);

    auto it = positions_.find(symbol_id);
    if (it != positions_.end()) {
        return it->second;
    }

    return PositionInfo{};  // Empty position if not found
}

std::vector<PositionInfo> AdvancedPositionTracker::get_all_positions() const {
    std::shared_lock<std::shared_mutex> lock(positions_mutex_);

    std::vector<PositionInfo> positions;
    positions.reserve(positions_.size());

    for (const auto& [symbol_id, position] : positions_) {
        if (position.quantity != 0.0) {
            positions.push_back(position);
        }
    }

    return positions;
}

std::vector<PositionInfo> AdvancedPositionTracker::get_strategy_positions(
    const std::string& strategy_id) const {
    std::shared_lock<std::shared_mutex> lock(positions_mutex_);

    std::vector<PositionInfo> positions;
    for (const auto& [symbol_id, position] : positions_) {
        if (position.strategy_id == strategy_id && position.quantity != 0.0) {
            positions.push_back(position);
        }
    }

    return positions;
}

PortfolioMetrics AdvancedPositionTracker::get_portfolio_metrics() const {
    std::lock_guard<std::mutex> lock(portfolio_mutex_);
    return portfolio_metrics_;
}

StrategyMetrics AdvancedPositionTracker::get_strategy_metrics(
    const std::string& strategy_id) const {
    std::shared_lock<std::shared_mutex> lock(strategy_mutex_);

    auto it = strategy_metrics_.find(strategy_id);
    if (it != strategy_metrics_.end()) {
        return it->second;
    }

    return StrategyMetrics{};  // Empty if not found
}

double AdvancedPositionTracker::calculate_portfolio_var(uint32_t days, double confidence) const {
    std::shared_lock<std::shared_mutex> lock(positions_mutex_);

    if (positions_.empty()) {
        return 0.0;
    }

    double portfolio_value = 0.0;
    double portfolio_variance = 0.0;

    // Calculate portfolio value and variance
    for (const auto& [symbol_id, position] : positions_) {
        if (position.quantity == 0.0)
            continue;

        double position_value = std::abs(position.market_value());
        portfolio_value += position_value;

        // Individual position variance
        double volatility = get_symbol_volatility(symbol_id);
        double position_variance = std::pow(position_value * volatility, 2);
        portfolio_variance += position_variance;
    }

    // Add correlation effects (simplified)
    double correlation_adjustment = 1.2;  // Assume some positive correlation
    portfolio_variance *= correlation_adjustment;

    double portfolio_std = std::sqrt(portfolio_variance);
    double z_score = (confidence == 0.95) ? 1.645 : (confidence == 0.99) ? 2.326 : 1.645;

    double time_factor = std::sqrt(static_cast<double>(days));
    double var = portfolio_std * z_score * time_factor;

    LOG_DEBUG(
        "AdvancedPositionTracker: Portfolio VaR ({} days, {}%): {}", days, confidence * 100, var);

    return var;
}

double AdvancedPositionTracker::calculate_strategy_var(const std::string& strategy_id,
                                                       uint32_t days) const {
    std::shared_lock<std::shared_mutex> lock(positions_mutex_);

    double strategy_variance = 0.0;

    for (const auto& [symbol_id, position] : positions_) {
        if (position.strategy_id != strategy_id || position.quantity == 0.0)
            continue;

        double position_value = std::abs(position.market_value());
        double volatility = get_symbol_volatility(symbol_id);
        double position_variance = std::pow(position_value * volatility, 2);
        strategy_variance += position_variance;
    }

    double strategy_std = std::sqrt(strategy_variance);
    double z_score = 1.645;  // 95% confidence
    double time_factor = std::sqrt(static_cast<double>(days));

    return strategy_std * z_score * time_factor;
}

void AdvancedPositionTracker::start_performance_tracking() {
    if (tracking_active_.load()) {
        LOG_WARN("AdvancedPositionTracker: Performance tracking already active");
        return;
    }

    shutdown_requested_ = false;
    tracking_thread_ = std::thread(&AdvancedPositionTracker::performance_tracking_worker, this);
    tracking_active_ = true;
    LOG_INFO("AdvancedPositionTracker: Performance tracking started");
}

void AdvancedPositionTracker::stop_performance_tracking() {
    if (!tracking_active_.load()) {
        return;
    }

    shutdown_requested_ = true;
    if (tracking_thread_.joinable()) {
        tracking_thread_.join();
    }
    tracking_active_ = false;
    LOG_INFO("AdvancedPositionTracker: Performance tracking stopped");
}

void AdvancedPositionTracker::set_position_limits(
    const std::unordered_map<std::string, double>& limits) {
    std::lock_guard<std::mutex> lock(limits_mutex_);
    position_limits_ = limits;
    LOG_INFO("AdvancedPositionTracker: Position limits updated");
}

void AdvancedPositionTracker::set_portfolio_limits(double max_gross_exposure, double max_var) {
    std::lock_guard<std::mutex> lock(limits_mutex_);
    max_gross_exposure_ = max_gross_exposure;
    max_portfolio_var_ = max_var;
    LOG_INFO("AdvancedPositionTracker: Portfolio limits updated - Exposure: {}, VaR: {}",
             max_gross_exposure,
             max_var);
}

std::vector<std::string> AdvancedPositionTracker::check_limit_violations() const {
    std::vector<std::string> violations;

    auto portfolio_metrics = get_portfolio_metrics();

    // Check gross exposure limit
    if (portfolio_metrics.gross_exposure > max_gross_exposure_) {
        violations.push_back(
            "Gross exposure limit exceeded: " + std::to_string(portfolio_metrics.gross_exposure) +
            " > " + std::to_string(max_gross_exposure_));
    }

    // Check VaR limit
    if (portfolio_metrics.portfolio_var_1d > max_portfolio_var_) {
        violations.push_back(
            "Portfolio VaR limit exceeded: " + std::to_string(portfolio_metrics.portfolio_var_1d) +
            " > " + std::to_string(max_portfolio_var_));
    }

    // Check individual position limits
    std::shared_lock<std::shared_mutex> pos_lock(positions_mutex_);
    std::lock_guard<std::mutex> limits_lock(limits_mutex_);

    for (const auto& [symbol_id, position] : positions_) {
        auto limit_it = position_limits_.find(position.symbol);
        if (limit_it != position_limits_.end()) {
            double notional_exposure = position.notional_exposure();
            if (notional_exposure > limit_it->second) {
                violations.push_back("Position limit exceeded for " + position.symbol + ": " +
                                     std::to_string(notional_exposure) + " > " +
                                     std::to_string(limit_it->second));
            }
        }
    }

    return violations;
}

// Private methods
void AdvancedPositionTracker::update_portfolio_metrics() {
    std::lock_guard<std::mutex> portfolio_lock(portfolio_mutex_);

    portfolio_metrics_.total_long_exposure = 0.0;
    portfolio_metrics_.total_short_exposure = 0.0;
    portfolio_metrics_.total_unrealized_pnl = 0.0;
    portfolio_metrics_.total_realized_pnl = 0.0;

    for (const auto& [symbol_id, position] : positions_) {
        double market_value = position.market_value();

        if (position.quantity > 0) {
            portfolio_metrics_.total_long_exposure += market_value;
        } else if (position.quantity < 0) {
            portfolio_metrics_.total_short_exposure += std::abs(market_value);
        }

        portfolio_metrics_.total_unrealized_pnl += position.unrealized_pnl;
        portfolio_metrics_.total_realized_pnl += position.realized_pnl;
    }

    portfolio_metrics_.net_exposure =
        portfolio_metrics_.total_long_exposure - portfolio_metrics_.total_short_exposure;
    portfolio_metrics_.gross_exposure =
        portfolio_metrics_.total_long_exposure + portfolio_metrics_.total_short_exposure;

    portfolio_metrics_.portfolio_var_1d = calculate_portfolio_var(1, 0.95);
    portfolio_metrics_.last_update = std::chrono::duration_cast<market_data::Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch());
}

void AdvancedPositionTracker::update_strategy_metrics(const std::string& strategy_id) {
    std::unique_lock<std::shared_mutex> lock(strategy_mutex_);

    auto& metrics = strategy_metrics_[strategy_id];
    metrics.strategy_id = strategy_id;
    metrics.strategy_pnl = 0.0;
    metrics.strategy_exposure = 0.0;
    metrics.active_positions = 0;

    std::shared_lock<std::shared_mutex> pos_lock(positions_mutex_);
    for (const auto& [symbol_id, position] : positions_) {
        if (position.strategy_id == strategy_id) {
            metrics.strategy_pnl += (position.unrealized_pnl + position.realized_pnl);
            metrics.strategy_exposure += position.notional_exposure();
            if (position.quantity != 0.0) {
                metrics.active_positions++;
            }
        }
    }

    metrics.strategy_var = calculate_strategy_var(strategy_id, 1);
    metrics.last_trade = std::chrono::duration_cast<market_data::Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch());
}

void AdvancedPositionTracker::calculate_position_risk_metrics(PositionInfo& position) {
    position.position_volatility = get_symbol_volatility(position.symbol_id);
    position.beta = get_symbol_beta(position.symbol_id);
    position.sector = get_symbol_sector(position.symbol_id);

    double position_value = std::abs(position.market_value());
    position.position_var_1d =
        position_value * position.position_volatility * 1.645;  // 95% confidence
}

void AdvancedPositionTracker::performance_tracking_worker() {
    while (!shutdown_requested_.load()) {
        mark_to_market();
        update_portfolio_metrics();

        // Update all strategy metrics
        std::set<std::string> strategies;
        {
            std::shared_lock<std::shared_mutex> lock(positions_mutex_);
            for (const auto& [symbol_id, position] : positions_) {
                if (!position.strategy_id.empty()) {
                    strategies.insert(position.strategy_id);
                }
            }
        }

        for (const auto& strategy_id : strategies) {
            update_strategy_metrics(strategy_id);
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));  // Update every 5 seconds
    }
}

// Helper methods (simplified implementations)
double AdvancedPositionTracker::get_symbol_volatility(uint64_t symbol_id) const {
    // Simplified volatility lookup
    return 0.02;  // 2% daily volatility default
}

double AdvancedPositionTracker::get_symbol_beta(uint64_t symbol_id) const {
    // Simplified beta lookup
    return 1.0;  // Market beta default
}

std::string AdvancedPositionTracker::get_symbol_sector(uint64_t symbol_id) const {
    // Simplified sector lookup
    return "Technology";  // Default sector
}

std::vector<StrategyMetrics> AdvancedPositionTracker::get_all_strategy_metrics() const {
    std::shared_lock<std::shared_mutex> lock(strategy_mutex_);

    std::vector<StrategyMetrics> all_metrics;
    all_metrics.reserve(strategy_metrics_.size());

    for (const auto& [strategy_id, metrics] : strategy_metrics_) {
        all_metrics.push_back(metrics);
    }

    return all_metrics;
}

double AdvancedPositionTracker::calculate_correlation_matrix() const {
    // Simplified correlation calculation - return average correlation
    return 0.3;  // 30% average correlation between assets
}

double AdvancedPositionTracker::calculate_concentration_risk() const {
    std::shared_lock<std::shared_mutex> lock(positions_mutex_);

    if (positions_.empty())
        return 0.0;

    double total_exposure = 0.0;
    double max_position_exposure = 0.0;

    for (const auto& [symbol_id, position] : positions_) {
        double exposure = position.notional_exposure();
        total_exposure += exposure;
        max_position_exposure = std::max(max_position_exposure, exposure);
    }

    if (total_exposure == 0.0)
        return 0.0;

    // Concentration risk as percentage of largest position
    return max_position_exposure / total_exposure;
}

double AdvancedPositionTracker::stress_test_portfolio(
    const std::unordered_map<uint64_t, double>& shock_scenarios) const {
    std::shared_lock<std::shared_mutex> lock(positions_mutex_);

    double total_stress_loss = 0.0;

    for (const auto& [symbol_id, position] : positions_) {
        auto shock_it = shock_scenarios.find(symbol_id);
        if (shock_it != shock_scenarios.end()) {
            // Apply shock to position
            double position_value = position.market_value();
            double stress_loss = position_value * shock_it->second;  // Shock as percentage
            total_stress_loss += stress_loss;
        }
    }

    return total_stress_loss;
}

std::vector<std::pair<std::string, double>> AdvancedPositionTracker::scenario_analysis() const {
    std::vector<std::pair<std::string, double>> scenarios;

    // Define standard stress scenarios
    std::unordered_map<uint64_t, double> market_crash_scenario;
    std::unordered_map<uint64_t, double> sector_rotation_scenario;
    std::unordered_map<uint64_t, double> volatility_spike_scenario;

    // Market crash: -20% across all positions
    {
        std::shared_lock<std::shared_mutex> lock(positions_mutex_);
        for (const auto& [symbol_id, position] : positions_) {
            market_crash_scenario[symbol_id] = -0.20;      // -20%
            sector_rotation_scenario[symbol_id] = -0.10;   // -10%
            volatility_spike_scenario[symbol_id] = -0.05;  // -5%
        }
    }

    scenarios.emplace_back("Market Crash (-20%)", stress_test_portfolio(market_crash_scenario));
    scenarios.emplace_back("Sector Rotation (-10%)",
                           stress_test_portfolio(sector_rotation_scenario));
    scenarios.emplace_back("Volatility Spike (-5%)",
                           stress_test_portfolio(volatility_spike_scenario));

    return scenarios;
}

double AdvancedPositionTracker::calculate_position_var(const PositionInfo& position,
                                                       uint32_t days) const {
    double position_value = std::abs(position.market_value());
    double volatility = position.position_volatility;
    double z_score = 1.645;  // 95% confidence
    double time_factor = std::sqrt(static_cast<double>(days));

    return position_value * volatility * z_score * time_factor;
}

double AdvancedPositionTracker::calculate_correlation(uint64_t symbol1, uint64_t symbol2) const {
    // Simplified correlation calculation
    if (symbol1 == symbol2)
        return 1.0;

    // Default correlation based on sector
    std::string sector1 = get_symbol_sector(symbol1);
    std::string sector2 = get_symbol_sector(symbol2);

    if (sector1 == sector2) {
        return 0.6;  // High correlation within same sector
    } else {
        return 0.3;  // Moderate correlation across sectors
    }
}

}  // namespace goldearn::risk