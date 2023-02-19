/*
 * libfyaml-generic.h - Generic type system public API
 * Copyright (c) 2023-2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef LIBFYAML_GENERIC_H
#define LIBFYAML_GENERIC_H

#include <stdalign.h>
#include <assert.h>
#include <math.h>
#include <float.h>

#include <libfyaml/libfyaml-util.h>
#include <libfyaml/libfyaml-core.h>
#include <libfyaml/libfyaml-align.h>
#include <libfyaml/libfyaml-endian.h>
#include <libfyaml/libfyaml-vlsize.h>
#include <libfyaml/libfyaml-atomics.h>
#include <libfyaml/libfyaml-dociter.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DOC: Generic runtime type system
 *
 * A compact, efficient runtime type system for representing arbitrary YAML
 * and JSON values in C, bringing Python/Rust-like dynamically-typed data
 * literals to C programs.
 *
 * The core type is ``fy_generic``, a single machine word (64 or 32 bit) that
 * encodes one of nine value types via pointer tagging:
 *
 * - **null**, **bool**, **int**, **float**, **string** — scalar types
 * - **sequence**, **mapping** — ordered arrays and key/value collections
 * - **indirect**, **alias** — YAML-specific wrappers (anchor, tag, style, …)
 *
 * Small values are stored inline in the pointer word with zero heap
 * allocation: 61-bit integers, 7-byte strings, and 32-bit floats all
 * fit in a single machine word.
 *
 * **Three API tiers for different storage lifetimes**:
 *
 * *Stack-based* (``fy_sequence(…)``, ``fy_mapping(…)``, ``fy_to_generic(x)``):
 * values live for the duration of the enclosing function.  C11 ``_Generic``
 * dispatch automatically selects the right conversion from native C types.
 * Zero heap allocation for scalars and small composite values::
 *
 *   fy_generic config = fy_mapping(
 *       "host", "localhost",
 *       "port", 8080,
 *       "tls",  true);
 *
 * *Low-level* (``fy_sequence_alloca()``, ``fy_mapping_alloca()``): build from
 * pre-constructed ``fy_generic`` arrays; the caller controls all allocation.
 *
 * *Builder* (``fy_gb_sequence()``, ``fy_gb_mapping()``): fy allocator created values
 * that survive beyond the current function.  Automatically internalises any
 * stack-based sub-values passed to it.
 *
 * **Immutability and thread safety**: generics are immutable — all operations
 * produce new values.  Multiple threads may safely read the same generic
 * concurrently without locking; only the builder's allocator requires
 * synchronisation for writes.
 *
 * **Conversion**: ``fy_document_to_generic()`` and ``fy_generic_to_document()``
 * convert between YAML document trees and generic values, enabling the generic
 * API to serve as an efficient in-memory representation for parsed YAML and JSON.
 */

/* struct fy_generic_builder - opaque generic value builder (heap-allocated) */
struct fy_generic_builder;

/**
 * enum fy_generic_type - Type discriminator for fy_generic values.
 *
 * Identifies the runtime type stored in an &fy_generic word.
 * The ordering INT < FLOAT < STRING must be preserved; internal
 * bithacks depend on consecutive placement.
 *
 * @FYGT_INVALID:  Sentinel representing an invalid or unset value.
 * @FYGT_NULL:     YAML/JSON null.
 * @FYGT_BOOL:     Boolean (true or false).
 * @FYGT_INT:      Signed or unsigned integer.
 * @FYGT_FLOAT:    Floating-point (double).
 * @FYGT_STRING:   UTF-8 string.
 * @FYGT_SEQUENCE: Ordered sequence of generic values.
 * @FYGT_MAPPING:  Key/value mapping of generic values.
 * @FYGT_INDIRECT: Value wrapped with metadata (anchor, tag, style, …).
 * @FYGT_ALIAS:    YAML alias (anchor reference).
 */
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

/**
 * fy_generic_type_is_scalar() - Test whether a type is a scalar type.
 *
 * @type: The type to test.
 *
 * Returns:
 * true if @type is one of %FYGT_NULL, %FYGT_BOOL, %FYGT_INT,
 * %FYGT_FLOAT, or %FYGT_STRING; false otherwise.
 */
static inline bool
fy_generic_type_is_scalar(const enum fy_generic_type type)
{
	return type >= FYGT_NULL && type <= FYGT_STRING;
}

/**
 * fy_generic_type_is_collection() - Test whether a type is a collection type.
 *
 * @type: The type to test.
 *
 * Returns:
 * true if @type is %FYGT_SEQUENCE or %FYGT_MAPPING; false otherwise.
 */
static inline bool
fy_generic_type_is_collection(const enum fy_generic_type type)
{
	return type >= FYGT_SEQUENCE && type <= FYGT_MAPPING;
}

/**
 * enum fy_generic_type_mask - Bitmask constants for sets of generic types.
 *
 * Each enumerator is a single bit corresponding to the matching
 * &enum fy_generic_type value.  Combine them with bitwise OR to test
 * for membership in a set of types.
 *
 * @FYGTM_INVALID:    Bit for %FYGT_INVALID.
 * @FYGTM_NULL:       Bit for %FYGT_NULL.
 * @FYGTM_BOOL:       Bit for %FYGT_BOOL.
 * @FYGTM_INT:        Bit for %FYGT_INT.
 * @FYGTM_FLOAT:      Bit for %FYGT_FLOAT.
 * @FYGTM_STRING:     Bit for %FYGT_STRING.
 * @FYGTM_SEQUENCE:   Bit for %FYGT_SEQUENCE.
 * @FYGTM_MAPPING:    Bit for %FYGT_MAPPING.
 * @FYGTM_INDIRECT:   Bit for %FYGT_INDIRECT.
 * @FYGTM_ALIAS:      Bit for %FYGT_ALIAS.
 * @FYGTM_COLLECTION: Combined mask for sequences and mappings.
 * @FYGTM_SCALAR:     Combined mask for all scalar types (null through string).
 * @FYGTM_ANY:        Combined mask for all non-invalid, non-alias types.
 */
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

/**
 * typedef fy_generic_value - Unsigned word used as the raw tagged-pointer storage.
 *
 * The low 3 bits hold the type tag; the remaining bits hold either
 * an inplace value (integer, short string, 32-bit float on 64-bit) or
 * an aligned pointer to heap/stack-allocated storage.
 */
typedef uintptr_t fy_generic_value;

/**
 * typedef fy_generic_value_signed - Signed variant of fy_generic_value.
 *
 * Used when arithmetic sign-extension of the raw word is needed,
 * e.g. when decoding inplace signed integers.
 */
typedef intptr_t fy_generic_value_signed;

/* 64-bit encoding parameters */
#define FYGT_GENERIC_BITS_64			64  /* total bits in a generic word */
#define FYGT_INT_INPLACE_BITS_64		61  /* usable integer bits when stored inplace */
#define FYGT_STRING_INPLACE_SIZE_64		6   /* max bytes of a string stored inplace (+ NUL = 7) */
#define FYGT_STRING_INPLACE_SIZE_MASK_64	7   /* mask for the 3-bit inplace string length field */
#define FYGT_SIZE_ENCODING_MAX_64		FYVL_SIZE_ENCODING_MAX_64  /* max vlencoded size on 64-bit */

/* 32-bit encoding parameters */
#define FYGT_GENERIC_BITS_32			32  /* total bits in a generic word */
#define FYGT_INT_INPLACE_BITS_32		29  /* usable integer bits when stored inplace */
#define FYGT_STRING_INPLACE_SIZE_32		2   /* max bytes of a string stored inplace */
#define FYGT_STRING_INPLACE_SIZE_MASK_32	3   /* mask for the 2-bit inplace string length field */
#define FYGT_SIZE_ENCODING_MAX_32		FYVL_SIZE_ENCODING_MAX_32  /* max vlencoded size on 32-bit */

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

/* Number of bits to shift when sign-extending an inplace integer */
#define FYGT_INT_INPLACE_SIGN_SHIFT		(FYGT_GENERIC_BITS - FYGT_INT_INPLACE_BITS)

/* Maximum value expressible by the variable-length size encoding (architecture-selected) */
#define FYGT_SIZE_ENCODING_MAX FYVL_SIZE_ENCODING_MAX

#ifdef FYGT_GENERIC_64

/**
 * FY_STRING_SHIFT7() - Build a 7-byte inplace string encoding word (64-bit).
 *
 * Packs up to seven character bytes into the upper 56 bits of a
 * &fy_generic_value in the byte order expected by the host so that
 * the low 8 bits remain free for the type tag and length field.
 * Endian-specific: little-endian stores _v0 at bits 8-15, big-endian
 * stores _v0 at bits 55-48.
 *
 * @_v0: First character byte (or 0 for padding).
 * @_v1: Second character byte.
 * @_v2: Third character byte.
 * @_v3: Fourth character byte.
 * @_v4: Fifth character byte.
 * @_v5: Sixth character byte.
 * @_v6: Seventh character byte.
 *
 * Returns:
 * A fy_generic_value with the seven bytes packed in, ready to be
 * OR-ed with the type tag and string length.
 */
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

/**
 * FY_STRING_SHIFT3() - Build a 3-byte inplace string encoding word (32-bit).
 *
 * Packs up to three character bytes into the upper 24 bits of a
 * &fy_generic_value, leaving the low 8 bits free for tag and length.
 * Endian-specific like FY_STRING_SHIFT7().
 *
 * @_v0: First character byte (or 0 for padding).
 * @_v1: Second character byte.
 * @_v2: Third character byte.
 *
 * Returns:
 * A fy_generic_value with the three bytes packed in.
 */
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

/* Number of bits used for the type tag in the low word */
#define FY_INPLACE_TYPE_SHIFT	3
/* Mask covering the 3-bit type tag */
#define FY_INPLACE_TYPE_MASK	(((fy_generic_value)1 << FY_INPLACE_TYPE_SHIFT) - 1)

/* NOTE: do not reorder — bithacks depend on the exact values below */

#define FY_NULL_V		0  /* type-tag bits for null (also used by sequence pointer) */
#define FY_SEQ_V		0  /* type-tag bits for an out-of-place sequence pointer */
#define FY_MAP_V		8  /* type-tag bits for an out-of-place mapping pointer (bit 3 set) */
/* Mask covering both the 3-bit tag and the collection discriminator bit */
#define FY_COLLECTION_MASK	(((fy_generic_value)1 << (FY_INPLACE_TYPE_SHIFT + 1)) - 1)

#define FY_BOOL_V		8   /* escape-code base value for booleans */
#define FY_BOOL_INPLACE_SHIFT	4   /* bit position of the boolean value within the escape word */

#define FY_INT_INPLACE_V	1   /* type-tag bits: integer stored inplace */
#define FY_INT_OUTPLACE_V	2   /* type-tag bits: integer stored out-of-place (pointer) */
#define FY_INT_INPLACE_SHIFT	3   /* bit position where the inplace integer value starts */

#define FY_FLOAT_INPLACE_V	3   /* type-tag bits: float stored inplace (64-bit only) */
#define FY_FLOAT_OUTPLACE_V	4   /* type-tag bits: float stored out-of-place (pointer) */

/* Bit position of the 32-bit float value within the 64-bit inplace word */
#ifdef FYGT_GENERIC_64
#define FY_FLOAT_INPLACE_SHIFT	32
#endif

#define FY_STRING_INPLACE_V	5   /* type-tag bits: short string stored inplace */
#define FY_STRING_OUTPLACE_V	6   /* type-tag bits: string stored out-of-place (pointer) */
#define FY_STRING_INPLACE_SIZE_SHIFT	4  /* bit position of the inplace string length field */

#define FY_INDIRECT_V		7   /* type-tag bits: indirect wrapper (pointer + escape codes) */

/* Escape mechanism: FYGT_INDIRECT with the escape-mark bit set encodes special constants */
#define FY_ESCAPE_SHIFT		(FY_INPLACE_TYPE_SHIFT + 1)  /* bits above the escape mark */
#define FY_ESCAPE_MASK		(((fy_generic_value)1 << FY_ESCAPE_SHIFT) - 1)  /* low escape bits */
#define FY_ESCAPE_MARK		((1 << (FY_ESCAPE_SHIFT - 1)) | FY_INDIRECT_V)  /* escape discriminator */
/* Test whether a raw value is an escape code */
#define FY_IS_ESCAPE(_v)	(((fy_generic_value)(_v) & FY_ESCAPE_MASK) == FY_ESCAPE_MARK)

#define FY_ESCAPE_NULL		0   /* escape index for the null constant */
#define FY_ESCAPE_FALSE		1   /* escape index for the false constant */
#define FY_ESCAPE_TRUE		2   /* escape index for the true constant */
#define FY_ESCAPE_COUNT		3   /* number of defined escape codes */

/* Build a raw escape-encoded fy_generic_value from an escape index */
#define FY_MAKE_ESCAPE(_v)		(((fy_generic_value)(_v) << FY_ESCAPE_SHIFT) | FY_ESCAPE_MARK)

/* Raw fy_generic_value constants for the primitive escape-encoded values */
#define fy_null_value			FY_MAKE_ESCAPE(FY_ESCAPE_NULL)
#define fy_false_value			FY_MAKE_ESCAPE(FY_ESCAPE_FALSE)
#define fy_true_value			FY_MAKE_ESCAPE(FY_ESCAPE_TRUE)
/* All-bits-set sentinel meaning "no value" / error */
#define fy_invalid_value		FY_MAKE_ESCAPE(-1)
/* Raw value for an empty (zero-element) sequence */
#define fy_seq_empty_value		((fy_generic_value)(FY_SEQ_V | 0))
/* Raw value for an empty (zero-element) mapping */
#define fy_map_empty_value		((fy_generic_value)(FY_MAP_V | 0))

/* Inclusive range of integers that fit inplace without out-of-place allocation */
#define FYGT_INT_INPLACE_MAX		((1LL << (FYGT_INT_INPLACE_BITS - 1)) - 1)
#define FYGT_INT_INPLACE_MIN		(-(1LL << (FYGT_INT_INPLACE_BITS - 1)))

/* Required alignment (bytes) for heap-allocated sequence and mapping objects */
#define FY_GENERIC_CONTAINER_ALIGN	16
/* Alias for FY_GENERIC_CONTAINER_ALIGN used for externally-visible allocations */
#define FY_GENERIC_EXTERNAL_ALIGN	FY_GENERIC_CONTAINER_ALIGN
/* Required alignment (bytes) for heap-allocated scalar objects (int, float, string) */
#define FY_GENERIC_SCALAR_ALIGN		8

/* GCC/Clang attribute spelling for container alignment */
#define FY_GENERIC_CONTAINER_ALIGNMENT FY_ALIGNED_TO(FY_GENERIC_CONTAINER_ALIGN)
/* Alias for FY_GENERIC_CONTAINER_ALIGNMENT */
#define FY_GENERIC_EXTERNAL_ALIGNMENT FY_GENERIC_CONTAINER_ALIGNMENT

/**
 * FY_MAX_ALIGNOF() - Return the larger of _Alignof(@_v) and @_min.
 *
 * Note: evaluates @_v twice; avoid expressions with side-effects.
 *
 * @_v:   Expression whose alignment is queried.
 * @_min: Minimum alignment to enforce.
 *
 * Returns:
 * The larger of _Alignof(@_v) and @_min as a size_t.
 */
#define FY_MAX_ALIGNOF(_v, _min) ((size_t)(_Alignof(_v) > (_min) ? _Alignof(_v) : (_min)))
/* Alignment for a type used as a container element, at least FY_GENERIC_CONTAINER_ALIGN */
#define FY_CONTAINER_ALIGNOF(_v) FY_MAX_ALIGNOF(_v, FY_GENERIC_CONTAINER_ALIGN)
/* Alignment for a type used as a scalar element, at least FY_GENERIC_SCALAR_ALIGN */
#define FY_SCALAR_ALIGNOF(_v) FY_MAX_ALIGNOF(_v, FY_GENERIC_SCALAR_ALIGN)

/* GCC/Clang attribute spelling for out-of-place integer storage alignment */
#define FY_INT_ALIGNMENT  FY_ALIGNED_TO(FY_SCALAR_ALIGNOF(long long))
/* GCC/Clang attribute spelling for out-of-place float storage alignment */
#define FY_FLOAT_ALIGNMENT FY_ALIGNED_TO(FY_SCALAR_ALIGNOF(double))
/* GCC/Clang attribute spelling for out-of-place string storage alignment */
#define FY_STRING_ALIGNMENT FY_ALIGNED_TO(FY_GENERIC_SCALAR_ALIGN)

/**
 * typedef fy_generic - A space-efficient tagged-union value.
 *
 * A single pointer-sized word encoding any YAML/JSON value without
 * heap allocation for small scalars (integers up to 61 bits, strings
 * up to 7 bytes on 64-bit, 32-bit floats on 64-bit).  Larger values
 * are stored out-of-place; the word holds an aligned pointer with the
 * low 3 bits used as a type tag.
 *
 * Access the raw word via .v (unsigned) or .vs (signed).  Always use
 * the provided accessor functions rather than reading the raw fields.
 */
typedef struct fy_generic {
	union {
		fy_generic_value v;
		fy_generic_value_signed vs;
	};
} fy_generic;

/* Typed fy_generic literal for YAML null */
#define fy_null		((fy_generic){ .v = fy_null_value })
/* Typed fy_generic literal for boolean false */
#define fy_false	((fy_generic){ .v = fy_false_value })
/* Typed fy_generic literal for boolean true */
#define fy_true		((fy_generic){ .v = fy_true_value })
/* Typed fy_generic sentinel meaning "no value" / error */
#define fy_invalid	((fy_generic){ .v = fy_invalid_value })
/* Typed fy_generic literal for an empty (zero-element) sequence */
#define fy_seq_empty	((fy_generic){. v = fy_seq_empty_value })
/* Typed fy_generic literal for an empty (zero-element) mapping */
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

/**
 * typedef fy_generic_indirect - Wrapper attaching YAML metadata to a generic value.
 *
 * An indirect is allocated out-of-place and pointed to by a tagged
 * &fy_generic word with type tag %FY_INDIRECT_V.  It stores the actual
 * value plus optional metadata controlled by the @flags bitmask
 * (%FYGIF_*).  An alias is encoded as an indirect with @value set to
 * %fy_invalid and @anchor holding the alias target name.  Optional
 * fields @anchor, @tag, @diag, @marker, @comment, @style and
 * @failsafe_str are present only when the corresponding %FYGIF_* bit
 * is set in @flags.
 */
typedef struct fy_generic_indirect {
	uintptr_t flags;		/* styling and existence flags */
	fy_generic value;		/* the actual value */
	fy_generic anchor;		/* string anchor or null */
	fy_generic tag;			/* string tag or null */
	fy_generic diag;		/* the diagnostics */
	fy_generic marker;		/* the marker (file, start, end) */
	fy_generic comment;		/* the comments */
	fy_generic style;		/* the original source style */
	fy_generic failsafe_str;	/* the original source failsafe string */
} fy_generic_indirect FY_GENERIC_CONTAINER_ALIGNMENT;

/* fy_generic_indirect flags — indicate which optional metadata fields are present */
#define FYGIF_VALUE		FY_BIT(0)  /* @value field is present */
#define FYGIF_ANCHOR		FY_BIT(1)  /* @anchor field is present */
#define FYGIF_TAG		FY_BIT(2)  /* @tag field is present */
#define FYGIF_ALIAS		FY_BIT(3)  /* this indirect encodes a YAML alias */
#define FYGIF_DIAG		FY_BIT(4)  /* @diag field is present */
#define FYGIF_MARKER		FY_BIT(5)  /* @marker field is present */
#define FYGIF_COMMENT		FY_BIT(6)  /* @comment field is present */
#define FYGIF_STYLE		FY_BIT(7)  /* @style field is present */
#define FYGIF_FAILSAFE_STR	FY_BIT(8)  /* @failsafe_str field is present */

/**
 * fy_generic_is_direct() - Test whether a generic value is encoded directly.
 *
 * A direct value stores its type and data entirely in the word (inplace
 * scalars, escape constants, and out-of-place pointers).  The opposite
 * of indirect (%FYGT_INDIRECT).
 *
 * @v: The generic value to test.
 *
 * Returns:
 * true if @v is not an indirect (no wrapping metadata); false otherwise.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct(fy_generic v)
{
	return (v.v & FY_ESCAPE_MASK) != FY_INDIRECT_V;
}

/**
 * fy_generic_is_indirect() - Test whether a generic value is an indirect.
 *
 * An indirect points to a &fy_generic_indirect holding the actual value
 * plus optional metadata (anchor, tag, style, diagnostics, …).
 *
 * @v: The generic value to test.
 *
 * Returns:
 * true if @v is an indirect (%FYGT_INDIRECT); false otherwise.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_indirect(fy_generic v)
{
	return !fy_generic_is_direct(v);
}

/**
 * fy_generic_is_direct_valid() - Test whether a direct generic value is not invalid.
 *
 * Only meaningful for direct (non-indirect) values.
 *
 * @v: The generic value to test.
 *
 * Returns:
 * true if @v is not the %fy_invalid sentinel; false if it is.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_valid(const fy_generic v)
{
	return v.v != fy_invalid_value;
}

/**
 * fy_generic_is_direct_invalid() - Test whether a direct generic value is invalid.
 *
 * Only meaningful for direct (non-indirect) values.
 *
 * @v: The generic value to test.
 *
 * Returns:
 * true if @v equals the %fy_invalid sentinel; false otherwise.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_invalid(const fy_generic v)
{
	return v.v == fy_invalid_value;
}

/**
 * fy_generic_resolve_ptr() - Extract the raw pointer from a non-collection generic.
 *
 * Strips the 3-bit type tag from @ptr to recover the original aligned
 * pointer for out-of-place scalars (int, float, string, indirect).
 * Not valid for collection values — use fy_generic_resolve_collection_ptr() instead.
 *
 * @ptr: An out-of-place non-collection generic value.
 *
 * Returns:
 * The 8-byte-aligned pointer stored in @ptr.
 */
static inline FY_ALWAYS_INLINE
const void *fy_generic_resolve_ptr(fy_generic ptr)
{
	/* clear the bottom 3 bits (all pointers are 8 byte aligned) */
	/* note collections have the bit 3 cleared too, so it's 16 byte aligned */
	ptr.v &= ~(uintptr_t)FY_INPLACE_TYPE_MASK;
	return (const void *)ptr.v;
}

/**
 * fy_generic_resolve_collection_ptr() - Extract the raw pointer from a collection generic.
 *
 * Strips the 4-bit collection mask (type tag + collection discriminator bit)
 * from @ptr to recover the 16-byte-aligned pointer for sequences and mappings.
 *
 * @ptr: An out-of-place sequence or mapping generic value.
 *
 * Returns:
 * The 16-byte-aligned pointer stored in @ptr.
 */
static inline FY_ALWAYS_INLINE
const void *fy_generic_resolve_collection_ptr(fy_generic ptr)
{
	/* clear the boot 3 bits (all pointers are 8 byte aligned) */
	/* note collections have the bit 3 cleared too, so it's 16 byte aligned */
	ptr.v &= ~(uintptr_t)FY_COLLECTION_MASK;
	return (const void *)ptr.v;
}

/**
 * fy_generic_relocate_ptr() - Adjust an out-of-place scalar pointer by a byte delta.
 *
 * Adds @d to the pointer embedded in @v while preserving the type tag.
 * Used when moving the backing buffer (e.g. realloc).
 * Asserts that the resulting pointer is still correctly aligned.
 *
 * @v: An out-of-place non-collection generic value.
 * @d: Byte delta to add to the embedded pointer.
 *
 * Returns:
 * A new &fy_generic with the adjusted pointer and the same type tag.
 */
static inline fy_generic fy_generic_relocate_ptr(fy_generic v, ptrdiff_t d)
{
	v.v = (fy_generic_value)((ptrdiff_t)((uintptr_t)v.v & ~(uintptr_t)FY_INPLACE_TYPE_MASK) + d);
	assert((v.v & (uintptr_t)FY_INPLACE_TYPE_MASK) == 0);
	return v;
}

/**
 * fy_generic_relocate_collection_ptr() - Adjust a collection pointer by a byte delta.
 *
 * Like fy_generic_relocate_ptr() but for sequence and mapping values,
 * stripping and restoring the wider 4-bit collection mask.
 *
 * @v: An out-of-place sequence or mapping generic value.
 * @d: Byte delta to add to the embedded pointer.
 *
 * Returns:
 * A new &fy_generic with the adjusted pointer and the same collection tag.
 */
static inline fy_generic fy_generic_relocate_collection_ptr(fy_generic v, ptrdiff_t d)
{
	v.v = (fy_generic_value)((ptrdiff_t)((uintptr_t)v.v & ~(uintptr_t)FY_COLLECTION_MASK) + d);
	assert((v.v & (uintptr_t)FY_COLLECTION_MASK) == 0);
	return v;
}

/**
 * fy_generic_get_direct_type_table() - Determine the type of a direct generic (table lookup).
 *
 * Uses a 16-entry lookup table indexed by the low 4 bits of @v to
 * classify the value.  Handles escape codes for null, true, and false.
 * Prefer fy_generic_get_direct_type_bithack() in hot paths.
 *
 * @v: A direct (non-indirect) generic value.
 *
 * Returns:
 * The &enum fy_generic_type of @v, or %FYGT_INVALID for unknown escape codes.
 */
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
/**
 * fy_generic_get_direct_type_bithack() - Determine the type of a direct generic (bithack).
 *
 * Fast branch-optimised implementation that decodes the type from the
 * low 4 bits using arithmetic rather than a table.  For INT, FLOAT,
 * and STRING it exploits the consecutive ordering of the inplace/outplace
 * tag pairs to compute the type in a single expression.  This is the
 * preferred implementation used by fy_generic_get_direct_type().
 *
 * @v: A direct (non-indirect) generic value.
 *
 * Returns:
 * The &enum fy_generic_type of @v, or %FYGT_INVALID for unknown escape codes.
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

/* fy_generic_get_direct_type() - Get the type of a direct generic value (preferred alias). */
#define fy_generic_get_direct_type(_v) \
	(fy_generic_get_direct_type_bithack((_v)))

/**
 * fy_generic_is_in_place_normal() - Test whether a generic value is stored inplace (switch impl).
 *
 * Returns true when the entire value fits inside the word with no
 * external allocation.  Uses a readable switch-based approach; prefer
 * fy_generic_is_in_place_bithack() in hot paths.
 *
 * @v: The generic value to test.
 *
 * Returns:
 * true if @v requires no heap/stack allocation; false otherwise.
 */
static inline FY_ALWAYS_INLINE bool
fy_generic_is_in_place_normal(fy_generic v)
{
	if (fy_generic_is_direct_invalid(v))
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

/**
 * fy_generic_is_in_place_bithack() - Test whether a generic value is stored inplace (bithack).
 *
 * Fast branch-friendly implementation.  The escape constants (null, true,
 * false, invalid) and empty collections are always inplace.  For int, float,
 * and string, bit 0 of the type tag distinguishes inplace (odd) from
 * out-of-place (even).
 *
 * @v: The generic value to test.
 *
 * Returns:
 * true if @v requires no heap/stack allocation; false otherwise.
 */
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

/**
 * fy_generic_is_in_place() - Test whether a generic value is stored inplace.
 *
 * Preferred alias for fy_generic_is_in_place_bithack().
 *
 * @v: The generic value to test.
 *
 * Returns:
 * true if @v requires no heap/stack allocation; false otherwise.
 */
static inline FY_ALWAYS_INLINE bool
fy_generic_is_in_place(fy_generic v)
{
	return fy_generic_is_in_place_bithack(v);
}

/**
 * fy_generic_get_type_indirect() - Get the type of an indirect generic value.
 *
 * Dereferences the &fy_generic_indirect to read the type of the wrapped value.
 * Only call this when fy_generic_is_indirect() returns true.
 *
 * @v: An indirect generic value.
 *
 * Returns:
 * The &enum fy_generic_type of the value wrapped by the indirect.
 */
enum fy_generic_type
fy_generic_get_type_indirect(fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_indirect_get() - Populate a fy_generic_indirect from an indirect value.
 *
 * Copies all fields of the &fy_generic_indirect pointed to by @v into @gi.
 *
 * @v:  An indirect generic value.
 * @gi: Pointer to a caller-allocated &fy_generic_indirect to receive the data.
 */
void
fy_generic_indirect_get(fy_generic v, fy_generic_indirect *gi)
	FY_EXPORT;

/**
 * fy_genericp_indirect_get_valuep_nocheck() - Get a pointer to the value inside an indirect.
 *
 * Returns a pointer into the &fy_generic_indirect's @value field without
 * validating that %FYGIF_VALUE is set.  Only use when the flag is known to be set.
 *
 * @vp: Pointer to an indirect generic value.
 *
 * Returns:
 * Pointer to the @value field of the underlying &fy_generic_indirect.
 */
const fy_generic *
fy_genericp_indirect_get_valuep_nocheck(const fy_generic *vp)
	FY_EXPORT;

/**
 * fy_genericp_indirect_get_valuep() - Get a pointer to the value inside an indirect.
 *
 * Like fy_genericp_indirect_get_valuep_nocheck() but checks that @vp is
 * a valid indirect and that %FYGIF_VALUE is set; returns NULL on failure.
 *
 * @vp: Pointer to an indirect generic value.
 *
 * Returns:
 * Pointer to the @value field, or NULL if @vp is not a valid indirect with a value.
 */
const fy_generic *
fy_genericp_indirect_get_valuep(const fy_generic *vp)
	FY_EXPORT;

/**
 * fy_generic_indirect_get_value_nocheck() - Get the value wrapped by an indirect.
 *
 * Dereferences the indirect without checking flags.  Only use when
 * %FYGIF_VALUE is known to be set.
 *
 * @v: An indirect generic value.
 *
 * Returns:
 * The wrapped &fy_generic value.
 */
fy_generic
fy_generic_indirect_get_value_nocheck(const fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_indirect_get_value() - Get the value wrapped by an indirect.
 *
 * Checks that @v is a valid indirect with %FYGIF_VALUE set.
 *
 * @v: An indirect generic value.
 *
 * Returns:
 * The wrapped &fy_generic value, or %fy_invalid if not available.
 */
fy_generic
fy_generic_indirect_get_value(const fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_indirect_get_anchor() - Get the anchor from an indirect.
 *
 * @v: An indirect generic value.
 *
 * Returns:
 * The anchor as a string &fy_generic, or %fy_null if %FYGIF_ANCHOR is not set.
 */
fy_generic
fy_generic_indirect_get_anchor(fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_indirect_get_tag() - Get the tag from an indirect.
 *
 * @v: An indirect generic value.
 *
 * Returns:
 * The tag as a string &fy_generic, or %fy_null if %FYGIF_TAG is not set.
 */
fy_generic
fy_generic_indirect_get_tag(fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_indirect_get_diag() - Get the diagnostics from an indirect.
 *
 * @v: An indirect generic value.
 *
 * Returns:
 * The diagnostics as a &fy_generic, or %fy_null if %FYGIF_DIAG is not set.
 */
fy_generic
fy_generic_indirect_get_diag(fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_indirect_get_marker() - Get the source-position marker from an indirect.
 *
 * @v: An indirect generic value.
 *
 * Returns:
 * The marker as a &fy_generic, or %fy_null if %FYGIF_MARKER is not set.
 */
fy_generic
fy_generic_indirect_get_marker(fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_indirect_get_style() - Get the source style from an indirect.
 *
 * @v: An indirect generic value.
 *
 * Returns:
 * The style as a &fy_generic, or %fy_null if %FYGIF_STYLE is not set.
 */
fy_generic
fy_generic_indirect_get_style(fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_indirect_get_comment() - Get the attached comment from an indirect.
 *
 * @v: An indirect generic value.
 *
 * Returns:
 * The comment as a &fy_generic, or %fy_null if %FYGIF_COMMENT is not set.
 */
fy_generic
fy_generic_indirect_get_comment(fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_get_anchor() - Get the anchor from any generic value.
 *
 * Handles both direct and indirect values; for direct values there is
 * no anchor, so %fy_null is returned.
 *
 * @v: Any generic value.
 *
 * Returns:
 * The anchor string &fy_generic, or %fy_null if none.
 */
fy_generic
fy_generic_get_anchor(fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_get_tag() - Get the tag from any generic value.
 *
 * @v: Any generic value.
 *
 * Returns:
 * The tag string &fy_generic, or %fy_null if none.
 */
fy_generic
fy_generic_get_tag(fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_get_diag() - Get the diagnostics from any generic value.
 *
 * @v: Any generic value.
 *
 * Returns:
 * The diagnostics &fy_generic, or %fy_null if none.
 */
fy_generic
fy_generic_get_diag(fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_get_marker() - Get the source-position marker from any generic value.
 *
 * @v: Any generic value.
 *
 * Returns:
 * The marker &fy_generic, or %fy_null if none.
 */
fy_generic
fy_generic_get_marker(fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_get_style() - Get the source style from any generic value.
 *
 * @v: Any generic value.
 *
 * Returns:
 * The style &fy_generic, or %fy_null if none.
 */
fy_generic
fy_generic_get_style(fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_get_comment() - Get the attached comment from any generic value.
 *
 * @v: Any generic value.
 *
 * Returns:
 * The comment &fy_generic, or %fy_null if none.
 */
fy_generic
fy_generic_get_comment(fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_get_type() - Get the type of any generic value.
 *
 * Dispatches to the indirect or direct type accessor as appropriate.
 * This is the preferred single entry-point for type queries.
 *
 * @v: Any generic value (direct or indirect).
 *
 * Returns:
 * The &enum fy_generic_type of @v.
 */
static inline enum fy_generic_type fy_generic_get_type(fy_generic v)
{
	if (fy_generic_is_indirect(v))
		return fy_generic_get_type_indirect(v);
	return fy_generic_get_direct_type(v);
}

/**
 * typedef fy_generic_sequence - Out-of-place storage for a generic sequence.
 *
 * A contiguous block of @count &fy_generic items following the header.
 * Must be 16-byte aligned (see %FY_GENERIC_CONTAINER_ALIGN).
 * Do not rearrange fields — the @count / @items layout is shared with
 * &fy_generic_collection and assumed in various bithack operations.
 */
typedef struct fy_generic_sequence {
	size_t count;
	fy_generic items[];
} fy_generic_sequence FY_GENERIC_CONTAINER_ALIGNMENT;

/**
 * typedef fy_generic_map_pair - A key/value pair within a generic mapping.
 *
 * The union allows access either as named @key / @value fields or as
 * a two-element @items array (index 0 = key, index 1 = value).
 * Do not change the layout — code iterates mappings as flat item arrays.
 */
typedef union fy_generic_map_pair {
	struct {
		fy_generic key;
		fy_generic value;
	};
	fy_generic items[2];
} fy_generic_map_pair;

/**
 * typedef fy_generic_mapping - Out-of-place storage for a generic mapping.
 *
 * Stores @count key/value pairs as a contiguous flexible array of
 * &fy_generic_map_pair elements.  Must be 16-byte aligned.  Do not
 * rearrange fields.
 */
typedef struct fy_generic_mapping {
	size_t count;
	fy_generic_map_pair pairs[];
} fy_generic_mapping FY_GENERIC_CONTAINER_ALIGNMENT;

/**
 * typedef fy_generic_collection - Generic view over a sequence or mapping buffer.
 *
 * Shares the same memory layout as &fy_generic_sequence; for mappings,
 * @count is the number of pairs and @items contains 2*count interleaved
 * key/value generics.  Used when processing sequences and mappings
 * uniformly without knowing the concrete type.
 */
typedef struct fy_generic_collection {
	size_t count;	/* *2 for mapping */
	fy_generic items[];
} fy_generic_collection FY_GENERIC_CONTAINER_ALIGNMENT;

/**
 * typedef fy_generic_sized_string - A string with an explicit byte count.
 *
 * Used when passing strings that may contain embedded NUL bytes or when
 * avoiding a strlen() call.  The @data pointer and @size byte count need
 * not be NUL-terminated.
 */
typedef struct fy_generic_sized_string {
	const char *data;
	size_t size;
} fy_generic_sized_string;

/* FYGDIF_UNSIGNED_RANGE_EXTEND - treat the integer as unsigned for range purposes */
#define FYGDIF_UNSIGNED_RANGE_EXTEND	FY_BIT(0)

/**
 * typedef fy_generic_decorated_int - An integer paired with encoding flags.
 *
 * Wraps a 64-bit integer value with a @flags word (%FYGDIF_*) that
 * controls how the integer is encoded (e.g. whether it should be treated
 * as unsigned even when the value fits in the signed range).  Access the
 * value as @sv (signed) or @uv (unsigned).  The value union must be first
 * for correct ABI — do not reorder.
 */
typedef struct fy_generic_decorated_int {
	/* must be first */
	union {
		signed long long sv;
		unsigned long long uv;
	};
	/* both clang and gcc miscompile this if it's a bitfield */
	unsigned long long flags;
} fy_generic_decorated_int;

/* Typed handle aliases for const pointers to collection storage */
typedef const fy_generic_sequence *fy_generic_sequence_handle;
typedef const fy_generic_mapping *fy_generic_mapping_handle;
typedef const fy_generic_map_pair *fy_generic_map_pair_handle;

/* Null (empty) handle sentinels */
#define fy_seq_handle_null	((fy_generic_sequence_handle)NULL)
#define fy_map_handle_null	((fy_generic_mapping_handle)NULL)
/* Zero-initialised sized-string and decorated-int literals */
#define fy_szstr_empty		((fy_generic_sized_string){ })
#define fy_dint_empty		((fy_generic_decorated_int){ })
/* A map pair where both key and value are fy_invalid */
#define fy_map_pair_invalid	((fy_generic_map_pair) { .key = fy_invalid, .value = fy_invalid })

/**
 * fy_sequence_storage_size() - Compute bytes needed for a sequence of @count items.
 *
 * Computes the total allocation size for a &fy_generic_sequence header
 * plus @count &fy_generic items, checking for overflow.
 *
 * @count: Number of items.
 *
 * Returns:
 * The required byte count, or %SIZE_MAX on integer overflow.
 */
static inline size_t fy_sequence_storage_size(const size_t count)
{
	size_t size;

	if (FY_MUL_OVERFLOW(count, sizeof(fy_generic), &size) ||
	    FY_ADD_OVERFLOW(size, sizeof(struct fy_generic_sequence), &size))
		return SIZE_MAX;
	return size;
}

/**
 * fy_mapping_storage_size() - Compute bytes needed for a mapping of @count pairs.
 *
 * Computes the total allocation size for a &fy_generic_mapping header
 * plus @count &fy_generic_map_pair entries, checking for overflow.
 *
 * @count: Number of key/value pairs.
 *
 * Returns:
 * The required byte count, or %SIZE_MAX on integer overflow.
 */
static inline size_t fy_mapping_storage_size(const size_t count)
{
	size_t size;

	if (FY_MUL_OVERFLOW(count, sizeof(fy_generic_map_pair), &size) ||
	    FY_ADD_OVERFLOW(size, sizeof(struct fy_generic_mapping), &size))
		return SIZE_MAX;
	return size;
}

/**
 * fy_collection_storage_size() - Compute bytes for a sequence or mapping.
 *
 * Dispatches to fy_sequence_storage_size() or fy_mapping_storage_size()
 * depending on @is_map.
 *
 * @is_map: true for a mapping, false for a sequence.
 * @count:  Number of items (pairs for mappings).
 *
 * Returns:
 * The required byte count, or %SIZE_MAX on integer overflow.
 */
static inline size_t fy_collection_storage_size(bool is_map, const size_t count)
{
	return !is_map ?
		fy_sequence_storage_size(count) :
		fy_mapping_storage_size(count);
}

/**
 * fy_generic_typeof() - Yield the canonical C type for a generic-compatible expression.
 *
 * Uses C11 _Generic to map @_v to its canonical type: char* and const char*
 * pass through unchanged, fy_generic_builder* passes through, and everything
 * else yields the type of @_v itself.  Used internally to normalise argument
 * types before encoding them as fy_generic values.
 *
 * @_v: An expression whose type is to be normalised.
 *
 * Returns:
 * An expression of the canonical type (for use with __typeof__).
 */
#define fy_generic_typeof(_v) \
	__typeof__(_Generic((_v), \
		char *: ((char *)0), \
		const char *: ((const char *)0), \
		struct fy_generic_builder *: ((struct fy_generic_builder *)0), \
		default: _v))

/**
 * fy_generic_is_valid() - Test whether any generic value is not invalid.
 *
 * Handles both direct and indirect values.  For an indirect, the
 * wrapped value is checked.
 *
 * @v: Any generic value.
 *
 * Returns:
 * true if @v (or its wrapped value for indirects) is not %fy_invalid.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_valid(const fy_generic v)
{
	if (fy_generic_is_indirect(v))
		return fy_generic_is_direct_valid(fy_generic_indirect_get_value(v));
	return fy_generic_is_direct_valid(v);
}

/**
 * fy_generic_is_invalid() - Test whether any generic value is invalid.
 *
 * Handles both direct and indirect values.  For an indirect, the
 * wrapped value is checked.
 *
 * @v: Any generic value.
 *
 * Returns:
 * true if @v (or its wrapped value for indirects) equals %fy_invalid.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_invalid(const fy_generic v)
{
	if (fy_generic_is_indirect(v))
		return fy_generic_is_direct_invalid(fy_generic_indirect_get_value(v));
	return fy_generic_is_direct_invalid(v);
}

/**
 * FY_GENERIC_IS_TEMPLATE_INLINE() - Generate inline type-check functions for a generic type.
 *
 * Instantiates three functions for the type suffix @_gtype:
 *   - fy_generic_is_indirect_##_gtype##_nocheck(): checks the type of an indirect's wrapped
 *     value without validating that the indirect flag is set.
 *   - fy_generic_is_indirect_##_gtype(): safe version with validation.
 *   - fy_generic_is_##_gtype(): inline dispatcher handling both direct and indirect values.
 *
 * @_gtype: Type suffix (e.g. null_type, bool_type, int_type, string, sequence, …).
 */
#define FY_GENERIC_IS_TEMPLATE_INLINE(_gtype) \
\
bool \
fy_generic_is_indirect_ ## _gtype ## _nocheck(const fy_generic v) \
	FY_EXPORT; \
\
bool \
fy_generic_is_indirect_ ## _gtype (const fy_generic v) \
	FY_EXPORT; \
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

/**
 * FY_GENERIC_IS_TEMPLATE_NON_INLINE() - Generate out-of-line type-check function bodies.
 *
 * Companion to FY_GENERIC_IS_TEMPLATE_INLINE() used in .c files to emit the
 * actual function bodies for fy_generic_is_indirect_##_gtype##_nocheck() and
 * fy_generic_is_indirect_##_gtype().
 *
 * @_gtype: Type suffix matching a previous FY_GENERIC_IS_TEMPLATE_INLINE() call.
 */
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

/* Direct (non-indirect) type testers — only valid when fy_generic_is_direct() is true */

/**
 * fy_generic_is_direct_null_type() - Test whether a direct generic is null.
 * @v: A direct generic value.
 * Returns: true if @v is the null escape constant.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_null_type(const fy_generic v)
{
	return v.v == fy_null_value;
}

/* Generates fy_generic_is_null_type(), fy_generic_is_indirect_null_type(), … */
FY_GENERIC_IS_TEMPLATE_INLINE(null_type);

/**
 * fy_generic_is_direct_bool_type() - Test whether a direct generic is a boolean.
 * @v: A direct generic value.
 * Returns: true if @v is fy_true or fy_false.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_bool_type(const fy_generic v)
{
	return v.v == fy_true_value || v.v == fy_false_value;
}

/* Generates fy_generic_is_bool_type(), fy_generic_is_indirect_bool_type(), … */
FY_GENERIC_IS_TEMPLATE_INLINE(bool_type);

/**
 * fy_generic_is_direct_int_type() - Test whether a direct generic is a signed integer.
 *
 * Checks for both inplace (%FY_INT_INPLACE_V) and out-of-place (%FY_INT_OUTPLACE_V)
 * integer tag values using an unsigned range trick.
 *
 * @v: A direct generic value.
 *
 * Returns:
 * true if @v holds an integer (signed or unsigned).
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_int_type(const fy_generic v)
{
	// fy_generic_value m = (v.v & FY_INPLACE_TYPE_MASK);
	// return m == FY_INT_INPLACE_V || m == FY_INT_OUTPLACE_V;
	// FY_INT_INPLACE_V = 3, FY_INT_OUTPLACE_V = 4
	return ((v.v & FY_INPLACE_TYPE_MASK) - FY_INT_INPLACE_V) <= 1;
}

/**
 * fy_generic_is_direct_uint_type() - Test whether a direct generic is an unsigned integer.
 *
 * Signed and unsigned integers share the same tag bits; this is an alias
 * for fy_generic_is_direct_int_type().
 *
 * @v: A direct generic value.
 *
 * Returns:
 * true if @v holds an integer value.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_uint_type(const fy_generic v)
{
	return fy_generic_is_direct_int_type(v);
}

/* Generates fy_generic_is_int_type(), fy_generic_is_uint_type(), and indirect variants */
FY_GENERIC_IS_TEMPLATE_INLINE(int_type);
FY_GENERIC_IS_TEMPLATE_INLINE(uint_type);

/**
 * fy_generic_is_direct_float_type() - Test whether a direct generic is a float.
 *
 * Checks for both inplace (%FY_FLOAT_INPLACE_V, 64-bit only) and
 * out-of-place (%FY_FLOAT_OUTPLACE_V) float tag values.
 *
 * @v: A direct generic value.
 *
 * Returns:
 * true if @v holds a floating-point value.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_float_type(const fy_generic v)
{
	// fy_generic_value m = (v.v & FY_INPLACE_TYPE_MASK);
	// return m == FY_FLOAT_INPLACE_V || m == FY_FLOAT_OUTPLACE_V;
	return ((v.v & FY_INPLACE_TYPE_MASK) - FY_FLOAT_INPLACE_V) <= 1;
}

/* Generates fy_generic_is_float_type() and indirect variants */
FY_GENERIC_IS_TEMPLATE_INLINE(float_type);

/**
 * fy_generic_is_direct_string() - Test whether a direct generic is a string.
 *
 * Checks for both inplace (%FY_STRING_INPLACE_V) and out-of-place
 * (%FY_STRING_OUTPLACE_V) string tag values.
 *
 * @v: A direct generic value.
 *
 * Returns:
 * true if @v holds a string value.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_string(const fy_generic v)
{
	// fy_generic_value m = (v.v & FY_INPLACE_TYPE_MASK);
	// return m == FY_STRING_INPLACE_V || m == FY_STRING_OUTPLACE_V;
	return ((v.v & FY_INPLACE_TYPE_MASK) - FY_STRING_INPLACE_V) <= 1;
}

/**
 * fy_generic_is_direct_string_type() - Alias for fy_generic_is_direct_string().
 * @v: A direct generic value.
 * Returns: true if @v holds a string value.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_string_type(const fy_generic v)
{
	return fy_generic_is_direct_string(v);
}

/* Generates fy_generic_is_string() / fy_generic_is_string_type() and indirect variants */
FY_GENERIC_IS_TEMPLATE_INLINE(string);
FY_GENERIC_IS_TEMPLATE_INLINE(string_type);

/**
 * fy_generic_is_direct_sequence() - Test whether a direct generic is a sequence.
 *
 * A direct sequence has the low 4 bits all zero (pointer or empty sentinel).
 *
 * @v: A direct generic value.
 *
 * Returns:
 * true if @v is a sequence (possibly empty).
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_sequence(const fy_generic v)
{
	return (v.v & FY_COLLECTION_MASK) == 0;	/* sequence is 0 lower 4 bits */
}

/**
 * fy_generic_is_direct_sequence_type() - Alias for fy_generic_is_direct_sequence().
 * @v: A direct generic value.
 * Returns: true if @v is a sequence.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_sequence_type(const fy_generic v)
{
	return fy_generic_is_direct_sequence(v);
}

/* Generates fy_generic_is_sequence() / fy_generic_is_sequence_type() and indirect variants */
FY_GENERIC_IS_TEMPLATE_INLINE(sequence);
FY_GENERIC_IS_TEMPLATE_INLINE(sequence_type);

/**
 * fy_generic_is_direct_mapping() - Test whether a direct generic is a mapping.
 *
 * A direct mapping has bit 3 set and the low 3 bits zero (low 4 bits == 8).
 *
 * @v: A direct generic value.
 *
 * Returns:
 * true if @v is a mapping (possibly empty).
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_mapping(const fy_generic v)
{
	return (v.v & FY_COLLECTION_MASK) == 8;	/* sequence is 8 lower 4 bits */
}

/**
 * fy_generic_is_direct_mapping_type() - Alias for fy_generic_is_direct_mapping().
 * @v: A direct generic value.
 * Returns: true if @v is a mapping.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_mapping_type(const fy_generic v)
{
	return fy_generic_is_direct_mapping(v);
}

/* Generates fy_generic_is_mapping() / fy_generic_is_mapping_type() and indirect variants */
FY_GENERIC_IS_TEMPLATE_INLINE(mapping);
FY_GENERIC_IS_TEMPLATE_INLINE(mapping_type);

/**
 * fy_generic_is_direct_collection() - Test whether a direct generic is a sequence or mapping.
 *
 * Both sequences and mappings have the low 3 bits of the type tag equal to zero.
 *
 * @v: A direct generic value.
 *
 * Returns:
 * true if @v is either a sequence or a mapping.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_collection(const fy_generic v)
{
	return (v.v & FY_INPLACE_TYPE_MASK) == 0;	/* sequence is 0, mapping is 8 (3 lower bits 0) */
}

/* Generates fy_generic_is_collection() and indirect variants */
FY_GENERIC_IS_TEMPLATE_INLINE(collection);

/**
 * fy_generic_collectionp_get_items() - Get the items array from a collection pointer.
 *
 * Returns the flat item array of a sequence or mapping storage block.
 * For mappings the returned array interleaves keys and values, so
 * @countp receives colp->count * 2.
 *
 * @type:   Either %FYGT_SEQUENCE or %FYGT_MAPPING.
 * @colp:   Pointer to the &fy_generic_collection storage block (may be NULL).
 * @countp: Receives the number of &fy_generic items in the returned array.
 *
 * Returns:
 * Pointer to the first item, or NULL if @colp is NULL or the collection is empty.
 */
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

/**
 * fy_generic_get_direct_collection() - Resolve a direct collection generic to its storage.
 *
 * Checks that @v is a direct sequence or mapping, identifies its type,
 * and returns a pointer to its &fy_generic_collection storage.
 *
 * @v:     A direct generic value.
 * @typep: Receives %FYGT_SEQUENCE, %FYGT_MAPPING, or %FYGT_INVALID if not a collection.
 *
 * Returns:
 * Pointer to the collection storage, or NULL if @v is not a direct collection.
 */
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

/**
 * fy_generic_is_direct_alias() - Test whether a direct generic is an alias.
 * @v: A direct generic value.
 * Returns: true if @v has type %FYGT_ALIAS.
 */
static inline FY_ALWAYS_INLINE
bool fy_generic_is_direct_alias(const fy_generic v)
{
	return fy_generic_get_type(v) == FYGT_ALIAS;
}

/* Generates fy_generic_is_alias() and indirect variants */
FY_GENERIC_IS_TEMPLATE_INLINE(alias);

/* Low-level primitive encode/decode helpers used by the _Generic dispatch machinery.
 * Each type provides four helpers: get_no_check (decode), in_place (encode inplace),
 * out_of_place_size (required extra bytes), out_of_place_put (encode out-of-place). */

/**
 * fy_generic_get_null_type_no_check() - Decode a null generic (always returns NULL).
 * @v: A generic value known to be null.
 * Returns: Always NULL.
 */
static inline void *fy_generic_get_null_type_no_check(fy_generic v)
{
	return NULL;
}

/**
 * fy_generic_in_place_null_type() - Encode a null pointer as an inplace null generic.
 * @p: Must be NULL; any non-NULL pointer yields %fy_invalid_value.
 * Returns: %fy_null_value if @p is NULL, %fy_invalid_value otherwise.
 */
static inline fy_generic_value fy_generic_in_place_null_type(void *p)
{
	return p == NULL ? fy_null_value : fy_invalid_value;
}

/**
 * fy_generic_out_of_place_size_null_type() - Out-of-place allocation size for null (always 0).
 * @v: Ignored.
 * Returns: 0 — nulls are always stored inplace.
 */
static inline size_t fy_generic_out_of_place_size_null_type(void *v)
{
	return 0;
}

/**
 * fy_generic_out_of_place_put_null_type() - Encode null into an out-of-place buffer.
 * @buf: Ignored (null has no out-of-place representation).
 * @v:   Ignored.
 * Returns: %fy_null_value.
 */
static inline fy_generic_value fy_generic_out_of_place_put_null_type(void *buf, void *v)
{
	return fy_null_value;
}

/**
 * fy_generic_get_bool_type_no_check() - Decode a boolean generic.
 * @v: A generic value known to be a boolean.
 * Returns: true if @v equals %fy_true_value; false for %fy_false_value.
 */
static inline bool fy_generic_get_bool_type_no_check(fy_generic v)
{
	return v.v == fy_true_value;
}

/**
 * fy_generic_in_place_bool_type() - Encode a boolean as an inplace generic.
 * @v: The boolean value to encode.
 * Returns: %fy_true_value or %fy_false_value.
 */
static inline fy_generic_value fy_generic_in_place_bool_type(_Bool v)
{
	return v ? fy_true_value : fy_false_value;
}

/**
 * fy_generic_out_of_place_size_bool_type() - Out-of-place allocation size for a boolean (0).
 * @v: Ignored.
 * Returns: 0 — booleans are always stored inplace.
 */
static inline size_t fy_generic_out_of_place_size_bool_type(bool v)
{
	return 0;
}

/**
 * fy_generic_out_of_place_put_bool_type() - Encode a boolean into an out-of-place buffer.
 * @buf: Ignored.
 * @v:   The boolean value.
 * Returns: %fy_true_value or %fy_false_value.
 */
static inline fy_generic_value fy_generic_out_of_place_put_bool_type(void *buf, bool v)
{
	return v ? fy_true_value : fy_false_value;
}

/**
 * fy_generic_in_place_int_type() - Try to encode a signed integer inplace.
 *
 * Stores the value in the upper bits of the word if it fits within the
 * inplace range [%FYGT_INT_INPLACE_MIN, %FYGT_INT_INPLACE_MAX].
 *
 * @v: The signed 64-bit integer to encode.
 *
 * Returns:
 * The encoded &fy_generic_value, or %fy_invalid_value if @v is out of range.
 */
static inline fy_generic_value fy_generic_in_place_int_type(const long long v)
{
	if (v >= FYGT_INT_INPLACE_MIN && v <= FYGT_INT_INPLACE_MAX)
		return (((fy_generic_value)(signed long long)v) << FY_INT_INPLACE_SHIFT) | FY_INT_INPLACE_V;
	return fy_invalid_value;
}

/**
 * fy_generic_out_of_place_put_int_type() - Encode a signed integer into an out-of-place buffer.
 *
 * Writes the value into a &fy_generic_decorated_int at @buf (which must be
 * aligned to %FY_INPLACE_TYPE_MASK + 1 bytes) and returns a tagged pointer.
 *
 * @buf: Caller-allocated, suitably aligned buffer of at least sizeof(fy_generic_decorated_int).
 * @v:   The signed 64-bit integer value.
 *
 * Returns:
 * A &fy_generic_value with %FY_INT_OUTPLACE_V tag and @buf as the pointer.
 */
static inline fy_generic_value fy_generic_out_of_place_put_int_type(void *buf, const long long v)
{
	struct fy_generic_decorated_int *p = buf;

	assert(((uintptr_t)buf & FY_INPLACE_TYPE_MASK) == 0);
	memset(p, 0, sizeof(*p));
	p->sv = v;
	p->flags = 0;
	return (fy_generic_value)buf | FY_INT_OUTPLACE_V;
}

/**
 * fy_generic_in_place_uint_type() - Try to encode an unsigned integer inplace.
 *
 * Stores the value inplace if it fits within %FYGT_INT_INPLACE_MAX
 * (unsigned values sharing the inplace range with signed integers).
 *
 * @v: The unsigned 64-bit integer to encode.
 *
 * Returns:
 * The encoded &fy_generic_value, or %fy_invalid_value if @v exceeds the range.
 */
static inline fy_generic_value fy_generic_in_place_uint_type(const unsigned long long v)
{
	if (v <= FYGT_INT_INPLACE_MAX)
		return (((fy_generic_value)v) << FY_INT_INPLACE_SHIFT) | FY_INT_INPLACE_V;
	return fy_invalid_value;
}

/**
 * fy_generic_out_of_place_put_uint_type() - Encode an unsigned integer into an out-of-place buffer.
 *
 * Writes the value into a &fy_generic_decorated_int and sets
 * %FYGDIF_UNSIGNED_RANGE_EXTEND if the value exceeds LLONG_MAX.
 *
 * @buf: Caller-allocated, suitably aligned buffer.
 * @v:   The unsigned 64-bit integer value.
 *
 * Returns:
 * A &fy_generic_value with %FY_INT_OUTPLACE_V tag and @buf as the pointer.
 */
static inline fy_generic_value fy_generic_out_of_place_put_uint_type(void *buf, const unsigned long long v)
{
	struct fy_generic_decorated_int *p = buf;

	assert(((uintptr_t)buf & FY_INPLACE_TYPE_MASK) == 0);
	p->uv = v;
	p->flags = (v > (unsigned long long)LLONG_MAX) ? FYGDIF_UNSIGNED_RANGE_EXTEND : 0;
	return (fy_generic_value)buf | FY_INT_OUTPLACE_V;
}

/**
 * fy_generic_in_place_float_type() - Try to encode a double as an inplace float (64-bit).
 *
 * On 64-bit platforms, stores the value as a 32-bit float in the upper half
 * of the word if the value is representable without precision loss (denormals,
 * infinities, NaN, and exact float32 values qualify).
 * On 32-bit platforms always returns %fy_invalid_value (no inplace floats).
 *
 * @v: The double value to encode.
 *
 * Returns:
 * The inplace-encoded &fy_generic_value, or %fy_invalid_value if @v cannot
 * be stored inplace.
 */
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

/**
 * fy_generic_out_of_place_put_float_type() - Encode a double into an out-of-place buffer.
 *
 * Writes the double at @buf (which must be suitably aligned) and returns
 * a tagged pointer with %FY_FLOAT_OUTPLACE_V.
 *
 * @buf: Caller-allocated, suitably aligned buffer of at least sizeof(double).
 * @v:   The double value to store.
 *
 * Returns:
 * A &fy_generic_value with %FY_FLOAT_OUTPLACE_V tag and @buf as the pointer.
 */
static inline fy_generic_value fy_generic_out_of_place_put_float_type(void *buf, const double v)
{
	assert(((uintptr_t)buf & FY_INPLACE_TYPE_MASK) == 0);
	*(double *)buf = v;
	return (fy_generic_value)buf | FY_FLOAT_OUTPLACE_V;
}

/**
 * fy_generic_get_int_type_no_check() - Decode a signed integer generic.
 *
 * Handles both inplace (sign-extends the packed value) and out-of-place
 * (dereferences the pointer) representations.
 *
 * @v: A generic value known to be an integer.
 *
 * Returns:
 * The decoded signed 64-bit integer, or 0 if the pointer resolves to NULL.
 */
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

/**
 * fy_generic_out_of_place_size_int_type() - Out-of-place allocation size for a signed integer.
 *
 * @v: The integer value to be encoded.
 *
 * Returns:
 * 0 if @v fits inplace, sizeof(fy_generic_decorated_int) otherwise.
 */
static inline size_t fy_generic_out_of_place_size_int_type(const long long v)
{
	return (v >= FYGT_INT_INPLACE_MIN && v <= FYGT_INT_INPLACE_MAX) ? 0 : sizeof(fy_generic_decorated_int);
}

/**
 * fy_generic_get_uint_type_no_check() - Decode an unsigned integer generic.
 *
 * Handles both inplace (zero-extends the packed value) and out-of-place
 * representations.
 *
 * @v: A generic value known to be an integer.
 *
 * Returns:
 * The decoded unsigned 64-bit integer, or 0 if the pointer resolves to NULL.
 */
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

/**
 * fy_generic_out_of_place_size_uint_type() - Out-of-place allocation size for an unsigned integer.
 *
 * @v: The unsigned integer value to be encoded.
 *
 * Returns:
 * 0 if @v fits inplace, sizeof(fy_generic_decorated_int) otherwise.
 */
static inline size_t fy_generic_out_of_place_size_uint_type(const unsigned long long v)
{
	return v <= FYGT_INT_INPLACE_MAX ? 0 : sizeof(fy_generic_decorated_int);
}

#ifdef FYGT_GENERIC_64
/* Byte offset of the 32-bit float within a 64-bit word (endian-specific) */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define FY_INPLACE_FLOAT_ADV	1  /* float32 occupies the upper 4 bytes on little-endian */
#else
#define FY_INPLACE_FLOAT_ADV	0  /* float32 occupies the upper 4 bytes on big-endian */
#endif
#endif

/**
 * fy_generic_get_float_type_no_check() - Decode a float generic (64-bit).
 *
 * Handles inplace 32-bit floats (widened to double) and out-of-place doubles.
 *
 * @v: A generic value known to be a float.
 *
 * Returns:
 * The decoded double value, or 0.0 if the out-of-place pointer is NULL.
 */
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

/**
 * fy_generic_out_of_place_size_float_type() - Out-of-place allocation size for a float (64-bit).
 *
 * @v: The double value to be encoded.
 *
 * Returns:
 * 0 if @v can be stored inplace (as float32 without loss), sizeof(double) otherwise.
 */
static inline size_t fy_generic_out_of_place_size_float_type(const double v)
{
	return (!isnormal(v) || (float)v == v) ? 0 : sizeof(double);
}

#else
/*
 * fy_generic_get_float_type_no_check() - Decode a float generic (32-bit).
 *
 * On 32-bit platforms floats are always out-of-place doubles.
 *
 * @v: A generic value known to be a float.
 *
 * Returns:
 * The decoded double value, or 0.0 if the pointer is NULL.
 */
static inline double fy_generic_get_float_type_no_check(fy_generic v)
{
	const void *p;

	p = fy_generic_resolve_ptr(v);
	if (!p)
		return 0.0;

	/* always double */
	return *(double *)p;
}

/**
 * fy_generic_out_of_place_size_double() - Out-of-place allocation size for a double (32-bit).
 *
 * On 32-bit platforms all floats are stored out-of-place as doubles.
 *
 * @v: Ignored.
 *
 * Returns:
 * sizeof(double).
 */
static inline size_t fy_generic_out_of_place_size_double(const double v)
{
	return sizeof(double);
}

#endif

/**
 * fy_generic_sequence_resolve_outofplace() - Resolve a non-direct sequence to its storage.
 *
 * Called when the sequence is indirect (wrapped) or otherwise not directly
 * addressable.  Do not call directly; use fy_generic_sequence_resolve() instead.
 *
 * @seq: A sequence generic value.
 *
 * Returns:
 * Pointer to the &fy_generic_sequence storage, or NULL on error.
 */
const fy_generic_sequence *
fy_generic_sequence_resolve_outofplace(fy_generic seq)
	FY_EXPORT;

/**
 * fy_generic_sequence_resolve() - Resolve any sequence generic to its storage pointer.
 *
 * Handles both direct (pointer embedded in the word) and indirect sequences.
 *
 * @seq: A sequence generic value.
 *
 * Returns:
 * Pointer to the &fy_generic_sequence storage, or NULL for an empty/invalid sequence.
 */
static inline FY_ALWAYS_INLINE const fy_generic_sequence *
fy_generic_sequence_resolve(const fy_generic seq)
{
	if (fy_generic_is_direct_sequence(seq))
		return fy_generic_resolve_collection_ptr(seq);
	return fy_generic_sequence_resolve_outofplace(seq);
}

/**
 * fy_generic_sequence_to_handle() - Convert a sequence generic to an opaque handle.
 *
 * @seq: A sequence generic value.
 *
 * Returns:
 * A &fy_generic_sequence_handle pointing to the sequence storage.
 */
static inline FY_ALWAYS_INLINE fy_generic_sequence_handle
fy_generic_sequence_to_handle(const fy_generic seq)
{
	return fy_generic_sequence_resolve(seq);
}

/**
 * fy_generic_sequencep_items() - Get the items array from a sequence pointer.
 *
 * @seqp: Pointer to a &fy_generic_sequence (may be NULL).
 *
 * Returns:
 * Pointer to the first item, or NULL if @seqp is NULL.
 */
static inline FY_ALWAYS_INLINE const fy_generic *
fy_generic_sequencep_items(const fy_generic_sequence *seqp)
{
	return seqp ? &seqp->items[0] : NULL;
}

/**
 * fy_generic_sequencep_get_item_count() - Get the item count from a sequence pointer.
 *
 * @seqp: Pointer to a &fy_generic_sequence (may be NULL).
 *
 * Returns:
 * The number of items, or 0 if @seqp is NULL.
 */
static inline FY_ALWAYS_INLINE size_t
fy_generic_sequencep_get_item_count(const fy_generic_sequence *seqp)
{
	return seqp ? seqp->count : 0;
}

/**
 * fy_generic_sequence_get_item_count() - Get the number of items in a sequence.
 *
 * @seq: A sequence generic value.
 *
 * Returns:
 * The number of items in the sequence, or 0 if empty or invalid.
 */
static inline FY_ALWAYS_INLINE size_t fy_generic_sequence_get_item_count(fy_generic seq)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_item_count(seqp);
}

/**
 * fy_generic_sequence_get_items() - Get the items array and count from a sequence.
 *
 * @seq:    A sequence generic value.
 * @countp: Receives the number of items.
 *
 * Returns:
 * Pointer to the first item, or NULL if the sequence is empty or invalid.
 */
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

/* we are making things very hard for the compiler; sometimes he gets lost */
FY_DIAG_PUSH
FY_DIAG_IGNORE_ARRAY_BOUNDS

/**
 * fy_generic_sequencep_get_itemp() - Get a pointer to a specific item in a sequence pointer.
 *
 * @seqp: Pointer to a &fy_generic_sequence (may be NULL).
 * @idx:  Zero-based item index.
 *
 * Returns:
 * Pointer to the item at @idx, or NULL if out of range or @seqp is NULL.
 */
static inline const fy_generic *fy_generic_sequencep_get_itemp(const fy_generic_sequence *seqp, const size_t idx)
{
	if (!seqp || idx >= seqp->count)
		return NULL;
	return &seqp->items[idx];
}

FY_DIAG_POP

/**
 * fy_generic_sequence_get_itemp() - Get a pointer to a specific item in a sequence.
 *
 * @seq: A sequence generic value.
 * @idx: Zero-based item index.
 *
 * Returns:
 * Pointer to the item at @idx, or NULL if out of range or the sequence is invalid.
 */
static inline const fy_generic *fy_generic_sequence_get_itemp(fy_generic seq, const size_t idx)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_itemp(seqp, idx);
}

/**
 * fy_generic_sequence_get_item_generic() - Get a specific item from a sequence as a value.
 *
 * @seq: A sequence generic value.
 * @idx: Zero-based item index.
 *
 * Returns:
 * The item at @idx, or %fy_invalid if out of range or the sequence is invalid.
 */
static inline fy_generic fy_generic_sequence_get_item_generic(fy_generic seq, const size_t idx)
{
	const fy_generic *vp = fy_generic_sequence_get_itemp(seq, idx);
	if (!vp)
		return fy_invalid;
	return *vp;
}

/**
 * fy_generic_mapping_resolve_outofplace() - Resolve a non-direct mapping to its storage.
 *
 * Called when the mapping is indirect (wrapped) or otherwise not directly
 * addressable.  Do not call directly; use fy_generic_mapping_resolve() instead.
 *
 * @map: A mapping generic value.
 *
 * Returns:
 * Pointer to the &fy_generic_mapping storage, or NULL on error.
 */
const fy_generic_mapping *
fy_generic_mapping_resolve_outofplace(fy_generic map)
	FY_EXPORT;

/**
 * fy_generic_mapping_resolve() - Resolve any mapping generic to its storage pointer.
 *
 * Handles both direct (pointer embedded in the word) and indirect mappings.
 *
 * @map: A mapping generic value.
 *
 * Returns:
 * Pointer to the &fy_generic_mapping storage, or NULL for an empty/invalid mapping.
 */
static inline FY_ALWAYS_INLINE const fy_generic_mapping *
fy_generic_mapping_resolve(const fy_generic map)
{
	if (fy_generic_is_direct_mapping(map))
		return fy_generic_resolve_collection_ptr(map);
	return fy_generic_mapping_resolve_outofplace(map);
}

/**
 * fy_generic_mapping_to_handle() - Convert a mapping generic to an opaque handle.
 *
 * @map: A mapping generic value.
 *
 * Returns:
 * A &fy_generic_mapping_handle pointing to the mapping storage.
 */
static inline FY_ALWAYS_INLINE fy_generic_mapping_handle
fy_generic_mapping_to_handle(const fy_generic map)
{
	return fy_generic_mapping_resolve(map);
}

/**
 * fy_generic_mappingp_items() - Get the flat interleaved items array from a mapping pointer.
 *
 * Returns the pairs storage as a flat array of &fy_generic values where
 * even indices are keys and odd indices are values.
 *
 * @mapp: Pointer to a &fy_generic_mapping (may be NULL).
 *
 * Returns:
 * Pointer to the first key, or NULL if @mapp is NULL.
 */
static inline FY_ALWAYS_INLINE const fy_generic *
fy_generic_mappingp_items(const fy_generic_mapping *mapp)
{
	return mapp ? &mapp->pairs[0].items[0] : NULL;
}

/**
 * fy_generic_mappingp_get_pair_count() - Get the number of key/value pairs from a mapping pointer.
 *
 * @mapp: Pointer to a &fy_generic_mapping (may be NULL).
 *
 * Returns:
 * The number of pairs, or 0 if @mapp is NULL.
 */
static inline FY_ALWAYS_INLINE size_t
fy_generic_mappingp_get_pair_count(const fy_generic_mapping *mapp)
{
	return mapp ? mapp->count : 0;
}

/**
 * fy_generic_mapping_get_pairs() - Get the pairs array and pair count from a mapping.
 *
 * @map:    A mapping generic value.
 * @countp: Receives the number of key/value pairs.
 *
 * Returns:
 * Pointer to the first &fy_generic_map_pair, or NULL if the mapping is empty or invalid.
 */
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

/**
 * fy_generic_mapping_get_items() - Get the flat interleaved items array and count from a mapping.
 *
 * Returns the entire pair storage as a flat array of &fy_generic values
 * (even = keys, odd = values).  @item_countp receives 2 * pair_count.
 *
 * @map:         A mapping generic value.
 * @item_countp: Receives the total number of items (2 x pair count).
 *
 * Returns:
 * Pointer to the first item, or NULL if the mapping is empty or invalid.
 */
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

/**
 * fy_generic_compare_out_of_place() - Compare two generics that are not word-equal.
 *
 * Handles out-of-place values (e.g. large integers, strings, collections)
 * that require dereferencing to compare.  Not intended for direct use;
 * call fy_generic_compare() instead.
 *
 * @a: First generic value.
 * @b: Second generic value.
 *
 * Returns:
 * 0 if equal, negative if @a < @b, positive if @a > @b, -2 if either is invalid.
 */
int fy_generic_compare_out_of_place(fy_generic a, fy_generic b)
	FY_EXPORT;

/**
 * fy_generic_compare() - Compare two generic values for equality and ordering.
 *
 * Fast path: if the raw words are equal the values are equal (covers
 * inplace scalars and identical pointers).  Falls back to
 * fy_generic_compare_out_of_place() for out-of-place or complex values.
 *
 * @a: First generic value.
 * @b: Second generic value.
 *
 * Returns:
 * 0 if @a == @b, negative if @a < @b, positive if @a > @b,
 * -2 if either operand is %fy_invalid.
 */
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

/**
 * fy_generic_mappingp_get_at_keyp() - Get a pointer to a key at a given index (mapping pointer).
 *
 * @mapp: Pointer to a &fy_generic_mapping (may be NULL).
 * @idx:  Zero-based pair index.
 *
 * Returns:
 * Pointer to the key at @idx, or NULL if out of range or @mapp is NULL.
 */
static inline const fy_generic *fy_generic_mappingp_get_at_keyp(const fy_generic_mapping *mapp, const size_t idx)
{
	if (!mapp || idx >= mapp->count)
		return NULL;
	return &mapp->pairs[idx].key;
}

/**
 * fy_generic_mapping_get_at_keyp() - Get a pointer to a key at a given index.
 *
 * @map: A mapping generic value.
 * @idx: Zero-based pair index.
 *
 * Returns:
 * Pointer to the key at @idx, or NULL if out of range or the mapping is invalid.
 */
static inline const fy_generic *fy_generic_mapping_get_at_keyp(fy_generic map, const size_t idx)
{
	return fy_generic_mappingp_get_at_keyp(fy_generic_mapping_resolve(map), idx);
}

/**
 * fy_generic_mappingp_get_at_key() - Get a key at a given index as a value (mapping pointer).
 *
 * @mapp: Pointer to a &fy_generic_mapping (may be NULL).
 * @idx:  Zero-based pair index.
 *
 * Returns:
 * The key at @idx, or %fy_invalid if out of range or @mapp is NULL.
 */
static inline fy_generic fy_generic_mappingp_get_at_key(const fy_generic_mapping *mapp, const size_t idx)
{
	if (!mapp || idx >= mapp->count)
		return fy_invalid;
	return mapp->pairs[idx].key;
}

/**
 * fy_generic_mapping_get_at_key() - Get a key at a given index as a value.
 *
 * @map: A mapping generic value.
 * @idx: Zero-based pair index.
 *
 * Returns:
 * The key at @idx, or %fy_invalid if out of range or the mapping is invalid.
 */
static inline fy_generic fy_generic_mapping_get_at_key(fy_generic map, const size_t idx)
{
	return fy_generic_mappingp_get_at_key(fy_generic_mapping_resolve(map), idx);
}

/**
 * fy_generic_mappingp_valuep_index() - Look up a value by key in a mapping pointer.
 *
 * Linear search by key equality.  If found, sets \*@idxp to the pair index.
 *
 * @mapp: Pointer to a &fy_generic_mapping (may be NULL).
 * @key:  The key to search for.
 * @idxp: Receives the found pair index, or (size_t)-1 if not found (may be NULL).
 *
 * Returns:
 * Pointer to the value, or NULL if the key was not found or @mapp is NULL.
 */
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

/**
 * fy_generic_mappingp_get_valuep() - Look up a value by key in a mapping pointer.
 *
 * @mapp: Pointer to a &fy_generic_mapping (may be NULL).
 * @key:  The key to search for.
 *
 * Returns:
 * Pointer to the value, or NULL if the key was not found.
 */
static inline const fy_generic *fy_generic_mappingp_get_valuep(const fy_generic_mapping *mapp, fy_generic key)
{
	size_t idx;
	return fy_generic_mappingp_valuep_index(mapp, key, &idx);
}

/**
 * fy_generic_mappingp_get_at_valuep() - Get a pointer to a value at a given index (mapping pointer).
 *
 * @mapp: Pointer to a &fy_generic_mapping (may be NULL).
 * @idx:  Zero-based pair index.
 *
 * Returns:
 * Pointer to the value at @idx, or NULL if out of range or @mapp is NULL.
 */
static inline const fy_generic *fy_generic_mappingp_get_at_valuep(const fy_generic_mapping *mapp, const size_t idx)
{
	if (!mapp || idx >= mapp->count)
		return NULL;
	return &mapp->pairs[idx].value;
}

/**
 * fy_generic_mapping_get_valuep_index() - Look up a value by key, returning its index.
 *
 * @map:  A mapping generic value.
 * @key:  The key to search for.
 * @idxp: Receives the found pair index, or (size_t)-1 if not found (may be NULL).
 *
 * Returns:
 * Pointer to the value, or NULL if not found.
 */
static inline const fy_generic *fy_generic_mapping_get_valuep_index(fy_generic map, fy_generic key, size_t *idxp)
{
	return fy_generic_mappingp_valuep_index(fy_generic_mapping_resolve(map), key, idxp);
}

/**
 * fy_generic_mapping_get_valuep() - Look up a value by key in a mapping.
 *
 * @map: A mapping generic value.
 * @key: The key to search for.
 *
 * Returns:
 * Pointer to the value, or NULL if the key was not found.
 */
static inline const fy_generic *fy_generic_mapping_get_valuep(fy_generic map, fy_generic key)
{
	return fy_generic_mapping_get_valuep_index(map, key, NULL);
}

/**
 * fy_generic_mapping_get_at_valuep() - Get a pointer to a value at a given index.
 *
 * @map: A mapping generic value.
 * @idx: Zero-based pair index.
 *
 * Returns:
 * Pointer to the value at @idx, or NULL if out of range or the mapping is invalid.
 */
static inline const fy_generic *fy_generic_mapping_get_at_valuep(fy_generic map, const size_t idx)
{
	return fy_generic_mappingp_get_at_valuep(fy_generic_mapping_resolve(map), idx);
}

/**
 * fy_generic_mapping_get_value_index() - Look up a value by key, returning it and its index.
 *
 * @map:  A mapping generic value.
 * @key:  The key to search for.
 * @idxp: Receives the found pair index, or (size_t)-1 if not found (may be NULL).
 *
 * Returns:
 * The value, or %fy_invalid if not found.
 */
static inline fy_generic fy_generic_mapping_get_value_index(fy_generic map, fy_generic key, size_t *idxp)
{
	const fy_generic *vp = fy_generic_mapping_get_valuep_index(map, key, idxp);
	return vp ? *vp : fy_invalid;
}

/**
 * fy_generic_mapping_get_value() - Look up a value by key in a mapping.
 *
 * @map: A mapping generic value.
 * @key: The key to search for.
 *
 * Returns:
 * The value, or %fy_invalid if not found.
 */
static inline fy_generic fy_generic_mapping_get_value(fy_generic map, fy_generic key)
{
	return fy_generic_mapping_get_value_index(map, key, NULL);
}

/**
 * fy_generic_mappingp_get_at_value() - Get a value at a given index as a value (mapping pointer).
 *
 * @mapp: Pointer to a &fy_generic_mapping (may be NULL).
 * @idx:  Zero-based pair index.
 *
 * Returns:
 * The value at @idx, or %fy_invalid if out of range or @mapp is NULL.
 */
static inline fy_generic fy_generic_mappingp_get_at_value(const fy_generic_mapping *mapp, const size_t idx)
{
	if (!mapp || idx >= mapp->count)
		return fy_invalid;
	return mapp->pairs[idx].value;
}

/**
 * fy_generic_mapping_get_at_value() - Get a value at a given index as a value.
 *
 * @map: A mapping generic value.
 * @idx: Zero-based pair index.
 *
 * Returns:
 * The value at @idx, or %fy_invalid if out of range or the mapping is invalid.
 */
static inline fy_generic fy_generic_mapping_get_at_value(fy_generic map, const size_t idx)
{
	return fy_generic_mappingp_get_at_value(fy_generic_mapping_resolve(map), idx);
}

/**
 * fy_generic_mapping_get_pair_count() - Get the number of key/value pairs in a mapping.
 *
 * @map: A mapping generic value.
 *
 * Returns:
 * The number of pairs, or 0 if the mapping is empty or invalid.
 */
static inline size_t fy_generic_mapping_get_pair_count(fy_generic map)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return mapp ? mapp->count : 0;
}

/**
 * fy_generic_collection_get_items() - Get the raw item array of a sequence or mapping.
 *
 * Returns a pointer to the contiguous array of generic items stored in the collection.
 * For a sequence this is the element array; for a mapping it is the interleaved
 * key/value array (pairs x 2 items).  Indirect wrappers are transparently resolved.
 *
 * @v:      A sequence or mapping generic value.
 * @countp: Receives the number of items (elements for sequences,
 *          2 x pair-count for mappings).
 *
 * Returns:
 * Pointer to the first item, or NULL if @v is not a collection or is invalid.
 */
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

/**
 * FY_GENERIC_LVAL_TEMPLATE() - Generate typed accessor/cast functions for a C scalar type.
 *
 * Generates a family of inline functions for a specific C type that map it onto
 * the underlying integer, unsigned-integer, or floating-point storage class:
 *
 * - fy_generic_get_<gtype>_no_check()      - extract value, no type check
 * - fy_generic_<gtype>_is_in_range_no_check() - range check without type check
 * - fy_generic_<gtype>_is_in_range()       - full range check
 * - fy_generic_is_direct_<gtype>()         - direct type predicate
 * - fy_generic_is_<gtype>()                - type predicate (resolves indirect)
 * - fy_generic_in_place_<gtype>()          - encode as inplace value
 * - fy_generic_out_of_place_size_<gtype>() - out-of-place storage size
 * - fy_generic_out_of_place_put_<gtype>()  - write out-of-place representation
 * - fy_generic_cast_<gtype>_default()      - cast generic to C type with default
 * - fy_generic_cast_<gtype>()              - cast generic to C type
 * - fy_genericp_cast_<gtype>_default()     - pointer variant with default
 * - fy_genericp_cast_<gtype>()             - pointer variant
 * - fy_generic_sequencep_get_<gtype>_itemp() - typed sequence item pointer
 * - fy_generic_sequence_get_<gtype>_itemp()  - typed sequence item pointer (handle)
 * - fy_generic_sequencep_get_<gtype>_default() - typed sequence item with default
 * - fy_generic_sequence_get_<gtype>_default()  - typed sequence item (handle)
 * - fy_generic_mappingp_get_<gtype>_valuep()   - typed mapping value pointer
 * - fy_generic_mapping_get_<gtype>_valuep()    - typed mapping value pointer (handle)
 * - fy_generic_mappingp_get_<gtype>_default()  - typed mapping value with default
 * - fy_generic_mapping_get_<gtype>_default()   - typed mapping value (handle)
 * - (and similar _at_ variants for positional access)
 *
 * @_ctype:     The C type (e.g. int, unsigned long).
 * @_gtype:     The token name used in generated identifiers (e.g. int, unsigned_long).
 * @_gttype:    The generic type tag category (INT, UINT, FLOAT, BOOL, NULL).
 * @_xctype:    The underlying storage C type (long long, unsigned long long, double, etc.).
 * @_xgtype:    The underlying storage type name (int_type, uint_type, float_type, etc.).
 * @_xminv:     Minimum representable value for range-checking.
 * @_xmaxv:     Maximum representable value for range-checking.
 * @_default_v: Default value returned when conversion fails.
 */
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
static inline const fy_generic *fy_generic_sequencep_get_ ## _gtype ## _itemp(const fy_generic_sequence *seqp, const size_t idx) \
{ \
	const fy_generic *vp = fy_generic_sequencep_get_itemp(seqp, idx); \
	return vp && fy_generic_is_direct_ ## _gtype (*vp) ? vp : NULL; \
} \
\
static inline const fy_generic *fy_generic_sequence_get_ ## _gtype ## _itemp(fy_generic seq, const size_t idx) \
{ \
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq); \
	return fy_generic_sequencep_get_ ## _gtype ## _itemp(seqp, idx); \
} \
\
static inline _ctype fy_generic_sequencep_get_ ## _gtype ## _default(const fy_generic_sequence *seqp, const size_t idx, _ctype default_value) \
{ \
	const fy_generic *vp = fy_generic_sequencep_get_ ## _gtype ## _itemp(seqp, idx); \
	return fy_genericp_cast_  ## _gtype ## _default(vp, default_value); \
} \
\
static inline _ctype fy_generic_sequence_get_ ## _gtype ## _default(fy_generic seq, const size_t idx, _ctype default_value) \
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
static inline const fy_generic *fy_generic_mappingp_get_at_ ## _gtype ## _valuep(const fy_generic_mapping *mapp, const size_t idx) \
{ \
	const fy_generic *vp = fy_generic_mappingp_get_at_valuep(mapp, idx); \
	return vp && fy_generic_is_direct_ ## _gtype (*vp) ? vp : NULL; \
} \
\
static inline const fy_generic *fy_generic_mapping_get_at_ ## _gtype ## _valuep(fy_generic map, const size_t idx) \
{ \
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map); \
	return fy_generic_mappingp_get_at_ ## _gtype ## _valuep(mapp, idx); \
} \
\
static inline _ctype fy_generic_mappingp_get_at_ ## _gtype ## _default(const fy_generic_mapping *mapp, const size_t idx, _ctype default_value) \
{ \
	const fy_generic *vp = fy_generic_mappingp_get_at_ ## _gtype ## _valuep(mapp, idx); \
	return fy_genericp_cast_  ## _gtype ## _default(vp, default_value); \
} \
\
static inline _ctype fy_generic_mapping_get_at_ ## _gtype ## _default(fy_generic map, const size_t idx, _ctype default_value) \
{ \
	const fy_generic *vp = fy_generic_mapping_get_at_ ## _gtype ## _valuep(map, idx); \
	return fy_genericp_cast_  ## _gtype ## _default(vp, default_value); \
} \
\
static inline const fy_generic *fy_generic_mappingp_get_at_ ## _gtype ## _keyp(const fy_generic_mapping *mapp, const size_t idx) \
{ \
	const fy_generic *vp = fy_generic_mappingp_get_at_keyp(mapp, idx); \
	return vp && fy_generic_is_direct_ ## _gtype (*vp) ? vp : NULL; \
} \
\
static inline const fy_generic *fy_generic_mapping_get_at_ ## _gtype ## _keyp(fy_generic map, const size_t idx) \
{ \
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map); \
	return fy_generic_mappingp_get_at_ ## _gtype ## _keyp(mapp, idx); \
} \
\
static inline const fy_generic *fy_generic_mapping_get_key_at_ ## _gtype ## _valuep(fy_generic map, const size_t idx) \
{ \
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map); \
	return fy_generic_mappingp_get_at_ ## _gtype ## _keyp(mapp, idx); \
} \
\
static inline _ctype fy_generic_mappingp_get_key_at_ ## _gtype ## _default(const fy_generic_mapping *mapp, const size_t idx, _ctype default_value) \
{ \
	const fy_generic *vp = fy_generic_mappingp_get_at_ ## _gtype ## _keyp(mapp, idx); \
	return fy_genericp_cast_  ## _gtype ## _default(vp, default_value); \
} \
\
static inline _ctype fy_generic_mapping_get_key_at_ ## _gtype ## _default(fy_generic map, const size_t idx, _ctype default_value) \
{ \
	const fy_generic *vp = fy_generic_mapping_get_at_ ## _gtype ## _keyp(map, idx); \
	return fy_genericp_cast_  ## _gtype ## _default(vp, default_value); \
} \
struct fy_useless_struct_for_semicolon

/* Range check: null is only in range when the pointer is NULL */
static inline bool fy_null_is_in_range(const void *v)
{
	return v == NULL;
}

FY_GENERIC_LVAL_TEMPLATE(void *, null, NULL, void *, null_type, NULL, NULL, NULL);

/* Range check: every bool value is valid */
static inline bool fy_bool_is_in_range(const bool v)
{
	return true;
}

FY_GENERIC_LVAL_TEMPLATE(bool, bool, BOOL, bool, bool_type, false, true, false);

/**
 * FY_GENERIC_INT_LVAL_TEMPLATE() - Specialise FY_GENERIC_LVAL_TEMPLATE for signed integer types.
 *
 * Defines fy_<gtype>_is_in_range() as a [_xminv, _xmaxv] check on a long long
 * value and then expands FY_GENERIC_LVAL_TEMPLATE using the int_type storage class.
 *
 * @_ctype:    The C type (e.g. int, short).
 * @_gtype:    Token name (e.g. int, short).
 * @_xminv:    Minimum value (e.g. INT_MIN).
 * @_xmaxv:    Maximum value (e.g. INT_MAX).
 * @_defaultv: Default value on conversion failure.
 */
#define FY_GENERIC_INT_LVAL_TEMPLATE(_ctype, _gtype, _xminv, _xmaxv, _defaultv) \
static inline bool fy_ ## _gtype ## _is_in_range(const long long v) \
{ \
	return v >= _xminv && v <= _xmaxv; \
} \
\
FY_GENERIC_LVAL_TEMPLATE(_ctype, _gtype, INT, long long, int_type, _xminv, _xmaxv, _defaultv)

/**
 * FY_GENERIC_UINT_LVAL_TEMPLATE() - Specialise FY_GENERIC_LVAL_TEMPLATE for unsigned integer types.
 *
 * Defines fy_<gtype>_is_in_range() as a [0, _xmaxv] check on an unsigned long long
 * value and then expands FY_GENERIC_LVAL_TEMPLATE using the uint_type storage class.
 *
 * @_ctype:    The C type (e.g. unsigned int, unsigned long).
 * @_gtype:    Token name (e.g. unsigned_int, unsigned_long).
 * @_xminv:    Minimum value (always 0 for unsigned types).
 * @_xmaxv:    Maximum value (e.g. UINT_MAX).
 * @_defaultv: Default value on conversion failure.
 */
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

/**
 * FY_GENERIC_FLOAT_LVAL_TEMPLATE() - Specialise FY_GENERIC_LVAL_TEMPLATE for floating-point types.
 *
 * Defines fy_<gtype>_is_in_range() accepting any non-normal value (NaN, inf, subnormal)
 * and normal values within [_xminv, _xmaxv], then expands FY_GENERIC_LVAL_TEMPLATE
 * using the float_type storage class.
 *
 * @_ctype:    The C type (float or double).
 * @_gtype:    Token name (float or double).
 * @_xminv:    Minimum normal value (e.g. -FLT_MAX).
 * @_xmaxv:    Maximum normal value (e.g. FLT_MAX).
 * @_defaultv: Default value on conversion failure.
 */
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

/* _Generic association: _ctype -> _base_<gtype>() */
#define FY_GENERIC_SCALAR_DISPATCH(_base, _ctype, _gtype) \
	_ctype: _base ## _ ## _gtype

/* _Generic association with suffix: _ctype -> _base_<gtype>_<sfx>() */
#define FY_GENERIC_SCALAR_DISPATCH_SFX(_base, _sfx, _ctype, _gtype) \
	_ctype: _base ## _ ## _gtype ## _ ## _sfx

/* Expand _Generic associations for all scalar types, dispatching to _base_<gtype>() */
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

/* Expand _Generic associations for all scalar types with suffix, dispatching to _base_<gtype>_<sfx>() */
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

/* Byte offset from the start of a fy_generic to the first character of an inplace string (LE=1, BE=0) */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define FY_INPLACE_STRING_ADV	1
#define FY_INPLACE_STRING_SHIFT	8
#else
#define FY_INPLACE_STRING_ADV	0
#define FY_INPLACE_STRING_SHIFT	0
#endif

/* if we can get the address of the argument, then we can return a pointer to it */

/**
 * fy_generic_get_string_inplace_size() - Extract the length of an inplace string.
 *
 * @v: A generic value with tag FY_STRING_INPLACE_V. Behaviour is undefined otherwise.
 *
 * Returns:
 * Length of the inplace string in bytes (0–7 on 64-bit, 0–3 on 32-bit).
 */
static inline size_t
fy_generic_get_string_inplace_size(const fy_generic v)
{
	assert((v.v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V);
	return (size_t)((v.v >> FY_STRING_INPLACE_SIZE_SHIFT) & FYGT_STRING_INPLACE_SIZE_MASK);
}

/**
 * fy_genericp_get_string_inplace() - Return a pointer to the inplace string bytes.
 *
 * The returned pointer is into the storage of @vp itself.  The caller must
 * ensure @vp lives at least as long as the returned pointer is used.
 *
 * @vp: Pointer to a generic with tag FY_STRING_INPLACE_V.
 *
 * Returns:
 * Pointer to the first character of the inplace string (not NUL-terminated).
 */
static const char *
fy_genericp_get_string_inplace(const fy_generic *vp)
{
	assert(((*vp).v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V);
	return (const char *)vp + FY_INPLACE_STRING_ADV;
}

/**
 * fy_genericp_get_string_size_no_check() - Get string pointer and length without type check.
 *
 * Returns the string data and length for both inplace and out-of-place strings,
 * without verifying that @vp actually holds a string.
 *
 * @vp:   Pointer to a generic known to hold a string (inplace or outplace).
 * @lenp: Receives the length of the string in bytes.
 *
 * Returns:
 * Pointer to the NUL-terminated string data.
 */
static inline const char *
fy_genericp_get_string_size_no_check(const fy_generic *vp, size_t *lenp)
{
	if (((*vp).v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V) {
		*lenp = fy_generic_get_string_inplace_size(*vp);
		return (const char *)vp + FY_INPLACE_STRING_ADV;
	}
	return (const char *)fy_decode_size_nocheck(fy_generic_resolve_ptr(*vp), lenp);
}

/**
 * fy_genericp_get_string_no_check() - Get string pointer without type check or length.
 *
 * @vp: Pointer to a generic known to hold a string (inplace or outplace).
 *
 * Returns:
 * Pointer to the NUL-terminated string data.
 */
static inline const char *
fy_genericp_get_string_no_check(const fy_generic *vp)
{
	if (((*vp).v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V)
		return (const char *)vp + FY_INPLACE_STRING_ADV;

	return (const char *)fy_skip_size_nocheck(fy_generic_resolve_ptr(*vp));
}

/**
 * fy_genericp_get_string_size() - Get string pointer and length from a generic pointer.
 *
 * Handles all string variants (inplace, outplace, indirect).  When the underlying
 * value is an inplace string a temporary aligned copy is alloca'd so that a stable
 * pointer can be returned.
 *
 * @_vp:   Pointer to a generic value (may be indirect).  May be NULL.
 * @_lenp: Receives the string length.  Set to 0 on failure.
 *
 * Returns:
 * Pointer to the NUL-terminated string, or NULL on error / non-string.
 */
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

/**
 * fy_genericp_get_string_default() - Get string pointer from a generic pointer, with a default.
 *
 * @_vp:        Pointer to a generic value.  May be NULL.
 * @_default_v: Returned when @_vp is NULL or not a string.
 *
 * Returns:
 * The string pointer, or @_default_v on failure.
 */
#define fy_genericp_get_string_default(_vp, _default_v)			\
	({								\
		const char *__ret;					\
		size_t __len;						\
		__ret = fy_genericp_get_string_size((_vp), &__len);	\
		if (!__ret)						\
			__ret = (_default_v);				\
		__ret;							\
	})

/* Get string pointer from a generic pointer, returning "" on failure */
#define fy_genericp_get_string(_vp) (fy_genericp_get_string_default((_vp), ""))

/**
 * fy_generic_get_string_size_alloca() - Get string pointer and length from a generic value.
 *
 * Like fy_genericp_get_string_size() but takes a value directly.  For inplace strings
 * an aligned alloca copy is made so the returned pointer remains valid for the
 * calling stack frame.
 *
 * @_v:    A generic value (may be indirect).
 * @_lenp: Receives the string length.  Set to 0 on failure.
 *
 * Returns:
 * Pointer to the NUL-terminated string, or NULL on error / non-string.
 */
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

/**
 * fy_generic_get_string_default_alloca() - Get string from a generic value with a default.
 *
 * @_v:        A generic value.
 * @_default_v: Returned when @_v is not a string.
 *
 * Returns:
 * The string pointer, or @_default_v on failure.
 */
#define fy_generic_get_string_default_alloca(_v, _default_v)					\
	({											\
		const char *__ret;								\
		size_t __len;									\
		__ret = fy_generic_get_string_size_alloca((_v), &__len);			\
		if (!__ret)									\
			__ret = (_default_v);							\
		__ret;										\
	})

/* Get string from a generic value using alloca for inplace strings, returning "" on failure */
#define fy_generic_get_string_alloca(_v) (fy_generic_get_string_default_alloca((_v), ""))

/**
 * fy_genericp_get_const_char_ptr_default() - Get a const char pointer from a generic pointer.
 *
 * Only works for out-of-place strings (heap-allocated).  Inplace strings cannot be
 * returned as a stable pointer; use fy_genericp_get_string_size() for those.
 *
 * @vp:            Pointer to a generic value (indirect is resolved).  May be NULL.
 * @default_value: Returned when @vp is NULL or not an out-of-place string.
 *
 * Returns:
 * Pointer to the NUL-terminated string, or @default_value on failure.
 */
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

/**
 * fy_genericp_get_const_char_ptr() - Get a const char pointer from a generic pointer.
 *
 * Returns "" when @vp is NULL or not an out-of-place string.
 *
 * @vp: Pointer to a generic value.
 *
 * Returns:
 * Pointer to the string, or "" on failure.
 */
static inline const char *fy_genericp_get_const_char_ptr(const fy_generic *vp)
{
	return fy_genericp_get_const_char_ptr_default(vp, "");
}

/**
 * fy_genericp_get_char_ptr_default() - Get a mutable char pointer from a generic pointer.
 *
 * Same as fy_genericp_get_const_char_ptr_default() but returns a non-const pointer.
 *
 * @vp:            Pointer to a generic value.
 * @default_value: Returned on failure.
 *
 * Returns:
 * Mutable pointer to the string, or @default_value on failure.
 */
static inline char *fy_genericp_get_char_ptr_default(fy_generic *vp, const char *default_value)
{
	return (char *)fy_genericp_get_const_char_ptr_default(vp, default_value);
}

/**
 * fy_genericp_get_char_ptr() - Get a mutable char pointer from a generic pointer.
 *
 * Returns "" when @vp is NULL or not an out-of-place string.
 *
 * @vp: Pointer to a generic value.
 *
 * Returns:
 * Mutable pointer to the string, or "" on failure.
 */
static inline char *fy_genericp_get_char_ptr(fy_generic *vp)
{
	return fy_genericp_get_char_ptr_default(vp, "");
}

/**
 * fy_generic_get_alias_alloca() - Get the anchor name string from an alias generic.
 *
 * Extracts the anchor (string) from an alias value using alloca for inplace storage.
 *
 * @_v: An alias generic value.
 *
 * Returns:
 * Pointer to the NUL-terminated anchor name, or "" if @_v is not an alias.
 */
#define fy_generic_get_alias_alloca(_v) \
	({ \
		fy_generic __va = fy_generic_get_anchor((_v)); \
		fy_generic_get_string_alloca(__va); \
	})

/**
 * fy_bool() - Create a bool generic from a C boolean expression.
 *
 * @_v: Boolean expression.
 *
 * Returns:
 * fy_true if @_v is non-zero, fy_false otherwise.
 */
#define fy_bool(_v) \
	((bool)(_v) ? fy_true : fy_false)

/* fy_local_bool() - Alias for fy_bool() for local (stack-scoped) contexts */
#define fy_local_bool(_v) \
	(fy_bool((_v)))

/**
 * fy_int_is_unsigned() - Detect whether an integer constant exceeds LLONG_MAX.
 *
 * Uses _Generic to check at compile time whether an unsigned long long value
 * is above LLONG_MAX, which indicates it requires unsigned representation.
 *
 * @_v: An integer expression.
 *
 * Returns:
 * true if @_v is an unsigned long long greater than LLONG_MAX, false otherwise.
 */
#define fy_int_is_unsigned(_v) 									\
	(_Generic(_v, 										\
		unsigned long long: ((unsigned long long)_v > (unsigned long long)LLONG_MAX),	\
		default: false))

/**
 * fy_int_alloca() - Create an integer generic using alloca for out-of-place values.
 *
 * Encodes @_v inplace when it fits in FYGT_INT_INPLACE_BITS; otherwise allocates
 * a fy_generic_decorated_int on the stack.  The result is valid only for the
 * duration of the current stack frame.
 *
 * @_v: Any integer expression.
 *
 * Returns:
 * A fy_generic_value encoding @_v.
 */
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
			__vp->flags = fy_int_is_unsigned(__v) ? FYGDIF_UNSIGNED_RANGE_EXTEND : 0; \
			_r = (fy_generic_value)__vp | FY_INT_OUTPLACE_V;			\
		}										\
		_r;										\
	})

/* Evaluate _v only when it is a compile-time constant, returning 0 otherwise */
#define fy_ensure_const(_v) \
	(__builtin_constant_p(_v) ? (_v) : 0)

/*
 * fy_int_const() - Create an integer generic from a compile-time constant.
 *
 * Encodes the constant inplace when possible; otherwise stores it in a file-scope
 * static object so no alloca is needed.  @_v MUST be a compile-time constant.
 *
 * @_v: A compile-time integer constant.
 *
 * Returns:
 * A fy_generic_value encoding @_v with static lifetime.
 */
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
				.flags = fy_int_is_unsigned(_v) ? FYGDIF_UNSIGNED_RANGE_EXTEND : 0, \
			};									\
			assert(((uintptr_t)&_vv & FY_INPLACE_TYPE_MASK) == 0);			\
			_r = (fy_generic_value)&_vv | FY_INT_OUTPLACE_V;			\
		}										\
		_r;										\
	})

/* Create an integer generic, using fy_int_const for constants and fy_int_alloca otherwise */
#define fy_local_int(_v) \
	((fy_generic){ .v = (__builtin_constant_p(_v) ? fy_int_const(_v) : fy_int_alloca(_v)) })

/**
 * fy_int() - Create an integer generic from any integer expression.
 *
 * Selects the most efficient encoding at compile time: inplace for small constants,
 * static storage for large constants, or alloca for runtime values.
 *
 * @_v: An integer expression of any width.
 *
 * Returns:
 * A fy_generic holding @_v.
 */
#define fy_int(_v) \
	fy_local_int((_v))

/* we are making things very hard for the compiler; sometimes he gets lost */
FY_DIAG_PUSH
FY_DIAG_IGNORE_ARRAY_BOUNDS

/**
 * fy_generic_in_place_char_ptr_len() - Attempt to encode a string inplace.
 *
 * Packs up to 7 bytes (64-bit) or 3 bytes (32-bit) of @p directly into the
 * generic word.  Returns fy_invalid_value when the string is too long for
 * inplace storage.
 *
 * @p:   String data (need not be NUL-terminated).
 * @len: Length of @p in bytes.
 *
 * Returns:
 * Encoded fy_generic_value on success, fy_invalid_value when @len is too large.
 */
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

FY_DIAG_POP

/**
 * fy_generic_in_place_char_ptr() - Attempt to encode a NUL-terminated string inplace.
 *
 * @p: NUL-terminated string.  May be NULL (returns fy_invalid_value).
 *
 * Returns:
 * Encoded fy_generic_value on success, fy_invalid_value when the string is too long.
 */
static inline fy_generic_value fy_generic_in_place_char_ptr(const char *p)
{
	if (!p)
		return fy_invalid_value;

	return fy_generic_in_place_char_ptr_len(p, strlen(p));
}

/**
 * fy_generic_in_place_const_szstrp() - Attempt to encode a sized string inplace.
 *
 * @szstrp: Pointer to a sized string.  May be NULL (returns fy_invalid_value).
 *
 * Returns:
 * Encoded fy_generic_value on success, fy_invalid_value when the string is too long.
 */
static inline fy_generic_value fy_generic_in_place_const_szstrp(const fy_generic_sized_string *szstrp)
{
	if (!szstrp)
		return fy_invalid_value;

	return fy_generic_in_place_char_ptr_len(szstrp->data, szstrp->size);
}

/**
 * fy_generic_in_place_szstr() - Attempt to encode a sized string value inplace.
 *
 * @szstr: A sized string by value.
 *
 * Returns:
 * Encoded fy_generic_value, or fy_invalid_value when too long.
 */
static inline fy_generic_value fy_generic_in_place_szstr(const fy_generic_sized_string szstr)
{
	return fy_generic_in_place_const_szstrp(&szstr);
}

/**
 * fy_generic_in_place_const_dintp() - Attempt to encode a decorated int inplace.
 *
 * Delegates to fy_generic_in_place_int_type() or fy_generic_in_place_uint_type()
 * depending on the FYGDIF_UNSIGNED_RANGE_EXTEND flag.
 *
 * @dintp: Pointer to the decorated int.  May be NULL (returns fy_invalid_value).
 *
 * Returns:
 * Encoded fy_generic_value, or fy_invalid_value when too large for inplace.
 */
static inline fy_generic_value fy_generic_in_place_const_dintp(const fy_generic_decorated_int *dintp)
{
	if (!dintp)
		return fy_invalid_value;

	return !(dintp->flags & FYGDIF_UNSIGNED_RANGE_EXTEND) ?
			fy_generic_in_place_int_type(dintp->sv) :
			fy_generic_in_place_uint_type(dintp->uv);
}

/**
 * fy_generic_in_place_dint() - Attempt to encode a decorated int by value inplace.
 *
 * @dint: A decorated int value.
 *
 * Returns:
 * Encoded fy_generic_value, or fy_invalid_value when too large for inplace.
 */
static inline fy_generic_value fy_generic_in_place_dint(const fy_generic_decorated_int dint)
{
	return fy_generic_in_place_const_dintp(&dint);
}

/* Pass-through: a generic is its own inplace encoding */
static inline fy_generic_value fy_generic_in_place_generic(fy_generic v)
{
	return v.v;
}

/**
 * fy_generic_in_place_sequence_handle() - Encode a sequence handle as a generic value.
 *
 * A NULL handle encodes as fy_seq_empty.  The handle pointer must be aligned
 * to FY_GENERIC_CONTAINER_ALIGN; misaligned pointers return fy_invalid_value.
 *
 * @seqh: A sequence handle.
 *
 * Returns:
 * Encoded fy_generic_value, fy_seq_empty_value for NULL, or fy_invalid_value on misalignment.
 */
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

/**
 * fy_generic_in_place_mapping_handle() - Encode a mapping handle as a generic value.
 *
 * A NULL handle encodes as fy_map_empty.  The handle pointer must be aligned
 * to FY_GENERIC_CONTAINER_ALIGN; misaligned pointers return fy_invalid_value.
 *
 * @maph: A mapping handle.
 *
 * Returns:
 * Encoded fy_generic_value, fy_map_empty_value for NULL, or fy_invalid_value on misalignment.
 */
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

/* fy_generic_in_place_generic_builderp() - Stub: builders cannot be inlined (always returns fy_invalid_value) */
static inline fy_generic_value fy_generic_in_place_generic_builderp(struct fy_generic_builder *gb)
{
	return fy_invalid_value;
}

/* fy_to_generic_inplace_Generic_dispatch - _Generic association list for inplace encoding of all supported types */
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
	struct fy_generic_builder *: fy_generic_in_place_generic_builderp

/**
 * fy_to_generic_inplace() - Encode a value as an inplace generic using _Generic dispatch.
 *
 * Attempts inplace encoding (no allocation) for all supported types.  Returns
 * fy_invalid_value when the value does not fit inplace (e.g. strings longer
 * than 7 bytes on 64-bit) and out-of-place encoding must be used instead.
 *
 * @_v: Value of any supported C type.
 *
 * Returns:
 * Encoded fy_generic_value, or fy_invalid_value if out-of-place is required.
 */
#define fy_to_generic_inplace(_v) ( _Generic((_v), fy_to_generic_inplace_Generic_dispatch)(_v))

/**
 * fy_generic_out_of_place_size_char_ptr() - Byte count needed for out-of-place storage of a C string.
 *
 * Returns the number of bytes required to store the length prefix and the
 * null-terminated string data in an out-of-place buffer.  Returns 0 for NULL.
 *
 * @p: Null-terminated C string, or NULL.
 *
 * Returns:
 * Storage size in bytes, or 0 for NULL.
 */
static inline size_t fy_generic_out_of_place_size_char_ptr(const char *p)
{
	if (!p)
		return 0;

	return FYGT_SIZE_ENCODING_MAX + strlen(p) + 1;
}

/* fy_generic_out_of_place_size_generic() - Always 0; generics do not need out-of-place storage */
static inline size_t fy_generic_out_of_place_size_generic(fy_generic v)
{
	/* should never have to do that */
	return 0;
}

/**
 * fy_generic_out_of_place_size_const_szstrp() - Byte count for out-of-place storage of a sized string pointer.
 *
 * @szstrp: Pointer to a sized string descriptor, or NULL.
 *
 * Returns:
 * Storage size in bytes (length prefix + string bytes + NUL), or 0 for NULL.
 */
static inline size_t fy_generic_out_of_place_size_const_szstrp(const fy_generic_sized_string *szstrp)
{
	if (!szstrp)
		return 0;

	return FYGT_SIZE_ENCODING_MAX + szstrp->size + 1;
}

/* fy_generic_out_of_place_size_szstr() - Byte count for out-of-place storage of a sized string (by value) */
static inline size_t fy_generic_out_of_place_size_szstr(fy_generic_sized_string szstr)
{
	return fy_generic_out_of_place_size_const_szstrp(&szstr);
}

/**
 * fy_generic_out_of_place_size_const_dintp() - Byte count for out-of-place storage of a decorated int pointer.
 *
 * Dispatches to the signed or unsigned variant depending on FYGDIF_UNSIGNED_RANGE_EXTEND.
 *
 * @dintp: Pointer to a decorated int descriptor, or NULL.
 *
 * Returns:
 * Storage size in bytes, or 0 for NULL.
 */
static inline size_t fy_generic_out_of_place_size_const_dintp(const fy_generic_decorated_int *dintp)
{
	if (!dintp)
		return 0;

	return !(dintp->flags & FYGDIF_UNSIGNED_RANGE_EXTEND) ?
			fy_generic_out_of_place_size_long_long(dintp->sv) :
			fy_generic_out_of_place_size_unsigned_long_long(dintp->uv);
}

/* fy_generic_out_of_place_size_dint() - Byte count for out-of-place storage of a decorated int (by value) */
static inline size_t fy_generic_out_of_place_size_dint(fy_generic_decorated_int dint)
{
	return fy_generic_out_of_place_size_const_dintp(&dint);
}

/* fy_generic_out_of_place_size_sequence_handle() - Stub: deep-copy size not yet implemented */
static inline size_t fy_generic_out_of_place_size_sequence_handle(fy_generic_sequence_handle seqh)
{
	return 0;	// TODO calculate
}

/* fy_generic_out_of_place_size_mapping_handle() - Stub: deep-copy size not yet implemented */
static inline size_t fy_generic_out_of_place_size_mapping_handle(fy_generic_mapping_handle maph)
{
	return 0;	// TODO calculate
}

/* fy_generic_out_of_place_size_generic_builderp() - Stub: deep-copy size not yet implemented */
static inline size_t fy_generic_out_of_place_size_generic_builderp(struct fy_generic_builder *gb)
{
	return 0;	// TODO calculate
}

/* fy_to_generic_outofplace_size_Generic_dispatch - _Generic association list for computing out-of-place buffer sizes */
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

/**
 * fy_to_generic_outofplace_size() - Compute buffer size needed for out-of-place encoding of a value.
 *
 * @_v: Value of any supported C type.
 *
 * Returns:
 * Number of bytes required, or 0 if the value can be encoded inplace or is unsupported.
 */
#define fy_to_generic_outofplace_size(_v) \
	(_Generic((_v), fy_to_generic_outofplace_size_Generic_dispatch)(_v))

/**
 * fy_generic_out_of_place_put_char_ptr() - Write a C string into an out-of-place buffer.
 *
 * Encodes the length prefix followed by the string bytes and a NUL terminator
 * into @buf, which must be aligned to FY_GENERIC_CONTAINER_ALIGN.
 *
 * @buf: Pre-allocated buffer of at least fy_generic_out_of_place_size_char_ptr() bytes.
 * @p:   Null-terminated C string (must not be NULL).
 *
 * Returns:
 * Encoded FY_STRING_OUTPLACE_V generic value pointing into @buf,
 * or fy_invalid_value for NULL @p.
 */
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

/**
 * fy_generic_out_of_place_put_const_szstrp() - Write a sized string into an out-of-place buffer.
 *
 * @buf:    Pre-allocated aligned buffer.
 * @szstrp: Sized string descriptor (must not be NULL).
 *
 * Returns:
 * Encoded FY_STRING_OUTPLACE_V generic value, or fy_invalid_value for NULL.
 */
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

/* fy_generic_out_of_place_put_szstr() - Write a sized string (by value) into an out-of-place buffer */
static inline fy_generic_value fy_generic_out_of_place_put_szstr(void *buf, fy_generic_sized_string szstrp)
{
	return fy_generic_out_of_place_put_const_szstrp(buf, &szstrp);
}

/**
 * fy_generic_out_of_place_put_const_dintp() - Write a decorated int into an out-of-place buffer.
 *
 * Dispatches to the signed or unsigned out-of-place encoder based on
 * FYGDIF_UNSIGNED_RANGE_EXTEND.
 *
 * @buf:   Pre-allocated aligned buffer.
 * @dintp: Decorated int descriptor (must not be NULL).
 *
 * Returns:
 * Encoded generic value, or fy_invalid_value for NULL.
 */
static inline fy_generic_value fy_generic_out_of_place_put_const_dintp(void *buf, const fy_generic_decorated_int *dintp)
{
	if (!dintp)
		return fy_invalid_value;

	return !(dintp->flags & FYGDIF_UNSIGNED_RANGE_EXTEND) ?
			fy_generic_out_of_place_put_long_long(buf, dintp->sv) :
			fy_generic_out_of_place_put_unsigned_long_long(buf, dintp->uv);
}

/* fy_generic_out_of_place_put_dint() - Write a decorated int (by value) into an out-of-place buffer */
static inline fy_generic_value fy_generic_out_of_place_put_dint(void *buf, fy_generic_decorated_int dint)
{
	return fy_generic_out_of_place_put_const_dintp(buf, &dint);
}

/* fy_generic_out_of_place_put_generic() - Stub: generics do not need out-of-place put (always fy_invalid_value) */
static inline fy_generic_value fy_generic_out_of_place_put_generic(void *buf, fy_generic v)
{
	/* we just pass-through, should never happen */
	return fy_invalid_value;
}

/* fy_generic_out_of_place_put_sequence_handle() - Stub: sequence handles need no out-of-place put */
static inline fy_generic_value fy_generic_out_of_place_put_sequence_handle(void *buf, fy_generic_sequence_handle seqh)
{
	return fy_invalid_value;	// shoud never happen?
}

/* fy_generic_out_of_place_put_mapping_handle() - Stub: mapping handles need no out-of-place put */
static inline fy_generic_value fy_generic_out_of_place_put_mapping_handle(void *buf, fy_generic_mapping_handle maph)
{
	return fy_invalid_value;	// shoud never happen?
}

/* fy_generic_out_of_place_put_generic_builderp() - Stub: builders need no out-of-place put */
static inline fy_generic_value fy_generic_out_of_place_put_generic_builderp(void *buf, struct fy_generic_builder *gb)
{
	return fy_invalid_value;	// shoud never happen?
}

/* fy_to_generic_outofplace_put_Generic_dispatch - _Generic association list for writing values into out-of-place buffers */
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

/**
 * fy_to_generic_outofplace_put() - Write a value into a pre-allocated out-of-place buffer.
 *
 * Uses _Generic dispatch to select the correct encoder for @_v.  @_vp must
 * point to a buffer of at least fy_to_generic_outofplace_size(@_v) bytes,
 * aligned to FY_GENERIC_CONTAINER_ALIGN.
 *
 * @_vp: Destination buffer pointer.
 * @_v:  Value to encode.
 *
 * Returns:
 * Encoded out-of-place generic value.
 */
#define fy_to_generic_outofplace_put(_vp, _v) \
	(_Generic((_v), fy_to_generic_outofplace_put_Generic_dispatch)((_vp), (_v)))

/**
 * fy_local_to_generic_value() - Encode any value as a fy_generic_value using alloca for out-of-place storage.
 *
 * Tries inplace encoding first.  If that returns fy_invalid_value and the type
 * requires out-of-place storage, allocates a stack buffer and writes the value
 * there.  The resulting value is only valid for the lifetime of the enclosing
 * function (because of alloca).
 *
 * @_v: Value of any supported C type.
 *
 * Returns:
 * fy_generic_value suitable for wrapping in a fy_generic struct.
 */
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

/**
 * fy_local_to_generic() - Encode any value as a fy_generic using alloca for out-of-place storage.
 *
 * Wrapper around fy_local_to_generic_value() that returns a full fy_generic struct.
 * The result is only valid within the enclosing function's scope.
 *
 * @_v: Value of any supported C type.
 *
 * Returns:
 * fy_generic value (stack lifetime for out-of-place types).
 */
#define fy_local_to_generic(_v) \
	((fy_generic) { .v = fy_local_to_generic_value(_v) })

#ifdef FYGT_GENERIC_64

/**
 * fy_float_alloca() - Encode a double as a generic float value using alloca for out-of-place storage.
 *
 * On 64-bit targets, a double that is representable as a 32-bit float is stored
 * inplace in the generic word.  Otherwise a stack-allocated double buffer is used.
 *
 * @_v: A double value.
 *
 * Returns:
 * fy_generic_value encoding @_v (stack lifetime for out-of-place case).
 */
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

/*
 * fy_float_const() - Encode a compile-time constant double using static storage.
 *
 * On 64-bit targets stores 32-bit-representable values inplace; otherwise uses
 * a static constant double.  Must only be called with a compile-time constant
 * argument (guarded by __builtin_constant_p in fy_float()).
 *
 * @_v: A compile-time constant double.
 *
 * Returns:
 * fy_generic_value with static storage lifetime.
 */
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

/*
 * fy_float_alloca() - Encode a double as a generic float value using alloca (32-bit variant).
 *
 * On 32-bit targets all doubles are stored out-of-place on the stack.
 *
 * @_v: A double value.
 *
 * Returns:
 * fy_generic_value encoding @_v (stack lifetime).
 */
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

/*
 * fy_float_const() - Encode a compile-time constant double using static storage (32-bit variant).
 *
 * @_v: A compile-time constant double.
 *
 * Returns:
 * fy_generic_value with static storage lifetime.
 */
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

/**
 * fy_local_float() - Construct a fy_generic float value, choosing alloca or static storage.
 *
 * Uses fy_float_const() when @_v is a compile-time constant; otherwise falls
 * back to fy_float_alloca().
 *
 * @_v: A double (constant or runtime).
 *
 * Returns:
 * fy_generic wrapping the encoded float.
 */
#define fy_local_float(_v) \
	((fy_generic){ .v = __builtin_constant_p(_v) ? fy_float_const(_v) : fy_float_alloca(_v) })

/**
 * fy_float() - High-level float generic constructor.
 *
 * Convenience wrapper around fy_local_float().
 *
 * @_v: A double value.
 *
 * Returns:
 * fy_generic wrapping the encoded float.
 */
#define fy_float(_v) \
	((fy_local_float(_v)))

/**
 * FY_CONST_P_INIT() - Detect a C string literal and return its value, or "" for non-literal char*.
 *
 * C string literals decay to `char *`, but `&"literal"` has type `char (*)[N]`,
 * not `char **`.  This macro exploits that to distinguish literals from variables:
 * literals are returned as-is; non-literal char* expressions yield "".
 *
 * Used internally by fy_string_size_const() to initialize static string buffers.
 *
 * @_v: A C expression (typically a string literal or char* variable).
 *
 * Returns:
 * @_v itself for string literals; "" for other char* values; "" for all other types.
 */
/* literal strings in C, are char *
 * However you can't get a type of & "literal"
 * So we use that to detect literal strings
 */
#define FY_CONST_P_INIT(_v) \
	(_Generic((_v), \
		char *: _Generic(&(_v), char **: "", default: (_v)), \
		default: "" ))

/**
 * fy_string_size_alloca() - Encode a string with explicit length using alloca for out-of-place storage.
 *
 * Attempts inplace encoding first (strings <=7 bytes on 64-bit).  Longer
 * strings are stored in a stack-allocated buffer prefixed with the encoded
 * length.
 *
 * @_v:   Pointer to the string data (need not be NUL-terminated).
 * @_len: Length in bytes.
 *
 * Returns:
 * fy_generic_value (stack lifetime for out-of-place case).
 */
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

/**
 * fy_string_size_const() - Encode a compile-time constant string with explicit length using static storage.
 *
 * Attempts inplace encoding first.  For longer strings, stores the data in a
 * static constant struct whose first bytes hold the variable-length encoded
 * length and whose flexible array member holds the string bytes.  The struct
 * is aligned so its address can be stored as an out-of-place generic pointer.
 *
 * Must only be called when both @_v and @_len are compile-time constants
 * (guarded by __builtin_constant_p in fy_string() / fy_local_string()).
 *
 * @_v:   Pointer to the string data (must be a string literal for static init).
 * @_len: Byte length of the string (excluding any NUL terminator).
 *
 * Returns:
 * fy_generic_value with static storage lifetime.
 */
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

/**
 * fy_string_alloca() - Encode a NUL-terminated string using alloca for out-of-place storage.
 *
 * Computes the length with strlen() then delegates to fy_string_size_alloca().
 *
 * @_v: Null-terminated C string.
 *
 * Returns:
 * fy_generic_value (stack lifetime for out-of-place case).
 */
#define fy_string_alloca(_v)					\
	({							\
		const char *___v = (_v);			\
		size_t ___len = strlen(___v);			\
		fy_string_size_alloca(___v, ___len);		\
	})

/* fy_string_const() - Encode a string literal using static storage; delegates to fy_string_size_const() with sizeof - 1 */
#define fy_string_const(_v) fy_string_size_const((_v), (sizeof(_v) - 1))

/**
 * fy_local_string_size() - Construct a fy_generic string with explicit length, choosing static or alloca storage.
 *
 * @_v:   String pointer (literal for static path, any pointer for alloca path).
 * @_len: Byte length.
 *
 * Returns:
 * fy_generic wrapping the encoded string.
 */
#define fy_local_string_size(_v, _len) \
	((fy_generic){ .v = __builtin_constant_p(_v) ? fy_string_size_const((_v), (_len)) : fy_string_size_alloca((_v), (_len)) })

/**
 * fy_local_string() - Construct a fy_generic string, choosing static or alloca storage.
 *
 * @_v: String pointer (literal uses static storage; variable pointer uses alloca).
 *
 * Returns:
 * fy_generic wrapping the encoded string.
 */
#define fy_local_string(_v) \
	((fy_generic){ .v = __builtin_constant_p(_v) ? fy_string_const(_v) : fy_string_alloca(_v) })

/**
 * fy_string_size() - High-level string generic constructor with explicit byte length.
 *
 * @_v:   String data pointer.
 * @_len: Byte length.
 *
 * Returns:
 * fy_generic wrapping the encoded string.
 */
#define fy_string_size(_v, _len) \
	(fy_local_string_size((_v), (_len)))

/**
 * fy_string() - High-level string generic constructor from a NUL-terminated C string.
 *
 * @_v: Null-terminated C string.
 *
 * Returns:
 * fy_generic wrapping the encoded string.
 */
#define fy_string(_v) \
	(fy_local_string((_v)))

/**
 * fy_stringf_value() - Format a string using fy_sprintfa() and encode it as a fy_generic_value.
 *
 * @_fmt: printf-compatible format string.
 * @...:  Format arguments.
 *
 * Returns:
 * fy_generic_value encoding the formatted string (stack lifetime).
 */
#define fy_stringf_value(_fmt, ...) \
	({ \
		char *_buf = fy_sprintfa((_fmt) __VA_OPT__(,) __VA_ARGS__); \
		fy_string_alloca(_buf); \
	})

/**
 * fy_stringf() - Format a string and return it as a fy_generic.
 *
 * @_fmt: printf-compatible format string.
 * @...:  Format arguments.
 *
 * Returns:
 * fy_generic wrapping the formatted string.
 */
#define fy_stringf(_fmt, ...) \
	((fy_generic){ .v = fy_stringf_value((_fmt) __VA_OPT__(,) __VA_ARGS__) })

/**
 * fy_sequence_alloca() - Allocate a sequence on the stack and copy items into it.
 *
 * @_count: Number of elements.
 * @_items: Pointer to an array of @_count fy_generic values to copy.
 *
 * Returns:
 * fy_generic_value encoding the stack-allocated sequence (stack lifetime).
 */
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

/**
 * fy_local_sequence_create_value() - Create a fy_generic_value for a sequence, handling the empty case.
 *
 * Returns fy_seq_empty_value when @_count is 0.
 *
 * @_count: Number of elements.
 * @_items: Array of elements to copy.
 *
 * Returns:
 * fy_generic_value for the sequence.
 */
#define fy_local_sequence_create_value(_count, _items) \
	({ \
		size_t __count = (_count); \
		__count ? fy_sequence_alloca((_count), (_items)) : fy_seq_empty_value; \
	})

/**
 * fy_local_sequence_create() - Create a fy_generic sequence from a count and item array.
 *
 * @_count: Number of elements.
 * @_items: Array of fy_generic values to copy.
 *
 * Returns:
 * fy_generic sequence value.
 */
#define fy_local_sequence_create(_count, _items) \
	((fy_generic) { .v = fy_local_sequence_create_value((_count), (_items)) })

/*
 * FY_CPP_SHORT_NAMES / long-name helper macros
 *
 * Two implementations of the variadic generic-item helpers:
 *  - Short names (FY_CPP_SHORT_NAMES defined): use abbreviated internal names
 *    (_FLI1, _FLIL, …) to work around compiler limits on macro argument counts.
 *  - Long names (default): readable _FY_CPP_* prefix.
 *
 * Public API exposed in both cases:
 *   FY_CPP_VA_GITEMS(_count, ...)    - build a (fy_generic [_count]){…} literal
 *   FY_CPP_VA_GBITEMS(_count, _gb, ...) - same but through a builder
 */
#ifdef FY_CPP_SHORT_NAMES

/* Short-name implementation */
#define _FLI1(a) fy_to_generic(a)
#define _FLIL(a) , _FLI1(a)
#define _FLILS(...) _FM(_FLIL, __VA_ARGS__)

#define _FVLIS(...)          \
    _FLI1(_F1F(__VA_ARGS__)) \
    _FLILS(_FRF(__VA_ARGS__))

#define _FVLISF(_c, ...) \
	((fy_generic [(_c)]) { __VA_OPT__(_FVLIS(__VA_ARGS__)) })

#define _FGBI1(_g, a) fy_gb_to_generic(_g, a)
#define _FGBIL(_g, a) , _FGBI1(_g, a)
#define _FGBILS(_g, ...) _FM2(_g, _FGBIL, __VA_ARGS__)

#define _FVGBIS(_g, ...) \
	_FGBI1(_g, _F1F(__VA_ARGS__)) \
	_FGBILS(_g, _FRF(__VA_ARGS__))

#define _FVGBISF(_c, _g, ...) \
	((fy_generic [(_c)]) { _FVGBIS((_g), __VA_ARGS__) })

/* Public API */
#define _FY_CPP_GITEM_ONE		_FLI1
#define _FY_CPP_GITEM_LATER_ARG		_FLIL
#define _FY_CPP_GITEM_LIST		_FLILS
#define _FY_CPP_VA_GITEMS		_FVLIS
#define FY_CPP_VA_GITEMS		_FVLISF

#define _FY_CPP_GBITEM_ONE		_FGBI1
#define _FY_CPP_GBITEM_LATER_ARG	_FGBIL
#define _FY_CPP_GBITEM_LIST		_FGBILS
#define _FY_CPP_VA_GBITEMS		_FVGBIS
#define FY_CPP_VA_GBITEMS		_FVGBISF

#else /* !FY_CPP_SHORT_NAMES */

/* Original long-name implementation */
#define _FY_CPP_GITEM_ONE(arg) fy_to_generic(arg)
#define _FY_CPP_GITEM_LATER_ARG(arg) , _FY_CPP_GITEM_ONE(arg)
#define _FY_CPP_GITEM_LIST(...) FY_CPP_MAP(_FY_CPP_GITEM_LATER_ARG, __VA_ARGS__)

#define _FY_CPP_VA_GITEMS(...)          \
    _FY_CPP_GITEM_ONE(FY_CPP_FIRST(__VA_ARGS__)) \
    _FY_CPP_GITEM_LIST(FY_CPP_REST(__VA_ARGS__))

/**
 * FY_CPP_VA_GITEMS() - Build a compound-literal fy_generic array from variadic arguments.
 *
 * Each argument is passed through fy_to_generic() for automatic type conversion.
 *
 * @_count: Number of elements (must equal the number of variadic arguments).
 * @...:    Values to convert.
 *
 * Returns:
 * A (fy_generic [_count]){…} compound literal.
 */
#define FY_CPP_VA_GITEMS(_count, ...) \
	((fy_generic [(_count)]) { __VA_OPT__(_FY_CPP_VA_GITEMS(__VA_ARGS__)) })

#define _FY_CPP_GBITEM_ONE(_gb, arg) fy_gb_to_generic(_gb, arg)
#define _FY_CPP_GBITEM_LATER_ARG(_gb, arg) , _FY_CPP_GBITEM_ONE(_gb, arg)
#define _FY_CPP_GBITEM_LIST(_gb, ...) FY_CPP_MAP2(_gb, _FY_CPP_GBITEM_LATER_ARG, __VA_ARGS__)

#define _FY_CPP_VA_GBITEMS(_gb, ...)          \
	_FY_CPP_GBITEM_ONE(_gb, FY_CPP_FIRST(__VA_ARGS__)) \
	_FY_CPP_GBITEM_LIST(_gb, FY_CPP_REST(__VA_ARGS__))

/**
 * FY_CPP_VA_GBITEMS() - Build a compound-literal fy_generic array from variadic arguments via a builder.
 *
 * Like FY_CPP_VA_GITEMS() but each item is internalized into @_gb via
 * fy_gb_to_generic(), ensuring persistent heap storage.
 *
 * @_count: Number of elements.
 * @_gb:    Builder to internalize values into.
 * @...:    Values to convert.
 *
 * Returns:
 * A (fy_generic [_count]){…} compound literal.
 */
#define FY_CPP_VA_GBITEMS(_count, _gb, ...) \
	((fy_generic [(_count)]) { _FY_CPP_VA_GBITEMS((_gb), __VA_ARGS__) })

#endif /* FY_CPP_SHORT_NAMES */


/**
 * fy_local_sequence_value() - Build a sequence fy_generic_value from variadic arguments.
 *
 * Each argument is converted via fy_to_generic(); the resulting items are
 * stored in a stack-allocated fy_generic_sequence.
 *
 * @...: Values to include in the sequence.
 *
 * Returns:
 * fy_generic_value encoding the sequence (stack lifetime).
 */
#define fy_local_sequence_value(...) \
	fy_local_sequence_create_value( \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS( \
				FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__))

/**
 * fy_local_sequence() - Build a fy_generic sequence from variadic arguments.
 *
 * @...: Values to include in the sequence.
 *
 * Returns:
 * fy_generic sequence value (stack lifetime).
 */
#define fy_local_sequence(...) \
	((fy_generic) { .v = fy_local_sequence_value(__VA_ARGS__) })

/**
 * fy_mapping_alloca() - Allocate a mapping on the stack and copy key-value pairs into it.
 *
 * @_count: Number of key-value pairs.
 * @_pairs: Pointer to an array of 2 x @_count fy_generic values (interleaved key, value).
 *
 * Returns:
 * fy_generic_value encoding the stack-allocated mapping (stack lifetime).
 */
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

/**
 * fy_local_mapping_create_value() - Create a fy_generic_value for a mapping, handling the empty case.
 *
 * @_count: Number of key-value pairs.
 * @_pairs: Array of interleaved key/value pairs.
 *
 * Returns:
 * fy_generic_value for the mapping.
 */
#define fy_local_mapping_create_value(_count, _pairs) \
	({ \
		size_t __count = (_count); \
		__count ? fy_mapping_alloca((_count), (_pairs)) : fy_map_empty_value; \
	})

/**
 * fy_local_mapping_create() - Create a fy_generic mapping from a count and pairs array.
 *
 * @_count: Number of key-value pairs.
 * @_items: Array of interleaved fy_generic key/value pairs.
 *
 * Returns:
 * fy_generic mapping value.
 */
#define fy_local_mapping_create(_count, _items) \
	((fy_generic) { .v = fy_local_mapping_create_value((_count), (_items)) })

/**
 * fy_local_mapping_value() - Build a mapping fy_generic_value from variadic key-value arguments.
 *
 * Arguments must be alternating key, value pairs.  Each is converted via
 * fy_to_generic().
 *
 * @...: Alternating key/value arguments (even count required).
 *
 * Returns:
 * fy_generic_value encoding the mapping (stack lifetime).
 */
#define fy_local_mapping_value(...) \
	fy_local_mapping_create_value( \
		FY_CPP_VA_COUNT(__VA_ARGS__) / 2, \
		FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__))

/**
 * fy_local_mapping() - Build a fy_generic mapping from variadic key-value arguments.
 *
 * @...: Alternating key/value arguments.
 *
 * Returns:
 * fy_generic mapping value (stack lifetime).
 */
#define fy_local_mapping(...) \
	((fy_generic) { .v = fy_local_mapping_value(__VA_ARGS__) })

/**
 * fy_indirect_alloca() - Build an indirect wrapper value on the stack.
 *
 * Allocates a compact array on the stack that holds a flags word followed by
 * whichever of value, anchor, and tag are not fy_invalid_value.
 *
 * @_v:      Wrapped value (fy_generic_value), or fy_invalid_value to omit.
 * @_anchor: Anchor string generic value (fy_string), or fy_invalid_value to omit.
 * @_tag:    Tag string generic value (fy_string), or fy_invalid_value to omit.
 *
 * Returns:
 * fy_generic_value encoding the indirect wrapper (stack lifetime).
 */
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

/**
 * fy_indirect() - Build an indirect-wrapper fy_generic from fy_generic arguments.
 *
 * @_v:      The wrapped value as fy_generic, or fy_invalid for none.
 * @_anchor: Anchor string as fy_generic, or fy_invalid for none.
 * @_tag:    Tag string as fy_generic, or fy_invalid for none.
 *
 * Returns:
 * fy_generic indirect wrapper (stack lifetime).
 */
#define fy_indirect(_v, _anchor, _tag)	\
	((fy_generic){ .v = fy_indirect_alloca((_v).v, (_anchor).v, (_tag).v) })

/**
 * fy_generic_cast_generic_default() - Return @v if it is valid, otherwise return @default_value.
 *
 * @v:             Generic value to check.
 * @default_value: Fallback fy_generic.
 *
 * Returns:
 * @v when valid, otherwise @default_value.
 */
static inline fy_generic fy_generic_cast_generic_default(fy_generic v, fy_generic default_value)
{
	if (fy_generic_is_valid(v))
		return v;
	return default_value;
}

/**
 * fy_genericp_cast_generic_default() - Dereference a generic pointer and return its value, or a default.
 *
 * @vp:            Pointer to a fy_generic, or NULL.
 * @default_value: Fallback when @vp is NULL or the value is invalid.
 *
 * Returns:
 * \*@vp when valid, otherwise @default_value.
 */
static inline fy_generic fy_genericp_cast_generic_default(const fy_generic *vp, fy_generic default_value)
{
	if (!vp)
		return default_value;
	return fy_generic_cast_generic_default(*vp, default_value);
}

/**
 * fy_generic_cast_sequence_handle_default() - Extract a sequence handle from a generic, or return a default.
 *
 * @v:             Generic value.
 * @default_value: Fallback handle when @v does not hold a sequence.
 *
 * Returns:
 * Sequence handle on success, or @default_value.
 */
static inline fy_generic_sequence_handle fy_generic_cast_sequence_handle_default(fy_generic v, fy_generic_sequence_handle default_value)
{
	const fy_generic_sequence_handle seqh = fy_generic_sequence_to_handle(v);
	return seqh ? seqh : default_value;
}

/**
 * fy_genericp_cast_sequence_handle_default() - Extract a sequence handle via a pointer, or return a default.
 *
 * @vp:            Pointer to a fy_generic, or NULL.
 * @default_value: Fallback when @vp is NULL or not a sequence.
 *
 * Returns:
 * Sequence handle, or @default_value.
 */
static inline fy_generic_sequence_handle fy_genericp_cast_sequence_handle_default(const fy_generic *vp, fy_generic_sequence_handle default_value)
{
	if (!vp)
		return default_value;
	return fy_generic_cast_sequence_handle_default(*vp, default_value);
}

/**
 * fy_generic_cast_mapping_handle_default() - Extract a mapping handle from a generic, or return a default.
 *
 * @v:             Generic value.
 * @default_value: Fallback when @v does not hold a mapping.
 *
 * Returns:
 * Mapping handle, or @default_value.
 */
static inline fy_generic_mapping_handle fy_generic_cast_mapping_handle_default(fy_generic v, fy_generic_mapping_handle default_value)
{
	const fy_generic_mapping_handle maph = fy_generic_mapping_to_handle(v);
	return maph ? maph : default_value;
}

/**
 * fy_genericp_cast_mapping_handle_default() - Extract a mapping handle via a pointer, or return a default.
 *
 * @vp:            Pointer to a fy_generic, or NULL.
 * @default_value: Fallback when @vp is NULL or not a mapping.
 *
 * Returns:
 * Mapping handle, or @default_value.
 */
static inline fy_generic_mapping_handle fy_genericp_cast_mapping_handle_default(const fy_generic *vp, fy_generic_mapping_handle default_value)
{
	if (!vp)
		return default_value;
	return fy_generic_cast_mapping_handle_default(*vp, default_value);
}

/**
 * fy_generic_cast_const_char_ptr_default() - Extract a const char* pointer from a string generic.
 *
 * Works for out-of-place strings (returns a pointer into the stored buffer).
 * Returns NULL for inplace strings (the caller must use the alloca path instead).
 *
 * @v:             Generic string value.
 * @default_value: Fallback when @v is not a string.
 *
 * Returns:
 * const char* into the string data, NULL for inplace strings, or @default_value.
 */
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

/* fy_generic_cast_char_ptr_default() - Non-const variant of fy_generic_cast_const_char_ptr_default() */
static inline char *fy_generic_cast_char_ptr_default(fy_generic v, char *default_value)
{
	return (char *)fy_generic_cast_const_char_ptr_default(v, default_value);
}

/**
 * fy_generic_cast_sized_string_default() - Extract a sized string from a generic string value.
 *
 * Out-of-place strings return a pointer+size directly into the stored buffer.
 * Inplace strings return an all-zero szstr (data=NULL, size=0) to indicate the
 * caller must use the alloca path to copy the bytes out of the generic word.
 *
 * @v:             Generic string value.
 * @default_value: Fallback when @v is not a string.
 *
 * Returns:
 * fy_generic_sized_string; data may be NULL for inplace strings.
 */
/* special... handling for in place strings */
static inline fy_generic_sized_string fy_generic_cast_sized_string_default(fy_generic v, const fy_generic_sized_string default_value)
{
	fy_generic_sized_string szstr;
	const fy_generic *vp;

	if (fy_generic_is_direct_string(v)) {
		/* out of place: heap pointer stored directly in v, safe to return */
		if ((v.v & FY_INPLACE_TYPE_MASK) != FY_STRING_INPLACE_V) {
			szstr.data = (const char *)fy_decode_size_nocheck(fy_generic_resolve_ptr(v), &szstr.size);
			return szstr;
		}
		/* inplace: bytes are stored inside v (a local copy) - return NULL
		 * to signal that the alloca path should be used by the caller */
		memset(&szstr, 0, sizeof(szstr));
		return szstr;
	}

	/* indirect: dereference once to get the actual value on the heap */
	vp = fy_genericp_indirect_get_valuep(&v);
	if (!vp || !fy_generic_is_direct_string(*vp))
		return default_value;

	szstr.data = fy_genericp_get_string_size_no_check(vp, &szstr.size);
	return szstr;
}

/**
 * fy_genericp_cast_const_char_ptr_default() - Extract a const char* via a pointer, or return a default.
 *
 * @vp:            Pointer to a fy_generic, or NULL.
 * @default_value: Fallback when @vp is NULL or not a string.
 *
 * Returns:
 * Pointer to string data, or @default_value.
 */
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

/* fy_genericp_cast_char_ptr_default() - Non-const variant of fy_genericp_cast_const_char_ptr_default() */
static inline char *fy_genericp_cast_char_ptr_default(const fy_generic *vp, char *default_value)
{
	return (char *)fy_genericp_cast_const_char_ptr_default(vp, default_value);
}

/**
 * fy_genericp_cast_sized_string_default() - Extract a sized string via a pointer, or return a default.
 *
 * @vp:            Pointer to a fy_generic, or NULL.
 * @default_value: Fallback when @vp is NULL or not a string.
 *
 * Returns:
 * fy_generic_sized_string with pointer into string data.
 */
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

/**
 * fy_generic_cast_decorated_int_default() - Extract a decorated int from a generic, or return a default.
 *
 * Handles both inplace (always signed) and out-of-place decorated int encodings.
 * Indirect wrappers are resolved automatically.
 *
 * @v:             Generic value.
 * @default_value: Fallback when @v does not hold an integer.
 *
 * Returns:
 * fy_generic_decorated_int on success, or @default_value.
 */
static inline fy_generic_decorated_int fy_generic_cast_decorated_int_default(fy_generic v, const fy_generic_decorated_int default_value)
{
	const fy_generic_decorated_int *p;
	fy_generic_decorated_int dv;

	if (!fy_generic_is_int_type(v))
		return default_value;

	v = fy_generic_indirect_get_value(v);
	// inplace? always signed */
	if ((v.v & FY_INPLACE_TYPE_MASK) == FY_INT_INPLACE_V) {
		dv.sv = (fy_generic_value_signed)((v.v >> FY_INPLACE_TYPE_SHIFT) <<
			       (FYGT_GENERIC_BITS - FYGT_INT_INPLACE_BITS)) >>
				       (FYGT_GENERIC_BITS - FYGT_INT_INPLACE_BITS);
		dv.flags = 0;	/* in place is always unsigned */
		return dv;
	}

	p = fy_generic_resolve_ptr(v);
	if (!p)
		return default_value;

	return *p;
}

/* fy_genericp_cast_decorated_int_default() - Pointer variant of fy_generic_cast_decorated_int_default() */
static inline fy_generic_decorated_int fy_genericp_cast_decorated_int_default(const fy_generic *vp, const fy_generic_decorated_int default_value)
{
	if (!vp)
		return default_value;
	return fy_generic_cast_decorated_int_default(*vp, default_value);
}

/**
 * fy_generic_cast_const_char_ptr_default_alloca() - Return alloca size needed for inplace string cast.
 *
 * Returns sizeof(fy_generic) bytes when the value is an inplace string (so
 * the caller can copy the bytes to the stack), 0 for all other types.
 *
 * @v: Generic value to examine.
 *
 * Returns:
 * sizeof(fy_generic) for inplace strings, 0 otherwise.
 */
static inline size_t fy_generic_cast_const_char_ptr_default_alloca(fy_generic v)
{
	/* only do the alloca dance for in place strings */
	return ((v.v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V) ? sizeof(fy_generic) : 0;
}

/* fy_generic_cast_sized_string_default_alloca() - Same as fy_generic_cast_const_char_ptr_default_alloca() */
static inline size_t fy_generic_cast_sized_string_default_alloca(fy_generic v)
{
	return fy_generic_cast_const_char_ptr_default_alloca(v);
}

/* fy_generic_cast_decorated_int_default_alloca() - Always 0; decorated ints never need alloca */
static inline size_t fy_generic_cast_decorated_int_default_alloca(fy_generic v)
{
	return 0;
}

/* fy_generic_cast_default_should_alloca_never() - Always 0; used as the default in _Generic dispatch */
/* never allocas for anything else */
static inline size_t fy_generic_cast_default_should_alloca_never(fy_generic v)
{
	return 0;
}

/**
 * fy_generic_cast_const_char_ptr_default_final() - Copy an inplace string to a stack buffer.
 *
 * Called after alloca when an inplace string was detected.  Copies the bytes
 * out of the generic word into @p and sets \*@store_value to point to @p.
 *
 * @v:           Inplace string generic value.
 * @p:           Stack buffer of at least @size bytes.
 * @size:        Buffer size in bytes.
 * @default_value: Unused; present for _Generic dispatch uniformity.
 * @store_value: Receives a pointer to the NUL-terminated copy in @p.
 */
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

/**
 * fy_generic_cast_sized_string_default_final() - Copy an inplace string to a stack buffer as a sized_string.
 *
 * @v:           Inplace string generic value.
 * @p:           Stack buffer.
 * @size:        Buffer size in bytes.
 * @default_value: Unused.
 * @store_value: Receives data/size pointing into @p.
 */
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

/* fy_generic_cast_decorated_int_default_final() - No-op; decorated ints never need a final copy step */
static inline void fy_generic_cast_decorated_int_default_final(fy_generic v,
		void *p, size_t size,
		fy_generic_decorated_int default_value, fy_generic_decorated_int *store_value)
{
	return;
}

/* fy_generic_cast_char_ptr_default_final() - Non-const variant of fy_generic_cast_const_char_ptr_default_final() */
static inline void fy_generic_cast_char_ptr_default_final(fy_generic v,
		void *p, size_t size,
		char *default_value, char **store_value)
{
	return fy_generic_cast_const_char_ptr_default_final(v, p, size, default_value,
			(const char **)store_value);
}

/* fy_generic_cast_default_final_never() - No-op final step used as default in _Generic dispatch */
static inline void fy_generic_cast_default_final_never(fy_generic v,
		void *p, size_t size, ...)
{
	return;	// do nothing
}

/* fy_generic_get_type_default_Generic_dispatch - _Generic table mapping C types to their zero/NULL defaults */
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

/* fy_generic_cast_default_Generic_dispatch - _Generic table for value-based typed cast with default */
#define fy_generic_cast_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_generic_cast, default), \
	char *: fy_generic_cast_char_ptr_default, \
	const char *: fy_generic_cast_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_generic_cast_sequence_handle_default, \
	fy_generic_mapping_handle: fy_generic_cast_mapping_handle_default, \
	fy_generic_sized_string: fy_generic_cast_sized_string_default, \
	fy_generic_decorated_int: fy_generic_cast_decorated_int_default, \
	fy_generic: fy_generic_cast_generic_default

/* fy_generic_cast_default_alloca_Generic_dispatch - _Generic table that returns alloca sizes for inplace string types */
#define fy_generic_cast_default_alloca_Generic_dispatch \
	char *: fy_generic_cast_const_char_ptr_default_alloca, \
	const char *: fy_generic_cast_const_char_ptr_default_alloca, \
	fy_generic_sized_string: fy_generic_cast_sized_string_default_alloca, \
	default: fy_generic_cast_default_should_alloca_never

/* fy_generic_cast_default_final_Generic_dispatch - _Generic table for the alloca final-copy step */
#define fy_generic_cast_default_final_Generic_dispatch \
	char *: fy_generic_cast_char_ptr_default_final, \
	const char *: fy_generic_cast_const_char_ptr_default_final, \
	fy_generic_sized_string: fy_generic_cast_sized_string_default_final, \
	default: fy_generic_cast_default_final_never

/**
 * fy_generic_cast_default() - Cast a generic value to a C type using _Generic dispatch.
 *
 * Converts @_v to the type inferred from @_dv.  Handles inplace strings by
 * allocating a stack buffer and copying the bytes there.  Indirect wrappers
 * are automatically resolved.
 *
 * @_v:  Source value (any type; converted via fy_to_generic()).
 * @_dv: Default value whose C type determines the target type.
 *
 * Returns:
 * The converted value of the same type as @_dv, or @_dv on failure.
 */
#define fy_generic_cast_default(_v, _dv) \
	({ \
		fy_generic __v = fy_to_generic(_v); \
		fy_generic_typeof(_dv) __dv = (_dv); \
		fy_generic_typeof(_dv) __ret; \
		size_t __size; \
		void *__p; \
		\
		if (fy_generic_is_indirect(__v)) \
			__v = fy_generic_indirect_get_value(__v); \
		__ret = _Generic(__dv, fy_generic_cast_default_Generic_dispatch)(__v, __dv); \
		__size = _Generic(__dv, fy_generic_cast_default_alloca_Generic_dispatch)(__v); \
		if (__size) { \
			__p = fy_alloca_align(__size, FY_GENERIC_CONTAINER_ALIGN); \
			_Generic(__dv, fy_generic_cast_default_final_Generic_dispatch) \
				(__v, __p, __size, __dv, &__ret); \
		} \
		__ret; \
	})

/**
 * fy_generic_get_type_default() - Produce the zero/NULL default for a C type.
 *
 * Uses _Generic dispatch over a local variable of @_type to look up the
 * appropriate zero value from fy_generic_get_type_default_Generic_dispatch.
 *
 * @_type: A C type name.
 *
 * Returns:
 * The default value for @_type.
 */
#define fy_generic_get_type_default(_type) \
	({ \
		_type __tmp = _Generic(__tmp, fy_generic_get_type_default_Generic_dispatch); \
		__tmp; \
	})

/**
 * fy_generic_cast_typed() - Cast a generic value to a specific C type.
 *
 * Convenience wrapper: derives the default from the type and delegates to
 * fy_generic_cast_default().
 *
 * @_v:    Source value (any type).
 * @_type: Target C type name.
 *
 * Returns:
 * Converted value of type @_type.
 */
/* when we have a pointer we can return to inplace strings */
#define fy_generic_cast_typed(_v, _type) \
	(fy_generic_cast_default((_v), fy_generic_get_type_default(_type)))

/* fy_genericp_cast_default_Generic_dispatch - _Generic table for pointer-based typed cast with default */
#define fy_genericp_cast_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_genericp_cast, default), \
	char *: fy_genericp_cast_char_ptr_default, \
	const char *: fy_genericp_cast_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_genericp_cast_sequence_handle_default, \
	fy_generic_mapping_handle: fy_genericp_cast_mapping_handle_default, \
	fy_generic_sized_string: fy_genericp_cast_sized_string_default, \
	fy_generic_decorated_int: fy_genericp_cast_decorated_int_default, \
	fy_generic: fy_genericp_cast_generic_default

/**
 * fy_genericp_cast_default() - Cast a generic pointer's value to a C type using _Generic dispatch.
 *
 * @_vp: Pointer to a fy_generic, or NULL.
 * @_dv: Default value whose type determines the target.
 *
 * Returns:
 * Converted value, or @_dv when @_vp is NULL or type mismatch.
 */
#define fy_genericp_cast_default(_vp, _dv) \
	(_Generic((_dv), fy_genericp_cast_default_Generic_dispatch)((_vp), (_dv)))

/**
 * fy_genericp_cast_typed() - Cast a generic pointer's value to a specific C type.
 *
 * @_vp:   Pointer to a fy_generic, or NULL.
 * @_type: Target C type name.
 *
 * Returns:
 * Converted value of type @_type.
 */
#define fy_genericp_cast_typed(_vp, _type) \
	(fy_genericp_cast_default((_vp), fy_generic_get_type_default(_type)))

/**
 * fy_genericp_get_generic_sequence_handle_default() - Extract a sequence handle via a pointer, or return a default.
 *
 * @vp:            Pointer to a fy_generic, or NULL.
 * @default_value: Fallback when @vp is NULL or not a sequence.
 *
 * Returns:
 * Sequence handle, or @default_value.
 */
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

/**
 * fy_genericp_get_generic_mapping_handle_default() - Extract a mapping handle via a pointer, or return a default.
 *
 * @vp:            Pointer to a fy_generic, or NULL.
 * @default_value: Fallback when @vp is NULL or not a mapping.
 *
 * Returns:
 * Mapping handle, or @default_value.
 */
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

/* we are making things very hard for the compiler; sometimes he gets lost */
FY_DIAG_PUSH
FY_DIAG_IGNORE_ARRAY_BOUNDS

/**
 * fy_genericp_get_generic_default() - Dereference a generic pointer, returning a default for NULL.
 *
 * @vp:            Pointer to a fy_generic, or NULL.
 * @default_value: Value returned when @vp is NULL.
 *
 * Returns:
 * \*@vp when non-NULL, otherwise @default_value.
 */
static inline FY_ALWAYS_INLINE fy_generic
fy_genericp_get_generic_default(const fy_generic *vp, fy_generic default_value)
{
	return vp ? *vp : default_value;
}

FY_DIAG_POP

/**
 * fy_genericp_get_string_genericp() - Return @vp itself if it points to a direct string, or NULL.
 *
 * @vp: Pointer to a fy_generic, or NULL.
 *
 * Returns:
 * @vp when \*@vp is a direct string, NULL otherwise.
 */
static inline FY_ALWAYS_INLINE const fy_generic *
fy_genericp_get_string_genericp(const fy_generic *vp)
{
	return vp && fy_generic_is_direct_string(*vp) ? vp : NULL;
}

/**
 * fy_genericp_get_szstr_default() - Extract a sized string via a pointer, resolving indirects.
 *
 * @vp:            Pointer to a fy_generic, or NULL.
 * @default_value: Fallback when @vp is NULL or not a string.
 *
 * Returns:
 * fy_generic_sized_string pointing into the stored data, or @default_value.
 */
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

/**
 * fy_generic_cast_default_coerse() - Convert a value to fy_generic first, then cast with a default.
 *
 * Like fy_generic_cast_default() but accepts any type for @_v by converting
 * it through fy_to_generic() before casting.
 *
 * @_v:  Value to convert (any supported type).
 * @_dv: Default value (determines output type).
 *
 * Returns:
 * Converted value of the type of @_dv.
 */
#define fy_generic_cast_default_coerse(_v, _dv) \
	({ \
		fy_generic __vv = fy_to_generic(_v); \
		fy_generic_cast_default(__vv, (_dv)); \
	})

/*
 * Sequence typed-get helpers.
 *
 * Each function retrieves the element at @idx from a sequence and casts it to
 * a specific type.  "seqp" variants accept a resolved fy_generic_sequence pointer;
 * "seq" variants accept a fy_generic and resolve internally.
 */

/* fy_generic_sequencep_get_generic_sequence_handle_default() - Get element at @idx as a sequence handle, or @default_value */
static inline FY_ALWAYS_INLINE fy_generic_sequence_handle
fy_generic_sequencep_get_generic_sequence_handle_default(const fy_generic_sequence *seqp, const size_t idx,
		fy_generic_sequence_handle default_value)
{
	return fy_genericp_get_generic_sequence_handle_default(
			fy_generic_sequencep_get_itemp(seqp, idx),
			default_value);
}

/* fy_generic_sequence_get_generic_sequence_handle_default() - Get element at @idx as a sequence handle, or @default_value */
static inline FY_ALWAYS_INLINE fy_generic_sequence_handle
fy_generic_sequence_get_generic_sequence_handle_default(fy_generic seq, const size_t idx,
		fy_generic_sequence_handle default_value)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_generic_sequence_handle_default(seqp, idx, default_value);
}

/* fy_generic_sequencep_get_generic_mapping_handle_default() - Get element at @idx as a mapping handle, or @default_value */
static inline FY_ALWAYS_INLINE fy_generic_mapping_handle
fy_generic_sequencep_get_generic_mapping_handle_default(const fy_generic_sequence *seqp, const size_t idx,
		fy_generic_mapping_handle default_value)
{
	return fy_genericp_get_generic_mapping_handle_default(
			fy_generic_sequencep_get_itemp(seqp, idx),
			default_value);
}

/* fy_generic_sequence_get_generic_mapping_handle_default() - Get element at @idx as a mapping handle, or @default_value */
static inline FY_ALWAYS_INLINE fy_generic_mapping_handle
fy_generic_sequence_get_generic_mapping_handle_default(fy_generic seq, const size_t idx,
		fy_generic_mapping_handle default_value)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_generic_mapping_handle_default(seqp, idx, default_value);
}

/* fy_generic_sequencep_get_generic_default() - Get element at @idx as fy_generic, or @default_value */
static inline FY_ALWAYS_INLINE fy_generic
fy_generic_sequencep_get_generic_default(const fy_generic_sequence *seqp, const size_t idx, fy_generic default_value)
{
	return fy_genericp_get_generic_default(
			fy_generic_sequencep_get_itemp(seqp, idx),
			default_value);
}

/* fy_generic_sequence_get_generic_default() - Get element at @idx as fy_generic, or @default_value */
static inline FY_ALWAYS_INLINE fy_generic
fy_generic_sequence_get_generic_default(fy_generic seq, const size_t idx, fy_generic default_value)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_generic_default(seqp, idx, default_value);
}

/* fy_generic_sequencep_get_const_char_ptr_default() - Get element at @idx as const char*, or @default_value */
static inline FY_ALWAYS_INLINE const char *
fy_generic_sequencep_get_const_char_ptr_default(const fy_generic_sequence *seqp, const size_t idx, const char *default_value)
{
	return fy_genericp_get_const_char_ptr_default(
			fy_generic_sequencep_get_itemp(seqp, idx),
			default_value);
}

/* fy_generic_sequence_get_const_char_ptr_default() - Get element at @idx as const char*, or @default_value */
static inline FY_ALWAYS_INLINE const char *
fy_generic_sequence_get_const_char_ptr_default(fy_generic seq, const size_t idx, const char *default_value)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_const_char_ptr_default(seqp, idx, default_value);
}

/* fy_generic_sequencep_get_char_ptr_default() - Non-const variant of fy_generic_sequencep_get_const_char_ptr_default() */
static inline FY_ALWAYS_INLINE char *
fy_generic_sequencep_get_char_ptr_default(const fy_generic_sequence *seqp, const size_t idx, const char *default_value)
{
	return (char *)fy_generic_sequencep_get_const_char_ptr_default(seqp, idx, default_value);
}

/* fy_generic_sequence_get_char_ptr_default() - Non-const variant of fy_generic_sequence_get_const_char_ptr_default() */
static inline FY_ALWAYS_INLINE char *
fy_generic_sequence_get_char_ptr_default(fy_generic seq, const size_t idx, char *default_value)
{
	return (char *)fy_generic_sequence_get_const_char_ptr_default(seq, idx, default_value);
}

/* fy_generic_sequencep_get_szstr_default() - Get element at @idx as a sized string, or @default_value */
static inline FY_ALWAYS_INLINE fy_generic_sized_string
fy_generic_sequencep_get_szstr_default(const fy_generic_sequence *seqp, const size_t idx,
		fy_generic_sized_string default_value)
{
	return fy_genericp_get_szstr_default(
			fy_generic_sequencep_get_itemp(seqp, idx),
			default_value);
}

/* fy_generic_sequence_get_szstr_default() - Get element at @idx as a sized string, or @default_value */
static inline FY_ALWAYS_INLINE fy_generic_sized_string
fy_generic_sequence_get_szstr_default(fy_generic seq, const size_t idx,
		fy_generic_sized_string default_value)
{
	const fy_generic_sequence *seqp = fy_generic_sequence_resolve(seq);
	return fy_generic_sequencep_get_szstr_default(seqp, idx, default_value);
}

/* fy_generic_sequence_get_map_pair_default() - Stub: map pairs not stored in sequences; always returns @default_value */
static inline FY_ALWAYS_INLINE fy_generic_map_pair
fy_generic_sequence_get_map_pair_default(fy_generic seq, const size_t idx, fy_generic_map_pair default_value)
{
	return default_value;
}

/* fy_generic_sequence_get_map_pairp_default() - Stub: always returns @default_value */
static inline FY_ALWAYS_INLINE fy_generic_map_pair *
fy_generic_sequence_get_map_pairp_default(fy_generic seq, const size_t idx, fy_generic_map_pair *default_value)
{
	return default_value;
}

/* fy_generic_sequence_get_const_map_pairp_default() - Stub: always returns @default_value */
static inline FY_ALWAYS_INLINE const fy_generic_map_pair *
fy_generic_sequence_get_const_map_pairp_default(fy_generic seq, const size_t idx, const fy_generic_map_pair *default_value)
{
	return default_value;
}

/* fy_generic_sequencep_get_map_pair_default() - Stub: always returns @default_value */
static inline FY_ALWAYS_INLINE fy_generic_map_pair
fy_generic_sequencep_get_map_pair_default(const fy_generic_sequence *seqp, const size_t idx, fy_generic_map_pair default_value)
{
	return default_value;
}

/* fy_generic_sequencep_get_map_pairp_default() - Stub: always returns @default_value */
static inline FY_ALWAYS_INLINE fy_generic_map_pair *
fy_generic_sequencep_get_map_pairp_default(const fy_generic_sequence *seqp, const size_t idx, fy_generic_map_pair *default_value)
{
	return default_value;
}

/* fy_generic_sequencep_get_const_map_pairp_default() - Stub: always returns @default_value */
static inline FY_ALWAYS_INLINE const fy_generic_map_pair *
fy_generic_sequencep_get_const_map_pairp_default(const fy_generic_sequence *seqp, const size_t idx, const fy_generic_map_pair *default_value)
{
	return default_value;
}

/* fy_generic_sequence_get_default_Generic_dispatch - _Generic table for typed element access on a sequence */
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

/**
 * fy_generic_sequence_get_default() - Retrieve a typed element from a sequence by index.
 *
 * @_seq: fy_generic sequence value.
 * @_idx: Element index.
 * @_dv:  Default value (type determines the output type).
 *
 * Returns:
 * Element cast to the type of @_dv, or @_dv if out of range or type mismatch.
 */
#define fy_generic_sequence_get_default(_seq, _idx, _dv) \
	(_Generic((_dv), fy_generic_sequence_get_default_Generic_dispatch)((_seq), (_idx), (_dv)))

/**
 * fy_generic_sequence_get_typed() - Retrieve a typed element from a sequence by index.
 *
 * Like fy_generic_sequence_get_default() but derives the default from @_type.
 *
 * @_seq:  fy_generic sequence value.
 * @_idx:  Element index.
 * @_type: C type name of the desired output type.
 *
 * Returns:
 * Element of type @_type, or the zero/NULL default.
 */
#define fy_generic_sequence_get_typed(_seq, _idx, _type) \
	(fy_generic_sequence_get_default((_seq), (_idx), fy_generic_get_type_default(_type)))

/**
 * fy_generic_sequence_get_default_coerse() - Retrieve an element from a sequence using a generic index.
 *
 * @_seq:  Sequence.
 * @_idxv: Index as any type convertible to fy_generic (e.g. an integer).
 * @_dv:   Default value.
 *
 * Returns:
 * Element cast to the type of @_dv, or @_dv.
 */
#define fy_generic_sequence_get_default_coerse(_seq, _idxv, _dv) \
	({ \
		__typeof__((_dv) + 0) __dv = (_dv); \
		fy_generic __idxv = fy_to_generic(_idxv); \
		const size_t __idxi = (size_t)fy_generic_get_unsigned_long_long_default(__idxv, (unsigned long long)-1); \
		fy_generic_sequence_get_default((_seq), __idxi, (_dv)); \
	})

/* fy_generic_sequence_get_coerse() - Like fy_generic_sequence_get_default_coerse() but derives default from @_type */
#define fy_generic_sequence_get_coerse(_seq, _idxv, _type) \
	(fy_generic_sequence_get_default_coerse((_seq), (_idxv), fy_generic_get_type_default(_type)))

/* fy_generic_sequencep_get_default_Generic_dispatch - _Generic table for typed element access on a sequence pointer */
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

/**
 * fy_generic_sequencep_get_default() - Retrieve a typed element from a sequence pointer by index.
 *
 * @_seqp: Pointer to a fy_generic_sequence.
 * @_idx:  Element index.
 * @_dv:   Default value.
 *
 * Returns:
 * Element cast to the type of @_dv, or @_dv.
 */
#define fy_generic_sequencep_get_default(_seqp, _idx, _dv) \
	(_Generic((_dv), fy_generic_sequencep_get_default_Generic_dispatch)((_seqp), (_idx), (_dv)))

/* fy_generic_sequencep_get_typed() - Like fy_generic_sequencep_get_default() but derives default from @_type */
#define fy_generic_sequencep_get_typed(_seqp, _idx, _type) \
	(fy_generic_sequencep_get_default((_seqp), (_idx), fy_generic_get_type_default(_type)))

/* fy_generic_sequencep_get_default_coerse() - Like fy_generic_sequence_get_default_coerse() for a sequence pointer */
#define fy_generic_sequencep_get_default_coerse(_seqp, _idxv, _dv) \
	({ \
		__typeof__((_dv) + 0) __dv = (_dv); \
		fy_generic __idxv = fy_to_generic(_idxv); \
		size_t __idxi = (size_t)fy_generic_get_unsigned_long_long_default(__idxv, (unsigned long long)-1); \
		fy_generic_sequencep_get_default((_seqp), __idxi, (_dv)); \
	})

/* fy_generic_sequencep_get_coerse() - Like fy_generic_sequencep_get_default_coerse() but derives default from @_type */
#define fy_generic_sequencep_get_coerse(_seqp, _idxv, _type) \
	(fy_generic_sequencep_get_default_coerse((_seqp), (_idxv), fy_generic_get_type_default(_type)))

/* for a sequence get_at is the same as get */
/* fy_generic_sequence_get_at_default() - Alias for fy_generic_sequence_get_default() */
#define fy_generic_sequence_get_at_default(_seq, _idx, _dv) \
	(fy_generic_sequence_get_default((_seq), (_idx), (_dv)))

/* fy_generic_sequence_get_at_typed() - Alias for fy_generic_sequence_get_typed() */
#define fy_generic_sequence_get_at_typed(_seq, _idx, _type) \
	(fy_generic_sequence_get_typed((_seq), (_idx), (_type)))

/* fy_generic_sequencep_get_at_default() - Alias for fy_generic_sequencep_get_default() */
#define fy_generic_sequencep_get_at_default(_seqp, _idx, _dv) \
	(fy_generic_sequencep_get_default((_seqp), (_idx), (_dv)))

/* fy_generic_sequencep_get_at_typed() - Alias for fy_generic_sequencep_get_typed() */
#define fy_generic_sequencep_get_at_typed(_seqp, _idx, _type) \
	(fy_generic_sequencep_get_typed((_seqp), (_idx), (_type)))

/*
 * Mapping typed-get helpers (by key).
 *
 * Each function looks up @key in a mapping and casts the value to a specific
 * type.  "mapp" variants take a resolved fy_generic_mapping pointer;
 * "map" variants take a fy_generic and resolve internally.
 */

/* fy_generic_mappingp_get_generic_sequence_handle_default() - Look up @key and return its value as a sequence handle, or @default_value */
static inline fy_generic_sequence_handle
fy_generic_mappingp_get_generic_sequence_handle_default(const fy_generic_mapping *mapp, fy_generic key,
		fy_generic_sequence_handle default_value)
{
	return fy_genericp_get_generic_sequence_handle_default(
			fy_generic_mappingp_get_valuep(mapp, key),
			default_value);
}

/* fy_generic_mapping_get_generic_sequence_handle_default() - Look up @key and return its value as a sequence handle, or @default_value */
static inline fy_generic_sequence_handle
fy_generic_mapping_get_generic_sequence_handle_default(fy_generic map, fy_generic key,
		fy_generic_sequence_handle default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_generic_sequence_handle_default(mapp, key, default_value);
}

/* fy_generic_mappingp_get_generic_mapping_handle_default() - Look up @key and return its value as a mapping handle, or @default_value */
static inline fy_generic_mapping_handle
fy_generic_mappingp_get_generic_mapping_handle_default(const fy_generic_mapping *mapp, fy_generic key,
		fy_generic_mapping_handle default_value)
{
	return fy_genericp_get_generic_mapping_handle_default(
			fy_generic_mappingp_get_valuep(mapp, key),
			default_value);
}

/* fy_generic_mapping_get_generic_mapping_handle_default() - Look up @key and return its value as a mapping handle, or @default_value */
static inline fy_generic_mapping_handle
fy_generic_mapping_get_generic_mapping_handle_default(fy_generic map, fy_generic key,
		fy_generic_mapping_handle default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_generic_mapping_handle_default(mapp, key, default_value);
}

/* fy_generic_mappingp_get_generic_default() - Look up @key and return its value as fy_generic, or @default_value */
static inline fy_generic fy_generic_mappingp_get_generic_default(const fy_generic_mapping *mapp, fy_generic key, fy_generic default_value)
{
	return fy_genericp_get_generic_default(
			fy_generic_mappingp_get_valuep(mapp, key),
			default_value);
}

/* fy_generic_mapping_get_generic_default() - Look up @key and return its value as fy_generic, or @default_value */
static inline fy_generic fy_generic_mapping_get_generic_default(fy_generic map, fy_generic key, fy_generic default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_generic_default(mapp, key, default_value);
}

/* fy_generic_mappingp_get_const_char_ptr_default() - Look up @key and return its string value as const char*, or @default_value */
static inline const char *fy_generic_mappingp_get_const_char_ptr_default(const fy_generic_mapping *mapp, fy_generic key, const char *default_value)
{
	return fy_genericp_get_const_char_ptr_default(
			fy_generic_mappingp_get_valuep(mapp, key),
			default_value);
}

/* fy_generic_mapping_get_const_char_ptr_default() - Look up @key and return its string value as const char*, or @default_value */
static inline const char *fy_generic_mapping_get_const_char_ptr_default(fy_generic map, fy_generic key, const char *default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_const_char_ptr_default(mapp, key, default_value);
}

/* fy_generic_mappingp_get_char_ptr_default() - Non-const variant of fy_generic_mappingp_get_const_char_ptr_default() */
static inline char *fy_generic_mappingp_get_char_ptr_default(const fy_generic_mapping *mapp, fy_generic key, const char *default_value)
{
	return (char *)fy_generic_mappingp_get_const_char_ptr_default(mapp, key, default_value);
}

/* fy_generic_mapping_get_char_ptr_default() - Non-const variant of fy_generic_mapping_get_const_char_ptr_default() */
static inline char *fy_generic_mapping_get_char_ptr_default(fy_generic map, fy_generic key, char *default_value)
{
	return (char *)fy_generic_mapping_get_const_char_ptr_default(map, key, default_value);
}

/* fy_generic_mappingp_get_szstr_default() - Look up @key and return its string value as a sized string, or @default_value */
static inline fy_generic_sized_string
fy_generic_mappingp_get_szstr_default(const fy_generic_mapping *mapp, fy_generic key,
		fy_generic_sized_string default_value)
{
	return fy_genericp_get_szstr_default(
			fy_generic_mappingp_get_valuep(mapp, key),
			default_value);
}

/* fy_generic_mapping_get_szstr_default() - Look up @key and return its string value as a sized string, or @default_value */
static inline fy_generic_sized_string
fy_generic_mapping_get_szstr_default(fy_generic map, fy_generic key,
		fy_generic_sized_string default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_szstr_default(mapp, key, default_value);
}

/* fy_generic_mapping_get_default_Generic_dispatch - _Generic table for typed value lookup by key */
#define fy_generic_mapping_get_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_generic_mapping_get, default), \
	char *: fy_generic_mapping_get_char_ptr_default, \
	const char *: fy_generic_mapping_get_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_generic_mapping_get_generic_sequence_handle_default, \
	fy_generic_mapping_handle: fy_generic_mapping_get_generic_mapping_handle_default, \
	fy_generic_sized_string: fy_generic_mapping_get_szstr_default, \
	fy_generic: fy_generic_mapping_get_generic_default

/**
 * fy_generic_mapping_get_default() - Look up a key in a mapping and return the typed value.
 *
 * @_map: fy_generic mapping value.
 * @_key: Key (any type; converted via fy_to_generic()).
 * @_dv:  Default value (its C type determines the output type).
 *
 * Returns:
 * Value cast to the type of @_dv, or @_dv when not found.
 */
#define fy_generic_mapping_get_default(_map, _key, _dv) \
	(_Generic((_dv), fy_generic_mapping_get_default_Generic_dispatch)((_map), fy_to_generic(_key), (_dv)))

/**
 * fy_generic_mapping_get_typed() - Look up a key in a mapping and return the value as a specific type.
 *
 * @_map:  fy_generic mapping value.
 * @_key:  Key (any type).
 * @_type: C type name of the desired output.
 *
 * Returns:
 * Value of type @_type, or the zero/NULL default.
 */
#define fy_generic_mapping_get_typed(_map, _key, _type) \
	(fy_generic_mapping_get_default((_map), (_key), fy_generic_get_type_default(_type)))

/* fy_generic_mappingp_get_default_Generic_dispatch - _Generic table for typed value lookup by key via a pointer */
#define fy_generic_mappingp_get_default_Generic_dispatch \
	FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_generic_mappingp_get, default), \
	char *: fy_generic_mappingp_get_char_ptr_default, \
	const char *: fy_generic_mappingp_get_const_char_ptr_default, \
	fy_generic_sequence_handle: fy_generic_mappingp_get_generic_sequence_handle_default, \
	fy_generic_mapping_handle: fy_generic_mappingp_get_generic_mapping_handle_default, \
	fy_generic_sized_string: fy_generic_mappingp_get_szstr_default, \
	fy_generic: fy_generic_mappingp_get_generic_default

/**
 * fy_generic_mappingp_get_default() - Look up a key in a mapping pointer and return the typed value.
 *
 * @_mapp: Pointer to a fy_generic_mapping.
 * @_key:  Key (any type).
 * @_dv:   Default value.
 *
 * Returns:
 * Value cast to the type of @_dv, or @_dv.
 */
#define fy_generic_mappingp_get_default(_mapp, _key, _dv) \
	({ \
		fy_generic_typeof(_dv) _ret; \
		fy_generic __key = fy_to_generic(_key); \
		\
		__ret = _Generic((_dv), fy_generic_mappingp_get_default_Generic_dispatch)((_mapp), __key, (_dv)); \
		__ret; \
	})

/* fy_generic_mappingp_get_typed() - Like fy_generic_mappingp_get_default() but derives default from @_type */
#define fy_generic_mappingp_get_typed(_mapp, _key, _type) \
	(fy_generic_mappingp_get_default((_mapp), (_key), fy_generic_get_type_default(_type)))

/* Mapping value-at-index typed helpers.
 * Each fy_generic_mappingp_get_at_<T>_default() accesses the VALUE at pair index @idx in a
 * fy_generic_mapping pointer, returning @default_value if out of bounds.
 * The fy_generic_mapping_get_at_<T>_default() variants accept a fy_generic map handle,
 * resolve it first, then delegate to the pointer variant.
 */

/* fy_generic_mappingp_get_at_generic_sequence_handle_default() - Value at index as sequence handle (pointer variant) */
static inline fy_generic_sequence_handle
fy_generic_mappingp_get_at_generic_sequence_handle_default(const fy_generic_mapping *mapp, const size_t idx,
		fy_generic_sequence_handle default_value)
{
	return fy_genericp_get_generic_sequence_handle_default(
			fy_generic_mappingp_get_at_valuep(mapp, idx),
			default_value);
}

/* fy_generic_mapping_get_at_generic_sequence_handle_default() - Value at index as sequence handle (generic variant) */
static inline fy_generic_sequence_handle
fy_generic_mapping_get_at_generic_sequence_handle_default(fy_generic map, const size_t idx,
		fy_generic_sequence_handle default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_at_generic_sequence_handle_default(mapp, idx, default_value);
}

/* fy_generic_mappingp_get_at_generic_mapping_handle_default() - Value at index as mapping handle (pointer variant) */
static inline fy_generic_mapping_handle
fy_generic_mappingp_get_at_generic_mapping_handle_default(const fy_generic_mapping *mapp, const size_t idx,
		fy_generic_mapping_handle default_value)
{
	return fy_genericp_get_generic_mapping_handle_default(
			fy_generic_mappingp_get_at_valuep(mapp, idx),
			default_value);
}

/* fy_generic_mapping_get_at_generic_mapping_handle_default() - Value at index as mapping handle (generic variant) */
static inline fy_generic_mapping_handle
fy_generic_mapping_get_at_generic_mapping_handle_default(fy_generic map, const size_t idx,
		fy_generic_mapping_handle default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_at_generic_mapping_handle_default(mapp, idx, default_value);
}

/* fy_generic_mappingp_get_at_generic_default() - Value at index as fy_generic (pointer variant) */
static inline fy_generic fy_generic_mappingp_get_at_generic_default(const fy_generic_mapping *mapp, const size_t idx, fy_generic default_value)
{
	return fy_genericp_get_generic_default(
			fy_generic_mappingp_get_at_valuep(mapp, idx),
			default_value);
}

/* fy_generic_mapping_get_at_generic_default() - Value at index as fy_generic (generic variant) */
static inline fy_generic fy_generic_mapping_get_at_generic_default(fy_generic map, const size_t idx, fy_generic default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_at_generic_default(mapp, idx, default_value);
}

/* fy_generic_mappingp_get_at_const_char_ptr_default() - Value at index as const char * (pointer variant) */
static inline const char *fy_generic_mappingp_get_at_const_char_ptr_default(const fy_generic_mapping *mapp, const size_t idx, const char *default_value)
{
	return fy_genericp_get_const_char_ptr_default(
			fy_generic_mappingp_get_at_valuep(mapp, idx),
			default_value);
}

/* fy_generic_mapping_get_at_const_char_ptr_default() - Value at index as const char * (generic variant) */
static inline const char *fy_generic_mapping_get_at_const_char_ptr_default(fy_generic map, const size_t idx, const char *default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_at_const_char_ptr_default(mapp, idx, default_value);
}

/* fy_generic_mappingp_get_at_char_ptr_default() - Value at index as char * (pointer variant; casts away const) */
static inline char *fy_generic_mappingp_get_at_char_ptr_default(const fy_generic_mapping *mapp, const size_t idx, const char *default_value)
{
	return (char *)fy_generic_mappingp_get_at_const_char_ptr_default(mapp, idx, default_value);
}

/* fy_generic_mapping_get_at_char_ptr_default() - Value at index as char * (generic variant; casts away const) */
static inline char *fy_generic_mapping_get_at_char_ptr_default(fy_generic map, const size_t idx, char *default_value)
{
	return (char *)fy_generic_mapping_get_at_const_char_ptr_default(map, idx, default_value);
}

/* fy_generic_mappingp_get_at_map_pairp_default() - Pointer to map pair at index (pointer variant) */
static inline const fy_generic_map_pair *fy_generic_mappingp_get_at_map_pairp_default(const fy_generic_mapping *mapp, const size_t idx, const fy_generic_map_pair *default_value)
{
	if (!mapp || idx >= mapp->count)
		return default_value;
	return &mapp->pairs[idx];
}

/* fy_generic_mappingp_get_at_map_pair_default() - Map pair by value at index (pointer variant) */
static inline fy_generic_map_pair fy_generic_mappingp_get_at_map_pair_default(const fy_generic_mapping *mapp, const size_t idx, fy_generic_map_pair default_value)
{
	if (!mapp || idx >= mapp->count)
		return default_value;
	return mapp->pairs[idx];
}

/* fy_generic_mapping_get_at_map_pair_default() - Map pair by value at index (generic variant) */
static inline fy_generic_map_pair fy_generic_mapping_get_at_map_pair_default(fy_generic map, const size_t idx, fy_generic_map_pair default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_at_map_pair_default(mapp, idx, default_value);
}

/* fy_generic_mapping_get_at_const_map_pairp_default() - Const pointer to map pair at index (generic variant) */
static inline const fy_generic_map_pair *
fy_generic_mapping_get_at_const_map_pairp_default(fy_generic map, const size_t idx, const fy_generic_map_pair *default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_at_map_pairp_default(mapp, idx, default_value);
}

/* fy_generic_mapping_get_at_map_pairp_default() - Mutable pointer to map pair at index (generic variant; casts away const) */
static inline fy_generic_map_pair *
fy_generic_mapping_get_at_map_pairp_default(fy_generic map, const size_t idx, fy_generic_map_pair *default_value)
{
	return (fy_generic_map_pair *)fy_generic_mapping_get_at_const_map_pairp_default(map, idx, default_value);
}

/* fy_generic_mappingp_get_at_szstr_default() - Value at index as sized string (pointer variant) */
static inline fy_generic_sized_string
fy_generic_mappingp_get_at_szstr_default(const fy_generic_mapping *mapp, const size_t idx,
		fy_generic_sized_string default_value)
{
	return fy_genericp_get_szstr_default(
			fy_generic_mappingp_get_at_valuep(mapp, idx),
			default_value);
}

/* fy_generic_mapping_get_at_szstr_default() - Value at index as sized string (generic variant) */
static inline fy_generic_sized_string
fy_generic_mapping_get_at_szstr_default(fy_generic map, const size_t idx,
		fy_generic_sized_string default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_at_szstr_default(mapp, idx, default_value);
}

/* fy_generic_mapping_get_at_default_Generic_dispatch - _Generic dispatch table for fy_generic_mapping_get_at_default() */
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

/**
 * fy_generic_mapping_get_at_default() - Get the value at index @_idx in a mapping, typed via default.
 *
 * @_map: fy_generic mapping value
 * @_idx: Zero-based pair index
 * @_dv:  Default value (type determines the return type)
 *
 * Returns:
 * The value at position @_idx cast to the type of @_dv, or @_dv if out of range.
 */
#define fy_generic_mapping_get_at_default(_map, _idx, _dv) \
	(_Generic((_dv), fy_generic_mapping_get_at_default_Generic_dispatch)((_map), (_idx), (_dv)))

/* fy_generic_mapping_get_at_typed() - Like fy_generic_mapping_get_at_default() but derives default from @_type */
#define fy_generic_mapping_get_at_typed(_map, _idx, _type) \
	(fy_generic_mapping_get_at_default((_map), (_idx), fy_generic_get_type_default(_type)))

/* fy_generic_mappingp_get_at_default_Generic_dispatch - _Generic dispatch table for fy_generic_mappingp_get_at_default() */
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

/**
 * fy_generic_mappingp_get_at_default() - Get the value at index @_idx in a mapping pointer, typed via default.
 *
 * @_mapp: Pointer to fy_generic_mapping
 * @_idx:  Zero-based pair index
 * @_dv:   Default value (type determines the return type)
 *
 * Returns:
 * The value at position @_idx cast to the type of @_dv, or @_dv if out of range.
 */
#define fy_generic_mappingp_get_at_default(_mapp, _idx, _dv) \
	(_Generic((_dv), fy_generic_mappingp_get_at_default_Generic_dispatch)((_mapp), (_idx), (_dv)))

/* fy_generic_mappingp_get_at_typed() - Like fy_generic_mappingp_get_at_default() but derives default from @_type */
#define fy_generic_mappingp_get_at_typed(_mapp, _idx, _type) \
	(fy_generic_mappingp_get_at_default((_mapp), (_idx), fy_generic_get_type_default(_type)))

/* Mapping key-at-index typed helpers.
 * Like the value-at-index family above but access the KEY of pair @idx instead.
 * fy_generic_mappingp_get_key_at_<T>_default() uses a fy_generic_mapping pointer;
 * fy_generic_mapping_get_key_at_<T>_default() resolves a fy_generic handle first.
 */

/* fy_generic_mappingp_get_key_at_generic_sequence_handle_default() - Key at index as sequence handle (pointer variant) */
static inline fy_generic_sequence_handle
fy_generic_mappingp_get_key_at_generic_sequence_handle_default(const fy_generic_mapping *mapp, const size_t idx,
		fy_generic_sequence_handle default_value)
{
	return fy_genericp_get_generic_sequence_handle_default(
			fy_generic_mappingp_get_at_keyp(mapp, idx),
			default_value);
}

/* fy_generic_mapping_get_key_at_generic_sequence_handle_default() - Key at index as sequence handle (generic variant) */
static inline fy_generic_sequence_handle
fy_generic_mapping_get_key_at_generic_sequence_handle_default(fy_generic map, const size_t idx,
		fy_generic_sequence_handle default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_key_at_generic_sequence_handle_default(mapp, idx, default_value);
}

/* fy_generic_mappingp_get_key_at_generic_mapping_handle_default() - Key at index as mapping handle (pointer variant) */
static inline fy_generic_mapping_handle
fy_generic_mappingp_get_key_at_generic_mapping_handle_default(const fy_generic_mapping *mapp, const size_t idx,
		fy_generic_mapping_handle default_value)
{
	return fy_genericp_get_generic_mapping_handle_default(
			fy_generic_mappingp_get_at_keyp(mapp, idx),
			default_value);
}

/* fy_generic_mapping_get_key_at_generic_mapping_handle_default() - Key at index as mapping handle (generic variant) */
static inline fy_generic_mapping_handle
fy_generic_mapping_get_key_at_generic_mapping_handle_default(fy_generic map, const size_t idx,
		fy_generic_mapping_handle default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_key_at_generic_mapping_handle_default(mapp, idx, default_value);
}

/* fy_generic_mappingp_get_key_at_generic_default() - Key at index as fy_generic (pointer variant) */
static inline fy_generic fy_generic_mappingp_get_key_at_generic_default(const fy_generic_mapping *mapp, const size_t idx, fy_generic default_value)
{
	return fy_genericp_get_generic_default(
			fy_generic_mappingp_get_at_keyp(mapp, idx),
			default_value);
}

/* fy_generic_mapping_get_key_at_generic_default() - Key at index as fy_generic (generic variant) */
static inline fy_generic fy_generic_mapping_get_key_at_generic_default(fy_generic map, const size_t idx, fy_generic default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_key_at_generic_default(mapp, idx, default_value);
}

/* fy_generic_mappingp_get_key_at_const_char_ptr_default() - Key at index as const char * (pointer variant) */
static inline const char *fy_generic_mappingp_get_key_at_const_char_ptr_default(const fy_generic_mapping *mapp, const size_t idx, const char *default_value)
{
	return fy_genericp_get_const_char_ptr_default(
			fy_generic_mappingp_get_at_keyp(mapp, idx),
			default_value);
}

/* fy_generic_mapping_get_key_at_const_char_ptr_default() - Key at index as const char * (generic variant) */
static inline const char *fy_generic_mapping_get_key_at_const_char_ptr_default(fy_generic map, const size_t idx, const char *default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_key_at_const_char_ptr_default(mapp, idx, default_value);
}

/* fy_generic_mappingp_get_key_at_char_ptr_default() - Key at index as char * (pointer variant; casts away const) */
static inline char *fy_generic_mappingp_get_key_at_char_ptr_default(const fy_generic_mapping *mapp, const size_t idx, const char *default_value)
{
	return (char *)fy_generic_mappingp_get_key_at_const_char_ptr_default(mapp, idx, default_value);
}

/* fy_generic_mapping_get_key_at_char_ptr_default() - Key at index as char * (generic variant; casts away const) */
static inline char *fy_generic_mapping_get_key_at_char_ptr_default(fy_generic map, const size_t idx, char *default_value)
{
	return (char *)fy_generic_mapping_get_key_at_const_char_ptr_default(map, idx, default_value);
}

/* fy_generic_mappingp_get_key_at_szstr_default() - Key at index as sized string (pointer variant) */
static inline fy_generic_sized_string
fy_generic_mappingp_get_key_at_szstr_default(const fy_generic_mapping *mapp, const size_t idx,
		fy_generic_sized_string default_value)
{
	return fy_genericp_get_szstr_default(
			fy_generic_mappingp_get_at_keyp(mapp, idx),
			default_value);
}

/* fy_generic_mapping_get_key_at_szstr_default() - Key at index as sized string (generic variant) */
static inline fy_generic_sized_string
fy_generic_mapping_get_key_at_szstr_default(fy_generic map, const size_t idx,
		fy_generic_sized_string default_value)
{
	const fy_generic_mapping *mapp = fy_generic_mapping_resolve(map);
	return fy_generic_mappingp_get_key_at_szstr_default(mapp, idx, default_value);
}

/* fy_generic_mapping_get_key_at_default_Generic_dispatch - _Generic dispatch table for fy_generic_mapping_get_key_at_default() */
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

/**
 * fy_generic_mapping_get_key_at_default() - Get the key at index @_idx in a mapping, typed via default.
 *
 * @_map: fy_generic mapping value
 * @_idx: Zero-based pair index
 * @_dv:  Default value (type determines the return type)
 *
 * Returns:
 * The key at position @_idx cast to the type of @_dv, or @_dv if out of range.
 */
#define fy_generic_mapping_get_key_at_default(_map, _idx, _dv) \
	(_Generic((_dv), fy_generic_mapping_get_key_at_default_Generic_dispatch)((_map), (_idx), (_dv)))

/* fy_generic_mapping_get_at_typed() - Like fy_generic_mapping_get_at_default() but derives default from @_type (duplicate alias) */
#define fy_generic_mapping_get_at_typed(_map, _idx, _type) \
	(fy_generic_mapping_get_at_default((_map), (_idx), fy_generic_get_type_default(_type)))

/* fy_generic_mappingp_get_key_at_default_Generic_dispatch - _Generic dispatch table for fy_generic_mappingp_get_key_at_default() */
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

/**
 * fy_generic_mappingp_get_key_at_default() - Get the key at index @_idx in a mapping pointer, typed via default.
 *
 * @_mapp: Pointer to fy_generic_mapping
 * @_idx:  Zero-based pair index
 * @_dv:   Default value (type determines the return type)
 *
 * Returns:
 * The key at position @_idx cast to the type of @_dv, or @_dv if out of range.
 */
#define fy_generic_mappingp_get_key_at_default(_mapp, _idx, _dv) \
	(_Generic((_dv), fy_generic_mappingp_get_key_at_default_Generic_dispatch)((_mapp), (_idx), (_dv)))

/* fy_generic_mappingp_get_key_at_typed() - Like fy_generic_mappingp_get_key_at_default() but derives default from @_type */
#define fy_generic_mappingp_get_key_at_typed(_mapp, _idx, _type) \
	(fy_generic_mappingp_get_key_at_default((_mapp), (_idx), fy_generic_get_type_default(_type)))

/* Helpers used by the top-level polymorphic get/len macros.
 * These accept a void* to allow _Generic dispatch without type mismatch.
 * fy_generic, fy_generic_sequence_handle and fy_generic_mapping_handle are
 * all the same size; the handle variants encode the pointer tag directly.
 */

/* fy_get_generic_generic() - Resolve indirect, then return the direct fy_generic value */
static inline fy_generic fy_get_generic_generic(const void *p)
{
	const fy_generic *vp = p;
	if (fy_generic_is_direct(*vp))
		return *vp;
	return fy_generic_indirect_get_value(*vp);
}

/**
 * fy_get_generic_direct_collection_type() - Determine whether a direct generic value is a sequence or mapping.
 *
 * @v: A direct fy_generic value (no indirection)
 *
 * Returns:
 * FYGT_SEQUENCE, FYGT_MAPPING, or FYGT_INVALID if @v is not a direct collection.
 */
static inline enum fy_generic_type fy_get_generic_direct_collection_type(fy_generic v)
{
	/* seq and collection have the lower 3 bits zero */
	if ((v.v & FY_INPLACE_TYPE_MASK) != 0)
		return FYGT_INVALID;
	/* sequence is 0, mapping is 8 */
	return FYGT_SEQUENCE + ((v.v >> 3) & 1);
}

/* fy_get_generic_seq_handle() - Box a sequence handle into a fy_generic value */
static inline fy_generic fy_get_generic_seq_handle(const void *p)
{
	const fy_generic_sequence_handle *seqh = p;
	return (fy_generic){ .v = fy_generic_in_place_sequence_handle(*seqh) };
}

/* fy_get_generic_map_handle() - Box a mapping handle into a fy_generic value */
static inline fy_generic fy_get_generic_map_handle(const void *p)
{
	const fy_generic_mapping_handle *maph = p;
	return (fy_generic){ .v = fy_generic_in_place_mapping_handle(*maph) };
}

/**
 * fy_generic_get_default() - Get an element from a collection by key or index, typed via default.
 *
 * Works uniformly on sequences and mappings. For mappings, @_key is converted to a
 * fy_generic and used as the lookup key. For sequences, @_key is coerced to a size_t index.
 * Accepts fy_generic, fy_generic_sequence_handle, or fy_generic_mapping_handle as @_colv.
 *
 * @_colv: Collection value (fy_generic, sequence handle, or mapping handle)
 * @_key:  Key for mappings, or integer index for sequences
 * @_dv:   Default value (type determines the return type)
 *
 * Returns:
 * The element at @_key cast to the type of @_dv, or @_dv if not found or wrong type.
 */
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
		case FYGT_MAPPING: { \
			const fy_generic __key = fy_to_generic(_key); \
			__ret = _Generic(__dv, fy_generic_mapping_get_default_Generic_dispatch) \
				(__colv2, __key, __dv); \
			} break; \
		case FYGT_SEQUENCE: { \
			const size_t __index = fy_generic_cast_default_coerse(_key, LLONG_MAX); \
			__ret = _Generic(__dv, fy_generic_sequence_get_default_Generic_dispatch) \
				(__colv2, __index, __dv); \
			} break; \
		default: \
			__ret = __dv; \
			break; \
		} \
		__ret; \
	})

/* fy_generic_get_typed() - Like fy_generic_get_default() but derives default from @_type */
#define fy_generic_get_typed(_colv, _key, _type) \
	(fy_generic_get_default((_colv), (_key), fy_generic_get_type_default(_type)))

/**
 * fy_generic_get_at_default() - Get an element from a collection at a numeric index, typed via default.
 *
 * Works uniformly on sequences and mappings. For mappings, @_idx selects the pair value.
 * Accepts fy_generic, fy_generic_sequence_handle, or fy_generic_mapping_handle as @_colv.
 *
 * @_colv: Collection value (fy_generic, sequence handle, or mapping handle)
 * @_idx:  Zero-based integer index
 * @_dv:   Default value (type determines the return type)
 *
 * Returns:
 * The element at @_idx cast to the type of @_dv, or @_dv if out of range.
 */
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

/* fy_generic_get_at_typed() - Like fy_generic_get_at_default() but derives default from @_type */
#define fy_generic_get_at_typed(_colv, _idx, _type) \
	(fy_generic_get_at_default((_colv), (_idx), fy_generic_get_type_default(_type)))

/**
 * fy_generic_get_key_at_default() - Get the KEY at a numeric index from a collection, typed via default.
 *
 * For mappings, returns the key of the pair at @_idx.
 * For sequences, returns the element at @_idx (keys are the elements themselves).
 *
 * @_colv: Collection value (fy_generic, sequence handle, or mapping handle)
 * @_idx:  Zero-based integer index
 * @_dv:   Default value (type determines the return type)
 *
 * Returns:
 * The key at @_idx cast to the type of @_dv, or @_dv if out of range.
 */
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

/* fy_generic_get_key_at_typed() - Like fy_generic_get_key_at_default() but derives default from @_type */
#define fy_generic_get_key_at_typed(_colv, _idx, _type) \
	(fy_generic_get_key_at_default((_colv), (_idx), fy_generic_get_type_default(_type)))

/* Length helpers used by fy_generic_len(). Accept void* for _Generic dispatch. */

/**
 * fy_get_len_genericp() - Return element/character count of a fy_generic collection or string.
 *
 * Resolves indirect values; returns count for sequences and mappings, byte length for strings.
 *
 * @p: Pointer to a fy_generic value
 *
 * Returns:
 * Element count (collection) or string length in bytes; 0 for scalar types or NULL.
 */
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

/* fy_get_len_seq_handle() - Item count of a sequence via handle pointer */
static inline size_t fy_get_len_seq_handle(const void *p)
{
	const fy_generic_sequence * const *seqpp = p;
	return fy_generic_sequencep_get_item_count(*seqpp);
}

/* fy_get_len_map_handle() - Pair count of a mapping via handle pointer */
static inline size_t fy_get_len_map_handle(const void *p)
{
	const fy_generic_mapping * const *mappp = p;
	return fy_generic_mappingp_get_pair_count(*mappp);
}

/**
 * fy_generic_len() - Return the number of elements in a collection or characters in a string.
 *
 * Accepts fy_generic, fy_generic_sequence_handle, or fy_generic_mapping_handle.
 * For strings, returns the byte length. For scalars, returns 0.
 *
 * @_colv: Collection or string value
 *
 * Returns:
 * Element count, pair count, or string length; 0 for non-collection scalars.
 */
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

/**
 * enum fy_generic_schema - YAML/JSON schema variant used during parsing and builder operations.
 *
 * @FYGS_AUTO:             Automatically select schema based on input
 * @FYGS_YAML1_2_FAILSAFE: YAML 1.2 failsafe schema (all values are strings)
 * @FYGS_YAML1_2_CORE:     YAML 1.2 core schema
 * @FYGS_YAML1_2_JSON:     YAML 1.2 JSON schema
 * @FYGS_YAML1_1_FAILSAFE: YAML 1.1 failsafe schema
 * @FYGS_YAML1_1:          YAML 1.1 schema
 * @FYGS_YAML1_1_PYYAML:   YAML 1.1 with PyYAML quirks
 * @FYGS_JSON:             JSON schema
 * @FYGS_PYTHON:           Python-compatible schema
 */
enum fy_generic_schema {
	// Core YAML/JSON schemas
	FYGS_AUTO,
	FYGS_YAML1_2_FAILSAFE,
	FYGS_YAML1_2_CORE,
	FYGS_YAML1_2_JSON,
	FYGS_YAML1_1_FAILSAFE,
	FYGS_YAML1_1,
	FYGS_YAML1_1_PYYAML,	// quirk
	FYGS_JSON,
	// Language-specific schemas
	FYGS_PYTHON
};
/* FYGS_COUNT - Total number of schema variants */
#define FYGS_COUNT (FYGS_PYTHON + 1)

/* fy_generic_schema_is_json() - Return true if @schema is one of the JSON schemas */
static inline bool fy_generic_schema_is_json(enum fy_generic_schema schema)
{
	return schema == FYGS_YAML1_2_JSON || schema == FYGS_JSON;
}

/* fy_generic_schema_is_yaml_1_2() - Return true if @schema is a YAML 1.2 schema */
static inline bool fy_generic_schema_is_yaml_1_2(enum fy_generic_schema schema)
{
	return schema >= FYGS_YAML1_2_FAILSAFE && schema <= FYGS_YAML1_2_JSON;
}

/* fy_generic_schema_is_yaml_1_1() - Return true if @schema is a YAML 1.1 schema */
static inline bool fy_generic_schema_is_yaml_1_1(enum fy_generic_schema schema)
{
	return schema >= FYGS_YAML1_1_FAILSAFE && schema <= FYGS_YAML1_1_PYYAML;
}

/**
 * fy_generic_schema_get_text() - Return a human-readable name for @schema.
 *
 * @schema: A fy_generic_schema value
 *
 * Returns:
 * A static NUL-terminated string describing the schema, or "unknown" for invalid values.
 */
const char *
fy_generic_schema_get_text(enum fy_generic_schema schema)
	FY_EXPORT;

/* Shift amount of the schema mode option */
#define FYGBCF_SCHEMA_SHIFT		0
/* Mask of the schema mode */
#define FYGBCF_SCHEMA_MASK		((1U << 4) - 1)
/* Build a schema mode option */
#define FYGBCF_SCHEMA(x)		(((unsigned int)(x) & FYGBCF_SCHEMA_MASK) << FYGBCF_SCHEMA_SHIFT)

/**
 * enum fy_gb_cfg_flags - Generic builder configuration flags.
 *
 * Schema selection occupies bits 0-3 (FYGBCF_SCHEMA_* values); remaining bits are
 * boolean flags.  Pass to fy_generic_builder_cfg.flags or fy_generic_builder_create_in_place().
 *
 * @FYGBCF_SCHEMA_AUTO:            Auto-detect schema from content
 * @FYGBCF_SCHEMA_YAML1_2_FAILSAFE: Use YAML 1.2 failsafe schema
 * @FYGBCF_SCHEMA_YAML1_2_CORE:    Use YAML 1.2 core schema
 * @FYGBCF_SCHEMA_YAML1_2_JSON:    Use YAML 1.2 JSON schema
 * @FYGBCF_SCHEMA_YAML1_1_FAILSAFE: Use YAML 1.1 failsafe schema
 * @FYGBCF_SCHEMA_YAML1_1:         Use YAML 1.1 schema
 * @FYGBCF_SCHEMA_YAML1_1_PYYAML:  Use YAML 1.1 with PyYAML quirks
 * @FYGBCF_SCHEMA_JSON:            Use JSON schema
 * @FYGBCF_SCHEMA_PYTHON:          Use Python-compatible schema
 * @FYGBCF_OWNS_ALLOCATOR:         Builder will destroy the allocator on cleanup
 * @FYGBCF_CREATE_ALLOCATOR:       Builder creates its own allocator
 * @FYGBCF_DUPLICATE_KEYS_DISABLED: Reject duplicate mapping keys
 * @FYGBCF_DEDUP_ENABLED:          Enable string/value deduplication
 * @FYGBCF_SCOPE_LEADER:           Builder is the scope leader (owns the scope)
 * @FYGBCF_CREATE_TAG:             Create an allocator tag on setup
 * @FYGBCF_TRACE:                  Enable diagnostic tracing
 */
enum fy_gb_cfg_flags {
	FYGBCF_SCHEMA_AUTO		= FYGBCF_SCHEMA(FYGS_AUTO),
	FYGBCF_SCHEMA_YAML1_2_FAILSAFE	= FYGBCF_SCHEMA(FYGS_YAML1_2_FAILSAFE),
	FYGBCF_SCHEMA_YAML1_2_CORE	= FYGBCF_SCHEMA(FYGS_YAML1_2_CORE),
	FYGBCF_SCHEMA_YAML1_2_JSON	= FYGBCF_SCHEMA(FYGS_YAML1_2_JSON),
	FYGBCF_SCHEMA_YAML1_1_FAILSAFE	= FYGBCF_SCHEMA(FYGS_YAML1_1_FAILSAFE),
	FYGBCF_SCHEMA_YAML1_1		= FYGBCF_SCHEMA(FYGS_YAML1_1),
	FYGBCF_SCHEMA_YAML1_1_PYYAML	= FYGBCF_SCHEMA(FYGS_YAML1_1_PYYAML),
	FYGBCF_SCHEMA_JSON		= FYGBCF_SCHEMA(FYGS_JSON),
	FYGBCF_SCHEMA_PYTHON		= FYGBCF_SCHEMA(FYGS_PYTHON),
	FYGBCF_OWNS_ALLOCATOR		= FY_BIT(4),
	FYGBCF_CREATE_ALLOCATOR		= FY_BIT(5),
	FYGBCF_DUPLICATE_KEYS_DISABLED	= FY_BIT(6),
	FYGBCF_DEDUP_ENABLED		= FY_BIT(7),
	FYGBCF_SCOPE_LEADER		= FY_BIT(8),
	FYGBCF_CREATE_TAG		= FY_BIT(9),
	FYGBCF_TRACE			= FY_BIT(10),
};

/**
 * struct fy_generic_builder_cfg - Configuration for creating a generic builder.
 *
 * @flags:              Configuration flags (see enum fy_gb_cfg_flags)
 * @allocator:          Optional pre-existing allocator to use; NULL lets the builder create one
 * @parent:             Optional parent builder for scoped (child) builders
 * @estimated_max_size: Hint for initial allocator capacity (0 = use default)
 * @diag:               Optional diagnostics context; NULL = no diagnostics
 */
struct fy_generic_builder_cfg {
	enum fy_gb_cfg_flags flags;
	struct fy_allocator *allocator;
	struct fy_generic_builder *parent;
	size_t estimated_max_size;
	struct fy_diag *diag;
};

/**
 * enum fy_gb_flags - Runtime state flags for a generic builder instance.
 *
 * These flags reflect the operating state of a builder and are not user-settable
 * directly; they are derived from the configuration flags at creation time.
 *
 * @FYGBF_NONE:           No flags set
 * @FYGBF_SCOPE_LEADER:   Builder is the scope leader
 * @FYGBF_DEDUP_ENABLED:  Deduplication is active on this builder
 * @FYGBF_DEDUP_CHAIN:    All builders in the chain have deduplication enabled
 * @FYGBF_OWNS_ALLOCATOR: Builder owns and will destroy the allocator
 * @FYGBF_CREATED_TAG:    Builder created a tag on the allocator
 */
enum fy_gb_flags {
	FYGBF_NONE			= 0,
	FYGBF_SCOPE_LEADER		= FY_BIT(0),	/* builder starts new scope */
	FYGBF_DEDUP_ENABLED		= FY_BIT(1),	/* build is dedup enabled */
	FYGBF_DEDUP_CHAIN		= FY_BIT(2),	/* builder chain is dedup */
	FYGBF_OWNS_ALLOCATOR		= FY_BIT(3),	/* builder owns the allocator */
	FYGBF_CREATED_TAG		= FY_BIT(4),	/* builder has created tag on allocator */
};

/**
 * fy_generic_builder_setup() - Initialize a pre-allocated generic builder structure.
 *
 * @gb:  Pointer to an already-allocated fy_generic_builder to initialize
 * @cfg: Configuration; NULL uses defaults
 *
 * Returns:
 * 0 on success, -1 on error.
 */
int
fy_generic_builder_setup(struct fy_generic_builder *gb, const struct fy_generic_builder_cfg *cfg)
	FY_EXPORT;

/**
 * fy_generic_builder_cleanup() - Release resources held by a builder initialized via fy_generic_builder_setup().
 *
 * Does NOT free @gb itself (mirrors fy_generic_builder_setup()).
 *
 * @gb: Builder to clean up
 */
void
fy_generic_builder_cleanup(struct fy_generic_builder *gb)
	FY_EXPORT;

/**
 * fy_generic_builder_create() - Heap-allocate and initialize a generic builder.
 *
 * @cfg: Configuration; NULL uses defaults
 *
 * Returns:
 * Newly allocated builder, or NULL on error. Free with fy_generic_builder_destroy().
 */
struct fy_generic_builder *
fy_generic_builder_create(const struct fy_generic_builder_cfg *cfg)
	FY_EXPORT;

/**
 * fy_generic_builder_destroy() - Destroy and free a builder created by fy_generic_builder_create().
 *
 * @gb: Builder to destroy (may be NULL)
 */
void
fy_generic_builder_destroy(struct fy_generic_builder *gb)
	FY_EXPORT;

/**
 * fy_generic_builder_reset() - Reset a builder to its initial empty state without freeing it.
 *
 * All previously built values become invalid after reset.
 *
 * @gb: Builder to reset
 */
void
fy_generic_builder_reset(struct fy_generic_builder *gb)
	FY_EXPORT;

/**
 * fy_gb_alloc() - Allocate raw bytes from the builder's arena.
 *
 * @gb:    Builder
 * @size:  Bytes to allocate
 * @align: Required alignment in bytes
 *
 * Returns:
 * Pointer to allocated memory, or NULL on failure.
 */
void *
fy_gb_alloc(struct fy_generic_builder *gb, size_t size, size_t align)
	FY_EXPORT;

/**
 * fy_gb_free() - Release a previously allocated block back to the builder.
 *
 * @gb:  Builder
 * @ptr: Pointer returned by fy_gb_alloc()
 */
void
fy_gb_free(struct fy_generic_builder *gb, void *ptr)
	FY_EXPORT;

/**
 * fy_gb_trim() - Trim excess reserved capacity in the builder's allocator.
 *
 * @gb: Builder
 */
void
fy_gb_trim(struct fy_generic_builder *gb)
	FY_EXPORT;

/**
 * fy_gb_store() - Copy @size bytes of @data into the builder's arena.
 *
 * @gb:    Builder
 * @data:  Source data
 * @size:  Bytes to copy
 * @align: Required alignment
 *
 * Returns:
 * Pointer to the stored copy inside the arena, or NULL on failure.
 */
const void *
fy_gb_store(struct fy_generic_builder *gb, const void *data, size_t size, size_t align)
	FY_EXPORT;

/**
 * fy_gb_storev() - Scatter-gather store: copy multiple iovec buffers into the arena.
 *
 * @gb:     Builder
 * @iov:    Array of iovec descriptors
 * @iovcnt: Number of iovec entries
 * @align:  Required alignment
 *
 * Returns:
 * Pointer to the contiguous stored copy, or NULL on failure.
 */
const void *
fy_gb_storev(struct fy_generic_builder *gb, const struct iovec *iov, unsigned int iovcnt, size_t align)
	FY_EXPORT;

/**
 * fy_gb_lookupv() - Look up existing data in the builder's arena by scatter-gather comparison.
 *
 * If deduplication is enabled, returns a pointer to an existing equal block.
 * Otherwise returns NULL
 *
 * @gb:     Builder
 * @iov:    Array of iovec descriptors describing the data to find
 * @iovcnt: Number of iovec entries
 * @align:  Required alignment
 *
 * Returns:
 * Pointer to existing copy, or NULL on failure.
 */
const void *
fy_gb_lookupv(struct fy_generic_builder *gb, const struct iovec *iov, unsigned int iovcnt, size_t align)
	FY_EXPORT;

/**
 * fy_gb_lookup() - Look up existing data in the builder's arena (single buffer variant).
 *
 * If deduplication is enabled, returns a pointer to an existing equal block.
 * Otherwise returns NULL
 *
 * @gb:    Builder
 * @data:  Data to find or store
 * @size:  Byte size of @data
 * @align: Required alignment
 *
 * Returns:
 * Pointer to existing copy, or NULL on failure.
 */
const void *
fy_gb_lookup(struct fy_generic_builder *gb, const void *data, size_t size, size_t align)
	FY_EXPORT;

/**
 * fy_gb_get_allocator_info() - Retrieve statistics about the builder's allocator.
 *
 * @gb: Builder
 *
 * Returns:
 * Pointer to allocator info (valid until the next allocation), or NULL.
 */
struct fy_allocator_info *
fy_gb_get_allocator_info(struct fy_generic_builder *gb)
	FY_EXPORT;

/**
 * fy_gb_release() - Release a reference to an arena allocation.
 *
 * @gb:   Builder
 * @ptr:  Pointer previously returned by a store/alloc call
 * @size: Size that was allocated
 */
void
fy_gb_release(struct fy_generic_builder *gb, const void *ptr, size_t size)
	FY_EXPORT;

/**
 * fy_gb_allocation_failures() - Return number of allocation failures since last reset.
 *
 * Non-zero indicates that at least one operation ran out of arena space.
 * This is used by FY_LOCAL_OP to decide whether to retry with a larger buffer.
 *
 * @gb: Builder
 *
 * Returns:
 * Count of allocation failures.
 */
uint64_t
fy_gb_allocation_failures(struct fy_generic_builder *gb)
	FY_EXPORT;

/**
 * fy_generic_builder_set_userdata() - Attach arbitrary user data to a builder.
 *
 * @gb:       Builder
 * @userdata: Pointer to user-supplied data
 *
 * Returns:
 * The previous userdata pointer.
 */
void *
fy_generic_builder_set_userdata(struct fy_generic_builder *gb, void *userdata)
	FY_EXPORT;

/**
 * fy_generic_builder_get_userdata() - Retrieve user data previously attached to a builder.
 *
 * @gb: Builder
 *
 * Returns:
 * The userdata pointer, or NULL if not set.
 */
void *
fy_generic_builder_get_userdata(struct fy_generic_builder *gb)
	FY_EXPORT;

/* FY_GENERIC_BUILDER_LINEAR_IN_PLACE_MIN_SIZE - Minimum buffer size for an in-place builder */
#define FY_GENERIC_BUILDER_LINEAR_IN_PLACE_MIN_SIZE	(FY_LINEAR_ALLOCATOR_IN_PLACE_MIN_SIZE + 128)

/**
 * fy_generic_builder_create_in_place() - Create a builder using a caller-supplied stack/static buffer.
 *
 * The builder uses @buffer as its backing store; no heap allocation is required.
 * The resulting builder does NOT need to be destroyed (no fy_generic_builder_destroy() call).
 * If the buffer runs out, operations fail with allocation errors.
 *
 * @flags:  Configuration flags (see enum fy_gb_cfg_flags)
 * @parent: Optional parent builder; NULL for standalone
 * @buffer: Caller-supplied backing buffer (typically alloca'd or stack-allocated)
 * @size:   Size of @buffer in bytes; must be >= FY_GENERIC_BUILDER_LINEAR_IN_PLACE_MIN_SIZE
 *
 * Returns:
 * Pointer to the builder (embedded in @buffer), or NULL on error.
 */
struct fy_generic_builder *
fy_generic_builder_create_in_place(enum fy_gb_cfg_flags flags, struct fy_generic_builder *parent, void *buffer, size_t size)
	FY_EXPORT;

/**
 * fy_generic_builder_get_allocator() - Return the allocator used by a builder.
 *
 * @gb: Builder
 *
 * Returns:
 * Pointer to the underlying fy_allocator, or NULL.
 */
struct fy_allocator *
fy_generic_builder_get_allocator(struct fy_generic_builder *gb)
	FY_EXPORT;

/**
 * fy_generic_builder_get_cfg() - Return the configuration used to create a builder.
 *
 * @gb: Builder
 *
 * Returns:
 * Pointer to the fy_generic_builder_cfg, or NULL for in-place builders.
 */
const struct fy_generic_builder_cfg *
fy_generic_builder_get_cfg(struct fy_generic_builder *gb)
	FY_EXPORT;

/**
 * fy_generic_builder_get_flags() - Return the runtime flags of a builder.
 *
 * @gb: Builder
 *
 * Returns:
 * The current fy_gb_flags bitmask.
 */
enum fy_gb_flags
fy_generic_builder_get_flags(struct fy_generic_builder *gb)
	FY_EXPORT;

/**
 * fy_generic_builder_get_free() - Return available free bytes in the builder's current arena.
 *
 * @gb: Builder
 *
 * Returns:
 * Bytes available before the next arena extension or failure.
 */
size_t
fy_generic_builder_get_free(struct fy_generic_builder *gb)
	FY_EXPORT;

/**
 * fy_generic_builder_contains_out_of_place() - Check whether an out-of-place generic lives in this builder's arena.
 *
 * @gb: Builder
 * @v:  Out-of-place generic value
 *
 * Returns:
 * true if the pointer inside @v points into @gb's arena.
 */
bool
fy_generic_builder_contains_out_of_place(struct fy_generic_builder *gb, fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_builder_contains() - Check whether a generic value is owned by this builder.
 *
 * Inplace values are always considered owned; invalid values are never owned.
 *
 * @gb: Builder (may be NULL)
 * @v:  Value to test
 *
 * Returns:
 * true if @v is inplace or its pointer lives in @gb's arena.
 */
static inline bool fy_generic_builder_contains(struct fy_generic_builder *gb, fy_generic v)
{
	/* invalids never contained */
	if (fy_generic_is_direct_invalid(v))
		return false;

	/* inplace always contained (implicitly) */
	if (fy_generic_is_in_place(v))
		return true;

	if (!gb)
		return false;

	return fy_generic_builder_contains_out_of_place(gb, v);
}

/**
 * fy_generic_builder_get_scope_leader() - Walk the builder chain to find the scope-leader builder.
 *
 * @gb: Builder
 *
 * Returns:
 * The scope-leader builder in the chain, or NULL.
 */
struct fy_generic_builder *
fy_generic_builder_get_scope_leader(struct fy_generic_builder *gb)
	FY_EXPORT;

/**
 * fy_generic_builder_get_export_builder() - Return the builder to which values should be exported.
 *
 * The export builder is the scope-leader's parent (or the scope-leader itself if at the root).
 *
 * @gb: Builder
 *
 * Returns:
 * The export builder, or NULL.
 */
struct fy_generic_builder *
fy_generic_builder_get_export_builder(struct fy_generic_builder *gb)
	FY_EXPORT;

/**
 * fy_generic_builder_export() - Copy a value into the export builder's arena if necessary.
 *
 * Ensures that @v is owned by the export builder so it survives beyond the scope
 * of the current (child) builder.
 *
 * @gb: Builder (the source scope)
 * @v:  Value to export
 *
 * Returns:
 * The exported value (possibly a copy), or fy_invalid on error.
 */
fy_generic
fy_generic_builder_export(struct fy_generic_builder *gb, fy_generic v)
	FY_EXPORT;

/* Primitive out-of-place type creators — force heap/arena allocation even when inplace would fit.
 * Normally used internally; prefer fy_gb_string_size_create() / fy_gb_to_generic() for user code.
 */

/* fy_gb_null_type_create_out_of_place() - Create an out-of-place null generic (for completeness; null is always inplace) */
fy_generic
fy_gb_null_type_create_out_of_place(struct fy_generic_builder *gb, void *p)
	FY_EXPORT;

/* fy_gb_bool_type_create_out_of_place() - Create an out-of-place bool generic */
fy_generic
fy_gb_bool_type_create_out_of_place(struct fy_generic_builder *gb, bool state)
	FY_EXPORT;

/* fy_gb_dint_type_create_out_of_place() - Create an out-of-place decorated-integer generic */
fy_generic
fy_gb_dint_type_create_out_of_place(struct fy_generic_builder *gb, fy_generic_decorated_int vald)
	FY_EXPORT;

/* fy_gb_int_type_create_out_of_place() - Create an out-of-place signed integer generic */
fy_generic
fy_gb_int_type_create_out_of_place(struct fy_generic_builder *gb, long long val)
	FY_EXPORT;

/* fy_gb_uint_type_create_out_of_place() - Create an out-of-place unsigned integer generic */
fy_generic
fy_gb_uint_type_create_out_of_place(struct fy_generic_builder *gb, unsigned long long val)
	FY_EXPORT;

/* fy_gb_float_type_create_out_of_place() - Create an out-of-place double generic */
fy_generic
fy_gb_float_type_create_out_of_place(struct fy_generic_builder *gb, double val)
	FY_EXPORT;

/**
 * fy_gb_string_size_create_out_of_place() - Intern a string of length @len in the builder's arena.
 *
 * @gb:  Builder
 * @str: String data (need not be NUL-terminated)
 * @len: String length in bytes
 *
 * Returns:
 * Out-of-place fy_generic string, or fy_invalid on allocation failure.
 */
fy_generic
fy_gb_string_size_create_out_of_place(struct fy_generic_builder *gb, const char *str, size_t len)
	FY_EXPORT;

/**
 * fy_gb_string_create_out_of_place() - Intern a NUL-terminated string in the builder's arena.
 *
 * @gb:  Builder
 * @str: NUL-terminated string
 *
 * Returns:
 * Out-of-place fy_generic string, or fy_invalid on allocation failure.
 */
fy_generic
fy_gb_string_create_out_of_place(struct fy_generic_builder *gb, const char *str)
	FY_EXPORT;

/**
 * fy_gb_szstr_create_out_of_place() - Intern a sized string in the builder's arena.
 *
 * @gb:    Builder
 * @szstr: Sized string (length + pointer)
 *
 * Returns:
 * Out-of-place fy_generic string, or fy_invalid on allocation failure.
 */
fy_generic
fy_gb_szstr_create_out_of_place(struct fy_generic_builder *gb, const fy_generic_sized_string szstr)
	FY_EXPORT;

/**
 * fy_gb_string_size_create() - Create a string generic, choosing inplace encoding if possible.
 *
 * Strings up to 7 bytes (64-bit) or 3 bytes (32-bit) are encoded inplace with no allocation.
 * Longer strings are stored in the builder's arena.
 *
 * @gb:  Builder
 * @str: String data
 * @len: Length in bytes
 *
 * Returns:
 * fy_generic string value, or fy_invalid on allocation failure.
 */
static inline fy_generic
fy_gb_string_size_create(struct fy_generic_builder *gb, const char *str, size_t len)
{
	fy_generic v = { .v = fy_generic_in_place_char_ptr_len(str, len) };
	if (v.v != fy_invalid_value)
		return v;
	return fy_gb_string_size_create_out_of_place(gb, str, len);
}

/* fy_gb_string_create() - Create a string generic from a NUL-terminated string, choosing inplace if possible */
static inline fy_generic
fy_gb_string_create(struct fy_generic_builder *gb, const char *str)
{
	return fy_gb_string_size_create(gb, str, strlen(str));
}

/* Type conversion functions — convert a fy_generic value to a different scalar type */

/* fy_gb_to_int() - Convert @v to an integer generic using the builder for out-of-place storage */
fy_generic
fy_gb_to_int(struct fy_generic_builder *gb, fy_generic v)
	FY_EXPORT;

/* fy_gb_to_float() - Convert @v to a float generic using the builder for out-of-place storage */
fy_generic
fy_gb_to_float(struct fy_generic_builder *gb, fy_generic v)
	FY_EXPORT;

/* fy_gb_to_string() - Convert @v to a string generic using the builder for out-of-place storage */
fy_generic
fy_gb_to_string(struct fy_generic_builder *gb, fy_generic v)
	FY_EXPORT;

/* fy_gb_to_bool() - Convert @v to a bool generic */
fy_generic
fy_gb_to_bool(struct fy_generic_builder *gb, fy_generic v)
	FY_EXPORT;

/**
 * FY_GENERIC_GB_LVAL_TEMPLATE() - Generate fy_gb_<T>_create_out_of_place() and fy_gb_<T>_create() for C type @_ctype.
 *
 * @_ctype:    C type (e.g. int, double)
 * @_gtype:    Name token used in the generated function names (e.g. int, double)
 * @_gttype:   Generic type tag (e.g. INT, FLOAT) — unused in generated code
 * @_xctype:   Cast destination type for the underlying primitive creator
 * @_xgtype:   Base type creator (int_type, uint_type, float_type, etc.)
 * @_xminv:    Minimum value (unused at runtime; kept for documentation)
 * @_xmaxv:    Maximum value (unused at runtime)
 * @_default_v: Default value (unused at runtime)
 */
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

/* FY_GENERIC_GB_INT_LVAL_TEMPLATE() - Instantiate FY_GENERIC_GB_LVAL_TEMPLATE for a signed integer type */
#define FY_GENERIC_GB_INT_LVAL_TEMPLATE(_ctype, _gtype, _xminv, _xmaxv, _defaultv) \
	FY_GENERIC_GB_LVAL_TEMPLATE(_ctype, _gtype, INT, long long, int_type, _xminv, _xmaxv, _defaultv)

/* FY_GENERIC_GB_UINT_LVAL_TEMPLATE() - Instantiate FY_GENERIC_GB_LVAL_TEMPLATE for an unsigned integer type */
#define FY_GENERIC_GB_UINT_LVAL_TEMPLATE(_ctype, _gtype, _xminv, _xmaxv, _defaultv) \
	FY_GENERIC_GB_LVAL_TEMPLATE(_ctype, _gtype, INT, unsigned long long, uint_type, _xminv, _xmaxv, _defaultv)

/* FY_GENERIC_GB_FLOAT_LVAL_TEMPLATE() - Instantiate FY_GENERIC_GB_LVAL_TEMPLATE for a floating-point type */
#define FY_GENERIC_GB_FLOAT_LVAL_TEMPLATE(_ctype, _gtype, _xminv, _xmaxv, _defaultv) \
	FY_GENERIC_GB_LVAL_TEMPLATE(_ctype, _gtype, FLOAT, double, float_type, _xminv, _xmaxv, _defaultv)

/* Instantiate fy_gb_<T>_create() and fy_gb_<T>_create_out_of_place() for all scalar C types */
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

/**
 * fy_gb_to_generic_outofplace_put() - Encode @_v as an out-of-place generic using the builder.
 *
 * Uses _Generic to select the appropriate fy_gb_<T>_create_out_of_place() function.
 *
 * @_gb: Builder
 * @_v:  Value of any supported C type
 *
 * Returns:
 * Out-of-place fy_generic, or fy_invalid on allocation failure.
 */
#define fy_gb_to_generic_outofplace_put(_gb, _v) \
	(_Generic((_v), \
		FY_GENERIC_ALL_SCALARS_DISPATCH_SFX(fy_gb, create_out_of_place), \
		char *: fy_gb_string_create_out_of_place, \
		const char *: fy_gb_string_create_out_of_place, \
		fy_generic: fy_gb_internalize_out_of_place, \
		fy_generic_sized_string: fy_gb_szstr_create_out_of_place, \
		fy_generic_decorated_int: fy_gb_dint_type_create_out_of_place \
	      )((_gb), (_v)))

/**
 * fy_gb_to_generic_value() - Encode @_v as a fy_generic_value using the builder for out-of-place storage.
 *
 * Tries inplace encoding first; falls back to the builder arena.
 *
 * @_gb: Builder
 * @_v:  Value to encode
 *
 * Returns:
 * fy_generic_value suitable for wrapping in a fy_generic struct.
 */
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

/**
 * fy_gb_to_generic() - Encode @_v as a fy_generic using the builder for out-of-place storage.
 *
 * @_gb: Builder
 * @_v:  Value to encode
 *
 * Returns:
 * fy_generic value backed by the builder's arena (or inplace for small values).
 */
#define fy_gb_to_generic(_gb, _v) \
	((fy_generic) { .v = fy_gb_to_generic_value((_gb), (_v)) })

/* fy_gb_or_NULL() - Return @_maybe_gb if it is a fy_generic_builder*, otherwise NULL */
#define fy_gb_or_NULL(_maybe_gb) \
	(_Generic((_maybe_gb), struct fy_generic_builder *: (_maybe_gb), default: NULL))

/* fy_first_non_gb() - If @_maybe_gb is a builder, return first variadic arg; otherwise return @_maybe_gb */
#define fy_first_non_gb(_maybe_gb, ...) \
	(_Generic((_maybe_gb), \
		  struct fy_generic_builder *: FY_CPP_FIRST(__VA_ARGS__), \
		  default: (_maybe_gb)))

/* fy_second_non_gb() - If @_maybe_gb is a builder, return second variadic arg; otherwise return first */
#define fy_second_non_gb(_maybe_gb, ...) \
	(_Generic((_maybe_gb), \
		  struct fy_generic_builder *: FY_CPP_SECOND(__VA_ARGS__), \
		  default: FY_CPP_FIRST(__VA_ARGS__)))

/* fy_third_non_gb() - If @_maybe_gb is a builder, return third variadic arg; otherwise return second */
#define fy_third_non_gb(_maybe_gb, ...) \
	(_Generic((_maybe_gb), \
		  struct fy_generic_builder *: FY_CPP_THIRD(__VA_ARGS__), \
		  default: FY_CPP_SECOND(__VA_ARGS__)))

/* fy_gb_to_generic_value_helper() - Internal helper forwarding a single arg to fy_gb_to_generic_value() */
#define fy_gb_to_generic_value_helper(_gb, _arg, ...) \
	fy_gb_to_generic_value((_gb), (_arg))

/**
 * fy_to_generic_value() - Encode a value using a builder (if provided) or alloca (for inplace / short-lived).
 *
 * If @_maybe_gb is a fy_generic_builder*, delegates to fy_gb_to_generic_value().
 * Otherwise treats @_maybe_gb as the value itself and uses fy_local_to_generic_value().
 *
 * @_maybe_gb: Either a fy_generic_builder* or any other C value
 *
 * Returns:
 * fy_generic_value for the encoded result.
 */
#define fy_to_generic_value(_maybe_gb, ...) \
	(_Generic((_maybe_gb), \
		struct fy_generic_builder *: ({ fy_gb_to_generic_value_helper(fy_gb_or_NULL(_maybe_gb), __VA_ARGS__ __VA_OPT__(,) 0); }), \
		default: (fy_local_to_generic_value((_maybe_gb)))))

/* fy_to_generic() - Unified generic encoder: builder or alloca depending on first arg type */
#define fy_to_generic(_maybe_gb, ...) \
	((fy_generic) { .v = fy_to_generic_value((_maybe_gb), ##__VA_ARGS__) })

/**
 * fy_gb_string_vcreate() - Create a string generic from a printf-style format string and va_list.
 *
 * @gb:  Builder
 * @fmt: printf-compatible format string
 * @ap:  Variadic argument list
 *
 * Returns:
 * fy_generic string, or fy_invalid on error.
 */
fy_generic
fy_gb_string_vcreate(struct fy_generic_builder *gb, const char *fmt, va_list ap)
	FY_EXPORT;

/**
 * fy_gb_string_createf() - Create a string generic from a printf-style format string.
 *
 * @gb:  Builder
 * @fmt: printf-compatible format string
 * @...: Format arguments
 *
 * Returns:
 * fy_generic string, or fy_invalid on error.
 */
fy_generic
fy_gb_string_createf(struct fy_generic_builder *gb, const char *fmt, ...)
	FY_FORMAT(printf, 2, 3)
	FY_EXPORT;

/* FYGBOPF_OP_SHIFT - Bit position of the opcode field within fy_gb_op_flags */
#define FYGBOPF_OP_SHIFT		0
/* FYGBOPF_OP_MASK - Mask covering the 8-bit opcode field */
#define FYGBOPF_OP_MASK			((1U << 8) - 1)
/* FYGBOPF_OP() - Build the opcode bits from an enum fy_gb_op value */
#define FYGBOPF_OP(x)			(((unsigned int)(x) & FYGBOPF_OP_MASK) << FYGBOPF_OP_SHIFT)

/**
 * enum fy_gb_op - Operation code for generic builder operations.
 *
 * Selects which collection or scalar operation fy_generic_op() / fy_generic_op_args()
 * should perform.  Encoded in the low 8 bits of fy_gb_op_flags via FYGBOPF_OP().
 *
 * @FYGBOP_CREATE_INV:      Create an invalid (sentinel) generic
 * @FYGBOP_CREATE_NULL:     Create a null generic
 * @FYGBOP_CREATE_BOOL:     Create a boolean generic from scalar args
 * @FYGBOP_CREATE_INT:      Create an integer generic from scalar args
 * @FYGBOP_CREATE_FLT:      Create a floating-point generic from scalar args
 * @FYGBOP_CREATE_STR:      Create a string generic from scalar args
 * @FYGBOP_CREATE_SEQ:      Create a sequence from an item array
 * @FYGBOP_CREATE_MAP:      Create a mapping from a key/value pair array
 * @FYGBOP_INSERT:          Insert elements into a sequence at a given index
 * @FYGBOP_REPLACE:         Replace elements in a sequence at a given index
 * @FYGBOP_APPEND:          Append elements to the end of a sequence or mapping
 * @FYGBOP_ASSOC:           Associate key/value pairs into a mapping
 * @FYGBOP_DISASSOC:        Remove key/value pairs from a mapping
 * @FYGBOP_KEYS:            Extract all keys of a mapping as a sequence
 * @FYGBOP_VALUES:          Extract all values of a mapping as a sequence
 * @FYGBOP_ITEMS:           Extract all key+value pairs of a mapping as a sequence
 * @FYGBOP_CONTAINS:        Test whether a collection contains given elements
 * @FYGBOP_CONCAT:          Concatenate two or more sequences or mappings
 * @FYGBOP_REVERSE:         Reverse the order of a sequence
 * @FYGBOP_MERGE:           Deep-merge two mappings
 * @FYGBOP_UNIQUE:          Remove duplicate elements from a sequence
 * @FYGBOP_SORT:            Sort a sequence using a comparator
 * @FYGBOP_FILTER:          Keep elements satisfying a predicate
 * @FYGBOP_MAP:             Transform each element via a function
 * @FYGBOP_REDUCE:          Fold a sequence into an accumulator via a function
 * @FYGBOP_SLICE:           Extract a subrange [start, end) by unsigned indices
 * @FYGBOP_SLICE_PY:        Extract a subrange using Python-style signed indices
 * @FYGBOP_TAKE:            Take the first N elements of a sequence
 * @FYGBOP_DROP:            Drop the first N elements of a sequence
 * @FYGBOP_FIRST:           Return the first element of a sequence
 * @FYGBOP_LAST:            Return the last element of a sequence
 * @FYGBOP_REST:            Return all but the first element of a sequence
 * @FYGBOP_GET:             Look up a value in a collection by key
 * @FYGBOP_GET_AT:          Look up a value by numeric index
 * @FYGBOP_GET_AT_PATH:     Traverse a nested path of keys/indices
 * @FYGBOP_SET:             Set (update) a key in a mapping
 * @FYGBOP_SET_AT:          Set a value at a numeric index in a sequence
 * @FYGBOP_SET_AT_PATH:     Set a value at a nested path (creating nodes as needed)
 * @FYGBOP_PARSE:           Parse YAML/JSON text into a generic value
 * @FYGBOP_EMIT:            Emit a generic value as YAML/JSON text
 * @FYGBOP_CONVERT:         Convert a generic value to a different type
 */
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
	FYGBOP_REDUCE,
	FYGBOP_SLICE,
	FYGBOP_SLICE_PY,
	FYGBOP_TAKE,
	FYGBOP_DROP,
	FYGBOP_FIRST,
	FYGBOP_LAST,
	FYGBOP_REST,
	FYGBOP_GET,
	FYGBOP_GET_AT,
	FYGBOP_GET_AT_PATH,
	FYGBOP_SET,
	FYGBOP_SET_AT,
	FYGBOP_SET_AT_PATH,
	FYGBOP_PARSE,
	FYGBOP_EMIT,
	FYGBOP_CONVERT,
};
/* FYGBOP_COUNT - Total number of generic builder opcodes */
#define FYGBOP_COUNT	(FYGBOP_CONVERT + 1)

/**
 * typedef fy_generic_filter_pred_fn - Predicate function for filter operations.
 *
 * @gb: The active generic builder (or NULL for stack-based operations)
 * @v:  The element being tested
 *
 * Returns:
 * true to keep the element, false to discard it.
 */
typedef bool (*fy_generic_filter_pred_fn)(struct fy_generic_builder *gb, fy_generic v);

/**
 * typedef fy_generic_map_xform_fn - Transform function for map operations.
 *
 * @gb: The active generic builder
 * @v:  The element to transform
 *
 * Returns:
 * The transformed fy_generic value.
 */
typedef fy_generic (*fy_generic_map_xform_fn)(struct fy_generic_builder *gb, fy_generic v);

/**
 * typedef fy_generic_reducer_fn - Reducer function for fold/reduce operations.
 *
 * @gb:  The active generic builder
 * @acc: The running accumulator value
 * @v:   The current element
 *
 * Returns:
 * The new accumulator value.
 */
typedef fy_generic (*fy_generic_reducer_fn)(struct fy_generic_builder *gb, fy_generic acc, fy_generic v);

/* Block variants of the above — available only when compiled with -fblocks (Clang) */
#if defined(__BLOCKS__)
/* fy_generic_filter_pred_block - Block (closure) variant of fy_generic_filter_pred_fn */
typedef bool (^fy_generic_filter_pred_block)(struct fy_generic_builder *gb, fy_generic v);
/* fy_generic_map_xform_block - Block (closure) variant of fy_generic_map_xform_fn */
typedef fy_generic (^fy_generic_map_xform_block)(struct fy_generic_builder *gb, fy_generic v);
/* fy_generic_reducer_block - Block (closure) variant of fy_generic_reducer_fn */
typedef fy_generic (^fy_generic_reducer_block)(struct fy_generic_builder *gb, fy_generic acc, fy_generic v);
#endif

/**
 * struct fy_op_common_args - Arguments common to all collection operations.
 *
 * Embedded as the first member in every fy_op_*_args struct so that
 * fy_generic_op_args() can read the item array uniformly regardless of
 * which opcode is used.
 *
 * Used by FYGBOP_CREATE_SEQ, FYGBOP_CREATE_MAP, FYGBOP_APPEND,
 * FYGBOP_ASSOC, FYGBOP_DISASSOC, FYGBOP_CONTAINS, FYGBOP_CONCAT,
 * FYGBOP_REVERSE, FYGBOP_MERGE, FYGBOP_UNIQUE, and FYGBOP_SET.
 *
 * @count: Number of elements in @items (x2 for mapping pair arrays)
 * @items: Pointer to the element (or key/value pair) array
 * @tp:    Optional thread pool for parallel operations (NULL = single-threaded)
 */
struct fy_op_common_args {
	size_t count;			/* x2 for map */
	const fy_generic *items;
	struct fy_thread_pool *tp;
};

/**
 * struct fy_op_create_scalar_args - Arguments for scalar creation operations.
 *
 * Used by FYGBOP_CREATE_BOOL, FYGBOP_CREATE_INT, FYGBOP_CREATE_FLT,
 * and FYGBOP_CREATE_STR.
 *
 * @common: Embedded common args (count/items unused for scalars)
 * @bval:   Boolean value (FYGBOP_CREATE_BOOL)
 * @fval:   Double value (FYGBOP_CREATE_FLT)
 * @ival:   Decorated integer value (FYGBOP_CREATE_INT)
 * @sval:   Sized string value (FYGBOP_CREATE_STR)
 */
struct fy_op_create_scalar_args {
	struct fy_op_common_args common;
	union {
		bool bval;
		double fval;
		fy_generic_decorated_int ival;
		fy_generic_sized_string sval;
	};
};

/**
 * struct fy_op_sort_args - Arguments for FYGBOP_SORT.
 *
 * @common: Embedded common args
 * @cmp_fn: Comparator returning negative/zero/positive; NULL uses default ordering
 */
struct fy_op_sort_args {
	struct fy_op_common_args common;
	int (*cmp_fn)(fy_generic a, fy_generic b);
};

/**
 * struct fy_op_insert_replace_get_set_at_args - Arguments for index-based operations.
 *
 * Used by FYGBOP_INSERT, FYGBOP_REPLACE, FYGBOP_GET_AT, and FYGBOP_SET_AT.
 *
 * @common: Embedded common args (items to insert/replace; unused for GET_AT)
 * @idx:    Zero-based target index
 */
struct fy_op_insert_replace_get_set_at_args {
	struct fy_op_common_args common;
	size_t idx;
};

/**
 * struct fy_op_keys_values_items_args - Arguments for key/value extraction operations.
 *
 * Used by FYGBOP_KEYS, FYGBOP_VALUES, and FYGBOP_ITEMS.  No extra fields
 * beyond the common args are needed.
 *
 * @common: Embedded common args
 */
struct fy_op_keys_values_items_args {
	struct fy_op_common_args common;
};

/**
 * struct fy_op_filter_args - Arguments for FYGBOP_FILTER.
 *
 * @common: Embedded common args
 * @fn:     Filter predicate function pointer
 * @blk:    Filter predicate block (Clang __BLOCKS__ only)
 */
struct fy_op_filter_args {
	struct fy_op_common_args common;
	union {
		fy_generic_filter_pred_fn fn;
#if defined(__BLOCKS__)
		fy_generic_filter_pred_block blk;
#endif
	};
};

/**
 * struct fy_op_map_args - Arguments for FYGBOP_MAP.
 *
 * @common: Embedded common args
 * @fn:     Transform function pointer
 * @blk:    Transform block (Clang __BLOCKS__ only)
 */
struct fy_op_map_args {
	struct fy_op_common_args common;
	union {
		fy_generic_map_xform_fn fn;
#if defined(__BLOCKS__)
		fy_generic_map_xform_block blk;
#endif
	};
};

/**
 * struct fy_op_reduce_args - Arguments for FYGBOP_REDUCE.
 *
 * @common: Embedded common args
 * @fn:     Reducer function pointer
 * @blk:    Reducer block (Clang __BLOCKS__ only)
 * @acc:    Initial accumulator value
 */
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

/**
 * struct fy_op_filter_map_reduce_common - Type-erased common layout for filter/map/reduce.
 *
 * Used internally so that the dispatcher can access the function pointer
 * uniformly before casting it to the appropriate typed function/block.
 *
 * @common: Embedded common args
 * @fn:     Type-erased function pointer
 */
struct fy_op_filter_map_reduce_common {
	struct fy_op_common_args common;
	union {
		void (*fn)(void);
		/* private: */
#if defined(__BLOCKS__)
		void (^blk)(void);
#endif
		/* public: */
	};
};

/* FYOPPF_INPUT_TYPE_SHIFT - Bit position of the input-type field in fy_op_parse_flags */
#define FYOPPF_INPUT_TYPE_SHIFT	4
/* FYOPPF_INPUT_TYPE_MASK - Mask covering the 4-bit input-type field */
#define FYOPPF_INPUT_TYPE_MASK	((1U << 4) - 1)
/* FYOPPF_INPUT_TYPE() - Build input-type bits from an integer selector */
#define FYOPPF_INPUT_TYPE(x)	(((unsigned int)(x) & FYOPPF_INPUT_TYPE_MASK) << FYOPPF_INPUT_TYPE_SHIFT)

/* FYOPPF_MODE_SHIFT - Bit position of the parser-mode field in fy_op_parse_flags */
#define FYOPPF_MODE_SHIFT	8
/* FYOPPF_MODE_MASK - Mask covering the 5-bit parser-mode field */
#define FYOPPF_MODE_MASK	((1U << 5) - 1)
/* FYOPPF_MODE() - Build parser-mode bits from an integer selector */
#define FYOPPF_MODE(x)		(((unsigned int)(x) & FYOPPF_MODE_MASK) << FYOPPF_MODE_SHIFT)

/**
 * enum fy_op_parse_flags - Flags for FYGBOP_PARSE operations.
 *
 * Control how input is located and how the YAML parser behaves during a
 * fy_parse() / fy_local_parse() / fy_gb_parse() call.
 *
 * @FYOPPF_DISABLE_DIRECTORY:     Do not include a document-state directory in the result
 * @FYOPPF_MULTI_DOCUMENT:        Allow multiple YAML documents in the input
 * @FYOPPF_TRACE:                 Enable parser trace output for debugging
 * @FYOPPF_DONT_RESOLVE:          Skip tag/anchor resolution (used by the YAML test-suite)
 * @FYOPPF_INPUT_TYPE_STRING:     Input data is a NUL-terminated or sized string
 * @FYOPPF_INPUT_TYPE_FILENAME:   Input data is a filename (const char \*)
 * @FYOPPF_INPUT_TYPE_INT_FD:     Input data is a file descriptor (int cast to void \*)
 * @FYOPPF_INPUT_TYPE_STDIN:      Read input from stdin (input_data ignored)
 * @FYOPPF_MODE_AUTO:             Auto-detect YAML version / JSON from content
 * @FYOPPF_MODE_YAML_1_1:         Force YAML 1.1 parsing rules
 * @FYOPPF_MODE_YAML_1_2:         Force YAML 1.2 parsing rules
 * @FYOPPF_MODE_YAML_1_3:         Force YAML 1.3 parsing rules
 * @FYOPPF_MODE_JSON:             Force JSON parsing rules
 * @FYOPPF_MODE_YAML_1_1_PYYAML: Force YAML 1.1 PyYAML-compatible parsing rules
 * @FYOPPF_COLLECT_DIAG:          Collect diagnostic messages into the result
 * @FYOPPF_KEEP_COMMENTS:         Preserve comments in the parsed representation
 * @FYOPPF_CREATE_MARKERS:        Attach position markers to parsed nodes
 * @FYOPPF_KEEP_STYLE:            Preserve original scalar/collection style information
 * @FYOPPF_KEEP_FAILSAFE_STR:     Keep failsafe-schema plain string tags
 */
enum fy_op_parse_flags {
	FYOPPF_DISABLE_DIRECTORY	= FY_BIT(0),
	FYOPPF_MULTI_DOCUMENT		= FY_BIT(1),
	FYOPPF_TRACE			= FY_BIT(2),
	FYOPPF_DONT_RESOLVE		= FY_BIT(3),	// special for testsuite
	FYOPPF_INPUT_TYPE_STRING	= FYOPPF_INPUT_TYPE(0),
	FYOPPF_INPUT_TYPE_FILENAME	= FYOPPF_INPUT_TYPE(1),
	FYOPPF_INPUT_TYPE_INT_FD	= FYOPPF_INPUT_TYPE(2),
	FYOPPF_INPUT_TYPE_STDIN		= FYOPPF_INPUT_TYPE(3),
	FYOPPF_MODE_AUTO		= FYOPPF_MODE(0),
	FYOPPF_MODE_YAML_1_1		= FYOPPF_MODE(1),
	FYOPPF_MODE_YAML_1_2		= FYOPPF_MODE(2),
	FYOPPF_MODE_YAML_1_3		= FYOPPF_MODE(3),
	FYOPPF_MODE_JSON		= FYOPPF_MODE(4),
	FYOPPF_MODE_YAML_1_1_PYYAML	= FYOPPF_MODE(5),
	FYOPPF_COLLECT_DIAG		= FY_BIT(14),
	FYOPPF_KEEP_COMMENTS		= FY_BIT(15),
	FYOPPF_CREATE_MARKERS		= FY_BIT(16),
	FYOPPF_KEEP_STYLE		= FY_BIT(17),
	FYOPPF_KEEP_FAILSAFE_STR	= FY_BIT(18),
};

/* FYOPPF_DEFAULT - Recommended default parse flags (disables directory) */
#define FYOPPF_DEFAULT		FYOPPF_DISABLE_DIRECTORY

/**
 * struct fy_op_parse_args - Arguments for FYGBOP_PARSE.
 *
 * @common:     Embedded common args (items may hold a schema override sequence)
 * @flags:      Parse configuration flags (input type, mode, options)
 * @input_data: Input source; interpretation depends on FYOPPF_INPUT_TYPE_*:
 *              STRING: const char \*, FILENAME: const char \*,
 *              INT_FD: (void \*)(uintptr_t)fd, STDIN: ignored
 */
struct fy_op_parse_args {
	struct fy_op_common_args common;
	enum fy_op_parse_flags flags;
	void *input_data;
};

/* FYOPEF_OUTPUT_TYPE_SHIFT - Bit position of the output-type field in fy_op_emit_flags */
#define FYOPEF_OUTPUT_TYPE_SHIFT	6
/* FYOPEF_OUTPUT_TYPE_MASK - Mask covering the 4-bit output-type field */
#define FYOPEF_OUTPUT_TYPE_MASK	((1U << 4) - 1)
/* FYOPEF_OUTPUT_TYPE() - Build output-type bits from an integer selector */
#define FYOPEF_OUTPUT_TYPE(x)	(((unsigned int)(x) & FYOPEF_OUTPUT_TYPE_MASK) << FYOPEF_OUTPUT_TYPE_SHIFT)

/* FYOPEF_MODE_SHIFT - Bit position of the emitter-mode field in fy_op_emit_flags */
#define FYOPEF_MODE_SHIFT	10
/* FYOPEF_MODE_MASK - Mask covering the 5-bit emitter-mode field */
#define FYOPEF_MODE_MASK	((1U << 5) - 1)
/* FYOPEF_MODE() - Build emitter-mode bits from an integer selector */
#define FYOPEF_MODE(x)		(((unsigned int)(x) & FYOPEF_MODE_MASK) << FYOPEF_MODE_SHIFT)

/* FYOPEF_COLOR_SHIFT - Bit position of the color-mode field in fy_op_emit_flags */
#define FYOPEF_COLOR_SHIFT	15
/* FYOPEF_COLOR_MASK - Mask covering the 2-bit color-mode field */
#define FYOPEF_COLOR_MASK	((1U << 2) - 1)
/* FYOPEF_COLOR() - Build color-mode bits from an integer selector */
#define FYOPEF_COLOR(x)		(((unsigned int)(x) & FYOPEF_COLOR_MASK) << FYOPEF_COLOR_SHIFT)

/* FYOPEF_INDENT_SHIFT - Bit position of the indent-level field in fy_op_emit_flags */
#define FYOPEF_INDENT_SHIFT	17
/* FYOPEF_INDENT_MASK - Mask covering the 3-bit indent-level field */
#define FYOPEF_INDENT_MASK	((1U << 3) - 1)
/* FYOPEF_INDENT() - Build indent-level bits from an integer selector */
#define FYOPEF_INDENT(x)		(((unsigned int)(x) & FYOPEF_INDENT_MASK) << FYOPEF_INDENT_SHIFT)

/* FYOPEF_WIDTH_SHIFT - Bit position of the line-width field in fy_op_emit_flags */
#define FYOPEF_WIDTH_SHIFT	20
/* FYOPEF_WIDTH_MASK - Mask covering the 2-bit line-width field */
#define FYOPEF_WIDTH_MASK	((1U << 2) - 1)
/* FYOPEF_WIDTH() - Build line-width bits from an integer selector */
#define FYOPEF_WIDTH(x)		(((unsigned int)(x) & FYOPEF_WIDTH_MASK) << FYOPEF_WIDTH_SHIFT)

/* FYOPEF_STYLE_SHIFT - Bit position of the output-style field in fy_op_emit_flags */
#define FYOPEF_STYLE_SHIFT	22
/* FYOPEF_STYLE_MASK - Mask covering the 3-bit output-style field */
#define FYOPEF_STYLE_MASK	((1U << 3) - 1)
/* FYOPEF_STYLE() - Build output-style bits from an integer selector */
#define FYOPEF_STYLE(x)		(((unsigned int)(x) & FYOPEF_STYLE_MASK) << FYOPEF_STYLE_SHIFT)

/**
 * enum fy_op_emit_flags - Flags for FYGBOP_EMIT operations.
 *
 * Control output destination, YAML/JSON version, indentation, line width,
 * and formatting style for fy_emit() / fy_local_emit() / fy_gb_emit() calls.
 *
 * @FYOPEF_DISABLE_DIRECTORY:      Do not include a document-state directory in the output
 * @FYOPEF_MULTI_DOCUMENT:         Emit multiple YAML documents (stream)
 * @FYOPEF_TRACE:                  Enable emitter trace output for debugging
 * @FYOPEF_NO_ENDING_NEWLINE:      Do not append a trailing newline
 * @FYOPEF_WIDTH_ADAPT_TO_TERMINAL: Adapt line width to the current terminal width
 * @FYOPEF_OUTPUT_COMMENTS:        Include inline comments in the output
 * @FYOPEF_OUTPUT_TYPE_STRING:     Emit to a NUL-terminated string (output_data = char \*\*)
 * @FYOPEF_OUTPUT_TYPE_FILENAME:   Emit to a file by name (output_data = const char \*)
 * @FYOPEF_OUTPUT_TYPE_INT_FD:     Emit to a file descriptor (output_data = (void \*)fd)
 * @FYOPEF_OUTPUT_TYPE_STDOUT:     Emit to stdout
 * @FYOPEF_OUTPUT_TYPE_STDERR:     Emit to stderr
 * @FYOPEF_MODE_AUTO:              Auto-select output format from schema
 * @FYOPEF_MODE_YAML_1_1:          Emit YAML 1.1
 * @FYOPEF_MODE_YAML_1_2:          Emit YAML 1.2
 * @FYOPEF_MODE_YAML_1_3:          Emit YAML 1.3
 * @FYOPEF_MODE_JSON:              Emit JSON
 * @FYOPEF_MODE_YAML_1_1_PYYAML:   Emit YAML 1.1 PyYAML-compatible
 * @FYOPEF_COLOR_AUTO:             Auto-detect terminal color support
 * @FYOPEF_COLOR_NONE:             Disable color output
 * @FYOPEF_COLOR_FORCE:            Force color output
 * @FYOPEF_INDENT_DEFAULT:         Use the library default indent (usually 2)
 * @FYOPEF_INDENT_1:               1-space indent
 * @FYOPEF_INDENT_2:               2-space indent
 * @FYOPEF_INDENT_3:               3-space indent
 * @FYOPEF_INDENT_4:               4-space indent
 * @FYOPEF_INDENT_6:               6-space indent
 * @FYOPEF_INDENT_8:               8-space indent (maximum)
 * @FYOPEF_WIDTH_DEFAULT:          Use default line width (typically infinite)
 * @FYOPEF_WIDTH_80:               Wrap at 80 columns
 * @FYOPEF_WIDTH_132:              Wrap at 132 columns
 * @FYOPEF_WIDTH_INF:              Infinite line width (no wrapping)
 * @FYOPEF_STYLE_DEFAULT:          Use the library default style
 * @FYOPEF_STYLE_BLOCK:            Block style
 * @FYOPEF_STYLE_FLOW:             Flow style
 * @FYOPEF_STYLE_PRETTY:           Pretty-printed style
 * @FYOPEF_STYLE_COMPACT:          Compact (minimal whitespace) style
 * @FYOPEF_STYLE_ONELINE:          Single-line style
 */
enum fy_op_emit_flags {
	FYOPEF_DISABLE_DIRECTORY	= FY_BIT(0),
	FYOPEF_MULTI_DOCUMENT		= FY_BIT(1),
	FYOPEF_TRACE			= FY_BIT(2),
	FYOPEF_NO_ENDING_NEWLINE	= FY_BIT(3),
	FYOPEF_WIDTH_ADAPT_TO_TERMINAL	= FY_BIT(4),
	FYOPEF_OUTPUT_COMMENTS		= FY_BIT(5),
	FYOPEF_OUTPUT_TYPE_STRING	= FYOPEF_OUTPUT_TYPE(0),
	FYOPEF_OUTPUT_TYPE_FILENAME	= FYOPEF_OUTPUT_TYPE(1),
	FYOPEF_OUTPUT_TYPE_INT_FD	= FYOPEF_OUTPUT_TYPE(2),
	FYOPEF_OUTPUT_TYPE_STDOUT	= FYOPEF_OUTPUT_TYPE(3),
	FYOPEF_OUTPUT_TYPE_STDERR	= FYOPEF_OUTPUT_TYPE(4),
	FYOPEF_MODE_AUTO		= FYOPEF_MODE(0),
	FYOPEF_MODE_YAML_1_1		= FYOPEF_MODE(1),
	FYOPEF_MODE_YAML_1_2		= FYOPEF_MODE(2),
	FYOPEF_MODE_YAML_1_3		= FYOPEF_MODE(3),
	FYOPEF_MODE_JSON		= FYOPEF_MODE(4),
	FYOPEF_MODE_YAML_1_1_PYYAML	= FYOPEF_MODE(5),
	FYOPEF_COLOR_AUTO		= FYOPEF_COLOR(0),
	FYOPEF_COLOR_NONE		= FYOPEF_COLOR(1),
	FYOPEF_COLOR_FORCE		= FYOPEF_COLOR(2),
	FYOPEF_INDENT_DEFAULT		= FYOPEF_INDENT(0),	// what ever is the default
	FYOPEF_INDENT_1			= FYOPEF_INDENT(1),
	FYOPEF_INDENT_2			= FYOPEF_INDENT(2),
	FYOPEF_INDENT_3			= FYOPEF_INDENT(3),
	FYOPEF_INDENT_4			= FYOPEF_INDENT(4),
	FYOPEF_INDENT_6			= FYOPEF_INDENT(5),
	FYOPEF_INDENT_8			= FYOPEF_INDENT(6),	// no more
	FYOPEF_WIDTH_DEFAULT		= FYOPEF_WIDTH(0),	// what ever is the default (usually infinite)
	FYOPEF_WIDTH_80			= FYOPEF_WIDTH(1),	// 80
	FYOPEF_WIDTH_132		= FYOPEF_WIDTH(2),	// 132
	FYOPEF_WIDTH_INF		= FYOPEF_WIDTH(3),	// infinite
	FYOPEF_STYLE_DEFAULT		= FYOPEF_STYLE(0),	// what ever is the default
	FYOPEF_STYLE_BLOCK		= FYOPEF_STYLE(1),
	FYOPEF_STYLE_FLOW		= FYOPEF_STYLE(2),
	FYOPEF_STYLE_PRETTY		= FYOPEF_STYLE(3),
	FYOPEF_STYLE_COMPACT		= FYOPEF_STYLE(4),
	FYOPEF_STYLE_ONELINE		= FYOPEF_STYLE(5),
};

/* FYOPEF_DEFAULT - Recommended default emit flags (disables directory) */
#define FYOPEF_DEFAULT		FYOPEF_DISABLE_DIRECTORY

/**
 * struct fy_op_emit_args - Arguments for FYGBOP_EMIT.
 *
 * @common:      Embedded common args (items may hold schema override sequences)
 * @flags:       Emit configuration flags (output type, mode, style, etc.)
 * @output_data: Output destination; interpretation depends on FYOPEF_OUTPUT_TYPE_*:
 *               STRING: char \*\*, FILENAME: const char \*,
 *               INT_FD: (void \*)(uintptr_t)fd, STDOUT/STDERR: ignored
 */
struct fy_op_emit_args {
	struct fy_op_common_args common;
	enum fy_op_emit_flags flags;	/* emitter configuration flags */
	void *output_data;		/* special output data */
};

/**
 * struct fy_op_slice_args - Arguments for FYGBOP_SLICE.
 *
 * @common: Embedded common args
 * @start:  Starting index (inclusive, zero-based)
 * @end:    Ending index (exclusive); use SIZE_MAX for "to the end"
 */
struct fy_op_slice_args {
	struct fy_op_common_args common;
	size_t start;				/* starting index (inclusive) */
	size_t end;				/* ending index (exclusive), or SIZE_MAX for end of sequence */
};

/**
 * struct fy_op_slice_py_args - Arguments for FYGBOP_SLICE_PY.
 *
 * Like fy_op_slice_args but uses Python-style signed indices where negative
 * values count backwards from the end of the sequence.
 *
 * @common: Embedded common args
 * @start:  Starting index (inclusive; negative counts from end)
 * @end:    Ending index (exclusive; negative counts from end)
 */
struct fy_op_slice_py_args {
	struct fy_op_common_args common;
	ssize_t start;				/* starting index (inclusive), negative counts from end */
	ssize_t end;				/* ending index (exclusive), negative counts from end */
};

/**
 * struct fy_op_take_args - Arguments for FYGBOP_TAKE.
 *
 * @common: Embedded common args
 * @n:      Number of elements to take from the start of the sequence
 */
struct fy_op_take_args {
	struct fy_op_common_args common;
	size_t n;				/* number of elements to take from start */
};

/**
 * struct fy_op_drop_args - Arguments for FYGBOP_DROP.
 *
 * @common: Embedded common args
 * @n:      Number of elements to drop from the start of the sequence
 */
struct fy_op_drop_args {
	struct fy_op_common_args common;
	size_t n;				/* number of elements to drop from start */
};

/**
 * struct fy_op_convert_args - Arguments for FYGBOP_CONVERT.
 *
 * @common: Embedded common args
 * @type:   Target generic type to convert the input value to
 */
struct fy_op_convert_args {
	struct fy_op_common_args common;
	enum fy_generic_type type;
};

/* FYGBOP_FIRST, FYGBOP_LAST, FYGBOP_REST - no additional args needed */

/**
 * enum fy_gb_op_flags - Combined opcode + modifier flags for fy_generic_op().
 *
 * The low 8 bits select the operation (use FYGBOPF_OP() / FYGBOPF_* opcode
 * constants).  The upper bits are modifier flags that refine behaviour.
 *
 * Opcode values (low 8 bits via FYGBOPF_OP()):
 * @FYGBOPF_CREATE_SEQ:      Create a sequence
 * @FYGBOPF_CREATE_MAP:      Create a mapping
 * @FYGBOPF_INSERT:          Insert into a sequence at an index
 * @FYGBOPF_REPLACE:         Replace elements at an index
 * @FYGBOPF_APPEND:          Append to a collection
 * @FYGBOPF_ASSOC:           Associate key/value pairs into a mapping
 * @FYGBOPF_DISASSOC:        Remove keys from a mapping
 * @FYGBOPF_KEYS:            Extract mapping keys as a sequence
 * @FYGBOPF_VALUES:          Extract mapping values as a sequence
 * @FYGBOPF_ITEMS:           Extract mapping pairs as a sequence
 * @FYGBOPF_CONTAINS:        Test for element membership
 * @FYGBOPF_CONCAT:          Concatenate collections
 * @FYGBOPF_REVERSE:         Reverse a sequence
 * @FYGBOPF_MERGE:           Deep-merge mappings
 * @FYGBOPF_UNIQUE:          Remove duplicates from a sequence
 * @FYGBOPF_SORT:            Sort a sequence
 * @FYGBOPF_FILTER:          Filter elements by predicate
 * @FYGBOPF_MAP:             Transform each element
 * @FYGBOPF_REDUCE:          Fold a sequence into an accumulator
 * @FYGBOPF_SLICE:           Unsigned index slice
 * @FYGBOPF_SLICE_PY:        Python-style signed slice
 * @FYGBOPF_TAKE:            Take first N elements
 * @FYGBOPF_DROP:            Drop first N elements
 * @FYGBOPF_FIRST:           Return first element
 * @FYGBOPF_LAST:            Return last element
 * @FYGBOPF_REST:            Return all but first element
 * @FYGBOPF_GET:             Lookup by key
 * @FYGBOPF_GET_AT:          Lookup by index
 * @FYGBOPF_GET_AT_PATH:     Traverse nested path of keys/indices
 * @FYGBOPF_SET:             Set a key in a mapping
 * @FYGBOPF_SET_AT:          Set element at an index
 * @FYGBOPF_SET_AT_PATH:     Set value at nested path (creating intermediate nodes)
 * @FYGBOPF_PARSE:           Parse YAML/JSON input
 * @FYGBOPF_EMIT:            Emit generic value as YAML/JSON
 * @FYGBOPF_CONVERT:         Convert to a different type
 * @FYGBOPF_DONT_INTERNALIZE: Skip copying items into the builder arena
 * @FYGBOPF_DEEP_VALIDATE:    Recursively validate all elements
 * @FYGBOPF_NO_CHECKS:        Skip all input validity checks
 * @FYGBOPF_PARALLEL:         Execute in parallel using a thread pool
 * @FYGBOPF_MAP_ITEM_COUNT:   Interpret @count as number of items, not pairs
 * @FYGBOPF_BLOCK_FN:         The function/callback is a Clang block (^)
 * @FYGBOPF_CREATE_PATH:      Create intermediate path nodes (like mkdir -p)
 * @FYGBOPF_UNSIGNED:         Integer scalar is unsigned (shares bit 23 with CREATE_PATH)
 */
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
	FYGBOPF_REDUCE		= FYGBOPF_OP(FYGBOP_REDUCE),
	FYGBOPF_SLICE		= FYGBOPF_OP(FYGBOP_SLICE),
	FYGBOPF_SLICE_PY	= FYGBOPF_OP(FYGBOP_SLICE_PY),
	FYGBOPF_TAKE		= FYGBOPF_OP(FYGBOP_TAKE),
	FYGBOPF_DROP		= FYGBOPF_OP(FYGBOP_DROP),
	FYGBOPF_FIRST		= FYGBOPF_OP(FYGBOP_FIRST),
	FYGBOPF_LAST		= FYGBOPF_OP(FYGBOP_LAST),
	FYGBOPF_REST		= FYGBOPF_OP(FYGBOP_REST),
	FYGBOPF_GET		= FYGBOPF_OP(FYGBOP_GET),
	FYGBOPF_GET_AT		= FYGBOPF_OP(FYGBOP_GET_AT),
	FYGBOPF_GET_AT_PATH	= FYGBOPF_OP(FYGBOP_GET_AT_PATH),
	FYGBOPF_SET		= FYGBOPF_OP(FYGBOP_SET),
	FYGBOPF_SET_AT		= FYGBOPF_OP(FYGBOP_SET_AT),
	FYGBOPF_SET_AT_PATH	= FYGBOPF_OP(FYGBOP_SET_AT_PATH),
	FYGBOPF_PARSE		= FYGBOPF_OP(FYGBOP_PARSE),
	FYGBOPF_EMIT		= FYGBOPF_OP(FYGBOP_EMIT),
	FYGBOPF_CONVERT		= FYGBOPF_OP(FYGBOP_CONVERT),
	FYGBOPF_DONT_INTERNALIZE= FY_BIT(16),			// do not internalize items
	FYGBOPF_DEEP_VALIDATE	= FY_BIT(17),			// perform deep validation
	FYGBOPF_NO_CHECKS	= FY_BIT(18),			// do not perform any checks on the items
	FYGBOPF_PARALLEL	= FY_BIT(19),			// perform in parallel
	FYGBOPF_MAP_ITEM_COUNT	= FY_BIT(20),			// the count is items, not pairs for mappings
	FYGBOPF_BLOCK_FN	= FY_BIT(21),			// the function is a block
	FYGBOPF_CREATE_PATH	= FY_BIT(23),			// create intermediate paths like mkdir -p
	FYGBOPF_UNSIGNED	= FY_BIT(23),			// int scalar created is unsigned (note same as CREATE_PATH)
};

/*
 * struct fy_generic_op_args - Union of all operation argument structs.
 *
 * Pass a pointer to this union as the @args argument of fy_generic_op_args().
 * Fill the appropriate member based on the opcode selected in @flags.
 *
 * @common:                  Valid for all operations — holds count/items/tp
 * @scalar:                  Scalar creation (FYGBOP_CREATE_BOOL/INT/FLT/STR)
 * @sort:                    Sort with comparator (FYGBOP_SORT)
 * @insert_replace_get_set_at: Index-based insert/replace/get/set (FYGBOP_INSERT etc.)
 * @keys_value_items:        Key/value/item extraction (FYGBOP_KEYS etc.)
 * @filter:                  Filter with predicate (FYGBOP_FILTER)
 * @map_filter:              Transform with function (FYGBOP_MAP)
 * @reduce:                  Fold with accumulator (FYGBOP_REDUCE)
 * @filter_map_reduce_common: Type-erased common layout for filter/map/reduce
 * @slice:                   Unsigned index slice (FYGBOP_SLICE)
 * @slice_py:                Python-style slice (FYGBOP_SLICE_PY)
 * @take:                    Take first N elements (FYGBOP_TAKE)
 * @drop:                    Drop first N elements (FYGBOP_DROP)
 * @parse:                   Parse input (FYGBOP_PARSE)
 * @emit:                    Emit output (FYGBOP_EMIT)
 * @convert:                 Type conversion (FYGBOP_CONVERT)
 */
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
		struct fy_op_slice_args slice;
		struct fy_op_slice_py_args slice_py;
		struct fy_op_take_args take;
		struct fy_op_drop_args drop;
		struct fy_op_parse_args parse;
		struct fy_op_emit_args emit;
		struct fy_op_convert_args convert;
	};
};

/**
 * fy_generic_op_args() - Execute a generic operation using a pre-filled args struct.
 *
 * Lower-level variant of fy_generic_op() for callers that prefer to fill a
 * fy_generic_op_args union explicitly rather than using the variadic interface.
 *
 * @gb:    Builder that owns the result (may be NULL for local/alloca operations)
 * @flags: Opcode (low 8 bits) plus modifier flags
 * @in:    Primary input collection or scalar; use fy_invalid / fy_seq_empty as appropriate
 * @args:  Pointer to the filled fy_generic_op_args union
 *
 * Returns:
 * The operation result, or fy_invalid on error or allocation failure.
 */
fy_generic
fy_generic_op_args(struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		   fy_generic in, const struct fy_generic_op_args *args)
	FY_EXPORT;

/**
 * fy_generic_op() - Execute a generic operation using variadic arguments.
 *
 * Dispatches to the appropriate collection or scalar operation selected by
 * @flags.  The remaining variadic arguments are interpreted according to the
 * opcode; see each FYGBOPF_* constant for the expected argument list.
 *
 * @gb:    Builder that owns the result
 * @flags: Opcode (low 8 bits) plus modifier flags
 * @...:   Operation-specific arguments (count, items, index, function, etc.)
 *
 * Returns:
 * The operation result, or fy_invalid on error or allocation failure.
 */
fy_generic
fy_generic_op(struct fy_generic_builder *gb, enum fy_gb_op_flags flags, ...)
	FY_EXPORT;

/**
 * fy_gb_sequence_create() - Create a sequence generic from an item array.
 *
 * @gb:    Builder that owns the result
 * @count: Number of elements in @items
 * @items: Array of fy_generic elements to include in the sequence
 *
 * Returns:
 * A sequence fy_generic backed by the builder arena, or fy_invalid on error.
 */
static inline fy_generic
fy_gb_sequence_create(struct fy_generic_builder *gb, size_t count, const fy_generic *items)
{
	struct fy_generic_op_args args = {
		.common.count = count,
		.common.items = items,
	};
	return fy_generic_op_args(gb, FYGBOPF_CREATE_SEQ, fy_seq_empty, &args);
}

/**
 * fy_gb_mapping_create() - Create a mapping generic from a key/value pair array.
 *
 * @gb:    Builder that owns the result
 * @count: Number of key/value PAIRS in @pairs (i.e. half the array length)
 * @pairs: Interleaved key/value fy_generic array
 *
 * Returns:
 * A mapping fy_generic backed by the builder arena, or fy_invalid on error.
 */
static inline fy_generic
fy_gb_mapping_create(struct fy_generic_builder *gb, size_t count, const fy_generic *pairs)
{
	struct fy_generic_op_args args = {
		.common.count = count,
		.common.items = pairs,
	};
	return fy_generic_op_args(gb, FYGBOPF_CREATE_MAP, fy_seq_empty, &args);
}

/**
 * fy_gb_indirect_create() - Create an indirect generic wrapping metadata.
 *
 * An indirect value carries an anchor, tag, style, or comment alongside the
 * actual value without changing its logical type.
 *
 * @gb: Builder that owns the result
 * @gi: Pointer to the indirect descriptor to copy into the builder arena
 *
 * Returns:
 * An FYGT_INDIRECT fy_generic, or fy_invalid on error.
 */
fy_generic
fy_gb_indirect_create(struct fy_generic_builder *gb, const fy_generic_indirect *gi)
	FY_EXPORT;

/**
 * fy_gb_alias_create() - Create an alias generic referencing an anchor value.
 *
 * @gb:     Builder that owns the result
 * @anchor: The fy_generic value that serves as the anchor target
 *
 * Returns:
 * An FYGT_ALIAS fy_generic, or fy_invalid on error.
 */
fy_generic
fy_gb_alias_create(struct fy_generic_builder *gb, fy_generic anchor)
	FY_EXPORT;

/**
 * fy_gb_sequence_value() - Build a sequence fy_generic_value from variadic elements using a builder.
 *
 * Encodes each variadic argument via fy_gb_to_generic() (using @_gb for out-of-place
 * storage), then creates a sequence.  With no arguments, returns fy_seq_empty_value.
 *
 * @_gb:  Builder
 * @...:  Zero or more values of any supported C type
 *
 * Returns:
 * fy_generic_value of a sequence, or fy_seq_empty_value if no arguments.
 */
#define fy_gb_sequence_value(_gb, ...) \
	((0 __VA_OPT__(+1)) ? \
		__VA_OPT__(fy_gb_sequence_create((_gb), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GBITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), (_gb), __VA_ARGS__)).v) : \
		fy_seq_empty_value)

/**
 * fy_gb_sequence() - Build a sequence fy_generic from variadic elements using a builder.
 *
 * Like fy_gb_sequence_value() but wraps the result in a fy_generic struct.
 *
 * @_gb: Builder
 * @...: Zero or more values of any supported C type
 *
 * Returns:
 * A sequence fy_generic, or fy_seq_empty if no arguments.
 */
#define fy_gb_sequence(_gb, ...) \
	((fy_generic) { .v = fy_gb_sequence_value((_gb) __VA_OPT__(,) __VA_ARGS__) })

/**
 * fy_gb_mapping_value() - Build a mapping fy_generic_value from variadic key/value pairs using a builder.
 *
 * Arguments must be interleaved key/value pairs.  With no arguments, returns
 * fy_map_empty_value.
 *
 * @_gb: Builder
 * @...: Zero or more key/value pairs of any supported C type
 *
 * Returns:
 * fy_generic_value of a mapping, or fy_map_empty_value if no arguments.
 */
#define fy_gb_mapping_value(_gb, ...) \
	((0 __VA_OPT__(+1)) ? \
		__VA_OPT__(fy_gb_mapping_create((_gb), \
			FY_CPP_VA_COUNT(__VA_ARGS__) / 2, \
			FY_CPP_VA_GBITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), (_gb), __VA_ARGS__)).v) : \
		fy_map_empty_value)

/**
 * fy_gb_mapping() - Build a mapping fy_generic from variadic key/value pairs using a builder.
 *
 * Like fy_gb_mapping_value() but wraps the result in a fy_generic struct.
 *
 * @_gb: Builder
 * @...: Zero or more key/value pairs of any supported C type
 *
 * Returns:
 * A mapping fy_generic, or fy_map_empty if no arguments.
 */
#define fy_gb_mapping(_gb, ...) \
	((fy_generic) { .v = fy_gb_mapping_value((_gb), __VA_ARGS__) })

/**
 * fy_gb_create_scalar_from_text() - Parse a text scalar and create a typed generic.
 *
 * Interprets @text as a YAML scalar value, optionally forcing a specific type.
 * Useful for converting stringified values (e.g. from a config file) into
 * the appropriate fy_generic representation.
 *
 * @gb:         Builder that owns the result
 * @text:       Pointer to the scalar text (not necessarily NUL-terminated)
 * @len:        Length of @text in bytes
 * @force_type: If != FYGT_INVALID, force this type instead of auto-detecting
 *
 * Returns:
 * A fy_generic of the detected (or forced) type, or fy_invalid on error.
 */
fy_generic
fy_gb_create_scalar_from_text(struct fy_generic_builder *gb,
			      const char *text, size_t len, enum fy_generic_type force_type)
	FY_EXPORT;

/**
 * fy_gb_copy_out_of_place() - Deep-copy an out-of-place generic into a builder arena.
 *
 * Recursively copies the value and all referenced data into @gb's arena.
 * This is the slow path; prefer fy_gb_copy() which skips the copy for
 * inplace values.
 *
 * @gb: Destination builder
 * @v:  Out-of-place source value (must not be inplace or invalid)
 *
 * Returns:
 * A new fy_generic backed by @gb, or fy_invalid on allocation failure.
 */
fy_generic
fy_gb_copy_out_of_place(struct fy_generic_builder *gb, fy_generic v)
	FY_EXPORT;

/**
 * fy_gb_copy() - Copy a generic into a builder arena, skipping inplace values.
 *
 * If @v is already stored inplace (no heap pointer), returns it unchanged.
 * Otherwise delegates to fy_gb_copy_out_of_place().
 *
 * @gb: Destination builder
 * @v:  Source value
 *
 * Returns:
 * @v unchanged if inplace, or a deep copy backed by @gb on success, or fy_invalid on error.
 */
static inline fy_generic fy_gb_copy(struct fy_generic_builder *gb, fy_generic v)
{
	if (fy_generic_is_in_place(v))
		return v;

	return fy_gb_copy_out_of_place(gb, v);
}

/**
 * fy_gb_internalize_out_of_place() - Intern an out-of-place generic if it lives outside the builder.
 *
 * Copies @v into @gb's arena only if its pointer does not already fall within
 * the builder's allocated regions.  If the value is already owned by @gb,
 * returns it unchanged.
 *
 * @gb: Target builder
 * @v:  Out-of-place generic to potentially intern
 *
 * Returns:
 * @v (unchanged if already internal) or a copy in @gb, or fy_invalid on error.
 */
fy_generic
fy_gb_internalize_out_of_place(struct fy_generic_builder *gb, fy_generic v)
	FY_EXPORT;

/**
 * fy_gb_internalize() - Intern a generic into a builder, skipping inplace and invalid values.
 *
 * Fast path: returns @v immediately if it is invalid or inplace.
 * Otherwise delegates to fy_gb_internalize_out_of_place().
 *
 * @gb: Target builder
 * @v:  Value to intern
 *
 * Returns:
 * @v unchanged, or an interned copy in @gb, or fy_invalid on error.
 */
static inline fy_generic
fy_gb_internalize(struct fy_generic_builder *gb, fy_generic v)
{
	/* if it's invalid or in place, just return it */
	if (fy_generic_is_invalid(v) || fy_generic_is_in_place(v))
		return v;

	return fy_gb_internalize_out_of_place(gb, v);
}

/**
 * fy_validate_out_of_place() - Validate an out-of-place generic (no builder).
 *
 * Checks structural and type consistency of @v without a builder context.
 * This is the slow path; prefer fy_validate() which skips validation for
 * inplace and invalid values.
 *
 * @v: Out-of-place generic to validate
 *
 * Returns:
 * @v if valid, or fy_invalid if structural errors are found.
 */
fy_generic
fy_validate_out_of_place(fy_generic v)
	FY_EXPORT;

/**
 * fy_validate() - Validate a generic, skipping inplace and invalid values.
 *
 * @v: Generic to validate
 *
 * Returns:
 * @v unchanged if already invalid or inplace; otherwise the result of
 * fy_validate_out_of_place().
 */
static inline fy_generic fy_validate(fy_generic v)
{
	/* if it's invalid or in place, just return it */
	if (fy_generic_is_invalid(v) || fy_generic_is_in_place(v))
		return v;

	return fy_validate_out_of_place(v);
}

/**
 * fy_gb_validate_out_of_place() - Validate an out-of-place generic using a builder context.
 *
 * Like fy_validate_out_of_place() but can use @gb for schema information
 * during validation.
 *
 * @gb: Builder providing schema context
 * @v:  Out-of-place generic to validate
 *
 * Returns:
 * @v if valid, or fy_invalid if errors are found.
 */
fy_generic
fy_gb_validate_out_of_place(struct fy_generic_builder *gb, fy_generic v)
	FY_EXPORT;

/**
 * fy_gb_validate() - Validate a generic using a builder, skipping trivial cases.
 *
 * @gb: Builder providing schema context
 * @v:  Generic to validate
 *
 * Returns:
 * @v if already invalid or inplace; otherwise the result of
 * fy_gb_validate_out_of_place().
 */
static inline fy_generic fy_gb_validate(struct fy_generic_builder *gb, fy_generic v)
{
	/* if it's invalid or in place, just return it */
	if (fy_generic_is_invalid(v) || fy_generic_is_in_place(v))
		return v;

	return fy_gb_validate_out_of_place(gb, v);
}

/**
 * fy_generic_relocate() - Adjust all pointers in a generic after a buffer realloc.
 *
 * When a backing buffer is reallocated (e.g. by mremap), all embedded pointers
 * shift by @d bytes.  This function patches every pointer in @v that falls
 * within [@start, @end) by adding @d.
 *
 * @start: Old buffer start
 * @end:   Old buffer end (exclusive)
 * @v:     Generic value to patch
 * @d:     Delta to add to pointers (new_base - old_base)
 *
 * Returns:
 * The relocated fy_generic with updated pointers.
 */
fy_generic
fy_generic_relocate(void *start, void *end, fy_generic v, ptrdiff_t d)
	FY_EXPORT;

/**
 * fy_gb_get_schema() - Return the schema currently active in a builder.
 *
 * @gb: Builder to query
 *
 * Returns:
 * The enum fy_generic_schema value configured for @gb.
 */
enum fy_generic_schema
fy_gb_get_schema(struct fy_generic_builder *gb)
	FY_EXPORT;

/**
 * fy_gb_set_schema() - Set the schema for a builder.
 *
 * @gb:     Builder to update
 * @schema: New schema to use for subsequent operations
 */
void
fy_gb_set_schema(struct fy_generic_builder *gb, enum fy_generic_schema schema)
	FY_EXPORT;

/**
 * fy_gb_set_schema_from_parser_mode() - Derive and set the builder schema from a parser mode.
 *
 * Converts a fy_parser_mode value to the closest fy_generic_schema and
 * calls fy_gb_set_schema().
 *
 * @gb:          Builder to update
 * @parser_mode: Parser mode whose schema to adopt
 *
 * Returns:
 * 0 on success, -1 if @parser_mode has no corresponding schema.
 */
int
fy_gb_set_schema_from_parser_mode(struct fy_generic_builder *gb, enum fy_parser_mode parser_mode)
	FY_EXPORT;

/**
 * fy_generic_dump_primitive() - Dump a fy_generic value to a FILE stream for debugging.
 *
 * Recursively prints @vv with indentation @level to @fp.  Intended for
 * development and debugging; output format is not stable.
 *
 * @fp:    Destination FILE stream (e.g. stderr)
 * @level: Current indentation level (pass 0 for the top-level call)
 * @vv:    Generic value to dump
 */
void
fy_generic_dump_primitive(FILE *fp, int level, fy_generic vv)
	FY_EXPORT;

/* Short convenience aliases for common generic operations.
 * Each macro is a thin wrapper around the underlying fy_generic_* function. */

/* fy_len() - Return the element/byte count of a collection or string */
#define fy_len(_colv) \
	(fy_generic_len((_colv)))

/* fy_get_default() - Look up @_key in @_colv, returning @_dv if not found */
#define fy_get_default(_colv, _key, _dv) \
	(fy_generic_get_default((_colv), (_key), (_dv)))

/* fy_get_typed() - Look up @_key, returning the type-derived default for @_type if not found */
#define fy_get_typed(_colv, _key, _type) \
	(fy_generic_get_default((_colv), (_key), fy_generic_get_type_default(_type)))

/* fy_get() - Alias for fy_get_default() */
#define fy_get(_colv, _key, _dv) \
	(fy_get_default((_colv), (_key), (_dv)))

/* fy_get_at_default() - Look up element at index @_idx, returning @_dv if out of range */
#define fy_get_at_default(_colv, _idx, _dv) \
	(fy_generic_get_at_default((_colv), (_idx), (_dv)))

/* fy_get_at_typed() - Look up element at index @_idx using type-derived default */
#define fy_get_at_typed(_colv, _idx, _type) \
	(fy_generic_get_at_default((_colv), (_idx), fy_generic_get_type_default(_type)))

/* fy_get_at() - Alias for fy_get_at_default() */
#define fy_get_at(_colv, _idx, _dv) \
	(fy_get_at_default((_colv), (_idx), (_dv)))

/* fy_get_key_at_default() - Return the KEY at index @_idx, returning @_dv if out of range */
#define fy_get_key_at_default(_colv, _idx, _dv) \
	(fy_generic_get_key_at_default((_colv), (_idx), (_dv)))

/* fy_get_key_at_typed() - Return the KEY at index @_idx using type-derived default */
#define fy_get_key_at_typed(_colv, _idx, _type) \
	(fy_generic_get_key_at_default((_colv), (_idx), fy_generic_get_type_default(_type)))

/* fy_get_key_at() - Alias for fy_get_key_at_default() */
#define fy_get_key_at(_colv, _idx, _dv) \
	(fy_get_key_at_default((_colv), (_idx), (_dv)))

/* fy_cast_default() - Cast @_v to the type of @_dv, returning @_dv on type mismatch */
#define fy_cast_default(_v, _dv) \
	(fy_generic_cast_default((_v), (_dv)))

/* fy_cast_typed() - Cast @_v to @_type, returning the type-zero value on mismatch */
#define fy_cast_typed(_v, _type) \
	(fy_generic_cast((_v), (_type)))

/* fy_cast() - Alias for fy_cast_default() */
#define fy_cast(_v, _dv) \
	(fy_cast_default((_v), (_dv)))

/* fy_castp_default() - Cast a fy_generic pointer @_v, returning @_dv on mismatch */
#define fy_castp_default(_v, _dv) \
	(fy_genericp_cast_default((_v), (_dv)))

/* fy_castp_typed() - Cast a fy_generic pointer @_v to @_type */
#define fy_castp_typed(_v, _type) \
	(fy_genericp_cast_typed((_v), (_type)))

/* fy_castp() - Alias for fy_castp_default() */
#define fy_castp(_v, _dv) \
	(fy_genericp_cast_default((_v), (_dv)))

/* fy_sequence_value_helper() - Internal dispatch: builder path or local alloca path */
#define fy_sequence_value_helper(_maybe_gb, ...) \
	(_Generic((_maybe_gb), \
		  struct fy_generic_builder *: ({ fy_gb_sequence_value(fy_gb_or_NULL(_maybe_gb), ##__VA_ARGS__); }), \
		  default: (fy_local_sequence_value((_maybe_gb), ##__VA_ARGS__))))

/**
 * fy_sequence_value() - Build a sequence fy_generic_value from variadic elements.
 *
 * The first argument may optionally be a fy_generic_builder* (selects builder
 * path) or a plain value (selects local/alloca path).  With no arguments,
 * returns fy_seq_empty_value.
 *
 * @...: Optional builder followed by zero or more element values
 *
 * Returns:
 * fy_generic_value of a sequence, or fy_seq_empty_value if empty.
 */
#define fy_sequence_value(...) \
	((0 __VA_OPT__(+1)) ? \
		__VA_OPT__(fy_sequence_value_helper(__VA_ARGS__)) : \
		fy_seq_empty_value)

/**
 * fy_sequence() - Build a sequence fy_generic from variadic elements.
 *
 * Wraps fy_sequence_value() in a fy_generic struct.
 *
 * @...: Optional builder followed by zero or more element values
 *
 * Returns:
 * A sequence fy_generic, or fy_seq_empty if empty.
 */
#define fy_sequence(...) \
	((fy_generic) { .v = fy_sequence_value(__VA_ARGS__) })

/* fy_mapping_value_helper() - Internal dispatch: builder path or local alloca path */
#define fy_mapping_value_helper(_maybe_gb, ...) \
	(_Generic((_maybe_gb), \
		  struct fy_generic_builder *: ({ fy_gb_mapping_value(fy_gb_or_NULL(_maybe_gb), ##__VA_ARGS__); }), \
		  default: (fy_local_mapping_value((_maybe_gb), ##__VA_ARGS__))))

/**
 * fy_mapping_value() - Build a mapping fy_generic_value from variadic key/value pairs.
 *
 * The first argument may optionally be a fy_generic_builder*.  Arguments
 * must be interleaved key/value pairs.  With no arguments, returns
 * fy_map_empty_value.
 *
 * @...: Optional builder followed by zero or more key/value pairs
 *
 * Returns:
 * fy_generic_value of a mapping, or fy_map_empty_value if empty.
 */
#define fy_mapping_value(...) \
	((0 __VA_OPT__(+1)) ? \
		__VA_OPT__(fy_mapping_value_helper(__VA_ARGS__)) : \
		fy_map_empty_value)

/**
 * fy_mapping() - Build a mapping fy_generic from variadic key/value pairs.
 *
 * Wraps fy_mapping_value() in a fy_generic struct.
 *
 * @...: Optional builder followed by zero or more key/value pairs
 *
 * Returns:
 * A mapping fy_generic, or fy_map_empty if empty.
 */
#define fy_mapping(...) \
	((fy_generic) { .v = fy_mapping_value(__VA_ARGS__) })

/**
 * fy_value() - Encode a value as a fy_generic using a builder or alloca.
 *
 * If @_maybe_gb is a fy_generic_builder*, uses fy_gb_to_generic_value().
 * Otherwise treats @_maybe_gb as the value itself.
 *
 * @_maybe_gb: Either a fy_generic_builder* or a C value to encode
 * @...:       Additional value args when @_maybe_gb is a builder
 *
 * Returns:
 * A fy_generic wrapping the encoded value.
 */
#define fy_value(_maybe_gb, ...) \
	((fy_generic) { .v = fy_to_generic_value((_maybe_gb), ##__VA_ARGS__) })

/* fy_inplace_value() - Try to encode @_v as an inplace fy_generic_value (no allocation) */
#define fy_inplace_value(_v) \
	(fy_to_generic_inplace(_v))

/* fy_is_inplace() - Return true if @_v is stored inplace (no heap pointer) */
#define fy_is_inplace(_v) \
	(fy_generic_is_in_place(v))

/* fy_get_type() - Return the fy_generic_type tag of @_v */
#define fy_get_type(_v) \
	(fy_generic_get_type(_v))

/* fy_compare() - Compare two values for ordering, encoding them first if needed */
#define fy_compare(_a, _b) \
	(fy_generic_compare(fy_value(_a), fy_value(_b)))

/**
 * fy_foreach() - Iterate over every element (or key) of a collection.
 *
 * For sequences: @_v receives each element in order.
 * For mappings:  @_v receives each key in order.
 *
 * The type of @_v determines how the element is cast via fy_get_key_at_typed().
 *
 * @_v:   Loop variable (lvalue) that receives each element/key
 * @_col: Collection to iterate (fy_generic sequence or mapping)
 */
#define fy_foreach(_v, _col) \
	for (struct { size_t i; size_t len; } FY_LUNIQUE(_iter) = { 0, fy_len(_col) }; \
	     FY_LUNIQUE(_iter).i < FY_LUNIQUE(_iter).len && \
		(((_v) = fy_get_key_at_typed((_col), FY_LUNIQUE(_iter).i, __typeof__(_v))), 1); \
	FY_LUNIQUE(_iter).i++)

/* Builder-backed collection operation macros.
 * All fy_gb_*() macros forward to fy_generic_op() with the matching opcode and
 * the provided builder @_gb.  Items are encoded via FY_CPP_VA_GITEMS() which
 * calls fy_gb_to_generic() on each argument. */

/* fy_gb_create_mapping() - Create a mapping from variadic key/value pairs using a builder */
#define fy_gb_create_mapping(_gb, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_CREATE_MAP | FYGBOPF_MAP_ITEM_COUNT, \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

/* fy_gb_create_sequence() - Create a sequence from variadic elements using a builder */
#define fy_gb_create_sequence(_gb, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_CREATE_SEQ, \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

/* fy_gb_insert() - Insert elements into @_col at index @_idx */
#define fy_gb_insert(_gb, _col, _idx, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_INSERT | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_idx)))

/* fy_gb_replace() - Replace elements in @_col at index @_idx */
#define fy_gb_replace(_gb, _col, _idx, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_REPLACE | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_idx)))

/* fy_gb_append() - Append elements to the end of @_col */
#define fy_gb_append(_gb, _col, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_APPEND | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

/* fy_gb_assoc() - Associate key/value pairs into mapping @_map */
#define fy_gb_assoc(_gb, _map, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_ASSOC | FYGBOPF_MAP_ITEM_COUNT, (_map), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

/* fy_gb_disassoc() - Remove keys from mapping @_map */
#define fy_gb_disassoc(_gb, _map, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_DISASSOC, (_map), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

/* fy_gb_keys() - Return all keys of @_map as a sequence */
#define fy_gb_keys(_gb, _map) \
	(fy_generic_op((_gb), FYGBOPF_KEYS, (_map)))

/* fy_gb_values() - Return all values of @_map as a sequence */
#define fy_gb_values(_gb, _map) \
	(fy_generic_op((_gb), FYGBOPF_VALUES, (_map)))

/* fy_gb_items() - Return all key/value pairs of @_map as a sequence */
#define fy_gb_items(_gb, _map) \
	(fy_generic_op((_gb), FYGBOPF_ITEMS, (_map)))

/* fy_gb_contains() - Return fy_true if @_col contains all given elements */
#define fy_gb_contains(_gb, _col, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_CONTAINS | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

/* fy_gb_concat() - Concatenate @_col with additional collections */
#define fy_gb_concat(_gb, _col, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_CONCAT | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

/* fy_gb_reverse() - Return a reversed copy of @_col */
#define fy_gb_reverse(_gb, _col, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_REVERSE | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

/* fy_gb_merge() - Deep-merge @_map with additional mappings */
#define fy_gb_merge(_gb, _map, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_MERGE | FYGBOPF_MAP_ITEM_COUNT, (_map), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

/* fy_gb_unique() - Return @_col with duplicate elements removed */
#define fy_gb_unique(_gb, _col, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_UNIQUE | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

/* fy_gb_sort() - Return a sorted copy of @_col (optional comparator in @...) */
#define fy_gb_sort(_gb, _col, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_SORT | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

/* fy_gb_filter() - Return elements of @_col satisfying predicate @_fn */
#define fy_gb_filter(_gb, _col, _fn, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_FILTER | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_fn)))

/* fy_gb_pfilter() - Parallel filter of @_col using thread pool @_tp and predicate @_fn */
#define fy_gb_pfilter(_gb, _col, _tp, _fn, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_FILTER | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_PARALLEL, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_tp), (_fn)))

/* fy_gb_map() - Transform each element of @_col via function @_fn */
#define fy_gb_map(_gb, _col, _fn, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_MAP | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_fn)))

/* fy_gb_pmap() - Parallel map of @_col using thread pool @_tp and transform @_fn */
#define fy_gb_pmap(_gb, _col, _tp, _fn, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_MAP | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_PARALLEL, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_tp), (_fn)))

/* fy_gb_reduce() - Fold @_col into accumulator @_acc via reducer @_fn */
#define fy_gb_reduce(_gb, _col, _acc, _fn, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_REDUCE | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_fn), fy_value(_acc)))

/* fy_gb_preduce() - Parallel reduce of @_col via thread pool @_tp and reducer @_fn */
#define fy_gb_preduce(_gb, _col, _acc, _tp, _fn, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_REDUCE | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_PARALLEL, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_tp), (_fn), fy_value(_acc)))

/* fy_gb_get_at_path() - Traverse nested path @... from root @_in using builder @_gb */
#define fy_gb_get_at_path(_gb, _in, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_GET_AT_PATH, (_in), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

/* fy_local_get_at_path() - Traverse nested path @... from root @_in using a stack-allocated builder */
#define fy_local_get_at_path(_in, ...) \
	({ \
		__typeof__(_in) __in = (_in); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_GET_AT_PATH, __in, _count, _items); \
	})

/* fy_get_at_path() - Traverse nested path; dispatches to fy_gb_get_at_path or fy_local_get_at_path */
#define fy_get_at_path(...) (FY_CPP_THIRD(__VA_ARGS__, fy_gb_get_at_path, fy_local_get_at_path)(__VA_ARGS__))

/* fy_gb_set() - Update key/value pair(s) in @_col */
#define fy_gb_set(_gb, _col, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_SET | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

/* fy_gb_set_at_path() - Update the value at nested path @... in @_col */
#define fy_gb_set_at_path(_gb, _col, ...) \
	(fy_generic_op((_gb), \
		FYGBOPF_SET_AT_PATH | FYGBOPF_MAP_ITEM_COUNT, (_col), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__)))

/* fy_gb_parse() - Parse @_v (or input_data) as YAML/JSON and return a generic */
#define fy_gb_parse(_gb, _v, _parse_flags, _input_data, ...) \
	(fy_generic_op((_gb), FYGBOPF_PARSE | FYGBOPF_MAP_ITEM_COUNT, \
		       fy_gb_to_generic((_gb), (_v)), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_parse_flags), (_input_data)))

/* fy_gb_parse_file() - Parse a file at @_filename as YAML/JSON and return a generic */
#define fy_gb_parse_file(_gb, _parse_flags, _filename, ...) \
	(fy_generic_op((_gb), FYGBOPF_PARSE | FYGBOPF_MAP_ITEM_COUNT, \
			fy_null, \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
			(_parse_flags) | FYOPPF_INPUT_TYPE_FILENAME, (_filename)))

/* fy_gb_emit() - Emit @_v as YAML/JSON to the destination specified by @_emit_flags/@_output_data */
#define fy_gb_emit(_gb, _v, _emit_flags, _output_data, ...) \
	(fy_generic_op((_gb), FYGBOPF_EMIT | FYGBOPF_MAP_ITEM_COUNT, \
		       fy_gb_to_generic((_gb), (_v)), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
		       (_emit_flags), (_output_data)))

/* fy_gb_emit_file() - Emit @_v as YAML/JSON to the file named @_filename */
#define fy_gb_emit_file(_gb, _v, _emit_flags, _filename, ...) \
	(fy_generic_op((_gb), FYGBOPF_EMIT | FYGBOPF_MAP_ITEM_COUNT, \
		       fy_gb_to_generic((_gb), (_v)), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
		       (_emit_flags) | FYOPEF_OUTPUT_TYPE_FILENAME, (_filename)))

/* fy_gb_convert() - Convert @_v to the generic type @_type */
#define fy_gb_convert(_gb, _v, _type, ...) \
	(fy_generic_op((_gb), FYGBOPF_CONVERT | FYGBOPF_MAP_ITEM_COUNT, \
		       fy_gb_to_generic((_gb), (_v)), \
			FY_CPP_VA_COUNT(__VA_ARGS__), \
			FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__), \
		       (_type)))


/* FY_GENERIC_BUILDER_IN_PLACE_MAX_SIZE - Maximum stack-buffer size tried by FY_LOCAL_OP before giving up */
#define FY_GENERIC_BUILDER_IN_PLACE_MAX_SIZE	65536

/**
 * FY_LOCAL_OP() - Execute a generic operation using a stack-allocated (alloca) builder.
 *
 * Creates a temporary fy_generic_builder backed by an alloca'd buffer and
 * forwards the remaining arguments to fy_generic_op().  On allocation failure
 * the buffer is doubled and the operation is retried until it either succeeds
 * or the buffer would exceed FY_GENERIC_BUILDER_IN_PLACE_MAX_SIZE.
 *
 * The stack frame is saved/restored around each retry so that failed attempts
 * do not waste stack space.  This is safe because all generic values are
 * immutable and operations have no observable side-effects on failure.
 *
 * @...: Same arguments as fy_generic_op() (flags, input, count, items, ...)
 *
 * Returns:
 * The operation result, or fy_invalid on error.
 */
#define FY_LOCAL_OP(...) \
	({ \
		size_t _sz = FY_GENERIC_BUILDER_LINEAR_IN_PLACE_MIN_SIZE; \
		char *_buf = NULL; \
		struct fy_generic_builder *_gb = NULL; \
		void *_stack_save; \
		fy_generic _v = fy_invalid; \
		bool _need_to_break; \
		\
		_stack_save = FY_STACK_SAVE(); \
		for (;;) { \
			_buf = alloca(_sz); \
			_gb = fy_generic_builder_create_in_place(FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER, NULL, _buf, _sz); \
			_v = fy_generic_op(_gb __VA_OPT__(,) __VA_ARGS__); \
			if (fy_generic_is_valid(_v)) \
				break; \
			_need_to_break = !fy_gb_allocation_failures(_gb) || _sz > FY_GENERIC_BUILDER_IN_PLACE_MAX_SIZE; \
			FY_STACK_RESTORE(_stack_save); \
			if (_need_to_break) \
				break; \
			_sz = _sz * 2; \
		} \
		_v; \
	})

/* fy_create_local_sequence() - Create a sequence from variadic elements using a stack builder */
#define fy_create_local_sequence(...) \
	({ \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_CREATE_SEQ, _count, _items); \
	})

/* fy_create_local_mapping() - Create a mapping from variadic key/value pairs using a stack builder */
#define fy_create_local_mapping(...) \
	({ \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_CREATE_MAP | FYGBOPF_MAP_ITEM_COUNT, _count, _items); \
	})

/* Stack-allocated (alloca) variants of all builder operations.
 * Each fy_local_*() macro is identical to the corresponding fy_gb_*() but
 * uses FY_LOCAL_OP (alloca backing) instead of an explicit builder pointer. */

/* fy_local_insert() - Insert elements at @_idx in @_col using a stack builder */
#define fy_local_insert(_col, _idx, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		__typeof__(_idx) __idx = (_idx); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_INSERT | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items, __idx); \
	})

/* fy_local_replace() - Replace elements at @_idx in @_col using a stack builder */
#define fy_local_replace( _col, _idx, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		__typeof__(_idx) __idx = (_idx); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_REPLACE | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items, __idx); \
	})

/* fy_local_append() - Append elements to @_col using a stack builder */
#define fy_local_append( _col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_APPEND | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

/* fy_local_assoc() - Associate key/value pairs into @_map using a stack builder */
#define fy_local_assoc(_map, ...) \
	({ \
		__typeof__(_map) __map = (_map); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_ASSOC | FYGBOPF_MAP_ITEM_COUNT, __map, _count, _items); \
	})

/* fy_local_disassoc() - Remove keys from @_map using a stack builder */
#define fy_local_disassoc(_map, ...) \
	({ \
		__typeof__(_map) __map = (_map); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_DISASSOC, __map, _count, _items); \
	})

/* fy_local_keys() - Return keys of @_map as a sequence using a stack builder */
#define fy_local_keys(_map) \
	({ \
		__typeof__(_map) __map = (_map); \
		FY_LOCAL_OP(FYGBOPF_KEYS, __map, 0, NULL); \
	})

/* fy_local_values() - Return values of @_map as a sequence using a stack builder */
#define fy_local_values(_map) \
	({ \
		__typeof__(_map) __map = (_map); \
		FY_LOCAL_OP(FYGBOPF_VALUES, __map, 0, NULL); \
	})

/* fy_local_items() - Return key/value pairs of @_map as a sequence using a stack builder */
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

/* fy_local_concat() - Concatenate @_col with additional collections using a stack builder */
#define fy_local_concat(_col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_CONCAT | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

/* fy_local_reverse() - Return a reversed copy of @_col using a stack builder */
#define fy_local_reverse(_col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_REVERSE | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

/* fy_local_merge() - Deep-merge @_col with additional mappings using a stack builder */
#define fy_local_merge(_col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_MERGE | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

/* fy_local_unique() - Remove duplicates from @_col using a stack builder */
#define fy_local_unique(_col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_UNIQUE | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

/* fy_local_sort() - Sort @_col using a stack builder (optional comparator in @...) */
#define fy_local_sort(_col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_SORT | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

/* fy_local_filter() - Filter elements of @_col via predicate @_fn using a stack builder */
#define fy_local_filter(_col, _fn, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_FILTER | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items, (_fn)); \
	})

/* fy_local_pfilter() - Parallel filter via thread pool @_tp and predicate @_fn using a stack builder */
#define fy_local_pfilter(_col, _tp, _fn, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_FILTER | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_PARALLEL, \
				__col, _count, _items, (_tp), (_fn)); \
	})

/* fy_local_map() - Transform each element of @_col via @_fn using a stack builder */
#define fy_local_map(_col, _fn, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_MAP | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items, (_fn)); \
	})

/* fy_local_pmap() - Parallel map via thread pool @_tp and transform @_fn using a stack builder */
#define fy_local_pmap(_col, _tp, _fn, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_MAP | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_PARALLEL, \
				__col, _count, _items, (_tp), (_fn)); \
	})

/* fy_local_reduce() - Fold @_col into @_acc via @_fn using a stack builder */
#define fy_local_reduce(_col, _acc, _fn, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_REDUCE | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items, (_fn), fy_value(_acc)); \
	})

/* fy_local_preduce() - Parallel reduce via @_tp and @_fn using a stack builder */
#define fy_local_preduce(_col, _acc, _tp, _fn, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_REDUCE | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_PARALLEL, \
			__col, _count, _items, (_tp), (_fn), fy_value(_acc)); \
	})

/* fy_local_set() - Update key/value pair(s) in @_col using a stack builder */
#define fy_local_set(_col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_SET | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

/* fy_local_set_at_path() - Update value at nested path using a stack builder */
#define fy_local_set_at_path(_col, ...) \
	({ \
		__typeof__(_col) __col = (_col); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_SET_AT_PATH | FYGBOPF_MAP_ITEM_COUNT, __col, _count, _items); \
	})

/* fy_local_parse() - Parse @_v/input_data as YAML/JSON using a stack builder */
#define fy_local_parse(_v, _parse_flags, _input_data, ...) \
	({ \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_PARSE | FYGBOPF_MAP_ITEM_COUNT, \
				fy_value(_v), _count, _items, \
				(_parse_flags), (_input_data)); \
	})

/* fy_local_emit() - Emit @_v as YAML/JSON to @_output_data using a stack builder */
#define fy_local_emit(_v, _emit_flags, _output_data, ...) \
	({ \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_EMIT | FYGBOPF_MAP_ITEM_COUNT, \
				fy_value(_v), _count, _items, \
				(_emit_flags), (_output_data)); \
	})

/* fy_local_parse_file() - Parse a file at @_filename as YAML/JSON using a stack builder */
#define fy_local_parse_file(_parse_flags, _filename, ...) \
	({ \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_PARSE | FYGBOPF_MAP_ITEM_COUNT, \
				fy_null, _count, _items, \
				(_parse_flags) | FYOPPF_INPUT_TYPE_FILENAME, (_filename)); \
	})

/* fy_local_emit_file() - Emit @_v as YAML/JSON to file @_filename using a stack builder */
#define fy_local_emit_file(_v, _emit_flags, _filename, ...) \
	({ \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_EMIT | FYGBOPF_MAP_ITEM_COUNT, \
				fy_value(_v), _count, _items, \
				(_emit_flags) | FYOPEF_OUTPUT_TYPE_FILENAME, (_filename)); \
	})

/* fy_local_convert() - Convert @_v to type @_type using a stack builder */
#define fy_local_convert(_v, _type, ...) \
	({ \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__); \
		FY_LOCAL_OP(FYGBOPF_CONVERT | FYGBOPF_MAP_ITEM_COUNT, \
				fy_value(_v), _count, _items, \
				(_type)); \
	})

/* Lambda operation macros.
 *
 * The fy_gb_*_lambda() macros create an anonymous function/block from an
 * inline expression and pass it to the corresponding fy_gb_*() operation.
 * The fy_local_*_lambda() variants use a stack builder instead.
 *
 * Two implementations are provided:
 *   - FY_HAVE_NESTED_FUNC_LAMBDAS (GCC nested function extension)
 *   - FY_HAVE_BLOCK_LAMBDAS (Clang __BLOCKS__ extension, -fblocks)
 *
 * In each lambda expression the variables @gb and @v (and @acc for reduce)
 * are implicitly available as the builder and current element respectively.
 */

#ifdef FY_HAVE_NESTED_FUNC_LAMBDAS

/* fy_gb_filter_lambda() - Filter @_col keeping elements where @_expr is true (builder, nested fn) */
#define fy_gb_filter_lambda(_gb, _col, _expr) \
	({ \
		fy_generic_filter_pred_fn _fn = ({ \
			bool __fy_pred_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_pred_fn; }); \
		fy_gb_filter((_gb), (_col), _fn); \
	})

/* fy_local_filter_lambda() - Filter @_col using @_expr (stack builder, nested fn) */
#define fy_local_filter_lambda(_col, _expr) \
	({ \
		fy_generic_filter_pred_fn _fn = ({ \
			bool __fy_pred_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_pred_fn; }); \
		fy_local_filter((_col), _fn); \
	})

/* fy_gb_pfilter_lambda() - Parallel filter @_col via @_tp keeping elements where @_expr (builder, nested fn) */
#define fy_gb_pfilter_lambda(_gb, _col, _tp, _expr) \
	({ \
		fy_generic_filter_pred_fn _fn = ({ \
			bool __fy_pred_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_pred_fn; }); \
		fy_gb_pfilter((_gb), (_col), (_tp), _fn); \
	})

/* fy_local_pfilter_lambda() - Parallel filter @_col via @_tp (stack builder, nested fn) */
#define fy_local_pfilter_lambda(_col, _tp, _expr) \
	({ \
		fy_generic_filter_pred_fn _fn = ({ \
			bool __fy_pred_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_pred_fn; }); \
		fy_local_pfilter((_col), (_tp), _fn); \
	})

/* fy_gb_map_lambda() - Transform each element of @_col via @_expr (builder, nested fn) */
#define fy_gb_map_lambda(_gb, _col, _expr) \
	({ \
		fy_generic_map_xform_fn _fn = ({ \
			fy_generic __fy_xform_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_xform_fn; }); \
		fy_gb_map((_gb), (_col), _fn); \
	})

/* fy_local_map_lambda() - Transform each element of @_col via @_expr (stack builder, nested fn) */
#define fy_local_map_lambda(_col, _expr) \
	({ \
		fy_generic_map_xform_fn _fn = ({ \
			fy_generic __fy_xform_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_xform_fn; }); \
		fy_local_map((_col), _fn); \
	})

/* fy_gb_pmap_lambda() - Parallel map @_col via @_tp using @_expr (builder, nested fn) */
#define fy_gb_pmap_lambda(_gb, _col, _tp, _expr) \
	({ \
		fy_generic_map_xform_fn _fn = ({ \
			fy_generic __fy_xform_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_xform_fn; }); \
		fy_gb_pmap((_gb), (_col), (_tp), _fn); \
	})

/* fy_local_pmap_lambda() - Parallel map @_col via @_tp using @_expr (stack builder, nested fn) */
#define fy_local_pmap_lambda(_col, _tp, _expr) \
	({ \
		fy_generic_map_xform_fn _fn = ({ \
			fy_generic __fy_xform_fn(struct fy_generic_builder *gb, fy_generic v) { \
				return (_expr) ; \
			} \
			&__fy_xform_fn; }); \
		fy_local_pmap((_col), (_tp), _fn); \
	})

/* fy_gb_reduce_lambda() - Fold @_col into @_acc via @_expr (builder, nested fn) */
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

/* fy_local_reduce_lambda() - Fold @_col into @_acc via @_expr (stack builder, nested fn) */
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

/* fy_gb_preduce_lambda() - Parallel fold via @_tp and @_expr (builder, nested fn) */
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

/* fy_local_preduce_lambda() - Parallel fold via @_tp and @_expr (stack builder, nested fn) */
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

/* Clang Blocks lambda variants — same interface as above but use ^block syntax.
 * Require compilation with -fblocks and linking against -lBlocksRuntime. */
#ifdef FY_HAVE_BLOCK_LAMBDAS

/* fy_gb_filter_lambda() - Filter @_col keeping elements where @_block_body is true (builder, block) */
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

/* fy_gb_map_lambda() - Transform each element of @_col via @_block_body (builder, block) */
#define fy_gb_map_lambda(_gb, _col, _block_body) \
	({ \
		fy_generic_map_xform_block _block = \
			(^fy_generic(struct fy_generic_builder *gb, fy_generic v) { \
				return _block_body ; \
			}); \
		fy_generic_op((_gb), FYGBOPF_MAP | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_BLOCK_FN, \
				(_col), 0, NULL, _block); \
	})

/* fy_local_map_lambda() - Transform each element of @_col via @_block_body (stack builder, block) */
#define fy_local_map_lambda(_col, _block_body) \
	({ \
		fy_generic_map_xform_block _block = \
			(^fy_generic(struct fy_generic_builder *gb, fy_generic v) { \
				return _block_body ; \
			}); \
		FY_LOCAL_OP(FYGBOPF_MAP | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_BLOCK_FN, \
				(_col), 0, NULL, _block); \
	})

/* fy_gb_pmap_lambda() - Parallel map @_col via @_tp using @_block_body (builder, block) */
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

/* fy_local_pmap_lambda() - Parallel map @_col via @_tp using @_block_body (stack builder, block) */
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

/* fy_gb_reduce_lambda() - Fold @_col into @_acc via @_block_body (builder, block) */
#define fy_gb_reduce_lambda(_gb, _col, _acc, _block_body) \
	({ \
		fy_generic_reducer_block _block = \
			(^fy_generic(struct fy_generic_builder *gb, fy_generic acc, fy_generic v) { \
				return _block_body ; \
			}); \
		fy_generic_op((_gb), FYGBOPF_REDUCE | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_BLOCK_FN, \
				(_col), 0, NULL, _block, fy_value(_acc)); \
	})

/* fy_local_reduce_lambda() - Fold @_col into @_acc via @_block_body (stack builder, block) */
#define fy_local_reduce_lambda(_col, _acc, _block_body) \
	({ \
		fy_generic_reducer_block _block = \
			(^fy_generic(struct fy_generic_builder *gb, fy_generic acc, fy_generic v) { \
				return _block_body ; \
			}); \
		FY_LOCAL_OP(FYGBOPF_REDUCE | FYGBOPF_MAP_ITEM_COUNT | FYGBOPF_BLOCK_FN, \
				(_col), 0, NULL, _block, fy_value(_acc)); \
	})

/* fy_gb_preduce_lambda() - Parallel fold via @_tp and @_block_body (builder, block) */
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

/* fy_local_preduce_lambda() - Parallel fold via @_tp and @_block_body (stack builder, block) */
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

/* Dispatching helpers for the unified top-level fy_* macros.
 * Each accepts an optional leading fy_generic_builder* as the first argument.
 * If present, the builder path is used; otherwise the local/alloca path. */

/**
 * FY_GB_OR_LOCAL_OP() - Dispatch to fy_generic_op or FY_LOCAL_OP based on @_gb_or_NULL.
 *
 * @_gb_or_NULL: Builder pointer, or NULL to use FY_LOCAL_OP
 * @_flags:      Opcode + modifiers
 * @...:         Remaining arguments forwarded unchanged
 */
#define FY_GB_OR_LOCAL_OP(_gb_or_NULL, _flags, ...) \
	((_gb_or_NULL) ? \
		fy_generic_op((_gb_or_NULL), (_flags) __VA_OPT__(,) __VA_ARGS__) : \
		FY_LOCAL_OP((_flags) __VA_OPT__(,) __VA_ARGS__))

/**
 * FY_GB_OR_LOCAL_COL() - Dispatch a single-collection operation (no items array).
 *
 * The first non-builder argument is treated as the input collection.
 *
 * @_flags:       Opcode + modifiers
 * @_gb_or_first: Optional builder or first collection arg
 * @...:          Remaining args (usually the collection when @_gb_or_first is a builder)
 */
#define FY_GB_OR_LOCAL_COL(_flags, _gb_or_first, ...) \
	({ \
		struct fy_generic_builder *_gb = fy_gb_or_NULL(_gb_or_first); \
		const fy_generic _col = fy_first_non_gb(_gb_or_first __VA_OPT__(,) __VA_ARGS__ , fy_invalid); \
		FY_GB_OR_LOCAL_OP(_gb, (_flags), _col, 0, NULL); \
	})

/**
 * FY_GB_OR_LOCAL_COL_COUNT_ITEMS() - Dispatch a collection+items operation.
 *
 * The first non-builder argument is the collection; remaining arguments are
 * encoded as the items array.
 *
 * @_flags:       Opcode + modifiers
 * @_gb_or_first: Optional builder or first collection arg
 * @...:          Collection (if builder given) + item arguments
 */
#define FY_GB_OR_LOCAL_COL_COUNT_ITEMS(_flags, _gb_or_first, ...) \
	({ \
		struct fy_generic_builder *_gb = fy_gb_or_NULL(_gb_or_first); \
		const fy_generic _col = fy_first_non_gb(_gb_or_first __VA_OPT__(,) __VA_ARGS__); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__) - (_gb != NULL); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__) + (_gb != NULL); \
		FY_GB_OR_LOCAL_OP(_gb, (_flags), _col, _count, _items); \
	})

/**
 * FY_GB_OR_LOCAL_COL_IDX_COUNT_ITEMS() - Dispatch a collection+index+items operation.
 *
 * The first non-builder argument is the collection, the second is the index,
 * and remaining arguments are the items.
 *
 * @_flags:       Opcode + modifiers
 * @_gb_or_first: Optional builder or first collection arg
 * @...:          Collection, index, and item arguments
 */
#define FY_GB_OR_LOCAL_COL_IDX_COUNT_ITEMS(_flags, _gb_or_first, ...) \
	({ \
		struct fy_generic_builder *_gb = fy_gb_or_NULL(_gb_or_first); \
		const fy_generic _col = fy_first_non_gb(_gb_or_first, __VA_ARGS__); \
		size_t _idx = fy_second_non_gb(_gb_or_first, __VA_ARGS__); \
		const size_t _count = FY_CPP_VA_COUNT(__VA_ARGS__) - 1 - (_gb != NULL); \
		const fy_generic *_items = FY_CPP_VA_GITEMS(FY_CPP_VA_COUNT(__VA_ARGS__), __VA_ARGS__) + 1 + (_gb != NULL); \
		FY_GB_OR_LOCAL_OP(_gb, (_flags), _col, _count, _items, _idx); \
	})

/* fy_insert() - Insert items into a collection at index; optional leading builder */
#define fy_insert(_first, ...) \
	FY_GB_OR_LOCAL_COL_IDX_COUNT_ITEMS(FYGBOPF_INSERT | FYGBOPF_MAP_ITEM_COUNT, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_replace() - Replace items in a collection at index; optional leading builder */
#define fy_replace(_first, ...) \
	FY_GB_OR_LOCAL_COL_IDX_COUNT_ITEMS(FYGBOPF_REPLACE | FYGBOPF_MAP_ITEM_COUNT, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_append() - Append items to a collection; optional leading builder */
#define fy_append(_first, ...) \
	FY_GB_OR_LOCAL_COL_COUNT_ITEMS(FYGBOPF_APPEND | FYGBOPF_MAP_ITEM_COUNT, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_assoc() - Associate key-value pairs in a mapping; optional leading builder */
#define fy_assoc(_first, ...) \
	FY_GB_OR_LOCAL_COL_COUNT_ITEMS(FYGBOPF_ASSOC | FYGBOPF_MAP_ITEM_COUNT, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_disassoc() - Remove key-value pairs from a mapping by key; optional leading builder */
#define fy_disassoc(_first, ...) \
	FY_GB_OR_LOCAL_COL_COUNT_ITEMS(FYGBOPF_DISASSOC, _first __VA_OPT__(,) __VA_ARGS__)

/**
 * FY_GB_OR_LOCAL_SLICE() - Dispatch a half-open slice [start, end) operation.
 *
 * Dispatches to a builder-backed or stack-local slice depending on whether a
 * leading fy_generic_builder pointer is provided.
 *
 * @_flags:       Opcode + modifier flags (e.g. FYGBOPF_SLICE).
 * @_gb_or_first: Optional builder or first sequence argument.
 * @...:          Sequence, start index (size_t), end index (size_t).
 */
#define FY_GB_OR_LOCAL_SLICE(_flags, _gb_or_first, ...) \
	({ \
		struct fy_generic_builder *_gb = fy_gb_or_NULL(_gb_or_first); \
		const fy_generic _seq = fy_first_non_gb(_gb_or_first, __VA_ARGS__); \
		const size_t _start = fy_second_non_gb(_gb_or_first, __VA_ARGS__); \
		const size_t _end = fy_third_non_gb(_gb_or_first, __VA_ARGS__); \
		FY_GB_OR_LOCAL_OP(_gb, (_flags), _seq, 0, NULL, _start, _end); \
	})

/**
 * FY_GB_OR_LOCAL_SLICE_PY() - Dispatch a Python-style slice operation with signed indices.
 *
 * Like FY_GB_OR_LOCAL_SLICE() but accepts signed (ssize_t) indices so that negative
 * values count from the end of the sequence, matching Python slice semantics.
 *
 * @_flags:       Opcode + modifier flags (e.g. FYGBOPF_SLICE_PY).
 * @_gb_or_first: Optional builder or first sequence argument.
 * @...:          Sequence, start (ssize_t), end (ssize_t).
 */
#define FY_GB_OR_LOCAL_SLICE_PY(_flags, _gb_or_first, ...) \
	({ \
		struct fy_generic_builder *_gb = fy_gb_or_NULL(_gb_or_first); \
		const fy_generic _seq = fy_first_non_gb(_gb_or_first, __VA_ARGS__); \
		const ssize_t _start = (ssize_t)fy_second_non_gb(_gb_or_first, __VA_ARGS__); \
		const ssize_t _end = (ssize_t)fy_third_non_gb(_gb_or_first, __VA_ARGS__); \
		FY_GB_OR_LOCAL_OP(_gb, (_flags), _seq, 0, NULL, _start, _end); \
	})

/**
 * FY_GB_OR_LOCAL_SLICE_N() - Dispatch a take-N or drop-N slice operation.
 *
 * Dispatches to a builder-backed or stack-local take/drop depending on whether a
 * leading fy_generic_builder pointer is provided.
 *
 * @_flags:       Opcode + modifier flags (e.g. FYGBOPF_TAKE or FYGBOPF_DROP).
 * @_gb_or_first: Optional builder or first sequence argument.
 * @...:          Sequence, count N (size_t).
 */
#define FY_GB_OR_LOCAL_SLICE_N(_flags, _gb_or_first, ...) \
	({ \
		struct fy_generic_builder *_gb = fy_gb_or_NULL(_gb_or_first); \
		const fy_generic _seq = fy_first_non_gb(_gb_or_first, __VA_ARGS__); \
		const size_t _n = fy_second_non_gb(_gb_or_first, __VA_ARGS__); \
		FY_GB_OR_LOCAL_OP(_gb, (_flags), _seq, 0, NULL, _n); \
	})

/* fy_keys() - Return the keys of a mapping as a sequence; optional leading builder */
#define fy_keys(_first, ...) \
	FY_GB_OR_LOCAL_COL(FYGBOPF_KEYS, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_values() - Return the values of a mapping (or elements of a sequence); optional leading builder */
#define fy_values(_first, ...) \
	FY_GB_OR_LOCAL_COL(FYGBOPF_VALUES, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_items() - Return key-value pairs of a mapping as a sequence of pairs; optional leading builder */
#define fy_items(_first, ...) \
	FY_GB_OR_LOCAL_COL(FYGBOPF_ITEMS, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_contains() - Test whether a collection contains the given item(s); optional leading builder */
#define fy_contains(_first, ...) \
	(FY_GB_OR_LOCAL_COL_COUNT_ITEMS(FYGBOPF_CONTAINS | FYGBOPF_MAP_ITEM_COUNT, \
					_first __VA_OPT__(,) __VA_ARGS__))

/* fy_concat() - Concatenate two or more collections; optional leading builder */
#define fy_concat(_first, ...) \
	FY_GB_OR_LOCAL_COL_COUNT_ITEMS(FYGBOPF_CONCAT, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_reverse() - Reverse the elements of a sequence or mapping; optional leading builder */
#define fy_reverse(_first, ...) \
	FY_GB_OR_LOCAL_COL(FYGBOPF_REVERSE, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_merge() - Merge two or more collections (mappings: last write wins); optional leading builder */
#define fy_merge(_first, ...) \
	FY_GB_OR_LOCAL_COL_COUNT_ITEMS(FYGBOPF_MERGE, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_unique() - Remove duplicate elements from a collection; optional leading builder */
#define fy_unique(_first, ...) \
	FY_GB_OR_LOCAL_COL(FYGBOPF_UNIQUE, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_sort() - Sort the elements of a collection; optional leading builder */
#define fy_sort(_first, ...) \
	FY_GB_OR_LOCAL_COL(FYGBOPF_SORT, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_filter() - Filter a collection with a predicate fn; dispatches to fy_gb_filter or fy_local_filter */
#define fy_filter(...) (FY_CPP_FOURTH(__VA_ARGS__, fy_gb_filter, fy_local_filter)(__VA_ARGS__))
/* fy_pfilter() - Parallel filter with a predicate fn; dispatches to fy_gb_pfilter or fy_local_pfilter */
#define fy_pfilter(...) (FY_CPP_FIFTH(__VA_ARGS__, fy_gb_pfilter, fy_local_pfilter)(__VA_ARGS__))

/* fy_map() - Transform collection elements; dispatches to fy_gb_map or fy_local_map */
#define fy_map(...) (FY_CPP_FOURTH(__VA_ARGS__, fy_gb_map, fy_local_map)(__VA_ARGS__))
/* fy_pmap() - Parallel map transform; dispatches to fy_gb_pmap or fy_local_pmap */
#define fy_pmap(...) (FY_CPP_FIFTH(__VA_ARGS__, fy_gb_pmap, fy_local_pmap)(__VA_ARGS__))

/* fy_reduce() - Fold a collection to a scalar; dispatches to fy_gb_reduce or fy_local_reduce */
#define fy_reduce(...) (FY_CPP_FIFTH(__VA_ARGS__, fy_gb_reduce, fy_local_reduce)(__VA_ARGS__))
/* fy_preduce() - Parallel fold; dispatches to fy_gb_preduce or fy_local_preduce */
#define fy_preduce(...) (FY_CPP_SIXTH(__VA_ARGS__, fy_gb_preduce, fy_local_preduce)(__VA_ARGS__))

/* Sequence slicing operations */
/* fy_slice() - Slice a sequence [start, end) with unsigned indices; optional leading builder */
#define fy_slice(_first, ...) \
	FY_GB_OR_LOCAL_SLICE(FYGBOPF_SLICE, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_slice_py() - Python-style slice [start, end) with signed indices; optional leading builder */
#define fy_slice_py(_first, ...) \
	FY_GB_OR_LOCAL_SLICE_PY(FYGBOPF_SLICE_PY, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_take() - Return the first N elements of a sequence; optional leading builder */
#define fy_take(_first, ...) \
	FY_GB_OR_LOCAL_SLICE_N(FYGBOPF_TAKE, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_drop() - Return all but the first N elements of a sequence; optional leading builder */
#define fy_drop(_first, ...) \
	FY_GB_OR_LOCAL_SLICE_N(FYGBOPF_DROP, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_first() - Return the first element of a sequence; optional leading builder */
#define fy_first(_first, ...) \
	({ \
		struct fy_generic_builder *_gb = fy_gb_or_NULL(_first); \
		const fy_generic _seq = fy_first_non_gb(_first __VA_OPT__(,) __VA_ARGS__, fy_invalid); \
		FY_GB_OR_LOCAL_OP(_gb, FYGBOPF_FIRST, _seq, 0, NULL); \
	})

/* fy_last() - Return the last element of a sequence; optional leading builder */
#define fy_last(_first, ...) \
	({ \
		struct fy_generic_builder *_gb = fy_gb_or_NULL(_first); \
		const fy_generic _seq = fy_first_non_gb(_first __VA_OPT__(,) __VA_ARGS__, fy_invalid); \
		FY_GB_OR_LOCAL_OP(_gb, FYGBOPF_LAST, _seq, 0, NULL); \
	})

/* fy_rest() - Return all elements of a sequence except the first; optional leading builder */
#define fy_rest(_first, ...) \
	({ \
		struct fy_generic_builder *_gb = fy_gb_or_NULL(_first); \
		const fy_generic _seq = fy_first_non_gb(_first __VA_OPT__(,) __VA_ARGS__, fy_invalid); \
		FY_GB_OR_LOCAL_OP(_gb, FYGBOPF_REST, _seq, 0, NULL); \
	})

/*
 * Lambda variants - fy_filter_lambda, fy_map_lambda, fy_reduce_lambda
 *
 *   - Clang: Blocks (requires -fblocks -lBlocksRuntime)
 *   - GCC: Nested function pointers (standard extension)
 */

#ifdef FY_HAVE_LAMBDAS

/* fy_filter_lambda() - Filter with inline lambda; dispatches to fy_gb_filter_lambda or fy_local_filter_lambda */
#define fy_filter_lambda(...) (FY_CPP_FOURTH(__VA_ARGS__, fy_gb_filter_lambda, fy_local_filter_lambda)(__VA_ARGS__))
/* fy_pfilter_lambda() - Parallel filter with inline lambda; dispatches to fy_gb_pfilter_lambda or fy_local_pfilter_lambda */
#define fy_pfilter_lambda(...) (FY_CPP_FIFTH(__VA_ARGS__, fy_gb_pfilter_lambda, fy_local_pfilter_lambda)(__VA_ARGS__))

/* fy_map_lambda() - Map transform with inline lambda; dispatches to fy_gb_map_lambda or fy_local_map_lambda */
#define fy_map_lambda(...) (FY_CPP_FOURTH(__VA_ARGS__, fy_gb_map_lambda, fy_local_map_lambda)(__VA_ARGS__))
/* fy_pmap_lambda() - Parallel map with inline lambda; dispatches to fy_gb_pmap_lambda or fy_local_pmap_lambda */
#define fy_pmap_lambda(...) (FY_CPP_FIFTH(__VA_ARGS__, fy_gb_pmap_lambda, fy_local_pmap_lambda)(__VA_ARGS__))

/* fy_reduce_lambda() - Fold with inline lambda; dispatches to fy_gb_reduce_lambda or fy_local_reduce_lambda */
#define fy_reduce_lambda(...) (FY_CPP_FIFTH(__VA_ARGS__, fy_gb_reduce_lambda, fy_local_reduce_lambda)(__VA_ARGS__))
/* fy_preduce_lambda() - Parallel fold with inline lambda; dispatches to fy_gb_preduce_lambda or fy_local_preduce_lambda */
#define fy_preduce_lambda(...) (FY_CPP_SIXTH(__VA_ARGS__, fy_gb_preduce_lambda, fy_local_preduce_lambda)(__VA_ARGS__))

#endif

/* fy_set() - Set key-value pair(s) in a collection (upsert); optional leading builder */
#define fy_set(_first, ...) \
	FY_GB_OR_LOCAL_COL_COUNT_ITEMS(FYGBOPF_SET | FYGBOPF_MAP_ITEM_COUNT, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_set_at_path() - Set a value at a dot-separated path, creating intermediate mappings; optional leading builder */
#define fy_set_at_path(_first, ...) \
	FY_GB_OR_LOCAL_COL_COUNT_ITEMS(FYGBOPF_SET_AT_PATH | FYGBOPF_MAP_ITEM_COUNT, _first __VA_OPT__(,) __VA_ARGS__)

/* fy_parse() - Parse YAML/JSON text into a generic; dispatches to fy_gb_parse or fy_local_parse */
#define fy_parse(...) \
	(FY_CPP_FIFTH(__VA_ARGS__, fy_gb_parse, fy_local_parse)(__VA_ARGS__))

/* fy_emit() - Emit a generic as YAML/JSON text; dispatches to fy_gb_emit or fy_local_emit */
#define fy_emit(...) \
	(FY_CPP_FIFTH(__VA_ARGS__, fy_gb_emit, fy_local_emit)(__VA_ARGS__))

/* fy_parse_file() - Parse a YAML/JSON file into a generic; dispatches to fy_gb_parse_file or fy_local_parse_file */
#define fy_parse_file(...) \
	(FY_CPP_FOURTH(__VA_ARGS__, fy_gb_parse_file, fy_local_parse_file)(__VA_ARGS__))

/* fy_emit_file() - Emit a generic as YAML/JSON to a file; dispatches to fy_gb_emit_file or fy_local_emit_file */
#define fy_emit_file(...) \
	(FY_CPP_FIFTH(__VA_ARGS__, fy_gb_emit_file, fy_local_emit_file)(__VA_ARGS__))

/* fy_convert() - Convert a generic value to a different scalar type; dispatches to fy_gb_convert or fy_local_convert */
#define fy_convert(...) \
	(FY_CPP_FOURTH(__VA_ARGS__, fy_gb_convert, fy_local_convert)(__VA_ARGS__))

/**
 * fy_generic_emit_compact() - Emit a generic value to stdout in compact (flow-style) format.
 *
 * Formats @v using flow style (all on one line) and writes the result to stdout.
 * Intended primarily for quick debugging and interactive inspection.
 *
 * @v: The generic value to emit.
 *
 * Returns:
 * 0 on success, -1 on error.
 */
int fy_generic_emit_compact(fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_emit_default() - Emit a generic value to stdout in default (block-style) format.
 *
 * Formats @v using block style (multi-line indented) and writes the result to stdout.
 * Intended primarily for quick debugging and interactive inspection.
 *
 * @v: The generic value to emit.
 *
 * Returns:
 * 0 on success, -1 on error.
 */
int fy_generic_emit_default(fy_generic v)
	FY_EXPORT;

////////////////////////

/*
 * Format of directory:
 *
 * root:
 *   foo: bar
 * version:
 *   major: 1
 *   minor: 2
 * version-explicit: false
 * tags:
 * - handle: !
 *   prefix: !
 * - handle: !!
 *   prefix: "tag:yaml.org,2002:"
 * - handle: ""
 *   prefix: ""
 * tags-explicit: false
 * schema: yaml1.2-core
 *
 */

/**
 * fy_generic_dir_get_document_count() - Get the number of documents in a directory generic.
 *
 * A directory generic is a sequence produced by parsing with directory mode enabled.
 * Each element is a vds (value-with-document-state) generic.
 *
 * @vdir: The directory generic.
 *
 * Returns:
 * The number of documents, or -1 on error.
 */
int
fy_generic_dir_get_document_count(fy_generic vdir)
	FY_EXPORT;

/**
 * fy_generic_dir_get_document_vds() - Get the vds generic for a document at a given index.
 *
 * Returns the combined (root, document-state) generic for the document at position @idx
 * within the directory @vdir.
 *
 * @vdir: The directory generic.
 * @idx:  Zero-based document index.
 *
 * Returns:
 * The vds fy_generic for the requested document, or fy_invalid on error.
 */
fy_generic
fy_generic_dir_get_document_vds(fy_generic vdir, size_t idx)
	FY_EXPORT;

/**
 * fy_generic_vds_get_root() - Extract the root value from a document-with-state generic.
 *
 * A vds generic bundles the document root together with its YAML document state.
 * This function returns only the root value portion.
 *
 * @vds: The document-with-state generic.
 *
 * Returns:
 * The root fy_generic, or fy_invalid on error.
 */
fy_generic
fy_generic_vds_get_root(fy_generic vds)
	FY_EXPORT;

/**
 * fy_generic_vds_get_document_state() - Extract the YAML document state from a vds generic.
 *
 * A vds generic bundles the document root together with its YAML document state.
 * This function returns the associated @fy_document_state pointer (version, tags, schema, etc.).
 *
 * @vds: The document-with-state generic.
 *
 * Returns:
 * Pointer to the @fy_document_state, or NULL on error.
 */
struct fy_document_state *
fy_generic_vds_get_document_state(fy_generic vds)
	FY_EXPORT;

/**
 * fy_generic_vds_create_from_document_state() - Bundle a root value and document state into a vds generic.
 *
 * Creates a document-with-state (vds) generic by combining @vroot with @fyds into a
 * mapping generic backed by the builder @gb.
 *
 * @gb:    Builder that will own the resulting vds generic.
 * @vroot: The document root generic.
 * @fyds:  The YAML document state (version, tags, schema information).
 *
 * Returns:
 * The vds fy_generic, or fy_invalid on error.
 */
fy_generic
fy_generic_vds_create_from_document_state(struct fy_generic_builder *gb, fy_generic vroot, struct fy_document_state *fyds)
	FY_EXPORT;

////////////////////////
///
/*
 * fy_generic_dump_primitive() - Dump a generic value as human-readable text to a FILE stream.
 *
 * Recursively prints @v to @fp with indentation proportional to @level.
 * Intended for debugging; output format is not guaranteed to be stable.
 *
 * @fp:    Destination FILE stream.
 * @level: Indentation depth (0 for top-level).
 * @v:     The generic value to dump.
 */
void
fy_generic_dump_primitive(FILE *fp, int level, fy_generic v)
	FY_EXPORT;

////////////////////////

struct fy_generic_iterator;

/* Shift amount of the want mode */
#define FYGICF_WANT_SHIFT		0
/* Mask of the WANT mode */
#define FYGICF_WANT_MASK			((1U << 2) - 1)
/* Build a WANT mode option */
#define FYGICF_WANT(x)			(((unsigned int)(x) & FYGICF_WANT_MASK) << FYGICF_WANT_SHIFT)

/**
 * enum fy_generic_iterator_cfg_flags - Document iterator configuration flags
 *
 * These flags control the operation of the document iterator
 *
 * @FYGICF_WANT_BODY_EVENTS: Generate body events
 * @FYGICF_WANT_DOCUMENT_BODY_EVENTS: Generate document and body events
 * @FYGICF_WANT_STREAM_DOCUMENT_BODY_EVENTS: Generate stream, document and body events
 * @FYGICF_HAS_FULL_DIRECTORY: Full directory contents of vdir
 * @FYGICF_STRIP_LABELS: Strip the labels (anchors)
 * @FYGICF_STRIP_TAGS: Strip the tags
 * @FYGICF_STRIP_COMMENTS: Strip comments from the output
 * @FYGICF_STRIP_STYLE: Strip style information from the output
 * @FYGICF_STRIP_FAILSAFE_STR: Strip failsafe schema plain string tags
 */
enum fy_generic_iterator_cfg_flags {
	FYGICF_WANT_BODY_EVENTS			= FYGICF_WANT(0),
	FYGICF_WANT_DOCUMENT_BODY_EVENTS	= FYGICF_WANT(1),
	FYGICF_WANT_STREAM_DOCUMENT_BODY_EVENTS	= FYGICF_WANT(2),
	FYGICF_HAS_FULL_DIRECTORY		= FY_BIT(2),
	FYGICF_STRIP_LABELS			= FY_BIT(3),
	FYGICF_STRIP_TAGS			= FY_BIT(4),
	FYGICF_STRIP_COMMENTS			= FY_BIT(5),
	FYGICF_STRIP_STYLE			= FY_BIT(6),
	FYGICF_STRIP_FAILSAFE_STR		= FY_BIT(7),
};

/**
 * struct fy_generic_iterator_cfg - document iterator configuration structure.
 *
 * Argument to the fy_generic_iterator_create_cfg() method.
 *
 * @flags: The document iterator flags
 * @vdir: The directory of the parsed input
 */
struct fy_generic_iterator_cfg {
	enum fy_generic_iterator_cfg_flags flags;
	fy_generic vdir;
};

/**
 * fy_generic_iterator_create() - Create a document iterator
 *
 * Creates a document iterator, that can trawl through a document
 * without using recursion.
 *
 * Returns:
 * The newly created document iterator or NULL on error
 */
struct fy_generic_iterator *
fy_generic_iterator_create(void)
	FY_EXPORT;

/**
 * fy_generic_iterator_create_cfg() - Create a document iterator using config
 *
 * Creates a document iterator, that can trawl through a document
 * without using recursion. The iterator will generate all the events
 * that created the given document starting at iterator root.
 *
 * @cfg: The document iterator to destroy
 *
 * Returns:
 * The newly created document iterator or NULL on error
 */
struct fy_generic_iterator *
fy_generic_iterator_create_cfg(const struct fy_generic_iterator_cfg *cfg)
	FY_EXPORT;

/**
 * fy_generic_iterator_destroy() - Destroy the given document iterator
 *
 * Destroy a document iterator created earlier via fy_generic_iterator_create().
 *
 * @fygi: The document iterator to destroy
 */
void
fy_generic_iterator_destroy(struct fy_generic_iterator *fygi)
	FY_EXPORT;

/**
 * fy_generic_iterator_event_free() - Free an event that was created by a document iterator
 *
 * Free (possibly recycling) an event that was created by a document iterator.
 *
 * @fygi: The document iterator that created the event
 * @fye: The event
 */
void
fy_generic_iterator_event_free(struct fy_generic_iterator *fygi, struct fy_event *fye)
	FY_EXPORT;

/**
 * fy_generic_iterator_stream_start() - Create a stream start event using the iterator
 *
 * Creates a stream start event on the document iterator and advances the internal state
 * of it accordingly.
 *
 * @fygi: The document iterator to create the event
 *
 * Returns:
 * The newly created stream start event, or NULL on error.
 */
struct fy_event *
fy_generic_iterator_stream_start(struct fy_generic_iterator *fygi)
	FY_EXPORT;

/**
 * fy_generic_iterator_stream_end() - Create a stream end event using the iterator
 *
 * Creates a stream end event on the document iterator and advances the internal state
 * of it accordingly.
 *
 * @fygi: The document iterator to create the event
 *
 * Returns:
 * The newly created stream end event, or NULL on error.
 */
struct fy_event *
fy_generic_iterator_stream_end(struct fy_generic_iterator *fygi)
	FY_EXPORT;

/**
 * fy_generic_iterator_document_start() - Create a document start event using the iterator
 *
 * Creates a document start event on the document iterator and advances the internal state
 * of it accordingly. The document must not be released until an error, cleanup or a call
 * to fy_generic_iterator_document_end().
 *
 * @fygi: The document iterator to create the event
 * @vds: The generic directory (or fy_null for none)
 *
 * Returns:
 * The newly created document start event, or NULL on error.
 */
struct fy_event *
fy_generic_iterator_document_start(struct fy_generic_iterator *fygi, fy_generic vds)
	FY_EXPORT;

/**
 * fy_generic_iterator_document_end() - Create a document end event using the iterator
 *
 * Creates a document end event on the document iterator and advances the internal state
 * of it accordingly. The document that was used earlier in the call of
 * fy_generic_iterator_document_start() can now be released.
 *
 * @fygi: The document iterator to create the event
 *
 * Returns:
 * The newly created document end event, or NULL on error.
 */
struct fy_event *
fy_generic_iterator_document_end(struct fy_generic_iterator *fygi)
	FY_EXPORT;

/**
 * fy_generic_iterator_body_next() - Create document body events until the end
 *
 * Creates the next document body, depth first until the end of the document.
 * The events are created depth first and are in same exact sequence that the
 * original events that created the document.
 *
 * That means that the finite event stream that generated the document is losslesly
 * preserved in such a way that the document tree representation is functionally
 * equivalent.
 *
 * Repeated calls to this function will generate a stream of SCALAR, ALIAS, SEQUENCE
 * START, SEQUENCE END, MAPPING START and MAPPING END events, returning NULL at the
 * end of the body event stream.
 *
 * @fygi: The document iterator to create the event
 *
 * Returns:
 * The newly created document body event or NULL at an error, or an end of the
 * event stream. Use fy_generic_iterator_get_error() to check if an error occured.
 */
struct fy_event *
fy_generic_iterator_body_next(struct fy_generic_iterator *fygi)
	FY_EXPORT;

/**
 * fy_generic_iterator_generic_start() - Start a document node iteration run using a starting point
 *
 * Starts an iteration run starting at the given node.
 *
 * @fygi: The document iterator to run with
 * @v: The generic to start on
 */
void
fy_generic_iterator_generic_start(struct fy_generic_iterator *fygi, fy_generic v)
	FY_EXPORT;

/**
 * fy_generic_iterator_generic_next() - Return the next node in the iteration sequence
 *
 * Returns a pointer to the next node iterating using as a start the node given
 * at fy_generic_iterator_node_start(). The first node returned will be that,
 * followed by all the remaing nodes in the subtree.
 *
 * @fygi: The document iterator to use for the iteration
 *
 * Returns:
 * The next node in the iteration sequence or NULL at the end, or if an error occured.
 */
fy_generic
fy_generic_iterator_generic_next(struct fy_generic_iterator *fygi)
	FY_EXPORT;

/**
 * fy_generic_iterator_generate_next() - Create events from document iterator
 *
 * This is a method that will handle the complex state of generating
 * stream, document and body events on the given iterator.
 *
 * When generation is complete a NULL event will be generated.
 *
 * @fygi: The document iterator to create the event
 *
 * Returns:
 * The newly created event or NULL at an error, or an end of the
 * event stream. Use fy_generic_iterator_get_error() to check if an error occured.
 */
struct fy_event *
fy_generic_iterator_generate_next(struct fy_generic_iterator *fygi)
	FY_EXPORT;

/**
 * fy_generic_iterator_get_error() - Get the error state of the document iterator
 *
 * Returns the error state of the iterator. If it's in error state, return true
 * and reset the iterator to the state just after creation.
 *
 * @fygi: The document iterator to use for checking it's error state.
 *
 * Returns:
 * true if it was in an error state, false otherwise.
 */
bool
fy_generic_iterator_get_error(struct fy_generic_iterator *fygi)
	FY_EXPORT;


/**
 * fy_parser_set_generic_iterator() - Associate a parser with a document iterator
 *
 * Associate a parser with a generic iterator, that is instead of parsing the events
 * will be generated by the generic iterator.
 *
 * @fyp: The parser
 * @flags: The event generation flags
 * @fygi: The document iterator to associate
 *
 * Returns:
 * 0 on success, -1 on error
 */
int
fy_parser_set_generic_iterator(struct fy_parser *fyp, enum fy_parser_event_generator_flags flags,
				struct fy_generic_iterator *fygi)
	FY_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* LIBFYAML_GENERIC_H */
