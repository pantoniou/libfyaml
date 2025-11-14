# Documentation Updates - December 2025

## Overview

This document summarizes the major documentation updates for libfyaml's generic API, reflecting recent implementation changes and new features.

## New Features Documented

### 1. `fy_foreach` Iterator Macro

**Implementation**: Commit 766128f - "add fy_foreach iterator"

The `fy_foreach` macro provides Python-like iteration over sequences and mappings with automatic type casting.

**Key characteristics**:
- Universal iterator working with any collection type
- Automatic type casting based on iterator variable type
- Uses `__typeof__` and `_Generic` for type safety
- Uses `__COUNTER__` to avoid namespace pollution
- Supports nested iteration without conflicts
- Zero runtime overhead (inline macro expansion)

**Syntax**:
```c
fy_generic seq = fy_sequence("a", "b", "c");

// Iterate with automatic type casting
const char *str;
fy_foreach(str, seq) {
    printf("%s\n", str);
}

// Iterate as fy_generic values
fy_generic v;
fy_foreach(v, seq) {
    // Process generic value
}

// Iterate mapping keys
fy_generic config = fy_mapping("host", "localhost", "port", 8080);
const char *key;
fy_foreach(key, config) {
    fy_generic value = fy_get(config, key, fy_invalid);
    // Process key-value pair
}

// Iterate mapping key-value pairs
fy_generic_map_pair pair;
fy_foreach(pair, config) {
    // Access pair.key and pair.value
}
```

### 2. Polymorphic API Naming

**Changes**: Commit 8f1a780 - "updated docs with polymorphic api"

Simplified function naming across the API:
- `fy_local_sequence(...)` → `fy_sequence(...)`
- `fy_local_mapping(...)` → `fy_mapping(...)`

The polymorphic API automatically handles stack vs heap allocation based on usage context. Functions detect whether a builder is provided and adjust behavior accordingly.

### 3. Simplified Handle Types

**Changes**: Commit d09f912 - "doc: Fix handle types - simple const pointer typedefs"

Handle types changed from wrapped structs to simple const pointer typedefs:

**Before**:
```c
typedef struct {
    struct fy_generic_sequence *seq;
} fy_seq_handle;
```

**After**:
```c
typedef const struct fy_generic_sequence *fy_seq_handle;
```

This simplification makes the API cleaner while maintaining type safety.

### 4. Unified Collection Operations

**Changes**: Commits 10bd94b, 3dd39be, 6d1deb4 - Varargs rework and consolidation

The `fy_gb_collection_op()` function now provides a unified interface for all collection operations including parallel execution:

```c
// Serial operations
result = fy_gb_collection_op(gb, FYGBOPF_MAP, seq, 0, NULL, map_fn);
result = fy_gb_collection_op(gb, FYGBOPF_FILTER, seq, 0, NULL, filter_fn);
result = fy_gb_collection_op(gb, FYGBOPF_REDUCE, seq, 0, NULL, init, reducer_fn);

// Parallel operations (just add FYGBOPF_PARALLEL flag)
result = fy_gb_collection_op(gb, FYGBOPF_MAP | FYGBOPF_PARALLEL, seq, 0, NULL, map_fn, tp);
result = fy_gb_collection_op(gb, FYGBOPF_FILTER | FYGBOPF_PARALLEL, seq, 0, NULL, filter_fn, tp);
result = fy_gb_collection_op(gb, FYGBOPF_REDUCE | FYGBOPF_PARALLEL, seq, 0, NULL, init, reducer_fn, tp);
```

**Benefits**:
- Single entry point for all collection operations
- Consistent API for serial and parallel execution
- Runtime selection of execution mode via flags
- Future-proof (can add new modes without API changes)

## Documentation Files Updated

### 1. `doc/GENERIC-API-REFERENCE.md`

**Section**: "Iteration API"

**Changes**:
- Replaced proposed iterator designs with actual `fy_foreach` implementation
- Added comprehensive documentation of the macro definition
- Documented type safety mechanisms (`__typeof__`, `_Generic`)
- Added examples for:
  - Sequence iteration (generic values, automatic casting)
  - Mapping iteration (keys, key-value pairs)
  - Index-based iteration (traditional approach)
  - Nested iteration patterns
- Updated language comparisons (Python, TypeScript, Rust)
- Added implementation notes explaining `__COUNTER__` usage

**Lines added**: ~220

### 2. `doc/GENERIC-API-FUNCTIONAL.md`

**Section**: "Iteration with `fy_foreach`" (new section)

**Changes**:
- Added comprehensive section on using `fy_foreach` with functional operations
- Examples covering:
  - Basic sequence iteration
  - Automatic type conversion
  - Mapping iteration (keys and pairs)
  - Functional pipelines with iteration
  - Nested iteration
  - Iterating transformed collections
  - Integration with builder allocations
- Comparison with manual iteration showing benefits
- Positioned before "Thread Safety" section for logical flow

**Lines added**: ~220

### 3. `doc/GENERIC-API-EXAMPLES.md`

