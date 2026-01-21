#!/bin/bash
#
# Demonstration of running GitHub Actions workflows locally with act
#

set -e

echo "=========================================="
echo "GitHub Actions Local Testing with act"
echo "=========================================="
echo ""

# Check if act is installed
if ! command -v act &> /dev/null; then
    echo "Error: act is not installed"
    echo "Install with: curl -s https://raw.githubusercontent.com/nektos/act/master/install.sh | sudo bash"
    exit 1
fi

echo "✓ act is installed ($(act --version))"
echo ""

# Demo 1: List all workflows
echo "=========================================="
echo "Demo 1: List all workflows and jobs"
echo "=========================================="
echo ""
echo "Command: act -l"
echo ""
act -l
echo ""

# Demo 2: List jobs in specific workflow
echo "=========================================="
echo "Demo 2: List jobs in CMake workflow"
echo "=========================================="
echo ""
echo "Command: act -W .github/workflows/cmake.yaml -l"
echo ""
act -W .github/workflows/cmake.yaml -l
echo ""

# Demo 3: Dry run
echo "=========================================="
echo "Demo 3: Dry run of Autotools workflow"
echo "=========================================="
echo ""
echo "Command: act -W .github/workflows/ci.yaml -n"
echo ""
echo "This shows what would run without actually running it..."
echo "(Showing first few matrix combinations)"
echo ""
act -W .github/workflows/ci.yaml -n 2>&1 | head -30
echo ""
echo "... (output truncated) ..."
echo ""

# Demo 4: Show what running a real job would look like
echo "=========================================="
echo "Demo 4: What running a job looks like"
echo "=========================================="
echo ""
echo "To actually run a workflow job, you would use:"
echo ""
echo "  act -W .github/workflows/cmake.yaml -j build"
echo ""
echo "This would:"
echo "  1. Pull Docker images for the platform"
echo "  2. Create containers"
echo "  3. Check out code"
echo "  4. Run all build steps"
echo "  5. Run tests"
echo "  6. Clean up"
echo ""
echo "Note: Matrix builds run ONE combination at a time."
echo ""

# Demo 5: Practical usage recommendations
echo "=========================================="
echo "Practical Usage Recommendations"
echo "=========================================="
echo ""
echo "✓ Use act for:"
echo "  - Validating workflow syntax"
echo "  - Quick smoke tests"
echo "  - Debugging workflow logic"
echo ""
echo "✓ Use local test scripts for:"
echo "  - Full build verification: ./test-local.sh"
echo "  - Quick iteration: ./test-local.sh --quick"
echo "  - Platform-specific testing"
echo ""
echo "✓ Use GitHub CI for:"
echo "  - All matrix combinations"
echo "  - Cross-platform testing"
echo "  - Final validation before merge"
echo ""

echo "=========================================="
echo "Common Commands"
echo "=========================================="
echo ""
cat << 'EOF'
# List all workflows
act -l

# Dry run specific workflow
act -W .github/workflows/ci.yaml -n

# Run specific workflow
act -W .github/workflows/cmake.yaml

# Run specific job
act -W .github/workflows/cmake.yaml -j build

# Verbose output
act -W .github/workflows/ci.yaml -v

# Simulate pull request event
act pull_request -W .github/workflows/cmake.yaml

# For more: see RUN-WORKFLOWS-LOCALLY.md
EOF
echo ""

echo "=========================================="
echo "Demo Complete!"
echo "=========================================="
echo ""
echo "For detailed documentation, see:"
echo "  - RUN-WORKFLOWS-LOCALLY.md (act usage guide)"
echo "  - LOCAL-TESTING.md (local testing with scripts)"
echo "  - BSD-SETUP.md (BSD-specific instructions)"
echo ""
