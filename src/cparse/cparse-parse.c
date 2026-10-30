/*
 * cparse-parse.c - C declaration front end: declaration parser
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Derived from slimcc's parse.c (declaration specifiers, declarators and the
 * struct/union layout rules), which is derived from chibicc:
 *
 * Copyright (c) 2019 Rui Ueyama
 * Copyright (c) 2023-2026 Hsiang-Ying Fu
 *
 * Only declarations are parsed. Function bodies, initializers and
 * `_Static_assert`s are skipped; function and variable declarations are
 * parsed (to find the types they define) and not recorded.
 *
 * Error handling: an error records a diagnostic and sets ps->err; every
 * function then returns, the external declaration being parsed is abandoned
 * and parsing resumes after it (see recover()).
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cparse-private.h"

enum kw {
	KW_NONE = 0,
	/* storage class and function specifiers */
	KW_TYPEDEF, KW_EXTERN, KW_STATIC, KW_AUTO, KW_REGISTER, KW_THREAD_LOCAL,
	KW_INLINE, KW_NORETURN,
	/* qualifiers */
	KW_CONST, KW_VOLATILE, KW_RESTRICT, KW_ATOMIC,
	/* type specifiers */
	KW_VOID, KW_CHAR, KW_SHORT, KW_INT, KW_LONG, KW_FLOAT, KW_DOUBLE, KW_SIGNED,
	KW_UNSIGNED, KW_BOOL, KW_COMPLEX, KW_INT128, KW_FLOAT16, KW_FLOAT128,
	KW_VA_LIST,
	KW_STRUCT, KW_UNION, KW_ENUM, KW_TYPEOF,
	/* others */
	KW_ALIGNAS, KW_ATTRIBUTE, KW_EXTENSION, KW_ASM, KW_STATIC_ASSERT,
	KW_NONNULL, KW_NULLABLE, KW_NULL_UNSPEC,
};

struct cp_attrs {
	bool packed;
	int aligned;			/* 0: none */
	bool has_mode;
	int mode_bits;
	int64_t vector_bytes;		/* vector_size, 0: none */
};

struct cp_declspec {
	struct cp_type *type;
	struct cp_token *start;
	unsigned quals;
	bool is_typedef;
	bool is_extern;
	bool is_static;
	struct cp_attrs attrs;
};

struct cp_parser {
	struct cp_tu *tu;
	struct cp_hashmap kws;		/* name -> enum kw */
	struct cp_hashmap typedefs;	/* name -> struct cp_type * (the CPTK_TYPEDEF node) */
	struct cp_hashmap tags;		/* name -> struct cp_type * */
	struct cp_hashmap enum_consts;	/* name -> struct cp_enumerator * */
	struct cp_type *va_list_type;
	struct cp_expr_hooks hooks;
	struct cp_token *decl_start;	/* first token of the declaration being parsed */
	struct cp_decl *scope;		/* the record whose members are being parsed */
	bool err;
	bool in_params;
	bool text_mode;			/* cp_parse_type_text(): names are not resolved */
};

static struct cp_type *declarator(struct cp_parser *ps, struct cp_token **rest,
				  struct cp_token *tok, struct cp_type *ty,
				  struct cp_token **name_tok, struct cp_attrs *attrs);
static int declspec(struct cp_parser *ps, struct cp_token **rest, struct cp_token *tok,
		    struct cp_declspec *sp);
static struct cp_type *apply_attr_types(struct cp_parser *ps, struct cp_type *ty,
					const struct cp_attrs *a);

/* helpers */

static bool is(const struct cp_token *tok, const char *s)
{
	return (tok->kind == CPT_PUNCT || tok->kind == CPT_IDENT) && cp_tok_equal(tok, s);
}

static void perr(struct cp_parser *ps, const struct cp_token *tok, const char *fmt, ...)
	FY_FORMAT(printf, 3, 4);

static void perr(struct cp_parser *ps, const struct cp_token *tok, const char *fmt, ...)
{
	va_list ap;
	char *msg;

	ps->err = true;
	va_start(ap, fmt);
	msg = cp_vformat(ps->tu, fmt, ap);
	va_end(ap);
	cp_error_tok(ps->tu, tok, "%s", msg ? msg : "parse error");
}

static void kw_init(struct cp_parser *ps)
{
	static const struct { const char *name; enum kw kw; } t[] = {
		{ "typedef", KW_TYPEDEF }, { "extern", KW_EXTERN }, { "static", KW_STATIC },
		{ "auto", KW_AUTO }, { "register", KW_REGISTER },
		{ "_Thread_local", KW_THREAD_LOCAL }, { "__thread", KW_THREAD_LOCAL },
		{ "thread_local", KW_THREAD_LOCAL },
		{ "inline", KW_INLINE }, { "__inline", KW_INLINE }, { "__inline__", KW_INLINE },
		{ "_Noreturn", KW_NORETURN },
		{ "const", KW_CONST }, { "__const", KW_CONST }, { "__const__", KW_CONST },
		{ "volatile", KW_VOLATILE }, { "__volatile", KW_VOLATILE },
		{ "__volatile__", KW_VOLATILE },
		{ "restrict", KW_RESTRICT }, { "__restrict", KW_RESTRICT },
		{ "__restrict__", KW_RESTRICT }, { "_Atomic", KW_ATOMIC },
		{ "void", KW_VOID }, { "char", KW_CHAR }, { "short", KW_SHORT }, { "int", KW_INT },
		{ "long", KW_LONG }, { "float", KW_FLOAT }, { "double", KW_DOUBLE },
		{ "signed", KW_SIGNED }, { "__signed", KW_SIGNED }, { "__signed__", KW_SIGNED },
		{ "unsigned", KW_UNSIGNED }, { "_Bool", KW_BOOL },
		{ "_Complex", KW_COMPLEX }, { "__complex__", KW_COMPLEX }, { "__complex", KW_COMPLEX },
		{ "__int128", KW_INT128 }, { "_Float16", KW_FLOAT16 }, { "__fp16", KW_FLOAT16 },
		{ "_Float128", KW_FLOAT128 }, { "__float128", KW_FLOAT128 },
		{ "__builtin_va_list", KW_VA_LIST },
		{ "struct", KW_STRUCT }, { "union", KW_UNION }, { "enum", KW_ENUM },
		{ "typeof", KW_TYPEOF }, { "__typeof", KW_TYPEOF }, { "__typeof__", KW_TYPEOF },
		{ "typeof_unqual", KW_TYPEOF },
		{ "_Alignas", KW_ALIGNAS }, { "alignas", KW_ALIGNAS },
		{ "__attribute__", KW_ATTRIBUTE }, { "__attribute", KW_ATTRIBUTE },
		{ "__extension__", KW_EXTENSION },
		{ "__asm__", KW_ASM }, { "__asm", KW_ASM }, { "asm", KW_ASM },
		{ "_Static_assert", KW_STATIC_ASSERT }, { "static_assert", KW_STATIC_ASSERT },
		{ "_Nonnull", KW_NONNULL }, { "__nonnull", KW_NONNULL },
		{ "_Nullable", KW_NULLABLE }, { "__nullable", KW_NULLABLE },
		{ "_Null_unspecified", KW_NULL_UNSPEC },
	};
	size_t i;
	struct cp_hashmap_entry *ent;

	for (i = 0; i < ARRAY_SIZE(t); i++) {
		ent = cp_hashmap_get_or_insert(ps->tu, &ps->kws, t[i].name, (int)strlen(t[i].name));
		if (ent)
			ent->val = (void *)(intptr_t)t[i].kw;
	}
	if (ps->tu->opts.std_c23) {
		ent = cp_hashmap_get_or_insert(ps->tu, &ps->kws, "bool", 4);
		if (ent)
			ent->val = (void *)(intptr_t)KW_BOOL;
	}
}

static enum kw kw_of(struct cp_parser *ps, const struct cp_token *tok)
{
	if (tok->kind != CPT_IDENT)
		return KW_NONE;
	return (enum kw)(intptr_t)cp_hashmap_get(&ps->kws, tok->loc, tok->len);
}

static struct cp_type *find_typedef(struct cp_parser *ps, const struct cp_token *tok)
{
	if (tok->kind != CPT_IDENT)
		return NULL;
	return cp_hashmap_get(&ps->typedefs, tok->loc, tok->len);
}

static bool is_type_kw(enum kw kw)
{
	switch (kw) {
	case KW_CONST: case KW_VOLATILE: case KW_RESTRICT: case KW_ATOMIC:
	case KW_VOID: case KW_CHAR: case KW_SHORT: case KW_INT: case KW_LONG:
	case KW_FLOAT: case KW_DOUBLE: case KW_SIGNED: case KW_UNSIGNED: case KW_BOOL:
	case KW_COMPLEX: case KW_INT128: case KW_FLOAT16: case KW_FLOAT128:
	case KW_VA_LIST:
	case KW_STRUCT: case KW_UNION: case KW_ENUM: case KW_TYPEOF:
	case KW_ATTRIBUTE: case KW_EXTENSION: case KW_ALIGNAS: case KW_NONNULL:
	case KW_NULLABLE: case KW_NULL_UNSPEC:
		return true;
	default:
		return false;
	}
}

static bool is_type_start(struct cp_parser *ps, const struct cp_token *tok)
{
	return is_type_kw(kw_of(ps, tok)) || find_typedef(ps, tok);
}

/* token after the group starting at tok, which is an opening ( [ or { */
static struct cp_token *skip_balanced(struct cp_parser *ps, struct cp_token *tok)
{
	struct cp_token *start = tok;
	int depth = 0;

