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

# Enhanced risk management tests
add_executable(test_enhanced_risk_management
    test_enhanced_risk_management.cpp
)

target_link_libraries(test_enhanced_risk_management
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

# Configuration tests
add_executable(test_config
    test_json_config_parser.cpp
)

target_link_libraries(test_config
    goldearn_core
    GTest::gtest
    GTest::gtest_main
)

# Authentication tests
add_executable(test_auth
    test_exchange_auth.cpp
)

target_link_libraries(test_auth
    goldearn_core
    GTest::gtest
    GTest::gtest_main
)

# Monitoring tests
add_executable(test_monitoring
    test_health_check.cpp
    test_prometheus_metrics.cpp
)

target_link_libraries(test_monitoring
    goldearn_core
    GTest::gtest
    GTest::gtest_main
)

# Security tests
add_executable(test_security
    test_certificate_manager.cpp
)

target_link_libraries(test_security
    goldearn_core
    GTest::gtest
    GTest::gtest_main
    ${OPENSSL_LIBRARIES}
)

# Integration tests
add_executable(test_integration
    integration/test_integration_suite.cpp
)

target_link_libraries(test_integration
    goldearn_core
    GTest::gtest
    GTest::gtest_main
)

# Performance tests
add_executable(test_performance
    performance/test_performance_regression.cpp
)

target_link_libraries(test_performance
    goldearn_core
    GTest::gtest
    GTest::gtest_main
)

# Link benchmark library if available
if(TARGET benchmark::benchmark)
    target_link_libraries(test_performance benchmark::benchmark)
endif()

# Add all tests to CTest
add_test(NAME MarketDataTests COMMAND test_market_data)
add_test(NAME TradingTests COMMAND test_trading)
add_test(NAME RiskTests COMMAND test_risk)
add_test(NAME EnhancedRiskTests COMMAND test_enhanced_risk_management)
add_test(NAME CoreTests COMMAND test_core)
add_test(NAME ConfigTests COMMAND test_config)
add_test(NAME AuthTests COMMAND test_auth)
add_test(NAME MonitoringTests COMMAND test_monitoring)
add_test(NAME SecurityTests COMMAND test_security)
add_test(NAME IntegrationTests COMMAND test_integration)
add_test(NAME PerformanceTests COMMAND test_performance)

# Test properties
set_tests_properties(MarketDataTests TradingTests RiskTests EnhancedRiskTests CoreTests ConfigTests AuthTests MonitoringTests SecurityTests
    PROPERTIES
    TIMEOUT 30
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Extended timeout for integration and performance tests
set_tests_properties(IntegrationTests PerformanceTests
    PROPERTIES
    TIMEOUT 300
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)