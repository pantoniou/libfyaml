#!/usr/bin/env python3
"""Test automatic type conversion - what works without explicit casting"""

import sys
sys.path.insert(0, 'libfyaml')
import _libfyaml as libfyaml

print("=" * 70)
print("Automatic Type Conversion Guide")
print("=" * 70)

doc = libfyaml.loads("""
name: Alice
age: 30
score: 95.5
active: true
count: 42
""")

print("\n### What Works AUTOMATICALLY (no casting needed):\n")

# 1. String formatting
print("1. String formatting (f-strings, print):")
print(f"   Hello {doc['name']}!")  # Calls __str__ automatically
print(f"   Age: {doc['age']}")     # Calls __str__ automatically

# 2. Boolean context
print("\n2. Boolean context (if/while):")
if doc['active']:  # Calls __bool__ automatically
    print("   Status: Active")

# 3. Comparisons (NEW!)
print("\n3. Comparisons (==, !=, <, >, <=, >=):")
print(f"   age == 30: {doc['age'] == 30}")          # NO int() needed!
print(f"   age > 25: {doc['age'] > 25}")            # NO int() needed!
print(f"   name == 'Alice': {doc['name'] == 'Alice'}")  # NO str() needed!
print(f"   score >= 90.0: {doc['score'] >= 90.0}") # NO float() needed!

# 4. Filtering and conditionals
print("\n4. Practical filtering (no casting!):")
users = libfyaml.loads("""
- name: Alice
  age: 30
- name: Bob
  age: 25
- name: Charlie
  age: 35
""")

adults = [user for user in users if user['age'] >= 30]  # NO int() needed!
print(f"   Adults: {[str(u['name']) for u in adults]}")

# 5. Iteration + comparison
print("\n5. Iteration + comparison:")
for user in users:
    if user['age'] > 26:  # NO int() needed!
        print(f"   {str(user['name'])}: {user['age']} years old")

print("\n### What STILL REQUIRES explicit casting:\n")

# 1. Arithmetic operations
print("1. Arithmetic (still needs int/float):")
try:
    result = doc['age'] + 10
    print(f"   age + 10: {result}")
except TypeError:
    age_value = int(doc['age'])
    result = age_value + 10
    print(f"   int(age) + 10: {result}")

# 2. Math operations
print("\n2. Math operations (still needs int/float):")
try:
    result = doc['count'] * 2
    print(f"   count * 2: {result}")
except TypeError:
    count_value = int(doc['count'])
    result = count_value * 2
    print(f"   int(count) * 2: {result}")

# 3. Type-specific methods
print("\n3. Type-specific methods (still needs casting):")
# String methods
name_upper = str(doc['name']).upper()  # Need str() for .upper()
print(f"   str(name).upper(): {name_upper}")

# 4. Function arguments expecting specific types
print("\n4. Function arguments (context-dependent):")
import math
# Need explicit float() for math functions
score_sqrt = math.sqrt(float(doc['score']))
print(f"   math.sqrt(float(score)): {score_sqrt:.2f}")

print("\n### Summary:\n")
print("✅ AUTOMATIC: comparisons, boolean context, string formatting")
print("❌ NEEDS CASTING: arithmetic, math ops, type-specific methods")
print("\nThis matches NumPy's behavior - comparison works, arithmetic doesn't!")

print("\n### Best Practices:\n")
print("""
1. Use comparisons directly:
   ✅ if doc['age'] > 30:
   ❌ if int(doc['age']) > 30:

2. Cast for arithmetic:
   ✅ total = int(doc['count']) + 10
   ❌ total = doc['count'] + 10

3. Cast for type methods:
   ✅ name_upper = str(doc['name']).upper()
   ❌ name_upper = doc['name'].upper()

4. Use to_python() for full conversion:
   ✅ data = doc.to_python()
""")

print("\n" + "=" * 70)
print("Automatic conversion makes code cleaner for filtering & conditions!")
print("=" * 70)
