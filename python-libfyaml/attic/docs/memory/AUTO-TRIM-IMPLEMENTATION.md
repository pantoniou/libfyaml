# Auto-Trim Implementation

## Summary

Added `trim` parameter to `loads()` that **defaults to True**, automatically trimming allocator arena after parsing to minimize memory usage.

## API Changes

### Before

```python
# Had to manually call trim() after loading
data = libfyaml.loads(content)
data.trim()  # Manual step required
```

### After

```python
# Auto-trims by default!
data = libfyaml.loads(content)  # Already trimmed!

# Can disable if needed
data = libfyaml.loads(content, trim=False)  # Keep arena over-allocated
```

## Complete API Signature

```python
libfyaml.loads(s, mode='yaml', dedup=True, trim=True)
```

**Parameters**:
- `s` (str): YAML/JSON string to parse
- `mode` (str): Parse mode - `'yaml'` (default) or `'json'`
- `dedup` (bool): Enable deduplication allocator (default `True`)
- `trim` (bool): Auto-trim arena after parsing (default `True`)

**Returns**: FyGeneric object representing the parsed data

## Memory Impact

### Test Results (AtomicCards-2-cleaned-small.yaml, 6.35 MB)

| Configuration | Memory | Ratio |
|--------------|--------|-------|
| Default (trim=True) | 14.21 MB | 2.24x ✅ |
| trim=False | 25.98 MB | 4.09x |
| Savings | **11.77 MB** | **45.3%** |

### Comparison with PyYAML

**libfyaml with auto-trim**:
```
Memory: 14.21 MB (2.24x file size)
```

**PyYAML (C/libyaml)**:
```
Memory: 110.00 MB (17.3x file size)
```

**Result**: libfyaml uses **7.7x less memory** than PyYAML!

## Use Cases

### Default Behavior (Recommended)

```python
import libfyaml

# Just parse - automatically optimized!
data = libfyaml.loads(yaml_string)

# Access data normally
config = data['config']
users = data['users']
```

**Result**: 14.21 MB (2.24x file size), 45.3% memory savings

### Disable Auto-Trim (If Needed)

```python
# Keep arena over-allocated for future allocations
data = libfyaml.loads(yaml_string, trim=False)

# Manually trim later if desired
data.trim()
```

**Use case**: If you plan to modify or extend the data structure after parsing.

### Fastest Parsing

```python
# Disable dedup for ~5% faster parsing
data = libfyaml.loads(yaml_string, dedup=False)
# Still auto-trims by default
```

**Result**: 111.65 ms (4.4% faster), 14.21 MB

### Maximum Performance + Memory

```python
# Disable dedup, disable trim (not recommended)
data = libfyaml.loads(yaml_string, dedup=False, trim=False)
```

**Result**: 111.00 ms (fastest), 25.98 MB (higher memory)

**Note**: Only use this if you have a specific reason to keep arena over-allocated.

## Implementation Details

### C Code Changes

**Modified**: `libfyaml/_libfyaml_minimal.c`

**Added parameter**:
```c
int trim = 1;  /* Default to True */
static char *kwlist[] = {"s", "mode", "dedup", "trim", NULL};
```

**Auto-trim after parsing**:
```c
/* Auto-trim if requested (default behavior) */
if (trim && gb && gb->allocator) {
    fy_allocator_trim_tag(gb->allocator, gb->alloc_tag);
}
```

### How It Works

1. Parse YAML/JSON into FyGeneric structure
2. Arena is over-allocated (grown by 2x: 4→8→16→32 MB)
3. If `trim=True` (default), call `fy_allocator_trim_tag()`
4. Arena is shrunk via `mremap()` to actual usage (page-aligned)
5. Return trimmed FyGeneric object

**Time cost**: 0.01 ms (essentially free!)

## Backward Compatibility

✅ **Fully backward compatible**

Old code continues to work without changes:
```python
# Old code - still works
data = libfyaml.loads(yaml_string)
# Now automatically optimized!
```

Manual trim() calls are still supported (but redundant with default behavior):
```python
# Still works, but trim() is now a no-op if already trimmed
data = libfyaml.loads(yaml_string)
data.trim()  # No harm, but not needed anymore
```

