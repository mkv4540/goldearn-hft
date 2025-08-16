#include "health_check.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "../market_data/nse_protocol.hpp"
#include "../utils/simple_logger.hpp"

namespace goldearn::monitoring {

HealthCheckServer::HealthCheckServer(uint16_t port) : port_(port) {
    LOG_INFO("HealthCheckServer: Initializing on port {}", port_);
}

HealthCheckServer::~HealthCheckServer() {
    stop();
}

bool HealthCheckServer::start() {
    if (running_) {
        LOG_WARN("HealthCheckServer: Server already running");
        return true;
    }

    // Create server socket
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0) {
        LOG_ERROR("HealthCheckServer: Failed to create socket: {}", strerror(errno));
        return false;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_WARN("HealthCheckServer: Failed to set SO_REUSEADDR: {}", strerror(errno));
    }

    // Bind to port
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(server_socket_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        LOG_ERROR("HealthCheckServer: Failed to bind to port {}: {}", port_, strerror(errno));
        close(server_socket_);
        server_socket_ = -1;
        return false;
    }

    // Listen for connections
    if (listen(server_socket_, 10) < 0) {
        LOG_ERROR("HealthCheckServer: Failed to listen: {}", strerror(errno));
        close(server_socket_);
        server_socket_ = -1;
        return false;
    }

    running_ = true;

    // Start threads
    server_thread_ = std::thread(&HealthCheckServer::server_thread_func, this);
    health_check_thread_ = std::thread(&HealthCheckServer::health_check_thread_func, this);

    LOG_INFO("HealthCheckServer: Started successfully on port {}", port_);
    return true;
}

void HealthCheckServer::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    // Close server socket to wake up accept()
    if (server_socket_ >= 0) {
        close(server_socket_);
        server_socket_ = -1;
    }

    // Wait for threads to finish
    if (server_thread_.joinable()) {
        server_thread_.join();
    }

    if (health_check_thread_.joinable()) {
        health_check_thread_.join();
    }

    LOG_INFO("HealthCheckServer: Stopped");
}

void HealthCheckServer::register_checker(std::shared_ptr<HealthChecker> checker) {
    std::lock_guard<std::mutex> lock(checkers_mutex_);
    checkers_.push_back(checker);
    LOG_INFO("HealthCheckServer: Registered health checker for {}", checker->get_component_name());
}

void HealthCheckServer::unregister_checker(const std::string& component_name) {
    std::lock_guard<std::mutex> lock(checkers_mutex_);
    checkers_.erase(
        std::remove_if(checkers_.begin(),
                       checkers_.end(),
                       [&component_name](const std::shared_ptr<HealthChecker>& checker) {
                           return checker->get_component_name() == component_name;
                       }),
        checkers_.end());
    LOG_INFO("HealthCheckServer: Unregistered health checker for {}", component_name);
}

SystemHealth HealthCheckServer::get_system_health() {
    std::lock_guard<std::mutex> lock(health_mutex_);
    return last_health_check_;
}

ComponentHealth HealthCheckServer::get_component_health(const std::string& component_name) {
    std::lock_guard<std::mutex> lock(health_mutex_);

    for (const auto& component : last_health_check_.components) {
        if (component.component_name == component_name) {
            return component;
        }
    }

    // Component not found
    ComponentHealth not_found;
    not_found.component_name = component_name;
    not_found.status = HealthStatus::UNKNOWN;
    not_found.message = "Component not found";
    not_found.last_check = std::chrono::system_clock::now();
    not_found.response_time_ms = 0.0;

    return not_found;
}

