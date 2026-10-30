/*
 * fy-cparse-backend.c - native C reflection backend, on the cparse front end
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * The declarations are parsed by cparse and the reflection's decls and types are
 * built straight from them: sizes, alignments and offsets are the target ABI's
 * (cparse's struct cp_target), not libclang's.
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <ctype.h>
#include <sys/stat.h>

#include "fy-reflection-private.h"
#include "fy-reflection-util.h"

#include "cparse-private.h"
#include "fy-cparse-backend.h"

static int cparse_reflection_setup(struct fy_reflection *rfl);
static void cparse_reflection_cleanup(struct fy_reflection *rfl);

static int cparse_import_setup(struct fy_import *imp, const void *user);
static void cparse_import_cleanup(struct fy_import *imp);

static int cparse_decl_setup(struct fy_decl *decl, void *user);
static void cparse_decl_cleanup(struct fy_decl *decl);

static int cparse_type_setup(struct fy_type *ft, void *user);
static void cparse_type_cleanup(struct fy_type *ft);

static const struct fy_reflection_backend_ops cparse_ops = {
	.reflection_setup = cparse_reflection_setup,
	.reflection_cleanup = cparse_reflection_cleanup,
	.import_setup = cparse_import_setup,
	.import_cleanup = cparse_import_cleanup,
	.type_setup = cparse_type_setup,
	.type_cleanup = cparse_type_cleanup,
	.decl_setup = cparse_decl_setup,
	.decl_cleanup = cparse_decl_cleanup,
};

const struct fy_reflection_backend fy_reflection_cparse_backend = {
	.name = "cparse",
	.ops = &cparse_ops,
};

struct cparse_reflect_backend {
	struct fy_cparse_backend_reflection_cfg cfg;
	struct fy_allocator *allocator;
};

struct cparse_import_backend {
	struct cp_tu *tu;
	char *name;
	struct cp_hashmap types;	/* cp_type object -> struct fy_type * */
	struct cp_hashmap fn_types;	/* function type spelling -> struct fy_type * */
	struct fy_type **created;	/* every type made, in order, for the final fixup */
	size_t num_created, cap_created;
};

struct cparse_decl_backend {
	struct fy_source_range source_range;
	char *raw_comment;		/* the TU does not outlive the import */
};

struct cparse_type_user {
	const struct cp_type *cpt;
};

struct cparse_decl_user {
	const struct cp_decl *cpd;
	const struct cp_field *field;
	const struct cp_enumerator *en;
	const struct cp_type *enum_type;
	enum fy_type_kind enum_kind;
};

/* growable string vectors, malloc'd */
struct strv {
	char **v;
	size_t n, cap;
};

static int strv_add(struct strv *sv, const char *s)
{
	char *d, **nv;

	if (sv->n == sv->cap) {
		nv = realloc(sv->v, (sv->cap ? sv->cap * 2 : 16) * sizeof(*nv));
		if (!nv)
			return -1;
		sv->v = nv;
		sv->cap = sv->cap ? sv->cap * 2 : 16;
	}
	d = strdup(s);
	if (!d)
		return -1;
	sv->v[sv->n++] = d;
	return 0;
}

static void strv_free(struct strv *sv)
{
	size_t i;

	for (i = 0; i < sv->n; i++)
		free(sv->v[i]);
	free(sv->v);
	memset(sv, 0, sizeof(*sv));
}

/* backend objects */

static int cparse_reflection_setup(struct fy_reflection *rfl)
{
	const struct fy_cparse_backend_reflection_cfg *cfg = rfl->cfg.backend_cfg;
	struct fy_auto_allocator_cfg acfg;
	struct cparse_reflect_backend *rflb;

	rflb = calloc(1, sizeof(*rflb));
	RFL_ASSERT(rflb);
	rfl->backend = rflb;

	if (cfg)
		rflb->cfg = *cfg;

	memset(&acfg, 0, sizeof(acfg));
	acfg.scenario = FYAST_PER_TAG_FREE;
	rflb->allocator = fy_allocator_create("auto", &acfg);
	RFL_ASSERT(rflb->allocator);

	return 0;

err_out:
	cparse_reflection_cleanup(rfl);
	return -1;
}

