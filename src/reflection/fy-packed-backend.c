/*
 * fy-packed-backend.c - Packed blob C type backend
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <float.h>
#include <assert.h>
#include <limits.h>
#include <ctype.h>

#if HAVE_ALLOCA
#include <alloca.h>
#endif

#include "fy-endian.h"
#include "fy-blob.h"

#include "fy-reflection-private.h"

#include "fy-packed-backend.h"

static int packed_reflection_setup(struct fy_reflection *rfl);
static void packed_reflection_cleanup(struct fy_reflection *rfl);

static int packed_import_setup(struct fy_import *imp, const void *user);
static void packed_import_cleanup(struct fy_import *imp);

static int packed_decl_setup(struct fy_decl *decl, void *user);
static void packed_decl_cleanup(struct fy_decl *decl);

static int packed_type_setup(struct fy_type *ft, void *user);
static void packed_type_cleanup(struct fy_type *ft);

static const struct fy_reflection_backend_ops packed_ops = {
	.reflection_setup = packed_reflection_setup,
	.reflection_cleanup = packed_reflection_cleanup,
	.import_setup = packed_import_setup,
	.import_cleanup = packed_import_cleanup,
	.type_setup = packed_type_setup,
	.type_cleanup = packed_type_cleanup,
	.decl_setup = packed_decl_setup,
	.decl_cleanup = packed_decl_cleanup,
};

const struct fy_reflection_backend fy_reflection_packed_backend = {
	.name = "packed",
	.ops = &packed_ops,
};

struct packed_reflect_backend {
	const struct fy_packed_type_info *type_info;
	/* if we're using a blob */
	struct fy_packed_type_info gen_type_info;
	struct fy_type_p *gen_types;
	struct fy_decl_p *gen_decls;
	const void *blob;	/* pointer to the original blob */
	size_t blob_size;
	void *blob_copy;	/* the copy */
	int next_anonymous_struct;
	int next_anonymous_union;
	int next_anonymous_enum;
	int next_anonymous_field;
};

struct packed_import_backend {
	char *name;
};

struct packed_decl_user {
	const struct fy_decl_p *declp;
	const struct fy_decl_p *decl_parentp;
	bool anonymous;
};

struct packed_decl_backend {
	const struct fy_decl_p *declp;
	const struct fy_type_p *typep;
	char *yaml_comment;
};

struct packed_type_user {
	const struct fy_type_p *typep;
};

struct packed_type_backend {
	const struct fy_type_p *typep;
	const struct fy_type_p *dependent_typep;
};

static struct fy_decl *
packed_lookup_decl_by_declp(struct fy_reflection *rfl, const struct fy_decl_p *declp)
{
	struct fy_decl *decl;
	struct packed_decl_backend *declb;

	if (!declp)
		return NULL;

	for (decl = fy_decl_list_head(&rfl->decls); decl != NULL; decl = fy_decl_next(&rfl->decls, decl)) {
		declb = decl->backend;
		if (!declb)
			continue;
		if (declp == declb->declp)
			return decl;
	}
	return NULL;
}

static struct fy_type *
packed_lookup_type_by_typep(struct fy_reflection *rfl, const struct fy_type_p *typep)
{
	struct fy_type *ft;
	struct packed_type_backend *ftb;

	if (!typep)
		return NULL;

	RFL_ASSERT(!fy_type_kind_is_primary(typep->type_kind));

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {
		ftb = ft->backend;
		if (!ftb)
			continue;
		if (typep == ftb->typep)
			return ft;
	}
	return NULL;
err_out:
	return NULL;
}

static struct fy_type *
packed_lookup_type_from_type_p_id(struct fy_reflection *rfl, fy_type_p_id type)
{
	struct packed_reflect_backend *rflb;
	const struct fy_packed_type_info *ti;
	const struct fy_type_p *typep;
	enum fy_type_kind type_kind;
	struct fy_type *ft;
	unsigned int base_id, quals;

	assert(rfl);
	rflb = rfl->backend;
	RFL_ASSERT(rflb);

	ti = rflb->type_info;

	if (ti->uses_pointers || type.id >= FY_TYPE_ID_OFFSET) {
		typep = fy_type_p_from_id(ti, type);
		RFL_ASSERT(typep);
		return packed_lookup_type_by_typep(rfl, typep);
	}
	// return rfl->primary_types[type.id];
	base_id = (type.id & ((1 << FYTK_PRIMARY_BITS) - 1));
	type_kind = base_id + FYTK_PRIMARY_FIRST;
	quals = type.id & (FY_QUALIFIER_CONST | FY_QUALIFIER_VOLATILE | FY_QUALIFIER_RESTRICT);

	ft = fy_reflection_get_primary_type(rfl, type_kind, quals);
	RFL_ASSERT(ft);

	return ft;

err_out:
	return NULL;
}

static int packed_reflection_setup_blob(struct fy_reflection *rfl);

static int packed_reflection_setup(struct fy_reflection *rfl)
{
	struct packed_reflect_backend *rflb;
	const struct fy_packed_backend_reflection_cfg *cfg = rfl->cfg.backend_cfg;
	int ret;

	/* verify */
	if (!cfg)
	       return -1;

	switch (cfg->type) {
	case FYPRT_TYPE_INFO:
		if (!cfg->type_info)
			return -1;
		break;

	case FYPRT_BLOB:
		if (!cfg->blob || cfg->blob_size < PGHDR_SIZE)
			return -1;
		break;

	default:
		return -1;
	}

	rflb = malloc(sizeof(*rflb));
	RFL_ASSERT(rflb);
	memset(rflb, 0, sizeof(*rflb));

	rfl->backend = rflb;

	switch (cfg->type) {
	case FYPRT_TYPE_INFO:
		rflb->type_info = cfg->type_info;
		break;

	case FYPRT_BLOB:
		ret = packed_reflection_setup_blob(rfl);
		RFL_ASSERT(!ret);
		break;
	}

	rflb->next_anonymous_struct = 1;
	rflb->next_anonymous_union = 1;
	rflb->next_anonymous_enum = 1;
	rflb->next_anonymous_field = 1;

	return 0;

err_out:
	fprintf(stderr, "%s:%d %s error\n", __FILE__, __LINE__, __func__);
	packed_reflection_cleanup(rfl);
	return -1;
}

