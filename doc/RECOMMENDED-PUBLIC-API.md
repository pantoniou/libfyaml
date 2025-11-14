# Recommended Public Generic API for libfyaml

**Date**: 2026-01-03
**Purpose**: Define the public generic API surface for the first release
**Scope**: Based on existing implementation, documentation, and multi-language binding needs

---

## Executive Summary

The libfyaml generic type system has **excellent internal implementation** and **comprehensive documentation**. However, it currently resides in `fy-internal-generic.h` rather than a public header. This document recommends what should be exposed as the stable public API in either:
- `include/libfyaml.h` (main public header), or
- `include/libfyaml-generic.h` (new dedicated public header)

### Key Findings:

**What EXISTS and is well-documented:**
- ✅ Complete generic type system with inline storage optimization
- ✅ Polymorphic operations via C11 `_Generic` (fy_get, fy_cast, fy_len)
- ✅ Functional operations (assoc, dissoc, conj, filter, map, reduce)
- ✅ Builder lifecycle with multiple allocation strategies
- ✅ Comprehensive iteration support (fy_foreach macro)
- ✅ 31+ utility functions (keys, values, slice, merge, etc.)
- ✅ Lambda/closure support (GCC nested functions, Clang Blocks)
- ✅ Parallel operations (pfilter, pmap, preduce)
- ✅ Document integration (YAML/JSON ↔ generic conversion)

**What's MISSING from public API (needed by language bindings):**
- ❌ Formal public header (currently in fy-internal-*.h)
- ❌ High-level convenience macros (fy_get_type, fy_len, fy_get shortcuts)
- ❌ Type conversion functions (to_int, to_float, to_string, to_bool)
- ❌ Comparison semantics with numeric promotion
- ❌ Arithmetic operator specifications

---

## 1. Core Public API (Essential - Tier 1)

### 1.1 Type System Fundamentals

**Primary Type:**
```c
/**
 * fy_generic - Universal type-tagged value
 *
 * Space-efficient representation:
 * - Inline storage: 61-bit ints, 7-byte strings, 32-bit floats
 * - Out-of-place: sequences, mappings, large values
 * - Pointer tagging: 3 bits for type, 1 bit for inplace/outofplace
 */
typedef struct fy_generic {
    union {
        uintptr_t v;
        intptr_t vs;
    };
} fy_generic;
```

**Type Enumeration:**
```c
enum fy_generic_type {
    FYGT_INVALID = 0,
    FYGT_NULL,
    FYGT_BOOL,
    FYGT_INT,
    FYGT_FLOAT,
    FYGT_STRING,
    FYGT_SEQUENCE,
    FYGT_MAPPING,
    FYGT_INDIRECT,     // With metadata (anchor, tag, style)
    FYGT_ALIAS,
};
```

**Special Values:**
```c
extern const fy_generic fy_invalid;    // Invalid/missing value
extern const fy_generic fy_null;       // null value
```

---

### 1.2 Type Checking

**Basic Predicates:**
```c
bool fy_generic_is_valid(fy_generic v);
bool fy_generic_is_null(fy_generic v);
bool fy_generic_is_bool(fy_generic v);
bool fy_generic_is_int(fy_generic v);
bool fy_generic_is_float(fy_generic v);
bool fy_generic_is_string(fy_generic v);
bool fy_generic_is_sequence(fy_generic v);
bool fy_generic_is_mapping(fy_generic v);

enum fy_generic_type fy_generic_get_type(fy_generic v);
```

**Convenience Macros (RECOMMENDED):**
```c
// Shorthand versions for common use
#define fy_is_valid(v)    fy_generic_is_valid(v)
#define fy_is_null(v)     fy_generic_is_null(v)
#define fy_is_bool(v)     fy_generic_is_bool(v)
#define fy_is_int(v)      fy_generic_is_int(v)
#define fy_is_float(v)    fy_generic_is_float(v)
#define fy_is_string(v)   fy_generic_is_string(v)
#define fy_is_sequence(v) fy_generic_is_sequence(v)
#define fy_is_mapping(v)  fy_generic_is_mapping(v)

#define fy_get_type(v)    fy_generic_get_type(v)
```