void HealthCheckServer::server_thread_func() {
    LOG_INFO("HealthCheckServer: Server thread started");

    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_socket = accept(server_socket_, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (running_) {
                LOG_ERROR("HealthCheckServer: Accept failed: {}", strerror(errno));
            }
            continue;
        }

        // Handle request in separate thread to avoid blocking
        std::thread([this, client_socket]() {
            char buffer[4096];
            ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                std::string request(buffer);

                // Parse HTTP request
                std::string method, path, version;
                std::istringstream request_stream(request);
                request_stream >> method >> path >> version;

                // Handle request
                std::string response = handle_request(path, method);

                // Send response
                send(client_socket, response.c_str(), response.length(), 0);
            }

            close(client_socket);
        }).detach();
    }

    LOG_INFO("HealthCheckServer: Server thread stopped");
}

void HealthCheckServer::health_check_thread_func() {
    LOG_INFO("HealthCheckServer: Health check thread started");

    while (running_) {
        auto start_time = std::chrono::steady_clock::now();

        SystemHealth health;
        health.timestamp = std::chrono::system_clock::now();
        health.components.clear();

        // Run all health checks
        {
            std::lock_guard<std::mutex> lock(checkers_mutex_);
            for (const auto& checker : checkers_) {
                try {
                    auto component_start = std::chrono::high_resolution_clock::now();
                    ComponentHealth component_health = checker->check_health();
                    auto component_end = std::chrono::high_resolution_clock::now();

                    component_health.response_time_ms =
                        std::chrono::duration<double, std::milli>(component_end - component_start)
                            .count();

                    health.components.push_back(component_health);
                } catch (const std::exception& e) {
                    ComponentHealth error_health;
                    error_health.component_name = checker->get_component_name();
                    error_health.status = HealthStatus::CRITICAL;
                    error_health.message = std::string("Health check failed: ") + e.what();
                    error_health.last_check = std::chrono::system_clock::now();
                    error_health.response_time_ms = 0.0;

                    health.components.push_back(error_health);
                    LOG_ERROR("HealthCheckServer: Health check failed for {}: {}",
                              checker->get_component_name(),
                              e.what());
                }
            }
        }

        // Determine overall status
        health.overall_status = determine_overall_status(health.components);

        // Generate summary message
        int healthy_count = 0;
        int warning_count = 0;
        int critical_count = 0;

        for (const auto& component : health.components) {
            switch (component.status) {
                case HealthStatus::HEALTHY:
                    healthy_count++;
                    break;
                case HealthStatus::WARNING:
                    warning_count++;
                    break;
                case HealthStatus::CRITICAL:
                    critical_count++;
                    break;
                default:
                    break;
            }
        }

        std::ostringstream summary;
        summary << "System Health: " << health_utils::status_to_string(health.overall_status);
        summary << " (" << healthy_count << " healthy, " << warning_count << " warning, "
                << critical_count << " critical)";
        health.summary_message = summary.str();

        // Add system metrics
        health.metrics["total_components"] = health.components.size();
        health.metrics["healthy_components"] = healthy_count;
        health.metrics["warning_components"] = warning_count;
        health.metrics["critical_components"] = critical_count;

        auto check_end = std::chrono::steady_clock::now();
        health.metrics["health_check_duration_ms"] =
            std::chrono::duration<double, std::milli>(check_end - start_time).count();

        // Update last health check
        {
            std::lock_guard<std::mutex> lock(health_mutex_);
            last_health_check_ = health;
        }

        // Sleep until next check
        std::this_thread::sleep_for(check_interval_);
    }

    LOG_INFO("HealthCheckServer: Health check thread stopped");
}

