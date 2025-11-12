# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

libfyaml is a fully-featured YAML 1.2 and JSON parser/writer library written in C. It's designed for high performance with zero-copy operation, supporting arbitrarily large documents without artificial limits. The library passes the complete YAML test suite and includes extensive programmable APIs for document manipulation.

**Key capabilities:**
- Event-based and tree-based parsing/emission
- Zero-copy operation for minimal memory overhead
- YAML path expressions (XPath-like queries)
- **Type-aware processing**: C struct reflection and generic type system for automatic serialization/deserialization (requires libclang)

### Why libfyaml's Approach Matters Today

Modern YAML/JSON requirements have fundamentally changed from simple configuration files to complex API interoperability. Today's software must handle:
- **Sum types** from modern languages (Rust enums, TypeScript unions, Python Union types)
- **Schema evolution** with `oneOf`/`anyOf` patterns in OpenAPI, GraphQL unions
- **AI API responses** with runtime type discrimination (OpenAI, Anthropic, etc.)

Traditional C libraries offer a stark choice: verbose event-based parsing or heap-heavy node-based trees with manual type checking. Neither provides natural sum types.

**libfyaml's solution**: The `fy_generic` type system brings C-native sum types with:
- One type for all values (int, string, array, object)
- Automatic type detection via C11 `_Generic`
- Natural pattern matching with switch statements
- Inline storage for small values (zero allocation)
- Clean, Python/Rust-like syntax in C

**See [doc/WHY-LIBFYAML.md](doc/WHY-LIBFYAML.md) for complete rationale with AI API examples and code comparisons.**

## Build System

This project supports two build systems: **CMake** (recommended for new projects) and **GNU Autotools** (maintained for compatibility).

### CMake Build (Recommended)

Modern CMake-based build with enhanced cross-compilation support and improved dependency detection.

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
cmake --build .

# Run tests
ctest
# Or: make all test
# Or: make check  (autotools-compatible TAP runner)

# Install
cmake --install .
```

#### CMake Configuration Options

Configure options are set using `-D` flags:

```bash
cmake -DENABLE_ASAN=ON \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..
```

Available options:
- `BUILD_SHARED_LIBS` - Build shared libraries (default: ON)
- `ENABLE_ASAN` - Enable AddressSanitizer for memory debugging (default: OFF)
- `ENABLE_STATIC_TOOLS` - Build tools as static executables (default: OFF)
- `ENABLE_PORTABLE_TARGET` - Disable CPU-specific optimizations (default: OFF)
- `ENABLE_NETWORK` - Enable network-based tests (default: ON)
- `BUILD_TESTING` - Build tests (default: ON)

#### Dependency Detection

CMake uses modern `find_package(CONFIG)` for all dependencies with automatic fallbacks:

**LLVM/libclang** (optional, for reflection/type-aware features)
```bash
# CMake will automatically find LLVM via its CMake config files
# If installed in non-standard location:
cmake -DCMAKE_PREFIX_PATH=/usr/lib/llvm-18 ..
# Or:
cmake -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm ..
```

When libclang is found, reflection support is automatically enabled. This provides:
- C type introspection for automatic YAML ↔ struct conversion
- Metadata extraction from source code comments
- Generic type system for runtime type-aware operations

**Without libclang**: Core YAML functionality works normally; only reflection features are unavailable.

**Cross-compilation**: Fully supported! Just set `CMAKE_PREFIX_PATH` to target installations:
```bash
cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake \
      -DCMAKE_PREFIX_PATH="/path/to/target/llvm;/path/to/target/libs" \
      ..
```

#### Using libfyaml in CMake Projects

After installation, downstream CMake projects can use libfyaml with `find_package`:

```cmake
find_package(libfyaml 0.9 REQUIRED)

# Check available features
if(libfyaml_HAS_LIBCLANG)
    message(STATUS "Reflection support available")
endif()

