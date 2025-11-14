# JSON Serialization Support

The Python libfyaml bindings provide seamless JSON serialization for FyGeneric objects through the `json_dumps()` and `json_dump()` convenience functions.

## Overview

After parsing YAML with libfyaml, you can directly serialize the result to JSON without manual conversion:

```python
import libfyaml

# Parse YAML
data = libfyaml.loads('name: Alice\nage: 30')

# Serialize to JSON
json_str = libfyaml.json_dumps(data)
# Result: '{"name": "Alice", "age": 30}'
```

## API Reference

### `libfyaml.json_dumps(obj, **kwargs)`

Serialize an object to a JSON formatted string, with automatic FyGeneric conversion.

**Parameters:**
- `obj`: Object to serialize (FyGeneric or Python object)
- `**kwargs`: Additional arguments passed to `json.dumps()`

**Returns:**
- JSON formatted string

**Example:**
```python
import libfyaml

data = libfyaml.loads('''
person:
  name: Alice
  age: 30
  items: [apple, banana, cherry]
''')

# Simple serialization
json_str = libfyaml.json_dumps(data)

# With formatting
json_str = libfyaml.json_dumps(data, indent=2)

# With sorted keys
json_str = libfyaml.json_dumps(data, sort_keys=True)

# Compact format
json_str = libfyaml.json_dumps(data, separators=(',', ':'))
```

### `libfyaml.json_dump(obj, fp, **kwargs)`

Serialize an object to JSON and write to a file-like object, with automatic FyGeneric conversion.

**Parameters:**
- `obj`: Object to serialize (FyGeneric or Python object)
- `fp`: File-like object to write to
- `**kwargs`: Additional arguments passed to `json.dump()`

**Example:**
```python
import libfyaml

data = libfyaml.loads('name: Alice\nage: 30')

# Write to file
with open('output.json', 'w') as f:
    libfyaml.json_dump(data, f, indent=2)
```

## Type Mapping

YAML types are converted to their JSON equivalents:

| YAML/FyGeneric Type | JSON Type |
|---------------------|-----------|
| Null (`null`)       | `null`    |
| Boolean (`true`, `false`) | `true`, `false` |
| Integer             | Number    |
| Float               | Number    |
| String              | String    |
| Sequence (list)     | Array     |
| Mapping (dict)      | Object    |

## Features

### Unicode Support

Unicode characters are preserved by default:

```python
data = libfyaml.loads('''
english: Hello
japanese: こんにちは
emoji: 👋🌍
''')

json_str = libfyaml.json_dumps(data, ensure_ascii=False)
# Result includes Unicode characters directly
```

### Empty Structures

Empty sequences and mappings are properly handled:

```python
data = libfyaml.loads('''
empty_list: []
empty_dict: {}
nested:
  also_empty: []
''')

json_str = libfyaml.json_dumps(data)
# Result: '{"empty_list": [], "empty_dict": {}, "nested": {"also_empty": []}}'
```

### Nested Structures

Deeply nested structures are fully supported:

```python
data = libfyaml.loads('''
level1:
  level2:
    level3:
      value: deep
''')

json_str = libfyaml.json_dumps(data, indent=2)
```

### All json.dumps() Parameters

All standard `json.dumps()` parameters work:

```python
import libfyaml

data = libfyaml.loads('name: Alice\nage: 30')

# indent - Pretty printing
libfyaml.json_dumps(data, indent=2)
libfyaml.json_dumps(data, indent=4)

# sort_keys - Sort dictionary keys
libfyaml.json_dumps(data, sort_keys=True)

# separators - Custom separators
libfyaml.json_dumps(data, separators=(',', ':'))  # Compact
libfyaml.json_dumps(data, separators=(', ', ': '))  # Spaced

# ensure_ascii - Handle Unicode
libfyaml.json_dumps(data, ensure_ascii=False)

# Combine multiple parameters
libfyaml.json_dumps(data, indent=2, sort_keys=True, ensure_ascii=False)
```

## Implementation Details

### Automatic Conversion

The `json_dumps()` and `json_dump()` functions automatically detect FyGeneric objects and convert them to Python equivalents using `.to_python()` before serialization:

```python
def json_dumps(obj, **kwargs):
    # Convert FyGeneric to Python if needed
    if isinstance(obj, FyGeneric):
        obj = obj.to_python()
    return _json.dumps(obj, **kwargs)
```

This means:
- **Zero overhead** for pure Python objects (no conversion needed)
- **Automatic conversion** for FyGeneric objects
- **Full compatibility** with standard JSON parameters

### Performance

- Conversion happens once at the beginning
- All standard `json` module optimizations apply
- No custom JSON encoder overhead
- Efficient for both small and large documents

## Use Cases

### YAML to JSON Conversion

Convert YAML configuration files to JSON:

```python
import libfyaml

# Read YAML config
with open('config.yaml') as f:
    config = libfyaml.load(f)

# Write as JSON
with open('config.json', 'w') as f:
    libfyaml.json_dump(config, f, indent=2)
```

### API Response Conversion

Parse YAML API responses and re-serialize as JSON:

```python
import libfyaml
import requests

# Fetch YAML response
response = requests.get('https://api.example.com/data.yaml')
data = libfyaml.loads(response.text)

# Send as JSON to another service
json_payload = libfyaml.json_dumps(data)
requests.post('https://other-api.example.com/', json=json_payload)
```

### Configuration Format Migration

Migrate between YAML and JSON configurations:

```python
import libfyaml
import glob

# Convert all YAML files to JSON
for yaml_file in glob.glob('config/*.yaml'):
    with open(yaml_file) as f:
        data = libfyaml.load(f)

    json_file = yaml_file.replace('.yaml', '.json')
    with open(json_file, 'w') as f:
        libfyaml.json_dump(data, f, indent=2, sort_keys=True)
```

## Comparison with Standard json Module

### Before (Manual Conversion)

```python
import libfyaml
import json

data = libfyaml.loads('name: Alice\nage: 30')
# Manual conversion required
py_data = data.to_python()
json_str = json.dumps(py_data, indent=2)
```

### After (Automatic Conversion)

```python
import libfyaml

data = libfyaml.loads('name: Alice\nage: 30')
# Automatic conversion
json_str = libfyaml.json_dumps(data, indent=2)
```

## Related Functions

- `libfyaml.loads()` - Parse YAML string to FyGeneric
- `libfyaml.load()` - Parse YAML file to FyGeneric
- `libfyaml.dumps()` - Serialize FyGeneric to YAML string
- `libfyaml.dump()` - Serialize FyGeneric to YAML file
- `FyGeneric.to_python()` - Convert FyGeneric to Python dict/list

## Testing

Run the comprehensive test suite:

```bash
python3 libfyaml/test_json_serialization.py
```

Tests cover:
- Simple structures
- Nested structures
- All YAML types
- Empty structures
- Unicode handling
- Number types
- File I/O
- All json.dumps() parameters
- Round-trip conversion
