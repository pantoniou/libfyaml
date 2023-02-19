/*
 * fy-reflection.c - Generic type reflection library
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <alloca.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <float.h>
#include <assert.h>
#include <limits.h>
#include <ctype.h>

#include "fy-utf8.h"
#include "fy-ctype.h"
#include "fy-reflection-private.h"

static struct fy_document *get_yaml_document(const char *cooked_comment);

static void fy_type_reset_type_info(struct fy_type *ft);
static struct fy_type_info *fy_type_get_type_info(struct fy_type *ft);

static inline int backend_reflection_setup(struct fy_reflection *rfl)
{
	return rfl->cfg.backend->ops->reflection_setup(rfl);
}

static inline void backend_reflection_cleanup(struct fy_reflection *rfl)
{
	rfl->cfg.backend->ops->reflection_cleanup(rfl);
}

static inline int backend_import_setup(struct fy_import *imp, const void *user)
{
	return imp->rfl->cfg.backend->ops->import_setup(imp, user);
}

static inline void backend_import_cleanup(struct fy_import *imp)
{
	imp->rfl->cfg.backend->ops->import_cleanup(imp);
}

static inline int backend_type_setup(struct fy_type *ft, void *user)
{
	return ft->rfl->cfg.backend->ops->type_setup(ft, user);
}

static inline void backend_type_cleanup(struct fy_type *ft)
{
	ft->rfl->cfg.backend->ops->type_cleanup(ft);
}

static inline int backend_decl_setup(struct fy_decl *decl, void *user)
{
	return decl->imp->rfl->cfg.backend->ops->decl_setup(decl, user);
}

static inline void backend_decl_cleanup(struct fy_decl *decl)
{
	decl->imp->rfl->cfg.backend->ops->decl_cleanup(decl);
}

const struct fy_decl_type_info fy_decl_type_info_table[FYDT_COUNT] = {
	[FYDT_NONE] = {
		.type = FYDT_NONE,
		.name = "none",
		.enum_name = "FYDT_NONE",
	},
	[FYDT_STRUCT] = {
		.type = FYDT_STRUCT,
		.name = "struct",
		.enum_name = "FYDT_STRUCT",
	},
	[FYDT_UNION] = {
		.type = FYDT_UNION,
		.name = "union",
		.enum_name = "FYDT_UNION",
	},
	[FYDT_CLASS] = {
		.type = FYDT_CLASS,
		.name = "class",
		.enum_name = "FYDT_CLASS",
	},
	[FYDT_ENUM] = {
		.type = FYDT_ENUM,
		.name = "enum",
		.enum_name = "FYDT_ENUM",
	},
	[FYDT_TYPEDEF] = {
		.type = FYDT_TYPEDEF,
		.name = "typedef",
		.enum_name = "FYDT_TYPEDEF",
	},
	[FYDT_FUNCTION] = {
		.type = FYDT_FUNCTION,
		.name = "function",
		.enum_name = "FYDT_FUNCTION",
	},
	[FYDT_FIELD] = {
		.type = FYDT_FIELD,
		.name = "field",
		.enum_name = "FYDT_FIELD",
	},
	[FYDT_BITFIELD] = {
		.type = FYDT_BITFIELD,
		.name = "bit-field",
		.enum_name = "FYDT_BITFIELD",
	},
	[FYDT_ENUM_VALUE] = {
		.type = FYDT_ENUM_VALUE,
		.name = "enum-value",
		.enum_name = "FYDT_ENUM_VALUE",
	},
};

const struct fy_type_kind_info fy_type_kind_info_table[FYTK_COUNT] = {
	[FYTK_INVALID] = {
		.kind		= FYTK_INVALID,
		.name		= "*invalid*",
		.enum_name	= "FYTK_INVALID",
	},
	[FYTK_VOID] = {
		.kind		= FYTK_VOID,
		.name		= "void",
		.size		= 0,
		.align		= 0,
		.enum_name	= "FYTK_VOID",
	},
	[FYTK_BOOL] = {
		.kind		= FYTK_BOOL,
		.name		= "_Bool",
		.size		= sizeof(_Bool),
		.align		= alignof(_Bool),
		.enum_name	= "FYTK_BOOL",
	},
	[FYTK_CHAR] = {
		.kind		= FYTK_CHAR,
		.name		= "char",
		.size		= sizeof(char),
		.align		= alignof(char),
		.enum_name	= "FYTK_CHAR",
	},
	[FYTK_SCHAR] = {
		.kind		= FYTK_SCHAR,
		.name		= "signed char",
		.size		= sizeof(signed char),
		.align		= alignof(signed char),
		.enum_name	= "FYTK_SCHAR",
	},
	[FYTK_UCHAR] = {
		.kind		= FYTK_UCHAR,
		.name		= "unsigned char",
		.size		= sizeof(unsigned char),
		.align		= alignof(unsigned char),
		.enum_name	= "FYTK_UCHAR",
	},
	[FYTK_SHORT] = {
		.kind		= FYTK_SHORT,
		.name		= "short",
		.size		= sizeof(short),
		.align		= alignof(short),
		.enum_name	= "FYTK_SHORT",
	},
	[FYTK_USHORT] = {
		.kind		= FYTK_USHORT,
		.name		= "unsigned short",
		.size		= sizeof(unsigned short),
		.align		= alignof(unsigned short),
		.enum_name	= "FYTK_USHORT",
	},
	[FYTK_INT] = {
		.kind		= FYTK_INT,
		.name		= "int",
		.size		= sizeof(int),
		.align		= alignof(int),
		.enum_name	= "FYTK_INT",
	},
	[FYTK_UINT] = {
		.kind		= FYTK_UINT,
		.name		= "unsigned int",
		.size		= sizeof(unsigned int),
		.align		= alignof(unsigned int),
		.enum_name	= "FYTK_UINT",
	},
	[FYTK_LONG] = {
		.kind		= FYTK_LONG,
		.name		= "long",
		.size		= sizeof(long),
		.align		= alignof(long),
		.enum_name	= "FYTK_LONG",
	},
	[FYTK_ULONG] = {
		.kind		= FYTK_ULONG,
		.name		= "unsigned long",
		.size		= sizeof(unsigned long),
		.align		= alignof(unsigned long),
		.enum_name	= "FYTK_ULONG",
	},
	[FYTK_LONGLONG] = {
		.kind		= FYTK_LONGLONG,
		.name		= "long long",
		.size		= sizeof(long long),
		.align		= alignof(long long),
		.enum_name	= "FYTK_LONGLONG",
	},
	[FYTK_ULONGLONG] = {
		.kind		= FYTK_ULONGLONG,
		.name		= "unsigned long long",
		.size		= sizeof(unsigned long),
		.align		= alignof(unsigned long),
		.enum_name	= "FYTK_ULONGLONG",
	},
#if defined(__SIZEOF_INT128__) && __SIZEOF_INT128__ == 16
	[FYTK_INT128] = {
		.kind		= FYTK_INT128,
		.name		= "__int128",
		.size		= sizeof(__int128),
		.align		= alignof(__int128),
		.enum_name	= "FYTK_INT128",
	},
	[FYTK_UINT128] = {
		.kind		= FYTK_UINT128,
		.name		= "unsigned __int128",
		.size		= sizeof(unsigned __int128),
		.align		= alignof(unsigned __int128),
		.enum_name	= "FYTK_UINT128",
	},
#else
	[FYTK_INT128] = {
		.kind		= FYTK_INVALID,
		.name		= "__int128",
		.enum_name	= "FYTK_INT128",
	},
	[FYTK_UINT128] = {
		.kind		= FYTK_INVALID,
		.name		= "unsigned __int128",
		.enum_name	= "FYTK_UINT128",
	},
#endif
	[FYTK_FLOAT] = {
		.kind		= FYTK_FLOAT,
		.name		= "float",
		.size		= sizeof(float),
		.align		= alignof(float),
		.enum_name	= "FYTK_FLOAT",
	},
	[FYTK_DOUBLE] = {
		.kind		= FYTK_DOUBLE,
		.name		= "double",
		.size		= sizeof(double),
		.align		= alignof(double),
		.enum_name	= "FYTK_DOUBLE",
	},
	[FYTK_LONGDOUBLE] = {
		.kind		= FYTK_LONGDOUBLE,
		.name		= "long double",
		.size		= sizeof(double),
		.align		= alignof(double),
		.enum_name	= "FYTK_LONGDOUBLE",
	},
#ifdef FY_HAS_FP16
	[FYTK_FLOAT16] = {
		.kind		= FYTK_FLOAT16,
		.name		= "__fp16",
		.size		= sizeof(__fp16),
		.align		= alignof(__fp16),
		.enum_name	= "FYTK_FLOAT16",
	},
#else
	[FYTK_FLOAT16] = {
		.kind		= FYTK_INVALID,
		.name		= "__fp16",
		.enum_name	= "FYTK_FLOAT16",
	},
#endif
#ifdef FY_HAS_FLOAT128
	[FYTK_FLOAT128] = {
		.kind		= FYTK_FLOAT128,
		.name		= "__float128",
		.size		= sizeof(__float128),
		.align		= alignof(__float128),
		.enum_name	= "FYTK_FLOAT128",
	},
#else
	[FYTK_FLOAT128] = {
		.kind		= FYTK_INVALID,
		.name		= "__float128",
		.enum_name	= "FYTK_FLOAT128",
	},
#endif
	/* the explicitly sized types are not generated */
	/* they must be explicitly created */
	[FYTK_S8] = {
		.kind		= FYTK_S8,
		.name		= "int8_t",
		.size		= sizeof(int8_t),
		.align		= alignof(int8_t),
		.enum_name	= "FYTK_S8",
	},
	[FYTK_U8] = {
		.kind		= FYTK_U8,
		.name		= "uint8_t",
		.size		= sizeof(uint8_t),
		.align		= alignof(uint8_t),
		.enum_name	= "FYTK_U8",
	},
	[FYTK_S16] = {
		.kind		= FYTK_S16,
		.name		= "int16_t",
		.size		= sizeof(int16_t),
		.align		= alignof(int16_t),
		.enum_name	= "FYTK_S16",
	},
	[FYTK_U16] = {
		.kind		= FYTK_U16,
		.name		= "uint16_t",
		.size		= sizeof(uint16_t),
		.align		= alignof(uint16_t),
		.enum_name	= "FYTK_U16",
	},
	[FYTK_S32] = {
		.kind		= FYTK_S32,
		.name		= "int32_t",
		.size		= sizeof(int32_t),
		.align		= alignof(int32_t),
		.enum_name	= "FYTK_S32",
	},
	[FYTK_U32] = {
		.kind		= FYTK_U32,
		.name		= "uint32_t",
		.size		= sizeof(uint32_t),
		.align		= alignof(uint32_t),
		.enum_name	= "FYTK_U32",
	},
	[FYTK_S64] = {
		.kind		= FYTK_S64,
		.name		= "int64_t",
		.size		= sizeof(int64_t),
		.align		= alignof(int64_t),
		.enum_name	= "FYTK_S64",
	},
	[FYTK_U64] = {
		.kind		= FYTK_U64,
		.name		= "uint64_t",
		.size		= sizeof(uint64_t),
		.align		= alignof(uint64_t),
		.enum_name	= "FYTK_U64",
	},
