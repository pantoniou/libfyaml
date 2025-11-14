#!/usr/bin/env python3
"""Test script for from_python() - Python to FyGeneric conversion"""

import sys
sys.path.insert(0, 'libfyaml')
import _libfyaml as libfyaml

print("=" * 60)
print("Testing from_python() - Direct Python → FyGeneric conversion")
print("=" * 60)

# Test 1: Basic conversion
print("\n### Test 1: Basic Python to FyGeneric")
data = {"name": "Alice", "age": 30, "active": True}
generic = libfyaml.from_python(data)
print(f"Python:  {data}")
print(f"Generic: {generic}")
print(f"Type:    {type(generic)}")

# Test 2: Why use from_python() instead of dumps()?
print("\n### Test 2: from_python() vs dumps()")
data = [1, 2, 3, 4, 5]

# Using dumps - goes through YAML string
yaml_str = libfyaml.dumps(data)
parsed = libfyaml.loads(yaml_str)
print(f"Via dumps/loads: {parsed.to_python()}")

# Using from_python - direct conversion
generic = libfyaml.from_python(data)
print(f"Via from_python: {generic.to_python()}")
print(f"Same result: {parsed.to_python() == generic.to_python()}")

# Test 3: Manipulating data in generic format
print("\n### Test 3: Working with data in generic format")
users = libfyaml.from_python([
    {"name": "Alice", "age": 30},
    {"name": "Bob", "age": 25},
])

print("Users in generic format:")
for user in users:
    name = str(user["name"])
    age = int(user["age"])
    print(f"  - {name}: {age} years old")

# Test 4: Building documents programmatically
print("\n### Test 4: Building documents from Python data")
config = {
    "database": {
        "host": "localhost",
        "port": 5432,
        "users": ["admin", "readonly"]
    },
    "logging": {
        "level": "info",
        "output": "/var/log/app.log"
    }
}

# Convert to generic
generic_config = libfyaml.from_python(config)

# Access lazily
db_host = str(generic_config["database"]["host"])
db_port = int(generic_config["database"]["port"])
print(f"Database: {db_host}:{db_port}")

# Emit to YAML
yaml_output = libfyaml.dumps(generic_config.to_python())
print(f"\nAs YAML:\n{yaml_output}")

# Test 5: Type preservation
print("\n### Test 5: Type preservation")
data = {
    "null_value": None,
    "bool_true": True,
    "bool_false": False,
    "int": 42,
    "float": 3.14,
    "string": "hello",
    "list": [1, 2, 3],
    "nested": {"key": "value"}
}

generic = libfyaml.from_python(data)
result = generic.to_python()

print("Type preservation check:")
for key in ["null_value", "bool_true", "bool_false", "int", "float", "string", "list", "nested"]:
    original = data[key]
    converted = result[key]
    match = "✓" if original == converted else "✗"
    print(f"  {match} {key:12s}: {type(original).__name__:10s} → {type(converted).__name__:10s}")

# Test 6: Performance benefit - no string serialization
print("\n### Test 6: Use case - avoiding string serialization")
large_data = {"items": [{"id": i, "value": i * 10} for i in range(100)]}

# Method 1: dumps → loads (serializes to string)
yaml_str = libfyaml.dumps(large_data)
from_yaml = libfyaml.loads(yaml_str)

# Method 2: from_python (direct conversion)
from_python = libfyaml.from_python(large_data)

# Both give same result
print(f"dumps→loads result: {len(from_yaml['items'])} items")
print(f"from_python result: {len(from_python['items'])} items")
print(f"Same data: {from_yaml.to_python() == from_python.to_python()}")
print(f"\nBenefit: from_python() skips YAML serialization/parsing overhead")

print("\n" + "=" * 60)
print("All from_python() tests completed!")
print("=" * 60)
