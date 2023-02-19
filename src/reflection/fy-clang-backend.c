/*
 * fy-clang-backend.c - Clang based C type backend
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

#include "fy-reflection-private.h"

#include "fy-clang-backend.h"

/* clang */
#include <clang-c/Index.h>
#include <clang-c/Documentation.h>

static int clang_reflection_setup(struct fy_reflection *rfl);
static void clang_reflection_cleanup(struct fy_reflection *rfl);

static int clang_import_setup(struct fy_import *imp, const void *user);
static void clang_import_cleanup(struct fy_import *imp);

static int clang_decl_setup(struct fy_decl *decl, void *user);
static void clang_decl_cleanup(struct fy_decl *decl);

static int clang_type_setup(struct fy_type *ft, void *user);
static void clang_type_cleanup(struct fy_type *ft);

static const struct fy_reflection_backend_ops clang_ops = {
	.reflection_setup = clang_reflection_setup,
	.reflection_cleanup = clang_reflection_cleanup,
	.import_setup = clang_import_setup,
	.import_cleanup = clang_import_cleanup,
	.type_setup = clang_type_setup,
	.type_cleanup = clang_type_cleanup,
	.decl_setup = clang_decl_setup,
	.decl_cleanup = clang_decl_cleanup,
};

const struct fy_reflection_backend fy_reflection_clang_backend = {
	.name = "clang",
	.ops = &clang_ops,
};

struct clang_str {
	CXString cx;
	const char *str;
};

static inline void clang_str_setup(struct clang_str *cstr, CXString cx)
{
	assert(cstr);
	memset(cstr, 0, sizeof(*cstr));

	cstr->cx = cx;
	cstr->str = clang_getCString(cx);
}

static inline void clang_str_cleanup(struct clang_str *cstr)
{
	if (!cstr)
		return;
	if (cstr->str)
		clang_disposeString(cstr->cx);
	memset(cstr, 0, sizeof(*cstr));
}

static inline const char *clang_str_get(struct clang_str *cstr)
{
	if (!cstr || !cstr->str)
		return "";
	return cstr->str;
}

#define clang_str_get_alloca(_cx) \
	({ \
		struct clang_str _cstr; \
		const char *_str1; \
		char *_str2; \
		size_t _len; \
		\
		clang_str_setup(&_cstr, _cx); \
		_str1 = clang_str_get(&_cstr); \
		_len = strlen(_str1); \
		_str2 = alloca(_len + 1); \
		memcpy(_str2, _str1, _len + 1); \
		clang_str_cleanup(&_cstr); \
		(const char *)_str2; \
	})

struct clang_reflect_backend {
	CXIndex index;
};

struct clang_import_backend {
	CXTranslationUnit tu;
	CXTargetInfo ti;
	struct clang_str target_triple;
	int level;
	char *file;
	char *name;
	const char *target_triple_str;
};

struct clang_decl_user {
	CXCursor cursor;
	CXCursor parent;
	bool is_fake_func;
};

struct clang_decl_backend {
	CXCursor cursor;
	CXSourceLocation location;
	CXType type;
	CXComment comment;
	struct clang_str raw_comment;

	FILE *comments_linear_fp;
	char *comments_linear;
	size_t comments_linear_size;
	enum CXCommentKind last_comment_kind;

	struct clang_str cursor_kind_spelling;
	struct clang_str cursor_spelling;
	struct clang_str cursor_display_name;
	struct clang_str cursor_usr;

	struct clang_str type_kind_spelling;
	struct clang_str type_spelling;

	/* from clang_getFileLocation */
	CXFile file;
	unsigned line, column, offset;
	struct fy_source_location source_location;

	union {
		struct {
			struct {
				CXType type;
				CXCursor cursor;
				struct clang_str type_kind_spelling;
				struct clang_str type_spelling;
			} underlying;
		} typedef_info;
		struct {
			struct {
				CXType type;
				struct clang_str type_kind_spelling;
				struct clang_str type_spelling;
			} inttype;
		} enum_info;
	};
};

struct clang_type_user {
	CXType type;
};

struct clang_type_backend {
	CXType type;
	CXType dependent_type;
	struct clang_str dependent_type_name;
};

