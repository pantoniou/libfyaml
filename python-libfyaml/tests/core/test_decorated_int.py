#!/usr/bin/env python3
"""
Test suite for decorated integer support with large unsigned values.

This test verifies that FyGeneric objects can handle the full unsigned long long
range (0 to 2^64-1) using libfyaml's fy_generic_decorated_int with the
FYGDIF_UNSIGNED_RANGE_EXTEND flag for values exceeding LLONG_MAX.
"""

import sys
import unittest

# Add parent directory to path for imports

import libfyaml

# Test constants
LLONG_MAX = (2**63) - 1           # 9223372036854775807
LLONG_MAX_PLUS_1 = 2**63          # 9223372036854775808
ULLONG_MAX = (2**64) - 1          # 18446744073709551615


class TestDecoratedIntBasics(unittest.TestCase):
    """Test basic decorated integer functionality"""

    def test_llong_max_signed(self):
        """Test LLONG_MAX (should work as regular signed)"""
        yaml_str = f"value: {LLONG_MAX}"
        data = libfyaml.loads(yaml_str)
        result = data.to_python()

        self.assertEqual(result['value'], LLONG_MAX)

    def test_llong_max_plus_1(self):
        """Test LLONG_MAX+1 (requires decorated int)"""
        yaml_str = f"value: {LLONG_MAX_PLUS_1}"
        data = libfyaml.loads(yaml_str)
        result = data.to_python()

        self.assertEqual(result['value'], LLONG_MAX_PLUS_1)

    def test_ullong_max(self):
        """Test ULLONG_MAX (maximum unsigned long long)"""
        yaml_str = f"value: {ULLONG_MAX}"
        data = libfyaml.loads(yaml_str)
        result = data.to_python()

        self.assertEqual(result['value'], ULLONG_MAX)

    def test_regular_int(self):
        """Test that regular integers still work"""
        yaml_str = "value: 42"
        data = libfyaml.loads(yaml_str)
        result = data.to_python()

        self.assertEqual(result['value'], 42)

    def test_negative_int(self):
        """Test that negative integers still work"""
        yaml_str = "value: -1000"
        data = libfyaml.loads(yaml_str)
        result = data.to_python()

        self.assertEqual(result['value'], -1000)

    def test_zero(self):
        """Test zero"""
        yaml_str = "value: 0"
        data = libfyaml.loads(yaml_str)
        result = data.to_python()

        self.assertEqual(result['value'], 0)


class TestDecoratedIntPythonConversion(unittest.TestCase):
    """Test Python to FyGeneric conversion with large integers"""

    def test_python_to_generic_llong_max(self):
        """Test converting Python int (LLONG_MAX) to FyGeneric"""
        data = libfyaml.from_python({'value': LLONG_MAX})
        result = data.to_python()

        self.assertEqual(result['value'], LLONG_MAX)

    def test_python_to_generic_llong_max_plus_1(self):
        """Test converting Python int (LLONG_MAX+1) to FyGeneric"""
        data = libfyaml.from_python({'value': LLONG_MAX_PLUS_1})
        result = data.to_python()

        self.assertEqual(result['value'], LLONG_MAX_PLUS_1)

    def test_python_to_generic_ullong_max(self):
        """Test converting Python int (ULLONG_MAX) to FyGeneric"""
        data = libfyaml.from_python({'value': ULLONG_MAX})
        result = data.to_python()

        self.assertEqual(result['value'], ULLONG_MAX)

    def test_python_to_generic_mixed_values(self):
        """Test converting dict with mixed integer sizes"""
        data_dict = {
            'small': 42,
            'llong_max': LLONG_MAX,
            'llong_max_plus_1': LLONG_MAX_PLUS_1,
            'ullong_max': ULLONG_MAX,
            'negative': -1000,
        }

        fy_data = libfyaml.from_python(data_dict)
        result = fy_data.to_python()

        self.assertEqual(result['small'], 42)
        self.assertEqual(result['llong_max'], LLONG_MAX)
        self.assertEqual(result['llong_max_plus_1'], LLONG_MAX_PLUS_1)
        self.assertEqual(result['ullong_max'], ULLONG_MAX)
        self.assertEqual(result['negative'], -1000)


