# Generic API Gaps - Detailed Review

## Gap 1: Out-of-Place Copy Size Calculation for Collections

### Location
- File: `include/libfyaml/libfyaml-generic.h`
- Lines: 3779-3795
- Functions:
  - `fy_generic_out_of_place_size_sequence_handle()`
  - `fy_generic_out_of_place_size_mapping_handle()`
  - `fy_generic_out_of_place_size_generic_builderp()`

### Current Implementation
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

### Analysis

**Architecture Understanding:**
- Out-of-place encoding is used when a value cannot fit in the 64-bit `fy_generic` inline storage
- For strings: encoded with length prefix + string bytes + NUL
- For decorated integers: converted to variable-width encoding
- For sized strings and other complex types: similar prefix-based encoding

**For Collections (Sequences/Mappings):**
- These are already **handles** (pointers) to external structures
- They're designed to be in-place references to pre-existing collections
- The put functions already return `fy_invalid_value` with comment "should never happen"
- **Key insight:** These are fundamentally different from strings - they refer to external data, not data you can embed in a buffer

**For Builders:**
- A builder object is mutable, modifiable state
- Cannot be meaningfully "out-of-place encoded" in a static buffer
- Would require copying the entire arena contents

### Current Usage
```c
// From tests - out-of-place is used for:
- Large integers (> FYGT_INT_INPLACE_MAX)
- Long strings ("long string out of place")
- Sized strings
- Decorated integers

// NOT used for:
- Sequences (handles)
- Mappings (handles)
- Builders (mutable state)
```

### Semantic Question
The comment "deep-copy size not yet implemented" suggests there may have been intent to support this, but:
1. The corresponding `_put` functions return `fy_invalid_value`
2. No tests use these functions with collections
3. Conceptually, encoding a handle/pointer to a buffer doesn't make sense

### Recommendation
**Status: Likely Not a Real Gap**
- These functions are stubs for a feature that may never have been intended
- The 0 return value is safe (indicates cannot be encoded out-of-place)
- Consider either:
  1. **Remove these functions** if they're never needed
  2. **Document why they return 0** if they're intentionally not supported
  3. **Assert/error** if actually called with collection types

---

## Gap 2: Numeric Literal Underscore Separators

### Location
- File: `src/generic/fy-generic.c`
- Lines: 1583, 1643

### Current Implementation
```c
// Line 1583 (integer parsing)
/* TODO: Support underscore separators in numeric literals (e.g., "1_000_000") */

// Line 1643 (float parsing)
/* TODO: Support underscore separators in numeric literals (e.g., "1_000.5") */
```

### Feature Description
Modern C (C23) and many languages (Python, Java, Rust, etc.) support underscores in numeric literals for readability:
- `1_000_000` instead of `1000000`
- `1_000.5` instead of `1000.5`
- `0xFF_FF_FF` for hex

### Impact
- Users cannot use underscore-separated numerics in YAML/input
- Would reject valid modern C literal syntax if parsing C-style numbers
- Minor compatibility issue for readability-focused code

### Implementation Difficulty
- Low to Medium
- Just need to skip `_` characters during parsing
- Must ensure they're not at start/end or consecutive
- C23 spec details available

### Examples of Where Needed
```
numbers_with_separators:
  - 1_000_000      # Should parse as 1000000
  - 0xFF_FF_FF     # Should parse as 0xFFFFFF
  - 3.14_159       # Should parse as 3.14159
```

