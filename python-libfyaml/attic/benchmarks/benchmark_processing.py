#!/usr/bin/env python3
"""
Benchmark with actual data processing: Calculate average manaValue across all cards.

This measures parse + process time for a realistic workload.
"""

import sys
import time
import gc
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


def format_time(seconds):
    """Format seconds to human-readable string."""
    if seconds < 1:
        return f"{seconds * 1000:.2f} ms"
    else:
        return f"{seconds:.2f} s"


def process_libfyaml(path):
    """Parse with libfyaml and calculate average manaValue."""
    # Parse
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()
    doc = libfyaml.loads(content)

    # Process: calculate average manaValue
    total = 0
    count = 0

    # The structure is: {meta: ..., data: {card_name: [variations], ...}}
    data_section = doc['data']

    for card_name in data_section.keys():
        variations = data_section[card_name]
        for variation in variations:
            # Check if manaValue exists
            if 'manaValue' in variation:
                mana = variation['manaValue']
                # Direct arithmetic - no casting needed!
                total = total + mana
                count = count + 1

    avg = total / count if count > 0 else 0
    return avg, count


def process_rapidyaml(path):
    """Parse with rapidyaml and calculate average manaValue."""
    # Parse
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()
    tree = ryml.parse_in_arena(content)

    # Process: calculate average manaValue
    total = 0.0
    count = 0

    # Navigate the tree structure
    root_id = tree.root_id()

    # Find 'data' node
    data_node = None
    for child_id in ryml.children(tree, root_id):
        key = tree.key(child_id)
        if key == b'data':
            data_node = child_id
            break

    if data_node is None:
        return 0, 0

    # Iterate over card names
    for card_id in ryml.children(tree, data_node):
        # Each card has variations (sequence)
        for variation_id in ryml.children(tree, card_id):
            # Look for manaValue in this variation
            for field_id in ryml.children(tree, variation_id):
                key = tree.key(field_id)
                if key == b'manaValue':
                    val = tree.val(field_id)
                    try:
                        mana = float(val)
                        total += mana
                        count += 1
                    except:
                        pass
                    break

    avg = total / count if count > 0 else 0
    return avg, count


def benchmark_processing(library_name, process_func, yaml_file, iterations=3):
    """Benchmark parse + process time."""
    print(f"\n{'='*70}")
    print(f"Benchmarking {library_name} (Parse + Process)")
    print(f"{'='*70}")

    times = []

    for i in range(iterations):
        # Force GC
        gc.collect()

        # Time parse + process
        start = time.time()
        avg, count = process_func(yaml_file)
        elapsed = time.time() - start

        times.append(elapsed)

        print(f"  Run {i+1}/{iterations}: {format_time(elapsed)} "
              f"(avg manaValue: {avg:.4f}, {count} cards)")

    # Calculate statistics
    avg_time = sum(times) / len(times)
    min_time = min(times)
    max_time = max(times)

    print(f"\n  Summary:")
    print(f"    Avg time:     {format_time(avg_time)}")
    print(f"    Min time:     {format_time(min_time)}")
    print(f"    Max time:     {format_time(max_time)}")
    print(f"    Consistency:  {((max_time - min_time) / avg_time * 100):.1f}% variation")

    return avg_time


def main():
    yaml_file = "AtomicCards-2-cleaned-small.yaml"

    print("="*70)
    print("YAML PROCESSING BENCHMARK")
    print("Task: Calculate average manaValue across all cards")
    print("="*70)
    print(f"File: {yaml_file}")

    import os
    file_size = os.path.getsize(yaml_file)
    print(f"Size: {file_size / (1024*1024):.2f} MB")
    print(f"Iterations: 3")

    results = {}

    # Benchmark libfyaml
    if HAS_LIBFYAML:
        avg_time = benchmark_processing("libfyaml", process_libfyaml, yaml_file)
        results['libfyaml'] = avg_time

    # Benchmark rapidyaml
    if HAS_RAPIDYAML:
        avg_time = benchmark_processing("rapidyaml", process_rapidyaml, yaml_file)
        results['rapidyaml'] = avg_time

    # Print comparison
    if results:
        print(f"\n{'='*70}")
        print("COMPARISON")
        print(f"{'='*70}")

        fastest = min(results.items(), key=lambda x: x[1])

        print(f"\n{'Library':<20} {'Time':<20} {'vs Fastest'}")
        print("-" * 70)

        for name, time_val in sorted(results.items(), key=lambda x: x[1]):
            ratio = time_val / fastest[1]
            indicator = "FASTEST" if ratio == 1.0 else f"{ratio:.2f}x"
            print(f"{name:<20} {format_time(time_val):<20} {indicator}")

        print(f"\n🏆 Fastest: {fastest[0]} ({format_time(fastest[1])})")

        if 'libfyaml' in results and 'rapidyaml' in results:
            ratio = results['libfyaml'] / results['rapidyaml']
            if ratio > 1:
                print(f"\nrapidyaml is {ratio:.2f}x faster than libfyaml")
            else:
                print(f"\nlibfyaml is {1/ratio:.2f}x faster than rapidyaml")


if __name__ == '__main__':
    main()
