/*
 * fy-generic.h - space efficient generics
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_GENERIC_H
#define FY_GENERIC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdalign.h>
#include <assert.h>
#include <math.h>
#include <stdarg.h>

#include "fy-endian.h"
#include "fy-utils.h"
#include "fy-allocator.h"
#include "fy-vlsize.h"

#include <libfyaml.h>

enum fy_generic_type {
	FYGT_INVALID,
	FYGT_NULL,
	FYGT_BOOL,
	FYGT_INT,
	FYGT_FLOAT,
	FYGT_STRING,
	FYGT_SEQUENCE,
	FYGT_MAPPING,
	FYGT_INDIRECT,
	FYGT_ALIAS,
};

/* type is encoded at the lower 3 bits which is satisfied with the 8 byte alignment */
typedef uintptr_t fy_generic;

#define FYGT_INT_INPLACE_BITS_64 61
#define FYGT_STRING_INPLACE_SIZE_64 7
#define FYGT_SIZE_ENCODING_MAX_64 FYVL_SIZE_ENCODING_MAX_64

#define FYGT_INT_INPLACE_BITS_32 29
#define FYGT_STRING_INPLACE_SIZE_32 3
#define FYGT_SIZE_ENCODING_MAX_32 FYVL_SIZE_ENCODING_MAX_32

#ifdef FY_HAS_64BIT_PTR
#define FYGT_INT_INPLACE_BITS FYGT_INT_INPLACE_BITS_64
#define FYGT_STRING_INPLACE_SIZE FYGT_STRING_INPLACE_SIZE_64
#else
#define FYGT_INT_INPLACE_BITS FYGT_INT_INPLACE_BITS_32
#define FYGT_STRING_INPLACE_SIZE FYGT_STRING_INPLACE_SIZE_32
#endif

#define FYGT_SIZE_ENCODING_MAX FYVL_SIZE_ENCODING_MAX


// 64 bit memory layout for generic types
//
//              |63             8|7654|3|210|
// -------------+----------------+----|-+---+
// null       0 |0000000000000000|0000|0|000|
// sequence   0 |pppppppppppppppp|pppp|0|000| pointer to a 16 byte aligned sequence
// mapping    0 |pppppppppppppppp|pppp|1|000| pointer to a 16 byte aligned mapping
// bool       0 |0000000000000000|0000|1|000| false
//            0 |1111111111111111|1111|1|000| true
// int        1 |xxxxxxxxxxxxxxxx|xxxx|x|001| int bits <= 61
//            2 |pppppppppppppppp|pppp|p|010| 8 byte aligned pointer to an long long
// float      3 |ffffffffffffffff|0000|0|011| 32 bit float without loss of precision
//            4 |pppppppppppppppp|pppp|p|100| pointer to 8 byte aligned double
// string     5 |ssssssssssssssss|0lll|0|101| string length <= 7 lll 3 bit length
//                                x    y      two available bits for styling info
//            6 |pppppppppppppppp|pppp|p|110| 8 byte aligned pointer to a string
// indirect   7 |pppppppppppppppp|pppp|p|111| 8 byte aligned pointer to an indirect
// invalid      |1111111111111111|1111|1|111| All bits set
//
// 32 bit memory layout for generic types
//
//              |32             8|7654|3|210|
// -------------+----------------+----|-+---+
// null       0 |0000000000000000|0000|0|000|
// sequence   0 |pppppppppppppppp|pppp|0|000| pointer to a 16 byte aligned sequence
// mapping    0 |pppppppppppppppp|pppp|1|000| pointer to a 16 byte aligned mapping
// bool       0 |0000000000000000|0000|1|000| false
//            0 |1111111111111111|1111|1|000| true
// int        1 |xxxxxxxxxxxxxxxx|xxxx|x|001| int bits <= 29
//            2 |pppppppppppppppp|pppp|1|010| 8 byte aligned pointer to an long long
// float      3 |pppppppppppppppp|pppp|p|011| pointer to 8 byte aligned float
//            4 |pppppppppppppppp|pppp|p|100| pointer to 8 byte aligned double
// string     5 |ssssssssssssssss|00ll|0|101| string length <= 3 ll 2 bit length
//                                xy   z      three available bits for styling info
//            6 |pppppppppppppppp|pppp|p|110| 8 byte aligned pointer to a string
// indirect   7 |pppppppppppppppp|pppp|p|111| 8 byte aligned pointer to an indirect
// invalid      |1111111111111111|1111|1|111| All bits set
//

/* we use the bottom 3 bits to get the primitive types */
#define FY_INPLACE_TYPE_SHIFT	3
#define FY_INPLACE_TYPE_MASK	(((fy_generic)1 << FY_INPLACE_TYPE_SHIFT) - 1)

