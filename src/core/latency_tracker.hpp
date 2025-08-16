#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

namespace goldearn::core {

// High-resolution timing utilities
class LatencyTracker {
   public:
    using TimePoint = std::chrono::high_resolution_clock::time_point;
    using Duration = std::chrono::nanoseconds;

    static constexpr size_t MAX_SAMPLES = 10000;  // Ring buffer for samples

    // Statistics structure for compatibility with existing code
    struct LatencyStats {
        uint64_t count;
        double avg_latency_us;
        double min_latency_us;
        double p50_latency_us;
        double p95_latency_us;
        double p99_latency_us;
        double max_latency_us;
    };

    LatencyTracker(const std::string& name);
    ~LatencyTracker();

    // Timing measurement
    class ScopedTimer {
       public:
        ScopedTimer(LatencyTracker& tracker) : tracker_(tracker), start_(now()) {
        }
        ~ScopedTimer() {
            tracker_.record_latency(now() - start_);
        }

       private:
        LatencyTracker& tracker_;
        TimePoint start_;
    };

    // Manual timing
    void start_timing() {
        timing_start_ = now();
    }
    void end_timing() {
        record_latency(now() - timing_start_);
    }

    // Record external latency
    void record_latency(Duration latency);
    void record_latency_ns(uint64_t nanoseconds);

    // Statistics
    double get_mean_latency_ns() const;
    double get_median_latency_ns() const;
    double get_p95_latency_ns() const;
    double get_p99_latency_ns() const;
    double get_max_latency_ns() const;
    double get_min_latency_ns() const;

    // Sample count
    uint64_t get_sample_count() const {
        return sample_count_.load();
    }

    // Get comprehensive statistics
    LatencyStats get_stats() const;

    // Reset statistics
    void reset();

    // Utility for scoped timing
    ScopedTimer scoped_timer() {
        return ScopedTimer(*this);
    }

    // Static utility functions
    static TimePoint now() {
        return std::chrono::high_resolution_clock::now();
    }

    static uint64_t now_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();
    }

    static uint64_t to_nanoseconds(Duration duration) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }

   private:
    std::string name_;
    std::array<uint64_t, MAX_SAMPLES> samples_;
    std::atomic<size_t> write_index_;
    std::atomic<uint64_t> sample_count_;
    TimePoint start_time_;
    TimePoint timing_start_;  // For manual timing

    void update_statistics();
};

// System-wide latency monitoring
class LatencyMonitor {
   public:
    static LatencyMonitor& instance() {
        static LatencyMonitor instance;
        return instance;
    }

    // Tracker management
    LatencyTracker* create_tracker(const std::string& name);
    LatencyTracker* get_tracker(const std::string& name);
    void remove_tracker(const std::string& name);

    // Global statistics
    struct SystemLatencyStats {
        std::string component_name;
        double mean_ns;
        double p95_ns;
        double p99_ns;
        double max_ns;
        uint64_t sample_count;
    };

    std::vector<SystemLatencyStats> get_all_stats() const;
    void print_latency_report() const;

    // Warnings and alerts
    void set_warning_threshold_ns(const std::string& tracker_name, uint64_t threshold_ns);
    void check_thresholds() const;

   private:
    LatencyMonitor() = default;
    std::map<std::string, std::unique_ptr<LatencyTracker>> trackers_;
    std::map<std::string, uint64_t> warning_thresholds_;
    mutable std::shared_mutex mutex_;
};

// Critical path latency measurement macros
#define LATENCY_TRACKER_CREATE(name) goldearn::core::LatencyMonitor::instance().create_tracker(name)

#define LATENCY_MEASURE_SCOPE(tracker_name) \
    auto timer =                            \
        goldearn::core::LatencyMonitor::instance().get_tracker(tracker_name)->scoped_timer()

#define LATENCY_MEASURE_START(tracker_name) \
    goldearn::core::LatencyMonitor::instance().get_tracker(tracker_name)->start_timing()

#define LATENCY_MEASURE_END(tracker_name) \
    goldearn::core::LatencyMonitor::instance().get_tracker(tracker_name)->end_timing()

// TSC (Time Stamp Counter) based ultra-low latency timing
class TSCTimer {
   public:
    TSCTimer();

    // Calibrate TSC frequency
    void calibrate();

    // TSC-based timing (platform specific)
    uint64_t rdtsc() const {
#if defined(__x86_64__) || defined(__i386__)
        uint32_t lo, hi;
        __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
        return ((uint64_t)hi << 32) | lo;
#elif defined(__aarch64__)
        uint64_t val;
        __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(val));
        return val;
#else
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
#endif
    }

    uint64_t rdtscp() const {
#if defined(__x86_64__) || defined(__i386__)
        uint32_t lo, hi, aux;
        __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
        return ((uint64_t)hi << 32) | lo;
#elif defined(__aarch64__)
        uint64_t val;
        __asm__ __volatile__("isb; mrs %0, cntvct_el0" : "=r"(val));
        return val;
#else
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
#endif
    }

    // Convert TSC cycles to nanoseconds
    uint64_t cycles_to_ns(uint64_t cycles) const {
        return cycles * ns_per_cycle_;
    }

    // Get current time in nanoseconds (TSC-based)
    uint64_t now_ns() const {
        return cycles_to_ns(rdtsc());
    }

    // High-precision sleep
    void sleep_ns(uint64_t nanoseconds) const;

   private:
    double ns_per_cycle_;
    uint64_t tsc_frequency_;
};

// Memory fence and synchronization utilities for timing
namespace timing_sync {
inline void memory_fence() {
    __asm__ __volatile__("mfence" ::: "memory");
}

inline void load_fence() {
    __asm__ __volatile__("lfence" ::: "memory");
}

inline void store_fence() {
    __asm__ __volatile__("sfence" ::: "memory");
}

inline void compiler_fence() {
    __asm__ __volatile__("" ::: "memory");
}
}  // namespace timing_sync

}  // namespace goldearn::core