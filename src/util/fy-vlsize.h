/*
 * fy-vlsize.h - variable length size encoding
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_VLSIZE_H
#define FY_VLSIZE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <limits.h>

// size encoding for 64 bit
//
// high bit is set, more follow with 0 ending the run
// The final 9th byte always terminates the run
//
//  0  1  2  3  4  5  6  7  8
// -- -- -- -- -- -- -- -- --
// 0k	                       7 bit length k
// 1k 0l                      14 bit length kl
// 1k 1l 0m                   21 bit length klm
// 1k 1l 1m 0n                28 bit length klmn
// 1k 1l 1m 1n 0o             35 bit length klmno
// 1k 1l 1m 1n 1o 0p          42 bit length klmnop
// 1k 1l 1m 1n 1o 1p 0q       49 bit length klmnopq
// 1k 1l 1m 1n 1o 1p 1q 0r    56 bit length klmnopqr
// 1k 1l 1m 1n 1o 1p 1q 1r  t 64 bit length klmnopqrt
//
// size encoding for 32 bit
//
// high bit is set, more follow with 0 ending the run
// The final 9th byte always terminates the run
//
//  0   1  2  3  4  5  6  7  8
// --  -- -- -- -- -- -- -- --
// 0k 	                       7 bit length k
// 1k  0l                      14 bit length kl
// 1k  1l 0m                   21 bit length klm
// 1k  1l 1m 0n                28 bit length klmn
// 1xk 1l 1m 1n o              32 bit length klmno 4 high bits ignored (at byte 0)
//

#define FYVL_SIZE_ENCODING_MAX_64 9 	// 7 * 8 + 8 = 64 bits
#define FYVL_SIZE_ENCODING_MAX_32 5	// 7 * 4 + 4 = 32 bits

/* 32 bit specific */
static inline unsigned int
fy_encode_size32_bytes(uint32_t size)
{
	if (size < ((uint32_t)1 << 7))
		return 1;
	if (size < ((uint32_t)1 << 14))
		return 2;
	if (size < ((uint32_t)1 << 21))
		return 3;
	if (size < ((uint32_t)1 << 28))
		return 4;
	return 5;
}

static inline uint8_t *
fy_encode_size32(uint8_t *p, uint32_t bufsz, uint32_t size)
{
	uint8_t *end = p + bufsz;

	if (size < ((uint32_t)1 << 7)) {
		if (p + 1 > end)
			return NULL;
		p[0] = (uint8_t)size;
		return p + 1;
	}
	if (size < ((uint32_t)1 << 14)) {
		if (p + 2 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 7) | 0x80;
		p[1] = (uint8_t)size & 0x7f;
		return p + 2;
	}
	if (size < ((uint32_t)1 << 21)) {
		if (p + 3 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 14) | 0x80;
		p[1] = (uint8_t)(size >>  7) | 0x80;
		p[2] = (uint8_t)size & 0x7f;
		return p + 3;
	}
	if (size < ((uint32_t)1 << 28)) {
		if (p + 4 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 21) | 0x80;
		p[1] = (uint8_t)(size >> 14) | 0x80;
		p[2] = (uint8_t)(size >>  7) | 0x80;
		p[3] = (uint8_t)size & 0x7f;
		return p + 4;
	}
	if (p + 5 > end)
		return NULL;
	p[0] = (uint8_t)(size >> 29) | 0x80;
	p[1] = (uint8_t)(size >> 22) | 0x80;
	p[2] = (uint8_t)(size >> 15) | 0x80;
	p[3] = (uint8_t)(size >>  8) | 0x80;
	p[4] = (uint8_t)size;
	return p + 5;
}

