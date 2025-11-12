# Generic Type System API

The generic type system provides a space-efficient runtime representation for YAML/JSON data. It uses pointer tagging and inline storage to minimize allocations while supporting automatic type conversion.

> **Note**: The generic type system works with runtime typed values. For compile-time C struct reflection and automatic serialization, see the Type-Aware Features (Reflection & Generics) section in CLAUDE.md.

## Overview

**What it provides:**
- Single `fy_generic` type that can hold any YAML value (null, bool, int, float, string, sequence, mapping)
- Automatic type detection via C11 `_Generic` dispatch
- Inline storage for small values (no allocation needed)
- Stack-based collections for temporary data (via `alloca`)
- Heap-based collections for persistent data (via builder)

**Key feature:** Small values are stored inline within a pointer-sized value:
- Integers up to 61 bits (64-bit systems) or 29 bits (32-bit systems)
- Strings up to 6-7 bytes
- 32-bit floating-point values
- No heap allocation or cleanup required

## Immutability and Thread Safety

**Core design principle:** Generic values are **immutable**. Operations that appear to modify a collection actually create new collections.

**Implications:**

1. **Idempotent operations**: Reading the same generic value always returns the same result
2. **No in-place modification**: "Adding" an item to a sequence creates a new sequence; the original remains unchanged
3. **Value semantics**: Generics behave like values, not references
4. **Inherent thread safety**: Multiple threads can safely read the same generic without locks

**Thread safety model:**
- **Reading generics**: Always safe, no synchronization needed
- **Builder operations**: Only the allocator requires locking (single advancing pointer in arena allocator)
- **Sharing across threads**: Safe by design—immutability eliminates race conditions

**Example - immutability in action:**
```c
fy_generic seq1 = fy_sequence(1, 2, 3);

// "Adding" creates new sequence, seq1 unchanged
fy_generic seq2 = fy_gb_sequence(gb,
    fy_generic_sequence_get_item(seq1, 0),
    fy_generic_sequence_get_item(seq1, 1),
    fy_generic_sequence_get_item(seq1, 2),
    fy_to_generic(4)  // New item
);

// seq1 still contains [1, 2, 3]
// seq2 contains [1, 2, 3, 4]
```

**Why this matters:**
- **Concurrent access**: Multiple threads can process the same generic tree without coordination
- **Functional programming**: Natural support for functional patterns in C
- **Debugging**: Values don't change unexpectedly
- **Performance**: Arena allocator with single pointer increment—minimal contention

**Allocator considerations:**
- **Linear/Arena**: Simple pointer bump allocation, lock-free reads
- **Dedup**: Hash table lookup requires locking, but reads remain safe
- **Builder**: Lock only when allocating, not when reading

This design makes generics ideal for multi-threaded YAML/JSON processing where multiple worker threads parse different documents or process different subtrees of the same document.

## Basic Usage

**Converting values to generic:**
```c
#include <libfyaml.h>

// Automatic type detection
fy_generic v1 = fy_to_generic(42);           // integer
fy_generic v2 = fy_to_generic(3.14);         // float
fy_generic v3 = fy_to_generic("hello");      // string
fy_generic v4 = fy_to_generic(true);         // boolean
fy_generic v5 = fy_to_generic(NULL);         // null
```

**Creating sequences:**
```c
// Simple sequence - variadic constructor
fy_generic seq = fy_sequence(1, 2, 3, 4, 5);

// Mixed types
fy_generic mixed = fy_sequence(42, "hello", true, 3.14);

// Empty sequence
fy_generic empty = fy_sequence();
```

**Creating mappings:**
```c
// Key-value pairs as alternating arguments
fy_generic map = fy_mapping(
    "name", "Alice",
    "age", 30,
    "active", true
);

// Mixed types in keys and values
fy_generic complex = fy_mapping(
    "count", 100,
    42, "answer",           // integer key!
    "pi", 3.14159
);
```

**Nested structures:**
```c
// Sequences in mappings
fy_generic config = fy_mapping(
    "server", "localhost",
    "ports", fy_sequence(8080, 8081, 8082),
    "enabled", true
);

// Mappings in sequences
fy_generic users = fy_sequence(
    fy_mapping("name", "Alice", "id", 1),
    fy_mapping("name", "Bob", "id", 2),
    fy_mapping("name", "Charlie", "id", 3)
);

// Complex nesting
fy_generic nested = fy_mapping(
    "users", fy_sequence(
        fy_mapping("name", "Alice", "age", 30),
        fy_mapping("name", "Bob", "age", 25)
    ),
    "config", fy_mapping(
        "timeout", 30,
        "retries", 3
    )
);
```

