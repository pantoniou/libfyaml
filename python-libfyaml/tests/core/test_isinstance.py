#!/usr/bin/env python3
"""
Test suite for isinstance() support via dynamic __class__ property.

This test verifies that FyGeneric objects correctly report their wrapped
type via __class__, enabling natural isinstance() checks while preserving
the ability to check for the FyGeneric wrapper type.
"""

import sys
import unittest

# Add parent directory to path for imports

import libfyaml


class TestIsinstanceSupport(unittest.TestCase):
    """Test isinstance() behavior with FyGeneric objects"""

    def setUp(self):
        """Load test data"""
        self.yaml_str = """
        null_value: null
        bool_true: true
        bool_false: false
        integer: 42
        negative_int: -17
        large_int: 9223372036854775807
        float_value: 3.14159
        string: hello world
        empty_string: ""
        sequence:
          - 1
          - 2
          - 3
        empty_sequence: []
        mapping:
          key1: value1
          key2: value2
        empty_mapping: {}
        nested:
          users:
            - name: Alice
              age: 30
            - name: Bob
              age: 25
        """
        self.data = libfyaml.loads(self.yaml_str)

    def test_basic_isinstance_int(self):
        """Test isinstance() with integer values"""
        self.assertTrue(isinstance(self.data['integer'], int))
        self.assertTrue(isinstance(self.data['negative_int'], int))
        self.assertTrue(isinstance(self.data['large_int'], int))

    def test_basic_isinstance_float(self):
        """Test isinstance() with float values"""
        self.assertTrue(isinstance(self.data['float_value'], float))

    def test_basic_isinstance_string(self):
        """Test isinstance() with string values"""
        self.assertTrue(isinstance(self.data['string'], str))
        self.assertTrue(isinstance(self.data['empty_string'], str))

    def test_basic_isinstance_bool(self):
        """Test isinstance() with boolean values"""
        self.assertTrue(isinstance(self.data['bool_true'], bool))
        self.assertTrue(isinstance(self.data['bool_false'], bool))

    def test_basic_isinstance_sequence(self):
        """Test isinstance() with sequence values"""
        self.assertTrue(isinstance(self.data['sequence'], list))
        self.assertTrue(isinstance(self.data['empty_sequence'], list))

    def test_basic_isinstance_mapping(self):
        """Test isinstance() with mapping values"""
        self.assertTrue(isinstance(self.data['mapping'], dict))
        self.assertTrue(isinstance(self.data['empty_mapping'], dict))

    def test_basic_isinstance_null(self):
        """Test isinstance() with null values"""
        self.assertTrue(isinstance(self.data['null_value'], type(None)))

    def test_isinstance_with_fygeneric(self):
        """Test that isinstance(obj, FyGeneric) still works"""
        self.assertTrue(isinstance(self.data['integer'], libfyaml.FyGeneric))
        self.assertTrue(isinstance(self.data['string'], libfyaml.FyGeneric))
        self.assertTrue(isinstance(self.data['sequence'], libfyaml.FyGeneric))
        self.assertTrue(isinstance(self.data['mapping'], libfyaml.FyGeneric))

    def test_isinstance_both_types(self):
        """Test that isinstance() works for both wrapped and wrapper type"""
        int_val = self.data['integer']
        self.assertTrue(isinstance(int_val, int))
        self.assertTrue(isinstance(int_val, libfyaml.FyGeneric))

    def test_isinstance_with_tuple(self):
        """Test isinstance() with multiple types"""
        int_val = self.data['integer']
        str_val = self.data['string']

        # Check against multiple types
        self.assertTrue(isinstance(int_val, (int, str)))
        self.assertTrue(isinstance(str_val, (int, str)))
        self.assertFalse(isinstance(int_val, (str, list, dict)))

    def test_cross_type_isinstance_fails(self):
        """Test that isinstance() correctly fails for wrong types"""
        int_val = self.data['integer']
        str_val = self.data['string']

        self.assertFalse(isinstance(int_val, str))
        self.assertFalse(isinstance(str_val, int))
        self.assertFalse(isinstance(int_val, (str, list, dict)))

    def test_class_attribute(self):
        """Test __class__ attribute returns correct type"""
        self.assertIs(self.data['integer'].__class__, int)
        self.assertIs(self.data['string'].__class__, str)
        self.assertIs(self.data['sequence'].__class__, list)
        self.assertIs(self.data['mapping'].__class__, dict)
        self.assertIs(self.data['bool_true'].__class__, bool)
        self.assertIs(self.data['float_value'].__class__, float)

    def test_type_returns_fygeneric(self):
        """Test that type() still returns FyGeneric"""
        int_val = self.data['integer']
        str_val = self.data['string']

        self.assertIs(type(int_val), libfyaml.FyGeneric)
        self.assertIs(type(str_val), libfyaml.FyGeneric)
        self.assertEqual(type(int_val).__name__, 'FyGeneric')

    def test_type_vs_class(self):
        """Test difference between type() and __class__"""
        int_val = self.data['integer']

        # __class__ returns wrapped type
        self.assertIs(int_val.__class__, int)

        # type() returns wrapper type
        self.assertIs(type(int_val), libfyaml.FyGeneric)

        # They are different
        self.assertIsNot(int_val.__class__, type(int_val))

    def test_isinstance_with_iteration(self):
        """Test isinstance() on iterated values"""
        users_yaml = """
        users:
          alice: 25
          bob: 30
          charlie: 35
        """
        data = libfyaml.loads(users_yaml)

        ages = []
        for value in data['users']:
            self.assertTrue(isinstance(value, int))
            ages.append(int(value))

        self.assertEqual(ages, [25, 30, 35])

    def test_isinstance_with_nested_structures(self):
        """Test isinstance() with nested structures"""
        users = self.data['nested']['users']
        self.assertTrue(isinstance(users, list))

        first_user = users[0]
        self.assertTrue(isinstance(first_user, dict))

        name = first_user['name']
        self.assertTrue(isinstance(name, str))

        age = first_user['age']
        self.assertTrue(isinstance(age, int))

    def test_issubclass_with_class(self):
        """Test issubclass() with __class__"""
        int_val = self.data['integer']

        self.assertTrue(issubclass(int_val.__class__, int))
        self.assertTrue(issubclass(int_val.__class__, object))

    def test_issubclass_with_type(self):
        """Test issubclass() with type()"""
        int_val = self.data['integer']

        self.assertTrue(issubclass(type(int_val), libfyaml.FyGeneric))
        self.assertTrue(issubclass(type(int_val), object))

    def test_class_assignment_raises_typeerror(self):
        """Test that __class__ assignment is not allowed"""
        int_val = self.data['integer']

        with self.assertRaises(TypeError) as cm:
            int_val.__class__ = str

        self.assertIn("__class__ assignment not supported", str(cm.exception))

    def test_isinstance_with_real_python_types(self):
        """Test that real Python types don't satisfy isinstance(x, FyGeneric)"""
        real_int = 42
        real_str = "hello"
        real_list = [1, 2, 3]

        # Real Python types satisfy their own isinstance
        self.assertTrue(isinstance(real_int, int))
        self.assertTrue(isinstance(real_str, str))
        self.assertTrue(isinstance(real_list, list))

        # But not FyGeneric
        self.assertFalse(isinstance(real_int, libfyaml.FyGeneric))
        self.assertFalse(isinstance(real_str, libfyaml.FyGeneric))
        self.assertFalse(isinstance(real_list, libfyaml.FyGeneric))

    def test_type_discrimination_pattern(self):
        """Test common type discrimination pattern"""
        def process_value(value):
            if isinstance(value, int):
                return value * 2
            elif isinstance(value, str):
                return value.upper()
            elif isinstance(value, list):
                return len(value)
            elif isinstance(value, dict):
                return list(value.keys())
            else:
                return None

        self.assertEqual(process_value(self.data['integer']), 84)
        self.assertEqual(process_value(self.data['string']), 'HELLO WORLD')
        self.assertEqual(process_value(self.data['sequence']), 3)
        self.assertEqual(process_value(self.data['mapping']), ['key1', 'key2'])

    def test_numeric_isinstance_pattern(self):
        """Test checking for numeric types"""
        int_val = self.data['integer']
        float_val = self.data['float_value']
        str_val = self.data['string']

        # int and float are both numbers
        self.assertTrue(isinstance(int_val, (int, float)))
        self.assertTrue(isinstance(float_val, (int, float)))

        # string is not
        self.assertFalse(isinstance(str_val, (int, float)))

    def test_null_value_behavior(self):
        """Test isinstance() with null values"""
        null_val = self.data['null_value']

        # isinstance works with type(None)
        self.assertTrue(isinstance(null_val, type(None)))

        # But the wrapper is not None itself
        self.assertIsNot(null_val, None)

        # It's still a FyGeneric
        self.assertTrue(isinstance(null_val, libfyaml.FyGeneric))

    def test_bool_is_also_int(self):
        """Test that bool satisfies isinstance(x, int) like Python"""
        # In Python, bool is a subclass of int
        bool_val = self.data['bool_true']

        # Should satisfy both bool and int
        self.assertTrue(isinstance(bool_val, bool))
        self.assertTrue(isinstance(bool_val, int))

        # __class__ should be bool
        self.assertIs(bool_val.__class__, bool)

    def test_empty_containers(self):
        """Test isinstance() with empty containers"""
        empty_list = self.data['empty_sequence']
        empty_dict = self.data['empty_mapping']
        empty_str = self.data['empty_string']

        self.assertTrue(isinstance(empty_list, list))
        self.assertTrue(isinstance(empty_dict, dict))
        self.assertTrue(isinstance(empty_str, str))

        # Still FyGeneric
        self.assertTrue(isinstance(empty_list, libfyaml.FyGeneric))
        self.assertTrue(isinstance(empty_dict, libfyaml.FyGeneric))
        self.assertTrue(isinstance(empty_str, libfyaml.FyGeneric))

    def test_all_types_are_fygeneric(self):
        """Test that all FyGeneric objects satisfy isinstance(x, FyGeneric)"""
        test_keys = [
            'null_value', 'bool_true', 'integer', 'float_value',
            'string', 'sequence', 'mapping'
        ]

        for key in test_keys:
            value = self.data[key]
            self.assertTrue(
                isinstance(value, libfyaml.FyGeneric),
                f"{key} should be instance of FyGeneric"
            )

    def test_duck_typing_compatibility(self):
        """Test that FyGeneric works with duck typing patterns"""
        def double_numeric(value):
            """Duck typing: works with any numeric type"""
            if isinstance(value, (int, float)):
                return value * 2
            raise TypeError("Expected numeric type")

        # Works with FyGeneric
        self.assertEqual(double_numeric(self.data['integer']), 84)
        self.assertEqual(double_numeric(self.data['float_value']), 3.14159 * 2)

        # Raises for non-numeric
        with self.assertRaises(TypeError):
            double_numeric(self.data['string'])


