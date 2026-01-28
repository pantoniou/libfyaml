/*
 * fy-id.h - small ID allocator
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_ID_H
#define FY_ID_H

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "fy-bit64.h"
#include "fy-atomics.h"

typedef uint64_t fy_id_bits_non_atomic;
typedef FY_ATOMIC(fy_id_bits_non_atomic) fy_id_bits;

#define FY_ID_BITS_SZ		((size_t)(sizeof(fy_id_bits)))
#define FY_ID_BITS_BITS 	(FY_ID_BITS_SZ * 8)	/* yes, every character is 8 bits in 2023 */
#define FY_ID_BITS_MASK		((uint64_t)(FY_ID_BITS_BITS - 1))

#define FY_ID_BITS_ARRAY_COUNT_BITS(_bits) \
	(((_bits) + FY_ID_BITS_MASK) & ~FY_ID_BITS_MASK)

#define FY_ID_BITS_ARRAY_COUNT(_bits) \
	(FY_ID_BITS_ARRAY_COUNT_BITS(_bits) / FY_ID_BITS_BITS)

#define FY_ID_OFFSET(_id)	((_id) / FY_ID_BITS_BITS)
#define FY_ID_BIT_MASK(_id)	((uint64_t)1 << ((_id) & FY_ID_BITS_MASK))

static inline int fy_id_ffs(const fy_id_bits_non_atomic id_bit)
{
	if (!id_bit)
		return -1;
	return FY_BIT64_FFS(id_bit) - 1;
}

static inline int fy_id_popcount(const fy_id_bits_non_atomic id_bit)
{
	return FY_BIT64_POPCNT(id_bit);
}

static inline void fy_id_reset(fy_id_bits *bits, const size_t count)
{
	size_t i;

	for (i = 0; i < count; i++, bits++)
		fy_atomic_store(bits, 0);
}

static inline int fy_id_alloc(fy_id_bits *bits, const size_t count)
{
	size_t i;
	int pos;
	fy_id_bits_non_atomic v, new_v;

	for (i = 0; i < count; i++, bits++) {
		for (;;) {
			v = fy_atomic_load(bits);
			pos = fy_id_ffs(~v);
			if (pos < 0)
				break;
			new_v = v | FY_ID_BIT_MASK(pos);
			if (fy_atomic_compare_exchange_strong(bits, &v, new_v))
				return ((int)i * FY_ID_BITS_BITS) + pos;
		}
	}
	return -1;
}

static inline int fy_id_alloc_fixed(fy_id_bits *bits, const size_t count, const int id)
{
	size_t offset;
	fy_id_bits_non_atomic m, v, new_v;

	offset = FY_ID_OFFSET(id);
	if (offset >= count)
		return -1;
	bits += offset;

	m = FY_ID_BIT_MASK(id);
	v = fy_atomic_load(bits);
	if (v & m)
		return -1;
	new_v = v | m;
	if (!fy_atomic_compare_exchange_strong(bits, &v, new_v))
		return -1;

	return id;
}

static inline bool fy_id_is_used(fy_id_bits *bits, const size_t count, const int id)
{
	fy_id_bits_non_atomic v;
	size_t offset;

	offset = FY_ID_OFFSET(id);
	if (offset >= count)
		return false;
	bits += offset;
	v = fy_atomic_load(bits);
	return !!(v & FY_ID_BIT_MASK(id));
}

static inline bool fy_id_is_free(fy_id_bits *bits, const size_t count, const int id)
{
	fy_id_bits_non_atomic v;
	size_t offset;

	offset = FY_ID_OFFSET(id);
	if (offset >= count)
		return false;
	bits += offset;
	v = fy_atomic_load(bits);
	return !(v & FY_ID_BIT_MASK(id));
}

static inline void fy_id_free(fy_id_bits *bits, const size_t count, int id)
{
	size_t offset;

	offset = FY_ID_OFFSET(id);
	if (offset >= count)
		return;
	bits += offset;
	fy_atomic_fetch_and(bits, ~FY_ID_BIT_MASK(id));	/* no need to check, if bit is free, it is still free */
}

static inline void fy_id_set_used(fy_id_bits *bits, const size_t count, const int id)
{
	size_t offset;

	offset = FY_ID_OFFSET(id);
	if (offset >= count)
		return;
	bits += offset;
	fy_atomic_fetch_or(bits, FY_ID_BIT_MASK(id));
}

static inline void fy_id_set_free(fy_id_bits *bits, const size_t count, const int id)
{
	size_t offset;

	offset = FY_ID_OFFSET(id);
	if (offset >= count)
		return;
	bits += offset;
	fy_atomic_fetch_and(bits, ~FY_ID_BIT_MASK(id));
}

static inline size_t fy_id_count_used(fy_id_bits *bits, const size_t count)
{
	fy_id_bits v;
	size_t i, popcount;

	popcount = 0;
	for (i = 0; i < count; i++, bits++) {
		v = fy_atomic_load(bits);
		popcount += fy_id_popcount(v);
	}
	return popcount;
}

static inline size_t fy_id_count_free(fy_id_bits *bits, const size_t count)
{
	return count - fy_id_count_used(bits, count);
}

struct fy_id_iter {
	const fy_id_bits *s;
	fy_id_bits m;
};

static inline void fy_id_iter_begin(const fy_id_bits *bits, const size_t count, struct fy_id_iter *iterp)
{
	iterp->s = bits;
	iterp->m = ~(fy_id_bits_non_atomic)0;
}

static inline int fy_id_iter_next(const fy_id_bits *bits, const size_t count, struct fy_id_iter *iterp)
{
	const fy_id_bits *s, *e;
	fy_id_bits_non_atomic v, m;
	int pos;

	s = iterp->s;
	if (!s)
		return -1;

	m = iterp->m;
	e = bits + count;

	if (s >= e)
		return -1;

	/* scan until we find a set bit or hit the end */
	pos = -1;
	while (s < e) {
		v = fy_atomic_load(s);
		pos = fy_id_ffs(v & m);
		if (pos >= 0)
			break;
		s++;
		m = ~(fy_id_bits_non_atomic)0;
	}

	/* not found */
	if (pos < 0) {
		iterp->s = NULL;
		iterp->m = 0;
		return -1;
	}

	/* remove this bit from the mask */
	m &= ~((fy_id_bits_non_atomic)1 << pos);

	/* advance by the number of fy_id_bits we scanned */
	pos += (int)(s - bits) * FY_ID_BITS_BITS;

	/* if we run out of mask, advance */
	if (!m) {
		s++;
		m = ~(fy_id_bits_non_atomic)0;
	}
	iterp->s = s;
	iterp->m = m;
	return pos;
}

static inline void fy_id_iter_end(const fy_id_bits *bits, const size_t count, const struct fy_id_iter *iterp)
{
	/* nothing */
}

#endif
