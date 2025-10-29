# Work In Progress: Converting libfyaml-parser.c to Test Cases

## Overview

This document tracks the ongoing work to extract testable functionality from `src/internal/libfyaml-parser.c` (a 4,622-line multi-mode debugging tool) into proper unit tests in the libfyaml test suite.

## What Has Been Done

### Test Infrastructure
- Created `test/libfyaml-test-parser.c` - new test file for parser-extracted tests
- Modified `test/Makefile.am` - added new test file to build
- Modified `test/libfyaml-test.c` - registered new test case function `libfyaml_case_parser()`
- All tests use the Check unit testing framework

### Tests Extracted (35 total)

#### Mapping Tests (4 tests)
From commented-out code in `do_build()` (lines 1710-3241):
- `parser_mapping_iterator` - Forward/reverse iteration, index access
- `parser_mapping_key_lookup` - Key lookups including complex keys
- `parser_mapping_prepend` - Prepending key-value pairs
- `parser_mapping_remove` - Removing pairs from mappings

#### Path Tests (2 tests)
From `do_build()` commented code:
- `parser_path_queries` - Path-based node queries with `fy_node_by_path()`
- `parser_node_path_generation` - Generating paths from nodes with `fy_node_get_path()`

#### Node Creation Tests (7 tests)
From `do_build()` commented code:
- `parser_node_creation_scalar` - Creating scalar nodes
- `parser_node_creation_multiline_scalar` - Multi-line scalars
- `parser_node_creation_empty_sequence` - Empty sequences
- `parser_node_creation_empty_mapping` - Empty mappings
- `parser_node_creation_populated_sequence` - Sequences with items
- `parser_node_creation_populated_mapping` - Mappings with pairs
- `parser_build_node_from_string` - Building nodes from YAML strings

#### Sequence Tests (3 tests)
From `do_build()` commented code:
- `parser_sequence_negative_index` - Negative indexing support
- `parser_sequence_append_prepend` - Adding items at both ends
- `parser_sequence_remove` - Removing items from sequences

#### Structure Tests (2 tests)
From `do_build()` commented code:
- `parser_complex_nested_structure` - Deeply nested YAML structures
- `parser_anchor_alias_resolution` - Anchor/alias dereferencing

#### Document Operation Tests (5 tests)
From `do_build()` and `do_dump()`:
- `parser_document_insert_at` - Inserting nodes at specific paths
- `parser_document_emit_flags` - Testing emit flags (sort, indent, width)
- `parser_multi_document_stream` - Multiple documents in stream
- `parser_empty_document` - Empty/null documents
- `parser_document_with_comments` - Documents containing comments

#### Iterator Tests (3 tests)
From `do_iterate()` (lines 967-1020):
- `parser_document_iterator` - Document-wide node iteration
- `parser_document_iterator_key_detection` - Detecting mapping keys during iteration
- `parser_iterator_alias_detection` - Detecting aliases during iteration

#### Comment Tests (1 test)
From `do_comment()` (lines 1022-1077):
- `parser_comment_retrieval` - Retrieving comments from tokens

#### Event and Parsing Tests (6 tests)
From `do_parse()`, `do_testsuite()`, `dump_testsuite_event()`, `do_dump()`:
- `parser_event_generation` - Generating and processing events
- `parser_scalar_styles` - Plain, single-quoted, double-quoted, literal, folded
- `parser_tag_handling` - Standard and custom tags
- `parser_yaml_version` - YAML 1.1 vs 1.2
- `parser_flow_block_styles` - Flow vs block notation
- `parser_document_builder` - Document builder API pattern

#### Utility Function Tests (2 tests) - **NEW in latest commit**
From `do_shell_split()` (lines 3961-3993) and `do_bad_utf8()` (lines 3866-3959):
- `parser_shell_split` - Shell-like string splitting with quotes and escapes
- `parser_utf8_validation` - UTF-8 encoding/decoding validation

## How to Test

### Building the Project
```bash
cd /home/user/libfyaml
make clean
make
```

### Running Tests Locally
The Check unit testing framework must be installed to run tests:

```bash
# Check if Check library is available
pkg-config --modversion check

# Build and run all tests
make check

# Run only the parser tests (if Check is available)
./test/libfyaml-test
```

**Note:** The Check library is not available in the current development environment, but the code compiles successfully. The CI system should have Check installed.

### Testing via CI
The GitHub Actions workflow (`.github/workflows/ci.yaml`) automatically runs tests on push:

```bash
# Push to the feature branch
git push -u origin claude/explore-libfyaml-tests-011CUaCE9PzP6ufkz6BHEdZj

# Check CI status on GitHub
# The workflow runs `make check` which includes all test suites
```

### Manual Testing of Specific Functionality
To manually test the underlying functions being tested:

```bash
# Test shell splitting
./src/libfyaml-parser --mode shell-split
# Then type: arg1 'quoted arg' arg3

# Test UTF-8 validation
./src/libfyaml-parser --mode bad-utf8

# Test document building
./src/libfyaml-parser --mode build < test.yaml
```

## Remaining do_* Functions to Extract

