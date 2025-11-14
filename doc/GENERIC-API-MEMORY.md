# Generic API Memory Management

This document covers memory management strategies and allocator configuration for libfyaml's generic type system.

## Memory Management and Allocators

libfyaml's generic API achieves **Python-level ergonomics without garbage collection** through a sophisticated allocator system that provides flexibility without complexity.

### Two Lifetime Models

**Stack-scoped values** (temporary, automatic cleanup):
```c
void process_config(void) {
    fy_generic config = fy_mapping("host", "localhost", "port", 8080);

    const char *host = fy_map_get(config, "host", "");
    int port = fy_map_get(config, "port", 0);

    // ... use config ...

}  // config automatically cleaned up when function returns
```

**Builder-scoped values** (persistent, explicit cleanup):
```c
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);

fy_generic config = fy_gb_mapping(gb,
    "host", "localhost",
    "port", 8080
);

// config persists beyond function scope
// ... pass config around, store it ...

fy_generic_builder_destroy(gb);  // Cleanup all builder-allocated values
```

### No Garbage Collection Required

Unlike Clojure or Python, libfyaml uses **scope-based lifetimes**:
- ✅ No GC pauses (deterministic performance)
- ✅ No reference counting (no atomic operations)
- ✅ Predictable cleanup (explicit destroy)
- ✅ Suitable for real-time systems
- ✅ Suitable for embedded systems

### Builder Configuration

Create builders with flexible allocator configuration:

```c
struct fy_generic_builder_cfg {
    struct fy_allocator *allocator;  // NULL = use auto allocator
    enum fy_alloc_policy policy;     // Policy hints for auto allocator
    // ... other configuration options
};

struct fy_generic_builder *fy_generic_builder_create(
    const struct fy_generic_builder_cfg *cfg  // NULL = sensible defaults
);
```

### Allocator Policy Hints

**Simple policy hints** for the auto allocator - no need to implement custom allocators:

```c
enum fy_alloc_policy {
    FY_ALLOC_DEFAULT     = 0,       // Balanced (dedup enabled)
    FY_ALLOC_FAST        = 1 << 0,  // Speed over memory
    FY_ALLOC_COMPACT     = 1 << 1,  // Memory over speed
    FY_ALLOC_DEDUP       = 1 << 2,  // Deduplicate values
    FY_ALLOC_ARENA       = 1 << 3,  // Arena/bump allocation
    FY_ALLOC_POOL        = 1 << 4,  // Object pooling
    FY_ALLOC_THREAD_SAFE = 1 << 5,  // Thread-safe allocator
};

// Policies compose via bitwise OR
struct fy_generic_builder *gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .policy = FY_ALLOC_DEDUP | FY_ALLOC_COMPACT
});
```

### Deduplicating Builder

**Deduplication eliminates redundant values automatically**:

```c
struct fy_generic_builder *gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .policy = FY_ALLOC_DEDUP
});

// All three servers share the SAME "localhost" string in memory
fy_generic server1 = fy_gb_mapping(gb, "host", "localhost", "port", 8080);
fy_generic server2 = fy_gb_mapping(gb, "host", "localhost", "port", 9090);
fy_generic server3 = fy_gb_mapping(gb, "host", "localhost", "port", 3000);

// Only ONE copy of "localhost" stored
// Integers 8080, 9090, 3000 may also be deduplicated
```

**Benefits**:
- 🚀 Reduced memory footprint (string interning, value sharing)
- 🚀 Better cache locality (shared values cluster in memory)
- 🚀 Faster equality checks (pointer comparison for deduplicated values)
- 🚀 Automatic optimization (no manual interning needed)

**When to use**:
- ✅ Configuration management (many duplicate strings)
- ✅ Long-lived data structures
- ✅ Large datasets with repetitive values
- ❌ When all values are unique (small overhead for dedup table)

### Internalization and Performance

**Internalization short-circuit** makes functional operations efficient:

```c
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);

// Start with stack-allocated sequence
fy_seq_handle seq1 = fy_get(fy_sequence("alice", "bob"), fy_seq_invalid);

// First use: internalizes seq1 into builder (one-time cost)
fy_seq_handle seq2 = fy_conj(gb, seq1, fy_string("charlie"));

// Subsequent uses: seq2 already internalized, no copy! (cheap)
fy_seq_handle seq3 = fy_conj(gb, seq2, fy_string("dave"));
fy_seq_handle seq4 = fy_conj(gb, seq3, fy_string("eve"));

// Internalization happens ONCE per value, amortized O(1)
```

**Key insight**: Operations check if a value is already builder-owned before copying.

**Performance characteristics**:
- First operation on stack value: O(n) internalization
- Subsequent operations: O(1) (structural sharing only)
- Result: Amortized O(1) per functional operation

### Stackable Allocators

**Hierarchical builders** enable parent-child relationships:

```c
// Parent: long-lived config with deduplication
struct fy_generic_builder *parent_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .policy = FY_ALLOC_DEDUP
});
fy_generic app_config = load_config(parent_gb);

// Child: request-scoped arena that shares parent's dedup table
struct fy_generic_builder *child_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .allocator = fy_allocator_create_stacked(parent_gb->allocator),
    .policy = FY_ALLOC_ARENA
});

// Process request with fast arena allocation...
// Child can see parent's deduplicated strings
// Child allocations are fast (arena bump allocation)

fy_generic_builder_destroy(child_gb);  // Fast cleanup, parent unaffected
// app_config still valid, parent_gb still alive
```

**Benefits**:
- 🎯 Hierarchical lifetimes (request < session < application)
- 🎯 Share dedup tables across scopes
- 🎯 Fast cleanup of child scopes
- 🎯 Flexible allocation strategies per scope

### Custom Allocators (Power Users)

**Full control** for specialized needs:

```c
// Embedded system: fixed-size pool (no fallback)
struct fy_allocator *embedded_alloc = fy_allocator_create_pool(&(struct fy_allocator_pool_cfg){
    .item_size = 64,
    .item_count = 1000,
    .fallback = NULL  // Fail if exhausted
});

// Real-time system: pre-allocated locked memory
void *rt_memory = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_LOCKED, -1, 0);
struct fy_allocator *rt_alloc = fy_allocator_create_arena(&(struct fy_allocator_arena_cfg){
    .memory = rt_memory,
    .size = 1 << 20  // 1MB locked in RAM
});

// Game engine: per-frame arena with reset
struct fy_allocator *frame_alloc = fy_allocator_create_arena_reset();

void render_frame(void) {
    struct fy_generic_builder *frame_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
        .allocator = frame_alloc
    });

    // Build frame data...

    fy_generic_builder_destroy(frame_gb);
    fy_allocator_reset(frame_alloc);  // Instant "free" of entire frame
}
```

### Read-Only Allocators with Copy-on-Write (Planned)

**Immutable cache with transactional updates** - not yet implemented but planned:

```c
// Cache of parsed files (immutable, read-only)
struct fy_allocator *cache_alloc = fy_allocator_create_dedup();
fy_generic cached_config = parse_yaml_file(cache_alloc, "config.yaml");

// Make cache read-only (no further modifications allowed)
fy_allocator_set_readonly(cache_alloc);

// Create new allocator for incremental updates, with cache as read-only base
struct fy_generic_builder *update_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .allocator = fy_allocator_create_cow(cache_alloc),  // Copy-on-write
    .policy = FY_ALLOC_DEDUP  // Dedup across cache and new data
});

// Update config - only modified parts allocated in update_gb
fy_map_handle updated_config = fy_assoc(update_gb,
    cached_config,
    "server.port", fy_int(9090)
);

// New values reference cached values via dedup (zero-copy for unchanged parts)
// Only the delta (new port value and updated map node) is allocated

// Transaction commit: switch to new version
struct fy_allocator *new_cache = fy_allocator_from_builder(update_gb);
fy_allocator_set_readonly(new_cache);

// Old cache can be freed when no longer referenced
fy_allocator_destroy(cache_alloc);

// Transaction rollback alternative: just destroy update_gb, cache unchanged
// fy_generic_builder_destroy(update_gb);
```

**How it works**:

1. **Immutable cache**: Parse files into deduplicated allocator, then make read-only
2. **COW allocator**: New allocator uses cache as read-only base for lookups
3. **Dedup lookup order**: New allocator → cache (read-only) → allocate new
4. **Zero-copy**: Unchanged values reference cache (no copying)
5. **Transactional**: Commit = freeze new allocator, rollback = destroy it

**Benefits**:
- 🎯 **Immutable cache**: Parsed files cached, never modified
- 🎯 **Zero-copy reads**: Cache hits reference existing allocator
- 🎯 **Efficient updates**: Only deltas allocated (copy-on-write)
- 🎯 **Transactional commits**: Atomic switch to new version
- 🎯 **Easy rollback**: Discard update allocator, cache unchanged
- 🎯 **Dedup across versions**: New data can reference cached data
- 🎯 **Multi-version concurrency**: Multiple readers on old cache, one writer on new
- 🎯 **Content-addressable**: Same content = same pointer (cache hit)

**Example: Configuration file cache**:

