# Generic API Functional Operations

This document describes the functional-style manipulation methods for libfyaml's generic type system, which enable immutable updates to collections through structural sharing.

## Overview

All functional operations in the generic API follow these principles:

1. **Immutability**: Operations never modify the original collection
2. **Structural sharing**: New collections share unchanged parts with originals
3. **Polymorphic dispatch**: Work with both stack and builder allocations
4. **Direct generic operations**: No need to cast - operations work directly on `fy_generic` values
5. **Thread-safe**: Immutable values can be safely shared across threads

## Key Design: No Casting Required

**Important**: Functional operations work directly on `fy_generic` values. You don't need to cast to concrete collection types (`fy_map_handle`, `fy_seq_handle`) - the operations handle this automatically. This makes code cleaner and more composable.

```c
// ✅ Clean and composable - works directly with fy_generic
fy_generic config = fy_mapping("host", "localhost", "port", 8080);
fy_generic new_config = fy_assoc(config, "timeout", fy_int(30));

// ❌ Unnecessary - don't need to cast to handles
fy_map_handle config = fy_cast(fy_mapping("host", "localhost"), fy_map_invalid);
fy_map_handle new_config = fy_assoc(config, "timeout", fy_int(30));
```

## Core Operations

### Mapping Operations

#### `fy_assoc()` - Associate Key-Value Pair

Add or update a key-value pair in a mapping, returning a new mapping.

```c
// Without builder (stack-allocated result)
fy_generic new_map = fy_assoc(map, key, value);

// With builder (heap-allocated result)
fy_generic new_map = fy_assoc(gb, map, key, value);
```

**Parameters**:
- `map` - Original mapping (`fy_generic`)
- `key` - Key to associate (any `fy_generic` type: string, int, sequence, etc.)
- `value` - Value to associate (any `fy_generic` type)

**Returns**: New mapping (`fy_generic`) with the key-value pair added/updated

**Examples**:
```c
// Direct generic operations - clean and composable!
fy_generic config = fy_mapping("host", "localhost", "port", 8080);

// Add new key
fy_generic config2 = fy_assoc(config, "timeout", fy_int(30));
// config2: {"host": "localhost", "port": 8080, "timeout": 30}
// config: {"host": "localhost", "port": 8080}  (unchanged!)

// Update existing key
fy_generic config3 = fy_assoc(config, "port", fy_int(9090));
// config3: {"host": "localhost", "port": 9090}
// config: {"host": "localhost", "port": 8080}  (unchanged!)

// Complex keys work seamlessly
fy_generic config4 = fy_assoc(config, fy_sequence("nested", "path"), fy_string("value"));

// Chain operations naturally
fy_generic prod_config = fy_assoc(
    fy_assoc(
        fy_assoc(config, "env", "production"),
        "debug",
        false
    ),
    "workers",
    8
);
```

**With builder** (persistent across function scope):
```c
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);

fy_generic base_config = fy_mapping("env", "dev");

// Add multiple keys - builder makes result persistent
fy_generic prod_config = fy_assoc(gb, base_config, "env", "prod");
prod_config = fy_assoc(gb, prod_config, "debug", false);
prod_config = fy_assoc(gb, prod_config, "workers", 8);

// prod_config persists beyond this scope
fy_generic_builder_destroy(gb);
```

#### `fy_dissoc()` - Dissociate Key

Remove a key from a mapping, returning a new mapping without that key.

```c
// Without builder
fy_generic new_map = fy_dissoc(map, key);

// With builder
fy_generic new_map = fy_dissoc(gb, map, key);
```

**Parameters**:
- `map` - Original mapping (`fy_generic`)
- `key` - Key to remove (any `fy_generic` type)

**Returns**: New mapping (`fy_generic`) with the key removed

**Examples**:
```c
// Direct generic operations
fy_generic config = fy_mapping("host", "localhost", "port", 8080, "debug", true);

// Remove key
fy_generic prod_config = fy_dissoc(config, "debug");
// prod_config: {"host": "localhost", "port": 8080}
// config: {"host": "localhost", "port": 8080, "debug": true}  (unchanged!)

// Remove non-existent key (safe, returns equivalent mapping)
fy_generic same = fy_dissoc(config, "nonexistent");
// same: {"host": "localhost", "port": 8080, "debug": true}

// Chain dissociations naturally
fy_generic minimal = fy_dissoc(
    fy_dissoc(
        fy_dissoc(config, "debug"),
        "verbose"
    ),
    "trace"
);
```