### High Priority (Simpler Functions)
- `do_scan()` (lines 595-605) - Token scanning
  - Uses `fy_scan()` and `dump_token()` - mostly output functions
  - Could create tests that verify token types are generated correctly

- `do_copy()` (lines 607-664) - Low-level character copying
  - Uses `fy_parse_get()` for character-level parsing
  - Could test character escape sequences and UTF-8 handling

### Medium Priority (More Complex)
- `do_compose()` (lines 933-948) - Document composition with event callback
  - Uses `fy_parse_compose()` with custom event handler
  - Complex internal path tracking

- `do_dump2()` (lines 697-736) - Alternative dump with document builder
  - Uses `fy_document_builder_load_document()`
  - Similar to existing dump tests but uses builder API

- `do_reader()` (lines 3600-3669) - Low-level reader operations
  - Uses internal reader APIs
  - May require internal headers

### Lower Priority (Very Complex or Internal)
- `do_walk()` (lines 3671-3825) - Path expression walking
  - Uses complex path parser and walker APIs
  - Requires extensive internal API access

- `do_pathspec()` (lines 3243-3287) - Path specification parsing
  - Parses custom path syntax
  - Complex internal structures

- `do_bypath()` (lines 3526-3598) - Path-based queries
  - Uses custom path structures
  - Complex internal logic

- `do_accel_test()` (lines 1677-1692) - Acceleration testing
  - Performance testing function
  - May not be suitable for unit tests

- `do_crash()` (lines 3827-3864) - Crash testing
  - Intentional error generation
  - May not be suitable for unit tests

### LibYAML Compatibility Functions (Skip)
These test libyaml compatibility mode, which is outside the scope:
- `do_libyaml_scan()` (lines 1188-1286)
- `do_libyaml_parse()` (lines 1288-1388)
- `do_libyaml_testsuite()` (lines 1390-1409)
- `do_libyaml_dump()` (lines 1411-1675)

### Timing/Performance Functions (Skip)
- `do_parse_timing()` (lines 3995+) - Performance benchmarking

## Key Learnings and Patterns

### Common API Patterns
```c
// Document creation and cleanup
struct fy_document *fyd = fy_document_build_from_string(NULL, "yaml: content", FY_NT);
ck_assert_ptr_ne(fyd, NULL);
// ... test code ...
fy_document_destroy(fyd);

// Node path queries
struct fy_node *fyn = fy_node_by_path(root, "/path/to/node", FY_NT, FYNWF_DONT_FOLLOW);

// Node value retrieval
const char *value = fy_node_get_scalar0(fyn);

// Event parsing
struct fy_parser *fyp = fy_parser_create(&cfg);
struct fy_event *event;
while ((event = fy_parser_parse(fyp)) != NULL) {
    // Process event
    fy_parser_event_free(fyp, event);
}
fy_parser_destroy(fyp);
```

### API Corrections Made by User
- Path format: `/frooz/0` not `/frooz/[0]`
- Node walk flag: `FYNWF_FOLLOW` not `FYNWF_FOLLOW_ALIASES`
- Node building: `fy_node_buildf()` for key-value, not `fy_node_build_from_string()`
- Event API: `fy_parser_parse()` returns pointer, not `fy_parse_get_event()`
- Tag retrieval: `fy_node_get_tag0()` for null-terminated
- Remove functions: Return removed node for cleanup, not int status
- Empty document: Use `"null"` not `""`

### Required Headers
```c
#include <libfyaml.h>
#include "fy-parse.h"   // For internal parser APIs
#include "fy-utf8.h"    // For UTF-8 utility functions
```

## File Structure

### test/libfyaml-test-parser.c
- Line count: ~1,575 lines (after latest additions)
- Test count: 35 tests
- Organization:
  - Lines 1-22: Headers and includes
  - Lines 24-1508: Test definitions (START_TEST/END_TEST blocks)
  - Lines 1510-1575: Test case registration function

### Key Test Registration Pattern
```c
TCase *libfyaml_case_parser(void)
{
    TCase *tc = tcase_create("parser");

    /* Mapping tests */
    tcase_add_test(tc, parser_mapping_iterator);
    // ... more tests ...

    return tc;
}
```

## Recent Commits

1. **5feab4b** - Add fy-allocators binary to .gitignore
2. **6fa904d** - Expand parser test suite with 11 additional test cases
3. **38f099a** - fix test build (by user)
4. **30a5bfd** - Add iterator and comment tests from libfyaml-parser.c
5. **2b04fa5** - Add event, scalar style, tag, and version tests
6. **c930936** - more tests fixed (by user)
7. **1b33a01** - Add utility function tests for shell splitting and UTF-8 validation (latest)

## Next Steps

1. Extract tests from `do_scan()` for token scanning
2. Extract tests from `do_copy()` for character-level parsing
3. Consider `do_compose()` and `do_dump2()` if they use public APIs
4. Document any functions that cannot be easily tested (complex internal APIs)
5. Wait for CI feedback after each commit to catch API mismatches

## Notes

- Tests compile successfully even without Check library installed
- CI system runs full test suite including these tests
- User fixes API mismatches discovered during CI runs
- Focus on functions that test public or semi-public APIs
- Skip functions that are purely for debugging or performance testing
