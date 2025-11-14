#!/usr/bin/env python3
"""Test script for iterator support"""

import sys
sys.path.insert(0, 'libfyaml')
import _libfyaml as libfyaml

print("=" * 60)
print("Testing iterator support for sequences and mappings")
print("=" * 60)

# Test 1: Iterate over sequence
print("\n### Test 1: Iterating over sequence")
doc = libfyaml.loads("[1, 2, 3, 4, 5]")
print(f"Sequence: {doc.to_python()}")
print("Items via iteration:")
for item in doc:
    print(f"  - {int(item)}")

# Test 2: Iterate over mapping (should yield keys)
print("\n### Test 2: Iterating over mapping (yields keys)")
doc = libfyaml.loads("name: Alice\nage: 30\nactive: true")
print(f"Mapping: {doc.to_python()}")
print("Keys via iteration:")
for key in doc:
    print(f"  - {str(key)}")

# Test 3: Iterate and access values
print("\n### Test 3: Iterating keys and accessing values")
doc = libfyaml.loads("a: 1\nb: 2\nc: 3")
print(f"Mapping: {doc.to_python()}")
print("Key-value pairs:")
for key in doc:
    key_str = str(key)
    value = int(doc[key_str])
    print(f"  {key_str}: {value}")

# Test 4: Nested iteration
print("\n### Test 4: Nested iteration")
doc = libfyaml.loads("""
users:
  - name: Alice
    age: 30
  - name: Bob
    age: 25
""")
print(f"Document: {doc.to_python()}")
print("Nested iteration:")
users = doc["users"]
for i, user in enumerate(users):
    print(f"  User {i}:")
    for key in user:
        print(f"    {str(key)}: {str(user[str(key)])}")

# Test 5: List comprehension
print("\n### Test 5: List comprehension")
doc = libfyaml.loads("[10, 20, 30, 40, 50]")
values = [int(x) for x in doc]
print(f"Original: {doc.to_python()}")
print(f"Via list comprehension: {values}")
print(f"Doubled: {[v * 2 for v in values]}")

# Test 6: Single item iteration
print("\n### Test 6: Single item iteration")
doc = libfyaml.loads("---\n- 42\n")
items = list(doc)
print(f"Single item sequence: {[int(x) for x in items]}")

doc = libfyaml.loads("---\nkey: value\n")
keys = list(doc)
print(f"Single item mapping keys: {[str(k) for k in keys]}")

# Test 7: for-else construct
print("\n### Test 7: For-else construct")
doc = libfyaml.loads("[1, 2, 3]")
found = False
for item in doc:
    if int(item) == 2:
        found = True
        print(f"  Found 2 in sequence!")
        break
else:
    print("  2 not found")

print("\n" + "=" * 60)
print("All iterator tests completed!")
print("=" * 60)
