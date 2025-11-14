#!/usr/bin/env python3
"""
Simple YAML benchmark using PyYAML
Usage: python3 yaml-bench.py <file.yaml>
"""

import sys
import time
import yaml

if len(sys.argv) < 2:
    print("Usage: python3 yaml-bench.py <yaml-file>")
    sys.exit(1)

filename = sys.argv[1]

# Read YAML
start_read = time.perf_counter()
with open(filename, 'r') as f:
    data = yaml.safe_load(f)
read_time = (time.perf_counter() - start_read) * 1000

# Write YAML
start_write = time.perf_counter()
output = yaml.dump(data, default_flow_style=False)
write_time = (time.perf_counter() - start_write) * 1000

print(output)
print(f"\nRead:  {read_time:.2f} ms", file=sys.stderr)
print(f"Write: {write_time:.2f} ms", file=sys.stderr)
print(f"Total: {read_time + write_time:.2f} ms", file=sys.stderr)
