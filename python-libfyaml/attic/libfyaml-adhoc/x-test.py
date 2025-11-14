#!/usr/bin/env python3
"""
Benchmark comparison: libfyaml vs PyYAML vs ruamel.yaml

Tests parsing performance and memory usage on AtomicCards-2.yaml (105MB).
"""

import sys
import time

import yaml as pyyaml

def load():
    with open('y.yaml', 'rb') as f:  # Read as binary
        return pyyaml.safe_load(f)

def main():
    data = load()

if __name__ == '__main__':
    main()

