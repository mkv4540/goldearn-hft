# GoldEarn HFT - Risk Management System

## 🛡️ **Risk Management Overview**

### **Core Objectives**
- **Real-time risk monitoring**: <10μs pre-trade checks
- **Comprehensive coverage**: Market, credit, operational, regulatory risks
- **Automated controls**: Circuit breakers and kill switches
- **Regulatory compliance**: SEBI risk management guidelines
- **Capital preservation**: Protect firm and client assets

### **Risk Management Framework**
```
┌─────────────────────────────────────────────────────────────────┐
│                   RISK MANAGEMENT SYSTEM                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐│
│  │   Pre-Trade     │  │  Post-Trade     │  │   Portfolio     ││
│  │     Risk        │  │     Risk        │  │     Risk        ││
│  │  • Limits Check │  │  • P&L Monitor  │  │  • VaR Calc     ││
│  │  • Margin Calc  │  │  • Exposure     │  │  • Stress Test  ││
│  │  • Validation   │  │  • Alerts       │  │  • Correlation  ││
│  └─────────────────┘  └─────────────────┘  └─────────────────┘│
│           │                    │                    │           │
│           └────────────────────┴────────────────────┘           │
│                               │                                 │
│                    ┌─────────────────┐                         │
│                    │  Risk Control   │                         │
│                    │     Engine      │                         │
│                    └─────────────────┘                         │
│                               │                                 │
│      ┌────────────────────────┴────────────────────────┐       │
│      │                                                  │       │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │       │
│  │   Action    │  │  Reporting  │  │ Compliance  │    │       │
│  │   Engine    │  │   Engine    │  │   Engine    │    │       │
│  │ • Reject    │  │ • Real-time │  │ • SEBI      │    │       │
│  │ • Alert     │  │ • Historical│  │ • Audit     │    │       │
│  │ • Liquidate │  │ • Analytics │  │ • Reports   │    │       │
│  └─────────────┘  └─────────────┘  └─────────────┘    │       │
└─────────────────────────────────────────────────────────────────┘
```

## 📊 **Risk Categories**

### **1. Market Risk**
**Definition**: Risk of losses due to market movements

**Metrics**:
- Value at Risk (VaR) - 95% and 99% confidence
- Expected Shortfall (ES/CVaR)
- Greeks (Delta, Gamma, Vega, Theta)
- Basis risk
- Correlation risk

**Controls**:
```cpp
struct MarketRiskLimits {
    double max_var_95;           // ₹50 lakhs
    double max_var_99;           // ₹100 lakhs
    double max_portfolio_delta;   // ₹500 lakhs
    double max_single_stock_var;  // ₹10 lakhs
    double max_sector_exposure;   // 30% of portfolio
    double max_correlation_risk;  // 0.7 correlation threshold
};
```

### **2. Credit Risk**
**Definition**: Risk of counterparty default

**Metrics**:
- Counterparty exposure
- Settlement risk
- Margin utilization
- Broker limits

**Controls**:
```cpp
struct CreditRiskLimits {
    std::map<CounterpartyId, double> counterparty_limits;
    double max_broker_exposure;      // ₹5 crores per broker
    double max_settlement_risk;      // ₹2 crores
    double margin_buffer;            // 20% above minimum
    int max_settlement_days;         // T+1
};
```

### **3. Liquidity Risk**
**Definition**: Risk of inability to exit positions

**Metrics**:
- Market depth analysis
- Bid-ask spreads
- Volume participation
- Time to liquidation

**Controls**:
```cpp
struct LiquidityRiskLimits {
    double max_position_vs_adv;      // 5% of Average Daily Volume
    double max_market_impact;        // 10 bps
    double min_market_depth;         // ₹10 lakhs on each side
    int max_liquidation_time;        // 300 seconds
    double liquidity_buffer;         // 2x normal spreads
};
```

### **4. Operational Risk**
**Definition**: Risk from failed processes, systems, or people

**Metrics**:
- System uptime
- Order error rate
- Settlement failures
- Technology incidents

**Controls**:
```cpp
struct OperationalRiskLimits {
    double max_order_error_rate;     // 0.01%
    double min_system_uptime;        // 99.95%
    int max_settlement_failures;     // 0 per month
    int max_technology_incidents;    // 2 per quarter
    double max_fat_finger_order;     // ₹1 crore
};
```

## 💻 **Risk Engine Implementation**

### **Pre-Trade Risk Checks**

