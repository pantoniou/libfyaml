# Parallel Map and Filter Implementation

This document describes the parallel map and filter operations added to libfyaml's generic collection API.

## Overview

Parallel versions of `map` and `filter` operations have been implemented using the existing work-stealing thread pool infrastructure. These operations automatically parallelize processing of large collections while falling back to sequential execution for small collections.

## Files Modified/Created

### New Files

**`src/generic/fy-generic-parallel.c`**
- Contains parallel map and filter implementations
- Worker functions for thread pool execution
- Threshold-based parallelization (100 items)

### Modified Files

**`src/generic/fy-generic.h`**
- Added function declarations for `fy_gb_pmap()` and `fy_gb_pfilter()`

**`src/Makefile.am`**
- Added `generic/fy-generic-parallel.c` to libfyaml_la_SOURCES

**`CMakeLists.txt`**
- Added `src/generic/fy-generic-parallel.c` to LIBFYAML_SOURCES

**`test/libfyaml-test-generic.c`**
- Added `gb_pmap` test case (10,000 item sequences, mappings)
- Added `gb_pfilter` test case (10,000 item sequences, empty sequences, mappings)

## Implementation Details

### Parallel Map (`fy_gb_pmap`)

**Signature:**
```c
fy_generic fy_gb_pmap(struct fy_generic_builder *gb, fy_generic seq,
                      size_t count, const fy_generic *items,
                      fy_generic_map_xform_fn fn)
```

**Features:**
- Work-stealing thread pool with auto-detected CPU count
- Chunks distributed evenly across threads
- Each worker applies map function to its chunk
- All workers share the same dedup builder (lock-free CAS operations!)
- Falls back to sequential for collections < 100 items
- Handles both sequences and mappings (maps values, preserves keys)
- **Order preserved**: Items appear in original order

**Worker Function:**
```c
static void fy_pmap_worker(void *arg)
```
- Processes a contiguous chunk of input items
- Calls user's map function with `(gb, value)`
- Stores results directly in pre-allocated output buffer at fixed positions
- Stride-aware (1 for sequences, 2 for mappings)

### Parallel Filter (`fy_gb_pfilter`)

**Signature:**
```c
fy_generic fy_gb_pfilter(struct fy_generic_builder *gb, fy_generic seq,
                         size_t count, const fy_generic *items,
                         fy_generic_filter_pred_fn fn)
```

**Features:**
- Work-stealing thread pool
- Atomic counter for output position allocation
- Each worker filters its chunk independently
- Results written directly to output buffer using atomic fetch-and-add
- Falls back to sequential for collections < 100 items
- **Order NOT preserved**: Items may appear in different order due to parallel writes

**Worker Function:**
```c
static void fy_pfilter_worker(void *arg)
```
- Tests predicate on each item in chunk
- Atomically reserves space in output buffer for passing items
- Writes passing items to reserved positions
- No post-processing or compaction needed

## Performance Characteristics

**Parallelization Threshold:**
- Collections with < 100 items use sequential implementation
- Avoids thread overhead for small workloads

**Expected Scaling:**
- Near-linear for map (embarrassingly parallel, order preserved)
- Good scaling for filter (atomic counter overhead minimal, order not preserved)
- Work-stealing prevents idle threads

**Memory Efficiency:**
- Shared dedup builder (lock-free, no contention)
- Single output buffer allocated once
- Stack allocation for work structures
- No compaction step needed for filter

## Integration with Existing Infrastructure

**Thread Pool:**
- Uses `FYTPCF_STEAL_MODE` for work stealing
- Auto-detects number of CPUs
- Calling thread participates in execution

**Dedup Allocator:**
- All workers share same `struct fy_generic_builder *gb`
- Lock-free CAS on bucket heads
- Automatic structural sharing across threads
- Growth triggered by CAS retry count

**Function Signatures:**
- Compatible with existing sequential callbacks:
  - `fy_generic_map_xform_fn(struct fy_generic_builder *gb, fy_generic v)`
  - `fy_generic_filter_pred_fn(struct fy_generic_builder *gb, fy_generic v)`

## Test Coverage

**Parallel Map Tests:**
- Empty sequence → returns `fy_seq_empty`
- Large sequence (10,000 items) → forces parallel execution
- Verify results: double all values, order preserved
- Mapping → preserves keys, maps values

**Parallel Filter Tests:**
- Empty sequence → returns `fy_seq_empty`
- Large sequence (10,000 items) → filters values > 100
- All items filtered out → returns empty
- Mapping → filters on values, preserves key-value pairs
- **Note**: Order is not preserved due to atomic output allocation

## Usage Example

```c
struct fy_generic_builder_cfg cfg;
struct fy_generic_builder *gb;

memset(&cfg, 0, sizeof(cfg));
cfg.flags = FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED;
gb = fy_generic_builder_create(&cfg);

// Create large sequence
fy_generic items[10000];
for (int i = 0; i < 10000; i++)
    items[i] = fy_value(gb, i);
fy_generic seq = fy_gb_sequence_create(gb, 10000, items);

// Parallel map (order preserved)
fy_generic doubled = fy_gb_pmap(gb, seq, 0, NULL, FN({
    return fy_value(gb, fy_cast(x, 0) * 2);
}));

// Parallel filter (order NOT preserved)
fy_generic filtered = fy_gb_pfilter(gb, seq, 0, NULL, FN({
    return fy_cast(x, 0) > 100;
}));

fy_generic_builder_destroy(gb);
```

## Limitations

**Order Preservation:**
- `fy_gb_pmap()`: Order is preserved
- `fy_gb_pfilter()`: Order is **not** preserved (items written as threads complete)

If order preservation is required for filter, use the sequential `fy_gb_collection_op()` with `FYGBOPF_FILTER`.

## Future Enhancements

**Not Yet Implemented:**
- Parallel reduce (requires tree reduction strategy)
- Adaptive threshold based on CPU count
- Batch support for multiple input sequences
- Performance benchmarking vs sequential
- Order-preserving parallel filter (would require sorted merge of thread-local buffers)

**Integration Opportunities:**
- Add `FYGBOPF_PARALLEL` flag support to `fy_gb_collection_op()`
- Automatic parallelization based on collection size
- Polymorphic macro wrappers: `fy_pmap()`, `fy_pfilter()`

## Build Instructions

Both build systems support the parallel implementation:

**Autotools:**
```bash
./configure
make
make check  # Runs parallel tests
```

**CMake:**
```bash
mkdir build && cd build
cmake ..
make
make all test  # or: ctest
```

## Notes

- Parallel operations work best with the dedup allocator
- Small collections automatically use sequential path (no overhead)
- Work-stealing provides excellent load balancing
- All thread pool creation/destruction is automatic (no manual management)
- Thread-safe thanks to lock-free dedup allocator
- **Parallel filter does not preserve order** - use sequential filter if order matters
