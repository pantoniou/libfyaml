# C Library Bug: Spurious Null Byte in Block Scalars with NEL (U+0085) Line Break

## Summary

When parsing in YAML 1.1 mode, block scalars whose trailing line break is a NEL
character (U+0085, `\xc2\x85` in UTF-8) produce a spurious null byte (`\0`) in the
scalar value. This affects both **clip** (default `|`) and **keep** (`|+`) chomping
modes. Strip (`|-`) is not affected because it removes the trailing line break entirely.

## Affected Tests

- PyYAML test suite: `spec-09-22.data` (YAML 1.1 block scalar chomping)
- PyYAML test suite: `spec-09-23.data` (YAML 1.1 block scalar chomping with comments)

## Reproduction

### Minimal Case

Input (hex): `78 3a 20 7c 0a 20 20 74 65 78 74 c2 85`

```yaml
x: |
  text<NEL>
```

where `<NEL>` is U+0085 (`\xc2\x85` in UTF-8).

```bash
# Using fy-tool in YAML 1.1 mode:
echo -ne 'x: |\n  text\xc2\x85' | fy-tool --yaml-1.1 --testsuite -
```

**Expected output:**
```
=VAL |text\n
```

**Actual output:**
```
=VAL |text\n\0
```

### All Line Break Types with Clip Chomping

| Line Break  | Input bytes        | Expected value | Actual value    | Status |
|-------------|-------------------|----------------|-----------------|--------|
| `\n`        | `0a`              | `text\n`       | `text\n`        | OK     |
| `\r`        | `0d`              | `text\n`       | `text\n`        | OK     |
| `\r\n`      | `0d 0a`           | `text\n`       | `text\n`        | OK     |
| NEL U+0085  | `c2 85`           | `text\n`       | `text\n\0`      | **BUG** |
| LS U+2028   | `e2 80 a8`        | `text\u2028`   | `text\u2028`    | OK     |
| PS U+2029   | `e2 80 a9`        | `text\u2029`   | `text\u2029`    | OK     |

Note: LS and PS are preserved as-is (not normalized to `\n`) — this matches PyYAML
behavior and is correct per YAML 1.1 spec (they are "line break" characters but their
content is preserved in block scalars).

### All Chomping Modes with NEL

| Chomping | Indicator | Expected value | Actual value    | Status |
|----------|-----------|----------------|-----------------|--------|
| Strip    | `\|-`     | `text`         | `text`          | OK     |
| Clip     | `\|`      | `text\n`       | `text\n\0`      | **BUG** |
| Keep     | `\|+`     | `text\n`       | `text\n\0`      | **BUG** |

Strip works correctly because it removes the trailing break entirely, avoiding the
code path that produces the null byte.

## Root Cause Analysis

The NEL character (U+0085) is a 2-byte sequence in UTF-8 (`\xc2\x85`). When the
parser normalizes NEL to `\n` for block scalar trailing content, it appears to write
a 1-byte `\n` but accounts for the original 2-byte input length, leaving a stale
null byte in the output buffer. This suggests a length/size mismatch in the line
break normalization code path within the block scalar scanner — likely in or around
`fy-parse.c` where block scalar content is assembled.

The bug is specific to:
1. YAML 1.1 mode (NEL is not a line break in YAML 1.2)
2. Block scalars (`|` literal and `>` folded)
3. Trailing line break position (not mid-content line breaks)
4. Clip or keep chomping (strip removes the break, avoiding the bug)

## PyYAML Reference Behavior

PyYAML (the reference YAML 1.1 implementation) correctly produces:
- `text\n` for clip chomping with NEL trailing break
- `text\n` for keep chomping with NEL trailing break
- `text` for strip chomping with NEL trailing break
