#pragma once

#include "message_types.hpp"
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include <string>
#include <thread>
#include "../core/rate_limiter.hpp"

namespace goldearn::market_data::nse {

// NSE NEAT protocol constants
constexpr uint16_t NSE_PORT = 9899;
constexpr size_t MAX_MESSAGE_SIZE = 4096;
constexpr size_t BUFFER_SIZE = 1024 * 1024; // 1MB ring buffer

// NSE message parsing states
enum class ParserState {
    WAITING_HEADER,
    READING_PAYLOAD,
    MESSAGE_COMPLETE,
    ERROR_STATE
};

// NSE protocol parser for real-time feeds
class NSEProtocolParser {
public:
    using MessageCallback = std::function<void(const MessageHeader&, const void*)>;
    
    NSEProtocolParser();
    ~NSEProtocolParser();
    
    // Parse incoming data buffer
    size_t parse_buffer(const uint8_t* data, size_t length);
    
    // Register message callbacks
    void set_trade_callback(MessageCallback callback);
    void set_quote_callback(MessageCallback callback);
    void set_order_callback(MessageCallback callback);
    
    // Connection management
    bool connect_to_feed(const std::string& host, uint16_t port);
    void disconnect();
    
    // Statistics
    uint64_t get_messages_processed() const { return messages_processed_; }
    uint64_t get_parse_errors() const { return parse_errors_; }
    
private:
    ParserState state_;
    uint8_t* buffer_;
    size_t buffer_pos_;
    size_t expected_message_size_;
    
    MessageCallback trade_callback_;
    MessageCallback quote_callback_;
    MessageCallback order_callback_;
    
    uint64_t messages_processed_;
    uint64_t parse_errors_;
    
    // Network connection
    int socket_fd_ = -1;
    bool connected_ = false;
    std::string host_;
    uint16_t port_ = 0;
    std::thread receiver_thread_;
    
    // Internal parsing methods
    bool parse_header(const uint8_t* data, MessageHeader& header);
    bool validate_message(const MessageHeader& header, const uint8_t* payload);
    void dispatch_message(const MessageHeader& header, const uint8_t* payload);
    
    // Network methods
    void receive_thread_func();
    void reset_parser_state();
    
    // Rate limiting
    std::unique_ptr<core::RateLimiter> message_rate_limiter_;
    std::unique_ptr<core::SlidingWindowRateLimiter> connection_rate_limiter_;
    
    // NSE-specific message conversion
    TradeMessage parse_nse_trade(const uint8_t* data);
    QuoteMessage parse_nse_quote(const uint8_t* data);
    OrderUpdateMessage parse_nse_order(const uint8_t* data);
};

// NSE symbol mapping and management
class NSESymbolManager {
public:
    struct SymbolInfo {
        uint64_t symbol_id;
        std::string symbol_name;
        std::string isin;
        InstrumentType type;
        double tick_size;
        uint64_t lot_size;
        double upper_circuit;
        double lower_circuit;
    };
    
    // Symbol resolution
    bool load_symbol_master(const std::string& filename);
    const SymbolInfo* get_symbol_info(uint64_t symbol_id) const;
    const SymbolInfo* get_symbol_info(const std::string& symbol_name) const;
    
    // Symbol ID mapping
    uint64_t get_symbol_id(const std::string& symbol_name) const;
    std::string get_symbol_name(uint64_t symbol_id) const;
    
    size_t get_symbol_count() const { return symbols_.size(); }
    
private:
    std::vector<SymbolInfo> symbols_;
    std::unordered_map<std::string, size_t> name_to_index_;
    std::unordered_map<uint64_t, size_t> id_to_index_;
};

// NSE market data feed handler
class NSEFeedHandler {
public:
    NSEFeedHandler();
    ~NSEFeedHandler();
    
    // Feed management
    bool start_feeds(const std::vector<std::string>& symbol_list);
    void stop_feeds();
    
    // Data subscriptions
    void subscribe_trades(const std::string& symbol);
    void subscribe_quotes(const std::string& symbol);
    void subscribe_orders(const std::string& symbol);
    
    // Callback registration
    void register_trade_handler(std::function<void(const TradeMessage&)> handler);
    void register_quote_handler(std::function<void(const QuoteMessage&)> handler);
    void register_order_handler(std::function<void(const OrderUpdateMessage&)> handler);
    
    // Statistics and health
    bool is_connected() const { return connected_; }
    double get_message_rate() const;
    Timestamp get_last_message_time() const { return last_message_time_; }
    
private:
    NSEProtocolParser parser_;
    NSESymbolManager symbol_manager_;
    
    bool connected_;
    Timestamp last_message_time_;
    uint64_t message_count_;
    
    std::function<void(const TradeMessage&)> trade_handler_;
    std::function<void(const QuoteMessage&)> quote_handler_;
    std::function<void(const OrderUpdateMessage&)> order_handler_;
    
    // Internal message handlers
    void handle_trade_message(const MessageHeader& header, const void* data);
    void handle_quote_message(const MessageHeader& header, const void* data);
    void handle_order_message(const MessageHeader& header, const void* data);
};

} // namespace goldearn::market_data::nse