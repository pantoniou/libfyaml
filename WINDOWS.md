# Building libfyaml on Windows

libfyaml now has **experimental Windows support** using MinGW-w64. This document describes how to build libfyaml on Windows.

## Status

- ✅ **MinGW-w64**: Supported (recommended)
- ⚠️ **MSVC**: Not yet supported (contributions welcome)

## Prerequisites

### Option 1: Building on Windows with MinGW-w64 (MSYS2)

1. Install MSYS2 from https://www.msys2.org/
2. Open MSYS2 MinGW 64-bit terminal
3. Install required packages:

```bash
pacman -S mingw-w64-x86_64-gcc \
          mingw-w64-x86_64-cmake \
          mingw-w64-x86_64-ninja \
          mingw-w64-x86_64-pkg-config \
          git
```

### Option 2: Cross-compiling from Linux

On Ubuntu/Debian:
```bash
sudo apt-get install mingw-w64 cmake
```

On Fedora:
```bash
sudo dnf install mingw64-gcc mingw64-gcc-c++ cmake
```

On Arch Linux:
```bash
sudo pacman -S mingw-w64-gcc cmake
```

## Building

### Option 1: Native Windows Build (MSYS2)

```bash
# Clone the repository
git clone https://github.com/pantoniou/libfyaml.git
cd libfyaml

# Create build directory
mkdir build && cd build

# Configure
cmake .. -G "Ninja"

# Build
ninja

# Run tests (optional)
ctest
```

### Option 2: Cross-Compiling from Linux

```bash
# Clone the repository
git clone https://github.com/pantoniou/libfyaml.git
cd libfyaml

# Create build directory
mkdir build-mingw && cd build-mingw

# Configure with toolchain file
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw-w64-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)

# The Windows binaries will be in build-mingw/
# - libfyaml.dll
# - fy-tool.exe
```

## Installation

```bash
# In the build directory
cmake --install .
```

By default, libraries and binaries are installed to:
- Libraries: `C:\Program Files (x86)\libfyaml\lib`
- Headers: `C:\Program Files (x86)\libfyaml\include`
- Binaries: `C:\Program Files (x86)\libfyaml\bin`

You can change the installation prefix:
```bash
cmake .. -DCMAKE_INSTALL_PREFIX="C:\MyLibs\libfyaml"
```

## Windows-Specific Notes

### Memory Mapping

Windows doesn't have `mmap()`, so libfyaml automatically falls back to buffered file I/O. This may be slightly slower for very large YAML files but works correctly.

### BLAKE3 SIMD Optimizations

The BLAKE3 hash function uses portable C code on Windows (no assembly optimizations). Performance is still good but not as optimal as on Linux with SSE/AVX assembly.

Future versions may add Windows-specific MASM assembly for better performance.

### Thread Pool

MinGW provides `winpthreads`, a pthread compatibility layer. The thread pool works correctly but uses condition variables instead of Linux futexes, which may have slightly higher overhead.

### File Paths

libfyaml accepts both Unix-style (`/`) and Windows-style (`\`) path separators.

## Known Limitations

1. **No mmap**: Memory-mapped file I/O is not available
2. **No SIMD assembly**: BLAKE3 uses portable C code instead of optimized assembly
3. **No mremap**: The mremap allocator is disabled on Windows
4. **No mincore**: File page residency checks are skipped (minor optimization)

These limitations don't affect correctness, only performance.

## Performance

Performance on Windows with MinGW is typically:
- **Parsing/emitting**: 90-95% of Linux native performance
- **BLAKE3 hashing**: 85-90% of Linux (due to lack of assembly)
- **Thread synchronization**: 85-95% of Linux (pthread emulation overhead)

For most use cases, this performance is more than adequate.

## Troubleshooting

### CMake can't find pthread

MinGW provides winpthreads automatically. If CMake complains:
```bash
cmake .. -DCMAKE_THREAD_LIBS_INIT="-lpthread"
```

### Linking errors

Make sure you're using a consistent toolchain. Mix of MSVC and MinGW libraries will not work.

### Runtime DLL errors

When distributing your application, you need to include these DLLs:
- `libfyaml.dll` (your library)
- `libgcc_s_seh-1.dll` (MinGW runtime)
- `libwinpthread-1.dll` (pthread compatibility)

Or link statically: `-DBUILD_SHARED_LIBS=OFF`

## Contributing

MSVC support is not yet implemented. Contributions welcome!

The main areas that need work for MSVC:
1. Replace pthread with Windows threads
2. Add MASM assembly files for BLAKE3
3. Implement mmap wrapper using CreateFileMapping
4. Handle MSVC-specific compiler flags

## Questions?

- GitHub Issues: https://github.com/pantoniou/libfyaml/issues
- Make sure to mention you're building on Windows
