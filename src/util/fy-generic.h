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

#if defined(HAVE_STRICT_ALIASING) && HAVE_STRICT_ALIASING && defined(__GNUC__) && !defined(__clang__)
#define GCC_DISABLE_WSTRICT_ALIASING
#endif

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
#define FYGT_STRING_INPLACE_SIZE_64 6
#define FYGT_STRING_INPLACE_SIZE_MASK_64 7
#define FYGT_SIZE_ENCODING_MAX_64 FYVL_SIZE_ENCODING_MAX_64

#define FYGT_INT_INPLACE_BITS_32 29
#define FYGT_STRING_INPLACE_SIZE_32 2
#define FYGT_STRING_INPLACE_SIZE_MASK_32 3
#define FYGT_SIZE_ENCODING_MAX_32 FYVL_SIZE_ENCODING_MAX_32

#ifdef FY_HAS_64BIT_PTR
#define FYGT_INT_INPLACE_BITS FYGT_INT_INPLACE_BITS_64
#define FYGT_STRING_INPLACE_SIZE FYGT_STRING_INPLACE_SIZE_64
#define FYGT_STRING_INPLACE_SIZE_MASK FYGT_STRING_INPLACE_SIZE_MASK_64
#else
#define FYGT_INT_INPLACE_BITS FYGT_INT_INPLACE_BITS_32
#define FYGT_STRING_INPLACE_SIZE FYGT_STRING_INPLACE_SIZE_32
#define FYGT_STRING_INPLACE_SIZE_MASK FYGT_STRING_INPLACE_SIZE_MASK_32
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

#define FYGT_INT_INPLACE_MAX ((1LL << (FYGT_INT_INPLACE_BITS - 1)) - 1)
#define FYGT_INT_INPLACE_MIN (-(1LL << (FYGT_INT_INPLACE_BITS - 1)))

#define FY_GENERIC_CONTAINER_ALIGNMENT __attribute__((aligned(16)))
#define FY_GENERIC_EXTERNAL_ALIGNMENT FY_GENERIC_CONTAINER_ALIGNMENT

/* yes, plenty of side-effects, use it with care */
#define FY_MAX_ALIGNOF(_v, _min) ((size_t)(alignof(_v) > (_min) ? alignof(_v) : (_min)))
#define FY_CONTAINER_ALIGNOF(_v) FY_MAX_ALIGNOF(_v, 16)
#define FY_SCALAR_ALIGNOF(_v) FY_MAX_ALIGNOF(_v, 8)

#define FY_INT_ALIGNMENT  __attribute__((aligned(FY_SCALAR_ALIGNOF(long long))))
#define FY_FLOAT_ALIGNMENT __attribute__((aligned(FY_SCALAR_ALIGNOF(double))))
#define FY_STRING_ALIGNMENT __attribute__((aligned(8)))

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
	void *linear;	/* when making it linear */
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

static inline struct fy_allocator_info *
fy_generic_builder_get_allocator_info(struct fy_generic_builder *gb)
{
	assert(gb);
	return fy_allocator_get_info(gb->allocator, gb->alloc_tag);
}

static inline void fy_generic_builder_release(struct fy_generic_builder *gb, const void *ptr, size_t size)
{
	assert(gb);
	fy_allocator_release(gb->allocator, gb->alloc_tag, ptr, size);
}

#define fy_generic_get_bool(_v)							\
	({									\
		fy_generic __v = (_v);						\
										\
		if (fy_generic_is_indirect(__v))				\
			__v = fy_generic_indirect_get_value(__v);		\
		assert(fy_generic_get_type(__v) == FYGT_BOOL);			\
		(bool)((__v >> FY_BOOL_INPLACE_SHIFT) != 0);			\
	})

#define fy_generic_get_int(_v)							\
	({									\
		fy_generic __v = (_v);						\
										\
		if (fy_generic_is_indirect(__v))				\
			__v = fy_generic_indirect_get_value(__v);		\
		assert(fy_generic_get_type(__v) == FYGT_INT);			\
		(__v & FY_INPLACE_TYPE_MASK) == FY_INT_INPLACE_V ? 		\
			(long long)(__v >> FY_INPLACE_TYPE_SHIFT) : 		\
			*(long long *)fy_generic_resolve_ptr(__v);		\
	})

