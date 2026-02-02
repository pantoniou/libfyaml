/*
 * fy-bit64.h - various bit64 methods
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_BIT64_H
#define FY_BIT64_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>

#define FY_BIT64(x) ((uint64_t)1 << (x))
#define FY_BIT64_COUNT(_x) (((_x) + 63) / 64)
#define FY_BIT64_SIZE(_x) ((size_t)((((_x) + 63) / 64) * sizeof(uint64_t)))

#if (defined(__GNUC__) && __GNUC__ >= 4) || defined(__clang__)

#define FY_BIT64_LOWEST(_x) ((unsigned int)__builtin_ctzll((uint64_t)(_x)))
#define FY_BIT64_HIGHEST(_x) ((unsigned int)__builtin_clzll((uint64_t)(_x)))
#define FY_BIT64_POPCNT(_x) ((unsigned int)__builtin_popcountl((uint64_t)(_x)))
#define FY_BIT64_FFS(_x) ((unsigned int)__builtin_ffsll((uint64_t)(_x)))

static inline unsigned int fy_bit64_lowest(uint64_t x)
{
	return FY_BIT64_LOWEST(x);
}

static inline unsigned int fy_bit64_highest(uint64_t x)
{
	return FY_BIT64_HIGHEST(x);
}

static inline unsigned int fy_bit64_popcnt(uint64_t x)
{
	return FY_BIT64_POPCNT(x);
}

static inline unsigned int fy_bit64_ffs(uint64_t x)
{
	return FY_BIT64_FFS(x);
}

#elif defined(_MSC_VER)	/* MSVC implementation */

#include <intrin.h>

#if defined(_M_X64) || defined(_M_AMD64) || defined(_M_ARM64)
/* 64-bit MSVC - use native 64-bit intrinsics */

static inline unsigned int fy_bit64_lowest(uint64_t x)
{
	unsigned long index;
	if (_BitScanForward64(&index, x))
		return (unsigned int)index;
	return 64;
}

static inline unsigned int fy_bit64_highest(uint64_t x)
{
	unsigned long index;
	if (_BitScanReverse64(&index, x))
		return 63 - (unsigned int)index;
	return 64;
}

static inline unsigned int fy_bit64_popcnt(uint64_t x)
{
	return (unsigned int)__popcnt64(x);
}

static inline unsigned int fy_bit64_ffs(uint64_t x)
{
	unsigned long index;
	if (x == 0)
		return 0;
	_BitScanForward64(&index, x);
	return (unsigned int)index + 1;
}

#else
/* 32-bit MSVC - use 32-bit intrinsics on halves */

static inline unsigned int fy_bit64_lowest(uint64_t x)
{
	unsigned long index;
	uint32_t lo = (uint32_t)x;
	uint32_t hi = (uint32_t)(x >> 32);

	if (lo && _BitScanForward(&index, lo))
		return (unsigned int)index;
	if (hi && _BitScanForward(&index, hi))
		return (unsigned int)index + 32;
	return 64;
}

static inline unsigned int fy_bit64_highest(uint64_t x)
{
	unsigned long index;
	uint32_t lo = (uint32_t)x;
	uint32_t hi = (uint32_t)(x >> 32);

	if (hi && _BitScanReverse(&index, hi))
		return 63 - ((unsigned int)index + 32);
	if (lo && _BitScanReverse(&index, lo))
		return 63 - (unsigned int)index;
	return 64;
}

static inline unsigned int fy_bit64_popcnt(uint64_t x)
{
	uint32_t lo = (uint32_t)x;
	uint32_t hi = (uint32_t)(x >> 32);
	return (unsigned int)(__popcnt(lo) + __popcnt(hi));
}

static inline unsigned int fy_bit64_ffs(uint64_t x)
{
	unsigned long index;
	uint32_t lo = (uint32_t)x;
	uint32_t hi = (uint32_t)(x >> 32);

	if (x == 0)
		return 0;
	if (lo && _BitScanForward(&index, lo))
		return (unsigned int)index + 1;
	if (hi && _BitScanForward(&index, hi))
		return (unsigned int)index + 33;
	return 0;
}

#endif /* 64-bit vs 32-bit MSVC */

#define FY_BIT64_LOWEST(_x) fy_bit64_lowest(_x)
#define FY_BIT64_HIGHEST(_x) fy_bit64_highest(_x)
#define FY_BIT64_POPCNT(_x) fy_bit64_popcnt(_x)
#define FY_BIT64_FFS(_x) fy_bit64_ffs(_x)

#else	/* portable implementation */

static inline unsigned int fy_bit64_lowest(uint64_t x)
{
	unsigned int c = 0;

	if (!(x & 0x00000000ffffffffULL)) { x >>= 32; c += 32; }
	if (!(x & 0x000000000000ffffULL)) { x >>= 16; c += 16; }
	if (!(x & 0x00000000000000ffULL)) { x >>=  8; c +=  8; }
	if (!(x & 0x000000000000000fULL)) { x >>=  4; c +=  4; }
	if (!(x & 0x0000000000000003ULL)) { x >>=  2; c +=  2; }
	if (!(x & 0x0000000000000001ULL)) {           c +=  1; }
	return c;
}

static inline unsigned int fy_bit64_highest(uint64_t x)
{
	unsigned int c = 0;

	if (x & 0xffffffff00000000ULL) { x >>= 32; c += 32; }
	if (x & 0x00000000ffff0000ULL) { x >>= 16; c += 16; }
	if (x & 0x000000000000ff00ULL) { x >>=  8; c +=  8; }
	if (x & 0x00000000000000f0ULL) { x >>=  4; c +=  4; }
	if (x & 0x000000000000000cULL) { x >>=  2; c +=  2; }
	if (x & 0x0000000000000002ULL) {           c +=  1; }
	return c;
}

static inline unsigned int fy_bit64_popcnt(uint64_t x)
{
	unsigned int count;

	for (count = 0; x; x &= x - 1)
		count++;
	return count;
}

static inline unsigned int fy_bit64_ffs(uint64_t x)
{
	return x ? (fy_bit64_lowest(x) + 1) : 0;
}

#define FY_BIT64_LOWEST(_x) fy_bit64_lowest(_x)
#define FY_BIT64_HIGHEST(_x) fy_bit64_highest(_x)
#define FY_BIT64_POPCNT(_x) fy_bit64_popcnt(_x)
#define FY_BIT64_FFS(_x) fy_bit64_ffs(_x)

#endif

#endif
