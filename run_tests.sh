#!/bin/bash

echo "Running all HFT system tests..."
echo "=================================="

# Array of test executables
tests=(
    "test_market_data"
    "test_config" 
    "test_core"
    "test_trading"
    "test_risk"
    "test_monitoring"
    "test_security"
    "test_auth"
    "test_integration"
    "test_performance"
)

# Counters
total_tests=0
passed_tests=0
failed_tests=0

# Run each test
for test in "${tests[@]}"; do
    echo ""
    echo "Running $test..."
    echo "----------------------------------------"
    
    if [ -f "tests/$test" ]; then
        if ./tests/$test; then
            echo "✅ $test PASSED"
            ((passed_tests++))
        else
            echo "❌ $test FAILED"
            ((failed_tests++))
        fi
    else
        echo "⚠️  $test not found"
    fi
    
    ((total_tests++))
done

echo ""
echo "=================================="
echo "Test Summary:"
echo "Total tests: $total_tests"
echo "Passed: $passed_tests"
echo "Failed: $failed_tests"
echo "=================================="

if [ $failed_tests -eq 0 ]; then
    echo "🎉 All tests passed!"
    exit 0
else
    echo "⚠️  Some tests failed"
    exit 1
fi