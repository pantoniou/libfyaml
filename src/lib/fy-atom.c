/*
 * fy-atom.c - YAML atom methods
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <alloca.h>

#include <libfyaml.h>

#include "fy-parse.h"

#define O_CPY(_src, _len) \
	do { \
		int _l = (_len); \
		if (o && _l) { \
			int _cl = _l; \
			if (_cl > (oe - o)) \
				_cl = oe - o; \
			memcpy(o, (_src), _cl); \
			o += _cl; \
		} \
		len += _l; \
	} while(0)

#ifndef NDEBUG

#define fy_atom_out_debug(_atom, _out, _fmt, ...) \
	do { \
		if (!(_out)) \
			fy_atom_debug(NULL, _fmt, ## __VA_ARGS__); \
	} while(0)

#else

#define fy_atom_out_debug(_atom, _out, _fmt, ...) \
	do { } while(0)

#endif

static int
fy_atom_format_internal_line(const struct fy_atom *atom,
		const char *s, const char *e,
		char **op, char *oe,
		enum fy_atom_style style,
		bool need_sep)
{
	size_t len;
	char *o = op ? *op : NULL;
	const char *t;
	int i, c, value, code_length, rlen, w;
	uint8_t code[4], *tt;
	char digitbuf[10];

	len = 0;
	switch (style) {

	case FYAS_LITERAL:
		O_CPY(s, e - s);
		break;

	case FYAS_PLAIN:
	case FYAS_FOLDED:
		rlen = e - s;
		if (need_sep && rlen > 0) {
			if (*s != ' ')
				O_CPY(" ", 1);
			need_sep = false;
		}
		if (rlen > 0)
			O_CPY(s, rlen);
		break;

	case FYAS_SINGLE_QUOTED:
		while (s < e) {
			/* find next single quote */
			t = memchr(s, '\'', e - s);
			rlen = (t ? t : e) - s;
			if (need_sep && rlen > 0) {
				if (*s != ' ')
					O_CPY(" ", 1);
				need_sep = false;
			}
			if (rlen > 0)
				O_CPY(s, rlen);

			/* end of string */
			if (!t)
				break;
			s = t;
			/* next character single quote too */
			if ((e - s) >= 2 && s[1] == '\'') {
				if (need_sep) {
					O_CPY(" ", 1);
					need_sep = false;
				}
				O_CPY(s, 1);
			}

			/* skip over this single quote char */
			s++;
		}
		break;

	case FYAS_DOUBLE_QUOTED:
		while (s < e) {
			fy_atom_out_debug(atom, o, ">%.*s<", (int)(e - s), s);
			/* find next escape */
			t = memchr(s, '\\', e - s);
			/* copy up to there (or end) */
			rlen = (t ? t : e) - s;
			if (need_sep && rlen > 0) {
				if (*s != ' ') {
					O_CPY(" ", 1);
					fy_atom_out_debug(atom, o, "DQ sep (1) rlen=%d (%td) c=%d '%c'", rlen, e - s, *s, *s);
				}
				need_sep = false;
			}
			if (rlen > 0) {
				fy_atom_out_debug(atom, o, "dq: '%.*s", rlen, s);
				O_CPY(s, rlen);
			}

			if (!t)
				break;
			s = t;
			c = *s++;
			/* get '\\' */
			if (s >= e)
				break;
			c = *s++;

			if (need_sep) {
				if (c != ' ') {
					O_CPY(" ", 1);
					fy_atom_out_debug(atom, o, "DQ sep (2)");
				}
				need_sep = false;
			}

			code_length = 0;
			switch (c) {
			case '0':
				O_CPY("\0", 1);
				break;
			case 'a':
				O_CPY("\a", 1);
				break;
			case 'b':
				O_CPY("\b", 1);
				break;
			case 't':
			case '\t':
				O_CPY("\t", 1);
				break;
			case 'n':
				O_CPY("\n", 1);
				break;
			case 'v':
				O_CPY("\v", 1);
				break;
			case 'f':
				O_CPY("\f", 1);
				break;
			case 'r':
				O_CPY("\r", 1);
				break;
			case 'e':
				O_CPY("\e", 1);
				break;
			case ' ':
				O_CPY(" ", 1);
				break;
			case '"':
				O_CPY("\"", 1);
				break;
			case '/':
				O_CPY("/", 1);
				break;
			case '\'':
				O_CPY("'", 1);
				break;
			case '\\':
				O_CPY("\\", 1);
				break;
			case 'N':	/* NEL 0x85 */
				O_CPY("\xc2\x85", 2);
				break;
			case '_':	/* 0xa0 */
				O_CPY("\xc2\xa0", 2);
				break;
			case 'L':	/* LS 0x2028 */
				O_CPY("\xe2\x80\xa8", 3);
				break;
			case 'P':	/* PS 0x2029 */
				O_CPY("\xe2\x80\xa9", 3);
				break;
			case 'x':
				code_length = 2;
				break;
			case 'u':
				code_length = 4;
				break;
			case 'U':
				code_length = 8;
				break;
			default:
				/* error */
				break;
			}

			if (!code_length)
				continue;

			/* not enough */
			if (code_length > (e - s))
				break;

			value = 0;
			for (i = 0; i < code_length; i++) {
				c = *s++;
				value <<= 4;
				if (c >= '0' && c <= '9')
					value |= c - '0';
				else if (c >= 'a' && c <= 'f')
					value |= 10 + c - 'a';
				else if (c >= 'A' && c <= 'F')
					value |= 10 + c - 'A';
				else {
					value = -1;
					break;
				}
			}

			tt = fy_utf8_put(code, sizeof(code), value);
			if (!tt)
				continue;
			O_CPY(code, tt - code);
		}
		break;

	case FYAS_URI:
		while (s < e) {
			/* find next escape */
			t = memchr(s, '%', e - s);
			rlen = (t ? t : e) - s;
			O_CPY(s, rlen);

			/* end of string */
			if (!t)
				break;
			s = t;

			code_length = sizeof(code);
			t = fy_uri_esc(s, e - s, code, &code_length);
			if (!t)
				break;

			/* output escaped utf8 */
			O_CPY(code, code_length);
			s = t;
		}
		break;

	case FYAS_DOUBLE_QUOTED_MANUAL:
		while ((c = fy_utf8_get(s, (e - s), &w)) != -1) {

			if (c != '"' && c != '\\' && fy_is_print(c)) {
				O_CPY(s, w);
				s += w;
				continue;
			}
			O_CPY("\\", 1);
			switch (c) {
			case '\\':
				O_CPY("\\", 1);
				break;
			case '"':
				O_CPY("\"", 1);
				break;
			case '\0':
				O_CPY("0", 1);
				break;
			case '\a':
				O_CPY("a", 1);
				break;
			case '\b':
				O_CPY("b", 1);
				break;
			case '\t':
				O_CPY("t", 1);
				break;
			case '\n':
				O_CPY("n", 1);
				break;
			case '\v':
				O_CPY("v", 1);
				break;
			case '\f':
				O_CPY("f", 1);
				break;
			case '\r':
				O_CPY("r", 1);
				break;
			case '\e':
				O_CPY("e", 1);
				break;
			case 0x85:
				O_CPY("N", 1);
				break;
			case 0xa0:
				O_CPY("_", 1);
				break;
			case 0x2028:
				O_CPY("L", 1);
				break;
			case 0x2029:
				O_CPY("P", 1);
				break;
			default:
				if (c <= 0xff)
					snprintf(digitbuf, sizeof(digitbuf), "x%02x", c & 0xff);
				else if (c <= 0xffff)
					snprintf(digitbuf, sizeof(digitbuf), "x%04x", c & 0xffff);
				else
					snprintf(digitbuf, sizeof(digitbuf), "U%08x", c & 0xffffffff);
				O_CPY(digitbuf, strlen(digitbuf));
				break;
			}
			s += w;
		}
		break;

	default:
		return -1;
	}

	if (op)
		*op = o;
	return len;
}

