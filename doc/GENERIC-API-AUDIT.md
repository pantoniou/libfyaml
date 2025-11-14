# libfyaml Generic API Audit Report

**Date**: 2026-01-03
**Purpose**: Pre-release API audit for naming consistency, completeness, and multi-language binding support
**Scope**: Generic type system API (`fy_generic_*`, `fy_gb_*`, and related functions)

---

## Executive Summary

This audit identified critical naming inconsistencies, missing API operations, and gaps that would require every language binding to reimplement common functionality. The API has solid fundamentals but suffers from:

1. **Duplicate type checking functions** (identical implementations with different names)
2. **Fragmented type conversion API** (50+ macro variants, no clear canonical form)
3. **Incomplete operation wrappers** (33 operations, only ~5 have convenience functions)
4. **Asymmetric collection APIs** (sequences and mappings lack parity)
5. **Missing type coercion** (every binding must implement numeric promotion)

**Recommendation**: Address critical naming issues and add core type conversion functions before first public release. Estimated effort: 1-2 days for high-impact fixes.

---

## 1. Critical Issues (Must Fix Before Release)

### 1.1 Duplicate Type Checking Functions

**Problem**: Multiple functions with identical implementations:

```c
// These are IDENTICAL:
bool fy_generic_is_string(fy_generic g);
bool fy_generic_is_string_type(fy_generic g);

// These are IDENTICAL:
bool fy_generic_is_sequence(fy_generic g);
bool fy_generic_is_sequence_type(fy_generic g);

// These BOTH check for int type (no distinction for unsigned!):
bool fy_generic_is_int_type(fy_generic g);
bool fy_generic_is_uint_type(fy_generic g);
```

**Impact**: Users don't know which to use; suggests incomplete refactoring.

**Recommendation**:
- Keep `fy_generic_is_TYPE()` form (cleaner, more consistent)
- Remove `_type` suffix variants before release
- Document in migration guide if any external users exist

**Effort**: 30 minutes (remove duplicates, update docs)

---

### 1.2 Inconsistent Function Naming

**Problem**: Three competing naming patterns across the API:

```c
// Pattern A: fy_generic_TYPE_OPERATION()
fy_generic_is_sequence(g)
fy_generic_is_mapping(g)

// Pattern B: fy_generic_OPERATION_TYPE()
fy_generic_cast_default(g, default)

// Pattern C: fy_generic_QUALIFIER_TYPE_OPERATION()
fy_generic_is_direct_int_type(g)
```

**Additional inconsistencies**:
- `_nocheck` suffix appears randomly on some functions but not others
- `*p` suffix is ambiguous (pointer argument? pointer return? both?)
- Example: `fy_genericp_indirect_get_valuep_nocheck()` vs `fy_generic_indirect_get_value()`

**Recommendation**:
- Establish naming convention document
- Pattern A for type checks: `fy_generic_is_TYPE()`
- Pattern for operations: `fy_generic_COLLECTION_OPERATION()` (e.g., `fy_generic_sequence_append()`)
- Clearly document `_nocheck` semantics (when/why to use)
- Use distinct conventions for pointer args vs returns

**Effort**: 2-3 hours (document standards, audit for violations)

---

### 1.3 Type Conversion Fragmentation

**Problem**: Over 50 macro variants for type conversion with no clear "canonical" form:

```c
// All of these create/extract integers:
fy_int(...)
fy_int_const(...)
fy_int_alloca(...)
fy_local_int(...)
fy_gb_int(...)
// ... 45+ more variants
```

**Current documentation**: Scattered across 11 markdown files with no clear guidance on which to use when.

**Recommendation**:
- Document 2-3 canonical forms:
  - `fy_TYPE(value)` - stack-based literal (for temporaries)
  - `fy_gb_TYPE(gb, value)` - heap-allocated (for persistence)
  - `fy_cast(g, default)` - extraction with fallback
- Add "Quick Reference" table to main API docs
- Consider deprecating rarely-used variants

**Effort**: 1-2 hours (documentation only, no code changes needed)

---

## 2. High Priority: Missing API Functions

### 2.1 Type Conversion Operations

**Current State**: Python binding had to implement these manually (see `_libfyaml_minimal.c` lines 296-406):

