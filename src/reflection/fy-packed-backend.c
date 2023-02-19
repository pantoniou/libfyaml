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
#include <alloca.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <float.h>
#include <assert.h>
#include <limits.h>
#include <ctype.h>

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
};

struct packed_import_backend {
	char *name;
};

struct packed_decl_user {
	const struct fy_decl_p *declp;
	const struct fy_decl_p *decl_parentp;
};

struct packed_decl_backend {
	const struct fy_decl_p *declp;
	const struct fy_type_p *typep;
};

struct packed_type_user {
	const struct fy_type_p *typep;
};

struct packed_type_backend {
	const struct fy_type_p *typep;
	const struct fy_type_p *dependent_typep;
};

static struct fy_type *
packed_lookup_type_by_typep(struct fy_reflection *rfl, const struct fy_type_p *fytp)
{
	struct fy_type *ft;
	struct packed_type_backend *ftb;

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {
		ftb = ft->backend;
		if (fytp == ftb->typep)
			return ft;
	}
	return NULL;
}

static char *
packed_type_generate_name(struct fy_reflection *rfl, const struct fy_type_p *fytp)
{
	struct packed_reflect_backend *rflb = rfl->backend;
	const struct fy_packed_type_info *ti = rflb->type_info;
	const struct fy_decl_p *declp;
	enum fy_type_kind tk, dtk;
	const char *basename, *declname, *pfx;
	const struct fy_type_p *fytdp = NULL;
	char *depname = NULL, *depname_ext = NULL;
	const char *sep, *s;
	FILE *fp;
	char *buf;
	size_t len;
	int ret;
	bool error = false;

	tk = fytp->type_kind;
	basename = fy_type_kind_info_get_internal(tk)->name;

	buf = NULL;
	len = 0;
	fp = open_memstream(&buf, &len);
	if (!fp)
		return NULL;

	if (fytp->flags & FYTPF_CONST) {
		ret = fprintf(fp, "const ");
		if (ret < 0)
			goto err_out;
	}

	if (fytp->flags & FYTPF_VOLATILE) {
		ret = fprintf(fp, "volatile ");
		if (ret < 0)
			goto err_out;
	}

	if (fy_type_kind_is_primitive(tk) || tk == FYTK_INVALID) {
		ret = fprintf(fp, "%s", basename);
		if (ret < 0)
			goto err_out;
		goto out;
	}

	declp = fy_decl_p_from_id(ti, fytp->decl);
	if (!declp)
		goto err_out;

	declname = fy_decl_p_name(ti, declp);
	if (!declname)
		goto err_out;

	if (fy_type_kind_is_record(tk) || tk == FYTK_ENUM || tk == FYTK_TYPEDEF || tk == FYTK_FUNCTION) {
		ret = fprintf(fp, "%s", declname);
		if (ret < 0)
			goto err_out;
		goto out;
	}

	dtk = FYTK_INVALID;
	if (fy_type_kind_is_dependent(tk)) {
		fytdp = fy_type_p_from_id(ti, fytp->dependent_type);
		if (!fytdp)
			goto err_out;
		dtk = fytdp->type_kind;
		depname = packed_type_generate_name(rfl, fytdp);
		if (!depname)
			goto err_out;

		if (dtk == FYTK_STRUCT || dtk == FYTK_UNION) {
			if (dtk == FYTK_STRUCT)
				pfx = "struct";
			else
				pfx = "union";
			ret = asprintf(&depname_ext, "%s %s", pfx, depname);
			if (ret < 0)
				goto err_out;
			free(depname);
			depname = depname_ext;
		}
	}

	buf = NULL;
	switch (tk) {

	case FYTK_PTR:
		if (dtk != FYTK_FUNCTION) {
			sep = fy_type_kind_is_like_ptr(dtk) ? "" : " ";
			ret = fprintf(fp, "%s%s*", depname, sep);
		} else {
			/* function names are int (int, char) like */

			s = strchr(depname, '(');
			if (s)
				ret = fprintf(fp, "%.*s(*)%s", (int)(s - depname), depname, s);
			else
				ret = fprintf(fp, "%s *", depname);
		}
		if (fytp->flags & FYTPF_RESTRICT) {
			ret = fprintf(fp, "restrict");
			if (ret < 0)
				goto err_out;
		}
		break;

	case FYTK_INCOMPLETEARRAY:
		sep = fy_type_kind_is_like_ptr(dtk) ? "" : " ";
		ret = fprintf(fp, "%s%s[]", depname, sep);
		break;

	case FYTK_CONSTARRAY:
		sep = fy_type_kind_is_like_ptr(dtk) ? "" : " ";
		ret = fprintf(fp, "%s%s[%llu]", depname, sep, fytp->element_count);
		break;

	case FYTK_FUNCTION:
		ret = fprintf(fp, "%s()", depname);
		break;
	default:
		abort();
		break;
	}

	if (ret < 0)
		goto err_out;

out:
	if (depname)
		free(depname);

	fclose(fp);

	if (error) {
		free(buf);
		buf = NULL;
	}

	return buf;

err_out:
	error = true;
	goto out;
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
	if (!rflb)
		goto err_out;
	memset(rflb, 0, sizeof(*rflb));

	rfl->backend = rflb;

	switch (cfg->type) {
	case FYPRT_TYPE_INFO:
		rflb->type_info = cfg->type_info;
		break;

	case FYPRT_BLOB:
		ret = packed_reflection_setup_blob(rfl);
		if (ret)
			goto err_out;
		break;
	}

	return 0;

err_out:
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
	struct packed_type_user fttu_local, *fttu = &fttu_local;
	const struct fy_packed_type_info *ti = rflb->type_info;
	const struct fy_type_p *fytp, *fyt_parentp;
	const struct fy_decl_p *declp, *decl_parentp, *declpe;
	struct packed_decl_user declu_local, *declu = &declu_local;
	struct fy_type *ft;
	struct fy_decl *decl, *decl_parent;
	char *type_name;

	(void)rflb;

	/* now do the decls */
	declp = ti->decls;
	declpe = declp + ti->decls_count;
	while (declp < declpe) {

		assert(!fy_decl_type_has_parent(declp->decl_type));

		fytp = fy_type_p_from_id(ti, declp->type);

		memset(declu, 0, sizeof(*declu));
		declu->declp = declp;
		declu->decl_parentp = NULL;

		decl = fy_decl_create(rfl, imp, NULL, declp->decl_type, fy_decl_p_name(ti, declp), declu);
		assert(decl);
		if (!decl)
			goto err_out;

		decl->type = packed_lookup_type_by_typep(rfl, fytp);
		fy_decl_list_add_tail(&rfl->decls, decl);

		if (!fy_decl_type_has_children(declp->decl_type)) {

			if (!decl->type) {
				memset(fttu, 0, sizeof(*fttu));
				fttu->typep = fytp;

				type_name = packed_type_generate_name(rfl, fytp);
				ft = fy_type_create(rfl, fytp->type_kind, type_name, decl, fttu);
				free(type_name);
				assert(ft != NULL);

				fy_type_list_add_tail(&rfl->types, ft);

				decl->type = ft;
			}
			declp++;
			continue;
		}

		decl_parent = decl;
		decl_parentp = declp;
		fyt_parentp = fytp;
		declp++;

		while (declp < declpe && fy_decl_type_has_parent(declp->decl_type)) {

			fytp = fy_type_p_from_id(ti, declp->type);

			memset(declu, 0, sizeof(*declu));
			declu->declp = declp;
			declu->decl_parentp = decl_parentp;

			assert(decl_parent);
			decl = fy_decl_create(rfl, imp, decl_parent, declp->decl_type, fy_decl_p_name(ti, declp), declu);
			assert(decl);
			if (!decl)
				goto err_out;

			decl->type = packed_lookup_type_by_typep(rfl, fytp);
			if (!decl->type) {
				memset(fttu, 0, sizeof(*fttu));
				fttu->typep = fytp;

				type_name = packed_type_generate_name(rfl, fytp);
				ft = fy_type_create(rfl, fytp->type_kind, type_name, decl, fttu);
				free(type_name);
				assert(ft != NULL);

				fy_type_list_add_tail(&rfl->types, ft);

				decl->type = ft;
			}

			fy_decl_list_add_tail(&decl_parent->children, decl);

			declp++;
		}
		if (!decl_parent->type) {

			memset(fttu, 0, sizeof(*fttu));
			fttu->typep = fyt_parentp;

			type_name = packed_type_generate_name(rfl, fyt_parentp);
			ft = fy_type_create(rfl, fyt_parentp->type_kind, type_name, decl_parent, fttu);
			free(type_name);
			assert(ft != NULL);

			fy_type_list_add_tail(&rfl->types, ft);

			decl_parent->type = ft;
		}
	}

	/* and fixup type size, aligns */
	fy_reflection_fixup_size_align(rfl);

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
	if (!impb)
		goto err_out;
	memset(impb, 0, sizeof(*impb));

	imp->backend = impb;

	assert(rflb);

	len = snprintf(NULL, 0, "packed@%p", rflb->type_info);
	if (len < 0)
		goto err_out;

	impb->name = malloc(len + 1);
	if (!impb->name)
		goto err_out;

	snprintf(impb->name, len + 1, "packed@%p", rflb->type_info);

	/* forward */
	imp->name = impb->name;

	ret = packed_do_import(imp);
	if (ret)
		goto err_out;

	return 0;

err_out:
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
	struct fy_reflection *rfl = decl->imp->rfl;
	struct packed_reflect_backend *rflb = rfl->backend;
	const struct fy_packed_type_info *ti = rflb->type_info;
	const struct packed_decl_user *declu = user;
	struct packed_decl_backend *declb;
	const struct fy_decl_p *declp;
	const struct fy_type_p *typep;
	const char *name;
	int signess;

	rfl = decl->imp->rfl;

	declb = malloc(sizeof(*declb));
	if (!declb)
		goto err_out;
	memset(declb, 0, sizeof(*declb));

	decl->backend = declb;

	declp = declu->declp;
	declb->declp = declp;

	typep = fy_type_p_from_id(ti, declp->type);

	name = fy_str_from_p(ti, declp->name);

	decl->anonymous = !name || !name[0];
	decl->in_system_header = false;
	decl->from_main_file = true;

	switch (decl->decl_type) {
	case FYDT_ENUM:
		assert(typep);
		break;

	case FYDT_ENUM_VALUE:
		assert(decl->parent);
		assert(decl->parent->decl_type == FYDT_ENUM);

		decl->enum_value_decl.type_kind = typep->type_kind;
		signess = fy_type_kind_signess(decl->enum_value_decl.type_kind);
		assert(signess != 0);
		if (signess > 0)
			decl->enum_value_decl.val.u = declp->enum_value.u;
		else
			decl->enum_value_decl.val.s = declp->enum_value.s;
		break;

	case FYDT_FIELD:
		assert(decl->parent);
		assert(decl->parent->decl_type == FYDT_STRUCT || decl->parent->decl_type == FYDT_UNION);
		decl->field_decl.byte_offset = 0;
		break;

	case FYDT_BITFIELD:
		assert(decl->parent);
		assert(decl->parent->decl_type == FYDT_STRUCT || decl->parent->decl_type == FYDT_UNION);
		decl->bitfield_decl.bit_offset = 0;
		decl->bitfield_decl.bit_width = declp->bit_width;
		break;

	default:
		break;
	}

	/* fill in the linear comments */
	decl->raw_comment = fy_str_from_p(ti, declp->comment);

	return 0;

err_out:
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

	free(declb);
}

