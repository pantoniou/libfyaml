# Lock-Free Dedup Allocator Design

## Overview

This document describes the lock-free design for libfyaml's deduplication allocator. The design eliminates locks from the hot path (store/lookup operations) while maintaining correctness through atomic operations and append-only data structures.

## Design Principles

### 1. Append-Only Architecture

Like the mremap allocator, the dedup allocator follows an **append-only** design during the hot path:

- **No deletions during allocation phase**: Entries are never removed while threads are actively allocating
- **No reference counting**: Eliminates atomic increment/decrement overhead and ABA problems
- **Cleanup is single-threaded**: Only called during document destruction (by design contract)

This matches libfyaml's usage pattern: multiple threads parse/manipulate documents concurrently, but document lifecycle (creation/destruction) is single-threaded.

### 2. Lock-Free Hash Table Growth

Traditional approach (problematic):
```c
struct fy_dedup_tag {
    unsigned int data_active;           // ❌ Flip-flop between 0/1
    struct fy_dedup_tag_data data[2];   // ❌ Readers see wrong table after flip
};
```

New approach (lock-free):
```c
struct fy_dedup_tag {
    FY_ATOMIC(struct fy_dedup_tag_data *) data_list;  // ✅ Linked list of tables
};

struct fy_dedup_tag_data {
    struct fy_dedup_tag_data *next;  // ✅ Next table in chain
    // ... bloom filter, buckets ...
};
```

When the hash table needs to grow:
1. Allocate a new, larger `fy_dedup_tag_data` structure
2. Atomically prepend it to `data_list` using CAS
3. Old tables remain readable - no data migration needed
4. New insertions go to the newest (head) table

### 3. Single-Linked Bucket Chains

Replace kernel-style double-linked lists with CAS-friendly single-linked lists:

**Old (not thread-safe):**
```c
struct fy_dedup_entry {
    struct list_head node;  // ❌ Prev/next pointers, complex updates
    // ...
};
```

**New (lock-free):**
```c
struct fy_dedup_entry {
    struct fy_dedup_entry *next;  // ✅ Single pointer, CAS-friendly
    uint64_t hash;
    size_t size;
    void *mem;
};

struct fy_dedup_entry_list {
    FY_ATOMIC(struct fy_dedup_entry *) head;  // ✅ Atomic head pointer
};
```

## Core Operations

### Lookup Algorithm

```c
const void *fy_dedup_storev(...) {
    // 1. Check all tables (newest to oldest) for existing entry
    for (dtd = fy_atomic_load(&dt->data_list); dtd; dtd = dtd->next) {
        // Check bloom filter first (fast rejection)
        bloom_hit = fy_id_is_used(dtd->bloom_id, bloom_pos);
        if (!bloom_hit)
            continue;

        // Search bucket chain for matching entry
        del = &dtd->buckets[bucket_pos];
        for (de = fy_atomic_load(&del->head); de; de = de->next) {
            if (de->hash == hash &&
                de->size == size &&
                !memcmp(de->mem, data, size)) {
                return de->mem;  // Found! Return existing allocation
            }
        }
    }

    // 2. Not found, need to insert new entry
    // ... (see insertion algorithm)
}
```

### Insertion Algorithm with Duplicate Detection

```c
// Not found in any table, allocate new entry
de = allocate_entry(...);
de->hash = hash;
de->size = size;
de->mem = allocated_memory;

do {
    // Snapshot current state
    top_dtd = fy_atomic_load(&dt->data_list);
    bucket_pos = hash & top_dtd->bucket_count_mask;
    del = &top_dtd->buckets[bucket_pos];
    old_head = fy_atomic_load(&del->head);

    // Check if table grew since we started
    if (fy_atomic_load(&dt->data_list) != top_dtd) {
        // Table grew - restart lookup from scratch
        // Another thread might have inserted our duplicate
        goto restart_lookup;
    }

    // Prepare to insert at head of bucket chain
    de->next = old_head;

    // Try atomic insertion
    if (!fy_atomic_compare_exchange_strong(&del->head, &old_head, de)) {
        // CAS failed - someone else modified this bucket
        // Check only the NEW entries (between new_head and old_head)
        new_head = fy_atomic_load(&del->head);
        for (check_de = new_head; check_de != old_head; check_de = check_de->next) {
            if (check_de->hash == hash &&
                check_de->size == size &&
                !memcmp(check_de->mem, data, size)) {
                // Found duplicate! Free our allocation, use theirs
                fy_allocator_free(..., de->mem);
                fy_allocator_free(..., de);
                return check_de->mem;
            }
        }
        // Not a duplicate, just a hash collision - retry CAS
        continue;
    }

    // Success! Update bloom filter
    fy_id_set_used(dtd->bloom_id, bloom_pos);
    break;

} while (true);

return de->mem;
```