---

### 1.3 Value Extraction (Casting)

**Primary Casting API:**
```c
/**
 * fy_cast() - Type-safe value extraction with C11 _Generic dispatch
 *
 * Usage:
 *   bool b = fy_cast(v, false);           // Extract bool
 *   long long i = fy_cast(v, 0LL);        // Extract int
 *   double d = fy_cast(v, 0.0);           // Extract float
 *   const char *s = fy_cast(v, "");       // Extract string
 *
 * Returns the default if type doesn't match or value is invalid.
 */
#define fy_cast(v, default) /* _Generic implementation */
```

**Sized String Support:**
```c
/**
 * fy_generic_sized_string - String with explicit length
 * Supports embedded NULs for binary-safe YAML strings
 */
typedef struct fy_generic_sized_string {
    const char *data;
    size_t size;
} fy_generic_sized_string;

fy_generic_sized_string fy_generic_get_string(fy_generic v);
```

**Decorated Integer Support:**
```c
/**
 * fy_generic_decorated_int - Extended integer representation
 * Supports full unsigned long long range (beyond LLONG_MAX)
 */
typedef struct fy_generic_decorated_int {
    union {
        long long sv;           // Signed value
        unsigned long long uv;  // Unsigned value
    };
    uint32_t flags;
    #define FYGDIF_UNSIGNED_RANGE_EXTEND 0x1  // uv > LLONG_MAX
} fy_generic_decorated_int;

fy_generic_decorated_int fy_generic_get_decorated_int(fy_generic v);
```

---

### 1.4 Collection Access

**Length:**
```c
/**
 * fy_len() - Get collection/string length
 * Works on: sequences, mappings, strings
 * Returns 0 for non-collection types
 */
size_t fy_len(fy_generic v);
```

**Universal Accessor:**
```c
/**
 * fy_get() - Polymorphic data accessor
 *
 * For sequences: fy_get(seq, index, default)
 * For mappings: fy_get(map, "key", default)
 *
 * Uses C11 _Generic to dispatch based on key type.
 * Returns default if key/index not found or type mismatch.
 */
#define fy_get(collection, key_or_index, default) /* implementation */
```

**Typed Access (for explicit type expectations):**
```c
/**
 * fy_get_typed() - Get with explicit type checking
 * Returns default if value exists but has wrong type
 */
#define fy_get_typed(collection, key, type) /* implementation */

// Example:
int port = fy_get_typed(config, "port", int);  // 0 if not an int
```

---

### 1.5 Iteration

**Universal Iterator:**
```c
/**
 * fy_foreach() - Universal iteration macro
 *
 * Sequences: iterates values
 * Mappings: iterates key-value pairs
 *
 * Usage:
 *   fy_foreach(item, sequence) { ... }
 *   fy_foreach_kv(key, val, mapping) { ... }
 */
#define fy_foreach(var, collection) /* implementation */
#define fy_foreach_kv(key_var, val_var, mapping) /* implementation */
```

**Direct Array Access (for performance-critical code):**
```c
const fy_generic *fy_generic_sequence_get_items(fy_generic seq, size_t *count);
const fy_generic_map_pair *fy_generic_mapping_get_pairs(fy_generic map, size_t *count);

typedef union fy_generic_map_pair {
    struct { fy_generic key; fy_generic value; };
} fy_generic_map_pair;
```

---

### 1.6 Builder Lifecycle

**Creation & Destruction:**
```c
/**
 * Generic builder - Memory management for generic values
 *
 * Allocation strategies:
 * - FYAST_PER_TAG_FREE: Fast allocation with per-type pools
 * - FYAST_PER_TAG_FREE_DEDUP: Deduplication for repeated values
 * - FYAST_MMAP: Memory-mapped allocation for large datasets
 * - Auto selection based on workload
 */
struct fy_generic_builder;

struct fy_generic_builder *fy_generic_builder_create(
    const struct fy_generic_builder_cfg *cfg);

void fy_generic_builder_destroy(struct fy_generic_builder *gb);

void fy_generic_builder_reset(struct fy_generic_builder *gb);
```

