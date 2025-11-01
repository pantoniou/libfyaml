# Bracket-Aware Configuration Parsing

## Overview

All libfyaml allocators now support **bracket-aware parsing** at the tokenization level. This allows ANY parameter value to use brackets `[...]` to protect commas and other delimiters from being interpreted as top-level separators.

## Implementation Location

**Shared Parsing Utilities:**
- Header: `src/allocator/fy-allocator-parse-util.h`
- Implementation: `src/allocator/fy-allocator-parse-util.c`

**Key Functions:**

### `fy_strtok_bracket_r()`
Bracket-aware tokenizer that replaces `strtok_r()`:
- Splits on delimiters (typically commas)
- Tracks bracket depth to avoid splitting inside brackets
- Handles nested brackets properly
- Returns tokens one at a time like `strtok_r()`

### `fy_extract_bracketed_value()`
Extracts content from bracketed values:
- Takes `"[content]"` and returns `"content"`
- Uses depth tracking for nested brackets
- Validates bracket matching
- Returns NULL if no brackets (caller uses value as-is)

## Usage Across All Allocators

All allocators now use `fy_strtok_bracket_r()` instead of `strtok_r()`:

- ✅ `fy-allocator-linear.c` - Line 395
- ✅ `fy-allocator-mremap.c` - Line 1020
- ✅ `fy-allocator-auto.c` - Line 442
- ✅ `fy-allocator-dedup.c` - Line 1093

Additionally, dedup uses `fy_extract_bracketed_value()` for parent config extraction (Line 1126).

## Why Bracket-Aware Parsing?

### Problem: Tokenization Conflict

Configuration strings use commas as delimiters between parameters:
```
allocator:param1=value1,param2=value2,param3=value3
```

But what if a parameter value itself contains commas?
```
dedup:parent=mremap:a=4M,b=1.5,threshold=32
                       ^     ^
                   These commas should be part of parent config,
                   NOT top-level separators!
```

### Solution: Bracket Protection

Wrap complex parameter values in brackets:
```
dedup:parent=[mremap:a=4M,b=1.5],threshold=32
             ^                  ^
          Brackets protect the commas inside
```

The bracket-aware tokenizer sees:
- Token 1: `parent=[mremap:a=4M,b=1.5]`
- Token 2: `threshold=32`

Then `fy_extract_bracketed_value()` unwraps:
- `[mremap:a=4M,b=1.5]` → `mremap:a=4M,b=1.5`

## Examples by Allocator

### 1. Linear Allocator

**Simple (no brackets needed):**
```bash
--allocator=linear:size=16M
```

**With brackets (works but not necessary):**
```bash
--allocator=linear:size=[16M]
```
The bracket is extracted and `size` parameter gets `16M`.

---

### 2. Mremap Allocator

**Multiple parameters WITHOUT brackets:**
```bash
# This works because no parameter values contain commas
--allocator=mremap:minimum_arena_size=4M,grow_ratio=1.5
```

**With brackets (if values had commas):**
```bash
# Hypothetical: if grow_ratio accepted a list like "1.5,2.0,3.0"
--allocator=mremap:minimum_arena_size=4M,grow_ratio=[1.5,2.0,3.0]
                                                    ^            ^
                                          Brackets protect internal commas
```

---

### 3. Dedup Allocator with Parent Config

**Parent with multiple parameters - REQUIRES brackets:**
```bash
--allocator=dedup:parent=[mremap:minimum_arena_size=4M,grow_ratio=1.5],threshold=64
                         ^                                             ^
                    Brackets protect parent's internal commas
```

**Parse flow:**
1. `fy_strtok_bracket_r` splits on commas at depth 0:
   - Token 1: `parent=[mremap:minimum_arena_size=4M,grow_ratio=1.5]`
   - Token 2: `threshold=64`

2. For the `parent` parameter, `fy_extract_bracketed_value` extracts:
   - Input: `[mremap:minimum_arena_size=4M,grow_ratio=1.5]`
   - Output: `mremap:minimum_arena_size=4M,grow_ratio=1.5`

3. Parent config is recursively parsed:
   - `fy_strtok_bracket_r` splits parent config:
     - Token 1: `minimum_arena_size=4M`
     - Token 2: `grow_ratio=1.5`

---

### 4. Nested Dedup Allocators

**Double nesting:**
```bash
--allocator=dedup:parent=[dedup:parent=[linear:size=16M],threshold=32],threshold=64
```

**Parse flow:**
1. **Outer dedup** tokenizes:
   - Token 1: `parent=[dedup:parent=[linear:size=16M],threshold=32]`
   - Token 2: `threshold=64`

2. **Outer dedup** extracts parent config:
   - Input: `[dedup:parent=[linear:size=16M],threshold=32]`
   - Output: `dedup:parent=[linear:size=16M],threshold=32`