static void packed_reflection_cleanup(struct fy_reflection *rfl)
{
	struct packed_reflect_backend *rflb;

	if (!rfl || !rfl->backend)
		return;

	rflb = rfl->backend;
	rfl->backend = NULL;

	if (rflb->gen_types)
		free(rflb->gen_types);
	if (rflb->gen_decls)
		free(rflb->gen_decls);

	if (rflb->blob_copy)
		free(rflb->blob_copy);

	free(rflb);
}

static int packed_do_import(struct fy_import *imp)
{
	struct fy_reflection *rfl = imp->rfl;
	struct packed_reflect_backend *rflb = rfl->backend;
	const struct fy_packed_type_info *ti = rflb->type_info;
	const struct fy_type_p *typep;
	const struct fy_decl_p *declp;
	struct packed_decl_user declu_local, *declu = &declu_local;
	struct packed_decl_user declu_field_local, *declu_field = &declu_field_local;
	struct packed_type_user ftu_local, *ftu = &ftu_local;
	struct packed_type_backend *ftb;
	struct fy_decl *decl, *declc;
	struct packed_decl_backend *declb;
	struct fy_type *ft, *ftt, *ft_dep;
	enum fy_type_flags flags;
	const char *decl_name;
	char *generated_decl_name = NULL;
	enum fy_type_kind type_kind;
	unsigned int quals;
	int i, ret;

	(void)rflb;

	fprintf(stderr, "%s: types\n", __func__);
	for (typep = ti->types, i = 0; i < ti->types_count; i++, typep++) {
		fprintf(stderr, "<T#%d/%s D#%d DT#%d EC=%ju\n", i + FY_TYPE_ID_OFFSET,
				fy_type_kind_name(typep->type_kind),
				typep->decl.id,
				typep->dependent_type.id,
				typep->element_count);

	}

	fprintf(stderr, "%s: decls\n", __func__);
	for (declp = ti->decls, i = 0; i < ti->decls_count; i++, declp++) {
		fprintf(stderr, "<D#%d %s T#%d C=%s\n", i + FY_DECL_ID_OFFSET,
				fy_str_from_p(ti, declp->name),
				declp->type.id,
				fy_str_from_p(ti, declp->comment));

	}

	memset(declu, 0, sizeof(*declu));
	memset(declu_field, 0, sizeof(*declu_field));

	/* first create the types */
	for (typep = ti->types, i = 0; i < ti->types_count; i++, typep++) {

		fprintf(stderr, "!T#%d D#%d\n", i + FY_TYPE_ID_OFFSET, typep->decl.id);

		type_kind = typep->type_kind;

		/* primaries should never be present */
		RFL_ASSERT(!fy_type_kind_is_primary(type_kind));

		declp = fy_decl_p_from_id(ti, typep->decl);

		/* an elaborated type has the same decl as the named type */
		if (declp && (typep->flags & FYTF_ELABORATED)) {
			decl = packed_lookup_decl_by_declp(rfl, declp);
			RFL_ASSERT(decl);

			/* get the named type */
			ft = decl->type;
			RFL_ASSERT(ft);

			/* must be not elaborated */
			RFL_ASSERT(!(ft->flags & FYTF_ELABORATED));

			memset(ftu, 0, sizeof(*ftu));
			ftu->typep = typep;

			quals = ((typep->flags & FYTF_CONST) ? FY_QUALIFIER_CONST : 0) |
				((typep->flags & FYTF_VOLATILE) ? FY_QUALIFIER_VOLATILE : 0) |
				((typep->flags & FYTF_RESTRICT) ? FY_QUALIFIER_RESTRICT : 0);
			RFL_ASSERT(quals);

			ftt = fy_type_create_with_qualifiers(ft, quals, ftu);
			RFL_ASSERT(ftt);

			fy_type_list_add_tail(&rfl->types, ftt);

			continue;
		}

		decl_name = NULL;

		if (declp) {
			decl_name = fy_str_from_p(ti, declp->name);

			if (!decl_name) {

				if (!(type_kind == FYTK_STRUCT || type_kind == FYTK_UNION || type_kind == FYTK_ENUM)) {
					printf("%s\n", fy_type_kind_name(type_kind));
				}
				RFL_ASSERT(type_kind == FYTK_STRUCT || type_kind == FYTK_UNION || type_kind == FYTK_ENUM);

				ret = asprintf(&generated_decl_name, "@anonymous-%d",
						type_kind == FYTK_STRUCT ? rflb->next_anonymous_struct++ :
						type_kind == FYTK_UNION ? rflb->next_anonymous_union++ :
						rflb->next_anonymous_enum++);
				RFL_ASSERT(ret >= 0);
				decl_name = generated_decl_name;
			}
		}

		flags = 0;
		if (fy_type_kind_is_primitive(typep->type_kind))
			flags |= FYTF_SYNTHETIC;

		if (declp) {
			declu->declp = declp;
			
			decl = fy_decl_create(rfl, imp, NULL, declp->decl_type, decl_name, declu);
			RFL_ASSERT(decl);
		} else
			decl = NULL;

		if (fy_type_kind_is_dependent(typep->type_kind)) {
			ft_dep = packed_lookup_type_from_type_p_id(rfl, typep->dependent_type);
			if (!ft_dep) {
				fprintf(stderr, "%s:%d %s #%d no dep at this time\n", __FILE__, __LINE__, __func__, (int)(typep - ti->types));
			}
		} else
			ft_dep = NULL;

		memset(ftu, 0, sizeof(*ftu));
		ftu->typep = typep;

		ft = fy_type_create(rfl, typep->type_kind, flags, NULL, decl, ft_dep, ftu, 0);	/* always generated name */
		RFL_ASSERT(ft);

		if (generated_decl_name) {
			free(generated_decl_name);
			generated_decl_name = NULL;
		}

		fy_type_list_add_tail(&rfl->types, ft);

		if (decl)
			fy_decl_list_add_tail(&rfl->decls, decl);
	}

	/* do a pass to relink decl->type if that was missing */
	for (typep = ti->types, i = 0; i < ti->types_count; i++, typep++) {

		ft = packed_lookup_type_by_typep(rfl, typep);
		RFL_ASSERT(ft);

		decl = fy_type_decl(ft);
		if (!decl)
			continue;

		if (ft->flags & FYTF_ELABORATED)
			continue;

		/* only for struct, union, enums */
		if (decl->decl_type != FYDT_STRUCT && decl->decl_type != FYDT_UNION && decl->decl_type != FYDT_ENUM)
			continue;

		for (declc = fy_decl_list_head(&decl->children); declc != NULL; declc = fy_decl_next(&decl->children, declc)) {

			if (declc->type)
				continue;

			declb = declc->backend;
			RFL_ASSERT(declb);

			declp = declb->declp;

			ftt = packed_lookup_type_from_type_p_id(rfl, declp->type);
			RFL_ASSERT(ftt);

			declc->type = ftt;
		}
	}

	/* now do the pass for the dependent types, and fix up */
	for (typep = ti->types, i = 0; i < ti->types_count; i++, typep++) {

		ft = packed_lookup_type_by_typep(rfl, typep);
		RFL_ASSERT(ft);

		if (ft->flags & FYTF_UNRESOLVED) {

			ftb = ft->backend;
			RFL_ASSERT(ftb);

			ftt = packed_lookup_type_by_typep(rfl, ftb->dependent_typep);
			RFL_ASSERT(ftt);

			ret = fy_type_set_dependent(ft, ftt);
			RFL_ASSERT(!ret);

			ret = fy_type_generate_name(ft);
			RFL_ASSERT(!ret);
		}

		/* fixup type */
		fy_type_fixup(ft);
	}

	return 0;

err_out:
	return -1;
}

