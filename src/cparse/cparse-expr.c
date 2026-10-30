/*
 * cparse-expr.c - C declaration front end: integer constant expressions
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Operates on tokens directly (no AST). Used for `#if` and by the declaration
 * parser (array sizes, enum values, bit-field widths, alignment).
 *
 * Values have a C integer type: int, unsigned int, long or unsigned long
 * (long long is as wide as long), with the usual arithmetic conversions and
 * wrap around. Without hooks (the preprocessor) everything is intmax_t /
 * uintmax_t, as `#if` requires.
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <string.h>

#include "cparse-private.h"

struct cp_eval {
	struct cp_tu *tu;
	const struct cp_expr_hooks *hooks;
	bool failed;
	bool no_comma;			/* declarations: ',' ends the expression */
	bool wide;			/* preprocessor: every value is 64 bit */
};

static void eval_error(struct cp_eval *ev, const struct cp_token *tok,
		       const char *fmt, ...) FY_FORMAT(printf, 3, 4);

static void eval_error(struct cp_eval *ev, const struct cp_token *tok,
		       const char *fmt, ...)
{
	va_list ap;
	char *msg;

	if (ev->failed)
		return;
	ev->failed = true;

	va_start(ap, fmt);
	msg = cp_vformat(ev->tu, fmt, ap);
	va_end(ap);
	cp_error_tok(ev->tu, tok, "%s", msg ? msg : "invalid expression");
}

/* make a value of the given type, wrapped to its width */
static struct cp_value mk(const struct cp_eval *ev, uint64_t val, bool uns, bool lng)
{
	struct cp_value v;

	lng = lng || ev->wide;
	if (!lng) {
		val = (uint32_t)val;
		if (!uns && (val & 0x80000000u))
			val |= ~UINT64_C(0xffffffff);
	}
	v.val = val;
	v.is_unsigned = uns;
	v.is_long = lng;
	return v;
}

static struct cp_value mk_int(const struct cp_eval *ev, uint64_t val)
{
	return mk(ev, val, false, false);
}

/* the type of the result of a binary arithmetic operation (usual conversions) */
static struct cp_value arith(const struct cp_eval *ev, uint64_t val,
			     struct cp_value l, struct cp_value r)
{
	bool lng = l.is_long || r.is_long, uns;

	if (l.is_long == r.is_long)
		uns = l.is_unsigned || r.is_unsigned;
	else	/* the wider type wins, signed or not: it can hold every value of the other */
		uns = l.is_long ? l.is_unsigned : r.is_unsigned;
	return mk(ev, val, uns, lng);
}

/* integer literal with C suffixes (u, l, ll) and the C rules for its type */
static int parse_int_literal(struct cp_eval *ev, const struct cp_token *tok,
			     struct cp_value *out)
{
	const char *p = tok->loc, *end = tok->loc + tok->len;
	uint64_t val = 0, digit, max;
	int base = 10, nl = 0;
	bool uns = false, overflow = false, any = false, lng, u_res;

	if (p[0] == '0' && p + 1 < end && (p[1] == 'x' || p[1] == 'X')) {
		base = 16;
		p += 2;
	} else if (p[0] == '0' && p + 1 < end && (p[1] == 'b' || p[1] == 'B')) {
		base = 2;
		p += 2;
	} else if (p[0] == '0') {
		base = 8;
	}

	for (; p < end; p++) {
		if (*p == '\'')		/* C23 digit separator */
			continue;
		if (isdigit((unsigned char)*p))
			digit = *p - '0';
		else if (base == 16 && isxdigit((unsigned char)*p))
			digit = (tolower((unsigned char)*p) - 'a') + 10;
		else
			break;
		if (digit >= (uint64_t)base)
			break;
		max = UINT64_MAX / base;
		if (val > max || (val == max && digit > UINT64_MAX % base))
			overflow = true;
		val = val * base + digit;
		any = true;
	}
	if (!any && base != 8)
		goto bad;

	for (; p < end; p++) {
		if (*p == 'u' || *p == 'U') {
			if (uns)
				goto bad;
			uns = true;
		} else if (*p == 'l' || *p == 'L') {
			if (++nl > 2)
				goto bad;
		} else {
			goto bad;
		}
	}

	if (overflow)
		eval_error(ev, tok, "integer constant is too large");

	/*
	 * C11 6.4.4.1: the first of these types the value fits in. Decimal
	 * without a suffix is never unsigned (GCC makes it so when nothing else
	 * fits. This code does the same).
	 */
	/* where long is 32 bit (ILP32), `L` leaves the value as wide as an int */
	if (nl == 1 && ev->tu->target->long_.size <= 4)
		nl = 0;
	lng = nl > 0;
	u_res = uns;
	if (!lng) {
		if (uns) {
			if (val > UINT32_MAX)
				lng = true;
		} else if (base == 10) {
			if (val > INT32_MAX)
				lng = true;
		} else {
			if (val > UINT32_MAX) {
				lng = true;
			} else if (val > INT32_MAX) {
				u_res = true;
			}
		}
	}
	if (lng && !u_res && val > (uint64_t)INT64_MAX)
		u_res = true;

