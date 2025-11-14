# Enhanced libfyaml API Design

## Overview

Enhanced `load()` and `loads()` functions with options for performance tuning and direct Python conversion.

## API Signature

```python
def loads(
    yaml_string: str,
    *,
    mutable: bool = False,
    dedup: bool = True,
    input_kind: str = "auto"
) -> Union[FyGeneric, dict, list]:
    """
    Parse YAML/JSON string.

    Args:
        yaml_string: YAML/JSON content as string
        mutable: If True, return native Python dict/list instead of FyGeneric
        dedup: Enable deduplication allocator (default: True)
        input_kind: Input format - "auto", "yaml1.1", "yaml1.2", "json"

    Returns:
        FyGeneric if mutable=False, otherwise native Python dict/list

    Examples:
        # Default: Fast FyGeneric (lazy conversion)
        >>> data = libfyaml.loads("name: Alice")
        >>> type(data)
        <class 'libfyaml.FyGeneric'>

        # Mutable: Direct Python objects
        >>> data = libfyaml.loads("name: Alice", mutable=True)
        >>> type(data)
        <class 'dict'>

        # Disable dedup for simple files
        >>> data = libfyaml.loads(content, dedup=False)

        # Force JSON parsing
        >>> data = libfyaml.loads(content, input_kind="json")
    """
    pass


def load(
    file_or_path: Union[str, IO],
    *,
    mutable: bool = False,
    dedup: bool = True,
    input_kind: str = "auto"
) -> Union[FyGeneric, dict, list]:
    """
    Parse YAML/JSON from file.

    Args:
        file_or_path: File path (str) or file object
        mutable: If True, return native Python dict/list instead of FyGeneric
        dedup: Enable deduplication allocator (default: True)
        input_kind: Input format - "auto", "yaml1.1", "yaml1.2", "json"

    Returns:
        FyGeneric if mutable=False, otherwise native Python dict/list

    Examples:
        # Load immutable FyGeneric
        >>> data = libfyaml.load("config.yaml")

        # Load as mutable Python dict
        >>> data = libfyaml.load("config.yaml", mutable=True)
        >>> data["key"] = "value"  # Can mutate directly

        # YAML 1.1 mode (for compatibility)
        >>> data = libfyaml.load("legacy.yaml", input_kind="yaml1.1")
    """
    pass
```

## Parameters

### `mutable: bool = False`

**When False (default):**
- Returns `FyGeneric` (lazy, immutable)
- Fast parsing: ~118ms for 6.35MB
- Convert only what you access
- Memory efficient: inline storage for small values
- **Best for:** Read-only access, config files, large documents

**When True:**
- Returns native Python `dict`/`list`
- Parse + convert: ~419ms for 6.35MB (6.2x faster than PyYAML)
- Everything materialized upfront
- **Best for:** When you need standard Python mutability

### `dedup: bool = True`

**When True (default):**
- Uses deduplication allocator
- Shares repeated strings/values
- **Best for:** Large files with repeated content
- Example: YAML with many repeated keys saves ~30-50% memory

**When False:**
- Uses standard allocator
- Faster for small files without repetition
- **Best for:** Small files, unique content, when dedup overhead isn't worth it

### `input_kind: str = "auto"`

**Values:**
- `"auto"` (default): Auto-detect format
- `"yaml1.2"`: YAML 1.2 (current standard)
- `"yaml1.1"`: YAML 1.1 (legacy compatibility)
- `"json"`: Strict JSON parsing

**Best for:**
- `"auto"`: General use
- `"yaml1.2"`: When you know it's modern YAML
- `"yaml1.1"`: Legacy files (different boolean/number parsing)
- `"json"`: Strict JSON validation

## Performance Characteristics

### Parse to FyGeneric (default)

| Configuration | Time | Memory | Use Case |
|--------------|------|--------|----------|
| `dedup=True` | ~118ms | 34MB | Large files with repetition |
| `dedup=False` | ~100ms | 40MB | Small/unique files |

### Parse to Python (mutable=True)

| Configuration | Time | Memory | Use Case |
|--------------|------|--------|----------|
| `mutable=True, dedup=True` | ~419ms | 37MB | Need mutability, large files |
| `mutable=True, dedup=False` | ~380ms | 45MB | Need mutability, small files |

**Reference:** PyYAML (C/libyaml) takes ~2.6s, 110MB for same 6.35MB file.

## Cache-Friendly Design

The generic scheme uses **optimal packing** that makes conversion extremely fast:

1. **Inline storage**: Small values (ints ≤ 61 bits, strings ≤ 7 bytes, 32-bit floats) stored directly in 8-byte pointer
2. **Sequential layout**: Sequences/mappings stored as contiguous arrays
3. **CPU cache friendly**: Conversion traverses memory linearly, excellent cache locality
4. **Type-tagged pointers**: No indirection for type checks

This is why conversion is only 2.5x slower than parsing (~300ms vs ~118ms), rather than being a major bottleneck.

## Usage Recommendations

### Config Files (Read-Only)
```python
# Best: FyGeneric with dedup
config = libfyaml.load("config.yaml")
db_host = config["database"]["host"]
```

### Data Processing (Need Mutation)
```python
# Direct Python objects
data = libfyaml.load("data.yaml", mutable=True)
data["results"].append({"new": "item"})
```

### Small Files (< 1MB)
```python
# Disable dedup overhead
data = libfyaml.load("small.yaml", dedup=False)
```

### JSON-Only
```python
# Strict JSON parsing
api_response = libfyaml.loads(response, input_kind="json")
```

### Legacy YAML 1.1
```python
# YAML 1.1 compatibility
old_config = libfyaml.load("legacy.yaml", input_kind="yaml1.1")
```

## Implementation Notes

These options map to C library features:

- `mutable=True`: Parse to FyGeneric, immediately call `fy_generic_to_python()` recursively
- `dedup=True`: Set `FYPCF_DEFAULT_DOC` with dedup allocator via `fy_parse_cfg_set_dedup()`
- `input_kind`: Set `FYPCF_PARSE_YAML_1_1`, `FYPCF_PARSE_YAML_1_2`, or `FYPCF_PARSE_JSON` flags

## Backward Compatibility

Existing code works without changes:

```python
# Old code (still works)
data = libfyaml.load("config.yaml")

# New code (explicit options)
data = libfyaml.load("config.yaml", mutable=False, dedup=True, input_kind="auto")
```

Default behavior is unchanged: returns FyGeneric with dedup enabled.
