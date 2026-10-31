# Building and testing libfyaml for WebAssembly

libfyaml builds for WebAssembly with [Emscripten](https://emscripten.org/). The
programs and the tests run under [node](https://nodejs.org/).

## Build

Install the emsdk and set its environment, then give CMake the width of the
pointers:

```sh
source ~/emsdk/emsdk_env.sh
cmake -G Ninja -S . -B build-wasm -DWASM=32 -DBUILD_SHARED_LIBS=OFF -DENABLE_LIBCLANG=OFF
cmake --build build-wasm
```

`-DWASM=32` is wasm32 and `-DWASM=64` is wasm64 (64 bit pointers). The module
`cmake/EmscriptenCrossCompile.cmake` finds the toolchain from `EMSDK` or from
`emcc` in `PATH`. The tests use the node of the emsdk (`EMSDK_NODE`) when there is
one. wasm64 needs node 23 or newer, which the emsdk has.

`emcmake cmake ...` also works for wasm32, without `-DWASM`.

## Test

```sh
cd build-wasm
ctest -j$(nproc)
```

`ctest` runs the unit tests, the YAML test suites, the emitter tests and the
reflection tests under node. The tests of the C front end (cparse) run the tools
directly, so `test/wasm/run-wasm-tests.sh build-wasm` runs them.

## What the build does

- The programs have threads (the thread pools, the BLAKE3 hashing and the parallel
  operations of the generics need them). `PROXY_TO_PTHREAD` runs `main()` in a
  thread, so that it can wait while node starts the workers.
- The programs read the files of the host (`NODERAWFS`) and have a stack of 8 MiB.
- `cmake/emscripten-node.js` gives a program the environment of node. node makes a
  pipe non-blocking, so a write to a slow reader fails with `EAGAIN`, and the script
  makes the system call wait. stdio cannot go on after that without losing data.
- `isatty()` of Emscripten says yes for the standard streams, also for a pipe, so
  the library asks node (`fy_isatty()`).
- The system has no `mremap()` and no hard links: the allocators do not use
  `mremap`, and `fy_rename()` checks the target and then renames.

## Status

wasm32 passes the tests, but for the durable arenas: a durable arena needs a
fixed address in a 64 bit address space, so those tests do not run.

wasm64 is experimental. It builds and most tests pass. The tests of the durable
arenas and a few reflection, emitter and generic tests fail.
