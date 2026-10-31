# EmscriptenCrossCompile.cmake - build for WebAssembly with Emscripten
#
# Usage: set WASM to 32 or 64, then include this file before project().
#
#   cmake -DWASM=32 ..     # wasm32: 32 bit pointers
#   cmake -DWASM=64 ..     # wasm64: 64 bit pointers (needs node 23 or newer)
#
# The compiler is the one of the emsdk. The module finds it from the environment
# that emsdk_env.sh sets (EMSDK), or from emcc in PATH. The tests run under node: the
# node of the emsdk (EMSDK_NODE) when there is one, because it is the one that fits the
# Emscripten, and else the one in PATH.
#
# With emcmake, that sets the toolchain itself, WASM is not needed (for wasm64 add
# -DCMAKE_C_FLAGS=-sMEMORY64=1).

if(NOT WASM)
    return()
endif()

if(NOT WASM STREQUAL "32" AND NOT WASM STREQUAL "64")
    message(FATAL_ERROR "WASM must be 32 or 64, not '${WASM}'")
endif()

# a toolchain is already chosen (emcmake): nothing to do
if(CMAKE_TOOLCHAIN_FILE)
    return()
endif()

set(_emscripten_toolchain "")
if(DEFINED ENV{EMSDK} AND EXISTS "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
    set(_emscripten_toolchain "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
else()
    find_program(_emcc_program NAMES emcc)
    if(_emcc_program)
        get_filename_component(_emcc_real "${_emcc_program}" REALPATH)
        get_filename_component(_emcc_dir "${_emcc_real}" DIRECTORY)
        if(EXISTS "${_emcc_dir}/cmake/Modules/Platform/Emscripten.cmake")
            set(_emscripten_toolchain "${_emcc_dir}/cmake/Modules/Platform/Emscripten.cmake")
        endif()
    endif()
endif()
if(NOT _emscripten_toolchain)
    message(FATAL_ERROR "WASM needs Emscripten: source emsdk_env.sh of the emsdk, or put emcc in PATH")
endif()
set(CMAKE_TOOLCHAIN_FILE "${_emscripten_toolchain}")

# node runs the programs; the try_run tests of CMake use it as the emulator
if(DEFINED ENV{EMSDK_NODE} AND EXISTS "$ENV{EMSDK_NODE}")
    set(NODE_EXECUTABLE "$ENV{EMSDK_NODE}" CACHE FILEPATH "node, that runs the WebAssembly programs")
else()
    find_program(NODE_EXECUTABLE NAMES node nodejs)
endif()
if(NODE_EXECUTABLE)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${NODE_EXECUTABLE}" CACHE STRING "node, that runs the WebAssembly programs")
    if(WASM STREQUAL "64")
        execute_process(COMMAND "${NODE_EXECUTABLE}" --version OUTPUT_VARIABLE _node_version
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        string(REGEX REPLACE "^v([0-9]+).*" "\\1" _node_major "${_node_version}")
        if(_node_major LESS 23)
            message(WARNING "wasm64 programs need node 23 or newer; ${NODE_EXECUTABLE} is ${_node_version}")
        endif()
    endif()
endif()

# 64 bit pointers: the flags are for the compiler and for the linker, which both read CMAKE_C_FLAGS
if(WASM STREQUAL "64")
    set(CMAKE_C_FLAGS_INIT "-sMEMORY64=1")
    set(CMAKE_CXX_FLAGS_INIT "-sMEMORY64=1")
    set(CMAKE_ASM_FLAGS_INIT "-sMEMORY64=1")
endif()

message(STATUS "WebAssembly ${WASM} bit, toolchain: ${CMAKE_TOOLCHAIN_FILE}")