static int packed_import_setup(struct fy_import *imp, const void *user)
{
	struct fy_reflection *rfl;
	struct packed_reflect_backend *rflb;
	struct packed_import_backend *impb;
	int len, ret;

	rfl = imp->rfl;
	rflb = rfl->backend;

	impb = malloc(sizeof(*impb));
	RFL_ASSERT(impb);
	memset(impb, 0, sizeof(*impb));

	imp->backend = impb;

	RFL_ASSERT(rflb);

	len = snprintf(NULL, 0, "packed@%p", rflb->type_info);
	RFL_ASSERT(len >= 0);

	impb->name = malloc(len + 1);
	RFL_ASSERT(impb->name);

	snprintf(impb->name, len + 1, "packed@%p", rflb->type_info);

	/* forward */
	imp->name = impb->name;

	ret = packed_do_import(imp);
	RFL_ASSERT(!ret);

	fy_reflection_renumber(rfl);

	return 0;

err_out:
	fprintf(stderr, "%s:%d %s error\n", __FILE__, __LINE__, __func__);
	packed_import_cleanup(imp);
	return -1;
}

static void packed_import_cleanup(struct fy_import *imp)
{
	struct packed_import_backend *impb;

	if (!imp || !imp->backend)
		return;

	impb = imp->backend;
	imp->backend = NULL;

	if (impb->name)
		free(impb->name);

	free(impb);
}

static int packed_decl_setup(struct fy_decl *decl, void *user)
{
	struct fy_reflection *rfl;
	struct packed_reflect_backend *rflb;
	const struct fy_packed_type_info *ti;
	const struct packed_decl_user *declu;
	enum fy_type_kind type_kind;
	struct packed_decl_backend *declb;
	const struct fy_decl_p *declp, *declpe, *declpf;
	struct packed_decl_user declfu_local, *declfu = &declfu_local;
	struct fy_decl *declf;
	struct fy_type *ftt;
	const char *comment, *field_name;
	char field_name_buf[64];
	int i, signess, len;

	/* fake declaration */
	if (!user)
		return 0;

	assert(decl);
	declu = user;

	rfl = decl->rfl;

	rflb = rfl->backend;
	RFL_ASSERT(rflb);

	ti = rflb->type_info;

	declb = malloc(sizeof(*declb));
	RFL_ASSERT(declb);
	memset(declb, 0, sizeof(*declb));

	decl->backend = declb;

	declp = declu->declp;
	declb->declp = declp;

	/* no name? it's anonymous */
	decl->flags &= ~FYDF_IN_SYSTEM_HEADER;
	decl->flags |= FYDF_FROM_MAIN_FILE;

	switch (decl->decl_type) {

	case FYDT_STRUCT:
	case FYDT_UNION:
	case FYDT_ENUM:

		memset(declfu, 0, sizeof(*declfu));
		declpe = ti->decls + ti->decls_count;
		for (declpf = declp + 1, i = 0; declpf < declpe; declpf++, i++) {

			if (decl->decl_type == FYDT_ENUM) {
				if (declpf->decl_type != FYDT_ENUM_VALUE)
					break;
			} else {
				if (declpf->decl_type != FYDT_FIELD && declpf->decl_type != FYDT_BITFIELD)
					break;
			}
			declfu->declp = declpf;
			field_name = fy_str_from_p(ti, declpf->name);

			if (!field_name) {
				len = snprintf(field_name_buf, sizeof(field_name_buf) - 1, "@anonymous-%d",
					rflb->next_anonymous_field++);
				RFL_ASSERT((size_t)len < sizeof(field_name_buf));
				field_name_buf[len] = '\0';
				field_name = field_name_buf;
			}

			declf = fy_decl_create(rfl, decl->imp, decl, declpf->decl_type, field_name, declfu);
			RFL_ASSERT(declf);

			ftt = packed_lookup_type_from_type_p_id(rfl, declpf->type);
			if (ftt) {
				declf->type = ftt;
			} else {
				fprintf(stderr, "%s:%d %s missing ftt from %d\n", __FILE__, __LINE__, __func__, declpf->type.id);
			}

			fy_decl_list_add_tail(&decl->children, declf);
		}

		break;

	case FYDT_ENUM_VALUE:
		RFL_ASSERT(decl->parent);
		RFL_ASSERT(decl->parent->decl_type == FYDT_ENUM);

		/* the enums are defined to be primitive types, and guaranteed to exist */
		ftt = packed_lookup_type_from_type_p_id(rfl, declp->type);
		RFL_ASSERT(ftt);
		type_kind = ftt->type_kind;

		decl->enum_value_decl.type_kind = type_kind;
		signess = fy_type_kind_signess(type_kind);
		RFL_ASSERT(signess != 0);
		if (signess > 0)
			decl->enum_value_decl.val.u = declp->enum_value.u;
		else
			decl->enum_value_decl.val.s = declp->enum_value.s;
		break;

	case FYDT_FIELD:
		RFL_ASSERT(decl->parent);
		RFL_ASSERT(decl->parent->decl_type == FYDT_STRUCT || decl->parent->decl_type == FYDT_UNION);
		decl->field_decl.byte_offset = 0;
		break;

	case FYDT_BITFIELD:
		RFL_ASSERT(decl->parent);
		RFL_ASSERT(decl->parent->decl_type == FYDT_STRUCT || decl->parent->decl_type == FYDT_UNION);
		decl->bitfield_decl.bit_offset = 0;
		decl->bitfield_decl.bit_width = declp->bit_width;
		break;

	case FYDT_FUNCTION:
		/* nothing */
		break;

	default:
		break;
	}

	/* fill in the linear comments */
	comment = fy_str_from_p(ti, declp->comment);
	if (comment) {
		len = asprintf(&declb->yaml_comment, "// yaml: %s", comment);
		RFL_ASSERT(len >= 0);

		decl->raw_comment = declb->yaml_comment;
	}

	return 0;

err_out:
	fprintf(stderr, "%s:%d %s error\n", __FILE__, __LINE__, __func__);
	packed_decl_cleanup(decl);
	return -1;
}

