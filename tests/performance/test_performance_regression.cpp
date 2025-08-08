#include <gtest/gtest.h>
// #include <benchmark/benchmark.h> // Commented out - not available
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <random>
#include <fstream>
#include <algorithm>

#include "../../src/config/config_manager.hpp"
#include "../../src/network/exchange_auth.hpp"
#include "../../src/market_data/nse_protocol.hpp"
#include "../../src/monitoring/health_check.hpp"
#include "../../src/monitoring/prometheus_metrics.hpp"
#include "../../src/security/certificate_manager.hpp"
#include "../../src/utils/simple_logger.hpp"

using namespace goldearn;

// Performance benchmarks and regression tests
class PerformanceRegressionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize components for performance testing
        // ConfigManager requires factory method
        config_manager_ = config::ConfigManager::load_from_file("test_config.conf");
        nse_parser_ = std::make_unique<market_data::nse::NSEProtocolParser>();
        metrics_collector_ = std::make_unique<monitoring::HFTMetricsCollector>();
        
        // Create test configuration
        create_test_config();
        config_manager_->load_config("perf_test_config.json");
        
        // Setup performance baselines
        setup_performance_baselines();
    }
    
    void TearDown() override {
        std::remove("perf_test_config.json");
        std::remove("performance_results.json");
    }
    
    void create_test_config() {
        std::ofstream config_file("perf_test_config.json");
        config_file << R"({
            "performance": {
                "target_latency_us": 50,
                "max_latency_us": 100,
                "target_throughput_msgs_per_sec": 100000,
                "memory_limit_mb": 512
            }
        })";
        config_file.close();
    }
    
    void setup_performance_baselines() {
        target_latency_us_ = config_manager_->get_double("performance.target_latency_us");
        max_latency_us_ = config_manager_->get_double("performance.max_latency_us");
        target_throughput_ = config_manager_->get_double("performance.target_throughput_msgs_per_sec");
        memory_limit_mb_ = config_manager_->get_double("performance.memory_limit_mb");
    }
    
    // Measure latency in microseconds
    double measure_operation_latency(std::function<void()> operation) {
        auto start = std::chrono::high_resolution_clock::now();
        operation();
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        return static_cast<double>(duration.count()) / 1000.0; // Convert to microseconds
    }
    
    // Measure throughput (operations per second)
    double measure_throughput(std::function<void()> operation, std::chrono::seconds duration) {
        std::atomic<uint64_t> operation_count{0};
        
        auto start = std::chrono::steady_clock::now();
        auto end_time = start + duration;
        
        while (std::chrono::steady_clock::now() < end_time) {
            operation();
            operation_count++;
        }
        
        auto actual_duration = std::chrono::steady_clock::now() - start;
        double seconds = std::chrono::duration<double>(actual_duration).count();
        
        return static_cast<double>(operation_count.load()) / seconds;
    }
    
    // Check memory usage
    double get_memory_usage_mb() {
        // Simplified memory usage check
        // In production, use proper system monitoring
        std::ifstream status("/proc/self/status");
        std::string line;
        while (std::getline(status, line)) {
            if (line.find("VmRSS:") == 0) {
                std::istringstream iss(line);
                std::string key;
                double value;
                std::string unit;
                iss >> key >> value >> unit;
                
                if (unit == "kB") {
                    return value / 1024.0; // Convert to MB
                }
            }
        }
        return 0.0; // Fallback if cannot read
    }
    
    void save_performance_results(const std::string& test_name, const std::map<std::string, double>& results) {
        std::ofstream results_file("performance_results.json", std::ios::app);
        results_file << "{\n";
        results_file << "  \"test\": \"" << test_name << "\",\n";
        results_file << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() << ",\n";
        
        for (const auto& result : results) {
            results_file << "  \"" << result.first << "\": " << result.second << ",\n";
        }
        
        results_file << "  \"status\": \"completed\"\n";
        results_file << "}\n";
        results_file.close();
    }

