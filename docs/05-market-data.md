# GoldEarn HFT - Market Data Processing

## 📡 **Market Data Overview**

### **Core Objectives**
- **Ultra-low latency**: <50μs market data processing
- **High throughput**: 1M+ messages/second
- **Real-time normalization**: Multiple exchange formats
- **Order book construction**: Level 2/3 depth
- **Signal generation**: Market microstructure insights

### **Data Sources**
- **Primary**: NSE, BSE direct feeds
- **Secondary**: Reuters, Bloomberg terminals  
- **Alternative**: News feeds, social sentiment
- **Reference**: Corporate actions, static data

## 🏗️ **Market Data Architecture**

```cpp
class MarketDataEngine {
private:
    // Feed handlers
    std::unique_ptr<NSEFeedHandler> nse_handler_;
    std::unique_ptr<BSEFeedHandler> bse_handler_;
    std::unique_ptr<ReutersFeedHandler> reuters_handler_;
    
    // Order book managers
    concurrent_hash_map<Symbol, OrderBook> order_books_;
    
    // Subscribers
    std::vector<IMarketDataSubscriber*> subscribers_;
    
public:
    void subscribe(Symbol symbol, IMarketDataSubscriber* subscriber);
    void process_tick(const RawTick& tick);
    OrderBook get_order_book(Symbol symbol) const;
    MarketData get_market_data(Symbol symbol) const;
};
```

### **Order Book Implementation**

```cpp
class OrderBook {
private:
    struct PriceLevel {
        double price;
        uint64_t quantity;
        uint32_t order_count;
        Timestamp timestamp;
    };
    
    // Using sorted containers for price levels
    std::map<double, PriceLevel, std::greater<double>> bids_;  // Descending
    std::map<double, PriceLevel, std::less<double>> asks_;     // Ascending
    
    // Best bid/ask cache
    mutable std::atomic<double> best_bid_{0.0};
    mutable std::atomic<double> best_ask_{0.0};
    
public:
    void update(const MarketDataTick& tick) {
        auto start = rdtsc();
        
        if (tick.side == Side::BID) {
            update_bid_side(tick);
        } else {
            update_ask_side(tick);
        }
        
        // Update best prices atomically
        if (!bids_.empty()) {
            best_bid_.store(bids_.begin()->first, std::memory_order_release);
        }
        if (!asks_.empty()) {
            best_ask_.store(asks_.begin()->first, std::memory_order_release);
        }
        
        auto latency = rdtsc() - start;
        update_processing_latency(latency);
    }
    
    double get_best_bid() const {
        return best_bid_.load(std::memory_order_acquire);
    }
    
    double get_best_ask() const {
        return best_ask_.load(std::memory_order_acquire);
    }
    
    double get_mid_price() const {
        auto bid = get_best_bid();
        auto ask = get_best_ask();
        return (bid + ask) / 2.0;
    }
    
    MarketDepth get_depth(int levels = 5) const {
        MarketDepth depth;
        
        // Bid levels
        int count = 0;
        for (const auto& [price, level] : bids_) {
            if (count >= levels) break;
            depth.bids.emplace_back(price, level.quantity);
            ++count;
        }
        
        // Ask levels  
        count = 0;
        for (const auto& [price, level] : asks_) {
            if (count >= levels) break;
            depth.asks.emplace_back(price, level.quantity);
            ++count;
        }
        
        return depth;
    }
};
```

## 📊 **Signal Generation**

### **Market Microstructure Signals**

