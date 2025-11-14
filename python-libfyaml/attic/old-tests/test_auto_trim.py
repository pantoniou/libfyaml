#!/usr/bin/env python3
"""
Test that auto-trim works by default.
"""

import sys
import gc
sys.path.insert(0, '.')
import libfyaml


def get_anon_mmap_size():
    """Get total size of anonymous mmap regions > 1MB."""
    try:
        with open('/proc/self/maps', 'r') as f:
            maps = f.read()

        total = 0
        for line in maps.split('\n'):
            if not line:
                continue
            parts = line.split()
            if len(parts) >= 6:
                continue  # Has path
            if len(parts) >= 2 and 'rw' in parts[1]:
                start, end = parts[0].split('-')
                size = int(end, 16) - int(start, 16)
                if size > 1024 * 1024:  # > 1MB
                    total += size
        return total
    except:
        return 0


def format_mb(bytes_val):
    return f"{bytes_val / (1024*1024):.2f} MB"


# Load YAML file
with open('AtomicCards-2-cleaned-small.yaml', 'r') as f:
    content = f.read()

file_size = len(content.encode('utf-8'))

print("="*60)
print("AUTO-TRIM TEST")
print("="*60)
print(f"File: AtomicCards-2-cleaned-small.yaml")
print(f"Size: {format_mb(file_size)}")

# Test 1: Default behavior (trim=True by default)
print(f"\n--- Test 1: Default behavior (should auto-trim) ---")
gc.collect()
baseline = get_anon_mmap_size()

data1 = libfyaml.loads(content)  # trim=True by default
gc.collect()

after1 = get_anon_mmap_size()
memory1 = after1 - baseline

print(f"Memory with default loads(): {format_mb(memory1)}")
print(f"Ratio to file size: {memory1/file_size:.2f}x")

del data1
gc.collect()

# Test 2: Explicit trim=False (no auto-trim)
print(f"\n--- Test 2: Explicit trim=False (no auto-trim) ---")
gc.collect()
baseline = get_anon_mmap_size()

data2 = libfyaml.loads(content, trim=False)
gc.collect()

after2 = get_anon_mmap_size()
memory2 = after2 - baseline

print(f"Memory with trim=False: {format_mb(memory2)}")
print(f"Ratio to file size: {memory2/file_size:.2f}x")

del data2
gc.collect()

# Test 3: Explicit trim=True
print(f"\n--- Test 3: Explicit trim=True ---")
gc.collect()
baseline = get_anon_mmap_size()

data3 = libfyaml.loads(content, trim=True)
gc.collect()

after3 = get_anon_mmap_size()
memory3 = after3 - baseline

print(f"Memory with trim=True: {format_mb(memory3)}")
print(f"Ratio to file size: {memory3/file_size:.2f}x")

del data3
gc.collect()

# Summary
print(f"\n{'='*60}")
print("RESULTS")
print(f"{'='*60}")
print(f"Default (auto-trim):   {format_mb(memory1)} ({memory1/file_size:.2f}x)")
print(f"trim=False (no trim):  {format_mb(memory2)} ({memory2/file_size:.2f}x)")
print(f"trim=True (explicit):  {format_mb(memory3)} ({memory3/file_size:.2f}x)")

# Verify auto-trim is working
if memory1 < memory2 * 0.6:  # Should be ~45% smaller
    print(f"\n✅ SUCCESS! Auto-trim is working by default!")
    print(f"   Saved {format_mb(memory2 - memory1)} ({(memory2-memory1)/memory2*100:.1f}%)")
else:
    print(f"\n❌ FAIL! Auto-trim may not be working")
    print(f"   Expected memory1 < memory2, but got {format_mb(memory1)} vs {format_mb(memory2)}")

# Verify trim=True matches default
if abs(memory1 - memory3) < 1024 * 1024:  # Within 1MB
    print(f"✅ Default matches trim=True")
else:
    print(f"❌ Default doesn't match trim=True: {format_mb(memory1)} vs {format_mb(memory3)}")