class TestDecoratedIntArithmetic(unittest.TestCase):
    """Test arithmetic operations with decorated integers"""

    def setUp(self):
        """Load large unsigned value for testing"""
        yaml_str = f"value: {LLONG_MAX_PLUS_1}"
        self.data = libfyaml.loads(yaml_str)

    def test_addition_with_small_int(self):
        """Test addition with small integer"""
        result = self.data['value'] + 1000
        self.assertEqual(result, LLONG_MAX_PLUS_1 + 1000)

    def test_addition_with_large_int(self):
        """Test addition resulting in > ULLONG_MAX"""
        result = self.data['value'] * 2
        self.assertEqual(result, LLONG_MAX_PLUS_1 * 2)

    def test_subtraction(self):
        """Test subtraction"""
        result = self.data['value'] - 1000
        self.assertEqual(result, LLONG_MAX_PLUS_1 - 1000)

    def test_multiplication(self):
        """Test multiplication"""
        result = self.data['value'] * 2
        self.assertEqual(result, LLONG_MAX_PLUS_1 * 2)

    def test_true_division(self):
        """Test true division"""
        result = self.data['value'] / 2
        expected = float(LLONG_MAX_PLUS_1) / 2
        self.assertEqual(result, expected)

    def test_floor_division(self):
        """Test floor division"""
        result = self.data['value'] // 100
        expected = LLONG_MAX_PLUS_1 // 100
        self.assertEqual(result, expected)

    def test_modulo(self):
        """Test modulo operation"""
        result = self.data['value'] % 1000
        expected = LLONG_MAX_PLUS_1 % 1000
        self.assertEqual(result, expected)

    def test_arithmetic_with_python_int(self):
        """Test arithmetic mixing FyGeneric and Python int"""
        fy_val = self.data['value']
        py_val = 1000

        # Addition
        self.assertEqual(fy_val + py_val, LLONG_MAX_PLUS_1 + 1000)
        self.assertEqual(py_val + fy_val, 1000 + LLONG_MAX_PLUS_1)

        # Multiplication
        self.assertEqual(fy_val * 2, LLONG_MAX_PLUS_1 * 2)
        self.assertEqual(2 * fy_val, 2 * LLONG_MAX_PLUS_1)


class TestDecoratedIntConversion(unittest.TestCase):
    """Test type conversion with decorated integers"""

    def test_int_conversion_llong_max_plus_1(self):
        """Test __int__() with LLONG_MAX+1"""
        yaml_str = f"value: {LLONG_MAX_PLUS_1}"
        data = libfyaml.loads(yaml_str)

        int_val = int(data['value'])
        self.assertEqual(int_val, LLONG_MAX_PLUS_1)
        self.assertIsInstance(int_val, int)

    def test_int_conversion_ullong_max(self):
        """Test __int__() with ULLONG_MAX"""
        yaml_str = f"value: {ULLONG_MAX}"
        data = libfyaml.loads(yaml_str)

        int_val = int(data['value'])
        self.assertEqual(int_val, ULLONG_MAX)
        self.assertIsInstance(int_val, int)

    def test_float_conversion(self):
        """Test __float__() with large unsigned value"""
        yaml_str = f"value: {LLONG_MAX_PLUS_1}"
        data = libfyaml.loads(yaml_str)

        float_val = float(data['value'])
        expected = float(LLONG_MAX_PLUS_1)
        self.assertEqual(float_val, expected)

    def test_to_python_method(self):
        """Test .to_python() preserves large unsigned values"""
        yaml_str = f"""
        small: 42
        large1: {LLONG_MAX_PLUS_1}
        large2: {ULLONG_MAX}
        """
        data = libfyaml.loads(yaml_str)
        result = data.to_python()

        self.assertEqual(result['small'], 42)
        self.assertEqual(result['large1'], LLONG_MAX_PLUS_1)
        self.assertEqual(result['large2'], ULLONG_MAX)


