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
#include <limits.h>
#include <stddef.h>
#include <float.h>

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
typedef intptr_t fy_generic_value_signed;

#define FYGT_GENERIC_BITS_64			64
#define FYGT_INT_INPLACE_BITS_64		61
#define FYGT_STRING_INPLACE_SIZE_64		6
#define FYGT_STRING_INPLACE_SIZE_MASK_64	7
#define FYGT_SIZE_ENCODING_MAX_64		FYVL_SIZE_ENCODING_MAX_64

#define FYGT_GENERIC_BITS_32			32
#define FYGT_INT_INPLACE_BITS_32		29
#define FYGT_STRING_INPLACE_SIZE_32		2
#define FYGT_STRING_INPLACE_SIZE_MASK_32	3
#define FYGT_SIZE_ENCODING_MAX_32		FYVL_SIZE_ENCODING_MAX_32

// by default we just follow the architecture
// it is conceivable that 64 bit generics could make sence in 32 bit too
#if !defined(FYGT_GENERIC_64) && !defined(FYGT_GENERIC_32)
#ifdef FY_HAS_64BIT_PTR
#define FYGT_GENERIC_64
#else
#define FYGT_GENERIC_32
#endif
#endif

#if defined(FYGT_GENERIC_64)
#define FYGT_GENERIC_BITS			FYGT_GENERIC_BITS_64
#define FYGT_INT_INPLACE_BITS			FYGT_INT_INPLACE_BITS_64
#define FYGT_STRING_INPLACE_SIZE		FYGT_STRING_INPLACE_SIZE_64
#define FYGT_STRING_INPLACE_SIZE_MASK		FYGT_STRING_INPLACE_SIZE_MASK_64
#elif defined(FYGT_GENERIC_32)
#define FYGT_GENERIC_BITS			FYGT_GENERIC_BITS_32
#define FYGT_INT_INPLACE_BITS			FYGT_INT_INPLACE_BITS_32
#define FYGT_STRING_INPLACE_SIZE		FYGT_STRING_INPLACE_SIZE_32
#define FYGT_STRING_INPLACE_SIZE_MASK		FYGT_STRING_INPLACE_SIZE_MASK_32
#else
#error Unsupported generic configuration
#endif

#define FYGT_INT_INPLACE_SIGN_SHIFT		(FYGT_GENERIC_BITS - FYGT_INT_INPLACE_BITS)

#define FYGT_SIZE_ENCODING_MAX FYVL_SIZE_ENCODING_MAX

#ifdef FYGT_GENERIC_64

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
// sequence   0 |pppppppppppppppp|pppp|0|000| pointer to a 16 byte aligned sequence
//              |0000000000000000|0000|0|000| empty sequence
// mapping    0 |pppppppppppppppp|pppp|1|000| pointer to a 16 byte aligned mapping
//              |0000000000000000|0000|1|000| empty mapping
// int        1 |xxxxxxxxxxxxxxxx|xxxx|x|001| int bits <= 61
//            2 |pppppppppppppppp|pppp|p|010| 8 byte aligned pointer to a long long
//              |0000000000000000|0000|0|010| int zero
// float      3 |ffffffffffffffff|0000|0|011| 32 bit float without loss of precision
//            4 |pppppppppppppppp|pppp|p|100| pointer to 8 byte aligned double
//              |0000000000000000|0000|0|100| float zero
// string     5 |ssssssssssssssss|0lll|0|101| string length <= 7 lll 3 bit length
//                                x    y      two extra available bit
//            6 |pppppppppppppppp|pppp|p|110| 8 byte aligned pointer to a string
//              |0000000000000000|0000|0|110| empty string
// indirect   7 |pppppppppppppppp|pppp|0|111| 16 byte aligned pointer to an indirect
//              |0000000000000000|0000|0|111| null indirect
// escape       |xxxxxxxxxxxxxxxx|xxxx|1|111|
// 
// 32 bit memory layout for generic types
//
//              |32             8|7654|3|210|
// -------------+----------------+----|-+---+
// sequence   0 |pppppppppppppppp|pppp|0|000| pointer to a 16 byte aligned sequence
//              |0000000000000000|0000|0|000| empty sequence
// mapping    0 |pppppppppppppppp|pppp|1|000| pointer to a 16 byte aligned mapping
//              |0000000000000000|0000|1|000| empty mapping
// int        1 |xxxxxxxxxxxxxxxx|xxxx|x|001| int bits <= 29
//            2 |pppppppppppppppp|pppp|1|010| 8 byte aligned pointer to an long long
//              |0000000000000000|0000|0|010| int zero
// float      3 |pppppppppppppppp|pppp|p|011| pointer to 8 byte aligned float
//              |0000000000000000|0000|0|100| float zero
//            4 |pppppppppppppppp|pppp|p|100| pointer to 8 byte aligned double
//              |0000000000000000|0000|0|100| double zero
// string     5 |ssssssssssssssss|00ll|0|101| string length <= 3 ll 2 bit length
//                                xy   z      three extra available bits
//            6 |pppppppppppppppp|pppp|p|110| 8 byte aligned pointer to a string
//              |0000000000000000|0000|0|110| empty string
// indirect   7 |pppppppppppppppp|pppp|0|111| 16 byte aligned pointer to an indirect
//              |0000000000000000|0000|0|111| null indirect
// escape       |xxxxxxxxxxxxxxxx|xxxx|1|111|
//
// escape codes:
// fy_null    0 |0000000000000000|0000|1|111| null value
// fy_false   1 |0000000000000000|0001|1|111| false boolean value
// fy_true    2 |0000000000000000|0010|1|111| true boolean value
// invalid      |1111111111111111|1111|1|111| All bits set
//
// all not defined codes map to invalid
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

// we can only inplace floats in 64 bits
#ifdef FYGT_GENERIC_64
#define FY_FLOAT_INPLACE_SHIFT	32
#endif

#define FY_STRING_INPLACE_V	5
#define FY_STRING_OUTPLACE_V	6
#define FY_STRING_INPLACE_SIZE_SHIFT	4

#define FY_INDIRECT_V		7

// escape mark is string inplace + set of the escape bit
#define FY_ESCAPE_SHIFT		(FY_INPLACE_TYPE_SHIFT + 1)
#define FY_ESCAPE_MASK		(((fy_generic_value)1 << FY_ESCAPE_SHIFT) - 1)
#define FY_ESCAPE_MARK		((1 << (FY_ESCAPE_SHIFT - 1)) | FY_INDIRECT_V)
#define FY_IS_ESCAPE(_v)	(((fy_generic_value)(_v) & FY_ESCAPE_MASK) == FY_ESCAPE_MARK)

#define FY_ESCAPE_ALT_NULL	0
#define FY_ESCAPE_ALT_FALSE	1
#define FY_ESCAPE_ALT_TRUE	2

#define FY_MAKE_ESCAPE(_v)		(((fy_generic_value)(_v) << FY_ESCAPE_SHIFT) | FY_ESCAPE_MARK)

#define fy_null_value			FY_MAKE_ESCAPE(FY_ESCAPE_ALT_NULL)
#define fy_false_value			FY_MAKE_ESCAPE(FY_ESCAPE_ALT_FALSE)
#define fy_true_value			FY_MAKE_ESCAPE(FY_ESCAPE_ALT_TRUE)
#define fy_invalid_value		FY_MAKE_ESCAPE(-1)
#define fy_seq_empty_value		((fy_generic_value)(FY_SEQ_V | 0))
#define fy_map_empty_value		((fy_generic_value)(FY_MAP_V | 0))

#define FYGT_INT_INPLACE_MAX		((1LL << (FYGT_INT_INPLACE_BITS - 1)) - 1)
#define FYGT_INT_INPLACE_MIN		(-(1LL << (FYGT_INT_INPLACE_BITS - 1)))

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
	union {
		fy_generic_value v;
		fy_generic_value_signed vs;
	};
} fy_generic;

#define fy_null		((fy_generic){ .v = fy_null_value })	/* simple does it */
#define fy_false	((fy_generic){ .v = fy_false_value })
#define fy_true		((fy_generic){ .v = fy_true_value })
#define fy_invalid	((fy_generic){ .v = fy_invalid_value })
#define fy_seq_empty	((fy_generic){. v = FY_SEQ_V })
#define fy_map_empty	((fy_generic){. v = FY_MAP_V })

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
typedef struct fy_generic_indirect {
	uintptr_t flags;	/* styling and existence flags */
	fy_generic value;	/* the actual value */
	fy_generic anchor;	/* string anchor or null */
	fy_generic tag;		/* string tag or null */
} fy_generic_indirect FY_GENERIC_CONTAINER_ALIGNMENT;

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

	/* we must check against the escape mask because the escape uses the same bits for indirect v */
	return (v.v & FY_ESCAPE_MARK) == FY_INDIRECT_V;
}

static inline const void *fy_generic_resolve_ptr(fy_generic ptr)
{
	/* clear the bottom 3 bits (all pointers are 8 byte aligned) */
	/* note collections have the bit 3 cleared too, so it's 16 byte aligned */
	ptr.v &= ~(uintptr_t)FY_INPLACE_TYPE_MASK;
	return (const void *)ptr.v;
}

static inline const void *fy_generic_resolve_collection_ptr(fy_generic ptr)
{
	/* clear the boot 3 bits (all pointers are 8 byte aligned) */
	/* note collections have the bit 3 cleared too, so it's 16 byte aligned */
	ptr.v &= ~(uintptr_t)FY_COLLECTION_MASK;
	return (const void *)ptr.v;
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
		[7] = FYGT_INDIRECT, [8 | 7] = FYGT_INVALID,	// this is the escape
	};
	static const enum fy_generic_type escapes[] = {
		[FY_ESCAPE_ALT_NULL] = FYGT_NULL,
		[FY_ESCAPE_ALT_FALSE] = FYGT_BOOL,
		[FY_ESCAPE_ALT_TRUE] = FYGT_BOOL,
	};
	enum fy_generic_type type;
	unsigned int escape_code;

	type = table[v.v & 15];
	if (type != FYGT_INVALID)
		return type;

	escape_code = (unsigned int)(v.v >> FY_ESCAPE_SHIFT);
	return escape_code < ARRAY_SIZE(escapes) ? escapes[escape_code] : FYGT_INVALID;
}

