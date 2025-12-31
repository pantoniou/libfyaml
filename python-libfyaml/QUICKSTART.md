# libfyaml Python Quickstart Guide

## 5-Second Start

```python
import libfyaml

# Parse YAML
data = libfyaml.loads(yaml_string)

# Use like a normal Python dict
print(data['name'])
for item in data['items']:
    print(item)
```

**That's it!** If you know Python dicts, you already know libfyaml.

## Installation

```bash
# Install from source (for now)
cd python-libfyaml
python3 setup.py install

# Or use in development
python3 setup.py build_ext --inplace
```

## Basic Usage

### Parse YAML String

```python
import libfyaml

yaml_string = """
name: Alice
age: 30
hobbies:
  - reading
  - coding
"""

data = libfyaml.loads(yaml_string)

print(data['name'])      # 'Alice'
print(data['age'])       # 30
print(data['hobbies'])   # ['reading', 'coding']
```

### Parse YAML File

```python
import libfyaml

# Read file manually
with open('config.yaml', 'r') as f:
    content = f.read()

data = libfyaml.loads(content)
```

### Parse JSON

```python
import libfyaml

json_string = '{"name": "Bob", "age": 25}'

data = libfyaml.loads(json_string, mode='json')
```

## Working with Data

### Dictionary Operations

```python
# Access values
name = data['name']
age = data.get('age', 0)  # With default

# Check keys
if 'email' in data:
    print(data['email'])

# Iterate
for key, value in data.items():
    print(f"{key}: {value}")

# Get all keys/values
keys = list(data.keys())
values = list(data.values())
```

### Nested Access

```python
yaml = """
user:
  profile:
    name: Alice
    contact:
      email: alice@example.com
"""

data = libfyaml.loads(yaml)

# Natural nested access
email = data['user']['profile']['contact']['email']
```

### Lists and Arrays

```python
yaml = """
users:
  - name: Alice
    age: 30
  - name: Bob
    age: 25
"""

data = libfyaml.loads(yaml)

# Iterate
for user in data['users']:
    print(f"{user['name']}: {user['age']}")

# Index access
first_user = data['users'][0]

# List comprehension
names = [user['name'] for user in data['users']]
```

## Converting to Python Objects

### Option 1: Lazy (Recommended)

```python
# FyGeneric objects convert on-the-fly
data = libfyaml.loads(yaml)

# Values auto-convert when accessed
name = data['name']  # Converts to Python str automatically
age = data['age']    # Converts to Python int automatically
```

**Best for**: Most use cases - fast, memory efficient

### Option 2: Full Conversion

```python
# Convert everything to native Python dict/list
data = libfyaml.loads(yaml)
python_dict = data.to_python()

# Or use .copy()
python_dict = data.copy()

# Or use dict()
python_dict = dict(data)
```

**Best for**: When you need a pure Python dict for serialization, etc.

## Performance Features

### Auto-Optimization (Enabled by Default)

```python
# Default behavior - already optimized!
data = libfyaml.loads(large_yaml)

# Automatically:
# ✅ Deduplicates repeated strings (saves ~40% on some files)
# ✅ Trims memory arena (saves ~35-45%)
# ✅ Uses optimal allocator
```

**You don't need to do anything - it's already optimal!**

### Manual Control (Advanced)

```python
# Disable dedup for unique content (8% faster)
data = libfyaml.loads(yaml, dedup=False)

# Disable auto-trim (keep arena for future allocations)
data = libfyaml.loads(yaml, trim=False)
data.trim()  # Trim manually later

# Both options
data = libfyaml.loads(yaml, dedup=False, trim=False)
```

### Memory Optimization

```python
# Free memory after parsing
data = libfyaml.loads(large_yaml)
data.trim()  # Release unused arena space

# Or rely on auto-trim (enabled by default)
data = libfyaml.loads(large_yaml)  # Already trimmed!
```

## Common Patterns

### Configuration Files

```python
import libfyaml

# Load config
with open('config.yaml', 'r') as f:
    config = libfyaml.loads(f.read())

# Use directly
db_host = config['database']['host']
db_port = config['database']['port']
max_connections = config['database']['max_connections']

# Or convert to Python dict
config_dict = config.to_python()
```

### Processing Large Files

```python
import libfyaml

# Parse large file (uses auto-optimizations)
with open('large_data.yaml', 'r') as f:
    data = libfyaml.loads(f.read())

# Process efficiently (lazy conversion)
for record in data['records']:
    process(record)  # Each record converts on-the-fly

# Or convert all at once if needed
all_records = [dict(r) for r in data['records']]
```

### Filtering and Transforming

```python
# Find items matching criteria
expensive_items = [
    item for item in data['items']
    if item['price'] > 100
]

# Transform data
names = [user['name'] for user in data['users']]

# Aggregate
total_price = sum(item['price'] for item in data['items'])
average_price = total_price / len(data['items'])
```