#define FY_NULL_V		0
#define FY_SEQ_V		0
#define FY_MAP_V		8
#define FY_COLLECTION_MASK	(((fy_generic)1 << (FY_INPLACE_TYPE_SHIFT + 1)) - 1)

#define FY_BOOL_V		8
#define FY_BOOL_INPLACE_SHIFT	4

#define FY_EXTERNAL_V		0

#define FY_INT_INPLACE_V	1
#define FY_INT_OUTPLACE_V	2
#define FY_INT_INPLACE_SHIFT	3

#define FY_FLOAT_INPLACE_V	3
#define FY_FLOAT_OUTPLACE_V	4
#define FY_FLOAT_INPLACE_SHIFT	32

#define FY_STRING_INPLACE_V	5
#define FY_STRING_OUTPLACE_V	6
#define FY_STRING_INPLACE_SIZE_SHIFT	4

#define FY_INDIRECT_V		7

#define fy_null		((fy_generic)0)	/* simple does it */
#define fy_false	((fy_generic)8)
#define fy_true		(~(fy_generic)7)
#define fy_invalid	((fy_generic)-1)

#define FYGT_STRING_INPLACE_BUF (FYGT_STRING_INPLACE_SIZE + 1)
#define FYGT_INT_INPLACE_MAX ((1LL << (FYGT_INT_INPLACE_BITS - 1)) - 1)
#define FYGT_INT_INPLACE_MIN (-(1LL << (FYGT_INT_INPLACE_BITS - 1)))

#define FY_GENERIC_CONTAINER_ALIGNMENT __attribute__((aligned(16)))
#define FY_GENERIC_EXTERNAL_ALIGNMENT FY_GENERIC_CONTAINER_ALIGNMENT

/* yes, plenty of side-effects, use it with care */
#define FY_MAX_ALIGNOF(_v, _min) ((size_t)(alignof(_v) > (_min) ? alignof(_v) : (_min)))
#define FY_CONTAINER_ALIGNOF(_v) FY_MAX_ALIGNOF(_v, 16)
#define FY_SCALAR_ALIGNOF(_v) FY_MAX_ALIGNOF(_v, 8)

/*
 * The encoding of generic indirect
 *
 * 1 byte of flags
 * value (if exists)
 * anchor (if exists)
 * tag (if exists)
 * positional info (if it exists)
 * > start: input_pos <vlencoded>
 * > start: line <vlencoded>
 * > start: column <vlencoded>
 * > end: delta input_pos <vlencoded>
 * > end: delta line <vlencoded>
 * > end: delta column <vlencoded>
 */

/* encoding an alias is anchor the string of the alias
 * and value fy_invalid */
struct fy_generic_indirect {
	uintptr_t flags;	/* styling and existence flags */
	fy_generic value;	/* the actual value */
	fy_generic anchor;	/* string anchor or null */
	fy_generic tag;		/* string tag or null */
};

#define FYGIF_VALUE		(1U << 0)
#define FYGIF_ANCHOR		(1U << 1)
#define FYGIF_TAG		(1U << 2)
#define FYGIF_STYLE_SHIFT	4
#define FYGIF_STYLE_MASK	(7U << FYGIF_STYLE_SHIFT)
#define FYGIF_PLAIN		(0U << FYGIF_STYLE_SHIFT)	/* scalar styles */
#define FYGIF_SINGLE_Q		(1U << FYGIF_STYLE_SHIFT)
#define FYGIF_DOUBLE_Q		(2U << FYGIF_STYLE_SHIFT)
#define FYGIF_LITERAL		(3U << FYGIF_STYLE_SHIFT)
#define FYGIF_FOLDED		(4U << FYGIF_STYLE_SHIFT)
#define FYGIF_BLOCK		(5U << FYGIF_STYLE_SHIFT)	/* collection styles */
#define FYGIF_FLOW		(6U << FYGIF_STYLE_SHIFT)

static inline bool fy_generic_is_indirect(fy_generic v)
{
	return (v & FY_INPLACE_TYPE_MASK) == FY_INDIRECT_V && v != fy_invalid;
}

static inline void *fy_generic_resolve_ptr(fy_generic ptr)
{
	/* clear the top 3 bits (all pointers are 8 byte aligned) */
	/* note collections have the bit 3 cleared too, so it's 16 byte aligned */
	ptr &= ~(uintptr_t)FY_INPLACE_TYPE_MASK;
	return (void *)ptr;
}

static inline void *fy_generic_resolve_collection_ptr(fy_generic ptr)
{
	/* clear the top 3 bits (all pointers are 8 byte aligned) */
	/* note collections have the bit 3 cleared too, so it's 16 byte aligned */
	ptr &= ~(uintptr_t)FY_COLLECTION_MASK;
	return (void *)ptr;
}