**Configuration:**
```c
struct fy_generic_builder_cfg {
    struct fy_allocator *allocator;     // Optional custom allocator
    size_t estimated_max_size;          // Size hint for optimization
    enum fy_gb_cfg_flags flags;
};

enum fy_gb_cfg_flags {
    FYGBCF_OWNS_ALLOCATOR = 0x1,       // Builder owns allocator
    // ... other flags
};
```

---

### 1.7 Value Creation

**Literals (stack-allocated, function-scoped):**
```c
/**
 * Stack-based value creation (temporary, function scope only)
 * No builder required - uses alloca internally
 */
fy_generic fy_sequence(/* values... */);
fy_generic fy_mapping(/* key1, val1, key2, val2, ... */);
fy_generic fy_value(/* any type */);

// Examples:
fy_generic seq = fy_sequence(1, 2, 3, "four");
fy_generic map = fy_mapping("host", "localhost", "port", 8080);
fy_generic val = fy_value(42);
```

**Heap-allocated (persistent):**
```c
/**
 * Builder-based value creation (persistent)
 * Requires builder - values live until builder destroyed
 */
fy_generic fy_gb_sequence(struct fy_generic_builder *gb, /* values... */);
fy_generic fy_gb_mapping(struct fy_generic_builder *gb, /* key1, val1, ... */);
fy_generic fy_gb_value(struct fy_generic_builder *gb, /* any type */);
```

---

## 2. Functional Operations (Essential - Tier 1)

### 2.1 Immutable Collection Operations

**These operations return NEW collections (immutable semantics):**

```c
/**
 * fy_assoc() - Associate key with value in mapping
 * Returns new mapping with key-value added/updated
 * Original mapping unchanged
 */
fy_generic fy_assoc(fy_generic map, fy_generic key, fy_generic value);
fy_generic fy_assoc(fy_generic map, const char *key, fy_generic value);  // String key variant

/**
 * fy_dissoc() - Remove key from mapping
 * Returns new mapping without key
 * Original mapping unchanged
 */
fy_generic fy_dissoc(fy_generic map, fy_generic key);
fy_generic fy_dissoc(fy_generic map, const char *key);

/**
 * fy_conj() - Append element to sequence
 * Returns new sequence with element added
 * Original sequence unchanged
 */
fy_generic fy_conj(fy_generic seq, fy_generic value);

/**
 * fy_assoc_at() - Update element at index in sequence
 * Returns new sequence with element updated
 * Original sequence unchanged
 */
fy_generic fy_assoc_at(fy_generic seq, size_t index, fy_generic value);
```

---

### 2.2 Higher-Order Functions

**Filter/Map/Reduce:**

```c
/**
 * Callback types for functional operations
 */
typedef bool (*fy_filter_fn)(struct fy_generic_builder *gb, fy_generic v);
typedef fy_generic (*fy_map_fn)(struct fy_generic_builder *gb, fy_generic v);
typedef fy_generic (*fy_reduce_fn)(struct fy_generic_builder *gb,
                                    fy_generic acc, fy_generic v);

/**
 * fy_filter() - Filter collection by predicate
 * Returns new collection with elements where predicate(elem) == true
 */
fy_generic fy_filter(fy_generic collection, fy_filter_fn predicate);

/**
 * fy_map() - Transform collection elements
 * Returns new collection with transform(elem) for each element
 */
fy_generic fy_map(fy_generic collection, fy_map_fn transform);

/**
 * fy_reduce() - Reduce collection to single value
 * Accumulator initialized with init, updated via reducer(acc, elem)
 */
fy_generic fy_reduce(fy_generic collection, fy_generic init, fy_reduce_fn reducer);
```

**Lambda Syntax (GCC nested functions, Clang Blocks):**