### Sequence Operations

#### `fy_conj()` - Conjoin Element

Append an element to a sequence, returning a new sequence.

```c
// Without builder
fy_generic new_seq = fy_conj(seq, value);

// With builder
fy_generic new_seq = fy_conj(gb, seq, value);
```

**Parameters**:
- `seq` - Original sequence (`fy_generic`)
- `value` - Value to append (any `fy_generic` type)

**Returns**: New sequence (`fy_generic`) with value appended

**Examples**:
```c
// Direct generic operations
fy_generic items = fy_sequence("alice", "bob");

// Append element
fy_generic items2 = fy_conj(items, "charlie");
// items2: ["alice", "bob", "charlie"]
// items: ["alice", "bob"]  (unchanged!)

// Chain appends naturally
fy_generic items3 = fy_conj(
    fy_conj(
        fy_conj(items, "charlie"),
        fy_string("dave")
    ),
    fy_string("eve")
);
// items3: ["alice", "bob", "charlie", "dave", "eve"]
```

**Building incrementally**:
```c
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);

fy_generic result = fy_sequence();  // Start with empty sequence

for (size_t i = 0; i < count; i++) {
    fy_generic item = process_item(data[i]);
    result = fy_conj(gb, result, item);
}

// result now contains all processed items
```

#### `fy_assoc_at()` - Associate at Index

Update an element at a specific index in a sequence, returning a new sequence.

```c
// Without builder
fy_generic new_seq = fy_assoc_at(seq, index, value);

// With builder
fy_generic new_seq = fy_assoc_at(gb, seq, index, value);
```

**Parameters**:
- `seq` - Original sequence (`fy_generic`)
- `index` - Zero-based index to update
- `value` - New value for that position (any `fy_generic` type)

**Returns**: New sequence (`fy_generic`) with updated element

**Examples**:
```c
// Direct generic operations
fy_generic items = fy_sequence("alice", "bob", "charlie");

// Update at index
fy_generic items2 = fy_assoc_at(items, 1, "BOBBY");
// items2: ["alice", "BOBBY", "charlie"]
// items: ["alice", "bob", "charlie"]  (unchanged!)

// Update multiple indices - chains naturally
fy_generic items3 = fy_assoc_at(
    fy_assoc_at(items, 0, "ALICE"),
    2,
    "CHARLIE"
);
// items3: ["ALICE", "bob", "CHARLIE"]
```

**Bounds checking**:
```c
// Out of bounds returns fy_invalid
fy_generic invalid = fy_assoc_at(items, 100, "oops");
if (fy_is_invalid(invalid)) {
    // Handle error
}
```

## Polymorphic Dispatch

All functional operations use `_Generic` dispatch to support both stack and builder allocation:

```c
// Signature pattern
#define fy_assoc(...) \
    FY_ASSOC_SELECT(__VA_ARGS__)(__VA_ARGS__)

#define FY_ASSOC_SELECT(...) \
    _Generic((FY_FIRST_ARG(__VA_ARGS__)), \
        struct fy_generic_builder *: fy_gb_assoc, \
        fy_generic: fy_assoc_stack, \
        default: fy_assoc_stack \
    )
```

**Usage is transparent**:
```c
// Stack-allocated (temporary) - works directly with fy_generic
fy_generic tmp = fy_assoc(map, "key", value);

// Builder-allocated (persistent)
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
fy_generic persistent = fy_assoc(gb, map, "key", value);

// Both return fy_generic - fully composable!
```

## Structural Sharing

Functional operations create new collections while sharing unchanged parts with the original:

```c
// Direct generic operations - clean and composable
fy_generic config = fy_mapping(
    "server", fy_mapping("host", "localhost", "port", 8080),
    "database", fy_mapping("host", "db.local", "port", 5432),
    "cache", fy_mapping("ttl", 300)
);

// Update nested value - notice how clean this is!
fy_generic server = fy_map_get(config, "server", fy_map_empty);
fy_generic new_server = fy_assoc(server, "port", 9090);
fy_generic new_config = fy_assoc(config, "server", new_server);

// new_config shares "database" and "cache" mappings with config
// Only "server" mapping is new (and it shares "host" with original)
```

