#!/bin/bash

# Git Timeout Optimizer Script
# Automatically configures git with optimized timeout and performance settings
# Prevents timeout issues during git operations

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Git Timeout Optimizer ===${NC}"
echo "Configuring git with optimized timeout and performance settings..."

# Function to set git config with error handling
set_git_config() {
    local scope="$1"
    local key="$2"
    local value="$3"
    local description="$4"
    
    echo -e "${YELLOW}Setting ${scope} ${key} = ${value}${NC}"
    echo "  Description: ${description}"
    
    if git config ${scope} "${key}" "${value}"; then
        echo -e "${GREEN}  ✓ Successfully set ${key}${NC}"
    else
        echo -e "${RED}  ✗ Failed to set ${key}${NC}"
        return 1
    fi
}

# Core timeout and performance configurations
echo -e "\n${BLUE}=== Core Timeout Settings ===${NC}"

# HTTP settings for network operations
set_git_config "--global" "http.postBuffer" "2147483648" "Maximum HTTP POST buffer size (2GB)"
set_git_config "--global" "http.lowSpeedLimit" "0" "Disable HTTP low speed limit to prevent timeouts"
set_git_config "--global" "http.lowSpeedTime" "0" "Disable HTTP low speed timeout"
set_git_config "--global" "http.timeout" "0" "Disable HTTP timeout completely"

# Core performance optimizations
echo -e "\n${BLUE}=== Performance Optimizations ===${NC}"
set_git_config "--global" "core.preloadindex" "true" "Preload index for faster operations"
set_git_config "--global" "core.fscache" "true" "Enable filesystem cache"
set_git_config "--global" "core.untrackedCache" "true" "Cache untracked files for speed"
set_git_config "--global" "core.commitGraph" "true" "Enable commit graph for faster operations"
set_git_config "--global" "core.multiPackIndex" "true" "Enable multi-pack index"

# Packing and compression optimizations
echo -e "\n${BLUE}=== Packing Optimizations ===${NC}"
set_git_config "--global" "pack.threads" "0" "Use all available cores for packing"
set_git_config "--global" "pack.deltaCacheSize" "2047m" "Large delta cache for better compression"
set_git_config "--global" "pack.windowMemory" "1g" "Memory limit for pack window"

# Index and status optimizations
echo -e "\n${BLUE}=== Index Optimizations ===${NC}"
set_git_config "--global" "status.submoduleSummary" "false" "Disable submodule summary for speed"
set_git_config "--global" "diff.renames" "false" "Disable rename detection for speed"
set_git_config "--global" "index.threads" "0" "Use all cores for index operations"

# Network and transfer optimizations
echo -e "\n${BLUE}=== Network Optimizations ===${NC}"
set_git_config "--global" "transfer.unpackLimit" "1" "Always use index-pack for better performance"
set_git_config "--global" "fetch.parallel" "0" "Enable parallel fetching"
set_git_config "--global" "submodule.fetchJobs" "0" "Parallel submodule operations"

# Maintenance and cleanup settings
echo -e "\n${BLUE}=== Maintenance Settings ===${NC}"
set_git_config "--global" "gc.auto" "0" "Disable automatic garbage collection"
set_git_config "--global" "gc.autoDetach" "false" "Don't detach gc process"

# Local repository optimizations (if in a git repo)
if git rev-parse --git-dir > /dev/null 2>&1; then
    echo -e "\n${BLUE}=== Local Repository Optimizations ===${NC}"
    
    # Enable maintenance for current repository
    echo -e "${YELLOW}Enabling git maintenance for current repository...${NC}"
    if git maintenance register 2>/dev/null; then
        echo -e "${GREEN}  ✓ Repository registered for maintenance${NC}"
    else
        echo -e "${YELLOW}  ! Maintenance registration skipped (may already be registered)${NC}"
    fi
    
    # Run immediate optimization
    echo -e "${YELLOW}Running immediate repository optimization...${NC}"
    if timeout 300 git gc --aggressive --prune=now 2>/dev/null; then
        echo -e "${GREEN}  ✓ Repository optimization completed${NC}"
    else
        echo -e "${YELLOW}  ! Repository optimization skipped or timed out${NC}"
    fi
else
    echo -e "${YELLOW}Not in a git repository - skipping local optimizations${NC}"
fi

# Create git wrapper functions
echo -e "\n${BLUE}=== Creating Git Wrapper Functions ===${NC}"

WRAPPER_DIR="$HOME/.git_wrappers"
mkdir -p "$WRAPPER_DIR"

# Create optimized git command wrapper
cat > "$WRAPPER_DIR/git_optimized" << 'EOF'
#!/bin/bash

# Optimized git wrapper with automatic timeout handling
# Usage: git_optimized <git-command> [args...]

set -euo pipefail

# Default timeout of 20 minutes (1200 seconds)
DEFAULT_TIMEOUT=1200
TIMEOUT=${GIT_TIMEOUT:-$DEFAULT_TIMEOUT}

# Function to run git with timeout and retries
run_git_with_timeout() {
    local cmd="$*"
    local retries=3
    local retry_count=0
    
    while [ $retry_count -lt $retries ]; do
        echo "Running: git $cmd (attempt $((retry_count + 1))/$retries, timeout: ${TIMEOUT}s)"
        
        if timeout "$TIMEOUT" git "$@"; then
            return 0
        else
            local exit_code=$?
            retry_count=$((retry_count + 1))
            
            if [ $retry_count -lt $retries ]; then
                echo "Command failed with exit code $exit_code, retrying in 5 seconds..."
                sleep 5
            else
                echo "Command failed after $retries attempts"
                return $exit_code
            fi
        fi
    done
}

# Run the git command with optimizations
run_git_with_timeout "$@"
EOF

chmod +x "$WRAPPER_DIR/git_optimized"

# Create quick git aliases
echo -e "${YELLOW}Creating optimized git aliases...${NC}"
git config --global alias.quick-status '!timeout 300 git status --porcelain'
git config --global alias.quick-log '!timeout 300 git log --oneline -10'
git config --global alias.quick-add '!timeout 600 git add'
git config --global alias.quick-commit '!timeout 600 git commit'
git config --global alias.quick-push '!timeout 1200 git push'
git config --global alias.quick-pull '!timeout 1200 git pull'

echo -e "${GREEN}  ✓ Git aliases created${NC}"

# Verify configuration
echo -e "\n${BLUE}=== Verifying Configuration ===${NC}"
echo "Current timeout-related git configuration:"
git config --global --get-regexp "http\." | head -10 || true
git config --global --get-regexp "core\." | head -10 || true

echo -e "\n${GREEN}=== Git Timeout Optimization Complete ===${NC}"
echo -e "${YELLOW}Key improvements:${NC}"
echo "  • HTTP timeouts disabled to prevent network timeouts"
echo "  • Large buffers configured for large repository operations"
echo "  • Performance optimizations enabled for faster operations"
echo "  • Parallel processing enabled where possible"
echo "  • Optimized git wrapper created at $WRAPPER_DIR/git_optimized"
echo "  • Quick git aliases created (git quick-status, quick-commit, etc.)"

echo -e "\n${BLUE}Usage recommendations:${NC}"
echo "  • Use 'git quick-<command>' for common operations with built-in timeouts"
echo "  • Set GIT_TIMEOUT environment variable to override default timeout"
echo "  • Run this script periodically to maintain optimizations"

echo -e "\n${GREEN}Git timeout issues should now be resolved!${NC}"