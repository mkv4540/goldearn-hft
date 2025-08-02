#include "latency_tracker.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <thread>
#include <mutex>

namespace goldearn::core {

LatencyTracker::LatencyTracker(const std::string& name) 
    : name_(name), write_index_(0), sample_count_(0) {
    samples_.fill(0);
}

LatencyTracker::~LatencyTracker() = default;

void LatencyTracker::record_latency(Duration latency) {
    uint64_t latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(latency).count();
    record_latency_ns(latency_ns);
}

void LatencyTracker::record_latency_ns(uint64_t nanoseconds) {
    size_t current_index = write_index_.fetch_add(1) % MAX_SAMPLES;
    samples_[current_index] = nanoseconds;
    
    uint64_t current_count = sample_count_.load();
    if (current_count < MAX_SAMPLES) {
        sample_count_.fetch_add(1);
    }
}


double LatencyTracker::get_mean_latency_ns() const {
    uint64_t count = sample_count_.load();
    if (count == 0) return 0.0;
    
    uint64_t sum = 0;
    size_t samples_to_read = std::min(count, static_cast<uint64_t>(MAX_SAMPLES));
    
    for (size_t i = 0; i < samples_to_read; ++i) {
        sum += samples_[i];
    }
    
    return static_cast<double>(sum) / samples_to_read;
}

double LatencyTracker::get_median_latency_ns() const {
    uint64_t count = sample_count_.load();
    if (count == 0) return 0.0;
    
    size_t samples_to_read = std::min(count, static_cast<uint64_t>(MAX_SAMPLES));
    std::vector<uint64_t> sorted_samples(samples_to_read);
    
    for (size_t i = 0; i < samples_to_read; ++i) {
        sorted_samples[i] = samples_[i];
    }
    
    std::sort(sorted_samples.begin(), sorted_samples.end());
    
    if (sorted_samples.size() % 2 == 0) {
        size_t mid1 = sorted_samples.size() / 2 - 1;
        size_t mid2 = sorted_samples.size() / 2;
        return (sorted_samples[mid1] + sorted_samples[mid2]) / 2.0;
    } else {
        return sorted_samples[sorted_samples.size() / 2];
    }
}

double LatencyTracker::get_p95_latency_ns() const {
    uint64_t count = sample_count_.load();
    if (count == 0) return 0.0;
    
    size_t samples_to_read = std::min(count, static_cast<uint64_t>(MAX_SAMPLES));
    std::vector<uint64_t> sorted_samples(samples_to_read);
    
    for (size_t i = 0; i < samples_to_read; ++i) {
        sorted_samples[i] = samples_[i];
    }
    
    std::sort(sorted_samples.begin(), sorted_samples.end());
    
    size_t p95_index = static_cast<size_t>(0.95 * sorted_samples.size());
    if (p95_index >= sorted_samples.size()) {
        p95_index = sorted_samples.size() - 1;
    }
    
    return sorted_samples[p95_index];
}

double LatencyTracker::get_p99_latency_ns() const {
    uint64_t count = sample_count_.load();
    if (count == 0) return 0.0;
    
    size_t samples_to_read = std::min(count, static_cast<uint64_t>(MAX_SAMPLES));
    std::vector<uint64_t> sorted_samples(samples_to_read);
    
    for (size_t i = 0; i < samples_to_read; ++i) {
        sorted_samples[i] = samples_[i];
    }
    
    std::sort(sorted_samples.begin(), sorted_samples.end());
    
    size_t p99_index = static_cast<size_t>(0.99 * sorted_samples.size());
    if (p99_index >= sorted_samples.size()) {
        p99_index = sorted_samples.size() - 1;
    }
    
    return sorted_samples[p99_index];
}

double LatencyTracker::get_max_latency_ns() const {
    uint64_t count = sample_count_.load();
    if (count == 0) return 0.0;
    
    size_t samples_to_read = std::min(count, static_cast<uint64_t>(MAX_SAMPLES));
    uint64_t max_val = 0;
    
    for (size_t i = 0; i < samples_to_read; ++i) {
        max_val = std::max(max_val, samples_[i]);
    }
    
    return static_cast<double>(max_val);
}

double LatencyTracker::get_min_latency_ns() const {
    uint64_t count = sample_count_.load();
    if (count == 0) return 0.0;
    
    size_t samples_to_read = std::min(count, static_cast<uint64_t>(MAX_SAMPLES));
    uint64_t min_val = UINT64_MAX;
    
    for (size_t i = 0; i < samples_to_read; ++i) {
        min_val = std::min(min_val, samples_[i]);
    }
    
    return static_cast<double>(min_val);
}

void LatencyTracker::reset() {
    write_index_.store(0);
    sample_count_.store(0);
    samples_.fill(0);
    start_time_ = TimePoint{};
}

// LatencyMonitor implementation
LatencyTracker* LatencyMonitor::create_tracker(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto tracker = std::make_unique<LatencyTracker>(name);
    auto* tracker_ptr = tracker.get();
    trackers_[name] = std::move(tracker);
    
    return tracker_ptr;
}

LatencyTracker* LatencyMonitor::get_tracker(const std::string& name) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto it = trackers_.find(name);
    if (it != trackers_.end()) {
        return it->second.get();
    }
    