```cpp
class PreTradeRiskEngine {
private:
    // Cached risk data for ultra-low latency
    struct alignas(64) RiskCache {
        std::atomic<double> portfolio_var;
        std::atomic<double> total_exposure;
        std::atomic<int64_t> net_position[MAX_SYMBOLS];
        std::atomic<double> utilized_margin;
    } cache_;
    
    MarketRiskLimits market_limits_;
    CreditRiskLimits credit_limits_;
    LiquidityRiskLimits liquidity_limits_;
    
public:
    // Target: <10 microseconds
    RiskDecision check_order(const Order& order) {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Parallel risk checks
        bool market_ok = check_market_risk(order);
        bool credit_ok = check_credit_risk(order);
        bool liquidity_ok = check_liquidity_risk(order);
        bool compliance_ok = check_compliance(order);
        
        // Fat finger check
        if (order.value() > operational_limits_.max_fat_finger_order) {
            return RiskDecision::REJECT_FAT_FINGER;
        }
        
        // Aggregate decision
        if (!market_ok) return RiskDecision::REJECT_MARKET_RISK;
        if (!credit_ok) return RiskDecision::REJECT_CREDIT_RISK;
        if (!liquidity_ok) return RiskDecision::REJECT_LIQUIDITY_RISK;
        if (!compliance_ok) return RiskDecision::REJECT_COMPLIANCE;
        
        // Update risk metrics (atomic)
        update_risk_cache(order);
        
        // Measure latency
        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        
        if (latency > 10) {
            LOG_WARNING("Pre-trade risk check took {}μs", latency);
        }
        
        return RiskDecision::APPROVE;
    }
    
private:
    bool check_market_risk(const Order& order) {
        // Calculate incremental VaR
        double incremental_var = calculate_incremental_var(order);
        double new_total_var = cache_.portfolio_var + incremental_var;
        
        if (new_total_var > market_limits_.max_var_95) {
            return false;
        }
        
        // Check position limits
        auto& position = cache_.net_position[order.symbol_id];
        int64_t new_position = position + 
            (order.side == Side::BUY ? order.quantity : -order.quantity);
        
        if (std::abs(new_position) > position_limits_[order.symbol_id]) {
            return false;
        }
        
        // Sector exposure check
        double sector_exposure = calculate_sector_exposure(order);
        if (sector_exposure > market_limits_.max_sector_exposure) {
            return false;
        }
        
        return true;
    }
};
```

### **Real-Time Risk Monitoring**

```cpp
class RealTimeRiskMonitor {
private:
    struct PortfolioState {
        double total_pnl;
        double unrealized_pnl;
        double realized_pnl;
        double current_var;
        double max_drawdown;
        std::chrono::time_point<std::chrono::high_resolution_clock> last_update;
    };
    
    std::atomic<PortfolioState> portfolio_state_;
    
public:
    void update_portfolio_risk() {
        // Calculate current portfolio metrics
        PortfolioState new_state;
        new_state.total_pnl = calculate_total_pnl();
        new_state.unrealized_pnl = calculate_unrealized_pnl();
        new_state.realized_pnl = calculate_realized_pnl();
        new_state.current_var = calculate_portfolio_var();
        new_state.max_drawdown = calculate_max_drawdown();
        new_state.last_update = std::chrono::high_resolution_clock::now();
        
        // Atomic update
        portfolio_state_.store(new_state);
        
        // Check for breaches
        check_risk_breaches(new_state);
    }
    
    void check_risk_breaches(const PortfolioState& state) {
        // Daily loss limit check
        if (state.total_pnl < -daily_loss_limit_) {
            trigger_circuit_breaker("DAILY_LOSS_LIMIT_BREACH");
        }
        
        // Drawdown check
        if (state.max_drawdown > max_drawdown_limit_) {
            trigger_circuit_breaker("MAX_DRAWDOWN_BREACH");
        }
        
        // VaR breach check
        if (state.current_var > var_limit_) {
            trigger_alert("VAR_LIMIT_WARNING", state.current_var);
        }
    }
};
```

### **Risk Analytics Engine**

```cpp
class RiskAnalyticsEngine {
private:
    // Historical data for risk calculations
    RollingWindow<PriceData> price_history_;
    CorrelationMatrix correlation_matrix_;
    
public:
    double calculate_var(double confidence_level) {
        // Historical VaR calculation
        std::vector<double> portfolio_returns = calculate_portfolio_returns();
        std::sort(portfolio_returns.begin(), portfolio_returns.end());
        
        size_t var_index = portfolio_returns.size() * (1 - confidence_level);
        return -portfolio_returns[var_index] * portfolio_value_;
    }
    
    double calculate_expected_shortfall(double confidence_level) {
        // CVaR/ES calculation
        std::vector<double> portfolio_returns = calculate_portfolio_returns();
        std::sort(portfolio_returns.begin(), portfolio_returns.end());
        
        size_t var_index = portfolio_returns.size() * (1 - confidence_level);
        double sum = 0.0;
        
        for (size_t i = 0; i < var_index; ++i) {
            sum += portfolio_returns[i];
        }
        
        return -sum / var_index * portfolio_value_;
    }
    
    StressTestResults perform_stress_test() {
        StressTestResults results;
        
        // Market crash scenario
        results.market_crash = simulate_scenario({
            {"NIFTY", -0.20},  // 20% crash
            {"BANKNIFTY", -0.25},  // 25% crash
            {"volatility", 2.0}  // Volatility doubles
        });
        
        // Liquidity crisis scenario
        results.liquidity_crisis = simulate_scenario({
            {"spreads", 5.0},  // 5x normal spreads
            {"volume", 0.2},   // 20% of normal volume
            {"correlation", 0.9}  // High correlation
        });
        
        // Black swan scenario
        results.black_swan = simulate_scenario({
            {"NIFTY", -0.40},  // 40% crash
            {"volatility", 4.0},  // 4x volatility
            {"correlation", 1.0}  // Perfect correlation
        });
        
        return results;
    }
};
```