static inline enum fy_type_kind
clang_map_type_kind(enum CXTypeKind clang_type, enum CXCursorKind cursor_kind)
{
	switch (clang_type) {
	/* basic types */
	case CXType_Void:
		return FYTK_VOID;
	case CXType_Bool:
		return FYTK_BOOL;
	case CXType_Char_S:
		return FYTK_CHAR;
	case CXType_UChar:
		return FYTK_UCHAR;
	case CXType_SChar:
		return FYTK_SCHAR;
	case CXType_Short:
		return FYTK_SHORT;
	case CXType_UShort:
		return FYTK_USHORT;
	case CXType_Int:
		return FYTK_INT;
	case CXType_UInt:
		return FYTK_UINT;
	case CXType_Long:
		return FYTK_LONG;
	case CXType_ULong:
		return FYTK_ULONG;
	case CXType_LongLong:
		return FYTK_LONGLONG;
	case CXType_ULongLong:
		return FYTK_ULONGLONG;
	case CXType_Int128:
		return FYTK_INT128;
	case CXType_UInt128:
		return FYTK_UINT128;
	case CXType_Float:
		return FYTK_FLOAT;
	case CXType_Double:
		return FYTK_DOUBLE;
	case CXType_LongDouble:
		return FYTK_LONGDOUBLE;
	case CXType_Float128:
		return FYTK_FLOAT128;
	case CXType_Half:
		return FYTK_FLOAT16;

	/* compound types */
	case CXType_Pointer:
		return FYTK_PTR;
	case CXType_Record:
		switch (cursor_kind) {
		case CXCursor_StructDecl:
			return FYTK_STRUCT;
		case CXCursor_UnionDecl:
			return FYTK_UNION;
		default:
			break;
		}
		/* default */
		return FYTK_RECORD;
	case CXType_Enum:
		return FYTK_ENUM;
	case CXType_Typedef:
		return FYTK_TYPEDEF;
	case CXType_ConstantArray:
		return FYTK_CONSTARRAY;
	case CXType_IncompleteArray:
		return FYTK_INCOMPLETEARRAY;

	case CXType_FunctionProto:
	case CXType_FunctionNoProto:
		return FYTK_FUNCTION;

	default:
		break;
	}

	return FYTK_INVALID;
}

/* 1=unsigned, -1=signed, 0 sign not relevant */
static inline int
clang_type_kind_signess(enum CXTypeKind clang_type)
{
	switch (clang_type) {
	case CXType_Bool:
	case CXType_UChar:
	case CXType_UShort:
	case CXType_UInt:
	case CXType_ULong:
	case CXType_ULongLong:
	case CXType_UInt128:
		return 1;	/* unsigned */
	case CXType_Char_S:
	case CXType_SChar:
	case CXType_Short:
	case CXType_Int:
	case CXType_Long:
	case CXType_LongLong:
	case CXType_Int128:
	case CXType_Float:
	case CXType_Double:
	case CXType_LongDouble:
		return -1;

	/* anything else doesn't have a sign */
	default:
		break;
	}

	return 0;
}

static struct fy_decl *
clang_lookup_decl_by_cursor(struct fy_reflection *rfl, CXCursor cursor)
{
	struct fy_decl *decl;
	struct clang_decl_backend *declb;

	for (decl = fy_decl_list_head(&rfl->decls); decl != NULL; decl = fy_decl_next(&rfl->decls, decl)) {
		declb = decl->backend;
		if (!declb)
			continue;
		if (clang_equalCursors(declb->cursor, cursor))
			return decl;
	}
	return NULL;
}

static struct fy_type *
clang_lookup_type_by_type(struct fy_reflection *rfl, CXType type, struct fy_decl *decl)
{
	struct fy_type *ft, *ft_best;
	struct clang_type_backend *ftb;
	const char *c1, *c2;

	ft_best = NULL;
	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {
		ftb = ft->backend;
		if (!ftb)
			continue;
		if (!clang_equalTypes(ftb->type, type))
			continue;

		/* if a decl was provided we must match the comment too */
		if (decl) {
			c1 = fy_decl_get_yaml_comment(decl);
			if (!c1)
				c1 = "";
			c2 = fy_decl_get_yaml_comment(ft->decl);
			if (!c2)
				c2 = "";
			if (!strcmp(c1, c2))
				return ft;
		} else if (!ft_best) {
			/* first one in */
			ft_best = ft;
		} else if (!ft->decl->raw_comment) {
			/* we prefer the bare type */
			ft_best = ft;
		}
	}
	return ft_best;
}

