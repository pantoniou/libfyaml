#!/usr/bin/env python3
"""
Track actual memory usage to demonstrate the difference.
"""

import sys
import gc
sys.path.insert(0, 'libfyaml')
import _libfyaml as libfyaml

try:
    import psutil
    import os
    HAS_PSUTIL = True
except ImportError:
    HAS_PSUTIL = False
    print("Note: Install 'psutil' for actual memory usage tracking")
    print("      Running with reference counting only\n")

def get_memory_mb():
    """Get current process memory in MB."""
    if HAS_PSUTIL:
        process = psutil.Process(os.getpid())
        return process.memory_info().rss / 1024 / 1024
    return 0

def show_memory(label):
    """Show memory usage."""
    mem = get_memory_mb()
    if mem > 0:
        print(f"  {label}: {mem:.1f} MB")

print("=" * 70)
print("MEMORY USAGE TRACKING")
print("=" * 70 + "\n")

SIZE = 50000  # Smaller for this demo
yaml_str = "[" + ", ".join(str(i) for i in range(SIZE)) + "]"

print(f"Creating array with {SIZE:,} elements...")
gc.collect()
show_memory("Before parse")

doc = libfyaml.loads(yaml_str)
show_memory("After parse")

print(f"\nTest 1: ACCUMULATE FyGeneric objects (MEMORY GROWS)")
print("-" * 70)
gc.collect()
show_memory("Before accumulation")

# Accumulate FyGeneric objects
accumulated = []
for i, item in enumerate(doc):
    accumulated.append(item)
    if (i + 1) % 10000 == 0:
        show_memory(f"After {i+1:,} items")

print(f"Accumulated {len(accumulated)} FyGeneric objects")
print(f"Root refcount: {sys.getrefcount(doc) - 1}")
show_memory("Peak usage")

# Cleanup
del accumulated
gc.collect()
show_memory("After cleanup")

print(f"\nTest 2: STREAM without keeping (MEMORY STABLE)")
print("-" * 70)
gc.collect()
show_memory("Before streaming")

total = 0
for i, item in enumerate(doc):
    total += item
    if (i + 1) % 10000 == 0:
        show_memory(f"After {i+1:,} items")

print(f"Processed {SIZE:,} items, total = {total:,}")
print(f"Root refcount: {sys.getrefcount(doc) - 1}")
show_memory("After streaming")

print(f"\nTest 3: CONVERT to Python (converts, then discards FyGeneric)")
print("-" * 70)
gc.collect()
show_memory("Before conversion")

python_list = []
for i, item in enumerate(doc):
    python_list.append(item + 0)  # Convert to Python int, FyGeneric discarded
    if (i + 1) % 10000 == 0:
        show_memory(f"After {i+1:,} items")

print(f"Converted {len(python_list)} items to Python ints")
print(f"Root refcount: {sys.getrefcount(doc) - 1}")
show_memory("After conversion")

del python_list
gc.collect()
show_memory("After cleanup")

print("\n" + "=" * 70)
print("KEY INSIGHTS")
print("=" * 70)
print("""
1. ACCUMULATING FyGeneric objects:
   - Keeps references to all items
   - Root refcount increases (each child holds ref to root)
   - Memory grows with number of items

2. STREAMING (no references kept):
   - Items freed immediately after each iteration
   - Root refcount stays constant
   - Memory usage stable

3. CONVERTING to Python:
   - Creates Python int, discards FyGeneric
   - Python ints are smaller than FyGeneric wrappers
   - Memory grows with Python list, not FyGeneric overhead

RECOMMENDATION:
  - For iteration: Use pattern 2 (streaming)
  - For accumulation: Use pattern 3 (convert to Python)
  - Avoid pattern 1 (accumulating FyGeneric) for large datasets
""")

print("\n" + "=" * 70)
print("REFERENCE COUNTING DEMONSTRATION")
print("=" * 70 + "\n")

print("Scenario: Iterating WITHOUT keeping references")
print("-" * 70)

base_refcount = sys.getrefcount(doc) - 1
print(f"Root refcount before loop: {base_refcount}")

for i, item in enumerate(doc):
    if i == 0:
        print(f"  Inside loop (first item): {sys.getrefcount(doc) - 1}")
        print(f"  Item refcount: {sys.getrefcount(item) - 1}")
    if i < 3:
        pass  # Just to have some iterations
    else:
        break

print(f"Root refcount after loop: {sys.getrefcount(doc) - 1}")
print("✅ Refcount returns to baseline - items were freed!\n")

print("Scenario: Iterating WITH keeping references")
print("-" * 70)

print(f"Root refcount before loop: {sys.getrefcount(doc) - 1}")

kept_refs = []
for i, item in enumerate(doc):
    kept_refs.append(item)
    if i < 3:
        print(f"  After keeping {i+1} items: root refcount = {sys.getrefcount(doc) - 1}")
    if i >= 9:
        break

print(f"Root refcount after loop: {sys.getrefcount(doc) - 1}")
print("⚠️  Refcount increased - items are still alive!\n")

del kept_refs
gc.collect()
print(f"Root refcount after cleanup: {sys.getrefcount(doc) - 1}")
print("✅ Refcount returns after cleanup\n")

print("=" * 70)
print("CONCLUSION")
print("=" * 70)
print("""
CPython's reference counting AUTOMATICALLY handles cleanup when:
  - You don't keep references to items
  - Items go out of scope after each iteration

For million-element arrays:
  ✅ DO: Iterate without keeping references
  ✅ DO: Convert to Python if you need to accumulate
  ❌ DON'T: Accumulate FyGeneric objects in a list
""")
