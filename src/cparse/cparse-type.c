/*
 * cparse-type.c - C declaration front end: types
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Derived from slimcc's type.c (builtin type table), which is derived from
 * chibicc:
 *
 * Copyright (c) 2019 Rui Ueyama
 * Copyright (c) 2023-2026 Hsiang-Ying Fu
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "cparse-private.h"

/*
 * Qualified types are separate small nodes that point at the unqualified one
 * (`unqual`); every property is read from the unqualified node, so that
 * completing a struct later is seen by all of its qualified variants.
 */
#define U(_t)	((_t)->unqual)

struct cp_type *cp_type_canonical(const struct cp_type *ty)
{
	ty = U(ty);
	while (ty->kind == CPTK_TYPEDEF || ty->kind == CPTK_ELABORATED)
		ty = U(ty->base);
	return (struct cp_type *)ty;
}

int64_t cp_type_size(const struct cp_type *ty)
{
	return cp_type_canonical(ty)->size;
}

int cp_type_align(const struct cp_type *ty)
{
	ty = U(ty);
	/* aligned on a typedef sets the alignment of every use of the typedef */
	while (ty->kind == CPTK_TYPEDEF || ty->kind == CPTK_ELABORATED) {
		if (ty->kind == CPTK_TYPEDEF && ty->align > 0)
			return ty->align;
		ty = U(ty->base);
	}
	return ty->align;
}

static bool kind_is_integer(enum cp_type_kind k)
{
	return k == CPTK_BOOL || (k >= CPTK_CHAR && k <= CPTK_UINT128);
}

bool cp_type_is_integer(const struct cp_type *ty)
{
	const struct cp_type *c = cp_type_canonical(ty);

	return kind_is_integer(c->kind) || c->kind == CPTK_ENUM;
}

bool cp_type_is_unsigned(const struct cp_type *ty)
{
	const struct cp_type *c = cp_type_canonical(ty);

	if (c->kind == CPTK_ENUM)
		return c->base ? cp_type_is_unsigned(c->base) : true;
	/* set when the builtin was made: plain char depends on the target */
	return c->is_unsigned;
}

static struct cp_type *type_new(struct cp_tu *tu, enum cp_type_kind kind)
{
	struct cp_type *ty = cp_alloc(tu, sizeof(*ty));

	if (!ty)
		return NULL;
	ty->kind = kind;
	ty->unqual = ty;
	ty->size = -1;
	ty->align = 1;
	ty->count = -1;
	return ty;
}

struct cp_type *cp_type_builtin(struct cp_tu *tu, enum cp_type_kind kind)
{
	const struct cp_target *t = tu->target;
	struct cp_type *ty;
	int64_t size;
	int align;

	if (kind >= CPTK_BUILTIN_END)
		return NULL;
	if (tu->builtin[kind])
		return tu->builtin[kind];

	switch (kind) {
	case CPTK_VOID:		size = -1; align = 1; break;	/* no size, as libclang says */
	case CPTK_BOOL:		size = t->bool_.size; align = t->bool_.align; break;
	case CPTK_CHAR:
	case CPTK_SCHAR:
	case CPTK_UCHAR:	size = 1; align = 1; break;
	case CPTK_SHORT:
	case CPTK_USHORT:	size = t->short_.size; align = t->short_.align; break;
	case CPTK_INT:
	case CPTK_UINT:		size = t->int_.size; align = t->int_.align; break;
	case CPTK_LONG:
	case CPTK_ULONG:	size = t->long_.size; align = t->long_.align; break;
	case CPTK_LONGLONG:
	case CPTK_ULONGLONG:	size = t->llong_.size; align = t->llong_.align; break;
	case CPTK_INT128:
	case CPTK_UINT128:	size = t->int128.size; align = t->int128.align; break;
	case CPTK_FLOAT:	size = t->float_.size; align = t->float_.align; break;
	case CPTK_DOUBLE:	size = t->double_.size; align = t->double_.align; break;
	case CPTK_LONGDOUBLE:	size = t->ldouble.size; align = t->ldouble.align; break;
	case CPTK_FLOAT16:	size = t->float16.size; align = t->float16.align; break;
	case CPTK_FLOAT128:	size = t->float128.size; align = t->float128.align; break;
	default:		return NULL;
	}

	ty = type_new(tu, kind);
	if (!ty)
		return NULL;
	ty->size = size;
	ty->align = align;
	ty->is_unsigned = kind == CPTK_BOOL || kind == CPTK_UCHAR || kind == CPTK_USHORT ||
			  kind == CPTK_UINT || kind == CPTK_ULONG || kind == CPTK_ULONGLONG ||
			  kind == CPTK_UINT128 || (kind == CPTK_CHAR && t->char_unsigned);
	ty->complete = true;
	tu->builtin[kind] = ty;
	return ty;
}

struct cp_type *cp_type_qualified(struct cp_tu *tu, struct cp_type *ty, unsigned quals)
{
	struct cp_type *u = U(ty), *q;

