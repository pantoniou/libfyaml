# libfyaml Windows Port

## Building with MSVC (via msvc-wine)

```bash
cd /mnt/980-linux/panto/work/msvc-wine/libfyaml

# Clean any previous build
rm -rf build

# Configure - need explicit paths because cmake doesn't find them in PATH reliably
PATH=/home/panto/work/msvc/bin/x64:/usr/bin:$PATH \
CC=cl CXX=cl \
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Windows \
  -G Ninja \
  -DCMAKE_MAKE_PROGRAM=/usr/bin/ninja

# Build
PATH=/home/panto/work/msvc/bin/x64:/usr/bin:$PATH \
ninja -C build
```

Outputs:
- `build/fyaml.dll` - shared library
- `build/fyaml.lib` - import library for DLL
- `build/fyaml_static.lib` - static library
- `build/fy-tool.exe` - command-line tool

## Branch Structure

- `crazy-windows-guy` - original monolithic Windows port (3 commits)
- `windows-port-v2` - same changes split into 10 logical patches:
  1. Fix GCC ternary `?:` extension
  2. Fix void pointer arithmetic + emscripten fix
  3. Add `fy-win32.h` (POSIX emulation)
  4. Add portable `getopt`
  5. Add MSVC `.def` file
  6. Port public API/utility headers
  7. Port threading layer
  8. Port input handling (CRLF, binary mode)
  9. Windows compat for core/tools/allocators
  10. CMake build system + portable tests

Both branches produce identical output (`git diff` is empty between them).

## Building with native clang-cl (no Wine)

Uses the system clang-cl to cross-compile, with MSVC headers/libraries only
(no Wine needed at build time). Requires workarounds for clang 18 bugs.

```bash
rm -rf build

INCLUDE="/home/panto/work/msvc/vc/tools/msvc/14.44.35207/include;/home/panto/work/msvc/kits/10/include/10.0.26100.0/shared;/home/panto/work/msvc/kits/10/include/10.0.26100.0/ucrt;/home/panto/work/msvc/kits/10/include/10.0.26100.0/um" \
LIB="/home/panto/work/msvc/vc/tools/msvc/14.44.35207/lib/x64;/home/panto/work/msvc/kits/10/lib/10.0.26100.0/ucrt/x64;/home/panto/work/msvc/kits/10/lib/10.0.26100.0/um/x64" \
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=clang-cl-18 \
  -DCMAKE_CXX_COMPILER=clang-cl-18 \
  -DCMAKE_LINKER=lld-link \
  -DCMAKE_MT=/home/panto/work/msvc/bin/x64/mt \
  -DCMAKE_C_FLAGS="-Wno-incompatible-function-pointer-types -Wno-implicit-function-declaration" \
  -G Ninja

# Build (INCLUDE and LIB must remain set)
INCLUDE="..." LIB="..." ninja -C build
```

### clang-cl workarounds applied

Three source-level fixes needed for clang-cl (clang defines `_MSC_VER`
but not `__GNUC__`, and its `intrin.h` has MMX vector type bugs):

1. **`src/util/fy-id.h`**: Changed `#if defined(__GNUC__) && __GNUC__ >= 4`
   to `#if (defined(__GNUC__) && __GNUC__ >= 4) || defined(__clang__)` so
   clang-cl uses `__builtin_ffs`/`__builtin_popcount` instead of falling
   through to the `_MSC_VER` branch that includes `<intrin.h>`.

2. **`src/blake3/blake3_impl.h`**: For clang-cl, forward-declare `_xgetbv`
   and `__cpuid` instead of `#include <intrin.h>`, which pulls in
   mmintrin.h with broken vector types in clang 18 cross-compilation mode.

3. **CMake flags**: `-Wno-incompatible-function-pointer-types` (enum vs
   unsigned int in allocator function pointer tables) and
   `-Wno-implicit-function-declaration` (fy_term_query_size is `#ifdef`'d
   out on Windows but called in dead code path).

## Building with regular clang (non-clang-cl)

Uses regular clang with `--target=x86_64-windows-msvc` triplet. Requires a
CMake toolchain file to properly configure for Windows cross-compilation.

Create `/tmp/clang-windows-toolchain.cmake`:
```cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

set(triple x86_64-windows-msvc)
set(CMAKE_C_COMPILER_TARGET ${triple})
set(CMAKE_CXX_COMPILER_TARGET ${triple})

set(CMAKE_LINKER lld-link)
set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_LINKER> <LINK_FLAGS> <OBJECTS> -out:<TARGET> <LINK_LIBRARIES>")
set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_LINKER> <LINK_FLAGS> <OBJECTS> -out:<TARGET> <LINK_LIBRARIES>")

set(CMAKE_C_CREATE_SHARED_LIBRARY "<CMAKE_LINKER> -dll <LINK_FLAGS> <OBJECTS> -out:<TARGET> -implib:<TARGET_IMPLIB> <LINK_LIBRARIES>")
set(CMAKE_CXX_CREATE_SHARED_LIBRARY "<CMAKE_LINKER> -dll <LINK_FLAGS> <OBJECTS> -out:<TARGET> -implib:<TARGET_IMPLIB> <LINK_LIBRARIES>")

# Disable Unix-specific flags
set(CMAKE_C_COMPILE_OPTIONS_PIC "")
set(CMAKE_CXX_COMPILE_OPTIONS_PIC "")
set(CMAKE_SHARED_LIBRARY_C_FLAGS "")
set(CMAKE_SHARED_LIBRARY_CXX_FLAGS "")

# Add -fuse-ld=lld to ensure clang uses lld
set(CMAKE_C_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_CXX_FLAGS_INIT "-fuse-ld=lld")
```

Then build:
```bash
rm -rf build

INCLUDE="/home/panto/work/msvc/vc/tools/msvc/14.44.35207/include;/home/panto/work/msvc/kits/10/include/10.0.26100.0/shared;/home/panto/work/msvc/kits/10/include/10.0.26100.0/ucrt;/home/panto/work/msvc/kits/10/include/10.0.26100.0/um" \
LIB="/home/panto/work/msvc/vc/tools/msvc/14.44.35207/lib/x64;/home/panto/work/msvc/kits/10/lib/10.0.26100.0/ucrt/x64;/home/panto/work/msvc/kits/10/lib/10.0.26100.0/um/x64" \
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/clang-windows-toolchain.cmake \
  -G Ninja

INCLUDE="..." LIB="..." ninja -C build
```

### CMakeLists.txt changes for regular clang

Two changes were needed in CMakeLists.txt:

1. Changed `if(NOT MSVC)` to `if(NOT MSVC AND NOT WIN32)` for the block that
   adds `-fPIC` and `-fvisibility=hidden`, since regular clang targeting
   Windows doesn't set `MSVC=true` but these flags are invalid for Windows.

2. Added clang-specific warning suppressions for Windows cross-compilation:
   ```cmake
   if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
       list(APPEND COMMON_C_FLAGS -Wno-implicit-function-declaration)
       list(APPEND COMMON_C_FLAGS -Wno-incompatible-function-pointer-types)
   endif()
   ```
