# libfyaml v1.0.0-alpha8

`v1.0.0-alpha8` is the durable-storage, comment fidelity, and generic API
follow-up for the 1.0 alpha line.

Highlights:

- Durable allocator and durable dedup support for on-disk generic arenas
- Transparent parse cache storage split into content and dedup cache files
- Auto-anchor emission in the generic API, `fy-tool`, and Python binding
- Improved comment parsing, attachment, and emission for round-trip fidelity
- More generic helpers for paths, equality, joining, deletion, sorting, filtering, interning, signatures, and storage stats
- Windows/static-library packaging fixes and portable mmap/fallocate/rename helpers
- Source tarballs now carry the referenced Sphinx/kernel-doc inputs needed to rebuild release documentation cleanly

## Durable Storage and Cache

This release adds durable allocator APIs with snapshot/restore, checkpoint,
verification, and offline garbage collection support. Dedup can now use durable
storage too, including read-only setup, external allocation config, and stable
canonical identity under races.

The transparent parse cache now keeps content and dedup cache files separate,
which makes the cache path better suited for persistent generic storage.

## Generic API Completion

The generic layer gained value signatures, storage statistics, join helpers,
indirect setters, auto-anchor emission, memoized copy, ID-set support,
deep path set/delete operations, pathseq/pathstr variants, sorted and
null-filtered collection creation, string interning, polymorphic comparison
macros, and raw two-argument getters.

## Comments and Round-Tripping

Comment handling now covers document-level and document-end comments, with
better parser attachment and emitter placement for mapping keys, sequence
items, and block-end positions. See `COMMENT-HANDLING.md` for the design notes.

## Python, Build, and Portability

Python gained auto-anchor emission and comment setter APIs, while `to_python()`
now memoizes conversions to avoid alias-expansion blowups. Build fixes include
installing the static library with shared builds and avoiding Windows static
import-library collisions. Portability work added Haiku endian detection and
portable mmap/fallocate/rename helpers.

The release tarball also includes the documentation sources and mirrored public
headers consumed by the Sphinx/kernel-doc build, so release archives can rebuild
the manual/API documentation during `make distcheck`.

## API Direction

This alpha adds public APIs, so the linker interface version moves to
`7:0:5`. The 1.0 beta direction remains the same: stabilize the core generic,
reflection, cache, and Python surfaces after the alpha feedback cycle.

## Installation

Source tarballs and release artifacts are attached to this release. Standard
CMake, Autotools, and Python packaging flows remain supported.

## Resources

- Full changelog: https://github.com/pantoniou/libfyaml/compare/v1.0.0-alpha7...v1.0.0-alpha8
- Python binding docs: `python-libfyaml/docs/API.md`
