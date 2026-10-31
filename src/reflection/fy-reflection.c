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
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <float.h>
#include <assert.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>

#if HAVE_ALLOCA
#include <alloca.h>
#endif

#include "fy-utf8.h"
#include "fy-ctype.h"
#include "fy-reflection-private.h"
#include "fy-reflection-util.h"

#include "cparse-private.h"

static struct fy_document *get_yaml_document(const char *cooked_comment);

static inline bool is_c_identifier(char c, bool first)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
		(!first && c >= '0' && c <= '9');
}

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
	return decl->rfl->cfg.backend->ops->decl_setup(decl, user);
}

static inline void backend_decl_cleanup(struct fy_decl *decl)
{
	decl->rfl->cfg.backend->ops->decl_cleanup(decl);
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

	[FYDT_PRIMITIVE] = {
		.type = FYDT_PRIMITIVE,
		.name = "primitive",
		.enum_name = "PRIMITIVE",
	},

	[FYDT_PRIMARY] = {
		.type = FYDT_PRIMARY,
		.name = "primary",
		.enum_name = "primary",
	},
};

#undef PRIM_TABLE_ENTRY
#define PRIM_TABLE_ENTRY(_kind, _type) \
	[(_kind) - FYTK_PRIMARY_FIRST] = { \
		.kind = (_kind), \
		.name = #_type, \
		.size = sizeof(_type), \
		.align = _Alignof(_type), \
	}

