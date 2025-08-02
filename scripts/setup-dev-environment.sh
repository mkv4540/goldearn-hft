#!/bin/bash

# GoldEarn HFT Development Environment Setup
set -e

echo "🚀 Setting up GoldEarn HFT Development Environment"
echo "=================================================="

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    echo "❌ Docker is required but not installed."
    echo "📖 Please install Docker Desktop from: https://www.docker.com/products/docker-desktop"
    exit 1
fi

if ! command -v docker-compose &> /dev/null; then
    echo "❌ Docker Compose is required but not installed."
    echo "📖 Please install Docker Compose from: https://docs.docker.com/compose/install/"
    exit 1
fi

echo "✅ Docker and Docker Compose are available"

# Create necessary directories
echo "📁 Creating development directories..."
mkdir -p {build,test-results,perf-results,logs,data,config}
mkdir -p scripts

# Create development scripts
echo "📝 Creating development scripts..."

# Create test runner script
cat > scripts/run-tests.sh << 'EOF'
#!/bin/bash
set -e

echo "🧪 Running comprehensive test suite..."

# Build tests
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON
make -j$(nproc)

# Run tests with coverage
echo "Running unit tests..."
ctest --output-on-failure --parallel $(nproc)

# Generate coverage report
if command -v gcov &> /dev/null; then
    echo "Generating coverage report..."
    gcov -r ../ $(find . -name "*.gcda")
fi

echo "✅ Tests completed successfully!"
EOF

# Create benchmark runner script
cat > scripts/run-benchmarks.sh << 'EOF'
#!/bin/bash
set -e

echo "⚡ Running performance benchmarks..."

# Build benchmarks
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
make -j$(nproc)

# Run benchmarks
echo "Running latency benchmarks..."
./goldearn_latency_benchmark --benchmark_format=json --benchmark_out=../perf-results/latency.json

echo "Running throughput benchmarks..."
./goldearn_throughput_benchmark --benchmark_format=json --benchmark_out=../perf-results/throughput.json

echo "Running order book benchmarks..."
./goldearn_orderbook_benchmark --benchmark_format=json --benchmark_out=../perf-results/orderbook.json

echo "✅ Benchmarks completed successfully!"
EOF

# Create development server script
cat > scripts/dev-server.sh << 'EOF'
#!/bin/bash
set -e

echo "🔧 Starting development server..."

# Build in debug mode
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
make -j$(nproc)

# Start development services
echo "Starting HFT engine in development mode..."
./goldearn_engine --config=../config/development.conf --log-level=DEBUG &

echo "Starting web dashboard..."
./goldearn_dashboard --port=8080 &

echo "Starting metrics server..."
./goldearn_metrics --port=9090 &

# Keep running
wait
EOF

