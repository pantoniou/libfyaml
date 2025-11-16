# Generic API Patterns and Best Practices

This document covers common patterns, best practices, and anti-patterns for libfyaml's generic type system.

## Common Patterns and Best Practices

### Pattern 1: Configuration Management

**Use case**: Long-lived application configuration with many duplicate strings

**Best practice**: Use deduplicating builder at application scope

```c
// Application startup
struct fy_generic_builder *app_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .policy = FY_ALLOC_DEDUP | FY_ALLOC_COMPACT
});

// Load config files
fy_generic server_config = load_yaml_config(app_gb, "server.yaml");
fy_generic db_config = load_yaml_config(app_gb, "database.yaml");
fy_generic app_config = load_yaml_config(app_gb, "app.yaml");

// All configs share common strings ("localhost", "production", "utf-8", etc.)
// Significant memory savings on large config sets

// ... application runs ...

// Application shutdown
fy_generic_builder_destroy(app_gb);
```

**Benefits**:
- Minimal memory footprint (string interning)
- Fast equality checks (pointer comparison)
- Single cleanup point

### Pattern 2: Per-Request Processing

**Use case**: Web server handling requests with temporary data

**Best practice**: Use arena allocator for fast request-scoped allocation and cleanup

```c
void handle_http_request(fy_generic request) {
    // Request-scoped arena builder
    struct fy_generic_builder *req_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
        .policy = FY_ALLOC_ARENA | FY_ALLOC_FAST
    });

    // Parse request body
    fy_generic body = fy_parse_json(req_gb, request_body_str);

    // Extract fields
    const char *username = fy_get(body, "username", "");
    const char *email = fy_get(body, "email", "");

    // Build response
    fy_generic response = fy_gb_mapping(req_gb,
        "status", "success",
        "user", fy_local_mapping("name", username, "email", email)
    );

    send_response(response);

    // Entire request state freed at once - O(1) cleanup!
    fy_generic_builder_destroy(req_gb);
}
```

**Benefits**:
- Fast allocation (arena bump allocation)
- Fast cleanup (destroy entire arena)
- No memory leaks (single destroy call)
- Predictable memory usage

### Pattern 3: Hierarchical Scopes

**Use case**: Long-lived application config with per-request overrides

**Best practice**: Stackable builders for parent-child relationships

```c
// Global: deduplicating builder for app config
struct fy_generic_builder *global_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .policy = FY_ALLOC_DEDUP
});
fy_generic app_config = load_config(global_gb);

void handle_request(fy_generic request) {
    // Request: stacked arena that shares parent's dedup table
    struct fy_generic_builder *req_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
        .allocator = fy_allocator_create_stacked(global_gb->allocator),
        .policy = FY_ALLOC_ARENA
    });

    // Override config for this request
    fy_map_handle req_config = fy_assoc(req_gb, app_config, "timeout", fy_int(5000));

    // req_config can see global config's deduplicated strings
    // req_config's own allocations are fast (arena)

    process_with_config(req_config);

    fy_generic_builder_destroy(req_gb);  // global_gb unaffected
}

// Cleanup
fy_generic_builder_destroy(global_gb);
```

**Benefits**:
- Share dedup tables across scopes
- Fast per-request allocation (arena)
- Fast per-request cleanup
- Clear lifetime hierarchy

### Pattern 4: Thread-Local Builders

**Use case**: Multi-threaded application with per-thread temporary data

**Best practice**: Thread-local builders with arena allocation (no locking needed)

```c
_Thread_local struct fy_generic_builder *thread_gb = NULL;

void thread_init(void) {
    thread_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
        .policy = FY_ALLOC_ARENA  // No thread-safe flag needed!
    });
}

void worker_task(fy_generic input) {
    // Use thread_gb without any locking
    fy_seq_handle results = process_data(thread_gb, input);

    // ... work with results ...
}

void thread_cleanup(void) {
    fy_generic_builder_destroy(thread_gb);
}
```

**Benefits**:
- Zero contention (no locks)
- Fast allocation (arena)
- Simple lifecycle

### Pattern 5: Transaction Scope

**Use case**: Database transactions or atomic operations

**Best practice**: Transaction-scoped builder for easy rollback

```c
bool execute_transaction(fy_generic txn_spec) {
    // Transaction-scoped builder
    struct fy_generic_builder *txn_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
        .policy = FY_ALLOC_ARENA
    });

    bool success = false;

    // Build up transaction state
    fy_seq_handle operations = fy_seq_empty;

    fy_seq_handle users = fy_get(txn_spec, "users", fy_seq_invalid);
    for (size_t i = 0; i < fy_len(users); i++) {
        fy_generic user = fy_get_item(users, i);

        // Validate operation
        if (!validate_user(user)) {
            goto rollback;  // Jump to cleanup
        }

        // Add to operations
        operations = fy_conj(txn_gb, operations, user);
    }

    // Commit
    if (apply_operations(operations)) {
        success = true;
    }

rollback:
    // Rollback is trivial: just destroy the builder
    // All transaction state is freed at once
    fy_generic_builder_destroy(txn_gb);

    return success;
}
```

