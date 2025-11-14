#!/usr/bin/env python3
"""Test JSON serialization support for FyGeneric objects."""

import json
import sys
import os
import tempfile

# Add parent directory to path for local import
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import libfyaml


def test_json_dumps_simple():
    """Test json_dumps with simple FyGeneric mapping."""
    data = libfyaml.loads('name: Alice\nage: 30')
    result = libfyaml.json_dumps(data)
    expected = '{"name": "Alice", "age": 30}'
    assert json.loads(result) == json.loads(expected)
    print("✓ test_json_dumps_simple")


def test_json_dumps_with_indent():
    """Test json_dumps with indent parameter."""
    data = libfyaml.loads('name: Alice\nage: 30')
    result = libfyaml.json_dumps(data, indent=2)
    # Should be valid JSON and properly formatted
    parsed = json.loads(result)
    assert parsed == {"name": "Alice", "age": 30}
    assert '\n' in result  # Should have newlines from indent
    print("✓ test_json_dumps_with_indent")


def test_json_dumps_nested():
    """Test json_dumps with nested structures."""
    data = libfyaml.loads('''
person:
  name: Alice
  age: 30
  items:
    - apple
    - banana
  metadata:
    created: 2024-01-01
    updated: 2024-01-02
''')
    result = libfyaml.json_dumps(data)
    parsed = json.loads(result)
    assert parsed['person']['name'] == 'Alice'
    assert parsed['person']['age'] == 30
    assert parsed['person']['items'] == ['apple', 'banana']
    assert parsed['person']['metadata']['created'] == '2024-01-01'
    print("✓ test_json_dumps_nested")


def test_json_dumps_all_types():
    """Test json_dumps with all FyGeneric types."""
    data = libfyaml.loads('''
null_value: null
bool_true: true
bool_false: false
integer: 42
float_value: 3.14
string: "hello"
sequence:
  - 1
  - 2
  - 3
mapping:
  key1: value1
  key2: value2
''')
    result = libfyaml.json_dumps(data)
    parsed = json.loads(result)
    assert parsed['null_value'] is None
    assert parsed['bool_true'] is True
    assert parsed['bool_false'] is False
    assert parsed['integer'] == 42
    assert parsed['float_value'] == 3.14
    assert parsed['string'] == 'hello'
    assert parsed['sequence'] == [1, 2, 3]
    assert parsed['mapping'] == {'key1': 'value1', 'key2': 'value2'}
    print("✓ test_json_dumps_all_types")


def test_json_dump_to_file():
    """Test json_dump writing to file."""
    data = libfyaml.loads('name: Alice\nage: 30\nitems: [apple, banana]')

    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.json') as f:
        temp_path = f.name
        libfyaml.json_dump(data, f, indent=2)

    try:
        # Read back and verify
        with open(temp_path, 'r') as f:
            content = f.read()
            parsed = json.loads(content)
            assert parsed == {'name': 'Alice', 'age': 30, 'items': ['apple', 'banana']}
        print("✓ test_json_dump_to_file")
    finally:
        os.unlink(temp_path)


def test_json_dumps_empty_structures():
    """Test json_dumps with empty sequences and mappings."""
    data = libfyaml.loads('''
empty_list: []
empty_dict: {}
nested:
  also_empty: []
''')
    result = libfyaml.json_dumps(data)
    parsed = json.loads(result)
    assert parsed['empty_list'] == []
    assert parsed['empty_dict'] == {}
    assert parsed['nested']['also_empty'] == []
    print("✓ test_json_dumps_empty_structures")


def test_json_dumps_unicode():
    """Test json_dumps with Unicode strings."""
    data = libfyaml.loads('''
english: Hello
japanese: こんにちは
emoji: 👋🌍
chinese: 你好
''')
    result = libfyaml.json_dumps(data, ensure_ascii=False)
    parsed = json.loads(result)
    assert parsed['english'] == 'Hello'
    assert parsed['japanese'] == 'こんにちは'
    assert parsed['emoji'] == '👋🌍'
    assert parsed['chinese'] == '你好'
    print("✓ test_json_dumps_unicode")