#ifdef FY_HAS_INT128
	[FYTK_S128] = {
		.kind		= FYTK_S128,
		.name		= "__int128",
		.size		= sizeof(__int128),
		.align		= alignof(__int128),
		.enum_name	= "FYTK_S128",
	},
	[FYTK_U128] = {
		.kind		= FYTK_U128,
		.name		= "unsigned __int128",
		.size		= sizeof(unsigned __int128),
		.align		= alignof(unsigned __int128),
		.enum_name	= "FYTK_U128",
	},
#else
	[FYTK_S128] = {
		.kind		= FYTK_INVALID,
		.name		= "__int128",
		.enum_name	= "FYTK_S128",
	},
	[FYTK_U128] = {
		.kind		= FYTK_INVALID,
		.name		= "unsigned __int128",
		.enum_name	= "FYTK_U128",
	},
#endif

	/* these are templates */
	[FYTK_RECORD] = {
		.kind		= FYTK_RECORD,
		.name		= "<record>",
		.enum_name	= "FYTK_RECORD",
	},
	[FYTK_STRUCT] = {
		.kind		= FYTK_STRUCT,
		.name		= "struct",
		.enum_name	= "FYTK_STRUCT",
	},
	[FYTK_UNION] = {
		.kind		= FYTK_UNION,
		.name		= "union",
		.enum_name	= "FYTK_UNION",
	},
	[FYTK_ENUM] = {
		.kind		= FYTK_ENUM,
		.name		= "enum",
		.enum_name	= "FYTK_ENUM",
	},
	[FYTK_TYPEDEF] = {
		.kind		= FYTK_TYPEDEF,
		.name		= "typedef",
		.enum_name	= "FYTK_TYPEDEF",
	},
	[FYTK_PTR] = {
		.kind		= FYTK_PTR,
		.name		= "ptr",
		.enum_name	= "FYTK_PTR",
	},
	[FYTK_CONSTARRAY] = {
		.kind		= FYTK_CONSTARRAY,
		.name		= "carray",
		.enum_name	= "FYTK_CONSTARRAY",
	},
	[FYTK_INCOMPLETEARRAY] = {
		.kind		= FYTK_INCOMPLETEARRAY,
		.name		= "iarray",
		.enum_name	= "FYTK_INCOMPLETEARRAY",
	},
	[FYTK_FUNCTION] = {
		.kind		= FYTK_FUNCTION,
		.name		= "func",
		.enum_name	= "FYTK_FUNCTION",
		.size		= 1,			// fake size and align numbers
		.align		= alignof(int),
	},
};

const struct fy_type_kind_info *
fy_type_kind_info_get(enum fy_type_kind type_kind)
{
	if (!fy_type_kind_is_valid(type_kind))
		return NULL;
	return fy_type_kind_info_get_internal(type_kind);
}

int fy_type_kind_signess(enum fy_type_kind type_kind)
{
	if (!fy_type_kind_is_numeric(type_kind))
		return 0;

	switch (type_kind) {
	case FYTK_CHAR:
		return CHAR_MIN < 0 ? -1 : 1;
	case FYTK_SCHAR:
	case FYTK_SHORT:
	case FYTK_INT:
	case FYTK_LONG:
	case FYTK_LONGLONG:
	case FYTK_INT128:
	case FYTK_FLOAT:
	case FYTK_DOUBLE:
	case FYTK_LONGDOUBLE:
	case FYTK_FLOAT16:
	case FYTK_FLOAT128:
	case FYTK_S8:
	case FYTK_S16:
	case FYTK_S32:
	case FYTK_S64:
	case FYTK_S128:
		return -1;
	case FYTK_BOOL:
	case FYTK_UCHAR:
	case FYTK_USHORT:
	case FYTK_UINT:
	case FYTK_ULONG:
	case FYTK_ULONGLONG:
	case FYTK_UINT128:
	case FYTK_U8:
	case FYTK_U16:
	case FYTK_U32:
	case FYTK_U64:
	case FYTK_U128:
		return 1;
	default:
		break;
	}
	return 0;
}

const char *decl_type_txt[FYDT_COUNT] = {
	[FYDT_NONE]	= "none",
	[FYDT_STRUCT]	= "struct",
	[FYDT_UNION]	= "union",
	[FYDT_CLASS]	= "class",
	[FYDT_ENUM]	= "enum",
	[FYDT_TYPEDEF]	= "typedef",
	[FYDT_FUNCTION]	= "function",
	[FYDT_FIELD]	= "field",
	[FYDT_BITFIELD]	= "bit-field",
	[FYDT_ENUM_VALUE]= "enum-value",
};

void fy_type_destroy(struct fy_type *ft)
{
	struct fy_reflection *rfl;

	if (!ft)
		return;

	fy_type_reset_type_info(ft);

	rfl = ft->rfl;

	/* if we're deleting an unresolved type, decrease the counter */
	if (!fy_type_is_resolved(ft))
		rfl->unresolved_types_count--;

	backend_type_cleanup(ft);

	if (ft->fake_resolve_data)
		free(ft->fake_resolve_data);
	if (ft->fullname)
		free(ft->fullname);
	if (ft->normalized_name)
		free(ft->normalized_name);
	free(ft);
}

struct fy_type *fy_type_create(struct fy_reflection *rfl, enum fy_type_kind type_kind, const char *name, struct fy_decl *decl, void *user)
{
	const char *pfx;
	struct fy_type *ft;
	int rc;

	/* guard against rollover */
	if (rfl->next_type_id + 1 <= 0)
		return NULL;

	ft = malloc(sizeof(*ft));
	if (!ft)
		goto err_out;
	memset(ft, 0, sizeof(*ft));

	ft->rfl = rfl;
	ft->type_kind = type_kind;

	if (fy_type_kind_has_prefix(type_kind)) {
		pfx = fy_type_kind_info_get_internal(type_kind)->name;
		rc = asprintf(&ft->fullname, "%s %s", pfx, name);
		if (rc < 0)
			goto err_out;
		ft->name = ft->fullname + strlen(pfx) + 1;
	} else {
		ft->fullname = strdup(name);
		if (!ft->fullname)
			goto err_out;
		ft->name = ft->fullname;
	}
	if (!ft->name)
		goto err_out;
	ft->normalized_name = fy_type_name_normalize(ft->name);
	if (!ft->normalized_name)
		goto err_out;

	ft->decl = decl;

	rc = backend_type_setup(ft, user);
	if (rc)
		goto err_out;

	assert(rfl->next_type_id >= 0);
	ft->id = rfl->next_type_id++;

	/* for the type to be anonymous it must be a non primitive
	 * type */
	if (decl && !fy_type_kind_is_primitive(ft->type_kind))
		ft->anonymous = decl->anonymous;

	return ft;
err_out:
	fy_type_destroy(ft);
	return NULL;
}

void fy_type_clear_marker(struct fy_type *ft)
{
	if (!ft)
		return;

	ft->marker = false;
}

void fy_type_mark(struct fy_type *ft)
{
	if (!ft || ft->marker)
		return;

	ft->marker = true;

	if (ft->decl)
		fy_decl_mark(ft->decl);
	if (ft->dependent_type)
		fy_type_mark(ft->dependent_type);
}

void fy_type_fixup_size_align(struct fy_type *ft)
{
	struct fy_type *ftc;
	enum fy_type_kind type_kind;
	struct fy_decl *declc, *decl;
	size_t bit_offset, bit_size, bit_align, max_align, max_size, max_bit_offset, bit_width;
	bool is_bitfield, last_was_bitfield, is_first_field;

	if (!ft || ft->is_fixed || ft->is_synthetic)
		return;

	/* check for recursive fix, and break out */
	if (ft->fix_in_progress)
		goto out;

	ft->fix_in_progress = true;

	type_kind = ft->type_kind;
	/* invalid or function don't have sizes */
	if (type_kind == FYTK_INVALID || type_kind == FYTK_FUNCTION)
		goto out;

	/* primitives have the primitive sizes */
	if (fy_type_kind_is_primitive(type_kind)) {
		ft->size = fy_type_kind_info_get_internal(type_kind)->size;
		ft->align = fy_type_kind_info_get_internal(type_kind)->align;
		goto out;
	}

	/* special handling for empty structs/unions */
	if (fy_type_kind_is_record(type_kind)) {
		decl = ft->decl;
		assert(decl);
		if (fy_decl_list_empty(&decl->children))
			;
	}

	/* for the rest, if size, align are set don't try again */
	if (ft->size && ft->align)
		goto out;

	switch (type_kind) {
	case FYTK_ENUM:
	case FYTK_TYPEDEF:
		assert(ft->dependent_type);
		fy_type_fixup_size_align(ft->dependent_type);
		ft->size = ft->dependent_type->size;
		ft->align = ft->dependent_type->align;
		break;

	case FYTK_PTR:
		assert(ft->dependent_type);
		fy_type_fixup_size_align(ft->dependent_type);
		/* the sizes are always the same as a void pointer */
		ft->size = sizeof(void *);
		ft->align = alignof(void *);
		break;

	case FYTK_INCOMPLETEARRAY:
		assert(ft->dependent_type);
		fy_type_fixup_size_align(ft->dependent_type);
		/* size is 0, but align is that of the underlying type */
		ft->size = 0;
		ft->align = ft->dependent_type->align;
		break;

	case FYTK_CONSTARRAY:
		assert(ft->dependent_type);
		fy_type_fixup_size_align(ft->dependent_type);
		/* size is the multiple of the element count */
		ft->size = ft->dependent_type->size * ft->element_count;
		ft->align = ft->dependent_type->align;
		break;

	case FYTK_STRUCT:
	case FYTK_UNION:
		decl = ft->decl;
		assert(decl);
		ft->size = ft->align = 0;
		bit_offset = max_bit_offset = 0;
		max_align = max_size = 0;
		last_was_bitfield = false;
		is_first_field = true;

		for (declc = fy_decl_list_head(&decl->children); declc; declc = fy_decl_next(&decl->children, declc)) {

			/* for unions we need to rewind each time */
			if (type_kind == FYTK_UNION)
				bit_offset = 0;

			ftc = declc->type;
			assert(ftc);
			fy_type_fixup_size_align(ftc);

			bit_align = ftc->align * 8;
			bit_size = ftc->size * 8;

			is_bitfield = declc->decl_type == FYDT_BITFIELD;

			if (!is_bitfield) {

				/* keep track of maximum alignment */
				if (max_align < ftc->align)
					max_align = ftc->align;

				/* advance bit_offset to byte */
				if (last_was_bitfield)
					bit_offset = (bit_offset + 8 - 1) & ~(8 - 1);

				/* align to given align */
				bit_offset = (bit_offset + bit_align - 1) & ~(bit_align - 1);

				/* store byte offset */
				if (is_first_field) {
					/* first field must always have 0 offset */
					assert(decl->field_decl.byte_offset == 0);
					decl->field_decl.byte_offset = 0;
				} else {
					/* if there is a configured offset check against it */
					/* it must always match */
					if (declc->field_decl.byte_offset)
						assert(declc->field_decl.byte_offset == bit_offset / 8);
					declc->field_decl.byte_offset = bit_offset / 8;
				}

				/* advance and align */
				bit_offset += bit_size;
				bit_offset = (bit_offset + bit_align - 1) & ~(bit_align - 1);
			} else {
				/* XXX probably needs a per target config here, everything is implementation defined */

				bit_width = declc->bitfield_decl.bit_width;

				/* the byte width of the type must be less or equal of the underlying type */
				assert(bit_width <= bit_size);

				/* special unnamed bitfield, align to natural boundary */
				if (bit_width == 0)
					bit_offset = (bit_offset + bit_align - 1) & ~(bit_align - 1);

				/* store the bit-offset */
				declc->bitfield_decl.bit_offset = bit_offset;

				/* advance */
				bit_offset += bit_width;
			}

			/* update bit offset */
			if (max_bit_offset < bit_offset)
				max_bit_offset = bit_offset;

			last_was_bitfield = is_bitfield;
			is_first_field = false;

		}

		/* save max align */
		ft->align = max_align;

		/* align to byte at the end */
		if (last_was_bitfield)
			bit_offset = (bit_offset + 8 - 1) & ~(8 - 1);

		/* align to maximum align */
		bit_align = ft->align * 8;
		bit_offset = (bit_offset + bit_align - 1) & ~(bit_align - 1);

		/* save max bit offset */
		if (max_bit_offset < bit_offset)
			max_bit_offset = bit_offset;

		/* the size is the maximum */
		ft->size = max_bit_offset / 8;
		break;

	default:
		break;
	}

out:
	ft->is_fixed = true;
	ft->fix_in_progress = false;
}