static struct fy_type *
clang_register_type(struct fy_reflection *rfl, struct fy_decl *decl, CXCursor cursor)
{
	struct fy_type *ft = NULL;
	struct clang_type_user ftu_local, *ftu = &ftu_local;
	CXType type;
	enum fy_type_kind type_kind;
	const char *type_name;
	bool elaborated, anonymous;

	type = clang_getCursorType(cursor);

	elaborated = false;
	if (type.kind == CXType_Elaborated) {
		elaborated = true;
		type = clang_Type_getNamedType(type);
	}

	type_kind = clang_map_type_kind(type.kind, clang_getTypeDeclaration(type).kind);
	if (type_kind == FYTK_INVALID)
		return NULL;

	anonymous = decl && decl->anonymous;
	(void)anonymous;

	if (!decl || fy_type_kind_is_primitive(type_kind) || fy_type_kind_is_like_ptr(type_kind) || type_kind == FYTK_TYPEDEF)
		type_name = clang_str_get_alloca(clang_getTypeSpelling(type));
	else {
		type_name = clang_str_get_alloca(clang_getCursorDisplayName(cursor));
		if (!type_name[0])
			type_name = clang_str_get_alloca(clang_getCursorUSR(cursor));
	}

	ft = clang_lookup_type_by_type(rfl, type, decl);

	if (!ft) {
		if (elaborated) {
			printf("%s: elaborated type_name=%s does not exist\n",
					__func__, type_name);

			/* try to pull it in */

			goto err_out;
		}

		memset(ftu, 0, sizeof(*ftu));
		ftu->type = type;

		ft = fy_type_create(rfl, type_kind, type_name, decl, ftu);
		if (!ft)
			goto err_out;
		fy_type_list_add_tail(&rfl->types, ft);
	}
out:
	return ft;

err_out:
	fy_type_destroy(ft);
	ft = NULL;
	goto out;
}

static enum CXChildVisitResult
fy_import_backend_root_visitor(CXCursor cursor, CXCursor parent, CXClientData client_data)
{
	struct fy_import *imp = client_data;
	struct fy_reflection *rfl;
	struct clang_decl_user declu_local, *declu = &declu_local;
	enum fy_decl_type decl_type;
	enum CXCursorKind cursor_kind;
	const char *cursor_spelling;
	const char *cursor_kind_spelling;
	struct fy_decl *decl = NULL;
	unsigned int ret;
	bool visit_children;

	assert(imp);
	rfl = imp->rfl;
	assert(rfl);

	visit_children = true;
	cursor_kind = clang_getCursorKind(cursor);
	switch (cursor_kind) {
	case CXCursor_StructDecl:
		decl_type = FYDT_STRUCT;
		break;
	case CXCursor_UnionDecl:
		decl_type = FYDT_UNION;
		break;
	case CXCursor_ClassDecl:
		decl_type = FYDT_CLASS;
		break;
	case CXCursor_EnumDecl:
		visit_children = false;
		decl_type = FYDT_ENUM;
		break;
	case CXCursor_TypedefDecl:
		decl_type = FYDT_TYPEDEF;
		break;
	case CXCursor_EnumConstantDecl:
		decl_type = FYDT_ENUM_VALUE;
		break;
	default:
		decl_type = FYDT_NONE;
		break;
	}

	/* cannot handle cursor type */
	if (decl_type == FYDT_NONE)
		return CXChildVisit_Continue;

	/* skip declarations only */
	if (!clang_isCursorDefinition(cursor))
		return CXChildVisit_Continue;

#if 1
	/* if the declaration is present already, do not continue */
	decl = clang_lookup_decl_by_cursor(rfl, cursor);
	if (decl)
		return CXChildVisit_Continue;
#endif

	cursor_spelling = clang_str_get_alloca(clang_getCursorSpelling(cursor));
	cursor_kind_spelling = clang_str_get_alloca(clang_getCursorKindSpelling(cursor_kind));
	(void)cursor_kind_spelling;

	/* visit the children first, so that we pick up definition intermingled */
	if (visit_children) {
		ret = clang_visitChildren(cursor, fy_import_backend_root_visitor, imp);
		if (ret)
			return CXChildVisit_Break;
	}

	memset(declu, 0, sizeof(*declu));
	declu->cursor = cursor;
	declu->parent = parent;

	decl = fy_decl_create(rfl, imp, NULL, decl_type, cursor_spelling, declu);
	if (!decl)
		goto err_out;

	decl->type = clang_register_type(rfl, decl, cursor);
	if (!decl->type)
		goto err_out;

	fy_decl_list_add_tail(&rfl->decls, decl);

	return CXChildVisit_Continue;

err_out:
	fy_decl_destroy(decl);
	return CXChildVisit_Break;
}

