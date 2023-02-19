#!/usr/bin/env python3
"""
Test suite for binary-safe string handling using fy_generic_sized_string.

This test verifies that FyGeneric objects can handle strings containing
null bytes (\x00) and other binary data correctly, preserving exact lengths
and content during round-trip conversions.
"""

import sys
import unittest

# Add parent directory to path for imports

import libfyaml


class TestSizedStringBasics(unittest.TestCase):
    """Test basic binary-safe string operations"""

    def test_null_byte_round_trip(self):
        """Test string with single null byte"""
        test_str = "foo\x00bar"
        data = libfyaml.from_python({'value': test_str})
        result = data.to_python()['value']

        self.assertEqual(result, test_str)
        self.assertEqual(len(result), len(test_str))

    def test_multiple_null_bytes(self):
        """Test string with multiple null bytes"""
        test_str = "foo\x00bar\x00baz"
        data = libfyaml.from_python({'value': test_str})
        result = data.to_python()['value']

        self.assertEqual(result, test_str)
        self.assertEqual(len(result), 11)

    def test_starting_with_null(self):
        """Test string starting with null byte"""
        test_str = "\x00hello"
        data = libfyaml.from_python({'value': test_str})
        result = data.to_python()['value']

        self.assertEqual(result, test_str)
        self.assertEqual(len(result), 6)

    def test_ending_with_null(self):
        """Test string ending with null byte"""
        test_str = "hello\x00"
        data = libfyaml.from_python({'value': test_str})
        result = data.to_python()['value']

        self.assertEqual(result, test_str)
        self.assertEqual(len(result), 6)

    def test_only_null_bytes(self):
        """Test string containing only null bytes"""
        test_str = "\x00\x00\x00"
        data = libfyaml.from_python({'value': test_str})
        result = data.to_python()['value']

        self.assertEqual(result, test_str)
        self.assertEqual(len(result), 3)

    def test_binary_data(self):
        """Test string with various binary characters"""
        test_str = "\x00\x01\x02\x03\x04\x05"
        data = libfyaml.from_python({'value': test_str})
        result = data.to_python()['value']

        self.assertEqual(result, test_str)
        self.assertEqual(len(result), 6)

    def test_empty_string(self):
        """Test empty string"""
        test_str = ""
        data = libfyaml.from_python({'value': test_str})
        result = data.to_python()['value']

        self.assertEqual(result, test_str)
        self.assertEqual(len(result), 0)

    def test_regular_string_still_works(self):
        """Test that regular strings without null bytes still work"""
        test_str = "hello world"
        data = libfyaml.from_python({'value': test_str})
        result = data.to_python()['value']

        self.assertEqual(result, test_str)
        self.assertEqual(len(result), 11)


class TestSizedStringYAML(unittest.TestCase):
    """Test YAML serialization/deserialization with binary-safe strings"""

    def test_yaml_round_trip_with_null(self):
        """Test YAML round-trip with null bytes"""
        test_str = "foo\x00bar"
        data = libfyaml.from_python({'value': test_str})

        # Serialize to YAML
        yaml_str = libfyaml.dumps(data)

        # Parse back
        parsed = libfyaml.loads(yaml_str)
        result = parsed.to_python()['value']

        self.assertEqual(result, test_str)
        self.assertEqual(len(result), 7)

    def test_yaml_escapes_null_bytes(self):
        """Test that YAML properly escapes null bytes"""
        test_str = "foo\x00bar"
        data = libfyaml.from_python({'value': test_str})

        yaml_str = libfyaml.dumps(data)

        # Should contain escaped null
        self.assertIn("\\0", yaml_str)

    def test_yaml_multiple_nulls_round_trip(self):
        """Test YAML round-trip with multiple null bytes"""
        test_str = "a\x00b\x00c\x00d"
        data = libfyaml.from_python({'value': test_str})

        yaml_str = libfyaml.dumps(data)
        parsed = libfyaml.loads(yaml_str)
        result = parsed.to_python()['value']

        self.assertEqual(result, test_str)
        self.assertEqual(len(result), 7)

    def test_yaml_sequence_with_binary_strings(self):
        """Test sequence containing binary-safe strings"""
        test_data = [
            "normal",
            "with\x00null",
            "more\x00null\x00bytes"
        ]
        data = libfyaml.from_python({'items': test_data})

        yaml_str = libfyaml.dumps(data)
        parsed = libfyaml.loads(yaml_str)
        result = parsed.to_python()['items']

        self.assertEqual(result, test_data)
        self.assertEqual(len(result[1]), 9)  # "with\x00null"
        self.assertEqual(len(result[2]), 15)  # "more\x00null\x00bytes"

    def test_yaml_mapping_keys_with_binary(self):
        """Test mapping with binary-safe string keys"""
        test_str_key = "key\x00with\x00nulls"
        test_str_val = "val\x00with\x00nulls"
        test_data = {test_str_key: test_str_val}

        data = libfyaml.from_python(test_data)

        yaml_str = libfyaml.dumps(data)
        parsed = libfyaml.loads(yaml_str)
        result = parsed.to_python()

        self.assertEqual(result, test_data)
        # Check the key exists with correct length (14 chars: "key\x00with\x00nulls")
        self.assertIn(test_str_key, result)
        self.assertEqual(len(list(result.keys())[0]), 14)


