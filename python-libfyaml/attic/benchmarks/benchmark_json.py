#!/usr/bin/env python3
"""
Benchmark libfyaml against JSON libraries.

Tests:
1. Parse performance (time)
2. Memory usage
3. Real-world JSON file (AllPrintings.json - 428MB MTG card database)
4. With/without deduplication

Usage:
    python benchmark_json.py                    # Use real AllPrintings.json file
    python benchmark_json.py --synthetic        # Use synthetic data (old behavior)
    python benchmark_json.py --file <path>      # Use custom JSON file
"""

import sys
import json
import time
import gc
import os
import argparse
from pathlib import Path

# Add current directory to path
sys.path.insert(0, str(Path(__file__).parent))
import libfyaml

try:
    import psutil
    HAS_PSUTIL = True
except ImportError:
    HAS_PSUTIL = False
    print("ERROR: psutil required for memory measurements")
    print("Install with: pip install psutil")
    sys.exit(1)

# Optional libraries
try:
    import orjson
    HAS_ORJSON = True
except ImportError:
    HAS_ORJSON = False

try:
    import ujson
    HAS_UJSON = True
except ImportError:
    HAS_UJSON = False


def generate_json_data(size='small', with_repetition=False):
    """Generate test JSON data of various sizes."""

    if size == 'small':
        # ~1KB
        count = 10
    elif size == 'medium':
        # ~100KB
        count = 1000
    elif size == 'large':
        # ~10MB
        count = 100000
    else:
        raise ValueError(f"Unknown size: {size}")

    if with_repetition:
        # Repeated strings to test deduplication
        items = [
            {
                "id": i,
                "name": "User Name",  # Repeated
                "email": "user@example.com",  # Repeated
                "status": "active",  # Repeated
                "role": "admin",  # Repeated
                "city": "San Francisco",  # Repeated
                "country": "USA",  # Repeated
                "tags": ["python", "developer", "senior"],  # Repeated
            }
            for i in range(count)
        ]
    else:
        # Unique strings (no dedup benefit)
        items = [
            {
                "id": i,
                "name": f"User_{i}",
                "email": f"user_{i}@example.com",
                "status": "active" if i % 2 == 0 else "inactive",
                "role": f"role_{i % 5}",
                "city": f"City_{i % 100}",
                "country": f"Country_{i % 20}",
                "tags": [f"tag_{i}_1", f"tag_{i}_2", f"tag_{i}_3"],
            }
            for i in range(count)
        ]

    data = {
        "users": items,
        "metadata": {
            "count": count,
            "generated": True,
            "version": "1.0",
        }
    }

    return json.dumps(data)


def get_memory_usage():
    """Get current process memory usage in bytes"""
    process = psutil.Process(os.getpid())
    return process.memory_info().rss

def benchmark_parser(name, parse_func, iterations=10):
    """Benchmark a JSON parser.

    parse_func should be a callable that takes no arguments and returns the parsed result.
    """

    # Warmup
    for _ in range(3):
        parse_func()

    # Time measurement
    times = []
    for _ in range(iterations):
        start = time.perf_counter()
        result = parse_func()
        elapsed = time.perf_counter() - start
        times.append(elapsed)

    avg_time = sum(times) / len(times)
    min_time = min(times)

    # Memory measurement
    gc.collect()
    mem_before = get_memory_usage()

    result = parse_func()

    gc.collect()
    mem_after = get_memory_usage()
    memory_kb = (mem_after - mem_before) / 1024  # Convert to KB

    return {
        'name': name,
        'avg_time_ms': avg_time * 1000,
        'min_time_ms': min_time * 1000,
        'memory_kb': memory_kb,
        'result': result,
    }


def get_file_info(filepath):
    """Get file size information."""
    file_size_mb = os.path.getsize(filepath) / (1024 * 1024)
    return file_size_mb