void fy_reflection_fixup_size_align(struct fy_reflection *rfl)
{
	struct fy_type *ft;

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {
		ft->is_fixed = false;
		ft->fix_in_progress = false;
	}

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft))
		fy_type_fixup_size_align(ft);

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {
		ft->is_fixed = false;
		ft->fix_in_progress = false;
	}
}

void fy_decl_destroy(struct fy_decl *decl)
{
	struct fy_decl *child;

	if (!decl)
		return;

	if (decl->cooked_comment) {
		free(decl->cooked_comment);
		decl->cooked_comment = NULL;
	}

	if (decl->fyd_yaml) {
		fy_document_destroy(decl->fyd_yaml);
		decl->fyd_yaml = NULL;
	}
	if (decl->yaml_comment) {
		free(decl->yaml_comment);
		decl->yaml_comment = NULL;
	}

	while ((child = fy_decl_list_pop(&decl->children)) != NULL)
		fy_decl_destroy(child);

	backend_decl_cleanup(decl);

	if (decl->name)
		free(decl->name);
	free(decl);
}

struct fy_decl *fy_decl_create(struct fy_reflection *rfl, struct fy_import *imp,
	struct fy_decl *parent, enum fy_decl_type decl_type, const char *name, void *user)
{
	struct fy_decl *decl;
	int rc;

	if (!rfl)
		return NULL;

	assert(!imp || imp->rfl == rfl);

	/* guard against rollover */
	if (rfl->next_decl_id + 1 <= 0)
		return NULL;

	decl = malloc(sizeof(*decl));
	if (!decl)
		goto err_out;
	memset(decl, 0, sizeof(*decl));

	decl->rfl = rfl;
	decl->imp = imp;
	decl->parent = parent;
	decl->decl_type = decl_type;
	decl->name = strdup(name);
	if (!decl->name)
		goto err_out;
	decl->source_location = NULL;

	fy_decl_list_init(&decl->children);

	rc = backend_decl_setup(decl, user);
	if (rc)
		goto err_out;

	assert(rfl->next_decl_id >= 0);
	decl->id = rfl->next_decl_id++;

	return decl;
err_out:
	fy_decl_destroy(decl);
	return NULL;
}

bool fy_decl_enum_value_is_unsigned(struct fy_decl *decl)
{
	int signess;

	/* only for enum values */
	if (decl->decl_type != FYDT_ENUM_VALUE)
		return false;

	signess = fy_type_kind_signess(decl->enum_value_decl.type_kind);
	assert(signess != 0);

	return signess > 0;
}

long long fy_decl_enum_value_signed(struct fy_decl *decl)
{
	/* only for enum values */
	if (!decl || decl->decl_type != FYDT_ENUM_VALUE)
		return LLONG_MAX;

	return decl->enum_value_decl.val.s;
}

unsigned long long fy_decl_enum_value_unsigned(struct fy_decl *decl)
{
	/* only for enum values */
	if (!decl || decl->decl_type != FYDT_ENUM_VALUE)
		return ULLONG_MAX;

	return decl->enum_value_decl.val.u;
}

bool fy_decl_field_is_bitfield(struct fy_decl *decl)
{
	return decl && decl->decl_type == FYDT_BITFIELD;
}

size_t fy_decl_field_offsetof(struct fy_decl *decl)
{
	/* only for field values */
	if (!decl || decl->decl_type != FYDT_FIELD)
		return SIZE_MAX;

	return decl->field_decl.byte_offset;
}

size_t fy_decl_field_bit_offsetof(struct fy_decl *decl)
{
	/* only for bit-field values */
	if (!decl || decl->decl_type != FYDT_BITFIELD)
		return SIZE_MAX;

	return decl->bitfield_decl.bit_offset;
}

size_t fy_decl_field_sizeof(struct fy_decl *decl)
{
	/* only for field values */
	if (!decl || decl->decl_type != FYDT_FIELD)
		return SIZE_MAX;

	assert(decl->type);
	return decl->type->size;
}

size_t fy_decl_field_bit_width(struct fy_decl *decl)
{
	/* only for bit-field values */
	if (!decl || decl->decl_type != FYDT_BITFIELD)
		return SIZE_MAX;

	return decl->bitfield_decl.bit_width;
}

bool fy_decl_is_in_system_header(struct fy_decl *decl)
{
	return decl && decl->in_system_header;
}

bool fy_decl_is_from_main_file(struct fy_decl *decl)
{
	return decl && decl->from_main_file;
}

const struct fy_source_location *fy_decl_get_location(struct fy_decl *decl)
{
	if (!decl)
		return NULL;

	return decl->source_location;
}

const char *fy_decl_get_spelling(struct fy_decl *decl)
{
	if (!decl)
		return NULL;
	return decl->spelling;
}

const char *fy_decl_get_display_name(struct fy_decl *decl)
{
	if (!decl)
		return NULL;
	return decl->display_name;
}

const char *fy_decl_get_signature(struct fy_decl *decl)
{
	if (!decl)
		return NULL;
	return decl->signature;
}

void fy_decl_clear_marker(struct fy_decl *decl)
{
	struct fy_decl *declp;

	if (!decl)
		return;

	decl->marker = false;

	for (declp = fy_decl_list_head(&decl->children); declp != NULL; declp = fy_decl_next(&decl->children, declp))
		fy_decl_clear_marker(declp);
}

void fy_decl_mark(struct fy_decl *decl)
{
	struct fy_decl *declp;

	if (!decl || decl->marker)
		return;

	decl->marker = true;

	if (decl->imp)
		fy_import_mark(decl->imp);
	if (decl->parent)
		fy_decl_mark(decl->parent);

	for (declp = fy_decl_list_head(&decl->children); declp != NULL; declp = fy_decl_next(&decl->children, declp))
		fy_decl_mark(declp);

	if (decl->source_location && decl->source_location->source_file)
		fy_source_file_mark(decl->source_location->source_file);

	if (decl->type)
		fy_type_mark(decl->type);

}

const char *fy_decl_get_raw_comment(struct fy_decl *decl)
{
	if (!decl)
		return NULL;
	return decl->raw_comment;
}

const char *fy_decl_get_cooked_comment(struct fy_decl *decl)
{
	if (!decl || !decl->raw_comment)
		return NULL;

	if (!decl->cooked_comment)
		decl->cooked_comment = fy_get_cooked_comment(decl->raw_comment, strlen(decl->raw_comment));

	return decl->cooked_comment;
}

struct fy_document *fy_decl_get_yaml_annotation(struct fy_decl *decl)
{
	const char *cooked_comment;

	if (!decl || !decl->raw_comment)
		return NULL;

	/* if we tried to parse always return what we found */
	if (decl->fyd_yaml_parsed)
		return decl->fyd_yaml;

	if (!decl->fyd_yaml) {
		cooked_comment = fy_decl_get_cooked_comment(decl);
		if (cooked_comment)
			decl->fyd_yaml = get_yaml_document(cooked_comment);
	}
	decl->fyd_yaml_parsed = true;

	return decl->fyd_yaml;
}

const char *fy_decl_get_yaml_comment(struct fy_decl *decl)
{
	struct fy_document *fyd;
	char *s, *e;

	if (!decl)
		return NULL;

	if (!decl->yaml_comment) {
		fyd = fy_decl_get_yaml_annotation(decl);
		if (fyd) {
			decl->yaml_comment = fy_emit_document_to_string(fyd, FYECF_MODE_FLOW_ONELINE);
			if (decl->yaml_comment) {
				/* trim newlines at the end */
				s = decl->yaml_comment;
				e = s + strlen(s);
				while (s < e && e[-1] == '\n')
					*--e = '\0';
			}
		}
	}

	return decl->yaml_comment;
}

const char *fy_decl_get_yaml_name(struct fy_decl *decl)
{
	return fy_token_get_text0(
			fy_node_get_scalar_token(
				fy_node_by_path(
					fy_document_root(fy_decl_get_yaml_annotation(decl)), "/name", FY_NT, FYNWF_DONT_FOLLOW)));
}

void fy_import_destroy(struct fy_import *imp)
{
	if (!imp)
		return;

	backend_import_cleanup(imp);

	free(imp);
}

struct fy_import *fy_import_create(struct fy_reflection *rfl, const void *user)
{
	struct fy_import *imp = NULL;
	int rc;

