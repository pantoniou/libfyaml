# Generic API Basics

This document covers the core design concepts of libfyaml's generic type system API.

## The Problem: Verbose Generic APIs

Traditional generic/dynamic type APIs in C require verbose function calls with explicit type conversions:

```c
// Verbose approach (old-style low-level API)
const char *role = fy_generic_get_string_default(
    fy_generic_get(message, fy_value("role")),
    "assistant"
);

int port = fy_generic_get_int_default(
    fy_generic_get(
        fy_generic_get(config, fy_value("server")),
        fy_value("port")
    ),
    8080
);
```

This is far from ideal:
- **Verbose**: Multiple function calls for simple operations
- **Nested lookups are unreadable**: Hard to follow the chain
- **Type conversion noise**: `fy_value()` everywhere
- **No Python-like defaults**: Can't naturally express "default to empty dict"

## Solution: Short-Form API with _Generic Dispatch

Using C11's `_Generic` feature, we can create type-safe shortcuts that dispatch based on the default value type:

### Core Design

```c
// Mapping lookup with default
#define fy_map_get(map, key, default) \
    _Generic((default), \
        const char *: fy_map_get_string, \
        char *: fy_map_get_string, \
        int: fy_map_get_int, \
        long: fy_map_get_int, \
        long long: fy_map_get_int, \
        double: fy_map_get_double, \
        float: fy_map_get_double, \
        bool: fy_map_get_bool, \
        fy_generic: fy_map_get_generic \
    )(map, key, default)

// Sequence indexing with default
#define fy_seq_get(seq, idx, default) \
    _Generic((default), \
        const char *: fy_seq_get_string, \
        char *: fy_seq_get_string, \
        int: fy_seq_get_int, \
        long: fy_seq_get_int, \
        long long: fy_seq_get_int, \
        double: fy_seq_get_double, \
        float: fy_seq_get_double, \
        bool: fy_seq_get_bool, \
        fy_generic: fy_seq_get_generic \
    )(seq, idx, default)
```

### Helper Functions

```c
// Mapping lookups
static inline const char *fy_map_get_string(fy_generic map, const char *key, const char *def);
static inline int64_t fy_map_get_int(fy_generic map, const char *key, int64_t def);
static inline double fy_map_get_double(fy_generic map, const char *key, double def);
static inline bool fy_map_get_bool(fy_generic map, const char *key, bool def);
static inline fy_generic fy_map_get_generic(fy_generic map, const char *key, fy_generic def);

// Sequence indexing
static inline const char *fy_seq_get_string(fy_generic seq, size_t idx, const char *def);
static inline int64_t fy_seq_get_int(fy_generic seq, size_t idx, int64_t def);
static inline double fy_seq_get_double(fy_generic seq, size_t idx, double def);
static inline bool fy_seq_get_bool(fy_generic seq, size_t idx, bool def);
static inline fy_generic fy_seq_get_generic(fy_generic seq, size_t idx, fy_generic def);
```

### Result

```c
// Clean, readable API
const char *role = fy_map_get(message, "role", "assistant");

int port = fy_map_get(
    fy_map_get(config, "server", fy_map_empty),
    "port",
    8080
);
```

**Benefits**:
- Type-safe: Compiler catches type mismatches
- Concise: One function call per lookup
- Natural defaults: Type inferred from default value
- Chainable: Nested lookups compose naturally

## Type Checking Shortcuts

Long function names were replaced with short, intuitive names:

### Before vs After

| Verbose API | Short-Form API |
|------------|----------------|
| `fy_generic_get_type(g)` | `fy_type(g)` |
| `fy_generic_is_null(g)` | `fy_is_null(g)` |
| `fy_generic_is_bool(g)` | `fy_is_bool(g)` |
| `fy_generic_is_int(g)` | `fy_is_int(g)` |
| `fy_generic_is_float(g)` | `fy_is_float(g)` |
| `fy_generic_is_string(g)` | `fy_is_string(g)` |
| `fy_generic_is_sequence(g)` | `fy_is_seq(g)` |
| `fy_generic_is_mapping(g)` | `fy_is_map(g)` |
| `fy_generic_sequence_get_item_count(g)` | `fy_seq_count(g)` |
| `fy_generic_mapping_get_pair_count(g)` | `fy_map_count(g)` |