static int packed_type_setup(struct fy_type *ft, void *user)
{
	struct fy_reflection *rfl = ft->rfl;
	struct packed_reflect_backend *rflb = rfl->backend;
	const struct fy_packed_type_info *ti = rflb->type_info;
	const struct packed_type_user *ftu = user;
	struct packed_type_backend *ftb;
	const struct fy_type_p *typep;
	struct fy_type *ft2;
	struct packed_type_backend *ft2b;
	enum fy_type_kind ttype_kind;
	struct fy_type *ftt;
	struct packed_type_user fttu_local, *fttu = &fttu_local;
	char *dependent_type_name;
	int ret = 0;

	ftb = malloc(sizeof(*ftb));
	if (!ftb)
		goto err_out;
	memset(ftb, 0, sizeof(*ftb));

	typep = ftu->typep;
	assert(typep);

	ftb->typep = typep;

	ft->backend = ftb;

	/* primitive types need not define size/align */
	if (fy_type_kind_is_primitive(ft->type_kind) || ft->type_kind == FYTK_FUNCTION) {
		ft->size = fy_type_kind_info_get_internal(ft->type_kind)->size;
		ft->align = fy_type_kind_info_get_internal(ft->type_kind)->align;
	} else {
		ft->size = 0;	/* must be filled later */
		ft->align = 0;
	}

	if (ft->type_kind == FYTK_CONSTARRAY)
		ft->element_count = typep->element_count;
	else
		ft->element_count = 1;

	/* finally update the qualifiers */
	ft->is_const = !!(typep->flags & FYTPF_CONST);
	ft->is_volatile = !!(typep->flags & FYTPF_VOLATILE);
	ft->is_restrict = !!(typep->flags & FYTPF_RESTRICT);

	if (fy_type_kind_is_dependent(ft->type_kind)) {
		ftb->dependent_typep = fy_type_p_from_id(ti, typep->dependent_type);
		assert(ftb->dependent_typep);
		ttype_kind = ftb->dependent_typep->type_kind;
		ftt = packed_lookup_type_by_typep(rfl, ftb->dependent_typep);
		if (!ftt && (ttype_kind == FYTK_INVALID || ttype_kind == FYTK_FUNCTION ||
			     fy_type_kind_is_primitive(ttype_kind) || fy_type_kind_is_like_ptr(ttype_kind))) {
			memset(fttu, 0, sizeof(*fttu));
			fttu->typep = ftb->dependent_typep;

			dependent_type_name = packed_type_generate_name(rfl, ftb->dependent_typep);
			ftt = fy_type_create(rfl, ttype_kind, dependent_type_name, NULL, fttu);
			free(dependent_type_name);
			assert(ftt != NULL);
			if (!ftt)
				goto err_out;

			fy_type_list_add_tail(&rfl->types, ftt);
		}

		if (ftt) {
			ft->dependent_type = ftt;
			ft->unresolved = false;
		} else {
			ft->unresolved = true;
			rfl->unresolved_types_count++;
		}
	}

	/* look for unresolves that match */
	for (ft2 = fy_type_list_head(&rfl->types);
	     rfl->unresolved_types_count > 0 && ft2 != NULL;
	     ft2 = fy_type_next(&rfl->types, ft2)) {

		if (!ft2->unresolved)
			continue;

		ft2b = ft2->backend;

		if (ft2b->dependent_typep != ftb->typep)
			continue;

		ft2->unresolved = false;
		ft2->was_fwd_declared = true;
		ft2->dependent_type = ft;

		assert(rfl->unresolved_types_count > 0);
		rfl->unresolved_types_count--;
	}
out:
	return ret;

err_out:
	packed_type_cleanup(ft);
	ret = -1;
	goto out;
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
		assert(signess != 0);
		if (signess > 0)
			fprintf(fp, "%llu", decl->enum_value_decl.val.u);
		else
			fprintf(fp, "%lld", decl->enum_value_decl.val.s);
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
	static const char *qual_map[8] = {
		"",						/* RVC = 000 */
		"FYTPF_CONST",					/* RVC = 001 */
		"FYTPF_VOLATILE",				/* RVC = 010 */
		"FYTPF_VOLATILE | FYTPF_CONST",			/* RVC = 011 */
		"FYTPF_RESTRICT",				/* RVC = 100 */
		"FYTPF_RESTRICT | FYTPF_CONST",			/* RVC = 101 */
		"FYTPF_RESTRICT | FYTPF_VOLATILE",		/* RVC = 110 */
		"FYTPF_RESTRICT | FYTPF_VOLATILE | FYTPF_CONST",/* RVC = 111 */
	};
	int qual_id;

	fprintf(fp, "\t[%d] = { .type_kind = %s, ",
			ft->id,
			fy_type_kind_info_get_internal(ft->type_kind)->enum_name);

	if (ft->decl)
		fprintf(fp, ".decl.id = %d, ", ft->decl->id);

	if (fy_type_kind_is_dependent(ft->type_kind))
		fprintf(fp, ".dependent_type.id = %d, ",
				ft->dependent_type ? ft->dependent_type->id : -1);

	if (ft->type_kind == FYTK_CONSTARRAY)
		fprintf(fp, ".element_count = %llu, ", ft->element_count);

	if (ft->is_const || ft->is_volatile || ft->is_restrict) {
		qual_id = ((int)!!ft->is_restrict << 2) |
			  ((int)!!ft->is_volatile << 1) |
			   (int)!!ft->is_const;

		fprintf(fp, ".flags = %s, ",qual_map[qual_id]);
	}

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
	enum blob_id_size Di, Ti, Si, Ci, Vi;	// id sizes
	br_wid_func Dwf, Twf, Swf, Cwf, Vwf;	// write methods
	br_rid_func Drf, Trf, Srf, Crf, Vrf;	// write methods
	size_t Ts, Ds, Ss, Hs;			// sizes of areas in bytes
	struct blob_region Hr, Tr, Dr, Sr;	// header, type, decl, string regions
};