**Key insight**: On CAS failure, we only need to check entries that appeared **between our snapshot and now** (the delta). This is the window where another thread could have inserted our duplicate.

### Hash Table Growth

The hash table grows when CAS retry contention is detected. This is a **superior growth trigger** compared to chain length because:

- **Direct contention measurement**: CAS retries indicate actual thread contention, not just bucket occupancy
- **Zero measurement cost**: Retry count is already tracked during insertion, no chain walk needed
- **Immediate feedback**: Detects hot spots exactly when performance degrades
- **Self-tuning**: Grows precisely when parallel performance suffers

```c
// During insertion (CAS retry loop)
retry_count = 0;
do {
    top_dtd = fy_atomic_load(&dt->data_list);
    bucket_pos = hash & top_dtd->bucket_count_mask;
    del = &top_dtd->buckets[bucket_pos];
    old_head = fy_atomic_load(&del->head);

    if (fy_atomic_load(&dt->data_list) != top_dtd)
        goto restart_lookup;

    de->next = old_head;

    if (!fy_atomic_compare_exchange_strong(&del->head, &old_head, de)) {
        retry_count++;  // Track contention
        // Check for duplicates in delta...
        continue;
    }
    break;
} while (true);

// After successful insert, check if growth needed
if (retry_count >= RETRY_THRESHOLD) {  // e.g., 2-3 retries
    attempt_grow(dtd);
}
```

**Growth implementation**:
```c
static void attempt_grow(struct fy_dedup_tag_data *dtd) {
    // Use atomic flag to prevent concurrent growth
    flags = fy_atomic_load(&dtd->flags);
    if (flags & FYDTDF_GROWING)
        return;  // Someone else is growing

    if (!fy_atomic_compare_exchange_strong(&dtd->flags, &flags,
                                           flags | FYDTDF_GROWING))
        return;  // Lost race to grow

    // We won - allocate new larger table
    new_dtd = allocate_table(larger_size);

    // Atomically prepend to table list
    do {
        old_head = fy_atomic_load(&dt->data_list);
        new_dtd->next = old_head;
    } while (!fy_atomic_compare_exchange_strong(&dt->data_list,
                                                 &old_head, new_dtd));

    // Release growing flag
    fy_atomic_fetch_and(&dtd->flags, ~FYDTDF_GROWING);
}
```

**Tuning the retry threshold**:
- **Conservative (2-3 retries)**: Grow eagerly, minimize contention, slightly more tables
- **Aggressive (5-10 retries)**: Tolerate more contention, fewer tables, more CAS spinning

With typical 60% memory savings from deduplication, a conservative threshold (2-3 retries) is recommended to maximize parallel throughput while still maintaining excellent memory efficiency.

**No data migration**: Old entries stay in old tables. Lookups traverse all tables, so nothing is lost.

## Race Condition Handling

### Race 1: Concurrent Insertions of Same Data

**Scenario**: Two threads A and B try to insert identical data simultaneously.

