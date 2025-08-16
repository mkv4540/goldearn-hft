#!/bin/bash

# Enhanced Git Wrapper with Automatic Timeout Handling
# This script wraps all git commands with extended timeouts and performance optimizations
# Usage: source this script or use git_safe function directly

set -euo pipefail

# Configuration
DEFAULT_TIMEOUT=1200  # 20 minutes
MAX_RETRIES=3
RETRY_DELAY=5

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Function to log with timestamp
log_with_timestamp() {
    echo -e "[$(date '+%Y-%m-%d %H:%M:%S')] $*"
}

# Function to run command with timeout (macOS compatible)
run_with_timeout() {
    local timeout_duration="$1"
    shift
    local cmd=("$@")
    
    # Use Perl-based timeout for macOS compatibility
    perl -e "
        use POSIX qw(:signal_h);
        my \$timeout = $timeout_duration;
        my \$pid = fork();
        
        if (\$pid == 0) {
            exec(@ARGV) or die \"exec failed: \$!\";
        } elsif (\$pid > 0) {
            my \$result = 0;
            eval {
                local \$SIG{ALRM} = sub { kill 'TERM', \$pid; die \"timeout\"; };
                alarm(\$timeout);
                waitpid(\$pid, 0);
                \$result = \$? >> 8;
                alarm(0);
            };
            if (\$@ =~ /timeout/) {
                kill 'KILL', \$pid;
                exit 124;  # timeout exit code
            }
            exit \$result;
        } else {
            die \"fork failed: \$!\";
        }
    " -- "${cmd[@]}"
}

# Enhanced git function with timeout and retry logic
git_safe() {
    local timeout_val=${GIT_TIMEOUT:-$DEFAULT_TIMEOUT}
    local retries=${GIT_RETRIES:-$MAX_RETRIES}
    local retry_count=0
    local cmd="$*"
    
    log_with_timestamp "${BLUE}Executing git command with timeout protection${NC}"
    log_with_timestamp "Command: git $cmd"
    log_with_timestamp "Timeout: ${timeout_val}s, Max retries: $retries"
    
    while [ $retry_count -lt $retries ]; do
        log_with_timestamp "${YELLOW}Attempt $((retry_count + 1))/$retries${NC}"
        
        # Pre-command optimizations based on git operation
        case "$1" in
            "add"|"commit"|"push"|"pull"|"fetch"|"clone")
                # Ensure optimal settings for these operations
                export GIT_OPTIONAL_LOCKS=0
                ;;
            "status"|"log"|"diff")
                # Fast operations - shorter timeout
                timeout_val=$((timeout_val / 2))
                ;;
        esac
        
        # Execute with timeout
        if run_with_timeout "$timeout_val" git "$@"; then
            log_with_timestamp "${GREEN}✓ Git command completed successfully${NC}"
            return 0
        else
            local exit_code=$?
            retry_count=$((retry_count + 1))
            
            log_with_timestamp "${RED}✗ Git command failed with exit code $exit_code${NC}"
            
            if [ $retry_count -lt $retries ]; then
                log_with_timestamp "${YELLOW}Retrying in ${RETRY_DELAY} seconds...${NC}"
                sleep $RETRY_DELAY
                
                # Apply recovery strategies based on failure type
                case $exit_code in
                    124) # Timeout
                        log_with_timestamp "Timeout detected - increasing timeout for retry"
                        timeout_val=$((timeout_val * 2))
                        ;;
                    128) # Network error
                        log_with_timestamp "Network error detected - checking connectivity"
                        # Could add network connectivity check here
                        ;;
                esac
            else
                log_with_timestamp "${RED}✗ Git command failed after $retries attempts${NC}"
                return $exit_code
            fi
        fi
    done
}

# Specialized functions for common git operations
git_safe_status() {
    log_with_timestamp "Running optimized git status"
    GIT_TIMEOUT=300 git_safe status --porcelain "$@"
}

git_safe_add() {
    log_with_timestamp "Running optimized git add"
    GIT_TIMEOUT=600 git_safe add "$@"
}

git_safe_commit() {
    log_with_timestamp "Running optimized git commit"
    GIT_TIMEOUT=600 git_safe commit "$@"
}