static int clang_reflection_setup(struct fy_reflection *rfl)
{
	struct clang_reflect_backend *rflb;
	const struct fy_clang_backend_reflection_cfg *backend_cfg;

	if (!rfl->cfg.backend_cfg)
		return -1;

	rflb = malloc(sizeof(*rflb));
	if (!rflb)
		goto err_out;
	memset(rflb, 0, sizeof(*rflb));

	rfl->backend = rflb;

	backend_cfg = rfl->cfg.backend_cfg;

	rflb->index = clang_createIndex(0, backend_cfg->display_diagnostics ? 1 : 0);
	if (!rflb->index)
		goto err_out;

	return 0;

err_out:
	clang_reflection_cleanup(rfl);
	return -1;
}

static void clang_reflection_cleanup(struct fy_reflection *rfl)
{
	struct clang_reflect_backend *rflb;

	if (!rfl || !rfl->backend)
		return;

	rflb = rfl->backend;
	rfl->backend = NULL;

	if (rflb->index)
		clang_disposeIndex(rflb->index);

	free(rflb);
}

static int clang_import_setup(struct fy_import *imp, const void *user)
{
	const struct fy_clang_backend_import_cfg *clang_cfg = user;
	struct fy_reflection *rfl;
	struct clang_reflect_backend *rflb;
	struct clang_import_backend *impb;
	int argc;
	const char * const *argv;
	const char *default_argv[2];
	int len;
	unsigned num_diag, ret;

	if (!clang_cfg || !clang_cfg->file)
		return -1;

	rfl = imp->rfl;
	rflb = rfl->backend;

	impb = malloc(sizeof(*impb));
	if (!impb)
		goto err_out;
	memset(impb, 0, sizeof(*impb));

	imp->backend = impb;

	if (!clang_cfg->argc || !clang_cfg->argv) {
		default_argv[0] = "-fparse-all-comments";
		default_argv[1] = NULL;

		argc = 1;
		argv = default_argv;
	} else {
		argc = clang_cfg->argc;
		argv = clang_cfg->argv;
	}

	assert(rflb);
	assert(rflb->index);

	impb->file = strdup(clang_cfg->file);
	if (!impb->file)
		goto err_out;

	impb->tu = clang_createTranslationUnitFromSourceFile(rflb->index, impb->file, argc, argv, 0, NULL);
	if (!impb->tu)
		goto err_out;

	/* we don't want any! warnings or errors */
	num_diag = clang_getNumDiagnostics(impb->tu);
	if (num_diag)
		goto err_out;

	impb->ti = clang_getTranslationUnitTargetInfo(impb->tu);
	if (!impb->ti)
		goto err_out;

	clang_str_setup(&impb->target_triple, clang_TargetInfo_getTriple(impb->ti));

	ret = clang_visitChildren(clang_getTranslationUnitCursor(impb->tu), fy_import_backend_root_visitor, imp);
	if (ret)
		goto err_out;

	impb->target_triple_str = clang_str_get(&impb->target_triple);

	len = snprintf(NULL, 0, "%s-%s", impb->file, impb->target_triple_str);
	if (len < 0)
		goto err_out;

	impb->name = malloc(len + 1);
	if (!impb->name)
		goto err_out;

	snprintf(impb->name, len + 1, "%s-%s", impb->file, impb->target_triple_str);

	/* forward */
	imp->name = impb->name;

	return 0;

err_out:
	clang_import_cleanup(imp);
	return -1;
}

static void clang_import_cleanup(struct fy_import *imp)
{
	struct clang_import_backend *impb;

	if (!imp || !imp->backend)
		return;

	impb = imp->backend;
	imp->backend = NULL;

	if (impb->name)
		free(impb->name);

	clang_str_cleanup(&impb->target_triple);

	if (impb->ti)
		clang_TargetInfo_dispose(impb->ti);

	if (impb->tu)
		clang_disposeTranslationUnit(impb->tu);

	if (impb->file)
		free(impb->file);

	free(impb);
}

