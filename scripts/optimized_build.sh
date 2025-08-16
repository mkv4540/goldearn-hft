#!/bin/bash
# High-Performance Build Script with Extended Timeouts
# This script implements automatic timeout extensions (minimum 20 minutes per operation)
# and applies comprehensive speed optimizations

set -euo pipefail

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration with extended timeouts (minimum 20 minutes = 1200 seconds)
CONFIGURE_TIMEOUT=1200
BUILD_TIMEOUT=1200
TEST_TIMEOUT=1200
INSTALL_TIMEOUT=1200

# Performance optimization settings
CPU_CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo "4")
MEMORY_GB=$(free -g 2>/dev/null | awk '/^Mem:/{print $2}' || echo "8")
MAX_PARALLEL_JOBS=$((CPU_CORES > 16 ? 16 : CPU_CORES))

# Build configuration
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${BUILD_DIR:-build}"
ENABLE_TESTS="${ENABLE_TESTS:-ON}"
ENABLE_BENCHMARKS="${ENABLE_BENCHMARKS:-OFF}"
ENABLE_SANITIZERS="${ENABLE_SANITIZERS:-OFF}"

# Optimization flags
ENABLE_UNITY_BUILD="${ENABLE_UNITY_BUILD:-ON}"
ENABLE_PRECOMPILED_HEADERS="${ENABLE_PRECOMPILED_HEADERS:-ON}"
ENABLE_PARALLEL_BUILD="${ENABLE_PARALLEL_BUILD:-ON}"
ENABLE_CCACHE="${ENABLE_CCACHE:-ON}"

print_header() {
    echo -e "${BLUE}================================================================${NC}"
    echo -e "${BLUE} $1${NC}"
    echo -e "${BLUE}================================================================${NC}"
}

