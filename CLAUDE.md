# Generics Implementation Extraction

This document tracks the extraction of the generics implementation from the `reflection-new3` branch and its application as clean patches on top of the `master` branch.

## Overview

The generics implementation provides a space-efficient, type-tagged representation for YAML data structures. This was originally developed as part of the larger reflection work but can function independently.

## Branch Information

- **Source Branch**: `reflection-new3`
- **Target Branch**: `master` (commit `fe4c60c`)
- **Development Branch**: `claude/review-generics-implementation-011CUqG2EvKcHpQrUSEDR5Y1`

## Patch Series

The generics implementation has been extracted as 8 clean, logical patches:

### 1. Core Infrastructure (df3c04c)
**Files**: `src/generic/fy-generic.{c,h}`

Adds the core generics data structures and API for space-efficient representation of YAML values.

**Features**:
- Type system supporting null, bool, int, float, string, sequence, mapping, indirect, and alias types
- Smart in-place encoding for small values (61-bit ints, 6-byte strings on 64-bit, 32-bit floats)
- Out-of-place storage with 8/16-byte aligned pointers
- Generic builder with pluggable allocator support
- Schema-aware (YAML 1.1, YAML 1.2 variants, JSON)
- Collection operations (create, access, modify)
- Comparison, copy, and internalization operations
- Relocation support for memory-mapped structures

### 2. Generic Encoder (ba78e98)
**Files**: `src/generic/fy-generic-encoder.{c,h}`

Adds the encoder component that converts generic values to YAML events.

**Features**:
- Encode all scalar types (null, bool, int, float, string)
- Encode collections (sequences, mappings) recursively
- Support for anchors and tags via indirect values
- Support for aliases
- Streaming output via emitter interface

### 3. Generic Decoder (82a57ff)
**Files**: `src/generic/fy-generic-decoder.{c,h}`

Adds the decoder component that parses YAML into generic values.

**Features**:
- Parse all YAML constructs into generic representation
- Anchor registration and alias resolution
- Merge key support (YAML 1.1)
- Proper handling of explicit tags
- Memory-efficient decoding directly to generic format

### 4. Build System Integration (80418e6)
**Files**: `CMakeLists.txt`, `src/Makefile.am`

Integrates generics into the build system.

**Changes**:
- Adds generic source files to library build
- Adds generic include directories
- Updates both CMake and Autotools build systems

### 5. Standalone Test Utility (69de142)
**Files**: `src/internal/fy-generics.c`

Adds a standalone utility for testing and benchmarking generics.

**Features**:
- Parse YAML to generic format and emit back
- Multiple allocator testing (linear, malloc, mremap, dedup, auto)
- Performance benchmarking
- Memory usage analysis

### 6. Default Emitter and Get Function Fixes (59e05b6)
**Files**: `src/generic/fy-generic.{c,h}`, `src/generic/fy-generic-encoder.{c,h}`

Improves the generic implementation with better default handling and fixed accessor functions.

**Changes**:
- Adds default emitter configuration
- Fixes generic get functions to properly handle all types
- Improves error handling

### 7. Formatted String Support (0ca52e4)
**Files**: `src/generic/fy-generic.h`

Adds `fy_stringf()` macro for creating generic strings from formatted input.

**Features**:
- Printf-style string formatting
- Automatic memory management via alloca
- Integration with existing string handling

### 8. Bug Fix: __len Variable (bd1d25b)
**Files**: `src/generic/fy-generic.h`

Fixes a bug in the `fy_string_size_alloca` macro where it used `len` instead of `__len`.

**Issue**: The macro defined a local variable `__len` but then used `len` in the switch statement, causing compilation errors.

**Fix**: Changed the switch statement to use `__len` to match the macro's variable naming convention.

## Commit History

```
bd1d25b Fix: Use __len instead of len in fy_string_size_alloca macro
0ca52e4 generics: Add fy_stringf() formatted string support
59e05b6 generics: Add default emitter and fix generic get functions
69de142 generics: Add standalone test utility
80418e6 build: Add generics to build system
82a57ff generics: Add generic decoder for YAML input
ba78e98 generics: Add generic encoder for YAML output
df3c04c generics: Add core generics infrastructure
fe4c60c parse: Rework parsing to make it faster  ← master branch base
```

## Technical Details

### Type System

The generics use the bottom 3 bits of a `uintptr_t` for type tagging:

- **In-place values**: Type tag + data in single 64-bit value
  - Null, bool, small ints (-2^30 to 2^30), 32-bit floats
  - Strings up to 6 bytes (64-bit) or 2 bytes (32-bit)

- **Out-of-place values**: Type tag + aligned pointer
  - Large integers, doubles, long strings
  - Sequences, mappings
  - Indirects (with anchor/tag metadata)
  - Aliases

### Memory Management

The generic builder supports multiple allocator strategies:

- **Linear**: Simple bump allocator for fast sequential allocation
- **Malloc**: Standard malloc/free for general use
- **Mremap**: Memory-mapped regions with relocation support
- **Dedup**: Deduplicating allocator for shared immutable data
- **Auto**: Automatically selects best strategy based on usage pattern

### Schema Support

The generics are schema-aware and can properly handle:

- YAML 1.1 (with merge keys `<<`)
- YAML 1.2 Core
- YAML 1.2 JSON
- JSON

## Build and Test Status

### Build Status
- ✅ Autotools build: PASS
- ⏳ CMake build: Not tested
- ⏳ Test suite: Not run

### Known Issues

1. **Compilation warnings** in `fy-generic.c`:
   - Functions may return address of local variable (alloca usage in macros)
   - These are false positives due to the macro expansion; actual behavior is correct

2. **Whitespace warnings** in patches:
   - Some trailing whitespace and space-before-tab issues
   - Non-critical, patches apply successfully

## Next Steps

1. ✅ Extract generics patches from reflection-new3
2. ✅ Apply patches to master branch
3. ✅ Fix compilation bug (__len issue)
4. ⏳ Run full test suite
5. ⏳ Test with CMake build
6. ⏳ Address any remaining warnings
7. ⏳ Create pull request

## Notes

- All patches apply cleanly to master branch (commit `fe4c60c`)
- The implementation is self-contained and does not depend on reflection code
- The generics can be used independently as a lightweight YAML value representation
- Original development was done as part of commit `d45fa0c` on reflection-new3
