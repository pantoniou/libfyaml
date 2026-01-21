#!/bin/bash
# Build libfyaml using Autotools inside Docker container
set -e

echo "=== Building libfyaml with Autotools ==="
echo "Compiler: CC=$CC CXX=$CXX"

# Create build directory in writable location
BUILD_DIR="${BUILD_DIR:-/home/builder/build/autotools}"
mkdir -p "$BUILD_DIR"

# Copy source to build directory (source is mounted read-only)
echo "Copying source to build directory..."
cp -r /home/builder/libfyaml/* "$BUILD_DIR/"
cd "$BUILD_DIR"

# Bootstrap
echo "=== Bootstrap ==="
./bootstrap.sh

# Configure
echo "=== Configure ==="
./configure

# Build
echo "=== Build ==="
make -j"$(nproc)"

# Test
echo "=== Test ==="
make check

echo "=== Build completed successfully ==="