static void decl_generate_one_blob(struct fy_packed_generator *pg, struct fy_decl *decl, struct blob_writer *bw)
{
	const char *yaml_comment;
	char *raw_comment;

	br_w8(&bw->Dr, (uint8_t)decl->decl_type);
	br_wid(&bw->Dr, bw->Ti, decl->type->id);
	br_wid(&bw->Dr, bw->Si, br_wstr(&bw->Sr, decl->name, (size_t)-1));
	switch (decl->decl_type) {
	case FYDT_BITFIELD:
		br_w8(&bw->Dr, (uint8_t)decl->bitfield_decl.bit_width);
		break;
	case FYDT_ENUM_VALUE:
		br_wX(&bw->Dr, bw->Vi, decl->enum_value_decl.val.u);
		break;
	default:
		break;
	}
	yaml_comment = fy_decl_get_yaml_comment(decl);
	if (yaml_comment) {
		raw_comment = alloca(strlen("// yaml: ") + strlen(yaml_comment) + 1);
		strcpy(raw_comment, "// yaml: ");
		strcat(raw_comment, yaml_comment);
		br_wid(&bw->Dr, bw->Si, br_wstr(&bw->Sr, raw_comment, (size_t)-1));
	} else
		br_wid(&bw->Dr, bw->Si, 0);
}

