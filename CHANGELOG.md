# Changelog

All notable changes to libfyaml will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.9.4] - 2026-02-03

### Major: Full Windows Support

This release adds **full native Windows support**. libfyaml now builds and runs natively on Windows with MSVC, clang-cl, and Clang compilers.

**Native Windows builds:**
- Full support for building on Windows using MSVC, clang-cl, or GCC
- Works with Visual Studio, VSCode, and other Windows development tools
- All tests pass on Windows

**Cross-compilation from Linux:**
- Support for msvc-wine to install MSVC redistributables on Linux
- Compile using `cl.exe` via Wine
- Compile using `clang-cl` without Wine
- Compile using Clang cross-compilation triplet

### Major: Comment Support Now Stable

Comment parsing and emission has been moved out of experimental status. Comments can now be reliably preserved and manipulated through the API.

### Added

- `fy_node_set_style()`: Set the style of a node (block, flow, plain, etc.) - Fixes #78
- `fy_token_set_comment()`: Attach comments to tokens programmatically
- `fy_event_to_string()`: Convert events to string representation
- `fy_diag_get_collect_errors()`: Query if error collection is enabled
- `fy_atom_lines_containing()`: Get lines containing an atom (for diagnostics)
- `fy_memstream`: Portable `open_memstream` alternative for cross-platform support
- CMake-based CI workflow with improved matrix coverage
- Emscripten platform detection for `endian.h`

### Changed

- libclang now defaults to OFF (will be enabled when reflection features are ready)
- Document start token is now preserved (may contain comments)
- Walk methods now handle error paths more systematically
- Removed non-existent experimental function declarations from `libfyaml.h`

### Fixed

- **#193**: Token creation now properly clears memory to avoid undefined behavior on invalid input
- **#186**: Reference loop nesting now respected when checking link validity
- **#185**: Fixed crash when setting document root to NULL; also fixed input size clamping for corrupted input
- **#184**: Walk memory leak fix with improved debugging infrastructure
- **#183, #191**: Error out early on `FYECF_EXTENDED_CFG` with helper emit methods (prevents crash)
- **#182**: Walk expression unref bug fix with debug infrastructure
- **#181**: Walk double-free on node delete
- **#178, #177**: Walk methods now handle error paths systematically (recursive alias resolution)
- **#176**: Off-by-one error in `fy_accel_grow`
- **#175**: Parser crash on corrupted UTF-8 at end of file
- **#174**: Superfluous document end marker with explicit version/tag directives
- **#173, #172**: Depth limit for node copy (prevents stack overflow under fuzzing)
- **#143**: Document root now correctly marked as attached
- Emit state now properly reset at end of document (fixes multi-document stream markers)
- Flow quoting error on ANY style (test was backwards)
- Empty file `fdopen` issue on some platforms
- Empty stream `realloc(0)` undefined behavior
- Removed jarring notice when alias is declared multiple times (valid YAML)

### Platform Support

**Supported platforms**: Linux, macOS, FreeBSD, OpenBSD, NetBSD, and **Windows**.

**Windows-specific:**
- Full native MSVC support (32-bit and 64-bit)
- clang-cl and Clang cross-compilation support
- msvc-wine support for Linux-based Windows cross-compilation
- Proper CRLF (DOS line ending) handling
- Fixed 32-bit MSVC intrinsics (`_BitScanForward64`, `_BitScanReverse64`, `__popcnt64`)

**Portability fixes:**
- Fixed void pointer arithmetic (GCC extension) for strict C compliance
- Fixed GCC ternary operator extension (`x ? : y` -> `x ? x : y`)
- Fixed `\e` escape sequence (GCC/clang extension) -> `\x1b`
- Fixed enum comparison warnings across platforms
- Align mremap initial size to page boundary (fixes BSD crashes)

**macOS:**
- Fixed ASAN support (requires `-fsanitize=address` at link time)
- Added extra ASAN flags for Apple's clang (alloca poisoning disabled)

**CI/Build:**
- New CMake-based GitHub Actions workflow
- Improved build matrix coverage
- Fixed distcheck breakage

### Internal