class TestDecoratedIntYAML(unittest.TestCase):
    """Test YAML serialization/deserialization with decorated integers"""

    def test_yaml_emission_llong_max_plus_1(self):
        """Test emitting YAML with LLONG_MAX+1"""
        data = libfyaml.from_python({'value': LLONG_MAX_PLUS_1})
        yaml_str = libfyaml.dumps(data)

        # Parse back
        parsed = libfyaml.loads(yaml_str)
        result = parsed.to_python()

        self.assertEqual(result['value'], LLONG_MAX_PLUS_1)

    def test_yaml_emission_ullong_max(self):
        """Test emitting YAML with ULLONG_MAX"""
        data = libfyaml.from_python({'value': ULLONG_MAX})
        yaml_str = libfyaml.dumps(data)

        # Parse back
        parsed = libfyaml.loads(yaml_str)
        result = parsed.to_python()

        self.assertEqual(result['value'], ULLONG_MAX)

    def test_yaml_round_trip_mixed(self):
        """Test YAML round-trip with mixed integer sizes"""
        original = {
            'small': 100,
            'llong_max': LLONG_MAX,
            'llong_max_plus_1': LLONG_MAX_PLUS_1,
            'ullong_max': ULLONG_MAX,
        }

        # Convert to FyGeneric
        fy_data = libfyaml.from_python(original)

        # Emit as YAML
        yaml_str = libfyaml.dumps(fy_data)

        # Parse back
        parsed = libfyaml.loads(yaml_str)
        result = parsed.to_python()

        self.assertEqual(result, original)

    def test_yaml_sequence_with_large_ints(self):
        """Test YAML sequence containing large unsigned integers"""
        original = {
            'values': [LLONG_MAX, LLONG_MAX_PLUS_1, ULLONG_MAX]
        }

        fy_data = libfyaml.from_python(original)
        yaml_str = libfyaml.dumps(fy_data)
        parsed = libfyaml.loads(yaml_str)
        result = parsed.to_python()

        self.assertEqual(result['values'], [LLONG_MAX, LLONG_MAX_PLUS_1, ULLONG_MAX])


class TestDecoratedIntJSON(unittest.TestCase):
    """Test JSON serialization with decorated integers"""

    def test_json_emission_llong_max_plus_1(self):
        """Test emitting JSON with LLONG_MAX+1"""
        data = libfyaml.from_python({'value': LLONG_MAX_PLUS_1})
        json_str = libfyaml.json_dumps(data)

        # Parse back
        parsed = libfyaml.loads(json_str, mode='json')
        result = parsed.to_python()

        self.assertEqual(result['value'], LLONG_MAX_PLUS_1)

    def test_json_emission_ullong_max(self):
        """Test emitting JSON with ULLONG_MAX"""
        data = libfyaml.from_python({'value': ULLONG_MAX})
        json_str = libfyaml.json_dumps(data)

        # Parse back
        parsed = libfyaml.loads(json_str, mode='json')
        result = parsed.to_python()

        self.assertEqual(result['value'], ULLONG_MAX)

    def test_json_round_trip(self):
        """Test JSON round-trip with large unsigned integers"""
        original = {
            'llong_max_plus_1': LLONG_MAX_PLUS_1,
            'ullong_max': ULLONG_MAX,
        }

        fy_data = libfyaml.from_python(original)
        json_str = libfyaml.json_dumps(fy_data)
        parsed = libfyaml.loads(json_str, mode='json')
        result = parsed.to_python()

        self.assertEqual(result, original)


class TestDecoratedIntComparison(unittest.TestCase):
    """Test comparison operations with decorated integers"""

    def test_equality_with_python_int(self):
        """Test equality with Python integer"""
        yaml_str = f"value: {LLONG_MAX_PLUS_1}"
        data = libfyaml.loads(yaml_str)

        self.assertEqual(data['value'], LLONG_MAX_PLUS_1)
        self.assertEqual(LLONG_MAX_PLUS_1, data['value'])

    def test_inequality(self):
        """Test inequality"""
        yaml_str = f"value: {LLONG_MAX_PLUS_1}"
        data = libfyaml.loads(yaml_str)

        self.assertNotEqual(data['value'], LLONG_MAX)
        self.assertNotEqual(data['value'], ULLONG_MAX)

    def test_less_than(self):
        """Test less-than comparison"""
        yaml_str = f"value: {LLONG_MAX_PLUS_1}"
        data = libfyaml.loads(yaml_str)

        self.assertLess(data['value'], ULLONG_MAX)
        self.assertLess(LLONG_MAX, data['value'])

    def test_greater_than(self):
        """Test greater-than comparison"""
        yaml_str = f"value: {LLONG_MAX_PLUS_1}"
        data = libfyaml.loads(yaml_str)

        self.assertGreater(data['value'], LLONG_MAX)
        self.assertGreater(ULLONG_MAX, data['value'])

    def test_comparison_chain(self):
        """Test comparison chain"""
        yaml_str = f"""
        v1: {LLONG_MAX}
        v2: {LLONG_MAX_PLUS_1}
        v3: {ULLONG_MAX}
        """
        data = libfyaml.loads(yaml_str)

        self.assertLess(data['v1'], data['v2'])
        self.assertLess(data['v2'], data['v3'])
        self.assertLess(data['v1'], data['v3'])


