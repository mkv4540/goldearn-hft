# GoldEarn HFT - Order Management System

## 🎯 **Order Management Overview**

### **Core Objectives**
- **Smart order routing**: Best execution across venues
- **Ultra-low latency**: <100μs order submission
- **High throughput**: 100K orders/second capacity
- **Intelligent execution**: Minimize market impact
- **Complete audit trail**: Regulatory compliance

### **Order Lifecycle**
```
Order Creation → Risk Check → Routing → Execution → Settlement
      ↓             ↓          ↓          ↓          ↓
   Validation   Pre-trade   Venue      Fill      Trade
   & Pricing    Risk Mgmt   Selection  Processing Settlement
```

## 🏗️ **OMS Architecture**

```cpp
class OrderManagementSystem {
private:
    // Core components
    std::unique_ptr<OrderValidator> validator_;
    std::unique_ptr<SmartRouter> router_;
    std::unique_ptr<VenueManager> venue_manager_;
    std::unique_ptr<FillProcessor> fill_processor_;
    
    // Order tracking
    concurrent_hash_map<OrderId, Order> active_orders_;
    lock_free_queue<OrderEvent> order_queue_;
    
public:
    OrderResult submit_order(const OrderRequest& request);
    OrderResult modify_order(OrderId id, const Modification& mod);
    OrderResult cancel_order(OrderId id);
    
    // Order status and queries
    Order get_order(OrderId id) const;
    std::vector<Order> get_orders_by_strategy(StrategyId id) const;
    
    // Event handling
    void on_fill(const Fill& fill);
    void on_order_ack(const OrderAck& ack);
    void on_order_reject(const OrderReject& reject);
};
```

## 📊 **Order Types & Strategies**

### **Supported Order Types**

```cpp
enum class OrderType {
    MARKET,         // Execute immediately at best available price
    LIMIT,          // Execute only at specified price or better
    STOP,           // Market order triggered by stop price
    STOP_LIMIT,     // Limit order triggered by stop price
    IOC,            // Immediate or Cancel
    FOK,            // Fill or Kill
    ICEBERG,        // Large order split into smaller pieces
    TWAP,           // Time-Weighted Average Price
    VWAP,           // Volume-Weighted Average Price
    POV,            // Percentage of Volume
    IMPLEMENTATION  // Implementation Shortfall
};
```

### **Order Attributes**

```cpp
struct Order {
    OrderId id;
    StrategyId strategy_id;
    Symbol symbol;
    Side side;                    // BUY/SELL
    OrderType type;
    TimeInForce time_in_force;    // DAY/GTC/IOC/FOK
    
    // Quantities
    uint64_t quantity;
    uint64_t filled_quantity;
    uint64_t remaining_quantity;
    
    // Pricing
    double price;                 // Limit price (0 for market)
    double stop_price;            // Stop trigger price
    double avg_fill_price;
    
    // Timestamps (microsecond precision)
    Timestamp created_time;
    Timestamp submitted_time;
    Timestamp first_fill_time;
    Timestamp last_fill_time;
    
    // Status and venue
    OrderStatus status;
    VenueId venue;
    std::string venue_order_id;
    
    // Risk and compliance
    bool risk_checked;
    std::string rejection_reason;
    
    // Performance tracking
    uint64_t submission_latency_us;
    uint64_t ack_latency_us;
};
```

## 🔀 **Smart Order Routing**

### **Routing Engine**

```cpp
class SmartRouter {
private:
    struct VenueMetrics {
        double fill_rate;
        double avg_latency_us;
        double effective_spread;
        double maker_fee_rate;
        double taker_fee_rate;
        uint64_t available_liquidity;
        Timestamp last_update;
    };
    
    std::unordered_map<VenueId, VenueMetrics> venue_metrics_;
    
public:
    RoutingDecision route_order(const Order& order) {
        // Analyze current market conditions
        auto market_analysis = analyze_market(order.symbol);
        
        // Score each venue
        std::vector<VenueScore> scores;
        for (const auto& venue : available_venues_) {
            if (supports_symbol(venue, order.symbol)) {
                double score = calculate_venue_score(venue, order, market_analysis);
                scores.emplace_back(venue, score);
            }
        }
        
        // Sort by score and return best venue
        std::sort(scores.begin(), scores.end(), 
                 [](const auto& a, const auto& b) { return a.score > b.score; });
        
        return RoutingDecision{
            .venue = scores[0].venue,
            .expected_fill_rate = venue_metrics_[scores[0].venue].fill_rate,
            .expected_latency = venue_metrics_[scores[0].venue].avg_latency_us,
            .reason = "Best overall score"
        };
    }
    
private:
    double calculate_venue_score(VenueId venue, const Order& order, 
                                const MarketAnalysis& analysis) {
        const auto& metrics = venue_metrics_[venue];
        
        // Components of score
        double fill_score = metrics.fill_rate * 0.3;
        double latency_score = (1000.0 - metrics.avg_latency_us) / 1000.0 * 0.2;
        double cost_score = calculate_cost_score(venue, order) * 0.3;
        double liquidity_score = std::min(1.0, 
            metrics.available_liquidity / order.quantity) * 0.2;
        
        return fill_score + latency_score + cost_score + liquidity_score;
    }
};
```