#ifdef FY_HAS_64BIT_PTR

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define FY_INPLACE_FLOAT_ADV	1
#else
#define FY_INPLACE_FLOAT_ADV	0
#endif

#define fy_generic_get_float(_v)						\
	({									\
		fy_generic __v = (_v);						\
										\
		if (fy_generic_is_indirect(__v))				\
			__v = fy_generic_indirect_get_value(__v);		\
		assert(fy_generic_get_type(__v) == FYGT_FLOAT);			\
		(__v & FY_INPLACE_TYPE_MASK) == FY_FLOAT_INPLACE_V ? 		\
			(double)*((float *)&__v + FY_INPLACE_FLOAT_ADV) :	\
			*(double *)fy_generic_resolve_ptr(__v);			\
	})

#else

#define fy_generic_get_float(_v)						\
	({									\
		fy_generic __v = (_v);						\
										\
		if (fy_generic_is_indirect(__v))				\
			__v = fy_generic_indirect_get_value(__v);		\
		assert(fy_generic_get_type(__v) == FYGT_FLOAT);			\
		(__v & FY_INPLACE_TYPE_MASK) == FY_FLOAT_INPLACE_V ? 		\
			(double)*(float *)fy_generic_resolve_ptr(__v);		\
			*(double *)fy_generic_resolve_ptr(__v);			\
	})
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define FY_INPLACE_STRING_ADV	1
#else
#define FY_INPLACE_STRING_ADV	0
#endif

/* if we can get the address of the argument, then we can return a pointer to it */