	/* the value is representable by construction: no wrapping */
	out->val = val;
	out->is_unsigned = u_res;
	out->is_long = lng || ev->wide;
	return 0;

bad:
	if (memchr(tok->loc, '.', tok->len) ||
	    (base != 16 && (memchr(tok->loc, 'e', tok->len) || memchr(tok->loc, 'E', tok->len))))
		eval_error(ev, tok, "floating constant in an integer constant expression");
	else
		eval_error(ev, tok, "invalid integer constant '%.*s'", tok->len, tok->loc);
	return -1;
}

static struct cp_value eval_cond(struct cp_eval *ev, struct cp_token **rest,
				 struct cp_token *tok, bool skip);

static int binary_prec(const struct cp_token *tok)
{
	static const struct { const char *op; int prec; } ops[] = {
		{ "||", 1 }, { "&&", 2 }, { "|", 3 }, { "^", 4 }, { "&", 5 },
		{ "==", 6 }, { "!=", 6 },
		{ "<", 7 }, { ">", 7 }, { "<=", 7 }, { ">=", 7 },
		{ "<<", 8 }, { ">>", 8 },
		{ "+", 9 }, { "-", 9 },
		{ "*", 10 }, { "/", 10 }, { "%", 10 },
	};
	size_t i;

	if (tok->kind != CPT_PUNCT)
		return 0;
	for (i = 0; i < ARRAY_SIZE(ops); i++)
		if (cp_tok_equal(tok, ops[i].op))
			return ops[i].prec;
	return 0;
}

/* convert to an integer type: truncate, then sign or zero extend */
static struct cp_value convert_to(const struct cp_eval *ev, struct cp_value v,
				  const struct cp_eval_type *t)
{
	uint64_t mask;

	if (t->bits > 0 && t->bits < 64) {
		mask = (UINT64_C(1) << t->bits) - 1;

		v.val &= mask;
		if (!t->is_unsigned && (v.val >> (t->bits - 1)))
			v.val |= ~mask;
	}
	v.is_unsigned = t->is_unsigned;
	v.is_long = t->bits > 32 || ev->wide;
	return v;
}

/* sizeof / _Alignof of a parenthesized type name */
static void eval_sizeof_alignof(struct cp_eval *ev, struct cp_token **rest,
				struct cp_token *tok, bool is_sizeof, struct cp_value *out)
{
	const struct cp_expr_hooks *h = ev->hooks;
	struct cp_eval_type t;
	struct cp_token *p = tok->next;

	if (!(p->kind == CPT_PUNCT && cp_tok_equal(p, "(")) || !h || !h->is_type_start ||
	    !h->is_type_start(h->ud, p->next)) {
		if (is_sizeof)
			eval_error(ev, tok, "unsupported sizeof operand in a constant expression");
		else
			eval_error(ev, tok, "expected a type name");
		return;
	}
	if (h->parse_type(h->ud, p->next, &p, &t)) {
		ev->failed = true;
		return;
	}
	if (!(p->kind == CPT_PUNCT && cp_tok_equal(p, ")"))) {
		eval_error(ev, p, "expected ')'");
		return;
	}
	if (t.size < 0) {
		eval_error(ev, tok, "invalid application of '%s' to an incomplete type",
			   is_sizeof ? "sizeof" : "_Alignof");
		return;
	}
	/* the result is size_t */
	*out = mk(ev, is_sizeof ? (uint64_t)t.size : (uint64_t)t.align, true,
		  ev->tu->target->ptr.size > 4);
	*rest = p->next;
}

static struct cp_value eval_unary(struct cp_eval *ev, struct cp_token **rest,
				  struct cp_token *tok, bool skip)
{
	const struct cp_expr_hooks *h = ev->hooks;
	struct cp_value v;
	struct cp_token *p;
	struct cp_eval_type t;

	if (ev->failed)
		return mk_int(ev, 0);

