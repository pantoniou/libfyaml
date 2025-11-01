# Bracket Depth Tracking Test Cases

## Implementation Status

✅ **IMPLEMENTED**: Proper bracket depth tracking with `extract_bracketed_content()` in `fy-allocator-dedup.c`

## How It Works

The `extract_bracketed_content()` function uses a depth counter to track nested brackets:

```c
for (i = 0; i < len; i++) {
    if (value[i] == '[') {
        if (depth == 0) start = i;
        depth++;
    } else if (value[i] == ']') {
        depth--;
        if (depth == 0) {
            end = i;
            break;  /* Found matching bracket */
        }
    }
}
```

When depth returns to 0, we've found the matching closing bracket.

## Test Case 1: Simple Bracket (No Nesting)

**Input:** `parent=[linear:size=16M]`

**Parsing Flow:**
```
Position: 0 1 2 3 4 5 6 7 8 9 ...
Value:    [ l i n e a r : s i ...
Depth:    1 1 1 1 1 1 1 1 1 1 ... 1 1 1 0
          ^                               ^
        start=0                         end=18
```

**Result:** ✅ Extracts `"linear:size=16M"`

**Usage:**
```bash
fy-tool --allocator=dedup:parent=[linear:size=16M],dedup_threshold=32 input.yaml
```

Creates:
- Linear allocator with 16MB buffer
- Dedup wrapping it with 32-byte threshold

---

## Test Case 2: Nested Brackets (One Level Deep)

**Input:** `parent=[dedup:parent=[linear:size=16M],threshold=32]`

**Parsing Flow:**
```
Position: 0 1 2 3 4 5 6 7 8 9 ...14 15 16 17 ...
Value:    [ d e d u p : p a r e n  t  =  [  l  i  n  e  a  r ...
Depth:    1 1 1 1 1 1 1 1 1 1 1 1  1  1  2  2  2  2  2  2  2  ...
          ^                          inner bracket starts here ↑

Position: ... 29 30 31 32 33 34 35 36 ...  48 49 50
Value:    ... :  s  i  z  e  =  1  6  M  ]  ,  t  h  r  ...  2  ]
Depth:    ... 2  2  2  2  2  2  2  2  2  1  1  1  1  1  ...  1  0
                                       ↑                      ^
                            inner bracket ends here         end=50
```

**Result:** ✅ Extracts `"dedup:parent=[linear:size=16M],threshold=32"`

**Recursive Parsing:**
1. Outer dedup parses: `parent=[linear:size=16M],threshold=32`
2. Inner dedup (recursively) parses: `parent=linear:size=16M,threshold=32`
   - Bracket removed at inner level: `linear:size=16M`
3. Linear parses: `size=16M`

**Usage:**
```bash
fy-tool --allocator=dedup:parent=[dedup:parent=[linear:size=16M],threshold=32],threshold=64 input.yaml
```

Creates:
- Linear allocator (16MB)
- Inner dedup wrapping it (32-byte threshold)
- Outer dedup wrapping inner dedup (64-byte threshold)

---

## Test Case 3: Multiple Nested Brackets (Complex)

**Input:** `parent=[mremap:minimum_arena_size=[4M],grow_ratio=[1.5]]`

**Parsing Flow:**
```
Position: 0  ...  28 29 30 31 32 33 34 35 36 ... 48 49 50 51 52 53 54 55
Value:    [  ...  =  [  4  M  ]  ,  g  r  o  w  _  r  a  t  i  o  =  [
Depth:    1  ...  1  2  2  2  1  1  1  1  1  1  1  1  1  1  1  1  1  2
                     ^     ^                                            ^
                  depth 2   back to 1                              depth 2 again

Position: 55 56 57 58 59 60
Value:    [  1  .  5  ]  ]
Depth:    2  2  2  2  1  0
                     ^  ^
              back to 1  done
```

**Result:** ✅ Extracts `"mremap:minimum_arena_size=[4M],grow_ratio=[1.5]"`

**Note:** This syntax is unusual (brackets around scalar values), but the parser handles it correctly. More typical would be:

```bash
# More typical usage (no inner brackets needed for scalars):
parent=[mremap:minimum_arena_size=4M,grow_ratio=1.5]
```

**Usage:**
```bash
fy-tool --allocator=dedup:parent=[mremap:minimum_arena_size=4M,grow_ratio=1.5],threshold=64 input.yaml
```