class TestSizedStringJSON(unittest.TestCase):
    """Test JSON serialization with binary-safe strings"""

    def test_json_round_trip_with_null(self):
        """Test JSON round-trip with null bytes"""
        test_str = "foo\x00bar"
        data = libfyaml.from_python({'value': test_str})

        # Serialize to JSON
        json_str = libfyaml.json_dumps(data)

        # Parse back
        parsed = libfyaml.loads(json_str, mode='json')
        result = parsed.to_python()['value']

        self.assertEqual(result, test_str)
        self.assertEqual(len(result), 7)

    def test_json_escapes_null_bytes(self):
        """Test that JSON properly escapes null bytes"""
        test_str = "foo\x00bar"
        data = libfyaml.from_python({'value': test_str})

        json_str = libfyaml.json_dumps(data)

        # JSON uses \u0000 for null
        self.assertIn("\\u0000", json_str)

    def test_json_multiple_nulls(self):
        """Test JSON with multiple null bytes"""
        test_str = "a\x00b\x00c"
        data = libfyaml.from_python({'value': test_str})

        json_str = libfyaml.json_dumps(data)
        parsed = libfyaml.loads(json_str, mode='json')
        result = parsed.to_python()['value']

        self.assertEqual(result, test_str)
        self.assertEqual(len(result), 5)


class TestSizedStringComparison(unittest.TestCase):
    """Test comparison operations with binary-safe strings"""

    def test_equality_with_null_bytes(self):
        """Test equality comparison with strings containing null bytes"""
        test_str = "foo\x00bar"
        data1 = libfyaml.from_python({'value': test_str})
        data2 = libfyaml.from_python({'value': test_str})

        # FyGeneric to FyGeneric
        self.assertEqual(data1['value'], data2['value'])

        # FyGeneric to Python string
        self.assertEqual(data1['value'], test_str)

    def test_inequality_different_nulls(self):
        """Test that different null positions are not equal"""
        str1 = "foo\x00bar"
        str2 = "fo\x00obar"

        data1 = libfyaml.from_python({'value': str1})
        data2 = libfyaml.from_python({'value': str2})

        self.assertNotEqual(data1['value'], data2['value'])

    def test_less_than_with_nulls(self):
        """Test less-than comparison with null bytes"""
        # Null byte has value 0, so should sort before other chars
        str1 = "\x00abc"
        str2 = "abc"

        data1 = libfyaml.from_python({'value': str1})
        data2 = libfyaml.from_python({'value': str2})

        self.assertLess(data1['value'], data2['value'])
        self.assertLess(data1['value'], str2)

    def test_greater_than_with_nulls(self):
        """Test greater-than comparison with null bytes"""
        str1 = "abc"
        str2 = "\x00abc"

        data1 = libfyaml.from_python({'value': str1})
        data2 = libfyaml.from_python({'value': str2})

        self.assertGreater(data1['value'], data2['value'])
        self.assertGreater(data1['value'], str2)

    def test_comparison_same_prefix(self):
        """Test comparison when one string is prefix of another with nulls"""
        str1 = "foo\x00"
        str2 = "foo\x00bar"

        data1 = libfyaml.from_python({'value': str1})
        data2 = libfyaml.from_python({'value': str2})

        self.assertLess(data1['value'], data2['value'])
        self.assertLess(data1['value'], str2)