```python
# What we implemented in Python:
int(fy_generic)     # bool→1/0, float→truncate, string→parse
float(fy_generic)   # bool→1.0/0.0, int→convert, string→parse
str(fy_generic)     # everything→string representation
bool(fy_generic)    # Python truthiness rules
```

**Problem**: Every language binding will need this. Go, Zig, JavaScript would all reimplement the same logic.

**Recommendation**: Add to C API:

```c
/**
 * Convert generic value to integer with Python-like semantics:
 * - bool: true→1, false→0
 * - int: return as-is
 * - float: truncate to integer
 * - string: parse as integer (base 10)
 * - others: returns fy_invalid
 */
fy_generic fy_generic_to_int(struct fy_generic_builder *gb, fy_generic g);

/**
 * Convert generic value to float with Python-like semantics:
 * - bool: true→1.0, false→0.0
 * - int: convert to double
 * - float: return as-is
 * - string: parse as float
 * - others: returns fy_invalid
 */
fy_generic fy_generic_to_float(struct fy_generic_builder *gb, fy_generic g);

/**
 * Convert generic value to string representation:
 * - Scalars: text representation
 * - Collections: YAML/JSON representation
 */
fy_generic fy_generic_to_string(struct fy_generic_builder *gb, fy_generic g);

/**
 * Convert generic value to boolean with Python-like truthiness:
 * - null, false, 0, 0.0, "" → false
 * - everything else → true
 */
fy_generic fy_generic_to_bool(struct fy_generic_builder *gb, fy_generic g);
```

**Effort**: 4-6 hours (implementation + tests)

---

### 2.2 Missing Operation Convenience Wrappers

**Current State**: 33 builder operations defined (`FYGBOP_*`) but only ~5 have convenience functions.

**Current usage** (verbose):
```c
// To filter a sequence, users must:
struct fy_generic_op_args args = {
    .filter_map_reduce_common.fn = my_predicate
};
fy_generic result = fy_generic_op_args(gb, FYGBOPF_FILTER, seq, &args);
```

**Should be**:
```c
fy_generic result = fy_gb_sequence_filter(gb, seq, my_predicate);
```

**Missing wrappers** (most commonly needed):

| Operation | Current | Needed Wrapper |
|-----------|---------|----------------|
| Filter | `FYGBOPF_FILTER` | `fy_gb_sequence_filter(gb, seq, predicate)` |
| Map | `FYGBOPF_MAP` | `fy_gb_sequence_map(gb, seq, transform)` |
| Reduce | `FYGBOPF_REDUCE` | `fy_gb_sequence_reduce(gb, seq, reducer, initial)` |
| Append | `FYGBOPF_APPEND` | `fy_gb_sequence_append(gb, seq, item)` |
| Insert | `FYGBOPF_INSERT` | `fy_gb_sequence_insert(gb, seq, idx, item)` |
| Assoc | `FYGBOPF_ASSOC` | `fy_gb_mapping_assoc(gb, map, key, value)` |
| Dissoc | `FYGBOPF_DISSOC` | `fy_gb_mapping_dissoc(gb, map, key)` |
| Keys | `FYGBOPF_KEYS` | `fy_gb_mapping_keys(gb, map)` |
| Values | `FYGBOPF_VALUES` | `fy_gb_mapping_values(gb, map)` |
| Contains | `FYGBOPF_CONTAINS` | `fy_gb_contains(gb, col, item)` ✓ (exists) |

**Recommendation**: Add top 10 most common operations as convenience wrappers.

**Effort**: 1 day (implementation + tests + docs)

---

### 2.3 Collection API Asymmetry

**Problem**: Sequences and mappings have inconsistent APIs:

```c
// Sequences: get by index
fy_generic_sequence_get_generic_default(seq, idx, default)

// Mappings: get by key
fy_generic_mapping_get_generic_default(map, key, default)

// But mappings ALSO get by index:
fy_generic_mapping_get_at_generic_default(map, idx, default)

// However, sequences DON'T have symmetric _get_at_* variants
// (they just alias to the same get_* functions)
```

**Recommendation**:
- Make sequence and mapping APIs parallel
- Or document why the asymmetry exists
- Consider adding `fy_generic_mapping_has_key(map, key)` for clarity

**Effort**: 1-2 hours (mostly documentation)

