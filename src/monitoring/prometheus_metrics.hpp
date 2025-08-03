#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include <sstream>
#include <algorithm>
#include <cstdint>

namespace goldearn::monitoring {

// Metric types
enum class MetricType {
    COUNTER,
    GAUGE,
    HISTOGRAM,
    SUMMARY
};

// Base metric class
class Metric {
public:
    Metric(const std::string& name, const std::string& help, MetricType type)
        : name_(name), help_(help), type_(type) {}
    
    virtual ~Metric() = default;
    
    virtual std::string serialize() const = 0;
    virtual void reset() = 0;
    
    const std::string& name() const { return name_; }
    const std::string& help() const { return help_; }
    MetricType type() const { return type_; }
    
protected:
    std::string name_;
    std::string help_;
    MetricType type_;
};

// Counter metric (monotonic increasing)
class Counter : public Metric {
public:
    Counter(const std::string& name, const std::string& help)
        : Metric(name, help, MetricType::COUNTER), value_(0) {}
    
    void increment(double value = 1.0) {
        double expected = value_.load(std::memory_order_relaxed);
        while (!value_.compare_exchange_weak(expected, expected + value, std::memory_order_relaxed)) {
            // Retry until successful
        }
    }
    
    double value() const {
        return value_.load(std::memory_order_relaxed);
    }
    
    std::string serialize() const override {
        return name_ + " " + std::to_string(value());
    }
    
    void reset() override {
        value_.store(0, std::memory_order_relaxed);
    }
    
private:
    std::atomic<double> value_;
};

// Gauge metric (can go up and down)
class Gauge : public Metric {
public:
    Gauge(const std::string& name, const std::string& help)
        : Metric(name, help, MetricType::GAUGE), value_(0) {}
    
    void set(double value) {
        value_.store(value, std::memory_order_relaxed);
    }
    
    void increment(double value = 1.0) {
        double current = value_.load(std::memory_order_relaxed);
        while (!value_.compare_exchange_weak(current, current + value, std::memory_order_relaxed)) {
            // Retry
        }
    }
    
    void decrement(double value = 1.0) {
        increment(-value);
    }
    
    double value() const {
        return value_.load(std::memory_order_relaxed);
    }
    
    std::string serialize() const override {
        return name_ + " " + std::to_string(value());
    }
    
    void reset() override {
        value_.store(0, std::memory_order_relaxed);
    }
    
private:
    std::atomic<double> value_;
};

// Histogram metric (for latency measurements)
class Histogram : public Metric {
public:
    Histogram(const std::string& name, const std::string& help, 
              const std::vector<double>& buckets = {0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1.0, 5.0, 10.0})
        : Metric(name, help, MetricType::HISTOGRAM), buckets_(buckets), count_(0), sum_(0) {
        bucket_counts_.resize(buckets_.size(), 0);
    }
    
    void observe(double value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        count_++;
        sum_ += value;
        
        // Update bucket counts
        for (size_t i = 0; i < buckets_.size(); ++i) {
            if (value <= buckets_[i]) {
                bucket_counts_[i]++;
            }
        }
    }
    
    std::string serialize() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::ostringstream ss;
        
        // Bucket counts
        for (size_t i = 0; i < buckets_.size(); ++i) {
            ss << name_ << "_bucket{le=\"" << buckets_[i] << "\"} " << bucket_counts_[i] << "\n";
        }
        
        // +Inf bucket
        ss << name_ << "_bucket{le=\"+Inf\"} " << count_ << "\n";
        
        // Count and sum
        ss << name_ << "_count " << count_ << "\n";
        ss << name_ << "_sum " << sum_;
        
        return ss.str();
    }
    
    void reset() override {
        std::lock_guard<std::mutex> lock(mutex_);
        count_ = 0;
        sum_ = 0;
        std::fill(bucket_counts_.begin(), bucket_counts_.end(), 0);
    }
    
private:
    mutable std::mutex mutex_;
    std::vector<double> buckets_;
    std::vector<uint64_t> bucket_counts_;
    uint64_t count_;
    double sum_;
};

// Summary metric (quantiles)
class Summary : public Metric {
public:
    Summary(const std::string& name, const std::string& help, size_t max_samples = 1000)
        : Metric(name, help, MetricType::SUMMARY), max_samples_(max_samples), count_(0), sum_(0) {}
    
    void observe(double value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        count_++;
        sum_ += value;
        
        samples_.push_back(value);
        
        // Keep only the most recent samples
        if (samples_.size() > max_samples_) {
            samples_.erase(samples_.begin());
        }
    }
    
    double quantile(double q) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (samples_.empty()) {
            return 0.0;
        }
        
        std::vector<double> sorted_samples = samples_;
        std::sort(sorted_samples.begin(), sorted_samples.end());
        
