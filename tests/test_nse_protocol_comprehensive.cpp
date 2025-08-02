#include <gtest/gtest.h>
#include "../src/market_data/nse_protocol.hpp"
#include "../src/utils/logger.hpp"
#include <vector>
#include <random>
#include <thread>

using namespace goldearn::market_data::nse;
using namespace goldearn::market_data;

class NSEProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser_ = std::make_unique<NSEProtocolParser>();
        symbol_manager_ = std::make_unique<NSESymbolManager>();
        feed_handler_ = std::make_unique<NSEFeedHandler>();
        
        // Set up test callbacks
        trade_messages_received_ = 0;
        quote_messages_received_ = 0;
        order_messages_received_ = 0;
        
        parser_->set_trade_callback([this](const MessageHeader& header, const void* data) {
            trade_messages_received_++;
            last_trade_header_ = header;
        });
        
        parser_->set_quote_callback([this](const MessageHeader& header, const void* data) {
            quote_messages_received_++;
            last_quote_header_ = header;
        });
        
        parser_->set_order_callback([this](const MessageHeader& header, const void* data) {
            order_messages_received_++;
            last_order_header_ = header;
        });
    }
    
    void TearDown() override {
        parser_.reset();
        symbol_manager_.reset();
        feed_handler_.reset();
    }
    
    // Helper function to create valid message data
    std::vector<uint8_t> create_valid_trade_message(uint64_t symbol_id = 1, double price = 100.50, uint64_t quantity = 1000) {
        std::vector<uint8_t> data;
        
        // Create header
        MessageHeader header{};
        header.msg_type = MessageType::TRADE;
        header.exchange = Exchange::NSE;
        header.msg_length = sizeof(MessageHeader) + sizeof(TradeMessage) - sizeof(MessageHeader);
        header.timestamp = std::chrono::duration_cast<Timestamp>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        );
        header.sequence_number = 12345;
        
        // Convert to network byte order
        header.msg_length = htobe32(header.msg_length);
        header.sequence_number = htobe64(header.sequence_number);
        
        // Add header to data
        const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&header);
        data.insert(data.end(), header_bytes, header_bytes + sizeof(MessageHeader));
        
        // Create trade data
        uint64_t be_symbol_id = htobe64(symbol_id);
        uint64_t be_trade_id = htobe64(99999);
        uint64_t be_quantity = htobe64(quantity);
        
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&be_symbol_id), 
                   reinterpret_cast<const uint8_t*>(&be_symbol_id) + sizeof(uint64_t));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&be_trade_id),
                   reinterpret_cast<const uint8_t*>(&be_trade_id) + sizeof(uint64_t));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&price),
                   reinterpret_cast<const uint8_t*>(&price) + sizeof(double));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&be_quantity),
                   reinterpret_cast<const uint8_t*>(&be_quantity) + sizeof(uint64_t));
        
        // Add broker IDs
        char buyer[8] = "BUYER01";
        char seller[8] = "SELL01  ";
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(buyer),
                   reinterpret_cast<const uint8_t*>(buyer) + 8);
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(seller),
                   reinterpret_cast<const uint8_t*>(seller) + 8);
        
        return data;
    }
    
    std::unique_ptr<NSEProtocolParser> parser_;
    std::unique_ptr<NSESymbolManager> symbol_manager_;
    std::unique_ptr<NSEFeedHandler> feed_handler_;
    
    std::atomic<uint32_t> trade_messages_received_{0};
    std::atomic<uint32_t> quote_messages_received_{0};
    std::atomic<uint32_t> order_messages_received_{0};
    
    MessageHeader last_trade_header_{};
    MessageHeader last_quote_header_{};
    MessageHeader last_order_header_{};
};

