#!/bin/bash
#
# Build and test libfyaml Python bindings
#

set -e

echo "========================================"
echo "Building libfyaml Python bindings"
echo "========================================"
echo

# Check if libfyaml is built
if [ ! -f "../build/libfyaml.so" ] && [ ! -f "../src/lib/.libs/libfyaml.so" ]; then
    echo "ERROR: libfyaml not built!"
    echo "Please build libfyaml first:"
    echo "  cd .. && mkdir build && cd build && cmake .. && make"
    echo "Or:"
    echo "  cd .. && ./bootstrap.sh && ./configure && make"
    exit 1
fi

# Clean previous build
echo "Cleaning previous build..."
rm -rf build/ libfyaml/*.so libfyaml/__pycache__ tests/__pycache__
echo

# Build extension
echo "Building C extension..."
python3 setup.py build_ext --inplace
echo

# Check if build succeeded
if [ ! -f "libfyaml/_libfyaml.so" ] && [ ! -f "libfyaml/_libfyaml.*.so" ]; then
    echo "ERROR: Build failed - extension module not found"
    exit 1
fi

echo "Build successful!"
echo

# Run tests
echo "========================================"
echo "Running tests"
echo "========================================"
echo

python3 tests/test_basic.py

echo
echo "========================================"
echo "Build and test complete!"
echo "========================================"
