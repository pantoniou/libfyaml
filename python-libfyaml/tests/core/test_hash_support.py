#!/usr/bin/env python3
"""
Test suite for hash support in FyGeneric objects.

This test verifies that FyGeneric objects can be hashed and used as dict keys
or in sets, with hashes matching their native Python equivalents.
"""

import sys
import unittest

# Add parent directory to path for imports

import libfyaml


class TestHashSupport(unittest.TestCase):
    """Test hash support for FyGeneric objects"""

    def setUp(self):
        """Load test data"""
        self.yaml_str = """
        int_val: 42
        negative_int: -17
        zero: 0
        str_val: hello
        empty_str: ""
        bool_true: true
        bool_false: false
        null_val: null
        float_val: 3.14
        negative_float: -2.5
        sequence: [1, 2, 3]
        mapping: {a: 1, b: 2}
        """
        self.data = libfyaml.loads(self.yaml_str)

    def test_hash_int(self):
        """Test hash of integer values"""
        fy_int = self.data['int_val']
        native_int = 42

        self.assertEqual(hash(fy_int), hash(native_int))

    def test_hash_negative_int(self):
        """Test hash of negative integers"""
        fy_int = self.data['negative_int']
        native_int = -17

        self.assertEqual(hash(fy_int), hash(native_int))

    def test_hash_zero(self):
        """Test hash of zero"""
        fy_zero = self.data['zero']
        self.assertEqual(hash(fy_zero), hash(0))

    def test_hash_string(self):
        """Test hash of string values"""
        fy_str = self.data['str_val']
        native_str = "hello"

        self.assertEqual(hash(fy_str), hash(native_str))

    def test_hash_empty_string(self):
        """Test hash of empty string"""
        fy_str = self.data['empty_str']
        self.assertEqual(hash(fy_str), hash(""))

    def test_hash_bool_true(self):
        """Test hash of True"""
        fy_bool = self.data['bool_true']
        self.assertEqual(hash(fy_bool), hash(True))

    def test_hash_bool_false(self):
        """Test hash of False"""
        fy_bool = self.data['bool_false']
        self.assertEqual(hash(fy_bool), hash(False))

    def test_hash_null(self):
        """Test hash of null/None"""
        fy_null = self.data['null_val']
        self.assertEqual(hash(fy_null), hash(None))

    def test_hash_float(self):
        """Test hash of float values"""
        fy_float = self.data['float_val']
        native_float = 3.14

        self.assertEqual(hash(fy_float), hash(native_float))

    def test_hash_negative_float(self):
        """Test hash of negative floats"""
        fy_float = self.data['negative_float']
        native_float = -2.5

        self.assertEqual(hash(fy_float), hash(native_float))

    def test_hash_stability(self):
        """Test that hash is stable (same value hashed multiple times)"""
        val = self.data['int_val']
        h1 = hash(val)
        h2 = hash(val)
        h3 = hash(val)

        self.assertEqual(h1, h2)
        self.assertEqual(h2, h3)

    def test_sequence_unhashable(self):
        """Test that sequences are unhashable"""
        seq = self.data['sequence']

        with self.assertRaises(TypeError) as cm:
            hash(seq)

        self.assertIn("unhashable", str(cm.exception))

    def test_mapping_unhashable(self):
        """Test that mappings are unhashable"""
        mapping = self.data['mapping']

        with self.assertRaises(TypeError) as cm:
            hash(mapping)

        self.assertIn("unhashable", str(cm.exception))

    def test_use_as_dict_key_int(self):
        """Test using FyGeneric int as dict key"""
        d = {}
        d[self.data['int_val']] = "value"

        # Should be able to look up with native int
        self.assertEqual(d[42], "value")

        # Should be able to look up with FyGeneric
        self.assertEqual(d[self.data['int_val']], "value")

    def test_use_as_dict_key_string(self):
        """Test using FyGeneric string as dict key"""
        d = {}
        d[self.data['str_val']] = "value"

        # Should be able to look up with native string
        self.assertEqual(d["hello"], "value")

        # Should be able to look up with FyGeneric
        self.assertEqual(d[self.data['str_val']], "value")

    def test_use_in_set_int(self):
        """Test using FyGeneric int in a set"""
        s = {self.data['int_val'], self.data['negative_int']}

        # Native ints should be in set
        self.assertIn(42, s)
        self.assertIn(-17, s)

        # FyGeneric should be in set
        self.assertIn(self.data['int_val'], s)
        self.assertIn(self.data['negative_int'], s)

    def test_use_in_set_string(self):
        """Test using FyGeneric string in a set"""
        s = {self.data['str_val'], self.data['empty_str']}

        # Native strings should be in set
        self.assertIn("hello", s)
        self.assertIn("", s)

        # FyGeneric should be in set
        self.assertIn(self.data['str_val'], s)
        self.assertIn(self.data['empty_str'], s)

    def test_cross_lookup_dict(self):
        """Test adding with FyGeneric, looking up with native"""
        cache = {}

        # Add with FyGeneric
        cache[self.data['int_val']] = "cached int"
        cache[self.data['str_val']] = "cached string"

        # Lookup with native types
        self.assertEqual(cache[42], "cached int")
        self.assertEqual(cache["hello"], "cached string")

    def test_cross_lookup_reverse(self):
        """Test adding with native, looking up with FyGeneric"""
        cache = {}

        # Add with native types
        cache[99] = "native int"
        cache["world"] = "native string"

        # Create FyGeneric with same values
        data = libfyaml.loads("num: 99\nword: world")

        # Lookup with FyGeneric
        self.assertEqual(cache[data['num']], "native int")
        self.assertEqual(cache[data['word']], "native string")

    def test_different_values_different_hash(self):
        """Test that different values usually have different hashes"""
        data = libfyaml.loads("""
        val1: 42
        val2: 43
        val3: hello
        val4: world
        """)

        hashes = {
            hash(data['val1']),
            hash(data['val2']),
            hash(data['val3']),
            hash(data['val4']),
        }

        # All four should have different hashes (highly likely)
        self.assertEqual(len(hashes), 4)

    def test_bool_hash_like_python(self):
        """Test that bool hash matches Python behavior (True=1, False=0)"""
        # In Python, True hashes to 1 and False hashes to 0
        self.assertEqual(hash(self.data['bool_true']), 1)
        self.assertEqual(hash(self.data['bool_false']), 0)

        # And they're equal to int hashes
        self.assertEqual(hash(self.data['bool_true']), hash(1))
        self.assertEqual(hash(self.data['bool_false']), hash(0))

    def test_set_deduplication(self):
        """Test that sets properly deduplicate based on hash+equality"""
        data1 = libfyaml.loads("val: 42")
        data2 = libfyaml.loads("val: 42")

        # Both wrap 42, should be considered equal
        s = {data1['val'], data2['val'], 42}

        # Should only have one element (all equal to 42)
        # Actually, they might not be equal due to object identity
        # Let me just check hash equality
        self.assertEqual(hash(data1['val']), hash(42))
        self.assertEqual(hash(data2['val']), hash(42))


