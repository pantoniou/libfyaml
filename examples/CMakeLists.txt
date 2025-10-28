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

# List of all examples
set(EXAMPLES
    quick-start
    basic-parsing
    path-queries
    document-manipulation
    event-streaming
    build-from-scratch
)

# Build each example
foreach(example ${EXAMPLES})
    add_executable(${example} ${example}.c)
    target_link_libraries(${example} PRIVATE libfyaml::libfyaml)

    # Set C standard
    set_property(TARGET ${example} PROPERTY C_STANDARD 99)

    # Enable warnings
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${example} PRIVATE -Wall -Wextra)
    endif()
endforeach()

# Installation (optional)
install(TARGETS ${EXAMPLES}
    RUNTIME DESTINATION bin/libfyaml-examples
)

# Install sample files
install(FILES
    README.md
    config.yaml
    invoice.yaml
    DESTINATION share/doc/libfyaml/examples
)

# Show configuration info
message(STATUS "libfyaml examples configuration:")
message(STATUS "  Examples to build: ${EXAMPLES}")
if(libfyaml_FOUND)
    message(STATUS "  Found libfyaml via CMake config")
    if(libfyaml_HAS_LIBCLANG)
        message(STATUS "  libfyaml has reflection support")
    endif()
else()
    message(STATUS "  Found libfyaml via pkg-config")
endif()
