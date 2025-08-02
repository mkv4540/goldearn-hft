#include "order_book.hpp"
#include <algorithm>
#include <cmath>

namespace goldearn::market_data {

OrderBook::OrderBook(uint64_t symbol_id, double tick_size) 
    : symbol_id_(symbol_id), tick_size_(tick_size), best_bid_(0.0), best_ask_(0.0),
      bid_quantity_(0), ask_quantity_(0), total_volume_(0), trade_count_(0),
      last_trade_price_(0.0), avg_update_latency_ns_(0.0), update_count_(0) {
    
    bid_levels_.fill(PriceLevel{});
    ask_levels_.fill(PriceLevel{});
}

OrderBook::~OrderBook() = default;

void OrderBook::add_order(uint64_t order_id, char side, double price, uint64_t quantity, Timestamp timestamp) {
    if (quantity == 0 || price <= 0.0) return;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Store order information
    OrderInfo order_info{price, quantity, side, timestamp};
    active_orders_[order_id] = order_info;
    
    if (side == 'B' || side == 'b') {
        update_bid_levels(price, static_cast<int64_t>(quantity), timestamp);
        update_best_prices();
    } else if (side == 'S' || side == 's') {
        update_ask_levels(price, static_cast<int64_t>(quantity), timestamp);
        update_best_prices();
    }
    
    last_update_ = timestamp;
    
    // Update performance metrics
    auto end_time = std::chrono::high_resolution_clock::now();
    auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    update_count_++;
    avg_update_latency_ns_ = ((avg_update_latency_ns_ * (update_count_ - 1)) + latency_ns) / update_count_;
}

void OrderBook::modify_order(uint64_t order_id, uint64_t new_quantity, Timestamp timestamp) {
    auto it = active_orders_.find(order_id);
    if (it == active_orders_.end()) return;
    
    auto& order_info = it->second;
    int64_t quantity_delta = static_cast<int64_t>(new_quantity) - static_cast<int64_t>(order_info.quantity);
    
    if (new_quantity == 0) {
        cancel_order(order_id, timestamp);
        return;
    }
    
    if (order_info.side == 'B' || order_info.side == 'b') {
        update_bid_levels(order_info.price, quantity_delta, timestamp);
    } else if (order_info.side == 'S' || order_info.side == 's') {
        update_ask_levels(order_info.price, quantity_delta, timestamp);
    }
    
    order_info.quantity = new_quantity;
    order_info.timestamp = timestamp;
    update_best_prices();
    last_update_ = timestamp;
}

void OrderBook::cancel_order(uint64_t order_id, Timestamp timestamp) {
    auto it = active_orders_.find(order_id);
    if (it == active_orders_.end()) return;
    
    const auto& order_info = it->second;
    int64_t quantity_delta = -static_cast<int64_t>(order_info.quantity);
    
    if (order_info.side == 'B' || order_info.side == 'b') {
        update_bid_levels(order_info.price, quantity_delta, timestamp);
    } else if (order_info.side == 'S' || order_info.side == 's') {
        update_ask_levels(order_info.price, quantity_delta, timestamp);
    }
    
    active_orders_.erase(it);
    update_best_prices();
    last_update_ = timestamp;
}

void OrderBook::update_trade(double price, uint64_t quantity, Timestamp timestamp) {
    total_volume_ += quantity;
    trade_count_++;
    last_trade_price_ = price;
    last_update_ = timestamp;
}

void OrderBook::update_quote(const QuoteMessage& quote) {
    if (quote.symbol_id != symbol_id_) return;
    
    // Update best bid/ask
    best_bid_.store(quote.bid_price);
    best_ask_.store(quote.ask_price);
    bid_quantity_.store(quote.bid_quantity);
    ask_quantity_.store(quote.ask_quantity);
    
    // Update depth levels
    for (size_t i = 0; i < MAX_DEPTH && i < quote.bid_levels.size(); ++i) {
        if (quote.bid_levels[i].price > 0) {
            bid_levels_[i] = PriceLevel{
                quote.bid_levels[i].price,
                quote.bid_levels[i].quantity,
                quote.bid_levels[i].num_orders,
                quote.header.timestamp
            };
        }
    }
    
    for (size_t i = 0; i < MAX_DEPTH && i < quote.ask_levels.size(); ++i) {
        if (quote.ask_levels[i].price > 0) {
            ask_levels_[i] = PriceLevel{
                quote.ask_levels[i].price,
                quote.ask_levels[i].quantity,
                quote.ask_levels[i].num_orders,
                quote.header.timestamp
            };
        }
    }
    
    last_update_ = quote.header.timestamp;
}

void OrderBook::full_refresh(const std::vector<PriceLevel>& bids, const std::vector<PriceLevel>& asks) {
    // Clear existing levels
    bid_levels_.fill(PriceLevel{});
    ask_levels_.fill(PriceLevel{});
    
    // Copy bid levels
    size_t bid_count = std::min(bids.size(), MAX_DEPTH);
    for (size_t i = 0; i < bid_count; ++i) {
        if (bids[i].total_quantity > 0) {
            bid_levels_[i] = bids[i];
        }
    }
    
    // Copy ask levels
    size_t ask_count = std::min(asks.size(), MAX_DEPTH);
    for (size_t i = 0; i < ask_count; ++i) {
        if (asks[i].total_quantity > 0) {
            ask_levels_[i] = asks[i];
        }
    }
    
    update_best_prices();
    last_update_ = std::chrono::duration_cast<Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
}

double OrderBook::get_vwap(size_t depth) const {
    if (depth == 0) return 0.0;
    
    double total_value = 0.0;
    uint64_t total_quantity = 0;
    
    // Calculate from bid side
    for (size_t i = 0; i < std::min(depth, MAX_DEPTH); ++i) {
        if (bid_levels_[i].price > 0 && bid_levels_[i].total_quantity > 0) {
            total_value += bid_levels_[i].price * bid_levels_[i].total_quantity;
            total_quantity += bid_levels_[i].total_quantity;
        }
    }
    
    // Calculate from ask side
    for (size_t i = 0; i < std::min(depth, MAX_DEPTH); ++i) {
        if (ask_levels_[i].price > 0 && ask_levels_[i].total_quantity > 0) {
            total_value += ask_levels_[i].price * ask_levels_[i].total_quantity;
            total_quantity += ask_levels_[i].total_quantity;
        }
    }
    
    if (total_quantity == 0) return 0.0;
    return total_value / total_quantity;
}

double OrderBook::get_imbalance() const {
    double bid_qty = static_cast<double>(bid_quantity_.load());
    double ask_qty = static_cast<double>(ask_quantity_.load());
    
    if (bid_qty == 0 && ask_qty == 0) return 0.0;
    if (ask_qty == 0) return 1.0;  // Maximum bid imbalance
    if (bid_qty == 0) return -1.0; // Maximum ask imbalance
    
    return (bid_qty - ask_qty) / (bid_qty + ask_qty);
}

void OrderBook::update_bid_levels(double price, int64_t quantity_delta, Timestamp timestamp) {
    // Find existing level or create new one
    bool found = false;
    for (size_t i = 0; i < MAX_DEPTH; ++i) {
        if (std::abs(bid_levels_[i].price - price) < tick_size_ / 2.0) {
            // Update existing level
            int64_t new_quantity = static_cast<int64_t>(bid_levels_[i].total_quantity) + quantity_delta;
            if (new_quantity <= 0) {
                // Remove level
                bid_levels_[i] = PriceLevel{};
            } else {
                bid_levels_[i].total_quantity = static_cast<uint64_t>(new_quantity);
                bid_levels_[i].last_update = timestamp;
                if (quantity_delta > 0) {
                    bid_levels_[i].order_count++;
                }
            }
            found = true;
            break;
        }
    }
    
    if (!found && quantity_delta > 0) {
        // Add new level
        PriceLevel new_level{price, static_cast<uint64_t>(quantity_delta), 1, timestamp};
        insert_bid_level(new_level);
    }
    
    rebuild_depth();
}

void OrderBook::update_ask_levels(double price, int64_t quantity_delta, Timestamp timestamp) {
    // Find existing level or create new one
    bool found = false;
    for (size_t i = 0; i < MAX_DEPTH; ++i) {
        if (std::abs(ask_levels_[i].price - price) < tick_size_ / 2.0) {
            // Update existing level
            int64_t new_quantity = static_cast<int64_t>(ask_levels_[i].total_quantity) + quantity_delta;
            if (new_quantity <= 0) {
                // Remove level
                ask_levels_[i] = PriceLevel{};
            } else {
                ask_levels_[i].total_quantity = static_cast<uint64_t>(new_quantity);
                ask_levels_[i].last_update = timestamp;
                if (quantity_delta > 0) {
                    ask_levels_[i].order_count++;
                }
            }
            found = true;
            break;
        }
    }
    
    if (!found && quantity_delta > 0) {
        // Add new level
        PriceLevel new_level{price, static_cast<uint64_t>(quantity_delta), 1, timestamp};
        insert_ask_level(new_level);
    }
    
    rebuild_depth();
}

void OrderBook::rebuild_depth() {
    // Sort bid levels (highest price first)
    std::sort(bid_levels_.begin(), bid_levels_.end(), 
              [](const PriceLevel& a, const PriceLevel& b) {
                  if (a.total_quantity == 0) return false;
                  if (b.total_quantity == 0) return true;
                  return a.price > b.price;
              });
    
    // Sort ask levels (lowest price first)
    std::sort(ask_levels_.begin(), ask_levels_.end(),
              [](const PriceLevel& a, const PriceLevel& b) {
                  if (a.total_quantity == 0) return false;
                  if (b.total_quantity == 0) return true;
                  return a.price < b.price;
              });
}

void OrderBook::update_best_prices() {
    // Update best bid
    double new_best_bid = 0.0;
    uint64_t new_bid_qty = 0;
    if (bid_levels_[0].total_quantity > 0) {
        new_best_bid = bid_levels_[0].price;
        new_bid_qty = bid_levels_[0].total_quantity;
    }
    
    // Update best ask
    double new_best_ask = 0.0;
    uint64_t new_ask_qty = 0;
    if (ask_levels_[0].total_quantity > 0) {
        new_best_ask = ask_levels_[0].price;
        new_ask_qty = ask_levels_[0].total_quantity;
    }
    
    best_bid_.store(new_best_bid);
    best_ask_.store(new_best_ask);
    bid_quantity_.store(new_bid_qty);
    ask_quantity_.store(new_ask_qty);
}

void OrderBook::insert_bid_level(const PriceLevel& level) {
    // Find insertion point and shift if necessary
    for (size_t i = 0; i < MAX_DEPTH; ++i) {
        if (bid_levels_[i].total_quantity == 0 || level.price > bid_levels_[i].price) {
            // Shift levels down
            for (size_t j = MAX_DEPTH - 1; j > i; --j) {
                bid_levels_[j] = bid_levels_[j - 1];
            }
            bid_levels_[i] = level;
            break;
        }
    }
}

void OrderBook::insert_ask_level(const PriceLevel& level) {
    // Find insertion point and shift if necessary
    for (size_t i = 0; i < MAX_DEPTH; ++i) {
        if (ask_levels_[i].total_quantity == 0 || level.price < ask_levels_[i].price) {
            // Shift levels down
            for (size_t j = MAX_DEPTH - 1; j > i; --j) {
                ask_levels_[j] = ask_levels_[j - 1];
            }
            ask_levels_[i] = level;
            break;
        }
    }
}

void OrderBook::remove_bid_level(double price) {
    for (size_t i = 0; i < MAX_DEPTH; ++i) {
        if (std::abs(bid_levels_[i].price - price) < tick_size_ / 2.0) {
            // Shift levels up
            for (size_t j = i; j < MAX_DEPTH - 1; ++j) {
                bid_levels_[j] = bid_levels_[j + 1];
            }
            bid_levels_[MAX_DEPTH - 1] = PriceLevel{};
            break;
        }
    }
}

void OrderBook::remove_ask_level(double price) {
    for (size_t i = 0; i < MAX_DEPTH; ++i) {
        if (std::abs(ask_levels_[i].price - price) < tick_size_ / 2.0) {
            // Shift levels up
            for (size_t j = i; j < MAX_DEPTH - 1; ++j) {
                ask_levels_[j] = ask_levels_[j + 1];
            }
            ask_levels_[MAX_DEPTH - 1] = PriceLevel{};
            break;
        }
    }
}

// OrderBookManager implementation
OrderBookManager::OrderBookManager() : total_updates_(0), total_latency_ns_(0.0) {}

OrderBookManager::~OrderBookManager() = default;

bool OrderBookManager::add_symbol(uint64_t symbol_id, double tick_size) {
    std::unique_lock<std::shared_mutex> lock(books_mutex_);
    
    if (order_books_.find(symbol_id) != order_books_.end()) {
        return false; // Symbol already exists
    }
    
    order_books_[symbol_id] = std::make_unique<OrderBook>(symbol_id, tick_size);
    return true;
}

void OrderBookManager::remove_symbol(uint64_t symbol_id) {
    std::unique_lock<std::shared_mutex> lock(books_mutex_);
    order_books_.erase(symbol_id);
}

OrderBook* OrderBookManager::get_order_book(uint64_t symbol_id) {
    std::shared_lock<std::shared_mutex> lock(books_mutex_);
    
    auto it = order_books_.find(symbol_id);
    if (it != order_books_.end()) {
        return it->second.get();
    }
    
    return nullptr;
}

const OrderBook* OrderBookManager::get_order_book(uint64_t symbol_id) const {
    std::shared_lock<std::shared_mutex> lock(books_mutex_);
    
    auto it = order_books_.find(symbol_id);
    if (it != order_books_.end()) {
        return it->second.get();
    }
    
    return nullptr;
}

void OrderBookManager::process_market_data_batch(const std::vector<QuoteMessage>& quotes) {
    for (const auto& quote : quotes) {
        auto* book = get_order_book(quote.symbol_id);
        if (book) {
            book->update_quote(quote);
            total_updates_.fetch_add(1);
        }
    }
}

void OrderBookManager::process_trade_batch(const std::vector<TradeMessage>& trades) {
    for (const auto& trade : trades) {
        auto* book = get_order_book(trade.symbol_id);
        if (book) {
            book->update_trade(trade.price, trade.quantity, trade.header.timestamp);
            total_updates_.fetch_add(1);
        }
    }
}

uint64_t OrderBookManager::get_total_updates() const {
    return total_updates_.load();
}

double OrderBookManager::get_average_latency() const {
    uint64_t updates = total_updates_.load();
    if (updates == 0) return 0.0;
    
    return total_latency_ns_.load() / updates;
}

std::vector<OrderBookManager::MarketSummary> OrderBookManager::get_market_summary() const {
    std::shared_lock<std::shared_mutex> lock(books_mutex_);
    
    std::vector<MarketSummary> summaries;
    summaries.reserve(order_books_.size());
    
    for (const auto& [symbol_id, book] : order_books_) {
        MarketSummary summary;
        summary.symbol_id = symbol_id;
        summary.last_price = book->get_mid_price();
        summary.bid = book->get_best_bid();
        summary.ask = book->get_best_ask();
        summary.volume = book->get_total_volume();
        summary.change_percent = 0.0; // Would need historical data
        
        summaries.push_back(summary);
    }
    
    return summaries;
}

// OrderBookAnalytics implementation
OrderBookAnalytics::OrderBookAnalytics(const OrderBook* book) : order_book_(book) {}

OrderBookAnalytics::Signal OrderBookAnalytics::generate_signal() const {
    Signal signal;
    signal.signal_type = Signal::NEUTRAL;
    signal.confidence = 0.0;
    signal.price_target = order_book_->get_mid_price();
    signal.timestamp = std::chrono::duration_cast<Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    
    // Simple signal generation based on imbalance
    double imbalance = order_book_->get_imbalance();
    if (imbalance > 0.3) {
        signal.signal_type = Signal::BUY;
        signal.confidence = std::min(imbalance, 1.0);
    } else if (imbalance < -0.3) {
        signal.signal_type = Signal::SELL;
        signal.confidence = std::min(-imbalance, 1.0);
    }
    
    return signal;
}

double OrderBookAnalytics::calculate_price_impact(double quantity, char side) const {
    // Simplified price impact calculation
    double total_quantity = 0;
    double weighted_price = 0;
    
    if (side == 'B' || side == 'b') {
        // Calculate impact on ask side
        const auto& ask_levels = order_book_->get_ask_levels();
        for (size_t i = 0; i < OrderBook::MAX_DEPTH && quantity > 0; ++i) {
            if (ask_levels[i].total_quantity > 0) {
                uint64_t level_qty = std::min(static_cast<uint64_t>(quantity), ask_levels[i].total_quantity);
                weighted_price += ask_levels[i].price * level_qty;
                total_quantity += level_qty;
                quantity -= level_qty;
            }
        }
    } else {
        // Calculate impact on bid side
        const auto& bid_levels = order_book_->get_bid_levels();
        for (size_t i = 0; i < OrderBook::MAX_DEPTH && quantity > 0; ++i) {
            if (bid_levels[i].total_quantity > 0) {
                uint64_t level_qty = std::min(static_cast<uint64_t>(quantity), bid_levels[i].total_quantity);
                weighted_price += bid_levels[i].price * level_qty;
                total_quantity += level_qty;
                quantity -= level_qty;
            }
        }
    }
    
    if (total_quantity == 0) return 0.0;
    
    double avg_execution_price = weighted_price / total_quantity;
    double mid_price = order_book_->get_mid_price();
    
    return std::abs(avg_execution_price - mid_price) / mid_price;
}

double OrderBookAnalytics::estimate_execution_cost(double quantity, char side) const {
    double price_impact = calculate_price_impact(quantity, side);
    double spread = order_book_->get_spread();
    double mid_price = order_book_->get_mid_price();
    
    // Execution cost = spread cost + market impact cost
    double spread_cost = (spread / 2.0) / mid_price; // Half-spread as percentage
    double impact_cost = price_impact;
    
    return spread_cost + impact_cost;
}

double OrderBookAnalytics::get_order_flow_imbalance() const {
    return order_book_->get_imbalance();
}

double OrderBookAnalytics::get_effective_spread() const {
    return order_book_->get_spread();
}

double OrderBookAnalytics::get_quoted_spread() const {
    return order_book_->get_spread();
}

double OrderBookAnalytics::get_depth_at_price(double price, char side) const {
    double total_depth = 0.0;
    
    if (side == 'B' || side == 'b') {
        const auto& bid_levels = order_book_->get_bid_levels();
        for (size_t i = 0; i < OrderBook::MAX_DEPTH; ++i) {
            if (bid_levels[i].price >= price && bid_levels[i].total_quantity > 0) {
                total_depth += bid_levels[i].total_quantity;
            }
        }
    } else {
        const auto& ask_levels = order_book_->get_ask_levels();
        for (size_t i = 0; i < OrderBook::MAX_DEPTH; ++i) {
            if (ask_levels[i].price <= price && ask_levels[i].total_quantity > 0) {
                total_depth += ask_levels[i].total_quantity;
            }
        }
    }
    
    return total_depth;
}

double OrderBookAnalytics::calculate_vpin() const {
    // Simplified VPIN calculation
    return std::abs(order_book_->get_imbalance());
}

double OrderBookAnalytics::calculate_kyle_lambda() const {
    // Simplified Kyle's lambda - would need trade data
    double spread = order_book_->get_spread();
    double mid_price = order_book_->get_mid_price();
    
    if (mid_price > 0) {
        return spread / mid_price;
    }
    
    return 0.0;
}

double OrderBookAnalytics::get_book_pressure() const {
    return order_book_->get_imbalance();
}

} // namespace goldearn::market_data