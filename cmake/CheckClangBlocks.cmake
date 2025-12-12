# --- CheckClangBlocks.cmake ---
# Detect support for Clang Blocks (Clang or AppleClang)
# Defines: HAVE_CLANG_BLOCKS (0 or 1)
# Defines: CLANG_BLOCKS_LIB (empty or "BlocksRuntime")

# Default
set(HAVE_CLANG_BLOCKS 0)
set(CLANG_BLOCKS_LIB "")

if (CMAKE_C_COMPILER_ID MATCHES "Clang")

    # AppleClang: Blocks are built-in
    if (CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
        message(STATUS "AppleClang detected — Blocks support is built in")

        set(HAVE_CLANG_BLOCKS 1)
        set(CLANG_BLOCKS_LIB "")

    else()
        # LLVM Clang: must test for -fblocks and libBlocksRuntime
        message(STATUS "LLVM Clang detected — checking for BlocksRuntime")

        include(CheckCSourceCompiles)

        set(CMAKE_REQUIRED_FLAGS "-fblocks")
        set(CMAKE_REQUIRED_LIBRARIES "BlocksRuntime")

        check_c_source_compiles("
            #include <Block.h>
            int main(void) {
                void (^b)(void) = ^{ };
                b();
                return 0;
            }
        " HAVE_WORKING_CLANG_BLOCKS)

        unset(CMAKE_REQUIRED_FLAGS)
        unset(CMAKE_REQUIRED_LIBRARIES)

        if (HAVE_WORKING_CLANG_BLOCKS)
            message(STATUS "Clang Blocks support ENABLED")
            set(HAVE_CLANG_BLOCKS 1)
            set(CLANG_BLOCKS_LIB "BlocksRuntime")
        else()
            message(WARNING
                "libBlocksRuntime not found — Clang Blocks disabled")
            set(HAVE_CLANG_BLOCKS 0)
            set(CLANG_BLOCKS_LIB "")
        endif()

    endif()

endif()
