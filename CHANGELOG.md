# Changelog

All notable changes to libfyaml will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

[0.9.1]: https://github.com/pantoniou/libfyaml/compare/v0.9...v0.9.1
[0.9]: https://github.com/pantoniou/libfyaml/releases/tag/v0.9
