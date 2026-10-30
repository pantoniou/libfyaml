/*
 * cparse-tokenize.c - C declaration front end: tokenizer
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Derived from slimcc's tokenize.c, which is derived from chibicc:
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

#define Ucast(c)	((unsigned int)(unsigned char)(c))
#define Inrange(c, x, y) ((Ucast(c) - Ucast(x)) <= (Ucast(y) - Ucast(x)))
#define Isdigit(c)	Inrange(c, '0', '9')
#define Isalnum(c)	(Inrange((c) | 0x20, 'a', 'z') || Isdigit(c))
#define Isxdigit(c)	(Isdigit(c) || Inrange((c) | 0x20, 'a', 'f'))
#define Casecmp(c, a)	(((c) | 0x20) == (a))

#define startswith2(p, x, y)	((p)[0] == (x) && (p)[1] == (y))
#define startswith3(p, x, y, z)	(startswith2(p, x, y) && (p)[2] == (z))

/* decode one UTF-8 code point; never looks further than 4 bytes ahead */
static int lex_utf8_get(const char *p, int *widthp)
{
	return fy_utf8_get(p, strnlen(p, 4), widthp);
}

/* the tokenizer state for one file */
struct cp_lexer {
	struct cp_tu *tu;
	struct cp_file *file;
	const char *line_start;
	int line;
	bool at_bol;
	bool has_space;
	bool failed;
};

static struct cp_token *lex_new_token(struct cp_lexer *lx, enum cp_token_kind kind,
				      const char *start, const char *end)
{
	struct cp_token *tok;

	tok = cp_alloc(lx->tu, sizeof(*tok));
	if (!tok) {
		lx->failed = true;
		return NULL;
	}
	tok->kind = kind;
	tok->loc = start;
	tok->len = (int)(end - start);
	tok->file = lx->file;
	tok->line_no = lx->line;
	tok->at_bol = lx->at_bol;
	tok->has_space = lx->has_space;
	lx->at_bol = lx->has_space = false;
	return tok;
}

static void lex_error(struct cp_lexer *lx, const char *loc, const char *fmt, ...)
	FY_FORMAT(printf, 3, 4);

static void lex_error(struct cp_lexer *lx, const char *loc, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	cp_diag_vadd(lx->tu, CPDS_ERROR, lx->file, loc, fmt, ap);
	va_end(ap);
	lx->failed = true;
}

/*
 * Identifiers: ASCII letters, digits, '_' and '$', and any valid non-ASCII
 * code point (UTF-8 only; this accepts a superset of the C11 Annex D ranges).
 * Returns the length, 0 if there is no identifier here, -1 on bad UTF-8.
 */
static int lex_ident_len(const char *p)
{
	const char *start = p;
	int c, width;

	for (;;) {
		if (Isalnum(*p) || *p == '_' || *p == '$') {
			p++;
			continue;
		}
		if ((unsigned char)*p >= 0x80) {
			c = lex_utf8_get(p, &width);
			if (c < 0)
				return -1;
			p += width;
			continue;
		}
		break;
	}
	return (int)(p - start);
}

static int from_hex(char c)
{
	if (Inrange(c, '0', '9'))
		return c - '0';
	if (Inrange(c, 'a', 'f'))
		return c - 'a' + 10;
	return c - 'A' + 10;
}

/* Read a punctuator token from p and returns its length. */
static int read_punct(const char *p)
{
	bool is_repeat = p[1] == *p;
	bool is_assign = p[1] == '=';

	switch (*p) {
	case '-':
		if (p[1] == '>')
			return 2;
		/* fall through */
	case '&':
	case '+':
	case '=':
	case '|':
		return (is_repeat | is_assign) + 1;
	case '<':
	case '>':
		if (is_repeat)
			return (p[2] == '=') + 2;
		/* fall through */
	case '!':
	case '%':
	case '*':
	case '/':
	case '^':
		return is_assign + 1;
	case '#':
		return is_repeat + 1;
	case '.':
		return (is_repeat && p[2] == *p) ? 3 : 1;
	case ':':
		return is_repeat + 1;
	case '(':
	case ')':
	case ',':
	case ';':
	case '?':
	case '@':
	case '[':
	case ']':
	case '`':
	case '{':
	case '}':
	case '~':
		return 1;
	}
	return 0;
}