## 🚨 **Risk Controls & Circuit Breakers**

### **Automated Risk Controls**

```cpp
class RiskControlSystem {
private:
    enum class CircuitBreakerState {
        NORMAL,
        WARNING,
        TRIGGERED,
        COOLDOWN
    };
    
    struct CircuitBreaker {
        std::string name;
        double threshold;
        std::chrono::seconds cooldown_period;
        std::atomic<CircuitBreakerState> state;
        std::chrono::time_point<std::chrono::steady_clock> triggered_time;
    };
    
    std::vector<CircuitBreaker> circuit_breakers_;
    
public:
    void initialize_circuit_breakers() {
        // Daily loss circuit breaker
        circuit_breakers_.push_back({
            "DAILY_LOSS",
            daily_loss_limit_,  // ₹2 crores
            std::chrono::seconds(3600),  // 1 hour cooldown
            CircuitBreakerState::NORMAL
        });
        
        // Position limit circuit breaker
        circuit_breakers_.push_back({
            "POSITION_LIMIT",
            max_position_value_,  // ₹10 crores
            std::chrono::seconds(1800),  // 30 min cooldown
            CircuitBreakerState::NORMAL
        });
        
        // Order rate circuit breaker
        circuit_breakers_.push_back({
            "ORDER_RATE",
            max_orders_per_second_,  // 1000 orders/sec
            std::chrono::seconds(60),  // 1 min cooldown
            CircuitBreakerState::NORMAL
        });
    }
    
    void trigger_circuit_breaker(const std::string& breaker_name) {
        auto it = std::find_if(circuit_breakers_.begin(), circuit_breakers_.end(),
            [&](const CircuitBreaker& cb) { return cb.name == breaker_name; });
        
        if (it != circuit_breakers_.end()) {
            it->state = CircuitBreakerState::TRIGGERED;
            it->triggered_time = std::chrono::steady_clock::now();
            
            // Execute emergency actions
            execute_emergency_actions(breaker_name);
            
            // Notify all stakeholders
            notify_circuit_breaker_triggered(breaker_name);
        }
    }
    
private:
    void execute_emergency_actions(const std::string& breaker_name) {
        if (breaker_name == "DAILY_LOSS") {
            // Stop all trading
            trading_engine_->stop_all_strategies();
            
            // Cancel all open orders
            order_manager_->cancel_all_orders();
            
            // Liquidate risky positions
            position_manager_->liquidate_positions_above_risk(0.8);
        }
        else if (breaker_name == "POSITION_LIMIT") {
            // Stop strategy that breached
            trading_engine_->stop_strategy(breaching_strategy_id_);
            
            // Reduce position to limit
            position_manager_->reduce_position_to_limit();
        }
    }
};
```

### **Kill Switch Implementation**

```cpp
class KillSwitch {
private:
    std::atomic<bool> emergency_stop_{false};
    std::atomic<std::chrono::steady_clock::time_point> activation_time_;
    
public:
    void activate(const std::string& reason) {
        bool expected = false;
        if (emergency_stop_.compare_exchange_strong(expected, true)) {
            activation_time_ = std::chrono::steady_clock::now();
            
            LOG_CRITICAL("KILL SWITCH ACTIVATED: {}", reason);
            
            // Immediate actions
            stop_all_trading();
            cancel_all_orders();
            notify_emergency_contacts();
            save_system_state();
            
            // Begin orderly shutdown
            begin_emergency_shutdown();
        }
    }
    
private:
    void stop_all_trading() {
        // Stop all strategies immediately
        for (auto& strategy : trading_engine_->get_strategies()) {
            strategy->emergency_stop();
        }
        
        // Disable order submission
        order_manager_->disable_order_submission();
        
        // Disconnect from exchanges
        for (auto& connection : exchange_connections_) {
            connection->disconnect();
        }
    }
};
```

## 📈 **Risk Reporting**

### **Real-Time Risk Dashboard**

