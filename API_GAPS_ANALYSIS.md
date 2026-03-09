# libfyaml API Gaps Analysis

## Overview

This document provides a comprehensive review of gaps and incomplete features in the **Generic API** and **Reflection API** of libfyaml. The analysis was performed on the current codebase and identifies areas that require attention for API completeness, consistency, and maturity.

---

## 1. Generic API Gaps

### 1.1 Incomplete Out-of-Place Copy Size Calculation

**Location:** `include/libfyaml/libfyaml-generic.h` (lines 3779-3795)

**Issue:** Three functions for calculating out-of-place buffer sizes are not implemented:

```c
/* fy_generic_out_of_place_size_sequence_handle() - Stub: deep-copy size not yet implemented */
static inline size_t fy_generic_out_of_place_size_sequence_handle(fy_generic_sequence_handle seqh)
{
    return 0;    // TODO calculate
}

/* fy_generic_out_of_place_size_mapping_handle() - Stub: deep-copy size not yet implemented */
static inline size_t fy_generic_out_of_place_size_mapping_handle(fy_generic_mapping_handle maph)
{
    return 0;    // TODO calculate
}

/* fy_generic_out_of_place_size_generic_builderp() - Stub: deep-copy size not yet implemented */
static inline size_t fy_generic_out_of_place_size_generic_builderp(struct fy_generic_builder *gb)
{
    return 0;    // TODO calculate
}
```

**Impact:** Users cannot calculate required buffer sizes for deep-copying complex collection types (sequences, mappings) and builder objects. These functions are part of the out-of-place encoding API and returning 0 may cause silent failures or incorrect memory allocation.

**Related API:** `fy_to_generic_outofplace_size()` macro that dispatches to these functions.

**Severity:** High - Impacts memory safety and correctness of copy operations

---

### 1.2 Numeric Literal Underscore Separators Not Supported

**Location:** `src/generic/fy-generic.c` (lines 1583, 1643)

**Issue:** Numeric parsing does not support underscore separators, which is a modern C literal feature:

```c
/* TODO: Support underscore separators in numeric literals (e.g., "1_000_000") */
// (in integer parsing)

/* TODO: Support underscore separators in numeric literals (e.g., "1_000.5") */
// (in float parsing)
```

**Example:** While C23 and other languages allow `1_000_000` for readability, libfyaml currently rejects such input.

**Impact:** Users cannot use underscore separators in numeric literals when parsing YAML/JSON with libfyaml, limiting compatibility with modern C code.

**Severity:** Low-Medium - Quality of life feature, not critical for basic functionality

---

### 1.3 YAML 1.1 Output Not Supported

**Location:** `src/generic/fy-generic-op.c` (line 2050)

**Issue:** Output operations only support YAML 1.2:

```c
/* XXX we only output YAML 1.2 */
```

**Impact:** Users cannot generate YAML 1.1 format output, limiting compatibility with systems that require YAML 1.1.

**Severity:** Medium - Important for interoperability with legacy systems

---

### 1.4 Terminal Width Adaptation Not Implemented

**Location:** `src/generic/fy-generic-op.c` (line 2059)

**Issue:** The `FYOPEF_WIDTH_ADAPT_TO_TERMINAL` flag is not honored:

```c
/* XXX FYOPEF_WIDTH_ADAPT_TO_TERMINAL ignoring adapt to terminal for now */
```

**Impact:** The feature flag for terminal width detection is accepted but ignored, so users cannot rely on automatic terminal width adaptation for output formatting.

**Severity:** Low - Nice-to-have feature for interactive output

---

### 1.5 Encoding Issues in Generic Encoder

**Location:** `src/generic/fy-generic-encoder.c`

**Issues:**

a) Line 753: Funky encoding scenario, not fatal
```c
/* XXX something very funky, but not fatal */
```

b) Line 411, 547: Wrong parameter type for functions
```c
/* XXX takes FYNS... - should've taken FYCS */
```

**Impact:** These are encoder edge cases that may produce suboptimal output in unusual scenarios.

**Severity:** Low - Not critical but indicates incomplete specification adherence

---

## 2. Reflection API Gaps

### 2.1 User Type System Not Integrated

**Location:** `src/reflection/fy-clang-backend.c` (line 2047)

**Issue:** No plug-in mechanism for custom/user-defined type systems:

```c
/* XXX TODO this is where a bolt on type system would resolve user type */
```

**Context:** When processing unresolved pointer dependencies, all are currently resolved to `void` with no mechanism for custom type resolution.

**Impact:** Users cannot define custom type resolution strategies for types unknown to libclang or for domain-specific type handling.

**Severity:** Medium - Limits extensibility for specialized use cases

---

### 2.2 Enum Type Concrete Type Information Missing

**Location:** `src/reflection/fy-clang-backend.c` (line 1630)

**Issue:** Enums don't have concrete type information:

```c
/* XXX no concrete type for the enum, only storage size */
```

**Impact:** When reflecting on enums, libclang only provides the storage size, not the concrete underlying type (e.g., signed int, unsigned char). This limits type-safe handling of enum values.

**Severity:** Medium - Affects correct serialization/deserialization of enum values

---

### 2.3 Unresolved Type Assertion Missing

**Location:** `src/reflection/fy-clang-backend.c` (line 1120)

**Issue:** Assertion commented out:

```c
// XXX assert(ftu->dependent_type.kind != CXType_Invalid);
```

**Impact:** No validation that dependent types have valid kinds, potentially allowing invalid type references to pass through.

**Severity:** Medium - Error detection and validation weakness

---

### 2.4 Source File Lookup Not Optimized