        size_t index = static_cast<size_t>(q * (sorted_samples.size() - 1));
        return sorted_samples[index];
    }
    
    std::string serialize() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::ostringstream ss;
        
        // Common quantiles
        std::vector<double> quantiles = {0.5, 0.9, 0.95, 0.99};
        for (double q : quantiles) {
            ss << name_ << "{quantile=\"" << q << "\"} " << quantile(q) << "\n";
        }
        
        ss << name_ << "_count " << count_ << "\n";
        ss << name_ << "_sum " << sum_;
        
        return ss.str();
    }
    
    void reset() override {
        std::lock_guard<std::mutex> lock(mutex_);
        count_ = 0;
        sum_ = 0;
        samples_.clear();
    }
    
private:
    mutable std::mutex mutex_;
    size_t max_samples_;
    std::vector<double> samples_;
    uint64_t count_;
    double sum_;
};

// Metrics registry
class MetricsRegistry {
public:
    static MetricsRegistry& instance() {
        static MetricsRegistry instance;
        return instance;
    }
    
    // Register metrics
    std::shared_ptr<Counter> create_counter(const std::string& name, const std::string& help);
    std::shared_ptr<Gauge> create_gauge(const std::string& name, const std::string& help);
    std::shared_ptr<Histogram> create_histogram(const std::string& name, const std::string& help,
                                               const std::vector<double>& buckets = {});
    std::shared_ptr<Summary> create_summary(const std::string& name, const std::string& help);
    
    // Get metrics
    std::shared_ptr<Metric> get_metric(const std::string& name);
    std::vector<std::shared_ptr<Metric>> get_all_metrics();
    
    // Serialize all metrics
    std::string serialize_all() const;
    
    // Remove metric
    void remove_metric(const std::string& name);
    
    // Clear all metrics
    void clear();
    
private:
    MetricsRegistry() = default;
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Metric>> metrics_;
};

// HFT-specific metrics collector
class HFTMetricsCollector {
public:
    HFTMetricsCollector();
    ~HFTMetricsCollector();
    
    // Start/stop metrics collection
    void start();
    void stop();
    
    // Trading metrics
    void record_order_latency(double latency_us);
    void record_order_placed();
    void record_order_filled();
    void record_order_rejected();
    
    // Market data metrics
    void record_market_data_message();
    void record_market_data_parse_error();
    void record_market_data_latency(double latency_us);
    
    // Risk metrics
    void record_risk_check(double latency_us);
    void record_risk_violation();
    void record_position_value(double value);
    void record_pnl(double pnl);
    
    // System metrics
    void record_cpu_usage(double percent);
    void record_memory_usage(double percent);
    void record_network_bytes_sent(uint64_t bytes);
    void record_network_bytes_received(uint64_t bytes);
    
    // Get current metrics snapshot
    std::string get_metrics_snapshot() const;
    
private:
    void collection_thread_func();
    void update_system_metrics();
    
private:
    // Trading metrics
    std::shared_ptr<Histogram> order_latency_histogram_;
    std::shared_ptr<Counter> orders_placed_counter_;
    std::shared_ptr<Counter> orders_filled_counter_;
    std::shared_ptr<Counter> orders_rejected_counter_;
    std::shared_ptr<Gauge> active_orders_gauge_;
    
    // Market data metrics
    std::shared_ptr<Counter> market_data_messages_counter_;
    std::shared_ptr<Counter> market_data_errors_counter_;
    std::shared_ptr<Histogram> market_data_latency_histogram_;
    std::shared_ptr<Gauge> market_data_rate_gauge_;
    
    // Risk metrics
    std::shared_ptr<Histogram> risk_check_latency_histogram_;
    std::shared_ptr<Counter> risk_violations_counter_;
    std::shared_ptr<Gauge> total_position_value_gauge_;
    std::shared_ptr<Gauge> unrealized_pnl_gauge_;
    std::shared_ptr<Gauge> realized_pnl_gauge_;
    
    // System metrics
    std::shared_ptr<Gauge> cpu_usage_gauge_;
    std::shared_ptr<Gauge> memory_usage_gauge_;
    std::shared_ptr<Counter> network_bytes_sent_counter_;
    std::shared_ptr<Counter> network_bytes_received_counter_;
    
    // Collection thread
    std::atomic<bool> running_{false};
    std::thread collection_thread_;
    std::chrono::seconds collection_interval_{5};
};

// Metrics helper macros
#define RECORD_LATENCY(histogram, start_time) \
    do { \
        auto end_time = std::chrono::high_resolution_clock::now(); \
        auto duration = std::chrono::duration<double, std::micro>(end_time - start_time); \
        histogram->observe(duration.count()); \
    } while(0)

#define RECORD_TIMING(histogram, code_block) \
    do { \
        auto start_time = std::chrono::high_resolution_clock::now(); \
        code_block; \
        auto end_time = std::chrono::high_resolution_clock::now(); \
        auto duration = std::chrono::duration<double, std::micro>(end_time - start_time); \
        histogram->observe(duration.count()); \
    } while(0)

} // namespace goldearn::monitoring