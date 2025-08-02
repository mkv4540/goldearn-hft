#pragma once

#include "../core/latency_tracker.hpp"
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>
#include <thread>
#include <chrono>
#include <functional>

namespace goldearn::monitoring {

// System-wide performance metrics
struct SystemMetrics {
    // CPU metrics
    std::atomic<double> cpu_usage_percent{0.0};
    std::atomic<double> cpu_user_percent{0.0};
    std::atomic<double> cpu_system_percent{0.0};
    std::atomic<double> cpu_idle_percent{0.0};
    
    // Memory metrics
    std::atomic<uint64_t> memory_total_bytes{0};
    std::atomic<uint64_t> memory_used_bytes{0};
    std::atomic<uint64_t> memory_free_bytes{0};
    std::atomic<uint64_t> memory_cached_bytes{0};
    std::atomic<double> memory_usage_percent{0.0};
    
    // Network metrics
    std::atomic<uint64_t> network_bytes_sent{0};
    std::atomic<uint64_t> network_bytes_received{0};
    std::atomic<uint64_t> network_packets_sent{0};
    std::atomic<uint64_t> network_packets_received{0};
    std::atomic<uint32_t> network_connections{0};
    
    // Disk I/O metrics
    std::atomic<uint64_t> disk_reads_total{0};
    std::atomic<uint64_t> disk_writes_total{0};
    std::atomic<uint64_t> disk_read_bytes{0};
    std::atomic<uint64_t> disk_write_bytes{0};
    
    // Process metrics
    std::atomic<uint32_t> thread_count{0};
    std::atomic<uint32_t> fd_count{0};
    std::atomic<double> process_cpu_percent{0.0};
    std::atomic<uint64_t> process_memory_bytes{0};
    
    std::chrono::steady_clock::time_point last_update;
};

// Trading-specific performance metrics
struct TradingMetrics {
    // Order metrics
    std::atomic<uint64_t> orders_submitted{0};
    std::atomic<uint64_t> orders_filled{0};
    std::atomic<uint64_t> orders_cancelled{0};
    std::atomic<uint64_t> orders_rejected{0};
    
    // Latency metrics (in nanoseconds)
    std::atomic<uint64_t> order_latency_min_ns{UINT64_MAX};
    std::atomic<uint64_t> order_latency_max_ns{0};
    std::atomic<uint64_t> order_latency_avg_ns{0};
    
    std::atomic<uint64_t> fill_latency_min_ns{UINT64_MAX};
    std::atomic<uint64_t> fill_latency_max_ns{0};
    std::atomic<uint64_t> fill_latency_avg_ns{0};
    
    // Market data metrics
    std::atomic<uint64_t> market_data_messages{0};
    std::atomic<uint64_t> quote_updates{0};
    std::atomic<uint64_t> trade_updates{0};
    std::atomic<uint64_t> market_data_gaps{0};
    
    // Risk metrics
    std::atomic<uint64_t> risk_checks_performed{0};
    std::atomic<uint64_t> risk_checks_passed{0};
    std::atomic<uint64_t> risk_checks_failed{0};
    std::atomic<uint64_t> risk_violations{0};
    
    // P&L metrics
    std::atomic<double> unrealized_pnl{0.0};
    std::atomic<double> realized_pnl{0.0};
    std::atomic<double> total_volume_traded{0.0};
    std::atomic<uint32_t> positions_count{0};
    
    std::chrono::steady_clock::time_point last_update;
};

// Alert levels for monitoring
enum class AlertLevel {
    INFO = 0,
    WARNING = 1,
    CRITICAL = 2,
    EMERGENCY = 3
};

// Performance alert
struct PerformanceAlert {
    AlertLevel level;
    std::string component;
    std::string metric_name;
    double current_value;
    double threshold_value;
    std::string description;
    std::chrono::steady_clock::time_point timestamp;
    
    PerformanceAlert() = default;
    PerformanceAlert(AlertLevel lvl, const std::string& comp, const std::string& metric,
                    double current, double threshold, const std::string& desc)
        : level(lvl), component(comp), metric_name(metric), current_value(current),
          threshold_value(threshold), description(desc), 
          timestamp(std::chrono::steady_clock::now()) {}
};

// Main performance monitoring class
class PerformanceMonitor {
public:
    static PerformanceMonitor& instance() {
        static PerformanceMonitor instance;
        return instance;
    }
    
    // Lifecycle
    bool initialize();
    void shutdown();
    bool is_running() const { return running_.load(); }
    
    // Metric collection
    const SystemMetrics& get_system_metrics() const { return system_metrics_; }
    const TradingMetrics& get_trading_metrics() const { return trading_metrics_; }
    
