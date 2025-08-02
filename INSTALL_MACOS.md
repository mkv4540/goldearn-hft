# 🍎 GoldEarn HFT - macOS Installation Guide

## 🚀 Quick Installation

### **One-Command Setup:**
```bash
# Make the script executable and run it
chmod +x ./scripts/setup-macos-build-env.sh && ./scripts/setup-macos-build-env.sh
```

### **Manual Installation Steps:**

## **1. Prerequisites**

### **Install Xcode Command Line Tools:**
```bash
xcode-select --install
```
*Note: This may take 10-15 minutes to download and install*

### **Install Homebrew (if not already installed):**
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

## **2. Install Build Tools**

```bash
# Core build tools
brew install cmake make ninja ccache pkg-config llvm clang-format

# Development libraries
brew install boost openssl@3 postgresql@15 redis zeromq protobuf hiredis googletest google-benchmark

# Performance tools (optional)
brew install htop iftop nload hyperfine flamegraph valgrind
```

## **3. Configure Environment**

Add to your `~/.zshrc` or `~/.bash_profile`:

```bash
# GoldEarn HFT Environment
export GOLDEARN_HFT_ROOT="$(pwd)"

# Homebrew paths (Apple Silicon)
if [[ -f "/opt/homebrew/bin/brew" ]]; then
    eval "$(/opt/homebrew/bin/brew shellenv)"
fi

# Build optimization
export PATH="/opt/homebrew/bin:$PATH"
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
export LDFLAGS="-L/opt/homebrew/lib -L/opt/homebrew/opt/openssl@3/lib"
export CPPFLAGS="-I/opt/homebrew/include -I/opt/homebrew/opt/openssl@3/include"
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:/opt/homebrew/opt/openssl@3/lib/pkgconfig"

# Performance settings
export CMAKE_BUILD_TYPE=Release
export CMAKE_CXX_FLAGS="-O3 -march=native -DNDEBUG"
export MAKEFLAGS="-j$(sysctl -n hw.ncpu)"
```

## **4. Build the System**

```bash
# Reload environment
source ~/.zshrc  # or ~/.bash_profile

# Build GoldEarn HFT
./build-macos.sh
```

## **5. Verify Installation**

```bash
# Quick test
./quick-test.sh

# Performance check
./monitor-performance.sh

# Start development services
./services.sh start
```

## **🎯 Build Targets**

### **Available Build Commands:**

| Command | Description |
|---------|-------------|
| `./build-macos.sh` | Full optimized build with tests |
| `./quick-test.sh` | Run core component tests |
| `./monitor-performance.sh` | System performance monitoring |
| `./services.sh start` | Start PostgreSQL & Redis |
| `./services.sh stop` | Stop development services |

### **Build Artifacts:**
```
build/
├── goldearn_engine           # Main HFT trading engine
├── goldearn_feed_handler     # Market data feed handler  
├── goldearn_risk_monitor     # Risk management monitor
├── goldearn_backtest         # Backtesting engine
├── test_*                    # Unit test executables
└── goldearn_benchmark        # Performance benchmarks
```

## **⚡ Performance Optimizations**

### **Apple Silicon (M1/M2) Optimizations:**
- Native ARM64 compilation with `-mcpu=apple-m1`
- Unified memory architecture optimizations
- Hardware-accelerated cryptography

### **Intel Mac Optimizations:**
- Native x86_64 compilation with `-march=native`
- AVX2/AVX512 SIMD instructions
- Intel-specific performance tuning

### **macOS-Specific Features:**
- Grand Central Dispatch integration
- Core Foundation optimizations
- Metal Performance Shaders (GPU acceleration)
- Network framework optimizations

## **🔧 Development Workflow**

### **1. Code Development:**
```bash
# Edit source files in src/
# Build and test
./build-macos.sh

# Quick iteration
cd build && ninja && ctest
```

### **2. Performance Testing:**
```bash
# Run benchmarks
cd build
./goldearn_benchmark

# Profile with Instruments
instruments -t "Time Profiler" ./goldearn_engine
```

### **3. Memory Analysis:**
```bash
# Check for leaks
leaks --atExit -- ./goldearn_engine

# Memory profiling with Instruments
instruments -t "Allocations" ./goldearn_engine
```

## **🐛 Troubleshooting**

### **Common Issues:**

**1. CMake can't find libraries:**
```bash
# Ensure Homebrew paths are set
echo $PKG_CONFIG_PATH
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig"
```

**2. Compilation errors:**
```bash
# Clear build cache
rm -rf build CMakeCache.txt
./build-macos.sh
```

**3. Test failures:**
```bash
# Run tests individually
cd build
./test_latency_tracker --gtest_verbose
```

**4. Permission errors:**
```bash
# Fix script permissions
find . -name "*.sh" -exec chmod +x {} \;
```

### **Performance Issues:**

**1. Slow build times:**
```bash
# Enable ccache
export PATH="/opt/homebrew/opt/ccache/libexec:$PATH"
ccache --set-config=max_size=5G
```

**2. Runtime performance:**
```bash
# Check CPU governor
pmset -g therm
# Disable Turbo Boost throttling if needed
sudo pmset -a lowpowermode 0
```

## **📊 System Requirements**

### **Minimum:**
- macOS 12.0 (Monterey) or later
- 8GB RAM
- 10GB free disk space
- Intel or Apple Silicon processor

### **Recommended:**
- macOS 13.0 (Ventura) or later
- 16GB+ RAM
- 50GB+ free disk space (SSD recommended)
- Apple Silicon M1/M2 for best performance

### **For Production:**
- macOS 14.0 (Sonoma) or later  
- 32GB+ RAM
- NVMe SSD storage
- 10GbE network interface
- Dedicated trading machine

## **🎯 Next Steps**

Once installation is complete:

1. **Review the architecture**: Check `docs/01-system-architecture.md`
2. **Run the test suite**: `./quick-test.sh`
3. **Start development**: Edit source files in `src/`
4. **Deploy for testing**: Follow `docs/09-deployment.md`

## **📞 Support**

**Installation Issues:**
- Check the build logs in `build/CMakeFiles/CMakeError.log`
- Ensure all Homebrew packages are installed correctly
- Verify Xcode Command Line Tools: `xcode-select -p`

**Performance Issues:**  
- Run the performance monitor: `./monitor-performance.sh`
- Check system resources with Activity Monitor
- Profile with Instruments for detailed analysis

**Happy Trading! 🚀📈**