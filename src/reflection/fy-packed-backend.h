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
	long long s;
	unsigned long long u;
} fy_p_enum_val;

#define FYTPF_CONST		0x01
#define FYTPF_VOLATILE		0x02
#define FYTPF_RESTRICT		0x04
#define FYTPF_UNRESOLVED	0x08	/* when this is unresolved dummy type */

struct fy_type_p {
	enum fy_type_kind type_kind;
	unsigned int flags;
	fy_decl_p_id decl;
	fy_type_p_id dependent_type;
	unsigned long long element_count;
};

struct fy_decl_p {
	enum fy_decl_type decl_type;
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
	if (ti->uses_pointers) {
		assert(!id.fytp || (id.fytp >= ti->types && id.fytp < &ti->types[ti->types_count]));
		return id.fytp;
	}
	assert((unsigned int)id.id < (unsigned int)ti->types_count);
	return ti->types + id.id;
}

static inline const struct fy_decl_p *
fy_decl_p_from_id(const struct fy_packed_type_info *ti, const fy_decl_p_id id)
{
	if (ti->uses_pointers) {
		assert(!id.declp || (id.declp >= ti->decls && id.declp < &ti->decls[ti->decls_count]));
		return id.declp;
	}
	assert((unsigned int)id.id < (unsigned int)ti->decls_count);
	return ti->decls + id.id;
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
 * [09-09] C element_count size (0=8bit, 1=16bit, 2=32bit, 3=64bit)
 * [0a-0a] V enum value size (0=8bit, 1=16bit, 2=32bit, 3=64bit)
 * [0a-0f] Reserved
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
 *         0    T type_kind
 *         T    1 flags
 *                - bit 0 -> const
 *                - bit 1 -> volatile
 *                - bit 2 -> restrict
 *                - bit 3 -> decl.id exists
 *                - bit 4 -> dependent_type.id exists
 *                - bit 5 -> element_count exists
 *       T+1    D decl.id (if exists)
 *     T+1+D    T dependent_type.id (if exists)
 *   T+1+D+T    C element count (if exists)
 * ---------
 * T+1+D+T+C max size of type entry
 * 2*T+D+C+1
 *
 * Decl table entries format
 *    offset size description
 * ---------- ---- -----------
 *          0    1 decl_type
 *          1    T type.id
 *        1+T    S name - string table offset
 *      1+T+S |  1 bit_width if bitfield
 *      1+T+S |  V enum_value if enum_value
 * 1+T+S+[V|1]   S comment - string table offset
 * ----------
 * 1+T+S+[V|1]+S maximum size of entry
 *
 */

#define PGHDR_SIZE	0x40

#define PGTF_CONST	0x01
#define PGTF_VOLATILE	0x02
#define PGTF_RESTRICT	0x04
#define PGTF_DECL	0x08
#define PGTF_DEP	0x10
#define PGTF_ECOUNT	0x20
#define PGTF_LOCATION	0x40	/* for future extension */

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
