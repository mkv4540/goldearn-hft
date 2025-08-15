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
option(ENABLE_PARALLEL_BUILD "Enable parallel compilation optimizations" ON)
option(ENABLE_UNITY_BUILD "Enable Unity/Jumbo builds for faster compilation" ON)
option(ENABLE_PRECOMPILED_HEADERS "Enable precompiled headers" ON)

# Enable parallel compilation by default
if(ENABLE_PARALLEL_BUILD)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -j$(nproc)")
    endif()
    # Set parallel build jobs for make
    set(CMAKE_BUILD_PARALLEL_LEVEL 0)  # Use all available cores
    # Enable faster linking
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--threads")
    endif()
endif()

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
pkg_check_modules(HIREDIS hiredis QUIET)
if(NOT HIREDIS_FOUND)
    message(STATUS "hiredis not found via pkg-config, trying alternative methods")
    find_library(HIREDIS_LIBRARIES NAMES hiredis)
    find_path(HIREDIS_INCLUDE_DIRS NAMES hiredis/hiredis.h)
    if(HIREDIS_LIBRARIES AND HIREDIS_INCLUDE_DIRS)
        set(HIREDIS_FOUND TRUE)
        set(HIREDIS_LIBRARIES ${HIREDIS_LIBRARIES})
        set(HIREDIS_INCLUDE_DIRS ${HIREDIS_INCLUDE_DIRS})
        message(STATUS "Found hiredis manually: ${HIREDIS_LIBRARIES}")
    else()
        # Try additional search paths for Ubuntu
        find_library(HIREDIS_LIBRARIES NAMES hiredis PATHS /usr/lib/x86_64-linux-gnu /usr/lib)
        find_path(HIREDIS_INCLUDE_DIRS NAMES hiredis/hiredis.h PATHS /usr/include /usr/local/include)
        if(HIREDIS_LIBRARIES AND HIREDIS_INCLUDE_DIRS)
            set(HIREDIS_FOUND TRUE)
            set(HIREDIS_LIBRARIES ${HIREDIS_LIBRARIES})
            set(HIREDIS_INCLUDE_DIRS ${HIREDIS_INCLUDE_DIRS})
            message(STATUS "Found hiredis manually in system paths: ${HIREDIS_LIBRARIES}")
        else()
            message(FATAL_ERROR "hiredis not found. Please install libhiredis-dev")
        endif()
    endif()
endif()

# On macOS, add homebrew library paths
if(APPLE)
    link_directories(/opt/homebrew/lib)
    include_directories(/opt/homebrew/include)
endif()

# PostgreSQL
find_package(PostgreSQL REQUIRED)

# ZeroMQ
pkg_check_modules(ZMQ libzmq QUIET)
if(NOT ZMQ_FOUND)
    message(STATUS "libzmq not found via pkg-config, trying alternative methods")
    find_library(ZMQ_LIBRARIES NAMES zmq)
    find_path(ZMQ_INCLUDE_DIRS NAMES zmq.h)
    if(ZMQ_LIBRARIES AND ZMQ_INCLUDE_DIRS)
        set(ZMQ_FOUND TRUE)
        set(ZMQ_LIBRARIES ${ZMQ_LIBRARIES})
        set(ZMQ_INCLUDE_DIRS ${ZMQ_INCLUDE_DIRS})
        message(STATUS "Found libzmq manually: ${ZMQ_LIBRARIES}")
    else()
        # Try additional search paths for Ubuntu
        find_library(ZMQ_LIBRARIES NAMES zmq PATHS /usr/lib/x86_64-linux-gnu /usr/lib)
        find_path(ZMQ_INCLUDE_DIRS NAMES zmq.h PATHS /usr/include /usr/local/include)
        if(ZMQ_LIBRARIES AND ZMQ_INCLUDE_DIRS)
            set(ZMQ_FOUND TRUE)
            set(ZMQ_LIBRARIES ${ZMQ_LIBRARIES})
            set(ZMQ_INCLUDE_DIRS ${ZMQ_INCLUDE_DIRS})
            message(STATUS "Found libzmq manually in system paths: ${ZMQ_LIBRARIES}")
        else()
            message(FATAL_ERROR "libzmq not found. Please install libzmq3-dev")
        endif()
    endif()
endif()