static void type_generate_one_blob(struct fy_packed_generator *pg, struct fy_type *ft, struct blob_writer *bw)
{
	uint8_t flags;

	br_w8(&bw->Tr, (uint8_t)ft->type_kind);
	flags = 0;
	if (ft->is_const)
		flags |= PGTF_CONST;
	if (ft->is_volatile)
		flags |= PGTF_VOLATILE;
	if (ft->is_restrict)
		flags |= PGTF_RESTRICT;
	if (!fy_type_kind_is_primitive(ft->type_kind) && ft->decl)
		flags |= PGTF_DECL;
	if (fy_type_kind_is_dependent(ft->type_kind) && ft->dependent_type)
		flags |= PGTF_DEP;
	if (ft->type_kind == FYTK_CONSTARRAY)
		flags |= PGTF_ECOUNT;
	br_w8(&bw->Tr, flags);

	if (flags & PGTF_DECL)
		br_wid(&bw->Tr, bw->Di, ft->decl->id);

	if (flags & PGTF_DEP)
		br_wid(&bw->Tr, bw->Ti, ft->dependent_type->id);

	if (flags & PGTF_ECOUNT)
		br_wid(&bw->Tr, bw->Ci, ft->element_count);
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
	br_w8(&bw->Hr, (uint8_t)bw->Ci);	/* element count size */
	br_w8(&bw->Hr, (uint8_t)bw->Vi);	/* enum value size */
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

	/* always write a zero at offset zero of the strtab */
	/* this allows us to use offset zero as a NULL marker */
	br_w8(&bw->Sr, 0);

	for (decl = fy_decl_list_head(&rfl->decls); decl != NULL; decl = fy_decl_next(&rfl->decls, decl)) {
		decl_generate_one_blob(pg, decl, bw);
		for (declc = fy_decl_list_head(&decl->children); declc != NULL; declc = fy_decl_next(&decl->children, declc))
			decl_generate_one_blob(pg, declc, bw);
	}

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft))
		type_generate_one_blob(pg, ft, bw);

	return 0;
}