## API Variants Comparison

| API Variant | Allocation | Use Case | Lifetime | Internalization |
|-------------|------------|----------|----------|-----------------|
| `fy_sequence(...)` | Stack (alloca) | Temporary data, simple cases | Function scope | Copied when used with builder |
| `fy_mapping(...)` | Stack (alloca) | Temporary data, simple cases | Function scope | Copied when used with builder |
| `fy_sequence_alloca(count, items)` | Stack (alloca) | Explicit control, pre-built arrays | Function scope | Copied when used with builder |
| `fy_mapping_alloca(count, pairs)` | Stack (alloca) | Explicit control, pre-built arrays | Function scope | Copied when used with builder |
| `fy_gb_sequence(gb, ...)` | Heap (builder) | Persistent data, large collections | Until builder freed | Already in builder memory |
| `fy_gb_mapping(gb, ...)` | Heap (builder) | Persistent data, large collections | Until builder freed | Already in builder memory |

**Note on Internalization:** Stack-allocated generics can be safely passed to builder functions. The builder automatically copies any out-of-place data, so you can freely mix `fy_sequence()` with `fy_gb_mapping(gb, ...)` for cleaner code.

## When to Use Which API

**Use `fy_sequence()` / `fy_mapping()` (high-level) when:**
- Creating temporary data structures
- Simple, inline data (< 10-20 elements)
- Automatic type conversion is desired
- Natural, readable code is priority
- **Building nested structures even with builder functions** (internalization handles copying)

**Use `fy_sequence_alloca()` / `fy_mapping_alloca()` (low-level) when:**
- You already have a pre-built array of generics
- Explicit control over type conversion is needed
- Building collections programmatically in a loop

**Use `fy_gb_sequence()` / `fy_gb_mapping()` (builder) when:**
- Data needs to outlive the current function
- Large collections that might overflow stack
- Memory should be managed by allocator subsystem
- Data will be embedded in documents or persistent structures

**Pro tip:** Due to automatic internalization, you can mix APIs freely:
```c
// This is perfectly fine and recommended for readability:
fy_generic data = fy_gb_mapping(gb,
    "items", fy_sequence(1, 2, 3),      // Stack-allocated
    "config", fy_mapping("a", "b")       // Stack-allocated
);
// The builder copies what it needs - clean and simple!
```

## Examples by Use Case

**Temporary test data:**
```c
// Clean, readable test setup
fy_generic test_data = fy_mapping(
    "input", fy_sequence(1, 2, 3),
    "expected", 6,
    "description", "sum test"
);

process_test(test_data);
// Automatically cleaned up when function returns
```

**Building dynamically:**
```c
// Pre-build array, then create sequence
fy_generic items[count];
for (int i = 0; i < count; i++) {
    items[i] = fy_to_generic(data[i]);
}
fy_generic seq = fy_sequence_alloca(count, items);
```

**Persistent data:**
```c
struct fy_generic_builder *gb = fy_generic_builder_create(...);

// Recommended: Use simple stack API for nested data
// Builder automatically copies what it needs
fy_generic persistent = fy_gb_mapping(gb,
    "data", fy_sequence(1, 2, 3, 4, 5),        // Clean and readable!
    "timestamp", timestamp,
    "user", username
);

// Alternative (also valid, but more verbose):
// fy_gb_mapping(gb, "data", fy_gb_sequence(gb, 1, 2, 3, 4, 5), ...)

// Store in document, use elsewhere
// ... later ...
fy_generic_builder_destroy(gb);  // Cleanup
```

## Internalization and Memory Management

A key feature of the generic builder is **automatic internalization**: when you pass stack-allocated generics to builder functions, the builder automatically copies any out-of-place (non-inline) data into its own heap-managed memory.

**This means you can freely mix stack and heap APIs:**

```c
struct fy_generic_builder *gb = fy_generic_builder_create(...);

// NO NEED to use fy_gb_sequence everywhere!
// This works and is much cleaner:
fy_generic map = fy_gb_mapping(gb,
    "foo", fy_sequence(1, 2, 3),           // Stack-allocated sequence
    "bar", "value",                         // Inline string
    "nested", fy_mapping(                   // Stack-allocated mapping
        "a", 1,
        "b", 2
    )
);

// The builder has copied all necessary data
// Stack allocations are safe to go out of scope
```

**Instead of the verbose:**
```c
// You DON'T need to do this:
map = fy_gb_mapping(gb,
    "foo", fy_gb_sequence(gb, 1, 2, 3),    // Unnecessarily verbose
    "bar", fy_gb_to_generic(gb, "value"),
    "nested", fy_gb_mapping(gb,
        "a", fy_gb_to_generic(gb, 1),
        "b", fy_gb_to_generic(gb, 2)
    )
);
```