# Make scripts executable
chmod +x scripts/*.sh

# Create configuration files
echo "⚙️  Creating configuration files..."

cat > config/development.conf << 'EOF'
# GoldEarn HFT Development Configuration

[system]
log_level = DEBUG
log_file = logs/goldearn-dev.log
enable_monitoring = true
monitoring_port = 9090

[market_data]
nse_host = localhost
nse_port = 9999
enable_simulation = true
simulation_file = data/market_data_sample.csv

[trading]
enable_paper_trading = true
initial_capital = 1000000.0
max_position_size = 100000.0

[risk]
enable_risk_checks = true
max_daily_loss = 50000.0
max_exposure = 500000.0

[database]
redis_host = redis
redis_port = 6379
postgres_host = postgres
postgres_port = 5432
postgres_db = goldearn_hft
postgres_user = hftuser
postgres_password = hft_dev_password
EOF

cat > config/production.conf << 'EOF'
# GoldEarn HFT Production Configuration

[system]
log_level = INFO
log_file = logs/goldearn-prod.log
enable_monitoring = true
monitoring_port = 9090

[market_data]
nse_host = nse-feed.example.com
nse_port = 9899
enable_simulation = false
connection_timeout = 5000
heartbeat_interval = 30000

[trading]
enable_paper_trading = false
initial_capital = 25000000.0
max_position_size = 1000000.0

[risk]
enable_risk_checks = true
max_daily_loss = 1000000.0
max_exposure = 10000000.0
max_var_1d = 500000.0

[database]
redis_host = prod-redis.internal
redis_port = 6379
postgres_host = prod-postgres.internal
postgres_port = 5432
clickhouse_host = prod-clickhouse.internal
clickhouse_port = 9000
EOF

# Create database initialization script
cat > scripts/init-db.sql << 'EOF'
-- GoldEarn HFT Database Schema

CREATE SCHEMA IF NOT EXISTS hft;

-- Trades table
CREATE TABLE IF NOT EXISTS hft.trades (
    id BIGSERIAL PRIMARY KEY,
    symbol_id BIGINT NOT NULL,
    trade_id BIGINT NOT NULL,
    price DECIMAL(12,4) NOT NULL,
    quantity BIGINT NOT NULL,
    side CHAR(1) NOT NULL,
    timestamp TIMESTAMP WITH TIME ZONE NOT NULL,
    strategy_id VARCHAR(50),
    execution_venue VARCHAR(20)
);

-- Orders table
CREATE TABLE IF NOT EXISTS hft.orders (
    id BIGSERIAL PRIMARY KEY,
    client_order_id BIGINT NOT NULL,
    symbol_id BIGINT NOT NULL,
    order_type VARCHAR(10) NOT NULL,
    side CHAR(1) NOT NULL,
    price DECIMAL(12,4),
    quantity BIGINT NOT NULL,
    filled_quantity BIGINT DEFAULT 0,
    status VARCHAR(20) NOT NULL,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Positions table
CREATE TABLE IF NOT EXISTS hft.positions (
    id BIGSERIAL PRIMARY KEY,
    strategy_id VARCHAR(50) NOT NULL,
    symbol_id BIGINT NOT NULL,
    quantity BIGINT NOT NULL,
    avg_price DECIMAL(12,4) NOT NULL,
    unrealized_pnl DECIMAL(12,4) DEFAULT 0,
    realized_pnl DECIMAL(12,4) DEFAULT 0,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    UNIQUE(strategy_id, symbol_id)
);

-- Performance metrics table
CREATE TABLE IF NOT EXISTS hft.metrics (
    id BIGSERIAL PRIMARY KEY,
    metric_name VARCHAR(100) NOT NULL,
    metric_value DECIMAL(12,4) NOT NULL,
    component VARCHAR(50) NOT NULL,
    timestamp TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Create indices for performance
CREATE INDEX IF NOT EXISTS idx_trades_symbol_timestamp ON hft.trades(symbol_id, timestamp);
CREATE INDEX IF NOT EXISTS idx_orders_status_created ON hft.orders(status, created_at);
CREATE INDEX IF NOT EXISTS idx_positions_strategy ON hft.positions(strategy_id);
CREATE INDEX IF NOT EXISTS idx_metrics_component_timestamp ON hft.metrics(component, timestamp);
EOF

# Build Docker environment
echo "🐳 Building Docker development environment..."
docker-compose build --parallel

echo ""
echo "✅ Development environment setup complete!"
echo ""
echo "🚀 Quick Start Commands:"
echo "  Start development environment: docker-compose up -d"
echo "  Run tests:                     docker-compose run goldearn-test"
echo "  Run benchmarks:                docker-compose run goldearn-perf"
echo "  Access development container:  docker-compose exec goldearn-dev bash"
echo "  View logs:                     docker-compose logs -f goldearn-build"
echo "  Stop environment:              docker-compose down"
echo ""
echo "📊 Services will be available at:"
echo "  Web Dashboard:    http://localhost:8080"
echo "  Metrics API:      http://localhost:9090"
echo "  PostgreSQL:       localhost:5432"
echo "  Redis:            localhost:6379"
echo "  ClickHouse:       localhost:8123"
echo ""
echo "🎯 Ready for HFT development!"
EOF

chmod +x scripts/setup-dev-environment.sh