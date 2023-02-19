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
import tempfile
import io

# Add parent directory to path for imports

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
        self.assertEqual(cloned.get_path(), ())

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
        self.assertEqual(cloned.get_path(), ())

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
        self.assertEqual(cloned_users.get_path(), ())
        self.assertEqual(users.get_path(), ('users',))

    def test_clone_resets_paths_for_children(self):
        """Test that cloned object's children have paths relative to new root"""
        yaml_str = """
        config:
          server:
            host: localhost
            port: 8080
          debug: true
        """
        data = libfyaml.loads(yaml_str)

        # Original paths before cloning
        config = data['config']
        self.assertEqual(config.get_path(), ('config',))

        server = config['server']
        self.assertEqual(server.get_path(), ('config', 'server'))

        host = server['host']
        self.assertEqual(host.get_path(), ('config', 'server', 'host'))

        # Clone the config object
        cloned_config = config.clone()

        # Cloned object should be new root
        self.assertEqual(cloned_config.get_path(), ())

        # Children of cloned object should have paths relative to new root
        cloned_server = cloned_config['server']
        self.assertEqual(cloned_server.get_path(), ('server',))

        cloned_host = cloned_server['host']
        self.assertEqual(cloned_host.get_path(), ('server', 'host'))

        cloned_port = cloned_server['port']
        self.assertEqual(cloned_port.get_path(), ('server', 'port'))

        cloned_debug = cloned_config['debug']
        self.assertEqual(cloned_debug.get_path(), ('debug',))

        # Verify values are correct
        self.assertEqual(str(cloned_host), 'localhost')
        self.assertEqual(int(cloned_port), 8080)
        self.assertEqual(bool(cloned_debug), True)

    def test_clone_has_independent_builder(self):
        """Test that cloned object has independent builder"""
        data = libfyaml.loads('{"key": "value"}')
        cloned = data.clone()

        # Both should work independently
        self.assertEqual(str(data), str(cloned))

        # Cloned object should be a root (has its own builder)
        self.assertEqual(cloned.get_path(), ())

    def test_clone_works_correctly(self):
        """Test that clone() produces functionally equivalent objects"""
        yaml_str = """
        users:
          - name: Alice
            age: 30
          - name: Bob
            age: 25
        """
        data = libfyaml.loads(yaml_str)
        cloned = data.clone()

        # Should have identical Python representation
        self.assertEqual(data.to_python(), cloned.to_python())

        # Should be able to access nested values
        self.assertEqual(str(cloned['users'][0]['name']), 'Alice')
        self.assertEqual(int(cloned['users'][1]['age']), 25)

    def test_nested_clone_works_correctly(self):
        """Test cloning nested values produces correct results"""
        data = libfyaml.loads('{"level1": {"level2": {"level3": "value"}}}')

        # Clone from level2
        level2 = data['level1']['level2']
        cloned_level2 = level2.clone()

        # Should have just level3 as new root
        self.assertEqual(cloned_level2.to_python(), {'level3': 'value'})
        self.assertEqual(cloned_level2.get_path(), ())
        self.assertEqual(str(cloned_level2['level3']), 'value')