print_info() {
    echo -e "${CYAN}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to run commands with extended timeout
run_with_timeout() {
    local timeout_seconds=$1
    local description=$2
    shift 2
    
    print_info "Running: $description (timeout: ${timeout_seconds}s / $((timeout_seconds/60))min)"
    print_info "Command: $*"
    
    if timeout "${timeout_seconds}s" "$@"; then
        print_success "$description completed successfully"
        return 0
    else
        local exit_code=$?
        if [ $exit_code -eq 124 ]; then
            print_error "$description timed out after ${timeout_seconds} seconds"
        else
            print_error "$description failed with exit code $exit_code"
        fi
        return $exit_code
    fi
}

# System information and optimization setup
setup_build_environment() {
    print_header "Setting Up High-Performance Build Environment"
    
    print_info "System Information:"
    echo "  CPU Cores: $CPU_CORES"
    echo "  Memory: ${MEMORY_GB}GB"
    echo "  Max Parallel Jobs: $MAX_PARALLEL_JOBS"
    echo "  Build Type: $BUILD_TYPE"
    echo "  Build Directory: $BUILD_DIR"
    
    print_info "Optimization Settings:"
    echo "  Unity Build: $ENABLE_UNITY_BUILD"
    echo "  Precompiled Headers: $ENABLE_PRECOMPILED_HEADERS"
    echo "  Parallel Build: $ENABLE_PARALLEL_BUILD"
    echo "  CCache: $ENABLE_CCACHE"
    
    # Setup ccache if available
    if command -v ccache >/dev/null 2>&1 && [ "$ENABLE_CCACHE" = "ON" ]; then
        print_info "Setting up ccache for faster rebuilds"
        export CC="ccache gcc"
        export CXX="ccache g++"
        ccache --set-config=max_size=2G
        ccache --zero-stats
    fi
    
    # Optimize system settings for build performance
    if [ "$(uname)" = "Linux" ]; then
        print_info "Optimizing system settings for build performance"
        # Increase file descriptor limits
        ulimit -n 65536 2>/dev/null || print_warning "Could not increase file descriptor limit"
        # Set CPU governor to performance if available
        if [ -d /sys/devices/system/cpu/cpu0/cpufreq ]; then
            sudo cpupower frequency-set -g performance 2>/dev/null || print_warning "Could not set CPU governor"
        fi
    fi
    
    print_success "Build environment setup completed"
}

# Clean and prepare build directory
prepare_build_directory() {
    print_header "Preparing Build Directory"
    
    if [ -d "$BUILD_DIR" ]; then
        print_info "Cleaning existing build directory"
        rm -rf "$BUILD_DIR"
    fi
    
    print_info "Creating build directory: $BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    
    print_success "Build directory prepared"
}

# Configure CMake with optimizations
configure_cmake() {
    print_header "Configuring CMake with Speed Optimizations"
    
    local cmake_args=(
        -B "$BUILD_DIR"
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
        -DBUILD_TESTS="$ENABLE_TESTS"
        -DBUILD_BENCHMARKS="$ENABLE_BENCHMARKS"
        -DENABLE_SANITIZERS="$ENABLE_SANITIZERS"
        -DENABLE_PARALLEL_BUILD="$ENABLE_PARALLEL_BUILD"
        -DENABLE_UNITY_BUILD="$ENABLE_UNITY_BUILD"
        -DENABLE_PRECOMPILED_HEADERS="$ENABLE_PRECOMPILED_HEADERS"
        -DCMAKE_BUILD_PARALLEL_LEVEL="$MAX_PARALLEL_JOBS"
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    )
    
    # Use Ninja generator if available (faster than Make)
    if command -v ninja >/dev/null 2>&1; then
        cmake_args+=(-G "Ninja")
        print_info "Using Ninja generator for faster builds"
    else
        print_warning "Ninja not found, using default generator"
    fi
    
    # Add ccache support if enabled
    if [ "$ENABLE_CCACHE" = "ON" ] && command -v ccache >/dev/null 2>&1; then
        cmake_args+=(
            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
            -DCMAKE_C_COMPILER_LAUNCHER=ccache
        )
    fi
    
    print_info "CMake configuration arguments:"
    printf '  %s\n' "${cmake_args[@]}"
    
    run_with_timeout $CONFIGURE_TIMEOUT "CMake Configuration" cmake "${cmake_args[@]}"
}

# Build the project with maximum parallelization
build_project() {
    print_header "Building Project with Maximum Parallelization"
    
    local build_args=(
        --build "$BUILD_DIR"
        --config "$BUILD_TYPE"
        --parallel "$MAX_PARALLEL_JOBS"
    )
    
    print_info "Build arguments:"
    printf '  %s\n' "${build_args[@]}"
    
    print_info "Starting high-performance build..."
    print_info "Memory usage before build:"
    free -h 2>/dev/null || echo "Memory info not available"
    
    local start_time=$(date +%s)
    
    run_with_timeout $BUILD_TIMEOUT "Project Build" cmake "${build_args[@]}"
    
    local end_time=$(date +%s)
    local build_duration=$((end_time - start_time))
    
    print_success "Build completed in ${build_duration} seconds ($((build_duration/60))min $((build_duration%60))s)"
    
    print_info "Memory usage after build:"
    free -h 2>/dev/null || echo "Memory info not available"
    
    print_info "Build artifacts:"
    find "$BUILD_DIR" -type f -executable -ls 2>/dev/null | head -10 || true
}

# Run tests with extended timeout
run_tests() {
    if [ "$ENABLE_TESTS" != "ON" ]; then
        print_info "Tests disabled, skipping test execution"
        return 0
    fi
    
    print_header "Running Tests with Extended Timeout"
    
    cd "$BUILD_DIR"
    
    local ctest_args=(
        --output-on-failure
        --parallel "$MAX_PARALLEL_JOBS"
        --timeout 1200  # 20 minutes per test
        --schedule-random
    )
    
    print_info "CTest arguments:"
    printf '  %s\n' "${ctest_args[@]}"
    
    print_info "Running tests with 20-minute timeout per test..."
    
    run_with_timeout $TEST_TIMEOUT "Unit Tests" ctest "${ctest_args[@]}"
    
    cd ..
}

# Install the project
install_project() {
    print_header "Installing Project"
    
    local install_args=(
        --build "$BUILD_DIR"
        --target install
        --config "$BUILD_TYPE"
    )
    
    print_info "Install arguments:"
    printf '  %s\n' "${install_args[@]}"
    
    run_with_timeout $INSTALL_TIMEOUT "Project Installation" cmake "${install_args[@]}"
}

# Display build statistics
display_statistics() {
    print_header "Build Statistics"
    
    if command -v ccache >/dev/null 2>&1 && [ "$ENABLE_CCACHE" = "ON" ]; then
        print_info "CCache Statistics:"
        ccache --show-stats
    fi
    
    if [ -d "$BUILD_DIR" ]; then
        print_info "Build Directory Size:"
        du -sh "$BUILD_DIR" 2>/dev/null || echo "Build directory size not available"
        
        print_info "Generated Executables:"
        find "$BUILD_DIR" -type f -executable -name "goldearn*" -ls 2>/dev/null || echo "No executables found"
    fi
    
    print_success "Build process completed successfully!"
}

# Main execution
main() {
    local start_time=$(date +%s)
    
    print_header "High-Performance Build Script with Extended Timeouts"
    print_info "Automatic timeout extension: Minimum 20 minutes per operation"
    print_info "Performance optimizations: Unity builds, precompiled headers, parallel execution"
    
    setup_build_environment
    prepare_build_directory
    configure_cmake
    build_project
    
    if [ "${RUN_TESTS:-true}" = "true" ]; then
        run_tests
    fi
    
    if [ "${INSTALL_PROJECT:-false}" = "true" ]; then
        install_project
    fi
    
    display_statistics
    
    local end_time=$(date +%s)
    local total_duration=$((end_time - start_time))
    
    print_success "Total build process completed in ${total_duration} seconds ($((total_duration/60))min $((total_duration%60))s)"
}

# Handle script interruption
trap 'print_error "Build interrupted"; exit 130' INT TERM

# Execute main function
main "$@"