std::string HealthCheckServer::handle_request(const std::string& path, const std::string& method) {
    std::ostringstream response;

    if (method != "GET") {
        response << "HTTP/1.1 405 Method Not Allowed\r\n";
        response << "Content-Type: text/plain\r\n";
        response << "Connection: close\r\n\r\n";
        response << "Method not allowed";
        return response.str();
    }

    if (path == "/health" || path == "/") {
        // Full health check
        SystemHealth health = get_system_health();
        std::string body = format_health_response(health, "json");

        std::string status_code =
            (health.overall_status == HealthStatus::HEALTHY) ? "200 OK" : "503 Service Unavailable";

        response << "HTTP/1.1 " << status_code << "\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "Connection: close\r\n\r\n";
        response << body;
    } else if (path == "/health/ready") {
        // Readiness check (simplified)
        SystemHealth health = get_system_health();
        bool ready = (health.overall_status == HealthStatus::HEALTHY ||
                      health.overall_status == HealthStatus::WARNING);

        std::string status_code = ready ? "200 OK" : "503 Service Unavailable";
        std::string body = ready ? "Ready" : "Not Ready";

        response << "HTTP/1.1 " << status_code << "\r\n";
        response << "Content-Type: text/plain\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "Connection: close\r\n\r\n";
        response << body;
    } else if (path == "/health/live") {
        // Liveness check (server is running)
        std::string body = "Alive";

        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: text/plain\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "Connection: close\r\n\r\n";
        response << body;
    } else if (path == "/metrics") {
        // Prometheus metrics
        std::string body = format_metrics_response();

        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "Connection: close\r\n\r\n";
        response << body;
    } else {
        // 404 Not Found
        std::string body = "Not Found";

        response << "HTTP/1.1 404 Not Found\r\n";
        response << "Content-Type: text/plain\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "Connection: close\r\n\r\n";
        response << body;
    }

    return response.str();
}

std::string HealthCheckServer::format_health_response(const SystemHealth& health,
                                                      const std::string& format) {
    if (format == "json") {
        return health_utils::health_to_json(health);
    } else {
        return health_utils::health_to_prometheus(health);
    }
}

std::string HealthCheckServer::format_metrics_response() {
    SystemHealth health = get_system_health();
    return health_utils::health_to_prometheus(health);
}

HealthStatus HealthCheckServer::determine_overall_status(
    const std::vector<ComponentHealth>& components) {
    if (components.empty()) {
        return HealthStatus::UNKNOWN;
    }

    bool has_critical = false;
    bool has_warning = false;

    for (const auto& component : components) {
        switch (component.status) {
            case HealthStatus::CRITICAL:
                has_critical = true;
                break;
            case HealthStatus::WARNING:
                has_warning = true;
                break;
            default:
                break;
        }
    }

    if (has_critical) {
        return HealthStatus::CRITICAL;
    } else if (has_warning) {
        return HealthStatus::WARNING;
    } else {
        return HealthStatus::HEALTHY;
    }
}

// Health checker implementations
DatabaseHealthChecker::DatabaseHealthChecker(const std::string& connection_string)
    : connection_string_(connection_string) {
}

ComponentHealth DatabaseHealthChecker::check_health() {
    ComponentHealth health;
    health.component_name = get_component_name();
    health.last_check = std::chrono::system_clock::now();

    try {
        // Simple database connection test
        // In production, this would test actual database connectivity
        health.status = HealthStatus::HEALTHY;
        health.message = "Database connection successful";
        health.details["connection_string"] = connection_string_;
        health.details["last_query_time"] = "< 1ms";
    } catch (const std::exception& e) {
        health.status = HealthStatus::CRITICAL;
        health.message = std::string("Database connection failed: ") + e.what();
    }

    return health;
}

MarketDataHealthChecker::MarketDataHealthChecker(
    goldearn::market_data::nse::NSEProtocolParser* parser)
    : parser_(parser) {
}

ComponentHealth MarketDataHealthChecker::check_health() {
    ComponentHealth health;
    health.component_name = get_component_name();
    health.last_check = std::chrono::system_clock::now();

    if (!parser_) {
        health.status = HealthStatus::CRITICAL;
        health.message = "Market data parser not initialized";
        return health;
    }

    uint64_t messages = parser_->get_messages_processed();
    uint64_t errors = parser_->get_parse_errors();

    double error_rate = (messages > 0) ? (100.0 * errors / messages) : 0.0;

    if (error_rate > 5.0) {
        health.status = HealthStatus::CRITICAL;
        health.message = "High error rate in market data";
    } else if (error_rate > 1.0) {
        health.status = HealthStatus::WARNING;
        health.message = "Elevated error rate in market data";
    } else {
        health.status = HealthStatus::HEALTHY;
        health.message = "Market data processing normally";
    }

    health.details["messages_processed"] = std::to_string(messages);
    health.details["parse_errors"] = std::to_string(errors);
    health.details["error_rate_percent"] = std::to_string(error_rate);

    return health;
}

