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
#include <strings.h>
#include <string.h>
#include <assert.h>

#include <stdint.h>

typedef unsigned int fy_id_bits;

#define FY_ID_BITS_SZ		((size_t)(sizeof(fy_id_bits)))
#define FY_ID_BITS_BITS 	(FY_ID_BITS_SZ * 8)	/* yes, every character is 8 bits in 2023 */
#define FY_ID_BITS_MASK		((size_t)(FY_ID_BITS_BITS - 1))

#define FY_ID_BITS_ARRAY_COUNT_BITS(_bits) \
	(((_bits) + FY_ID_BITS_MASK) & ~FY_ID_BITS_MASK)

#define FY_ID_BITS_ARRAY_COUNT(_bits) \
	(FY_ID_BITS_ARRAY_COUNT_BITS(_bits) / FY_ID_BITS_BITS)

#define FY_ID_OFFSET(_id)	((_id) / FY_ID_BITS_BITS)
#define FY_ID_BIT_MASK(_id)	((fy_id_bits)1 << ((_id) & FY_ID_BITS_MASK))

#if defined(__GNUC__) && __GNUC__ >= 4
static inline int fy_id_ffs(const fy_id_bits id_bit)
{
	if (!id_bit)
		return -1;
	return __builtin_ffs(id_bit) - 1;
}

#else
static inline int fy_id_ffs(const fy_id_bits id_bit)
{
	return ffs((int)id_bits) - 1;
}
#endif

#if defined(__GNUC__) && __GNUC__ >= 4
static inline int fy_id_popcount(const fy_id_bits id_bit)
{
	return __builtin_popcount(id_bit);
}

#else
static inline int fy_id_popcount(fy_id_bits id_bit)
{
	int count, pos;

	count = 0;
	while ((pos = fy_id_ffs(id_bit)) >= 0) {
		count++;
		id_bit &= ~((fy_id_bits)1 << pos);
	}
	return count;
}
#endif

static inline void fy_id_reset(fy_id_bits *bits, const size_t count)
{
	memset(bits, 0, sizeof(*bits) * count);
}

static inline int fy_id_alloc(fy_id_bits *bits, const size_t count)
{
	size_t i;
	int pos;

	for (i = 0; i < count; i++, bits++) {
		pos = fy_id_ffs(~*bits);
		if (pos >= 0) {
			*bits |= FY_ID_BIT_MASK(pos);
			return (i * FY_ID_BITS_BITS) + pos;
		}
	}
	return -1;
}

static inline int fy_id_alloc_fixed(fy_id_bits *bits, const size_t count, const int id)
{
	size_t offset;
	fy_id_bits m;

	offset = FY_ID_OFFSET(id);
	if (offset >= count)
		return -1;
	bits += offset;
	m = FY_ID_BIT_MASK(id);
	if (*bits & m)
		return -1;
	*bits |= m;
	return id;
}

static inline bool fy_id_is_used(fy_id_bits *bits, const size_t count, const int id)
{
	size_t offset;

	offset = FY_ID_OFFSET(id);
	if (offset >= count)
		return false;
	return !!(bits[offset] & FY_ID_BIT_MASK(id));
}

static inline bool fy_id_is_used_no_check(fy_id_bits *bits, const size_t count, const int id)
{
	size_t offset;

	offset = FY_ID_OFFSET(id);
	if (offset >= count)
		return false;
	return !!(bits[offset] & FY_ID_BIT_MASK(id));
}

static inline bool fy_id_is_free(fy_id_bits *bits, const size_t count, const int id)
{
	size_t offset;

	offset = FY_ID_OFFSET(id);
	if (offset >= count)
		return false;
	return !(bits[offset] & FY_ID_BIT_MASK(id));
}

static inline void fy_id_free(fy_id_bits *bits, const size_t count, int id)
{
	size_t offset;

	offset = FY_ID_OFFSET(id);
	if (offset >= count)
		return;
	bits += offset;
	*bits &= ~FY_ID_BIT_MASK(id);	/* no need to check, if bit is free, it is still free */
}

static inline void fy_id_set_used(fy_id_bits *bits, const size_t count, const int id)
{
	assert(FY_ID_OFFSET(id) < count);
	bits[FY_ID_OFFSET(id)] |= FY_ID_BIT_MASK(id);
}

static inline void fy_id_set_free(fy_id_bits *bits, const size_t count, const int id)
{
	assert(FY_ID_OFFSET(id) < count);
	bits[FY_ID_OFFSET(id)] &= ~FY_ID_BIT_MASK(id);
}

static inline size_t fy_id_count_used(fy_id_bits *bits, const size_t count)
{
	size_t i, popcount;

	popcount = 0;
	for (i = 0; i < count; i++, bits++)
		popcount += fy_id_popcount(*bits);
	return popcount;
}

static inline size_t fy_id_count_free(fy_id_bits *bits, const size_t count)
{
	size_t i, popcount;

	popcount = 0;
	for (i = 0; i < count; i++, bits++)
		popcount += fy_id_popcount(~*bits);
	return popcount;
}

struct fy_id_iter {
	const fy_id_bits *s;
	fy_id_bits m;
};

static inline void fy_id_iter_begin(const fy_id_bits *bits, const size_t count, struct fy_id_iter *iterp)
{
	iterp->s = bits;
	iterp->m = ~(fy_id_bits)0;
}

static inline int fy_id_iter_next(const fy_id_bits *bits, const size_t count, struct fy_id_iter *iterp)
{
	const fy_id_bits *s, *e;
	fy_id_bits m;
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
	while (s < e && (pos = fy_id_ffs(*s & m)) < 0) {
		s++;
		m = ~(fy_id_bits)0;
	}

	/* not found */
	if (pos < 0) {
		iterp->s = NULL;
		iterp->m = 0;
		return -1;
	}

	/* remove this bit from the mask */
	m &= ~((fy_id_bits)1 << pos);

	/* advance by the number of fy_id_bits we scanned */
	pos += (int)(s - bits) * FY_ID_BITS_BITS;

	/* if we run out of mask, advance */
	if (!m) {
		s++;
		m = ~(fy_id_bits)0;
	}
	iterp->s = s;
	iterp->m = m;
	return pos;
}

static inline void fy_id_iter_end(fy_id_bits *bits, const size_t count, const struct fy_id_iter *iterp)
{
	/* nothing */
}

#endif