	if (h && tok->kind == CPT_IDENT) {
		if (cp_tok_equal(tok, "__extension__"))
			return eval_unary(ev, rest, tok->next, skip);
		if (cp_tok_equal(tok, "sizeof") || cp_tok_equal(tok, "_Alignof") ||
		    cp_tok_equal(tok, "__alignof__") || cp_tok_equal(tok, "__alignof") ||
		    cp_tok_equal(tok, "alignof")) {
			eval_sizeof_alignof(ev, rest, tok, cp_tok_equal(tok, "sizeof"), &v);
			return ev->failed ? mk_int(ev, 0) : v;
		}
		if (cp_tok_equal(tok, "__builtin_offsetof") && cp_tok_equal(tok->next, "(") &&
		    h->offsetof_) {
			if (h->offsetof_(h->ud, tok->next->next, &p, &v)) {
				ev->failed = true;
				return mk_int(ev, 0);
			}
			if (!cp_tok_equal(p, ")")) {
				eval_error(ev, p, "expected ')'");
				return mk_int(ev, 0);
			}
			*rest = p->next;
			return mk(ev, v.val, true, true);
		}
	}

	/* a cast: (type) operand */
	if (h && h->is_type_start && tok->kind == CPT_PUNCT && cp_tok_equal(tok, "(") &&
	    h->is_type_start(h->ud, tok->next)) {
		if (h->parse_type(h->ud, tok->next, &p, &t)) {
			ev->failed = true;
			return mk_int(ev, 0);
		}
		if (!(p->kind == CPT_PUNCT && cp_tok_equal(p, ")"))) {
			eval_error(ev, p, "expected ')'");
			return mk_int(ev, 0);
		}
		if (!t.is_integer) {
			eval_error(ev, tok, "cast to a non-integer type in a constant expression");
			return mk_int(ev, 0);
		}
		v = eval_unary(ev, rest, p->next, skip);
		return convert_to(ev, v, &t);
	}

	if (cp_tok_equal(tok, "+")) {
		return eval_unary(ev, rest, tok->next, skip);
	} else if (cp_tok_equal(tok, "-")) {
		v = eval_unary(ev, rest, tok->next, skip);
		return mk(ev, -v.val, v.is_unsigned, v.is_long);
	} else if (cp_tok_equal(tok, "~")) {
		v = eval_unary(ev, rest, tok->next, skip);
		return mk(ev, ~v.val, v.is_unsigned, v.is_long);
	} else if (cp_tok_equal(tok, "!")) {
		v = eval_unary(ev, rest, tok->next, skip);
		return mk_int(ev, !v.val);
	} else if (cp_tok_equal(tok, "(")) {
		v = eval_cond(ev, &tok, tok->next, skip);
		if (ev->failed)
			return mk_int(ev, 0);
		if (!cp_tok_equal(tok, ")")) {
			eval_error(ev, tok, "expected ')'");
			return mk_int(ev, 0);
		}
		*rest = tok->next;
		return v;
	}

	switch (tok->kind) {
	case CPT_NUM:
		if (parse_int_literal(ev, tok, &v))
			return mk_int(ev, 0);
		*rest = tok->next;
		return v;
	case CPT_CHAR:
		*rest = tok->next;
		return mk_int(ev, (uint64_t)tok->ival);
	case CPT_IDENT:
		*rest = tok->next;
		if (h && h->lookup_ident) {
			if (!h->lookup_ident(h->ud, tok, &v))
				return v;
			eval_error(ev, tok, "'%.*s' is not a constant in a constant expression",
				   tok->len, tok->loc);
			return mk_int(ev, 0);
		}
		/* in the preprocessor what is left after macro expansion is 0 */
		return mk_int(ev, 0);
	default:
		eval_error(ev, tok, "unexpected '%.*s' in expression", tok->len, tok->loc);
		return mk_int(ev, 0);
	}
}

static struct cp_value apply_binary(struct cp_eval *ev, const struct cp_token *optok,
				    struct cp_value l, struct cp_value r, bool skip)
{
	bool is_div;
	bool left;
	bool neg;
	uint64_t n;

	bool uns = l.is_long == r.is_long ? (l.is_unsigned || r.is_unsigned) :
		   (l.is_long ? l.is_unsigned : r.is_unsigned);
	int64_t sl = (int64_t)l.val, sr = (int64_t)r.val;