static void packed_decl_cleanup(struct fy_decl *decl)
{
	struct packed_decl_backend *declb;

	if (!decl || !decl->backend)
		return;

	declb = decl->backend;
	decl->backend = NULL;

	if (declb->yaml_comment)
		free(declb->yaml_comment);

	free(declb);
}

static int packed_type_setup(struct fy_type *ft, void *user)
{
	struct fy_reflection *rfl;
	struct packed_reflect_backend *rflb;
	const struct fy_packed_type_info *ti;
	const struct packed_type_user *ftu;
	struct packed_type_backend *ftb;
	const struct fy_type_p *typep;

	/* fake declaration */
	if (!user)
		return 0;

	assert(ft);
	rfl = ft->rfl;
	assert(rfl);

	rflb = rfl->backend;
	RFL_ASSERT(rflb);

	ti = rflb->type_info;
	ftu = user;

	ftb = malloc(sizeof(*ftb));
	RFL_ASSERT(ftb);
	memset(ftb, 0, sizeof(*ftb));

	typep = ftu->typep;
	RFL_ASSERT(typep);

	ftb->typep = typep;

	ft->backend = ftb;

	if (ft->type_kind == FYTK_CONSTARRAY)
		ft->element_count = typep->element_count;
	else
		ft->element_count = 1;

	if (fy_type_kind_is_primitive(typep->type_kind))
		ft->flags |= FYTF_SYNTHETIC;

	ft->flags |= (typep->flags & 0xff);

	if (fy_type_kind_is_dependent(ft->type_kind))
		ftb->dependent_typep = fy_type_p_from_id(ti, typep->dependent_type);

	return 0;

err_out:
	fprintf(stderr, "%s:%d %s error\n", __FILE__, __LINE__, __func__);
	packed_type_cleanup(ft);
	return -1;
}

static void packed_type_cleanup(struct fy_type *ft)
{
	struct packed_type_backend *ftb;

	if (!ft || !ft->backend)
		return;

	ftb = ft->backend;
	ft->backend = NULL;

	free(ftb);
}

/* generation */

static void fp_quoted_string(FILE *fp, const char *str)
{
	char c;
	const char *p;

	// XXX should be utf8 (use libfyaml)

	fprintf(fp, "\"");
	while ((c = *str++) != '\0') {
		if (c > 0x7e)
			continue;
		if (c < 0x20) {
			switch (c) {
			case '\r':
				p = "\\r";
				break;
			case '\n':
				p = "\\n";
				break;
			case '\t':
				p = "\\t";
				break;
			default:
				p = NULL;
				break;
			}
			if (p)
				fprintf(fp, "%s", p);
			continue;
		}
		fprintf(fp, "%c", c);
	}
	fprintf(fp, "\"");
}

static void decl_generate_one_fp(struct fy_packed_generator *pg, struct fy_decl *decl, FILE *fp)
{
	const char *yaml_comment;
	char *raw_comment;
	int signess;

	fprintf(fp, "\t[%d] = { .decl_type = %s, .name.str = \"%s\", .type.id = %d, ",
			decl->id,
			fy_decl_type_info_table[decl->decl_type].enum_name,
			decl->name,
			decl->type->id);

	switch (decl->decl_type) {
	case FYDT_FIELD:
		break;

	case FYDT_BITFIELD:
		fprintf(fp, ".bit_width = %zu", decl->bitfield_decl.bit_width);
		break;

	case FYDT_ENUM_VALUE:
		fprintf(fp, ".enum_value = ");
		signess = fy_type_kind_signess(decl->enum_value_decl.type_kind);
		if (signess > 0)
			fprintf(fp, "%ju", decl->enum_value_decl.val.u);
		else
			fprintf(fp, "%jd", decl->enum_value_decl.val.s);
		fprintf(fp, ",");
		break;
	default:
		break;
	}

	yaml_comment = fy_decl_get_yaml_comment(decl);
	if (yaml_comment) {
		fprintf(fp, ".comment.str = ");
		raw_comment = alloca(strlen("// yaml: ") + strlen(yaml_comment) + 1);
		strcpy(raw_comment, "// yaml: ");
		strcat(raw_comment, yaml_comment);
		fp_quoted_string(fp, raw_comment);
		fprintf(fp, ", ");
	}

	fprintf(fp, "},");

	fprintf(fp, "\n");
}

static void type_generate_one_fp(struct fy_packed_generator *pg, struct fy_type *ft, FILE *fp)
{
	struct fy_decl *decl;

	fprintf(fp, "\t[%d] = { .type_kind = %s, ",
			ft->id,
			fy_type_kind_info_get_internal(ft->type_kind)->enum_name);

	decl = fy_type_decl(ft);
	if (decl)
		fprintf(fp, ".decl.id = %d, ", decl->id);

	if (fy_type_kind_is_dependent(ft->type_kind))
		fprintf(fp, ".dependent_type.id = %d, ",
				ft->dependent_type ? ft->dependent_type->id : -1);

	if (ft->type_kind == FYTK_CONSTARRAY)
		fprintf(fp, ".element_count = %ju, ", ft->element_count);

	fprintf(fp, "},");
	fprintf(fp, "\n");
}