    // Custom metrics
    void record_custom_metric(const std::string& name, double value);
    double get_custom_metric(const std::string& name) const;
    std::vector<std::pair<std::string, double>> get_all_custom_metrics() const;
    
    // Latency tracking integration
    void register_latency_tracker(const std::string& name, 
                                 std::shared_ptr<core::LatencyTracker> tracker);
    void unregister_latency_tracker(const std::string& name);
    
    // Alert management
    using AlertCallback = std::function<void(const PerformanceAlert&)>;
    void set_alert_callback(AlertCallback callback) { alert_callback_ = callback; }
    
    void set_threshold(const std::string& metric_name, double threshold, AlertLevel level);
    void remove_threshold(const std::string& metric_name);
    
    std::vector<PerformanceAlert> get_recent_alerts(std::chrono::minutes duration = std::chrono::minutes(60)) const;
    uint32_t get_alert_count(AlertLevel min_level = AlertLevel::WARNING) const;
    
    // Performance analysis
    struct PerformanceReport {
        SystemMetrics system_snapshot;
        TradingMetrics trading_snapshot;
        std::vector<std::pair<std::string, double>> custom_metrics;
        std::vector<PerformanceAlert> active_alerts;
        
        // Analysis
        double overall_system_health_score; // 0.0 to 100.0
        std::vector<std::string> performance_bottlenecks;
        std::vector<std::string> recommendations;
        
        std::chrono::steady_clock::time_point generated_at;
    };
    
    PerformanceReport generate_report() const;
    void export_report_json(const std::string& filename) const;
    void export_report_csv(const std::string& filename) const;
    
    // Real-time monitoring
    void start_monitoring(std::chrono::milliseconds update_interval = std::chrono::milliseconds(1000));
    void stop_monitoring();
    
    // Configuration
    struct MonitoringConfig {
        bool enable_system_monitoring = true;
        bool enable_trading_monitoring = true;
        bool enable_custom_metrics = true;
        
        std::chrono::milliseconds update_interval{1000};
        std::chrono::minutes alert_history_retention{60 * 24}; // 24 hours
        
        // Thresholds
        double cpu_usage_warning_threshold = 80.0;
        double cpu_usage_critical_threshold = 95.0;
        double memory_usage_warning_threshold = 85.0;
        double memory_usage_critical_threshold = 95.0;
        
        uint64_t order_latency_warning_ns = 100000;  // 100μs
        uint64_t order_latency_critical_ns = 1000000; // 1ms
        uint64_t fill_latency_warning_ns = 500000;   // 500μs
        uint64_t fill_latency_critical_ns = 2000000; // 2ms
        
        uint32_t max_alerts_per_minute = 10;
        bool enable_alert_suppression = true;
    };
    
    void configure(const MonitoringConfig& config);
    const MonitoringConfig& get_config() const { return config_; }
    
    // Trading-specific metric updates
    void record_order_submitted(uint64_t latency_ns);
    void record_order_filled(uint64_t latency_ns);
    void record_order_cancelled();
    void record_order_rejected();
    void record_market_data_message();
    void record_quote_update();
    void record_trade_update();
    void record_market_data_gap();
    void record_risk_check(bool passed, uint64_t latency_ns = 0);
    void record_risk_violation();
    void update_pnl(double unrealized, double realized);
    void update_position_count(uint32_t count);
    
private:
    PerformanceMonitor() = default;
    ~PerformanceMonitor();
    
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    
    SystemMetrics system_metrics_;
    TradingMetrics trading_metrics_;
    MonitoringConfig config_;
    
    // Custom metrics
    mutable std::shared_mutex custom_metrics_mutex_;
    std::unordered_map<std::string, std::atomic<double>> custom_metrics_;
    
    // Latency trackers
    mutable std::shared_mutex trackers_mutex_;
    std::unordered_map<std::string, std::shared_ptr<core::LatencyTracker>> latency_trackers_;
    
    // Alerts
    mutable std::shared_mutex alerts_mutex_;
    std::vector<PerformanceAlert> alert_history_;
    std::unordered_map<std::string, std::pair<double, AlertLevel>> thresholds_;
    AlertCallback alert_callback_;
    
    // Threading
    std::unique_ptr<std::thread> monitoring_thread_;
    std::unique_ptr<std::thread> cleanup_thread_;
    
    // System monitoring implementation
    void update_system_metrics();
    void update_trading_metrics();
    void check_thresholds();
    void cleanup_old_alerts();
    
    void monitoring_loop();
    void cleanup_loop();
    