class TestIsinstanceEdgeCases(unittest.TestCase):
    """Test edge cases and corner cases"""

    def test_deeply_nested_isinstance(self):
        """Test isinstance() with deeply nested structures"""
        yaml_str = """
        level1:
          level2:
            level3:
              level4:
                value: 42
        """
        data = libfyaml.loads(yaml_str)

        # Navigate down the nesting
        l1 = data['level1']
        self.assertTrue(isinstance(l1, dict))

        l2 = l1['level2']
        self.assertTrue(isinstance(l2, dict))

        l3 = l2['level3']
        self.assertTrue(isinstance(l3, dict))

        l4 = l3['level4']
        self.assertTrue(isinstance(l4, dict))

        value = l4['value']
        self.assertTrue(isinstance(value, int))
        self.assertEqual(value, 42)

    def test_mixed_type_list(self):
        """Test isinstance() with mixed-type lists"""
        yaml_str = """
        mixed:
          - 42
          - hello
          - 3.14
          - true
          - [1, 2, 3]
          - {key: value}
        """
        data = libfyaml.loads(yaml_str)
        mixed = data['mixed']

        self.assertTrue(isinstance(mixed[0], int))
        self.assertTrue(isinstance(mixed[1], str))
        self.assertTrue(isinstance(mixed[2], float))
        self.assertTrue(isinstance(mixed[3], bool))
        self.assertTrue(isinstance(mixed[4], list))
        self.assertTrue(isinstance(mixed[5], dict))

    def test_isinstance_after_modification(self):
        """Test isinstance() after modifying mutable FyGeneric"""
        yaml_str = """
        users:
          alice: 25
        """
        data = libfyaml.loads(yaml_str, mutable=True)

        # Before modification
        users = data['users']
        self.assertTrue(isinstance(users, dict))

        # After modification (if mutable operations are implemented)
        # The type should still be correct
        self.assertTrue(isinstance(users, dict))

    def test_large_integer_isinstance(self):
        """Test isinstance() with large integers beyond C long long"""
        # This is at the boundary of long long
        yaml_str = "value: 9223372036854775807"
        data = libfyaml.loads(yaml_str)

        self.assertTrue(isinstance(data['value'], int))
        self.assertEqual(data['value'], 9223372036854775807)


def run_tests():
    """Run all tests"""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    # Add all test classes
    suite.addTests(loader.loadTestsFromTestCase(TestIsinstanceSupport))
    suite.addTests(loader.loadTestsFromTestCase(TestIsinstanceEdgeCases))

    # Run with verbose output
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    # Return exit code
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(run_tests())
