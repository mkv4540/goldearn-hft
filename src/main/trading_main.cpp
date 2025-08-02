#include <iostream>
#include <signal.h>
#include <atomic>
#include <thread>
#include <chrono>
#include "../utils/simple_logger.hpp"
#include "../market_data/nse_protocol.hpp"
#include "../market_data/order_book.hpp"
#include "../core/latency_tracker.hpp"
#include "../config/config_manager.hpp"

using namespace goldearn;

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        LOG_INFO("Received shutdown signal");
        g_running = false;
    }
}

void print_banner() {
    std::cout << R"(
   ____       _     _ _____                  _   _ _____ _____ 
  / ___| ___ | | __| | ____|__ _ _ __ _ __  | | | |  ___|_   _|
 | |  _ / _ \| |/ _` |  _| / _` | '__| '_ \ | |_| | |_    | |  
 | |_| | (_) | | (_| | |__| (_| | |  | | | ||  _  |  _|   | |  
  \____|\___/|_|\__,_|_____\__,_|_|  |_| |_||_| |_|_|     |_|  
                                                               
  High-Frequency Trading System v1.0.0
  =====================================
)" << std::endl;
}

class TradingEngine {
public:
    TradingEngine() : latency_tracker_("TradingEngine") {
        LOG_INFO("Initializing GoldEarn HFT Trading Engine");
    }
    
    bool initialize(const std::string& config_file) {
        LOG_INFO("Loading configuration from: {}", config_file);
        
        // Load configuration
        auto& config = config::ConfigManager::instance();
        if (!config.load_from_file(config_file)) {
            LOG_ERROR("Failed to load configuration file: {}", config_file);
            return false;
        }
        
        // Load environment variables
        config.load_from_environment();
        
        // Validate configuration
        if (!config.validate()) {
            LOG_ERROR("Configuration validation failed");
            return false;
        }
        
        // Check if we're in production mode with invalid endpoints
        if (config.is_production()) {
            if (!config::ProductionConfig::validate_production_config(config)) {
                LOG_ERROR("Production configuration validation failed");
                return false;
            }
        }
        
        // Initialize market data feed
        if (!init_market_data(config)) {
            LOG_ERROR("Failed to initialize market data feed");
            return false;
        }
        
        // Initialize order books
        if (!init_order_books()) {
            LOG_ERROR("Failed to initialize order books");
            return false;
        }
        
        // Initialize risk management
        if (!init_risk_management()) {
            LOG_ERROR("Failed to initialize risk management");
            return false;
        }
        
        // Initialize trading strategies
        if (!init_strategies()) {
            LOG_ERROR("Failed to initialize trading strategies");
            return false;
        }
        
        LOG_INFO("Trading engine initialized successfully");
        return true;
    }
    
    void run() {
        LOG_INFO("Starting trading engine main loop");
        
        auto last_health_check = std::chrono::steady_clock::now();
        const auto health_check_interval = std::chrono::seconds(30);
        
        while (g_running) {
            // Process market data
            process_market_data();
            
            // Run trading strategies
            run_strategies();
            
            // Process orders
            process_orders();
            
            // Health check
            auto now = std::chrono::steady_clock::now();
            if (now - last_health_check >= health_check_interval) {
                perform_health_check();
                last_health_check = now;
            }
            
            // Minimal sleep to prevent CPU spinning
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        
        LOG_INFO("Trading engine main loop terminated");
    }
    
    void shutdown() {
        LOG_INFO("Shutting down trading engine");
        
        // Cancel all pending orders
        cancel_all_orders();
        
        // Close all positions
        close_all_positions();
        
        // Disconnect from market data
        if (nse_parser_) {
            nse_parser_->disconnect();
        }
        
        // Print final statistics
        print_statistics();
        
        LOG_INFO("Trading engine shutdown complete");
    }
    
private:
    bool init_market_data(const config::ConfigManager& config) {
        nse_parser_ = std::make_unique<market_data::nse::NSEProtocolParser>();
        
        // Set up callbacks
        nse_parser_->set_trade_callback(
            [this](const auto& header, const void* data) {
                handle_trade_message(header, data);
            });
        
        nse_parser_->set_quote_callback(
            [this](const auto& header, const void* data) {
                handle_quote_message(header, data);
            });
        
        // Get connection details from configuration
        std::string nse_host = config.get_string("market_data", "nse_host", "127.0.0.1");
        uint16_t nse_port = static_cast<uint16_t>(config.get_int("market_data", "nse_port", 9899));
        
        LOG_INFO("Connecting to NSE feed at {}:{}", nse_host, nse_port);
        
        // Validate production endpoints
        if (config.is_production() && (nse_host == "127.0.0.1" || nse_host == "localhost" || 
                                      nse_host.find("example.com") != std::string::npos)) {
            LOG_ERROR("Production mode cannot use test endpoints: {}", nse_host);
            return false;
        }
        
        // Connect to NSE feed
        if (!nse_parser_->connect_to_feed(nse_host, nse_port)) {
            LOG_ERROR("Failed to connect to NSE feed at {}:{}", nse_host, nse_port);
            return false;
        }
        
        return true;
    }
    
    bool init_order_books() {
        // Initialize order books for tracked symbols
        const std::vector<std::string> symbols = {"RELIANCE", "TCS", "HDFCBANK", "NIFTY", "BANKNIFTY"};
        
        for (const auto& symbol : symbols) {
            order_books_[symbol] = std::make_unique<market_data::OrderBook>(symbol);
            LOG_INFO("Created order book for {}", symbol);
        }
        
        return true;
    }
    
    bool init_risk_management() {
        // Initialize risk parameters
        max_position_value_ = 10000000.0;  // 10M INR
        max_daily_loss_ = 500000.0;        // 500K INR
        current_daily_pnl_ = 0.0;
        
        LOG_INFO("Risk management initialized - Max position: {}, Max daily loss: {}", 
                 max_position_value_, max_daily_loss_);
        return true;
    }
    
    bool init_strategies() {
        // Initialize trading strategies
        // TODO: Load actual strategy implementations
        LOG_INFO("Trading strategies initialized");
        return true;
    }
    
    void process_market_data() {
        // Market data processing is handled by callbacks
        // This could aggregate data or trigger events
    }
    
    void run_strategies() {
        // Execute trading strategies
        // TODO: Implement strategy execution logic
    }
    
    void process_orders() {
        // Process order queue
        // TODO: Implement order processing
    }
    
    void cancel_all_orders() {
        LOG_INFO("Cancelling all pending orders");
        // TODO: Implement order cancellation
    }
    
    void close_all_positions() {
        LOG_INFO("Closing all open positions");
        // TODO: Implement position closing
    }
    
    void perform_health_check() {
        LOG_INFO("Performing health check");
        
        // Check market data connectivity
        if (nse_parser_) {
            uint64_t messages = nse_parser_->get_messages_processed();
            uint64_t errors = nse_parser_->get_parse_errors();
            LOG_INFO("Market data - Messages: {}, Errors: {}", messages, errors);
        }
        
        // Check system resources
        // TODO: Add CPU, memory, network checks
        
        // Check latency metrics
        auto stats = latency_tracker_.get_stats();
        LOG_INFO("Latency - Avg: {:.2f}μs, P99: {:.2f}μs, Max: {:.2f}μs",
                 stats.avg_latency_us, stats.p99_latency_us, stats.max_latency_us);
    }
    
    void print_statistics() {
        LOG_INFO("=== Trading Session Statistics ===");
        LOG_INFO("Daily P&L: {:.2f} INR", current_daily_pnl_);
        LOG_INFO("Messages processed: {}", 
                 nse_parser_ ? nse_parser_->get_messages_processed() : 0);
        
        auto latency_stats = latency_tracker_.get_stats();
        LOG_INFO("Latency Statistics:");
        LOG_INFO("  Average: {:.2f} μs", latency_stats.avg_latency_us);
        LOG_INFO("  P50: {:.2f} μs", latency_stats.p50_latency_us);
        LOG_INFO("  P95: {:.2f} μs", latency_stats.p95_latency_us);
        LOG_INFO("  P99: {:.2f} μs", latency_stats.p99_latency_us);
        LOG_INFO("  Max: {:.2f} μs", latency_stats.max_latency_us);
        LOG_INFO("================================");
    }
    
    void handle_trade_message(const market_data::MessageHeader& header, const void* data) {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Process trade message
        // TODO: Implement trade processing
        
        auto end = std::chrono::high_resolution_clock::now();
        latency_tracker_.record_latency(start, end);
    }
    
    void handle_quote_message(const market_data::MessageHeader& header, const void* data) {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Process quote message
        // TODO: Implement quote processing
        
        auto end = std::chrono::high_resolution_clock::now();
        latency_tracker_.record_latency(start, end);
    }
    
private:
    // Market data
    std::unique_ptr<market_data::nse::NSEProtocolParser> nse_parser_;
    std::unordered_map<std::string, std::unique_ptr<market_data::OrderBook>> order_books_;
    
    // Risk management
    double max_position_value_;
    double max_daily_loss_;
    std::atomic<double> current_daily_pnl_;
    
    // Performance tracking
    core::LatencyTracker latency_tracker_;
};

int main(int argc, char* argv[]) {
    // Print banner
    print_banner();
    
    // Initialize logger
    utils::Logger::init("goldearn_engine.log");
    LOG_INFO("GoldEarn HFT Trading Engine starting");
    
    // Parse command line arguments
    std::string config_file = "config/production.conf";
    if (argc > 1 && std::string(argv[1]) == "--config" && argc > 2) {
        config_file = argv[2];
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create and initialize trading engine
    TradingEngine engine;
    
    if (!engine.initialize(config_file)) {
        LOG_ERROR("Failed to initialize trading engine");
        return 1;
    }
    
    // Run the engine
    LOG_INFO("Trading engine running, press Ctrl+C to stop");
    engine.run();
    
    // Shutdown
    engine.shutdown();
    
    LOG_INFO("GoldEarn HFT Trading Engine terminated");
    return 0;
}