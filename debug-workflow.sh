#!/bin/bash
#
# Demonstrate workflow debugging with act
#

set -e

echo "╔════════════════════════════════════════════════════════════╗"
echo "║      Debugging GitHub Actions Workflows with act          ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Check if act is installed
if ! command -v act &> /dev/null; then
    echo "Error: act is not installed"
    echo "Run: ./install-act-safe.sh"
    exit 1
fi

echo "✓ act is installed ($(act --version))"
echo ""

# Function to count matrix combinations
count_matrix() {
    local file=$1
    echo "Analyzing matrix in $file:"
    
    # This is approximate - parsing YAML properly is complex
    local os_count=$(grep -A 20 "matrix:" "$file" | grep "os:" | head -1 | tr -cd ',' | wc -c)
    os_count=$((os_count + 1))
    
    local compiler_count=$(grep -A 20 "matrix:" "$file" | grep "compiler:" | head -1 | tr -cd ',' | wc -c)
    compiler_count=$((compiler_count + 1))
    
    local type_count=$(grep -A 20 "matrix:" "$file" | grep "build_type:" | head -1 | tr -cd ',' | wc -c)
    if [ $type_count -eq 0 ]; then
        type_count=1
    else
        type_count=$((type_count + 1))
    fi
    
    local total=$((os_count * compiler_count * type_count))
    echo "  OS variants: ~$os_count"
    echo "  Compilers: ~$compiler_count"
    echo "  Build types: ~$type_count"
    echo "  Approximate total: ~$total combinations"
    echo ""
}

echo "════════════════════════════════════════════════════════════"
echo "Step 1: Analyze Workflow Matrices"
echo "════════════════════════════════════════════════════════════"
echo ""

if [ -f .github/workflows/cmake.yaml ]; then
    count_matrix .github/workflows/cmake.yaml
fi

if [ -f .github/workflows/ci.yaml ]; then
    count_matrix .github/workflows/ci.yaml
fi

echo "⚠️  Running these workflows with act will create many containers!"
echo ""

echo "════════════════════════════════════════════════════════════"
echo "Step 2: Safe Validation (Dry Run)"
echo "════════════════════════════════════════════════════════════"
echo ""
echo "Command: act -W .github/workflows/cmake.yaml -n"
echo ""
echo "This validates syntax without actually running:"
read -p "Press Enter to continue..."
echo ""

act -W .github/workflows/cmake.yaml -n 2>&1 | head -20
echo ""
echo "... (output truncated) ..."
echo ""
echo "✓ Dry run completed - syntax is valid"
echo ""

echo "════════════════════════════════════════════════════════════"
echo "Step 3: Check Docker Resources"
echo "════════════════════════════════════════════════════════════"
echo ""

# Check if there are running containers
RUNNING=$(docker ps -q | wc -l)
if [ $RUNNING -gt 0 ]; then
    echo "⚠️  Warning: $RUNNING containers currently running"
    echo ""
    docker ps --format "table {{.Image}}\t{{.Status}}\t{{.Names}}" | head -10
    echo ""
    read -p "Stop these containers? [y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        docker ps -q | xargs docker stop
        echo "✓ Containers stopped"
    fi
else
    echo "✓ No containers currently running"
fi
echo ""

# Check memory
echo "Available memory:"
free -h | grep Mem
echo ""

echo "════════════════════════════════════════════════════════════"
echo "Step 4: Recommendations"
echo "════════════════════════════════════════════════════════════"
echo ""
echo "DON'T: Run full matrix with act"
echo "  ✗ act -W .github/workflows/cmake.yaml -j build"
echo "  (Creates 20+ containers, causes OOM)"
echo ""
echo "DO: Use one of these approaches:"
echo ""
echo "  1. Validate syntax only (fastest)"
echo "     act -W .github/workflows/cmake.yaml -n"
echo ""
echo "  2. Use local test scripts (most reliable)"
echo "     ./test-local.sh --cmake-only"
echo ""
echo "  3. Push to GitHub for full testing"
echo "     git push origin HEAD"
echo ""
echo "  4. Create simplified workflow for act"
echo "     See DEBUGGING-WORKFLOWS.md"
echo ""

echo "════════════════════════════════════════════════════════════"
echo "Step 5: Try Local Testing Now?"
echo "════════════════════════════════════════════════════════════"
echo ""
read -p "Run ./test-local.sh --quick --cmake-only? [y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    ./test-local.sh --quick --cmake-only
fi

echo ""
echo "════════════════════════════════════════════════════════════"
echo "Summary"
echo "════════════════════════════════════════════════════════════"
echo ""
echo "✓ Use act for: Syntax validation (dry-run)"
echo "✓ Use local scripts for: Real testing"
echo "✓ Use GitHub CI for: Full matrix testing"
echo ""
echo "See DEBUGGING-WORKFLOWS.md for detailed guide"
echo ""
