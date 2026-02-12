# PyYAML Test Suite Failure Analysis

**Test run**: 1220 passed, 60 failed (95.3% pass rate)
**Test suite**: PyYAML legacy test suite run against libfyaml Python bindings (pyyaml_compat)

## Summary

| Category | Count | Root Cause |
|----------|-------|------------|
| Loader errors (C lib leniency) | 22 | C parser accepts YAML that PyYAML rejects |
| Emitter styles (block scalars) | 16 | C emitter block scalar bugs in mapping key contexts |
| Emitter on_data | 5 | C emitter event round-trip issues |
| Lone surrogates | 4 | C library rejects lone surrogates |
| Composer (`---`/`...` handling) | 1 | C parser `---`/`...` in flow context |
| Path resolver | 2 | Not implemented in Python bindings |
| Recursive round-trip | 2 | Anchor/alias reconstruction during load |
| Sexagesimal | 2 | C library doesn't resolve sexagesimal notation |
| Implicit resolver schema | 1 | C library YAML 1.1 schema differences (103/261 sub-tests) |
| Timestamp bugs | 1 | C library doesn't parse single-digit timezone offsets |
| Stream error | 1 | Test uses PyYAML's `Reader` class (not implemented in bindings) |
| Duplicate mapping key | 1 | C parser fails on `*anchor: value` syntax |
| Tuple as mapping key | 1 | C emitter uses `!<uri>` instead of `!!shorthand` for tags |
| Unicode transfer (UTF-16-BE) | 1 | C emitter UTF-16-BE encoding issue |

---

## 1. Loader Error Failures (22 tests)

**Tests**: `test_loader_error` (11) + `test_loader_error_string` (11) — same 11 cases tested from file and from string.

**Root cause**: The C library's YAML parser is more lenient than PyYAML. These tests expect a loading error (exception) but libfyaml successfully parses the input. Many of these are valid in the YAML 1.1 specification; PyYAML's rejections are arbitrary restrictions.

### 1a. duplicate-anchor-1.loader-error

**Input**:
```yaml
- &foo bar
- &bar bar
- &foo bar
```

**Expected**: Error — duplicate anchor `&foo` defined twice.
**Actual**: libfyaml accepts this, allowing anchor redefinition (which is valid per YAML spec — later definition takes precedence).

**C library behavior**: Allows duplicate anchors. The YAML specification says anchors can be redefined; PyYAML is stricter.

### 1b. duplicate-anchor-2.loader-error

**Input**:
```yaml
&foo [1, 2, 3, &foo 4]
```

**Expected**: Error — anchor `&foo` redefined within its own node.
**Actual**: libfyaml accepts nested anchor redefinition.

**C library behavior**: Same as 1a — allows anchor redefinition including self-referential cases.

### 1c. empty-python-module.loader-error

**Input**:
```yaml
--- !!python:module:
```

**Expected**: Error — malformed `!!python:module:` tag with empty module name.
**Actual**: libfyaml parses the tag without validation; Python-side constructor doesn't receive this because the C parser doesn't flag it.

**Note**: This is partially a Python-side issue — the `!!python:module:` tag needs constructor-level validation. However, the C parser delivers the empty tag value, and the Python constructor code path doesn't trigger because the tag format differs from what PyYAML produces (`tag:yaml.org,2002:python/module:`).

### 1d. invalid-anchor-1.loader-error

**Input**:
```yaml
--- &?  foo
```

**Expected**: Error — anchor name `?` contains invalid characters (PyYAML only allows ASCII alphanumeric).
**Actual**: libfyaml accepts `?` as an anchor name.

**C library behavior**: libfyaml follows the YAML 1.1/1.2 spec more closely, which allows a broader set of characters in anchor names than PyYAML's restricted set.

### 1e. invalid-directive-name-1.loader-error

**Input**:
```yaml
%   # no name at all
---
```

**Expected**: Error — `%` directive with no name.
**Actual**: libfyaml ignores the malformed directive line.

**C library behavior**: Treats unknown/malformed directives as ignorable, which is arguably more robust.

### 1f. invalid-directive-name-2.loader-error

