#include "secure_connection.hpp"
#include "../utils/simple_logger.hpp"
#include <thread>
#include <iostream>
#include <cstring>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace goldearn::network {

// Base SecureConnection implementation
SecureConnection::SecureConnection(const ConnectionConfig& config) 
    : config_(config), state_(ConnectionState::DISCONNECTED) {
    LOG_INFO("SecureConnection: Initialized with config for {}:{}", config.host, config.port);
}

SecureConnection::~SecureConnection() {
    if (is_connected()) {
        disconnect();
    }
}

bool SecureConnection::connect() {
    if (is_connected()) {
        LOG_WARN("SecureConnection: Already connected");
        return true;
    }
    
    set_state(ConnectionState::CONNECTING, "Initiating connection");
    
    if (!establish_connection()) {
        set_state(ConnectionState::ERROR, "Failed to establish connection");
        return false;
    }
    
    set_state(ConnectionState::CONNECTED, "Connection established");
    stats_.connection_attempts++;
    stats_.last_connect_time = std::chrono::steady_clock::now();
    
    if (config_.auto_reconnect) {
        start_receive_loop();
    }
    
    return true;
}

void SecureConnection::disconnect() {
    if (!is_connected()) {
        return;
    }
    
    set_state(ConnectionState::DISCONNECTED, "Disconnecting");
    close_connection();
    stop_receive_loop();
}

bool SecureConnection::send_data(const uint8_t* data, size_t length) {
    if (!is_connected()) {
        LOG_ERROR("SecureConnection: Cannot send data - not connected");
        return false;
    }
    
    if (!check_rate_limits()) {
        LOG_WARN("SecureConnection: Rate limit exceeded");
        return false;
    }
    
    if (!send_raw_data(data, length)) {
        LOG_ERROR("SecureConnection: Failed to send raw data");
        return false;
    }
    
    stats_.bytes_sent += length;
    stats_.messages_sent++;
    stats_.last_message_time = std::chrono::steady_clock::now();
    update_rate_counters();
    
    return true;
}

bool SecureConnection::send_message(const std::string& message) {
    return send_data(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
}

void SecureConnection::update_config(const ConnectionConfig& config) {
    config_ = config;
    LOG_INFO("SecureConnection: Configuration updated");
}

void SecureConnection::reset_stats() {
    // Reset individual fields since ConnectionStats has atomic members
    stats_.bytes_sent = 0;
    stats_.bytes_received = 0;
    stats_.messages_sent = 0;
    stats_.messages_received = 0;
    stats_.connection_attempts = 0;
    stats_.connection_failures = 0;
    stats_.timeouts = 0;
    stats_.protocol_errors = 0;
    stats_.current_latency_ms = 0;
    stats_.avg_latency_ms = 0;
    LOG_INFO("SecureConnection: Statistics reset");
}

bool SecureConnection::send_heartbeat() {
    if (!is_connected()) {
        return false;
    }
    
    // Send a simple heartbeat message
    std::string heartbeat = "HEARTBEAT";
    return send_message(heartbeat);
}

std::chrono::milliseconds SecureConnection::get_last_activity_time() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - stats_.last_message_time);
}

double SecureConnection::get_connection_uptime_seconds() const {
    if (stats_.last_connect_time.time_since_epoch().count() == 0) {
        return 0.0;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - stats_.last_connect_time);
    return uptime.count();
}

bool SecureConnection::check_rate_limits() const {
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_rate_check_);
    
    if (elapsed.count() >= 1) {
        // Reset counters every second
        const_cast<SecureConnection*>(this)->messages_this_second_ = 0;
        const_cast<SecureConnection*>(this)->bytes_this_second_ = 0;
        const_cast<SecureConnection*>(this)->last_rate_check_ = now;
    }
    
    return messages_this_second_ < config_.max_messages_per_second &&
           bytes_this_second_ < config_.max_bytes_per_second;
}

void SecureConnection::update_rate_counters() {
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);
    messages_this_second_++;
    bytes_this_second_ += 1; // Simplified for now
}

void SecureConnection::set_state(ConnectionState new_state, const std::string& reason) {
    auto old_state = state_.load();
    state_.store(new_state);
    
    if (state_callback_) {
        state_callback_(new_state, reason);
    }
    
    LOG_INFO("SecureConnection: State changed from {} to {}: {}", 
             static_cast<int>(old_state), static_cast<int>(new_state), reason);
}

void SecureConnection::report_error(const std::string& error) {
    if (error_callback_) {
        error_callback_(error);
    }
    LOG_ERROR("SecureConnection: {}", error);
}