	for (; tok->kind != CPT_EOF; tok = tok->next) {
		if (tok->kind != CPT_PUNCT)
			continue;
		if (is(tok, "(") || is(tok, "[") || is(tok, "{"))
			depth++;
		else if ((is(tok, ")") || is(tok, "]") || is(tok, "}")) && --depth == 0)
			return tok->next;
	}
	perr(ps, start, "unterminated '%.*s'", start->len, start->loc);
	return tok;
}

static const struct cp_token *tok_before(const struct cp_token *start, const struct cp_token *tok)
{
	const struct cp_token *t = start, *prev = start;

	for (; t && t != tok && t->kind != CPT_EOF; t = t->next)
		prev = t;
	return prev;
}

/* source ranges: the location of the root (outermost macro invocation) token */
static void decl_range(struct cp_decl *d, const struct cp_token *start,
		       const struct cp_token *last)
{
	const struct cp_token *rs = cp_tok_root(start), *rl = cp_tok_root(last);

	if (!rs->file)
		return;
	d->file = rs->file;
	d->start = (size_t)(rs->loc - rs->file->contents);
	if (rl->file == rs->file && rl->loc >= rs->loc)
		d->end = (size_t)(rl->loc + rl->len - rs->file->contents);
	else
		d->end = d->start + rs->len;
}

static struct cp_decl *new_decl(struct cp_parser *ps, enum cp_decl_kind kind,
				const char *name, struct cp_type *type,
				struct cp_decl *parent, const struct cp_token *start,
				const struct cp_token *last)
{
	struct cp_decl *d = cp_alloc(ps->tu, sizeof(*d));

	if (!d || cp_decl_add(ps->tu, d)) {
		ps->err = true;
		cp_error_at(ps->tu, NULL, NULL, "out of memory");
		return NULL;
	}
	d->kind = kind;
	d->name = name;
	d->type = type;
	d->parent = parent;
	decl_range(d, start, last);
	return d;
}

static const char *tok_name(struct cp_parser *ps, const struct cp_token *tok)
{
	return cp_strndup(ps->tu, tok->loc, tok->len);
}

/* constant expressions */

static int eval_const(struct cp_parser *ps, struct cp_token *tok, struct cp_token **rest,
		      struct cp_value *v)
{
	if (cp_eval_int_expr(ps->tu, tok, rest, v, &ps->hooks)) {
		ps->err = true;
		return -1;
	}
	return 0;
}

/* attributes */

/* the name without the __ decoration */
static bool attr_is(const struct cp_token *tok, const char *name)
{
	size_t n = strlen(name);

	if (tok->kind != CPT_IDENT)
		return false;
	if ((size_t)tok->len == n && !memcmp(tok->loc, name, n))
		return true;
	return (size_t)tok->len == n + 4 && !memcmp(tok->loc, "__", 2) &&
	       !memcmp(tok->loc + 2, name, n) && !memcmp(tok->loc + 2 + n, "__", 2);
}

static int mode_bits(const struct cp_token *tok)
{
	static const struct { const char *name; int bits; } t[] = {
		{ "QI", 8 }, { "byte", 8 }, { "HI", 16 }, { "SI", 32 }, { "DI", 64 },
		{ "TI", 128 }, { "word", 64 }, { "pointer", 64 },
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(t); i++)
		if (attr_is(tok, t[i].name))
			return t[i].bits;
	return 0;
}

/* tok is `(`, the parameters of one attribute; returns the token after `)` */
static struct cp_token *attr_params(struct cp_parser *ps, struct cp_token *name,
				    struct cp_token *tok, struct cp_attrs *a)
{
	struct cp_token *after = skip_balanced(ps, tok), *rest;
	struct cp_value v;

	if (ps->err)
		return after;

	if (attr_is(name, "aligned")) {
		if (is(tok->next, ")"))
			return after;
		if (!eval_const(ps, tok->next, &rest, &v) && v.val)
			a->aligned = (int)v.val;
	} else if (attr_is(name, "mode")) {
		if (tok->next->kind == CPT_IDENT) {
			a->has_mode = true;
			a->mode_bits = mode_bits(tok->next);
		}
	} else if (attr_is(name, "vector_size")) {
		if (!eval_const(ps, tok->next, &rest, &v))
			a->vector_bytes = (int64_t)v.val;
	}
	return after;
}

/* __attribute__((...)) and [[...]] sequences, any number of them */
static struct cp_token *parse_attributes(struct cp_parser *ps, struct cp_token *tok,
					 struct cp_attrs *a)
{
	struct cp_attrs dummy;
	struct cp_token *start;
	struct cp_token *name;
	struct cp_token *after;

	if (!a)
		a = &dummy;

	while (!ps->err) {
		if (kw_of(ps, tok) == KW_ATTRIBUTE) {
			start = tok;

			tok = tok->next;
			if (!is(tok, "(") || !is(tok->next, "(")) {
				perr(ps, start, "expected '((' after __attribute__");
				return tok;
			}
			tok = tok->next->next;
			while (!is(tok, ")") && tok->kind != CPT_EOF) {
				name = tok;

				if (is(tok, ",")) {
					tok = tok->next;
					continue;
				}
				if (tok->kind != CPT_IDENT) {
					perr(ps, tok, "expected an attribute name");
					return tok;
				}
				tok = tok->next;
				if (attr_is(name, "packed"))
					a->packed = true;
				if (is(tok, "("))
					tok = attr_params(ps, name, tok, a);
			}
			if (!is(tok, ")") || !is(tok->next, ")")) {
				perr(ps, start, "unterminated __attribute__");
				return tok;
			}
			tok = tok->next->next;
			continue;
		}
		if (is(tok, "[") && is(tok->next, "[")) {
			after = skip_balanced(ps, tok);

			/* [[ ... ]] is two groups */
			tok = after;
			continue;
		}
		break;
	}
	return tok;
}

/* an asm label (`__asm__("name")`) after a declarator */
static struct cp_token *skip_asm_label(struct cp_parser *ps, struct cp_token *tok)
{
	if (kw_of(ps, tok) == KW_ASM && is(tok->next, "("))
		return skip_balanced(ps, tok->next);
	return tok;
}

/* types */

static struct cp_type *adjust_param(struct cp_parser *ps, struct cp_type *ty)
{
	struct cp_type *c = cp_type_canonical(ty);

	if (c->kind == CPTK_ARRAY)
		return cp_type_pointer(ps->tu, c->base);
	if (c->kind == CPTK_FUNC)
		return cp_type_pointer(ps->tu, ty);
	return ty;
}

static struct cp_type *func_params(struct cp_parser *ps, struct cp_token **rest,
				   struct cp_token *tok, struct cp_type *ret)
{
	struct cp_type *fn, **params = NULL;
	int n = 0, cap = 0;
	bool variadic = false, noproto = false;
	bool saved_in_params = ps->in_params;
	struct cp_declspec sp;
	struct cp_token *name;
	struct cp_type *pty;
	struct cp_type **np;
	struct cp_token *t;
	struct cp_attrs pa;

	fn = cp_alloc(ps->tu, sizeof(*fn));
	if (!fn) {
		ps->err = true;
		return ret;
	}
	fn->kind = CPTK_FUNC;
	fn->unqual = fn;
	fn->base = ret;
	fn->size = 1;			/* GNU: sizeof a function is 1, libclang reports it */
	fn->align = 1;
	fn->count = -1;

	ps->in_params = true;

	if (is(tok, ")")) {
		noproto = true;
		tok = tok->next;
		goto done;
	}
	if (is(tok, "void") && is(tok->next, ")")) {
		tok = tok->next->next;
		goto done;
	}
	/* K&R identifier list: (a, b, c) */
	if (tok->kind == CPT_IDENT && !is_type_start(ps, tok) && kw_of(ps, tok) == KW_NONE &&
	    (is(tok->next, ",") || is(tok->next, ")"))) {
		noproto = true;
		goto knr;
	}

	while (!ps->err) {
		name = NULL;
		memset(&pa, 0, sizeof(pa));

		if (is(tok, "...")) {
			variadic = true;
			tok = tok->next;
			break;
		}
		memset(&sp, 0, sizeof(sp));
		if (declspec(ps, &tok, tok, &sp))
			break;
		pty = declarator(ps, &tok, tok, sp.type, &name, &pa);
		if (ps->err)
			break;
		tok = parse_attributes(ps, tok, NULL);
		pty = adjust_param(ps, cp_type_qualified(ps->tu, pty, 0));
		if (n == cap) {
			cap = cap ? cap * 2 : 8;
			np = cp_alloc(ps->tu, cap * sizeof(*np));
			if (!np) {
				ps->err = true;
				break;
			}
			if (n)
				memcpy(np, params, n * sizeof(*np));
			params = np;
		}
		params[n++] = pty;
		if (is(tok, ",")) {
			tok = tok->next;
			continue;
		}
		break;
	}
	if (!ps->err) {
		if (!is(tok, ")"))
			perr(ps, tok, "expected ')' at the end of the parameter list");
		else
			tok = tok->next;
	}
	goto done;

knr:
	/* skip the identifier list up to the closing paren */
	{
		t = tok;

		while (t->kind != CPT_EOF && !is(t, ")"))
			t = t->next;
		tok = is(t, ")") ? t->next : t;
	}

done:
	ps->in_params = saved_in_params;
	fn->params = params;
	fn->num_params = n;
	fn->variadic = variadic;
	fn->noproto = noproto;
	*rest = tok;
	return fn;
}

