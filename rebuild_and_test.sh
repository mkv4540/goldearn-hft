#!/bin/bash

echo "🔨 Rebuilding GoldEarn HFT with test fixes..."
echo "============================================"

cd build

# Clean and rebuild
echo "🧹 Cleaning build..."
make clean >/dev/null 2>&1

echo "🔨 Building..."
if make -j4; then
    echo "✅ Build successful!"
else
    echo "❌ Build failed!"
    exit 1
fi

echo ""
echo "🧪 Running tests..."
echo "==================="

# Run individual tests
tests=(
    "test_config"
    "test_core" 
    "test_market_data"
    "test_trading"
    "test_risk"
    "test_monitoring"
    "test_security"
    "test_auth"
)

passed=0
failed=0

for test in "${tests[@]}"; do
    echo ""
    echo "📝 Running $test..."
    if ./tests/$test --gtest_brief=1; then
        echo "✅ $test PASSED"
        ((passed++))
    else
        echo "❌ $test FAILED"
        ((failed++))
    fi
done

echo ""
echo "============================================"
echo "📊 Test Summary:"
echo "   Total: ${#tests[@]}"
echo "   Passed: $passed"
echo "   Failed: $failed"
echo "============================================"

if [ $failed -eq 0 ]; then
    echo "🎉 All tests passed!"
    exit 0
else
    echo "⚠️  $failed tests failed"
    exit 1
fi