static inline const uint8_t *
fy_decode_size32(const uint8_t *start, size_t bufsz, uint32_t *sizep)
{
	const uint8_t *p, *end, *end_scan, *end_max_scan;
	uint32_t size;

	end = start + bufsz;

	end_max_scan = start + FYVL_SIZE_ENCODING_MAX_32;

	if (end_max_scan < end)
		end_scan = end_max_scan;
	else
		end_scan = end;

	end_max_scan--;

	p = start;
	size = 0;
	while (p < end_scan) {
		if (p < end_max_scan) {
			size <<= 7;
			size |= (*p & 0x7f);
			if (!(*p & 0x80))
				goto done;
		} else {
			/* last one is always the full 8 bit */
			size <<= 8;
			size |= *p;
			goto done;
		}
		p++;
	}

	*sizep = (uint32_t)-1;
	return NULL;
done:
	if (++p >= end)
		p = end;
	*sizep = size;
	return p;
}

static inline const uint8_t *
fy_decode_size32_nocheck(const uint8_t *start, uint64_t *sizep)
{
	const uint8_t *p;
	size_t size;
	unsigned int i;

	p = start;
	size = 0;
	for (i = 0; i < FYVL_SIZE_ENCODING_MAX_32-1; i++) {
		size <<= 7;
		size |= (p[i] & 0x7f);
		if ((int8_t)p[i] >= 0)
			goto out;
	}
	size <<= 8;
	size |= p[i];
out:
	*sizep = size;
	return p + i + 1;
}

static inline const uint8_t *
fy_skip_size32(const uint8_t *start, size_t bufsz)
{
	const uint8_t *p, *end, *end_scan, *end_max_scan;

	end = start + bufsz;

	end_max_scan = start + FYVL_SIZE_ENCODING_MAX_32;

	if (end_max_scan < end)
		end_scan = end_max_scan;
	else
		end_scan = end;

	end_max_scan--;

	p = start;
	while (p < end_scan) {
		if (p < end_max_scan) {
			if (!(*p & 0x80))
				goto done;
		} else
			goto done;
		p++;
	}

	return NULL;
done:
	if (++p >= end)
		p = end;
	return p;
}

static inline const uint8_t *
fy_skip_size32_nocheck(const uint8_t *p)
{
	unsigned int i;

	for (i = 0; i < FYVL_SIZE_ENCODING_MAX_32; ) {
		if ((int8_t)p[i++] >= 0)
			break;
	}
	return p + i;
}

static inline unsigned int
fy_encode_size64_bytes(uint64_t size)
{
	if (size < ((uint64_t)1 << 7))
		return 1;
	if (size < ((uint64_t)1 << 14))
		return 2;
	if (size < ((uint64_t)1 << 21))
		return 3;
	if (size < ((uint64_t)1 << 28))
		return 4;
	if (size < ((uint64_t)1 << 35))
		return 5;
	if (size < ((uint64_t)1 << 42))
		return 6;
	if (size < ((uint64_t)1 << 49))
		return 7;
	if (size < ((uint64_t)1 << 56))
		return 8;
	return 9;
}