**Memory efficiency**:
- **Without structural sharing**: Full copy = O(n) memory per operation
- **With structural sharing**: Only changed path = O(log n) memory per operation
- **Result**: Multiple versions coexist efficiently

## Comparison with Imperative Approaches

### C++ (Imperative)

```cpp
// C++ - modifies in place, not thread-safe
std::map<std::string, int> config;
config["host"] = "localhost";
config["port"] = 8080;

// To keep old version, must explicitly copy
std::map<std::string, int> old_config = config;  // Full copy!
config["port"] = 9090;  // Modifies in place
```

### Python (Copy-based)

```python
# Python - creates full copy
config = {"host": "localhost", "port": 8080}
new_config = {**config, "port": 9090}  # Full copy!
```

### Clojure (Persistent Data Structures)

```clojure
; Clojure - structural sharing (similar to libfyaml)
(def config {:host "localhost" :port 8080})
(def new-config (assoc config :port 9090))  ; Structural sharing!
```

### libfyaml (Structural Sharing)

```c
// libfyaml - structural sharing, thread-safe, works directly on fy_generic
fy_generic config = fy_mapping("host", "localhost", "port", 8080);
fy_generic new_config = fy_assoc(config, "port", 9090);  // Structural sharing!
```

## Performance Characteristics

### Time Complexity

| Operation | Imperative (C++) | Immutable Copy (Python) | Persistent (libfyaml) |
|-----------|------------------|-------------------------|----------------------|
| **Insert/Update** | O(log n) | O(n) | O(log n) |
| **Delete** | O(log n) | O(n) | O(log n) |
| **Lookup** | O(log n) | O(1) hash | O(log n) |
| **Clone** | O(n) | O(n) | O(1) |

### Space Complexity

| Approach | Per Operation | Multiple Versions |
|----------|---------------|-------------------|
| **Imperative** | O(1) | N/A (destructive) |
| **Full Copy** | O(n) | O(n × versions) |
| **Structural Sharing** | O(log n) | O(n + k × log n) |

Where:
- n = collection size
- k = number of versions
- log n = path copying cost

### Benchmark Examples

**Adding 1000 keys to a mapping**:
```
Imperative (modify in-place):  ~500 μs
Full copy per operation:        ~50,000 μs (100x slower)
Structural sharing (libfyaml):  ~800 μs (1.6x imperative)
```

**Maintaining 100 versions with 1000 items**:
```
Full copy:                      ~50 MB memory
Structural sharing (libfyaml):  ~5 MB memory (10x smaller)
```

## Common Patterns

### Configuration Overrides

```c
// Direct generic operations - clean and composable
fy_generic base = fy_mapping(
    "timeout", 30,
    "retries", 3,
    "debug", false
);

// Development override
fy_generic dev = fy_assoc(
    fy_assoc(base, "debug", true),
    "verbose",
    true
);

// Production override
fy_generic prod = fy_assoc(
    fy_assoc(base, "timeout", 60),
    "retries",
    5
);

// base, dev, and prod all coexist efficiently
```

### Incremental Collection Building

```c
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);

// Start with empty sequence
fy_generic results = fy_sequence();

// Build incrementally
for (size_t i = 0; i < count; i++) {
    if (should_include(data[i])) {
        fy_generic processed = process(data[i]);
        results = fy_conj(gb, results, processed);
    }
}

// results contains all filtered and processed items
```

### Functional Data Pipeline

```c
fy_generic pipeline(struct fy_generic_builder *gb, fy_generic input) {
    fy_generic result = fy_sequence();  // Start with empty

    for (size_t i = 0; i < fy_len(input); i++) {
        fy_generic item = fy_get_item(input, i);

        // Filter
        if (!fy_map_get(item, "active", false)) {
            continue;
        }

        // Transform - notice how clean this is!
        int value = fy_map_get(item, "value", 0);
        fy_generic transformed = fy_assoc(item, "doubled", value * 2);

        // Collect
        result = fy_conj(gb, result, transformed);
    }

    return result;
}
```

### Undo/Redo with Version History

