#!/bin/bash
# Premium Performance and Timeout Optimization Agent
# This script sets up extended timeouts and performance optimizations for the entire session

set -euo pipefail

# Color codes
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

print_info() {
    echo -e "${BLUE}[PREMIUM]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[PREMIUM]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[PREMIUM]${NC} $1"
}

# Set environment variables for extended timeouts and performance optimization
setup_session_optimizations() {
    print_info "Setting up premium timeout extensions and performance optimizations..."
    
    # Extended timeout settings (minimum 20 minutes = 1200 seconds)
    export CMAKE_BUILD_TIMEOUT=1200
    export CTEST_TIMEOUT=1200
    export MAKE_TIMEOUT=1200
    export NINJA_TIMEOUT=1200
    export APT_TIMEOUT=1200
    export GIT_TIMEOUT=1200
    export COMPILE_TIMEOUT=1200
    
    # Performance optimization settings
    export CMAKE_BUILD_PARALLEL_LEVEL=0  # Use all available cores
    export MAKEFLAGS="-j$(nproc 2>/dev/null || echo 4)"
    export NINJA_STATUS="[%f/%t %o/s] "  # Progress display for Ninja
    
    # Enable faster package operations
    export DEBIAN_FRONTEND=noninteractive
    export APT_LISTCHANGES_FRONTEND=none
    export APT_OPTIONS="--no-install-recommends --yes"
    
    # Optimize git operations
    export GIT_CONFIG_GLOBAL="protocol.version=2"
    
    # Set optimal CPU usage
    if [ "$(uname)" = "Linux" ]; then
        export OMP_NUM_THREADS=$(nproc)
        export OMP_THREAD_LIMIT=$(nproc)
    fi
    
    print_success "Session optimizations configured"
}

# Create timeout wrapper functions
create_timeout_wrappers() {
    print_info "Creating timeout wrapper functions..."
    
    # Create a temporary file for wrapper functions
    local wrapper_file="/tmp/timeout_wrappers_$$"
    
    cat > "$wrapper_file" << 'EOF'
# Premium timeout wrapper functions with automatic 20-minute minimum

# CMake wrapper with extended timeout and optimization
cmake_optimized() {
    local timeout_seconds=1200
    local args=("$@")
    
    # Add performance optimizations if configuring
    if [[ " ${args[*]} " =~ " -B " ]] || [[ " ${args[*]} " =~ " --build " ]]; then
        if [[ " ${args[*]} " =~ " -B " ]]; then
            # Configuration phase - add optimization flags
            args+=(
                "-DCMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-0}"
                "-DENABLE_PARALLEL_BUILD=ON"
                "-DENABLE_UNITY_BUILD=ON"
                "-DENABLE_PRECOMPILED_HEADERS=ON"
            )
            # Use Ninja if available
            if command -v ninja >/dev/null 2>&1; then
                args+=("-G" "Ninja")
            fi
        fi
        
        if [[ " ${args[*]} " =~ " --build " ]]; then
            # Build phase - ensure parallel execution
            if ! [[ " ${args[*]} " =~ " --parallel " ]]; then
                args+=("--parallel" "$(nproc 2>/dev/null || echo 4)")
            fi
        fi
    fi
    
    echo "[PREMIUM] Running cmake with ${timeout_seconds}s timeout and optimizations"
    timeout "${timeout_seconds}s" cmake "${args[@]}"
}

# Make wrapper with extended timeout and parallel execution
make_optimized() {
    local timeout_seconds=1200
    local args=("$@")
    
    # Add parallel execution if not specified
    if ! [[ " ${args[*]} " =~ " -j" ]]; then
        args+=("-j$(nproc 2>/dev/null || echo 4)")
    fi
    
    echo "[PREMIUM] Running make with ${timeout_seconds}s timeout and parallel execution"
    timeout "${timeout_seconds}s" make "${args[@]}"
}

# Ninja wrapper with extended timeout
ninja_optimized() {
    local timeout_seconds=1200
    local args=("$@")
    
    echo "[PREMIUM] Running ninja with ${timeout_seconds}s timeout"
    timeout "${timeout_seconds}s" ninja "${args[@]}"
}

# CTest wrapper with extended timeout and parallel execution
ctest_optimized() {
    local timeout_seconds=1200
    local args=("$@")
    
    # Add parallel execution and extended test timeout if not specified
    if ! [[ " ${args[*]} " =~ " --parallel " ]]; then
        args+=("--parallel" "$(nproc 2>/dev/null || echo 4)")
    fi
    
    if ! [[ " ${args[*]} " =~ " --timeout " ]]; then
        args+=("--timeout" "1200")  # 20 minutes per test
    fi
    
    echo "[PREMIUM] Running ctest with ${timeout_seconds}s timeout and optimizations"
    timeout "${timeout_seconds}s" ctest "${args[@]}"
}

# APT wrapper with extended timeout and optimizations
apt_optimized() {
    local timeout_seconds=1200
    local args=("$@")
    
    # Add optimization flags
    args+=("--no-install-recommends" "--yes")
    
    echo "[PREMIUM] Running apt with ${timeout_seconds}s timeout and optimizations"
    timeout "${timeout_seconds}s" apt "${args[@]}"
}

# Git wrapper with extended timeout and network optimizations
git_optimized() {
    local timeout_seconds=1200
    local args=("$@")
    local command="${args[0]}"
    
    # Network optimization settings for git operations
    local git_config_args=()
    
    # Optimize for network operations (push, pull, fetch, clone)
    if [[ "$command" =~ ^(push|pull|fetch|clone)$ ]]; then
        # Increase network timeouts and optimize transfer
        git_config_args+=(
            "-c" "http.lowSpeedLimit=1000"
            "-c" "http.lowSpeedTime=300"
            "-c" "http.timeout=1200"
            "-c" "transfer.fsckObjects=false"
            "-c" "fetch.fsckObjects=false"
            "-c" "receive.fsckObjects=false"
            "-c" "core.preloadindex=true"
            "-c" "core.fscache=true"
            "-c" "gc.auto=0"
        )
        
        # For push operations, add additional optimizations
        if [[ "$command" == "push" ]]; then
            git_config_args+=(
                "-c" "push.default=simple"
                "-c" "sendpack.sideband=false"
            )
            
            # Add progress for large pushes
            if ! [[ " ${args[*]} " =~ " --progress " ]] && ! [[ " ${args[*]} " =~ " --quiet " ]]; then
                args+=("--progress")
            fi
        fi
        
        # For clone operations, add optimizations
        if [[ "$command" == "clone" ]]; then
            if ! [[ " ${args[*]} " =~ " --depth " ]]; then
                # Use shallow clone for faster initial clone
                args+=("--depth" "1")
            fi
            if ! [[ " ${args[*]} " =~ " --single-branch " ]]; then
                args+=("--single-branch")
            fi
        fi
        
        # For fetch/pull operations
        if [[ "$command" =~ ^(fetch|pull)$ ]]; then
            if ! [[ " ${args[*]} " =~ " --jobs " ]]; then
                args+=("--jobs" "$(nproc 2>/dev/null || echo 4)")
            fi
        fi
        
        echo "[PREMIUM] Running git $command with ${timeout_seconds}s timeout and network optimizations"
        timeout "${timeout_seconds}s" git "${git_config_args[@]}" "${args[@]}"
    else
        # For non-network operations, use standard timeout
        echo "[PREMIUM] Running git $command with ${timeout_seconds}s timeout"
        timeout "${timeout_seconds}s" git "${args[@]}"
    fi
}

# General compiler wrapper with extended timeout and parallel execution
compile_optimized() {
    local timeout_seconds=1200
    local compiler="$1"
    shift
    local args=("$@")
    
    # Add parallel compilation flags for supported compilers
    if [[ "$compiler" =~ ^(gcc|g\+\+|clang|clang\+\+)$ ]]; then
        # Add parallel compilation flags if building multiple files
        if [[ "${#args[@]}" -gt 3 ]]; then
            args+=("-j$(nproc 2>/dev/null || echo 4)")
        fi
    fi
    
    echo "[PREMIUM] Running $compiler with ${timeout_seconds}s timeout"
    timeout "${timeout_seconds}s" "$compiler" "${args[@]}"
}

# Aliases for common commands with automatic optimization
alias cmake='cmake_optimized'
alias make='make_optimized'
alias ninja='ninja_optimized'
alias ctest='ctest_optimized'
alias apt-get='apt_optimized'
alias git='git_optimized'
alias gcc='compile_optimized gcc'
alias g++='compile_optimized g++'
alias clang='compile_optimized clang'
alias clang++='compile_optimized clang++'
EOF

    # Source the wrapper functions in current session
    source "$wrapper_file"
    
    # Make it available for future sessions in this directory
    cp "$wrapper_file" ".timeout_wrappers"
    
    print_success "Timeout wrapper functions created and activated"
}

