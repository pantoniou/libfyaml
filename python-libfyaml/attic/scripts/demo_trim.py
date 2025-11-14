#!/usr/bin/env python3
"""
Demonstrate trim() method reducing memory usage.

Shows actual memory savings from calling trim() on parsed YAML data.
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
print("TRIM() METHOD DEMONSTRATION")
print("="*60)
print(f"File: AtomicCards-2-cleaned-small.yaml")
print(f"Size: {format_mb(file_size)}")

# Measure before parsing
gc.collect()
baseline = get_anon_mmap_size()

# Parse
print(f"\nParsing...")
data = libfyaml.loads(content)
gc.collect()

# Measure after parsing (before trim)
before_trim = get_anon_mmap_size()
before_increase = before_trim - baseline

print(f"\nBEFORE trim():")
print(f"  Anonymous mmap: {format_mb(before_trim)}")
print(f"  Increase:       {format_mb(before_increase)} ({before_increase/file_size:.2f}x file size)")

# Call trim
print(f"\nCalling data.trim()...")
data.trim()
gc.collect()

# Measure after trim
after_trim = get_anon_mmap_size()
after_increase = after_trim - baseline

print(f"\nAFTER trim():")
print(f"  Anonymous mmap: {format_mb(after_trim)}")
print(f"  Increase:       {format_mb(after_increase)} ({after_increase/file_size:.2f}x file size)")

# Calculate savings
saved = before_increase - after_increase
saved_pct = (saved / before_increase * 100) if before_increase > 0 else 0

print(f"\nSAVINGS:")
print(f"  Memory freed:   {format_mb(saved)} ({saved_pct:.1f}%)")
print(f"  Arena trimmed:  {format_mb(before_trim - baseline)} → {format_mb(after_trim - baseline)}")

print(f"\n" + "="*60)
print("CONCLUSION")
print("="*60)
if saved > 1024 * 1024:  # > 1 MB
    print(f"✅ SUCCESS! trim() freed {format_mb(saved)} of memory!")
    print(f"   Arena reduced by {saved_pct:.1f}%")
    print(f"   From {before_increase/file_size:.2f}x to {after_increase/file_size:.2f}x file size")
else:
    print(f"⚠️  Small savings: {format_mb(saved)}")
    print(f"   Arena may already be tight, or trim had minimal effect")

print(f"\nThe data is still fully usable:")
print(f"  Type: {type(data)}")
print(f"  Can access: {hasattr(data, 'keys')}")
if hasattr(data, 'keys'):
    keys = list(data.keys())[:3]
    print(f"  Sample keys: {keys}")