### Example Usage

```c
// Type switching
switch (fy_type(value)) {
    case FYGT_MAPPING:
        printf("Map with %zu entries\n", fy_map_count(value));
        break;
    case FYGT_SEQUENCE:
        printf("Sequence with %zu items\n", fy_seq_count(value));
        break;
    case FYGT_STRING:
        printf("String: %s\n", fy_string_get(value));
        break;
    default:
        break;
}

// Type checking
if (fy_is_map(config)) {
    const char *host = fy_map_get(config, "host", "localhost");
    int port = fy_map_get(config, "port", 8080);
}
```

## Advanced: Type Casting with fy_cast()

Beyond direct container access, we provide `fy_cast()` and `fy_get()` for type-safe conversion of fy_generic values with explicit defaults.

### Design with _Generic Dispatch

Using C11's `_Generic`, we dispatch based on the type of the default value:

```c
// fy_cast() - cast value with explicit default
#define fy_cast(_v, _default) \
    fy_generic_cast((_v), (_default))

// fy_get() - get from container with explicit default
#define fy_get(_colv, _key, _default) \
    fy_cast(fy_generic_get((_colv), (_key)), (_default))

// fy_castp() - cast via pointer (no alloca for inline strings)
#define fy_castp(_vp, _default) \
    fy_generic_castp((_vp), (_default))
```

**Usage examples:**

```c
// Access config values with explicit defaults
int port = fy_get(config, "port", 8080);              // 8080 if missing
const char *host = fy_get(config, "host", "localhost");  // "localhost" if missing
bool enabled = fy_get(config, "enabled", false);       // false if missing

// Type's zero/empty value as default
int count = fy_get(config, "count", 0);             // 0 if missing
const char *name = fy_get(config, "name", "");      // "" if missing

// Sized strings (for YAML strings with embedded \0 or binary data)
fy_generic_sized_string data = fy_get(config, "binary", fy_szstr_empty);
// data.data may contain \0 bytes, use data.size for length

// Example: YAML file with embedded null
// config.yaml:
//   text: "Hello\0World"  # Double-quoted strings can contain escaped \0
//   binary: "\x00\x01\xFF"

// Decorated integers (for full unsigned long long range)
fy_generic_decorated_int num = fy_cast(value, fy_dint_empty);
if (num.is_unsigned)
    printf("Unsigned: %llu\n", num.uv);
else
    printf("Signed: %lld\n", num.sv);

// Example: Large unsigned value
// config.yaml:
//   max_uint64: 18446744073709551615  # 0xFFFFFFFFFFFFFFFF
```

### Extended Types: Sized Strings and Decorated Integers

**Sized strings** (`fy_generic_sized_string`):
- **Purpose**: Handle YAML strings with embedded `\0` bytes
- **Fields**: `.data` (may contain nulls), `.size` (explicit length)
- **Use case**: Binary data, YAML double-quoted strings with escaped nulls

**Decorated integers** (`fy_generic_decorated_int`):
- **Purpose**: Full `unsigned long long` range coverage without consuming a type tag bit
- **Fields**: `.sv` / `.uv` (signed/unsigned union), `.is_unsigned` (signedness flag)
- **Design rationale**: The generic system already unifies C integer types (short/int/long/long long). Adding a type tag for signed/unsigned would be inconsistent—why preserve signedness but not width?

**What `is_unsigned` means:**
- **For inline values** (≤61 bits): `is_unsigned = (value >= 0)` → "Can be interpreted as unsigned"
- **For out-of-place values**: `is_unsigned` preserves creation intent → Values > `LLONG_MAX` require `is_unsigned=true` for correct emission

