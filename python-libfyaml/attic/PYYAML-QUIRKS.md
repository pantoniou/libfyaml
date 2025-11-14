# PyYAML Quirks: Deviations from YAML 1.1 Spec

This document catalogs all differences between PyYAML's behavior and the YAML 1.1 specification,
as well as differences between PyYAML and libfyaml's current `yaml1.1` mode.

The goal is to implement a `yaml1.1-pyyaml` mode in libfyaml's C library that matches PyYAML's
exact behavior, eliminating the need for Python-level normalization hacks.

## Summary

PyYAML implements a **subset** of YAML 1.1 with several non-standard extensions:
- Restricts booleans (no single-letter y/Y/n/N)
- Adds underscore separators and sexagesimal notation for numbers
- Requires sign in scientific exponent (`1.0e+3` yes, `1.0e3` no)
- Resolves timestamps to native types
- Accepts bare tagged block scalars without `---`
- Accepts empty/whitespace-only documents as null

---

## 1. Boolean Resolution

### PyYAML accepts (exact match, case-sensitive per variant):
```
yes Yes YES
no  No  NO
true True TRUE
false False FALSE
on  On  ON
off Off OFF
```

### PyYAML does NOT accept (but YAML 1.1 spec does):
```
y Y n N
```

### libfyaml yaml1.1 currently does:
Resolves ALL of the above including `y`/`Y`/`n`/`N` per spec.

### Fix needed:
Remove `y`/`Y`/`n`/`N` from boolean resolution in pyyaml mode.

### Mixed case NOT accepted by either:
```
yEs oN oFf nO  → string (both agree)
```

---

## 2. Integer Resolution

### 2a. Octal format (`0NNN`)

| Input | PyYAML | libfyaml | YAML 1.1 spec |
|-------|--------|----------|---------------|
| `052` | 42 (octal) | 52 (WRONG - treats as decimal) | 42 (octal) |
| `010` | 8 (octal) | 10 (WRONG) | 8 (octal) |
| `0777` | 511 (octal) | 777 (WRONG) | 511 (octal) |

**Fix needed**: libfyaml must treat `0NNN` as base-8 in pyyaml mode (it currently strips the leading zero).

### 2b. Binary format (`0b`)

| Input | PyYAML | libfyaml |
|-------|--------|----------|
| `0b101010` | 42 (int) | `'0b101010'` (string) |
| `0b_1010` | 10 (int) | `'0b_1010'` (string) |

**Fix needed**: Resolve `0b` prefix as binary integer.

### 2c. Underscore separators

| Input | PyYAML | libfyaml |
|-------|--------|----------|
| `1_000` | 1000 | `'1_000'` (string) |
| `0x_2A` | 42 | `'0x_2A'` (string) |
| `0_52` | 42 (octal with underscore) | `'0_52'` (string) |

**Fix needed**: Allow `_` as separator in all integer formats.

PyYAML regexp: `[-+]?0b[0-1_]+|[-+]?0[0-7_]+|[-+]?(?:0|[1-9][0-9_]*)|[-+]?0x[0-9a-fA-F_]+`

### 2d. Sexagesimal (base 60)

| Input | PyYAML | libfyaml |
|-------|--------|----------|
| `1:30` | 90 | `'1:30'` (string) |
| `190:20:30` | 685230 | `'190:20:30'` (string) |
| `1:30:00` | 5400 | `'1:30:00'` (string) |

Formula: `a:b:c` = a×3600 + b×60 + c

PyYAML regexp for sexagesimal int: `[-+]?[1-9][0-9_]*(?::[0-5]?[0-9])+`

**Fix needed**: Resolve sexagesimal integer notation.

### 2e. Python-style octal (`0oNNN`)

| Input | PyYAML | libfyaml |
|-------|--------|----------|
| `0o52` | `'0o52'` (string) | `'0o52'` (string) |

Both agree: `0o` prefix is NOT recognized in YAML 1.1.

### 2f. Already matching

| Input | PyYAML | libfyaml |
|-------|--------|----------|
| `42` | 42 | 42 |
| `+42` | 42 | 42 |
| `-42` | -42 | -42 |
| `0x2A` | 42 | 42 |
| `+0x2A` | 42 | 42 |
| `-0x2A` | -42 | -42 |