# Google Test for unit testing
if(BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()
endif()

# Google Benchmark for performance testing
if(BUILD_BENCHMARKS)
    find_package(benchmark QUIET)
    if(NOT benchmark_FOUND)
        message(WARNING "Google Benchmark not found. Benchmarks will be disabled.")
        set(BUILD_BENCHMARKS OFF)
    endif()
endif()

# Boost for some utilities
find_package(Boost REQUIRED COMPONENTS system filesystem thread)

# OpenSSL for secure connections
find_package(OpenSSL REQUIRED)

# nlohmann/json for secure JSON parsing
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found via find_package, using fallback")
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp
        PATHS /usr/local/include /opt/homebrew/include
        PATH_SUFFIXES nlohmann)
    if(NLOHMANN_JSON_INCLUDE_DIR)
        add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
        target_include_directories(nlohmann_json::nlohmann_json INTERFACE ${NLOHMANN_JSON_INCLUDE_DIR})
    endif()
endif()

# jwt-cpp for JWT token management
find_package(jwt-cpp QUIET)
if(NOT jwt-cpp_FOUND)
    message(STATUS "jwt-cpp not found via find_package, using fallback")
    find_path(JWT_CPP_INCLUDE_DIR jwt-cpp/jwt.h
        PATHS /usr/local/include /opt/homebrew/include)
    if(JWT_CPP_INCLUDE_DIR)
        add_library(jwt-cpp::jwt-cpp INTERFACE IMPORTED)
        target_include_directories(jwt-cpp::jwt-cpp INTERFACE ${JWT_CPP_INCLUDE_DIR})
    endif()
endif()

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
    src/config/secure_config_manager.cpp
)

set(UTILS_SOURCES
    src/utils/logger.cpp
)

# Placeholder for future sources
set(TRADING_SOURCES)
set(RISK_SOURCES
    src/risk/risk_engine.cpp
    src/risk/advanced_position_tracker.cpp
)
set(STRATEGIES_SOURCES)
set(NETWORK_SOURCES
    src/network/secure_connection.cpp
    src/network/exchange_auth.cpp
)
set(MONITORING_SOURCES
    src/monitoring/health_check.cpp
    src/monitoring/prometheus_metrics.cpp
    src/monitoring/secure_health_check.cpp
)
set(SECURITY_SOURCES
    src/security/certificate_manager.cpp
    src/security/secure_jwt_manager.cpp
)

# Main library - include all sources
add_library(goldearn_core STATIC
    ${MARKET_DATA_SOURCES}
    ${CORE_SOURCES}
    ${CONFIG_SOURCES}
    ${UTILS_SOURCES}
    ${RISK_SOURCES}
    ${NETWORK_SOURCES}
    ${MONITORING_SOURCES}
    ${SECURITY_SOURCES}
)

