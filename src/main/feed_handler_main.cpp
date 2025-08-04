#include <iostream>
#include <signal.h>
#include <atomic>
#include <thread>
#include <chrono>
#include "../utils/simple_logger.hpp"
#include "../market_data/nse_protocol.hpp"
#include "../market_data/order_book.hpp"
#include "../core/latency_tracker.hpp"

using namespace goldearn;

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        LOG_INFO("Feed handler received shutdown signal");
        g_running = false;
    }
}

class FeedHandler {
public:
    FeedHandler() : latency_tracker_("FeedHandler") {
        LOG_INFO("Initializing GoldEarn HFT Feed Handler");
    }
    
    bool initialize(const std::string& config_file) {
        LOG_INFO("Loading feed handler configuration from: {}", config_file);
        
        // Initialize NSE parser
        nse_parser_ = std::make_unique<market_data::nse::NSEProtocolParser>();
        
        // Set up message callbacks
        setup_callbacks();
        
        // Initialize symbol manager
        symbol_manager_ = std::make_unique<market_data::nse::NSESymbolManager>();
        if (!symbol_manager_->load_symbol_master("config/nse_symbols.csv")) {
            LOG_WARN("Failed to load symbol master, using defaults");
        }
        
        // Initialize order books for all symbols
        init_order_books();
        
        LOG_INFO("Feed handler initialized successfully");
        return true;
    }
    
    bool connect(const std::string& host, uint16_t port) {
        LOG_INFO("Connecting to market data feed at {}:{}", host, port);
        
        if (!nse_parser_->connect_to_feed(host, port)) {
            LOG_ERROR("Failed to connect to market data feed");
            return false;
        }
        
        LOG_INFO("Successfully connected to market data feed");
        return true;
    }
    