---

## 3. Float Resolution

### 3a. Dot-prefix (no integer part)

| Input | PyYAML | libfyaml |
|-------|--------|----------|
| `.5` | 0.5 (float) | `'.5'` (string) |
| `.0` | 0.0 (float) | `'.0'` (string) |

PyYAML regexp: `\.[0-9][0-9_]*(?:[eE][-+][0-9]+)?`

**Fix needed**: Resolve `.N` as float.

### 3b. Trailing dot (no fractional part)

| Input | PyYAML | libfyaml |
|-------|--------|----------|
| `1.` | 1.0 (float) | 1 (INTEGER - strips dot!) |
| `0.` | 0.0 (float) | 0 (INTEGER) |

PyYAML regexp: `[0-9][0-9_]*\.[0-9_]*` (fractional part `[0-9_]*` can be empty)

**Fix needed**: `N.` (trailing dot, no fractional digits) should be float, not integer.

### 3c. Scientific notation - sign requirement

| Input | PyYAML | libfyaml |
|-------|--------|----------|
| `1.0e+3` | 1000.0 (float) | 1000.0 (float) |
| `1.0e-3` | 0.001 (float) | 0.001 (float) |
| `1.0e3` | `'1.0e3'` (STRING!) | 1000.0 (float) |
| `1.0E3` | `'1.0E3'` (STRING!) | 1000.0 (float) |
| `1e+3` | `'1e+3'` (string) | 1000.0 (float) |
| `1e3` | `'1e3'` (string) | 1000.0 (float) |

PyYAML regexp requires `[eE][-+][0-9]+` - the sign is MANDATORY.
Also requires fractional part: `[0-9][0-9_]*\.[0-9_]*` before the exponent.

**Fix needed**: In pyyaml mode:
1. Require `[-+]` sign after `e`/`E`
2. Require fractional dot before exponent (reject `1e+3`)

### 3d. Underscore separators

| Input | PyYAML | libfyaml |
|-------|--------|----------|
| `1_000.5` | 1000.5 | `'1_000.5'` (string) |
| `1_000.5_0` | 1000.5 | `'1_000.5_0'` (string) |

**Fix needed**: Allow `_` as separator in float formats.

### 3e. Sexagesimal floats

| Input | PyYAML | libfyaml |
|-------|--------|----------|
| `190:20:30.15` | 685230.15 | `'190:20:30.15'` (string) |

PyYAML regexp: `[-+]?[0-9][0-9_]*(?::[0-5]?[0-9])+\.[0-9_]*`

**Fix needed**: Resolve sexagesimal float notation.

### 3f. NaN/Inf case sensitivity

| Input | PyYAML | libfyaml | Match? |
|-------|--------|----------|--------|
| `.nan` | nan | nan | ✓ |
| `.NaN` | nan | `'.NaN'` (string!) | ✗ |
| `.NAN` | nan | nan | ✓ |
| `.Nan` | `'.Nan'` (string) | nan | ✗ (reversed!) |
| `.inf` | inf | inf | ✓ |
| `.Inf` | inf | inf | ✓ |
| `.INF` | inf | inf | ✓ |
| `+.inf` | inf | inf | ✓ |
| `-.inf` | -inf | -inf | ✓ |

PyYAML accepts: `.nan`, `.NaN`, `.NAN` (but NOT `.Nan`)
PyYAML accepts: `.inf`, `.Inf`, `.INF`

libfyaml accepts: `.nan`, `.NAN`, `.Nan` (but NOT `.NaN` - different from PyYAML!)
libfyaml accepts: `.inf`, `.Inf`, `.INF`

**Fix needed**: Accept exactly `nan|NaN|NAN` for NaN (match PyYAML).

### 3g. Already matching

| Input | PyYAML | libfyaml |
|-------|--------|----------|
| `3.14` | 3.14 | 3.14 |
| `1.0` | 1.0 | 1.0 |

---

## 4. Timestamp Resolution