static inline uint8_t *
fy_encode_size64(uint8_t *p, size_t bufsz, uint64_t size)
{
	uint8_t *end = p + bufsz;

	if (size < ((uint64_t)1 << 7)) {
		if (p + 1 > end)
			return NULL;
		p[0] = (uint8_t)size;
		return p + 1;
	}
	if (size < ((uint64_t)1 << 14)) {
		if (p + 2 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 7) | 0x80;
		p[1] = (uint8_t)size & 0x7f;
		return p + 2;
	}
	if (size < ((uint64_t)1 << 21)) {
		if (p + 3 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 14) | 0x80;
		p[1] = (uint8_t)(size >>  7) | 0x80;
		p[2] = (uint8_t)size & 0x7f;
		return p + 3;
	}
	if (size < ((uint64_t)1 << 28)) {
		if (p + 4 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 21) | 0x80;
		p[1] = (uint8_t)(size >> 14) | 0x80;
		p[2] = (uint8_t)(size >>  7) | 0x80;
		p[3] = (uint8_t)size & 0x7f;
		return p + 4;
	}
	if (size < ((uint64_t)1 << 35)) {
		if (p + 5 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 28) | 0x80;
		p[1] = (uint8_t)(size >> 21) | 0x80;
		p[2] = (uint8_t)(size >> 14) | 0x80;
		p[3] = (uint8_t)(size >>  7) | 0x80;
		p[4] = (uint8_t)size & 0x7f;
		return p + 5;
	}
	if (size < ((uint64_t)1 << 42)) {
		if (p + 6 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 35) | 0x80;
		p[1] = (uint8_t)(size >> 28) | 0x80;
		p[2] = (uint8_t)(size >> 21) | 0x80;
		p[3] = (uint8_t)(size >> 14) | 0x80;
		p[4] = (uint8_t)(size >>  7) | 0x80;
		p[5] = (uint8_t)size & 0x7f;
		return p + 6;
	}
	if (size < ((uint64_t)1 << 49)) {
		if (p + 7 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 42) | 0x80;
		p[1] = (uint8_t)(size >> 35) | 0x80;
		p[2] = (uint8_t)(size >> 28) | 0x80;
		p[3] = (uint8_t)(size >> 21) | 0x80;
		p[4] = (uint8_t)(size >> 14) | 0x80;
		p[5] = (uint8_t)(size >>  7) | 0x80;
		p[6] = (uint8_t)size & 0x7f;
		return p + 7;
	}
	if (size < ((uint64_t)1 << 56)) {
		if (p + 8 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 49) | 0x80;
		p[1] = (uint8_t)(size >> 42) | 0x80;
		p[2] = (uint8_t)(size >> 35) | 0x80;
		p[3] = (uint8_t)(size >> 28) | 0x80;
		p[4] = (uint8_t)(size >> 21) | 0x80;
		p[5] = (uint8_t)(size >> 14) | 0x80;
		p[6] = (uint8_t)(size >>  7) | 0x80;
		p[7] = (uint8_t)size & 0x7f;
		return p + 8;
	}
	if (p + 9 > end)
		return NULL;
	p[0] = (uint8_t)(size >> 57) | 0x80;
	p[1] = (uint8_t)(size >> 50) | 0x80;
	p[2] = (uint8_t)(size >> 43) | 0x80;
	p[3] = (uint8_t)(size >> 36) | 0x80;
	p[4] = (uint8_t)(size >> 29) | 0x80;
	p[5] = (uint8_t)(size >> 22) | 0x80;
	p[6] = (uint8_t)(size >> 15) | 0x80;
	p[7] = (uint8_t)(size >>  8) | 0x80;
	p[8] = (uint8_t)size;
	return p + 9;
}

static inline const uint8_t *
fy_decode_size64(const uint8_t *start, size_t bufsz, uint64_t *sizep)
{
	const uint8_t *p, *end, *end_scan, *end_max_scan;
	size_t size;

	end = start + bufsz;

	end_max_scan = start + FYVL_SIZE_ENCODING_MAX_64;

	if (end_max_scan < end)
		end_scan = end_max_scan;
	else
		end_scan = end;

	end_max_scan--;

	p = start;
	size = 0;
	while (p < end_scan) {
		if (p < end_max_scan) {
			size <<= 7;
			size |= (*p & 0x7f);
			if (!(*p & 0x80))
				goto done;
		} else {
			/* last one is always 8 bit */
			size <<= 8;
			size |= *p;
			goto done;
		}
		p++;
	}

	*sizep = (size_t)-1;
	return NULL;
done:
	if (++p >= end)
		p = end;
	*sizep = size;
	return p;
}

static inline const uint8_t *
fy_decode_size64_nocheck(const uint8_t *start, uint64_t *sizep)
{
	const uint8_t *p;
	size_t size;
	unsigned int i;

	p = start;
	size = 0;
	for (i = 0; i < FYVL_SIZE_ENCODING_MAX_64-1; i++) {
		size <<= 7;
		size |= (p[i] & 0x7f);
		if ((int8_t)p[i] >= 0)
			goto out;
	}
	size <<= 8;
	size |= p[i];
out:
	*sizep = size;
	return p + i + 1;
}

