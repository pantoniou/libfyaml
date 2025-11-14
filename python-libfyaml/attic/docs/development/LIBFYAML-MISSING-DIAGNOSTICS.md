# libfyaml Missing Diagnostics

These YAML error cases raise `ValueError: Failed to parse YAML/JSON` without returning diagnostic information when using `collect_diag=True`.

## Error Cases Without Diagnostics

| # | Description | Input | Expected Error Type |
|---|-------------|-------|---------------------|
| 1 | unclosed brace | `{` | ParserError |
| 2 | unclosed bracket | `[` | ParserError |
| 3 | unclosed brace with content | `{a: 1` | ParserError |
| 4 | unclosed bracket with content | `[1, 2` | ParserError |
| 5 | bad indent after colon | `a:\n b` | ScannerError |
| 6 | bare colon | `:` | ParserError |
| 7 | nested dashes | `- - -` | ParserError |
| 8 | colon on new line | `key\n:` | ParserError |
| 9 | bare anchor | `&` | ScannerError |

## Test Code

```python
import libfyaml as fy

error_cases = [
    ('{', 'unclosed brace'),
    ('[', 'unclosed bracket'),
    ('{a: 1', 'unclosed brace with content'),
    ('[1, 2', 'unclosed bracket with content'),
    ('a:\n b', 'bad indent after colon'),
    (':', 'bare colon'),
    ('- - -', 'nested dashes'),
    ('key\n:', 'colon on new line'),
    ('&', 'bare anchor'),
]

for yaml_str, desc in error_cases:
    try:
        result = fy.loads(yaml_str, mode='yaml1.1', collect_diag=True)
        print(f'{desc}: parsed (unexpected)')
    except ValueError as e:
        print(f'{desc}: {e}')
```

## Cases That DO Return Diagnostics (Working)

These error cases correctly return diagnostic information:

| Description | Input | Diagnostic Message |
|-------------|-------|-------------------|
| tab in indentation | `---\nhosts: localhost\nvars:\n  foo: bar\n\tblip: baz` | `invalid indentation in mapping` |
| unexpected indent | `key: value\n  bad: indent` | `invalid multiline plain key` |
| leading indent | `  - item` | (parses as string) |
| double colon | `a: b: c` | (parses as `{a: "b: c"}`) |
| tab indent | `a:\n\tb: c` | `invalid indentation in mapping` |
| unclosed double quote | `"unclosed` | `unterminated double quoted string` |
| unclosed single quote | `'unclosed` | `unterminated single quoted string` |
| undefined alias | `*undefined` | `no definition for alias` |
| missing value in flow | `{a: 1, b}` | (parses as `{a: 1, b: null}`) |
| trailing comma | `[1, 2, ]` | (parses as `[1, 2, null]`) |

## Workaround

In `pyyaml_compat`, we catch `ValueError` and convert to `ParserError`:

```python
try:
    doc = fy.loads(stream, mode='yaml1.1', collect_diag=True)
except ValueError as e:
    raise ParserError(problem=str(e))
```

This provides PyYAML-compatible exceptions but without detailed line/column information.

## Desired Behavior

Ideally, libfyaml would return diagnostic info for all these cases, including:
- Line and column number where the error was detected
- Descriptive error message
- Content snippet showing the problem location
