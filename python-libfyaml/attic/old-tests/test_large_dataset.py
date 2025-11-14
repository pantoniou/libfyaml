#!/usr/bin/env python3
"""
Demonstrate efficient large dataset processing.
"""

import sys
import gc
import time
sys.path.insert(0, 'libfyaml')
import _libfyaml as libfyaml

def benchmark_pattern(name, func, doc, size):
    """Benchmark a processing pattern."""
    gc.collect()
    start_refs = sys.getrefcount(doc) - 1

    start = time.time()
    result = func(doc, size)
    elapsed = time.time() - start

    end_refs = sys.getrefcount(doc) - 1
    gc.collect()
    final_refs = sys.getrefcount(doc) - 1

    print(f"{name}:")
    print(f"  Time: {elapsed:.3f}s")
    print(f"  Result: {result}")
    print(f"  Refs: start={start_refs}, end={end_refs}, after_gc={final_refs}")
    return result

print("=" * 70)
print("LARGE DATASET PROCESSING - MEMORY EFFICIENCY DEMO")
print("=" * 70)

# Create test data
SIZE = 100000
print(f"\nCreating array with {SIZE:,} elements...")
yaml_str = "[" + ", ".join(str(i) for i in range(SIZE)) + "]"
doc = libfyaml.loads(yaml_str)
print(f"Array created. Root refcount: {sys.getrefcount(doc) - 1}")

# Pattern 1: BAD - Accumulate FyGeneric objects
def pattern_bad(doc, size):
    items = []
    for item in doc:
        items.append(item)  # Keeps FyGeneric
    total = sum(x + 0 for x in items)  # Convert when using
    return total

# Pattern 2: GOOD - Don't keep references
def pattern_good_stream(doc, size):
    total = 0
    for item in doc:
        total += item  # Uses item, then freed
    return total

# Pattern 3: GOOD - Convert to Python immediately
def pattern_good_convert(doc, size):
    items = []
    for item in doc:
        items.append(item + 0)  # Convert to Python int
    return sum(items)

# Pattern 4: BEST - Generator/streaming
def pattern_best_generator(doc, size):
    return sum(item + 0 for item in doc)

print("\n" + "=" * 70)
print("BENCHMARKING PATTERNS")
print("=" * 70 + "\n")

r1 = benchmark_pattern("1. BAD: Accumulate FyGeneric", pattern_bad, doc, SIZE)
r2 = benchmark_pattern("2. GOOD: Stream without keeping", pattern_good_stream, doc, SIZE)
r3 = benchmark_pattern("3. GOOD: Convert to Python", pattern_good_convert, doc, SIZE)
r4 = benchmark_pattern("4. BEST: Generator", pattern_best_generator, doc, SIZE)

print("\n" + "=" * 70)
print("CONCLUSIONS")
print("=" * 70)
print(f"\nAll methods got same result: {r1 == r2 == r3 == r4}")
print("\nPattern 1 (BAD): Accumulates FyGeneric objects, high ref count")
print("Pattern 2-4 (GOOD): Low/stable ref count, immediate cleanup")
print("\nFor million-element arrays: Use patterns 2-4!")
print("  - Pattern 2: Simple iteration (best for reductions)")
print("  - Pattern 3: Need Python list? Convert as you go")
print("  - Pattern 4: Pythonic generator expressions")

# Demonstrate batch processing
print("\n" + "=" * 70)
print("BATCH PROCESSING DEMO")
print("=" * 70 + "\n")

def process_in_batches(doc, batch_size=10000):
    """Process large array in batches."""
    total = 0
    batch_count = 0
    current_batch = []

    for i, item in enumerate(doc):
        current_batch.append(item + 0)  # Convert to Python

        if len(current_batch) >= batch_size:
            # Process batch
            total += sum(current_batch)
            batch_count += 1

            # Clear batch (frees Python ints)
            current_batch = []

            if batch_count <= 3:
                print(f"  Processed batch {batch_count} ({batch_size:,} items)")

    # Process remaining
    if current_batch:
        total += sum(current_batch)
        batch_count += 1
        print(f"  Processed final batch {batch_count} ({len(current_batch):,} items)")

    return total, batch_count

result, batches = process_in_batches(doc, batch_size=10000)
print(f"\nTotal: {result:,} in {batches} batches")
print(f"Final root refcount: {sys.getrefcount(doc) - 1}")

print("\n" + "=" * 70)
print("✅ MEMORY EFFICIENT PATTERNS DEMONSTRATED!")
print("=" * 70)