static bool read_ucn(uint32_t *val, const char **new_pos, const char *p)
{
	int len = (*p++ == 'u') ? 4 : 8, i;
	uint32_t c = 0;

	for (i = 0; i < len; i++) {
		if (!Isxdigit(p[i]))
			return false;
		c = (c << 4) | from_hex(p[i]);
	}
	*val = c;
	*new_pos = p + len;
	return true;
}

static uint32_t read_escape_seq(struct cp_lexer *lx, const char **new_pos, const char *p)
{
	uint32_t c;

	if (Inrange(*p, '0', '7')) {
		/* octal */
		c = *p++ - '0';
		if (Inrange(*p, '0', '7')) {
			c = (c << 3) + (*p++ - '0');
			if (Inrange(*p, '0', '7'))
				c = (c << 3) + (*p++ - '0');
		}
		*new_pos = p;
		return c;
	}

	if (*p == 'x') {
		p++;
		if (!Isxdigit(*p)) {
			lex_error(lx, p, "invalid hex escape sequence");
			*new_pos = p;
			return 0;
		}
		c = 0;
		for (; Isxdigit(*p); p++)
			c = (c << 4) + from_hex(*p);
		*new_pos = p;
		return c;
	}

	if (Casecmp(*p, 'u')) {
		if (!read_ucn(&c, new_pos, p)) {
			lex_error(lx, p, "incomplete universal character name");
			*new_pos = p + 1;
			return 0;
		}
		return c;
	}

	*new_pos = p + 1;
	switch (*p) {
	case 'a': return '\a';
	case 'b': return '\b';
	case 't': return '\t';
	case 'n': return '\n';
	case 'v': return '\v';
	case 'f': return '\f';
	case 'r': return '\r';
	case 'e': return 27;	/* GNU */
	default: return (unsigned char)*p;
	}
}

/*
 * String literals are kept as raw text; the declaration front end only needs
 * to know where they end (include paths and attribute arguments are read from
 * the raw text).
 */
static struct cp_token *lex_string_literal(struct cp_lexer *lx, const char *start,
					   const char *quote)
{
	const char *p;

	for (p = quote + 1; *p != '"'; p++) {
		if (*p == '\n' || *p == '\0') {
			lex_error(lx, start, "unclosed string literal");
			return NULL;
		}
		if (*p == '\\')
			p++;
	}
	return lex_new_token(lx, CPT_STR, start, p + 1);
}

/*
 * Character literals have a value because they appear in constant
 * expressions. 'quote' points at the opening quote, 'start' at the prefix.
 */
static struct cp_token *lex_char_literal(struct cp_lexer *lx, const char *start,
					 const char *quote, bool prefixed)
{
	const char *p = quote + 1;
	struct cp_token *tok;
	uint32_t val = 0;
	bool is_multi = false;
	int c, width;

	if (*p == '\'') {
		lex_error(lx, p, "empty character literal");
		return NULL;
	}

	for (;;) {
		if (*p == '\0' || *p == '\n') {
			lex_error(lx, start, "unclosed character literal");
			return NULL;
		}

		if (*p == '\\') {
			c = read_escape_seq(lx, &p, p + 1);
			if (lx->failed)
				return NULL;
		} else {
			c = lex_utf8_get(p, &width);
			if (c < 0) {
				lex_error(lx, p, "invalid UTF-8 in character literal");
				return NULL;
			}
			p += width;
		}

		if (prefixed)
			val = c;
		else
			val = (val << 8) | (uint8_t)c;

		if (*p == '\'')
			break;
		is_multi = true;
		if (prefixed) {
			lex_error(lx, p, "invalid unicode character literal");
			return NULL;
		}
	}

	if (!prefixed && !is_multi && !lx->tu->target->char_unsigned)
		val = (int8_t)val;

	tok = lex_new_token(lx, CPT_CHAR, start, p + 1);
	if (tok)
		tok->ival = (int32_t)val;
	return tok;
}

