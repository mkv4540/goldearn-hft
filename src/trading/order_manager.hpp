#pragma once

#include "trading_engine.hpp"
#include "../core/latency_tracker.hpp"
#include <unordered_map>
#include <queue>
#include <memory>
#include <atomic>
#include <thread>

namespace goldearn::trading {

// Order state tracking
enum class OrderState {
    CREATED,          // Order object created but not submitted
    PRE_TRADE_CHECK,  // Undergoing risk checks
    PENDING_SUBMIT,   // Queued for submission
    SUBMITTED,        // Sent to venue
    ACKNOWLEDGED,     // Confirmed by venue
    PARTIALLY_FILLED, // Partial execution
    FILLED,          // Fully executed
    PENDING_CANCEL,  // Cancel request sent
    CANCELLED,       // Successfully cancelled
    REJECTED,        // Rejected by venue or risk system
    EXPIRED,         // Order expired
    ERROR            // Error state
};

// Enhanced order structure with full lifecycle tracking
struct ManagedOrder : public Order {
    OrderState state;
    std::string venue_name;
    uint64_t venue_order_id;
    std::vector<ExecutionReport> executions;
    std::string rejection_reason;
    market_data::Timestamp last_state_change;
    uint32_t retry_count;
    
    // Performance metrics
    market_data::Timestamp pre_trade_check_start;
    market_data::Timestamp submission_start;
    market_data::Timestamp acknowledgment_time;
    market_data::Timestamp first_fill_time;
    
    // Risk attributes
    double max_slippage_bps;
    uint64_t max_execution_time_ms;
    bool allow_partial_fills;
    
    ManagedOrder(const Order& base_order) : Order(base_order) {
        state = OrderState::CREATED;
        venue_order_id = 0;
        retry_count = 0;
        max_slippage_bps = 10.0; // Default 10 bps max slippage
        max_execution_time_ms = 5000; // Default 5 second timeout
        allow_partial_fills = true;
        last_state_change = std::chrono::duration_cast<market_data::Timestamp>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        );
    }
};

// Order manager handles the complete order lifecycle
class OrderManager {
public:
    OrderManager();
    ~OrderManager();
    
    // Lifecycle
    bool initialize();
    void shutdown();
    
    // Order submission and management
    bool submit_order(const Order& order);
    bool cancel_order(uint64_t order_id);
    bool modify_order(uint64_t order_id, double new_price, uint64_t new_quantity);
    
    // Order queries
    ManagedOrder* get_order(uint64_t order_id);
    const ManagedOrder* get_order(uint64_t order_id) const;
    std::vector<ManagedOrder*> get_orders_by_strategy(const std::string& strategy_id);
    std::vector<ManagedOrder*> get_orders_by_symbol(uint64_t symbol_id);
    std::vector<ManagedOrder*> get_pending_orders();
    
    // Order state management
    void update_order_state(uint64_t order_id, OrderState new_state);
    void handle_execution_report(const ExecutionReport& execution);
    void handle_order_rejection(uint64_t order_id, const std::string& reason);
    
    // Venue integration
    void register_venue(std::shared_ptr<ExecutionVenue> venue);
    void unregister_venue(const std::string& venue_name);
    ExecutionVenue* get_venue(const std::string& venue_name);
    
    // Risk integration
    void set_pre_trade_check_callback(std::function<bool(const ManagedOrder&)> callback);
    void set_post_trade_check_callback(std::function<void(const ExecutionReport&)> callback);
    
    // Performance and monitoring
    struct OrderManagerStats {
        uint64_t total_orders_submitted;
        uint64_t total_orders_filled;
        uint64_t total_orders_cancelled;
        uint64_t total_orders_rejected;
        uint64_t orders_pending;
        double avg_submission_latency_ns;
        double avg_fill_latency_ns;
        double avg_cancel_latency_ns;
        double fill_rate_percent;
    };
    
    OrderManagerStats get_statistics() const;
    void reset_statistics();
    
    // Order routing and smart execution
    enum class RoutingStrategy {
        SINGLE_VENUE,      // Route to single best venue
        SPLIT_ACROSS_VENUES, // Split large orders across venues
        ICEBERG,           // Break into smaller child orders
        TWAP,             // Time-weighted average price
        VWAP,             // Volume-weighted average price
        IMPLEMENTATION_SHORTFALL // Minimize implementation shortfall
    };
    
    void set_default_routing_strategy(RoutingStrategy strategy);
    void set_routing_strategy_for_symbol(uint64_t symbol_id, RoutingStrategy strategy);
    
private:
    // Order storage and indexing
    std::unordered_map<uint64_t, std::unique_ptr<ManagedOrder>> orders_;
    std::unordered_map<std::string, std::vector<uint64_t>> strategy_orders_;
    std::unordered_map<uint64_t, std::vector<uint64_t>> symbol_orders_;
    mutable std::shared_mutex orders_mutex_;
    
    // Venue management
    std::unordered_map<std::string, std::shared_ptr<ExecutionVenue>> venues_;
    std::shared_mutex venues_mutex_;
    
    // Order processing queues
    std::queue<uint64_t> pre_trade_check_queue_;
    std::queue<uint64_t> submission_queue_;
    std::queue<uint64_t> cancel_queue_;
    std::queue<uint64_t> modify_queue_;
    
    // Threading and async processing
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> shutdown_requested_;
    std::condition_variable queue_condition_;
    std::mutex queue_mutex_;
    
    // Callbacks
    std::function<bool(const ManagedOrder&)> pre_trade_check_callback_;
    std::function<void(const ExecutionReport&)> post_trade_check_callback_;
    