| Input | PyYAML | libfyaml |
|-------|--------|----------|
| `2024-01-01` | `datetime.date(2024, 1, 1)` | `'2024-01-01'` (string) |
| `2024-01-01 12:00:00` | `datetime.datetime(2024, 1, 1, 12, 0)` | `'2024-01-01 12:00:00'` (string) |
| `2024-01-01T12:00:00Z` | `datetime.datetime(...)` with tz | `'2024-01-01T12:00:00Z'` (string) |

PyYAML resolves timestamps to Python `datetime.date` / `datetime.datetime` objects.
libfyaml keeps them as strings.

**Fix needed**: Resolve ISO 8601 date/datetime patterns. The generic type system
would need to either:
- Return the timestamp string with a `tag:yaml.org,2002:timestamp` tag (let Python construct)
- Or add a timestamp type to the generic system

PyYAML timestamp regexp:
```
[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]                    # date only
|[0-9][0-9][0-9][0-9]-[0-9][0-9]?-[0-9][0-9]?                 # date part
 (?:[Tt]|[ \t]+)[0-9][0-9]?:[0-9][0-9]:[0-9][0-9]             # time
 (?:\.[0-9]*)?                                                  # fraction
 (?:[ \t]*(?:Z|[-+][0-9][0-9]?(?::[0-9][0-9])?))?              # timezone
```

---

## 5. Null Resolution

Both PyYAML and libfyaml agree on null:

| Input | Result |
|-------|--------|
| `~` | None |
| `null` | None |
| `Null` | None |
| `NULL` | None |
| (empty) | None |

No fix needed.

---

## 6. Merge Key (`<<`)

Both PyYAML and libfyaml handle merge keys correctly:
```yaml
base: &base
  a: 1
  b: 2
derived:
  <<: *base
  b: 99
  c: 3
# Result: {a: 1, b: 99, c: 3}
```

No fix needed.

---

## 7. Document Acceptance / Parsing Quirks

### 7a. Empty/whitespace-only input

| Input | PyYAML | libfyaml |
|-------|--------|----------|
| `""` (empty) | None | ValueError: "No documents found" |
| `"   "` (spaces) | None | ValueError: "No documents found" |
| `"\n\n"` (newlines) | None | ValueError: "No documents found" |

**Fix needed**: Accept empty/whitespace-only input as a null document.

### 7b. Tagged block scalar without `---`

```yaml
!!binary |
  aGVsbG8=
```

| Parser | Result |
|--------|--------|
| PyYAML | `b'hello'` (tag recognized, block scalar parsed) |
| libfyaml | Tag preserved but value is null |

Valid YAML requires `---` for this: `--- !!binary |\n  aGVsbG8=`

**Fix needed**: Accept bare tagged block scalars as if `---` were present.

### 7c. Block scalar clip chomping at EOF

When input lacks a trailing newline, the clip indicator (`|`) behavior differs:

| Input | PyYAML | libfyaml |
|-------|--------|----------|
| `"k: \|\n  line1\n  line2"` (no trailing NL) | `'line1\nline2'` | `'line1\nline2\n'` (adds NL!) |
| `"k: \|\n  line1\n  line2\n"` (with trailing NL) | `'line1\nline2\n'` | `'line1\nline2\n'` |

Strip (`|-`) and keep (`|+`) chomping agree between both parsers.

**Fix needed**: In pyyaml mode, clip chomping should NOT add trailing newline when input doesn't end with one.

---

## 8. Emit/Dump Differences (for reference)

These affect YAML output, not parsing:

| Feature | PyYAML | libfyaml |
|---------|--------|----------|
| Tag format | `!!binary` (short) | `!<tag:yaml.org,2002:binary>` (full URI) |
| Block scalar style | `\|` with indentation | Not supported in generic dump |
| Key ordering | Insertion order | Insertion order |
| Null representation | `null` | `null` |

---

## Complete PyYAML Float Regexp (for reference)

```python
re.compile(r'''
    ^(?:
        [-+]?(?:[0-9][0-9_]*)\.[0-9_]*(?:[eE][-+][0-9]+)?  # N.N or N.NeN
    |   \.[0-9][0-9_]*(?:[eE][-+][0-9]+)?                   # .N (no int part)
    |   [-+]?[0-9][0-9_]*(?::[0-5]?[0-9])+\.[0-9_]*         # sexagesimal float
    |   [-+]?\.(?:inf|Inf|INF)                               # infinity
    |   \.(?:nan|NaN|NAN)                                    # not-a-number
    )$
''', re.VERBOSE)
```