### **Venue Management**

```cpp
class VenueManager {
private:
    struct VenueConnection {
        VenueId id;
        std::string name;
        ConnectionType type;        // FIX, Binary, REST
        ConnectionStatus status;
        std::unique_ptr<VenueConnector> connector;
        
        // Performance metrics
        uint64_t orders_sent;
        uint64_t orders_filled;
        double avg_latency_us;
        Timestamp last_heartbeat;
    };
    
    std::vector<VenueConnection> venues_;
    
public:
    void add_venue(VenueConfig config) {
        VenueConnection connection;
        connection.id = config.id;
        connection.name = config.name;
        connection.type = config.type;
        connection.status = ConnectionStatus::DISCONNECTED;
        
        // Create appropriate connector
        switch (config.type) {
            case ConnectionType::FIX:
                connection.connector = std::make_unique<FIXConnector>(config);
                break;
            case ConnectionType::BINARY:
                connection.connector = std::make_unique<BinaryConnector>(config);
                break;
            case ConnectionType::REST:
                connection.connector = std::make_unique<RESTConnector>(config);
                break;
        }
        
        venues_.push_back(std::move(connection));
    }
    
    OrderResult send_order(VenueId venue, const Order& order) {
        auto it = find_venue(venue);
        if (it == venues_.end()) {
            return OrderResult{false, "Venue not found"};
        }
        
        if (it->status != ConnectionStatus::CONNECTED) {
            return OrderResult{false, "Venue not connected"};
        }
        
        auto start = std::chrono::high_resolution_clock::now();
        auto result = it->connector->send_order(order);
        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        
        // Update metrics
        it->orders_sent++;
        it->avg_latency_us = (it->avg_latency_us * 0.9) + (latency * 0.1);
        
        return result;
    }
};
```

## ⚡ **High-Performance Execution**

### **Order Processing Pipeline**

```cpp
class OrderProcessor {
private:
    // Lock-free ring buffers for each stage
    ring_buffer<OrderRequest> request_queue_;
    ring_buffer<ValidatedOrder> validated_queue_;
    ring_buffer<RoutedOrder> routed_queue_;
    ring_buffer<VenueOrder> venue_queue_;
    
    // Processing threads
    std::thread validation_thread_;
    std::thread routing_thread_;
    std::thread submission_thread_;
    
public:
    void process_orders() {
        // Parallel processing pipeline
        std::thread([this] { validation_loop(); }).detach();
        std::thread([this] { routing_loop(); }).detach();
        std::thread([this] { submission_loop(); }).detach();
    }
    
private:
    void validation_loop() {
        pin_to_cpu(VALIDATION_CPU);
        
        while (running_) {
            OrderRequest request;
            if (request_queue_.pop(request)) {
                auto start = rdtsc();
                
                // Validate order
                auto validation_result = validator_->validate(request);
                
                if (validation_result.valid) {
                    ValidatedOrder validated_order{request, validation_result};
                    validated_queue_.push(validated_order);
                } else {
                    // Send rejection
                    send_rejection(request, validation_result.reason);
                }
                
                auto latency = rdtsc() - start;
                update_latency_metric("validation", latency);
            }
        }
    }
};
```

### **Zero-Copy Order Processing**