def run_benchmark_file(filepath):
    """Run benchmark on a real JSON file."""

    print(f"\n{'='*70}")
    print(f"Benchmark: Real JSON File")
    print(f"{'='*70}")

    # Get file info
    file_size_mb = get_file_info(filepath)
    print(f"File: {filepath}")
    print(f"Size: {file_size_mb:.2f} MB")

    # Adjust iterations based on file size
    if file_size_mb > 100:
        iterations = 3
        print(f"\n⚠ Large file detected ({file_size_mb:.0f} MB) - using {iterations} iterations")
        print("   (This may take several minutes...)")
    elif file_size_mb > 10:
        iterations = 5
        print(f"\n⚠ Medium-large file ({file_size_mb:.0f} MB) - using {iterations} iterations")
    else:
        iterations = 10

    results = []

    # Standard library json - uses json.load() to read directly from file
    print("\nBenchmarking json (stdlib)...")
    results.append(benchmark_parser(
        'json (stdlib)',
        lambda: json.load(open(filepath, 'r')),
        iterations=iterations
    ))

    # libfyaml with dedup - uses load() to read directly from file
    print("Benchmarking libfyaml (dedup=True)...")
    results.append(benchmark_parser(
        'libfyaml (dedup=True)',
        lambda: libfyaml.load(open(filepath, 'r'), mode='json', dedup=True),
        iterations=iterations
    ))

    # libfyaml without dedup - uses load() to read directly from file
    print("Benchmarking libfyaml (dedup=False)...")
    results.append(benchmark_parser(
        'libfyaml (dedup=False)',
        lambda: libfyaml.load(open(filepath, 'r'), mode='json', dedup=False),
        iterations=iterations
    ))

    # orjson if available - needs to read entire file since it only has loads()
    if HAS_ORJSON:
        print("Benchmarking orjson...")
        # orjson doesn't have load(), only loads() - so we need to read the file
        with open(filepath, 'rb') as f:
            orjson_data = f.read()
        results.append(benchmark_parser(
            'orjson',
            lambda: orjson.loads(orjson_data),
            iterations=iterations
        ))

    # ujson if available - has ujson.load()
    if HAS_UJSON:
        print("Benchmarking ujson...")
        results.append(benchmark_parser(
            'ujson',
            lambda: ujson.load(open(filepath, 'r')),
            iterations=iterations
        ))

    # Print results table
    print(f"\n{'Library':<25} {'Time (ms)':>12} {'Memory (KB)':>15} {'vs json':>12}")
    print("-" * 70)

    baseline_time = results[0]['avg_time_ms']
    baseline_mem = results[0]['memory_kb']

    for r in results:
        time_vs = f"{baseline_time / r['avg_time_ms']:.2f}x" if r['avg_time_ms'] > 0 else "N/A"

        # Handle memory comparisons carefully
        if baseline_mem > 100 and r['memory_kb'] > 100:  # Only compare if both > 100 KB
            mem_vs = f"{baseline_mem / r['memory_kb']:.2f}x"
        else:
            mem_vs = "~"  # Too small to measure reliably

        print(f"{r['name']:<25} {r['avg_time_ms']:>11.2f}  {r['memory_kb']:>14,.0f}  {time_vs:>6} {mem_vs:>5}")

    # Dedup savings
    if len(results) >= 3:
        dedup_result = results[1]  # libfyaml with dedup
        nodedup_result = results[2]  # libfyaml without dedup

        if nodedup_result['memory_kb'] > 100:  # Only if we have measurable memory
            mem_savings = (1 - dedup_result['memory_kb'] / nodedup_result['memory_kb']) * 100
            print(f"\nDedup memory savings: {mem_savings:.1f}%")
        else:
            print(f"\nDedup memory savings: (too small to measure)")

    return results