/* array dimensions and function parameter lists */
static struct cp_type *type_suffix(struct cp_parser *ps, struct cp_token **rest,
				   struct cp_token *tok, struct cp_type *ty)
{
	struct cp_value v;
	int64_t count = -1;
	struct cp_token *open, *t;
	size_t nd;
	size_t ne;

	if (ps->err) {
		*rest = tok;
		return ty;
	}

	if (is(tok, "(")) {
		ty = func_params(ps, &tok, tok->next, ty);
		tok = parse_attributes(ps, tok, NULL);
		*rest = tok;
		return ty;
	}

	if (is(tok, "[") && !is(tok->next, "[")) {
		open = tok;
		tok = tok->next;
		/* qualifiers and `static` in a parameter's array declarator */
		while (kw_of(ps, tok) == KW_CONST || kw_of(ps, tok) == KW_VOLATILE ||
		       kw_of(ps, tok) == KW_RESTRICT || kw_of(ps, tok) == KW_STATIC)
			tok = tok->next;

		if (is(tok, "]")) {
			count = -1;
		} else if (is(tok, "*") && is(tok->next, "]")) {
			tok = tok->next;
			count = -1;
		} else if (ps->in_params) {
			/*
			 * Only the outermost dimension is adjusted to a pointer, so
			 * the size still matters (`int (*)[3]`), but a parameter may
			 * also be a VLA: try quietly and fall back to unknown.
			 */
			nd = ps->tu->num_diags;
			ne = ps->tu->num_errors;

			if (!cp_eval_int_expr(ps->tu, tok, &t, &v, &ps->hooks) && is(t, "]")) {
				tok = t;
				count = (int64_t)v.val;
			} else {
				ps->tu->num_diags = nd;
				ps->tu->num_errors = ne;
				for (t = tok; t->kind != CPT_EOF && !is(t, "]"); t = t->next) {
					if (is(t, "(") || is(t, "[") || is(t, "{"))
						t = skip_balanced(ps, t);
					if (ps->err)
						break;
				}
				tok = t;
				count = -1;
			}
		} else {
			if (eval_const(ps, tok, &tok, &v)) {
				*rest = tok;
				return ty;
			}
			if (!v.is_unsigned && (int64_t)v.val < 0) {
				perr(ps, open, "array size is negative");
				*rest = tok;
				return ty;
			}
			count = (int64_t)v.val;
		}
		if (!is(tok, "]")) {
			perr(ps, tok, "expected ']'");
			*rest = tok;
			return ty;
		}
		tok = tok->next;
		/* the remaining dimensions are the element type of this one */
		ty = type_suffix(ps, &tok, tok, ty);
		*rest = tok;
		if (ps->err)
			return ty;
		if (cp_type_size(ty) < 0 && cp_type_canonical(ty)->kind != CPTK_ARRAY &&
		    !ps->text_mode) {
			perr(ps, open, "array has incomplete element type '%s'",
			     cp_type_spelling(ps->tu, ty));
			return ty;
		}
		if (count > 0 && cp_type_size(ty) > 0 &&
		    count > INT64_MAX / cp_type_size(ty)) {
			perr(ps, open, "array is too large");
			return ty;
		}
		return cp_type_array(ps->tu, ty, count);
	}

	*rest = tok;
	return ty;
}

static struct cp_type *pointers(struct cp_parser *ps, struct cp_token **rest,
				struct cp_token *tok, struct cp_type *ty,
				struct cp_attrs *attrs)
{
	unsigned quals;
	enum kw kw;

	for (;;) {
		quals = 0;

		tok = parse_attributes(ps, tok, attrs);
		if (!is(tok, "*") || tok->kind != CPT_PUNCT)
			break;
		tok = tok->next;
		ty = cp_type_pointer(ps->tu, ty);
		if (!ty) {
			ps->err = true;
			break;
		}
		for (;;) {
			kw = kw_of(ps, tok);

			if (kw == KW_CONST)
				quals |= CPQ_CONST;
			else if (kw == KW_VOLATILE)
				quals |= CPQ_VOLATILE;
			else if (kw == KW_RESTRICT)
				quals |= CPQ_RESTRICT;
			else if (kw == KW_ATOMIC)
				quals |= CPQ_ATOMIC;
			else if (kw == KW_ATTRIBUTE) {
				tok = parse_attributes(ps, tok, attrs);
				continue;
			} else if (kw == KW_NONNULL)
				quals |= CPQ_NONNULL;
			else if (kw == KW_NULLABLE)
				quals |= CPQ_NULLABLE;
			else if (kw == KW_NULL_UNSPEC)
				quals |= CPQ_NULL_UNSPEC;
			else
				break;
			tok = tok->next;
		}
		if (quals)
			ty = cp_type_qualified(ps->tu, ty, quals);
	}
	*rest = tok;
	return ty;
}

/* `(` starts a grouping declarator rather than a parameter list */
static bool is_grouping_paren(struct cp_parser *ps, struct cp_token *tok)
{
	struct cp_token *n = tok->next;

	if (is(n, "*") || is(n, "(") || is(n, "["))
		return n->kind == CPT_PUNCT && !is(n, "[");
	if (kw_of(ps, n) == KW_ATTRIBUTE)
		return true;
	return n->kind == CPT_IDENT && !is_type_start(ps, n) && kw_of(ps, n) == KW_NONE;
}

static struct cp_type *declarator(struct cp_parser *ps, struct cp_token **rest,
				  struct cp_token *tok, struct cp_type *ty,
				  struct cp_token **name_tok, struct cp_attrs *attrs)
{
	struct cp_token *start, *dummy;

	ty = pointers(ps, &tok, tok, ty, attrs);
	if (ps->err) {
		*rest = tok;
		return ty;
	}

	if (is(tok, "(") && is_grouping_paren(ps, tok)) {
		/* the suffix after the group applies to ty first, then the group's declarator */
		start = tok->next;
		tok = skip_balanced(ps, tok);
		if (ps->err) {
			*rest = tok;
			return ty;
		}
		tok = parse_attributes(ps, tok, attrs);
		ty = type_suffix(ps, &tok, tok, ty);
		ty = declarator(ps, &dummy, start, ty, name_tok, attrs);
		*rest = tok;
		return ty;
	}

	if (tok->kind == CPT_IDENT && kw_of(ps, tok) == KW_NONE) {
		if (name_tok)
			*name_tok = tok;
		tok = tok->next;
	}
	tok = parse_attributes(ps, tok, attrs);
	ty = type_suffix(ps, &tok, tok, ty);
	*rest = tok;
	return ty;
}

/* a type name: specifiers and an abstract declarator */
static struct cp_type *parse_type_name(struct cp_parser *ps, struct cp_token **rest,
				       struct cp_token *tok)
{
	struct cp_declspec sp;
	struct cp_type *ty;

	struct cp_token *name = NULL;

	memset(&sp, 0, sizeof(sp));
	if (declspec(ps, &tok, tok, &sp))
		return NULL;
	ty = declarator(ps, &tok, tok, sp.type, &name, NULL);
	*rest = tok;
	if (!ps->err && name)
		perr(ps, name, "unexpected identifier '%.*s' in a type name", name->len, name->loc);
	return ps->err ? NULL : ty;
}

/* layout: struct and union, the System V rules including packed and aligned */

static int64_t align_to(int64_t n, int64_t align)
{
	return (n + align - 1) / align * align;
}

/* the alignment a member gets inside a struct (some targets cap scalars) */
static int64_t member_align(const struct cp_target *t, const struct cp_type *type)
{
	const struct cp_type *c = cp_type_canonical(type), *u;
	int64_t align = cp_type_align(type);
	bool explicit_align = false;

	/* the alignment a typedef sets is not lowered */
	for (u = type->unqual; u->kind == CPTK_TYPEDEF || u->kind == CPTK_ELABORATED; u = u->base->unqual)
		if (u->kind == CPTK_TYPEDEF && u->align > 0)
			explicit_align = true;

	while (c->kind == CPTK_ARRAY)
		c = cp_type_canonical(c->base);
	if (!explicit_align && t->member_align_cap && c->kind < CPTK_BUILTIN_END &&
	    align > t->member_align_cap)
		align = t->member_align_cap;
	return align;
}

/*
 * The record layout of the Windows targets, MSVC and MinGW as clang has it. A
 * bit-field takes a whole unit of its type, and a following bit-field of the same
 * size shares the unit while it fits. A bit-field of another size, a field that
 * is not a bit-field and a zero width bit-field end the unit.
 */
static void layout_struct_ms(const struct cp_target *t, struct cp_type *ty, int alt_align,
			     int pack_align, int pragma_pack)
{
	int64_t size = 0, align = 1, falign, mem_align, fsize;
	int64_t unit_size = 0, remaining = 0;
	bool last_bitfield = false;
	struct cp_field *mem;
	int i;

	for (i = 0; i < ty->num_fields; i++) {
		mem = ty->fields[i];
		falign = member_align(t, mem->type);
		/* MinGW: the packed attribute does not apply to a bit-field, #pragma pack does */
		if (mem->bit_width >= 0 && t->mingw)
			mem_align = pragma_pack > 0 && pragma_pack < falign ? pragma_pack : falign;
		else
			mem_align = mem->is_packed ? 1 :
				(pack_align > 0 && pack_align < falign) ? pack_align : falign;
		if (mem->alt_align > mem_align)
			mem_align = mem->alt_align;
		fsize = cp_type_size(mem->type);

		if (mem->bit_width == 0) {
			/* only a zero width bit-field after a bit-field does something */
			if (last_bitfield) {
				size = align_to(size, mem_align);
				if (mem_align > align)
					align = mem_align;
			}
			mem->offset = size;
			mem->bit_offset = 0;
			last_bitfield = false;
			continue;
		}

		if (mem->bit_width > 0) {
			if (!last_bitfield || fsize != unit_size || remaining < mem->bit_width) {
				size = align_to(size, mem_align);
				mem->offset = size;
				size += fsize;
				unit_size = fsize;
				remaining = fsize * 8;
			} else {
				mem->offset = size - unit_size;
			}
			mem->bit_offset = (int)(unit_size * 8 - remaining);
			remaining -= mem->bit_width;
			last_bitfield = true;
			if (mem_align > align)
				align = mem_align;
			continue;
		}

		size = align_to(size, mem_align);
		mem->offset = size;
		last_bitfield = false;
		if (mem_align > align)
			align = mem_align;
		if (fsize < 0)		/* flexible array member */
			break;
		size += fsize;
	}

	/* MSVC: a record without any size has 4 bytes */
	if (!size && t->msvc)
		size = 4;
	ty->align = alt_align > align ? alt_align : (int)align;
	ty->size = align_to(size, ty->align);
}