**This is value-range coverage, not type preservation:**
```c
// Generic system is intentionally lossy about C types:
short s = 42;         // → FYGT_INT
int i = 42;           // → FYGT_INT
long l = 42;          // → FYGT_INT
long long ll = 42;    // → FYGT_INT
// All become the same generic int - width information lost

// But we need full value range:
unsigned long long max = 0xFFFFFFFFFFFFFFFF;  // 18446744073709551615
// Without decorated int, this would wrap to negative when emitted
// With decorated int: is_unsigned=true ensures correct YAML output

// The trade-off is correct:
// - Don't waste type tag bit on signed/unsigned (inconsistent with losing width info)
// - Do add minimal metadata (1 bool per out-of-place int) for value range coverage
```

### Container Handle Types

To avoid exposing raw pointers in the API, we wrap container pointers in opaque handle structures:

```c
// Opaque wrapper types for type safety
typedef struct {
    struct fy_generic_sequence *seq;
} fy_seq_handle;

typedef struct {
    struct fy_generic_mapping *map;
} fy_map_handle;

// Sentinel values for invalid/missing containers
extern const fy_seq_handle fy_seq_invalid;
extern const fy_map_handle fy_map_invalid;
```

### Unified Polymorphic Operations

**The public API** - these are the only operations external users need:

**Read operations**:
```c
// fy_len() - works on sequences, mappings, strings, and fy_generic
#define fy_len(v) \
    _Generic((v), \
        fy_seq_handle: fy_seq_handle_count, \
        fy_map_handle: fy_map_handle_count, \
        const char *: strlen, \
        char *: strlen, \
        fy_generic: fy_generic_len \
    )(v)

// fy_get_item() - unified indexing/lookup
#define fy_get_item(container, key) \
    _Generic((container), \
        fy_seq_handle: fy_seq_handle_get, \
        fy_map_handle: fy_map_handle_lookup, \
        fy_generic: fy_generic_get_item \
    )(container, key)

// fy_is_valid() - unified validity check
#define fy_is_valid(v) \
    _Generic((v), \
        fy_seq_handle: fy_seq_handle_is_valid, \
        fy_map_handle: fy_map_handle_is_valid, \
        fy_generic: fy_generic_is_valid \
    )(v)
```

**Functional operations** (immutable - return new collections):
```c
// Polymorphic operations - work with or without builder!

// fy_assoc() - associate key with value (returns new mapping)
#define fy_assoc(...) \
    FY_ASSOC_SELECT(__VA_ARGS__)(__VA_ARGS__)

#define FY_ASSOC_SELECT(...) \
    _Generic((FY_FIRST_ARG(__VA_ARGS__)), \
        struct fy_generic_builder *: fy_gb_map_handle_assoc, \
        fy_map_handle: fy_map_handle_assoc \
    )

// fy_dissoc() - dissociate key (returns new mapping without key)
#define fy_dissoc(...) \
    FY_DISSOC_SELECT(__VA_ARGS__)(__VA_ARGS__)

#define FY_DISSOC_SELECT(...) \
    _Generic((FY_FIRST_ARG(__VA_ARGS__)), \
        struct fy_generic_builder *: fy_gb_map_handle_dissoc, \
        fy_map_handle: fy_map_handle_dissoc \
    )

// fy_conj() - conjoin/append value (returns new sequence)
#define fy_conj(...) \
    FY_CONJ_SELECT(__VA_ARGS__)(__VA_ARGS__)

#define FY_CONJ_SELECT(...) \
    _Generic((FY_FIRST_ARG(__VA_ARGS__)), \
        struct fy_generic_builder *: fy_gb_seq_handle_conj, \
        fy_seq_handle: fy_seq_handle_conj \
    )

// fy_assoc_at() - associate at index (returns new sequence)
#define fy_assoc_at(...) \
    FY_ASSOC_AT_SELECT(__VA_ARGS__)(__VA_ARGS__)

#define FY_ASSOC_AT_SELECT(...) \
    _Generic((FY_FIRST_ARG(__VA_ARGS__)), \
        struct fy_generic_builder *: fy_gb_seq_handle_assoc_at, \
        fy_seq_handle: fy_seq_handle_assoc_at \
    )
```

**Usage patterns**:

