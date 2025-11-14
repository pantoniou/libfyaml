#!/usr/bin/env python3
"""
Test if calling allocator trim reduces memory usage.

Based on user suggestion that allocators have a trim method that should be
called at end of parsing to release unused arena space.
"""

import sys
import os
import gc
sys.path.insert(0, '.')
import libfyaml


def get_process_memory():
    """Get actual process memory from /proc/self/status (RSS)."""
    try:
        with open('/proc/self/status', 'r') as f:
            for line in f:
                if line.startswith('VmRSS:'):
                    kb = int(line.split()[1])
                    return kb * 1024
    except:
        return None
    return None


def get_anon_mmap_regions():
    """Get total size of anonymous mmap regions from /proc/self/maps."""
    try:
        with open('/proc/self/maps', 'r') as f:
            maps = f.read()

        total_anon = 0
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

                    # Only count large regions (> 1MB) - likely arenas
                    if size > 1024 * 1024:
                        total_anon += size
                        anon_regions.append((addr_range, size))

        return total_anon, anon_regions
    except Exception as e:
        return None, []


def format_bytes(bytes_size):
    """Format bytes to human-readable string."""
    for unit in ['B', 'KB', 'MB', 'GB']:
        if bytes_size < 1024.0:
            return f"{bytes_size:.2f} {unit}"
        bytes_size /= 1024.0
    return f"{bytes_size:.2f} TB"


def main():
    yaml_path = 'AtomicCards-2-cleaned-small.yaml'
    if not os.path.exists(yaml_path):
        print(f"ERROR: File not found: {yaml_path}")
        return 1

    # Read file
    with open(yaml_path, 'r', encoding='utf-8') as f:
        content = f.read()

    file_size = len(content.encode('utf-8'))

    print("="*70)
    print("TESTING ALLOCATOR TRIM METHOD")
    print("="*70)
    print(f"File: {yaml_path}")
    print(f"Size: {format_bytes(file_size)}")

    # Get baseline
    gc.collect()
    baseline_rss = get_process_memory()
    baseline_anon, _ = get_anon_mmap_regions()

    print(f"\nBaseline RSS:      {format_bytes(baseline_rss)}")
    print(f"Baseline anon mmap: {format_bytes(baseline_anon)}")

    # Parse
    print(f"\nParsing...")
    data = libfyaml.loads(content)
    gc.collect()

    # Measurements before trim
    before_rss = get_process_memory()
    before_anon, before_regions = get_anon_mmap_regions()

    rss_increase_before = before_rss - baseline_rss
    anon_increase_before = before_anon - baseline_anon

    print(f"\n{'='*70}")
    print("BEFORE TRIM")
    print(f"{'='*70}")
    print(f"RSS increase:      {format_bytes(rss_increase_before)} ({rss_increase_before/file_size:.2f}x file size)")
    print(f"Anon mmap increase: {format_bytes(anon_increase_before)} ({anon_increase_before/file_size:.2f}x file size)")

    print(f"\nAnonymous RW regions > 1MB:")
    for addr, size in sorted(before_regions, key=lambda x: -x[1])[:5]:
        print(f"  {addr}: {format_bytes(size)}")

    # Now try to call trim if it's accessible
    print(f"\n{'='*70}")
    print("ATTEMPTING TO CALL TRIM")
    print(f"{'='*70}")

    # The Python bindings use FyDocument internally
    # Check if we can access the document or allocator
    print(f"\ndata type: {type(data)}")
    print(f"data attributes: {dir(data)}")

    # Check if there's a way to call trim
    # This might not work if trim isn't exposed in Python bindings
    has_trim = False
    if hasattr(data, 'trim'):
        print(f"\nCalling data.trim()...")
        data.trim()
        has_trim = True
    elif hasattr(data, '_trim'):
        print(f"\nCalling data._trim()...")
        data._trim()
        has_trim = True
    elif hasattr(data, 'allocator_trim'):
        print(f"\nCalling data.allocator_trim()...")
        data.allocator_trim()
        has_trim = True
    else:
        print(f"\nNo trim method found in Python bindings")
        print(f"This is expected - trim() needs to be added to the bindings")

    if has_trim:
        gc.collect()

        # Measurements after trim
        after_rss = get_process_memory()
        after_anon, after_regions = get_anon_mmap_regions()

        rss_increase_after = after_rss - baseline_rss
        anon_increase_after = after_anon - baseline_anon

        print(f"\n{'='*70}")
        print("AFTER TRIM")
        print(f"{'='*70}")
        print(f"RSS increase:      {format_bytes(rss_increase_after)} ({rss_increase_after/file_size:.2f}x file size)")
        print(f"Anon mmap increase: {format_bytes(anon_increase_after)} ({anon_increase_after/file_size:.2f}x file size)")

        print(f"\nAnonymous RW regions > 1MB:")
        for addr, size in sorted(after_regions, key=lambda x: -x[1])[:5]:
            print(f"  {addr}: {format_bytes(size)}")

        # Calculate savings
        rss_saved = rss_increase_before - rss_increase_after
        anon_saved = anon_increase_before - anon_increase_after

        print(f"\n{'='*70}")
        print("SAVINGS FROM TRIM")
        print(f"{'='*70}")
        print(f"RSS saved:          {format_bytes(rss_saved)} ({rss_saved/rss_increase_before*100:.1f}%)")
        print(f"Anon mmap saved:    {format_bytes(anon_saved)} ({anon_saved/anon_increase_before*100:.1f}%)")

    print(f"\n{'='*70}")
    print("CONCLUSIONS")
    print(f"{'='*70}")

    if has_trim:
        print("""
✅ Trim method is accessible and was called
   → Check savings above to see if mremap arena was trimmed
   → If savings ~7 MB, trim successfully released unused arena space!
""")
    else:
        print("""
⚠️ Trim method is NOT accessible from Python bindings

NEXT STEPS:
1. Expose trim in Python bindings (add to FyDocument or FyGeneric)
2. Or: Have C library call trim automatically after parsing
3. Or: Expose allocator in Python so we can call trim manually

EXPECTED RESULTS (from C code investigation):
- fy_allocator_trim_tag() exists and works
- fy_gb_trim() wrapper exists in generic API
- NOT called by fy_document_build_from_string()
- NOT called by fy_parse_cleanup()

If trim were called, we would expect:
- mremap arena to shrink from ~32 MB to ~10-15 MB used space
- Savings: ~7 MB (31% memory reduction!)
- RSS would drop from 22.36 MB to ~15 MB
""")

    print(f"\n{'='*70}")
    print("RECOMMENDATION")
    print(f"{'='*70}")
    print("""
The allocator trim functionality exists but is not being used!

OPTION 1: Auto-trim in C library (best)
  - Add fy_allocator_trim_tag() call to fy_document_build_from_string()
  - Call after fy_parse_load_document() completes
  - Benefit: All users get automatic memory reduction

OPTION 2: Expose in Python bindings
  - Add trim() method to FyDocument wrapper
  - Users can call manually after parsing
  - Benefit: Gives users control over when to trim

OPTION 3: Both
  - Auto-trim in C library by default
  - Expose in Python for advanced users
  - Add flag to disable auto-trim if needed
""")


if __name__ == '__main__':
    sys.exit(main() or 0)