const struct fy_type_info fy_type_info_primitive_table[FYTK_PRIMARY_COUNT] = {
	[FYTK_VOID - FYTK_PRIMARY_FIRST] = {
		.kind = FYTK_VOID,
		.name = "void",
		.size = 0,
		.align = 0,
	},
	PRIM_TABLE_ENTRY(FYTK_BOOL, _Bool),
	PRIM_TABLE_ENTRY(FYTK_CHAR, char),
	PRIM_TABLE_ENTRY(FYTK_SCHAR, signed char),
	PRIM_TABLE_ENTRY(FYTK_UCHAR, unsigned char),
	PRIM_TABLE_ENTRY(FYTK_SHORT, short),
	PRIM_TABLE_ENTRY(FYTK_USHORT, unsigned short),
	PRIM_TABLE_ENTRY(FYTK_INT, int),
	PRIM_TABLE_ENTRY(FYTK_UINT, unsigned int),
	PRIM_TABLE_ENTRY(FYTK_LONG, long),
	PRIM_TABLE_ENTRY(FYTK_ULONG, unsigned long),
	PRIM_TABLE_ENTRY(FYTK_LONGLONG, long long),
	PRIM_TABLE_ENTRY(FYTK_ULONGLONG, unsigned long long),
#ifdef FY_HAS_INT128
	PRIM_TABLE_ENTRY(FYTK_INT128, __int128),
	PRIM_TABLE_ENTRY(FYTK_UINT128, unsigned __int128),
#endif
	PRIM_TABLE_ENTRY(FYTK_FLOAT, float),
	PRIM_TABLE_ENTRY(FYTK_DOUBLE, double),
	PRIM_TABLE_ENTRY(FYTK_LONGDOUBLE, long double),
#ifdef FY_HAS_FP16
	PRIM_TABLE_ENTRY(FYTK_FLOAT16, __fp16),
#endif
#ifdef FY_HAS_FLOAT128
	PRIM_TABLE_ENTRY(FYTK_FLOAT128, __float128),
#endif
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
	[FYTK_NULL] = {
		.kind		= FYTK_NULL,
		.name		= "null",
		.size		= 0,
		.align		= 0,
		.enum_name	= "FYTK_NULL",
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
		.size		= sizeof(unsigned long long),
		.align		= alignof(unsigned long long),
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
		.size		= sizeof(long double),
		.align		= alignof(long double),
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
		.size		= sizeof(void *),
		.align		= alignof(void *),
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

size_t fy_type_kind_size(enum fy_type_kind type_kind)
{
	if (!fy_type_kind_is_valid(type_kind))
		return 0;

	return fy_type_kind_info_table[type_kind].size;
}

size_t fy_type_kind_align(enum fy_type_kind type_kind)
{
	if (!fy_type_kind_is_valid(type_kind))
		return 0;

	return fy_type_kind_info_table[type_kind].align;
}

const char *fy_type_kind_name(enum fy_type_kind type_kind)
{
	if (!fy_type_kind_is_valid(type_kind))
		return 0;

	return fy_type_kind_info_table[type_kind].name;
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
		return -1;
	case FYTK_BOOL:
	case FYTK_UCHAR:
	case FYTK_USHORT:
	case FYTK_UINT:
	case FYTK_ULONG:
	case FYTK_ULONGLONG:
	case FYTK_UINT128:
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
	[FYDT_PRIMITIVE]= "primitive",
	[FYDT_PRIMARY]= "primary",
};

static struct cp_tu *rfl_cp_tu_create(struct fy_reflection *rfl);

/* stand-ins for the base type (and a function's return type and arguments) */
#define GEN_BASE	"__fy_base"
#define GEN_RET		"__fy_ret"

static unsigned int fy_flags_to_cp_quals(unsigned int flags)
{
	return ((flags & FYTF_CONST) ? CPQ_CONST : 0) |
	       ((flags & FYTF_VOLATILE) ? CPQ_VOLATILE : 0) |
	       ((flags & FYTF_RESTRICT) ? CPQ_RESTRICT : 0);
}

/* replace the first occurrence of key in str by with (and optionally a space after it) */
static char *gen_subst(char *str, const char *key, const char *with, bool eat_space)
{
	char *at = strstr(str, key), *out;
	size_t klen = strlen(key), wlen = strlen(with), pre, post;

	if (!at)
		return str;
	pre = at - str;
	post = strlen(at + klen);
	if (eat_space && !wlen && at[klen] == ' ') {
		klen++;
		post--;
	}
	out = malloc(pre + wlen + post + 1);
	if (!out) {
		free(str);
		return NULL;
	}
	memcpy(out, str, pre);
	memcpy(out + pre, with, wlen);
	memcpy(out + pre + wlen, at + klen, post + 1);
	free(str);
	return out;
}

char *fy_type_generate_c_declaration(struct fy_type *ft, const char *field, unsigned int flags)
{
	struct fy_reflection *rfl;
	const struct fy_type_kind_info *tki;
	struct fy_type *ftd, *slow, *fast;
	struct fy_decl *decld;
	struct fy_type **ft_stack;
	struct cp_tu *tu = NULL;
	struct cp_type *cpt;
	const char *type_name = NULL, *type_prefix = NULL, *s, *e, *decl;
	char *func_return = NULL, *func_args = NULL;
	char *ret = NULL, *base = NULL;
	size_t len;
	int i, stack_count;
	bool no_type, is_func;
	unsigned int q;
	char *paren;

	if (!ft)
		return NULL;

	rfl = ft->rfl;
	no_type = !!(flags & FYTGTF_NO_TYPE);

	assert(ft->type_kind != FYTK_INVALID);

	/* Reject a malformed dependent type cycle before counting the chain. */
	slow = fast = ft;
	for (;;) {
		slow = slow && slow->dependent_type && slow->type_kind != FYTK_TYPEDEF &&
		       slow->type_kind != FYTK_ENUM ? slow->dependent_type : NULL;
		fast = fast && fast->dependent_type && fast->type_kind != FYTK_TYPEDEF &&
		       fast->type_kind != FYTK_ENUM ? fast->dependent_type : NULL;
		fast = fast && fast->dependent_type && fast->type_kind != FYTK_TYPEDEF &&
		       fast->type_kind != FYTK_ENUM ? fast->dependent_type : NULL;
		if (!slow || !fast)
			break;
		if (slow == fast)
			return NULL;
	}

	/* the chain of dependencies, ending at no dep or typedef */
	ftd = ft;
	for (i = 0; ftd->dependent_type && (ftd->type_kind != FYTK_TYPEDEF && ftd->type_kind != FYTK_ENUM); i++)
		ftd = ftd->dependent_type;
	i++;

	/* may happen on recursive cases */
	if (!ftd || fy_type_kind_is_like_ptr(ftd->type_kind))
		return NULL;

	ft_stack = alloca(i * sizeof(*ft_stack));
	stack_count = i;
	for (ftd = ft, i = 0; i < stack_count; ftd = ftd->dependent_type, i++) {
		assert(ftd);
		ft_stack[i] = ftd;
	}
	ftd = ft_stack[stack_count - 1];
	is_func = ftd->type_kind == FYTK_FUNCTION;

	/* a function type is spelled "ret (args)" in its declaration name */
	if (is_func) {
		decld = fy_type_decl(ftd);
		s = decld ? decld->name : NULL;
		s = s ? strchr(s, '(') : NULL;
		if (s) {
			decl = decld->name;
			e = decl + strlen(decl);
			while (e > (s + 1) && isspace((unsigned char)e[-1]))
				e--;
			if (e > (s + 1) && e[-1] == ')')
				e--;
			len = e - (s + 1);
			func_args = strndup(s + 1, len);
			while (s > decl && isspace((unsigned char)s[-1]))
				s--;
			func_return = strndup(decl, s - decl);
		} else {
			func_return = strdup("void");
			func_args = strdup("");
		}
		if (!func_return || !func_args)
			goto out;
	}

	/* the base type's own name */
	if (!no_type) {
		decld = fy_type_decl(ftd);
		tki = fy_type_kind_info_get_internal(ftd->type_kind);
		if (fy_type_kind_has_prefix(ftd->type_kind)) {
			/* a corrupt import can leave the type without a decl */
			if (!decld)
				goto out;
			type_prefix = tki->name;
			type_name = decld->name;
		} else if (fy_type_kind_is_primitive(ftd->type_kind)) {
			type_name = tki->name;
		} else if (ftd->type_kind == FYTK_TYPEDEF) {
			if (!decld)
				goto out;
			type_name = decld->name;
		} else if (!is_func)
			goto out;
	}

	tu = rfl_cp_tu_create(rfl);
	if (!tu)
		goto out;

	/* innermost first */
	if (is_func) {
		/* "ret ()" has no prototype; a parameter stand-in is dropped later */
		s = *func_args ? GEN_RET " (int)" : GEN_RET " (void)";
		cpt = cp_parse_type_text(tu, s, strlen(s));
	} else {
		cpt = cp_parse_type_text(tu, GEN_BASE, strlen(GEN_BASE));
		if (cpt && !no_type && fy_flags_to_cp_quals(ftd->flags))
			cpt = cp_type_qualified(tu, cpt, fy_flags_to_cp_quals(ftd->flags));
	}
	if (!cpt)
		goto out;

	for (i = stack_count - 2; i >= 0 && cpt; i--) {
		q = no_type ? 0 : fy_flags_to_cp_quals(ft_stack[i]->flags);

		switch (ft_stack[i]->type_kind) {
		case FYTK_PTR:
			cpt = cp_type_pointer(tu, cpt);
			if (cpt && q)
				cpt = cp_type_qualified(tu, cpt, q);
			break;
		case FYTK_INCOMPLETEARRAY:
		case FYTK_CONSTARRAY:
			/* qualifiers on an array apply to its elements */
			if (q)
				cpt = cp_type_qualified(tu, cpt, q);
			cpt = cp_type_array(tu, cpt, ft_stack[i]->type_kind == FYTK_CONSTARRAY ?
						     (int64_t)ft_stack[i]->element_count : -1);
			break;
		default:
			break;
		}
	}
	if (!cpt)
		goto out;

	s = cp_type_declaration(tu, cpt, field);
	if (!s)
		goto out;
	ret = strdup(s);
	if (!ret)
		goto out;

	if (is_func) {
		/* the arguments first, the return type text may hold "(int)" too */
		if (*func_args) {
			paren = malloc(strlen(func_args) + 3);

			if (!paren) {
				free(ret);
				ret = NULL;
				goto out;
			}
			sprintf(paren, "(%s)", func_args);
			ret = gen_subst(ret, "(int)", paren, false);
			free(paren);
		} else if (ret) {
			ret = gen_subst(ret, "(void)", "()", false);
		}
		if (ret)
			ret = gen_subst(ret, GEN_RET, no_type ? "" : func_return, false);
	} else if (type_name) {
		base = malloc(strlen(type_prefix ? type_prefix : "") + strlen(type_name) + 2);
		if (!base) {
			free(ret);
			ret = NULL;
			goto out;
		}
		sprintf(base, "%s%s%s", type_prefix ? type_prefix : "", type_prefix ? " " : "", type_name);
		ret = gen_subst(ret, GEN_BASE, base, false);
	} else
		ret = gen_subst(ret, GEN_BASE, "", true);

	if (ret) {
		len = strlen(ret);
		while (len && isspace((unsigned char)ret[len - 1]))
			ret[--len] = '\0';
	}
out:
	free(base);
	free(func_return);
	free(func_args);
	if (tu)
		cp_tu_destroy(tu);
	return ret;
}

int fy_type_generate_name(struct fy_type *ft)
{
	struct fy_reflection *rfl;
	struct fy_type *ft_revdep;
	char *gen_name;
	int ret;

	assert(ft);
	rfl = ft->rfl;
	assert(rfl);

	if (!(ft->flags & FYTF_NEEDS_NAME))
		return 0;

	RFL_ASSERT(!ft->fullname);
	RFL_ASSERT(ft->flags & FYTF_NEEDS_NAME);

	gen_name = fy_type_generate_c_declaration(ft, NULL, FYTGTF_NO_FIELD);
	if (!gen_name) {
		ft->flags |= FYTF_NEEDS_NAME;
		return -1;
	}

	if (ft->fullname)
		free(ft->fullname);
	ft->fullname = gen_name;
	ft->fullname_len = strlen(gen_name);

	ft->flags &= ~FYTF_NEEDS_NAME;

#if 0
	if (ft->backend_name && strcmp(ft->backend_name, ft->fullname))
		fprintf(stderr, "\e[31m" "%s: %s:%d generated type (kind=%s) differs fullname='%s' != backend_name='%s'" "\e[0m\n",
				__func__, __FILE__, __LINE__,
				fy_type_kind_name(ft->type_kind),
				ft->fullname, ft->backend_name);
	else
		fprintf(stderr, "\e[32m" "%s: %s:%d generated name fullname='%s'" "\e[0m\n",
				__func__, __FILE__, __LINE__,
				ft->fullname);
#endif

	/* find all types that use this as a dependent and generate their names */
	for (ft_revdep = fy_type_list_head(&rfl->types); ft_revdep != NULL; ft_revdep = fy_type_next(&rfl->types, ft_revdep)) {
		if (!(ft_revdep->flags & FYTF_NEEDS_NAME) || ft_revdep->dependent_type != ft)
			continue;

		ret = fy_type_generate_name(ft_revdep);
		RFL_ASSERT(!ret);
	}

	return 0;

err_out:
	return -1;
}

void fy_type_destroy(struct fy_type *ft)
{
	struct fy_type_info_wrapper *tiw;

	if (!ft)
		return;

	backend_type_cleanup(ft);

	tiw = &ft->tiw;
	if (tiw->field_decls)
		free(tiw->field_decls);
	if (tiw->fields)
		free(tiw->fields);

	if (ft->backend_name)
		free(ft->backend_name);
	if (ft->fullname)
		free(ft->fullname);
	free(ft);
}

struct fy_type *fy_type_create(struct fy_reflection *rfl, enum fy_type_kind type_kind,
			       enum fy_type_flags flags, const char *name,
			       struct fy_decl *decl, struct fy_type *ft_dep, void *user,
			       uintmax_t element_count)
{
	const struct fy_type_kind_info *tki;
	struct fy_type *ft = NULL;
	int rc;

	assert(rfl);

	/* for an array, the dependency must be there */
	if (type_kind == FYTK_CONSTARRAY && !ft_dep)
		return NULL;
	/* Synthetic arrays skip fixup, so validate before multiplying here too. */
	if (type_kind == FYTK_CONSTARRAY &&
	    element_count > (uintmax_t)PTRDIFF_MAX / (ft_dep->size ? ft_dep->size : 1))
		return NULL;

	/* guard against rollover */
	if (rfl->next_type_id + 1 <= 0)
		return NULL;

	RFL_ASSERT(fy_type_kind_is_valid(type_kind));

	ft = malloc(sizeof(*ft));
	RFL_ASSERT(ft);
	memset(ft, 0, sizeof(*ft));

	ft->rfl = rfl;
	ft->type_kind = type_kind;
	ft->flags = flags | FYTF_NEEDS_NAME;

	/* prime the sizes (named types get sized appropriately later) */

	if (type_kind == FYTK_CONSTARRAY) {
		ft->element_count = element_count;
		ft->size = ft_dep->size * element_count;
		ft->align = ft_dep->align;
	} else {
		tki = fy_type_kind_info_get_internal(type_kind);
		ft->size = tki->size;
		ft->align = tki->align;
	}

	if (decl) {
		ft->decl = decl;
		RFL_ASSERT(!(ft->flags & FYTF_ELABORATED));
		decl->type = ft;

		if (type_kind == FYTK_TYPEDEF) {
			RFL_ASSERT(decl->decl_type == FYDT_TYPEDEF);
		}
	}

	if (name) {
		ft->backend_name = strdup(name);
		RFL_ASSERT(ft->backend_name);
	}

	if (fy_type_kind_is_dependent(ft->type_kind)) {

		ft->flags |= FYTF_UNRESOLVED;

		if (ft_dep) {
			rc = fy_type_set_dependent(ft, ft_dep);
			RFL_ASSERT(!rc);
		} else {
			rc = fy_type_register_unresolved(ft);
			RFL_ASSERT(!rc);
		}
	}

	rc = backend_type_setup(ft, user);
	RFL_ASSERT(!rc);

	rc = fy_type_generate_name(ft);
	if (rc < 0) {
		rfl_debug(rfl, "cannot generate type name (backend='%s') (dependent)\n",
				ft->backend_name ? ft->backend_name : "");
		rc = 0;
	}

	assert(rfl->next_type_id >= 0);
	ft->id = rfl->next_type_id++;

	return ft;
err_out:
	fy_type_destroy(ft);
	return NULL;
}

struct fy_type *fy_type_with_qualifiers(struct fy_type *ft_src, unsigned int quals)
{
	unsigned int src_quals;

	if (!ft_src || (quals >> FY_QUALIFIER_BIT_START) >= (1 << FY_QUALIFIER_COUNT))
		return NULL;

	src_quals = ((ft_src->flags & FYTF_CONST) ? FY_QUALIFIER_CONST : 0) |
		    ((ft_src->flags & FYTF_VOLATILE) ? FY_QUALIFIER_VOLATILE : 0) |
		    ((ft_src->flags & FYTF_RESTRICT) ? FY_QUALIFIER_RESTRICT : 0);

	/* the base type must be unqualified */
	if (src_quals != 0 || (ft_src->flags & FYTF_ELABORATED))
		return NULL;

	/* requesting the unqualified type */
	if (src_quals == 0 && quals == 0)
		return ft_src;

	/* return the qualified type, if any */
	return ft_src->qualified_types[quals >> FY_QUALIFIER_BIT_START];
}

struct fy_type *fy_type_unqualified(struct fy_type *ft)
{
	if (!ft)
		return NULL;

	if (!(ft->flags & FYTF_ELABORATED))
		return ft;

	return ft->unqualified_type;
}

int fy_type_update_elaborated(struct fy_type *ft, void *user)
{
	struct fy_reflection *rfl;
	enum fy_type_flags keep_flags;
	struct fy_type *ft_src;
	int rc;

	if (!ft)
		return 0;

	rfl = ft->rfl;
	assert(rfl);

	if (!(ft->flags & FYTF_ELABORATED) || !ft->unqualified_type)
		return -1;

	if (ft->fullname) {
		free(ft->fullname);
		ft->fullname = NULL;
		ft->fullname_len = 0;
	}

	if (ft->backend_name) {
		free(ft->backend_name);
		ft->backend_name = NULL;
	}

	ft_src = ft->unqualified_type;

	keep_flags = ft->flags & (FYTF_CONST | FYTF_VOLATILE | FYTF_RESTRICT);
	ft->flags = ft_src->flags | keep_flags | FYTF_ELABORATED | FYTF_NEEDS_NAME;
	/* clear bits that need to be redone */
	ft->flags &= ~(FYTF_TYPE_INFO_UPDATED | FYTF_TYPE_INFO_UPDATING |
		       FYTF_MARKER | FYTF_MARK_IN_PROGRESS | FYTF_UPDATE_TYPE_INFO);

	ft->size = ft_src->size;
	ft->align = ft_src->align;
	ft->element_count = ft_src->element_count;
	ft->dependent_type = ft_src->dependent_type;

	rc = fy_type_generate_name(ft);
	RFL_ASSERT(!rc);

	ft->backend_name = strdup(ft->fullname);
	RFL_ASSERT(ft->backend_name);

	if (user) {
		rc = backend_type_setup(ft, user);
		RFL_ASSERT(!rc);
	}

	return 0;

err_out:
	return -1;
}

int fy_type_update_all_elaborated(struct fy_type *ft)
{
	struct fy_reflection *rfl;
	struct fy_type *ftt;
	unsigned int i;
	int ret;

	if (!ft)
		return 0;

	rfl = ft->rfl;
	assert(rfl);

	for (i = 0; i < ARRAY_SIZE(ft->qualified_types); i++) {
		ftt = ft->qualified_types[i];
		if (!ftt)
			continue;

		ret = fy_type_update_elaborated(ftt, NULL);
		RFL_ASSERT(!ret);
	}
	return 0;

err_out:
	return -1;
}

struct fy_type *fy_type_create_with_qualifiers(struct fy_type *ft_src, unsigned int quals, void *user)
{
	struct fy_reflection *rfl;
	struct fy_type *ft;
	int rc;

	if (!ft_src || (quals >> FY_QUALIFIER_BIT_START) >= (1 << FY_QUALIFIER_COUNT))
		return NULL;

	/* qualified type already exists */
	ft = fy_type_with_qualifiers(ft_src, quals);
	if (ft)
		return NULL;

	rfl = ft_src->rfl;

	/* guard against rollover */
	if (rfl->next_type_id + 1 <= 0)
		return NULL;

	ft = malloc(sizeof(*ft));
	RFL_ASSERT(ft);
	memset(ft, 0, sizeof(*ft));

	ft->rfl = rfl;
	ft->type_kind = ft_src->type_kind;
	ft->unqualified_type = ft_src;

	/* initial flags */
	ft->flags = FYTF_ELABORATED |
		     ((quals & FY_QUALIFIER_CONST) ? FYTF_CONST : 0) |
		     ((quals & FY_QUALIFIER_VOLATILE) ? FYTF_VOLATILE : 0) |
		     ((quals & FY_QUALIFIER_RESTRICT) ? FYTF_RESTRICT : 0);
	rc = fy_type_update_elaborated(ft, user);
	RFL_ASSERT(!rc);

	assert(rfl->next_type_id >= 0);
	ft->id = rfl->next_type_id++;

	/* store it */
	ft_src->qualified_types[quals >> FY_QUALIFIER_BIT_START] = ft;

	return ft;
err_out:
	fy_type_destroy(ft);
	return NULL;
}

struct fy_type *
fy_type_create_pointer(struct fy_type *ft_base, unsigned int quals)
{
	struct fy_reflection *rfl;
	struct fy_type *ft = NULL;
	enum fy_type_flags flags;

	if (!ft_base)
		return NULL;

	rfl = ft_base->rfl;
	assert(rfl);

	flags = FYTF_SYNTHETIC |
		((quals & FY_QUALIFIER_CONST) ? FYTF_CONST : 0) |
		((quals & FY_QUALIFIER_VOLATILE) ? FYTF_VOLATILE : 0) |
		((quals & FY_QUALIFIER_RESTRICT) ? FYTF_RESTRICT : 0);

	ft = fy_type_create(rfl, FYTK_PTR, flags, NULL, NULL, ft_base, NULL, 0);
	RFL_ASSERT(ft);

	fy_type_list_add_tail(&rfl->types, ft);

	return ft;

err_out:
	fy_type_destroy(ft);
	return NULL;
}

struct fy_type *
fy_type_create_array(struct fy_type *ft_base, unsigned int quals, uintmax_t array_size)
{
	struct fy_reflection *rfl;
	struct fy_type *ft = NULL;
	enum fy_type_flags flags;

	if (!ft_base)
		return NULL;

	rfl = ft_base->rfl;
	assert(rfl);

	flags = FYTF_SYNTHETIC |
		((quals & FY_QUALIFIER_CONST) ? FYTF_CONST : 0) |
		((quals & FY_QUALIFIER_VOLATILE) ? FYTF_VOLATILE : 0) |
		((quals & FY_QUALIFIER_RESTRICT) ? FYTF_RESTRICT : 0);

	ft = fy_type_create(rfl, FYTK_CONSTARRAY, flags, NULL, NULL, ft_base, NULL, array_size);
	RFL_ASSERT(ft);

	fy_type_list_add_tail(&rfl->types, ft);

	return ft;

err_out:
	fy_type_destroy(ft);
	return NULL;
}

/* compare removing spaces unless they separate alnums */
bool
fy_c_decl_equal(const char *a, size_t alen, const char *b, size_t blen)
{
	char ca, cb, lastc;
	bool spacea, spaceb;
	const char *ae, *be;

	if (alen == (size_t)-1)
		alen = strlen(a);
	if (blen == (size_t)-1)
		blen = strlen(b);

	ae = a + alen;
	be = b + blen;

	/* fast and easy */
	if (alen == blen && !memcmp(a, b, alen))
		return true;

	lastc = '\0';
	do {
		spacea = a < ae && isspace((unsigned char)*a);
		if (spacea) {
			while (a < ae && isspace((unsigned char)*a))
				a++;
		}
		ca = a < ae ? *a++ : '\0';

		spaceb = b < be && isspace((unsigned char)*b);
		if (spaceb) {
			while (b < be && isspace((unsigned char)*b))
				b++;
		}
		cb = b < be ? *b++ : '\0';

		/* the (non-space) characters must always match */
		if (ca != cb)
			return false;

		/* 'ab' vs 'a b' not match */
		/* '*(' vs '* (' match */
		if (ca && is_c_identifier(lastc, false) && spacea != spaceb)
			return false;

		lastc = ca;

	} while (lastc != '\0');

	return true;
}

struct fy_type *
fy_base_type_lookup_by_kind(struct fy_reflection *rfl, enum fy_type_kind type_kind,
			    const char *base_name, size_t base_name_len, unsigned int quals)
{
	struct fy_type *ft;
	struct fy_decl *decl;
	enum fy_type_flags qual_type_flags;

	assert(rfl);

	/* only base types */
	RFL_ASSERT(fy_type_kind_is_named(type_kind) || fy_type_kind_is_primary(type_kind));

	if (fy_type_kind_is_primary(type_kind))
		return fy_reflection_get_primary_type(rfl, type_kind, quals);

	if (base_name_len == (size_t)-1)
		base_name_len = strlen(base_name);

	qual_type_flags = ((quals & FY_QUALIFIER_CONST) ? FYTF_CONST : 0) |
			  ((quals & FY_QUALIFIER_VOLATILE) ? FYTF_VOLATILE : 0) |
			  ((quals & FY_QUALIFIER_RESTRICT) ? FYTF_RESTRICT : 0);

	/* named type? struct, union, typedef, function, enum */
	RFL_ASSERT(fy_type_kind_is_named(type_kind));

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {

		if (ft->type_kind != type_kind)
			continue;

		decl = fy_type_decl(ft);
		RFL_ASSERT(decl);

		if (decl->name_len == base_name_len &&
		    !memcmp(decl->name, base_name, base_name_len) &&
		    (ft->flags & (FYTF_CONST | FYTF_VOLATILE | FYTF_RESTRICT)) == qual_type_flags)
			return ft;
	}

	return NULL;

err_out:
	return NULL;
}

struct fy_type *
fy_base_type_lookup(struct fy_reflection *rfl, enum fy_type_kind type_kind,
		    const char *base_name, size_t base_name_len, unsigned int quals)
{
	struct fy_type *ft;
	struct fy_decl *decl;
	enum fy_type_flags qual_type_flags;

	if (fy_type_kind_is_primary(type_kind)) {
		ft = fy_reflection_get_primary_type(rfl, type_kind, quals);
		RFL_ASSERT(ft);
	} else {
		qual_type_flags = ((quals & FY_QUALIFIER_CONST) ? FYTF_CONST : 0) |
				  ((quals & FY_QUALIFIER_VOLATILE) ? FYTF_VOLATILE : 0) |
				  ((quals & FY_QUALIFIER_RESTRICT) ? FYTF_RESTRICT : 0);

		if (base_name_len == (size_t)-1)
			base_name_len = strlen(base_name);

		for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {

			if (ft->type_kind != type_kind)
				continue;

			decl = fy_type_decl(ft);
			if (!decl)
				continue;

			if (decl->name_len == base_name_len &&
			    !memcmp(decl->name, base_name, base_name_len) &&
			    (ft->flags & (FYTF_CONST | FYTF_VOLATILE | FYTF_RESTRICT)) == qual_type_flags)
				break;
		}
	}

	return ft;

err_out:
	return NULL;
}

struct fy_type *
fy_type_lookup_pointer(struct fy_type *ft_base, unsigned int quals)
{
	struct fy_reflection *rfl;
	struct fy_type *ft;
	enum fy_type_flags qual_type_flags;

	assert(ft_base);
	rfl = ft_base->rfl;
	assert(rfl);

	qual_type_flags = ((quals & FY_QUALIFIER_CONST) ? FYTF_CONST : 0) |
			  ((quals & FY_QUALIFIER_VOLATILE) ? FYTF_VOLATILE : 0) |
			  ((quals & FY_QUALIFIER_RESTRICT) ? FYTF_RESTRICT : 0);

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {
		if (ft->type_kind == FYTK_PTR && ft->dependent_type == ft_base &&
		    (ft->flags & (FYTF_CONST | FYTF_VOLATILE | FYTF_RESTRICT)) == qual_type_flags &&
		    (ft->flags & FYTF_SYNTHETIC) && !fy_type_decl(ft))
			break;
	}

	return ft;
}

struct fy_type *
fy_type_lookup_array(struct fy_type *ft_base, unsigned int quals, uintmax_t arrsz)
{
	struct fy_reflection *rfl;
	struct fy_type *ft;
	enum fy_type_flags qual_type_flags;

	assert(ft_base);
	rfl = ft_base->rfl;
	assert(rfl);

	qual_type_flags = ((quals & FY_QUALIFIER_CONST) ? FYTF_CONST : 0) |
			  ((quals & FY_QUALIFIER_VOLATILE) ? FYTF_VOLATILE : 0) |
			  ((quals & FY_QUALIFIER_RESTRICT) ? FYTF_RESTRICT : 0);

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {
		if (ft->type_kind == FYTK_CONSTARRAY && ft->dependent_type == ft_base &&
		    ft->element_count == arrsz &&
		    (ft->flags & (FYTF_CONST | FYTF_VOLATILE | FYTF_RESTRICT)) == qual_type_flags &&
		    (ft->flags & FYTF_SYNTHETIC) && !fy_type_decl(ft))
			break;
	}

	return ft;
}

struct fy_type *
fy_type_lookup(struct fy_reflection *rfl, const char *name, size_t namelen)
{
	struct fy_type *ft;

	if (namelen == (size_t)-1)
		namelen = strlen(name);

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {
		if (!ft->fullname)
			continue;
		if (fy_c_decl_equal(ft->fullname, (size_t)-1, name, namelen))
			return ft;
	}
	return NULL;
}

struct fy_type_info_wrapper *
fy_type_get_info_wrapper(struct fy_type *ft,
			 struct fy_decl *decl FY_UNUSED)
{
	return ft ? &ft->tiw : NULL;
}

void fy_type_info_wrapper_dump_pending(struct fy_reflection *rfl)
{
	struct fy_unresolved_dep *udep, *udepn;
	struct fy_type_info_wrapper *tiwn;
	struct fy_type *ft FY_DEBUG_UNUSED;
	struct fy_type_info *ti FY_DEBUG_UNUSED;

	for (udep = fy_unresolved_dep_list_head(&rfl->unresolved_deps);
	     udep != NULL;
	     udep = udepn) {
		udepn = fy_unresolved_dep_next(&rfl->unresolved_deps, udep);

		tiwn = udep->tiw;
		ti = &tiwn->type_info;
		ft = fy_type_from_info_wrapper(tiwn);

		rfl_debug(rfl, "PENDING ti->name=%s ft->dependent_type='%s'\n", ti->name,
				ft->dependent_type ? ft->dependent_type->fullname : "<NULL>");
	}
}

void fy_type_update_info_flags(struct fy_type *ft)
{
	struct fy_reflection *rfl FY_DEBUG_UNUSED;
	struct fy_type_info *ti;
	struct fy_decl *decl;

	if (!ft)
		return;
	rfl = ft->rfl;
	assert(rfl);

	decl = fy_type_decl(ft);

	rfl_debug(rfl, "ft->fullname=%s\n", ft->fullname);
	ti = &ft->tiw.type_info;

	ti->flags = 0;
	if (ft->flags & FYTF_CONST)
		ti->flags |= FYTIF_CONST;
	if (ft->flags & FYTF_VOLATILE)
		ti->flags |= FYTIF_VOLATILE;
	if (ft->flags & FYTF_RESTRICT)
		ti->flags |= FYTIF_RESTRICT;
	if (ft->flags & (FYTF_FAKE_RESOLVED | FYTF_INCOMPLETE))
		ti->flags |= FYTIF_UNRESOLVED;
	if (decl) {
		if (decl->flags & FYDF_FROM_MAIN_FILE)
			ti->flags |= FYTIF_MAIN_FILE;
		if (decl->flags & FYDF_IN_SYSTEM_HEADER)
			ti->flags |= FYTIF_SYSTEM_HEADER;
	}
	if (ft->flags & FYTF_ANONYMOUS)
		ti->flags |= FYTIF_ANONYMOUS;
	if (ft->flags & FYTF_ANONYMOUS_RECORD_DECL)
		ti->flags |= FYTIF_ANONYMOUS_RECORD_DECL;
	if (ft->flags & FYTF_ANONYMOUS_DEP)
		ti->flags |= FYTIF_ANONYMOUS_DEP;
	if (ft->flags & FYTF_INCOMPLETE)
		ti->flags |= FYTIF_INCOMPLETE;
	if (ft->flags & FYTF_ELABORATED)
		ti->flags |= FYTIF_ELABORATED;
	if (ft->flags & FYTF_ANONYMOUS_GLOBAL)
		ti->flags |= FYTIF_ANONYMOUS_GLOBAL;
}

int fy_type_create_info(struct fy_type *ft)
{
	struct fy_reflection *rfl;
	struct fy_decl *decl, *declc;
	struct fy_type_info_wrapper *tiw, *tiw_dep;
	struct fy_type_info *ti;
	struct fy_field_info *fi;
	struct fy_decl **fds;
	int ret;

	if (!ft)
		return -1;

	rfl = ft->rfl;
	assert(rfl);

	decl = fy_type_decl(ft);

	rfl_debug(rfl, "create_info: ft->fullname=%s\n", ft->fullname);
	tiw = &ft->tiw;

	/* clear olds ones */
	if (tiw->field_decls)
		free(tiw->field_decls);
	if (tiw->fields)
		free(tiw->fields);

	memset(tiw, 0, sizeof(*tiw));

	ti = &tiw->type_info;

	fy_type_update_info_flags(ft);

	ti->kind = ft->type_kind;
	ti->name = ft->fullname;
	ti->size = ft->size;
	ti->align = ft->align;

	/* primitive types, no more to do */
	if (fy_type_kind_is_primitive(ti->kind) || ti->kind == FYTK_FUNCTION)
		goto out;

	/* if we have a type we're dependent on, pull it in */
	if (ft->dependent_type) {
		tiw_dep = &ft->dependent_type->tiw;
		ti->dependent_type = &tiw_dep->type_info;
	}

	if (fy_type_kind_is_dependent(ti->kind) && !ti->dependent_type) {
		rfl_debug(rfl, "missing ti->dependent_type '%s'\n", ft->fullname);
		ret = fy_unresolved_dep_register_wrapper(tiw);
		RFL_ASSERT(!ret);
	}

	/* for pointers or typedef we' done now */
	if (ti->kind == FYTK_PTR || ti->kind == FYTK_TYPEDEF || ti->kind == FYTK_INCOMPLETEARRAY)
		goto out;

	/* constant array? fill in and out */
	if (ti->kind == FYTK_CONSTARRAY) {
		ti->count = ft->element_count;
		goto out;
	}

	/* if no fields, we're done */
	if (!fy_type_kind_has_fields(ti->kind))
		goto out;

	/* for a type to have fields, it must be a declared type */
	RFL_ASSERT(decl);

	/* count the number of fields/enum_values */
	ti->count = 0;
	for (declc = fy_decl_list_head(&decl->children); declc; declc = fy_decl_next(&decl->children, declc))
		ti->count++;

	tiw->field_decls = malloc(sizeof(*tiw->field_decls) * ti->count);
	RFL_ASSERT(tiw->field_decls);
	memset(tiw->field_decls, 0, sizeof(*tiw->field_decls) * ti->count);

	fi = malloc(sizeof(*fi) * ti->count);
	RFL_ASSERT(fi);
	memset(fi, 0, sizeof(*fi) * ti->count);

	ti->fields = tiw->fields = fi;

	fds = tiw->field_decls;
	for (declc = fy_decl_list_head(&decl->children); declc; declc = fy_decl_next(&decl->children, declc), fi++) {
		*fds++ = declc;
		fi->flags = 0;
		fi->parent = ti;
		fi->name = declc->name;
		/* may be null for enum values */

		tiw_dep = fy_type_get_info_wrapper(declc->type, declc);
		RFL_ASSERT(tiw_dep);

		// fprintf(stderr, "\e[34m%s: %s.%s (declc->type=%s '%s') tiw_dep=%s\e[0m\n", __func__, ft->fullname, fi->name,
		//		declc->type ? declc->type->name : "<NULL>", fy_decl_get_yaml_comment(declc),
		//		tiw_dep ? tiw_dep->type_info.fullname : "");

		fi->type_info = &tiw_dep->type_info;

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
	return 0;

err_out:
	return -1;
}

static bool fy_type_has_dependent_cycle(const struct fy_type *ft)
{
	const struct fy_type *slow, *fast;

	slow = fast = ft;
	while (fast && fast->dependent_type) {
		fast = fast->dependent_type->dependent_type;
		slow = slow->dependent_type;
		if (fast == slow)
			return true;
	}
	return false;
}

int fy_type_update_info(struct fy_type *ft)
{
	struct fy_reflection *rfl;
	struct fy_type **pending = NULL, **new_pending;
	const struct fy_type_info *ti;
	const struct fy_field_info *fi;
	size_t i, count = 0, alloc = 0;
	int ret;

	if (!ft)
		return 0;

	rfl = ft->rfl;
	assert(rfl);

	if (ft->flags & FYTF_TYPE_INFO_UPDATING)
		return 0;

	/* a dependent type chain must end; reject a malformed cycle */
	RFL_ASSERT(!fy_type_has_dependent_cycle(ft));

	/* Keep dependent chains off the C stack, even before type setup. */
	while (ft && !(ft->flags & (FYTF_TYPE_INFO_UPDATING | FYTF_TYPE_INFO_UPDATED))) {
		if (count == alloc) {
			alloc = alloc ? alloc * 2 : 16;
			new_pending = realloc(pending, alloc * sizeof(*pending));
			RFL_ASSERT(new_pending);
			pending = new_pending;
		}
		pending[count++] = ft;
		ft->flags |= FYTF_TYPE_INFO_UPDATING;
		ret = fy_type_create_info(ft);
		RFL_ASSERT(!ret);
		ft = ft->dependent_type;
	}
	if (ft)
		RFL_ASSERT(fy_type_kind_is_valid(ft->tiw.type_info.kind));

	/* Finish dependencies before their users, retaining the cycle markers
	 * while updating fields that may refer back to a type in progress. */
	while (count) {
		ft = pending[count - 1];
		ti = &ft->tiw.type_info;
		if (fy_type_kind_has_fields(ti->kind)) {
			for (i = 0, fi = ti->fields; i < ti->count; i++, fi++) {
				ret = fy_type_update_info(fy_type_from_info(fi->type_info));
				RFL_ASSERT(!ret);
			}
		}

		RFL_ASSERT(fy_type_kind_is_valid(ti->kind));
		ft->flags &= ~FYTF_TYPE_INFO_UPDATING;
		ft->flags |= FYTF_TYPE_INFO_UPDATED;
		count--;
	}
	free(pending);
	return 0;

err_out:
	/* do not let a failed update look like a recursive update */
	while (count)
		pending[--count]->flags &= ~FYTF_TYPE_INFO_UPDATING;
	free(pending);
	return -1;
}

int fy_type_update_all_info(struct fy_reflection *rfl)
{
	struct fy_type *ft;
	int ret;

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {
		ret = fy_type_update_info(ft);
		RFL_ASSERT(!ret);
	}

	return 0;

err_out:
	return -1;
}

void fy_type_clear_marker(struct fy_type *ft)
{
	if (!ft || !ft->marker)
		return;

	assert(ft->marker > 0);
	ft->marker--;
}

void fy_type_mark(struct fy_type *ft)
{
	unsigned int i;

	if (!ft || (ft->flags & FYTF_MARK_IN_PROGRESS))
		return;

	ft->flags |= FYTF_MARK_IN_PROGRESS;

	ft->marker++;
	assert(ft->marker > 0);

	fy_decl_mark(ft->decl);	/* is NULL for elaborated and primitives */
	fy_type_mark(ft->dependent_type);
	fy_type_mark(ft->unqualified_type);
	for (i = 0; i < ARRAY_SIZE(ft->qualified_types); i++)
		fy_type_mark(ft->qualified_types[i]);

	ft->flags &= ~FYTF_MARK_IN_PROGRESS;
}

int fy_type_fixup(struct fy_type *ft)
{
	struct fy_reflection *rfl;
	struct fy_type *ftc;
	enum fy_type_kind type_kind;
	struct fy_decl *declc, *decl;
	size_t bit_offset, bit_size, bit_align, max_align, max_size, max_bit_offset, bit_width;
	size_t elem_size;
	uintmax_t max_count;
	bool is_bitfield, last_was_bitfield, is_first_field;
	int rc;

	if (!ft)
		return -1;

	rfl = ft->rfl;
	assert(rfl);

	if (ft->flags & (FYTF_FIXED | FYTF_SYNTHETIC))
		return 0;

	rc = 0;
	/* check for recursive fix, and break out */
	if (ft->flags & FYTF_FIX_IN_PROGRESS)
		goto out;

	ft->flags |= FYTF_FIX_IN_PROGRESS;

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

	/* for the rest, if size, align are set don't try again */
	if (ft->size && ft->align)
		goto out;

	switch (type_kind) {
	case FYTK_ENUM:
	case FYTK_TYPEDEF:
		RFL_ASSERT(ft->dependent_type);

		rc = fy_type_fixup(ft->dependent_type);
		RFL_ASSERT(!rc);

		ft->size = ft->dependent_type->size;
		ft->align = ft->dependent_type->align;
		break;

	case FYTK_PTR:
		RFL_ASSERT(ft->dependent_type);

		rc = fy_type_fixup(ft->dependent_type);
		RFL_ASSERT(!rc);

		/* the sizes are always the same as a void pointer */
		ft->size = sizeof(void *);
		ft->align = alignof(void *);
		break;

	case FYTK_INCOMPLETEARRAY:
		RFL_ASSERT(ft->dependent_type);

		rc = fy_type_fixup(ft->dependent_type);
		RFL_ASSERT(!rc);

		/* size is 0, but align is that of the underlying type */
		ft->size = 0;
		ft->align = ft->dependent_type->align;
		break;

	case FYTK_CONSTARRAY:
		RFL_ASSERT(ft->dependent_type);

		rc = fy_type_fixup(ft->dependent_type);
		RFL_ASSERT(!rc);

		/* the array must fit in the address space, even if its elements are empty */
		elem_size = ft->dependent_type->size;
		if (!elem_size)
			elem_size = 1;
		max_count = (uintmax_t)PTRDIFF_MAX / elem_size;
		RFL_ASSERT(ft->element_count <= max_count);

		/* size is the multiple of the element count */
		ft->size = ft->dependent_type->size * ft->element_count;
		ft->align = ft->dependent_type->align;
		break;

	case FYTK_STRUCT:
	case FYTK_UNION:
		decl = fy_type_decl(ft);
		RFL_ASSERT(decl);
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
			RFL_ASSERT(ftc);

			rc = fy_type_fixup(ftc);
			RFL_ASSERT(!rc);

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
					RFL_ASSERT(decl->field_decl.byte_offset == 0);
					decl->field_decl.byte_offset = 0;
				} else {
					/* if there is a configured offset check against it */
					/* it must always match */
					if (declc->field_decl.byte_offset)
						RFL_ASSERT(declc->field_decl.byte_offset == bit_offset / 8);
					declc->field_decl.byte_offset = bit_offset / 8;
				}

				/* advance and align */
				bit_offset += bit_size;
				bit_offset = (bit_offset + bit_align - 1) & ~(bit_align - 1);
			} else {
				/* XXX probably needs a per target config here, everything is implementation defined */

				bit_width = declc->bitfield_decl.bit_width;

				/* the byte width of the type must be less or equal of the underlying type */
				RFL_ASSERT(bit_width <= bit_size);

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

		if (!max_align && !(ft->flags & FYTF_INCOMPLETE))
			max_align = 1;

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

	rc = 0;
	goto out;
err_out:
	rc = -1;
out:
	ft->flags |= FYTF_FIXED;
	ft->flags &= ~FYTF_FIX_IN_PROGRESS;
	return rc;
}

void fy_type_set_flags(struct fy_type *ft, enum fy_type_flags flags_set, enum fy_type_flags flags_mask)
{
	if (!ft)
		return;

	ft->flags = (ft->flags & ~flags_mask) | (flags_set & flags_mask);
	fy_type_update_info_flags(ft);
}

void fy_type_all_set_flags(struct fy_type *ft, enum fy_type_flags flags_set, enum fy_type_flags flags_mask)
{
	struct fy_type *ft_qual;
	unsigned int i;

	fy_type_set_flags(ft, flags_set, flags_mask);
	if (ft->flags & FYTF_ELABORATED)
		return;

	for (i = 0; i < ARRAY_SIZE(ft->qualified_types); i++) {
		ft_qual = ft->qualified_types[i];
		if (!ft_qual)
			continue;
		fy_type_set_flags(ft_qual, flags_set, flags_mask);
	}
}

int
fy_type_set_dependent(struct fy_type *ft, struct fy_type *ft_dep)
{
	struct fy_reflection *rfl;
	struct fy_type *ft_qual;
	unsigned int i;

	assert(ft);
	rfl = ft->rfl;
	assert(rfl);

	RFL_ASSERT(ft_dep);

	RFL_ASSERT((ft->flags & FYTF_UNRESOLVED));
	RFL_ASSERT(!ft->dependent_type);

	ft->dependent_type = ft_dep;

	fy_type_set_flags(ft, 0, FYTF_UNRESOLVED);	/* clear */
	fy_type_unregister_unresolved(ft);

	for (i = 0; i < ARRAY_SIZE(ft->qualified_types); i++) {
		ft_qual = ft->qualified_types[i];
		if (!ft_qual)
			continue;
		ft_qual->dependent_type = ft_dep;
		fy_type_set_flags(ft_qual, 0, FYTF_UNRESOLVED);
	}

	if (ft_dep->flags & (FYTF_ANONYMOUS | FYTF_ANONYMOUS_DEP))
		fy_type_all_set_flags(ft, FYTF_ANONYMOUS_DEP, FYTF_ANONYMOUS_DEP);	/* set */

	/* a dependency to a global anonymous type is impossible */
	if (ft_dep->flags & FYTF_ANONYMOUS_GLOBAL)
		fy_type_all_set_flags(ft_dep, 0, FYTF_ANONYMOUS_GLOBAL);	/* clear */

	return 0;

err_out:
	return -1;
}

int fy_reflection_fixup(struct fy_reflection *rfl)
{
	struct fy_type *ft;
	int rc;

	assert(rfl);

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft))
		ft->flags &= ~(FYTF_FIXED | FYTF_FIX_IN_PROGRESS);

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {
		rc = fy_type_fixup(ft);
		RFL_ASSERT(!rc);
	}

	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft))
		ft->flags &= ~FYTF_FIX_IN_PROGRESS;

	return 0;