static void cparse_reflection_cleanup(struct fy_reflection *rfl)
{
	struct cparse_reflect_backend *rflb = rfl->backend;

	if (!rflb)
		return;
	rfl->backend = NULL;

	if (rflb->allocator)
		fy_allocator_destroy(rflb->allocator);
	free(rflb);
}

static enum fy_type_kind cp_builtin_kind(enum cp_type_kind k)
{
	switch (k) {
	case CPTK_VOID: return FYTK_VOID;
	case CPTK_BOOL: return FYTK_BOOL;
	case CPTK_CHAR: return FYTK_CHAR;
	case CPTK_SCHAR: return FYTK_SCHAR;
	case CPTK_UCHAR: return FYTK_UCHAR;
	case CPTK_SHORT: return FYTK_SHORT;
	case CPTK_USHORT: return FYTK_USHORT;
	case CPTK_INT: return FYTK_INT;
	case CPTK_UINT: return FYTK_UINT;
	case CPTK_LONG: return FYTK_LONG;
	case CPTK_ULONG: return FYTK_ULONG;
	case CPTK_LONGLONG: return FYTK_LONGLONG;
	case CPTK_ULONGLONG: return FYTK_ULONGLONG;
	case CPTK_INT128: return FYTK_INT128;
	case CPTK_UINT128: return FYTK_UINT128;
	case CPTK_FLOAT: return FYTK_FLOAT;
	case CPTK_DOUBLE: return FYTK_DOUBLE;
	case CPTK_LONGDOUBLE: return FYTK_LONGDOUBLE;
	case CPTK_FLOAT16: return FYTK_FLOAT16;
	case CPTK_FLOAT128: return FYTK_FLOAT128;
	default: return FYTK_INVALID;
	}
}

static unsigned int cp_to_fy_quals(unsigned int q)
{
	return ((q & CPQ_CONST) ? FY_QUALIFIER_CONST : 0) |
	       ((q & CPQ_VOLATILE) ? FY_QUALIFIER_VOLATILE : 0) |
	       ((q & CPQ_RESTRICT) ? FY_QUALIFIER_RESTRICT : 0);
}

static struct fy_source_file *source_file_for(struct fy_reflection *rfl, const struct cp_file *file)
{
	struct fy_source_file *sf;
	struct stat st;

	sf = fy_reflection_lookup_source_file(rfl, file->name);
	if (sf)
		return sf;
	sf = fy_source_file_create(rfl, file->name);
	if (!sf)
		return NULL;
	if (!stat(file->name, &st))
		sf->filetime = st.st_mtime;
	sf->system_header = file->system_header;
	sf->main_file = file->main_file;
	fy_source_file_list_add_tail(&rfl->source_files, sf);
	return sf;
}

