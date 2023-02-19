"""
Core functionality tests for libfyaml Python bindings.

Run with: python3 -m pytest tests/
Or: python3 tests/test_core.py
"""

import sys
import unittest
import libfyaml


class TestLoads(unittest.TestCase):
    """Test YAML/JSON parsing."""

    def test_simple_dict(self):
        """Test parsing simple dictionary."""
        doc = libfyaml.loads("name: Alice\nage: 30")
        self.assertEqual(doc['name'], 'Alice')
        self.assertEqual(doc['age'], 30)

    def test_simple_list(self):
        """Test parsing simple list."""
        doc = libfyaml.loads("- apple\n- banana\n- cherry")
        self.assertEqual(len(doc), 3)
        self.assertEqual(doc[0], 'apple')
        self.assertEqual(doc[1], 'banana')
        self.assertEqual(doc[2], 'cherry')

    def test_nested_structure(self):
        """Test parsing nested structure."""
        yaml = """
        user:
          name: Bob
          age: 25
          tags: [admin, user]
        """
        doc = libfyaml.loads(yaml)
        self.assertEqual(doc['user']['name'], 'Bob')
        self.assertEqual(doc['user']['age'], 25)
        self.assertEqual(len(doc['user']['tags']), 2)

    def test_json_mode(self):
        """Test JSON parsing mode."""
        json_str = '{"name": "Charlie", "age": 35}'
        doc = libfyaml.loads(json_str, mode='json')
        self.assertEqual(doc['name'], 'Charlie')
        self.assertEqual(doc['age'], 35)

    def test_empty_document(self):
        """Test empty document raises ValueError."""
        with self.assertRaises(ValueError):
            libfyaml.loads("")


class TestDumps(unittest.TestCase):
    """Test YAML/JSON emission."""

    def test_simple_dumps(self):
        """Test dumps basic functionality."""
        doc = libfyaml.loads("name: Alice\nage: 30")
        yaml = libfyaml.dumps(doc)
        self.assertIn('name:', yaml)
        self.assertIn('Alice', yaml)
        self.assertIn('age:', yaml)
        self.assertIn('30', yaml)

    def test_round_trip(self):
        """Test parse -> dump -> parse round-trip."""
        original = "name: Bob\nage: 25\nitems: [a, b, c]"
        doc1 = libfyaml.loads(original)
        yaml = libfyaml.dumps(doc1)
        doc2 = libfyaml.loads(yaml)

        self.assertEqual(doc1['name'], doc2['name'])
        self.assertEqual(doc1['age'], doc2['age'])
        self.assertEqual(len(doc1['items']), len(doc2['items']))


class TestMultiDocument(unittest.TestCase):
    """Test multi-document YAML support."""

    def test_loads_all(self):
        """Test loading multiple documents."""
        yaml = "---\nname: Alice\n---\nname: Bob\n---\nname: Charlie"
        docs = libfyaml.loads_all(yaml)

        self.assertEqual(len(docs), 3)
        self.assertEqual(docs[0]['name'], 'Alice')
        self.assertEqual(docs[1]['name'], 'Bob')
        self.assertEqual(docs[2]['name'], 'Charlie')

    def test_dumps_all(self):
        """Test dumping multiple documents."""
        docs = libfyaml.loads_all("---\na: 1\n---\nb: 2")
        yaml = libfyaml.dumps_all(docs)

        self.assertIn('a:', yaml)
        self.assertIn('b:', yaml)


class TestDictInterface(unittest.TestCase):
    """Test dict-like interface."""

    def test_key_access(self):
        """Test dictionary key access."""
        doc = libfyaml.loads("a: 1\nb: 2\nc: 3")
        self.assertEqual(doc['a'], 1)
        self.assertEqual(doc['b'], 2)
        self.assertEqual(doc['c'], 3)

    def test_get_method(self):
        """Test get() method with default."""
        doc = libfyaml.loads("name: Alice")
        self.assertEqual(doc.get('name'), 'Alice')
        self.assertEqual(doc.get('missing', 'default'), 'default')

    def test_contains(self):
        """Test 'in' operator."""
        doc = libfyaml.loads("name: Alice\nage: 30")
        self.assertIn('name', doc)
        self.assertIn('age', doc)
        self.assertNotIn('missing', doc)

    def test_keys(self):
        """Test keys() method."""
        doc = libfyaml.loads("a: 1\nb: 2\nc: 3")
        keys = list(doc.keys())
        self.assertIn('a', keys)
        self.assertIn('b', keys)
        self.assertIn('c', keys)

    def test_values(self):
        """Test values() method."""
        doc = libfyaml.loads("a: 1\nb: 2\nc: 3")
        values = list(doc.values())
        self.assertIn(1, values)
        self.assertIn(2, values)
        self.assertIn(3, values)

    def test_items(self):
        """Test items() method."""
        doc = libfyaml.loads("a: 1\nb: 2")
        items = list(doc.items())
        self.assertEqual(len(items), 2)
        # Verify keys and values
        keys = [k for k, v in items]
        values = [v for k, v in items]
        self.assertIn('a', keys)
        self.assertIn('b', keys)
        self.assertIn(1, values)
        self.assertIn(2, values)


