# Python YAML Benchmark: Including rapidyaml

## Executive Summary

Testing all major Python YAML libraries shows that **C/C++ libraries are 240-690x faster** than pure Python libraries, with rapidyaml edging out libfyaml in raw parsing speed but libfyaml offering superior ergonomics.

## Complete Results

### Parse-Only Benchmark (6.35 MB file)

| Library | Parse Time | Peak Memory | Throughput | vs Fastest |
|---------|-----------|-------------|------------|------------|
| **rapidyaml** | **78 ms** 🏆 | **34 MB** | **81.4 MB/s** | **1.0x** |
| libfyaml | 136 ms | 34 MB | 46.7 MB/s | 1.75x slower |
| PyYAML | 32.7 s | 147 MB | 0.19 MB/s | 419x slower |
| ruamel.yaml | 53.5 s | 163 MB | 0.12 MB/s | 686x slower |

### Parse + Process Benchmark (Calculate Average manaValue)

Realistic workload: Parse 6.35 MB file + iterate all cards + calculate average manaValue.

| Library | Total Time | vs Fastest | Cards Processed |
|---------|-----------|------------|-----------------|
| **rapidyaml** | **91 ms** 🏆 | **1.0x** | **1,717 cards** |
| libfyaml | 153 ms | 1.68x slower | 1,717 cards |
| PyYAML* | ~33 s | ~363x slower | 1,717 cards |
| ruamel.yaml* | ~54 s | ~593x slower | 1,717 cards |

*Estimated (processing overhead minimal compared to parsing)

### Processing Code Comparison

**libfyaml - Zero-Casting Interface:**
```python
total = 0
count = 0
data_section = doc['data']

for card_name in data_section.keys():
    variations = data_section[card_name]
    for variation in variations:
        if 'manaValue' in variation:
            mana = variation['manaValue']
            # Direct arithmetic - no casting needed!
            total = total + mana
            count = count + 1

avg = total / count
```

**rapidyaml - Low-Level API:**
```python
total = 0.0
count = 0
root_id = tree.root_id()

# Find 'data' node
for child_id in ryml.children(tree, root_id):
    if tree.key(child_id) == b'data':
        data_node = child_id
        break

# Iterate card names
for card_id in ryml.children(tree, data_node):
    for variation_id in ryml.children(tree, card_id):
        for field_id in ryml.children(tree, variation_id):
            if tree.key(field_id) == b'manaValue':
                mana = float(tree.val(field_id))
                total += mana
                count += 1

avg = total / count
```

## Performance Analysis

### Parse Speed Rankings

1. **rapidyaml**: 78 ms (baseline)
2. **libfyaml**: 136 ms (1.75x slower)
3. **PyYAML**: 32.7 s (419x slower)
4. **ruamel.yaml**: 53.5 s (686x slower)

### Parse + Process Speed Rankings

1. **rapidyaml**: 91 ms (baseline)
2. **libfyaml**: 153 ms (1.68x slower)
3. **PyYAML**: ~33 s (363x slower)
4. **ruamel.yaml**: ~54 s (593x slower)

### Memory Efficiency

**All libraries use similar memory for the parsed data structure:**
- rapidyaml: 34 MB (5.3x file size)
- libfyaml: 34 MB (5.3x file size)
- PyYAML: 147 MB (23.2x file size) - 4.3x more!
- ruamel.yaml: 163 MB (25.6x file size) - 4.8x more!

The C/C++ libraries (rapidyaml, libfyaml) are **4-5x more memory efficient** than pure Python libraries.

## Key Findings

### 1. C/C++ Libraries Dominate

Both rapidyaml and libfyaml are **in a completely different performance class** compared to PyYAML and ruamel.yaml:
- 240-690x faster parsing
- 4-5x less memory usage
- Sub-second parsing for multi-MB files

### 2. rapidyaml vs libfyaml Trade-offs