static inline const uint8_t *
fy_skip_size64(const uint8_t *start, size_t bufsz)
{
	const uint8_t *p, *end, *end_scan, *end_max_scan;

	end = start + bufsz;

	end_max_scan = start + FYVL_SIZE_ENCODING_MAX_64;

	if (end_max_scan < end)
		end_scan = end_max_scan;
	else
		end_scan = end;

	end_max_scan--;

	p = start;
	while (p < end_scan) {
		if (p < end_max_scan) {
			if (!(*p & 0x80))
				goto done;
		} else
			goto done;
		p++;
	}

	return NULL;
done:
	if (++p >= end)
		p = end;
	return p;
}

static inline const uint8_t *
fy_skip_size64_nocheck(const uint8_t *p)
{
	unsigned int i;

	for (i = 0; i < FYVL_SIZE_ENCODING_MAX_64; ) {
		if ((int8_t)p[i++] >= 0)
			break;
	}
	return p + i;
}

/* is pointless to pretend size_t is anything other than 64 or 32 bits */
#if SIZE_MAX == UINT64_MAX

static inline unsigned int
fy_encode_size_bytes(size_t size)
{
	return fy_encode_size64_bytes(size);
}

static inline uint8_t *
fy_encode_size(uint8_t *p, size_t bufsz, size_t size)
{
	return fy_encode_size64(p, bufsz, size);
}

static inline const uint8_t *
fy_decode_size(const uint8_t *start, size_t bufsz, size_t *sizep)
{
	uint64_t sz;
	const uint8_t *ret;

	ret = fy_decode_size64(start, bufsz, &sz);
	if (!ret) {
		*sizep = 0;
		return NULL;
	}
	*sizep = sz;
	return ret;
}

static inline const uint8_t *
fy_decode_size_nocheck(const uint8_t *start, size_t *sizep)
{
#if 1
	return fy_decode_size64_nocheck(start, sizep);
#else
	const uint8_t *p0, *p1;
	size_t sz0, sz1;
	p0 = fy_decode_size64(start, FYVL_SIZE_ENCODING_MAX_64, &sz0);
	p1 = fy_decode_size64_nocheck(start, &sz1);
	assert(p0 == p1);
	if (sz0 != sz1) {
		fprintf(stderr, "%zu %zu\n", sz0, sz1);
	}
	assert(sz0 == sz1);
	*sizep = sz0;
	return p0;
#endif
}

static inline const uint8_t *
fy_skip_size(const uint8_t *start, size_t bufsz)
{
	return fy_skip_size64(start, bufsz);
}

static inline const uint8_t *
fy_skip_size_nocheck(const uint8_t *start)
{
	return fy_skip_size64_nocheck(start);
}

#define FYVL_SIZE_ENCODING_MAX FYVL_SIZE_ENCODING_MAX_64

#else

static inline unsigned int
fy_encode_size_bytes(size_t size)
{
	return fy_encode_size32_bytes(size);
}

static inline uint8_t *
fy_encode_size(uint8_t *p, size_t bufsz, size_t size)
{
	return fy_encode_size32(p, bufsz, size);
}

static inline const uint8_t *
fy_decode_size(const uint8_t *start, size_t bufsz, size_t *sizep)
{
	uint32_t sz;
	const uint8_t *ret;

	ret = fy_decode_size32(start, bufsz, &sz);
	if (!ret)
		return NULL;
	*sizep = sz;
	return ret;
}

static inline const uint8_t *
fy_decode_size_nocheck(const uint8_t *start, size_t *sizep)
{
	return fy_decode_size32_nocheck(start, sizep);
}

static inline const uint8_t *
fy_skip_size(const uint8_t *start, size_t bufsz)
{
	return fy_skip_size32(start, bufsz, &sz);
}

static inline const uint8_t *
fy_skip_size_nocheck(const uint8_t *start)
{
	return fy_skip_size32_nocheck(start);
}

#define FYVL_SIZE_ENCODING_MAX FYVL_SIZE_ENCODING_MAX_32

#endif

#endif
