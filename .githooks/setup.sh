#!/bin/sh
# Run this once after cloning the repo to install git hooks
git config core.hooksPath .githooks
echo "✅ Git hooks installed. Direct pushes to main/master are now blocked."