```c
/**
 * Lambda macros for inline function definitions
 * Only available with GCC/Clang compiler support
 */
#define fy_filter_lambda(collection, body) /* implementation */
#define fy_map_lambda(collection, body) /* implementation */
#define fy_reduce_lambda(collection, init, body) /* implementation */

// Example:
fy_generic evens = fy_filter_lambda(numbers, {
    int n = fy_cast(_v, 0);
    return n % 2 == 0;
});
```

**Parallel Variants:**

```c
/**
 * Parallel functional operations
 * Requires thread pool for work distribution
 */
fy_generic fy_pfilter(fy_generic collection, void *thread_pool, fy_filter_fn predicate);
fy_generic fy_pmap(fy_generic collection, void *thread_pool, fy_map_fn transform);
fy_generic fy_preduce(fy_generic collection, fy_generic init, void *thread_pool,
                       fy_reduce_fn reducer);
```

---

## 3. Utility Functions (Important - Tier 2)

### 3.1 Mapping Utilities

```c
fy_generic fy_keys(fy_generic map);              // Extract all keys as sequence
fy_generic fy_values(fy_generic map);            // Extract all values as sequence
fy_generic fy_items(fy_generic map);             // Extract [key, value] pairs

bool fy_contains(fy_generic map, fy_generic key);        // Check key membership
bool fy_contains(fy_generic map, const char *key);       // String key variant

fy_generic fy_merge(fy_generic map1, fy_generic map2);   // Merge two mappings
fy_generic fy_merge_all(/* maps... */);                   // Merge multiple
```

---

### 3.2 Sequence Utilities

**Slicing:**
```c
fy_generic fy_slice(fy_generic seq, size_t start, size_t end);     // Extract [start, end)
fy_generic fy_slice_py(fy_generic seq, ssize_t start, ssize_t end); // Python-style (negative indices)

fy_generic fy_take(fy_generic seq, size_t n);           // First n elements
fy_generic fy_drop(fy_generic seq, size_t n);           // Skip first n elements
```

**Accessors:**
```c
fy_generic fy_first(fy_generic seq);                    // Get first element
fy_generic fy_last(fy_generic seq);                     // Get last element
fy_generic fy_rest(fy_generic seq);                     // All but first (tail)
```

**Transformations:**
```c
fy_generic fy_reverse(fy_generic seq);                  // Reverse order
fy_generic fy_concat(fy_generic seq1, fy_generic seq2); // Concatenate
fy_generic fy_concat_all(/* seqs... */);                // Concatenate multiple

fy_generic fy_distinct(fy_generic seq);                 // Remove duplicates
fy_generic fy_unique(fy_generic seq);                   // Alias for distinct

fy_generic fy_flatten(fy_generic seq);                  // Flatten nested sequences
fy_generic fy_flatten_depth(fy_generic seq, int depth); // Flatten to depth
```

**Zipping:**
```c
fy_generic fy_zip(fy_generic seq1, fy_generic seq2);    // Zip two sequences
fy_generic fy_zip_all(/* seqs... */);                   // Zip multiple
```

---

## 4. Type Conversion Functions (NEW - Tier 2)

**These are MISSING from current API but needed by all language bindings:**

```c
/**
 * fy_generic_to_int() - Convert to integer with type coercion
 *
 * Conversion rules:
 * - bool: true→1, false→0
 * - int: return as-is
 * - float: truncate to integer
 * - string: parse as integer (base 10)
 * - others: returns fy_invalid
 */
fy_generic fy_generic_to_int(struct fy_generic_builder *gb, fy_generic v);

/**
 * fy_generic_to_float() - Convert to float with type coercion
 *
 * Conversion rules:
 * - bool: true→1.0, false→0.0
 * - int: convert to double
 * - float: return as-is
 * - string: parse as float
 * - others: returns fy_invalid
 */
fy_generic fy_generic_to_float(struct fy_generic_builder *gb, fy_generic v);

/**
 * fy_generic_to_string() - Convert to string representation
 *
 * Conversion rules:
 * - Scalars: text representation ("42", "3.14", "true", etc.)
 * - Collections: YAML/JSON representation ("[1, 2, 3]", "{a: 1}", etc.)
 */
fy_generic fy_generic_to_string(struct fy_generic_builder *gb, fy_generic v);

/**
 * fy_generic_to_bool() - Convert to boolean with truthiness rules
 *
 * Conversion rules:
 * - null, false, 0, 0.0, "", empty collections → false
 * - everything else → true
 */
fy_generic fy_generic_to_bool(struct fy_generic_builder *gb, fy_generic v);
```