static int cparse_decl_setup(struct fy_decl *decl, void *user)
{
	const struct cparse_decl_user *du = user;
	struct cparse_decl_backend *declb;
	const struct cp_decl *cpd;
	struct fy_reflection *rfl = decl->rfl;
	struct fy_source_range *range;
	int line, col;
	const struct cp_file *file;

	declb = calloc(1, sizeof(*declb));
	RFL_ASSERT(declb);
	decl->backend = declb;

	if (!du)
		return 0;
	cpd = du->cpd;

	/* what the front end makes up (the va_list tag): not a user declaration */
	if (!cpd || !cpd->file)
		decl->flags |= FYDF_IN_SYSTEM_HEADER;

	if (cpd && cpd->file) {
		file = cpd->file;

		if (file->system_header)
			decl->flags |= FYDF_IN_SYSTEM_HEADER;
		if (file->main_file)
			decl->flags |= FYDF_FROM_MAIN_FILE;

		range = &declb->source_range;
		range->start_offset = cpd->start;
		range->end_offset = cpd->end;
		cp_file_location(file, file->contents + cpd->start, &line, &col);
		range->start_line = (unsigned int)line;
		range->start_column = (unsigned int)col;
		cp_file_location(file, file->contents + cpd->end, &line, &col);
		range->end_line = (unsigned int)line;
		range->end_column = (unsigned int)col;
		range->source_file = source_file_for(rfl, file);
		RFL_ASSERT(range->source_file);
		decl->source_range = range;

		if (cpd->comment && *cpd->comment) {
			declb->raw_comment = strdup(cpd->comment);
			RFL_ASSERT(declb->raw_comment);
			decl->raw_comment = declb->raw_comment;
		}
	}

	switch (decl->decl_type) {
	case FYDT_ENUM:
		decl->enum_decl.type_kind = du->enum_kind;
		break;
	case FYDT_ENUM_VALUE:
		decl->enum_value_decl.type_kind = du->enum_kind;
		if (fy_type_kind_signess(du->enum_kind) > 0)
			decl->enum_value_decl.val.u = (uintmax_t)du->en->value;
		else
			decl->enum_value_decl.val.s = (intmax_t)du->en->value;
		break;
	case FYDT_FIELD:
		decl->field_decl.byte_offset = (size_t)du->field->offset;
		break;
	case FYDT_BITFIELD:
		/* the absolute bit offset from the start of the struct */
		decl->bitfield_decl.bit_offset =
			(size_t)(du->field->offset * 8 + du->field->bit_offset);
		decl->bitfield_decl.bit_width = (size_t)du->field->bit_width;
		break;
	default:
		break;
	}

	return 0;

err_out:
	cparse_decl_cleanup(decl);
	return -1;
}

static void cparse_decl_cleanup(struct fy_decl *decl)
{
	if (!decl || !decl->backend)
		return;
	free(((struct cparse_decl_backend *)decl->backend)->raw_comment);
	free(decl->backend);
	decl->backend = NULL;
}

static int cparse_type_setup(struct fy_type *ft, void *user)
{
	const struct cparse_type_user *tu = user;
	const struct cp_type *c;

	/* made without a cparse type: primaries, qualified clones */
	if (!tu)
		return 0;

	c = tu->cpt;

	switch (c->kind) {
	case CPTK_STRUCT:
	case CPTK_UNION:
	case CPTK_ENUM:
		if (!c->complete) {
			ft->flags |= FYTF_INCOMPLETE;
			return 0;
		}
		ft->size = (size_t)c->size;
		ft->align = (size_t)c->align;
		/*
		 * cparse did the layout. An empty record has the size 0, and the
		 * core layout would compute its alignment again.
		 */
		ft->flags |= FYTF_FIXED;
		break;
	case CPTK_TYPEDEF:
		/* a typedef of a function type has no size or alignment of its own */
		if (cp_type_canonical(c)->kind != CPTK_FUNC && cp_type_size(c) >= 0) {
			ft->size = (size_t)cp_type_size(c);
			ft->align = (size_t)cp_type_align(c);
		}
		break;
	default:
		break;
	}
	return 0;
}

static void cparse_type_cleanup(struct fy_type *ft FY_UNUSED)
{
	/* nothing */
}

/* types from cparse types */

static struct fy_type *cp_type_for(struct fy_import *imp, const struct cp_type *cpt);

static int created_add(struct cparse_import_backend *impb, struct fy_type *ft)
{
	struct fy_type **n;

	if (impb->num_created == impb->cap_created) {
		n = realloc(impb->created, (impb->cap_created ? impb->cap_created * 2 : 256) * sizeof(*n));
		if (!n)
			return -1;
		impb->created = n;
		impb->cap_created = impb->cap_created ? impb->cap_created * 2 : 256;
	}
	impb->created[impb->num_created++] = ft;
	return 0;
}

static struct fy_type *map_get(struct cp_hashmap *map, const void *key, size_t len)
{
	return cp_hashmap_get(map, key, (int)len);
}

static int map_put(struct cp_tu *tu, struct cp_hashmap *map, const void *key, size_t len,
		   struct fy_type *ft)
{
	struct cp_hashmap_entry *ent = cp_hashmap_get_or_insert(tu, map, key, (int)len);

	if (!ent)
		return -1;
	ent->val = ft;
	return 0;
}

/* the type that a tag or typedef node stands for */
static const struct cp_type *named_object(const struct cp_type *cpt)
{
	const struct cp_type *u = cpt->unqual;

	if (u->kind == CPTK_ELABORATED)
		return u->base->unqual;
	return u;
}

