#!/usr/bin/env python3
"""
Parallel map/reduce benchmark for Python
Usage: python3 python-parallel-bench.py <yaml-file> [n-processes]
"""

import sys
import time
import yaml
from multiprocessing import Pool
import threading

def process_item(item):
    """Process a single item - add metadata"""
    if isinstance(item, dict):
        item = item.copy()  # Don't modify original
        item['processed'] = True
        item['thread-id'] = threading.current_thread().ident
    return item

def main(filename, n_processes=8):
    # Parse YAML
    start_parse = time.perf_counter()
    with open(filename, 'r') as f:
        data = yaml.safe_load(f)
    parse_time = (time.perf_counter() - start_parse) * 1000

    # Parallel map over sequence
    start_parallel = time.perf_counter()
    if isinstance(data, list):
        with Pool(processes=n_processes) as pool:
            processed = pool.map(process_item, data)
    else:
        processed = data
    parallel_time = (time.perf_counter() - start_parallel) * 1000

    # Emit back to YAML
    start_emit = time.perf_counter()
    output = yaml.dump(processed, default_flow_style=False)
    emit_time = (time.perf_counter() - start_emit) * 1000

    # Print YAML output to stdout
    print(output)

    # Print timings to stderr
    print(f"Parse:    {parse_time:.2f} ms", file=sys.stderr)
    print(f"Parallel: {parallel_time:.2f} ms ({len(data) if isinstance(data, list) else 0} items)", file=sys.stderr)
    print(f"Emit:     {emit_time:.2f} ms", file=sys.stderr)
    print(f"Total:    {parse_time + parallel_time + emit_time:.2f} ms", file=sys.stderr)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 python-parallel-bench.py <yaml-file> [n-processes]")
        sys.exit(1)
    
    filename = sys.argv[1]
    n_processes = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    
    main(filename, n_processes)
