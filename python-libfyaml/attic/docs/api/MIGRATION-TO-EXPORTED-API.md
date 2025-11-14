# Migration to Exported Generic API

## Overview

Successfully migrated Python bindings from using internal static libraries to using the exported generic API from the shared library (`libfyaml.so`).

## Changes Made

### 1. Updated C Extension Source

**File**: `libfyaml/_libfyaml_minimal.c`

- Changed header include from internal to exported:
  ```c
  // Before:
  #include "generic/fy-generic.h"

  // After:
  #include <libfyaml/fy-internal-generic.h>
  ```

- Replaced direct struct member access with exported function:
  ```c
  // Before (5 locations):
  if (self->gb && self->gb->allocator) {
      fy_allocator_trim_tag(self->gb->allocator, self->gb->alloc_tag);
  }

  // After:
  if (self->gb) {
      fy_gb_trim(self->gb);
  }
  ```

### 2. Simplified Build Configuration

**File**: `setup.py`

Complete rewrite to use only the exported API:

**Removed**:
- Static library linking (`extra_objects`)
- LLVM/libclang dependency handling
- Internal source directory includes
- Complex build logic for different backends

**Simplified to**:
- Only link against shared library (`-lfyaml`)
- Only include public headers (`include/` and `build/`)
- Automatic detection of CMake or Autotools build

### 3. Symbol Export Updates

The following symbols were added to the libfyaml export list:
- `fy_gb_trim` - Generic builder trim function
- `fy_generic_compare_out_of_place` - Generic comparison function

All symbols required by Python bindings are now exported:
```
fy_allocator_create
fy_allocator_destroy
fy_gb_float_type_create_out_of_place
fy_gb_internalize_out_of_place
fy_gb_int_type_create_out_of_place
fy_gb_string_create_out_of_place
fy_gb_string_size_create_out_of_place
fy_gb_trim
fy_generic_builder_create
fy_generic_builder_destroy
fy_generic_compare_out_of_place
fy_generic_get_type_indirect
fy_generic_indirect_get_value
fy_generic_mapping_resolve_outofplace
fy_generic_op
fy_generic_op_args
fy_genericp_indirect_get_valuep
fy_generic_sequence_resolve_outofplace
```

## Build Instructions

### Building the C Library

```bash
cd /mnt/980-linux/panto/work/sandbox/libfyaml-generics-docs/build
cmake --build . --target fyaml
```

### Building Python Extension

```bash
cd python-libfyaml
python3 setup.py build_ext --inplace
```

### Running Tests

```bash
# Set library path
export LD_LIBRARY_PATH=/mnt/980-linux/panto/work/sandbox/libfyaml-generics-docs/build:$LD_LIBRARY_PATH

# Run Python tests
python3 -c "
import sys
sys.path.insert(0, '.')
import libfyaml
print('✓ Import successful')
doc = libfyaml.loads('name: Alice\\nage: 30')
print(f'✓ Parse: {doc[\"name\"]}, {doc[\"age\"]}')
"
```

## Benefits

1. **Cleaner Architecture**: No dependency on internal static libraries
2. **Easier Maintenance**: Uses only public exported API
3. **Simpler Build**: Reduced build complexity and dependencies
4. **Better Encapsulation**: No access to internal implementation details
5. **Standard Linking**: Uses standard shared library linking mechanism

## Verification

All required symbols are properly exported and functional:
- ✓ Basic YAML parsing
- ✓ Arithmetic operations
- ✓ String methods
- ✓ Comparisons
- ✓ Round-trip emit/parse
- ✓ Multi-document support
- ✓ File I/O operations

## Files Modified

- `libfyaml/_libfyaml_minimal.c` - Updated to use exported API
- `setup.py` - Simplified to link against shared library only

## Testing Completed

- Multi-document YAML parsing and emission
- Arithmetic operations on numeric values
- String operations and comparisons
- File-based load/dump operations
- Round-trip conversion testing
