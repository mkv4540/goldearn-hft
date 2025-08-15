#include <gtest/gtest.h>
#include "../src/risk/risk_engine.hpp"
#include "../src/risk/advanced_position_tracker.hpp"
#include "../src/trading/trading_engine.hpp"
#include "../src/utils/simple_logger.hpp"

using namespace goldearn::risk;
using namespace goldearn::trading;

class EnhancedRiskManagementTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize risk engine
        risk_engine_ = std::make_unique<goldearn::risk::RiskEngine>();
        ASSERT_TRUE(risk_engine_->initialize());
        
        // Initialize position tracker
        position_tracker_ = std::make_unique<AdvancedPositionTracker>();
        ASSERT_TRUE(position_tracker_->initialize());
        
        // Set up test limits
        RiskLimits limits;
        limits.max_position_size = 1000000.0;
        limits.max_portfolio_exposure = 10000000.0;
        limits.max_order_value = 500000.0;
        limits.max_daily_loss = -100000.0;
        risk_engine_->set_risk_limits(limits);
        
        // Set up position limits
        std::unordered_map<std::string, double> position_limits;
        position_limits["RELIANCE"] = 2000000.0;
        position_limits["TCS"] = 1500000.0;
        position_tracker_->set_position_limits(position_limits);
        
        position_tracker_->set_portfolio_limits(5000000.0, 200000.0); // 5M exposure, 200K VaR
    }
    
    void TearDown() override {
        if (position_tracker_) {
            position_tracker_->shutdown();
        }
        if (risk_engine_) {
            risk_engine_->shutdown();
        }
    }
    
    std::unique_ptr<goldearn::risk::RiskEngine> risk_engine_;
    std::unique_ptr<AdvancedPositionTracker> position_tracker_;
};

TEST_F(EnhancedRiskManagementTest, BasicRiskChecks) {
    // Test approved order
    Order approved_order;
    approved_order.symbol_id = 1001;
    // approved_order.symbol = "RELIANCE"; // Not available in Order struct
    approved_order.quantity = 100;
    approved_order.price = 2500.0;
    approved_order.side = OrderSide::BUY;
    approved_order.strategy_id = "market_making";
    
    auto result = risk_engine_->quick_pre_trade_check(approved_order);
    EXPECT_EQ(result, RiskCheckResult::APPROVED);
    
    // Test rejected order (too large)
    Order rejected_order;
    rejected_order.symbol_id = 1001;
    // rejected_order.symbol = "RELIANCE"; // Not available in Order struct
    rejected_order.quantity = 1000;
    rejected_order.price = 5000.0; // 5M order value
    rejected_order.side = OrderSide::BUY;
    rejected_order.strategy_id = "market_making";
    
    result = risk_engine_->quick_pre_trade_check(rejected_order);
    EXPECT_EQ(result, RiskCheckResult::REJECTED_ORDER_SIZE);
}

TEST_F(EnhancedRiskManagementTest, AdvancedVarCalculation) {
    // Test portfolio VaR calculation
    double portfolio_var_1d = risk_engine_->calculate_portfolio_var(1);
    EXPECT_GT(portfolio_var_1d, 0.0);
    
    double portfolio_var_10d = risk_engine_->calculate_portfolio_var(10);
    EXPECT_GT(portfolio_var_10d, portfolio_var_1d); // 10-day VaR should be higher
    
    // Test strategy-specific VaR
    double mm_var = risk_engine_->calculate_strategy_var("market_making", 1);
    double arb_var = risk_engine_->calculate_strategy_var("arbitrage", 1);
    
    EXPECT_GT(mm_var, 0.0);
    EXPECT_GT(arb_var, 0.0);
    EXPECT_LT(arb_var, mm_var); // Arbitrage should have lower VaR
}

TEST_F(EnhancedRiskManagementTest, PositionTracking) {
    // Create test fill
    Fill fill;
    fill.symbol_id = 1001;
    fill.symbol = "RELIANCE";
    fill.quantity = 100;
    fill.price = 2500.0;
    fill.side = OrderSide::BUY;
    fill.strategy_id = "market_making";
    fill.fill_time = std::chrono::duration_cast<goldearn::market_data::Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    
    // Update position
    position_tracker_->update_position(fill);
    
    // Check position was created
    auto position = position_tracker_->get_position(1001);
    EXPECT_EQ(position.symbol_id, 1001);
    EXPECT_EQ(position.quantity, 100);
    EXPECT_EQ(position.avg_cost, 2500.0);
    EXPECT_EQ(position.strategy_id, "market_making");
    
    // Test market data update
    position_tracker_->update_market_data(1001, 2600.0);
    position = position_tracker_->get_position(1001);
    EXPECT_EQ(position.current_price, 2600.0);
    EXPECT_GT(position.unrealized_pnl, 0.0); // Should have positive P&L
    
    // Test portfolio metrics
    auto portfolio_metrics = position_tracker_->get_portfolio_metrics();
    EXPECT_GT(portfolio_metrics.total_long_exposure, 0.0);
    EXPECT_EQ(portfolio_metrics.total_short_exposure, 0.0);
    EXPECT_GT(portfolio_metrics.total_unrealized_pnl, 0.0);
}