static void clang_decl_cleanup(struct fy_decl *decl)
{
	struct clang_decl_backend *declb;

	if (!decl || !decl->backend)
		return;

	declb = decl->backend;
	decl->backend = NULL;

	if (declb->comments_linear_fp)
		fclose(declb->comments_linear_fp);
	if (declb->comments_linear)
		free(declb->comments_linear);

	switch (decl->decl_type) {
	case FYDT_TYPEDEF:
		clang_str_cleanup(&declb->typedef_info.underlying.type_kind_spelling);
		clang_str_cleanup(&declb->typedef_info.underlying.type_spelling);
		break;
	case FYDT_ENUM:
		clang_str_cleanup(&declb->enum_info.inttype.type_kind_spelling);
		clang_str_cleanup(&declb->enum_info.inttype.type_spelling);
		break;
	default:
		break;
	}

	clang_str_cleanup(&declb->type_kind_spelling);
	clang_str_cleanup(&declb->type_spelling);

	clang_str_cleanup(&declb->cursor_kind_spelling);
	clang_str_cleanup(&declb->cursor_spelling);
	clang_str_cleanup(&declb->cursor_display_name);
	clang_str_cleanup(&declb->cursor_usr);

	clang_str_cleanup(&declb->raw_comment);

	free(declb);
}

static enum CXVisitorResult
fy_import_backend_struct_field_visitor(CXCursor cursor, CXClientData client_data)
{
	enum CXVisitorResult res = CXVisit_Continue;
	struct fy_import *imp;
	struct fy_reflection *rfl;
	struct fy_decl *decl = NULL;
	struct fy_decl *parent_decl = client_data;
	struct clang_decl_user declu_local, *declu = &declu_local;
	CXType cursor_type;
	const char *cursor_spelling;
	const char *cursor_kind_spelling;
	const char *type_spelling;
	const char *type_kind_spelling;
	bool is_bitfield;

	cursor_type = clang_getCursorType(cursor);

	cursor_spelling = clang_str_get_alloca(clang_getCursorSpelling(cursor));
	cursor_kind_spelling = clang_str_get_alloca(clang_getCursorKindSpelling(clang_getCursorKind(cursor)));
	type_spelling = clang_str_get_alloca(clang_getTypeSpelling(cursor_type));
	type_kind_spelling = clang_str_get_alloca(clang_getTypeKindSpelling(cursor_type.kind));
	(void)cursor_kind_spelling;
	(void)type_spelling;
	(void)type_kind_spelling;

	/* fprintf(stderr, "> %s:'%s' cursor %s:'%s' type %s:'%s'\n",
			decl_type_txt[parent_decl->decl_type], parent_decl->name,
			cursor_kind_spelling, cursor_spelling,
			type_kind_spelling, type_spelling); */

	memset(declu, 0, sizeof(*declu));
	declu->cursor = cursor;
	declu->parent = clang_getCursorSemanticParent(cursor);

	imp = parent_decl->imp;
	rfl = imp->rfl;

	is_bitfield = clang_Cursor_isBitField(cursor);

	/* create field declaration */
	decl = fy_decl_create(rfl, imp, parent_decl,
			!is_bitfield ? FYDT_FIELD : FYDT_BITFIELD,
			cursor_spelling, declu);
	if (!decl)
		goto err_out;

	decl->type = clang_register_type(rfl, decl, cursor);
	if (!decl->type)
		goto err_out;

	/* add to the parent */
	fy_decl_list_add_tail(&parent_decl->children, decl);

out:
	return res;

err_out:
	fy_decl_destroy(decl);
	res = CXVisit_Break;
	goto out;
}

static enum CXChildVisitResult
fy_import_backend_enum_visitor(CXCursor cursor, CXCursor parent, CXClientData client_data)
{
	enum CXChildVisitResult res = CXChildVisit_Continue;
	struct fy_import *imp;
	struct fy_reflection *rfl;
	struct fy_decl *decl = NULL;
	struct fy_decl *parent_decl = client_data;
	struct clang_decl_user declu_local, *declu = &declu_local;
	CXType cursor_type;
	const char *cursor_spelling;
	const char *cursor_kind_spelling;
	const char *type_spelling;
	const char *type_kind_spelling;

	cursor_type = clang_getCursorType(cursor);

	cursor_spelling = clang_str_get_alloca(clang_getCursorSpelling(cursor));
	cursor_kind_spelling = clang_str_get_alloca(clang_getCursorKindSpelling(clang_getCursorKind(cursor)));
	type_spelling = clang_str_get_alloca(clang_getTypeSpelling(cursor_type));
	type_kind_spelling = clang_str_get_alloca(clang_getTypeKindSpelling(cursor_type.kind));
	(void)cursor_kind_spelling;
	(void)type_spelling;
	(void)type_kind_spelling;

	/* fprintf(stderr, ">>>> %s:'%s' cursor %s:'%s' type %s:'%s'\n",
			decl_type_txt[parent_decl->decl_type], parent_decl->name,
			cursor_kind_spelling, cursor_spelling,
			type_kind_spelling, type_spelling); */

	memset(declu, 0, sizeof(*declu));
	declu->cursor = cursor;
	declu->parent = parent;

	imp = parent_decl->imp;
	rfl = imp->rfl;

	/* create field declaration */
	decl = fy_decl_create(rfl, imp, parent_decl, FYDT_ENUM_VALUE, cursor_spelling, declu);
	if (!decl)
		goto err_out;

	decl->type = clang_register_type(rfl, decl, cursor);
	if (!decl->type)
		goto err_out;

	/* add to the parent */
	fy_decl_list_add_tail(&parent_decl->children, decl);

out:
	return res;

err_out:
	fy_decl_destroy(decl);
	res = CXChildVisit_Break;
	goto out;
}

