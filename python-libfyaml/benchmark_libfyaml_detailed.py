#!/usr/bin/env python3
"""
Detailed libfyaml benchmark showing parse + conversion steps.

Tests the "worst case" where libfyaml parses AND converts to Python objects,
making it apples-to-apples with PyYAML.
"""

import sys
import time
import gc
import tracemalloc
sys.path.insert(0, '.')

try:
    import libfyaml
    HAS_LIBFYAML = True
except ImportError:
    HAS_LIBFYAML = False
    print("ERROR: libfyaml not available")
    sys.exit(1)

try:
    import yaml as pyyaml
    HAS_PYYAML = True
    HAS_LIBYAML = hasattr(pyyaml, 'CSafeLoader')
except ImportError:
    HAS_PYYAML = False
    HAS_LIBYAML = False


def format_bytes(bytes_size):
    """Format bytes to human-readable string."""
    for unit in ['B', 'KB', 'MB', 'GB']:
        if bytes_size < 1024.0:
            return f"{bytes_size:.2f} {unit}"
        bytes_size /= 1024.0
    return f"{bytes_size:.2f} TB"


def format_time(seconds):
    """Format seconds to human-readable string."""
    if seconds < 1:
        return f"{seconds * 1000:.2f} ms"
    elif seconds < 60:
        return f"{seconds:.2f} s"
    else:
        minutes = int(seconds / 60)
        secs = seconds % 60
        return f"{minutes}m {secs:.2f}s"


