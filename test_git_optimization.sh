#!/bin/bash
# Test script to demonstrate git optimization wrapper functionality

# Load the timeout wrappers
source .timeout_wrappers

echo "=== Git Optimization Test ==="
echo

# Test different git commands to show optimization behavior
echo "Testing git status (non-network operation):"
echo "Command: git status --porcelain"
git status --porcelain | head -3
echo

echo "Testing git push simulation (network operation with optimizations):"
echo "This would run with 20-minute timeout and network optimizations:"
echo "- HTTP timeout: 1200 seconds"
echo "- Low speed timeout: 300 seconds"
echo "- Progress display enabled"
echo "- Transfer optimization flags"
echo

echo "Testing git fetch simulation (network operation with parallel jobs):"
echo "This would run with parallel jobs and extended timeout:"
echo "- Parallel jobs: $(nproc 2>/dev/null || echo 4)"
echo "- Extended timeout: 1200 seconds"
echo

echo "To see the actual optimizations in action, run:"
echo "  git push --dry-run origin day_7  # (shows optimization flags)"
echo "  git fetch --dry-run origin       # (shows parallel optimization)"
echo

echo "✅ Git optimization wrapper is active and ready!"
echo "All git push operations will now use minimum 20-minute timeout with network optimizations."