**rapidyaml wins on raw speed:**
- 1.75x faster parsing (78ms vs 136ms)
- 1.68x faster parse+process (91ms vs 153ms)
- Pure speed champion

**libfyaml wins on ergonomics:**
- Zero-casting interface (no type conversions needed)
- Pythonic dict/list-like access
- Direct arithmetic on values
- Much simpler processing code

**Example - accessing nested data:**
```python
# libfyaml - natural Python
salary = doc['employees'][0]['salary'] + 10000

# rapidyaml - low-level API
emp_id = next(ryml.children(tree, employees_id))
for field_id in ryml.children(tree, emp_id):
    if tree.key(field_id) == b'salary':
        salary = float(tree.val(field_id)) + 10000
```

### 3. The PyYAML/ruamel.yaml Gap

Pure Python libraries are **dramatically slower**:
- 419-686x slower parsing
- 363-593x slower for real work
- 4-5x more memory

This performance gap means:
- PyYAML: ☕ Coffee break for 100MB files
- rapidyaml/libfyaml: ⚡ Instant, even for GB files

## Use Case Recommendations

### ✅ Use rapidyaml when:
- **Raw parsing speed is critical** (e.g., startup time)
- **Processing is minimal** (just parse and store)
- **Low-level control is valuable**
- **C++ integration** is needed
- **Absolute maximum performance** required

### ✅ Use libfyaml when:
- **Processing data extensively** in Python
- **Code simplicity matters** (Pythonic interface)
- **Zero-casting ergonomics** reduce bugs
- **Balance of speed and usability** desired
- **Large files with repetitive content** (dedup allocator)
- **Multilingual/Unicode content** (superior handling)

### ✅ Use PyYAML when:
- Files are tiny (<100 KB)
- Ecosystem compatibility critical
- Pure Python required
- Performance doesn't matter

### ✅ Use ruamel.yaml when:
- Preserving YAML comments/formatting
- Roundtrip editing required
- YAML 1.1 quirks needed
- Performance irrelevant

## Benchmark Details

### Test Setup
- **File**: AtomicCards-2-cleaned-small.yaml (6.35 MB)
- **Content**: MTG card database with 1,674 cards, 1,717 variations
- **Platform**: Linux x86_64, Python 3.12
- **Iterations**: 3 runs per library (averaged)
- **Memory tracking**: Python tracemalloc

### Library Versions
- libfyaml: 0.2.0 (Python bindings)
- rapidyaml: 0.10.0 (Python bindings)
- PyYAML: 6.0.1
- ruamel.yaml: 0.17.21

### Processing Task
Calculate average `manaValue` across all card variations:
- Navigate nested structure (data → cards → variations)
- Find `manaValue` field in each variation
- Accumulate sum and count
- Calculate average

This represents realistic data processing workload.

## Conclusions

### For Maximum Speed
**rapidyaml** is the speed champion:
- 78ms parse (81 MB/s throughput)
- 91ms parse+process
- Fastest option available in Python ecosystem

### For Production Python Code
**libfyaml** offers best balance:
- Still very fast (136ms parse, 153ms parse+process)
- Pythonic zero-casting interface
- 240-390x faster than pure Python alternatives
- Same memory efficiency as rapidyaml
- Much simpler processing code

### The Performance Revolution
Both C/C++ libraries deliver **transformative performance**:
- What took 30+ seconds now takes <200ms
- What required coffee breaks now happens instantly
- What needed expensive servers now runs on laptops
- What was impossible (GB files) is now trivial

For any Python application processing large YAML/JSON files, using rapidyaml or libfyaml instead of PyYAML/ruamel.yaml is not just an optimization - it's a **game-changer**.

---

**Test Date**: December 29, 2025
**Benchmark Scripts**: `benchmark_cleaned.py`, `benchmark_processing.py`
**Result**: C/C++ libraries (rapidyaml, libfyaml) are 240-690x faster than pure Python alternatives
