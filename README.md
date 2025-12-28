# libfyaml

[![CI Status](https://github.com/pantoniou/libfyaml/workflows/Standard%20Automake%20CI/badge.svg)](https://github.com/pantoniou/libfyaml/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Language: C](https://img.shields.io/badge/language-C-brightgreen.svg)](https://en.wikipedia.org/wiki/C_(programming_language))

**A fully-featured YAML 1.2 and JSON parser/writer with zero-copy operation and no artificial limits.**

libfyaml is designed for high performance with zero content duplication, supporting arbitrarily large documents without the 1024-character limit on implicit keys found in other parsers. It passes the complete YAML test suite and provides both event-based (streaming) and document tree APIs for maximum flexibility.

**Fully MIT Licensed** - v0.9.1 removes all GPL code, making the entire library consistently MIT licensed for use in commercial and proprietary projects.

**Key Highlights:**
- **Zero-copy architecture** - minimal memory usage, handles arbitrarily large documents
- **YAML 1.2 & JSON** - full spec compliance, passes complete YAML test suite
- **Dual API** - event-based streaming (like libyaml) or document tree manipulation
- **No artificial limits** - unlike libyaml's 1024-char implicit key restriction
- **High performance** - up to 24x faster on large files with streaming mode, thread-safe lockless allocators
- **Rich manipulation** - path expressions, scanf/printf-style APIs, programmatic document building
- **Production-ready** - memory-safe, valgrind-clean, extensive test coverage

---

## Quick Start

Parse, query, and modify YAML documents with ease:

```c
#include <libfyaml.h>

// Parse YAML from string or file
struct fy_document *fyd = fy_document_build_from_file(NULL, "config.yaml");

// Extract values using path-based scanf
unsigned int port;
char hostname[256];
fy_document_scanf(fyd, "/server/port %u /server/host %255s", &port, hostname);

// Modify document using path-based insertion
fy_document_insert_at(fyd, "/server", FY_NT, fy_node_buildf(fyd, "timeout: 30"));

// Emit as YAML or JSON
fy_emit_document_to_file(fyd, FYECF_SORT_KEYS, "config.yaml");
fy_document_destroy(fyd);
```

---

## What's New in v0.9.1

**Major Updates:**

- **GPL Code Removed**: All GPL-licensed code has been replaced with MIT-licensed implementations. The entire library is now consistently MIT licensed.
- **Performance**: Up to 24x faster on large files with new streaming mode default in fy-tool
- **Thread Safety**: New thread-safe lockless allocators for multi-threaded applications
- **Stability**: Fixed 12 critical bugs including segmentation faults and memory corruption
- **New APIs**: Parser checkpointing, composer interface, document builder, YPath set operators
- **Build System**: CMake now at feature parity with autotools, cross-compilation support

See [CHANGELOG.md](CHANGELOG.md) for complete release notes.

---

## Why libfyaml?

### Zero-Copy Efficiency
Content is never duplicated internally. libfyaml uses "atoms" that reference source data directly, keeping memory usage low and enabling handling of gigabyte-sized documents without performance degradation.

### No Artificial Limits
Unlike libyaml's 1024-character limit on implicit keys, libfyaml has no hardcoded restrictions. Parse real-world YAML documents of any complexity without worrying about hitting parser limits.

### Dual API Design
Choose the right API for your use case:
- **Event-based** (streaming): For memory-efficient processing of large streams
- **Document tree**: For convenient manipulation, queries, and modifications

Both APIs coexist and can be mixed as needed.

### Powerful Manipulation
- **Path expressions** (YPATH): XPath-like queries (`/foo/bar`, `/items/[0]`)
- **scanf/printf style**: Extract and build data with format strings
- **Programmatic building**: Construct documents from scratch with simple APIs
- **Rich emitter**: Output as YAML (block/flow), JSON, with colors and formatting options

### Production Quality
- **Memory safe**: Valgrind-clean, no leaks under normal operation
- **Accurate errors**: Compiler-format diagnostics that integrate with IDEs
- **Fully tested**: Passes complete YAML 1.2 test suite

---

## Features

### YAML & JSON Support
- Full YAML 1.2 specification compliance
- YAML 1.3 compatibility (preparing for upcoming spec)
- JSON parsing and emission
- Comment preservation (experimental)
- Complete test suite coverage

### Parsing Modes
- Event-based streaming parser (like libyaml)
- Document tree builder with full manipulation API
- Configurable resolution of anchors and merge keys
- Multiple input sources: files, strings, file pointers, streams

### Document Manipulation
- Path-based queries and modifications (YPATH)
- scanf/printf-style data extraction and insertion
- Programmatic document and node creation
- Clone, copy, and merge operations
- Anchor and alias management
- Mapping key sorting

### Output & Emission
- Multiple output modes: original, block, flow, JSON
- ANSI color output support
- Visible whitespace mode for debugging
- Configurable indentation and line width
- Streaming or buffered emission

### Performance & Scalability
- Zero-copy operation on mmap'd files and constant strings
- No artificial limits on key lengths, nesting depth, or document size
- Thread-safe lockless allocators (linear, mremap, dedup, auto) for multi-threaded applications
- Streaming mode with up to 24x performance improvement on large files
- Configurable memory allocators for different usage patterns
- Efficient handling of arbitrarily large documents

### Developer Experience
- Single header inclusion: `<libfyaml.h>`
- Opaque pointers - stable ABI across versions
- Descriptive error messages in compiler format
- Extensive API documentation
- Multiple detailed examples

---

## Installation

### Using CMake (Recommended)

Add libfyaml to your CMake project:

```cmake
find_package(libfyaml 0.9.1 REQUIRED)
target_link_libraries(your_app PRIVATE libfyaml::libfyaml)
```

Check for optional features:
```cmake
if(libfyaml_HAS_LIBCLANG)
    message(STATUS "Reflection support available")
endif()
```

### Using pkg-config

```bash
# Compile flags
pkg-config --cflags libfyaml

# Linker flags
pkg-config --libs libfyaml
```

In your Makefile:
```make
CFLAGS += $(shell pkg-config --cflags libfyaml)
LDFLAGS += $(shell pkg-config --libs libfyaml)
```

### Building from Source

**Using CMake:**
```bash
mkdir build && cd build
cmake ..
cmake --build .
ctest                    # Run tests
cmake --install .        # Install
```

CMake configuration options:
```bash
cmake -DBUILD_SHARED_LIBS=ON \        # Build shared libraries (default: ON)
      -DENABLE_ASAN=OFF \             # AddressSanitizer for debugging (default: OFF)
      -DENABLE_STATIC_TOOLS=OFF \     # Static tool executables (default: OFF)
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..
```

**Using Autotools:**
```bash
./bootstrap.sh
./configure
make
make check               # Run tests
sudo make install
```

### Prerequisites

**Debian/Ubuntu:**
```bash
sudo apt install gcc autoconf automake libtool git make libltdl-dev pkg-config check
```

**Optional dependencies:**
- `libyaml-dev` - for comparison testing
- `llvm-dev libclang-dev` - for reflection support (experimental)

---

## Usage Examples

### Level 1: Basic Parsing

Parse and extract data from a YAML file:

```c
#include <stdlib.h>
#include <stdio.h>
#include <libfyaml.h>

int main(void) {
    struct fy_document *fyd = fy_document_build_from_file(NULL, "config.yaml");
    if (!fyd) {
        fprintf(stderr, "Failed to parse YAML\n");
        return EXIT_FAILURE;
    }

    // Get root node and emit
    fy_emit_document_to_fp(fyd, FYECF_MODE_FLOW_ONELINE, stdout);

    fy_document_destroy(fyd);
    return EXIT_SUCCESS;
}
```

### Level 2: Path-Based Queries

Extract specific values using YPATH expressions:

```c
#include <libfyaml.h>

int main(void) {
    const char *yaml =
        "server:\n"
        "  host: localhost\n"
        "  port: 8080\n"
        "  ssl: true\n";

    struct fy_document *fyd = fy_document_build_from_string(NULL, yaml, FY_NT);

    // Extract multiple values at once
    char host[256];
    unsigned int port;
    fy_document_scanf(fyd,
        "/server/host %255s "
        "/server/port %u",
        host, &port);

    printf("Server: %s:%u\n", host, port);

    // Query single node by path
    struct fy_node *ssl_node = fy_node_by_path(
        fy_document_root(fyd), "/server/ssl", FY_NT, FYNWF_DONT_FOLLOW);
    if (ssl_node && fy_node_compare_string(ssl_node, "true", FY_NT) == 0) {
        printf("SSL is enabled\n");
    }

    fy_document_destroy(fyd);
    return 0;
}
```

### Level 3: Document Manipulation

Modify existing documents programmatically:

```c
#include <libfyaml.h>

int main(void) {
    struct fy_document *fyd = fy_document_build_from_file(NULL, "invoice.yaml");

    // Read current invoice number
    unsigned int invoice_nr;
    char given_name[256];
    int count = fy_document_scanf(fyd,
        "/invoice %u "
        "/bill-to/given %255s",
        &invoice_nr, given_name);

    if (count == 2) {
        printf("Processing invoice #%u for %s\n", invoice_nr, given_name);

        // Update invoice number
        fy_document_insert_at(fyd, "/invoice", FY_NT,
            fy_node_buildf(fyd, "%u", invoice_nr + 1));

        // Add new fields
        fy_document_insert_at(fyd, "/bill-to", FY_NT,
            fy_node_buildf(fyd, "spouse: %s", "Jane"));

        fy_document_insert_at(fyd, "/bill-to", FY_NT,
            fy_node_buildf(fyd,
                "delivery-address:\n"
                "  street: 123 Main St\n"
                "  city: Springfield\n"));

        // Save with sorted keys
        fy_emit_document_to_file(fyd,
            FYECF_DEFAULT | FYECF_SORT_KEYS,
            "invoice_updated.yaml");
    }

    fy_document_destroy(fyd);
    return 0;
}
```

### Level 4: Event-Based Streaming

Process large YAML files with minimal memory:

```c
#include <libfyaml.h>

int main(void) {
    struct fy_parser *fyp = fy_parser_create(NULL);
    if (!fyp)
        return EXIT_FAILURE;

    // Set input
    fy_parser_set_input_file(fyp, "large_file.yaml");

    // Process events
    struct fy_event *fye;
    while ((fye = fy_parser_parse(fyp)) != NULL) {
        enum fy_event_type type = fye->type;

        switch (type) {
        case FYET_SCALAR: {
            const char *value = fy_token_get_text0(fy_event_get_token(fye));
            printf("Scalar: %s\n", value);
            break;
        }
        case FYET_MAPPING_START:
            printf("Mapping start\n");
            break;
        case FYET_SEQUENCE_START:
            printf("Sequence start\n");
            break;
        default:
            break;
        }

        fy_parser_event_free(fyp, fye);
    }

    fy_parser_destroy(fyp);
    return 0;
}
```

### Level 5: Building Documents Programmatically

Create YAML documents from scratch:

```c
#include <libfyaml.h>

int main(void) {
    // Create empty document
    struct fy_document *fyd = fy_document_create(NULL);

    // Build root mapping using printf-style formatting
    struct fy_node *root = fy_node_buildf(fyd,
        "application: MyApp\n"
        "version: %d.%d.%d\n"
        "settings:\n"
        "  debug: %s\n"
        "  max_connections: %d\n"
        "  allowed_hosts:\n"
        "    - localhost\n"
        "    - 127.0.0.1\n",
        1, 2, 3,
        "true",
        100);

    fy_document_set_root(fyd, root);

    // Add more fields programmatically
    fy_document_insert_at(fyd, "/settings", FY_NT,
        fy_node_buildf(fyd, "log_level: info"));

    // Output as JSON
    printf("As JSON:\n");
    fy_emit_document_to_fp(fyd, FYECF_MODE_JSON, stdout);

    printf("\nAs YAML:\n");
    fy_emit_document_to_fp(fyd, FYECF_MODE_BLOCK | FYECF_SORT_KEYS, stdout);

    fy_document_destroy(fyd);
    return 0;
}
```

---

## API Overview

libfyaml provides a comprehensive API organized into functional categories:

| Category | Purpose | Key Functions |
|----------|---------|---------------|
| **Parser** | Event-based streaming YAML parsing | `fy_parser_create()`, `fy_parser_parse()`, `fy_parser_set_input_*()` |
| **Parser Checkpointing** | Look-ahead and peek operations | `fy_parser_parse_peek()`, `fy_parser_skip()`, `fy_parser_count_*()` |
| **Composer** | Event-based document composition | `fy_composer_create()`, `fy_composer_process_event()` |
| **Document** | High-level document tree manipulation | `fy_document_build_from_*()`, `fy_document_scanf()`, `fy_document_insert_at()` |
| **Document Builder** | Programmatic document construction | `fy_document_builder_create()`, `fy_document_builder_*()` |
| **Node** | Individual YAML node operations | `fy_node_create_*()`, `fy_node_by_path()`, `fy_node_buildf()`, `fy_node_delete()` |
| **Sequence** | Array/list operations | `fy_node_sequence_iterate()`, `fy_node_sequence_append()` |
| **Mapping** | Key-value dictionary operations | `fy_node_mapping_lookup_*()`, `fy_node_mapping_append()` |
| **Emitter** | YAML/JSON output generation | `fy_emitter_create()`, `fy_emit_document_to_*()`, `fy_emit_body_node()` |
| **Path** | XPath-like YAML queries (YPATH) | `fy_node_by_path()`, `fy_path_exec_execute()` |
| **Anchor** | Anchor and alias management | `fy_document_lookup_anchor()`, `fy_node_create_alias()` |
| **Allocator** | Memory allocation control | `fy_allocator_create()`, `fy_allocator_contains()` |

**See the [complete API documentation](https://pantoniou.github.io/libfyaml/) for detailed reference.**

---

## Tools

libfyaml includes `fy-tool`, a multi-function YAML manipulation utility:

### fy-dump - Parse and Pretty-Print

**Note**: As of v0.9.1, fy-dump uses streaming mode by default for up to 24x better performance on large files. Use `--no-streaming` if you need forward anchor references or strict duplicate key checking.

```bash
# Convert YAML to JSON (streaming mode)
fy-dump -m json config.yaml

# Use document mode for forward anchors
fy-dump --no-streaming -m json config.yaml

# Format with visible whitespace
fy-dump -V -m block messy.yaml

# Pretty-print with sorted keys
fy-dump -s -m block config.yaml > formatted.yaml
```

### fy-filter - Extract Document Parts

```bash
# Extract specific paths
fy-filter --file config.yaml /database/host /database/port

# Filter with complex keys
echo "{ foo: bar }: baz" | fy-filter "/{foo: bar}/"
# Output: baz
```

### fy-join - Merge YAML Documents

```bash
# Merge two configuration files
fy-join base-config.yaml override-config.yaml

# Merge inline YAML
fy-join ">foo: bar" ">baz: qux"
# Output:
# foo: bar
# baz: qux
```

### fy-testsuite - Test Suite Event Format

```bash
# Generate test-suite event stream
echo "foo: bar" | fy-testsuite -
# Output:
# +STR
# +DOC
# +MAP
# =VAL :foo
# =VAL :bar
# -MAP
# -DOC
# -STR
```

**All tools support:**
- Syntax coloring (`--color`)
- Whitespace visualization (`--visible`)
- Anchor resolution (`--resolve`)
- Multiple output modes (`--mode`)
- Flexible input from files, stdin, or inline strings (`">yaml here"`)

---

## Performance & Architecture

### Zero-Copy Design

libfyaml's core design principle is **zero content duplication**:

- **Atoms**: Internal references to source data without copying
- **Memory efficiency**: Only metadata is allocated, content stays in source
- **Scalability**: Handles multi-gigabyte documents with minimal RAM
- **Speed**: No time wasted on redundant memory operations

### Memory Usage

For a typical YAML document:
- **libyaml**: Copies all content into internal structures
- **libfyaml**: References original data, allocates only ~1-5% overhead for structure

### No Artificial Limits

Common parser limitations that libfyaml **does not have**:
- No 1024-char limit on implicit keys (unlike libyaml)
- No hardcoded nesting depth limits
- No document size restrictions
- No key count limits per mapping

Configurable limits exist for safety but can be adjusted or removed entirely.

---

## Documentation

- **[API Reference](https://pantoniou.github.io/libfyaml/)** - Complete API documentation
- **[Examples](examples/)** - Sample code and use cases
- **[CMake Integration](cmake/README-package.md)** - Using libfyaml in CMake projects
- **[Changelog](CHANGELOG.md)** - Version history and release notes

### Building Documentation Locally

```bash
# HTML documentation
make doc-html
# Output: doc/_build/html/index.html

# PDF documentation
make doc-latexpdf
# Output: doc/_build/latex/libfyaml.pdf
```

---

## Platform Support

**Primary Platform**: Linux (Debian/Ubuntu, Fedora, Arch, etc.)
**Also Supported**: macOS (via Homebrew)
**Not Yet Supported**: Windows (contributions welcome!)

**Architectures**: x86, x86_64, ARM, ARM64, and others
**Compilers**: GCC, Clang, ICC

---

## Contributing

We welcome contributions! Here's how to get involved:

1. **Report Issues**: [GitHub Issues](https://github.com/pantoniou/libfyaml/issues)
2. **Submit Pull Requests**: Fork, modify, and submit PRs
3. **Improve Documentation**: Help make libfyaml more accessible
4. **Add Examples**: Share your use cases with the community

### Development Setup

```bash
# Clone repository
git clone https://github.com/pantoniou/libfyaml.git
cd libfyaml

# Build with CMake
mkdir build && cd build
cmake -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
ctest

# Or build with Autotools
./bootstrap.sh
./configure --enable-asan
make check
```

### Running Tests

```bash
# All tests
make check

# Specific test suites
./test/libfyaml.test
./test/testemitter.test

# With valgrind
./scripts/run-valgrind.sh
```

For CMake ctest is supported
```bash
cd build
ctest --progress -j`nproc`
```

---

## Missing Features

Current limitations:
- **Windows support**: Not yet implemented (contributions welcome)
- **Unicode**: UTF-8 only, no wide character input support

---

## License

libfyaml is released under the **MIT License**.

**As of version 0.9.1, all GPL-licensed code has been removed.** The entire library is now consistently MIT licensed, making it suitable for use in commercial and proprietary projects without any GPL licensing concerns.

```
Copyright (c) 2019-2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
```

See [LICENSE](LICENSE) for complete terms.

---

## Acknowledgments

- **YAML Test Suite**: https://github.com/yaml/yaml-test-suite
- **Author**: Pantelis Antoniou [@pantoniou](https://github.com/pantoniou)
- **Contributors**: Thank you to all who have contributed code, bug reports, and feedback!

---

**[Back to Top](#libfyaml)**
