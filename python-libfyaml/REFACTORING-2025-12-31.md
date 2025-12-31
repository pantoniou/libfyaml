# Python Bindings Refactoring - December 31, 2025

## Issues Fixed

### 1. Code Duplication - Primitive Type Conversion
**Problem:** `FyGenericIterator_next` and `FyGeneric_to_python` both had identical 25-line switch statements converting fy_generic keys to Python objects.

**Solution:** Created `fy_generic_to_python_primitive()` helper function (lines 35-67) that:
- Handles all primitive types: NULL, BOOL, INT, FLOAT, STRING
- Properly errors on unhashable types (SEQUENCE, MAPPING) for dict keys
- Handles INDIRECT/ALIAS gracefully
- Eliminates ~50 lines of duplicate code

### 2. Iterator Bug - Returning Keys Instead of Values
**Problem:** Line 114 of `FyGenericIterator_next` was returning the **key** instead of the **value** when iterating over mappings.

```c
// WRONG:
result = FyGeneric_from_parent(key, self->generic_obj, key_obj);

// CORRECT:
result = FyGeneric_from_parent(item, self->generic_obj, key_obj);
```

**Impact:** Iterating over a mapping would yield keys, not values - opposite of Python dict behavior.

### 3. Missing Key Type Handling
**Problem:** Dict key conversion only handled 5 types (NULL, BOOL, INT, FLOAT, STRING), silently returning NULL for:
- SEQUENCE (arrays)
- MAPPING (nested dicts)
- INDIRECT
- ALIAS

**Solution:** Helper function now explicitly handles all types with proper error messages:
- SEQUENCE/MAPPING: `TypeError: unhashable type`
- INDIRECT/ALIAS: `RuntimeError: unresolved indirect/alias type`

### 4. Arithmetic Operations Losing Integer Precision
**Problem:** All arithmetic operations (`+`, `-`, `*`, `//`, `%`) converted everything to `double`, causing:

1. **Precision loss** for large integers:
   ```python
   x = (1 << 60) + 1  # 1152921504606846977
   result = x + 1     # Lost precision via double (53-bit mantissa)
   ```

2. **No overflow detection** - integer overflow was hidden by double conversion

3. **Wrong type semantics**:
   ```python
   10 // 3  # Returned int(double(10) / double(3)) instead of true integer division
   ```

**Solution:** Complete rewrite of arithmetic operations:

#### Addition/Subtraction/Multiplication
- Detect if both operands are integers
- Use `__builtin_add_overflow()`, `__builtin_sub_overflow()`, `__builtin_mul_overflow()`
- On overflow: promote to Python's arbitrary precision via `PyNumber_*` API
- Only convert to double when at least one operand is float

#### Floor Division
- Integer path: proper Python floor division semantics (rounds toward negative infinity)
- Float path: uses `floor()` function

#### Modulo
- Integer path: proper Python modulo semantics (same sign as divisor)
- Float path: uses `fmod()` function

## Test Results

All fixes verified with comprehensive test suite:

1. **Iterator correctness**: Iterating over mapping yields values (25, 30, 35), not keys
2. **Large integer precision**: `(1<<60)+1 + 1` preserves all 61 bits
3. **Overflow handling**: `LLONG_MAX + 100` correctly promotes to arbitrary precision
4. **Type preservation**: `10 // 3` returns `int`, not `float`
5. **Error handling**: Sequence keys properly raise `TypeError: unhashable type`

## Code Changes Summary

**File:** `python-libfyaml/libfyaml/_libfyaml_minimal.c`

- **Lines 35-67:** Added `fy_generic_to_python_primitive()` helper
- **Lines 126-133:** Fixed iterator to return values, use helper function
- **Lines 562-564:** Updated `FyGeneric_to_python` to use helper
- **Lines 1459-1533:** Rewrote `FyGeneric_add()` with integer precision
- **Lines 1535-1609:** Rewrote `FyGeneric_sub()` with integer precision
- **Lines 1611-1685:** Rewrote `FyGeneric_mul()` with integer precision
- **Lines 1723-1799:** Rewrote `FyGeneric_floordiv()` with proper semantics
- **Lines 1801-1877:** Rewrote `FyGeneric_mod()` with proper semantics

**Net change:** ~200 lines changed, ~50 lines eliminated (duplication)

## Performance Impact

**Positive:**
- Integer arithmetic no longer requires double conversion
- Overflow detection is compiler builtin (single instruction on x86-64)
- Reduced code duplication improves I-cache efficiency

**Neutral:**
- Float arithmetic unchanged (still uses double)
- Operator extraction is slightly more complex (type checking before conversion)

**Overall:** Expected performance improvement for integer-heavy workloads, negligible impact on float/mixed workloads.

## Breaking Changes

None - all changes preserve Python semantics and fix bugs. Users may notice:
1. Iterator over mappings now correctly yields values
2. Large integer arithmetic now correct (was broken before)
3. Better error messages for unsupported key types