Creates:
- Mremap allocator with 4MB minimum arena size and 1.5 grow ratio
- Dedup wrapping it with 64-byte threshold

---

## Test Case 4: Unmatched Brackets (Error Detection)

### 4a. Missing Closing Bracket

**Input:** `parent=[mremap:size=4M`

**Parsing Flow:**
```
Position: 0 1 2 3 4 5 ... 17
Value:    [ m r e m a p ... M
Depth:    1 1 1 1 1 1 1 ... 1
          ^                 ^
        start=0           end of string but depth=1 (not 0!)
```

**Result:** ❌ Error: `"Unmatched brackets in config"`

**Validation:** `depth != 0` at end of string

---

### 4b. Extra Closing Bracket

**Input:** `parent=[mremap:size=4M]]`

**Parsing Flow:**
```
Position: 0 1 2 ... 17 18 19
Value:    [ m r e ... 4  M  ]  ]
Depth:    1 1 1 1 ... 1  1  0  -1
                            ^  ^
                          valid but end != len-1
```

**Result:** ❌ Error: `"Unmatched brackets in config"`

**Validation:** `end != len - 1` (extra content after matching bracket)

---

### 4c. Content After Closing Bracket

**Input:** `parent=[mremap:size=4M]extra`

**Parsing Flow:**
```
Position: 0 1 2 ... 17 18 19 20 21 22 23
Value:    [ m r e ... 4  M  ]  e  x  t  r  a
Depth:    1 1 1 1 ... 1  1  0
                            ^
                      end=18, but len=24
```

**Result:** ❌ Error: `"Unmatched brackets in config"`

**Validation:** `end != len - 1` (content after matching bracket)

---

## Test Case 5: Empty Brackets

**Input:** `parent=[]`

**Parsing Flow:**
```
Position: 0 1
Value:    [ ]
Depth:    1 0
          ^ ^
        start=0, end=1
```

**Result:** ✅ Extracts empty string `""`

**Note:** This would likely fail later during parent type parsing, but bracket extraction succeeds.

---

## Test Case 6: Real-World Complex Example

**Input:** `parent=[mremap:minimum_arena_size=4M,grow_ratio=1.5,empty_threshold=2M]`

**Parsing Flow:**
```
Position: 0 1 2 3 4 5 6 7 8 ... 71 72
Value:    [ m r e m a p : m i ... M  ]
Depth:    1 1 1 1 1 1 1 1 1 1 ... 1  0
          ^                         ^
        start=0                   end=72
```

**Result:** ✅ Extracts `"mremap:minimum_arena_size=4M,grow_ratio=1.5,empty_threshold=2M"`

**Usage:**
```bash
fy-tool --allocator=dedup:parent=[mremap:minimum_arena_size=4M,grow_ratio=1.5],threshold=64 input.yaml
```

**Without Brackets (Would Fail):**
```bash
# ❌ WRONG: Comma in parent config breaks top-level tokenization
fy-tool --allocator=dedup:parent=mremap:minimum_arena_size=4M,grow_ratio=1.5,threshold=64 input.yaml
#                                                              ^
#                    This comma would be treated as top-level separator!
#                    Parser would see: token1="parent=mremap:minimum_arena_size=4M"
#                                     token2="grow_ratio=1.5"  (ERROR!)
```

---

## Summary of Benefits

### ✅ Correctly Handles:
1. **Simple brackets**: `[linear:size=16M]`
2. **Nested brackets**: `[dedup:parent=[linear:size=16M]]`
3. **Multiple parameters**: `[mremap:a=4M,b=1.5]`
4. **Deep nesting**: `[a:[b:[c]]]`

### ✅ Properly Detects Errors:
1. **Unmatched opening**: `[mremap:size=4M`
2. **Unmatched closing**: `[mremap:size=4M]]`
3. **Extra content**: `[mremap:size=4M]extra`

### ✅ Protection from Tokenization Issues:
- Commas inside brackets are **protected** from top-level `strtok_r` splitting
- Allows parent allocators with multiple comma-separated parameters

---

## Code Location

**Implementation:** `src/allocator/fy-allocator-dedup.c`
- Function: `extract_bracketed_content()` (lines 1068-1105)
- Usage: In `fy_dedup_parse_cfg()` (line 1180)

**Documentation:**
- This file: `docs/bracket-depth-tracking-tests.md`
- Original problem analysis: `docs/bracket-parsing-fix.md`
- Recursive parsing overview: `docs/allocator-recursive-parsing-demo.md`