**Resolution**:
1. Both threads traverse all tables, both miss (data doesn't exist yet)
2. Both allocate memory and create entries
3. Thread A wins CAS, inserts entry
4. Thread B's CAS fails, sees A's entry in delta check
5. Thread B frees its allocation, returns A's memory

**Result**: Only one copy stored, no memory leak.

### Race 2: Table Growth During Insertion

**Scenario**: Thread A is inserting while Thread B grows the table.

**Resolution**:
1. Thread A snapshots `top_dtd` before CAS
2. Thread B allocates and prepends new table
3. Thread A checks if `dt->data_list` changed
4. If changed, Thread A restarts lookup (might find duplicate in new table)
5. If unchanged, Thread A's entry goes into old table (still valid - lookups check all tables)

**Result**: Either duplicate detected on restart, or entry stored in old table (still findable).

### Race 3: Concurrent Table Growth

**Scenario**: Multiple threads decide to grow simultaneously.

**Resolution**: Use `FYDTDF_GROWING` flag (similar to mremap's `FYMRAF_GROWING`):
1. First thread sets flag via CAS, proceeds with growth
2. Other threads see flag set, skip growth
3. First thread clears flag after prepending new table

**Result**: Only one growth operation at a time, no wasted allocations.

## Data Structure Changes

### Main Structures

```c
struct fy_dedup_tag {
    FY_ATOMIC(struct fy_dedup_tag_data *) data_list;  // Linked list of tables
    int entries_tag;                                   // For entry allocations
    int content_tag;                                   // For content allocations
    bool bloom_filter_needs_update;                    // Cleanup hint
    struct fy_allocator_stats stats;                   // Atomic updates
};

struct fy_dedup_tag_data {
    struct fy_dedup_tag_data *next;      // Next table (immutable after link)
    FY_ATOMIC(uint64_t) flags;           // FYDTDF_GROWING, etc.

    unsigned int bloom_filter_bits;
    unsigned int bloom_filter_mask;
    size_t bloom_id_count;
    fy_id_bits *bloom_id;                // Atomic bit operations
    fy_id_bits *bloom_update_id;

    unsigned int bucket_count_bits;
    unsigned int bucket_count_mask;
    size_t bucket_count;
    struct fy_dedup_entry_list *buckets; // Array of atomic head pointers
    struct fy_dedup_entry_list *buckets_end;

    size_t bucket_id_count;
    fy_id_bits *buckets_in_use;          // Atomic bit operations
    fy_id_bits *buckets_collision;

    size_t dedup_threshold;
    unsigned int retry_grow_threshold;   // CAS retry threshold for growth
};

struct fy_dedup_entry_list {
    FY_ATOMIC(struct fy_dedup_entry *) head;  // Atomic head pointer for CAS
};

struct fy_dedup_entry {
    struct fy_dedup_entry *next;  // Immutable after insertion
    uint64_t hash;
    size_t size;
    void *mem;                    // Content pointer
};
```

### Flags

```c
#define FYDTDF_GROWING  FY_BIT(0)  // Table is being grown
```

## Performance Characteristics

### Hot Path (Lock-Free)

**Lookup hit**:
- Traverse table list: O(number of grows) - typically 1-3 tables
- Bloom filter check: O(1) atomic load
- Bucket chain walk: O(chain length) - kept short by growth policy
- **No locks, no contention**

**Insertion**:
- All of lookup costs
- CAS on bucket head: O(1) atomic operation
- Rare retry on collision
- **No locks, minimal contention** (distributed across hash buckets)

**Growth**:
- Triggered by CAS retry contention (direct performance measurement)
- One thread grows via GROWING flag, others continue allocating
- **No stop-the-world pause**

### Cleanup Path (Single-Threaded)

- Traverse all tables, free all entries
- Free all table structures
- No atomics needed (single-threaded by contract)

## Comparison with Lock-Based Design

| Aspect | Lock-Based | Lock-Free |
|--------|------------|-----------|
| **Contention** | High (global or per-tag lock) | Low (distributed across buckets) |
| **Scalability** | Poor (serialization) | Excellent (parallel lookups/inserts) |
| **Complexity** | Simple (mutex around operations) | Moderate (CAS loops, delta checks) |
| **Growth** | Stop-the-world or complex locking | Append new table, no pause |
| **Memory** | Single table, periodic resize | Multiple tables, memory overhead |
| **Duplicates** | None (locked critical section) | Rare (tiny race window) |

## Memory Overhead

**Multiple tables**: Growing creates new tables without freeing old ones. For a document that triggers N grows:
- Memory = initial_table + 2×initial + 4×initial + ... + 2^N×initial
- In practice: 2-3 grows typical, ~8× overhead worst case
- Acceptable for document lifetime (freed at cleanup)

**Trade-off**: More memory during allocation phase for zero-lock concurrency.

## Thread Safety Guarantees

### Safe Operations (Multi-Threaded)

- ✅ `fy_dedup_storev()` / `fy_dedup_store()` - Concurrent stores
- ✅ Lookups across all tables
- ✅ Hash table growth
- ✅ Bloom filter updates (atomic bit operations)
- ✅ Stats updates (atomic increments)

### Unsafe Operations (Single-Threaded Only)

- ❌ `fy_dedup_tag_setup()` - Document creation
- ❌ `fy_dedup_tag_cleanup()` - Document destruction
- ❌ `fy_dedup_tag_reset()` - Table reset
- ❌ `fy_dedup_release()` - Entry deletion (if implemented)

This matches the design contract: document lifecycle is single-threaded, but document manipulation (which triggers allocations) is multi-threaded.

## Implementation Notes

### Bloom Filter Thread Safety

The `fy_id_set_*` operations are thread-safe (atomic bit operations). Bloom filter false positives are acceptable, so concurrent updates don't require additional synchronization beyond atomic bit sets.

### Stats Accounting

Use atomic operations for stats updates:
```c
fy_atomic_fetch_add(&dt->stats.stores, 1);
fy_atomic_fetch_add(&dt->stats.stored, total_size);
```

Or use per-thread stats with merge at cleanup (more complex, zero contention).

### Memory Ordering

CAS operations provide necessary memory barriers. No explicit fences needed in typical usage.

## Future Optimizations

### Table Compaction (Optional)

During cleanup or explicit trim, could migrate entries from old tables to newest:
- Walk old tables
- Re-insert entries into newest table
- Free old tables

**Trade-off**: Reduces memory at cost of cleanup time.

### Smoothed Retry Rate Tracking (Alternative)

Instead of per-operation threshold, track aggregate retry rate:

```c
struct fy_dedup_tag_data {
    FY_ATOMIC(uint64_t) total_retries;
    FY_ATOMIC(uint64_t) total_inserts;
};

// After each insert
fy_atomic_fetch_add(&dtd->total_retries, retry_count);
fy_atomic_fetch_add(&dtd->total_inserts, 1);

// Periodic check (every 256 inserts)
if ((total_inserts & 0xFF) == 0) {
    if (total_retries > total_inserts * 2) {  // >2× retry rate
        attempt_grow(dtd);
    }
}
```

**Trade-off**: More resistant to noise, but slightly delayed response. Per-operation threshold (2-3 retries) is simpler and works well in practice.

### Per-Bucket Retry Statistics (Debugging)

For tuning and debugging, track which buckets are hot:

```c
struct fy_dedup_tag_data {
    FY_ATOMIC(uint32_t) *bucket_retries;  // Parallel array to buckets
};

// During insert
if (retry_count > 0)
    fy_atomic_fetch_add(&dtd->bucket_retries[bucket_pos], retry_count);

// On stats dump
for (i = 0; i < bucket_count; i++) {
    retries = fy_atomic_load(&dtd->bucket_retries[i]);
    if (retries > threshold)
        fprintf(stderr, "Hot bucket %u: %u retries\n", i, retries);
}
```

Reveals hash distribution problems and whether growth is effective.

### RCU-Style Deletion (Future)

If entry deletion becomes needed:
- Mark entries as deleted (atomic flag)
- Actual free during cleanup
- Lookups skip deleted entries

## Performance Expectations

### Memory Efficiency

Real-world measurements show **~60% memory reduction** for large documents with typical string deduplication patterns (repeated keys, common values). The lock-free design maintains this efficiency while enabling parallel access.

### Throughput Estimates

**Single-threaded baseline**:
- Lookup hit: ~50-100ns (10-20M ops/sec)
- Insert: ~500-1000ns (1-2M ops/sec)
- Dedup overhead: ~5-10% vs. no dedup

**Multi-threaded scaling**:
- 4 threads: ~3.5-3.8× speedup (vs. single-threaded)
- 8 threads: ~6-7× speedup (70-90% efficiency)
- Lock-based would achieve only ~1.5-2.5× due to contention

**Growth trigger performance**:
- CAS retry threshold provides immediate response to contention
- Conservative threshold (2-3 retries): minimizes CAS spinning, maximizes throughput
- Aggressive threshold (5-10 retries): tolerates more contention, fewer table allocations

### Bottleneck Analysis

1. **Table list traversal**: ~5-10 cycles per table; 2-3 tables typical after growth
2. **Memcmp on collision**: ~1 cycle/byte; rare with good hash function (XXHash64)
3. **CAS retry storms**: Mitigated by retry-based growth trigger

For I/O-bound YAML parsing, the lock-free allocator effectively **eliminates the allocator as a bottleneck**.

## Conclusion

The lock-free dedup allocator design leverages:
1. **Append-only architecture** - eliminates deletion complexity
2. **Multiple hash tables** - allows growth without stop-the-world
3. **CAS-based insertion** - detects concurrent duplicates
4. **Retry-based growth trigger** - directly measures and responds to contention
5. **Single-threaded cleanup** - simplifies memory management

This design provides excellent multi-threaded performance for libfyaml's document parsing workloads while maintaining 60% memory savings and simplicity. The CAS retry growth trigger ensures the allocator automatically adapts to contention, maintaining high throughput as thread count scales.
