# Building and testing libfyaml with FIL-C

[FIL-C](https://fil-c.org/) is a memory-safe implementation of C. Every
pointer carries a hidden capability; out-of-bounds accesses, use after free
and dereferences of pointers forged from integers stop the program with a
diagnostic instead of corrupting memory. libfyaml builds and runs under it,
which makes it a strong extra check on top of ASAN and valgrind.

This guide covers FIL-C 0.687 on Linux x86-64.

## Status

| Area | Status |
| --- | --- |
| Parser, document tree, emitter | Works |
| Generics (builders, ops, emit, images, parse cache) | Works |
| Allocators (linear, malloc, mremap, dedup, auto) | Works |
| Durable arenas | Work, including sharing arenas with native processes |
| Durable arenas with persistent dedup, durable GC | Not supported yet (`ENOTSUP`) |
| Reflection (libclang) | Not tested; build it without |
| Python bindings | Not built |

The full test suite passes except five durable-arena tests that need the
two unsupported features above (see [Expected test results](#expected-test-results)).

## Getting the toolchain

Download the musl-based distribution and check it:

```sh
curl -LO https://github.com/pizlonator/fil-c/releases/download/v0.687/filc-0.687-linux-x86_64.tar.xz
echo "aab538401f78a47ab261ee79afed8c6b99e1d4848e263ae159a75b03c1288da1  filc-0.687-linux-x86_64.tar.xz" | sha256sum -c
tar xJf filc-0.687-linux-x86_64.tar.xz
export FILC=$PWD/filc-0.687-linux-x86_64
```

Then run its `setup.sh`, which needs `patchelf`. Without `patchelf`, do the
only part libfyaml needs by hand, the kernel header links, and use
`LD_LIBRARY_PATH` for the runtime libraries:

```sh
mkdir -p $FILC/pizfix/os-include
cd $FILC/pizfix/os-include
ln -s /usr/include/linux linux
ln -s /usr/include/x86_64-linux-gnu/asm asm      # or /usr/include/asm
ln -s /usr/include/asm-generic asm-generic
cd -
export LD_LIBRARY_PATH=$FILC/pizfix/lib
```

The compiler is `$FILC/build/bin/clang` (and `clang++`).

## Building libfyaml

```sh
mkdir -p empty-pc
PKG_CONFIG_LIBDIR=$PWD/empty-pc PKG_CONFIG_PATH= \
cmake -S . -B build-filc \
    -DCMAKE_C_COMPILER=$FILC/build/bin/clang \
    -DCMAKE_CXX_COMPILER=$FILC/build/bin/clang++ \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS=-O1 \
    -DENABLE_PORTABLE_TARGET=ON \
    -DENABLE_LIBCLANG=OFF -DENABLE_REFLECTION=OFF \
    -DENABLE_PYTHON_BINDINGS=OFF \
    -DENABLE_NETWORK=OFF \
    -DCMAKE_DISABLE_FIND_PACKAGE_check=ON
cmake --build build-filc -j8
```

Why these options:

- **`PKG_CONFIG_LIBDIR` / `CMAKE_DISABLE_FIND_PACKAGE_check`**: nothing built
  by the host compiler can be linked into a FIL-C program. These hide the
  host's `check` unit-test library, so CMake fetches and builds it with
  FIL-C instead.
- **`ENABLE_PYTHON_BINDINGS=OFF`**: otherwise the build picks up the host
  Python headers.
- **`ENABLE_LIBCLANG=OFF`, `ENABLE_REFLECTION=OFF`**: libclang is a host
  library.
- **`ENABLE_PORTABLE_TARGET=ON`**: no CPU-specific SIMD paths.
- **`ENABLE_NETWORK=OFF`**: the YAML test suite download is optional.

Use `-DCMAKE_BUILD_TYPE=Release` for timing runs.

**Always reconfigure with the same environment.** Running a plain
`cmake .` in the build directory without `PKG_CONFIG_LIBDIR` lets host
packages into the cache; the visible symptom is every emitter test failing
with `Error: fy-tool not found or not executable`. Delete the build
directory and configure it again.

## Running the tests

```sh
ulimit -c 0          # see below
cd build-filc
LD_LIBRARY_PATH=$FILC/pizfix/lib ctest -j8
```

A FIL-C safety stop raises `SIGTRAP`. On Ubuntu, apport then collects a
crash dump for every one, which takes seconds each; `ulimit -c 0` avoids
that.

Single unit tests run by name:

```sh
cd build-filc/test
LD_LIBRARY_PATH=$FILC/pizfix/lib CK_FORK=no ./libfyaml-test generic_compare
```

### Expected test results

All tests pass except these five, which need persistent dedup or raw
pointers published as the refs head of a durable arena:

```
libfyaml/durable-arena/durable_combined_no_index
libfyaml/durable-arena/durable_separate_index_cross_session
libfyaml/durable-arena/durable_separate_index_grow
libfyaml/durable-arena/refs_cross_session
libfyaml/durable-arena/refs_checkpoint_crash
```

Some tests print `SKIP:` under FIL-C (fixed-address reservations, user
namespaces, durable GC, and two mremap stress tests that trip a FIL-C
runtime bug).

### Cross-runtime durable tests

`test/filc/run-durable.py` checks that FIL-C and native processes can share
durable arenas (reading, appending, concurrent writers, checkpoints). It
needs a FIL-C build and a native build of the shared library:

```sh
cmake -S . -B build-native -DENABLE_PORTABLE_TARGET=ON \
    -DENABLE_LIBCLANG=OFF -DENABLE_REFLECTION=OFF -DBUILD_TESTING=OFF
cmake --build build-native --target fyaml -j8
python3 test/filc/run-durable.py $FILC build-filc build-native /tmp/filc-durable-results
```

## Using libfyaml from your own FIL-C program

```sh
$FILC/build/bin/clang -O2 -I include -I build-filc -o prog prog.c \
    -L build-filc -lfyaml
LD_LIBRARY_PATH=build-filc:$FILC/pizfix/lib ./prog
```

Your program and every library it links must be built with FIL-C.

## How libfyaml works under FIL-C

FIL-C forbids two things libfyaml relied on: turning an integer back into a
usable pointer, and mapping memory at an address given as an integer. The
port handles them as follows. Native builds compile to the same code as
before; everything below is under `#ifdef __FILC__` or computes identical
values natively.

- **Generic values.** An out-of-place `fy_generic` is a tagged address in an
  integer. Every value is now created by `fy_generic_encode_ptr()` and read
  by `fy_generic_resolve_ptr()` / `fy_generic_resolve_collection_ptr()`.
  Under FIL-C these call into the library, which recovers the pointer with
  its capability.
- **Range registry** (`src/allocator/fy-range-registry.[ch]`). Allocators
  register the memory they obtain (linear buffers, mremap arenas, durable
  regions) together with a pointer that carries its capability. Resolving an
  address in a registered range rebuilds the pointer from it. Lookups and
  updates take no locks. Natively the registry also maps an address back to
  its owning allocator.
- **Other memory** (single malloc objects, `alloca` storage, static
  literals, in-place builders on stack buffers) goes through a FIL-C
  `zexact_ptrtable` when a value is encoded.
- **Durable arenas.** The region is reserved wherever the runtime places it.
  Persisted data keeps using the configured `region_base` as a *logical*
  address: chunk links, and generic values stored in the arena, hold logical
  addresses, which the registry translates. Natively the logical address is
  the runtime address, so FIL-C and native processes share the same files.
- **Relocation** (generic images, builder linearization, parse cache) derives
  relocated pointers from the destination buffer instead of integer
  arithmetic, and the parse cache always loads through its copying path.

## Limitations

- **Raw pointers do not survive a durable reopen.** Under FIL-C the arena
  lands at a different address in every process. Publish generic values (or
  logical addresses) as the refs head, not runtime pointers.
- **No fixed-address mappings.** `mmap` with a non-NULL address hint is
  refused, so nothing can be placed at a chosen address.
- **Persistent dedup and durable GC** return `ENOTSUP`.
- **Growing an mremap arena in place never succeeds**; the allocator makes
  new arenas instead, which costs some memory.
- **Speed.** FIL-C is roughly 5 to 7 times slower than a native release
  build for parsing and emitting (4.7x for parse + dump, 6.9x for event
  streaming, which allocates many small objects). Generic values in memory
  that no allocator registered pay a pointer-table lookup.

## Reading FIL-C diagnostics

A safety stop prints the failing access and a stack:

```
filc safety error: cannot read pointer with null object.
    pointer: 0x7bcbd2997550,<null>
```

- **`with null object`, address looks valid**: a pointer was made from an
  integer. In libfyaml that means a generic value or an address that did not
  go through the encode/resolve functions.
- **`ptr >= upper` / `ptr < lower` far outside the object**: a pointer
  computed by integer arithmetic kept one object's capability but got
  another object's address (relocation by a delta, for example).
- **`filc user error: ... bad syscall`**: the runtime refuses that system
  call (`unshare()`, for example).
- **`filc panic: src/libpas/...`**: an internal runtime assertion, not a
  safety check.

The panic stack shows where the access happened; for a runtime panic
without one, run under `gdb` and take a backtrace at the `SIGTRAP`.

## Known FIL-C 0.687 issues

- **libpas assertion `!node->is_booted`** (`pas_large_sharing_pool.c:950`)
  under heavy multi-threaded use of the mremap allocator, while the runtime
  allocates the capability ("aux") area of a large mapping on the first
  pointer store into it. The affected stress tests are skipped.
- **`syscall()` returns -1 as 4294967295.** The value is zero-extended from
  32 bits. libfyaml stores raw syscall results in `int`, which hides it.
- **musl has no `RENAME_NOREPLACE`**; libfyaml uses the raw `renameat2`
  system call instead.
