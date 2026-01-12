# GoldEarn HFT Build Environment
FROM ubuntu:24.04

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Install build tools and dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    make \
    g++ \
    clang \
    clang-tidy \
    clang-format \
    git \
    pkg-config \
    wget \
    curl \
    valgrind \
    gdb \
    perf-tools-unstable \
    && rm -rf /var/lib/apt/lists/*

# Install development libraries
RUN apt-get update && apt-get install -y \
    libgtest-dev \
    libgmock-dev \
    libboost-all-dev \
    libssl-dev \
    libpq-dev \
    libhiredis-dev \
    libzmq3-dev \
    libnuma-dev \
    libprotobuf-dev \
    protobuf-compiler \
    && rm -rf /var/lib/apt/lists/*

# Build and install Google Test
WORKDIR /tmp
RUN cd /usr/src/gtest && \
    cmake CMakeLists.txt && \
    make && \
    cp lib/*.a /usr/lib/ && \
    cd /usr/src/gmock && \
    cmake CMakeLists.txt && \
    make && \
    cp lib/*.a /usr/lib/

# Install Google Benchmark
RUN git clone https://github.com/google/benchmark.git && \
    cd benchmark && \
    cmake -E make_directory "build" && \
    cmake -E chdir "build" cmake -DBENCHMARK_DOWNLOAD_DEPENDENCIES=on -DCMAKE_BUILD_TYPE=Release ../ && \
    cmake --build "build" --config Release && \
    cmake --build "build" --config Release --target install

# Set up working directory
WORKDIR /app

# Copy build configuration
COPY CMakeLists.txt .
COPY build.sh .
RUN chmod +x build.sh

# Copy source code
COPY src/ src/
COPY tests/ tests/
COPY docs/ docs/

# Set up environment variables for optimal performance
ENV CMAKE_BUILD_TYPE=Release
ENV CMAKE_CXX_FLAGS="-O3 -march=native -mtune=native -DNDEBUG"
ENV OMP_NUM_THREADS=8
ENV MALLOC_CONF="background_thread:true,metadata_thp:auto"

# Build the project
RUN ./build.sh

# Create runtime user (security best practice)
RUN useradd -m -u 1000 hftuser && \
    chown -R hftuser:hftuser /app

USER hftuser

# Default command
CMD ["./build/goldearn_engine", "--config=config/production.conf"]

# Health check
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
    CMD ./build/goldearn_engine --health-check || exit 1

# Labels for documentation
LABEL maintainer="GoldEarn HFT Team <dev@goldearn.com>"
LABEL version="1.0.0"
LABEL description="High-Frequency Trading System - Build Environment"
LABEL build-date="2025-01-16"