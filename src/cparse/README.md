# cparse - C declaration front end

cparse is a small C front end for the libfyaml reflection support. It reads C
headers without LLVM. It does these tasks:

- It preprocesses the source.
- It parses declarations only: structs, unions, enums, typedefs, fields and
  enumerators. It skips function bodies and initializers.
- It computes the layout of records for a target ABI.
- It keeps source ranges and comments.

cparse does not generate code. It has no expression tree and no statement
parser.

The reflection core uses cparse to parse type text and to spell C
declarations. The cparse reflection backend (`src/reflection/fy-cparse-backend.c`)
builds reflection types from cparse declarations. The libclang backend is
optional.

## Derivation

This code is derived from slimcc (https://github.com/pantoniou/slimcc). slimcc
is a fork of https://github.com/fuhsnn/slimcc, which is derived from chibicc
(https://github.com/rui314/chibicc). This code is not slimcc. Do not report
problems in this code to the slimcc project.

- The fork point is slimcc commit `bb764b16598065bad430da63317e1177bc1a38b3`.
- `LICENSE.slimcc` holds the original MIT license and copyright notices. Each
  derived file repeats them in its header.

### Kept and adapted

| File | From slimcc | Content |
|---|---|---|
| `cparse-tokenize.c` | `tokenize.c` | punctuators, escape sequences, preprocessing numbers, character literals, newline and byte order mark handling |
| `cparse-parse.c` | `parse.c` | declaration specifiers, declarators, struct, union and enum parsing, System V layout (packed, aligned, `#pragma pack`, bit-fields) |
| `cparse-type.c` | `type.c` | builtin types and sizes |
| `cparse-preprocess.c` | `preprocess.c` | directives, include search, include guards, `#pragma once`, builtin macros, `__VA_OPT__`, GNU `, ## __VA_ARGS__` |

### Rewritten

- The target ABI (`cparse-target.c`) is new. slimcc fixes the target to the host.
- Memory: all memory comes from a libfyaml `fy_allocator`, under a tag that the
  translation unit (`struct cp_tu`) owns. Destroying the unit releases it. There
  are no per-object frees, no `exit()` and no global state.
- Errors: the front end records diagnostics in the unit and never prints. It
  never exits. The tokenizer always returns a list that ends in `CPT_EOF`.
- Macro expansion uses hide sets instead of the slimcc lock stack.
- `cparse-expr.c` evaluates integer constant expressions on tokens. It has hooks
  for `sizeof`, `_Alignof`, casts, `offsetof` and enumeration constants.
- Comments go to a side list (`tu->comments`) and do not enter the token
  stream. `cparse-comments.c` attaches them to declarations.
- Only UTF-8 is supported. The code uses `src/util/fy-utf8`. Identifiers accept
  ASCII and valid non-ASCII code points. The front end does not decode
  universal character names or `u""`, `U""` and `L""` literals. It keeps them as
  raw text.

### Removed

Code generation, the driver and option parsing, expression and statement
parsing, variable length arrays, `asm`, `_BitInt`, floating point conversion,
the name prefix extensions, and `unicode.c`.

### Known limits

- `_Pragma` is dropped and `#line` is ignored.
- `__has_builtin`, `__has_feature` and `__has_extension` always give 0.
- `typeof(expression)` and `sizeof expression` are not supported.
- Floating point constants in integer constant expressions are rejected.
- Offsets and columns after a backslash-newline refer to the spliced buffer.
- A comment that follows the closing brace of a struct is not attached to the
  last field, as libclang does.

## Target ABI

A `struct cp_target` gives the sizes, alignments and layout rules.

- The native target is the default. cparse fills it from the compiler that
  built cparse: `sizeof` and `_Alignof` of each builtin type, the member
  alignment of scalars, the signedness of `char`, `__int128` support and the
  `va_list` shape.
- A table holds the targets. Linux: x86_64, i386, aarch64, arm (32 bit, hard
  float), riscv64, riscv32, ppc64le, ppc64, s390x, loongarch64, mips (32 bit) and
  mips64. Windows: x86_64, i386 and aarch64 for MSVC, and for MinGW. macOS:
  x86_64 and aarch64. Fil-C: x86_64 and aarch64 only. WebAssembly: wasm32 (also
  for WASI and for Emscripten) and wasm64. An entry holds the sizes, the alignments, the signedness of
  `char`, the shape of `va_list` and the layout rules that differ: the alignment
  of members, whether an unnamed bit-field aligns its record (ARM), and the
  Microsoft record layout of Windows (`ms_layout`: a bit-field takes a unit of its
  type, a zero width bit-field ends it, an empty record has 4 bytes with MSVC,
  enumerations are `int` with MSVC, and MinGW does not pack a record with
  bit-fields). The tests compare the layouts with libclang.
- A target triple must name Linux, FreeBSD, NetBSD, OpenBSD, bare ELF, Windows
  (MSVC or MinGW), macOS (the iOS triples give the same ABI) or Fil-C (a
  component `filc`, as in `x86_64-unknown-linux-filc`), WebAssembly (`wasm32` or
  `wasm64`, with the system `wasi` or `emscripten`), or hold only the
  architecture. A triple for another operating system, such as Cygwin or AIX, is
  rejected, and so is an architecture that has no entry for the operating system.
- `cp_target_from_args()` accepts the arguments `-target T`, `--target=T`,
  `-m32`, `-m64`, `-funsigned-char` and `-fsigned-char`.
  `cp_target_args_filter()` selects the same arguments from a command line. The
  caller can pass them to the compiler that reports the predefined macros, so
  that both agree.

Constant expressions follow the target. An `L` suffix has the width of `int`
where `long` has 32 bits. `sizeof` has the width of a pointer. The headers
define `wchar_t` and the other target typedefs.

## Supported architectures

The target table and the native target cover the targets of the section above.
`cparse-arch.h` names the target of the build: the architecture, and `-windows`,
`-mingw`, `-darwin`, `-filc`, and `-wasi` or `-emscripten` for wasm32. Fil-C has the Linux ABI
of its architecture; a pointer is 8 bytes. It has its own macros (`__FILC__`),
made from the Fil-C compiler, for x86_64 and aarch64. WebAssembly has the
same layout for WASI and Emscripten, but for one thing: Emscripten aligns
`long double` to 8 bytes. A build with Emscripten (`__EMSCRIPTEN__`) or for WASI
(`__wasi__`) has its own native target.

A build for another architecture or environment has no native target for
cparse. The build systems test for this (`HAVE_CPARSE_BACKEND`). Without a
target the library has no C backend of its own: reflection reads C files only
with libclang (`ENABLE_LIBCLANG`), and the tests of the C backend are not
registered. The rest of reflection (packed blobs, the type system, serialization)
does not change.

To add an architecture, add its entry to the table in `cparse-target.c`, to
`cparse-arch.h` and to the triple names, add its macros to
`cparse-predefs-clang.inc`, and add test cases. Check the entry with libclang.

## Inputs

cparse does not run a compiler. The predefined macros and the system include
directories are compiled in:

- The build runs `gen-predefs.sh host` for the compiler that builds the library.
  The result is the set for the native target, and for `-m32` when the compiler
  has that option. The compiler is `gcc`, `clang` or `unknown`, as its macros
  say.
- `cparse-predefs-clang.inc` holds the macros of clang 20.1.2 for the table
  targets. `gen-predefs.sh targets` makes the file.
- `cp_predefs_find(compiler, target)` selects a set. A set of the build is used
  when it has the compiler (or the compiler is NULL) and the architecture of the
  target. Any other compiler and any other target get the clang set of the
  architecture. The clang sets have no system include directories.

A header that needs the system directories of another compiler or target needs
`-isystem` options.

The caller gives the rest in `struct cp_options`:

- `defines` holds `NAME`, `NAME=VALUE` and `NAME(args)=VALUE` entries. An entry
  `-NAME` removes a macro.
- `include_paths` holds the `-I`, `-isystem` and `-iquote` paths.
- `macro_files` holds files that are read for their macros and then dropped
  (`-imacros`). Use them for the macros of a compiler that is not built in.
- `force_includes` holds the files to process first (`-include`).

The reflection backend uses `cp_predefs_find()`. `compiler` in its
configuration selects the compiler. `no_predefs` disables the compiled-in
macros and directories.

A caller can give the macros of its own compiler. Make the file with the
compiler that builds the program, for example `cc -dM -E -x c /dev/null`, and
give it as `macro_files` in the import configuration or as `-imacros` in the
arguments. Set `no_predefs` to use only these macros. The system header
directories still come from `-isystem` options.

## Comments

`cp_attach_comments()` sets `decl->comment` after parsing. A comment that
follows a field or an enumerator on the same line has priority. Otherwise the
declaration gets the comment block that ends before it with only white space
between them. Consecutive `//` lines form one block.

## Tests

The tests are in `test/cparse`. They run with ctest (`ctest -R cparse`) and
in the automake suite (`testcparse.test`).

`test/cparse/cases.txt` lists the cases. Each case runs `cparse-tool` on an
input file for one target and compares the output with `expected/NAME.txt`.
The inputs cover layout, packing, bit-fields, flexible arrays, anonymous
members, function pointers, enumerations, constant expression types,
nullability, comments, the preprocessor and the compiled-in predefined macros.
The predefined macros are compared with the ABI of the target.

When libclang is enabled, each declaration case also compares the output with
the declarations that libclang gives (`libclang-dump.c` prints them in the
format of `cparse-tool decls`). When a `clang` program is available, the
preprocessor case compares the token stream with `clang -E -P`. A libclang that
does not know a target skips the case for that target.

To change an expected output, run
`test/cparse/run-cparse-test.sh update NAME` with `CPARSE_TOOL` set. Check the
result against libclang first.

`test/cparse/sweep-headers.sh` and `test/cparse/fuzz-layout.py` are manual checks.
The first compares cparse with libclang for all headers in a directory. The
second compares the layout of random records for the targets you name; it finds
some rare mixes of the `aligned` attribute that lower an alignment, and of
`packed` with bit-fields on MinGW.

`test/cparse/fuzz-msvc.py` compares the MSVC layout with the real `cl` compiler.
It builds a program that prints the sizes, offsets and bit positions of random
records. The `cl` can be the one of msvc-wine, which runs under wine.

`test/wasm/run-wasm-tests.sh BUILD_DIR` runs the cparse cases and the C round
trip tests with the library built for WebAssembly by Emscripten, under node
(CMake registers no tests when it cross compiles). The workflow `wasm.yaml` runs it.
The auto allocator of the library cannot use `mremap` there: WebAssembly has
none, and the allocator then grows its arenas in another way.

`test/cparse/fuzz-input.c` feeds damaged text to the front end: a text cut at
every place (a file that ends in a token, a comment, a string or a UTF-8
sequence), invalid UTF-8, NUL bytes, and random changes. It has no oracle: a
crash, a hang or an ASAN or Fil-C stop is the failure. The run does not depend on
the host; the same seed gives the same inputs. It is not in `all` or in the
tests. Build and run it with `cmake --build . --target cparse-fuzz` (set the
cache variable `CPARSE_FUZZ_ARGS` to `-s 7 -n 200000` for a longer run) or with
`make cparse-fuzz` in `test`.

The reflection round trip tests are in `test/reflection-roundtrip`. They
generate C from each header, read the C again and compare the two reflections.
The tests in `test/reflection-macros` read a header with the macros of a file,
as the backend configuration and as `-imacros`, and without the compiled-in
macros.

`cparse-tool.c` is a development tool. It is a test program and not part of the
library:

```
cparse-tool [-I dir] [-isystem dir] [-iquote dir] [-D def] [-U name]
            [-imacros file] [-include file] [-P file]
            [-B compiler] [-V N] [-T triple]
            [-m32|-m64] tokens|comments|pp|decls|check-predefs <file>
```