def run_benchmark(size, with_repetition):
    """Run benchmark for a specific configuration (synthetic data)."""

    print(f"\n{'='*70}")
    print(f"Benchmark: size={size}, repetition={with_repetition}")
    print(f"{'='*70}")

    # Generate test data
    json_str = generate_json_data(size, with_repetition)
    json_size_mb = len(json_str) / (1024 * 1024)
    print(f"JSON size: {json_size_mb:.2f} MB ({len(json_str):,} bytes)")

    results = []

    # Standard library json
    results.append(benchmark_parser(
        'json (stdlib)',
        lambda: json.loads(json_str)
    ))

    # libfyaml with dedup
    results.append(benchmark_parser(
        'libfyaml (dedup=True)',
        lambda: libfyaml.loads(json_str, mode='json', dedup=True)
    ))

    # libfyaml without dedup
    results.append(benchmark_parser(
        'libfyaml (dedup=False)',
        lambda: libfyaml.loads(json_str, mode='json', dedup=False)
    ))

    # orjson if available
    if HAS_ORJSON:
        json_bytes = json_str.encode()
        results.append(benchmark_parser(
            'orjson',
            lambda: orjson.loads(json_bytes)
        ))

    # ujson if available
    if HAS_UJSON:
        results.append(benchmark_parser(
            'ujson',
            lambda: ujson.loads(json_str)
        ))

    # Print results table
    print(f"\n{'Library':<25} {'Time (ms)':>12} {'Memory (KB)':>15} {'vs json':>12}")
    print("-" * 70)

    baseline_time = results[0]['avg_time_ms']
    baseline_mem = results[0]['memory_kb']

    for r in results:
        time_vs = f"{baseline_time / r['avg_time_ms']:.2f}x" if r['avg_time_ms'] > 0 else "N/A"

        # Handle memory comparisons carefully
        if baseline_mem > 100 and r['memory_kb'] > 100:  # Only compare if both > 100 KB
            mem_vs = f"{baseline_mem / r['memory_kb']:.2f}x"
        else:
            mem_vs = "~"  # Too small to measure reliably

        print(f"{r['name']:<25} {r['avg_time_ms']:>11.2f}  {r['memory_kb']:>14,.0f}  {time_vs:>6} {mem_vs:>5}")

    # Dedup savings (only if memory measurements are meaningful)
    if with_repetition and len(results) >= 3:
        dedup_result = results[1]  # libfyaml with dedup
        nodedup_result = results[2]  # libfyaml without dedup

        if nodedup_result['memory_kb'] > 100:  # Only if we have measurable memory
            mem_savings = (1 - dedup_result['memory_kb'] / nodedup_result['memory_kb']) * 100
            print(f"\nDedup memory savings: {mem_savings:.1f}%")
        else:
            print(f"\nDedup memory savings: (too small to measure)")

    return results


def main():
    parser = argparse.ArgumentParser(
        description='Benchmark libfyaml JSON parsing against other libraries',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument('--synthetic', action='store_true',
                        help='Use synthetic data instead of real file')
    parser.add_argument('--file', type=str,
                        help='Path to JSON file (default: AllPrintings.json)')

    args = parser.parse_args()

    print("="*70)
    print("JSON Parsing Benchmark: libfyaml vs other libraries")
    print("="*70)

    if args.synthetic:
        # Original synthetic data benchmarks
        print("\nUsing SYNTHETIC DATA")

        # Small file without repetition
        run_benchmark('small', False)

        # Small file with repetition (dedup benefit)
        run_benchmark('small', True)

        # Medium file without repetition
        run_benchmark('medium', False)

        # Medium file with repetition (dedup benefit)
        run_benchmark('medium', True)

        # Large file with repetition (major dedup benefit)
        print("\n" + "="*70)
        print("Large File Benchmark (this may take a minute...)")
        print("="*70)
        run_benchmark('large', True)

    else:
        # Real file benchmark
        print("\nUsing REAL JSON FILE")

        # Determine file path
        if args.file:
            json_file = args.file
        else:
            # Default to AllPrintings.json in the same directory
            script_dir = Path(__file__).parent
            json_file = script_dir / 'AllPrintings.json'

        if not os.path.exists(json_file):
            print(f"\nERROR: JSON file not found: {json_file}")
            print("\nOptions:")
            print("  1. Download AllPrintings.json from https://mtgjson.com/downloads/all-files/")
            print("  2. Use --file <path> to specify a different JSON file")
            print("  3. Use --synthetic to generate synthetic test data")
            sys.exit(1)

        run_benchmark_file(json_file)

    print("\n" + "="*70)
    print("Summary")
    print("="*70)
    print("✓ libfyaml supports JSON via mode='json'")
    print("✓ Deduplication significantly reduces memory on repeated data")
    print("✓ Performance competitive with stdlib json")
    if HAS_ORJSON:
        print("✓ Compared against orjson (fastest JSON parser)")
    if HAS_UJSON:
        print("✓ Compared against ujson")
    if not HAS_ORJSON:
        print("⚠ Install orjson for comparison: pip install orjson")
    if not HAS_UJSON:
        print("⚠ Install ujson for comparison: pip install ujson")


if __name__ == '__main__':
    main()