```c
// Stack-allocated (no builder - immutable, structural sharing)
fy_seq_handle seq1 = fy_cast(fy_local_sequence("a", "b"), fy_seq_invalid);
fy_seq_handle seq2 = fy_conj(seq1, fy_string("c"));  // Returns new with sharing

// Heap-allocated via builder (persistent beyond stack scope)
// Builder automatically internalizes values when you use it!
struct fy_generic_builder *gb = fy_generic_builder_create();

fy_seq_handle seq3 = fy_cast(fy_local_sequence("a", "b"), fy_seq_invalid);
fy_seq_handle seq4 = fy_conj(gb, seq3, fy_string("c"));  // Builder internalizes

// Same API, different allocation strategy!
fy_map_handle map1 = fy_cast(fy_local_mapping("key", 1), fy_map_invalid);
fy_map_handle map2 = fy_assoc(map1, "key2", fy_int(42));     // Stack
fy_map_handle map3 = fy_assoc(gb, map1, "key2", fy_int(42)); // Heap (internalized)

fy_generic_builder_destroy(gb);
```

**Immutability principle**:

All generic values are **immutable**. Modifications return new collections without changing the original:

```c
// Original sequence unchanged
fy_seq_handle original = /* ... */;
fy_seq_handle modified = fy_conj(original, fy_string("new item"));

// Both exist independently
printf("Original: %zu items\n", fy_len(original));  // Unchanged
printf("Modified: %zu items\n", fy_len(modified));  // Original + 1
```

**Functional equivalence**:

```python
# Python (immutable tuples)      # libfyaml
len(container)                   # fy_len(container)
container[key]                   # fy_get(container, key)
new_dict = {**dict, key: value}  # fy_assoc(map, key, value)
new_list = list + [value]        # fy_conj(seq, value)
```

```clojure
; Clojure (persistent data structures)  ; libfyaml
(count coll)                             ; fy_len(container)
(get coll key)                           ; fy_get_item(container, key)
(assoc map key value)                    ; fy_assoc(map, key, value)
(dissoc map key)                         ; fy_dissoc(map, key)
(conj seq value)                         ; fy_conj(seq, value)
```

**Why polymorphic operations?**

Instead of exposing type-specific functions like `fy_seq_handle_count()` or `fy_map_handle_lookup()`, the API provides a unified interface that works across all types. This matches how `len()` works on everything in Python, and how Clojure's persistent data structures provide uniform operations.

**Benefits**:
- **Simpler API**: Learn a few core operations instead of dozens
- **Natural code**: Write `fy_len(container)` regardless of type
- **Type safety**: `_Generic` ensures correct dispatch at compile time
- **Functional**: Immutable values enable thread-safe, predictable code
- **Performance**: Structural sharing makes copies cheap

### Value Identity: The Power of Immutability + Deduplication

**Critical architectural insight:** When you combine immutability with deduplication, you get **value identity**—each unique value exists exactly once in the builder's memory.

**Two types of value stability:**

1. **Inline objects** (small ints ≤61 bits, short strings ≤7 bytes, 32-bit floats): Stored directly in the 64-bit `fy_generic` value—inherently stable, no builder needed
2. **Builder-allocated objects** (large values, collections): Deduplicated within the builder—single representation per unique value

**Builder-allocated objects with deduplication:**
```c
// Create builder with deduplication policy
struct fy_generic_builder *gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .policy = FY_ALLOC_DEDUP
});

// Two identical strings get deduplicated
fy_generic s1 = fy_gb_to_generic(gb, "hello");
fy_generic s2 = fy_gb_to_generic(gb, "hello");

// Because of deduplication: s1 == s2 (literally the same 64-bit value!)
// No strcmp() needed - just pointer/value comparison - O(1)

fy_generic_builder_destroy(gb);
```

**Inline objects are always stable:**
```c
// Small integers don't need builder
fy_generic n1 = fy_value(42);  // Inline storage (no allocation)
fy_generic n2 = fy_value(42);  // Same inline representation

if (n1 == n2) {  // TRUE - inline values inherently stable
    printf("Same!\n");
}
```

**Consequences for equality:**
- **String comparison**: O(1) instead of O(n)
- **Tree comparison**: O(1) instead of O(tree size)
- **Collection membership**: O(1) instead of deep traversal
- **Hash tables**: Use `fy_generic` value directly as hash