static int fy_atom_format_internal(const struct fy_atom *atom,
				   void *out, size_t *outszp)
{
	enum fy_atom_style style = atom->style;
	size_t len;
	const char *s, *e;
	char *o = NULL, *oe = NULL;
	size_t outsz;
	int chomp, fchomp, leading_line_ws, trailing_line_ws;
	bool is_first, is_last;
	bool is_empty_line, has_trailing_breaks, has_break;
	bool need_sep, last_need_sep, is_quoted, is_block;
	bool is_indented, next_is_indented;
	const char *lb, *lbe, *nnlb;	/* linebreak, after linebreak, next non linebreak */
	const char *fnws, *lnws; 	/* first non whitespace, last non whitespace */
	const char *nnlbnws;		/* next non linebreak, non whitespace */
	const char *tlb, *tlbe;
	const char *fnspc;		/* first non space */
	const char *fnwslb, *fnwslbs;	/* first non whitespace or linebreak, start */
	bool has_trailing_breaks_ws;

	s = fy_atom_data(atom);
	len = fy_atom_size(atom);
	e = s + len;

	if (out && *outszp <= 0)
		return 0;

	fy_atom_out_debug(atom, out, "atom_fmt='%s'",
				fy_utf8_format_text_a(s, len, fyue_singlequote));

	if (out) {
		outsz = *outszp;
		o = out;
		oe = out + outsz;
	}

	len = 0;

	if (style == FYAS_COMMENT) {
		while (s < e) {

			/* find line break */
			lb = fy_find_lb(s, e - s);
			if (!lb)
				lb = e;
			/* skip over line break */
			lbe = fy_skip_lb(lb, e - lb);
			if (!lbe)
				lbe = e;

			/* find non whitespace, linebreak */
			fnws = fy_find_non_ws(s, lb - s);
			if (fnws)
				O_CPY(fnws, lb - fnws);
			O_CPY(lb, lbe - lb);

			s = lbe;
		}
		return len;
	}

	is_quoted = style == FYAS_SINGLE_QUOTED || style == FYAS_DOUBLE_QUOTED;
	is_block = style == FYAS_LITERAL || style == FYAS_FOLDED;

	chomp = is_block ? atom->increment : 0;
	fchomp = 0;

	/* scan forward for chomp */
	if (!chomp && is_block) {
		fnwslb = fy_find_non_ws_lb(s, e - s);

		/* track back until start of line */
		fnwslbs = fnwslb;
		while (fnwslbs > s && fy_is_ws(fnwslbs[-1]))
			fnwslbs--;
		fchomp = fnwslb - fnwslbs;

		fy_atom_out_debug(atom, out, "detected fchomp=%d", fchomp);
	}

	last_need_sep = false;

	is_first = true;
	while (s < e) {

		/* find next lb (or end) */
		lb = fy_find_lb(s, e - s);

		if (!lb) {
			/* point line break at end of input */
			lb = e;
			lbe = e;
			nnlb = e;
		} else {
			/* find end of this linebreak */
			lbe = fy_skip_lb(lb, e - lb);
			if (!lbe)
				lbe = e;
		}

		/* find first non-ws */
		fnws = fy_find_non_ws(s, lb - s);
		if (fnws) {
			/* find last non-ws */
			lnws = fy_last_non_ws(fnws, lb - fnws);
			assert(lnws);	/* must find the fnws */
		} else {
			fnws = lb;
			lnws = lb;
		}

		/* how many leading whitespaces? */
		leading_line_ws = fnws - s;

		fnspc = fy_find_non_space(s, fnws - s);
		if (!fnspc)
			fnspc = fnws;

		/* how many trailing whitespaces? */
		trailing_line_ws = lb - lnws;

		/* is this line nothing but whitespace? */
		is_empty_line = fnws == lb;

		/* find next non-lb */
		nnlb = fy_find_non_ws_lb(lbe, e - lbe);
		if (!nnlb)
			nnlb = e;

		/* the run from lbe to nnlb contains only space and linebreaks */
		/* the run from nnlb to nnlbnws contains only space */
		if (lbe < e) {
			/* find next non linebreak, non whitespace */
			nnlbnws = fy_find_non_ws_lb(lbe, e - lbe);
			if (!nnlbnws)
				nnlbnws = e;
			/* track back until start of line */
			nnlb = nnlbnws;
			while (nnlb > lbe && fy_is_ws(nnlb[-1]))
				nnlb--;
		} else {
			nnlbnws = e;
			nnlb = e;
		}

		/* is this the last run? */
		is_last = nnlbnws == e;

		/* is there any break? */
		has_break = lb < e;

		/* do we have more than one trailing break? */
		has_trailing_breaks = lbe < e && fy_find_lb(lbe, nnlb - lbe);

		/* we need a seperator is this is a non empty line
		 * and there are no more than one breaks */
		need_sep = ((!is_empty_line && !has_trailing_breaks) ||
				(is_empty_line && has_break && !has_trailing_breaks));

		/* chomping for block styles */
		if (is_block && !is_empty_line && !chomp) {
			chomp = leading_line_ws;
			fy_atom_out_debug(atom, out, "setting chomp to %d", chomp);
		}
		/* is this indented? */
		is_indented = is_block && leading_line_ws > chomp;

		/* is the next run indented in? */
		next_is_indented = is_block && nnlbnws > nnlb && (nnlbnws - nnlb) > chomp;

		has_trailing_breaks_ws = false;
		if (!is_last && style == FYAS_FOLDED && has_trailing_breaks) {
			tlbe = lbe;
			while (tlbe < nnlb && (tlb = fy_find_lb(tlbe, nnlb - tlbe)) != NULL) {
				if (chomp && (tlb - tlbe) > chomp) {
					has_trailing_breaks_ws = true;
					break;
				}
				tlbe = fy_skip_lb(tlb, nnlb - tlb);
			}
		}

		fy_atom_out_debug(atom, out, "s->lb: '%s'\n",
			fy_utf8_format_text_a(s, lb - s, fyue_singlequote));
		fy_atom_out_debug(atom, out, "s->fnws: '%s'\n",
			fy_utf8_format_text_a(s, fnws - s, fyue_singlequote));
		fy_atom_out_debug(atom, out, "fnws->lnws: '%s'\n",
			fy_utf8_format_text_a(fnws, lnws - fnws, fyue_singlequote));
		fy_atom_out_debug(atom, out, "lb->lbe: '%s'\n",
			fy_utf8_format_text_a(lb, lbe - lb, fyue_singlequote));
		fy_atom_out_debug(atom, out, "lbe->nnlb: '%s'\n",
			fy_utf8_format_text_a(lbe, nnlb - lbe, fyue_singlequote));

		fy_atom_out_debug(atom, out, "is_first=%s is_last=%s is_empty_line=%s has_break=%s has_trailing_breaks=%s leading_line_ws=%d trailing_line_ws=%d",
				is_first ? "true" : "false",
				is_last ? "true" : "false",
				is_empty_line ? "true" : "false",
				has_break ? "true" : "false",
				has_trailing_breaks ? "true" : "false",
				leading_line_ws,
				trailing_line_ws);
		fy_atom_out_debug(atom, out, "need_sep=%s chomp=%d",
				need_sep ? "true" : "false",
				chomp);

		/* nothing but spaces */
		if (is_quoted && is_first && is_last && is_empty_line && !has_break) {
			fy_atom_out_debug(atom, out, "quoted-only-whitespace: '%.*s'",
						(int)(fnws - s), s);
			O_CPY(s, fnws - s);
			break;
		}

		/* quoted styles need the leading whitespace preserved */
		if (is_first && !is_empty_line && is_quoted) {
			fy_atom_out_debug(atom, out, "quoted-prefix-whitespace: '%.*s'",
						(int)(fnws - s), s);
			O_CPY(s, fnws - s);
		}

		/* literal style, output whitespaces after the chomp point */
		if (style == FYAS_LITERAL && is_indented && chomp) {
			fy_atom_out_debug(atom, out, "literal-prefix-whitespace: '%.*s'",
						(int)(fnws - s - chomp), s + chomp);
			O_CPY(s + chomp, fnws - s - chomp);
		}

		/* literal style, output whitespaces after the chomp point */
		if (style == FYAS_FOLDED && is_indented && !is_empty_line && chomp) {
			fy_atom_out_debug(atom, out, "folded-prefix-whitespace: '%.*s'",
						(int)(fnws - s - chomp), s + chomp);
			O_CPY(s + chomp, fnws - s - chomp);
			last_need_sep = false;
		}

		/* block style, before setting of chomp */
		if (style == FYAS_FOLDED && !chomp && fchomp && fnws > fnspc) {
			fy_atom_out_debug(atom, out, "folded-prefix-whitespace special: '%.*s'",
						(int)(fnws - s - fchomp), s + fchomp);
			O_CPY(s + fchomp, fnws - s - fchomp);
			last_need_sep = false;
		}

		/* output the non-ws chunk */
		if (!is_empty_line) {

			fy_atom_out_debug(atom, out, "OUT: %s'%.*s'\n",
					last_need_sep ? "SEP " : "",
					(int)(lnws - fnws), fnws);

			len += fy_atom_format_internal_line(atom, fnws, lnws,
					o ? &o : NULL, o ? oe : NULL,
					style, last_need_sep);

			/* literal style, output the whitespace until lb */
			if (lnws < lb && (style == FYAS_LITERAL ||
					  (style == FYAS_FOLDED &&
					  	(is_indented || next_is_indented || has_trailing_breaks)))) {
				fy_atom_out_debug(atom, out, "trailing-block-whitespace: '%.*s'",
							(int)(lb - lnws), lnws);
				O_CPY(lnws, lb - lnws);
			}

			/* quoted style, with trailing backslash just before lb, turn off seperator */
			if (style == FYAS_DOUBLE_QUOTED && lnws > fnws && lnws[-1] == '\\' && !trailing_line_ws)
				need_sep = false;
		}

		/* last run, quoted style with trailing white space (without extra linebreaks) */
		if (is_last && is_quoted && !is_empty_line && trailing_line_ws && !has_break) {
			fy_atom_out_debug(atom, out, "quoted-trailing-whitespace: '%.*s'",
						(int)(lb - lnws), lnws);
			O_CPY(lnws, lb - lnws);
			break;
		}

		/* last run, non-block style with a break, but without trailing linebreaks */
		if (is_last && !is_block && has_break && !has_trailing_breaks) {
			fy_atom_out_debug(atom, out, "last-trailing-sep");
			O_CPY(" ", 1);
			break;
		}

		/* last run, not a block style, output trailing line breaks */
		if (is_last && !is_block && has_trailing_breaks) {
			fy_atom_out_debug(atom, out, "last-trailing-breaks");
			/* if we have trailing linebreaks spit them out */
			tlbe = lbe;
			while (tlbe < nnlb && (tlb = fy_find_lb(tlbe, nnlb - tlbe)) != NULL) {
				tlbe = fy_skip_lb(tlb, nnlb - tlb);
				O_CPY(tlb, tlbe - tlb);
			}
			break;
		}

		/* last run, block style, strip, immediate break */
		if (is_last && is_block && atom->chomp == FYAC_STRIP) {
			fy_atom_out_debug(atom, out, "last-block-strip");
			break;
		}

		/* last run, block style, clip to single linebreak */
		if (is_last && is_block && atom->chomp == FYAC_CLIP) {
			fy_atom_out_debug(atom, out, "last-block-clip");
			if (!is_empty_line)
				O_CPY(lb, lbe - lb);
			break;
		}

		/* last run, block style, keep linebreaks */
		if (is_last && is_block && atom->chomp == FYAC_KEEP) {

			fy_atom_out_debug(atom, out, "last-block-keep");

			/* always output this linebreak */
			O_CPY(lb, lbe - lb);

			/* if we have trailing linebreaks spit them out */
			tlbe = lbe;
			while (tlbe < nnlb && (tlb = fy_find_lb(tlbe, nnlb - tlbe)) != NULL) {

				/* output white space for literal style */
				if (style == FYAS_LITERAL && (tlb - tlbe) > chomp && (!is_first || !is_empty_line)) {
					O_CPY(tlbe + chomp, tlb - tlbe - chomp);
					fy_atom_out_debug(atom, out, "last-block-keep ws: '%s'\n",
						fy_utf8_format_text_a(tlbe + chomp, tlb - tlbe - chomp, fyue_singlequote));
				}

				tlbe = fy_skip_lb(tlb, nnlb - tlb);
				O_CPY(tlb, tlbe - tlb);
			}
			break;
		}

		/* always output the literal linebreak */
		if (!is_last && style == FYAS_LITERAL && has_break) {
			fy_atom_out_debug(atom, out, "literal-lb");
			O_CPY(lb, lbe - lb);
		}

		/* output the folded linebreak only when this, or the next line change indentation */
		if (!is_last && style == FYAS_FOLDED && (is_indented || next_is_indented || has_trailing_breaks_ws)) {

			fy_atom_out_debug(atom, out, "folded-lb");

			O_CPY(lb, lbe - lb);
			need_sep = false;
		}

		/* not last run, with trailing breaks */
		if (!is_last && has_trailing_breaks) {

			fy_atom_out_debug(atom, out, "trailing-breaks");

			tlbe = lbe;
			while (tlbe < nnlb && (tlb = fy_find_lb(tlbe, nnlb - tlbe)) != NULL) {
				/* output white space for literal style */
				if (is_block && chomp && (tlb - tlbe) > chomp)
					O_CPY(tlbe + chomp, tlb - tlbe - chomp);

				/* output the linebreak */
				tlbe = fy_skip_lb(tlb, nnlb - tlb);
				O_CPY(tlb, tlbe - tlb);
			}

			need_sep = false;
		}

		/* save next seperator state */
		last_need_sep = need_sep;

		/* no longer first */
		is_first = false;

		/* and skip all over the linebreaks */
		s = nnlb;
	}

	return len;
}

