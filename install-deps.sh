#!/bin/bash
#
# Dependency installation script for libfyaml
# Installs all dependencies needed for local development and CI testing
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
echo -e "${BLUE}libfyaml Dependency Installer${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "OS: ${GREEN}$OS${NC}"
echo ""

# Parse command line arguments
INSTALL_LLVM=false
LLVM_VERSION="20"

while [[ $# -gt 0 ]]; do
    case $1 in
        --with-llvm)
            INSTALL_LLVM=true
            shift
            ;;
        --llvm-version)
            LLVM_VERSION="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --with-llvm           Install LLVM/libclang for reflection support"
            echo "  --llvm-version <ver>  Specify LLVM version (default: 20)"
            echo "  --help                Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                    Install basic dependencies"
            echo "  $0 --with-llvm        Install with LLVM 20"
            echo "  $0 --with-llvm --llvm-version 18"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Run '$0 --help' for usage information"
            exit 1
            ;;
    esac
done

if [ "$OS" = "linux" ]; then
    echo -e "${YELLOW}Installing dependencies for Ubuntu/Debian...${NC}"
    echo ""

    echo "Updating package lists..."
    sudo apt-get update -qq

    echo "Installing core build dependencies..."
    sudo apt-get install --no-install-recommends -y \
        autoconf \
        automake \
        libtool \
        git \
        make \
        libyaml-dev \
        libltdl-dev \
        pkg-config \
        check \
        python3 \
        python3-pip \
        python3-setuptools \
        cmake \
        ninja-build \
        gcc \
        g++ \
        clang

    if [ "$INSTALL_LLVM" = true ]; then
        echo ""
        echo "Installing LLVM ${LLVM_VERSION} and libclang..."

        # Check if LLVM version is available
        if apt-cache show "llvm-${LLVM_VERSION}-dev" &>/dev/null; then
            sudo apt-get install --no-install-recommends -y \
                "llvm-${LLVM_VERSION}" \
                "llvm-${LLVM_VERSION}-dev" \
                "libclang-${LLVM_VERSION}-dev"
            echo -e "${GREEN}✓ LLVM ${LLVM_VERSION} installed${NC}"
        else
            echo -e "${YELLOW}Warning: LLVM ${LLVM_VERSION} not found in repositories${NC}"
            echo "Attempting to install via LLVM apt repository..."

            # Download and run LLVM installation script
            wget -q https://apt.llvm.org/llvm.sh -O /tmp/llvm.sh
            chmod +x /tmp/llvm.sh
            sudo /tmp/llvm.sh ${LLVM_VERSION}

            sudo apt-get install --no-install-recommends -y \
                "libclang-${LLVM_VERSION}-dev" \
                "llvm-${LLVM_VERSION}-dev"

            rm /tmp/llvm.sh
            echo -e "${GREEN}✓ LLVM ${LLVM_VERSION} installed from apt.llvm.org${NC}"
        fi
    fi

    echo ""
    echo -e "${GREEN}✓ All dependencies installed successfully${NC}"

elif [ "$OS" = "macos" ]; then
    echo -e "${YELLOW}Installing dependencies for macOS...${NC}"
    echo ""

    # Check if Homebrew is installed
    if ! command -v brew &>/dev/null; then
        echo -e "${RED}Homebrew is not installed${NC}"
        echo "Please install Homebrew first: https://brew.sh"
        exit 1
    fi

    echo "Updating Homebrew..."
    brew update

    echo "Installing core build dependencies..."
    brew install \
        autoconf \
        automake \
        libtool \
        pkg-config \
        check \
        libyaml \
        cmake \
        ninja

    if [ "$INSTALL_LLVM" = true ]; then
        echo ""
        echo "Installing LLVM..."

        if [ "$LLVM_VERSION" = "20" ] || [ "$LLVM_VERSION" = "latest" ]; then
            brew install llvm
        else
            brew install "llvm@${LLVM_VERSION}"
        fi

        echo -e "${GREEN}✓ LLVM installed${NC}"
        echo ""
        echo -e "${YELLOW}Note: You may need to set CMAKE_PREFIX_PATH:${NC}"

        if [ "$LLVM_VERSION" = "20" ] || [ "$LLVM_VERSION" = "latest" ]; then
            LLVM_PATH="$(brew --prefix llvm)"
        else
            LLVM_PATH="$(brew --prefix llvm@${LLVM_VERSION})"
        fi

        echo "  export CMAKE_PREFIX_PATH=\"${LLVM_PATH}\""
        echo "or when running cmake:"
        echo "  cmake -DCMAKE_PREFIX_PATH=\"${LLVM_PATH}\" ..."
    fi

    echo ""
    echo -e "${GREEN}✓ All dependencies installed successfully${NC}"
fi

echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Installation Complete${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "You can now build libfyaml:"
echo ""
echo "Autotools:"
echo "  ./bootstrap.sh"
echo "  ./configure"
echo "  make"
echo "  make check"
echo ""
echo "CMake:"
echo "  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug"
echo "  cmake --build build"
echo "  cd build && ctest"
echo ""
echo "Or run the local test script:"
echo "  ./test-local.sh"
echo ""

if [ "$INSTALL_LLVM" = true ]; then
    echo -e "${GREEN}Reflection support enabled!${NC}"
    echo "The library will be built with libclang for type-aware features."
    echo ""
fi
