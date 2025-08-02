#!/bin/bash

# GoldEarn HFT macOS Build Environment Setup
set -e

echo "🍎 Setting up GoldEarn HFT Build Environment for macOS"
echo "====================================================="

# Check if we're on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    echo "❌ This script is for macOS only!"
    exit 1
fi

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check for Xcode Command Line Tools
echo "🔨 Checking for Xcode Command Line Tools..."
if ! command_exists xcode-select || ! xcode-select -p &> /dev/null; then
    echo "📱 Installing Xcode Command Line Tools..."
    xcode-select --install
    echo "⏳ Please complete the Xcode Command Line Tools installation and run this script again."
    echo "   This may take several minutes..."
    exit 1
else
    echo "✅ Xcode Command Line Tools are installed"
fi

# Check for Homebrew
echo "🍺 Checking for Homebrew..."
if ! command_exists brew; then
    echo "📦 Installing Homebrew..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    
    # Add Homebrew to PATH for Apple Silicon Macs
    if [[ -f "/opt/homebrew/bin/brew" ]]; then
        echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
        eval "$(/opt/homebrew/bin/brew shellenv)"
    fi
else
    echo "✅ Homebrew is installed"
    echo "🔄 Updating Homebrew..."
    brew update
fi

# Install build tools
echo "🔧 Installing build tools..."
brew_packages=(
    "cmake"
    "make"
    "ninja"
    "ccache"
    "pkg-config"
    "llvm"
    "clang-format"
    "valgrind"
)

for package in "${brew_packages[@]}"; do
    if brew list "$package" &>/dev/null; then
        echo "✅ $package is already installed"
    else
        echo "📦 Installing $package..."
        brew install "$package"
    fi
done

# Install development libraries
echo "📚 Installing development libraries..."
dev_packages=(
    "boost"
    "openssl@3"
    "postgresql@15"
    "redis"
    "zeromq"
    "protobuf"
    "hiredis"
    "numa"
    "googletest"
    "google-benchmark"
)

for package in "${dev_packages[@]}"; do
    if brew list "$package" &>/dev/null; then
        echo "✅ $package is already installed"
    else
        echo "📦 Installing $package..."
        brew install "$package"
    fi
done

# Install optional performance tools
echo "⚡ Installing performance tools..."
perf_packages=(
    "htop"
    "iftop"
    "nload"
    "hyperfine"
    "flamegraph"
)

for package in "${perf_packages[@]}"; do
    if brew list "$package" &>/dev/null; then
        echo "✅ $package is already installed"
    else
        echo "📦 Installing $package..."
        brew install "$package" || echo "⚠️  Failed to install $package (optional)"
    fi
done

# Set up environment variables
echo "🌍 Setting up environment variables..."

# Create/update shell profile
if [[ -f ~/.zshrc ]]; then
    SHELL_RC=~/.zshrc
elif [[ -f ~/.bash_profile ]]; then
    SHELL_RC=~/.bash_profile
else
    SHELL_RC=~/.zshrc
    touch "$SHELL_RC"
fi

# Add environment setup
cat >> "$SHELL_RC" << 'EOF'

# GoldEarn HFT Build Environment
export GOLDEARN_HFT_ROOT="$HOME/Desktop/gitlocal/api_python3/goldearn/goldearn"

# Homebrew environment (for Apple Silicon)
if [[ -f "/opt/homebrew/bin/brew" ]]; then
    eval "$(/opt/homebrew/bin/brew shellenv)"
fi

# Build tool paths
export PATH="/opt/homebrew/bin:$PATH"
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"

# Library paths for linking
export LDFLAGS="-L/opt/homebrew/lib -L/opt/homebrew/opt/openssl@3/lib -L/opt/homebrew/opt/postgresql@15/lib"
export CPPFLAGS="-I/opt/homebrew/include -I/opt/homebrew/opt/openssl@3/include -I/opt/homebrew/opt/postgresql@15/include"
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:/opt/homebrew/opt/openssl@3/lib/pkgconfig:/opt/homebrew/opt/postgresql@15/lib/pkgconfig"