**Location:** `src/reflection/fy-reflection.c` (line 2865)

**Issue:** Source file tracking uses linear search instead of hash table:

```c
/* TODO hash it */
for (srcf = fy_source_file_list_head(&rfl->source_files); srcf != NULL;
     srcf = fy_source_file_next(&rfl->source_files, srcf)) {
    if (!strcmp(srcf->realpath, realname))
        break;
}
```

**Impact:** O(n) lookup for source files. With many included headers, this becomes a performance issue.

**Severity:** Low - Performance issue, not correctness

---

### 2.5 UTF-8 Handling in Packed Backend

**Location:** `src/reflection/fy-packed-backend.c` (line 737)

**Issue:** Packed backend should use libfyaml's UTF-8 utilities:

```c
// XXX should be utf8 (use libfyaml)
```

**Impact:** Possible incorrect UTF-8 handling in packed blob serialization/deserialization.

**Severity:** Medium - Data integrity issue for non-ASCII names

---

### 2.6 Declaration Flags Not Set in Packed Backend

**Location:** `src/reflection/fy-packed-backend.c` (line 1442)

**Issue:** Declaration flags are set to 0 with no clear reason:

```c
declp->flags = 0;    /* XXX */
```

**Impact:** Loss of flag information during pack/unpack cycle.

**Severity:** Low - May affect metadata preservation

---

### 2.7 Portable Size Configuration Incomplete

**Location:** `src/reflection/fy-reflection.c` (line 2260)

**Issue:** Per-target configuration for implementation-defined sizes needed:

```c
/* XXX probably needs a per target config here, everything is implementation defined */
```

**Context:** Type sizes and alignments are implementation-defined for some types.

**Impact:** Reflection data may not be portable across different platforms/architectures without explicit per-target configuration.

**Severity:** Medium - Affects cross-platform portability

---

### 2.8 Lazy Source File Loading Not Implemented

**Location:** `src/reflection/fy-reflection.c` (line 2573)

**Issue:** Source file metadata is fully loaded upfront:

```c
/* XXX maybe do it on demand? */
```

**Impact:** Large reflection databases load all source information at initialization, even if not used.

**Severity:** Low - Performance/memory issue for large codebases

---

## 3. API Completeness Metrics

| Metric | Value |
|--------|-------|
| Generic API functions | 379 |
| Reflection API functions | 64 |
| Unimplemented TODOs in Generic | 3 |
| Unimplemented TODOs in Reflection | 0 (but 7+ XXXs) |
| XXX/HACK marks in Generic impl | 5 |
| XXX/HACK marks in Reflection impl | 8 |

---

## 4. Categorized Gap Summary

### Functional Gaps
1. **Generic API**: Out-of-place copy size calculation for collections and builders
2. **Generic API**: Numeric literal underscore separator support
3. **Generic API**: YAML 1.1 output format support
4. **Generic API**: Terminal width auto-adaptation
5. **Reflection API**: User-defined type system integration
6. **Reflection API**: Enum underlying type information

### Data Integrity Gaps
1. **Reflection API**: UTF-8 handling in packed backend
2. **Reflection API**: Declaration flags preservation in pack/unpack
3. **Reflection API**: Type assertion validation for dependencies

### Performance Gaps
1. **Generic API**: Source file lookup not hashed (O(n) complexity)
2. **Reflection API**: Lazy loading of source metadata not implemented
3. **Reflection API**: Full source file loading at initialization

### Extensibility Gaps
1. **Reflection API**: No plug-in system for custom type resolution
2. **Reflection API**: No per-target configuration for platform-specific sizes

---

## 5. Recommended Priority for Fixes

### High Priority (Correctness & Safety)
- Out-of-place collection copy size calculation
- Enum underlying type support
- Type assertion validation

### Medium Priority (Functionality & Interoperability)
- YAML 1.1 output support
- Numeric underscore separators
- User type system integration
- UTF-8 handling in packed backend
- Portable size configuration

### Low Priority (Performance & Polish)
- Source file lookup optimization
- Terminal width adaptation
- Lazy source metadata loading
- Declaration flag preservation

---

## 6. Notes

1. **Consistency:** The Generic API has more TODOs (3 explicit), while the Reflection API uses XXX comments (8) to mark issues, making reflection problems less obvious.

2. **Stability:** Both APIs are marked as experimental (pre-1.0), which explains some gaps.

3. **Coverage:** The Reflection API is less comprehensive (64 functions vs 379 in Generic), but this is by design - it focuses on type introspection rather than object manipulation.

4. **Documentation:** Some issues are marked but with limited context (e.g., "something very funky" at line 753 of generic-encoder).

---

## 7. Testing Observations

- No explicit gaps found in the type checking functions (`fy_type_kind_is_*`)
- Most primitive type handling appears complete
- Collections and complex types have the most gaps
- The packed backend (used for deployment) has more issues than the clang backend

---

## Appendix: Location Reference

### Generic API Header
- File: `/include/libfyaml/libfyaml-generic.h`
- Size: ~369 KB
- Functions: 379

### Reflection API Header
- File: `/include/libfyaml/libfyaml-reflection.h`
- Size: ~30 KB
- Functions: 64

### Key Implementation Files
- Generic encoder: `src/generic/fy-generic-encoder.c`
- Generic operations: `src/generic/fy-generic-op.c`
- Generic core: `src/generic/fy-generic.c`
- Reflection clang backend: `src/reflection/fy-clang-backend.c`
- Reflection packed backend: `src/reflection/fy-packed-backend.c`
- Reflection core: `src/reflection/fy-reflection.c`
