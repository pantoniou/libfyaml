/*
 * cparse-preprocess.c - C declaration front end: preprocessor
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Derived from slimcc's preprocess.c (directive handling, include search,
 * builtin macros), which is derived from chibicc:
 *
 * Copyright (c) 2019 Rui Ueyama
 * Copyright (c) 2023-2026 Hsiang-Ying Fu
 *
 * Macro expansion uses hide sets (Dave Prosser's algorithm) instead of slimcc's
 * lock stack; with allocation from a per-TU tag there is no token free list.
 *
 * Error handling: an error records a diagnostic and the function returns an
 * EOF token (or NULL where a list is expected), so that every loop, which
 * stops at EOF, terminates without any non-local exit.
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "fy-win32.h"
#include "cparse-private.h"

struct cp_hideset {
	struct cp_hideset *next;
	const struct cp_macro *m;
};

struct cp_macro_arg {
	struct cp_token *tok;		/* EOF terminated */
	struct cp_token *expanded;	/* lazily filled */
	bool is_va;
	bool omitted;			/* variadic argument not given at all (vs. empty) */
};

typedef struct cp_token *(*cp_macro_handler)(struct cp_pp *pp, struct cp_token *tok,
					      struct cp_token **rest);

struct cp_macro {
	const char *name;
	bool objlike;
	bool va;			/* last parameter is variadic */
	int nparams;
	struct cp_token **params;
	struct cp_token *body;		/* EOF terminated */
	cp_macro_handler handler;	/* builtin dynamic macro */
};

/* `#if` nests, so conditionals form a stack */
struct cp_cond {
	struct cp_cond *next;
	struct cp_token *tok;
	bool is_else;
	bool been_active;
};

struct cp_pp {
	struct cp_tu *tu;
	struct cp_hashmap macros;	/* name -> struct cp_macro *, NULL once undefined */
	struct cp_hashmap pragma_once;	/* realpath -> 1 */
	struct cp_hashmap guards;	/* realpath -> guard macro name */
	struct cp_cond *conds;
	const char **dirs;		/* -I and system dirs, in search order */
	bool *dir_system;
	size_t num_dirs;
	const char **qdirs;		/* -iquote dirs */
	size_t num_qdirs;
	struct cp_file *main_file;
	struct cp_token *va_args_tok;	/* the implicit __VA_ARGS__ parameter */
	int counter;
};

static struct cp_token *pp_run(struct cp_pp *pp, struct cp_token *tok);
static bool expand_macro(struct cp_pp *pp, struct cp_token **rest, struct cp_token *tok);
static struct cp_token *subst(struct cp_pp *pp, struct cp_macro *m,
			      struct cp_macro_arg *args, struct cp_token *tok);

/* tokens and lists */

static bool tok_is(const struct cp_token *tok, const char *s)
{
	return cp_tok_equal(tok, s);
}

static bool is_hash(const struct cp_token *tok)
{
	return tok->at_bol && tok->kind == CPT_PUNCT && tok_is(tok, "#");
}

static struct cp_token *copy_token(struct cp_pp *pp, const struct cp_token *tok)
{
	struct cp_token *t;

	t = cp_alloc(pp->tu, sizeof(*t));
	if (!t)
		return NULL;
	*t = *tok;
	t->next = NULL;
	return t;
}

static struct cp_token *new_eof(struct cp_pp *pp, const struct cp_token *tok)
{
	struct cp_token *t = copy_token(pp, tok);

	if (!t)
		return NULL;
	t->kind = CPT_EOF;
	t->len = 0;
	t->at_bol = true;
	return t;
}

/* a token whose list is EOF terminated: stands in for "stop everything" */
static struct cp_token *fail_eof(struct cp_pp *pp, const struct cp_token *tok)
{
	struct cp_token *t = new_eof(pp, tok);

	if (!cp_tu_failed(pp->tu))
		cp_error_at(pp->tu, NULL, NULL, "out of memory");
	return t ? t : (struct cp_token *)tok;
}

static struct cp_token *pp_error(struct cp_pp *pp, const struct cp_token *tok,
				 const char *fmt, ...)
	FY_FORMAT(printf, 3, 4);

static struct cp_token *pp_error(struct cp_pp *pp, const struct cp_token *tok,
				 const char *fmt, ...)
{
	va_list ap;
	char *msg;

	va_start(ap, fmt);
	msg = cp_vformat(pp->tu, fmt, ap);
	va_end(ap);
	cp_error_tok(pp->tu, tok, "%s", msg ? msg : "preprocessor error");
	return new_eof(pp, tok);
}

/* append list b to the EOF terminated list a (a's EOF is dropped) */
static struct cp_token *append(struct cp_token *a, struct cp_token *b)
{
	struct cp_token *t;

	if (!a || a->kind == CPT_EOF)
		return b;
	for (t = a; t->next && t->next->kind != CPT_EOF; t = t->next)
		;
	t->next = b;
	return a;
}

/* Some directives allow extraneous tokens before the newline; skip them. */
static struct cp_token *skip_line(struct cp_pp *pp, struct cp_token *tok)
{
	if (tok->at_bol || tok->kind == CPT_EOF)
		return tok;
	cp_warn_tok(pp->tu, tok, "extra token");
	while (!tok->at_bol && tok->kind != CPT_EOF)
		tok = tok->next;
	return tok;
}

/* the tokens up to the next line start as an EOF terminated list */
static struct cp_token *split_line(struct cp_pp *pp, struct cp_token **rest,
				   struct cp_token *tok)
{
	struct cp_token head = { .next = tok };
	struct cp_token *cur = &head;

	while (!cur->next->at_bol && cur->next->kind != CPT_EOF)
		cur = cur->next;

	*rest = cur->next;
	cur->next = new_eof(pp, tok);
	return head.next;
}

/* text tokenized to a list; on failure the diagnostic is already recorded */
static struct cp_token *tokenize_text(struct cp_pp *pp, struct cp_token *origin,
				      const char *text, size_t len)
{
	struct cp_token *list = cp_tokenize_text(pp->tu, origin, text, len);

	if (!list && !cp_tu_failed(pp->tu))
		cp_error_at(pp->tu, NULL, NULL, "out of memory");
	return list;
}

static struct cp_token *num_token(struct cp_pp *pp, struct cp_token *origin, int val)
{
	char buf[32];
	struct cp_token *t;

	snprintf(buf, sizeof(buf), "%d", val);
	t = tokenize_text(pp, origin, buf, strlen(buf));
	if (!t)
		return NULL;
	t->at_bol = false;
	return t;
}

/* a string literal token holding `s` (escaped) */
static struct cp_token *str_token(struct cp_pp *pp, struct cp_token *origin,
				  const char *s, size_t len, bool escape)
{
	size_t i, n = 0;
	char *buf;
	struct cp_token *t;

	buf = cp_alloc(pp->tu, len * 2 + 3);
	if (!buf)
		return NULL;
	buf[n++] = '"';
	for (i = 0; i < len; i++) {
		if (escape && (s[i] == '\\' || s[i] == '"'))
			buf[n++] = '\\';
		buf[n++] = s[i];
	}
	buf[n++] = '"';
	t = tokenize_text(pp, origin, buf, n);
	if (t)
		t->at_bol = false;
	return t;
}

/* hide sets */

static bool hs_contains(const struct cp_hideset *hs, const struct cp_macro *m)
{
	for (; hs; hs = hs->next)
		if (hs->m == m)
			return true;
	return false;
}

static struct cp_hideset *hs_cons(struct cp_pp *pp, const struct cp_macro *m,
				  struct cp_hideset *next)
{
	struct cp_hideset *hs = cp_alloc(pp->tu, sizeof(*hs));

