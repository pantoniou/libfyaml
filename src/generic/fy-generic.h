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
typedef uintptr_t fy_generic_value;

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

#ifdef FY_HAS_64BIT_PTR

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define FY_STRING_SHIFT7(_v0, _v1, _v2, _v3, _v4, _v5, _v6) \
	(\
		((fy_generic_value)(uint8_t)_v0 <<  8) | \
		((fy_generic_value)(uint8_t)_v1 << 16) | \
		((fy_generic_value)(uint8_t)_v2 << 24) | \
		((fy_generic_value)(uint8_t)_v3 << 32) | \
		((fy_generic_value)(uint8_t)_v4 << 40) | \
		((fy_generic_value)(uint8_t)_v5 << 48) | \
		((fy_generic_value)(uint8_t)_v6 << 56) \
	)
#else
#define FY_STRING_SHIFT7(_v0, _v1, _v2, _v3, _v4, _v5, _v6) \
	(\
		((fy_generic_value)(uint8_t)_v6 <<  8) | \
		((fy_generic_value)(uint8_t)_v5 << 16) | \
		((fy_generic_value)(uint8_t)_v4 << 24) | \
		((fy_generic_value)(uint8_t)_v3 << 32) | \
		((fy_generic_value)(uint8_t)_v2 << 40) | \
		((fy_generic_value)(uint8_t)_v1 << 48) | \
		((fy_generic_value)(uint8_t)_v0 << 56) \
	)
#endif

#else

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define FY_STRING_SHIFT3(_v0, _v1, _v2) \
	(\
		((fy_generic_value)(uint8_t)_v0 <<  8) | \
		((fy_generic_value)(uint8_t)_v1 << 16) | \
		((fy_generic_value)(uint8_t)_v2 << 24)   \
	)
#else
#define FY_STRING_SHIFT3(_v0, _v1, _v2) \
	(\
		((fy_generic_value)(uint8_t)_v2 <<  8) | \
		((fy_generic_value)(uint8_t)_v1 << 16) | \
		((fy_generic_value)(uint8_t)_v0 << 24) | \
	)
#endif

#endif


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
#define FY_INPLACE_TYPE_MASK	(((fy_generic_value)1 << FY_INPLACE_TYPE_SHIFT) - 1)

#define FY_NULL_V		0
#define FY_SEQ_V		0
#define FY_MAP_V		8
#define FY_COLLECTION_MASK	(((fy_generic_value)1 << (FY_INPLACE_TYPE_SHIFT + 1)) - 1)

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

#define fy_null_value		((fy_generic_value)0)
#define fy_false_value		((fy_generic_value)8)
#define fy_true_value		(~(fy_generic_value)7)
#define fy_invalid_value	((fy_generic_value)-1)

#define FYGT_INT_INPLACE_MAX ((1LL << (FYGT_INT_INPLACE_BITS - 1)) - 1)
#define FYGT_INT_INPLACE_MIN (-(1LL << (FYGT_INT_INPLACE_BITS - 1)))

#define FY_GENERIC_CONTAINER_ALIGN	16
#define FY_GENERIC_EXTERNAL_ALIGN	FY_GENERIC_CONTAINER_ALIGN
#define FY_GENERIC_SCALAR_ALIGN		8

#define FY_GENERIC_CONTAINER_ALIGNMENT __attribute__((aligned(FY_GENERIC_CONTAINER_ALIGN)))
#define FY_GENERIC_EXTERNAL_ALIGNMENT FY_GENERIC_CONTAINER_ALIGNMENT

/* yes, plenty of side-effects, use it with care */
#define FY_MAX_ALIGNOF(_v, _min) ((size_t)(alignof(_v) > (_min) ? alignof(_v) : (_min)))
#define FY_CONTAINER_ALIGNOF(_v) FY_MAX_ALIGNOF(_v, FY_GENERIC_CONTAINER_ALIGN)
#define FY_SCALAR_ALIGNOF(_v) FY_MAX_ALIGNOF(_v, FY_GENERIC_SCALAR_ALIGN)

#define FY_INT_ALIGNMENT  __attribute__((aligned(FY_SCALAR_ALIGNOF(long long))))
#define FY_FLOAT_ALIGNMENT __attribute__((aligned(FY_SCALAR_ALIGNOF(double))))
#define FY_STRING_ALIGNMENT __attribute__((aligned(FY_GENERIC_SCALAR_ALIGN)))

/* the encoded type */
typedef struct fy_generic {
	fy_generic_value v;
} fy_generic;

#define fy_null		((fy_generic){ .v = fy_null_value })	/* simple does it */
#define fy_false	((fy_generic){ .v = fy_false_value })
#define fy_true		((fy_generic){ .v = fy_true_value })
#define fy_invalid	((fy_generic){ .v = fy_invalid_value })

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
	if (v.v == fy_invalid_value)
		return false;

	if ((v.v & FY_INPLACE_TYPE_MASK) != FY_INDIRECT_V)
		return false;

	return true;
}

static inline void *fy_generic_resolve_ptr(fy_generic ptr)
{
	/* clear the top 3 bits (all pointers are 8 byte aligned) */
	/* note collections have the bit 3 cleared too, so it's 16 byte aligned */
	ptr.v &= ~(uintptr_t)FY_INPLACE_TYPE_MASK;
	return (void *)ptr.v;
}

static inline void *fy_generic_resolve_collection_ptr(fy_generic ptr)
{
	/* clear the top 3 bits (all pointers are 8 byte aligned) */
	/* note collections have the bit 3 cleared too, so it's 16 byte aligned */
	ptr.v &= ~(uintptr_t)FY_COLLECTION_MASK;
	return (void *)ptr.v;
}

static inline fy_generic fy_generic_relocate_ptr(fy_generic v, ptrdiff_t d)
{
	v.v = (fy_generic_value)((ptrdiff_t)((uintptr_t)v.v & ~(uintptr_t)FY_INPLACE_TYPE_MASK) + d);
	assert((v.v & (uintptr_t)FY_INPLACE_TYPE_MASK) == 0);
	return v;
}

static inline fy_generic fy_generic_relocate_collection_ptr(fy_generic v, ptrdiff_t d)
{
	v.v = (fy_generic_value)((ptrdiff_t)((uintptr_t)v.v & ~(uintptr_t)FY_COLLECTION_MASK) + d);
	assert((v.v & (uintptr_t)FY_COLLECTION_MASK) == 0);
	return v;
}

static inline enum fy_generic_type fy_generic_get_direct_type(fy_generic v)
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

	if (v.v == fy_invalid_value)
		return FYGT_INVALID;

	if (v.v == fy_null_value)
		return FYGT_NULL;

	if (v.v == fy_true_value || v.v == fy_false_value)
		return FYGT_BOOL;

	return table[v.v & 15];
}

static inline enum fy_generic_type fy_generic_get_type(fy_generic v)
{
	const fy_generic_value *p;
	uintptr_t flags;
	enum fy_generic_type type;

	type = fy_generic_get_direct_type(v);
	if (type != FYGT_INDIRECT)
		return type;

	/* get the indirect */
	p = fy_generic_resolve_ptr(v);

	/* an invalid value marks an alias, the value of the alias is at the anchor */
	flags = *p++;
	if (!(flags & FYGIF_VALUE))
		return FYGT_ALIAS;
	/* value immediately follows */
	v.v = *p;
	type = fy_generic_get_direct_type(v);
	return type != FYGT_INDIRECT ? type : FYGT_INVALID;
}

static inline void fy_generic_indirect_get(fy_generic v, struct fy_generic_indirect *gi)
{
	const fy_generic_value *p;

	memset(gi, 0, sizeof(*gi));

	if (!fy_generic_is_indirect(v)) {
		gi->flags = FYGIF_VALUE;
		gi->value = v;
		gi->anchor.v = fy_invalid_value;
		gi->tag.v = fy_invalid_value;
		return;
	}

	p = fy_generic_resolve_ptr(v);

	/* get flags */
	gi->flags = *p++;
	if (gi->flags & FYGIF_VALUE)
		gi->value.v = *p++;
	else
		gi->value.v = fy_invalid_value;
	if (gi->flags & FYGIF_ANCHOR)
		gi->anchor.v = *p++;
	else
		gi->anchor.v = fy_invalid_value;
	if (gi->flags & FYGIF_TAG)
		gi->tag.v = *p++;
	else
		gi->tag.v = fy_invalid_value;
}