static inline fy_generic fy_generic_relocate_ptr(fy_generic v, ptrdiff_t d)
{
	v = (fy_generic)((ptrdiff_t)((uintptr_t)v & ~(uintptr_t)FY_INPLACE_TYPE_MASK) + d);
	assert((v & (uintptr_t)FY_INPLACE_TYPE_MASK) == 0);
	return v;
}

static inline fy_generic fy_generic_relocate_collection_ptr(fy_generic v, ptrdiff_t d)
{
	v = (fy_generic)((ptrdiff_t)((uintptr_t)v & ~(uintptr_t)FY_COLLECTION_MASK) + d);
	assert((v & (uintptr_t)FY_COLLECTION_MASK) == 0);
	return v;
}

static inline enum fy_generic_type fy_generic_get_type(fy_generic v)
{
	static const uint8_t table[16] = {
		[0] = FYGT_SEQUENCE, [8 | 0] = FYGT_MAPPING,
		[1] = FYGT_INT,      [8 | 1] = FYGT_INT,
		[2] = FYGT_INT,      [8 | 2] = FYGT_INT,
		[3] = FYGT_FLOAT,    [8 | 3] = FYGT_FLOAT,
		[4] = FYGT_FLOAT,    [8 | 4] = FYGT_FLOAT,
		[5] = FYGT_STRING,   [8 | 5] = FYGT_STRING,
		[6] = FYGT_STRING,   [8 | 6] = FYGT_STRING,
		[7] = FYGT_INDIRECT, [8 | 7] = FYGT_INDIRECT,
	};
	const uint8_t *p;
	uint8_t flags;
	enum fy_generic_type type;

	if (v == fy_invalid)
		return FYGT_INVALID;

	if (v == fy_null)
		return FYGT_NULL;

	if (v == fy_true || v == fy_false)
		return FYGT_BOOL;

	type = table[v & 15];
	if (type != FYGT_INDIRECT)
		return type;

	/* get the indirect */
	p = fy_generic_resolve_ptr(v);

	/* an invalid value marks an alias, the value of the alias is at the anchor */
	flags = *p++;
	if (!(flags & FYGIF_VALUE))
		return FYGT_ALIAS;

	/* value immediately follows */
	memcpy(&v, p, sizeof(v));

	if (v == fy_null)
		return FYGT_NULL;

	if (v == fy_true || v == fy_false)
		return FYGT_BOOL;

	type = table[v & 15];
	return type != FYGT_INDIRECT ? type : FYGT_INVALID;
}

static inline bool fy_generic_is_in_place(fy_generic v)
{
	enum fy_generic_type t;

	t = fy_generic_get_type(v);
	/* for int, float, string inplace forms, the in place tag is always odd (bit 0 set) */
	return t <= FYGT_BOOL || (t < FYGT_SEQUENCE && (v & 1));
}

static inline void fy_generic_indirect_get(fy_generic v, struct fy_generic_indirect *gi)
{
	const uint8_t *p;
	uint8_t flags;

	assert(fy_generic_is_indirect(v));
	p = fy_generic_resolve_ptr(v);

	gi->flags = 0;
	gi->value = fy_invalid;
	gi->anchor = fy_invalid;
	gi->tag = fy_invalid;

	/* get flags */
	flags = *p++;
	if (flags & FYGIF_VALUE) {
		memcpy(&gi->value, p, sizeof(gi->value));
		p += sizeof(gi->value);
	}
	if (flags & FYGIF_ANCHOR) {
		memcpy(&gi->anchor, p, sizeof(gi->anchor));
		p += sizeof(gi->anchor);
	}
	if (flags & FYGIF_TAG) {
		memcpy(&gi->tag, p, sizeof(gi->tag));
		p += sizeof(gi->tag);
	}
}

static inline fy_generic fy_generic_indirect_get_value(fy_generic v)
{
	const uint8_t *p;
	uint8_t flags;
	fy_generic vv;

	assert(fy_generic_is_indirect(v));
	p = fy_generic_resolve_ptr(v);
	flags = *p++;
	if (!(flags & FYGIF_VALUE))
		return fy_invalid;
	memcpy(&vv, p, sizeof(vv));
	return vv;
}

static inline fy_generic fy_generic_indirect_get_anchor(fy_generic v)
{
	struct fy_generic_indirect gi;

	fy_generic_indirect_get(v, &gi);
	return gi.anchor;
}

static inline fy_generic fy_generic_indirect_get_tag(fy_generic v)
{
	struct fy_generic_indirect gi;

	fy_generic_indirect_get(v, &gi);
	return gi.tag;
}