**Benefits**:
- Easy rollback (just destroy builder)
- No need to track individual allocations
- Clear transaction boundaries

### Pattern 6: Functional Data Transformation

**Use case**: Transform data through multiple stages

**Best practice**: Use immutable operations for clear data flow

```c
fy_seq_handle transform_users(struct fy_generic_builder *gb, fy_seq_handle users) {
    fy_seq_handle result = fy_seq_empty;

    for (size_t i = 0; i < fy_len(users); i++) {
        fy_generic user = fy_get_item(users, i);

        // Filter: only active users
        if (!fy_get(user, "active", false)) {
            continue;
        }

        // Transform: add computed field
        int age = fy_get(user, "age", 0);
        fy_map_handle enriched = fy_assoc(gb, user, "senior", fy_bool(age >= 65));

        // Collect
        result = fy_conj(gb, result, enriched);
    }

    return result;
}

// Original users unchanged, new collection returned
fy_seq_handle active_seniors = transform_users(gb, all_users);
```

**Benefits**:
- Original data unchanged (referential transparency)
- Easy to test (pure function)
- Can chain transformations naturally

### Pattern 7: Copy-on-Write Configuration

**Use case**: Default config with per-user customization

**Best practice**: Share base config, only allocate deltas

```c
// Base config (stack-allocated, immutable)
fy_generic base_config = fy_local_mapping(
    "theme", "light",
    "language", "en",
    "notifications", true,
    "timeout", 30
);

// Per-user overrides (builder-allocated)
fy_map_handle alice_config = fy_assoc(user_gb, base_config, "theme", fy_string("dark"));
fy_map_handle bob_config = fy_assoc(user_gb, base_config, "language", fy_string("es"));
fy_map_handle carol_config = fy_assoc(user_gb, base_config,
    "theme", fy_string("dark"),
    "language", fy_string("fr")
);

// base_config shared via structural sharing, only deltas allocated
```

**Benefits**:
- Memory efficient (shared base)
- Fast customization (only allocate deltas)
- Immutable (thread-safe)

### Pattern 8: Temporary Stack Values

**Use case**: Quick data transformations without heap allocation

**Best practice**: Use stack-allocated values for short-lived data

```c
void quick_json_parse(const char *json_str) {
    // Stack-allocated (automatic cleanup)
    fy_generic data = fy_parse_json_string(json_str);

    // Extract what you need
    const char *name = fy_get(data, "name", "unknown");
    int count = fy_get(data, "count", 0);

    printf("Name: %s, Count: %d\n", name, count);

    // No cleanup needed - automatic at function exit
}
```

**Benefits**:
- Zero allocation overhead
- Automatic cleanup
- Simple code

### Pattern 9: Immutable Cache with Copy-on-Write Updates (Planned)

**Use case**: Fast caching of parsed files with incremental updates

**Best practice**: Use read-only allocator for cache, COW allocator for updates

```c
// Global immutable cache
struct {
    struct fy_allocator *cache;
    struct {
        const char *filename;
        fy_generic data;
    } entries[MAX_FILES];
} file_cache;

void init_cache(void) {
    file_cache.cache = fy_allocator_create_dedup();
}

// Parse file and cache result
fy_generic parse_and_cache(const char *filename) {
    // Parse into cache
    fy_generic data = parse_yaml_file(file_cache.cache, filename);

    // Add to cache
    add_cache_entry(filename, data);

    return data;
}

// Make cache immutable (no further modifications)
void freeze_cache(void) {
    fy_allocator_set_readonly(file_cache.cache);
}

// Incremental update with COW
fy_generic update_cached_file(const char *filename) {
    fy_generic old_data = lookup_cache(filename);

    // Create COW allocator (uses cache as read-only base)
    struct fy_generic_builder *update_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
        .allocator = fy_allocator_create_cow(file_cache.cache),
        .policy = FY_ALLOC_DEDUP
    });

    // Parse new version
    fy_generic new_data = parse_yaml_file(update_gb, filename);

    // Dedup will reference unchanged parts from cache (zero-copy)!
    // Only changed parts allocated in update_gb

    // Commit: freeze new allocator as new cache
    struct fy_allocator *new_cache = fy_allocator_from_builder(update_gb);
    fy_allocator_set_readonly(new_cache);

    // Swap caches
    struct fy_allocator *old_cache = file_cache.cache;
    file_cache.cache = new_cache;

    // Clean up old cache when safe
    // (when all readers using old_data are done)

    return new_data;
}

// Transaction: try update, rollback on error
fy_generic transactional_update(const char *filename) {
    struct fy_generic_builder *txn_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
        .allocator = fy_allocator_create_cow(file_cache.cache),
        .policy = FY_ALLOC_DEDUP
    });

    fy_generic new_data = parse_yaml_file(txn_gb, filename);

    // Validate
    if (!validate(new_data)) {
        // Rollback: just destroy, cache unchanged
        fy_generic_builder_destroy(txn_gb);
        return fy_invalid;
    }

    // Commit
    struct fy_allocator *new_cache = fy_allocator_from_builder(txn_gb);
    fy_allocator_set_readonly(new_cache);

    update_cache(new_cache);
    return new_data;
}
```