static const struct fy_source_location *
clang_backend_get_location(struct fy_decl *decl)
{
	struct fy_reflection *rfl;
	struct fy_import *imp;
	struct clang_decl_backend *declb;
	CXString cxfilename;
	const char *filename = NULL;
	struct fy_source_file *source_file;
	struct fy_source_file *source_file_new;

	assert(decl && decl->backend);

	imp = decl->imp;
	rfl = imp->rfl;
	declb = decl->backend;

	clang_getFileLocation(declb->location, &declb->file, &declb->line, &declb->column, &declb->offset);
	// clang_getExpansionLocation(declb->location, &declb->file, &declb->line, &declb->column, &declb->offset);

	cxfilename = clang_getFileName(declb->file);
	filename = clang_getCString(cxfilename);

	source_file = fy_reflection_lookup_source_file(rfl, filename);
	if (!source_file) {
		source_file_new = fy_source_file_create(rfl, filename);
		if (!source_file_new)
			goto err_out;

		source_file_new->filetime = clang_getFileTime(declb->file);

		source_file_new->system_header = !!clang_Location_isInSystemHeader(declb->location);
		source_file_new->main_file = !!clang_Location_isFromMainFile(declb->location);

		fy_source_file_list_add_tail(&rfl->source_files, source_file_new);

		source_file = source_file_new;
	}

	memset(&declb->source_location, 0, sizeof(declb->source_location));

	declb->source_location.source_file = source_file;
	declb->source_location.line = declb->line;
	declb->source_location.column = declb->column;
	declb->source_location.offset = declb->offset;

	clang_disposeString(cxfilename);

	return &declb->source_location;

err_out:

	if (filename)
		clang_disposeString(cxfilename);

	return NULL;
}

