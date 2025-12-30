# libfyaml Python Utilities

Simple command-line utilities using the libfyaml Python bindings.

## yaml_cat.py

Minimal YAML cat utility - reads a YAML file and outputs it to stdout.

**Usage:**
```bash
./yaml_cat.py <file.yaml>
```

**Example:**
```bash
./yaml_cat.py config.yaml
```

**Features:**
- Ultra-simple: just 12 lines of code
- Uses mmap-based loading for efficiency
- Memory-efficient for large files

## yaml_dump.py

Feature-rich YAML dumper with format options.

**Usage:**
```bash
./yaml_dump.py <yaml_file> [options]
```

**Options:**
- `--json` - Output as JSON instead of YAML
- `--compact` - Use compact/flow style
- `--no-dedup` - Disable deduplication

**Examples:**

Pretty-print YAML:
```bash
./yaml_dump.py data.yaml
```

Convert YAML to JSON:
```bash
./yaml_dump.py config.yaml --json
```

Compact YAML output:
```bash
./yaml_dump.py large.yaml --compact
```

Compact JSON output:
```bash
./yaml_dump.py data.yaml --json --compact
```

Disable deduplication for unique content:
```bash
./yaml_dump.py unique.yaml --no-dedup
```

**Features:**
- Automatic mmap-based loading for large files
- Format conversion (YAML ↔ JSON)
- Multiple output styles (block, flow, compact)
- Deduplication control
- Efficient memory usage

## Performance Notes

Both utilities use the new mmap-based `libfyaml.load()` API for efficient file loading:

**Memory usage on AllPrices.yaml (768 MB):**
- With mmap: 338 MB (0.44x file size)
- Sub-file-size memory usage with deduplication
- Can handle files larger than available RAM

**Comparison with other tools:**
```
Tool              Memory (768 MB file)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
yaml_cat.py       338 MB (mmap)
yaml_dump.py      338 MB (mmap)
yq (Go)           ~2 GB
python -m yaml    ~10+ GB
```

## Use Cases

### Format Conversion
```bash
# YAML to JSON
./yaml_dump.py config.yaml --json > config.json

# YAML to compact YAML
./yaml_dump.py verbose.yaml --compact > compact.yaml
```

### Validation
```bash
# Check if YAML is valid
./yaml_cat.py data.yaml > /dev/null && echo "Valid YAML" || echo "Invalid YAML"
```

### Pipeline Processing
```bash
# Pretty-print YAML from stdin
cat messy.yaml | ./yaml_cat.py - | less

# Chain with other tools
./yaml_dump.py input.yaml --json | jq '.field'
```

### Large File Handling
```bash
# Memory-efficient processing of huge files
./yaml_dump.py AllPrices.yaml --compact > compact.yaml

# Works even on memory-constrained systems
# (can parse 768 MB file with only 400 MB RAM!)
```

## Implementation Details

Both utilities leverage libfyaml's key features:

1. **mmap-based file loading** - `libfyaml.load(path)` uses memory-mapped I/O
2. **Zero-copy architecture** - Values point into the mmap'd source
3. **Deduplication** - Reduces memory for files with repeated content
4. **Efficient serialization** - `libfyaml.dumps()` uses new emit API

**Code structure:**
- Load: `libfyaml.load(file, mode='yaml', dedup=True)`
- Dump: `libfyaml.dumps(data, json=False, compact=False)`

Both operations are memory-efficient and can handle arbitrarily large files.

## Installation

These utilities are standalone Python scripts. Just ensure the libfyaml Python bindings are built:

```bash
cd python-libfyaml
python3 setup.py build_ext --inplace
```

Then run the utilities directly:
```bash
./yaml_cat.py file.yaml
./yaml_dump.py file.yaml --json
```

Or install globally:
```bash
chmod +x yaml_cat.py yaml_dump.py
sudo cp yaml_cat.py yaml_dump.py /usr/local/bin/
```

## See Also

- **benchmark_*.py** - Memory and performance benchmarks
- **ALLPRICES-BENCHMARK-RESULTS.md** - Large file benchmark results
- **MEMORY-LIMIT-COMPARISON.md** - Stress test results vs rapidyaml