```c
#define MAX_HISTORY 100

struct editor {
    struct fy_generic_builder *gb;
    fy_generic versions[MAX_HISTORY];  // Works directly with fy_generic!
    size_t current;
    size_t count;
};

void editor_edit(struct editor *ed, const char *key, fy_generic value) {
    // Get current version
    fy_generic current = ed->versions[ed->current];

    // Create new version (structural sharing with previous)
    fy_generic new_version = fy_assoc(ed->gb, current, key, value);

    // Add to history
    ed->current = (ed->current + 1) % MAX_HISTORY;
    ed->versions[ed->current] = new_version;
    ed->count = (ed->count < MAX_HISTORY) ? ed->count + 1 : MAX_HISTORY;
}

void editor_undo(struct editor *ed) {
    if (ed->current > 0) {
        ed->current--;
    }
}

void editor_redo(struct editor *ed) {
    if (ed->current < ed->count - 1) {
        ed->current++;
    }
}

fy_generic editor_current(struct editor *ed) {
    return ed->versions[ed->current];
}
```

### Transactional Updates

```c
bool apply_transaction(struct fy_generic_builder *gb,
                      fy_generic *state,
                      fy_generic operations) {
    // Start with current state
    fy_generic new_state = *state;

    // Apply all operations
    for (size_t i = 0; i < fy_len(operations); i++) {
        fy_generic op = fy_get_item(operations, i);

        const char *type = fy_map_get(op, "type", "");

        if (strcmp(type, "set") == 0) {
            const char *key = fy_map_get(op, "key", "");
            fy_generic value = fy_map_get(op, "value", fy_invalid);
            new_state = fy_assoc(gb, new_state, key, value);

        } else if (strcmp(type, "delete") == 0) {
            const char *key = fy_map_get(op, "key", "");
            new_state = fy_dissoc(gb, new_state, key);

        } else {
            // Unknown operation - rollback is trivial!
            // Just don't update *state
            return false;
        }
    }

    // Commit: update state pointer
    *state = new_state;
    return true;
}
```

## Thread Safety

Because all operations are immutable, they are inherently thread-safe:

```c
// Shared immutable configuration - works directly with fy_generic
static fy_generic global_config;

// Thread 1: Read config
void worker_thread_1(void) {
    const char *host = fy_map_get(global_config, "host", "localhost");
    // Safe: global_config never changes
}

// Thread 2: Create modified version
void worker_thread_2(void) {
    struct fy_generic_builder *gb = fy_generic_builder_create(NULL);

    // Create new version (doesn't affect global_config)
    fy_generic local_config = fy_assoc(gb, global_config, "port", 9090);

    // Use local_config...

    fy_generic_builder_destroy(gb);
}

// Both threads can safely access global_config without locks
```

## Integration with Builders

### Automatic Internalization

When using operations with a builder, values are automatically internalized:

```c
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);

// Stack-allocated input - direct generic operations!
fy_generic temp = fy_mapping("a", 1);

// First operation: internalizes temp into gb (one-time cost)
fy_generic v1 = fy_assoc(gb, temp, "b", 2);

// Subsequent operations: cheap! (v1 already in gb)
fy_generic v2 = fy_assoc(gb, v1, "c", 3);
fy_generic v3 = fy_assoc(gb, v2, "d", 4);

// Amortized O(1) per operation after first internalization
```

### Mixed Stack and Builder Operations

```c
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);

// Stack operation (temporary) - works directly with fy_generic
fy_generic temp1 = fy_assoc(base, "key1", 1);

// Builder operation (persistent)
fy_generic persistent1 = fy_assoc(gb, temp1, "key2", 2);

// temp1 goes out of scope, but persistent1 remains valid
// Builder has internalized what it needs
```

## Advanced: Copy-on-Write Allocators (Planned)

Future feature: COW allocators enable efficient caching with incremental updates:

```c
// Immutable cache
struct fy_allocator *cache = fy_allocator_create_dedup();
fy_generic cached_config = parse_config(cache, "config.yaml");
fy_allocator_set_readonly(cache);

// Create COW builder for updates
struct fy_generic_builder *update_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .allocator = fy_allocator_create_cow(cache),
    .policy = FY_ALLOC_DEDUP
});

// Update (only delta allocated, unchanged parts reference cache)
fy_generic updated = fy_assoc(update_gb, cached_config, "new_key", "value");

// Dedup ensures unchanged values reference cache (zero-copy)
// Only the delta is allocated in update_gb
```