static inline bool fy_generic_is_in_place(fy_generic v)
{
	if (v.v == fy_invalid_value)
		return true;

	if (fy_generic_is_indirect(v))
		return false;

	switch (fy_generic_get_direct_type(v)) {
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

static inline enum fy_generic_type fy_generic_get_type(fy_generic v)
{
	const fy_generic_value *p;
	uintptr_t flags;
	enum fy_generic_type type;

	type = fy_generic_get_direct_type(v);
	if (type != FYGT_INDIRECT)
		return type;

	/* get the indirect */
	p = fy_generic_resolve_collection_ptr(v);

	/* an invalid value marks an alias, the value of the alias is at the anchor */
	flags = *p++;
	if (!(flags & FYGIF_VALUE))
		return FYGT_ALIAS;
	/* value immediately follows */
	v.v = *p;
	type = fy_generic_get_direct_type(v);
	return type != FYGT_INDIRECT ? type : FYGT_INVALID;
}

static inline void fy_generic_indirect_get(fy_generic v, fy_generic_indirect *gi)
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

	p = fy_generic_resolve_collection_ptr(v);

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

static inline const fy_generic *fy_genericp_indirect_get_valuep(const fy_generic *vp)
{
	const fy_generic_value *p;
	uintptr_t flags;

	if (!vp)
		return NULL;

	if (!fy_generic_is_indirect(*vp))
		return vp;

	p = fy_generic_resolve_collection_ptr(*vp);
	flags = *p++;
	return (flags & FYGIF_VALUE) ? (const fy_generic *)p : NULL;
}

static inline fy_generic fy_generic_indirect_get_value(const fy_generic v)
{
	const fy_generic_value *p;

	if (!fy_generic_is_indirect(v))
		return v;

	p = fy_generic_resolve_collection_ptr(v);
	return (p[0] & FYGIF_VALUE) ? *(const fy_generic *)&p[1] : fy_invalid;
}

static inline fy_generic fy_generic_indirect_get_anchor(fy_generic v)
{
	fy_generic_indirect gi;

	fy_generic_indirect_get(v, &gi);
	return gi.anchor;
}

static inline fy_generic fy_generic_indirect_get_tag(fy_generic v)
{
	fy_generic_indirect gi;

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

typedef struct fy_generic_sequence {
	size_t count;
	fy_generic items[];
} fy_generic_sequence FY_GENERIC_CONTAINER_ALIGNMENT;

// do not change the layout!
typedef union fy_generic_map_pair {
	struct {
		fy_generic key;
		fy_generic value;
	};
	fy_generic items[2];
} fy_generic_map_pair;

typedef struct fy_generic_mapping {
	size_t count;
	fy_generic_map_pair pairs[];
} fy_generic_mapping FY_GENERIC_CONTAINER_ALIGNMENT;

typedef struct fy_generic_sized_string {
	const char *data;
	size_t size;
} fy_generic_sized_string;

/* wrap into handles */
typedef const fy_generic_sequence *fy_generic_sequence_handle;
typedef const fy_generic_mapping *fy_generic_mapping_handle;
typedef const fy_generic_map_pair *fy_generic_map_pair_handle;
typedef const fy_generic_sized_string *fy_generic_sized_string_handle;

#define fy_seq_handle_null	((fy_generic_sequence_handle)NULL)
#define fy_map_handle_null	((fy_generic_mapping_handle)NULL)
#define fy_szstr_empty		((fy_generic_sized_string){ })

#define fy_generic_typeof(_v) \
	typeof(_Generic((_v), \
		char *: ((char *)0), \
		const char *: ((const char *)0), \
		default: _v))

static inline bool fy_generic_is_valid(const fy_generic v)
{
	return v.v != fy_invalid_value;
}

static inline bool fy_generic_is_invalid(const fy_generic v)
{
	return v.v == fy_invalid_value;
}

//
// example generation for bool
//
// fy_generic_is_direct_bool(), fy_generic_is_bool(), fy_generic_is_range_checked_bool()
//
//
#define FY_GENERIC_IS_TEMPLATE(_gtype, _gttype) \
static inline bool fy_generic_is_direct_ ## _gtype (const fy_generic v) \
{ \
	return fy_generic_get_direct_type(v) == FYGT_ ## _gttype ; \
} \
\
static inline bool fy_generic_is_ ## _gtype (const fy_generic v) \
{ \
	return fy_generic_get_direct_type(fy_generic_indirect_get_value(v)) == FYGT_ ## _gttype ; \
} \
\
struct fy_useless_struct_for_semicolon

/* the base types that match the spec */
FY_GENERIC_IS_TEMPLATE(gnull, NULL);
FY_GENERIC_IS_TEMPLATE(gbool, BOOL);
FY_GENERIC_IS_TEMPLATE(gint, INT);
FY_GENERIC_IS_TEMPLATE(gfloat, FLOAT);
FY_GENERIC_IS_TEMPLATE(string, STRING);
FY_GENERIC_IS_TEMPLATE(sequence, SEQUENCE);
FY_GENERIC_IS_TEMPLATE(mapping, MAPPING);
FY_GENERIC_IS_TEMPLATE(alias, ALIAS);

static inline void *fy_generic_get_gnull_no_check(fy_generic v)
{
	return NULL;
}

static inline fy_generic_value fy_generic_in_place_gnull(void *p)
{
	return p == NULL ? fy_null_value : fy_invalid_value;
}

static inline size_t fy_generic_out_of_place_size_gnull(void *v)
{
	return 0;
}

static inline fy_generic_value fy_generic_out_of_place_put_gnull(void *buf, void *v)
{
	return fy_invalid_value;	// should never happen
}

static inline bool fy_generic_get_gbool_no_check(fy_generic v)
{
	return v.v == fy_true_value;
}

static inline fy_generic_value fy_generic_in_place_gbool(_Bool v)
{
	return fy_invalid_value;	// should never happen
}

static inline size_t fy_generic_out_of_place_size_gbool(bool v)
{
	return 0;
}

static inline fy_generic_value fy_generic_out_of_place_put_gbool(void *buf, bool v)
{
	return fy_invalid_value;	// should never happen
}

static inline fy_generic_value fy_generic_in_place_gint(const long long v)
{
	if (v >= FYGT_INT_INPLACE_MIN && v <= FYGT_INT_INPLACE_MAX)
		return (((fy_generic_value)(signed long long)v) << FY_INT_INPLACE_SHIFT) | FY_INT_INPLACE_V;
	return fy_invalid_value;
}

static inline fy_generic_value fy_generic_out_of_place_put_gint(void *buf, const long long v)
{
	assert(((uintptr_t)buf & FY_INPLACE_TYPE_MASK) == 0);
	*(long long *)buf = v;
	return (fy_generic_value)buf | FY_INT_OUTPLACE_V;
}

#ifdef FYGT_GENERIC_64

static inline fy_generic_value fy_generic_in_place_gfloat(const double v)
{
	if (!isnormal(v) || (float)v == v) {
		const union { float f; uint32_t f_bits; } u = { .f = (float)v };
		return ((fy_generic_value)u.f_bits << FY_FLOAT_INPLACE_SHIFT) | FY_FLOAT_INPLACE_V;
	}
	return fy_invalid_value;
}

#else

static inline fy_generic_value fy_generic_in_place_gfloat(const double v)
{
	return fy_invalid_value;
}

#endif

static inline fy_generic_value fy_generic_out_of_place_put_gfloat(void *buf, const double v)
{
	assert(((uintptr_t)buf & FY_INPLACE_TYPE_MASK) == 0);
	*(double *)buf = v;
	return (fy_generic_value)buf | FY_FLOAT_OUTPLACE_V;
}

static inline long long fy_generic_get_gint_no_check(fy_generic v)
{
	const long long *p;

	/* make sure you sign extend */
	if ((v.v & FY_INPLACE_TYPE_MASK) == FY_INT_INPLACE_V)
		return (long long)(
			(fy_generic_value_signed)((v.v >> FY_INPLACE_TYPE_SHIFT) <<
                               (FYGT_GENERIC_BITS - FYGT_INT_INPLACE_BITS)) >>
                                       (FYGT_GENERIC_BITS - FYGT_INT_INPLACE_BITS));

	p = fy_generic_resolve_ptr(v);
	if (!p)
		return 0;
	return *p;
}

static inline size_t fy_generic_out_of_place_size_gint(const long long v)
{
	return (v >= FYGT_INT_INPLACE_MIN && v <= FYGT_INT_INPLACE_MAX) ? 0 : sizeof(long long);
}

#ifdef FYGT_GENERIC_64

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define FY_INPLACE_FLOAT_ADV	1
#else
#define FY_INPLACE_FLOAT_ADV	0
#endif

#endif

#ifdef FYGT_GENERIC_64

static inline float fy_generic_get_gfloat_no_check(fy_generic v)
{
	const double *p;

	if ((v.v & FY_INPLACE_TYPE_MASK) == FY_FLOAT_INPLACE_V)
		return *((float *)&v.v + FY_INPLACE_FLOAT_ADV);

	/* the out of place values are always doubles */
	p = fy_generic_resolve_ptr(v);
	if (!p)
		return 0.0;
	return (float)*p;
}

static inline size_t fy_generic_out_of_place_size_gfloat(const double v)
{
	return (!isnormal(v) || (float)v == v) ? 0 : sizeof(double);
}

#else
static inline float fy_generic_get_gfloat_no_check(fy_generic v)
{
	const void *p;

	p = fy_generic_resolve_ptr(v);
	if (!p)
		return 0.0;

	/* float, or double out of place */
	if ((v.v & FY_INPLACE_TYPE_MASK) == FY_FLOAT_INPLACE_V)
		return *(float *)p;
	else
		return (float)*(double *)p;
}

static inline size_t fy_generic_out_of_place_size_double(const double v)
{
	return sizeof(double);
}

#endif

// sequence
static inline const fy_generic_sequence *
fy_generic_sequence_resolve(const fy_generic seq)
{
	if (!fy_generic_is_indirect(seq)) {
		if (!fy_generic_is_direct_sequence(seq))
			return NULL;
		return fy_generic_resolve_collection_ptr(seq);
	}

	const fy_generic seqi = fy_generic_indirect_get_value(seq);

	if (!fy_generic_is_direct_sequence(seqi))
		return NULL;

	return fy_generic_resolve_collection_ptr(seqi);
}

static inline fy_generic_sequence_handle
fy_generic_sequence_to_handle(const fy_generic seq)
{
	return fy_generic_sequence_resolve(seq);
}

static inline const fy_generic *
fy_generic_sequencep_items(const fy_generic_sequence *seqp)
{
	return seqp ? &seqp->items[0] : NULL;
}

static inline size_t
fy_generic_sequencep_get_item_count(const fy_generic_sequence *seqp)
{
	return seqp ? seqp->count : 0;
}

static inline size_t fy_generic_sequence_get_item_count(fy_generic seq)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_item_count(seqp);
}

static inline const fy_generic *fy_generic_sequence_get_items(fy_generic seq, size_t *countp)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);

	if (!seqp) {
		*countp = 0;
		return NULL;
	}

	*countp = seqp->count;
	return &seqp->items[0];
}

