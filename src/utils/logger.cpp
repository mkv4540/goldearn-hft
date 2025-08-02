#include "logger.hpp"
#include <iostream>
#include <iomanip>
#include <thread>
#include <cstring>

namespace goldearn::utils {

Logger::Logger() = default;

Logger::~Logger() {
    flush();
}

void Logger::set_file_output(const std::string& filename) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    if (file_stream_) {
        file_stream_->close();
    }
    
    file_stream_ = std::make_unique<std::ofstream>(filename, std::ios::app);
    if (!file_stream_->is_open()) {
        std::cerr << "Failed to open log file: " << filename << std::endl;
        file_stream_.reset();
    }
}

void Logger::log(LogLevel level, const std::string& message, const char* file, int line, const char* function) {
    if (level < log_level_.load()) {
        messages_dropped_.fetch_add(1);
        return;
    }
    
    try {
        std::ostringstream oss;
        oss << "[" << get_timestamp() << "] "
            << "[" << level_to_string(level) << "] "
            << "[" << std::this_thread::get_id() << "] ";
        
        // Extract just filename from full path
        const char* filename = strrchr(file, '/');
        filename = filename ? filename + 1 : file;
        
        oss << "[" << filename << ":" << line << " " << function << "] "
            << message;
        
        write_to_outputs(oss.str());
        messages_logged_.fetch_add(1);
        
    } catch (const std::exception& e) {
        messages_dropped_.fetch_add(1);
        // Last resort - try to log to stderr
        std::cerr << "Logger error: " << e.what() << " - Original message: " << message << std::endl;
    }
}

void Logger::log_fast(LogLevel level, const char* message) {
    if (level < log_level_.load()) {
        messages_dropped_.fetch_add(1);
        return;
    }
    
    // Ultra-fast logging without formatting for HFT critical paths
    if (console_output_) {
        std::cout << message << std::endl;
    }
    
    // Skip file logging for maximum speed
    messages_logged_.fetch_add(1);
}

void Logger::flush() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    if (console_output_) {
        std::cout.flush();
    }
    
    if (file_stream_ && file_stream_->is_open()) {
        file_stream_->flush();
    }
}

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE:    return "TRACE";
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO";
        case LogLevel::WARNING:  return "WARN";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRIT";
        default:                 return "UNKNOWN";
    }
}

void Logger::write_to_outputs(const std::string& formatted_message) {
    // Console output
    if (console_output_) {
        std::cout << formatted_message << std::endl;
    }
    
    // File output
    {
        std::lock_guard<std::mutex> lock(file_mutex_);
        if (file_stream_ && file_stream_->is_open()) {
            *file_stream_ << formatted_message << std::endl;
        }
    }
}

} // namespace goldearn::utils