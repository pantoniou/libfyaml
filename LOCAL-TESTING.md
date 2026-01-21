# Local Testing Guide for libfyaml

This guide helps you run the CI pipeline locally on Ubuntu and macOS.

**For BSD systems (FreeBSD, OpenBSD, NetBSD):** See [BSD-SETUP.md](BSD-SETUP.md) for BSD-specific instructions and self-hosted runner setup.

## Quick Start

### 1. Install Dependencies

```bash
# Install all dependencies
./install-deps.sh

# With LLVM/libclang for reflection support
./install-deps.sh --with-llvm

# With specific LLVM version
./install-deps.sh --with-llvm --llvm-version 18
```

### 2. Run Tests

```bash
# Run all tests (Autotools + CMake)
./test-local.sh

# Quick mode (faster, skips distcheck)
./test-local.sh --quick

# Test only CMake
./test-local.sh --cmake-only

# Test only Autotools
./test-local.sh --autotools-only

# Release build
./test-local.sh --release

# With valgrind (slow but thorough)
./test-local.sh --valgrind
```

## Manual Testing

### Autotools Build

```bash
# Bootstrap
./bootstrap.sh

# Configure
./configure

# Build
make -j$(nproc)

# Run tests
make check

# Distribution check
make distcheck
```

### CMake Build

```bash
# Configure
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON \
    -DENABLE_NETWORK=ON

# Build
cmake --build build

# Run tests
cd build && ctest --output-on-failure
```

### CMake with LLVM (Reflection Support)

```bash
# Automatic detection (if LLVM is in standard location)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Manual LLVM path (Linux)
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH=/usr/lib/llvm-20

# Manual LLVM path (macOS)
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH="$(brew --prefix llvm)"
```

## Build Options

### CMake Options

- `BUILD_SHARED_LIBS=ON` - Build shared libraries (default: ON)
- `ENABLE_ASAN=ON` - Enable AddressSanitizer for memory debugging
- `ENABLE_STATIC_TOOLS=ON` - Build tools as static executables
- `ENABLE_PORTABLE_TARGET=ON` - Disable CPU-specific optimizations
- `ENABLE_NETWORK=ON` - Enable network-based tests (default: ON)
- `BUILD_TESTING=ON` - Build tests (default: ON)

Example:
```bash
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_ASAN=ON \
    -DBUILD_TESTING=ON
```

### Autotools Options

- `--enable-asan` - Enable AddressSanitizer
- `--enable-static-tools` - Build tools as static executables
- `--enable-portable-target` - Disable CPU-specific optimizations
- `--enable-network` - Enable network-based tests
- `--with-libclang` - Enable libclang-based reflection backend

Example:
```bash
./configure --enable-asan --with-libclang
```

## Running Specific Tests

### CMake (CTest)

```bash
cd build

# List all tests
ctest -N

# Run specific test
ctest -R libfyaml

# Run tests matching pattern
ctest -R "testemitter/.*"

# Verbose output
ctest -V

# Run in parallel
ctest -j$(nproc)
```

### Autotools

```bash
# Run specific test script
./test/libfyaml.test
./test/testerrors.test
./test/testemitter.test

# Run test binary directly
./test/libfyaml-test
```

## Memory Testing

### Valgrind

```bash
# With test script
./test-local.sh --valgrind

# Manual (Autotools)
make check TESTS_ENVIRONMENT="valgrind --leak-check=full --error-exitcode=1"

# Manual (CMake)
cd build
ctest -T memcheck
```

### AddressSanitizer

```bash
# CMake
cmake -B build -DENABLE_ASAN=ON
cmake --build build
cd build && ctest

# Autotools
./configure --enable-asan
make
make check
```

## Performance Testing

### Release Build

```bash
# CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Autotools
./configure CFLAGS="-O3"
make
```

### Benchmarking

```bash
# Run tests with timing
cd build
ctest --output-on-failure --progress

# Use massif for memory profiling
./scripts/run-massif.sh
```

## Continuous Testing During Development

### Watch mode (requires `entr`)

```bash
# Install entr
sudo apt-get install entr  # Ubuntu
brew install entr          # macOS

# Auto-rebuild on file changes
find src test -name '*.c' -o -name '*.h' | entr -c make check
```

### Quick iteration

```bash
# Make changes to code, then:
make -j$(nproc) && make check

# Or with CMake:
cmake --build build && cd build && ctest
```

## Debugging

### GDB

```bash
# Build with debug symbols
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run under GDB
gdb --args ./build/test/libfyaml-test
```

### Core dumps

```bash
# Enable core dumps
ulimit -c unlimited

# Run tests
make check

# If crash occurs, debug with:
gdb ./test/libfyaml-test core
```

## Troubleshooting

### Dependencies not found

```bash
# Re-run dependency installer
./install-deps.sh

# Or check what's missing
./test-local.sh  # Will list missing tools
```

### CMake can't find LLVM

```bash
# Linux: Install LLVM dev packages
sudo apt-get install llvm-20-dev libclang-20-dev

# macOS: Install via Homebrew
brew install llvm

# Set path manually
export CMAKE_PREFIX_PATH=/usr/lib/llvm-20  # Linux
export CMAKE_PREFIX_PATH="$(brew --prefix llvm)"  # macOS
```

### Tests failing

```bash
# Clean rebuild (CMake)
rm -rf build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure

# Clean rebuild (Autotools)
make distclean
./bootstrap.sh
./configure
make
make check
```

### Network tests timeout

```bash
# Disable network tests
cmake -B build -DENABLE_NETWORK=OFF  # CMake
./configure --disable-network        # Autotools
```

## GitHub Actions Locally

To run the actual GitHub Actions workflows locally, use [act](https://github.com/nektos/act):

```bash
# Install act
brew install act  # macOS
# or visit: https://github.com/nektos/act/releases

# List workflows
act -l

# Run specific workflow
act -W .github/workflows/ci.yaml
act -W .github/workflows/cmake.yaml

# Run specific job
act -j build
```

**Note:** `act` has limitations with matrix builds and platform emulation.

## Getting Help

- Read the main README: `README.md`
- Project documentation: `CLAUDE.md`
- Build system docs: `cmake/README-package.md`
- Generic testing: `scripts/README-generic-testsuite.md`

## Contributing

When submitting PRs, ensure:

1. All tests pass locally: `./test-local.sh`
2. No memory leaks: `./test-local.sh --valgrind`
3. Code compiles with both CMake and Autotools
4. Tests pass on both Debug and Release builds

For significant changes, test on both Ubuntu and macOS if possible.
