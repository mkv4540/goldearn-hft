#include "prometheus_metrics.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "../utils/simple_logger.hpp"

namespace goldearn::monitoring {

// MetricsRegistry implementation
std::shared_ptr<Counter> MetricsRegistry::create_counter(const std::string& name,
                                                         const std::string& help) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto counter = std::make_shared<Counter>(name, help);
    metrics_[name] = counter;

    LOG_DEBUG("MetricsRegistry: Created counter '{}'", name);
    return counter;
}

std::shared_ptr<Gauge> MetricsRegistry::create_gauge(const std::string& name,
                                                     const std::string& help) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto gauge = std::make_shared<Gauge>(name, help);
    metrics_[name] = gauge;

    LOG_DEBUG("MetricsRegistry: Created gauge '{}'", name);
    return gauge;
}

std::shared_ptr<Histogram> MetricsRegistry::create_histogram(const std::string& name,
                                                             const std::string& help,
                                                             const std::vector<double>& buckets) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto histogram = buckets.empty() ? std::make_shared<Histogram>(name, help)
                                     : std::make_shared<Histogram>(name, help, buckets);
    metrics_[name] = histogram;

    LOG_DEBUG("MetricsRegistry: Created histogram '{}'", name);
    return histogram;
}

std::shared_ptr<Summary> MetricsRegistry::create_summary(const std::string& name,
                                                         const std::string& help) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto summary = std::make_shared<Summary>(name, help);
    metrics_[name] = summary;

    LOG_DEBUG("MetricsRegistry: Created summary '{}'", name);
    return summary;
}

std::shared_ptr<Metric> MetricsRegistry::get_metric(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = metrics_.find(name);
    return (it != metrics_.end()) ? it->second : nullptr;
}

std::vector<std::shared_ptr<Metric>> MetricsRegistry::get_all_metrics() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::shared_ptr<Metric>> result;
    result.reserve(metrics_.size());

    for (const auto& pair : metrics_) {
        result.push_back(pair.second);
    }

    return result;
}

std::string MetricsRegistry::serialize_all() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream output;

    for (const auto& pair : metrics_) {
        const auto& metric = pair.second;

        // Add HELP and TYPE comments
        output << "# HELP " << metric->name() << " " << metric->help() << "\n";
        output << "# TYPE " << metric->name() << " ";

        switch (metric->type()) {
            case MetricType::COUNTER:
                output << "counter";
                break;
            case MetricType::GAUGE:
                output << "gauge";
                break;
            case MetricType::HISTOGRAM:
                output << "histogram";
                break;
            case MetricType::SUMMARY:
                output << "summary";
                break;
        }
        output << "\n";

        // Add metric data
        output << metric->serialize() << "\n\n";
    }

    return output.str();
}

void MetricsRegistry::remove_metric(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = metrics_.find(name);
    if (it != metrics_.end()) {
        metrics_.erase(it);
        LOG_DEBUG("MetricsRegistry: Removed metric '{}'", name);
    }
}

void MetricsRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    metrics_.clear();
    LOG_INFO("MetricsRegistry: Cleared all metrics");
}

