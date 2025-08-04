#!/bin/bash

echo "🔍 Verifying GoldEarn HFT Build"
echo "================================"

# Check if executables exist
echo ""
echo "📦 Checking main executables:"
executables=("goldearn_engine" "goldearn_feed_handler" "goldearn_risk_monitor")
for exe in "${executables[@]}"; do
    if [ -f "build/$exe" ]; then
        echo "  ✅ $exe - Found"
        # Check if it's executable
        if [ -x "build/$exe" ]; then
            echo "     └─ Executable: Yes"
        else
            echo "     └─ Executable: No"
        fi
    else
        echo "  ❌ $exe - Not found"
    fi
done

echo ""
echo "🧪 Checking test executables:"
tests=("test_config" "test_core" "test_market_data" "test_trading" "test_risk" "test_auth" "test_monitoring" "test_security" "test_integration" "test_performance")
for test in "${tests[@]}"; do
    if [ -f "build/tests/$test" ]; then
        echo "  ✅ $test - Found"
    else
        echo "  ❌ $test - Not found"
    fi
done

echo ""
echo "📚 Checking library:"
if [ -f "build/libgoldearn_core.a" ]; then
    echo "  ✅ libgoldearn_core.a - Found"
    size=$(ls -lh build/libgoldearn_core.a | awk '{print $5}')
    echo "     └─ Size: $size"
else
    echo "  ❌ libgoldearn_core.a - Not found"
fi

echo ""
echo "================================"
echo "✅ Build verification complete"

# Alternative test run approach
echo ""
echo "To run tests manually, use:"
echo "  cd build"
echo "  ./tests/test_core --help"
echo "  ./tests/test_core --gtest_list_tests"
echo "  ./tests/test_core --gtest_filter='*TestName*'"
echo ""
echo "Or run a specific test case:"
echo "  ./tests/test_config --gtest_filter='ConfigManagerTest.*'"