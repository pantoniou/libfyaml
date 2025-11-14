# Empty Structures Bug Fix and JSON Serialization Support

**Date:** 2025-12-31
**Summary:** Fixed critical bug with empty flow-style YAML structures and added JSON serialization support

## Changes Overview

This update includes two major improvements:

1. **Bug Fix:** Empty flow-style YAML structures (`[]` and `{}`) now work correctly
2. **New Feature:** JSON serialization support via `json_dumps()` and `json_dump()`

---

## 1. Empty Flow-Style Structures Bug Fix

### Problem

Empty flow-style YAML structures caused errors:

```python
import libfyaml

data = libfyaml.loads('items: []')
result = data.to_python()
# RuntimeError: Invalid sequence
```

This also caused segmentation faults when iterating over empty structures.

### Root Cause

In `_libfyaml_minimal.c`, when `fy_cast()` returned NULL for sequences or mappings, the code treated this as an error. However, NULL actually indicates an **empty** flow-style structure, not an invalid one.

### Solution

Modified `FyGeneric_to_python()` in `libfyaml/_libfyaml_minimal.c`:

**Lines 495-501 (Sequences):**
```c
case FYGT_SEQUENCE: {
    fy_generic_sequence_handle seqh = fy_cast(self->fyg, fy_seq_handle_null);
    if (!seqh) {
        /* Empty flow-style sequence (e.g., []) - return empty list */
        return PyList_New(0);
    }
    // ... rest of code
}
```

**Lines 543-549 (Mappings):**
```c
case FYGT_MAPPING: {
    fy_generic_mapping_handle maph = fy_cast(self->fyg, fy_map_handle_null);
    if (!maph) {
        /* Empty flow-style mapping (e.g., {}) - return empty dict */
        return PyDict_New();
    }
    // ... rest of code
}
```

### Testing

Now works correctly:

```python
import libfyaml

# Empty sequence
data = libfyaml.loads('items: []')
assert data.to_python() == {'items': []}

# Empty mapping
data = libfyaml.loads('config: {}')
assert data.to_python() == {'config': {}}

# Nested empty structures
data = libfyaml.loads('''
empty_list: []
empty_dict: {}
nested:
  also_empty: []
''')
result = data.to_python()
assert result == {
    'empty_list': [],
    'empty_dict': {},
    'nested': {'also_empty': []}
}
```

---

## 2. JSON Serialization Support

### Overview

Added convenient JSON serialization functions that automatically handle FyGeneric objects:

- `libfyaml.json_dumps(obj, **kwargs)` - Serialize to JSON string
- `libfyaml.json_dump(obj, fp, **kwargs)` - Serialize to file

### Usage

```python
import libfyaml

# Parse YAML
data = libfyaml.loads('name: Alice\nage: 30')

# Serialize to JSON
json_str = libfyaml.json_dumps(data)
# Result: '{"name": "Alice", "age": 30}'

# With formatting
json_str = libfyaml.json_dumps(data, indent=2)

# Write to file
with open('output.json', 'w') as f:
    libfyaml.json_dump(data, f, indent=2)
```

### Implementation

Added to `libfyaml/__init__.py`:

```python
import json as _json

def json_dumps(obj, **kwargs):
    """Serialize obj to JSON string, with FyGeneric support."""
    if isinstance(obj, FyGeneric):
        obj = obj.to_python()
    return _json.dumps(obj, **kwargs)

def json_dump(obj, fp, **kwargs):
    """Serialize obj to JSON file, with FyGeneric support."""
    if isinstance(obj, FyGeneric):
        obj = obj.to_python()
    return _json.dump(obj, fp, **kwargs)
```

### Features

- **Automatic conversion:** FyGeneric objects automatically converted to Python equivalents
- **Zero overhead:** Pure Python objects passed directly through
- **Full compatibility:** All `json.dumps()` parameters supported (indent, sort_keys, separators, etc.)
- **Unicode support:** Handles all Unicode characters correctly
- **Empty structures:** Works with empty lists and dicts (thanks to the bug fix above!)

### Type Mapping

| YAML/FyGeneric | JSON |
|----------------|------|
| null | null |
| bool | true/false |
| int | number |
| float | number |
| string | string |
| sequence | array |
| mapping | object |

---

## Files Modified

### Core Implementation

1. **`libfyaml/_libfyaml_minimal.c`**
   - Lines 498-500: Fixed empty sequence handling
   - Lines 546-548: Fixed empty mapping handling

2. **`libfyaml/__init__.py`**
   - Added `json_dumps()` function (lines 42-69)
   - Added `json_dump()` function (lines 72-93)
   - Updated `__all__` to export new functions

### Documentation

3. **`JSON-SERIALIZATION.md`** (new file)
   - Complete API reference
   - Usage examples
   - Type mapping table
   - Use cases and patterns
   - Performance notes

### Tests

4. **`libfyaml/test_json_serialization.py`** (new file)
   - 13 comprehensive test cases
   - Tests all JSON parameters
   - Tests all YAML types
   - Tests edge cases (empty structures, Unicode, deep nesting)
   - All tests passing ✅

---

## Benefits

### For Users

1. **Simpler API:** No manual `.to_python()` calls needed
2. **YAML → JSON conversion:** One-liner format conversion
3. **No more segfaults:** Empty structures work correctly
4. **Better compatibility:** Works with all standard JSON parameters

### For Developers

1. **Clean implementation:** Minimal code, maximum functionality
2. **Well tested:** Comprehensive test suite
3. **Well documented:** Complete usage guide
4. **No breaking changes:** Purely additive features

---

## Examples

### Before This Update

```python
import libfyaml
import json

# This would crash!
data = libfyaml.loads('items: []')
# RuntimeError: Invalid sequence

# Manual conversion required for JSON
data = libfyaml.loads('name: Alice')
py_data = data.to_python()
json_str = json.dumps(py_data)
```

### After This Update

```python
import libfyaml

# Empty structures work!
data = libfyaml.loads('items: []')
result = data.to_python()  # {'items': []}

# JSON serialization is automatic
data = libfyaml.loads('name: Alice')
json_str = libfyaml.json_dumps(data)  # One line!
```

---

## Testing

Run the test suite:

```bash
# JSON serialization tests
python3 libfyaml/test_json_serialization.py

# Output:
# ✓ test_json_dumps_simple
# ✓ test_json_dumps_with_indent
# ✓ test_json_dumps_nested
# ✓ test_json_dumps_all_types
# ✓ test_json_dump_to_file
# ✓ test_json_dumps_empty_structures  # This one used to fail!
# ✓ test_json_dumps_unicode
# ✓ test_json_dumps_numbers
# ✓ test_json_roundtrip
# ✓ test_json_dumps_with_sort_keys
# ✓ test_json_dumps_with_separators
# ✓ test_json_dumps_deeply_nested
# ✓ test_json_dumps_mixed_sequence
#
# ✅ All JSON serialization tests passed!
```

---

## Backward Compatibility

✅ **Fully backward compatible**

- No existing APIs changed
- Only new functions added
- Bug fix only corrects previously broken behavior
- All existing code continues to work

---

## Related Work

This completes the Python type emulation series:

1. ✅ Dynamic `__class__` property for `isinstance()` support
2. ✅ Hash support for dict keys and sets
3. ✅ Integer precision preservation in arithmetic
4. ✅ Empty structures bug fix (this update)
5. ✅ JSON serialization support (this update)

FyGeneric objects now behave like native Python types in virtually all contexts!