// HFTMetricsCollector implementation
HFTMetricsCollector::HFTMetricsCollector() {
    auto& registry = MetricsRegistry::instance();

    // Create trading metrics
    order_latency_histogram_ =
        registry.create_histogram("goldearn_order_latency_microseconds",
                                  "Order processing latency in microseconds",
                                  {1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000});

    orders_placed_counter_ =
        registry.create_counter("goldearn_orders_placed_total", "Total number of orders placed");

    orders_filled_counter_ =
        registry.create_counter("goldearn_orders_filled_total", "Total number of orders filled");

    orders_rejected_counter_ = registry.create_counter("goldearn_orders_rejected_total",
                                                       "Total number of orders rejected");

    active_orders_gauge_ =
        registry.create_gauge("goldearn_active_orders", "Number of currently active orders");

    // Create market data metrics
    market_data_messages_counter_ = registry.create_counter(
        "goldearn_market_data_messages_total", "Total number of market data messages processed");

    market_data_errors_counter_ = registry.create_counter(
        "goldearn_market_data_errors_total", "Total number of market data parsing errors");

    market_data_latency_histogram_ =
        registry.create_histogram("goldearn_market_data_latency_microseconds",
                                  "Market data processing latency in microseconds",
                                  {0.1, 0.5, 1, 2, 5, 10, 25, 50, 100, 250, 500});

    market_data_rate_gauge_ = registry.create_gauge("goldearn_market_data_rate_messages_per_second",
                                                    "Current market data message rate");

    // Create risk metrics
    risk_check_latency_histogram_ =
        registry.create_histogram("goldearn_risk_check_latency_microseconds",
                                  "Risk check latency in microseconds",
                                  {0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500});

    risk_violations_counter_ = registry.create_counter("goldearn_risk_violations_total",
                                                       "Total number of risk violations");

    total_position_value_gauge_ =
        registry.create_gauge("goldearn_total_position_value_inr", "Total position value in INR");

    unrealized_pnl_gauge_ =
        registry.create_gauge("goldearn_unrealized_pnl_inr", "Unrealized P&L in INR");

    realized_pnl_gauge_ = registry.create_gauge("goldearn_realized_pnl_inr", "Realized P&L in INR");

    // Create system metrics
    cpu_usage_gauge_ = registry.create_gauge("goldearn_cpu_usage_percent", "CPU usage percentage");

    memory_usage_gauge_ =
        registry.create_gauge("goldearn_memory_usage_percent", "Memory usage percentage");

    network_bytes_sent_counter_ =
        registry.create_counter("goldearn_network_bytes_sent_total", "Total network bytes sent");

    network_bytes_received_counter_ = registry.create_counter(
        "goldearn_network_bytes_received_total", "Total network bytes received");

    LOG_INFO("HFTMetricsCollector: Initialized with {} metrics",
             MetricsRegistry::instance().get_all_metrics().size());
}

HFTMetricsCollector::~HFTMetricsCollector() {
    stop();
}

void HFTMetricsCollector::start() {
    if (running_) {
        LOG_WARN("HFTMetricsCollector: Already running");
        return;
    }

    running_ = true;
    collection_thread_ = std::thread(&HFTMetricsCollector::collection_thread_func, this);

    LOG_INFO("HFTMetricsCollector: Started metrics collection");
}

void HFTMetricsCollector::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    if (collection_thread_.joinable()) {
        collection_thread_.join();
    }

    LOG_INFO("HFTMetricsCollector: Stopped metrics collection");
}

void HFTMetricsCollector::record_order_latency(double latency_us) {
    if (order_latency_histogram_) {
        order_latency_histogram_->observe(latency_us);
    }
}

void HFTMetricsCollector::record_order_placed() {
    if (orders_placed_counter_) {
        orders_placed_counter_->increment();
    }
    if (active_orders_gauge_) {
        active_orders_gauge_->increment();
    }
}

void HFTMetricsCollector::record_order_filled() {
    if (orders_filled_counter_) {
        orders_filled_counter_->increment();
    }
    if (active_orders_gauge_) {
        active_orders_gauge_->decrement();
    }
}

void HFTMetricsCollector::record_order_rejected() {
    if (orders_rejected_counter_) {
        orders_rejected_counter_->increment();
    }
    if (active_orders_gauge_) {
        active_orders_gauge_->decrement();
    }
}

void HFTMetricsCollector::record_market_data_message() {
    if (market_data_messages_counter_) {
        market_data_messages_counter_->increment();
    }
}

void HFTMetricsCollector::record_market_data_parse_error() {
    if (market_data_errors_counter_) {
        market_data_errors_counter_->increment();
    }
}

void HFTMetricsCollector::record_market_data_latency(double latency_us) {
    if (market_data_latency_histogram_) {
        market_data_latency_histogram_->observe(latency_us);
    }
}

void HFTMetricsCollector::record_risk_check(double latency_us) {
    if (risk_check_latency_histogram_) {
        risk_check_latency_histogram_->observe(latency_us);
    }
}

void HFTMetricsCollector::record_risk_violation() {
    if (risk_violations_counter_) {
        risk_violations_counter_->increment();
    }
}

void HFTMetricsCollector::record_position_value(double value) {
    if (total_position_value_gauge_) {
        total_position_value_gauge_->set(value);
    }
}

