#!/bin/bash
#
# Local CI test script for libfyaml
# Runs both Autotools and CMake builds with tests
# Works on Ubuntu and macOS
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Detect OS
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS="linux"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macos"
else
    echo -e "${RED}Unsupported OS: $OSTYPE${NC}"
    exit 1
fi

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}libfyaml Local CI Test Script${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "OS: ${GREEN}$OS${NC}"
echo ""

# Function to print section headers
print_section() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check for required tools
print_section "Checking for required tools"

MISSING_TOOLS=()

if ! command_exists git; then MISSING_TOOLS+=(git); fi
if ! command_exists make; then MISSING_TOOLS+=(make); fi
if ! command_exists gcc; then MISSING_TOOLS+=(gcc); fi
if ! command_exists autoconf; then MISSING_TOOLS+=(autoconf); fi
if ! command_exists automake; then MISSING_TOOLS+=(automake); fi
if ! command_exists libtoolize && ! command_exists glibtoolize; then MISSING_TOOLS+=(libtool); fi
if ! command_exists pkg-config; then MISSING_TOOLS+=(pkg-config); fi
if ! command_exists cmake; then MISSING_TOOLS+=(cmake); fi

if [ ${#MISSING_TOOLS[@]} -ne 0 ]; then
    echo -e "${RED}Missing required tools: ${MISSING_TOOLS[*]}${NC}"
    echo ""
    echo "To install dependencies:"
    if [ "$OS" = "linux" ]; then
        echo -e "${YELLOW}sudo apt-get update${NC}"
        echo -e "${YELLOW}sudo apt-get install -y autoconf automake libtool git make \\${NC}"
        echo -e "${YELLOW}  libyaml-dev libltdl-dev pkg-config check \\${NC}"
        echo -e "${YELLOW}  python3 python3-pip cmake ninja-build gcc g++ clang${NC}"
        echo ""
        echo "Optional (for reflection support):"
        echo -e "${YELLOW}sudo apt-get install -y llvm-20-dev libclang-20-dev${NC}"
    else
        echo -e "${YELLOW}brew install autoconf automake libtool pkg-config check libyaml cmake ninja${NC}"
    fi
    exit 1
fi

echo -e "${GREEN}All required tools found${NC}"

# Get number of CPU cores
if [ "$OS" = "linux" ]; then
    NCPUS=$(nproc)
else
    NCPUS=$(sysctl -n hw.ncpu)
fi
echo "Using $NCPUS CPU cores for parallel builds"

# Test mode selection
TEST_AUTOTOOLS=true
TEST_CMAKE=true
BUILD_TYPE="Debug"
RUN_VALGRIND=false
QUICK_MODE=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --autotools-only)
            TEST_CMAKE=false
            shift
            ;;
        --cmake-only)
            TEST_AUTOTOOLS=false
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --valgrind)
            RUN_VALGRIND=true
            shift
            ;;
        --quick)
            QUICK_MODE=true
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --autotools-only   Only run Autotools build"
            echo "  --cmake-only       Only run CMake build"
            echo "  --release          Build in Release mode (default: Debug)"
            echo "  --valgrind         Run tests under valgrind (slow)"
            echo "  --quick            Skip distcheck and extensive tests"
            echo "  --help             Show this help message"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Run '$0 --help' for usage information"
            exit 1
            ;;
    esac
done

# Autotools build and test
if [ "$TEST_AUTOTOOLS" = true ]; then
    print_section "Autotools Build"

    echo "Running bootstrap..."
    ./bootstrap.sh

    echo "Running configure..."
    ./configure

    echo "Building with make..."
    make -j$NCPUS

    echo "Running tests..."
    if [ "$RUN_VALGRIND" = true ]; then
        make check TESTS_ENVIRONMENT="valgrind --leak-check=full --error-exitcode=1"
    else
        make check
    fi

    if [ "$QUICK_MODE" = false ]; then
        echo "Running distcheck..."
        make distcheck
    fi

    echo -e "${GREEN}✓ Autotools build and tests passed${NC}"

    # Clean up for next build
    echo "Cleaning up..."
    make distclean || true
fi

# CMake build and test
if [ "$TEST_CMAKE" = true ]; then
    print_section "CMake Build ($BUILD_TYPE)"

    # Clean any existing build directory
    rm -rf build

    echo "Configuring CMake..."
    cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
        -DBUILD_TESTING=ON \
        -DENABLE_NETWORK=ON

    echo "Building with CMake..."
    cmake --build build --config $BUILD_TYPE

    echo "Running CTest..."
    cd build
    if [ "$QUICK_MODE" = true ]; then
        # Run only the core libfyaml test
        ctest --output-on-failure -R "^libfyaml$"
    else
        # Run all tests
        ctest --output-on-failure --config $BUILD_TYPE
    fi
    cd ..

    echo -e "${GREEN}✓ CMake build and tests passed${NC}"
fi

# Summary
print_section "Test Summary"

echo -e "${GREEN}All tests completed successfully!${NC}"
echo ""
echo "Tested configurations:"
if [ "$TEST_AUTOTOOLS" = true ]; then
    echo "  ✓ Autotools build"
fi
if [ "$TEST_CMAKE" = true ]; then
    echo "  ✓ CMake build ($BUILD_TYPE)"
fi
echo ""
echo "Your local environment is ready for development."
echo ""
echo "Next steps:"
echo "  - Run individual tests: cd test && ./libfyaml.test"
echo "  - Build specific targets: make -C build fy-tool"
echo "  - Run with ASAN: cmake -B build -DENABLE_ASAN=ON && cmake --build build"
echo "  - Generate docs: make doc-html (requires sphinx)"
