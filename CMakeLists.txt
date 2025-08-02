cmake_minimum_required(VERSION 3.20)
project(GoldEarn_HFT VERSION 1.0.0 LANGUAGES CXX)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build configuration
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_BENCHMARKS "Build performance benchmarks" ON)
option(ENABLE_PROFILING "Enable profiling support" OFF)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)

# Compiler-specific optimizations for ultra-low latency
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Release optimizations
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -mtune=native")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto -ffast-math")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -funroll-loops -fomit-frame-pointer")
    
    # Debug flags
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
    
    # Warning flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter -Wno-sign-compare")
    
    # Low-latency specific flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2 -mavx2") # SIMD support
endif()

# Sanitizers for development
if(ENABLE_SANITIZERS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")
endif()

# Find required packages
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Redis/hiredis
pkg_check_modules(HIREDIS REQUIRED hiredis)

# PostgreSQL
find_package(PostgreSQL REQUIRED)

# ZeroMQ
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

# Google Test for unit testing
if(BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()
endif()

# Google Benchmark for performance testing
if(BUILD_BENCHMARKS)
    find_package(benchmark REQUIRED)
endif()

# Boost for some utilities
find_package(Boost REQUIRED COMPONENTS system filesystem thread)

# OpenSSL for secure connections
find_package(OpenSSL REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files organization
set(MARKET_DATA_SOURCES
    src/market_data/nse_protocol.cpp
    src/market_data/order_book.cpp
)

set(CORE_SOURCES
    src/core/latency_tracker.cpp
)

set(CONFIG_SOURCES
    src/config/config_manager.cpp
)

set(UTILS_SOURCES
    src/utils/logger.cpp
)

# Placeholder for future sources
set(TRADING_SOURCES)
set(RISK_SOURCES)
set(STRATEGIES_SOURCES)
set(NETWORK_SOURCES
    src/network/secure_connection.cpp
)
set(MONITORING_SOURCES)

# Main library
add_library(goldearn_core STATIC
    ${MARKET_DATA_SOURCES}
    ${TRADING_SOURCES}
    ${CORE_SOURCES}
    ${CONFIG_SOURCES}
    ${RISK_SOURCES}
    ${STRATEGIES_SOURCES}
    ${UTILS_SOURCES}
    ${NETWORK_SOURCES}
    ${MONITORING_SOURCES}
)

# Set library properties
set_target_properties(goldearn_core PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# Link libraries
target_link_libraries(goldearn_core
    Threads::Threads
    ${HIREDIS_LIBRARIES}
    ${PostgreSQL_LIBRARIES}
    ${ZMQ_LIBRARIES}
    Boost::system
    Boost::filesystem
    Boost::thread
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Include directories for library
target_include_directories(goldearn_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${HIREDIS_INCLUDE_DIRS}
    ${PostgreSQL_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

# Main trading engine executable
add_executable(goldearn_engine
    src/main/trading_main.cpp
)
target_link_libraries(goldearn_engine goldearn_core)

# Market data feed handler executable
add_executable(goldearn_feed_handler
    src/main/feed_handler_main.cpp
)
target_link_libraries(goldearn_feed_handler goldearn_core)

# Risk monitor executable
add_executable(goldearn_risk_monitor
    src/main/risk_monitor_main.cpp
)
target_link_libraries(goldearn_risk_monitor goldearn_core)

# Unit tests
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Benchmarks
if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# Installation
install(TARGETS goldearn_core goldearn_engine goldearn_feed_handler goldearn_risk_monitor
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY src/ DESTINATION include/goldearn
    FILES_MATCHING PATTERN "*.hpp"
)

# Configuration files
install(DIRECTORY config/ DESTINATION etc/goldearn
    FILES_MATCHING PATTERN "*.conf" PATTERN "*.json" PATTERN "*.yaml"
)

# Documentation
install(DIRECTORY docs/ DESTINATION share/doc/goldearn
    FILES_MATCHING PATTERN "*.md"
)

# Packaging
set(CPACK_PACKAGE_NAME "GoldEarn-HFT")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-Frequency Trading System")
set(CPACK_PACKAGE_VENDOR "GoldEarn Technologies")
set(CPACK_GENERATOR "TGZ;DEB")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Debian package configuration
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "GoldEarn Dev Team <dev@goldearn.com>")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libhiredis-dev, libpq-dev, libzmq3-dev, libboost-all-dev")
    set(CPACK_DEBIAN_PACKAGE_SECTION "finance")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
endif()

include(CPack)

# Custom targets for development

# Code formatting with clang-format
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    add_custom_target(format
        COMMAND find ${CMAKE_SOURCE_DIR}/src -name "*.cpp" -o -name "*.hpp" | xargs ${CLANG_FORMAT} -i
        COMMENT "Formatting code with clang-format"
    )
endif()

# Static analysis with clang-tidy
find_program(CLANG_TIDY clang-tidy)
if(CLANG_TIDY)
    add_custom_target(lint
        COMMAND ${CLANG_TIDY} ${CMAKE_SOURCE_DIR}/src/**/*.cpp -p ${CMAKE_BINARY_DIR}
        COMMENT "Running static analysis with clang-tidy"
    )
endif()

# Memory check with valgrind
find_program(VALGRIND valgrind)
if(VALGRIND)
    add_custom_target(memcheck
        COMMAND ${VALGRIND} --tool=memcheck --leak-check=full --show-leak-kinds=all 
                ${CMAKE_BINARY_DIR}/goldearn_engine --config=${CMAKE_SOURCE_DIR}/config/test.conf
        COMMENT "Running memory check with valgrind"
    )
endif()

# Performance profiling with perf
add_custom_target(profile
    COMMAND perf record -g ${CMAKE_BINARY_DIR}/goldearn_engine --config=${CMAKE_SOURCE_DIR}/config/profile.conf
    COMMAND perf report
    COMMENT "Running performance profiling with perf"
)

# Generate compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Print build configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build tests: ${BUILD_TESTS}")
message(STATUS "Build benchmarks: ${BUILD_BENCHMARKS}")
message(STATUS "Enable profiling: ${ENABLE_PROFILING}")
message(STATUS "Enable sanitizers: ${ENABLE_SANITIZERS}")