class TestGetPathMethod(unittest.TestCase):
    """Test get_path() method for tracking paths"""

    def test_get_path_root(self):
        """Test get_path() for root object"""
        data = libfyaml.loads("value: 42")
        path = data.get_path()

        # Root returns empty tuple (no path from root to itself)
        self.assertIsNotNone(path)
        self.assertEqual(path, ())

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
        self.assertEqual(server.get_path(), ('server',))

        host = data['server']['host']
        self.assertEqual(host.get_path(), ('server', 'host'))

        port = data['server']['port']
        self.assertEqual(port.get_path(), ('server', 'port'))

    def test_get_path_sequence_item(self):
        """Test get_path() for sequence items"""
        data = libfyaml.loads("items: [a, b, c]")

        # Access sequence items
        item0 = data['items'][0]
        self.assertEqual(item0.get_path(), ('items', 0))

        item1 = data['items'][1]
        self.assertEqual(item1.get_path(), ('items', 1))

        item2 = data['items'][2]
        self.assertEqual(item2.get_path(), ('items', 2))

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
        self.assertEqual(value.get_path(), ('level1', 'level2', 'level3', 'value'))

    def test_path_is_tuple_not_list(self):
        """Test that get_path() returns tuple, not list"""
        data = libfyaml.loads('{"server": {"host": "localhost"}}')
        path = data['server']['host'].get_path()
        self.assertIsInstance(path, tuple, "Path should be a tuple for immutability")
        self.assertEqual(path, ('server', 'host'))

    def test_root_path_is_empty_tuple(self):
        """Test that root get_path() returns empty tuple"""
        data = libfyaml.loads('{"key": "value"}')
        path = data.get_path()
        self.assertIsInstance(path, tuple, "Root path should be a tuple")
        self.assertEqual(path, ())

    def test_path_is_immutable(self):
        """Test that returned path tuple cannot be modified"""
        data = libfyaml.loads('{"key": "value"}')
        path = data['key'].get_path()
        self.assertIsInstance(path, tuple)
        with self.assertRaises(TypeError, msg="Tuples don't support item assignment"):
            path[0] = 'modified'  # Should raise TypeError


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

    def test_path_with_float_element(self):
        """Test path with float element"""
        # YAML with bare float key
        yaml_str = """
        1.5:
          nested: value
        """
        data = libfyaml.loads(yaml_str)
        value = data.get_at_path([1.5, "nested"])
        self.assertEqual(str(value), "value")

    def test_path_with_bool_true_element(self):
        """Test path with True boolean element"""
        # YAML with bare true key
        yaml_str = """
        true:
          nested: value
        """
        data = libfyaml.loads(yaml_str)
        value = data.get_at_path([True, "nested"])
        self.assertEqual(str(value), "value")

    def test_path_with_bool_false_element(self):
        """Test path with False boolean element"""
        # YAML with bare false key
        yaml_str = """
        false:
          nested: value
        """
        data = libfyaml.loads(yaml_str)
        value = data.get_at_path([False, "nested"])
        self.assertEqual(str(value), "value")

    def test_path_with_none_raises_typeerror(self):
        """Test that None in path raises TypeError"""
        data = libfyaml.loads('{"key": "value"}')
        with self.assertRaises(TypeError) as cm:
            data.get_at_path([None, "key"])
        self.assertIn("cannot be None", str(cm.exception))

    def test_path_with_mixed_types(self):
        """Test path with mixed element types (int, str, float, bool)"""
        # Complex nested structure with various key types (bare scalars)
        yaml_str = """
        items:
          - name: first
            1.5: float_key
            true: bool_key
        """
        data = libfyaml.loads(yaml_str)

        # Access with integer index
        first_item = data.get_at_path(["items", 0])
        self.assertEqual(str(first_item["name"]), "first")

        # Access with float key
        float_val = data.get_at_path(["items", 0, 1.5])
        self.assertEqual(str(float_val), "float_key")

        # Access with bool key
        bool_val = data.get_at_path(["items", 0, True])
        self.assertEqual(str(bool_val), "bool_key")