	imp = malloc(sizeof(*imp));
	if (!imp)
		goto err_out;
	memset(imp, 0, sizeof(*imp));

	imp->rfl = rfl;

	rfl->imp_curr = imp;
	rc = backend_import_setup(imp, user);
	rfl->imp_curr = NULL;
	if (rc)
		goto err_out;

	return imp;

err_out:
	fy_import_destroy(imp);
	return NULL;
}

void fy_import_clear_marker(struct fy_import *imp)
{
	if (!imp)
		return;

	imp->marker = false;
}

void fy_import_mark(struct fy_import *imp)
{
	if (!imp || imp->marker)
		return;

	imp->marker = true;
}

void fy_source_file_destroy(struct fy_source_file *srcf)
{
	if (!srcf)
		return;

	if (srcf->realpath)
		free(srcf->realpath);

	if (srcf->filename)
		free(srcf->filename);

	free(srcf);
}

struct fy_source_file *
fy_reflection_lookup_source_file(struct fy_reflection *rfl, const char *filename)
{
	struct fy_source_file *srcf;
	char *realname;

	if (!rfl || !filename)
		return NULL;

	realname = realpath(filename, NULL);
	if (!realname)
		return NULL;

	/* TODO hash it */
	for (srcf = fy_source_file_list_head(&rfl->source_files); srcf != NULL; srcf = fy_source_file_next(&rfl->source_files, srcf)) {
		if (!strcmp(srcf->realpath, realname))
			break;
	}

	free(realname);

	return srcf;
}

struct fy_source_file *
fy_source_file_create(struct fy_reflection *rfl, const char *filename)
{
	struct fy_source_file *srcf = NULL;

	if (!rfl || !filename)
		return NULL;

	/* guard against rollover */
	if (rfl->next_source_file_id + 1 <= 0)
		return NULL;

	srcf = malloc(sizeof(*srcf));
	if (!srcf)
		goto err_out;
	memset(srcf, 0, sizeof(*srcf));

	srcf->filename = strdup(filename);
	if (!srcf->filename)
		goto err_out;

	srcf->realpath = realpath(filename, NULL);
	if (!srcf->realpath)
		goto err_out;

	assert(rfl->next_source_file_id >= 0);
	srcf->id = rfl->next_source_file_id++;

	return srcf;

err_out:
	fy_source_file_destroy(srcf);
	return NULL;
}

void fy_source_file_clear_marker(struct fy_source_file *srcf)
{
	if (!srcf)
		return;

	srcf->marker = false;
}

void fy_source_file_mark(struct fy_source_file *srcf)
{
	if (!srcf || srcf->marker)
		return;

	srcf->marker = true;
}

void fy_source_file_dump(struct fy_source_file *srcf)
{
	if (!srcf)
		return;

	printf("\t%c %s realpath='%s' system=%s main_file=%s\n",
			srcf->marker ? '*' : ' ',
			srcf->filename,
			srcf->realpath,
			srcf->system_header ? "true" : "false",
			srcf->main_file ? "true" : "false");
}

static int fy_reflection_setup(struct fy_reflection *rfl, const struct fy_reflection_cfg *rflc);
static void fy_reflection_cleanup(struct fy_reflection *rfl);

static int fy_reflection_setup(struct fy_reflection *rfl, const struct fy_reflection_cfg *rflc)
{
	const struct fy_reflection_backend_ops *ops;
	int rc;

	/* basic checks */
	if (!rflc || !rflc->backend || !rflc->backend->ops)
		goto err_out;

	ops = rflc->backend->ops;
	/* all methods must be non NULL */
	if (!ops->reflection_setup ||
	    !ops->reflection_cleanup ||
	    !ops->import_setup ||
	    !ops->import_cleanup ||
	    !ops->type_setup ||
	    !ops->type_cleanup ||
	    !ops->decl_setup ||
	    !ops->decl_cleanup)
		goto err_out;

	memset(rfl, 0, sizeof(*rfl));
	rfl->cfg = *rflc;
	fy_import_list_init(&rfl->imports);
	fy_source_file_list_init(&rfl->source_files);
	fy_type_list_init(&rfl->types);
	fy_decl_list_init(&rfl->decls);
	rfl->next_type_id = 0;
	rfl->next_decl_id = 0;
	rfl->next_source_file_id = 0;

	rc = backend_reflection_setup(rfl);
	if (rc)
		goto err_out;

	return 0;

err_out:
	return -1;
}

void fy_reflection_update_type_info(struct fy_reflection *rfl)
{
	struct fy_type *ft;

	/* reset all type infos */
	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft))
		fy_type_reset_type_info(ft);

	/* and generate them */
	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft))
		(void)fy_type_get_type_info(ft);
}

static void fy_reflection_cleanup(struct fy_reflection *rfl)
{
	struct fy_import *imp;
	struct fy_source_file *srcf;
	struct fy_type *ft;
	struct fy_decl *decl;

	assert(rfl);

	while ((ft = fy_type_list_pop(&rfl->types)) != NULL)
		fy_type_destroy(ft);

	while ((decl = fy_decl_list_pop(&rfl->decls)) != NULL)
		fy_decl_destroy(decl);

	while ((srcf = fy_source_file_list_pop(&rfl->source_files)) != NULL)
		fy_source_file_destroy(srcf);

	while ((imp = fy_import_list_pop(&rfl->imports)) != NULL)
		fy_import_destroy(imp);

	backend_reflection_cleanup(rfl);
}

static void fy_type_reset_type_info(struct fy_type *ft)
{
	struct fy_type_info *ti;

	if (!ft)
		return;

	if (ft->field_decls) {
		free(ft->field_decls);
		ft->field_decls = NULL;
	}

	ti = &ft->type_info;
	if (ti->fields)
		free((void *)ti->fields);
	memset(ti, 0, sizeof(*ti));
	ft->has_type_info = false;
}

static struct fy_type_info *fy_type_get_type_info(struct fy_type *ft)
{
	struct fy_decl *decl, *declc;
	struct fy_type_info *ti;
	struct fy_field_info *fi = NULL;
	struct fy_decl **field_decls = NULL, **fds;

	if (!ft)
		return NULL;

	/* need to check if recursively producing */
	if (ft->has_type_info || ft->producing_type_info)
		return &ft->type_info;

	ft->producing_type_info = true;

	fy_type_reset_type_info(ft);

	ti = &ft->type_info;
	ti->kind = ft->type_kind;
	if (ft->is_const)
		ti->flags |= FYTIF_CONST;
	if (ft->is_volatile)
		ti->flags |= FYTIF_VOLATILE;
	if (ft->is_restrict)
		ti->flags |= FYTIF_RESTRICT;
	if (ft->is_fake_resolved)
		ti->flags |= FYTIF_UNRESOLVED_PTR;
	if (ft->decl && ft->decl->from_main_file)
		ti->flags |= FYTIF_MAIN_FILE;
	if (ft->decl && ft->decl->in_system_header)
		ti->flags |= FYTIF_SYSTEM_HEADER;
	if (ft->anonymous)
		ti->flags |= FYTIF_ANONYMOUS;

	ti->name = ft->name;
	ti->fullname = ft->fullname;
	ti->normalized_name = ft->normalized_name;
	ti->size = ft->size;
	ti->align = ft->align;

	/* primitive types, no more to do */
	if (fy_type_kind_is_primitive(ti->kind) ||
	    ti->kind == FYTK_FUNCTION || ti->kind == FYTK_VOID)
		goto out;

	/* if we have a type we're dependent on, pull it in */
	if (ft->dependent_type) {
		ti->dependent_type = fy_type_get_type_info(ft->dependent_type);
		if (!ti->dependent_type)
			goto err_out;
	}

	/* for pointers or typedef we' done now */
	if (ti->kind == FYTK_PTR || ti->kind == FYTK_TYPEDEF || ti->kind == FYTK_INCOMPLETEARRAY)
		goto out;

	/* constant array? fill in and out */
	if (ti->kind == FYTK_CONSTARRAY) {
		ti->count = ft->element_count;
		goto out;
	}

	/* only those from now on */
	assert(fy_type_kind_has_fields(ti->kind));

	assert(ft->decl);

	decl = ft->decl;

	/* count the number of fields/enum_values */
	ti->count = 0;
	for (declc = fy_decl_list_head(&decl->children); declc; declc = fy_decl_next(&decl->children, declc))
		ti->count++;

	field_decls = malloc(sizeof(*field_decls) * ti->count);
	if (!field_decls)
		goto err_out;

	fi = malloc(sizeof(*fi) * ti->count);
	if (!fi)
		goto err_out;
	memset(fi, 0, sizeof(*fi) * ti->count);
	ti->fields = fi;
	ft->field_decls = field_decls;

	fds = field_decls;
	for (declc = fy_decl_list_head(&decl->children); declc; declc = fy_decl_next(&decl->children, declc), fi++) {
		*fds++ = declc;
		fi->flags = 0;
		if (declc->anonymous)
			fi->flags |= FYFIF_ANONYMOUS;
		fi->parent = ti;
		fi->name = declc->name;
		fi->type_info = fy_type_get_type_info(declc->type);	/* may be null for enum values */

		if (ft->type_kind == FYTK_ENUM) {
			if (fy_decl_enum_value_is_unsigned(declc)) {
				fi->flags |= FYFIF_ENUM_UNSIGNED;
				fi->uval = fy_decl_enum_value_unsigned(declc);
			} else {
				fi->sval = fy_decl_enum_value_signed(declc);
			}
		} else {
			if (declc->decl_type == FYDT_BITFIELD) {
				fi->flags |= FYFIF_BITFIELD;
				fi->bit_offset = fy_decl_field_bit_offsetof(declc);
				fi->bit_width = fy_decl_field_bit_width(declc);
			} else
				fi->offset = fy_decl_field_offsetof(declc);
		}
	}

out:
	ft->producing_type_info = false;
	ft->has_type_info = true;
	return ti;

err_out:
	if (field_decls)
		free(field_decls);
	if (fi)
		free(fi);
	ft->producing_type_info = false;
	ft->has_type_info = false;
	return NULL;
}

void fy_reflection_destroy(struct fy_reflection *rfl)
{
	if (!rfl)
		return;
	fy_reflection_cleanup(rfl);
	free(rfl);
}

static struct fy_document *get_yaml_document_at_keyword(const char *start, size_t size, size_t *advance)
{
	const char *s, *e;
	struct fy_document *fyd = NULL;
	size_t skip = 0;

	assert(size > strlen("yaml:") + 1);
	assert(!memcmp(start, "yaml:", 5));

	s = start;
	e = s + size;

	/* skip over yaml: */
	s += 5;

	/* skip over spaces and tabs */
	while (s < e && isblank(*s))
		s++;

