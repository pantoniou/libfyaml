# Parallel Map/Filter Performance Benchmarks

## System Configuration
- CPU: 12 threads detected by thread pool
- Parallelization threshold: 100 items
- Dedup allocator enabled

## Benchmark Results

### Heavy Workload (100 sin/cos operations per item)

| Items  | MAP Serial | MAP Parallel | MAP Speedup | FILTER Serial | FILTER Parallel | FILTER Speedup |
|--------|------------|--------------|-------------|---------------|-----------------|----------------|
| 1,000  | 2.8 ms     | 0.8 ms       | **3.50x**   | 1.2 ms        | 0.6 ms          | **2.04x**      |
| 5,000  | 10.0 ms    | 2.0 ms       | **5.02x**   | 6.6 ms        | 1.2 ms          | **5.36x**      |
| 10,000 | 21.7 ms    | 3.0 ms       | **7.22x**   | 13.8 ms       | 2.0 ms          | **6.74x**      |
| 50,000 | 102.9 ms   | 14.2 ms      | **7.23x**   | 101.1 ms      | 8.8 ms          | **11.45x**     |

### Light Workload (trivial integer operations)

| Items     | MAP Serial | MAP Parallel | MAP Speedup | FILTER Serial | FILTER Parallel | FILTER Speedup |
|-----------|------------|--------------|-------------|---------------|-----------------|----------------|
| 100,000   | 0.9 ms     | 1.2 ms       | **0.74x**   | 0.7 ms        | 2.8 ms          | **0.24x**      |
| 1,000,000 | 7.1 ms     | 7.7 ms       | **0.92x**   | 7.6 ms        | 22.2 ms         | **0.34x**      |

## Analysis

**Heavy Workload:**
- Parallel map achieves 3.5x - 7.2x speedup
- Parallel filter achieves 2.0x - 11.5x speedup
- Speedup scales well with data size
- Best speedup at 50,000 items: **11.45x for filter**

**Light Workload:**
- Parallel versions are **slower** due to thread pool overhead
- Thread creation/synchronization dominates trivial operations
- Serial execution is better for simple operations

**Conclusion:**
- Parallel operations shine with CPU-intensive work
- For trivial operations (< 1µs per item), stay sequential
- The 100-item threshold is appropriate but may need tuning based on operation complexity
- Work-stealing delivers excellent scaling on 12-core CPU

## Recommendations

**Use parallel operations when:**
- Per-item processing takes > 10µs
- Collection has > 1,000 items
- Operation is CPU-bound (not memory-bound)

**Use serial operations when:**
- Per-item processing is trivial (< 1µs)
- Collection has < 1,000 items
- Order preservation is required (filter only)
