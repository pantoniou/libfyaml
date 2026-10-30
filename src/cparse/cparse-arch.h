/*
 * cparse-arch.h - the architecture of the machine a program is built for
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CPARSE_ARCH_H
#define CPARSE_ARCH_H

/*
 * CP_HOST_ARCH is the name of the architecture of the build in the target table
 * of cparse: the architecture, and the environment for Windows (-windows for MSVC,
 * -mingw), macOS (-darwin), Fil-C (-filc), and -wasi and -emscripten for wasm32. It is not defined for a
 * combination without an entry in the table.
 *
 * The build systems include this file in a test of the compiler. When the macro
 * is not defined, the library has no C backend of its own: it reads C files only
 * with libclang. Add the architectures here and in the table of cparse-target.c
 * together.
 */
#if defined(_WIN32) && (defined(__MINGW32__) || defined(__MINGW64__))
#if defined(__x86_64__)
#define CP_HOST_ARCH "x86_64-mingw"
#elif defined(__i386__)
#define CP_HOST_ARCH "i386-mingw"
#elif defined(__aarch64__)
#define CP_HOST_ARCH "aarch64-mingw"
#endif
#elif defined(_WIN32)
#if defined(_M_X64) || defined(__x86_64__)
#define CP_HOST_ARCH "x86_64-windows"
#elif defined(_M_IX86) || defined(__i386__)
#define CP_HOST_ARCH "i386-windows"
#elif defined(_M_ARM64) || defined(__aarch64__)
#define CP_HOST_ARCH "aarch64-windows"
#endif
#elif defined(__APPLE__)
#if defined(__x86_64__)
#define CP_HOST_ARCH "x86_64-darwin"
#elif defined(__aarch64__)
#define CP_HOST_ARCH "aarch64-darwin"
#endif
#elif defined(__FILC__)
/* Fil-C runs on x86-64 and AArch64 only */
#if defined(__x86_64__)
#define CP_HOST_ARCH "x86_64-filc"
#elif defined(__aarch64__)
#define CP_HOST_ARCH "aarch64-filc"
#endif
#elif defined(__wasm32__)
#if defined(__EMSCRIPTEN__)
#define CP_HOST_ARCH "wasm32-emscripten"
#elif defined(__wasi__)
#define CP_HOST_ARCH "wasm32-wasi"
#else
#define CP_HOST_ARCH "wasm32"
#endif
#elif defined(__wasm64__)
#define CP_HOST_ARCH "wasm64"
#elif defined(__x86_64__)
#define CP_HOST_ARCH "x86_64"
#elif defined(__i386__)
#define CP_HOST_ARCH "i386"
#elif defined(__aarch64__)
#define CP_HOST_ARCH "aarch64"
#elif defined(__arm__)
#define CP_HOST_ARCH "arm"
#elif defined(__riscv) && __riscv_xlen == 64
#define CP_HOST_ARCH "riscv64"
#elif defined(__riscv) && __riscv_xlen == 32
#define CP_HOST_ARCH "riscv32"
#elif defined(__powerpc64__) && defined(__LITTLE_ENDIAN__)
#define CP_HOST_ARCH "ppc64le"
#elif defined(__powerpc64__)
#define CP_HOST_ARCH "ppc64"
#elif defined(__s390x__)
#define CP_HOST_ARCH "s390x"
#elif defined(__loongarch64)
#define CP_HOST_ARCH "loongarch64"
#elif defined(__mips64)
#define CP_HOST_ARCH "mips64"
#elif defined(__mips__)
#define CP_HOST_ARCH "mips"
#endif

#endif
