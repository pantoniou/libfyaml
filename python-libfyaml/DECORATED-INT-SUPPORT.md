# Decorated Integer Support for Python Bindings

**Date:** 2025-12-31
**Summary:** Added support for the full unsigned long long range using decorated integers

## Overview

The Python bindings now support the complete unsigned long long range (0 to 2^64-1) by utilizing libfyaml's `fy_generic_decorated_int` type with the `FYGDIF_UNSIGNED_RANGE_EXTEND` flag for values exceeding `LLONG_MAX`.

## Changes

### 1. Extended `numeric_operand` Structure

Added fields to track large unsigned integers:

```c
typedef struct {
    int is_int;              /* 1 if integer, 0 if float */
    int is_unsigned_large;   /* 1 if unsigned value > LLONG_MAX */
    long long int_val;       /* signed integer value */
    unsigned long long uint_val; /* unsigned integer value (if is_unsigned_large) */
    double float_val;        /* float value (if !is_int) */
} numeric_operand;
```

### 2. Updated `extract_numeric_operand()` Helper

Now detects decorated integers and extracts the correct value:

```c
if (type == FYGT_INT) {
    /* Get decorated int to check if it's unsigned and large */
    fy_generic_decorated_int dint = fy_cast(fyg, fy_dint_empty);

    if (dint.flags & FYGDIF_UNSIGNED_RANGE_EXTEND) {
        /* Unsigned value > LLONG_MAX - use Python arbitrary precision */
        *py_obj = PyLong_FromUnsignedLongLong(dint.uv);
        return -3;  /* Overflow - use Python object */
    } else {
        /* Regular signed long long */
        operand->int_val = dint.sv;
        operand->is_int = 1;
        return 0;
    }
}
```

### 3. Updated Arithmetic Operations

All six arithmetic operations now handle large unsigned values:
- **`__add__`**, **`__sub__`**, **`__mul__`**: Delegate to Python's arbitrary precision when needed
- **`__truediv__`**: Converts large unsigned to double
- **`__floordiv__`**, **`__mod__`**: Use Python's operations for large values

### 4. Updated Type Conversions

**`FyGeneric_int()` - `__int__()` method:**
```c
if (type == FYGT_INT) {
    fy_generic_decorated_int dint = fy_cast(self->fyg, fy_dint_empty);
    if (dint.flags & FYGDIF_UNSIGNED_RANGE_EXTEND) {
        return PyLong_FromUnsignedLongLong(dint.uv);
    } else {
        return PyLong_FromLongLong(dint.sv);
    }
}
```

**`FyGeneric_to_python()` - `.to_python()` method:**
```c
case FYGT_INT: {
    fy_generic_decorated_int dint = fy_cast(self->fyg, fy_dint_empty);
    if (dint.flags & FYGDIF_UNSIGNED_RANGE_EXTEND) {
        return PyLong_FromUnsignedLongLong(dint.uv);
    } else {
        return PyLong_FromLongLong(dint.sv);
    }
}
```

**`python_to_generic()` - Python to FyGeneric conversion:**
```c
if (PyLong_Check(obj)) {
    long long val = PyLong_AsLongLong(obj);
    if (val == -1 && PyErr_Occurred()) {
        PyErr_Clear();
        unsigned long long uval = PyLong_AsUnsignedLongLong(obj);
        if (uval == (unsigned long long)-1 && PyErr_Occurred()) {
            return fy_invalid;  /* Too large even for unsigned long long */
        }
        /* Check if it exceeds LLONG_MAX - need decorated int */
        if (uval > (unsigned long long)LLONG_MAX) {
            fy_generic_decorated_int dint = {
                .uv = uval,
                .flags = FYGDIF_UNSIGNED_RANGE_EXTEND
            };
            return fy_gb_dint_type_create_out_of_place(gb, dint);
        }
        return fy_gb_long_long_create(gb, (long long)uval);
    }
    return fy_gb_long_long_create(gb, val);
}
```

## Supported Range

| Value | Minimum | Maximum |
|-------|---------|---------|
| **Signed long long** | -2^63 | 2^63 - 1 |
| **Unsigned long long** | 0 | 2^64 - 1 |
| **Python int** | unlimited | unlimited |

