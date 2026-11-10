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
| Durable arenas, persistent dedup, durable GC | Work, including sharing arenas with native processes. At the configured base with a runtime that has `zgc_reserve_at()`, at a runtime chosen address (logical base) with any other |
| Reflection (built-in C parser, cparse) | Works; the cparse tests pass |
| Reflection (libclang) | Not available: libclang is a host library |
| Python bindings | Not built |

The full test suite passes.

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
cmake -S . -B build-filc \
    -DCMAKE_C_COMPILER=$FILC/build/bin/clang \
    -DCMAKE_CXX_COMPILER=$FILC/build/bin/clang++ \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS=-O1
cmake --build build-filc -j8
```

CMake detects FIL-C. Since nothing built by the host compiler can be linked
into a FIL-C program, it then looks for no host packages (the `check`
unit-test library is fetched and built with FIL-C), and turns off the
CPU-specific SIMD code, libclang (a host library) and the Python bindings.
Reflection stays on and reads C with the built-in parser, which has the targets
`x86_64-filc` and `aarch64-filc`.

Use `-DCMAKE_BUILD_TYPE=Release` for timing runs.

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

All tests pass. Some print `SKIP:` under FIL-C: user namespaces, and two mremap
stress tests that trip a FIL-C runtime bug (set `FY_TEST_FILC_STRESS=1` to run
them anyway). With a runtime that does not have `zgc_reserve_at()` the
fixed-address reservation tests print `SKIP:` too.

### Cross-runtime durable tests

The `durable-probe` test runs durable arena operations in separate
processes. Give a FIL-C build the probe of a native build (or the reverse)
and the operations alternate between the two runtimes on the same arenas:

```sh
cmake -S . -B build-native
cmake --build build-native --target durable-probe -j8
cmake build-filc -DDURABLE_PEER_PROBE=$PWD/build-native/test/durable-probe
cmake --build build-filc -j8
cd build-filc && LD_LIBRARY_PATH=$FILC/pizfix/lib ctest -L durable-probe
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
- **Range registry** (`src/allocator/fy-range-registry.[ch]`). Under FIL-C,
  allocators register the memory they obtain (linear buffers, mremap
  arenas, durable regions) together with a pointer that carries its
  capability. Resolving an address in a registered range rebuilds the
  pointer from it. Lookups and updates take no locks. Natively nothing
  registers.
- **Other memory** (single malloc objects, `alloca` storage, static
  literals, in-place builders on stack buffers) goes through a FIL-C
  `zexact_ptrtable` when a value is encoded.
- **Durable arenas.** Persisted data uses the configured `region_base` as a
  *logical* address: chunk links, the persistent dedup index and generic
  values stored in the arena hold logical addresses, as integers that the
  registry turns back into pointers, because a pointer stored in a file does
  not keep its capability. Natively the logical address is the runtime
  address, so FIL-C and native processes share the same files. Where the
  region goes depends on the runtime:
  - With `zgc_reserve_at()` (`FILC_HAS_ZGC_RESERVE_AT` in `<stdfil.h>`) the
    region is reserved at `region_base`, exactly as natively. A base that is
    in use fails with `region base ... is unavailable`, as natively.
  - Without it, or for a base that the runtime cannot place (`EINVAL`), the
    region is reserved wherever the runtime puts it and `region_base` stays
    only logical.
- **Relocation** (generic images, builder linearization, parse cache) derives
  relocated pointers from the destination buffer instead of integer
  arithmetic, and the parse cache always loads through its copying path.

## Fixed addresses and alignment

`zgc_reserve_at(address, length)` returns an `mmap`-style object at exactly
`address`, or `NULL` with `errno`. The runtime keeps its own data for the
object next to it, and that decides where a region can go:

- The runtime keeps a header just below `address`, with mark bits in the first
  page of the 512 KiB chunk that header is in, and it takes whole chunks. So
  the chunk below the base and the rest of the chunk that the region ends in
  must be free too. A base that is a multiple of 512 KiB always works. A base
  in the first two pages of a chunk is refused (`EINVAL`) and the region falls
  back to a runtime chosen address.
- Regions cannot touch: leave at least a chunk between one region and the
  next. The durable GC does that for its scratch region.
- A closed arena keeps its address until the GC has found that nothing
  refers to it. Reopening at the same base in the same process therefore
  waits for that, collecting garbage up to 5 times, and fails if something
  still points into the old arena, **including a dead local in a frame that
  is still running**, since the runtime scans every slot of a frame, and the
  pointers that a frame passed to a call stay in it until another call
  overwrites them. Do the first session in a function of its own, so that its
  frame is gone. A stale pointer then can never see the new arena.
- A program that reopens an arena deep in its call stack can start the reopen
  on a fresh stack: `zcall_on_fresh_stack(callback, arg)`
  (`FILC_HAS_ZCALL_ON_FRESH_STACK` in `<stdfil.h>`) drops every frame of the
  thread and runs the callback on a new stack. The reopen must be the start of
  that callback, before functions that hold pointers into the arena have run.
  To find what keeps an old arena alive, `zdump_heap(fd)` lists what reaches
  each object; the source `thread` is a frame.

## Limitations

- **Raw pointers do not survive a durable reopen.** A pointer stored in a
  file loses its capability, even where the arena is at the same address.
  Publish a root generic with `fy_generic_refs_publish()`, or a handle from
  `fy_allocator_ptr_to_handle()`, as the refs head, never a runtime pointer.
- **No `mmap` at an address hint.** `mmap` with a non-NULL address that is not
  inside an object it returned is refused. Use `zgc_reserve_at()` to place
  memory at a chosen address; it is not in release builds of 0.687.
- **Growing an mremap arena in place never succeeds**; the allocator makes
  new arenas instead, which costs some memory. For the same reason arenas are
  mapped at their size and not as a larger "balloon" that is trimmed back: a
  mapping is an object that keeps its size under FIL-C, and so does the aux
  area that holds the capabilities of the pointers stored in it, which every
  collection scans. So the first arena of a builder is 1 MiB, an arena is
  never grown in place, and an allocation of 1 MiB or more gets an arena of
  its own size that does not make the arenas after it large.
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

Both of the first two were checked again against the tip of the FIL-C
repository (`f641231a`, November 2026), and are still there.

- **libpas assertion `!node->is_booted`** (`pas_large_sharing_pool.c:950`)
  under heavy multi-threaded use of the mremap allocator, while the runtime
  allocates the capability ("aux") area of a large mapping on the first
  pointer store into it. The affected stress tests are skipped.
- **`syscall()` returns -1 as 4294967295.** The value is zero-extended from
  32 bits. libfyaml stores raw syscall results in `int`, which hides it.
- **musl has no `RENAME_NOREPLACE`**; libfyaml uses the raw `renameat2`
  system call instead.