static int packed_generate_fp(struct fy_packed_generator *pg, FILE *fp)
{
	struct fy_reflection *rfl = pg->rfl;
	struct fy_decl *decl, *declc;
	struct fy_type *ft;
	const char *use_static, *decls_name, *types_name, *type_info_name;

	decls_name = pg->decls_name;
	if (!decls_name)
		decls_name = "decls";
	types_name = pg->types_name;
	if (!types_name)
		types_name = "types";
	type_info_name = pg->type_info_name;
	if (!type_info_name)
		type_info_name = "type_info";

	use_static = pg->use_static ? "static " : "";

	fprintf(fp, "%sconst struct fy_decl_p %s[] = {\n", use_static, decls_name);
	for (decl = fy_decl_list_head(&rfl->decls); decl != NULL; decl = fy_decl_next(&rfl->decls, decl)) {
		decl_generate_one_fp(pg, decl, fp);
		for (declc = fy_decl_list_head(&decl->children); declc != NULL; declc = fy_decl_next(&decl->children, declc))
			decl_generate_one_fp(pg, declc, fp);
	}
	fprintf(fp, "};\n");

	fprintf(fp, "%sconst struct fy_type_p %s[] = {\n", use_static, types_name);
	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {
		type_generate_one_fp(pg, ft, fp);
	}
	fprintf(fp, "};\n");

	fprintf(fp, "%sconst struct fy_packed_type_info %s = {\n"
		    "\t.types = %s,\n"
		    "\t.types_count = sizeof(%s)/sizeof(%s[0]),\n"
		    "\t.decls = %s,\n"
		    "\t.decls_count = sizeof(%s)/sizeof(%s[0]),\n",
		    use_static, type_info_name,
		    types_name, types_name, types_name,
		    decls_name, decls_name, decls_name);

	fprintf(fp, "};\n");

	return 0;
}

#define BLOB_ENDIAN	BET_BIG_ENDIAN

struct blob_writer {
	int Tc, Dc;				// type count, decl count
	enum blob_id_size Di, Ti, Si;		// id sizes
	br_wid_func Dwf, Twf, Swf;		// write methods for decl id, type id, string offset
	size_t Ts, Ds, Ss, Hs;			// sizes of areas in bytes
	struct blob_region Hr, Tr, Dr, Sr;	// header, type, decl, string regions
	uint64_t Si_maxval;			// in probing, maximum offset
};

static int decl_generate_one_blob(struct fy_decl *decl, struct blob_writer *bw, bool is_field)
{
	struct fy_reflection *rfl;
	const char *yaml_comment;
	size_t str_offset;
	bool is_anonymous, is_generated_name;
	uint8_t v8, flags;
	enum blob_id_size Vi;
	int signess;
	uintmax_t uv;
	intmax_t sv;

	assert(decl);
	rfl = decl->rfl;
	assert(rfl);

	is_anonymous = !!(decl->type->flags & (FYTF_ANONYMOUS | FYTF_ANONYMOUS_RECORD_DECL));
	is_generated_name = decl->decl_type == FYDT_PRIMITIVE;	/* ptr, const array etc */

	flags = 0;

	if (decl->decl_type == FYDT_ENUM_VALUE) {
		RFL_ASSERT(fy_type_kind_is_valid(decl->enum_value_decl.type_kind));
		signess = fy_type_kind_signess(decl->enum_value_decl.type_kind);
		RFL_ASSERT(signess != 0);
		if (signess > 0) {
			flags &= ~PGDF_ENUM_VALUE_SIGNED;
			uv = decl->enum_value_decl.val.u;
			if (uv <= UINT8_MAX)
				Vi = BID_U8;
			else if (uv <= UINT16_MAX)
				Vi = BID_U16;
			else if (uv <= UINT32_MAX)
				Vi = BID_U32;
			else
				Vi = BID_U64;
		} else {
			flags |= PGDF_ENUM_VALUE_SIGNED;
			sv = decl->enum_value_decl.val.s;
			if (sv >= INT8_MIN && sv <= INT8_MAX)
				Vi = BID_U8;
			else if (sv >= INT16_MIN && sv <= INT16_MAX)
				Vi = BID_U16;
			else if (sv >= INT32_MIN && sv <= INT32_MAX)
				Vi = BID_U32;
			else
				Vi = BID_U64;
		}
		flags |= (uint8_t)Vi << PGDF_ENUM_VALUE_SIZE_SHIFT;
	}

	v8 = ((uint8_t)decl->decl_type << PGDF_DECL_TYPE_SHIFT) | flags;
	br_w8(&bw->Dr, v8);

	bw->Twf(&bw->Dr, decl->type->id);
	if (!is_anonymous && !is_generated_name)
		str_offset = br_wstr(&bw->Sr, true, decl->name);
	else {
		if (decl->type) {
			if (decl->type->type_kind == FYTK_TYPEDEF) {
				fy_decl_dump(decl, 0, true);
				fy_type_dump(decl->type, true);
				fprintf(stderr, "decl->name=%s type=%s\n", decl->name, decl->type->fullname);
			}
			RFL_ASSERT(decl->type->type_kind != FYTK_TYPEDEF);
		}
		str_offset = 0;
	}

	bw->Swf(&bw->Dr, str_offset);
	if (str_offset > bw->Si_maxval)
		bw->Si_maxval = str_offset;
	switch (decl->decl_type) {
	case FYDT_BITFIELD:
		br_w8(&bw->Dr, (uint8_t)decl->bitfield_decl.bit_width);
		break;
	case FYDT_ENUM_VALUE:
		Vi = (flags & PGDF_ENUM_VALUE_SIZE_MASK) >> PGDF_ENUM_VALUE_SIZE_SHIFT;
		br_wX(&bw->Dr, Vi, decl->enum_value_decl.val.u);
		break;
	default:
		break;
	}
	yaml_comment = fy_decl_get_yaml_comment(decl);
	str_offset = yaml_comment ? br_wstr(&bw->Sr, true, yaml_comment) : 0;
	bw->Swf(&bw->Dr, str_offset);
	if (str_offset > bw->Si_maxval)
		bw->Si_maxval = str_offset;

	// fprintf(stderr, ">D#%d '%s':'%s' T#%d C=%s\n", decl->id,
	//		fy_decl_type_info_table[decl->decl_type].name, !is_anonymous ? decl->name : "",
	//		decl->type->id, yaml_comment ? yaml_comment : "");

	return 0;

err_out:
	return -1;
}