    // Performance tracking
    std::unique_ptr<core::LatencyTracker> submission_latency_tracker_;
    std::unique_ptr<core::LatencyTracker> fill_latency_tracker_;
    std::unique_ptr<core::LatencyTracker> cancel_latency_tracker_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    OrderManagerStats stats_;
    
    // Routing configuration
    RoutingStrategy default_routing_strategy_;
    std::unordered_map<uint64_t, RoutingStrategy> symbol_routing_strategies_;
    
    // Worker thread functions
    void pre_trade_check_worker();
    void order_submission_worker();
    void order_management_worker();
    
    // Internal order processing
    bool process_pre_trade_checks(ManagedOrder& order);
    bool route_and_submit_order(ManagedOrder& order);
    bool execute_order_cancellation(ManagedOrder& order);
    bool execute_order_modification(ManagedOrder& order, double new_price, uint64_t new_quantity);
    
    // Order routing logic
    ExecutionVenue* select_venue_for_order(const ManagedOrder& order);
    std::vector<ManagedOrder> split_order_for_routing(const ManagedOrder& order, RoutingStrategy strategy);
    
    // Order state transitions
    void transition_order_state(ManagedOrder& order, OrderState new_state);
    bool is_valid_state_transition(OrderState from, OrderState to) const;
    
    // Venue callbacks
    void on_venue_execution_report(const ExecutionReport& execution);
    void on_venue_order_rejection(const Order& order, const std::string& reason);
    
    // Order validation
    bool validate_order_parameters(const Order& order) const;
    bool validate_modification_parameters(const ManagedOrder& order, double new_price, uint64_t new_quantity) const;
    
    // Timeout and cleanup
    void monitor_order_timeouts();
    void cleanup_completed_orders();
    std::thread timeout_monitor_thread_;
    std::atomic<std::chrono::seconds> order_cleanup_interval_{std::chrono::seconds(300)}; // 5 minutes
};

// Smart order types implementation
class SmartOrderExecution {
public:
    SmartOrderExecution(OrderManager* order_manager);
    ~SmartOrderExecution();
    
    // TWAP (Time Weighted Average Price) execution
    struct TWAPParams {
        uint64_t total_quantity;
        uint64_t duration_ms;
        uint64_t slice_interval_ms;
        double participation_rate; // % of volume to participate
        bool allow_market_orders;
    };
    
    bool execute_twap(uint64_t symbol_id, OrderSide side, const TWAPParams& params);
    
    // VWAP (Volume Weighted Average Price) execution
    struct VWAPParams {
        uint64_t total_quantity;
        std::vector<double> historical_volume_profile; // Hourly volume distribution
        double participation_rate;
        bool use_real_time_volume;
    };
    
    bool execute_vwap(uint64_t symbol_id, OrderSide side, const VWAPParams& params);
    
    // Iceberg order execution
    struct IcebergParams {
        uint64_t total_quantity;
        uint64_t visible_quantity;
        uint64_t refresh_quantity; // When to refresh visible quantity
        double price_improvement_threshold; // When to adjust price
    };
    
    bool execute_iceberg(uint64_t symbol_id, OrderSide side, double price, const IcebergParams& params);
    
    // Implementation Shortfall execution
    struct ISParams {
        uint64_t total_quantity;
        double arrival_price;
        double risk_aversion; // 0.0 to 1.0, higher means more aggressive
        uint64_t max_duration_ms;
    };
    
    bool execute_implementation_shortfall(uint64_t symbol_id, OrderSide side, const ISParams& params);
    
    // Execution monitoring
    struct ExecutionProgress {
        uint64_t execution_id;
        uint64_t total_quantity;
        uint64_t executed_quantity;
        uint64_t remaining_quantity;
        double avg_execution_price;
        double benchmark_price; // TWAP, VWAP, or arrival price
        double implementation_shortfall;
        std::chrono::milliseconds elapsed_time;
        std::chrono::milliseconds estimated_completion_time;
    };
    
    ExecutionProgress get_execution_progress(uint64_t execution_id) const;
    std::vector<ExecutionProgress> get_all_active_executions() const;
    
    // Cancel smart execution
    bool cancel_execution(uint64_t execution_id);
    
private:
    OrderManager* order_manager_;
    std::atomic<uint64_t> next_execution_id_;
    
    // Active executions tracking
    struct ActiveExecution {
        uint64_t execution_id;
        std::string execution_type;
        uint64_t symbol_id;
        OrderSide side;
        std::vector<uint64_t> child_order_ids;
        std::chrono::high_resolution_clock::time_point start_time;
        std::atomic<bool> cancelled;
        ExecutionProgress progress;
    };
    
    std::unordered_map<uint64_t, std::unique_ptr<ActiveExecution>> active_executions_;
    std::shared_mutex executions_mutex_;
    
    // Execution worker threads
    std::vector<std::thread> execution_workers_;
    std::atomic<bool> shutdown_requested_;
    
    // Individual execution implementations
    void execute_twap_worker(uint64_t execution_id, const TWAPParams& params);
    void execute_vwap_worker(uint64_t execution_id, const VWAPParams& params);
    void execute_iceberg_worker(uint64_t execution_id, const IcebergParams& params);
    void execute_is_worker(uint64_t execution_id, const ISParams& params);
    
    // Helper functions
    uint64_t calculate_slice_size(const TWAPParams& params, size_t slice_number) const;
    uint64_t calculate_vwap_slice_size(const VWAPParams& params, double current_volume_rate) const;
    void update_execution_progress(uint64_t execution_id);
};

} // namespace goldearn::trading