    // System-specific implementations
    bool read_cpu_stats();
    bool read_memory_stats();
    bool read_network_stats();
    bool read_disk_stats();
    bool read_process_stats();
    
    // Alert handling
    void trigger_alert(const PerformanceAlert& alert);
    bool should_suppress_alert(const PerformanceAlert& alert) const;
    void add_alert_to_history(const PerformanceAlert& alert);
    
    // Utility functions
    double calculate_system_health_score() const;
    std::vector<std::string> identify_bottlenecks() const;
    std::vector<std::string> generate_recommendations() const;
};

// Specialized performance trackers for HFT components

// Order book performance tracker
class OrderBookPerformanceTracker {
public:
    OrderBookPerformanceTracker(const std::string& symbol);
    
    void record_update(uint64_t latency_ns, size_t depth_levels);
    void record_query(uint64_t latency_ns, const std::string& query_type);
    
    struct Stats {
        uint64_t total_updates;
        uint64_t total_queries;
        double avg_update_latency_ns;
        double avg_query_latency_ns;
        double max_update_latency_ns;
        double max_query_latency_ns;
        uint32_t avg_depth_levels;
    };
    
    Stats get_stats() const;
    void reset_stats();
    
private:
    std::string symbol_;
    std::atomic<uint64_t> update_count_{0};
    std::atomic<uint64_t> query_count_{0};
    std::atomic<uint64_t> total_update_latency_{0};
    std::atomic<uint64_t> total_query_latency_{0};
    std::atomic<uint64_t> max_update_latency_{0};
    std::atomic<uint64_t> max_query_latency_{0};
    std::atomic<uint64_t> total_depth_levels_{0};
};

// Risk engine performance tracker
class RiskEnginePerformanceTracker {
public:
    RiskEnginePerformanceTracker();
    
    void record_pre_trade_check(uint64_t latency_ns, bool passed);
    void record_post_trade_check(uint64_t latency_ns);
    void record_limit_violation(const std::string& limit_type);
    
    struct Stats {
        uint64_t total_pre_trade_checks;
        uint64_t passed_pre_trade_checks;
        uint64_t failed_pre_trade_checks;
        uint64_t total_post_trade_checks;
        double avg_pre_trade_latency_ns;
        double avg_post_trade_latency_ns;
        double max_pre_trade_latency_ns;
        std::unordered_map<std::string, uint32_t> violation_counts;
    };
    
    Stats get_stats() const;
    void reset_stats();
    
private:
    std::atomic<uint64_t> pre_trade_count_{0};
    std::atomic<uint64_t> pre_trade_passed_{0};
    std::atomic<uint64_t> post_trade_count_{0};
    std::atomic<uint64_t> total_pre_trade_latency_{0};
    std::atomic<uint64_t> total_post_trade_latency_{0};
    std::atomic<uint64_t> max_pre_trade_latency_{0};
    
    mutable std::shared_mutex violations_mutex_;
    std::unordered_map<std::string, std::atomic<uint32_t>> violation_counts_;
};

// Strategy performance tracker
class StrategyPerformanceTracker {
public:
    StrategyPerformanceTracker(const std::string& strategy_id);
    
    void record_signal_generation(uint64_t latency_ns);
    void record_order_decision(uint64_t latency_ns, bool submitted);
    void record_pnl_update(double unrealized_pnl, double realized_pnl);
    void record_position_update(const std::string& symbol, double quantity);
    
    struct Stats {
        uint64_t signals_generated;
        uint64_t orders_submitted;
        uint64_t orders_not_submitted;
        double avg_signal_latency_ns;
        double avg_decision_latency_ns;
        double total_unrealized_pnl;
        double total_realized_pnl;
        uint32_t active_positions;
        double sharpe_ratio;
        double max_drawdown;
    };
    
    Stats get_stats() const;
    void reset_stats();
    
private:
    std::string strategy_id_;
    std::atomic<uint64_t> signal_count_{0};
    std::atomic<uint64_t> order_count_{0};
    std::atomic<uint64_t> order_rejected_count_{0};
    std::atomic<uint64_t> total_signal_latency_{0};
    std::atomic<uint64_t> total_decision_latency_{0};
    
    std::atomic<double> unrealized_pnl_{0.0};
    std::atomic<double> realized_pnl_{0.0};
    std::atomic<double> peak_pnl_{0.0};
    std::atomic<double> max_drawdown_{0.0};
    
    mutable std::shared_mutex positions_mutex_;
    std::unordered_map<std::string, double> positions_;
    std::vector<double> pnl_history_;
};

} // namespace goldearn::monitoring