protected:
    std::unique_ptr<config::ConfigManager> config_manager_;
    std::unique_ptr<market_data::nse::NSEProtocolParser> nse_parser_;
    std::unique_ptr<monitoring::HFTMetricsCollector> metrics_collector_;
    
    double target_latency_us_;
    double max_latency_us_;
    double target_throughput_;
    double memory_limit_mb_;
};

// Test configuration loading performance
TEST_F(PerformanceRegressionTest, ConfigurationLoadingPerformance) {
    const int num_iterations = 1000;
    std::vector<double> latencies;
    latencies.reserve(num_iterations);
    
    // Measure configuration loading latency
    for (int i = 0; i < num_iterations; ++i) {
        auto config_manager = std::make_unique<config::ConfigManager>();
        
        double latency = measure_operation_latency([&]() {
            config_manager->load_config("perf_test_config.json");
        });
        
        latencies.push_back(latency);
    }
    
    // Calculate statistics
    double avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    double max_latency = *std::max_element(latencies.begin(), latencies.end());
    double min_latency = *std::min_element(latencies.begin(), latencies.end());
    
    std::sort(latencies.begin(), latencies.end());
    double p95_latency = latencies[static_cast<size_t>(latencies.size() * 0.95)];
    double p99_latency = latencies[static_cast<size_t>(latencies.size() * 0.99)];
    
    // Performance assertions
    EXPECT_LT(avg_latency, 1000.0) << "Average config loading latency too high: " << avg_latency << "us";
    EXPECT_LT(p95_latency, 2000.0) << "P95 config loading latency too high: " << p95_latency << "us";
    EXPECT_LT(max_latency, 5000.0) << "Max config loading latency too high: " << max_latency << "us";
    
    // Save results
    std::map<std::string, double> results = {
        {"avg_latency_us", avg_latency},
        {"max_latency_us", max_latency},
        {"min_latency_us", min_latency},
        {"p95_latency_us", p95_latency},
        {"p99_latency_us", p99_latency}
    };
    save_performance_results("ConfigurationLoadingPerformance", results);
    
    LOG_INFO("Config loading performance: avg={}us, p95={}us, p99={}us, max={}us", 
             avg_latency, p95_latency, p99_latency, max_latency);
}

// Test market data processing performance
TEST_F(PerformanceRegressionTest, MarketDataProcessingPerformance) {
    const int num_messages = 100000;
    std::vector<double> processing_times;
    processing_times.reserve(num_messages);
    
    // Simulate market data messages
    for (int i = 0; i < num_messages; ++i) {
        double processing_time = measure_operation_latency([&]() {
            // Simulate message processing
            nse_parser_->increment_message_count();
        });
        
        processing_times.push_back(processing_time);
        
        // Collect metrics periodically to avoid overhead
        if (i % 1000 == 0) {
            metrics_collector_->record_market_data_message();
            metrics_collector_->record_market_data_latency(processing_time);
        }
    }
    
    // Calculate statistics
    double avg_processing_time = std::accumulate(processing_times.begin(), processing_times.end(), 0.0) / processing_times.size();
    double max_processing_time = *std::max_element(processing_times.begin(), processing_times.end());
    
    std::sort(processing_times.begin(), processing_times.end());
    double p95_processing_time = processing_times[static_cast<size_t>(processing_times.size() * 0.95)];
    double p99_processing_time = processing_times[static_cast<size_t>(processing_times.size() * 0.99)];
    
    // Performance assertions
    EXPECT_LT(avg_processing_time, target_latency_us_) << "Average processing time exceeds target: " << avg_processing_time << "us";
    EXPECT_LT(p99_processing_time, max_latency_us_) << "P99 processing time exceeds limit: " << p99_processing_time << "us";
    
    // Test throughput
    double throughput = measure_throughput([&]() {
        nse_parser_->increment_message_count();
    }, std::chrono::seconds(1));
    
    EXPECT_GT(throughput, target_throughput_) << "Throughput below target: " << throughput << " msgs/sec";
    
    // Save results
    std::map<std::string, double> results = {
        {"avg_processing_time_us", avg_processing_time},
        {"max_processing_time_us", max_processing_time},
        {"p95_processing_time_us", p95_processing_time},
        {"p99_processing_time_us", p99_processing_time},
        {"throughput_msgs_per_sec", throughput}
    };
    save_performance_results("MarketDataProcessingPerformance", results);
    
    LOG_INFO("Market data performance: avg={}us, p99={}us, throughput={} msgs/sec", 
             avg_processing_time, p99_processing_time, throughput);
}

