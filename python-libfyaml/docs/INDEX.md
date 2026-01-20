# libfyaml Python Bindings - Documentation Index

This directory contains comprehensive documentation for the libfyaml Python bindings.

## Quick Navigation

### Getting Started
- **[README](../README.md)** - Project overview, installation, and quick start
- **[QUICKSTART](getting-started/QUICKSTART.md)** - Comprehensive tutorial
- **[STATUS](getting-started/STATUS.md)** - Current project status
- **[CHANGELOG](getting-started/CHANGELOG.md)** - Release history

### API Reference
- **[API Reference](api/ACTUAL_API.md)** - Complete API documentation
- **[PyYAML Compatibility](api/PYYAML-COMPATIBILITY.md)** - Drop-in PyYAML replacement guide
- **[PyYAML Comparison](api/PYYAML-COMPARISON.md)** - Detailed comparison with PyYAML
- **[API Comparison (vs rapidyaml)](api/API-COMPARISON-PYTHON-VS-TRENCHCOAT.md)** - API quality comparison
- **[Parse/Emit API](api/PARSE-EMIT-API.md)** - Parsing and emitting details
- **[Porting Guide](api/PORTING-GUIDE.md)** - Guide for migrating from other libraries

### Performance & Benchmarks
- **[Benchmarks Overview](benchmarks/BENCHMARKS.md)** - Performance overview
- **[Benchmark Results](benchmarks/BENCHMARK-RESULTS.md)** - Detailed benchmark data
- **[Large File Benchmarks](benchmarks/ALLPRICES-BENCHMARK-FINAL.md)** - 768 MB file results
- **[Dedup Benchmarks](benchmarks/DEDUP-BENCHMARK-RESULTS.md)** - Deduplication impact
- **[JSON Benchmarks](benchmarks/JSON-BENCHMARK-RESULTS.md)** - JSON parsing performance
- **[rapidyaml Comparison](benchmarks/BENCHMARK-WITH-RAPIDYAML.md)** - Speed comparison

### Memory Management
- **[Memory Analysis](memory/MEMORY-ANALYSIS.md)** - Memory usage deep dive
- **[Memory Best Practices](memory/MEMORY-BEST-PRACTICES.md)** - Optimization guide
- **[Memory Management Guide](memory/MEMORY-MANAGEMENT-GUIDE.md)** - Complete memory guide
- **[Auto-Trim Implementation](memory/AUTO-TRIM-IMPLEMENTATION.md)** - Arena trimming details
- **[rapidyaml Memory Wall](memory/RAPIDYAML-MEMORY-WALL.md)** - Why memory matters

### Features
- **[Decorated Int Support](features/DECORATED-INT-SUPPORT.md)** - Large unsigned integers
- **[Hash Support](features/HASH-SUPPORT.md)** - Using FyGeneric as dict keys
- **[isinstance Support](features/ISINSTANCE-SUPPORT.md)** - Type checking support
- **[JSON Serialization](features/JSON-SERIALIZATION.md)** - JSON output support
- **[Type Propagation](features/TYPE-PROPAGATION.md)** - Type system details
- **[Format String Enhancement](features/FORMAT-STRING-ENHANCEMENT.md)** - f-string support
- **[Mutation Design](features/MUTATION-DESIGN.md)** - Mutable document support

### Development
- **[Design Notes](development/DESIGN-NOTES.md)** - Architecture decisions
- **[Productization](development/PRODUCTIZATION.md)** - Production readiness
- **[TODO](development/TODO-FULL-IMPLEMENTATION.md)** - Future work

## Documentation Structure

```
docs/
├── INDEX.md                 # This file
├── getting-started/         # Installation, tutorials, status
│   ├── QUICKSTART.md
│   ├── STATUS.md
│   └── CHANGELOG.md
├── api/                     # API reference and compatibility
│   ├── ACTUAL_API.md
│   ├── PYYAML-COMPATIBILITY.md
│   ├── PYYAML-COMPARISON.md
│   ├── API-COMPARISON-PYTHON-VS-TRENCHCOAT.md
│   ├── PARSE-EMIT-API.md
│   └── PORTING-GUIDE.md
├── benchmarks/              # Performance data
│   ├── BENCHMARKS.md
│   ├── BENCHMARK-RESULTS.md
│   ├── ALLPRICES-BENCHMARK-FINAL.md
│   ├── DEDUP-BENCHMARK-RESULTS.md
│   ├── JSON-BENCHMARK-RESULTS.md
│   └── BENCHMARK-WITH-RAPIDYAML.md
├── memory/                  # Memory management
│   ├── MEMORY-ANALYSIS.md
│   ├── MEMORY-BEST-PRACTICES.md
│   ├── MEMORY-MANAGEMENT-GUIDE.md
│   ├── AUTO-TRIM-IMPLEMENTATION.md
│   └── RAPIDYAML-MEMORY-WALL.md
├── features/                # Feature documentation
│   ├── DECORATED-INT-SUPPORT.md
│   ├── HASH-SUPPORT.md
│   ├── ISINSTANCE-SUPPORT.md
│   ├── JSON-SERIALIZATION.md
│   ├── TYPE-PROPAGATION.md
│   ├── FORMAT-STRING-ENHANCEMENT.md
│   └── MUTATION-DESIGN.md
└── development/             # Development notes
    ├── DESIGN-NOTES.md
    ├── PRODUCTIZATION.md
    └── TODO-FULL-IMPLEMENTATION.md
```

## Platform Support

| Platform | Status |
|----------|--------|
| Linux | Fully supported |
| macOS | Fully supported |
| Windows | Not yet supported |

## Known Limitations

### PyYAML Compatibility Gaps

The following PyYAML features are **not implemented**:
- Streaming API: `scan()`, `parse()`, `emit()`, `serialize()`
- Token and Event classes
- `add_implicit_resolver()` (stub only)
- `add_path_resolver()` (stub only)

These features are rarely used directly and represent <5% of typical PyYAML usage.

**See [PyYAML Compatibility](api/PYYAML-COMPATIBILITY.md) for details.**