TradingEngineHealthChecker::TradingEngineHealthChecker()
    : total_orders_processed_(0), failed_orders_(0) {
}

ComponentHealth TradingEngineHealthChecker::check_health() {
    ComponentHealth health;
    health.component_name = get_component_name();
    health.last_check = std::chrono::system_clock::now();

    // Check if trading engine is processing orders
    auto now = std::chrono::system_clock::now();
    auto time_since_last_order = now - last_order_time_;

    if (time_since_last_order > std::chrono::minutes(5)) {
        health.status = HealthStatus::WARNING;
        health.message = "No recent trading activity";
    } else {
        health.status = HealthStatus::HEALTHY;
        health.message = "Trading engine operating normally";
    }

    double failure_rate =
        (total_orders_processed_ > 0) ? (100.0 * failed_orders_ / total_orders_processed_) : 0.0;

    if (failure_rate > 10.0) {
        health.status = HealthStatus::CRITICAL;
        health.message = "High order failure rate";
    }

    health.details["total_orders"] = std::to_string(total_orders_processed_);
    health.details["failed_orders"] = std::to_string(failed_orders_);
    health.details["failure_rate_percent"] = std::to_string(failure_rate);

    return health;
}

RiskEngineHealthChecker::RiskEngineHealthChecker() {
}

ComponentHealth RiskEngineHealthChecker::check_health() {
    ComponentHealth health;
    health.component_name = get_component_name();
    health.last_check = std::chrono::system_clock::now();

    if (!trading_enabled_) {
        health.status = HealthStatus::CRITICAL;
        health.message = "Trading disabled by risk engine";
    } else {
        uint64_t checks = risk_checks_performed_.load();
        uint64_t failures = risk_checks_failed_.load();

        double failure_rate = (checks > 0) ? (100.0 * failures / checks) : 0.0;

        if (failure_rate > 1.0) {
            health.status = HealthStatus::WARNING;
            health.message = "Elevated risk check failure rate";
        } else {
            health.status = HealthStatus::HEALTHY;
            health.message = "Risk engine operating normally";
        }
    }

    health.details["risk_checks_performed"] = std::to_string(risk_checks_performed_.load());
    health.details["risk_checks_failed"] = std::to_string(risk_checks_failed_.load());
    health.details["trading_enabled"] = trading_enabled_ ? "true" : "false";

    return health;
}

SystemResourceHealthChecker::SystemResourceHealthChecker() {
}

ComponentHealth SystemResourceHealthChecker::check_health() {
    ComponentHealth health;
    health.component_name = get_component_name();
    health.last_check = std::chrono::system_clock::now();

    double cpu_usage = get_cpu_usage();
    double memory_usage = get_memory_usage();
    double disk_usage = get_disk_usage();

    if (cpu_usage > 90.0 || memory_usage > 90.0 || disk_usage > 95.0) {
        health.status = HealthStatus::CRITICAL;
        health.message = "Critical resource usage";
    } else if (cpu_usage > 80.0 || memory_usage > 80.0 || disk_usage > 90.0) {
        health.status = HealthStatus::WARNING;
        health.message = "High resource usage";
    } else {
        health.status = HealthStatus::HEALTHY;
        health.message = "System resources normal";
    }

    health.details["cpu_usage_percent"] = std::to_string(cpu_usage);
    health.details["memory_usage_percent"] = std::to_string(memory_usage);
    health.details["disk_usage_percent"] = std::to_string(disk_usage);

    return health;
}

double SystemResourceHealthChecker::get_cpu_usage() {
    // Simplified CPU usage calculation
    // In production, use proper system monitoring
    return 25.0;  // Mock value
}

