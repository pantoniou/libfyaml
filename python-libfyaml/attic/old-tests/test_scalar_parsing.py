#!/usr/bin/env python3
"""
Test scalar parsing behavior across different YAML schemas.

This script loads test-scalars.yaml and validates parsing behavior for:
- PyYAML (reference implementation)
- libfyaml yaml1.1 mode
- libfyaml yaml1.2 mode (if available)
- libfyaml json mode (if available)

Usage:
    python test_scalar_parsing.py [--verbose] [--category CATEGORY]
"""

import sys
import math
import datetime
import argparse
from pathlib import Path

# Add current directory to path for libfyaml import
sys.path.insert(0, str(Path(__file__).parent))

import yaml as pyyaml
import libfyaml as fy
from libfyaml import pyyaml_compat as fy_compat


def parse_expected(expected_str, category):
    """Convert expected value string to Python value for comparison."""
    if expected_str is None:
        return None
    if isinstance(expected_str, (int, float, bool)):
        return expected_str
    if isinstance(expected_str, str):
        # Special handling for pseudo-values in test file
        if expected_str.startswith('date('):
            # date(2024,1,15) -> datetime.date
            parts = expected_str[5:-1].split(',')
            return datetime.date(int(parts[0]), int(parts[1]), int(parts[2]))
        if expected_str.startswith('datetime('):
            # datetime(2024,1,15,10,30) or datetime(2024,1,15,10,30,tz=UTC)
            content = expected_str[9:-1]
            if 'tz=' in content:
                parts_str, tz_str = content.rsplit(',tz=', 1)
                parts = [int(p) for p in parts_str.split(',')]
                if tz_str == 'UTC':
                    tz = datetime.timezone.utc
                elif tz_str.startswith('+') or tz_str.startswith('-'):
                    # Parse +5:30 or -8:00
                    sign = 1 if tz_str[0] == '+' else -1
                    h, m = tz_str[1:].split(':')
                    tz = datetime.timezone(datetime.timedelta(hours=sign*int(h), minutes=sign*int(m)))
                else:
                    tz = None
                while len(parts) < 6:
                    parts.append(0)
                return datetime.datetime(*parts[:6], tzinfo=tz)
            else:
                parts = [int(p) for p in content.split(',')]
                while len(parts) < 6:
                    parts.append(0)
                return datetime.datetime(*parts[:6])
        return expected_str
    return expected_str


def values_equal(a, b):
    """Compare two values, handling NaN and float precision."""
    # Handle NaN
    if isinstance(a, float) and isinstance(b, float):
        if math.isnan(a) and math.isnan(b):
            return True
        if math.isinf(a) and math.isinf(b):
            return (a > 0) == (b > 0)  # Same sign infinity
    # Handle type differences for int/float
    if type(a) != type(b):
        # 1 == 1.0 but types differ
        if isinstance(a, (int, float)) and isinstance(b, (int, float)):
            if a == b:
                return False  # Same value but different type - report as mismatch
    return a == b


def test_pyyaml(input_str):
    """Parse with PyYAML safe_load."""
    try:
        return pyyaml.safe_load(input_str)
    except Exception as e:
        return f"ERROR: {e}"


def test_libfyaml_yaml11(input_str):
    """Parse with libfyaml yaml1.1 mode."""
    try:
        result = fy.loads(input_str, mode='yaml1.1')
        return result.to_python()
    except Exception as e:
        return f"ERROR: {e}"


def test_libfyaml_yaml12(input_str):
    """Parse with libfyaml yaml1.2 mode."""
    try:
        result = fy.loads(input_str, mode='yaml1.2')
        return result.to_python()
    except Exception as e:
        return f"ERROR: {e}"


def test_libfyaml_json(input_str):
    """Parse with libfyaml json mode."""
    try:
        result = fy.loads(input_str, mode='json')
        return result.to_python()
    except Exception as e:
        return f"ERROR: {e}"


def test_pyyaml_compat(input_str):
    """Parse with libfyaml pyyaml_compat layer."""
    try:
        return fy_compat.safe_load(input_str)
    except Exception as e:
        return f"ERROR: {e}"