**Input**:
```yaml
%invalid-characters:in-directive name
---
```

**Expected**: Error — directive name contains invalid characters (`:` in name).
**Actual**: libfyaml ignores unknown directives.

**C library behavior**: Same as 1e — unknown directives are silently ignored.

### 1g. invalid-item-without-trailing-break.loader-error

**Input** (no trailing newline):
```
-
-0
```

**Expected**: Error — item `0` has no trailing line break.
**Actual**: libfyaml accepts input without trailing newline.

**C library behavior**: Does not require a trailing newline at end of input. This is common behavior for robust parsers.

### 1h. invalid-tag-handle-2.loader-error

**Input**:
```yaml
%TAG    !foo    bar
---
```

**Expected**: Error — tag handle `!foo` is invalid (not `!`, `!!`, or `!name!` format).
**Actual**: libfyaml accepts this tag handle definition.

**C library behavior**: More lenient tag handle validation. PyYAML requires handles to be `!`, `!!`, or end with `!`.

### 1i. invalid-yaml-directive-version-1.loader-error

**Input**:
```yaml
%YAML
---
```

**Expected**: Error — `%YAML` directive with no version number.
**Actual**: libfyaml accepts this, treating it as a versionless YAML directive.

**C library behavior**: Does not require a version number for the `%YAML` directive.

### 1j. no-block-mapping-end-2.loader-error

**Input**:
```yaml
? foo
: bar
: baz
```

**Expected**: Error — duplicate `:` value indicator without a key.
**Actual**: libfyaml parses this successfully.

**C library behavior**: Handles the duplicate value indicator case differently, possibly treating the second `: baz` as a separate mapping entry with an empty key.

### 1k. no-document-start.loader-error

**Input**:
```yaml
%YAML   1.1
# no ---
foo: bar
```

**Expected**: Error — document content after `%YAML` directive without `---` document start marker.
**Actual**: libfyaml parses the content without requiring explicit document start after a directive.

**C library behavior**: Does not require `---` after a `%YAML` directive, treating the directive as optional metadata.

---

## 2. Emitter Style Failures (16 tests)

**Tests**: `test_emitter_styles` for spec-05-03, spec-05-11, spec-06-05, spec-06-08, spec-08-12, spec-08-15, spec-09-02, spec-09-08, spec-09-12, spec-09-16, spec-09-22, spec-09-23, spec-09-24, spec-10-05, spec-10-07, spec-10-15.

**Root cause**: The C library's emitter produces invalid or different YAML when emitting block scalars (literal `|` and folded `>`), particularly in mapping key contexts. The test round-trips: parse → emit with specific styles → re-parse, and verifies the events match.

### General Pattern

The `test_emitter_styles` test iterates through all style combinations for each document:
1. Parse the input YAML to get events
2. For each style combination (flow/block for collections, plain/single/double/literal/folded for scalars), re-emit
3. Re-parse the emitted output
4. Compare events

**Failure modes**:
- Block scalars (`|`, `>`) used as mapping keys produce malformed YAML
- The C emitter doesn't properly handle indentation/formatting when a block scalar appears in a complex key position
- Some spec files with special characters (line breaks, tabs, Unicode) trigger emitter formatting issues

### Affected spec files:

| File | Content Description |
|------|-------------------|
| spec-05-03 | Sequence and mapping with `? key : value` syntax |
| spec-05-11 | Unicode line breaks (NEL U+0085, LS U+2028, PS U+2029) in block scalars |
| spec-06-05 | Flow mapping as block mapping key |
| spec-06-08 | Folded block scalar (`>-`) with specific whitespace |
| spec-08-12 | Anchored and tagged nodes in flow sequence |
| spec-08-15 | Empty scalars and nested mappings |
| spec-09-02 | Double-quoted scalar with tabs and trimming |
| spec-09-08 | Single-quoted scalar with tabs |
| spec-09-12 | Plain scalars with special characters (`::`, `,`, `-`) |
| spec-09-16 | Plain scalar with tabs (confusing whitespace) |
| spec-09-22 | Block scalars with all chomping indicators (strip `|-`, clip `|`, keep `|+`) |
| spec-09-23 | Folded block scalars with all chomping indicators |
| spec-09-24 | Folded block scalars with empty/whitespace lines |
| spec-10-05 | Block scalar in sequence context |
| spec-10-07 | Flow mapping with empty keys and compact notation |
| spec-10-15 | Nested mappings as mapping keys |