def benchmark_libfyaml_detailed(yaml_path, iterations=3):
    """Benchmark libfyaml with detailed timing of each step."""
    print(f"\n{'='*70}")
    print(f"Benchmarking libfyaml (detailed)")
    print(f"{'='*70}")

    parse_times = []
    convert_times = []
    total_times = []
    memory_peaks = []

    for i in range(iterations):
        gc.collect()
        tracemalloc.start()

        # Read file
        with open(yaml_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # Step 1: Parse to FyGeneric (fast, lazy)
        start_parse = time.time()
        data_generic = libfyaml.loads(content)
        parse_elapsed = time.time() - start_parse

        # Step 2: Convert to Python objects (materialization)
        start_convert = time.time()
        data_python = data_generic.to_python()
        convert_elapsed = time.time() - start_convert

        total_elapsed = parse_elapsed + convert_elapsed

        # Get memory
        current, peak = tracemalloc.get_traced_memory()
        tracemalloc.stop()

        parse_times.append(parse_elapsed)
        convert_times.append(convert_elapsed)
        total_times.append(total_elapsed)
        memory_peaks.append(peak)

        print(f"  Run {i+1}/{iterations}:")
        print(f"    Parse (FyGeneric):    {format_time(parse_elapsed)}")
        print(f"    Convert (to Python):  {format_time(convert_elapsed)}")
        print(f"    Total:                {format_time(total_elapsed)}")
        print(f"    Peak memory:          {format_bytes(peak)}")

        # Clean up
        del data_generic
        del data_python
        del content
        gc.collect()

    # Calculate statistics
    avg_parse = sum(parse_times) / len(parse_times)
    avg_convert = sum(convert_times) / len(convert_times)
    avg_total = sum(total_times) / len(total_times)
    avg_memory = sum(memory_peaks) / len(memory_peaks)

    print(f"\n  Summary:")
    print(f"    Avg parse time:       {format_time(avg_parse)}")
    print(f"    Avg convert time:     {format_time(avg_convert)}")
    print(f"    Avg total time:       {format_time(avg_total)}")
    print(f"    Avg memory:           {format_bytes(avg_memory)}")
    print(f"    Parse vs Convert:     {avg_parse/avg_convert:.2f}x faster to parse")

    return {
        'parse': avg_parse,
        'convert': avg_convert,
        'total': avg_total,
        'memory': avg_memory
    }


def benchmark_pyyaml_c(yaml_path, iterations=3):
    """Benchmark PyYAML (C/libyaml) for comparison."""
    print(f"\n{'='*70}")
    print(f"Benchmarking PyYAML (C/libyaml)")
    print(f"{'='*70}")

    times = []
    memory_peaks = []

    for i in range(iterations):
        gc.collect()
        tracemalloc.start()

        start = time.time()
        with open(yaml_path, 'rb') as f:
            data = pyyaml.load(f, Loader=pyyaml.CSafeLoader)
        elapsed = time.time() - start

        current, peak = tracemalloc.get_traced_memory()
        tracemalloc.stop()

        times.append(elapsed)
        memory_peaks.append(peak)

        print(f"  Run {i+1}/{iterations}:")
        print(f"    Total:                {format_time(elapsed)}")
        print(f"    Peak memory:          {format_bytes(peak)}")

        del data
        gc.collect()

    avg_time = sum(times) / len(times)
    avg_memory = sum(memory_peaks) / len(memory_peaks)

    print(f"\n  Summary:")
    print(f"    Avg time:             {format_time(avg_time)}")
    print(f"    Avg memory:           {format_bytes(avg_memory)}")

    return {
        'total': avg_time,
        'memory': avg_memory
    }


def main():
    import argparse
    parser = argparse.ArgumentParser(description='Detailed libfyaml benchmark')
    parser.add_argument('--skip-pyyaml', action='store_true',
                        help='Skip PyYAML benchmark (only run libfyaml)')
    parser.add_argument('--file', default='AtomicCards-2-cleaned-small.yaml',
                        help='YAML file to benchmark (default: AtomicCards-2-cleaned-small.yaml)')
    parser.add_argument('--iterations', type=int, default=3,
                        help='Number of iterations (default: 3)')
    args = parser.parse_args()

    yaml_file = args.file
    iterations = args.iterations

    print("="*70)
    print("LIBFYAML DETAILED BENCHMARK")
    print("="*70)
    print(f"File: {yaml_file}")

    # Get file size
    import os
    file_size = os.path.getsize(yaml_file)
    print(f"Size: {format_bytes(file_size)}")
    print(f"Iterations: {iterations}")

    # Benchmark libfyaml with detailed timing
    libfyaml_results = benchmark_libfyaml_detailed(yaml_file, iterations)

    # Optionally benchmark PyYAML for comparison
    pyyaml_results = None
    if not args.skip_pyyaml and HAS_PYYAML and HAS_LIBYAML:
        pyyaml_results = benchmark_pyyaml_c(yaml_file, iterations)
    elif args.skip_pyyaml:
        print(f"\n{'='*70}")
        print("Skipping PyYAML benchmark (--skip-pyyaml)")
        print(f"{'='*70}")

    # Comparison
    if pyyaml_results:
        print(f"\n{'='*70}")
        print("COMPARISON")
        print(f"{'='*70}")

        print(f"\nlibfyaml breakdown:")
        print(f"  Parse (FyGeneric):    {format_time(libfyaml_results['parse'])}")
        print(f"  Convert (to Python):  {format_time(libfyaml_results['convert'])}")
        print(f"  Total:                {format_time(libfyaml_results['total'])}")
        print(f"  Memory:               {format_bytes(libfyaml_results['memory'])}")

        print(f"\nPyYAML (C/libyaml):")
        print(f"  Total:                {format_time(pyyaml_results['total'])}")
        print(f"  Memory:               {format_bytes(pyyaml_results['memory'])}")

        # Calculate speedup
        speedup = pyyaml_results['total'] / libfyaml_results['total']
        mem_ratio = pyyaml_results['memory'] / libfyaml_results['memory']

        print(f"\n🏆 libfyaml (parse + convert) vs PyYAML:")
        print(f"   Speed: {speedup:.2f}x faster")
        print(f"   Memory: {1/mem_ratio:.2f}x less")

        # Show what if we only count parsing
        speedup_parse_only = pyyaml_results['total'] / libfyaml_results['parse']
        print(f"\n💡 libfyaml (parse only, FyGeneric) vs PyYAML:")
        print(f"   Speed: {speedup_parse_only:.2f}x faster")
        print(f"   (FyGeneric is lazy - convert only what you access)")


if __name__ == '__main__':
    main()
