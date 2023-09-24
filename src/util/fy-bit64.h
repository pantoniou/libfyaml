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

#define FY_BIT64_LOWEST(_x) ((unsigned int)__builtin_ctzll((unsigned long long)(_x)))
#define FY_BIT64_HIGHEST(_x) ((unsigned int)__builtin_clzll((unsigned long long)(_x)))
#define FY_BIT64_POPCNT(_x) ((unsigned int)__builtin_popcountl((unsigned long long)(_x)))

static inline unsigned int fy_bit64_lowest(uint64_t x)
{
	return FY_BIT64_LOWEST(x);
}

static inline unsigned int fy_bit64_highest(uint64_t x)
{
	return FY_BIT64_HIGHEST(x);
}

static inline unsigned int FY_bit64_popcnt(uint64_t x)
{
	return FY_BIT64_POPCNT(x);
}

#else	/* portable implementation */

static inline unsigned int FY_BIT64_LOWEST(uint64_t x)
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

static inline unsigned int FY_BIT64_HIGHEST(uint64_t x)
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

static inline unsigned int FY_BIT64_POPCNT(uint64_t x)
{
	unsigned int count;

	for (count = 0; x; x &= x - 1)
		count++;
	return count;
}

#define FY_BIT64_LOWEST(_x) fy_bit64_lowest(_x)
#define FY_BIT64_HIGEST(_x) fy_bit64_highest(_x)
#define FY_BIT64_POPCNT(_x) fy_bit64_popcnt(_x)

#endif

#endif
