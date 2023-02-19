/*
 * libfyaml-endian.h - simple system endian header wrapper
 *
 * Copyright (c) 2023-2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * DOC: Endian detection and byte-swap utilities
 *
 * This header provides a portable way to include the platform's byte-order
 * detection headers and ensures the following macros are always defined:
 *
 * - ``__BYTE_ORDER``  — the byte order of the current platform
 * - ``__BIG_ENDIAN``  — big-endian sentinel value
 * - ``__LITTLE_ENDIAN`` — little-endian sentinel value
 *
 * Usage::
 *
 *   #include <libfyaml/fy-internal-endian.h>
 *
 *   #if __BYTE_ORDER == __LITTLE_ENDIAN
 *       // little-endian path
 *   #else
 *       // big-endian path
 *   #endif
 *
 * Supported platforms:
 * - Linux / Cygwin / OpenBSD / GNU Hurd / Emscripten — via ``<endian.h>``
 * - macOS / iOS — via ``<libkern/OSByteOrder.h>`` + ``<machine/endian.h>``
 * - NetBSD / FreeBSD / DragonFly BSD — via ``<sys/endian.h>``
 * - Windows (MSVC) — via ``<winsock2.h>`` (+ ``<sys/param.h>`` for MinGW)
 *
 * The non-standard ``BYTE_ORDER``, ``BIG_ENDIAN``, and ``LITTLE_ENDIAN``
 * spellings are aliased to the double-underscore variants if needed.
 */
#ifndef LIBFYAML_ENDIAN_H
#define LIBFYAML_ENDIAN_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__linux__) || defined(__CYGWIN__) || defined(__OpenBSD__) || defined(__GNU__) || defined(__EMSCRIPTEN__)
# include <endian.h>
#elif defined(__APPLE__)
# include <libkern/OSByteOrder.h>
# include <machine/endian.h>
#elif defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__)
# include <sys/endian.h>
#elif defined(_MSC_VER)
# include <winsock2.h>
# ifdef __GNUC__
#  include <sys/param.h>
# endif
#else
# error unsupported platform
#endif

#if !defined(__BYTE_ORDER) && defined(BYTE_ORDER)
#define __BYTE_ORDER	BYTE_ORDER
#endif

#if !defined(__BIG_ENDIAN) && defined(BIG_ENDIAN)
#define __BIG_ENDIAN	BIG_ENDIAN
#endif

#if !defined(__LITTLE_ENDIAN) && defined(LITTLE_ENDIAN)
#define __LITTLE_ENDIAN	LITTLE_ENDIAN
#endif

#if !defined(__BYTE_ORDER) || !defined(__BIG_ENDIAN) || !defined(__LITTLE_ENDIAN)
# error Platform does not define endian macros
#endif

/* no-one cares about PDP endian anymore */

/**
 * bswap_8() - Byte-swap an 8-bit value (no-op).
 *
 * Defined for symmetry with ``bswap_16()``, ``bswap_32()``, and ``bswap_64()``.
 * Swapping a single byte is always a no-op.
 *
 * @x: The 8-bit value.
 *
 * Returns: @x unchanged.
 */
/* make the macros work for 8 bit too */
#ifndef bswap_8
#define bswap_8(x) (x)
#endif

#ifdef __cplusplus
}
#endif

#endif	// LIBFYAML_ENDIAN_H
