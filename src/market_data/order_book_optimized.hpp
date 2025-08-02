#pragma once

#include "message_types.hpp"
#include "../core/latency_tracker.hpp"
#include <array>
#include <atomic>
#include <memory>
#include <immintrin.h> // For SIMD operations

namespace goldearn::market_data {

// Ultra-low latency order book with <50μs update times
class OptimizedOrderBook {
public:
    static constexpr size_t MAX_DEPTH = 10;
    static constexpr size_t CACHE_LINE_SIZE = 64;
    static constexpr size_t MEMORY_PREFETCH_DISTANCE = 2;
    
    OptimizedOrderBook(uint64_t symbol_id, double tick_size);
    ~OptimizedOrderBook();
    
    // Core operations optimized for ultra-low latency
    void __attribute__((hot)) add_order(uint64_t order_id, char side, double price, uint64_t quantity, Timestamp timestamp);
    void __attribute__((hot)) modify_order(uint64_t order_id, uint64_t new_quantity, Timestamp timestamp);
    void __attribute__((hot)) cancel_order(uint64_t order_id, Timestamp timestamp);
    void __attribute__((hot)) update_quote_fast(const QuoteMessage& quote);
    
    // Lock-free getters (safe for concurrent read access)
    double __attribute__((pure)) get_best_bid() const noexcept { return best_bid_.load(std::memory_order_acquire); }
    double __attribute__((pure)) get_best_ask() const noexcept { return best_ask_.load(std::memory_order_acquire); }
    uint64_t __attribute__((pure)) get_bid_quantity() const noexcept { return bid_quantity_.load(std::memory_order_acquire); }
    uint64_t __attribute__((pure)) get_ask_quantity() const noexcept { return ask_quantity_.load(std::memory_order_acquire); }
    
    // Fast spread and mid calculations
    double __attribute__((pure)) get_spread() const noexcept {
        double bid = best_bid_.load(std::memory_order_acquire);
        double ask = best_ask_.load(std::memory_order_acquire);
        return ask - bid;
    }
    
    double __attribute__((pure)) get_mid_price() const noexcept {
        double bid = best_bid_.load(std::memory_order_acquire);
        double ask = best_ask_.load(std::memory_order_acquire);
        return (bid + ask) * 0.5;
    }
    
    // SIMD-optimized calculations
    double get_vwap_simd(size_t depth = 5) const;
    double get_imbalance_fast() const noexcept;
    
    // Performance monitoring
    uint64_t get_update_count() const noexcept { return update_count_.load(std::memory_order_relaxed); }
    double get_avg_update_latency_ns() const noexcept { return avg_update_latency_ns_.load(std::memory_order_relaxed); }
    
    // Memory-mapped level access for zero-copy operations
    const PriceLevel* get_bid_levels_ptr() const noexcept { return bid_levels_.data(); }
    const PriceLevel* get_ask_levels_ptr() const noexcept { return ask_levels_.data(); }
    
private:
    uint64_t symbol_id_;
    double tick_size_;
    
    // Cache-aligned atomic variables for best prices
    alignas(CACHE_LINE_SIZE) std::atomic<double> best_bid_;
    alignas(CACHE_LINE_SIZE) std::atomic<double> best_ask_;
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> bid_quantity_;
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> ask_quantity_;
    
    // Cache-aligned price level arrays
    alignas(CACHE_LINE_SIZE) std::array<PriceLevel, MAX_DEPTH> bid_levels_;
    alignas(CACHE_LINE_SIZE) std::array<PriceLevel, MAX_DEPTH> ask_levels_;
    
    // Performance counters
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> update_count_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<double> avg_update_latency_ns_{0.0};
    
    // Memory pool for order tracking (avoid dynamic allocation)
    struct OrderInfo {
        double price;
        uint64_t quantity;
        char side;
        uint32_t level_index;
    };
    