	if (!hs)
		return next;
	hs->m = m;
	hs->next = next;
	return hs;
}

static struct cp_hideset *hs_union(struct cp_pp *pp, struct cp_hideset *a,
				   struct cp_hideset *b)
{
	struct cp_hideset *res = b;

	for (; a; a = a->next)
		if (!hs_contains(res, a->m))
			res = hs_cons(pp, a->m, res);
	return res;
}

static struct cp_hideset *hs_intersection(struct cp_pp *pp, struct cp_hideset *a,
					  struct cp_hideset *b)
{
	struct cp_hideset *res = NULL;

	for (; a; a = a->next)
		if (hs_contains(b, a->m) && !hs_contains(res, a->m))
			res = hs_cons(pp, a->m, res);
	return res;
}

/* copy the list, adding hs to every token's hide set */
static struct cp_token *add_hideset(struct cp_pp *pp, struct cp_token *tok,
				    struct cp_hideset *hs)
{
	struct cp_token head = { 0 };
	struct cp_token *cur = &head, *t;

	for (; tok->kind != CPT_EOF; tok = tok->next) {
		t = copy_token(pp, tok);
		if (!t)
			return fail_eof(pp, tok);
		t->hideset = hs_union(pp, t->hideset, hs);
		cur = cur->next = t;
	}
	cur->next = new_eof(pp, tok);
	return head.next;
}

/* macro table */

static struct cp_macro *find_macro(struct cp_pp *pp, const struct cp_token *tok)
{
	if (tok->kind != CPT_IDENT)
		return NULL;
	return cp_hashmap_get(&pp->macros, tok->loc, tok->len);
}

static struct cp_macro *new_macro(struct cp_pp *pp, const char *name, size_t len,
				  bool objlike)
{
	struct cp_hashmap_entry *ent;
	struct cp_macro *m;

	ent = cp_hashmap_get_or_insert(pp->tu, &pp->macros, name, (int)len);
	m = cp_alloc(pp->tu, sizeof(*m));
	if (!ent || !m)
		return NULL;
	m->name = ent->key;
	m->objlike = objlike;
	ent->val = m;
	return m;
}

static void undef_macro(struct cp_pp *pp, const char *name, size_t len)
{
	struct cp_hashmap_entry *ent;

	ent = cp_hashmap_get_or_insert(pp->tu, &pp->macros, name, (int)len);
	if (ent)
		ent->val = NULL;
}

static bool has_macro(struct cp_pp *pp, struct cp_token *tok)
{
	if (tok->kind != CPT_IDENT) {
		pp_error(pp, tok, "expected an identifier");
		return false;
	}
	return find_macro(pp, tok) != NULL;
}

/*
 * Parse "NAME [(params)] body..." from an EOF terminated list (a #define line
 * or a predefined macro).
 */
static void read_macro_definition(struct cp_pp *pp, struct cp_token *tok)
{
	struct cp_token *name = tok, **params = NULL;
	struct cp_macro *m;
	int nparams = 0, cap = 0;
	bool va = false, objlike = true;
	struct cp_token *p;
	int i;
	struct cp_token **np;

	if (tok->kind != CPT_IDENT) {
		pp_error(pp, tok, "macro name must be an identifier");
		return;
	}
	tok = tok->next;

	if (!tok->has_space && tok->kind == CPT_PUNCT && tok_is(tok, "(")) {
		objlike = false;
		tok = tok->next;
		while (!(tok->kind == CPT_PUNCT && tok_is(tok, ")"))) {
			if (nparams) {
				if (!tok_is(tok, ",")) {
					pp_error(pp, tok, "expected ',' in macro parameters");
					return;
				}
				tok = tok->next;
			}
			if (tok->kind == CPT_PUNCT && tok_is(tok, "...")) {
				p = pp->va_args_tok;
				va = true;
				tok = tok->next;
			} else if (tok->kind == CPT_IDENT) {
				p = tok;
				tok = tok->next;
				if (tok->kind == CPT_PUNCT && tok_is(tok, "...")) {	/* GNU: name... */
					va = true;
					tok = tok->next;
				}
			} else {
				pp_error(pp, tok, "invalid macro parameter");
				return;
			}
			for (i = 0; i < nparams; i++) {
				if (params[i]->len == p->len &&
				    !memcmp(params[i]->loc, p->loc, p->len)) {
					pp_error(pp, p, "duplicated macro parameter");
					return;
				}
			}
			if (nparams == cap) {
				cap = cap ? cap * 2 : 8;
				np = cp_alloc(pp->tu, cap * sizeof(*np));
				if (!np) {
					fail_eof(pp, tok);
					return;
				}
				if (nparams)
					memcpy(np, params, nparams * sizeof(*np));
				params = np;
			}
			params[nparams++] = p;
			if (va) {
				if (!(tok->kind == CPT_PUNCT && tok_is(tok, ")"))) {
					pp_error(pp, tok, "expected ')' after '...'");
					return;
				}
				break;
			}
			if (tok->kind == CPT_EOF) {
				pp_error(pp, name, "unterminated macro parameter list");
				return;
			}
		}
		tok = tok->next;	/* the ')' */
	}

	m = new_macro(pp, name->loc, name->len, objlike);
	if (!m) {
		fail_eof(pp, name);
		return;
	}
	m->va = va;
	m->nparams = nparams;
	m->params = params;
	m->body = tok;
}

/* macro arguments */

/*
 * Read one argument (or, read_rest, everything up to the closing paren) as a
 * copied EOF terminated list. *rest is the ')' or ',' that stopped it.
 */
static struct cp_token *read_macro_arg_one(struct cp_pp *pp, struct cp_token **rest,
					   struct cp_token *tok, bool read_rest)
{
	struct cp_token head = { 0 };
	struct cp_token *cur = &head, *start = tok;
	int level = 0;

	for (;;) {
		if (tok->kind == CPT_EOF) {
			*rest = tok;
			return pp_error(pp, start, "unterminated argument list");
		}
		if (tok->kind == CPT_PUNCT) {
			if (level == 0 && tok_is(tok, ")"))
				break;
			if (level == 0 && !read_rest && tok_is(tok, ","))
				break;
			if (tok_is(tok, "("))
				level++;
			else if (tok_is(tok, ")"))
				level--;
		}
		cur->next = copy_token(pp, tok);
		if (!cur->next) {
			*rest = tok;
			return fail_eof(pp, tok);
		}
		cur = cur->next;
		tok = tok->next;
	}
	cur->next = new_eof(pp, tok);
	*rest = tok;
	return head.next;
}

/* tok is the '(' after the macro name; *rest is the closing ')' */
static struct cp_macro_arg *read_macro_args(struct cp_pp *pp, struct cp_token **rest,
					    struct cp_token *tok, struct cp_macro *m,
					    struct cp_token *name_tok)
{
	struct cp_macro_arg *args;
	int i;
	bool is_va;
	bool omitted;

	args = cp_alloc(pp->tu, (m->nparams ? m->nparams : 1) * sizeof(*args));
	if (!args)
		goto err_mem;

	tok = tok->next;		/* past '(' */
	for (i = 0; i < m->nparams; i++) {
		is_va = m->va && i == m->nparams - 1;

		omitted = is_va && tok_is(tok, ")");

		if (i && !omitted) {
			if (!tok_is(tok, ",")) {
				*rest = pp_error(pp, name_tok,
						 "macro '%s' requires %d arguments", m->name,
						 m->nparams);
				return NULL;
			}
			tok = tok->next;
		}
		args[i].is_va = is_va;
		args[i].omitted = omitted;
		args[i].tok = read_macro_arg_one(pp, &tok, tok, is_va);
		if (cp_tu_failed(pp->tu)) {
			*rest = tok;
			return NULL;
		}
	}

