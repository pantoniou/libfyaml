# libfyaml Python Bindings - Benchmark Results

## Test Environment

- **File**: AtomicCards-2-cleaned-small.yaml (6.35 MB)
- **Platform**: Linux
- **Python**: 3.x
- **Iterations**: 3 runs averaged

## Summary: libfyaml vs PyYAML (C/libyaml)

| Library | Configuration | Time | Memory | Speedup |
|---------|--------------|------|--------|---------|
| **libfyaml** | FyGeneric (lazy) | **126 ms** | **34 MB** | **20.6x faster** |
| **libfyaml** | Python (full convert) | **315 ms** | **37 MB** | **8.3x faster** |
| PyYAML | C/libyaml | 2.6 s | 110 MB | baseline |
| PyYAML | Pure Python | 32.4 s | 147 MB | 12.5x slower |

**Key finding**: Even when forcing full conversion to Python objects (worst case), libfyaml is **8.3x faster** with **3x less memory** than PyYAML (C/libyaml).

## Cache-Friendly Conversion

Why is conversion only 2.5x slower than parsing (instead of 10x+)?

1. **Inline storage**: Small values stored directly (no heap allocation)
2. **Sequential layout**: Arrays stored contiguously (excellent cache locality)
3. **Type-tagged pointers**: No indirection for type checks
4. **Linear traversal**: Predictable access pattern for CPU prefetcher

**Measured impact**: Conversion takes 301ms for 6.35MB (476 MB/s throughput)