## Performance Characteristics

### Parse Time Impact

**Auto-trim overhead**: 0.01 ms (~0.01%)

| Configuration | Parse Time | Trim Time | Total |
|--------------|-----------|-----------|-------|
| trim=False | 116.78 ms | - | 116.78 ms |
| trim=True (default) | 116.78 ms | 0.01 ms | **116.79 ms** |

**Conclusion**: Auto-trim is essentially free (0.01% overhead).

### Memory Impact

**Without trim**:
```
Arena capacity:  ~32 MB (over-allocated by 2x)
Actually used:   ~10-15 MB
Wasted:          ~17 MB (67%)
Total memory:    25.98 MB
```

**With trim (default)**:
```
Arena capacity:  ~14 MB (page-aligned to usage)
Actually used:   ~14 MB
Wasted:          ~0 MB
Total memory:    14.21 MB
SAVINGS:         11.77 MB (45.3%)
```

## Recommendations

### For Most Users

**Just use the defaults**:
```python
data = libfyaml.loads(yaml_string)
```

**Result**:
- ✅ Optimal memory usage (2.24x file size)
- ✅ Fast parsing
- ✅ Safe defaults (dedup enabled)
- ✅ No manual trim() needed

### For Performance-Critical Code

**Disable dedup if content is unique**:
```python
data = libfyaml.loads(yaml_string, dedup=False)
```

**Result**:
- ✅ 5% faster parsing
- ✅ Same memory usage (still auto-trims)
- ⚠️  No protection against repeated content

### For Memory-Constrained Environments

**Defaults are already optimal**:
```python
data = libfyaml.loads(yaml_string)
```

No need to change anything - auto-trim ensures minimal memory usage.

### When NOT to Auto-Trim

**Only disable trim if**:
- You plan to add more data to the structure after parsing
- You're benchmarking allocator behavior
- You have specific arena management requirements

```python
data = libfyaml.loads(yaml_string, trim=False)
# ... extend data structure ...
data.trim()  # Trim when done
```

## Migration Guide

### If You Were Manually Calling trim()

**Old pattern**:
```python
data = libfyaml.loads(content)
data.trim()  # Manual optimization
```

**New pattern** (recommended):
```python
data = libfyaml.loads(content)
# Auto-trimmed! No manual call needed
```

**Or keep old code** (still works):
```python
data = libfyaml.loads(content)
data.trim()  # Redundant but harmless
```

### If You Need Non-Trimmed Data

**Old behavior**:
```python
data = libfyaml.loads(content)
# Arena over-allocated by default
```

**New equivalent**:
```python
data = libfyaml.loads(content, trim=False)
# Explicitly disable auto-trim
```

## Testing

### Verification Test

Run `test_auto_trim.py` to verify auto-trim behavior:

```bash
$ python3 test_auto_trim.py
```

**Expected output**:
```
✅ SUCCESS! Auto-trim is working by default!
   Saved 11.77 MB (45.3%)
✅ Default matches trim=True
```

### Benchmark

Run `benchmark_dedup_configurations.py` to see all configuration permutations:

```bash
$ python3 benchmark_dedup_configurations.py
```

## Documentation Updates

**Updated files**:
- `MEMORY-INVESTIGATION-SUMMARY.md` - Added implementation section
- `DEDUP-BENCHMARK-RESULTS.md` - Benchmark results with trim
- `AUTO-TRIM-IMPLEMENTATION.md` - This document
- `test_auto_trim.py` - Verification test

## Future Work

**Potential enhancements**:
1. Add `allocator` parameter to choose allocator type (`'auto'`, `'mremap'`, `'malloc'`, etc.)
2. Add `mmap` mode for zero-copy file parsing
3. Expose allocator statistics for debugging

## Conclusion

Auto-trim is now enabled by default, providing:
- **45.3% memory savings** (11.77 MB on test file)
- **Essentially free** (0.01 ms overhead)
- **Backward compatible** (old code still works)
- **7.7x less memory** than PyYAML

**Recommendation**: Use the defaults for optimal performance and memory usage. No manual trim() calls needed!