	if (!tok_is(tok, ")")) {
		*rest = pp_error(pp, name_tok, "too many arguments to macro '%s'", m->name);
		return NULL;
	}
	*rest = tok;
	return args;

err_mem:
	*rest = fail_eof(pp, name_tok);
	return NULL;
}

/* full macro expansion of a standalone, EOF terminated list */
static struct cp_token *expand_list(struct cp_pp *pp, struct cp_token *tok)
{
	struct cp_token head = { 0 };
	struct cp_token *cur = &head;

	while (tok->kind != CPT_EOF && !cp_tu_failed(pp->tu)) {
		if (expand_macro(pp, &tok, tok))
			continue;
		cur->next = copy_token(pp, tok);
		if (!cur->next)
			return fail_eof(pp, tok);
		cur = cur->next;
		tok = tok->next;
	}
	cur->next = new_eof(pp, tok);
	return head.next;
}

static struct cp_token *expand_arg(struct cp_pp *pp, struct cp_macro_arg *arg)
{
	if (!arg->expanded)
		arg->expanded = expand_list(pp, arg->tok);
	return arg->expanded;
}

static bool has_non_empty_va_arg(struct cp_pp *pp, struct cp_macro *m,
				 struct cp_macro_arg *args)
{
	if (!m->va)
		return false;
	return expand_arg(pp, &args[m->nparams - 1])->kind != CPT_EOF;
}

static struct cp_macro_arg *find_arg(struct cp_macro *m, struct cp_macro_arg *args,
				     const struct cp_token *tok)
{
	int i;

	if (tok->kind != CPT_IDENT)
		return NULL;
	for (i = 0; i < m->nparams; i++)
		if (m->params[i]->len == tok->len &&
		    !memcmp(m->params[i]->loc, tok->loc, tok->len))
			return &args[i];
	return NULL;
}

/* the text of a token list, tokens separated by a space where they were */
static char *join_tokens(struct cp_pp *pp, const struct cp_token *tok,
			 const struct cp_token *end, bool add_slash, size_t *lenp)
{
	const struct cp_token *t;
	size_t len = 1, pos = 0;
	char *buf;
	int i;

	for (t = tok; t != end && t->kind != CPT_EOF; t = t->next) {
		if ((t->has_space || t->at_bol) && len != 1)
			len++;
		if (add_slash && (t->kind == CPT_STR || t->kind == CPT_CHAR))
			for (i = 0; i < t->len; i++)
				if (t->loc[i] == '\\' || t->loc[i] == '"')
					len++;
		len += t->len;
	}

	buf = cp_alloc(pp->tu, len);
	if (!buf)
		return NULL;

	for (t = tok; t != end && t->kind != CPT_EOF; t = t->next) {
		if ((t->has_space || t->at_bol) && pos)
			buf[pos++] = ' ';
		if (add_slash && (t->kind == CPT_STR || t->kind == CPT_CHAR)) {
			for (i = 0; i < t->len; i++) {
				if (t->loc[i] == '\\' || t->loc[i] == '"')
					buf[pos++] = '\\';
				buf[pos++] = t->loc[i];
			}
			continue;
		}
		memcpy(buf + pos, t->loc, t->len);
		pos += t->len;
	}
	buf[pos] = '\0';
	if (lenp)
		*lenp = pos;
	return buf;
}

/* the stringizing operator (#) */
static struct cp_token *stringize(struct cp_pp *pp, struct cp_token *hash,
				  struct cp_token *arg)
{
	size_t len;
	char *s;

	s = join_tokens(pp, arg, NULL, true, &len);
	if (!s)
		return fail_eof(pp, hash);
	/* join_tokens already escaped, so the literal text only needs quotes */
	return str_token(pp, hash, s, len, false);
}

/* the token pasting operator (##); NULL on error */
static struct cp_token *paste(struct cp_pp *pp, struct cp_token *lhs, struct cp_token *rhs)
{
	struct cp_token *tok;
	char *buf;

	buf = cp_format(pp->tu, "%.*s%.*s", lhs->len, lhs->loc, rhs->len, rhs->loc);
	if (!buf) {
		fail_eof(pp, lhs);
		return NULL;
	}

	tok = tokenize_text(pp, lhs, buf, strlen(buf));
	if (!tok)
		return NULL;
	if (tok->kind == CPT_EOF || tok->next->kind != CPT_EOF) {
		pp_error(pp, lhs, "pasting forms '%s', an invalid token", buf);
		return NULL;
	}
	tok->at_bol = lhs->at_bol;
	tok->has_space = lhs->has_space;
	tok->hideset = lhs->hideset;
	/*
	 * The result replaces lhs in place (see subst), so it must not point at
	 * lhs as its origin: that would be a cycle. It inherits lhs's origin, and
	 * expand_macro gives the tokens without one the macro invocation.
	 */
	tok->origin = lhs->origin;
	return tok;
}

static bool is_punct(const struct cp_token *tok, const char *s)
{
	return tok->kind == CPT_PUNCT && tok_is(tok, s);
}

/* replace the parameters of a function-like macro body with the arguments */
static struct cp_token *subst(struct cp_pp *pp, struct cp_macro *m,
			      struct cp_macro_arg *args, struct cp_token *tok)
{
	struct cp_token head = { 0 };
	struct cp_token *cur = &head, *t, *p;
	struct cp_macro_arg *arg, *arg2;
	struct cp_token *rhs;
	struct cp_token *inner;
	struct cp_token *rest;

#define ADD(_t) \
	do { \
		struct cp_token *_n = copy_token(pp, (_t)); \
		if (!_n) \
			return fail_eof(pp, tok); \
		cur = cur->next = _n; \
	} while (0)