static inline fy_generic fy_generic_get_anchor(fy_generic v)
{
	fy_generic va;

	if (!fy_generic_is_indirect(v))
		return fy_null;

	va = fy_generic_indirect_get_anchor(v);
	assert(va == fy_null || va == fy_invalid || fy_generic_get_type(va) == FYGT_STRING);
	return va;
}

static inline fy_generic fy_generic_get_tag(fy_generic v)
{
	fy_generic vt;

	if (!fy_generic_is_indirect(v))
		return fy_null;

	vt = fy_generic_indirect_get_tag(v);
	assert(vt == fy_null || vt == fy_invalid || fy_generic_get_type(vt) == FYGT_STRING);
	return vt;
}

struct fy_generic_sequence {
	size_t count;
	fy_generic items[];
};

struct fy_generic_mapping {
	size_t count;
	fy_generic pairs[];
};

struct fy_generic_builder {
	struct fy_allocator *allocator;
	bool owns_allocator;
	fy_alloc_tag shared_tag;
	fy_alloc_tag alloc_tag;
};

struct fy_generic_builder *fy_generic_builder_create(struct fy_allocator *a, fy_alloc_tag shared_tag);
void fy_generic_builder_destroy(struct fy_generic_builder *gb);
void fy_generic_builder_reset(struct fy_generic_builder *gb);

static inline void *fy_generic_builder_alloc(struct fy_generic_builder *gb, size_t size, size_t align)
{
	assert(gb);
	return fy_allocator_alloc(gb->allocator, gb->alloc_tag, size, align);
}

static inline void fy_generic_builder_free(struct fy_generic_builder *gb, void *ptr)
{
	assert(gb);
	fy_allocator_free(gb->allocator, gb->alloc_tag, ptr);
}

static inline void fy_generic_builder_trim(struct fy_generic_builder *gb)
{
	assert(gb);
	fy_allocator_trim_tag(gb->allocator, gb->alloc_tag);
}

static inline const void *fy_generic_builder_store(struct fy_generic_builder *gb, const void *data, size_t size, size_t align)
{
	assert(gb);
	return fy_allocator_store(gb->allocator, gb->alloc_tag, data, size, align);
}

static inline const void *fy_generic_builder_storev(struct fy_generic_builder *gb, const struct fy_iovecw *iov, unsigned int iovcnt, size_t align)
{
	assert(gb);
	return fy_allocator_storev(gb->allocator, gb->alloc_tag, iov, iovcnt, align);
}

static inline ssize_t fy_generic_builder_get_areas(struct fy_generic_builder *gb, struct fy_iovecw *iov, size_t maxiov)
{
	assert(gb);
	return fy_allocator_get_areas(gb->allocator, gb->alloc_tag, iov, maxiov);
}

static inline const void *fy_generic_builder_get_single_area(struct fy_generic_builder *gb, size_t *sizep, size_t *startp, size_t *allocp)
{
	assert(gb);
	return fy_allocator_get_single_area(gb->allocator, gb->alloc_tag, sizep, startp, allocp);
}

static inline void fy_generic_builder_release(struct fy_generic_builder *gb, const void *ptr, size_t size)
{
	assert(gb);
	fy_allocator_release(gb->allocator, gb->alloc_tag, ptr, size);
}

static inline bool fy_generic_get_bool(fy_generic v)
{
	if (fy_generic_is_indirect(v))
		v = fy_generic_indirect_get_value(v);
	assert(fy_generic_get_type(v) == FYGT_BOOL);
	return (v >> FY_BOOL_INPLACE_SHIFT) != 0;
}

static inline long long fy_generic_get_int(fy_generic v)
{
	long long *p;

	if (fy_generic_is_indirect(v))
		v = fy_generic_indirect_get_value(v);

	assert(fy_generic_get_type(v) == FYGT_INT);
	/* inplace? */
	if ((v & FY_INPLACE_TYPE_MASK) == FY_INT_INPLACE_V)
		return (intptr_t)v >> FY_INPLACE_TYPE_SHIFT;
	p = fy_generic_resolve_ptr(v);
	return *p;
}

static inline double fy_generic_get_float(fy_generic v)
{
#ifndef FY_HAS_64BIT_PTR
	float *pf;
#endif
	double *pd;

	if (fy_generic_is_indirect(v))
		v = fy_generic_indirect_get_value(v);

	assert(fy_generic_get_type(v) == FYGT_FLOAT);

	/* in place for 64 bit, pointer for 32 bit */
	if ((v & FY_INPLACE_TYPE_MASK) == FY_FLOAT_INPLACE_V) {
#ifndef FY_HAS_64BIT_PTR
		pf = fy_generic_resolve_ptr(v);
		return (double)*pf;
#else
#if __BYTE_ORDER == __LITTLE_ENDIAN
		return (double)*((float *)&v + 1);
#else
		return (double)*((float *)&v);
#endif
#endif
	}
	pd = fy_generic_resolve_ptr(v);
	return *pd;
}