	assert(s < e);
	if (*s == '\n') {	/* block document */
		s++;
		assert(s < e);

		fyd = fy_block_document_build_from_string(NULL, s, e - s, &skip);

	} else if (*s == '{' || *s == '[') {		/* flow document */
		fyd = fy_flow_document_build_from_string(NULL, s, e - s, &skip);
	}

	if (fyd)
		s += skip;
	*advance = (size_t)(s - start);

	return fyd;
}

static struct fy_document *get_yaml_document(const char *cooked_comment)
{
	struct fy_document *fyd = NULL;
	struct fy_keyword_iter iter;
	const char *found;
	size_t advance;

	if (!cooked_comment)
		return NULL;

	fy_keyword_iter_begin(cooked_comment, strlen(cooked_comment), "yaml:", &iter);
	while ((found = fy_keyword_iter_next(&iter)) != NULL) {

		/* single document only for now */
		fyd = get_yaml_document_at_keyword(found, strlen(found), &advance);
		if (fyd)
			break;

		fy_keyword_iter_advance(&iter, advance);
	}
	fy_keyword_iter_end(&iter);

	return fyd;
}

void fy_decl_dump(struct fy_decl *decl, int start_level, bool no_location)
{
	const struct fy_source_location *source_location;
	struct fy_decl *declp;
	const char *type_name;
	const char *comment;
	int level;
	char *tabs;
	size_t bitoff;
	struct fy_comment_iter iter;
	const char *text;
	size_t len;
	bool raw_comments = false;

	level = start_level;
	declp = decl->parent;
	while (declp) {
		declp = declp->parent;
		level++;
	}
	tabs = alloca(level + 1);
	memset(tabs, '\t', level);
	tabs[level] = '\0';

	if (raw_comments) {
		comment = fy_decl_get_raw_comment(decl);
		if (comment) {
			fy_comment_iter_begin(comment, strlen(comment), &iter);
			while ((text = fy_comment_iter_next_line(&iter, &len)) != NULL)
				printf("%s\t  // %.*s\n", tabs, (int)len, text);
			fy_comment_iter_end(&iter);
		}
	}
	comment = fy_decl_get_yaml_comment(decl);
	if (comment) {
		printf("%s\t  // yaml: %s\n", tabs, comment);
	}

	printf("%s\t%c D#%u '%s':'%s'", tabs,
		decl->marker ? '*' : ' ',
		decl->id,
		decl_type_txt[decl->decl_type], decl->name);

	assert(decl->type);
	type_name = fy_type_kind_info_get_internal(decl->type->type_kind)->name;
	printf(" -> T#%d %s:'%s'", decl->type->id, type_name, decl->type->name);

	switch (decl->decl_type) {
	case FYDT_ENUM:
		assert(decl->type->dependent_type);
		printf(" \"%s\"", fy_type_kind_info_get_internal(decl->type->dependent_type->type_kind)->name);
		break;
	case FYDT_ENUM_VALUE:
		if (!fy_decl_enum_value_is_unsigned(decl))
			printf(" %lld", fy_decl_enum_value_signed(decl));
		else
			printf(" %llu", fy_decl_enum_value_unsigned(decl));
		break;
	case FYDT_FIELD:
		printf(" offset=%zu", fy_decl_field_offsetof(decl));
		break;
	case FYDT_BITFIELD:
		bitoff = fy_decl_field_bit_offsetof(decl);
		printf(" bitfield offset=%zu (%zu/%zu) width=%zu",
				bitoff, bitoff/8, bitoff%8,
				fy_decl_field_bit_width(decl));
		break;
	default:
		break;
	}

	if (!no_location) {
		source_location = fy_decl_get_location(decl);
		if (source_location)
			printf(" %s@%u:%u", source_location->source_file->filename, source_location->line, source_location->column);
	}

	if (decl->is_synthetic)
		printf(" synthetic");

	printf("\n");

	for (declp = fy_decl_list_head(&decl->children); declp != NULL; declp = fy_decl_next(&decl->children, declp))
		fy_decl_dump(declp, start_level, no_location);
}

bool fy_type_is_pointer(struct fy_type *ft)
{
	if (!ft)
		return false;
	return ft->type_kind == FYTK_PTR;
}

bool fy_type_is_array(struct fy_type *ft)
{
	if (!ft)
		return false;
	return ft->type_kind == FYTK_CONSTARRAY || ft->type_kind == FYTK_INCOMPLETEARRAY;
}

bool fy_type_is_constant_array(struct fy_type *ft)
{
	if (!ft)
		return false;
	return ft->type_kind == FYTK_CONSTARRAY;
}

bool fy_type_is_incomplete_array(struct fy_type *ft)
{
	if (!ft)
		return false;
	return ft->type_kind == FYTK_INCOMPLETEARRAY;
}

size_t fy_type_get_sizeof(struct fy_type *ft)
{
	if (!ft)
		return 0;
	return ft->size;
}

size_t fy_type_get_alignof(struct fy_type *ft)
{
	if (!ft)
		return 0;
	return ft->align;
}

int fy_type_get_constant_array_element_count(struct fy_type *ft)
{
	if (!fy_type_is_constant_array(ft))
		return -1;
	return ft->element_count;
}

struct fy_type *fy_type_get_dependent_type(struct fy_type *ft)
{
	if (!ft || !fy_type_kind_is_dependent(ft->type_kind))
		return NULL;
	return ft->dependent_type;
}

/* TODO optimize later */
struct fy_decl *fy_type_get_field_decl_by_name(struct fy_type *ft, const char *field)
{
	struct fy_decl *decl, *declf;

	if (!ft || !field || !fy_type_kind_has_fields(ft->type_kind))
		return NULL;

	decl = ft->decl;
	assert(decl);

	for (declf = fy_decl_list_head(&decl->children); declf != NULL; declf = fy_decl_next(&decl->children, declf)) {
		assert(fy_decl_type_is_field(declf->decl_type));
		if (!strcmp(declf->name, field))
			return declf;
	}

	return NULL;
}

/* TODO optimize later */
struct fy_decl *fy_type_get_field_decl_by_enum_value(struct fy_type *ft, long long val)
{
	struct fy_decl *decl, *declf;

	if (!ft || ft->type_kind != FYTK_ENUM)
		return NULL;

	decl = ft->decl;
	assert(decl);

	for (declf = fy_decl_list_head(&decl->children); declf != NULL; declf = fy_decl_next(&decl->children, declf)) {
		assert(declf->decl_type == FYDT_ENUM_VALUE);
		if (declf->enum_value_decl.val.s == val)
			return declf;
	}

	return NULL;
}

/* TODO optimize later */
struct fy_decl *fy_type_get_field_decl_by_unsigned_enum_value(struct fy_type *ft, unsigned long long val)
{
	struct fy_decl *decl, *declf;

	if (!ft || ft->type_kind != FYTK_ENUM)
		return NULL;

	decl = ft->decl;
	assert(decl);

	for (declf = fy_decl_list_head(&decl->children); declf != NULL; declf = fy_decl_next(&decl->children, declf)) {
		assert(declf->decl_type == FYDT_ENUM_VALUE);
		if (declf->enum_value_decl.val.u == val)
			return declf;
	}

	return NULL;
}

/* TODO optimize later */
int fy_type_get_field_count(struct fy_type *ft)
{
	struct fy_decl *decl, *declf;
	int count;

	if (!ft || !fy_type_kind_has_fields(ft->type_kind))
		return -1;

	decl = ft->decl;
	assert(decl);

	count = 0;
	for (declf = fy_decl_list_head(&decl->children); declf != NULL; declf = fy_decl_next(&decl->children, declf)) {
		assert(fy_decl_type_is_field(declf->decl_type));
		count++;
	}
	return count;
}

/* TODO optimize later */
int fy_type_get_field_index_by_name(struct fy_type *ft, const char *field)
{
	struct fy_decl *decl, *declf;
	const char *field_name;
	int idx;

	if (!ft || !field || !fy_type_kind_has_fields(ft->type_kind))
		return -1;

	decl = ft->decl;
	assert(decl);

	idx = 0;
	for (declf = fy_decl_list_head(&decl->children); declf != NULL; declf = fy_decl_next(&decl->children, declf)) {
		assert(fy_decl_type_is_field(declf->decl_type));

		field_name = fy_decl_get_yaml_name(declf);
		if (!field_name)
			field_name = declf->name;

		if (!strcmp(field_name, field))
			return idx;
		idx++;
	}

	return -1;
}

/* TODO optimize later */
struct fy_decl *fy_type_get_field_decl_by_idx(struct fy_type *ft, unsigned int idx)
{
	struct fy_decl *decl, *declf;
	unsigned int i;

	if (!ft || !fy_type_kind_has_fields(ft->type_kind))
		return NULL;

	decl = ft->decl;
	assert(decl);

	i = 0;
	for (declf = fy_decl_list_head(&decl->children); declf != NULL; declf = fy_decl_next(&decl->children, declf)) {
		assert(fy_decl_type_is_field(declf->decl_type));
		if (i++ == idx)
			return declf;
	}

	return NULL;
}

int fy_type_get_field_idx_by_decl(struct fy_type *ft, struct fy_decl *decl)
{
	struct fy_decl *declf;
	unsigned int i;

	if (!ft || !decl)
		return -1;

	assert(ft->decl);
	i = 0;
	for (declf = fy_decl_list_head(&ft->decl->children); declf != NULL; declf = fy_decl_next(&ft->decl->children, declf)) {
		if (declf == decl)
			return i;
		i++;
	}
	return -1;
}

const struct fy_source_location *
fy_type_get_decl_location(struct fy_type *ft)
{
	/* cannot get location in those cases */
	if (!ft || !ft->decl || fy_type_kind_is_primitive(ft->type_kind))
		return NULL;

	return fy_decl_get_location(ft->decl);
}