**Benefits**:
- Fast cache lookups (O(1) pointer dereference, zero-copy)
- Incremental updates (O(Δ) where Δ = size of changes)
- Transactional semantics (commit/rollback)
- Multi-version concurrency (multiple readers, one writer)
- Content-addressable (dedup across versions)
- Git-like storage (immutable objects)

**Use cases**:
- Configuration file caching with hot reload
- Build system (cache parsed build files)
- LSP server (incremental document edits)
- Template engines (cache + hot reload)
- Query cache (parsed queries with invalidation)

**Performance**:
- Cache hit: 0 ns (pointer dereference)
- Incremental update: Proportional to change size, not file size
- Memory: Unchanged parts shared, only deltas allocated
- Commit: O(1) pointer swap
- Rollback: O(1) destroy update allocator

### Anti-Patterns to Avoid

**❌ Don't mix stack and builder lifetimes carelessly**:
```c
// BAD: Storing stack value pointer beyond scope
fy_generic *get_config_ptr(void) {
    fy_generic config = fy_local_mapping("key", "value");
    return &config;  // DANGLING POINTER!
}

// GOOD: Use builder for persistent values
fy_generic get_config(struct fy_generic_builder *gb) {
    return fy_gb_mapping(gb, "key", "value");  // Safe
}
```

**❌ Don't forget to destroy builders**:
```c
// BAD: Memory leak
void process_data(void) {
    struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
    // ... use gb ...
    return;  // LEAK!
}

// GOOD: Always destroy
void process_data(void) {
    struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
    // ... use gb ...
    fy_generic_builder_destroy(gb);
}
```

**❌ Don't use arena allocators for long-lived data**:
```c
// BAD: Arena grows without bound
struct fy_generic_builder *arena_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .policy = FY_ALLOC_ARENA
});

// Long-running loop
while (server_running) {
    fy_generic request = receive_request();
    fy_generic response = fy_gb_mapping(arena_gb, ...);  // Arena grows forever!
    send_response(response);
}
```

**❌ Don't over-use dedup for unique data**:
```c
// BAD: Dedup overhead with no benefit
struct fy_generic_builder *gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .policy = FY_ALLOC_DEDUP
});

// Generate unique IDs
for (int i = 0; i < 1000000; i++) {
    char id[64];
    snprintf(id, sizeof(id), "unique-id-%d", i);  // All unique!
    fy_generic record = fy_gb_mapping(gb, "id", id, ...);  // Dedup table wasted
}

// GOOD: Use fast allocator for unique data
struct fy_generic_builder *gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
    .policy = FY_ALLOC_FAST
});
```

### Performance Tips

1. **Choose the right allocator policy**:
   - Config files → `FY_ALLOC_DEDUP` (many duplicate strings)
   - Request handling → `FY_ALLOC_ARENA` (fast alloc/free)
   - Unique data → `FY_ALLOC_FAST` (no dedup overhead)

2. **Reuse builders when possible**:
   ```c
   // Thread-local builder reused across requests
   _Thread_local struct fy_generic_builder *req_gb = NULL;

   void handle_request(void) {
       if (!req_gb) {
           req_gb = fy_generic_builder_create(&(struct fy_generic_builder_cfg){
               .policy = FY_ALLOC_ARENA
           });
       }

       // Use req_gb...

       // Optional: reset arena periodically
       if (request_count % 1000 == 0) {
           fy_generic_builder_reset(req_gb);
       }
   }
   ```

3. **Leverage internalization short-circuit**:
   ```c
   // First operation: internalizes (one-time cost)
   fy_seq_handle seq2 = fy_conj(gb, seq1, item1);

   // Subsequent: cheap! (already internalized)
   fy_seq_handle seq3 = fy_conj(gb, seq2, item2);
   fy_seq_handle seq4 = fy_conj(gb, seq3, item3);
   ```

4. **Use stack values for temporary work**:
   ```c
   // No allocation overhead
   fy_generic temp = fy_local_mapping("key", "value");
   const char *value = fy_get(temp, "key", "");
   // Automatic cleanup
   ```