static int type_generate_one_blob(struct fy_type *ft, struct blob_writer *bw)
{
	struct fy_reflection *rfl;
	struct fy_decl *decl;
	enum blob_id_size Ci;
	uint8_t flags, v8;

	assert(ft);
	rfl = ft->rfl;
	assert(rfl);

	decl = fy_type_decl(ft);

	RFL_ASSERT(fy_type_kind_is_valid(ft->type_kind));

	flags = 0;
	v8 = ((uint8_t)ft->type_kind << PGTF_TYPE_KIND_SHIFT);

	if (fy_type_kind_has_element_count(ft->type_kind)) {
		Ci = blob_count_to_id_size(ft->element_count);
		RFL_ASSERT((unsigned int)Ci < (1 << PGTF_ELEM_SIZE_WIDTH));
		flags |= ((unsigned int)Ci << PGTF_ELEM_SIZE_SHIFT);
	}

	/* the low byte of the flags is stored */
	if (ft->flags & 0xff)
		flags |= PGTF_EXTFLAGS;

	v8 |= flags;
	br_w8(&bw->Tr, v8);

	if (flags & PGTF_EXTFLAGS)
		br_w8(&bw->Tr, (uint8_t)ft->flags);

	/* always have a decl */
	bw->Dwf(&bw->Tr, decl ? decl->id : 0);

	if (fy_type_kind_is_dependent(ft->type_kind)) {
		RFL_ASSERT(ft->dependent_type);
		bw->Twf(&bw->Tr, ft->dependent_type->id);
	}

	if (fy_type_kind_has_element_count(ft->type_kind)) {
		Ci = (flags >> PGTF_ELEM_SIZE_SHIFT) & ((1 << PGTF_ELEM_SIZE_WIDTH) - 1);
		br_wid(&bw->Tr, Ci, ft->element_count);
	}

	// fprintf(stderr, ">T#%d '%s'\n", ft->id, ft->fullname);

	return 0;

err_out:
	return -1;
}

static int packed_generate_blob_H(struct fy_packed_generator *pg, struct blob_writer *bw)
{
	br_w8(&bw->Hr, 'F');		/* format id */
	br_w8(&bw->Hr, 'Y');
	br_w8(&bw->Hr, 'P');
	br_w8(&bw->Hr, 'G');
	br_w8(&bw->Hr, 0);	/* major version */
	br_w8(&bw->Hr, 1);	/* minor version */
	br_w8(&bw->Hr, (uint8_t)bw->Ti);	/* type id size */
	br_w8(&bw->Hr, (uint8_t)bw->Di);	/* decl id size */
	br_w8(&bw->Hr, (uint8_t)bw->Si);	/* string table offset size */
	br_wskip_to(&bw->Hr, 0x10);
	br_w64(&bw->Hr, bw->Tc);	/* # of type entries */
	br_w64(&bw->Hr, bw->Ts);	/* # size of type entries area */
	br_w64(&bw->Hr, bw->Dc);	/* # of decl entries */
	br_w64(&bw->Hr, bw->Ds);	/* # size of decl entries area */
	br_w64(&bw->Hr, bw->Ss);	/* # size of string table area */

	/* skip to end */
	br_wskip_to(&bw->Hr, PGHDR_SIZE);

	return 0;
}

static int packed_generate_blob_TDS(struct fy_packed_generator *pg, struct blob_writer *bw)
{
	struct fy_reflection *rfl = pg->rfl;
	struct fy_decl *decl, *declc;
	struct fy_type *ft;
	int rc;

	/* always write a zero at offset zero of the strtab */
	/* this allows us to use offset zero as a NULL marker */
	br_w8(&bw->Sr, 0);

	for (decl = fy_decl_list_head(&rfl->decls); decl != NULL; decl = fy_decl_next(&rfl->decls, decl)) {

		/* do not generate primaries */
		if (fy_type_kind_is_primary(decl->type->type_kind))
			continue;

		rc = decl_generate_one_blob(decl, bw, false);
		RFL_ASSERT(!rc);

		for (declc = fy_decl_list_head(&decl->children); declc != NULL; declc = fy_decl_next(&decl->children, declc)) {
			rc = decl_generate_one_blob(declc, bw, true);
			RFL_ASSERT(!rc);
		}
	}

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {

		/* do not generate primaries */
		if (fy_type_kind_is_primary(ft->type_kind))
			continue;

		rc = type_generate_one_blob(ft, bw);
		RFL_ASSERT(!rc);
	}

	return 0;

err_out:
	return -1;
}

static int bw_reflection_probe(struct blob_writer *bw, struct fy_reflection *rfl)
{
	struct fy_decl *decl, *declc;
	struct fy_type *ft;
	int type_id_max, decl_id_max;
	int num_types, num_decls;

	decl_id_max = FY_DECL_ID_OFFSET;
	num_decls = 0;
	for (decl = fy_decl_list_head(&rfl->decls); decl != NULL; decl = fy_decl_next(&rfl->decls, decl)) {
		if (decl->id > decl_id_max)
			decl_id_max = decl->id;
		num_decls++;
		RFL_ASSERT(num_decls > 0);

		for (declc = fy_decl_list_head(&decl->children); declc != NULL; declc = fy_decl_next(&decl->children, declc)) {
			if (declc->id > decl_id_max)
				decl_id_max = declc->id;
			num_decls++;
			RFL_ASSERT(num_decls > 0);
		}
	}

	type_id_max = FY_TYPE_ID_OFFSET;
	num_types = 0;
	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {

		/* we do not include the primary types */
		if (fy_type_kind_is_primary(ft->type_kind))
			continue;

		/* must be so */
		RFL_ASSERT(ft->id >= FY_USER_DEFINED_ID_START);

		if (ft->id > type_id_max)
			type_id_max = ft->id;
		num_types++;
		RFL_ASSERT(num_types > 0);
	}

	bw->Tc = num_types;
	bw->Dc = num_decls;

	bw->Ti = blob_count_to_id_size(type_id_max + 1);
	bw->Di = blob_count_to_id_size(decl_id_max + 1);
	bw->Si = BID_U64;	// everything is maximum width at start
				//
	return 0;

err_out:
	return -1;
}