	quals |= ty->quals;
	if (!quals)
		return u;
	if (quals == ty->quals)
		return ty;
	for (q = u->variants; q; q = q->next_variant)
		if (q->quals == quals)
			return q;

	q = cp_alloc(tu, sizeof(*q));
	if (!q)
		return NULL;
	*q = *u;
	q->variants = NULL;
	q->quals = quals;
	q->unqual = u;
	q->next_variant = u->variants;
	u->variants = q;
	return q;
}

struct cp_type *cp_type_requalified(struct cp_tu *tu, struct cp_type *ty, unsigned quals)
{
	struct cp_type *u = U(ty);

	if (!quals)
		return u;
	return cp_type_qualified(tu, u, quals);
}

struct cp_type *cp_type_pointer(struct cp_tu *tu, struct cp_type *base)
{
	struct cp_type *ty = type_new(tu, CPTK_PTR);

	if (!ty)
		return NULL;
	ty->base = base;
	ty->size = tu->target->ptr.size;
	ty->align = tu->target->ptr.align;
	ty->complete = true;
	return ty;
}

struct cp_type *cp_type_array(struct cp_tu *tu, struct cp_type *base, int64_t count)
{
	struct cp_type *ty = type_new(tu, CPTK_ARRAY);
	int64_t esize = cp_type_size(base);

	if (!ty)
		return NULL;
	ty->base = base;
	ty->count = count;
	ty->align = cp_type_align(base);
	ty->size = (count >= 0 && esize >= 0) ? count * esize : -1;
	ty->complete = count >= 0 && esize >= 0;
	return ty;
}

struct cp_type *cp_type_elaborated(struct cp_tu *tu, struct cp_type *tag)
{
	struct cp_type *u = U(tag), *ty;

	if (u->elaborated)
		return cp_type_qualified(tu, u->elaborated, tag->quals);

	ty = type_new(tu, CPTK_ELABORATED);
	if (!ty)
		return NULL;
	ty->base = u;
	ty->keyword = u->kind;
	ty->name = u->name;
	u->elaborated = ty;
	return cp_type_qualified(tu, ty, tag->quals);
}

struct cp_type *cp_type_complex(struct cp_tu *tu, struct cp_type *base)
{
	struct cp_type *ty = type_new(tu, CPTK_COMPLEX);

	if (!ty)
		return NULL;
	ty->base = base;
	ty->size = cp_type_size(base) * 2;
	ty->align = cp_type_align(base);
	ty->complete = true;
	return ty;
}

struct cp_type *cp_type_vector(struct cp_tu *tu, struct cp_type *base, int64_t bytes)
{
	struct cp_type *ty = type_new(tu, CPTK_VECTOR);

	if (!ty)
		return NULL;
	ty->base = base;
	ty->size = bytes;
	/* natural alignment, but no more than the target's vector register */
	ty->align = tu->target->max_vector_align && bytes > tu->target->max_vector_align ?
		    tu->target->max_vector_align : (int)bytes;
	ty->count = cp_type_size(base) > 0 ? bytes / cp_type_size(base) : 0;
	ty->complete = true;
	return ty;
}

/* spelling, in clang's style */

static const char *builtin_name(enum cp_type_kind k)
{
	switch (k) {
	case CPTK_VOID: return "void";
	case CPTK_BOOL: return "_Bool";
	case CPTK_CHAR: return "char";
	case CPTK_SCHAR: return "signed char";
	case CPTK_UCHAR: return "unsigned char";
	case CPTK_SHORT: return "short";
	case CPTK_USHORT: return "unsigned short";
	case CPTK_INT: return "int";
	case CPTK_UINT: return "unsigned int";
	case CPTK_LONG: return "long";
	case CPTK_ULONG: return "unsigned long";
	case CPTK_LONGLONG: return "long long";
	case CPTK_ULONGLONG: return "unsigned long long";
	case CPTK_INT128: return "__int128";
	case CPTK_UINT128: return "unsigned __int128";
	case CPTK_FLOAT: return "float";
	case CPTK_DOUBLE: return "double";
	case CPTK_LONGDOUBLE: return "long double";
	case CPTK_FLOAT16: return "_Float16";
	case CPTK_FLOAT128: return "__float128";
	default: return "?";
	}
}

static const char *qual_text(struct cp_tu *tu, unsigned quals, bool trailing)
{
	char *s = cp_strndup(tu, "", 0);
	static const struct { unsigned bit; const char *name; } q[] = {
		{ CPQ_CONST, "const" }, { CPQ_VOLATILE, "volatile" },
		{ CPQ_RESTRICT, "restrict" }, { CPQ_ATOMIC, "_Atomic" },
	};
	size_t i;

	for (i = 0; s && i < ARRAY_SIZE(q); i++)
		if (quals & q[i].bit)
			s = cp_format(tu, "%s%s%s", s, *s ? " " : "", q[i].name);
	/* as a prefix ("const int") there is a trailing space; raw otherwise ("*const") */
	if (s && *s && !trailing)
		s = cp_format(tu, "%s ", s);
	return s ? s : "";
}

