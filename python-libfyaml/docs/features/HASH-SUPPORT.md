# Hash Support for FyGeneric Objects

## Overview

FyGeneric objects now support hashing via the `__hash__()` method, enabling them to be used as dictionary keys and in sets, with hashes matching their native Python equivalents.

## Usage

### Basic Hashing

```python
data = libfyaml.loads("""
number: 42
text: hello
""")

# Hash FyGeneric values
hash(data['number'])  # Same as hash(42)
hash(data['text'])    # Same as hash("hello")
```

### As Dictionary Keys

```python
data = libfyaml.loads("""
user_id: 12345
username: alice
""")

# Use as dict keys
cache = {}
cache[data['user_id']] = "User data for 12345"
cache[data['username']] = "User data for alice"

# Look up with native Python types
print(cache[12345])    # "User data for 12345"
print(cache["alice"])  # "User data for alice"
```

### In Sets

```python
data = libfyaml.loads("""
items:
  - 1
  - 2
  - 3
  - 2
  - 1
""")

# Create set to deduplicate
unique_items = set(data['items'])
print(unique_items)  # {1, 2, 3}

# Check membership with native types
print(1 in unique_items)  # True
print(4 in unique_items)  # False
```

### Cross-Compatibility

FyGeneric hashes are designed to match native Python types:

```python
data = libfyaml.loads("value: 42")

# Add with FyGeneric, lookup with int
cache = {data['value']: "data"}
print(cache[42])  # "data"

# Add with int, lookup with FyGeneric
cache2 = {99: "native"}
data2 = libfyaml.loads("num: 99")
print(cache2[data2['num']])  # "native"
```

## Supported Types

### Hashable Types

The following FyGeneric types are hashable:

| Type | Hash Behavior | Example |
|------|--------------|---------|
| `int` | Same as `hash(int)` | `hash(fy_int) == hash(42)` |
| `float` | Same as `hash(float)` | `hash(fy_float) == hash(3.14)` |
| `str` | Same as `hash(str)` | `hash(fy_str) == hash("hello")` |
| `bool` | Same as `hash(bool)` | `hash(fy_bool) == hash(True)` |
| `None` | Same as `hash(None)` | `hash(fy_null) == hash(None)` |

### Unhashable Types

The following types raise `TypeError` when hashed (matching Python behavior):

| Type | Error |
|------|-------|
| `sequence` | `TypeError: unhashable type: 'sequence'` |
| `mapping` | `TypeError: unhashable type: 'mapping'` |

```python
data = libfyaml.loads("""
my_list: [1, 2, 3]
my_dict: {a: 1}
""")

# These raise TypeError
hash(data['my_list'])  # Error: unhashable type: 'sequence'
hash(data['my_dict'])  # Error: unhashable type: 'mapping'
```

## Implementation Details

### Hash Computation

The hash is computed by:
1. Converting the wrapped value to a temporary Python object
2. Computing the hash of that object
3. Returning the hash value

This ensures **hash consistency** with native Python types:

```python
assert hash(fy_int) == hash(int_value)
assert hash(fy_str) == hash(str_value)
```

### Hash Stability

Hashing the same value multiple times returns the same hash:

```python
data = libfyaml.loads("value: 42")
val = data['value']

h1 = hash(val)
h2 = hash(val)
h3 = hash(val)

assert h1 == h2 == h3  # All equal
```

### Performance

**Cost per hash operation:**
- Create temporary Python object: ~20-30 CPU cycles
- Compute Python hash: ~10-50 cycles (depends on type)
- **Total: ~30-80 cycles**

This is acceptable for typical use cases (dict lookups, set operations).

## Hash and Equality

Python's contract requires: **if `a == b`, then `hash(a) == hash(b)`**

FyGeneric objects satisfy this contract:

```python
data1 = libfyaml.loads("val: 42")
data2 = libfyaml.loads("val: 42")

# Equality (via richcompare)
assert data1['val'] == data2['val']
assert data1['val'] == 42

# Hash consistency
assert hash(data1['val']) == hash(data2['val'])
assert hash(data1['val']) == hash(42)
```

## Use Cases

### 1. Caching