int fy_atom_format_text_length(const struct fy_atom *atom)
{
	return fy_atom_format_internal(atom, NULL, NULL);
}

int fy_atom_format_text_length_hint(const struct fy_atom *atom)
{
	if (atom->storage_hint)
		return atom->storage_hint;

	return fy_atom_format_text_length(atom);
}

const char *fy_atom_format_text(const struct fy_atom *atom, char *buf, size_t maxsz)
{
	if (!buf)
		return NULL;

	fy_atom_format_internal(atom, buf, &maxsz);
	return buf;
}

void fy_fill_atom_start(struct fy_parser *fyp, struct fy_atom *handle)
{
	memset(handle, 0, sizeof(*handle));

	/* start mark */
	fy_get_mark(fyp, &handle->start_mark);
	handle->end_mark = handle->start_mark;
	handle->fyi = fyp->current_input;

	assert(fyp->current_input);
	/* note that handle->data may be zero for empty input */
}

void fy_fill_atom_end_at(struct fy_parser *fyp, struct fy_atom *handle,
			 struct fy_mark *end_mark)
{
	assert(!fyp->current_input || handle->fyi == fyp->current_input);

	if (end_mark)
		handle->end_mark = *end_mark;
	else
		fy_get_mark(fyp, &handle->end_mark);

	/* default is plain, modify at return */
	handle->style = FYAS_PLAIN;
	handle->chomp = FYAC_CLIP;
	/* by default we don't do storage hints, it's the job of the caller */
	handle->storage_hint = 0;
}

void fy_fill_atom_end(struct fy_parser *fyp, struct fy_atom *handle)
{
	fy_fill_atom_end_at(fyp, handle, NULL);
}

struct fy_atom *fy_fill_atom(struct fy_parser *fyp, int advance, struct fy_atom *handle)
{
	/* start mark */
	fy_fill_atom_start(fyp, handle);

	/* advance the given number of characters */
	if (advance > 0)
		fy_advance_by(fyp, advance);

	fy_fill_atom_end(fyp, handle);

	return handle;
}