static inline fy_generic fy_generic_indirect_get_value(fy_generic v)
{
	const fy_generic_value *p;
	uintptr_t flags;

	if (!fy_generic_is_indirect(v))
		return v;

	p = fy_generic_resolve_ptr(v);
	flags = *p++;
	v.v = (flags & FYGIF_VALUE) ? *p : fy_invalid_value;
	return v;
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
	assert(va.v == fy_null_value || va.v == fy_invalid_value || fy_generic_get_type(va) == FYGT_STRING);
	return va;
}

static inline fy_generic fy_generic_get_tag(fy_generic v)
{
	fy_generic vt;

	if (!fy_generic_is_indirect(v))
		return fy_null;

	vt = fy_generic_indirect_get_tag(v);
	assert(vt.v == fy_null_value || vt.v == fy_invalid_value || fy_generic_get_type(vt) == FYGT_STRING);
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

static inline bool fy_generic_is_valid(const fy_generic v)
{
	return v.v != fy_invalid_value;
}

static inline bool fy_generic_is_invalid(const fy_generic v)
{
	return v.v == fy_invalid_value;
}

static inline bool fy_generic_is_null(const fy_generic v)
{
	return v.v == fy_null_value || fy_generic_get_type(v) == FYGT_NULL;
}

static inline bool fy_generic_is_bool(const fy_generic v)
{
	return v.v == fy_true_value || v.v == fy_false_value || fy_generic_get_type(v) == FYGT_BOOL;
}

static inline bool fy_generic_is_int(const fy_generic v)
{
	return fy_generic_get_type(v) == FYGT_INT;
}

static inline bool fy_generic_is_float(const fy_generic v)
{
	return fy_generic_get_type(v) == FYGT_FLOAT;
}

static inline bool fy_generic_is_string(const fy_generic v)
{
	return fy_generic_get_type(v) == FYGT_STRING;
}

static inline bool fy_generic_is_sequence(const fy_generic v)
{
	return fy_generic_get_type(v) == FYGT_SEQUENCE;
}

static inline bool fy_generic_is_mapping(const fy_generic v)
{
	return fy_generic_get_type(v) == FYGT_MAPPING;
}

static inline bool fy_generic_is_alias(const fy_generic v)
{
	return fy_generic_get_type(v) == FYGT_ALIAS;
}

/* NOTE: All get method multiple-evaluate the argument, so take care */

#define FY_GENERIC_GET_BOOL(_v)							\
	(									\
		(bool)(((_v).v >> FY_BOOL_INPLACE_SHIFT) != 0)			\
	)

static inline bool fy_generic_get_bool_no_check(fy_generic v)
{
	return FY_GENERIC_GET_BOOL(v);
}

static inline bool fy_generic_get_bool_default(fy_generic v, bool default_value)
{
	return fy_generic_is_bool(v) ?
		fy_generic_get_bool_no_check(fy_generic_indirect_get_value(v)) :
		default_value;
}

static inline bool fy_generic_get_bool(fy_generic v)
{
	return fy_generic_get_bool_default(v, false);
}

#define FY_GENERIC_GET_INT(_v)							\
	(									\
		((_v).v & FY_INPLACE_TYPE_MASK) == FY_INT_INPLACE_V ? 		\
			((long long)(((_v).v >> FY_INPLACE_TYPE_SHIFT) << (64 - FYGT_INT_INPLACE_BITS)) >> (64 - FYGT_INT_INPLACE_BITS)) : 		\
			*(long long *)fy_generic_resolve_ptr(_v)		\
	)

static inline long long fy_generic_get_int_no_check(fy_generic v)
{
	return FY_GENERIC_GET_INT(v);
}

static inline long long fy_generic_get_int_default(fy_generic v, int default_value)
{
	return fy_generic_is_int(v) ?
		fy_generic_get_int_no_check(fy_generic_indirect_get_value(v)) :
		default_value;
}

static inline long long fy_generic_get_int(fy_generic v)
{
	return fy_generic_get_int_default(v, 0);
}

#ifdef FY_HAS_64BIT_PTR

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define FY_INPLACE_FLOAT_ADV	1
#else
#define FY_INPLACE_FLOAT_ADV	0
#endif

#define FY_GENERIC_GET_FLOAT(_v)						\
	(									\
		((_v).v & FY_INPLACE_TYPE_MASK) == FY_FLOAT_INPLACE_V ? 	\
			(double)*((float *)&(_v) + FY_INPLACE_FLOAT_ADV) :	\
			*(double *)fy_generic_resolve_ptr(_v)			\
	)

#else

#define FY_GENERIC_GET_FLOAT(_v)						\
	(									\
		((_v).v & FY_INPLACE_TYPE_MASK) == FY_FLOAT_INPLACE_V ? 	\
			(double)*(float *)fy_generic_resolve_ptr(_v) :		\
			*(double *)fy_generic_resolve_ptr(_v)			\
	)
#endif

static inline double fy_generic_get_float_no_check(fy_generic v)
{
	return FY_GENERIC_GET_FLOAT(v);
}

static inline double fy_generic_get_float_default(fy_generic v, double default_value)
{
	return fy_generic_is_float(v) ?
		fy_generic_get_float_no_check(fy_generic_indirect_get_value(v)) :
		default_value;
}

static inline double fy_generic_get_float(fy_generic v)
{
	return fy_generic_get_float_default(v, 0.0);
}

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define FY_INPLACE_STRING_ADV	1
#define FY_INPLACE_STRING_SHIFT	8
#else
#define FY_INPLACE_STRING_ADV	0
#define FY_INPLACE_STRING_SHIFT	0
#endif

/* if we can get the address of the argument, then we can return a pointer to it */

#define FY_GENERIC_GET_INPLACE_STRING(_v)						\
	({										\
		fy_generic *___vp = alloca(sizeof(fy_generic));				\
		*___vp = (_v);								\
		(const uint8_t *)___vp + FY_INPLACE_STRING_ADV;				\
	})

#define FY_GENERIC_GET_STRING_SIZE(_v, _lenp)						\
	((const char *)(								\
		(((_v) & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V) ?		\
			( *(_lenp) = ((_v) >> FY_STRING_INPLACE_SIZE_SHIFT) & 		\
					FYGT_STRING_INPLACE_SIZE_MASK,			\
			  FY_GENERIC_GET_INPLACE_STRING(_v) ) :				\
			fy_decode_size_nocheck(						\
					fy_generic_resolve_ptr(_v), _lenp)		\
	))

#define FY_GENERIC_GET_STRING(_v)							\
	((const char *)(								\
		(((_v) & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V) ?		\
			FY_GENERIC_GET_INPLACE_STRING(_v) :				\
			fy_skip_size_nocheck(fy_generic_resolve_ptr(_v))		\
	))

#define FY_GENERIC_GET_STRING_LVAL(_v)							\
	((const char *)(								\
		(((_v) & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V) ?		\
			(const uint8_t *)(&(_v)) + FY_INPLACE_STRING_ADV :		\
			fy_skip_size_nocheck(fy_generic_resolve_ptr(_v))		\
	))

static inline const char *
fy_generic_get_string_size_no_check(const fy_generic *vp, size_t *lenp)
{
	if (((*vp).v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V) {
		*lenp = ((*vp).v >> FY_STRING_INPLACE_SIZE_SHIFT) & FYGT_STRING_INPLACE_SIZE_MASK;
		return (const char *)vp + FY_INPLACE_STRING_ADV;
	}
	return (const char *)fy_decode_size_nocheck(fy_generic_resolve_ptr(*vp), lenp);
}

static inline const char *
fy_generic_get_string_no_check(const fy_generic *vp)
{
	if (((*vp).v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V)
		return (const char *)vp + FY_INPLACE_STRING_ADV;

	return (const char *)fy_skip_size_nocheck(fy_generic_resolve_ptr(*vp));
}

/* this dance is done because the inplace strings must be alloca'ed */
#define fy_generic_get_string_size(_vp, _lenp)							\
	({											\
		const char *__ret = NULL;							\
		const fy_generic *__vp = (_vp);							\
		size_t *__lenp = (_lenp);							\
		fy_generic __vfinal, *__vpp;							\
												\
		if (!__vp || !fy_generic_is_string(*__vp)) {					\
			*__lenp = 0;								\
		} else {									\
			if (fy_generic_is_indirect(*__vp)) {					\
				__vfinal = fy_generic_indirect_get_value(*__vp);		\
				if ((__vfinal.v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V) {	\
					__vpp = alloca(sizeof(*__vpp));				\
					*__vpp = __vfinal;					\
					__vp = __vpp;						\
				} else								\
					__vp = &__vfinal;					\
			}									\
			__ret = fy_generic_get_string_size_no_check(__vp, __lenp);		\
		}										\
		__ret;										\
	})

#define fy_generic_get_string_default(_vp, _default_v)			\
	({								\
		const char *__ret = (_default_v); 			\
		size_t __len;						\
		__ret = fy_generic_get_string_size((_vp), &__len);	\
		if (!__ret)						\
			__ret = (_default_v);				\
		__ret;							\
	})

#define fy_generic_get_string(_vp) (fy_generic_get_string_default((_vp), NULL))

#define fy_generic_get_alias(_vp) \
	({ \
		fy_generic __va = fy_generic_get_anchor(*(_vp)); \
		fy_generic_get_string(&__va); \
	})

#define fy_bool(_v)			((bool)(_v) ? fy_true : fy_false)

#define fy_int_alloca(_v) 									\
	({											\
		long long __v = (_v);								\
		long long *__vp;								\
		fy_generic_value _r;								\
												\
		if (__v >= FYGT_INT_INPLACE_MIN && __v <= FYGT_INT_INPLACE_MAX)			\
			_r = (__v << FY_INT_INPLACE_SHIFT) | FY_INT_INPLACE_V;			\
		else {										\
			__vp = alloca(sizeof(*__vp));						\
			assert(((uintptr_t)__vp & FY_INPLACE_TYPE_MASK) == 0);			\
			*__vp = __v;								\
			_r = (fy_generic_value)__vp | FY_INT_OUTPLACE_V;			\
		}										\
		_r;										\
	})

/* note the builtin_constant_p guard; this makes the fy_int() macro work */
#define fy_int_const(_v)									\
	({											\
		fy_generic_value _r;								\
												\
		if ((_v) >= FYGT_INT_INPLACE_MIN && (_v) <= FYGT_INT_INPLACE_MAX)		\
			_r = (fy_generic_value)(((_v) << FY_INT_INPLACE_SHIFT) | FY_INT_INPLACE_V);	\
		else {										\
			static const long long _vv FY_INT_ALIGNMENT =				\
				__builtin_constant_p(_v) ? (_v) : 0;				\
			assert(((uintptr_t)&_vv & FY_INPLACE_TYPE_MASK) == 0);			\
			_r = (fy_generic_value)&_vv | FY_INT_OUTPLACE_V;			\
		}										\
		_r;										\
	})

#define fy_int(_v) \
	((fy_generic){ .v = (__builtin_constant_p(_v) ? fy_int_const(_v) : fy_int_alloca(_v)) })

static inline fy_generic_value fy_generic_in_place_null(void *p)
{
	return p == NULL ? fy_null_value : fy_invalid_value;
}

static inline fy_generic_value fy_generic_in_place_bool(_Bool v)
{
	return v ? fy_true_value : fy_false_value;
}

static inline fy_generic_value fy_generic_in_place_int(const long long v)
{
	if (v >= FYGT_INT_INPLACE_MIN && v <= FYGT_INT_INPLACE_MAX)
		return (v << FY_INT_INPLACE_SHIFT) | FY_INT_INPLACE_V;
	return fy_invalid_value;
}

static inline fy_generic_value fy_generic_in_place_uint(const unsigned long long v)
{
	if (v <= FYGT_INT_INPLACE_MAX)
		return (v << FY_INT_INPLACE_SHIFT) | FY_INT_INPLACE_V;
	return fy_invalid_value;
}

static inline fy_generic_value fy_generic_in_place_char_ptr_len(const char *p, const size_t len)
{
	fy_generic_value v;

	switch (len) {
	case 0:
		v = (0 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;
		break;
	case 1:
		v = FY_STRING_SHIFT7(p[0], 0, 0, 0, 0, 0, 0) |
		     (1 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;
		break;
	case 2:
		v = FY_STRING_SHIFT7(p[0], p[1], 0, 0, 0, 0, 0) |
		     (2 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;
		break;
#ifdef FY_HAS_64BIT_PTR
	case 3:
		v = FY_STRING_SHIFT7(p[0], p[1], p[2], 0, 0, 0, 0) |
		     (3 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;
		break;
	case 4:
		v = FY_STRING_SHIFT7(p[0], p[1], p[2], p[3], 0, 0, 0) |
		     (4 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;
		break;
	case 5:
		v = FY_STRING_SHIFT7(p[0], p[1], p[2], p[3], p[4], 0, 0) |
		     (5 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;
		break;
	case 6:
		v = FY_STRING_SHIFT7(p[0], p[1], p[2], p[3], p[4], p[5], 0) |
		     (6 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;
		break;
#endif
	default:
		v = fy_invalid_value;
		break;
	}
	return v;
}

static inline fy_generic_value fy_generic_in_place_char_ptr(const char *p)
{
	if (!p)
		return fy_invalid_value;

	return fy_generic_in_place_char_ptr_len(p, strlen(p));
}

static inline fy_generic_value fy_generic_in_place_invalid(...)
{
	return fy_invalid_value;
}

#ifdef FY_HAS_64BIT_PTR

static inline fy_generic_value fy_generic_in_place_float(const float v)
{
	const union { float f; uint32_t f_bits; } u = { .f = v };
	return ((fy_generic_value)u.f_bits << FY_FLOAT_INPLACE_SHIFT) | FY_FLOAT_INPLACE_V;
}

static inline fy_generic_value fy_generic_in_place_double(const double v)
{
	if (!isnormal(v) || (float)v == v)
		return fy_generic_in_place_float((float)v);
	return fy_invalid_value;
}

#else

static inline fy_generic_value fy_generic_in_place_float(const float v)
{
	return fy_invalid_value;
}

static inline fy_generic_value fy_generic_in_place_double(const double v)
{
	return fy_invalid_value;
}

#endif

// pass-through
static inline fy_generic_value fy_generic_in_place_generic(fy_generic v)
{
	return v.v;
}

#define fy_to_generic_inplace(_v) \
	( _Generic((_v), \
		fy_generic: fy_generic_in_place_generic, \
		void *: fy_generic_in_place_null, \
		_Bool: fy_generic_in_place_bool, \
		signed char: fy_generic_in_place_int, \
		signed short: fy_generic_in_place_int, \
		signed int: fy_generic_in_place_int, \
		signed long: fy_generic_in_place_int, \
		signed long long: fy_generic_in_place_int, \
		unsigned char: fy_generic_in_place_uint, \
		unsigned short: fy_generic_in_place_uint, \
		unsigned int: fy_generic_in_place_uint, \
		unsigned long: fy_generic_in_place_uint, \
		unsigned long long: fy_generic_in_place_uint, \
		char *: fy_generic_in_place_char_ptr, \
		const char *: fy_generic_in_place_char_ptr, \
		float: fy_generic_in_place_float, \
		double: fy_generic_in_place_double, \
		default: fy_generic_in_place_invalid \
	      )(_v))

static inline size_t fy_generic_out_of_place_size_int(const long long v)
{
	return sizeof(long long);
}

static inline size_t fy_generic_out_of_place_size_uint(const unsigned long long v)
{
	return sizeof(unsigned long long);
}

static inline size_t fy_generic_out_of_place_size_char_ptr(const char *p)
{
	if (!p)
		return 0;

	return FYGT_SIZE_ENCODING_MAX + strlen(p) + 1;
}

#ifdef FY_HAS_64BIT_PTR

static inline size_t fy_generic_out_of_place_size_float(const float v)
{
	return 0;	// should never happen
}

static inline size_t fy_generic_out_of_place_size_double(const double v)
{
	return sizeof(double);
}

#else

static inline size_t fy_generic_out_of_place_size_float(const float v)
{
	return sizeof(double);
}

static inline size_t fy_generic_out_of_place_size_double(const double v)
{
	return sizeof(double);
}

#endif

static inline size_t fy_generic_out_of_place_size_generic(...)
{
	/* should never have to do that */
	return 0;
}

static inline size_t fy_generic_out_of_place_size_zero(...)
{
	return 0;
}

#define fy_to_generic_outofplace_size(_v) \
	(_Generic((_v), \
		signed char: fy_generic_out_of_place_size_int, \
		signed short: fy_generic_out_of_place_size_int, \
		signed int: fy_generic_out_of_place_size_int, \
		signed long: fy_generic_out_of_place_size_int, \
		signed long long: fy_generic_out_of_place_size_int, \
		unsigned char: fy_generic_out_of_place_size_uint, \
		unsigned short: fy_generic_out_of_place_size_uint, \
		unsigned int: fy_generic_out_of_place_size_uint, \
		unsigned long: fy_generic_out_of_place_size_uint, \
		unsigned long long: fy_generic_out_of_place_size_uint, \
		char *: fy_generic_out_of_place_size_char_ptr, \
		const char *: fy_generic_out_of_place_size_char_ptr, \
		float: fy_generic_out_of_place_size_float, \
		double: fy_generic_out_of_place_size_double, \
		fy_generic: fy_generic_out_of_place_size_generic, \
		default: fy_generic_out_of_place_size_zero \
	      )(_v))

static inline fy_generic_value fy_generic_out_of_place_put_int(void *buf, const long long v)
{
	assert(((uintptr_t)buf & FY_INPLACE_TYPE_MASK) == 0);
	*(long long *)buf = v;
	return (fy_generic_value)buf | FY_INT_OUTPLACE_V;
}

static inline fy_generic_value fy_generic_out_of_place_put_uint(void *buf, const unsigned long long v)
{
	assert(((uintptr_t)buf & FY_INPLACE_TYPE_MASK) == 0);
	*(unsigned long long *)buf = v;
	return (fy_generic_value)buf | FY_INT_OUTPLACE_V;
}

static inline fy_generic_value fy_generic_out_of_place_put_char_ptr(void *buf, const char *p)
{
	uint8_t *s;
	size_t len;

	if (!p)
		return fy_invalid_value;

	assert(((uintptr_t)buf & FY_INPLACE_TYPE_MASK) == 0);
	len = strlen(p);

	s = fy_encode_size(buf, FYGT_SIZE_ENCODING_MAX, len);
	memcpy(s, p, len);
	s[len] = '\0';
	return (fy_generic_value)buf | FY_STRING_OUTPLACE_V;
}

#ifdef FY_HAS_64BIT_PTR

static inline fy_generic_value fy_generic_out_of_place_put_float(void *buf, const float v)
{
	return fy_invalid_value;	// should never happen
}

static inline fy_generic_value fy_generic_out_of_place_put_double(void *buf, const double v)
{
	assert(((uintptr_t)buf & FY_INPLACE_TYPE_MASK) == 0);
	*(double *)buf = v;
	return (fy_generic_value)buf | FY_FLOAT_OUTPLACE_V;
}

#else

static inline fy_generic_value fy_generic_out_of_place_put_float(void *buf, const float v)
{
	assert(((uintptr_t)buf & FY_INPLACE_TYPE_MASK) == 0);
	*(double *)buf = v;
	return (fy_generic_value)buf | FY_FLOAT_OUTPLACE_V;
}

static inline fy_generic_value fy_generic_out_of_place_put_double(void *buf, const double v);
{
	assert(((uintptr_t)buf & FY_INPLACE_TYPE_MASK) == 0);
	*(double *)buf = v;
	return (fy_generic_value)buf | FY_FLOAT_OUTPLACE_V;
}

#endif

static inline fy_generic_value fy_generic_out_of_place_put_generic(void *buf, ...)
{
	/* we just pass-through, should never happen */
	return fy_invalid_value;
}

static inline fy_generic_value fy_generic_out_of_place_put_invalid(void *buf, ...)
{
	return fy_invalid_value;
}

#define fy_to_generic_outofplace_put(_vp, _v) \
	(_Generic((_v), \
		signed char: fy_generic_out_of_place_put_int, \
		signed short: fy_generic_out_of_place_put_int, \
		signed int: fy_generic_out_of_place_put_int, \
		signed long: fy_generic_out_of_place_put_int, \
		signed long long: fy_generic_out_of_place_put_int, \
		unsigned char: fy_generic_out_of_place_put_uint, \
		unsigned short: fy_generic_out_of_place_put_uint, \
		unsigned int: fy_generic_out_of_place_put_uint, \
		unsigned long: fy_generic_out_of_place_put_uint, \
		unsigned long long: fy_generic_out_of_place_put_uint, \
		char *: fy_generic_out_of_place_put_char_ptr, \
		const char *: fy_generic_out_of_place_put_char_ptr, \
		float: fy_generic_out_of_place_put_float, \
		double: fy_generic_out_of_place_put_double, \
		fy_generic: fy_generic_out_of_place_put_generic, \
		default: fy_generic_out_of_place_put_invalid \
	      )((_vp), (_v)))

#define fy_to_generic_value(_v) \
	({	\
		typeof (_v) __v = (_v); \
		fy_generic_value __r; \
		\
		__r = fy_to_generic_inplace(__v); \
		if (__r == fy_invalid_value) { \
			size_t __outofplace_size = fy_to_generic_outofplace_size(__v); \
			fy_generic *__vp = __outofplace_size ? alloca(__outofplace_size) : NULL; \
			__r = fy_to_generic_outofplace_put(__vp, __v); \
		} \
		__r; \
	})

#define fy_to_generic(_v) \
	((fy_generic) { .v = fy_to_generic_value(_v) })

#ifdef FY_HAS_64BIT_PTR

#define fy_float_alloca(_v) 									\
	({											\
		double __v = (_v);								\
		double *__vp;									\
		fy_generic_value _r;								\
												\
		if (!isnormal(__v) || (float)__v == __v) {					\
			const union { float f; uint32_t f_bits; } __u = { .f = (float)__v };	\
			_r = ((fy_generic_value)__u.f_bits << FY_FLOAT_INPLACE_SHIFT) | 	\
				FY_FLOAT_INPLACE_V;						\
		} else {									\
			__vp = alloca(sizeof(*__vp));						\
			assert(((uintptr_t)__vp & FY_INPLACE_TYPE_MASK) == 0);			\
			*__vp = __v;								\
			_r = (fy_generic_value)__vp | FY_FLOAT_OUTPLACE_V;			\
		}										\
		_r;										\
	})

/* note the builtin_constant_p guard; this makes the fy_float() macro work */
#define fy_float_const(_v)									\
	({											\
		fy_generic_value _r;								\
												\
		if (!isnormal(_v) || (float)(_v) == (double)(_v)) {				\
			const union { float f; uint32_t f_bits; } __u =				\
				{ .f = __builtin_constant_p(_v) ? (float)(_v) : 0.0 };		\
			_r = ((fy_generic_value)__u.f_bits  << FY_FLOAT_INPLACE_SHIFT) |	\
				FY_FLOAT_INPLACE_V;						\
		} else {									\
			static const double _vv FY_FLOAT_ALIGNMENT = 				\
				__builtin_constant_p(_v) ? (_v) : 0;				\
			assert(((uintptr_t)&_vv & FY_INPLACE_TYPE_MASK) == 0);			\
			_r = (fy_generic_value)&_vv | FY_FLOAT_OUTPLACE_V;			\
		}										\
		_r;										\
	})

#else

#define fy_float_alloca(_v) 						\
	({								\
		double __v = (_v);					\
		double *__vp;						\
		fy_generic_value _r;					\
									\
		__vp = alloca(sizeof(*__vp));				\
		assert(((uintptr_t)__vp & FY_INPLACE_TYPE_MASK) == 0);	\
		*__vp = __v;						\
		_r = (fy_generic_value)__vp | FY_FLOAT_OUTPLACE_V;	\
		_r;							\
	})

/* note the builtin_constant_p guard; this makes the fy_float() macro work */
#define fy_float_const(_v)						\
	({								\
		fy_generic_value _r;					\
									\
		static const double _vv FY_FLOAT_ALIGNMENT = 		\
			__builtin_constant_p(_v) ? (_v) : 0;		\
		assert(((uintptr_t)&_vv & FY_INPLACE_TYPE_MASK) == 0);	\
		_r = (fy_generic_value)&_vv | FY_FLOAT_OUTPLACE_V;	\
		_r;							\
	})

#endif

#define fy_float(_v) \
	((fy_generic){ .v = __builtin_constant_p(_v) ? fy_float_const(_v) : fy_float_alloca(_v) })

int fy_generic_compare_out_of_place(fy_generic a, fy_generic b);

static inline int fy_generic_compare(fy_generic a, fy_generic b)
{
	/* invalids are always non-matching */
	if (a.v == fy_invalid_value || b.v == fy_invalid_value)
		return -1;

	/* equals? nice - should work for null, bool, in place int, float and strings  */
	/* also for anything that's a pointer */
	if (a.v == b.v)
		return 0;

	/* invalid types, or differing types do not match */
	if (fy_generic_get_type(a) != fy_generic_get_type(b))
		return -1;

	return fy_generic_compare_out_of_place(a, b);
}

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

#define fy_string_size_alloca(_v, _len) 							\
	({											\
		const char *__v = (_v);								\
		size_t __len = (_len);								\
		uint8_t *__vp, *__s;								\
		fy_generic_value _r;								\
												\
		_r = fy_generic_in_place_char_ptr_len(__v, __len);				\
		if (_r == fy_invalid_value) {							\
			__vp = alloca(FYGT_SIZE_ENCODING_MAX + __len + 1); 			\
			assert(((uintptr_t)__vp & FY_INPLACE_TYPE_MASK) == 0);			\
			__s = fy_encode_size(__vp, FYGT_SIZE_ENCODING_MAX, __len);		\
			memcpy(__s, __v, __len);						\
			__s[__len] = '\0';							\
			_r = (fy_generic_value)__vp | FY_STRING_OUTPLACE_V;			\
		}										\
		_r;										\
	})

#ifdef FY_HAS_64BIT_PTR

#define fy_string_size_const(_v, _len) 								\
	({											\
		fy_generic_value _r;								\
												\
		_r = fy_generic_in_place_char_ptr_len((_v), (_len));				\
		if (_r == fy_invalid_value) {							\
			if ((_len) < ((uint64_t)1 <<  7)) {					\
				static const struct {						\
					uint8_t l0;						\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? (uint8_t)(_len) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic_value)&_s | FY_STRING_OUTPLACE_V;		\
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
				_r = (fy_generic_value)&_s | FY_STRING_OUTPLACE_V;		\
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
				_r = (fy_generic_value)&_s | FY_STRING_OUTPLACE_V;		\
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
				_r = (fy_generic_value)&_s | FY_STRING_OUTPLACE_V;		\
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
				_r = (fy_generic_value)&_s | FY_STRING_OUTPLACE_V;		\
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
				_r = (fy_generic_value)&_s | FY_STRING_OUTPLACE_V;		\
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
				_r = (fy_generic_value)&_s | FY_STRING_OUTPLACE_V;		\
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
				_r = (fy_generic_value)&_s | FY_STRING_OUTPLACE_V;		\
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
				_r = (fy_generic_value)&_s | FY_STRING_OUTPLACE_V;		\
			}									\
		}										\
		_r;										\
	})

#else

#define fy_string_size_const(_v, _len) 								\
	({											\
		fy_generic_value _r;								\
												\
		_r = fy_generic_in_place_char_ptr_len((_v), (_len));				\
		_r = fy_generic_in_place_char_ptr_len((_v), (_len));				\
		if (_r == fy_invalid_value) {							\
			if ((_len) < ((uint64_t)1 <<  7)) {					\
				static const struct {						\
					uint8_t l0;						\
					char s[];						\
				} _s FY_STRING_ALIGNMENT = {					\
					__builtin_constant_p(_len) ? (uint8_t)(_len) : 0,	\
					{ FY_CONST_P_INIT(_v) }					\
				};								\
				assert(((uintptr_t)&_s & FY_INPLACE_TYPE_MASK) == 0);		\
				_r = (fy_generic_value)&_s | FY_STRING_OUTPLACE_V;		\
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
				_r = (fy_generic_value)&_s | FY_STRING_OUTPLACE_V;		\
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
				_r = (fy_generic_value)&_s | FY_STRING_OUTPLACE_V;		\
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
				_r = (fy_generic_value)&_s | FY_STRING_OUTPLACE_V;		\
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
				_r = (fy_generic_value)&_s | FY_STRING_OUTPLACE_V;		\
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

#define fy_string_size(_v, _len) \
	((fy_generic){ .v = __builtin_constant_p(_v) ? fy_string_size_const((_v), (_len)) : fy_string_size_alloca((_v), (_len)) })

#define fy_string(_v) \
	((fy_generic){ .v = __builtin_constant_p(_v) ? fy_string_const(_v) : fy_string_alloca(_v) })

#define fy_stringf(_fmt, ...) \
	((fy_generic){ \
		.v = ({ \
			const char *__fmt = (_fmt); \
			int _size; \
			int _sizew __FY_DEBUG_UNUSED__; \
			char *_buf = NULL, *_s; \
			\
			_size = snprintf(NULL, 0, __fmt, ## __VA_ARGS__); \
			if (_size != -1) { \
				_buf = alloca(_size + 1); \
				_sizew = snprintf(_buf, _size + 1, __fmt, __VA_ARGS__); \
				assert(_size == _sizew); \
				_s = _buf + strlen(_buf); \
				while (_s > _buf && _s[-1] == '\n') \
					*--_s = '\0'; \
			} \
			fy_string_alloca(_buf); \
		}) \
	})

#define fy_sequence_alloca(_count, _items) 						\
	({										\
		struct fy_generic_sequence *__vp;					\
		size_t __count = (_count);						\
		size_t __size = sizeof(*__vp) + __count * sizeof(fy_generic);		\
											\
		__vp = fy_ptr_align(alloca(__size + FY_GENERIC_CONTAINER_ALIGN - 1),	\
				FY_GENERIC_CONTAINER_ALIGN);				\
		__vp->count = (_count);							\
		memcpy(__vp->items, (_items), __count * sizeof(fy_generic)); 		\
		(fy_generic_value)((uintptr_t)__vp | FY_SEQ_V);				\
	})

#define fy_sequence_explicit(_count, _items) fy_sequence_alloca((_count), (_items))

#define fy_sequence_create(_count, _items) \
	((fy_generic) { .v = fy_sequence_explicit((_count), (_items)) })

#define _FY_CPP_GITEM_ONE(arg) fy_to_generic(arg)
#define _FY_CPP_GITEM_LATER_ARG(arg) , _FY_CPP_GITEM_ONE(arg)
#define _FY_CPP_GITEM_LIST(...) FY_CPP_MAP(_FY_CPP_GITEM_LATER_ARG, __VA_ARGS__)

#define _FY_CPP_VA_GITEMS(...)          \
    _FY_CPP_GITEM_ONE(FY_CPP_FIRST(__VA_ARGS__)) \
    _FY_CPP_GITEM_LIST(FY_CPP_REST(__VA_ARGS__))

#define FY_CPP_VA_GITEMS(_count, ...) \
	((fy_generic [(_count)]) { _FY_CPP_VA_GITEMS(__VA_ARGS__) })

#define fy_sequence(...) \
	((fy_generic) { \
		.v = fy_sequence_explicit(FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)) })

static inline const fy_generic *fy_generic_sequence_get_items(fy_generic seq, size_t *countp)
{
	struct fy_generic_sequence *p;

	if (!countp)
	       return NULL;

	if (!fy_generic_is_sequence(seq)) {
		*countp = 0;
		return NULL;
	}

	if (fy_generic_is_indirect(seq))
		seq = fy_generic_indirect_get_value(seq);

	p = fy_generic_resolve_collection_ptr(seq);

	*countp = p->count;
	return &p->items[0];
}

static inline fy_generic fy_generic_sequence_get_item(fy_generic seq, size_t idx)
{
	const fy_generic *items;
	size_t count;

	if (fy_generic_get_type(seq) != FYGT_SEQUENCE)
		return fy_invalid;

	items = fy_generic_sequence_get_items(seq, &count);
	if (!items || idx >= count)
		return fy_invalid;
	return items[idx];
}

static inline size_t fy_generic_sequence_get_item_count(fy_generic seq)
{
	struct fy_generic_sequence *p;

	if (fy_generic_get_type(seq) != FYGT_SEQUENCE)
		return (size_t)-1;

	if (fy_generic_is_indirect(seq))
		seq = fy_generic_indirect_get_value(seq);

	p = fy_generic_resolve_collection_ptr(seq);
	return p->count;
}

static inline fy_generic fy_generic_sequence_get_null_item(fy_generic seq, size_t idx)
{
	fy_generic v = fy_generic_sequence_get_item(seq, idx);
	return fy_generic_is_null(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_sequence_get_bool_item(fy_generic seq, size_t idx)
{
	fy_generic v = fy_generic_sequence_get_item(seq, idx);
	return fy_generic_is_bool(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_sequence_get_int_item(fy_generic seq, size_t idx)
{
	fy_generic v = fy_generic_sequence_get_item(seq, idx);
	return fy_generic_is_int(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_sequence_get_float_item(fy_generic seq, size_t idx)
{
	fy_generic v = fy_generic_sequence_get_item(seq, idx);
	return fy_generic_is_float(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_sequence_get_string_item(fy_generic seq, size_t idx)
{
	fy_generic v = fy_generic_sequence_get_item(seq, idx);
	return fy_generic_is_string(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_sequence_get_sequence_item(fy_generic seq, size_t idx)
{
	fy_generic v = fy_generic_sequence_get_item(seq, idx);
	return fy_generic_is_sequence(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_sequence_get_mapping_item(fy_generic seq, size_t idx)
{
	fy_generic v = fy_generic_sequence_get_item(seq, idx);
	return fy_generic_is_mapping(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_sequence_get_alias_item(fy_generic seq, size_t idx)
{
	fy_generic v = fy_generic_sequence_get_item(seq, idx);
	return fy_generic_is_alias(v) ? v : fy_invalid;
}

#define fy_mapping_alloca(_count, _pairs) 						\
	({										\
		struct fy_generic_mapping *__vp;					\
		size_t __count = (_count);						\
		size_t __size = sizeof(*__vp) + 2 * __count * sizeof(fy_generic);	\
											\
		__vp = fy_ptr_align(alloca(__size + FY_GENERIC_CONTAINER_ALIGN - 1), 	\
				FY_GENERIC_CONTAINER_ALIGN);				\
		__vp->count = (_count);							\
		memcpy(__vp->pairs, (_pairs), 2 * __count * sizeof(fy_generic)); 	\
		(fy_generic){ .v = (uintptr_t)__vp | FY_MAP_V };			\
	})

#define fy_mapping_explicit(_count, _pairs) fy_mapping_alloca((_count), (_pairs))

#define fy_mapping_create(_count, _items) \
	((fy_generic) { .v = fy_mapping_explicit((_count), (_items)) })

#define fy_mapping(...) \
	fy_mapping_explicit(FY_CPP_VA_COUNT(__VA_ARGS__) / 2, \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__))

static inline const fy_generic *fy_generic_mapping_get_pairs(fy_generic map, size_t *countp)
{
	struct fy_generic_mapping *p;

	if (!countp)
		return NULL;

	if (!fy_generic_is_mapping(map)) {
		*countp = 0;
		return NULL;
	}

	if (fy_generic_is_indirect(map))
		map = fy_generic_indirect_get_value(map);

	assert(fy_generic_get_type(map) == FYGT_MAPPING);

	p = fy_generic_resolve_collection_ptr(map);

	*countp = p->count;
	return p->pairs;
}

static inline size_t fy_generic_mapping_get_pair_count(fy_generic map)
{
	struct fy_generic_mapping *p;

	if (fy_generic_get_type(map) != FYGT_MAPPING)
		return (size_t)-1;

	if (fy_generic_is_indirect(map))
		map = fy_generic_indirect_get_value(map);

	p = fy_generic_resolve_collection_ptr(map);
	return p->count;
}

static inline const fy_generic fy_generic_mapping_get_value_index(fy_generic map, fy_generic key, size_t *idxp)
{
	struct fy_generic_mapping *p;
	const fy_generic *pair;
	size_t i;

	if (fy_generic_is_indirect(map))
		map = fy_generic_indirect_get_value(map);
	p = fy_generic_resolve_collection_ptr(map);
	pair = p->pairs;
	for (i = 0; i < p->count; i++, pair += 2) {
		if (fy_generic_compare(key, pair[0]) == 0) {
			if (idxp)
				*idxp = i;
			return pair[1];
		}
	}
	if (idxp)
		*idxp = (size_t)-1;
	return fy_invalid;
}

static inline const fy_generic fy_generic_mapping_get_value(fy_generic map, fy_generic key)
{
	return fy_generic_mapping_get_value_index(map, key, NULL);
}

static inline fy_generic fy_generic_mapping_get_null_value(fy_generic map, fy_generic key)
{
	fy_generic v = fy_generic_mapping_get_value(map, key);
	return fy_generic_is_null(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_mapping_get_bool_value(fy_generic map, fy_generic key)
{
	fy_generic v = fy_generic_mapping_get_value(map, key);
	return fy_generic_is_bool(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_mapping_get_int_value(fy_generic map, fy_generic key)
{
	fy_generic v = fy_generic_mapping_get_value(map, key);
	return fy_generic_is_int(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_mapping_get_float_value(fy_generic map, fy_generic key)
{
	fy_generic v = fy_generic_mapping_get_value(map, key);
	return fy_generic_is_float(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_mapping_get_string_value(fy_generic map, fy_generic key)
{
	fy_generic v = fy_generic_mapping_get_value(map, key);
	return fy_generic_is_string(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_mapping_get_sequence_value(fy_generic map, fy_generic key)
{
	fy_generic v = fy_generic_mapping_get_value(map, key);
	return fy_generic_is_sequence(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_mapping_get_mapping_value(fy_generic map, fy_generic key)
{
	fy_generic v = fy_generic_mapping_get_value(map, key);
	return fy_generic_is_mapping(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_mapping_get_alias_value(fy_generic map, fy_generic key)
{
	fy_generic v = fy_generic_mapping_get_value(map, key);
	return fy_generic_is_alias(v) ? v : fy_invalid;
}

#define fy_indirect_alloca(_v, _anchor, _tag)						\
	({										\
		fy_generic_value __v = (_v);						\
		fy_generic_value __anchor = (_anchor), __tag = (_tag), *__vp;	\
		int __i = 1;								\
											\
		__vp = fy_ptr_align(alloca(4 * sizeof(*__vp) + FY_GENERIC_CONTAINER_ALIGN - 1),	\
				FY_GENERIC_CONTAINER_ALIGN);				\
		__vp[0] = 0;								\
		if (__v != fy_invalid_value) {						\
			 __vp[0] |= FYGIF_VALUE;					\
			 __vp[__i++] = __v;						\
		}									\
		if (__anchor != fy_invalid_value) {					\
			 __vp[0] |= FYGIF_ANCHOR;					\
			 __vp[__i++] = __anchor;					\
		}									\
		if (_tag != fy_invalid_value) {						\
			 __vp[0] |= FYGIF_TAG;						\
			 __vp[__i++] = __tag;						\
		}									\
		(fy_generic_value)__vp | FY_INDIRECT_V;					\
	})

#define fy_indirect(_v, _anchor, _tag)	\
	((fy_generic){ .v = fy_indirect_alloca((_v).v, (_anchor).v, (_tag).v) })

static inline bool fy_generic_is_in_place(fy_generic v)
{
	if (v.v == fy_invalid_value)
		return true;

	if (fy_generic_is_indirect(v))
		return false;

	switch (fy_generic_get_type(v)) {
	case FYGT_NULL:
	case FYGT_BOOL:
		return true;

	case FYGT_INT:
		if ((v.v & FY_INPLACE_TYPE_MASK) == FY_INT_INPLACE_V)
			return true;
		break;

	case FYGT_FLOAT:
		if ((v.v & FY_INPLACE_TYPE_MASK) == FY_FLOAT_INPLACE_V)
			return true;
		break;

	case FYGT_STRING:
		if ((v.v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V)
			return true;
		break;

	default:
		break;
	}
	return false;
}

enum fy_generic_schema {
	FYGS_AUTO,
	FYGS_YAML1_2_FAILSAFE,
	FYGS_YAML1_2_CORE,
	FYGS_YAML1_2_JSON,
	FYGS_YAML1_1,
	FYGS_JSON,
};
#define FYGS_COUNT (FYGS_JSON + 1)

static inline bool fy_generic_schema_is_json(enum fy_generic_schema schema)
{
	return schema == FYGS_YAML1_2_JSON || schema == FYGS_JSON;
}

const char *fy_generic_schema_get_text(enum fy_generic_schema schema);

/* Shift amount of the schema mode option */
#define FYGBCF_SCHEMA_SHIFT		0
/* Mask of the schema mode */
#define FYGBCF_SCHEMA_MASK		((1U << 4) - 1)
/* Build a schema mode option */
#define FYGBCF_SCHEMA(x)		(((unsigned int)(x) & FYGBCF_SCHEMA_MASK) << FYGBCF_SCHEMA_SHIFT)

/**
 * enum fy_gb_cfg_flags - Generic builder configuration flags
 *
 * These flags control the state of of the generic builder
 *
 * @FYGBCF_OWNS_ALLOCATOR: The builder owns the allocator, will destroy
 */
enum fy_gb_cfg_flags {
	FYGBCF_SCHEMA_AUTO		= FYGBCF_SCHEMA(FYGS_AUTO),
	FYGBCF_SCHEMA_YAML1_2_FAILSAFE	= FYGBCF_SCHEMA(FYGS_YAML1_2_FAILSAFE),
	FYGBCF_SCHEMA_YAML1_2_CORE	= FYGBCF_SCHEMA(FYGS_YAML1_2_CORE),
	FYGBCF_SCHEMA_YAML1_2_JSON	= FYGBCF_SCHEMA(FYGS_YAML1_2_JSON),
	FYGBCF_SCHEMA_YAML1_1		= FYGBCF_SCHEMA(FYGS_YAML1_1),
	FYGBCF_SCHEMA_JSON		= FYGBCF_SCHEMA(FYGS_JSON),
	FYGBCF_OWNS_ALLOCATOR		= FY_BIT(4),
};

struct fy_generic_builder_cfg {
	enum fy_gb_cfg_flags flags;
	struct fy_allocator *allocator;
	int shared_tag;
	struct fy_diag *diag;
};

struct fy_generic_builder;

struct fy_generic_builder *fy_generic_builder_create(const struct fy_generic_builder_cfg *cfg);
void fy_generic_builder_destroy(struct fy_generic_builder *gb);
void fy_generic_builder_reset(struct fy_generic_builder *gb);

static inline fy_generic fy_gb_null_create(struct fy_generic_builder *gb, void *p)
{
	return fy_null;
}

static inline fy_generic fy_gb_bool_create(struct fy_generic_builder *gb, bool state)
{
	return state ? fy_true : fy_false;
}

fy_generic fy_gb_int_create_out_of_place(struct fy_generic_builder *gb, long long val);

static inline fy_generic
fy_gb_int_create(struct fy_generic_builder *gb, long long val)
{
	fy_generic v = { .v = fy_generic_in_place_int(val) };
	if (v.v != fy_invalid_value)
		return v;
	return fy_gb_int_create_out_of_place(gb, val);
}

fy_generic fy_gb_float_create_out_of_place(struct fy_generic_builder *gb, float val);

static inline fy_generic
fy_gb_float_create(struct fy_generic_builder *gb, float val)
{
	fy_generic v = { .v = fy_generic_in_place_float(val) };
	if (v.v != fy_invalid_value)
		return v;
	return fy_gb_float_create_out_of_place(gb, val);
}

fy_generic fy_gb_double_create_out_of_place(struct fy_generic_builder *gb, double val);

static inline fy_generic
fy_gb_double_create(struct fy_generic_builder *gb, double val)
{
	fy_generic v = { .v = fy_generic_in_place_double(val) };
	if (v.v != fy_invalid_value)
		return v;
	return fy_gb_double_create_out_of_place(gb, val);
}

fy_generic fy_gb_string_size_create_out_of_place(struct fy_generic_builder *gb, const char *str, size_t len);

static inline fy_generic fy_gb_string_create_out_of_place(struct fy_generic_builder *gb, const char *str)
{
	return fy_gb_string_size_create_out_of_place(gb, str, str ? strlen(str) : 0);
}

static inline fy_generic
fy_gb_string_size_create(struct fy_generic_builder *gb, const char *str, size_t len)
{
	fy_generic v = { .v = fy_generic_in_place_char_ptr_len(str, len) };
	if (v.v != fy_invalid_value)
		return v;
	return fy_gb_string_size_create_out_of_place(gb, str, len);
}

static inline fy_generic fy_gb_string_create(struct fy_generic_builder *gb, const char *str)
{
	return fy_gb_string_size_create(gb, str, strlen(str));
}

static inline fy_generic fy_gb_invalid_create_out_of_place(struct fy_generic_builder *gb, ...)
{
	return fy_invalid;
}

#define fy_gb_to_generic_outofplace_put(_gb, _v) \
	(_Generic((_v), \
		void *: fy_gb_null_create, \
		_Bool: fy_gb_bool_create, \
		signed char: fy_gb_int_create_out_of_place, \
		signed short: fy_gb_int_create_out_of_place, \
		signed int: fy_gb_int_create_out_of_place, \
		signed long: fy_gb_int_create_out_of_place, \
		signed long long: fy_gb_int_create_out_of_place, \
		unsigned char: fy_gb_int_create_out_of_place, \
		unsigned short: fy_gb_int_create_out_of_place, \
		unsigned int: fy_gb_int_create_out_of_place, \
		unsigned long: fy_gb_int_create_out_of_place, \
		unsigned long long: fy_gb_int_create_out_of_place, \
		char *: fy_gb_string_create_out_of_place, \
		const char *: fy_gb_string_create_out_of_place, \
		float: fy_gb_float_create_out_of_place, \
		double: fy_gb_double_create_out_of_place, \
		fy_generic: fy_gb_internalize_out_of_place, \
		default: fy_gb_invalid_create_out_of_place \
	      )((_gb), (_v)))

#define fy_gb_to_generic_value(_gb, _v) \
	({	\
		typeof (_v) __v = (_v); \
		fy_generic_value __r; \
		\
		__r = fy_to_generic_inplace(__v); \
		if (__r == fy_invalid_value) \
			__r = fy_gb_to_generic_outofplace_put((_gb), __v).v; \
		__r; \
	})

#define fy_gb_to_generic(_gb, _v) \
	((fy_generic) { .v = fy_gb_to_generic_value((_gb), (_v)) })

fy_generic fy_gb_string_vcreate(struct fy_generic_builder *gb, const char *fmt, va_list ap);
fy_generic fy_gb_string_createf(struct fy_generic_builder *gb, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

fy_generic fy_gb_sequence_create_i(struct fy_generic_builder *gb, bool internalize,
					size_t count, const fy_generic *items);
fy_generic fy_gb_sequence_create(struct fy_generic_builder *gb, size_t count, const fy_generic *items);

fy_generic fy_gb_sequence_remove(struct fy_generic_builder *gb, fy_generic seq, size_t idx, size_t count);

fy_generic fy_gb_sequence_insert_replace_i(struct fy_generic_builder *gb, bool insert, bool internalize,
						fy_generic seq, size_t idx, size_t count, const fy_generic *items);
fy_generic fy_gb_sequence_insert_replace(struct fy_generic_builder *gb, bool insert,
					      fy_generic seq, size_t idx, size_t count, const fy_generic *items);
fy_generic fy_gb_sequence_insert_i(struct fy_generic_builder *gb, bool internalize,
					fy_generic seq, size_t idx, size_t count, const fy_generic *items);
fy_generic fy_gb_sequence_insert(struct fy_generic_builder *gb, fy_generic seq, size_t idx, size_t count,
				      const fy_generic *items);
fy_generic fy_gb_sequence_replace_i(struct fy_generic_builder *gb, bool internalize,
					 fy_generic seq, size_t idx, size_t count, const fy_generic *items);
fy_generic fy_gb_sequence_replace(struct fy_generic_builder *gb, fy_generic seq, size_t idx, size_t count,
				       const fy_generic *items);

fy_generic fy_gb_sequence_append_i(struct fy_generic_builder *gb, bool internalize,
					fy_generic seq, size_t count, const fy_generic *items);
fy_generic fy_gb_sequence_append(struct fy_generic_builder *gb, fy_generic seq, size_t count, const fy_generic *items);

fy_generic fy_gb_mapping_create_i(struct fy_generic_builder *gb, bool internalize,
				       size_t count, const fy_generic *pairs);
fy_generic fy_gb_mapping_create(struct fy_generic_builder *gb, size_t count, const fy_generic *pairs);

fy_generic fy_gb_mapping_remove(struct fy_generic_builder *gb, fy_generic map, size_t idx, size_t count);

fy_generic fy_gb_mapping_insert_replace_i(struct fy_generic_builder *gb, bool insert, bool internalize,
						fy_generic map, size_t idx, size_t count, const fy_generic *pairs);
fy_generic fy_gb_mapping_insert_replace(struct fy_generic_builder *gb, bool insert,
					      fy_generic map, size_t idx, size_t count, const fy_generic *pairs);
fy_generic fy_gb_mapping_insert_i(struct fy_generic_builder *gb, bool internalize,
					fy_generic map, size_t idx, size_t count, const fy_generic *pairs);
fy_generic fy_gb_mapping_insert(struct fy_generic_builder *gb, fy_generic map, size_t idx, size_t count,
				      const fy_generic *pairs);
fy_generic fy_gb_mapping_replace_i(struct fy_generic_builder *gb, bool internalize,
					 fy_generic map, size_t idx, size_t count, const fy_generic *pairs);
fy_generic fy_gb_mapping_replace(struct fy_generic_builder *gb, fy_generic map, size_t idx, size_t count,
				       const fy_generic *pairs);

fy_generic fy_gb_mapping_append_i(struct fy_generic_builder *gb, bool internalize,
					fy_generic map, size_t count, const fy_generic *pairs);
fy_generic fy_gb_mapping_append(struct fy_generic_builder *gb, fy_generic map, size_t count, const fy_generic *pairs);

fy_generic fy_gb_mapping_set_value_i(struct fy_generic_builder *gb, bool internalize,
					  fy_generic map, fy_generic key, fy_generic value);
fy_generic fy_gb_mapping_set_value(struct fy_generic_builder *gb, fy_generic map,
					fy_generic key, fy_generic value);

fy_generic fy_gb_indirect_create(struct fy_generic_builder *gb, const struct fy_generic_indirect *gi);

fy_generic fy_gb_alias_create(struct fy_generic_builder *gb, fy_generic anchor);

#define FY_CPP2_EMPTY(a)
#define FY_CPP2_POSTPONE1(a, macro) macro FY_CPP2_EMPTY(a)

#define FY_CPP2_MAP(a, macro, ...) \
    __VA_OPT__(FY_CPP_EVAL(_FY_CPP2_MAP_ONE(a, macro, __VA_ARGS__)))

#define _FY_CPP2_MAP_ONE(a, macro, x, ...) macro(a, x) \
    __VA_OPT__(FY_CPP2_POSTPONE1(a, _FY_CPP2_MAP_INDIRECT)()(a, macro, __VA_ARGS__))
#define _FY_CPP2_MAP_INDIRECT() _FY_CPP2_MAP_ONE

#define _FY_CPP_GBITEM_ONE(_gb, arg) fy_gb_to_generic(_gb, arg)
#define _FY_CPP_GBITEM_LATER_ARG(_gb, arg) , _FY_CPP_GBITEM_ONE(_gb, arg)
#define _FY_CPP_GBITEM_LIST(_gb, ...) FY_CPP2_MAP(_gb, _FY_CPP_GBITEM_LATER_ARG, __VA_ARGS__)

#define _FY_CPP_VA_GBITEMS(_gb, ...)          \
    _FY_CPP_GBITEM_ONE(_gb, FY_CPP_FIRST(__VA_ARGS__)) \
    _FY_CPP_GBITEM_LIST(_gb, FY_CPP_REST(__VA_ARGS__))

#define FY_CPP_VA_GBITEMS(_count, _gb, ...) \
	((fy_generic [(_count)]) { _FY_CPP_VA_GBITEMS(gb, __VA_ARGS__) })

#define fy_gb_sequence(_gb, ...) \
	((fy_generic) { .v = fy_gb_sequence_create(gb, FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GBITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), gb, __VA_ARGS__)).v })

#define fy_gb_mapping(_gb, ...) \
	((fy_generic) { .v = fy_gb_mapping_create(gb, FY_CPP_VA_COUNT(__VA_ARGS__) / 2, \
			FY_CPP_VA_GBITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), gb, __VA_ARGS__)).v })

fy_generic fy_gb_create_scalar_from_text(struct fy_generic_builder *gb,
					      const char *text, size_t len, enum fy_generic_type force_type);

fy_generic fy_gb_copy_out_of_place(struct fy_generic_builder *gb, fy_generic v);

static inline fy_generic fy_gb_copy(struct fy_generic_builder *gb, fy_generic v)
{
	if (fy_generic_is_in_place(v))
		return v;

	return fy_gb_copy_out_of_place(gb, v);
}

/* internalize the value (i.e. copy it if the pointer is not part of the builder arenas) */

fy_generic fy_gb_internalize_out_of_place(struct fy_generic_builder *gb, fy_generic v);

static inline fy_generic fy_gb_internalize(struct fy_generic_builder *gb, fy_generic v)
{
	/* if it's invalid or in place, just return it */
	if (fy_generic_is_invalid(v) || fy_generic_is_in_place(v))
		return v;

	return fy_gb_internalize_out_of_place(gb, v);
}

fy_generic fy_generic_relocate(void *start, void *end, fy_generic v, ptrdiff_t d);

enum fy_generic_schema fy_gb_get_schema(struct fy_generic_builder *gb);
void fy_gb_set_schema(struct fy_generic_builder *gb, enum fy_generic_schema schema);

int fy_gb_set_schema_from_parser_mode(struct fy_generic_builder *gb, enum fy_parser_mode parser_mode);

//////////////////////////////////////////////////////

struct fy_generic_builder {
	struct fy_generic_builder_cfg cfg;
	enum fy_generic_schema schema;
	struct fy_allocator *allocator;
	bool owns_allocator;
	int shared_tag;
	int alloc_tag;
	void *linear;	/* when making it linear */
};

static inline void *fy_gb_alloc(struct fy_generic_builder *gb, size_t size, size_t align)
{
	return fy_allocator_alloc(gb->allocator, gb->alloc_tag, size, align);
}

static inline void fy_gb_free(struct fy_generic_builder *gb, void *ptr)
{
	fy_allocator_free(gb->allocator, gb->alloc_tag, ptr);
}

static inline void fy_gb_trim(struct fy_generic_builder *gb)
{
	fy_allocator_trim_tag(gb->allocator, gb->alloc_tag);
}

static inline const void *fy_gb_store(struct fy_generic_builder *gb, const void *data, size_t size, size_t align)
{
	return fy_allocator_store(gb->allocator, gb->alloc_tag, data, size, align);
}

static inline const void *fy_gb_storev(struct fy_generic_builder *gb, const struct iovec *iov, unsigned int iovcnt, size_t align)
{
	return fy_allocator_storev(gb->allocator, gb->alloc_tag, iov, iovcnt, align);
}

static inline struct fy_allocator_info *
fy_gb_get_allocator_info(struct fy_generic_builder *gb)
{
	return fy_allocator_get_info(gb->allocator, gb->alloc_tag);
}

static inline void fy_gb_release(struct fy_generic_builder *gb, const void *ptr, size_t size)
{
	fy_allocator_release(gb->allocator, gb->alloc_tag, ptr, size);
}

#endif
