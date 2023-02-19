/*
 * fy-packed-backend.h - Packed blob reflection backend header
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_PACKED_BACKEND_H
#define FY_PACKED_BACKEND_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "fy-reflection-private.h"

/* packed structure */

struct fy_type_p;
struct fy_decl_p;

typedef union {
	const struct fy_decl_p *declp;
	int id;
} fy_decl_p_id;

typedef union {
	const struct fy_type_p *fytp;
	int id;
} fy_type_p_id;

typedef union {
	const char *str;
	unsigned int offset;
} fy_p_str;

typedef union {
	intmax_t s;
	uintmax_t u;
} fy_p_enum_val;

struct fy_type_p {
	enum fy_type_kind type_kind;
	enum fy_type_flags flags;
	fy_decl_p_id decl;
	fy_type_p_id dependent_type;
	uintmax_t element_count;
};

struct fy_decl_p {
	enum fy_decl_type decl_type;
	enum fy_decl_flags flags;
	fy_p_str name;
	fy_type_p_id type;
	fy_p_str comment;
	union {
		fy_p_enum_val enum_value;
		size_t bit_width;
	};
};

struct fy_packed_type_info {
	bool uses_pointers;
	const struct fy_type_p *types;
	int types_count;
	const struct fy_decl_p *decls;
	int decls_count;
	const char *strtab;
	size_t strtab_size;
};

static inline const struct fy_type_p *
fy_type_p_from_id(const struct fy_packed_type_info *ti, const fy_type_p_id id)
{
	unsigned int idx;

	if (ti->uses_pointers) {
		assert(!id.fytp || (id.fytp >= ti->types && id.fytp < &ti->types[ti->types_count]));
		return id.fytp;
	}
	idx = id.id - FY_TYPE_ID_OFFSET;
	if ((unsigned int)idx >= (unsigned int)ti->types_count)
		return NULL;
	return ti->types + idx;
}

static inline const struct fy_decl_p *
fy_decl_p_from_id(const struct fy_packed_type_info *ti, const fy_decl_p_id id)
{
	unsigned int idx;

	if (ti->uses_pointers) {
		assert(!id.declp || (id.declp >= ti->decls && id.declp < &ti->decls[ti->decls_count]));
		return id.declp;
	}
	idx = id.id - FY_DECL_ID_OFFSET;
	if ((unsigned int)idx >= (unsigned int)ti->decls_count)
		return NULL;
	return ti->decls + idx;
}

static inline const char *
fy_str_from_p(const struct fy_packed_type_info *ti, fy_p_str strp)
{
	if (ti->uses_pointers)
		return strp.str;

	if (!strp.offset)
		return NULL;

	assert(strp.offset < ti->strtab_size);

	return ti->strtab + strp.offset;
}

static inline const char *
fy_decl_p_name(const struct fy_packed_type_info *ti, const struct fy_decl_p *declp)
{
	return fy_str_from_p(ti, declp->name);
}

static inline const char *
fy_type_p_name(const struct fy_packed_type_info *ti, const struct fy_type_p *fytp)
{
	const struct fy_decl_p *declp;

	if (fy_type_kind_is_primitive(fytp->type_kind))
		return fy_type_kind_info_get_internal(fytp->type_kind)->name;

	declp = fy_decl_p_from_id(ti, fytp->decl);
	assert(declp);
	return declp ? fy_decl_p_name(ti, declp) : "";
}

enum fy_packed_reflection_type {
	FYPRT_TYPE_INFO,
	FYPRT_BLOB,
};

struct fy_packed_backend_reflection_cfg {
	enum fy_packed_reflection_type type;
	union {
		const struct fy_packed_type_info *type_info;
		struct {
			const void *blob;
			size_t blob_size;
			bool copy;
		};
	};
};

struct fy_packed_backend_import_cfg {
	/* nothing, there is only a single import per this backend */
};

extern const struct fy_reflection_backend fy_reflection_packed_backend;

enum fy_packed_generator_type {
	FYPGT_TO_FILE,
	FYPGT_TO_STRING,
	FYPGT_BLOB,
};

/* format of "BLOB"
 *
 * All larger than 8 bit values are in little-endian
 *
 * [Header]
 * [TypeEntries]
 * [DeclEntries]
 *
 * Header format:
 * [00-03] 'F', 'Y' 'P' 'G'
 * [04-04] Major version number of the format
 * [05-05] Minor version number of the format
 * [06-06] T Size of type ids (0=8bit, 1=16bit, 2=32bit, 3=64bit)
 * [07-07] D Size of decl ids (0=8bit, 1=16bit, 2=32bit, 3=64bit)
 * [08-08] S String offset count size (0=8bit, 1=16bit, 2=32bit, 3=64bit)
 * [09-0f] Reserved
 * [10-17] Type table # entries
 * [10-18] Type table size in bytes
 * [20-27] Decl table # entries
 * [28-2f] Decl table size in bytes
 * [30-37] Size of string table
 * [37-3f] Reserved
 * Total size = 0x40 bytes
 *
 * D = Decl ID size in bytes
 * T = Type ID size in bytes
 * S = String table offset in bytes
 * C = Element count size in bytes
 * V = Enum value size in bytes
 *
 * Type table entries format
 *    offset size description
 * --------- ---- -----------
 *         0    1 [0:4] type_kind | constant_array ? [5:6] size of C in BID_ : [7] elaborated type
 *         1    1 [0:2] CONST,VOLATILE,RESTRICT
 *         1    D decl.id (if it exists for the given type)
 *       2+D    T dependent_type.id (if it exists for the given type)
 *     2+D+T    C element count (if it exists for the given type)
 * ---------
 * T+1+D+T+C max size of type entry
 * 2*T+D+C+1
 *
 * Decl table entries format
 *    offset size description
 * ---------- ---- -----------
 *          0    1 decl_type[0:3] | enum_value ? [4:5]size of V in BID_
 *          1    T type.id
 *        1+T    S name - string table offset (0 if anonymous or can be generated)
 *      1+T+S |  1 bit_width if bitfield
 *      1+T+S |  V enum_value if enum_value
 * 1+T+S+[V|1]   S comment - string table offset
 * ----------
 * 1+T+S+[V|1]+S maximum size of entry
 *
 */