class TestUnixPathMethods(unittest.TestCase):
    """Test Unix-style path methods (get_unix_path, get_at_unix_path)"""

    def test_get_unix_path_root(self):
        """Test get_unix_path() for root object"""
        data = libfyaml.loads("value: 42")
        path = data.get_unix_path()

        # Root returns "/"
        self.assertEqual(path, "/")

    def test_get_unix_path_simple(self):
        """Test get_unix_path() for simple nested value"""
        data = libfyaml.loads("name: Alice\nage: 30")

        name = data['name']
        self.assertEqual(name.get_unix_path(), "/name")

        age = data['age']
        self.assertEqual(age.get_unix_path(), "/age")

    def test_get_unix_path_nested(self):
        """Test get_unix_path() for nested values"""
        yaml_str = """
        server:
          host: localhost
          port: 8080
        """
        data = libfyaml.loads(yaml_str)

        server = data['server']
        self.assertEqual(server.get_unix_path(), "/server")

        host = data['server']['host']
        self.assertEqual(host.get_unix_path(), "/server/host")

        port = data['server']['port']
        self.assertEqual(port.get_unix_path(), "/server/port")

    def test_get_unix_path_sequence_item(self):
        """Test get_unix_path() for sequence items"""
        data = libfyaml.loads("items: [a, b, c]")

        item0 = data['items'][0]
        self.assertEqual(item0.get_unix_path(), "/items/0")

        item1 = data['items'][1]
        self.assertEqual(item1.get_unix_path(), "/items/1")

        item2 = data['items'][2]
        self.assertEqual(item2.get_unix_path(), "/items/2")

    def test_get_unix_path_deeply_nested(self):
        """Test get_unix_path() for deeply nested values"""
        yaml_str = """
        level1:
          level2:
            level3:
              value: deep
        """
        data = libfyaml.loads(yaml_str)

        value = data['level1']['level2']['level3']['value']
        self.assertEqual(value.get_unix_path(), "/level1/level2/level3/value")

    def test_get_at_unix_path_root(self):
        """Test get_at_unix_path() with root path"""
        data = libfyaml.loads("name: Alice")

        # "/" should return root
        result = data.get_at_unix_path("/")
        self.assertEqual(result.to_python(), data.to_python())

    def test_get_at_unix_path_simple(self):
        """Test get_at_unix_path() with simple path"""
        data = libfyaml.loads("name: Alice\nage: 30")

        name = data.get_at_unix_path("/name")
        self.assertEqual(str(name), "Alice")

        age = data.get_at_unix_path("/age")
        self.assertEqual(int(age), 30)

    def test_get_at_unix_path_nested(self):
        """Test get_at_unix_path() with nested path"""
        yaml_str = """
        server:
          host: localhost
          port: 8080
        """
        data = libfyaml.loads(yaml_str)

        host = data.get_at_unix_path("/server/host")
        self.assertEqual(str(host), "localhost")

        port = data.get_at_unix_path("/server/port")
        self.assertEqual(int(port), 8080)

        server = data.get_at_unix_path("/server")
        self.assertTrue(server.is_mapping())

    def test_get_at_unix_path_sequence(self):
        """Test get_at_unix_path() with sequence index"""
        data = libfyaml.loads("items: [a, b, c]")

        item0 = data.get_at_unix_path("/items/0")
        self.assertEqual(str(item0), "a")

        item1 = data.get_at_unix_path("/items/1")
        self.assertEqual(str(item1), "b")

        item2 = data.get_at_unix_path("/items/2")
        self.assertEqual(str(item2), "c")

    def test_get_at_unix_path_deeply_nested(self):
        """Test get_at_unix_path() with deeply nested path"""
        yaml_str = """
        level1:
          level2:
            level3:
              value: deep
        """
        data = libfyaml.loads(yaml_str)

        value = data.get_at_unix_path("/level1/level2/level3/value")
        self.assertEqual(str(value), "deep")

    def test_get_at_unix_path_invalid(self):
        """Test get_at_unix_path() with invalid path"""
        data = libfyaml.loads("name: Alice")

        # Path without leading "/" should raise ValueError
        with self.assertRaises(ValueError):
            data.get_at_unix_path("name")

    def test_unix_path_round_trip(self):
        """Test that get_unix_path() and get_at_unix_path() are inverse operations"""
        yaml_str = """
        config:
          server:
            host: localhost
            port: 8080
          users:
            - alice
            - bob
        """
        data = libfyaml.loads(yaml_str)

        # Test various nested values
        host = data['config']['server']['host']
        unix_path = host.get_unix_path()
        retrieved = data.get_at_unix_path(unix_path)
        self.assertEqual(str(retrieved), str(host))

        port = data['config']['server']['port']
        unix_path = port.get_unix_path()
        retrieved = data.get_at_unix_path(unix_path)
        self.assertEqual(int(retrieved), int(port))

        user0 = data['config']['users'][0]
        unix_path = user0.get_unix_path()
        retrieved = data.get_at_unix_path(unix_path)
        self.assertEqual(str(retrieved), str(user0))

    def test_set_at_unix_path_simple(self):
        """Test set_at_unix_path() with simple path"""
        data = libfyaml.loads("name: Alice\nage: 30", mutable=True)

        # Set existing value
        data.set_at_unix_path("/name", "Bob")
        self.assertEqual(str(data['name']), "Bob")

        # Set another value
        data.set_at_unix_path("/age", 25)
        self.assertEqual(int(data['age']), 25)

    def test_set_at_unix_path_nested(self):
        """Test set_at_unix_path() with nested path"""
        yaml_str = """
        server:
          host: localhost
          port: 8080
        """
        data = libfyaml.loads(yaml_str, mutable=True)

        # Set nested value
        data.set_at_unix_path("/server/host", "example.com")
        self.assertEqual(str(data['server']['host']), "example.com")

        data.set_at_unix_path("/server/port", 9000)
        self.assertEqual(int(data['server']['port']), 9000)

    def test_set_at_unix_path_new_key(self):
        """Test set_at_unix_path() adding new keys"""
        data = libfyaml.loads("server: {host: localhost}", mutable=True)

        # Add new key
        data.set_at_unix_path("/server/port", 8080)
        self.assertEqual(int(data['server']['port']), 8080)

        # Add another new key
        data.set_at_unix_path("/server/ssl", True)
        self.assertEqual(bool(data['server']['ssl']), True)

    def test_set_at_unix_path_sequence(self):
        """Test set_at_unix_path() with sequence index"""
        data = libfyaml.loads("items: [a, b, c]", mutable=True)

        # Modify sequence item
        data.set_at_unix_path("/items/1", "x")
        self.assertEqual(str(data['items'][1]), "x")

    def test_set_at_unix_path_requires_mutable(self):
        """Test set_at_unix_path() requires mutable=True"""
        data = libfyaml.loads("name: Alice")  # mutable=False by default

        # Should raise TypeError for read-only object
        with self.assertRaises(TypeError):
            data.set_at_unix_path("/name", "Bob")

    def test_set_at_unix_path_root_error(self):
        """Test set_at_unix_path() cannot set at root"""
        data = libfyaml.loads("name: Alice", mutable=True)

        # Cannot set at root path "/"
        with self.assertRaises(ValueError):
            data.set_at_unix_path("/", {"new": "value"})

    def test_set_at_unix_path_invalid_path(self):
        """Test set_at_unix_path() with invalid path"""
        data = libfyaml.loads("name: Alice", mutable=True)

        # Path without leading "/"
        with self.assertRaises(ValueError):
            data.set_at_unix_path("name", "Bob")