---

## 3. Medium Priority: Ergonomic Improvements

### 3.1 Comparison with Type Promotion

**Current State**: Python binding implements complex numeric promotion manually (see `compare_int_helper()` lines 817-940):

```c
// 140+ lines to compare int with:
// - unsigned large values
// - floats (promotes to float)
// - other FyGeneric types
// - Python int/float objects
```

**Problem**: Comparing `int(42)` with `float(42.5)` requires:
1. Detect types
2. Promote int to float
3. Create temporary Python float objects
4. Call Python comparison
5. Clean up temporaries

**Recommendation**: Add to C API:

```c
/**
 * Compare with automatic numeric type promotion:
 * - int vs float: promote int to float
 * - int vs bool: promote bool to int
 * - etc.
 */
int fy_generic_compare_coerce(fy_generic a, fy_generic b);
```

**Effort**: 3-4 hours (reuse existing comparison logic)

---

### 3.2 String Operations

**Current State**: Python binding had to create PyUnicode objects just to count UTF-8 characters (lines 447-455):

```c
// To count characters (not bytes):
fy_generic_sized_string szstr = fy_cast(self->fyg, fy_szstr_empty);
PyObject *str_obj = PyUnicode_FromStringAndSize(szstr.data, szstr.size);
Py_ssize_t length = PyUnicode_GET_LENGTH(str_obj);  // Character count
Py_DECREF(str_obj);
```

**Recommendation**: Add UTF-8 aware functions:

```c
size_t fy_generic_string_length_chars(fy_generic g);  // UTF-8 character count
size_t fy_generic_string_length_bytes(fy_generic g);  // byte count
```

**Effort**: 1-2 hours (use existing UTF-8 utilities)

---

### 3.3 Type Checking Helpers

**Recommendation**: Add convenience predicates:

```c
bool fy_generic_is_numeric(fy_generic g);      // int || float || bool
bool fy_generic_is_scalar(fy_generic g);       // not sequence/mapping
bool fy_generic_is_collection(fy_generic g);   // sequence || mapping
```

**Effort**: 30 minutes (trivial implementations)

---

## 4. Language Binding Analysis

### 4.1 Python Binding Pain Points (Already Implemented)

**What we had to implement manually**:

1. **Type conversions** (lines 296-406):
   - `FyGeneric_int()`: Handles bool→int, float→int (truncate), string→int (parse)
   - `FyGeneric_float()`: Handles bool→float, int→float, string→float (parse)
   - Decorated int complexity (large unsigned values)

2. **Comparison with promotion** (lines 817-1108):
   - `compare_int_helper()`: 140+ lines for int comparison with promotion
   - `compare_float_helper()`: Handles cross-type float comparisons
   - `compare_bool_helper()`: Promotes bool to int/float

3. **Collection operations** (lines 2161-2198):
   - `FyGeneric_contains()`: Had to use different APIs for sequences vs mappings
   - Contains on mapping uses `fy_generic_mapping_get_generic_default()` hack
   - Contains on sequence uses `fy_gb_contains()` (proper API)

4. **String handling** (lines 239-294):
   - Collections to string requires emitting then extracting
   - UTF-8 character counting requires creating Python objects

5. **Numeric operand extraction** (lines 2357-2442):
   - Complex return codes: 0 (success), -1 (error), -2 (not implemented), -3 (overflow)
   - Manual overflow detection and Python fallback

**Implications for other languages**:
- Go, Zig, JavaScript would all need the same workarounds
- Every binding reimplements type conversion semantics
- Each language would independently define "truthiness" rules

---

### 4.2 Go Language Binding Needs

**Type System Mapping**:
```go
// Go's strong typing requires explicit conversions
port := config.GetInt("port", 8080)      // Type-specific getter
host := config.GetString("host", "localhost")

// Error handling pattern
val, err := config.Get("timeout")
if err != nil {
    // handle error
}
```

**Key Needs**:
1. **Error returns**: Go uses `(value, error)` pattern; `fy_invalid` needs error context
2. **Explicit type getters**: `GetInt()`, `GetFloat()`, `GetString()`, etc.
3. **Iterator protocol**: For `range` loops over sequences/mappings
4. **Builder pattern**: For constructing nested structures idiomatically