```cpp
class MicrostructureSignalGenerator {
private:
    RollingWindow<Trade> recent_trades_;
    RollingWindow<OrderBookSnapshot> book_snapshots_;
    
public:
    MicrostructureSignals generate_signals(const OrderBook& book,
                                         const std::vector<Trade>& trades) {
        MicrostructureSignals signals;
        
        // Order flow imbalance
        signals.order_flow_imbalance = calculate_order_flow_imbalance(trades);
        
        // Bid-ask spread
        signals.spread_bps = calculate_spread_bps(book);
        
        // Market depth imbalance
        signals.depth_imbalance = calculate_depth_imbalance(book);
        
        // Price impact
        signals.price_impact = calculate_price_impact(trades);
        
        // Volume profile
        signals.volume_profile = calculate_volume_profile(trades);
        
        return signals;
    }
    
private:
    double calculate_order_flow_imbalance(const std::vector<Trade>& trades) {
        if (trades.empty()) return 0.0;
        
        double buy_volume = 0.0;
        double sell_volume = 0.0;
        
        for (const auto& trade : trades) {
            if (trade.aggressor_side == Side::BUY) {
                buy_volume += trade.quantity * trade.price;
            } else {
                sell_volume += trade.quantity * trade.price;
            }
        }
        
        double total_volume = buy_volume + sell_volume;
        if (total_volume == 0.0) return 0.0;
        
        return (buy_volume - sell_volume) / total_volume;
    }
    
    double calculate_spread_bps(const OrderBook& book) {
        auto bid = book.get_best_bid();
        auto ask = book.get_best_ask();
        
        if (bid <= 0.0 || ask <= 0.0) return 0.0;
        
        auto mid = (bid + ask) / 2.0;
        return (ask - bid) / mid * 10000.0;  // basis points
    }
    
    double calculate_depth_imbalance(const OrderBook& book) {
        auto depth = book.get_depth(5);
        
        double bid_volume = 0.0;
        double ask_volume = 0.0;
        
        for (const auto& [price, quantity] : depth.bids) {
            bid_volume += quantity;
        }
        
        for (const auto& [price, quantity] : depth.asks) {
            ask_volume += quantity;
        }
        
        double total_volume = bid_volume + ask_volume;
        if (total_volume == 0.0) return 0.0;
        
        return (bid_volume - ask_volume) / total_volume;
    }
};
```

### **VWAP & TWAP Calculations**

```cpp
class VWAPCalculator {
private:
    struct VWAPState {
        double cumulative_value;
        uint64_t cumulative_volume;
        std::chrono::steady_clock::time_point start_time;
    };
    
    std::unordered_map<Symbol, VWAPState> vwap_states_;
    
public:
    void update_trade(Symbol symbol, const Trade& trade) {
        auto& state = vwap_states_[symbol];
        
        state.cumulative_value += trade.price * trade.quantity;
        state.cumulative_volume += trade.quantity;
    }
    
    double get_vwap(Symbol symbol) const {
        auto it = vwap_states_.find(symbol);
        if (it == vwap_states_.end() || it->second.cumulative_volume == 0) {
            return 0.0;
        }
        
        return it->second.cumulative_value / it->second.cumulative_volume;
    }
    
    void reset_daily_vwap(Symbol symbol) {
        vwap_states_[symbol] = VWAPState{
            .cumulative_value = 0.0,
            .cumulative_volume = 0,
            .start_time = std::chrono::steady_clock::now()
        };
    }
};
```

## ⚡ **Performance Optimization**

### **Lock-Free Processing**

```cpp
// Lock-free market data queue
template<typename T>
class LockFreeQueue {
private:
    struct alignas(64) Node {
        std::atomic<T*> data{nullptr};
        std::atomic<Node*> next{nullptr};
    };
    
    alignas(64) std::atomic<Node*> head_{nullptr};
    alignas(64) std::atomic<Node*> tail_{nullptr};
    
public:
    void enqueue(T item) {
        Node* new_node = new Node;
        T* data = new T(std::move(item));
        new_node->data.store(data);
        
        Node* prev_tail = tail_.exchange(new_node);
        prev_tail->next.store(new_node);
    }
    
    bool dequeue(T& result) {
        Node* head = head_.load();
        Node* next = head->next.load();
        
        if (next == nullptr) {
            return false;  // Queue is empty
        }
        
        T* data = next->data.load();
        if (data == nullptr) {
            return false;
        }
        
        result = *data;
        delete data;
        
        head_.store(next);
        delete head;
        
        return true;
    }
};
```

