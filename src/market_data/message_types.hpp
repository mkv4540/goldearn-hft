#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <array>

namespace goldearn::market_data {

// High-precision timestamp using nanoseconds since epoch
using Timestamp = std::chrono::nanoseconds;

// Market data message types for NSE/BSE protocols
enum class MessageType : uint8_t {
    TRADE = 1,
    QUOTE = 2,
    ORDER_UPDATE = 3,
    MARKET_STATUS = 4,
    SYMBOL_UPDATE = 5,
    INDEX_UPDATE = 6,
    HEARTBEAT = 7
};

// Exchange identifiers
enum class Exchange : uint8_t {
    NSE = 1,
    BSE = 2,
    MCX = 3  // For future commodities support
};

// Instrument types
enum class InstrumentType : uint8_t {
    EQUITY = 1,
    DERIVATIVE = 2,
    COMMODITY = 3,
    CURRENCY = 4,
    INDEX = 5,
    FUTURE = 6,
    OPTION = 7
};

// Base message header for all market data
struct __attribute__((packed)) MessageHeader {
    MessageType msg_type;
    Exchange exchange;
    uint32_t msg_length;
    Timestamp timestamp;
    uint64_t sequence_number;
};

// Trade message structure
struct __attribute__((packed)) TradeMessage {
    MessageHeader header;
    uint64_t symbol_id;
    uint64_t trade_id;
    double price;
    uint64_t quantity;
    char buyer_broker[8];
    char seller_broker[8];
    Timestamp trade_time;
};

// Quote/Level-II data message
struct __attribute__((packed)) QuoteMessage {
    MessageHeader header;
    uint64_t symbol_id;
    
    // Best bid/ask
    double bid_price;
    uint64_t bid_quantity;
    double ask_price;
    uint64_t ask_quantity;
    
    // Market depth (5 levels each side)
    struct Level {
        double price;
        uint64_t quantity;
        uint16_t num_orders;
    };
    
    std::array<Level, 5> bid_levels;
    std::array<Level, 5> ask_levels;
    
    Timestamp quote_time;
};

// Order update message
struct __attribute__((packed)) OrderUpdateMessage {
    MessageHeader header;
    uint64_t symbol_id;
    uint64_t order_id;
    char order_type;  // 'B' = Buy, 'S' = Sell
    double price;
    uint64_t quantity;
    uint64_t disclosed_quantity;
    char order_status;  // 'N' = New, 'M' = Modified, 'C' = Cancelled
    Timestamp order_time;
};

// Market status message
struct __attribute__((packed)) MarketStatusMessage {
    MessageHeader header;
    char market_status[16];  // "OPEN", "CLOSED", "PRE_OPEN", etc.
    Timestamp status_time;
};

// Symbol master data
struct __attribute__((packed)) SymbolUpdateMessage {
    MessageHeader header;
    uint64_t symbol_id;
    char symbol_name[32];
    char isin[16];
    InstrumentType instrument_type;
    double tick_size;
    uint64_t lot_size;
    double upper_circuit;
    double lower_circuit;
    Timestamp update_time;
};

// Index data message
struct __attribute__((packed)) IndexUpdateMessage {
    MessageHeader header;
    uint64_t index_id;
    char index_name[32];
    double index_value;
    double change;
    double change_percent;
    uint64_t market_cap;
    Timestamp index_time;
};

// Heartbeat message for connection monitoring
struct __attribute__((packed)) HeartbeatMessage {
    MessageHeader header;
    uint32_t heartbeat_id;
    Timestamp heartbeat_time;
};

} // namespace goldearn::market_data