# Parallel Map/Filter Performance Benchmarks (Updated with Thread Pool Reuse)

## System Configuration
- CPU: 12 threads detected by thread pool  
- Parallelization threshold: 100 items
- Dedup allocator enabled
- **Thread pool created ONCE and reused** (reflects real-world usage)

## Benchmark Results

### Heavy Workload (100 sin/cos operations per item)

| Items  | MAP Serial | MAP Parallel | MAP Speedup | FILTER Serial | FILTER Parallel | FILTER Speedup |
|--------|------------|--------------|-------------|---------------|-----------------|----------------|
| 1,000  | 2.9 ms     | 0.3 ms       | **8.66x**   | 1.8 ms        | 0.2 ms          | **11.21x**     |
| 5,000  | 11.0 ms    | 1.6 ms       | **6.71x**   | 6.5 ms        | 1.0 ms          | **6.58x**      |
| 10,000 | 21.6 ms    | 3.2 ms       | **6.69x**   | 14.1 ms       | 2.0 ms          | **7.25x**      |
| 50,000 | 101.9 ms   | 14.3 ms      | **7.15x**   | 102.3 ms      | 8.7 ms          | **11.74x**     |

### Key Improvements with Thread Pool Reuse

**Before (creating thread pool each time):**
- Map 1,000 items: 3.50x speedup
- Filter 1,000 items: 2.04x speedup

**After (reusing thread pool):**
- Map 1,000 items: **8.66x speedup** (2.5x improvement!)
- Filter 1,000 items: **11.21x speedup** (5.5x improvement!)

Thread pool creation was adding ~1-2ms overhead per operation, which was significant for small datasets.

## API Usage

### Reusing Thread Pool (Recommended)

```c
struct fy_thread_pool_cfg tp_cfg = {
    .flags = FYTPCF_STEAL_MODE,
    .num_threads = 0  // Auto-detect CPUs
};
struct fy_thread_pool *tp = fy_thread_pool_create(&tp_cfg);

// Reuse the thread pool for multiple operations
for (int i = 0; i < many_iterations; i++) {
    result1 = fy_gb_pmap(gb, tp, seq, 0, NULL, map_fn);
    result2 = fy_gb_pfilter(gb, tp, seq2, 0, NULL, filter_fn);
}

fy_thread_pool_destroy(tp);
```

### One-Off Usage (Convenience)

```c
// Pass NULL to create a temporary thread pool
result = fy_gb_pmap(gb, NULL, seq, 0, NULL, map_fn);
```

## Analysis

**Thread Pool Reuse Benefits:**
- Eliminates 1-2ms overhead per operation
- Small datasets (1K items) show 2.5-5.5x better speedup
- Large datasets show minimal difference (overhead amortized)
- Matches real-world usage patterns (batch processing)

**Conclusion:**
- **Always reuse thread pools** for production code
- Peak speedup: **11.74x for filter** (50K items)
- Consistent 6-11x speedups across all dataset sizes
- Even 1,000 item datasets benefit significantly

## Recommendations

**For production code:**
1. Create thread pool at application startup
2. Reuse for all parallel operations
3. Destroy at shutdown
4. Avoids overhead, maximizes throughput

**For one-off operations:**
- Pass NULL for convenience
- Accept ~1-2ms overhead for simplicity
- Still much faster than serial for large datasets