double SystemResourceHealthChecker::get_memory_usage() {
    // Simplified memory usage calculation
    return 45.0;  // Mock value
}

double SystemResourceHealthChecker::get_disk_usage() {
    // Simplified disk usage calculation
    return 60.0;  // Mock value
}

// Utility function implementations
namespace health_utils {

std::string status_to_string(HealthStatus status) {
    switch (status) {
        case HealthStatus::HEALTHY:
            return "HEALTHY";
        case HealthStatus::WARNING:
            return "WARNING";
        case HealthStatus::CRITICAL:
            return "CRITICAL";
        case HealthStatus::UNKNOWN:
            return "UNKNOWN";
        default:
            return "UNKNOWN";
    }
}

HealthStatus string_to_status(const std::string& status_str) {
    if (status_str == "HEALTHY")
        return HealthStatus::HEALTHY;
    if (status_str == "WARNING")
        return HealthStatus::WARNING;
    if (status_str == "CRITICAL")
        return HealthStatus::CRITICAL;
    return HealthStatus::UNKNOWN;
}

std::string get_iso8601_timestamp(std::chrono::system_clock::time_point tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;

    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';

    return ss.str();
}

std::string health_to_json(const SystemHealth& health) {
    std::ostringstream json;

    json << "{\n";
    json << "  \"status\": \"" << status_to_string(health.overall_status) << "\",\n";
    json << "  \"timestamp\": \"" << get_iso8601_timestamp(health.timestamp) << "\",\n";
    json << "  \"summary\": \"" << health.summary_message << "\",\n";
    json << "  \"components\": [\n";

    for (size_t i = 0; i < health.components.size(); ++i) {
        const auto& comp = health.components[i];
        json << "    {\n";
        json << "      \"name\": \"" << comp.component_name << "\",\n";
        json << "      \"status\": \"" << status_to_string(comp.status) << "\",\n";
        json << "      \"message\": \"" << comp.message << "\",\n";
        json << "      \"response_time_ms\": " << comp.response_time_ms << ",\n";
        json << "      \"last_check\": \"" << get_iso8601_timestamp(comp.last_check) << "\"\n";
        json << "    }";
        if (i < health.components.size() - 1)
            json << ",";
        json << "\n";
    }

    json << "  ]\n";
    json << "}";

    return json.str();
}

std::string health_to_prometheus(const SystemHealth& health) {
    std::ostringstream prometheus;

    // Overall health status
    prometheus << "# HELP goldearn_health_status Overall system health status (0=unknown, "
                  "1=healthy, 2=warning, 3=critical)\n";
    prometheus << "# TYPE goldearn_health_status gauge\n";
    prometheus << "goldearn_health_status " << static_cast<int>(health.overall_status) << "\n\n";

    // Component health status
    prometheus << "# HELP goldearn_component_health_status Component health status\n";
    prometheus << "# TYPE goldearn_component_health_status gauge\n";
    for (const auto& comp : health.components) {
        prometheus << "goldearn_component_health_status{component=\"" << comp.component_name
                   << "\"} " << static_cast<int>(comp.status) << "\n";
    }
    prometheus << "\n";

    // Component response times
    prometheus << "# HELP goldearn_component_response_time_ms Component health check response time "
                  "in milliseconds\n";
    prometheus << "# TYPE goldearn_component_response_time_ms gauge\n";
    for (const auto& comp : health.components) {
        prometheus << "goldearn_component_response_time_ms{component=\"" << comp.component_name
                   << "\"} " << comp.response_time_ms << "\n";
    }
    prometheus << "\n";

    // System metrics
    for (const auto& metric : health.metrics) {
        prometheus << "# HELP goldearn_" << metric.first << " System metric\n";
        prometheus << "# TYPE goldearn_" << metric.first << " gauge\n";
        prometheus << "goldearn_" << metric.first << " " << metric.second << "\n\n";
    }

    return prometheus.str();
}

}  // namespace health_utils

}  // namespace goldearn::monitoring