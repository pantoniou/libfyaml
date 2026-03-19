# libfyaml Python Binding

Python bindings for libfyaml's generic YAML/JSON value model.

The binding exposes lazy `FyGeneric` wrappers on top of libfyaml's generic API.
It supports value-oriented parsing, access, conversion, and emission while
keeping the same data model used by the C generic runtime.

## Highlights

* YAML and JSON parsing through libfyaml
* lazy wrappers for mappings, sequences, and scalars
* direct correspondence with the C `fy_generic` API
* wheels built from the bundled libfyaml source tree on supported platforms

## Platform Support

The core libfyaml library supports Linux, macOS, FreeBSD, NetBSD, OpenBSD, and
Windows.

The Python packaging in this directory currently targets:

* wheel builds on Linux and macOS
* source builds on Windows, macOS, and the BSDs

On Windows, building from source requires a Clang-family C compiler
(`clang`/`clang-cl`). The binding depends on libfyaml generics, which use
statement expressions not supported by `cl.exe`.

## Build Modes

When the full libfyaml repository is available, the Python extension builds
against a bundled static `libfyaml` produced from the parent source tree.

When the repository sources are not available, the build falls back to a
system-installed `libfyaml` discovered through `pkg-config` or standard system
paths.

## Links

* API reference: `docs/API.md`
* Quickstart: `QUICKSTART.md`
* Project repository: <https://github.com/pantoniou/libfyaml>
