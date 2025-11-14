# Type Propagation in Python Bindings

## The Problem

Originally, accessing generic data required explicit casting everywhere:

```python
doc = libfyaml.loads("name: Alice\nage: 30")

# ❌ Verbose - needed casting for every comparison
if int(doc['age']) > 25:
    print(str(doc['name']))

# ❌ Verbose - filtering required explicit casts
adults = [u for u in users if int(u['age']) >= 18]
```

## The Solution: Rich Comparison Operators

Implemented `__richcompare__` to enable **automatic type conversion in comparisons** while preserving lazy access benefits.

### What Python Gives Us Automatically

Python's data model provides several automatic conversions through magic methods:

| Context | Magic Method | Example | Works? |
|---------|-------------|---------|---------|
| Boolean | `__bool__()` | `if doc['active']:` | ✅ Always worked |
| String formatting | `__str__()` | `f"Hello {doc['name']}"` | ✅ Always worked |
| Comparisons | `__richcompare__()` | `doc['age'] == 30` | ✅ **NOW WORKS** |
| Arithmetic | `__add__()`, `__mul__()`, etc. | `doc['age'] + 1` | ❌ Still needs casting |

### Implementation

Added `FyGeneric_richcompare()` that implements all 6 comparison operators:
- `==`, `!=` (equality)
- `<`, `<=`, `>`, `>=` (ordering)

The function:
1. Extracts the native value based on type (int, float, string, bool)
2. Compares with Python objects OR other FyGeneric objects
3. Returns `True`/`False` or `NotImplemented` (for unsupported types)

```c
/* FyGeneric: __richcompare__ - implements ==, !=, <, <=, >, >= */
static PyObject *
FyGeneric_richcompare(PyObject *self, PyObject *other, int op)
{
    FyGenericObject *self_obj = (FyGenericObject *)self;
    enum fy_generic_type self_type = fy_get_type(self_obj->fyg);

    /* Type-specific comparison logic for:
       - FYGT_INT: compare as long long
       - FYGT_FLOAT: compare as double
       - FYGT_STRING: compare as strcmp
       - FYGT_BOOL: compare as _Bool
    */

    // Supports:
    // - FyGeneric vs Python native (doc['age'] == 30)
    // - FyGeneric vs FyGeneric (doc['age'] == doc2['age'])
}
```

### Usage Examples

**Before (verbose):**
```python
# Filtering
adults = [u for u in users if int(u['age']) >= 18]

# Conditionals
if int(doc['age']) > 25 and str(doc['name']) == "Alice":
    ...

# Finding
user = next(u for u in users if str(u['name']) == target_name)
```

**After (clean):**
```python
# Filtering - natural Python!
adults = [u for u in users if u['age'] >= 18]

# Conditionals - reads like Python
if doc['age'] > 25 and doc['name'] == "Alice":
    ...

# Finding - clean and readable
user = next(u for u in users if u['name'] == target_name)
```

## When You Still Need Casting

### Arithmetic Operations

Arithmetic doesn't automatically convert because it's ambiguous what type to return:

```python
# ❌ Doesn't work
result = doc['age'] + 10

# ✅ Explicit cast
result = int(doc['age']) + 10
```

**Why not implement `__add__()`?**
- Return type ambiguity: should `doc['age'] + 10` return `FyGeneric` or `int`?
- Breaking lazy access: arithmetic implies you want the computed value
- NumPy pattern: NumPy arrays DO support arithmetic, but they return arrays
- For libfyaml, explicit casting is clearer for value extraction

### Type-Specific Methods

Methods like `.upper()`, `.split()` are type-specific and require the actual Python type:

```python
# ❌ Doesn't work
upper_name = doc['name'].upper()

# ✅ Cast first
upper_name = str(doc['name']).upper()
```

## Design Philosophy

The implementation follows **NumPy's selective automatic conversion**:

1. **Comparisons**: Automatic (most common use case)
   - Filtering: `[x for x in items if x['score'] > 90]`
   - Conditionals: `if user['age'] >= 18:`
   - Searching: `item == target_value`

2. **Arithmetic**: Explicit casting required
   - Clear intent: you want to compute with the value
   - Type clarity: explicitly specify int/float
   - Lazy access preservation: don't accidentally convert everything

3. **Boolean/String**: Already worked
   - Python implicitly calls these in appropriate contexts
   - `if value:` calls `__bool__()`
   - `f"{value}"` calls `__str__()`

## Performance

Comparison overhead is minimal:
- Type check: O(1)
- Value extraction: O(1) (no allocation, just cast from generic)
- Comparison: O(1) for numbers/bools, O(n) for strings
- **No Python object creation** until result boolean

Example filtering a 1000-item list:
- Before: 1000 `int()` calls + 1000 comparisons (creates 1000 Python int objects)
- After: 1000 comparisons (creates 0 intermediate objects)

## Complete API Summary

| Operation | Automatic? | Example |
|-----------|-----------|---------|
| **Comparisons** | ✅ Yes | `doc['age'] == 30` |
| **Boolean context** | ✅ Yes | `if doc['active']:` |
| **String formatting** | ✅ Yes | `f"Name: {doc['name']}"` |
| **Iteration** | ✅ Yes | `for item in doc:` |
| **Arithmetic** | ❌ Cast needed | `int(doc['age']) + 1` |
| **Type methods** | ❌ Cast needed | `str(doc['name']).upper()` |
| **Math functions** | ❌ Cast needed | `math.sqrt(float(doc['x']))` |

## Benefits

1. **Cleaner code**: Most common operations (filtering, conditionals) work naturally
2. **Pythonic**: Code reads like native Python dict/list operations
3. **Performance**: No intermediate object creation for comparisons
4. **Explicit when needed**: Arithmetic requires clear intent (cast)
5. **NumPy-like**: Familiar pattern for scientific Python users

## Code Size

The implementation adds ~80 lines to handle all 6 comparison operators across 4 types (int, float, string, bool), bringing total to **~1000 lines** for a fully-featured binding with:
- Parse/emit (loads/dumps/from_python)
- Lazy access with automatic comparisons
- Iteration support
- Memory-safe builder lifetime management
- Full type conversion support
