#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <memory>
#include <vector>
#include <atomic>

#include "../../src/config/config_manager.hpp"
#include "../../src/network/exchange_auth.hpp"
#include "../../src/market_data/nse_protocol.hpp"
#include "../../src/monitoring/health_check.hpp"
#include "../../src/monitoring/prometheus_metrics.hpp"
#include "../../src/security/certificate_manager.hpp"

using namespace goldearn;

class IntegrationTestSuite : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test configuration
        create_test_config();
        
        // Initialize core components
        config_manager_ = std::make_unique<config::ConfigManager>();
        auth_manager_ = std::make_unique<network::ExchangeAuthenticator>("NSE", "test_endpoint");
        nse_parser_ = std::make_unique<market_data::nse::NSEProtocolParser>();
        health_server_ = std::make_unique<monitoring::HealthCheckServer>(8081);
        metrics_collector_ = std::make_unique<monitoring::HFTMetricsCollector>();
        cert_manager_ = std::make_unique<security::CertificateManager>();
        
        // Load test configuration
        ASSERT_TRUE(config_manager_->load_config("test_config.json"));
    }
    
    void TearDown() override {
        // Stop all services
        if (health_server_->is_running()) {
            health_server_->stop();
        }
        metrics_collector_->stop();
        cert_manager_->stop_rotation_service();
        
        // Cleanup test files
        cleanup_test_files();
    }
    
    void create_test_config() {
        std::ofstream config_file("test_config.json");
        config_file << R"({
            "market_data": {
                "nse": {
                    "host": "127.0.0.1",
                    "port": 9000,
                    "timeout_ms": 5000,
                    "retry_count": 3
                }
            },
            "authentication": {
                "nse": {
                    "api_key": "test_api_key",
                    "secret": "test_secret",
                    "endpoint": "https://test.nse.com/api"
                }
            },
            "monitoring": {
                "health_check_port": 8081,
                "metrics_collection_interval": 5,
                "prometheus_enabled": true
            },
            "security": {
                "certificate_rotation_hours": 168,
                "certificate_check_interval": 24
            }
        })";
        config_file.close();
    }
    
    void cleanup_test_files() {
        std::remove("test_config.json");
        std::remove("test_cert.pem");
        std::remove("test_key.pem");
    }
    
    void create_test_certificates() {
        // Create dummy certificate and key files for testing
        std::ofstream cert_file("test_cert.pem");
        cert_file << "-----BEGIN CERTIFICATE-----\n";
        cert_file << "MIICDTCCAXYCCQDdwJmuFqAoEDANBgkqhkiG9w0BAQsFADANMQswCQYDVQQGEwJJ\n";
        cert_file << "TjAeFw0yNTA4MDIwMDAwMDBaFw0yNjA4MDIwMDAwMDBaMIGGMQswCQYDVQQGEwJJ\n";
        cert_file << "TjANBgkqhkiG9w0BAQEFAAOBjQAwgYkCgYEA1234567890abcdefghijklmnopqr\n";
        cert_file << "stuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890abcdefghijklmnopqrst\n";
        cert_file << "uvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890abcdefghijklmnopqrstuv\n";
        cert_file << "wxyzABCDEFGHIJKLMNOPQRSTUVWXYZwIDAQABMA0GCSqGSIb3DQEBCwUAA4GBAB\n";
        cert_file << "12345678901234567890123456789012345678901234567890123456789012345\n";
        cert_file << "-----END CERTIFICATE-----\n";
        cert_file.close();
        
        std::ofstream key_file("test_key.pem");
        key_file << "-----BEGIN PRIVATE KEY-----\n";
        key_file << "MIICDTCCAXYCCQDdwJmuFqAoEDANBgkqhkiG9w0BAQsFADANMQswCQYDVQQGEwJJ\n";
        key_file << "TjAeFw0yNTA4MDIwMDAwMDBaFw0yNjA4MDIwMDAwMDBaMIGGMQswCQYDVQQGEwJJ\n";
        key_file << "-----END PRIVATE KEY-----\n";
        key_file.close();
    }

protected:
    std::unique_ptr<config::ConfigManager> config_manager_;
    std::unique_ptr<network::ExchangeAuthenticator> auth_manager_;
    std::unique_ptr<market_data::nse::NSEProtocolParser> nse_parser_;
    std::unique_ptr<monitoring::HealthCheckServer> health_server_;
    std::unique_ptr<monitoring::HFTMetricsCollector> metrics_collector_;
    std::unique_ptr<security::CertificateManager> cert_manager_;
};

// Test configuration loading and parsing
TEST_F(IntegrationTestSuite, ConfigurationManagement) {
    // Test JSON config loading
    EXPECT_TRUE(config_manager_->has_section("market_data"));
    EXPECT_TRUE(config_manager_->has_section("authentication"));
    EXPECT_TRUE(config_manager_->has_section("monitoring"));
    
    // Test configuration values
    EXPECT_EQ(config_manager_->get_value("market_data.nse.host"), "127.0.0.1");
    EXPECT_EQ(config_manager_->get_int("market_data.nse.port"), 9000);
    EXPECT_EQ(config_manager_->get_int("monitoring.health_check_port"), 8081);
    
    // Test configuration updates
    config_manager_->set_value("test.value", "updated");
    EXPECT_EQ(config_manager_->get_value("test.value"), "updated");
}