// Test metrics collection performance
TEST_F(PerformanceRegressionTest, MetricsCollectionPerformance) {
    metrics_collector_->start();
    
    const int num_operations = 1000000;
    std::vector<double> metric_latencies;
    metric_latencies.reserve(num_operations);
    
    // Test different types of metrics
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> latency_dist(1.0, 100.0);
    
    for (int i = 0; i < num_operations; ++i) {
        double latency = latency_dist(gen);
        
        double metric_time = measure_operation_latency([&]() {
            switch (i % 4) {
                case 0:
                    metrics_collector_->record_order_latency(latency);
                    break;
                case 1:
                    metrics_collector_->record_market_data_message();
                    break;
                case 2:
                    metrics_collector_->record_risk_check(latency);
                    break;
                case 3:
                    metrics_collector_->record_order_placed();
                    break;
            }
        });
        
        metric_latencies.push_back(metric_time);
    }
    
    // Calculate statistics
    double avg_metric_time = std::accumulate(metric_latencies.begin(), metric_latencies.end(), 0.0) / metric_latencies.size();
    double max_metric_time = *std::max_element(metric_latencies.begin(), metric_latencies.end());
    
    std::sort(metric_latencies.begin(), metric_latencies.end());
    double p95_metric_time = metric_latencies[static_cast<size_t>(metric_latencies.size() * 0.95)];
    double p99_metric_time = metric_latencies[static_cast<size_t>(metric_latencies.size() * 0.99)];
    
    // Performance assertions for metrics collection
    EXPECT_LT(avg_metric_time, 1.0) << "Average metrics collection time too high: " << avg_metric_time << "us";
    EXPECT_LT(p99_metric_time, 10.0) << "P99 metrics collection time too high: " << p99_metric_time << "us";
    
    metrics_collector_->stop();
    
    // Save results
    std::map<std::string, double> results = {
        {"avg_metric_time_us", avg_metric_time},
        {"max_metric_time_us", max_metric_time},
        {"p95_metric_time_us", p95_metric_time},
        {"p99_metric_time_us", p99_metric_time}
    };
    save_performance_results("MetricsCollectionPerformance", results);
    
    LOG_INFO("Metrics collection performance: avg={}us, p99={}us", avg_metric_time, p99_metric_time);
}

