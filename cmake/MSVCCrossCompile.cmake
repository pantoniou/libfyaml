# MSVCCrossCompile.cmake - Auto-configure MSVC cross-compilation from msvc-wine
#
# Usage: Set MSVC_WINE_DIR to the msvc-wine installation directory, then include
# this file before project(). The module will auto-detect versions and set up
# the environment.
#
# Options:
#   MSVC_WINE_DIR       - Path to msvc-wine installation (required)
#   MSVC_WINE_COMPILER  - Compiler to use: "cl" (Wine) or "clang-cl" (default: cl)
#   MSVC_WINE_ARCH      - Target architecture: "x64", "x86", "arm64" (default: x64)
#   MSVC_VERSION        - Override MSVC toolchain version (auto-detected if not set)
#   WINSDK_VERSION      - Override Windows SDK version (auto-detected if not set)
#
# Example:
#   cmake -DMSVC_WINE_DIR=/path/to/msvc -DMSVC_WINE_COMPILER=clang-cl ..

if(NOT DEFINED MSVC_WINE_DIR)
    return()
endif()

# Normalize path
get_filename_component(MSVC_WINE_DIR "${MSVC_WINE_DIR}" ABSOLUTE)

if(NOT EXISTS "${MSVC_WINE_DIR}")
    message(FATAL_ERROR "MSVC_WINE_DIR does not exist: ${MSVC_WINE_DIR}")
endif()

# Set defaults
if(NOT DEFINED MSVC_WINE_COMPILER)
    set(MSVC_WINE_COMPILER "cl")
endif()

if(NOT DEFINED MSVC_WINE_ARCH)
    set(MSVC_WINE_ARCH "x64")
endif()

# Map architecture to library subdirectory
if(MSVC_WINE_ARCH STREQUAL "x64")
    set(_msvc_lib_arch "x64")
    set(_msvc_bin_arch "x64")
    set(_clang_triple "x86_64-windows-msvc")
elseif(MSVC_WINE_ARCH STREQUAL "x86")
    set(_msvc_lib_arch "x86")
    set(_msvc_bin_arch "x86")
    set(_clang_triple "i686-windows-msvc")
elseif(MSVC_WINE_ARCH STREQUAL "arm64")
    set(_msvc_lib_arch "arm64")
    set(_msvc_bin_arch "arm64")
    set(_clang_triple "aarch64-windows-msvc")
else()
    message(FATAL_ERROR "Unsupported MSVC_WINE_ARCH: ${MSVC_WINE_ARCH}")
endif()

# Auto-detect MSVC version if not specified
if(NOT DEFINED MSVC_VERSION)
    file(GLOB _msvc_versions "${MSVC_WINE_DIR}/vc/tools/msvc/*")
    list(LENGTH _msvc_versions _msvc_version_count)
    if(_msvc_version_count EQUAL 0)
        message(FATAL_ERROR "No MSVC toolchain found in ${MSVC_WINE_DIR}/vc/tools/msvc/")
    endif()
    # Sort and take the latest version
    list(SORT _msvc_versions COMPARE NATURAL ORDER DESCENDING)
    list(GET _msvc_versions 0 _msvc_version_path)
    get_filename_component(MSVC_VERSION "${_msvc_version_path}" NAME)
    message(STATUS "Auto-detected MSVC version: ${MSVC_VERSION}")
endif()

# Auto-detect Windows SDK version if not specified
if(NOT DEFINED WINSDK_VERSION)
    file(GLOB _sdk_versions "${MSVC_WINE_DIR}/kits/10/include/*")
    list(LENGTH _sdk_versions _sdk_version_count)
    if(_sdk_version_count EQUAL 0)
        message(FATAL_ERROR "No Windows SDK found in ${MSVC_WINE_DIR}/kits/10/include/")
    endif()
    # Sort and take the latest version
    list(SORT _sdk_versions COMPARE NATURAL ORDER DESCENDING)
    list(GET _sdk_versions 0 _sdk_version_path)
    get_filename_component(WINSDK_VERSION "${_sdk_version_path}" NAME)
    message(STATUS "Auto-detected Windows SDK version: ${WINSDK_VERSION}")
endif()

