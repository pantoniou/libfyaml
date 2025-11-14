#!/usr/bin/env python3
"""
Benchmark comparison: libfyaml vs PyYAML vs ruamel.yaml

Tests parsing performance and memory usage on AtomicCards-2.yaml (105MB).
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
    print("Warning: libfyaml not available")

try:
    import yaml as pyyaml
    HAS_PYYAML = True
    # Check if libyaml C extension is available
    HAS_LIBYAML = hasattr(pyyaml, 'CSafeLoader')
    if HAS_LIBYAML:
        print("✓ PyYAML with libyaml C extension detected")
    else:
        print("⚠ PyYAML using pure Python implementation (libyaml not available)")
except ImportError:
    HAS_PYYAML = False
    HAS_LIBYAML = False
    print("Warning: PyYAML not available")

try:
    from ruamel.yaml import YAML
    HAS_RUAMEL = True
except ImportError:
    HAS_RUAMEL = False
    print("Warning: ruamel.yaml not available")


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


def benchmark_parse(library_name, parse_func, yaml_path, iterations=3):
    """Benchmark parsing performance."""
    print(f"\n{'='*70}")
    print(f"Benchmarking {library_name}")
    print(f"{'='*70}")

    times = []
    memory_peaks = []

    for i in range(iterations):
        # Force garbage collection before each run
        gc.collect()

        # Start memory tracking
        tracemalloc.start()

        # Time the parse
        start = time.time()
        try:
            data = parse_func(yaml_path)
            elapsed = time.time() - start

            # Get memory snapshot
            current, peak = tracemalloc.get_traced_memory()
            tracemalloc.stop()

            times.append(elapsed)
            memory_peaks.append(peak)

            print(f"  Run {i+1}/{iterations}: {format_time(elapsed)}, "
                  f"Peak memory: {format_bytes(peak)}")

            # Clean up
            del data
            gc.collect()

        except Exception as e:
            tracemalloc.stop()
            print(f"  ERROR: {e}")
            return None, None

    # Calculate statistics
    avg_time = sum(times) / len(times)
    min_time = min(times)
    max_time = max(times)
    avg_memory = sum(memory_peaks) / len(memory_peaks)

    print(f"\n  Summary:")
    print(f"    Avg time:     {format_time(avg_time)}")
    print(f"    Min time:     {format_time(min_time)}")
    print(f"    Max time:     {format_time(max_time)}")
    print(f"    Avg memory:   {format_bytes(avg_memory)}")

    return avg_time, avg_memory


def benchmark_access(library_name, data, num_accesses=100):
    """Benchmark data access patterns."""
    print(f"\n  Access pattern benchmark ({num_accesses} accesses):")

    # Get first few keys for testing
    if hasattr(data, 'keys'):
        keys = list(data.keys())[:min(num_accesses, len(data.keys()) if hasattr(data, 'keys') else 0)]
    else:
        print("    Data is not a mapping, skipping access benchmark")
        return

    if not keys:
        print("    No keys available")
        return

    # Benchmark key access
    start = time.time()
    for key in keys:
        _ = data[key]
    access_time = time.time() - start

    print(f"    {num_accesses} key accesses: {format_time(access_time)}")
    print(f"    Per-access: {format_time(access_time / num_accesses)}")


# Parse functions for each library
def parse_libfyaml(path):
    """Parse with libfyaml."""
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()
    return libfyaml.loads(content)


def parse_pyyaml(path):
    """Parse with PyYAML (auto-detects C or Python loader)."""
    with open(path, 'rb') as f:  # Read as binary
        return pyyaml.safe_load(f)


def parse_pyyaml_c(path):
    """Parse with PyYAML using explicit C loader (libyaml)."""
    with open(path, 'rb') as f:
        return pyyaml.load(f, Loader=pyyaml.CSafeLoader)


def parse_pyyaml_python(path):
    """Parse with PyYAML using pure Python loader."""
    with open(path, 'rb') as f:
        return pyyaml.load(f, Loader=pyyaml.SafeLoader)


def parse_ruamel(path):
    """Parse with ruamel.yaml."""
    yaml = YAML()
    yaml.preserve_quotes = False
    yaml.allow_unicode = True
    with open(path, 'rb') as f:  # Read as binary
        return yaml.load(f)


def main():
    yaml_file = "AtomicCards-2-cleaned-small.yaml"

    print("="*70)
    print("YAML LIBRARY BENCHMARK COMPARISON")
    print("="*70)
    print(f"File: {yaml_file}")

    # Get file size
    import os
    file_size = os.path.getsize(yaml_file)
    print(f"Size: {format_bytes(file_size)}")
    print(f"Iterations: 3 (average)")

    results = {}

    # Benchmark libfyaml
    if HAS_LIBFYAML:
        avg_time, avg_memory = benchmark_parse("libfyaml", parse_libfyaml, yaml_file)
        if avg_time:
            results['libfyaml'] = {'time': avg_time, 'memory': avg_memory}

            # Test access patterns
            data = parse_libfyaml(yaml_file)
            benchmark_access("libfyaml", data['data'] if 'data' in data else data)
            del data
            gc.collect()

    # Benchmark PyYAML with libyaml C extension
    if HAS_PYYAML and HAS_LIBYAML:
        avg_time, avg_memory = benchmark_parse("PyYAML (C/libyaml)", parse_pyyaml_c, yaml_file)
        if avg_time:
            results['PyYAML (C/libyaml)'] = {'time': avg_time, 'memory': avg_memory}

            # Test access patterns
            data = parse_pyyaml_c(yaml_file)
            benchmark_access("PyYAML (C/libyaml)", data['data'] if 'data' in data else data)
            del data
            gc.collect()

    # Benchmark PyYAML pure Python (for comparison)
    if HAS_PYYAML:
        avg_time, avg_memory = benchmark_parse("PyYAML (pure Python)", parse_pyyaml_python, yaml_file)
        if avg_time:
            results['PyYAML (pure Python)'] = {'time': avg_time, 'memory': avg_memory}

            # Test access patterns
            data = parse_pyyaml_python(yaml_file)
            benchmark_access("PyYAML (pure Python)", data['data'] if 'data' in data else data)
            del data
            gc.collect()

    # Benchmark ruamel.yaml
    if HAS_RUAMEL:
        avg_time, avg_memory = benchmark_parse("ruamel.yaml", parse_ruamel, yaml_file)
        if avg_time:
            results['ruamel.yaml'] = {'time': avg_time, 'memory': avg_memory}

            # Test access patterns
            data = parse_ruamel(yaml_file)
            benchmark_access("ruamel.yaml", data['data'] if 'data' in data else data)
            del data
            gc.collect()

    # Print comparison summary
    if results:
        print(f"\n{'='*70}")
        print("COMPARISON SUMMARY")
        print(f"{'='*70}")

        # Find fastest and most memory efficient
        fastest = min(results.items(), key=lambda x: x[1]['time'])
        most_efficient = min(results.items(), key=lambda x: x[1]['memory'])

        print(f"\n{'Library':<20} {'Parse Time':<20} {'Memory':<20} {'Speed vs Fastest'}")
        print("-" * 70)

        for name, metrics in sorted(results.items(), key=lambda x: x[1]['time']):
            time_ratio = metrics['time'] / fastest[1]['time']
            speed_indicator = f"{time_ratio:.2f}x" if time_ratio > 1 else "FASTEST"

            print(f"{name:<20} {format_time(metrics['time']):<20} "
                  f"{format_bytes(metrics['memory']):<20} {speed_indicator}")

        print(f"\n🏆 Fastest: {fastest[0]} ({format_time(fastest[1]['time'])})")
        print(f"💾 Most memory efficient: {most_efficient[0]} "
              f"({format_bytes(most_efficient[1]['memory'])})")

        # Calculate speedup comparisons
        if 'libfyaml' in results:
            print(f"\nlibfyaml speedup comparisons:")

            if 'PyYAML (C/libyaml)' in results:
                speedup = results['PyYAML (C/libyaml)']['time'] / results['libfyaml']['time']
                mem_ratio = results['PyYAML (C/libyaml)']['memory'] / results['libfyaml']['memory']
                print(f"  vs PyYAML (C/libyaml):")
                print(f"    Speed: {speedup:.2f}x faster")
                print(f"    Memory: {1/mem_ratio:.2f}x less" if mem_ratio > 1 else f"    Memory: {mem_ratio:.2f}x more")

            if 'PyYAML (pure Python)' in results:
                speedup = results['PyYAML (pure Python)']['time'] / results['libfyaml']['time']
                mem_ratio = results['PyYAML (pure Python)']['memory'] / results['libfyaml']['memory']
                print(f"  vs PyYAML (pure Python):")
                print(f"    Speed: {speedup:.2f}x faster")
                print(f"    Memory: {1/mem_ratio:.2f}x less" if mem_ratio > 1 else f"    Memory: {mem_ratio:.2f}x more")

        if 'libfyaml' in results and 'ruamel.yaml' in results:
            speedup = results['ruamel.yaml']['time'] / results['libfyaml']['time']
            mem_ratio = results['ruamel.yaml']['memory'] / results['libfyaml']['memory']
            print(f"\nlibfyaml vs ruamel.yaml:")
            print(f"  Speed: {speedup:.2f}x faster")
            print(f"  Memory: {mem_ratio:.2f}x {'more' if mem_ratio > 1 else 'less'}")


if __name__ == '__main__':
    main()
