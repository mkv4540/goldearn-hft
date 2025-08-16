#pragma once

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>

namespace goldearn::utils {

enum class LogLevel { TRACE = 0, DEBUG = 1, INFO = 2, WARNING = 3, ERROR = 4, CRITICAL = 5 };

class Logger {
   public:
    static Logger& instance() {
        static Logger instance;
        return instance;
    }

    void set_level(LogLevel level) {
        log_level_.store(level);
    }
    LogLevel get_level() const {
        return log_level_.load();
    }

    void set_file_output(const std::string& filename);
    void set_console_output(bool enabled) {
        console_output_ = enabled;
    }

    void log(LogLevel level,
             const std::string& message,
             const char* file = __builtin_FILE(),
             int line = __builtin_LINE(),
             const char* function = __builtin_FUNCTION());

    // Overload for when no arguments are provided
    void log_formatted(LogLevel level,
                       const std::string& format,
                       const char* file = __builtin_FILE(),
                       int line = __builtin_LINE(),
                       const char* function = __builtin_FUNCTION()) {
        if (level < log_level_.load())
            return;
        log(level, format, file, line, function);
    }

    template <typename... Args>
    typename std::enable_if<sizeof...(Args) != 0, void>::type log_formatted(
        LogLevel level,
        const std::string& format,
        Args&&... args,
        const char* file = __builtin_FILE(),
        int line = __builtin_LINE(),
        const char* function = __builtin_FUNCTION()) {
        if (level < log_level_.load())
            return;

        try {
            std::string message = format_string(format, std::forward<Args>(args)...);
            log(level, message, file, line, function);
        } catch (const std::exception& e) {
            // Fallback to basic logging if formatting fails
            log(LogLevel::ERROR,
                "Log formatting error: " + std::string(e.what()),
                file,
                line,
                function);
            log(level, format, file, line, function);
        }
    }

    // Performance-critical logging for HFT
    void log_fast(LogLevel level, const char* message);

    // Statistics
    uint64_t get_messages_logged() const {
        return messages_logged_.load();
    }
    uint64_t get_messages_dropped() const {
        return messages_dropped_.load();
    }

    void flush();

   private:
    Logger();
    ~Logger();

    std::atomic<LogLevel> log_level_{LogLevel::INFO};
    std::atomic<bool> console_output_{true};
    std::unique_ptr<std::ofstream> file_stream_;
    std::mutex file_mutex_;

    std::atomic<uint64_t> messages_logged_{0};
    std::atomic<uint64_t> messages_dropped_{0};

    // Base case for no arguments
    std::string format_string(const std::string& format) {
        return format;
    }

    template <typename T>
    std::string format_string(const std::string& format, T&& value) {
        // Simple format replacement for {}
        std::string result = format;
        size_t pos = result.find("{}");
        if (pos != std::string::npos) {
            std::ostringstream oss;
            oss << std::forward<T>(value);
            result.replace(pos, 2, oss.str());
        }
        return result;
    }

    template <typename T, typename... Args>
    std::string format_string(const std::string& format, T&& first, Args&&... rest) {
        std::string partial = format_string(format, std::forward<T>(first));
        return format_string(partial, std::forward<Args>(rest)...);
    }

    std::string get_timestamp();
    std::string level_to_string(LogLevel level);
    void write_to_outputs(const std::string& formatted_message);
};

// Note: Logging macros are defined in simple_logger.hpp to avoid conflicts
// Use goldearn::utils::Logger::instance().log_formatted() directly for advanced logging

// High-performance logging for critical paths
#define LOG_FAST(level, msg) goldearn::utils::Logger::instance().log_fast(level, msg)

}  // namespace goldearn::utils