/*
 * A bit-field in a union takes the size of its type and has no alignment of its
 * own. A zero width one has no size with MSVC, and one byte with MinGW.
 */
static void layout_union_ms(const struct cp_target *t, struct cp_type *ty, int alt_align,
			    int pack_align)
{
	int64_t size = 0, align = 1, falign, mem_align, fsize;
	bool last_bitfield = false;
	struct cp_field *mem;
	int i;

	for (i = 0; i < ty->num_fields; i++) {
		mem = ty->fields[i];
		falign = member_align(t, mem->type);
		mem_align = mem->is_packed ? 1 :
			(pack_align > 0 && pack_align < falign) ? pack_align : falign;
		if (mem->alt_align > mem_align)
			mem_align = mem->alt_align;
		fsize = cp_type_size(mem->type);

		if (mem->bit_width < 0 && mem_align > align)
			align = mem_align;

		if (mem->bit_width == 0) {
			/* after a bit-field it pads the union to its type; alone it has no size */
			if (t->mingw)
				fsize = 1;
			else if (last_bitfield)
				fsize = align_to(size, mem_align);
			else
				fsize = 0;
		}
		last_bitfield = mem->bit_width > 0;
		if (fsize > size)
			size = fsize;
	}

	if (!size && t->msvc)
		size = 4;
	ty->align = alt_align > align ? alt_align : (int)align;
	ty->size = align_to(size, ty->align);
}

static void layout_struct(const struct cp_target *t, struct cp_type *ty, int alt_align,
			  int pack_align)
{
	int64_t bits = 0;
	int i;
	struct cp_field *mem;
	int64_t falign;
	int64_t fsize;
	int64_t ceil;
	int64_t rsz;
	bool aligned;
	int64_t mofs;
	int64_t bsz;
	int64_t abits;
	int64_t mem_align;
	bool is_record;

	for (i = 0; i < ty->num_fields; i++) {
		mem = ty->fields[i];
		falign = member_align(t, mem->type);
		mem_align = mem->is_packed ? 1 :
			(pack_align > 0 && pack_align < falign) ? pack_align : falign;
		is_record = cp_type_canonical(mem->type)->kind == CPTK_STRUCT ||
			    cp_type_canonical(mem->type)->kind == CPTK_UNION;

		if (mem->bit_width < 0 || mem->name || t->unnamed_bitfield_align) {
			if (mem_align > alt_align)
				alt_align = (int)mem_align;
			if (mem->alt_align > alt_align)
				alt_align = mem->alt_align;
		}
		if (mem->alt_align && (mem->alt_align > falign || !is_record))
			bits = align_to(bits, mem->alt_align * 8);

		if (mem->bit_width >= 0) {
			fsize = cp_type_size(mem->type);

			if (mem->bit_width == 0) {
				/* aligns to the type's alignment; that is where the field is */
				bits = align_to(bits, falign * 8);
				mem->offset = bits / 8;
				mem->bit_offset = 0;
				continue;
			}
			if (mem->is_packed || pack_align) {
				ceil = align_to(bits + mem->bit_width, 8);
				aligned = false;

				for (rsz = 8; rsz <= 64; rsz *= 2) {
					if (ceil % rsz != 0)
						break;
					mofs = (ceil - rsz) / 8;
					if (mofs * 8 > bits)
						continue;
					mem->offset = mofs;
					aligned = true;
					break;
				}
				if (!aligned)
					mem->offset = bits / 8;
			} else {
				/*
				 * The field must fit in an allocation unit of the type's
				 * alignment: it is padded when it would run past the end of
				 * the type's size counted from the last alignment boundary.
				 */
				bsz = fsize * 8;
				abits = falign * 8;

				if ((bits % abits) + mem->bit_width > bsz)
					bits = align_to(bits, abits);
				mem->offset = bits / abits * falign;
			}
			mem->bit_offset = (int)(bits - mem->offset * 8);
			bits += mem->bit_width;
			continue;
		}

		bits = align_to(bits, mem_align * 8);
		mem->offset = bits / 8;
		if (cp_type_size(mem->type) < 0)	/* flexible array member */
			break;
		bits += cp_type_size(mem->type) * 8;
	}

	if (alt_align)
		ty->align = alt_align;
	if (!alt_align && pack_align > 0)
		ty->size = align_to(bits, pack_align * 8) / 8;
	else
		ty->size = align_to(bits, ty->align * 8) / 8;
}

static void layout_union(const struct cp_target *t, struct cp_type *ty, int alt_align,
			 int pack_align)
{
	int64_t size = 0;
	int i;
	struct cp_field *mem;
	int64_t falign;
	int64_t sz;
	int64_t mem_align;

	for (i = 0; i < ty->num_fields; i++) {
		mem = ty->fields[i];
		falign = member_align(t, mem->type);
		mem_align = mem->is_packed ? 1 :
			(pack_align > 0 && pack_align < falign) ? pack_align : falign;

		if (mem->bit_width < 0 || mem->name || t->unnamed_bitfield_align) {
			if (mem_align > alt_align)
				alt_align = (int)mem_align;
			if (mem->alt_align > alt_align)
				alt_align = mem->alt_align;
		}
		sz = mem->bit_width >= 0 ? align_to(mem->bit_width, 8) / 8 : cp_type_size(mem->type);
		if (sz > size)
			size = sz;
	}
	if (alt_align)
		ty->align = alt_align;
	ty->size = align_to(size, ty->align);
}

/* struct, union and enum specifiers */

static struct cp_type *tag_new(struct cp_parser *ps, enum cp_type_kind kind, const char *name)
{
	struct cp_type *ty = cp_alloc(ps->tu, sizeof(*ty));

	if (!ty) {
		ps->err = true;
		return NULL;
	}
	ty->kind = kind;
	ty->unqual = ty;
	ty->size = -1;
	ty->align = 1;
	ty->count = -1;
	ty->name = name;
	return ty;
}

static void tag_register(struct cp_parser *ps, const struct cp_token *tag, struct cp_type *ty)
{
	struct cp_hashmap_entry *ent;

	ent = cp_hashmap_get_or_insert(ps->tu, &ps->tags, tag->loc, tag->len);
	if (ent)
		ent->val = ty;
}

static struct cp_field *add_field(struct cp_parser *ps, struct cp_type *rec,
				  struct cp_field ***arrp, int *cap)
{
	struct cp_field *f = cp_alloc(ps->tu, sizeof(*f));
	struct cp_field **n;

	if (!f) {
		ps->err = true;
		return NULL;
	}
	if (rec->num_fields == *cap) {
		*cap = *cap ? *cap * 2 : 8;
		n = cp_alloc(ps->tu, *cap * sizeof(*n));
		if (!n) {
			ps->err = true;
			return NULL;
		}
		if (rec->num_fields)
			memcpy(n, *arrp, rec->num_fields * sizeof(*n));
		*arrp = n;
	}
	f->bit_width = -1;
	(*arrp)[rec->num_fields++] = f;
	rec->fields = *arrp;
	return f;
}

static struct cp_token *struct_members(struct cp_parser *ps, struct cp_token *tok,
				       struct cp_type *rec, struct cp_decl *rdecl)
{
	struct cp_field **arr = NULL;
	struct cp_decl *saved_scope = ps->scope;
	int cap = 0;
	struct cp_declspec sp;
	struct cp_token *start;
	bool first;
	struct cp_type *c;
	struct cp_field *f;
	struct cp_token *name;
	struct cp_token *dstart;
	struct cp_attrs da;
	struct cp_type *fty;
	int bit_width;
	struct cp_value v;

	ps->scope = rdecl;

