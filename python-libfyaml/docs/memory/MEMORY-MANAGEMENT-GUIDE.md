# Memory Management Guide for Large Datasets

## Understanding the Memory Model

### How FyGeneric Objects Work

1. **Root object**: Owns the `fy_generic_builder` (the actual YAML data)
2. **Child objects**: Reference the root, contain a `fy_generic` value (just a tagged pointer)
3. **Reference counting**: Each child holds a reference to root to keep data alive

### Memory Characteristics

- **FyGeneric wrapper**: ~48 bytes (small!)
- **Actual data**: Stored once in the builder (not duplicated)
- **Refcounting**: CPython frees objects immediately when refcount reaches 0

## The Good News: Automatic Cleanup Works!

For most use cases, you don't need to do anything special:

```python
# This is EFFICIENT - items freed immediately after each iteration
for item in huge_array:
    process(item)
    # 'item' refcount drops to 0 here, freed immediately!
```

## When Memory Accumulates

Memory only accumulates if you **keep references**:

```python
# BAD: Keeps all items in memory
all_items = [item for item in huge_array]  # Don't do this!

# BAD: Accumulates items
items_list = []
for item in huge_array:
    items_list.append(item)  # Keeps references
```

## Solutions for Large Datasets

### 1. Batch Processing with `to_python()`

Convert in chunks to avoid keeping FyGeneric wrappers:

```python
import sys
sys.path.insert(0, 'libfyaml')
import _libfyaml as libfyaml

def process_large_array_batched(doc, batch_size=1000):
    """
    Process large array in batches, converting to Python objects.
    FyGeneric wrappers freed after each batch.
    """
    # Get total length
    count = 0
    for _ in doc:
        count += 1

    # Process in batches
    for start in range(0, count, batch_size):
        # Convert batch to Python (creates native list/ints)
        batch_items = []
        for i, item in enumerate(doc):
            if i < start:
                continue
            if i >= start + batch_size:
                break
            batch_items.append(item + 0)  # Convert to Python int

        # Process the batch (uses Python ints, not FyGeneric)
        for value in batch_items:
            process(value)

        # batch_items goes out of scope, freed
        del batch_items

# Usage
huge_doc = libfyaml.loads("[" + ", ".join(str(i) for i in range(1000000)) + "]")
process_large_array_batched(huge_doc)
```

### 2. Streaming Pattern (Process-Then-Discard)

Don't accumulate - process and discard immediately:

```python
def stream_filter_map(doc, predicate, transform):
    """
    Generator that filters and transforms without keeping references.
    """
    for item in doc:
        if predicate(item):
            # Transform and yield (caller decides what to keep)
            yield transform(item)
        # 'item' freed here (no reference kept)

# Usage: only keep results, not original items
results = []
for value in stream_filter_map(huge_doc, lambda x: x > 1000, lambda x: x * 2):
    results.append(value)  # Keep processed results, not original FyGeneric
```

### 3. Direct Conversion for Aggregations

For reductions/aggregations, convert values inline:

```python
# GOOD: No FyGeneric objects kept
total = 0
for item in huge_array:
    total += item + 0  # The '+0' converts to Python int, FyGeneric freed

# GOOD: Generator expression doesn't keep references
total = sum(item + 0 for item in huge_array)

# GOOD: Max/min/etc
max_val = max(item + 0 for item in huge_array)
```

### 4. Explicit Scoping with Functions

Use functions to ensure cleanup:

```python
def process_chunk(chunk_doc):
    """Process a chunk and ensure all FyGeneric objects freed on return."""
    result = 0
    for item in chunk_doc:
        result += item * 2  # Arithmetic works, item freed after
    return result
    # All FyGeneric refs go out of scope here

# Process chunks
for i in range(0, 1000000, 10000):
    chunk_result = process_chunk(doc[i:i+10000])  # If slicing supported
    # OR:
    chunk_items = [doc[j] for j in range(i, min(i+10000, len_doc))]
    chunk_result = process_chunk(chunk_items)
    del chunk_items  # Explicit cleanup
```

### 5. Manual GC Hints (Last Resort)

For extremely large datasets, hint to GC:

```python
import gc

def process_huge_dataset(doc):
    batch_count = 0
    for item in doc:
        process(item)

        batch_count += 1
        if batch_count % 10000 == 0:
            # Hint to GC to collect every 10k items
            gc.collect(0)  # Only collect youngest generation (fast)

# Or disable/re-enable GC for performance-critical sections
gc.disable()
try:
    for item in huge_array:
        process(item)
finally:
    gc.enable()
    gc.collect()  # Clean up after
```