**Real-world implications:**
```c
struct fy_generic_builder *gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .policy = FY_ALLOC_DEDUP
});

// Efficient set operations
fy_generic collection = fy_gb_sequence(gb, 1, 42, 100, 42, 200);

// All instances of 42 are inline (inherently same value)
// Collections are deduplicated in builder

// Perfect for memoization
fy_generic cached_result = memoize_table_lookup(input);  // O(1)
if (cached_result == fy_invalid) {
    // Compute...
}

// Efficient structural sharing
// Can cheaply check if two trees share subtrees
if (tree1_node == tree2_node) {
    // Identical subtrees - no need to compare recursively
}

fy_generic_builder_destroy(gb);
```

**Why this matters for performance:**
- Traditional libraries: `strcmp("hello", "hello")` → 6 char comparisons
- libfyaml with dedup builder: `s1 == s2` → 1 integer comparison
- Hash tables can use the value directly (no hash function needed!)
- Cache lookups are trivial
- Persistent data structures become practical in C

**Global existence optimization:**

The dedup builder maintains a global table of all values. Before searching for a key in a collection, you can check if it exists anywhere—but this optimization requires careful allocator management to avoid **builder pollution**.

**The builder pollution problem:**

When you create temporary keys for existence checks using `fy_gb_to_generic(gb, "search_key")`, those keys get permanently inserted into the builder's dedup table. This happens even when you're just searching, not storing. The builder state becomes polluted with temporary data that was never meant to be persistent.

```c
// ❌ NAIVE PATTERN - HAS POLLUTION ISSUE
struct fy_generic_builder *gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .policy = FY_ALLOC_DEDUP
});

fy_generic config = /* ... load config ... */;

// This pollutes the builder!
fy_generic key = fy_gb_to_generic(gb, "debug_mode");  // Permanently added to dedup table

if (!fy_builder_contains_value(gb, key)) {
    return fy_invalid;
}
// Problem: "debug_mode" string now lives in builder forever, even if not in config
```

**Solution: Stacked allocators (read-only + writable):**

The proper pattern uses a **stacked allocator design** with:
- **Base builder** (read-only during requests): Contains actual persistent data
- **Request-scoped child builder**: Temporary allocations that can be reset after each request

```c
// ✅ CORRECT PATTERN - STACKED ALLOCATORS
// Base builder with actual data (read-only during request processing)
struct fy_generic_builder *base_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .policy = FY_ALLOC_DEDUP
});

// Load persistent configuration
fy_generic config = /* ... parse config files ... */;

// Request handler
void handle_request(fy_generic req_data) {
    // Create child builder for request-scoped temporary allocations
    struct fy_generic_builder *req_gb = fy_generic_builder_create_child(base_gb, &(struct fy_generic_builder_cfg){
        .policy = FY_ALLOC_ARENA  // Fast temporary allocations
    });

    // Temporary key for search (allocated in request-scoped builder)
    fy_generic search_key = fy_gb_to_generic(req_gb, "debug_mode");

    // Check if exists in base builder (dedup table not polluted!)
    if (!fy_builder_contains_value(base_gb, search_key)) {
        fy_generic_builder_destroy(req_gb);  // Clean up temporary allocations
        return fy_invalid;  // Fast path: doesn't exist anywhere
    }

    // Key exists somewhere, do actual mapping lookup
    fy_generic value = fy_map_get(config, search_key);

    fy_generic_builder_destroy(req_gb);  // Reset to clean state
    return value;
}

// Later: cleanup base builder
fy_generic_builder_destroy(base_gb);
```

**Why this is powerful:**
- **Negative lookups are cheap**: Most lookups in real systems are for non-existent keys
- **Skip expensive traversals**: If key doesn't exist globally, avoid searching collections
- **No builder pollution**: Temporary keys don't leak into persistent data
- **Clean state**: Each request starts fresh without accumulated search keys
- **Complements value identity**: After global check passes, actual lookup uses O(1) equality
- **Useful for validation**: Check if a set of required keys exists before processing