	while (!ps->err && !is(tok, "}")) {
		first = true;

		if (tok->kind == CPT_EOF) {
			perr(ps, tok, "unterminated struct or union");
			break;
		}
		if (is(tok, ";")) {
			tok = tok->next;
			continue;
		}
		if (kw_of(ps, tok) == KW_STATIC_ASSERT) {
			tok = tok->next;
			if (is(tok, "("))
				tok = skip_balanced(ps, tok);
			if (is(tok, ";"))
				tok = tok->next;
			continue;
		}

		memset(&sp, 0, sizeof(sp));
		if (declspec(ps, &tok, tok, &sp))
			break;
		start = sp.start;

		/* anonymous struct or union member */
		if (is(tok, ";")) {
			c = cp_type_canonical(sp.type);

			if (c->kind == CPTK_STRUCT || c->kind == CPTK_UNION) {
				f = add_field(ps, rec, &arr, &cap);

				if (!f)
					break;
				/* no declaration: libclang reports none for these either */
				f->type = sp.type;
				f->is_anon_record = true;
				f->is_packed = sp.attrs.packed;
				f->alt_align = sp.attrs.aligned;
				(void)start;
			}
			tok = tok->next;
			continue;
		}

		for (;;) {
			name = NULL;
			dstart = tok;
			da = sp.attrs;
			fty = sp.type;
			bit_width = -1;

			if (!first) {
				if (!is(tok, ","))
					break;
				tok = tok->next;
				dstart = tok;
			}
			first = false;

			if (!is(tok, ":"))
				fty = declarator(ps, &tok, tok, sp.type, &name, &da);
			if (ps->err)
				break;

			if (is(tok, ":")) {
				if (!cp_type_is_integer(fty)) {
					perr(ps, tok, "bit-field has non-integer type '%s'",
					     cp_type_spelling(ps->tu, fty));
					break;
				}
				tok = tok->next;
				if (eval_const(ps, tok, &tok, &v))
					break;
				if (!v.is_unsigned && (int64_t)v.val < 0) {
					perr(ps, tok, "bit-field with negative width");
					break;
				}
				bit_width = (int)v.val;
			}
			tok = parse_attributes(ps, tok, &da);
			if (ps->err)
				break;
			fty = apply_attr_types(ps, fty, &da);

			if (cp_type_canonical(fty)->kind == CPTK_FUNC ||
			    cp_type_canonical(fty)->kind == CPTK_VOID) {
				perr(ps, dstart, "field has invalid type");
				break;
			}

			f = add_field(ps, rec, &arr, &cap);
			if (!f)
				break;
			f->name = name ? tok_name(ps, name) : NULL;
			f->type = fty;
			f->bit_width = bit_width;
			f->is_packed = da.packed;
			f->alt_align = da.aligned;
			f->decl = new_decl(ps, CPDK_FIELD, f->name, fty, rdecl, start,
					   tok_before(dstart, tok));
			if (f->decl)
				f->decl->field = f;

			/* an incomplete array is only allowed as the last member */
			if (cp_type_size(fty) < 0 && bit_width < 0) {
				c = cp_type_canonical(fty);

				if (c->kind == CPTK_ARRAY && c->count < 0 && rec->kind == CPTK_STRUCT) {
					rec->is_flexible = true;
				} else {
					perr(ps, dstart, "field has incomplete type '%s'",
					     cp_type_spelling(ps->tu, fty));
					break;
				}
			}
		}
		if (ps->err)
			break;
		if (!is(tok, ";")) {
			perr(ps, tok, "expected ';' at the end of a member declaration");
			break;
		}
		tok = tok->next;
	}
	ps->scope = saved_scope;
	return tok;
}

static struct cp_type *struct_union_specifier(struct cp_parser *ps, struct cp_token **rest,
					      struct cp_token *tok, enum cp_type_kind kind,
					      struct cp_decl *parent)
{
	struct cp_token *kwtok = tok, *tag = NULL, *after_tag;
	struct cp_attrs attrs = { 0 };
	struct cp_type *ty = NULL;
	struct cp_decl *decl;
	enum cp_decl_kind dk = kind == CPTK_STRUCT ? CPDK_STRUCT : CPDK_UNION;
	const char *name = NULL;
	bool has_body;
	int pack;

	parent = ps->scope;

	tok = parse_attributes(ps, tok->next, &attrs);
	if (tok->kind == CPT_IDENT && kw_of(ps, tok) == KW_NONE) {
		tag = tok;
		tok = tok->next;
		name = tok_name(ps, tag);
	}
	after_tag = tok;
	tok = parse_attributes(ps, tok, &attrs);
	has_body = is(tok, "{");
	if (ps->err)
		return NULL;

	if (tag) {
		ty = cp_hashmap_get(&ps->tags, tag->loc, tag->len);
		if (ty && ty->kind != kind) {
			perr(ps, tag, "use of '%s' with tag type that does not match previous declaration",
			     name);
			return NULL;
		}
	}

	if (!has_body) {
		if (!tag) {
			perr(ps, tok, "expected '{' or a tag name");
			return NULL;
		}
		if (!ty) {
			/* the first mention declares the tag */
			ty = tag_new(ps, kind, name);
			if (!ty)
				return NULL;
			tag_register(ps, tag, ty);
			decl = new_decl(ps, dk, name, ty, parent, kwtok, tag);
			if (decl)
				ty->decl = decl;
		} else if (kwtok == ps->decl_start && is(tok, ";")) {
			/* `struct X;` is a declaration even when X is already known */
			new_decl(ps, dk, name, ty, parent, kwtok, tag);
		}
		/* the attributes of a use of the tag belong to the declaration */
		*rest = after_tag;
		return cp_type_elaborated(ps->tu, ty);
	}

	/* a definition that is not closed yet is a redefinition too: `struct s { struct s { int a; }; };` */
	if (ty && (ty->complete || (ty->decl && ty->decl->is_definition))) {
		perr(ps, kwtok, "redefinition of '%s %s'", kind == CPTK_STRUCT ? "struct" : "union",
		     name);
		return NULL;
	}
	if (!ty) {
		ty = tag_new(ps, kind, name);
		if (!ty)
			return NULL;
		if (tag)
			tag_register(ps, tag, ty);
	}
	decl = new_decl(ps, dk, name, ty, parent, kwtok, tok);
	if (!decl)
		return NULL;
	decl->is_definition = true;
	ty->decl = decl;

	tok = struct_members(ps, tok->next, ty, decl);
	if (ps->err)
		return NULL;
	if (!is(tok, "}")) {
		perr(ps, tok, "expected '}'");
		return NULL;
	}
	decl_range(decl, kwtok, tok);
	tok = tok->next;
	tok = parse_attributes(ps, tok, &attrs);
	if (ps->err)
		return NULL;

	/* #pragma pack is read from the struct keyword; packed means pack(1) */
	{
		pack = attrs.packed ? 1 : kwtok->pack;

		ty->size = 0;
		ty->align = 1;
		if (ps->tu->target->ms_layout && kind == CPTK_STRUCT)
			layout_struct_ms(ps->tu->target, ty, attrs.aligned, pack, kwtok->pack);
		else if (ps->tu->target->ms_layout)
			layout_union_ms(ps->tu->target, ty, attrs.aligned, pack);
		else if (kind == CPTK_STRUCT)
			layout_struct(ps->tu->target, ty, attrs.aligned, pack);
		else
			layout_union(ps->tu->target, ty, attrs.aligned, pack);
	}
	ty->complete = true;

	*rest = tok;
	return cp_type_elaborated(ps->tu, ty);
}

/*
 * The type of an enumeration without a fixed type, as libclang picks it for C:
 * a type for the values that are not negative, or a signed one. A value of an
 * unsigned type counts as not negative, whatever its bit pattern.
 */
static struct cp_type *enum_underlying(struct cp_parser *ps, int64_t min, uint64_t max)
{
	bool lp64 = ps->tu->target->long_.size >= 8;
	enum cp_type_kind k;

	/* MSVC: always an int */
	if (ps->tu->target->msvc)
		return cp_type_builtin(ps->tu, CPTK_INT);
	if (min >= 0)
		k = max <= UINT_MAX ? CPTK_UINT : lp64 ? CPTK_ULONG : CPTK_ULONGLONG;
	else
		k = (min >= INT_MIN && max <= INT_MAX) ? CPTK_INT : lp64 ? CPTK_LONG : CPTK_LONGLONG;
	return cp_type_builtin(ps->tu, k);
}

static struct cp_type *enum_specifier(struct cp_parser *ps, struct cp_token **rest,
				      struct cp_token *tok, struct cp_decl *parent)
{
	struct cp_token *kwtok = tok, *tag = NULL;
	struct cp_attrs attrs = { 0 };
	struct cp_type *ty = NULL, *fixed = NULL;
	struct cp_decl *decl;
	const char *name = NULL;
	struct cp_enumerator **arr = NULL;
	int cap = 0, n = 0;
	int64_t next_val = 0, min = 0;
	uint64_t max = 0;
	bool next_uns = false;
	struct cp_token *nametok;
	struct cp_token *last;
	struct cp_enumerator *e;
	struct cp_hashmap_entry *ent;
	struct cp_value v;
	struct cp_enumerator **na;

	parent = ps->scope;

	tok = parse_attributes(ps, tok->next, &attrs);
	if (tok->kind == CPT_IDENT && kw_of(ps, tok) == KW_NONE) {
		tag = tok;
		tok = tok->next;
		name = tok_name(ps, tag);
	}
	tok = parse_attributes(ps, tok, &attrs);

	/* C23 fixed underlying type */
	if (is(tok, ":") ) {
		fixed = parse_type_name(ps, &tok, tok->next);
		if (!fixed)
			return NULL;
	}
	if (ps->err)
		return NULL;

	if (tag) {
		ty = cp_hashmap_get(&ps->tags, tag->loc, tag->len);
		if (ty && ty->kind != CPTK_ENUM) {
			perr(ps, tag, "use of '%s' with tag type that does not match previous declaration",
			     name);
			return NULL;
		}
	}

	if (!is(tok, "{")) {
		if (!tag) {
			perr(ps, tok, "expected '{' or a tag name");
			return NULL;
		}
		if (!ty) {
			ty = tag_new(ps, CPTK_ENUM, name);
			if (!ty)
				return NULL;
			tag_register(ps, tag, ty);
			decl = new_decl(ps, CPDK_ENUM, name, ty, parent, kwtok, tag);
			if (decl)
				ty->decl = decl;
		} else if (kwtok == ps->decl_start && is(tok, ";")) {
			new_decl(ps, CPDK_ENUM, name, ty, parent, kwtok, tag);
		}
		*rest = tok;
		return cp_type_elaborated(ps->tu, ty);
	}