# Link against libfyaml
target_link_libraries(myapp PRIVATE libfyaml::libfyaml)
```

See `cmake/README-package.md` for complete documentation on using libfyaml as a CMake package.

### Autotools Build (Traditional)

Traditional GNU Autotools build system:

```bash
# Bootstrap (required after fresh clone)
./bootstrap.sh

# Configure (see Configure Options below)
./configure

# Build
make

# Run tests
make check

# Install
make install
```

#### Autotools Configure Options

- `--enable-asan` - Enable AddressSanitizer for memory debugging
- `--enable-static-tools` - Build tools as static executables
- `--enable-portable-target` - Disable CPU-specific optimizations (SSE2/AVX2/AVX512/NEON)
- `--enable-network` - Enable network-based tests (downloads YAML test suite via git)
- `--with-libclang` - Enable libclang-based reflection backend (for type-aware features)

### Running Specific Tests

```bash
# Run a single test suite
./test/libfyaml.test          # Core API tests
./test/testerrors.test        # Error handling tests
./test/testemitter.test       # Emitter tests
./test/testreflection.test    # Reflection tests (requires libclang)

# Run specific check test
./test/libfyaml-test
```

### Documentation

```bash
# Build HTML documentation
make doc-html

# Build PDF documentation
make doc-latexpdf
```

The HTML documentation is generated in `doc/_build/html/`.

## Code Architecture

### Main Components

The codebase is organized into functional subsystems under `src/`:

**lib/** - Core YAML parsing and document handling
- `fy-parse.*` - Main YAML parser (event-based)
- `fy-doc.*` - Document tree API (for programmatic manipulation)
- `fy-emit.*` - YAML/JSON emitter with formatting options
- `fy-composer.*` - Composes events into documents
- `fy-docbuilder.*` - Builds documents programmatically
- `fy-path.*` - YAML path expressions (like XPath for YAML)
- `fy-token.*` - Token representation
- `fy-atom.*` - Atomic text chunks (zero-copy references)
- `fy-input.*` - Input handling (files, strings, streams)
- `fy-diag.*` - Diagnostic/error reporting

**allocator/** - Memory allocation subsystem
- Multiple allocator strategies: linear, malloc, mremap, dedup, auto
- Configurable per document/parser for performance tuning

**blake3/** - BLAKE3 cryptographic hash implementation
- Multiple SIMD backends (SSE2/SSE41/AVX2/AVX512/NEON) selected at runtime
- Used internally for deduplication and checksums

**util/** - Utility functions
- `fy-utf8.*` - UTF-8 validation and processing
- `fy-ctype.*` - Character classification
- `fy-list.h` - Intrusive linked lists
- `fy-blob.*` - Binary blob handling

**thread/** - Threading primitives (minimal, mostly for future use)

**generic/** - Generic type system for runtime YAML/JSON representation
- `fy-generic.*` - Core generic type system with pointer tagging
- `fy-generic-encoder.*` - Encode C structures to generic representation
- `fy-generic-decoder.*` - Decode generic representation to C structures
- Memory-efficient: inline storage for small values (integers up to 61 bits, strings up to 7 bytes, 32-bit floats)
- Supports 9 types: null, bool, int, float, string, sequence, mapping, indirect, alias
- **High-level API**: `fy_to_generic()`, `fy_sequence(...)`, `fy_mapping(...)` - ergonomic variadic constructors with automatic type detection
- **Low-level API**: `fy_sequence_alloca()`, `fy_mapping_alloca()` - explicit control over allocation
- **Builder API**: `fy_gb_sequence()`, `fy_gb_mapping()` - heap-allocated variants for persistent data

**reflection/** - C type introspection system (requires libclang)
- `fy-reflection.*` - Main reflection API and type registry
- `fy-clang-backend.*` - libclang-based backend for parsing C headers
- `fy-packed-backend.*` - Serialized reflection data for distribution (no runtime libclang dependency)
- `fy-null-backend.*` - Minimal backend for testing
- `fy-registry.*` - Type lookup and management
- Supports full C type system: structs, unions, enums, bitfields, arrays, pointers, typedefs, qualifiers

**tool/** - `fy-tool` multi-tool implementation
- Symlinked to create: `fy-dump`, `fy-filter`, `fy-testsuite`, `fy-join`, `fy-ypath`, `fy-compose`
- Supports `--reflect` flag for type-aware YAML processing

### Key Design Patterns

**Zero-Copy Operation**: Content is never copied internally. The library uses "atoms" (`fy-atom.*`) that reference source data directly. This keeps memory usage low and enables handling of arbitrarily large documents.

**Opaque Pointers**: All public API types are opaque pointers (`struct fy_document *`, etc.). Users never directly dereference library structures, allowing internal changes without breaking ABI.

**Event-Based and Tree-Based APIs**: The library supports both:
- Event-based parsing (like libyaml) via `fy_parser_*` functions
- Document tree API via `fy_document_*` and `fy_node_*` functions

**Path Expressions**: YAML path syntax (similar to JSONPath) for querying/modifying documents:
```c
fy_document_scanf(fyd, "/invoice %u /bill-to/given %256s", &num, name);
fy_document_insert_at(fyd, "/bill-to", FY_NT, fy_node_buildf(...));
```

**Pluggable Allocators**: Different allocation strategies can be chosen per-document based on usage patterns (streaming, large static documents, etc.).

**Type-Aware Processing**: When libclang is available, the library can introspect C type definitions and automatically serialize/deserialize between YAML documents and C structures. This combines:
- Reflection system: Extracts type metadata from C headers
- Generic type system: Efficient runtime representation of typed data
- Metadata annotations: YAML comments in C code guide the mapping (e.g., `// yaml: { key: id, counter: count }`)
- Backend abstraction: Development uses libclang; distribution can use pre-generated packed blobs