    return nullptr;
}

void LatencyMonitor::remove_tracker(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    trackers_.erase(name);
}

std::vector<LatencyMonitor::SystemLatencyStats> LatencyMonitor::get_all_stats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<SystemLatencyStats> stats;
    stats.reserve(trackers_.size());
    
    for (const auto& [name, tracker] : trackers_) {
        SystemLatencyStats stat;
        stat.component_name = name;
        stat.mean_ns = tracker->get_mean_latency_ns();
        stat.p95_ns = tracker->get_p95_latency_ns();
        stat.p99_ns = tracker->get_p99_latency_ns();
        stat.max_ns = tracker->get_max_latency_ns();
        stat.sample_count = tracker->get_sample_count();
        
        stats.push_back(stat);
    }
    
    return stats;
}

void LatencyMonitor::print_latency_report() const {
    auto stats = get_all_stats();
    
    printf("=== Latency Report ===\n");
    printf("%-20s %10s %10s %10s %10s %10s\n", 
           "Component", "Mean(ns)", "P95(ns)", "P99(ns)", "Max(ns)", "Samples");
    printf("--------------------------------------------------------------------------------\n");
    
    for (const auto& stat : stats) {
        printf("%-20s %10.1f %10.1f %10.1f %10.1f %10lu\n",
               stat.component_name.c_str(),
               stat.mean_ns,
               stat.p95_ns,
               stat.p99_ns,
               stat.max_ns,
               stat.sample_count);
    }
}

void LatencyMonitor::set_warning_threshold_ns(const std::string& tracker_name, uint64_t threshold_ns) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    warning_thresholds_[tracker_name] = threshold_ns;
}

void LatencyMonitor::check_thresholds() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    for (const auto& [name, threshold] : warning_thresholds_) {
        auto it = trackers_.find(name);
        if (it != trackers_.end()) {
            double mean_latency = it->second->get_mean_latency_ns();
            if (mean_latency > threshold) {
                printf("WARNING: Tracker '%s' mean latency %.1fns exceeds threshold %luns\n",
                       name.c_str(), mean_latency, threshold);
            }
        }
    }
}

// TSCTimer implementation
TSCTimer::TSCTimer() : ns_per_cycle_(0.0), tsc_frequency_(0) {
    calibrate();
}

void TSCTimer::calibrate() {
    // Simple calibration - measure TSC frequency
    auto start_time = std::chrono::high_resolution_clock::now();
    uint64_t start_tsc = rdtsc();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto end_time = std::chrono::high_resolution_clock::now();
    uint64_t end_tsc = rdtsc();
    
    auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    uint64_t tsc_cycles = end_tsc - start_tsc;
    
    if (tsc_cycles > 0) {
        ns_per_cycle_ = static_cast<double>(duration_ns) / tsc_cycles;
        tsc_frequency_ = static_cast<uint64_t>(1e9 / ns_per_cycle_);
    }
}

void TSCTimer::sleep_ns(uint64_t nanoseconds) const {
    uint64_t target_cycles = static_cast<uint64_t>(nanoseconds / ns_per_cycle_);
    uint64_t start = rdtsc();
    
    while (rdtsc() - start < target_cycles) {
        // Busy wait
    }
}

} // namespace goldearn::core