char *fy_type_generate_name_internal(struct fy_type *ft, const char *field, int genidx, bool normalized)
{
	const struct fy_type_kind_info *tki;
	struct fy_decl *decl;
	struct fy_type *ftd;
	enum fy_type_kind tk;
	const char *declname, *s;
	char *depname = NULL;
	int depname_size;
	FILE *fp = NULL;
	char *buf, *bufn;
	size_t len;
	int ret;
	bool error = false;
	bool has_field;
	char marker[16];
	const char *ms, *me;
	const char *punct_list;
	bool dep_last_is_punct, field_first_is_punct;

	if (!ft)
		return NULL;

	has_field = field && field[0];

	tk = ft->type_kind;
	tki = fy_type_kind_info_get_internal(tk);

	buf = NULL;
	len = 0;
	fp = open_memstream(&buf, &len);
	if (!fp)
		goto err_out;

#undef FPRINTF
#define FPRINTF(_fmt, ...) \
	do { \
		ret = fprintf(fp, (_fmt), ##__VA_ARGS__); \
		if (ret < 0) \
			goto err_out; \
	} while(0)

	if (ft->is_const)
		FPRINTF("const ");

	if (ft->is_volatile)
		FPRINTF("volatile ");

	if (fy_type_kind_is_primitive(tk) || tk == FYTK_INVALID) {
		FPRINTF("%s%s%s", tki->name, has_field ? " " : "", has_field ? field : "");
		goto out;
	}

	decl = ft->decl;
	if (decl && decl->name)
		declname = decl->name;
	else
		declname = "";

	if (tk == FYTK_TYPEDEF) {
		FPRINTF("%s%s%s", declname, has_field ? " " : "", has_field ? field : "");
		goto out;
	}
	if (tk == FYTK_FUNCTION) {
		s = strchr(declname, '(');
		if (!s)
			goto err_out;

		FPRINTF("%.*s%s%s", (int)(s - declname), declname, has_field ? field : "", s);
		goto out;
	}

	/* struct, union and enum */
	if (fy_type_kind_has_prefix(tk)) {
		if (!has_field) {
			FPRINTF("%s", declname);
		} else {
			FPRINTF("%s %s %s", tki->name, declname, field);
		}
		goto out;
	}

	assert(fy_type_kind_is_dependent(tk));

	ftd = ft->dependent_type;
	if (!ftd)
		goto err_out;

	snprintf(marker, sizeof(marker) - 1, "@%d@", genidx);
	depname = fy_type_generate_name_internal(ftd, marker, genidx + 1, normalized);
	if (!depname)
		goto err_out;

	ms = strstr(depname, marker);
	if (!ms)
		goto err_out;
	depname_size = (int)(ms - depname);
	/* strip trailing spaces */
	while (depname_size > 0 && isspace(depname[depname_size-1]))
		depname_size--;

	me = ms + strlen(marker);

	punct_list = "*()";
	field_first_is_punct = field && field[0] && strchr(punct_list, field[0]) != NULL;
	dep_last_is_punct = depname_size <= 0 || strchr(punct_list, depname[depname_size-1]) != NULL;

	FPRINTF("%.*s", depname_size, depname);

	// fprintf(stderr, "%s:%d dep='%.*s' field='%s', me='%s' field_first_is_punct=%s dep_last_is_punct=%s\n",
	//		__FILE__, __LINE__, depname_size, depname, field, me,
	//		field_first_is_punct ? "true" : "false",
	//		dep_last_is_punct ? "true" : "false");

	/* if neither the first of the field or the last of the dep is punct, put a space there */
	if (!field_first_is_punct && !dep_last_is_punct)
		FPRINTF(" ");

	switch (tk) {

	case FYTK_PTR:
		if (ftd->type_kind != FYTK_FUNCTION && ftd->type_kind != FYTK_CONSTARRAY && ftd->type_kind != FYTK_INCOMPLETEARRAY)
			FPRINTF("*%s", field);
		else
			FPRINTF("(*%s)", field);
		break;

	case FYTK_INCOMPLETEARRAY:
		if (ftd->type_kind != FYTK_FUNCTION)
			FPRINTF("%s[]", field);
		else
			FPRINTF("(%s[])", field);
		break;

	case FYTK_CONSTARRAY:
		if (ftd->type_kind != FYTK_FUNCTION)
			FPRINTF("%s[%llu]", field, ft->element_count);
		else
			FPRINTF("(%s[%llu])", field, ft->element_count);
		break;

	default:
		abort();
		break;
	}
	FPRINTF("%s", me);

out:
	if (depname)
		free(depname);

	if (fp)
		fclose(fp);

	if (error) {
		free(buf);
		buf = NULL;
	} else if (normalized && buf) {
		bufn = fy_type_name_normalize(buf);
		assert(bufn);
		assert(bufn);
		free(buf);
		buf = bufn;
		assert(buf);
	}

	return buf;

err_out:
	error = true;
	goto out;
}

char *fy_type_generate_name(struct fy_type *ft, const char *field, bool normalized)
{
	return fy_type_generate_name_internal(ft, field ? field : "", 0, normalized);
}

void fy_type_dump(struct fy_type *ft, bool no_location);

struct fy_decl *fy_type_get_anonymous_parent_decl(struct fy_type *ft)
{
	struct fy_reflection *rfl;
	struct fy_type *ftp;
	struct fy_decl *decl, *declc;

	if (!ft || !ft->anonymous)
		return NULL;
	rfl = ft->rfl;
	assert(rfl);

	for (ftp = fy_type_list_head(&rfl->types); ftp != NULL; ftp = fy_type_next(&rfl->types, ftp)) {
		if (!ftp->decl)
			continue;
		decl = ftp->decl;
		for (declc = fy_decl_list_head(&decl->children); declc; declc = fy_decl_next(&decl->children, declc)) {
			if (declc->type == ft)
				return declc;
		}
	}
	return NULL;
}

size_t fy_type_eponymous_offset(struct fy_type *ft)
{
	size_t offset;
	struct fy_decl *decl;

	if (!ft)
		return 0;

	offset = 0;
	while (ft->anonymous && (decl = fy_type_get_anonymous_parent_decl(ft)) != NULL) {
		assert(decl->decl_type == FYDT_FIELD);
		offset += decl->field_decl.byte_offset;
		ft = decl->parent->type;
	}

	return offset;
}

const char *fy_type_get_raw_comment(struct fy_type *ft)
{
	if (!ft || !ft->decl)
		return NULL;
	return fy_decl_get_raw_comment(ft->decl);
}

const char *fy_type_get_cooked_comment(struct fy_type *ft)
{
	if (!ft || !ft->decl)
		return NULL;
	return fy_decl_get_cooked_comment(ft->decl);
}

struct fy_document *fy_type_get_yaml_annotation(struct fy_type *ft)
{
	if (!ft || !ft->decl)
		return NULL;
	return fy_decl_get_yaml_annotation(ft->decl);
}

const char *fy_type_get_yaml_comment(struct fy_type *ft)
{
	if (!ft || !ft->decl)
		return NULL;
	return fy_decl_get_yaml_comment(ft->decl);
}

const char *fy_type_get_yaml_name(struct fy_type *ft)
{
	const char *name;

	if (!ft || !ft->decl)
		return NULL;

	name = fy_decl_get_yaml_name(ft->decl);
	if (name)
		return name;
	return ft->name;
}


void fy_type_dump(struct fy_type *ft, bool no_location)
{
	const struct fy_source_location *source_location;
	const char *type_name;
	struct fy_type *ftd;
	char *ntn1, *ntn2;

	printf("\t%c T#%d", ft->marker ? '*' : ' ', ft->id);
	type_name = fy_type_kind_info_get_internal(ft->type_kind)->name;
	printf(" %s:'%s'", type_name, ft->name);

	/* compare the normalized names */
	ntn1 = fy_type_generate_name(ft, NULL, true);
	assert(ntn1);
	ntn2 = fy_type_name_normalize(ft->name);
	assert(ntn2);

	/* display diffs in normalized names */
	if (strcmp(ntn1, ntn2))
		printf(":!'%s'-'%s'", ntn1, ntn2);

	free(ntn2);
	free(ntn1);

	printf(" size=%zu align=%zu", fy_type_get_sizeof(ft), fy_type_get_alignof(ft));

	if (fy_type_is_declared(ft)) {
		printf(" -> D#%u", ft->decl->id);
		if (!no_location) {
			source_location = fy_type_get_decl_location(ft);
			if (source_location)
				printf(" %s@%u:%u", source_location->source_file->filename, source_location->line, source_location->column);
		}
	}

	if (fy_type_kind_is_dependent(ft->type_kind)) {
		if (fy_type_is_resolved(ft)) {
			ftd = fy_type_get_dependent_type(ft);
			if (ftd) {
				type_name = fy_type_kind_info_get_internal(ftd->type_kind)->name;
				printf(" -> T#%d %s:'%s'", ftd->id, type_name, ftd->name);
			} else {
				printf(" -> T#<NULL>");
			}
		} else
			printf(" unresolved");
	}

	if (ft->anonymous)
		printf(" anonymous");

	if (ft->is_synthetic)
		printf(" synthetic");

	if (ft->is_fake_resolved)
		printf(" fake-resolved");

	if (ft->is_const)
		printf(" const");

	if (ft->is_volatile)
		printf(" volatile");

	if (ft->is_restrict)
		printf(" restrict");

	printf("\n");
}

struct fy_reflection *fy_reflection_create(const struct fy_reflection_cfg *rflc)
{
	struct fy_reflection *rfl;
	int rc;

	rfl = malloc(sizeof(*rfl));
	if (!rfl)
		goto err_out;
	memset(rfl, 0, sizeof(*rfl));

	rc = fy_reflection_setup(rfl, rflc);
	if (rc)
		goto err_out;

	return rfl;

err_out:
	fy_reflection_destroy(rfl);
	return NULL;
}

int fy_reflection_import(struct fy_reflection *rfl, const void *user)
{
	struct fy_import *imp = NULL;

	assert(rfl);

	imp = fy_import_create(rfl, user);
	if (!imp)
		goto err_out;

	fy_import_list_add_tail(&rfl->imports, imp);

	return 0;

err_out:
	fy_import_destroy(imp);
	return -1;
}

bool fy_reflection_is_resolved(struct fy_reflection *rfl)
{
	return rfl && rfl->unresolved_types_count == 0;
}

void fy_reflection_clear_all_markers(struct fy_reflection *rfl)
{
	struct fy_type *ft;
	struct fy_decl *decl;
	struct fy_import *imp;
	struct fy_source_file *srcf;

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft))
		fy_type_clear_marker(ft);

	for (decl = fy_decl_list_head(&rfl->decls); decl != NULL; decl = fy_decl_next(&rfl->decls, decl))
		fy_decl_clear_marker(decl);

	for (imp = fy_import_list_head(&rfl->imports); imp != NULL; imp = fy_import_next(&rfl->imports, imp))
		fy_import_clear_marker(imp);

	for (srcf = fy_source_file_list_head(&rfl->source_files); srcf != NULL; srcf = fy_source_file_next(&rfl->source_files, srcf))
		fy_source_file_clear_marker(srcf);
}

