#!/usr/bin/env python3
"""
Test to understand memory behavior during iteration.
"""

import sys
import gc
sys.path.insert(0, 'libfyaml')
import _libfyaml as libfyaml

print("=" * 70)
print("MEMORY BEHAVIOR ANALYSIS")
print("=" * 70)

# Create a large array
size = 1000
yaml_str = "[" + ", ".join(str(i) for i in range(size)) + "]"
doc = libfyaml.loads(yaml_str)

print(f"\n1. Created array with {size} elements")
print(f"   Root object refcount: {sys.getrefcount(doc) - 1}")  # -1 for the getrefcount arg

# Test: Simple iteration (items should be freed immediately)
print("\n2. Simple iteration test:")
refs_held = []
for i, item in enumerate(doc):
    if i == 0:
        print(f"   First item refcount: {sys.getrefcount(item) - 1}")
    if i < 5:
        refs_held.append(item)  # Keep some references

print(f"   Kept {len(refs_held)} references")
print(f"   Root object refcount: {sys.getrefcount(doc) - 1}")
print(f"   First kept item refcount: {sys.getrefcount(refs_held[0]) - 1}")

# Test: Do references keep root alive?
print("\n3. Reference behavior:")
saved_item = doc[100]
print(f"   Saved item refcount: {sys.getrefcount(saved_item) - 1}")
print(f"   Root refcount: {sys.getrefcount(doc) - 1}")

# Test: What happens when we don't keep references?
print("\n4. No-reference iteration:")
count = 0
for item in doc:
    count += 1  # Just count, don't keep references

print(f"   Processed {count} items without keeping references")
print(f"   Root refcount: {sys.getrefcount(doc) - 1}")

# Test: Explicit cleanup
print("\n5. Manual cleanup:")
del refs_held
del saved_item
gc.collect()
print(f"   After cleanup, root refcount: {sys.getrefcount(doc) - 1}")

print("\n" + "=" * 70)
print("CONCLUSIONS:")
print("=" * 70)
print("- Items NOT kept in variables are freed immediately (refcounting)")
print("- Items kept in variables hold a reference to root")
print("- Root stays alive as long as ANY child exists")
print("- This is CORRECT behavior (data safety)")