static const char *tag_decl_name(struct cp_tu *tu, const struct cp_type *tag)
{
	const char *sp, *kw;

	if (tag->name)
		return tag->name;
	if (tag->linkage_name)
		return cp_format(tu, "@%s", tag->linkage_name);

	/* a fully anonymous one is named by where it is, as clang spells it */
	sp = cp_type_spelling(tu, cp_type_elaborated(tu, (struct cp_type *)tag));
	kw = strchr(sp, ' ');
	return kw ? kw + 1 : sp;
}

static struct fy_type *named_type(struct fy_import *imp, const struct cp_type *u)
{
	int rc;
	struct fy_reflection *rfl = imp->rfl;
	struct cparse_import_backend *impb = imp->backend;
	struct cp_tu *tu = impb->tu;
	struct cparse_decl_user du;
	struct cparse_type_user ftu;
	struct fy_decl *decl = NULL, *declf;
	struct fy_type *ft = NULL, *ft_dep = NULL, *ftf;
	enum fy_decl_type decl_type;
	enum fy_type_kind type_kind;
	enum fy_type_flags flags = 0;
	const char *name;
	const void *key;
	int i;
	const struct cp_field *f;
	char anon[48];
	const char *fname;
	struct fy_type *ftt;
	const struct cp_type *fo;
	const void *fkey;
	const struct cp_enumerator *e;
	struct fy_type *ft_val;

	/* one reflection type per declaration, however many nodes name it */
	key = u->decl ? (const void *)u->decl : (const void *)u;
	ft = map_get(&impb->types, &key, sizeof(key));
	if (ft)
		return ft;

	memset(&du, 0, sizeof(du));
	memset(&ftu, 0, sizeof(ftu));
	ftu.cpt = u;
	du.cpd = u->decl;

	switch (u->kind) {
	case CPTK_TYPEDEF:
		ft_dep = cp_type_for(imp, u->base);
		RFL_ASSERT(ft_dep);

		/* the base can lead back here (a record with a field of this typedef) */
		ft = map_get(&impb->types, &key, sizeof(key));
		if (ft)
			return ft;

		decl = fy_decl_create(rfl, imp, NULL, FYDT_TYPEDEF, u->name, &du);
		RFL_ASSERT(decl);
		ft = fy_type_create(rfl, FYTK_TYPEDEF, 0, NULL, decl, ft_dep, &ftu, 0);
		RFL_ASSERT(ft);
		break;

	case CPTK_STRUCT:
	case CPTK_UNION:
		type_kind = u->kind == CPTK_STRUCT ? FYTK_STRUCT : FYTK_UNION;
		decl_type = u->kind == CPTK_STRUCT ? FYDT_STRUCT : FYDT_UNION;
		name = tag_decl_name(tu, u);
		if (!u->name) {
			flags |= FYTF_ANONYMOUS;
			/* at file scope; a user of the type clears it again */
			if (!u->decl || !u->decl->parent)
				flags |= FYTF_ANONYMOUS_GLOBAL;
		}
		if (!u->complete)
			flags |= FYTF_INCOMPLETE;

		decl = fy_decl_create(rfl, imp, NULL, decl_type, name, &du);
		RFL_ASSERT(decl);
		ft = fy_type_create(rfl, type_kind, flags, NULL, decl, NULL, &ftu, 0);
		RFL_ASSERT(ft);
		break;

	case CPTK_ENUM:
		name = tag_decl_name(tu, u);
		if (!u->name) {
			flags |= FYTF_ANONYMOUS;
			/* at file scope; a user of the type clears it again */
			if (!u->decl || !u->decl->parent)
				flags |= FYTF_ANONYMOUS_GLOBAL;
		}
		if (!u->complete)
			flags |= FYTF_INCOMPLETE;
		if (u->base) {
			ft_dep = cp_type_for(imp, u->base);
			RFL_ASSERT(ft_dep);
			du.enum_kind = ft_dep->type_kind;
		}

		decl = fy_decl_create(rfl, imp, NULL, FYDT_ENUM, name, &du);
		RFL_ASSERT(decl);
		ft = fy_type_create(rfl, FYTK_ENUM, flags, NULL, decl, ft_dep, &ftu, 0);
		RFL_ASSERT(ft);
		break;

	default:
		RFL_ERROR_CHECK(false, "cparse: not a named type");
	}

