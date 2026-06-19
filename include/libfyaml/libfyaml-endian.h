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

#include <stdint.h>

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
#elif defined(_WIN32) || defined(_MSC_VER)
/* Windows is always little-endian on supported platforms */
# define __LITTLE_ENDIAN 1234
# define __BIG_ENDIAN    4321
# define __BYTE_ORDER    __LITTLE_ENDIAN
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

/*
 * Byte-swap primitives.
 */
#if defined(__GNUC__) || defined(__clang__)

static inline uint16_t fy_bswap16(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t fy_bswap32(uint32_t x) { return __builtin_bswap32(x); }
static inline uint64_t fy_bswap64(uint64_t x) { return __builtin_bswap64(x); }

#elif defined(_MSC_VER)

#include <stdlib.h>
static inline uint16_t fy_bswap16(uint16_t x) { return _byteswap_ushort(x); }
static inline uint32_t fy_bswap32(uint32_t x) { return _byteswap_ulong(x); }
static inline uint64_t fy_bswap64(uint64_t x) { return _byteswap_uint64(x); }

#else

static inline uint16_t fy_bswap16(uint16_t x)
{
	return (x << 8) | (x >> 8);
}

static inline uint32_t fy_bswap32(uint32_t x)
{
	return ((x & (uint32_t)0x000000ffUL) << 24) |
	       ((x & (uint32_t)0x0000ff00UL) <<  8) |
	       ((x & (uint32_t)0x00ff0000UL) >>  8) |
	       ((x & (uint32_t)0xff000000UL) >> 24);
}

static inline uint64_t fy_bswap64(uint64_t x)
{
	return ((x & (uint64_t)0x00000000000000ffULL) << 56) |
	       ((x & (uint64_t)0x000000000000ff00ULL) << 40) |
	       ((x & (uint64_t)0x0000000000ff0000ULL) << 24) |
	       ((x & (uint64_t)0x00000000ff000000ULL) <<  8) |
	       ((x & (uint64_t)0x000000ff00000000ULL) >>  8) |
	       ((x & (uint64_t)0x0000ff0000000000ULL) >> 24) |
	       ((x & (uint64_t)0x00ff000000000000ULL) >> 40) |
	       ((x & (uint64_t)0xff00000000000000ULL) >> 56);
}

#endif

/*
 * host <-> little/big-endian conversion helpers.
 *
 * If the platform already provides these use them, otherwise build them
 */
#if __BYTE_ORDER == __LITTLE_ENDIAN

#ifndef htole16
#define htole16(x) ((uint16_t)(x))
#endif
#ifndef le16toh
#define le16toh(x) ((uint16_t)(x))
#endif
#ifndef htobe16
#define htobe16(x) fy_bswap16((uint16_t)(x))
#endif
#ifndef be16toh
#define be16toh(x) fy_bswap16((uint16_t)(x))
#endif

#ifndef htole32
#define htole32(x) ((uint32_t)(x))
#endif
#ifndef le32toh
#define le32toh(x) ((uint32_t)(x))
#endif
#ifndef htobe32
#define htobe32(x) fy_bswap32((uint32_t)(x))
#endif
#ifndef be32toh
#define be32toh(x) fy_bswap32((uint32_t)(x))
#endif

#ifndef htole64
#define htole64(x) ((uint64_t)(x))
#endif
#ifndef le64toh
#define le64toh(x) ((uint64_t)(x))
#endif
#ifndef htobe64
#define htobe64(x) fy_bswap64((uint64_t)(x))
#endif
#ifndef be64toh
#define be64toh(x) fy_bswap64((uint64_t)(x))
#endif

#else /* big endian */

#ifndef htole16
#define htole16(x) fy_bswap16((uint16_t)(x))
#endif
#ifndef le16toh
#define le16toh(x) fy_bswap16((uint16_t)(x))
#endif
#ifndef htobe16
#define htobe16(x) ((uint16_t)(x))
#endif
#ifndef be16toh
#define be16toh(x) ((uint16_t)(x))
#endif

#ifndef htole32
#define htole32(x) fy_bswap32((uint32_t)(x))
#endif
#ifndef le32toh
#define le32toh(x) fy_bswap32((uint32_t)(x))
#endif
#ifndef htobe32
#define htobe32(x) ((uint32_t)(x))
#endif
#ifndef be32toh
#define be32toh(x) ((uint32_t)(x))
#endif

#ifndef htole64
#define htole64(x) fy_bswap64((uint64_t)(x))
#endif
#ifndef le64toh
#define le64toh(x) fy_bswap64((uint64_t)(x))
#endif
#ifndef htobe64
#define htobe64(x) ((uint64_t)(x))
#endif
#ifndef be64toh
#define be64toh(x) ((uint64_t)(x))
#endif

#endif /* __BYTE_ORDER */

#ifdef __cplusplus
}
#endif

#endif	// LIBFYAML_ENDIAN_H