3. **Inner dedup** (recursively) tokenizes:
   - Token 1: `parent=[linear:size=16M]`
   - Token 2: `threshold=32`

4. **Inner dedup** extracts parent config:
   - Input: `[linear:size=16M]`
   - Output: `linear:size=16M`

5. **Linear** (recursively) tokenizes:
   - Token 1: `size=16M`

**Result:** Outer dedup → Inner dedup → Linear allocator chain

---

### 5. Complex Nested Example

**Triple nesting with multiple parameters:**
```bash
--allocator=dedup:parent=[dedup:parent=[mremap:minimum_arena_size=4M,grow_ratio=1.5],threshold=32],threshold=64
```

**Bracket depth tracking visualization:**
```
Config: dedup:parent=[dedup:parent=[mremap:minimum_arena_size=4M,grow_ratio=1.5],threshold=32],threshold=64
Depth:  00000000000001111111111111122222222222222222222222222222222222222211111111111111110000000000000
                     ^              ^                                            ^              ^
               Depth 1 start   Depth 2 start                                Depth 2 end   Depth 1 end
```

**Top-level tokenization splits at depth 0:**
- Token 1: `parent=[dedup:parent=[mremap:minimum_arena_size=4M,grow_ratio=1.5],threshold=32]`
- Token 2: `threshold=64`

The commas at depth 1 and 2 are protected from tokenization.

---

## Bracket Depth Tracking Algorithm

The `fy_strtok_bracket_r()` function uses a depth counter:

```c
for (p = str; *p; p++) {
    if (*p == '[') {
        depth++;
    } else if (*p == ']') {
        depth--;
    } else if (depth == 0 && strchr(delim, *p)) {
        /* Found delimiter at bracket depth 0 - split here */
        *p = '\0';
        return token_start;
    }
}
```

**Key insight:** Only split on delimiters when `depth == 0`.

## Error Detection

### Unmatched Opening Bracket
```bash
--allocator=dedup:parent=[mremap:size=4M
```
**Error:** `"Unmatched opening bracket in config"`

### Unmatched Closing Bracket
```bash
--allocator=dedup:parent=[mremap:size=4M]]
```
**Error:** `"Unmatched brackets in config"`

### Extra Content After Closing Bracket
```bash
--allocator=dedup:parent=[mremap:size=4M]extra
```
**Error:** `"Unmatched brackets in config"`

---

## Benefits

### ✅ Universal Bracket Support
- ANY allocator can use brackets for ANY parameter
- No allocator-specific handling needed
- Consistent behavior across all allocators

### ✅ Recursive Parent Configs
- Parent allocators can have complex multi-parameter configs
- Nested allocators work seamlessly
- Unlimited nesting depth

### ✅ Robust Error Handling
- Detects unmatched brackets
- Clear error messages
- Fails fast on malformed configs

### ✅ Backward Compatible
- Parameters without brackets work as before
- No breaking changes to existing configs
- Brackets are optional (only needed for complex values)

---

## Code Changes Summary

### Added to Parse Utilities

**`src/allocator/fy-allocator-parse-util.h`:**
- Added `fy_strtok_bracket_r()` declaration
- Added `fy_extract_bracketed_value()` declaration

**`src/allocator/fy-allocator-parse-util.c`:**
- Implemented `fy_strtok_bracket_r()` (bracket-aware tokenizer)
- Implemented `fy_extract_bracketed_value()` (bracket content extraction)

### Updated Allocators

**All allocators updated:**
- Replaced `strtok_r()` with `fy_strtok_bracket_r()`
- Comment updated to indicate "bracket-aware" parsing

**Dedup allocator specifically:**
- Removed local `extract_bracketed_content()` function
- Uses shared `fy_extract_bracketed_value()` instead
- Parent config extraction now uses shared utility

---

## Future Enhancements

### Potential Additions:
1. **Quote support**: `param="value with , comma"` as alternative to brackets
2. **Escape sequences**: `param=value\,with\,escaped\,commas`
3. **Validation API**: Pre-validate config strings before parsing
4. **Config builder**: Programmatic config string builder with auto-bracketing

### Backward Compatibility:
All enhancements will maintain backward compatibility with existing bracket syntax.

---

## Related Documentation

- `docs/allocator-recursive-parsing-demo.md` - Recursive parent config parsing
- `docs/bracket-parsing-fix.md` - Original problem analysis (dedup-specific, now generalized)
- `docs/bracket-depth-tracking-tests.md` - Test cases for depth tracking (dedup-specific, now applies to all allocators)

---

## Summary

**Before:** Only dedup allocator could use brackets, and only for the parent parameter.

**After:** ALL allocators support brackets for ALL parameters via shared utilities.

This makes the parsing system more consistent, flexible, and robust across the entire allocator infrastructure.