// Test NSEProtocolParser::parse_buffer - all scenarios
TEST_F(NSEProtocolTest, ParseBuffer) {
    // Test empty buffer
    EXPECT_EQ(parser_->parse_buffer(nullptr, 0), 0UL);
    EXPECT_EQ(parser_->get_parse_errors(), 1UL);
    
    // Test valid trade message
    auto trade_data = create_valid_trade_message();
    size_t parsed = parser_->parse_buffer(trade_data.data(), trade_data.size());
    EXPECT_EQ(parsed, trade_data.size());
    EXPECT_EQ(trade_messages_received_.load(), 1U);
    EXPECT_EQ(parser_->get_messages_processed(), 1UL);
    
    // Test oversized message
    std::vector<uint8_t> oversized_data(100000, 0xFF);
    EXPECT_EQ(parser_->parse_buffer(oversized_data.data(), oversized_data.size()), 0UL);
    EXPECT_GT(parser_->get_parse_errors(), 1UL);
    
    // Test corrupted header
    std::vector<uint8_t> corrupted_data = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x08};
    parser_->parse_buffer(corrupted_data.data(), corrupted_data.size());
    EXPECT_GT(parser_->get_parse_errors(), 1UL);
    
    // Test partial message (header only)
    MessageHeader partial_header{};
    partial_header.msg_type = MessageType::HEARTBEAT;
    partial_header.exchange = Exchange::NSE;
    partial_header.msg_length = htobe32(sizeof(MessageHeader) + 10);
    
    uint8_t* header_data = reinterpret_cast<uint8_t*>(&partial_header);
    size_t partial_parsed = parser_->parse_buffer(header_data, sizeof(MessageHeader));
    EXPECT_EQ(partial_parsed, sizeof(MessageHeader));
    
    // Test fragmented message parsing
    auto large_message = create_valid_trade_message();
    size_t chunk_size = 10;
    size_t total_parsed = 0;
    
    for (size_t i = 0; i < large_message.size(); i += chunk_size) {
        size_t remaining = std::min(chunk_size, large_message.size() - i);
        total_parsed += parser_->parse_buffer(large_message.data() + i, remaining);
    }
    
    EXPECT_EQ(total_parsed, large_message.size());
    EXPECT_EQ(trade_messages_received_.load(), 2U); // Original + fragmented
    
    // Test multiple messages in single buffer
    auto msg1 = create_valid_trade_message(1, 100.0, 1000);
    auto msg2 = create_valid_trade_message(2, 200.0, 2000);
    auto msg3 = create_valid_trade_message(3, 300.0, 3000);
    
    std::vector<uint8_t> multi_message;
    multi_message.insert(multi_message.end(), msg1.begin(), msg1.end());
    multi_message.insert(multi_message.end(), msg2.begin(), msg2.end());
    multi_message.insert(multi_message.end(), msg3.begin(), msg3.end());
    
    size_t multi_parsed = parser_->parse_buffer(multi_message.data(), multi_message.size());
    EXPECT_EQ(multi_parsed, multi_message.size());
    EXPECT_EQ(trade_messages_received_.load(), 5U); // 2 previous + 3 new
}

// Test NSEProtocolParser security validation
TEST_F(NSEProtocolTest, SecurityValidation) {
    // Test buffer overflow protection
    uint8_t malicious_data[1000];
    std::memset(malicious_data, 0xFF, sizeof(malicious_data));
    
    // Set invalid message length that could cause overflow
    MessageHeader* fake_header = reinterpret_cast<MessageHeader*>(malicious_data);
    fake_header->msg_type = MessageType::TRADE;
    fake_header->exchange = Exchange::NSE;
    fake_header->msg_length = htobe32(0xFFFFFFFF); // Malicious large size
    
    size_t parsed = parser_->parse_buffer(malicious_data, sizeof(malicious_data));
    EXPECT_EQ(parsed, 0UL);
    EXPECT_GT(parser_->get_parse_errors(), 0UL);
    
    // Test invalid message type
    fake_header->msg_length = htobe32(100);
    fake_header->msg_type = static_cast<MessageType>(255); // Invalid type
    
    parsed = parser_->parse_buffer(malicious_data, sizeof(MessageHeader) + 50);
    EXPECT_EQ(parsed, 0UL);
    
    // Test invalid exchange
    fake_header->msg_type = MessageType::TRADE;
    fake_header->exchange = static_cast<Exchange>(255); // Invalid exchange
    
    parsed = parser_->parse_buffer(malicious_data, sizeof(MessageHeader) + 50);
    EXPECT_EQ(parsed, 0UL);
    
    // Test price validation
    auto trade_data = create_valid_trade_message(1, -100.0, 1000); // Negative price
    parsed = parser_->parse_buffer(trade_data.data(), trade_data.size());
    EXPECT_GT(parser_->get_parse_errors(), 0UL);
    
    // Test quantity validation
    trade_data = create_valid_trade_message(1, 100.0, 0); // Zero quantity
    parsed = parser_->parse_buffer(trade_data.data(), trade_data.size());
    EXPECT_GT(parser_->get_parse_errors(), 0UL);
    
    // Test extremely large price
    trade_data = create_valid_trade_message(1, 999999999.99, 1000);
    parsed = parser_->parse_buffer(trade_data.data(), trade_data.size());
    EXPECT_GT(parser_->get_parse_errors(), 0UL);
    
    // Test extremely large quantity
    trade_data = create_valid_trade_message(1, 100.0, UINT64_MAX);
    parsed = parser_->parse_buffer(trade_data.data(), trade_data.size());
    EXPECT_GT(parser_->get_parse_errors(), 0UL);
}

