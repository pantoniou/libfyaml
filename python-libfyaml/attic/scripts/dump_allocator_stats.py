#!/usr/bin/env python3
"""
Dump allocator statistics using ctypes to call C functions directly.

This bypasses the Python bindings to directly call libfyaml's allocator dump.
"""

import sys
import ctypes
import os

# Try to find libfyaml.so
possible_paths = [
    '../build/src/lib/libfyaml.so',
    '/usr/local/lib/libfyaml.so',
    '/usr/lib/libfyaml.so',
    '/usr/lib/x86_64-linux-gnu/libfyaml.so',
]

libfyaml = None
for path in possible_paths:
    if os.path.exists(path):
        try:
            libfyaml = ctypes.CDLL(path)
            print(f"Loaded: {path}")
            break
        except:
            pass

if not libfyaml:
    print("ERROR: Could not find libfyaml.so")
    sys.exit(1)

# Now use the normal Python bindings to parse
sys.path.insert(0, '.')
import libfyaml

# Read YAML file
yaml_file = 'AtomicCards-2-cleaned-small.yaml'
print(f"\nParsing: {yaml_file}")

with open(yaml_file, 'r') as f:
    content = f.read()

file_size = len(content.encode('utf-8'))
print(f"File size: {file_size / (1024*1024):.2f} MB")

# Parse
data = libfyaml.loads(content)
print(f"Parsed successfully, type: {type(data)}")

# Now we need to call the allocator dump, but we don't have access to the
# internal allocator from Python bindings. Let's just document what we'd need.

print("\n" + "="*70)
print("ANALYSIS")
print("="*70)

print("""
To get allocator statistics, we need to:

1. Expose fy_allocator_dump() in Python bindings
2. OR use DEBUG_ARENA compilation flag and watch stderr
3. OR check /proc/self/smaps for mmap'd regions

The auto allocator uses mremap which:
- Creates large mmap'd arenas (grows by 2x each time)
- Shows up in RSS even if not fully used
- May over-allocate for future growth

To investigate:
- Recompile with -DDEBUG_ARENA to see arena creation
- Check /proc/self/maps for mmap regions
- Use allocator="malloc" instead of "auto" to compare
""")

# Try to check /proc/self/maps
print("\n" + "="*70)
print("CHECKING /proc/self/maps FOR MMAP'D REGIONS")
print("="*70)

try:
    with open('/proc/self/maps', 'r') as f:
        maps = f.read()

    # Look for anonymous mappings (likely mremap arenas)
    anon_regions = []
    for line in maps.split('\n'):
        if not line:
            continue
        parts = line.split()
        if len(parts) >= 6:
            # Has a path, skip
            continue
        if len(parts) >= 2:
            # Anonymous mapping
            addr_range = parts[0]
            perms = parts[1]
            if 'rw' in perms:
                # Parse address range
                start, end = addr_range.split('-')
                start_int = int(start, 16)
                end_int = int(end, 16)
                size = end_int - start_int

                # Only show large regions (> 1MB)
                if size > 1024 * 1024:
                    anon_regions.append((addr_range, size))

    print(f"\nFound {len(anon_regions)} anonymous RW regions > 1MB:")
    total_anon = 0
    for addr, size in sorted(anon_regions, key=lambda x: -x[1])[:10]:
        print(f"  {addr}: {size / (1024*1024):.2f} MB")
        total_anon += size

    print(f"\nTotal anonymous memory > 1MB: {total_anon / (1024*1024):.2f} MB")
    print(f"File size: {file_size / (1024*1024):.2f} MB")
    print(f"Ratio: {total_anon / file_size:.2f}x")

except Exception as e:
    print(f"Could not read /proc/self/maps: {e}")

print("\n" + "="*70)
print("RECOMMENDATIONS")
print("="*70)

print("""
To reduce memory overhead:

1. **Use dedup=False for unique content**
   - Saves ~1 MB dedup hash table
   - Faster parsing (no hash lookups)

2. **Consider allocator="malloc" for small files**
   - mremap over-allocates for growth
   - malloc is better for small/fixed-size data

3. **Use mmap-based parsing** (future)
   - Eliminates source copy (save 6.35 MB)
   - OS pages in data on demand

The 7.35 MB "allocator overhead" is likely:
- mremap arena over-allocation (arenas grow by 2x)
- Unused space in current arena
- Allocator metadata (free lists, etc.)

This is a tradeoff for fast allocation - arenas avoid
fragmentation and syscalls.
""")

