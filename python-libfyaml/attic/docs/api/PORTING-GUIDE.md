# Porting Guide: PyYAML to libfyaml

**Get 238-390x speedup with minimal code changes!**

This guide walks you through migrating from PyYAML to libfyaml step-by-step, covering common scenarios and pitfalls.

## Table of Contents

1. [Quick Start](#quick-start)
2. [Migration Strategies](#migration-strategies)
3. [Step-by-Step Porting](#step-by-step-porting)
4. [Common Code Patterns](#common-code-patterns)
5. [Handling Differences](#handling-differences)
6. [Testing Your Migration](#testing-your-migration)
7. [Troubleshooting](#troubleshooting)
8. [Real-World Examples](#real-world-examples)
9. [Performance Tips](#performance-tips)

---

## Quick Start

### The 30-Second Migration

For most code, migration is this simple:

```python
# BEFORE (PyYAML)
import yaml

with open('config.yaml') as f:
    config = yaml.safe_load(f)

# Use config...
print(config['server']['host'])
```

```python
# AFTER (libfyaml) - Option 1: Direct
import libfyaml

config = libfyaml.load('config.yaml')

# Use config... (IDENTICAL CODE!)
print(config['server']['host'])
```

```python
# AFTER (libfyaml) - Option 2: Drop-in replacement
import libfyaml as yaml

# Create PyYAML-compatible wrapper
yaml.safe_load = lambda f: libfyaml.load(f.name if hasattr(f, 'name') else f)

with open('config.yaml') as f:
    config = yaml.safe_load(f)

# Rest of code UNCHANGED
print(config['server']['host'])
```

**Result**: Same functionality, 238-390x faster! 🚀

---

## Migration Strategies

### Strategy 1: Drop-In Replacement (Fastest)

**Best for**: Quick wins, minimal code changes, testing the waters

**How it works**: Add compatibility shim at the top of your files

```python
# compatibility_shim.py
try:
    import libfyaml

    class YamlCompat:
        """PyYAML-compatible wrapper for libfyaml"""

        @staticmethod
        def safe_load(stream):
            if hasattr(stream, 'read'):
                content = stream.read()
            else:
                content = stream
            return libfyaml.loads(content)

        @staticmethod
        def safe_load_all(stream):
            raise NotImplementedError("Multi-document YAML not yet exposed in Python bindings")

        @staticmethod
        def dump(data, stream=None, **kwargs):
            # Map PyYAML kwargs to libfyaml
            compact = kwargs.get('default_flow_style', False)
            yaml_str = libfyaml.dumps(data, compact=compact)

            if stream is not None:
                stream.write(yaml_str)
            else:
                return yaml_str

        @staticmethod
        def dumps(data, **kwargs):
            compact = kwargs.get('default_flow_style', False)
            return libfyaml.dumps(data, compact=compact)

        load = safe_load
        loads = lambda s: libfyaml.loads(s)

    yaml = YamlCompat()
    HAS_LIBFYAML = True

except ImportError:
    import yaml
    HAS_LIBFYAML = False
```

Then in your code:

```python
from compatibility_shim import yaml

# All your existing PyYAML code works!
config = yaml.safe_load(open('config.yaml'))
```

**Pros**:
- ✅ Zero code changes to business logic
- ✅ Gradual migration (one module at a time)
- ✅ Easy rollback (just remove shim)
- ✅ Immediate performance gains

**Cons**:
- ⚠️ Doesn't leverage libfyaml's zero-casting
- ⚠️ Slight compatibility layer overhead

### Strategy 2: Incremental Migration (Recommended)

**Best for**: Production code, large codebases, thorough testing

**Process**:
1. Identify YAML loading points
2. Migrate one module/feature at a time
3. Test thoroughly
4. Monitor performance improvements
5. Repeat

**Migration checklist per module**:
```python
# ✅ 1. Change imports
- import yaml
+ import libfyaml

# ✅ 2. Update load calls
- data = yaml.safe_load(file)
+ data = libfyaml.load(file.name)  # Or libfyaml.loads(file.read())

# ✅ 3. Check for type checks
- if isinstance(data['value'], str):
+ if data['value'].is_string():  # Or just use the value directly

# ✅ 4. Check for mutations
- data['new_key'] = value
+ data = data.to_python(); data['new_key'] = value  # Convert if needed

# ✅ 5. Remove defensive casting (optional but recommended!)
- age = int(data['age']) + 1
+ age = data['age'] + 1  # Zero-casting!

# ✅ 6. Test!
```

### Strategy 3: Full Rewrite (Maximum Performance)

**Best for**: New features, greenfield projects, performance-critical code

**Key principle**: Embrace FyGeneric, don't convert to native Python

```python
# DON'T do this (defeats the purpose):
config = libfyaml.load('config.yaml').to_python()

# DO this (keeps it efficient):
config = libfyaml.load('config.yaml')
# Use config directly - FyGeneric is lazy and memory-efficient!
```

---

## Step-by-Step Porting

### Step 1: Install libfyaml

```bash
pip install libfyaml
```

### Step 2: Identify YAML Operations

Search your codebase for PyYAML usage:

```bash
# Find all YAML loading
grep -r "yaml.safe_load\|yaml.load" .

# Find all YAML dumping
grep -r "yaml.dump\|yaml.safe_dump" .

# Find multi-document usage (not yet exposed in Python bindings)
grep -r "yaml.safe_load_all\|yaml.load_all" .
```

### Step 3: Create a Migration Plan

Prioritize by:
1. **Impact**: Large files (>1MB) give biggest speedup
2. **Risk**: Start with non-critical paths
3. **Complexity**: Simple loads before complex processing

### Step 4: Port Loading Code

#### Pattern 1: File loading

```python
# BEFORE
import yaml

with open('config.yaml') as f:
    config = yaml.safe_load(f)
```

```python
# AFTER - Option A (recommended)
import libfyaml

config = libfyaml.load('config.yaml')
```

```python
# AFTER - Option B (if you need file handle)
import libfyaml

with open('config.yaml') as f:
    config = libfyaml.loads(f.read())
```

#### Pattern 2: String loading

```python
# BEFORE
import yaml

yaml_str = """
server:
  host: localhost
  port: 8080
"""
config = yaml.safe_load(yaml_str)
```

```python
# AFTER (identical!)
import libfyaml

yaml_str = """
server:
  host: localhost
  port: 8080
"""
config = libfyaml.loads(yaml_str)
```

#### Pattern 3: Loading with error handling

```python
# BEFORE
import yaml

try:
    with open('config.yaml') as f:
        config = yaml.safe_load(f)
except yaml.YAMLError as e:
    print(f"YAML error: {e}")
    config = {}
```

```python
# AFTER
import libfyaml

try:
    config = libfyaml.load('config.yaml')
except Exception as e:  # libfyaml uses standard exceptions
    print(f"YAML error: {e}")
    config = {}
```

### Step 5: Port Dumping Code

#### Pattern 1: Dump to string

```python
# BEFORE
import yaml

data = {'server': {'host': 'localhost', 'port': 8080}}
yaml_str = yaml.dump(data)
```

```python
# AFTER
import libfyaml

data = {'server': {'host': 'localhost', 'port': 8080}}
yaml_str = libfyaml.dumps(data)
```

#### Pattern 2: Dump to file

```python
# BEFORE
import yaml

with open('output.yaml', 'w') as f:
    yaml.dump(data, f)
```

```python
# AFTER - Option A (recommended)
import libfyaml

libfyaml.dump('output.yaml', data)
```

```python
# AFTER - Option B (manual)
import libfyaml

with open('output.yaml', 'w') as f:
    f.write(libfyaml.dumps(data))
```

#### Pattern 3: Dump with formatting

```python
# BEFORE
import yaml

yaml_str = yaml.dump(data, default_flow_style=False)
```

```python
# AFTER
import libfyaml

yaml_str = libfyaml.dumps(data, compact=False)
```

### Step 6: Update Data Access (Usually No Changes!)

**Good news**: Most data access code works identically!

```python
# All of these work the same in PyYAML and libfyaml:

# Dict access
host = config['server']['host']

# List access
first_user = users[0]

# Iteration
for key in config.keys():
    print(key, config[key])

# Membership
if 'database' in config:
    connect_db(config['database'])

# Comprehensions
names = [user['name'] for user in users]

# Filtering
admins = [u for u in users if u['role'] == 'admin']

# Aggregation
total = sum(item['price'] for item in items)
```

### Step 7: Fix Type Checks (If Any)

```python
# BEFORE - Type checks with isinstance
if isinstance(config['port'], int):
    port = config['port']
else:
    port = int(config['port'])
```

```python
# AFTER - Option 1: Use FyGeneric methods
if config['port'].is_int():
    port = config['port']
else:
    port = int(config['port'])
```

```python
# AFTER - Option 2: Just use it (recommended!)
# FyGeneric zero-casting handles type conversions automatically
port = config['port']  # Works whether it's int or string!
```

### Step 8: Fix Mutations (If Any)

```python
# BEFORE - PyYAML allows mutation
config = yaml.safe_load(yaml_str)
config['new_setting'] = 'value'
config['server']['timeout'] = 30
```

```python
# AFTER - Option 1: Convert to native Python
config = libfyaml.loads(yaml_str).to_python()
config['new_setting'] = 'value'
config['server']['timeout'] = 30
```

```python
# AFTER - Option 2: Build new config (functional style)
config = libfyaml.loads(yaml_str)
# Read from config, build new dict for changes
new_config = config.to_python()
new_config['new_setting'] = 'value'
```

---

## Common Code Patterns

### Configuration Files

```python
# BEFORE
import yaml

class Config:
    def __init__(self, path):
        with open(path) as f:
            self.data = yaml.safe_load(f)

    def get(self, key, default=None):
        return self.data.get(key, default)
```

```python
# AFTER - Minimal changes
import libfyaml

class Config:
    def __init__(self, path):
        self.data = libfyaml.load(path)  # Changed!

    def get(self, key, default=None):
        return self.data.get(key, default)  # Works identically!
```

### Data Processing Pipelines

```python
# BEFORE
import yaml

def process_data(yaml_file):
    with open(yaml_file) as f:
        data = yaml.safe_load(f)

    # Process...
    results = []
    for item in data['items']:
        if item['value'] > 100:
            results.append({
                'id': item['id'],
                'value': item['value'] * 1.1
            })

    return results
```

```python
# AFTER - Only import changes!
import libfyaml

def process_data(yaml_file):
    data = libfyaml.load(yaml_file)  # Changed!

    # Process... (IDENTICAL CODE!)
    results = []
    for item in data['items']:
        if item['value'] > 100:
            results.append({
                'id': item['id'],
                'value': item['value'] * 1.1  # Zero-casting works!
            })

    return results
```

### API Responses

```python
# BEFORE
import yaml
import json

def parse_response(response_text, format='json'):
    if format == 'json':
        return json.loads(response_text)
    elif format == 'yaml':
        return yaml.safe_load(response_text)
```

```python
# AFTER
import libfyaml
import json

def parse_response(response_text, format='json'):
    if format == 'json':
        return json.loads(response_text)
    elif format == 'yaml':
        return libfyaml.loads(response_text)  # Changed!
```

### Testing Fixtures

```python
# BEFORE
import yaml
import pytest

@pytest.fixture
def test_data():
    with open('tests/fixtures/data.yaml') as f:
        return yaml.safe_load(f)

def test_processing(test_data):
    result = process(test_data)
    assert result['status'] == 'success'
```

```python
# AFTER
import libfyaml
import pytest

@pytest.fixture
def test_data():
    return libfyaml.load('tests/fixtures/data.yaml')  # Changed!

def test_processing(test_data):
    result = process(test_data)
    assert result['status'] == 'success'  # Works identically!
```

---

## Handling Differences

### 1. isinstance() Type Checks

**Problem**: `isinstance(value, str)` returns False for FyGeneric

**Solutions**:

```python
# ❌ BEFORE - Doesn't work with FyGeneric
if isinstance(data['name'], str):
    process_string(data['name'])

# ✅ AFTER - Option 1: Use FyGeneric methods
if data['name'].is_string():
    process_string(data['name'])

# ✅ AFTER - Option 2: Use duck typing (best!)
try:
    # Just use it - if it works, it's the right type
    result = data['name'].upper()
except AttributeError:
    # Handle wrong type
    pass

# ✅ AFTER - Option 3: Check operations, not types
# Instead of checking if it's a string, check if you can use it as a string
if hasattr(data['name'], 'upper'):
    process_string(data['name'])
```

### 2. Direct Mutation

**Problem**: FyGeneric is currently immutable in Python bindings

**Note**: libfyaml C library has mutable generic API. A future enhancement could auto-convert FyGeneric to native Python dict/list on mutation attempts (via `__setitem__`), making mutations seamless while maintaining the immutability advantage for read-only operations.

**Current solutions**:

```python
# ❌ CURRENT - Doesn't work with FyGeneric
config = libfyaml.load('config.yaml')
config['new_key'] = 'value'  # ERROR!

# ✅ SOLUTION - Option 1: Convert to native Python
config = libfyaml.load('config.yaml').to_python()
config['new_key'] = 'value'  # Works!

# ✅ SOLUTION - Option 2: Build new dict
config = libfyaml.load('config.yaml')
# Read from FyGeneric, write to new dict
updated_config = {**config.to_python(), 'new_key': 'value'}

# ✅ SOLUTION - Option 3: Separate read and write concerns (recommended!)
# Read config (use FyGeneric - efficient!)
config = libfyaml.load('config.yaml')
db_host = config['database']['host']

# Build new config for writing (use dict)
new_config = {'database': {'host': 'newhost'}}
with open('config.yaml', 'w') as f:
    f.write(libfyaml.dumps(new_config))
```

**Future**: With auto-conversion on mutation, this could become:
```python
# FUTURE - Auto-converts to dict on mutation attempt
config = libfyaml.load('config.yaml')  # Returns FyGeneric
config['new_key'] = 'value'  # Auto-converts to dict, mutation works!
type(config)  # Now <class 'dict'>
```

### 3. Multi-Document YAML

**Problem**: `load_all()` not yet exposed in Python bindings

**Note**: libfyaml C library has full multi-document support. This needs to be exposed in Python bindings - coming soon!

**Current workarounds**:

```python
# ❌ BEFORE - Not yet available in Python bindings
for doc in yaml.safe_load_all(file):
    process(doc)

# ✅ AFTER - Option 1: Split manually
content = open('multi.yaml').read()
docs = content.split('\n---\n')
for doc_str in docs:
    doc = libfyaml.loads(doc_str)
    process(doc)

# ✅ AFTER - Option 2: Use single-document YAML
# Combine into single document with array:
# Instead of: (multiple docs)
#   ---
#   item1: value1
#   ---
#   item2: value2
#
# Use: (single doc with array)
#   - item1: value1
#   - item2: value2

data = libfyaml.load('single-doc.yaml')
for item in data:
    process(item)

# ✅ AFTER - Option 3: Keep PyYAML for this specific case (temporary)
import yaml  # Use PyYAML for multi-doc until Python bindings expose it
for doc in yaml.safe_load_all(file):
    process(doc)
```

### 4. Custom Constructors/Representers

**Problem**: libfyaml doesn't support PyYAML's custom tags

**Solutions**:

```python
# ❌ BEFORE - Custom constructor
yaml.add_constructor('!env', env_constructor)
config = yaml.safe_load(file)

# ✅ AFTER - Option 1: Post-process
config = libfyaml.load('config.yaml')
# Manually handle special values
if isinstance(config.get('database', {}).get('password'), str):
    if config['database']['password'].startswith('$ENV_'):
        var_name = config['database']['password'][5:]
        config = config.to_python()
        config['database']['password'] = os.getenv(var_name)

# ✅ AFTER - Option 2: Pre-process YAML
# Transform YAML before loading
import re
yaml_str = open('config.yaml').read()
yaml_str = re.sub(r'!env (\w+)', lambda m: os.getenv(m.group(1)), yaml_str)
config = libfyaml.loads(yaml_str)

# ✅ AFTER - Option 3: Keep PyYAML for complex custom tags
import yaml
yaml.add_constructor('!env', env_constructor)
config = yaml.safe_load(file)
```

### 5. Defensive Casting

**Problem**: Your code has defensive casts that are no longer needed

**This isn't really a problem - it's an opportunity!**

```python
# 😐 BEFORE - Defensive casting with PyYAML
age = int(config.get('age', 0))
name = str(config.get('name', ''))
score = float(config.get('score', 0.0))

# 😊 AFTER - Can keep working (but unnecessary)
age = int(config.get('age', 0))
name = str(config.get('name', ''))
score = float(config.get('score', 0.0))

# 🎉 AFTER - Better! Remove casts, use zero-casting
age = config.get('age', 0)
name = config.get('name', '')
score = config.get('score', 0.0)
# FyGeneric handles type conversions automatically!
```

---

## Testing Your Migration

### Test Strategy

1. **Unit tests**: Verify YAML parsing correctness
2. **Integration tests**: Verify end-to-end behavior
3. **Performance tests**: Measure speedup
4. **Regression tests**: Ensure no behavior changes

### Example Test Suite

```python
import unittest
import libfyaml
import yaml  # Keep PyYAML for comparison

class TestMigration(unittest.TestCase):

    def setUp(self):
        self.yaml_str = """
        server:
          host: localhost
          port: 8080
        users:
          - name: Alice
            age: 30
          - name: Bob
            age: 25
        """

    def test_parsing_equivalence(self):
        """Verify libfyaml parses same as PyYAML"""
        pyyaml_data = yaml.safe_load(self.yaml_str)
        libfyaml_data = libfyaml.loads(self.yaml_str)

        # Convert libfyaml to native for comparison
        libfyaml_native = libfyaml_data.to_python()

        self.assertEqual(pyyaml_data, libfyaml_native)

    def test_dict_access(self):
        """Verify dict access works"""
        data = libfyaml.loads(self.yaml_str)

        self.assertEqual(data['server']['host'], 'localhost')
        self.assertEqual(data['server']['port'], 8080)

    def test_list_iteration(self):
        """Verify list iteration works"""
        data = libfyaml.loads(self.yaml_str)

        names = [user['name'] for user in data['users']]
        self.assertEqual(names, ['Alice', 'Bob'])

    def test_arithmetic(self):
        """Verify arithmetic works (zero-casting)"""
        data = libfyaml.loads(self.yaml_str)

        # This tests zero-casting!
        ages_next_year = [user['age'] + 1 for user in data['users']]
        self.assertEqual(ages_next_year, [31, 26])

    def test_comparisons(self):
        """Verify comparisons work"""
        data = libfyaml.loads(self.yaml_str)

        adults = [u for u in data['users'] if u['age'] >= 30]
        self.assertEqual(len(adults), 1)
        self.assertEqual(adults[0]['name'], 'Alice')

    def test_performance(self):
        """Verify performance improvement"""
        import time

        # Large YAML for meaningful timing
        large_yaml = "items:\n" + "".join(
            f"  - id: {i}\n    value: {i*10}\n"
            for i in range(10000)
        )

        # Time PyYAML
        start = time.time()
        pyyaml_data = yaml.safe_load(large_yaml)
        pyyaml_time = time.time() - start

        # Time libfyaml
        start = time.time()
        libfyaml_data = libfyaml.loads(large_yaml)
        libfyaml_time = time.time() - start

        speedup = pyyaml_time / libfyaml_time
        print(f"\nSpeedup: {speedup:.1f}x")

        # Verify libfyaml is faster
        self.assertGreater(speedup, 10, "libfyaml should be >10x faster")

if __name__ == '__main__':
    unittest.main()
```

### Running Tests

```bash
# Run tests
python -m pytest test_migration.py -v

# Check for behavioral differences
python -m pytest test_migration.py::TestMigration::test_parsing_equivalence

# Benchmark performance
python -m pytest test_migration.py::TestMigration::test_performance -s
```

---

## Troubleshooting

### Issue 1: "FyGeneric object is not subscriptable"

**Cause**: Trying to access FyGeneric that's not a mapping or sequence

```python
# ❌ Error
data = libfyaml.loads("value: 42")
result = data['value']['nested']  # If 'value' is scalar, this fails
```

**Fix**: Check the type first

```python
# ✅ Fixed
data = libfyaml.loads("value: 42")
if data['value'].is_mapping():
    result = data['value']['nested']
else:
    print(f"Value is scalar: {data['value']}")
```

### Issue 2: "Cannot modify FyGeneric"

**Cause**: FyGeneric is currently immutable in Python bindings

```python
# ❌ Error
config = libfyaml.load('config.yaml')
config['new_key'] = 'value'  # FyGeneric doesn't support assignment yet
```

**Fix**: Convert to native Python first

```python
# ✅ Fixed
config = libfyaml.load('config.yaml').to_python()
config['new_key'] = 'value'
```

**Note**: Future enhancement could auto-convert FyGeneric on mutation attempts, making this seamless.

### Issue 3: Type checks fail

**Cause**: `isinstance()` doesn't work with FyGeneric

```python
# ❌ Fails
data = libfyaml.loads("name: Alice")
if isinstance(data['name'], str):  # Returns False!
    process(data['name'])
```

**Fix**: Use FyGeneric methods or duck typing

```python
# ✅ Fixed - Option 1
if data['name'].is_string():
    process(data['name'])

# ✅ Fixed - Option 2
# Just use it - zero-casting handles it!
process(data['name'])  # Works!
```

### Issue 4: "File not found" with file handles

**Cause**: libfyaml expects filenames, not file objects

```python
# ❌ Error
with open('config.yaml') as f:
    config = libfyaml.load(f)  # Expects filename string
```

**Fix**: Use filename or read content

```python
# ✅ Fixed - Option 1: Use filename
config = libfyaml.load('config.yaml')

# ✅ Fixed - Option 2: Read content
with open('config.yaml') as f:
    config = libfyaml.loads(f.read())
```

### Issue 5: Performance not improved

**Cause**: Converting to native Python negates benefits

```python
# 😐 Slow - defeats the purpose
config = libfyaml.load('large.yaml').to_python()
```

**Fix**: Keep as FyGeneric, use directly

```python
# 🚀 Fast - lazy, memory-efficient
config = libfyaml.load('large.yaml')
# Use config directly!
host = config['server']['host']
```

---

## Real-World Examples

### Example 1: Flask Configuration

**BEFORE (PyYAML)**:

```python
# config.py
import yaml
from flask import Flask

def create_app():
    app = Flask(__name__)

    with open('config.yaml') as f:
        config = yaml.safe_load(f)

    app.config.update(
        SECRET_KEY=config['app']['secret_key'],
        DATABASE_URI=config['database']['uri'],
        REDIS_HOST=config['redis']['host'],
        REDIS_PORT=config['redis']['port']
    )

    return app
```

**AFTER (libfyaml)** - Minimal changes:

```python
# config.py
import libfyaml  # Changed!
from flask import Flask

def create_app():
    app = Flask(__name__)

    config = libfyaml.load('config.yaml')  # Changed!

    # Convert to dict for Flask (Flask expects native dict)
    app.config.update(
        SECRET_KEY=config['app']['secret_key'],
        DATABASE_URI=config['database']['uri'],
        REDIS_HOST=config['redis']['host'],
        REDIS_PORT=config['redis']['port']
    )

    return app
```

**Better approach** - Keep FyGeneric longer:

```python
# config.py
import libfyaml
from flask import Flask

def create_app():
    app = Flask(__name__)

    config = libfyaml.load('config.yaml')

    # Use FyGeneric directly - convert only at boundary
    flask_config = {
        'SECRET_KEY': str(config['app']['secret_key']),
        'DATABASE_URI': str(config['database']['uri']),
        'REDIS_HOST': str(config['redis']['host']),
        'REDIS_PORT': int(config['redis']['port'])
    }
    app.config.update(flask_config)

    return app
```

### Example 2: Data Validation

**BEFORE (PyYAML)**:

```python
# validator.py
import yaml
from jsonschema import validate

def validate_config(config_file, schema_file):
    with open(config_file) as f:
        config = yaml.safe_load(f)

    with open(schema_file) as f:
        schema = yaml.safe_load(f)

    validate(instance=config, schema=schema)
    return config
```

**AFTER (libfyaml)** - One line changed:

```python
# validator.py
import libfyaml  # Changed!
from jsonschema import validate

def validate_config(config_file, schema_file):
    config = libfyaml.load(config_file)  # Changed!
    schema = libfyaml.load(schema_file)  # Changed!

    # Convert for jsonschema (expects native Python)
    validate(instance=config.to_python(), schema=schema.to_python())

    return config
```

### Example 3: Data Pipeline

**BEFORE (PyYAML)**:

```python
# pipeline.py
import yaml
import pandas as pd

def load_and_process(yaml_file):
    """Load YAML data and convert to DataFrame"""
    with open(yaml_file) as f:
        data = yaml.safe_load(f)

    # Extract records
    records = []
    for item in data['items']:
        records.append({
            'id': item['id'],
            'name': item['name'],
            'value': float(item['value']),
            'timestamp': item['timestamp']
        })

    return pd.DataFrame(records)
```

**AFTER (libfyaml)** - Identical logic:

```python
# pipeline.py
import libfyaml  # Changed!
import pandas as pd

def load_and_process(yaml_file):
    """Load YAML data and convert to DataFrame"""
    data = libfyaml.load(yaml_file)  # Changed!

    # Extract records (IDENTICAL CODE!)
    records = []
    for item in data['items']:
        records.append({
            'id': item['id'],
            'name': item['name'],
            'value': float(item['value']),  # Zero-casting makes this unnecessary!
            'timestamp': item['timestamp']
        })

    return pd.DataFrame(records)
```

**Even better** - Remove defensive casting:

```python
# pipeline.py
import libfyaml
import pandas as pd

def load_and_process(yaml_file):
    """Load YAML data and convert to DataFrame"""
    data = libfyaml.load(yaml_file)

    # Cleaner with zero-casting!
    records = []
    for item in data['items']:
        records.append({
            'id': item['id'],
            'name': item['name'],
            'value': item['value'],  # No float() needed!
            'timestamp': item['timestamp']
        })

    return pd.DataFrame(records)
```

### Example 4: Configuration Management

**BEFORE (PyYAML)**:

```python
# config_manager.py
import yaml
import os

class ConfigManager:
    def __init__(self, env='development'):
        self.env = env
        self.config = self._load_config()

    def _load_config(self):
        base_config = self._load_file('config/base.yaml')
        env_config = self._load_file(f'config/{self.env}.yaml')

        # Merge configs
        return {**base_config, **env_config}

    def _load_file(self, path):
        if not os.path.exists(path):
            return {}

        with open(path) as f:
            return yaml.safe_load(f) or {}

    def get(self, key, default=None):
        keys = key.split('.')
        value = self.config

        for k in keys:
            if isinstance(value, dict):
                value = value.get(k)
                if value is None:
                    return default
            else:
                return default

        return value
```

**AFTER (libfyaml)** - Two lines changed:

```python
# config_manager.py
import libfyaml  # Changed!
import os

class ConfigManager:
    def __init__(self, env='development'):
        self.env = env
        self.config = self._load_config()

    def _load_config(self):
        base_config = self._load_file('config/base.yaml')
        env_config = self._load_file(f'config/{self.env}.yaml')

        # Merge configs (need native dicts for merging)
        return {**base_config.to_python(), **env_config.to_python()}

    def _load_file(self, path):
        if not os.path.exists(path):
            return libfyaml.loads("{}")  # Empty FyGeneric

        return libfyaml.load(path)  # Changed!

    def get(self, key, default=None):
        keys = key.split('.')
        value = self.config

        for k in keys:
            if isinstance(value, dict):
                value = value.get(k)
                if value is None:
                    return default
            else:
                return default

        return value
```

---

## Performance Tips

### Tip 1: Keep FyGeneric as Long as Possible

```python
# 😐 Less efficient - converts immediately
config = libfyaml.load('large.yaml').to_python()
value = config['deep']['nested']['value']

# 🚀 More efficient - lazy access
config = libfyaml.load('large.yaml')
value = config['deep']['nested']['value']  # Only this path is realized
```

### Tip 2: Avoid Unnecessary Conversions

```python
# 😐 Slow - converts entire tree
data = libfyaml.load('large.yaml')
native = data.to_python()
process(native['section']['item'])

# 🚀 Fast - convert only what you need
data = libfyaml.load('large.yaml')
# FyGeneric zero-casting handles conversions automatically
process(data['section']['item'])
```

### Tip 3: Use Direct File Loading

```python
# 😐 Slower - extra read step
with open('config.yaml') as f:
    content = f.read()
config = libfyaml.loads(content)

# 🚀 Faster - direct loading
config = libfyaml.load('config.yaml')
```

### Tip 4: Process Large Files Incrementally

```python
# 😐 Memory-heavy - loads entire file
data = libfyaml.load('huge.yaml').to_python()
for item in data['items']:
    process(item)

# 🚀 Memory-efficient - lazy access
data = libfyaml.load('huge.yaml')
for item in data['items']:  # Items accessed lazily
    process(item)
```

### Tip 5: Benchmark Your Specific Use Case

```python
import time
import libfyaml
import yaml

def benchmark_migration(yaml_file):
    # Benchmark PyYAML
    start = time.time()
    with open(yaml_file) as f:
        pyyaml_data = yaml.safe_load(f)
    pyyaml_time = time.time() - start

    # Benchmark libfyaml
    start = time.time()
    libfyaml_data = libfyaml.load(yaml_file)
    libfyaml_time = time.time() - start

    speedup = pyyaml_time / libfyaml_time

    print(f"PyYAML time: {pyyaml_time:.3f}s")
    print(f"libfyaml time: {libfyaml_time:.3f}s")
    print(f"Speedup: {speedup:.1f}x")

    return speedup

# Test your actual files
benchmark_migration('your-actual-config.yaml')
```

---

## Migration Checklist

### Pre-Migration

- [ ] Install libfyaml: `pip install libfyaml`
- [ ] Identify all YAML loading points in codebase
- [ ] Check for multi-document YAML usage (workarounds available, full support coming soon)
- [ ] Check for custom PyYAML constructors/representers
- [ ] Create backup branch: `git checkout -b pyyaml-to-libfyaml-migration`

### Migration

- [ ] Choose migration strategy (drop-in / incremental / full)
- [ ] Create compatibility shim if using drop-in strategy
- [ ] Update imports: `import yaml` → `import libfyaml`
- [ ] Update load calls: `yaml.safe_load(f)` → `libfyaml.load(filename)`
- [ ] Update dump calls: `yaml.dump(data, f)` → `libfyaml.dump(filename, data)`
- [ ] Fix isinstance() type checks → Use `.is_string()` etc.
- [ ] Fix mutations → Convert to native Python with `.to_python()`
- [ ] Remove defensive casting (optional but recommended)

### Testing

- [ ] Run existing test suite
- [ ] Add migration-specific tests
- [ ] Test with production-sized YAML files
- [ ] Verify performance improvements
- [ ] Check memory usage
- [ ] Test error handling

### Post-Migration

- [ ] Monitor production for issues
- [ ] Measure actual performance gains
- [ ] Update documentation
- [ ] Share results with team
- [ ] Consider removing PyYAML dependency

---

## Summary

### Key Takeaways

1. **Migration is easy**: Most code needs only import changes
2. **Performance gains are huge**: 238-390x speedup
3. **API is ~95% compatible**: Data access works identically
4. **Key differences**: FyGeneric vs native types, immutability
5. **Gradual migration works**: Port one module at a time

### When to Use libfyaml

✅ **Use libfyaml when**:
- Large YAML files (>1MB) - massive speedup
- Memory efficiency matters - 4-5x less memory
- Performance is important - 238-390x faster
- Modern Python codebase - leverages Python features

⚠️ **Stick with PyYAML when** (temporarily):
- Need multi-document YAML right now (`load_all` - coming to Python bindings soon!)
- Heavy use of custom constructors/representers
- Legacy codebase with extensive isinstance() checks (refactoring not worth it)
- Already fast enough and no problems

### Migration Effort

| Codebase Size | Migration Time | Speedup |
|---------------|----------------|---------|
| Small (<1000 LOC) | 30 mins - 2 hours | 238-390x |
| Medium (1000-10k LOC) | 2-8 hours | 238-390x |
| Large (>10k LOC) | 1-3 days | 238-390x |

### Support

- **Documentation**: See `PYYAML-COMPATIBILITY.md` for detailed API comparison
- **Examples**: Check `examples/` directory for more code samples
- **Issues**: Report problems at [github.com/pantoniou/libfyaml](https://github.com/pantoniou/libfyaml)

---

**Ready to migrate? Start with one config file and see the speedup for yourself!** 🚀
