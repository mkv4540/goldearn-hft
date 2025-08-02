# Unit tests for GoldEarn HFT system

# Market data tests
add_executable(test_market_data
    test_order_book.cpp
    test_nse_protocol.cpp
    test_market_data_engine.cpp
)

target_link_libraries(test_market_data
    goldearn_core
    GTest::gtest
    GTest::gtest_main
)

# Trading engine tests
add_executable(test_trading
    test_trading_engine.cpp
    test_strategy_base.cpp
    test_order_manager.cpp
    test_position_manager.cpp
)

target_link_libraries(test_trading
    goldearn_core
    GTest::gtest
    GTest::gtest_main
)

# Risk management tests
add_executable(test_risk
    test_risk_engine.cpp
    test_pre_trade_checks.cpp
    test_var_calculator.cpp
)

target_link_libraries(test_risk
    goldearn_core
    GTest::gtest
    GTest::gtest_main
)

# Core utilities tests
add_executable(test_core
    test_latency_tracker.cpp
    test_memory_pool.cpp
    test_thread_pool.cpp
)

target_link_libraries(test_core
    goldearn_core
    GTest::gtest
    GTest::gtest_main
)

# Add all tests to CTest
add_test(NAME MarketDataTests COMMAND test_market_data)
add_test(NAME TradingTests COMMAND test_trading)
add_test(NAME RiskTests COMMAND test_risk)
add_test(NAME CoreTests COMMAND test_core)

# Test properties
set_tests_properties(MarketDataTests TradingTests RiskTests CoreTests
    PROPERTIES
    TIMEOUT 30
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)