	if (ty && (ty->complete || (ty->decl && ty->decl->is_definition))) {
		perr(ps, kwtok, "redefinition of 'enum %s'", name);
		return NULL;
	}
	if (!ty) {
		ty = tag_new(ps, CPTK_ENUM, name);
		if (!ty)
			return NULL;
		if (tag)
			tag_register(ps, tag, ty);
	}
	decl = new_decl(ps, CPDK_ENUM, name, ty, parent, kwtok, tok);
	if (!decl)
		return NULL;
	decl->is_definition = true;
	ty->decl = decl;

	tok = tok->next;
	while (!ps->err && !is(tok, "}")) {
		nametok = tok;

		if (tok->kind != CPT_IDENT) {
			perr(ps, tok, "expected an enumerator name");
			break;
		}
		tok = parse_attributes(ps, tok->next, NULL);
		if (is(tok, "=")) {
			if (eval_const(ps, tok->next, &tok, &v))
				break;
			next_val = (int64_t)v.val;
			next_uns = v.is_unsigned;
		}
		/* MSVC: the value is an int */
		if (ps->tu->target->msvc) {
			next_val = (int32_t)next_val;
			next_uns = false;
		}
		last = (struct cp_token *)tok_before(nametok, tok);
		tok = parse_attributes(ps, tok, NULL);

		e = cp_alloc(ps->tu, sizeof(*e));
		if (!e) {
			ps->err = true;
			break;
		}
		e->name = tok_name(ps, nametok);
		e->value = next_val;
		e->is_unsigned = next_uns;
		e->decl = new_decl(ps, CPDK_ENUMERATOR, e->name, NULL, decl, nametok, last);
		if (e->decl)
			e->decl->value = e->value;
		ent = cp_hashmap_get_or_insert(ps->tu, &ps->enum_consts, nametok->loc, nametok->len);
		if (ent)
			ent->val = e;

		/* a negative value of a signed type; the rest are not negative */
		if (!next_uns && next_val < min)
			min = next_val;
		if ((next_uns || next_val >= 0) && (uint64_t)next_val > max)
			max = (uint64_t)next_val;

		if (n == cap) {
			cap = cap ? cap * 2 : 8;
			na = cp_alloc(ps->tu, cap * sizeof(*na));
			if (!na) {
				ps->err = true;
				break;
			}
			if (n)
				memcpy(na, arr, n * sizeof(*na));
			arr = na;
		}
		arr[n++] = e;
		next_val++;

		if (is(tok, ","))
			tok = tok->next;
		else if (!is(tok, "}")) {
			perr(ps, tok, "expected ',' or '}' in an enum");
			break;
		}
	}
	if (ps->err)
		return NULL;
	if (!is(tok, "}")) {
		perr(ps, tok, "expected '}'");
		return NULL;
	}
	decl_range(decl, kwtok, tok);
	tok = parse_attributes(ps, tok->next, &attrs);

	ty->enumerators = arr;
	ty->num_enumerators = n;
	ty->base = fixed ? fixed : enum_underlying(ps, min, max);
	ty->size = cp_type_size(ty->base);
	ty->align = cp_type_align(ty->base);
	ty->complete = true;

	*rest = tok;
	return cp_type_elaborated(ps->tu, ty);
}

/* declaration specifiers */

enum {
	C_VOID = 1 << 0, C_BOOL = 1 << 2, C_CHAR = 1 << 4, C_SHORT = 1 << 6, C_INT = 1 << 8,
	C_LONG = 1 << 10, C_FLOAT = 1 << 12, C_DOUBLE = 1 << 14, C_OTHER = 1 << 16,
	C_SIGNED = 1 << 17, C_UNSIGNED = 1 << 18, C_INT128 = 1 << 19, C_COMPLEX = 1 << 20,
};

static struct cp_type *va_list_type(struct cp_parser *ps)
{
	const struct cp_target *t = ps->tu->target;
	struct cp_type *tag, *base, *td;

	if (ps->va_list_type)
		return ps->va_list_type;

	switch (t->va_list) {
	case CPVA_X86_64:
		/* typedef struct __va_list_tag __builtin_va_list[1]; */
		tag = tag_new(ps, CPTK_STRUCT, "__va_list_tag");
		if (!tag)
			return NULL;
		tag->size = 24;
		tag->align = 8;
		tag->complete = true;
		base = cp_type_array(ps->tu, cp_type_elaborated(ps->tu, tag), 1);
		break;
	case CPVA_AARCH64:
		/* typedef struct __va_list __builtin_va_list; */
		tag = tag_new(ps, CPTK_STRUCT, "__va_list");
		if (!tag)
			return NULL;
		tag->size = 32;
		tag->align = 8;
		tag->complete = true;
		base = cp_type_elaborated(ps->tu, tag);
		break;
	case CPVA_ARM:
		/* typedef struct __va_list __builtin_va_list; */
		tag = tag_new(ps, CPTK_STRUCT, "__va_list");
		if (!tag)
			return NULL;
		tag->size = 4;
		tag->align = 4;
		tag->complete = true;
		base = cp_type_elaborated(ps->tu, tag);
		break;
	case CPVA_S390X:
		/* typedef struct __va_list_tag __builtin_va_list[1]; */
		tag = tag_new(ps, CPTK_STRUCT, "__va_list_tag");
		if (!tag)
			return NULL;
		tag->size = 32;
		tag->align = 8;
		tag->complete = true;
		base = cp_type_array(ps->tu, cp_type_elaborated(ps->tu, tag), 1);
		break;
	case CPVA_PTR_CHAR:
		base = cp_type_pointer(ps->tu, cp_type_builtin(ps->tu, CPTK_CHAR));
		break;
	default:
		base = cp_type_pointer(ps->tu, cp_type_builtin(ps->tu, CPTK_VOID));
		break;
	}
	td = tag_new(ps, CPTK_TYPEDEF, "__builtin_va_list");
	if (!base || !td)
		return NULL;
	td->base = base;
	ps->va_list_type = td;
	return td;
}

static struct cp_type *builtin_from_counter(struct cp_parser *ps, struct cp_token *tok, int c)
{
	enum cp_type_kind k;

	switch (c) {
	case C_VOID: k = CPTK_VOID; break;
	case C_BOOL: k = CPTK_BOOL; break;
	case C_CHAR: k = CPTK_CHAR; break;
	case C_SIGNED + C_CHAR: k = CPTK_SCHAR; break;
	case C_UNSIGNED + C_CHAR: k = CPTK_UCHAR; break;
	case C_SHORT: case C_SHORT + C_INT: case C_SIGNED + C_SHORT:
	case C_SIGNED + C_SHORT + C_INT: k = CPTK_SHORT; break;
	case C_UNSIGNED + C_SHORT: case C_UNSIGNED + C_SHORT + C_INT: k = CPTK_USHORT; break;
	case C_INT: case C_SIGNED: case C_SIGNED + C_INT: k = CPTK_INT; break;
	case C_UNSIGNED: case C_UNSIGNED + C_INT: k = CPTK_UINT; break;
	case C_LONG: case C_LONG + C_INT: case C_SIGNED + C_LONG:
	case C_SIGNED + C_LONG + C_INT: k = CPTK_LONG; break;
	case C_UNSIGNED + C_LONG: case C_UNSIGNED + C_LONG + C_INT: k = CPTK_ULONG; break;
	case C_LONG + C_LONG: case C_LONG + C_LONG + C_INT: case C_SIGNED + C_LONG + C_LONG:
	case C_SIGNED + C_LONG + C_LONG + C_INT: k = CPTK_LONGLONG; break;
	case C_UNSIGNED + C_LONG + C_LONG: case C_UNSIGNED + C_LONG + C_LONG + C_INT:
		k = CPTK_ULONGLONG; break;
	case C_FLOAT: k = CPTK_FLOAT; break;
	case C_DOUBLE: k = CPTK_DOUBLE; break;
	case C_LONG + C_DOUBLE: k = CPTK_LONGDOUBLE; break;
	case C_INT128: case C_SIGNED + C_INT128: k = CPTK_INT128; break;
	case C_UNSIGNED + C_INT128: k = CPTK_UINT128; break;
	default:
		perr(ps, tok, "invalid or unsupported combination of type specifiers");
		return NULL;
	}
	return cp_type_builtin(ps->tu, k);
}

/*
 * In text mode a name that is not a keyword is a typedef name that only the
 * caller can resolve: an opaque typedef (its base is a placeholder).
 */
static struct cp_type *text_typedef(struct cp_parser *ps, const struct cp_token *tok)
{
	struct cp_hashmap_entry *ent;
	struct cp_type *td = tag_new(ps, CPTK_TYPEDEF, tok_name(ps, tok));

	if (!td)
		return NULL;
	td->base = cp_type_builtin(ps->tu, CPTK_INT);
	ent = cp_hashmap_get_or_insert(ps->tu, &ps->typedefs, tok->loc, tok->len);
	if (ent)
		ent->val = td;
	return td;
}

/* apply __attribute__((mode(...))) to an integer type */
static struct cp_type *apply_mode(struct cp_parser *ps, struct cp_type *ty, int bits)
{
	bool uns = cp_type_is_unsigned(ty);
	enum cp_type_kind k;

