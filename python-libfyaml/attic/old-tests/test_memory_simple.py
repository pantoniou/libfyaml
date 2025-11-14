#!/usr/bin/env python3
"""Simple test for Valgrind massif profiling."""
import sys
sys.path.insert(0, '.')
import libfyaml

# Load the YAML file
with open('AtomicCards-2-cleaned-small.yaml', 'r') as f:
    content = f.read()

# Parse it
data = libfyaml.loads(content)

# Touch it to keep it alive
print(f"Parsed successfully, type: {type(data)}")

# Keep alive for snapshot
input("Press Enter to exit (so massif can snapshot)...")