	/* register before the children: a field can point back at the record */
	rc = map_put(tu, &impb->types, &key, sizeof(key), ft);
	RFL_ASSERT(!rc);
	rc = created_add(impb, ft);
	RFL_ASSERT(!rc);

	fy_decl_list_add_tail(&rfl->decls, decl);
	fy_type_list_add_tail(&rfl->types, ft);

	if ((u->kind == CPTK_STRUCT || u->kind == CPTK_UNION) && u->complete) {
		for (i = 0; i < u->num_fields; i++) {
			f = u->fields[i];
			fname = f->name;

			if (!fname && f->is_anon_record) {
				snprintf(anon, sizeof(anon), "@anon-%llu",
					 (unsigned long long)(f->offset * 8));
				fname = anon;
			}

			memset(&du, 0, sizeof(du));
			du.cpd = f->decl;
			du.field = f;
			declf = fy_decl_create(rfl, imp, decl, f->bit_width >= 0 ? FYDT_BITFIELD : FYDT_FIELD,
					       fname, &du);
			RFL_ASSERT(declf);

			ftf = cp_type_for(imp, f->type);
			RFL_ASSERT(ftf);
			declf->type = ftf;

			if (f->is_anon_record) {
				fo = named_object(f->type);
				fkey = fo->decl ? (const void *)fo->decl : (const void *)fo;

				ftt = map_get(&impb->types, &fkey, sizeof(fkey));
				if (ftt)
					fy_type_set_flags(ftt, FYTF_ANONYMOUS_RECORD_DECL,
							  FYTF_ANONYMOUS_RECORD_DECL);
			}

			fy_decl_list_add_tail(&decl->children, declf);
		}
	} else if (u->kind == CPTK_ENUM && u->complete) {
		for (i = 0; i < u->num_enumerators; i++) {
			e = u->enumerators[i];

			ft_val = ft_dep;

			/* an enumerator is an int, unless its value needs the enum's type */
			if (e->is_unsigned ? (uint64_t)e->value <= INT_MAX :
			    (e->value >= INT_MIN && e->value <= INT_MAX))
				ft_val = fy_reflection_get_primary_type(rfl, FYTK_INT, 0);
			RFL_ASSERT(ft_val);

			memset(&du, 0, sizeof(du));
			du.cpd = e->decl;
			du.en = e;
			du.enum_type = u;
			du.enum_kind = ft_val->type_kind;
			declf = fy_decl_create(rfl, imp, decl, FYDT_ENUM_VALUE, e->name, &du);
			RFL_ASSERT(declf);
			declf->type = ft_val;
			fy_decl_list_add_tail(&decl->children, declf);
		}
	}

	return ft;

err_out:
	return NULL;
}

/* the reflection type for a cparse type, creating what is needed */
static struct fy_type *cp_type_for(struct fy_import *imp, const struct cp_type *cpt)
{
	int rc;
	struct fy_reflection *rfl = imp->rfl;
	struct cparse_import_backend *impb = imp->backend;
	struct cp_tu *tu = impb->tu;
	const struct cp_type *u = cpt->unqual;
	const struct cp_target *target = tu->target;
	unsigned int quals = cp_to_fy_quals(cpt->quals);
	struct fy_type *ft = NULL, *dep, *base;
	enum fy_type_kind kind;
	const char *spelling;
	struct fy_decl *decl;

	/* an _Atomic(T) is reflected as T, as the clang backend does */

	if (u->kind < CPTK_BUILTIN_END) {
		kind = cp_builtin_kind(u->kind);
		RFL_ERROR_CHECK(kind != FYTK_INVALID, "cparse: unsupported builtin type");
		ft = fy_reflection_get_primary_type(rfl, kind, quals);
		RFL_ASSERT(ft);
		/* the primary types carry the host's sizes: the target decides */
		if (kind != FYTK_VOID && (ft->size != (size_t)u->size || ft->align != (size_t)u->align)) {
			ft->size = (size_t)u->size;
			ft->align = (size_t)u->align;
		}
		return ft;
	}

