#!/usr/bin/env python3
"""
Minimal YAML cat - reads YAML and outputs to stdout
Usage: yaml_cat.py <file.yaml>
"""

import sys
sys.path.insert(0, '/mnt/980-linux/panto/work/sandbox/libfyaml-generics-docs/python-libfyaml')
import libfyaml

if len(sys.argv) != 2:
    print("Usage: yaml_cat.py <file.yaml>", file=sys.stderr)
    sys.exit(1)

# Load and dump
data = libfyaml.load(sys.argv[1])
print(libfyaml.dumps(data), end='')