// Test concurrent performance
TEST_F(PerformanceRegressionTest, ConcurrentPerformance) {
    const int num_threads = std::thread::hardware_concurrency();
    const int operations_per_thread = 100000;
    
    metrics_collector_->start();
    
    std::vector<std::thread> threads;
    std::vector<std::vector<double>> thread_latencies(num_threads);
    std::atomic<uint64_t> total_operations{0};
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Create worker threads
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            thread_latencies[t].reserve(operations_per_thread);
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> latency_dist(1.0, 100.0);
            
            for (int i = 0; i < operations_per_thread; ++i) {
                double latency = latency_dist(gen);
                
                double operation_time = measure_operation_latency([&]() {
                    // Mix of operations to simulate real load
                    switch (i % 6) {
                        case 0:
                            metrics_collector_->record_order_latency(latency);
                            break;
                        case 1:
                            metrics_collector_->record_market_data_message();
                            break;
                        case 2:
                            nse_parser_->increment_message_count();
                            break;
                        case 3:
                            metrics_collector_->record_risk_check(latency);
                            break;
                        case 4:
                            metrics_collector_->record_order_placed();
                            break;
                        case 5:
                            config_manager_->get_value("performance.target_latency_us");
                            break;
                    }
                });
                
                thread_latencies[t].push_back(operation_time);
                total_operations++;
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    // Aggregate results from all threads
    std::vector<double> all_latencies;
    for (const auto& thread_latency : thread_latencies) {
        all_latencies.insert(all_latencies.end(), thread_latency.begin(), thread_latency.end());
    }
    
    double avg_latency = std::accumulate(all_latencies.begin(), all_latencies.end(), 0.0) / all_latencies.size();
    double max_latency = *std::max_element(all_latencies.begin(), all_latencies.end());
    
    std::sort(all_latencies.begin(), all_latencies.end());
    double p95_latency = all_latencies[static_cast<size_t>(all_latencies.size() * 0.95)];
    double p99_latency = all_latencies[static_cast<size_t>(all_latencies.size() * 0.99)];
    
    double total_throughput = static_cast<double>(total_operations.load()) / 
                             (static_cast<double>(total_duration.count()) / 1000000.0);
    
    // Performance assertions
    EXPECT_LT(avg_latency, target_latency_us_ * 2) << "Concurrent avg latency too high: " << avg_latency << "us";
    EXPECT_LT(p99_latency, max_latency_us_ * 2) << "Concurrent P99 latency too high: " << p99_latency << "us";
    EXPECT_GT(total_throughput, target_throughput_ * 0.8) << "Concurrent throughput too low: " << total_throughput << " ops/sec";
    
    metrics_collector_->stop();
    
    // Save results
    std::map<std::string, double> results = {
        {"threads", static_cast<double>(num_threads)},
        {"avg_latency_us", avg_latency},
        {"max_latency_us", max_latency},
        {"p95_latency_us", p95_latency},
        {"p99_latency_us", p99_latency},
        {"total_throughput_ops_per_sec", total_throughput},
        {"total_operations", static_cast<double>(total_operations.load())}
    };
    save_performance_results("ConcurrentPerformance", results);
    
    LOG_INFO("Concurrent performance: {} threads, avg={}us, p99={}us, throughput={} ops/sec", 
             num_threads, avg_latency, p99_latency, total_throughput);
}

// Test memory usage and leaks
TEST_F(PerformanceRegressionTest, MemoryUsageTest) {
    double initial_memory = get_memory_usage_mb();
    
    // Perform memory-intensive operations
    const int num_iterations = 10000;
    std::vector<std::unique_ptr<config::ConfigManager>> configs;
    
    for (int i = 0; i < num_iterations; ++i) {
        auto config = std::make_unique<config::ConfigManager>();
        config->load_config("perf_test_config.json");
        
        // Keep some in memory to test memory usage
        if (i % 100 == 0) {
            configs.push_back(std::move(config));
        }
        
        // Record metrics
        metrics_collector_->record_order_latency(static_cast<double>(i % 100));
        nse_parser_->increment_message_count();
    }
    
    double peak_memory = get_memory_usage_mb();
    
    // Clear allocated objects
    configs.clear();
    
    // Force garbage collection (in real scenario, might need explicit cleanup)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    double final_memory = get_memory_usage_mb();
    double memory_growth = final_memory - initial_memory;
    
    // Memory assertions
    EXPECT_LT(peak_memory, memory_limit_mb_) << "Peak memory usage exceeds limit: " << peak_memory << "MB";
    EXPECT_LT(memory_growth, 50.0) << "Memory growth too high: " << memory_growth << "MB";
    
    // Save results
    std::map<std::string, double> results = {
        {"initial_memory_mb", initial_memory},
        {"peak_memory_mb", peak_memory},
        {"final_memory_mb", final_memory},
        {"memory_growth_mb", memory_growth}
    };
    save_performance_results("MemoryUsageTest", results);
    
    LOG_INFO("Memory usage: initial={}MB, peak={}MB, final={}MB, growth={}MB", 
             initial_memory, peak_memory, final_memory, memory_growth);
}

// Test system stability under sustained load
TEST_F(PerformanceRegressionTest, SustainedLoadTest) {
    const auto test_duration = std::chrono::minutes(1); // 1 minute sustained test
    const int target_ops_per_second = 10000;
    
    metrics_collector_->start();
    
    std::atomic<uint64_t> total_operations{0};
    std::atomic<bool> running{true};
    std::vector<double> latency_samples;
    std::mutex latency_mutex;
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Background operation thread
    std::thread operation_thread([&]() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> latency_dist(1.0, 100.0);
        
        auto next_sample_time = std::chrono::steady_clock::now();
        
        while (running.load()) {
            auto operation_start = std::chrono::high_resolution_clock::now();
            
            // Perform operations
            double latency = latency_dist(gen);
            metrics_collector_->record_order_latency(latency);
            metrics_collector_->record_market_data_message();
            nse_parser_->increment_message_count();
            
            auto operation_end = std::chrono::high_resolution_clock::now();
            double operation_time = std::chrono::duration<double, std::micro>(operation_end - operation_start).count();
            
            // Sample latency periodically
            if (std::chrono::steady_clock::now() >= next_sample_time) {
                std::lock_guard<std::mutex> lock(latency_mutex);
                latency_samples.push_back(operation_time);
                next_sample_time += std::chrono::milliseconds(100); // Sample every 100ms
            }
            
            total_operations++;
            
            // Control rate to avoid overwhelming system
            std::this_thread::sleep_for(std::chrono::microseconds(1000000 / target_ops_per_second));
        }
    });
    
    // Let the test run for the specified duration
    std::this_thread::sleep_for(test_duration);
    running.store(false);
    operation_thread.join();
    
    auto end_time = std::chrono::steady_clock::now();
    auto actual_duration = std::chrono::duration<double>(end_time - start_time);
    
    double actual_throughput = static_cast<double>(total_operations.load()) / actual_duration.count();
    
    // Analyze latency samples
    std::lock_guard<std::mutex> lock(latency_mutex);
    if (!latency_samples.empty()) {
        double avg_latency = std::accumulate(latency_samples.begin(), latency_samples.end(), 0.0) / latency_samples.size();
        double max_latency = *std::max_element(latency_samples.begin(), latency_samples.end());
        
        std::sort(latency_samples.begin(), latency_samples.end());
        double p95_latency = latency_samples[static_cast<size_t>(latency_samples.size() * 0.95)];
        
        // Performance assertions
        EXPECT_LT(avg_latency, target_latency_us_ * 3) << "Sustained load avg latency too high: " << avg_latency << "us";
        EXPECT_LT(p95_latency, max_latency_us_ * 2) << "Sustained load P95 latency too high: " << p95_latency << "us";
        
        LOG_INFO("Sustained load test: {}s duration, {} ops, {}ops/sec, avg={}us, p95={}us", 
                 actual_duration.count(), total_operations.load(), actual_throughput, avg_latency, p95_latency);
        
        // Save results
        std::map<std::string, double> results = {
            {"duration_seconds", actual_duration.count()},
            {"total_operations", static_cast<double>(total_operations.load())},
            {"throughput_ops_per_sec", actual_throughput},
            {"avg_latency_us", avg_latency},
            {"max_latency_us", max_latency},
            {"p95_latency_us", p95_latency}
        };
        save_performance_results("SustainedLoadTest", results);
    }
    
    metrics_collector_->stop();
    
    EXPECT_GT(actual_throughput, target_ops_per_second * 0.9) << "Sustained throughput too low: " << actual_throughput;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}