	while (tok->kind != CPT_EOF && !cp_tu_failed(pp->tu)) {
		/* "#" followed by a parameter is replaced with the stringized actuals */
		if (is_punct(tok, "#")) {
			arg = find_arg(m, args, tok->next);
			if (!arg)
				return pp_error(pp, tok->next,
						"'#' is not followed by a macro parameter");
			t = stringize(pp, tok, arg->tok);
			if (!t)
				return fail_eof(pp, tok);
			cur = cur->next = t;
			tok = tok->next->next;
			continue;
		}

		/*
		 * [GNU] if the variable arguments are omitted, `, ## __VA_ARGS__`
		 * is the empty token list; otherwise it is `,` and the arguments
		 * (an argument that is present but empty keeps the comma).
		 */
		if (is_punct(tok, ",") && is_punct(tok->next, "##")) {
			arg = find_arg(m, args, tok->next->next);
			if (arg && arg->is_va) {
				if (arg->omitted) {
					tok = tok->next->next->next;
				} else {
					ADD(tok);
					tok = tok->next->next;
				}
				continue;
			}
		}

		if (is_punct(tok, "##")) {
			if (cur == &head)
				return pp_error(pp, tok, "'##' cannot appear at start of macro expansion");
			if (tok->next->kind == CPT_EOF)
				return pp_error(pp, tok, "'##' cannot appear at end of macro expansion");

			arg = find_arg(m, args, tok->next);
			if (arg) {
				if (arg->tok->kind != CPT_EOF) {
					t = paste(pp, cur, arg->tok);
					if (!t)
						return fail_eof(pp, tok);
					/* replace the last token with the pasted one */
					t->next = NULL;
					*cur = *t;
					for (p = arg->tok->next; p->kind != CPT_EOF; p = p->next)
						ADD(p);
				}
				tok = tok->next->next;
				continue;
			}

			t = paste(pp, cur, tok->next);
			if (!t)
				return fail_eof(pp, tok);
			t->next = NULL;
			*cur = *t;
			tok = tok->next->next;
			continue;
		}

		arg = find_arg(m, args, tok);
		if (arg && is_punct(tok->next, "##")) {
			rhs = tok->next->next;

			if (arg->tok->kind == CPT_EOF) {
				arg2 = find_arg(m, args, rhs);
				if (arg2) {
					for (p = arg2->tok; p->kind != CPT_EOF; p = p->next)
						ADD(p);
				} else {
					if (rhs->kind == CPT_EOF)
						return pp_error(pp, tok, "'##' cannot appear at end of macro expansion");
					ADD(rhs);
				}
				tok = rhs->kind == CPT_EOF ? rhs : rhs->next;
				continue;
			}

			for (p = arg->tok; p->kind != CPT_EOF; p = p->next)
				ADD(p);
			tok = tok->next;
			continue;
		}

		/* __VA_OPT__(x) is x if the variable arguments are not empty, else nothing */
		if (m->va && tok->kind == CPT_IDENT && tok_is(tok, "__VA_OPT__") &&
		    is_punct(tok->next, "(")) {
			inner = read_macro_arg_one(pp, &rest, tok->next->next, true);
			if (cp_tu_failed(pp->tu))
				return fail_eof(pp, tok);
			if (has_non_empty_va_arg(pp, m, args)) {
				inner = subst(pp, m, args, inner);
				for (p = inner; p->kind != CPT_EOF; p = p->next)
					ADD(p);
			}
			tok = rest->next;
			continue;
		}

		/* arguments are completely macro-expanded before substitution */
		if (arg) {
			t = expand_arg(pp, arg);
			for (p = t; p->kind != CPT_EOF; p = p->next) {
				ADD(p);
				/* the first token takes the parameter's spacing */
				if (p == t)
					cur->has_space = tok->has_space;
			}
			tok = tok->next;
			continue;
		}

		ADD(tok);
		tok = tok->next;
	}
	cur->next = new_eof(pp, tok);
	return head.next;
#undef ADD
}

/* builtin dynamic macros */

static struct cp_token *file_macro(struct cp_pp *pp, struct cp_token *tok,
				   struct cp_token **rest)
{
	const struct cp_token *root = cp_tok_root(tok);
	const char *name = root->file ? root->file->name : "";

	*rest = tok->next;
	return append(str_token(pp, tok, name, strlen(name), true), new_eof(pp, tok));
}

static struct cp_token *base_file_macro(struct cp_pp *pp, struct cp_token *tok,
					struct cp_token **rest)
{
	const char *name = pp->main_file->name;

	*rest = tok->next;
	return append(str_token(pp, tok, name, strlen(name), true), new_eof(pp, tok));
}

static struct cp_token *line_macro(struct cp_pp *pp, struct cp_token *tok,
				   struct cp_token **rest)
{
	*rest = tok->next;
	return append(num_token(pp, tok, cp_tok_root(tok)->line_no), new_eof(pp, tok));
}

static struct cp_token *counter_macro(struct cp_pp *pp, struct cp_token *tok,
				      struct cp_token **rest)
{
	*rest = tok->next;
	return append(num_token(pp, tok, pp->counter++), new_eof(pp, tok));
}

/* deterministic, so that the output does not depend on when it was made */
static struct cp_token *date_macro(struct cp_pp *pp, struct cp_token *tok,
				   struct cp_token **rest)
{
	*rest = tok->next;
	return append(str_token(pp, tok, "Jan  1 1970", 11, false), new_eof(pp, tok));
}

static struct cp_token *time_macro(struct cp_pp *pp, struct cp_token *tok,
				   struct cp_token **rest)
{
	*rest = tok->next;
	return append(str_token(pp, tok, "00:00:00", 8, false), new_eof(pp, tok));
}

static struct cp_token *timestamp_macro(struct cp_pp *pp, struct cp_token *tok,
					struct cp_token **rest)
{
	*rest = tok->next;
	return append(str_token(pp, tok, "Thu Jan  1 00:00:00 1970", 24, false),
		      new_eof(pp, tok));
}

/* the tokens inside "( ... )" following a builtin, as an EOF terminated list */
static struct cp_token *builtin_args(struct cp_pp *pp, struct cp_token *tok,
				     struct cp_token **rest)
{
	struct cp_token *inner, *r;

	if (!is_punct(tok->next, "(")) {
		*rest = pp_error(pp, tok, "expected '(' after '%.*s'", tok->len, tok->loc);
		return NULL;
	}
	inner = read_macro_arg_one(pp, &r, tok->next->next, true);
	if (cp_tu_failed(pp->tu)) {
		*rest = r;
		return NULL;
	}
	*rest = r->next;
	return inner;
}

static struct cp_token *pragma_macro(struct cp_pp *pp, struct cp_token *tok,
				     struct cp_token **rest)
{
	struct cp_token *args = builtin_args(pp, tok, rest);

	/* _Pragma("...") is dropped; only `#pragma` lines reach the parser */
	if (!args)
		return NULL;
	return new_eof(pp, tok);
}

static bool file_exists(const char *path)
{
	struct stat st;

	return !stat(path, &st) && S_ISREG(st.st_mode);
}

static char *dir_of(struct cp_pp *pp, const char *name)
{
	const char *slash = strrchr(name, '/');

	if (!slash)
		return cp_strndup(pp->tu, ".", 1);
	if (slash == name)
		return cp_strndup(pp->tu, "/", 1);
	return cp_strndup(pp->tu, name, (size_t)(slash - name));
}

/*
 * Find an include file. quoted: "x" form (the including file's directory and
 * the -iquote dirs come first). next: #include_next, start after the dir the
 * including file was found in. *idxp is the search dir index it was found in,
 * or -1.
 */
static const char *find_include(struct cp_pp *pp, const char *filename,
				const struct cp_file *from, bool quoted, bool next,
				int *idxp)
{
	size_t i, start = 0;
	char *path;

	*idxp = -1;
	if (filename[0] == '/')
		return file_exists(filename) ? filename : NULL;

	if (next) {
		if (from && from->incl_idx >= 0)
			start = (size_t)from->incl_idx + 1;
	} else if (quoted) {
		if (from) {
			path = cp_format(pp->tu, "%s/%s", dir_of(pp, from->name), filename);
			if (path && file_exists(path))
				return path;
		}
		for (i = 0; i < pp->num_qdirs; i++) {
			path = cp_format(pp->tu, "%s/%s", pp->qdirs[i], filename);
			if (path && file_exists(path))
				return path;
		}
	}

	for (i = start; i < pp->num_dirs; i++) {
		path = cp_format(pp->tu, "%s/%s", pp->dirs[i], filename);
		if (path && file_exists(path)) {
			*idxp = (int)i;
			return path;
		}
	}
	return NULL;
}

/*
 * Read an include argument: "file", <file>, or a macro expanding to one of
 * those. tok is an EOF terminated list. Returns the file name (NULL with a
 * diagnostic on error).
 */