# Performance optimizations for macOS
export CMAKE_BUILD_TYPE=Release
export CMAKE_CXX_FLAGS="-O3 -march=native -mtune=native -DNDEBUG"
export MAKEFLAGS="-j$(sysctl -n hw.ncpu)"

# ccache for faster builds
export PATH="/opt/homebrew/opt/ccache/libexec:$PATH"
export CCACHE_DIR="$HOME/.ccache"
export CCACHE_MAXSIZE="5G"

# HFT-specific optimizations
export MALLOC_CONF="background_thread:true,metadata_thp:auto,dirty_decay_ms:30000,muzzy_decay_ms:30000"

EOF

# Source the updated profile
source "$SHELL_RC"

# Configure ccache
echo "🚀 Configuring ccache for faster builds..."
ccache --set-config=max_size=5G
ccache --set-config=compression=true
ccache --set-config=compression_level=6

# Create optimized build script for macOS
echo "📝 Creating macOS-optimized build script..."
cat > build-macos.sh << 'EOF'
#!/bin/bash

# GoldEarn HFT macOS Build Script
set -e

echo "🍎 Building GoldEarn HFT for macOS..."
echo "=================================="

# Detect architecture
ARCH=$(uname -m)
echo "🔍 Detected architecture: $ARCH"

# Set architecture-specific flags
if [[ "$ARCH" == "arm64" ]]; then
    # Apple Silicon optimizations
    ARCH_FLAGS="-mcpu=apple-m1"
    echo "🚀 Using Apple Silicon optimizations"
elif [[ "$ARCH" == "x86_64" ]]; then
    # Intel optimizations
    ARCH_FLAGS="-march=native -mtune=native"
    echo "🚀 Using Intel optimizations"
else
    ARCH_FLAGS=""
    echo "⚠️  Unknown architecture, using default flags"
fi

# Clean and create build directory
echo "📁 Preparing build directory..."
rm -rf build
mkdir -p build
cd build

# Configure with CMake
echo "⚙️  Configuring with CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=ON \
    -DBUILD_BENCHMARKS=ON \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_FLAGS="-O3 $ARCH_FLAGS -DNDEBUG -stdlib=libc++" \
    -DCMAKE_PREFIX_PATH="/opt/homebrew" \
    -DOPENSSL_ROOT_DIR="/opt/homebrew/opt/openssl@3" \
    -DPostgreSQL_ROOT="/opt/homebrew/opt/postgresql@15" \
    -DBOOST_ROOT="/opt/homebrew" \
    -GNinja

# Build with all available cores
echo "🔨 Building with Ninja ($(sysctl -n hw.ncpu) cores)..."
ninja -j$(sysctl -n hw.ncpu)

# Run tests
echo "🧪 Running test suite..."
ctest --output-on-failure --parallel $(sysctl -n hw.ncpu)

# Run benchmarks
echo "⚡ Running performance benchmarks..."
if [[ -f "./goldearn_benchmark" ]]; then
    ./goldearn_benchmark --benchmark_format=console
fi

echo ""
echo "✅ Build completed successfully!"
echo "📊 Build Summary:"
echo "   - Architecture: $ARCH"
echo "   - Compiler: $(clang++ --version | head -n1)"
echo "   - Build Type: Release (optimized)"
echo "   - Tests: Passed"
echo ""
echo "🎯 Ready for HFT deployment on macOS!"
EOF

chmod +x build-macos.sh

# Create development utilities
echo "🛠️  Creating development utilities..."

# Performance monitoring script
cat > monitor-performance.sh << 'EOF'
#!/bin/bash

echo "📊 GoldEarn HFT Performance Monitor"
echo "=================================="

echo "💻 System Information:"
echo "  CPU: $(sysctl -n machdep.cpu.brand_string)"
echo "  Cores: $(sysctl -n hw.ncpu)"
echo "  Memory: $(echo "scale=2; $(sysctl -n hw.memsize) / 1024 / 1024 / 1024" | bc)GB"
echo "  Architecture: $(uname -m)"

echo ""
echo "🔥 CPU Usage:"
top -l 1 -n 0 | grep "CPU usage"

