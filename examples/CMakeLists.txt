cmake_minimum_required(VERSION 3.12)
project(libfyaml-examples C)

# Find libfyaml
find_package(libfyaml 0.9 QUIET)

if(NOT libfyaml_FOUND)
    # Fall back to pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBFYAML REQUIRED libfyaml)

    # Create imported target for consistency
    add_library(libfyaml::libfyaml INTERFACE IMPORTED)
    set_target_properties(libfyaml::libfyaml PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${LIBFYAML_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${LIBFYAML_LIBRARIES}"
        INTERFACE_LINK_DIRECTORIES "${LIBFYAML_LIBRARY_DIRS}"
        INTERFACE_COMPILE_OPTIONS "${LIBFYAML_CFLAGS_OTHER}"
    )
endif()

# Generic/value-model examples are always available.
set(GENERIC_EXAMPLES
    intro-core-update
    intro-generic-update
    quick-start
    basic-parsing
    path-queries
    document-manipulation
    event-streaming
    build-from-scratch
    generic-literals
    generic-lambda-capture
    generic-parallel-transform
    generic-transform
    generic-roundtrip
    generic-adoption-bridge
)

foreach(example ${GENERIC_EXAMPLES})
    add_executable(${example} ${example}.c)
    target_link_libraries(${example} PRIVATE libfyaml::libfyaml)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${example} PRIVATE -std=gnu2x -Wall -Wextra)
    endif()
endforeach()

set(REFLECTION_EXAMPLES)
set(REFLECTION_LIBCLANG_EXAMPLES)

# CMake package config can tell us whether libclang-backed reflection was built.
# With pkg-config fallback we keep the example set to generics to avoid guessing.
if(libfyaml_FOUND)
    list(APPEND REFLECTION_EXAMPLES reflection-packed)
    if(libfyaml_HAS_LIBCLANG)
        list(APPEND REFLECTION_LIBCLANG_EXAMPLES
            intro-reflection-update
            reflection-libclang
            reflection-export-packed
        )
    endif()
endif()

foreach(example ${REFLECTION_EXAMPLES} ${REFLECTION_LIBCLANG_EXAMPLES})
    add_executable(${example} ${example}.c)
    target_link_libraries(${example} PRIVATE libfyaml::libfyaml)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${example} PRIVATE -std=gnu2x -Wall -Wextra)
    endif()
endforeach()

# Installation (optional)
install(TARGETS
    ${GENERIC_EXAMPLES}
    ${REFLECTION_EXAMPLES}
    ${REFLECTION_LIBCLANG_EXAMPLES}
    RUNTIME DESTINATION bin/libfyaml-examples
)

# Install sample files
install(FILES
    README.md
    config.yaml
    intro-config.yaml
    invoice.yaml
    reflection-config.h
    reflection-config.yaml
    reflection-intro-config.h
    DESTINATION share/doc/libfyaml/examples
)

# Show configuration info
message(STATUS "libfyaml examples configuration:")
message(STATUS "  Generic examples: ${GENERIC_EXAMPLES}")
if(libfyaml_FOUND)
    message(STATUS "  Found libfyaml via CMake config")
    if(REFLECTION_EXAMPLES)
        message(STATUS "  Reflection examples: ${REFLECTION_EXAMPLES}")
    endif()
    if(libfyaml_HAS_LIBCLANG)
        message(STATUS "  Libclang reflection examples: ${REFLECTION_LIBCLANG_EXAMPLES}")
    endif()
else()
    message(STATUS "  Found libfyaml via pkg-config")
    message(STATUS "  Reflection examples disabled with pkg-config fallback")
endif()
