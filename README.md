# libfyaml 1.0-alpha1

[![Autotools CI](https://github.com/pantoniou/libfyaml/workflows/Standard%20Automake%20CI/badge.svg)](https://github.com/pantoniou/libfyaml/actions?query=workflow%3A%22Standard+Automake+CI%22)
[![CMake CI](https://github.com/pantoniou/libfyaml/workflows/CMake%20CI/badge.svg)](https://github.com/pantoniou/libfyaml/actions?query=workflow%3A%22CMake+CI%22)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Language: C](https://img.shields.io/badge/language-C-brightgreen.svg)](https://en.wikipedia.org/wiki/C_(programming_language))

libfyaml is a high-performance YAML 1.2 and JSON parser/emitter with zero-copy
operation, full document and event APIs, and now two major 1.0-alpha1 features:

* generics: a schema-light, sum-type value model for YAML/JSON data in C
* reflection/meta-type: typed YAML <-> C serdes driven by C type metadata

The alpha release adds a clear progression:

* use the core API when you need parser, emitter, event, or document-tree control
* use generics when your problem is "work with values"
* use reflection when your problem is "populate native C data structures"

## Why 1.0-alpha1 matters

### Generic runtime

The center of the generic API is `fy_generic`.

`fy_generic` is the sum-type value used to represent YAML and JSON data in C.
It carries one runtime value of one type: null, bool, int, float, string,
sequence, mapping, or YAML-specific wrappers.

It is a single pointer-sized word with inline storage for common small values,
including 61-bit signed integers on 64-bit builds, short strings, and inline
32-bit floats.

The rest of the generic API is about working with `fy_generic` values:

* creating `fy_generic` values from C literals or parsed input
* reading typed values back out
* transforming one `fy_generic` into another
* controlling lifetime through stack-local values and builders

That gives C a Python-like data model:

* scalars, sequences, and mappings as immutable tagged `fy_generic` values
* construction via ``fy_value()``, ``fy_sequence()``, and ``fy_mapping()``
* parse/emit helpers for YAML and JSON
* functional collection operations such as map, filter, and reduce

If you know Python ``dict`` / ``list`` workflows, ``serde_json::Value``,
tagged unions, or other sum-type/value-tree APIs, generics are the direct fit.

### Reflection / meta-type

The reflection subsystem provides schema-driven typed serdes:

* extract type metadata from annotated C headers
* deserialize YAML directly into native C structs
* emit native C structs back to YAML
* inspect type and field metadata through the public reflection API
* choose between direct libclang authoring or packed metadata blobs

Reflection is the typed layer for stable C data models.

### Python binding

The Python binding in [`python-libfyaml/`](python-libfyaml/) is built on the
generic runtime. It is a direct bridge into the C generics API:

* Python ``FyGeneric`` lazy wrappers mirror C ``fy_generic`` values
* the binding demonstrates dict/list/scalar usage over the same data model
* users can move from Python prototypes to C without changing how they think
  about the data

See the binding reference at
[`python-libfyaml/docs/API.md`](python-libfyaml/docs/API.md).

## Which layer should I use?

### Core API

Choose the core library when you need:

* event-streaming parsing
* YAML document tree access and mutation
* path queries and document-building helpers
* full control over emission details and original YAML structure

### Generic API

Choose generics when you need:

* Python-like data handling in C
* a schema-less or schema-light value layer
* transformations over YAML/JSON values
* a common model shared with the Python binding

### Reflection API

Choose reflection when you need:

* direct YAML <-> C struct serdes
* stable typed configuration objects
* metadata-aware array/mapping handling
* deployable packed schemas without runtime libclang dependency

## Quick look

### Generic literals in C

```c
#include <libfyaml/libfyaml-generic.h>

fy_generic config = fy_mapping(
    "server", fy_mapping(
        "host", "localhost",
        "port", 8080,
        "tls",  true),
    "features", fy_sequence("http", "metrics", "admin"));
```

### Generic parse and transform

```c
fy_generic doc = fy_parse(
    "values: [1, 2, 3, 4]",
    FYOPPF_DISABLE_DIRECTORY | FYOPPF_INPUT_TYPE_STRING,
    NULL);

fy_generic values = fy_get(doc, "values", fy_invalid);
fy_generic first = fy_first(values);
```

### Reflection-based typed parse

```c
#include <libfyaml/libfyaml-reflection.h>

struct fy_reflection *rfl = fy_reflection_from_c_file_with_cflags(
    "schema.h", "", false, true, NULL);

struct fy_type_context_cfg cfg = {
    .rfl = rfl,
    .entry_type = "struct app_config",
};
struct fy_type_context *ctx = fy_type_context_create(&cfg);
```

## Documentation roadmap

Start with these pages:

* [`doc/generics-guide.rst`](doc/generics-guide.rst): value model, schemas, lifetimes
* [`doc/reflection-guide.rst`](doc/reflection-guide.rst): typed serdes, libclang, packed blobs

Reference pages:

* [`doc/libfyaml-core.rst`](doc/libfyaml-core.rst)
* [`doc/libfyaml-generics.rst`](doc/libfyaml-generics.rst)
* [`doc/libfyaml-reflection.rst`](doc/libfyaml-reflection.rst)

## Examples

The refreshed examples directory now covers the new alpha workflows:

* generic literals and Python-like object construction
* generic parse/transform/reduce flows
* generic lambda examples with captured local variables
* generic serial and parallel lambda-based filter/map/reduce flows with an
  explicit thread pool and configurable workload size
* schema-sensitive generic round-trips
* a Python-binding-to-C adoption bridge
* reflection from libclang-processed headers
* reflection export to packed blobs and runtime load from packed metadata

See [`examples/README.md`](examples/README.md) for the full list.

## Existing strengths still apply

libfyaml also remains:

* a full YAML 1.2 and JSON parser/emitter
* zero-copy in core parsing paths
* free of artificial key/document size limits
* strong on diagnostics and document manipulation
* fully MIT licensed

## Installation

### Using CMake

```cmake
find_package(libfyaml 1.0 REQUIRED)
target_link_libraries(your_app PRIVATE libfyaml::libfyaml)
```

If the installed package was built with libclang support, the CMake package also
exports ``libfyaml_HAS_LIBCLANG``.

### Using pkg-config

```bash
pkg-config --cflags libfyaml
pkg-config --libs libfyaml
```

### Building from source

Using CMake:

```bash
mkdir build && cd build
cmake ..
cmake --build .
ctest --progress -j"$(nproc)"
```

Using Autotools:

```bash
./bootstrap.sh
./configure
make
make check
```

### Optional dependencies

* `llvm-dev libclang-dev`: author reflection metadata directly from C headers
* no runtime libclang is required when using packed reflection blobs

### Documentation builds

Sphinx documentation targets require the Python documentation toolchain:

* `sphinx`
* `sphinx_rtd_theme`
* `sphinx-markdown-builder`
* `linuxdoc`

PDF documentation also requires a LaTeX toolchain with:

* `latexmk`
* `pdflatex`
* `xcolor.sty`
* `wrapfig.sty`

On Debian/Ubuntu, the practical package set is:

```bash
python3 -m pip install sphinx sphinx_rtd_theme sphinx-markdown-builder linuxdoc
sudo apt-get install latexmk texlive-latex-base texlive-latex-recommended texlive-latex-extra
```

Then build the docs with:

```bash
cmake --build build --target doc-html
cmake --build build --target doc-latexpdf
```

## Python binding

The binding lives in [`python-libfyaml/`](python-libfyaml/). Run its tests with:

```bash
cd python-libfyaml
python3 -m pytest tests/
```

The binding is part of the alpha release story and shows the generic runtime's
data model in regular use.

## License

libfyaml is fully MIT licensed.
