#!/usr/bin/env python3
"""
Benchmark comparison with cleaned YAML file and debug output.
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
except ImportError:
    HAS_PYYAML = False
    print("Warning: PyYAML not available")

try:
    from ruamel.yaml import YAML
    HAS_RUAMEL = True
except ImportError:
    HAS_RUAMEL = False
    print("Warning: ruamel.yaml not available")

try:
    import ryml
    HAS_RAPIDYAML = True
except ImportError:
    HAS_RAPIDYAML = False
    print("Warning: rapidyaml not available")


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
        print(f"\n  Run {i+1}/{iterations}:")

        # Force garbage collection before each run
        print(f"    [DEBUG] Running gc.collect()...")
        gc.collect()

        # Start memory tracking
        print(f"    [DEBUG] Starting tracemalloc...")
        tracemalloc.start()

        # Time the parse
        print(f"    [DEBUG] Starting timer...")
        start = time.time()

        try:
            print(f"    [DEBUG] Calling {library_name} parse function...")
            data = parse_func(yaml_path)

            elapsed = time.time() - start
            print(f"    [DEBUG] Parse completed in {format_time(elapsed)}")

            # Get memory snapshot
            print(f"    [DEBUG] Getting memory snapshot...")
            current, peak = tracemalloc.get_traced_memory()
            tracemalloc.stop()

            times.append(elapsed)
            memory_peaks.append(peak)

            print(f"    ✅ {format_time(elapsed)}, Peak memory: {format_bytes(peak)}")

            # Clean up
            print(f"    [DEBUG] Cleaning up data object...")
            del data
            print(f"    [DEBUG] Running gc.collect()...")
            gc.collect()

        except Exception as e:
            print(f"    [DEBUG] Exception caught!")
            tracemalloc.stop()
            print(f"    ❌ ERROR: {e}")
            import traceback
            traceback.print_exc()
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

    # Handle different data types
    if hasattr(data, 'keys'):
        # Python dict (PyYAML, ruamel.yaml)
        keys_list = list(data.keys())
        print(f"    [DEBUG] Got {len(keys_list)} keys from Python mapping")
        keys = keys_list[:min(num_accesses, len(keys_list))]

        print(f"    [DEBUG] Accessing {len(keys)} keys...")
        start = time.time()
        for key in keys:
            _ = data[key]
        access_time = time.time() - start

    elif hasattr(data, 'rootref'):
        # rapidyaml tree - access children
        print(f"    [DEBUG] rapidyaml tree - accessing child nodes")
        root = data.rootref()

        # Access children
        start = time.time()
        count = 0
        for child in ryml.children(root):
            count += 1
            if count >= num_accesses:
                break
        access_time = time.time() - start
        num_accesses = count

    else:
        # libfyaml or other - assume FyGeneric with keys() method
        try:
            keys_list = list(data.keys())
            print(f"    [DEBUG] Got {len(keys_list)} keys from FyGeneric mapping")
            keys = keys_list[:min(num_accesses, len(keys_list))]

            print(f"    [DEBUG] Accessing {len(keys)} keys...")
            start = time.time()
            for key in keys:
                _ = data[key]
            access_time = time.time() - start
        except:
            print("    Unable to access data, skipping benchmark")
            return

    print(f"    {num_accesses} key accesses: {format_time(access_time)}")
    print(f"    Per-access: {format_time(access_time / num_accesses)}")


# Parse functions for each library
def parse_libfyaml(path):
    """Parse with libfyaml."""
    print(f"      [DEBUG] Opening file: {path}")
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()
    print(f"      [DEBUG] Read {len(content)} characters")
    print(f"      [DEBUG] Calling libfyaml.loads()...")
    result = libfyaml.loads(content)
    print(f"      [DEBUG] libfyaml.loads() returned")
    return result


def parse_pyyaml(path):
    """Parse with PyYAML."""
    print(f"      [DEBUG] Opening file: {path}")
    with open(path, 'r', encoding='utf-8') as f:
        print(f"      [DEBUG] Calling yaml.safe_load()...")
        result = pyyaml.safe_load(f)
        print(f"      [DEBUG] yaml.safe_load() returned")
        return result


def parse_ruamel(path):
    """Parse with ruamel.yaml."""
    print(f"      [DEBUG] Creating YAML() instance...")
    yaml = YAML()
    yaml.preserve_quotes = False
    yaml.allow_unicode = True
    print(f"      [DEBUG] Opening file: {path}")
    with open(path, 'r', encoding='utf-8') as f:
        print(f"      [DEBUG] Calling yaml.load()...")
        result = yaml.load(f)
        print(f"      [DEBUG] yaml.load() returned")
        return result


def parse_rapidyaml(path):
    """Parse with rapidyaml (ryml)."""
    print(f"      [DEBUG] Opening file: {path}")
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()
    print(f"      [DEBUG] Read {len(content)} characters")
    print(f"      [DEBUG] Calling ryml.parse_in_arena()...")
    tree = ryml.parse_in_arena(content)
    print(f"      [DEBUG] ryml.parse_in_arena() returned")
    # Return tree directly (ryml keeps data in C++ structures like libfyaml)
    # No conversion needed for fair comparison
    return tree


def main():
    yaml_file = "AtomicCards-2-cleaned-small.yaml"

    print("="*70)
    print("YAML LIBRARY BENCHMARK COMPARISON (CLEANED FILE)")
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
        print(f"\n[DEBUG] Starting libfyaml benchmark...")
        avg_time, avg_memory = benchmark_parse("libfyaml", parse_libfyaml, yaml_file)
        if avg_time:
            results['libfyaml'] = {'time': avg_time, 'memory': avg_memory}

            # Test access patterns
            print(f"\n[DEBUG] Testing libfyaml access patterns...")
            data = parse_libfyaml(yaml_file)
            if 'data' in data:
                benchmark_access("libfyaml", data['data'])
            else:
                benchmark_access("libfyaml", data)
            del data
            gc.collect()

    # Benchmark rapidyaml
    if HAS_RAPIDYAML:
        print(f"\n[DEBUG] Starting rapidyaml benchmark...")
        avg_time, avg_memory = benchmark_parse("rapidyaml", parse_rapidyaml, yaml_file)
        if avg_time:
            results['rapidyaml'] = {'time': avg_time, 'memory': avg_memory}

            # Test access patterns
            print(f"\n[DEBUG] Testing rapidyaml access patterns...")
            data = parse_rapidyaml(yaml_file)
            benchmark_access("rapidyaml", data)
            del data
            gc.collect()

    # Skip slow libraries (PyYAML, ruamel.yaml) - already benchmarked
    print(f"\n[DEBUG] Skipping PyYAML and ruamel.yaml (too slow, already benchmarked)")
    # Add previous results for comparison
    results['PyYAML (prev)'] = {'time': 32.70, 'memory': 147.36 * 1024 * 1024}
    results['ruamel.yaml (prev)'] = {'time': 53.49, 'memory': 162.50 * 1024 * 1024}

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

        # Calculate speedup
        if 'libfyaml' in results and 'rapidyaml' in results:
            speedup = results['rapidyaml']['time'] / results['libfyaml']['time']
            mem_ratio = results['rapidyaml']['memory'] / results['libfyaml']['memory']
            print(f"\nlibfyaml vs rapidyaml:")
            print(f"  Speed: {speedup:.2f}x {'faster' if speedup > 1 else 'slower'}")
            print(f"  Memory: {mem_ratio:.2f}x {'less' if mem_ratio < 1 else 'more'}")

        if 'libfyaml' in results and 'PyYAML (prev)' in results:
            speedup = results['PyYAML (prev)']['time'] / results['libfyaml']['time']
            mem_ratio = results['PyYAML (prev)']['memory'] / results['libfyaml']['memory']
            print(f"\nlibfyaml vs PyYAML (previous benchmark):")
            print(f"  Speed: {speedup:.2f}x {'faster' if speedup > 1 else 'slower'}")
            print(f"  Memory: {mem_ratio:.2f}x {'less' if mem_ratio < 1 else 'more'}")

        if 'libfyaml' in results and 'ruamel.yaml (prev)' in results:
            speedup = results['ruamel.yaml (prev)']['time'] / results['libfyaml']['time']
            mem_ratio = results['ruamel.yaml (prev)']['memory'] / results['libfyaml']['memory']
            print(f"\nlibfyaml vs ruamel.yaml (previous benchmark):")
            print(f"  Speed: {speedup:.2f}x {'faster' if speedup > 1 else 'slower'}")
            print(f"  Memory: {mem_ratio:.2f}x {'less' if mem_ratio < 1 else 'more'}")


if __name__ == '__main__':
    main()