# Verify paths exist
set(_msvc_include "${MSVC_WINE_DIR}/vc/tools/msvc/${MSVC_VERSION}/include")
set(_msvc_lib "${MSVC_WINE_DIR}/vc/tools/msvc/${MSVC_VERSION}/lib/${_msvc_lib_arch}")
set(_sdk_include_shared "${MSVC_WINE_DIR}/kits/10/include/${WINSDK_VERSION}/shared")
set(_sdk_include_ucrt "${MSVC_WINE_DIR}/kits/10/include/${WINSDK_VERSION}/ucrt")
set(_sdk_include_um "${MSVC_WINE_DIR}/kits/10/include/${WINSDK_VERSION}/um")
set(_sdk_lib_ucrt "${MSVC_WINE_DIR}/kits/10/lib/${WINSDK_VERSION}/ucrt/${_msvc_lib_arch}")
set(_sdk_lib_um "${MSVC_WINE_DIR}/kits/10/lib/${WINSDK_VERSION}/um/${_msvc_lib_arch}")

foreach(_path ${_msvc_include} ${_msvc_lib} ${_sdk_include_shared} ${_sdk_include_ucrt} ${_sdk_include_um} ${_sdk_lib_ucrt} ${_sdk_lib_um})
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Required path does not exist: ${_path}")
    endif()
endforeach()

# Build INCLUDE and LIB paths (semicolon-separated for Windows/MSVC)
set(MSVC_WINE_INCLUDE "${_msvc_include};${_sdk_include_shared};${_sdk_include_ucrt};${_sdk_include_um}")
set(MSVC_WINE_LIB "${_msvc_lib};${_sdk_lib_ucrt};${_sdk_lib_um}")

# Build compiler/linker flags for include/lib paths
# clang-cl uses -imsvc for system includes, regular clang uses -isystem
set(_include_flags_clangcl "")
set(_include_flags_clang "")
foreach(_inc ${_msvc_include} ${_sdk_include_shared} ${_sdk_include_ucrt} ${_sdk_include_um})
    string(APPEND _include_flags_clangcl " -imsvc \"${_inc}\"")
    string(APPEND _include_flags_clang " -isystem \"${_inc}\"")
endforeach()

# Linker flags: /LIBPATH: for lld-link (clang-cl), -L for regular clang
set(_lib_flags_lldlink "")
set(_lib_flags_clang "")
foreach(_lib ${_msvc_lib} ${_sdk_lib_ucrt} ${_sdk_lib_um})
    string(APPEND _lib_flags_lldlink " /LIBPATH:\"${_lib}\"")
    string(APPEND _lib_flags_clang " -L\"${_lib}\"")
endforeach()

message(STATUS "MSVC cross-compile configuration:")
message(STATUS "  MSVC_WINE_DIR: ${MSVC_WINE_DIR}")
message(STATUS "  MSVC_WINE_COMPILER: ${MSVC_WINE_COMPILER}")
message(STATUS "  MSVC_WINE_ARCH: ${MSVC_WINE_ARCH}")
message(STATUS "  MSVC_VERSION: ${MSVC_VERSION}")
message(STATUS "  WINSDK_VERSION: ${WINSDK_VERSION}")

# Set environment variables for the build (used by cl.exe via Wine)
set(ENV{INCLUDE} "${MSVC_WINE_INCLUDE}")
set(ENV{LIB} "${MSVC_WINE_LIB}")

# Configure based on compiler choice
if(MSVC_WINE_COMPILER STREQUAL "cl")
    # Use cl.exe via Wine
    set(_bin_dir "${MSVC_WINE_DIR}/bin/${_msvc_bin_arch}")
    if(NOT EXISTS "${_bin_dir}/cl")
        message(FATAL_ERROR "cl wrapper not found in ${_bin_dir}")
    endif()

    # Prepend to PATH
    set(ENV{PATH} "${_bin_dir}:$ENV{PATH}")

    set(CMAKE_SYSTEM_NAME Windows CACHE STRING "" FORCE)
    set(CMAKE_C_COMPILER "${_bin_dir}/cl" CACHE STRING "" FORCE)
    set(CMAKE_CXX_COMPILER "${_bin_dir}/cl" CACHE STRING "" FORCE)
    set(CMAKE_LINKER "${_bin_dir}/link" CACHE STRING "" FORCE)
    set(CMAKE_RC_COMPILER "${_bin_dir}/rc" CACHE STRING "" FORCE)
    set(CMAKE_MT "${_bin_dir}/mt" CACHE STRING "" FORCE)

