#!/usr/bin/env python3
"""
Benchmark rapidyaml on AllPrices.yaml (768 MB) with memory monitoring
"""

import sys
import gc
import os
import time
import resource

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

def benchmark_rapidyaml():
    yaml_file = "/mnt/980-linux/panto/work/sandbox/libfyaml-generics-docs/python-libfyaml/AllPrices.yaml"

    print("="*70)
    print(f"RAPIDYAML BENCHMARK: AllPrices.yaml")
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

    # Test rapidyaml
    print("="*70)
    print("Testing rapidyaml - parse_in_arena()")
    print("="*70)
    print("Note: rapidyaml keeps full parse tree in memory")
    print()

    gc.collect()
    before_memory = get_memory_usage()
    peak_memory = before_memory

    print(f"Memory before: {format_size(before_memory)}")
    print("Reading file...")

    try:
        # Read file
        read_start = time.time()
        with open(yaml_file, 'r') as f:
            content = f.read()
        read_time = time.time() - read_start

        after_read = get_memory_usage()
        print(f"File read time: {read_time:.2f} seconds")
        print(f"Memory after read: {format_size(after_read)} (+{format_size(after_read - before_memory)})")
        peak_memory = max(peak_memory, after_read)

        # Parse with rapidyaml
        print("Parsing with rapidyaml...")
        parse_start = time.time()
        tree = ryml.parse_in_arena(content)
        parse_time = time.time() - parse_start

        gc.collect()
        after_parse = get_memory_usage()
        peak_memory = max(peak_memory, after_parse)

        print(f"Parse time: {parse_time:.2f} seconds")
        print(f"Memory after parse: {format_size(after_parse)}")
        print(f"Peak memory: {format_size(peak_memory)}")
        print()

        total_increase = after_parse - before_memory
        print(f"Total memory increase: {format_size(total_increase)} ({total_increase/file_size:.2f}x file)")
        print()

        # Try to access the tree
        print("Accessing tree structure...")
        root_id = tree.root_id()
        print(f"Root ID: {root_id}")

        # Count top-level keys
        key_count = 0
        for child_id in ryml.children(tree, root_id):
            key_count += 1
            if key_count >= 10:  # Sample first 10
                break
        print(f"Sampled {key_count} top-level keys")
        print()

        print("✓ SUCCESS: rapidyaml completed parsing")

        del tree
        del content
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
    success, memory, time_taken = benchmark_rapidyaml()

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