static const char *read_filename(struct cp_pp *pp, struct cp_token *tok, bool *quotedp)
{
	struct cp_token *start;
	bool expanded = false;
	const char *name2;

	if (tok->kind == CPT_IDENT) {
		tok = expand_list(pp, tok);
		expanded = true;
	}

	if (tok->kind == CPT_STR) {
		if (tok->len < 3 || tok->loc[0] != '"') {
			pp_error(pp, tok, "expected a filename");
			return NULL;
		}
		*quotedp = true;
		/* no escape sequences in a header name: use the raw text */
		return cp_strndup(pp->tu, tok->loc + 1, tok->len - 2);
	}

	if (is_punct(tok, "<")) {
		start = tok;
		for (; !is_punct(tok, ">"); tok = tok->next) {
			if (tok->kind == CPT_EOF) {
				pp_error(pp, start, "expected '>'");
				return NULL;
			}
		}
		*quotedp = false;
		if (!expanded && start->file && start->file == tok->file && start->loc < tok->loc)
			name2 = cp_strndup(pp->tu, start->loc + 1,
					  (size_t)(tok->loc - start->loc - 1));
		else
			name2 = join_tokens(pp, start->next, tok, false, NULL);
		if (name2 && *name2)
			return name2;
	}

	pp_error(pp, tok, "expected a filename");
	return NULL;
}

/* __has_include(...) and __has_include_next(...) */
static struct cp_token *has_include_common(struct cp_pp *pp, struct cp_token *tok,
					   struct cp_token **rest, bool next)
{
	const struct cp_token *root = cp_tok_root(tok);
	struct cp_token *args = builtin_args(pp, tok, rest);
	const char *filename;
	bool quoted = false;
	int idx;

	if (!args)
		return NULL;
	filename = read_filename(pp, args, &quoted);
	if (!filename)
		return NULL;
	return append(num_token(pp, tok, find_include(pp, filename, root->file, quoted,
						      next, &idx) != NULL),
		      new_eof(pp, tok));
}

static struct cp_token *has_include_macro(struct cp_pp *pp, struct cp_token *tok,
					  struct cp_token **rest)
{
	return has_include_common(pp, tok, rest, false);
}

static struct cp_token *has_include_next_macro(struct cp_pp *pp, struct cp_token *tok,
					       struct cp_token **rest)
{
	return has_include_common(pp, tok, rest, true);
}

/*
 * __has_attribute and friends. Attributes are only skipped by the declaration
 * parser (apart from the layout ones), so claiming the common set is safe.
 */
static bool known_attribute(const char *s, size_t len)
{
	static const char *const names[] = {
		"aligned", "packed", "deprecated", "unused", "used", "visibility",
		"noreturn", "format", "format_arg", "nonnull", "always_inline", "noinline",
		"const", "pure", "malloc", "warn_unused_result", "sentinel", "weak",
		"alias", "section", "cold", "hot", "constructor", "destructor",
		"fallthrough", "mode", "vector_size", "transparent_union", "cleanup",
		"nothrow", "returns_nonnull", "alloc_size", "alloc_align", "access",
		"artificial", "gnu_inline", "may_alias", "noclone", "optimize", "flatten",
		"leaf", "externally_visible", "no_instrument_function", "error", "warning",
		"designated_init", "nonstring", "counted_by", "unavailable", "ifunc",
		"copy", "symver", "tls_model", "returns_twice", "no_sanitize_address",
		"target", "noipa", "no_reorder", "stack_protect", "assume_aligned",
	};
	size_t i;

	/* __name__ is the same as name */
	if (len > 4 && s[0] == '_' && s[1] == '_' && s[len - 1] == '_' && s[len - 2] == '_') {
		s += 2;
		len -= 4;
	}
	for (i = 0; i < ARRAY_SIZE(names); i++)
		if (strlen(names[i]) == len && !memcmp(names[i], s, len))
			return true;
	return false;
}

static struct cp_token *has_attribute_macro(struct cp_pp *pp, struct cp_token *tok,
					    struct cp_token **rest)
{
	struct cp_token *args = builtin_args(pp, tok, rest), *t;
	bool res = false;

	if (!args)
		return NULL;
	/* gnu::name and __gnu__::name: look at the last identifier */
	for (t = args; t->kind != CPT_EOF; t = t->next)
		if (t->kind == CPT_IDENT && t->next->kind == CPT_EOF)
			res = known_attribute(t->loc, t->len);
	return append(num_token(pp, tok, res), new_eof(pp, tok));
}

/* answers 0: the fallback paths of headers are always safe to take */
static struct cp_token *has_zero_macro(struct cp_pp *pp, struct cp_token *tok,
				       struct cp_token **rest)
{
	struct cp_token *args = builtin_args(pp, tok, rest);

	if (!args)
		return NULL;
	return append(num_token(pp, tok, 0), new_eof(pp, tok));
}

/* clang's __is_identifier(x): 1 unless x is a keyword */
static struct cp_token *is_identifier_macro(struct cp_pp *pp, struct cp_token *tok,
					    struct cp_token **rest)
{
	static const char *const keywords[] = {
		"auto", "break", "case", "char", "const", "continue", "default", "do",
		"double", "else", "enum", "extern", "float", "for", "goto", "if", "inline",
		"int", "long", "register", "restrict", "return", "short", "signed",
		"sizeof", "static", "struct", "switch", "typedef", "union", "unsigned",
		"void", "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool",
		"_Complex", "_Generic", "_Imaginary", "_Noreturn", "_Static_assert",
		"_Thread_local", "__attribute", "__attribute__", "__asm", "__asm__", "asm",
		"__typeof", "__typeof__", "typeof", "__extension__", "__inline", "__inline__",
		"__const", "__const__", "__volatile", "__volatile__", "__restrict",
		"__restrict__", "__signed", "__signed__", "__alignof", "__alignof__",
	};
	struct cp_token *args = builtin_args(pp, tok, rest);
	bool res = true;
	size_t i;

	if (!args)
		return NULL;
	if (args->kind == CPT_IDENT)
		for (i = 0; i < ARRAY_SIZE(keywords); i++)
			if (strlen(keywords[i]) == (size_t)args->len &&
			    !memcmp(keywords[i], args->loc, args->len))
				res = false;
	return append(num_token(pp, tok, res), new_eof(pp, tok));
}

static void add_builtin(struct cp_pp *pp, const char *name, cp_macro_handler fn)
{
	struct cp_macro *m = new_macro(pp, name, strlen(name), true);

	if (m)
		m->handler = fn;
}

/* "NAME", "NAME=VAL", "NAME(a,b)=VAL" -> a #define line */
static void define_from_string(struct cp_pp *pp, const char *def)
{
	const char *eq;
	struct cp_token *list;
	char *line;

	if (def[0] == '-') {
		undef_macro(pp, def + 1, strlen(def + 1));
		return;
	}
	eq = strchr(def, '=');

	if (eq)
		line = cp_format(pp->tu, "%.*s %s", (int)(eq - def), def, eq + 1);
	else
		line = cp_format(pp->tu, "%s 1", def);
	if (!line) {
		fail_eof(pp, pp->va_args_tok);
		return;
	}
	list = tokenize_text(pp, NULL, line, strlen(line));
	if (list)
		read_macro_definition(pp, list);
}

