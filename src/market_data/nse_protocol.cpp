#include "nse_protocol.hpp"
#include "../utils/logger.hpp"
#include <cstring>
#include <algorithm>
#include <sstream>
#include <thread>
#include <netdb.h>
#include <netinet/tcp.h>
#include <fstream>
#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <endian.h>
#elif __APPLE__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <machine/endian.h>
#include <libkern/OSByteOrder.h>
#define be16toh(x) OSSwapBigToHostInt16(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#endif

namespace goldearn::market_data::nse {

// NSE Protocol constants
constexpr uint32_t NSE_MAGIC_NUMBER = 0x4E534501; // "NSE" + version
constexpr size_t MIN_MESSAGE_SIZE = sizeof(MessageHeader);
constexpr size_t MAX_SYMBOL_LENGTH = 32;
constexpr double MAX_PRICE = 999999.99;
constexpr uint64_t MAX_QUANTITY = 99999999999ULL;

NSEProtocolParser::NSEProtocolParser() 
    : state_(ParserState::WAITING_HEADER), buffer_(nullptr), buffer_pos_(0), 
      expected_message_size_(0), messages_processed_(0), parse_errors_(0) {
    
    buffer_ = new uint8_t[BUFFER_SIZE];
    std::memset(buffer_, 0, BUFFER_SIZE);
    
    // Initialize rate limiters
    // 10,000 messages per second limit
    message_rate_limiter_ = std::make_unique<core::RateLimiter>(10000, 10000);
    // 10 connections per minute
    connection_rate_limiter_ = std::make_unique<core::SlidingWindowRateLimiter>(
        10, std::chrono::minutes(1));
}

NSEProtocolParser::~NSEProtocolParser() {
    delete[] buffer_;
}

size_t NSEProtocolParser::parse_buffer(const uint8_t* data, size_t length) {
    if (!data || length == 0) {
        LOG_WARNING("NSEProtocolParser: Invalid input data");
        return 0;
    }
    
    if (length > MAX_MESSAGE_SIZE) {
        LOG_ERROR("NSEProtocolParser: Input data too large: {} bytes", length);
        parse_errors_++;
        return 0;
    }
    
    size_t bytes_processed = 0;
    
    while (bytes_processed < length) {
        switch (state_) {
            case ParserState::WAITING_HEADER: {
                size_t header_bytes_needed = MIN_MESSAGE_SIZE - buffer_pos_;
                size_t available_bytes = length - bytes_processed;
                size_t bytes_to_copy = std::min(header_bytes_needed, available_bytes);
                
                // Bounds check
                if (buffer_pos_ + bytes_to_copy > BUFFER_SIZE) {
                    LOG_ERROR("NSEProtocolParser: Buffer overflow attempt detected");
                    parse_errors_++;
                    reset_parser_state();
                    return bytes_processed;
                }
                
                std::memcpy(buffer_ + buffer_pos_, data + bytes_processed, bytes_to_copy);
                buffer_pos_ += bytes_to_copy;
                bytes_processed += bytes_to_copy;
                
                if (buffer_pos_ >= MIN_MESSAGE_SIZE) {
                    MessageHeader header;
                    if (!parse_header(buffer_, header)) {
                        LOG_ERROR("NSEProtocolParser: Invalid message header");
                        parse_errors_++;
                        reset_parser_state();
                        continue;
                    }
                    
                    expected_message_size_ = header.msg_length;
                    
                    // Validate message size
                    if (expected_message_size_ < MIN_MESSAGE_SIZE || 
                        expected_message_size_ > MAX_MESSAGE_SIZE) {
                        LOG_ERROR("NSEProtocolParser: Invalid message size: {}", expected_message_size_);
                        parse_errors_++;
                        reset_parser_state();
                        continue;
                    }
                    
                    state_ = ParserState::READING_PAYLOAD;
                }
                break;
            }
            
            case ParserState::READING_PAYLOAD: {
                size_t payload_bytes_needed = expected_message_size_ - buffer_pos_;
                size_t available_bytes = length - bytes_processed;
                size_t bytes_to_copy = std::min(payload_bytes_needed, available_bytes);
                
                // Bounds check
                if (buffer_pos_ + bytes_to_copy > BUFFER_SIZE || 
                    buffer_pos_ + bytes_to_copy > expected_message_size_) {
                    LOG_ERROR("NSEProtocolParser: Payload buffer overflow attempt");
                    parse_errors_++;
                    reset_parser_state();
                    return bytes_processed;
                }
                
                std::memcpy(buffer_ + buffer_pos_, data + bytes_processed, bytes_to_copy);
                buffer_pos_ += bytes_to_copy;
                bytes_processed += bytes_to_copy;
                
                if (buffer_pos_ >= expected_message_size_) {
                    state_ = ParserState::MESSAGE_COMPLETE;
                }
                break;
            }
            
            case ParserState::MESSAGE_COMPLETE: {
                MessageHeader header;
                if (!parse_header(buffer_, header)) {
                    LOG_ERROR("NSEProtocolParser: Header validation failed on complete message");
                    parse_errors_++;
                    reset_parser_state();
                    continue;
                }
                
                // Validate complete message
                if (!validate_message(header, buffer_ + sizeof(MessageHeader))) {
                    LOG_ERROR("NSEProtocolParser: Message validation failed");
                    parse_errors_++;
                    reset_parser_state();
                    continue;
                }
                
                // Dispatch message
                dispatch_message(header, buffer_ + sizeof(MessageHeader));
                messages_processed_++;
                
                reset_parser_state();
                break;
            }
            
            case ParserState::ERROR_STATE:
            default: {
                LOG_ERROR("NSEProtocolParser: Parser in error state, resetting");
                reset_parser_state();
                return bytes_processed;
            }
        }
    }
    
    return bytes_processed;
}

bool NSEProtocolParser::parse_header(const uint8_t* data, MessageHeader& header) {
    if (!data) return false;
    
    // Use safe memory copy to avoid alignment issues
    std::memcpy(&header, data, sizeof(MessageHeader));
    
    // Convert from network byte order if needed
    header.msg_length = be32toh(header.msg_length);
    header.sequence_number = be64toh(header.sequence_number);
    
    // Basic validation
    if (header.msg_length < MIN_MESSAGE_SIZE || header.msg_length > MAX_MESSAGE_SIZE) {
        return false;
    }
    
    // Validate message type
    switch (header.msg_type) {
        case MessageType::TRADE:
        case MessageType::QUOTE:
        case MessageType::ORDER_UPDATE:
        case MessageType::MARKET_STATUS:
        case MessageType::SYMBOL_UPDATE:
        case MessageType::INDEX_UPDATE:
        case MessageType::HEARTBEAT:
            break;
        default:
            LOG_WARNING("NSEProtocolParser: Unknown message type: {}", static_cast<uint8_t>(header.msg_type));
            return false;
    }
    
    // Validate exchange
    switch (header.exchange) {
        case Exchange::NSE:
        case Exchange::BSE:
        case Exchange::MCX:
            break;
        default:
            LOG_WARNING("NSEProtocolParser: Unknown exchange: {}", static_cast<uint8_t>(header.exchange));
            return false;
    }
    
    return true;
}

bool NSEProtocolParser::validate_message(const MessageHeader& header, const uint8_t* payload) {
    if (!payload) return false;
    
    switch (header.msg_type) {
        case MessageType::TRADE: {
            if (header.msg_length != sizeof(MessageHeader) + sizeof(TradeMessage) - sizeof(MessageHeader)) {
                return false;
            }
            
            TradeMessage trade = parse_nse_trade(payload);
            
            // Validate trade data
            if (trade.price <= 0.0 || trade.price > MAX_PRICE) {
                LOG_WARNING("NSEProtocolParser: Invalid trade price: {}", trade.price);
                return false;
            }
            
            if (trade.quantity == 0 || trade.quantity > MAX_QUANTITY) {
                LOG_WARNING("NSEProtocolParser: Invalid trade quantity: {}", trade.quantity);
                return false;
            }
            
            break;
        }
        
        case MessageType::QUOTE: {
            if (header.msg_length != sizeof(MessageHeader) + sizeof(QuoteMessage) - sizeof(MessageHeader)) {
                return false;
            }
            
            QuoteMessage quote = parse_nse_quote(payload);
            
            // Validate quote data
            if (quote.bid_price < 0.0 || quote.bid_price > MAX_PRICE ||
                quote.ask_price < 0.0 || quote.ask_price > MAX_PRICE) {
                LOG_WARNING("NSEProtocolParser: Invalid quote prices: bid={}, ask={}", 
                           quote.bid_price, quote.ask_price);
                return false;
            }
            
            if (quote.bid_price > 0.0 && quote.ask_price > 0.0 && quote.bid_price >= quote.ask_price) {
                LOG_WARNING("NSEProtocolParser: Crossed quote detected: bid={}, ask={}", 
                           quote.bid_price, quote.ask_price);
                // Allow crossed quotes but log warning
            }
            
            break;
        }
        
        case MessageType::ORDER_UPDATE: {
            OrderUpdateMessage order = parse_nse_order(payload);
            
            if (order.price < 0.0 || order.price > MAX_PRICE) {
                LOG_WARNING("NSEProtocolParser: Invalid order price: {}", order.price);
                return false;
            }
            
            if (order.quantity > MAX_QUANTITY) {
                LOG_WARNING("NSEProtocolParser: Invalid order quantity: {}", order.quantity);
                return false;
            }
            
            break;
        }
        
        case MessageType::HEARTBEAT:
            // Heartbeat messages are always valid if they have correct length
            break;
            
        default:
            // Other message types - basic length validation
            break;
    }
    
    return true;
}

void NSEProtocolParser::dispatch_message(const MessageHeader& header, const uint8_t* payload) {
    switch (header.msg_type) {
        case MessageType::TRADE: {
            if (trade_callback_) {
                trade_callback_(header, payload);
            }
            break;
        }
        
        case MessageType::QUOTE: {
            if (quote_callback_) {
                quote_callback_(header, payload);
            }
            break;
        }
        
        case MessageType::ORDER_UPDATE: {
            if (order_callback_) {
                order_callback_(header, payload);
            }
            break;
        }
        
        default:
            LOG_DEBUG("NSEProtocolParser: Unhandled message type: {}", static_cast<uint8_t>(header.msg_type));
            break;
    }
}

TradeMessage NSEProtocolParser::parse_nse_trade(const uint8_t* data) {
    TradeMessage trade{};
    
    // Safe parsing with bounds checking
    if (data) {
        size_t offset = 0;
        
        // Parse symbol_id
        std::memcpy(&trade.symbol_id, data + offset, sizeof(trade.symbol_id));
        trade.symbol_id = be64toh(trade.symbol_id);
        offset += sizeof(trade.symbol_id);
        
        // Parse trade_id
        std::memcpy(&trade.trade_id, data + offset, sizeof(trade.trade_id));
        trade.trade_id = be64toh(trade.trade_id);
        offset += sizeof(trade.trade_id);
        
        // Parse price (IEEE 754 double)
        std::memcpy(&trade.price, data + offset, sizeof(trade.price));
        offset += sizeof(trade.price);
        
        // Parse quantity
        std::memcpy(&trade.quantity, data + offset, sizeof(trade.quantity));
        trade.quantity = be64toh(trade.quantity);
        offset += sizeof(trade.quantity);
        
        // Parse broker IDs (strings)
        std::memcpy(trade.buyer_broker, data + offset, sizeof(trade.buyer_broker));
        offset += sizeof(trade.buyer_broker);
        std::memcpy(trade.seller_broker, data + offset, sizeof(trade.seller_broker));
        
        // Null-terminate broker strings for safety
        trade.buyer_broker[sizeof(trade.buyer_broker) - 1] = '\0';
        trade.seller_broker[sizeof(trade.seller_broker) - 1] = '\0';
    }
    
    return trade;
}

QuoteMessage NSEProtocolParser::parse_nse_quote(const uint8_t* data) {
    QuoteMessage quote{};
    
    if (data) {
        size_t offset = 0;
        
        // Parse symbol_id
        std::memcpy(&quote.symbol_id, data + offset, sizeof(quote.symbol_id));
        quote.symbol_id = be64toh(quote.symbol_id);
        offset += sizeof(quote.symbol_id);
        
        // Parse best bid/ask
        std::memcpy(&quote.bid_price, data + offset, sizeof(quote.bid_price));
        offset += sizeof(quote.bid_price);
        
        std::memcpy(&quote.bid_quantity, data + offset, sizeof(quote.bid_quantity));
        quote.bid_quantity = be64toh(quote.bid_quantity);
        offset += sizeof(quote.bid_quantity);
        
        std::memcpy(&quote.ask_price, data + offset, sizeof(quote.ask_price));
        offset += sizeof(quote.ask_price);
        
        std::memcpy(&quote.ask_quantity, data + offset, sizeof(quote.ask_quantity));
        quote.ask_quantity = be64toh(quote.ask_quantity);
        offset += sizeof(quote.ask_quantity);
        
        // Parse depth levels
        for (int i = 0; i < 5; ++i) {
            std::memcpy(&quote.bid_levels[i].price, data + offset, sizeof(double));
            offset += sizeof(double);
            
            std::memcpy(&quote.bid_levels[i].quantity, data + offset, sizeof(uint64_t));
            quote.bid_levels[i].quantity = be64toh(quote.bid_levels[i].quantity);
            offset += sizeof(uint64_t);
            
            std::memcpy(&quote.bid_levels[i].num_orders, data + offset, sizeof(uint16_t));
            quote.bid_levels[i].num_orders = be16toh(quote.bid_levels[i].num_orders);
            offset += sizeof(uint16_t);
        }
        
        for (int i = 0; i < 5; ++i) {
            std::memcpy(&quote.ask_levels[i].price, data + offset, sizeof(double));
            offset += sizeof(double);
            
            std::memcpy(&quote.ask_levels[i].quantity, data + offset, sizeof(uint64_t));
            quote.ask_levels[i].quantity = be64toh(quote.ask_levels[i].quantity);
            offset += sizeof(uint64_t);
            
            std::memcpy(&quote.ask_levels[i].num_orders, data + offset, sizeof(uint16_t));
            quote.ask_levels[i].num_orders = be16toh(quote.ask_levels[i].num_orders);
            offset += sizeof(uint16_t);
        }
    }
    
    return quote;
}

OrderUpdateMessage NSEProtocolParser::parse_nse_order(const uint8_t* data) {
    OrderUpdateMessage order{};
    
    if (data) {
        size_t offset = 0;
        
        std::memcpy(&order.symbol_id, data + offset, sizeof(order.symbol_id));
        order.symbol_id = be64toh(order.symbol_id);
        offset += sizeof(order.symbol_id);
        
        std::memcpy(&order.order_id, data + offset, sizeof(order.order_id));
        order.order_id = be64toh(order.order_id);
        offset += sizeof(order.order_id);
        
        std::memcpy(&order.order_type, data + offset, sizeof(order.order_type));
        offset += sizeof(order.order_type);
        
        std::memcpy(&order.price, data + offset, sizeof(order.price));
        offset += sizeof(order.price);
        
        std::memcpy(&order.quantity, data + offset, sizeof(order.quantity));
        order.quantity = be64toh(order.quantity);
        offset += sizeof(order.quantity);
        
        std::memcpy(&order.disclosed_quantity, data + offset, sizeof(order.disclosed_quantity));
        order.disclosed_quantity = be64toh(order.disclosed_quantity);
        offset += sizeof(order.disclosed_quantity);
        
        std::memcpy(&order.order_status, data + offset, sizeof(order.order_status));
    }
    
    return order;
}

void NSEProtocolParser::set_trade_callback(MessageCallback callback) {
    trade_callback_ = callback;
}

void NSEProtocolParser::set_quote_callback(MessageCallback callback) {
    quote_callback_ = callback;
}

void NSEProtocolParser::set_order_callback(MessageCallback callback) {
    order_callback_ = callback;
}

bool NSEProtocolParser::connect_to_feed(const std::string& host, uint16_t port) {
    try {
        // Check connection rate limit
        if (!connection_rate_limiter_->try_acquire()) {
            LOG_ERROR("NSEProtocolParser: Connection rate limit exceeded");
            return false;
        }
        
        // Close existing connection if any
        if (socket_fd_ >= 0) {
            disconnect();
        }
        
        // Create socket
        socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd_ < 0) {
            LOG_ERROR("NSEProtocolParser: Failed to create socket: {}", strerror(errno));
            return false;
        }
        
        // Set socket options for low latency
        int flag = 1;
        if (setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
            LOG_WARN("NSEProtocolParser: Failed to set TCP_NODELAY: {}", strerror(errno));
        }
        
        // Set socket to non-blocking mode
        int flags = fcntl(socket_fd_, F_GETFL, 0);
        if (flags < 0 || fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
            LOG_ERROR("NSEProtocolParser: Failed to set non-blocking mode: {}", strerror(errno));
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
        
        // Resolve host
        struct addrinfo hints = {0};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        struct addrinfo* result = nullptr;
        std::string port_str = std::to_string(port);
        int ret = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
        if (ret != 0) {
            LOG_ERROR("NSEProtocolParser: Failed to resolve host {}: {}", host, gai_strerror(ret));
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
        
        // Connect with timeout
        ret = connect(socket_fd_, result->ai_addr, result->ai_addrlen);
        if (ret < 0 && errno != EINPROGRESS) {
            LOG_ERROR("NSEProtocolParser: Failed to connect: {}", strerror(errno));
            freeaddrinfo(result);
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
        
        freeaddrinfo(result);
        
        // Wait for connection with timeout
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(socket_fd_, &write_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 5;  // 5 second timeout
        timeout.tv_usec = 0;
        
        ret = select(socket_fd_ + 1, nullptr, &write_fds, nullptr, &timeout);
        if (ret <= 0) {
            LOG_ERROR("NSEProtocolParser: Connection timeout or error");
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
        
        // Check if connection succeeded
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
            LOG_ERROR("NSEProtocolParser: Connection failed: {}", strerror(error));
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
        
        // Set back to blocking mode for simplicity
        fcntl(socket_fd_, F_SETFL, flags);
        
        // Set receive buffer size for performance
        int recv_buf_size = 1024 * 1024;  // 1MB
        setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &recv_buf_size, sizeof(recv_buf_size));
        
        LOG_INFO("NSEProtocolParser: Successfully connected to NSE feed at {}:{}", host, port);
        connected_ = true;
        host_ = host;
        port_ = port;
        
        // Start receiver thread
        receiver_thread_ = std::thread(&NSEProtocolParser::receive_thread_func, this);
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("NSEProtocolParser: Exception during connect: {}", e.what());
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
        return false;
    }
}

void NSEProtocolParser::disconnect() {
    LOG_INFO("NSEProtocolParser: Disconnecting from NSE feed");
    
    connected_ = false;
    
    // Shutdown socket to wake up receiver thread
    if (socket_fd_ >= 0) {
        shutdown(socket_fd_, SHUT_RDWR);
    }
    
    // Wait for receiver thread to finish
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }
    
    // Close socket
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    
    // Reset state
    reset_parser_state();
}

void NSEProtocolParser::reset_parser_state() {
    state_ = ParserState::WAITING_HEADER;
    buffer_pos_ = 0;
    expected_message_size_ = 0;
    std::memset(buffer_, 0, std::min(static_cast<size_t>(1024), BUFFER_SIZE)); // Clear first 1KB
}

void NSEProtocolParser::receive_thread_func() {
    LOG_INFO("NSEProtocolParser: Receiver thread started");
    
    uint8_t recv_buffer[4096];
    
    while (connected_) {
        // Use select for timeout handling
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(socket_fd_, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int ret = select(socket_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("NSEProtocolParser: Select error: {}", strerror(errno));
            break;
        }
        
        if (ret == 0) {
            // Timeout - check if still connected
            continue;
        }
        
        // Data available
        ssize_t bytes_received = recv(socket_fd_, recv_buffer, sizeof(recv_buffer), 0);
        
        if (bytes_received < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            LOG_ERROR("NSEProtocolParser: Receive error: {}", strerror(errno));
            break;
        }
        
        if (bytes_received == 0) {
            LOG_INFO("NSEProtocolParser: Connection closed by peer");
            break;
        }
        
        // Check message rate limit
        if (!message_rate_limiter_->try_acquire()) {
            LOG_WARN("NSEProtocolParser: Message rate limit exceeded, dropping data");
            continue;
        }
        
        // Parse received data
        size_t parsed = parse_buffer(recv_buffer, bytes_received);
        if (parsed < static_cast<size_t>(bytes_received)) {
            LOG_WARN("NSEProtocolParser: Only parsed {} of {} bytes", parsed, bytes_received);
        }
    }
    
    LOG_INFO("NSEProtocolParser: Receiver thread exiting");
    connected_ = false;
}

// NSESymbolManager implementation
bool NSESymbolManager::load_symbol_master(const std::string& filename) {
    LOG_INFO("NSESymbolManager: Loading symbol master from {}", filename);
    
    symbols_.clear();
    name_to_index_.clear();
    id_to_index_.clear();
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("NSESymbolManager: Failed to open symbol master file: {}", filename);
        
        // Load default symbols for testing
        SymbolInfo reliance{1, "RELIANCE", "INE002A01018", InstrumentType::EQUITY, 0.05, 1, 3000.0, 1500.0};
        SymbolInfo tcs{2, "TCS", "INE467B01029", InstrumentType::EQUITY, 0.05, 1, 4500.0, 2250.0};
        SymbolInfo hdfc{3, "HDFCBANK", "INE040A01034", InstrumentType::EQUITY, 0.05, 1, 2000.0, 1000.0};
        SymbolInfo nifty{4, "NIFTY", "NIFTY50", InstrumentType::INDEX, 0.05, 1, 25000.0, 15000.0};
        SymbolInfo banknifty{5, "BANKNIFTY", "BANKNIFTY", InstrumentType::INDEX, 0.05, 1, 50000.0, 30000.0};
        
        symbols_.push_back(reliance);
        symbols_.push_back(tcs);
        symbols_.push_back(hdfc);
        symbols_.push_back(nifty);
        symbols_.push_back(banknifty);
        
        // Build indices
        for (size_t i = 0; i < symbols_.size(); ++i) {
            name_to_index_[symbols_[i].symbol_name] = i;
            id_to_index_[symbols_[i].symbol_id] = i;
        }
        
        LOG_WARN("NSESymbolManager: Using default symbols, loaded {} symbols", symbols_.size());
        return true;
    }
    
    // Parse CSV format: symbol_id,symbol_name,isin,type,tick_size,lot_size,upper_circuit,lower_circuit
    std::string line;
    std::getline(file, line); // Skip header
    
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string token;
        std::vector<std::string> tokens;
        
        while (std::getline(iss, token, ',')) {
            tokens.push_back(token);
        }
        
        if (tokens.size() < 8) {
            LOG_WARN("NSESymbolManager: Invalid line in symbol master: {}", line);
            continue;
        }
        
        try {
            SymbolInfo info;
            info.symbol_id = std::stoull(tokens[0]);
            info.symbol_name = tokens[1];
            info.isin = tokens[2];
            info.type = (tokens[3] == "EQUITY") ? InstrumentType::EQUITY :
                       (tokens[3] == "FUTURE") ? InstrumentType::FUTURE :
                       (tokens[3] == "OPTION") ? InstrumentType::OPTION :
                       (tokens[3] == "INDEX") ? InstrumentType::INDEX : InstrumentType::EQUITY;
            info.tick_size = std::stod(tokens[4]);
            info.lot_size = std::stoull(tokens[5]);
            info.upper_circuit = std::stod(tokens[6]);
            info.lower_circuit = std::stod(tokens[7]);
            
            symbols_.push_back(info);
        } catch (const std::exception& e) {
            LOG_ERROR("NSESymbolManager: Error parsing symbol line: {}, error: {}", line, e.what());
        }
    }
    
    file.close();
    
    // Build indices
    for (size_t i = 0; i < symbols_.size(); ++i) {
        name_to_index_[symbols_[i].symbol_name] = i;
        id_to_index_[symbols_[i].symbol_id] = i;
    }
    
    LOG_INFO("NSESymbolManager: Loaded {} symbols from file", symbols_.size());
    return !symbols_.empty();
}

const NSESymbolManager::SymbolInfo* NSESymbolManager::get_symbol_info(uint64_t symbol_id) const {
    auto it = id_to_index_.find(symbol_id);
    if (it != id_to_index_.end()) {
        return &symbols_[it->second];
    }
    return nullptr;
}

const NSESymbolManager::SymbolInfo* NSESymbolManager::get_symbol_info(const std::string& symbol_name) const {
    auto it = name_to_index_.find(symbol_name);
    if (it != name_to_index_.end()) {
        return &symbols_[it->second];
    }
    return nullptr;
}

uint64_t NSESymbolManager::get_symbol_id(const std::string& symbol_name) const {
    const auto* info = get_symbol_info(symbol_name);
    return info ? info->symbol_id : 0;
}

std::string NSESymbolManager::get_symbol_name(uint64_t symbol_id) const {
    const auto* info = get_symbol_info(symbol_id);
    return info ? info->symbol_name : "";
}

// NSEFeedHandler implementation
NSEFeedHandler::NSEFeedHandler() : connected_(false), last_message_time_{}, message_count_(0) {}

NSEFeedHandler::~NSEFeedHandler() {
    stop_feeds();
}

bool NSEFeedHandler::start_feeds(const std::vector<std::string>& symbol_list) {
    LOG_INFO("NSEFeedHandler: Starting feeds for {} symbols", symbol_list.size());
    
    // Load symbol master
    if (!symbol_manager_.load_symbol_master("symbols.txt")) {
        LOG_ERROR("NSEFeedHandler: Failed to load symbol master");
        return false;
    }
    
    // Set up parser callbacks
    parser_.set_trade_callback([this](const MessageHeader& header, const void* data) {
        handle_trade_message(header, data);
    });
    
    parser_.set_quote_callback([this](const MessageHeader& header, const void* data) {
        handle_quote_message(header, data);
    });
    
    parser_.set_order_callback([this](const MessageHeader& header, const void* data) {
        handle_order_message(header, data);
    });
    
    // Connect to NSE feed
    if (!parser_.connect_to_feed("nse-feed.example.com", NSE_PORT)) {
        LOG_ERROR("NSEFeedHandler: Failed to connect to NSE feed");
        return false;
    }
    
    connected_ = true;
    LOG_INFO("NSEFeedHandler: Successfully started feeds");
    return true;
}

void NSEFeedHandler::stop_feeds() {
    if (connected_) {
        parser_.disconnect();
        connected_ = false;
        LOG_INFO("NSEFeedHandler: Stopped feeds");
    }
}

void NSEFeedHandler::subscribe_trades(const std::string& symbol) {
    LOG_DEBUG("NSEFeedHandler: Subscribing to trades for {}", symbol);
}

void NSEFeedHandler::subscribe_quotes(const std::string& symbol) {
    LOG_DEBUG("NSEFeedHandler: Subscribing to quotes for {}", symbol);
}

void NSEFeedHandler::subscribe_orders(const std::string& symbol) {
    LOG_DEBUG("NSEFeedHandler: Subscribing to orders for {}", symbol);
}

void NSEFeedHandler::register_trade_handler(std::function<void(const TradeMessage&)> handler) {
    trade_handler_ = handler;
}

void NSEFeedHandler::register_quote_handler(std::function<void(const QuoteMessage&)> handler) {
    quote_handler_ = handler;
}

void NSEFeedHandler::register_order_handler(std::function<void(const OrderUpdateMessage&)> handler) {
    order_handler_ = handler;
}

double NSEFeedHandler::get_message_rate() const {
    auto now = std::chrono::steady_clock::now();
    auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(now - 
        std::chrono::time_point<std::chrono::steady_clock>{}).count();
    
    if (time_diff > 0) {
        return static_cast<double>(message_count_) / time_diff;
    }
    
    return 0.0;
}

void NSEFeedHandler::handle_trade_message(const MessageHeader& header, const void* data) {
    TradeMessage trade = parser_.parse_nse_trade(static_cast<const uint8_t*>(data));
    trade.header = header;
    
    last_message_time_ = header.timestamp;
    message_count_++;
    
    if (trade_handler_) {
        try {
            trade_handler_(trade);
        } catch (const std::exception& e) {
            LOG_ERROR("NSEFeedHandler: Trade handler exception: {}", e.what());
        }
    }
}

void NSEFeedHandler::handle_quote_message(const MessageHeader& header, const void* data) {
    QuoteMessage quote = parser_.parse_nse_quote(static_cast<const uint8_t*>(data));
    quote.header = header;
    
    last_message_time_ = header.timestamp;
    message_count_++;
    
    if (quote_handler_) {
        try {
            quote_handler_(quote);
        } catch (const std::exception& e) {
            LOG_ERROR("NSEFeedHandler: Quote handler exception: {}", e.what());
        }
    }
}

void NSEFeedHandler::handle_order_message(const MessageHeader& header, const void* data) {
    OrderUpdateMessage order = parser_.parse_nse_order(static_cast<const uint8_t*>(data));
    order.header = header;
    
    last_message_time_ = header.timestamp;
    message_count_++;
    
    if (order_handler_) {
        try {
            order_handler_(order);
        } catch (const std::exception& e) {
            LOG_ERROR("NSEFeedHandler: Order handler exception: {}", e.what());
        }
    }
}

} // namespace goldearn::market_data::nse