TEST_F(EnhancedRiskManagementTest, MultiplePositionsAndStrategies) {
    // Create fills for different strategies and symbols
    std::vector<Fill> fills = {
        {1001, "RELIANCE", 100, 2500.0, OrderSide::BUY, "market_making"},
        {1002, "TCS", 50, 3200.0, OrderSide::BUY, "arbitrage"},
        {1003, "INFY", 200, 1800.0, OrderSide::SELL, "momentum"}
    };
    
    for (auto& fill : fills) {
        fill.fill_time = std::chrono::duration_cast<goldearn::market_data::Timestamp>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        );
        position_tracker_->update_position(fill);
    }
    
    // Check all positions created
    auto all_positions = position_tracker_->get_all_positions();
    EXPECT_EQ(all_positions.size(), 3);
    
    // Check strategy positions
    auto mm_positions = position_tracker_->get_strategy_positions("market_making");
    EXPECT_EQ(mm_positions.size(), 1);
    EXPECT_EQ(mm_positions[0].symbol, "RELIANCE");
    
    auto arb_positions = position_tracker_->get_strategy_positions("arbitrage");
    EXPECT_EQ(arb_positions.size(), 1);
    EXPECT_EQ(arb_positions[0].symbol, "TCS");
    
    // Check portfolio metrics
    auto portfolio_metrics = position_tracker_->get_portfolio_metrics();
    EXPECT_GT(portfolio_metrics.total_long_exposure, 0.0);
    EXPECT_GT(portfolio_metrics.total_short_exposure, 0.0);
    EXPECT_GT(portfolio_metrics.gross_exposure, 0.0);
    
    // Check strategy metrics
    auto mm_metrics = position_tracker_->get_strategy_metrics("market_making");
    EXPECT_EQ(mm_metrics.strategy_id, "market_making");
    EXPECT_EQ(mm_metrics.active_positions, 1);
    EXPECT_GT(mm_metrics.strategy_exposure, 0.0);
}

TEST_F(EnhancedRiskManagementTest, RiskLimitViolations) {
    // Create a large position that should violate limits
    Fill large_fill;
    large_fill.symbol_id = 1001;
    large_fill.symbol = "RELIANCE";
    large_fill.quantity = 1000;
    large_fill.price = 2500.0; // 2.5M position
    large_fill.side = OrderSide::BUY;
    large_fill.strategy_id = "market_making";
    large_fill.fill_time = std::chrono::duration_cast<goldearn::market_data::Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    
    position_tracker_->update_position(large_fill);
    
    // Check for violations
    auto violations = position_tracker_->check_limit_violations();
    EXPECT_GT(violations.size(), 0); // Should have at least one violation
    
    // Check that violation message contains relevant information
    bool found_position_violation = false;
    for (const auto& violation : violations) {
        if (violation.find("RELIANCE") != std::string::npos) {
            found_position_violation = true;
            break;
        }
    }
    EXPECT_TRUE(found_position_violation);
}

