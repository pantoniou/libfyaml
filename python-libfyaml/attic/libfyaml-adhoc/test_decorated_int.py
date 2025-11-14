#!/usr/bin/env python3
"""Test decorated integer support for large unsigned values."""

import sys
import os

# Add parent directory to path to ensure we import the local libfyaml
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import libfyaml

# Test values
LLONG_MAX = (2**63) - 1
LLONG_MAX_PLUS_1 = 2**63
ULLONG_MAX = (2**64) - 1

def test_large_unsigned_roundtrip():
    """Test round-trip conversion of large unsigned integers."""
    print("Testing large unsigned integer round-trip...")

    # Test LLONG_MAX (should work as signed)
    yaml1 = f"value: {LLONG_MAX}"
    data1 = libfyaml.loads(yaml1)
    result1 = data1.to_python()
    print(f"  LLONG_MAX ({LLONG_MAX}): {result1['value']} - {'✓' if result1['value'] == LLONG_MAX else '✗'}")
    assert result1['value'] == LLONG_MAX

    # Test LLONG_MAX + 1 (needs decorated int with unsigned flag)
    yaml2 = f"value: {LLONG_MAX_PLUS_1}"
    data2 = libfyaml.loads(yaml2)
    result2 = data2.to_python()
    print(f"  LLONG_MAX+1 ({LLONG_MAX_PLUS_1}): {result2['value']} - {'✓' if result2['value'] == LLONG_MAX_PLUS_1 else '✗'}")
    assert result2['value'] == LLONG_MAX_PLUS_1

    # Test ULLONG_MAX (maximum unsigned long long)
    yaml3 = f"value: {ULLONG_MAX}"
    data3 = libfyaml.loads(yaml3)
    result3 = data3.to_python()
    print(f"  ULLONG_MAX ({ULLONG_MAX}): {result3['value']} - {'✓' if result3['value'] == ULLONG_MAX else '✗'}")
    assert result3['value'] == ULLONG_MAX

    print("✓ All large unsigned integer round-trips passed!")


def test_python_to_yaml_large_ints():
    """Test converting Python large ints to YAML."""
    print("\nTesting Python to YAML conversion for large ints...")

    # Create Python dict with large unsigned values
    data = {
        'llong_max': LLONG_MAX,
        'llong_max_plus_1': LLONG_MAX_PLUS_1,
        'ullong_max': ULLONG_MAX,
        'regular': 42
    }

    # Convert to FyGeneric and back
    fy_data = libfyaml.from_python(data)
    result = fy_data.to_python()

    print(f"  llong_max: {result['llong_max']} - {'✓' if result['llong_max'] == LLONG_MAX else '✗'}")
    assert result['llong_max'] == LLONG_MAX

    print(f"  llong_max_plus_1: {result['llong_max_plus_1']} - {'✓' if result['llong_max_plus_1'] == LLONG_MAX_PLUS_1 else '✗'}")
    assert result['llong_max_plus_1'] == LLONG_MAX_PLUS_1

    print(f"  ullong_max: {result['ullong_max']} - {'✓' if result['ullong_max'] == ULLONG_MAX else '✗'}")
    assert result['ullong_max'] == ULLONG_MAX

    print(f"  regular: {result['regular']} - {'✓' if result['regular'] == 42 else '✗'}")
    assert result['regular'] == 42

    print("✓ All Python to YAML conversions passed!")


def test_arithmetic_with_large_ints():
    """Test arithmetic operations with large unsigned integers."""
    print("\nTesting arithmetic with large unsigned integers...")

    # Load large unsigned value
    yaml = f"value: {LLONG_MAX_PLUS_1}"
    data = libfyaml.loads(yaml)

    # Test addition (should use Python's arbitrary precision)
    result_add = data['value'] + 1000
    print(f"  {LLONG_MAX_PLUS_1} + 1000 = {result_add} - {'✓' if result_add == LLONG_MAX_PLUS_1 + 1000 else '✗'}")
    assert result_add == LLONG_MAX_PLUS_1 + 1000

    # Test subtraction
    result_sub = data['value'] - 1000
    print(f"  {LLONG_MAX_PLUS_1} - 1000 = {result_sub} - {'✓' if result_sub == LLONG_MAX_PLUS_1 - 1000 else '✗'}")
    assert result_sub == LLONG_MAX_PLUS_1 - 1000

    # Test multiplication (should use Python's arbitrary precision)
    result_mul = data['value'] * 2
    print(f"  {LLONG_MAX_PLUS_1} * 2 = {result_mul} - {'✓' if result_mul == LLONG_MAX_PLUS_1 * 2 else '✗'}")
    assert result_mul == LLONG_MAX_PLUS_1 * 2

    # Test division
    result_div = data['value'] / 2
    expected_div = float(LLONG_MAX_PLUS_1) / 2
    print(f"  {LLONG_MAX_PLUS_1} / 2 = {result_div} - {'✓' if result_div == expected_div else '✗'}")
    assert result_div == expected_div

    print("✓ All arithmetic operations with large ints passed!")


def test_int_conversion():
    """Test __int__() conversion for large unsigned values."""
    print("\nTesting __int__() conversion...")

    # Load large unsigned value
    yaml = f"value: {LLONG_MAX_PLUS_1}"
    data = libfyaml.loads(yaml)

    # Convert to int
    int_val = int(data['value'])
    print(f"  int({LLONG_MAX_PLUS_1}) = {int_val} - {'✓' if int_val == LLONG_MAX_PLUS_1 else '✗'}")
    assert int_val == LLONG_MAX_PLUS_1

    # Test with ULLONG_MAX
    yaml2 = f"value: {ULLONG_MAX}"
    data2 = libfyaml.loads(yaml2)
    int_val2 = int(data2['value'])
    print(f"  int({ULLONG_MAX}) = {int_val2} - {'✓' if int_val2 == ULLONG_MAX else '✗'}")
    assert int_val2 == ULLONG_MAX

    print("✓ All __int__() conversions passed!")


def test_yaml_emission():
    """Test emitting YAML with large unsigned integers."""
    print("\nTesting YAML emission with large ints...")

    # Create data with large unsigned values
    data = {
        'llong_max_plus_1': LLONG_MAX_PLUS_1,
        'ullong_max': ULLONG_MAX,
    }

    # Convert to FyGeneric
    fy_data = libfyaml.from_python(data)

    # Emit as YAML
    yaml_str = libfyaml.dumps(fy_data)
    print(f"  Generated YAML:\n{yaml_str}")

    # Parse back and verify
    parsed = libfyaml.loads(yaml_str)
    result = parsed.to_python()

    print(f"  Round-trip llong_max_plus_1: {result['llong_max_plus_1']} - {'✓' if result['llong_max_plus_1'] == LLONG_MAX_PLUS_1 else '✗'}")
    assert result['llong_max_plus_1'] == LLONG_MAX_PLUS_1

    print(f"  Round-trip ullong_max: {result['ullong_max']} - {'✓' if result['ullong_max'] == ULLONG_MAX else '✗'}")
    assert result['ullong_max'] == ULLONG_MAX

    print("✓ YAML emission test passed!")


def run_all_tests():
    """Run all tests."""
    print("=" * 70)
    print("Testing Decorated Integer Support")
    print("=" * 70)
    print()

    test_large_unsigned_roundtrip()
    test_python_to_yaml_large_ints()
    test_arithmetic_with_large_ints()
    test_int_conversion()
    test_yaml_emission()

    print()
    print("=" * 70)
    print("✅ All decorated integer tests passed!")
    print("=" * 70)


if __name__ == '__main__':
    run_all_tests()