def run_tests(test_file, verbose=False, category_filter=None):
    """Run all scalar tests and report results."""
    # Load test data
    with open(test_file) as f:
        test_data = pyyaml.safe_load(f)

    results = {
        'total': 0,
        'pyyaml_match': 0,
        'yaml11_match': 0,
        'yaml12_match': 0,
        'compat_match': 0,
        'failures': [],
        'compat_failures': [],
    }

    for category, tests in test_data.items():
        if category_filter and category != category_filter:
            continue
        if not isinstance(tests, list):
            continue

        if verbose:
            print(f"\n=== {category} ===")

        for test in tests:
            input_str = test.get('input')
            if input_str is None:
                continue

            results['total'] += 1

            # Get expected values
            expected_pyyaml = parse_expected(test.get('pyyaml'), category)
            expected_yaml11 = parse_expected(test.get('yaml11'), category)
            expected_yaml12 = parse_expected(test.get('yaml12'), category)

            # Run parsers
            actual_pyyaml = test_pyyaml(input_str)
            actual_yaml11 = test_libfyaml_yaml11(input_str)
            actual_yaml12 = test_libfyaml_yaml12(input_str)
            actual_compat = test_pyyaml_compat(input_str)

            # Check results
            pyyaml_ok = values_equal(actual_pyyaml, expected_pyyaml)
            yaml11_ok = values_equal(actual_yaml11, expected_yaml11)
            yaml12_ok = values_equal(actual_yaml12, expected_yaml12)

            if pyyaml_ok:
                results['pyyaml_match'] += 1
            if yaml11_ok:
                results['yaml11_match'] += 1
            if yaml12_ok:
                results['yaml12_match'] += 1

            # Check if libfyaml matches PyYAML (the goal for pyyaml compat)
            fy_matches_pyyaml = values_equal(actual_yaml11, actual_pyyaml)
            compat_matches_pyyaml = values_equal(actual_compat, actual_pyyaml)

            if compat_matches_pyyaml:
                results['compat_match'] += 1

            if verbose:
                status = '✓' if compat_matches_pyyaml else '✗'
                print(f"  {status} {input_str!r:25s} pyyaml={actual_pyyaml!r:20s} compat={actual_compat!r:20s}")

            if not fy_matches_pyyaml:
                results['failures'].append({
                    'category': category,
                    'input': input_str,
                    'pyyaml': actual_pyyaml,
                    'libfyaml': actual_yaml11,
                })

            if not compat_matches_pyyaml:
                results['compat_failures'].append({
                    'category': category,
                    'input': input_str,
                    'pyyaml': actual_pyyaml,
                    'compat': actual_compat,
                })

    return results


def main():
    parser = argparse.ArgumentParser(description='Test scalar parsing across YAML schemas')
    parser.add_argument('--verbose', '-v', action='store_true', help='Show all test results')
    parser.add_argument('--category', '-c', help='Only test specific category')
    parser.add_argument('--show-failures', '-f', action='store_true', help='Show failure details')
    args = parser.parse_args()

    test_file = Path(__file__).parent / 'test-scalars.yaml'
    if not test_file.exists():
        print(f"Error: {test_file} not found")
        sys.exit(1)

    print("Testing scalar parsing: libfyaml vs PyYAML")
    print("=" * 60)

    results = run_tests(test_file, verbose=args.verbose, category_filter=args.category)

    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)
    print(f"Total tests: {results['total']}")
    print()

    raw_failures = len(results['failures'])
    raw_ok = results['total'] - raw_failures
    print(f"RAW LIBFYAML yaml1.1 vs PyYAML:")
    print(f"  Matching: {raw_ok}/{results['total']} ({100*raw_ok/results['total']:.1f}%)")
    print(f"  Differing: {raw_failures} (needs C-level fixes)")
    print()

    compat_failures = len(results['compat_failures'])
    compat_ok = results['total'] - compat_failures
    print(f"PYYAML_COMPAT layer vs PyYAML:")
    print(f"  Matching: {compat_ok}/{results['total']} ({100*compat_ok/results['total']:.1f}%)")
    print(f"  Differing: {compat_failures} (remaining gaps)")

    if args.show_failures:
        if results['compat_failures']:
            print("\n" + "-" * 60)
            print("PYYAML_COMPAT FAILURES (still differ from PyYAML)")
            print("-" * 60)

            by_category = {}
            for f in results['compat_failures']:
                cat = f['category']
                if cat not in by_category:
                    by_category[cat] = []
                by_category[cat].append(f)

            for cat, failures in sorted(by_category.items()):
                print(f"\n{cat}:")
                for f in failures:
                    print(f"  {f['input']!r:25s} pyyaml={f['pyyaml']!r:20s} compat={f['compat']!r}")

    # Exit with failure only if pyyaml_compat has gaps
    if compat_failures > 0:
        sys.exit(1)
    sys.exit(0)


if __name__ == '__main__':
    main()