static int packed_generate_blob(struct fy_packed_generator *pg)
{
	struct fy_reflection *rfl = pg->rfl;
	struct blob_writer bw_local, *bw = &bw_local;
	void *blob = NULL;
	size_t blob_size = 0;
	uint8_t *p;
	int ret;

	memset(bw, 0, sizeof(*bw));

	bw->Tc = rfl->next_type_id;
	bw->Dc = rfl->next_decl_id;

	bw->Ti = blob_count_to_id_size(bw->Tc);
	bw->Di = blob_count_to_id_size(bw->Dc);
	bw->Si = BID_U32;	// hardcoded to 4 bytes for now
	bw->Ci = BID_U64;	// hardcoded to 8 bytes for now
	bw->Vi = BID_U64;	// hardcoded to 8 bytes for now

	bw->Twf = br_wid_get_func(bw->Ti);
	bw->Trf = br_rid_get_func(bw->Ti);
	bw->Dwf = br_wid_get_func(bw->Di);
	bw->Drf = br_rid_get_func(bw->Di);
	bw->Swf = br_wid_get_func(bw->Si);
	bw->Srf = br_rid_get_func(bw->Si);
	bw->Cwf = br_wid_get_func(bw->Ci);
	bw->Crf = br_rid_get_func(bw->Ci);
	bw->Vwf = br_wid_get_func(bw->Vi);
	bw->Vrf = br_rid_get_func(bw->Vi);

	/* setup all for infinite non backed writes */
	br_wsetup(&bw->Tr, NULL, (size_t)-1, BLOB_ENDIAN);
	br_wsetup(&bw->Dr, NULL, (size_t)-1, BLOB_ENDIAN);
	br_wsetup(&bw->Sr, NULL, (size_t)-1, BLOB_ENDIAN);

	/* generate entries to get the extends */
	ret = packed_generate_blob_TDS(pg, bw);
	if (ret)
		goto err_out;

	bw->Ts = br_curr(&bw->Tr);
	bw->Ds = br_curr(&bw->Dr);
	bw->Ss = br_curr(&bw->Sr);
	bw->Hs = PGHDR_SIZE;

	/* total size is header + type + decl + stringtab */
	blob_size = bw->Hs + bw->Ts + bw->Ds + bw->Ss;
	blob = malloc(blob_size);
	if (!blob)
		goto err_out;

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

	/* verify that everything was setup correctly */
	assert(p == blob + blob_size);

	ret = packed_generate_blob_H(pg, bw);
	if (ret)
		goto err_out;

	ret = packed_generate_blob_TDS(pg, bw);
	if (ret)
		goto err_out;

	*pg->blobp = blob;
	*pg->blob_sizep = blob_size;

	return 0;
err_out:
	if (blob)
		free(blob);
	return ret;
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
	int i, Tc, Dc;		// type count, decl count
	enum blob_id_size Di, Ti, Si, Ci, Vi;	// id sizes
	size_t Ts, Ds, Ss;
	uint8_t major, minor;
	struct fy_type_p *typep;
	struct fy_decl_p *declp;
	uint8_t flags;

	rflb = rfl->backend;

	rflb->blob_size = cfg->blob_size;

	if (!cfg->copy)
		rflb->blob = cfg->blob;
	else {
		rflb->blob_copy = malloc(rflb->blob_size);
		if (!rflb->blob_copy)
			return -1;
		memcpy(rflb->blob_copy, cfg->blob, rflb->blob_size);
		rflb->blob = rflb->blob_copy;
	}

	br_rsetup(&Br, rflb->blob, rflb->blob_size, BLOB_ENDIAN);
	/* FYPG */
	if (br_r8(&Br) != 'F' || br_r8(&Br) != 'Y' || br_r8(&Br) != 'P' || br_r8(&Br) != 'G')
		goto err_out;

	/* major, minor version check */
	major = br_r8(&Br);
	minor = br_r8(&Br);
	if (major > 0)
		goto err_out;
	(void)minor;

	Ti = br_r8(&Br);
	Di = br_r8(&Br);
	Si = br_r8(&Br);
	Ci = br_r8(&Br);
	Vi = br_r8(&Br);
	br_rskip_to(&Br, 0x10);
	Tc = br_r64(&Br);
	Ts = br_r64(&Br);
	Dc = br_r64(&Br);
	Ds = br_r64(&Br);
	Ss = br_r64(&Br);

	br_rskip_to(&Br, PGHDR_SIZE);

	/* allocate expanded structures */
	rflb->gen_types = malloc(sizeof(*rflb->gen_types) * Tc);
	if (!rflb->gen_types)
		goto err_out;
	memset(rflb->gen_types, 0, sizeof(*rflb->gen_types) * Tc);
	rflb->gen_decls = malloc(sizeof(*rflb->gen_decls) * Dc);
	if (!rflb->gen_decls)
		goto err_out;
	memset(rflb->gen_decls, 0, sizeof(*rflb->gen_decls) * Dc);

	for (typep = rflb->gen_types, i = 0; i < Tc; i++, typep++) {
		typep->type_kind = br_r8(&Br);
		assert(fy_type_kind_is_valid(typep->type_kind));

		flags = br_r8(&Br);
		if (flags & PGTF_CONST)
			typep->flags |= FYTPF_CONST;
		if (flags & PGTF_VOLATILE)
			typep->flags |= FYTPF_VOLATILE;
		if (flags & PGTF_RESTRICT)
			typep->flags |= FYTPF_RESTRICT;
		if (flags & PGTF_DECL)
			typep->decl.id = br_rid(&Br, Di);
		if (flags & PGTF_DEP)
			typep->dependent_type.id = br_rid(&Br, Ti);
		if (flags & PGTF_ECOUNT)
			typep->element_count = br_rid(&Br, Ci);
	}

	for (declp = rflb->gen_decls, i = 0; i < Dc; i++, declp++) {
		declp->decl_type = br_r8(&Br);
		assert(fy_decl_type_is_valid(declp->decl_type));
		declp->type.id = br_rid(&Br, Ti);
		declp->name.offset = br_rX(&Br, Si);
		switch (declp->decl_type) {
		case FYDT_BITFIELD:
			declp->bit_width = br_r8(&Br);
			break;
		case FYDT_ENUM_VALUE:
			declp->enum_value.u = br_rX(&Br, Vi);
			break;
		default:
			break;
		}
		declp->comment.offset = br_rX(&Br, Si);
	}

	/* it must be consumed completely */
	assert(br_curr(&Br) == PGHDR_SIZE + Ts + Ds);

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