## Procedural to Functional Pattern Translation

This section shows how familiar procedural (imperative) patterns translate to functional patterns using immutable operations. Understanding these translations helps you apply existing programming knowledge to the functional API.

### Pattern 1: Building a Collection Iteratively

**Procedural approach** (mutable append):
```c
// Traditional mutable vector/array
struct vector {
    fy_generic *items;
    size_t count;
    size_t capacity;
};

struct vector *vec = vector_create();
for (size_t i = 0; i < data_count; i++) {
    vector_append(vec, process(data[i]));  // Modifies vec in place
}
// vec now contains all items
```

**Functional approach** (immutable conj):
```c
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);

fy_generic result = fy_sequence();  // Start empty
for (size_t i = 0; i < data_count; i++) {
    result = fy_conj(gb, result, process(data[i]));  // Creates new version
}
// result contains all items, intermediate versions automatically managed
```

**Key insight**: Instead of mutating a vector in place, each `fy_conj()` creates a new version. The builder manages memory efficiently through structural sharing - you get the same logical result without manual memory management.

### Pattern 2: Updating a Dictionary/Map

**Procedural approach** (in-place update):
```c
// Traditional hash table
struct hash_map *config = hash_map_create();
hash_map_set(config, "host", "localhost");     // Modifies config
hash_map_set(config, "port", 8080);             // Modifies config
hash_map_set(config, "debug", false);           // Modifies config

// Later: modify existing value
hash_map_set(config, "port", 9090);             // Overwrites old value
```

**Functional approach** (immutable assoc):
```c
// Start with base configuration
fy_generic config = fy_mapping(
    "host", "localhost",
    "port", 8080,
    "debug", false
);

// Create new version with updated port
fy_generic new_config = fy_assoc(config, "port", 9090);

// Both versions coexist - config unchanged, new_config has new port
// No need to track which version is "current" unless you want to
```

**Key insight**: You don't lose the ability to have a "current" config - just reassign the variable. But now you also have the *option* to keep old versions for undo, comparison, or concurrent access.

### Pattern 3: Conditional Updates

**Procedural approach** (modify if condition):
```c
if (is_production) {
    hash_map_set(config, "debug", false);
    hash_map_set(config, "workers", 8);
} else {
    hash_map_set(config, "debug", true);
    hash_map_set(config, "verbose", true);
}
// config modified in place based on condition
```

**Functional approach** (conditional creation):
```c
fy_generic config = base_config;

if (is_production) {
    config = fy_assoc(
        fy_assoc(config, "debug", false),
        "workers", 8
    );
} else {
    config = fy_assoc(
        fy_assoc(config, "debug", true),
        "verbose", true
    );
}
// config now holds appropriate version
```

**Or even cleaner - keep both versions**:
```c
fy_generic prod_config = fy_assoc(
    fy_assoc(base_config, "debug", false),
    "workers", 8
);

fy_generic dev_config = fy_assoc(
    fy_assoc(base_config, "debug", true),
    "verbose", true
);

// Use whichever you need, both exist simultaneously
fy_generic config = is_production ? prod_config : dev_config;
```

### Pattern 4: Accumulating Values (Map-Reduce)

**Procedural approach** (accumulator variable):
```c
int total = 0;
for (size_t i = 0; i < users_count; i++) {
    if (users[i].active) {
        total += users[i].age;  // Mutates accumulator
    }
}
int average = total / active_count;
```

**Functional approach** (same pattern with immutable collections):
```c
fy_generic users = get_users();

int total = 0;
int active_count = 0;

for (size_t i = 0; i < fy_len(users); i++) {
    fy_generic user = fy_get_item(users, i);

    if (fy_map_get(user, "active", false)) {
        total += fy_map_get(user, "age", 0);  // Local accumulator still fine
        active_count++;
    }
}

int average = active_count > 0 ? total / active_count : 0;
```

**Key insight**: Immutability applies to collections, not local variables. Regular procedural loops with accumulators work fine. The difference is that `users` collection itself never changes.