static inline const fy_generic *fy_generic_sequencep_get_itemp(const fy_generic_sequence *seqp, size_t idx)
{
	return seqp && idx < seqp->count ? fy_genericp_indirect_get_valuep(&seqp->items[idx]) : NULL;
}

static inline const fy_generic *fy_generic_sequence_get_itemp(fy_generic seq, size_t idx)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_itemp(seqp, idx);
}

static inline fy_generic fy_generic_sequence_get_item_generic(fy_generic seq, size_t idx)
{
	const fy_generic *vp = fy_generic_sequence_get_itemp(seq, idx);
	return vp ? *vp : fy_invalid;
}

// mapping
static inline const fy_generic_mapping *
fy_generic_mapping_resolve(const fy_generic map)
{
	if (!fy_generic_is_indirect(map)) {
		if (!fy_generic_is_direct_mapping(map))
			return NULL;
		return fy_generic_resolve_collection_ptr(map);
	}

	const fy_generic mapi = fy_generic_indirect_get_value(map);

	if (!fy_generic_is_direct_mapping(mapi))
		return NULL;

	return fy_generic_resolve_collection_ptr(mapi);
}

static inline fy_generic_mapping_handle
fy_generic_mapping_to_handle(const fy_generic map)
{
	return fy_generic_mapping_resolve(map);
}

static inline const fy_generic *
fy_generic_mappingp_items(const fy_generic_mapping *mapp)
{
	return mapp ? &mapp->pairs[0].items[0] : NULL;
}

static inline size_t
fy_generic_mappingp_get_pair_count(const fy_generic_mapping *mapp)
{
	return mapp ? mapp->count : 0;
}

static inline const fy_generic_map_pair *
fy_generic_mapping_get_pairs(fy_generic map, size_t *countp)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);

	if (!mapp) {
		*countp = 0;
		return NULL;
	}

	*countp = mapp->count;
	return mapp->pairs;
}

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

static inline const fy_generic *fy_generic_mappingp_valuep_index(const fy_generic_mapping *mapp, fy_generic key, size_t *idxp)
{
	size_t i;

	if (!mapp)
		goto err_out;

	for (i = 0; i < mapp->count; i++) {
		if (fy_generic_compare(key, mapp->pairs[i].key) == 0) {
			if (idxp)
				*idxp = i;
			return &mapp->pairs[i].value;
		}
	}

err_out:
	if (idxp)
		*idxp = (size_t)-1;
	return NULL;
}

static inline const fy_generic *fy_generic_mappingp_get_valuep(const fy_generic_mapping *mapp, fy_generic key)
{
	size_t idx;
	return fy_generic_mappingp_valuep_index(mapp, key, &idx);
}

static inline const fy_generic *fy_generic_mapping_get_valuep_index(fy_generic map, fy_generic key, size_t *idxp)
{
	return fy_generic_mappingp_valuep_index(fy_generic_mapping_resolve(map), key, idxp);
}

static inline const fy_generic *fy_generic_mapping_get_valuep(fy_generic map, fy_generic key)
{
	return fy_generic_mapping_get_valuep_index(map, key, NULL);
}

static inline const fy_generic fy_generic_mapping_get_value_index(fy_generic map, fy_generic key, size_t *idxp)
{
	const fy_generic *vp = fy_generic_mapping_get_valuep_index(map, key, idxp);
	return vp ? *vp : fy_invalid;
}

static inline const fy_generic fy_generic_mapping_get_value(fy_generic map, fy_generic key)
{
	return fy_generic_mapping_get_value_index(map, key, NULL);
}

static inline size_t fy_generic_mapping_get_pair_count(fy_generic map)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return mapp ? mapp->count : 0;
}