**Real-world example with validation:**
```c
// Base builder with config
struct fy_generic_builder *base_gb = /* ... */;
fy_generic config = /* ... */;

void validate_config(void) {
    // Request-scoped builder for temporary validation keys
    struct fy_generic_builder *check_gb = fy_generic_builder_create_child(base_gb, &(struct fy_generic_builder_cfg){
        .policy = FY_ALLOC_ARENA
    });

    // Check if config has all required keys
    const char *required[] = {"host", "port", "database", NULL};

    for (const char **p = required; *p; p++) {
        fy_generic key = fy_gb_to_generic(check_gb, *p);  // Temporary in child
        if (!fy_builder_contains_value(base_gb, key)) {
            fprintf(stderr, "Missing required key: %s\n", *p);
            fy_generic_builder_destroy(check_gb);  // Cleanup
            return -1;  // Fast failure - key doesn't exist anywhere
        }
    }

    fy_generic_builder_destroy(check_gb);  // Reset - no pollution!
    // All required keys exist, proceed with actual lookups
}
```

**Status:** ⚠️ **Stacked allocator pattern is a planned/priority feature**. The implementation requires:
- `fy_generic_builder_create_child(parent_gb, cfg)` - Create child builder inheriting parent's dedup table
- Child builder reads from parent's dedup table without modifying it
- Child builder's own allocations are isolated and can be destroyed without affecting parent
- Efficient for request-scoped patterns where temporary allocations need cleanup

**Key point:** Value identity applies to objects stored in a dedup-enabled builder. Inline objects are stable by definition (stored in the value itself).

This is the secret sauce that makes functional programming patterns viable in C.

### Usage Examples

**Functional updates (immutable) - Stack-allocated**:

```c
void demonstrate_stack_allocated(void) {
    // Stack-allocated (temporary) - uses structural sharing
    fy_generic items = fy_local_sequence("alice", "bob", "charlie");
    fy_seq_handle seq1 = fy_get(items, fy_seq_invalid);

    // Add an item - returns NEW sequence, original unchanged
    fy_seq_handle seq2 = fy_conj(seq1, fy_string("dave"));

    printf("Original: %zu items\n", fy_len(seq1));  // 3
    printf("Modified: %zu items\n", fy_len(seq2));  // 4

    // Update at index - returns NEW sequence
    fy_seq_handle seq3 = fy_assoc_at(seq2, 1, fy_string("BOBBY"));

    // Both originals unchanged
    printf("seq1[1]: %s\n", fy_string_get(fy_get_item(seq1, 1)));  // bob
    printf("seq3[1]: %s\n", fy_string_get(fy_get_item(seq3, 1)));  // BOBBY

    // Chain operations naturally
    fy_seq_handle seq4 = fy_conj(fy_conj(seq1, fy_string("eve")), fy_string("frank"));
    printf("Chained: %zu items\n", fy_len(seq4));  // 5
}
```

**Functional updates with builder - Heap-allocated**:

```c
void demonstrate_builder_allocated(void) {
    // Heap-allocated via builder - builder automatically internalizes
    struct fy_generic_builder *gb = fy_generic_builder_create();

    // Start with stack-allocated sequence
    fy_seq_handle seq1 = fy_get(fy_local_sequence("alice", "bob"), fy_seq_invalid);

    // Add items using builder - builder internalizes automatically!
    fy_seq_handle seq2 = fy_conj(gb, seq1, fy_string("charlie"));
    fy_seq_handle seq3 = fy_conj(gb, seq2, fy_string("dave"));

    printf("Built sequence: %zu items\n", fy_len(seq3));  // 4

    // Update at index - still returns NEW sequence
    fy_seq_handle seq4 = fy_assoc_at(gb, seq3, 1, fy_string("BOBBY"));

    // Originals unchanged
    printf("seq3[1]: %s\n", fy_string_get(fy_get_item(seq3, 1)));  // bob
    printf("seq4[1]: %s\n", fy_string_get(fy_get_item(seq4, 1)));  // BOBBY

    // All values now managed by builder, persist beyond stack scope
    fy_generic_builder_destroy(gb);
}
```