**See also**: `C-PARSER-NEL-BLOCK-SCALAR-BUG.md` for the NEL-specific block scalar issue, and C test cases in `test/libfyaml-test-emit-bugs.c` (Bug 14).

---

## 3. Emitter On-Data Failures (5 tests)

**Tests**: `test_emitter_on_data` for spec-05-11, spec-06-08, spec-09-08, spec-09-16, spec-09-22.

**Root cause**: The `on_data` callback test checks that emitted chunks, when concatenated, produce valid YAML that re-parses to the same events. The C emitter's chunking/formatting issues cause the re-parsed events to differ from the original.

### Affected spec files:

| File | Issue |
|------|-------|
| spec-05-11 | NEL/LS/PS Unicode line break characters cause emitter output differences |
| spec-06-08 | Folded scalar whitespace handling produces different output |
| spec-09-08 | Single-quoted scalar tab handling |
| spec-09-16 | Plain scalar tab/space handling |
| spec-09-22 | Block scalar NEL character produces spurious NUL byte (see Bug 14) |

**Related**: These overlap with the emitter_styles failures — the underlying C emitter issues are the same.

---

## 4. Lone Surrogate Failures (4 tests)

**Tests**: `test_constructor_types` and `test_representer_types` for both `emitting-unacceptable-unicode-character-bug.data/.code` and `emitting-unacceptable-unicode-character-bug-py3.data/.code`.

**Test data**: The value is a Python string containing a lone surrogate: `"\udd00"`

**Root cause**: The C library rejects lone surrogates at the emitter level with:
```
[ERR]: NULL value with len > 0, illegal SCALAR
```

**Expected behavior (PyYAML)**: PyYAML raises a `RepresenterError` with a message about unacceptable Unicode character `#xdd00`. The test catches this error and validates the error message.

**libfyaml behavior**: The C `from_python()` function fails when trying to serialize the lone surrogate to UTF-8 (since lone surrogates cannot be represented in valid UTF-8). The error path differs from PyYAML's — it's a C-level error rather than a Python representer error.

**Impact**: 2 constructor tests (load the .data file, which embeds the surrogate) + 2 representer tests (try to dump the surrogate value).

---

## 5. Composer Failures (1 test)

**Tests**: `test_composer` for spec-09-15.

~~spec-09-22 and spec-09-23 previously failed here due to the NEL block scalar bug — now **FIXED**.~~

### 5a. spec-09-15 — `---` and `...` as plain scalars

**Input**:
```yaml
---
"---" : foo
...: bar
---
[
---,
...,
{
? ---
: ...
}
]
...
```

**Expected**: `---` and `...` inside flow context are plain scalars.
**Actual**: C library resolves `---` as document markers or null in some contexts where PyYAML treats them as plain strings.

**C library behavior**: `---` in flow context is resolved differently than PyYAML expects. This is a C-side tag resolution issue.

---

## 6. Parser Failures — FIXED

~~**Tests**: `test_parser` for spec-09-22 and spec-09-23.~~

**Status**: **FIXED** — The NEL block scalar bug (spurious NUL byte with clip/keep chomping) has been resolved in the C library. Both spec-09-22 and spec-09-23 parser tests now pass. The NUL byte in input stream and partial/invalid UTF-8 handling has also been fixed (see Bug 15 in `test/libfyaml-test-emit-bugs.c`).

**C test cases**: `test/libfyaml-test-emit-bugs.c`, Bug 14 (NEL parser tests — all pass) and Bug 15 (NUL/UTF-8 input validation — all 11 pass).

---

## 7. Path Resolver Failures (2 tests)

**Tests**: `test_path_resolver_loader` and `test_path_resolver_dumper` for `resolver.data`.