#define fy_generic_get_string_size(_v, _lenp)					\
	({									\
		fy_generic __v = (_v);						\
		size_t *__lenp = (_lenp);					\
		size_t __len;							\
		const char *__str;						\
										\
		if (fy_generic_is_indirect(__v))				\
			__v = fy_generic_indirect_get_value(__v);		\
		assert(fy_generic_get_type(__v) == FYGT_STRING);		\
		if ((__v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V) {	\
			__len = (__v >> FY_STRING_INPLACE_SIZE_SHIFT) & 	\
					FYGT_STRING_INPLACE_SIZE_MASK;		\
			__str = (const char *)					\
				_Generic(&(_v),					\
					fy_generic *: \
						(const char *)&(_v) + FY_INPLACE_STRING_ADV, \
					default: \
						memcpy(alloca(__len + 1), \
							(const char *)&__v + FY_INPLACE_STRING_ADV, \
							__len + 1));		\
			*__lenp = __len;					\
		} else								\
			__str = (const char *)fy_decode_size(			\
					fy_generic_resolve_ptr(__v), 		\
					FYGT_SIZE_ENCODING_MAX, __lenp);	\
		__str;								\
	})

#define fy_generic_get_string(_v)						\
	({									\
		fy_generic __v = (_v);						\
		size_t __len;							\
		const char *__str;						\
										\
		if (fy_generic_is_indirect(__v))				\
			__v = fy_generic_indirect_get_value(__v);		\
		assert(fy_generic_get_type(__v) == FYGT_STRING);		\
		if ((__v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V) { 	\
			__len = (__v >> FY_STRING_INPLACE_SIZE_SHIFT) & 	\
					FYGT_STRING_INPLACE_SIZE_MASK;		\
			__str = (const char *)					\
				_Generic(&(_v),					\
					fy_generic *: \
						(const char *)&(_v) + FY_INPLACE_STRING_ADV, \
					default: \
						memcpy(alloca(__len + 1), \
							(const char *)&__v + FY_INPLACE_STRING_ADV, \
							__len + 1));		\
		} else								\
			__str = (const char *)fy_skip_size(			\
					fy_generic_resolve_ptr(__v), 		\
					FYGT_SIZE_ENCODING_MAX);		\
		__str;								\
	})

static inline fy_generic fy_generic_null_create(struct fy_generic_builder *gb)
{
	return fy_null;
}

static inline fy_generic fy_generic_bool_create(struct fy_generic_builder *gb, bool state)
{
	return state ? fy_true : fy_false;
}

#define fy_bool(_v)			((bool)(_v) ? fy_true : fy_false)
#define fy_bool_alloca(_v)		fy_bool(_v)

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

#define fy_int_alloca(_v) 									\
	({											\
		long long __v = (_v);								\
		long long *__vp;								\
		fy_generic _r;									\
												\
		if (__v >= FYGT_INT_INPLACE_MIN && __v <= FYGT_INT_INPLACE_MAX)			\
			_r = (__v << FY_INT_INPLACE_SHIFT) | FY_INT_INPLACE_V;			\
		else {										\
			__vp = alloca(sizeof(*__vp));						\
			assert(((uintptr_t)__vp & FY_INPLACE_TYPE_MASK) == 0);			\
			*__vp = __v;								\
			_r = (fy_generic)__vp | FY_INT_OUTPLACE_V;				\
		}										\
		_r;										\
	})

/* note the builtin_constant_p guard; this makes the fy_int() macro work */
#define fy_int_const(_v)									\
	({											\
		fy_generic _r;									\
												\
		if ((_v) >= FYGT_INT_INPLACE_MIN && (_v) <= FYGT_INT_INPLACE_MAX)		\
			_r = (fy_generic)(((_v) << FY_INT_INPLACE_SHIFT) | FY_INT_INPLACE_V);	\
		else {										\
			static const long long _vv FY_INT_ALIGNMENT =				\
				__builtin_constant_p(_v) ? (_v) : 0;				\
			assert(((uintptr_t)&_vv & FY_INPLACE_TYPE_MASK) == 0);			\
			_r = (fy_generic)&_vv | FY_INT_OUTPLACE_V;				\
		}										\
		_r;										\
	})

#define fy_int(_v) (__builtin_constant_p(_v) ? fy_int_const(_v) : fy_int_alloca(_v))

fy_generic fy_generic_float_create(struct fy_generic_builder *gb, double val);

#ifdef FY_HAS_64BIT_PTR

#define fy_float_alloca(_v) 									\
	({											\
		double __v = (_v);								\
		double *__vp;									\
		fy_generic _r;									\
												\
		if (!isnormal(__v) || (float)__v == __v) {					\
			const union { float f; uint32_t f_bits; } __u = { .f = (float)__v };	\
			_r = ((fy_generic)__u.f_bits << FY_FLOAT_INPLACE_SHIFT) | 		\
				FY_FLOAT_INPLACE_V;						\
		} else {									\
			__vp = alloca(sizeof(*__vp));						\
			assert(((uintptr_t)__vp & FY_INPLACE_TYPE_MASK) == 0);			\
			*__vp = __v;								\
			_r = (fy_generic)__vp | FY_FLOAT_OUTPLACE_V;				\
		}										\
		_r;										\
	})

/* note the builtin_constant_p guard; this makes the fy_float() macro work */
#define fy_float_const(_v)									\
	({											\
		fy_generic _r;									\
												\
		if (!isnormal(_v) || (float)(_v) == (double)(_v)) {				\
			const union { float f; uint32_t f_bits; } __u =				\
				{ .f = __builtin_constant_p(_v) ? (float)(_v) : 0.0 };		\
			_r = ((fy_generic)__u.f_bits  << FY_FLOAT_INPLACE_SHIFT) |		\
				FY_FLOAT_INPLACE_V;						\
		} else {									\
			static const double _vv FY_FLOAT_ALIGNMENT = 				\
				__builtin_constant_p(_v) ? (_v) : 0;				\
			assert(((uintptr_t)&_vv & FY_INPLACE_TYPE_MASK) == 0);			\
			_r = (fy_generic)&_vv | FY_FLOAT_OUTPLACE_V;				\
		}										\
		_r;										\
	})

#else

#define fy_float_alloca(_v) 						\
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

/* note the builtin_constant_p guard; this makes the fy_float() macro work */
#define fy_float_const(_v)						\
	({								\
		fy_generic _r;						\
									\
		static const double _vv FY_FLOAT_ALIGNMENT = 		\
			__builtin_constant_p(_v) ? (_v) : 0;		\
		assert(((uintptr_t)&_vv & FY_INPLACE_TYPE_MASK) == 0);	\
		_r = (fy_generic)&_vv | FY_FLOAT_OUTPLACE_V;		\
		_r;							\
	})

#endif

#define fy_float(_v) (__builtin_constant_p(_v) ? fy_float_const(_v) : fy_float_alloca(_v))

fy_generic fy_generic_string_size_create(struct fy_generic_builder *gb, const char *str, size_t len);

static inline fy_generic fy_generic_string_create(struct fy_generic_builder *gb, const char *str)
{
	return fy_generic_string_size_create(gb, str, strlen(str));
}

fy_generic fy_generic_string_vcreate(struct fy_generic_builder *gb, const char *fmt, va_list ap);
fy_generic fy_generic_string_createf(struct fy_generic_builder *gb, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

/* literal strings in C, are char *
 * However you can't get a type of & "literal"
 * So we use that to detect literal strings
 */
#define FY_CONST_P_INIT(_v) \
    (_Generic((_v), \
        char *: _Generic(&(_v), char **: "", default: (_v)), \
        default: "" ))


#define FY_GENERIC_LVALUE(_v) \
    (_Generic((_v), \
        fy_generic: _Generic(&(_v), char **: "", default: (_v)), \
        default: "" ))

#ifdef FY_HAS_64BIT_PTR

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define FY_STRING_SHIFT7(_v0, _v1, _v2, _v3, _v4, _v5, _v6) \
	(\
		((fy_generic)(uint8_t)_v0 <<  8) | \
		((fy_generic)(uint8_t)_v1 << 16) | \
		((fy_generic)(uint8_t)_v2 << 24) | \
		((fy_generic)(uint8_t)_v3 << 32) | \
		((fy_generic)(uint8_t)_v4 << 40) | \
		((fy_generic)(uint8_t)_v5 << 48) | \
		((fy_generic)(uint8_t)_v6 << 56) \
	)
#else
#define FY_STRING_SHIFT7(_v0, _v1, _v2, _v3, _v4, _v5, _v6) \
	(\
		((fy_generic)(uint8_t)_v6 <<  8) | \
		((fy_generic)(uint8_t)_v5 << 16) | \
		((fy_generic)(uint8_t)_v4 << 24) | \
		((fy_generic)(uint8_t)_v3 << 32) | \
		((fy_generic)(uint8_t)_v2 << 40) | \
		((fy_generic)(uint8_t)_v1 << 48) | \
		((fy_generic)(uint8_t)_v0 << 56) \
	)
#endif

#define fy_string_size_alloca(_v, _len) 							\
	({											\
		const char *__v = (_v);								\
		size_t __len = (_len);								\
		uint8_t *__vp, *__s;								\
		fy_generic _r;									\
												\
		switch (__len) {								\
		case 0:										\
			_r = (0 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 1:										\
			_r = FY_STRING_SHIFT7(__v[0], 0, 0, 0, 0, 0, 0) |			\
			     (1 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 2:										\
			_r = FY_STRING_SHIFT7(__v[0], __v[1], 0, 0, 0, 0, 0) |			\
			     (2 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 3:										\
			_r = FY_STRING_SHIFT7(__v[0], __v[1], __v[2], 0, 0, 0, 0) |		\
			     (3 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 4:										\
			_r = FY_STRING_SHIFT7(__v[0], __v[1], __v[2], __v[3], 0, 0, 0) |	\
			     (4 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 5:										\
			_r = FY_STRING_SHIFT7(__v[0], __v[1], __v[2], __v[3], __v[4], 0, 0) |	\
			     (5 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 6:										\
			_r = FY_STRING_SHIFT7(__v[0], __v[1], __v[2], __v[3], __v[4], 		\
					      __v[5], 0) |					\
			     (6 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
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

#define fy_string_size_const(_v, _len) 								\
	({											\
		fy_generic _r;									\
												\
		switch (_len) {									\
		case 0:										\
			_r = (0 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 1:										\
			_r = FY_STRING_SHIFT7((_v)[0], 0, 0, 0, 0, 0, 0) |			\
			     (1 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 2:										\
			_r = FY_STRING_SHIFT7((_v)[0], (_v)[1], 0, 0, 0, 0, 0) |		\
			     (2 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 3:										\
			_r = FY_STRING_SHIFT7((_v)[0], (_v)[1], (_v)[2], 0, 0, 0, 0) |		\
			     (3 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 4:										\
			_r = FY_STRING_SHIFT7((_v)[0], (_v)[1], (_v)[2], (_v)[3], 0, 0, 0) |	\
			     (4 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 5:										\
			_r = FY_STRING_SHIFT7((_v)[0], (_v)[1], (_v)[2], (_v)[3], 		\
					      (_v)[4], 0, 0) |					\
			     (5 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 6:										\
			_r = FY_STRING_SHIFT7((_v)[0], (_v)[1], (_v)[2], (_v)[3], 		\
					      (_v)[4], (_v)[5], 0) |				\
			     (6 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		default:									\
			if ((_len) < ((uint64_t)1 <<  7)) {					\
				static const struct {						\
					uint8_t l0;						\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? (uint8_t)(_len) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic)&_s | FY_STRING_OUTPLACE_V;			\
			} else if ((_len) < ((uint64_t)1 << 14)) {				\
				static const struct {						\
					uint8_t l0, l1;						\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 7)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)(_len) & 0x7f) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic)&_s | FY_STRING_OUTPLACE_V;			\
			} else if ((_len) < ((uint64_t)1 << 21)) {				\
				static const struct {						\
					uint8_t l0, l1, l2;					\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 14)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >>  7)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)(_len) & 0x7f) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic)&_s | FY_STRING_OUTPLACE_V;			\
			} else if ((_len) < ((uint64_t)1 << 28)) {				\
				static const struct {						\
					uint8_t l0, l1, l2, l3;					\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 21)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 14)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >>  7)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)(_len) & 0x7f) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic)&_s | FY_STRING_OUTPLACE_V;			\
			} else if ((_len) < ((uint64_t)1 << 35)) {				\
				static const struct {						\
					uint8_t l0, l1, l2, l3, l4;				\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 28)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 21)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 14)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >>  7)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)(_len) & 0x7f) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic)&_s | FY_STRING_OUTPLACE_V;			\
			} else if ((_len) < ((uint64_t)1 << 42)) {				\
				static const struct {						\
					uint8_t l0, l1, l2, l3, l4, l5;				\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 35)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 28)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 21)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 14)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >>  7)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)(_len) & 0x7f) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic)&_s | FY_STRING_OUTPLACE_V;			\
			} else if ((_len) < ((uint64_t)1 << 49)) {				\
				static const struct {						\
					uint8_t l0, l1, l2, l3, l4, l5, l6;			\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 42)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 35)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 28)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 21)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 14)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >>  7)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)(_len) & 0x7f) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic)&_s | FY_STRING_OUTPLACE_V;			\
			} else if ((_len) < ((uint64_t)1 << 56)) {				\
				static const struct {						\
					uint8_t l0, l1, l2, l3, l4, l5, l6, l7;			\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 49)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 42)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 35)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 28)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 21)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 14)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >>  7)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)(_len) & 0x7f) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic)&_s | FY_STRING_OUTPLACE_V;			\
			} else {								\
				static const struct {						\
					uint8_t l0, l1, l2, l3, l4, l5, l6, l7, l8;		\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 57)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 50)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 43)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 36)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 29)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 22)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 15)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >>  8)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? (uint8_t)(_len) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic)&_s | FY_STRING_OUTPLACE_V;			\
			}									\
		}										\
		_r;										\
	})

