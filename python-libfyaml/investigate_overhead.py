#!/usr/bin/env python3
"""
Deep investigation into the "Other overhead" memory usage.

Uses multiple profiling approaches:
1. tracemalloc (Python allocations)
2. /proc/self/status (process RSS)
3. gc module (object counts)
4. sys.getsizeof (object sizes)
"""

import sys
import gc
import tracemalloc
import os
sys.path.insert(0, '.')

try:
    import libfyaml
    HAS_LIBFYAML = True
except ImportError:
    HAS_LIBFYAML = False
    print("ERROR: libfyaml not available")
    sys.exit(1)


def format_bytes(bytes_size):
    """Format bytes to human-readable string."""
    for unit in ['B', 'KB', 'MB', 'GB']:
        if bytes_size < 1024.0:
            return f"{bytes_size:.2f} {unit}"
        bytes_size /= 1024.0
    return f"{bytes_size:.2f} TB"


def get_process_memory():
    """Get actual process memory from /proc/self/status."""
    try:
        with open('/proc/self/status', 'r') as f:
            for line in f:
                if line.startswith('VmRSS:'):
                    # VmRSS is in KB
                    kb = int(line.split()[1])
                    return kb * 1024
    except:
        return None
    return None


def count_objects_by_type():
    """Count all objects by type."""
    counts = {}
    for obj in gc.get_objects():
        t = type(obj).__name__
        counts[t] = counts.get(t, 0) + 1
    return counts