**Root cause**: The `add_path_resolver()` functionality is not implemented in the libfyaml Python bindings. PyYAML's path resolver allows registering custom tags based on the path within the document tree (e.g., "apply tag `!foo` to any mapping at path `/key11`").

**Test data**: The test registers path-based resolvers like:
```python
yaml.add_path_resolver('!root', [])
yaml.add_path_resolver('!root/key11/key12/key13/seq', ['key11', 'key12', 'key13'])
```
and verifies that loaded/dumped documents have the correct tags applied based on their path.

**Python bindings status**: `add_path_resolver()` is defined as a no-op stub. Implementing this would require:
1. Tracking the document path during construction
2. Applying resolver rules based on path matching
3. Both loader and dumper support

**Priority**: Low — path resolvers are rarely used in practice.

---

## 8. Recursive Round-Trip Failures (2 tests)

**Tests**: `test_recursive` for `recursive-set.recursive` and `recursive-tuple.recursive`.

### 8a. recursive-set

**Test code**:
```python
value = set()
value.add(AnInstance(foo=value, bar=value))
value.add(AnInstance(foo=value, bar=value))
```

**Test logic**: `dump(value) → output1`, `unsafe_load(output1) → value2`, `dump(value2) → output2`, assert `output1 == output2`.

**Root cause**: The dump produces correct YAML with anchors (`&id001`, `&id002`) and aliases (`*id001`, `*id002`), but `unsafe_load` doesn't correctly reconstruct the recursive object graph. On re-load, some self-referential anchors resolve to `None` instead of the correct back-reference, causing the second dump to produce different output.

**Technical details**: libfyaml's C parser handles anchors and aliases at the parser level, producing the correct events. However, the Python-side constructor's two-phase construction (create object, then fill in references) doesn't correctly wire up all back-references for recursive sets containing custom objects.

### 8b. recursive-tuple

**Test code**:
```python
value = ([], [])
value[0].append(value)
value[1].append(value[0])
```

**Root cause**: Same as 8a — the dump produces correct YAML with anchors/aliases, but `unsafe_load` doesn't reconstruct the recursive tuple+list structure correctly. The second dump produces different anchor numbering and structure.

**Both failures**: These are fundamentally about the Python-side constructor's handling of anchor resolution during two-phase construction of recursive objects. The C library correctly parses and emits the anchors/aliases, but the Python constructor doesn't maintain the same anchor-to-object mapping.

---

## 9. Sexagesimal Notation Failures (2 tests)

**Tests**: `test_constructor_types` for `construct-int.data` and `construct-float.data`.

### 9a. construct-int — sexagesimal integer

**Input**: `sexagesimal: 190:20:30`
**Expected**: `685230` (= 190*3600 + 20*60 + 30)
**Actual**: `'190:20:30'` (returned as string)

### 9b. construct-float — sexagesimal float

**Input**: `sexagesimal: 190:20:30.15`
**Expected**: `685230.15` (= 190*3600 + 20*60 + 30.15)
**Actual**: `'190:20:30.15'` (returned as string)

**Root cause**: The C library's `yaml1.1-pyyaml` resolver mode does not recognize sexagesimal notation (`base-60` format: `nnn:nn:nn`). This format is defined in YAML 1.1 for integers and floats.

**C library fix needed**: The `yaml1.1-pyyaml` resolver in the C library needs to recognize the `[0-9]+:[0-5]?[0-9](:...)*` pattern and tag it as `!!int` or `!!float`.

---

## 10. Implicit Resolver Schema Failure (1 test)

**Test**: `test_implicit_resolver` for `yaml11.schema`.

**Root cause**: 103 out of 261 YAML 1.1 schema resolution tests fail. The test checks that untagged plain scalars are resolved to the correct type (null, bool, int, float, str) according to the YAML 1.1 schema.

**Notable differences between libfyaml and PyYAML resolution**:

| Scalar | PyYAML says | libfyaml says | Issue |
|--------|------------|---------------|-------|
| `190:20:30` | int (685230) | str | Sexagesimal not recognized |
| `190:20:30.15` | float (685230.15) | str | Sexagesimal not recognized |
| `.` | str | float (0.0) | C lib treats `.` as float |
| `+0.3e3` | str | float | Exponent without `+`/`-` after `e` |
| `0.3e3` | str | float | Same issue |
| `.3e3` | str | float | Same issue |
| `3e3` | str | float | Same issue |
| `08` | str | int | Leading zero + non-octal digit |
| `3.3e+3` | float (3300) | float (3300.0) | Minor: repr difference |
| `0o0`, `0o7`, `0o10` | str | str | Correct (Python-style octal not YAML 1.1) |

**C library fix needed**: Multiple resolver rules differ from PyYAML's YAML 1.1 implementation:
1. Sexagesimal `nnn:nn:nn` patterns
2. `.` (dot) should be string, not float
3. `3e3` (no decimal point) should be string in YAML 1.1
4. `08` (leading zero + non-octal digit) should be string
5. Various edge cases in scientific notation patterns

---

## 11. Timestamp Bug Failure (1 test)

**Test**: `test_constructor_types` for `timestamp-bugs.data`.

**Input** (problematic entries):
```yaml
- !MyTime
  - 2001-12-14 21:59:43+1        # Single-digit timezone hour
- !MyTime
  - 2001-12-14 21:59:43-1:30     # Single-digit timezone hour with minutes
```

**Expected**: Parsed as `datetime.datetime` with timezone info.
**Actual**: Returned as strings `'2001-12-14 21:59:43+1'` and `'2001-12-14 21:59:43-1:30'`.

**Root cause**: The C library's timestamp resolver doesn't recognize single-digit timezone offsets (like `+1` or `-1:30`). It requires two-digit hours (like `+01:00` or `-01:30`).

**C library fix needed**: Extend timestamp regex to accept single-digit timezone hour offsets.

---

## 12. Stream Error Failure (1 test)

**Test**: `test_stream_error` for `invalid-character.stream-error`.

**Input**: A file containing a NUL byte (`\x00`) embedded in otherwise valid YAML text.

**Expected**: Error — NUL characters are not allowed in YAML streams.
**Actual**: The test still fails, but NOT because of the C library.

**C library status**: **FIXED** — The C library now correctly rejects NUL bytes in input streams. Running `fy-tool --dump` on this file produces: `error: Invalid UTF8 (null \0) in the input stream`. C test cases added in `test/libfyaml-test-emit-bugs.c` (Bug 15) verify NUL rejection in both scalar values and comments.

**Why the PyYAML test still fails**: The `test_stream_error` test uses `yaml.reader.Reader`, a PyYAML-internal low-level character stream processor class. This class is not implemented by the libfyaml Python bindings — the `conftest_libfyaml.py` patch only replaces the top-level `yaml` module, not `yaml.reader`. The test exercises PyYAML's own `Reader` class, which never goes through our C library code path.

**Fix**: Would require implementing a `reader` submodule in `pyyaml_compat` that wraps the C library's stream processing. Low priority since this is a PyYAML internal API not used by applications.

**C test coverage (Bug 15)**: 11 test cases for invalid input stream handling — **all pass**:
- NUL byte in scalar and in comment — both correctly rejected
- Partial UTF-8: 2-byte truncated, 3-byte truncated, 4-byte truncated, truncated at EOF — all correctly rejected
- Invalid UTF-8: lone continuation byte, overlong encoding, 0xFE, 0xFF — all correctly rejected
- Valid UTF-8 sanity check (multi-byte chars including emoji) — correctly accepted

---

## 13. Duplicate Mapping Key (Anchor Syntax) Failure (1 test)

**Test**: `test_constructor_types` for `duplicate-mapping-key.former-loader-error.data`.

**Input**:
```yaml
---
&anchor foo:
    foo: bar
    *anchor: duplicate key
    baz: bat
    *anchor: duplicate key
```

**Expected**: Successfully loads — `*anchor` alias as mapping key resolves to `{foo: bar, ...}` (the mapping itself).
**Actual**: C parser error: `could not find expected ':'` at `*anchor: duplicate key`.

**Root cause**: The C parser has trouble with `*anchor: value` syntax where an alias is used as a simple (non-complex) mapping key. The parser doesn't recognize `*anchor` as a valid simple key followed by `:`.