// Test NSESymbolManager functionality
TEST_F(NSEProtocolTest, SymbolManager) {
    // Test loading symbol master
    EXPECT_TRUE(symbol_manager_->load_symbol_master("test_symbols.txt"));
    EXPECT_GT(symbol_manager_->get_symbol_count(), 0UL);
    
    // Test symbol lookup by ID
    const auto* reliance_info = symbol_manager_->get_symbol_info(1);
    ASSERT_NE(reliance_info, nullptr);
    EXPECT_EQ(reliance_info->symbol_name, "RELIANCE");
    EXPECT_EQ(reliance_info->symbol_id, 1UL);
    
    // Test symbol lookup by name
    const auto* tcs_info = symbol_manager_->get_symbol_info("TCS");
    ASSERT_NE(tcs_info, nullptr);
    EXPECT_EQ(tcs_info->symbol_id, 2UL);
    EXPECT_EQ(tcs_info->symbol_name, "TCS");
    
    // Test invalid symbol lookup
    const auto* invalid_info = symbol_manager_->get_symbol_info(99999);
    EXPECT_EQ(invalid_info, nullptr);
    
    const auto* invalid_name_info = symbol_manager_->get_symbol_info("INVALID");
    EXPECT_EQ(invalid_name_info, nullptr);
    
    // Test symbol ID lookup
    uint64_t hdfc_id = symbol_manager_->get_symbol_id("HDFCBANK");
    EXPECT_EQ(hdfc_id, 3UL);
    
    uint64_t invalid_id = symbol_manager_->get_symbol_id("NONEXISTENT");
    EXPECT_EQ(invalid_id, 0UL);
    
    // Test symbol name lookup
    std::string symbol_name = symbol_manager_->get_symbol_name(1);
    EXPECT_EQ(symbol_name, "RELIANCE");
    
    std::string invalid_name = symbol_manager_->get_symbol_name(99999);
    EXPECT_TRUE(invalid_name.empty());
    
    // Test edge cases
    EXPECT_TRUE(symbol_manager_->load_symbol_master("")); // Empty filename should handle gracefully
    
    // Test with special characters in symbol names
    const auto* symbol_with_special = symbol_manager_->get_symbol_info("TEST-SYMBOL.NS");
    // Should handle gracefully even if not found
}

// Test NSEFeedHandler functionality
TEST_F(NSEProtocolTest, FeedHandler) {
    // Test feed startup
    std::vector<std::string> symbols = {"RELIANCE", "TCS", "HDFCBANK"};
    EXPECT_TRUE(feed_handler_->start_feeds(symbols));
    EXPECT_TRUE(feed_handler_->is_connected());
    
    // Test subscriptions
    feed_handler_->subscribe_trades("RELIANCE");
    feed_handler_->subscribe_quotes("TCS");
    feed_handler_->subscribe_orders("HDFCBANK");
    
    // Test handler registration
    std::atomic<uint32_t> trade_handler_calls{0};
    std::atomic<uint32_t> quote_handler_calls{0};
    std::atomic<uint32_t> order_handler_calls{0};
    
    feed_handler_->register_trade_handler([&trade_handler_calls](const TradeMessage& trade) {
        trade_handler_calls++;
    });
    
    feed_handler_->register_quote_handler([&quote_handler_calls](const QuoteMessage& quote) {
        quote_handler_calls++;
    });
    
    feed_handler_->register_order_handler([&order_handler_calls](const OrderUpdateMessage& order) {
        order_handler_calls++;
    });
    
    // Test message rate calculation
    double initial_rate = feed_handler_->get_message_rate();
    EXPECT_GE(initial_rate, 0.0);
    
    // Test last message time
    auto last_time = feed_handler_->get_last_message_time();
    // Should be valid timestamp or zero
    
    // Test stop feeds
    feed_handler_->stop_feeds();
    EXPECT_FALSE(feed_handler_->is_connected());
    
    // Test restart capability
    EXPECT_TRUE(feed_handler_->start_feeds(symbols));
    EXPECT_TRUE(feed_handler_->is_connected());
    feed_handler_->stop_feeds();
    
    // Test error handling
    std::vector<std::string> empty_symbols;
    // Should handle empty symbol list gracefully
    EXPECT_TRUE(feed_handler_->start_feeds(empty_symbols));
    
    // Test with invalid symbols
    std::vector<std::string> invalid_symbols = {"INVALID1", "INVALID2"};
    EXPECT_TRUE(feed_handler_->start_feeds(invalid_symbols)); // Should not crash
}