**How it works:**
- **Inline values** (small ints, short strings, 32-bit floats): Stored directly in the generic, no copying needed
- **Out-of-place values** (large ints, long strings, doubles, collections): Automatically copied into builder's memory during internalization
- **Result**: Simple, readable code without manual memory management

**Practical pattern:**
```c
// Create temporary config on stack - clean and readable
fy_generic temp_config = fy_mapping(
    "host", "localhost",
    "ports", fy_sequence(8080, 8081, 8082),
    "timeout", 30
);

// Internalize into builder - everything is copied
fy_generic persistent = fy_gb_internalize(gb, temp_config);

// temp_config goes out of scope, but persistent is safe
// The builder owns all the data now
```

**When to use `fy_gb_*` vs `fy_*`:**
- Use `fy_sequence()` / `fy_mapping()` for readability - let internalization handle copying
- Only use `fy_gb_sequence()` / `fy_gb_mapping()` if you specifically need the generic to be pre-allocated in the builder
- In practice, the simple stack-based API is usually clearer

## Accessing Generic Values

**Type checking and extraction:**
```c
fy_generic v = fy_sequence(1, 2, 3);

// Get type
enum fy_generic_type type = fy_generic_get_type(v);

// Check specific type
if (fy_generic_is_sequence(v)) {
    // Access sequence elements
    size_t count = fy_generic_sequence_get_item_count(v);
    for (size_t i = 0; i < count; i++) {
        fy_generic item = fy_generic_sequence_get_item(v, i);
        // Process item...
    }
}

// Extract scalar values
if (fy_generic_is_int(v)) {
    long long value = fy_generic_get_int(v);
}
```

**Mapping lookup:**
```c
fy_generic map = fy_mapping("name", "Alice", "age", 30);

// Lookup by key
const fy_generic *age_ptr = fy_generic_mapping_lookup(map, fy_to_generic("age"));
if (age_ptr) {
    long long age = fy_generic_get_int(*age_ptr);
}

// Can use complex keys (sequences, mappings)
fy_generic complex_key = fy_sequence(1, 2);
const fy_generic *value = fy_generic_mapping_lookup(map, complex_key);
```

## Implementation Notes

**Pointer Tagging:** The lower 3 bits of pointers encode the type tag (pointers are 8-byte aligned). This allows type information and small values to fit in a single pointer-sized word.

**Inline Storage:** Values that fit within the remaining bits are stored directly:
- 64-bit systems: 61-bit integers, 7-byte strings, 32-bit floats
- 32-bit systems: 29-bit integers, 3-byte strings

**Out-of-place Storage:** Larger values are allocated (via `alloca` or builder) and a pointer is stored with appropriate type tag.

**Type Dispatch:** The `fy_to_generic()` macro uses C11 `_Generic` to select the appropriate conversion function based on the argument type at compile-time.

## Limitations

**Stack allocation (`fy_sequence` / `fy_mapping`):**
- Uses `alloca()` which allocates on the stack
- Large or deeply nested structures can overflow stack
- No runtime checks on stack usage
- Data is automatically freed when function returns (cannot return these values)

**GNU C extensions:**
- Requires C11 `_Generic` (widely supported)
- Uses statement expressions `({...})` (GNU extension)
- Uses `typeof` (GNU extension)
- May not compile with strict C99 compilers

**Type ranges:**
- Integers: 61 bits signed on 64-bit systems, 29 bits on 32-bit
- Strings: 7 bytes inline on 64-bit, 3 bytes on 32-bit
- Larger values require out-of-place storage

**Error handling:**
- Invalid values represented as `fy_invalid`
- Runtime assertions in debug builds
- No compile-time type safety for conversions

## Code Example: Complete Usage

```c
#include <libfyaml.h>

void process_config(void) {
    // Create complex configuration using generic API
    fy_generic config = fy_mapping(
        "server", fy_mapping(
            "host", "0.0.0.0",
            "port", 8080,
            "workers", 4
        ),
        "database", fy_mapping(
            "hosts", fy_sequence("db1.example.com", "db2.example.com"),
            "port", 5432,
            "pool_size", 10
        ),
        "features", fy_sequence("auth", "logging", "metrics"),
        "debug", false
    );

    // Emit as YAML
    fy_generic_emit_default(config);

    // Or convert to document
    struct fy_document *doc = fy_generic_to_document(NULL, config);
    fy_emit_document_to_fp(doc, FYECF_MODE_FLOW_ONELINE, stderr);
    fy_document_destroy(doc);
}
```