	if (cp_tok_equal(optok, "*"))
		return arith(ev, l.val * r.val, l, r);
	if (cp_tok_equal(optok, "/") || cp_tok_equal(optok, "%")) {
		is_div = cp_tok_equal(optok, "/");

		if (!r.val) {
			if (!skip)
				eval_error(ev, optok, "division by zero in a constant expression");
			return arith(ev, 0, l, r);
		}
		if (uns)
			return arith(ev, is_div ? l.val / r.val : l.val % r.val, l, r);
		if (sr == -1)		/* INT64_MIN / -1 would trap */
			return arith(ev, is_div ? -l.val : 0, l, r);
		return arith(ev, is_div ? (uint64_t)(sl / sr) : (uint64_t)(sl % sr), l, r);
	}
	if (cp_tok_equal(optok, "+"))
		return arith(ev, l.val + r.val, l, r);
	if (cp_tok_equal(optok, "-"))
		return arith(ev, l.val - r.val, l, r);
	if (cp_tok_equal(optok, "<<") || cp_tok_equal(optok, ">>")) {
		left = cp_tok_equal(optok, "<<");
		neg = !r.is_unsigned && sr < 0;
		n = neg ? -(uint64_t)sr : r.val;

		/* a negative count shifts the other way; the result has the left type */
		if (neg)
			left = !left;
		if (n >= 64)
			return mk(ev, !left && !l.is_unsigned && sl < 0 ? UINT64_MAX : 0,
				  l.is_unsigned, l.is_long);
		if (left)
			return mk(ev, l.val << n, l.is_unsigned, l.is_long);
		if (l.is_unsigned)
			return mk(ev, l.val >> n, true, l.is_long);
		return mk(ev, (uint64_t)(sl >> n), false, l.is_long);
	}
	if (cp_tok_equal(optok, "<"))
		return mk_int(ev, uns ? l.val < r.val : sl < sr);
	if (cp_tok_equal(optok, ">"))
		return mk_int(ev, uns ? l.val > r.val : sl > sr);
	if (cp_tok_equal(optok, "<="))
		return mk_int(ev, uns ? l.val <= r.val : sl <= sr);
	if (cp_tok_equal(optok, ">="))
		return mk_int(ev, uns ? l.val >= r.val : sl >= sr);
	if (cp_tok_equal(optok, "=="))
		return mk_int(ev, l.val == r.val);
	if (cp_tok_equal(optok, "!="))
		return mk_int(ev, l.val != r.val);
	if (cp_tok_equal(optok, "&"))
		return arith(ev, l.val & r.val, l, r);
	if (cp_tok_equal(optok, "^"))
		return arith(ev, l.val ^ r.val, l, r);
	if (cp_tok_equal(optok, "|"))
		return arith(ev, l.val | r.val, l, r);
	return mk_int(ev, 0);
}

/* precedence climbing; `skip` suppresses errors (division by zero) in dead operands */
static struct cp_value eval_binary(struct cp_eval *ev, struct cp_token **rest,
				   struct cp_token *tok, int min_prec, bool skip)
{
	struct cp_value l, r;
	struct cp_token *optok;
	int prec;
	bool is_or;
	bool is_and;
	bool dead2;

	l = eval_unary(ev, &tok, tok, skip);
	while (!ev->failed && (prec = binary_prec(tok)) >= min_prec && prec) {
		optok = tok;
		is_or = cp_tok_equal(optok, "||");
		is_and = cp_tok_equal(optok, "&&");
		dead2 = skip || (is_or && l.val) || (is_and && !l.val);

		r = eval_binary(ev, &tok, optok->next, prec + 1, dead2);
		if (is_or)
			l = mk_int(ev, l.val || r.val);
		else if (is_and)
			l = mk_int(ev, l.val && r.val);
		else
			l = apply_binary(ev, optok, l, r, skip);
	}
	*rest = tok;
	return l;
}

static struct cp_value eval_cond(struct cp_eval *ev, struct cp_token **rest,
				 struct cp_token *tok, bool skip)
{
	struct cp_value c, a, b;

	c = eval_binary(ev, &tok, tok, 1, skip);
	if (!ev->failed && cp_tok_equal(tok, "?")) {
		a = eval_cond(ev, &tok, tok->next, skip || !c.val);
		if (ev->failed)
			return mk_int(ev, 0);
		if (!cp_tok_equal(tok, ":")) {
			eval_error(ev, tok, "expected ':'");
			return mk_int(ev, 0);
		}
		b = eval_cond(ev, &tok, tok->next, skip || c.val);
		/* both arms are converted to their common type */
		c = arith(ev, c.val ? a.val : b.val, a, b);
	}
	/* the comma operator (a separator in declarations) */
	while (!ev->failed && !ev->no_comma && cp_tok_equal(tok, ","))
		c = eval_cond(ev, &tok, tok->next, skip);
	*rest = tok;
	return c;
}

int cp_eval_int_expr(struct cp_tu *tu, struct cp_token *tok,
		     struct cp_token **rest, struct cp_value *out,
		     const struct cp_expr_hooks *hooks)
{
	struct cp_eval ev = { .tu = tu, .hooks = hooks, .no_comma = hooks != NULL,
			      .wide = hooks == NULL };
	struct cp_value v;

	if (tok->kind == CPT_EOF) {
		eval_error(&ev, tok, "expected an expression");
		return -1;
	}

	v = eval_cond(&ev, &tok, tok, false);
	if (ev.failed)
		return -1;
	if (out)
		*out = v;
	if (rest)
		*rest = tok;
	return 0;
}