/*
 * A numeric literal is tokenized as a "pp-number" first. The conversion to a
 * value happens where the value is needed (constant expressions).
 */
static struct cp_token *lex_pp_number(struct cp_lexer *lx, const char *start,
				      const char *p)
{
	int c, width;

	for (;;) {
		if (*p == '.') {
			p++;
			continue;
		}
		if (*p == '\'' && Isalnum(p[1]) && lx->tu->opts.std_c23) {
			p += 2;
			continue;
		}
		if ((*p == '+' || *p == '-') && (Casecmp(p[-1], 'e') || Casecmp(p[-1], 'p'))) {
			p++;
			continue;
		}
		if (Isalnum(*p) || *p == '_' || *p == '$') {
			p++;
			continue;
		}
		if ((unsigned char)*p >= 0x80) {
			c = lex_utf8_get(p, &width);
			if (c >= 0) {
				p += width;
				continue;
			}
		}
		break;
	}
	return lex_new_token(lx, CPT_NUM, start, p);
}

static void lex_newline(struct cp_lexer *lx, const char *p)
{
	/* p points at the newline */
	lx->line++;
	lx->line_start = p + 1;
}

static void lex_comment(struct cp_lexer *lx, const char *start, const char *end,
			enum cp_comment_kind kind, int line, int column, bool after_code)
{
	struct cp_comment cmt;

	memset(&cmt, 0, sizeof(cmt));
	cmt.file = lx->file;
	cmt.start = (size_t)(start - lx->file->contents);
	cmt.end = (size_t)(end - lx->file->contents);
	cmt.line = line;
	cmt.end_line = lx->line;
	cmt.column = column;
	cmt.kind = kind;
	cmt.after_code = after_code;
	if (cp_tu_comment_add(lx->tu, &cmt))
		lx->failed = true;
}

struct cp_token *cp_tokenize(struct cp_tu *tu, struct cp_file *file)
{
	struct cp_lexer lx;
	struct cp_token head, *cur = &head, *tok;
	const char *p = file->contents, *q;
	const char *p2;
	int punct_len, len, cline, ccol;
	bool after_code;
	char ch;

	memset(&lx, 0, sizeof(lx));
	memset(&head, 0, sizeof(head));
	lx.tu = tu;
	lx.file = file;
	lx.line_start = p;
	lx.line = 1;
	lx.at_bol = true;