**Missing from C API**:
- Functions that return error codes/context (not just `fy_invalid`)
- Type-specific getters (currently polymorphic via `_Generic`)
- Iterator callback API (exists but not well documented)

---

### 4.3 Zig Language Binding Needs

**Type System Mapping**:
```zig
// Zig's tagged unions map perfectly to fy_generic
const FyValue = union(enum) {
    null: void,
    boolean: bool,
    integer: i64,
    float: f64,
    string: []const u8,
    sequence: SeqHandle,
    mapping: MapHandle,
};
```

**Key Needs**:
1. **Optional types**: `fy_invalid` maps to `null` in `?fy_generic`
2. **Error unions**: Functions should return `!T` with error sets
3. **Allocator integration**: Zig passes allocators explicitly
4. **Comptime support**: Could enable zero-cost type dispatch

**Missing from C API**:
- Error set definitions (OutOfMemory, InvalidType, etc.)
- Allocator parameter threading (currently uses builder pattern)

---

### 4.4 JavaScript/TypeScript Binding Needs

**Dynamic Typing** (similar to Python):
```javascript
const val = config.get("port", 8080);
if (val instanceof FyGeneric) {
    const port = Number(val);
}
```

**Key Needs**:
1. **JSON interop**: Native JSON conversion (YAML ↔ JSON bridge)
2. **Async support**: Promises for large file parsing
3. **TypeScript definitions**: Complete `.d.ts` files for IDE support
4. **Streaming API**: Event-based parsing for large documents

**Missing from C API**:
- Direct YAML→JSON conversion (currently requires tree + emit)
- Async/callback hooks for event loop integration
- Streaming emitter (only parser has streaming support)

---

### 4.5 Universal Needs Across Languages

**All bindings require**:

| Need | Status | Priority |
|------|--------|----------|
| Type checking predicates | ✓ Exists | - |
| Type conversions | ✗ Missing | **HIGH** |
| Collection iteration | ✓ Exists (`fy_foreach`) | - |
| Comparison with coercion | ✗ Missing | Medium |
| Error context | ✗ Missing (`fy_invalid` opaque) | Medium |
| UTF-8 string length | ✗ Missing | Low |
| Mutation operations | Partial (via `fy_generic_op`) | High |

---

## 5. Recommendations by Priority

### Immediate (Before Release)

1. **Remove duplicate type checking functions** ⏱️ 30 min
   - Keep `fy_generic_is_TYPE()` form
   - Remove `_type` suffix variants
   - Update documentation

2. **Add type conversion functions** ⏱️ 4-6 hours
   - `fy_generic_to_int(gb, g)`
   - `fy_generic_to_float(gb, g)`
   - `fy_generic_to_string(gb, g)`
   - `fy_generic_to_bool(gb, g)`
   - These will be used by ALL language bindings

3. **Document type conversion macro variants** ⏱️ 1-2 hours
   - Create quick reference table
   - Specify canonical forms
   - Add to main API documentation

**Total effort**: ~1 day
**Impact**: Prevents user confusion, reduces binding implementation burden

---

### High Priority (Next Release)

1. **Add top 10 operation wrappers** ⏱️ 1 day
   - filter, map, reduce
   - append, insert
   - assoc, dissoc
   - keys, values, items

2. **Add comparison with coercion** ⏱️ 3-4 hours
   - `fy_generic_compare_coerce(a, b)`
   - Automatic numeric promotion

3. **Standardize naming conventions** ⏱️ 2-3 hours
   - Document patterns
   - Audit for violations
   - Fix inconsistencies

**Total effort**: ~2 days
**Impact**: Makes API much more ergonomic

---

### Medium Priority (Future)

1. **Error context improvements**
   - Thread-local error state
   - Error type enumeration
   - Better error messages

2. **Collection API symmetry**
   - Make sequence/mapping APIs parallel
   - Add missing operations

3. **UTF-8 string helpers**
   - Character length
   - Substring operations

4. **Iterator API refinement**
   - Better callback documentation
   - Streaming support

**Total effort**: ~1 week
**Impact**: Completes the API surface

---

## 6. Testing Implications

