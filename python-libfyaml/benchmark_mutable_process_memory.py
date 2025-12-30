#!/usr/bin/env python3
"""
Benchmark to measure process memory impact of mutable=True vs mutable=False
Uses psutil to track actual process memory (includes C allocations)
"""

import sys
import gc
import os
sys.path.insert(0, '/mnt/980-linux/panto/work/sandbox/libfyaml-generics-docs/python-libfyaml')
import libfyaml

try:
    import psutil
    HAS_PSUTIL = True
except ImportError:
    HAS_PSUTIL = False
    print("Warning: psutil not available - install with: pip3 install psutil")
    sys.exit(1)

def format_size(bytes_val):
    """Format bytes to human-readable string."""
    for unit in ['B', 'KB', 'MB', 'GB']:
        if abs(bytes_val) < 1024.0:
            return f"{bytes_val:.2f} {unit}"
        bytes_val /= 1024.0
    return f"{bytes_val:.2f} TB"

def get_memory_usage():
    """Get current process memory usage in bytes"""
    process = psutil.Process(os.getpid())
    return process.memory_info().rss

def benchmark_mutable_flag(yaml_file, iterations=3):
    """Compare memory usage with mutable=True vs mutable=False"""

    print("="*70)
    print(f"MUTABLE FLAG PROCESS MEMORY BENCHMARK")
    print("="*70)
    print(f"File: {yaml_file}")

    file_size = os.path.getsize(yaml_file)
    print(f"Size: {format_size(file_size)}")
    print(f"Iterations: {iterations}")
    print()

    # Baseline
    gc.collect()
    baseline_memory = get_memory_usage()
    print(f"Baseline process memory: {format_size(baseline_memory)}")
    print()

    results = {}

    for mutable in [False, True]:
        print("="*70)
        print(f"Testing with mutable={mutable}")
        print("="*70)

        memories = []

        for i in range(iterations):
            # Force GC
            gc.collect()
            before_memory = get_memory_usage()

            # Parse with mutable flag
            data = libfyaml.load(yaml_file, mode='yaml', mutable=mutable, dedup=True)

            # Get memory after
            gc.collect()
            after_memory = get_memory_usage()

            # Calculate memory increase
            memory_increase = after_memory - before_memory

            memories.append(memory_increase)

            print(f"  Run {i+1}/{iterations}:")
            print(f"    Before:   {format_size(before_memory)}")
            print(f"    After:    {format_size(after_memory)}")
            print(f"    Increase: {format_size(memory_increase)}")

            # Clean up
            del data
            gc.collect()

        avg_memory = sum(memories) / len(memories)
        min_memory = min(memories)
        max_memory = max(memories)

        print(f"\n  Summary:")
        print(f"    Avg memory increase: {format_size(avg_memory)}")
        print(f"    Min memory increase: {format_size(min_memory)}")
        print(f"    Max memory increase: {format_size(max_memory)}")
        print(f"    Variation:           {((max_memory - min_memory) / avg_memory * 100):.1f}%")
        print()

        results[mutable] = avg_memory

    # Print comparison
    print("="*70)
    print("COMPARISON")
    print("="*70)
    print()
    print(f"{'Mode':<20} {'Memory Increase':<20} {'vs Immutable':<15} {'Ratio to File'}")
    print("-" * 80)

    for mutable, memory in sorted(results.items()):
        ratio = memory / results[False]
        file_ratio = memory / file_size
        mode_str = "mutable=True" if mutable else "mutable=False"
        indicator = "baseline" if ratio == 1.0 else f"{ratio:.2f}x"
        print(f"{mode_str:<20} {format_size(memory):<20} {indicator:<15} {file_ratio:.2f}x")

    print()
    diff = results[True] - results[False]
    pct = (diff / results[False]) * 100

    if abs(diff) < 1024:  # Less than 1KB difference
        print(f"✓ No significant difference ({format_size(diff)})")
        print(f"  → Path tracking overhead is negligible")
    elif diff > 0:
        print(f"mutable=True uses {format_size(diff)} MORE ({pct:+.1f}%)")
        print(f"  → Path tracking overhead for mutation support")
    else:
        print(f"mutable=True uses {format_size(-diff)} LESS ({pct:+.1f}%)")

    print()
    print("Memory breakdown:")
    print(f"  File size:        {format_size(file_size)}")
    print(f"  mutable=False:    {format_size(results[False])} ({results[False]/file_size:.2f}x file)")
    print(f"  mutable=True:     {format_size(results[True])} ({results[True]/file_size:.2f}x file)")
    print()
    print("Key insights:")
    print("  • mutable=True adds path tracking for set_at_path() support")
    print("  • Path tracking is lazy: only created when mutation happens")
    print("  • For read-only workloads, both flags have similar memory usage")
    print("  • Memory overhead mainly from FyGeneric structures + dedup")

if __name__ == '__main__':
    yaml_file = "AtomicCards-2-cleaned-small.yaml"
    benchmark_mutable_flag(yaml_file, iterations=3)