err_out:
	return -1;
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

	if (decl->name_alloc)
		free(decl->name_alloc);
	free(decl);
}

struct fy_decl *fy_decl_create(struct fy_reflection *rfl, struct fy_import *imp,
	struct fy_decl *parent, enum fy_decl_type decl_type, const char *name, void *user)
{
	struct fy_decl *decl = NULL;
	int rc;

	if (!rfl)
		return NULL;

	RFL_ASSERT(fy_decl_type_is_valid(decl_type));

	RFL_ASSERT(!imp || imp->rfl == rfl);

	/* guard against rollover */
	if (rfl->next_decl_id + 1 <= 0)
		return NULL;

	decl = malloc(sizeof(*decl));
	RFL_ASSERT(decl);
	memset(decl, 0, sizeof(*decl));

	decl->rfl = rfl;
	decl->imp = imp;
	decl->parent = parent;
	decl->decl_type = decl_type;

	fy_decl_list_init(&decl->children);

	if (name) {
		decl->name_alloc = strdup(name);
		RFL_ASSERT(decl->name_alloc);
		decl->name = decl->name_alloc;
		decl->name_len = strlen(decl->name);
	} else
		decl->name = "";

	rc = backend_decl_setup(decl, user);
	RFL_ASSERT(!rc);

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

intmax_t fy_decl_enum_value_signed(struct fy_decl *decl)
{
	/* only for enum values */
	if (!decl || decl->decl_type != FYDT_ENUM_VALUE)
		return LLONG_MAX;

	return decl->enum_value_decl.val.s;
}

uintmax_t fy_decl_enum_value_unsigned(struct fy_decl *decl)
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

const struct fy_source_range *
fy_decl_get_source_range(struct fy_decl *decl)
{
	if (!decl)
		return NULL;
	/* XXX maybe do it on demand? */
	return decl->source_range;
}

void fy_decl_clear_marker(struct fy_decl *decl)
{
	struct fy_decl *declp;

	if (!decl || !decl->marker)
		return;

	assert(decl->marker > 0);
	decl->marker--;

	for (declp = fy_decl_list_head(&decl->children); declp != NULL; declp = fy_decl_next(&decl->children, declp))
		fy_decl_clear_marker(declp);
}

void fy_decl_mark(struct fy_decl *decl)
{
	struct fy_decl *declp;

	if (!decl || (decl->flags & FYDF_MARK_IN_PROGRESS))
		return;

	decl->flags |= FYDF_MARK_IN_PROGRESS;

	decl->marker++;
	assert(decl->marker > 0);

	if (decl->imp)
		fy_import_mark(decl->imp);
	if (decl->parent)
		fy_decl_mark(decl->parent);

	for (declp = fy_decl_list_head(&decl->children); declp != NULL; declp = fy_decl_next(&decl->children, declp))
		fy_decl_mark(declp);

	if (decl->source_range && decl->source_range->source_file)
		fy_source_file_mark(decl->source_range->source_file);

	if (decl->type)
		fy_type_mark(decl->type);

	decl->flags &= ~FYDF_MARK_IN_PROGRESS;
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

	if (!decl)
		return NULL;

	if (!decl->raw_comment)
		return NULL;

	/* if we tried to parse always return what we found */
	if (decl->flags & FYDF_META_PARSED)
		return decl->fyd_yaml;

	if (!decl->fyd_yaml) {
		cooked_comment = fy_decl_get_cooked_comment(decl);
		if (cooked_comment)
			decl->fyd_yaml = get_yaml_document(cooked_comment);
	}
	decl->flags |= FYDF_META_PARSED;

	return decl->fyd_yaml;
}

const char *fy_decl_get_yaml_comment(struct fy_decl *decl)
{
	struct fy_reflection *rfl;
	struct fy_document *fyd;

	if (!decl)
		return NULL;
	rfl = decl->rfl;
	assert(rfl);

	if (decl->yaml_comment_generated)
		return decl->yaml_comment;

	if (!decl->yaml_comment) {
		fyd = fy_decl_get_yaml_annotation(decl);
		if (fyd) {
			decl->yaml_comment = fy_emit_document_to_string(fyd,
					FYECF_MODE_FLOW_ONELINE | FYECF_WIDTH_INF | FYECF_NO_ENDING_NEWLINE);
			RFL_ASSERT(decl->yaml_comment);
		}
	}
	decl->yaml_comment_generated = true;
	return decl->yaml_comment;

err_out:
	decl->yaml_comment_generated = true;
	return NULL;
}

struct fy_node *
fy_decl_get_yaml_node(struct fy_decl *decl, const char *path)
{
	struct fy_document *fyd;
	struct fy_node *fyn_root;

	assert(decl);

	fyd = fy_decl_get_yaml_annotation(decl);
	if (!fyd)
		return NULL;

	fyn_root = fy_document_root(fyd);
	if (!fyn_root)
		return NULL;

	return fy_node_by_path(fyn_root, path, FY_NT, FYNWF_DONT_FOLLOW);
}

const char *
fy_decl_get_yaml_string(struct fy_decl *decl, const char *path)
{
	struct fy_reflection *rfl;
	struct fy_document *fyd;
	struct fy_node *fyn_root, *fyn;
	struct fy_token *fyt;
	const char *text0;

	assert(decl);
	rfl = decl->rfl;
	assert(rfl);

	fyd = fy_decl_get_yaml_annotation(decl);
	if (!fyd)
		return NULL;

	fyn_root = fy_document_root(fyd);
	if (!fyn_root)
		return NULL;

	fyn = fy_node_by_path(fyn_root, path, FY_NT, FYNWF_DONT_FOLLOW);
	if (!fyn)
		return NULL;

	fyt = fy_node_get_scalar_token(fyn);
	RFL_ASSERT(fyt);

	text0 = fy_token_get_text0(fyt);
	RFL_ASSERT(text0);

	return text0;

err_out:
	return NULL;
}

int fy_decl_vscanf(struct fy_decl *decl, const char *fmt, va_list ap)
{
	struct fy_document *fyd;
	struct fy_node *fyn_root;

	assert(decl);

	fyd = fy_decl_get_yaml_annotation(decl);
	if (!fyd)
		return -1;

	fyn_root = fy_document_root(fyd);
	assert(fyn_root);
	if (!fyn_root)
		return -1;

	return fy_node_vscanf(fyn_root, fmt, ap);
}

int fy_decl_scanf(struct fy_decl *decl, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = fy_decl_vscanf(decl, fmt, ap);
	va_end(ap);

	return ret;
}

const char *fy_decl_get_yaml_name(struct fy_decl *decl)
{
	return fy_decl_get_yaml_string(decl, "/name");
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
	RFL_ASSERT(imp);
	memset(imp, 0, sizeof(*imp));

	imp->rfl = rfl;

	rfl->imp_curr = imp;
	rc = backend_import_setup(imp, user);
	rfl->imp_curr = NULL;
	RFL_ASSERT(!rc);

	return imp;

err_out:
	fy_import_destroy(imp);
	return NULL;
}

void fy_import_clear_marker(struct fy_import *imp)
{
	if (!imp || !imp->marker)
		return;

	assert(imp->marker > 0);
	imp->marker--;
}

void fy_import_mark(struct fy_import *imp)
{
	if (!imp)
		return;

	imp->marker++;
	assert(imp->marker > 0);
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

#ifndef _WIN32
	realname = realpath(filename, NULL);
#else
	realname = strdup(filename);
#endif
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
	RFL_ASSERT(srcf);
	memset(srcf, 0, sizeof(*srcf));

	srcf->filename = strdup(filename);
	RFL_ASSERT(srcf->filename);

#ifndef _WIN32
	srcf->realpath = realpath(filename, NULL);
#else
	srcf->realpath = strdup(filename);
#endif
	RFL_ASSERT(srcf->realpath);

	assert(rfl->next_source_file_id >= 0);
	srcf->id = rfl->next_source_file_id++;

	return srcf;

err_out:
	fy_source_file_destroy(srcf);
	return NULL;
}

void fy_source_file_clear_marker(struct fy_source_file *srcf)
{
	if (!srcf || !srcf->marker)
		return;

	assert(srcf->marker > 0);
	srcf->marker--;
}

void fy_source_file_mark(struct fy_source_file *srcf)
{
	if (!srcf)
		return;

	srcf->marker++;
	assert(srcf->marker > 0);
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

void fy_unresolved_dep_destroy(struct fy_unresolved_dep *udep)
{
	if (!udep)
		return;
	free(udep);
}

int fy_unresolved_dep_register_wrapper(struct fy_type_info_wrapper *tiw)
{
	struct fy_type *ft;
	struct fy_reflection *rfl;
	struct fy_unresolved_dep *udep;

	ft = fy_type_from_info_wrapper(tiw);
	if (!ft)
		return -1;
	rfl = ft->rfl;

	udep = malloc(sizeof(*udep));
	if (!udep)
		return -1;
	memset(udep, 0, sizeof(*udep));
	udep->tiw = tiw;

	fy_unresolved_dep_list_add_tail(&rfl->unresolved_deps, udep);

	return 0;
}

int fy_type_register_unresolved(struct fy_type *ft)
{
	struct fy_reflection *rfl;
	struct fy_unresolved_dep *udep;

	if (!ft || !(ft->flags & FYTF_UNRESOLVED))
		return -1;

	/* elaborated types are never registered */
	if (ft->flags & FYTF_ELABORATED)
		return 0;

	rfl = ft->rfl;
	for (udep = fy_unresolved_dep_list_head(&rfl->unresolved_deps);
	     udep != NULL;
	     udep = fy_unresolved_dep_next(&rfl->unresolved_deps, udep)) {

		if (fy_type_from_info_wrapper(udep->tiw) == ft)
			return -1;
	}

	return fy_unresolved_dep_register_wrapper(&ft->tiw);
}

void fy_type_unregister_unresolved(struct fy_type *ft)
{
	struct fy_reflection *rfl;
	struct fy_unresolved_dep *udep;

	if (!ft || (ft->flags & FYTF_UNRESOLVED))
		return;

	/* elaborated types are never registered */
	if (ft->flags & FYTF_ELABORATED)
		return;

	rfl = ft->rfl;
	for (udep = fy_unresolved_dep_list_head(&rfl->unresolved_deps);
	     udep != NULL;
	     udep = fy_unresolved_dep_next(&rfl->unresolved_deps, udep)) {
		if (fy_type_from_info_wrapper(udep->tiw) == ft)
			break;
	}

	if (udep) {
		fy_unresolved_dep_list_del(&rfl->unresolved_deps, udep);
		fy_unresolved_dep_destroy(udep);
	}
}

static int fy_reflection_setup(struct fy_reflection *rfl, const struct fy_reflection_internal_cfg *rflic);
static void fy_reflection_cleanup(struct fy_reflection *rfl);

struct fy_type *
fy_reflection_get_primary_type(struct fy_reflection *rfl, enum fy_type_kind type_kind, unsigned int quals)
{
	struct fy_type *ft, *ftt;
	enum fy_type_flags flags;
	const struct fy_type_info *ti;
	unsigned int base_id, id;
	char name[64];	/* this is enough primary types + quals */
	int ret FY_DEBUG_UNUSED;

	if (!rfl || !fy_type_kind_is_primary(type_kind))
		return NULL;

	quals &= (FY_QUALIFIER_CONST | FY_QUALIFIER_VOLATILE | FY_QUALIFIER_RESTRICT);

	id = ((int)type_kind - FYTK_PRIMARY_FIRST ) | quals;

	if (id >= ARRAY_SIZE(rfl->primary_types))
		return NULL;

	ft = rfl->primary_types[id];
	if (ft)
		return ft;

	base_id = id & ((1 << FYTK_PRIMARY_BITS) - 1);
	if (base_id >= ARRAY_SIZE(fy_type_info_primitive_table))
		return NULL;

	ti = &fy_type_info_primitive_table[base_id];

	ret = snprintf(name, sizeof(name), "%s%s%s%s",
		(id & FY_QUALIFIER_CONST) ? "const " : "",
		(id & FY_QUALIFIER_VOLATILE) ? "volatile " : "",
		(id & FY_QUALIFIER_RESTRICT) ? "restrict " : "",
		ti->name);
	RFL_ASSERT(ret > 0 && ret < (int)sizeof(name));

	flags = FYTF_SYNTHETIC |
		((quals & FY_QUALIFIER_CONST) ? FYTF_CONST : 0) |
		((quals & FY_QUALIFIER_VOLATILE) ? FYTF_VOLATILE : 0) |
		((quals & FY_QUALIFIER_RESTRICT) ? FYTF_RESTRICT : 0);

	ft = fy_type_create(rfl, ti->kind, flags, NULL, NULL, NULL, NULL, 0);
	RFL_ASSERT(ft);

	ft->id = (int)id;

	ft->size = ti->size;
	ft->align = ti->align;

	for (ftt = fy_type_list_head(&rfl->types); ftt != NULL; ftt = fy_type_next(&rfl->types, ftt)) {
		if (ftt->id > ft->id)
			break;
	}

	if (!ftt)
		fy_type_list_add_tail(&rfl->types, ft);
	else
		fy_type_list_insert_before(&rfl->types, ftt, ft);

	rfl->primary_types[id] = ft;

	return ft;

err_out:
	return NULL;
}

static int fy_reflection_setup(struct fy_reflection *rfl, const struct fy_reflection_internal_cfg *rflic)
{
	struct fy_diag_cfg diag_default_cfg;
	const struct fy_reflection_backend_ops *ops;
	int rc;

	assert(rfl);
	memset(rfl, 0, sizeof(*rfl));

	fy_import_list_init(&rfl->imports);
	fy_source_file_list_init(&rfl->source_files);
	fy_type_list_init(&rfl->types);
	fy_decl_list_init(&rfl->decls);
	fy_unresolved_dep_list_init(&rfl->unresolved_deps);

	/* basic checks */
	if (!rflic || !rflic->backend || !rflic->backend->ops)
		goto err_out;

	ops = rflic->backend->ops;

	rfl->cfg = *rflic;

	if (!rfl->cfg.diag) {
		fy_diag_cfg_default(&diag_default_cfg);
		rfl->diag = fy_diag_create(&diag_default_cfg);
		if (!rfl->diag) {
			fprintf(stderr, "%s: fy_diag_create() failed\n", __func__);
			goto err_out;
		}
	} else
		rfl->diag = fy_diag_ref(rfl->cfg.diag);

	/* all methods must be non NULL */
	RFL_ERROR_CHECK(ops->reflection_setup &&
			ops->reflection_cleanup &&
			ops->import_setup &&
			ops->import_cleanup &&
			ops->type_setup &&
			ops->type_cleanup &&
			ops->decl_setup &&
			ops->decl_cleanup,
			"NULL methods not allowed");

	rfl->next_type_id = 0;
	rfl->next_decl_id = 0;
	rfl->next_source_file_id = 0;
	rfl->next_anonymous_struct_id = 0;
	rfl->next_anonymous_union_id = 0;
	rfl->next_anonymous_enum_id = 0;

	/* make sure that we don't have more primaries than we can handle */
	RFL_ASSERT(rfl->next_type_id < FY_USER_DEFINED_ID_START);
	RFL_ASSERT(rfl->next_type_id < FY_TYPE_ID_OFFSET);
	RFL_ASSERT(rfl->next_decl_id < FY_DECL_ID_OFFSET);

	/* the regular user defined types start from here */
	rfl->next_type_id = FY_TYPE_ID_OFFSET;
	rfl->next_decl_id = FY_DECL_ID_OFFSET;

	rc = backend_reflection_setup(rfl);
	RFL_ASSERT(!rc);

	return 0;

err_out:
	fy_reflection_cleanup(rfl);
	return -1;
}

static void fy_reflection_cleanup(struct fy_reflection *rfl)
{
	struct fy_import *imp;
	struct fy_source_file *srcf;
	struct fy_type *ft;
	struct fy_decl *decl;
	struct fy_unresolved_dep *udep;

	assert(rfl);

	while ((udep = fy_unresolved_dep_list_pop(&rfl->unresolved_deps)) != NULL)
		fy_unresolved_dep_destroy(udep);

	while ((ft = fy_type_list_pop(&rfl->types)) != NULL)
		fy_type_destroy(ft);

	while ((decl = fy_decl_list_pop(&rfl->decls)) != NULL)
		fy_decl_destroy(decl);

	while ((srcf = fy_source_file_list_pop(&rfl->source_files)) != NULL)
		fy_source_file_destroy(srcf);

	while ((imp = fy_import_list_pop(&rfl->imports)) != NULL)
		fy_import_destroy(imp);

	backend_reflection_cleanup(rfl);

	if (rfl->cparse_allocator) {
		fy_allocator_destroy(rfl->cparse_allocator);
		rfl->cparse_allocator = NULL;
	}

	if (rfl->diag) {
		fy_diag_unref(rfl->diag);
		rfl->diag = NULL;
	}
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

	if (size < 6 || memcmp(start, "yaml:", 5))
		return NULL;

	s = start;
	e = s + size;

	/* skip over yaml: */
	s += 5;

	/* skip over spaces and tabs */
	while (s < e && isblank((unsigned char)*s))
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
		advance = 0;
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
	const struct fy_source_range *source_range;
	struct fy_decl *declp;
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

	assert((unsigned int)decl->decl_type < FYDT_COUNT);
	printf("%s\t%c D#%u '%s':'%s'", tabs,
		decl->marker ? '*' : ' ',
		decl->id,
		decl_type_txt[decl->decl_type], decl->name);

	/* a corrupt packed blob can leave out any of the types */
	if (decl->type)
		printf(" -> T#%d '%s'%s", decl->type->id,
				decl->type->fullname ? decl->type->fullname : "<NULL>",
				(decl->type->flags & FYTF_UNRESOLVED) ? " (unresolved)" : "");
	else
		printf(" -> <NULL>");

	switch (decl->decl_type) {
	case FYDT_ENUM:
		if (decl->type && decl->type->dependent_type)
			printf(" \"%s\"", fy_type_kind_info_get_internal(decl->type->dependent_type->type_kind)->name);
		else
			printf(" \"<NULL>\"");
		break;
	case FYDT_ENUM_VALUE:
		if (!fy_decl_enum_value_is_unsigned(decl))
			printf(" %jd", fy_decl_enum_value_signed(decl));
		else
			printf(" %ju", fy_decl_enum_value_unsigned(decl));
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
		source_range = fy_decl_get_source_range(decl);
		if (source_range)
			printf(" %s@[%u:%u-%u:%u]", source_range->source_file->filename,
					source_range->start_line, source_range->start_column,
					source_range->end_line, source_range->end_column);
	}

	if (decl->flags & FYDF_IN_SYSTEM_HEADER)
		printf(" in-system-header");
	if (decl->flags & FYDF_FROM_MAIN_FILE)
		printf(" from-main-file");
	if (decl->flags & FYDF_META_PARSED)
		printf(" meta-parsed");

	printf("\n");

	for (declp = fy_decl_list_head(&decl->children); declp != NULL; declp = fy_decl_next(&decl->children, declp))
		fy_decl_dump(declp, start_level, no_location);
}

struct fy_decl *fy_type_get_anonymous_parent_decl(struct fy_type *ft)
{
	struct fy_reflection *rfl;
	struct fy_type *ftp;
	struct fy_decl *decl, *declc;

	if (!ft)
		return NULL;

	if (!(ft->flags & FYTF_ANONYMOUS))
		return NULL;

	rfl = ft->rfl;
	assert(rfl);

	for (ftp = fy_type_list_head(&rfl->types); ftp != NULL; ftp = fy_type_next(&rfl->types, ftp)) {

		decl = fy_type_decl(ftp);
		if (!decl)
			continue;

		for (declc = fy_decl_list_head(&decl->children); declc; declc = fy_decl_next(&decl->children, declc)) {
			if (declc->type == ft)
				return declc;
		}
	}
	return NULL;
}

static struct fy_type *fy_type_anonymous_parent_type(struct fy_type *ft)
{
	struct fy_decl *decl;

	decl = fy_type_get_anonymous_parent_decl(ft);
	return decl && decl->parent ? decl->parent->type : NULL;
}

size_t fy_type_eponymous_offset(struct fy_type *ft)
{
	size_t offset;
	struct fy_decl *decl;
	struct fy_type *slow, *fast;

	if (!ft)
		return 0;

	offset = 0;
	slow = fast = ft;
	while ((decl = fy_type_get_anonymous_parent_decl(ft)) != NULL) {
		if (decl->decl_type != FYDT_FIELD || !decl->parent)
			return 0;
		offset += decl->field_decl.byte_offset;
		ft = decl->parent->type;

		slow = fy_type_anonymous_parent_type(slow);
		fast = fy_type_anonymous_parent_type(fast);
		if (fast)
			fast = fy_type_anonymous_parent_type(fast);
		if (slow && slow == fast)
			return 0;
	}

	return offset;
}

void fy_type_dump(struct fy_type *ft, bool no_location)
{
	struct fy_type *ft_unqual;
	struct fy_decl *decl;
	const struct fy_source_range *source_range;
	const char *comment;
	struct fy_type *ftd;

	comment = fy_decl_get_yaml_comment(ft->decl);	/* may be NULL for primitives and elaborated types */
	if (comment)
		printf("\t  // yaml: %s\n", comment);

	printf("\t%c T#%d", ft->marker ? '*' : ' ', ft->id);
	printf(" '%s'", ft->fullname ? ft->fullname : "<NULL>");

	printf(" size=%zu align=%zu", ft->size, ft->align);

	decl = fy_type_decl(ft);
	if (decl && !(ft->flags & FYTF_ELABORATED)) {
		printf(" -> D#%u", decl ? decl->id : -1);
		if (decl && !no_location) {
			source_range = fy_decl_get_source_range(decl);
			if (source_range)
				printf(" %s@[%u:%u-%u:%u]", source_range->source_file->filename,
						source_range->start_line, source_range->start_column,
						source_range->end_line, source_range->end_column);
		}
	} else if (ft->flags & FYTF_ELABORATED) {
		ft_unqual = fy_type_unqualified(ft);
		if (ft_unqual)
			printf(" -> T#%d '%s'", ft_unqual->id, ft_unqual->fullname ? ft_unqual->fullname : "<NULL>");
		else
			printf(" -> <NULL>");
	}

	if (fy_type_kind_is_dependent(ft->type_kind)) {
		if (!(ft->flags & FYTF_UNRESOLVED)) {
			ftd = ft->dependent_type;
			if (ftd) {
				printf(" -> T#%d '%s'", ftd->id, ftd->fullname);
			} else {
				printf(" -> T#<NULL>");
			}
		} else
			printf(" unresolved");
	}

	if (ft->flags & FYTF_ANONYMOUS)
		printf(" anonymous");

	if (ft->flags & FYTF_ANONYMOUS_RECORD_DECL)
		printf(" anonymous-record-decl");

	if (ft->flags & FYTF_ANONYMOUS_DEP)
		printf(" anonymous-dep");

	if (ft->flags & FYTF_ANONYMOUS_GLOBAL)
		printf(" anonymous-global");

	if (ft->flags & FYTF_SYNTHETIC)
		printf(" synthetic");

	if (ft->flags & FYTF_FAKE_RESOLVED)
		printf(" fake-resolved");

	if (ft->flags & FYTF_CONST)
		printf(" const");

	if (ft->flags & FYTF_VOLATILE)
		printf(" volatile");

	if (ft->flags & FYTF_RESTRICT)
		printf(" restrict");

	if (ft->flags & FYTF_ELABORATED)
		printf(" elaborated");

	if (ft->flags & FYTF_INCOMPLETE)
		printf(" incomplete");

	if (ft->flags & FYTF_NEEDS_NAME)
		printf(" needs-name");

	if (ft->flags & FYTF_UPDATE_TYPE_INFO)
		printf(" update-type-info");

	if (ft->flags & FYTF_TYPE_INFO_UPDATED)
		printf(" type-info-updated");

	printf("\n");
}

struct fy_reflection *fy_reflection_create_internal(const struct fy_reflection_internal_cfg *rflic)
{
	struct fy_reflection *rfl;
	int rc;

	rfl = malloc(sizeof(*rfl));
	if (!rfl)
		goto err_out;
	memset(rfl, 0, sizeof(*rfl));

	rc = fy_reflection_setup(rfl, rflic);
	if (rc)
		goto err_out;

	return rfl;

err_out:
	fy_reflection_destroy(rfl);
	return NULL;
}

void fy_reflection_set_userdata(struct fy_reflection *rfl, void *userdata)
{
	if (!rfl)
		return;
	rfl->userdata = userdata;
}

void *fy_reflection_get_userdata(struct fy_reflection *rfl)
{
	if (!rfl)
		return NULL;
	return rfl->userdata;
}

int fy_reflection_import(struct fy_reflection *rfl, const void *user)
{
	struct fy_import *imp = NULL;

	assert(rfl);

	imp = fy_import_create(rfl, user);
	RFL_ASSERT(imp);

	fy_import_list_add_tail(&rfl->imports, imp);

	return 0;

err_out:
	fy_import_destroy(imp);
	return -1;
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

void fy_reflection_renumber(struct fy_reflection *rfl)
{
	struct fy_decl *decl, *decl2;
	struct fy_type *ft;
	struct fy_source_file *srcf;

	/* the regular user defined types start from here */
	rfl->next_decl_id = FY_DECL_ID_OFFSET;
	for (decl = fy_decl_list_head(&rfl->decls); decl != NULL; decl = fy_decl_next(&rfl->decls, decl)) {
		decl->id = rfl->next_decl_id++;
		for (decl2 = fy_decl_list_head(&decl->children); decl2 != NULL; decl2 = fy_decl_next(&decl->children, decl2)) {
			decl2->id = rfl->next_decl_id++;
			/* note that there is no second level of decls */
			assert(fy_decl_list_empty(&decl2->children));
		}
	}

	rfl->next_type_id = FY_TYPE_ID_OFFSET;
	for (ft = fy_type_list_head(&rfl->types); ft != NULL; ft = fy_type_next(&rfl->types, ft)) {

		/* do not renumber the primary types */
		if (fy_type_kind_is_primary(ft->type_kind))
			continue;

		assert(ft->id >= FY_USER_DEFINED_ID_START);

		ft->id = rfl->next_type_id++;
	}

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
			   int num_imports, const void *import_cfgs[],
			   struct fy_diag *diag)
{
	const struct fy_reflection_backend *backend;
	struct fy_reflection_internal_cfg ricfg;
	struct fy_reflection *rfl = NULL;
	int i, rc;

	if (!backend_name || num_imports <= 0)
		return NULL;

	backend = fy_reflection_backend_lookup(backend_name);
	if (!backend)
		return NULL;

	memset(&ricfg, 0, sizeof(ricfg));
	ricfg.diag = diag;
	ricfg.backend_cfg = backend_cfg;
	ricfg.backend = backend;

	rfl = fy_reflection_create_internal(&ricfg);
	if (!rfl)
		goto err_out;

	/* do the imports */
	for (i = 0; i < num_imports; i++) {
		rc = fy_reflection_import(rfl, import_cfgs ? import_cfgs[i] : NULL);
		RFL_ASSERT(!rc);
	}

	// fy_reflection_dump(rfl, false, false);

	// fprintf(stderr, "\e[31m%s: dump of pending fixup\e[0m\n", __func__);
	// fy_type_info_wrapper_dump_pending(rfl);

	// fy_reflection_dump(rfl, false, true);

	return rfl;

err_out:
	fy_reflection_destroy(rfl);
	return NULL;
}

struct fy_reflection *
fy_reflection_from_import(const char *backend_name, const void *backend_cfg,
			  const void *import_cfg, struct fy_diag *diag)
{
	return fy_reflection_from_imports(backend_name, backend_cfg,
					  1, import_cfg ? &import_cfg : NULL,
					  diag);
}

static inline const struct fy_type_info *
fy_type_get_info(struct fy_type *ft)
{
	int ret;

	if (!ft)
		return NULL;

	ret = fy_type_update_info(ft);
	if (ret)
		return NULL;
	return &ft->tiw.type_info;
}

const struct fy_type_info *
fy_type_info_iterate(struct fy_reflection *rfl, void **prevp)
{
	const struct fy_type_info *ti;
	struct fy_type *ft;

	if (!rfl || !prevp)
		return NULL;

	ti = *prevp;
	ft = fy_type_from_info(ti);

	if (!ft)
		ft = fy_type_list_head(&rfl->types);
	else
		ft = fy_type_next(&rfl->types, ft);

	ti = fy_type_get_info(ft);	// NULL is OK

	*prevp = (void *)ti;
	return ti;
}

const struct fy_type_info *
fy_type_info_with_qualifiers(const struct fy_type_info *ti, enum fy_type_info_flags qual_flags)
{
	struct fy_type *ft;
	unsigned int quals;

	if (!ti)
		return NULL;

	ft = fy_type_from_info(ti);

	/* if we we given an elaborated type, unqualify it */
	if (ft->flags & FYTF_ELABORATED)
		ft = fy_type_unqualified(ft);

	if (!ft)
		return NULL;

	quals = ((qual_flags & FYTIF_CONST) ? FY_QUALIFIER_CONST : 0) |
		((qual_flags & FYTIF_VOLATILE) ? FY_QUALIFIER_VOLATILE : 0) |
		((qual_flags & FYTIF_RESTRICT) ? FY_QUALIFIER_RESTRICT : 0);

	ft = fy_type_with_qualifiers(ft, quals);

	return fy_type_get_info(ft);
}

const struct fy_type_info *
fy_type_info_unqualified(const struct fy_type_info *ti)
{
	struct fy_type *ft;

	if (!ti)
		return NULL;

	ft = fy_type_from_info(ti);

	/* if we we given an elaborated type, unqualify it */
	if (ft->flags & FYTF_ELABORATED)
		ft = fy_type_unqualified(ft);

	return fy_type_get_info(ft);
}

/*
 * Type text is parsed by the C declaration front end (cparse); this maps what
 * it makes onto the reflection's own types.
 */

static unsigned int cp_quals_to_fy(unsigned int quals)
{
	return ((quals & CPQ_CONST) ? FY_QUALIFIER_CONST : 0) |
	       ((quals & CPQ_VOLATILE) ? FY_QUALIFIER_VOLATILE : 0) |
	       ((quals & CPQ_RESTRICT) ? FY_QUALIFIER_RESTRICT : 0);
}

/* the reflection kind of a cparse builtin type, FYTK_INVALID if there is none */
static enum fy_type_kind cp_builtin_to_fy_kind(enum cp_type_kind k)
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

/*
 * The base of a type as the reflection names it: its kind, its name and the
 * qualifiers written on it. Returns false for anything that is not a plain
 * (non pointer, array or function) type. The name is not copied: it is owned by
 * the cparse type.
 */
static bool cp_type_base_info(const struct cp_type *cpt, enum fy_type_kind *kindp,
			      const char **namep, unsigned int *qualsp)
{
	const struct cp_type *u = cpt->unqual;
	enum fy_type_kind kind;

	/* atomic, vectors, complex and the rest have no reflection type */
	if (cpt->quals & CPQ_ATOMIC)
		return false;

	*qualsp = cp_quals_to_fy(cpt->quals);
	if (u->kind < CPTK_BUILTIN_END) {
		kind = cp_builtin_to_fy_kind(u->kind);
		if (kind == FYTK_INVALID)
			return false;
		*namep = fy_type_kind_name(kind);
	} else if (u->kind == CPTK_TYPEDEF) {
		kind = FYTK_TYPEDEF;
		*namep = u->name;
	} else if (u->kind == CPTK_ELABORATED) {
		kind = u->keyword == CPTK_STRUCT ? FYTK_STRUCT :
		       u->keyword == CPTK_UNION ? FYTK_UNION : FYTK_ENUM;
		*namep = u->name;
	} else {
		return false;
	}
	if (!*namep)
		return false;
	*kindp = kind;
	return true;
}

/* a cparse TU for parsing type text, sharing the reflection's allocator */
static struct cp_tu *rfl_cp_tu_create(struct fy_reflection *rfl)
{
	struct fy_auto_allocator_cfg acfg;
	struct cp_options opts;

	if (!rfl->cparse_allocator) {
		memset(&acfg, 0, sizeof(acfg));
		acfg.scenario = FYAST_PER_TAG_FREE;
		rfl->cparse_allocator = fy_allocator_create("auto", &acfg);
		if (!rfl->cparse_allocator)
			return NULL;
	}

	memset(&opts, 0, sizeof(opts));
	opts.allocator = rfl->cparse_allocator;
	opts.std_c23 = true;		/* bool is a keyword */
	return cp_tu_create(&opts);
}

/* report what cparse said, as the reflection's error */
static void rfl_cp_report(struct fy_reflection *rfl, struct cp_tu *tu,
			  const char *text, size_t len)
{
	if (tu->num_diags)
		rfl_error(rfl, "%s: '%.*s': %s\n", __func__, (int)len, text,
			  tu->diags[tu->num_diags - 1].msg);
}

char *fy_parse_c_base_type_dup(const char *s, size_t len, enum fy_type_kind *type_kindp,
			       unsigned int *qualsp)
{
	struct fy_allocator *a = NULL;
	struct fy_auto_allocator_cfg acfg;
	struct cp_options opts;
	struct cp_tu *tu = NULL;
	struct cp_type *cpt;
	enum fy_type_kind kind = FYTK_INVALID;
	const char *name = NULL;
	unsigned int quals = 0;
	char *ret = NULL;

	if (!s)
		return NULL;
	if (len == (size_t)-1)
		len = strlen(s);

	/* no reflection to borrow an allocator from: a short lived one */
	memset(&acfg, 0, sizeof(acfg));
	acfg.scenario = FYAST_PER_TAG_FREE;
	a = fy_allocator_create("auto", &acfg);
	if (!a)
		return NULL;
	memset(&opts, 0, sizeof(opts));
	opts.allocator = a;
	opts.std_c23 = true;
	tu = cp_tu_create(&opts);
	if (!tu)
		goto out;

	cpt = cp_parse_type_text(tu, s, len);
	if (!cpt || !cp_type_base_info(cpt, &kind, &name, &quals))
		goto out;

	ret = strdup(name);
	*type_kindp = kind;
	*qualsp = quals;
out:
	cp_tu_destroy(tu);
	fy_allocator_destroy(a);
	return ret;
}

/*
 * The reflection type for a cparse type, creating pointers and arrays as needed.
 * A chain of pointers and arrays can be very long: walk down to the base type
 * first and build the chain back up, so that the depth does not use the stack.
 */
static struct fy_type *fy_type_from_cp_type(struct fy_reflection *rfl, const struct cp_type *cpt)
{
	const struct cp_type *cur, *u, **chain = NULL, **nchain;
	size_t depth = 0, cap = 0, i;
	struct fy_type *ft = NULL, *ft_base = NULL;
	enum fy_type_kind type_kind;
	const char *type_name;
	unsigned int quals;
	bool ok;

	for (cur = cpt; ; cur = u->base) {
		u = cur->unqual;
		if (u->kind != CPTK_PTR && u->kind != CPTK_ARRAY)
			break;

		if (depth == cap) {
			cap = cap ? cap * 2 : 64;
			nchain = realloc(chain, cap * sizeof(*chain));
			RFL_ERROR_CHECK(nchain, "%s: out of memory", __func__);
			chain = nchain;
		}
		chain[depth++] = cur;
	}

	ok = cp_type_base_info(cur, &type_kind, &type_name, &quals);
	RFL_ERROR_CHECK(ok, "%s: unsupported type", __func__);

	ft_base = fy_base_type_lookup_by_kind(rfl, type_kind, type_name, strlen(type_name), quals);
	if (!ft_base && quals) {
		/* try without the qualifiers */
		ft_base = fy_base_type_lookup_by_kind(rfl, type_kind, type_name, strlen(type_name), 0);
		if (ft_base) {
			ft = fy_type_create_with_qualifiers(ft_base, quals, NULL);
			RFL_ASSERT(ft);
			fy_type_list_add_tail(&rfl->types, ft);
			ft_base = ft;
		}
	}
	RFL_ERROR_CHECK(ft_base, "%s: could not find base type '%s'\n", __func__, type_name);

	rfl_debug(rfl, "%s: > found base type: %s\n", __func__, ft_base->fullname);

	/* from the innermost level out */
	for (i = depth; i > 0; i--) {
		cur = chain[i - 1];
		u = cur->unqual;

		if (u->kind == CPTK_PTR) {
			RFL_ERROR_CHECK(!(cur->quals & CPQ_ATOMIC), "atomic types are not supported");
			quals = cp_quals_to_fy(cur->quals);
			ft = fy_type_lookup_pointer(ft_base, quals);
			if (!ft)
				ft = fy_type_create_pointer(ft_base, quals);
			RFL_ASSERT(ft);
			rfl_debug(rfl, "%s: > created pointer %s\n", __func__, ft->fullname);
		} else {
			RFL_ERROR_CHECK(u->count >= 0, "incomplete arrays are not supported");
			ft = fy_type_lookup_array(ft_base, 0, (uintmax_t)u->count);
			if (!ft)
				ft = fy_type_create_array(ft_base, 0, (uintmax_t)u->count);
			RFL_ASSERT(ft);
			rfl_debug(rfl, "%s: > created const array %s\n", __func__, ft->fullname);
		}
		ft_base = ft;
	}

	free(chain);
	return ft_base;

err_out:
	free(chain);
	return NULL;
}

struct fy_type *
fy_type_lookup_or_create(struct fy_reflection *rfl, const char *name, size_t name_len)
{
	struct fy_type *ft = NULL;
	struct cp_tu *tu = NULL;
	struct cp_type *cpt;

	if (!rfl || !name)
		return NULL;

	if (name_len == (size_t)-1)
		name_len = strlen(name);

	rfl_debug(rfl, "%s: > '%.*s'", __func__, (int)name_len, name);

	tu = rfl_cp_tu_create(rfl);
	RFL_ERROR_CHECK(tu, "%s: failed to create the type parser", __func__);

	cpt = cp_parse_type_text(tu, name, name_len);
	if (!cpt) {
		rfl_cp_report(rfl, tu, name, name_len);
		goto err_out;
	}

	ft = fy_type_from_cp_type(rfl, cpt);

err_out:
	cp_tu_destroy(tu);
	return ft;
}

const struct fy_type_info *
fy_type_info_lookup(struct fy_reflection *rfl, const char *name)
{
	struct fy_type *ft;
	const struct fy_type_info *ti;

	ft = fy_type_lookup_or_create(rfl, name, (size_t)-1);
	if (!ft)
		return NULL;

	ti = fy_type_get_info(ft);
	if (!ti)
		return NULL;

	return ti;
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

const char *
fy_type_info_prefixless_name(const struct fy_type_info *ti)
{
	const char *fullname;
	size_t adv;

	if (!ti)
		return NULL;

	fullname = ti->name;
	switch (ti->kind) {
	case FYTK_STRUCT:
		adv = 7;	/* 'struct ' */
		break;
	case FYTK_UNION:
		adv = 6;	/* 'union ' */
		break;
	case FYTK_ENUM:
		adv = 5;	/* 'enum ' */
		break;
	default:
		adv = 0;	/* return the full name */
		break;
	}
	/* a corrupt import can leave the name empty, or make it the prefix */
	if (!fullname || strlen(fullname) <= adv)
		return "";

	assert(!adv || isspace(fullname[adv-1]));
	return fullname + adv;
}

/*
 * Is the declaration of this type spelled with an anonymous tag's body? A
 * typedef ends the walk: a use of the typedef names it, only the typedef's own
 * declaration holds the body.
 */
static bool fy_type_info_inline_anon(const struct fy_type_info *ti)
{
	int n;

	for (n = 0; ti && n < 256; ti = ti->dependent_type, n++) {
		if (ti->kind == FYTK_TYPEDEF)
			return false;
		if (ti->flags & FYTIF_ANONYMOUS)
			return true;
	}
	return false;
}

char *
fy_type_info_generate_name(const struct fy_type_info *ti, const char *field)
{
	struct fy_type *ft;
	unsigned int flags;

	ft = fy_type_from_info(ti);
	if (!ft)
		return NULL;

	flags = 0;
	if (fy_type_info_inline_anon(ti))
		flags |= FYTGTF_NO_TYPE;

	return fy_type_generate_c_declaration(ft, field, flags);
}

char *
fy_field_info_generate_name(const struct fy_field_info *fi)
{
	struct fy_type *ft;
	struct fy_decl *decl;
	unsigned int flags;

	decl = fy_decl_from_field_info(fi);
	if (!decl)
		return NULL;

	ft = decl->type;
	assert(ft);

	flags = 0;
	if (fy_type_info_inline_anon(fi->type_info))
		flags |= FYTGTF_NO_TYPE;

	return fy_type_generate_c_declaration(ft, decl->name, flags);
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
	struct fy_type *ft;
	struct fy_decl *decl, *declf;
	const char *field_name;
	int idx;

	ft = fy_type_from_info(ti);
	if (!ft)
		return NULL;

	decl = fy_type_decl(ft);
	if (!decl)
		return NULL;

	idx = 0;
	for (declf = fy_decl_list_head(&decl->children); declf != NULL; declf = fy_decl_next(&decl->children, declf)) {
		assert(fy_decl_type_is_field(declf->decl_type));

		field_name = fy_decl_get_yaml_name(declf);
		if (!field_name)
			field_name = declf->name;
		if (!field_name)
			continue;

		if (!strcmp(field_name, name))
			return ti->fields + idx;
		idx++;
	}
	return NULL;
}

const struct fy_field_info *
fy_type_info_lookup_field_by_enum_value(const struct fy_type_info *ti, intmax_t val)
{
	struct fy_type *ft;
	struct fy_decl *decl, *declf;
	unsigned int idx;

	ft = fy_type_from_info(ti);
	if (!ft || ft->type_kind != FYTK_ENUM)
		return NULL;

	decl = fy_type_decl(ft);
	if (!decl)
		return NULL;

	idx = 0;
	for (declf = fy_decl_list_head(&decl->children); declf != NULL; declf = fy_decl_next(&decl->children, declf)) {
		assert(declf->decl_type == FYDT_ENUM_VALUE);
		if (declf->enum_value_decl.val.s == val)
			return ti->fields + idx;
		idx++;
	}
	return NULL;
}

const struct fy_field_info *
fy_type_info_lookup_field_by_unsigned_enum_value(const struct fy_type_info *ti, uintmax_t val)
{
	struct fy_type *ft;
	struct fy_decl *decl, *declf;
	unsigned int idx;

	ft = fy_type_from_info(ti);
	if (!ft || ft->type_kind != FYTK_ENUM)
		return NULL;

	decl = fy_type_decl(ft);
	if (!decl)
		return NULL;

	idx = 0;
	for (declf = fy_decl_list_head(&decl->children); declf != NULL; declf = fy_decl_next(&decl->children, declf)) {
		assert(declf->decl_type == FYDT_ENUM_VALUE);
		if (declf->enum_value_decl.val.u == val)
			return ti->fields + idx;
		idx++;
	}
	return NULL;
}

/* -------------------------------------------------------------------------
 * fy_type_info / fy_field_info field accessors (opaque-type public API)
 * ------------------------------------------------------------------------- */

enum fy_type_kind fy_type_info_get_kind(const struct fy_type_info *ti)
{
	return ti ? ti->kind : FYTK_INVALID;
}

enum fy_type_info_flags fy_type_info_get_flags(const struct fy_type_info *ti)
{
	return ti ? ti->flags : 0;
}

const char *fy_type_info_get_name(const struct fy_type_info *ti)
{
	return ti ? ti->name : NULL;
}

size_t fy_type_info_get_size(const struct fy_type_info *ti)
{
	return ti ? ti->size : 0;
}

size_t fy_type_info_get_align(const struct fy_type_info *ti)
{
	return ti ? ti->align : 0;
}

const struct fy_type_info *
fy_type_info_get_dependent_type(const struct fy_type_info *ti)
{
	return ti ? ti->dependent_type : NULL;
}

size_t fy_type_info_get_count(const struct fy_type_info *ti)
{
	return ti ? ti->count : 0;
}

const struct fy_field_info *
fy_type_info_get_field_at(const struct fy_type_info *ti, size_t idx)
{
	if (!ti || !ti->fields || idx >= ti->count)
		return NULL;
	return ti->fields + idx;
}

enum fy_field_info_flags fy_field_info_get_flags(const struct fy_field_info *fi)
{
	return fi ? fi->flags : 0;
}

const struct fy_type_info *
fy_field_info_get_parent(const struct fy_field_info *fi)
{
	return fi ? fi->parent : NULL;
}

const char *fy_field_info_get_name(const struct fy_field_info *fi)
{
	return fi ? fi->name : NULL;
}

const struct fy_type_info *
fy_field_info_get_type_info(const struct fy_field_info *fi)
{
	return fi ? fi->type_info : NULL;
}

size_t fy_field_info_get_offset(const struct fy_field_info *fi)
{
	return fi ? fi->offset : 0;
}

size_t fy_field_info_get_bit_offset(const struct fy_field_info *fi)
{
	return fi ? fi->bit_offset : 0;
}

size_t fy_field_info_get_bit_width(const struct fy_field_info *fi)
{
	return fi ? fi->bit_width : 0;
}

intmax_t fy_field_info_get_enum_value(const struct fy_field_info *fi)
{
	return fi ? fi->sval : 0;
}

uintmax_t fy_field_info_get_unsigned_enum_value(const struct fy_field_info *fi)
{
	return fi ? fi->uval : 0;
}

const char *fy_type_info_get_comment(const struct fy_type_info *ti)
{
	struct fy_type *ft;
	struct fy_decl *decl;

	ft = fy_type_from_info(ti);
	if (!ft)
		return NULL;

	decl = fy_type_decl(ft);
	if (!decl)
		return NULL;

	return fy_decl_get_cooked_comment(decl);
}

const char *fy_field_info_get_comment(const struct fy_field_info *fi)
{
	return fy_decl_get_cooked_comment(fy_decl_from_field_info(fi));
}

struct fy_document *fy_type_info_get_yaml_annotation(const struct fy_type_info *ti)
{
	struct fy_type *ft;
	struct fy_decl *decl;

	ft = fy_type_from_info(ti);
	if (!ft)
		return NULL;

	decl = fy_type_decl(ft);
	if (!decl)
		return NULL;

	return fy_decl_get_yaml_annotation(decl);
}

const char *fy_type_info_get_yaml_comment(const struct fy_type_info *ti)
{
	struct fy_type *ft;
	struct fy_decl *decl;

	ft = fy_type_from_info(ti);
	if (!ft)
		return NULL;

	decl = fy_type_decl(ft);
	if (!decl)
		return NULL;

	return fy_decl_get_yaml_comment(decl);
}

struct fy_document *fy_field_info_get_yaml_annotation(const struct fy_field_info *fi)
{
	return fy_decl_get_yaml_annotation(fy_decl_from_field_info(fi));
}

const char *fy_field_info_get_yaml_comment(const struct fy_field_info *fi)
{
	return fy_decl_get_yaml_comment(fy_decl_from_field_info(fi));
}

int
fy_type_info_get_id(const struct fy_type_info *ti)
{
	struct fy_type *ft;

	ft = fy_type_from_info(ti);
	return ft ? ft->id : -1;
}

struct c_type_info_array {
	unsigned int alloc, count;
	const struct fy_type_info **tis;
};

static inline void c_type_info_array_setup(struct c_type_info_array *ti_arr)
{
	memset(ti_arr, 0, sizeof(*ti_arr));
}

static inline void c_type_info_array_cleanup(struct c_type_info_array *ti_arr)
{
	if (!ti_arr)
		return;
	if (ti_arr->tis)
		free(ti_arr->tis);
}

static inline bool c_type_info_exists(struct c_type_info_array *ti_arr, const struct fy_type_info *ti)
{
	unsigned int i;

	for (i = 0; i < ti_arr->count; i++)
		if (ti_arr->tis[i] == ti)
			return true;

	return false;
}

static inline int c_type_info_add(struct c_type_info_array *ti_arr, const struct fy_type_info *ti)
{
	unsigned int new_alloc;
	const struct fy_type_info **new_tis;

	if (c_type_info_exists(ti_arr, ti))
		return -1;

	/* insert and grow */
	if (ti_arr->count >= ti_arr->alloc) {
		new_alloc = ti_arr->alloc * 2;
		if (!new_alloc)
			new_alloc = 16;
		new_tis = realloc(ti_arr->tis, new_alloc * sizeof(*ti_arr->tis));
		if (!new_tis)
			goto err_out;
		ti_arr->alloc = new_alloc;
		ti_arr->tis = new_tis;
	}
	assert(ti_arr->count < ti_arr->alloc);
	ti_arr->tis[ti_arr->count++] = ti;

	return 0;

err_out:
	return -1;
}

static inline int c_type_info_push(struct c_type_info_array *ti_arr, const struct fy_type_info *ti)
{
	return c_type_info_add(ti_arr, ti);
}

static inline const struct fy_type_info *c_type_info_pop(struct c_type_info_array *ti_arr)
{
	const struct fy_type_info *ti;

	if (!ti_arr->count)
		return NULL;
	ti = ti_arr->tis[--ti_arr->count];
	ti_arr->tis[ti_arr->count] = NULL;
	return ti;
}

#define FCGTI_STACK	0
#define FCGTI_DECL	1
#define FCGTI_FWD_DECL	2
#define FCGTI_ANON_DECL	3
#define FCGTI_COUNT	4

struct fy_c_generator {
	struct fy_reflection *rfl;
	enum fy_c_generation_flags flags;
	struct c_type_info_array ti_arrs[FCGTI_COUNT];
	const char *comment_pfx;
	FILE *fp;
};

static inline FY_UNUSED bool
fy_c_generator_any_comments(struct fy_c_generator *cgen)
{
	return (cgen->flags & FYCGF_COMMENT_MASK) == FYCGF_COMMENT_NONE;
}

static inline bool fy_c_generator_raw_comments(struct fy_c_generator *cgen)
{
	return (cgen->flags & FYCGF_COMMENT_MASK) == FYCGF_COMMENT_RAW;
}

static inline bool fy_c_generator_yaml_comments(struct fy_c_generator *cgen)
{
	return (cgen->flags & FYCGF_COMMENT_MASK) == FYCGF_COMMENT_YAML;
}

void fy_c_generator_setup(struct fy_c_generator *cgen, struct fy_reflection *rfl,
			  enum fy_c_generation_flags flags, FILE *fp)
{
	unsigned int i;

	memset(cgen, 0, sizeof(*cgen));

	cgen->rfl = rfl;
	cgen->flags = flags;
	cgen->fp = fp;

	for (i = 0; i < ARRAY_SIZE(cgen->ti_arrs); i++)
		c_type_info_array_setup(&cgen->ti_arrs[i]);

	switch (flags & FYCGF_COMMENT_MASK) {
	case FYCGF_COMMENT_NONE:
	case FYCGF_COMMENT_RAW:
	default:
		cgen->comment_pfx = "";
		break;
	case FYCGF_COMMENT_YAML:
		cgen->comment_pfx = "yaml: ";
		break;
	}
}

void fy_c_generator_cleanup(struct fy_c_generator *cgen)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cgen->ti_arrs); i++)
		c_type_info_array_cleanup(&cgen->ti_arrs[i]);
}

static inline bool c_in_ti_stack(struct fy_c_generator *cgen, const struct fy_type_info *ti)
{
	return c_type_info_exists(&cgen->ti_arrs[FCGTI_STACK], ti);
}

static inline bool c_decl_exists(struct fy_c_generator *cgen, const struct fy_type_info *ti)
{
	return c_type_info_exists(&cgen->ti_arrs[FCGTI_DECL], ti);
}

static inline bool c_fwd_decl_exists(struct fy_c_generator *cgen, const struct fy_type_info *ti)
{
	return c_type_info_exists(&cgen->ti_arrs[FCGTI_FWD_DECL], ti);
}

static inline bool c_any_decl_exists(struct fy_c_generator *cgen, const struct fy_type_info *ti)
{
	return c_decl_exists(cgen, ti) || c_fwd_decl_exists(cgen, ti);
}

static inline bool c_anon_decl_exists(struct fy_c_generator *cgen, const struct fy_type_info *ti)
{
	return c_type_info_exists(&cgen->ti_arrs[FCGTI_ANON_DECL], ti);
}

static inline int c_add_decl(struct fy_c_generator *cgen, const struct fy_type_info *ti)
{
	return c_type_info_add(&cgen->ti_arrs[FCGTI_DECL], ti);
}

static inline int c_add_fwd_decl(struct fy_c_generator *cgen, const struct fy_type_info *ti)
{
	return c_type_info_add(&cgen->ti_arrs[FCGTI_FWD_DECL], ti);
}

static inline int c_add_anon_decl(struct fy_c_generator *cgen, const struct fy_type_info *ti)
{
	return c_type_info_add(&cgen->ti_arrs[FCGTI_ANON_DECL], ti);
}

static inline int c_push_ti_stack(struct fy_c_generator *cgen, const struct fy_type_info *ti)
{
	return c_type_info_push(&cgen->ti_arrs[FCGTI_STACK], ti);
}

static inline const struct fy_type_info *c_pop_ti_stack(struct fy_c_generator *cgen)
{
	return c_type_info_pop(&cgen->ti_arrs[FCGTI_STACK]);
}

static int c_generate_single_base_type(struct fy_c_generator *cgen, FILE *fp, const struct fy_type_info *ti);

/* the indentation of a level: one tab, or the spaces that the flags ask for */
static size_t c_indent_len(unsigned int flags, int level)
{
	unsigned int spaces = (flags >> FYCGF_INDENT_SHIFT) & FYCGF_INDENT_MASK;

	return spaces ? (size_t)level * spaces : (size_t)level;
}

static void c_indent_fill(char *buf, unsigned int flags, int level)
{
	unsigned int spaces = (flags >> FYCGF_INDENT_SHIFT) & FYCGF_INDENT_MASK;
	size_t len = c_indent_len(flags, level);

	memset(buf, spaces ? ' ' : '\t', len);
	buf[len] = '\0';
}

/* the buffer is on the stack of the caller, so this is a macro */
#define c_indent_alloca(_var, _flags, _level) \
	do { \
		char *_buf = alloca(c_indent_len((_flags), (_level)) + 1); \
		\
		c_indent_fill(_buf, (_flags), (_level)); \
		(_var) = _buf; \
	} while(0)

static int c_comment(FILE *fp, const char *indent, const char *pfx, const char *comment)
{
	size_t len;
	const char *s, *e, *le;
	int lines = 0;

	if (!comment)
		return 0;

	if (!pfx)
		pfx = "";

	len = strlen(comment);
	s = comment;
	e = s + len;
	while (s < e) {
		le = strchr(s, '\n');
		len = le ? (size_t)(le - s) : strlen(s);
		fprintf(fp, "%s// %s%.*s\n", indent, pfx, (int)len, s);
		s += len + 1;
		lines++;
	}
	return lines;
}

static inline const struct fy_type_info *
get_final_ti(const struct fy_type_info *ti)
{
	while (ti && ti->dependent_type && ti->kind != FYTK_ENUM)
		ti = ti->dependent_type;
	return ti;
}

static const struct fy_type_info **
c_generate_collect_co_dependents(struct fy_reflection *rfl, const struct fy_type_info *start_ti)
{
	const struct fy_type_info *ti, *final_ti, *final_start_ti, **tis;
	void *prev;
	int pass, i, count;

	assert(rfl);
	assert(start_ti);

	if (start_ti->kind != FYTK_TYPEDEF)
		return NULL;

	final_start_ti = get_final_ti(start_ti);
	if (final_start_ti == start_ti)	/* nothing to do */
		return NULL;

	/* two passes */
	tis = NULL;
	count = 0;
	for (pass = 0; pass < 2; pass++) {
		prev = NULL;
		i = 0;
		while ((ti = fy_type_info_iterate(rfl, &prev)) != NULL) {

			if (ti == start_ti || ti->kind != FYTK_TYPEDEF)
				continue;

			final_ti = get_final_ti(ti);
			if (final_ti == ti || final_ti != final_start_ti)
				continue;

			/* `typedef A B;` names the first typedef, it does not share its body */
			if (!fy_type_info_inline_anon(ti->dependent_type))
				continue;

			if (tis) {
				/* the second pass must not see more types */
				if (i >= count)
					break;
				tis[i++] = ti;
			} else
				i++;
		}

		if (pass == 0) {
			count = i;
		       	if (count == 0)
				return NULL;
			tis = malloc(sizeof(*tis) * (count + 1));
			if (!tis)
				return NULL;
		}
	}
	tis[i] = NULL;	/* NULL terminate */
	return tis;
}

/*
 * The layout attribute a record needs to come back the way it is: packed when
 * its alignment is below what its members ask for, aligned when above.
 */
static const char *c_generate_record_attr(const struct fy_type_info *ti, char *buf, size_t bufsz)
{
	const struct fy_field_info *fi;
	size_t i, fa, maxfa = 1;

	if (!fy_type_kind_is_record(ti->kind) || !ti->count || !ti->align)
		return "";

	for (i = 0, fi = ti->fields; i < ti->count; i++, fi++) {
		if (!fi->type_info)
			continue;
		/* a flexible array has no alignment of its own */
		fa = fi->type_info->align;
		if (!fa && fi->type_info->kind == FYTK_INCOMPLETEARRAY && fi->type_info->dependent_type)
			fa = fi->type_info->dependent_type->align;
		if (fa > maxfa)
			maxfa = fa;
	}

	if (ti->align < maxfa) {
		if (ti->align == 1)
			return " __attribute__((packed))";
		snprintf(buf, bufsz, " __attribute__((packed, aligned(%zu)))", ti->align);
		return buf;
	}
	if (ti->align > maxfa) {
		snprintf(buf, bufsz, " __attribute__((aligned(%zu)))", ti->align);
		return buf;
	}
	return "";
}

static int c_generate_type_with_fields(struct fy_c_generator *cgen, FILE *fp, const struct fy_type_info *ti, bool is_base, int level, const char *field_name, bool no_first_pad)
{
	const struct fy_type_kind_info *tki;
	const struct fy_field_info *fi, *fi_fwd;
	const struct fy_type_info *final_ti, *fwd_final_ti;
	char *name = NULL, *fwd_name = NULL;
	const char *indent, *indent_plus_1, *comment;
	char attrbuf[64];
	size_t prev_end, field_align;
	bool pushed;
	size_t fa;
	size_t a;
	size_t natural;
	union {
		uintmax_t uval;
		intmax_t sval;
	} next_enum_value;
	bool force_explicit_enum;
	size_t i, j, explicits;
	int ret, lines = 0;

	assert(ti);
	assert(fy_type_kind_has_fields(ti->kind));

	/* don't generate top level qualified types i.e. const struct bar */
	if (is_base && (ti->flags & (FYTIF_CONST | FYTIF_VOLATILE | FYTIF_RESTRICT)))
		return 0;

	/* do not generate anonymous if not marked as global */
	if (is_base && (ti->flags & FYTIF_ANONYMOUS) && !(ti->flags & FYTIF_ANONYMOUS_GLOBAL))
		return 0;

	/* no content, and a fwd declaration already there */
	if ((ti->flags & FYTIF_UNRESOLVED) && c_fwd_decl_exists(cgen, ti))
		return 0;

	c_indent_alloca(indent, cgen->flags, level);

	tki = fy_type_kind_info_get(ti->kind);
	assert(tki);

	if (!no_first_pad) {
		comment = fy_c_generator_yaml_comments(cgen) ? fy_type_info_get_yaml_comment(ti) :
			  fy_c_generator_raw_comments(cgen) ? fy_type_info_get_comment(ti) :
			  NULL;
		if (comment) {
			ret = c_comment(fp, indent, "yaml: ", comment);
			if (ret < 0)
				goto err_out;
			lines += ret;
		}
		fprintf(fp, "%s", indent);
	}
	fprintf(fp, "%s%s%s%s%s",
		(ti->flags & FYTIF_CONST) ? "const " : "",
		(ti->flags & FYTIF_VOLATILE) ? "volatile " : "",
		(ti->flags & FYTIF_RESTRICT) ? "restrict " : "",
		tki->name,
		(ti->flags & FYTIF_UNRESOLVED) ? "" : c_generate_record_attr(ti, attrbuf, sizeof(attrbuf)));
	if (!(ti->flags & (FYTIF_ANONYMOUS | FYTIF_ANONYMOUS_RECORD_DECL)))
		fprintf(fp, " %s", fy_type_info_prefixless_name(ti));

	/* there is no content, just a fwd declaration */
	if (ti->flags & FYTIF_UNRESOLVED)
		goto out;

	c_indent_alloca(indent_plus_1, cgen->flags, level + 1);

	fprintf(fp, " {");

	fprintf(fp, "\n");
	lines++;

	/* probe enum for known patterns */
	force_explicit_enum = false;
	if (ti->kind == FYTK_ENUM) {

		next_enum_value.uval = 0;
		explicits = 0;
		for (i = 0, fi = ti->fields; i < ti->count; i++, fi++) {
			if (fi->flags & FYFIF_ENUM_UNSIGNED) {
				if (fi->uval != next_enum_value.uval)
					explicits++;
				if (fi->uval != UINTMAX_MAX)
					next_enum_value.uval = fi->uval + 1;
			} else {
				if (fi->sval != next_enum_value.sval)
					explicits++;
				if (fi->sval != INTMAX_MAX)
					next_enum_value.sval = fi->sval + 1;
			}
		}

		/* if we have more explicit values that half of the total force explicit */
		force_explicit_enum = explicits > (ti->count / 2);
	}

	next_enum_value.uval = 0;
	/* a struct member placed further than its type needs carries an aligned attribute */
	prev_end = 0;
	for (i = 0, fi = ti->fields; i < ti->count; i++, fi++) {

		field_align = 0;
		/* a packed record places its members by its own rule; an aligned one does not */
		if (ti->kind == FYTK_STRUCT && !(fi->flags & FYFIF_BITFIELD) && fi->type_info &&
		    fi->type_info->align &&
		    !strstr(c_generate_record_attr(ti, attrbuf, sizeof(attrbuf)), "packed")) {
			fa = fi->type_info->align;
			natural = (prev_end + fa - 1) / fa * fa;

			if (fi->offset > natural) {
				for (a = fa * 2; a && a <= 4096; a *= 2)
					if ((prev_end + a - 1) / a * a == fi->offset)
						break;
				if (a && a <= 4096)
					field_align = a;
			}
		}
		if (!(fi->flags & FYFIF_BITFIELD))
			prev_end = fi->offset + (fi->type_info ? fi->type_info->size : 0);

		final_ti = get_final_ti(fi->type_info);

		/* skip field already generated */
		if (fy_type_info_inline_anon(fi->type_info) &&
		     c_anon_decl_exists(cgen, final_ti))
			continue;

		comment = fy_c_generator_yaml_comments(cgen) ? fy_field_info_get_yaml_comment(fi) :
			  fy_c_generator_raw_comments(cgen) ? fy_field_info_get_comment(fi) :
			  NULL;
		if (comment) {
			ret = c_comment(fp, indent_plus_1, "yaml: ", comment);
			if (ret < 0)
				goto err_out;
			lines += ret;
		}

		/* anonymous record decl struct foo { struct { int v; } ... } */
		if (fi->type_info->flags & FYTIF_ANONYMOUS_RECORD_DECL) {

			/* a record cannot contain a record that is in generation */
			if (!fy_type_kind_is_record(fi->type_info->kind) ||
			    c_in_ti_stack(cgen, fi->type_info))
				goto err_out;

			ret = c_push_ti_stack(cgen, fi->type_info);
			if (ret < 0)
				goto err_out;

			ret = c_generate_type_with_fields(cgen, fp, fi->type_info, false, level + 1, "", false);
			(void)c_pop_ti_stack(cgen);
			if (ret < 0)
				goto err_out;
			lines += ret;

			fprintf(fp, ";\n");
			lines++;

		} else if (!fy_type_info_inline_anon(fi->type_info)) {

			fprintf(fp, "%s", indent_plus_1);
			if (ti->kind == FYTK_ENUM) {
				fprintf(fp, "%s", fi->name);

				if (fi->flags & FYFIF_ENUM_UNSIGNED) {
					if (force_explicit_enum || fi->uval != next_enum_value.uval)
						fprintf(fp, " = %ju", fi->uval);
					if (fi->uval != UINTMAX_MAX)
						next_enum_value.uval = fi->uval + 1;
				} else {
					if (force_explicit_enum || fi->sval != next_enum_value.sval)
						fprintf(fp, " = %jd", fi->sval);
					if (fi->sval != INTMAX_MAX)
						next_enum_value.sval = fi->sval + 1;
				}

				fprintf(fp, ",");
			} else {
				name = fy_field_info_generate_name(fi);
				if (!name)
					goto err_out;
				fprintf(fp, "%s", name);
				if (field_align)
					fprintf(fp, " __attribute__((aligned(%zu)))", field_align);
				if (!(fi->flags & FYFIF_BITFIELD))
					fprintf(fp, ";");
				else
					fprintf(fp, ": %zu;", fi->bit_width);
				free(name);
				name = NULL;
			}
			fprintf(fp, "\n");
			lines++;
		} else {
			/* the anonymous type must have fields and not be in generation */
			if (!final_ti || !fy_type_kind_has_fields(final_ti->kind) ||
			    final_ti == ti || c_in_ti_stack(cgen, final_ti))
				goto err_out;

			name = fy_field_info_generate_name(fi);
			if (!name)
				goto err_out;

			/* an anonymous record in generation is on the stack already */
			pushed = !c_in_ti_stack(cgen, ti);
			if (pushed) {
				ret = c_push_ti_stack(cgen, ti);
				if (ret < 0)
					goto err_out;
			}

			ret = c_generate_type_with_fields(cgen, fp, final_ti, false, level + 1, name, false);
			if (pushed)
				(void)c_pop_ti_stack(cgen);
			if (ret < 0)
				goto err_out;
			lines += ret;

			for (j = i + 1, fi_fwd = fi + 1; j < ti->count; j++, fi_fwd++) {
				fwd_final_ti = get_final_ti(fi_fwd->type_info);
				if (fwd_final_ti == final_ti) {
					fwd_name = fy_field_info_generate_name(fi_fwd);
					if (!fwd_name)
						goto err_out;
					fprintf(fp, ", %s", fwd_name);
					free(fwd_name);
					fwd_name = NULL;
				}
			}

			ret = c_add_anon_decl(cgen, final_ti);
			if (ret)
				goto err_out;

			fprintf(fp, ";\n");
			lines++;

			free(name);
			name = NULL;
		}
	}

	fprintf(fp, "%s", indent);
	if (!field_name || !field_name[0])
		fprintf(fp, "}");
	else
		fprintf(fp, "} %s", field_name);
out:
	if (is_base) {
		fprintf(fp, ";\n");
		lines++;
	}
	return lines;

err_out:
	if (fwd_name)
		free(fwd_name);
	if (name)
		free(name);
	return -1;
}

/* typedef are top level only */
static int c_generate_typedef(struct fy_c_generator *cgen, FILE *fp, const struct fy_type_info *ti)
{
	const struct fy_type_info *final_ti;
	const struct fy_type_info **ti_codeps = NULL, **tip, *tit;
	char *name = NULL, *codep_name = NULL, *aligned_name = NULL;
	const char *comment;
	size_t len;
	int ret, lines = 0;

	/* not qualified typedef */
	if ((ti->flags & (FYTIF_CONST | FYTIF_VOLATILE | FYTIF_RESTRICT)))
		return 0;

	assert(ti->kind == FYTK_TYPEDEF);

	final_ti = get_final_ti(ti->dependent_type);

	/* all anons are generated once */
	if (fy_type_info_inline_anon(ti->dependent_type) && c_anon_decl_exists(cgen, final_ti))
		return 0;

	comment = fy_c_generator_yaml_comments(cgen) ? fy_type_info_get_yaml_comment(ti) :
		  fy_c_generator_raw_comments(cgen) ? fy_type_info_get_comment(ti) :
		  NULL;
	if (comment) {
		ret = c_comment(fp, "", "yaml: ", comment);
		if (ret < 0)
			goto err_out;
		lines += ret;
	}

	name = fy_type_info_generate_name(ti->dependent_type, fy_type_info_prefixless_name(ti));
	if (!name)
		goto err_out;

	/* the typedef has an alignment of its own, the aligned attribute sets it */
	if (ti->align && ti->dependent_type && ti->dependent_type->align &&
	    ti->align != ti->dependent_type->align) {
		len = strlen(name) + 40;
		aligned_name = malloc(len);
		if (!aligned_name)
			goto err_out;
		snprintf(aligned_name, len, "%s __attribute__((aligned(%zu)))", name, ti->align);
		free(name);
		name = aligned_name;
		aligned_name = NULL;
	}

	fprintf(fp, "typedef ");
	if (!fy_type_info_inline_anon(ti->dependent_type)) {
		fprintf(fp, "%s", name);
	} else {

		ret = c_generate_type_with_fields(cgen, fp, final_ti, false, 0, name, true);
		if (ret < 0)
			goto err_out;
		lines += ret;

		ti_codeps = c_generate_collect_co_dependents(cgen->rfl, ti);
		if (ti_codeps) {
			tip = ti_codeps;
			while ((tit = *tip++) != NULL) {

				codep_name = fy_type_info_generate_name(tit->dependent_type, fy_type_info_prefixless_name(tit));
				if (!codep_name)
					goto err_out;

				fprintf(fp, ", %s", codep_name);

				free(codep_name);
				codep_name = NULL;
			}
			free(ti_codeps);
			ti_codeps = NULL;
		}

		ret = c_add_anon_decl(cgen, final_ti);
		if (ret)
			goto err_out;
	}
	free(name);

	fprintf(fp, ";\n");
	lines++;

	return lines;

err_out:
	if (ti_codeps)
		free(ti_codeps);
	if (codep_name)
		free(codep_name);
	if (name)
		free(name);
	return -1;
}

/* what the compiler itself provides; generating it would redefine it */
static bool c_generate_is_compiler_builtin(const struct fy_type_info *ti)
{
	if (ti->kind != FYTK_TYPEDEF && ti->kind != FYTK_STRUCT)
		return false;
	if (!ti->name || (ti->flags & (FYTIF_ANONYMOUS | FYTIF_ANONYMOUS_RECORD_DECL)))
		return false;
	return !strcmp(ti->name, ti->kind == FYTK_TYPEDEF ? "__builtin_va_list" : "struct __va_list_tag");
}

static int c_generate_fwd_decls(struct fy_c_generator *cgen, FILE *fp, const struct fy_type_info *ti, bool is_base, int level, const struct fy_type_info *top_ti)
{
	const struct fy_field_info *fi;
	size_t i;
	int ret, lines = 0;
	bool output_fwd_decl;

	if (ti->flags & FYTIF_ELABORATED) {
		ti = fy_type_info_unqualified(ti);
		if (!ti)
			return -1;
	}

	if (c_in_ti_stack(cgen, ti))
		return 0;

	// fprintf(fp, "// %s:%s %d %u\n", fy_type_kind_name(ti->kind), ti->name, level, cgen->ti_stack_top);

	ret = c_push_ti_stack(cgen, ti);
	if (ret < 0)
		return -1;

	if (c_any_decl_exists(cgen, ti) || c_generate_is_compiler_builtin(ti))
		goto out;

	output_fwd_decl = (!(ti->flags & (FYTIF_ANONYMOUS | FYTIF_ANONYMOUS_RECORD_DECL)) &&
			   (ti->kind == FYTK_STRUCT || ti->kind == FYTK_UNION) &&
			   ti != top_ti) ||
			  (!is_base && (ti->flags & FYTIF_UNRESOLVED));

	if (output_fwd_decl) {

		if (ti->flags & FYTIF_UNRESOLVED) {
			fprintf(fp, "// incomplete\n");
			lines++;
		}
		if (ti->kind != FYTK_TYPEDEF) {
			fprintf(fp, "%s;\n", ti->name);
			lines++;

			ret = c_add_fwd_decl(cgen, ti);
			if (ret < 0)
				goto err_out;
		} else {
			ret = c_generate_single_base_type(cgen, fp, ti);
			if (ret < 0)
				goto err_out;
			lines += ret;
		}

	}

	assert(!fy_type_kind_is_dependent(ti->kind) || ti->dependent_type);

	if (fy_type_kind_has_fields(ti->kind)) {

		for (i = 0, fi = ti->fields; i < ti->count; i++, fi++) {
			if (fy_type_kind_is_primitive(fi->type_info->kind))
				continue;
			ret = c_generate_fwd_decls(cgen, fp, fi->type_info, false, level + 1, top_ti);
			if (ret < 0)
				goto err_out;
			lines += ret;
		}

	} else if (fy_type_kind_is_dependent(ti->kind) && ti->dependent_type &&
		   !fy_type_kind_is_primitive(ti->dependent_type->kind)) {

		ret = c_generate_fwd_decls(cgen, fp, ti->dependent_type, false, level + 1, top_ti);
		if (ret < 0)
			goto err_out;
		lines += ret;
	}

out:
	(void)c_pop_ti_stack(cgen);
	return lines;

err_out:
	lines = -1;
	goto out;
}

static bool c_generate_is_base_type(const struct fy_type_info *ti)
{
	if (c_generate_is_compiler_builtin(ti))
		return false;

	if ((ti->flags & (FYTIF_ANONYMOUS | FYTIF_ANONYMOUS_RECORD_DECL)) && ti->kind != FYTK_ENUM)
		return false;

	if (!fy_type_kind_is_named(ti->kind))
		return false;

	return true;
}

/*
 * The type a declaration needs defined before it: through pointers only a
 * typedef (a tag can be forward declared), through arrays also a record.
 */
static const struct fy_type_info *c_generate_needed_type(const struct fy_type_info *ti)
{
	const struct fy_type_info *t = ti;
	bool seen_ptr = false;
	int n;

	for (n = 0; t && fy_type_kind_is_like_ptr(t->kind) && t->dependent_type && n < 256; n++) {
		if (t->kind == FYTK_PTR)
			seen_ptr = true;
		t = t->dependent_type;
	}
	if (!t || t == ti)
		return ti;
	if (t->kind == FYTK_TYPEDEF || (!seen_ptr && fy_type_kind_has_fields(t->kind)))
		return t;
	return ti;
}

/* a type whose own dependencies have to be looked at: base types, and the bodies of inline anonymous records */
static bool c_generate_is_dep_candidate(const struct fy_type_info *ti)
{
	return c_generate_is_base_type(ti) ||
	       (fy_type_kind_is_record(ti->kind) &&
		(ti->flags & (FYTIF_ANONYMOUS | FYTIF_ANONYMOUS_RECORD_DECL)));
}

static int c_generate_direct_dep_types(struct fy_c_generator *cgen,
				       FILE *fp,
				       const struct fy_type_info *ti,
				       bool is_base,
				       int level,
				       const struct fy_type_info *top_ti);

/*
 * A function type only knows its argument types by their spelling; find the
 * typedefs and tags in it and have them generated first.
 */
static int c_generate_function_deps(struct fy_c_generator *cgen, FILE *fp,
				    const struct fy_type_info *ti, int level,
				    const struct fy_type_info *top_ti)
{
	const struct fy_type_info *dep;
	struct fy_type *ft;
	const char *s, *e, *kw = NULL;
	char *name;
	size_t len, kwlen = 0;
	int n, ret, lines = 0;

	for (n = 0; ti && fy_type_kind_is_like_ptr(ti->kind) && ti->dependent_type && n < 256; n++)
		ti = ti->dependent_type;
	if (!ti || ti->kind != FYTK_FUNCTION || !ti->name)
		return 0;

	for (s = ti->name; *s; s = e) {
		if (!(isalpha((unsigned char)*s) || *s == '_')) {
			e = s + 1;
			continue;
		}
		for (e = s; isalnum((unsigned char)*e) || *e == '_'; e++)
			;
		len = e - s;

		if ((len == 6 && !strncmp(s, "struct", 6)) || (len == 5 && !strncmp(s, "union", 5)) ||
		    (len == 4 && !strncmp(s, "enum", 4))) {
			kw = s;
			kwlen = len;
			continue;
		}

		if (kw) {
			name = malloc(kwlen + 1 + len + 1);
			if (!name)
				return -1;
			sprintf(name, "%.*s %.*s", (int)kwlen, kw, (int)len, s);
			kw = NULL;
		} else {
			name = strndup(s, len);
			if (!name)
				return -1;
		}
		ft = fy_type_lookup(cgen->rfl, name, strlen(name));
		free(name);
		if (!ft || (ft->type_kind != FYTK_TYPEDEF && !fy_type_kind_has_fields(ft->type_kind)))
			continue;
		dep = fy_type_get_info(ft);
		if (!dep)
			continue;

		if (fy_type_kind_is_record(dep->kind) && !(dep->flags & FYTIF_ANONYMOUS)) {
			/* a tag in an argument list only needs to be declared */
			if (c_any_decl_exists(cgen, dep) || c_generate_is_compiler_builtin(dep))
				continue;
			fprintf(fp, "%s;\n", dep->name);
			lines++;
			if (c_add_fwd_decl(cgen, dep) < 0)
				return -1;
			continue;
		}

		if (dep->kind == FYTK_TYPEDEF && dep->dependent_type &&
		    fy_type_kind_is_record(dep->dependent_type->kind) &&
		    !fy_type_info_inline_anon(dep->dependent_type)) {
			/* the same, through a typedef: the record's body can come later */
			ret = c_generate_single_base_type(cgen, fp, dep);
		} else {
			ret = c_generate_direct_dep_types(cgen, fp, dep, false, level + 1, top_ti);
		}
		if (ret < 0)
			return -1;
		lines += ret;
	}
	return lines;
}

static int c_generate_direct_dep_types(struct fy_c_generator *cgen,
				       FILE *fp,
				       const struct fy_type_info *ti,
				       bool is_base FY_UNUSED,
				       int level,
				       const struct fy_type_info *top_ti)
{
	const struct fy_field_info *fi;
	size_t i;
	int ret, lines = 0;
	bool output_decl;
	const struct fy_type_info *need;

	if (ti->flags & FYTIF_ELABORATED) {
		ti = fy_type_info_unqualified(ti);
		if (!ti)
			return -1;
	}

	/* only generate if it's a base type */
	if (!c_generate_is_dep_candidate(ti))
		return 0;

	if (c_in_ti_stack(cgen, ti))
		return 0;

	ret = c_push_ti_stack(cgen, ti);
	if (ret < 0)
		return -1;

	if (c_decl_exists(cgen, ti))
		goto out;

	/*
	 * `typedef struct S T;` only needs the tag, and what S holds may need T:
	 * declare the typedef before looking into the record.
	 */
	if (ti->kind == FYTK_TYPEDEF && ti->dependent_type &&
	    fy_type_kind_is_record(ti->dependent_type->kind) &&
	    !fy_type_info_inline_anon(ti->dependent_type)) {
		ret = c_generate_single_base_type(cgen, fp, ti);
		if (ret < 0)
			goto err_out;
		lines += ret;
	}

	if (fy_type_kind_has_fields(ti->kind)) {

		for (i = 0, fi = ti->fields; i < ti->count; i++, fi++) {

			need = c_generate_needed_type(fi->type_info);

			ret = c_generate_function_deps(cgen, fp, fi->type_info, level, top_ti);
			if (ret < 0)
				goto err_out;
			lines += ret;

			if (fy_type_kind_is_primitive(need->kind))
				continue;

			if (!c_generate_is_dep_candidate(need))
				continue;

			ret = c_generate_direct_dep_types(cgen, fp, need, false, level + 1, top_ti);
			if (ret < 0)
				goto err_out;
			lines += ret;
		}

	} else if (fy_type_kind_is_dependent(ti->kind) && ti->dependent_type) {

		ret = c_generate_function_deps(cgen, fp, ti->dependent_type, level, top_ti);
		if (ret < 0)
			goto err_out;
		lines += ret;
	}

	if (!fy_type_kind_has_fields(ti->kind) && fy_type_kind_is_dependent(ti->kind) && ti->dependent_type &&
		   !fy_type_kind_is_primitive(c_generate_needed_type(ti->dependent_type)->kind) &&
		   c_generate_is_dep_candidate(c_generate_needed_type(ti->dependent_type))) {

		ret = c_generate_direct_dep_types(cgen, fp, c_generate_needed_type(ti->dependent_type),
						  false, level + 1, top_ti);
		if (ret < 0)
			goto err_out;
		lines += ret;
	}

	/*
	 * What it is made of goes first, then the declaration itself; that one
	 * pushes the type on the stack again, so it has to be off it by then.
	 */
	(void)c_pop_ti_stack(cgen);
	output_decl = !(ti->flags & (FYTIF_ANONYMOUS | FYTIF_ANONYMOUS_RECORD_DECL)) && ti != top_ti;
	if (output_decl) {
		ret = c_generate_single_base_type(cgen, fp, ti);
		if (ret < 0)
			return -1;

		lines += ret;
	}
	return lines;

out:
	(void)c_pop_ti_stack(cgen);
	return lines;

err_out:
	lines = -1;
	goto out;
}

static int c_generate_single_base_type(struct fy_c_generator *cgen, FILE *fp, const struct fy_type_info *ti)
{
	int lines = 0, ret;

	if (c_decl_exists(cgen, ti) || c_generate_is_compiler_builtin(ti))
		return 0;

	if (ti->kind == FYTK_TYPEDEF) {
		ret = c_generate_typedef(cgen, fp, ti);
		if (ret < 0)
			goto err_out;
		lines += ret;

	} else if (fy_type_kind_has_fields(ti->kind)) {
		ret = c_generate_type_with_fields(cgen, fp, ti, true, 0, NULL, false);
		if (ret < 0)
			goto err_out;
		lines += ret;
	}

	ret = c_add_decl(cgen, ti);
	if (ret < 0)
		goto err_out;

	return lines;

err_out:
	return -1;
}

static int c_generate_base_type(struct fy_c_generator *cgen, FILE *fp, const struct fy_type_info *ti)
{
	int ret;
	int lines;

	assert(c_generate_is_base_type(ti));

	if (c_decl_exists(cgen, ti))
		return 0;

	lines = 0;
	ret = c_generate_fwd_decls(cgen, fp, ti, true, 0, ti);
	if (ret < 0)
		goto err_out;
	lines += ret;

	if (lines > 0) {
		fprintf(fp, "\n");
		lines++;
	}

	ret = c_generate_direct_dep_types(cgen, fp, ti, true, 0, ti);
	if (ret < 0)
		goto err_out;
	lines += ret;

	if (lines > 0) {
		fprintf(fp, "\n");
		lines++;
	}

	ret = c_generate_single_base_type(cgen, fp, ti);
	if (ret < 0)
		goto err_out;

	lines += ret;

	return lines;
err_out:
	return -1;
}

int fy_reflection_generate_c(struct fy_reflection *rfl, enum fy_c_generation_flags flags, FILE *fp)
{
	const struct fy_type_info *ti;
	void *prev = NULL;
	struct fy_c_generator cgen_local, *cgen = &cgen_local;
	int lines, ret, prev_lines, this_lines, xtra_lines;
	struct fy_memstream *fyms = NULL;
	FILE *one_fp = NULL;
	char *one_buf = NULL;
	size_t one_size = 0, wrsz;

	fy_c_generator_setup(cgen, rfl, flags, fp);

	prev = NULL;
	lines = 0;
	prev_lines = 1;
	while ((ti = fy_type_info_iterate(rfl, &prev)) != NULL) {

		if (!c_generate_is_base_type(ti))
			continue;

		fyms = fy_memstream_open(&one_fp);
		RFL_ASSERT(fyms);

		ret = c_generate_base_type(cgen, one_fp, ti);
		RFL_ASSERT(ret >= 0);

		one_buf = fy_memstream_close(fyms, &one_size);
		fyms = NULL;
		one_fp = NULL;
		RFL_ASSERT(one_buf);

		if (ret > 0) {
			this_lines = ret;

			xtra_lines = 0;
			if (prev_lines > 1 || this_lines > 1) {
				fprintf(cgen->fp, "\n");
				xtra_lines++;
			}

			wrsz = fwrite(one_buf, 1, one_size, cgen->fp);
			RFL_ASSERT(wrsz == one_size);

			prev_lines = this_lines;
			lines += this_lines + xtra_lines;
		}

		free(one_buf);
		one_buf = NULL;

	}

out:
	if (fyms)
		one_buf = fy_memstream_close(fyms, &one_size);
	if (one_buf)
		free(one_buf);

	fy_c_generator_cleanup(cgen);

	return lines;

err_out:
	lines = -1;
	goto out;
}

char *fy_reflection_generate_c_string(struct fy_reflection *rfl, enum fy_c_generation_flags flags)
{
	char *mbuf = NULL;
	size_t msize;
	struct fy_memstream *fyms = NULL;
	FILE *fp = NULL;
	int ret;

	fyms = fy_memstream_open(&fp);
	if (!fyms)
		return NULL;

	ret = fy_reflection_generate_c(rfl, flags, fp);

	mbuf = fy_memstream_close(fyms, &msize);

	if (ret < 0) {
		free(mbuf);
		return NULL;
	}

	return mbuf;
}