## Generic Type System API

The generic type system provides Python/Rust-like data literals in C with space-efficient representation.

**Quick example:**
```c
fy_generic config = fy_mapping(
    "host", "localhost",
    "port", 8080,
    "users", fy_sequence("alice", "bob", "charlie")
);
```

**Key features:**
- **Immutable values**: Generics are immutable—operations create new values, enabling inherent thread safety
- **Thread-safe by design**: Multiple threads can safely read generics without locks (only allocator requires synchronization)
- **Inline storage**: Small values (61-bit ints, 7-byte strings, 32-bit floats) stored directly—zero allocations
- **Automatic type detection**: `fy_to_generic(42)` uses C11 `_Generic` dispatch
- **Three API variants**:
  - Stack-based (`fy_sequence(...)`, `fy_mapping(...)`): Temporary data, function scope
  - Low-level (`fy_sequence_alloca()`, `fy_mapping_alloca()`): Pre-built arrays
  - Builder (`fy_gb_sequence(gb, ...)`, `fy_gb_mapping(gb, ...)`): Persistent data
- **Automatic internalization**: Mix stack and heap APIs freely—builder copies what it needs
- **Idempotent operations**: Reading same generic always returns same result—perfect for functional programming

**See [doc/GENERIC-API.md](doc/GENERIC-API.md) for complete API documentation, usage patterns, and examples.**

