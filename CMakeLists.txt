cmake_minimum_required(VERSION 3.10)

if (POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW) # Allow project(xxx VERSION a.b.c)
endif()

if (POLICY CMP0110)
    cmake_policy(SET CMP0110 NEW) # add_test() supports arbitrary characters in test names
endif()

# Get version from git-version-gen
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.tarball-version")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.tarball-version" PROJECT_VERSION)
    string(STRIP "${PROJECT_VERSION}" PROJECT_VERSION)
else()
    execute_process(
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/build-aux/git-version-gen" "${CMAKE_CURRENT_SOURCE_DIR}/.tarball-version"
        OUTPUT_VARIABLE PROJECT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

# Parse version components
string(REGEX MATCH "^([0-9]+)(\\.([0-9]+)(\\.([0-9]+))?)?(-(.*))?$" _ "${PROJECT_VERSION}")
set(VERSION_MAJOR "${CMAKE_MATCH_1}")
if(CMAKE_MATCH_3)
    set(VERSION_MINOR "${CMAKE_MATCH_3}")
else()
    set(VERSION_MINOR "0")
endif()
if(CMAKE_MATCH_5)
    set(VERSION_PATCH "${CMAKE_MATCH_5}")
else()
    set(VERSION_PATCH "0")
endif()
if(CMAKE_MATCH_7)
    set(VERSION_EXTRA "${CMAKE_MATCH_7}")
else()
    set(VERSION_EXTRA "")
endif()

project(libfyaml LANGUAGES C ASM VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

# Must use GNUInstallDirs to install libraries into correct locations on all platforms
include(GNUInstallDirs)
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckCSourceCompiles)
include(CheckCCompilerFlag)
include(CheckLibraryExists)
include(CMakeDependentOption)
include(CTest)

# Options
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(ENABLE_PORTABLE_TARGET "Enable portable mode (disable per-target optimizations)" OFF)
option(ENABLE_STATIC_TOOLS "Tools will be compiled as static executables" OFF)
option(ENABLE_ASAN "Enable ASAN support" OFF)
option(ENABLE_NETWORK "Enable tests requiring network access" ON)
option(ENABLE_DEVMODE "Enable development mode only debugging" OFF)
option(BUILD_TESTING "Build tests" ON)

# Read libtool version
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.libtool-version" LIBTOOL_VERSION)
string(STRIP "${LIBTOOL_VERSION}" LIBTOOL_VERSION)

# Parse libtool version components (format is current:revision:age)
string(REGEX REPLACE "^([0-9]+):.*" "\\1" LIBTOOL_CURRENT "${LIBTOOL_VERSION}")
string(REGEX REPLACE "^[0-9]+:([0-9]+):.*" "\\1" LIBTOOL_REVISION "${LIBTOOL_VERSION}")
string(REGEX REPLACE "^[0-9]+:[0-9]+:([0-9]+)" "\\1" LIBTOOL_AGE "${LIBTOOL_VERSION}")

# Calculate SO version (current - age)
math(EXPR SO_VERSION "${LIBTOOL_CURRENT} - ${LIBTOOL_AGE}")
set(SO_VERSION_FULL "${SO_VERSION}.${LIBTOOL_AGE}.${LIBTOOL_REVISION}")

# Platform detection
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(TARGET_CPU_X86_64 TRUE)
    set(TARGET_CPU_ANY_X86 TRUE)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i.86|x86")
    set(TARGET_CPU_X86 TRUE)
    set(TARGET_CPU_ANY_X86 TRUE)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(TARGET_CPU_ARM64 TRUE)
    set(TARGET_CPU_ANY_ARM TRUE)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    set(TARGET_CPU_ARM TRUE)
    set(TARGET_CPU_ANY_ARM TRUE)
endif()

# Check for required dependencies
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
if(NOT CMAKE_USE_PTHREADS_INIT)
    message(FATAL_ERROR "Missing required pthread support")
endif()

# Check for headers and functions
check_include_file("byteswap.h" HAVE_BYTESWAP_H)
check_function_exists(__builtin_bswap16 HAVE___BUILTIN_BSWAP16)
check_function_exists(__builtin_bswap32 HAVE___BUILTIN_BSWAP32)
check_function_exists(__builtin_bswap64 HAVE___BUILTIN_BSWAP64)
check_function_exists(qsort_r HAVE_QSORT_R)
check_function_exists(mremap HAVE_MREMAP)

# Check for environment variables
check_c_source_compiles("
    extern char **environ;
    int main() { return 0; }
" HAVE_DECL_ENVIRON)

# Check for optional dependencies
find_package(PkgConfig)

set(HAVE_LIBYAML 0)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBYAML yaml-0.1)
    if(LIBYAML_FOUND)
        set(HAVE_LIBYAML 1)
    else()
        message(WARNING "failed to find libyaml; compatibility disabled")
    endif()
endif()

set(HAVE_CHECK 0)
set(HAVE_COMPATIBLE_CHECK 0)
if(PKG_CONFIG_FOUND AND BUILD_TESTING)
    pkg_check_modules(CHECK check)
    if(CHECK_FOUND)
        set(HAVE_CHECK 1)

        # Check if libcheck has srunner_set_tap (jessie has outdated libcheck)

    # save and restore for the check
    set(_TEMP_CMAKE_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES}")
    set(_TEMP_CMAKE_REQUIRED_INCLUDES  "${CMAKE_REQUIRED_INCLUDES}")
        set(CMAKE_REQUIRED_LIBRARIES ${CHECK_LIBRARIES})
        set(CMAKE_REQUIRED_INCLUDES ${CHECK_INCLUDE_DIRS})

        check_library_exists("${CHECK_LIBRARIES}" srunner_set_tap "" HAVE_SRUNNER_SET_TAP)

    set(CMAKE_REQUIRED_LIBRARIES "${_TEMP_CMAKE_REQUIRED_LIBRARIES}")
    set(CMAKE_REQUIRED_INCLUDES  "${_TEMP_CMAKE_REQUIRED_INCLUDES}")
    unset(_TEMP_CMAKE_REQUIRED_LIBRARIES)
    unset(_TEMP_CMAKE_REQUIRED_INCLUDES)

        if(HAVE_SRUNNER_SET_TAP)
            set(HAVE_COMPATIBLE_CHECK 1)
        endif()
    endif()
endif()

# Check for tools
find_program(GIT_EXECUTABLE git)
if(GIT_EXECUTABLE)
    set(HAVE_GIT 1)
else()
    set(HAVE_GIT 0)
endif()

find_program(JQ_EXECUTABLE jq)
if(JQ_EXECUTABLE)
    set(HAVE_JQ 1)
else()
    set(HAVE_JQ 0)
endif()

find_program(DOCKER_EXECUTABLE docker)
if(DOCKER_EXECUTABLE)
    set(HAVE_DOCKER 1)
else()
    set(HAVE_DOCKER 0)
endif()

# Test suite URLs
if(NOT TESTSUITEURL)
    set(TESTSUITEURL "https://github.com/yaml/yaml-test-suite")
endif()
if(NOT TESTSUITECHECKOUT)
    set(TESTSUITECHECKOUT "6e6c296ae9c9d2d5c4134b4b64d01b29ac19ff6f")
endif()
if(NOT JSONTESTSUITEURL)
    set(JSONTESTSUITEURL "https://github.com/nst/JSONTestSuite")
endif()
if(NOT JSONTESTSUITECHECKOUT)
    set(JSONTESTSUITECHECKOUT "d64aefb55228d9584d3e5b2433f720ea8fd00c82")
endif()

# SIMD capability detection
set(TARGET_HAS_SSE2 FALSE)
set(TARGET_HAS_SSE41 FALSE)
set(TARGET_HAS_AVX2 FALSE)
set(TARGET_HAS_AVX512 FALSE)
set(TARGET_HAS_NEON FALSE)

if(TARGET_CPU_ANY_X86 AND NOT ENABLE_PORTABLE_TARGET)
    check_c_source_compiles("
        #include <emmintrin.h>
        int main() { __m128i a = _mm_setzero_si128(); return 0; }
    " COMPILER_SUPPORTS_SSE2)
    if(COMPILER_SUPPORTS_SSE2)
        set(TARGET_HAS_SSE2 TRUE)
    endif()

    check_c_source_compiles("
        #include <smmintrin.h>
        int main() { __m128i a = _mm_setzero_si128(); return 0; }
    " COMPILER_SUPPORTS_SSE41)
    if(COMPILER_SUPPORTS_SSE41)
        set(TARGET_HAS_SSE41 TRUE)
    endif()

    set(CMAKE_REQUIRED_FLAGS "-mavx2")
    check_c_source_compiles("
        #include <immintrin.h>
        int main() { __m256i a = _mm256_setzero_si256(); return 0; }
    " COMPILER_SUPPORTS_AVX2)
    set(CMAKE_REQUIRED_FLAGS "")
    if(COMPILER_SUPPORTS_AVX2)
        set(TARGET_HAS_AVX2 TRUE)
    endif()

    set(CMAKE_REQUIRED_FLAGS "-mavx512f -mavx512vl")
    check_c_source_compiles("
        #include <immintrin.h>
        int main() { __m512i a = _mm512_setzero_si512(); return 0; }
    " COMPILER_SUPPORTS_AVX512)
    set(CMAKE_REQUIRED_FLAGS "")
    if(COMPILER_SUPPORTS_AVX512)
        set(TARGET_HAS_AVX512 TRUE)
    endif()
endif()

if(TARGET_CPU_ANY_ARM AND NOT ENABLE_PORTABLE_TARGET)
    if(TARGET_CPU_ARM64)
        set(TARGET_HAS_NEON TRUE)
    else()
        check_c_source_compiles("
            #include <arm_neon.h>
            int main() { uint8x16_t a = vdupq_n_u8(0); return 0; }
        " COMPILER_SUPPORTS_NEON)
        if(COMPILER_SUPPORTS_NEON)
            set(TARGET_HAS_NEON TRUE)
        endif()
    endif()
endif()

# ASAN support
set(HAVE_ASAN 0)
if(ENABLE_ASAN)
    check_c_source_compiles("
        int main() { return 0; }
    " ASAN_WORKS)
    if(ASAN_WORKS)
        set(HAVE_ASAN 1)
    endif()
endif()

# Build common compiler flags for all targets
set(COMMON_C_FLAGS)
set(COMMON_C_DEFINITIONS _GNU_SOURCE)

if(NOT MSVC)
    list(APPEND COMMON_C_FLAGS -Wall -Wsign-compare)

    check_c_compiler_flag(-Wno-unused-function COMPILER_SUPPORTS_WNO_UNUSED_FUNCTION)
    if(COMPILER_SUPPORTS_WNO_UNUSED_FUNCTION)
        list(APPEND COMMON_C_FLAGS -Wno-unused-function)
    endif()

    check_c_compiler_flag(-Wno-stringop-overflow COMPILER_SUPPORTS_WNO_STRINGOP_OVERFLOW)
    if(COMPILER_SUPPORTS_WNO_STRINGOP_OVERFLOW)
        list(APPEND COMMON_C_FLAGS -Wno-stringop-overflow)
    endif()

    check_c_compiler_flag(-Wno-tautological-constant-out-of-range-compare
                          COMPILER_SUPPORTS_WNO_TAUTOLOGICAL_CONSTANT_OUT_OF_RANGE_COMPARE)
    if(COMPILER_SUPPORTS_WNO_TAUTOLOGICAL_CONSTANT_OUT_OF_RANGE_COMPARE)
        list(APPEND COMMON_C_FLAGS -Wno-tautological-constant-out-of-range-compare)
    endif()
endif()

# Network tests
if(ENABLE_NETWORK)
    set(HAVE_NETWORK 1)
else()
    set(HAVE_NETWORK 0)
endif()

# Dev mode
if(ENABLE_DEVMODE)
    set(HAVE_DEVMODE 1)
else()
    set(HAVE_DEVMODE 0)
endif()

# Static support
if(NOT BUILD_SHARED_LIBS)
    set(HAVE_STATIC 1)
else()
    set(HAVE_STATIC 0)
endif()

# Library sources
set(LIBHDRS
    src/lib/fy-accel.h
    src/lib/fy-atom.h
    src/lib/fy-composer.h
    src/lib/fy-diag.h
    src/lib/fy-doc.h
    src/lib/fy-docbuilder.h
    src/lib/fy-docstate.h
    src/lib/fy-dump.h
    src/lib/fy-emit.h
    src/lib/fy-emit-accum.h
    src/lib/fy-event.h
    src/lib/fy-input.h
    src/lib/fy-parse.h
    src/lib/fy-path.h
    src/lib/fy-token.h
    src/lib/fy-types.h
    src/lib/fy-walk.h
)

set(UTILHDRS
    src/util/fy-blob.h
    src/util/fy-ctype.h
    src/util/fy-endian.h
    src/util/fy-id.h
    src/util/fy-list.h
    src/util/fy-typelist.h
    src/util/fy-utf8.h
    src/util/fy-utils.h
    src/util/fy-align.h
    src/util/fy-bit64.h
    src/util/fy-vlsize.h
)

set(LIBSRCS
    src/lib/fy-accel.c
    src/lib/fy-atom.c
    src/lib/fy-composer.c
    src/lib/fy-diag.c
    src/lib/fy-doc.c
    src/lib/fy-docbuilder.c
    src/lib/fy-docstate.c
    src/lib/fy-dump.c
    src/lib/fy-emit.c
    src/lib/fy-event.c
    src/lib/fy-input.c
    src/lib/fy-parse.c
    src/lib/fy-path.c
    src/lib/fy-token.c
    src/lib/fy-types.c
    src/lib/fy-walk.c
    src/util/fy-blob.c
    src/util/fy-ctype.c
    src/util/fy-utf8.c
    src/util/fy-utils.c
    src/xxhash/xxhash.c
    src/thread/fy-thread.c
    src/allocator/fy-allocator.c
    src/allocator/fy-allocator-linear.c
    src/allocator/fy-allocator-malloc.c
    src/allocator/fy-allocator-mremap.c
    src/allocator/fy-allocator-dedup.c
    src/allocator/fy-allocator-auto.c
    src/blake3/blake3_host_state.c
    src/blake3/blake3_backend.c
    src/blake3/blake3_be_cpusimd.c
    src/blake3/fy-blake3.c
)

set(LIBHDRSPUB
    include/libfyaml.h
)

# BLAKE3 portable library
add_library(b3portable OBJECT
    src/blake3/blake3_portable.c
    src/blake3/blake3.c
)
target_include_directories(b3portable PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    ${CMAKE_CURRENT_SOURCE_DIR}/src/thread
    ${CMAKE_CURRENT_SOURCE_DIR}/src/blake3
)
target_compile_definitions(b3portable PRIVATE
    ${COMMON_C_DEFINITIONS}
    HASHER_SUFFIX=portable
    SIMD_DEGREE=1
)
target_compile_options(b3portable PRIVATE ${COMMON_C_FLAGS})

# BLAKE3 SIMD libraries
set(BLAKE3_TARGETS b3portable)

if(TARGET_HAS_SSE2)
    add_library(b3sse2 OBJECT
        src/blake3/blake3_sse2.c
        src/blake3/blake3_sse2_x86-64_unix.S
        src/blake3/blake3.c
    )
    target_include_directories(b3sse2 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
        ${CMAKE_CURRENT_SOURCE_DIR}/src/thread
        ${CMAKE_CURRENT_SOURCE_DIR}/src/blake3
    )
    target_compile_definitions(b3sse2 PRIVATE
        ${COMMON_C_DEFINITIONS}
        HASHER_SUFFIX=sse2
        SIMD_DEGREE=4
    )
    target_compile_options(b3sse2 PRIVATE ${COMMON_C_FLAGS} -msse2)
    list(APPEND BLAKE3_TARGETS b3sse2)
endif()

if(TARGET_HAS_SSE41)
    add_library(b3sse41 OBJECT
        src/blake3/blake3_sse41.c
        src/blake3/blake3_sse41_x86-64_unix.S
        src/blake3/blake3.c
    )
    target_include_directories(b3sse41 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
        ${CMAKE_CURRENT_SOURCE_DIR}/src/thread
        ${CMAKE_CURRENT_SOURCE_DIR}/src/blake3
    )
    target_compile_definitions(b3sse41 PRIVATE
        ${COMMON_C_DEFINITIONS}
        HASHER_SUFFIX=sse41
        SIMD_DEGREE=4
    )
    target_compile_options(b3sse41 PRIVATE ${COMMON_C_FLAGS} -msse4.1)
    list(APPEND BLAKE3_TARGETS b3sse41)
endif()

if(TARGET_HAS_AVX2)
    add_library(b3avx2 OBJECT
        src/blake3/blake3_avx2.c
        src/blake3/blake3_avx2_x86-64_unix.S
        src/blake3/blake3.c
    )
    target_include_directories(b3avx2 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
        ${CMAKE_CURRENT_SOURCE_DIR}/src/thread
        ${CMAKE_CURRENT_SOURCE_DIR}/src/blake3
    )
    target_compile_definitions(b3avx2 PRIVATE
        ${COMMON_C_DEFINITIONS}
        HASHER_SUFFIX=avx2
        SIMD_DEGREE=8
    )
    target_compile_options(b3avx2 PRIVATE ${COMMON_C_FLAGS} -mavx2)
    list(APPEND BLAKE3_TARGETS b3avx2)
endif()

if(TARGET_HAS_AVX512)
    add_library(b3avx512 OBJECT
        src/blake3/blake3_avx512.c
        src/blake3/blake3_avx512_x86-64_unix.S
        src/blake3/blake3.c
    )
    target_include_directories(b3avx512 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
        ${CMAKE_CURRENT_SOURCE_DIR}/src/thread
        ${CMAKE_CURRENT_SOURCE_DIR}/src/blake3
    )
    target_compile_definitions(b3avx512 PRIVATE
        ${COMMON_C_DEFINITIONS}
        HASHER_SUFFIX=avx512
        SIMD_DEGREE=16
    )
    target_compile_options(b3avx512 PRIVATE ${COMMON_C_FLAGS} -mavx512f -mavx512vl)
    list(APPEND BLAKE3_TARGETS b3avx512)
endif()

if(TARGET_HAS_NEON)
    add_library(b3neon OBJECT
        src/blake3/blake3_neon.c
        src/blake3/blake3.c
    )
    target_include_directories(b3neon PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
        ${CMAKE_CURRENT_SOURCE_DIR}/src/thread
        ${CMAKE_CURRENT_SOURCE_DIR}/src/blake3
    )
    target_compile_definitions(b3neon PRIVATE
        ${COMMON_C_DEFINITIONS}
        HASHER_SUFFIX=neon
        SIMD_DEGREE=4
    )
    target_compile_options(b3neon PRIVATE ${COMMON_C_FLAGS})
    if(TARGET_CPU_ARM)
        target_compile_options(b3neon PRIVATE -mfpu=neon)
    endif()
    list(APPEND BLAKE3_TARGETS b3neon)
endif()

# Main library
add_library(fyaml
    ${LIBHDRS}
    ${UTILHDRS}
    ${LIBSRCS}
    ${LIBHDRSPUB}
)

# Link BLAKE3 object libraries
foreach(blake3_target ${BLAKE3_TARGETS})
    target_sources(fyaml PRIVATE $<TARGET_OBJECTS:${blake3_target}>)
endforeach()

add_library(libfyaml::libfyaml ALIAS fyaml)

# Always build a static library for internal tools (they need access to internal APIs)
# This is separate from BUILD_SHARED_LIBS and matches autotools behavior
add_library(fyaml_static STATIC
    ${LIBHDRS}
    ${UTILHDRS}
    ${LIBSRCS}
    ${LIBHDRSPUB}
)

# Link BLAKE3 object libraries to static version
foreach(blake3_target ${BLAKE3_TARGETS})
    target_sources(fyaml_static PRIVATE $<TARGET_OBJECTS:${blake3_target}>)
endforeach()

set_target_properties(fyaml_static PROPERTIES
    OUTPUT_NAME fyaml_static
    POSITION_INDEPENDENT_CODE OFF
    FOLDER "Libraries"
)

# Configure static library with same settings as main library
target_include_directories(fyaml_static
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
        ${CMAKE_CURRENT_SOURCE_DIR}/src/xxhash
        ${CMAKE_CURRENT_SOURCE_DIR}/src/thread
        ${CMAKE_CURRENT_SOURCE_DIR}/src/allocator
        ${CMAKE_CURRENT_SOURCE_DIR}/src/blake3
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(fyaml_static
    PUBLIC
        Threads::Threads
)

target_compile_definitions(fyaml_static PRIVATE
    HAVE_CONFIG_H
    VERSION="${PROJECT_VERSION}"
    $<$<NOT:$<CONFIG:DEBUG>>:NDEBUG>
)

target_compile_definitions(fyaml_static PRIVATE ${COMMON_C_DEFINITIONS})
target_compile_options(fyaml_static PRIVATE ${COMMON_C_FLAGS})

if(NOT MSVC)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(fyaml_static PRIVATE -O2)
    endif()

    if(HAVE_ASAN)
        target_compile_options(fyaml_static PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        # Don't add link options here - executables will link dynamically against libasan
    endif()
endif()

set_target_properties(fyaml PROPERTIES
    OUTPUT_NAME fyaml
    VERSION ${PROJECT_VERSION}
    SOVERSION ${VERSION_MAJOR}
    FOLDER "Libraries"
    PUBLIC_HEADER "${LIBHDRSPUB}"
)

target_include_directories(fyaml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
        ${CMAKE_CURRENT_SOURCE_DIR}/src/xxhash
        ${CMAKE_CURRENT_SOURCE_DIR}/src/thread
        ${CMAKE_CURRENT_SOURCE_DIR}/src/allocator
        ${CMAKE_CURRENT_SOURCE_DIR}/src/blake3
)

target_link_libraries(fyaml
    PUBLIC
        Threads::Threads
)

# Generate config.h
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"
)
target_include_directories(fyaml PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_compile_definitions(fyaml PRIVATE
    HAVE_CONFIG_H
    VERSION="${PROJECT_VERSION}"
    $<$<NOT:$<CONFIG:DEBUG>>:NDEBUG>
)

target_compile_definitions(fyaml PRIVATE ${COMMON_C_DEFINITIONS})
target_compile_options(fyaml PRIVATE ${COMMON_C_FLAGS})

# Shared options between GCC and CLANG
if(NOT MSVC)
    target_compile_options(fyaml PRIVATE -fvisibility=hidden)

    if(BUILD_SHARED_LIBS)
        target_compile_options(fyaml PRIVATE -fPIC)
    endif()

    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(fyaml PRIVATE -O2)
    endif()

    if(HAVE_ASAN)
        target_compile_options(fyaml PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        # Use INTERFACE to propagate link flags to executables linking against this library
        target_link_options(fyaml INTERFACE -fsanitize=address)
    endif()
endif()

# Build fy-tool
add_executable(fy-tool
    src/tool/fy-tool.c
    src/valgrind/fy-valgrind.h
)

target_include_directories(fy-tool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/valgrind
)

target_link_libraries(fy-tool PRIVATE fyaml)

if(ENABLE_STATIC_TOOLS AND NOT MSVC)
    target_link_options(fy-tool PRIVATE -static)
endif()

# Build internal tools - these are never installed, built for internal testing
# These tools use private/internal APIs and embed libfyaml code via fyaml_static
# System libraries are dynamically linked (works with or without ASAN)

# libfyaml-parser requires libyaml
if(HAVE_LIBYAML)
    add_executable(libfyaml-parser
        src/internal/libfyaml-parser.c
        src/valgrind/fy-valgrind.h
    )
    target_include_directories(libfyaml-parser PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/valgrind
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
        ${CMAKE_CURRENT_SOURCE_DIR}/src/xxhash
        ${CMAKE_CURRENT_SOURCE_DIR}/src/thread
        ${CMAKE_CURRENT_SOURCE_DIR}/src/allocator
        ${LIBYAML_INCLUDE_DIRS}
    )
    target_link_directories(libfyaml-parser PRIVATE ${LIBYAML_LIBRARY_DIRS})
    if(NOT MSVC)
        # Embed libfyaml code statically, allow dynamic linking for system libraries
        if(APPLE)
            target_link_libraries(libfyaml-parser PRIVATE
                -Wl,-force_load,$<TARGET_FILE:fyaml_static>
                ${LIBYAML_LIBRARIES}
            )
        else()
            target_link_libraries(libfyaml-parser PRIVATE
                -Wl,--whole-archive fyaml_static -Wl,--no-whole-archive
                ${LIBYAML_LIBRARIES}
            )
        endif()
        if(HAVE_ASAN)
            target_link_options(libfyaml-parser PRIVATE -fsanitize=address)
        endif()
    else()
        target_link_libraries(libfyaml-parser PRIVATE fyaml_static ${LIBYAML_LIBRARIES})
    endif()
    add_dependencies(libfyaml-parser fyaml_static)
endif()

add_executable(fy-thread
    src/internal/fy-thread.c
    src/valgrind/fy-valgrind.h
)
target_include_directories(fy-thread PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/valgrind
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    ${CMAKE_CURRENT_SOURCE_DIR}/src/thread
    ${CMAKE_CURRENT_SOURCE_DIR}/src/allocator
)
if(NOT MSVC)
    if(APPLE)
        target_link_libraries(fy-thread PRIVATE
            -Wl,-force_load,$<TARGET_FILE:fyaml_static>
        )
    else()
        target_link_libraries(fy-thread PRIVATE
            -Wl,--whole-archive fyaml_static -Wl,--no-whole-archive
        )
    endif()
    if(HAVE_ASAN)
        target_link_options(fy-thread PRIVATE -fsanitize=address)
    endif()
else()
    target_link_libraries(fy-thread PRIVATE fyaml_static)
endif()
add_dependencies(fy-thread fyaml_static)

add_executable(fy-b3sum
    src/internal/fy-b3sum.c
    src/valgrind/fy-valgrind.h
)
target_include_directories(fy-b3sum PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/valgrind
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    ${CMAKE_CURRENT_SOURCE_DIR}/src/blake3
    ${CMAKE_CURRENT_SOURCE_DIR}/src/thread
    ${CMAKE_CURRENT_SOURCE_DIR}/src/allocator
)
if(NOT MSVC)
    if(APPLE)
        target_link_libraries(fy-b3sum PRIVATE
            -Wl,-force_load,$<TARGET_FILE:fyaml_static>
        )
    else()
        target_link_libraries(fy-b3sum PRIVATE
            -Wl,--whole-archive fyaml_static -Wl,--no-whole-archive
        )
    endif()
    if(HAVE_ASAN)
        target_link_options(fy-b3sum PRIVATE -fsanitize=address)
    endif()
else()
    target_link_libraries(fy-b3sum PRIVATE fyaml_static)
endif()
add_dependencies(fy-b3sum fyaml_static)

add_executable(fy-allocators
    src/internal/fy-allocators.c
    src/valgrind/fy-valgrind.h
)
target_include_directories(fy-allocators PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/valgrind
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    ${CMAKE_CURRENT_SOURCE_DIR}/src/xxhash
    ${CMAKE_CURRENT_SOURCE_DIR}/src/thread
    ${CMAKE_CURRENT_SOURCE_DIR}/src/allocator
)
if(NOT MSVC)
    if(APPLE)
        target_link_libraries(fy-allocators PRIVATE
            -Wl,-force_load,$<TARGET_FILE:fyaml_static>
        )
    else()
        target_link_libraries(fy-allocators PRIVATE
            -Wl,--whole-archive fyaml_static -Wl,--no-whole-archive
        )
    endif()
    if(HAVE_ASAN)
        target_link_options(fy-allocators PRIVATE -fsanitize=address)
    endif()
else()
    target_link_libraries(fy-allocators PRIVATE fyaml_static)
endif()
add_dependencies(fy-allocators fyaml_static)

# Testing
if(BUILD_TESTING)
    # Include TAP subtest registration functions
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/add-tap-subtests.cmake)

    # Create test directory
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test)

    # Create src directory for compatibility with test scripts
    # Tests expect tools at build/src/
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/src)
    add_custom_command(TARGET fy-tool POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_BINARY_DIR}/fy-tool
            ${CMAKE_CURRENT_BINARY_DIR}/src/fy-tool
        COMMENT "Creating symlink for test compatibility"
    )

    # Create symlinks for internal tools
    add_custom_command(TARGET fy-thread POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_BINARY_DIR}/fy-thread
            ${CMAKE_CURRENT_BINARY_DIR}/src/fy-thread
    )
    add_custom_command(TARGET fy-b3sum POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_BINARY_DIR}/fy-b3sum
            ${CMAKE_CURRENT_BINARY_DIR}/src/fy-b3sum
    )
    add_custom_command(TARGET fy-allocators POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_BINARY_DIR}/fy-allocators
            ${CMAKE_CURRENT_BINARY_DIR}/src/fy-allocators
    )
    if(HAVE_LIBYAML)
        add_custom_command(TARGET libfyaml-parser POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E create_symlink
                ${CMAKE_CURRENT_BINARY_DIR}/libfyaml-parser
                ${CMAKE_CURRENT_BINARY_DIR}/src/libfyaml-parser
        )
    endif()

    # Build libfyaml-test if check is available
    if(HAVE_COMPATIBLE_CHECK)
        add_executable(libfyaml-test
            test/libfyaml-test.c
            test/libfyaml-test-core.c
            test/libfyaml-test-meta.c
            test/libfyaml-test-emit.c
            test/libfyaml-test-allocator.c
            test/libfyaml-test-private.c
            test/libfyaml-test-private-id.c
        )

        target_include_directories(libfyaml-test PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/valgrind
            ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
            ${CMAKE_CURRENT_SOURCE_DIR}/src/util
            ${CHECK_INCLUDE_DIRS}
        )

        # Link against static library to access private symbols
        # Use same pattern as internal tools
        if(NOT MSVC)
            if(APPLE)
                target_link_libraries(libfyaml-test PRIVATE
                    -Wl,-force_load,$<TARGET_FILE:fyaml_static>
                    ${CHECK_LIBRARIES}
                )
            else()
                target_link_libraries(libfyaml-test PRIVATE
                    -Wl,--whole-archive fyaml_static -Wl,--no-whole-archive
                    ${CHECK_LIBRARIES}
                )
            endif()
            if(HAVE_ASAN)
                target_link_options(libfyaml-test PRIVATE -fsanitize=address)
            endif()
        else()
            target_link_libraries(libfyaml-test PRIVATE fyaml_static ${CHECK_LIBRARIES})
        endif()
        add_dependencies(libfyaml-test fyaml_static)

        # Set output directory to match autoconf behavior
        set_target_properties(libfyaml-test PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
        )

        # Add libfyaml.test
        add_test(NAME libfyaml
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/libfyaml.test
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
        )
        set_tests_properties(libfyaml PROPERTIES
            ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test"
        )
    endif()

    # Error tests - register individual subtests
    add_testerrors_tests()

    # Emitter tests - register individual subtests
    add_testemitter_tests(testemitter "")
    add_testemitter_tests(testemitter-streaming "--streaming")
    add_testemitter_tests(testemitter-restreaming "--streaming --recreating")

    # Network and git-dependent tests
    if(HAVE_NETWORK AND HAVE_GIT)
        # YAML test suite - setup fixture
        add_test(NAME yaml-test-suite-setup
            COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR}/test
                ${CMAKE_COMMAND}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DTESTSUITEURL=${TESTSUITEURL}
                -DTESTSUITECHECKOUT=${TESTSUITECHECKOUT}
                -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/test
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/setup-yaml-test-suite.cmake
        )
        set_tests_properties(yaml-test-suite-setup PROPERTIES
            FIXTURES_SETUP yaml-test-suite-data
        )

        # YAML test suite tests - register individual subtests
        # Three modes (handled automatically by add_testsuite_tests):
        # 1. If test-suite-data exists: scan filesystem and register tests
        # 2. If cmake/testsuite-tests.cmake exists: use pre-generated list
        # 3. Otherwise: use monolithic test (re-run cmake after downloading data)
        # To generate cmake/testsuite-tests.cmake, see cmake/README-testsuite-generation.md
        add_testsuite_tests(testsuite testsuite.test)
        add_testsuite_tests(testsuite-evstream testsuite-evstream.test)
        add_testsuite_tests(testsuite-resolution testsuite-resolution.test)
        if(HAVE_JQ)
            add_testsuite_tests(testsuite-json testsuite-json.test)
        endif()

        # JSON test suite - setup fixture
        add_test(NAME json-test-suite-setup
            COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR}/test
                ${CMAKE_COMMAND}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DTESTSUITEURL=${JSONTESTSUITEURL}
                -DTESTSUITECHECKOUT=${JSONTESTSUITECHECKOUT}
                -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/test
                -DREPO_NAME=json-test-suite-data
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/setup-yaml-test-suite.cmake
        )
        set_tests_properties(json-test-suite-setup PROPERTIES
            FIXTURES_SETUP json-test-suite-data
        )

        # JSON test suite - register individual subtests
        # Three modes (handled automatically by add_jsontestsuite_tests):
        # 1. If json-test-suite-data exists: scan filesystem and register tests
        # 2. If cmake/jsontestsuite-tests.cmake exists: use pre-generated list
        # 3. Otherwise: use monolithic test (re-run cmake after downloading data)
        # To generate cmake/jsontestsuite-tests.cmake, see cmake/README-testsuite-generation.md
        add_jsontestsuite_tests()
    endif()

    set(TEST_DRIVER ${CMAKE_SOURCE_DIR}/build-aux/tap-driver.sh)

    # Build all test dependencies as part of default 'all' target
    # Note: CMake's 'make test' target doesn't trigger builds by design.
    # Use 'make all test' or 'make check' or 'make build_and_test' for complete workflow.
    set(TEST_DEPENDENCIES fy-tool fy-thread fy-b3sum fy-allocators)
    if(HAVE_COMPATIBLE_CHECK)
        list(APPEND TEST_DEPENDENCIES libfyaml-test)
    endif()
    if(HAVE_LIBYAML)
        list(APPEND TEST_DEPENDENCIES libfyaml-parser)
    endif()

    add_custom_target(build_tests ALL
        DEPENDS ${TEST_DEPENDENCIES}
        COMMENT "Building test dependencies"
    )

    # Convenience target that builds and runs tests in one command
    add_custom_target(build_and_test
        COMMAND ${CMAKE_CTEST_COMMAND}
        DEPENDS ${TEST_DEPENDENCIES}
        COMMENT "Building and running tests"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    # keep the old behavior of check
    add_custom_target(check
        COMMENT "Running tests via Automake test-driver"
    )

    if(HAVE_COMPATIBLE_CHECK)
        set(TEST_EXECUTABLES
            libfyaml-test
        )

        foreach(exe IN LISTS TEST_EXECUTABLES)
            add_custom_target(check_${exe}
                    COMMAND ${CMAKE_COMMAND} -E env TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR} TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR} SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test
                    ${TEST_DRIVER}
                    --test-name ${exe}
                    --log-file ${exe}.log
                    --trs-file ${exe}.trs
                    --comments
                    --color-tests yes
                    --expect-failure 0
                    -- ${CMAKE_CURRENT_BINARY_DIR}/test/${exe}
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
                COMMENT "Running ${exe}"
            )
            add_dependencies(check_${exe} ${exe})
            add_dependencies(check check_${exe})
        endforeach()
    endif()

    set(TEST_SUITES
        testsuite.test
        jsontestsuite.test
        testsuite-evstream.test
        testsuite-resolution.test
        testerrors.test
        testemitter.test
        testemitter-streaming.test
        testemitter-restreaming.test
    )

    if(HAVE_JQ)
        list(APPEND TEST_SUITES testsuite-json.test)
    endif()

    foreach(suite IN LISTS TEST_SUITES)
        add_custom_target(check_${suite}
            COMMAND ${CMAKE_COMMAND} -E env TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR} TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR} SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test
                ${TEST_DRIVER}
                --test-name ${suite}
                --log-file ${suite}.log
                --trs-file ${suite}.trs
                --comments
                --color-tests yes
                --expect-failure 0
                -- ${CMAKE_SOURCE_DIR}/test/${suite}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
            COMMENT "Running ${suite}"
        )
        add_dependencies(check_${suite} fy-tool)
        add_dependencies(check check_${suite})
    endforeach()