### Working with Optional Fields

```python
# Safe access with .get()
email = user.get('email', 'no-email@example.com')

# Check before access
if 'optional_field' in data:
    value = data['optional_field']

# Multiple levels
contact = data.get('user', {}).get('contact', {})
email = contact.get('email', 'N/A')
```

## Migrating from PyYAML

### Simple Replacement

```python
# Before (PyYAML)
import yaml
data = yaml.safe_load(content)

# After (libfyaml)
import libfyaml
data = libfyaml.loads(content)
```

**That's it!** The API is compatible.

### Performance Comparison

| Library | Parse Time | Memory | Code Changes |
|---------|-----------|--------|--------------|
| PyYAML | 2,610 ms | 110 MB | N/A |
| **libfyaml** | **119 ms** | **14 MB** | **1 line** |

**libfyaml is 22x faster and uses 7.7x less memory!**

### Dumping/Writing (Future)

```python
# Currently use PyYAML for writing
import yaml
with open('output.yaml', 'w') as f:
    yaml.dump(python_dict, f)

# libfyaml writing support coming soon
```

## Real-World Examples

### Example 1: REST API Configuration

```python
import libfyaml

config_yaml = """
api:
  host: localhost
  port: 8080
  endpoints:
    - path: /users
      methods: [GET, POST]
    - path: /products
      methods: [GET, POST, PUT, DELETE]
  database:
    host: db.example.com
    port: 5432
    name: myapp
"""

config = libfyaml.loads(config_yaml)

# Use configuration
server_host = config['api']['host']
server_port = config['api']['port']

# Process endpoints
for endpoint in config['api']['endpoints']:
    print(f"Register {endpoint['path']}: {endpoint['methods']}")

# Database connection
db_config = config['api']['database']
connect_db(db_config['host'], db_config['port'], db_config['name'])
```

### Example 2: Data Processing Pipeline

```python
import libfyaml

# Load large dataset
with open('data.yaml', 'r') as f:
    data = libfyaml.loads(f.read())

# Filter and transform (lazy - memory efficient)
results = [
    {
        'id': record['id'],
        'name': record['name'],
        'score': record['metrics']['score']
    }
    for record in data['records']
    if record['metrics']['score'] > 0.8
]

# Aggregate
avg_score = sum(r['score'] for r in results) / len(results)
print(f"Average score: {avg_score:.2f}")
```

### Example 3: Multi-Environment Config

```python
import libfyaml
import os

environment = os.getenv('ENV', 'development')

config_yaml = """
development:
  debug: true
  database:
    host: localhost
    port: 5432

production:
  debug: false
  database:
    host: prod-db.example.com
    port: 5432

common:
  app_name: MyApp
  version: 1.0.0
"""

config = libfyaml.loads(config_yaml)

# Get environment-specific config
env_config = config[environment]
common_config = config['common']

# Merge configs
app_config = {**common_config, **env_config}

print(f"Running {app_config['app_name']} v{app_config['version']}")
print(f"Debug mode: {app_config['debug']}")
print(f"Database: {app_config['database']['host']}")
```

## Performance Tips

### ✅ Do This (Fast)

```python
# Use lazy conversion (default)
data = libfyaml.loads(yaml)
for item in data['items']:
    process(item)  # Converts on-the-fly

# Let auto-optimizations work
data = libfyaml.loads(yaml)  # dedup=True, trim=True by default
```

### ⚠️ Avoid This (Slower)

```python
# Don't convert everything if you don't need to
data = libfyaml.loads(yaml)
python_dict = data.to_python()  # Unnecessary if just reading
for item in python_dict['items']:
    process(item)
```

### Memory-Constrained Environments

```python
# For unique content, disable dedup (8% faster, slightly less memory)
data = libfyaml.loads(yaml, dedup=False)

# Process and discard quickly
with open('large.yaml', 'r') as f:
    data = libfyaml.loads(f.read())
    process_data(data)
    del data  # Free memory immediately
```

## Debugging

### View Data

```python
# FyGeneric prints like a dict
data = libfyaml.loads(yaml)
print(data)  # Shows readable dict representation

# Or convert and pretty-print
import json
print(json.dumps(data.to_python(), indent=2))
```

### Type Checking

```python
# Check what you have
print(type(data))              # <class 'libfyaml.FyGeneric'>
print(type(data['nested']))    # <class 'libfyaml.FyGeneric'>

# After conversion
python_dict = data.to_python()
print(type(python_dict))       # <class 'dict'>
```

### Memory Usage

```python
# Check memory impact
import tracemalloc

tracemalloc.start()
data = libfyaml.loads(large_yaml)
current, peak = tracemalloc.get_traced_memory()
tracemalloc.stop()

print(f"Memory used: {peak / 1024 / 1024:.2f} MB")
```

## Common Issues