static inline const char *fy_generic_get_string_size(fy_generic v, char *inplace, size_t *lenp)
{
	size_t len;

	if (fy_generic_is_indirect(v))
		v = fy_generic_indirect_get_value(v);

	assert(fy_generic_get_type(v) == FYGT_STRING);

	/* in place */
	if ((v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V) {

		len = (v >> FY_STRING_INPLACE_SIZE_SHIFT) & FYGT_STRING_INPLACE_SIZE;
		switch (len) {
		case 0:
			inplace[0] = '\0';
			break;
		case 1:
			inplace[0] = (char)(uint8_t)(v >> 8);
			inplace[1] = '\0';
			break;
		case 2:
			inplace[0] = (char)(uint8_t)(v >> 8);
			inplace[1] = (char)(uint8_t)(v >> 16);
			inplace[2] = '\0';
			break;
		case 3:
			inplace[0] = (char)(uint8_t)(v >> 8);
			inplace[1] = (char)(uint8_t)(v >> 16);
			inplace[2] = (char)(uint8_t)(v >> 24);
			inplace[3] = '\0';
			break;
#ifdef FY_HAS_64BIT_PTR
		case 4:
			inplace[0] = (char)(uint8_t)(v >> 8);
			inplace[1] = (char)(uint8_t)(v >> 16);
			inplace[2] = (char)(uint8_t)(v >> 24);
			inplace[3] = (char)(uint8_t)(v >> 32);
			inplace[4] = '\0';
			break;
		case 5:
			inplace[0] = (char)(uint8_t)(v >> 8);
			inplace[1] = (char)(uint8_t)(v >> 16);
			inplace[2] = (char)(uint8_t)(v >> 24);
			inplace[3] = (char)(uint8_t)(v >> 32);
			inplace[4] = (char)(uint8_t)(v >> 40);
			inplace[5] = '\0';
			break;
		case 6:
			inplace[0] = (char)(uint8_t)(v >> 8);
			inplace[1] = (char)(uint8_t)(v >> 16);
			inplace[2] = (char)(uint8_t)(v >> 24);
			inplace[3] = (char)(uint8_t)(v >> 32);
			inplace[4] = (char)(uint8_t)(v >> 40);
			inplace[5] = (char)(uint8_t)(v >> 48);
			inplace[6] = '\0';
			break;
		case 7:
			inplace[0] = (char)(uint8_t)(v >> 8);
			inplace[1] = (char)(uint8_t)(v >> 16);
			inplace[2] = (char)(uint8_t)(v >> 24);
			inplace[3] = (char)(uint8_t)(v >> 32);
			inplace[4] = (char)(uint8_t)(v >> 40);
			inplace[5] = (char)(uint8_t)(v >> 48);
			inplace[6] = (char)(uint8_t)(v >> 56);
			inplace[7] = '\0';
			break;
#endif
		default:	/* will never happen but the compiler is stupid */
			assert(0);
			len = 0;
			break;

		}
		*lenp = len;
		return inplace;
	}
	return (const char *)fy_decode_size(fy_generic_resolve_ptr(v), FYGT_SIZE_ENCODING_MAX, lenp);
}

static inline const char *fy_generic_get_string(fy_generic v, char *inplace)
{
	size_t size;

	return fy_generic_get_string_size(v, inplace, &size);
}

#define fy_generic_get_string_size_alloca(_v, _lenp) 				\
	({									\
		fy_generic __v = (_v);						\
		char *__inplace = NULL;						\
										\
		if (fy_generic_is_indirect(__v))				\
			__v = fy_generic_indirect_get_value(__v);		\
		assert(fy_generic_get_type(__v) == FYGT_STRING);		\
		if ((__v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V)  	\
			__inplace = alloca(FYGT_STRING_INPLACE_BUF);		\
		fy_generic_get_string_size(__v, __inplace, (_lenp));		\
	})

#define fy_generic_get_string_alloca(_v) 					\
	({									\
		size_t __len;							\
		fy_generic_get_string_size_alloca((_v), &__len);		\
	})

static inline fy_generic fy_generic_null_create(struct fy_generic_builder *gb)
{
	return fy_null;
}

static inline fy_generic fy_generic_bool_create(struct fy_generic_builder *gb, bool state)
{
	return state ? fy_true : fy_false;
}

#define fy_generic_bool_alloca(_v)	((_v) ? fy_true : fy_false)

static inline fy_generic fy_generic_int_create(struct fy_generic_builder *gb, long long val)
{
	const long long *valp;

	if (val >= FYGT_INT_INPLACE_MIN && val <= FYGT_INT_INPLACE_MAX)
		return (fy_generic)(val << FY_INT_INPLACE_SHIFT) | FY_INT_INPLACE_V;

	valp = fy_generic_builder_store(gb, &val, sizeof(val), FY_SCALAR_ALIGNOF(long long));
	if (!valp)
		return fy_invalid;
	assert(((uintptr_t)valp & FY_INPLACE_TYPE_MASK) == 0);
	return (fy_generic)valp | FY_INT_OUTPLACE_V;
}

#define fy_generic_int_alloca(_v) 						\
	({									\
		long long __v = (_v);						\
		long long *__vp;						\
		fy_generic _r;							\
										\
		if (__v >= FYGT_INT_INPLACE_MIN && __v <= FYGT_INT_INPLACE_MAX)	\
			_r = (__v << FY_INT_INPLACE_SHIFT) | FY_INT_INPLACE_V;	\
		else {								\
			__vp = alloca(sizeof(*__vp));				\
			assert(((uintptr_t)__vp & FY_INPLACE_TYPE_MASK) == 0);	\
			*__vp = __v;						\
			_r = (fy_generic)__vp | FY_INT_OUTPLACE_V;		\
		}								\
		_r;								\
	})

fy_generic fy_generic_float_create(struct fy_generic_builder *gb, double val);

static inline bool fy_double_fits_in_float(double val)
{
	float f;

	if (!isnormal(val))
		return true;

	f = (float)val;
	return (double)f == val;
}

#ifdef FY_HAS_64BIT_PTR
#define fy_generic_float_alloca(_v) 								\
	({											\
		double __v = (_v);								\
		double *__vp;									\
		fy_generic _r;									\
		float __f;									\
		uint32_t __fi;									\
												\
		if (fy_double_fits_in_float(__v)) {						\
			__f = (float)__v;							\
			memcpy(&__fi, &__f, sizeof(__fi));					\
			_r = ((fy_generic)__fi << FY_FLOAT_INPLACE_SHIFT) | FY_FLOAT_INPLACE_V;	\
		} else {									\
			__vp = alloca(sizeof(*__vp));						\
			assert(((uintptr_t)__vp & FY_INPLACE_TYPE_MASK) == 0);			\
			*__vp = __v;								\
			_r = (fy_generic)__vp | FY_FLOAT_OUTPLACE_V;				\
		}										\
		_r;										\
	})
#else
#define fy_generic_float_alloca(_v) 					\
	({								\
		double __v = (_v);					\
		double *__vp;						\
		fy_generic _r;						\
									\
		__vp = alloca(sizeof(*__vp));				\
		assert(((uintptr_t)__vp & FY_INPLACE_TYPE_MASK) == 0);	\
		*__vp = __v;						\
		_r = (fy_generic)__vp | FY_FLOAT_OUTPLACE_V;		\
		_r;							\
	})
#endif

fy_generic fy_generic_string_size_create(struct fy_generic_builder *gb, const char *str, size_t len);

static inline fy_generic fy_generic_string_create(struct fy_generic_builder *gb, const char *str)
{
	return fy_generic_string_size_create(gb, str, strlen(str));
}

fy_generic fy_generic_string_vcreate(struct fy_generic_builder *gb, const char *fmt, va_list ap);
fy_generic fy_generic_string_createf(struct fy_generic_builder *gb, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

#ifdef FY_HAS_64BIT_PTR
#define fy_generic_string_size_alloca(_v, _len) 						\
	({											\
		const char *__v = (_v);								\
		size_t __len = (_len);								\
		uint8_t *__vp, *__s;								\
		fy_generic _r;									\
												\
		switch (__len) {								\
		case 0:										\
			_r = ((fy_generic)0) |							\
			     (0 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 1:										\
			_r = ((fy_generic)__v[0] <<  8) |					\
			     (1 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 2:										\
			_r = ((fy_generic)__v[0] <<  8) |					\
			     ((fy_generic)__v[1] << 16) |					\
			     (2 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 3:										\
			_r = ((fy_generic)__v[0] <<  8) |					\
			     ((fy_generic)__v[1] << 16) |					\
			     ((fy_generic)__v[2] << 24) |					\
			     (3 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 4:										\
			_r = ((fy_generic)__v[0] <<  8) |					\
			     ((fy_generic)__v[1] << 16) |					\
			     ((fy_generic)__v[2] << 24) |					\
			     ((fy_generic)__v[3] << 32) |					\
			     (4 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 5:										\
			_r = ((fy_generic)__v[0] <<  8) |					\
			     ((fy_generic)__v[1] << 16) |					\
			     ((fy_generic)__v[2] << 24) |					\
			     ((fy_generic)__v[3] << 32) |					\
			     ((fy_generic)__v[4] << 40) |					\
			     (5 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 6:										\
			_r = ((fy_generic)__v[0] <<  8) |					\
			     ((fy_generic)__v[1] << 16) |					\
			     ((fy_generic)__v[2] << 24) |					\
			     ((fy_generic)__v[3] << 32) |					\
			     ((fy_generic)__v[4] << 40) |					\
			     ((fy_generic)__v[5] << 48) |					\
			     (6 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 7:										\
			_r = ((fy_generic)__v[0] <<  8) |					\
			     ((fy_generic)__v[1] << 16) |					\
			     ((fy_generic)__v[2] << 24) |					\
			     ((fy_generic)__v[3] << 32) |					\
			     ((fy_generic)__v[4] << 40) |					\
			     ((fy_generic)__v[5] << 48) |					\
			     ((fy_generic)__v[6] << 56) |					\
			     (7 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		default:									\
			__vp = alloca(FYGT_SIZE_ENCODING_MAX + __len + 1); 			\
			assert(((uintptr_t)__vp & FY_INPLACE_TYPE_MASK) == 0);			\
			__s = fy_encode_size(__vp, FYGT_SIZE_ENCODING_MAX, __len);		\
			memcpy(__s, __v, __len);						\
			__s[__len] = '\0';							\
			_r = (fy_generic)__vp | FY_STRING_OUTPLACE_V;				\
			break;									\
		}										\
		_r;										\
	})
#else
#define fy_generic_string_size_alloca(_v, _len) 						\
	({											\
		const char *__v = (_v);								\
		size_t __len = (_len);								\
		uint8_t *__vp, *__s;								\
		fy_generic _r;									\
												\
		switch (len) {									\
		case 0:										\
			_r = ((fy_generic)0) |							\
			     (0 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 1:										\
			_r = ((fy_generic)__v[0] <<  8) |					\
			     (1 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 2:										\
			_r = ((fy_generic)__v[0] <<  8) |					\
			     ((fy_generic)__v[1] << 16) |					\
			     (2 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 3:										\
			_r = ((fy_generic)__v[0] <<  8) |					\
			     ((fy_generic)__v[1] << 16) |					\
			     ((fy_generic)__v[2] << 24) |					\
			     (3 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		default:									\
			__vp = alloca(FYGT_SIZE_ENCODING_MAX + __len + 1); 			\
			assert(((uintptr_t)__vp & FY_INPLACE_TYPE_MASK) == 0);			\
			__s = fy_encode_size(__vp, FYGT_SIZE_ENCODING_MAX, __len);		\
			memcpy(__s, __v, __len);						\
			__s[__len] = '\0';							\
			_r = (fy_generic)__vp | FY_STRING_OUTPLACE_V;				\
			break;									\
		}										\
		_r;										\
	})
#endif

#define fy_generic_string_alloca(_v)				\
	({							\
		const char *___v = (_v);			\
		size_t ___len = strlen(___v);			\
		fy_generic_string_size_alloca(___v, ___len);	\
	})

fy_generic fy_generic_sequence_create(struct fy_generic_builder *gb, size_t count, const fy_generic *items);

#define fy_generic_sequence_alloca(_count, _items) 				\
	({									\
		struct fy_generic_sequence *__vp;				\
		size_t __count = (_count);					\
		size_t __size = sizeof(*__vp) + __count * sizeof(fy_generic);	\
										\
		__vp = fy_ptr_align(alloca(__size + 15), 16);			\
		__vp->count = (_count);						\
		memcpy(__vp->items, (_items), __count * sizeof(fy_generic)); 	\
		(fy_generic)__vp | FY_SEQ_V;					\
	})

static inline const fy_generic *fy_generic_sequence_get_items(fy_generic seq, size_t *countp)
{
	struct fy_generic_sequence *p;

	assert(countp);

	if (fy_generic_is_indirect(seq))
		seq = fy_generic_indirect_get_value(seq);

	assert(fy_generic_get_type(seq) == FYGT_SEQUENCE);

	p = fy_generic_resolve_collection_ptr(seq);

	*countp = p->count;
	return &p->items[0];
}

static inline fy_generic fy_generic_sequence_get_item(fy_generic seq, size_t idx)
{
	const fy_generic *items;
	size_t count;

	items = fy_generic_sequence_get_items(seq, &count);
	if (idx >= count)
		return fy_invalid;
	return items[idx];
}

static inline size_t fy_generic_sequence_get_item_count(fy_generic seq)
{
	struct fy_generic_sequence *p;

	if (fy_generic_is_indirect(seq))
		seq = fy_generic_indirect_get_value(seq);

	assert(fy_generic_get_type(seq) == FYGT_SEQUENCE);

	p = fy_generic_resolve_collection_ptr(seq);
	return p->count;
}

fy_generic fy_generic_mapping_create(struct fy_generic_builder *gb, size_t count, const fy_generic *pairs);

#define fy_generic_mapping_alloca(_count, _pairs) 					\
	({										\
		struct fy_generic_mapping *__vp;					\
		size_t __count = (_count);						\
		size_t __size = sizeof(*__vp) + 2 * __count * sizeof(fy_generic);	\
											\
		__vp = fy_ptr_align(alloca(__size + 15), 16);				\
		__vp->count = (_count);							\
		memcpy(__vp->pairs, (_pairs), 2 * __count * sizeof(fy_generic)); 	\
		(fy_generic)__vp | FY_MAP_V;						\
	})

static inline const fy_generic *fy_generic_mapping_get_pairs(fy_generic map, size_t *countp)
{
	struct fy_generic_mapping *p;

	if (fy_generic_is_indirect(map))
		map = fy_generic_indirect_get_value(map);

	assert(fy_generic_get_type(map) == FYGT_MAPPING);

	p = fy_generic_resolve_collection_ptr(map);
	assert(p);
	assert(countp);

	*countp = p->count;
	return p->pairs;
}

static inline size_t fy_generic_mapping_get_pair_count(fy_generic map)
{
	struct fy_generic_mapping *p;

	if (fy_generic_is_indirect(map))
		map = fy_generic_indirect_get_value(map);

	assert(fy_generic_get_type(map) == FYGT_MAPPING);

	p = fy_generic_resolve_collection_ptr(map);
	assert(p);
	return p->count;
}

static inline const char *fy_generic_get_alias_size(fy_generic v, char *inplace, size_t *lenp)
{
	fy_generic va;

	assert(fy_generic_is_indirect(v));

	va = fy_generic_indirect_get_anchor(v);
	return fy_generic_get_string_size(va, inplace, lenp);
}

static inline const char *fy_generic_get_alias(fy_generic v, char *inplace)
{
	size_t size;

	return fy_generic_get_alias_size(v, inplace, &size);
}

#define fy_generic_get_alias_size_alloca(_v, _lenp) \
		fy_generic_get_string_size_alloca( \
				fy_generic_indirect_get_anchor(_v), (_lenp))

#define fy_generic_get_alias_alloca(_v) 				\
	({								\
		size_t __len;						\
		fy_generic_get_alias_size_alloca((_v), &__len);		\
	})

fy_generic fy_generic_mapping_lookup(fy_generic map, fy_generic key);

fy_generic fy_generic_indirect_create(struct fy_generic_builder *gb, const struct fy_generic_indirect *gi);

fy_generic fy_generic_alias_create(struct fy_generic_builder *gb, fy_generic anchor);

int fy_generic_compare_out_of_place(fy_generic a, fy_generic b);

static inline int fy_generic_compare(fy_generic a, fy_generic b)
{
	/* invalids are always non-matching */
	if (a == fy_invalid || b == fy_invalid)
		return -1;

	/* equals? nice - should work for null, bool, in place int, float and strings  */
	/* also for anything that's a pointer */
	if (a == b)
		return 0;

	/* invalid types, or differing types do not match */
	if (fy_generic_get_type(a) != fy_generic_get_type(b))
		return -1;

	return fy_generic_compare_out_of_place(a, b);
}

fy_generic fy_generic_builder_copy_out_of_place(struct fy_generic_builder *gb, fy_generic v);

static inline fy_generic fy_generic_builder_copy(struct fy_generic_builder *gb, fy_generic v)
{
	if (v == fy_invalid)
		return fy_invalid;

	if (!fy_generic_is_indirect(v)) {

		switch (fy_generic_get_type(v)) {
		case FYGT_NULL:
		case FYGT_BOOL:
			return v;

		case FYGT_INT:
			if (v & FY_INT_INPLACE_V)
				return v;
			break;

		case FYGT_FLOAT:
			if (v & FY_FLOAT_INPLACE_V)
				return v;
			break;

		case FYGT_STRING:
			if (v & FY_STRING_INPLACE_V)
				return v;
			break;

		default:
			break;
		}
	}

	return fy_generic_builder_copy_out_of_place(gb, v);
}

fy_generic fy_generic_relocate(void *start, void *end, fy_generic v, ptrdiff_t d);

enum fy_generic_schema {
	FYGS_AUTO,
	FYGS_FALLBACK,
	FYGS_YAML1_1,
	FYGS_YAML1_2,
	FYGS_JSON,
};

#endif
