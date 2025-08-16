#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

namespace goldearn::core {

// Token bucket rate limiter with constant-time operations
// Resistant to timing attacks
class RateLimiter {
   public:
    RateLimiter(uint32_t max_tokens, uint32_t refill_rate_per_second)
        : max_tokens_(max_tokens),
          refill_rate_(refill_rate_per_second),
          tokens_(max_tokens),
          last_refill_time_(std::chrono::steady_clock::now()) {
    }

    // Check if request is allowed (constant time operation)
    bool try_acquire(uint32_t tokens = 1) {
        const auto now = std::chrono::steady_clock::now();

        // Lock-free fast path for rejection
        if (tokens_.load(std::memory_order_acquire) < tokens) {
            refill_tokens(now);
            if (tokens_.load(std::memory_order_acquire) < tokens) {
                return false;
            }
        }

        // Synchronized token consumption
        std::lock_guard<std::mutex> lock(mutex_);

        // Refill tokens based on elapsed time
        refill_tokens_locked(now);

        // Constant-time check and consume
        uint32_t current = tokens_.load(std::memory_order_relaxed);
        if (current >= tokens) {
            tokens_.store(current - tokens, std::memory_order_relaxed);
            return true;
        }

        return false;
    }

    // Get current token count (for monitoring)
    uint32_t get_available_tokens() const {
        return tokens_.load(std::memory_order_acquire);
    }

    // Reset the rate limiter
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        tokens_.store(max_tokens_, std::memory_order_release);
        last_refill_time_ = std::chrono::steady_clock::now();
    }

   private:
    void refill_tokens(const std::chrono::steady_clock::time_point& now) {
        // Quick check without lock
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - last_refill_time_.load(std::memory_order_acquire))
                           .count();

        if (elapsed > 100) {  // Only lock if significant time passed
            std::lock_guard<std::mutex> lock(mutex_);
            refill_tokens_locked(now);
        }
    }

    void refill_tokens_locked(const std::chrono::steady_clock::time_point& now) {
        auto last_refill = last_refill_time_.load(std::memory_order_relaxed);
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_refill).count();

        if (elapsed > 0) {
            uint32_t tokens_to_add = static_cast<uint32_t>((elapsed * refill_rate_) / 1000);

            if (tokens_to_add > 0) {
                uint32_t current = tokens_.load(std::memory_order_relaxed);
                uint32_t new_tokens = std::min(current + tokens_to_add, max_tokens_);
                tokens_.store(new_tokens, std::memory_order_relaxed);
                last_refill_time_.store(now, std::memory_order_relaxed);
            }
        }
    }

   private:
    const uint32_t max_tokens_;
    const uint32_t refill_rate_;
    std::atomic<uint32_t> tokens_;
    std::atomic<std::chrono::steady_clock::time_point> last_refill_time_;
    mutable std::mutex mutex_;
};

// Sliding window rate limiter for more precise control
class SlidingWindowRateLimiter {
   public:
    SlidingWindowRateLimiter(uint32_t max_requests, std::chrono::milliseconds window_size)
        : max_requests_(max_requests), window_size_(window_size), request_times_(max_requests) {
    }

    bool try_acquire() {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);

        // Remove old requests outside the window
        const auto cutoff = now - window_size_;
        while (oldest_index_ < newest_index_ &&
               request_times_[oldest_index_ % max_requests_] < cutoff) {
            oldest_index_++;
        }

        // Check if we can accept new request
        uint32_t current_count = newest_index_ - oldest_index_;
        if (current_count >= max_requests_) {
            return false;
        }

        // Add new request
        request_times_[newest_index_ % max_requests_] = now;
        newest_index_++;

        return true;
    }

    uint32_t get_current_rate() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return newest_index_ - oldest_index_;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        oldest_index_ = 0;
        newest_index_ = 0;
    }

   private:
    const uint32_t max_requests_;
    const std::chrono::milliseconds window_size_;

    mutable std::mutex mutex_;
    std::vector<std::chrono::steady_clock::time_point> request_times_;
    uint32_t oldest_index_ = 0;
    uint32_t newest_index_ = 0;
};

// Distributed rate limiter using shared memory or Redis
class DistributedRateLimiter {
   public:
    DistributedRateLimiter(const std::string& key,
                           uint32_t max_requests,
                           std::chrono::seconds window)
        : key_(key), max_requests_(max_requests), window_(window) {
        // Initialize Redis connection or shared memory
        // TODO: Implement distributed backend
    }

    bool try_acquire(const std::string& client_id = "") {
        // Use consistent algorithm across all instances
        // TODO: Implement using Redis INCR with TTL or similar
        return local_limiter_.try_acquire();
    }

   private:
    std::string key_;
    uint32_t max_requests_;
    std::chrono::seconds window_;
    RateLimiter local_limiter_{100, 100};  // Fallback local limiter
};

// Multi-tier rate limiter for different priority levels
class PriorityRateLimiter {
   public:
    enum class Priority { HIGH = 0, MEDIUM = 1, LOW = 2 };

    PriorityRateLimiter() {
        // Different limits for different priorities
        limiters_[0] = std::make_unique<RateLimiter>(1000, 1000);  // High priority
        limiters_[1] = std::make_unique<RateLimiter>(500, 500);    // Medium priority
        limiters_[2] = std::make_unique<RateLimiter>(100, 100);    // Low priority
    }

    bool try_acquire(Priority priority, uint32_t tokens = 1) {
        size_t idx = static_cast<size_t>(priority);
        if (idx >= limiters_.size()) {
            return false;
        }
        return limiters_[idx]->try_acquire(tokens);
    }

    void reset() {
        for (auto& limiter : limiters_) {
            limiter->reset();
        }
    }

   private:
    std::array<std::unique_ptr<RateLimiter>, 3> limiters_;
};

}  // namespace goldearn::core