echo ""
echo "💾 Memory Usage:"
vm_stat | grep -E "(free|inactive|active|wired)"

echo ""
echo "📈 HFT Process Monitoring:"
if pgrep goldearn > /dev/null; then
    echo "✅ GoldEarn HFT processes running:"
    ps aux | grep goldearn | grep -v grep
    
    echo ""
    echo "📊 Process Resource Usage:"
    for pid in $(pgrep goldearn); do
        echo "  PID $pid: $(ps -p $pid -o %cpu,%mem,time | tail -n 1)"
    done
else
    echo "❌ No GoldEarn HFT processes running"
fi

echo ""
echo "🌐 Network Connections:"
netstat -an | grep ESTABLISHED | grep -E "(9899|6379|5432)" | wc -l | xargs echo "  Active connections:"

echo ""
echo "💿 Disk I/O:"
iostat -d 1 1 | tail -n +3
EOF

chmod +x monitor-performance.sh

# Create quick test script
cat > quick-test.sh << 'EOF'
#!/bin/bash

echo "🧪 GoldEarn HFT Quick Test"
echo "========================="

if [[ ! -d "build" ]]; then
    echo "❌ Build directory not found. Run ./build-macos.sh first."
    exit 1
fi

cd build

echo "🚀 Running core component tests..."

# Test individual components
echo "  ⚡ Testing LatencyTracker..."
if [[ -f "./test_latency_tracker" ]]; then
    ./test_latency_tracker --gtest_brief=1
else
    echo "     ⚠️  LatencyTracker test not found"
fi

echo "  📊 Testing OrderBook..."
if [[ -f "./test_order_book" ]]; then
    ./test_order_book --gtest_brief=1
else
    echo "     ⚠️  OrderBook test not found"
fi

echo "  🔒 Testing NSE Protocol..."
if [[ -f "./test_nse_protocol" ]]; then
    ./test_nse_protocol --gtest_brief=1
else
    echo "     ⚠️  NSE Protocol test not found"
fi

echo ""
echo "⚡ Performance Quick Check..."
if [[ -f "./goldearn_benchmark" ]]; then
    ./goldearn_benchmark --benchmark_filter=".*Latency.*" --benchmark_min_time=1
else
    echo "   ⚠️  Benchmarks not available"
fi

echo ""
echo "✅ Quick test completed!"
EOF

chmod +x quick-test.sh

# Services management script
cat > services.sh << 'EOF'
#!/bin/bash

case "$1" in
    start)
        echo "🚀 Starting development services..."
        brew services start postgresql@15
        brew services start redis
        echo "✅ Services started"
        ;;
    stop)
        echo "🛑 Stopping development services..."
        brew services stop postgresql@15
        brew services stop redis
        echo "✅ Services stopped"
        ;;
    status)
        echo "📊 Service Status:"
        echo "  PostgreSQL: $(brew services list | grep postgresql@15 | awk '{print $2}')"
        echo "  Redis: $(brew services list | grep redis | awk '{print $2}')"
        ;;
    *)
        echo "Usage: $0 {start|stop|status}"
        exit 1
        ;;
esac
EOF

chmod +x services.sh

echo ""
echo "✅ macOS Build Environment Setup Complete!"
echo ""
echo "🚀 Next Steps:"
echo "  1. Restart your terminal (or run: source $SHELL_RC)"
echo "  2. Navigate to the project directory"
echo "  3. Run: ./build-macos.sh"
echo ""
echo "🛠️  Available Commands:"
echo "  ./build-macos.sh          - Build the complete HFT system"
echo "  ./quick-test.sh           - Run quick component tests"
echo "  ./monitor-performance.sh  - Monitor system performance"
echo "  ./services.sh start       - Start development services"
echo "  ./services.sh stop        - Stop development services"
echo ""
echo "📊 Development Services:"
echo "  PostgreSQL: Port 5432 (brew services start postgresql@15)"
echo "  Redis: Port 6379 (brew services start redis)"
echo ""
echo "🎯 Ready for high-frequency trading development on macOS!"
echo ""
echo "⚠️  Note: Please restart your terminal for environment changes to take effect."