**Functional mapping updates - Stack-allocated**:

```c
void demonstrate_map_updates_stack(void) {
    // Start with an immutable mapping (stack-allocated)
    fy_generic config = fy_local_mapping(
        "host", "localhost",
        "port", 8080,
        "enabled", true
    );
    fy_map_handle map1 = fy_get(config, fy_map_invalid);

    // Associate new key - returns NEW map
    fy_map_handle map2 = fy_assoc(map1, "timeout", fy_int(30));

    printf("Original: %zu entries\n", fy_len(map1));  // 3
    printf("Modified: %zu entries\n", fy_len(map2));  // 4

    // Update existing key - returns NEW map
    fy_map_handle map3 = fy_assoc(map1, "port", fy_int(9090));

    // Originals unchanged
    fy_generic port1 = fy_get_item(map1, "port");
    fy_generic port3 = fy_get_item(map3, "port");

    printf("map1 port: %lld\n", fy_int_get(port1));  // 8080
    printf("map3 port: %lld\n", fy_int_get(port3));  // 9090

    // Dissociate key - returns NEW map without that key
    fy_map_handle map4 = fy_dissoc(map1, "enabled");

    printf("After dissoc: %zu entries\n", fy_len(map4));  // 2
    printf("Original still: %zu entries\n", fy_len(map1));  // 3 (unchanged)
}
```

**Functional mapping updates - Heap-allocated with builder**:

```c
void demonstrate_map_updates_builder(void) {
    // Heap-allocated via builder - builder automatically internalizes
    struct fy_generic_builder *gb = fy_generic_builder_create();

    // Start with stack-allocated mapping
    fy_map_handle map1 = fy_get(
        fy_local_mapping("host", "localhost", "port", 8080, "enabled", true),
        fy_map_invalid
    );

    printf("Initial: %zu entries\n", fy_len(map1));  // 3

    // Builder automatically internalizes when you use it!
    fy_map_handle map2 = fy_assoc(gb, map1, "timeout", fy_int(30));

    printf("With timeout: %zu entries\n", fy_len(map2));  // 4
    printf("Original: %zu entries\n", fy_len(map1));      // 3 (unchanged)

    // Update and dissociate - all managed by builder
    fy_map_handle map3 = fy_assoc(gb, map1, "port", fy_int(9090));
    fy_map_handle map4 = fy_dissoc(gb, map1, "enabled");

    printf("Updated port: %zu entries\n", fy_len(map3));  // 3
    printf("Removed key: %zu entries\n", fy_len(map4));   // 2

    // All values persist beyond stack scope, managed by builder
    fy_generic_builder_destroy(gb);
}
```

**Comparison with functional languages**:

```clojure
; Clojure (persistent data structures)
(def items ["alice" "bob" "charlie"])
(def items2 (conj items "dave"))           ; Original unchanged
(def items3 (assoc items 1 "BOBBY"))       ; Returns new vector

(def config {:host "localhost" :port 8080})
(def config2 (assoc config :timeout 30))   ; Returns new map
(def config3 (dissoc config :enabled))     ; Returns new map
```

```c
// libfyaml - same semantics!
fy_generic items = fy_local_sequence("alice", "bob", "charlie");
fy_seq_handle items2 = fy_conj(items, fy_string("dave"));        // Original unchanged
fy_seq_handle items3 = fy_assoc_at(items, 1, fy_string("BOBBY")); // Returns new

fy_generic config = fy_local_mapping("host", "localhost", "port", 8080);
fy_map_handle config2 = fy_assoc(config, "timeout", fy_int(30));  // Returns new map
fy_map_handle config3 = fy_dissoc(config, "enabled");             // Returns new map
```

**Python-like optional defaults**:
```c
// Python: value (no conversion)
fy_generic v = some_value;

// Python: str(value) or ""
const char *text = fy_cast(v, "");
const char *name = fy_cast(v, "unknown");

// Python: int(value) or 0
int port = fy_cast(v, 0);
int count = fy_cast(v, 42);

// Python: bool(value) or False
bool enabled = fy_cast(v, false);

// Python: float(value) or 0.0
double ratio = fy_cast(v, 0.0);
```

