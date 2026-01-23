# CMakeLists.txt for libfyaml generic API benchmarks
#
# These benchmarks demonstrate the performance characteristics of the
# generic type system, including functional operations, parallel processing,
# and structural sharing.
#
# Build as part of main project:
#   mkdir build && cd build
#   cmake -DBUILD_BENCHMARKS=ON ..
#   make benchmarks
#
# Build standalone (requires installed libfyaml with internal headers):
#   cd examples/benchmarks
#   mkdir build && cd build
#   cmake -DLIBFYAML_SOURCE_DIR=/path/to/libfyaml ..
#   make

cmake_minimum_required(VERSION 3.12)
project(libfyaml-benchmarks C)

# Check if we're building as part of the main project
if(NOT TARGET fyaml)
    # Standalone build - check if LIBFYAML_SOURCE_DIR points to a build
    if(LIBFYAML_SOURCE_DIR AND EXISTS "${LIBFYAML_SOURCE_DIR}/build/libfyaml.so")
        # Use library from source tree build directory
        message(STATUS "Using libfyaml from source tree: ${LIBFYAML_SOURCE_DIR}/build")

        add_library(libfyaml::libfyaml SHARED IMPORTED)
        set_target_properties(libfyaml::libfyaml PROPERTIES
            IMPORTED_LOCATION "${LIBFYAML_SOURCE_DIR}/build/libfyaml.so"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBFYAML_SOURCE_DIR}/include"
        )
        set(FYAML_TARGET libfyaml::libfyaml)
    else()
        # Try to find installed libfyaml
        find_package(libfyaml 0.9 QUIET)

        if(NOT libfyaml_FOUND)
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(LIBFYAML REQUIRED libfyaml)

            add_library(libfyaml::libfyaml INTERFACE IMPORTED)
            set_target_properties(libfyaml::libfyaml PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${LIBFYAML_INCLUDE_DIRS}"
                INTERFACE_LINK_LIBRARIES "${LIBFYAML_LIBRARIES}"
                INTERFACE_LINK_DIRECTORIES "${LIBFYAML_LIBRARY_DIRS}"
                INTERFACE_COMPILE_OPTIONS "${LIBFYAML_CFLAGS_OTHER}"
            )
        endif()

        set(FYAML_TARGET libfyaml::libfyaml)

        # For standalone build, user must provide source dir for internal headers
        if(NOT LIBFYAML_SOURCE_DIR)
            message(WARNING "LIBFYAML_SOURCE_DIR not set - benchmarks requiring internal headers will not build")
            set(LIBFYAML_SOURCE_DIR "")
        endif()
    endif()
else()
    # Building as part of main project
    set(FYAML_TARGET fyaml)
    set(LIBFYAML_SOURCE_DIR "${CMAKE_SOURCE_DIR}")
endif()

# Find required libraries
find_package(Threads REQUIRED)

# Common compile options
set(BENCH_COMPILE_OPTIONS -Wall -Wextra)

# C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Include directory for internal headers (fy-internal-generic.h)
if(LIBFYAML_SOURCE_DIR)
    set(FYAML_INCLUDE_DIR ${LIBFYAML_SOURCE_DIR}/include)
else()
    # Try to find it via pkg-config
    set(FYAML_INCLUDE_DIR "")
endif()

#
# Benchmark: bench_functional
# Tests structural sharing and immutable operations
#
add_executable(bench_functional bench_functional.c)
target_link_libraries(bench_functional PRIVATE ${FYAML_TARGET})
target_compile_options(bench_functional PRIVATE ${BENCH_COMPILE_OPTIONS})
if(FYAML_INCLUDE_DIR)
    target_include_directories(bench_functional PRIVATE ${FYAML_INCLUDE_DIR})
endif()