static int packed_generate_blob(struct fy_packed_generator *pg)
{
	struct fy_reflection *rfl = pg->rfl;
	struct blob_writer bw_local, *bw = &bw_local;
	uint8_t *blob = NULL, *new_blob;
	size_t blob_size = 0, new_blob_size;
	uint8_t *p;
	int rc, ret = -1;

	memset(bw, 0, sizeof(*bw));

	rc = bw_reflection_probe(bw, rfl);
	RFL_ASSERT(!rc);

	bw->Twf = br_wid_get_func(bw->Ti);
	bw->Dwf = br_wid_get_func(bw->Di);
	bw->Swf = br_wid_get_func(bw->Si);

	/* setup all for infinite non backed writes */
	br_wsetup(&bw->Tr, NULL, (size_t)-1, BLOB_ENDIAN);
	br_wsetup(&bw->Dr, NULL, (size_t)-1, BLOB_ENDIAN);
	br_wsetup(&bw->Sr, NULL, (size_t)-1, BLOB_ENDIAN);

	/* generate entries to get the worst case extents */
	ret = packed_generate_blob_TDS(pg, bw);
	RFL_ASSERT(!ret);

	/* get the maximum extents of the regions */
	bw->Ts = br_curr(&bw->Tr);
	bw->Ds = br_curr(&bw->Dr);
	bw->Ss = br_curr(&bw->Sr);
	bw->Hs = PGHDR_SIZE;

	/* worst case total size is header + type + decl + stringtab */
	blob_size = bw->Hs + bw->Ts + bw->Ds + bw->Ss;
	blob = malloc(blob_size);
	RFL_ASSERT(blob);

	/* get the worst case sizing of the string table */
	bw->Ss = br_curr(&bw->Sr);	/* note that this will shrunk due to text compression */

	/* generate string table again, to get the dedup effect */
	br_wsetup(&bw->Tr, NULL, (size_t)-1, BLOB_ENDIAN);
	br_wsetup(&bw->Dr, NULL, (size_t)-1, BLOB_ENDIAN);
	br_wsetup(&bw->Sr, blob, bw->Ss, BLOB_ENDIAN);

	ret = packed_generate_blob_TDS(pg, bw);
	RFL_ASSERT(!ret);

	/* get the actual string table size */
	bw->Ss = br_curr(&bw->Sr);

	/* update the string offset writer to the correct value */
	bw->Si = blob_count_to_id_size(bw->Si_maxval);
	bw->Swf = br_wid_get_func(bw->Si);

	/* generate again to size the type and decl regions */
	br_wsetup(&bw->Tr, NULL, (size_t)-1, BLOB_ENDIAN);
	br_wsetup(&bw->Dr, NULL, (size_t)-1, BLOB_ENDIAN);
	br_wsetup(&bw->Sr, NULL, (size_t)-1, BLOB_ENDIAN);

	ret = packed_generate_blob_TDS(pg, bw);
	RFL_ASSERT(!ret);

	/* final type and decl region sizes */
	bw->Ts = br_curr(&bw->Tr);
	bw->Ds = br_curr(&bw->Dr);

	/* update with the new blob_size */
	new_blob_size = bw->Hs + bw->Ts + bw->Ds + bw->Ss;
	RFL_ASSERT(new_blob_size <= blob_size);
	if (new_blob_size < blob_size) {
		new_blob = realloc(blob, new_blob_size);
		RFL_ASSERT(new_blob);
		blob = new_blob;
		blob_size = new_blob_size;
	}

	/* setup again, pointing at the actual buffer */
	p = blob;
	br_wsetup(&bw->Hr, p, bw->Hs, BLOB_ENDIAN);
	p += bw->Hs;

	br_wsetup(&bw->Tr, p, bw->Ts, BLOB_ENDIAN);
	p += bw->Ts;

	br_wsetup(&bw->Dr, p, bw->Ds, BLOB_ENDIAN);
	p += bw->Ds;

	br_wsetup(&bw->Sr, p, bw->Ss, BLOB_ENDIAN);
	p += bw->Ss;

	/* verify that everything lines up now */
	RFL_ASSERT((size_t)(p - blob) == blob_size);

	/* and generate everything for real */
	ret = packed_generate_blob_H(pg, bw);
	RFL_ASSERT(!ret);

	ret = packed_generate_blob_TDS(pg, bw);
	RFL_ASSERT(!ret);

	/* again verify in case of an error */
	RFL_ASSERT(bw->Hs == br_curr(&bw->Hr));
	RFL_ASSERT(bw->Ts == br_curr(&bw->Tr));
	RFL_ASSERT(bw->Ds == br_curr(&bw->Dr));
	RFL_ASSERT(bw->Ss == br_curr(&bw->Sr));

	*pg->blobp = blob;
	*pg->blob_sizep = blob_size;

	return 0;
err_out:
	if (blob)
		free(blob);
	return -1;
}

int fy_packed_generate(struct fy_packed_generator *pg)
{
	struct fy_reflection *rfl;
	FILE *fp = NULL;
	int ret;

	if (!pg)
		return -1;

	rfl = pg->rfl;
	if (!rfl)
		return -1;

	/* the generation relies on the renumbering settings ids correctly */
	fy_reflection_renumber(rfl);

	ret = -1;
	switch (pg->type) {
	case FYPGT_TO_FILE:
		ret = packed_generate_fp(pg, pg->fp);
		break;

	case FYPGT_TO_STRING:
		fp = open_memstream(pg->strp, pg->str_sizep);
		if (!fp)
			break;
		ret = packed_generate_fp(pg, fp);
		fclose(fp);
		fp = NULL;
		break;

	case FYPGT_BLOB:

		if (!pg->blobp || !pg->blob_sizep)
			return -1;

		ret = packed_generate_blob(pg);
		break;

	default:
		ret = -1;
		break;
	}

	return ret;
}