# Set library properties with build optimizations
set_target_properties(goldearn_core PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# Enable Unity Build for faster compilation
if(ENABLE_UNITY_BUILD AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    set_target_properties(goldearn_core PROPERTIES UNITY_BUILD ON)
    # Limit unity build batch size to avoid memory issues
    set_target_properties(goldearn_core PROPERTIES UNITY_BUILD_BATCH_SIZE 8)
endif()

# Enable precompiled headers for faster compilation
if(ENABLE_PRECOMPILED_HEADERS AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    target_precompile_headers(goldearn_core PRIVATE
        <iostream>
        <string>
        <vector>
        <memory>
        <chrono>
        <thread>
        <mutex>
        <atomic>
        <algorithm>
        <unordered_map>
        <queue>
    )
endif()

# Link libraries
target_link_libraries(goldearn_core
    Threads::Threads
    ${HIREDIS_LIBRARIES}
    ${PostgreSQL_LIBRARIES}
    ${ZMQ_LIBRARIES}
    Boost::system
    Boost::filesystem
    Boost::thread
    ${OPENSSL_LIBRARIES}
)

# Link JSON and JWT libraries conditionally
if(nlohmann_json_FOUND OR NLOHMANN_JSON_INCLUDE_DIR)
    target_link_libraries(goldearn_core nlohmann_json::nlohmann_json)
endif()

if(jwt-cpp_FOUND OR JWT_CPP_INCLUDE_DIR)
    target_link_libraries(goldearn_core jwt-cpp::jwt-cpp)
endif()

# Include directories for library
target_include_directories(goldearn_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${HIREDIS_INCLUDE_DIRS}
    ${PostgreSQL_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
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
if(BUILD_BENCHMARKS AND EXISTS "${CMAKE_SOURCE_DIR}/benchmarks")
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

# Code formatting with clang-format (optimized for speed)
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE ALL_SOURCE_FILES ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/*.hpp)
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND} -E echo "Formatting ${CMAKE_CURRENT_LIST_LENGTH} source files in parallel..."
        COMMAND find ${CMAKE_SOURCE_DIR}/src -name "*.cpp" -o -name "*.hpp" | xargs -P0 -I{} ${CLANG_FORMAT} -i {}
        COMMENT "Formatting code with clang-format (parallel execution)"
        VERBATIM
    )
endif()

# Static analysis with clang-tidy (optimized for speed)
find_program(CLANG_TIDY clang-tidy)
if(CLANG_TIDY)
    file(GLOB_RECURSE ALL_CPP_FILES ${CMAKE_SOURCE_DIR}/src/*.cpp)
    add_custom_target(lint
        COMMAND ${CMAKE_COMMAND} -E echo "Running static analysis on ${CMAKE_CURRENT_LIST_LENGTH} files in parallel..."
        COMMAND find ${CMAKE_SOURCE_DIR}/src -name "*.cpp" | xargs -P0 -I{} ${CLANG_TIDY} {} -p ${CMAKE_BINARY_DIR} --quiet
        COMMENT "Running static analysis with clang-tidy (parallel execution)"
        VERBATIM
        DEPENDS ${CMAKE_BINARY_DIR}/compile_commands.json
    )
endif()

# Code coverage target
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    find_program(GCOV gcov)
    find_program(LCOV lcov)
    find_program(GENHTML genhtml)
    
    if(GCOV AND LCOV AND GENHTML)
        # Add coverage flags
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
        
        # Coverage target
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
            COMMAND ${LCOV} --capture --directory ${CMAKE_BINARY_DIR} --output-file ${CMAKE_BINARY_DIR}/coverage/coverage.info
            COMMAND ${LCOV} --remove ${CMAKE_BINARY_DIR}/coverage/coverage.info '/usr/*' '${CMAKE_SOURCE_DIR}/temp/*' '${CMAKE_SOURCE_DIR}/tests/*' --output-file ${CMAKE_BINARY_DIR}/coverage/coverage_filtered.info
            COMMAND ${GENHTML} ${CMAKE_BINARY_DIR}/coverage/coverage_filtered.info --output-directory ${CMAKE_BINARY_DIR}/coverage/html
            COMMAND ${CMAKE_COMMAND} -E echo "Coverage report generated in ${CMAKE_BINARY_DIR}/coverage/html/index.html"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating code coverage report"
        )
        
        # Clean coverage data
        add_custom_target(coverage-clean
            COMMAND ${LCOV} --zerocounters --directory ${CMAKE_BINARY_DIR}
            COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/coverage
            COMMENT "Cleaning coverage data"
        )
    endif()
endif()

# Memory check with valgrind
find_program(VALGRIND valgrind)
if(VALGRIND)
    add_custom_target(memcheck
        COMMAND ${VALGRIND} --tool=memcheck --leak-check=full --show-leak-kinds=all --error-exitcode=1
                $<TARGET_FILE:goldearn_engine> --help
        COMMENT "Running memory check with valgrind"
        DEPENDS goldearn_engine
    )
endif()

# Performance profiling with perf
find_program(PERF perf)
if(PERF)
    add_custom_target(profile
        COMMAND ${PERF} record -g $<TARGET_FILE:goldearn_engine> --help
        COMMAND ${PERF} report
        COMMENT "Running performance profiling with perf"
        DEPENDS goldearn_engine
    )
endif()

# Generate compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Print build configuration with optimization status
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build tests: ${BUILD_TESTS}")
message(STATUS "Build benchmarks: ${BUILD_BENCHMARKS}")
message(STATUS "Enable profiling: ${ENABLE_PROFILING}")
message(STATUS "Enable sanitizers: ${ENABLE_SANITIZERS}")
message(STATUS "Enable parallel build: ${ENABLE_PARALLEL_BUILD}")
message(STATUS "Enable unity build: ${ENABLE_UNITY_BUILD}")
message(STATUS "Enable precompiled headers: ${ENABLE_PRECOMPILED_HEADERS}")
message(STATUS "Parallel build level: ${CMAKE_BUILD_PARALLEL_LEVEL}")
if(ENABLE_PARALLEL_BUILD)
    cmake_host_system_information(RESULT CPU_CORES QUERY NUMBER_OF_LOGICAL_CORES)
    message(STATUS "Available CPU cores: ${CPU_CORES}")
endif()