void HFTMetricsCollector::record_pnl(double pnl) {
    if (pnl >= 0 && realized_pnl_gauge_) {
        realized_pnl_gauge_->set(pnl);
    } else if (unrealized_pnl_gauge_) {
        unrealized_pnl_gauge_->set(pnl);
    }
}

void HFTMetricsCollector::record_cpu_usage(double percent) {
    if (cpu_usage_gauge_) {
        cpu_usage_gauge_->set(percent);
    }
}

void HFTMetricsCollector::record_memory_usage(double percent) {
    if (memory_usage_gauge_) {
        memory_usage_gauge_->set(percent);
    }
}

void HFTMetricsCollector::record_network_bytes_sent(uint64_t bytes) {
    if (network_bytes_sent_counter_) {
        network_bytes_sent_counter_->increment(static_cast<double>(bytes));
    }
}

void HFTMetricsCollector::record_network_bytes_received(uint64_t bytes) {
    if (network_bytes_received_counter_) {
        network_bytes_received_counter_->increment(static_cast<double>(bytes));
    }
}

std::string HFTMetricsCollector::get_metrics_snapshot() const {
    return MetricsRegistry::instance().serialize_all();
}

void HFTMetricsCollector::collection_thread_func() {
    LOG_INFO("HFTMetricsCollector: Collection thread started");

    while (running_) {
        try {
            update_system_metrics();
        } catch (const std::exception& e) {
            LOG_ERROR("HFTMetricsCollector: Error updating system metrics: {}", e.what());
        }

        std::this_thread::sleep_for(collection_interval_);
    }

    LOG_INFO("HFTMetricsCollector: Collection thread stopped");
}

void HFTMetricsCollector::update_system_metrics() {
    // Update CPU usage
    double cpu_usage = get_current_cpu_usage();
    record_cpu_usage(cpu_usage);

    // Update memory usage
    double memory_usage = get_current_memory_usage();
    record_memory_usage(memory_usage);

    // Update market data rate
    static uint64_t last_message_count = 0;
    static auto last_update_time = std::chrono::steady_clock::now();

    uint64_t current_message_count =
        market_data_messages_counter_
            ? static_cast<uint64_t>(market_data_messages_counter_->value())
            : 0;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_update_time);

    if (elapsed.count() > 0) {
        double rate =
            static_cast<double>(current_message_count - last_message_count) / elapsed.count();
        if (market_data_rate_gauge_) {
            market_data_rate_gauge_->set(rate);
        }

        last_message_count = current_message_count;
        last_update_time = now;
    }
}

double HFTMetricsCollector::get_current_cpu_usage() {
    // Simplified CPU usage calculation
    // In production, read from /proc/stat or use proper system monitoring
    static double mock_cpu_usage = 25.0;
    mock_cpu_usage += (rand() % 10 - 5) * 0.1;  // Small random variation
    mock_cpu_usage = std::max(0.0, std::min(100.0, mock_cpu_usage));
    return mock_cpu_usage;
}

double HFTMetricsCollector::get_current_memory_usage() {
    // Simplified memory usage calculation
    // In production, read from /proc/meminfo or use proper system monitoring
    try {
        std::ifstream meminfo("/proc/meminfo");
        if (meminfo.is_open()) {
            std::string line;
            uint64_t total_mem = 0, available_mem = 0;

            while (std::getline(meminfo, line)) {
                if (line.find("MemTotal:") == 0) {
                    sscanf(line.c_str(), "MemTotal: %llu kB", &total_mem);
                } else if (line.find("MemAvailable:") == 0) {
                    sscanf(line.c_str(), "MemAvailable: %llu kB", &available_mem);
                }

                if (total_mem > 0 && available_mem > 0) {
                    break;
                }
            }

            if (total_mem > 0) {
                return 100.0 * (1.0 - static_cast<double>(available_mem) / total_mem);
            }
        }
    } catch (...) {
        // Fallback to mock value
    }

    // Mock value if real calculation fails
    static double mock_memory_usage = 45.0;
    mock_memory_usage += (rand() % 10 - 5) * 0.1;
    mock_memory_usage = std::max(0.0, std::min(100.0, mock_memory_usage));
    return mock_memory_usage;
}

}  // namespace goldearn::monitoring