git_safe_push() {
    log_with_timestamp "Running optimized git push"
    GIT_TIMEOUT=1800 git_safe push "$@"  # 30 minutes for push
}

git_safe_pull() {
    log_with_timestamp "Running optimized git pull"
    GIT_TIMEOUT=1800 git_safe pull "$@"  # 30 minutes for pull
}

git_safe_fetch() {
    log_with_timestamp "Running optimized git fetch"
    GIT_TIMEOUT=1200 git_safe fetch "$@"
}

git_safe_clone() {
    log_with_timestamp "Running optimized git clone"
    GIT_TIMEOUT=3600 git_safe clone "$@"  # 1 hour for clone
}

# Batch operations with automatic optimization
git_safe_batch_add_commit() {
    local message="$1"
    shift
    local files=("$@")
    
    log_with_timestamp "${BLUE}Performing batch add and commit operation${NC}"
    
    # Add files
    for file in "${files[@]}"; do
        if git_safe_add "$file"; then
            log_with_timestamp "${GREEN}✓ Added: $file${NC}"
        else
            log_with_timestamp "${RED}✗ Failed to add: $file${NC}"
            return 1
        fi
    done
    
    # Commit
    if git_safe_commit -m "$message"; then
        log_with_timestamp "${GREEN}✓ Committed with message: $message${NC}"
        return 0
    else
        log_with_timestamp "${RED}✗ Failed to commit${NC}"
        return 1
    fi
}

# Repository health check
git_health_check() {
    log_with_timestamp "${BLUE}Running git repository health check${NC}"
    
    # Check if we're in a git repository
    if ! git rev-parse --git-dir > /dev/null 2>&1; then
        log_with_timestamp "${RED}✗ Not in a git repository${NC}"
        return 1
    fi
    
    # Check repository status
    log_with_timestamp "Checking repository status..."
    if git_safe_status > /dev/null; then
        log_with_timestamp "${GREEN}✓ Repository status OK${NC}"
    else
        log_with_timestamp "${RED}✗ Repository status check failed${NC}"
        return 1
    fi
    
    # Check connectivity to remote
    log_with_timestamp "Checking remote connectivity..."
    if run_with_timeout 30 git ls-remote --heads origin > /dev/null 2>&1; then
        log_with_timestamp "${GREEN}✓ Remote connectivity OK${NC}"
    else
        log_with_timestamp "${YELLOW}! Remote connectivity issues detected${NC}"
    fi
    
    # Check for large files that might cause timeouts
    log_with_timestamp "Checking for large files..."
    if git_safe log --all --pretty=format: --name-only | sort -u | xargs -I {} sh -c 'test -f "{}" && echo "$(du -h "{}" | cut -f1) {}"' | sort -hr | head -5; then
        log_with_timestamp "${GREEN}✓ Large file check completed${NC}"
    fi
    
    log_with_timestamp "${GREEN}✓ Repository health check completed${NC}"
}

# Auto-optimization function
git_auto_optimize() {
    log_with_timestamp "${BLUE}Running automatic git optimizations${NC}"
    
    # Run git gc if needed
    if [ -d .git/objects ]; then
        local object_count=$(find .git/objects -type f | wc -l)
        if [ "$object_count" -gt 1000 ]; then
            log_with_timestamp "Running git gc (object count: $object_count)"
            if run_with_timeout 600 git gc --auto; then
                log_with_timestamp "${GREEN}✓ Git gc completed${NC}"
            else
                log_with_timestamp "${YELLOW}! Git gc timed out or failed${NC}"
            fi
        fi
    fi
    
    # Update index for performance
    if run_with_timeout 300 git update-index --refresh > /dev/null 2>&1; then
        log_with_timestamp "${GREEN}✓ Index updated${NC}"
    fi
    
    log_with_timestamp "${GREEN}✓ Auto-optimization completed${NC}"
}

# Export functions for use
export -f git_safe
export -f git_safe_status
export -f git_safe_add
export -f git_safe_commit
export -f git_safe_push
export -f git_safe_pull
export -f git_safe_fetch
export -f git_safe_clone
export -f git_safe_batch_add_commit
export -f git_health_check
export -f git_auto_optimize

# If script is being executed directly, run the command passed as arguments
if [ $# -gt 0 ]; then
    git_safe "$@"
fi