#else

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define FY_STRING_SHIFT3(_v0, _v1, _v2) \
	(\
		((fy_generic)(uint8_t)_v0 <<  8) | \
		((fy_generic)(uint8_t)_v1 << 16) | \
		((fy_generic)(uint8_t)_v2 << 24)   \
	)
#else
#define FY_STRING_SHIFT3(_v0, _v1, _v2) \
	(\
		((fy_generic)(uint8_t)_v2 <<  8) | \
		((fy_generic)(uint8_t)_v1 << 16) | \
		((fy_generic)(uint8_t)_v0 << 24) | \
	)
#endif


#define fy_string_size_alloca(_v, _len) 							\
	({											\
		const char *__v = (_v);								\
		size_t __len = (_len);								\
		uint8_t *__vp, *__s;								\
		fy_generic _r;									\
												\
		switch (len) {									\
		case 0:										\
			_r = (0 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 1:										\
			_r = FY_STRING_SHIFT3(__v[0], 0, 0) |					\
			     (1 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 2:										\
			_r = FY_STRING_SHIFT3(__v[0], __v[1], 0) |				\
			     (2 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
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

#define fy_string_size_const(_v, _len) 								\
	({											\
		fy_generic _r;									\
												\
		switch (_len) {									\
		case 0:										\
			_r = (0 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 1:										\
			_r = FY_STRING_SHIFT3((_v)[0], 0, 0) |					\
			     (1 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		case 2:										\
			_r = FY_STRING_SHIFT3((_v)[0], (_v)[1], 0) |				\
			     (2 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;		\
			break;									\
		default:									\
			if ((_len) < ((uint64_t)1 <<  7)) {					\
				static const struct {						\
					uint8_t l0;						\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? (uint8_t)(_len) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic)&_s | FY_STRING_OUTPLACE_V;			\
			} else if ((_len) < ((uint64_t)1 << 14)) {				\
				static const struct {						\
					uint8_t l0, l1;						\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 7)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)(_len) & 0x7f) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic)&_s | FY_STRING_OUTPLACE_V;			\
			} else if ((_len) < ((uint64_t)1 << 21)) {				\
				static const struct {						\
					uint8_t l0, l1, l2;					\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 14)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >>  7)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)(_len) & 0x7f) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic)&_s | FY_STRING_OUTPLACE_V;			\
			} else if ((_len) < ((uint64_t)1 << 28)) {				\
				static const struct {						\
					uint8_t l0, l1, l2, l3;					\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 21)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 14)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >>  7)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)(_len) & 0x7f) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic)&_s | FY_STRING_OUTPLACE_V;			\
			} else {								\
				static const struct {						\
					uint8_t l0, l1, l2, l3, l4;				\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 29)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 22)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >> 15)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? ((uint8_t)((_len) >>  8)) | 0x80 : 0,	\
					__builtin_constant_p(_len) ? (uint8_t)(_len) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic)&_s | FY_STRING_OUTPLACE_V;			\
			}									\
		}										\
		_r;										\
	})

