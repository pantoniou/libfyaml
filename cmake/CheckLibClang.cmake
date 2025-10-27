# CheckLibClang.cmake - Helper function to test if libclang works
#
# This module provides a function to test if libclang can be compiled and linked
# with the given compiler flags and libraries.
#
# Usage:
#   check_libclang_works(<result_var>
#                        CFLAGS <cflags>
#                        INCLUDES <includes>
#                        LINK_DIRS <link_dirs>
#                        LIBRARIES <libraries>)
#
# Arguments:
#   result_var - Variable name to store the result (TRUE/FALSE)
#   CFLAGS - Compiler flags to use (can be a list or string)
#   INCLUDES - Include directories (list)
#   LINK_DIRS - Link directories (list)
#   LIBRARIES - Libraries to link (list or string)
#
# Example:
#   check_libclang_works(LIBCLANG_WORKS
#                        CFLAGS "${LLVM_DEFINITIONS}"
#                        INCLUDES "${LLVM_INCLUDE_DIRS};${CLANG_INCLUDE_DIRS}"
#                        LINK_DIRS "${LLVM_LIBRARY_DIRS}"
#                        LIBRARIES "${LIBCLANG_LIBS}")

include(CheckCSourceCompiles)

function(check_libclang_works result_var)
    # Parse arguments
    set(options "")
    set(oneValueArgs "")
    set(multiValueArgs CFLAGS INCLUDES LINK_DIRS LIBRARIES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Save current CMAKE_REQUIRED_* variables
    set(_SAVED_CMAKE_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES}")
    set(_SAVED_CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
    set(_SAVED_CMAKE_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")
    set(_SAVED_CMAKE_REQUIRED_LINK_DIRECTORIES "${CMAKE_REQUIRED_LINK_DIRECTORIES}")

    # Set up test environment
    if(ARG_CFLAGS)
        set(CMAKE_REQUIRED_FLAGS "${ARG_CFLAGS}")
    endif()

    if(ARG_INCLUDES)
        set(CMAKE_REQUIRED_INCLUDES "${ARG_INCLUDES}")
    endif()

    if(ARG_LINK_DIRS)
        set(CMAKE_REQUIRED_LINK_DIRECTORIES "${ARG_LINK_DIRS}")
    endif()

    if(ARG_LIBRARIES)
        set(CMAKE_REQUIRED_LIBRARIES "${ARG_LIBRARIES}")
    endif()

    # Test if we can compile and link with libclang
    check_c_source_compiles("
        #include <clang-c/Index.h>
        int main() {
            CXIndex idx = clang_createIndex(0, 0);
            clang_disposeIndex(idx);
            return 0;
        }
    " _LIBCLANG_COMPILE_TEST)

    # Restore CMAKE_REQUIRED_* variables
    set(CMAKE_REQUIRED_LIBRARIES "${_SAVED_CMAKE_REQUIRED_LIBRARIES}" PARENT_SCOPE)
    set(CMAKE_REQUIRED_FLAGS "${_SAVED_CMAKE_REQUIRED_FLAGS}" PARENT_SCOPE)
    set(CMAKE_REQUIRED_INCLUDES "${_SAVED_CMAKE_REQUIRED_INCLUDES}" PARENT_SCOPE)
    set(CMAKE_REQUIRED_LINK_DIRECTORIES "${_SAVED_CMAKE_REQUIRED_LINK_DIRECTORIES}" PARENT_SCOPE)

    # Return result
    set(${result_var} ${_LIBCLANG_COMPILE_TEST} PARENT_SCOPE)
endfunction()