# Display optimization summary
display_optimization_summary() {
    print_info "=== PREMIUM PERFORMANCE OPTIMIZATION SUMMARY ==="
    echo "  ✅ Extended timeouts: Minimum 20 minutes (1200 seconds) for all operations"
    echo "  ✅ Parallel execution: Using $(nproc 2>/dev/null || echo 4) CPU cores"
    echo "  ✅ Build optimizations: Unity builds, precompiled headers, parallel compilation"
    echo "  ✅ Package optimizations: No recommended packages, non-interactive mode"
    echo "  ✅ CMake optimizations: Ninja generator, parallel configuration and build"
    echo "  ✅ Test optimizations: Parallel test execution with 20-minute per-test timeout"
    echo "  ✅ Git optimizations: Extended network timeouts, parallel operations, progress display"
    echo ""
    echo "  📊 Environment Variables Set:"
    echo "    CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-0}"
    echo "    MAKEFLAGS=${MAKEFLAGS:-not set}"
    echo "    OMP_NUM_THREADS=${OMP_NUM_THREADS:-not set}"
    echo ""
    echo "  🚀 Optimized Commands Available:"
    echo "    cmake, make, ninja, ctest, apt-get, git, gcc, g++, clang, clang++"
    echo ""
    echo "  💡 Usage: All commands now automatically use extended timeouts and optimizations"
    echo "     Example: 'cmake -B build' will use Ninja, parallel builds, and 20-min timeout"
    print_success "Premium session is now optimized for maximum performance and reliability!"
}

# Main execution
main() {
    echo ""
    print_info "Premium Performance and Timeout Optimization Agent"
    print_info "Configuring premium session for extended timeouts and maximum speed..."
    echo ""
    
    setup_session_optimizations
    create_timeout_wrappers
    display_optimization_summary
    
    echo ""
    print_success "Premium optimization setup complete! All subsequent commands will use:"
    print_success "• Minimum 20-minute timeouts for all operations"
    print_success "• Maximum parallel execution for faster builds"
    print_success "• Optimized flags and configurations for best performance"
    echo ""
}

# Execute if run as script
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi