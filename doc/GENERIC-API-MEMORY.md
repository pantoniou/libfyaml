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

### Polymorphic Builder Interface

**The breakthrough insight**: Immutability enables **safe thread-local builders** and **zero-cost context threading**.

#### Why Immutability Makes Thread-Local Builders Safe

Unlike mutable APIs where a thread-local context could cause "spooky action at a distance", **immutable operations never modify existing values**:

```c
// Thread-local builder is SAFE because nothing is mutated
fy_generic_builder_push(gb);

// These return NEW values - originals unchanged
fy_generic config = fy_mapping("host", "localhost");
fy_generic config2 = fy_assoc(config, "port", 8080);     // config still valid!
fy_generic config3 = fy_assoc(config, "timeout", 30);    // fork from config

// No mutations = no temporal coupling = thread-local "just works"
fy_generic_builder_pop(gb);
```

**Compare to a mutable API** (where thread-local would be dangerous):
```c
// HYPOTHETICAL mutable API - thread-local would be risky
dict_set(config, "port", 8080);  // Mutates in place!
// Anyone holding a reference to 'config' sees the change
// Thread-local builder could cause unexpected side effects
```

**Key insight**: Immutability + persistent data structures = referential transparency = safe implicit context.

#### Polymorphic fy_to_generic() and Builder Detection

The `fy_to_generic()` macro **transparently detects builders** using C11 `_Generic`:

```c
// Polymorphic: auto-detects builder as first argument
fy_generic result = fy_assoc(gb, map, "key", "value");    // Uses gb
fy_generic result = fy_assoc(map, "key", "value");        // Stack allocation
```

**Multi-tier precedence** for maximum flexibility:

1. **Explicit builder** (first arg): If first argument is `struct fy_generic_builder *`, use it
2. **Thread-local builder**: If builder pushed with `fy_generic_builder_push()`, use it
3. **Stack allocation**: Otherwise, allocate on stack (automatic cleanup)

#### Thread-Local Builder Stack

**The implementation is trivial** - just a parent pointer and thread-local variable:

```c
struct fy_generic_builder {
    struct fy_allocator *allocator;
    struct fy_generic_builder *parent;  // Parent for dedup lookup chain
    struct fy_dedup_table *dedup_table;
    // ... other fields
};

// Thread-local: single pointer to current builder
_Thread_local struct fy_generic_builder *fy_current_builder = NULL;

// Push: link to parent
void fy_generic_builder_push(struct fy_generic_builder *gb) {
    gb->parent = fy_current_builder;  // Link to current (may be NULL)
    fy_current_builder = gb;          // Make this the current
}

// Pop: restore parent
void fy_generic_builder_pop(struct fy_generic_builder *gb) {
    assert(fy_current_builder == gb);
    fy_current_builder = gb->parent;  // Restore parent (may be NULL)
}
```

**Usage pattern** - clean, composable code:

```c
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
fy_generic_builder_push(gb);

// All operations transparently use thread-local builder
fy_generic config = fy_mapping("host", "localhost");
config = fy_assoc(config, "port", 8080);
config = fy_assoc(config, "timeout", 30);

// Complex nested expressions - builder threads through automatically
fy_generic services = fy_sequence(
    fy_mapping("name", "api", "port", 8080),
    fy_mapping("name", "web", "port", 3000)
);
config = fy_assoc(config, "services", services);

fy_generic_builder_pop(gb);
// config persists, allocated in gb
```

**Dedup lookup** naturally walks the parent chain:

```c
void *fy_dedup_lookup(struct fy_generic_builder *gb, fy_type type, void *value) {
    // Walk up parent chain - like lexical scope lookup
    for (struct fy_generic_builder *b = gb; b != NULL; b = b->parent) {
        void *existing = hashtable_lookup(b->dedup_table, type, value);
        if (existing) {
            return existing;  // Found in this builder or ancestor
        }
    }
    return NULL;  // Not found in chain
}
```

**The pattern that emerges**:
- **Top builder** (current thread-local): receives all new allocations
- **Parent builders**: read-only, used only for dedup lookups
- **Dedup chain**: walks up like lexical scoping

This is identical to:
- Lexical scope chains in programming languages
- Prototype chains (JavaScript `__proto__`)
- Overlay/union filesystems (lower layers read-only)
- Shadow page tables

#### Hierarchical Builder Pattern

**Long-lived parent for common data, ephemeral children for request-specific data**:

```c
// Application startup: create long-lived parent with dedup
struct fy_generic_builder *app_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .policy = FY_ALLOC_DEDUP
});
fy_generic_builder_push(app_gb);

// Load config - stored in parent, deduplicated
fy_generic app_config = parse_yaml("config.yaml");
fy_generic common_strings = fy_sequence("localhost", "production", "enabled");

// Per-request processing
void handle_request(struct request *req) {
    // Create ephemeral child builder
    struct fy_generic_builder *req_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
        .policy = FY_ALLOC_ARENA  // Fast arena allocation
    });
    fy_generic_builder_push(req_gb);

    // Build request-specific data
    // Common strings like "localhost" deduplicated from parent!
    fy_generic response = fy_mapping(
        "host", "localhost",     // Dedup hit in parent - zero allocation!
        "timestamp", time(NULL), // New data in child
        "request_id", req->id    // New data in child
    );

    // Process...
    send_response(response);

    // Cleanup child - instant free of request data
    fy_generic_builder_pop(req_gb);
    fy_generic_builder_destroy(req_gb);

    // app_config still valid, parent still alive
}

// Shutdown
fy_generic_builder_pop(app_gb);
fy_generic_builder_destroy(app_gb);
```

**What happens**:
1. **Parent builder**: holds deduplicated config, long-lived strings
2. **Child builder**: allocates request-specific data
3. **Dedup lookup**: child checks parent chain, finds "localhost" → zero allocation
4. **Fast cleanup**: destroying child instantly frees all request data
5. **Parent survives**: app_config and common data unaffected

**Memory efficiency**:
- Common strings allocated once in parent
- Each request only allocates unique data (timestamps, IDs)
- No string duplication across requests
- Fast per-request cleanup (arena destroy)

This pattern is **perfect for**:
- ✅ Web servers (app config + per-request data)
- ✅ Compilers (global symbols + per-function temporaries)
- ✅ Game engines (level data + per-frame temporaries)
- ✅ Databases (schema + per-transaction data)

