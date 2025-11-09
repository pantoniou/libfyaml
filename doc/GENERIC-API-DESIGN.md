# Generic API Design: Achieving Python Ergonomics in C

This document describes the design evolution of libfyaml's generic type system API, showing how careful API design can achieve Python-level ergonomics while maintaining C's type safety and performance.

## Table of Contents

1. [The Problem: Verbose Generic APIs](#the-problem-verbose-generic-apis)
2. [Solution: Short-Form API with _Generic Dispatch](#solution-short-form-api-with-_generic-dispatch)
3. [Type Checking Shortcuts](#type-checking-shortcuts)
4. [Advanced: Unified fy_get() with Optional Parameters](#advanced-unified-fy_get-with-optional-parameters)
5. [The Empty Collection Pattern](#the-empty-collection-pattern)
6. [Complete API Reference](#complete-api-reference)
7. [Real-World Example: Anthropic Messages API](#real-world-example-anthropic-messages-api)
8. [Language Comparisons](#language-comparisons)

## The Problem: Verbose Generic APIs

Traditional generic/dynamic type APIs in C require verbose function calls with explicit type conversions:

```c
// Verbose approach
const char *role = fy_generic_get_string_default(
    fy_generic_mapping_lookup(message, fy_to_generic("role")),
    "assistant"
);

int port = fy_generic_get_int_default(
    fy_generic_mapping_lookup(
        fy_generic_mapping_lookup(config, fy_to_generic("server")),
        fy_to_generic("port")
    ),
    8080
);
```

This is far from ideal:
- **Verbose**: Multiple function calls for simple operations
- **Nested lookups are unreadable**: Hard to follow the chain
- **Type conversion noise**: `fy_to_generic()` everywhere
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

## Advanced: Unified fy_get() with Optional Parameters

Beyond `fy_map_get()` and `fy_seq_get()`, we can create a unified `fy_get()` that extracts values with optional type-safe defaults.

### Design with __VA_OPT__

Using C11's variadic macros with `__VA_OPT__`, we can make the default parameter optional:

```c
// Macro argument counting helper
#define FY_GET_SELECT(_1, _2, NAME, ...) NAME

// Main fy_get macro with optional second argument
#define fy_get(g, ...) \
    FY_GET_SELECT(__VA_ARGS__, FY_GET_WITH_DEFAULT, FY_GET_NO_DEFAULT, _dummy)(g, ##__VA_ARGS__)

// No default: return as-is (fy_generic)
#define FY_GET_NO_DEFAULT(g, ...) (g)

// With default: dispatch based on type
#define FY_GET_WITH_DEFAULT(g, default, ...) \
    _Generic((default), \
        const char *: fy_get_string, \
        int: fy_get_int, \
        long long: fy_get_int, \
        double: fy_get_double, \
        bool: fy_get_bool, \
        fy_generic: fy_get_generic, \
        fy_seq_handle: fy_get_seq_handle, \
        fy_map_handle: fy_get_map_handle \
    )(g, default)
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
fy_seq_handle seq1 = fy_get(fy_sequence("a", "b"), fy_seq_invalid);
fy_seq_handle seq2 = fy_conj(seq1, fy_string("c"));  // Returns new with sharing

// Heap-allocated via builder (persistent beyond stack scope)
// Builder automatically internalizes values when you use it!
struct fy_generic_builder *gb = fy_generic_builder_create();

fy_seq_handle seq3 = fy_get(fy_sequence("a", "b"), fy_seq_invalid);
fy_seq_handle seq4 = fy_conj(gb, seq3, fy_string("c"));  // Builder internalizes

// Same API, different allocation strategy!
fy_map_handle map1 = fy_get(fy_mapping("key", 1), fy_map_invalid);
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
container[key]                   # fy_get_item(container, key)
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

### Usage Examples

**Functional updates (immutable) - Stack-allocated**:

```c
void demonstrate_stack_allocated(void) {
    // Stack-allocated (temporary) - uses structural sharing
    fy_generic items = fy_sequence("alice", "bob", "charlie");
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
    fy_seq_handle seq1 = fy_get(fy_sequence("alice", "bob"), fy_seq_invalid);

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
    fy_generic config = fy_mapping(
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
        fy_mapping("host", "localhost", "port", 8080, "enabled", true),
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
fy_generic items = fy_sequence("alice", "bob", "charlie");
fy_seq_handle items2 = fy_conj(items, fy_string("dave"));        // Original unchanged
fy_seq_handle items3 = fy_assoc_at(items, 1, fy_string("BOBBY")); // Returns new

fy_generic config = fy_mapping("host", "localhost", "port", 8080);
fy_map_handle config2 = fy_assoc(config, "timeout", fy_int(30));  // Returns new map
fy_map_handle config3 = fy_dissoc(config, "enabled");             // Returns new map
```

**Python-like optional defaults**:
```c
// Python: value  (no conversion)
fy_generic v = fy_get(some_value);

// Python: str(value) or ""
const char *text = fy_get(v, "");
const char *name = fy_get(v, "unknown");

// Python: int(value) or 0
int port = fy_get(v, 0);
int count = fy_get(v, 42);

// Python: bool(value) or False
bool enabled = fy_get(v, false);

// Python: float(value) or 0.0
double ratio = fy_get(v, 0.0);
```

**Type-safe container extraction with unified operations**:
```c
// Extract as sequence handle
fy_seq_handle items = fy_get(value, fy_seq_invalid);
if (fy_is_valid(items)) {
    for (size_t i = 0; i < fy_len(items); i++) {
        fy_generic item = fy_get_item(items, i);
        const char *name = fy_get(item, "");
        printf("%s\n", name);
    }
}

// Extract as mapping handle
fy_map_handle config = fy_get(value, fy_map_invalid);
if (fy_is_valid(config)) {
    fy_generic host_val = fy_get_item(config, "host");
    const char *host = fy_get(host_val, "localhost");
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
- Preprocessor macro expanding to `fy_mapping()` / `fy_sequence()`
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

## Complete API Reference

### The Lean Public API

libfyaml exposes a minimal, polymorphic API that's all you need for working with generic values:

**Read operations (work on everything)**:
- `fy_len(v)` - Get length/count (sequences, mappings, strings)
- `fy_get_item(container, key)` - Index or lookup (sequences by index, mappings by key)
- `fy_is_valid(v)` - Check validity (handles, generics)

**Functional operations (immutable - return new collections)**:
- `fy_assoc(map, key, value)` - Associate key with value (returns new mapping)
- `fy_dissoc(map, key)` - Dissociate key (returns new mapping without key)
- `fy_conj(seq, value)` - Conjoin/append value (returns new sequence)
- `fy_assoc_at(seq, index, value)` - Associate at index (returns new sequence)

**Type-safe extraction**:
- `fy_get(g, default)` - Extract with type-safe default (optional second parameter)
- `fy_map_get(map, key, default)` - Lookup in mapping with default

**Type checking**:
- `fy_type(g)` - Get type enum
- `fy_is_*()` - Type predicates (`fy_is_map`, `fy_is_seq`, `fy_is_string`, etc.)

**Construction**:
- `fy_mapping(...)` - Create mapping (stack-allocated, immutable)
- `fy_sequence(...)` - Create sequence (stack-allocated, immutable)
- `fy_gb_mapping(gb, ...)` - Create mapping via builder (heap-allocated)
- `fy_gb_sequence(gb, ...)` - Create sequence via builder (heap-allocated)

**Key principle**: All values are **immutable**. Operations like `fy_assoc()` and `fy_conj()` return new collections without modifying the original. This enables:
- **Thread safety**: Immutable values are safe to share across threads
- **Predictability**: No action-at-a-distance bugs
- **Structural sharing**: Updates are efficient through shared structure

### Construction

**Stack-allocated (temporary)**:
```c
fy_generic msg = fy_mapping(
    "role", "user",
    "content", "Hello!"
);

fy_generic items = fy_sequence("foo", "bar", "baz");
```

**Heap-allocated (persistent)**:
```c
struct fy_generic_builder *gb = fy_generic_builder_create();

fy_generic msg = fy_gb_mapping(gb,
    "role", "user",
    "content", "Hello!"
);

fy_generic items = fy_gb_sequence(gb, "foo", "bar", "baz");
```

**Empty collections**:
```c
fy_generic empty_map = fy_map_empty;
fy_generic empty_seq = fy_seq_empty;
```

### Type Checking

```c
enum fy_generic_type fy_type(fy_generic g);

bool fy_is_null(fy_generic g);
bool fy_is_bool(fy_generic g);
bool fy_is_int(fy_generic g);
bool fy_is_float(fy_generic g);
bool fy_is_string(fy_generic g);
bool fy_is_seq(fy_generic g);
bool fy_is_map(fy_generic g);
bool fy_is_invalid(fy_generic g);
```

### Value Extraction

**Unified extraction with optional defaults**:
```c
// Extract with type-safe default (optional second parameter)
fy_get(g)           // No conversion, returns fy_generic
fy_get(g, "")       // Extract as string with default
fy_get(g, 0)        // Extract as int with default
fy_get(g, 0.0)      // Extract as double with default
fy_get(g, false)    // Extract as bool with default
```

**Direct extraction (no defaults)**:
```c
bool fy_bool_get(fy_generic g);
int64_t fy_int_get(fy_generic g);
double fy_float_get(fy_generic g);
const char *fy_string_get(fy_generic g);
```

**Container handles and polymorphic operations**:
```c
// Handle types
typedef struct { struct fy_generic_sequence *seq; } fy_seq_handle;
typedef struct { struct fy_generic_mapping *map; } fy_map_handle;

// Sentinels
extern const fy_seq_handle fy_seq_invalid;
extern const fy_map_handle fy_map_invalid;

// Extract handles using fy_get() or fy_map_get()
fy_seq_handle sh = fy_get(g, fy_seq_invalid);
fy_map_handle mh = fy_map_get(data, "config", fy_map_invalid);

// ========================================
// PUBLIC API: Polymorphic Operations
// ========================================
// These 3 operations are all you need:

fy_len(v)                   // Get length/count - works on sequences, maps, strings
fy_get_item(container, key) // Get item by index/key - works on sequences, maps
fy_is_valid(v)              // Check validity - works on handles, fy_generic
```

### High-Level Container Operations

**For fy_generic values** (when you have a `fy_generic` directly):

```c
// Lookup with type-safe default (uses _Generic dispatch)
fy_map_get(map, key, default)    // Returns typed value or default

// Traditional operations (for backwards compatibility)
size_t fy_map_count(fy_generic map);
size_t fy_seq_count(fy_generic seq);
```

**For handles** (recommended - use polymorphic operations):

```c
// Extract handles
fy_seq_handle items = fy_map_get(data, "items", fy_seq_invalid);
fy_map_handle config = fy_map_get(data, "config", fy_map_invalid);

// Then use polymorphic operations:
fy_len(items)           // Get count
fy_get_item(items, 0)   // Get by index
fy_is_valid(items)      // Check validity
```

### Document Integration

```c
// Generic to document
struct fy_document *fy_generic_to_document(const struct fy_document_cfg *cfg, fy_generic g);

// Document to generic
fy_generic fy_document_to_generic(struct fy_document *fyd);

// Parse directly to generic
fy_generic fy_parse_to_generic(const char *input, size_t len);

// Emit generic as JSON/YAML
char *fy_emit_generic_to_string(fy_generic g, enum fy_emitter_cfg_flags flags);
```

## Real-World Example: Anthropic Messages API

### Parsing a Response

```c
#include <libfyaml.h>

void parse_anthropic_response(const char *json_response) {
    // Parse JSON to document
    struct fy_document *doc = fy_document_build_from_string(NULL, json_response, -1);
    fy_generic response = fy_document_to_generic(doc);

    // Extract top-level fields - clean and readable
    const char *id = fy_map_get(response, "id", "");
    const char *model = fy_map_get(response, "model", "");
    const char *role = fy_map_get(response, "role", "assistant");
    const char *stop_reason = fy_map_get(response, "stop_reason", "");

    printf("Message ID: %s\n", id);
    printf("Model: %s\n", model);
    printf("Role: %s\n", role);
    printf("Stop reason: %s\n", stop_reason);

    // Chained lookup with fy_map_empty - just like Python!
    int input_tokens = fy_map_get(
        fy_map_get(response, "usage", fy_map_empty),
        "input_tokens",
        0
    );
    int output_tokens = fy_map_get(
        fy_map_get(response, "usage", fy_map_empty),
        "output_tokens",
        0
    );

    printf("Tokens: %d input, %d output\n", input_tokens, output_tokens);

    // Get content array - use handles for type safety
    fy_seq_handle content = fy_map_get(response, "content", fy_seq_invalid);

    // Polymorphic operations: fy_len() and fy_get_item() work on handles!
    for (size_t i = 0; i < fy_len(content); i++) {
        fy_generic block = fy_get_item(content, i);

        const char *type = fy_map_get(block, "type", "unknown");

        if (strcmp(type, "text") == 0) {
            const char *text = fy_map_get(block, "text", "");
            printf("  [text] %s\n", text);

        } else if (strcmp(type, "tool_use") == 0) {
            const char *tool_id = fy_map_get(block, "id", "");
            const char *tool_name = fy_map_get(block, "name", "");

            printf("  [tool_use] %s (id: %s)\n", tool_name, tool_id);

            // Chained lookup into tool input
            const char *query = fy_map_get(
                fy_map_get(block, "input", fy_map_empty),
                "query",
                ""
            );

            if (query[0]) {
                printf("    Query: %s\n", query);
            }
        }
    }

    fy_document_destroy(doc);
}
```

### Building a Request

```c
struct fy_document *build_anthropic_request(struct fy_generic_builder *gb) {
    // Create message history with natural nesting
    fy_generic messages = fy_gb_sequence(gb,
        fy_mapping(
            "role", "user",
            "content", "What's the weather in San Francisco?"
        ),
        fy_mapping(
            "role", "assistant",
            "content", fy_sequence(
                fy_mapping(
                    "type", "text",
                    "text", "I'll check the weather for you."
                ),
                fy_mapping(
                    "type", "tool_use",
                    "id", "toolu_01A",
                    "name", "get_weather",
                    "input", fy_mapping("location", "San Francisco, CA")
                )
            )
        ),
        fy_mapping(
            "role", "user",
            "content", fy_sequence(
                fy_mapping(
                    "type", "tool_result",
                    "tool_use_id", "toolu_01A",
                    "content", "72°F and sunny"
                )
            )
        )
    );

    // Define available tools
    fy_generic tools = fy_gb_sequence(gb,
        fy_mapping(
            "name", "get_weather",
            "description", "Get current weather for a location",
            "input_schema", fy_mapping(
                "type", "object",
                "properties", fy_mapping(
                    "location", fy_mapping(
                        "type", "string",
                        "description", "City and state, e.g. San Francisco, CA"
                    )
                ),
                "required", fy_sequence("location")
            )
        )
    );

    // Build complete request
    fy_generic request = fy_gb_mapping(gb,
        "model", "claude-3-5-sonnet-20241022",
        "max_tokens", 1024,
        "messages", messages,
        "tools", tools
    );

    return fy_generic_to_document(NULL, request);
}
```

### Polymorphic Operations in Action

```c
void demonstrate_polymorphic_ops(fy_generic data) {
    // Extract both sequences and mappings using handles
    fy_seq_handle messages = fy_map_get(data, "messages", fy_seq_invalid);
    fy_map_handle config = fy_map_get(data, "config", fy_map_invalid);
    const char *api_key = fy_map_get(data, "api_key", "");

    // fy_len() works polymorphically on all types!
    printf("Messages: %zu\n", fy_len(messages));    // sequence
    printf("Config: %zu\n", fy_len(config));         // mapping
    printf("API Key: %zu chars\n", fy_len(api_key)); // string

    // fy_get_item() works on both sequences (by index) and mappings (by key)
    if (fy_is_valid(messages)) {
        for (size_t i = 0; i < fy_len(messages); i++) {
            fy_generic msg = fy_get_item(messages, i);  // sequence indexing
            const char *role = fy_map_get(msg, "role", "");
            printf("  Message %zu: %s\n", i, role);
        }
    }

    if (fy_is_valid(config)) {
        // fy_get_item() also works on mappings!
        fy_generic host = fy_get_item(config, "host");
        fy_generic port = fy_get_item(config, "port");

        printf("  Host: %s\n", fy_get(host, "localhost"));
        printf("  Port: %d\n", fy_get(port, 8080));
    }
}
```

### Pattern Matching on Sum Types

```c
void handle_content_block(fy_generic block) {
    const char *type = fy_map_get(block, "type", "");

    if (strcmp(type, "text") == 0) {
        const char *text = fy_map_get(block, "text", "");
        printf("Text: %s\n", text);

    } else if (strcmp(type, "image") == 0) {
        fy_generic source = fy_map_get(block, "source", fy_map_empty);
        const char *source_type = fy_map_get(source, "type", "");
        const char *media_type = fy_map_get(source, "media_type", "");

        printf("Image: %s (%s)\n", source_type, media_type);

    } else if (strcmp(type, "tool_use") == 0) {
        const char *id = fy_map_get(block, "id", "");
        const char *name = fy_map_get(block, "name", "");

        // Extract input - could be mapping, sequence, or scalar
        fy_map_handle input_map = fy_map_get(block, "input", fy_map_invalid);
        fy_seq_handle input_seq = fy_map_get(block, "input", fy_seq_invalid);

        printf("Tool: %s (id=%s)\n", name, id);

        // fy_len() works polymorphically!
        if (fy_is_valid(input_map)) {
            printf("  Parameters: %zu fields\n", fy_len(input_map));
        } else if (fy_is_valid(input_seq)) {
            printf("  Parameters: %zu items\n", fy_len(input_seq));
        }
    }
}
```

## Language Comparisons

### Simple Lookup with Defaults

**Python**:
```python
role = message.get("role", "assistant")
port = config.get("port", 8080)
enabled = settings.get("enabled", True)
```

**TypeScript**:
```typescript
const role = message.role ?? "assistant";
const port = config.port ?? 8080;
const enabled = settings.enabled ?? true;
```

**Rust**:
```rust
let role = message.get("role").unwrap_or("assistant");
let port = config.get("port").unwrap_or(8080);
let enabled = settings.get("enabled").unwrap_or(true);
```

**libfyaml**:
```c
const char *role = fy_map_get(message, "role", "assistant");
int port = fy_map_get(config, "port", 8080);
bool enabled = fy_map_get(settings, "enabled", true);
```

### Nested Navigation

**Python**:
```python
port = root.get("server", {}).get("config", {}).get("port", 8080)
```

**TypeScript**:
```typescript
const port = root.server?.config?.port ?? 8080;
```

**Rust**:
```rust
let port = root
    .get("server").and_then(|s| s.as_object())
    .and_then(|s| s.get("config")).and_then(|c| c.as_object())
    .and_then(|c| c.get("port")).and_then(|p| p.as_i64())
    .unwrap_or(8080);
```

**libfyaml**:
```c
int port = fy_map_get(
    fy_map_get(
        fy_map_get(root, "server", fy_map_empty),
        "config",
        fy_map_empty
    ),
    "port",
    8080
);
```

*Note: TypeScript has the cleanest syntax with optional chaining, but libfyaml's approach is nearly as concise and more explicit about defaults.*

### Polymorphic Operations (len/length)

**Python**:
```python
users = data.get("users", [])
config = data.get("config", {})
name = "example"

print(f"Users: {len(users)}")
print(f"Config: {len(config)}")
print(f"Name: {len(name)}")
```

**TypeScript**:
```typescript
const users = data.users ?? [];
const config = data.config ?? {};
const name = "example";

console.log(`Users: ${users.length}`);
console.log(`Config: ${Object.keys(config).length}`);
console.log(`Name: ${name.length}`);
```

**Rust**:
```rust
let users = data.get("users").and_then(|v| v.as_array()).unwrap_or(&vec![]);
let config = data.get("config").and_then(|v| v.as_object()).unwrap_or(&Map::new());
let name = "example";

println!("Users: {}", users.len());
println!("Config: {}", config.len());
println!("Name: {}", name.len());
```

**libfyaml**:
```c
fy_seq_handle users = fy_map_get(data, "users", fy_seq_invalid);
fy_map_handle config = fy_map_get(data, "config", fy_map_invalid);
const char *name = "example";

printf("Users: %zu\n", fy_len(users));
printf("Config: %zu\n", fy_len(config));
printf("Name: %zu\n", fy_len(name));
```

*All four languages support polymorphic length operations. libfyaml's `fy_len()` matches Python's `len()` and Rust's `.len()` semantics.*

### Iteration

**Python**:
```python
users = data.get("users", [])
for user in users:
    name = user.get("name", "anonymous")
    print(name)

# Or with index
for i in range(len(users)):
    user = users[i]
    print(user.get("name", "anonymous"))
```

**TypeScript**:
```typescript
const users = data.users ?? [];
for (const user of users) {
    const name = user.name ?? "anonymous";
    console.log(name);
}

// Or with index
for (let i = 0; i < users.length; i++) {
    const user = users[i];
    console.log(user.name ?? "anonymous");
}
```

**Rust**:
```rust
let users = data.get("users").and_then(|v| v.as_array()).unwrap_or(&vec![]);
for user in users {
    let name = user.get("name")
        .and_then(|v| v.as_str())
        .unwrap_or("anonymous");
    println!("{}", name);
}

// Or with index
for i in 0..users.len() {
    let user = &users[i];
    let name = user.get("name")
        .and_then(|v| v.as_str())
        .unwrap_or("anonymous");
    println!("{}", name);
}
```

**libfyaml**:
```c
fy_seq_handle users = fy_map_get(data, "users", fy_seq_invalid);
for (size_t i = 0; i < fy_len(users); i++) {
    fy_generic user = fy_get_item(users, i);
    const char *name = fy_get(user, "anonymous");
    printf("%s\n", name);
}
```

*libfyaml's iteration is nearly identical to Rust's indexed approach and comparable to Python/TypeScript.*

### Building Data Structures

**Python**:
```python
config = {
    "host": "localhost",
    "port": 8080,
    "users": ["alice", "bob", "charlie"],
    "enabled": True
}
```

**TypeScript**:
```typescript
const config = {
    host: "localhost",
    port: 8080,
    users: ["alice", "bob", "charlie"],
    enabled: true
};
```

**Rust (with serde_json)**:
```rust
let config = json!({
    "host": "localhost",
    "port": 8080,
    "users": ["alice", "bob", "charlie"],
    "enabled": true
});
```

**libfyaml**:
```c
fy_generic config = fy_mapping(
    "host", "localhost",
    "port", 8080,
    "users", fy_sequence("alice", "bob", "charlie"),
    "enabled", true
);
```

*All four languages support concise literal syntax for building data structures.*

### Type Checking and Pattern Matching

**Python**:
```python
if isinstance(value, dict):
    print(f"Map with {len(value)} entries")
elif isinstance(value, list):
    print(f"List with {len(value)} items")
elif isinstance(value, str):
    print(f"String: {value}")
else:
    print(f"Other: {value}")
```

**TypeScript**:
```typescript
if (typeof value === "object" && !Array.isArray(value)) {
    console.log(`Map with ${Object.keys(value).length} entries`);
} else if (Array.isArray(value)) {
    console.log(`List with ${value.length} items`);
} else if (typeof value === "string") {
    console.log(`String: ${value}`);
} else {
    console.log(`Other: ${value}`);
}
```

**Rust (with serde_json::Value)**:
```rust
match value {
    Value::Object(map) => println!("Map with {} entries", map.len()),
    Value::Array(arr) => println!("List with {} items", arr.len()),
    Value::String(s) => println!("String: {}", s),
    _ => println!("Other: {:?}", value),
}
```

**libfyaml**:
```c
switch (fy_type(value)) {
    case FYGT_MAPPING:
        printf("Map with %zu entries\n", fy_map_count(value));
        break;
    case FYGT_SEQUENCE:
        printf("List with %zu items\n", fy_seq_count(value));
        break;
    case FYGT_STRING:
        printf("String: %s\n", fy_string_get(value));
        break;
    default:
        printf("Other type\n");
        break;
}
```

*Rust's pattern matching is most concise. libfyaml's switch statement is comparable and more concise than Python/TypeScript.*

### Complex Example: Processing API Response

**Python**:
```python
def process_response(data):
    users = data.get("users", [])
    total = 0

    for user in users:
        if user.get("active", False):
            age = user.get("age", 0)
            total += age
            profile = user.get("profile", {})
            name = profile.get("name", "unknown")
            print(f"{name}: {age}")

    return total / len(users) if len(users) > 0 else 0
```

**TypeScript**:
```typescript
function processResponse(data: any): number {
    const users = data.users ?? [];
    let total = 0;

    for (const user of users) {
        if (user.active ?? false) {
            const age = user.age ?? 0;
            total += age;
            const profile = user.profile ?? {};
            const name = profile.name ?? "unknown";
            console.log(`${name}: ${age}`);
        }
    }

    return users.length > 0 ? total / users.length : 0;
}
```

**Rust**:
```rust
fn process_response(data: &Value) -> f64 {
    let users = data.get("users")
        .and_then(|v| v.as_array())
        .unwrap_or(&vec![]);
    let mut total = 0;

    for user in users {
        if user.get("active").and_then(|v| v.as_bool()).unwrap_or(false) {
            let age = user.get("age").and_then(|v| v.as_i64()).unwrap_or(0);
            total += age;
            let profile = user.get("profile").and_then(|v| v.as_object()).unwrap_or(&Map::new());
            let name = profile.get("name").and_then(|v| v.as_str()).unwrap_or("unknown");
            println!("{}: {}", name, age);
        }
    }

    if users.len() > 0 { total as f64 / users.len() as f64 } else { 0.0 }
}
```

**libfyaml**:
```c
double process_response(fy_generic data) {
    fy_seq_handle users = fy_map_get(data, "users", fy_seq_invalid);
    int total = 0;

    for (size_t i = 0; i < fy_len(users); i++) {
        fy_generic user = fy_get_item(users, i);

        if (fy_map_get(user, "active", false)) {
            int age = fy_map_get(user, "age", 0);
            total += age;

            fy_generic profile = fy_map_get(user, "profile", fy_map_empty);
            const char *name = fy_map_get(profile, "name", "unknown");

            printf("%s: %d\n", name, age);
        }
    }

    return fy_len(users) > 0 ? (double)total / fy_len(users) : 0.0;
}
```

### Key Observations

1. **Python vs libfyaml**: Nearly identical ergonomics. libfyaml requires type prefixes (`fy_`) and explicit length in loops, but otherwise matches Python's conciseness.

2. **TypeScript vs libfyaml**: TypeScript's optional chaining (`?.`) is more concise for nested access, but libfyaml's explicit defaults are clearer. Both have similar iteration patterns.

3. **Rust vs libfyaml**: Rust's type safety requires more verbose unwrapping. libfyaml achieves similar safety through `_Generic` dispatch but with less ceremony. Rust's pattern matching is more powerful, but libfyaml's switch statements are competitive.

4. **Unified operations**: libfyaml's `fy_len()` provides the same polymorphism as Python's `len()`, Rust's `.len()`, and TypeScript's `.length`, working across sequences, mappings, and strings.

5. **Performance**: libfyaml matches or exceeds all three languages:
   - **vs Python**: Zero-copy, inline storage, no GC pauses
   - **vs TypeScript**: No V8 overhead, direct memory access
   - **vs Rust**: Comparable performance with simpler syntax

### Summary

libfyaml achieves **Python-level ergonomics** while maintaining **C-level performance**:
- Concise syntax through `_Generic` dispatch
- Natural defaults with `fy_map_empty` and `fy_seq_empty`
- Polymorphic operations (`fy_len`, `fy_get_item`)
- Type safety at compile time
- Zero runtime overhead

The API proves that systems languages can match the ergonomics of high-level languages without sacrificing performance or safety.

## Design Principles

The short-form API achieves Python ergonomics through:

1. **C11 `_Generic` dispatch**: Type-safe polymorphism at compile time
2. **Smart defaults**: Use `fy_map_empty` and `fy_seq_empty` instead of error values
3. **Natural chaining**: Empty collections propagate gracefully through lookups
4. **Consistent naming**: Short, intuitive names (`fy_map_get`, `fy_is_map`, etc.)
5. **Zero runtime overhead**: All dispatch happens at compile time
6. **Type safety**: Compiler catches type mismatches in defaults
7. **Unified extraction**: Single `fy_get()` works with optional defaults via `__VA_OPT__`
8. **Container handles**: Opaque wrappers avoid pointer exposure while maintaining type safety
9. **Optional parameters**: Match Python's flexibility with variadic macro tricks
10. **Polymorphic operations**: `fy_len()`, `fy_get_item()`, and `fy_is_valid()` work across types
11. **Python naming**: `fy_len()` matches Python's `len()`, making the API immediately familiar

## Performance Characteristics

- **Compile-time dispatch**: `_Generic` has zero runtime cost
- **Inline storage**: Small values (61-bit ints, 7-byte strings, 32-bit floats) have no allocation overhead
- **Immutable values**: Thread-safe reads without locks
- **Efficient chaining**: Lookups return inline values when possible
- **No hidden allocations**: Stack-allocated temporaries with `alloca()`

## Conclusion

libfyaml's short-form generic API demonstrates that C can achieve the same level of ergonomics as dynamic languages like Python, while maintaining:

- **Type safety**: Compile-time type checking via `_Generic`
- **Performance**: Zero-copy, inline storage, no runtime dispatch overhead
- **Memory safety**: Immutable values, controlled allocation
- **Clarity**: Code reads like Python but executes like C

This design proves that careful API design can bridge the gap between systems languages and scripting languages, offering the best of both worlds.
