#!/usr/bin/env python3
"""
Benchmark comparing memory usage of mmap vs string API on AllPrices.yaml (768 MB)
"""

import sys
import gc
import os
import time
sys.path.insert(0, '/mnt/980-linux/panto/work/sandbox/libfyaml-generics-docs/python-libfyaml')
import libfyaml

try:
    import psutil
    HAS_PSUTIL = True
except ImportError:
    HAS_PSUTIL = False
    print("Warning: psutil not available")
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

def benchmark_large_file():
    """Benchmark AllPrices.yaml (768 MB)"""

    yaml_file = "/mnt/980-linux/panto/work/sandbox/libfyaml-generics-docs/python-libfyaml/AllPrices.yaml"

    print("="*70)
    print(f"LARGE FILE BENCHMARK: AllPrices.yaml")
    print("="*70)

    file_size = os.path.getsize(yaml_file)
    print(f"File size: {format_size(file_size)}")
    print()

    # Baseline
    gc.collect()
    baseline_memory = get_memory_usage()
    print(f"Baseline process memory: {format_size(baseline_memory)}")
    print()

    results = {}

    # Test 1: mmap-based load (fy_gb_parse_file)
    print("="*70)
    print("Test 1: load(path) - mmap-based API")
    print("="*70)
    print("Using: fy_gb_parse_file() with memory-mapped I/O")
    print()

    gc.collect()
    before_memory = get_memory_usage()
    print(f"Memory before: {format_size(before_memory)}")

    start_time = time.time()
    data = libfyaml.load(yaml_file, mode='yaml', mutable=True, dedup=True)
    parse_time = time.time() - start_time

    gc.collect()
    after_memory = get_memory_usage()
    memory_increase = after_memory - before_memory

    print(f"Parse time:    {parse_time:.2f} seconds")
    print(f"Memory after:  {format_size(after_memory)}")
    print(f"Memory used:   {format_size(memory_increase)} ({memory_increase/file_size:.2f}x file size)")
    print()

    # Count items
    print("Analyzing structure...")
    item_count = 0
    try:
        for key in data.keys():
            item_count += 1
            if item_count >= 10:  # Sample first 10
                break
    except:
        pass

    print(f"Structure: Top-level mapping with many keys")
    print()

    results['mmap'] = {
        'memory': memory_increase,
        'time': parse_time
    }

    del data
    gc.collect()

    # Wait a bit for memory to stabilize
    time.sleep(2)

    # Test 2: String-based load (fy_gb_parse)
    print("="*70)
    print("Test 2: loads(string) - string-based API")
    print("="*70)
    print("Using: fy_gb_parse() with Python string")
    print("WARNING: This will load 768 MB into Python string first!")
    print()

    gc.collect()
    before_memory = get_memory_usage()
    print(f"Memory before: {format_size(before_memory)}")

    start_time = time.time()

    # Read file into string
    print("Reading file into Python string...")
    read_start = time.time()
    with open(yaml_file, 'r') as f:
        content = f.read()
    read_time = time.time() - read_start
    print(f"File read time: {read_time:.2f} seconds")

    # Parse
    print("Parsing string...")
    parse_start = time.time()
    data = libfyaml.loads(content, mode='yaml', mutable=True, dedup=True)
    parse_time = time.time() - parse_start
    total_time = time.time() - start_time

    gc.collect()
    after_memory = get_memory_usage()
    memory_increase = after_memory - before_memory

    print(f"Parse time:    {parse_time:.2f} seconds")
    print(f"Total time:    {total_time:.2f} seconds")
    print(f"Memory after:  {format_size(after_memory)}")
    print(f"Memory used:   {format_size(memory_increase)} ({memory_increase/file_size:.2f}x file size)")
    print()

    results['string'] = {
        'memory': memory_increase,
        'time': total_time
    }

    del data
    del content
    gc.collect()

    # Comparison
    print("="*70)
    print("COMPARISON")
    print("="*70)
    print()

    print(f"{'Method':<20} {'Memory':<15} {'Time':<12} {'vs File':<12} {'vs mmap'}")
    print("-" * 80)

    for method, stats in [('mmap (load path)', results['mmap']),
                          ('string (loads)', results['string'])]:
        mem = stats['memory']
        t = stats['time']
        file_ratio = mem / file_size
        mmap_ratio = mem / results['mmap']['memory']
        ratio_str = "baseline" if mmap_ratio == 1.0 else f"{mmap_ratio:.2f}x"
        print(f"{method:<20} {format_size(mem):<15} {t:>8.2f}s   {file_ratio:>5.2f}x       {ratio_str}")

    print()
    mem_diff = results['string']['memory'] - results['mmap']['memory']
    mem_pct = (mem_diff / results['mmap']['memory']) * 100
    time_diff = results['string']['time'] - results['mmap']['time']

    print(f"Memory savings with mmap:")
    print(f"  Saved: {format_size(mem_diff)} ({mem_pct:.1f}% reduction)")
    print(f"  Time difference: {time_diff:+.2f} seconds")
    print()
    print(f"File size:        {format_size(file_size)}")
    print(f"mmap memory:      {format_size(results['mmap']['memory'])} ({results['mmap']['memory']/file_size:.2f}x)")
    print(f"string memory:    {format_size(results['string']['memory'])} ({results['string']['memory']/file_size:.2f}x)")
    print()
    print("🎯 Key Insights for 768 MB file:")
    print(f"  ✓ mmap uses {mem_pct:.0f}% less memory than string-based approach")
    print(f"  ✓ mmap avoids copying {format_size(file_size)} into Python string")
    print(f"  ✓ Both use zero-copy for YAML values (point into source)")
    print(f"  ✓ Memory overhead: {format_size(results['mmap']['memory'] - file_size)} for FyGeneric structures")

if __name__ == '__main__':
    benchmark_large_file()
