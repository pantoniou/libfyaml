#!/usr/bin/env python3
"""
Benchmark AllPrices.yaml with libfyaml and rapidyaml comparison.
"""

import sys
import time
import gc
import os
sys.path.insert(0, '.')

try:
    import libfyaml
    HAS_LIBFYAML = True
except ImportError:
    HAS_LIBFYAML = False
    print("Warning: libfyaml not available")

try:
    import ryml
    HAS_RAPIDYAML = True
except ImportError:
    HAS_RAPIDYAML = False
    print("Warning: rapidyaml not available")


def get_anon_mmap_size():
    """Get total size of anonymous mmap regions > 1MB."""
    try:
        with open('/proc/self/maps', 'r') as f:
            maps = f.read()

        total = 0
        for line in maps.split('\n'):
            if not line:
                continue
            parts = line.split()
            if len(parts) >= 6:
                continue  # Has path
            if len(parts) >= 2 and 'rw' in parts[1]:
                start, end = parts[0].split('-')
                size = int(end, 16) - int(start, 16)
                if size > 1024 * 1024:  # > 1MB
                    total += size
        return total
    except:
        return 0


def format_size(bytes_val):
    if bytes_val > 1024*1024*1024:
        return f"{bytes_val / (1024*1024*1024):.2f} GB"
    else:
        return f"{bytes_val / (1024*1024):.2f} MB"


def format_time(seconds):
    if seconds < 1.0:
        return f"{seconds * 1000:.2f} ms"
    else:
        return f"{seconds:.2f} s"


def benchmark_libfyaml(yaml_path, dedup=True, trim=True):
    """Benchmark libfyaml parsing."""
    print(f"\n{'='*70}")
    print(f"Benchmarking libfyaml (dedup={dedup}, trim={trim})")
    print(f"{'='*70}")

    # Read file
    print(f"  Reading file...")
    with open(yaml_path, 'r') as f:
        content = f.read()

    file_size = len(content.encode('utf-8'))
    print(f"  File size: {format_size(file_size)}")

    gc.collect()
    baseline_mmap = get_anon_mmap_size()

    # Parse
    print(f"  Parsing...")
    start = time.time()
    data = libfyaml.loads(content, dedup=dedup, trim=False)  # Don't auto-trim for accurate measurement
    parse_time = time.time() - start
    print(f"  Parse time: {format_time(parse_time)}")

    gc.collect()
    after_parse = get_anon_mmap_size()
    memory_before_trim = after_parse - baseline_mmap
    print(f"  Memory before trim: {format_size(memory_before_trim)}")

    # Trim if requested
    trim_time = 0
    if trim:
        print(f"  Trimming...")
        start = time.time()
        data.trim()
        trim_time = time.time() - start
        gc.collect()

        after_trim = get_anon_mmap_size()
        memory_after_trim = after_trim - baseline_mmap
        print(f"  Memory after trim: {format_size(memory_after_trim)}")
        print(f"  Trim time: {format_time(trim_time)}")
        saved = memory_before_trim - memory_after_trim
        saved_pct = (saved / memory_before_trim * 100) if memory_before_trim > 0 else 0
        print(f"  Trim saved: {format_size(saved)} ({saved_pct:.1f}%)")
    else:
        memory_after_trim = memory_before_trim

    total_time = parse_time + trim_time

    # Cleanup
    del data
    gc.collect()

    return {
        'library': 'libfyaml',
        'dedup': dedup,
        'trim': trim,
        'file_size': file_size,
        'parse_time': parse_time,
        'trim_time': trim_time,
        'total_time': total_time,
        'memory_before_trim': memory_before_trim,
        'memory_after_trim': memory_after_trim,
    }


def benchmark_rapidyaml(yaml_path):
    """Benchmark rapidyaml parsing."""
    print(f"\n{'='*70}")
    print(f"Benchmarking rapidyaml")
    print(f"{'='*70}")

    # Read file
    print(f"  Reading file...")
    with open(yaml_path, 'r') as f:
        content = f.read()

    file_size = len(content.encode('utf-8'))
    print(f"  File size: {format_size(file_size)}")

    gc.collect()
    baseline_mmap = get_anon_mmap_size()

    # Parse
    print(f"  Parsing...")
    start = time.time()
    tree = ryml.parse_in_arena(content)
    parse_time = time.time() - start
    print(f"  Parse time: {format_time(parse_time)}")

    gc.collect()
    after_parse = get_anon_mmap_size()
    memory_used = after_parse - baseline_mmap
    print(f"  Memory used: {format_size(memory_used)}")

    # Cleanup
    del tree
    gc.collect()

    return {
        'library': 'rapidyaml',
        'file_size': file_size,
        'parse_time': parse_time,
        'total_time': parse_time,
        'memory_used': memory_used,
    }