```cpp
struct RiskDashboard {
    // Portfolio metrics
    double total_pnl;
    double daily_pnl;
    double unrealized_pnl;
    double realized_pnl;
    
    // Risk metrics
    double current_var_95;
    double current_var_99;
    double expected_shortfall;
    double max_drawdown;
    
    // Exposure metrics
    double gross_exposure;
    double net_exposure;
    std::map<std::string, double> sector_exposure;
    std::map<std::string, double> symbol_exposure;
    
    // Limit utilization
    double var_limit_usage;      // Percentage
    double exposure_limit_usage;  // Percentage
    double margin_usage;          // Percentage
    
    // Alerts
    std::vector<RiskAlert> active_alerts;
    
    // Circuit breakers
    std::map<std::string, CircuitBreakerStatus> circuit_breakers;
};
```

### **Risk Reports**

```cpp
class RiskReportGenerator {
public:
    void generate_daily_risk_report() {
        RiskReport report;
        report.date = std::chrono::system_clock::now();
        
        // Trading summary
        report.total_trades = trade_repository_->count_trades_today();
        report.total_volume = trade_repository_->sum_volume_today();
        report.total_pnl = pnl_calculator_->calculate_daily_pnl();
        
        // Risk summary
        report.max_var_95 = risk_analytics_->get_max_var_95_today();
        report.max_drawdown = risk_analytics_->get_max_drawdown_today();
        report.risk_breaches = risk_monitor_->get_breaches_today();
        
        // Compliance summary
        report.regulatory_breaches = compliance_->get_breaches_today();
        report.limit_violations = limit_monitor_->get_violations_today();
        
        // Save and distribute
        report_repository_->save(report);
        report_distributor_->send_to_stakeholders(report);
    }
};
```

## 🔧 **Risk Configuration**

### **Risk Parameters Configuration**

```yaml
risk_management:
  market_risk:
    var_confidence_95: 0.95
    var_confidence_99: 0.99
    var_limit_95: 5000000  # ₹50 lakhs
    var_limit_99: 10000000  # ₹1 crore
    max_portfolio_delta: 50000000  # ₹5 crores
    
  position_limits:
    default_limit: 1000000  # ₹10 lakhs
    symbol_limits:
      RELIANCE: 2000000  # ₹20 lakhs
      TCS: 1500000       # ₹15 lakhs
      INFY: 1500000      # ₹15 lakhs
      
  daily_limits:
    max_loss: 20000000  # ₹2 crores
    max_trades: 10000
    max_volume: 1000000000  # ₹100 crores
    
  circuit_breakers:
    daily_loss:
      threshold: 20000000  # ₹2 crores
      action: stop_all_trading
      cooldown: 3600  # 1 hour
      
    position_breach:
      threshold: 1.2  # 120% of limit
      action: stop_strategy
      cooldown: 1800  # 30 minutes
      
    system_error:
      threshold: 10  # errors per minute
      action: reduce_trading
      cooldown: 300  # 5 minutes
```

## 📊 **Risk Metrics & KPIs**

### **Key Risk Indicators (KRIs)**

| Metric | Target | Warning | Critical |
|--------|--------|---------|----------|
| VaR (95%) | <₹50L | >₹40L | >₹50L |
| VaR (99%) | <₹100L | >₹80L | >₹100L |
| Sharpe Ratio | >3.0 | <2.5 | <2.0 |
| Max Drawdown | <5% | >4% | >5% |
| Daily P&L | >₹0 | <-₹1Cr | <-₹2Cr |
| Order Error Rate | <0.01% | >0.01% | >0.05% |
| Margin Usage | <80% | >80% | >90% |
| System Uptime | >99.95% | <99.95% | <99.9% |

### **Risk Attribution**

```cpp
struct RiskAttribution {
    // By strategy
    std::map<std::string, double> strategy_var;
    std::map<std::string, double> strategy_pnl;
    
    // By asset class
    std::map<std::string, double> asset_var;
    std::map<std::string, double> asset_pnl;
    
    // By risk factor
    std::map<std::string, double> factor_contribution;
    
    // Correlation impact
    double diversification_benefit;
    double correlation_risk;
};
```

## ✅ **Regulatory Compliance**

### **SEBI Risk Management Requirements**

1. **Pre-trade risk controls**
   - Price bands validation
   - Quantity limits checking
   - Order value restrictions
   - Cumulative order limits

2. **Real-time monitoring**
   - Position tracking
   - Margin monitoring
   - Exposure limits
   - P&L tracking

3. **Post-trade surveillance**
   - Trade surveillance
   - Pattern detection
   - Manipulation detection
   - Reporting requirements

4. **Audit trail**
   - Complete order lifecycle
   - Risk decisions logging
   - System access logs
   - Configuration changes

---

**This comprehensive risk management system ensures capital preservation while enabling profitable trading within acceptable risk parameters.**