    static constexpr size_t MAX_ORDERS = 10000;
    std::array<OrderInfo, MAX_ORDERS> order_pool_;
    std::atomic<uint32_t> next_order_slot_{0};
    std::array<std::atomic<bool>, MAX_ORDERS> slot_occupied_;
    
    // Fast hash map for order ID lookup (lock-free)
    static constexpr size_t HASH_TABLE_SIZE = 16384; // Power of 2 for fast modulo
    struct HashEntry {
        std::atomic<uint64_t> order_id{0};
        std::atomic<uint32_t> slot_index{UINT32_MAX};
    };
    std::array<HashEntry, HASH_TABLE_SIZE> order_hash_table_;
    
    // Inline helper functions for maximum performance
    inline uint32_t __attribute__((always_inline)) hash_order_id(uint64_t order_id) const noexcept {
        // Fast hash function optimized for order IDs
        return (order_id ^ (order_id >> 16)) & (HASH_TABLE_SIZE - 1);
    }
    
    inline void __attribute__((always_inline)) prefetch_memory(const void* addr) const noexcept {
        __builtin_prefetch(addr, 0, 3); // Prefetch for read, high temporal locality
    }
    
    inline void __attribute__((always_inline)) memory_fence() const noexcept {
        std::atomic_thread_fence(std::memory_order_acq_rel);
    }
    
    // Optimized level management
    void __attribute__((hot)) update_bid_levels_fast(double price, int64_t quantity_delta);
    void __attribute__((hot)) update_ask_levels_fast(double price, int64_t quantity_delta);
    void __attribute__((hot)) sort_levels_simd();
    
    // Order tracking optimized for insertion/removal
    uint32_t allocate_order_slot();
    void deallocate_order_slot(uint32_t slot);
    uint32_t find_order_slot(uint64_t order_id) const;
    void insert_order_hash(uint64_t order_id, uint32_t slot);
    void remove_order_hash(uint64_t order_id);
    
    // Branch prediction hints
    #define likely(x)   __builtin_expect(!!(x), 1)
    #define unlikely(x) __builtin_expect(!!(x), 0)
};

// Lock-free order book manager for multiple symbols
class OptimizedOrderBookManager {
public:
    static constexpr size_t MAX_SYMBOLS = 1000;
    
    OptimizedOrderBookManager();
    ~OptimizedOrderBookManager();
    
    // Symbol management
    bool add_symbol(uint64_t symbol_id, double tick_size);
    OptimizedOrderBook* __attribute__((hot)) get_order_book(uint64_t symbol_id) const noexcept;
    
    // Batch processing for maximum throughput
    void process_quote_batch(const QuoteMessage* quotes, size_t count);
    void process_trade_batch(const TradeMessage* trades, size_t count);
    
    // NUMA-aware allocation
    bool enable_numa_optimization(int node_id);
    
    // Statistics
    uint64_t get_total_updates() const noexcept { return total_updates_.load(std::memory_order_relaxed); }
    double get_average_latency_ns() const noexcept;
    
private:
    // Symbol lookup using perfect hashing for O(1) access
    struct SymbolEntry {
        std::atomic<uint64_t> symbol_id{0};
        std::atomic<OptimizedOrderBook*> order_book{nullptr};
    };
    
    alignas(CACHE_LINE_SIZE) std::array<SymbolEntry, MAX_SYMBOLS> symbol_table_;
    std::atomic<uint32_t> symbol_count_{0};
    
    // Performance counters
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> total_updates_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> total_latency_ns_{0};
    
    // Memory management
    std::aligned_storage_t<sizeof(OptimizedOrderBook), alignof(OptimizedOrderBook)> book_storage_[MAX_SYMBOLS];
    std::atomic<uint32_t> next_book_slot_{0};
    
    // Fast symbol lookup
    uint32_t find_symbol_slot(uint64_t symbol_id) const noexcept;
    uint32_t allocate_symbol_slot();
};

// Hardware-accelerated market data processing
class SIMDMarketDataProcessor {
public:
    // Process multiple quotes simultaneously using AVX2
    static void process_quotes_avx2(const QuoteMessage* quotes, size_t count, OptimizedOrderBook** books);
    
