#!/bin/bash
# run-asan-python.sh - Run Python with ASan-instrumented libfyaml
#
# Usage:
#   scripts/run-asan-python.sh -m pytest tests/
#   scripts/run-asan-python.sh myscript.py
#
# Environment variables:
#   DETECT_LEAKS=1     Enable leak detection (default: 0, see note below)
#   BUILD_DIR=path     Path to cmake build dir (default: auto-detected)
#   ASAN_PYTHON=path   Path to ASan-instrumented Python (default: system python3)
#
# Note on leak detection:
#   By default leak detection is disabled because the system Python interpreter
#   is not ASan-instrumented, so Python's own allocations appear as leaks.
#   To get accurate leak reports, build Python with ASan (see below).
#
# Building an ASan-instrumented Python:
#   git clone https://github.com/python/cpython && cd cpython
#   ./configure --with-address-sanitizer --without-pymalloc \
#               --prefix=$HOME/.local/python-asan
#   make -j$(nproc) && make install
#   Then: ASAN_PYTHON=$HOME/.local/python-asan/bin/python3 scripts/run-asan-python.sh ...

set -e

# --- Locate the cmake build directory ---
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ -z "$BUILD_DIR" ]; then
    BUILD_DIR="$REPO_ROOT/build"
fi

if [ ! -d "$BUILD_DIR" ]; then
    echo "ERROR: Build directory not found: $BUILD_DIR" >&2
    echo "Run: cmake -DENABLE_ASAN=ON -S $REPO_ROOT -B $BUILD_DIR" >&2
    exit 1
fi

if [ ! -f "$BUILD_DIR/libfyaml.so" ] && [ ! -f "$BUILD_DIR/libfyaml.so.0" ]; then
    echo "ERROR: libfyaml.so not found in $BUILD_DIR" >&2
    echo "Run: cmake --build $BUILD_DIR --target fyaml _libfyaml" >&2
    exit 1
fi

# --- Locate libasan ---
LIBASAN=$(gcc -print-file-name=libasan.so 2>/dev/null)
if [ -z "$LIBASAN" ] || [ "$LIBASAN" = "libasan.so" ]; then
    # Try common locations
    for candidate in \
        /usr/lib/gcc/x86_64-linux-gnu/13/libasan.so \
        /usr/lib/gcc/x86_64-linux-gnu/12/libasan.so \
        /usr/lib/gcc/aarch64-linux-gnu/13/libasan.so \
        $(clang --print-resource-dir 2>/dev/null)/lib/linux/libclang_rt.asan-x86_64.so
    do
        if [ -f "$candidate" ]; then
            LIBASAN="$candidate"
            break
        fi
    done
fi

if [ -z "$LIBASAN" ] || [ ! -f "$LIBASAN" ]; then
    echo "ERROR: libasan.so not found. Install gcc or clang." >&2
    exit 1
fi

# --- Python interpreter ---
PYTHON="${ASAN_PYTHON:-python3}"

# --- ASan options ---
DETECT_LEAKS="${DETECT_LEAKS:-0}"
ASAN_OPTS="detect_leaks=${DETECT_LEAKS}:halt_on_error=1:detect_stack_use_after_return=1"

# When using ASan Python, also enable LSAN suppressions for known Python internals
if [ -n "$ASAN_PYTHON" ] && [ -f "$REPO_ROOT/scripts/asan-python.supp" ]; then
    LSAN_OPTS="suppressions=$REPO_ROOT/scripts/asan-python.supp"
    export LSAN_OPTIONS="$LSAN_OPTS"
fi

echo "=== ASan Python Run ===" >&2
echo "  libasan:  $LIBASAN" >&2
echo "  build:    $BUILD_DIR" >&2
echo "  python:   $PYTHON" >&2
echo "  leaks:    $DETECT_LEAKS" >&2
echo "=======================" >&2

export LD_PRELOAD="$LIBASAN"
export LD_LIBRARY_PATH="$BUILD_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONPATH="$BUILD_DIR/python-libfyaml${PYTHONPATH:+:$PYTHONPATH}"
export ASAN_OPTIONS="$ASAN_OPTS"

exec "$PYTHON" "$@"
