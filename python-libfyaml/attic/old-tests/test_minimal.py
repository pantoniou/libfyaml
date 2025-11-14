#!/usr/bin/env python3
"""
Minimal test for libfyaml Python bindings prototype.
Tests basic functionality with the actual generic API.
"""

import sys
import os

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(__file__))

import libfyaml._libfyaml as libfyaml

print("=" * 60)
print("Minimal libfyaml Python Bindings Test")
print("=" * 60)
print()

# Test 1: Parse simple YAML
print("Test 1: Parse simple YAML")
yaml_str = """
name: Alice
age: 30
active: true
"""

doc = libfyaml.loads(yaml_str)
print(f"  Parsed: {repr(doc)}")
print(f"  Type: {type(doc)}")
print()

# Test 2: Access values (lazy)
print("Test 2: Lazy value access")
name = doc["name"]
print(f"  doc['name']: {repr(name)}")
print(f"  str(name): {str(name)}")

age = doc["age"]
print(f"  doc['age']: {repr(age)}")
print(f"  int(age): {int(age)}")

active = doc["active"]
print(f"  doc['active']: {repr(active)}")
print(f"  bool(active): {bool(active)}")
print()

# Test 3: Type checking
print("Test 3: Type checking")
print(f"  name.is_string(): {name.is_string()}")
print(f"  age.is_int(): {age.is_int()}")
print(f"  active.is_bool(): {active.is_bool()}")
print(f"  doc.is_mapping(): {doc.is_mapping()}")
print()

# Test 4: Sequence access
print("Test 4: Parse and access sequence")
seq_yaml = """
- apple
- banana
- cherry
"""

seq = libfyaml.loads(seq_yaml)
print(f"  Parsed: {repr(seq)}")
print(f"  len(seq): {len(seq)}")
print(f"  seq[0]: {str(seq[0])}")
print(f"  seq[1]: {str(seq[1])}")
print(f"  seq[2]: {str(seq[2])}")
print()

# Test 5: Nested structures
print("Test 5: Nested structures")
nested_yaml = """
server:
  host: localhost
  port: 8080
  enabled: true
"""

nested = libfyaml.loads(nested_yaml)
print(f"  Parsed: {repr(nested)}")

server = nested["server"]
print(f"  server: {repr(server)}")

host = server["host"]
print(f"  server['host']: {str(host)}")

port = server["port"]
print(f"  server['port']: {int(port)}")
print()

# Test 6: Full conversion
print("Test 6: Full conversion to Python")
python_dict = doc.to_python()
print(f"  Type: {type(python_dict)}")
print(f"  Value: {python_dict}")
print()

# Test 7: JSON parsing
print("Test 7: Parse JSON")
json_str = '{"name": "Bob", "values": [1, 2, 3]}'
json_doc = libfyaml.loads(json_str, mode="json")
print(f"  Parsed: {repr(json_doc)}")
print(f"  name: {str(json_doc['name'])}")
print(f"  values: {repr(json_doc['values'])}")
print(f"  values[0]: {int(json_doc['values'][0])}")
print()

# Test 8: Boolean operations
print("Test 8: Boolean operations")
print(f"  bool(doc): {bool(doc)}")
print(f"  len(doc): {len(doc)}")
print()

print("=" * 60)
print("All tests passed!")
print("=" * 60)
