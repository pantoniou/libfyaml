/*
 * fy-endian.h - simple system endian header wrapper
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_ENDIAN_H
#define FY_ENDIAN_H

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

/* make the macros work for 8 bit too */
#ifndef bswap_8
#define bswap_8(x) (x)
#endif

#endif