### 6. Convert Entire Structure to Python (If You Need It)

If you need to work with the whole dataset as Python objects:

```python
# Convert entire array to Python list (one-time conversion)
python_list = doc.to_python()

# Now work with native Python objects (no FyGeneric overhead)
for item in python_list:
    process(item)  # 'item' is a Python int/str/etc, not FyGeneric

# Trade-off: Uses more memory initially (full conversion),
# but no FyGeneric wrappers, cleaner memory pattern
```

## Best Practices Summary

### ✅ DO: Process Without Keeping References

```python
# Good - immediate cleanup
total = 0
for item in huge_array:
    total += item
    # 'item' freed here

# Good - generator pattern
results = (item * 2 for item in huge_array if item > 0)
for result in results:
    use(result)
```

### ❌ DON'T: Accumulate All Items

```python
# Bad - keeps all items in memory
all_items = list(huge_array)  # Creates million FyGeneric objects!
all_items = [item for item in huge_array]  # Same problem!
```

### ✅ DO: Convert to Python for Accumulation

```python
# Good - convert to Python types if you need to accumulate
all_values = [item + 0 for item in huge_array]  # Python ints, not FyGeneric
all_names = [str(item['name']) for item in users]  # Python strs
```

### ✅ DO: Use Batch Processing

```python
# Good - process in batches
for i in range(0, len(items), 1000):
    batch = [items[j] for j in range(i, min(i+1000, len(items)))]
    process_batch([x + 0 for x in batch])  # Convert to Python
    del batch
```

## Memory Profiling Example

```python
import sys
import gc
import _libfyaml as libfyaml

def profile_memory_usage():
    # Create large array
    size = 100000
    yaml_str = "[" + ", ".join(str(i) for i in range(size)) + "]"
    doc = libfyaml.loads(yaml_str)

    print(f"Root refcount: {sys.getrefcount(doc) - 1}")

    # Pattern 1: Keep all references (BAD)
    print("\nPattern 1: Keep all references")
    refs = []
    for item in doc:
        refs.append(item)
    print(f"  Kept {len(refs)} refs, root refcount: {sys.getrefcount(doc) - 1}")
    del refs
    gc.collect()
    print(f"  After cleanup, root refcount: {sys.getrefcount(doc) - 1}")

    # Pattern 2: Process without keeping (GOOD)
    print("\nPattern 2: Process without keeping")
    total = 0
    for item in doc:
        total += item
    print(f"  Processed {size} items, root refcount: {sys.getrefcount(doc) - 1}")
    print(f"  Total: {total}")

    # Pattern 3: Convert to Python (GOOD for accumulation)
    print("\nPattern 3: Convert to Python")
    python_list = [item + 0 for item in doc]
    print(f"  Created Python list, root refcount: {sys.getrefcount(doc) - 1}")
    print(f"  Python list is native ints, no FyGeneric overhead")
    del python_list

profile_memory_usage()
```

## When to Worry vs Not Worry

### Don't Worry (Automatic Cleanup Works):
- ✅ Simple iteration without keeping references
- ✅ Aggregations (sum, max, min)
- ✅ Filtering and transforming with generators
- ✅ Map/reduce patterns

### Do Optimize (Memory Could Accumulate):
- ⚠️ List comprehensions that keep FyGeneric objects
- ⚠️ Appending items to lists for later processing
- ⚠️ Building large dictionaries from YAML data
- ⚠️ Long-lived references to individual items

### Solution: Convert to Python When Accumulating:
```python
# Instead of:
items = [item for item in doc]  # FyGeneric objects

# Do:
items = [item + 0 for item in doc]  # Python objects (int)
items = [str(item) for item in doc]  # Python objects (str)
```

## Conclusion

**Most code doesn't need special handling** - CPython's reference counting automatically frees FyGeneric objects when they go out of scope.

**For large datasets**, follow these principles:
1. **Don't accumulate FyGeneric objects** - convert to Python if you need to keep them
2. **Use streaming/generator patterns** when possible
3. **Process in batches** for very large datasets
4. **Explicit `del`** when you're done with large references
5. **Manual GC hints** only for extreme cases (100M+ items)

The memory overhead of a FyGeneric wrapper is small (~48 bytes), but for millions of items, converting to native Python objects (when accumulating) is more efficient.
