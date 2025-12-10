# Generic API Reference

This document provides the complete API reference for libfyaml's generic type system.

## Iteration API

### The `fy_foreach` Macro

libfyaml provides a universal `fy_foreach` macro that works with sequences, mappings, and any value types. It uses C11 `_Generic` and `__typeof__` for type-safe iteration without manual casting.

**Definition**:
```c
#define fy_foreach(_v, _col) \
    for ( \
        struct { size_t i; size_t len; } _iter ## __COUNTER__ = { 0, fy_len(_col) }; \
        _iter ## __COUNTER__ .i < _iter ## __COUNTER__ .len && \
            (((_v) = fy_get_key_at_typed((_col), _iter ## __COUNTER__ .i, __typeof__(_v))), 1); \
        _iter ## __COUNTER__ .i++)
```

**Key features**:
- Works with any collection type (sequences, mappings)
- Automatic type casting based on variable type
- No namespace pollution (`__COUNTER__` ensures unique names)
- Zero runtime overhead (inline expansion)

### Sequence Iteration

#### Iterate as `fy_generic` Values

```c
fy_generic seq = fy_sequence(10, 20, 30, 40);

fy_generic v;
fy_foreach(v, seq) {
    int val = fy_cast(v, 0);
    printf("%d\n", val);
}
// Output: 10, 20, 30, 40
```

#### Iterate with Automatic Type Casting

The macro automatically casts values based on the iterator variable's type:

```c
// Iterate as integers directly
fy_generic seq = fy_sequence(0, 10, 20, 30);

int ival;
fy_foreach(ival, seq) {
    printf("%d\n", ival);
}
// Output: 0, 10, 20, 30
```

```c
// Iterate as strings directly
fy_generic names = fy_sequence("Alice", "Bob", "Charlie");

const char *name;
fy_foreach(name, names) {
    printf("Hello, %s!\n", name);
}
// Output:
// Hello, Alice!
// Hello, Bob!
// Hello, Charlie!
```

#### Complex Values in Sequences

```c
fy_generic users = fy_sequence(
    fy_mapping("name", "Alice", "age", 30),
    fy_mapping("name", "Bob", "age", 25),
    fy_mapping("name", "Charlie", "age", 35)
);

fy_generic user;
fy_foreach(user, users) {
    const char *name = fy_get(user, "name", "unknown");
    int age = fy_get(user, "age", 0);
    printf("%s is %d years old\n", name, age);
}
// Output:
// Alice is 30 years old
// Bob is 25 years old
// Charlie is 35 years old
```

### Mapping Iteration

#### Iterate Over Keys (as strings)

```c
fy_generic config = fy_mapping(
    "host", "localhost",
    "port", 8080,
    "debug", true
);

const char *key;
fy_foreach(key, config) {
    fy_generic value = fy_get(config, key, fy_invalid);
    // Process key-value pair
    printf("%s = ", key);

    switch (fy_type(value)) {
    case FYGT_STRING:
        printf("%s\n", fy_cast(value, ""));
        break;
    case FYGT_INT:
        printf("%d\n", fy_cast(value, 0));
        break;
    case FYGT_BOOL:
        printf("%s\n", fy_cast(value, false) ? "true" : "false");
        break;
    default:
        printf("<other>\n");
        break;
    }
}
// Output:
// host = localhost
// port = 8080
// debug = true
```

#### Iterate Over Key-Value Pairs

Use `fy_generic_map_pair` to iterate over both keys and values simultaneously:

```c
fy_generic config = fy_mapping("foo", 100, "bar", 200, "baz", 300);

fy_generic_map_pair mp;
fy_foreach(mp, config) {
    const char *key = fy_cast(mp.key, "");
    int value = fy_cast(mp.value, 0);
    printf("%s: %d\n", key, value);
}
// Output:
// foo: 100
// bar: 200
// baz: 300
```

### Index-Based Iteration (Traditional)

For cases where you need the index:

```c
fy_generic items = fy_sequence("a", "b", "c");

for (size_t i = 0; i < fy_len(items); i++) {
    fy_generic item = fy_get_item(items, i);
    printf("[%zu] = %s\n", i, fy_cast(item, ""));
}
// Output:
// [0] = a
// [1] = b
// [2] = c
```

