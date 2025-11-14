#!/usr/bin/env python3
"""
Test libfyaml (mmap) with memory limits on AllPrices.yaml
"""

import sys
import gc
import os
import time
import resource
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

def get_memory_limit():
    """Get current memory limit"""
    soft, hard = resource.getrlimit(resource.RLIMIT_AS)
    return soft, hard

def benchmark_libfyaml():
    yaml_file = "/mnt/980-linux/panto/work/sandbox/libfyaml-generics-docs/python-libfyaml/AllPrices.yaml"

    print("="*70)
    print(f"LIBFYAML (MMAP) BENCHMARK: AllPrices.yaml")
    print("="*70)

    file_size = os.path.getsize(yaml_file)
    print(f"File size: {format_size(file_size)}")

    # Show memory limit
    soft_limit, hard_limit = get_memory_limit()
    if soft_limit == resource.RLIM_INFINITY:
        print(f"Memory limit: unlimited")
    else:
        print(f"Memory limit: {format_size(soft_limit)} (soft), {format_size(hard_limit)} (hard)")
    print()

    # Baseline
    gc.collect()
    baseline_memory = get_memory_usage()
    print(f"Baseline process memory: {format_size(baseline_memory)}")
    print()

    # Test libfyaml with mmap
    print("="*70)
    print("Testing libfyaml.load(path) - mmap-based")
    print("="*70)
    print()

    gc.collect()
    before_memory = get_memory_usage()

    print(f"Memory before: {format_size(before_memory)}")

    try:
        # Parse with mmap
        print("Parsing with libfyaml (mmap)...")
        parse_start = time.time()
        data = libfyaml.load(yaml_file, mode='yaml', mutable=True, dedup=True)
        parse_time = time.time() - parse_start

        gc.collect()
        after_parse = get_memory_usage()

        print(f"Parse time: {parse_time:.2f} seconds")
        print(f"Memory after parse: {format_size(after_parse)}")
        print()

        total_increase = after_parse - before_memory
        print(f"Total memory increase: {format_size(total_increase)} ({total_increase/file_size:.2f}x file)")
        print()

        print("✓ SUCCESS: libfyaml completed parsing")

        del data
        gc.collect()

        final_memory = get_memory_usage()
        print(f"Memory after cleanup: {format_size(final_memory)}")

        return True, total_increase, parse_time

    except MemoryError as e:
        print(f"✗ FAILED: MemoryError - {e}")
        return False, None, None
    except Exception as e:
        print(f"✗ FAILED: {type(e).__name__} - {e}")
        import traceback
        traceback.print_exc()
        return False, None, None

if __name__ == '__main__':
    success, memory, time_taken = benchmark_libfyaml()

    if success:
        print()
        print("="*70)
        print("RESULT: SUCCESS")
        print("="*70)
        print(f"Memory used: {format_size(memory)}")
        print(f"Parse time: {time_taken:.2f} seconds")
        sys.exit(0)
    else:
        print()
        print("="*70)
        print("RESULT: FAILED")
        print("="*70)
        sys.exit(1)
