# libfyaml Examples

This directory contains practical examples demonstrating various features of libfyaml.

## Building the Examples

### Using CMake (Recommended)

```bash
cd examples
mkdir build && cd build
cmake ..
cmake --build .
```

The compiled examples will be in the `build/` directory.

### Using Make

```bash
cd examples
make
```

The compiled examples will be in the current directory.

### Manual Compilation

```bash
gcc -o quick-start quick-start.c $(pkg-config --cflags --libs libfyaml)
gcc -o basic-parsing basic-parsing.c $(pkg-config --cflags --libs libfyaml)
# ... and so on for other examples
```

## Examples Overview

### 1. quick-start.c

Example showing:
- Parsing YAML from a file
- Extracting values using `fy_document_scanf()` (path-based queries)
- Modifying the document with `fy_document_insert_at()`
- Emitting the updated YAML

**Usage**:
```bash
./quick-start [config.yaml]
```

**Required YAML structure** (for config.yaml):
```yaml
server:
  host: localhost
  port: 8080
```

---

### 2. basic-parsing.c

Basic YAML parsing and different output formats:
- Parse YAML from file
- Emit as compact flow format
- Emit as standard block YAML
- Emit as JSON

**Usage**:
```bash
./basic-parsing [file.yaml]
```

Works with any valid YAML file.

---

### 3. path-queries.c

Query YAML documents using path expressions:
- Extract multiple values with `fy_document_scanf()`
- Query individual nodes with `fy_node_by_path()`
- Compare node values with strings
- Handle missing paths gracefully

**Usage**:
```bash
./path-queries
```

This example uses inline YAML, so no external file is needed.

---

### 4. document-manipulation.c

Example of document manipulation:
- Read existing values from YAML
- Update values (replace invoice number)
- Add new fields (spouse, delivery address)
- Emit with sorted keys

**Usage**:
```bash
./document-manipulation [invoice.yaml]
```

**Expected YAML structure** (for invoice.yaml):
```yaml
invoice: 34843
date: 2001-01-23
bill-to:
  given: Chris
  family: Dumars
  address:
    lines: |
      458 Walkman Dr.
      Suite #292
```

If no file is provided, the example uses built-in default data.

---

### 5. event-streaming.c

Demonstrates the low-level event-based API:
- Create parser and set input
- Process events one by one
- Handle different event types (scalars, mappings, sequences)
- Memory-efficient parsing for large files

**Usage**:
```bash
./event-streaming [file.yaml]
```

This is the most memory-efficient way to parse YAML, suitable for very large files.

---

### 6. build-from-scratch.c

Create YAML documents from scratch:
- Create empty document
- Build complex structures using `fy_node_buildf()` with printf-style formatting
- Add fields programmatically
- Emit as both JSON and YAML

**Usage**:
```bash
./build-from-scratch
```

No input file needed - generates a complete configuration document.

---

## Sample YAML Files

Sample YAML files are provided for testing:

- **config.yaml** - Simple server configuration
- **invoice.yaml** - Invoice document for manipulation example

Create these files in the examples directory, or the programs will use sensible defaults.

### Sample config.yaml

```yaml
server:
  host: localhost
  port: 8080
  ssl: true
  max_connections: 100

database:
  host: db.example.com
  port: 5432
  name: production_db

logging:
  level: info
  file: /var/log/app.log
```

### Sample invoice.yaml

```yaml
invoice: 34843
date: 2001-01-23
bill-to:
  given: Chris
  family: Dumars
  address:
    lines: |
      458 Walkman Dr.
      Suite #292
    city: Royal Oak
    state: MI
    postal: 48046
ship-to:
  given: Chris
  family: Dumars
product:
  - sku: BL394D
    quantity: 4
    description: Basketball
    price: 450.00
  - sku: BL4438H
    quantity: 1
    description: Super Hoop
    price: 2392.00
tax: 251.42
total: 4443.52
comments: >
  Late afternoon is best.
  Backup contact is Nancy
  Billsmer @ 338-4338.
```

## API Reference

For complete API documentation, visit: https://pantoniou.github.io/libfyaml/

## Common Patterns

### Error Handling

All examples demonstrate proper error handling:
```c
struct fy_document *fyd = fy_document_build_from_file(NULL, "file.yaml");
if (!fyd) {
    fprintf(stderr, "Failed to parse YAML\n");
    return EXIT_FAILURE;
}

// ... use document ...

fy_document_destroy(fyd);  // Always clean up
```

### Path-Based Queries

Extract multiple values efficiently:
```c
int count = fy_document_scanf(fyd,
    "/path1 %s "
    "/path2 %d",
    string_var, &int_var);

if (count != 2) {
    // Handle error
}
```

### Document Modification

Add or update fields:
```c
fy_document_insert_at(fyd, "/parent/path", FY_NT,
    fy_node_buildf(fyd, "key: value"));
```

### Output Formatting

Choose output mode based on needs:
```c
// Compact JSON
fy_emit_document_to_fp(fyd, FYECF_MODE_JSON, stdout);

// Pretty YAML with sorted keys
fy_emit_document_to_fp(fyd, FYECF_MODE_BLOCK | FYECF_SORT_KEYS, stdout);

// One-line flow format
fy_emit_document_to_fp(fyd, FYECF_MODE_FLOW_ONELINE, stdout);
```

## License

All examples are released under the MIT License, same as libfyaml.

Copyright (c) 2019-2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