### Comparison with Other Languages

**Python**:
```python
for user in users:
    print(user.get("name", "anonymous"))

for key, value in config.items():
    print(f"{key} = {value}")
```

**libfyaml**:
```c
fy_generic user;
fy_foreach(user, users) {
    printf("%s\n", fy_get(user, "name", "anonymous"));
}

fy_generic_map_pair mp;
fy_foreach(mp, config) {
    const char *key = fy_cast(mp.key, "");
    fy_generic value = mp.value;
    printf("%s = %s\n", key, fy_cast(value, ""));
}
```

**Nearly identical ergonomics!**

### Implementation Notes

**Type Safety**:
The macro uses `__typeof__` to determine the iterator variable's type and calls the appropriate `fy_get_key_at_typed()` function, which performs automatic casting:
- `fy_generic` → Returns value as-is
- `int`, `long`, etc. → Calls `fy_cast(v, default_int)`
- `const char *` → Calls `fy_cast(v, default_string)`
- `fy_generic_map_pair` → Returns key-value pair structure

**Unique Iterator Variables**:
The macro uses `__COUNTER__` to generate unique iterator variable names, allowing nested `fy_foreach` loops without conflicts:

```c
fy_generic outer_seq = fy_sequence(
    fy_sequence(1, 2, 3),
    fy_sequence(4, 5, 6)
);

fy_generic inner_seq;
fy_foreach(inner_seq, outer_seq) {
    int val;
    fy_foreach(val, inner_seq) {
        printf("%d ", val);
    }
    printf("\n");
}
// Output:
// 1 2 3
// 4 5 6
```

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
- `fy_get(map, key, default)` - Lookup in mapping with default

**Type checking**:
- `fy_type(g)` - Get type enum
- `fy_is_*()` - Type predicates (`fy_is_map`, `fy_is_seq`, `fy_is_string`, etc.)

**Construction** (polymorphic - detects builder automatically):
- `fy_mapping(...)` - Create mapping (stack-allocated if no builder)
- `fy_sequence(...)` - Create sequence (stack-allocated if no builder)
- `fy_mapping(gb, ...)` - Create mapping via builder (heap-allocated)
- `fy_sequence(gb, ...)` - Create sequence via builder (heap-allocated)
- `fy_value(...)` / `fy_value(gb, ...)` - Create any type polymorphically

**Key principle**: All values are **immutable**. Operations like `fy_assoc()` and `fy_conj()` return new collections without modifying the original. This enables:
- **Thread safety**: Immutable values are safe to share across threads
- **Predictability**: No action-at-a-distance bugs
- **Structural sharing**: Updates are efficient through shared structure

### Construction

**Stack-allocated (temporary - no builder)**:
```c
fy_generic msg = fy_mapping(
    "role", "user",
    "content", "Hello!"
);

fy_generic items = fy_sequence("foo", "bar", "baz");
```

**Heap-allocated (persistent - with builder)**:
```c
struct fy_generic_builder *gb = fy_generic_builder_create();

fy_generic msg = fy_mapping(gb,
    "role", "user",
    "content", "Hello!"
);

fy_generic items = fy_sequence(gb, "foo", "bar", "baz");
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
const char *fy_cast(fy_generic g, "");
```

**Container handles and polymorphic operations**:
```c
// Handle types (simple const pointer typedefs)
typedef const struct fy_generic_sequence *fy_seq_handle;
typedef const struct fy_generic_mapping *fy_map_handle;

// Sentinels
extern const fy_seq_handle fy_seq_invalid;
extern const fy_map_handle fy_map_invalid;

// Extract handles using fy_get() or fy_get()
fy_seq_handle sh = fy_get(g, fy_seq_invalid);
fy_map_handle mh = fy_get(data, "config", fy_map_invalid);

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
fy_get(map, key, default)    // Returns typed value or default

// Traditional operations (for backwards compatibility)
size_t fy_len(fy_generic map);
size_t fy_len(fy_generic seq);
```

**For handles** (recommended - use polymorphic operations):

```c
// Extract handles
fy_seq_handle items = fy_get(data, "items", fy_seq_invalid);
fy_map_handle config = fy_get(data, "config", fy_map_invalid);

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