- Walk expression debug infrastructure for easier debugging
- Portable `fy_memstream` wrapper for `open_memstream`
- Use `fy_align_alloc/free` wrappers in allocator
- Fixed allocator `get_caps` return type (enum, not int)
- Atomic counter function instead of macro
- General warning cleanup pass

### Statistics

- 58 commits since v0.9.3
- 18 bug fix issues closed
- Full Windows platform support added

## [0.9.3] - 2026-01-14

### Added

- fy-tool: `-winf` option for infinite width output
- fy-tool: `--no-output-newline` option (useful with oneline mode)
- Emitter: `FYEXCF_OUTPUT_FILENAME` extended option for direct file output

### Changed

- JSON emit mode now works like YAML flow mode (less special-casing)
- Compact emit mode now produces truly compact output (no indentation)
- Emitter only changes plain scalar style when space or linebreak is present (preserves numeric scalars better)

### Fixed

- **#170**: Wrong length return when buffer ends mid UTF-8 sequence
- **#167**: Oneline styles now correctly use infinite width
- **#139**: Walk path parser now always creates diagnostics (fixes token reference leak)
- **#137**: Walk memory leak on parse error path
- **#136**: Walk memory leak when no output is generated
- **#131**: Walk memory leak when expression error occurs
- Composer: Memory leak when back-to-back complex keys exist
- Emitter: Broken flow mode output
- Emitter: Unnecessary plain scalar style changes

### Platform Support

**Supported platforms**: Linux, macOS, FreeBSD, OpenBSD, and NetBSD.

- **NetBSD**: Disabled mremap (different semantics than Linux)
- **NetBSD**: CMake shared libraries now correctly built with -fPIC
- **NetBSD**: Only include alloca.h if it exists
- **NetBSD**: Fixed ctype(3) argument casting (unsigned char)
- **GNU/Hurd**: Use `<endian.h>` for endianness detection
- Use `getopt_long` instead of `getopt_long_only` for portability
- Fix test(1) operator in test suite (use POSIX `=` not bash `==`)
- Fix `-Wformat` warning (use PRIx64 for uint64_t)

### Statistics

- 25 commits since v0.9.2
- 6 bug fix issues closed

## [0.9.2] - 2025-12-29

### Fixed
- **#156**: automake: Respect --disable-static and don't build internals
- **#157**: dist tarball now includes the missing CMake files

## [0.9.1] - 2025-12-28

### CRITICAL: Licensing Change

**GPL Code Removed - Library Now Fully MIT Licensed**

Replaced GPL-licensed list implementation with clean-room MIT-licensed minimal
implementation. The entire library is now consistently MIT licensed without any
GPL components. This is a major change for users requiring permissive licensing
for commercial or proprietary projects.

### Added

- Thread-safe lockless allocator infrastructure (linear, malloc, mremap, dedup, auto)
- Parser checkpointing API: `fy_parser_parse_peek()`, `fy_parser_skip()`, `fy_parser_count_sequence_items()`, `fy_parser_count_mapping_items()`
- Public composer and document builder interfaces
- YPath set operators: `@` for inclusion, `!` for exclusion
- Compact emitter formats (flow and JSON)
- `fy_emit_body_node()` for emitting single nodes without stream/doc events
- `fy_event_report()` methods for error diagnostics in composer interface
- `fy_node_get_tag0()` and `fy_node_get_tag_length()` tag utility methods
- `fy_node_delete()` method for explicit node removal
- `fy_allocator_contains()` for pointer containment checks
- `fy_event_get_type()` convenience wrapper
- CMake documentation build targets (doc-html, doc-latexpdf, doc-man)
- Cross-compilation support for both CMake and autotools build systems
- Optional libclang dependency detection for type-aware features
- `--enable-static-tools` configuration option
- `examples/` directory with library usage examples
- Comprehensive allocator and parser tests

### Changed

