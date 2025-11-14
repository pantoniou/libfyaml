#!/usr/bin/env python3
"""
Compare memory usage between different allocators.

Tests our hypothesis that malloc allocator uses less memory for small files
because it avoids mremap arena over-allocation.
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

        anon_regions = []
        total_anon = 0
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
                        anon_regions.append((addr_range, size))
                        total_anon += size

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


def test_allocator(allocator_name, yaml_path, dedup=True):
    """Test memory usage with specific allocator."""
    print(f"\n{'='*70}")
    print(f"Testing allocator: {allocator_name} (dedup={dedup})")
    print(f"{'='*70}")

    # Get baseline measurements
    gc.collect()
    baseline_rss = get_process_memory()
    baseline_anon, _ = get_anon_mmap_regions()

    print(f"Baseline RSS:      {format_bytes(baseline_rss)}")
    print(f"Baseline anon mmap: {format_bytes(baseline_anon)}")

    # Read file
    with open(yaml_path, 'r', encoding='utf-8') as f:
        content = f.read()

    file_size = len(content.encode('utf-8'))
    print(f"File size:         {format_bytes(file_size)}")

    # Parse with specified allocator
    # Note: Current Python bindings may not expose allocator parameter yet
    # For now, test with default which uses "auto" (mremap)
    try:
        if allocator_name == "auto":
            # Default allocator
            data = libfyaml.loads(content)
        else:
            # Try to use allocator parameter if available
            # This may not work yet if not implemented
            try:
                data = libfyaml.loads(content, allocator=allocator_name)
            except TypeError:
                print(f"WARNING: allocator parameter not yet supported in bindings")
                print(f"         Using default (auto) allocator")
                data = libfyaml.loads(content)
    except Exception as e:
        print(f"ERROR: Failed to parse: {e}")
        return None

    gc.collect()

    # Get after measurements
    after_rss = get_process_memory()
    after_anon, anon_regions = get_anon_mmap_regions()

    rss_increase = after_rss - baseline_rss
    anon_increase = after_anon - baseline_anon

    print(f"\nAfter parsing:")
    print(f"RSS increase:      {format_bytes(rss_increase)} ({rss_increase/file_size:.2f}x file size)")
    print(f"Anon mmap increase: {format_bytes(anon_increase)} ({anon_increase/file_size:.2f}x file size)")

    # Show anonymous regions > 1MB
    print(f"\nAnonymous RW regions > 1MB:")
    for addr, size in sorted(anon_regions, key=lambda x: -x[1])[:5]:
        print(f"  {addr}: {format_bytes(size)}")

    return {
        'allocator': allocator_name,
        'dedup': dedup,
        'file_size': file_size,
        'rss_increase': rss_increase,
        'anon_increase': anon_increase,
        'data': data
    }


def main():
    import argparse
    parser = argparse.ArgumentParser(description='Test allocator memory usage')
    parser.add_argument('--file', default='AtomicCards-2-cleaned-small.yaml',
                        help='YAML file to test')
    args = parser.parse_args()

    yaml_path = args.file
    if not os.path.exists(yaml_path):
        print(f"ERROR: File not found: {yaml_path}")
        return 1

    print("="*70)
    print("ALLOCATOR MEMORY COMPARISON")
    print("="*70)
    print(f"File: {yaml_path}")

    # Test different configurations
    results = []

    # Test 1: Default (auto/mremap) with dedup
    result = test_allocator("auto", yaml_path, dedup=True)
    if result:
        results.append(result)

    # Clean up before next test
    gc.collect()

    # Test 2: malloc allocator (if we can specify it)
    # Note: This may not work yet if bindings don't support allocator parameter
    print("\n" + "="*70)
    print("NOTE: Testing malloc allocator")
    print("      (may fall back to auto if not yet implemented)")
    print("="*70)

    result = test_allocator("malloc", yaml_path, dedup=True)
    if result:
        results.append(result)

    # Summary comparison
    if len(results) >= 2:
        print("\n" + "="*70)
        print("COMPARISON SUMMARY")
        print("="*70)
        print(f"\n{'Allocator':<15} {'RSS Increase':<15} {'Anon mmap':<15} {'Ratio vs File'}")
        print("-" * 70)

        for r in results:
            print(f"{r['allocator']:<15} "
                  f"{format_bytes(r['rss_increase']):<15} "
                  f"{format_bytes(r['anon_increase']):<15} "
                  f"{r['rss_increase']/r['file_size']:.2f}x")

        if results[0]['rss_increase'] > results[1]['rss_increase']:
            savings = results[0]['rss_increase'] - results[1]['rss_increase']
            print(f"\nMalloc allocator saves: {format_bytes(savings)} "
                  f"({savings/results[0]['rss_increase']*100:.1f}%)")

    print("\n" + "="*70)
    print("CONCLUSIONS")
    print("="*70)
    print("""
If malloc allocator shows lower memory usage:
  → Confirms our hypothesis about mremap arena over-allocation
  → Malloc is better for small files (< 10 MB)
  → Auto/mremap is better for large files (fast allocation)

If both show similar usage:
  → Allocator parameter may not be implemented yet in bindings
  → Need to add allocator support to Python API
  → See ENHANCED-API.md for design
""")


if __name__ == '__main__':
    sys.exit(main() or 0)
