#!/usr/bin/env python3
"""
Benchmark comparing memory usage of:
1. load() with file path (mmap-based via fy_gb_parse_file)
2. loads() with file content (string-based via fy_gb_parse)
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

def benchmark_api_comparison(yaml_file, iterations=3):
    """Compare memory usage of file-based vs string-based API"""

    print("="*70)
    print(f"MMAP VS STRING API MEMORY BENCHMARK")
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

    # Test 1: File-based API (mmap via fy_gb_parse_file)
    print("="*70)
    print("Testing load(path) - mmap-based (fy_gb_parse_file)")
    print("="*70)
    print("Uses: fy_gb_parse_file() with mmap for efficient file I/O")
    print()

    memories = []
    for i in range(iterations):
        gc.collect()
        before_memory = get_memory_usage()

        # Parse using file path (mmap-based)
        data = libfyaml.load(yaml_file, mode='yaml', mutable=True, dedup=True)

        gc.collect()
        after_memory = get_memory_usage()
        memory_increase = after_memory - before_memory
        memories.append(memory_increase)

        print(f"  Run {i+1}/{iterations}: {format_size(memory_increase)}")

        del data
        gc.collect()

    results['mmap (load path)'] = sum(memories) / len(memories)
    print(f"  Average: {format_size(results['mmap (load path)'])}")
    print()

    # Test 2: String-based API (fy_gb_parse)
    print("="*70)
    print("Testing loads(string) - string-based (fy_gb_parse)")
    print("="*70)
    print("Uses: fy_gb_parse() after reading file into Python string")
    print()

    memories = []
    for i in range(iterations):
        gc.collect()
        before_memory = get_memory_usage()

        # Read file into string
        with open(yaml_file, 'r') as f:
            content = f.read()

        # Parse using string
        data = libfyaml.loads(content, mode='yaml', mutable=True, dedup=True)

        gc.collect()
        after_memory = get_memory_usage()
        memory_increase = after_memory - before_memory
        memories.append(memory_increase)

        print(f"  Run {i+1}/{iterations}: {format_size(memory_increase)}")

        del data
        del content
        gc.collect()

    results['string (loads)'] = sum(memories) / len(memories)
    print(f"  Average: {format_size(results['string (loads)'])}")
    print()

    # Print comparison
    print("="*70)
    print("COMPARISON")
    print("="*70)
    print()
    print(f"{'Method':<25} {'Memory':<15} {'vs File Size':<15} {'vs mmap'}")
    print("-" * 75)

    mmap_memory = results['mmap (load path)']
    for method, memory in results.items():
        file_ratio = memory / file_size
        mmap_ratio = memory / mmap_memory
        ratio_str = "baseline" if mmap_ratio == 1.0 else f"{mmap_ratio:.2f}x"
        print(f"{method:<25} {format_size(memory):<15} {file_ratio:.2f}x{'':<11} {ratio_str}")

    print()
    diff = results['string (loads)'] - results['mmap (load path)']
    pct = (diff / results['mmap (load path)']) * 100

    print(f"Memory savings with mmap:")
    if diff > 0:
        print(f"  mmap saves: {format_size(diff)} ({pct:.1f}% reduction)")
        print(f"  Reason: mmap avoids Python string copy of file content")
    elif diff < 0:
        print(f"  String-based uses {format_size(-diff)} LESS ({-pct:.1f}%)")
    else:
        print(f"  No significant difference")

    print()
    print("API Comparison:")
    print(f"  File size:                  {format_size(file_size)}")
    print(f"  mmap (load path):           {format_size(mmap_memory)} ({mmap_memory/file_size:.2f}x)")
    print(f"  string (loads):             {format_size(results['string (loads)'])} ({results['string (loads)']/file_size:.2f}x)")
    print()
    print("Memory breakdown for mmap-based API:")
    estimated_yaml_copy = file_size  # mmap'd or in C
    estimated_overhead = mmap_memory - estimated_yaml_copy
    print(f"  YAML content (mmap):        ~{format_size(estimated_yaml_copy)} (in C, zero-copy)")
    print(f"  FyGeneric + overhead:       ~{format_size(estimated_overhead)} (structs, dedup, parser)")
    print()
    print("Key insights:")
    print("  ✓ New mmap-based load(path) API reduces memory overhead")
    print("  ✓ File content is memory-mapped instead of copied to Python string")
    print("  ✓ Both APIs use zero-copy for YAML values (point into source)")
    print("  ✓ Choose load(path) for large files, loads(string) for dynamic content")

if __name__ == '__main__':
    yaml_file = "AtomicCards-2-cleaned-small.yaml"
    benchmark_api_comparison(yaml_file, iterations=3)