**Type-safe container extraction with unified operations**:
```c
// Extract as sequence handle
fy_seq_handle items = fy_cast(value, fy_seq_invalid);
if (fy_is_valid(items)) {
    for (size_t i = 0; i < fy_len(items); i++) {
        fy_generic item = fy_get(items, i);
        const char *name = fy_cast(item, "");
        printf("%s\n", name);
    }
}

// Extract as mapping handle
fy_map_handle config = fy_cast(value, fy_map_invalid);
if (fy_is_valid(config)) {
    fy_generic host_val = fy_get(config, "host");
    const char *host = fy_cast(host_val, "localhost");
    printf("Host: %s\n", host);
}

// Works with all types
printf("Items: %zu\n", fy_len(items));
printf("Config: %zu\n", fy_len(config));
printf("Name: %zu\n", fy_len("example"));
```

**Combined with fy_map_get() - Complete example**:
```c
// Extract nested containers type-safely
fy_map_handle db_config = fy_map_get(root, "database", fy_map_invalid);
fy_seq_handle users = fy_map_get(data, "users", fy_seq_invalid);

if (fy_is_valid(db_config)) {
    fy_generic host = fy_get_item(db_config, "host");
    printf("DB Host: %s\n", fy_get(host, "localhost"));
    printf("Config entries: %zu\n", fy_len(db_config));
}

if (fy_is_valid(users)) {
    printf("Users: %zu\n", fy_len(users));

    // Iterate using unified operations
    for (size_t i = 0; i < fy_len(users); i++) {
        fy_generic user = fy_get_item(users, i);
        const char *username = fy_get(user, "anonymous");
        printf("  User %zu: %s\n", i, username);
    }
}
```

### Benefits

1. **Python-like syntax**: Optional defaults match Python's flexibility
2. **No pointer exposure**: Handles hide implementation details
3. **Type safety**: `_Generic` ensures correct dispatch
4. **Consistent API**: Same pattern across all types
5. **Clean error handling**: Invalid handles are explicit and checkable
6. **Unified operations**: `fy_len()`, `fy_get_item()`, and `fy_is_valid()` work polymorphically
7. **Natural iteration**: Loop over containers just like Python with `for (i = 0; i < fy_len(c); i++)`

## The Empty Collection Pattern

The key insight for Python-equivalence: **default to empty collections instead of `fy_invalid`**.

### Motivation

When chaining lookups, Python uses empty dicts/lists as defaults:

```python
# Python: default to empty dict
port = config.get("server", {}).get("port", 8080)

# Python: default to empty list
users = data.get("users", [])
```

Using `fy_invalid` doesn't match this pattern:
```c
// Awkward: using fy_invalid
int port = fy_map_get(
    fy_map_get(config, "server", fy_invalid),  // Not semantically clear
    "port",
    8080
);
```

### Solution: fy_map_empty and fy_seq_empty

```c
// Define empty collection constants
#define fy_map_empty  /* empty mapping constant */
#define fy_seq_empty  /* empty sequence constant */
```

These can be implemented as:
- Static inline function returning empty generic
- Preprocessor macro expanding to `fy_local_mapping()` / `fy_local_sequence()`
- Global constant (if generics are POD-compatible)

### Usage

```c
// Now matches Python exactly!
int port = fy_map_get(
    fy_map_get(config, "server", fy_map_empty),
    "port",
    8080
);

fy_generic users = fy_map_get(data, "users", fy_seq_empty);

// Deep navigation with type safety
const char *db_host = fy_map_get(
    fy_map_get(
        fy_map_get(root, "database", fy_map_empty),
        "connection",
        fy_map_empty
    ),
    "host",
    "localhost"
);
```

### Benefits

1. **Semantic clarity**: "Default to empty collection" is clearer than "default to invalid"
2. **Type correctness**: The default matches the expected type
3. **Python equivalence**: Exact match with Python's `.get({})` and `.get([])`
4. **Safe chaining**: Looking up in empty map/seq returns the next default
5. **No special cases**: Empty collections behave like any other collection