**Convenience Macros:**
```c
#define fy_to_int(gb, v)    fy_generic_to_int((gb), (v))
#define fy_to_float(gb, v)  fy_generic_to_float((gb), (v))
#define fy_to_string(gb, v) fy_generic_to_string((gb), (v))
#define fy_to_bool(gb, v)   fy_generic_to_bool((gb), (v))
```

---

## 5. Comparison & Arithmetic (NEW - Tier 3)

**These are MISSING but would benefit language bindings:**

### 5.1 Comparison with Type Promotion

```c
/**
 * fy_generic_compare() - Compare two generics
 * Returns: <0 if a<b, 0 if a==b, >0 if a>b
 *
 * No type coercion - types must match
 */
int fy_generic_compare(fy_generic a, fy_generic b);

/**
 * fy_generic_compare_coerce() - Compare with automatic numeric promotion
 *
 * Promotion rules:
 * - bool, int, float: promote to common numeric type
 * - int vs float: promote int to float
 * - bool vs numeric: promote bool to int/float
 * - string: lexicographic comparison
 * - incompatible types: return FY_COMPARE_INCOMPATIBLE
 */
int fy_generic_compare_coerce(fy_generic a, fy_generic b);

#define FY_COMPARE_INCOMPATIBLE INT_MIN
```

---

### 5.2 Arithmetic Operations (Optional)

```c
/**
 * Arithmetic operations with overflow handling
 * Return fy_invalid on overflow or type mismatch
 */
fy_generic fy_add(struct fy_generic_builder *gb, fy_generic a, fy_generic b);
fy_generic fy_sub(struct fy_generic_builder *gb, fy_generic a, fy_generic b);
fy_generic fy_mul(struct fy_generic_builder *gb, fy_generic a, fy_generic b);
fy_generic fy_div(struct fy_generic_builder *gb, fy_generic a, fy_generic b);
fy_generic fy_mod(struct fy_generic_builder *gb, fy_generic a, fy_generic b);

/**
 * Arithmetic with automatic promotion
 * - Promotes to float if either operand is float
 * - Supports bool (treats as 0/1)
 * - Detects overflow and promotes to Python-style arbitrary precision (returns fy_invalid with error context)
 */
fy_generic fy_add_coerce(struct fy_generic_builder *gb, fy_generic a, fy_generic b);
// ... similar for sub, mul, div, mod
```

---

## 6. Document Integration (Important - Tier 2)

**YAML/JSON ↔ Generic Conversion:**

```c
/**
 * Parse YAML/JSON to generic representation
 */
fy_generic fy_generic_parse(struct fy_generic_builder *gb,
                             const char *input, size_t len,
                             enum fy_parse_flags flags);

fy_generic fy_generic_parse_file(struct fy_generic_builder *gb,
                                  const char *filename,
                                  enum fy_parse_flags flags);

/**
 * Emit generic to YAML/JSON
 */
char *fy_generic_emit_to_string(fy_generic v, enum fy_emit_flags flags);

int fy_generic_emit_to_file(fy_generic v, const char *filename,
                              enum fy_emit_flags flags);

/**
 * Document conversion
 */
fy_generic fy_document_to_generic(struct fy_generic_builder *gb,
                                   struct fy_document *fyd);

struct fy_document *fy_generic_to_document(const struct fy_document_cfg *cfg,
                                            fy_generic v);
```

---

## 7. Advanced Features (Optional - Tier 3)

### 7.1 Metadata & Styling

