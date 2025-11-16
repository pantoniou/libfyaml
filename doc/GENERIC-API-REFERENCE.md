# Generic API Reference

This document provides the complete API reference for libfyaml's generic type system.

## Iteration API

### Sequence Iteration

**Index-based iteration** (works today):
```c
fy_seq_handle users = fy_map_get(data, "users", fy_seq_invalid);

for (size_t i = 0; i < fy_len(users); i++) {
    fy_generic user = fy_get_item(users, i);
    const char *name = fy_map_get(user, "name", "anonymous");
    printf("%s\n", name);
}
```

**Foreach macro** (proposed):
```c
#define fy_seq_foreach(item, seq) \
    for (size_t _fy_i = 0, _fy_n = fy_len(seq); \
         _fy_i < _fy_n && ((item) = fy_get_item(seq, _fy_i), 1); \
         _fy_i++)

// Usage:
fy_generic user;
fy_seq_foreach(user, users) {
    const char *name = fy_map_get(user, "name", "anonymous");
    printf("%s\n", name);
}
```

### Mapping Iteration

**Iterator-based approach**:
```c
// Iterator structure
typedef struct {
    fy_map_handle map;
    void *internal_state;  // Opaque iterator position
    const char *current_key;
    fy_generic current_value;
} fy_map_iter;

// Create iterator
fy_map_iter fy_map_iter_create(fy_map_handle map);

// Advance to next entry (returns false when done)
bool fy_map_iter_next(fy_map_iter *iter);

// Get current key/value (after successful next())
const char *fy_map_iter_key(const fy_map_iter *iter);
fy_generic fy_map_iter_value(const fy_map_iter *iter);
```

**Usage**:
```c
fy_map_handle config = fy_map_get(data, "config", fy_map_invalid);

fy_map_iter iter = fy_map_iter_create(config);
while (fy_map_iter_next(&iter)) {
    const char *key = fy_map_iter_key(&iter);
    fy_generic value = fy_map_iter_value(&iter);

    printf("%s = ", key);

    switch (fy_type(value)) {
        case FYGT_STRING:
            printf("%s\n", fy_string_get(value));
            break;
        case FYGT_INT:
            printf("%lld\n", fy_int_get(value));
            break;
        // ... handle other types
    }
}
```

**Foreach macro for mappings**:
```c
#define fy_map_foreach_kv(key_var, value_var, map) \
    for (fy_map_iter _fy_it = fy_map_iter_create(map); \
         fy_map_iter_next(&_fy_it) && \
         ((key_var) = fy_map_iter_key(&_fy_it), \
          (value_var) = fy_map_iter_value(&_fy_it), 1); )

// Usage:
const char *key;
fy_generic value;
fy_map_foreach_kv(key, value, config) {
    printf("%s = %s\n", key, fy_string_get(value));
}
```

**Callback-based iteration** (alternative):
```c
typedef void (*fy_map_foreach_fn)(const char *key, fy_generic value, void *ctx);

void fy_map_foreach(fy_map_handle map, fy_map_foreach_fn fn, void *ctx);

// Usage:
void print_entry(const char *key, fy_generic value, void *ctx) {
    printf("%s = %s\n", key, fy_string_get(value));
}

fy_map_foreach(config, print_entry, NULL);
```

### Comparison with Other Languages

**Python**:
```python
for user in users:
    print(user.get("name", "anonymous"))

for key, value in config.items():
    print(f"{key} = {value}")
```

**libfyaml (with macros)**:
```c
fy_generic user;
fy_seq_foreach(user, users) {
    printf("%s\n", fy_map_get(user, "name", "anonymous"));
}

const char *key;
fy_generic value;
fy_map_foreach_kv(key, value, config) {
    printf("%s = %s\n", key, fy_string_get(value));
}
```

**Nearly identical ergonomics!**

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
- `fy_local_mapping(...)` - Create mapping (stack-allocated, immutable)
- `fy_local_sequence(...)` - Create sequence (stack-allocated, immutable)
- `fy_gb_mapping(gb, ...)` - Create mapping via builder (heap-allocated)
- `fy_gb_sequence(gb, ...)` - Create sequence via builder (heap-allocated)

**Key principle**: All values are **immutable**. Operations like `fy_assoc()` and `fy_conj()` return new collections without modifying the original. This enables:
- **Thread safety**: Immutable values are safe to share across threads
- **Predictability**: No action-at-a-distance bugs
- **Structural sharing**: Updates are efficient through shared structure

### Construction

**Stack-allocated (temporary)**:
```c
fy_generic msg = fy_local_mapping(
    "role", "user",
    "content", "Hello!"
);

fy_generic items = fy_local_sequence("foo", "bar", "baz");
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