```c
// Global cache of parsed config files
struct {
    struct fy_allocator *cache;
    fy_generic configs[MAX_FILES];
} global_cache;

void init_cache(void) {
    global_cache.cache = fy_allocator_create_dedup();
}

// Load config file (cache if not present)
fy_generic load_config(const char *filename) {
    // Check if already cached
    for (size_t i = 0; i < MAX_FILES; i++) {
        if (configs[i].filename && strcmp(configs[i].filename, filename) == 0) {
            return configs[i].data;  // Cache hit - zero cost!
        }
    }

    // Parse and cache
    fy_generic config = parse_yaml_file(global_cache.cache, filename);
    cache_entry_add(filename, config);

    return config;
}

// Hot reload: create new version with updates
fy_generic reload_config(const char *filename, fy_generic old_config) {
    // Create COW allocator for update
    struct fy_generic_builder *update_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
        .allocator = fy_allocator_create_cow(global_cache.cache),
        .policy = FY_ALLOC_DEDUP
    });

    // Parse new file
    fy_generic new_config = parse_yaml_file(update_gb, filename);

    // Dedup will reuse unchanged values from cache!
    // Only changed parts allocated in update_gb

    // Commit: freeze new allocator
    struct fy_allocator *new_cache = fy_allocator_from_builder(update_gb);
    fy_allocator_set_readonly(new_cache);

    // Update global cache (TODO: handle old cache cleanup)
    return new_config;
}
```

**Example: LSP server incremental updates**:

```c
// Language server with parsed file cache
struct lsp_server {
    struct fy_allocator *cache;  // All parsed files
    fy_generic documents[1000];
};

// Edit document incrementally
fy_generic apply_edit(struct lsp_server *lsp, const char *uri, struct edit *e) {
    fy_generic old_doc = find_document(lsp, uri);

    // Create COW allocator
    struct fy_generic_builder *edit_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
        .allocator = fy_allocator_create_cow(lsp->cache),
        .policy = FY_ALLOC_DEDUP
    });

    // Apply edit - most of AST unchanged, only affected nodes allocated
    fy_generic new_doc = apply_incremental_edit(edit_gb, old_doc, e);

    // Dedup ensures unchanged subtrees reference cache
    // Memory usage: O(size of change), not O(size of document)!

    // Commit
    struct fy_allocator *new_cache = fy_allocator_from_builder(edit_gb);
    fy_allocator_set_readonly(new_cache);

    // Swap cache (old cache cleaned up when no references remain)
    struct fy_allocator *old_cache = lsp->cache;
    lsp->cache = new_cache;

    return new_doc;
}
```

**Use cases**:
- ✅ **Configuration file caching**: Parse once, cache immutably, incremental updates
- ✅ **Build systems**: Cache parsed build files, update only changed files
- ✅ **LSP servers**: Cache parsed source files, incremental updates on edit
- ✅ **Web frameworks**: Cache parsed templates, hot reload with COW
- ✅ **Database query cache**: Cache parsed queries, invalidate selectively
- ✅ **MVCC storage**: Multi-version storage with transaction isolation
- ✅ **Git-like storage**: Content-addressable immutable objects

**Performance characteristics**:
- **Cache hit**: O(1) pointer dereference (zero-copy)
- **Incremental update**: O(size of change), not O(size of data)
- **Dedup lookup**: O(1) hash table lookup in cache
- **Memory overhead**: Only deltas allocated, unchanged data shared
- **Commit**: O(1) pointer swap
- **Rollback**: O(1) destroy update allocator

**Comparison to other approaches**:

| Approach | Memory | Update Cost | Rollback | Immutable |
|----------|--------|-------------|----------|-----------|
| **Full copy** | O(n) | O(n) copy | O(1) | ❌ |
| **In-place mutation** | O(1) | O(1) | ❌ Hard | ❌ |
| **COW filesystem** | O(Δ) | O(Δ) | O(1) | ✅ |
| **Git objects** | O(Δ) | O(Δ) | O(1) | ✅ |
| **libfyaml COW** | O(Δ) | O(Δ) | O(1) | ✅ |

Where Δ = size of changes

This pattern enables **database-like MVCC semantics in C** with zero-copy reads, incremental writes, and transactional commits.

### Memory Management Summary

| Aspect | libfyaml | Python | Rust | C++ |
|--------|----------|--------|------|-----|
| **Lifetime model** | Scope-based | GC | Ownership | RAII |
| **Determinism** | ✅ Yes | ❌ No (GC pauses) | ✅ Yes | ✅ Yes |
| **Deduplication** | ✅ Built-in | ✅ Strings only | ❌ Manual | ❌ Manual |
| **Real-time safe** | ✅ Yes | ❌ No | ✅ Yes | ⚠️ Careful |
| **Thread safety** | ✅ Immutable | ⚠️ GIL | ✅ Ownership | ❌ Manual |
| **Allocation control** | ✅ Full | ❌ Limited | ⚠️ Global | ⚠️ Per-type |
| **Simplicity** | ✅ Simple | ✅ Simple | ⚠️ Complex | ⚠️ Complex |

**Key advantages**:
- Python-level simplicity (scope-based)
- Rust-level determinism (no GC)
- C++-level control (custom allocators)
- Better than all: dedup by default, stackable allocators

