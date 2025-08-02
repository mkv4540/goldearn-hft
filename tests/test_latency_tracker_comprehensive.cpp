#include <gtest/gtest.h>
#include "../src/core/latency_tracker.hpp"
#include <thread>
#include <chrono>
#include <random>
#include <algorithm>
#include <vector>
#include <stdexcept>

using namespace goldearn::core;

class LatencyTrackerComprehensiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        tracker_ = std::make_unique<LatencyTracker>("test_tracker");
    }
    
    void TearDown() override {
        tracker_.reset();
    }
    
    std::unique_ptr<LatencyTracker> tracker_;
};

// Test LatencyTracker constructor - covers all initialization scenarios
TEST_F(LatencyTrackerComprehensiveTest, Constructor) {
    // Test normal construction
    auto tracker1 = std::make_unique<LatencyTracker>("normal_tracker");
    EXPECT_EQ(tracker1->get_sample_count(), 0UL);
    EXPECT_EQ(tracker1->get_mean_latency_ns(), 0.0);
    
    // Test with empty name
    auto tracker2 = std::make_unique<LatencyTracker>("");
    EXPECT_EQ(tracker2->get_sample_count(), 0UL);
    
    // Test with very long name
    std::string long_name(1000, 'a');
    auto tracker3 = std::make_unique<LatencyTracker>(long_name);
    EXPECT_EQ(tracker3->get_sample_count(), 0UL);
    
    // Test with special characters
    auto tracker4 = std::make_unique<LatencyTracker>("test!@#$%^&*()_+-={}[]|\\:;\"'<>?,./");
    EXPECT_EQ(tracker4->get_sample_count(), 0UL);
    
    // Test multiple instances
    std::vector<std::unique_ptr<LatencyTracker>> trackers;
    for (int i = 0; i < 100; ++i) {
        trackers.push_back(std::make_unique<LatencyTracker>("tracker_" + std::to_string(i)));
        EXPECT_EQ(trackers.back()->get_sample_count(), 0UL);
    }
}

// Test record_latency - covers all Duration recording scenarios
TEST_F(LatencyTrackerComprehensiveTest, RecordLatency) {
    // Test recording zero duration
    LatencyTracker::Duration zero_duration{0};
    tracker_->record_latency(zero_duration);
    EXPECT_EQ(tracker_->get_sample_count(), 1UL);
    EXPECT_EQ(tracker_->get_mean_latency_ns(), 0.0);
    
    // Test recording normal durations
    auto duration1 = std::chrono::nanoseconds(1000);
    auto duration2 = std::chrono::microseconds(2);
    auto duration3 = std::chrono::milliseconds(1);
    
    tracker_->record_latency(duration1);
    tracker_->record_latency(duration2);
    tracker_->record_latency(duration3);
    
    EXPECT_EQ(tracker_->get_sample_count(), 4UL); // Including zero duration
    
    // Test recording maximum duration
    auto max_duration = std::chrono::nanoseconds(UINT64_MAX);
    tracker_->record_latency(max_duration);
    EXPECT_EQ(tracker_->get_sample_count(), 5UL);
    
    // Test recording negative duration (should handle gracefully)
    auto negative_duration = std::chrono::nanoseconds(-1000);
    tracker_->record_latency(negative_duration);
    EXPECT_EQ(tracker_->get_sample_count(), 6UL);
    
    // Test batch recording
    for (int i = 0; i < 1000; ++i) {
        tracker_->record_latency(std::chrono::nanoseconds(i * 100));
    }
    EXPECT_EQ(tracker_->get_sample_count(), 1006UL);
    
    // Test ring buffer overflow
    for (size_t i = 0; i < LatencyTracker::MAX_SAMPLES + 500; ++i) {
        tracker_->record_latency(std::chrono::nanoseconds(i + 10000));
    }
    EXPECT_EQ(tracker_->get_sample_count(), LatencyTracker::MAX_SAMPLES);
    
    // Verify statistics are still valid after overflow
    EXPECT_GT(tracker_->get_mean_latency_ns(), 0.0);
    EXPECT_GT(tracker_->get_max_latency_ns(), 0.0);
}