//
// template for repetitive definitions
//
// for FY_GENERIC_INT_LVAL_TEMPLATE(int, int, INT_MIN, INT_MAX, 0) ->
//	FY_GENERIC_LVAL_TEMPLATE(int, int, INT, long long, gint, INT_MIN, INT_MAX, 0)
// 
// static inline long long fy_generic_get_int_no_check(fy_generic v);
// static inline bool fy_int_is_in_range(const long long v);
// static inline bool fy_generic_int_is_in_range_no_check(const fy_generic v);
// static inline bool fy_generic_int_is_in_range(const fy_generic v);
// static inline bool fy_generic_is_direct_int(const fy_generic v);
// static inline bool fy_generic_is_int(const fy_generic v);
// static inline int fy_generic_get_int_default(fy_generic v, int default_value);
// static inline int fy_generic_get_int(fy_generic v);
// static inline int fy_genericp_get_int_default(const fy_generic *vp, int default_value);
// static inline int fy_genericp_get_int(const fy_generic *vp);
//
#define FY_GENERIC_LVAL_TEMPLATE(_ctype, _gtype, _gttype, _xctype, _xgtype, _xminv, _xmaxv, _default_v) \
static inline _xctype fy_generic_get_ ## _gtype ## _no_check(fy_generic v) \
{ \
	return fy_generic_get_ ## _xgtype ## _no_check(v); \
} \
\
static inline bool fy_ ## _gtype ## _is_in_range(const _xctype v) \
{ \
	return v >= (_xctype)_xminv && v <= (_xctype)_xmaxv; \
} \
\
static inline bool fy_generic_ ## _gtype ## _is_in_range_no_check(const fy_generic v) \
{ \
	const _xctype xv = fy_generic_get_ ## _xgtype ## _no_check(v); \
	return fy_ ## _gtype ## _is_in_range(xv); \
} \
\
static inline bool fy_generic_ ## _gtype ## _is_in_range(const fy_generic v) \
{ \
	if (!fy_generic_is_direct_ ## _xgtype (v)) \
		return false; \
	return fy_generic_ ## _gtype ## _is_in_range_no_check(v); \
} \
\
static inline bool fy_generic_is_direct_ ## _gtype (const fy_generic v) \
{ \
	return fy_generic_ ## _gtype ## _is_in_range(v); \
} \
\
static inline bool fy_generic_is_ ## _gtype (const fy_generic v) \
{ \
	return fy_generic_is_direct_ ## _gtype (fy_generic_indirect_get_value(v)); \
} \
\
static inline fy_generic_value fy_generic_in_place_ ## _gtype ( _ctype v) \
{ \
	return fy_generic_in_place_ ## _xgtype ( (_xctype)v ); \
} \
\
static inline size_t fy_generic_out_of_place_size_ ## _gtype ( _ctype v) \
{ \
	return fy_generic_out_of_place_size_ ## _xgtype ( (_xctype)v ); \
} \
\
static inline fy_generic_value fy_generic_out_of_place_put_ ##_gtype (void *buf, const _ctype v) \
{ \
	return fy_generic_out_of_place_put_ ## _xgtype (buf, (_xctype)v ); \
} \
\
static inline _ctype fy_generic_get_ ## _gtype ## _default(fy_generic v, _ctype default_value) \
{ \
	if (!fy_generic_is_ ## _xgtype (v)) \
		return default_value; \
	const _xctype xv = fy_generic_get_ ## _xgtype ## _no_check(v); \
	if (!fy_ ## _gtype ## _is_in_range(xv)) \
		return default_value; \
	return (_ctype)xv; \
} \
\
static inline _ctype fy_generic_get_ ## _gtype (fy_generic v) \
{ \
	return fy_generic_get_ ## _gtype ## _default(v, _default_v ); \
} \
\
static inline _ctype fy_genericp_get_ ## _gtype ## _default(const fy_generic *vp, _ctype default_value) \
{ \
	return vp ? fy_generic_get_ ## _gtype ## _default(*vp, default_value) : default_value; \
} \
\
static inline _ctype fy_genericp_get_ ## _gtype (const fy_generic *vp) \
{ \
	return fy_genericp_get_ ## _gtype ## _default(vp, _default_v ); \
} \
\
static inline const fy_generic *fy_generic_sequencep_get_ ## _gtype ## _itemp(const fy_generic_sequence *seqp, size_t idx) \
{ \
	const fy_generic *vp = fy_generic_sequencep_get_itemp(seqp, idx); \
	return vp && fy_generic_is_direct_ ## _gtype (*vp) ? vp : NULL; \
} \
static inline const fy_generic *fy_generic_sequence_get_ ## _gtype ## _itemp(fy_generic seq, size_t idx) \
{ \
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq); \
	return fy_generic_sequencep_get_ ## _gtype ## _itemp(seqp, idx); \
} \
\
static inline _ctype fy_generic_sequence_get_ ## _gtype ## _default(fy_generic seq, size_t idx, _ctype default_value) \
{ \
	const fy_generic *vp = fy_generic_sequence_get_ ## _gtype ## _itemp(seq, idx); \
	return fy_genericp_get_  ## _gtype ## _default(vp, default_value); \
} \
\
static inline const fy_generic *fy_generic_mappingp_get_ ## _gtype ## _valuep(const fy_generic_mapping *mapp, fy_generic key) \
{ \
	const fy_generic *vp = fy_generic_mappingp_get_valuep(mapp, key); \
	return vp && fy_generic_is_direct_ ## _gtype (*vp) ? vp : NULL; \
} \
static inline const fy_generic *fy_generic_mapping_get_ ## _gtype ## _valuep(fy_generic map, fy_generic key) \
{ \
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map); \
	return fy_generic_mappingp_get_ ## _gtype ## _valuep(mapp, key); \
} \
\
static inline _ctype fy_generic_mapping_get_ ## _gtype ## _default(fy_generic map, fy_generic key, _ctype default_value) \
{ \
	const fy_generic *vp = fy_generic_mapping_get_ ## _gtype ## _valuep(map, key); \
	return fy_genericp_get_  ## _gtype ## _default(vp, default_value); \
} \
\
struct fy_useless_struct_for_semicolon

#define FY_GENERIC_INT_LVAL_TEMPLATE(_ctype, _gtype, _xminv, _xmaxv, _defaultv) \
	FY_GENERIC_LVAL_TEMPLATE(_ctype, _gtype, INT, long long, gint, _xminv, _xmaxv, _defaultv)

#define FY_GENERIC_FLOAT_LVAL_TEMPLATE(_ctype, _gtype, _xminv, _xmaxv, _defaultv) \
	FY_GENERIC_LVAL_TEMPLATE(_ctype, _gtype, FLOAT, double, gfloat, _xminv, _xmaxv, _defaultv)

FY_GENERIC_LVAL_TEMPLATE(void *, null, NULL, void *, gnull, NULL, NULL, NULL);
FY_GENERIC_LVAL_TEMPLATE(bool, bool, BOOL, bool, gbool, false, true, false);
FY_GENERIC_INT_LVAL_TEMPLATE(char, char, CHAR_MIN, CHAR_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(unsigned char, unsigned_char, 0, UCHAR_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(signed char, signed_char, SCHAR_MIN, SCHAR_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(short, short, SHRT_MIN, SHRT_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(unsigned short, unsigned_short, 0, USHRT_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(signed short, signed_short, SHRT_MIN, SHRT_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(int, int, INT_MIN, INT_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(unsigned int, unsigned_int, 0, UINT_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(signed int, signed_int, INT_MIN, INT_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(long, long, LONG_MIN, LONG_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(unsigned long, unsigned_long, 0, ULONG_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(signed long, signed_long, LONG_MIN, LONG_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(long long, long_long, LLONG_MIN, LLONG_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(unsigned long long, unsigned_long_long, 0, LONG_MAX, 0);	// XXX not ULONG_MAX
FY_GENERIC_INT_LVAL_TEMPLATE(signed long long, signed_long_long, LLONG_MIN, LLONG_MAX, 0);
FY_GENERIC_FLOAT_LVAL_TEMPLATE(float, float, FLT_MIN, FLT_MAX, 0.0);
FY_GENERIC_FLOAT_LVAL_TEMPLATE(double, double, DBL_MIN, DBL_MAX, 0.0);

#define FY_GENERIC_SCALAR_DISPATCH(_base, _ctype, _gtype) \
	_ctype: _base ## _ ## _gtype

#define FY_GENERIC_ALL_SCALARS_DISPATCH(_base) \
	FY_GENERIC_SCALAR_DISPATCH(_base, void *, null), \
	FY_GENERIC_SCALAR_DISPATCH(_base, _Bool, bool), \
	FY_GENERIC_SCALAR_DISPATCH(_base, signed char, signed_char), \
	FY_GENERIC_SCALAR_DISPATCH(_base, unsigned char, unsigned_char), \
	FY_GENERIC_SCALAR_DISPATCH(_base, signed short, signed_short), \
	FY_GENERIC_SCALAR_DISPATCH(_base, unsigned short, unsigned_short), \
	FY_GENERIC_SCALAR_DISPATCH(_base, signed int, signed_int), \
	FY_GENERIC_SCALAR_DISPATCH(_base, unsigned int, unsigned_int), \
	FY_GENERIC_SCALAR_DISPATCH(_base, signed long, signed_long), \
	FY_GENERIC_SCALAR_DISPATCH(_base, unsigned long, unsigned_long), \
	FY_GENERIC_SCALAR_DISPATCH(_base, signed long long, signed_long_long), \
	FY_GENERIC_SCALAR_DISPATCH(_base, unsigned long long, unsigned_long_long), \
	FY_GENERIC_SCALAR_DISPATCH(_base, float, float), \
	FY_GENERIC_SCALAR_DISPATCH(_base, double, double)

#define FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, _ctype, _gtype) \
	_ctype: _base ## _ ## _gtype ## _ ## _sfx

#define FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(_base, _sfx) \
	FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, void *, null), \
	FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, _Bool, bool), \
	FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, signed char, signed_char), \
	FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, unsigned char, unsigned_char), \
	FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, signed short, signed_short), \
	FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, unsigned short, unsigned_short), \
	FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, signed int, signed_int), \
	FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, unsigned int, unsigned_int), \
	FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, signed long, signed_long), \
	FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, unsigned long, unsigned_long), \
	FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, signed long long, signed_long_long), \
	FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, unsigned long long, unsigned_long_long), \
	FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, float, float), \
	FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, double, double)

/* NOTE: All get method multiple-evaluate the argument, so take care */

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
		fy_generic *___vp = fy_alloca_align(sizeof(fy_generic), 		\
					FY_GENERIC_SCALAR_ALIGN);			\
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

static inline size_t
fy_generic_get_string_inplace_size(const fy_generic v)
{
	assert((v.v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V);
	return (size_t)((v.v >> FY_STRING_INPLACE_SIZE_SHIFT) & FYGT_STRING_INPLACE_SIZE_MASK);
}

static const char *
fy_genericp_get_string_inplace(const fy_generic *vp)
{
	assert(((*vp).v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V);
	return (const char *)vp + FY_INPLACE_STRING_ADV;
}

static inline const char *
fy_genericp_get_string_size_no_check(const fy_generic *vp, size_t *lenp)
{
	if (((*vp).v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V) {
		*lenp = fy_generic_get_string_inplace_size(*vp);
		return (const char *)vp + FY_INPLACE_STRING_ADV;
	}
	return (const char *)fy_decode_size_nocheck(fy_generic_resolve_ptr(*vp), lenp);
}

static inline const char *
fy_genericp_get_string_no_check(const fy_generic *vp)
{
	if (((*vp).v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V)
		return (const char *)vp + FY_INPLACE_STRING_ADV;

	return (const char *)fy_skip_size_nocheck(fy_generic_resolve_ptr(*vp));
}

/* this dance is done because the inplace strings must be alloca'ed */
#define fy_genericp_get_string_size(_vp, _lenp)							\
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
					__vpp = fy_alloca_align(sizeof(*__vpp), 		\
							FY_GENERIC_SCALAR_ALIGN	);		\
					*__vpp = __vfinal;					\
					__vp = __vpp;						\
				} else								\
					__vp = &__vfinal;					\
			}									\
			__ret = fy_genericp_get_string_size_no_check(__vp, __lenp);		\
		}										\
		__ret;										\
	})

#define fy_genericp_get_string_default(_vp, _default_v)			\
	({								\
		const char *__ret;					\
		size_t __len;						\
		__ret = fy_genericp_get_string_size((_vp), &__len);	\
		if (!__ret)						\
			__ret = (_default_v);				\
		__ret;							\
	})

#define fy_genericp_get_string(_vp) (fy_genericp_get_string_default((_vp), ""))

/* this is special cased, for inplace strings creates an alloca fy_generic to return a
 * pointer to...
 * Also note there is no attempt to get the size either.
 */