struct fy_type *
fy_reflection_lookup_type(struct fy_reflection *rfl, enum fy_type_kind type_kind, const char *name)
{
	struct fy_type *ft;
	const char *type_name;
	const char *s;
	char *ntn1, *ntn2;
	size_t len;
	enum fy_type_kind type_kind_auto;

	if (!rfl || !name)
		return NULL;

	ntn1 = ntn2 = NULL;
	ft = NULL;

	ntn1 = fy_type_name_normalize(name);
	if (!ntn1)
		goto err_out;

	s = strchr(ntn1, ' ');
	type_name = s ? s : ntn1;
	while (isblank(*type_name))
		type_name++;
	len = strlen(ntn1);
	if (len > 7 && !strncmp(ntn1, "struct ", 7))
		type_kind_auto = FYTK_STRUCT;
	else if (len > 6 && !strncmp(ntn1, "union ", 6))
		type_kind_auto = FYTK_UNION;
	else
		type_kind_auto = FYTK_INVALID;

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {

		/* if type kind is valid, the type must match */
		if (type_kind != FYTK_INVALID && ft->type_kind != type_kind)
			continue;

		/* type kind is invalid, but an auto type kind was found, match that */
		if (type_kind == FYTK_INVALID && type_kind_auto != FYTK_INVALID && ft->type_kind != type_kind_auto)
			continue;

		/* check name match */
		if (!strcmp(ft->normalized_name, type_name))
			break;
	}

err_out:
	if (ntn1)
		free(ntn1);
	if (ntn2)
		free(ntn2);

	return ft;
}

void fy_reflection_renumber(struct fy_reflection *rfl)
{
	struct fy_decl *decl, *decl2;
	struct fy_type *ft;
	struct fy_source_file *srcf;

	rfl->next_decl_id = 0;
	for (decl = fy_decl_list_head(&rfl->decls); decl != NULL; decl = fy_decl_next(&rfl->decls, decl)) {
		decl->id = rfl->next_decl_id++;
		for (decl2 = fy_decl_list_head(&decl->children); decl2 != NULL; decl2 = fy_decl_next(&decl->children, decl2)) {
			decl2->id = rfl->next_decl_id++;
			/* note that there is no second level of decls */
			assert(fy_decl_list_empty(&decl2->children));
		}
	}

	rfl->next_type_id = 0;
	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft))
		ft->id = rfl->next_type_id++;

	rfl->next_source_file_id = 0;
	for (srcf = fy_source_file_list_head(&rfl->source_files); srcf != NULL; srcf = fy_source_file_next(&rfl->source_files, srcf))
		srcf->id = rfl->next_source_file_id++;
}

void fy_reflection_prune_unmarked(struct fy_reflection *rfl)
{
	struct fy_import *imp, *impn;
	struct fy_decl *decl, *decln;
	struct fy_source_file *srcf, *srcfn;
	struct fy_type *ft, *ftn;

	if (!rfl)
		return;

	for (imp = fy_import_list_head(&rfl->imports); imp != NULL; imp = impn) {
		impn = fy_import_next(&rfl->imports, imp);

		if (!imp->marker) {
			fy_import_list_del(&rfl->imports, imp);
			fy_import_destroy(imp);
			continue;
		}
	}

	/* note second level decls are always fields */
	for (decl = fy_decl_list_head(&rfl->decls); decl != NULL; decl = decln) {
		decln = fy_decl_next(&rfl->decls, decl);

		if (!decl->marker) {
			fy_decl_list_del(&rfl->decls, decl);
			fy_decl_destroy(decl);
			continue;
		}
	}

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = ftn) {
		ftn = fy_type_next(&rfl->types, ft);

		if (!ft->marker) {
			fy_type_list_del(&rfl->types, ft);
			fy_type_destroy(ft);
			continue;
		}
	}

	for (srcf = fy_source_file_list_head(&rfl->source_files); srcf != NULL; srcf = srcfn) {
		srcfn = fy_source_file_next(&rfl->source_files, srcf);

		if (!srcf->marker) {
			fy_source_file_list_del(&rfl->source_files, srcf);
			fy_source_file_destroy(srcf);
			continue;
		}
	}

	fy_reflection_renumber(rfl);
	fy_reflection_update_type_info(rfl);
}

struct fy_type *
lookup_or_create_primitive_type(struct fy_reflection *rfl, enum fy_type_kind type_kind)
{
	const struct fy_type_kind_info *tki;
	struct fy_type *ft;

	tki = fy_type_kind_info_get_internal(type_kind);
	ft = fy_reflection_lookup_type(rfl, type_kind, tki->name);
	if (ft)
		return ft;

	ft = fy_type_create(rfl, type_kind, tki->name, NULL, NULL);
	if (!ft)
		return NULL;
	ft->size = tki->size;
	ft->align = tki->align;
	fy_type_list_add_tail(&rfl->types, ft);
	return ft;
}

enum fy_type_kind
select_primitive_type_kind_for_array(struct fy_reflection *rfl, size_t size, size_t align)
{
	const struct fy_type_kind_info *tki;
	bool use_ull;
	static const enum fy_type_kind check[] = {
		FYTK_ULONGLONG,
		FYTK_ULONG,
		FYTK_UINT,
		FYTK_USHORT,
		FYTK_UCHAR,
		FYTK_INVALID,	/* end */
	};
	static const enum fy_type_kind *p;

	assert(align > 0);
	assert(size > 0);

	/* otherwise it's a constant array with the required size and alignment */
	p = check;
	use_ull = (sizeof(unsigned long long) != sizeof(unsigned long)) ||
		  (alignof(unsigned long long) != sizeof(unsigned long));
	if (!use_ull)
		p++;
	for (; *p != FYTK_INVALID; p++) {
		tki = fy_type_kind_info_get_internal(*p);
		if (tki->align == align && size >= tki->size && (size % tki->size) == 0)
			break;
	}
	/* this is impossible */
	assert(*p != FYTK_INVALID);
	return *p;
}

struct fy_type *
lookup_or_create_primitive_type_array(struct fy_reflection *rfl, enum fy_type_kind type_kind, unsigned long long element_count)
{
	const struct fy_type_kind_info *tki;
	struct fy_type *ft, *ft_base;
	char *name = NULL;
	int ret;

	tki = fy_type_kind_info_get_internal(type_kind);
	ft_base = lookup_or_create_primitive_type(rfl, type_kind);
	if (!ft_base)
		return NULL;

	ret = asprintf(&name, "%s [%llu]", tki->name, element_count);
	if (ret < 0)
		goto err_out;

	ft = fy_reflection_lookup_type(rfl, FYTK_CONSTARRAY, name);
	if (ft) {
		free(name);
		return ft;
	}

	ft = fy_type_create(rfl, FYTK_CONSTARRAY, name, NULL, NULL);
	if (!ft)
		goto err_out;

	ft->is_synthetic = true;
	ft->size = ft_base->size * element_count;
	ft->align = ft_base->align;
	ft->element_count = element_count;
	ft->dependent_type_kind = type_kind;
	ft->dependent_type = ft_base;
	ft->fake_resolve_data = name;

	fy_type_list_add_tail(&rfl->types, ft);

	return ft;

err_out:
	if (name)
		free(name);
	return NULL;
}

static int
fix_unresolved_type(struct fy_type *ft)
{
	const struct fy_type_kind_info *tki_base = NULL;
	struct fy_reflection *rfl = ft->rfl;
	struct fy_type *ftt, *ftt_inner;
	struct fy_import *imp;
	struct fy_decl *decl, *declc;
	enum fy_decl_type decl_type;
	enum fy_type_kind base_type_kind;
	char *synthetic_name = NULL;
	const char *name;
	unsigned long long element_count;

	assert(ft->dependent_type_name);

	/* fprintf(stderr, "%s:%d - ft->type_kind=%s ft->name=%s ft->dependent_type_kind=%s ft->dependent_type_name=%s\n",
			__FILE__, __LINE__,
			fy_type_kind_info_get_internal(ft->type_kind)->name, ft->name,
			fy_type_kind_info_get_internal(ft->dependent_type_kind)->name, ft->dependent_type_name); */

	/* for an unresolved pointer, first try if there's a type registered
	 * at the previous pass
	 */
	if (ft->type_kind == FYTK_PTR) {
		ftt = fy_reflection_lookup_type(rfl, ft->dependent_type_kind, ft->dependent_type_name);
		 /* if found, very  good, already resolved */
		if (ftt)
			goto done;
	}

	assert(ft->dependent_type_kind == FYTK_STRUCT ||
	       ft->dependent_type_kind == FYTK_UNION ||
	       ft->dependent_type_kind == FYTK_TYPEDEF);

	/* if we have concrete sizes, put something in */
	if (ft->type_kind != FYTK_PTR) {
		base_type_kind = select_primitive_type_kind_for_array(rfl, ft->size, ft->align);
		if (base_type_kind == FYTK_INVALID)
			goto err_out;
		tki_base = fy_type_kind_info_get_internal(base_type_kind);
		element_count = ft->size / tki_base->size;
	} else {
		base_type_kind = FYTK_VOID;
		tki_base = fy_type_kind_info_get_internal(base_type_kind);
		element_count = 0;
	}

	decl_type = ft->dependent_type_kind == FYTK_STRUCT ? FYDT_STRUCT :
		    ft->dependent_type_kind == FYTK_UNION ? FYDT_UNION :
		    FYDT_TYPEDEF;

	name = strchr(ft->dependent_type_name, ' ');
	if (name) {
		while (*name == ' ')
			name++;
	}
	if (!name || !name[0])
		name = ft->dependent_type_name;

	if (ft->decl)
		imp = ft->decl->imp;
	else
		imp = fy_import_list_head(&rfl->imports);
	if (!imp)
		goto err_out;

	/* create fake declaration */
	decl = fy_decl_create(rfl, imp, NULL, decl_type, name, NULL);
	if (!decl)
		goto err_out;
	decl->is_synthetic = true;
	decl->in_system_header = true;

	if (element_count > 0) {
		ftt_inner = lookup_or_create_primitive_type_array(rfl, base_type_kind, element_count);
		if (!ftt_inner)
			goto err_out;
	} else if (fy_type_kind_is_dependent(ft->dependent_type_kind)) {
		ftt_inner = lookup_or_create_primitive_type(rfl, base_type_kind);
		if (!ftt_inner)
			goto err_out;
	} else
		ftt_inner = NULL;

	if (decl_type != FYDT_TYPEDEF && ftt_inner) {
		declc = fy_decl_create(rfl, imp, decl, FYDT_FIELD, "", NULL);
		if (!declc)
			goto err_out;
		declc->type = ftt_inner;
		declc->field_decl.byte_offset = 0;
		declc->is_synthetic = true;
		declc->anonymous = true;
		declc->in_system_header = true;

		fy_decl_list_add_tail(&decl->children, declc);
	}

	fy_decl_list_add_tail(&rfl->decls, decl);

	ftt = fy_type_create(rfl, ft->dependent_type_kind, name, decl, NULL);
	if (!ftt)
		goto err_out;

	if (fy_type_kind_is_dependent(ftt->type_kind))
		ftt->dependent_type = ftt_inner;
	else if (element_count > 0) {
		ftt->size = ftt_inner->size;
		ftt->align = ftt_inner->align;
	}

	fy_type_list_add_tail(&rfl->types, ftt);

	decl->type = ftt;

done:
	assert(ftt);

	ft->dependent_type = ftt;
	ft->is_synthetic = true;

	return 0;
err_out:
	if (synthetic_name)
		free(synthetic_name);
	return -1;
}