    void run() {
        LOG_INFO("Feed handler running");
        
        auto last_stats_time = std::chrono::steady_clock::now();
        const auto stats_interval = std::chrono::seconds(60);
        
        while (g_running) {
            // Main processing happens in callbacks
            
            // Print statistics periodically
            auto now = std::chrono::steady_clock::now();
            if (now - last_stats_time >= stats_interval) {
                print_statistics();
                last_stats_time = now;
            }
            
            // Sleep to prevent CPU spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        LOG_INFO("Feed handler stopped");
    }
    
    void shutdown() {
        LOG_INFO("Shutting down feed handler");
        
        if (nse_parser_) {
            nse_parser_->disconnect();
        }
        
        print_final_statistics();
        
        LOG_INFO("Feed handler shutdown complete");
    }
    
private:
    void setup_callbacks() {
        // Trade message callback
        nse_parser_->set_trade_callback(
            [this](const market_data::MessageHeader& header, const void* data) {
                handle_trade_message(header, data);
            });
        
        // Quote message callback
        nse_parser_->set_quote_callback(
            [this](const market_data::MessageHeader& header, const void* data) {
                handle_quote_message(header, data);
            });
        
        // Order update callback
        nse_parser_->set_order_callback(
            [this](const market_data::MessageHeader& header, const void* data) {
                handle_order_message(header, data);
            });
    }
    
    void init_order_books() {
        if (!symbol_manager_) return;
        
        for (size_t i = 0; i < symbol_manager_->get_symbol_count(); ++i) {
            const auto* info = symbol_manager_->get_symbol_info(i);
            if (info) {
                order_books_[info->symbol_name] = 
                    std::make_unique<market_data::OrderBook>(info->symbol_name);
                LOG_INFO("Created order book for {}", info->symbol_name);
            }
        }
    }
    
    void handle_trade_message(const market_data::MessageHeader& header, const void* data) {
        auto start = std::chrono::high_resolution_clock::now();
        
        const auto* trade_msg = static_cast<const market_data::TradeMessage*>(data);
        
        // Update statistics
        trade_count_++;
        total_trade_volume_ += trade_msg->quantity;
        total_trade_value_ = total_trade_value_.load() + (trade_msg->price * trade_msg->quantity);
        
        // Get symbol name
        const auto* symbol_info = symbol_manager_->get_symbol_info(trade_msg->symbol_id);
        if (!symbol_info) {
            LOG_WARN("Unknown symbol ID in trade: {}", trade_msg->symbol_id);
            return;
        }
        
        // Update order book
        auto it = order_books_.find(symbol_info->symbol_name);
        if (it != order_books_.end()) {
            // Trade updates might affect the order book state
            LOG_DEBUG("Trade: {} - Price: {}, Qty: {}, Side: {}", 
                     symbol_info->symbol_name, trade_msg->price, 
                     trade_msg->quantity, 'T');
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        latency_tracker_.record_latency(duration);
    }
    
    void handle_quote_message(const market_data::MessageHeader& header, const void* data) {
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            auto quote_msg = nse_parser_->parse_nse_quote(static_cast<const uint8_t*>(data));
            
            // Get symbol information
            const auto* symbol_info = symbol_manager_->get_symbol_info(quote_msg.symbol_id);
            if (!symbol_info) {
                LOG_WARNING("Unknown symbol ID: {}", quote_msg.symbol_id);
                return;
            }
            
            // Update order book
            auto& order_book = order_books_[symbol_info->symbol_name];
            if (!order_book) {
                order_book = std::make_unique<market_data::OrderBook>(symbol_info->symbol_name);
            }
            
            // Create QuoteMessage object and update order book
            market_data::QuoteMessage quote;
            quote.symbol_id = quote_msg.symbol_id;
            quote.bid_price = quote_msg.bid_price;
            quote.bid_quantity = quote_msg.bid_quantity;
            quote.ask_price = quote_msg.ask_price;
            quote.ask_quantity = quote_msg.ask_quantity;
            quote.quote_time = quote_msg.quote_time;
            
            order_book->update_quote(quote);
            quote_count_++;
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error processing quote message: {}", e.what());
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        latency_tracker_.record_latency(duration);
    }
    
    void handle_order_message(const market_data::MessageHeader& header, const void* data) {
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            auto order_msg = nse_parser_->parse_nse_order(static_cast<const uint8_t*>(data));
            
            // Get symbol information
            const auto* symbol_info = symbol_manager_->get_symbol_info(order_msg.symbol_id);
            if (!symbol_info) {
                LOG_WARNING("Unknown symbol ID: {}", order_msg.symbol_id);
                return;
            }
            
            // Update order book
            auto& order_book = order_books_[symbol_info->symbol_name];
            if (!order_book) {
                order_book = std::make_unique<market_data::OrderBook>(symbol_info->symbol_name);
            }
            
            // Add order to order book with timestamp
            order_book->add_order(order_msg.order_id, order_msg.order_type, order_msg.price, order_msg.quantity, order_msg.order_time);
            order_update_count_++;
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error processing order message: {}", e.what());
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        latency_tracker_.record_latency(duration);
    }
    
    void print_statistics() {
        LOG_INFO("=== Feed Handler Statistics ===");
        LOG_INFO("Messages processed: {}", nse_parser_->get_messages_processed());
        LOG_INFO("Parse errors: {}", nse_parser_->get_parse_errors());
        LOG_INFO("Trades: {}, Volume: {}, Value: {:.2f}", 
                 trade_count_.load(), total_trade_volume_.load(), total_trade_value_.load());
        LOG_INFO("Quotes: {}", quote_count_.load());
        LOG_INFO("Order updates: {}", order_update_count_.load());
        
        auto stats = latency_tracker_.get_stats();
        LOG_INFO("Processing latency - Avg: {:.2f}μs, P99: {:.2f}μs", 
                 stats.avg_latency_us, stats.p99_latency_us);
        LOG_INFO("=============================");
    }
    
    void print_final_statistics() {
        LOG_INFO("=== Final Feed Handler Statistics ===");
        LOG_INFO("Total messages: {}", nse_parser_->get_messages_processed());
        LOG_INFO("Total errors: {}", nse_parser_->get_parse_errors());
        LOG_INFO("Error rate: {:.4f}%", 
                 nse_parser_->get_messages_processed() > 0 ?
                 (100.0 * nse_parser_->get_parse_errors() / nse_parser_->get_messages_processed()) : 0.0);
        
        auto stats = latency_tracker_.get_stats();
        LOG_INFO("Latency Statistics:");
        LOG_INFO("  Count: {}", stats.count);
        LOG_INFO("  Average: {:.2f} μs", stats.avg_latency_us);
        LOG_INFO("  Min: {:.2f} μs", stats.min_latency_us);
        LOG_INFO("  P50: {:.2f} μs", stats.p50_latency_us);
        LOG_INFO("  P95: {:.2f} μs", stats.p95_latency_us);
        LOG_INFO("  P99: {:.2f} μs", stats.p99_latency_us);
        LOG_INFO("  Max: {:.2f} μs", stats.max_latency_us);
        LOG_INFO("==================================");
    }
    
private:
    // Market data components
    std::unique_ptr<market_data::nse::NSEProtocolParser> nse_parser_;
    std::unique_ptr<market_data::nse::NSESymbolManager> symbol_manager_;
    std::unordered_map<std::string, std::unique_ptr<market_data::OrderBook>> order_books_;
    
    // Statistics
    std::atomic<uint64_t> trade_count_{0};
    std::atomic<uint64_t> quote_count_{0};
    std::atomic<uint64_t> order_update_count_{0};
    std::atomic<uint64_t> total_trade_volume_{0};
    std::atomic<double> total_trade_value_{0.0};
    
    // Performance tracking
    core::LatencyTracker latency_tracker_;
};

int main(int argc, char* argv[]) {
    // Initialize logger - use SimpleLogger instead of Logger
    LOG_INFO("GoldEarn HFT Feed Handler starting");
    
    // Parse command line arguments
    std::string config_file = "config/feed_handler.conf";
    std::string host = "127.0.0.1";
    uint16_t port = 9899;
    
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (std::string(argv[i]) == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (std::string(argv[i]) == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (std::string(argv[i]) == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --config <file>   Configuration file (default: config/feed_handler.conf)\n";
            std::cout << "  --host <host>     Market data host (default: 127.0.0.1)\n";
            std::cout << "  --port <port>     Market data port (default: 9899)\n";
            std::cout << "  --help            Show this help message\n";
            return 0;
        }
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create and initialize feed handler
    FeedHandler handler;
    
    if (!handler.initialize(config_file)) {
        LOG_ERROR("Failed to initialize feed handler");
        return 1;
    }
    
    // Connect to market data feed
    if (!handler.connect(host, port)) {
        LOG_ERROR("Failed to connect to market data feed");
        return 1;
    }
    
    // Run the feed handler
    LOG_INFO("Feed handler running, press Ctrl+C to stop");
    handler.run();
    
    // Shutdown
    handler.shutdown();
    LOG_INFO("Feed handler shutdown complete");
    
    return 0;
}