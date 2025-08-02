#include <gtest/gtest.h>
#include "../src/core/latency_tracker.hpp"
#include <thread>
#include <chrono>

using namespace goldearn::core;

class LatencyTrackerTest : public ::testing::Test {
protected:
    void SetUp() override {
        tracker_ = std::make_unique<LatencyTracker>("test_tracker");
    }
    
    void TearDown() override {
        tracker_.reset();
    }
    
    std::unique_ptr<LatencyTracker> tracker_;
};

TEST_F(LatencyTrackerTest, InitialState) {
    EXPECT_EQ(tracker_->get_sample_count(), 0UL);
    EXPECT_EQ(tracker_->get_mean_latency_ns(), 0.0);
    EXPECT_EQ(tracker_->get_max_latency_ns(), 0.0);
    EXPECT_EQ(tracker_->get_min_latency_ns(), 0.0);
}

TEST_F(LatencyTrackerTest, RecordSingleLatency) {
    uint64_t test_latency_ns = 1000; // 1 microsecond
    tracker_->record_latency_ns(test_latency_ns);
    
    EXPECT_EQ(tracker_->get_sample_count(), 1UL);
    EXPECT_EQ(tracker_->get_mean_latency_ns(), test_latency_ns);
    EXPECT_EQ(tracker_->get_max_latency_ns(), test_latency_ns);
    EXPECT_EQ(tracker_->get_min_latency_ns(), test_latency_ns);
    EXPECT_EQ(tracker_->get_median_latency_ns(), test_latency_ns);
}

TEST_F(LatencyTrackerTest, RecordMultipleLatencies) {
    std::vector<uint64_t> latencies = {500, 1000, 1500, 2000, 2500}; // ns
    
    for (uint64_t latency : latencies) {
        tracker_->record_latency_ns(latency);
    }
    
    EXPECT_EQ(tracker_->get_sample_count(), 5UL);
    EXPECT_EQ(tracker_->get_mean_latency_ns(), 1500.0); // Average
    EXPECT_EQ(tracker_->get_max_latency_ns(), 2500.0);
    EXPECT_EQ(tracker_->get_min_latency_ns(), 500.0);
    EXPECT_EQ(tracker_->get_median_latency_ns(), 1500.0); // Middle value
}

TEST_F(LatencyTrackerTest, PercentileCalculations) {
    // Add 100 samples with known distribution
    for (int i = 1; i <= 100; ++i) {
        tracker_->record_latency_ns(i * 10); // 10, 20, 30, ..., 1000 ns
    }
    
    EXPECT_EQ(tracker_->get_sample_count(), 100UL);
    
    // Check percentiles (approximate due to implementation details)
    double p95 = tracker_->get_p95_latency_ns();
    double p99 = tracker_->get_p99_latency_ns();
    
    EXPECT_GE(p95, 900.0); // Should be around 95th percentile
    EXPECT_LE(p95, 1000.0);
    EXPECT_GE(p99, 980.0); // Should be around 99th percentile
    EXPECT_LE(p99, 1000.0);
    EXPECT_GE(p99, p95); // P99 should be >= P95
}

TEST_F(LatencyTrackerTest, ScopedTimer) {
    {
        auto timer = tracker_->scoped_timer();
        std::this_thread::sleep_for(std::chrono::microseconds(10)); // 10 microseconds
    } // Timer destructor should record latency
    
    EXPECT_EQ(tracker_->get_sample_count(), 1UL);
    
    // Should have recorded roughly 10 microseconds (10,000 ns)
    // Allow some tolerance for timing precision
    double recorded_latency = tracker_->get_mean_latency_ns();
    EXPECT_GE(recorded_latency, 8000.0); // At least 8 microseconds
    EXPECT_LE(recorded_latency, 20000.0); // At most 20 microseconds
}