class TestListInterface(unittest.TestCase):
    """Test list-like interface."""

    def test_index_access(self):
        """Test list index access."""
        doc = libfyaml.loads("- a\n- b\n- c")
        self.assertEqual(doc[0], 'a')
        self.assertEqual(doc[1], 'b')
        self.assertEqual(doc[2], 'c')

    def test_length(self):
        """Test len() function."""
        doc = libfyaml.loads("- a\n- b\n- c")
        self.assertEqual(len(doc), 3)

    def test_iteration(self):
        """Test iteration over list."""
        doc = libfyaml.loads("- a\n- b\n- c")
        items = list(doc)
        self.assertEqual(items, ['a', 'b', 'c'])


class TestArithmetic(unittest.TestCase):
    """Test arithmetic operations."""

    def test_addition(self):
        """Test addition with integers."""
        doc = libfyaml.loads("value: 10")
        result = doc['value'] + 5
        self.assertEqual(result, 15)

    def test_subtraction(self):
        """Test subtraction with integers."""
        doc = libfyaml.loads("value: 10")
        result = doc['value'] - 3
        self.assertEqual(result, 7)

    def test_multiplication(self):
        """Test multiplication with integers."""
        doc = libfyaml.loads("value: 10")
        result = doc['value'] * 2
        self.assertEqual(result, 20)


class TestComparison(unittest.TestCase):
    """Test comparison operations."""

    def test_equality(self):
        """Test equality comparison."""
        doc = libfyaml.loads("value: 42")
        self.assertTrue(doc['value'] == 42)
        self.assertFalse(doc['value'] == 10)

    def test_inequality(self):
        """Test inequality comparison."""
        doc = libfyaml.loads("value: 42")
        self.assertTrue(doc['value'] != 10)
        self.assertFalse(doc['value'] != 42)

    def test_ordering(self):
        """Test ordering comparisons."""
        doc = libfyaml.loads("value: 42")
        self.assertTrue(doc['value'] > 10)
        self.assertTrue(doc['value'] >= 42)
        self.assertTrue(doc['value'] < 100)
        self.assertTrue(doc['value'] <= 42)


class TestStringMethods(unittest.TestCase):
    """Test string method delegation."""

    def test_upper(self):
        """Test upper() method."""
        doc = libfyaml.loads("name: alice")
        self.assertEqual(doc['name'].upper(), 'ALICE')

    def test_lower(self):
        """Test lower() method."""
        doc = libfyaml.loads("name: ALICE")
        self.assertEqual(doc['name'].lower(), 'alice')

    def test_startswith(self):
        """Test startswith() method."""
        doc = libfyaml.loads("name: Alice")
        self.assertTrue(doc['name'].startswith('Ali'))
        self.assertFalse(doc['name'].startswith('Bob'))


class TestConversion(unittest.TestCase):
    """Test conversion to Python objects."""

    def test_to_python_dict(self):
        """Test to_python() on dictionary."""
        doc = libfyaml.loads("a: 1\nb: 2")
        py_obj = doc.to_python()
        self.assertIsInstance(py_obj, dict)
        self.assertEqual(py_obj['a'], 1)
        self.assertEqual(py_obj['b'], 2)

    def test_to_python_list(self):
        """Test to_python() on list."""
        doc = libfyaml.loads("- a\n- b\n- c")
        py_obj = doc.to_python()
        self.assertIsInstance(py_obj, list)
        self.assertEqual(py_obj, ['a', 'b', 'c'])

    def test_copy_method(self):
        """Test copy() method."""
        doc = libfyaml.loads("name: Alice\nage: 30")
        py_obj = doc.copy()
        self.assertIsInstance(py_obj, dict)
        self.assertEqual(py_obj['name'], 'Alice')
        self.assertEqual(py_obj['age'], 30)

    def test_dict_constructor(self):
        """Test dict-like iteration."""
        doc = libfyaml.loads("a: 1\nb: 2")
        # Use to_python() for full conversion (dict(doc) requires hashable values)
        py_dict = doc.to_python()
        self.assertIsInstance(py_dict, dict)
        self.assertEqual(py_dict['a'], 1)
        self.assertEqual(py_dict['b'], 2)


class TestFromPython(unittest.TestCase):
    """Test converting Python objects to FyGeneric."""

    def test_from_dict(self):
        """Test converting dict."""
        py_dict = {'name': 'Alice', 'age': 30}
        doc = libfyaml.from_python(py_dict)
        self.assertEqual(doc['name'], 'Alice')
        self.assertEqual(doc['age'], 30)

    def test_from_list(self):
        """Test converting list."""
        py_list = ['a', 'b', 'c']
        doc = libfyaml.from_python(py_list)
        self.assertEqual(len(doc), 3)
        self.assertEqual(doc[0], 'a')

    def test_round_trip_python(self):
        """Test Python -> FyGeneric -> Python round-trip."""
        original = {'users': [{'name': 'Alice', 'age': 30}, {'name': 'Bob', 'age': 25}]}
        doc = libfyaml.from_python(original)
        result = doc.to_python()
        self.assertEqual(original, result)


def run_tests():
    """Run all tests."""
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromModule(sys.modules[__name__])
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(run_tests())