class TestDecoratedIntHash(unittest.TestCase):
    """Test hash operations with decorated integers"""

    def test_hash_llong_max_plus_1(self):
        """Test hash of LLONG_MAX+1"""
        yaml_str = f"value: {LLONG_MAX_PLUS_1}"
        data = libfyaml.loads(yaml_str)

        # Should match Python's hash
        self.assertEqual(hash(data['value']), hash(LLONG_MAX_PLUS_1))

    def test_hash_ullong_max(self):
        """Test hash of ULLONG_MAX"""
        yaml_str = f"value: {ULLONG_MAX}"
        data = libfyaml.loads(yaml_str)

        # Should match Python's hash
        self.assertEqual(hash(data['value']), hash(ULLONG_MAX))

    def test_use_as_dict_key(self):
        """Test using large unsigned int as dict key"""
        yaml_str = f"value: {LLONG_MAX_PLUS_1}"
        data = libfyaml.loads(yaml_str)

        d = {}
        d[data['value']] = "test"

        # Should be able to look up with Python int
        self.assertEqual(d[LLONG_MAX_PLUS_1], "test")

        # Should be able to look up with FyGeneric
        self.assertEqual(d[data['value']], "test")

    def test_use_in_set(self):
        """Test using large unsigned ints in set"""
        yaml_str = f"""
        v1: {LLONG_MAX_PLUS_1}
        v2: {ULLONG_MAX}
        """
        data = libfyaml.loads(yaml_str)

        s = {data['v1'], data['v2']}

        # Python ints should be in set
        self.assertIn(LLONG_MAX_PLUS_1, s)
        self.assertIn(ULLONG_MAX, s)

        # FyGeneric should be in set
        self.assertIn(data['v1'], s)
        self.assertIn(data['v2'], s)


class TestDecoratedIntEdgeCases(unittest.TestCase):
    """Test edge cases for decorated integers"""

    def test_boundary_values(self):
        """Test boundary values around LLONG_MAX"""
        values = [
            LLONG_MAX - 1,
            LLONG_MAX,
            LLONG_MAX + 1,
            LLONG_MAX + 2,
        ]

        for val in values:
            data = libfyaml.from_python({'v': val})
            result = data.to_python()
            self.assertEqual(result['v'], val, f"Failed for {val}")

    def test_ullong_max_boundary(self):
        """Test values at ULLONG_MAX boundary"""
        values = [
            ULLONG_MAX - 1,
            ULLONG_MAX,
        ]

        for val in values:
            data = libfyaml.from_python({'v': val})
            result = data.to_python()
            self.assertEqual(result['v'], val, f"Failed for {val}")

    def test_powers_of_two(self):
        """Test powers of two around the boundary"""
        values = [
            2**62,
            2**63 - 1,
            2**63,
            2**63 + 1,
            2**64 - 1,
        ]

        for val in values:
            data = libfyaml.from_python({'v': val})
            result = data.to_python()
            self.assertEqual(result['v'], val, f"Failed for 2^{val.bit_length()-1}")

    def test_nested_structure_with_large_ints(self):
        """Test large ints in nested structures"""
        original = {
            'level1': {
                'level2': {
                    'level3': {
                        'value': ULLONG_MAX
                    }
                }
            }
        }

        fy_data = libfyaml.from_python(original)
        yaml_str = libfyaml.dumps(fy_data)
        parsed = libfyaml.loads(yaml_str)
        result = parsed.to_python()

        self.assertEqual(
            result['level1']['level2']['level3']['value'],
            ULLONG_MAX
        )


def run_tests():
    """Run all tests"""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    # Add all test classes
    suite.addTests(loader.loadTestsFromTestCase(TestDecoratedIntBasics))
    suite.addTests(loader.loadTestsFromTestCase(TestDecoratedIntPythonConversion))
    suite.addTests(loader.loadTestsFromTestCase(TestDecoratedIntArithmetic))
    suite.addTests(loader.loadTestsFromTestCase(TestDecoratedIntConversion))
    suite.addTests(loader.loadTestsFromTestCase(TestDecoratedIntYAML))
    suite.addTests(loader.loadTestsFromTestCase(TestDecoratedIntJSON))
    suite.addTests(loader.loadTestsFromTestCase(TestDecoratedIntComparison))
    suite.addTests(loader.loadTestsFromTestCase(TestDecoratedIntHash))
    suite.addTests(loader.loadTestsFromTestCase(TestDecoratedIntEdgeCases))

    # Run with verbose output
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    # Return exit code
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(run_tests())