```c
/**
 * Indirect generics - values with metadata
 * Supports: anchors, tags, style information
 */
struct fy_generic_indirect {
    fy_generic value;
    fy_generic anchor;
    fy_generic tag;
    uint32_t flags;  // Style: plain, quoted, literal, folded, etc.
};

fy_generic fy_generic_set_anchor(struct fy_generic_builder *gb,
                                   fy_generic v, const char *anchor);
fy_generic fy_generic_set_tag(struct fy_generic_builder *gb,
                                fy_generic v, const char *tag);

const char *fy_generic_get_anchor(fy_generic v);
const char *fy_generic_get_tag(fy_generic v);
```

---

### 7.2 Path-Based Operations

```c
/**
 * Navigate and modify via path expressions
 */
fy_generic fy_get_at_path(fy_generic root, /* path components... */);
fy_generic fy_set_at_path(struct fy_generic_builder *gb,
                           fy_generic root, /* path components..., value */);

// Example:
fy_generic port = fy_get_at_path(config, "server", "port");
fy_generic new_config = fy_set_at_path(gb, config, "server", "host", "0.0.0.0");
```

---

### 7.3 Validation & Cloning

```c
/**
 * Deep validation of generic structure
 */
bool fy_generic_validate(fy_generic v);

/**
 * Deep copy (clones entire structure)
 */
fy_generic fy_generic_clone(struct fy_generic_builder *gb, fy_generic v);

/**
 * Internalize external generic into builder
 * (Copies if from different builder)
 */
fy_generic fy_generic_internalize(struct fy_generic_builder *gb, fy_generic v);
```

---

## 8. Error Handling & Diagnostics

**Current Status: fy_invalid is the only error indicator**

**Recommendation for Future:**

```c
/**
 * Error context (thread-local or explicit parameter)
 */
enum fy_generic_error_type {
    FY_GERR_NONE,
    FY_GERR_INVALID_TYPE,
    FY_GERR_INDEX_OUT_OF_BOUNDS,
    FY_GERR_KEY_NOT_FOUND,
    FY_GERR_OVERFLOW,
    FY_GERR_PARSE_ERROR,
    FY_GERR_OUT_OF_MEMORY,
};

struct fy_generic_error {
    enum fy_generic_error_type type;
    const char *message;
};

const struct fy_generic_error *fy_generic_get_last_error(void);
```

---

## 9. Recommended Phasing

### Phase 1: Core Public API (Before First Release)

**Must have:**
1. Type system fundamentals (fy_generic, enums, special values)
2. Type checking (fy_is_*, fy_get_type)
3. Casting (fy_cast, sized strings, decorated ints)
4. Collection access (fy_len, fy_get)
5. Iteration (fy_foreach)
6. Builder lifecycle (create, destroy, reset)
7. Value creation (fy_sequence, fy_mapping, fy_value and builder variants)

**Estimated effort:** Already exists, just needs to be moved to public header

---

### Phase 2: Functional API (Before First Release)

**Should have:**
1. Immutable operations (assoc, dissoc, conj, assoc_at)
2. Higher-order functions (filter, map, reduce + lambda support)
3. Basic utilities (keys, values, slice, take, drop, first, last, reverse, concat)
4. Document integration (parse, emit, conversion)

**Estimated effort:** Already exists, just needs public exposure

---

### Phase 3: Type Conversions (Before or Shortly After Release)

**Important for bindings:**
1. Type conversion functions (to_int, to_float, to_string, to_bool)
2. Comparison with coercion (compare_coerce)

**Estimated effort:** 1-2 days (based on Python binding implementation)

---

### Phase 4: Advanced Features (Post-Release)

**Nice to have:**
1. Arithmetic operations
2. Path-based operations
3. Metadata/styling
4. Error context API
5. Remaining utilities (flatten, zip, group_by, etc.)

**Estimated effort:** 1 week

---

## 10. Header Organization Recommendation

### Option A: Single Public Header

