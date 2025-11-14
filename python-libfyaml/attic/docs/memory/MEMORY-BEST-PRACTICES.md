# Memory Best Practices - Quick Reference

## The Golden Rule

**Don't keep references to FyGeneric objects when processing large datasets.**

CPython's reference counting will automatically free objects when they go out of scope.

## Quick Decision Tree

```
Processing large array?
│
├─ Need to accumulate items?
│  │
│  ├─ YES → Convert to Python immediately
│  │         items = [x + 0 for x in array]  # Python ints
│  │
│  └─ NO  → Use streaming/reduction
│            total = sum(x for x in array)
│
└─ Just iterating?
   │
   └─ Don't keep references → Automatic cleanup!
      for item in array:
          process(item)  # 'item' freed after each iteration
```

## Patterns

### ✅ Pattern 1: Reduction/Aggregation (BEST)

```python
# Sum, max, min, etc. - no references kept
total = sum(item for item in huge_array)
maximum = max(item + 0 for item in huge_array)
average = sum(item for item in huge_array) / len(huge_array)

# Counted iteration
count = sum(1 for item in huge_array if item > threshold)
```

**Why it works**: Generator expression doesn't keep references.

### ✅ Pattern 2: Convert to Python (for accumulation)

```python
# Convert to Python types immediately
python_ints = [item + 0 for item in huge_array]
python_strs = [str(item) for item in huge_array]

# Then work with Python objects
filtered = [x for x in python_ints if x > 1000]
```

**Why it works**: FyGeneric wrappers discarded, only Python objects kept.

### ✅ Pattern 3: Streaming with Generators

```python
def process_stream(array):
    for item in array:
        yield item * 2  # FyGeneric freed after yield

# Only keep processed results
results = list(process_stream(huge_array))
```

**Why it works**: Only one FyGeneric alive at a time.

### ✅ Pattern 4: Batch Processing

```python
def process_batches(array, batch_size=10000):
    batch = []
    for i, item in enumerate(array):
        batch.append(item + 0)  # Convert to Python

        if len(batch) >= batch_size:
            process_batch(batch)  # Work with Python objects
            batch = []  # Discard batch

    if batch:  # Process remaining
        process_batch(batch)
```

**Why it works**: Controls memory by processing in chunks.

### ❌ Anti-Pattern: Accumulating FyGeneric

```python
# DON'T DO THIS for large datasets!
items = [item for item in huge_array]  # Keeps all FyGeneric objects
items = list(huge_array)                # Same problem

# This keeps 1 million FyGeneric wrappers in memory!
```

**Why it's bad**: Each FyGeneric holds a reference to root, root refcount → 1M+.

## Real-World Examples

### Example 1: Filter and Transform

```python
# GOOD: Convert while filtering
adults = [
    {'name': str(u['name']), 'age': u['age'] + 0}  # Convert to Python
    for u in users
    if u['age'] >= 18
]

# BAD: Keep FyGeneric objects
adults = [u for u in users if u['age'] >= 18]  # FyGeneric objects!
```

### Example 2: Statistics

```python
# GOOD: Streaming aggregation
total_salary = sum(emp['salary'] for emp in employees)
avg_salary = total_salary / sum(1 for _ in employees)
max_salary = max(emp['salary'] + 0 for emp in employees)

# BAD: Converting entire structure
py_employees = employees.to_python()  # Full conversion!
total_salary = sum(e['salary'] for e in py_employees)
```

### Example 3: Building Results

```python
# GOOD: Extract Python values as you go
results = []
for item in huge_array:
    if meets_criteria(item):
        results.append({
            'id': item['id'] + 0,        # Python int
            'name': str(item['name']),   # Python str
            'value': item['value'] + 0.0 # Python float
        })
# 'results' contains Python dicts, not FyGeneric

# BAD: Keep FyGeneric objects
results = [item for item in huge_array if meets_criteria(item)]
# Then convert later - wasted memory!
py_results = [x.to_python() for x in results]
```

## Memory Profiling

### Check Reference Counts

```python
import sys

doc = libfyaml.loads(yaml_str)
print(f"Initial refcount: {sys.getrefcount(doc) - 1}")  # Should be 1-2

# After processing
print(f"After refcount: {sys.getrefcount(doc) - 1}")

# If refcount is huge (thousands), you're keeping references!
```

### Expected Refcounts

| Scenario | Root Refcount | Meaning |
|----------|---------------|---------|
| 1-2 | Normal | No/few references kept |
| 100s | Caution | Keeping some items |
| 1000s+ | Problem | Accumulating FyGeneric objects |

## When to Use Each Approach

### Use Streaming (Pattern 1, 3)
- ✅ Computing statistics (sum, avg, max, min)
- ✅ Filtering with immediate processing
- ✅ Counting/grouping operations
- ✅ Single-pass processing

### Use Conversion (Pattern 2, 4)
- ✅ Need to keep results for later use
- ✅ Building new data structures
- ✅ Multiple passes over data
- ✅ Interfacing with code expecting Python objects

### Never Accumulate FyGeneric
- ❌ Large list comprehensions of FyGeneric
- ❌ Appending FyGeneric to lists
- ❌ Storing FyGeneric in dicts/sets

## Garbage Collection

### Usually Not Needed

CPython's reference counting handles most cases automatically.

### When to Consider GC

For **extreme** cases (100M+ items):

```python
import gc

# Disable GC during bulk processing (faster)
gc.disable()
try:
    for item in huge_array:
        process(item)
finally:
    gc.enable()
    gc.collect()  # Clean up at end

# Or hint to GC periodically
for i, item in enumerate(huge_array):
    process(item)
    if i % 100000 == 0:
        gc.collect(0)  # Quick young-generation collect
```

**Usually not needed** - reference counting is sufficient.

## Testing Your Code

### Simple Test

```python
import sys
doc = libfyaml.loads(big_yaml)

before_refs = sys.getrefcount(doc) - 1
your_processing_function(doc)
after_refs = sys.getrefcount(doc) - 1

print(f"Refcount: before={before_refs}, after={after_refs}")

# If after_refs >> before_refs, you're accumulating!
```

### Memory Test

```python
try:
    import psutil, os
    process = psutil.Process(os.getpid())

    before_mb = process.memory_info().rss / 1024 / 1024
    your_processing_function(doc)
    after_mb = process.memory_info().rss / 1024 / 1024

    print(f"Memory: {before_mb:.1f} MB → {after_mb:.1f} MB")
except ImportError:
    print("Install 'psutil' for memory tracking")
```

## Summary

| ✅ DO | ❌ DON'T |
|-------|----------|
| Iterate without keeping refs | Accumulate FyGeneric in lists |
| Convert to Python when accumulating | Keep FyGeneric for later use |
| Use generators for streaming | List comprehensions of FyGeneric |
| Trust reference counting | Worry unless processing 100M+ items |

**Bottom line**: For 99% of use cases, just don't keep references to FyGeneric objects and memory will be managed automatically!
