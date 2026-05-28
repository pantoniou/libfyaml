#!/usr/bin/env python3
"""
Benchmark libfyaml parsing with transparent cache disabled, cold, and hot.

Usage:
    python3 docs/benchmark-parse-cache.py <yaml-file> [--runs N]

The benchmark uses an isolated cache directory by default.  Cold-cache runs
clear it with fy-tool --cache-clear before each timed parse.  Hot-cache runs
clear it once, prewarm it once, then time parses with the cache already present.
"""

import argparse
import os
import statistics
import subprocess
import sys
import tempfile


_WORKER = """
import statistics, sys, time

def _rss_kb():
    with open("/proc/self/status") as f:
        for line in f:
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    return 0

file = sys.argv[1]
runs = int(sys.argv[2])
multi = sys.argv[3] == "1"
enable_cache = sys.argv[4] == "1"
cache_min_file_size = int(sys.argv[5])

import libfyaml as fy

if cache_min_file_size >= 0:
    fy.set_cache_min_file_size(cache_min_file_size)

kw = {"dedup": True, "trim": True, "enable_cache": enable_cache}
baseline = _rss_kb()
times = []
peaks = []

for _ in range(runs):
    t0 = time.perf_counter()
    doc = fy.load_all(file, **kw) if multi else fy.load(file, **kw)
    elapsed = time.perf_counter() - t0
    times.append(elapsed)
    peaks.append(_rss_kb())
    del doc

print("%.6f\\t%.6f\\t%.6f\\t%.6f" % (
    statistics.median(times) * 1000,
    min(times) * 1000,
    statistics.median(peaks) / 1024,
    (statistics.median(peaks) - baseline) / 1024,
))
"""


def cache_clear(fy_tool, cache_dir):
    env = os.environ.copy()
    env["FY_PARSE_CACHE_OVERRIDE"] = cache_dir
    subprocess.run([fy_tool, "--cache-clear"], env=env, check=True,
                   stdout=subprocess.DEVNULL)


def run_parse(file, runs, multi, enable_cache, cache_min_size, cache_dir):
    env = os.environ.copy()
    env["FY_PARSE_CACHE_OVERRIDE"] = cache_dir
    env["PYTHONSAFEPATH"] = "1"
    r = subprocess.run(
        [sys.executable, "-c", _WORKER, file, str(runs),
         "1" if multi else "0", "1" if enable_cache else "0",
         str(cache_min_size if cache_min_size is not None else -1)],
        env=env, capture_output=True, text=True,
    )
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip() or f"worker exited with {r.returncode}")
    median_ms, min_ms, peak_rss_mb, delta_mb = (float(v) for v in r.stdout.split())
    return {
        "median_ms": median_ms,
        "min_ms": min_ms,
        "peak_rss_mb": peak_rss_mb,
        "delta_mb": delta_mb,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("file", help="YAML file to parse")
    parser.add_argument("--runs", type=int, default=5,
                        help="number of timed parses per row (default: 5)")
    parser.add_argument("--multi", action="store_true",
                        help="use load_all for multi-document files")
    parser.add_argument("--fy-tool", default="build/fy-tool",
                        help="fy-tool path used for --cache-clear (default: build/fy-tool)")
    parser.add_argument("--cache-dir", default=None,
                        help="cache directory to use (default: temporary directory)")
    parser.add_argument("--cache-min-size", type=int, default=None,
                        help="override libfyaml cache minimum file size in bytes")
    args = parser.parse_args()

    if not os.path.exists(args.file):
        sys.exit(f"error: file not found: {args.file}")
    if not os.path.exists(args.fy_tool):
        sys.exit(f"error: fy-tool not found: {args.fy_tool}")

    size_mb = os.path.getsize(args.file) / 1024 / 1024
    mode_desc = "multi-doc" if args.multi else "single-doc"

    cache_root = tempfile.TemporaryDirectory(prefix="libfyaml-bench-cache-")
    cache_dir = args.cache_dir or cache_root.name
    os.makedirs(cache_dir, exist_ok=True)

    rows = [
        ("cache=off", False),
        ("cache=cold", True),
        ("cache=hot", True),
    ]

    print(f"\nFile: {args.file}  ({size_mb:.1f} MB, runs={args.runs}, {mode_desc})")
    print(f"Cache dir: {cache_dir}\n")

    fmt = "  {:<12}  {:>9}  {:>9}  {:>10}  {:>10}"
    print(fmt.format("Configuration", "Median", "Min", "Peak RSS", "RSS delta"))
    print(fmt.format("-" * 12, "-" * 9, "-" * 9, "-" * 10, "-" * 10))

    for label, enable_cache in rows:
        if label == "cache=cold":
            results = []
            for _ in range(args.runs):
                cache_clear(args.fy_tool, cache_dir)
                results.append(run_parse(args.file, 1, args.multi, enable_cache,
                                         args.cache_min_size, cache_dir))
            d = {
                "median_ms": statistics.median(r["median_ms"] for r in results),
                "min_ms": min(r["min_ms"] for r in results),
                "peak_rss_mb": statistics.median(r["peak_rss_mb"] for r in results),
                "delta_mb": statistics.median(r["delta_mb"] for r in results),
            }
        else:
            if label == "cache=hot":
                cache_clear(args.fy_tool, cache_dir)
                run_parse(args.file, 1, args.multi, enable_cache,
                          args.cache_min_size, cache_dir)
            d = run_parse(args.file, args.runs, args.multi, enable_cache,
                          args.cache_min_size, cache_dir)

        print(fmt.format(
            label,
            f"{d['median_ms']:>7.1f} ms",
            f"{d['min_ms']:>7.1f} ms",
            f"{d['peak_rss_mb']:>8.1f} MB",
            f"{d['delta_mb']:>+8.1f} MB",
        ))

    cache_root.cleanup()


if __name__ == "__main__":
    main()
