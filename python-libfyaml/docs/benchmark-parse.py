#!/usr/bin/env python3
"""
Parse benchmark: libfyaml (dedup on/off) vs PyYAML safe_load vs PyYAML CLoader.

Usage:
    python3 docs/benchmark-parse.py <yaml-file> [--runs N] [--multi]

Each configuration is measured in an isolated subprocess so that allocations
from earlier runs cannot affect later ones.  All libraries are imported before
the RSS baseline is taken so that library load cost is excluded from the delta;
the delta reflects only the memory added by parsing the file itself.

Use --multi for files containing multiple YAML documents (separated by ---).
"""

import argparse
import json
import os
import statistics
import subprocess
import sys

# ---------------------------------------------------------------------------
# Worker — runs inside each subprocess
# ---------------------------------------------------------------------------
_WORKER = """
import time, json, sys

def _rss_kb():
    with open("/proc/self/status") as f:
        for line in f:
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    return 0

mode  = sys.argv[1]
file  = sys.argv[2]
runs  = int(sys.argv[3])
multi = sys.argv[4] == "1"

# Pre-import all libraries before measuring the baseline so that the import
# cost is excluded from the RSS delta.  Without this, libfyaml's ~50 MB .so
# would be counted in the delta for fy: modes while PyYAML's much smaller
# yaml.so is pre-loaded via _patch_pyyaml() — making the comparison unfair.
import yaml
from yaml import SafeLoader, CLoader

# YAML 1.1 treats the bare '=' scalar as tag:yaml.org,2002:value (the
# "default value" indicator).  PyYAML's SafeLoader / CLoader have no
# constructor for it, so files that contain '=' as a plain enum value
# (valid in YAML 1.2 and common in Kubernetes CRDs) would raise a
# ConstructorError.  Register a constructor that returns it as a string.
tag = 'tag:yaml.org,2002:value'
_handler = lambda loader, node: loader.construct_scalar(node)
for _Loader in (SafeLoader, CLoader):
    yaml.add_constructor(tag, _handler, Loader=_Loader)

if mode.startswith("fy:"):
    import libfyaml as fy

baseline = _rss_kb()
times, peaks = [], []

for _ in range(runs):
    if mode == "pyyaml":
        t0 = time.perf_counter()
        with open(file) as f:
            doc = list(yaml.safe_load_all(f)) if multi else yaml.safe_load(f)
        elapsed = time.perf_counter() - t0
    elif mode == "pyyaml-c":
        t0 = time.perf_counter()
        with open(file) as f:
            doc = list(yaml.load_all(f, Loader=CLoader)) if multi else yaml.load(f, Loader=CLoader)
        elapsed = time.perf_counter() - t0
    elif mode.startswith("fy:"):
        kw = json.loads(mode[3:])
        t0 = time.perf_counter()
        doc = fy.load_all(file, **kw) if multi else fy.load(file, **kw)
        elapsed = time.perf_counter() - t0
    else:
        raise ValueError(f"unknown mode {mode!r}")
    times.append(elapsed)
    peaks.append(_rss_kb())
    del doc

import statistics
print(json.dumps({
    "median_ms":   statistics.median(times) * 1000,
    "min_ms":      min(times) * 1000,
    "peak_rss_mb": statistics.median(peaks) / 1024,
    "delta_mb":    (statistics.median(peaks) - baseline) / 1024,
}))
"""

# ---------------------------------------------------------------------------
# Benchmark driver
# ---------------------------------------------------------------------------

def run_config(mode, file, runs, multi):
    r = subprocess.run(
        [sys.executable, "-c", _WORKER, mode, file, str(runs), "1" if multi else "0"],
        capture_output=True, text=True,
    )
    if r.returncode != 0:
        return None, r.stderr.strip()
    return json.loads(r.stdout), None


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("file", help="YAML file to parse")
    parser.add_argument("--runs", type=int, default=5,
                        help="number of timed runs per configuration (default: 5)")
    parser.add_argument("--multi", action="store_true",
                        help="use load_all / safe_load_all for multi-document files")
    args = parser.parse_args()

    if not os.path.exists(args.file):
        sys.exit(f"error: file not found: {args.file}")

    size_mb = os.path.getsize(args.file) / 1024 / 1024

    configs = [
        ("pyyaml safe_load",               "pyyaml"),
        ("pyyaml CLoader (libyaml)",        "pyyaml-c"),
        ("libfyaml dedup=True  (default)", "fy:" + json.dumps(dict(dedup=True,  trim=True))),
        ("libfyaml dedup=False",           "fy:" + json.dumps(dict(dedup=False, trim=True))),
    ]

    multi = args.multi
    mode_desc = "multi-doc" if multi else "single-doc"
    print(f"\nFile: {args.file}  ({size_mb:.1f} MB,  runs={args.runs},  {mode_desc})\n")
    fmt = "  {:<30}  {:>9}  {:>9}  {:>10}  {:>10}"
    print(fmt.format("Configuration", "Median", "Min", "Peak RSS", "RSS delta"))
    print(fmt.format("-"*30, "-"*9, "-"*9, "-"*10, "-"*10))

    for label, mode in configs:
        d, err = run_config(mode, args.file, args.runs, multi)
        if err:
            print(f"  {label:<30}  ERROR: {err[:70]}")
            continue
        print(fmt.format(
            label,
            f"{d['median_ms']:>7.1f} ms",
            f"{d['min_ms']:>7.1f} ms",
            f"{d['peak_rss_mb']:>8.1f} MB",
            f"{d['delta_mb']:>+8.1f} MB",
        ))
    print()


if __name__ == "__main__":
    main()
