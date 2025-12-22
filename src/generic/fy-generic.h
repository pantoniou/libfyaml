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
#include "fy-atomics.h"

#include <libfyaml.h>

#if defined(HAVE_STRICT_ALIASING) && HAVE_STRICT_ALIASING && defined(__GNUC__) && !defined(__clang__)
#define GCC_DISABLE_WSTRICT_ALIASING
#endif

/* DO NOT REORDER - we especially rely on INT, FLOAT and STRING being consecutive */
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

enum fy_generic_type_mask {
	FYGTM_INVALID		= FY_BIT(FYGT_INVALID),
	FYGTM_NULL		= FY_BIT(FYGT_NULL),
	FYGTM_BOOL		= FY_BIT(FYGT_BOOL),
	FYGTM_INT		= FY_BIT(FYGT_INT),
	FYGTM_FLOAT		= FY_BIT(FYGT_FLOAT),
	FYGTM_STRING		= FY_BIT(FYGT_STRING),
	FYGTM_SEQUENCE		= FY_BIT(FYGT_SEQUENCE),
	FYGTM_MAPPING		= FY_BIT(FYGT_MAPPING),
	FYGTM_INDIRECT		= FY_BIT(FYGT_INDIRECT),
	FYGTM_ALIAS		= FY_BIT(FYGT_ALIAS),
	FYGTM_COLLECTION	= (FYGTM_SEQUENCE | FYGTM_MAPPING), 
	FYGTM_SCALAR		= (FYGTM_NULL | FYGTM_BOOL | FYGTM_INT | FYGTM_FLOAT | FYGTM_STRING), 
	FYGTM_ANY		= (FYGTM_COLLECTION | FYGTM_SCALAR),
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

// DO NO REORDER THE VALUES - bithacks expect them as they are in order to work

#define FY_NULL_V		0
#define FY_SEQ_V		0
#define FY_MAP_V		8
#define FY_COLLECTION_MASK	(((fy_generic_value)1 << (FY_INPLACE_TYPE_SHIFT + 1)) - 1)

#define FY_BOOL_V		8
#define FY_BOOL_INPLACE_SHIFT	4

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

#define FY_ESCAPE_NULL		0
#define FY_ESCAPE_FALSE		1
#define FY_ESCAPE_TRUE		2
#define FY_ESCAPE_COUNT		3

#define FY_MAKE_ESCAPE(_v)		(((fy_generic_value)(_v) << FY_ESCAPE_SHIFT) | FY_ESCAPE_MARK)

#define fy_null_value			FY_MAKE_ESCAPE(FY_ESCAPE_NULL)
#define fy_false_value			FY_MAKE_ESCAPE(FY_ESCAPE_FALSE)
#define fy_true_value			FY_MAKE_ESCAPE(FY_ESCAPE_TRUE)
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
#define FY_MAX_ALIGNOF(_v, _min) ((size_t)(_Alignof(_v) > (_min) ? _Alignof(_v) : (_min)))
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
#define fy_seq_empty	((fy_generic){. v = fy_seq_empty_value })
#define fy_map_empty	((fy_generic){. v = fy_map_empty_value })

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

static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct(fy_generic v)
{
	return (v.v & FY_ESCAPE_MASK) != FY_INDIRECT_V;
}

static inline FY_ALWAYS_INLINE
bool fy_generic_is_indirect(fy_generic v)
{
	return !fy_generic_is_direct(v);
}

static inline FY_ALWAYS_INLINE
const void *fy_generic_resolve_ptr(fy_generic ptr)
{
	/* clear the bottom 3 bits (all pointers are 8 byte aligned) */
	/* note collections have the bit 3 cleared too, so it's 16 byte aligned */
	ptr.v &= ~(uintptr_t)FY_INPLACE_TYPE_MASK;
	return (const void *)ptr.v;
}

static inline FY_ALWAYS_INLINE
const void *fy_generic_resolve_collection_ptr(fy_generic ptr)
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

static inline enum fy_generic_type fy_generic_get_direct_type_table(fy_generic v)
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
	static const enum fy_generic_type escapes[FY_ESCAPE_COUNT] = {
		[FY_ESCAPE_NULL] = FYGT_NULL,
		[FY_ESCAPE_FALSE] = FYGT_BOOL,
		[FY_ESCAPE_TRUE] = FYGT_BOOL,
	};
	enum fy_generic_type type;
	unsigned int escape_code;

	type = table[v.v & 15];
	if (type != FYGT_INVALID)
		return type;

	escape_code = (unsigned int)(v.v >> FY_ESCAPE_SHIFT);
	return escape_code < ARRAY_SIZE(escapes) ? escapes[escape_code] : FYGT_INVALID;
}

/*
 * The type is encoded at the lower 4 bits
 *
 * First we have the collections and the indirect/escape codes
 * 0 -> seq, 8 -> mapping, 7 -> indirect, 15 -> escape codes
 *
 * Now we have to find the type of INT, FLOAT, STRING, they are are consecutive
 * mask out the high bit because it is used, type is now on low 3 bits
 *
 * we have to map: 1, 2 -> int 3, 4 -> float 5, 6 -> string
 * subtract 1: 0, 1 -> int 2, 3 -> float 4, 5 -> string
 * shift right: 0 -> int, 1 -> float, 2 -> string
 * add FYGT_INT and we're done
 */
static inline FY_ALWAYS_INLINE
enum fy_generic_type fy_generic_get_direct_type_bithack(fy_generic v)
{
	if (v.v == fy_invalid_value)
		return FYGT_INVALID;

	switch (v.v & 15) {
		case     0:
			return FYGT_SEQUENCE;
		case 8 | 0:
			return FYGT_MAPPING;
		case     7:
			return FYGT_INDIRECT;
		case 8 | 7:
			switch ((unsigned int)(v.v >> FY_ESCAPE_SHIFT)) {
			case FY_ESCAPE_NULL:
				return FYGT_NULL;
			case FY_ESCAPE_FALSE:
			case FY_ESCAPE_TRUE:
				return FYGT_BOOL;
			default:
				break;
			}
			return FYGT_INVALID;
		default:
			break;
	}

	return FYGT_INT + (((v.v & 7) - 1) >> 1);
}

#define fy_generic_get_direct_type(_v) \
	(fy_generic_get_direct_type_bithack((_v)))

static inline FY_ALWAYS_INLINE bool
fy_generic_is_in_place_normal(fy_generic v)
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

static inline FY_ALWAYS_INLINE
bool fy_generic_is_in_place_bithack(fy_generic v)
{
	fy_generic_value m;

	/* the direct values in place */
	switch (v.v) {
	case fy_invalid_value:
	case fy_true_value:
	case fy_false_value:
	case fy_null_value:
	case fy_seq_empty_value:
	case fy_map_empty_value:
		return true;
	}
	/* sequence, mapping or indirect */
	m = v.v & FY_INPLACE_TYPE_MASK;
	if (m == 0 || m == 7)
		return false;

	/* for int, float and string, the bit 0 is the inplace marker */
	return (m & 1);
}

static inline FY_ALWAYS_INLINE bool
fy_generic_is_in_place(fy_generic v)
{
	return fy_generic_is_in_place_bithack(v);
}

enum fy_generic_type fy_generic_get_type_indirect(fy_generic v);

static inline enum fy_generic_type fy_generic_get_type(fy_generic v)
{
	if (fy_generic_is_indirect(v))
		return fy_generic_get_type_indirect(v);
	return fy_generic_get_direct_type(v);
}

void fy_generic_indirect_get(fy_generic v, fy_generic_indirect *gi);

const fy_generic *fy_genericp_indirect_get_valuep_nocheck(const fy_generic *vp);
const fy_generic *fy_genericp_indirect_get_valuep(const fy_generic *vp);
fy_generic fy_generic_indirect_get_value_nocheck(const fy_generic v);
fy_generic fy_generic_indirect_get_value(const fy_generic v);

fy_generic fy_generic_indirect_get_anchor(fy_generic v);
fy_generic fy_generic_indirect_get_tag(fy_generic v);
fy_generic fy_generic_get_anchor(fy_generic v);
fy_generic fy_generic_get_tag(fy_generic v);

/*
 * Generic sequence and mapping must start with the count and the items following
 * DO NOT rearrange structure fields
 */
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

typedef struct fy_generic_collection {
	size_t count;	/* *2 for mapping */
	fy_generic items[];
} fy_generic_collection FY_GENERIC_CONTAINER_ALIGNMENT;

typedef struct fy_generic_sized_string {
	const char *data;
	size_t size;
} fy_generic_sized_string;

typedef struct fy_generic_decorated_int {
	/* must be first */
	union {
		signed long long sv;
		unsigned long long uv;
	};
	/* BUG: clang miscompiles this if it's a bool */
	unsigned long long is_unsigned : 1;	/* single bit, it is important for binary comparisons */
} fy_generic_decorated_int;

typedef struct fy_generic_iterator {
	size_t idx;
} fy_generic_iterator;

/* wrap into handles */
typedef const fy_generic_sequence *fy_generic_sequence_handle;
typedef const fy_generic_mapping *fy_generic_mapping_handle;
typedef const fy_generic_map_pair *fy_generic_map_pair_handle;

#define fy_seq_handle_null	((fy_generic_sequence_handle)NULL)
#define fy_map_handle_null	((fy_generic_mapping_handle)NULL)
#define fy_szstr_empty		((fy_generic_sized_string){ })
#define fy_dint_empty		((fy_generic_decorated_int){ })
#define fy_map_pair_invalid	((fy_generic_map_pair) { .key = fy_invalid, .value = fy_invalid })

static inline size_t fy_sequence_storage_size(size_t count)
{
	size_t size;

	if (FY_MUL_OVERFLOW(count, sizeof(fy_generic), &size) ||
	    FY_ADD_OVERFLOW(size, sizeof(struct fy_generic_sequence), &size))
		return SIZE_MAX;
	return size;
}

static inline size_t fy_mapping_storage_size(size_t count)
{
	size_t size;

	if (FY_MUL_OVERFLOW(count, sizeof(fy_generic_map_pair), &size) ||
	    FY_ADD_OVERFLOW(size, sizeof(struct fy_generic_mapping), &size))
		return SIZE_MAX;
	return size;
}

static inline size_t fy_collection_storage_size(bool is_map, size_t count)
{
	return !is_map ?
		fy_sequence_storage_size(count) :
		fy_mapping_storage_size(count);
}

struct fy_generic_builder;

#define fy_generic_typeof(_v) \
	__typeof__(_Generic((_v), \
		char *: ((char *)0), \
		const char *: ((const char *)0), \
		struct fy_generic_builder *: ((struct fy_generic_builder *)0), \
		default: _v))

static inline bool fy_generic_is_valid(const fy_generic v)
{
	return v.v != fy_invalid_value;
}

static inline bool fy_generic_is_invalid(const fy_generic v)
{
	return v.v == fy_invalid_value;
}

#define FY_GENERIC_IS_TEMPLATE_INLINE(_gtype) \
\
bool fy_generic_is_indirect_ ## _gtype ## _nocheck(const fy_generic v); \
bool fy_generic_is_indirect_ ## _gtype (const fy_generic v); \
\
static inline FY_ALWAYS_INLINE \
bool fy_generic_is_ ## _gtype (const fy_generic v) \
{ \
	if (fy_generic_is_direct(v)) \
		return fy_generic_is_direct_ ## _gtype (v); \
	return fy_generic_is_direct_ ## _gtype (fy_generic_indirect_get_value(v)); \
	if (fy_generic_is_direct_ ## _gtype (v)) \
	       return true; \
	if (!fy_generic_is_indirect(v)) \
		return false; \
	return fy_generic_is_indirect_ ## _gtype ## _nocheck(v); \
} \
\
struct fy_useless_struct_for_semicolon

#define FY_GENERIC_IS_TEMPLATE_NON_INLINE(_gtype) \
\
bool fy_generic_is_indirect_ ## _gtype ## _nocheck(const fy_generic v) \
{ \
	return fy_generic_is_direct_ ## _gtype (fy_generic_indirect_get_value(v)); \
} \
\
bool fy_generic_is_indirect_ ## _gtype (const fy_generic v) \
{ \
	if (!fy_generic_is_indirect(v)) \
		return false; \
	return fy_generic_is_direct_ ## _gtype (fy_generic_indirect_get_value(v)); \
} \
struct fy_useless_struct_for_semicolon

/* the base types that match the spec */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_null_type(const fy_generic v)
{
	return v.v == fy_null_value;
}

FY_GENERIC_IS_TEMPLATE_INLINE(null_type);

static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_bool_type(const fy_generic v)
{
	return v.v == fy_true_value || v.v == fy_false_value;
}

FY_GENERIC_IS_TEMPLATE_INLINE(bool_type);

static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_int_type(const fy_generic v)
{
	// fy_generic_value m = (v.v & FY_INPLACE_TYPE_MASK);
	// return m == FY_INT_INPLACE_V || m == FY_INT_OUTPLACE_V;
	// FY_INT_INPLACE_V = 3, FY_INT_OUTPLACE_V = 4
	return ((v.v & FY_INPLACE_TYPE_MASK) - FY_INT_INPLACE_V) <= 1;
}

static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_uint_type(const fy_generic v)
{
	return fy_generic_is_direct_int_type(v);
}

FY_GENERIC_IS_TEMPLATE_INLINE(int_type);
FY_GENERIC_IS_TEMPLATE_INLINE(uint_type);

static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_float_type(const fy_generic v)
{
	// fy_generic_value m = (v.v & FY_INPLACE_TYPE_MASK);
	// return m == FY_FLOAT_INPLACE_V || m == FY_FLOAT_OUTPLACE_V;
	return ((v.v & FY_INPLACE_TYPE_MASK) - FY_FLOAT_INPLACE_V) <= 1;
}

FY_GENERIC_IS_TEMPLATE_INLINE(float_type);

static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_string(const fy_generic v)
{
	// fy_generic_value m = (v.v & FY_INPLACE_TYPE_MASK);
	// return m == FY_STRING_INPLACE_V || m == FY_STRING_OUTPLACE_V;
	return ((v.v & FY_INPLACE_TYPE_MASK) - FY_STRING_INPLACE_V) <= 1;
}

static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_string_type(const fy_generic v)
{
	return fy_generic_is_direct_string(v);
}

FY_GENERIC_IS_TEMPLATE_INLINE(string);
FY_GENERIC_IS_TEMPLATE_INLINE(string_type);

static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_sequence(const fy_generic v)
{
	return (v.v & FY_COLLECTION_MASK) == 0;	/* sequence is 0 lower 4 bits */
}

static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_sequence_type(const fy_generic v)
{
	return fy_generic_is_direct_sequence(v);
}

FY_GENERIC_IS_TEMPLATE_INLINE(sequence);
FY_GENERIC_IS_TEMPLATE_INLINE(sequence_type);

static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_mapping(const fy_generic v)
{
	return (v.v & FY_COLLECTION_MASK) == 8;	/* sequence is 8 lower 4 bits */
}

static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_mapping_type(const fy_generic v)
{
	return fy_generic_is_direct_mapping(v);
}

FY_GENERIC_IS_TEMPLATE_INLINE(mapping);
FY_GENERIC_IS_TEMPLATE_INLINE(mapping_type);

static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_collection(const fy_generic v)
{
	return (v.v & FY_INPLACE_TYPE_MASK) == 0;	/* sequence is 0, mapping is 8 (3 lower bits 0) */
}

FY_GENERIC_IS_TEMPLATE_INLINE(collection);

static inline const fy_generic *
fy_generic_collectionp_get_items(const enum fy_generic_type type, const fy_generic_collection *colp, size_t *countp)
{
	assert(type == FYGT_SEQUENCE || type == FYGT_MAPPING);
	if (!colp || !colp->count) {
		*countp = 0;
		return NULL;
	}
	*countp = colp->count * (type == FYGT_MAPPING ? 2 : 1);
	return &colp->items[0];
}

static inline const fy_generic_collection *
fy_generic_get_direct_collection(fy_generic v, enum fy_generic_type *typep)
{
	/* collections (seq/map) */
	if (!fy_generic_is_direct_collection(v)) {
		*typep = FYGT_INVALID;
		return NULL;
	}
	*typep = fy_generic_is_direct_sequence(v) ? FYGT_SEQUENCE : FYGT_MAPPING;
	return fy_generic_resolve_collection_ptr(v);
}

static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_alias(const fy_generic v)
{
	return fy_generic_get_type(v) == FYGT_ALIAS;
}

FY_GENERIC_IS_TEMPLATE_INLINE(alias);

static inline void *fy_generic_get_null_type_no_check(fy_generic v)
{
	return NULL;
}

static inline fy_generic_value fy_generic_in_place_null_type(void *p)
{
	return p == NULL ? fy_null_value : fy_invalid_value;
}

static inline size_t fy_generic_out_of_place_size_null_type(void *v)
{
	return 0;
}

static inline fy_generic_value fy_generic_out_of_place_put_null_type(void *buf, void *v)
{
	return fy_null_value;
}

static inline bool fy_generic_get_bool_type_no_check(fy_generic v)
{
	return v.v == fy_true_value;
}

static inline fy_generic_value fy_generic_in_place_bool_type(_Bool v)
{
	return v ? fy_true_value : fy_false_value;
}

static inline size_t fy_generic_out_of_place_size_bool_type(bool v)
{
	return 0;
}

static inline fy_generic_value fy_generic_out_of_place_put_bool_type(void *buf, bool v)
{
	return v ? fy_true_value : fy_false_value;
}

static inline fy_generic_value fy_generic_in_place_int_type(const long long v)
{
	if (v >= FYGT_INT_INPLACE_MIN && v <= FYGT_INT_INPLACE_MAX)
		return (((fy_generic_value)(signed long long)v) << FY_INT_INPLACE_SHIFT) | FY_INT_INPLACE_V;
	return fy_invalid_value;
}

static inline fy_generic_value fy_generic_out_of_place_put_int_type(void *buf, const long long v)
{
	struct fy_generic_decorated_int *p = buf;

	assert(((uintptr_t)buf & FY_INPLACE_TYPE_MASK) == 0);
	memset(p, 0, sizeof(*p));
	p->sv = v;
	p->is_unsigned = false;
	return (fy_generic_value)buf | FY_INT_OUTPLACE_V;
}

static inline fy_generic_value fy_generic_in_place_uint_type(const unsigned long long v)
{
	if (v <= FYGT_INT_INPLACE_MAX)
		return (((fy_generic_value)v) << FY_INT_INPLACE_SHIFT) | FY_INT_INPLACE_V;
	return fy_invalid_value;
}

static inline fy_generic_value fy_generic_out_of_place_put_uint_type(void *buf, const unsigned long long v)
{
	struct fy_generic_decorated_int *p = buf;

	assert(((uintptr_t)buf & FY_INPLACE_TYPE_MASK) == 0);
	memset(p, 0, sizeof(*p));
	p->uv = v;
	p->is_unsigned = v > (unsigned long long)LLONG_MAX;
	return (fy_generic_value)buf | FY_INT_OUTPLACE_V;
}

#ifdef FYGT_GENERIC_64

static inline fy_generic_value fy_generic_in_place_float_type(const double v)
{
	if (!isnormal(v) || (float)v == v) {
		const union { float f; uint32_t f_bits; } u = { .f = (float)v };
		return ((fy_generic_value)u.f_bits << FY_FLOAT_INPLACE_SHIFT) | FY_FLOAT_INPLACE_V;
	}
	return fy_invalid_value;
}

#else

static inline fy_generic_value fy_generic_in_place_float_type(const double v)
{
	return fy_invalid_value;
}

#endif

static inline fy_generic_value fy_generic_out_of_place_put_float_type(void *buf, const double v)
{
	assert(((uintptr_t)buf & FY_INPLACE_TYPE_MASK) == 0);
	*(double *)buf = v;
	return (fy_generic_value)buf | FY_FLOAT_OUTPLACE_V;
}

static inline long long fy_generic_get_int_type_no_check(fy_generic v)
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

static inline size_t fy_generic_out_of_place_size_int_type(const long long v)
{
	return (v >= FYGT_INT_INPLACE_MIN && v <= FYGT_INT_INPLACE_MAX) ? 0 : sizeof(fy_generic_decorated_int);
}

static inline unsigned long long fy_generic_get_uint_type_no_check(fy_generic v)
{
	const unsigned long long *p;

	/* make sure you sign extend */
	if ((v.v & FY_INPLACE_TYPE_MASK) == FY_INT_INPLACE_V)
		return (unsigned long long)(v.v >> FY_INPLACE_TYPE_SHIFT);

	p = fy_generic_resolve_ptr(v);
	if (!p)
		return 0;
	return *p;
}

static inline size_t fy_generic_out_of_place_size_uint_type(const unsigned long long v)
{
	return v <= FYGT_INT_INPLACE_MAX ? 0 : sizeof(fy_generic_decorated_int);
}

#ifdef FYGT_GENERIC_64

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define FY_INPLACE_FLOAT_ADV	1
#else
#define FY_INPLACE_FLOAT_ADV	0
#endif

#endif

#ifdef FYGT_GENERIC_64

static inline double fy_generic_get_float_type_no_check(fy_generic v)
{
	const double *p;

	if ((v.v & FY_INPLACE_TYPE_MASK) == FY_FLOAT_INPLACE_V)
		return (double)(*((float *)&v.v + FY_INPLACE_FLOAT_ADV));

	/* the out of place values are always doubles */
	p = fy_generic_resolve_ptr(v);
	if (!p)
		return 0.0;
	return (double)*p;
}

static inline size_t fy_generic_out_of_place_size_float_type(const double v)
{
	return (!isnormal(v) || (float)v == v) ? 0 : sizeof(double);
}

#else
static inline double fy_generic_get_float_type_no_check(fy_generic v)
{
	const void *p;

	p = fy_generic_resolve_ptr(v);
	if (!p)
		return 0.0;

	/* always double */
	return *(double *)p;
}

static inline size_t fy_generic_out_of_place_size_double(const double v)
{
	return sizeof(double);
}

#endif

// sequence

const fy_generic_sequence *
fy_generic_sequence_resolve_outofplace(fy_generic seq);

static inline FY_ALWAYS_INLINE const fy_generic_sequence *
fy_generic_sequence_resolve(const fy_generic seq)
{
	if (fy_generic_is_direct_sequence(seq))
		return fy_generic_resolve_collection_ptr(seq);
	return fy_generic_sequence_resolve_outofplace(seq);
}

static inline FY_ALWAYS_INLINE fy_generic_sequence_handle
fy_generic_sequence_to_handle(const fy_generic seq)
{
	return fy_generic_sequence_resolve(seq);
}

static inline FY_ALWAYS_INLINE const fy_generic *
fy_generic_sequencep_items(const fy_generic_sequence *seqp)
{
	return seqp ? &seqp->items[0] : NULL;
}

static inline FY_ALWAYS_INLINE size_t
fy_generic_sequencep_get_item_count(const fy_generic_sequence *seqp)
{
	return seqp ? seqp->count : 0;
}

static inline FY_ALWAYS_INLINE size_t fy_generic_sequence_get_item_count(fy_generic seq)
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
	if (!seqp || idx >= seqp->count)
		return NULL;
	return &seqp->items[idx];
}