// Test record_latency_ns - covers all nanosecond recording scenarios
TEST_F(LatencyTrackerComprehensiveTest, RecordLatencyNs) {
    // Test recording zero nanoseconds
    tracker_->record_latency_ns(0);
    EXPECT_EQ(tracker_->get_sample_count(), 1UL);
    EXPECT_EQ(tracker_->get_mean_latency_ns(), 0.0);
    EXPECT_EQ(tracker_->get_min_latency_ns(), 0.0);
    EXPECT_EQ(tracker_->get_max_latency_ns(), 0.0);
    
    // Test recording single nanosecond
    tracker_->record_latency_ns(1);
    EXPECT_EQ(tracker_->get_sample_count(), 2UL);
    EXPECT_EQ(tracker_->get_mean_latency_ns(), 0.5); // (0 + 1) / 2
    
    // Test recording microseconds in nanoseconds
    tracker_->record_latency_ns(1000); // 1 microsecond
    tracker_->record_latency_ns(2000); // 2 microseconds
    EXPECT_EQ(tracker_->get_sample_count(), 4UL);
    
    // Test recording milliseconds in nanoseconds
    tracker_->record_latency_ns(1000000);   // 1 millisecond
    tracker_->record_latency_ns(5000000);   // 5 milliseconds
    EXPECT_EQ(tracker_->get_sample_count(), 6UL);
    
    // Test recording seconds in nanoseconds
    tracker_->record_latency_ns(1000000000UL); // 1 second
    EXPECT_EQ(tracker_->get_sample_count(), 7UL);
    
    // Test recording maximum value
    tracker_->record_latency_ns(UINT64_MAX);
    EXPECT_EQ(tracker_->get_sample_count(), 8UL);
    EXPECT_EQ(tracker_->get_max_latency_ns(), UINT64_MAX);
    
    // Test rapid recording for performance
    auto start_time = LatencyTracker::now();
    for (int i = 0; i < 10000; ++i) {
        tracker_->record_latency_ns(i);
    }
    auto end_time = LatencyTracker::now();
    auto recording_overhead = LatencyTracker::to_nanoseconds(end_time - start_time);
    
    // Recording should be very fast (< 10ns per call on average)
    EXPECT_LT(recording_overhead / 10000, 100); // < 100ns per recording
    
    // Test concurrent recording
    std::vector<std::thread> threads;
    const int num_threads = 4;
    const int recordings_per_thread = 1000;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, t, recordings_per_thread]() {
            for (int i = 0; i < recordings_per_thread; ++i) {
                tracker_->record_latency_ns(t * 1000 + i);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Should handle concurrent access correctly
    size_t expected_min = 8 + num_threads * recordings_per_thread;
    size_t actual_count = tracker_->get_sample_count();
    EXPECT_GE(actual_count, std::min(expected_min, LatencyTracker::MAX_SAMPLES));
}

// Test start_timing - covers all timing start scenarios
TEST_F(LatencyTrackerComprehensiveTest, StartTiming) {
    // Test normal start timing
    tracker_->start_timing();
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    tracker_->end_timing();
    
    EXPECT_EQ(tracker_->get_sample_count(), 1UL);
    EXPECT_GT(tracker_->get_mean_latency_ns(), 8000.0); // At least 8 microseconds
    
    // Test multiple start/end cycles
    for (int i = 0; i < 10; ++i) {
        tracker_->start_timing();
        std::this_thread::sleep_for(std::chrono::microseconds(i + 1));
        tracker_->end_timing();
    }
    EXPECT_EQ(tracker_->get_sample_count(), 11UL);
    
    // Test start timing without end (should not crash)
    tracker_->start_timing();
    tracker_->start_timing(); // Second start should reset
    tracker_->end_timing();
    EXPECT_EQ(tracker_->get_sample_count(), 12UL);
    
    // Test rapid start/end cycles
    auto start = LatencyTracker::now();
    for (int i = 0; i < 1000; ++i) {
        tracker_->start_timing();
        // Immediate end for minimal latency
        tracker_->end_timing();
    }
    auto end = LatencyTracker::now();
    auto total_overhead = LatencyTracker::to_nanoseconds(end - start);
    
    EXPECT_EQ(tracker_->get_sample_count(), 1012UL);
    // Each start/end cycle should be very fast
    EXPECT_LT(total_overhead / 1000, 1000); // < 1μs per cycle
    
    // Test concurrent start_timing (stress test)
    std::vector<std::thread> timing_threads;
    std::atomic<int> completed_timings{0};
    
    for (int t = 0; t < 4; ++t) {
        timing_threads.emplace_back([this, &completed_timings]() {
            for (int i = 0; i < 100; ++i) {
                tracker_->start_timing();
                std::this_thread::sleep_for(std::chrono::nanoseconds(100));
                tracker_->end_timing();
                completed_timings.fetch_add(1);
            }
        });
    }
    
    for (auto& thread : timing_threads) {
        thread.join();
    }
    
    EXPECT_EQ(completed_timings.load(), 400);
    // Should handle concurrent timing correctly
}

// Test end_timing - covers all timing end scenarios
TEST_F(LatencyTrackerComprehensiveTest, EndTiming) {
    // Test end without start (should handle gracefully)
    tracker_->end_timing();
    EXPECT_EQ(tracker_->get_sample_count(), 0UL); // Should not record invalid timing
    
    // Test normal end after start
    tracker_->start_timing();
    std::this_thread::sleep_for(std::chrono::microseconds(5));
    tracker_->end_timing();
    EXPECT_EQ(tracker_->get_sample_count(), 1UL);
    
    // Test multiple ends after single start (subsequent ends should be ignored)
    tracker_->start_timing();
    std::this_thread::sleep_for(std::chrono::microseconds(1));
    tracker_->end_timing();
    tracker_->end_timing(); // Should be ignored
    tracker_->end_timing(); // Should be ignored
    EXPECT_EQ(tracker_->get_sample_count(), 2UL);
    
    // Test very short timing intervals
    for (int i = 0; i < 100; ++i) {
        tracker_->start_timing();
        // No sleep - measure minimal overhead
        tracker_->end_timing();
    }
    EXPECT_EQ(tracker_->get_sample_count(), 102UL);
    
    // All recorded latencies should be reasonable (< 10μs for minimal operations)
    EXPECT_LT(tracker_->get_max_latency_ns(), 10000.0);
    
    // Test timing precision with known intervals
    std::vector<uint64_t> expected_intervals = {1, 5, 10, 50, 100}; // microseconds
    
    for (uint64_t interval : expected_intervals) {
        tracker_->start_timing();
        std::this_thread::sleep_for(std::chrono::microseconds(interval));
        tracker_->end_timing();
    }
    
    // Should have recorded all intervals with reasonable accuracy
    EXPECT_GT(tracker_->get_mean_latency_ns(), 1000.0); // Should be > 1μs on average
    
    // Test rapid end timing calls
    tracker_->start_timing();
    auto rapid_start = LatencyTracker::now();
    for (int i = 0; i < 1000; ++i) {
        if (i == 0) {
            tracker_->end_timing();
        } else {
            tracker_->end_timing(); // Should be ignored after first
        }
    }
    auto rapid_end = LatencyTracker::now();
    auto rapid_overhead = LatencyTracker::to_nanoseconds(rapid_end - rapid_start);
    
    // Rapid calls should be very fast
    EXPECT_LT(rapid_overhead / 1000, 100); // < 100ns per call
}

// Test scoped_timer - covers all scoped timing scenarios
TEST_F(LatencyTrackerComprehensiveTest, ScopedTimer) {
    // Test basic scoped timing
    {
        auto timer = tracker_->scoped_timer();
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    } // Timer destructor should record latency
    
    EXPECT_EQ(tracker_->get_sample_count(), 1UL);
    EXPECT_GT(tracker_->get_mean_latency_ns(), 8000.0); // At least 8μs
    
    // Test nested scoped timers
    {
        auto timer1 = tracker_->scoped_timer();
        std::this_thread::sleep_for(std::chrono::microseconds(5));
        {
            auto timer2 = tracker_->scoped_timer();
            std::this_thread::sleep_for(std::chrono::microseconds(3));
        } // timer2 destructor
        std::this_thread::sleep_for(std::chrono::microseconds(2));
    } // timer1 destructor
    
    EXPECT_EQ(tracker_->get_sample_count(), 3UL);
    
    // Test scoped timer with early destruction
    {
        auto timer = tracker_->scoped_timer();
        std::this_thread::sleep_for(std::chrono::microseconds(1));
        timer.~ScopedTimer(); // Explicit destruction
        std::this_thread::sleep_for(std::chrono::microseconds(10)); // Should not be measured
    }
    
    EXPECT_EQ(tracker_->get_sample_count(), 4UL);
    
    // Test multiple scoped timers in sequence
    for (int i = 0; i < 10; ++i) {
        auto timer = tracker_->scoped_timer();
        std::this_thread::sleep_for(std::chrono::microseconds(i + 1));
    }
    
    EXPECT_EQ(tracker_->get_sample_count(), 14UL);
    
    // Test scoped timer with exceptions (should still record on stack unwind)
    try {
        auto timer = tracker_->scoped_timer();
        std::this_thread::sleep_for(std::chrono::microseconds(2));
        throw std::runtime_error("test exception");
    } catch (...) {
        // Exception caught, timer should have recorded latency
    }
    
    EXPECT_EQ(tracker_->get_sample_count(), 15UL);
    
    // Test scoped timer move semantics
    {
        auto timer1 = tracker_->scoped_timer();
        std::this_thread::sleep_for(std::chrono::microseconds(1));
        auto timer2 = std::move(timer1); // Move constructor
        std::this_thread::sleep_for(std::chrono::microseconds(2));
    } // Only timer2 should record (timer1 was moved)
    
    EXPECT_EQ(tracker_->get_sample_count(), 16UL);
    
    // Test concurrent scoped timers
    std::vector<std::thread> scoped_threads;
    std::atomic<int> completed_scoped{0};
    
    for (int t = 0; t < 4; ++t) {
        scoped_threads.emplace_back([this, &completed_scoped, t]() {
            for (int i = 0; i < 50; ++i) {
                auto timer = tracker_->scoped_timer();
                std::this_thread::sleep_for(std::chrono::nanoseconds(100 * (t + 1)));
                completed_scoped.fetch_add(1);
            }
        });
    }
    
    for (auto& thread : scoped_threads) {
        thread.join();
    }
    
    EXPECT_EQ(completed_scoped.load(), 200);
    EXPECT_GE(tracker_->get_sample_count(), 216UL); // 16 + 200
}

// Test get_sample_count - covers all sample counting scenarios
TEST_F(LatencyTrackerComprehensiveTest, GetSampleCount) {
    // Test empty tracker
    EXPECT_EQ(tracker_->get_sample_count(), 0UL);
    
    // Test single sample
    tracker_->record_latency_ns(1000);
    EXPECT_EQ(tracker_->get_sample_count(), 1UL);
    
    // Test multiple samples
    for (int i = 0; i < 100; ++i) {
        tracker_->record_latency_ns(i * 1000);
    }
    EXPECT_EQ(tracker_->get_sample_count(), 101UL);
    
    // Test ring buffer behavior - before overflow
    for (size_t i = 101; i < LatencyTracker::MAX_SAMPLES; ++i) {
        tracker_->record_latency_ns(i);
    }
    EXPECT_EQ(tracker_->get_sample_count(), LatencyTracker::MAX_SAMPLES);
    
    // Test ring buffer behavior - after overflow
    for (int i = 0; i < 1000; ++i) {
        tracker_->record_latency_ns(i + 50000);
    }
    EXPECT_EQ(tracker_->get_sample_count(), LatencyTracker::MAX_SAMPLES); // Should cap at MAX_SAMPLES
    
    // Test with mixed recording methods
    auto tracker2 = std::make_unique<LatencyTracker>("mixed_tracker");
    
    tracker2->record_latency_ns(1000);                     // 1 sample
    tracker2->record_latency(std::chrono::microseconds(2)); // 2 samples
    {
        auto timer = tracker2->scoped_timer();
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }                                                       // 3 samples
    tracker2->start_timing();
    tracker2->end_timing();                                 // 4 samples
    
    EXPECT_EQ(tracker2->get_sample_count(), 4UL);
    
    // Test after reset
    tracker_->reset();
    EXPECT_EQ(tracker_->get_sample_count(), 0UL);
    
    // Test concurrent sample counting
    std::vector<std::thread> counting_threads;
    const int recordings_per_thread = 1000;
    const int num_threads = 4;
    
    for (int t = 0; t < num_threads; ++t) {
        counting_threads.emplace_back([this, recordings_per_thread]() {
            for (int i = 0; i < recordings_per_thread; ++i) {
                tracker_->record_latency_ns(i);
            }
        });
    }
    
    for (auto& thread : counting_threads) {
        thread.join();
    }
    
    size_t expected_count = std::min(static_cast<size_t>(num_threads * recordings_per_thread), 
                                   LatencyTracker::MAX_SAMPLES);
    EXPECT_EQ(tracker_->get_sample_count(), expected_count);
}

// Test get_mean_latency_ns - covers all mean calculation scenarios
TEST_F(LatencyTrackerComprehensiveTest, GetMeanLatencyNs) {
    // Test empty tracker
    EXPECT_EQ(tracker_->get_mean_latency_ns(), 0.0);
    
    // Test single sample
    tracker_->record_latency_ns(1000);
    EXPECT_EQ(tracker_->get_mean_latency_ns(), 1000.0);
    
    // Test two samples
    tracker_->record_latency_ns(2000);
    EXPECT_EQ(tracker_->get_mean_latency_ns(), 1500.0); // (1000 + 2000) / 2
    
    // Test known sequence
    tracker_->reset();
    std::vector<uint64_t> values = {100, 200, 300, 400, 500};
    for (uint64_t value : values) {
        tracker_->record_latency_ns(value);
    }
    EXPECT_EQ(tracker_->get_mean_latency_ns(), 300.0); // Sum = 1500, Count = 5
    
    // Test with zeros
    tracker_->reset();
    tracker_->record_latency_ns(0);
    tracker_->record_latency_ns(0);
    tracker_->record_latency_ns(1000);
    EXPECT_EQ(tracker_->get_mean_latency_ns(), 1000.0 / 3.0);
    
    // Test large values
    tracker_->reset();
    tracker_->record_latency_ns(UINT64_MAX / 2);
    tracker_->record_latency_ns(UINT64_MAX / 2);
    EXPECT_EQ(tracker_->get_mean_latency_ns(), UINT64_MAX / 2.0);
    
    // Test precision with small values
    tracker_->reset();
    for (int i = 1; i <= 10; ++i) {
        tracker_->record_latency_ns(i);
    }
    EXPECT_DOUBLE_EQ(tracker_->get_mean_latency_ns(), 5.5); // (1+2+...+10)/10 = 55/10 = 5.5
    
    // Test ring buffer overflow behavior
    tracker_->reset();
    // Fill beyond ring buffer capacity
    for (size_t i = 0; i < LatencyTracker::MAX_SAMPLES + 1000; ++i) {
        tracker_->record_latency_ns(i);
    }
    
    // Mean should be calculated only from most recent MAX_SAMPLES
    double mean = tracker_->get_mean_latency_ns();
    EXPECT_GT(mean, 0.0);
    EXPECT_LT(mean, static_cast<double>(LatencyTracker::MAX_SAMPLES + 1000));
    
    // Test floating point precision
    tracker_->reset();
    tracker_->record_latency_ns(1);
    tracker_->record_latency_ns(2);
    tracker_->record_latency_ns(3);
    EXPECT_DOUBLE_EQ(tracker_->get_mean_latency_ns(), 2.0);
    
    // Test after operations that might affect mean
    tracker_->reset();
    for (int i = 0; i < 100; ++i) {
        tracker_->record_latency_ns(1000); // All same value
    }
    EXPECT_EQ(tracker_->get_mean_latency_ns(), 1000.0);
    
    // Add one different value
    tracker_->record_latency_ns(2000);
    double expected_mean = (100.0 * 1000.0 + 2000.0) / 101.0;
    EXPECT_DOUBLE_EQ(tracker_->get_mean_latency_ns(), expected_mean);
}

// Test get_median_latency_ns - covers all median calculation scenarios
TEST_F(LatencyTrackerComprehensiveTest, GetMedianLatencyNs) {
    // Test empty tracker
    EXPECT_EQ(tracker_->get_median_latency_ns(), 0.0);
    
    // Test single sample
    tracker_->record_latency_ns(1000);
    EXPECT_EQ(tracker_->get_median_latency_ns(), 1000.0);
    
    // Test two samples (median of even count)
    tracker_->record_latency_ns(2000);
    EXPECT_EQ(tracker_->get_median_latency_ns(), 1500.0); // (1000 + 2000) / 2
    
    // Test three samples (median of odd count)
    tracker_->record_latency_ns(3000);
    EXPECT_EQ(tracker_->get_median_latency_ns(), 2000.0); // Middle value
    
    // Test ordered sequence
    tracker_->reset();
    std::vector<uint64_t> ordered = {100, 200, 300, 400, 500};
    for (uint64_t value : ordered) {
        tracker_->record_latency_ns(value);
    }
    EXPECT_EQ(tracker_->get_median_latency_ns(), 300.0); // Middle value
    
    // Test unordered sequence (should sort internally)
    tracker_->reset();
    std::vector<uint64_t> unordered = {500, 100, 400, 200, 300};
    for (uint64_t value : unordered) {
        tracker_->record_latency_ns(value);
    }
    EXPECT_EQ(tracker_->get_median_latency_ns(), 300.0); // Still middle value after sorting
    
    // Test with duplicates
    tracker_->reset();
    std::vector<uint64_t> duplicates = {100, 200, 200, 200, 300};
    for (uint64_t value : duplicates) {
        tracker_->record_latency_ns(value);
    }
    EXPECT_EQ(tracker_->get_median_latency_ns(), 200.0); // Middle value with duplicates
    
    // Test large dataset (even count)
    tracker_->reset();
    for (int i = 0; i < 1000; ++i) {
        tracker_->record_latency_ns(i);
    }
    EXPECT_EQ(tracker_->get_median_latency_ns(), 499.5); // (499 + 500) / 2
    
    // Test large dataset (odd count)
    tracker_->record_latency_ns(1000);
    EXPECT_EQ(tracker_->get_median_latency_ns(), 500.0); // Middle of 1001 values
    
    // Test extreme values
    tracker_->reset();
    tracker_->record_latency_ns(1);
    tracker_->record_latency_ns(UINT64_MAX);
    tracker_->record_latency_ns(1000);
    EXPECT_EQ(tracker_->get_median_latency_ns(), 1000.0); // Middle value
    
    // Test all same values
    tracker_->reset();
    for (int i = 0; i < 100; ++i) {
        tracker_->record_latency_ns(5000);
    }
    EXPECT_EQ(tracker_->get_median_latency_ns(), 5000.0);
    
    // Test ring buffer overflow impact on median
    tracker_->reset();
    // Add more than MAX_SAMPLES
    for (size_t i = 0; i < LatencyTracker::MAX_SAMPLES + 500; ++i) {
        tracker_->record_latency_ns(i);
    }
    
    double median = tracker_->get_median_latency_ns();
    EXPECT_GT(median, 0.0);
    // Median should be from the most recent MAX_SAMPLES only
    EXPECT_GT(median, 500.0); // Should be from newer samples
}

// Test get_p95_latency_ns - covers all 95th percentile calculation scenarios
TEST_F(LatencyTrackerComprehensiveTest, GetP95LatencyNs) {
    // Test empty tracker
    EXPECT_EQ(tracker_->get_p95_latency_ns(), 0.0);
    
    // Test insufficient samples (less than 20 for reliable P95)
    for (int i = 1; i <= 10; ++i) {
        tracker_->record_latency_ns(i * 100);
    }
    double p95_small = tracker_->get_p95_latency_ns();
    EXPECT_GT(p95_small, 0.0);
    
    // Test known dataset with 100 samples (0-99)
    tracker_->reset();
    for (int i = 0; i < 100; ++i) {
        tracker_->record_latency_ns(i);
    }
    double p95_100 = tracker_->get_p95_latency_ns();
    EXPECT_GE(p95_100, 94.0); // 95th percentile should be around 94-95
    EXPECT_LE(p95_100, 95.0);
    
    // Test with 1000 samples
    tracker_->reset();
    for (int i = 0; i < 1000; ++i) {
        tracker_->record_latency_ns(i);
    }
    double p95_1000 = tracker_->get_p95_latency_ns();
    EXPECT_GE(p95_1000, 949.0); // 95th percentile should be around 949-950
    EXPECT_LE(p95_1000, 951.0);
    
    // Test with random distribution
    tracker_->reset();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dis(1, 10000);
    
    std::vector<uint64_t> random_values;
    for (int i = 0; i < 1000; ++i) {
        uint64_t value = dis(gen);
        random_values.push_back(value);
        tracker_->record_latency_ns(value);
    }
    
    // Calculate expected P95 manually
    std::sort(random_values.begin(), random_values.end());
    size_t p95_index = static_cast<size_t>(0.95 * random_values.size());
    uint64_t expected_p95 = random_values[p95_index];
    
    double actual_p95 = tracker_->get_p95_latency_ns();
    EXPECT_NEAR(actual_p95, expected_p95, expected_p95 * 0.05); // Within 5% tolerance
    
    // Test with outliers
    tracker_->reset();
    // Add 95 normal values
    for (int i = 0; i < 95; ++i) {
        tracker_->record_latency_ns(1000);
    }
    // Add 5 outliers
    for (int i = 0; i < 5; ++i) {
        tracker_->record_latency_ns(100000);
    }
    
    double p95_outliers = tracker_->get_p95_latency_ns();
    EXPECT_GE(p95_outliers, 1000.0); // Should be at least the normal value
    EXPECT_LE(p95_outliers, 100000.0); // But not exceed the outlier
    
    // Test with all same values
    tracker_->reset();
    for (int i = 0; i < 100; ++i) {
        tracker_->record_latency_ns(5000);
    }
    EXPECT_EQ(tracker_->get_p95_latency_ns(), 5000.0);
    
    // Test monotonic increase
    tracker_->reset();
    for (int i = 1; i <= 100; ++i) {
        tracker_->record_latency_ns(i * 10);
    }
    double p95_mono = tracker_->get_p95_latency_ns();
    EXPECT_GE(p95_mono, 940.0); // Should be around 95th value * 10
    EXPECT_LE(p95_mono, 960.0);
}

// Test get_p99_latency_ns - covers all 99th percentile calculation scenarios
TEST_F(LatencyTrackerComprehensiveTest, GetP99LatencyNs) {
    // Test empty tracker
    EXPECT_EQ(tracker_->get_p99_latency_ns(), 0.0);
    
    // Test insufficient samples
    for (int i = 1; i <= 50; ++i) {
        tracker_->record_latency_ns(i * 100);
    }
    double p99_small = tracker_->get_p99_latency_ns();
    EXPECT_GT(p99_small, 0.0);
    
    // Test known dataset with 100 samples
    tracker_->reset();
    for (int i = 0; i < 100; ++i) {
        tracker_->record_latency_ns(i);
    }
    double p99_100 = tracker_->get_p99_latency_ns();
    EXPECT_GE(p99_100, 98.0); // 99th percentile should be around 98-99
    EXPECT_LE(p99_100, 99.0);
    
    // Test with 1000 samples
    tracker_->reset();
    for (int i = 0; i < 1000; ++i) {
        tracker_->record_latency_ns(i);
    }
    double p99_1000 = tracker_->get_p99_latency_ns();
    EXPECT_GE(p99_1000, 989.0); // 99th percentile should be around 989-990
    EXPECT_LE(p99_1000, 991.0);
    
    // Test relationship: P99 >= P95
    tracker_->reset();
    for (int i = 0; i < 500; ++i) {
        tracker_->record_latency_ns(i);
    }
    double p95 = tracker_->get_p95_latency_ns();
    double p99 = tracker_->get_p99_latency_ns();
    EXPECT_GE(p99, p95);
    
    // Test with extreme outliers
    tracker_->reset();
    // Add 990 normal values
    for (int i = 0; i < 990; ++i) {
        tracker_->record_latency_ns(1000);
    }
    // Add 10 extreme outliers
    for (int i = 0; i < 10; ++i) {
        tracker_->record_latency_ns(1000000);
    }
    
    double p99_extreme = tracker_->get_p99_latency_ns();
    EXPECT_GE(p99_extreme, 1000.0);
    EXPECT_LE(p99_extreme, 1000000.0);
    
    // Test with gradual increase
    tracker_->reset();
    for (int i = 1; i <= 1000; ++i) {
        tracker_->record_latency_ns(i);
    }
    double p99_gradual = tracker_->get_p99_latency_ns();
    EXPECT_GE(p99_gradual, 990.0);
    EXPECT_LE(p99_gradual, 1000.0);
    
    // Test stability with additional samples
    double p99_before = tracker_->get_p99_latency_ns();
    // Add more samples in the middle range
    for (int i = 500; i <= 600; ++i) {
        tracker_->record_latency_ns(i);
    }
    double p99_after = tracker_->get_p99_latency_ns();
    
    // P99 should remain relatively stable
    EXPECT_NEAR(p99_after, p99_before, p99_before * 0.1); // Within 10%
}

// Test get_max_latency_ns - covers all maximum latency scenarios
TEST_F(LatencyTrackerComprehensiveTest, GetMaxLatencyNs) {
    // Test empty tracker
    EXPECT_EQ(tracker_->get_max_latency_ns(), 0.0);
    
    // Test single sample
    tracker_->record_latency_ns(1000);
    EXPECT_EQ(tracker_->get_max_latency_ns(), 1000.0);
    
    // Test increasing maximum
    tracker_->record_latency_ns(500);   // Lower than current max
    EXPECT_EQ(tracker_->get_max_latency_ns(), 1000.0); // Should remain 1000
    
    tracker_->record_latency_ns(1500);  // Higher than current max
    EXPECT_EQ(tracker_->get_max_latency_ns(), 1500.0); // Should update to 1500
    
    // Test with zero
    tracker_->record_latency_ns(0);
    EXPECT_EQ(tracker_->get_max_latency_ns(), 1500.0); // Should remain 1500
    
    // Test random sequence
    tracker_->reset();
    std::vector<uint64_t> values = {300, 100, 800, 200, 600, 1200, 400};
    uint64_t expected_max = 0;
    
    for (uint64_t value : values) {
        tracker_->record_latency_ns(value);
        expected_max = std::max(expected_max, value);
        EXPECT_EQ(tracker_->get_max_latency_ns(), expected_max);
    }
    
    // Test maximum value
    tracker_->record_latency_ns(UINT64_MAX);
    EXPECT_EQ(tracker_->get_max_latency_ns(), UINT64_MAX);
    
    // Test that max persists through ring buffer overflow
    tracker_->reset();
    tracker_->record_latency_ns(999999);  // Large value first
    
    // Add many smaller values to cause ring buffer overflow
    for (size_t i = 0; i < LatencyTracker::MAX_SAMPLES + 1000; ++i) {
        tracker_->record_latency_ns(i % 1000); // Much smaller values
    }
    
    // Max should still reflect the large value if it's within the ring buffer
    // Or should be the max from the current ring buffer contents
    double current_max = tracker_->get_max_latency_ns();
    EXPECT_GT(current_max, 0.0);
    
    // Test with all same values
    tracker_->reset();
    for (int i = 0; i < 100; ++i) {
        tracker_->record_latency_ns(5000);
    }
    EXPECT_EQ(tracker_->get_max_latency_ns(), 5000.0);
    
    // Test concurrent maximum updates
    tracker_->reset();
    std::vector<std::thread> max_threads;
    std::atomic<uint64_t> global_max{0};
    
    for (int t = 0; t < 4; ++t) {
        max_threads.emplace_back([this, &global_max, t]() {
            for (int i = 0; i < 1000; ++i) {
                uint64_t value = t * 10000 + i;
                tracker_->record_latency_ns(value);
                global_max.store(std::max(global_max.load(), value));
            }
        });
    }
    
    for (auto& thread : max_threads) {
        thread.join();
    }
    
    // The tracker's max should be at least as large as any individual thread's max
    EXPECT_GE(tracker_->get_max_latency_ns(), 30000.0); // At least from thread 3
}

// Test get_min_latency_ns - covers all minimum latency scenarios
TEST_F(LatencyTrackerComprehensiveTest, GetMinLatencyNs) {
    // Test empty tracker
    EXPECT_EQ(tracker_->get_min_latency_ns(), 0.0);
    
    // Test single sample
    tracker_->record_latency_ns(1000);
    EXPECT_EQ(tracker_->get_min_latency_ns(), 1000.0);
    
    // Test decreasing minimum
    tracker_->record_latency_ns(1500); // Higher than current min
    EXPECT_EQ(tracker_->get_min_latency_ns(), 1000.0); // Should remain 1000
    
    tracker_->record_latency_ns(500);  // Lower than current min
    EXPECT_EQ(tracker_->get_min_latency_ns(), 500.0); // Should update to 500
    
    // Test with zero
    tracker_->record_latency_ns(0);
    EXPECT_EQ(tracker_->get_min_latency_ns(), 0.0); // Should update to 0
    
    // Test random sequence
    tracker_->reset();
    std::vector<uint64_t> values = {300, 100, 800, 200, 600, 50, 400};
    uint64_t expected_min = UINT64_MAX;
    
    for (uint64_t value : values) {
        tracker_->record_latency_ns(value);
        expected_min = std::min(expected_min, value);
        EXPECT_EQ(tracker_->get_min_latency_ns(), expected_min);
    }
    
    // Test minimum value (zero)
    tracker_->record_latency_ns(0);
    EXPECT_EQ(tracker_->get_min_latency_ns(), 0.0);
    
    // Test that min persists appropriately through ring buffer
    tracker_->reset();
    tracker_->record_latency_ns(1);  // Very small value first
    
    // Add many larger values
    for (int i = 0; i < 1000; ++i) {
        tracker_->record_latency_ns(i + 1000); // Much larger values
    }
    
    // Min should reflect the current ring buffer contents
    double current_min = tracker_->get_min_latency_ns();
    EXPECT_GE(current_min, 0.0);
    
    // Test with all same values
    tracker_->reset();
    for (int i = 0; i < 100; ++i) {
        tracker_->record_latency_ns(5000);
    }
    EXPECT_EQ(tracker_->get_min_latency_ns(), 5000.0);
    
    // Test with large values then small
    tracker_->reset();
    tracker_->record_latency_ns(1000000);
    tracker_->record_latency_ns(999999);
    tracker_->record_latency_ns(1);        // Much smaller
    EXPECT_EQ(tracker_->get_min_latency_ns(), 1.0);
    
    // Test concurrent minimum updates
    tracker_->reset();
    std::vector<std::thread> min_threads;
    std::atomic<uint64_t> global_min{UINT64_MAX};
    
    for (int t = 0; t < 4; ++t) {
        min_threads.emplace_back([this, &global_min, t]() {
            for (int i = 0; i < 1000; ++i) {
                uint64_t value = t * 1000 + i + 1; // Start from 1 to avoid zero
                tracker_->record_latency_ns(value);
                
                uint64_t current_global = global_min.load();
                while (value < current_global && 
                       !global_min.compare_exchange_weak(current_global, value)) {
                    // Retry until successful
                }
            }
        });
    }
    
    for (auto& thread : min_threads) {
        thread.join();
    }
    
    // The tracker's min should be close to the theoretical minimum
    EXPECT_LE(tracker_->get_min_latency_ns(), 10.0); // Should be quite small
    EXPECT_GT(tracker_->get_min_latency_ns(), 0.0);  // But greater than zero
}

// Test reset - covers all reset scenarios
TEST_F(LatencyTrackerComprehensiveTest, Reset) {
    // Test reset on empty tracker
    tracker_->reset();
    EXPECT_EQ(tracker_->get_sample_count(), 0UL);
    EXPECT_EQ(tracker_->get_mean_latency_ns(), 0.0);
    EXPECT_EQ(tracker_->get_max_latency_ns(), 0.0);
    EXPECT_EQ(tracker_->get_min_latency_ns(), 0.0);
    
    // Test reset after adding samples
    for (int i = 0; i < 100; ++i) {
        tracker_->record_latency_ns(i * 1000);
    }
    
    EXPECT_EQ(tracker_->get_sample_count(), 100UL);
    EXPECT_GT(tracker_->get_mean_latency_ns(), 0.0);
    
    tracker_->reset();
    
    EXPECT_EQ(tracker_->get_sample_count(), 0UL);
    EXPECT_EQ(tracker_->get_mean_latency_ns(), 0.0);
    EXPECT_EQ(tracker_->get_max_latency_ns(), 0.0);
    EXPECT_EQ(tracker_->get_min_latency_ns(), 0.0);
    EXPECT_EQ(tracker_->get_median_latency_ns(), 0.0);
    EXPECT_EQ(tracker_->get_p95_latency_ns(), 0.0);
    EXPECT_EQ(tracker_->get_p99_latency_ns(), 0.0);
    
    // Test reset after ring buffer overflow
    for (size_t i = 0; i < LatencyTracker::MAX_SAMPLES + 1000; ++i) {
        tracker_->record_latency_ns(i);
    }
    
    EXPECT_EQ(tracker_->get_sample_count(), LatencyTracker::MAX_SAMPLES);
    
    tracker_->reset();
    
    EXPECT_EQ(tracker_->get_sample_count(), 0UL);
    
    // Test reset during timing operation
    tracker_->start_timing();
    tracker_->reset();
    
    // Should handle gracefully - subsequent end_timing should not record
    tracker_->end_timing();
    EXPECT_EQ(tracker_->get_sample_count(), 0UL);
    
    // Test functionality after reset
    tracker_->record_latency_ns(5000);
    EXPECT_EQ(tracker_->get_sample_count(), 1UL);
    EXPECT_EQ(tracker_->get_mean_latency_ns(), 5000.0);
    
    // Test multiple resets
    for (int r = 0; r < 10; ++r) {
        // Add some data
        for (int i = 0; i < 50; ++i) {
            tracker_->record_latency_ns(i * 100);
        }
        
        EXPECT_EQ(tracker_->get_sample_count(), 50UL);
        
        // Reset
        tracker_->reset();
        
        EXPECT_EQ(tracker_->get_sample_count(), 0UL);
    }
    
    // Test concurrent reset (stress test)
    std::vector<std::thread> reset_threads;
    std::atomic<int> reset_count{0};
    std::atomic<bool> stop_flag{false};
    
    // Thread that keeps adding data
    std::thread data_thread([this, &stop_flag]() {
        int counter = 0;
        while (!stop_flag.load()) {
            tracker_->record_latency_ns(counter++);
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    });
    
    // Threads that reset periodically
    for (int t = 0; t < 3; ++t) {
        reset_threads.emplace_back([this, &reset_count, &stop_flag]() {
            for (int i = 0; i < 10; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                tracker_->reset();
                reset_count.fetch_add(1);
            }
        });
    }
    
    for (auto& thread : reset_threads) {
        thread.join();
    }
    
    stop_flag.store(true);
    data_thread.join();
    
    EXPECT_EQ(reset_count.load(), 30); // 3 threads * 10 resets each
    
    // Final state should be valid
    EXPECT_GE(tracker_->get_sample_count(), 0UL);
    if (tracker_->get_sample_count() > 0) {
        EXPECT_GE(tracker_->get_mean_latency_ns(), 0.0);
    }
}

// Test static utility functions - covers all static method scenarios
TEST_F(LatencyTrackerComprehensiveTest, StaticUtilityFunctions) {
    // Test now() function
    auto time1 = LatencyTracker::now();
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    auto time2 = LatencyTracker::now();
    
    EXPECT_GT(time2, time1); // Time should progress
    
    // Test precision of now()
    std::vector<LatencyTracker::TimePoint> timestamps;
    for (int i = 0; i < 1000; ++i) {
        timestamps.push_back(LatencyTracker::now());
    }
    
    // All timestamps should be different (or at least mostly different)
    std::sort(timestamps.begin(), timestamps.end());
    auto unique_end = std::unique(timestamps.begin(), timestamps.end());
    size_t unique_count = std::distance(timestamps.begin(), unique_end);
    EXPECT_GT(unique_count, 990UL); // At least 99% should be unique
    
    // Test now_ns() function
    uint64_t ns1 = LatencyTracker::now_ns();
    std::this_thread::sleep_for(std::chrono::microseconds(5));
    uint64_t ns2 = LatencyTracker::now_ns();
    
    EXPECT_GT(ns2, ns1);
    EXPECT_GT(ns2 - ns1, 4000UL); // At least 4 microseconds difference
    
    // Test to_nanoseconds() function
    auto duration_1us = std::chrono::microseconds(1);
    auto duration_1ms = std::chrono::milliseconds(1);
    auto duration_1s = std::chrono::seconds(1);
    
    EXPECT_EQ(LatencyTracker::to_nanoseconds(duration_1us), 1000UL);
    EXPECT_EQ(LatencyTracker::to_nanoseconds(duration_1ms), 1000000UL);
    EXPECT_EQ(LatencyTracker::to_nanoseconds(duration_1s), 1000000000UL);
    
    // Test zero duration
    auto zero_duration = std::chrono::nanoseconds(0);
    EXPECT_EQ(LatencyTracker::to_nanoseconds(zero_duration), 0UL);
    
    // Test maximum duration
    auto max_duration = std::chrono::nanoseconds(UINT64_MAX);
    EXPECT_EQ(LatencyTracker::to_nanoseconds(max_duration), UINT64_MAX);
    
    // Test negative duration (implementation-defined behavior)
    auto negative_duration = std::chrono::nanoseconds(-1000);
    uint64_t negative_result = LatencyTracker::to_nanoseconds(negative_duration);
    // Result may vary by implementation, but should not crash
    
    // Test precision consistency
    auto precise_start = LatencyTracker::now();
    auto precise_ns_start = LatencyTracker::now_ns();
    
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    
    auto precise_end = LatencyTracker::now();
    auto precise_ns_end = LatencyTracker::now_ns();
    
    uint64_t duration_from_timepoint = LatencyTracker::to_nanoseconds(precise_end - precise_start);
    uint64_t duration_from_ns = precise_ns_end - precise_ns_start;
    
    // Should be approximately equal (within 10% due to timing variations)
    double ratio = static_cast<double>(duration_from_timepoint) / duration_from_ns;
    EXPECT_GT(ratio, 0.9);
    EXPECT_LT(ratio, 1.1);
    
    // Test rapid calls for performance
    auto rapid_start = LatencyTracker::now();
    for (int i = 0; i < 10000; ++i) {
        volatile auto dummy = LatencyTracker::now();
        (void)dummy; // Suppress unused variable warning
    }
    auto rapid_end = LatencyTracker::now();
    auto rapid_total = LatencyTracker::to_nanoseconds(rapid_end - rapid_start);
    
    // Each call should be very fast (< 100ns average)
    EXPECT_LT(rapid_total / 10000, 1000UL); // < 1μs per call
    
    // Test now_ns() performance
    auto rapid_ns_start = LatencyTracker::now_ns();
    for (int i = 0; i < 10000; ++i) {
        volatile uint64_t dummy = LatencyTracker::now_ns();
        (void)dummy;
    }
    auto rapid_ns_end = LatencyTracker::now_ns();
    auto rapid_ns_total = rapid_ns_end - rapid_ns_start;
    
    // now_ns() should also be very fast
    EXPECT_LT(rapid_ns_total / 10000, 1000UL); // < 1μs per call
}

// Test thread safety and concurrent access
TEST_F(LatencyTrackerComprehensiveTest, ThreadSafetyAndConcurrency) {
    const int num_threads = 8;
    const int operations_per_thread = 10000;
    std::vector<std::thread> threads;
    std::atomic<int> completed_operations{0};
    
    // Test concurrent record_latency_ns
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, &completed_operations, t, operations_per_thread]() {
            for (int i = 0; i < operations_per_thread; ++i) {
                tracker_->record_latency_ns(t * 1000 + i);
                completed_operations.fetch_add(1);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(completed_operations.load(), num_threads * operations_per_thread);
    
    // Sample count should be correct (capped at MAX_SAMPLES)
    size_t expected_samples = std::min(static_cast<size_t>(num_threads * operations_per_thread),
                                      LatencyTracker::MAX_SAMPLES);
    EXPECT_EQ(tracker_->get_sample_count(), expected_samples);
    
    // Statistics should be reasonable
    EXPECT_GT(tracker_->get_mean_latency_ns(), 0.0);
    EXPECT_GT(tracker_->get_max_latency_ns(), 0.0);
    EXPECT_GE(tracker_->get_min_latency_ns(), 0.0);
    
    // Test concurrent mixed operations
    tracker_->reset();
    threads.clear();
    std::atomic<int> timing_ops{0};
    std::atomic<int> scoped_ops{0};
    std::atomic<int> direct_ops{0};
    std::atomic<int> query_ops{0};
    
    // Thread 1: Manual timing
    threads.emplace_back([this, &timing_ops]() {
        for (int i = 0; i < 1000; ++i) {
            tracker_->start_timing();
            std::this_thread::sleep_for(std::chrono::nanoseconds(100));
            tracker_->end_timing();
            timing_ops.fetch_add(1);
        }
    });
    
    // Thread 2: Scoped timing
    threads.emplace_back([this, &scoped_ops]() {
        for (int i = 0; i < 1000; ++i) {
            auto timer = tracker_->scoped_timer();
            std::this_thread::sleep_for(std::chrono::nanoseconds(150));
            scoped_ops.fetch_add(1);
        }
    });
    
    // Thread 3: Direct recording
    threads.emplace_back([this, &direct_ops]() {
        for (int i = 0; i < 1000; ++i) {
            tracker_->record_latency_ns(i + 1000);
            direct_ops.fetch_add(1);
        }
    });
    
    // Thread 4: Statistics queries
    threads.emplace_back([this, &query_ops]() {
        for (int i = 0; i < 1000; ++i) {
            volatile double mean = tracker_->get_mean_latency_ns();
            volatile double max = tracker_->get_max_latency_ns();
            volatile double min = tracker_->get_min_latency_ns();
            volatile size_t count = tracker_->get_sample_count();
            (void)mean; (void)max; (void)min; (void)count;
            query_ops.fetch_add(1);
        }
    });
    
    // Thread 5: Periodic resets
    threads.emplace_back([this]() {
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            tracker_->reset();
        }
    });
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(timing_ops.load(), 1000);
    EXPECT_EQ(scoped_ops.load(), 1000);
    EXPECT_EQ(direct_ops.load(), 1000);
    EXPECT_EQ(query_ops.load(), 1000);
    
    // Final state should be valid (may be empty due to resets)
    EXPECT_GE(tracker_->get_sample_count(), 0UL);
    
    // Test data race detection (if running with thread sanitizer)
    // This test ensures no data races are detected
    std::atomic<bool> race_test_complete{false};
    
    std::thread race_writer([this, &race_test_complete]() {
        while (!race_test_complete.load()) {
            tracker_->record_latency_ns(12345);
        }
    });
    
    std::thread race_reader([this, &race_test_complete]() {
        for (int i = 0; i < 10000; ++i) {
            volatile double stats = tracker_->get_mean_latency_ns();
            (void)stats;
        }
        race_test_complete.store(true);
    });
    
    race_reader.join();
    race_writer.join();
    
    // Should complete without data race warnings from thread sanitizer
}

// Test performance characteristics under load
TEST_F(LatencyTrackerComprehensiveTest, PerformanceUnderLoad) {
    // Test latency distribution of the tracker itself
    std::vector<uint64_t> recording_latencies;
    recording_latencies.reserve(100000);
    
    for (int i = 0; i < 100000; ++i) {
        auto start = LatencyTracker::now();
        tracker_->record_latency_ns(i);
        auto end = LatencyTracker::now();
        
        recording_latencies.push_back(LatencyTracker::to_nanoseconds(end - start));
    }
    
    // Calculate statistics of recording latencies
    std::sort(recording_latencies.begin(), recording_latencies.end());
    
    uint64_t min_recording = recording_latencies.front();
    uint64_t max_recording = recording_latencies.back();
    uint64_t median_recording = recording_latencies[recording_latencies.size() / 2];
    uint64_t p95_recording = recording_latencies[static_cast<size_t>(0.95 * recording_latencies.size())];
    uint64_t p99_recording = recording_latencies[static_cast<size_t>(0.99 * recording_latencies.size())];
    
    // Performance requirements for HFT system
    EXPECT_LT(median_recording, 100UL);    // Median < 100ns
    EXPECT_LT(p95_recording, 500UL);       // P95 < 500ns
    EXPECT_LT(p99_recording, 1000UL);      // P99 < 1μs
    EXPECT_LT(max_recording, 10000UL);     // Max < 10μs
    
    // Test statistics calculation performance
    auto stats_start = LatencyTracker::now();
    for (int i = 0; i < 1000; ++i) {
        volatile double mean = tracker_->get_mean_latency_ns();
        volatile double median = tracker_->get_median_latency_ns();
        volatile double p95 = tracker_->get_p95_latency_ns();
        volatile double p99 = tracker_->get_p99_latency_ns();
        (void)mean; (void)median; (void)p95; (void)p99;
    }
    auto stats_end = LatencyTracker::now();
    auto stats_total = LatencyTracker::to_nanoseconds(stats_end - stats_start);
    
    // Statistics calculation should be reasonable fast
    EXPECT_LT(stats_total / 1000, 10000UL); // < 10μs per full statistics calculation
    
    // Test memory usage stability under load
    size_t initial_count = tracker_->get_sample_count();
    
    // Add many more samples
    for (size_t i = 0; i < LatencyTracker::MAX_SAMPLES * 5; ++i) {
        tracker_->record_latency_ns(i % 10000);
    }
    
    // Sample count should stabilize at MAX_SAMPLES
    EXPECT_EQ(tracker_->get_sample_count(), LatencyTracker::MAX_SAMPLES);
    
    // Statistics should still be fast after ring buffer is full
    auto full_stats_start = LatencyTracker::now();
    tracker_->get_p99_latency_ns(); // Most expensive calculation
    auto full_stats_end = LatencyTracker::now();
    auto full_stats_duration = LatencyTracker::to_nanoseconds(full_stats_end - full_stats_start);
    
    EXPECT_LT(full_stats_duration, 50000UL); // < 50μs even with full buffer
}

// Test edge cases and error conditions
TEST_F(LatencyTrackerComprehensiveTest, EdgeCasesAndErrorConditions) {
    // Test with extreme values
    tracker_->record_latency_ns(0);
    tracker_->record_latency_ns(1);
    tracker_->record_latency_ns(UINT64_MAX);
    
    EXPECT_EQ(tracker_->get_sample_count(), 3UL);
    EXPECT_EQ(tracker_->get_min_latency_ns(), 0.0);
    EXPECT_EQ(tracker_->get_max_latency_ns(), static_cast<double>(UINT64_MAX));
    
    // Test numerical stability with very large numbers
    tracker_->reset();
    for (int i = 0; i < 100; ++i) {
        tracker_->record_latency_ns(UINT64_MAX / 2);
    }
    
    double large_mean = tracker_->get_mean_latency_ns();
    EXPECT_NEAR(large_mean, static_cast<double>(UINT64_MAX / 2), 1.0);
    
    // Test with alternating small and large values
    tracker_->reset();
    for (int i = 0; i < 1000; ++i) {
        if (i % 2 == 0) {
            tracker_->record_latency_ns(1);
        } else {
            tracker_->record_latency_ns(1000000);
        }
    }
    
    double alternating_mean = tracker_->get_mean_latency_ns();
    EXPECT_NEAR(alternating_mean, 500000.5, 1000.0); // Should be approximately average
    
    // Test precision with very small differences
    tracker_->reset();
    for (int i = 0; i < 1000; ++i) {
        tracker_->record_latency_ns(1000 + i); // Very small increments
    }
    
    double precise_mean = tracker_->get_mean_latency_ns();
    double expected_precise = 1000.0 + 999.0 / 2.0; // 1000 + average of 0-999
    EXPECT_NEAR(precise_mean, expected_precise, 1.0);
    
    // Test rapid state changes
    for (int cycle = 0; cycle < 100; ++cycle) {
        tracker_->reset();
        
        // Add some data
        for (int i = 0; i < 10; ++i) {
            tracker_->record_latency_ns(cycle * 100 + i);
        }
        
        // Query statistics
        EXPECT_EQ(tracker_->get_sample_count(), 10UL);
        EXPECT_GT(tracker_->get_mean_latency_ns(), 0.0);
    }
    
    // Test with timing operations that might fail
    tracker_->reset();
    
    // Start timing but never end
    tracker_->start_timing();
    
    // Reset while timing is active
    tracker_->reset();
    
    // Try to end the timing (should handle gracefully)
    tracker_->end_timing();
    EXPECT_EQ(tracker_->get_sample_count(), 0UL);
    
    // Test multiple overlapping scoped timers (should each track independently)
    {
        auto timer1 = tracker_->scoped_timer();
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        {
            auto timer2 = tracker_->scoped_timer();
            std::this_thread::sleep_for(std::chrono::microseconds(5));
        } // timer2 ends
        std::this_thread::sleep_for(std::chrono::microseconds(5));
    } // timer1 ends
    
    EXPECT_EQ(tracker_->get_sample_count(), 2UL);
    
    // Both timings should be reasonable
    EXPECT_GT(tracker_->get_min_latency_ns(), 3000.0); // At least 3μs for the shorter one
    EXPECT_GT(tracker_->get_max_latency_ns(), 18000.0); // At least 18μs for the longer one
}