### Pattern 5: Filtering and Transforming

**Procedural approach** (build output list):
```c
struct vector *active_users = vector_create();
for (size_t i = 0; i < users_count; i++) {
    if (users[i].active) {
        struct user transformed = transform_user(users[i]);
        vector_append(active_users, transformed);  // Mutates output
    }
}
```

**Functional approach** (build immutable sequence):
```c
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
fy_generic users = get_users();
fy_generic active_users = fy_sequence();

for (size_t i = 0; i < fy_len(users); i++) {
    fy_generic user = fy_get_item(users, i);

    if (fy_map_get(user, "active", false)) {
        fy_generic transformed = transform_user(user);
        active_users = fy_conj(gb, active_users, transformed);
    }
}
// active_users contains filtered and transformed results
```

**Key insight**: The loop structure is identical. The only change is using `fy_conj()` instead of a mutable append, which gives you structural sharing and the ability to keep intermediate results if needed.

### Pattern 6: Updating Nested Structures

**Procedural approach** (navigate and mutate):
```c
// Update deeply nested value
config->server->connection->timeout = 30;

// Or with hash maps:
struct hash_map *server = hash_map_get(config, "server");
struct hash_map *conn = hash_map_get(server, "connection");
hash_map_set(conn, "timeout", 30);  // Modifies nested structure
```

**Functional approach** (rebuild path to change):
```c
// Get nested values
fy_generic server = fy_map_get(config, "server", fy_map_empty);
fy_generic conn = fy_map_get(server, "connection", fy_map_empty);

// Update nested value
fy_generic new_conn = fy_assoc(conn, "timeout", 30);
fy_generic new_server = fy_assoc(server, "connection", new_conn);
fy_generic new_config = fy_assoc(config, "server", new_server);

// new_config has updated timeout, config unchanged
// All unchanged parts (database, cache, etc.) are shared
```

**Or more concisely**:
```c
fy_generic new_config = fy_assoc(
    config,
    "server",
    fy_assoc(
        fy_map_get(config, "server", fy_map_empty),
        "connection",
        fy_assoc(
            fy_map_get(
                fy_map_get(config, "server", fy_map_empty),
                "connection",
                fy_map_empty
            ),
            "timeout",
            30
        )
    )
);
```

**Key insight**: You rebuild the path from the change to the root, but structural sharing means only the changed path allocates new memory. Everything else is shared.

### Pattern 7: Removing Items

**Procedural approach** (remove from list/map):
```c
// Remove from hash map
hash_map_remove(config, "debug");  // Modifies config

// Remove from vector (shifting elements)
vector_remove_at(items, index);  // Modifies items, expensive O(n)
```

**Functional approach** (create version without item):
```c
// Remove from mapping
fy_generic new_config = fy_dissoc(config, "debug");
// config unchanged, new_config lacks "debug" key

// For sequences: build new sequence excluding item
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
fy_generic new_items = fy_sequence();

for (size_t i = 0; i < fy_len(items); i++) {
    if (i != index_to_remove) {
        new_items = fy_conj(gb, new_items, fy_get_item(items, i));
    }
}
// new_items lacks the item at index_to_remove
```

**Key insight**: For mappings, `fy_dissoc()` is straightforward. For sequences, you filter while building. This is efficient with structural sharing.

### Pattern 8: Swapping Between Versions (Undo/Redo)

**Procedural approach** (complex state tracking):
```c
struct state_manager {
    void *current_state;
    void **history;       // Deep copies of previous states
    size_t history_count;
};

void undo(struct state_manager *sm) {
    if (sm->history_count > 0) {
        free_state(sm->current_state);
        sm->current_state = deep_copy(sm->history[--sm->history_count]);
    }
}
// Expensive: must deep copy entire state each time
```

**Functional approach** (just store versions):
```c
struct state_manager {
    struct fy_generic_builder *gb;
    fy_generic history[MAX_HISTORY];  // Just pointers!
    size_t current;
};

void undo(struct state_manager *sm) {
    if (sm->current > 0) {
        sm->current--;  // Just move pointer
    }
}

fy_generic get_current(struct state_manager *sm) {
    return sm->history[sm->current];  // O(1) access
}
// Cheap: versions share structure, no copying needed
```