**For type conversion functions**:
```c
// Test suite should cover:
assert(fy_to_int(bool_true) == 1);
assert(fy_to_int(bool_false) == 0);
assert(fy_to_int(float_42_5) == 42);  // truncate
assert(fy_to_int(string_123) == 123);  // parse
assert(fy_to_float(bool_true) == 1.0);
assert(fy_to_float(int_42) == 42.0);
assert(fy_to_string(int_42) == "42");
assert(fy_to_string(sequence) == "[1, 2, 3]");  // YAML representation
```

**For operation wrappers**:
```c
// Each wrapper needs tests
seq = fy_gb_sequence_create(gb, 3, fy_int(1), fy_int(2), fy_int(3));
filtered = fy_gb_sequence_filter(gb, seq, is_even);
assert(length(filtered) == 1);

mapped = fy_gb_sequence_map(gb, seq, double_value);
assert(fy_get(mapped, 0) == 2);
```

---

## 7. Migration Guide (If Needed)

If any existing code uses the API (internal or external):

### Breaking Changes
1. Remove `*_type` suffix variants of type checking functions
   - **Before**: `fy_generic_is_string_type(g)`
   - **After**: `fy_generic_is_string(g)`

2. Consolidate type conversion macros (if we remove variants)
   - **Before**: `fy_int_const(42)`, `fy_int_alloca(42)`, `fy_local_int(42)`
   - **After**: `fy_int(42)` (stack), `fy_gb_int(gb, 42)` (heap)

### Non-Breaking Additions
- All new functions are additions, no breaking changes
- Type conversion functions are new API surface
- Operation wrappers are convenience functions around existing ops

---

## 8. Documentation Updates Needed

1. **API Reference** (`include/libfyaml.h` docstrings):
   - Document new type conversion functions
   - Document operation wrapper functions
   - Add `@since 1.0` tags

2. **Quick Reference Guide** (new doc):
   - Table of type conversion forms
   - When to use stack vs heap allocations
   - Common patterns and examples

3. **Migration Guide** (if needed):
   - Changes from internal API
   - Removed duplicate functions

4. **Language Binding Guide** (new doc):
   - Recommendations for Go bindings
   - Recommendations for Zig bindings
   - Recommendations for JavaScript bindings
   - Common patterns all bindings need

---

## 9. Summary Statistics

**API Surface Analysis**:
- Total `fy_generic_*` functions: ~150+
- Duplicate functions identified: 6
- Missing type conversion functions: 4
- Missing operation wrappers: ~25
- Naming inconsistencies: ~20

**Impact Metrics**:
- Functions every binding reimplements: 4 (type conversions)
- Lines of code saved per binding: ~300-500
- API confusion reduction: High (remove duplicates)
- Ergonomics improvement: High (add wrappers)

**Effort Estimates**:
- Critical fixes (pre-release): 1 day
- High priority (next release): 2 days
- Medium priority (future): 1 week
- Total to complete API: ~2 weeks

---

## Appendix A: Python Binding Code References

**Key implementation files**:
- `/mnt/980-linux/panto/work/sandbox/libfyaml-generics-docs/python-libfyaml/libfyaml/_libfyaml_minimal.c`

**Critical sections**:
- Lines 296-406: Type conversion implementations (`__int__`, `__float__`)
- Lines 817-1108: Comparison with numeric promotion
- Lines 2161-2198: Collection contains operation
- Lines 2357-2442: Numeric operand extraction helper
- Lines 3141-3284: Python→generic conversion

---

## Appendix B: Affected Header Files

**Primary**:
- `include/libfyaml.h` - Public API
- `include/libfyaml/fy-internal-generic.h` - Internal generic API

**Documentation**:
- `doc/GENERIC-API.md` - Main generic API docs
- `doc/GENERIC-API-REFERENCE.md` - Function reference
- `doc/GENERIC-API-BASICS.md` - Tutorial/basics
- 8 other generic API docs in `doc/`

---

## Appendix C: Contact and Follow-up

**Questions for clarification**:
1. Timeline for first public release?
2. Acceptable level of breaking changes?
3. Priority: C developers vs language binding developers?
4. Resources available for implementation?

**Next steps**:
1. Review and approve recommendations
2. Prioritize changes based on timeline
3. Implement critical fixes
4. Update documentation
5. Add tests for new functions
6. Update language binding plans

---

**End of Audit Report**