static void init_macros(struct cp_pp *pp)
{
	size_t i;

	define_from_string(pp, "__STDC__=1");
	define_from_string(pp, "__STDC_HOSTED__=1");
	define_from_string(pp, "__STDC_VERSION__=201710L");

	add_builtin(pp, "__FILE__", file_macro);
	add_builtin(pp, "__LINE__", line_macro);
	add_builtin(pp, "__COUNTER__", counter_macro);
	add_builtin(pp, "__BASE_FILE__", base_file_macro);
	add_builtin(pp, "__DATE__", date_macro);
	add_builtin(pp, "__TIME__", time_macro);
	add_builtin(pp, "__TIMESTAMP__", timestamp_macro);
	add_builtin(pp, "_Pragma", pragma_macro);
	add_builtin(pp, "__has_include", has_include_macro);
	add_builtin(pp, "__has_include_next", has_include_next_macro);
	add_builtin(pp, "__has_attribute", has_attribute_macro);
	add_builtin(pp, "__has_c_attribute", has_attribute_macro);
	add_builtin(pp, "__has_builtin", has_zero_macro);
	add_builtin(pp, "__has_feature", has_zero_macro);
	add_builtin(pp, "__has_extension", has_zero_macro);
	add_builtin(pp, "__has_cpp_attribute", has_zero_macro);
	add_builtin(pp, "__has_declspec_attribute", has_zero_macro);
	add_builtin(pp, "__has_warning", has_zero_macro);
	add_builtin(pp, "__building_module", has_zero_macro);
	add_builtin(pp, "__is_identifier", is_identifier_macro);

	for (i = 0; i < pp->tu->opts.num_defines && !cp_tu_failed(pp->tu); i++)
		define_from_string(pp, pp->tu->opts.defines[i]);
}

/* macro expansion */

static bool expand_macro(struct cp_pp *pp, struct cp_token **rest, struct cp_token *tok)
{
	struct cp_macro *m;
	struct cp_macro_arg *args;
	struct cp_token *body, *rparen, *macro_token, *t, *next;
	struct cp_hideset *hs;

	next = NULL;
	m = find_macro(pp, tok);
	if (!m || hs_contains(tok->hideset, m))
		return false;

	/* builtin dynamic macro such as __LINE__ */
	if (m->handler) {
		body = m->handler(pp, tok, &next);
		if (!body) {
			*rest = next ? next : new_eof(pp, tok);
			return true;
		}
		if (body->kind != CPT_EOF) {
			body->at_bol = false;
			body->has_space = tok->has_space;
		}
		*rest = append(body, next);
		return true;
	}

	/* object-like macro */
	if (m->objlike) {
		hs = hs_union(pp, tok->hideset, hs_cons(pp, m, NULL));
		body = add_hideset(pp, m->body, hs);
		for (t = body; t->kind != CPT_EOF; t = t->next)
			if (!t->origin)
				t->origin = tok;
		if (body->kind != CPT_EOF) {
			body->at_bol = false;
			body->has_space = tok->has_space;
		}
		*rest = append(body, tok->next);
		return true;
	}

	/* a function-like macro name not followed by '(' is an ordinary identifier */
	if (!is_punct(tok->next, "("))
		return false;

	macro_token = tok;
	args = read_macro_args(pp, &rparen, tok->next, m, macro_token);
	if (!args) {
		*rest = rparen;
		return true;
	}
	next = rparen->next;

	/*
	 * The tokens of the invocation may carry different hide sets; take the
	 * intersection of the macro name and the closing paren (Prosser).
	 */
	hs = hs_intersection(pp, macro_token->hideset, rparen->hideset);
	hs = hs_union(pp, hs, hs_cons(pp, m, NULL));

	body = subst(pp, m, args, m->body);
	body = add_hideset(pp, body, hs);
	for (t = body; t->kind != CPT_EOF; t = t->next)
		if (!t->origin)
			t->origin = macro_token;
	if (body->kind != CPT_EOF) {
		body->at_bol = false;
		body->has_space = macro_token->has_space;
	}
	*rest = append(body, next);
	return true;
}

/* conditionals */

static void push_cond(struct cp_pp *pp, struct cp_token *tok, bool active)
{
	struct cp_cond *c = cp_alloc(pp->tu, sizeof(*c));

	if (!c) {
		fail_eof(pp, tok);
		return;
	}
	c->tok = tok;
	c->been_active = active;
	c->next = pp->conds;
	pp->conds = c;
}

/*
 * Skip until the next #else, #elif or #endif at this level; returns the '#'
 * of that directive. Nested conditionals are skipped.
 */
static struct cp_token *skip_cond_incl(struct cp_token *tok)
{
	int lvl = 0;

	for (; tok->kind != CPT_EOF; tok = tok->next) {
		if (!is_hash(tok))
			continue;
		if (tok_is(tok->next, "if") || tok_is(tok->next, "ifdef") ||
		    tok_is(tok->next, "ifndef")) {
			lvl++;
			tok = tok->next;
			continue;
		}
		if (lvl && tok_is(tok->next, "endif")) {
			lvl--;
			tok = tok->next;
			continue;
		}
		if (lvl == 0 && (tok_is(tok->next, "endif") || tok_is(tok->next, "else") ||
				 tok_is(tok->next, "elif") || tok_is(tok->next, "elifdef") ||
				 tok_is(tok->next, "elifndef")))
			break;
	}
	return tok;
}

/* the line of an #if as an expanded token list with `defined` resolved */
static struct cp_token *read_const_expr(struct cp_pp *pp, struct cp_token *tok)
{
	struct cp_token head = { 0 };
	struct cp_token *cur = &head, *t, *start;
	bool paren;

	while (tok->kind != CPT_EOF && !cp_tu_failed(pp->tu)) {
		if (tok->kind == CPT_IDENT && tok_is(tok, "defined")) {
			start = tok;
			tok = tok->next;
			paren = is_punct(tok, "(");
			if (paren)
				tok = tok->next;
			t = num_token(pp, start, has_macro(pp, tok));
			if (!t || cp_tu_failed(pp->tu))
				return fail_eof(pp, start);
			cur = cur->next = t;
			tok = tok->next;
			if (paren) {
				if (!is_punct(tok, ")"))
					return pp_error(pp, tok, "expected ')'");
				tok = tok->next;
			}
			continue;
		}

		if (expand_macro(pp, &tok, tok))
			continue;

		if (tok->kind == CPT_IDENT) {
			/* an identifier that is left over is 0 */
			t = num_token(pp, tok, 0);
			if (!t)
				return fail_eof(pp, tok);
			cur = cur->next = t;
			tok = tok->next;
			continue;
		}

		t = copy_token(pp, tok);
		if (!t)
			return fail_eof(pp, tok);
		cur = cur->next = t;
		tok = tok->next;
	}
	cur->next = new_eof(pp, tok);
	return head.next;
}

/* evaluate the rest of an #if / #elif line; *rest is the start of the next line */
static bool eval_if(struct cp_pp *pp, struct cp_token *start, struct cp_token **rest,
		    const char *what)
{
	struct cp_token *line, *end;
	struct cp_value v;

	line = split_line(pp, rest, start);
	if (line->kind == CPT_EOF) {
		pp_error(pp, start, "#%s with no expression", what);
		return false;
	}
	line = read_const_expr(pp, line);
	if (cp_tu_failed(pp->tu))
		return false;
	if (cp_eval_int_expr(pp->tu, line, &end, &v, NULL))
		return false;
	if (end->kind != CPT_EOF) {
		pp_error(pp, end, "extra token in #%s expression", what);
		return false;
	}
	return v.val != 0;
}

/* includes */

static char *real_path_of(struct cp_pp *pp, const char *path)
{
	char *rp = realpath(path, NULL), *res;

	if (!rp)
		return cp_strndup(pp->tu, path, strlen(path));
	res = cp_strndup(pp->tu, rp, strlen(rp));
	free(rp);
	return res;
}

/*
 * If the whole file is `#ifndef G ... #endif` (the include guard idiom), return
 * G. A later include is a no-op while G is defined.
 */
static struct cp_token *detect_include_guard(struct cp_token *list)
{
	struct cp_token *t, *guard;
	int depth = 0;

	if (!(is_hash(list) && tok_is(list->next, "ifndef") &&
	      list->next->next->kind == CPT_IDENT))
		return NULL;
	guard = list->next->next;

