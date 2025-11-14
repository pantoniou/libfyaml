#!/usr/bin/env python3
"""Test script for dumps() implementation"""

import sys
sys.path.insert(0, 'libfyaml')
import _libfyaml as libfyaml

print("=" * 60)
print("Testing dumps() - Python → YAML serialization")
print("=" * 60)

# Test 1: Simple types
print("\n### Test 1: Simple types")
for value, name in [(None, "None"), (True, "True"), (False, "False"),
                     (42, "int"), (3.14, "float"), ("hello", "string")]:
    result = libfyaml.dumps(value)
    print(f"{name:10s} → {result.strip()}")

# Test 2: Lists
print("\n### Test 2: Lists")
data = [1, 2, 3, 4, 5]
result = libfyaml.dumps(data)
print(f"List: {data}")
print(f"YAML:\n{result}")

# Test 3: Dictionaries
print("\n### Test 3: Dictionaries")
data = {"name": "Alice", "age": 30, "active": True}
result = libfyaml.dumps(data)
print(f"Dict: {data}")
print(f"YAML:\n{result}")

# Test 4: Nested structures
print("\n### Test 4: Nested structures")
data = {
    "users": [
        {"name": "Alice", "age": 30, "active": True},
        {"name": "Bob", "age": 25, "active": False}
    ],
    "metadata": {
        "version": "1.0",
        "count": 2
    }
}
result = libfyaml.dumps(data)
print(f"YAML:\n{result}")

# Test 5: Compact mode
print("\n### Test 5: Compact mode (flow style)")
data = {"name": "Charlie", "score": 100}
result = libfyaml.dumps(data, compact=True)
print(f"Compact: {result.strip()}")

# Test 6: Round-trip
print("\n### Test 6: Round-trip (dumps → loads)")
original = {
    "string": "test",
    "number": 123,
    "float": 45.67,
    "bool": True,
    "list": [1, 2, 3],
    "nested": {"key": "value"}
}

yaml_str = libfyaml.dumps(original)
parsed = libfyaml.loads(yaml_str)
result = parsed.to_python()

print(f"Original: {original}")
print(f"Round-trip result: {result}")
print(f"Match: {result == original}")

# Test 7: Edge cases
print("\n### Test 7: Edge cases")
test_cases = [
    ([], "empty list"),
    ({}, "empty dict"),
    ([None, None], "list of nulls"),
    ({"a": None}, "null value"),
]

for data, description in test_cases:
    result = libfyaml.dumps(data)
    print(f"{description:20s} → {result.strip()}")

print("\n" + "=" * 60)
print("All tests completed!")
print("=" * 60)
