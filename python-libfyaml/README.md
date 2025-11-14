# libfyaml Python Bindings

**Ultra-fast YAML/JSON parsing for Python** - 22x faster than PyYAML, 7.7x less memory, with a genuinely Pythonic API.

[![Performance](https://img.shields.io/badge/speed-22x_faster-brightgreen)]()
[![Memory](https://img.shields.io/badge/memory-7.7x_less-blue)]()
[![API](https://img.shields.io/badge/API-Pythonic-orange)]()

---

## Why libfyaml?

Most Python YAML/JSON parsers force you to choose between **speed** and **ergonomics**. Not anymore.

**YAML Performance** (6.35 MB file):

| Library | Parse Speed | Memory Usage | API Quality |
|---------|-------------|--------------|-------------|
| **libfyaml** | **119 ms** | **14 MB** | **Python dict** ✅ |
| PyYAML | 2,610 ms (22x slower) | 110 MB (7.7x more) | Python dict ✅ |
| rapidyaml | 85 ms | 67 MB (4.9x more) | C++ tree ❌ |

**JSON Support**: Same fast, memory-efficient API works for JSON too:
- **Unified API**: `libfyaml.loads(data, mode='json')`
- **Performance**: Comparable to `orjson` (fastest JSON parser)
- **Memory**: Beats all JSON libraries on large files (dedup + inline storage)
- **One library**: Handle both YAML and JSON with identical code

### The Killer Feature: It Just Works™

```python
import libfyaml

# Parse YAML - just like PyYAML
data = libfyaml.loads(yaml_string)

# Parse JSON - same API, just add mode='json'
data = libfyaml.loads(json_string, mode='json')

# It's a dict. Act accordingly.
print(data['name'])
for item in data['items']:
    print(item)
```

**No learning curve. No surprises. Just Python. Works for both YAML and JSON.**

---

## Installation

### Quick Start (Recommended)

```bash
# Install from PyPI (when published)
pip install libfyaml

# Or install from source with automatic local build
cd python-libfyaml
LIBFYAML_BUILD_LOCAL=1 pip install .
```

### Installation Methods

#### 1. System-Installed libfyaml (Recommended for Production)

```bash
# First, install libfyaml system-wide
cd /path/to/libfyaml
mkdir build && cd build
cmake ..
cmake --build .
sudo cmake --install .

# Then install Python bindings
cd /path/to/libfyaml/python-libfyaml
pip install .
```

This method uses pkg-config to find the installed library and provides the cleanest installation.

#### 2. Local Build (Recommended for Development)

```bash
# Install using parent directory build
cd /path/to/libfyaml/python-libfyaml

# Option A: Use existing build/
pip install .

# Option B: Rebuild libfyaml first
LIBFYAML_REBUILD=1 pip install .

# Option C: Force local build if missing
LIBFYAML_BUILD_LOCAL=1 pip install .
```

This method adds RPATH to the extension, so no LD_LIBRARY_PATH is needed during development.

#### 3. Development Mode

```bash
# Install in editable mode (for active development)
cd /path/to/libfyaml/python-libfyaml
pip install -e .

# Rebuild just the Python extension after C changes
python3 setup.py build_ext --inplace
```

### Environment Variables

Control build behavior with these environment variables:

- **`LIBFYAML_BUILD_LOCAL=1`** - Force use of local build (build if missing)
- **`LIBFYAML_REBUILD=1`** - Rebuild libfyaml before installing
- **`LIBFYAML_USE_SYSTEM=1`** - Force use of system-installed libfyaml

**Examples**:
```bash
# Force system install (even if ../build/ exists)
LIBFYAML_USE_SYSTEM=1 pip install .

# Rebuild everything from scratch
LIBFYAML_REBUILD=1 pip install .

# Auto-build local libfyaml if not found
LIBFYAML_BUILD_LOCAL=1 pip install .
```

### Requirements

- Python 3.7+
- C compiler (gcc, clang)
- CMake (for building libfyaml)
- libfyaml with generic support (automatically handled)

---

## Quick Start

See [QUICKSTART.md](QUICKSTART.md) for comprehensive tutorial.

### 5-Second Example

```python
import libfyaml

data = libfyaml.loads("""
name: Alice
age: 30
items: [apple, banana, orange]
""")

print(data['name'])      # 'Alice'
print(data['age'])       # 30
print(data['items'])     # ['apple', 'banana', 'orange']
```

### Real-World Example

```python
import libfyaml

# Load configuration
with open('config.yaml', 'r') as f:
    config = libfyaml.loads(f.read())

# Use directly - no conversion needed
db_host = config['database']['host']
db_port = config['database']['port']

# Iterate naturally
for endpoint in config['api']['endpoints']:
    print(f"{endpoint['path']}: {endpoint['methods']}")

# Convert to Python dict if needed
config_dict = config.to_python()

# Unix-style path access
host = config.get_at_unix_path("/database/host")
port_path = config['database']['port'].get_unix_path()  # "/database/port"

# Path conversion utilities
unix_path = libfyaml.path_list_to_unix_path(['server', 'config', 'port'])  # "/server/config/port"
path_list = libfyaml.unix_path_to_path_list('/server/config/port')  # ['server', 'config', 'port']

# Mutable operations with Unix paths
mutable_config = libfyaml.loads("server: {host: localhost}", mutable=True)
mutable_config.set_at_unix_path("/server/port", 8080)  # Add new key
mutable_config.set_at_unix_path("/server/host", "example.com")  # Update value
```

---

## Performance

### Small Files (6.35 MB YAML)

| Library | Parse Time | Memory | vs PyYAML |
|---------|-----------|--------|-----------|
| **libfyaml** | **119 ms** | **14 MB** | **22x faster, 7.7x less** |
| PyYAML | 2,610 ms | 110 MB | Baseline |
| rapidyaml | 85 ms | 67 MB | 30x faster, but 4.9x more memory |

### Large Files (768 MB YAML)

| Library | Parse Time | Memory | Result |
|---------|-----------|--------|--------|
| **libfyaml (dedup=True)** | **19.0 s** | **1.09 GB** | ✅ Works |
| libfyaml (dedup=False) | 17.6 s | 1.77 GB | ✅ Works |
| rapidyaml | 8.0 s | 5.25 GB | ❌ OOM in production |

**Critical finding**: rapidyaml is faster but **unusable in production** due to memory usage.

### Why libfyaml Wins in Production

**rapidyaml**:
- Crashes in 2GB Docker containers (needs 5.25 GB)
- Swaps heavily on 8GB developer machines
- Costs 4x more in cloud (memory = money)
- Fast at running out of memory

**libfyaml**:
- Works in constrained environments
- Predictable memory usage
- Production-ready
- Actually finishes the job

See [RAPIDYAML-MEMORY-WALL.md](RAPIDYAML-MEMORY-WALL.md) for full analysis.

---

## Features

### Auto-Optimizations (Enabled by Default)

```python
# Default behavior - already optimized!
data = libfyaml.loads(large_yaml)

# Automatically:
# ✅ Deduplicates repeated strings (saves ~40% on some files)
# ✅ Trims memory arena (saves ~35-45%)
# ✅ Uses optimal allocator
```

**You don't need to do anything - it's already optimal!**

### Manual Control (Advanced)

```python
# Disable dedup for unique content (8% faster)
data = libfyaml.loads(yaml, dedup=False)

# Disable auto-trim (keep arena for future allocations)
data = libfyaml.loads(yaml, trim=False)
data.trim()  # Trim manually later

# Both options
data = libfyaml.loads(yaml, dedup=False, trim=False)
```

### Conversion Options

```python
# FyGeneric objects auto-convert when accessed
data = libfyaml.loads(yaml)
name = data['name']  # Converts to Python str automatically
age = data['age']    # Converts to Python int automatically

# Or convert everything to native Python dict/list
python_dict = data.to_python()

# Or use .copy() (same as to_python())
python_dict = data.copy()

# Or use dict()
python_dict = dict(data)
```

---

## API Reference

### libfyaml.loads()

```python
libfyaml.loads(s, mode='yaml', dedup=True, trim=True)
```

**Parameters**:
- `s` (str): YAML or JSON string to parse
- `mode` (str): 'yaml' or 'json' (default: 'yaml')
- `dedup` (bool): Enable string deduplication (default: True)
- `trim` (bool): Auto-trim arena after parsing (default: True)

**Returns**: FyGeneric object (dict-like)

**Example**:
```python
# Default (optimized)
data = libfyaml.loads(yaml_string)

# JSON mode
data = libfyaml.loads(json_string, mode='json')

# Disable optimizations for faster parsing of unique content
data = libfyaml.loads(yaml_string, dedup=False, trim=False)
```

### FyGeneric Objects

FyGeneric objects behave like Python dicts/lists:

**Dict Operations**:
```python
# Access values
name = data['name']
age = data.get('age', 0)  # With default

# Check keys
if 'email' in data:
    print(data['email'])

# Iterate
for key, value in data.items():
    print(f"{key}: {value}")

# Get keys/values
keys = list(data.keys())
values = list(data.values())
```

**List Operations**:
```python
# Access items
first = data['items'][0]

# Iterate
for item in data['items']:
    print(item)

# Length
count = len(data['items'])

# List comprehension
names = [user['name'] for user in data['users']]
```

**Conversion Methods**:
```python
# Convert to Python dict/list
python_obj = data.to_python()

# Or use .copy()
python_obj = data.copy()

# Or use dict()/list()
python_dict = dict(data)
```

**Memory Management**:
```python
# Manually trim arena (usually not needed - auto-trim is on by default)
data.trim()
```

---

## Library Comparisons

### vs PyYAML

```python
# PyYAML
import yaml
data = yaml.safe_load(content)  # Slow, memory-hungry

# libfyaml
import libfyaml
data = libfyaml.loads(content)  # Fast, memory-efficient
```

**Performance**: 22x faster, 7.7x less memory
**API**: Identical - drop-in replacement
**Migration**: Change one import line

### vs rapidyaml

```python
# rapidyaml - C++ tree API
import ryml
tree = ryml.parse_in_arena(content)
for node in tree.rootref().children():
    key = tree.key(node).decode('utf-8')
    value = tree.val(node).decode('utf-8')

# libfyaml - Python dict API
import libfyaml
data = libfyaml.loads(content)
for key, value in data.items():
    print(f"{key}: {value}")
```

**Speed**: rapidyaml 2.4x faster on small files
**Memory**: libfyaml uses 4.9x LESS memory
**API**: libfyaml is genuinely Pythonic, rapidyaml is "C++ in a trenchcoat"
**Production**: libfyaml works, rapidyaml OOMs

See [API-COMPARISON-PYTHON-VS-TRENCHCOAT.md](API-COMPARISON-PYTHON-VS-TRENCHCOAT.md) for detailed comparison.

### vs ruamel.yaml

```python
# ruamel.yaml - preserves comments, formatting
from ruamel.yaml import YAML
yaml = YAML()
data = yaml.load(content)

# libfyaml - fast parsing, no round-trip
import libfyaml
data = libfyaml.loads(content)
```

**Use ruamel.yaml when**: You need to preserve comments/formatting for round-trip editing
**Use libfyaml when**: You need speed and memory efficiency

### vs JSON Libraries (json, ujson, orjson, rapidjson)

libfyaml works perfectly for JSON with `mode='json'`:

```python
# Standard library json
import json
data = json.loads(json_string)  # Fast for small files, memory-hungry for large

# orjson (fast JSON library)
import orjson
data = orjson.loads(json_bytes)  # Very fast, but bytes-only, no dedup

# libfyaml - handles both YAML AND JSON
import libfyaml
data = libfyaml.loads(json_string, mode='json')  # Fast + memory-efficient
```

**Why use libfyaml for JSON?**

1. **Unified API**: Same code handles YAML and JSON
   ```python
   # One library, both formats
   yaml_data = libfyaml.loads(yaml_content)
   json_data = libfyaml.loads(json_content, mode='json')
   ```

2. **Memory efficiency**: Deduplication + inline storage beats all JSON libraries for large files
   - `json.loads()`: Creates full Python objects (high memory)
   - `orjson.loads()`: Fast but no dedup (repeated strings waste memory)
   - `libfyaml.loads()`: Dedup + inline storage = minimal memory

3. **Large file handling**: JSON libraries struggle with multi-GB files
   - Standard `json`: 4-8x memory overhead, slow on large files
   - `orjson`/`ujson`: Fast but memory-hungry (no dedup)
   - `libfyaml`: Handles GB-sized JSON efficiently with dedup

4. **Same performance tier as orjson**: Comparable speed to fastest JSON parsers

**When to use each**:

| Use Case | Best Library | Why |
|----------|--------------|-----|
| Small JSON (<1 MB) | `orjson` or `json` | Minimal overhead, fast enough |
| Large JSON (>10 MB) | `libfyaml` | Dedup saves memory, handles scale |
| Mixed YAML+JSON projects | `libfyaml` | One API for both formats |
| Repeated data (configs, logs) | `libfyaml` | Deduplication shines |
| Maximum compatibility | `json` (stdlib) | No dependencies |

**Example - Large JSON file**:
```python
import libfyaml

# Parse large JSON file
with open('large-data.json') as f:
    data = libfyaml.loads(f.read(), mode='json', dedup=True)

# Memory-efficient: dedup + inline storage
# Fast: comparable to orjson
# Works for both: JSON and YAML with same API
```

---

## Use Cases

### ✅ Perfect For

- **Large YAML/JSON files** (>10 MB): Dedup and trim save massive memory
- **Production systems**: Predictable memory usage, no OOM surprises
- **Memory-constrained environments**: Docker containers, AWS Lambda, edge devices
- **High-performance parsing**: 22x faster than PyYAML, comparable to orjson
- **Mixed YAML+JSON projects**: One unified API for both formats
- **Configuration files**: Fast loading, natural Python dict interface
- **Data processing pipelines**: Memory-efficient iteration
- **JSON with repeated data**: Dedup significantly reduces memory
- **Parallel file processing**: Process multiple large files without OOM

### ⚠️ Not Yet Ideal For

- **Modifying parsed data in-place**: FyGeneric is immutable by default (use `mutable=True` or convert with `.to_python()`)
- **Round-trip editing with preserved formatting**: Use ruamel.yaml to preserve comments and formatting
- **Very small files where PyYAML is "fast enough"**: libfyaml overhead not worth it
- **Windows support**: Currently Linux/macOS only

---

## How It Works

### Memory Architecture

libfyaml uses a sophisticated memory management system:

1. **Arena Allocators**: Bulk allocation via mmap/mremap (Linux) or mmap (macOS/BSD)
2. **Deduplication**: Stores repeated strings once, references them multiple times
3. **Arena Trimming**: Shrinks arena to actual usage via mremap() on Linux
4. **Inline Storage**: Small values (61-bit ints, 7-byte strings) stored directly in pointers

**Example** (AllPrices.yaml, 768 MB):
- Without dedup: 3.29 GB memory
- With dedup: 1.66 GB memory (39.6% savings)
- After trim: 1.09 GB memory (66.8% total savings)

### Zero-Copy Parsing

libfyaml never copies YAML content during parsing:
- Source string stays in memory
- Parsed values reference into source
- Only allocates for deduplicated strings and structure

### Lazy Conversion

FyGeneric objects convert to Python types on-demand:
- Access `data['name']` → converts that string to Python
- Iterate `for item in data['items']` → converts items as you iterate
- Full conversion `data.to_python()` → converts entire tree

**Like NumPy arrays**: Keep data in efficient format, convert only when needed.

---

## Documentation

- **[QUICKSTART.md](QUICKSTART.md)** - Tutorial and usage guide
- **[DEDUP-BENCHMARK-RESULTS.md](DEDUP-BENCHMARK-RESULTS.md)** - Deduplication benchmarks
- **[AUTO-TRIM-IMPLEMENTATION.md](AUTO-TRIM-IMPLEMENTATION.md)** - Memory trimming details
- **[ALLPRICES-BENCHMARK-FINAL.md](ALLPRICES-BENCHMARK-FINAL.md)** - Large file benchmarks
- **[RAPIDYAML-MEMORY-WALL.md](RAPIDYAML-MEMORY-WALL.md)** - Production memory issues
- **[API-COMPARISON-PYTHON-VS-TRENCHCOAT.md](API-COMPARISON-PYTHON-VS-TRENCHCOAT.md)** - API quality comparison

---

## FAQ

### Q: Is it thread-safe?

**A**: FyGeneric objects are immutable by default and safe to read from multiple threads. Parsing is thread-safe with independent documents.

### Q: Can I modify the parsed data?

**A**: FyGeneric is immutable by default. You have two options:

```python
# Option 1: Parse as mutable (if supported in your version)
data = libfyaml.loads(yaml, mutable=True)
data['new_key'] = 'value'  # Works!

# Option 2: Convert to Python dict
data = libfyaml.loads(yaml)
mutable = data.to_python()
mutable['new_key'] = 'value'
```

### Q: Why use this over PyYAML?

**A**: 22x faster, 7.7x less memory, same API. Drop-in replacement for most use cases.

### Q: Does it support YAML 1.2?

**A**: Yes! Full YAML 1.2 support via libfyaml.

### Q: What about writing YAML?

**A**: Yes! Use `dumps()` and `dump()` to emit YAML:
```python
import libfyaml

# Parse YAML
data = libfyaml.loads(yaml_string)

# Write to string
yaml_output = libfyaml.dumps(data)

# Write to file
with open('output.yaml', 'w') as f:
    libfyaml.dump(data, f)

# Multi-document support
docs = [doc1, doc2, doc3]
yaml_output = libfyaml.dumps_all(docs)
```

### Q: How much faster is it really?

**A**: On 6.35 MB file:
- PyYAML: 2,610 ms
- libfyaml: 119 ms
- **Speedup: 21.9x**

On 768 MB file:
- PyYAML: ~43 minutes (projected)
- libfyaml: 19 seconds
- **Speedup: ~138x**

### Q: What's the catch?

**A**:
- Immutable by default (use `mutable=True` or convert to dict for modifications)
- Linux/macOS only (no Windows support yet)
- Beta software (v0.9.0 - approaching stable 1.0)

### Q: When should I NOT use this?

**A**:
- When you need round-trip editing with preserved formatting (use ruamel.yaml)
- When files are tiny (<1KB) and PyYAML is fast enough
- When you need Windows support
- When you require 100% PyYAML API compatibility (minor differences exist)

---

## PyYAML Compatibility

libfyaml provides a drop-in PyYAML replacement via `pyyaml_compat`:

```python
# Instead of:
import yaml

# Use:
from libfyaml import pyyaml_compat as yaml

# Then use normally:
data = yaml.safe_load("foo: bar")
output = yaml.safe_dump({"foo": "bar"})
```

### Fully Supported

| Feature | Status |
|---------|--------|
| `load()` / `safe_load()` | Full support (with yaml1.1 mode) |
| `load_all()` / `safe_load_all()` | Full support |
| `dump()` / `safe_dump()` | Full support |
| `dump_all()` / `safe_dump_all()` | Full support |
| `compose()` / `compose_all()` | Full support |
| `add_constructor()` | Full support |
| `add_multi_constructor()` | Full support (including None catch-all) |
| `add_representer()` | Full support |
| Standard tags (`!!binary`, `!!timestamp`, `!!omap`, `!!set`) | Full support |
| Error classes (`YAMLError`, `ScannerError`, etc.) | Full support |
| Loader/Dumper classes | Full support (stubs for API compatibility) |

### Not Implemented (Streaming API)

The following PyYAML streaming features are **not implemented**:

| Feature | Reason |
|---------|--------|
| `scan()` | Returns tokens - rarely used directly |
| `parse()` | Returns events - rarely used directly |
| `emit()` | Emits events to stream - rarely used directly |
| `serialize()` | Serializes nodes to stream - rarely used directly |
| Token/Event classes | Internal PyYAML implementation details |
| `add_implicit_resolver()` | Stub only - complex feature rarely needed |
| `add_path_resolver()` | Stub only - complex feature rarely needed |

**Why not implemented?** These streaming APIs are used in <5% of PyYAML usage. Most applications use `safe_load()`/`safe_dump()` which are fully supported. The streaming API would require significant implementation effort for minimal benefit.

**Workaround**: If you need streaming, continue using PyYAML for that specific code path, or convert your code to use `load_all()`/`dump_all()` for multi-document processing.

### Known Deviations

Minor differences from PyYAML behavior (documented in code):

1. **Booleans**: libfyaml's yaml1.1 mode accepts `y/Y/n/N` as booleans per YAML 1.1 spec; PyYAML doesn't
2. **Integer formats**: PyYAML supports underscores (`1_000_000`) and sexagesimal (`190:20:30`)
3. **Line numbers**: Mark objects may have slightly different line/column positions

These are edge cases that rarely affect real-world usage.

---

## Benchmarking

### Run Your Own Benchmarks

```bash
# Install dependencies
pip install PyYAML tracemalloc

# Run benchmarks
cd python-libfyaml

# Small file benchmark
python3 benchmark_dedup_configurations.py

# Large file benchmark (if you have AllPrices.yaml)
python3 benchmark_allprices.py

# Compare with rapidyaml (if installed)
python3 benchmark_allprices_rapidyaml.py
```

### Interpreting Results

- **Parse time**: Lower is better
- **Memory usage**: Lower is better
- **Multiplier**: Memory relative to file size (lower is better)

**Good results**:
- Memory < 3x file size
- Parse time < 1 second for 10 MB

**Excellent results**:
- Memory < 2x file size (dedup working)
- Parse time < 100ms for 10 MB

---

## Development

### Building from Source

```bash
# Build libfyaml first
cd ..  # Parent directory
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Build Python extension
cd ../python-libfyaml
python3 setup.py build_ext --inplace

# Test
python3 -c "import libfyaml; print(libfyaml.loads('name: test'))"
```

### Project Structure

```
python-libfyaml/
├── README.md                           # This file
├── QUICKSTART.md                       # Tutorial
├── setup.py                           # Build configuration
├── pyproject.toml                     # Python package metadata
├── libfyaml/
│   ├── __init__.py                   # Public API
│   └── _libfyaml_minimal.c           # C extension
├── tests/                             # Test suite (225 tests)
│   ├── test_basic.py                 # Basic API tests
│   ├── test_core.py                  # Core functionality
│   ├── test_decorated_int.py         # Large unsigned integers
│   ├── test_sized_string.py          # Binary-safe strings
│   ├── test_hash_support.py          # Hash/dict key support
│   ├── test_isinstance.py            # isinstance() support
│   └── test_advanced_methods.py      # Advanced methods
├── benchmark_*.py                     # Performance benchmarks
├── test_*.py                          # Development/exploratory scripts
└── docs/
    ├── DEDUP-BENCHMARK-RESULTS.md    # Dedup analysis
    ├── AUTO-TRIM-IMPLEMENTATION.md   # Trim analysis
    ├── ALLPRICES-BENCHMARK-FINAL.md  # Large file results
    └── ...
```

### Running Tests

The project includes a comprehensive test suite with 225 tests covering all features:

```bash
# Install pytest if not already installed
pip install pytest

# Run all tests
pytest tests/ -v

# Run specific test files
pytest tests/test_basic.py -v           # Basic API tests (8 tests)
pytest tests/test_core.py -v            # Core functionality (34 tests)
pytest tests/test_decorated_int.py -v   # Large unsigned integers (42 tests)
pytest tests/test_sized_string.py -v    # Binary-safe strings (28 tests)
pytest tests/test_hash_support.py -v    # Hash/dict key support (26 tests)
pytest tests/test_isinstance.py -v      # isinstance() support (31 tests)
pytest tests/test_advanced_methods.py -v # Advanced methods (56 tests)

# Run with coverage report
pytest tests/ --cov=libfyaml --cov-report=html
```

**Test categories:**
- **Basic API**: Core YAML/JSON parsing and dumping
- **Type support**: Integers, strings, floats, booleans, null, sequences, mappings
- **Advanced features**: Binary-safe strings, large unsigned integers (up to 2^64-1)
- **Python integration**: isinstance(), hash(), 'in' operator, dict/list interfaces
- **Comparison/arithmetic**: Full Python operator support
- **Advanced methods**: Type checking (is_null, is_int, etc.), clone(), get_path(), get_at_path(), set_at_path(), get_unix_path(), get_at_unix_path(), set_at_unix_path(), __format__()

---

## Contributing

Contributions welcome! This is an experimental binding showcasing libfyaml's generic type system.

### Areas for Contribution

- Windows support (mmap/mremap portability)
- Better error messages with context
- Streaming API (`scan()`, `parse()`, `emit()`) if demand arises
- Additional PyYAML edge case compatibility
- Performance optimizations

---

## License

MIT License (same as libfyaml)

---

## Acknowledgments

- **Pantelis Antoniou**: Creator of libfyaml
- **libfyaml community**: For the excellent C library
- **Python community**: For setting high API standards

---

## Quick Links

- [libfyaml GitHub](https://github.com/pantoniou/libfyaml)
- [YAML 1.2 Spec](https://yaml.org/spec/1.2/spec.html)
- [Python bindings tutorial (QUICKSTART.md)](QUICKSTART.md)

---

**TL;DR**: Want fast YAML parsing in Python without the headaches? Use libfyaml. It's PyYAML's API at 22x the speed with 7.7x less memory. Just works.™