	while (*p && !lx.failed) {
		if (*p == '\n') {
			lex_newline(&lx, p);
			p++;
			lx.at_bol = true;
			lx.has_space = false;
			continue;
		}

		if (*p == ' ' || *p == '\t' || *p == '\v' || *p == '\f') {
			ch = *p;
			while (*(++p) == ch)
				;
			lx.has_space = true;
			continue;
		}

		/* line comments */
		if (startswith2(p, '/', '/')) {
			q = p + 2;
			while (*q != '\n')
				q++;
			/* a token precedes it on this line */
			lex_comment(&lx, p, q, CPCK_LINE, lx.line,
				    (int)(p - lx.line_start) + 1, !lx.at_bol);
			p = q;
			lx.has_space = true;
			continue;
		}

		/* block comments */
		if (startswith2(p, '/', '*')) {
			cline = lx.line;
			ccol = (int)(p - lx.line_start) + 1;
			after_code = !lx.at_bol;
			for (q = p + 2; *q; q++) {
				if (startswith2(q, '*', '/'))
					break;
				if (*q == '\n')
					lex_newline(&lx, q);
			}
			if (!*q) {
				lex_error(&lx, p, "unclosed block comment");
				break;
			}
			lex_comment(&lx, p, q + 2, CPCK_BLOCK, cline, ccol, after_code);
			p = q + 2;
			lx.has_space = true;
			continue;
		}

		/* numeric literal */
		p2 = (*p == '.') ? p + 1 : p;
		if (Isdigit(*p2)) {
			tok = lex_pp_number(&lx, p, p2 + 1);
			if (!tok)
				break;
			cur = cur->next = tok;
			p += tok->len;
			continue;
		}

		/* punctuators */
		punct_len = read_punct(p);
		if (punct_len) {
			tok = lex_new_token(&lx, CPT_PUNCT, p, p + punct_len);
			if (!tok)
				break;
			cur = cur->next = tok;
			p += tok->len;
			continue;
		}

		/* string and character literals, with their encoding prefixes */
		tok = NULL;
		q = NULL;
		if (*p == '"')
			q = p;
		else if (startswith3(p, 'u', '8', '"'))
			q = p + 2;
		else if ((*p == 'u' || *p == 'U' || *p == 'L') && p[1] == '"')
			q = p + 1;
		if (q) {
			tok = lex_string_literal(&lx, p, q);
			if (!tok)
				break;
			cur = cur->next = tok;
			p += tok->len;
			continue;
		}

		if (*p == '\'')
			q = p;
		else if (startswith3(p, 'u', '8', '\'') && tu->opts.std_c23)
			q = p + 2;
		else if ((*p == 'u' || *p == 'U' || *p == 'L') && p[1] == '\'')
			q = p + 1;
		if (q) {
			tok = lex_char_literal(&lx, p, q, q != p);
			if (!tok)
				break;
			cur = cur->next = tok;
			p += tok->len;
			continue;
		}

		/* identifier or keyword */
		len = lex_ident_len(p);
		if (len < 0) {
			lex_error(&lx, p, "invalid UTF-8 in identifier");
			break;
		}
		if (len) {
			tok = lex_new_token(&lx, CPT_IDENT, p, p + len);
			if (!tok)
				break;
			cur = cur->next = tok;
			p += tok->len;
			continue;
		}

		lex_error(&lx, p, "invalid token");
	}

	/* always terminate the list */
	tok = lex_new_token(&lx, CPT_EOF, p, p);
	if (tok) {
		tok->at_bol = true;
		cur->next = tok;
	}
	return head.next;
}

struct cp_token *cp_tokenize_text(struct cp_tu *tu, struct cp_token *origin,
				  const char *text, size_t len)
{
	struct cp_file *tmp;
	struct cp_token *list, *tok;
	size_t num_comments = tu->num_comments;
	size_t errors = tu->num_errors;
	char *buf;

	/* diagnostics keep the file pointer, so it must live as long as the TU */
	tmp = cp_alloc(tu, sizeof(*tmp));
	buf = cp_alloc(tu, len + 2);
	if (!tmp || !buf)
		return NULL;
	memcpy(buf, text, len);
	buf[len] = '\n';

	if (origin)
		origin = (struct cp_token *)cp_tok_root(origin);

	tmp->name = origin && origin->file ? origin->file->name : "<text>";
	tmp->contents = buf;
	tmp->size = len + 1;
	tmp->incl_idx = -1;

	list = cp_tokenize(tu, tmp);

	/* text that turns into a comment is not a token sequence */
	if (tu->num_comments != num_comments) {
		tu->num_comments = num_comments;
		cp_error_at(tu, origin ? origin->file : NULL, origin ? origin->loc : NULL,
			    "'%.*s' is not a valid token sequence", (int)len, text);
	}
	if (tu->num_errors != errors || !list)
		return NULL;

	for (tok = list; tok; tok = tok->next) {
		tok->file = NULL;
		tok->origin = origin;
		tok->at_bol = false;
		if (tok->kind == CPT_EOF)
			break;
	}
	return list;
}