### **SIMD Optimized Calculations**

```cpp
// Vectorized price calculations using AVX2
void calculate_weighted_prices_simd(const double* prices, 
                                   const double* weights,
                                   double* results, 
                                   size_t count) {
    const size_t simd_count = count & ~3;  // Process 4 at a time
    
    for (size_t i = 0; i < simd_count; i += 4) {
        __m256d price_vec = _mm256_load_pd(&prices[i]);
        __m256d weight_vec = _mm256_load_pd(&weights[i]);
        __m256d result_vec = _mm256_mul_pd(price_vec, weight_vec);
        _mm256_store_pd(&results[i], result_vec);
    }
    
    // Handle remaining elements
    for (size_t i = simd_count; i < count; ++i) {
        results[i] = prices[i] * weights[i];
    }
}
```

## 📈 **Analytics & Insights**

### **Real-Time Analytics**

```cpp
class MarketAnalytics {
private:
    RollingWindow<double> price_history_;
    RollingWindow<uint64_t> volume_history_;
    RollingWindow<Trade> trade_history_;
    
public:
    MarketStatistics calculate_statistics(Symbol symbol) {
        MarketStatistics stats;
        
        // Price statistics
        stats.current_price = get_last_price(symbol);
        stats.daily_high = calculate_daily_high(symbol);
        stats.daily_low = calculate_daily_low(symbol);
        stats.daily_open = get_daily_open(symbol);
        
        // Volume statistics
        stats.total_volume = calculate_total_volume(symbol);
        stats.avg_trade_size = calculate_avg_trade_size(symbol);
        stats.trade_count = get_trade_count(symbol);
        
        // Volatility
        stats.realized_volatility = calculate_realized_volatility(symbol);
        stats.price_change_pct = calculate_price_change_pct(symbol);
        
        // Market microstructure
        stats.avg_spread_bps = calculate_avg_spread(symbol);
        stats.market_impact_bps = calculate_market_impact(symbol);
        
        return stats;
    }
    
private:
    double calculate_realized_volatility(Symbol symbol) {
        auto prices = get_recent_prices(symbol, 100);  // Last 100 ticks
        if (prices.size() < 2) return 0.0;
        
        std::vector<double> returns;
        for (size_t i = 1; i < prices.size(); ++i) {
            returns.push_back(std::log(prices[i] / prices[i-1]));
        }
        
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) 
                     / returns.size();
        
        double variance = 0.0;
        for (double ret : returns) {
            variance += (ret - mean) * (ret - mean);
        }
        variance /= returns.size();
        
        // Annualized volatility (assuming 252 trading days)
        return std::sqrt(variance * 252 * 390);  // 390 minutes per day
    }
};
```

### **Pattern Detection**