```cpp
// Memory-mapped order structures for zero-copy
struct alignas(64) OrderMessage {
    MessageHeader header;
    OrderId order_id;
    StrategyId strategy_id;
    Symbol symbol;
    Side side;
    uint64_t quantity;
    double price;
    Timestamp timestamp;
    
    // Padding to cache line boundary
    char padding[64 - sizeof(OrderId) - sizeof(StrategyId) - 
                 sizeof(Symbol) - sizeof(Side) - sizeof(uint64_t) - 
                 sizeof(double) - sizeof(Timestamp) - sizeof(MessageHeader)];
};

static_assert(sizeof(OrderMessage) == 64, "Order must fit in cache line");
```

## 📈 **Execution Algorithms**

### **TWAP (Time-Weighted Average Price)**

```cpp
class TWAPAlgorithm : public ExecutionAlgorithm {
private:
    struct TWAPState {
        uint64_t total_quantity;
        uint64_t remaining_quantity;
        std::chrono::seconds duration;
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point end_time;
        double participation_rate;
    };
    
public:
    void execute(const AlgorithmicOrder& order) override {
        TWAPState state;
        state.total_quantity = order.quantity;
        state.remaining_quantity = order.quantity;
        state.duration = order.duration;
        state.start_time = std::chrono::steady_clock::now();
        state.end_time = state.start_time + state.duration;
        state.participation_rate = order.participation_rate;
        
        // Schedule slices
        auto slice_interval = state.duration / order.num_slices;
        auto slice_size = state.total_quantity / order.num_slices;
        
        for (int i = 0; i < order.num_slices; ++i) {
            auto schedule_time = state.start_time + (slice_interval * i);
            schedule_slice(schedule_time, slice_size, order);
        }
    }
    
private:
    void schedule_slice(std::chrono::steady_clock::time_point when,
                       uint64_t quantity, const AlgorithmicOrder& order) {
        timer_manager_->schedule(when, [this, quantity, order]() {
            // Check market conditions
            auto market_data = get_current_market_data(order.symbol);
            
            // Adjust slice size based on participation rate
            auto adjusted_quantity = std::min(quantity,
                static_cast<uint64_t>(market_data.volume * 
                                     order.participation_rate));
            
            // Submit slice order
            Order slice_order;
            slice_order.symbol = order.symbol;
            slice_order.side = order.side;
            slice_order.quantity = adjusted_quantity;
            slice_order.type = OrderType::LIMIT;
            slice_order.price = calculate_limit_price(market_data, order.side);
            
            submit_order(slice_order);
        });
    }
};
```

### **VWAP (Volume-Weighted Average Price)**

```cpp
class VWAPAlgorithm : public ExecutionAlgorithm {
private:
    HistoricalVolumeProfile volume_profile_;
    
public:
    void execute(const AlgorithmicOrder& order) override {
        // Load historical volume profile
        volume_profile_ = load_volume_profile(order.symbol);
        
        // Calculate expected volume for each time slice
        auto current_time = market_open_time();
        auto end_time = std::min(order.end_time, market_close_time());
        
        while (current_time < end_time && has_remaining_quantity()) {
            auto slice_duration = std::chrono::minutes(5);
            auto expected_volume = volume_profile_.get_expected_volume(
                current_time, current_time + slice_duration);
            
            auto target_volume = expected_volume * order.participation_rate;
            auto slice_quantity = std::min(remaining_quantity(), 
                                          target_volume);
            
            if (slice_quantity > 0) {
                submit_vwap_slice(slice_quantity, order);
            }
            
            current_time += slice_duration;
        }
    }
    
private:
    void submit_vwap_slice(uint64_t quantity, const AlgorithmicOrder& order) {
        // Get current market data
        auto market_data = get_current_market_data(order.symbol);
        
        // Calculate aggressive price based on VWAP
        double target_price = market_data.vwap;
        if (order.side == Side::BUY) {
            target_price = std::min(target_price + order.price_tolerance,
                                   market_data.ask);
        } else {
            target_price = std::max(target_price - order.price_tolerance,
                                   market_data.bid);
        }
        
        Order slice_order;
        slice_order.symbol = order.symbol;
        slice_order.side = order.side;
        slice_order.quantity = quantity;
        slice_order.type = OrderType::LIMIT;
        slice_order.price = target_price;
        slice_order.time_in_force = TimeInForce::IOC;
        
        submit_order(slice_order);
    }
};
```

## 🔍 **Order Monitoring & Analytics**

### **Real-Time Order Tracking**