TEST_F(EnhancedRiskManagementTest, VarCalculationAccuracy) {
    // Create a diversified portfolio
    std::vector<Fill> fills = {
        {1001, "RELIANCE", 100, 2500.0, OrderSide::BUY, "market_making"},
        {1002, "TCS", 200, 3200.0, OrderSide::BUY, "market_making"},
        {1003, "INFY", 300, 1800.0, OrderSide::BUY, "arbitrage"},
        {1004, "WIPRO", 150, 400.0, OrderSide::BUY, "arbitrage"},
        {1005, "HDFCBANK", 80, 1600.0, OrderSide::BUY, "momentum"}
    };
    
    for (auto& fill : fills) {
        fill.fill_time = std::chrono::duration_cast<goldearn::market_data::Timestamp>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        );
        position_tracker_->update_position(fill);
    }
    
    // Calculate portfolio VaR
    double portfolio_var = position_tracker_->calculate_portfolio_var(1, 0.95);
    EXPECT_GT(portfolio_var, 0.0);
    
    // Portfolio VaR should be reasonable relative to gross exposure
    auto metrics = position_tracker_->get_portfolio_metrics();
    double var_percentage = portfolio_var / metrics.gross_exposure;
    EXPECT_GT(var_percentage, 0.01); // At least 1%
    EXPECT_LT(var_percentage, 0.10); // Less than 10%
    
    // Test strategy-specific VaR
    double mm_var = position_tracker_->calculate_strategy_var("market_making", 1);
    double arb_var = position_tracker_->calculate_strategy_var("arbitrage", 1);
    double mom_var = position_tracker_->calculate_strategy_var("momentum", 1);
    
    EXPECT_GT(mm_var, 0.0);
    EXPECT_GT(arb_var, 0.0);
    EXPECT_GT(mom_var, 0.0);
    
    // Arbitrage should have lowest VaR due to lower volatility
    EXPECT_LT(arb_var, mm_var);
    EXPECT_LT(arb_var, mom_var);
}

TEST_F(EnhancedRiskManagementTest, CircuitBreakerFunctionality) {
    // Test circuit breaker triggering
    EXPECT_FALSE(risk_engine_->is_circuit_breaker_active());
    
    risk_engine_->trigger_circuit_breaker("Test emergency stop");
    EXPECT_TRUE(risk_engine_->is_circuit_breaker_active());
    
    // Orders should be rejected when circuit breaker is active
    Order test_order;
    test_order.symbol_id = 1001;
    test_order.quantity = 100;
    test_order.price = 2500.0;
    test_order.side = OrderSide::BUY;
    
    auto result = risk_engine_->quick_pre_trade_check(test_order);
    EXPECT_EQ(result, RiskCheckResult::REJECTED_CIRCUIT_BREAKER);
    
    // Reset circuit breaker
    risk_engine_->reset_circuit_breaker();
    EXPECT_FALSE(risk_engine_->is_circuit_breaker_active());
    
    // Orders should be approved again
    result = risk_engine_->quick_pre_trade_check(test_order);
    EXPECT_EQ(result, RiskCheckResult::APPROVED);
}

TEST_F(EnhancedRiskManagementTest, BlacklistFunctionality) {
    // Add symbol to blacklist
    risk_engine_->add_symbol_to_blacklist(1001, "Regulatory concerns");
    EXPECT_TRUE(risk_engine_->is_symbol_blacklisted(1001));
    
    // Orders for blacklisted symbol should be rejected
    Order blacklisted_order;
    blacklisted_order.symbol_id = 1001;
    blacklisted_order.quantity = 100;
    blacklisted_order.price = 2500.0;
    blacklisted_order.side = OrderSide::BUY;
    
    auto result = risk_engine_->quick_pre_trade_check(blacklisted_order);
    EXPECT_EQ(result, RiskCheckResult::REJECTED_BLACKLIST);
    
    // Remove from blacklist
    risk_engine_->remove_symbol_from_blacklist(1001);
    EXPECT_FALSE(risk_engine_->is_symbol_blacklisted(1001));
    
    // Order should be approved now
    result = risk_engine_->quick_pre_trade_check(blacklisted_order);
    EXPECT_EQ(result, RiskCheckResult::APPROVED);
}

TEST_F(EnhancedRiskManagementTest, PerformanceMetrics) {
    // Run multiple risk checks to gather statistics
    Order test_order;
    test_order.symbol_id = 1001;
    test_order.quantity = 100;
    test_order.price = 2500.0;
    test_order.side = OrderSide::BUY;
    test_order.strategy_id = "market_making";
    
    const int num_checks = 1000;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_checks; ++i) {
        risk_engine_->quick_pre_trade_check(test_order);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
    auto avg_latency = total_duration.count() / num_checks;
    
    // Check that average latency is reasonable (should be < 1 microsecond)
    EXPECT_LT(avg_latency, 1000); // Less than 1000 nanoseconds
    
    // Check statistics
    auto stats = risk_engine_->get_statistics();
    EXPECT_EQ(stats.total_checks_performed, num_checks);
    EXPECT_EQ(stats.checks_approved, num_checks);
    EXPECT_EQ(stats.checks_rejected, 0);
    
    LOG_INFO("Risk check performance: {} checks, avg latency: {} ns", 
             num_checks, avg_latency);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}