	if (!cp_type_is_integer(ty))
		return ty;
	switch (bits) {
	case 8: k = uns ? CPTK_UCHAR : CPTK_SCHAR; break;
	case 16: k = uns ? CPTK_USHORT : CPTK_SHORT; break;
	case 32: k = uns ? CPTK_UINT : CPTK_INT; break;
	case 64: k = uns ? CPTK_ULONG : CPTK_LONG; break;
	case 128: k = uns ? CPTK_UINT128 : CPTK_INT128; break;
	default: return ty;
	}
	return cp_type_builtin(ps->tu, k);
}

/* the attributes that change the type: mode and vector_size */
static struct cp_type *apply_attr_types(struct cp_parser *ps, struct cp_type *ty,
					const struct cp_attrs *a)
{
	if (a->has_mode)
		ty = apply_mode(ps, ty, a->mode_bits);
	if (a->vector_bytes > 0 && cp_type_size(ty) > 0)
		ty = cp_type_vector(ps->tu, ty, a->vector_bytes);
	return ty;
}

static int declspec(struct cp_parser *ps, struct cp_token **rest, struct cp_token *tok,
		    struct cp_declspec *sp)
{
	struct cp_type *ty = NULL;
	int counter = 0;
	bool saw_anything = false;
	struct cp_token *begin = tok;
	enum kw kw;
	struct cp_type *td;
	struct cp_token *t;
	struct cp_value v;
	struct cp_type *at;
	bool complex;

	sp->start = tok;
	ps->decl_start = tok;

	while (!ps->err && tok->kind == CPT_IDENT) {
		kw = kw_of(ps, tok);

		if (kw == KW_NONE) {
			td = find_typedef(ps, tok);
			if (!td && ps->text_mode && !counter)
				td = text_typedef(ps, tok);
			if (!td || counter)
				break;
			ty = td;
			counter |= C_OTHER;
			saw_anything = true;
			tok = tok->next;
			continue;
		}
		saw_anything = true;

		switch (kw) {
		case KW_TYPEDEF: sp->is_typedef = true; tok = tok->next; continue;
		case KW_EXTERN: sp->is_extern = true; tok = tok->next; continue;
		case KW_STATIC: sp->is_static = true; tok = tok->next; continue;
		case KW_EXTENSION:
			/* clang starts the declaration after a leading __extension__ */
			if (sp->start == tok) {
				sp->start = tok->next;
				ps->decl_start = tok->next;
			}
			tok = tok->next;
			continue;
		case KW_AUTO: case KW_REGISTER: case KW_THREAD_LOCAL: case KW_INLINE:
		case KW_NORETURN:
			tok = tok->next;
			continue;
		case KW_NONNULL: sp->quals |= CPQ_NONNULL; tok = tok->next; continue;
		case KW_NULLABLE: sp->quals |= CPQ_NULLABLE; tok = tok->next; continue;
		case KW_NULL_UNSPEC: sp->quals |= CPQ_NULL_UNSPEC; tok = tok->next; continue;
		case KW_CONST: sp->quals |= CPQ_CONST; tok = tok->next; continue;
		case KW_VOLATILE: sp->quals |= CPQ_VOLATILE; tok = tok->next; continue;
		case KW_RESTRICT: sp->quals |= CPQ_RESTRICT; tok = tok->next; continue;
		case KW_ATTRIBUTE:
			tok = parse_attributes(ps, tok, &sp->attrs);
			continue;
		case KW_ALIGNAS: {
			t = tok->next;

			if (!is(t, "(")) {
				perr(ps, tok, "expected '(' after _Alignas");
				break;
			}
			t = t->next;
			if (is_type_start(ps, t)) {
				at = parse_type_name(ps, &t, t);

				if (at)
					sp->attrs.aligned = cp_type_align(at);
			} else if (!eval_const(ps, t, &t, &v)) {
				sp->attrs.aligned = (int)v.val;
			}
			if (!ps->err && !is(t, ")"))
				perr(ps, t, "expected ')'");
			tok = ps->err ? t : t->next;
			continue;
		}
		case KW_ATOMIC:
			if (is(tok->next, "(")) {
				t = tok->next->next;
				at = parse_type_name(ps, &t, t);

				if (!at || !is(t, ")")) {
					if (!ps->err)
						perr(ps, t, "expected ')'");
					break;
				}
				ty = cp_type_qualified(ps->tu, at, CPQ_ATOMIC);
				counter |= C_OTHER;
				tok = t->next;
			} else {
				sp->quals |= CPQ_ATOMIC;
				tok = tok->next;
			}
			continue;
		default:
			break;
		}
		if (ps->err)
			break;

		/* type specifiers */
		if (counter & C_OTHER) {
			perr(ps, tok, "invalid combination of type specifiers");
			break;
		}
		switch (kw) {
		case KW_STRUCT: case KW_UNION: {
			ty = struct_union_specifier(ps, &tok, tok,
						    kw == KW_STRUCT ? CPTK_STRUCT : CPTK_UNION, NULL);
			if (!ty)
				break;
			counter |= C_OTHER;
			continue;
		}
		case KW_ENUM:
			ty = enum_specifier(ps, &tok, tok, NULL);
			if (!ty)
				break;
			counter |= C_OTHER;
			continue;
		case KW_TYPEOF: {
			t = tok->next;

			if (!is(t, "(")) {
				perr(ps, tok, "expected '(' after typeof");
				break;
			}
			t = t->next;
			if (!is_type_start(ps, t)) {
				perr(ps, tok, "typeof of an expression is not supported");
				break;
			}
			ty = parse_type_name(ps, &t, t);
			if (!ty)
				break;
			if (!is(t, ")")) {
				perr(ps, t, "expected ')'");
				break;
			}
			tok = t->next;
			counter |= C_OTHER;
			continue;
		}
		case KW_VA_LIST:
			ty = va_list_type(ps);
			if (!ty)
				break;
			counter |= C_OTHER;
			tok = tok->next;
			continue;
		case KW_FLOAT16: ty = cp_type_builtin(ps->tu, CPTK_FLOAT16); counter |= C_OTHER; tok = tok->next; continue;
		case KW_FLOAT128: ty = cp_type_builtin(ps->tu, CPTK_FLOAT128); counter |= C_OTHER; tok = tok->next; continue;
		case KW_VOID: counter += C_VOID; break;
		case KW_BOOL: counter += C_BOOL; break;
		case KW_CHAR: counter += C_CHAR; break;
		case KW_SHORT: counter += C_SHORT; break;
		case KW_INT: counter += C_INT; break;
		case KW_LONG: counter += C_LONG; break;
		case KW_FLOAT: counter += C_FLOAT; break;
		case KW_DOUBLE: counter += C_DOUBLE; break;
		case KW_SIGNED: counter |= C_SIGNED; break;
		case KW_UNSIGNED: counter |= C_UNSIGNED; break;
		case KW_INT128:
			if (!ps->tu->target->has_int128) {
				perr(ps, tok, "__int128 is not supported on this target");
				break;
			}
			counter += C_INT128;
			break;
		case KW_COMPLEX: counter |= C_COMPLEX; break;
		default:
			perr(ps, tok, "unexpected '%.*s' in declaration specifiers", tok->len, tok->loc);
			break;
		}
		if (ps->err || !ty == !(counter & C_OTHER)) {
			/* fall through to the checks below */
		}
		if (ps->err)
			break;
		tok = tok->next;
	}

	if (ps->err) {
		*rest = tok;
		return -1;
	}

	if (!(counter & C_OTHER)) {
		if (!counter) {
			if (!saw_anything) {
				perr(ps, begin, "expected a declaration specifier");
				*rest = tok;
				return -1;
			}
			/* implicit int: `static x;` */
			counter = C_INT;
		}
		complex = counter & C_COMPLEX;

		if (complex) {
			counter &= ~C_COMPLEX;
			if (!counter)
				counter = C_DOUBLE;	/* GNU: a bare _Complex is _Complex double */
		}
		ty = builtin_from_counter(ps, begin, counter);
		if (ty && complex) {
			if (cp_type_is_integer(ty) || ty->kind == CPTK_VOID) {
				perr(ps, begin, "_Complex of a non floating type");
				ty = NULL;
			} else {
				ty = cp_type_complex(ps->tu, ty);
			}
		}
		if (!ty) {
			*rest = tok;
			return -1;
		}
	}

	if (sp->quals)
		ty = cp_type_qualified(ps->tu, ty, sp->quals);
	if (sp->attrs.has_mode)
		ty = apply_mode(ps, ty, sp->attrs.mode_bits);
	sp->type = ty;
	*rest = tok;
	return 0;
}

/* hooks for constant expressions */

static bool hook_is_type_start(void *ud, struct cp_token *tok)
{
	return is_type_start(ud, tok);
}

static int hook_parse_type(void *ud, struct cp_token *tok, struct cp_token **rest,
			   struct cp_eval_type *out)
{
	struct cp_parser *ps = ud;
	struct cp_type *ty = parse_type_name(ps, &tok, tok);

	if (!ty)
		return -1;
	out->size = cp_type_size(ty);
	out->align = cp_type_align(ty);
	out->is_integer = cp_type_is_integer(ty);
	out->is_unsigned = cp_type_is_unsigned(ty);
	out->bits = out->size > 0 ? (int)(out->size * 8) : 0;
	*rest = tok;
	return 0;
}

static int hook_lookup_ident(void *ud, struct cp_token *tok, struct cp_value *out)
{
	struct cp_parser *ps = ud;
	struct cp_enumerator *e = cp_hashmap_get(&ps->enum_consts, tok->loc, tok->len);

	if (!e)
		return 1;
	out->val = (uint64_t)e->value;
	out->is_unsigned = false;
	out->is_long = e->value < INT_MIN || e->value > INT_MAX;
	return 0;
}

