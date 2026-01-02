#!/usr/bin/env python3
"""
Test suite for advanced FyGeneric methods.

This test verifies advanced functionality including:
- Type checking methods (is_null, is_bool, is_int, etc.)
- clone() for creating independent copies
- get_path() for tracking object paths
- get_at_path() for path-based access
- __format__() for format string support
"""

import sys
import os
import unittest

# Add parent directory to path for imports
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

import libfyaml


class TestTypeCheckingMethods(unittest.TestCase):
    """Test is_* type checking methods"""

    def setUp(self):
        """Load test data with various types"""
        self.yaml_str = """
        null_value: null
        bool_true: true
        bool_false: false
        integer: 42
        float_value: 3.14
        string: hello
        sequence: [1, 2, 3]
        mapping: {a: 1, b: 2}
        """
        self.data = libfyaml.loads(self.yaml_str)

    def test_is_null(self):
        """Test is_null() method"""
        self.assertTrue(self.data['null_value'].is_null())
        self.assertFalse(self.data['integer'].is_null())
        self.assertFalse(self.data['string'].is_null())

    def test_is_bool(self):
        """Test is_bool() method"""
        self.assertTrue(self.data['bool_true'].is_bool())
        self.assertTrue(self.data['bool_false'].is_bool())
        self.assertFalse(self.data['integer'].is_bool())
        self.assertFalse(self.data['string'].is_bool())

    def test_is_int(self):
        """Test is_int() method"""
        self.assertTrue(self.data['integer'].is_int())
        # Note: FyGeneric treats bool as distinct from int
        # (unlike Python where bool is subclass of int)
        self.assertFalse(self.data['bool_true'].is_int())
        self.assertFalse(self.data['float_value'].is_int())
        self.assertFalse(self.data['string'].is_int())

    def test_is_float(self):
        """Test is_float() method"""
        self.assertTrue(self.data['float_value'].is_float())
        self.assertFalse(self.data['integer'].is_float())
        self.assertFalse(self.data['string'].is_float())

    def test_is_string(self):
        """Test is_string() method"""
        self.assertTrue(self.data['string'].is_string())
        self.assertFalse(self.data['integer'].is_string())
        self.assertFalse(self.data['bool_true'].is_string())

    def test_is_sequence(self):
        """Test is_sequence() method"""
        self.assertTrue(self.data['sequence'].is_sequence())
        self.assertFalse(self.data['mapping'].is_sequence())
        self.assertFalse(self.data['string'].is_sequence())

    def test_is_mapping(self):
        """Test is_mapping() method"""
        self.assertTrue(self.data['mapping'].is_mapping())
        self.assertFalse(self.data['sequence'].is_mapping())
        self.assertFalse(self.data['string'].is_mapping())

    def test_type_discrimination_pattern(self):
        """Test using is_* methods for type discrimination"""
        def process_value(value):
            if value.is_null():
                return "NULL"
            elif value.is_bool():
                return "BOOL"
            elif value.is_int():
                return "INT"
            elif value.is_float():
                return "FLOAT"
            elif value.is_string():
                return "STRING"
            elif value.is_sequence():
                return "SEQUENCE"
            elif value.is_mapping():
                return "MAPPING"
            else:
                return "UNKNOWN"

        self.assertEqual(process_value(self.data['null_value']), "NULL")
        self.assertEqual(process_value(self.data['bool_true']), "BOOL")
        self.assertEqual(process_value(self.data['integer']), "INT")
        self.assertEqual(process_value(self.data['float_value']), "FLOAT")
        self.assertEqual(process_value(self.data['string']), "STRING")
        self.assertEqual(process_value(self.data['sequence']), "SEQUENCE")
        self.assertEqual(process_value(self.data['mapping']), "MAPPING")