```cpp
class PatternDetector {
public:
    std::vector<Pattern> detect_patterns(const OrderBook& book,
                                        const std::vector<Trade>& trades) {
        std::vector<Pattern> patterns;
        
        // Momentum patterns
        auto momentum = detect_momentum_pattern(trades);
        if (momentum.confidence > 0.7) {
            patterns.push_back(momentum);
        }
        
        // Mean reversion patterns
        auto mean_reversion = detect_mean_reversion_pattern(trades);
        if (mean_reversion.confidence > 0.7) {
            patterns.push_back(mean_reversion);
        }
        
        // Order book patterns
        auto book_pattern = detect_order_book_pattern(book);
        if (book_pattern.confidence > 0.7) {
            patterns.push_back(book_pattern);
        }
        
        return patterns;
    }
    
private:
    Pattern detect_momentum_pattern(const std::vector<Trade>& trades) {
        if (trades.size() < 10) {
            return Pattern{PatternType::MOMENTUM, 0.0, ""};
        }
        
        // Calculate price momentum
        double start_price = trades[0].price;
        double end_price = trades.back().price;
        double price_change = (end_price - start_price) / start_price;
        
        // Calculate volume momentum
        double recent_volume = 0.0;
        double earlier_volume = 0.0;
        
        size_t mid = trades.size() / 2;
        for (size_t i = mid; i < trades.size(); ++i) {
            recent_volume += trades[i].quantity;
        }
        for (size_t i = 0; i < mid; ++i) {
            earlier_volume += trades[i].quantity;
        }
        
        double volume_ratio = recent_volume / earlier_volume;
        
        // Momentum signal strength
        double momentum_strength = std::abs(price_change) * 
                                  std::log(volume_ratio);
        
        double confidence = std::min(1.0, momentum_strength * 10.0);
        
        return Pattern{
            PatternType::MOMENTUM,
            confidence,
            "Price momentum with volume confirmation"
        };
    }
};
```

## 🔧 **Configuration & Monitoring**

### **Market Data Configuration**

```yaml
market_data:
  feeds:
    nse:
      host: "nse-feed.goldearn.com"
      port: 9001
      protocol: "binary"
      symbols: ["RELIANCE", "TCS", "INFY", "HDFC", "ICICI"]
      
    bse:
      host: "bse-feed.goldearn.com" 
      port: 9002
      protocol: "binary"
      symbols: ["RELIANCE", "TCS", "INFY"]
      
    reuters:
      host: "reuters-feed.goldearn.com"
      port: 9003
      protocol: "reuters"
      symbols: ["USDINR", "EURINR"]
      
  processing:
    max_latency_us: 50
    queue_size: 1000000
    worker_threads: 4
    cpu_affinity: [2, 3, 4, 5]
    
  analytics:
    vwap_window_minutes: 60
    volatility_window_ticks: 100
    pattern_detection: true
    signal_generation: true
```

### **Performance Monitoring**

```cpp
struct MarketDataMetrics {
    // Latency metrics
    uint64_t avg_processing_latency_ns;
    uint64_t max_processing_latency_ns;
    uint64_t p99_processing_latency_ns;
    
    // Throughput metrics
    uint64_t messages_per_second;
    uint64_t bytes_per_second;
    uint64_t order_book_updates_per_second;
    
    // Quality metrics
    double message_loss_rate;
    double order_book_accuracy;
    uint64_t sequence_gaps;
    
    // By feed breakdown
    std::map<std::string, FeedMetrics> feed_metrics;
};
```

## 📊 **Market Data Quality**

### **Data Validation**

```cpp
class DataValidator {
public:
    ValidationResult validate_tick(const MarketDataTick& tick) {
        ValidationResult result;
        result.valid = true;
        
        // Price validation
        if (tick.price <= 0.0) {
            result.valid = false;
            result.errors.push_back("Invalid price: " + std::to_string(tick.price));
        }
        
        // Quantity validation
        if (tick.quantity == 0) {
            result.valid = false;
            result.errors.push_back("Zero quantity");
        }
        
        // Timestamp validation
        auto now = std::chrono::system_clock::now();
        auto tick_time = std::chrono::system_clock::time_point(
            std::chrono::microseconds(tick.timestamp));
        
        if (tick_time > now) {
            result.valid = false;
            result.errors.push_back("Future timestamp");
        }
        
        // Price movement validation (circuit breaker check)
        auto last_price = get_last_valid_price(tick.symbol);
        if (last_price > 0.0) {
            double price_change = std::abs(tick.price - last_price) / last_price;
            if (price_change > 0.20) {  // 20% circuit breaker
                result.valid = false;
                result.errors.push_back("Excessive price movement");
            }
        }
        
        return result;
    }
};
```

---

**This market data system provides ultra-low latency processing with comprehensive analytics for informed trading decisions.**