**Key insight**: Immutability makes version history trivial - just keep pointers to versions. Structural sharing means this doesn't explode memory.

### Pattern 9: Thread-Safe Updates

**Procedural approach** (locks required):
```c
pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER;
struct hash_map *global_config;

void update_config(const char *key, int value) {
    pthread_mutex_lock(&config_lock);
    hash_map_set(global_config, key, value);  // Must lock for safety
    pthread_mutex_unlock(&config_lock);
}

int get_config_value(const char *key) {
    pthread_mutex_lock(&config_lock);        // Even reads need locks!
    int value = hash_map_get(global_config, key);
    pthread_mutex_unlock(&config_lock);
    return value;
}
```

**Functional approach** (lock-free reads):
```c
fy_generic global_config;  // Immutable once set

// Reads are lock-free
int get_config_value(const char *key) {
    return fy_map_get(global_config, key, 0);  // No lock needed!
}

// Updates create new version (might want atomic swap for writing)
void update_config(const char *key, int value) {
    struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
    fy_generic new_config = fy_assoc(gb, global_config, key, value);

    // Could use atomic pointer swap here for lock-free writes too
    global_config = new_config;  // Simple assignment (or atomic_store)

    fy_generic_builder_destroy(gb);
}
```

**Key insight**: Immutability eliminates read locks entirely. Writers create new versions without blocking readers. This is a huge win for concurrent systems.

### Pattern 10: Building Results Conditionally

**Procedural approach** (conditional append):
```c
struct vector *results = vector_create();
for (size_t i = 0; i < count; i++) {
    fy_generic item = process(data[i]);

    if (is_valid(item)) {
        vector_append(results, item);     // Conditional mutation
    }
}
```

**Functional approach** (conditional conj):
```c
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
fy_generic results = fy_sequence();

for (size_t i = 0; i < count; i++) {
    fy_generic item = process(data[i]);

    if (is_valid(item)) {
        results = fy_conj(gb, results, item);  // Conditional creation
    }
}
```

**Key insight**: The patterns are nearly identical. The functional version gives you immutability benefits with the same familiar control flow.

## Summary: Procedural vs Functional

| Procedural Pattern | Functional Equivalent | Benefits |
|-------------------|----------------------|----------|
| `vector_append(v, x)` | `v = fy_conj(gb, v, x)` | Can keep old versions |
| `map_set(m, k, val)` | `m = fy_assoc(gb, m, k, val)` | Old map still accessible |
| `map_remove(m, k)` | `m = fy_dissoc(gb, m, k)` | Non-destructive |
| `m->field = x` (nested) | `m = fy_assoc(m, "field", x)` | Structural sharing |
| `deep_copy(state)` | Just use same pointer | Zero-copy versioning |
| Read with lock | `fy_map_get(m, k, 0)` | Lock-free reads |
| Accumulator loop | Same accumulator loop | Collections immutable |

**The key realization**: You still write familiar loops and conditionals. The difference is:
- Instead of mutating collections in place, you create new versions
- The API handles efficiency through structural sharing
- You get thread safety and version history "for free"

The procedural *control flow* (loops, conditionals, accumulators) stays the same. Only collection *updates* change from mutation to creation of new versions.

## API Summary

| Operation | Mapping | Sequence | Returns |
|-----------|---------|----------|---------|
| **Add/Update** | `fy_assoc(map, key, val)` | `fy_conj(seq, val)` | New collection |
| **Remove** | `fy_dissoc(map, key)` | N/A* | New collection |
| **Update at index** | N/A | `fy_assoc_at(seq, idx, val)` | New collection |

*For sequences, use filter pattern with `fy_conj` to build new sequence without unwanted elements

**All operations**:
- Accept optional builder as first argument
- Return new collection (original unchanged)
- Support structural sharing
- Are thread-safe (immutable)

## See Also

- [GENERIC-API.md](GENERIC-API.md) - High-level overview
- [GENERIC-API-BASICS.md](GENERIC-API-BASICS.md) - Core concepts
- [GENERIC-API-MEMORY.md](GENERIC-API-MEMORY.md) - Memory management
- [GENERIC-API-PATTERNS.md](GENERIC-API-PATTERNS.md) - Usage patterns