	switch (u->kind) {
	case CPTK_TYPEDEF:
	case CPTK_STRUCT:
	case CPTK_UNION:
	case CPTK_ENUM:
	case CPTK_ELABORATED:
		base = named_type(imp, named_object(cpt));
		RFL_ASSERT(base);
		if (!quals)
			return base;
		ft = fy_type_with_qualifiers(base, quals);
		if (!ft) {
			ft = fy_type_create_with_qualifiers(base, quals, NULL);
			RFL_ASSERT(ft);
			fy_type_list_add_tail(&rfl->types, ft);
		}
		return ft;

	case CPTK_PTR:
		dep = cp_type_for(imp, u->base);
		RFL_ASSERT(dep);
		ft = fy_type_lookup_pointer(dep, quals);
		if (!ft) {
			ft = fy_type_create_pointer(dep, quals);
			RFL_ASSERT(ft);
			ft->size = (size_t)target->ptr.size;
			ft->align = (size_t)target->ptr.align;
		}
		return ft;

	case CPTK_ARRAY:
		dep = cp_type_for(imp, u->base);
		RFL_ASSERT(dep);
		if (u->count >= 0) {
			ft = fy_type_lookup_array(dep, 0, (uintmax_t)u->count);
			if (!ft)
				ft = fy_type_create_array(dep, 0, (uintmax_t)u->count);
		} else {
			ft = fy_type_create(rfl, FYTK_INCOMPLETEARRAY, FYTF_SYNTHETIC, NULL, NULL, dep,
					    NULL, 0);
			RFL_ASSERT(ft);
			fy_type_list_add_tail(&rfl->types, ft);
		}
		RFL_ASSERT(ft);
		return ft;

	case CPTK_FUNC: {
		spelling = cp_type_spelling(tu, u);

		ft = map_get(&impb->fn_types, spelling, strlen(spelling));
		if (ft)
			return ft;
		decl = fy_decl_create(rfl, imp, NULL, FYDT_FUNCTION, spelling, NULL);
		RFL_ASSERT(decl);
		ft = fy_type_create(rfl, FYTK_FUNCTION, FYTF_SYNTHETIC, NULL, decl, NULL, NULL, 0);
		RFL_ASSERT(ft);
		fy_decl_list_add_tail(&rfl->decls, decl);
		fy_type_list_add_tail(&rfl->types, ft);
		rc = map_put(tu, &impb->fn_types, spelling, strlen(spelling), ft);
		RFL_ASSERT(!rc);
		return ft;
	}

	default:
		RFL_ERROR_CHECK(false, "cparse: unsupported type '%s'", cp_type_spelling(tu, cpt));
	}

err_out:
	return NULL;
}

/* the import */

static int import_build(struct fy_import *imp)
{
	int rc;
	struct fy_type *ft;
	struct cparse_import_backend *impb = imp->backend;
	struct fy_reflection *rfl = imp->rfl;
	struct cp_tu *tu = impb->tu;
	size_t i;
	const struct cp_decl *d;

	for (i = 0; i < tu->num_decls; i++) {
		d = tu->decls[i];

		switch (d->kind) {
		case CPDK_STRUCT:
		case CPDK_UNION:
		case CPDK_ENUM:
			ft = named_type(imp, d->type->unqual);
			RFL_ASSERT(ft);
			break;
		case CPDK_TYPEDEF:
			ft = named_type(imp, d->type);
			RFL_ASSERT(ft);
			break;
		default:
			break;
		}
	}

	/* every type has its size and alignment now; settle what is left */
	for (i = 0; i < impb->num_created; i++) {
		rc = fy_type_fixup(impb->created[i]);
		RFL_ASSERT(!rc);
	}

	return 0;

err_out:
	return -1;
}