- **fy-tool now uses streaming mode by default** for dump operations (up to 24x faster on large files: 105MB in 0.776s vs 18.3s; document mode available via `--no-streaming`)
- Emission performance improved up to 2x through token analysis optimization
- UTF-8 handling significantly faster
- Plain scalar parsing and fetch reworked for better performance
- fy-tool no longer wraps output when stdout is not a TTY
- fy-tool now properly calls `fy_shutdown()` at exit for clean valgrind runs
- CMake build system now at feature parity with autotools
- Stream YAML version handling now per-stream (not per-document)
- Split diagnostics into separate modules by type
- Reorganized fy-tool directory structure
- Renamed `alloca_vsprintf` to `fy_vsprintfa`
- Improved block scalar end linebreak and whitespace handling

### Fixed

- **#133**: Segmentation fault in `fy_reader_*` when all input processed
- **#135**: Use-after-free when document acceleration fails
- **#134**: Double-free in `fy_node_setup_path_expr_data`
- **#132**: Use-after-free after document resolution
- **#123**: Crash when assertions compiled out in release mode
- **#122**: Crash when addressing expression as node incorrectly
- **#120**: Crash on error during walk evaluation
- **#119**: Segmentation fault on garbage input in `evaluate_new()`
- **#118**: Segmentation fault when block scalar indicators last in input
- **#115**: Assertion preventing node deletion via `fy_document_insert_at()`
- **#107**: Crash when comparing mappings with NULL values
- **#102**: Portability crash on NEC Aurora (enums in bitfields)
- Infinite loop in scan directive for malformed UTF-8 input
- Use-after-free in auto allocator (sub-allocator freeing order)
- Use-after-free in anchor setup before resolution
- Composer halt teardown event pumping
- Document state lifecycle in presence of errors
- Parser reset queued inputs initialization
- Blank/whitespace-only scalar creation via emit event API
- Zero-length scalars as plain mapping keys
- Prefix-only tags passing scan validation
- Plain scalars with linebreaks creation
- Pretty mode document end marker forcing
- MacOS `LIST_HEAD` warning and linker options
- Numerous compiler warnings at high optimization levels
- Testsuite failures with latest jq versions

### Performance

- fy-tool: Up to 24x faster on large files (streaming mode by default)
- Emission: ~2x faster (optimized plain scalar token analysis)
- Parsing: Faster plain scalar fetch with inline specialization
- UTF-8: Significantly improved validation and processing speed

### Internal

- New atomic helpers (fy-atomics) based on C atomics with fallbacks
- fy-shutdown mechanism for library cleanup
- FY_IMPOSSIBLE_ABORT() macro for impossible conditions
- Variable size encoding header (fy-vlsize)
- CPP varargs macros for recursive expansion
- iovec copy/from/xxhash methods
- FY_LAMBDA define for lambda support detection
- Enhanced blob infrastructure
- Improved utility functions (overflow checks, alloca sprintf, constructors)

### API Compatibility

- **Minor breakage**: Added `minimum_bucket_occupancy` to dedup allocator config
  (no known external users affected)
- **Behavioral change**: fy-tool now defaults to streaming mode instead of document
  mode (24x faster, but loses forward anchor references and strict duplicate key
  checking; use `--no-streaming` for old behavior)
- All other changes are backwards compatible additions

### Contributors

Pantelis Antoniou, Alessandro Astone, Kevin Wooten, Henry Qin, Roland Wirth,
Yurii Rashkovskii, Alexandre Detiste, Benjamin Rodenberg,
Jose Luis Blanco-Claraco, Andrey Somov, Orange_233, Martin Diehl

### Statistics

- 171 commits since v0.9
- 12 bug fix issues closed
- 12 contributors

## [0.9] - 2023-09-28

Initial public release with comprehensive YAML 1.2 support.

[0.9.4]: https://github.com/pantoniou/libfyaml/compare/v0.9.3...v0.9.4
[0.9.3]: https://github.com/pantoniou/libfyaml/compare/v0.9.2...v0.9.3
[0.9.2]: https://github.com/pantoniou/libfyaml/compare/v0.9.1...v0.9.2
[0.9.1]: https://github.com/pantoniou/libfyaml/compare/v0.9...v0.9.1
[0.9]: https://github.com/pantoniou/libfyaml/releases/tag/v0.9