```python
# Parse YAML config
config = libfyaml.loads("""
database:
  host: localhost
  port: 5432
""")

# Use config values as cache keys
connection_cache = {}
cache_key = (config['database']['host'],
             config['database']['port'])
connection_cache[cache_key] = create_connection(...)
```

### 2. Deduplication

```python
# Parse list with duplicates
data = libfyaml.loads("""
user_ids: [123, 456, 789, 123, 456]
""")

# Deduplicate using set
unique_ids = set(data['user_ids'])
# Works because integers are hashable
```

### 3. Dictionary Lookups

```python
# Parse lookup table
lookup = libfyaml.loads("""
error_codes:
  404: Not Found
  500: Internal Server Error
  200: OK
""")

# Use as lookup dictionary
# Keys are FyGeneric integers
for code, message in lookup['error_codes'].items():
    print(f"HTTP {code}: {message}")
```

### 4. Set Operations

```python
data = libfyaml.loads("""
set_a: [1, 2, 3, 4]
set_b: [3, 4, 5, 6]
""")

a = set(data['set_a'])
b = set(data['set_b'])

intersection = a & b  # {3, 4}
union = a | b         # {1, 2, 3, 4, 5, 6}
difference = a - b    # {1, 2}
```

## Limitations

### 1. Object Identity vs Equality

```python
data1 = libfyaml.loads("val: 42")
data2 = libfyaml.loads("val: 42")

# Same hash, equal values
assert hash(data1['val']) == hash(data2['val'])
assert data1['val'] == data2['val']

# But different object identities
assert data1['val'] is not data2['val']
```

This is expected - they're different wrapper objects.

### 2. Sets Only Check Hash+Equality

When adding to sets, Python uses both hash and equality:

```python
s = {data1['val'], data2['val'], 42}

# All three are equal (value 42), but might be separate objects
# Set size depends on implementation of __eq__
```

### 3. Mutable Container Values

Lists and dicts inside FyGeneric are still unhashable:

```python
data = libfyaml.loads("""
config:
  hosts: [server1, server2]
""")

# Can hash the config dict? No!
hash(data['config'])  # TypeError: unhashable type: 'mapping'

# But can hash individual host strings
for host in data['config']['hosts']:
    cache[host] = ...  # Works!
```

## Comparison with Native Types

| Operation | Native Python | FyGeneric | Result |
|-----------|--------------|-----------|--------|
| `hash(42)` | 42 | `hash(fy_int)` | Same hash |
| `{42: "v"}[42]` | Works | `{fy_int: "v"}[42]` | Works |
| `{1, 2, 3}` | Set of ints | `set(fy_ints)` | Set of FyGeneric |
| `hash([1,2])` | TypeError | `hash(fy_list)` | TypeError |

## Best Practices

### 1. Use for Immutable Keys

```python
# Good: Use primitive types as keys
cache = {data['user_id']: user_data}

# Bad: Don't use mutable types
cache = {data['user_list']: ...}  # TypeError!
```

### 2. Cross-Lookup Pattern

```python
# Build cache with FyGeneric keys
for item in yaml_data['items']:
    cache[item['id']] = item

# Look up with native Python values
result = cache[12345]  # Works!
```

### 3. Set Deduplication

```python
# Parse potentially duplicate data
data = libfyaml.loads_all(multiple_yaml_docs)

# Extract IDs and deduplicate
all_ids = []
for doc in data:
    all_ids.extend(doc['ids'])

unique_ids = set(all_ids)  # Deduplicates based on hash+equality
```

## Testing

The hash implementation includes comprehensive tests:

- **Hash equality**: FyGeneric hash == native Python hash
- **Dict keys**: Use as keys, lookup with native types
- **Sets**: Membership, deduplication
- **Cross-lookup**: Add with one type, lookup with another
- **Unhashable types**: Proper TypeError for lists/dicts
- **Hash stability**: Same value → same hash
- **Edge cases**: Large ints, long strings, special floats, Unicode

See `test_hash_support.py` for 26 comprehensive tests.

## See Also

- [isinstance() Support](ISINSTANCE-SUPPORT.md) - Type checking support
- [Python Data Model - Hash](https://docs.python.org/3/reference/datamodel.html#object.__hash__)
- [Python Built-in - hash()](https://docs.python.org/3/library/functions.html#hash)