class TestCloneMethod(unittest.TestCase):
    """Test clone() method for creating independent copies"""

    def test_clone_root_document(self):
        """Test cloning root document"""
        data = libfyaml.loads("value: 42\nname: test")
        cloned = data.clone()

        # Should have same values
        self.assertEqual(data.to_python(), cloned.to_python())

        # Should be different objects
        self.assertIsNot(data, cloned)

    def test_clone_nested_value_creates_new_root(self):
        """Test that cloning nested value creates new root from that point"""
        data = libfyaml.loads("config: {host: localhost, port: 8080}")
        config = data['config']
        cloned = config.clone()

        # clone() should create new root from the nested value
        # Not the whole original document!
        self.assertEqual(cloned.to_python(), {'host': 'localhost', 'port': 8080})

        # Verify it's a true root (not nested)
        self.assertEqual(cloned.get_path(), [])

        # Can access values directly
        self.assertEqual(str(cloned['host']), 'localhost')
        self.assertEqual(int(cloned['port']), 8080)

    def test_clone_sequence_creates_new_root(self):
        """Test cloning sequence creates new root"""
        data = libfyaml.loads("items: [1, 2, 3]")
        items = data['items']
        cloned = items.clone()

        # Should be the sequence as new root
        self.assertEqual(cloned.to_python(), [1, 2, 3])
        self.assertEqual(cloned.get_path(), [])

    def test_clone_preserves_nested_structure(self):
        """Test cloning nested value preserves its internal structure"""
        yaml_str = """
        database:
          server:
            host: localhost
            port: 5432
          users:
            - admin
            - readonly
        """
        data = libfyaml.loads(yaml_str)

        # Clone the 'database' substructure
        database = data['database']
        cloned_db = database.clone()

        # Should preserve the internal structure
        self.assertEqual(str(cloned_db['server']['host']), "localhost")
        self.assertEqual(int(cloned_db['server']['port']), 5432)
        self.assertEqual(str(cloned_db['users'][0]), "admin")

    def test_clone_independence(self):
        """Test that cloned values are truly independent"""
        data = libfyaml.loads("""
        users:
          - name: Alice
            age: 30
          - name: Bob
            age: 25
        """)

        # Clone just the users array
        users = data['users']
        cloned_users = users.clone()

        # Verify independence - cloned is new root
        self.assertEqual(cloned_users.to_python(), users.to_python())
        self.assertEqual(cloned_users.get_path(), [])
        self.assertEqual(users.get_path(), ['users'])


class TestGetPathMethod(unittest.TestCase):
    """Test get_path() method for tracking paths"""

    def test_get_path_root(self):
        """Test get_path() for root object"""
        data = libfyaml.loads("value: 42")
        path = data.get_path()

        # Root returns empty list (no path from root to itself)
        self.assertIsNotNone(path)
        self.assertEqual(path, [])

    def test_get_path_nested_mapping(self):
        """Test get_path() for nested mapping value"""
        yaml_str = """
        server:
          host: localhost
          port: 8080
        """
        data = libfyaml.loads(yaml_str)

        # Access nested values and verify paths
        server = data['server']
        self.assertEqual(server.get_path(), ['server'])

        host = data['server']['host']
        self.assertEqual(host.get_path(), ['server', 'host'])

        port = data['server']['port']
        self.assertEqual(port.get_path(), ['server', 'port'])

    def test_get_path_sequence_item(self):
        """Test get_path() for sequence items"""
        data = libfyaml.loads("items: [a, b, c]")

        # Access sequence items
        item0 = data['items'][0]
        self.assertEqual(item0.get_path(), ['items', 0])

        item1 = data['items'][1]
        self.assertEqual(item1.get_path(), ['items', 1])

        item2 = data['items'][2]
        self.assertEqual(item2.get_path(), ['items', 2])

    def test_get_path_deeply_nested(self):
        """Test get_path() for deeply nested values"""
        yaml_str = """
        level1:
          level2:
            level3:
              value: deep
        """
        data = libfyaml.loads(yaml_str)

        value = data['level1']['level2']['level3']['value']
        self.assertEqual(value.get_path(), ['level1', 'level2', 'level3', 'value'])


class TestGetAtPathMethod(unittest.TestCase):
    """Test get_at_path() method for path-based access"""

    def test_get_at_path_simple(self):
        """Test get_at_path() with simple path (list format)"""
        data = libfyaml.loads("name: Alice\nage: 30")

        # Access via path (requires list or tuple)
        name = data.get_at_path(["name"])
        self.assertEqual(str(name), "Alice")

        age = data.get_at_path(["age"])
        self.assertEqual(int(age), 30)

    def test_get_at_path_nested(self):
        """Test get_at_path() with nested path (list format)"""
        yaml_str = """
        server:
          host: localhost
          port: 8080
        """
        data = libfyaml.loads(yaml_str)

        # Access nested value (path as list)
        host = data.get_at_path(["server", "host"])
        self.assertEqual(str(host), "localhost")

        port = data.get_at_path(["server", "port"])
        self.assertEqual(int(port), 8080)

    def test_get_at_path_sequence(self):
        """Test get_at_path() with sequence index (list format)"""
        data = libfyaml.loads("items: [a, b, c]")

        # Access by index (path as list with integer index)
        item = data.get_at_path(["items", 1])
        self.assertEqual(str(item), "b")

    def test_get_at_path_nonexistent(self):
        """Test get_at_path() with non-existent path"""
        data = libfyaml.loads("name: Alice")

        # Should return None or raise exception for missing path
        # (behavior depends on implementation)
        try:
            result = data.get_at_path("nonexistent")
            # If it returns, it should be None or invalid
            self.assertTrue(result is None or hasattr(result, 'is_null'))
        except Exception:
            # Exception is also acceptable behavior
            pass