> **Note**: For compile-time C struct reflection, see [Type-Aware Features](#type-aware-features-reflection--generics).

## Library Comparisons

libfyaml compares with other YAML/JSON libraries and language features across multiple dimensions:

**Compared libraries:**
- **rapidyaml** (C++): Zero-copy tree building vs libfyaml's inline storage + dedup allocator
  - rapidyaml excels at parse speed for typical documents
  - libfyaml wins on ultra-large files with repeated content (dedup), small configs (inline), and billion laughs immunity
- **libyaml** (C): Low-level events vs libfyaml's high-level generics (both available in libfyaml)
- **yaml-cpp** (C++): Node-based with heap allocations vs libfyaml's value-based generics with inline storage
- **JSON libraries** (json-c, cJSON, jansson): Verbose manual construction vs libfyaml's Python-like syntax
- **Dynamic languages** (Python, JavaScript, Rust): libfyaml achieves similar ergonomics in C via `_Generic` dispatch and preprocessor metaprogramming

**Architecture philosophies:**
- Memory models: Zero-copy, inline storage + pluggable allocators (dedup!), refcounting, GC, arena/pool
- API designs: Event-based, tree-based, hybrid (libfyaml), functional/declarative
- Type systems: Static typing, tagged union (libfyaml), dynamic typing, opaque pointers

**libfyaml's unique strengths:**
1. Ultra-large YAML files (dedup allocator)
2. Security-critical parsing (billion laughs immunity)
3. Small config performance (inline storage)
4. Adaptive memory management (linear, mremap, dedup, auto allocators)
5. Ergonomic C API (Python/Rust-like syntax)
6. Type-aware processing (reflection)

**See [doc/LIBRARY-COMPARISONS.md](doc/LIBRARY-COMPARISONS.md) for detailed comparisons with code examples, feature tables, and architecture analysis.**

## Public API

The single public header is `include/libfyaml.h`. All public symbols are prefixed with `fy_` or `FY_`.

### Using libfyaml in Your Project

**CMake** (recommended):
```cmake
find_package(libfyaml 0.9 REQUIRED)
target_link_libraries(myapp PRIVATE libfyaml::libfyaml)
```

**pkg-config**:
```bash
pkg-config --cflags libfyaml
pkg-config --libs libfyaml
```

**Compiler flags directly**:
```bash
gcc myapp.c -o myapp -lfyaml
```

## Type-Aware Features (Reflection & Generics)

When built with libclang support, libfyaml provides powerful type-aware capabilities that enable automatic conversion between YAML documents and C structures.

> **Note**: For information about the generic type system API (runtime typed values), see the [Generic Type System API](#generic-type-system-api) section. This section focuses on compile-time type reflection.

### What It Provides

**Reflection System**: Introspects C type definitions to understand struct layouts, field types, and relationships. Extracts metadata from source code comments to guide YAML mapping.

**Integration with Generic Type System**: The reflection system uses the [generic type system](#generic-type-system-api) internally for efficient runtime representation during serialization/deserialization.

**Use Cases**:
- Configuration file parsing into native C structures
- Schema validation against C type definitions
- Type-safe serialization/deserialization
- Automatic documentation from C headers

### Basic Usage

**1. Define C structures with metadata annotations**:
```c
// config.h
struct server_config {
    char *host;
    int port;
    int max_connections;
    char **allowed_ips;    // yaml: { counter: num_ips }
    int num_ips;
};
```

**2. Parse YAML using reflection**:
```bash
# Using fy-tool with reflection
fy-tool --reflect \
    --import-c-file config.h \
    --entry-type "struct server_config" \
    config.yaml
```

**3. In C code**:
```c
// Create reflection from C header
struct fy_reflection *rfl = fy_reflection_from_c_file("config.h", NULL);

// Get type information
const struct fy_type_info *ti = fy_type_info_lookup(rfl, "struct server_config");

// Parse YAML into C structure
struct fy_document *fyd = fy_document_build_from_file(NULL, "config.yaml");
struct server_config config;
fy_document_to_reflection(fyd, rfl, ti, &config);

// Use the config
printf("Server: %s:%d\n", config.host, config.port);

// Cleanup
fy_document_destroy(fyd);
fy_reflection_destroy(rfl);
```

### Metadata Annotations

Guide the YAML ↔ C mapping using YAML-formatted comments:

**Dynamic arrays**:
```c
struct user {
    char *name;
    struct item *items;    // yaml: { counter: item_count }
    int item_count;
};
```

**Map keys**:
```c
struct user_database {
    struct user *users;    // yaml: { key: user_id, counter: user_count }
    int user_count;
};
```
In YAML, this becomes a mapping where `user_id` fields become keys.

**Entry metadata** (for root type):
```bash
fy-tool --reflect \
    --entry-meta "yaml: { key: id, counter: count }" \
    ...
```

### Backends

**Clang Backend** (development):
- Parses C headers using libclang at runtime
- Full access to type information and comments
- Requires libclang installed

**Packed Backend** (distribution):
- Pre-serialize reflection data to binary blob
- No runtime libclang dependency
- Use for deployed applications

**Workflow**:
```bash
# Development: Parse C headers directly
fy-tool --reflect --import-c-file types.h ...

# Production: Generate packed blob during build
fy-tool --reflect --import-c-file types.h --export-packed types.blob

# Production: Use packed blob at runtime
fy-tool --reflect --import-packed types.blob ...
```

### fy-tool Integration

The `fy-tool` utility integrates reflection for command-line type-aware operations:

```bash
# Parse and validate YAML against C types
fy-tool --reflect \
    --import-c-file schema.h \
    --entry-type "struct config" \
    input.yaml

# Convert with type information
fy-tool dump --reflect \
    --import-c-file types.h \
    --entry-type "struct data" \
    data.yaml

# Use packed reflection data
fy-tool --reflect \
    --import-packed types.blob \
    --entry-type "struct config" \
    config.yaml
```

### API Reference

**Creating reflection**:
```c
struct fy_reflection *fy_reflection_from_c_file(const char *file, const struct fy_parse_cfg *cfg);
struct fy_reflection *fy_reflection_from_packed_blob(const void *blob, size_t size, const struct fy_parse_cfg *cfg);
void fy_reflection_destroy(struct fy_reflection *rfl);
```

**Type queries**:
```c
const struct fy_type_info *fy_type_info_lookup(struct fy_reflection *rfl, const char *name);
bool fy_type_kind_is_primitive(enum fy_type_kind type_kind);
const char *fy_type_kind_name(enum fy_type_kind type_kind);
```

**Document integration**:
```c
struct fy_document *fy_document_create_from_reflection(
    struct fy_reflection *rfl,
    const struct fy_type_info *ti,
    const void *data);
```

### Feature Detection

Check if reflection is available:

**CMake**:
```cmake
if(libfyaml_HAS_LIBCLANG)
    message(STATUS "Reflection support available")
endif()
```

**Runtime**:
```c
#ifdef FY_HAS_LIBCLANG
    // Reflection code
#endif
```

### Limitations & Notes

- **Experimental**: Reflection features are marked experimental until v1.0
- **libclang dependency**: Development-time dependency on LLVM/Clang (use packed backend to avoid runtime dependency)
- **Platform support**: Tested on Linux and macOS; cross-compilation supported
- **Type coverage**: Supports full C type system including bitfields, unions, anonymous types

## Testing

**test/libfyaml-test.c** - Main test suite using the check framework
- `libfyaml-test-core.c` - Core functionality tests
- `libfyaml-test-meta.c` - Meta operations tests
- `libfyaml-test-emit.c` - Emitter tests
- `libfyaml-test-allocator.c` - Allocator tests
- `libfyaml-test-private.c` - Private API tests (only with static builds)

**test/*.test** - Shell-based test drivers for different test suites:
- Uses official YAML test suite (cloned from GitHub during test runs if `--enable-network`)
- Emitter round-trip tests using files in `test/emitter-examples/`
- Error handling tests using files in `test/test-errors/`
- Reflection tests using files in `test/reflection-data/`

**test/reflection-data/** - Reflection/type-aware test suite (requires libclang):
- Contains ~107 test directories, each testing YAML ↔ C structure conversion
- Test structure for each case:
  - `===`: Test description
  - `definition.h`: C type definitions (optional for primitive types)
  - `entry`: Entry type name (e.g., "int", "struct foo")
  - `meta`: Metadata annotations (optional)
  - `in.yaml`: Input YAML document
  - `test.event`: Expected output in event format
  - `env`: Environment requirements (word sizes, signedness)
- Tests round-trip conversion and validates type-aware parsing/emission
- Platform-specific filtering (32/64-bit, char signedness)

**scripts/** - Helper scripts for testing:
- `run-valgrind.sh` - Run tests under valgrind
- `run-massif.sh` - Memory profiling
- `run-compare-*.sh` - Compare with libyaml output

## Internal Tools

**fy-tool** - Multi-function YAML manipulation tool (behavior changes based on argv[0]):
- `fy-dump` - Parse and pretty-print YAML/JSON
- `fy-filter` - Extract parts of YAML documents using paths
- `fy-testsuite` - Generate test-suite event format
- `fy-join` - Join multiple YAML documents
- `fy-ypath` - YAML path operations
- `fy-compose` - Compose operations

**Reflection support in fy-tool** (when built with libclang):
```bash
# Type-aware parsing with C header
fy-tool --reflect \
    --import-c-file schema.h \
    --entry-type "struct config" \
    config.yaml

# Using packed reflection blob
fy-tool --reflect \
    --import-packed schema.blob \
    --entry-type "struct config" \
    config.yaml

# With metadata annotations
fy-tool --reflect \
    --import-c-file types.h \
    --entry-type "struct data" \
    --entry-meta "yaml: { key: id }" \
    data.yaml
```

**Internal test programs** (noinst_PROGRAMS, only built with static linking):
- `libfyaml-parser` - Parser comparison tool (requires libyaml)
  - Contains comprehensive examples of generic API usage (see `src/internal/libfyaml-parser.c`)
  - Demonstrates `fy_sequence()`, `fy_mapping()`, `fy_to_generic()` patterns
- `fy-thread` - Threading tests
- `fy-b3sum` - BLAKE3 checksum utility
- `fy-allocators` - Allocator benchmarking
- `fy-generics` - Generic type system tests and benchmarks
  - Tests all generic API variants (stack, heap, inline, out-of-place)
  - Performance benchmarks for different allocation strategies

## Development Notes

- The codebase targets YAML 1.2 spec and aims for YAML 1.3 compatibility
- Error messages follow standard compiler format for IDE integration
- Valgrind-clean: no memory leaks under normal operation
- Thread-safe parsing is supported (with appropriate allocator configuration)
- The library is primarily developed on Linux but supports macOS via Homebrew

## CMake Build System Documentation

The `cmake/` directory contains comprehensive documentation about the CMake build system:

- **`cmake/README-package.md`** - Complete guide for using libfyaml in CMake projects
  - Basic usage with `find_package()`
  - Feature detection (libclang, libyaml support)
  - Version requirements
  - Cross-compilation
  - Complete examples

- **`cmake/IMPROVEMENTS.md`** - Technical documentation of CMake improvements
  - Modernized dependency detection (CMake config files)
  - Enhanced package configuration
  - Cross-compilation support details
  - Migration guide from pkg-config

- **`cmake/libfyaml-config.cmake.in`** - Package configuration template
  - Exports `libfyaml::libfyaml`, `libfyaml::fyaml`, and `fyaml` targets
  - Provides `libfyaml_HAS_LIBCLANG` and `libfyaml_HAS_LIBYAML` feature flags
  - Automatically handles dependencies (Threads, LLVM/Clang)

## SIMD Optimizations

BLAKE3 hashing uses runtime CPU detection to select optimal SIMD implementation:
- x86/x86_64: SSE2, SSE4.1, AVX2, AVX512
- ARM/ARM64: NEON
- Fallback: Portable C implementation

Disable with `--enable-portable-target` (autotools) or `-DENABLE_PORTABLE_TARGET=ON` (CMake) for maximum compatibility.