#
# Benchmarks requiring internal headers (thread pool)
# Only build if we have access to source tree
#
if(LIBFYAML_SOURCE_DIR AND EXISTS "${LIBFYAML_SOURCE_DIR}/src/thread/fy-thread.h")
    # Common include directories for thread-pool benchmarks
    set(INTERNAL_INCLUDE_DIRS
        ${LIBFYAML_SOURCE_DIR}/include
        ${LIBFYAML_SOURCE_DIR}/src/thread
        ${LIBFYAML_SOURCE_DIR}/src/util
        ${LIBFYAML_SOURCE_DIR}/build  # for config.h
    )

    #
    # Benchmark: bench_parallel
    # Basic parallel map/filter with thread pool
    #
    add_executable(bench_parallel bench_parallel.c)
    target_link_libraries(bench_parallel PRIVATE ${FYAML_TARGET} Threads::Threads)
    target_include_directories(bench_parallel PRIVATE ${INTERNAL_INCLUDE_DIRS})
    target_compile_options(bench_parallel PRIVATE ${BENCH_COMPILE_OPTIONS})

    #
    # Benchmark: bench_parallel_heavy
    # Heavy workload parallel operations (sin/cos intensive)
    #
    add_executable(bench_parallel_heavy bench_parallel_heavy.c)
    target_link_libraries(bench_parallel_heavy PRIVATE ${FYAML_TARGET} Threads::Threads m)
    target_include_directories(bench_parallel_heavy PRIVATE ${INTERNAL_INCLUDE_DIRS})
    target_compile_options(bench_parallel_heavy PRIVATE ${BENCH_COMPILE_OPTIONS})

    #
    # Benchmark: bench_parallel_light
    # Light workload parallel operations
    #
    add_executable(bench_parallel_light bench_parallel_light.c)
    target_link_libraries(bench_parallel_light PRIVATE ${FYAML_TARGET} Threads::Threads m)
    target_include_directories(bench_parallel_light PRIVATE ${INTERNAL_INCLUDE_DIRS})
    target_compile_options(bench_parallel_light PRIVATE ${BENCH_COMPILE_OPTIONS})

    #
    # Benchmark: bench_parallel_reduce
    # Parallel reduce operations
    #
    add_executable(bench_parallel_reduce bench_parallel_reduce.c)
    target_link_libraries(bench_parallel_reduce PRIVATE ${FYAML_TARGET} Threads::Threads m)
    target_include_directories(bench_parallel_reduce PRIVATE ${INTERNAL_INCLUDE_DIRS})
    target_compile_options(bench_parallel_reduce PRIVATE ${BENCH_COMPILE_OPTIONS})

    set(THREAD_POOL_BENCHMARKS bench_parallel bench_parallel_heavy bench_parallel_light bench_parallel_reduce)
    message(STATUS "Building thread-pool benchmarks: ${THREAD_POOL_BENCHMARKS}")
else()
    message(STATUS "Skipping thread-pool benchmarks (internal headers not available)")
    set(THREAD_POOL_BENCHMARKS "")
endif()

#
# Example: yaml-parallel-example
# Demonstrates manual parallel YAML processing
#
add_executable(yaml-parallel-example yaml-parallel-example.c)
target_link_libraries(yaml-parallel-example PRIVATE ${FYAML_TARGET} Threads::Threads)
target_compile_options(yaml-parallel-example PRIVATE ${BENCH_COMPILE_OPTIONS})
if(FYAML_INCLUDE_DIR)
    target_include_directories(yaml-parallel-example PRIVATE ${FYAML_INCLUDE_DIR})
endif()

# Collect all benchmark targets
set(ALL_BENCHMARKS
    bench_functional
    ${THREAD_POOL_BENCHMARKS}
    yaml-parallel-example
)

# Create a custom target to build all benchmarks
add_custom_target(benchmarks DEPENDS ${ALL_BENCHMARKS})

# Installation (optional)
install(TARGETS ${ALL_BENCHMARKS}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/libfyaml-benchmarks
    OPTIONAL
)

# Print configuration summary
message(STATUS "")
message(STATUS "libfyaml benchmarks configuration:")
message(STATUS "  Benchmarks to build: ${ALL_BENCHMARKS}")
message(STATUS "  libfyaml target: ${FYAML_TARGET}")
if(LIBFYAML_SOURCE_DIR)
    message(STATUS "  Source directory: ${LIBFYAML_SOURCE_DIR}")
endif()
message(STATUS "")