	for (t = list; t->kind != CPT_EOF; t = t->next) {
		if (!is_hash(t))
			continue;
		if (tok_is(t->next, "if") || tok_is(t->next, "ifdef") ||
		    tok_is(t->next, "ifndef")) {
			depth++;
		} else if (tok_is(t->next, "endif")) {
			if (--depth == 0) {
				/* nothing may follow the matching #endif */
				for (t = t->next->next; t->kind != CPT_EOF && !t->at_bol; t = t->next)
					;
				return t->kind == CPT_EOF ? guard : NULL;
			}
		} else if (depth == 1 && (tok_is(t->next, "else") || tok_is(t->next, "elif") ||
					  tok_is(t->next, "elifdef") ||
					  tok_is(t->next, "elifndef"))) {
			return NULL;
		}
	}
	return NULL;
}

static struct cp_token *include_file(struct cp_pp *pp, struct cp_token *rest,
				     const char *path, struct cp_token *filename_tok,
				     int idx)
{
	const struct cp_token *root = cp_tok_root(filename_tok);
	struct cp_file *file;
	struct cp_token *list, *last, *guard;
	const char *rp, *gname;
	bool system;
	struct cp_hashmap_entry *ent;

	rp = real_path_of(pp, path);
	if (!rp)
		return fail_eof(pp, filename_tok);

	if (cp_hashmap_get(&pp->pragma_once, rp, (int)strlen(rp)))
		return rest;
	gname = cp_hashmap_get(&pp->guards, rp, (int)strlen(rp));
	if (gname && cp_hashmap_get(&pp->macros, gname, (int)strlen(gname)))
		return rest;

	system = (root->file && root->file->system_header) ||
		 (idx >= 0 && pp->dir_system[idx]);
	file = cp_file_add_from_path(pp->tu, path, system, false);
	if (!file)
		return new_eof(pp, filename_tok);
	file->real_path = rp;
	file->incl_idx = idx;

	list = cp_tokenize(pp->tu, file);
	if (!list || cp_tu_failed(pp->tu))
		return new_eof(pp, filename_tok);
	if (list->kind == CPT_EOF)
		return rest;

	guard = detect_include_guard(list);
	if (guard) {
		ent = cp_hashmap_get_or_insert(pp->tu, &pp->guards, rp, (int)strlen(rp));
		if (ent)
			ent->val = cp_strndup(pp->tu, guard->loc, guard->len);
	}

	/* the file's EOF is replaced by the rest of the including stream */
	for (last = list; last->next->kind != CPT_EOF; last = last->next)
		;
	last->next = rest;
	return list;
}

static struct cp_token *do_include(struct cp_pp *pp, struct cp_token *tok, bool next)
{
	struct cp_token *file_tok = tok->next, *rest, *line;
	const struct cp_token *root = cp_tok_root(tok);
	const char *filename, *path;
	bool quoted = false;
	int idx;

	line = split_line(pp, &rest, file_tok);
	filename = read_filename(pp, line, &quoted);
	if (!filename)
		return new_eof(pp, tok);

	path = find_include(pp, filename, root->file, quoted, next, &idx);
	if (!path) {
		cp_error_tok(pp->tu, file_tok, "'%s' file not found", filename);
		return new_eof(pp, tok);
	}
	return include_file(pp, rest, path, file_tok, idx);
}

/* `#pragma` lines other than `once` stay in the stream for the parser */
/*
 * The last token of the line is marked: once macros expand, at_bol cannot tell
 * where a pragma line ends (a macro invocation at the start of the next line
 * loses its at_bol).
 */
static struct cp_token *pass_line(struct cp_token **cur, struct cp_token *tok)
{
	do {
		(*cur)->next = tok;
		*cur = tok;
		tok = tok->next;
	} while (!tok->at_bol && tok->kind != CPT_EOF);
	(*cur)->eol = true;
	return tok;
}

static struct cp_token *directives(struct cp_pp *pp, struct cp_token **cur,
				   struct cp_token *start)
{
	struct cp_token *tok = start->next, *rest;
	struct cp_cond *cond;
	bool active;
	struct cp_token *line;
	bool neg;
	bool is_elif;
	bool is_error;
	char *msg;
	const struct cp_token *root;
	struct cp_hashmap_entry *ent;
	const char *rp;

	if (tok->at_bol || tok->kind == CPT_EOF)	/* the null directive */
		return tok;

	if (tok_is(tok, "include"))
		return do_include(pp, tok, false);

	if (tok_is(tok, "include_next"))
		return do_include(pp, tok, true);

	if (tok_is(tok, "define")) {
		line = split_line(pp, &rest, tok->next);

		read_macro_definition(pp, line);
		return rest;
	}

	if (tok_is(tok, "undef")) {
		tok = tok->next;
		if (tok->kind != CPT_IDENT)
			return pp_error(pp, tok, "macro name must be an identifier");
		undef_macro(pp, tok->loc, tok->len);
		return skip_line(pp, tok->next);
	}

	if (tok_is(tok, "if")) {
		active = eval_if(pp, tok->next, &rest, "if");
		if (cp_tu_failed(pp->tu))
			return new_eof(pp, tok);
		push_cond(pp, start, active);
		return active ? rest : skip_cond_incl(rest);
	}

	if (tok_is(tok, "ifdef") || tok_is(tok, "ifndef")) {
		neg = tok_is(tok, "ifndef");

		active = has_macro(pp, tok->next) != neg;
		if (cp_tu_failed(pp->tu))
			return new_eof(pp, tok);
		push_cond(pp, start, active);
		rest = skip_line(pp, tok->next->next);
		return active ? rest : skip_cond_incl(rest);
	}

	if (tok_is(tok, "elif") || tok_is(tok, "elifdef") || tok_is(tok, "elifndef")) {
		is_elif = tok_is(tok, "elif");
		neg = tok_is(tok, "elifndef");

		cond = pp->conds;
		if (!cond || cond->is_else)
			return pp_error(pp, start, "stray #%.*s", tok->len, tok->loc);

		if (cond->been_active)
			return skip_cond_incl(tok->next);

		if (is_elif) {
			active = eval_if(pp, tok->next, &rest, "elif");
		} else {
			active = has_macro(pp, tok->next) != neg;
			rest = skip_line(pp, tok->next->next);
		}
		if (cp_tu_failed(pp->tu))
			return new_eof(pp, tok);
		if (active) {
			cond->been_active = true;
			return rest;
		}
		return skip_cond_incl(rest);
	}

	if (tok_is(tok, "else")) {
		cond = pp->conds;
		if (!cond || cond->is_else)
			return pp_error(pp, start, "stray #else");
		cond->is_else = true;
		rest = skip_line(pp, tok->next);
		return cond->been_active ? skip_cond_incl(rest) : rest;
	}

	if (tok_is(tok, "endif")) {
		if (!pp->conds)
			return pp_error(pp, start, "stray #endif");
		pp->conds = pp->conds->next;
		return skip_line(pp, tok->next);
	}

	/* line control is accepted and ignored; the tokenizer's lines are real */
	if (tok_is(tok, "line") || tok->kind == CPT_NUM) {
		split_line(pp, &rest, tok);
		return rest;
	}

	if (tok_is(tok, "error") || tok_is(tok, "warning")) {
		is_error = tok_is(tok, "error");
		line = split_line(pp, &rest, tok->next);
		msg = join_tokens(pp, line, NULL, false, NULL);

		if (is_error)
			cp_error_tok(pp->tu, tok, "#error %s", msg ? msg : "");
		else
			cp_warn_tok(pp->tu, tok, "#warning %s", msg ? msg : "");
		return is_error ? new_eof(pp, tok) : rest;
	}