### Issue: "KeyError: 'missing_key'"

```python
# Solution: Use .get() with default
value = data.get('missing_key', 'default_value')

# Or check first
if 'key' in data:
    value = data['key']
```

### Issue: Need to modify data

```python
# Option 1: Parse as mutable (if supported)
data = libfyaml.loads(yaml, mutable=True)
data['new_key'] = 'value'

# Option 2: Convert to Python dict
data = libfyaml.loads(yaml)
mutable_dict = data.to_python()
mutable_dict['new_key'] = 'value'
mutable_dict['existing_key'] = 'new_value'
```

### Issue: Writing YAML back

```python
# Use dumps() to emit YAML
import libfyaml

data = libfyaml.loads(yaml_string)

# Convert to mutable, modify, write
python_dict = data.to_python()
python_dict['updated'] = True

# Write back using libfyaml
yaml_str = libfyaml.dumps(python_dict)
with open('output.yaml', 'w') as f:
    f.write(yaml_str)

# Or use dump() directly
with open('output.yaml', 'w') as f:
    libfyaml.dump(python_dict, f)
```

## API Reference (Quick)

### libfyaml.loads()

```python
libfyaml.loads(s, mode='yaml', dedup=True, trim=True)
```

- `s` (str): YAML/JSON string
- `mode` (str): 'yaml' or 'json' (default: 'yaml')
- `dedup` (bool): Enable deduplication (default: True)
- `trim` (bool): Auto-trim arena (default: True)

**Returns**: FyGeneric object (dict-like)

### FyGeneric Methods

```python
data.to_python()        # Convert to native Python dict/list
data.copy()             # Same as to_python()
data.trim()             # Manually trim arena (usually not needed)

# Standard dict methods
data.keys()             # Get keys
data.values()           # Get values
data.items()            # Get key-value pairs
data.get(key, default)  # Get with default
```

## Performance Characteristics

| File Size | Parse Time | Memory Usage | vs PyYAML |
|-----------|-----------|--------------|-----------|
| 6 MB | ~120 ms | ~14 MB | 22x faster, 7.7x less memory |
| 768 MB | ~19 s | ~1.1 GB | 138x faster, 100x less memory |

**libfyaml excels at large files!**

## When to Use libfyaml

### ✅ Perfect For

- Large YAML files (>10 MB)
- Production systems
- Memory-constrained environments (containers, Lambda)
- High-performance parsing
- Reading configuration files
- Data processing pipelines
- Parallel file processing

### ⚠️ Not Yet Ideal For

- Writing/dumping YAML (use PyYAML)
- Modifying parsed data (convert with .to_python() first)
- Very small files where PyYAML is "fast enough"

## Getting Help

### Documentation

- [DEDUP-BENCHMARK-RESULTS.md](DEDUP-BENCHMARK-RESULTS.md) - Performance details
- [AUTO-TRIM-IMPLEMENTATION.md](AUTO-TRIM-IMPLEMENTATION.md) - Memory optimization
- [API-COMPARISON-PYTHON-VS-TRENCHCOAT.md](API-COMPARISON-PYTHON-VS-TRENCHCOAT.md) - API advantages

### Common Questions

**Q: Is it thread-safe?**
A: FyGeneric objects are immutable by default and safe to read from multiple threads.

**Q: Can I modify the parsed data?**
A: FyGeneric is immutable by default. Use `mutable=True` when parsing, or convert to Python dict: `data.to_python()`

**Q: Why use this over PyYAML?**
A: 22x faster, 7.7x less memory, same API

**Q: Does it support YAML 1.2?**
A: Yes! Full YAML 1.2 support.

## Quick Comparison

### PyYAML

```python
import yaml

data = yaml.safe_load(content)  # Slow, memory-hungry
# Use as dict
```

**Speed**: Slow (baseline)
**Memory**: High
**API**: Python dict ✅

### rapidyaml

```python
import ryml

tree = ryml.parse_in_arena(content)  # Fast, memory-hungry
# Manual tree traversal with byte strings
for node in tree.rootref().children():
    key = tree.key(node).decode('utf-8')
    value = tree.val(node).decode('utf-8')
```

**Speed**: Very fast ✅
**Memory**: Very high ❌
**API**: C++ tree API ❌

### libfyaml (This Library)

```python
import libfyaml

data = libfyaml.loads(content)  # Fast, memory-efficient
# Use as dict
```

**Speed**: Very fast ✅
**Memory**: Very low ✅
**API**: Python dict ✅

**Winner**: libfyaml (fast, efficient, Pythonic)

---

## Next Steps

1. **Try it**: `data = libfyaml.loads(your_yaml)`
2. **Use it**: Just treat `data` like a normal dict
3. **Optimize**: It's already optimized by default!
4. **Enjoy**: Fast parsing, low memory, zero learning curve

**Welcome to libfyaml!** 🚀