```cpp
class OrderTracker {
private:
    struct OrderMetrics {
        double fill_rate;
        double avg_fill_time_ms;
        double implementation_shortfall;
        double market_impact_bps;
        uint64_t total_orders;
        uint64_t filled_orders;
        uint64_t cancelled_orders;
        uint64_t rejected_orders;
    };
    
    std::unordered_map<StrategyId, OrderMetrics> strategy_metrics_;
    std::unordered_map<VenueId, OrderMetrics> venue_metrics_;
    
public:
    void update_metrics(const OrderEvent& event) {
        switch (event.type) {
            case OrderEventType::FILL:
                update_fill_metrics(event);
                break;
            case OrderEventType::CANCEL:
                update_cancel_metrics(event);
                break;
            case OrderEventType::REJECT:
                update_reject_metrics(event);
                break;
        }
    }
    
    OrderMetrics get_strategy_metrics(StrategyId id) const {
        auto it = strategy_metrics_.find(id);
        return it != strategy_metrics_.end() ? it->second : OrderMetrics{};
    }
    
    void generate_execution_report(const Order& order) {
        ExecutionReport report;
        report.order_id = order.id;
        report.symbol = order.symbol;
        report.side = order.side;
        report.quantity = order.quantity;
        report.filled_quantity = order.filled_quantity;
        report.avg_fill_price = order.avg_fill_price;
        
        // Calculate implementation shortfall
        auto benchmark_price = get_decision_price(order);
        report.implementation_shortfall = 
            calculate_implementation_shortfall(order, benchmark_price);
        
        // Calculate market impact
        report.market_impact_bps = 
            calculate_market_impact(order);
        
        // Timing analysis
        report.time_to_fill = order.last_fill_time - order.created_time;
        report.submission_latency = order.submission_latency_us;
        
        save_execution_report(report);
    }
};
```

### **Best Execution Analysis**

```cpp
class BestExecutionAnalyzer {
public:
    BestExecutionReport analyze_execution(const Order& order) {
        BestExecutionReport report;
        
        // Price improvement analysis
        auto market_price = get_market_price_at_time(order.symbol, 
                                                    order.created_time);
        report.price_improvement = calculate_price_improvement(order, market_price);
        
        // Venue comparison
        report.venue_analysis = compare_venue_performance(order);
        
        // Timing analysis
        report.timing_analysis = analyze_execution_timing(order);
        
        // Cost analysis
        report.total_cost = calculate_total_execution_cost(order);
        report.explicit_costs = calculate_explicit_costs(order);
        report.implicit_costs = calculate_implicit_costs(order);
        
        return report;
    }
    
private:
    double calculate_implementation_shortfall(const Order& order,
                                            double benchmark_price) {
        if (order.filled_quantity == 0) return 0.0;
        
        double execution_price = order.avg_fill_price;
        double shortfall;
        
        if (order.side == Side::BUY) {
            shortfall = execution_price - benchmark_price;
        } else {
            shortfall = benchmark_price - execution_price;
        }
        
        return shortfall / benchmark_price * 10000; // in basis points
    }
};
```

## 📊 **Performance Metrics**

### **Order Management KPIs**

| Metric | Target | Warning | Critical |
|--------|--------|---------|----------|
| Order Latency | <100μs | >75μs | >100μs |
| Fill Rate | >95% | <90% | <85% |
| Order Accuracy | >99.99% | <99.95% | <99.9% |
| Venue Connectivity | 100% | <99% | <95% |
| Implementation Shortfall | <5 bps | >10 bps | >20 bps |
| Market Impact | <10 bps | >15 bps | >25 bps |

### **Order Flow Analytics**

```cpp
struct OrderFlowMetrics {
    // Volume metrics
    uint64_t total_orders_submitted;
    uint64_t total_quantity_submitted;
    uint64_t total_quantity_filled;
    
    // Timing metrics
    double avg_order_latency_us;
    double avg_fill_time_ms;
    double avg_cancel_time_ms;
    
    // Quality metrics
    double fill_rate;
    double cancel_rate;
    double reject_rate;
    
    // Cost metrics
    double avg_implementation_shortfall_bps;
    double avg_market_impact_bps;
    double total_commission;
    
    // By venue breakdown
    std::map<VenueId, VenueOrderMetrics> venue_breakdown;
};
```

---

**This Order Management System provides sophisticated execution capabilities with ultra-low latency and intelligent routing for optimal trade execution.**