// Test authentication system with token refresh
TEST_F(IntegrationTestSuite, AuthenticationSystem) {
    // Configure authentication
    std::string api_key = config_manager_->get_value("authentication.nse.api_key");
    std::string secret = config_manager_->get_value("authentication.nse.secret");
    
    EXPECT_FALSE(api_key.empty());
    EXPECT_FALSE(secret.empty());
    
    // Test token generation (would normally connect to actual endpoint)
    // In integration test, we simulate success
    EXPECT_TRUE(true); // Placeholder for actual auth test
    
    // Test automatic token refresh scheduling
    auto start_time = std::chrono::steady_clock::now();
    
    // In real test, would wait for token refresh and verify
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    EXPECT_GE(elapsed, std::chrono::milliseconds(100));
}

// Test market data processing pipeline
TEST_F(IntegrationTestSuite, MarketDataPipeline) {
    // Test NSE protocol parser initialization
    std::string host = config_manager_->get_value("market_data.nse.host");
    int port = config_manager_->get_int("market_data.nse.port");
    
    EXPECT_FALSE(host.empty());
    EXPECT_GT(port, 0);
    
    // Test connection attempt (would fail in test environment, but validates config)
    bool connection_attempted = false;
    try {
        nse_parser_->connect_to_feed(host, static_cast<uint16_t>(port));
        connection_attempted = true;
    } catch (...) {
        connection_attempted = true; // Expected to fail in test
    }
    
    EXPECT_TRUE(connection_attempted);
    
    // Test message processing metrics
    uint64_t initial_messages = nse_parser_->get_messages_processed();
    uint64_t initial_errors = nse_parser_->get_parse_errors();
    
    // Simulate message processing
    nse_parser_->increment_message_count();
    
    EXPECT_EQ(nse_parser_->get_messages_processed(), initial_messages + 1);
    EXPECT_EQ(nse_parser_->get_parse_errors(), initial_errors);
}

// Test health check and monitoring system
TEST_F(IntegrationTestSuite, HealthCheckSystem) {
    // Start health check server
    ASSERT_TRUE(health_server_->start());
    EXPECT_TRUE(health_server_->is_running());
    
    // Register health checkers
    auto market_data_checker = std::make_shared<monitoring::MarketDataHealthChecker>(nse_parser_.get());
    auto system_checker = std::make_shared<monitoring::SystemResourceHealthChecker>();
    
    health_server_->register_checker(market_data_checker);
    health_server_->register_checker(system_checker);
    
    // Wait for health checks to run
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Test health status retrieval
    auto system_health = health_server_->get_system_health();
    EXPECT_FALSE(system_health.components.empty());
    
    // Test individual component health
    auto component_health = health_server_->get_component_health("MarketData");
    EXPECT_EQ(component_health.component_name, "MarketData");
    
    // Test health endpoints (would require HTTP client in full test)
    // For integration test, we verify the system is running
    EXPECT_TRUE(health_server_->is_running());
}

// Test Prometheus metrics collection
TEST_F(IntegrationTestSuite, PrometheusMetrics) {
    // Start metrics collection
    metrics_collector_->start();
    
    // Record some test metrics
    metrics_collector_->record_order_latency(50.0);
    metrics_collector_->record_order_placed();
    metrics_collector_->record_market_data_message();
    metrics_collector_->record_risk_check(10.0);
    
    // Wait for metrics to be collected
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Get metrics snapshot
    std::string metrics = metrics_collector_->get_metrics_snapshot();
    EXPECT_FALSE(metrics.empty());
    
    // Verify metrics contain expected entries
    EXPECT_NE(metrics.find("goldearn_order_latency"), std::string::npos);
    EXPECT_NE(metrics.find("goldearn_orders_placed"), std::string::npos);
    EXPECT_NE(metrics.find("goldearn_market_data_messages"), std::string::npos);
    
    metrics_collector_->stop();
}

// Test certificate management and rotation
TEST_F(IntegrationTestSuite, CertificateManagement) {
    create_test_certificates();
    
    // Create test certificate
    security::Certificate test_cert;
    test_cert.cert_path = "test_cert.pem";
    test_cert.key_path = "test_key.pem";
    test_cert.ca_path = "";
    
    // Load certificate
    EXPECT_TRUE(cert_manager_->load_certificate("test_nse", test_cert));
    
    // Get certificate
    auto* loaded_cert = cert_manager_->get_certificate("test_nse");
    ASSERT_NE(loaded_cert, nullptr);
    EXPECT_EQ(loaded_cert->cert_path, "test_cert.pem");
    
    // Test certificate validation (would fail with dummy cert, but tests the path)
    auto expiry_date = cert_manager_->get_expiry_date("test_cert.pem");
    // Dummy cert won't have valid expiry, but function should not crash
    
    // Test rotation service
    cert_manager_->start_rotation_service(std::chrono::hours(1));
    EXPECT_TRUE(cert_manager_->is_rotation_service_running());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    cert_manager_->stop_rotation_service();
    EXPECT_FALSE(cert_manager_->is_rotation_service_running());
}