elseif(MSVC_WINE_COMPILER STREQUAL "clang-cl")
    # Use clang-cl (native cross-compilation, no Wine needed)
    find_program(CLANG_CL_EXECUTABLE NAMES clang-cl clang-cl-18 clang-cl-17 clang-cl-16 clang-cl-15)
    if(NOT CLANG_CL_EXECUTABLE)
        message(FATAL_ERROR "clang-cl not found. Install with: apt install clang")
    endif()

    find_program(LLD_LINK_EXECUTABLE NAMES lld-link lld-link-18 lld-link-17 lld-link-16 lld-link-15)
    if(NOT LLD_LINK_EXECUTABLE)
        message(FATAL_ERROR "lld-link not found. Install with: apt install lld")
    endif()

    find_program(LLVM_RC_EXECUTABLE NAMES llvm-rc llvm-rc-18 llvm-rc-17 llvm-rc-16 llvm-rc-15)
    if(NOT LLVM_RC_EXECUTABLE)
        message(FATAL_ERROR "llvm-rc not found. Install with: apt install llvm")
    endif()

    find_program(LLVM_MT_EXECUTABLE NAMES llvm-mt llvm-mt-18 llvm-mt-17 llvm-mt-16 llvm-mt-15)
    if(NOT LLVM_MT_EXECUTABLE)
        message(FATAL_ERROR "llvm-mt not found. Install with: apt install llvm")
    endif()

    set(CMAKE_SYSTEM_NAME Windows CACHE STRING "" FORCE)
    set(CMAKE_C_COMPILER "${CLANG_CL_EXECUTABLE}" CACHE STRING "" FORCE)
    set(CMAKE_CXX_COMPILER "${CLANG_CL_EXECUTABLE}" CACHE STRING "" FORCE)
    set(CMAKE_LINKER "${LLD_LINK_EXECUTABLE}" CACHE STRING "" FORCE)
    set(CMAKE_RC_COMPILER "${LLVM_RC_EXECUTABLE}" CACHE STRING "" FORCE)
    set(CMAKE_MT "${LLVM_MT_EXECUTABLE}" CACHE STRING "" FORCE)

    # Pass include paths via compiler flags (env vars don't persist to build)
    set(CMAKE_C_FLAGS_INIT "${_include_flags_clangcl}" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_INIT "${_include_flags_clangcl}" CACHE STRING "" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS_INIT "${_lib_flags_lldlink}" CACHE STRING "" FORCE)
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_lib_flags_lldlink}" CACHE STRING "" FORCE)
    set(CMAKE_STATIC_LINKER_FLAGS_INIT "" CACHE STRING "" FORCE)

    message(STATUS "  Using clang-cl: ${CLANG_CL_EXECUTABLE}")
    message(STATUS "  Using lld-link: ${LLD_LINK_EXECUTABLE}")
    message(STATUS "  Using llvm-rc: ${LLVM_RC_EXECUTABLE}")
    message(STATUS "  Using llvm-mt: ${LLVM_MT_EXECUTABLE}")

elseif(MSVC_WINE_COMPILER STREQUAL "clang")
    # Use regular clang with Windows target triplet via toolchain file (no Wine needed)
    # The toolchain file is required for proper cross-compilation detection
    set(_toolchain_file "${CMAKE_CURRENT_SOURCE_DIR}/cmake/clang-windows-toolchain.cmake")
    if(NOT EXISTS "${_toolchain_file}")
        message(FATAL_ERROR "Toolchain file not found: ${_toolchain_file}")
    endif()

    # Find LLVM tools before toolchain processing
    find_program(LLVM_RC_EXECUTABLE NAMES llvm-rc llvm-rc-18 llvm-rc-17 llvm-rc-16 llvm-rc-15)
    if(NOT LLVM_RC_EXECUTABLE)
        message(FATAL_ERROR "llvm-rc not found. Install with: apt install llvm")
    endif()

    find_program(LLVM_MT_EXECUTABLE NAMES llvm-mt llvm-mt-18 llvm-mt-17 llvm-mt-16 llvm-mt-15)
    if(NOT LLVM_MT_EXECUTABLE)
        message(FATAL_ERROR "llvm-mt not found. Install with: apt install llvm")
    endif()

    set(CMAKE_RC_COMPILER "${LLVM_RC_EXECUTABLE}" CACHE STRING "" FORCE)
    set(CMAKE_MT "${LLVM_MT_EXECUTABLE}" CACHE STRING "" FORCE)
    set(CMAKE_TOOLCHAIN_FILE "${_toolchain_file}" CACHE STRING "" FORCE)

    message(STATUS "  Using toolchain file: ${_toolchain_file}")
    message(STATUS "  Using llvm-rc: ${LLVM_RC_EXECUTABLE}")
    message(STATUS "  Using llvm-mt: ${LLVM_MT_EXECUTABLE}")
    message(STATUS "  Note: INCLUDE and LIB env vars must remain set for build")

else()
    message(FATAL_ERROR "Unknown MSVC_WINE_COMPILER: ${MSVC_WINE_COMPILER} (use 'cl', 'clang-cl', or 'clang')")
endif()

# Export variables for use in the project
set(MSVC_WINE_CONFIGURED TRUE CACHE INTERNAL "MSVC Wine cross-compilation configured")