### Recommendation
**Status: Nice-to-Have Feature**
- Severity: Low-Medium (quality of life, not critical)
- Priority: Low (legacy code doesn't use this)
- Implementation: Straightforward once tests are written
- Test coverage needed: All numeric types with various separator patterns

---

## Gap 3: YAML 1.1 Output Format Support

### Location
- File: `src/generic/fy-generic-op.c`
- Line: 2050

### Current Implementation
```c
/* XXX we only output YAML 1.2 */
```

### Specification Details
- YAML 1.1: Published 2005, widely supported, some legacy systems
- YAML 1.2: Published 2009, cleaner spec, better alignment with JSON
- Key differences:
  - Boolean values: "yes/no/on/off" (1.1) vs strict true/false (1.2)
  - Null representation: "~", "Null", "NULL", "" (1.1) vs only "null" (1.2)
  - Integer parsing: More implicit types in 1.1 (e.g., "01" is octal)

### Impact
- Users cannot generate YAML 1.1 compatible output
- Systems expecting YAML 1.1 may reject libfyaml output
- Primarily affects legacy integrations

### Implementation Difficulty
- Medium to High
- Requires tracking output version throughout encoding
- Different rules for booleans, nulls, quoted strings
- Existing code assumes YAML 1.2

### When Needed
- Legacy system integration
- Interoperability with older YAML processors
- Not needed for modern workflows

### Recommendation
**Status: Medium Priority**
- Severity: Medium (interoperability issue)
- Priority: Medium (needed for legacy systems)
- Implementation: Moderate complexity
- Consider: Feature flag to select output version

---

## Gap 4: Terminal Width Auto-Adaptation

### Location
- File: `src/generic/fy-generic-op.c`
- Line: 2059

### Current Implementation
```c
/* XXX FYOPEF_WIDTH_ADAPT_TO_TERMINAL ignoring adapt to terminal for now */
```

### Feature Description
Flag: `FYOPEF_WIDTH_ADAPT_TO_TERMINAL`
- Intended to detect terminal width using system calls
- Auto-wrap output to match terminal capabilities
- Would query terminal via termios/ioctl on Unix-like systems

### Impact
- Output width is fixed regardless of terminal size
- Users get hardcoded line lengths in formatted output
- Only affects pretty-printed/formatted output, not normal operation

### Implementation Difficulty
- Low
- Can use `ioctl(TIOCGWINSZ)` on Unix
- Graceful fallback to default width if not a terminal
- Already has framework for custom width

### When Needed
- Interactive output to terminal
- Pretty-printing large data structures
- Not needed for non-interactive use

### Recommendation
**Status: Low Priority (Polish)**
- Severity: Low (non-critical feature)
- Priority: Low (affects only interactive output)
- Implementation: Straightforward
- Use case: Making pretty-printed output more readable

---

## Gap 5: Encoding Edge Cases

### Location
- File: `src/generic/fy-generic-encoder.c`
- Multiple locations with XXX comments

### Issues Identified

#### 5.1 Funky Encoding Scenario (Line 753)
```c
/* XXX something very funky, but not fatal */
```
- Context: Unclear from code comment alone
- Severity: Low (marked "not fatal")
- Needs investigation of surrounding code

#### 5.2 Wrong Parameter Type (Lines 411, 547)
```c
/* XXX takes FYNS... - should've taken FYCS */
```
- Context: Function takes `FYNS` (namespace) but should take `FYCS` (?)
- Suggests suboptimal parameter design
- May impact encoding behavior in edge cases
- Severity: Low to Medium

### Impact
These appear to be known encoder issues that work but produce suboptimal results.

### Recommendation
**Status: Requires Investigation**
- Need to examine actual code context
- Likely minor API design issues
- Probably don't affect correctness, only performance/optimization

---

## Summary Table: Generic API Gaps

| Gap | Type | Severity | Priority | Fixability | Notes |
|-----|------|----------|----------|------------|-------|
| Out-of-place size for collections | Architecture | Low | Low | Medium | Likely not a real gap; stubs may be correct |
| Numeric underscore separators | Feature | Low-Med | Low | Low | Nice-to-have, straightforward implementation |
| YAML 1.1 output | Interop | Medium | Medium | Medium | Needed for legacy systems |
| Terminal width adaptation | Polish | Low | Low | Low | Good quality-of-life improvement |
| Encoder edge cases | Quality | Low | Low | Medium | Need code investigation |

---

## Recommended Action Plan

### Immediate Actions
1. **Clarify out-of-place collections** - Determine if this is intentional or a misguided TODO
2. **Investigate encoder XXXs** - Read the actual code context to understand issues

### Short Term (1-2 weeks)
1. **Numeric underscores** - Add tests, implement parsing skip logic
2. **Terminal width** - Implement using standard termios APIs

### Medium Term (1-2 months)
1. **YAML 1.1 support** - Add feature flag, implement output variants
2. **Encoder fixes** - Address the parameter type issues

### Documentation
- Comment out or remove obsolete stubs if confirmed not needed
- Add TODO comments with rationale if deferring work
- Document workarounds for gaps in release notes
