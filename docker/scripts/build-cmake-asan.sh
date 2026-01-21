#!/bin/bash
# Build libfyaml using CMake with AddressSanitizer inside Docker container
set -e

echo "=== Building libfyaml with CMake + ASAN ==="
echo "Compiler: CC=$CC CXX=$CXX"

# Create build directory in writable location
BUILD_DIR="${BUILD_DIR:-/home/builder/build/cmake-asan}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
echo "=== Configure ==="
cmake /home/builder/libfyaml -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_ASAN=ON \
    -DBUILD_TESTING=ON \
    -DENABLE_NETWORK=ON

# Build
echo "=== Build ==="
cmake --build .

# Test
echo "=== Test ==="
ctest --output-on-failure

echo "=== Build completed successfully ==="
