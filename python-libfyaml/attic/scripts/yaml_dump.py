#!/usr/bin/env python3
"""
Simple YAML dumper using libfyaml
Reads a YAML file and dumps it to stdout
"""

import sys
import os

# Add libfyaml to path
sys.path.insert(0, '/mnt/980-linux/panto/work/sandbox/libfyaml-generics-docs/python-libfyaml')
import libfyaml

def main():
    if len(sys.argv) < 2:
        print("Usage: yaml_dump.py <yaml_file> [options]", file=sys.stderr)
        print("", file=sys.stderr)
        print("Options:", file=sys.stderr)
        print("  --json         Output as JSON instead of YAML", file=sys.stderr)
        print("  --compact      Use compact/flow style", file=sys.stderr)
        print("  --no-dedup     Disable deduplication", file=sys.stderr)
        print("", file=sys.stderr)
        print("Examples:", file=sys.stderr)
        print("  yaml_dump.py config.yaml", file=sys.stderr)
        print("  yaml_dump.py data.yaml --json", file=sys.stderr)
        print("  yaml_dump.py large.yaml --compact", file=sys.stderr)
        sys.exit(1)

    yaml_file = sys.argv[1]

    # Parse options
    json_mode = '--json' in sys.argv
    compact = '--compact' in sys.argv
    dedup = '--no-dedup' not in sys.argv

    # Check file exists
    if not os.path.exists(yaml_file):
        print(f"Error: File not found: {yaml_file}", file=sys.stderr)
        sys.exit(1)

    try:
        # Load YAML file using mmap for efficiency
        data = libfyaml.load(yaml_file, mode='yaml', mutable=False, dedup=dedup)

        # Dump to stdout
        output = libfyaml.dumps(data, compact=compact, json=json_mode)
        print(output, end='')

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main()