#endif

#define fy_string_alloca(_v)					\
	({							\
		const char *___v = (_v);			\
		size_t ___len = strlen(___v);			\
		fy_string_size_alloca(___v, ___len);		\
	})

#define fy_string_const(_v) fy_string_size_const((_v), strlen(_v))

#define fy_string_size(_v, _len) (__builtin_constant_p(_v) ? fy_string_size_const((_v), (_len)) : fy_string_size_alloca((_v), (_len)))
#define fy_string(_v) (__builtin_constant_p(_v) ? fy_string_const(_v) : fy_string_alloca(_v))

fy_generic fy_generic_sequence_create(struct fy_generic_builder *gb, size_t count, const fy_generic *items);

#define fy_sequence_alloca(_count, _items) 					\
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

#if 0
#define fy_sequence_const(_count, ...) \
	({ \
		static const struct fy_generic_sequence _seq FY_GENERIC_CONTAINER_ALIGNMENT = { \
			.count = (_count), \
	 		.items = { __VA_ARGS__ }}, \
	 	}; \
		(fy_generic)&_seq | FY_SEQ_V;					\
	})
#endif

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

#define fy_mapping_alloca(_count, _pairs) 						\
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

#define fy_generic_get_alias_size(_v, _lenp)				\
	({								\
		fy_generic ___v = fy_generic_indirect_get_anchor(_v);	\
		fy_generic_get_string_size(___v, (_lenp));		\
	 })

#define fy_generic_get_alias(_v)					\
	({								\
		fy_generic ___v = fy_generic_indirect_get_anchor(_v);	\
		fy_generic_get_string(___v);				\
	})

fy_generic fy_generic_mapping_lookup(fy_generic map, fy_generic key);

fy_generic fy_generic_indirect_create(struct fy_generic_builder *gb, const struct fy_generic_indirect *gi);

fy_generic fy_generic_alias_create(struct fy_generic_builder *gb, fy_generic anchor);

enum fy_generic_schema {
	FYGS_YAML1_2_FAILSAFE,
	FYGS_YAML1_2_CORE,
	FYGS_YAML1_2_JSON,
	FYGS_YAML1_1,
	FYGS_JSON,
};

static inline bool fy_generic_schema_is_json(enum fy_generic_schema schema)
{
	return schema == FYGS_YAML1_2_JSON || schema == FYGS_JSON;
}

fy_generic fy_generic_create_scalar_from_text(struct fy_generic_builder *gb, enum fy_generic_schema schema, const char *text, size_t len, enum fy_generic_type force_type);

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

#endif