def print_comparison(results):
    """Print comparison table."""
    print(f"\n{'='*80}")
    print(f"COMPARISON RESULTS - {format_size(results[0]['file_size'])} YAML file")
    print(f"{'='*80}")

    print(f"\n{'Library':<25} {'Parse Time':<15} {'Memory':<15} {'vs Fastest'}")
    print("-" * 80)

    # Find fastest
    fastest_time = min(r['parse_time'] for r in results)

    for r in sorted(results, key=lambda x: x['parse_time']):
        lib_name = r['library']
        if r['library'] == 'libfyaml':
            lib_name += f" (dedup={r['dedup']}, trim={r['trim']})"

        time_str = format_time(r['parse_time'])

        if 'memory_after_trim' in r and r['trim']:
            mem_str = format_size(r['memory_after_trim'])
        elif 'memory_before_trim' in r:
            mem_str = format_size(r['memory_before_trim'])
        else:
            mem_str = format_size(r['memory_used'])

        ratio = r['parse_time'] / fastest_time
        if ratio == 1.0:
            ratio_str = "FASTEST"
        else:
            ratio_str = f"{ratio:.2f}x slower"

        print(f"{lib_name:<25} {time_str:<15} {mem_str:<15} {ratio_str}")


    # Key comparisons
    print(f"\n{'='*80}")
    print("KEY COMPARISONS")
    print(f"{'='*80}")

    # Find rapidyaml and best libfyaml
    rapidyaml_result = next((r for r in results if r['library'] == 'rapidyaml'), None)
    libfyaml_best = min((r for r in results if r['library'] == 'libfyaml'),
                        key=lambda x: x['memory_after_trim'])

    if rapidyaml_result and libfyaml_best:
        print(f"\nrapidyaml vs libfyaml (best config):")

        time_ratio = rapidyaml_result['parse_time'] / libfyaml_best['parse_time']
        if time_ratio < 1:
            print(f"  Parse speed: rapidyaml is {1/time_ratio:.2f}x FASTER")
        else:
            print(f"  Parse speed: rapidyaml is {time_ratio:.2f}x slower")

        rapidyaml_mem = rapidyaml_result['memory_used']
        libfyaml_mem = libfyaml_best['memory_after_trim']
        mem_ratio = rapidyaml_mem / libfyaml_mem
        if mem_ratio < 1:
            print(f"  Memory: rapidyaml uses {1/mem_ratio:.2f}x LESS ({format_size(rapidyaml_mem)} vs {format_size(libfyaml_mem)})")
        else:
            print(f"  Memory: rapidyaml uses {mem_ratio:.2f}x MORE ({format_size(rapidyaml_mem)} vs {format_size(libfyaml_mem)})")

    # libfyaml configurations comparison
    libfyaml_results = [r for r in results if r['library'] == 'libfyaml']
    if len(libfyaml_results) >= 2:
        print(f"\nlibfyaml configurations:")
        dedup_true = next((r for r in libfyaml_results if r['dedup'] and r['trim']), None)
        dedup_false = next((r for r in libfyaml_results if not r['dedup'] and r['trim']), None)

        if dedup_true and dedup_false:
            time_diff = dedup_true['parse_time'] - dedup_false['parse_time']
            time_diff_pct = (time_diff / dedup_false['parse_time'] * 100)

            mem_diff = dedup_true['memory_after_trim'] - dedup_false['memory_after_trim']
            mem_diff_pct = (mem_diff / dedup_false['memory_after_trim'] * 100) if dedup_false['memory_after_trim'] > 0 else 0

            print(f"  dedup=True vs dedup=False:")
            print(f"    Time: {format_time(abs(time_diff))} ({'slower' if time_diff > 0 else 'faster'}, {abs(time_diff_pct):.1f}%)")
            print(f"    Memory: {format_size(abs(mem_diff))} ({'more' if mem_diff > 0 else 'less'}, {abs(mem_diff_pct):.1f}%)")


def main():
    yaml_file = "AllPrices.yaml"

    if not os.path.exists(yaml_file):
        print(f"ERROR: {yaml_file} not found")
        return 1

    file_size = os.path.getsize(yaml_file)

    print("="*80)
    print("ALLPRICES.YAML BENCHMARK - libfyaml vs rapidyaml")
    print("="*80)
    print(f"\nFile: {yaml_file}")
    print(f"Size: {format_size(file_size)}")
    print(f"\nThis will take several minutes...")

    results = []

    # Benchmark rapidyaml
    if HAS_RAPIDYAML:
        print(f"\n[1/3] Running rapidyaml benchmark...")
        r = benchmark_rapidyaml(yaml_file)
        results.append(r)
    else:
        print(f"\n[SKIP] rapidyaml not available")

    # Benchmark libfyaml with dedup
    if HAS_LIBFYAML:
        print(f"\n[2/3] Running libfyaml benchmark (dedup=True, trim=True)...")
        r = benchmark_libfyaml(yaml_file, dedup=True, trim=True)
        results.append(r)

        print(f"\n[3/3] Running libfyaml benchmark (dedup=False, trim=True)...")
        r = benchmark_libfyaml(yaml_file, dedup=False, trim=True)
        results.append(r)
    else:
        print(f"\n[SKIP] libfyaml not available")

    # Print comparison
    if results:
        print_comparison(results)


if __name__ == '__main__':
    sys.exit(main() or 0)