static const char *null_text(unsigned quals)
{
	if (quals & CPQ_NONNULL)
		return " _Nonnull";
	if (quals & CPQ_NULLABLE)
		return " _Nullable";
	if (quals & CPQ_NULL_UNSPEC)
		return " _Null_unspecified";
	return "";
}

static const char *spell(struct cp_tu *tu, const struct cp_type *ty, const char *inner);

static const char *spell_params(struct cp_tu *tu, const struct cp_type *f)
{
	const char *s = "(";
	int i;

	if (f->noproto)
		return "()";
	if (!f->num_params && !f->variadic)
		return "(void)";
	for (i = 0; i < f->num_params; i++)
		s = cp_format(tu, "%s%s%s", s, i ? ", " : "", spell(tu, f->params[i], ""));
	if (f->variadic)
		s = cp_format(tu, "%s%s...", s, f->num_params ? ", " : "");
	return cp_format(tu, "%s)", s);
}

static const char *named_base(struct cp_tu *tu, const struct cp_type *ty, const char *inner)
{
	const struct cp_type *u = U(ty);
	const char *name, *kw;
	const struct cp_type *tag;
	unsigned quals = ty->quals;
	int line;
	int col;
	const struct cp_decl *d;

	if (u->kind == CPTK_TYPEDEF) {
		name = u->name;
	} else if (u->kind == CPTK_ELABORATED) {
		tag = U(u->base);
		kw = u->keyword == CPTK_STRUCT ? "struct" : u->keyword == CPTK_UNION ? "union" : "enum";
		if (tag->name || tag->linkage_name) {
			/* clang spells an anonymous tag by its typedef name */
			name = cp_format(tu, "%s %s", kw, tag->name ? tag->name : tag->linkage_name);
		} else {
			line = 0;
			col = 0;
			d = tag->decl;

			if (d && d->file)
				cp_file_location(d->file, d->file->contents + d->start, &line, &col);
			name = cp_format(tu, "%s (unnamed %s at %s:%d:%d)", kw, kw,
					 d && d->file ? d->file->name : "?", line, col);
		}
	} else if (u->kind == CPTK_VECTOR) {
		/* clang normalizes the size to elements * sizeof(element) */
		name = cp_format(tu, "__attribute__((__vector_size__(%lld * sizeof(%s)))) %s",
				 (long long)u->count, spell(tu, u->base, ""), spell(tu, u->base, ""));
	} else if (u->kind == CPTK_COMPLEX) {
		name = cp_format(tu, "_Complex %s", spell(tu, u->base, ""));
	} else if (u->kind == CPTK_STRUCT || u->kind == CPTK_UNION || u->kind == CPTK_ENUM) {
		kw = u->kind == CPTK_STRUCT ? "struct" : u->kind == CPTK_UNION ? "union" : "enum";
		name = u->name ? cp_format(tu, "%s %s", kw, u->name) : cp_format(tu, "%s (unnamed)", kw);
	} else {
		name = builtin_name(u->kind);
	}
	/* clang spells an atomic type `_Atomic(T)`, not as a qualifier */
	if (quals & CPQ_ATOMIC) {
		name = cp_format(tu, "_Atomic(%s)", name ? name : "?");
		quals &= ~CPQ_ATOMIC;
	}
	/* "int[4]": no space before an array suffix, "int *[4]" has one */
	return cp_format(tu, "%s%s%s%s%s", qual_text(tu, quals, false), name ? name : "?",
			 null_text(quals), *inner && inner[0] != '[' ? " " : "", inner);
}

/* `inner` is the declarator text built so far (what goes around the name) */
static const char *spell(struct cp_tu *tu, const struct cp_type *ty, const char *inner)
{
	const struct cp_type *u = U(ty), *b;
	const char *in;
	const char *q;
	const char *n;

	switch (u->kind) {
	case CPTK_PTR:
	{
		q = qual_text(tu, ty->quals, true);
		n = null_text(ty->quals);

		in = cp_format(tu, "*%s%s%s%s", q, n, (*q || *n) && *inner ? " " : "", inner);
	}
		b = U(u->base);
		if (b->kind == CPTK_ARRAY || b->kind == CPTK_FUNC)
			in = cp_format(tu, "(%s)", in);
		return spell(tu, u->base, in);
	case CPTK_ARRAY:
		if (u->count >= 0)
			in = cp_format(tu, "%s[%lld]", inner, (long long)u->count);
		else
			in = cp_format(tu, "%s[]", inner);
		return spell(tu, u->base, in);
	case CPTK_FUNC:
		in = cp_format(tu, "%s%s", inner, spell_params(tu, u));
		return spell(tu, u->base, in);
	default:
		return named_base(tu, ty, inner);
	}
}

const char *cp_type_spelling(struct cp_tu *tu, const struct cp_type *ty)
{
	return spell(tu, ty, "");
}

const char *cp_type_declaration(struct cp_tu *tu, const struct cp_type *ty, const char *name)
{
	return spell(tu, ty, name ? name : "");
}