**Section**: "Iteration with `fy_foreach`" (new subsection under "Language Comparisons")

**Changes**:
- Added iteration examples comparing libfyaml with Python, TypeScript, and Rust
- Examples include:
  - Basic iteration
  - Mapping iteration
  - Nested iteration
  - Functional pipelines with iteration
- Updated "Key Observations" to include `fy_foreach` ergonomics
- Highlighted automatic type casting and zero-cost abstraction

**Lines added**: ~110

### 4. Existing Parallel Operations Documentation

**Files**:
- `UNIFIED-API-MIGRATION.md` (created previously)
- `PARALLEL-REDUCE-RESULTS.md`
- `C-VS-PYTHON-REDUCE.md`
- `PARALLEL-MAP-FILTER-IMPLEMENTATION.md`

**Status**: Already documents the unified `fy_gb_collection_op()` API with parallel flags.

## Summary of Changes

### Total Documentation Impact

| File | Lines Added | Sections Updated | New Sections |
|------|-------------|------------------|--------------|
| GENERIC-API-REFERENCE.md | ~220 | 1 | 1 (complete rewrite) |
| GENERIC-API-FUNCTIONAL.md | ~220 | 0 | 1 |
| GENERIC-API-EXAMPLES.md | ~110 | 1 | 1 |
| **Total** | **~550** | **2** | **3** |

### Key Improvements

1. **Complete `fy_foreach` documentation**
   - Implementation details
   - Usage patterns for sequences and mappings
   - Type safety mechanisms
   - Language comparisons

2. **Consistent examples across docs**
   - API Reference: Technical details and implementation
   - Functional Operations: Integration with functional patterns
   - Examples: Language comparisons and real-world usage

3. **Updated for recent commits**
   - Polymorphic API naming (fy_sequence, fy_mapping)
   - Simplified handle types (const pointers)
   - Unified collection operations API
   - New iterator macro

## What's Still Current

These documentation files remain accurate and don't need updates:

- `doc/GENERIC-API-BASICS.md` - Core concepts unchanged
- `doc/GENERIC-API-MEMORY.md` - Memory model unchanged
- `doc/GENERIC-API-PATTERNS.md` - Patterns still valid
- `doc/GENERIC-API-PERFORMANCE.md` - Performance characteristics unchanged
- `doc/GENERIC-API-UTILITIES.md` - Utility functions unchanged
- `doc/GENERIC-API-DESIGN.md` - Design rationale unchanged
- `doc/DEDUP-LOCKLESS-DESIGN.md` - Dedup allocator documentation current

## Testing Coverage

The `fy_foreach` feature includes comprehensive test coverage in `test/libfyaml-test-generic.c`:

- Generic value iteration
- Integer iteration with automatic casting
- String iteration with automatic casting
- Mapping key iteration
- Mapping key-value pair iteration
- Nested collections

All tests passing (commit 766128f).

## Migration Notes

### For Users of Previous Iteration Approaches

**Old approach** (index-based):
```c
for (size_t i = 0; i < fy_len(seq); i++) {
    fy_generic item = fy_get_item(seq, i);
    const char *str = fy_cast(item, "");
    printf("%s\n", str);
}
```

**New approach** (fy_foreach):
```c
const char *str;
fy_foreach(str, seq) {
    printf("%s\n", str);
}
```

**Migration is optional** - index-based iteration still works and is documented as an alternative for cases where you need the index.

### For Users of Parallel Operations

The direct parallel functions are now wrapped by the unified API:

**Old**:
```c
result = fy_gb_pmap(gb, tp, seq, 0, NULL, map_fn);
result = fy_gb_pfilter(gb, tp, seq, 0, NULL, filter_fn);
result = fy_gb_preduce(gb, tp, seq, 0, NULL, init, reducer_fn);
```

**New** (documented in `UNIFIED-API-MIGRATION.md`):
```c
result = fy_gb_collection_op(gb, FYGBOPF_MAP | FYGBOPF_PARALLEL, seq, 0, NULL, map_fn, tp);
result = fy_gb_collection_op(gb, FYGBOPF_FILTER | FYGBOPF_PARALLEL, seq, 0, NULL, filter_fn, tp);
result = fy_gb_collection_op(gb, FYGBOPF_REDUCE | FYGBOPF_PARALLEL, seq, 0, NULL, init, reducer_fn, tp);
```

## Future Documentation Tasks

Potential future enhancements (not currently needed):

1. **Video tutorials** - Screencast demonstrating fy_foreach usage
2. **Cookbook** - Common patterns using fy_foreach
3. **Performance guide** - When to use fy_foreach vs manual iteration
4. **Debugging guide** - Using fy_foreach with debuggers

## Conclusion

The documentation now comprehensively covers:
- ✅ New `fy_foreach` iterator macro
- ✅ Polymorphic API naming
- ✅ Simplified handle types
- ✅ Unified collection operations
- ✅ Complete usage examples
- ✅ Language comparisons
- ✅ Integration patterns

All documentation is up-to-date with the latest implementation (through commit 766128f) and provides clear guidance for using libfyaml's modern generic API.
