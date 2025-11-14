#!/usr/bin/env python3
"""
Direct comparison: libfyaml vs rapidyaml on AllPrices.yaml (768 MB)
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

try:
    import ryml
    HAS_RAPIDYAML = True
except ImportError:
    HAS_RAPIDYAML = False
    print("Warning: rapidyaml not available")

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

def benchmark_library(name, parse_func, yaml_file):
    """Benchmark a library"""
    print("="*70)
    print(f"Benchmarking {name}")
    print("="*70)

    gc.collect()
    before_memory = get_memory_usage()

    start_time = time.time()
    result = parse_func(yaml_file)
    parse_time = time.time() - start_time

    gc.collect()
    after_memory = get_memory_usage()
    memory_increase = after_memory - before_memory

    print(f"Parse time:    {parse_time:.2f} seconds")
    print(f"Memory used:   {format_size(memory_increase)}")
    print(f"Peak memory:   {format_size(after_memory)}")
    print()

    # Cleanup
    del result
    gc.collect()
    time.sleep(1)

    return {
        'memory': memory_increase,
        'time': parse_time
    }

def parse_libfyaml_mmap(yaml_file):
    """Parse with libfyaml using mmap"""
    return libfyaml.load(yaml_file, mode='yaml', mutable=True, dedup=True)

def parse_libfyaml_string(yaml_file):
    """Parse with libfyaml using string"""
    with open(yaml_file, 'r') as f:
        content = f.read()
    return libfyaml.loads(content, mode='yaml', mutable=True, dedup=True)

def parse_rapidyaml(yaml_file):
    """Parse with rapidyaml"""
    with open(yaml_file, 'r') as f:
        content = f.read()
    return ryml.parse_in_arena(content)

def main():
    yaml_file = "/mnt/980-linux/panto/work/sandbox/libfyaml-generics-docs/python-libfyaml/AllPrices.yaml"

    print("="*70)
    print("LIBRARY COMPARISON: AllPrices.yaml")
    print("="*70)

    file_size = os.path.getsize(yaml_file)
    print(f"File: {yaml_file}")
    print(f"Size: {format_size(file_size)}")
    print()

    results = {}

    # Test libfyaml with mmap
    results['libfyaml (mmap)'] = benchmark_library(
        'libfyaml with mmap',
        parse_libfyaml_mmap,
        yaml_file
    )

    # Test libfyaml with string
    results['libfyaml (string)'] = benchmark_library(
        'libfyaml with string',
        parse_libfyaml_string,
        yaml_file
    )

    # Test rapidyaml
    if HAS_RAPIDYAML:
        results['rapidyaml'] = benchmark_library(
            'rapidyaml',
            parse_rapidyaml,
            yaml_file
        )

    # Comparison
    print("="*70)
    print("COMPARISON")
    print("="*70)
    print()

    print(f"{'Library':<25} {'Memory':<15} {'Time':<12} {'vs File':<12} {'vs Best'}")
    print("-" * 85)

    best_memory = min(r['memory'] for r in results.values())

    for name, stats in sorted(results.items(), key=lambda x: x[1]['memory']):
        mem = stats['memory']
        t = stats['time']
        file_ratio = mem / file_size
        best_ratio = mem / best_memory
        ratio_str = "BEST" if best_ratio == 1.0 else f"{best_ratio:.2f}x"
        print(f"{name:<25} {format_size(mem):<15} {t:>8.2f}s   {file_ratio:>5.2f}x       {ratio_str}")

    print()
    print("="*70)
    print("KEY FINDINGS")
    print("="*70)
    print()

    libfyaml_mmap = results['libfyaml (mmap)']['memory']
    libfyaml_string = results['libfyaml (string)']['memory']

    if HAS_RAPIDYAML:
        rapidyaml_mem = results['rapidyaml']['memory']

        print(f"File size:            {format_size(file_size)}")
        print(f"libfyaml (mmap):      {format_size(libfyaml_mmap)} ({libfyaml_mmap/file_size:.2f}x) 🏆")
        print(f"libfyaml (string):    {format_size(libfyaml_string)} ({libfyaml_string/file_size:.2f}x)")
        print(f"rapidyaml:            {format_size(rapidyaml_mem)} ({rapidyaml_mem/file_size:.2f}x)")
        print()

        # Calculate differences
        rapid_vs_mmap = rapidyaml_mem / libfyaml_mmap
        mmap_savings = rapidyaml_mem - libfyaml_mmap

        print(f"rapidyaml uses {rapid_vs_mmap:.1f}x more memory than libfyaml (mmap)")
        print(f"libfyaml saves: {format_size(mmap_savings)}")
        print()

        # Memory budget analysis
        print("Memory budget analysis:")
        print(f"  With 8 GB RAM:")
        print(f"    libfyaml (mmap):   {int(8 * 1024**3 / libfyaml_mmap)} instances")
        print(f"    rapidyaml:         {int(8 * 1024**3 / rapidyaml_mem)} instance")
        print()

        # Speed comparison
        libfyaml_time = results['libfyaml (mmap)']['time']
        rapidyaml_time = results['rapidyaml']['time']
        speed_ratio = libfyaml_time / rapidyaml_time

        print(f"Speed comparison:")
        print(f"  rapidyaml: {rapidyaml_time:.2f}s (faster)")
        print(f"  libfyaml:  {libfyaml_time:.2f}s ({speed_ratio:.2f}x slower)")
        print()
        print("Trade-off:")
        print(f"  rapidyaml: {speed_ratio:.2f}x faster but {rapid_vs_mmap:.1f}x more memory")
        print(f"  libfyaml:  {1/speed_ratio:.2f}x slower but {1/rapid_vs_mmap:.2f}x less memory")

if __name__ == '__main__':
    main()