```c
// include/libfyaml-generic.h

#ifndef LIBFYAML_GENERIC_H
#define LIBFYAML_GENERIC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Core types */
typedef struct fy_generic { ... } fy_generic;
enum fy_generic_type { ... };

/* Builder */
struct fy_generic_builder;
struct fy_generic_builder_cfg;

/* Type checking */
bool fy_generic_is_valid(fy_generic v);
// ...

/* Casting */
#define fy_cast(v, default) ...
// ...

/* Collection access */
size_t fy_len(fy_generic v);
#define fy_get(col, key, default) ...
// ...

/* Iteration */
#define fy_foreach(var, col) ...
// ...

/* Functional operations */
fy_generic fy_assoc(fy_generic map, ...);
// ...

/* Utilities */
fy_generic fy_keys(fy_generic map);
// ...

#endif /* LIBFYAML_GENERIC_H */
```

---

### Option B: Integrated in Main Header

```c
// include/libfyaml.h

/* ... existing parser/emitter API ... */

#ifdef FY_ENABLE_GENERICS  /* or always include */

/* Generic type system */
typedef struct fy_generic { ... } fy_generic;
// ... generic API ...

#endif /* FY_ENABLE_GENERICS */
```

---

**Recommendation: Option A (separate header)**
- Cleaner separation of concerns
- Easier to document
- Users can opt-in to generic API
- Matches the extensive separate documentation

---

## 11. Documentation Requirements

For each public function/macro:

1. **API Reference:**
   - Function signature
   - Parameter descriptions
   - Return value semantics
   - Error conditions
   - Example usage

2. **Examples:**
   - Simple usage
   - Common patterns
   - Edge cases

3. **Performance Notes:**
   - Time complexity
   - Space complexity
   - Allocation behavior

4. **Cross-References:**
   - Related functions
   - Alternative approaches

**Good news:** Most of this documentation already exists in doc/GENERIC-*.md files!

---

## 12. Summary

### What to Expose as Public API:

**Tier 1 (Essential - must have):**
- Core type system ✅ (exists)
- Type checking ✅ (exists)
- Casting/extraction ✅ (exists)
- Collection access ✅ (exists)
- Iteration ✅ (exists)
- Builder lifecycle ✅ (exists)
- Value creation ✅ (exists)
- Functional operations ✅ (exists)

**Tier 2 (Important - should have):**
- Utility functions ✅ (exists)
- Type conversions ❌ (needs implementation)
- Document integration ✅ (exists)

**Tier 3 (Nice to have - can defer):**
- Comparison with coercion ❌ (needs implementation)
- Arithmetic operations ❌ (needs implementation)
- Advanced features ✅ (exists)

### Implementation Status:

- **~90% already implemented and documented**
- **~10% new functions needed** (type conversions, comparison with coercion)
- **Main task: Organize into public header** (1-2 days)
- **New implementations: Type conversions** (1-2 days)

### Total Effort Estimate:

- Create public header: 1-2 days
- Add type conversion functions: 1-2 days
- Review and test: 1 day
- **Total: 3-5 days**

---

## Appendix: Language Binding Considerations

### For Python:
- ✅ Needs type conversions (to_int, to_float, to_string, to_bool)
- ✅ Needs comparison with promotion
- ✅ Benefits from fy_len, fy_get shortcuts
- ✅ Uses fy_foreach for iteration

### For Go:
- ✅ Needs type-specific getters (GetInt, GetString, etc.)
- ✅ Needs error context (not just fy_invalid)
- ✅ Benefits from explicit type conversions
- ❌ May not use lambdas (no GCC nested functions)

### For Zig:
- ✅ Tagged unions map perfectly to fy_generic
- ✅ Optional types map to fy_invalid → null
- ✅ Needs error sets (not just fy_invalid)
- ✅ Could use comptime for zero-cost abstractions

### For JavaScript:
- ✅ Dynamic typing like Python - similar needs
- ✅ Needs JSON integration (already exists!)
- ✅ Benefits from type conversions
- ❌ May need async/Promise wrappers (out of scope for C API)

---

**End of Recommendations**