void SecureConnection::start_receive_loop() {
    if (receive_thread_) {
        return; // Already running
    }
    
    stop_requested_ = false;
    receive_thread_ = std::make_unique<std::thread>([this]() { receive_loop(); });
    LOG_INFO("SecureConnection: Receive loop started");
}

void SecureConnection::stop_receive_loop() {
    if (!receive_thread_) {
        return;
    }
    
    stop_requested_ = true;
    if (receive_thread_->joinable()) {
        receive_thread_->join();
    }
    receive_thread_.reset();
    LOG_INFO("SecureConnection: Receive loop stopped");
}

void SecureConnection::receive_loop() {
    while (!stop_requested_.load()) {
        uint8_t buffer[4096];
        size_t received = receive_data(buffer, sizeof(buffer));
        
        if (received > 0) {
            stats_.bytes_received += received;
            stats_.messages_received++;
            stats_.last_message_time = std::chrono::steady_clock::now();
            
            if (data_callback_) {
                data_callback_(buffer, received);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void SecureConnection::reconnect_loop() {
    while (!stop_requested_.load() && reconnect_attempts_ < config_.max_reconnect_attempts) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.reconnect_delay_ms));
        
        if (connect()) {
            reconnect_attempts_ = 0;
            break;
        }
        
        reconnect_attempts_++;
        LOG_WARN("SecureConnection: Reconnection attempt {} failed", reconnect_attempts_.load());
    }
}

bool SecureConnection::validate_message(const uint8_t* data, size_t length) const {
    return length <= config_.max_message_size;
}

void SecureConnection::update_latency_stats(uint32_t latency_ms) {
    stats_.current_latency_ms = latency_ms;
    
    // Simple moving average
    uint32_t current_avg = stats_.avg_latency_ms.load();
    uint32_t new_avg = (current_avg + latency_ms) / 2;
    stats_.avg_latency_ms = new_avg;
}

// SecureTCPConnection implementation
SecureTCPConnection::SecureTCPConnection(const ConnectionConfig& config)
    : SecureConnection(config), socket_fd_(-1), ssl_context_(nullptr), ssl_connection_(nullptr) {
    LOG_INFO("SecureTCPConnection: Initialized for {}:{}", config.host, config.port);
}

SecureTCPConnection::~SecureTCPConnection() {
    close_connection();
}

bool SecureTCPConnection::establish_connection() {
    // Create socket
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        LOG_ERROR("SecureTCPConnection: Failed to create socket");
        return false;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Set non-blocking
    fcntl(socket_fd_, F_SETFL, O_NONBLOCK);
    
    // Connect
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(config_.port);
    inet_pton(AF_INET, config_.host.c_str(), &server_addr.sin_addr);
    
    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        if (errno != EINPROGRESS) {
            LOG_ERROR("SecureTCPConnection: Failed to connect to {}:{}", config_.host, config_.port);
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
    }
    
    // Setup SSL if required
    if (config_.security != SecurityLevel::NONE) {
        if (!setup_ssl_context()) {
            LOG_ERROR("SecureTCPConnection: Failed to setup SSL context");
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
    }
    
    LOG_INFO("SecureTCPConnection: Connected to {}:{}", config_.host, config_.port);
    return true;
}

void SecureTCPConnection::close_connection() {
    if (ssl_connection_) {
        // SSL cleanup would go here
        ssl_connection_ = nullptr;
    }
    
    if (ssl_context_) {
        // SSL context cleanup would go here
        ssl_context_ = nullptr;
    }
    
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

bool SecureTCPConnection::send_raw_data(const uint8_t* data, size_t length) {
    if (socket_fd_ < 0) {
        return false;
    }
    
    ssize_t sent = send(socket_fd_, data, length, 0);
    return sent == static_cast<ssize_t>(length);
}

size_t SecureTCPConnection::receive_data(uint8_t* buffer, size_t max_length) {
    if (socket_fd_ < 0) {
        return 0;
    }
    
    ssize_t received = recv(socket_fd_, buffer, max_length, 0);
    return (received > 0) ? static_cast<size_t>(received) : 0;
}

bool SecureTCPConnection::setup_ssl_context() {
    // SSL setup would go here
    // For now, just log that SSL is not implemented
    LOG_WARN("SecureTCPConnection: SSL not yet implemented");
    return true;
}

void SecureTCPConnection::cleanup_ssl() {
    // SSL cleanup would go here
}

bool SecureTCPConnection::verify_peer_certificate() {
    // Certificate verification would go here
    return true;
}

std::string SecureTCPConnection::get_ssl_error_string() const {
    return "SSL not implemented";
}

} // namespace goldearn::network