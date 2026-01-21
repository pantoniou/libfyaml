#!/bin/bash
# Build libfyaml using CMake inside Docker container
set -e

BUILD_TYPE="${BUILD_TYPE:-Release}"

echo "=== Building libfyaml with CMake ==="
echo "Compiler: CC=$CC CXX=$CXX"
echo "Build type: $BUILD_TYPE"

# Create build directory in writable location
BUILD_DIR="${BUILD_DIR:-/home/builder/build/cmake}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
echo "=== Configure ==="
cmake /home/builder/libfyaml -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DBUILD_TESTING=ON \
    -DENABLE_NETWORK=ON

# Build
echo "=== Build ==="
cmake --build .

# Test
echo "=== Test ==="
ctest --output-on-failure

echo "=== Build completed successfully ==="