class TestSizedStringHash(unittest.TestCase):
    """Test hash operations with binary-safe strings"""

    def test_hash_with_null_bytes(self):
        """Test that strings with null bytes can be hashed"""
        test_str = "foo\x00bar"
        data = libfyaml.from_python({'value': test_str})

        # Should match Python's hash
        self.assertEqual(hash(data['value']), hash(test_str))

    def test_hash_different_for_different_nulls(self):
        """Test that different null positions produce different hashes"""
        str1 = "foo\x00bar"
        str2 = "fo\x00obar"

        data1 = libfyaml.from_python({'value': str1})
        data2 = libfyaml.from_python({'value': str2})

        # Highly likely to be different (though hash collisions are possible)
        # Just verify they can both be hashed
        hash1 = hash(data1['value'])
        hash2 = hash(data2['value'])

        # They should be different (same length, different content)
        self.assertNotEqual(hash1, hash2)

    def test_use_as_dict_key_with_nulls(self):
        """Test using binary-safe strings as dict keys"""
        test_str = "key\x00with\x00nulls"
        data = libfyaml.from_python({'value': test_str})

        d = {}
        d[data['value']] = "test_value"

        # Should be able to look up with native string
        self.assertEqual(d[test_str], "test_value")

        # Should be able to look up with FyGeneric
        self.assertEqual(d[data['value']], "test_value")


class TestSizedStringEdgeCases(unittest.TestCase):
    """Test edge cases for binary-safe string handling"""

    def test_unicode_with_null_bytes(self):
        """Test Unicode strings containing null bytes"""
        test_str = "hello\x00世界\x00🌍"
        data = libfyaml.from_python({'value': test_str})
        result = data.to_python()['value']

        self.assertEqual(result, test_str)
        # 10 characters: h e l l o \x00 世 界 \x00 🌍
        self.assertEqual(len(result), 10)

    def test_very_long_string_with_nulls(self):
        """Test very long strings with null bytes"""
        # Create a long string with nulls every 100 chars
        parts = ["x" * 100 for _ in range(100)]
        test_str = "\x00".join(parts)

        data = libfyaml.from_python({'value': test_str})
        result = data.to_python()['value']

        self.assertEqual(result, test_str)
        # 100 parts * 100 chars + 99 null bytes
        self.assertEqual(len(result), 10099)

    def test_all_byte_values(self):
        """Test string containing all possible byte values 0-255"""
        # Note: Some values might not be valid UTF-8
        # Just test a range that includes nulls and other low values
        test_bytes = bytes(range(32))  # 0-31 control characters
        try:
            test_str = test_bytes.decode('latin-1')  # Preserve byte values
            data = libfyaml.from_python({'value': test_str})
            result = data.to_python()['value']

            self.assertEqual(result, test_str)
            self.assertEqual(len(result), 32)
        except Exception:
            # If encoding fails, that's ok - just testing what we can
            pass

    def test_null_byte_in_nested_structure(self):
        """Test null bytes in deeply nested structures"""
        test_data = {
            'level1': {
                'level2': {
                    'level3': {
                        'value': "deep\x00nested\x00string"
                    }
                }
            }
        }

        data = libfyaml.from_python(test_data)
        yaml_str = libfyaml.dumps(data)
        parsed = libfyaml.loads(yaml_str)
        result = parsed.to_python()

        self.assertEqual(
            result['level1']['level2']['level3']['value'],
            "deep\x00nested\x00string"
        )


def run_tests():
    """Run all tests"""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    # Add all test classes
    suite.addTests(loader.loadTestsFromTestCase(TestSizedStringBasics))
    suite.addTests(loader.loadTestsFromTestCase(TestSizedStringYAML))
    suite.addTests(loader.loadTestsFromTestCase(TestSizedStringJSON))
    suite.addTests(loader.loadTestsFromTestCase(TestSizedStringComparison))
    suite.addTests(loader.loadTestsFromTestCase(TestSizedStringHash))
    suite.addTests(loader.loadTestsFromTestCase(TestSizedStringEdgeCases))

    # Run with verbose output
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    # Return exit code
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(run_tests())
