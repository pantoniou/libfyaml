# FIL-C durable storage experiment

This is an experimental port of the **durable allocator substrate**, not a
complete libfyaml/fyai port. It was tested on Linux x86-64 using unmodified
FIL-C 0.687 (Clang 20.1.8), with native GCC as an interoperability control.
The source base was `be739ea0e815295a598f6ed4fb2c4e070bbb2fbc`.

The final gated matrix passed **42/42 checks**, with the FIL-C library and
probes compiled at `-O2`. Full output is in `results/`.

## What changed

FIL-C rejects a fresh fixed-address reservation made from an integer address.
Its `filc_native_zsys_mmap` requires a live mapping capability for any non-null
address (even for a hint); NULL requests create a new managed mapping.

Under `__FILC__`, this branch therefore:

- Reserves the arena at a runtime-selected address with `mmap(NULL, ...)`.
- Keeps `region_base` as the stable **logical** address space used by disk data.
- Reads/writes persisted chunk-head and next-chunk links as integer atomics,
  encoding/decoding them against the region's real mapping capability.
- Maps chunk files within that live reservation using MAP_FIXED, as before.
- Preserves the returned reservation pointer for boot-page access.
- Rejects persistent dedup and graph compaction explicitly with ENOTSUP, because
  their pointer graphs/context-free generic accesses have not been ported.

Native builds keep the original fixed runtime addresses and pointer operations.
The 64-bit version-4 disk layout is unchanged. Native and FIL-C executables
successfully read, append, and verify each other's test arenas, including
concurrent mixed-runtime writers.

**Compatibility boundary:** public integer region_base/refs values are logical
addresses under FIL-C. They cannot be cast to live pointers. This is deliberately
an experimental branch: a context-aware public resolver or owning-range registry
is still needed before existing clients can use it unchanged. The probe uses
private allocator state for that translation; no new public ABI is proposed yet.

## Test coverage

- Create, publish refs head with durability ordering, destroy and reopen.
- Grow 25 records across nine 64-KiB chunk files and validate all payload bytes.
- Compare-and-swap success and stale-expected failure without head corruption.
- Read-only reopening and refusal to publish through a read-only allocator.
- Abrupt `_exit` after publish/checkpoint ordering, followed by fresh-process reopen.
  This tests process termination, **not power-loss durability**.
- Four pthread writers sharing an allocator, with retained immutable record chains.
- Four separately started FIL-C writer processes plus a read-only observer.
- Concurrent native and FIL-C writers plus a FIL-C read-only observer.
- All writer processes and the observer wait at a gate after opening the arena;
  the observer checks the initial root, then monotonic consistent prefixes.
- Final record counts, identifier sums, scalar values, and every payload byte.
- Checkpoint creation, verification after reopen, and rollback across chunk growth.
- Native verification of FIL-C checkpoints and FIL-C verification of native checkpoints.
- FIL-C use-after-destroy detection, including an old pointer retained across reopen.
- Explicit unsupported results for persistent dedup and graph compaction;
  the unsupported compaction call leaves the tested root/data intact.

All transient staging uses `fy_allocator_create("malloc", NULL)` explicitly.
The mremap allocator is not used or ported. Default allocator selection in other
library paths is unchanged. The records contain actual inline `fy_generic` scalar
values, but this does not test the complete generic builder/parser API.

The integer-only durable region's capability dump contains no `aux=` field.
That is consistent with no per-value pointer-shadow metadata in these mappings;
this is not a quantitative memory/performance benchmark.

## Reproduce

Use the 0.687 self-contained pizfix distribution:

https://github.com/pizlonator/fil-c/releases/download/v0.687/filc-0.687-linux-x86_64.tar.xz

SHA-256: `aab538401f78a47ab261ee79afed8c6b99e1d4848e263ae159a75b03c1288da1`.
Follow its setup instructions, including the Linux UAPI header symlinks under
`pizfix/os-include`. CMake, make, Python 3, and native GCC are needed.

From the repository root, with FILC pointing to the extracted distribution:

```sh
export FILC=/absolute/path/filc-0.687-linux-x86_64
export TMPDIR=/absolute/path/writable-temporary-directory
export LD_LIBRARY_PATH="$FILC/pizfix/lib"
cmake -S . -B build-filc \
  -DCMAKE_C_COMPILER="$FILC/build/bin/clang" \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS=-O2 \
  -DENABLE_PORTABLE_TARGET=ON -DENABLE_LIBCLANG=OFF \
  -DENABLE_REFLECTION=OFF -DBUILD_TESTING=OFF
cmake --build build-filc --target fyaml -j4
cmake -S . -B build-native \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_PORTABLE_TARGET=ON -DENABLE_LIBCLANG=OFF \
  -DENABLE_REFLECTION=OFF -DBUILD_TESTING=OFF
cmake --build build-native --target fyaml -j4
python3 test/filc/run-durable.py "$FILC" build-filc build-native /absolute/path/results
```

The runner creates fresh test arenas, builds the probes, disables core dumps,
checks expected signals/diagnostics, and writes logs and `summary.json`.
Expected failures are isolated subprocesses. Compiler and runtime binaries,
large arena files, and build directories are not committed.

## Remaining work

- General-purpose public handle resolution, including nested out-of-place
  generics, cross-arena values and transient heap/stack-backed generics.
- Persistent dedup's separate pointer graph and index region.
- Generic graph copying/compaction; mremap's full functionality remains out of scope.
- A full existing regression/fuzzing suite and production-level corrupt-file handling.
- Concurrent whole-arena teardown. Current tests wait for workers before destruction.
- Documentation/API decisions for logical addresses versus runtime pointers.

Capabilities enforce the reservation boundary and whole-reservation retirement.
They do not revoke individual values on rollback, nor enforce object-level bounds
between neighboring allocations within a live reservation. Publication and
retirement remain separate from persistent reachability/graph compaction.
