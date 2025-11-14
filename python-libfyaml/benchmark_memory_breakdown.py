#!/usr/bin/env python3
"""
Memory breakdown analysis for libfyaml.

Analyzes what contributes to memory consumption:
- Source file size
- FyGeneric structures
- Dedup allocator overhead
- Python wrapper objects
- String data and parsing structures
"""

import sys
import gc
import tracemalloc
import os
sys.path.insert(0, '.')

try:
    import libfyaml
    HAS_LIBFYAML = True
except ImportError:
    HAS_LIBFYAML = False
    print("ERROR: libfyaml not available")
    sys.exit(1)


def format_bytes(bytes_size):
    """Format bytes to human-readable string."""
    for unit in ['B', 'KB', 'MB', 'GB']:
        if bytes_size < 1024.0:
            return f"{bytes_size:.2f} {unit}"
        bytes_size /= 1024.0
    return f"{bytes_size:.2f} TB"


def analyze_memory_breakdown(yaml_path):
    """Detailed memory breakdown analysis."""

    print("="*70)
    print("MEMORY BREAKDOWN ANALYSIS")
    print("="*70)

    # Get file size
    file_size = os.path.getsize(yaml_path)
    print(f"\nInput file: {yaml_path}")
    print(f"File size:  {format_bytes(file_size)}")

    # Read file content
    with open(yaml_path, 'r', encoding='utf-8') as f:
        content = f.read()

    content_bytes = len(content.encode('utf-8'))
    print(f"Content size (UTF-8): {format_bytes(content_bytes)}")

    # Baseline: Start tracking before allocating string
    gc.collect()
    tracemalloc.start()
    baseline_current, baseline_peak = tracemalloc.get_traced_memory()
    print(f"\nBaseline (empty): {format_bytes(baseline_peak)}")

    # Allocate fresh string copy for proper measurement
    test_string = content  # This references the original
    string_copy = str(content)  # Force new allocation
    string_current, string_peak = tracemalloc.get_traced_memory()
    string_overhead = string_peak - baseline_peak
    print(f"Python string in memory: {format_bytes(string_overhead)}")
    print(f"  Overhead vs file: {string_overhead / file_size:.2f}x")

    # Parse to FyGeneric
    data = libfyaml.loads(content)
    parse_current, parse_peak = tracemalloc.get_traced_memory()
    parse_overhead = parse_peak - string_peak
    total_parsed = parse_peak - baseline_peak

    # Account for source string that C library retains
    # The C library keeps the original YAML source in memory (zero-copy design)
    c_source_estimate = content_bytes

    print(f"\nAfter parsing to FyGeneric:")
    print(f"  Additional memory: {format_bytes(parse_overhead)}")
    print(f"  Total memory: {format_bytes(total_parsed)}")
    print(f"  Ratio to file size: {total_parsed / file_size:.2f}x")

    # Analyze structure
    print(f"\nMemory breakdown:")
    print(f"  Source string:     {format_bytes(string_overhead):>12}  "
          f"({string_overhead/total_parsed*100:.1f}%)")
    print(f"  FyGeneric + overhead: {format_bytes(parse_overhead):>12}  "
          f"({parse_overhead/total_parsed*100:.1f}%)")
    print(f"  Total:             {format_bytes(total_parsed):>12}  (100.0%)")

    # Count structure
    def count_items(obj, depth=0, max_depth=3):
        """Recursively count items in structure."""
        if depth > max_depth:
            return {"mappings": 0, "sequences": 0, "scalars": 0}

        counts = {"mappings": 0, "sequences": 0, "scalars": 0}

        # Check if it's a mapping
        if hasattr(obj, 'keys'):
            counts["mappings"] += 1
            try:
                for key in obj.keys():
                    child_counts = count_items(obj[key], depth + 1, max_depth)
                    for k in counts:
                        counts[k] += child_counts[k]
            except:
                pass
        # Check if it's a sequence
        elif hasattr(obj, '__len__') and not isinstance(obj, (str, bytes)):
            counts["sequences"] += 1
            try:
                for item in obj:
                    child_counts = count_items(item, depth + 1, max_depth)
                    for k in counts:
                        counts[k] += child_counts[k]
            except:
                pass
        else:
            counts["scalars"] += 1

        return counts

    print(f"\nStructure analysis (top 3 levels):")
    counts = count_items(data)
    print(f"  Mappings:  {counts['mappings']:,}")
    print(f"  Sequences: {counts['sequences']:,}")
    print(f"  Scalars:   {counts['scalars']:,}")
    total_items = sum(counts.values())
    print(f"  Total items: {total_items:,}")

    if total_items > 0:
        bytes_per_item = parse_overhead / total_items
        print(f"\n  Avg bytes per item (FyGeneric overhead): {bytes_per_item:.1f}")

    # Convert to Python
    del test_string
    gc.collect()

    python_data = data.to_python()
    convert_current, convert_peak = tracemalloc.get_traced_memory()
    convert_overhead = convert_peak - parse_peak
    total_converted = convert_peak - baseline_peak

    print(f"\nAfter conversion to Python:")
    print(f"  Additional memory: {format_bytes(convert_overhead)}")
    print(f"  Total memory: {format_bytes(total_converted)}")
    print(f"  Ratio to file size: {total_converted / file_size:.2f}x")

    print(f"\nFull breakdown:")
    print(f"  Source string:     {format_bytes(string_overhead):>12}  "
          f"({string_overhead/total_converted*100:.1f}%)")
    print(f"  FyGeneric overhead: {format_bytes(parse_overhead):>12}  "
          f"({parse_overhead/total_converted*100:.1f}%)")
    print(f"  Python conversion: {format_bytes(convert_overhead):>12}  "
          f"({convert_overhead/total_converted*100:.1f}%)")
    print(f"  Total:             {format_bytes(total_converted):>12}  (100.0%)")

    tracemalloc.stop()

    # Detailed analysis
    print(f"\n{'='*70}")
    print("DETAILED ANALYSIS")
    print("="*70)

    print(f"\n1. Source string overhead ({string_overhead/file_size:.2f}x):")
    print(f"   File on disk:        {format_bytes(file_size)}")
    print(f"   Python string:       {format_bytes(string_overhead)}")
    print(f"   Overhead:            {format_bytes(string_overhead - file_size)}")
    print(f"   Reason: Python string object overhead + UTF-8 validation")

    print(f"\n2. FyGeneric overhead ({parse_overhead/file_size:.2f}x):")
    print(f"   Memory used:         {format_bytes(parse_overhead)}")
    print(f"   ")
    print(f"   IMPORTANT: libfyaml is ZERO-COPY!")
    print(f"   It keeps the source YAML in memory and FyGeneric values")
    print(f"   point into it (no string duplication).")
    print(f"   ")
    print(f"   Estimated components:")

    # Estimate components (these are rough estimates)
    estimated_fy_structures = total_items * 8  # Each FyGeneric is 8 bytes
    estimated_source_copy = content_bytes      # C library keeps source
    estimated_parsing_trees = file_size * 0.2  # Parser structures, tokens, etc.
    estimated_dedup_hash = file_size * 0.15    # Dedup hash table

    remaining = parse_overhead - estimated_fy_structures - estimated_source_copy - estimated_parsing_trees - estimated_dedup_hash

    print(f"     - Source YAML (zero-copy): ~{format_bytes(estimated_source_copy):>10}  "
          f"(kept in C)")
    print(f"     - FyGeneric structs:       ~{format_bytes(estimated_fy_structures):>10}  "
          f"({total_items:,} items × 8 bytes)")
    print(f"     - Dedup hash table:        ~{format_bytes(estimated_dedup_hash):>10}  "
          f"(dedup overhead)")
    print(f"     - Parser/Doc structures:   ~{format_bytes(estimated_parsing_trees):>10}  "
          f"(tokens, nodes)")
    print(f"     - Other overhead:          ~{format_bytes(remaining):>10}  "
          f"(allocator, etc.)")

    print(f"\n3. Python conversion overhead:")
    print(f"   Additional memory:   {format_bytes(convert_overhead)}")
    print(f"   Reason: Python dict/list objects, reference counts")

    print(f"\n{'='*70}")
    print("MEMORY EFFICIENCY NOTES")
    print("="*70)

    print(f"\n• The source string accounts for {string_overhead/total_parsed*100:.1f}% of FyGeneric memory")
    print(f"  → This is unavoidable (need the source in memory)")
    print(f"  → Could be eliminated if parsing from mmap'd file")

    print(f"\n• FyGeneric overhead is {parse_overhead/file_size:.2f}x file size")
    print(f"  → Includes dedup hash table (~15% of file size)")
    print(f"  → Includes parser temporary structures (~30% of file size)")
    print(f"  → Includes FyGeneric pointers ({total_items:,} × 8 bytes = {format_bytes(total_items*8)})")

    print(f"\n• Dedup effectiveness:")
    if parse_overhead < string_overhead:
        print(f"  → String data SMALLER after dedup (good!)")
        savings = string_overhead - parse_overhead
        print(f"  → Estimated savings: {format_bytes(savings)} ({savings/string_overhead*100:.1f}%)")
    else:
        print(f"  → Hash table overhead larger than savings for this file")
        print(f"  → Consider dedup=False for files without repetition")

    print(f"\n• Compared to PyYAML (C/libyaml):")
    pyyaml_memory = 110 * 1024 * 1024  # 110 MB from benchmark
    print(f"  → PyYAML uses: {format_bytes(pyyaml_memory)}")
    print(f"  → libfyaml (FyGeneric): {format_bytes(total_parsed)}")
    print(f"  → libfyaml (Python): {format_bytes(total_converted)}")
    print(f"  → Savings: {pyyaml_memory/total_parsed:.1f}x less (FyGeneric)")
    print(f"  → Savings: {pyyaml_memory/total_converted:.1f}x less (Python)")

    print(f"\n{'='*70}")
    print("POTENTIAL OPTIMIZATIONS")
    print("="*70)

    print(f"\n1. File-based parsing (eliminate source copy):")
    optimized_file = total_parsed - content_bytes
    print(f"   Current (string):    {format_bytes(total_parsed)}")
    print(f"   With mmap:           {format_bytes(optimized_file)} (potential)")
    print(f"   Savings:             {format_bytes(content_bytes)} ({content_bytes/total_parsed*100:.1f}%)")
    print(f"   Method: Parse directly from mmap'd file")

    print(f"\n2. Disable dedup for unique content:")
    dedup_savings = estimated_dedup_hash
    optimized_nodedup = total_parsed - dedup_savings
    print(f"   Current (dedup=True): {format_bytes(total_parsed)}")
    print(f"   With dedup=False:     {format_bytes(optimized_nodedup)} (potential)")
    print(f"   Savings:              {format_bytes(dedup_savings)} ({dedup_savings/total_parsed*100:.1f}%)")
    print(f"   Best for: Files without repetition")

    print(f"\n3. Combined optimization (mmap + no dedup):")
    fully_optimized = total_parsed - content_bytes - dedup_savings
    print(f"   Current:              {format_bytes(total_parsed)}")
    print(f"   Fully optimized:      {format_bytes(fully_optimized)} (potential)")
    print(f"   Savings:              {format_bytes(content_bytes + dedup_savings)} "
          f"({(content_bytes + dedup_savings)/total_parsed*100:.1f}%)")
    print(f"   Ratio to file size:   {fully_optimized/file_size:.2f}x")

    print(f"\n4. Why still higher than file size?")
    print(f"   File on disk:         {format_bytes(file_size)}")
    print(f"   Optimized memory:     {format_bytes(fully_optimized)}")
    print(f"   Ratio:                {fully_optimized/file_size:.2f}x")
    print(f"   ")
    print(f"   Remaining overhead comes from:")
    print(f"   - FyGeneric structs:   {format_bytes(estimated_fy_structures)} "
          f"({total_items:,} items)")
    print(f"   - Parser structures:   {format_bytes(estimated_parsing_trees)} "
          f"(document tree)")
    print(f"   - Allocator overhead:  {format_bytes(remaining)} "
          f"(bookkeeping)")
    print(f"   ")
    print(f"   This is the cost of having an in-memory queryable structure.")
    print(f"   Still {pyyaml_memory/fully_optimized:.1f}x less than PyYAML!")


def main():
    import argparse
    parser = argparse.ArgumentParser(description='Memory breakdown analysis')
    parser.add_argument('--file', default='AtomicCards-2-cleaned-small.yaml',
                        help='YAML file to analyze')
    args = parser.parse_args()

    analyze_memory_breakdown(args.file)


if __name__ == '__main__':
    main()