endif()

# Install targets
install(TARGETS fyaml fy-tool
    EXPORT libfyaml-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Create symlinks for fy-tool
install(CODE "
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink fy-tool fy-dump WORKING_DIRECTORY \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink fy-tool fy-filter WORKING_DIRECTORY \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink fy-tool fy-testsuite WORKING_DIRECTORY \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink fy-tool fy-join WORKING_DIRECTORY \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink fy-tool fy-ypath WORKING_DIRECTORY \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink fy-tool fy-compose WORKING_DIRECTORY \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
")

install(EXPORT libfyaml-targets
    FILE libfyaml-targets.cmake
    NAMESPACE libfyaml::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libfyaml
)

export(
    TARGETS fyaml
    FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/libfyaml-targets.cmake"
    NAMESPACE libfyaml::
)

# Package config
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/libfyaml-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/libfyaml-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libfyaml
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/libfyaml-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/libfyaml-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/libfyaml-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libfyaml
)

# pkg-config file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/libfyaml.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/libfyaml.pc
    @ONLY
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/libfyaml.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# Print configuration summary
message(STATUS "")
message(STATUS "---{ ${PROJECT_NAME} ${PROJECT_VERSION} }---")
message(STATUS "")
message(STATUS "VERSION:               ${PROJECT_VERSION}")
message(STATUS "MAJOR.MINOR:           ${VERSION_MAJOR}.${VERSION_MINOR}")
message(STATUS "PATCH:                 ${VERSION_PATCH}")
message(STATUS "EXTRA:                 ${VERSION_EXTRA}")
message(STATUS "LIBTOOL_VERSION:       ${LIBTOOL_VERSION}")
message(STATUS "prefix:                ${CMAKE_INSTALL_PREFIX}")
message(STATUS "C compiler:            ${CMAKE_C_COMPILER}")
message(STATUS "Build type:            ${CMAKE_BUILD_TYPE}")
message(STATUS "HAVE_CHECK:            ${HAVE_CHECK}")
message(STATUS "HAVE_COMPATIBLE_CHECK: ${HAVE_COMPATIBLE_CHECK}")
message(STATUS "HAVE_LIBYAML:          ${HAVE_LIBYAML}")
message(STATUS "HAVE_NETWORK:          ${HAVE_NETWORK}")
message(STATUS "HAVE_DEVMODE:          ${HAVE_DEVMODE}")
message(STATUS "HAVE_GIT:              ${HAVE_GIT}")
message(STATUS "HAVE_JQ:               ${HAVE_JQ}")
message(STATUS "HAVE_DOCKER:           ${HAVE_DOCKER}")
message(STATUS "HAVE_ASAN:             ${HAVE_ASAN}")
message(STATUS "TARGET_HAS_SSE2:       ${TARGET_HAS_SSE2}")
message(STATUS "TARGET_HAS_SSE41:      ${TARGET_HAS_SSE41}")
message(STATUS "TARGET_HAS_AVX2:       ${TARGET_HAS_AVX2}")
message(STATUS "TARGET_HAS_AVX512:     ${TARGET_HAS_AVX512}")
message(STATUS "TARGET_HAS_NEON:       ${TARGET_HAS_NEON}")
message(STATUS "TESTSUITEURL:          ${TESTSUITEURL}")
message(STATUS "TESTSUITECHECKOUT:     ${TESTSUITECHECKOUT}")
message(STATUS "JSONTESTSUITEURL:      ${JSONTESTSUITEURL}")
message(STATUS "JSONTESTSUITECHECKOUT: ${JSONTESTSUITECHECKOUT}")
message(STATUS "")
if(BUILD_TESTING)
    message(STATUS "Test targets:")
    message(STATUS "  make all test       - Build everything then run CTest (2464 individual tests)")
    message(STATUS "  make build_and_test - Convenience target: build and test in one command")
    message(STATUS "  make check          - Autotools-compatible TAP test runner (~10 test scripts)")
    message(STATUS "")
    message(STATUS "NOTE: 'make test' alone will fail if binaries aren't built. Use 'make all test'.")
    message(STATUS "")
endif()