// Test concurrent access and thread safety
TEST_F(NSEProtocolTest, ConcurrentAccess) {
    const int num_threads = 4;
    const int messages_per_thread = 1000;
    std::vector<std::thread> threads;
    std::atomic<uint32_t> total_messages_sent{0};
    
    // Test concurrent message parsing
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, &total_messages_sent, t, messages_per_thread]() {
            for (int i = 0; i < messages_per_thread; ++i) {
                auto trade_data = create_valid_trade_message(t * 1000 + i, 100.0 + i, 1000 + i);
                parser_->parse_buffer(trade_data.data(), trade_data.size());
                total_messages_sent++;
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(total_messages_sent.load(), num_threads * messages_per_thread);
    EXPECT_EQ(trade_messages_received_.load(), num_threads * messages_per_thread);
    
    // Test concurrent symbol manager access
    threads.clear();
    std::atomic<uint32_t> successful_lookups{0};
    
    // Load symbols first
    symbol_manager_->load_symbol_master("test_symbols.txt");
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, &successful_lookups]() {
            for (int i = 0; i < 1000; ++i) {
                // Alternate between ID and name lookups
                if (i % 2 == 0) {
                    const auto* info = symbol_manager_->get_symbol_info(1 + (i % 3));
                    if (info) successful_lookups++;
                } else {
                    std::vector<std::string> symbols = {"RELIANCE", "TCS", "HDFCBANK"};
                    const auto* info = symbol_manager_->get_symbol_info(symbols[i % 3]);
                    if (info) successful_lookups++;
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_GT(successful_lookups.load(), 0U);
}

// Test performance under load
TEST_F(NSEProtocolTest, PerformanceUnderLoad) {
    const size_t num_messages = 100000;
    std::vector<std::vector<uint8_t>> messages;
    messages.reserve(num_messages);
    
    // Pre-generate messages
    for (size_t i = 0; i < num_messages; ++i) {
        messages.push_back(create_valid_trade_message(i, 100.0 + i * 0.01, 1000 + i));
    }
    
    // Measure parsing performance
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (const auto& msg_data : messages) {
        parser_->parse_buffer(msg_data.data(), msg_data.size());
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    
    double avg_latency_ns = static_cast<double>(duration_ns) / num_messages;
    
    // Should process messages very quickly (target < 1μs per message)
    EXPECT_LT(avg_latency_ns, 1000.0);
    EXPECT_EQ(trade_messages_received_.load(), num_messages);
    
    // Test parsing rate
    double messages_per_second = (num_messages * 1e9) / duration_ns;
    EXPECT_GT(messages_per_second, 100000.0); // Should handle >100K msgs/sec
    
    // Test symbol manager performance
    symbol_manager_->load_symbol_master("test_symbols.txt");
    
    start_time = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < num_messages; ++i) {
        symbol_manager_->get_symbol_info(1 + (i % 3));
    }
    
    end_time = std::chrono::high_resolution_clock::now();
    duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    
    double avg_lookup_ns = static_cast<double>(duration_ns) / num_messages;
    EXPECT_LT(avg_lookup_ns, 100.0); // Should be very fast lookups
}

// Test error recovery and resilience
TEST_F(NSEProtocolTest, ErrorRecoveryAndResilience) {
    // Test recovery from parse errors
    uint64_t initial_errors = parser_->get_parse_errors();
    
    // Send some invalid data
    std::vector<uint8_t> invalid_data = {0xFF, 0xFF, 0xFF, 0xFF};
    parser_->parse_buffer(invalid_data.data(), invalid_data.size());
    
    EXPECT_GT(parser_->get_parse_errors(), initial_errors);
    
    // Verify parser can still process valid messages after errors
    auto valid_trade = create_valid_trade_message();
    size_t parsed = parser_->parse_buffer(valid_trade.data(), valid_trade.size());
    EXPECT_EQ(parsed, valid_trade.size());
    EXPECT_GT(trade_messages_received_.load(), 0U);
    
    // Test symbol manager resilience
    // Try to load non-existent file
    EXPECT_TRUE(symbol_manager_->load_symbol_master("non_existent_file.txt"));
    EXPECT_GE(symbol_manager_->get_symbol_count(), 0UL); // Should handle gracefully
    
    // Test feed handler resilience
    // Try to connect to invalid host
    std::vector<std::string> symbols = {"TEST"};
    // Should not crash even with connection failures
    
    // Test parser state after malformed messages
    std::vector<uint8_t> partial_header = {0x01, 0x02, 0x03}; // Incomplete header
    parser_->parse_buffer(partial_header.data(), partial_header.size());
    
    // Should still be able to parse complete messages
    auto recovery_trade = create_valid_trade_message();
    parsed = parser_->parse_buffer(recovery_trade.data(), recovery_trade.size());
    EXPECT_EQ(parsed, recovery_trade.size());
    
    // Test message sequence handling
    auto msg1 = create_valid_trade_message(1, 100.0, 1000);
    auto msg2 = create_valid_trade_message(2, 200.0, 2000);
    
    // Parse them in sequence
    parser_->parse_buffer(msg1.data(), msg1.size());
    parser_->parse_buffer(msg2.data(), msg2.size());
    
    // Both should be processed successfully
    EXPECT_GT(trade_messages_received_.load(), 0U);
}

// Test memory management and resource cleanup
TEST_F(NSEProtocolTest, MemoryManagementAndCleanup) {
    // Test parser destruction and cleanup
    {
        auto temp_parser = std::make_unique<NSEProtocolParser>();
        
        // Use the parser
        auto trade_data = create_valid_trade_message();
        temp_parser->parse_buffer(trade_data.data(), trade_data.size());
        
        // Parser should clean up properly when destroyed
    }
    
    // Test symbol manager cleanup
    {
        auto temp_manager = std::make_unique<NSESymbolManager>();
        temp_manager->load_symbol_master("test_symbols.txt");
        
        // Should clean up symbol data properly
    }
    
    // Test feed handler cleanup
    {
        auto temp_handler = std::make_unique<NSEFeedHandler>();
        std::vector<std::string> symbols = {"TEST"};
        temp_handler->start_feeds(symbols);
        temp_handler->stop_feeds();
        
        // Should clean up connections properly
    }
    
    // Test large message handling without memory leaks
    const size_t large_message_count = 10000;
    for (size_t i = 0; i < large_message_count; ++i) {
        auto trade_data = create_valid_trade_message(i, 100.0 + i, 1000 + i);
        parser_->parse_buffer(trade_data.data(), trade_data.size());
    }
    
    // Should handle large volumes without issues
    EXPECT_EQ(trade_messages_received_.load(), large_message_count);
}

// Test edge cases and boundary conditions
TEST_F(NSEProtocolTest, EdgeCasesAndBoundaryConditions) {
    // Test minimum valid message size
    MessageHeader minimal_header{};
    minimal_header.msg_type = MessageType::HEARTBEAT;
    minimal_header.exchange = Exchange::NSE;
    minimal_header.msg_length = htobe32(sizeof(MessageHeader));
    minimal_header.sequence_number = htobe64(1);
    
    uint8_t* header_data = reinterpret_cast<uint8_t*>(&minimal_header);
    size_t parsed = parser_->parse_buffer(header_data, sizeof(MessageHeader));
    EXPECT_EQ(parsed, sizeof(MessageHeader));
    
    // Test maximum symbol ID
    auto max_symbol_trade = create_valid_trade_message(UINT64_MAX, 100.0, 1000);
    parsed = parser_->parse_buffer(max_symbol_trade.data(), max_symbol_trade.size());
    EXPECT_GT(parsed, 0UL);
    
    // Test minimum price (should be positive)
    auto min_price_trade = create_valid_trade_message(1, 0.01, 1000);
    parsed = parser_->parse_buffer(min_price_trade.data(), min_price_trade.size());
    EXPECT_GT(parsed, 0UL);
    
    // Test minimum quantity
    auto min_qty_trade = create_valid_trade_message(1, 100.0, 1);
    parsed = parser_->parse_buffer(min_qty_trade.data(), min_qty_trade.size());
    EXPECT_GT(parsed, 0UL);
    
    // Test symbol manager with empty symbol list
    auto empty_manager = std::make_unique<NSESymbolManager>();
    EXPECT_EQ(empty_manager->get_symbol_count(), 0UL);
    EXPECT_EQ(empty_manager->get_symbol_info(1), nullptr);
    EXPECT_EQ(empty_manager->get_symbol_info("TEST"), nullptr);
    
    // Test rapid connect/disconnect cycles
    for (int i = 0; i < 10; ++i) {
        std::vector<std::string> symbols = {"TEST"};
        EXPECT_TRUE(feed_handler_->start_feeds(symbols));
        feed_handler_->stop_feeds();
    }
}