**This is essentially a zero-cost monad in C**:
- Thread-local builder = implicit "allocation context" (like Haskell's Reader monad)
- Immutability = referential transparency
- `_Generic` dispatch = type-safe polymorphism
- Zero runtime overhead = compile-time dispatch

#### Perfect for Functional Composition

Immutability means **builder lifetime doesn't couple to value lifetime**:

```c
fy_generic_builder_push(gb);

// Complex expression tree - all intermediate values immutable
fy_generic result =
    fy_merge(
        fy_assoc(base_config, "env", "production"),
        fy_mapping(
            "database", fy_mapping(
                "host", "db.example.com",
                "port", 5432,
                "pool", fy_mapping(
                    "min", 5,
                    "max", 20
                )
            ),
            "cache", fy_mapping(
                "enabled", true,
                "ttl", 3600
            )
        )
    );

fy_generic_builder_pop(gb);
```

**Every intermediate value is immutable**, so:
- ✅ Thread-local builder safe to use anywhere in expression tree
- ✅ No temporal coupling between operations
- ✅ Perfect for functional composition patterns
- ✅ Essentially Rust-like ergonomics without borrow checker complexity

#### Ergonomics Comparison

**Explicit builder** (maximum control):
```c
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
fy_generic config = fy_mapping(gb, "host", "localhost");
config = fy_assoc(gb, config, "port", 8080);
config = fy_assoc(gb, config, "timeout", 30);
fy_generic_builder_destroy(gb);
```

**Thread-local builder** (clean composition):
```c
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
fy_generic_builder_push(gb);

fy_generic config = fy_mapping("host", "localhost");
config = fy_assoc(config, "port", 8080);
config = fy_assoc(config, "timeout", 30);

fy_generic_builder_pop(gb);
fy_generic_builder_destroy(gb);
```

**Stack allocation** (temporary values):
```c
void process_request(void) {
    fy_generic config = fy_mapping("host", "localhost");
    config = fy_assoc(config, "port", 8080);

    // Use config...

}  // Automatic cleanup
```

#### Why This Design is Novel

The combination of:
1. **C11 `_Generic`** for polymorphic dispatch
2. **Immutability** for safe implicit context
3. **Thread-local builders** for ergonomic composition
4. **Structural sharing** for performance
5. **Zero-cost abstractions** (compile-time dispatch)

...creates a **genuinely novel pattern for C**: high-level language ergonomics with systems language performance and control.

This hasn't been executed this cleanly in C before. It proves that with careful API design, **C can match Python/Rust ergonomics** without sacrificing:
- ❌ Type safety (compile-time checked)
- ❌ Performance (zero-cost abstractions)
- ❌ Determinism (no GC, explicit lifetimes)
- ❌ Control (full allocator control when needed)

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

### Content-Addressable Canonical Form (Future Direction)

**The vision**: Combine deduplication with **canonical ordering** to create a **deterministic, content-addressable representation** of any generic value.

#### The Core Insight

During normal operation, values are allocated in insertion order for performance. At **serialization/cache-write time**, canonicalize the entire arena:

1. **Sort values by comparison order** within each type class
2. **Relocate to zero-based offsets** in canonical order
3. **Generate deterministic byte stream** suitable for content-addressable hashing
4. **Same logical content = identical bytes** across all documents

#### Why Deferred Canonicalization Works

**Live allocation** (fast, insertion order):
```c
// Normal dedup builder - allocations happen in insertion order
struct fy_generic_builder *gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .policy = FY_ALLOC_DEDUP
});

// Values allocated as encountered
fy_generic config = fy_mapping(
    "port", 8080,      // Allocated first
    "host", "localhost", // Allocated second (even though "host" < "port" lexically)
    "timeout", 30      // Allocated third
);

// Fast: O(1) dedup lookup, O(1) arena append
```

**Serialization time** (canonicalize once):
```c
// Write to cache - canonicalize the arena
uint8_t hash[32];
size_t size;
void *canonical = fy_serialize_canonical(gb, &size, &hash);

// Inside fy_serialize_canonical():
// 1. Topologically sort all values by comparison order
// 2. Strings: lexicographic ("host" < "port" < "timeout")
// 3. Integers: numeric (30 < 8080)
// 4. Doubles: IEEE 754 total order
// 5. Collections: recursively canonical (keys sorted)
// 6. Relocate to zero-based offsets
// 7. Write deterministic byte stream
// 8. Compute b3sum hash
```

**Performance characteristics**:
- **Live allocation**: O(1) amortized (no sorting overhead)
- **Serialization**: O(n log n) sort + O(n) write (done once)
- **Deserialization**: O(n) rebuild dedup table (values already canonical)
- **Comparison within builder**: O(1) after canonicalization (pointer arithmetic!)

#### Canonical Ordering by Type

**Small immediates** (value in tag):
```c
// Integers: already canonical
fy_generic a = fy_int(42);
fy_generic b = fy_int(100);
// Tag comparison: a.tag < b.tag ⟺ 42 < 100
```

**Heap values** (pointer in tag, canonical after serialization):
```c
// Doubles: sorted by IEEE 754 total order
fy_generic e = fy_double(2.71828);
fy_generic pi = fy_double(3.14159);
fy_generic tau = fy_double(6.28318);

// After canonicalization:
// e at offset 0x0000, pi at 0x0008, tau at 0x0010
// Pointer comparison: e.ptr < pi.ptr < tau.ptr ⟺ 2.71828 < 3.14159 < 6.28318
```

**Strings** (lexicographic order):
```c
// Sorted by strcmp
fy_generic alice = fy_string("alice");
fy_generic bob = fy_string("bob");
fy_generic charlie = fy_string("charlie");

// After canonicalization:
// "alice" at offset 0x0000, "bob" at 0x0020, "charlie" at 0x0040
// Pointer comparison: alice.ptr < bob.ptr < charlie.ptr
```

**Mappings** (canonical key order):
```c
// Key order doesn't matter - canonical form sorts keys
fy_generic config1 = fy_mapping("port", 8080, "host", "localhost");
fy_generic config2 = fy_mapping("host", "localhost", "port", 8080);

// After canonicalization:
// Both become: {"host": "localhost", "port": 8080} (keys sorted)
// Dedup recognizes them as identical
// config1.ptr == config2.ptr (same canonical form!)
```

**Sequences** (preserve insertion order):
```c
// Order matters for sequences
fy_generic seq1 = fy_sequence("a", "b", "c");
fy_generic seq2 = fy_sequence("c", "b", "a");

// Different sequences (order preserved)
// But elements internally canonicalized
```

#### O(1) Comparison After Canonicalization

Once canonicalized, comparison becomes **pointer arithmetic**:

```c
// After fy_serialize_canonical() or fy_builder_canonicalize()
int fy_compare_canonical(fy_generic a, fy_generic b) {
    // Fast path: same builder, same type, canonical
    if (fy_same_builder(a, b) && fy_type(a) == fy_type(b) && fy_is_canonical(a)) {
        return (intptr_t)a.tag - (intptr_t)b.tag;  // Pointer arithmetic!
    }

    // Slow path: deep comparison
    return fy_deep_compare(a, b);
}

bool fy_equals_canonical(fy_generic a, fy_generic b) {
    // Deduplicated + canonical = pointer equality
    return a.tag == b.tag && fy_same_builder(a, b);
}
```

#### Content-Addressable Storage

**Deterministic hashing** enables Git-like content addressing:

```c
// Parse YAML, build in dedup arena
struct fy_generic_builder *gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .policy = FY_ALLOC_DEDUP
});
fy_generic config = parse_yaml(gb, "config.yaml");

// Serialize to canonical form with b3sum hash
uint8_t hash[32];
size_t size;
void *canonical = fy_serialize_canonical(gb, &size, &hash);

// Store in content-addressable cache
char hash_hex[65];
b3sum_to_hex(hash, hash_hex);
cache_store(hash_hex, canonical, size);

// Later: retrieve by content hash
void *retrieved = cache_fetch(hash_hex);
fy_generic restored = fy_deserialize_canonical(retrieved, size);
```

**Same content = same hash** (across all documents):
```c
// Two parsers, same logical content
fy_generic doc1 = parse_yaml(gb1, "config.yaml");
fy_generic doc2 = parse_json(gb2, "{\"host\": \"localhost\", \"port\": 8080}");

// Different formats, same logical structure
uint8_t hash1[32], hash2[32];
fy_serialize_canonical(gb1, NULL, &hash1);
fy_serialize_canonical(gb2, NULL, &hash2);

// Identical hashes!
assert(memcmp(hash1, hash2, 32) == 0);
```

#### Use Cases

**Build system caching**:
```c
// Cache build outputs by input hash
uint8_t input_hash[32];
fy_serialize_canonical(build_config, NULL, &input_hash);

void *cached_output = build_cache_lookup(input_hash);
if (cached_output) {
    return deserialize(cached_output);  // Cache hit!
}

// Build and cache
output = expensive_build(build_config);
build_cache_store(input_hash, serialize(output));
```

**Incremental computation**:
```c
// Track dependencies by content hash
struct computation {
    uint8_t input_hash[32];
    uint8_t output_hash[32];
    fy_generic cached_result;
};

// Check if input changed
uint8_t current_hash[32];
fy_serialize_canonical(input, NULL, &current_hash);

if (memcmp(current_hash, comp.input_hash, 32) == 0) {
    return comp.cached_result;  // Input unchanged, use cached result
}

// Recompute
comp.cached_result = compute(input);
memcpy(comp.input_hash, current_hash, 32);
fy_serialize_canonical(comp.cached_result, NULL, &comp.output_hash);
```

**Merkle tree / structural sharing**:
```c
// Subtrees with same hash are structurally identical
fy_generic prod = fy_mapping(
    "env", "production",
    "database", common_db_config  // Shared subtree
);

fy_generic dev = fy_mapping(
    "env", "development",
    "database", common_db_config  // Same pointer after dedup
);

// Subtree hash: common_db_config has identical hash in both
uint8_t db_hash[32];
fy_serialize_canonical_subtree(common_db_config, NULL, &db_hash);

// Can efficiently detect "configs differ only in env field"
```

**Git-like IPFS/CAS**:
```c
// Store by content hash (like Git objects)
uint8_t hash[32];
void *data = fy_serialize_canonical(document, &size, &hash);

// Store in content-addressable storage
cas_put(hash, data, size);

// Retrieve by hash
void *retrieved = cas_get(hash);
fy_generic doc = fy_deserialize_canonical(retrieved, size);

// Automatic deduplication: same content stored once
```

#### Why This Approach Is Optimal

**Separating concerns**:
- ✅ **Live performance**: No sorting overhead during allocation
- ✅ **Determinism**: Canonical form computed once at serialization
- ✅ **Flexibility**: Can use either raw or canonical representations
- ✅ **Compatibility**: Existing dedup code unchanged

**Comparison to alternatives**:

| Approach | Live Allocation | Serialization | Comparison |
|----------|----------------|---------------|------------|
| **Always sorted** | O(log n) insert | O(n) copy | O(1) pointer |
| **Deferred canonical** | O(1) insert | O(n log n) sort | O(1) after canonicalization |
| **No canonicalization** | O(1) insert | O(n) copy | O(n) deep compare |

**Deferred canonicalization wins**:
- Same O(1) live performance as non-canonical
- One-time O(n log n) cost only when serializing (caching, persistence)
- O(1) comparisons after canonicalization
- Deterministic hashing for content addressing

#### Implementation Sketch

```c
// Canonicalize builder in-place (for fast comparisons)
void fy_builder_canonicalize(struct fy_generic_builder *gb) {
    // Sort all values by type and comparison order
    qsort_by_type_and_value(gb->arena);

    // Update dedup table pointers
    rehash_dedup_table(gb);

    // Mark as canonical
    gb->flags |= FY_BUILDER_CANONICAL;
}

// Serialize to canonical byte stream with hash
void *fy_serialize_canonical(struct fy_generic_builder *gb,
                              size_t *out_size,
                              uint8_t out_hash[32]) {
    // Topologically sort arena
    struct arena_sorted sorted = topological_sort_arena(gb);

    // Relocate to zero-based offsets
    struct reloc_map relocs = compute_relocations(&sorted);

    // Write canonical byte stream
    void *buffer = write_canonical_stream(&sorted, &relocs, out_size);

    // Compute b3sum hash
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, buffer, *out_size);
    blake3_hasher_finalize(&hasher, out_hash, 32);

    return buffer;
}
```

**This creates a genuinely novel capability**: content-addressable, immutable, deduplicated data structures in C with **deterministic hashing across all documents** - essentially building a persistent, content-addressable database with the ergonomics of native data structures.

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