static int cparse_import_setup(struct fy_import *imp, const void *user)
{
	int rc;
	const struct fy_cparse_backend_import_cfg *cfg = user;
	struct fy_reflection *rfl = imp->rfl;
	struct cparse_reflect_backend *rflb = rfl->backend;
	struct cparse_import_backend *impb;
	struct strv defs = { 0 }, forced = { 0 }, incs = { 0 }, macros = { 0 };
	const struct cp_predef_set *set = NULL;
	struct stat st;
	size_t nset = 0;
	struct cp_include_path *paths = NULL;
	const char **defines = NULL;
	struct cp_options opts;
	struct cp_target target;
	const char *targs[24], *terr = NULL;
	struct cp_file *file;
	struct cp_token *tokens;
	size_t i, npaths = 0, ndefs = 0;
	int ntargs, argc, ai;
	const char * const *argv;
	size_t d;
	const char *a;
	const char *val;
	enum cp_include_kind kind;
	char *u;
	const struct cp_diag *dg;

	if (!cfg || !cfg->file)
		return -1;
	RFL_ASSERT(rflb);

	impb = calloc(1, sizeof(*impb));
	RFL_ASSERT(impb);
	imp->backend = impb;

	argc = cfg->argc;
	argv = cfg->argv;
	if (!argv)
		argc = 0;

	memset(&opts, 0, sizeof(opts));
	opts.allocator = rflb->allocator;

	/* the target: the host unless the arguments say otherwise */
	ntargs = cp_target_args_filter(argc, argv, targs, (int)(ARRAY_SIZE(targs)));
	if (ntargs) {
		rc = cp_target_from_args(NULL, ntargs, targs, &target, &terr);
		RFL_ERROR_CHECK(!rc,
				"cparse: unsupported target (%s)", terr ? terr : "?");
		opts.target = &target;
	}

	/* what the compiler predefines for that target, then the arguments' own */
	if (!rflb->cfg.no_predefs) {
		set = cp_predefs_find(rflb->cfg.compiler, opts.target ? opts.target : cp_target_host());
		if (!set)
			rfl_warning(rfl, "cparse: no predefined macros for the target\n");
	}

	paths = calloc(argc + (set ? set->num_includes : 0) + 1, sizeof(*paths));
	RFL_ASSERT(paths);

	/* the macro files of the configuration come before the ones of the arguments */
	for (i = 0; i < cfg->num_macro_files; i++) {
		rc = strv_add(&macros, cfg->macro_files[i]);
		RFL_ASSERT(!rc);
	}

	for (ai = 0; ai < argc; ai++) {
		a = argv[ai];
		val = NULL;
		kind = CPIK_USER;

		if (!strncmp(a, "-I", 2)) {
			val = a[2] ? a + 2 : (ai + 1 < argc ? argv[++ai] : NULL);
		} else if (!strcmp(a, "-isystem") && ai + 1 < argc) {
			val = argv[++ai];
			kind = CPIK_SYSTEM;
		} else if (!strcmp(a, "-iquote") && ai + 1 < argc) {
			val = argv[++ai];
			kind = CPIK_QUOTE;
		} else if (!strncmp(a, "-D", 2)) {
			val = a[2] ? a + 2 : (ai + 1 < argc ? argv[++ai] : NULL);
			if (val) {
				rc = strv_add(&defs, val);
				RFL_ASSERT(!rc);
			}
			continue;
		} else if (!strncmp(a, "-U", 2)) {
			val = a[2] ? a + 2 : (ai + 1 < argc ? argv[++ai] : NULL);
			if (val) {
				u = malloc(strlen(val) + 2);

				RFL_ASSERT(u);
				sprintf(u, "-%s", val);
				rc = strv_add(&defs, u);
				RFL_ASSERT(!rc);
				free(u);
			}
			continue;
		} else if (!strcmp(a, "-imacros") && ai + 1 < argc) {
			rc = strv_add(&macros, argv[++ai]);
			RFL_ASSERT(!rc);
			continue;
		} else if (!strcmp(a, "-include") && ai + 1 < argc) {
			rc = strv_add(&forced, argv[++ai]);
			RFL_ASSERT(!rc);
			continue;
		} else if (!strncmp(a, "-x", 2)) {
			val = a[2] ? a + 2 : (ai + 1 < argc ? argv[++ai] : NULL);
			RFL_ERROR_CHECK(!val || !strcmp(val, "c") || !strcmp(val, "c-header") ||
					!strcmp(val, "none"),
					"cparse: unsupported language '%s' (only C)", val);
			continue;
		} else if (!strncmp(a, "-std=", 5)) {
			opts.std_c23 = !strcmp(a + 5, "c23") || !strcmp(a + 5, "c2x") ||
				       !strcmp(a + 5, "gnu23") || !strcmp(a + 5, "gnu2x");
			continue;
		} else {
			/* the target options were taken above; the rest do not matter here */
			continue;
		}
		if (val) {
			rc = strv_add(&incs, val);
			RFL_ASSERT(!rc);
			paths[npaths].kind = kind;
			paths[npaths++].path = incs.v[incs.n - 1];
		}
	}

	/* the system directories of the compiler, as far as they are there now */
	for (d = 0; set && d < set->num_includes; d++) {
		if (stat(set->includes[d], &st) || !S_ISDIR(st.st_mode))
			continue;
		paths[npaths].kind = CPIK_SYSTEM;
		paths[npaths++].path = set->includes[d];
	}

	/* the macros of the compiler come first, the arguments' own after them */
	nset = set ? set->num_defines : 0;
	defines = calloc(nset + defs.n + 1, sizeof(*defines));
	RFL_ASSERT(defines);
	for (d = 0; d < nset; d++)
		defines[ndefs++] = set->defines[d];
	for (i = 0; i < defs.n; i++)
		defines[ndefs++] = defs.v[i];

	opts.include_paths = paths;
	opts.num_include_paths = npaths;
	opts.defines = defines;
	opts.num_defines = ndefs;
	opts.macro_files = (const char *const *)macros.v;
	opts.num_macro_files = macros.n;
	opts.force_includes = (const char *const *)forced.v;
	opts.num_force_includes = forced.n;

	impb->tu = cp_tu_create(&opts);
	RFL_ERROR_CHECK(impb->tu, "cparse: cannot create the translation unit");

	file = cp_file_add_from_path(impb->tu, cfg->file, false, true);
	tokens = file ? cp_preprocess(impb->tu, file) : NULL;

	/* a failed preprocessing leaves nothing to parse */
	if (!tokens || cp_tu_failed(impb->tu)) {
		for (i = 0; i < impb->tu->num_diags; i++) {
			dg = &impb->tu->diags[i];

			if (dg->severity == CPDS_ERROR)
				rfl_error(rfl, "cparse: %s:%d:%d: %s\n",
					  dg->file ? dg->file->name : cfg->file, dg->line, dg->column,
					  dg->msg);
		}
		goto err_out;
	}

	cp_parse(impb->tu, tokens);
	cp_attach_comments(impb->tu);
	if (rflb->cfg.display_diagnostics) {
		for (i = 0; i < impb->tu->num_diags; i++) {
			dg = &impb->tu->diags[i];

			fprintf(stderr, "%s:%d:%d: %s: %s\n", dg->file ? dg->file->name : cfg->file,
				dg->line, dg->column, dg->severity == CPDS_ERROR ? "error" : "warning",
				dg->msg);
		}
	}

	rc = import_build(imp);
	RFL_ASSERT(!rc);

	if (asprintf(&impb->name, "%s-%s", cfg->file, impb->tu->target->triple) < 0)
		impb->name = NULL;
	RFL_ASSERT(impb->name);
	imp->name = impb->name;

	strv_free(&defs);
	strv_free(&forced);
	strv_free(&macros);
	strv_free(&incs);
	free(paths);
	free(defines);
	return 0;

err_out:
	strv_free(&defs);
	strv_free(&forced);
	strv_free(&macros);
	strv_free(&incs);
	free(paths);
	free(defines);
	cparse_import_cleanup(imp);
	return -1;
}

static void cparse_import_cleanup(struct fy_import *imp)
{
	struct cparse_import_backend *impb = imp->backend;

	if (!impb)
		return;
	imp->backend = NULL;

	free(impb->created);
	free(impb->name);
	cp_tu_destroy(impb->tu);
	free(impb);
}