static int clang_decl_setup(struct fy_decl *decl, void *user)
{
	const struct clang_decl_user *declu = user;
	struct clang_decl_backend *declb;
	const char *raw_comment;
	int signess;

	/* fake declaration */
	if (!declu)
		return 0;

	declb = malloc(sizeof(*declb));
	if (!declb)
		goto err_out;
	memset(declb, 0, sizeof(*declb));

	decl->backend = declb;

	declb->cursor = declu->cursor;
	declb->location = clang_getCursorLocation(declu->cursor);
	declb->type = clang_getCursorType(declu->cursor);
	declb->comment = clang_Cursor_getParsedComment(declu->cursor);

	clang_str_setup(&declb->cursor_kind_spelling, clang_getCursorKindSpelling(clang_getCursorKind(declu->cursor)));
	clang_str_setup(&declb->cursor_spelling, clang_getCursorSpelling(declu->cursor));
	clang_str_setup(&declb->cursor_display_name, clang_getCursorDisplayName(declu->cursor));
	clang_str_setup(&declb->cursor_usr, clang_getCursorUSR(declu->cursor));

	clang_str_setup(&declb->type_kind_spelling, clang_getTypeKindSpelling(declb->type.kind));
	clang_str_setup(&declb->type_spelling, clang_getTypeSpelling(declb->type));

	/* must be after the backend assignment */
	decl->source_location = clang_backend_get_location(decl);
	decl->spelling = clang_str_get(&declb->cursor_spelling);
	decl->display_name = clang_str_get(&declb->cursor_display_name);
	decl->signature = clang_str_get(&declb->cursor_usr);

	/* mark it as anonymous */
	decl->anonymous = !!clang_Cursor_isAnonymous(declu->cursor) || !decl->name || !decl->name[0];

	decl->in_system_header = !!clang_Location_isInSystemHeader(declb->location);
	decl->from_main_file = !!clang_Location_isFromMainFile(declb->location);

	switch (decl->decl_type) {
	case FYDT_TYPEDEF:
		declb->typedef_info.underlying.type = clang_getTypedefDeclUnderlyingType(declu->cursor);
		declb->typedef_info.underlying.cursor = clang_getTypeDeclaration(declb->typedef_info.underlying.type);
		clang_str_setup(&declb->typedef_info.underlying.type_kind_spelling, clang_getTypeKindSpelling(declb->typedef_info.underlying.type.kind));
		clang_str_setup(&declb->typedef_info.underlying.type_spelling, clang_getTypeSpelling(declb->typedef_info.underlying.type));

	case FYDT_STRUCT:
	case FYDT_UNION:
		/* return value is bogus, will return non zero at the end of a normal visit */
		(void)clang_Type_visitFields(declb->type, fy_import_backend_struct_field_visitor, decl);
		break;

	case FYDT_ENUM:
		declb->enum_info.inttype.type = clang_getEnumDeclIntegerType(declu->cursor);
		clang_str_setup(&declb->enum_info.inttype.type_kind_spelling, clang_getTypeKindSpelling(declb->enum_info.inttype.type.kind));
		clang_str_setup(&declb->enum_info.inttype.type_spelling, clang_getTypeSpelling(declb->enum_info.inttype.type));

		/* XXX no concrete type for the enum, only storage size */
		decl->enum_decl.type_kind = clang_map_type_kind(declb->enum_info.inttype.type.kind, CXCursor_EnumDecl);
		assert(fy_type_kind_is_enum_constant_decl(decl->enum_decl.type_kind));

		/* there is no field for enums, so visit children */
		clang_visitChildren(declu->cursor, fy_import_backend_enum_visitor, decl);
		break;

	case FYDT_ENUM_VALUE:
		assert(decl->parent->decl_type == FYDT_ENUM);

		decl->enum_value_decl.type_kind = clang_map_type_kind(declb->type.kind, CXCursor_EnumConstantDecl);
		assert(fy_type_kind_is_enum_constant_decl(decl->enum_value_decl.type_kind));

		signess = clang_type_kind_signess(declb->type.kind);
		assert(signess != 0);
		if (signess > 0)
			decl->enum_value_decl.val.u = clang_getEnumConstantDeclUnsignedValue(declu->cursor);
		else
			decl->enum_value_decl.val.s = clang_getEnumConstantDeclValue(declu->cursor);
		break;

	case FYDT_FIELD:
		assert(decl->parent->decl_type == FYDT_STRUCT || decl->parent->decl_type == FYDT_UNION);
		decl->field_decl.byte_offset = clang_Cursor_getOffsetOfField(declu->cursor) / 8;
		break;

	case FYDT_BITFIELD:
		assert(decl->parent->decl_type == FYDT_STRUCT || decl->parent->decl_type == FYDT_UNION);
		decl->bitfield_decl.bit_offset = clang_Cursor_getOffsetOfField(declu->cursor);
		decl->bitfield_decl.bit_width = clang_getFieldDeclBitWidth(declu->cursor);
		break;

	case FYDT_FUNCTION:
		abort();
		break;

	default:
		break;
	}

	clang_str_setup(&declb->raw_comment, clang_Cursor_getRawCommentText(declu->cursor));
	raw_comment = clang_str_get(&declb->raw_comment);
	if (raw_comment && strlen(raw_comment) > 0)
		decl->raw_comment = raw_comment;

	return 0;

err_out:
	clang_decl_cleanup(decl);
	return -1;
}

