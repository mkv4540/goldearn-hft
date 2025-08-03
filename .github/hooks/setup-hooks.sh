#!/usr/bin/env bash

# Cross-platform script to set up git hooks for C++ HFT project
# Works on both Unix/Mac and Windows (Git Bash)

# Ensure script runs in Bash
if [ -z "$BASH" ]; then
    echo "Error: This script must be run in Bash (use Git Bash on Windows)"
    exit 1
fi

# Get script directory in a cross-platform way
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
GIT_DIR="$(git rev-parse --git-dir)"

if [ $? -ne 0 ]; then
    echo "Error: Not in a git repository"
    exit 1
fi

echo "Setting up git hooks for C++ HFT project..."
echo "Script directory: $SCRIPT_DIR"
echo "Git directory: $GIT_DIR"

# Function to copy and setup a hook
setup_hook() {
    local hook_name=$1
    local source_path="$SCRIPT_DIR/$hook_name"
    local target_path="$GIT_DIR/hooks/$hook_name"
    
    echo "Setting up $hook_name..."
    
    # Check if source exists
    if [ ! -f "$source_path" ]; then
        echo "Error: $hook_name not found in $SCRIPT_DIR"
        return 1
    fi
    
    # Create hooks directory if it doesn't exist
    mkdir -p "$GIT_DIR/hooks"
    
    # Copy the hook
    cp "$source_path" "$target_path"
    if [ $? -ne 0 ]; then
        echo "Error: Failed to copy $hook_name"
        return 1
    fi
    
    # Make executable (will be ignored on Windows)
    chmod +x "$target_path" 2>/dev/null || true
    
    # Fix line endings on Windows
    if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
        # Remove carriage returns if they exist
        if command -v dos2unix >/dev/null 2>&1; then
            dos2unix "$target_path" 2>/dev/null
        else
            # Fallback if dos2unix is not available
            tr -d '\r' < "$target_path" > "$target_path.tmp" && mv "$target_path.tmp" "$target_path"
        fi
    fi
    
    echo "✓ Successfully installed $hook_name"
    return 0
}

# Setup each hook
setup_hook "pre-commit"
setup_hook "pre-push"

# Configure Git to not modify line endings for hooks
git config core.fileMode false 2>/dev/null || true

# Verify the installation
echo -e "\nVerifying hooks..."
if [ -f "$GIT_DIR/hooks/pre-commit" ] && [ -f "$GIT_DIR/hooks/pre-push" ]; then
    echo "✓ Hooks are installed successfully"
    
    # Additional Windows-specific checks
    if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
        echo -e "\nWindows-specific configuration:"
        echo "✓ Line ending conversion configured"
        echo "✓ File mode handling configured"
    fi
    
    echo -e "\nHooks installed:"
    echo "- pre-commit: Prevents commits to main branch, checks code formatting"
    echo "- pre-push: Prevents pushes to main branch, runs build checks"
    
else
    echo "⚠️  Warning: Some hooks may not have been installed correctly"
fi

echo -e "\nSetup complete! If you experience any issues:"
echo "1. Ensure you're using Git Bash on Windows"
echo "2. Try running: git config core.fileMode false"
echo "3. Check that hooks are executable: chmod +x .git/hooks/*"
echo "4. For C++ formatting, install clang-format"