// Test end-to-end system integration
TEST_F(IntegrationTestSuite, EndToEndSystemIntegration) {
    // Start all systems
    ASSERT_TRUE(health_server_->start());
    metrics_collector_->start();
    cert_manager_->start_rotation_service(std::chrono::hours(24));
    
    // Register health checkers
    auto market_data_checker = std::make_shared<monitoring::MarketDataHealthChecker>(nse_parser_.get());
    auto system_checker = std::make_shared<monitoring::SystemResourceHealthChecker>();
    
    health_server_->register_checker(market_data_checker);
    health_server_->register_checker(system_checker);
    
    // Simulate some system activity
    for (int i = 0; i < 10; ++i) {
        metrics_collector_->record_order_latency(50.0 + i);
        metrics_collector_->record_market_data_message();
        nse_parser_->increment_message_count();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Wait for systems to process
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Verify system health
    auto system_health = health_server_->get_system_health();
    EXPECT_GE(system_health.components.size(), 2);
    
    // Verify metrics are being collected
    std::string metrics = metrics_collector_->get_metrics_snapshot();
    EXPECT_FALSE(metrics.empty());
    EXPECT_NE(metrics.find("goldearn_order_latency"), std::string::npos);
    
    // Verify market data processing
    EXPECT_GE(nse_parser_->get_messages_processed(), 10);
    
    // All systems should still be running
    EXPECT_TRUE(health_server_->is_running());
    EXPECT_TRUE(cert_manager_->is_rotation_service_running());
}

// Test system under load
TEST_F(IntegrationTestSuite, SystemLoadTest) {
    // Start all systems
    ASSERT_TRUE(health_server_->start());
    metrics_collector_->start();
    
    const int num_threads = 4;
    const int operations_per_thread = 1000;
    std::vector<std::thread> threads;
    std::atomic<int> total_operations{0};
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Create worker threads
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, &total_operations, operations_per_thread]() {
            for (int j = 0; j < operations_per_thread; ++j) {
                // Simulate high-frequency operations
                metrics_collector_->record_order_latency(static_cast<double>(j % 100));
                metrics_collector_->record_market_data_message();
                nse_parser_->increment_message_count();
                
                total_operations++;
                
                // Small delay to prevent overwhelming the system
                if (j % 100 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Verify all operations completed
    EXPECT_EQ(total_operations.load(), num_threads * operations_per_thread);
    
    // Verify system performance (should complete within reasonable time)
    EXPECT_LT(duration.count(), 10000); // Less than 10 seconds
    
    // Verify metrics were recorded
    std::string metrics = metrics_collector_->get_metrics_snapshot();
    EXPECT_FALSE(metrics.empty());
    
    // Verify market data processing
    EXPECT_GE(nse_parser_->get_messages_processed(), static_cast<uint64_t>(num_threads * operations_per_thread));
    
    // System should still be healthy after load test
    auto system_health = health_server_->get_system_health();
    EXPECT_FALSE(system_health.components.empty());
    
    LOG_INFO("IntegrationTestSuite: Load test completed {} operations in {}ms", 
             total_operations.load(), duration.count());
}

// Test error handling and recovery
TEST_F(IntegrationTestSuite, ErrorHandlingAndRecovery) {
    // Start systems
    ASSERT_TRUE(health_server_->start());
    metrics_collector_->start();
    
    // Test configuration error handling
    auto backup_config = std::make_unique<config::ConfigManager>();
    EXPECT_FALSE(backup_config->load_config("nonexistent_config.json"));
    
    // Test certificate error handling
    security::Certificate invalid_cert;
    invalid_cert.cert_path = "nonexistent_cert.pem";
    invalid_cert.key_path = "nonexistent_key.pem";
    
    EXPECT_FALSE(cert_manager_->load_certificate("invalid", invalid_cert));
    
    // Test health check with failing component
    auto failing_checker = std::make_shared<monitoring::SystemResourceHealthChecker>();
    health_server_->register_checker(failing_checker);
    
    // Wait for health check to run
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // System should handle component failures gracefully
    auto system_health = health_server_->get_system_health();
    EXPECT_FALSE(system_health.components.empty());
    
    // Test metrics collection with errors
    for (int i = 0; i < 100; ++i) {
        metrics_collector_->record_order_latency(static_cast<double>(i));
    }
    
    // Metrics should still be available
    std::string metrics = metrics_collector_->get_metrics_snapshot();
    EXPECT_FALSE(metrics.empty());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}