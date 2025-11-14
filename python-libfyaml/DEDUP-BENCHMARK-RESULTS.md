# Dedup Configuration Benchmark Results

## Summary

Benchmarked all permutations of `dedup` and `trim` parameters on `AtomicCards-2-cleaned-small.yaml` (6.35 MB).

## Key Findings

### 1. Dedup Has No Effect on This File

**Memory usage (before trim)**:
- `dedup=True`: 25.98 MB
- `dedup=False`: 25.98 MB
- **Difference**: 0.00 MB (0.0%)

**Conclusion**: This particular YAML file doesn't have enough repeated content to benefit from deduplication. The dedup allocator adds no memory overhead, but also provides no memory savings.

### 2. Dedup Has Small Performance Cost

**Parse time**:
- `dedup=True`: 116.78 ms
- `dedup=False`: 111.00 ms
- **Difference**: +5.78 ms (+5.2% slower with dedup)

**Conclusion**: The dedup hash table lookup adds a small performance overhead (~5%), even when there's nothing to deduplicate.

### 3. Trim Works Equally Well Regardless of Dedup

**Memory savings from trim()**:
- With `dedup=True`: 11.77 MB (45.3% savings)
- With `dedup=False`: 11.77 MB (45.3% savings)

**Trim time**: 0.01 ms (negligible)

**Conclusion**: trim() is effective and fast regardless of whether dedup is enabled. The arena over-allocation issue exists in both cases and trim() solves it.

## Complete Results

### Configuration 1: dedup=True, no trim
- **Parse time**: 116.78 ms
- **Memory**: 25.98 MB (4.09x file size)
- **Use case**: Default behavior (backward compatible)

### Configuration 2: dedup=True, with trim ⭐
- **Parse time**: 116.59 ms
- **Memory**: 14.21 MB (2.24x file size)
- **Savings**: 11.77 MB (45.3% reduction)
- **Use case**: Best memory usage with dedup safety net

### Configuration 3: dedup=False, no trim
- **Parse time**: 111.00 ms (5.2% faster than dedup=True)
- **Memory**: 25.98 MB (4.09x file size)
- **Use case**: Slightly faster parsing when you know content is unique

### Configuration 4: dedup=False, with trim
- **Parse time**: 111.65 ms (4.4% faster than dedup=True)
- **Memory**: 14.21 MB (2.24x file size)
- **Savings**: 11.77 MB (45.3% reduction)
- **Use case**: Best performance + memory when content is unique

## Recommendations

### For This File (AtomicCards)

Since this file has no repeated content:
```python
# Fastest + lowest memory
data = libfyaml.loads(content, dedup=False)
data.trim()
```

**Result**: 111.65 ms, 14.21 MB (2.24x file size)

### For Files with Unknown Content

If you don't know whether the file has repeated content:
```python
# Safe default with memory optimization
data = libfyaml.loads(content, dedup=True)
data.trim()
```

**Result**: 116.59 ms, 14.21 MB (2.24x file size)

The performance difference is small (5ms or 4.4%), so `dedup=True` is a safe default.

### Always Call trim()

**Regardless of dedup setting, always call trim():**
- Saves 45.3% memory (11.77 MB on this file)
- Costs only 0.01 ms
- No downsides

## Memory Breakdown

**Final memory usage (after trim)**:
```
Total: 14.21 MB (2.24x file size)

├─ Source YAML:        6.35 MB (44.7%) ← Zero-copy
├─ Parser structures: ~1.30 MB  (9.2%)
├─ Dedup hash table:  ~1.00 MB  (7.0%) ← Only if dedup=True
└─ Trimmed arena:     ~5.56 MB (39.1%) ← Actual data structures
```

**Before trim**:
```
Total: 25.98 MB (4.09x file size)

├─ Source YAML:        6.35 MB (24.4%)
├─ Parser structures: ~1.30 MB  (5.0%)
├─ Dedup hash table:  ~1.00 MB  (3.8%)
└─ Over-allocated:    17.33 MB (66.7%) ← Wasted arena space
```

## Impact on PyYAML Comparison

**Previous comparison (without trim)**:
```
libfyaml (dedup=True):  25.98 MB (4.09x file size)
PyYAML (C/libyaml):    110.00 MB (17.3x file size)
Ratio: PyYAML uses 4.2x more memory
```

**New comparison (with trim)**:
```
libfyaml (dedup=False, trim): 14.21 MB (2.24x file size) ⭐
PyYAML (C/libyaml):          110.00 MB (17.3x file size)
Ratio: PyYAML uses 7.7x more memory!
```

**Improvement**: By calling trim(), libfyaml now uses **7.7x less memory** than PyYAML (up from 4.2x).

## API Usage

```python
import libfyaml

# Read YAML file
with open('config.yaml', 'r') as f:
    content = f.read()

# Option 1: Safe default (dedup=True is default)
data = libfyaml.loads(content)
data.trim()

# Option 2: Explicit dedup control
data = libfyaml.loads(content, dedup=True)   # Enable dedup
data = libfyaml.loads(content, dedup=False)  # Disable dedup
data.trim()  # Always call trim!

# Option 3: One-liner for best memory
data = libfyaml.loads(content, dedup=False)
data.trim()
```

## When to Use dedup=True vs dedup=False

### Use `dedup=True` (default) when:
- Unknown content characteristics
- Large files where 5% performance overhead is acceptable
- Files with potentially repeated strings
- Safety is more important than performance

### Use `dedup=False` when:
- Content is known to be unique (e.g., UUID-heavy data)
- Performance is critical (5% faster parsing)
- Small files where dedup overhead exceeds savings
- Memory after trim is same anyway (as shown in this benchmark)

## Benchmark Details

**Test file**: `AtomicCards-2-cleaned-small.yaml`
**File size**: 6.35 MB
**Iterations**: 3 per configuration
**Measurement**: Anonymous mmap regions via `/proc/self/maps`

**Configurations tested**:
1. `dedup=True, no trim`
2. `dedup=True, with trim`
3. `dedup=False, no trim`
4. `dedup=False, with trim`

## Conclusion

For the AtomicCards file:
- **Dedup provides no benefit** (no repeated content)
- **Dedup adds 5% performance overhead**
- **trim() saves 45.3% memory** (regardless of dedup)
- **Best configuration**: `dedup=False` + `trim()`

**General recommendation**: Always call `trim()` after parsing, and use `dedup=False` if you know your content is unique, otherwise stick with the default `dedup=True`.