TEST_F(LatencyTrackerTest, ManualTiming) {
    tracker_->start_timing();
    std::this_thread::sleep_for(std::chrono::microseconds(5)); // 5 microseconds
    tracker_->end_timing();
    
    EXPECT_EQ(tracker_->get_sample_count(), 1UL);
    
    // Should have recorded roughly 5 microseconds
    double recorded_latency = tracker_->get_mean_latency_ns();
    EXPECT_GE(recorded_latency, 3000.0); // At least 3 microseconds
    EXPECT_LE(recorded_latency, 10000.0); // At most 10 microseconds
}

TEST_F(LatencyTrackerTest, ResetFunctionality) {
    // Add some samples
    for (int i = 0; i < 10; ++i) {
        tracker_->record_latency_ns(1000 + i * 100);
    }
    
    EXPECT_EQ(tracker_->get_sample_count(), 10UL);
    EXPECT_GT(tracker_->get_mean_latency_ns(), 0.0);
    
    // Reset and verify
    tracker_->reset();
    
    EXPECT_EQ(tracker_->get_sample_count(), 0UL);
    EXPECT_EQ(tracker_->get_mean_latency_ns(), 0.0);
    EXPECT_EQ(tracker_->get_max_latency_ns(), 0.0);
    EXPECT_EQ(tracker_->get_min_latency_ns(), 0.0);
}

TEST_F(LatencyTrackerTest, RingBufferBehavior) {
    // Add more samples than the ring buffer can hold
    const size_t samples_to_add = LatencyTracker::MAX_SAMPLES + 100;
    
    for (size_t i = 0; i < samples_to_add; ++i) {
        tracker_->record_latency_ns(1000 + i);
    }
    
    // Sample count should be capped at MAX_SAMPLES
    EXPECT_EQ(tracker_->get_sample_count(), LatencyTracker::MAX_SAMPLES);
    
    // Statistics should still be valid
    EXPECT_GT(tracker_->get_mean_latency_ns(), 0.0);
    EXPECT_GT(tracker_->get_max_latency_ns(), 0.0);
    EXPECT_GT(tracker_->get_min_latency_ns(), 0.0);
}

TEST_F(LatencyTrackerTest, StaticUtilityFunctions) {
    auto start_time = LatencyTracker::now();
    std::this_thread::sleep_for(std::chrono::microseconds(1));
    auto end_time = LatencyTracker::now();
    
    auto duration = end_time - start_time;
    uint64_t duration_ns = LatencyTracker::to_nanoseconds(duration);
    
    EXPECT_GT(duration_ns, 0UL);
    EXPECT_LT(duration_ns, 1000000UL); // Should be less than 1ms
    
    // Test now_ns function
    uint64_t current_time_ns = LatencyTracker::now_ns();
    EXPECT_GT(current_time_ns, 0UL);
}

// Test LatencyMonitor singleton
TEST(LatencyMonitorTest, SingletonBehavior) {
    auto& monitor1 = LatencyMonitor::instance();
    auto& monitor2 = LatencyMonitor::instance();
    
    // Should be the same instance
    EXPECT_EQ(&monitor1, &monitor2);
}

TEST(LatencyMonitorTest, TrackerManagement) {
    auto& monitor = LatencyMonitor::instance();
    
    // Create a tracker
    auto* tracker1 = monitor.create_tracker("test_tracker_1");
    EXPECT_NE(tracker1, nullptr);
    
    // Get the same tracker
    auto* tracker2 = monitor.get_tracker("test_tracker_1");
    EXPECT_EQ(tracker1, tracker2);
    
    // Create a different tracker
    auto* tracker3 = monitor.create_tracker("test_tracker_2");
    EXPECT_NE(tracker3, nullptr);
    EXPECT_NE(tracker1, tracker3);
    
    // Clean up
    monitor.remove_tracker("test_tracker_1");
    monitor.remove_tracker("test_tracker_2");
    
    // Should return nullptr after removal
    auto* tracker4 = monitor.get_tracker("test_tracker_1");
    EXPECT_EQ(tracker4, nullptr);
}