#define fy_generic_get_string_size(_v, _lenp) 	 						\
	({											\
	 	fy_generic __v = (_v);								\
		const char *__ret = NULL;							\
		size_t *__lenp = (_lenp);							\
		fy_generic *__vp;								\
												\
		if (!fy_generic_is_string(__v)) {						\
			*__lenp = 0;								\
		} else {									\
			__v = fy_generic_indirect_get_value(__v);				\
			if ((__v.v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V) {		\
				__vp = fy_alloca_align(sizeof(*__vp), FY_GENERIC_SCALAR_ALIGN);	\
				*__vp = __v;							\
			} else 									\
				__vp = &__v;							\
			__ret = fy_genericp_get_string_size_no_check(__vp, __lenp);		\
		}										\
		__ret;										\
	})

#define fy_generic_get_string_default(_v, _default_v)			\
	({								\
		const char *__ret;					\
		size_t __len;						\
		__ret = fy_generic_get_string_size((_v), &__len);	\
		if (!__ret)						\
			__ret = (_default_v);				\
		__ret;							\
	})

#define fy_generic_get_string(_v) (fy_generic_get_string_default((_v), ""))

static inline const char *fy_genericp_get_const_char_ptr_default(const fy_generic *vp, const char *default_value)
{
	if (!vp || !fy_generic_is_string(*vp))
		return default_value;

	return fy_genericp_get_string_no_check(fy_genericp_indirect_get_valuep(vp));
}

static inline const char *fy_genericp_get_const_char_ptr(const fy_generic *vp)
{
	return fy_genericp_get_const_char_ptr_default(vp, "");
}

static inline char *fy_genericp_get_char_ptr_default(fy_generic *vp, const char *default_value)
{
	return (char *)fy_genericp_get_const_char_ptr_default(vp, default_value);
}

static inline char *fy_genericp_get_char_ptr(fy_generic *vp)
{
	return fy_genericp_get_char_ptr_default(vp, "");
}

#define fy_generic_get_alias(_v) \
	({ \
		fy_generic __va = fy_generic_get_anchor((_v)); \
		fy_generic_get_string(__va); \
	})

#define fy_bool(_v)			((bool)(_v) ? fy_true : fy_false)

#define fy_int_alloca(_v) 									\
	({											\
	 	typeof (1 ? (_v) : (_v)) __v = (_v);						\
		long long *__vp;								\
		fy_generic_value _r;								\
												\
		if (__v >= FYGT_INT_INPLACE_MIN && __v <= FYGT_INT_INPLACE_MAX)			\
			_r = (((fy_generic_value)(signed long long)__v) << FY_INT_INPLACE_SHIFT) \
							| FY_INT_INPLACE_V;			\
		else {										\
			__vp = fy_alloca_align(sizeof(*__vp), FY_GENERIC_SCALAR_ALIGN);		\
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
			_r = (fy_generic_value)((((unsigned long long)(signed long long)(_v))	\
						<< FY_INT_INPLACE_SHIFT) | FY_INT_INPLACE_V);	\
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
#ifdef FYGT_GENERIC_64
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

static inline fy_generic_value fy_generic_in_place_const_szstrp(const fy_generic_sized_string *szstrp)
{
	if (!szstrp)
		return fy_invalid_value;

	return fy_generic_in_place_char_ptr_len(szstrp->data, szstrp->size);
}

static inline fy_generic_value fy_generic_in_place_szstr(const fy_generic_sized_string szstr)
{
	return fy_generic_in_place_const_szstrp(&szstr);
}

static inline fy_generic_value fy_generic_in_place_invalid(...)
{
	return fy_invalid_value;
}

// pass-through
static inline fy_generic_value fy_generic_in_place_generic(fy_generic v)
{
	return v.v;
}

// convert it to a sequence (if it's aligned properly)
static inline fy_generic_value fy_generic_in_place_sequence_handle(fy_generic_sequence_handle seqh)
{
	uintptr_t ptr = (uintptr_t)seqh;

	/* NULL? code then it's fy_null */
	if (!ptr)
		return fy_seq_empty_value;

	/* if has to exist and be properly aligned */
	if (ptr & (FY_GENERIC_CONTAINER_ALIGN - 1))
		return fy_invalid_value;

	return (fy_generic_value)((uintptr_t)ptr | FY_SEQ_V);
}

// convert it to a sequence (if it's aligned properly)
static inline fy_generic_value fy_generic_in_place_mapping_handle(fy_generic_mapping_handle maph)
{
	uintptr_t ptr = (uintptr_t)maph;

	/* NULL? code then it's fy_null */
	if (!ptr)
		return fy_map_empty_value;

	/* if has to exist and be properly aligned */
	if (ptr & (FY_GENERIC_CONTAINER_ALIGN - 1))
		return fy_invalid_value;

	return (fy_generic_value)((uintptr_t)ptr | FY_MAP_V);
}

#define fy_to_generic_inplace_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH(fy_generic_in_place), \
	char *: fy_generic_in_place_char_ptr, \
	const char *: fy_generic_in_place_char_ptr, \
	fy_generic: fy_generic_in_place_generic, \
	fy_generic_sequence_handle: fy_generic_in_place_sequence_handle, \
	fy_generic_mapping_handle: fy_generic_in_place_mapping_handle, \
	const fy_generic_sized_string *: fy_generic_in_place_const_szstrp, \
	fy_generic_sized_string *: fy_generic_in_place_const_szstrp, \
	fy_generic_sized_string: fy_generic_in_place_szstr, \
	default: fy_generic_in_place_invalid

#define fy_to_generic_inplace(_v) ( _Generic((_v), fy_to_generic_inplace_Generic_dispatch)(_v))

static inline size_t fy_generic_out_of_place_size_char_ptr(const char *p)
{
	if (!p)
		return 0;

	return FYGT_SIZE_ENCODING_MAX + strlen(p) + 1;
}

static inline size_t fy_generic_out_of_place_size_generic(...)
{
	/* should never have to do that */
	return 0;
}

static inline size_t fy_generic_out_of_place_size_const_szstrp(const fy_generic_sized_string *szstrp)
{
	if (!szstrp)
		return 0;

	return FYGT_SIZE_ENCODING_MAX + szstrp->size + 1;
}

static inline size_t fy_generic_out_of_place_size_szstr(fy_generic_sized_string szstr)
{
	return fy_generic_out_of_place_size_const_szstrp(&szstr);
}

static inline size_t fy_generic_out_of_place_size_sequence_handle(fy_generic_sequence_handle seqh)
{
	return 0;	// TODO calculate
}

static inline size_t fy_generic_out_of_place_size_mapping_handle(fy_generic_mapping_handle maph)
{
	return 0;	// TODO calculate
}

#define fy_to_generic_outofplace_size_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH(fy_generic_out_of_place_size), \
	char *: fy_generic_out_of_place_size_char_ptr, \
	const char *: fy_generic_out_of_place_size_char_ptr, \
	fy_generic: fy_generic_out_of_place_size_generic, \
	fy_generic_sequence_handle: fy_generic_out_of_place_size_sequence_handle, \
	fy_generic_mapping_handle: fy_generic_out_of_place_size_mapping_handle, \
	const fy_generic_sized_string *: fy_generic_out_of_place_size_const_szstrp, \
	fy_generic_sized_string *: fy_generic_out_of_place_size_const_szstrp, \
	fy_generic_sized_string: fy_generic_out_of_place_size_szstr

#define fy_to_generic_outofplace_size(_v) \
	(_Generic((_v), fy_to_generic_outofplace_size_Generic_dispatch)(_v))

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

static inline fy_generic_value fy_generic_out_of_place_put_const_szstrp(void *buf, const fy_generic_sized_string *szstrp)
{
	uint8_t *s;

	if (!szstrp)
		return fy_invalid_value;

	assert(((uintptr_t)buf & FY_INPLACE_TYPE_MASK) == 0);

	s = fy_encode_size(buf, FYGT_SIZE_ENCODING_MAX, szstrp->size);
	memcpy(s, szstrp->data, szstrp->size);
	s[szstrp->size] = '\0';		// maybe we don't need this but play it safe
	return (fy_generic_value)buf | FY_STRING_OUTPLACE_V;
}

static inline fy_generic_value fy_generic_out_of_place_put_szstr(void *buf, fy_generic_sized_string szstrp)
{
	return fy_generic_out_of_place_put_const_szstrp(buf, &szstrp);
}

static inline fy_generic_value fy_generic_out_of_place_put_generic(void *buf, fy_generic v)
{
	/* we just pass-through, should never happen */
	return fy_invalid_value;
}

static inline fy_generic_value fy_generic_out_of_place_put_sequence_handle(void *buf, fy_generic_sequence_handle seqh)
{
	return fy_invalid_value;	// shoud never happen?
}

static inline fy_generic_value fy_generic_out_of_place_put_mapping_handle(void *buf, fy_generic_mapping_handle maph)
{
	return fy_invalid_value;	// shoud never happen?
}

#define fy_to_generic_outofplace_put_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH(fy_generic_out_of_place_put), \
	char *: fy_generic_out_of_place_put_char_ptr, \
	const char *: fy_generic_out_of_place_put_char_ptr, \
	fy_generic: fy_generic_out_of_place_put_generic, \
	fy_generic_sequence_handle: fy_generic_out_of_place_put_sequence_handle, \
	fy_generic_mapping_handle: fy_generic_out_of_place_put_mapping_handle, \
	const fy_generic_sized_string *: fy_generic_out_of_place_put_const_szstrp, \
	fy_generic_sized_string *: fy_generic_out_of_place_put_const_szstrp, \
	fy_generic_sized_string: fy_generic_out_of_place_put_szstr

#define fy_to_generic_outofplace_put(_vp, _v) \
	(_Generic((_v), fy_to_generic_outofplace_put_Generic_dispatch)((_vp), (_v)))

#define fy_to_generic_value(_v) \
	({	\
		typeof (1 ? (_v) : (_v)) __v = (_v); \
		fy_generic_value __r; \
		\
		__r = fy_to_generic_inplace(__v); \
		if (__r == fy_invalid_value) { \
			size_t __outofplace_size = fy_to_generic_outofplace_size(__v); \
			fy_generic *__vp = __outofplace_size ? fy_alloca_align(__outofplace_size, FY_GENERIC_CONTAINER_ALIGN) : NULL; \
			__r = fy_to_generic_outofplace_put(__vp, __v); \
		} \
		__r; \
	})

#define fy_to_generic(_v) \
	((fy_generic) { .v = fy_to_generic_value(_v) })

#ifdef FYGT_GENERIC_64

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
			__vp = fy_alloca_align(sizeof(*__vp), FY_GENERIC_SCALAR_ALIGN);		\
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

#define fy_float_alloca(_v) 							\
	({									\
		double __v = (_v);						\
		double *__vp;							\
		fy_generic_value _r;						\
										\
		__vp = fy_alloca_align(sizeof(*__vp), FY_GENERIC_SCALAR_ALIGN);	\
		assert(((uintptr_t)__vp & FY_INPLACE_TYPE_MASK) == 0);		\
		*__vp = __v;							\
		_r = (fy_generic_value)__vp | FY_FLOAT_OUTPLACE_V;		\
		_r;								\
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

/* literal strings in C, are char *
 * However you can't get a type of & "literal"
 * So we use that to detect literal strings
 */
#define FY_CONST_P_INIT(_v) \
	(_Generic((_v), \
		char *: _Generic(&(_v), char **: "", default: (_v)), \
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
			__vp = fy_alloca_align(FYGT_SIZE_ENCODING_MAX + __len + 1, 		\
					FY_GENERIC_SCALAR_ALIGN); 				\
			assert(((uintptr_t)__vp & FY_INPLACE_TYPE_MASK) == 0);			\
			__s = fy_encode_size(__vp, FYGT_SIZE_ENCODING_MAX, __len);		\
			memcpy(__s, __v, __len);						\
			__s[__len] = '\0';							\
			_r = (fy_generic_value)__vp | FY_STRING_OUTPLACE_V;			\
		}										\
		_r;										\
	})

#ifdef FYGT_GENERIC_64

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

#define fy_stringf_value(_fmt, ...) \
	({ \
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
	})

#define fy_stringf(_fmt, ...) \
	((fy_generic){ .v = fy_stringf_value((_fmt), ## __VA_ARGS__) })

#define fy_sequence_alloca(_count, _items) 						\
	({										\
		fy_generic_sequence *__vp;						\
		size_t __count = (_count);						\
		size_t __size = sizeof(*__vp) + __count * sizeof(fy_generic);		\
											\
		__vp = fy_alloca_align(__size, FY_GENERIC_CONTAINER_ALIGN);		\
		__vp->count = (_count);							\
		memcpy(__vp->items, (_items), __count * sizeof(fy_generic)); 		\
		(fy_generic_value)((uintptr_t)__vp | FY_SEQ_V);				\
	})

#define fy_sequence_explicit(_count, _items) \
	({ \
		size_t __count = (_count); \
		__count ? fy_sequence_alloca((_count), (_items)) : fy_seq_empty_value; \
	})

#define fy_sequence_create(_count, _items) \
	((fy_generic) { .v = fy_sequence_explicit((_count), (_items)) })

#define _FY_CPP_GITEM_ONE(arg) fy_to_generic(arg)
#define _FY_CPP_GITEM_LATER_ARG(arg) , _FY_CPP_GITEM_ONE(arg)
#define _FY_CPP_GITEM_LIST(...) FY_CPP_MAP(_FY_CPP_GITEM_LATER_ARG, __VA_ARGS__)

#define _FY_CPP_VA_GITEMS(...)          \
    _FY_CPP_GITEM_ONE(FY_CPP_FIRST(__VA_ARGS__)) \
    _FY_CPP_GITEM_LIST(FY_CPP_REST(__VA_ARGS__))

#define FY_CPP_VA_GITEMS(_count, ...) \
	((fy_generic [(_count)]) { __VA_OPT__(_FY_CPP_VA_GITEMS(__VA_ARGS__)) })

#define fy_sequence(...) \
	((fy_generic) { \
		.v = fy_sequence_explicit( \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS( \
				FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)) })

#define fy_mapping_alloca(_count, _pairs) 						\
	({										\
		fy_generic_mapping *__vp;						\
		size_t __count = (_count);						\
		size_t __size = sizeof(*__vp) + 2 * __count * sizeof(fy_generic);	\
											\
		__vp = fy_alloca_align(__size, FY_GENERIC_CONTAINER_ALIGN);		\
		__vp->count = (_count);							\
		memcpy(__vp->pairs, (_pairs), 2 * __count * sizeof(fy_generic)); 	\
		(fy_generic_value)((uintptr_t)__vp | FY_MAP_V);				\
	})

#define fy_mapping_explicit(_count, _pairs) \
	({ \
		size_t __count = (_count); \
		__count ? fy_mapping_alloca((_count), (_pairs)) : fy_map_empty_value; \
	})

#define fy_mapping_create(_count, _items) \
	((fy_generic) { .v = fy_mapping_explicit((_count), (_items)) })

#define fy_mapping(...) \
	((fy_generic) { \
		.v = fy_mapping_explicit( \
			FY_CPP_VA_COUNT(__VA_ARGS__) / 2, \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)) })

// indirect

#define fy_indirect_alloca(_v, _anchor, _tag)						\
	({										\
		fy_generic_value __v = (_v);						\
		fy_generic_value __anchor = (_anchor), __tag = (_tag), *__vp;		\
		int __i = 1;								\
											\
		__vp = fy_alloca_align(4 * sizeof(*__vp), FY_GENERIC_CONTAINER_ALIGN);	\
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

static inline fy_generic fy_generic_get_generic_default(fy_generic v, fy_generic vdefault)
{
	if (fy_generic_is_valid(v))
		return v;
	return vdefault;
}

static inline fy_generic_sequence_handle fy_generic_get_sequence_handle_default(fy_generic v, fy_generic_sequence_handle default_value)
{
	const fy_generic_sequence_handle seqh = fy_generic_sequence_to_handle(v);
	return seqh ? seqh : default_value;
}

static inline fy_generic_mapping_handle fy_generic_get_mapping_handle_default(fy_generic v, fy_generic_mapping_handle default_value)
{
	const fy_generic_mapping_handle maph = fy_generic_mapping_to_handle(v);
	return maph ? maph : default_value;
}

/* special... handling for in place strings */
static inline const char *fy_generic_get_const_char_ptr_default(fy_generic v, const char *default_value)
{
	if (!fy_generic_is_string(v))
		return default_value;

	v = fy_generic_indirect_get_value(v);

	/* out of place is easier actually */
	if ((v.v & FY_INPLACE_TYPE_MASK) != FY_STRING_INPLACE_V)
		return (const char *)fy_skip_size_nocheck(fy_generic_resolve_ptr(v));

	return NULL;
}

static inline char *fy_generic_get_char_ptr_default(fy_generic v, char *default_value)
{
	return (char *)fy_generic_get_const_char_ptr_default(v, default_value);
}

static inline fy_generic_sized_string fy_generic_get_sized_string_default(fy_generic v, const fy_generic_sized_string default_value)
{
	size_t size;
	const char *data;

	if (!fy_generic_is_string(v))
		return default_value;

	v = fy_generic_indirect_get_value(v);

	data = fy_genericp_get_string_size_no_check(&v, &size);

	if ((v.v & FY_INPLACE_TYPE_MASK) != FY_STRING_INPLACE_V)
		return (fy_generic_sized_string){ .data = data, .size = size };

	/* NOTE: this is not directly useable, if it's inline it must be alloca'ed */
	return (fy_generic_sized_string){ .data = NULL, .size = 0 };
}

static inline size_t fy_generic_get_const_char_ptr_default_alloca(fy_generic v)
{
	/* only do the alloca dance for in place strings */
	return ((v.v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V) ? sizeof(fy_generic) : 0;
}

static inline size_t fy_generic_get_sized_string_default_alloca(fy_generic v)
{
	return fy_generic_get_const_char_ptr_default_alloca(v);
}

/* never allocas for anything else */
static inline size_t fy_generic_get_default_should_alloca_never(fy_generic v)
{
	return 0;
}

static inline void fy_generic_get_const_char_ptr_default_final(fy_generic v,
		void *p, size_t size,
		const char *default_value, const char **store_value)
{
	size_t len;
	char *store = p;

	/* only for in place strings */
	assert((v.v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V);
	len = fy_generic_get_string_inplace_size(v);
	assert(size >= len + 1);

	/* we only fill the simple generic value and return a pointer to the in place storage */
	memcpy(store, fy_genericp_get_string_inplace(&v), len);
	store[len] = '\0';
	*store_value = store;
}

static inline void fy_generic_get_sized_string_default_final(fy_generic v,
		void *p, size_t size,
		fy_generic_sized_string default_value, fy_generic_sized_string *store_value)
{
	size_t len;
	char *store = p;

	/* only for in place strings */
	assert((v.v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V);
	len = fy_generic_get_string_inplace_size(v);
	assert(size >= len + 1);

	/* we only fill the simple generic value and return a pointer to the in place storage */
	memcpy(store, fy_genericp_get_string_inplace(&v), len);
	store[len] = '\0';

	store_value->data = store;
	store_value->size = len;
}

static inline void fy_generic_get_char_ptr_default_final(fy_generic v,
		void *p, size_t size,
		char *default_value, char **store_value)
{
	return fy_generic_get_const_char_ptr_default_final(v, p, size, default_value,
			(const char **)store_value);
}

static inline void fy_generic_get_default_final_never(fy_generic v,
		void *p, size_t size, ...)
{
	return;	// do nothing
}

/* defaults for a given type */
#define fy_generic_get_type_default_Generic_dispatch \
	void *: NULL, \
	_Bool: false, \
	signed char: (signed char)0, \
	unsigned char: (unsigned char)0, \
	signed short: (signed short)0, \
	unsigned short: (unsigned short)0, \
	signed int: (signed int)0, \
	unsigned int: (unsigned int)0, \
	signed long: (signed long)0, \
	unsigned long: (unsigned long)0, \
	signed long long: (signed long long)0, \
	unsigned long long: (unsigned long long)0, \
	float: (float)0, \
	double: (double)0, \
	char *: (char *)NULL, \
	const char *: (const char *)NULL, \
	fy_generic_sequence_handle: fy_seq_handle_null, \
	fy_generic_mapping_handle: fy_map_handle_null, \
	const fy_generic_sized_string *: (const fy_generic_sized_string *)NULL, \
	fy_generic_sized_string *: (fy_generic_sized_string *)NULL, \
	fy_generic_sized_string: ((fy_generic_sized_string){ .data = NULL, .size = 0 }), \
	fy_generic: fy_null


#define fy_generic_get_type_default(_type) \
	({ \
		 _type __tmp = _Generic(__tmp, fy_generic_get_type_default_Generic_dispatch); \
		__tmp; \
	})

#define fy_generic_get_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_generic_get, default), \
	char *: fy_generic_get_char_ptr_default, \
	const char *: fy_generic_get_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_generic_get_sequence_handle_default, \
	fy_generic_mapping_handle: fy_generic_get_mapping_handle_default, \
	fy_generic_sized_string: fy_generic_get_sized_string_default, \
	fy_generic: fy_generic_get_generic_default

//	fy_generic_sized_string: fy_generic_get_szstr_default,


#define fy_generic_get_default_alloca_Generic_dispatch \
	char *: fy_generic_get_const_char_ptr_default_alloca, \
	const char *: fy_generic_get_const_char_ptr_default_alloca, \
	fy_generic_sized_string: fy_generic_get_sized_string_default_alloca, \
	default: fy_generic_get_default_should_alloca_never

#define fy_generic_get_default_final_Generic_dispatch \
	char *: fy_generic_get_char_ptr_default_final, \
	const char *: fy_generic_get_const_char_ptr_default_final, \
	fy_generic_sized_string: fy_generic_get_sized_string_default_final, \
	default: fy_generic_get_default_final_never

#if 0
#define fy_generic_get_default(_v, _dv) \
	({ \
		fy_generic __v = (_v); \
		typeof ((_dv) + 0) __dv = (_dv); \
		typeof ((_dv) + 0) __ret; \
		size_t __size; \
		void *__p; \
		\
		__ret = _Generic(__dv, fy_generic_get_default_Generic_dispatch)(__v, __dv); \
		__size = _Generic(__dv, fy_generic_get_default_alloca_Generic_dispatch)(__v); \
		if (__size) { \
			__p = fy_alloca_align(__size, FY_GENERIC_CONTAINER_ALIGN); \
			_Generic(__dv, fy_generic_get_default_final_Generic_dispatch) \
	 			(__v, __p, __size, __dv, &__ret); \
		} \
		__ret; \
	})
#else
#define fy_generic_get_default(_v, _dv) \
	({ \
		fy_generic __v = (_v); \
		fy_generic_typeof(_dv) __dv = (_dv); \
		fy_generic_typeof(_dv) __ret; \
		size_t __size; \
		void *__p; \
		\
		__ret = _Generic(__dv, fy_generic_get_default_Generic_dispatch)(__v, __dv); \
		__size = _Generic(__dv, fy_generic_get_default_alloca_Generic_dispatch)(__v); \
		if (__size) { \
			__p = fy_alloca_align(__size, FY_GENERIC_CONTAINER_ALIGN); \
			_Generic(__dv, fy_generic_get_default_final_Generic_dispatch) \
	 			(__v, __p, __size, __dv, &__ret); \
		} \
		__ret; \
	})
#endif

/* when we have a pointer we can return to inplace strings */
#define fy_generic_get(_v, _type) \
	(fy_generic_get_default((_v), fy_generic_get_type_default(_type)))

#if 0
/* when we have a pointer we can return to inplace strings */
#define fy_genericp_get_default_Generic_dispatch \
	void *: fy_generic_get_null_default, \
	_Bool: fy_generic_get_bool_default, \
	signed char: fy_generic_get_signed_char_default, \
	unsigned char: fy_generic_get_unsigned_char_default, \
	signed short: fy_generic_get_signed_short_default, \
	unsigned short: fy_generic_get_unsigned_short_default, \
	signed int: fy_generic_get_signed_int_default, \
	unsigned int: fy_generic_get_unsigned_int_default, \
	signed long: fy_generic_get_signed_long_default, \
	unsigned long: fy_generic_get_unsigned_long_default, \
	signed long long: fy_generic_get_signed_long_long_default, \
	unsigned long long: fy_generic_get_unsigned_long_long_default, \
	float: fy_generic_get_float_default, \
	double: fy_generic_get_double_default, \
	char *: fy_generic_get_char_ptr_default, \
	const char *: fy_generic_get_const_char_ptr_default, \
	fy_generic: fy_generic_get_generic_default

#define fy_genericp_get_default(_vp, _dv) \
	({ \
		const fy_generic __vp = (_vp); \
		typeof ((_dv) + 0) __dv = (_dv); \
		typeof ((_dv) + 0) __ret; \
		size_t __size; \
		void *__p; \
		\
		__ret = _Generic(__dv, fy_genericp_get_default_Generic_dispatch)(__v, __dv); \
	})

#define fy_genericp_get(_vp, _type) \
	(fy_genericp_get_default((_vp), fy_generic_get_type_default(_type)))
#endif

#define fy_generic_get_default_coerse(_v, _dv) \
	({ \
		fy_generic __vv = fy_to_generic(_v); \
	 	fy_generic_get_default(__vv, (_dv)); \
	})

static inline fy_generic_sequence_handle
fy_generic_sequencep_get_generic_sequence_handle_default(const fy_generic_sequence *seqp, size_t idx,
		fy_generic_sequence_handle default_value)
{
	const fy_generic *vp = fy_generic_sequencep_get_itemp(seqp, idx);
	fy_generic_sequence_handle seqh;

	if (!vp)
		return default_value;

	seqh = fy_generic_sequence_to_handle(*vp);
	if (!seqh)
		return default_value;

	return seqh;
}

static inline fy_generic_sequence_handle
fy_generic_sequence_get_generic_sequence_handle_default(fy_generic seq, size_t idx,
		fy_generic_sequence_handle default_value)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_generic_sequence_handle_default(seqp, idx, default_value);
}

static inline fy_generic_mapping_handle
fy_generic_sequencep_get_generic_mapping_handle_default(const fy_generic_sequence *seqp, size_t idx,
		fy_generic_mapping_handle default_value)
{
	const fy_generic *vp = fy_generic_sequencep_get_itemp(seqp, idx);
	fy_generic_mapping_handle seqh;

	if (!vp)
		return default_value;

	seqh = fy_generic_mapping_to_handle(*vp);
	if (!seqh)
		return default_value;

	return seqh;
}

static inline fy_generic_mapping_handle
fy_generic_sequence_get_generic_mapping_handle_default(fy_generic seq, size_t idx,
		fy_generic_mapping_handle default_value)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_generic_mapping_handle_default(seqp, idx, default_value);
}

static inline fy_generic fy_generic_sequencep_get_generic_default(const fy_generic_sequence *seqp, size_t idx, fy_generic default_value)
{
	const fy_generic *vp = fy_generic_sequencep_get_itemp(seqp, idx);
	return vp ? *vp : default_value;
}

static inline fy_generic fy_generic_sequence_get_generic_default(fy_generic seq, size_t idx, fy_generic default_value)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_generic_default(seqp, idx, default_value);
}

static inline const fy_generic *fy_generic_sequence_get_string_itemp(fy_generic seq, size_t idx)
{
	const fy_generic *vp = fy_generic_sequence_get_itemp(seq, idx);
	return vp && fy_generic_is_direct_string(*vp) ? vp : NULL;
}

static inline const char *fy_generic_sequence_get_const_char_ptr_default(fy_generic seq, size_t idx, const char *default_value)
{
	const fy_generic *vp = fy_generic_sequence_get_string_itemp(seq, idx);
	return fy_genericp_get_const_char_ptr_default(vp, default_value);
}

static inline char *fy_generic_sequence_get_char_ptr_default(fy_generic seq, size_t idx, char *default_value)
{
	return (char *)fy_generic_sequence_get_const_char_ptr_default(seq, idx, default_value);
}

static inline fy_generic_sized_string fy_generic_sequence_get_szstr_default(fy_generic seq, size_t idx,
		fy_generic_sized_string default_value)
{
	struct fy_generic_sized_string ret = { .data = NULL, .size = 0 };
	const fy_generic *vp = fy_generic_sequence_get_string_itemp(seq, idx);

	if (!vp || !fy_generic_is_string(*vp))
		return ret;

	ret.data = fy_genericp_get_string_size_no_check(fy_genericp_indirect_get_valuep(vp), &ret.size);
	return ret;
}

static inline const fy_generic *fy_generic_sequence_get_sequence_itemp(fy_generic seq, size_t idx)
{
	const fy_generic *vp = fy_generic_sequence_get_itemp(seq, idx);
	return vp && fy_generic_is_direct_sequence(*vp) ? vp : NULL;
}

static inline const fy_generic *fy_generic_sequence_get_mapping_itemp(fy_generic seq, size_t idx)
{
	const fy_generic *vp = fy_generic_sequence_get_itemp(seq, idx);
	return vp && fy_generic_is_direct_mapping(*vp) ? vp : NULL;
}

static inline const fy_generic *fy_generic_sequence_get_alias_itemp(fy_generic seq, size_t idx)
{
	const fy_generic *vp = fy_generic_sequence_get_itemp(seq, idx);
	return vp && fy_generic_is_direct_alias(*vp) ? vp : NULL;
}

static inline fy_generic fy_generic_sequence_get_string_item(fy_generic seq, size_t idx)
{
	fy_generic v = fy_generic_sequence_get_item_generic(seq, idx);
	return fy_generic_is_string(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_sequence_get_sequence_item(fy_generic seq, size_t idx)
{
	fy_generic v = fy_generic_sequence_get_item_generic(seq, idx);
	return fy_generic_is_sequence(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_sequence_get_mapping_item(fy_generic seq, size_t idx)
{
	fy_generic v = fy_generic_sequence_get_item_generic(seq, idx);
	return fy_generic_is_mapping(v) ? v : fy_invalid;
}

static inline fy_generic fy_generic_sequence_get_alias_item(fy_generic seq, size_t idx)
{
	fy_generic v = fy_generic_sequence_get_item_generic(seq, idx);
	return fy_generic_is_alias(v) ? v : fy_invalid;
}

#define fy_generic_sequence_get_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_generic_sequence_get, default), \
	char *: fy_generic_sequence_get_char_ptr_default, \
	const char *: fy_generic_sequence_get_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_generic_sequence_get_generic_sequence_handle_default, \
	fy_generic_mapping_handle: fy_generic_sequence_get_generic_mapping_handle_default, \
	fy_generic_sized_string: fy_generic_sequence_get_szstr_default, \
	fy_generic: fy_generic_sequence_get_generic_default

#define fy_generic_sequence_get_default(_seq, _idx, _dv) \
	({ \
		typeof (1 ? (_dv) : (_dv))  __ret; \
		\
		__ret = _Generic((_dv), fy_generic_sequence_get_default_Generic_dispatch)((_seq), (_idx), (_dv)); \
		__ret; \
	})

#define fy_generic_sequence_get(_seq, _idx, _type) \
	(fy_generic_sequence_get_default((_seq), (_idx), fy_generic_get_type_default(_type)))

#define fy_generic_sequence_get_default_coerse(_seq, _idxv, _dv) \
	({ \
		typeof ((_dv) + 0) __dv = (_dv); \
		fy_generic __idxv = fy_to_generic(_idxv); \
		size_t __idxi = (size_t)fy_generic_get_unsigned_long_long_default(__idxv, (unsigned long long)-1); \
		fy_generic_sequence_get_default((_seq), __idxi, (_dv)); \
	})

#define fy_generic_sequence_get_coerse(_seq, _idxv, _type) \
	(fy_generic_sequence_get_defaul_coerse((_seq), (_idxv), fy_generic_get_type_default(_type)))

// map

static inline fy_generic fy_generic_mapping_get_generic_default(fy_generic map, fy_generic key, fy_generic default_value)
{
	const fy_generic *vp = fy_generic_mapping_get_valuep(map, key);
	return vp ? *vp : default_value;
}

static inline fy_generic_sequence_handle fy_generic_mapping_get_sequence_handle_default(fy_generic map, fy_generic key, fy_generic_sequence_handle default_value)
{
	const fy_generic *vp = fy_generic_mapping_get_valuep(map, key);
	fy_generic_sequence_handle seqh;

	if (!vp)
		return default_value;

	seqh = fy_generic_sequence_to_handle(*vp);
	if (!seqh)
		return default_value;

	return seqh;
}

static inline fy_generic_mapping_handle fy_generic_mapping_get_mapping_handle_default(fy_generic map, fy_generic key, fy_generic_mapping_handle default_value)
{
	const fy_generic *vp = fy_generic_mapping_get_valuep(map, key);
	fy_generic_mapping_handle maph;

	if (!vp)
		return default_value;

	maph = fy_generic_mapping_to_handle(*vp);
	if (!maph)
		return default_value;

	return maph;
}

static inline const fy_generic *fy_generic_mapping_get_string_valuep(fy_generic map, fy_generic key)
{
	const fy_generic *vp = fy_generic_mapping_get_valuep(map, key);
	return vp && fy_generic_is_direct_string(*vp) ? vp : NULL;
}

static inline const char *fy_generic_mapping_get_const_char_ptr_default(fy_generic map, fy_generic key, const char *default_value)
{
	const fy_generic *vp = fy_generic_mapping_get_string_valuep(map, key);
	return fy_genericp_get_const_char_ptr_default(vp, default_value);
}

static inline char *fy_generic_mapping_get_char_ptr_default(fy_generic map, fy_generic key, char *default_value)
{
	return (char *)fy_generic_mapping_get_const_char_ptr_default(map, key, default_value);
}

static inline fy_generic_sized_string fy_generic_mapping_get_szstr_default(fy_generic map, fy_generic key,
		fy_generic_sized_string default_value)
{
	struct fy_generic_sized_string ret = { .data = NULL, .size = 0 };
	const fy_generic *vp = fy_generic_mapping_get_string_valuep(map, key);

	if (!vp || !fy_generic_is_string(*vp))
		return ret;

	ret.data = fy_genericp_get_string_size_no_check(fy_genericp_indirect_get_valuep(vp), &ret.size);
	return ret;
}

static inline const fy_generic *fy_generic_mapping_get_mapping_valuep(fy_generic map, fy_generic key)
{
	const fy_generic *vp = fy_generic_mapping_get_valuep(map, key);
	return vp && fy_generic_is_direct_mapping(*vp) ? vp : NULL;
}

static inline const fy_generic *fy_generic_mapping_get_alias_valuep(fy_generic map, fy_generic key)
{
	const fy_generic *vp = fy_generic_mapping_get_valuep(map, key);
	return vp && fy_generic_is_direct_alias(*vp) ? vp : NULL;
}

#define fy_generic_mapping_get_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_generic_mapping_get, default), \
	char *: fy_generic_mapping_get_char_ptr_default, \
	const char *: fy_generic_mapping_get_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_generic_mapping_get_sequence_handle_default, \
	fy_generic_mapping_handle: fy_generic_mapping_get_mapping_handle_default, \
	fy_generic_sized_string: fy_generic_mapping_get_szstr_default, \
	fy_generic: fy_generic_mapping_get_generic_default

#define fy_generic_mapping_get_default(_map, _key, _dv) \
	({ \
		typeof (1 ? (_dv) : (_dv)) __ret; \
		fy_generic __key = fy_to_generic(_key); \
		\
		__ret = _Generic((_dv), fy_generic_mapping_get_default_Generic_dispatch)((_map), __key, (_dv)); \
		__ret; \
	})

#define fy_generic_mapping_get(_map, _key, _type) \
	(fy_generic_mapping_get_default((_map), (_key), fy_generic_get_type_default(_type)))

// final polymorphism is a go go

/* by definition the generic and the handles are the same size
 * when the type is a generic, that operation is an NOP,
 * when is is a handle we will never execute it, but the code
 * compiles
 */

/* when it's a generic */
static inline fy_generic fy_get_generic_generic(const void *p)
{
	const fy_generic *vp = p;
	return fy_generic_indirect_get_value(*vp);
}

static inline fy_generic fy_get_generic_seq_handle(const void *p)
{
	const fy_generic_sequence_handle *seqh = p;
	return (fy_generic){ .v = fy_generic_in_place_sequence_handle(*seqh) };
}

static inline fy_generic fy_get_generic_map_handle(const void *p)
{
	const fy_generic_mapping_handle *maph = p;
	return (fy_generic){ .v = fy_generic_in_place_mapping_handle(*maph) };
}

#define fy_get_default(_colv, _key, _dv) \
	({ \
		typeof (1 ? (_colv) : (_colv)) __colv = (_colv); \
		typeof (1 ? (_dv) : (_dv)) __dv = (_dv); \
		typeof (1 ? (_dv) : (_dv)) __ret; \
		fy_generic __colv2; \
		enum fy_generic_type __type; \
		\
		__colv2 = _Generic(__colv, \
			fy_generic: fy_get_generic_generic(&__colv), \
			fy_generic_sequence_handle: fy_get_generic_seq_handle(&__colv), \
			fy_generic_mapping_handle: fy_get_generic_map_handle(&__colv) ); \
		__type = _Generic(__colv, \
			fy_generic: fy_generic_get_direct_type(__colv2), \
			fy_generic_sequence_handle: FYGT_SEQUENCE, \
			fy_generic_mapping_handle: FYGT_MAPPING ); \
		switch (__type) { \
		case FYGT_MAPPING: \
			const fy_generic __key = fy_to_generic(_key); \
			__ret = _Generic(__dv, fy_generic_mapping_get_default_Generic_dispatch) \
				(__colv2, __key, __dv); \
			break; \
		case FYGT_SEQUENCE: \
			const size_t __index = fy_generic_get_default_coerse(_key, LLONG_MAX); \
			__ret = _Generic(__dv, fy_generic_sequence_get_default_Generic_dispatch) \
				(__colv2, __index, __dv); \
			break; \
		default: \
			__ret = __dv; \
			break; \
		} \
		__ret; \
	})

#define fy_get(_colv, _key, _type) \
	(fy_get_default((_colv), (_key), fy_generic_get_type_default(_type)))

/* when it's a generic */
static inline size_t fy_get_len_generic(const void *p)
{
	const fy_generic *vp = fy_genericp_indirect_get_valuep(p);
	enum fy_generic_type type;
	size_t len;

	type = fy_generic_get_direct_type(*vp);
	switch (type) {
	case FYGT_SEQUENCE:
	case FYGT_MAPPING:
		const void *colp = fy_generic_resolve_collection_ptr(*vp);
		if (!colp)
			return 0;	// empty seq or map
		return type == FYGT_SEQUENCE ? fy_generic_sequencep_get_item_count(colp) :
					       fy_generic_mappingp_get_pair_count(colp);
	case FYGT_STRING:
		(void)fy_genericp_get_string_size_no_check(vp, &len);
		return len;
	default:
		break;
	}
	return 0;
}

static inline size_t fy_get_len_seq_handle(const void *p)
{
	return fy_generic_sequencep_get_item_count(p);
}

static inline size_t fy_get_len_map_handle(const void *p)
{
	return fy_generic_mappingp_get_pair_count(p);
}

#define fy_len(_colv) \
	({ \
		typeof (1 ? (_colv) : (_colv)) __colv = (_colv); \
		_Generic(__colv, \
			fy_generic: fy_get_len_generic, \
			fy_generic_sequence_handle: fy_get_len_seq_handle, \
			fy_generic_mapping_handle: fy_get_len_map_handle \
			)(&__colv); \
	})

//////////////////////////////////////////////////////

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

fy_generic fy_gb_indirect_create(struct fy_generic_builder *gb, const fy_generic_indirect *gi);

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
	((fy_generic) { .v = fy_gb_sequence_create(gb, \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GBITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), gb, __VA_ARGS__)).v })

#define fy_gb_mapping(_gb, ...) \
	((fy_generic) { .v = fy_gb_mapping_create(gb, \
			FY_CPP_VA_COUNT(__VA_ARGS__) / 2, \
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
