# PyYAML Compatibility Layer Benchmark Results

## Executive Summary

The **libfyaml pyyaml_compat layer** provides a drop-in replacement for PyYAML that is:
- **5.6x faster** than PyYAML with libyaml C backend
- **68x faster** than PyYAML pure Python
- Uses **3x less memory** than PyYAML C backend

## Test Setup

- **Test File**: AtomicCards-2-cleaned-small.yaml (6.35 MB)
- **Platform**: Linux x86_64
- **Python**: 3.12
- **Iterations**: 3 runs per library (averaged)

## Results Summary

| Library | Parse Time | Memory | Throughput | vs Native | vs PyYAML C |
|---------|-----------|--------|------------|-----------|-------------|
| **libfyaml (native)** | **133 ms** | **34 MB** | **47.9 MB/s** | 1.0x | 20.7x faster |
| **libfyaml (pyyaml_compat)** | **487 ms** | **37 MB** | **13.1 MB/s** | 3.7x slower | **5.6x faster** |
| PyYAML (C/libyaml) | 2.75 s | 110 MB | 2.3 MB/s | 20.7x slower | 1.0x |
| PyYAML (pure Python) | 33.16 s | 147 MB | 0.2 MB/s | 250x slower | 12.1x slower |
| ruamel.yaml | 54.10 s | 163 MB | 0.1 MB/s | 408x slower | 19.7x slower |

## Visual Comparison

```
Parse Time (lower is better, 6.35 MB file):
libfyaml native      ▏ 133 ms
libfyaml compat      ██ 487 ms (3.7x native)
PyYAML (C/libyaml)   █████████████████████████████████ 2.75 s (5.6x compat)
PyYAML (pure Python) ████████████████████████████████████████████ 33.16 s
ruamel.yaml          █████████████████████████████████████████████████████████ 54.10 s

Memory Usage (lower is better):
libfyaml native      ██ 34 MB
libfyaml compat      ██ 37 MB
PyYAML (C/libyaml)   ███████████ 110 MB (3x more)
PyYAML (pure Python) ███████████████ 147 MB
ruamel.yaml          ████████████████ 163 MB
```

## Compat Layer Overhead Analysis

The pyyaml_compat layer has approximately **267% time overhead** vs native libfyaml, primarily due to:

1. **Python object conversion** (`to_python()`) - Converting FyGeneric to native Python dicts/lists
2. **Constructor checking** - Examining registered YAML constructors
3. **Stream normalization** - Handling edge cases for PyYAML compatibility

**Memory overhead** is only **9.7%** (37 MB vs 34 MB).

## Large File Test (109 MB)

| Library | Parse Time | Memory | Notes |
|---------|-----------|--------|-------|
| libfyaml native | 2.73 s | 558 MB | 40 MB/s throughput |
| libfyaml compat | 9.43 s | 612 MB | 11.6 MB/s throughput |
| PyYAML | **FAILED** | - | Scanner error at line 197285 |

libfyaml successfully parses files that crash PyYAML.

## When to Use Each Layer

### Use libfyaml native API when:
- Maximum performance is required
- You control the codebase and can use FyGeneric directly
- Processing very large files
- Memory efficiency is critical

### Use pyyaml_compat when:
- Drop-in replacement for existing PyYAML code
- Using libraries that depend on PyYAML (mkdocs, Ansible, etc.)
- Still want 5-68x speedup over PyYAML
- Need better error recovery than PyYAML

### Use PyYAML when:
- Maximum ecosystem compatibility is required
- Files are tiny (<10 KB) and performance doesn't matter
- Pure Python with no C dependencies is mandatory

## Migration Guide

Replace:
```python
import yaml
data = yaml.safe_load(stream)
```

With:
```python
from libfyaml import pyyaml_compat as yaml
data = yaml.safe_load(stream)  # 5.6x faster!
```

Or for system-wide replacement:
```python
import sys
from libfyaml import pyyaml_compat
sys.modules['yaml'] = pyyaml_compat
```

## Compatibility Status

| Feature | Status |
|---------|--------|
| `safe_load()` / `safe_dump()` | ✅ Full |
| `load()` / `dump()` | ✅ Full |
| `load_all()` / `dump_all()` | ✅ Full |
| Custom constructors (`add_constructor()`) | ✅ Full |
| Loader classes (SafeLoader, etc.) | ✅ Full |
| Node classes (ScalarNode, etc.) | ✅ Compatible |
| `compose()` / `compose_all()` | ✅ Full |
| Mark/line numbers | ⚠️ Partial (start_mark only) |
| Custom representers | ✅ Full |

## Benchmark Date

January 16, 2026

## Conclusion

The pyyaml_compat layer provides an excellent balance:
- **5.6-68x faster** than PyYAML (depending on backend)
- **3x less memory** than PyYAML C
- **Drop-in compatible** with most PyYAML usage
- **Handles files** that crash PyYAML