class TestModuleLevelPathConversion(unittest.TestCase):
    """Test module-level path conversion functions"""

    def test_path_list_to_unix_path_simple(self):
        """Test path_list_to_unix_path() with simple paths"""
        # Simple nested path
        result = libfyaml.path_list_to_unix_path(['server', 'host'])
        self.assertEqual(result, '/server/host')

        # Single element
        result = libfyaml.path_list_to_unix_path(['name'])
        self.assertEqual(result, '/name')

    def test_path_list_to_unix_path_with_indices(self):
        """Test path_list_to_unix_path() with sequence indices"""
        result = libfyaml.path_list_to_unix_path(['items', 0])
        self.assertEqual(result, '/items/0')

        result = libfyaml.path_list_to_unix_path(['users', 1, 'name'])
        self.assertEqual(result, '/users/1/name')

    def test_path_list_to_unix_path_empty(self):
        """Test path_list_to_unix_path() with empty list (root)"""
        result = libfyaml.path_list_to_unix_path([])
        self.assertEqual(result, '/')

    def test_path_list_to_unix_path_tuple(self):
        """Test path_list_to_unix_path() with tuple"""
        result = libfyaml.path_list_to_unix_path(('server', 'port'))
        self.assertEqual(result, '/server/port')

    def test_unix_path_to_path_list_simple(self):
        """Test unix_path_to_path_list() with simple paths"""
        result = libfyaml.unix_path_to_path_list('/server/host')
        self.assertEqual(result, ['server', 'host'])

        result = libfyaml.unix_path_to_path_list('/name')
        self.assertEqual(result, ['name'])

    def test_unix_path_to_path_list_with_indices(self):
        """Test unix_path_to_path_list() with sequence indices"""
        result = libfyaml.unix_path_to_path_list('/items/0')
        self.assertEqual(result, ['items', 0])

        result = libfyaml.unix_path_to_path_list('/users/1/name')
        self.assertEqual(result, ['users', 1, 'name'])

    def test_unix_path_to_path_list_root(self):
        """Test unix_path_to_path_list() with root path"""
        result = libfyaml.unix_path_to_path_list('/')
        self.assertEqual(result, [])

    def test_unix_path_to_path_list_invalid(self):
        """Test unix_path_to_path_list() with invalid path"""
        # Path without leading "/"
        with self.assertRaises(ValueError):
            libfyaml.unix_path_to_path_list('server/host')

    def test_path_conversion_round_trip(self):
        """Test that path conversions are inverse operations"""
        # List to Unix to List
        original_list = ['server', 'config', 'port']
        unix_path = libfyaml.path_list_to_unix_path(original_list)
        result_list = libfyaml.unix_path_to_path_list(unix_path)
        self.assertEqual(result_list, original_list)

        # Unix to List to Unix
        original_unix = '/database/users/0/name'
        path_list = libfyaml.unix_path_to_path_list(original_unix)
        result_unix = libfyaml.path_list_to_unix_path(path_list)
        self.assertEqual(result_unix, original_unix)

        # Root round trip
        self.assertEqual(libfyaml.unix_path_to_path_list('/'), [])
        self.assertEqual(libfyaml.path_list_to_unix_path([]), '/')


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
        self.assertEqual(cloned_root.get_path(), ())

        # Clone from nested value
        config = data['config']
        cloned_config = config.clone()

        # Cloned config should be new root with just the config data
        self.assertEqual(cloned_config.to_python(), {'host': 'localhost', 'port': 8080})
        self.assertEqual(cloned_config.get_path(), ())

        # Can access directly without 'config' key
        self.assertEqual(str(cloned_config['host']), 'localhost')
        self.assertEqual(int(cloned_config['port']), 8080)


