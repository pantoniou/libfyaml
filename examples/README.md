# libfyaml examples

This directory now covers both the long-standing core APIs and the 1.0-alpha1
feature set built around generics and reflection.

## Building

### Using CMake

```bash
cd examples
mkdir build && cd build
cmake ..
cmake --build .
```

### Using pkg-config manually

```bash
gcc -std=gnu2x -o generic-literals generic-literals.c \
    $(pkg-config --cflags --libs libfyaml)
```

When examples are built against the exported CMake package, reflection examples
are enabled according to the features present in the installed libfyaml build.

## Core examples

### `intro-core-update.c`

Small core-library example used by the documentation introduction:

* parse `intro-config.yaml`
* update `/port` from `80` to `8080`
* emit the updated document

### `quick-start.c`

Document API walkthrough for parsing, querying, updating, and emitting YAML.

### `basic-parsing.c`

Basic YAML parsing with multiple output modes.

### `path-queries.c`

Path-based lookups with the document API.

### `document-manipulation.c`

Document updates and sorted emission.

### `event-streaming.c`

Low-level event API example for memory-efficient streaming.

### `build-from-scratch.c`

Construct a document tree programmatically.

## Generic runtime examples

### `intro-generic-update.c`

Small generic example used by the documentation introduction:

* parse `intro-config.yaml`
* update `"port"` from `80` to `8080`
* emit the updated value tree

### `generic-literals.c`

Shows the "Python-like data model in C" story directly:

* `fy_mapping()` and `fy_sequence()` literals
* stack-local versus builder-backed values
* typed access with `fy_get()` and `fy_len()`

### `generic-transform.c`

A value-oriented workflow using the generic API:

* parse YAML into generics
* select a nested sequence
* filter, map, and reduce values
* install derived values back into the root mapping

### `generic-lambda-capture.c`

Focused lambda example for the generic API:

* use `fy_filter_lambda()`, `fy_map_lambda()`, and `fy_reduce_lambda()`
* reference local variables such as thresholds, prefixes, and bonuses directly
  inside the lambda expressions
* emit the captured values alongside the transformed result

### `generic-parallel-transform.c`

The same value-oriented workflow, but with an explicit thread pool:

* create a `fy_thread_pool`
* use `fy_filter_lambda()`, `fy_map_lambda()`, `fy_reduce_lambda()`
* use `fy_pfilter_lambda()`, `fy_pmap_lambda()`, and `fy_preduce_lambda()`
* compare serial and parallel phase timings over the same immutable value tree
* defaults to 32768 items and accepts `item-count` and `thread-count`
  command-line arguments

On GCC, the lambda forms use nested functions. The build checks for heap
trampoline support and enables it automatically when available, which avoids
the executable-stack warning on newer GCC releases. On Clang, the lambda forms
use Blocks when Blocks support is enabled.

### `generic-roundtrip.c`

Shows that schemas affect scalar typing:

* parse the same document under YAML 1.2 and YAML 1.1 PyYAML-compatible rules
* inspect the resulting generic types
* emit normalized output

### `generic-adoption-bridge.c`

Bridges the Python binding and the C generic runtime:

* build the same value tree from literals and from parsed YAML
* compare the results
* show the same dict/list/scalar shape on the C side

## Reflection examples

These use the sample schemas in `reflection-config.h` and
`reflection-intro-config.h`, and the corresponding YAML inputs in
`reflection-config.yaml` and `intro-config.yaml`.

### `intro-reflection-update.c`

Small reflection example used by the documentation introduction:

* reflect `struct server_config` from `reflection-intro-config.h`
* parse `intro-config.yaml` into typed C data
* update `cfg->port` from `80` to `8080`
* emit the updated typed result

This example is only built when libfyaml was built with libclang support.

### `reflection-packed.c`

Runtime typed parse/emit using a packed reflection blob.

Typical flow:

1. generate a blob with `reflection-export-packed`
2. load the blob with `reflection-packed`
3. parse YAML into `struct service_config`

### `reflection-libclang.c`

Direct libclang-backed authoring path:

* load reflection from `reflection-config.h`
* create a `fy_type_context`
* parse YAML directly into native C data
* emit the typed data back to YAML

This example is only built when libfyaml was built with libclang support.

### `reflection-export-packed.c`

Exports packed reflection metadata from `reflection-config.h` so it can be
loaded later without runtime libclang dependency.

This example is only built when libfyaml was built with libclang support.

## Reflection sample data

### `reflection-config.h`

Annotated schema used by the reflection examples.

### `reflection-config.yaml`

Sample YAML that maps into `struct service_config`.

### `reflection-intro-config.h`

Simple typed schema used by `intro-reflection-update.c`.

### `intro-config.yaml`

Small `host` / `port` input used by the introduction examples.

## Suggested reading order

1. `intro-core-update.c`
2. `intro-generic-update.c`
3. `generic-literals.c`
4. `generic-transform.c`
5. `generic-lambda-capture.c`
6. `generic-parallel-transform.c`
7. `generic-roundtrip.c`
8. `generic-adoption-bridge.c`
9. `intro-reflection-update.c`
10. `reflection-libclang.c`
11. `reflection-export-packed.c`
12. `reflection-packed.c`