    // Calculate VWAP for multiple symbols simultaneously
    static void calculate_vwap_batch_avx2(const OptimizedOrderBook** books, size_t count, double* results, size_t depth);
    
    // Fast price validation using SIMD
    static bool validate_prices_avx2(const double* prices, size_t count, double min_price, double max_price);
    
    // Parallel spread calculation
    static void calculate_spreads_avx2(const OptimizedOrderBook** books, size_t count, double* spreads);
    
private:
    // SIMD utility functions
    static bool has_avx2_support();
    static bool has_avx512_support();
    
    // Vectorized operations
    static __m256d load_prices_avx2(const double* prices);
    static void store_results_avx2(double* results, __m256d values);
};

// Cache-optimized order book for specific trading patterns
template<size_t MaxLevels = 5>
class SpecializedOrderBook {
public:
    SpecializedOrderBook(uint64_t symbol_id, double tick_size);
    
    // Template specializations for common cases
    void update_top_of_book(double bid_price, uint64_t bid_qty, double ask_price, uint64_t ask_qty);
    
    // Compile-time optimized getters
    template<size_t Level>
    constexpr PriceLevel get_bid_level() const noexcept {
        static_assert(Level < MaxLevels, "Level exceeds maximum depth");
        return bid_levels_[Level];
    }
    
    template<size_t Level>
    constexpr PriceLevel get_ask_level() const noexcept {
        static_assert(Level < MaxLevels, "Level exceeds maximum depth");
        return ask_levels_[Level];
    }
    
    // Specialized for market making strategies
    std::pair<double, double> get_inside_market() const noexcept {
        return {bid_levels_[0].price, ask_levels_[0].price};
    }
    
    // Specialized for arbitrage strategies
    bool is_crossed() const noexcept {
        return bid_levels_[0].price >= ask_levels_[0].price;
    }
    
private:
    uint64_t symbol_id_;
    double tick_size_;
    
    alignas(64) std::array<PriceLevel, MaxLevels> bid_levels_;
    alignas(64) std::array<PriceLevel, MaxLevels> ask_levels_;
};

// Memory-mapped order book for ultra-low latency (shared memory)
class MemoryMappedOrderBook {
public:
    struct SharedBookData {
        alignas(64) std::atomic<double> best_bid;
        alignas(64) std::atomic<double> best_ask;
        alignas(64) std::atomic<uint64_t> bid_quantity;
        alignas(64) std::atomic<uint64_t> ask_quantity;
        alignas(64) PriceLevel bid_levels[10];
        alignas(64) PriceLevel ask_levels[10];
        alignas(64) std::atomic<uint64_t> sequence_number;
        alignas(64) std::atomic<Timestamp> last_update;
    };
    
    MemoryMappedOrderBook(const std::string& shared_memory_name, uint64_t symbol_id);
    ~MemoryMappedOrderBook();
    
    // Zero-copy access to shared data
    const SharedBookData* get_shared_data() const noexcept { return shared_data_; }
    
    // Producer interface (for market data feed)
    void update_shared_book(const QuoteMessage& quote);
    
    // Consumer interface (for trading strategies)
    bool read_book_snapshot(SharedBookData& snapshot) const noexcept;
    
    // Synchronization
    uint64_t get_sequence_number() const noexcept { 
        return shared_data_->sequence_number.load(std::memory_order_acquire); 
    }
    
    bool wait_for_update(uint64_t last_seen_sequence, std::chrono::nanoseconds timeout) const;
    
private:
    std::string shared_memory_name_;
    uint64_t symbol_id_;
    int shared_memory_fd_;
    SharedBookData* shared_data_;
    size_t shared_memory_size_;
    
    bool create_shared_memory();
    bool map_shared_memory();
    void unmap_shared_memory();
};

} // namespace goldearn::market_data