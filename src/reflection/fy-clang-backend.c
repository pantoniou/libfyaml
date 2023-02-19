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

#include "fy-utils.h"
#include "fy-reflection-private.h"

#include "fy-clang-backend.h"

/* clang */
#include <clang-c/Index.h>
#include <clang-c/Documentation.h>

void clang_dump_types(struct fy_reflection *rfl);

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

static int clang_type_resolve(struct fy_type *ft_new);
static int clang_import_done(struct fy_import *imp);

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

#define clang_str_get_malloc(_cx) \
	({ \
		struct clang_str _cstr; \
		const char *_str1; \
		char *_str2; \
		size_t _len; \
		\
		clang_str_setup(&_cstr, _cx); \
		_str1 = clang_str_get(&_cstr); \
		_len = strlen(_str1); \
		_str2 = malloc(_len + 1); \
		if (_str2) \
			memcpy(_str2, _str1, _len + 1); \
		clang_str_cleanup(&_cstr); \
		_str2; \
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
	bool field_visit_error;
};

struct clang_decl_user {
	CXCursor cursor;
	CXCursor parent;
	unsigned int quals;
};

struct clang_decl_backend {
	struct fy_decl *decl;
	struct list_head node;
	CXSourceRange extent;
	CXSourceLocation start_location;
	CXSourceLocation end_location;
	CXType type;
	CXComment comment;

	struct clang_str raw_comment;
	unsigned int quals;
	bool is_incomplete;	/* if this is an incomplete type */
	bool is_invalid;	/* if this is an invalid type */

	enum CXCommentKind last_comment_kind;

	struct clang_str type_spelling;

	/* from clang_getFileLocation */
	CXFile file;
	struct fy_source_range source_range;

	union {
		struct {
			struct {
				CXType type;
			} underlying;
		} typedef_info;
		struct {
			struct {
				CXType type;
			} inttype;
		} enum_info;
	};
};

struct clang_type_user {
	CXType type;
	CXType dependent_type;
};

struct clang_type_backend {
	CXType type;
	CXType dependent_type;
	CXType final_dependent_type;
};

static int clang_decl_update(struct fy_decl *decl, const struct clang_decl_user *declu);
static int clang_type_update(struct fy_type *ft, const struct clang_type_user *ftu);

static inline enum fy_type_kind
clang_map_primary_type_kind(enum CXTypeKind clang_type_kind)
{
	switch (clang_type_kind) {
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
	default:
		break;
	}

	return FYTK_INVALID;
}

static inline enum fy_type_kind
clang_map_primary_type(CXType type)
{
	if (type.kind == CXType_Elaborated)
		type = clang_Type_getNamedType(type);

	return clang_map_primary_type_kind(type.kind);
}

static inline enum fy_type_kind
clang_map_type_kind(enum CXTypeKind clang_type_kind, enum CXCursorKind cursor_kind)
{
	enum fy_type_kind type_kind;

	type_kind = clang_map_primary_type_kind(clang_type_kind);
	if (type_kind != FYTK_INVALID)
		return type_kind;

	switch (clang_type_kind) {
	/* basic types */
	case CXType_Void:
		return FYTK_VOID;
	case CXType_NullPtr:
		return FYTK_NULL;

	/* primary types are matched earlier */

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

static inline enum fy_type_kind
clang_map_type(CXType type)
{
	if (type.kind == CXType_Elaborated)
		type = clang_Type_getNamedType(type);

	return clang_map_type_kind(type.kind, clang_getTypeDeclaration(type).kind);
}

static inline int
clang_map_cursor_decl(CXCursor cursor)
{
	switch (clang_getCursorKind(cursor)) {
	case CXCursor_StructDecl:
		return FYDT_STRUCT;
	case CXCursor_UnionDecl:
		return FYDT_UNION;
	case CXCursor_ClassDecl:
		return FYDT_CLASS;
	case CXCursor_EnumDecl:
		return FYDT_ENUM;
	case CXCursor_TypedefDecl:
		return FYDT_TYPEDEF;
	case CXCursor_EnumConstantDecl:
		return FYDT_ENUM_VALUE;
	default:
		break;
	}

	return FYDT_NONE;
}

static inline enum fy_decl_type
clang_map_type_decl(CXType type)
{
	if (type.kind == CXType_Elaborated)
		type = clang_Type_getNamedType(type);

	if ((type.kind >= CXType_FirstBuiltin && type.kind <= CXType_LastBuiltin) ||
	     type.kind == CXType_Pointer || type.kind == CXType_ConstantArray || type.kind == CXType_IncompleteArray)
		return FYDT_PRIMITIVE;

	if (type.kind == CXType_FunctionProto || type.kind == CXType_FunctionNoProto)
		return FYDT_FUNCTION;

	if (type.kind == CXType_Record) {
		switch (clang_getTypeDeclaration(type).kind) {
		case CXCursor_StructDecl:
			return FYDT_STRUCT;
		case CXCursor_UnionDecl:
			return FYDT_UNION;
		default:
			break;
		}
	}
	/* default */
	return FYDT_PRIMITIVE;
}

/* 1=unsigned, -1=signed, 0 sign not relevant */
static inline int
clang_type_kind_signess(enum CXTypeKind clang_type_kind)
{
	switch (clang_type_kind) {
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

static inline bool
clang_type_is_ptr(CXType type)
{
	if (type.kind == CXType_Elaborated)
		type = clang_Type_getNamedType(type);

	return type.kind == CXType_Pointer;
}

static inline bool
clang_type_is_array(CXType type)
{
	if (type.kind == CXType_Elaborated)
		type = clang_Type_getNamedType(type);

	return type.kind == CXType_ConstantArray ||
	       type.kind == CXType_IncompleteArray;
}

static inline bool
clang_type_is_ptr_or_array(CXType type)
{
	return clang_type_is_ptr(type) || clang_type_is_array(type);
}

static inline CXType
clang_type_get_pointee_or_element(CXType type)
{
	CXType ttype;

	if (clang_type_is_ptr(type)) {
		ttype = clang_getPointeeType(type);
		assert(ttype.kind != CXType_Invalid);
		return ttype;
	}
	if (clang_type_is_array(type)) {
		ttype = clang_getElementType(type);
		assert(ttype.kind != CXType_Invalid);
		return ttype;
	}

	memset(&ttype, 0, sizeof(ttype));
	ttype.kind = CXType_Invalid;
	return ttype;
}

static inline CXType
clang_type_get_dependent(CXType type)
{
	if (type.kind == CXType_Elaborated)
		type = clang_Type_getNamedType(type);

	switch (type.kind) {
	case CXType_Typedef:
		return clang_getTypedefDeclUnderlyingType(clang_getTypeDeclaration(type));
	case CXType_Pointer:
		return clang_getPointeeType(type);
	case CXType_ConstantArray:
	case CXType_IncompleteArray:
		return clang_getArrayElementType(type);
	case CXType_Enum:
		return clang_getEnumDeclIntegerType(clang_getTypeDeclaration(type));
	case CXType_FunctionProto:
	case CXType_FunctionNoProto:
		break;
	default:
		break;
	}
	memset(&type, 0, sizeof(type));
	type.kind = CXType_Invalid;
	return type;
}

static inline CXType
clang_type_get_base(CXType type)
{
	CXType ttype;

	while (clang_type_is_ptr_or_array(type)) {
		ttype = clang_type_get_pointee_or_element(type);
		if (ttype.kind == CXType_Invalid)
			break;

		type = ttype;
	}

	return type;
}

static inline bool
clang_type_is_primitive(CXType type)
{
	if (type.kind == CXType_Elaborated)
		type = clang_Type_getNamedType(type);

	switch (type.kind) {
	case CXType_Void:
	case CXType_Bool:
	case CXType_Char_S:
	case CXType_UChar:
	case CXType_SChar:
	case CXType_Short:
	case CXType_UShort:
	case CXType_Int:
	case CXType_UInt:
	case CXType_Long:
	case CXType_ULong:
	case CXType_LongLong:
	case CXType_ULongLong:
	case CXType_Int128:
	case CXType_UInt128:
	case CXType_Float:
	case CXType_Double:
	case CXType_LongDouble:
	case CXType_Float128:
	case CXType_Half:
		return true;
	default:
		break;
	}
	return false;
}

static inline bool
clang_type_is_function(CXType type)
{
	if (type.kind == CXType_Elaborated)
		type = clang_Type_getNamedType(type);

	return type.kind == CXType_FunctionProto || type.kind == CXType_FunctionNoProto;
}

struct fy_type *clang_create_single_synthetic_type(struct fy_reflection *rfl, struct fy_import *imp, CXType type);
static int clang_create_primitive_types(struct fy_reflection *rfl, struct fy_import *imp, CXType type);

static unsigned int
clang_type_get_qualifiers(CXType type)
{
	return (clang_isConstQualifiedType(type) ? FY_QUALIFIER_CONST : 0) |
	       (clang_isVolatileQualifiedType(type) ? FY_QUALIFIER_VOLATILE : 0) | 
	       (clang_isRestrictQualifiedType(type) ? FY_QUALIFIER_RESTRICT : 0);
}

static bool
clang_type_equal(CXType type1, CXType type2, bool strict)
{
	unsigned int quals1, quals2;

	if (clang_equalTypes(type1, type2))
		return true;

	quals1 = clang_type_get_qualifiers(type1);
	quals2 = clang_type_get_qualifiers(type2);

	if (type1.kind == CXType_Elaborated)
		type1 = clang_Type_getNamedType(type1);

	if (type2.kind == CXType_Elaborated)
		type2 = clang_Type_getNamedType(type2);

	if (!clang_equalTypes(type1, type2))
		return false;

	return strict ? quals1 == quals2 : true;
}

static struct fy_type *
clang_lookup_type_by_type(struct fy_reflection *rfl, CXType type, bool strict)
{
	enum fy_type_kind type_kind;
	struct fy_type *ft;
	struct clang_type_backend *ftb;

	type_kind = clang_map_primary_type(type);
	if (type_kind != FYTK_INVALID) {
		ft = fy_reflection_get_primary_type(rfl, type_kind, clang_type_get_qualifiers(type));
		if (!ft)
			return NULL;
		return ft;
	}

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {

		ftb = ft->backend;
		if (!ftb)
			continue;

		if (clang_type_equal(ftb->type, type, strict)) {
			// fprintf(stderr, "\e[32m" "%s:%d %s match %s - %s" "\e[0m" "\n",
			//		__FILE__, __LINE__, __func__,
			//		clang_str_get_alloca(clang_getTypeSpelling(type)),
			//		clang_str_get_alloca(clang_getTypeSpelling(ftb->type)));
			break;
		}
		// fprintf(stderr, "\e[34m" "%s:%d %s no match %s - %s" "\e[0m" "\n",
		//		__FILE__, __LINE__, __func__,
		//		clang_str_get_alloca(clang_getTypeSpelling(type)),
		//		clang_str_get_alloca(clang_getTypeSpelling(ftb->type)));
	}

	return ft;
}

static enum CXChildVisitResult
fy_import_backend_root_visitor(CXCursor cursor, CXCursor parent, CXClientData client_data)
{
	struct fy_import *imp = client_data;
	struct fy_reflection *rfl;
	struct clang_reflect_backend *rflb FY_DEBUG_UNUSED;
	struct clang_decl_user declu_local, *declu = &declu_local;
	enum CXCursorKind cursor_kind;
	struct fy_decl *decl = NULL, *decl_fwd = NULL;
	struct fy_decl *declt FY_DEBUG_UNUSED;
	struct fy_type *ft = NULL, *ft_fwd = NULL, *ft_curr, *ft_dep;
	enum fy_type_flags flags;
	unsigned int quals;
	const char *type_spelling;
	bool visit_children;
	enum CXChildVisitResult visit_result = CXChildVisit_Continue;
	struct clang_type_user ftu_local, *ftu = &ftu_local;
	struct clang_decl_backend *declb FY_DEBUG_UNUSED;
	enum CXCursorKind parent_kind;
	bool in_global_scope, is_fwd_decl;
	CXType type;
	enum fy_type_kind type_kind;
	enum fy_decl_type decl_type;
	const char *cursor_spelling;
	const char *type_pfx;
	size_t type_pfx_len, tlen, clen;
	char *new_type_spelling, *new_cursor_spelling, *s;
	unsigned int ret;
	int rc FY_DEBUG_UNUSED;

	assert(imp);
	rfl = imp->rfl;
	assert(rfl);

	rflb = rfl->backend;
	assert(rflb);

	cursor_kind = clang_getCursorKind(cursor);
#if 0
	{
		CXType fwd_decl_type;
		enum fy_type_kind fwd_decl_type_kind;
		CXFile cxfile;
		unsigned int line, column, offset, end_line, end_column, end_offset;
		const char *file;

		fwd_decl_type = clang_getCursorType(cursor);
		fwd_decl_type_kind = clang_map_type_kind(fwd_decl_type.kind, cursor_kind);

		clang_getFileLocation(clang_getCursorLocation(cursor), &cxfile, &line, &column, &offset);
		file = clang_str_get_alloca(clang_getFileName(cxfile));

		clang_getFileLocation(clang_getRangeEnd(clang_getCursorExtent(cursor)), &cxfile, &end_line, &end_column, &end_offset);

		fprintf(stderr, "\e[34m" "%s: %s:%d - %s: %s type='%s' cursor_display_name='%s' cursor_kind='%s' @%s:[%u:%u-%u:%u] - cursor_spelling='%s'%s%s\n" "\e[0m", __func__, __FILE__, __LINE__,
				fy_type_kind_name(fwd_decl_type_kind) ? : "<NULL>",
				clang_isCursorDefinition(cursor) ? "definition" : "forward-declaration",
				clang_str_get_alloca(clang_getTypeSpelling(fwd_decl_type)),
				clang_str_get_alloca(clang_getCursorDisplayName(cursor)),
				clang_str_get_alloca(clang_getCursorKindSpelling(cursor_kind)),
				file, line, column, end_line, end_column,
				clang_str_get_alloca(clang_getCursorSpelling(cursor)),
				clang_Cursor_isAnonymousRecordDecl(cursor) ? " anonymous-record-decl" : "",
				clang_Cursor_isAnonymous(cursor) ? " anonymous" : "");
	}
#endif

	/* we only handle those declarations */
	if (cursor_kind != CXCursor_StructDecl &&
	    cursor_kind != CXCursor_UnionDecl &&
	    cursor_kind != CXCursor_ClassDecl &&
	    cursor_kind != CXCursor_EnumDecl &&
	    cursor_kind != CXCursor_TypedefDecl &&
	    cursor_kind != CXCursor_EnumConstantDecl)
		return CXChildVisit_Continue;

	parent_kind = clang_getCursorKind(parent);

	in_global_scope = parent_kind == CXCursor_TranslationUnit;
	is_fwd_decl = !clang_isCursorDefinition(cursor);

	type = clang_getCursorType(cursor);
	RFL_ASSERT(type.kind != CXType_Invalid);

	type_kind = clang_map_type_kind(
			type.kind != CXType_Elaborated ? type.kind :
					clang_Type_getNamedType(type).kind,
			cursor_kind);
	RFL_ASSERT(type_kind != FYTK_INVALID);

	decl_type = clang_map_cursor_decl(cursor);
	RFL_ASSERT(decl_type != FYDT_NONE);

	type_spelling = clang_str_get_alloca(clang_getTypeSpelling(type));
	cursor_spelling = clang_str_get_alloca(clang_getCursorSpelling(cursor));

	/* no recursion for typedef and enum (or fwd declarations) */
	visit_children = !is_fwd_decl && cursor_kind != CXCursor_EnumDecl && cursor_kind != CXCursor_TypedefDecl;

	/* visit the children first, so that we pick up definition intermingled */
	if (visit_children) {
		ret = clang_visitChildren(cursor, fy_import_backend_root_visitor, imp);
		if (ret)
			return CXChildVisit_Break;
	}

	quals = clang_type_get_qualifiers(type);
	flags = ((quals & FY_QUALIFIER_CONST) ? FYTF_CONST : 0) | 
		((quals & FY_QUALIFIER_VOLATILE) ? FYTF_VOLATILE : 0) | 
		((quals & FY_QUALIFIER_RESTRICT) ? FYTF_RESTRICT : 0);

	if (is_fwd_decl)
		flags |= FYTF_INCOMPLETE;

	// fprintf(stderr, "\e[34m" "%s: %s:%d cursor_spelling='%s' type_spelling='%s' cursor_kind_spelling=%s" "\e[0m\n",
	//		__func__, __FILE__, __LINE__,
	//		cursor_spelling, type_spelling, clang_str_get_alloca(clang_getCursorKindSpelling(cursor_kind)));

	if (clang_Cursor_isAnonymousRecordDecl(cursor))
		flags |= FYTF_ANONYMOUS_RECORD_DECL;

	if (clang_Cursor_isAnonymous(cursor))
		flags |= FYTF_ANONYMOUS;

	/* globally anonymous? */
	if ((flags & FYTF_ANONYMOUS) && in_global_scope)
		flags |= FYTF_ANONYMOUS_GLOBAL;

	type_pfx = fy_type_kind_name(type_kind);
	type_pfx_len = strlen(type_pfx);

	/* if the cursor is anonymous, we have to do some processing removing the prefix */
	if (flags & (FYTF_ANONYMOUS | FYTF_ANONYMOUS_RECORD_DECL)) {

		/* XXX */
		// fprintf(stderr, "\e[31m" "%s: %s:%d ANON cursor_spelling='%s' type_spelling='%s'" "\e[0m\n", __func__, __FILE__, __LINE__, cursor_spelling, type_spelling);

		if (strlen(cursor_spelling) > type_pfx_len + 1 && !memcmp(cursor_spelling, type_pfx, type_pfx_len) && isspace(cursor_spelling[type_pfx_len])) {
			type_spelling = cursor_spelling;
			cursor_spelling += type_pfx_len;
			while (isspace(*cursor_spelling))
				cursor_spelling++;

			// fprintf(stderr, "\e[31m" "%s: %s:%d FIXED ANON cursor_spelling='%s' type_spelling='%s'" "\e[0m\n", __func__, __FILE__, __LINE__, cursor_spelling, type_spelling);
		}

	} else if (type_kind == FYTK_STRUCT || type_kind == FYTK_UNION || type_kind == FYTK_ENUM) {

		tlen = strlen(type_spelling);
		if (tlen < type_pfx_len + 1 || memcmp(type_pfx, type_spelling, type_pfx_len) || !isspace(type_spelling[type_pfx_len])) {

			// fprintf(stderr, "\e[31m" "%s: %s:%d typedef %s anonymous cursor_spelling='%s' type_spelling='%s'" "\e[0m\n",
			//		__func__, __FILE__, __LINE__, type_pfx, cursor_spelling, type_spelling);

			flags |= FYTF_ANONYMOUS;

			clen = strlen(cursor_spelling);
			new_cursor_spelling = alloca(1 + clen + 1);
			s = new_cursor_spelling;
			*s++ = '@';
			memcpy(s, cursor_spelling, clen + 1);
			cursor_spelling = new_cursor_spelling;
			clen++;	/* account for the extra 2 */

			new_type_spelling = alloca(type_pfx_len + 1 + clen + 1);
			s = new_type_spelling;
			memcpy(s, type_pfx, type_pfx_len);
			s += type_pfx_len;
			*s++ = ' ';
			memcpy(s, cursor_spelling, clen + 1);
			type_spelling = new_type_spelling;

			// fprintf(stderr, "\e[31m" "%s: %s:%d UPDATED typedef struct anonymous cursor_spelling='%s' type_spelling='%s'" "\e[0m\n",
			//		__func__, __FILE__, __LINE__, cursor_spelling, type_spelling);

		}
	}

	// fprintf(stderr, "%s:%d %s calling clang_lookup_type_by_type\n", __FILE__, __LINE__, __func__);
	decl = NULL;
	ft = clang_lookup_type_by_type(rfl, type, true);
	if (ft) {

		/* multiple fwd declarations, are harmless */
		if (is_fwd_decl) {
			visit_result = CXChildVisit_Continue;
			goto out;
		}

		declt = fy_type_decl(ft);
		RFL_ASSERT(declt);

		ft_fwd = ft;
		ft = NULL;

		decl_fwd = fy_type_decl(ft_fwd);
		RFL_ASSERT(decl_fwd);

		ft_curr = ft_fwd;
		// fprintf(stderr, "\e[33m" "%s: %s:%d real declaration for fwd declared type '%s'" "\e[0m\n", __func__, __FILE__, __LINE__, ft_fwd->fullname);
	}

	memset(declu, 0, sizeof(*declu));
	declu->cursor = cursor;
	declu->parent = parent;
	declu->quals = quals;
	
	memset(ftu, 0, sizeof(*ftu));
	ftu->type = type;
	ftu->dependent_type = clang_type_get_dependent(type);

	if (!decl_fwd) {
		decl = fy_decl_create(rfl, imp, NULL, decl_type, cursor_spelling, declu);
		RFL_ASSERT(decl);

		declb = decl->backend;
		RFL_ASSERT(declb);
	} else {
		rc = clang_decl_update(decl_fwd, declu);
		RFL_ASSERT(!rc);

		declb = decl_fwd->backend;
		RFL_ASSERT(declb);
	}

	ft_dep = NULL;
	if (!is_fwd_decl) {
		if (ftu->dependent_type.kind != CXType_Invalid) {
			ft_dep = clang_lookup_type_by_type(rfl, ftu->dependent_type, true);
			if (!ft_dep) {
				// fprintf(stderr, "\e[33m" "%s:%d %s: primitive types leading to %s" "\e[0m" "\n",
				//		__FILE__, __LINE__, __func__,
				//		clang_str_get_alloca(clang_getTypeSpelling(ftu->dependent_type)));

				ret = clang_create_primitive_types(rfl, imp, ftu->dependent_type);
				RFL_ASSERT(ret >= 0);

				ft_dep = clang_lookup_type_by_type(rfl, ftu->dependent_type, true);
			}
		}
	}

	if (!ft_fwd) {
		// fprintf(stderr, "\e[33m" "%s: %s:%d create %stype '%s' (%s)" "\e[0m\n",
		//		__func__, __FILE__, __LINE__,
		//		is_fwd_decl ? " fwd_decl" : "", cursor_spelling, type_spelling);

		ft = fy_type_create(rfl, type_kind, flags, type_spelling, decl, ft_dep, ftu, 0);
		RFL_ASSERT(ft);

		ft_curr = ft;
	} else {
		// fprintf(stderr, "\e[33m" "%s: %s:%d updating type '%s' (%s)" "\e[0m\n", __func__, __FILE__, __LINE__, cursor_spelling, type_spelling);

		/* clear the incomplete flag */
		fy_type_set_flags(ft_fwd, 0, FYTF_INCOMPLETE);

		rc = clang_type_update(ft_fwd, ftu);
		RFL_ASSERT(!rc);

		/* update all elaborated types with the new info */
		ret = fy_type_update_all_elaborated(ft_fwd);
		RFL_ASSERT(!ret);

		ft_curr = ft_fwd;
	}

	// fprintf(stderr, "\e[33m" "%s: %s:%d add type '%s'" "\e[0m\n", __func__, __FILE__, __LINE__, ft_curr->fullname);

	ret = clang_type_resolve(ft_curr);
	RFL_ASSERT(!ret);

	if (decl)
		fy_decl_list_add_tail(&rfl->decls, decl);
	if (ft)
		fy_type_list_add_tail(&rfl->types, ft);

	visit_result = CXChildVisit_Continue;

out:
	return visit_result;

err_out:
	if (decl)
		fy_decl_destroy(decl);
	if (ft)
		fy_type_destroy(ft);

	visit_result = CXChildVisit_Break;
	goto out;
}

static int clang_reflection_setup(struct fy_reflection *rfl)
{
	struct clang_reflect_backend *rflb;
	const struct fy_clang_backend_reflection_cfg *backend_cfg;

	assert(rfl);

	RFL_ASSERT(rfl->cfg.backend_cfg);

	rflb = malloc(sizeof(*rflb));
	RFL_ASSERT(rflb);
	memset(rflb, 0, sizeof(*rflb));

	rfl->backend = rflb;

	backend_cfg = rfl->cfg.backend_cfg;

	rflb->index = clang_createIndex(0, backend_cfg->display_diagnostics ? 1 : 0);
	RFL_ASSERT(rflb->index);

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
	CXCursor cursor;
	int argc;
	const char * const *argv;
	const char *default_argv[2];
	int len;
	unsigned num_diag, ret;

	assert(imp);

	if (!clang_cfg || !clang_cfg->file)
		return -1;

	rfl = imp->rfl;
	assert(rfl);

	rflb = rfl->backend;

	impb = malloc(sizeof(*impb));
	RFL_ASSERT(impb);
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

	RFL_ASSERT(rflb);
	RFL_ASSERT(rflb->index);

	impb->file = strdup(clang_cfg->file);
	RFL_ASSERT(impb->file);

	impb->tu = clang_createTranslationUnitFromSourceFile(rflb->index, impb->file, argc, argv, 0, NULL);
	RFL_ERROR_CHECK(impb->tu,
			"clang: clang_createTranslationUnitFromSourceFile() failed for file '%s'", impb->file);

	/* we don't want any! warnings or errors */
	num_diag = clang_getNumDiagnostics(impb->tu);
	RFL_ERROR_CHECK(num_diag == 0,
			"clang: failed due to diagnostics present");

	impb->ti = clang_getTranslationUnitTargetInfo(impb->tu);
	RFL_ASSERT(impb->ti);

	clang_str_setup(&impb->target_triple, clang_TargetInfo_getTriple(impb->ti));

	cursor = clang_getTranslationUnitCursor(impb->tu);
	ret = clang_visitChildren(cursor, fy_import_backend_root_visitor, imp);
	RFL_ASSERT(!ret);

	impb->target_triple_str = clang_str_get(&impb->target_triple);

	len = snprintf(NULL, 0, "%s-%s", impb->file, impb->target_triple_str);
	RFL_ASSERT(len > 0);

	impb->name = malloc(len + 1);
	RFL_ASSERT(impb->name);

	snprintf(impb->name, len + 1, "%s-%s", impb->file, impb->target_triple_str);

	/* forward */
	imp->name = impb->name;

	/* final tieing of loose ends */
	ret = clang_import_done(imp);
	RFL_ASSERT(!ret);

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

	clang_str_cleanup(&declb->raw_comment);

	free(declb);
}

struct fy_type *
clang_create_single_synthetic_type(struct fy_reflection *rfl, struct fy_import *imp, CXType type)
{
	struct clang_type_user ftu_local, *ftu = &ftu_local;
	struct fy_type *ft = NULL, *ft_dep;
	enum fy_type_kind type_kind;
	enum fy_type_flags flags;
	struct clang_decl_user declu_local, *declu = &declu_local;
	struct fy_decl *decl = NULL;
	enum fy_decl_type decl_type;
	unsigned int quals;
	const char *decl_spelling;
	const char *type_spelling FY_DEBUG_UNUSED;
	int ret;

	assert(rfl);

	RFL_ASSERT(imp);

	// fprintf(stderr, "%s:%d %s calling clang_lookup_type_by_type\n", __FILE__, __LINE__, __func__);
	// assert(clang_type_get_qualifiers(type) == 0);
	ft = clang_lookup_type_by_type(rfl, type, true);
	if (ft)
		return ft;

	quals = clang_type_get_qualifiers(type);

	type_kind = clang_map_type(type);
	RFL_ASSERT(fy_type_kind_is_valid(type_kind));

	memset(ftu, 0, sizeof(*ftu));
	ftu->type = type;
	ftu->dependent_type = clang_type_get_dependent(type);

	ft_dep = NULL;

	// XXX assert(ftu->dependent_type.kind != CXType_Invalid);

	if (ftu->dependent_type.kind != CXType_Invalid) {
		ft_dep = clang_lookup_type_by_type(rfl, ftu->dependent_type, true);
		// if (!ft_dep) {
		//	fprintf(stderr, "\e[31m" "%s:%d %s: no underlying type to %s (recursive definition)" "\e[0m" "\n",
		//			__FILE__, __LINE__, __func__,
		//			clang_str_get_alloca(clang_getTypeSpelling(ftu->dependent_type)));
		// }
	} else {
		decl_type = clang_map_type_decl(ftu->type);

		decl_spelling = clang_str_get_alloca(clang_getTypeSpelling(ftu->type));

		memset(declu, 0, sizeof(*declu));
		declu->cursor = clang_getNullCursor();
		declu->parent = clang_getNullCursor();
		declu->quals = quals;

		decl = fy_decl_create(rfl, imp, NULL, decl_type, decl_spelling, declu);
		RFL_ASSERT(decl);
	}

	flags = FYTF_SYNTHETIC |
		((quals & FY_QUALIFIER_CONST) ? FYTF_CONST : 0) | 
		((quals & FY_QUALIFIER_VOLATILE) ? FYTF_VOLATILE : 0) | 
		((quals & FY_QUALIFIER_RESTRICT) ? FYTF_RESTRICT : 0);

	type_spelling = clang_str_get_alloca(clang_getTypeSpelling(ftu->type));
	RFL_ASSERT(type_spelling);

	// fprintf(stderr, "\e[33m" "%s: %s:%d create type '%s'" "\e[0m\n", __func__, __FILE__, __LINE__,
	//		type_spelling);

	/* we don't pass a name for those types, it's automatically named */
	ft = fy_type_create(rfl, type_kind, flags, NULL, decl, ft_dep, ftu, 0);
	RFL_ASSERT(ft);

	// fprintf(stderr, "\e[33m" "%s: %s:%d add type '%s' (%s)" "\e[0m\n", __func__, __FILE__, __LINE__, ft->fullname, type_spelling);

	// if (!ft->fullname)
	//	fprintf(stderr, "\e[31m" "%s: %s:%d NEEDS_NAME type %s" "\e[0m\n", __func__, __FILE__, __LINE__, type_spelling);

	ret = clang_type_resolve(ft);
	RFL_ASSERT(!ret);

	if (decl)
		fy_decl_list_add_tail(&rfl->decls, decl);

	if (ft)
		fy_type_list_add_tail(&rfl->types, ft);

	return ft;

err_out:
	if (ft)
		fy_type_destroy(ft);
	if (decl)
		fy_decl_destroy(decl);

	return NULL;
}

/* given a primitive type (scalar or pointer) create all missing */

struct clang_type_stack {
	int top, alloc;
	CXType *types;
	CXType types_local[16];
};

void clang_type_stack_setup(struct clang_type_stack *ts)
{
	assert(ts);
	memset(ts, 0, sizeof(*ts));
	ts->alloc = sizeof(ts->types_local)/sizeof(ts->types_local[0]);
	ts->types = ts->types_local;
}

void clang_type_stack_cleanup(struct clang_type_stack *ts)
{
	assert(ts);

	if (ts->types && ts->types != ts->types_local)
		free(ts->types);
}

bool clang_type_stack_type_exists(struct clang_type_stack *ts, CXType type)
{
	int i;

	for (i = ts->top - 1; i >= 0; i--) {
		if (clang_type_equal(ts->types[i], type, true))
			return true;
	}
	return false;
}

int clang_type_stack_push(struct clang_type_stack *ts, CXType type)
{
	CXType *types_new;

	assert(ts);

	/* add only once */
	if (clang_type_stack_type_exists(ts, type))
		return 0;

	if (ts->top >= ts->alloc) {
		types_new = malloc(sizeof(*types_new) * ts->alloc * 2);
		if (!types_new)
			return -1;
		memcpy(types_new, ts->types, sizeof(*types_new) * ts->top);
		if (ts->types != ts->types_local)
			free(ts->types);
		ts->types = types_new;
		ts->alloc *= 2;
	}

	assert(ts->top < ts->alloc);

	// fprintf(stderr, "%s: %s:%d >>> Pushing #%d %s:%s\n", __func__, __FILE__, __LINE__, ts->top,
	//	clang_str_get_alloca(clang_getTypeKindSpelling(type.kind)),
	//	clang_str_get_alloca(clang_getTypeSpelling(type)));

	ts->types[ts->top++] = type;
	return 0;
}

CXType clang_type_stack_pop(struct clang_type_stack *ts)
{
	CXType ttype;

	if (ts->top <= 0) {
		memset(&ttype, 0, sizeof(ttype));
		ttype.kind = CXType_Invalid;
		return ttype;
	}
	return ts->types[--ts->top];
}

static struct fy_type *
clang_lookup_qualified_type(struct fy_reflection *rfl, CXType type, bool resolve_new)
{
	struct clang_type_user ftu_local, *ftu = &ftu_local;
	struct fy_type *ft, *ft_q = NULL;
	unsigned int quals;

	/* try strict match first */
	ft = clang_lookup_type_by_type(rfl, type, true);
	if (ft)
		return ft;

	/* failed? try loose match */
	ft = clang_lookup_type_by_type(rfl, type, false);
	if (!ft)
		return NULL;

	quals = clang_type_get_qualifiers(type);

	/* first try to get the qualifier'ed type (if it exists) */
	ft_q = fy_type_with_qualifiers(ft, quals);
	if (ft_q)
		return ft_q;

	memset(ftu, 0, sizeof(*ftu));
	ftu->type = type;
	ftu->dependent_type.kind = CXType_Invalid;

	ft_q = fy_type_create_with_qualifiers(ft, quals, ftu);
	RFL_ASSERT(ft_q);

	fy_type_list_add_tail(&rfl->types, ft_q);

	// fprintf(stderr, "\e[33m" "%s:%d %s created qualified type for %s (%s)" "\e[0m" "\n",
	//		__FILE__, __LINE__, __func__,
	//		ft_q->fullname,
	//		clang_str_get_alloca(clang_getTypeSpelling(type)));

	if (resolve_new)
		(void)clang_type_resolve(ft);

	return ft_q;

err_out:
	if (ft_q)
		fy_type_destroy(ft_q);
	return NULL;
}

static int
clang_create_primitive_types(struct fy_reflection *rfl, struct fy_import *imp, CXType type)
{
	struct fy_type *ft;
	CXType ttype;
	int created_count;
	struct clang_type_stack ts;

	/* nothing to do */
	if (type.kind == CXType_Invalid)
		return 0;

	ft = clang_lookup_type_by_type(rfl, type, true);
	if (ft)
		return 0;

	created_count = 0;
	clang_type_stack_setup(&ts);

	/* collect all the types that are pointer like primitives */
	while (clang_type_is_ptr_or_array(type)) {

		clang_type_stack_push(&ts, type);
		ttype = clang_type_get_pointee_or_element(type);
		if (ttype.kind == CXType_Invalid)
			break;

		type = ttype;
	}

	/* append final type if it's a primitive (or a function) */
	if (clang_type_is_primitive(type) || clang_type_is_function(type))
		clang_type_stack_push(&ts, type);
	else {
		ft = clang_lookup_qualified_type(rfl, type, true);
		(void)ft;
	}

	while ((type = clang_type_stack_pop(&ts)).kind != CXType_Invalid) {

		// fprintf(stderr, "\e[34m" "%s:%d %s create single primitive type '%s'" "\e[0m" "\n",
		//		__FILE__, __LINE__, __func__,
		//		clang_str_get_alloca(clang_getTypeSpelling(type)));

		ft = clang_lookup_type_by_type(rfl, type, true);
		if (ft)
			continue;

		ft = clang_create_single_synthetic_type(rfl, imp, type);
		RFL_ASSERT(ft);

		created_count++;
	}

out:
	clang_type_stack_cleanup(&ts);
	return created_count;

err_out:
	created_count = -1;
	goto out;
}

static int
clang_field_visitor(CXCursor cursor, CXCursor parent, struct fy_decl *parent_decl)
{
	int res = 0;
	struct fy_import *imp;
	struct fy_reflection *rfl;
	struct fy_decl *decl = NULL;
	struct clang_decl_user declu_local, *declu = &declu_local;
	struct fy_type *ft = NULL;
	enum fy_decl_type decl_type;
	bool is_bitfield;
	unsigned int quals;
	CXType type;
	int ret;
	const char *cursor_spelling;

	assert(parent_decl);

	type = clang_getCursorType(cursor);
	quals = clang_type_get_qualifiers(type);

	cursor_spelling = clang_str_get_alloca(clang_getCursorSpelling(cursor));

	memset(declu, 0, sizeof(*declu));
	declu->cursor = cursor;
	declu->parent = parent;
	declu->quals = quals;

	imp = parent_decl->imp;
	rfl = parent_decl->rfl;

	// fprintf(stderr, "\e[32m" "%s: %s:%d FIELD cursor_spelling='%s'" "\e[0m\n", __func__, __FILE__, __LINE__, cursor_spelling);

	if (parent_decl->decl_type == FYDT_STRUCT || parent_decl->decl_type == FYDT_UNION) {
		is_bitfield = clang_Cursor_isBitField(cursor);
		decl_type = !is_bitfield ? FYDT_FIELD : FYDT_BITFIELD;
	} else if (parent_decl->decl_type == FYDT_ENUM) {
		decl_type = FYDT_ENUM_VALUE;
	} else
		RFL_ERROR_CHECK(false,
				"clang: can't visit");

	ft = clang_lookup_type_by_type(rfl, type, true);
	if (!ft) {
		// fprintf(stderr, "\e[33m" "%s:%d %s: primitive types leading to %s" "\e[0m" "\n",
		//		__FILE__, __LINE__, __func__,
		//		clang_str_get_alloca(clang_getTypeSpelling(type)));

		ret = clang_create_primitive_types(rfl, imp, type);
		RFL_ASSERT(ret >= 0);

		/* lookup (or create qualified type) */
		ft = clang_lookup_type_by_type(rfl, type, true);
		RFL_ASSERT(ft);
	}

	/* create field declaration */
	decl = fy_decl_create(rfl, imp, parent_decl, decl_type, cursor_spelling, declu);
	RFL_ASSERT(decl);

	decl->type = ft;

	/* add to the parent */
	fy_decl_list_add_tail(&parent_decl->children, decl);

	res = 0;
out:
	return res;

err_out:
	fy_decl_destroy(decl);
	res = -1;
	goto out;
}

static enum CXVisitorResult
fy_import_backend_struct_field_visitor(CXCursor cursor, CXClientData client_data)
{
	struct fy_decl *parent_decl;
	struct fy_import *imp;
	struct clang_import_backend *impb;
	CXCursor parent;
	int ret;

	parent_decl = client_data;
	assert(parent_decl);
	imp = parent_decl->imp;
	assert(imp);
	impb = imp->backend;
	assert(impb);

	impb->field_visit_error = false;

	parent = clang_getCursorSemanticParent(cursor);
	ret = clang_field_visitor(cursor, parent, client_data);
	if (!ret)
		return CXVisit_Continue;

	impb->field_visit_error = true;

	return CXVisit_Break;
}

static enum CXChildVisitResult
fy_import_backend_enum_visitor(CXCursor cursor, CXCursor parent, CXClientData client_data)
{
	return clang_field_visitor(cursor, parent, client_data) == 0 ?
	       CXChildVisit_Continue : CXChildVisit_Break; 
}

static int
clang_setup_source_range(struct fy_decl *decl)
{
	struct fy_reflection *rfl;
	struct fy_import *imp;
	struct clang_decl_backend *declb;
	struct fy_source_file *source_file;
	CXFile cxfile_start, cxfile_end;
	const char *file_start, *filename;
	const char *file_end FY_DEBUG_UNUSED;
	struct fy_source_range *range;
	unsigned int line, column, offset;

	assert(decl && decl->backend);

	imp = decl->imp;
	assert(imp);

	rfl = imp->rfl;
	assert(rfl);

	declb = decl->backend;

	range = &declb->source_range;
	memset(range, 0, sizeof(*range));

	clang_getFileLocation(declb->start_location, &cxfile_start, &line, &column, &offset);
	range->start_line = line;
	range->start_column = column;
	range->start_offset = offset;

	clang_getFileLocation(declb->end_location, &cxfile_end, &line, &column, &offset);
	range->end_line = line;
	range->end_column = column;
	range->end_offset = offset;

	file_start = clang_str_get_alloca(clang_getFileName(cxfile_start));
	file_end = clang_str_get_alloca(clang_getFileName(cxfile_end));

	/* the start location and end location files must be the same */
	RFL_ASSERT(!strcmp(file_start, file_end));

	filename = file_start;

	source_file = fy_reflection_lookup_source_file(rfl, filename);
	if (!source_file) {
		source_file = fy_source_file_create(rfl, filename);
		RFL_ASSERT(source_file);

		source_file->filetime = clang_getFileTime(cxfile_start);

		source_file->system_header = !!(decl->flags & FYDF_IN_SYSTEM_HEADER);
		source_file->main_file = !!(decl->flags & FYDF_FROM_MAIN_FILE);

		fy_source_file_list_add_tail(&rfl->source_files, source_file);
	}
	range->source_file = source_file;

	/* point the exposed decl source range */
	decl->source_range = &declb->source_range;

	return 0;

err_out:
	return -1;
}

static int clang_decl_update(struct fy_decl *decl, const struct clang_decl_user *declu)
{
	struct fy_reflection *rfl;
	struct clang_reflect_backend *rflb FY_DEBUG_UNUSED;
	struct fy_import *imp;
	struct clang_import_backend *impb;
	struct clang_decl_backend *declb;
	const char *raw_comment;
	int signess;
	unsigned int visit_res;
	long long size;
	bool null_cursor;
	int ret;

	assert(decl);
	if (!declu)
		return 0;

	assert(decl->backend);

	rfl = decl->rfl;
	assert(rfl);

	rflb = rfl->backend;
	assert(rflb);
	imp = decl->imp;
	assert(imp);
	impb = imp->backend;
	assert(impb);

	declb = decl->backend;
	assert(declb->decl == decl);

	null_cursor = clang_Cursor_isNull(declu->cursor);
	if (!null_cursor && !clang_isCursorDefinition(declu->cursor))
		return 0;

	declb->extent = clang_getCursorExtent(declu->cursor);
	declb->start_location = clang_getRangeStart(declb->extent);
	declb->end_location = clang_getRangeEnd(declb->extent);

	declb->type = clang_getCursorType(declu->cursor);

	declb->comment = clang_Cursor_getParsedComment(declu->cursor);

	declb->quals = declu->quals;

	/* check if it is a declaration of an incomplete type */
	size = clang_Type_getSizeOf(declb->type);
	declb->is_invalid = size < 0;
	declb->is_incomplete = size == CXTypeLayoutError_Incomplete;

	if (clang_Location_isInSystemHeader(declb->start_location))
		decl->flags |= FYDF_IN_SYSTEM_HEADER;
	if (clang_Location_isFromMainFile(declb->start_location))
		decl->flags |= FYDF_FROM_MAIN_FILE;

	/* must be after the backend assignment */
	if (!declb->is_invalid) {
		ret = clang_setup_source_range(decl);
		RFL_ASSERT(!ret);
	}

	switch (decl->decl_type) {
	case FYDT_TYPEDEF:
		declb->typedef_info.underlying.type = clang_getTypedefDeclUnderlyingType(declu->cursor);
		break;

	case FYDT_STRUCT:
	case FYDT_UNION:
		if (!declb->is_invalid) {
			/* documentation of the header is bogus, returns true on success, false on error */
			visit_res = clang_Type_visitFields(declb->type, fy_import_backend_struct_field_visitor, decl);
			RFL_ASSERT(visit_res && !impb->field_visit_error);
		}
		break;

	case FYDT_ENUM:
		declb->enum_info.inttype.type = clang_getEnumDeclIntegerType(declu->cursor);

		/* XXX no concrete type for the enum, only storage size */
		decl->enum_decl.type_kind = clang_map_type_kind(declb->enum_info.inttype.type.kind, CXCursor_EnumDecl);
		RFL_ASSERT(fy_type_kind_is_enum_constant_decl(decl->enum_decl.type_kind));

		/* there is no field for enums, so visit children */
		clang_visitChildren(declu->cursor, fy_import_backend_enum_visitor, decl);
		break;

	case FYDT_ENUM_VALUE:
		RFL_ASSERT(decl);
		RFL_ASSERT(decl->parent);
		RFL_ASSERT(decl->parent->decl_type == FYDT_ENUM);

		decl->enum_value_decl.type_kind = clang_map_type_kind(declb->type.kind, CXCursor_EnumConstantDecl);
		RFL_ASSERT(fy_type_kind_is_enum_constant_decl(decl->enum_value_decl.type_kind));

		signess = clang_type_kind_signess(declb->type.kind);
		RFL_ASSERT(signess != 0);
		if (signess > 0)
			decl->enum_value_decl.val.u = clang_getEnumConstantDeclUnsignedValue(declu->cursor);
		else
			decl->enum_value_decl.val.s = clang_getEnumConstantDeclValue(declu->cursor);
		break;

	case FYDT_FIELD:
		RFL_ASSERT(decl->parent->decl_type == FYDT_STRUCT || decl->parent->decl_type == FYDT_UNION);
		decl->field_decl.byte_offset = clang_Cursor_getOffsetOfField(declu->cursor) / 8;
		break;

	case FYDT_BITFIELD:
		RFL_ASSERT(decl->parent->decl_type == FYDT_STRUCT || decl->parent->decl_type == FYDT_UNION);
		decl->bitfield_decl.bit_offset = clang_Cursor_getOffsetOfField(declu->cursor);
		decl->bitfield_decl.bit_width = clang_getFieldDeclBitWidth(declu->cursor);
		break;

	case FYDT_FUNCTION:
		break;

	default:
		break;
	}

	clang_str_cleanup(&declb->raw_comment);
	clang_str_setup(&declb->raw_comment, clang_Cursor_getRawCommentText(declu->cursor));
	raw_comment = clang_str_get(&declb->raw_comment);
	if (raw_comment && strlen(raw_comment) > 0)
		decl->raw_comment = raw_comment;

	return 0;

err_out:
	return -1;
}

static int clang_decl_setup(struct fy_decl *decl, void *user)
{
	struct fy_reflection *rfl;
	const struct clang_decl_user *declu = user;
	struct clang_decl_backend *declb;
	int rc;

	if (!decl || !declu)
		return 0;

	rfl = decl->rfl;
	assert(rfl);

	declb = malloc(sizeof(*declb));
	RFL_ASSERT(declb);

	memset(declb, 0, sizeof(*declb));

	decl->backend = declb;
	declb->decl = decl;

	rc = clang_decl_update(decl, declu);
	RFL_ASSERT(!rc);

	return 0;

err_out:
	clang_decl_cleanup(decl);
	return -1;
}

static int clang_type_update(struct fy_type *ft, const struct clang_type_user *ftu)
{
	struct fy_reflection *rfl;
	struct clang_type_backend *ftb;
	CXType ttype, final_ttype;
	struct fy_type *ftt;
	long long llret;
	unsigned int i;
	int ret = 0;

	assert(ft);
	rfl = ft->rfl;
	assert(rfl);

	if (!ftu)
		return 0;

	RFL_ASSERT(ft->backend);
	ftb = ft->backend;

	ftb->type = ftu->type;
	ftb->dependent_type = ftu->dependent_type;

	// fprintf(stderr, "%s: backend_name='%s'\n", __func__, ft->backend_name);

	/* if we only have a fwd declaration, we're incomplete */
	if (ft->flags & FYTF_INCOMPLETE) {
		// fprintf(stderr, "%s: backend_name='%s' FWD_DECL exit\n", __func__, ft->backend_name);
		return 0;
	}

	llret = clang_Type_getSizeOf(ftb->type);

	/* instantiating an incomplete type (i.e. deref of unresolved ptr) */
	if (llret == CXTypeLayoutError_Incomplete)
		return 0;

	/* not incomplete anymore */
	ft->flags &= ~FYTF_INCOMPLETE;
	/* if any qualified types exists, clear their incomplete bit */
	for (i = 0; i < ARRAY_SIZE(ft->qualified_types); i++) {
		ftt = ft->qualified_types[i];
		if (ftt)
			ftt->flags &= ~FYTF_INCOMPLETE;
	}

	RFL_ASSERT(llret >= 0);

	ft->size = (size_t)llret;

	llret = clang_Type_getAlignOf(ftb->type);
	RFL_ASSERT(llret >= 0);

	ft->align = (size_t)llret;

	if (ft->type_kind == FYTK_CONSTARRAY)
		ft->element_count = clang_getNumElements(ftb->type);	/* err, int should be enough for element counts */

	/* no dependency, or dependency satisfied already */
	if (!fy_type_kind_is_dependent(ft->type_kind) || ft->dependent_type)
		goto out;

	/* a dependent type must be there */
	if (ftu->dependent_type.kind == CXType_Invalid) {
		/* this happens when trying to instantiate builtin types */
		// fprintf(stderr, "\e[31m" "%s:%d %s: type %s CANNOT BE RESOLVED" "\e[0m" "\n",
		//		__FILE__, __LINE__, __func__,
		//		clang_str_get_alloca(clang_getTypeSpelling(ftb->type)));
		goto out;
	}

	/* walk until we find the final dependent type */
	final_ttype = ftb->dependent_type;
	while (clang_type_is_ptr_or_array(final_ttype)) {
		ttype = clang_type_get_pointee_or_element(final_ttype);
		if (ttype.kind == CXType_Invalid)
			break;
		final_ttype = ttype;
	}
	ftb->final_dependent_type = final_ttype;

	ftt = clang_lookup_type_by_type(rfl, ftb->dependent_type, true);

	// fprintf(stderr, "\e[35m" "%s:%d %s: type %s RESOLVED strict dependent type %s (final %s) -> %s" "\e[0m" "\n",
	//		__FILE__, __LINE__, __func__,
	//		clang_str_get_alloca(clang_getTypeSpelling(ftb->type)),
	//		clang_str_get_alloca(clang_getTypeSpelling(ftb->dependent_type)),
	//		clang_str_get_alloca(clang_getTypeSpelling(ftb->final_dependent_type)),
	//		ftt ? ftt->fullname : "<NULL>");

	// fprintf(stderr, "%s:%d %s: ftb %s ftb->dependent_type %s\n", __FILE__, __LINE__, __func__,
	//		ftb->normalized_name, ttype->normalized_name);
	//
	if (!ftt) {
		ftt = clang_lookup_type_by_type(rfl, ftb->dependent_type, false);
		// if (ftt) {
		//	fprintf(stderr, "\e[32m" "%s:%d %s: type %s RESOLVED loose dependent type %s (final %s)" "\e[0m" "\n",
		//			__FILE__, __LINE__, __func__,
		//			clang_str_get_alloca(clang_getTypeSpelling(ftb->type)),
		//			clang_str_get_alloca(clang_getTypeSpelling(ftb->dependent_type)),
		//			clang_str_get_alloca(clang_getTypeSpelling(ftb->final_dependent_type)));
		// } else {
		//	fprintf(stderr, "\e[32m" "%s:%d %s: cannot lookup type %s dependent type %s (final %s)" "\e[0m" "\n",
		//			__FILE__, __LINE__, __func__,
		//			clang_str_get_alloca(clang_getTypeSpelling(ftb->type)),
		//			clang_str_get_alloca(clang_getTypeSpelling(ftb->dependent_type)),
		//			clang_str_get_alloca(clang_getTypeSpelling(ftb->final_dependent_type)));
		// }
	}

	if (ftt) {
		ret = fy_type_set_dependent(ft, ftt);
		RFL_ASSERT(!ret);

		// fprintf(stderr, "\e[32m" "%s:%d %s: type %s RESOLVED dependent type %s (final %s)" "\e[0m" "\n",
		//		__FILE__, __LINE__, __func__,
		//		clang_str_get_alloca(clang_getTypeSpelling(ftb->type)),
		//		clang_str_get_alloca(clang_getTypeSpelling(ftb->dependent_type)),
		//		clang_str_get_alloca(clang_getTypeSpelling(ftb->final_dependent_type)));

	} else if (!(ft->flags & FYTF_UNRESOLVED)) {

		ft->flags |= FYTF_UNRESOLVED;

		// fprintf(stderr, "\e[31m" "%s:%d %s: type %s UNRESOLVED dependent type %s (final %s)" "\e[0m" "\n",
		//		__FILE__, __LINE__, __func__,
		//		clang_str_get_alloca(clang_getTypeSpelling(ftb->type)),
		//		clang_str_get_alloca(clang_getTypeSpelling(ftb->dependent_type)),
		//		clang_str_get_alloca(clang_getTypeSpelling(ftb->final_dependent_type)));

		// clang_dump_types(rfl);

		ret = fy_type_register_unresolved(ft);
		RFL_ASSERT(!ret);

	} else {
		// fprintf(stderr, "\e[31m" "%s:%d %s: type %s TYPE STAYS UNRESOLVED dependent type %s (final %s)" "\e[0m" "\n",
		//		__FILE__, __LINE__, __func__,
		//		clang_str_get_alloca(clang_getTypeSpelling(ftb->type)),
		//		clang_str_get_alloca(clang_getTypeSpelling(ftb->dependent_type)),
		//		clang_str_get_alloca(clang_getTypeSpelling(ftb->final_dependent_type)));
	}

	ret = 0;
out:
	return ret;

err_out:
	clang_type_cleanup(ft);
	ret = -1;
	goto out;
}

static int clang_type_setup(struct fy_type *ft, void *user)
{
	struct fy_reflection *rfl;
	const struct clang_type_user *ftu = user;
	struct clang_type_backend *ftb;
	int rc;

	/* primitive type */
	if (!ftu)
		return 0;

	assert(ft);
	rfl = ft->rfl;
	assert(rfl);

	ftb = malloc(sizeof(*ftb));
	RFL_ASSERT(ftb);

	memset(ftb, 0, sizeof(*ftb));

	ft->backend = ftb;

	rc = clang_type_update(ft, ftu);
	RFL_ASSERT(!rc);

	return 0;

err_out:
	clang_type_cleanup(ft);
	return -1;
}

static void clang_type_cleanup(struct fy_type *ft)
{
	struct clang_type_backend *ftb;

	if (!ft || !ft->backend)
		return;

	ftb = ft->backend;
	ft->backend = NULL;

	free(ftb);
}

static int clang_type_resolve(struct fy_type *ft_new)
{
	struct clang_type_backend *ftb_new, *ftb;
	struct fy_unresolved_dep *udep, *udepn;
	struct fy_type *ft, *ft_resolved, *ft_q;
	struct fy_reflection *rfl;
	int ret, resolutions, pass;
	bool strict_match;
	unsigned int resolved_quals;
	struct clang_type_user ftu_local, *ftu = &ftu_local;
	
	assert(ft_new);
	rfl = ft_new->rfl;
	assert(rfl);

	ftb_new = ft_new->backend;
	if (!ftb_new)
		return 0;

again:
	/* two passes, one strict, one loose */
	resolutions = 0;
	for (pass = 0; pass < 2; pass++) {

		strict_match = pass == 0;

		for (udep = fy_unresolved_dep_list_head(&rfl->unresolved_deps);
		     udep != NULL;
		     udep = udepn) {

			udepn = fy_unresolved_dep_next(&rfl->unresolved_deps, udep);

			ft = fy_type_from_info_wrapper(udep->tiw);

			RFL_ASSERT(!ft->tiw.type_info.dependent_type);

			ftb = ft->backend;
			if (!ftb)
				continue;

			// fprintf(stderr, "\e[31m" "%s:%d %s resolve compare %s'%s' <=> %s'%s' (%s)" "\e[0m" "\n",
			//		__FILE__, __LINE__, __func__,
			//		ftb_new->type.kind == CXType_Elaborated ? "E" : "", clang_str_get_alloca(clang_getTypeSpelling(ftb_new->type)),
			//		ftb->dependent_type.kind == CXType_Elaborated ? "E" : "", clang_str_get_alloca(clang_getTypeSpelling(ftb->dependent_type)),
			//		clang_str_get_alloca(clang_getTypeSpelling(ftb->type)));

			if (!clang_type_equal(ftb_new->type, ftb->dependent_type, strict_match))
				continue;

			if (strict_match) {
				ft_resolved = ft_new;
			} else {
				resolved_quals = clang_type_get_qualifiers(ftb->dependent_type);

				/* first try to get the qualifier'ed type */
				ft_q = fy_type_with_qualifiers(ft_new, resolved_quals);
				if (ft_q) {
					// fprintf(stderr, "\e[32m" "%s:%d %s existing qtype resolved %s -> %s (%s)" "\e[0m" "\n",
					//		__FILE__, __LINE__, __func__,
					//		ft->fullname, ft_q->fullname,
					//		clang_str_get_alloca(clang_getTypeSpelling(ftb->dependent_type)));
				} else {
					// fprintf(stderr, "\e[33m" "%s:%d %s no qtype resolved for %s (%s)" "\e[0m" "\n",
					//		__FILE__, __LINE__, __func__,
					//		ft->fullname,
					//		clang_str_get_alloca(clang_getTypeSpelling(ftb->dependent_type)));

					memset(ftu, 0, sizeof(*ftu));
					ftu->type = ftb->dependent_type;
					ftu->dependent_type.kind = CXType_Invalid;

					ft_q = fy_type_create_with_qualifiers(ft_new, resolved_quals, ftu);
					RFL_ASSERT(ft_q);

					fy_type_list_add_tail(&rfl->types, ft_q);

					udepn = NULL;	/* force rescan */
				}

				ft_resolved = ft_q;
			}

			/* the udep is destroyed here */
			ret = fy_type_set_dependent(ft, ft_resolved);
			RFL_ASSERT(!ret);

			ret = fy_type_generate_name(ft);
			RFL_ASSERT(!ret);

			// fprintf(stderr, "\e[32m" "%s:%d %s %s resolved %s -> %s" "\e[0m" "\n",
			//		__FILE__, __LINE__, __func__,
			//		strict_match ? "strictly" : "loosely",
			//		ft->fullname, ft_resolved->fullname);

			resolutions++;
		}
	}

	/* while there was at least one resolution, try again */
	if (resolutions)
		goto again;

	return 0;

err_out:
	return -1;
}

static int clang_import_done(struct fy_import *imp)
{
	struct fy_reflection *rfl;
	struct fy_unresolved_dep *udep;
	struct fy_type_info_wrapper *tiwn;
	struct fy_type *ft, *ft_final, *ft_dep;
	struct clang_type_backend *ftb;
	struct clang_decl_backend *declb;
	char *final_type_spell, *final_type_name = NULL;
	const char *tmp_type;
	const char *type_rem FY_DEBUG_UNUSED;
	size_t tmp_type_len;
	unsigned int tmp_quals;
	enum fy_type_kind dep_type_kind, final_type_kind, tmp_type_kind;
	enum fy_decl_type final_decl_type;
	struct fy_decl *decl, *final_decl;
	struct clang_type_user ftu_local, *ftu = &ftu_local;
	struct clang_decl_user declu_local, *declu = &declu_local;
	bool unresolved;
	int ret;

	rfl = imp->rfl;
	assert(rfl);
	
	/* go over all unresolved pointer deps, and resolve all to void */

	/* XXX TODO this is where a bolt on type system would resolve user type */

	unresolved = false;
	while ((udep = fy_unresolved_dep_list_pop(&rfl->unresolved_deps)) != NULL) {

		tiwn = udep->tiw;
		fy_unresolved_dep_destroy(udep);

		ft = fy_type_from_info_wrapper(tiwn);
		RFL_ASSERT(ft);
		RFL_ASSERT(!ft->dependent_type);

		ftb = ft->backend;
		RFL_ASSERT(ftb);

		decl = fy_type_decl(ft);
		RFL_ASSERT(decl);

		declb = decl->backend;
		RFL_ASSERT(declb);

		// fprintf(stderr, "\e[31m%s: PENDING ft->dependent_type=%s", __func__,
		//		ft->dependent_type ? ft->dependent_type->fullname : "<NULL>");
		// fprintf(stderr, " ftb=%s:%s dependent_type=%s:%s final_dependent_type=%s:%s\e[0m\n",
		//		clang_str_get_alloca(clang_getTypeKindSpelling(ftb->type.kind)), clang_str_get_alloca(clang_getTypeSpelling(ftb->type)),
		//		clang_str_get_alloca(clang_getTypeKindSpelling(ftb->dependent_type.kind)), clang_str_get_alloca(clang_getTypeSpelling(ftb->dependent_type)),
		//		clang_str_get_alloca(clang_getTypeKindSpelling(ftb->final_dependent_type.kind)), clang_str_get_alloca(clang_getTypeSpelling(ftb->final_dependent_type)));


		final_type_kind = clang_map_type(ftb->final_dependent_type);

		final_type_kind = clang_map_type(ftb->final_dependent_type);
		if (fy_type_kind_is_valid(final_type_kind)) {
			// fprintf(stderr, " final_type_kind=%s\n", fy_type_kind_info_get_internal(final_type_kind)->name);

			final_decl_type = clang_map_type_decl(ftb->final_dependent_type);
			RFL_ASSERT(fy_decl_type_is_valid(final_decl_type));

			if (final_type_kind == FYTK_TYPEDEF)
				final_decl_type = FYDT_TYPEDEF;
			// fprintf(stderr, " final_decl_type=%s\n", decl_type_txt[final_decl_type]);

			memset(declu, 0, sizeof(*declu));
			declu->cursor = clang_getNullCursor();
			declu->parent = clang_getNullCursor();

			declu->quals = clang_type_get_qualifiers(ftb->final_dependent_type);

			final_type_spell = clang_str_get_malloc(clang_getTypeSpelling(ftb->final_dependent_type));
			RFL_ASSERT(final_type_spell);

			// fprintf(stderr, "%s: final_type_spell='%s'\n", __func__, final_type_spell);

			type_rem = fy_parse_c_base_type(final_type_spell, (size_t)-1, &tmp_type_kind,
					&tmp_type, &tmp_type_len, &tmp_quals);
			RFL_ASSERT(type_rem && *type_rem == '\0');

			final_type_name = malloc(tmp_type_len + 1);
			RFL_ASSERT(final_type_name);

			memcpy(final_type_name, tmp_type, tmp_type_len);
			final_type_name[tmp_type_len] = '\0';

			final_decl = fy_decl_create(rfl, decl->imp, NULL, final_decl_type, final_type_name, declu);
			RFL_ASSERT(final_decl);

			memset(ftu, 0, sizeof(*ftu));
			ftu->type = ftb->final_dependent_type;
			ftu->dependent_type.kind = CXType_Invalid;

			// fprintf(stderr, "\e[33m" "%s: %s:%d create type '%s' (%s)" "\e[0m\n", __func__, __FILE__, __LINE__,
			//		final_type_name, clang_str_get_alloca(clang_getTypeSpelling(ftu->type)));

			ft_final = fy_type_create(rfl, final_type_kind, FYTF_SYNTHETIC | FYTF_FAKE_RESOLVED, final_type_name, final_decl, NULL, ftu, 0);
			RFL_ASSERT(ft_final);

			free(final_type_spell);
			final_type_spell = NULL;

			free(final_type_name);
			final_type_name = NULL;

			// fprintf(stderr, "\e[33m" "%s: %s:%d add type '%s'" "\e[0m\n", __func__, __FILE__, __LINE__, ft->fullname);

			ret = clang_type_resolve(ft_final);
			RFL_ASSERT(!ret);

			fy_decl_list_add_tail(&rfl->decls, final_decl);
			fy_type_list_add_tail(&rfl->types, ft_final);

		}

		dep_type_kind = clang_map_type(ftb->dependent_type);
		if (fy_type_kind_is_valid(dep_type_kind)) {

			/* we don't care about qualifiers now */
			ft_dep = clang_lookup_type_by_type(rfl, ftb->dependent_type, false);
			if (!ft_dep) {
				// fprintf(stderr, "\e[33m" "%s:%d %s: primitive types leading to %s" "\e[0m" "\n",
				//		__FILE__, __LINE__, __func__,
				//		clang_str_get_alloca(clang_getTypeSpelling(ftb->dependent_type)));

				ret = clang_create_primitive_types(rfl, decl->imp, ftb->dependent_type);
				RFL_ASSERT(ret >= 0);

				ft_dep = clang_lookup_type_by_type(rfl, ftb->dependent_type, false);
				RFL_ASSERT(ft_dep);
			}

		} else {

			// fprintf(stderr, "dependent_type is invalid (means that it's unresolved) pointing to void\n");

			// fprintf(stderr, "  declb->type: %s:%s\n",
			//		clang_str_get_alloca(clang_getTypeKindSpelling(declb->type.kind)),
			//		clang_str_get_alloca(clang_getTypeSpelling(declb->type)));

			// fprintf(stderr, "  ftb->type: %s:%s\n",
			//		clang_str_get_alloca(clang_getTypeKindSpelling(ftb->type.kind)),
			//		clang_str_get_alloca(clang_getTypeSpelling(ftb->type)));

			// fprintf(stderr, "  ftb->dependent_type: %s:%s\n",
			//		clang_str_get_alloca(clang_getTypeKindSpelling(ftb->dependent_type.kind)),
			//		clang_str_get_alloca(clang_getTypeSpelling(ftb->dependent_type)));

			ft->flags |= FYTF_FAKE_RESOLVED;
			ft->flags &= ~FYTF_ANONYMOUS;

			if (strstr(ft->fullname, "__builtin")) {
				RFL_ASSERT(ft->decl);
				ft->decl->flags |= FYDF_IN_SYSTEM_HEADER;
			}

			ft_dep = fy_reflection_get_primary_type(rfl, FYTK_VOID, clang_type_get_qualifiers(ftb->type));
			RFL_ASSERT(ft_dep);
		}

		ret = fy_type_set_dependent(ft, ft_dep);
		RFL_ASSERT(!ret);

		ret = fy_type_generate_name(ft);
		RFL_ASSERT(!ret);
	}

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {

		if (ft->flags & (FYTF_INCOMPLETE | FYTF_NEEDS_NAME)) {
			ret = fy_type_generate_name(ft);
			RFL_ASSERT(!ret);
		}
	}

	fy_reflection_renumber(rfl);

	return !unresolved ? 0 : -1;

err_out:
	return -1;
}

void clang_dump_types(struct fy_reflection *rfl)
{
	struct fy_type *ft;

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {
		fprintf(stderr, "\e[3%dm%s: '%s'%s%s" "\e[0m\n",
				(ft->flags & (FYTF_UNRESOLVED | FYTF_FAKE_RESOLVED)) ? 1 : 2,
				__func__,
				ft->fullname,
				((ft->flags & FYTF_UNRESOLVED) || ft->dependent_type) ? " -> " : "",
				ft->dependent_type ? ft->dependent_type->fullname : "");
	}
}