static inline const fy_generic *fy_generic_sequence_get_itemp(fy_generic seq, size_t idx)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_itemp(seqp, idx);
}

static inline fy_generic fy_generic_sequence_get_item_generic(fy_generic seq, size_t idx)
{
	const fy_generic *vp = fy_generic_sequence_get_itemp(seq, idx);
	if (!vp)
		return fy_invalid;
	return *vp;
}

// mapping
const fy_generic_mapping *
fy_generic_mapping_resolve_outofplace(fy_generic map);

static inline FY_ALWAYS_INLINE const fy_generic_mapping *
fy_generic_mapping_resolve(const fy_generic map)
{
	if (fy_generic_is_direct_mapping(map))
		return fy_generic_resolve_collection_ptr(map);
	return fy_generic_mapping_resolve_outofplace(map);
}

static inline FY_ALWAYS_INLINE fy_generic_mapping_handle
fy_generic_mapping_to_handle(const fy_generic map)
{
	return fy_generic_mapping_resolve(map);
}

static inline FY_ALWAYS_INLINE const fy_generic *
fy_generic_mappingp_items(const fy_generic_mapping *mapp)
{
	return mapp ? &mapp->pairs[0].items[0] : NULL;
}

static inline FY_ALWAYS_INLINE size_t
fy_generic_mappingp_get_pair_count(const fy_generic_mapping *mapp)
{
	return mapp ? mapp->count : 0;
}

static inline FY_ALWAYS_INLINE const fy_generic_map_pair *
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

static inline FY_ALWAYS_INLINE const fy_generic *
fy_generic_mapping_get_items(fy_generic map, size_t *item_countp)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);

	if (!mapp) {
		*item_countp = 0;
		return NULL;
	}

	*item_countp = mapp->count * 2;
	return &mapp->pairs[0].items[0];
}

int fy_generic_compare_out_of_place(fy_generic a, fy_generic b);

static inline int fy_generic_compare(fy_generic a, fy_generic b)
{
	enum fy_generic_type ta, tb;

	/* invalids are always non-matching */
	if (a.v == fy_invalid_value || b.v == fy_invalid_value)
		return -2;	/* -2 signal that invalid was found */

	/* equals? nice - should work for null, bool, in place int, float and strings  */
	/* also for anything that's a pointer */
	if (a.v == b.v)
		return 0;

	ta = fy_generic_get_type(a);
	tb = fy_generic_get_type(b);
	/* invalid types, or differing types do not match */
	if (ta != tb)
		return ta > tb ? 1 : -1;	/* but order according to types */

	return fy_generic_compare_out_of_place(a, b);
}

static inline const fy_generic *fy_generic_mappingp_get_at_keyp(const fy_generic_mapping *mapp, size_t idx)
{
	if (!mapp || idx >= mapp->count)
		return NULL;
	return &mapp->pairs[idx].key;
}

static inline const fy_generic *fy_generic_mapping_get_at_keyp(fy_generic map, size_t idx)
{
	return fy_generic_mappingp_get_at_keyp(fy_generic_mapping_resolve(map), idx);
}

static inline const fy_generic fy_generic_mappingp_get_at_key(const fy_generic_mapping *mapp, size_t idx)
{
	if (!mapp || idx >= mapp->count)
		return fy_invalid;
	return mapp->pairs[idx].key;
}

static inline const fy_generic fy_generic_mapping_get_at_key(fy_generic map, size_t idx)
{
	return fy_generic_mappingp_get_at_key(fy_generic_mapping_resolve(map), idx);
}

// XXX this can be considerably sped up
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

static inline const fy_generic *fy_generic_mappingp_get_at_valuep(const fy_generic_mapping *mapp, size_t idx)
{
	if (!mapp || idx >= mapp->count)
		return NULL;
	return &mapp->pairs[idx].value;
}

static inline const fy_generic *fy_generic_mapping_get_valuep_index(fy_generic map, fy_generic key, size_t *idxp)
{
	return fy_generic_mappingp_valuep_index(fy_generic_mapping_resolve(map), key, idxp);
}

static inline const fy_generic *fy_generic_mapping_get_valuep(fy_generic map, fy_generic key)
{
	return fy_generic_mapping_get_valuep_index(map, key, NULL);
}

static inline const fy_generic *fy_generic_mapping_get_at_valuep(fy_generic map, size_t idx)
{
	return fy_generic_mappingp_get_at_valuep(fy_generic_mapping_resolve(map), idx);
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

static inline const fy_generic fy_generic_mappingp_get_at_value(const fy_generic_mapping *mapp, size_t idx)
{
	if (!mapp || idx >= mapp->count)
		return fy_invalid;
	return mapp->pairs[idx].value;
}

static inline const fy_generic fy_generic_mapping_get_at_value(fy_generic map, size_t idx)
{
	return fy_generic_mappingp_get_at_value(fy_generic_mapping_resolve(map), idx);
}

static inline size_t fy_generic_mapping_get_pair_count(fy_generic map)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return mapp ? mapp->count : 0;
}

static inline const fy_generic *
fy_generic_collection_get_items(fy_generic v, size_t *countp)
{
	const fy_generic_collection *colp;
	enum fy_generic_type type;

	if (!fy_generic_is_direct(v))
		v = fy_generic_indirect_get_value(v);

	if (!fy_generic_is_direct_collection(v)) {
		*countp = 0;
		return NULL;
	}
	type = fy_generic_is_direct_sequence(v) ? FYGT_SEQUENCE : FYGT_MAPPING;
	colp = fy_generic_resolve_collection_ptr(v);
	return fy_generic_collectionp_get_items(type, colp, countp);
}

//
// template for repetitive definitions
//
// for FY_GENERIC_INT_LVAL_TEMPLATE(int, int, INT_MIN, INT_MAX, 0) ->
//	FY_GENERIC_LVAL_TEMPLATE(int, int, INT, long long, int_type, INT_MIN, INT_MAX, 0)
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
static inline fy_generic_value fy_generic_in_place_ ## _gtype ( const _ctype v) \
{ \
	return fy_generic_in_place_ ## _xgtype ( (_xctype)v ); \
} \
\
static inline size_t fy_generic_out_of_place_size_ ## _gtype ( const _ctype v) \
{ \
	return fy_generic_out_of_place_size_ ## _xgtype ( (_xctype)v ); \
} \
\
static inline fy_generic_value fy_generic_out_of_place_put_ ##_gtype (void *buf, const _ctype v) \
{ \
	return fy_generic_out_of_place_put_ ## _xgtype (buf, (_xctype)v ); \
} \
\
static inline _ctype fy_generic_cast_ ## _gtype ## _default(fy_generic v, _ctype default_value) \
{ \
	if (!fy_generic_is_ ## _xgtype (v)) \
		return default_value; \
	const _xctype xv = fy_generic_get_ ## _xgtype ## _no_check(v); \
	if (!fy_ ## _gtype ## _is_in_range(xv)) \
		return default_value; \
	return (_ctype)xv; \
} \
\
static inline _ctype fy_generic_cast_ ## _gtype (fy_generic v) \
{ \
	return fy_generic_cast_ ## _gtype ## _default(v, _default_v ); \
} \
\
static inline _ctype fy_genericp_cast_ ## _gtype ## _default(const fy_generic *vp, _ctype default_value) \
{ \
	return vp ? fy_generic_cast_ ## _gtype ## _default(*vp, default_value) : default_value; \
} \
\
static inline _ctype fy_genericp_cast_ ## _gtype (const fy_generic *vp) \
{ \
	return fy_genericp_cast_ ## _gtype ## _default(vp, _default_v ); \
} \
\
static inline const fy_generic *fy_generic_sequencep_get_ ## _gtype ## _itemp(const fy_generic_sequence *seqp, size_t idx) \
{ \
	const fy_generic *vp = fy_generic_sequencep_get_itemp(seqp, idx); \
	return vp && fy_generic_is_direct_ ## _gtype (*vp) ? vp : NULL; \
} \
\
static inline const fy_generic *fy_generic_sequence_get_ ## _gtype ## _itemp(fy_generic seq, size_t idx) \
{ \
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq); \
	return fy_generic_sequencep_get_ ## _gtype ## _itemp(seqp, idx); \
} \
\
static inline _ctype fy_generic_sequencep_get_ ## _gtype ## _default(const fy_generic_sequence *seqp, size_t idx, _ctype default_value) \
{ \
	const fy_generic *vp = fy_generic_sequencep_get_ ## _gtype ## _itemp(seqp, idx); \
	return fy_genericp_cast_  ## _gtype ## _default(vp, default_value); \
} \
\
static inline _ctype fy_generic_sequence_get_ ## _gtype ## _default(fy_generic seq, size_t idx, _ctype default_value) \
{ \
	const fy_generic *vp = fy_generic_sequence_get_ ## _gtype ## _itemp(seq, idx); \
	return fy_genericp_cast_  ## _gtype ## _default(vp, default_value); \
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
static inline _ctype fy_generic_mappingp_get_ ## _gtype ## _default(const fy_generic_mapping *mapp, fy_generic key, _ctype default_value) \
{ \
	const fy_generic *vp = fy_generic_mappingp_get_ ## _gtype ## _valuep(mapp, key); \
	return fy_genericp_cast_  ## _gtype ## _default(vp, default_value); \
} \
\
static inline _ctype fy_generic_mapping_get_ ## _gtype ## _default(fy_generic map, fy_generic key, _ctype default_value) \
{ \
	const fy_generic *vp = fy_generic_mapping_get_ ## _gtype ## _valuep(map, key); \
	return fy_genericp_cast_  ## _gtype ## _default(vp, default_value); \
} \
\
static inline const fy_generic *fy_generic_mappingp_get_at_ ## _gtype ## _valuep(const fy_generic_mapping *mapp, size_t idx) \
{ \
	const fy_generic *vp = fy_generic_mappingp_get_at_valuep(mapp, idx); \
	return vp && fy_generic_is_direct_ ## _gtype (*vp) ? vp : NULL; \
} \
\
static inline const fy_generic *fy_generic_mapping_get_at_ ## _gtype ## _valuep(fy_generic map, size_t idx) \
{ \
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map); \
	return fy_generic_mappingp_get_at_ ## _gtype ## _valuep(mapp, idx); \
} \
\
static inline _ctype fy_generic_mappingp_get_at_ ## _gtype ## _default(const fy_generic_mapping *mapp, size_t idx, _ctype default_value) \
{ \
	const fy_generic *vp = fy_generic_mappingp_get_at_ ## _gtype ## _valuep(mapp, idx); \
	return fy_genericp_cast_  ## _gtype ## _default(vp, default_value); \
} \
\
static inline _ctype fy_generic_mapping_get_at_ ## _gtype ## _default(fy_generic map, size_t idx, _ctype default_value) \
{ \
	const fy_generic *vp = fy_generic_mapping_get_at_ ## _gtype ## _valuep(map, idx); \
	return fy_genericp_cast_  ## _gtype ## _default(vp, default_value); \
} \
\
static inline const fy_generic *fy_generic_mappingp_get_at_ ## _gtype ## _keyp(const fy_generic_mapping *mapp, size_t idx) \
{ \
	const fy_generic *vp = fy_generic_mappingp_get_at_keyp(mapp, idx); \
	return vp && fy_generic_is_direct_ ## _gtype (*vp) ? vp : NULL; \
} \
\
static inline const fy_generic *fy_generic_mapping_get_at_ ## _gtype ## _keyp(fy_generic map, size_t idx) \
{ \
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map); \
	return fy_generic_mappingp_get_at_ ## _gtype ## _keyp(mapp, idx); \
} \
\
static inline const fy_generic *fy_generic_mapping_get_key_at_ ## _gtype ## _valuep(fy_generic map, size_t idx) \
{ \
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map); \
	return fy_generic_mappingp_get_at_ ## _gtype ## _keyp(mapp, idx); \
} \
\
static inline _ctype fy_generic_mappingp_get_key_at_ ## _gtype ## _default(const fy_generic_mapping *mapp, size_t idx, _ctype default_value) \
{ \
	const fy_generic *vp = fy_generic_mappingp_get_at_ ## _gtype ## _keyp(mapp, idx); \
	return fy_genericp_cast_  ## _gtype ## _default(vp, default_value); \
} \
\
static inline _ctype fy_generic_mapping_get_key_at_ ## _gtype ## _default(fy_generic map, size_t idx, _ctype default_value) \
{ \
	const fy_generic *vp = fy_generic_mapping_get_at_ ## _gtype ## _keyp(map, idx); \
	return fy_genericp_cast_  ## _gtype ## _default(vp, default_value); \
} \
struct fy_useless_struct_for_semicolon

static inline bool fy_null_is_in_range(const void *v)
{
	return v == NULL;
}

FY_GENERIC_LVAL_TEMPLATE(void *, null, NULL, void *, null_type, NULL, NULL, NULL);

static inline bool fy_bool_is_in_range(const bool v)
{
	return true;
}

FY_GENERIC_LVAL_TEMPLATE(bool, bool, BOOL, bool, bool_type, false, true, false);

#define FY_GENERIC_INT_LVAL_TEMPLATE(_ctype, _gtype, _xminv, _xmaxv, _defaultv) \
static inline bool fy_ ## _gtype ## _is_in_range(const long long v) \
{ \
	return v >= _xminv && v <= _xmaxv; \
} \
\
FY_GENERIC_LVAL_TEMPLATE(_ctype, _gtype, INT, long long, int_type, _xminv, _xmaxv, _defaultv)

#define FY_GENERIC_UINT_LVAL_TEMPLATE(_ctype, _gtype, _xminv, _xmaxv, _defaultv) \
static inline bool fy_ ## _gtype ## _is_in_range(const unsigned long long v) \
{ \
	return v <= _xmaxv; \
} \
\
FY_GENERIC_LVAL_TEMPLATE(_ctype, _gtype, INT, unsigned long long, uint_type, _xminv, _xmaxv, _defaultv)