	if (tok_is(tok, "pragma")) {
		if (tok_is(tok->next, "once") && !tok->next->at_bol) {
			root = cp_tok_root(tok);
			rp = root->file && root->file->real_path ?
				root->file->real_path : root->file ? root->file->name : "";

			ent = cp_hashmap_get_or_insert(pp->tu, &pp->pragma_once, rp,
						       (int)strlen(rp));
			if (ent)
				ent->val = (void *)1;
			return skip_line(pp, tok->next->next);
		}
		return pass_line(cur, start);
	}

	return pp_error(pp, tok, "invalid preprocessor directive");
}

static struct cp_token *pp_run(struct cp_pp *pp, struct cp_token *tok)
{
	struct cp_token head = { 0 };
	struct cp_token *cur = &head;

	while (tok->kind != CPT_EOF && !cp_tu_failed(pp->tu)) {
		if (expand_macro(pp, &tok, tok))
			continue;

		if (!is_hash(tok)) {
			cur = cur->next = tok;
			tok = tok->next;
			continue;
		}

		tok = directives(pp, &cur, tok);
	}
	cur->next = tok;
	return head.next;
}

/*
 * `#pragma pack` applies to the tokens that follow it, so it becomes an
 * annotation (token->pack) rather than tokens the parser would have to look
 * for everywhere. Every other pragma is dropped. Pragmas are in stream order
 * here, so includes (push in one header, pop in the same) work out.
 */
static struct cp_token *apply_pragmas(struct cp_pp *pp, struct cp_token *list)
{
	struct cp_token head = { .next = list };
	struct cp_token *prev = &head, *tok = list, *t;
	uint8_t stack[64];
	int depth = 0;
	uint8_t cur = 0;
	bool push2;
	bool pop2;
	bool have_num;
	uint64_t num;
	bool last;

	while (tok->kind != CPT_EOF) {
		if (!(is_hash(tok) && tok_is(tok->next, "pragma"))) {
			tok->pack = cur;
			prev = tok;
			tok = tok->next;
			continue;
		}

		/* the pragma line runs up to its token marked eol */
		t = tok->next->next;
		if (t->kind == CPT_IDENT && tok_is(t, "pack") && !tok->next->eol &&
		    is_punct(t->next, "(")) {
			push2 = false;
			pop2 = false;
			have_num = false;
			num = 0;

			for (t = t->next->next; t->kind != CPT_EOF && !is_punct(t, ")");
			     t = t->next) {
				if (t->kind == CPT_IDENT && tok_is(t, "push2"))
					push2 = true;
				else if (t->kind == CPT_IDENT && tok_is(t, "pop2"))
					pop2 = true;
				else if (t->kind == CPT_NUM) {
					have_num = true;
					num = strtoull(t->loc, NULL, 0);
				}
			}
			if (push2) {
				if (depth < (int)sizeof(stack))
					stack[depth++] = cur;
				if (have_num)
					cur = (uint8_t)num;
			} else if (pop2) {
				cur = depth ? stack[--depth] : 0;
			} else {
				cur = have_num ? (uint8_t)num : 0;
			}
		}

		/* drop the whole line, up to and including its marked last token */
		while (tok->kind != CPT_EOF) {
			last = tok->eol;

			tok = tok->next;
			if (last)
				break;
		}
		prev->next = tok;
	}
	(void)pp;
	return head.next;
}

struct cp_token *cp_preprocess(struct cp_tu *tu, struct cp_file *main_file)
{
	const struct cp_options *o = &tu->opts;
	struct cp_pp *pp;
	struct cp_token *list, *out, *t, *forced, **forced_tail;
	size_t i, nd = 0, nq = 0;
	struct cp_file *ff;

	pp = cp_alloc(tu, sizeof(*pp));
	if (!pp) {
		cp_error_at(tu, NULL, NULL, "out of memory");
		return NULL;
	}
	pp->tu = tu;
	pp->main_file = main_file;
	tu->pp = pp;

	pp->dirs = cp_alloc(tu, (o->num_include_paths + 1) * sizeof(*pp->dirs));
	pp->dir_system = cp_alloc(tu, (o->num_include_paths + 1) * sizeof(*pp->dir_system));
	pp->qdirs = cp_alloc(tu, (o->num_include_paths + 1) * sizeof(*pp->qdirs));
	if (!pp->dirs || !pp->dir_system || !pp->qdirs) {
		cp_error_at(tu, NULL, NULL, "out of memory");
		return NULL;
	}
	/* -I before system dirs regardless of the order they were given in */
	for (i = 0; i < o->num_include_paths; i++) {
		if (o->include_paths[i].kind == CPIK_QUOTE)
			pp->qdirs[nq++] = cp_strndup(tu, o->include_paths[i].path,
						     strlen(o->include_paths[i].path));
		else if (o->include_paths[i].kind == CPIK_USER) {
			pp->dir_system[nd] = false;
			pp->dirs[nd++] = cp_strndup(tu, o->include_paths[i].path,
						    strlen(o->include_paths[i].path));
		}
	}
	for (i = 0; i < o->num_include_paths; i++) {
		if (o->include_paths[i].kind == CPIK_SYSTEM) {
			pp->dir_system[nd] = true;
			pp->dirs[nd++] = cp_strndup(tu, o->include_paths[i].path,
						    strlen(o->include_paths[i].path));
		}
	}
	pp->num_dirs = nd;
	pp->num_qdirs = nq;

	/* the implicit __VA_ARGS__ parameter */
	t = tokenize_text(pp, NULL, "__VA_ARGS__", 11);
	if (!t)
		return NULL;
	pp->va_args_tok = t;

	init_macros(pp);
	if (cp_tu_failed(tu))
		return NULL;

	if (!main_file->real_path)
		main_file->real_path = real_path_of(pp, main_file->name);

	/* the files of macros: the macros stay, the text goes */
	for (i = 0; i < o->num_macro_files; i++) {
		ff = cp_file_add_from_path(tu, o->macro_files[i], false, false);

		if (!ff)
			return NULL;
		ff->real_path = real_path_of(pp, ff->name);
		list = cp_tokenize(tu, ff);
		if (!list || cp_tu_failed(tu))
			return NULL;
		list = pp_run(pp, list);
		if (!list || cp_tu_failed(tu))
			return NULL;
	}

	/* forced includes first, in order; they share the macro state */
	forced = NULL;
	forced_tail = &forced;
	for (i = 0; i < o->num_force_includes; i++) {
		ff = cp_file_add_from_path(tu, o->force_includes[i], false, false);

		if (!ff)
			return NULL;
		ff->real_path = real_path_of(pp, ff->name);
		list = cp_tokenize(tu, ff);
		if (!list || cp_tu_failed(tu))
			return NULL;
		list = pp_run(pp, list);
		if (!list || cp_tu_failed(tu))
			return NULL;
		*forced_tail = list;
		while (*forced_tail && (*forced_tail)->kind != CPT_EOF)
			forced_tail = &(*forced_tail)->next;
	}

	list = cp_tokenize(tu, main_file);
	if (!list || cp_tu_failed(tu))
		return list;

	out = pp_run(pp, list);
	if (out && forced) {
		*forced_tail = out;
		out = forced;
	}
	if (out && !cp_tu_failed(tu))
		out = apply_pragmas(pp, out);

	if (pp->conds && !cp_tu_failed(tu))
		cp_error_tok(tu, pp->conds->tok, "unterminated conditional directive");
	return out;
}