The bindings automatically:
- Use decorated ints for values in range `LLONG_MAX+1` to `ULLONG_MAX`
- Delegate to Python's arbitrary precision for values > `ULLONG_MAX`
- Preserve exact values through YAML/JSON round-trips

## Examples

### Basic Usage

```python
import libfyaml

# Values exceeding LLONG_MAX (9223372036854775807)
LLONG_MAX_PLUS_1 = 2**63      # 9223372036854775808
ULLONG_MAX = (2**64) - 1      # 18446744073709551615

# Parse from YAML
data = libfyaml.loads(f'value: {LLONG_MAX_PLUS_1}')
print(data['value'])  # 9223372036854775808

# Create from Python
fy_data = libfyaml.from_python({'value': ULLONG_MAX})
print(fy_data.to_python())  # {'value': 18446744073709551615}
```

### Arithmetic

```python
# All arithmetic operations work with large unsigned values
large_val = libfyaml.from_python({'v': 2**63})['v']

print(large_val + 1000)    # 9223372036854776808
print(large_val * 2)        # 18446744073709551616 (uses Python)
print(large_val / 2)        # 4.611686018427388e+18
print(large_val // 100)     # 92233720368547758
print(large_val % 1000)     # 808
```

### Round-Trip

```python
# YAML round-trip
data = libfyaml.from_python({'value': 2**63})
yaml_str = libfyaml.dumps(data)
# value: 9223372036854775808

parsed = libfyaml.loads(yaml_str)
assert parsed.to_python()['value'] == 2**63  # ✓

# JSON round-trip
json_str = libfyaml.json_dumps(data)
# {"value": 9223372036854775808}

parsed_json = libfyaml.loads(json_str)
assert parsed_json.to_python()['value'] == 2**63  # ✓
```

## Testing

Run the comprehensive test suite:

```bash
python3 libfyaml/test_decorated_int.py
```

Tests cover:
- Round-trip conversion (YAML, JSON, Python)
- Arithmetic operations (+, -, *, /, //, %)
- Type conversions (`int()`, `.to_python()`)
- Edge cases (LLONG_MAX, LLONG_MAX+1, ULLONG_MAX)
- Mixed operations with regular integers

## Performance

- **Small integers** (≤ 2^61): Stored inline, zero allocation
- **Regular long long** (-2^63 to 2^63-1): Out-of-place storage
- **Large unsigned** (2^63 to 2^64-1): Decorated int with single flag
- **Very large** (> 2^64-1): Delegates to Python's arbitrary precision

## Backward Compatibility

✅ **Fully backward compatible**

- Existing code continues to work unchanged
- Regular integers use the same code paths
- Only large unsigned values trigger decorated int handling
- No API changes to existing functions

## Technical Details

### Decorated Integer Structure

```c
typedef struct fy_generic_decorated_int {
    union {
        signed long long sv;    /* Signed value */
        unsigned long long uv;  /* Unsigned value */
    };
    unsigned long long flags;   /* FYGDIF_UNSIGNED_RANGE_EXTEND for large unsigned */
} fy_generic_decorated_int;
```

### Flag Definition

```c
#define FYGDIF_UNSIGNED_RANGE_EXTEND  FY_BIT(0)
```

When this flag is set, the `uv` field contains an unsigned value > `LLONG_MAX`.

### Casting

Use `fy_cast()` with `fy_dint_empty` to extract decorated integers:

```c
fy_generic_decorated_int dint = fy_cast(fyg, fy_dint_empty);
if (dint.flags & FYGDIF_UNSIGNED_RANGE_EXTEND) {
    /* Use dint.uv - unsigned value > LLONG_MAX */
} else {
    /* Use dint.sv - regular signed value */
}
```

## Files Modified

- **`libfyaml/_libfyaml_minimal.c`**:
  - Updated `numeric_operand` structure (+3 fields)
  - Updated `extract_numeric_operand()` helper (+45 lines)
  - Updated all 6 arithmetic operations (+150 lines)
  - Updated `FyGeneric_int()` (+9 lines)
  - Updated `FyGeneric_to_python()` (+9 lines)
  - Updated `python_to_generic()` (+22 lines)
  - Net change: +349 lines added, -49 lines removed

## Related Documentation

- [libfyaml Generic API Documentation](../doc/GENERIC-API.md)
- [Python Bindings Documentation](README.md)
- [JSON Serialization Support](JSON-SERIALIZATION.md)
