# JSON Benchmark Guide

## Overview

The `benchmark_json.py` script benchmarks libfyaml's JSON parsing performance against other popular Python JSON libraries using real-world data.

**Key Features:**
- ✅ **Direct file loading:** Uses `libfyaml.load()` and `json.load()` to read directly from files without pre-loading into memory
- ✅ **Real-world data:** Benchmarks with actual production JSON (AllPrintings.json)
- ✅ **Memory efficient:** Measures actual parsing memory, not file loading overhead
- ✅ **Fair comparison:** Each library uses its most efficient file-loading method

## Usage

### With Real Data (Default)

Uses the **AllPrintings.json** file (428MB MTG card database):

```bash
python benchmark_json.py
```

**Note:** This is a large file and the benchmark will take several minutes to complete.

### With Custom File

```bash
python benchmark_json.py --file path/to/your/file.json
```

### With Synthetic Data

Uses generated test data (legacy mode):

```bash
python benchmark_json.py --synthetic
```

## AllPrintings.json

This is a real-world JSON file from the [MTGJSON project](https://mtgjson.com/) containing Magic: The Gathering card data.

### Download

1. **Direct download:** https://mtgjson.com/downloads/all-files/
2. Look for **AllPrintings.json** (~428MB compressed, ~428MB uncompressed)
3. Place it in the `python-libfyaml/` directory (or symlink it)

### File Characteristics

- **Size:** ~428 MB
- **Type:** Real-world JSON database
- **Structure:** Deeply nested objects and arrays
- **Content:** Card names, descriptions, metadata (lots of repeated strings)
- **Dedup benefit:** High - Many repeated strings (card types, colors, rarities, etc.)

### Why This File?

- **Real-world complexity:** Actual production JSON structure
- **Size:** Large enough to show meaningful differences
- **Repetition:** Natural deduplication opportunities (not artificially inflated)
- **Stability:** Well-known dataset in the MTG community
- **Accessibility:** Freely available, updated regularly

## Expected Results

With the AllPrintings.json file (428MB):

### Parse Time (approximate)

- **json (stdlib):** ~2-3 seconds
- **orjson:** ~0.5-1 second (fastest)
- **libfyaml (dedup=True):** ~3-4 seconds
- **libfyaml (dedup=False):** ~2.5-3.5 seconds

### Memory Usage (approximate)

- **json (stdlib):** ~600-800 MB
- **orjson:** ~600-800 MB
- **libfyaml (dedup=False):** ~600-800 MB
- **libfyaml (dedup=True):** ~400-500 MB (**30-40% savings!**)

### Key Insights

1. **Speed:** libfyaml is competitive with stdlib json, though slower than orjson
2. **Memory:** Deduplication provides significant memory savings on real-world data
3. **Trade-off:** Slight speed penalty for dedup is worth it for large datasets

## Libraries Compared

### Included by Default

- **json (stdlib):** Python's built-in JSON library
  - Uses: `json.load(fp)` - direct file loading
- **libfyaml:** This library (with/without dedup)
  - Uses: `libfyaml.load(fp, mode='json', dedup=...)` - direct file loading with optional dedup

### Optional (if installed)

- **orjson:** Fast JSON library written in Rust
  ```bash
  pip install orjson
  ```
  - Uses: `orjson.loads(data)` - requires pre-loading file (no file API)

- **ujson:** Ultra-fast JSON library in C
  ```bash
  pip install ujson
  ```
  - Uses: `ujson.load(fp)` - direct file loading

## Requirements

```bash
pip install psutil  # Required for memory measurements
pip install orjson  # Optional - for comparison
pip install ujson   # Optional - for comparison
```

## Benchmark Output

Example output format:

```
======================================================================
Benchmark: Real JSON File
======================================================================
Loading JSON file: AllPrintings.json
File size: 428.00 MB
Loaded: 428.00 MB (448,765,432 bytes)

⚠ Large file detected (428 MB) - using 3 iterations
   (This may take several minutes...)

Benchmarking json (stdlib)...
Benchmarking libfyaml (dedup=True)...
Benchmarking libfyaml (dedup=False)...
Benchmarking orjson...

Library                      Time (ms)     Memory (KB)      vs json
----------------------------------------------------------------------
json (stdlib)                 2341.23         786,432   1.00x  1.00x
libfyaml (dedup=True)         3127.45         458,752   0.75x  1.71x
libfyaml (dedup=False)        2876.91         786,432   0.81x  1.00x
orjson                         687.32         786,432   3.41x  1.00x

Dedup memory savings: 41.7%
```

## Interpreting Results

### Time (ms)
- Lower is faster
- "vs json" shows speedup multiplier (2.00x = twice as fast)

### Memory (KB)
- Lower is better
- "vs json" shows memory savings multiplier (2.00x = uses half the memory)

### Dedup Savings
- Percentage of memory saved by using deduplication
- Higher on data with repeated strings (like AllPrintings.json)

## Tips

1. **Large files:** Be patient - 428MB takes time to parse multiple times
2. **Memory:** Close other applications for accurate memory measurements
3. **Consistency:** Run multiple times to account for system variance
4. **Custom data:** Test with your own JSON files using `--file`

## Troubleshooting

### "ERROR: JSON file not found"

Options:
1. Download AllPrintings.json from https://mtgjson.com/downloads/all-files/
2. Use `--file <path>` to specify a different file
3. Use `--synthetic` to generate test data

### "ERROR: psutil required"

```bash
pip install psutil
```

### Benchmark too slow

For faster testing:
```bash
python benchmark_json.py --synthetic  # Uses smaller generated data
```

Or use a smaller JSON file:
```bash
python benchmark_json.py --file smaller_file.json
```
