# libfyaml Zephyr module - in-memory YAML 1.2 / JSON parser
# SPDX-License-Identifier: MIT

if(CONFIG_LIBFYAML)

zephyr_library()

set(FY_DIR ${ZEPHYR_CURRENT_MODULE_DIR})

# Public API headers, exposed to the application: #include <libfyaml.h>
zephyr_include_directories(${FY_DIR}/include)

# Private include paths for the library translation units.
zephyr_library_include_directories(
  ${FY_DIR}/zephyr/include   # forced config.h
  ${FY_DIR}/include
  ${FY_DIR}/src
  ${FY_DIR}/src/lib
  ${FY_DIR}/src/util
  ${FY_DIR}/src/xxhash
  ${FY_DIR}/src/allocator
)

zephyr_library_compile_definitions(
  HAVE_CONFIG_H=1
  _GNU_SOURCE=1
)

# libfyaml intentionally uses constructs Zephyr's default warning set flags.
zephyr_library_compile_options(
  -Wno-unused-parameter
  -Wno-sign-compare
)

zephyr_library_sources(
  # --- core library ---
  ${FY_DIR}/src/lib/fy-accel.c
  ${FY_DIR}/src/lib/fy-atom.c
  ${FY_DIR}/src/lib/fy-composer.c
  ${FY_DIR}/src/lib/fy-diag.c
  ${FY_DIR}/src/lib/fy-doc.c
  ${FY_DIR}/src/lib/fy-docbuilder.c
  ${FY_DIR}/src/lib/fy-docstate.c
  ${FY_DIR}/src/lib/fy-dump.c
  ${FY_DIR}/src/lib/fy-emit.c
  ${FY_DIR}/src/lib/fy-event.c
  ${FY_DIR}/src/lib/fy-input.c
  ${FY_DIR}/src/lib/fy-parse.c
  ${FY_DIR}/src/lib/fy-path.c
  ${FY_DIR}/src/lib/fy-token.c
  ${FY_DIR}/src/lib/fy-types.c
  ${FY_DIR}/src/lib/fy-walk.c

  # --- diagnostics ---
  ${FY_DIR}/src/lib/fy-composer-diag.c
  ${FY_DIR}/src/lib/fy-doc-diag.c
  ${FY_DIR}/src/lib/fy-docbuilder-diag.c
  ${FY_DIR}/src/lib/fy-input-diag.c
  ${FY_DIR}/src/lib/fy-parse-diag.c

  # --- utilities ---
  ${FY_DIR}/src/util/fy-blob.c
  ${FY_DIR}/src/util/fy-ctype.c
  ${FY_DIR}/src/util/fy-utf8.c
  ${FY_DIR}/src/util/fy-utils.c

  # --- hashing (xxhash; BLAKE3 deliberately omitted) ---
  ${FY_DIR}/src/xxhash/xxhash.c
)

# Deliberately excluded for the in-memory build:
#   src/allocator/*         - allocator subsystem is used only by the generic
#                             builder (HAVE_GENERIC off); the classic document
#                             tree allocates with plain malloc/free. Dropping it
#                             avoids the mmap/mremap/pthread/sysconf POSIX deps.
#   src/lib/fy-cache.c      - parse cache (inert without HAVE_GENERIC)
#   src/blake3/*            - only used by the parse cache
#   src/thread/fy-thread.c  - only used by BLAKE3's SIMD dispatcher
#   src/generic/*           - generic subsystem (HAVE_GENERIC off)
#   src/reflection/*        - needs libclang (host only)

endif()
