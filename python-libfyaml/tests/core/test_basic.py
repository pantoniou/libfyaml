#!/usr/bin/env python3
"""
Basic tests for libfyaml Python bindings.

These tests demonstrate the NumPy-like interface where parsed data stays
in native format with lazy conversion to Python objects.
"""

import sys
import os

# Add parent directory to path for imports

import libfyaml


def test_load_file():
    """Test loading YAML from file."""
    print("=" * 60)
    print("TEST: Load YAML from file")
    print("=" * 60)

    fixture_path = os.path.join(os.path.dirname(__file__), 'fixtures', 'config.yaml')
    doc = libfyaml.load(fixture_path)

    print(f"Document type: {type(doc)}")
    print(f"Document repr: {repr(doc)}")
    print()


def test_lazy_access():
    """Test lazy access without full conversion."""
    print("=" * 60)
    print("TEST: Lazy access (NumPy-like behavior)")
    print("=" * 60)

    fixture_path = os.path.join(os.path.dirname(__file__), 'fixtures', 'config.yaml')
    doc = libfyaml.load(fixture_path)

    # Access nested values - returns FyGeneric wrappers, not Python dicts
    server = doc["server"]
    print(f"server type: {type(server)}")
    print(f"server repr: {repr(server)}")

    host = server["host"]
    print(f"host type: {type(host)}")
    print(f"host repr: {repr(host)}")

    # Convert individual values when needed
    host_str = str(host)
    print(f"host value: {host_str}")

    port = server["port"]
    port_int = int(port)
    print(f"port value: {port_int}")

    # Type checking
    print(f"host is_string(): {host.is_string()}")
    print(f"port is_int(): {port.is_int()}")
    print(f"server is_mapping(): {server.is_mapping()}")
    print()


def test_sequence_access():
    """Test sequence (list) access and iteration."""
    print("=" * 60)
    print("TEST: Sequence access and iteration")
    print("=" * 60)

    fixture_path = os.path.join(os.path.dirname(__file__), 'fixtures', 'config.yaml')
    doc = libfyaml.load(fixture_path)

    # Access sequence
    items = doc["items"]
    print(f"items type: {type(items)}")
    print(f"items is_sequence(): {items.is_sequence()}")
    print(f"items length: {len(items)}")

    # Access by index
    first_item = items[0]
    print(f"first_item type: {type(first_item)}")
    print(f"first_item['name']: {str(first_item['name'])}")
    print(f"first_item['price']: {float(first_item['price'])}")

    # Iterate - yields FyGeneric objects, not full Python dicts
    print("\nIterating over items (lazy):")
    for i, item in enumerate(items):
        item_id = int(item["id"])
        item_name = str(item["name"])
        item_price = float(item["price"])
        print(f"  Item {i}: id={item_id}, name={item_name}, price=${item_price}")
    print()


def test_mapping_operations():
    """Test mapping (dict) operations."""
    print("=" * 60)
    print("TEST: Mapping operations")
    print("=" * 60)

    fixture_path = os.path.join(os.path.dirname(__file__), 'fixtures', 'config.yaml')
    doc = libfyaml.load(fixture_path)

    server = doc["server"]

    # Keys, values, items
    print("Server keys:")
    for key in server.keys():
        print(f"  - {key}")

    print("\nServer values:")
    for value in server.values():
        print(f"  - {value}")

    print("\nServer items:")
    for key, value in server.items():
        print(f"  {key}: {value}")
    print()


def test_full_conversion():
    """Test converting entire document to Python objects."""
    print("=" * 60)
    print("TEST: Full conversion to Python objects")
    print("=" * 60)

    fixture_path = os.path.join(os.path.dirname(__file__), 'fixtures', 'config.yaml')
    doc = libfyaml.load(fixture_path)

    # Convert entire document to Python dict
    python_dict = doc.to_python()
    print(f"Converted type: {type(python_dict)}")
    print(f"Server host: {python_dict['server']['host']}")
    print(f"Database type: {python_dict['database']['type']}")
    print(f"First item name: {python_dict['items'][0]['name']}")
    print()


def test_loads_string():
    """Test loading YAML from string."""
    print("=" * 60)
    print("TEST: Load YAML from string")
    print("=" * 60)

    yaml_str = """
name: Alice
age: 30
active: true
balance: 1234.56
hobbies:
  - reading
  - coding
  - hiking
contact:
  email: alice@example.com
  phone: "555-1234"
"""

    doc = libfyaml.loads(yaml_str)

    print(f"name: {str(doc['name'])}")
    print(f"age: {int(doc['age'])}")
    print(f"active: {bool(doc['active'])}")
    print(f"balance: {float(doc['balance'])}")

    print("\nHobbies:")
    for hobby in doc['hobbies']:
        print(f"  - {str(hobby)}")

    print(f"\nEmail: {str(doc['contact']['email'])}")
    print()


def test_type_conversions():
    """Test automatic type conversions."""
    print("=" * 60)
    print("TEST: Type conversions")
    print("=" * 60)

    yaml_str = """
null_value: null
bool_true: true
bool_false: false
int_value: 42
float_value: 3.14159
string_value: "Hello, World!"
"""

    doc = libfyaml.loads(yaml_str)

    # Test null
    null_val = doc["null_value"]
    print(f"null_value is_null(): {null_val.is_null()}")
    print(f"null_value bool(): {bool(null_val)}")

    # Test bool
    bool_t = doc["bool_true"]
    print(f"bool_true: {bool(bool_t)}")
    print(f"bool_false: {bool(doc['bool_false'])}")

    # Test int
    int_val = doc["int_value"]
    print(f"int_value: {int(int_val)}")
    print(f"int_value as str: {str(int_val)}")

    # Test float
    float_val = doc["float_value"]
    print(f"float_value: {float(float_val)}")
    print(f"float_value as str: {str(float_val)}")

    # Test string
    str_val = doc["string_value"]
    print(f"string_value: {str(str_val)}")
    print()


def test_performance_comparison():
    """Demonstrate memory efficiency vs full conversion."""
    print("=" * 60)
    print("TEST: Memory efficiency demonstration")
    print("=" * 60)

    yaml_str = """
large_list:
""" + "\n".join([f"  - item_{i}" for i in range(1000)])

    doc = libfyaml.loads(yaml_str)

    # Lazy access - only converts what we need
    print("Lazy access (first 5 items only):")
    large_list = doc["large_list"]
    for i in range(5):
        item = large_list[i]
        print(f"  {i}: {str(item)}")

    print(f"\nTotal items in list: {len(large_list)}")
    print("(Only converted 5 items to Python strings, rest stayed in native format)")
    print()


if __name__ == "__main__":
    print("\n" + "=" * 60)
    print("libfyaml Python Bindings - Test Suite")
    print("NumPy-like interface with lazy conversion")
    print("=" * 60 + "\n")

    try:
        test_load_file()
        test_lazy_access()
        test_sequence_access()
        test_mapping_operations()
        test_full_conversion()
        test_loads_string()
        test_type_conversions()
        test_performance_comparison()

        print("=" * 60)
        print("ALL TESTS PASSED!")
        print("=" * 60)

    except Exception as e:
        print(f"\nERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