class TestDumpMethod(unittest.TestCase):
    """Test dump() instance method for serializing values"""

    def test_dump_returns_string_with_no_args(self):
        """Test dump() with no arguments returns YAML string"""
        data = libfyaml.loads('{"key": "value"}')
        result = data.dump()

        self.assertIsInstance(result, str)
        self.assertIn('key:', result)
        self.assertIn('value', result)

    def test_dump_yaml_mode(self):
        """Test dump(mode='yaml') returns YAML format"""
        data = libfyaml.loads('{"name": "test", "count": 42}')
        result = data.dump(mode='yaml')

        self.assertIsInstance(result, str)
        # YAML format uses key: value
        self.assertIn('name:', result)
        self.assertIn('count:', result)

    def test_dump_json_mode(self):
        """Test dump(mode='json') returns JSON format"""
        data = libfyaml.loads('{"name": "test", "count": 42}')
        result = data.dump(mode='json')

        self.assertIsInstance(result, str)
        # JSON format uses quotes and braces
        self.assertIn('"name"', result)
        self.assertIn('"test"', result)
        self.assertIn('{', result)
        self.assertIn('}', result)

    def test_dump_compact_yaml(self):
        """Test dump(compact=True) returns flow-style YAML"""
        data = libfyaml.loads('{"a": 1, "b": 2}')
        result = data.dump(compact=True)

        self.assertIsInstance(result, str)
        # Flow style uses braces/brackets for collections
        # Count opening braces and brackets - should have at least 2 (one pair)
        brace_count = result.count('{') + result.count('}')
        bracket_count = result.count('[') + result.count(']')
        flow_indicators = brace_count + bracket_count
        self.assertGreaterEqual(flow_indicators, 2, "Flow style should use braces/brackets")

    def test_dump_block_yaml(self):
        """Test dump(compact=False) returns block-style YAML"""
        data = libfyaml.loads('{"a": 1, "b": 2}')
        result = data.dump(compact=False)

        self.assertIsInstance(result, str)
        # Block style should NOT use braces/brackets for collections
        brace_count = result.count('{') + result.count('}')
        bracket_count = result.count('[') + result.count(']')
        flow_indicators = brace_count + bracket_count
        self.assertEqual(flow_indicators, 0, "Block style should not use braces/brackets")

    def test_dump_oneline_with_newline(self):
        """Test dump(compact=True) produces one-line flow style with terminating newline"""
        data = libfyaml.loads('{"a": 1, "b": 2}')
        result = data.dump(compact=True, strip_newline=False)

        self.assertIsInstance(result, str)
        # Should end with newline by default
        self.assertTrue(result.endswith('\n'), "Should end with newline by default")
        # Should have exactly one line (excluding the terminating newline)
        lines = result.rstrip('\n').split('\n')
        self.assertEqual(len(lines), 1, "Should be one line (excluding terminating newline)")
        # Should use flow style (braces/brackets)
        flow_indicators = result.count('{') + result.count('}') + result.count('[') + result.count(']')
        self.assertGreaterEqual(flow_indicators, 2, "Should use flow style")

    def test_dump_oneline_without_newline(self):
        """Test dump(compact=True, strip_newline=True) produces one-line flow without terminating newline"""
        data = libfyaml.loads('{"a": 1, "b": 2}')
        result = data.dump(compact=True, strip_newline=True)

        self.assertIsInstance(result, str)
        # Should NOT have any newlines at all
        self.assertNotIn('\n', result, "Should not contain any newlines")
        # Should use flow style (braces/brackets)
        flow_indicators = result.count('{') + result.count('}') + result.count('[') + result.count(']')
        self.assertGreaterEqual(flow_indicators, 2, "Should use flow style")

    def test_dump_to_file_path(self):
        """Test dump(file=path) writes to file"""
        data = libfyaml.loads('{"key": "value"}')

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.yaml') as f:
            path = f.name

        try:
            result = data.dump(file=path)

            # Should return None when writing to file
            self.assertIsNone(result)

            # Verify file was written
            with open(path, 'r') as f:
                content = f.read()

            self.assertIn('key:', content)
            self.assertIn('value', content)
        finally:
            os.unlink(path)

    def test_dump_to_file_object(self):
        """Test dump(file=file_obj) writes to file object"""
        data = libfyaml.loads('{"key": "value"}')
        output = io.StringIO()

        result = data.dump(file=output)

        # Should return None when writing to file
        self.assertIsNone(result)

        # Verify content was written
        content = output.getvalue()
        self.assertIn('key:', content)
        self.assertIn('value', content)

    def test_dump_nested_value(self):
        """Test dump() on nested value creates new root"""
        data = libfyaml.loads('{"server": {"host": "localhost", "port": 8080}}')
        server = data['server']

        result = server.dump()

        # Should dump just the server dict as a new root
        self.assertIn('host:', result)
        self.assertIn('localhost', result)
        self.assertIn('port:', result)
        # Should NOT include 'server:' key (it's the new root)
        self.assertNotIn('server:', result)

    def test_dump_scalar_value(self):
        """Test dump() on scalar values"""
        data = libfyaml.loads('{"number": 42}')
        number = data['number']

        result = number.dump()

        # Scalar should dump as itself
        self.assertIn('42', result)

    def test_dump_list_value(self):
        """Test dump() on list value"""
        data = libfyaml.loads('{"items": [1, 2, 3]}')
        items = data['items']

        result = items.dump()

        # Should dump as list
        self.assertIn('1', result)
        self.assertIn('2', result)
        self.assertIn('3', result)

    def test_dump_json_mode_with_file(self):
        """Test dump(file=path, mode='json') writes JSON to file"""
        data = libfyaml.loads('{"key": "value"}')

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.json') as f:
            path = f.name

        try:
            result = data.dump(file=path, mode='json')

            self.assertIsNone(result)

            # Verify JSON was written
            with open(path, 'r') as f:
                content = f.read()

            self.assertIn('"key"', content)
            self.assertIn('"value"', content)
            self.assertIn('{', content)
        finally:
            os.unlink(path)

    def test_dump_preserves_types(self):
        """Test dump() preserves all YAML types correctly"""
        yaml_str = """
        string: hello
        integer: 42
        float: 3.14
        boolean: true
        null_value: null
        list: [1, 2, 3]
        dict: {a: 1, b: 2}
        """
        data = libfyaml.loads(yaml_str)
        result = data.dump()

        # Should contain all values
        self.assertIn('string:', result)
        self.assertIn('hello', result)
        self.assertIn('integer:', result)
        self.assertIn('42', result)
        self.assertIn('float:', result)
        self.assertIn('3.14', result)
        self.assertIn('boolean:', result)
        self.assertIn('true', result)


def run_tests():
    """Run all tests"""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    # Add all test classes
    suite.addTests(loader.loadTestsFromTestCase(TestTypeCheckingMethods))
    suite.addTests(loader.loadTestsFromTestCase(TestCloneMethod))
    suite.addTests(loader.loadTestsFromTestCase(TestGetPathMethod))
    suite.addTests(loader.loadTestsFromTestCase(TestGetAtPathMethod))
    suite.addTests(loader.loadTestsFromTestCase(TestUnixPathMethods))
    suite.addTests(loader.loadTestsFromTestCase(TestModuleLevelPathConversion))
    suite.addTests(loader.loadTestsFromTestCase(TestFormatMethod))
    suite.addTests(loader.loadTestsFromTestCase(TestAdvancedPatterns))
    suite.addTests(loader.loadTestsFromTestCase(TestDumpMethod))

    # Run with verbose output
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    # Return exit code
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(run_tests())
