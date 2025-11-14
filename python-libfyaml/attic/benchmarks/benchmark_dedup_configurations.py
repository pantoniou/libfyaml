#!/usr/bin/env python3
"""
Benchmark all dedup configurations with memory profiling.

Tests 4 configurations:
1. dedup=True, no trim
2. dedup=True, with trim
3. dedup=False, no trim
4. dedup=False, with trim

Measures actual memory usage via /proc/self/maps.
"""

import sys
import time
import gc
import os
sys.path.insert(0, '.')
import libfyaml


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


def get_process_rss():
    """Get process RSS from /proc/self/status."""
    try:
        with open('/proc/self/status', 'r') as f:
            for line in f:
                if line.startswith('VmRSS:'):
                    return int(line.split()[1]) * 1024
    except:
        return 0
    return 0


def format_mb(bytes_val):
    return f"{bytes_val / (1024*1024):.2f} MB"


def format_ms(seconds):
    return f"{seconds * 1000:.2f} ms"


def benchmark_config(name, use_dedup, use_trim, yaml_path, iterations=3):
    """Benchmark a specific configuration."""

    # Read file
    with open(yaml_path, 'r') as f:
        content = f.read()

    file_size = len(content.encode('utf-8'))

    times_parse = []
    times_trim = []
    memory_before_trim = []
    memory_after_trim = []
    rss_before_trim = []
    rss_after_trim = []

    for i in range(iterations):
        gc.collect()

        # Baseline
        baseline_mmap = get_anon_mmap_size()
        baseline_rss = get_process_rss()

        # Parse with dedup setting
        start_parse = time.time()
        data = libfyaml.loads(content, dedup=use_dedup)
        time_parse = time.time() - start_parse
        times_parse.append(time_parse)

        gc.collect()

        # Memory before trim
        before_mmap = get_anon_mmap_size()
        before_rss = get_process_rss()
        memory_before_trim.append(before_mmap - baseline_mmap)
        rss_before_trim.append(before_rss - baseline_rss)

        # Trim if requested
        time_trim = 0
        if use_trim:
            start_trim = time.time()
            data.trim()
            time_trim = time.time() - start_trim
            times_trim.append(time_trim)
            gc.collect()

            after_mmap = get_anon_mmap_size()
            after_rss = get_process_rss()
            memory_after_trim.append(after_mmap - baseline_mmap)
            rss_after_trim.append(after_rss - baseline_rss)
        else:
            memory_after_trim.append(before_mmap - baseline_mmap)
            rss_after_trim.append(before_rss - baseline_rss)

        # Cleanup
        del data
        gc.collect()

    # Calculate averages
    avg_parse = sum(times_parse) / len(times_parse)
    avg_trim = sum(times_trim) / len(times_trim) if times_trim else 0
    avg_total = avg_parse + avg_trim

    avg_mem_before = sum(memory_before_trim) / len(memory_before_trim)
    avg_mem_after = sum(memory_after_trim) / len(memory_after_trim)

    avg_rss_before = sum(rss_before_trim) / len(rss_before_trim)
    avg_rss_after = sum(rss_after_trim) / len(rss_after_trim)

    return {
        'name': name,
        'file_size': file_size,
        'time_parse': avg_parse,
        'time_trim': avg_trim,
        'time_total': avg_total,
        'mmap_before_trim': avg_mem_before,
        'mmap_after_trim': avg_mem_after,
        'rss_before_trim': avg_rss_before,
        'rss_after_trim': avg_rss_after,
        'use_dedup': use_dedup,
        'use_trim': use_trim,
    }


def print_results(results, baseline_result):
    """Print benchmark results."""

    file_size = baseline_result['file_size']

    print(f"\n{'='*80}")
    print(f"BENCHMARK RESULTS - {format_mb(file_size)} YAML file")
    print(f"{'='*80}")

    # Header
    print(f"\n{'Configuration':<40} {'Time':<12} {'Arena':<12} {'Savings'}")
    print(f"{'-'*80}")

    for r in results:
        name = r['name']
        time_str = format_ms(r['time_total'])

        # Show final memory state (after trim if applicable)
        arena_str = format_mb(r['mmap_after_trim'])

        # Calculate savings vs baseline
        baseline_mmap = baseline_result['mmap_before_trim']  # Baseline is before trim
        if baseline_mmap > 0:
            savings = baseline_mmap - r['mmap_after_trim']
            savings_pct = (savings / baseline_mmap * 100)
            if savings_pct > 0:
                savings_str = f"-{savings_pct:.1f}%"
            elif savings_pct < 0:
                savings_str = f"+{abs(savings_pct):.1f}%"
            else:
                savings_str = "baseline"
        else:
            savings_str = "n/a"

        print(f"{name:<40} {time_str:<12} {arena_str:<12} {savings_str}")

    # Detailed breakdown
    print(f"\n{'='*80}")
    print("DETAILED TIMING BREAKDOWN")
    print(f"{'='*80}")
    print(f"\n{'Configuration':<40} {'Parse':<12} {'Trim':<12} {'Total'}")
    print(f"{'-'*80}")

    for r in results:
        name = r['name']
        parse_str = format_ms(r['time_parse'])
        trim_str = format_ms(r['time_trim']) if r['time_trim'] > 0 else "-"
        total_str = format_ms(r['time_total'])

        print(f"{name:<40} {parse_str:<12} {trim_str:<12} {total_str}")

    # Memory impact analysis
    print(f"\n{'='*80}")
    print("MEMORY IMPACT ANALYSIS")
    print(f"{'='*80}")
    print(f"\n{'Configuration':<40} {'Before Trim':<15} {'After Trim':<15} {'Saved'}")
    print(f"{'-'*80}")

    for r in results:
        name = r['name']
        before = format_mb(r['mmap_before_trim'])
        after = format_mb(r['mmap_after_trim'])

        if r['use_trim']:
            saved = r['mmap_before_trim'] - r['mmap_after_trim']
            saved_pct = (saved / r['mmap_before_trim'] * 100) if r['mmap_before_trim'] > 0 else 0
            saved_str = f"{format_mb(saved)} ({saved_pct:.1f}%)"
        else:
            saved_str = "-"

        print(f"{name:<40} {before:<15} {after:<15} {saved_str}")