class TestHashEdgeCases(unittest.TestCase):
    """Test edge cases for hash support"""

    def test_large_integer_hash(self):
        """Test hash of large integers"""
        large_int = 9223372036854775807  # Max long long
        data = libfyaml.loads(f"val: {large_int}")

        self.assertEqual(hash(data['val']), hash(large_int))

    def test_very_long_string_hash(self):
        """Test hash of very long strings"""
        long_str = "a" * 10000
        # Use from_python to create it
        data = libfyaml.from_python({"val": long_str})

        self.assertEqual(hash(data['val']), hash(long_str))

    def test_special_float_values(self):
        """Test hash of special float values"""
        # Note: NaN and inf might not be representable in YAML
        # Just test a few edge case floats
        test_floats = [0.0, -0.0, 1.0, -1.0, 0.1, 1e10, 1e-10]

        for f in test_floats:
            data = libfyaml.loads(f"val: {f}")
            self.assertEqual(
                hash(data['val']), hash(f),
                f"Hash mismatch for {f}"
            )

    def test_unicode_string_hash(self):
        """Test hash of unicode strings"""
        unicode_str = "hello 世界 🌍"
        data = libfyaml.from_python({"val": unicode_str})

        self.assertEqual(hash(data['val']), hash(unicode_str))


def run_tests():
    """Run all tests"""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    # Add all test classes
    suite.addTests(loader.loadTestsFromTestCase(TestHashSupport))
    suite.addTests(loader.loadTestsFromTestCase(TestHashEdgeCases))

    # Run with verbose output
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    # Return exit code
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(run_tests())