def investigate_overhead(yaml_path):
    """Deep investigation of memory overhead."""

    print("="*70)
    print("DEEP MEMORY INVESTIGATION")
    print("="*70)

    file_size = os.path.getsize(yaml_path)
    print(f"\nFile: {yaml_path}")
    print(f"Size: {format_bytes(file_size)}")

    # Read content
    with open(yaml_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # Baseline measurements
    gc.collect()
    baseline_rss = get_process_memory()
    baseline_objects = count_objects_by_type()

    print(f"\n{'='*70}")
    print("BASELINE (before parsing)")
    print(f"{'='*70}")
    print(f"Process RSS: {format_bytes(baseline_rss)}")
    print(f"Total objects: {sum(baseline_objects.values()):,}")

    # Start tracemalloc
    tracemalloc.start()
    baseline_tracemalloc = tracemalloc.get_traced_memory()[0]

    # Parse to FyGeneric
    print(f"\n{'='*70}")
    print("PARSING TO FYGENERIC...")
    print(f"{'='*70}")

    data = libfyaml.loads(content)
    gc.collect()

    # Measurements after parsing
    after_parse_rss = get_process_memory()
    after_parse_tracemalloc, peak_tracemalloc = tracemalloc.get_traced_memory()
    after_parse_objects = count_objects_by_type()

    rss_increase = after_parse_rss - baseline_rss
    tracemalloc_increase = after_parse_tracemalloc - baseline_tracemalloc

    print(f"\nProcess RSS:      {format_bytes(after_parse_rss)} "
          f"(+{format_bytes(rss_increase)})")
    print(f"tracemalloc:      {format_bytes(after_parse_tracemalloc)} "
          f"(+{format_bytes(tracemalloc_increase)})")
    print(f"Peak tracemalloc: {format_bytes(peak_tracemalloc)}")

    # Object count changes
    print(f"\nObject count changes:")
    new_types = {}
    for t, count in after_parse_objects.items():
        baseline_count = baseline_objects.get(t, 0)
        if count > baseline_count:
            new_types[t] = count - baseline_count

    # Sort by count
    for t, count in sorted(new_types.items(), key=lambda x: -x[1])[:20]:
        print(f"  {t:<40} +{count:>8,}")

    # Analyze FyGeneric objects
    print(f"\n{'='*70}")
    print("FYGENERIC OBJECT ANALYSIS")
    print(f"{'='*70}")

    fygeneric_count = new_types.get('FyGeneric', 0)
    print(f"FyGeneric objects created: {fygeneric_count:,}")

    if fygeneric_count > 0:
        # Get a sample FyGeneric object
        sample_size = sys.getsizeof(data)
        print(f"Size of root FyGeneric: {format_bytes(sample_size)}")

        estimated_fygeneric_overhead = fygeneric_count * sample_size
        print(f"Estimated FyGeneric Python objects: {format_bytes(estimated_fygeneric_overhead)}")

    # Compare RSS vs tracemalloc
    print(f"\n{'='*70}")
    print("RSS vs TRACEMALLOC COMPARISON")
    print(f"{'='*70}")
    print(f"Process RSS increase:     {format_bytes(rss_increase)}")
    print(f"tracemalloc increase:     {format_bytes(tracemalloc_increase)}")
    print(f"Difference (untracked):   {format_bytes(rss_increase - tracemalloc_increase)}")
    print(f"\nThe difference is likely C library allocations (not tracked by tracemalloc)")

    # Try to access deep into structure to trigger wrapper creation
    print(f"\n{'='*70}")
    print("TESTING LAZY WRAPPER CREATION")
    print(f"{'='*70}")

    # Count FyGeneric before access
    gc.collect()
    before_access = count_objects_by_type().get('FyGeneric', 0)
    print(f"FyGeneric count before access: {before_access:,}")

    # Access some items (triggers wrapper creation)
    try:
        if hasattr(data, 'keys'):
            keys = list(data.keys())[:10]
            for key in keys:
                _ = data[key]
    except:
        pass

    gc.collect()
    after_access = count_objects_by_type().get('FyGeneric', 0)
    print(f"FyGeneric count after accessing 10 keys: {after_access:,}")
    print(f"New wrappers created: {after_access - before_access:,}")

    # Memory snapshot
    print(f"\n{'='*70}")
    print("TRACEMALLOC TOP ALLOCATIONS")
    print(f"{'='*70}")

    snapshot = tracemalloc.take_snapshot()
    top_stats = snapshot.statistics('lineno')

    print("\nTop 10 allocations:")
    for index, stat in enumerate(top_stats[:10], 1):
        print(f"{index}. {stat.traceback.format()[0]}")
        print(f"   Size: {format_bytes(stat.size)}, Count: {stat.count}")

    tracemalloc.stop()

    # Calculate breakdown
    print(f"\n{'='*70}")
    print("MEMORY BREAKDOWN (RSS-based)")
    print(f"{'='*70}")

    print(f"\nTotal RSS increase:       {format_bytes(rss_increase)}")
    print(f"Ratio to file size:       {rss_increase / file_size:.2f}x")

    # Estimate components
    source_size = len(content.encode('utf-8'))
    estimated_fy_wrappers = fygeneric_count * sample_size if fygeneric_count > 0 else 0
    estimated_c_library = rss_increase - tracemalloc_increase

    print(f"\nEstimated breakdown:")
    print(f"  Python tracked:           {format_bytes(tracemalloc_increase)} "
          f"({tracemalloc_increase/rss_increase*100:.1f}%)")
    print(f"    - FyGeneric wrappers:   ~{format_bytes(estimated_fy_wrappers)}")
    print(f"    - Other Python objects: ~{format_bytes(tracemalloc_increase - estimated_fy_wrappers)}")
    print(f"  C library (untracked):    {format_bytes(estimated_c_library)} "
          f"({estimated_c_library/rss_increase*100:.1f}%)")
    print(f"    - Source YAML:          ~{format_bytes(source_size)}")
    print(f"    - FyGeneric structs:    ~{format_bytes(fygeneric_count * 8) if fygeneric_count > 0 else 0}")
    print(f"    - Parser/allocator:     ~{format_bytes(estimated_c_library - source_size)}")

    # Check if wrappers are the issue
    print(f"\n{'='*70}")
    print("CONCLUSIONS")
    print(f"{'='*70}")

    if fygeneric_count > 100:
        print(f"\n⚠️ FOUND THE ISSUE: {fygeneric_count:,} FyGeneric Python wrappers!")
        print(f"   Each wrapper is ~{sample_size} bytes")
        print(f"   Total overhead: ~{format_bytes(estimated_fy_wrappers)}")
        print(f"   ")
        print(f"   This is likely because libfyaml creates wrappers eagerly.")
        print(f"   ")
        print(f"   OPTIMIZATION: Implement lazy wrapper creation")
        print(f"   - Only create wrappers when accessed")
        print(f"   - Pool wrappers for reuse")
        print(f"   - Use weak references")
        print(f"   ")
        print(f"   Potential savings: ~{format_bytes(estimated_fy_wrappers * 0.9)}")

    untracked_ratio = estimated_c_library / rss_increase
    if untracked_ratio > 0.5:
        print(f"\n⚠️ Large C library allocations: {format_bytes(estimated_c_library)} ({untracked_ratio*100:.1f}%)")
        print(f"   This is normal for libfyaml's zero-copy design.")
        print(f"   Includes: source YAML, parser structures, dedup hash table")


def main():
    import argparse
    parser = argparse.ArgumentParser(description='Investigate memory overhead')
    parser.add_argument('--file', default='AtomicCards-2-cleaned-small.yaml',
                        help='YAML file to analyze')
    args = parser.parse_args()

    investigate_overhead(args.file)


if __name__ == '__main__':
    main()
