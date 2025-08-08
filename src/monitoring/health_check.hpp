#pragma once

#include <string>
#include <memory>
#include <chrono>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <vector>
#include <functional>
#include <mutex>

// Forward declarations
namespace goldearn::market_data::nse {
    class NSEProtocolParser;
}

namespace goldearn::monitoring {

// Health status levels
enum class HealthStatus {
    HEALTHY,
    WARNING,
    CRITICAL,
    UNKNOWN
};

// Individual component health check
struct ComponentHealth {
    std::string component_name;
    HealthStatus status;
    std::string message;
    std::chrono::system_clock::time_point last_check;
    double response_time_ms;
    std::unordered_map<std::string, std::string> details;
};

// System-wide health summary
struct SystemHealth {
    HealthStatus overall_status;
    std::string summary_message;
    std::chrono::system_clock::time_point timestamp;
    std::vector<ComponentHealth> components;
    std::unordered_map<std::string, double> metrics;
};

// Health check interface
class HealthChecker {
public:
    virtual ~HealthChecker() = default;
    virtual ComponentHealth check_health() = 0;
    virtual std::string get_component_name() const = 0;
};

// HTTP health check server
class HealthCheckServer {
public:
    HealthCheckServer(uint16_t port = 8080);
    ~HealthCheckServer();
    
    // Server control
    bool start();
    void stop();
    bool is_running() const { return running_; }
    
    // Register health checkers
    void register_checker(std::shared_ptr<HealthChecker> checker);
    void unregister_checker(const std::string& component_name);
    
    // Manual health check
    SystemHealth get_system_health();
    ComponentHealth get_component_health(const std::string& component_name);
    
    // Configuration
    void set_check_interval(std::chrono::seconds interval) { check_interval_ = interval; }
    void set_timeout(std::chrono::milliseconds timeout) { timeout_ = timeout; }
    
private:
    void server_thread_func();
    void health_check_thread_func();
    std::string handle_request(const std::string& path, const std::string& method);
    std::string format_health_response(const SystemHealth& health, const std::string& format = "json");
    std::string format_metrics_response();
    HealthStatus determine_overall_status(const std::vector<ComponentHealth>& components);
    
protected:
    
private:
    uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread server_thread_;
    std::thread health_check_thread_;
    
    std::vector<std::shared_ptr<HealthChecker>> checkers_;
    mutable std::mutex checkers_mutex_;
    
    SystemHealth last_health_check_;
    mutable std::mutex health_mutex_;
    
    std::chrono::seconds check_interval_{30};
    std::chrono::milliseconds timeout_{5000};
    
    int server_socket_ = -1;
};

// Built-in health checkers
class DatabaseHealthChecker : public HealthChecker {
public:
    DatabaseHealthChecker(const std::string& connection_string);
    ComponentHealth check_health() override;
    std::string get_component_name() const override { return "Database"; }
    
private:
    std::string connection_string_;
};

class MarketDataHealthChecker : public HealthChecker {
public:
    MarketDataHealthChecker(goldearn::market_data::nse::NSEProtocolParser* parser);
    ComponentHealth check_health() override;
    std::string get_component_name() const override { return "MarketData"; }
    
private:
    goldearn::market_data::nse::NSEProtocolParser* parser_;
};

class TradingEngineHealthChecker : public HealthChecker {
public:
    TradingEngineHealthChecker();
    ComponentHealth check_health() override;
    std::string get_component_name() const override { return "TradingEngine"; }
    
private:
    std::chrono::system_clock::time_point last_order_time_;
    uint64_t total_orders_processed_;
    uint64_t failed_orders_;
};

class RiskEngineHealthChecker : public HealthChecker {
public:
    RiskEngineHealthChecker();
    ComponentHealth check_health() override;
    std::string get_component_name() const override { return "RiskEngine"; }
    
private:
    std::atomic<uint64_t> risk_checks_performed_{0};
    std::atomic<uint64_t> risk_checks_failed_{0};
    std::atomic<bool> trading_enabled_{true};
};

class NetworkHealthChecker : public HealthChecker {
public:
    NetworkHealthChecker(const std::vector<std::string>& hosts_to_check);
    ComponentHealth check_health() override;
    std::string get_component_name() const override { return "Network"; }
    
private:
    std::vector<std::string> hosts_to_check_;
    bool ping_host(const std::string& host, std::chrono::milliseconds timeout);
};

class SystemResourceHealthChecker : public HealthChecker {
public:
    SystemResourceHealthChecker();
    ComponentHealth check_health() override;
    std::string get_component_name() const override { return "SystemResources"; }
    
private:
    double get_cpu_usage();
    double get_memory_usage();
    double get_disk_usage();
    uint64_t get_network_bytes_sent();
    uint64_t get_network_bytes_received();
};

// Utility functions
namespace health_utils {
    std::string status_to_string(HealthStatus status);
    HealthStatus string_to_status(const std::string& status_str);
    std::string format_duration(std::chrono::milliseconds duration);
    std::string get_iso8601_timestamp(std::chrono::system_clock::time_point tp);
    std::string health_to_json(const SystemHealth& health);
    std::string health_to_prometheus(const SystemHealth& health);
}

} // namespace goldearn::monitoring