#define PGHDR_SIZE	0x40

/* packed type flags */
#define PGTF_TYPE_KIND_SHIFT	0
#define PGTF_TYPE_KIND_WIDTH	FYTK_BITS
#define PGTF_TYPE_KIND_MASK	(((1U << PGTF_TYPE_KIND_WIDTH) - 1) << PGTF_TYPE_KIND_SHIFT)

#define PGTF_ELEM_SIZE_SHIFT	(PGTF_TYPE_KIND_SHIFT + PGTF_TYPE_KIND_WIDTH)
#define PGTF_ELEM_SIZE_WIDTH	2
#define PGTF_ELEM_SIZE_MASK	(((1U << PGTF_ELEM_SIZE_WIDTH) - 1) << PGTF_ELEM_SIZE_SHIFT)

#define PGTF_ELEM_SIZE_U8	(0 << PGTF_ELEM_SIZE_SHIFT)
#define PGTF_ELEM_SIZE_U16	(1 << PGTF_ELEM_SIZE_SHIFT)
#define PGTF_ELEM_SIZE_U32	(2 << PGTF_ELEM_SIZE_SHIFT)
#define PGTF_ELEM_SIZE_U64	(3 << PGTF_ELEM_SIZE_SHIFT)

#define PGTF_EXTFLAGS_SHIFT	(PGTF_ELEM_SIZE_SHIFT + PGTF_ELEM_SIZE_WIDTH)
#define PGTF_EXTFLAGS_WIDTH	1
#define PGTF_EXTFLAGS		(1 << PGTF_EXTFLAGS_SHIFT)

#if PTTF_ELEM_SIZE_SHIFT + PGTF_ELEM_SIZE_WIDTH	+ PGTF_EXTFLAGS_WIDTH	> 8
#error Bad PGTF_ELEM_SIZE_SHIFT	value
#endif

/* packed decl flags */
#define PGDF_DECL_TYPE_SHIFT		0
#define PGDF_DECL_TYPE_WIDTH		FYDT_BITS
#define PGDF_DECL_TYPE_MASK		(((1U << PGDF_DECL_TYPE_WIDTH) - 1) << PGDF_DECL_TYPE_SHIFT)

#define PGDF_ENUM_VALUE_SIZE_SHIFT	(PGDF_DECL_TYPE_SHIFT + PGDF_DECL_TYPE_WIDTH)
#define PGDF_ENUM_VALUE_SIZE_WIDTH	2
#define PGDF_ENUM_VALUE_SIZE_MASK	(((1U << PGDF_ENUM_VALUE_SIZE_WIDTH) - 1) << PGDF_ENUM_VALUE_SIZE_SHIFT)

#define PGDF_ENUM_VALUE_SIZE_U8		(0 << PGDF_ENUM_VALUE_SIZE_SHIFT)
#define PGDF_ENUM_VALUE_SIZE_U16	(1 << PGDF_ENUM_VALUE_SIZE_SHIFT)
#define PGDF_ENUM_VALUE_SIZE_U32	(2 << PGDF_ENUM_VALUE_SIZE_SHIFT)
#define PGDF_ENUM_VALUE_SIZE_U64	(3 << PGDF_ENUM_VALUE_SIZE_SHIFT)

#define PGDF_ENUM_VALUE_SIGNED_SHIFT	(PGDF_ENUM_VALUE_SIZE_SHIFT + PGDF_ENUM_VALUE_SIZE_WIDTH)
#define PGDF_ENUM_VALUE_SIGNED		(1 << PGDF_ENUM_VALUE_SIGNED_SHIFT)

#if (PGDF_ENUM_VALUE_SIGNED_SHIFT + 1) > 8
#error Bad PGDF_ENUM_VALUE_SIZE_SHIFT
#endif

struct fy_packed_generator {
	struct fy_reflection *rfl;
	enum fy_packed_generator_type type;
	union {
		struct {
			bool use_static;
			const char *decls_name;
			const char *types_name;
			const char *type_info_name;
			union {
				FILE *fp;
				struct {
					char **strp;
					size_t *str_sizep;
				};
			};
		};
		struct {
			void **blobp;
			size_t *blob_sizep;
		};
	};
};

int fy_packed_generate(struct fy_packed_generator *pg);

#endif