TEST(LatencyMonitorTest, GlobalStatistics) {
    auto& monitor = LatencyMonitor::instance();
    
    // Create trackers and add data
    auto* tracker1 = monitor.create_tracker("perf_test_1");
    auto* tracker2 = monitor.create_tracker("perf_test_2");
    
    // Add some latency data
    for (int i = 0; i < 100; ++i) {
        tracker1->record_latency_ns(1000 + i * 10);
        tracker2->record_latency_ns(2000 + i * 5);
    }
    
    // Get global statistics
    auto stats = monitor.get_all_stats();
    EXPECT_GE(stats.size(), 2UL);
    
    bool found_tracker1 = false, found_tracker2 = false;
    for (const auto& stat : stats) {
        if (stat.component_name == "perf_test_1") {
            found_tracker1 = true;
            EXPECT_EQ(stat.sample_count, 100UL);
            EXPECT_GT(stat.mean_ns, 0.0);
        }
        if (stat.component_name == "perf_test_2") {
            found_tracker2 = true;
            EXPECT_EQ(stat.sample_count, 100UL);
            EXPECT_GT(stat.mean_ns, 0.0);
        }
    }
    
    EXPECT_TRUE(found_tracker1);
    EXPECT_TRUE(found_tracker2);
    
    // Clean up
    monitor.remove_tracker("perf_test_1");
    monitor.remove_tracker("perf_test_2");
}

// Test TSC timer functionality (x86 specific)
#ifdef __x86_64__
TEST(TSCTimerTest, BasicFunctionality) {
    TSCTimer tsc_timer;
    tsc_timer.calibrate();
    
    // Test TSC reading
    uint64_t tsc1 = tsc_timer.rdtsc();
    std::this_thread::sleep_for(std::chrono::nanoseconds(100));
    uint64_t tsc2 = tsc_timer.rdtsc();
    
    EXPECT_GT(tsc2, tsc1); // TSC should increase
    
    // Test time conversion
    uint64_t cycles = tsc2 - tsc1;
    uint64_t nanoseconds = tsc_timer.cycles_to_ns(cycles);
    EXPECT_GT(nanoseconds, 0UL);
    
    // Test now_ns function
    uint64_t time_ns = tsc_timer.now_ns();
    EXPECT_GT(time_ns, 0UL);
}

TEST(TSCTimerTest, LatencyMeasurement) {
    TSCTimer tsc_timer;
    tsc_timer.calibrate();
    
    // Measure a very short operation
    uint64_t start = tsc_timer.rdtsc();
    volatile int dummy = 0; // Prevent optimization
    for (int i = 0; i < 100; ++i) {
        dummy += i;
    }
    uint64_t end = tsc_timer.rdtsc();
    
    uint64_t cycles = end - start;
    uint64_t nanoseconds = tsc_timer.cycles_to_ns(cycles);
    
    // Should be very fast (less than 1 microsecond)
    EXPECT_LT(nanoseconds, 1000UL);
    EXPECT_GT(nanoseconds, 0UL);
}
#endif

// Performance benchmark for latency tracking itself
TEST_F(LatencyTrackerTest, LatencyTrackerPerformance) {
    const size_t num_measurements = 100000;
    auto start_time = LatencyTracker::now();
    
    // Measure the overhead of latency tracking
    for (size_t i = 0; i < num_measurements; ++i) {
        tracker_->record_latency_ns(1000 + i % 1000);
    }
    
    auto end_time = LatencyTracker::now();
    auto total_duration_ns = LatencyTracker::to_nanoseconds(end_time - start_time);
    double avg_overhead_ns = static_cast<double>(total_duration_ns) / num_measurements;
    
    // Latency tracking itself should be very fast (target < 100ns per measurement)
    EXPECT_LT(avg_overhead_ns, 100.0);
    
    EXPECT_EQ(tracker_->get_sample_count(), std::min(num_measurements, LatencyTracker::MAX_SAMPLES));
}