def test_json_dumps_numbers():
    """Test json_dumps with various number types."""
    data = libfyaml.loads('''
small_int: 1
large_int: 9223372036854775807
negative: -42
zero: 0
small_float: 0.1
negative_float: -3.14
scientific: 1.23e-10
''')
    result = libfyaml.json_dumps(data)
    parsed = json.loads(result)
    assert parsed['small_int'] == 1
    assert parsed['large_int'] == 9223372036854775807
    assert parsed['negative'] == -42
    assert parsed['zero'] == 0
    assert abs(parsed['small_float'] - 0.1) < 0.0001
    assert parsed['negative_float'] == -3.14
    print("✓ test_json_dumps_numbers")


def test_json_roundtrip():
    """Test YAML -> FyGeneric -> JSON -> Python -> JSON -> Python."""
    yaml_str = '''
name: Alice
age: 30
items:
  - apple
  - banana
metadata:
  created: 2024-01-01
'''

    # YAML -> FyGeneric
    fy_data = libfyaml.loads(yaml_str)

    # FyGeneric -> JSON
    json_str = libfyaml.json_dumps(fy_data, indent=2, sort_keys=True)

    # JSON -> Python
    py_data = json.loads(json_str)

    # Python -> JSON (should be identical)
    json_str2 = json.dumps(py_data, indent=2, sort_keys=True)

    assert json_str == json_str2
    print("✓ test_json_roundtrip")


def test_json_dumps_with_sort_keys():
    """Test json_dumps with sort_keys parameter."""
    data = libfyaml.loads('zebra: 1\napple: 2\nbanana: 3')
    result = libfyaml.json_dumps(data, sort_keys=True)
    # Keys should be alphabetically sorted
    assert result == '{"apple": 2, "banana": 3, "zebra": 1}'
    print("✓ test_json_dumps_with_sort_keys")


def test_json_dumps_with_separators():
    """Test json_dumps with custom separators."""
    data = libfyaml.loads('name: Alice\nage: 30')
    result = libfyaml.json_dumps(data, separators=(',', ':'))
    # Should be compact without spaces
    parsed = json.loads(result)
    assert parsed == {'name': 'Alice', 'age': 30}
    assert ' ' not in result  # No spaces in compact form
    print("✓ test_json_dumps_with_separators")


def test_json_dumps_deeply_nested():
    """Test json_dumps with deeply nested structures."""
    data = libfyaml.loads('''
level1:
  level2:
    level3:
      level4:
        level5:
          value: deep
''')
    result = libfyaml.json_dumps(data)
    parsed = json.loads(result)
    assert parsed['level1']['level2']['level3']['level4']['level5']['value'] == 'deep'
    print("✓ test_json_dumps_deeply_nested")


def test_json_dumps_mixed_sequence():
    """Test json_dumps with sequences containing mixed types."""
    data = libfyaml.loads('''
mixed:
  - 42
  - "hello"
  - true
  - null
  - 3.14
  - [nested, list]
  - {nested: object}
''')
    result = libfyaml.json_dumps(data)
    parsed = json.loads(result)
    expected = [42, "hello", True, None, 3.14, ["nested", "list"], {"nested": "object"}]
    assert parsed['mixed'] == expected
    print("✓ test_json_dumps_mixed_sequence")


def run_all_tests():
    """Run all tests."""
    print("Running JSON serialization tests...\n")

    test_json_dumps_simple()
    test_json_dumps_with_indent()
    test_json_dumps_nested()
    test_json_dumps_all_types()
    test_json_dump_to_file()
    test_json_dumps_empty_structures()
    test_json_dumps_unicode()
    test_json_dumps_numbers()
    test_json_roundtrip()
    test_json_dumps_with_sort_keys()
    test_json_dumps_with_separators()
    test_json_dumps_deeply_nested()
    test_json_dumps_mixed_sequence()

    print("\n✅ All JSON serialization tests passed!")


if __name__ == '__main__':
    run_all_tests()