/* __builtin_offsetof(type, member[.member | [index]]...) */
static int hook_offsetof(void *ud, struct cp_token *tok, struct cp_token **rest,
			 struct cp_value *out)
{
	struct cp_parser *ps = ud;
	struct cp_type *ty = parse_type_name(ps, &tok, tok), *c;
	int64_t offset = 0;
	int i;
	struct cp_field *f;
	struct cp_value v;
	bool first;

	if (!ty)
		return -1;
	if (!is(tok, ",")) {
		perr(ps, tok, "expected ',' in __builtin_offsetof");
		return -1;
	}
	tok = tok->next;

	for (first = true;; first = false) {
		c = cp_type_canonical(ty);
		if (first || is(tok, ".")) {
			f = NULL;

			if (!first)
				tok = tok->next;
			if (tok->kind != CPT_IDENT || (c->kind != CPTK_STRUCT && c->kind != CPTK_UNION)) {
				perr(ps, tok, "invalid member designator in __builtin_offsetof");
				return -1;
			}
			for (i = 0; i < c->num_fields; i++) {
				if (c->fields[i]->name && (int)strlen(c->fields[i]->name) == tok->len &&
				    !memcmp(c->fields[i]->name, tok->loc, tok->len)) {
					f = c->fields[i];
					break;
				}
			}
			if (!f) {
				perr(ps, tok, "no member named '%.*s'", tok->len, tok->loc);
				return -1;
			}
			offset += f->offset;
			ty = f->type;
			tok = tok->next;
		} else if (is(tok, "[")) {
			if (c->kind != CPTK_ARRAY) {
				perr(ps, tok, "subscript of a non-array in __builtin_offsetof");
				return -1;
			}
			if (eval_const(ps, tok->next, &tok, &v))
				return -1;
			if (!is(tok, "]")) {
				perr(ps, tok, "expected ']'");
				return -1;
			}
			tok = tok->next;
			offset += (int64_t)v.val * cp_type_size(c->base);
			ty = c->base;
		} else {
			break;
		}
	}
	out->val = (uint64_t)offset;
	out->is_unsigned = true;
	*rest = tok;
	return 0;
}

/* top level */

static struct cp_token *skip_initializer(struct cp_parser *ps, struct cp_token *tok)
{
	while (tok->kind != CPT_EOF && !ps->err) {
		if (is(tok, ",") || is(tok, ";"))
			break;
		if (tok->kind == CPT_PUNCT && (is(tok, "(") || is(tok, "[") || is(tok, "{"))) {
			tok = skip_balanced(ps, tok);
			continue;
		}
		tok = tok->next;
	}
	return tok;
}

#define U_KIND_IS_ELAB(_t)	((_t)->unqual->kind == CPTK_ELABORATED)

static void define_typedef(struct cp_parser *ps, struct cp_declspec *sp, struct cp_type *ty,
			   struct cp_token *nametok, const struct cp_token *last, int aligned)
{
	struct cp_type *td;
	struct cp_hashmap_entry *ent;
	struct cp_decl *d;
	struct cp_type *tag;

	td = tag_new(ps, CPTK_TYPEDEF, tok_name(ps, nametok));
	if (!td)
		return;
	td->base = ty;
	/* the alignment of the typedef; Windows does not lower it, 0 is the base's */
	if (aligned && ps->tu->target->ms_layout && aligned < cp_type_align(ty))
		aligned = 0;
	td->align = aligned;
	/* the typedef directly naming an anonymous tag becomes its name (clang) */
	if (U_KIND_IS_ELAB(ty)) {
		tag = ty->unqual->base;

		if (!tag->name && !tag->linkage_name)
			tag->linkage_name = td->name;
	}
	d = new_decl(ps, CPDK_TYPEDEF, td->name, td, NULL, sp->start, last);
	if (!d)
		return;
	d->is_definition = true;
	td->decl = d;
	ent = cp_hashmap_get_or_insert(ps->tu, &ps->typedefs, nametok->loc, nametok->len);
	if (ent)
		ent->val = td;
}

static struct cp_token *parse_external(struct cp_parser *ps, struct cp_token *tok)
{
	struct cp_declspec sp;
	bool first = true;
	struct cp_token *name;
	struct cp_token *dstart;
	struct cp_attrs da;
	struct cp_type *ty;

	if (is(tok, ";"))
		return tok->next;

	switch (kw_of(ps, tok)) {
	case KW_STATIC_ASSERT:
		tok = tok->next;
		if (is(tok, "("))
			tok = skip_balanced(ps, tok);
		return is(tok, ";") ? tok->next : tok;
	case KW_ASM:
		tok = tok->next;
		if (is(tok, "("))
			tok = skip_balanced(ps, tok);
		return is(tok, ";") ? tok->next : tok;
	default:
		break;
	}

	memset(&sp, 0, sizeof(sp));
	if (declspec(ps, &tok, tok, &sp))
		return tok;

	if (is(tok, ";"))
		return tok->next;

	for (;;) {
		name = NULL;
		da = sp.attrs;

		if (!first) {
			if (!is(tok, ","))
				break;
			tok = tok->next;
		}
		first = false;
		dstart = tok;

		ty = declarator(ps, &tok, tok, sp.type, &name, &da);
		if (ps->err)
			return tok;
		tok = skip_asm_label(ps, tok);
		tok = parse_attributes(ps, tok, &da);
		if (ps->err)
			return tok;

		if (sp.is_typedef) {
			if (!name) {
				perr(ps, dstart, "typedef name omitted");
				return tok;
			}
			ty = apply_attr_types(ps, ty, &da);
			define_typedef(ps, &sp, ty, name, tok_before(dstart, tok), da.aligned);
			if (ps->err)
				return tok;
		} else if (cp_type_canonical(ty)->kind == CPTK_FUNC) {
			/* K&R parameter declarations, then the body */
			if (!is(tok, "{") && !is(tok, ";") && !is(tok, ",") && !is(tok, "=")) {
				while (tok->kind != CPT_EOF && !is(tok, "{"))
					tok = tok->next;
			}
			if (is(tok, "{"))
				return skip_balanced(ps, tok);
		} else if (is(tok, "=")) {
			tok = skip_initializer(ps, tok->next);
		}
	}

	if (!is(tok, ";")) {
		perr(ps, tok, "expected ';' after the declaration");
		return tok;
	}
	return tok->next;
}

/* after an error: resume after the declaration, which ends at a `;` or a `}` of a body */
static struct cp_token *recover(struct cp_parser *ps, struct cp_token *tok)
{
	const struct cp_token *prev = tok;
	bool brace;
	struct cp_token *after2;

	ps->err = false;
	while (tok->kind != CPT_EOF) {
		if (is(tok, ";"))
			return tok->next;
		if (tok->kind == CPT_PUNCT && (is(tok, "(") || is(tok, "[") || is(tok, "{"))) {
			brace = is(tok, "{");
			after2 = skip_balanced(ps, tok);

			if (ps->err) {
				ps->err = false;
				return after2;
			}
			/* a function body ends the declaration */
			if (brace && prev && is(prev, ")"))
				return after2;
			prev = tok;
			tok = after2;
			continue;
		}
		prev = tok;
		tok = tok->next;
	}
	return tok;
}

static int parser_init(struct cp_parser *ps, struct cp_tu *tu)
{
	struct cp_type *td;
	struct cp_hashmap_entry *ent;

	static const struct { const char *name; enum cp_type_kind kind; } imp[] = {
		/* clang predefines these as typedefs of __int128 */
		{ "__int128_t", CPTK_INT128 }, { "__uint128_t", CPTK_UINT128 },
	};
	size_t i;

	memset(ps, 0, sizeof(*ps));
	ps->tu = tu;
	ps->hooks.ud = ps;
	ps->hooks.is_type_start = hook_is_type_start;
	ps->hooks.parse_type = hook_parse_type;
	ps->hooks.lookup_ident = hook_lookup_ident;
	ps->hooks.offsetof_ = hook_offsetof;
	kw_init(ps);

	for (i = 0; ps->tu->target->has_int128 && i < ARRAY_SIZE(imp); i++) {
		td = tag_new(ps, CPTK_TYPEDEF, imp[i].name);

		if (!td)
			return -1;
		td->base = cp_type_builtin(tu, imp[i].kind);
		ent = cp_hashmap_get_or_insert(tu, &ps->typedefs, imp[i].name,
					       (int)strlen(imp[i].name));
		if (ent)
			ent->val = td;
	}
	return 0;
}

int cp_parse(struct cp_tu *tu, struct cp_token *tok)
{
	struct cp_parser ps;
	size_t errors = tu->num_errors;
	struct cp_token *start;

	if (parser_init(&ps, tu))
		return -1;

	while (tok && tok->kind != CPT_EOF) {
		start = tok;

		tok = parse_external(&ps, tok);
		if (ps.err)
			tok = recover(&ps, tok == start ? start : tok);
		/* always make progress */
		if (tok == start)
			tok = tok->next;
	}
	return (int)(tu->num_errors - errors);
}

struct cp_type *cp_parse_type_text(struct cp_tu *tu, const char *text, size_t len)
{
	struct cp_parser ps;
	struct cp_token *tok, *rest;
	struct cp_type *ty;

	tok = cp_tokenize_text(tu, NULL, text, len);
	if (!tok)
		return NULL;
	if (parser_init(&ps, tu))
		return NULL;
	ps.text_mode = true;

	ty = parse_type_name(&ps, &rest, tok);
	if (!ty || ps.err)
		return NULL;
	if (rest->kind != CPT_EOF) {
		perr(&ps, rest, "unexpected '%.*s' after the type", rest->len, rest->loc);
		return NULL;
	}
	return ty;
}