static int packed_reflection_setup_blob(struct fy_reflection *rfl)
{
	const struct fy_packed_backend_reflection_cfg *cfg = rfl->cfg.backend_cfg;
	struct packed_reflect_backend *rflb;
	struct blob_region Br;
	int i, Tc, Dc;				// type count, decl count
	enum blob_id_size Di, Ti, Si, Ci, Vi; 	// id sizes
	br_rid_func Trf, Drf, Srf;
	size_t Ts, Ds, Ss;
	uint8_t major, minor;
	struct fy_type_p *typep;
	struct fy_decl_p *declp;
	uint8_t v8, flags;
	bool valid_hdr;

	rflb = rfl->backend;

	rflb->blob_size = cfg->blob_size;

	if (!cfg->copy)
		rflb->blob = cfg->blob;
	else {
		rflb->blob_copy = malloc(rflb->blob_size);
		RFL_ASSERT(rflb->blob_copy);
		memcpy(rflb->blob_copy, cfg->blob, rflb->blob_size);
		rflb->blob = rflb->blob_copy;
	}

	br_rsetup(&Br, rflb->blob, rflb->blob_size, BLOB_ENDIAN);

	/* FYPG */
	valid_hdr = br_r8(&Br) == 'F' && br_r8(&Br) == 'Y' && br_r8(&Br) == 'P' && br_r8(&Br) == 'G';
	RFL_ERROR_CHECK(valid_hdr,
			"packed: Illegal blob signature (not FYPG)");

	/* major, minor version check */
	major = br_r8(&Br);
	minor = br_r8(&Br);
	RFL_ERROR_CHECK(major > 0,
			"packed: Illegal blob signature");
	(void)minor;

	Ti = br_r8(&Br);
	Di = br_r8(&Br);
	Si = br_r8(&Br);
	br_rskip_to(&Br, 0x10);
	Tc = br_r64(&Br);
	Ts = br_r64(&Br);
	Dc = br_r64(&Br);
	Ds = br_r64(&Br);
	Ss = br_r64(&Br);

	br_rskip_to(&Br, PGHDR_SIZE);

	// fprintf(stderr, "<Tc=%d Dc=%d \n", Tc, Dc);

	/* get accessors */
	Trf = br_rid_get_func(Ti);
	Drf = br_rid_get_func(Di);
	Srf = br_rid_get_func(Si);

	/* allocate expanded structures */
	rflb->gen_types = malloc(sizeof(*rflb->gen_types) * Tc);
	RFL_ASSERT(rflb->gen_types);
	memset(rflb->gen_types, 0, sizeof(*rflb->gen_types) * Tc);

	rflb->gen_decls = malloc(sizeof(*rflb->gen_decls) * Dc);
	RFL_ASSERT(rflb->gen_decls);
	memset(rflb->gen_decls, 0, sizeof(*rflb->gen_decls) * Dc);

	for (typep = rflb->gen_types, i = 0; i < Tc; i++, typep++) {

		// fprintf(stderr, "<T#[%d]\n", i);

		v8 = br_r8(&Br);
		flags = v8 & ~PGTF_TYPE_KIND_MASK;

		typep->type_kind = (v8 & PGTF_TYPE_KIND_MASK) >> PGTF_TYPE_KIND_SHIFT;
		RFL_ASSERT(fy_type_kind_is_valid(typep->type_kind));

		if (v8 & PGTF_EXTFLAGS) {
			v8 = br_r8(&Br);
			typep->flags = v8;
		} else
			typep->flags = 0;

		/* always have a decl */
		typep->decl.id = Drf(&Br);

		if (fy_type_kind_is_dependent(typep->type_kind))
			typep->dependent_type.id = Trf(&Br);
		if (fy_type_kind_has_element_count(typep->type_kind)) {
			Ci = (flags >> PGTF_ELEM_SIZE_SHIFT) & ((1 << PGTF_ELEM_SIZE_WIDTH) - 1);
			typep->element_count = br_rid(&Br, Ci);
		}
	}

	for (declp = rflb->gen_decls, i = 0; i < Dc; i++, declp++) {

		// fprintf(stderr, "<D#[%d]\n", i);

		v8 = br_r8(&Br);
		flags = v8 & ~PGDF_DECL_TYPE_MASK;

		declp->decl_type = (v8 & PGDF_DECL_TYPE_MASK) >> PGDF_DECL_TYPE_SHIFT;

		RFL_ASSERT(fy_decl_type_is_valid(declp->decl_type));

		/* for non enum values, get the qualifiers */
		if (declp->decl_type != FYDT_ENUM_VALUE)
			declp->flags = 0;	/* XXX */

		declp->type.id = Trf(&Br);
		declp->name.offset = Srf(&Br);
		switch (declp->decl_type) {
		case FYDT_BITFIELD:
			declp->bit_width = br_r8(&Br);
			break;
		case FYDT_ENUM_VALUE:
			Vi = (flags & PGDF_ENUM_VALUE_SIZE_MASK) >> PGDF_ENUM_VALUE_SIZE_SHIFT;
			declp->enum_value.u = br_rX(&Br, Vi);
			if (flags & PGDF_ENUM_VALUE_SIGNED) {
				/* need to propagate the sign */
				switch (Vi) {
				case BID_U8:
					if (declp->enum_value.u & (1 << (8 - 1)))
						declp->enum_value.u |= ((uint64_t)-1) << 8;
					break;
				case BID_U16:
					if (declp->enum_value.u & (1 << (16 - 1)))
						declp->enum_value.u |= ((uint64_t)-1) << 16;
					break;
				case BID_U32:
					if (declp->enum_value.u & (1 << (32 - 1)))
						declp->enum_value.u |= ((uint64_t)-1) << 32;
					break;
				default:
					break;
				}
			}
			break;
		default:
			break;
		}
		declp->comment.offset = Srf(&Br);
	}

	/* it must be consumed completely */
	RFL_ASSERT(br_curr(&Br) == PGHDR_SIZE + Ts + Ds);

	rflb->gen_type_info.uses_pointers = false;
	rflb->gen_type_info.types = rflb->gen_types;
	rflb->gen_type_info.types_count = Tc;
	rflb->gen_type_info.decls = rflb->gen_decls;
	rflb->gen_type_info.decls_count = Dc;
	rflb->gen_type_info.strtab = (const char *)(rflb->blob + PGHDR_SIZE + Ts + Ds);
	rflb->gen_type_info.strtab_size = Ss;

	rflb->type_info = &rflb->gen_type_info;

	return 0;
err_out:
	if (rflb->gen_types) {
		free(rflb->gen_types);
		rflb->gen_types = NULL;
	}
	if (rflb->gen_decls) {
		free(rflb->gen_decls);
		rflb->gen_decls = NULL;
	}
	return -1;
}