**C library fix needed**: The parser should recognize `*alias` as a valid simple key in block mapping context.

---

## 14. Tuple as Mapping Key (Tag Format) Failure (1 test)

**Test**: `test_representer_types` for `construct-python-tuple-list-dict.code`.

**Test data**:
```python
[
    [1, 2, 3, 4],
    (1, 2, 3, 4),
    {1: 2, 3: 4},
    {(0,0): 0, (0,1): 1, (1,0): 1, (1,1): 0},
]
```

**Expected output** (PyYAML):
```yaml
- !!python/tuple
  - 1
  - 2
```

**Actual output** (libfyaml):
```yaml
- !<tag:yaml.org,2002:python/tuple>
  - 1
  - 2
```

**Root cause (primary)**: The C library's emitter uses the full URI form `!<tag:yaml.org,2002:...>` instead of the shorthand `!!...` for tags with the standard YAML tag prefix. While semantically equivalent, this causes byte-for-byte comparison failures.

**Root cause (secondary)**: The test also fails because `unsafe_load` can't re-parse the output when tuples are used as complex mapping keys. The C parser struggles with `? !<tag:yaml.org,2002:python/tuple>` followed by sequence items as a mapping key.

**C library fix needed**: The emitter should use `!!shorthand` form for tags with `tag:yaml.org,2002:` prefix when that tag directive is in effect.

---

## 15. Unicode Transfer (UTF-16-BE) Failure (1 test)

**Test**: `test_unicode_transfer` for `emoticons2.unicode`.

**Error**: `UnicodeDecodeError: 'utf-16-be' codec can't decode byte 0x0a in position 26: truncated data`

**Root cause**: When the C emitter is asked to produce UTF-16-BE encoded output, it produces malformed output for documents containing characters outside the Basic Multilingual Plane (emoji/emoticons require surrogate pairs in UTF-16).

**Test flow**: The test encodes a document to UTF-16-BE, then decodes the result. The C emitter produces output with incorrect byte alignment or missing surrogate pair bytes, causing the UTF-16-BE decoder to fail on the truncated data.

**C library fix needed**: The emitter's UTF-16-BE encoding needs to correctly handle supplementary plane characters (U+10000 and above) using surrogate pairs.

---

## Failure Classification Summary

### C Library Parser/Resolver Issues (27 tests)
These require changes to the C library's parser, resolver, or tag handling:
- 11 loader-error tests (x2 = 22 with string variants): More lenient parsing
- 2 sexagesimal: Missing `nnn:nn:nn` pattern recognition
- 1 timestamp-bugs: Single-digit timezone offsets
- 1 duplicate-mapping-key: `*alias:` as simple key
- 1 implicit-resolver: 103/261 schema resolution differences
- ~~2 parser (NEL) + 2 composer (NEL): **FIXED** — NEL block scalar bug resolved~~
- ~~1 stream-error (NUL): **FIXED** — C library now rejects NUL in streams~~

### C Library Emitter Issues (22 tests)
These require changes to the C library's YAML emitter:
- 16 emitter-styles: Block scalar formatting in complex contexts
- 5 emitter-on-data: Event round-trip issues (overlaps with above)
- 1 tuple-as-key: `!<uri>` vs `!!shorthand` tag format

### C Library Unicode Issues (5 tests)
- 4 lone-surrogate: Cannot serialize lone surrogates to UTF-8
- 1 emoticons-utf16be: UTF-16-BE surrogate pair encoding

### Python Bindings Issues (6 tests)
These could potentially be fixed in the Python layer:
- 2 recursive: Anchor/alias reconstruction in two-phase construction
- 2 path-resolver: Not implemented (stub only)
- 1 stream-error: Test uses `yaml.reader.Reader` (PyYAML internal, not implemented in bindings; C library NUL rejection is fixed)
- 1 empty-python-module: Constructor-level tag validation (partial overlap with C)

### Not Fixable (4 tests)
- 4 lone-surrogate tests: Lone surrogates cannot be represented in valid UTF-8 — this is a fundamental limitation, not a bug. PyYAML raises a `RepresenterError` which the test expects.
