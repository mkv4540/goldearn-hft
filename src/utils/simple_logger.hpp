#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <atomic>

namespace goldearn::utils {

// Simple logger that converts everything to string
class SimpleLogger {
public:
    template<typename... Args>
    static void log(const std::string& level, const std::string& format, Args... args) {
        std::string msg = format_string(format, args...);
        std::cerr << "[" << level << "] " << msg << std::endl;
    }
    
private:
    static std::string format_string(const std::string& format) {
        return format;
    }
    
    template<typename T>
    static std::string to_string(const T& value) {
        if constexpr (std::is_arithmetic_v<T>) {
            std::ostringstream oss;
            oss << value;
            return oss.str();
        } else {
            std::ostringstream oss;
            oss << value;
            return oss.str();
        }
    }
    
    // Specialization for atomic types
    template<typename T>
    static std::string to_string(const std::atomic<T>& value) {
        return to_string(value.load());
    }
    
    template<typename T, typename... Args>
    static std::string format_string(const std::string& format, T first, Args... rest) {
        size_t pos = format.find("{}");
        if (pos == std::string::npos) {
            return format;
        }
        
        std::string result = format.substr(0, pos);
        result += to_string(first);
        result += format_string(format.substr(pos + 2), rest...);
        return result;
    }
};

// Simplified macros
#define LOG_ERROR(fmt, ...) goldearn::utils::SimpleLogger::log("ERROR", fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) goldearn::utils::SimpleLogger::log("WARNING", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) goldearn::utils::SimpleLogger::log("WARNING", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) goldearn::utils::SimpleLogger::log("INFO", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) goldearn::utils::SimpleLogger::log("DEBUG", fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) goldearn::utils::SimpleLogger::log("TRACE", fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(fmt, ...) goldearn::utils::SimpleLogger::log("CRITICAL", fmt, ##__VA_ARGS__)

} // namespace goldearn::utils