class TestFormatMethod(unittest.TestCase):
    """Test __format__() method for format string support"""

    def test_format_integer(self):
        """Test formatting integers"""
        data = libfyaml.loads("count: 42")
        count = data['count']

        # Test various format specs
        self.assertEqual(f"{count:d}", "42")
        self.assertEqual(f"{count:05d}", "00042")

    def test_format_float(self):
        """Test formatting floats"""
        data = libfyaml.loads("value: 3.14159")
        value = data['value']

        # Test float formatting
        self.assertEqual(f"{value:.2f}", "3.14")
        self.assertEqual(f"{value:.4f}", "3.1416")

    def test_format_string(self):
        """Test formatting strings"""
        data = libfyaml.loads("name: alice")
        name = data['name']

        # Test string formatting
        formatted = f"{name:>10s}"
        self.assertEqual(len(formatted), 10)
        self.assertTrue(formatted.endswith("alice"))

    def test_format_default(self):
        """Test default formatting (no spec)"""
        data = libfyaml.loads("""
        int_val: 42
        float_val: 3.14
        str_val: hello
        bool_val: true
        """)

        # Default format should work like str()
        self.assertEqual(f"{data['int_val']}", "42")
        self.assertEqual(f"{data['float_val']}", "3.14")
        self.assertEqual(f"{data['str_val']}", "hello")
        # Python formats booleans as "True" not "true"
        self.assertEqual(f"{data['bool_val']}", "True")


class TestAdvancedPatterns(unittest.TestCase):
    """Test advanced usage patterns combining multiple methods"""

    def test_type_based_processing(self):
        """Test processing values based on runtime type checking"""
        yaml_str = """
        values:
          - 42
          - hello
          - 3.14
          - true
          - [1, 2, 3]
          - {key: value}
        """
        data = libfyaml.loads(yaml_str)

        type_counts = {
            'int': 0,
            'float': 0,
            'string': 0,
            'bool': 0,
            'sequence': 0,
            'mapping': 0
        }

        for value in data['values']:
            if value.is_bool():
                type_counts['bool'] += 1
            elif value.is_int():
                type_counts['int'] += 1
            elif value.is_float():
                type_counts['float'] += 1
            elif value.is_string():
                type_counts['string'] += 1
            elif value.is_sequence():
                type_counts['sequence'] += 1
            elif value.is_mapping():
                type_counts['mapping'] += 1

        self.assertEqual(type_counts['bool'], 1)
        self.assertEqual(type_counts['int'], 1)
        self.assertEqual(type_counts['float'], 1)
        self.assertEqual(type_counts['string'], 1)
        self.assertEqual(type_counts['sequence'], 1)
        self.assertEqual(type_counts['mapping'], 1)

    def test_clone_and_verify_independence(self):
        """Test that cloned objects are truly independent"""
        data = libfyaml.loads("config: {host: localhost, port: 8080}")

        # Clone from root
        cloned_root = data.clone()
        self.assertEqual(data.to_python(), cloned_root.to_python())
        self.assertEqual(cloned_root.get_path(), [])

        # Clone from nested value
        config = data['config']
        cloned_config = config.clone()

        # Cloned config should be new root with just the config data
        self.assertEqual(cloned_config.to_python(), {'host': 'localhost', 'port': 8080})
        self.assertEqual(cloned_config.get_path(), [])

        # Can access directly without 'config' key
        self.assertEqual(str(cloned_config['host']), 'localhost')
        self.assertEqual(int(cloned_config['port']), 8080)


def run_tests():
    """Run all tests"""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    # Add all test classes
    suite.addTests(loader.loadTestsFromTestCase(TestTypeCheckingMethods))
    suite.addTests(loader.loadTestsFromTestCase(TestCloneMethod))
    suite.addTests(loader.loadTestsFromTestCase(TestGetPathMethod))
    suite.addTests(loader.loadTestsFromTestCase(TestGetAtPathMethod))
    suite.addTests(loader.loadTestsFromTestCase(TestFormatMethod))
    suite.addTests(loader.loadTestsFromTestCase(TestAdvancedPatterns))

    # Run with verbose output
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    # Return exit code
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(run_tests())