#if CHAR_MIN < 0
FY_GENERIC_INT_LVAL_TEMPLATE(char, char, CHAR_MIN, CHAR_MAX, 0);
#else
FY_GENERIC_UINT_LVAL_TEMPLATE(char, char, 0, CHAR_MAX, 0);
#endif
FY_GENERIC_INT_LVAL_TEMPLATE(signed char, signed_char, SCHAR_MIN, SCHAR_MAX, 0);
FY_GENERIC_UINT_LVAL_TEMPLATE(unsigned char, unsigned_char, 0, UCHAR_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(short, short, SHRT_MIN, SHRT_MAX, 0);
FY_GENERIC_UINT_LVAL_TEMPLATE(unsigned short, unsigned_short, 0, USHRT_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(signed short, signed_short, SHRT_MIN, SHRT_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(int, int, INT_MIN, INT_MAX, 0);
FY_GENERIC_UINT_LVAL_TEMPLATE(unsigned int, unsigned_int, 0, UINT_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(signed int, signed_int, INT_MIN, INT_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(long, long, LONG_MIN, LONG_MAX, 0);
FY_GENERIC_UINT_LVAL_TEMPLATE(unsigned long, unsigned_long, 0, ULONG_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(signed long, signed_long, LONG_MIN, LONG_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(long long, long_long, LLONG_MIN, LLONG_MAX, 0);
FY_GENERIC_UINT_LVAL_TEMPLATE(unsigned long long, unsigned_long_long, 0, ULLONG_MAX, 0);
FY_GENERIC_INT_LVAL_TEMPLATE(signed long long, signed_long_long, LLONG_MIN, LLONG_MAX, 0);

#define FY_GENERIC_FLOAT_LVAL_TEMPLATE(_ctype, _gtype, _xminv, _xmaxv, _defaultv) \
static inline bool fy_ ## _gtype ## _is_in_range(const double v) \
{ \
	if (isnormal(v)) \
		return v >= _xminv && v <= _xmaxv; \
	return true; \
} \
\
FY_GENERIC_LVAL_TEMPLATE(_ctype, _gtype, FLOAT, double, float_type, _xminv, _xmaxv, _defaultv)

FY_GENERIC_FLOAT_LVAL_TEMPLATE(float, float, -FLT_MAX, FLT_MAX, 0.0);
FY_GENERIC_FLOAT_LVAL_TEMPLATE(double, double, -DBL_MAX, DBL_MAX, 0.0);

#define FY_GENERIC_SCALAR_DISPATCH(_base, _ctype, _gtype) \
	_ctype: _base ## _ ## _gtype

#define FY_GENERIC_ALL_SCALARS_DISPATCH(_base) \
	FY_GENERIC_SCALAR_DISPATCH(_base, void *, null), \
	FY_GENERIC_SCALAR_DISPATCH(_base, _Bool, bool), \
	FY_GENERIC_SCALAR_DISPATCH(_base, char, char), \
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
	FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, char, char), \
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
#define fy_generic_get_string_size_alloca(_v, _lenp) 						\
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

#define fy_generic_get_string_default_alloca(_v, _default_v)					\
	({											\
		const char *__ret;								\
		size_t __len;									\
		__ret = fy_generic_get_string_size_alloca((_v), &__len);			\
		if (!__ret)									\
			__ret = (_default_v);							\
		__ret;										\
	})

#define fy_generic_get_string_alloca(_v) (fy_generic_get_string_default_alloca((_v), ""))

static inline const char *fy_genericp_get_const_char_ptr_default(const fy_generic *vp, const char *default_value)
{
	if (!vp)
		return default_value;

	if (!fy_generic_is_direct(*vp))
		vp = fy_genericp_indirect_get_valuep(vp);

	if (!vp || !fy_generic_is_direct_string(*vp))
		return default_value;

	return fy_genericp_get_string_no_check(vp);
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

#define fy_generic_get_alias_alloca(_v) \
	({ \
		fy_generic __va = fy_generic_get_anchor((_v)); \
		fy_generic_get_string_alloca(__va); \
	})

#define fy_bool(_v) \
	((bool)(_v) ? fy_true : fy_false)

#define fy_local_bool(_v) \
	(fy_bool((_v)))

#define fy_int_is_unsigned(_v) 									\
	(_Generic(_v, 										\
		unsigned long long: ((unsigned long long)_v > (unsigned long long)LLONG_MAX),	\
		default: false))

#define fy_int_alloca(_v) 									\
	({											\
		fy_generic_typeof(_v) __v = (_v);						\
		fy_generic_decorated_int *__vp;							\
		fy_generic_value _r;								\
												\
		if (__v >= FYGT_INT_INPLACE_MIN && __v <= FYGT_INT_INPLACE_MAX)			\
			_r = (((fy_generic_value)(signed long long)__v) << FY_INT_INPLACE_SHIFT) \
							| FY_INT_INPLACE_V;			\
		else {										\
			__vp = fy_alloca_align(sizeof(*__vp), FY_GENERIC_SCALAR_ALIGN);		\
			memset(__vp, 0, sizeof(*__vp));						\
			__vp->sv = __v;								\
			__vp->is_unsigned = fy_int_is_unsigned(__v);				\
			_r = (fy_generic_value)__vp | FY_INT_OUTPLACE_V;			\
		}										\
		_r;										\
	})

#define fy_ensure_const(_v) \
	(__builtin_constant_p(_v) ? (_v) : 0)

/* note the builtin_constant_p guard; this makes the fy_int() macro work */
#define fy_int_const(_v)									\
	({											\
		fy_generic_value _r;								\
												\
		if ((_v) >= FYGT_INT_INPLACE_MIN && (_v) <= FYGT_INT_INPLACE_MAX)		\
			_r = (fy_generic_value)((((unsigned long long)(signed long long)(_v))	\
						<< FY_INT_INPLACE_SHIFT) | FY_INT_INPLACE_V);	\
		else {										\
			static const fy_generic_decorated_int _vv FY_INT_ALIGNMENT = { 		\
				.sv = (signed long long)fy_ensure_const(_v),			\
				.is_unsigned = fy_int_is_unsigned(_v),				\
			};									\
			assert(((uintptr_t)&_vv & FY_INPLACE_TYPE_MASK) == 0);			\
			_r = (fy_generic_value)&_vv | FY_INT_OUTPLACE_V;			\
		}										\
		_r;										\
	})

#define fy_local_int(_v) \
	((fy_generic){ .v = (__builtin_constant_p(_v) ? fy_int_const(_v) : fy_int_alloca(_v)) })

#define fy_int(_v) \
	fy_local_int((_v))

static inline fy_generic_value fy_generic_in_place_default(...)
{
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

static inline fy_generic_value fy_generic_in_place_const_dintp(const fy_generic_decorated_int *dintp)
{
	if (!dintp)
		return fy_invalid_value;

	return !dintp->is_unsigned ? fy_generic_in_place_int_type(dintp->sv) : fy_generic_in_place_int_type(dintp->uv);
}

static inline fy_generic_value fy_generic_in_place_dint(const fy_generic_decorated_int dint)
{
	return fy_generic_in_place_const_dintp(&dint);
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

static inline fy_generic_value fy_generic_in_place_generic_builderp(struct fy_generic_builder *gb)
{
	return fy_invalid_value;
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
	const fy_generic_decorated_int *: fy_generic_in_place_const_dintp, \
	fy_generic_decorated_int *: fy_generic_in_place_const_dintp, \
	fy_generic_decorated_int: fy_generic_in_place_dint, \
	struct fy_generic_builder *: fy_generic_in_place_generic_builderp, \
	default: fy_generic_in_place_default

#define fy_to_generic_inplace(_v) ( _Generic((_v), fy_to_generic_inplace_Generic_dispatch)(_v))

static inline size_t fy_generic_out_of_place_size_char_ptr(const char *p)
{
	if (!p)
		return 0;

	return FYGT_SIZE_ENCODING_MAX + strlen(p) + 1;
}

static inline size_t fy_generic_out_of_place_size_generic(fy_generic v)
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

static inline size_t fy_generic_out_of_place_size_const_dintp(const fy_generic_decorated_int *dintp)
{
	if (!dintp)
		return 0;

	return !dintp->is_unsigned ? fy_generic_out_of_place_size_long_long(dintp->sv) :
				     fy_generic_out_of_place_size_unsigned_long_long(dintp->uv);
}

static inline size_t fy_generic_out_of_place_size_dint(fy_generic_decorated_int dint)
{
	return fy_generic_out_of_place_size_const_dintp(&dint);
}

static inline size_t fy_generic_out_of_place_size_sequence_handle(fy_generic_sequence_handle seqh)
{
	return 0;	// TODO calculate
}

static inline size_t fy_generic_out_of_place_size_mapping_handle(fy_generic_mapping_handle maph)
{
	return 0;	// TODO calculate
}

static inline size_t fy_generic_out_of_place_size_generic_builderp(struct fy_generic_builder *gb)
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
	fy_generic_sized_string: fy_generic_out_of_place_size_szstr, \
	const fy_generic_decorated_int *: fy_generic_out_of_place_size_const_dintp, \
	fy_generic_decorated_int *: fy_generic_out_of_place_size_const_dintp, \
	fy_generic_decorated_int: fy_generic_out_of_place_size_dint, \
	struct fy_generic_builder *: fy_generic_out_of_place_size_generic_builderp

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

static inline fy_generic_value fy_generic_out_of_place_put_const_dintp(void *buf, const fy_generic_decorated_int *dintp)
{
	if (!dintp)
		return fy_invalid_value;

	return !dintp->is_unsigned ? fy_generic_out_of_place_put_long_long(buf, dintp->sv) :
				     fy_generic_out_of_place_put_unsigned_long_long(buf, dintp->uv);
}

static inline fy_generic_value fy_generic_out_of_place_put_dint(void *buf, fy_generic_decorated_int dint)
{
	return fy_generic_out_of_place_put_const_dintp(buf, &dint);
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

static inline fy_generic_value fy_generic_out_of_place_put_generic_builderp(void *buf, struct fy_generic_builder *gb)
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
	fy_generic_sized_string: fy_generic_out_of_place_put_szstr, \
	const fy_generic_decorated_int *: fy_generic_out_of_place_put_const_dintp, \
	fy_generic_decorated_int *: fy_generic_out_of_place_put_const_dintp, \
	fy_generic_decorated_int: fy_generic_out_of_place_put_szstr, \
	struct fy_generic_builder *: fy_generic_out_of_place_put_generic_builderp

#define fy_to_generic_outofplace_put(_vp, _v) \
	(_Generic((_v), fy_to_generic_outofplace_put_Generic_dispatch)((_vp), (_v)))

#define fy_local_to_generic_value(_v) \
	({	\
		fy_generic_typeof(_v) __v = (_v); \
		fy_generic_value __r; \
		\
		__r = fy_to_generic_inplace(__v); \
		if (__r == fy_invalid_value) { \
			size_t __outofplace_size = fy_to_generic_outofplace_size(__v); \
			if (__outofplace_size > 0) { \
				fy_generic *__vp = __outofplace_size ? fy_alloca_align(__outofplace_size, FY_GENERIC_CONTAINER_ALIGN) : NULL; \
				__r = fy_to_generic_outofplace_put(__vp, __v); \
			} \
		} \
		__r; \
	})

#define fy_local_to_generic(_v) \
	((fy_generic) { .v = fy_local_to_generic_value(_v) })

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

#define fy_local_float(_v) \
	((fy_generic){ .v = __builtin_constant_p(_v) ? fy_float_const(_v) : fy_float_alloca(_v) })

#define fy_float(_v) \
	((fy_local_float(_v)))

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

#define fy_string_const(_v) fy_string_size_const((_v), (sizeof(_v) - 1))

#define fy_local_string_size(_v, _len) \
	((fy_generic){ .v = __builtin_constant_p(_v) ? fy_string_size_const((_v), (_len)) : fy_string_size_alloca((_v), (_len)) })

#define fy_local_string(_v) \
	((fy_generic){ .v = __builtin_constant_p(_v) ? fy_string_const(_v) : fy_string_alloca(_v) })

#define fy_string_size(_v, _len) \
	(fy_local_string_size((_v), (_len)))

#define fy_string(_v) \
	(fy_local_string((_v)))

#define fy_stringf_value(_fmt, ...) \
	({ \
		char *_buf = fy_sprintfa((_fmt) __VA_OPT__(,) __VA_ARGS__); \
		fy_string_alloca(_buf); \
	})

#define fy_stringf(_fmt, ...) \
	((fy_generic){ .v = fy_stringf_value((_fmt) __VA_OPT__(,) __VA_ARGS__) })

#define fy_sequence_alloca(_count, _items) 						\
	({										\
		fy_generic_sequence *__vp;						\
		size_t __count = (_count);						\
		size_t __size = fy_sequence_storage_size(__count);			\
											\
		__vp = fy_alloca_align(__size, FY_GENERIC_CONTAINER_ALIGN);		\
		__vp->count = __count;							\
		memcpy(__vp->items, (_items), __count * sizeof(fy_generic)); 		\
		(fy_generic_value)((uintptr_t)__vp | FY_SEQ_V);				\
	})

#define fy_local_sequence_create_value(_count, _items) \
	({ \
		size_t __count = (_count); \
		__count ? fy_sequence_alloca((_count), (_items)) : fy_seq_empty_value; \
	})

#define fy_local_sequence_create(_count, _items) \
	((fy_generic) { .v = fy_local_sequence_create_value((_count), (_items)) })

#define _FY_CPP_GITEM_ONE(arg) fy_to_generic(arg)
#define _FY_CPP_GITEM_LATER_ARG(arg) , _FY_CPP_GITEM_ONE(arg)
#define _FY_CPP_GITEM_LIST(...) FY_CPP_MAP(_FY_CPP_GITEM_LATER_ARG, __VA_ARGS__)

#define _FY_CPP_VA_GITEMS(...)          \
    _FY_CPP_GITEM_ONE(FY_CPP_FIRST(__VA_ARGS__)) \
    _FY_CPP_GITEM_LIST(FY_CPP_REST(__VA_ARGS__))

#define FY_CPP_VA_GITEMS(_count, ...) \
	((fy_generic [(_count)]) { __VA_OPT__(_FY_CPP_VA_GITEMS(__VA_ARGS__)) })

#define fy_local_sequence_value(...) \
	fy_local_sequence_create_value( \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS( \
				FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__))

#define fy_local_sequence(...) \
	((fy_generic) { .v = fy_local_sequence_value(__VA_ARGS__) })

#define fy_mapping_alloca(_count, _pairs) 						\
	({										\
		fy_generic_mapping *__vp;						\
		size_t __count = (_count);						\
		size_t __size = fy_mapping_storage_size(__count);			\
											\
		__vp = fy_alloca_align(__size, FY_GENERIC_CONTAINER_ALIGN);		\
		__vp->count = __count;							\
		memcpy(__vp->pairs, (_pairs), 2 * __count * sizeof(fy_generic)); 	\
		(fy_generic_value)((uintptr_t)__vp | FY_MAP_V);				\
	})

#define fy_local_mapping_create_value(_count, _pairs) \
	({ \
		size_t __count = (_count); \
		__count ? fy_mapping_alloca((_count), (_pairs)) : fy_map_empty_value; \
	})

#define fy_local_mapping_create(_count, _items) \
	((fy_generic) { .v = fy_local_mapping_create_value((_count), (_items)) })

#define fy_local_mapping_value(...) \
	fy_local_mapping_create_value( \
		FY_CPP_VA_COUNT(__VA_ARGS__) / 2, \
		FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__))

#define fy_local_mapping(...) \
	((fy_generic) { .v = fy_local_mapping_value(__VA_ARGS__) })

// indirect

#define fy_indirect_alloca(_v, _anchor, _tag)						\
	({										\
		fy_generic_value __v = (_v);						\
		fy_generic_value __anchor = (_anchor), __tag = (_tag), *__vp;		\
		int __i = 1, __count;							\
											\
		__count = (1 + (__v      != fy_invalid_value) + 			\
			       (__anchor != fy_invalid_value) + 			\
			       (__tag    != fy_invalid_value));				\
		__vp = fy_alloca_align(__count * sizeof(*__vp), 			\
					FY_GENERIC_CONTAINER_ALIGN);			\
		__vp[0] = (__v      != fy_invalid_value ? FYGIF_VALUE  : 0) |		\
			  (__anchor != fy_invalid_value ? FYGIF_ANCHOR : 0) |		\
			  (__tag    != fy_invalid_value ? FYGIF_TAG    : 0);		\
		if (__v != fy_invalid_value) 						\
			 __vp[__i++] = __v;						\
		if (__anchor != fy_invalid_value) 					\
			 __vp[__i++] = __anchor;					\
		if (_tag != fy_invalid_value) 						\
			 __vp[__i++] = __tag;						\
		(fy_generic_value)__vp | FY_INDIRECT_V;					\
	})

#define fy_indirect(_v, _anchor, _tag)	\
	((fy_generic){ .v = fy_indirect_alloca((_v).v, (_anchor).v, (_tag).v) })

static inline fy_generic fy_generic_cast_generic_default(fy_generic v, fy_generic default_value)
{
	if (fy_generic_is_valid(v))
		return v;
	return default_value;
}

static inline fy_generic fy_genericp_cast_generic_default(const fy_generic *vp, fy_generic default_value)
{
	if (!vp)
		return default_value;
	return fy_generic_cast_generic_default(*vp, default_value);
}

static inline fy_generic_sequence_handle fy_generic_cast_sequence_handle_default(fy_generic v, fy_generic_sequence_handle default_value)
{
	const fy_generic_sequence_handle seqh = fy_generic_sequence_to_handle(v);
	return seqh ? seqh : default_value;
}

static inline fy_generic_sequence_handle fy_genericp_cast_sequence_handle_default(const fy_generic *vp, fy_generic_sequence_handle default_value)
{
	if (!vp)
		return default_value;
	return fy_generic_cast_sequence_handle_default(*vp, default_value);
}

static inline fy_generic_mapping_handle fy_generic_cast_mapping_handle_default(fy_generic v, fy_generic_mapping_handle default_value)
{
	const fy_generic_mapping_handle maph = fy_generic_mapping_to_handle(v);
	return maph ? maph : default_value;
}

static inline fy_generic_mapping_handle fy_genericp_cast_mapping_handle_default(const fy_generic *vp, fy_generic_mapping_handle default_value)
{
	if (!vp)
		return default_value;
	return fy_generic_cast_mapping_handle_default(*vp, default_value);
}

/* special... handling for in place strings */
static inline const char *fy_generic_cast_const_char_ptr_default(fy_generic v, const char *default_value)
{
	const fy_generic *vp;

	if (fy_generic_is_direct_string(v)) {
		/* out of place is easier actually */
		if ((v.v & FY_INPLACE_TYPE_MASK) != FY_STRING_INPLACE_V)
			return (const char *)fy_skip_size_nocheck(fy_generic_resolve_ptr(v));

		return NULL;
	}

	/* direct is stable we can return a point anywhere */
	vp = fy_genericp_indirect_get_valuep(&v);
	if (!vp || !fy_generic_is_direct_string(*vp))
		return default_value;

	return fy_genericp_get_string_no_check(vp);
}

static inline char *fy_generic_cast_char_ptr_default(fy_generic v, char *default_value)
{
	return (char *)fy_generic_cast_const_char_ptr_default(v, default_value);
}

static inline fy_generic_sized_string fy_generic_cast_sized_string_default(fy_generic v, const fy_generic_sized_string default_value)
{
	fy_generic_sized_string szstr;
	const fy_generic *vp;

	vp = NULL;
	if (fy_generic_is_direct_string(v)) {
		/* out of place is easier actually */
		if ((v.v & FY_INPLACE_TYPE_MASK) != FY_STRING_INPLACE_V)
			vp = &v;
	} else {
		vp = fy_genericp_indirect_get_valuep(&v);
		if (vp && !fy_generic_is_direct_string(*vp))
			vp = NULL;
	}

	if (vp)
		szstr.data = fy_genericp_get_string_size_no_check(vp, &szstr.size);
	else
		memset(&szstr, 0, sizeof(szstr));
	return szstr;
}

static inline const char *fy_genericp_cast_const_char_ptr_default(const fy_generic *vp, const char *default_value)
{
	if (!vp)
		return default_value;

	if (!fy_generic_is_direct(*vp))
		vp = fy_genericp_indirect_get_valuep(vp);
	if (!vp || !fy_generic_is_direct_string(*vp))
		return default_value;

	return fy_genericp_get_string_no_check(vp);
}

static inline char *fy_genericp_cast_char_ptr_default(const fy_generic *vp, char *default_value)
{
	return (char *)fy_genericp_cast_const_char_ptr_default(vp, default_value);
}

static inline fy_generic_sized_string fy_genericp_cast_sized_string_default(const fy_generic *vp, const fy_generic_sized_string default_value)
{
	fy_generic_sized_string ret;

	if (!vp)
		return default_value;

	if (!fy_generic_is_direct(*vp))
		vp = fy_genericp_indirect_get_valuep(vp);
	if (!vp || !fy_generic_is_direct_string(*vp))
		return default_value;

	ret.data = fy_genericp_get_string_size_no_check(vp, &ret.size);
	return ret;
}

static inline fy_generic_decorated_int fy_generic_cast_decorated_int_default(fy_generic v, const fy_generic_decorated_int default_value)
{
	const fy_generic_decorated_int *p;
	fy_generic_decorated_int dv;

	if (!fy_generic_is_int_type(v))
		return default_value;

	memset(&dv, 0, sizeof(dv));

	v = fy_generic_indirect_get_value(v);
	// inplace? always signed */
	if ((v.v & FY_INPLACE_TYPE_MASK) == FY_INT_INPLACE_V) {
		dv.sv = (fy_generic_value_signed)((v.v >> FY_INPLACE_TYPE_SHIFT) <<
			       (FYGT_GENERIC_BITS - FYGT_INT_INPLACE_BITS)) >>
				       (FYGT_GENERIC_BITS - FYGT_INT_INPLACE_BITS);
		dv.is_unsigned = false;	/* in place is always unsigned */
		return dv;
	}

	p = fy_generic_resolve_ptr(v);
	if (!p)
		return default_value;

	dv.sv = p->sv;
	dv.is_unsigned = p->is_unsigned;
	return dv;
}

static inline fy_generic_decorated_int fy_genericp_cast_decorated_int_default(const fy_generic *vp, const fy_generic_decorated_int default_value)
{
	if (!vp)
		return default_value;
	return fy_generic_cast_decorated_int_default(*vp, default_value);
}

static inline size_t fy_generic_cast_const_char_ptr_default_alloca(fy_generic v)
{
	/* only do the alloca dance for in place strings */
	return ((v.v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V) ? sizeof(fy_generic) : 0;
}

static inline size_t fy_generic_cast_sized_string_default_alloca(fy_generic v)
{
	return fy_generic_cast_const_char_ptr_default_alloca(v);
}

static inline size_t fy_generic_cast_decorated_int_default_alloca(fy_generic v)
{
	return 0;
}

/* never allocas for anything else */
static inline size_t fy_generic_cast_default_should_alloca_never(fy_generic v)
{
	return 0;
}

static inline void fy_generic_cast_const_char_ptr_default_final(fy_generic v,
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

static inline void fy_generic_cast_sized_string_default_final(fy_generic v,
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

static inline void fy_generic_cast_decorated_int_default_final(fy_generic v,
		void *p, size_t size,
		fy_generic_decorated_int default_value, fy_generic_decorated_int *store_value)
{
	return;
}

static inline void fy_generic_cast_char_ptr_default_final(fy_generic v,
		void *p, size_t size,
		char *default_value, char **store_value)
{
	return fy_generic_cast_const_char_ptr_default_final(v, p, size, default_value,
			(const char **)store_value);
}

static inline void fy_generic_cast_default_final_never(fy_generic v,
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
	const fy_generic_decorated_int *: (const fy_generic_decorated_int *)NULL, \
	fy_generic_decorated_int *: (fy_generic_decorated_int *)NULL, \
	fy_generic_decorated_int: ((fy_generic_decorated_int){ }), \
	fy_generic: fy_null, \
	fy_generic_map_pair: fy_map_pair_invalid, \
	const fy_generic_map_pair *: ((fy_generic_map_pair *)NULL), \
	fy_generic_map_pair *: ((fy_generic_map_pair *)NULL)

#define fy_generic_cast_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_generic_cast, default), \
	char *: fy_generic_cast_char_ptr_default, \
	const char *: fy_generic_cast_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_generic_cast_sequence_handle_default, \
	fy_generic_mapping_handle: fy_generic_cast_mapping_handle_default, \
	fy_generic_sized_string: fy_generic_cast_sized_string_default, \
	fy_generic_decorated_int: fy_generic_cast_decorated_int_default, \
	fy_generic: fy_generic_cast_generic_default

#define fy_generic_cast_default_alloca_Generic_dispatch \
	char *: fy_generic_cast_const_char_ptr_default_alloca, \
	const char *: fy_generic_cast_const_char_ptr_default_alloca, \
	fy_generic_sized_string: fy_generic_cast_sized_string_default_alloca, \
	default: fy_generic_cast_default_should_alloca_never

#define fy_generic_cast_default_final_Generic_dispatch \
	char *: fy_generic_cast_char_ptr_default_final, \
	const char *: fy_generic_cast_const_char_ptr_default_final, \
	fy_generic_sized_string: fy_generic_cast_sized_string_default_final, \
	default: fy_generic_cast_default_final_never

#define fy_generic_cast_default(_v, _dv) \
	({ \
		fy_generic __v = fy_to_generic(_v); \
		fy_generic_typeof(_dv) __dv = (_dv); \
		fy_generic_typeof(_dv) __ret; \
		size_t __size; \
		void *__p; \
		\
		__ret = _Generic(__dv, fy_generic_cast_default_Generic_dispatch)(__v, __dv); \
		__size = _Generic(__dv, fy_generic_cast_default_alloca_Generic_dispatch)(__v); \
		if (__size) { \
			__p = fy_alloca_align(__size, FY_GENERIC_CONTAINER_ALIGN); \
			_Generic(__dv, fy_generic_cast_default_final_Generic_dispatch) \
				(__v, __p, __size, __dv, &__ret); \
		} \
		__ret; \
	})

#define fy_generic_get_type_default(_type) \
	({ \
		_type __tmp = _Generic(__tmp, fy_generic_get_type_default_Generic_dispatch); \
		__tmp; \
	})

/* when we have a pointer we can return to inplace strings */
#define fy_generic_cast_typed(_v, _type) \
	(fy_generic_cast_default((_v), fy_generic_get_type_default(_type)))

#define fy_genericp_cast_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_genericp_cast, default), \
	char *: fy_genericp_cast_char_ptr_default, \
	const char *: fy_genericp_cast_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_genericp_cast_sequence_handle_default, \
	fy_generic_mapping_handle: fy_genericp_cast_mapping_handle_default, \
	fy_generic_sized_string: fy_genericp_cast_sized_string_default, \
	fy_generic_decorated_int: fy_genericp_cast_decorated_int_default, \
	fy_generic: fy_genericp_cast_generic_default

#define fy_genericp_cast_default(_vp, _dv) \
	(_Generic((_dv), fy_genericp_cast_default_Generic_dispatch)((_vp), (_dv)))

#define fy_genericp_cast_typed(_vp, _type) \
	(fy_genericp_cast_default((_vp), fy_generic_get_type_default(_type)))

static inline FY_ALWAYS_INLINE fy_generic_sequence_handle
fy_genericp_get_generic_sequence_handle_default(const fy_generic *vp,
		fy_generic_sequence_handle default_value)
{
	fy_generic_sequence_handle seqh;

	if (!vp)
		return default_value;

	seqh = fy_generic_sequence_to_handle(*vp);
	if (!seqh)
		return default_value;

	return seqh;
}

static inline FY_ALWAYS_INLINE fy_generic_mapping_handle
fy_genericp_get_generic_mapping_handle_default(const fy_generic *vp,
		fy_generic_mapping_handle default_value)
{
	fy_generic_mapping_handle maph;

	if (!vp)
		return default_value;

	maph = fy_generic_mapping_to_handle(*vp);
	if (!maph)
		return default_value;

	return maph;
}

static inline FY_ALWAYS_INLINE fy_generic
fy_genericp_get_generic_default(const fy_generic *vp, fy_generic default_value)
{
	return vp ? *vp : default_value;
}

static inline FY_ALWAYS_INLINE const fy_generic *
fy_genericp_get_string_genericp(const fy_generic *vp)
{
	return vp && fy_generic_is_direct_string(*vp) ? vp : NULL;
}

static inline FY_ALWAYS_INLINE fy_generic_sized_string
fy_genericp_get_szstr_default(const fy_generic *vp, fy_generic_sized_string default_value)
{
	struct fy_generic_sized_string ret;

	if (!vp)
		return default_value;

	if (!fy_generic_is_direct(*vp))
		vp = fy_genericp_indirect_get_valuep(vp);
	if (!vp || !fy_generic_is_direct_string(*vp))
		return default_value;

	ret.data = fy_genericp_get_string_size_no_check(vp, &ret.size);
	return ret;
}

#define fy_generic_cast_default_coerse(_v, _dv) \
	({ \
		fy_generic __vv = fy_to_generic(_v); \
		fy_generic_cast_default(__vv, (_dv)); \
	})

static inline FY_ALWAYS_INLINE fy_generic_sequence_handle
fy_generic_sequencep_get_generic_sequence_handle_default(const fy_generic_sequence *seqp, size_t idx,
		fy_generic_sequence_handle default_value)
{
	return fy_genericp_get_generic_sequence_handle_default(
			fy_generic_sequencep_get_itemp(seqp, idx),
			default_value);
}

static inline FY_ALWAYS_INLINE fy_generic_sequence_handle
fy_generic_sequence_get_generic_sequence_handle_default(fy_generic seq, size_t idx,
		fy_generic_sequence_handle default_value)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_generic_sequence_handle_default(seqp, idx, default_value);
}

static inline FY_ALWAYS_INLINE fy_generic_mapping_handle
fy_generic_sequencep_get_generic_mapping_handle_default(const fy_generic_sequence *seqp, size_t idx,
		fy_generic_mapping_handle default_value)
{
	return fy_genericp_get_generic_mapping_handle_default(
			fy_generic_sequencep_get_itemp(seqp, idx),
			default_value);
}

static inline FY_ALWAYS_INLINE fy_generic_mapping_handle
fy_generic_sequence_get_generic_mapping_handle_default(fy_generic seq, size_t idx,
		fy_generic_mapping_handle default_value)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_generic_mapping_handle_default(seqp, idx, default_value);
}

static inline FY_ALWAYS_INLINE fy_generic
fy_generic_sequencep_get_generic_default(const fy_generic_sequence *seqp, size_t idx, fy_generic default_value)
{
	return fy_genericp_get_generic_default(
			fy_generic_sequencep_get_itemp(seqp, idx),
			default_value);
}

static inline FY_ALWAYS_INLINE fy_generic
fy_generic_sequence_get_generic_default(fy_generic seq, size_t idx, fy_generic default_value)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_generic_default(seqp, idx, default_value);
}

static inline FY_ALWAYS_INLINE const char *
fy_generic_sequencep_get_const_char_ptr_default(const fy_generic_sequence *seqp, size_t idx, const char *default_value)
{
	return fy_genericp_get_const_char_ptr_default(
			fy_generic_sequencep_get_itemp(seqp, idx),
			default_value);
}

static inline FY_ALWAYS_INLINE const char *
fy_generic_sequence_get_const_char_ptr_default(fy_generic seq, size_t idx, const char *default_value)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_const_char_ptr_default(seqp, idx, default_value);
}

static inline FY_ALWAYS_INLINE char *
fy_generic_sequencep_get_char_ptr_default(const fy_generic_sequence *seqp, size_t idx, const char *default_value)
{
	return (char *)fy_generic_sequencep_get_const_char_ptr_default(seqp, idx, default_value);
}

static inline FY_ALWAYS_INLINE char *
fy_generic_sequence_get_char_ptr_default(fy_generic seq, size_t idx, char *default_value)
{
	return (char *)fy_generic_sequence_get_const_char_ptr_default(seq, idx, default_value);
}

static inline FY_ALWAYS_INLINE fy_generic_sized_string
fy_generic_sequencep_get_szstr_default(const fy_generic_sequence *seqp, size_t idx,
		fy_generic_sized_string default_value)
{
	return fy_genericp_get_szstr_default(
			fy_generic_sequencep_get_itemp(seqp, idx),
			default_value);
}

static inline FY_ALWAYS_INLINE fy_generic_sized_string
fy_generic_sequence_get_szstr_default(fy_generic seq, size_t idx,
		fy_generic_sized_string default_value)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_szstr_default(seqp, idx, default_value);
}

static inline FY_ALWAYS_INLINE fy_generic_map_pair
fy_generic_sequence_get_map_pair_default(fy_generic seq, size_t idx, fy_generic_map_pair default_value)
{
	return default_value;
}

static inline FY_ALWAYS_INLINE fy_generic_map_pair *
fy_generic_sequence_get_map_pairp_default(fy_generic seq, size_t idx, fy_generic_map_pair *default_value)
{
	return default_value;
}

static inline FY_ALWAYS_INLINE const fy_generic_map_pair *
fy_generic_sequence_get_const_map_pairp_default(fy_generic seq, size_t idx, const fy_generic_map_pair *default_value)
{
	return default_value;
}

static inline FY_ALWAYS_INLINE fy_generic_map_pair
fy_generic_sequencep_get_map_pair_default(const fy_generic_sequence *seqp, size_t idx, fy_generic_map_pair default_value)
{
	return default_value;
}

static inline FY_ALWAYS_INLINE fy_generic_map_pair *
fy_generic_sequencep_get_map_pairp_default(const fy_generic_sequence *seqp, size_t idx, fy_generic_map_pair *default_value)
{
	return default_value;
}

static inline FY_ALWAYS_INLINE const fy_generic_map_pair *
fy_generic_sequencep_get_const_map_pairp_default(const fy_generic_sequence *seqp, size_t idx, const fy_generic_map_pair *default_value)
{
	return default_value;
}

#define fy_generic_sequence_get_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_generic_sequence_get, default), \
	char *: fy_generic_sequence_get_char_ptr_default, \
	const char *: fy_generic_sequence_get_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_generic_sequence_get_generic_sequence_handle_default, \
	fy_generic_mapping_handle: fy_generic_sequence_get_generic_mapping_handle_default, \
	fy_generic_sized_string: fy_generic_sequence_get_szstr_default, \
	fy_generic: fy_generic_sequence_get_generic_default, \
	fy_generic_map_pair: fy_generic_sequence_get_map_pair_default, \
	fy_generic_map_pair *: fy_generic_sequence_get_map_pairp_default, \
	const fy_generic_map_pair *: fy_generic_sequence_get_const_map_pairp_default

#define fy_generic_sequence_get_default(_seq, _idx, _dv) \
	(_Generic((_dv), fy_generic_sequence_get_default_Generic_dispatch)((_seq), (_idx), (_dv)))

#define fy_generic_sequence_get_typed(_seq, _idx, _type) \
	(fy_generic_sequence_get_default((_seq), (_idx), fy_generic_get_type_default(_type)))

#define fy_generic_sequence_get_default_coerse(_seq, _idxv, _dv) \
	({ \
		__typeof__((_dv) + 0) __dv = (_dv); \
		fy_generic __idxv = fy_to_generic(_idxv); \
		size_t __idxi = (size_t)fy_generic_get_unsigned_long_long_default(__idxv, (unsigned long long)-1); \
		fy_generic_sequence_get_default((_seq), __idxi, (_dv)); \
	})

#define fy_generic_sequence_get_coerse(_seq, _idxv, _type) \
	(fy_generic_sequence_get_default_coerse((_seq), (_idxv), fy_generic_get_type_default(_type)))

#define fy_generic_sequencep_get_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_generic_sequencep_get, default), \
	char *: fy_generic_sequencep_get_char_ptr_default, \
	const char *: fy_generic_sequencep_get_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_generic_sequencep_get_generic_sequence_handle_default, \
	fy_generic_mapping_handle: fy_generic_sequencep_get_generic_mapping_handle_default, \
	fy_generic_sized_string: fy_generic_sequencep_get_szstr_default, \
	fy_generic: fy_generic_sequencep_get_generic_default, \
	fy_generic_map_pair: fy_generic_sequencep_get_map_pair_default, \
	fy_generic_map_pair *: fy_generic_sequencep_get_map_pairp_default, \
	const fy_generic_map_pair *: fy_generic_sequencep_get_const_map_pairp_default

#define fy_generic_sequencep_get_default(_seqp, _idx, _dv) \
	(_Generic((_dv), fy_generic_sequencep_get_default_Generic_dispatch)((_seqp), (_idx), (_dv)))

#define fy_generic_sequencep_get_typed(_seqp, _idx, _type) \
	(fy_generic_sequencep_get_default((_seqp), (_idx), fy_generic_get_type_default(_type)))

#define fy_generic_sequencep_get_default_coerse(_seqp, _idxv, _dv) \
	({ \
		__typeof__((_dv) + 0) __dv = (_dv); \
		fy_generic __idxv = fy_to_generic(_idxv); \
		size_t __idxi = (size_t)fy_generic_get_unsigned_long_long_default(__idxv, (unsigned long long)-1); \
		fy_generic_sequencep_get_default((_seqp), __idxi, (_dv)); \
	})

#define fy_generic_sequencep_get_coerse(_seqp, _idxv, _type) \
	(fy_generic_sequencep_get_default_coerse((_seqp), (_idxv), fy_generic_get_type_default(_type)))

// for a sequence get_at is the same as get
#define fy_generic_sequence_get_at_default(_seq, _idx, _dv) \
	(fy_generic_sequence_get_default((_seq), (_idx), (_dv)))

#define fy_generic_sequence_get_at_typed(_seq, _idx, _type) \
	(fy_generic_sequence_get_typed((_seq), (_idx), (_type)))

#define fy_generic_sequencep_get_at_default(_seqp, _idx, _dv) \
	(fy_generic_sequencep_get_default((_seqp), (_idx), (_dv)))

#define fy_generic_sequencep_get_at_typed(_seqp, _idx, _type) \
	(fy_generic_sequencep_get_typed((_seqp), (_idx), (_type)))

// map

static inline fy_generic_sequence_handle
fy_generic_mappingp_get_generic_sequence_handle_default(const fy_generic_mapping *mapp, fy_generic key,
		fy_generic_sequence_handle default_value)
{
	return fy_genericp_get_generic_sequence_handle_default(
			fy_generic_mappingp_get_valuep(mapp, key),
			default_value);
}

static inline fy_generic_sequence_handle
fy_generic_mapping_get_generic_sequence_handle_default(fy_generic map, fy_generic key,
		fy_generic_sequence_handle default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_generic_sequence_handle_default(mapp, key, default_value);
}

static inline fy_generic_mapping_handle
fy_generic_mappingp_get_generic_mapping_handle_default(const fy_generic_mapping *mapp, fy_generic key,
		fy_generic_mapping_handle default_value)
{
	return fy_genericp_get_generic_mapping_handle_default(
			fy_generic_mappingp_get_valuep(mapp, key),
			default_value);
}

static inline fy_generic_mapping_handle
fy_generic_mapping_get_generic_mapping_handle_default(fy_generic map, fy_generic key,
		fy_generic_mapping_handle default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_generic_mapping_handle_default(mapp, key, default_value);
}

static inline fy_generic fy_generic_mappingp_get_generic_default(const fy_generic_mapping *mapp, fy_generic key, fy_generic default_value)
{
	return fy_genericp_get_generic_default(
			fy_generic_mappingp_get_valuep(mapp, key),
			default_value);
}

static inline fy_generic fy_generic_mapping_get_generic_default(fy_generic map, fy_generic key, fy_generic default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_generic_default(mapp, key, default_value);
}

static inline const char *fy_generic_mappingp_get_const_char_ptr_default(const fy_generic_mapping *mapp, fy_generic key, const char *default_value)
{
	return fy_genericp_get_const_char_ptr_default(
			fy_generic_mappingp_get_valuep(mapp, key),
			default_value);
}

static inline const char *fy_generic_mapping_get_const_char_ptr_default(fy_generic map, fy_generic key, const char *default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_const_char_ptr_default(mapp, key, default_value);
}

static inline char *fy_generic_mappingp_get_char_ptr_default(const fy_generic_mapping *mapp, fy_generic key, const char *default_value)
{
	return (char *)fy_generic_mappingp_get_const_char_ptr_default(mapp, key, default_value);
}

static inline char *fy_generic_mapping_get_char_ptr_default(fy_generic map, fy_generic key, char *default_value)
{
	return (char *)fy_generic_mapping_get_const_char_ptr_default(map, key, default_value);
}

static inline fy_generic_sized_string
fy_generic_mappingp_get_szstr_default(const fy_generic_mapping *mapp, fy_generic key,
		fy_generic_sized_string default_value)
{
	return fy_genericp_get_szstr_default(
			fy_generic_mappingp_get_valuep(mapp, key),
			default_value);
}

static inline fy_generic_sized_string
fy_generic_mapping_get_szstr_default(fy_generic map, fy_generic key,
		fy_generic_sized_string default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_szstr_default(mapp, key, default_value);
}

#define fy_generic_mapping_get_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_generic_mapping_get, default), \
	char *: fy_generic_mapping_get_char_ptr_default, \
	const char *: fy_generic_mapping_get_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_generic_mapping_get_generic_sequence_handle_default, \
	fy_generic_mapping_handle: fy_generic_mapping_get_generic_mapping_handle_default, \
	fy_generic_sized_string: fy_generic_mapping_get_szstr_default, \
	fy_generic: fy_generic_mapping_get_generic_default

#define fy_generic_mapping_get_default(_map, _key, _dv) \
	(_Generic((_dv), fy_generic_mapping_get_default_Generic_dispatch)((_map), fy_to_generic(_key), (_dv)))

#define fy_generic_mapping_get_typed(_map, _key, _type) \
	(fy_generic_mapping_get_default((_map), (_key), fy_generic_get_type_default(_type)))

#define fy_generic_mappingp_get_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_generic_mappingp_get, default), \
	char *: fy_generic_mappingp_get_char_ptr_default, \
	const char *: fy_generic_mappingp_get_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_generic_mappingp_get_generic_sequence_handle_default, \
	fy_generic_mapping_handle: fy_generic_mappingp_get_generic_mapping_handle_default, \
	fy_generic_sized_string: fy_generic_mappingp_get_szstr_default, \
	fy_generic: fy_generic_mappingp_get_generic_default

#define fy_generic_mappingp_get_default(_mapp, _key, _dv) \
	({ \
		fy_generic_typeof(_dv) _ret; \
		fy_generic __key = fy_to_generic(_key); \
		\
		__ret = _Generic((_dv), fy_generic_mappingp_get_default_Generic_dispatch)((_mapp), __key, (_dv)); \
		__ret; \
	})

#define fy_generic_mappingp_get_typed(_mapp, _key, _type) \
	(fy_generic_mappingp_get_default((_mapp), (_key), fy_generic_get_type_default(_type)))

// for a mapping get_at it's like a sequence of map pairs */
static inline fy_generic_sequence_handle
fy_generic_mappingp_get_at_generic_sequence_handle_default(const fy_generic_mapping *mapp, size_t idx,
		fy_generic_sequence_handle default_value)
{
	return fy_genericp_get_generic_sequence_handle_default(
			fy_generic_mappingp_get_at_valuep(mapp, idx),
			default_value);
}

static inline fy_generic_sequence_handle
fy_generic_mapping_get_at_generic_sequence_handle_default(fy_generic map, size_t idx,
		fy_generic_sequence_handle default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_at_generic_sequence_handle_default(mapp, idx, default_value);
}

static inline fy_generic_mapping_handle
fy_generic_mappingp_get_at_generic_mapping_handle_default(const fy_generic_mapping *mapp, size_t idx,
		fy_generic_mapping_handle default_value)
{
	return fy_genericp_get_generic_mapping_handle_default(
			fy_generic_mappingp_get_at_valuep(mapp, idx),
			default_value);
}

static inline fy_generic_mapping_handle
fy_generic_mapping_get_at_generic_mapping_handle_default(fy_generic map, size_t idx,
		fy_generic_mapping_handle default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_at_generic_mapping_handle_default(mapp, idx, default_value);
}

static inline fy_generic fy_generic_mappingp_get_at_generic_default(const fy_generic_mapping *mapp, size_t idx, fy_generic default_value)
{
	return fy_genericp_get_generic_default(
			fy_generic_mappingp_get_at_valuep(mapp, idx),
			default_value);
}

static inline fy_generic fy_generic_mapping_get_at_generic_default(fy_generic map, size_t idx, fy_generic default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_at_generic_default(mapp, idx, default_value);
}

static inline const char *fy_generic_mappingp_get_at_const_char_ptr_default(const fy_generic_mapping *mapp, size_t idx, const char *default_value)
{
	return fy_genericp_get_const_char_ptr_default(
			fy_generic_mappingp_get_at_valuep(mapp, idx),
			default_value);
}

static inline const char *fy_generic_mapping_get_at_const_char_ptr_default(fy_generic map, size_t idx, const char *default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_at_const_char_ptr_default(mapp, idx, default_value);
}

static inline char *fy_generic_mappingp_get_at_char_ptr_default(const fy_generic_mapping *mapp, size_t idx, const char *default_value)
{
	return (char *)fy_generic_mappingp_get_at_const_char_ptr_default(mapp, idx, default_value);
}

static inline char *fy_generic_mapping_get_at_char_ptr_default(fy_generic map, size_t idx, char *default_value)
{
	return (char *)fy_generic_mapping_get_at_const_char_ptr_default(map, idx, default_value);
}

static inline const fy_generic_map_pair *fy_generic_mappingp_get_at_map_pairp_default(const fy_generic_mapping *mapp, size_t idx, const fy_generic_map_pair *default_value)
{
	if (!mapp || idx >= mapp->count)
		return default_value;
	return &mapp->pairs[idx];
}

static inline fy_generic_map_pair fy_generic_mappingp_get_at_map_pair_default(const fy_generic_mapping *mapp, size_t idx, fy_generic_map_pair default_value)
{
	if (!mapp || idx >= mapp->count)
		return default_value;
	return mapp->pairs[idx];
}

static inline fy_generic_map_pair fy_generic_mapping_get_at_map_pair_default(fy_generic map, size_t idx, fy_generic_map_pair default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_at_map_pair_default(mapp, idx, default_value);
}

static inline const fy_generic_map_pair *
fy_generic_mapping_get_at_const_map_pairp_default(fy_generic map, size_t idx, const fy_generic_map_pair *default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_at_map_pairp_default(mapp, idx, default_value);
}

static inline fy_generic_map_pair *
fy_generic_mapping_get_at_map_pairp_default(fy_generic map, size_t idx, fy_generic_map_pair *default_value)
{
	return (fy_generic_map_pair *)fy_generic_mapping_get_at_const_map_pairp_default(map, idx, default_value);
}

static inline fy_generic_sized_string
fy_generic_mappingp_get_at_szstr_default(const fy_generic_mapping *mapp, size_t idx,
		fy_generic_sized_string default_value)
{
	return fy_genericp_get_szstr_default(
			fy_generic_mappingp_get_at_valuep(mapp, idx),
			default_value);
}

static inline fy_generic_sized_string
fy_generic_mapping_get_at_szstr_default(fy_generic map, size_t idx,
		fy_generic_sized_string default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_at_szstr_default(mapp, idx, default_value);
}

// value retrieval at pos
#define fy_generic_mapping_get_at_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_generic_mapping_get_at, default), \
	char *: fy_generic_mapping_get_at_char_ptr_default, \
	const char *: fy_generic_mapping_get_at_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_generic_mapping_get_at_generic_sequence_handle_default, \
	fy_generic_mapping_handle: fy_generic_mapping_get_at_generic_mapping_handle_default, \
	fy_generic_sized_string: fy_generic_mapping_get_at_szstr_default, \
	fy_generic: fy_generic_mapping_get_at_generic_default, \
	fy_generic_map_pair: fy_generic_mapping_get_at_map_pair_default, \
	fy_generic_map_pair *: fy_generic_mapping_get_at_map_pairp_default, \
	const fy_generic_map_pair *: fy_generic_mapping_get_at_const_map_pairp_default

#define fy_generic_mapping_get_at_default(_map, _idx, _dv) \
	(_Generic((_dv), fy_generic_mapping_get_at_default_Generic_dispatch)((_map), (_idx), (_dv)))

#define fy_generic_mapping_get_at_typed(_map, _idx, _type) \
	(fy_generic_mapping_get_at_default((_map), (_idx), fy_generic_get_type_default(_type)))

#define fy_generic_mappingp_get_at_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_generic_mappingp_get_at, default), \
	char *: fy_generic_mappingp_get_at_char_ptr_default, \
	const char *: fy_generic_mappingp_get_at_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_generic_mappingp_get_at_generic_sequence_handle_default, \
	fy_generic_mapping_handle: fy_generic_mappingp_get_at_generic_mapping_handle_default, \
	fy_generic_sized_string: fy_generic_mappingp_get_at_szstr_default, \
	fy_generic: fy_generic_mappingp_get_at_generic_default, \
	fy_generic_map_pair: fy_generic_mappingp_get_at_map_pair_default, \
	fy_generic_map_pair *: fy_generic_mappingp_get_at_map_pairp_default, \
	const fy_generic_map_pair *: fy_generic_mappingp_get_at_const_map_pairp_default

#define fy_generic_mappingp_get_at_default(_mapp, _idx, _dv) \
	(_Generic((_dv), fy_generic_mappingp_get_at_default_Generic_dispatch)((_mapp), (_idx), (_dv)))

#define fy_generic_mappingp_get_at_typed(_mapp, _idx, _type) \
	(fy_generic_mappingp_get_at_default((_mapp), (_idx), fy_generic_get_type_default(_type)))

// for a mapping get_at it's like a sequence of map pairs */
static inline fy_generic_sequence_handle
fy_generic_mappingp_get_key_at_generic_sequence_handle_default(const fy_generic_mapping *mapp, size_t idx,
		fy_generic_sequence_handle default_value)
{
	return fy_genericp_get_generic_sequence_handle_default(
			fy_generic_mappingp_get_at_keyp(mapp, idx),
			default_value);
}

static inline fy_generic_sequence_handle
fy_generic_mapping_get_key_at_generic_sequence_handle_default(fy_generic map, size_t idx,
		fy_generic_sequence_handle default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_key_at_generic_sequence_handle_default(mapp, idx, default_value);
}

static inline fy_generic_mapping_handle
fy_generic_mappingp_get_key_at_generic_mapping_handle_default(const fy_generic_mapping *mapp, size_t idx,
		fy_generic_mapping_handle default_value)
{
	return fy_genericp_get_generic_mapping_handle_default(
			fy_generic_mappingp_get_at_keyp(mapp, idx),
			default_value);
}

static inline fy_generic_mapping_handle
fy_generic_mapping_get_key_at_generic_mapping_handle_default(fy_generic map, size_t idx,
		fy_generic_mapping_handle default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_key_at_generic_mapping_handle_default(mapp, idx, default_value);
}

static inline fy_generic fy_generic_mappingp_get_key_at_generic_default(const fy_generic_mapping *mapp, size_t idx, fy_generic default_value)
{
	return fy_genericp_get_generic_default(
			fy_generic_mappingp_get_at_keyp(mapp, idx),
			default_value);
}

static inline fy_generic fy_generic_mapping_get_key_at_generic_default(fy_generic map, size_t idx, fy_generic default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_key_at_generic_default(mapp, idx, default_value);
}

static inline const char *fy_generic_mappingp_get_key_at_const_char_ptr_default(const fy_generic_mapping *mapp, size_t idx, const char *default_value)
{
	return fy_genericp_get_const_char_ptr_default(
			fy_generic_mappingp_get_at_keyp(mapp, idx),
			default_value);
}

static inline const char *fy_generic_mapping_get_key_at_const_char_ptr_default(fy_generic map, size_t idx, const char *default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_key_at_const_char_ptr_default(mapp, idx, default_value);
}

static inline char *fy_generic_mappingp_get_key_at_char_ptr_default(const fy_generic_mapping *mapp, size_t idx, const char *default_value)
{
	return (char *)fy_generic_mappingp_get_key_at_const_char_ptr_default(mapp, idx, default_value);
}

static inline char *fy_generic_mapping_get_key_at_char_ptr_default(fy_generic map, size_t idx, char *default_value)
{
	return (char *)fy_generic_mapping_get_key_at_const_char_ptr_default(map, idx, default_value);
}

static inline fy_generic_sized_string
fy_generic_mappingp_get_key_at_szstr_default(const fy_generic_mapping *mapp, size_t idx,
		fy_generic_sized_string default_value)
{
	return fy_genericp_get_szstr_default(
			fy_generic_mappingp_get_at_keyp(mapp, idx),
			default_value);
}

static inline fy_generic_sized_string
fy_generic_mapping_get_key_at_szstr_default(fy_generic map, size_t idx,
		fy_generic_sized_string default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_key_at_szstr_default(mapp, idx, default_value);
}

// key retrieval at pos
#define fy_generic_mapping_get_key_at_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_generic_mapping_get_key_at, default), \
	char *: fy_generic_mapping_get_key_at_char_ptr_default, \
	const char *: fy_generic_mapping_get_key_at_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_generic_mapping_get_key_at_generic_sequence_handle_default, \
	fy_generic_mapping_handle: fy_generic_mapping_get_key_at_generic_mapping_handle_default, \
	fy_generic_sized_string: fy_generic_mapping_get_key_at_szstr_default, \
	fy_generic: fy_generic_mapping_get_key_at_generic_default, \
	fy_generic_map_pair: fy_generic_mapping_get_at_map_pair_default, \
	fy_generic_map_pair *: fy_generic_mapping_get_at_map_pairp_default, \
	const fy_generic_map_pair *: fy_generic_mapping_get_at_const_map_pairp_default

#define fy_generic_mapping_get_key_at_default(_map, _idx, _dv) \
	(_Generic((_dv), fy_generic_mapping_get_key_at_default_Generic_dispatch)((_map), (_idx), (_dv)))

#define fy_generic_mapping_get_at_typed(_map, _idx, _type) \
	(fy_generic_mapping_get_at_default((_map), (_idx), fy_generic_get_type_default(_type)))

#define fy_generic_mappingp_get_key_at_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_generic_mappingp_get_key_at, default), \
	char *: fy_generic_mappingp_get_key_at_char_ptr_default, \
	const char *: fy_generic_mappingp_get_key_at_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_generic_mappingp_get_key_at_generic_sequence_handle_default, \
	fy_generic_mapping_handle: fy_generic_mappingp_get_key_at_generic_mapping_handle_default, \
	fy_generic_sized_string: fy_generic_mappingp_get_key_at_szstr_default, \
	fy_generic: fy_generic_mappingp_get_key_at_generic_default, \
	fy_generic_map_pair: fy_generic_mappingp_get_at_map_pair_default, \
	fy_generic_map_pair *: fy_generic_mappingp_get_at_map_pairp_default, \
	const fy_generic_map_pair *: fy_generic_mappingp_get_at_const_map_pairp_default

#define fy_generic_mappingp_get_key_at_default(_mapp, _idx, _dv) \
	(_Generic((_dv), fy_generic_mappingp_get_key_at_default_Generic_dispatch)((_mapp), (_idx), (_dv)))

#define fy_generic_mappingp_get_key_at_typed(_mapp, _idx, _type) \
	(fy_generic_mappingp_get_key_at_default((_mapp), (_idx), fy_generic_get_type_default(_type)))

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
	if (fy_generic_is_direct(*vp))
		return *vp;
	return fy_generic_indirect_get_value(*vp);
}

static inline enum fy_generic_type fy_get_generic_direct_collection_type(fy_generic v)
{
	/* seq and collection have the lower 3 bits zero */
	if ((v.v & FY_INPLACE_TYPE_MASK) != 0)
		return FYGT_INVALID;
	/* sequence is 0, mapping is 8 */
	return FYGT_SEQUENCE + ((v.v >> 3) & 1);
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

#define fy_generic_get_default(_colv, _key, _dv) \
	({ \
		fy_generic_typeof(_colv) __colv = (_colv); \
		fy_generic_typeof(_dv) __dv = (_dv); \
		fy_generic_typeof(_dv) __ret; \
		\
		const fy_generic __colv2 = _Generic(__colv, \
			fy_generic: fy_get_generic_generic(&__colv), \
			fy_generic_sequence_handle: fy_get_generic_seq_handle(&__colv), \
			fy_generic_mapping_handle: fy_get_generic_map_handle(&__colv) ); \
		const enum fy_generic_type __type = _Generic(__colv, \
			fy_generic: fy_get_generic_direct_collection_type(__colv2), \
			fy_generic_sequence_handle: FYGT_SEQUENCE, \
			fy_generic_mapping_handle: FYGT_MAPPING ); \
		switch (__type) { \
		case FYGT_MAPPING: \
			const fy_generic __key = fy_to_generic(_key); \
			__ret = _Generic(__dv, fy_generic_mapping_get_default_Generic_dispatch) \
				(__colv2, __key, __dv); \
			break; \
		case FYGT_SEQUENCE: \
			const size_t __index = fy_generic_cast_default_coerse(_key, LLONG_MAX); \
			__ret = _Generic(__dv, fy_generic_sequence_get_default_Generic_dispatch) \
				(__colv2, __index, __dv); \
			break; \
		default: \
			__ret = __dv; \
			break; \
		} \
		__ret; \
	})

#define fy_generic_get_typed(_colv, _key, _type) \
	(fy_generic_get_default((_colv), (_key), fy_generic_get_type_default(_type)))

#define fy_generic_get_at_default(_colv, _idx, _dv) \
	({ \
		fy_generic_typeof(_colv) __colv = (_colv); \
		fy_generic_typeof(_dv) __dv = (_dv); \
		fy_generic_typeof(_dv) __ret; \
		\
		const fy_generic __colv2 = _Generic(__colv, \
			fy_generic: fy_get_generic_generic(&__colv), \
			fy_generic_sequence_handle: fy_get_generic_seq_handle(&__colv), \
			fy_generic_mapping_handle: fy_get_generic_map_handle(&__colv) ); \
		const enum fy_generic_type __type = _Generic(__colv, \
			fy_generic: fy_get_generic_direct_collection_type(__colv2), \
			fy_generic_sequence_handle: FYGT_SEQUENCE, \
			fy_generic_mapping_handle: FYGT_MAPPING ); \
		const size_t __index = (_idx); \
		switch (__type) { \
		case FYGT_MAPPING: \
			__ret = _Generic(__dv, fy_generic_mapping_get_at_default_Generic_dispatch) \
				(__colv2, __index, __dv); \
			break; \
		case FYGT_SEQUENCE: \
			__ret = _Generic(__dv, fy_generic_sequence_get_default_Generic_dispatch) \
				(__colv2, __index, __dv); \
			break; \
		default: \
			__ret = __dv; \
			break; \
		} \
		__ret; \
	})

#define fy_generic_get_at_typed(_colv, _idx, _type) \
	(fy_generic_get_at_default((_colv), (_idx), fy_generic_get_type_default(_type)))

#define fy_generic_get_key_at_default(_colv, _idx, _dv) \
	({ \
		fy_generic_typeof(_colv) __colv = (_colv); \
		fy_generic_typeof(_dv) __dv = (_dv); \
		fy_generic_typeof(_dv) __ret; \
		\
		const fy_generic __colv2 = _Generic(__colv, \
			fy_generic: fy_get_generic_generic(&__colv), \
			fy_generic_sequence_handle: fy_get_generic_seq_handle(&__colv), \
			fy_generic_mapping_handle: fy_get_generic_map_handle(&__colv) ); \
		const enum fy_generic_type __type = _Generic(__colv, \
			fy_generic: fy_get_generic_direct_collection_type(__colv2), \
			fy_generic_sequence_handle: FYGT_SEQUENCE, \
			fy_generic_mapping_handle: FYGT_MAPPING ); \
		const size_t __index = (_idx); \
		switch (__type) { \
		case FYGT_MAPPING: \
			__ret = _Generic(__dv, fy_generic_mapping_get_key_at_default_Generic_dispatch) \
				(__colv2, __index, __dv); \
			break; \
		case FYGT_SEQUENCE: \
			__ret = _Generic(__dv, fy_generic_sequence_get_default_Generic_dispatch) \
				(__colv2, __index, __dv); \
			break; \
		default: \
			__ret = __dv; \
			break; \
		} \
		__ret; \
	})

#define fy_generic_get_key_at_typed(_colv, _idx, _type) \
	(fy_generic_get_key_at_default((_colv), (_idx), fy_generic_get_type_default(_type)))

/* when it's a generic */
static inline FY_ALWAYS_INLINE size_t
fy_get_len_genericp(const void *p)
{
	const fy_generic *vp;
	size_t len;

	vp = p;
	if (!fy_generic_is_direct(*vp))
		vp = fy_genericp_indirect_get_valuep(vp);

	/* collections (seq/map) */
	if (fy_generic_is_direct_collection(*vp)) {
		const fy_generic_collection *colp = fy_generic_resolve_collection_ptr(*vp);
		return colp ? colp->count : 0;
	}

	if (fy_generic_is_direct_string(*vp)) {
		(void)fy_genericp_get_string_size_no_check(vp, &len);
		return len;
	}

	return 0;
}

static inline size_t fy_get_len_seq_handle(const void *p)
{
	const fy_generic_sequence * const *seqpp = p;
	return fy_generic_sequencep_get_item_count(*seqpp);
}

static inline size_t fy_get_len_map_handle(const void *p)
{
	const fy_generic_mapping * const *mappp = p;
	return fy_generic_mappingp_get_pair_count(*mappp);
}

#define fy_generic_len(_colv) \
	({ \
		fy_generic_typeof(_colv) __colv = (_colv); \
		_Generic(__colv, \
			fy_generic: fy_get_len_genericp, \
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
	FYGBCF_CREATE_ALLOCATOR		= FY_BIT(5),
	FYGBCF_DUPLICATE_KEYS_DISABLED	= FY_BIT(6),
	FYGBCF_DEDUP_ENABLED		= FY_BIT(7),
	FYGBCF_SCOPE_LEADER		= FY_BIT(8),
	FYGBCF_CREATE_TAG		= FY_BIT(9),
	FYGBCF_TRACE			= FY_BIT(10),
};

struct fy_generic_builder_cfg {
	enum fy_gb_cfg_flags flags;
	struct fy_allocator *allocator;
	struct fy_generic_builder *parent;
	size_t estimated_max_size;
	struct fy_diag *diag;
};

enum fy_gb_flags {
	FYGBF_NONE			= 0,
	FYGBF_SCOPE_LEADER		= FY_BIT(0),	/* builder starts new scope */
	FYGBF_DEDUP_ENABLED		= FY_BIT(1),	/* build is dedup enabled */
	FYGBF_DEDUP_CHAIN		= FY_BIT(2),	/* builder chain is dedup */
	FYGBF_OWNS_ALLOCATOR		= FY_BIT(3),	/* builder owns the allocator */
	FYGBF_CREATED_TAG		= FY_BIT(4),	/* builder has created tag on allocator */
};

struct fy_generic_builder {
	struct fy_generic_builder_cfg cfg;
	enum fy_generic_schema schema;
	enum fy_gb_flags flags;
	struct fy_allocator *allocator;
	int alloc_tag;
	void *linear;	/* when making it linear */
	FY_ATOMIC(uint64_t) allocation_failures;
};

extern __thread struct fy_generic_builder *fy_current_gb;

static inline void *fy_gb_alloc(struct fy_generic_builder *gb, size_t size, size_t align)
{
	void *p;

	p = fy_allocator_alloc_nocheck(gb->allocator, gb->alloc_tag, size, align);
	if (!p)
		fy_atomic_fetch_add(&gb->allocation_failures, 1);
	return p;
}

static inline void fy_gb_free(struct fy_generic_builder *gb, void *ptr)
{
	fy_allocator_free_nocheck(gb->allocator, gb->alloc_tag, ptr);
}

static inline void fy_gb_trim(struct fy_generic_builder *gb)
{
	fy_allocator_trim_tag_nocheck(gb->allocator, gb->alloc_tag);
}

static inline const void *fy_gb_store(struct fy_generic_builder *gb, const void *data, size_t size, size_t align)
{
	const void *p;

	p = fy_allocator_store_nocheck(gb->allocator, gb->alloc_tag, data, size, align);
	if (!p)
		fy_atomic_fetch_add(&gb->allocation_failures, 1);
	return p;
}

static inline const void *fy_gb_storev(struct fy_generic_builder *gb, const struct iovec *iov, unsigned int iovcnt, size_t align)
{
	const void *p;

	p = fy_allocator_storev_nocheck(gb->allocator, gb->alloc_tag, iov, iovcnt, align);
	if (!p)
		fy_atomic_fetch_add(&gb->allocation_failures, 1);
	return p;
}

static inline const void *fy_gb_lookupv(struct fy_generic_builder *gb, const struct iovec *iov, unsigned int iovcnt, size_t align)
{
	const void *ptr;
	uint64_t hash;

	if (!(gb->flags & FYGBF_DEDUP_ENABLED))
		return NULL;

	hash = fy_iovec_xxhash64(iov, iovcnt);

	while (gb && (gb->flags & FYGBF_DEDUP_ENABLED)) {
		ptr = fy_allocator_lookupv_nocheck(gb->allocator, gb->alloc_tag, iov, iovcnt, align, hash);
		if (ptr)
			return ptr;
		gb = gb->cfg.parent;
	}

	return NULL;
}

static inline const void *fy_gb_lookup(struct fy_generic_builder *gb, const void *data, size_t size, size_t align)
{
	struct iovec iov[1];

	/* just call the storev */
	iov[0].iov_base = (void *)data;
	iov[0].iov_len = size;

	return fy_gb_lookupv(gb, iov, 1, align);
}

static inline struct fy_allocator_info *
fy_gb_get_allocator_info(struct fy_generic_builder *gb)
{
	return fy_allocator_get_info_nocheck(gb->allocator, gb->alloc_tag);
}

static inline void fy_gb_release(struct fy_generic_builder *gb, const void *ptr, size_t size)
{
	fy_allocator_release_nocheck(gb->allocator, gb->alloc_tag, ptr, size);
}

static inline uint64_t fy_gb_allocation_failures(struct fy_generic_builder *gb)
{
	return fy_atomic_load(&gb->allocation_failures);
}

int fy_generic_builder_setup(struct fy_generic_builder *gb, const struct fy_generic_builder_cfg *cfg);
void fy_generic_builder_cleanup(struct fy_generic_builder *gb);

struct fy_generic_builder *fy_generic_builder_create(const struct fy_generic_builder_cfg *cfg);
void fy_generic_builder_destroy(struct fy_generic_builder *gb);
void fy_generic_builder_reset(struct fy_generic_builder *gb);

#define FY_GENERIC_BUILDER_LINEAR_IN_PLACE_MIN_SIZE	(FY_LINEAR_ALLOCATOR_IN_PLACE_MIN_SIZE + 128)

/* no need to destroy */
struct fy_generic_builder *fy_generic_builder_create_in_place(enum fy_gb_cfg_flags flags, struct fy_generic_builder *parent, void *buffer, size_t size);

struct fy_allocator *fy_generic_builder_get_allocator(struct fy_generic_builder *gb);
size_t fy_generic_builder_get_free(struct fy_generic_builder *gb);

bool fy_generic_builder_contains_out_of_place(struct fy_generic_builder *gb, fy_generic v);

static inline bool fy_generic_builder_contains(struct fy_generic_builder *gb, fy_generic v)
{
	/* invalids never contained */
	if (v.v == fy_invalid_value)
		return false;

	/* inplace always contained (implicitly) */
	if (fy_generic_is_in_place(v))
		return true;

	if (!gb)
		return false;

	return fy_generic_builder_contains_out_of_place(gb, v);
}

struct fy_generic_builder *fy_generic_builder_get_scope_leader(struct fy_generic_builder *gb);
struct fy_generic_builder *fy_generic_builder_get_export_builder(struct fy_generic_builder *gb);
fy_generic fy_generic_builder_export(struct fy_generic_builder *gb, fy_generic v);

static inline fy_generic fy_gb_null_type_create_out_of_place(struct fy_generic_builder *gb, void *p)
{
	return fy_invalid;
}

static inline fy_generic fy_gb_bool_type_create_out_of_place(struct fy_generic_builder *gb, bool state)
{
	return fy_invalid;
}

static inline fy_generic fy_gb_dint_type_create_out_of_place(struct fy_generic_builder *gb, fy_generic_decorated_int vald)
{
	const struct fy_generic_decorated_int *valp;

	valp = fy_gb_lookup(gb, &vald, sizeof(vald), FY_GENERIC_SCALAR_ALIGN);
	if (!valp)
		valp = fy_gb_store(gb, &vald, sizeof(vald), FY_GENERIC_SCALAR_ALIGN);
	if (!valp)
		return fy_invalid;
	return (fy_generic){ .v = (uintptr_t)valp | FY_INT_OUTPLACE_V };
}

static inline fy_generic fy_gb_int_type_create_out_of_place(struct fy_generic_builder *gb, long long val)
{
	return fy_gb_dint_type_create_out_of_place(gb, (struct fy_generic_decorated_int){
				.sv = val,
				.is_unsigned = false });
}

static inline fy_generic fy_gb_uint_type_create_out_of_place(struct fy_generic_builder *gb, unsigned long long val)
{
	return fy_gb_dint_type_create_out_of_place(gb, (struct fy_generic_decorated_int){
				.uv = val,
				.is_unsigned = val > (unsigned long long)LLONG_MAX });
}

static inline fy_generic fy_gb_float_type_create_out_of_place(struct fy_generic_builder *gb, double val)
{
	const double *valp;

	valp = fy_gb_lookup(gb, &val, sizeof(val), FY_SCALAR_ALIGNOF(double));
	if (!valp)
		valp = fy_gb_store(gb, &val, sizeof(val), FY_SCALAR_ALIGNOF(double));
	if (!valp)
		return fy_invalid;
	return (fy_generic){ .v = (uintptr_t)valp | FY_FLOAT_OUTPLACE_V };
}

static inline fy_generic fy_gb_string_size_create_out_of_place(struct fy_generic_builder *gb, const char *str, size_t len)
{
	uint8_t lenbuf[FYGT_SIZE_ENCODING_MAX];
	struct iovec iov[3];
	const void *s;
	void *p;

	p = fy_encode_size(lenbuf, sizeof(lenbuf), len);
	assert(p);

	iov[0].iov_base = lenbuf;
	iov[0].iov_len = (size_t)((uint8_t *)p - lenbuf) ;
	iov[1].iov_base = (void *)str;
	iov[1].iov_len = len;
	iov[2].iov_base = "\x00";	/* null terminate always */
	iov[2].iov_len = 1;

	/* strings are aligned at 8 always */
	s = fy_gb_lookupv(gb, iov, ARRAY_SIZE(iov), FY_GENERIC_SCALAR_ALIGN);
	if (!s)
		s = fy_gb_storev(gb, iov, ARRAY_SIZE(iov), FY_GENERIC_SCALAR_ALIGN);
	if (!s)
		return fy_invalid;

	return (fy_generic){ .v = (uintptr_t)s | FY_STRING_OUTPLACE_V };
}

static inline fy_generic fy_gb_string_create_out_of_place(struct fy_generic_builder *gb, const char *str)
{
	return fy_gb_string_size_create_out_of_place(gb, str, str ? strlen(str) : 0);
}

static inline fy_generic fy_gb_szstr_create_out_of_place(struct fy_generic_builder *gb, const fy_generic_sized_string szstr)
{
	return fy_gb_string_size_create_out_of_place(gb, szstr.data, szstr.size);
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

#define FY_GENERIC_GB_LVAL_TEMPLATE(_ctype, _gtype, _gttype, _xctype, _xgtype, _xminv, _xmaxv, _default_v) \
static inline fy_generic fy_gb_ ## _gtype ## _create_out_of_place(struct fy_generic_builder *gb, const _ctype v) \
{ \
	return fy_gb_ ## _xgtype ## _create_out_of_place (gb, (_xctype)v ); \
} \
\
static inline fy_generic fy_gb_ ## _gtype ## _create(struct fy_generic_builder *gb, const _ctype v) \
{ \
	const fy_generic_value gv = fy_generic_in_place_ ## _gtype(v); \
	if (gv != fy_invalid_value) \
		return (fy_generic){ .v = gv }; \
	return fy_gb_ ## _xgtype ## _create_out_of_place (gb, (_xctype)v ); \
} \
\
struct fy_useless_struct_for_semicolon

#define FY_GENERIC_GB_INT_LVAL_TEMPLATE(_ctype, _gtype, _xminv, _xmaxv, _defaultv) \
	FY_GENERIC_GB_LVAL_TEMPLATE(_ctype, _gtype, INT, long long, int_type, _xminv, _xmaxv, _defaultv)

#define FY_GENERIC_GB_UINT_LVAL_TEMPLATE(_ctype, _gtype, _xminv, _xmaxv, _defaultv) \
	FY_GENERIC_GB_LVAL_TEMPLATE(_ctype, _gtype, INT, unsigned long long, uint_type, _xminv, _xmaxv, _defaultv)

#define FY_GENERIC_GB_FLOAT_LVAL_TEMPLATE(_ctype, _gtype, _xminv, _xmaxv, _defaultv) \
	FY_GENERIC_GB_LVAL_TEMPLATE(_ctype, _gtype, FLOAT, double, float_type, _xminv, _xmaxv, _defaultv)

FY_GENERIC_GB_LVAL_TEMPLATE(void *, null, NULL, void *, null_type, NULL, NULL, NULL);
FY_GENERIC_GB_LVAL_TEMPLATE(bool, bool, BOOL, bool, bool_type, false, true, false);
#if CHAR_MIN < 0
FY_GENERIC_GB_INT_LVAL_TEMPLATE(char, char, CHAR_MIN, CHAR_MAX, 0);
#else
FY_GENERIC_GB_UINT_LVAL_TEMPLATE(char, char, CHAR_MIN, CHAR_MAX, 0);
#endif
FY_GENERIC_GB_UINT_LVAL_TEMPLATE(unsigned char, unsigned_char, 0, UCHAR_MAX, 0);
FY_GENERIC_GB_INT_LVAL_TEMPLATE(signed char, signed_char, SCHAR_MIN, SCHAR_MAX, 0);
FY_GENERIC_GB_INT_LVAL_TEMPLATE(short, short, SHRT_MIN, SHRT_MAX, 0);
FY_GENERIC_GB_UINT_LVAL_TEMPLATE(unsigned short, unsigned_short, 0, USHRT_MAX, 0);
FY_GENERIC_GB_INT_LVAL_TEMPLATE(signed short, signed_short, SHRT_MIN, SHRT_MAX, 0);
FY_GENERIC_GB_INT_LVAL_TEMPLATE(int, int, INT_MIN, INT_MAX, 0);
FY_GENERIC_GB_UINT_LVAL_TEMPLATE(unsigned int, unsigned_int, 0, UINT_MAX, 0);
FY_GENERIC_GB_INT_LVAL_TEMPLATE(signed int, signed_int, INT_MIN, INT_MAX, 0);
FY_GENERIC_GB_INT_LVAL_TEMPLATE(long, long, LONG_MIN, LONG_MAX, 0);
FY_GENERIC_GB_UINT_LVAL_TEMPLATE(unsigned long, unsigned_long, 0, ULONG_MAX, 0);
FY_GENERIC_GB_INT_LVAL_TEMPLATE(signed long, signed_long, LONG_MIN, LONG_MAX, 0);
FY_GENERIC_GB_INT_LVAL_TEMPLATE(long long, long_long, LLONG_MIN, LLONG_MAX, 0);
FY_GENERIC_GB_UINT_LVAL_TEMPLATE(unsigned long long, unsigned_long_long, 0, ULLONG_MAX, 0);
FY_GENERIC_GB_INT_LVAL_TEMPLATE(signed long long, signed_long_long, LLONG_MIN, LLONG_MAX, 0);
FY_GENERIC_GB_FLOAT_LVAL_TEMPLATE(float, float, -FLT_MAX, FLT_MAX, 0.0);
FY_GENERIC_GB_FLOAT_LVAL_TEMPLATE(double, double, -DBL_MAX, DBL_MAX, 0.0);

#define fy_gb_to_generic_outofplace_put(_gb, _v) \
	(_Generic((_v), \
		FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_gb, create_out_of_place), \
		char *: fy_gb_string_create_out_of_place, \
		const char *: fy_gb_string_create_out_of_place, \
		fy_generic: fy_gb_internalize_out_of_place, \
		fy_generic_sized_string: fy_gb_szstr_create_out_of_place, \
		fy_generic_decorated_int: fy_gb_dint_type_create_out_of_place \
	      )((_gb), (_v)))

#define fy_gb_to_generic_value(_gb, _v) \
	({	\
		fy_generic_typeof(_v) __v = (_v); \
		fy_generic_value __r; \
		\
		__r = fy_to_generic_inplace(__v); \
		if (__r == fy_invalid_value) \
			__r = fy_gb_to_generic_outofplace_put((_gb), __v).v; \
		__r; \
	})

#define fy_gb_to_generic(_gb, _v) \
	((fy_generic) { .v = fy_gb_to_generic_value((_gb), (_v)) })

#define fy_gb_or_NULL(_maybe_gb) \
	(_Generic((_maybe_gb), struct fy_generic_builder *: (_maybe_gb), default: NULL))

#define fy_first_non_gb(_maybe_gb, ...) \
	(_Generic((_maybe_gb), \
		  struct fy_generic_builder *: FY_CPP_FIRST(__VA_ARGS__), \
		  default: (_maybe_gb)))

#define fy_second_non_gb(_maybe_gb, ...) \
	(_Generic((_maybe_gb), \
		  struct fy_generic_builder *: FY_CPP_SECOND(__VA_ARGS__), \
		  default: FY_CPP_FIRST(__VA_ARGS__)))

#define fy_third_non_gb(_maybe_gb, ...) \
	(_Generic((_maybe_gb), \
		  struct fy_generic_builder *: FY_CPP_THIRD(__VA_ARGS__), \
		  default: FY_CPP_SECOND(__VA_ARGS__)))

#define fy_gb_to_generic_value_helper(_gb, _arg, ...) \
	fy_gb_to_generic_value((_gb), (_arg))

#define fy_to_generic_value(_maybe_gb, ...) \
	(_Generic((_maybe_gb), \
		struct fy_generic_builder *: ({ fy_gb_to_generic_value_helper(fy_gb_or_NULL(_maybe_gb), __VA_ARGS__ __VA_OPT__(,) 0); }), \
		default: (fy_local_to_generic_value((_maybe_gb)))))

#define fy_to_generic(_maybe_gb, ...) \
	((fy_generic) { .v = fy_to_generic_value((_maybe_gb), ##__VA_ARGS__) })

fy_generic fy_gb_string_vcreate(struct fy_generic_builder *gb, const char *fmt, va_list ap);
fy_generic fy_gb_string_createf(struct fy_generic_builder *gb, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

#define FYGBOPF_OP_SHIFT		0
#define FYGBOPF_OP_MASK			((1U << 8) - 1)
#define FYGBOPF_OP(x)			(((unsigned int)(x) & FYGBOPF_OP_MASK) << FYGBOPF_OP_SHIFT)

enum fy_gb_op {
	FYGBOP_CREATE_INV,
	FYGBOP_CREATE_NULL,
	FYGBOP_CREATE_BOOL,
	FYGBOP_CREATE_INT,
	FYGBOP_CREATE_FLT,
	FYGBOP_CREATE_STR,
	FYGBOP_CREATE_SEQ,
	FYGBOP_CREATE_MAP,
	FYGBOP_INSERT,
	FYGBOP_REPLACE,
	FYGBOP_APPEND,
	FYGBOP_ASSOC,
	FYGBOP_DISASSOC,
	FYGBOP_KEYS,
	FYGBOP_VALUES,
	FYGBOP_ITEMS,
	FYGBOP_CONTAINS,
	FYGBOP_CONCAT,
	FYGBOP_REVERSE,
	FYGBOP_MERGE,
	FYGBOP_UNIQUE,
	FYGBOP_SORT,
	FYGBOP_FILTER,
	FYGBOP_MAP,
	FYGBOP_MAP_FILTER,
	FYGBOP_REDUCE,
	FYGBOP_SLICE,
	FYGBOP_SLICE_PY,
	FYGBOP_GET,
	FYGBOP_GET_AT,
	FYGBOP_GET_AT_PATH,
	FYGBOP_SET,
	FYGBOP_SET_AT,
	FYGBOP_SET_AT_PATH,
	FYGBOP_PARSE,
	FYGBOP_EMIT,
};
#define FYGBOP_COUNT	(FYGBOP_EMIT + 1)

typedef bool (*fy_generic_filter_pred_fn)(struct fy_generic_builder *gb, fy_generic v);
typedef fy_generic (*fy_generic_map_xform_fn)(struct fy_generic_builder *gb, fy_generic v);
typedef fy_generic (*fy_generic_reducer_fn)(struct fy_generic_builder *gb, fy_generic acc, fy_generic v);

#if defined(__BLOCKS__)
typedef bool (^fy_generic_filter_pred_block)(struct fy_generic_builder *gb, fy_generic v);
typedef fy_generic (^fy_generic_map_xform_block)(struct fy_generic_builder *gb, fy_generic v);
typedef fy_generic (^fy_generic_reducer_block)(struct fy_generic_builder *gb, fy_generic acc, fy_generic v);
#endif

/* FYGBOP_CREATE_SEQ, FYGBOP_CREATE_MAP, FYGBOP_APPEND, FYGBOP_ASSOC, FYGBOP_DISASSOC */
/* FYGBOP_CONTAINS, FYGBOP_CONCAT, FYGBOP_REVERSE, FYGBOP_MERGE, FYGBOP_UNIQUE */
/* GYGBOP_SET */
struct fy_op_common_args {
	size_t count;			/* x2 for map */
	const fy_generic *items;
	struct fy_thread_pool *tp;
};

struct fy_op_create_scalar_args {
	struct fy_op_common_args common;
	union {
		bool bval;
		double fval;
		fy_generic_decorated_int ival;
		fy_generic_sized_string sval;
	};
};

/* FYGBOP_SORT */
struct fy_op_sort_args {
	struct fy_op_common_args common;
	int (*cmp_fn)(fy_generic a, fy_generic b);
};

/* FYGBOP_INSERT, FYGBOP_REPLACE, FYGBOP_GET_AT */
struct fy_op_insert_replace_get_set_at_args {
	struct fy_op_common_args common;
	size_t idx;
};

/* FYGBOP_KEYS, FYGBOP_VALUES, FYGBOP_ITEMS */
struct fy_op_keys_values_items_args {
	struct fy_op_common_args common;
};

/* FYGBOP_FILTER */
struct fy_op_filter_args {
	struct fy_op_common_args common;
	union {
		fy_generic_filter_pred_fn fn;
#if defined(__BLOCKS__)
		fy_generic_filter_pred_block blk;
#endif
	};
};

/* FYGBOP_MAP, FYGBOP_MAP_FILTER */
struct fy_op_map_args {
	struct fy_op_common_args common;
	union {
		fy_generic_map_xform_fn fn;
#if defined(__BLOCKS__)
		fy_generic_map_xform_block blk;
#endif
	};
};

/* FYGBOP_REDUCE */
struct fy_op_reduce_args {
	struct fy_op_common_args common;
	union {
		fy_generic_reducer_fn fn;
#if defined(__BLOCKS__)
		fy_generic_reducer_block blk;
#endif
	};
	fy_generic acc;
};

/* common for filter,map,reduce */
struct fy_op_filter_map_reduce_common {
	struct fy_op_common_args common;
	union {
		void (*fn)(void);
#if defined(__BLOCKS__)
		void (^blk)(void);
#endif
	};
};

/* FYGBOP_PARSE */
struct fy_op_parse_args {
	struct fy_op_common_args common;
	enum fy_parser_mode parser_mode;	/* FYPM_YAML, FYPM_JSON, etc. */
	bool multi_document;			/* parse multiple documents */
};

/* FYGBOP_EMIT */
struct fy_op_emit_args {
	struct fy_op_common_args common;
	enum fy_emitter_cfg_flags emit_flags;	/* emitter configuration flags */
};

enum fy_gb_op_flags {
	FYGBOPF_CREATE_SEQ	= FYGBOPF_OP(FYGBOP_CREATE_SEQ),
	FYGBOPF_CREATE_MAP	= FYGBOPF_OP(FYGBOP_CREATE_MAP),
	FYGBOPF_INSERT		= FYGBOPF_OP(FYGBOP_INSERT),
	FYGBOPF_REPLACE		= FYGBOPF_OP(FYGBOP_REPLACE),
	FYGBOPF_APPEND		= FYGBOPF_OP(FYGBOP_APPEND),
	FYGBOPF_ASSOC		= FYGBOPF_OP(FYGBOP_ASSOC),
	FYGBOPF_DISASSOC	= FYGBOPF_OP(FYGBOP_DISASSOC),
	FYGBOPF_KEYS		= FYGBOPF_OP(FYGBOP_KEYS),
	FYGBOPF_VALUES		= FYGBOPF_OP(FYGBOP_VALUES),
	FYGBOPF_ITEMS		= FYGBOPF_OP(FYGBOP_ITEMS),
	FYGBOPF_CONTAINS	= FYGBOPF_OP(FYGBOP_CONTAINS),
	FYGBOPF_CONCAT		= FYGBOPF_OP(FYGBOP_CONCAT),
	FYGBOPF_REVERSE		= FYGBOPF_OP(FYGBOP_REVERSE),
	FYGBOPF_MERGE		= FYGBOPF_OP(FYGBOP_MERGE),
	FYGBOPF_UNIQUE		= FYGBOPF_OP(FYGBOP_UNIQUE),
	FYGBOPF_SORT		= FYGBOPF_OP(FYGBOP_SORT),
	FYGBOPF_FILTER		= FYGBOPF_OP(FYGBOP_FILTER),
	FYGBOPF_MAP		= FYGBOPF_OP(FYGBOP_MAP),
	FYGBOPF_MAP_FILTER	= FYGBOPF_OP(FYGBOP_MAP_FILTER),
	FYGBOPF_REDUCE		= FYGBOPF_OP(FYGBOP_REDUCE),
	FYGBOPF_GET		= FYGBOPF_OP(FYGBOP_GET),
	FYGBOPF_GET_AT		= FYGBOPF_OP(FYGBOP_GET_AT),
	FYGBOPF_GET_AT_PATH	= FYGBOPF_OP(FYGBOP_GET_AT_PATH),
	FYGBOPF_SET		= FYGBOPF_OP(FYGBOP_SET),
	FYGBOPF_SET_AT		= FYGBOPF_OP(FYGBOP_SET_AT),
	FYGBOPF_SET_AT_PATH	= FYGBOPF_OP(FYGBOP_SET_AT_PATH),
	FYGBOPF_PARSE		= FYGBOPF_OP(FYGBOP_PARSE),
	FYGBOPF_EMIT		= FYGBOPF_OP(FYGBOP_EMIT),
	FYGBOPF_DONT_INTERNALIZE= FY_BIT(16),			// do not internalize items
	FYGBOPF_DEEP_VALIDATE	= FY_BIT(17),			// perform deep validation
	FYGBOPF_NO_CHECKS	= FY_BIT(18),			// do not perform any checks on the items
	FYGBOPF_PARALLEL	= FY_BIT(19),			// perform in parallel
	FYGBOPF_MAP_ITEM_COUNT	= FY_BIT(20),			// the count is items, not pairs for mappings
	FYGBOPF_BLOCK_FN	= FY_BIT(21),			// the function is a block
	FYGBOPF_CREATE_PATH	= FY_BIT(23),			// create intermediate paths like mkdir -p
	FYGBOPF_UNSIGNED	= FY_BIT(23),			// int scalar created is unsigned (note same as CREATE_PATH)
};

struct fy_generic_op_args {
	union {
		/* this is common to all */
		struct fy_op_common_args common;
		struct fy_op_create_scalar_args scalar;
		struct fy_op_sort_args sort;
		struct fy_op_insert_replace_get_set_at_args insert_replace_get_set_at;
		struct fy_op_keys_values_items_args keys_value_items;
		struct fy_op_filter_args filter;
		struct fy_op_map_args map_filter;
		struct fy_op_reduce_args reduce;
		struct fy_op_filter_map_reduce_common filter_map_reduce_common;
		struct fy_op_parse_args parse;
		struct fy_op_emit_args emit;
	};
};

fy_generic fy_generic_op_args(struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
			      fy_generic in, const struct fy_generic_op_args *args);

fy_generic fy_generic_op(struct fy_generic_builder *gb, enum fy_gb_op_flags flags, ...);

static inline fy_generic
fy_gb_collection_create(struct fy_generic_builder *gb, bool is_map, size_t count, const fy_generic *items, bool internalize)
{
	enum fy_gb_op_flags flags;

	flags = (!is_map ? FYGBOPF_CREATE_SEQ : FYGBOPF_CREATE_MAP) |
		(!internalize ? FYGBOPF_DONT_INTERNALIZE : 0);

	return fy_generic_op(gb, flags, count, items);
}

static inline fy_generic
fy_gb_sequence_create_i(struct fy_generic_builder *gb, bool internalize, size_t count, const fy_generic *items)
{
	return fy_gb_collection_create(gb, false, count, items, internalize);
}

static inline fy_generic
fy_gb_sequence_create(struct fy_generic_builder *gb, size_t count, const fy_generic *items)
{
	return fy_gb_collection_create(gb, false, count, items, true);
}

static inline fy_generic
fy_gb_mapping_create_i(struct fy_generic_builder *gb, bool internalize, size_t count, const fy_generic *pairs)
{
	return fy_gb_collection_create(gb, true, count, pairs, internalize);
}

static inline fy_generic
fy_gb_mapping_create(struct fy_generic_builder *gb, size_t count, const fy_generic *pairs)
{
	return fy_gb_mapping_create_i(gb, true, count, pairs);
}

fy_generic fy_gb_indirect_create(struct fy_generic_builder *gb, const fy_generic_indirect *gi);

fy_generic fy_gb_alias_create(struct fy_generic_builder *gb, fy_generic anchor);

#define _FY_CPP_GBITEM_ONE(_gb, arg) fy_gb_to_generic(_gb, arg)
#define _FY_CPP_GBITEM_LATER_ARG(_gb, arg) , _FY_CPP_GBITEM_ONE(_gb, arg)
#define _FY_CPP_GBITEM_LIST(_gb, ...) FY_CPP_MAP2(_gb, _FY_CPP_GBITEM_LATER_ARG, __VA_ARGS__)

#define _FY_CPP_VA_GBITEMS(_gb, ...)          \
	_FY_CPP_GBITEM_ONE(_gb, FY_CPP_FIRST(__VA_ARGS__)) \
	_FY_CPP_GBITEM_LIST(_gb, FY_CPP_REST(__VA_ARGS__))

#define FY_CPP_VA_GBITEMS(_count, _gb, ...) \
	((fy_generic [(_count)]) { _FY_CPP_VA_GBITEMS((_gb), __VA_ARGS__) })

/* this is scary, but make the thing to work when there are no arguments
 *
 * no arguments: -> ((0) ? : fy_seq_empty_value)
 * args -> (0 + 1) ? fy_gb_sequence...() : fy_seq_empty_value)
 */
#define fy_gb_sequence_value(_gb, ...) \
	((0 __VA_OPT__(+1)) ? \
		__VA_OPT__(fy_gb_sequence_create((_gb), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GBITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), (_gb), __VA_ARGS__)).v) : \
		fy_seq_empty_value)

#define fy_gb_sequence(_gb, ...) \
	((fy_generic) { .v = fy_gb_sequence_value((_gb) __VA_OPT__(,) __VA_ARGS__) })

#define fy_gb_mapping_value(_gb, ...) \
	((0 __VA_OPT__(+1)) ? \
		__VA_OPT__(fy_gb_mapping_create((_gb), \
			FY_CPP_VA_COUNT(__VA_ARGS__) / 2, \
			FY_CPP_VA_GBITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), (_gb), __VA_ARGS__)).v) : \
		fy_map_empty_value)

#define fy_gb_mapping(_gb, ...) \
	((fy_generic) { .v = fy_gb_mapping_value((_gb), __VA_ARGS__) })

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

fy_generic fy_validate_out_of_place(fy_generic v);

static inline fy_generic fy_validate(fy_generic v)
{
	/* if it's invalid or in place, just return it */
	if (fy_generic_is_invalid(v) || fy_generic_is_in_place(v))
		return v;

	return fy_validate_out_of_place(v);
}

int fy_validate_array(size_t count, const fy_generic *vp);

fy_generic fy_gb_validate_out_of_place(struct fy_generic_builder *gb, fy_generic v);

static inline fy_generic fy_gb_validate(struct fy_generic_builder *gb, fy_generic v)
{
	/* if it's invalid or in place, just return it */
	if (fy_generic_is_invalid(v) || fy_generic_is_in_place(v))
		return v;

	return fy_gb_validate_out_of_place(gb, v);
}

int fy_gb_validate_array(struct fy_generic_builder *gb, size_t count, const fy_generic *vp);

fy_generic fy_generic_relocate(void *start, void *end, fy_generic v, ptrdiff_t d);

enum fy_generic_schema fy_gb_get_schema(struct fy_generic_builder *gb);
void fy_gb_set_schema(struct fy_generic_builder *gb, enum fy_generic_schema schema);

int fy_gb_set_schema_from_parser_mode(struct fy_generic_builder *gb, enum fy_parser_mode parser_mode);

void fy_generic_dump_primitive(FILE *fp, int level, fy_generic vv);

//////////////////////////////////////////////////////

#define fy_len(_colv) \
	(fy_generic_len((_colv)))

#define fy_get_default(_colv, _key, _dv) \
	(fy_generic_get_default((_colv), (_key), (_dv)))

#define fy_get_typed(_colv, _key, _type) \
	(fy_generic_get_default((_colv), (_key), fy_generic_get_type_default(_type)))

#define fy_get(_colv, _key, _dv) \
	(fy_get_default((_colv), (_key), (_dv)))

#define fy_get_at_default(_colv, _idx, _dv) \
	(fy_generic_get_at_default((_colv), (_idx), (_dv)))

#define fy_get_at_typed(_colv, _idx, _type) \
	(fy_generic_get_at_default((_colv), (_idx), fy_generic_get_type_default(_type)))

#define fy_get_at(_colv, _idx, _dv) \
	(fy_get_at_default((_colv), (_idx), (_dv)))

#define fy_get_key_at_default(_colv, _idx, _dv) \
	(fy_generic_get_key_at_default((_colv), (_idx), (_dv)))

#define fy_get_key_at_typed(_colv, _idx, _type) \
	(fy_generic_get_key_at_default((_colv), (_idx), fy_generic_get_type_default(_type)))

#define fy_get_key_at(_colv, _idx, _dv) \
	(fy_get_key_at_default((_colv), (_idx), (_dv)))

#define fy_cast_default(_v, _dv) \
	(fy_generic_cast_default((_v), (_dv)))

#define fy_cast_typed(_v, _type) \
	(fy_generic_cast((_v), (_type)))

#define fy_cast(_v, _dv) \
	(fy_cast_default((_v), (_dv)))

#define fy_castp_default(_v, _dv) \
	(fy_genericp_cast_default((_v), (_dv)))

#define fy_castp_typed(_v, _type) \
	(fy_genericp_cast_typed((_v), (_type)))

#define fy_castp(_v, _dv) \
	(fy_genericp_cast_default((_v), (_dv)))

#define fy_sequence_value_helper(_maybe_gb, ...) \
	(_Generic((_maybe_gb), \
		  struct fy_generic_builder *: ({ fy_gb_sequence_value(fy_gb_or_NULL(_maybe_gb), ##__VA_ARGS__); }), \
		  default: (fy_local_sequence_value((_maybe_gb), ##__VA_ARGS__))))

#define fy_sequence_value(...) \
	((0 __VA_OPT__(+1)) ? \
		__VA_OPT__(fy_sequence_value_helper(__VA_ARGS__)) : \
		fy_seq_empty_value)

#define fy_sequence(...) \
	((fy_generic) { .v = fy_sequence_value(__VA_ARGS__) })


#define fy_mapping_value_helper(_maybe_gb, ...) \
	(_Generic((_maybe_gb), \
		  struct fy_generic_builder *: ({ fy_gb_mapping_value(fy_gb_or_NULL(_maybe_gb), ##__VA_ARGS__); }), \
		  default: (fy_local_mapping_value((_maybe_gb), ##__VA_ARGS__))))

#define fy_mapping_value(...) \
	((0 __VA_OPT__(+1)) ? \
		__VA_OPT__(fy_mapping_value_helper(__VA_ARGS__)) : \
		fy_map_empty_value)

#define fy_mapping(...) \
	((fy_generic) { .v = fy_mapping_value(__VA_ARGS__) })

#define fy_value(_maybe_gb, ...) \
	((fy_generic) { .v = fy_to_generic_value((_maybe_gb), ##__VA_ARGS__) })

#define fy_inplace_value(_v) \
	(fy_to_generic_inplace(_v))

#define fy_is_inplace(_v) \
	(fy_generic_is_in_place(v))

#define fy_get_type(_v) \
	(fy_generic_get_type(_v))

#define fy_compare(_a, _b) \
	(fy_generic_compare(fy_value(_a), fy_value(_b)))

/* ops */

#define fy_gb_create_mapping(_gb, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_CREATE_MAP | FYGBOPF_MAP_ITEM_COUNT, \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

#define fy_gb_create_sequence(_gb, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_CREATE_SEQ, \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

#define fy_gb_insert(_gb, _col, _idx, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_INSERT | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_idx)))

#define fy_gb_replace(_gb, _col, _idx, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_REPLACE | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_idx)))

#define fy_gb_append(_gb, _col, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_APPEND | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

#define fy_gb_assoc(_gb, _map, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_ASSOC | FYGBOPF_MAP_ITEM_COUNT, (_map), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

#define fy_gb_disassoc(_gb, _map, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_DISASSOC, (_map), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

#define fy_gb_keys(_gb, _map) \
	(fy_generic_op((_gb), FYGBOPF_KEYS, (_map)))

#define fy_gb_values(_gb, _map) \
	(fy_generic_op((_gb), FYGBOPF_VALUES, (_map)))

#define fy_gb_items(_gb, _map) \
	(fy_generic_op((_gb), FYGBOPF_ITEMS, (_map)))

#define fy_gb_contains(_gb, _col, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_CONTAINS | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

#define fy_gb_concat(_gb, _col, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_CONCAT | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

#define fy_gb_reverse(_gb, _col, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_REVERSE | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

#define fy_gb_merge(_gb, _map, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_MERGE | FYGBOPF_MAP_ITEM_COUNT, (_map), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

#define fy_gb_unique(_gb, _col, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_UNIQUE | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

#define fy_gb_sort(_gb, _col, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_SORT | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

#define fy_gb_filter(_gb, _col, _fn, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_FILTER | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_fn)))

#define fy_gb_pfilter(_gb, _col, _tp, _fn, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_FILTER | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_PARALLEL, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_tp), (_fn)))

#define fy_gb_map(_gb, _col, _fn, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_MAP | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_fn)))

#define fy_gb_pmap(_gb, _col, _tp, _fn, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_MAP | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_PARALLEL, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_tp), (_fn)))

#define fy_gb_reduce(_gb, _col, _acc, _fn, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_REDUCE | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_fn), fy_value(_acc)))

#define fy_gb_preduce(_gb, _col, _acc, _tp, _fn, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_REDUCE | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_PARALLEL, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_tp), (_fn), fy_value(_acc)))

#define fy_gb_get_at_path(_gb, _in, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_GET_AT_PATH, (_in), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

#define fy_local_get_at_path(_in, ...) \
	({ \
		__typeof__(_in) __in = (_in); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_GET_AT_PATH, __in, _count, _items); \
	})

#define fy_get_at_path(...) (FY_CPP_THIRD(__VA_ARGS__, fy_gb_get_at_path, fy_local_get_at_path)(__VA_ARGS__))

#define fy_gb_set(_gb, _col, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_SET | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

/* iterators */
#define fy_foreach(_v, _col) \
	for (struct { size_t i; size_t len; } FY_LUNIQUE(_iter) = { 0, fy_len(_col) }; \
	     FY_LUNIQUE(_iter).i < FY_LUNIQUE(_iter).len && \
		(((_v) = fy_get_key_at_typed((_col), FY_LUNIQUE(_iter).i, __typeof__(_v))), 1); \
	FY_LUNIQUE(_iter).i++)

#define FY_GENERIC_BUILDER_IN_PLACE_MAX_SIZE	65536

/* retry op, until it either succeeds, or fails without an allocation failure
 * we will abort when we hit the maximum in place size
 * This is safe because all values are immutable and there are no side-effects
 * when an op fails.
 * We also try to save and restore stack so that we don't keep around stack
 * buffers that were too small.
 */
#define FY_LOCAL_OP(...) \
	({ \
		size_t _sz = FY_GENERIC_BUILDER_LINEAR_IN_PLACE_MIN_SIZE; \
		char *_buf = NULL; \
		struct fy_generic_builder *_gb = NULL; \
		void *_stack_save; \
		fy_generic _v = fy_invalid; \
		\
		_stack_save = FY_STACK_SAVE(); \
		for (;;) { \
			_buf = alloca(_sz); \
			_gb = fy_generic_builder_create_in_place(FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER, NULL, _buf, _sz); \
			_v = fy_generic_op(_gb __VA_OPT__(,) __VA_ARGS__); \
			if (fy_generic_is_valid(_v)) \
				break; \
			FY_STACK_RESTORE(_stack_save); \
			if (!fy_gb_allocation_failures(_gb) || _sz > FY_GENERIC_BUILDER_IN_PLACE_MAX_SIZE) \
				break; \
			_sz = _sz * 2; \
		} \
		_v; \
	})

#define fy_create_local_sequence(...) \
	({ \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_CREATE_SEQ, _count, _items); \
	})

#define fy_create_local_mapping(...) \
	({ \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_CREATE_MAP | FYGBOPF_MAP_ITEM_COUNT, _count, _items); \
	})

#define fy_local_insert(_col, _idx, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		__typeof__(_idx) __idx = (_idx); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_INSERT | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items, __idx); \
	})

#define fy_local_replace( _col, _idx, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		__typeof__(_idx) __idx = (_idx); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_REPLACE | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items, __idx); \
	})

#define fy_local_append( _col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_APPEND | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

#define fy_local_assoc(_map, ...) \
	({ \
		__typeof__(_map) __map = (_map); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_ASSOC | FYGBOPF_MAP_ITEM_COUNT, __map, _count, _items); \
	})

#define fy_local_disassoc(_map, ...) \
	({ \
		__typeof__(_map) __map = (_map); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_DISASSOC, __map, _count, _items); \
	})

#define fy_local_keys(_map) \
	({ \
		__typeof__(_map) __map = (_map); \
		FY_LOCAL_OP(FYGBOPF_KEYS, __map, 0, NULL); \
	})

#define fy_local_values(_map) \
	({ \
		__typeof__(_map) __map = (_map); \
		FY_LOCAL_OP(FYGBOPF_VALUES, __map, 0, NULL); \
	})

#define fy_local_items(_map) \
	({ \
		__typeof__(_map) __map = (_map); \
		FY_LOCAL_OP(FYGBOPF_ITEMS, __map, 0, NULL); \
	})

#define fy_local_contains(_col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_CONTAINS | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

#define fy_local_concat(_col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_CONCAT | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

#define fy_local_reverse(_col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_REVERSE | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

#define fy_local_merge(_col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_MERGE | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

#define fy_local_unique(_col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_UNIQUE | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

#define fy_local_sort(_col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_SORT | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

#define fy_local_filter(_col, _fn, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_FILTER | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items, (_fn)); \
	})

#define fy_local_pfilter(_col, _tp, _fn, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_FILTER | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_PARALLEL, \
				__col, _count, _items, (_tp), (_fn)); \
	})

#define fy_local_map(_col, _fn, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_MAP | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items, (_fn)); \
	})

#define fy_local_pmap(_col, _tp, _fn, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_MAP | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_PARALLEL, \
				__col, _count, _items, (_tp), (_fn)); \
	})

#define fy_local_reduce(_col, _acc, _fn, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_REDUCE | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items, (_fn), fy_value(_acc)); \
	})

#define fy_local_preduce(_col, _acc, _tp, _fn, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_REDUCE | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_PARALLEL, \
			__col, _count, _items, (_tp), (_fn), fy_value(_acc)); \
	})

#define fy_local_set(_col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_SET | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

/* lambdas */

#ifdef FY_HAVE_NESTED_FUNC_LAMBDAS

#define fy_gb_filter_lambda(_gb, _col, _expr) \
	({ \
		fy_generic_filter_pred_fn _fn = ({ \
			bool __fy_pred_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_pred_fn; }); \
		fy_gb_filter((_gb), (_col), _fn); \
	})

#define fy_local_filter_lambda(_col, _expr) \
	({ \
		fy_generic_filter_pred_fn _fn = ({ \
			bool __fy_pred_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_pred_fn; }); \
		fy_local_filter((_col), _fn); \
	})

#define fy_gb_pfilter_lambda(_gb, _col, _tp, _expr) \
	({ \
		fy_generic_filter_pred_fn _fn = ({ \
			bool __fy_pred_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_pred_fn; }); \
		fy_gb_pfilter((_gb), (_col), (_tp), _fn); \
	})

#define fy_local_pfilter_lambda(_col, _tp, _expr) \
	({ \
		fy_generic_filter_pred_fn _fn = ({ \
			bool __fy_pred_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_pred_fn; }); \
		fy_local_pfilter((_col), (_tp), _fn); \
	})

#define fy_gb_map_lambda(_gb, _col, _expr) \
	({ \
		fy_generic_map_xform_fn _fn = ({ \
			fy_generic __fy_xform_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_xform_fn; }); \
		fy_gb_map((_gb), (_col), _fn); \
	})

#define fy_local_map_lambda(_col, _expr) \
	({ \
		fy_generic_map_xform_fn _fn = ({ \
			fy_generic __fy_xform_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_xform_fn; }); \
		fy_local_map((_col), _fn); \
	})

#define fy_gb_pmap_lambda(_gb, _col, _tp, _expr) \
	({ \
		fy_generic_map_xform_fn _fn = ({ \
			fy_generic __fy_xform_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_xform_fn; }); \
		fy_gb_pmap((_gb), (_col), (_tp), _fn); \
	})

#define fy_local_pmap_lambda(_col, _tp, _expr) \
	({ \
		fy_generic_map_xform_fn _fn = ({ \
			fy_generic __fy_xform_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_xform_fn; }); \
		fy_local_pmap((_col), (_tp), _fn); \
	})

#define fy_gb_reduce_lambda(_gb, _col, _acc, _expr) \
	({ \
		fy_generic_reducer_fn _fn = ({ \
			fy_generic __fy_reducer_fn(struct fy_generic_builder *gb, fy_generic acc, \
						   fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_reducer_fn; }); \
		fy_gb_reduce((_gb), (_col), (_acc), _fn); \
	})

#define fy_local_reduce_lambda(_col, _acc, _expr) \
	({ \
		fy_generic_reducer_fn _fn = ({ \
			fy_generic __fy_reducer_fn(struct fy_generic_builder *gb, fy_generic acc, \
						   fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_reducer_fn; }); \
		fy_local_reduce((_col), (_acc), _fn); \
	})

#define fy_gb_preduce_lambda(_gb, _col, _acc, _tp, _expr) \
	({ \
		fy_generic_reducer_fn _fn = ({ \
			fy_generic __fy_reducer_fn(struct fy_generic_builder *gb, fy_generic acc, \
						   fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_reducer_fn; }); \
		fy_gb_preduce((_gb), (_col), (_acc), (_tp), _fn); \
	})

#define fy_local_preduce_lambda(_col, _acc, _tp, _expr) \
	({ \
		fy_generic_reducer_fn _fn = ({ \
			fy_generic __fy_reducer_fn(struct fy_generic_builder *gb, fy_generic acc, \
						   fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_reducer_fn; }); \
		fy_local_preduce((_col), (_acc), (_tp), _fn); \
	})

#endif

#ifdef FY_HAVE_BLOCK_LAMBDAS

#define fy_gb_filter_lambda(_gb, _col, _block_body) \
	({ \
		fy_generic_filter_pred_block _block = \
			(^bool(struct fy_generic_builder *gb, fy_generic v) { \
				return _block_body ; \
			}); \
		fy_generic_op((_gb), FYGBOPF_FILTER | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_BLOCK_FN, \
				(_col), 0, NULL, _block); \
	})

#define fy_local_filter_lambda(_col, _block_body) \
	({ \
		fy_generic_filter_pred_block _block = \
			(^bool(struct fy_generic_builder *gb, fy_generic v) { \
				return _block_body ; \
			}); \
		FY_LOCAL_OP(FYGBOPF_FILTER | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_BLOCK_FN, \
				(_col), 0, NULL, _block); \
	})

#define fy_gb_pfilter_lambda(_gb, _col, _tp, _block_body) \
	({ \
		fy_generic_filter_pred_block _block = \
			(^bool(struct fy_generic_builder *gb, fy_generic v) { \
				return _block_body ; \
			}); \
		fy_generic_op((_gb), FYGBOPF_FILTER | FYGBOPF_MAP_ITEM_COUNT | \
				     FYGBOPF_BLOCK_FN | FYGBOPF_PARALLEL, \
				(_col), 0, NULL, (_tp), _block); \
	})

#define fy_local_pfilter_lambda(_col, _tp, _block_body) \
	({ \
		fy_generic_filter_pred_block _block = \
			(^bool(struct fy_generic_builder *gb, fy_generic v) { \
				return _block_body ; \
			}); \
		FY_LOCAL_OP(FYGBOPF_FILTER | FYGBOPF_MAP_ITEM_COUNT | \
			    FYGBOPF_BLOCK_FN | FYGBOPF_PARALLEL, \
				(_col), 0, NULL, (_tp), _block); \
	})

#define fy_gb_map_lambda(_gb, _col, _block_body) \
	({ \
		fy_generic_map_xform_block _block = \
			(^fy_generic(struct fy_generic_builder *gb, fy_generic v) { \
				return _block_body ; \
			}); \
		fy_generic_op((_gb), FYGBOPF_MAP | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_BLOCK_FN, \
				(_col), 0, NULL, _block); \
	})

#define fy_local_map_lambda(_col, _block_body) \
	({ \
		fy_generic_map_xform_block _block = \
			(^fy_generic(struct fy_generic_builder *gb, fy_generic v) { \
				return _block_body ; \
			}); \
		FY_LOCAL_OP(FYGBOPF_MAP | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_BLOCK_FN, \
				(_col), 0, NULL, _block); \
	})

#define fy_gb_pmap_lambda(_gb, _col, _tp, _block_body) \
	({ \
		fy_generic_map_xform_block _block = \
			(^fy_generic(struct fy_generic_builder *gb, fy_generic v) { \
				return _block_body ; \
			}); \
		fy_generic_op((_gb), FYGBOPF_MAP | FYGBOPF_MAP_ITEM_COUNT | \
				     FYGBOPF_BLOCK_FN | FYGBOPF_PARALLEL, \
				(_col), 0, NULL, (_tp), _block); \
	})

#define fy_local_pmap_lambda(_col, _tp, _block_body) \
	({ \
		fy_generic_map_xform_block _block = \
			(^fy_generic(struct fy_generic_builder *gb, fy_generic v) { \
				return _block_body ; \
			}); \
		FY_LOCAL_OP(FYGBOPF_MAP | FYGBOPF_MAP_ITEM_COUNT | \
			    FYGBOPF_BLOCK_FN | FYGBOPF_PARALLEL, \
				(_col), 0, NULL, (_tp), _block); \
	})

#define fy_gb_reduce_lambda(_gb, _col, _acc, _block_body) \
	({ \
		fy_generic_reducer_block _block = \
			(^fy_generic(struct fy_generic_builder *gb, fy_generic acc, fy_generic v) { \
				return _block_body ; \
			}); \
		fy_generic_op((_gb), FYGBOPF_REDUCE | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_BLOCK_FN, \
				(_col), 0, NULL, _block, fy_value(_acc)); \
	})

#define fy_local_reduce_lambda(_col, _acc, _block_body) \
	({ \
		fy_generic_reducer_block _block = \
			(^fy_generic(struct fy_generic_builder *gb, fy_generic acc, fy_generic v) { \
				return _block_body ; \
			}); \
		FY_LOCAL_OP(FYGBOPF_REDUCE | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_BLOCK_FN, \
				(_col), 0, NULL, _block, fy_value(_acc)); \
	})

#define fy_gb_preduce_lambda(_gb, _col, _acc, _tp, _block_body) \
	({ \
		fy_generic_reducer_block _block = \
			(^fy_generic(struct fy_generic_builder *gb, fy_generic acc, fy_generic v) { \
				return _block_body ; \
			}); \
		fy_generic_op((_gb), FYGBOPF_REDUCE | FYGBOPF_MAP_ITEM_COUNT | \
				     FYGBOPF_BLOCK_FN | FYGBOPF_PARALLEL, \
				(_col), 0, NULL, (_tp), _block, fy_value(_acc)); \
	})

#define fy_local_preduce_lambda(_col, _acc, _tp, _block_body) \
	({ \
		fy_generic_reducer_block _block = \
			(^fy_generic(struct fy_generic_builder *gb, fy_generic acc, fy_generic v) { \
				return _block_body ; \
			}); \
		FY_LOCAL_OP(FYGBOPF_REDUCE | FYGBOPF_MAP_ITEM_COUNT | \
			    FYGBOPF_BLOCK_FN | FYGBOPF_PARALLEL, \
				(_col), 0, NULL, (_tp), _block, fy_value(_acc)); \
	})

#endif

/* now the grand finale */
#define FY_GB_OR_LOCAL_OP(_gb_or_NULL, _flags, ...) \
	((_gb_or_NULL) ? \
		fy_generic_op((_gb_or_NULL), (_flags) __VA_OPT__(,) __VA_ARGS__) : \
		FY_LOCAL_OP((_flags) __VA_OPT__(,) __VA_ARGS__))

#define FY_GB_OR_LOCAL_COL(_flags, _gb_or_first, ...) \
	({ \
		struct fy_generic_builder *_gb = fy_gb_or_NULL(_gb_or_first); \
		const fy_generic _col = fy_first_non_gb(_gb_or_first __VA_OPT__(,) __VA_ARGS__ , fy_invalid); \
		FY_GB_OR_LOCAL_OP(_gb, (_flags), _col, 0, NULL); \
	})

#define FY_GB_OR_LOCAL_COL_COUNT_ITEMS(_flags, _gb_or_first, ...) \
	({ \
		struct fy_generic_builder *_gb = fy_gb_or_NULL(_gb_or_first); \
		const fy_generic _col = fy_first_non_gb(_gb_or_first __VA_OPT__(,) __VA_ARGS__); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__) - (_gb != NULL); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__) + (_gb != NULL); \
		FY_GB_OR_LOCAL_OP(_gb, (_flags), _col, _count, _items); \
	})

#define FY_GB_OR_LOCAL_COL_IDX_COUNT_ITEMS(_flags, _gb_or_first, ...) \
	({ \
		struct fy_generic_builder *_gb = fy_gb_or_NULL(_gb_or_first); \
		const fy_generic _col = fy_first_non_gb(_gb_or_first, __VA_ARGS__); \
		size_t _idx = fy_second_non_gb(_gb_or_first, __VA_ARGS__); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__) - 1 - (_gb != NULL); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__) + 1 + (_gb != NULL); \
		FY_GB_OR_LOCAL_OP(_gb, (_flags), _col, _count, _items, _idx); \
	})

#define fy_insert(_first, ...) \
	FY_GB_OR_LOCAL_COL_IDX_COUNT_ITEMS(FYGBOPF_INSERT | FYGBOPF_MAP_ITEM_COUNT, _first __VA_OPT__(,) __VA_ARGS__)

#define fy_replace(_first, ...) \
	FY_GB_OR_LOCAL_COL_IDX_COUNT_ITEMS(FYGBOPF_REPLACE | FYGBOPF_MAP_ITEM_COUNT, _first __VA_OPT__(,) __VA_ARGS__)

#define fy_append(_first, ...) \
	FY_GB_OR_LOCAL_COL_COUNT_ITEMS(FYGBOPF_APPEND | FYGBOPF_MAP_ITEM_COUNT, _first __VA_OPT__(,) __VA_ARGS__)

#define fy_assoc(_first, ...) \
	FY_GB_OR_LOCAL_COL_COUNT_ITEMS(FYGBOPF_ASSOC | FYGBOPF_MAP_ITEM_COUNT, _first __VA_OPT__(,) __VA_ARGS__)

#define fy_disassoc(_first, ...) \
	FY_GB_OR_LOCAL_COL_COUNT_ITEMS(FYGBOPF_DISASSOC, _first __VA_OPT__(,) __VA_ARGS__)

#define fy_keys(_first, ...) \
	FY_GB_OR_LOCAL_COL(FYGBOPF_KEYS, _first __VA_OPT__(,) __VA_ARGS__)

#define fy_values(_first, ...) \
	FY_GB_OR_LOCAL_COL(FYGBOPF_VALUES, _first __VA_OPT__(,) __VA_ARGS__)

#define fy_items(_first, ...) \
	FY_GB_OR_LOCAL_COL(FYGBOPF_ITEMS, _first __VA_OPT__(,) __VA_ARGS__)

#define fy_contains(_first, ...) \
	(FY_GB_OR_LOCAL_COL_COUNT_ITEMS(FYGBOPF_CONTAINS | FYGBOPF_MAP_ITEM_COUNT, \
					_first __VA_OPT__(,) __VA_ARGS__))

#define fy_concat(_first, ...) \
	FY_GB_OR_LOCAL_COL_COUNT_ITEMS(FYGBOPF_CONCAT, _first __VA_OPT__(,) __VA_ARGS__)

#define fy_reverse(_first, ...) \
	FY_GB_OR_LOCAL_COL(FYGBOPF_REVERSE, _first __VA_OPT__(,) __VA_ARGS__)

#define fy_merge(_first, ...) \
	FY_GB_OR_LOCAL_COL_COUNT_ITEMS(FYGBOPF_MERGE, _first __VA_OPT__(,) __VA_ARGS__)

#define fy_unique(_first, ...) \
	FY_GB_OR_LOCAL_COL(FYGBOPF_UNIQUE, _first __VA_OPT__(,) __VA_ARGS__)

#define fy_sort(_first, ...) \
	FY_GB_OR_LOCAL_COL(FYGBOPF_SORT, _first __VA_OPT__(,) __VA_ARGS__)

#define fy_filter(...) (FY_CPP_FOURTH(__VA_ARGS__, fy_gb_filter, fy_local_filter)(__VA_ARGS__))
#define fy_pfilter(...) (FY_CPP_FIFTH(__VA_ARGS__, fy_gb_pfilter, fy_local_pfilter)(__VA_ARGS__))

#define fy_map(...) (FY_CPP_FOURTH(__VA_ARGS__, fy_gb_map, fy_local_map)(__VA_ARGS__))
#define fy_pmap(...) (FY_CPP_FIFTH(__VA_ARGS__, fy_gb_pmap, fy_local_pmap)(__VA_ARGS__))

#define fy_reduce(...) (FY_CPP_FIFTH(__VA_ARGS__, fy_gb_reduce, fy_local_reduce)(__VA_ARGS__))
#define fy_preduce(...) (FY_CPP_SIXTH(__VA_ARGS__, fy_gb_preduce, fy_local_preduce)(__VA_ARGS__))

/*
 * Lambda variants - fy_filter_lambda, fy_map_lambda, fy_reduce_lambda
 *
 *   - Clang: Blocks (requires -fblocks -lBlocksRuntime)
 *   - GCC: Nested function pointers (standard extension)
 */

#ifdef FY_HAVE_LAMBDAS

#define fy_filter_lambda(...) (FY_CPP_FOURTH(__VA_ARGS__, fy_gb_filter_lambda, fy_local_filter_lambda)(__VA_ARGS__))
#define fy_pfilter_lambda(...) (FY_CPP_FIFTH(__VA_ARGS__, fy_gb_pfilter_lambda, fy_local_pfilter_lambda)(__VA_ARGS__))

#define fy_map_lambda(...) (FY_CPP_FOURTH(__VA_ARGS__, fy_gb_map_lambda, fy_local_map_lambda)(__VA_ARGS__))
#define fy_pmap_lambda(...) (FY_CPP_FIFTH(__VA_ARGS__, fy_gb_pmap_lambda, fy_local_pmap_lambda)(__VA_ARGS__))

#define fy_reduce_lambda(...) (FY_CPP_FIFTH(__VA_ARGS__, fy_gb_reduce_lambda, fy_local_reduce_lambda)(__VA_ARGS__))
#define fy_preduce_lambda(...) (FY_CPP_SIXTH(__VA_ARGS__, fy_gb_preduce_lambda, fy_local_preduce_lambda)(__VA_ARGS__))

#endif

#define fy_set(_first, ...) \
	FY_GB_OR_LOCAL_COL_COUNT_ITEMS(FYGBOPF_SET | FYGBOPF_MAP_ITEM_COUNT, _first __VA_OPT__(,) __VA_ARGS__)

#endif