static int
fix_unresolved_types(struct fy_reflection *rfl, bool no_pointers)
{
	struct fy_type *ft, *ftn;
	int ret;

	/* nothing to do in this case */
	if (!rfl || !rfl->unresolved_types_count)
		return 0;

	for (ft = fy_type_list_head(&rfl->types);
		rfl->unresolved_types_count > 0 && ft != NULL; ft = ftn) {

		ftn = fy_type_next(&rfl->types, ft);

		if (fy_type_is_resolved(ft))
			continue;

		if (no_pointers && ft->type_kind == FYTK_PTR)
			continue;

		ret = fix_unresolved_type(ft);
		if (ret < 0)
			goto err_out;

		ft->unresolved = false;
		rfl->unresolved_types_count--;
	}

	return 0;

err_out:
	return -1;
}

void fy_reflection_fix_unresolved(struct fy_reflection *rfl)
{
	int ret;

	/* first pass, don't fix pointers */
	ret = fix_unresolved_types(rfl, true);
	if (ret)
		goto err_out;

	/* second pass, fix pointers */
	ret = fix_unresolved_types(rfl, false);
	if (ret)
		goto err_out;
	return;
err_out:
	return;
}

void fy_reflection_dump(struct fy_reflection *rfl, bool marked_only, bool no_location)
{
	struct fy_import *imp;
	struct fy_decl *decl;
	struct fy_source_file *srcf;
	struct fy_type *ft;

	if (!rfl)
		return;

	printf("Reflection imports:\n");
	for (imp = fy_import_list_head(&rfl->imports); imp != NULL; imp = fy_import_next(&rfl->imports, imp)) {

		if (marked_only && !imp->marker)
			continue;

		printf("\t%c %s\n",
			imp->marker ? '*' : ' ',
			imp->name);

	}

	printf("Reflection decls:\n");
	for (decl = fy_decl_list_head(&rfl->decls); decl != NULL; decl = fy_decl_next(&rfl->decls, decl)) {

		if (marked_only && !decl->marker)
			continue;

		fy_decl_dump(decl, 0, no_location);
	}

	printf("Reflection types:\n");
	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {

		if (marked_only && !ft->marker)
			continue;

		fy_type_dump(ft, no_location);
	}

	printf("Reflection files:\n");
	for (srcf = fy_source_file_list_head(&rfl->source_files); srcf != NULL; srcf = fy_source_file_next(&rfl->source_files, srcf)) {
		if (marked_only && !srcf->marker)
			continue;

		fy_source_file_dump(srcf);
	}
}

const char *fy_import_get_name(struct fy_import *imp)
{
	return imp ? imp->name : NULL;
}

struct fy_type *
fy_type_iterate(struct fy_reflection *rfl, void **prevp)
{
	if (!rfl || !prevp)
		return NULL;

	return *prevp = *prevp ? fy_type_next(&rfl->types, *prevp) : fy_type_list_head(&rfl->types);
}

struct fy_reflection *
fy_reflection_from_imports(const char *backend_name, const void *backend_cfg,
			   int num_imports, const void *import_cfgs[])
{
	const struct fy_reflection_backend *backend;
	struct fy_reflection_cfg rcfg;
	struct fy_reflection *rfl = NULL;
	int i, rc;

	if (!backend_name || num_imports <= 0)
		return NULL;

	backend = fy_reflection_backend_lookup(backend_name);
	if (!backend)
		return NULL;

	memset(&rcfg, 0, sizeof(rcfg));
	rcfg.backend_cfg = backend_cfg;
	rcfg.backend = backend;

	rfl = fy_reflection_create(&rcfg);
	if (!rfl)
		goto err_out;

	/* do the imports */
	for (i = 0; i < num_imports; i++) {
		rc = fy_reflection_import(rfl, import_cfgs ? import_cfgs[i] : NULL);
		if (rc)
			goto err_out;
		break;
	}

	/* if the reflection is not resolve, try to resolve it */
	if (!fy_reflection_is_resolved(rfl))
		fy_reflection_fix_unresolved(rfl);

	fy_reflection_dump(rfl, false, false);

	/* if still unresolved, failure */
	if (!fy_reflection_is_resolved(rfl))
		goto err_out;

	/* renumber to better numbers */
	fy_reflection_renumber(rfl);

	/* update the type info */
	fy_reflection_update_type_info(rfl);

	return rfl;

err_out:
	fy_reflection_destroy(rfl);
	return NULL;
}

struct fy_reflection *
fy_reflection_from_import(const char *backend_name, const void *backend_cfg, const void *import_cfg)
{
	return fy_reflection_from_imports(backend_name, backend_cfg,
					  1, import_cfg ? &import_cfg : NULL);
}

const struct fy_type_info *
fy_type_info_iterate(struct fy_reflection *rfl, void **prevp)
{
	struct fy_type *ft;

	if (!rfl || !prevp)
		return NULL;

	ft = *prevp = *prevp ? fy_type_next(&rfl->types, *prevp) : fy_type_list_head(&rfl->types);
	if (!ft)
		return NULL;
	return fy_type_get_type_info(ft);
}

const struct fy_type_info *
fy_type_info_reverse_iterate(struct fy_reflection *rfl, void **prevp)
{
	struct fy_type *ft;

	if (!rfl || !prevp)
		return NULL;

	ft = *prevp = *prevp ? fy_type_prev(&rfl->types, *prevp) : fy_type_list_tail(&rfl->types);
	if (!ft)
		return NULL;
	return fy_type_get_type_info(ft);
}

struct fy_reflection *
fy_type_info_to_reflection(const struct fy_type_info *ti)
{
	struct fy_type *ft;

	ft = fy_type_from_info(ti);
	if (!ft)
		return NULL;
	return ft->rfl;
}

char *
fy_type_info_generate_name(const struct fy_type_info *ti, const char *field, bool normalized)
{
	struct fy_type *ft;

	ft = fy_type_from_info(ti);
	if (!ft)
		return NULL;

	return fy_type_generate_name(ft, field, normalized);
}

char *
fy_type_name_normalize(const char *type_name)
{
	const char *s;
	char *d;
	char *buf;
	char c, lastc;

	if (!type_name)
		return NULL;

	/* the buffer will never grow, so worst case allocation */
	buf = malloc(strlen(type_name) + 1);
	if (!buf)
		return NULL;

	s = type_name;
	d = buf;

	lastc = -1;
	while ((c = *s++) != '\0') {
		/* space, if last and next was alnum keep one, otherwise skip all */
		if (isspace(c)) {
			lastc = c;
			while ((c = *s) != '\0' && isspace(c))
				s++;
			if (isalnum(lastc) && isalnum(c))
				*d++ = ' ';
			continue;
		}
		*d++ = c;
		lastc = c;
	}
	*d = '\0';

	return buf;
}

void fy_type_info_clear_marker(const struct fy_type_info *ti)
{
	fy_type_clear_marker(fy_type_from_info(ti));
}

void fy_type_info_mark(const struct fy_type_info *ti)
{
	fy_type_mark(fy_type_from_info(ti));
}

bool fy_type_info_is_marked(const struct fy_type_info *ti)
{
	struct fy_type *ft;

	ft = fy_type_from_info(ti);
	return ft && ft->marker;
}

size_t fy_type_info_eponymous_offset(const struct fy_type_info *ti)
{
	return fy_type_eponymous_offset(fy_type_from_info(ti));
}

int fy_field_info_index(const struct fy_field_info *fi)
{
	const struct fy_type_info *ti;
	int idx;

	if (!fi)
		return -1;
	ti = fi->parent;
	assert(ti);
	assert(ti->fields);
	idx = fi - ti->fields;
	assert((unsigned int)idx < ti->count);
	return idx;
}

const struct fy_field_info *
fy_type_info_lookup_field(const struct fy_type_info *ti, const char *name)
{
	int idx;

	idx = fy_type_get_field_index_by_name(fy_type_from_info(ti), name);
	if (idx < 0)
		return NULL;
	assert((unsigned int)idx < ti->count);
	return ti->fields + idx;
}

const struct fy_field_info *
fy_type_info_lookup_field_by_enum_value(const struct fy_type_info *ti, long long val)
{
	struct fy_type *ft;
	int idx;

	ft = fy_type_from_info(ti);
	idx = fy_type_get_field_idx_by_decl(ft, fy_type_get_field_decl_by_enum_value(ft, val));
	if (idx < 0)
		return NULL;
	assert((unsigned int)idx < ti->count);
	return ti->fields + idx;
}

const struct fy_field_info *
fy_type_info_lookup_field_by_unsigned_enum_value(const struct fy_type_info *ti, unsigned long long val)
{
	struct fy_type *ft;
	int idx;

	ft = fy_type_from_info(ti);
	idx = fy_type_get_field_idx_by_decl(ft, fy_type_get_field_decl_by_unsigned_enum_value(ft, val));
	if (idx < 0)
		return NULL;
	assert((unsigned int)idx < ti->count);
	return ti->fields + idx;
}

void fy_type_info_set_userdata(const struct fy_type_info *ti, void *userdata)
{
	fy_type_set_userdata(fy_type_from_info(ti), userdata);
}

void *fy_type_info_get_userdata(const struct fy_type_info *ti)
{
	return fy_type_get_userdata(fy_type_from_info(ti));
}

void fy_field_info_set_userdata(const struct fy_field_info *fi, void *userdata)
{
	fy_decl_set_userdata(fy_decl_from_field_info(fi), userdata);
}

void *fy_field_info_get_userdata(const struct fy_field_info *fi)
{
	return fy_decl_get_userdata(fy_decl_from_field_info(fi));
}

const char *fy_type_info_get_comment(const struct fy_type_info *ti)
{
	return fy_type_get_cooked_comment(fy_type_from_info(ti));
}

const char *fy_field_info_get_comment(const struct fy_field_info *fi)
{
	return fy_decl_get_cooked_comment(fy_decl_from_field_info(fi));
}

struct fy_document *fy_type_info_get_yaml_annotation(const struct fy_type_info *ti)
{
	return fy_type_get_yaml_annotation(fy_type_from_info(ti));
}

struct fy_document *fy_field_info_get_yaml_annotation(const struct fy_field_info *fi)
{
	return fy_decl_get_yaml_annotation(fy_decl_from_field_info(fi));
}

const char *fy_type_info_get_yaml_name(const struct fy_type_info *ti)
{
	return fy_type_get_yaml_name(fy_type_from_info(ti));
}

const char *fy_field_info_get_yaml_name(const struct fy_field_info *fi)
{
	return fy_decl_get_yaml_name(fy_decl_from_field_info(fi));
}