Key points:
- Fractional part `[0-9_]*` CAN be empty (trailing dot: `1.`)
- Dot-prefix `.N` requires at least one digit after dot
- Scientific exponent REQUIRES sign: `[eE][-+][0-9]+`
- No scientific notation without fractional dot (rejects `1e+3`)

## Complete PyYAML Integer Regexp (for reference)

```python
re.compile(r'''
    ^(?:
        [-+]?0b[0-1_]+                          # binary
    |   [-+]?0[0-7_]+                           # octal (0NNN, allows underscores)
    |   [-+]?(?:0|[1-9][0-9_]*)                 # decimal (allows underscores)
    |   [-+]?0x[0-9a-fA-F_]+                    # hexadecimal (allows underscores)
    |   [-+]?[1-9][0-9_]*(?::[0-5]?[0-9])+     # sexagesimal
    )$
''', re.VERBOSE)
```

## Complete PyYAML Boolean Regexp (for reference)

```python
re.compile(r'^(?:yes|Yes|YES|no|No|NO|true|True|TRUE|false|False|FALSE|on|On|ON|off|Off|OFF)$')
```

First chars: `yYnNtTfFoO` (note: `y`/`Y`/`n`/`N` trigger check but only match multi-char words)

---

## Implementation Priority

For a `yaml1.1-pyyaml` C library mode:

**High priority** (causes real-world breakage):
1. Boolean: Remove y/Y/n/N single-letter resolution
2. Octal: Fix `0NNN` to be actual base-8
3. Timestamp: Resolve to tagged value (let Python construct datetime)
4. Empty input: Accept as null document
5. Tagged block scalar: Accept without `---`
6. Block scalar clip chomping: Don't add trailing NL when input lacks one

**Medium priority** (uncommon in practice):
7. Integer underscore separators
8. Float underscore separators
9. Float dot-prefix (`.5`)
10. Float trailing-dot (`1.`)
11. Scientific notation: require sign after e/E
12. NaN case: accept `NaN` not `Nan`

**Low priority** (rare in real-world YAML):
13. Sexagesimal integers
14. Sexagesimal floats
15. Binary integers (`0b`)
16. Scientific without fractional dot (reject `1e+3`)

---

## Current Python-level Normalizations

With a `yaml1.1-pyyaml` C mode, only TWO normalizations remain in Python:

1. **Empty/whitespace input → None**: libfyaml raises "No documents found"
2. **Bare tagged block scalars**: Prepend `---` to make valid YAML

All other cases (`~`, `null`, `[]`, `{}`, `""`, `''`) are handled natively by libfyaml.

---

## Current Compatibility Status

Tested with pyyaml_compat vs PyYAML safe_load():

| Feature | Status | Notes |
|---------|--------|-------|
| Timestamps | ✓ OK | Python-level `_try_implicit_timestamp()` workaround |
| dot-prefix float (`.5`) | ✗ DIFF | libfyaml: string, PyYAML: 0.5 |
| trailing-dot float (`1.`) | ✗ DIFF | libfyaml: int 1, PyYAML: float 1.0 |
| sci notation no sign (`1.0e3`) | ✗ DIFF | libfyaml: 1000.0, PyYAML: string (reversed!) |
| underscore int (`1_000`) | ✗ DIFF | libfyaml: string, PyYAML: 1000 |
| binary int (`0b101010`) | ✗ DIFF | libfyaml: string, PyYAML: 42 |
| sexagesimal (`1:30`) | ✗ DIFF | libfyaml: string, PyYAML: 90 |
| NaN mixed case (`.NaN`) | ✗ DIFF | libfyaml: string, PyYAML: nan |
| Boolean y/Y/n/N | ✗ DIFF | libfyaml: bool, PyYAML: string (reversed!) |
| Octal (`052`) | ✗ DIFF | libfyaml: 52 (decimal), PyYAML: 42 (octal) |

**Note**: "Reversed" means libfyaml is MORE permissive than PyYAML - it accepts values PyYAML rejects.