def main():
    yaml_path = 'AtomicCards-2-cleaned-small.yaml'

    if not os.path.exists(yaml_path):
        print(f"ERROR: {yaml_path} not found")
        return 1

    print("="*80)
    print("BENCHMARKING DEDUP CONFIGURATIONS")
    print("="*80)
    print(f"\nFile: {yaml_path}")
    print(f"Testing 4 configurations:")
    print(f"  1. dedup=True, no trim")
    print(f"  2. dedup=True, with trim")
    print(f"  3. dedup=False, no trim")
    print(f"  4. dedup=False, with trim")
    print(f"\nRunning 3 iterations per configuration...")

    # Run benchmarks
    results = []

    print(f"\n[1/4] dedup=True, no trim...")
    r1 = benchmark_config("dedup=True, no trim",
                         use_dedup=True, use_trim=False,
                         yaml_path=yaml_path)
    results.append(r1)

    print(f"[2/4] dedup=True, with trim...")
    r2 = benchmark_config("dedup=True, with trim",
                         use_dedup=True, use_trim=True,
                         yaml_path=yaml_path)
    results.append(r2)

    print(f"[3/4] dedup=False, no trim...")
    r3 = benchmark_config("dedup=False, no trim",
                         use_dedup=False, use_trim=False,
                         yaml_path=yaml_path)
    results.append(r3)

    print(f"[4/4] dedup=False, with trim...")
    r4 = benchmark_config("dedup=False, with trim",
                         use_dedup=False, use_trim=True,
                         yaml_path=yaml_path)
    results.append(r4)

    # Print results (baseline is dedup=True, no trim)
    print_results(results, baseline_result=r1)

    # Summary
    print(f"\n{'='*80}")
    print("KEY FINDINGS")
    print(f"{'='*80}")

    # Compare dedup impact
    dedup_true_no_trim = r1['mmap_before_trim']
    dedup_false_no_trim = r3['mmap_before_trim']
    dedup_impact = dedup_false_no_trim - dedup_true_no_trim
    dedup_impact_pct = (dedup_impact / dedup_true_no_trim * 100) if dedup_true_no_trim > 0 else 0

    print(f"\n📊 DEDUP IMPACT:")
    print(f"   Without dedup: {format_mb(dedup_false_no_trim)}")
    print(f"   With dedup:    {format_mb(dedup_true_no_trim)}")
    if dedup_impact > 0:
        print(f"   Overhead:      +{format_mb(dedup_impact)} (+{dedup_impact_pct:.1f}%)")
    else:
        print(f"   Savings:       {format_mb(abs(dedup_impact))} ({abs(dedup_impact_pct):.1f}%)")

    # Compare trim impact
    trim_savings_with_dedup = r1['mmap_before_trim'] - r2['mmap_after_trim']
    trim_savings_pct_dedup = (trim_savings_with_dedup / r1['mmap_before_trim'] * 100) if r1['mmap_before_trim'] > 0 else 0

    trim_savings_no_dedup = r3['mmap_before_trim'] - r4['mmap_after_trim']
    trim_savings_pct_no_dedup = (trim_savings_no_dedup / r3['mmap_before_trim'] * 100) if r3['mmap_before_trim'] > 0 else 0

    print(f"\n✂️  TRIM IMPACT:")
    print(f"   With dedup:    {format_mb(trim_savings_with_dedup)} ({trim_savings_pct_dedup:.1f}% savings)")
    print(f"   Without dedup: {format_mb(trim_savings_no_dedup)} ({trim_savings_pct_no_dedup:.1f}% savings)")

    # Best configuration
    print(f"\n💡 RECOMMENDATION:")

    # Find config with lowest memory
    min_memory_config = min(results, key=lambda r: r['mmap_after_trim'])
    print(f"   Lowest memory: {min_memory_config['name']}")
    print(f"   Memory usage:  {format_mb(min_memory_config['mmap_after_trim'])} ({min_memory_config['mmap_after_trim'] / r1['file_size']:.2f}x file size)")

    # Evaluate dedup usefulness
    if dedup_impact < 0:  # dedup saves memory
        print(f"\n   ✅ dedup=True saves {format_mb(abs(dedup_impact))} - RECOMMENDED")
    else:  # dedup costs memory
        print(f"\n   ⚠️  dedup=True adds {format_mb(dedup_impact)} overhead")
        print(f"   This file may not have enough repeated content to benefit from dedup")

    print(f"\n   ✅ Always call trim() after parsing - saves {trim_savings_pct_dedup:.0f}%-{trim_savings_pct_no_dedup:.0f}%")


if __name__ == '__main__':
    sys.exit(main() or 0)