static int clang_type_setup(struct fy_type *ft, void *user)
{
	const struct clang_type_user *ftu = user;
	struct clang_type_user fttu_local, *fttu = &fttu_local;
	struct fy_reflection *rfl = ft->rfl;
	struct clang_type_backend *ftb;
	const char *tname;
	CXType ttype;
	enum fy_type_kind ttype_kind;
	struct fy_type *ftt;
	bool elaborated;
	struct fy_type *ft2;
	struct clang_type_backend *ft2b;
	long long llret;
	int ret = 0;

	/* fake type */
	if (!ftu)
		return 0;

	ftb = malloc(sizeof(*ftb));
	if (!ftb)
		goto err_out;
	memset(ftb, 0, sizeof(*ftb));

	ftb->type = ftu->type;

	ft->backend = ftb;

	/* fill-in size and align for all types */
	ft->size = 0;
	ft->align = 0;

	llret = clang_Type_getSizeOf(ftb->type);
	if (llret > 0)
		ft->size = llret;
	llret = clang_Type_getAlignOf(ftb->type);
	if (llret > 0)
		ft->align = llret;

	if (ft->type_kind == FYTK_CONSTARRAY)
		ft->element_count = clang_getNumElements(ftb->type);	/* err, int should be enough for element counts */
	else
		ft->element_count = 1;

	/* finally update the qualifiers */
	ft->is_const = !!clang_isConstQualifiedType(ftb->type);
	ft->is_volatile = !!clang_isVolatileQualifiedType(ftb->type);
	ft->is_restrict = !!clang_isRestrictQualifiedType(ftb->type);

	if (fy_type_kind_is_dependent(ft->type_kind)) {

		switch (ft->type_kind) {
		case FYTK_TYPEDEF:
			ttype = clang_getTypedefDeclUnderlyingType(clang_getTypeDeclaration(ftb->type));
			break;
		case FYTK_PTR:
			ttype = clang_getPointeeType(ftb->type);
			break;
		case FYTK_CONSTARRAY:
		case FYTK_INCOMPLETEARRAY:
			ttype = clang_getArrayElementType(ftb->type);
			break;
		case FYTK_ENUM:
			ttype = clang_getEnumDeclIntegerType(clang_getTypeDeclaration(ftb->type));
			break;
		default:
			memset(&ttype, 0, sizeof(ttype));
			abort();
			break;
		}

		ftb->dependent_type = ttype;

		elaborated = false;
		if (ttype.kind == CXType_Elaborated) {
			elaborated = true;
			ttype = clang_Type_getNamedType(ttype);
		}
		(void)elaborated;

		tname = clang_str_get_alloca(clang_getTypeSpelling(ttype));

		ttype_kind = clang_map_type_kind(ttype.kind, clang_getTypeDeclaration(ttype).kind);

		assert(ttype_kind != FYTK_INVALID);

		ftt = clang_lookup_type_by_type(rfl, ttype, NULL);

		if (!ftt && (ttype_kind == FYTK_INVALID || ttype_kind == FYTK_FUNCTION ||
			     fy_type_kind_is_primitive(ttype_kind) || fy_type_kind_is_like_ptr(ttype_kind))) {
			/* builtin or pointer but not registered */

			memset(fttu, 0, sizeof(*fttu));
			fttu->type = ttype;

			if (ttype_kind == FYTK_FUNCTION) {
				struct fy_decl *declf;
				struct clang_decl_user declfu_local, *declfu = &declfu_local;
				struct fy_import *imp;

				memset(declfu, 0, sizeof(*declfu));
				declfu->cursor = clang_getNullCursor();
				declfu->parent = clang_getNullCursor();
				declfu->is_fake_func = true;

				if (ft->decl)
					imp = ft->decl->imp;
				else
					imp = rfl->imp_curr;

				assert(imp);
				declf = fy_decl_create(rfl, imp, NULL, FYDT_FUNCTION, tname, NULL);
				if (!declf)
					goto err_out;

				fy_decl_list_add_tail(&rfl->decls, declf);

				ftt = fy_type_create(rfl, ttype_kind, tname, declf, fttu);
				if (!ftt)
					goto err_out;

				fy_type_list_add_tail(&rfl->types, ftt);

				declf->type = ftt;
			} else {
				ftt = fy_type_create(rfl, ttype_kind, tname, NULL, fttu);
				if (!ftt)
					goto err_out;

				fy_type_list_add_tail(&rfl->types, ftt);

			}
		}

		ftb->dependent_type = ttype;

		clang_str_setup(&ftb->dependent_type_name, clang_getTypeSpelling(ttype));

		if (ftt) {
			ft->dependent_type = ftt;
			ft->unresolved = false;
		} else {
			ft->unresolved = true;
			rfl->unresolved_types_count++;
		}
		/* save this info */
		ft->dependent_type_kind = ttype_kind;
		ft->dependent_type_name = clang_str_get(&ftb->dependent_type_name);
	}

	/* look for unresolves that match */
	for (ft2 = fy_type_list_head(&rfl->types);
	     rfl->unresolved_types_count > 0 && ft2 != NULL;
	     ft2 = fy_type_next(&rfl->types, ft2)) {

		if (!ft2->unresolved)
			continue;

		ft2b = ft2->backend;

		if (!clang_equalTypes(ft2b->dependent_type, ftb->type))
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
	clang_type_cleanup(ft);
	ret = -1;
	goto out;
}

static void clang_type_cleanup(struct fy_type *ft)
{
	struct clang_type_backend *ftb;

	if (!ft || !ft->backend)
		return;

	ftb = ft->backend;
	ft->backend = NULL;

	clang_str_cleanup(&ftb->dependent_type_name);

	free(ftb);
}
