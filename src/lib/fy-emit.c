/*
 * fy-emit.c - Internal YAML emitter methods
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
#include <assert.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <ctype.h>

#include <libfyaml.h>

#include "fy-parse.h"
#include "fy-emit.h"

#define DDNF_ROOT		0x0001
#define DDNF_SEQ		0x0002
#define DDNF_MAP		0x0004
#define DDNF_SIMPLE		0x0008
#define DDNF_FLOW		0x0010
#define DDNF_INDENTLESS		0x0020
#define DDNF_SIMPLE_SCALAR_KEY	0x0040

static inline bool fy_emit_is_json_mode(const struct fy_emitter *emit)
{
	enum fy_emitter_cfg_flags flags = emit->cfg->flags & FYECF_MODE(FYECF_MODE_MASK);

	return flags == FYECF_MODE_JSON || flags == FYECF_MODE_JSON_TP || flags == FYECF_MODE_JSON_ONELINE;
}

static inline bool fy_emit_is_flow_mode(const struct fy_emitter *emit)
{
	enum fy_emitter_cfg_flags flags = emit->cfg->flags & FYECF_MODE(FYECF_MODE_MASK);

	return flags == FYECF_MODE_FLOW || flags == FYECF_MODE_FLOW_ONELINE;
}

static inline bool fy_emit_is_block_mode(const struct fy_emitter *emit)
{
	enum fy_emitter_cfg_flags flags = emit->cfg->flags & FYECF_MODE(FYECF_MODE_MASK);

	return flags == FYECF_MODE_BLOCK;
}

static inline bool fy_emit_is_oneline(const struct fy_emitter *emit)
{
	enum fy_emitter_cfg_flags flags = emit->cfg->flags & FYECF_MODE(FYECF_MODE_MASK);

	return flags == FYECF_MODE_FLOW_ONELINE || flags == FYECF_MODE_JSON_ONELINE;
}

static inline int fy_emit_indent(struct fy_emitter *emit)
{
	int indent;

	indent = (emit->cfg->flags & FYECF_INDENT(FYECF_INDENT_MASK)) >> FYECF_INDENT_SHIFT;
	return indent ? indent : 2;
}

static inline int fy_emit_width(struct fy_emitter *emit)
{
	int width;

	width = (emit->cfg->flags & FYECF_WIDTH(FYECF_WIDTH_MASK)) >> FYECF_WIDTH_SHIFT;
	if (width == 0)
		return 80;
	if (width == FYECF_WIDTH_MASK)
		return INT_MAX;
	return width;
}

static inline bool fy_emit_output_comments(struct fy_emitter *emit)
{
	return !!(emit->cfg->flags & FYECF_OUTPUT_COMMENTS);
}

void fy_emit_node_internal(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent);
void fy_emit_scalar(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent);
void fy_emit_sequence(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent);
void fy_emit_mapping(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent);

void fy_emit_write(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *str, int len)
{
	int c, w;
	const char *m, *e;
	int outlen;

	if (!len)
		return;

	outlen = emit->cfg->output(emit, type, str, len, emit->cfg->userdata);
	if (outlen != len)
		emit->output_error = true;

	e = str + len;
	while ((c = fy_utf8_get(str, (e - str), &w)) != -1) {

		/* special handling for MSDOS */
		if (c == '\r' && (e - str) > 1 && str[1] == '\n') {
			str += 2;
			emit->column = 0;
			emit->line++;
			continue;
		}

		/* regular line break */
		if (fy_is_lb(c)) {
			emit->column = 0;
			emit->line++;
			str += w;
			continue;
		}

		/* completely ignore ANSI color escape sequences */
		if (c == '\x1b' && (e - str) > 2 && str[1] == '[' &&
		    (m = memchr(str, 'm', e - str)) != NULL) {
			str = m + 1;
			continue;
		}

		emit->column++;
		str += w;
	}
}

void fy_emit_puts(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *str)
{
	fy_emit_write(emit, type, str, strlen(str));
}

void fy_emit_putc(struct fy_emitter *emit, enum fy_emitter_write_type type, int c)
{
	char buf[FY_UTF8_FORMAT_BUFMIN];

	fy_utf8_format(c, buf, fyue_none);
	fy_emit_puts(emit, type, buf);
}

void fy_emit_vprintf(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *fmt, va_list ap)
{
	char *str;
	int size;
	va_list ap2;

	va_copy(ap2, ap);

	size = vsnprintf(NULL, 0, fmt, ap);
	if (size < 0)
		return;

	str = alloca(size + 1);
	size = vsnprintf(str, size + 1, fmt, ap2);
	if (size < 0)
		return;

	fy_emit_write(emit, type, str, size);
}

void fy_emit_printf(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *fmt, ...)
		__attribute__((format(printf, 3, 4)));

void fy_emit_printf(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_emit_vprintf(emit, type, fmt, ap);
	va_end(ap);
}

void fy_emit_write_ws(struct fy_emitter *emit)
{
	fy_emit_putc(emit, fyewt_whitespace, ' ');
	emit->flags |= FYEF_WHITESPACE;
}

void fy_emit_write_indent(struct fy_emitter *emit, int indent)
{
	int len;
	char *ws;

	indent = indent > 0 ? indent : 0;

	if (!fy_emit_indentation(emit) || emit->column > indent ||
	    (emit->column == indent && !fy_emit_whitespace(emit)))
		fy_emit_putc(emit, fyewt_linebreak, '\n');

	if (emit->column < indent) {
		len = indent - emit->column;
		ws = alloca(len + 1);
		memset(ws, ' ', len);
		ws[len] = '\0';
		fy_emit_write(emit, fyewt_indent, ws, len);
	}

	emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;
}

enum document_indicator {
	di_question_mark,
	di_colon,
	di_dash,
	di_left_bracket,
	di_right_bracket,
	di_left_brace,
	di_right_brace,
	di_comma,
	di_bar,
	di_greater,
	di_single_quote_start,
	di_single_quote_end,
	di_double_quote_start,
	di_double_quote_end,
	di_ambersand,
	di_star,
};

void fy_emit_write_indicator(struct fy_emitter *emit,
		enum document_indicator indicator,
		int flags, int indent,
		enum fy_emitter_write_type wtype)
{
	switch (indicator) {

	case di_question_mark:
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
		fy_emit_putc(emit, wtype, '?');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_OPEN_ENDED);
		break;

	case di_colon:
		if (!(flags & DDNF_SIMPLE)) {
			if (!emit->flow_level && !fy_emit_is_oneline(emit))
				fy_emit_write_indent(emit, indent);
			if (!fy_emit_whitespace(emit))
				fy_emit_write_ws(emit);
		}
		fy_emit_putc(emit, wtype, ':');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_OPEN_ENDED);
		break;

	case di_dash:
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
		fy_emit_putc(emit, wtype, '-');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_OPEN_ENDED);
		break;

	case di_left_bracket:
	case di_left_brace:
		emit->flow_level++;
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
		fy_emit_putc(emit, wtype, indicator == di_left_bracket ? '[' : '{');
		emit->flags |= FYEF_WHITESPACE;
		emit->flags &= ~(FYEF_INDENTATION | FYEF_OPEN_ENDED);
		break;

	case di_right_bracket:
	case di_right_brace:
		emit->flow_level--;
		fy_emit_putc(emit, wtype, indicator == di_right_bracket ? ']' : '}');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION | FYEF_OPEN_ENDED);
		break;

	case di_comma:
		fy_emit_putc(emit, wtype, ',');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION | FYEF_OPEN_ENDED);
		break;

	case di_bar:
	case di_greater:
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
		fy_emit_putc(emit, wtype, indicator == di_bar ? '|' : '>');
		emit->flags &= ~(FYEF_INDENTATION | FYEF_WHITESPACE | FYEF_OPEN_ENDED);
		break;

	case di_single_quote_start:
	case di_double_quote_start:
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
		fy_emit_putc(emit, wtype, indicator == di_single_quote_start ? '\'' : '"');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION | FYEF_OPEN_ENDED);
		break;

	case di_single_quote_end:
	case di_double_quote_end:
		fy_emit_putc(emit, wtype, indicator == di_single_quote_end ? '\'' : '"');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION | FYEF_OPEN_ENDED);
		break;

	case di_ambersand:
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
		fy_emit_putc(emit, wtype, '&');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION);
		break;

	case di_star:
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
		fy_emit_putc(emit, wtype, '*');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION);
		break;
	}
}

int fy_emit_increase_indent(struct fy_emitter *emit, int flags, int indent)
{
	if (indent < 0)
		return (flags & DDNF_FLOW) ? fy_emit_indent(emit) : 0;

	if (!(flags & DDNF_INDENTLESS))
		return indent + fy_emit_indent(emit);

	return indent;
}

void fy_emit_write_comment(struct fy_emitter *emit, int flags, int indent, const char *str, size_t len)
{
	const char *s, *e, *sr;
	int c, w;
	bool breaks;

	if (!str || !len)
		return;

	if (len == (size_t)-1)
		len = strlen(str);

	if (!fy_emit_whitespace(emit))
		fy_emit_write_ws(emit);
	indent = emit->column;

	s = str;
	e = str + len;

	sr = s;	/* start of normal output run */
	breaks = false;
	while (s < e && (c = fy_utf8_get(s, e - s, &w)) > 0) {

		if (fy_is_break(c)) {

			/* output run */
			fy_emit_write(emit, fyewt_comment, sr, s - sr);
			sr = s + w;
			fy_emit_write_indent(emit, indent);
			emit->flags |= FYEF_INDENTATION;
			breaks = true;
		} else {

			if (breaks) {
				fy_emit_write(emit, fyewt_comment, sr, s - sr);
				sr = s;
				fy_emit_write_indent(emit, indent);
			}
			emit->flags &= ~FYEF_INDENTATION;
			breaks = false;
		}

		s += w;
	}

	/* dump what's remaining */
	fy_emit_write(emit, fyewt_comment, sr, s - sr);

	emit->flags |= (FYEF_WHITESPACE | FYEF_INDENTATION);
}

struct fy_atom *fy_emit_node_comment_handle(struct fy_emitter *emit, struct fy_node *fyn, enum fy_comment_placement placement)
{
	struct fy_token *fyt;
	struct fy_atom *handle;

	if (!emit || !fyn || !fy_emit_output_comments(emit) ||
			(unsigned int)placement >= fycp_max)
		return NULL;

	switch (fyn->type) {
	case FYNT_SCALAR:
		fyt = fyn->scalar;
		break;
	case FYNT_SEQUENCE:
		fyt = fyn->sequence_start;
		break;
	case FYNT_MAPPING:
		fyt = fyn->mapping_start;
		break;
	default:
		fyt = NULL;
		break;
	}

	if (!fyt)
		return NULL;

	handle = &fyt->comment[placement];
	return fy_atom_is_set(handle) ? handle : NULL;
}

bool fy_emit_node_has_comment(struct fy_emitter *emit, struct fy_node *fyn, enum fy_comment_placement placement)
{
	return fy_emit_node_comment_handle(emit, fyn, placement) ? true : false;
}

void fy_emit_node_comment(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent,
			  enum fy_comment_placement placement)
{
	struct fy_atom *handle;

	handle = fy_emit_node_comment_handle(emit, fyn, placement);
	if (!handle)
		return;

	if (placement == fycp_top || placement == fycp_bottom) {
		fy_emit_write_indent(emit, indent);
		emit->flags |= FYEF_WHITESPACE;
	}

	fy_emit_write_comment(emit, flags, indent, fy_atom_get_text_a(handle), -1);

	emit->flags &= ~FYEF_INDENTATION;

	if (placement == fycp_top || placement == fycp_bottom) {
		fy_emit_write_indent(emit, indent);
		emit->flags |= FYEF_WHITESPACE;
	}
}

void fy_emit_node_internal(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent)
{
	enum fy_node_type type;
	struct fy_anchor *fya = NULL;
	const char *anchor = NULL;
	const char *tag = NULL;
	const char *td_prefix __FY_DEBUG_UNUSED__;
	const char *td_handle;
	size_t td_prefix_size, td_handle_size;
	size_t tag_len = 0, anchor_len = 0;
	bool json_mode = false;

	if (!fyn)
		return;

	json_mode = fy_emit_is_json_mode(emit);

	if (!json_mode) {
		if (!(emit->cfg->flags & FYECF_STRIP_LABELS)) {
			fya = fy_document_lookup_anchor_by_node(emit->fyd, fyn);
			if (fya)
				anchor = fy_anchor_get_text(fya, &anchor_len);
		}

		if (!(emit->cfg->flags & FYECF_STRIP_TAGS)) {
			if (fyn->tag)
				tag = fy_token_get_text(fyn->tag, &tag_len);
		}

		if (anchor) {
			fy_emit_write_indicator(emit, di_ambersand, flags, indent, fyewt_anchor);
			fy_emit_write(emit, fyewt_anchor, anchor, anchor_len);
		}

		if (tag) {
			if (!fy_emit_whitespace(emit))
				fy_emit_write_ws(emit);

			td_handle = fy_tag_token_get_directive_handle(fyn->tag, &td_handle_size);
			assert(td_handle);
			td_prefix = fy_tag_token_get_directive_prefix(fyn->tag, &td_prefix_size);
			assert(td_prefix);

			if (!td_handle_size)
				fy_emit_printf(emit, fyewt_tag, "!<%.*s>", (int)tag_len, tag);
			else
				fy_emit_printf(emit, fyewt_tag, "%.*s%.*s",
						(int)td_handle_size, td_handle,
						(int)(tag_len - td_prefix_size), tag + td_prefix_size);

			emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION);
		}
	}

	/* content for root always starts on a new line */
	if ((flags & DDNF_ROOT) && emit->column != 0 &&
            !(emit->flags & FYEF_HAD_DOCUMENT_START)) {
		fy_emit_putc(emit, fyewt_linebreak, '\n');
		emit->flags = FYEF_WHITESPACE | FYEF_INDENTATION;
	}

	type = fyn ? fyn->type : FYNT_SCALAR;

	switch (type) {
	case FYNT_SCALAR:
		fy_emit_scalar(emit, fyn, flags, indent);
		break;
	case FYNT_SEQUENCE:
		fy_emit_sequence(emit, fyn, flags, indent);
		break;
	case FYNT_MAPPING:
		fy_emit_mapping(emit, fyn, flags, indent);
		break;
	}
}

void fy_emit_write_plain(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent)
{
	bool allow_breaks, should_indent, spaces, breaks;
	const char *s, *e, *sr, *nlb;
	int c, w, ww, srlen;
	enum fy_emitter_write_type wtype;
	const char *str = NULL;
	size_t len = 0;

	if (fyn && fyn->scalar)
		str = fy_token_get_text(fyn->scalar, &len);

	if (!str) {
		str = "";
		len = 0;
	}

	s = str;
	e = str + len;

	allow_breaks = !(flags & DDNF_SIMPLE) && !fy_emit_is_json_mode(emit) && !fy_emit_is_oneline(emit);

	wtype = (flags & DDNF_SIMPLE_SCALAR_KEY) ? fyewt_plain_scalar_key : fyewt_plain_scalar;

	/* if we don't allow breaks, we should not find any linebreaks */
	if (!allow_breaks) {

		nlb = fy_find_lb(s, e - s);
		if (nlb)
			e = nlb;

		sr = s;
		s = e;
		goto done;
	}

	spaces = false;
	breaks = false;

	sr = s;	/* start of normal output run */
	srlen = 0;
	while (s < e && (c = fy_utf8_get(s, e - s, &w)) > 0) {

		if (fy_is_ws(c)) {

			should_indent = !spaces && emit->column + srlen > fy_emit_width(emit);

			if (should_indent && !fy_is_ws(fy_utf8_get(s + w, e - (s + w), &ww))) {
				/* output run */
				fy_emit_write(emit, wtype, sr, s - sr);
				/* skip this whitespace */
				fy_emit_write_indent(emit, indent);
				sr = s + w;
				srlen = 0;
				continue;
			}
			spaces = true;

		} else if (fy_is_lb(c)) {

			/* output run */
			if (!breaks) {
				fy_emit_write(emit, wtype, sr, s - sr);
				fy_emit_write_indent(emit, indent);
			}

			fy_emit_putc(emit, fyewt_linebreak, '\n'); 
			fy_emit_write_indent(emit, indent);

			sr = s + w;
			srlen = 0;

			breaks = true;

		} else {

			if (breaks) {
				fy_emit_write(emit, wtype, sr, s - sr);
				sr = s;
				srlen = 0;
				fy_emit_write_indent(emit, indent);
			}
			srlen++;

			emit->flags &= ~FYEF_INDENTATION;
			spaces = false;
			breaks = false;
		}

		s += w;
	}

done:
	/* dump what's remaining */
	fy_emit_write(emit, wtype, sr, s - sr);

	emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION);
}

void fy_emit_write_alias(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent)
{
	const char *str = NULL;
	size_t len = 0;

	assert(fyn);
	assert(fyn->scalar);

	str = fy_token_get_text(fyn->scalar, &len);

	fy_emit_write_indicator(emit, di_star, flags, indent, fyewt_alias);
	fy_emit_write(emit, fyewt_alias, str, len);
}

void fy_emit_write_quoted(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent, char qc)
{
	bool allow_breaks, spaces, breaks;
	const char *s, *e, *sr, *nnws, *stws, *etws;
	int c, cn, w, ww, srlen;
	enum fy_emitter_write_type wtype;
	const char *str = NULL;
	size_t len = 0;

	if (fyn && fyn->scalar)
		str = fy_token_get_text(fyn->scalar, &len);

	if (!str) {
		str = "";
		len = 0;
	}

	s = str;
	e = str + len;

	wtype = qc == '\'' ?
		((flags & DDNF_SIMPLE_SCALAR_KEY) ?
		 	fyewt_single_quoted_scalar_key : fyewt_single_quoted_scalar) :
		((flags & DDNF_SIMPLE_SCALAR_KEY) ?
		 	fyewt_double_quoted_scalar_key : fyewt_double_quoted_scalar);

	fy_emit_write_indicator(emit,
			qc == '\'' ? di_single_quote_start : di_double_quote_start,
			flags, indent, wtype);

	allow_breaks = !(flags & DDNF_SIMPLE) && !fy_emit_is_json_mode(emit) && !fy_emit_is_oneline(emit);

	/* output any leading ws */
	nnws = fy_find_non_ws(s, e - s);
	if (!nnws)
		nnws = e;

	if (nnws > s) {
		fy_emit_write(emit, wtype, s, nnws - s);
		s = nnws;
	}

	/* find trailing whitespace */
	stws = fy_last_non_ws(s, e - s);
	etws = e;
	if (!stws)
		stws = e;
	else
		e = stws;

	/* start of normal output run */
	sr = s;
	srlen = 0;

	spaces = false;
	breaks = false;

	while (s < e && (c = fy_utf8_get(s, e - s, &w)) >= 0) {

		cn = fy_utf8_get(s + w, e - (s + w), &ww);

		if (allow_breaks && fy_is_ws(c)) {

			if (!spaces && emit->column + srlen > fy_emit_width(emit) &&
			    ((qc == '\'' && !fy_is_ws(cn)) || qc == '"')) {

				/* output run */
				fy_emit_write(emit, wtype, sr, s - sr);

				/* skip this whitespace */
				fy_emit_write_indent(emit, indent);
				if (qc == '"' && fy_is_ws(cn))
					fy_emit_putc(emit, wtype, '\\');
				sr = s + w;
				srlen = 0;

				continue;
			}

			spaces = true;

		} else if (qc == '\'' && fy_is_lb(c)) {

			if (!breaks) {
				fy_emit_write(emit, wtype, sr, s - sr);
				fy_emit_write_indent(emit, indent);
			}

			fy_emit_putc(emit, fyewt_linebreak, '\n'); 
			fy_emit_write_indent(emit, indent);

			sr = s + w;
			srlen = 0;

			breaks = true;

		} else {
			if (breaks) {
				fy_emit_write(emit, wtype, sr, s - sr);
				sr = s;
				srlen = 0;
				fy_emit_write_indent(emit, indent);
			}

			/* escape */
			if (qc == '\'' && c == '\'') {
				fy_emit_write(emit, wtype, sr, s - sr);
				sr = s;
				srlen = 0;
				fy_emit_putc(emit, wtype, '\'');
			} else if (qc == '"' &&
				   (!fy_is_print(c) || c == FY_UTF8_BOM ||
				    fy_is_break(c) || c == '"' || c == '\\')) {
				fy_emit_write(emit, wtype, sr, s - sr);
				sr = s + w;
				srlen = 0;

				fy_emit_putc(emit, wtype, '\\');
				switch (c) {
				case '\0':
					fy_emit_putc(emit, wtype, '0');
					break;
				case '\a':
					fy_emit_putc(emit, wtype, 'a');
					break;
				case '\b':
					fy_emit_putc(emit, wtype, 'b');
					break;
				case '\t':
					fy_emit_putc(emit, wtype, 't');
					break;
				case '\n':
					fy_emit_putc(emit, wtype, 'n');
					break;
				case '\v':
					fy_emit_putc(emit, wtype, 'v');
					break;
				case '\f':
					fy_emit_putc(emit, wtype, 'f');
					break;
				case '\r':
					fy_emit_putc(emit, wtype, 'r');
					break;
				case '\e':
					fy_emit_putc(emit, wtype, 'e');
					break;
				case '"':
					fy_emit_putc(emit, wtype, '"');
					break;
				case '\\':
					fy_emit_putc(emit, wtype, '\\');
					break;
				case 0x85:
					fy_emit_putc(emit, wtype, 'N');
					break;
				case 0xa0:
					fy_emit_putc(emit, wtype, '_');
					break;
				case 0x2028:
					fy_emit_putc(emit, wtype, 'L');
					break;
				case 0x2029:
					fy_emit_putc(emit, wtype, 'P');
					break;
				default:
					if (c <= 0xff)
						fy_emit_printf(emit, wtype, "x%02x", c & 0xff);
					else if (c <= 0xffff)
						fy_emit_printf(emit, wtype, "u%04x", c & 0xffff);
					else
						fy_emit_printf(emit, wtype, "U%08x", c & 0xffffffff);
					break;
				}
			}

			emit->flags &= ~FYEF_INDENTATION;
			spaces = false;
			breaks = false;
		}

		s += w;
		srlen++;

	}

	/* dump what's remaining */
	fy_emit_write(emit, wtype, sr, s - sr);

	/* output trailing whitespace */
	fy_emit_write(emit, wtype, stws, etws - stws);

	fy_emit_write_indicator(emit,
			qc == '\'' ? di_single_quote_end : di_double_quote_end,
			flags, indent, wtype);
}

bool fy_emit_write_block_hints(struct fy_emitter *emit, int flags, int indent, const char *str, size_t len, char *chompp)
{
	const char *s, *e, *ee;
	int c, w;
	char chomp = '\0';
	bool explicit_chomp = false;

	s = str;
	e = str + len;

	c = fy_utf8_get(s, e - s, &w);
	if (fy_is_space(c) /* || fy_is_break(c) */ ) {
		fy_emit_putc(emit, fyewt_indicator, '0' + fy_emit_indent(emit));
		explicit_chomp = true;
	}
	emit->flags &= ~FYEF_OPEN_ENDED;

	if (s == e) {
		chomp = '-';
		goto out;
	}

	c = fy_utf8_get_right(s, e - s, &w);

	if (!fy_is_break(c)) {
		chomp = '-';
		goto out;
	}

	if (s >= (e - w)) {
		emit->flags |= FYEF_OPEN_ENDED;
		chomp = '+';
		goto out;
	}

	c = fy_utf8_get_right(s, e - w - s, &w);

	/* hmm? ending with whitespace? */
	/* scan back until we hit something which is not */
	if (fy_is_ws(c)) {
		ee = e - w;
		while (fy_is_ws(c = fy_utf8_get_right(s, ee - s, &w)))
			ee -= w;
	}
	if (fy_is_break(c)) {
		chomp = '+';
		emit->flags |= FYEF_OPEN_ENDED;
		goto out;
	}
out:
	if (chomp)
		fy_emit_putc(emit, fyewt_indicator, chomp);
	*chompp = chomp;
	return explicit_chomp;
}

void fy_emit_write_literal(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent)
{
	bool breaks, explicit_chomp;
	const char *s, *e, *sr;
	int c, w;
	char chomp;
	const char *str = NULL;
	size_t len = 0;

	if (fyn && fyn->scalar)
		str = fy_token_get_text(fyn->scalar, &len);

	if (!str) {
		str = "";
		len = 0;
	}

	s = str;
	e = str + len;

	fy_emit_write_indicator(emit, di_bar, flags, indent, fyewt_indicator);

	explicit_chomp = fy_emit_write_block_hints(emit, flags, indent, str, len, &chomp);
	if ((flags & DDNF_ROOT) || explicit_chomp)
		indent += fy_emit_indent(emit);

	fy_emit_putc(emit, fyewt_linebreak, '\n');
	emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;

	breaks = true;
	sr = s;	/* start of normal output run */
	while (s < e && (c = fy_utf8_get(s, e - s, &w)) > 0) {

		if (fy_is_break(c)) {
			fy_emit_write(emit, fyewt_literal_scalar, sr, s - sr);
			emit->flags &= ~FYEF_INDENTATION;

			sr = s + w;

			if (s + w < e)
				fy_emit_write_indent(emit, indent);

			breaks = true;
		} else {
			if (breaks) {
				fy_emit_write_indent(emit, indent);
				breaks = false;
			}
		}

		s += w;
	}

	/* dump what's remaining */
	fy_emit_write(emit, fyewt_literal_scalar, sr, s - sr);
	emit->flags &= ~FYEF_INDENTATION;
}

void fy_emit_write_folded(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent)
{
	bool leading_spaces, breaks, explicit_chomp;
	const char *s, *e, *sr, *ss;
	int c, cc, w, ww, srlen;
	char chomp;
	const char *str = NULL;
	size_t len = 0;

	if (fyn && fyn->scalar)
		str = fy_token_get_text(fyn->scalar, &len);

	if (!str) {
		str = "";
		len = 0;
	}


	s = str;
	e = str + len;

	fy_emit_write_indicator(emit, di_greater, flags, indent, fyewt_indicator);

	explicit_chomp = fy_emit_write_block_hints(emit, flags, indent, str, len, &chomp);
	if ((flags & DDNF_ROOT) || explicit_chomp)
		indent += fy_emit_indent(emit);

	fy_emit_putc(emit, fyewt_linebreak, '\n');
	emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;

	breaks = true;
	leading_spaces = true;

	sr = s;	/* start of normal output run */
	srlen = 0;
	while (s < e && (c = fy_utf8_get(s, e - s, &w)) > 0) {

		if (fy_is_break(c)) {

			/* output run */

			fy_emit_write(emit, fyewt_folded_scalar, sr, s - sr);
			emit->flags &= ~FYEF_INDENTATION;

			if (!breaks && !leading_spaces) {
				ss = s;
				while (fy_is_break(c = fy_utf8_get(ss, e - ss, &ww)))
					ss += w;
				if (ss > s && !fy_is_blankz(c))
					fy_emit_write_indent(emit, indent);
			}

			if (!fy_is_z(c)) {
				emit->flags &= ~FYEF_INDENTATION;
				fy_emit_write_indent(emit, indent);
			}
			breaks = true;

			s += w;

			sr = s;
			srlen = 0;

			continue;
		}

		if (breaks) {
			fy_emit_write(emit, fyewt_folded_scalar, sr, s - sr);
			sr = s;
			srlen = 0;
			fy_emit_write_indent(emit, indent);
			leading_spaces = fy_is_blank(c);
		}

		cc = fy_utf8_get(s + w, e - (s + w), &ww);
		if (!breaks && fy_is_space(c) && !fy_is_space(cc) &&
				emit->column + srlen > fy_emit_width(emit)) {
			fy_emit_write(emit, fyewt_folded_scalar, sr, s - sr);
			sr = s + w;
			srlen = 0;
			fy_emit_write_indent(emit, indent);
		} else {
			srlen++;
			emit->flags &= ~FYEF_INDENTATION;
		}
		breaks = false;

		s += w;
	}

	/* dump what's remaining */
	fy_emit_write(emit, fyewt_folded_scalar, sr, s - sr);
}

void fy_emit_write_auto_style_scalar(struct fy_emitter *emit, struct fy_node *fyn,
				     int flags, int indent, const char *str, size_t len)
{
	int aflags;

	aflags = fy_token_text_analyze(fyn->scalar);
	if (aflags & FYTTAF_DIRECT_OUTPUT)
		fy_emit_write_plain(emit, fyn, flags, indent);
	else
		fy_emit_write_quoted(emit, fyn, flags, indent, '"');
}

static enum fy_node_style
fy_emit_scalar_style(struct fy_emitter *emit, struct fy_node *fyn,
		     int flags, const char *value, size_t len,
		     enum fy_node_style style)
{
	bool json, flow;
	const char *s, *e;

	/* check if style is allowed (i.e. no block styles in flow context) */
	if ((flags & DDNF_FLOW) && (style == FYNS_LITERAL || style == FYNS_FOLDED))
		style = FYNS_ANY;

	json = fy_emit_is_json_mode(emit);

	/* literal in JSON mode is output as quoted */
	if (json && (style == FYNS_LITERAL || style == FYNS_FOLDED)) {
		style = FYNS_DOUBLE_QUOTED;
		goto out;
	}

	if (json && style == FYNS_PLAIN) {
		/* NULL */
		if (len == 0) {
			style = FYNS_PLAIN;
			goto out;
		}

		/* any of the plain scalars valid for JSON */
		if ((len == 5 && !strncmp(value, "false", 5)) ||
		    (len == 4 && !strncmp(value, "true", 4)) ||
		    (len == 4 && !strncmp(value, "null", 4))) {
			style = FYNS_PLAIN;
			goto out;
		}

		/* check if it's a number */
		s = value;
		e = s + len;
		/* skip the sign */
		if (*s == '+' || *s == '-')
			s++;

		/* skip digits */
		while (s < e && isdigit(*s))
			s++;
		/* dot? */
		if (s < e && *s == '.') {
			s++;
			/* decimal part */
			while (s < e && isdigit(*s))
				s++;
		}
		/* scientific notation */
		if (s < e && (*s == 'e' || *s == 'E')) {
			s++;
			while (s < e && isdigit(*s))
				s++;
		}
		/* everything consumed */
		if (s == e) {
			style = FYNS_PLAIN;
			goto out;
		}
	}

	if (json) {
		style = FYNS_DOUBLE_QUOTED;
		goto out;
	}

	flow = fy_emit_is_flow_mode(emit);

	/* in flow mode, we can't let a bare plain */
	if (flow && len == 0)
		style = FYNS_DOUBLE_QUOTED;

	if (flow && (style == FYNS_ANY || style == FYNS_LITERAL || style == FYNS_FOLDED)) {

		/* if there's a linebreak, use double quoted style */
		if (fy_find_lb(value, len)) {
			style = FYNS_DOUBLE_QUOTED;
			goto out;
		}

		/* check if there's a non printable */
		if (!fy_find_non_print(value, len)) {
			style = FYNS_SINGLE_QUOTED;
			goto out;
		}

		style = FYNS_DOUBLE_QUOTED;
	}

out:
	if (style == FYNS_ANY)
		style = (fy_token_text_analyze(fyn->scalar) & FYTTAF_DIRECT_OUTPUT) ?
				FYNS_PLAIN : FYNS_DOUBLE_QUOTED;

	return style;
}

void fy_emit_scalar(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent)
{
	enum fy_node_style style;
	const char *value = NULL;
	size_t len = 0;

	style = fyn ? fyn->style : FYNS_ANY;
	assert(style != FYNS_FLOW && style != FYNS_BLOCK);

	indent = fy_emit_increase_indent(emit, flags, indent);

	if (!fy_emit_whitespace(emit))
		fy_emit_write_ws(emit);

	if (fyn && fyn->scalar)
		value = fy_token_get_text(fyn->scalar, &len);

	style = fy_emit_scalar_style(emit, fyn, flags, value, len, style);

	switch (style) {
	case FYNS_ALIAS:
		fy_emit_write_alias(emit, fyn, flags, indent);
		break;
	case FYNS_PLAIN:
		fy_emit_write_plain(emit, fyn, flags, indent);
		break;
	case FYNS_DOUBLE_QUOTED:
		fy_emit_write_quoted(emit, fyn, flags, indent, '"');
		break;
	case FYNS_SINGLE_QUOTED:
		fy_emit_write_quoted(emit, fyn, flags, indent, '\'');
		break;
	case FYNS_LITERAL:
		fy_emit_write_literal(emit, fyn, flags, indent);
		break;
	case FYNS_FOLDED:
		fy_emit_write_folded(emit, fyn, flags, indent);
		break;
	default:
		break;
	}
}

void fy_emit_sequence(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent)
{
	struct fy_node *fyni, *fynin;
	bool flow = false, json = false, oneline = false, empty;
	int old_indent = indent, tmp_indent;

	oneline = fy_emit_is_oneline(emit);
	json = fy_emit_is_json_mode(emit);
	empty = fy_node_list_empty(&fyn->sequence);
	if (!json) {
		if (fy_emit_is_flow_mode(emit))
			flow = true;
		else if (fy_emit_is_block_mode(emit))
			flow = false;
		else
			flow = emit->flow_level || fyn->style == FYNS_FLOW || fy_node_list_empty(&fyn->sequence);

		if (flow) {
			if (!emit->flow_level) {
				indent = fy_emit_increase_indent(emit, flags, indent);
				old_indent = indent;
			}

			flags = (flags | DDNF_FLOW) | (flags & ~DDNF_INDENTLESS);
			fy_emit_write_indicator(emit, di_left_bracket, flags, indent, fyewt_indicator);
		} else {
			flags = (flags & ~DDNF_FLOW) | ((flags & DDNF_MAP) ? DDNF_INDENTLESS : 0);
		}
	} else {
		flags = (flags | DDNF_FLOW) | (flags & ~DDNF_INDENTLESS);
		fy_emit_write_indicator(emit, di_left_bracket, flags, indent, fyewt_indicator);
	}

	if (!oneline)
		indent = fy_emit_increase_indent(emit, flags, indent);

	flags &= ~DDNF_ROOT;

	for (fyni = fy_node_list_head(&fyn->sequence); fyni; fyni = fynin) {

		fynin = fy_node_next(&fyn->sequence, fyni);

		flags |= DDNF_SEQ;

		if (!oneline)
			fy_emit_write_indent(emit, indent);

		if (!flow && !json)
			fy_emit_write_indicator(emit, di_dash, flags, indent, fyewt_indicator);

		tmp_indent = indent;
		if (fy_emit_node_has_comment(emit, fyni, fycp_top)) {
			if (!flow && !json)
				tmp_indent = fy_emit_increase_indent(emit, flags, indent);
			fy_emit_node_comment(emit, fyni, flags, tmp_indent, fycp_top);
		}

		fy_emit_node_internal(emit, fyni, flags, indent);

		if ((flow || json) && fynin)
			fy_emit_write_indicator(emit, di_comma, flags, indent, fyewt_indicator);

		fy_emit_node_comment(emit, fyni, flags, indent, fycp_right);

		if (!fynin && (flow || json) && !oneline)
			fy_emit_write_indent(emit, old_indent);

		flags &= ~DDNF_SEQ;
	}

	if (flow || json) {
		if (!oneline && !empty)
			fy_emit_write_indent(emit, old_indent);
		fy_emit_write_indicator(emit, di_right_bracket, flags, old_indent, fyewt_indicator);
	}
}

void fy_emit_mapping(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent)
{
	struct fy_node_pair *fynp, *fynpn, **fynpp = NULL;
	int aflags;
	bool flow = false, json = false, oneline = false, empty;
	int old_indent = indent, tmp_indent, i;

	oneline = fy_emit_is_oneline(emit);
	json = fy_emit_is_json_mode(emit);
	empty = fy_node_pair_list_empty(&fyn->mapping);
	if (!json) {
		if (fy_emit_is_flow_mode(emit))
			flow = true;
		else if (fy_emit_is_block_mode(emit))
			flow = false;
		else
			flow = emit->flow_level || fyn->style == FYNS_FLOW || empty;

		if (flow) {
			if (!emit->flow_level) {
				indent = fy_emit_increase_indent(emit, flags, indent);
				old_indent = indent;
			}

			flags = (flags | DDNF_FLOW) | (flags & ~DDNF_INDENTLESS);
			fy_emit_write_indicator(emit, di_left_brace, flags, indent, fyewt_indicator);
		}  else
			flags &= ~(DDNF_FLOW | DDNF_INDENTLESS);

	} else {
		flags = (flags | DDNF_FLOW) | (flags & ~DDNF_INDENTLESS);
		fy_emit_write_indicator(emit, di_left_brace, flags, indent, fyewt_indicator);
	}

	if (!oneline && !empty)
		indent = fy_emit_increase_indent(emit, flags, indent);

	flags &= ~DDNF_ROOT;

	if (!(emit->cfg->flags & FYECF_SORT_KEYS)) {
		fynp = fy_node_pair_list_head(&fyn->mapping);
		fynpp = NULL;
	} else {
		fynpp = fy_node_mapping_sort_array(fyn, NULL, NULL, NULL);
		i = 0;
		fynp = fynpp[i];
	}

	for (; fynp; fynp = fynpn) {

		if (!fynpp)
			fynpn = fy_node_pair_next(&fyn->mapping, fynp);
		else
			fynpn = fynpp[++i];

		if (!oneline)
			fy_emit_write_indent(emit, indent);

		if (fynp->key) {
			flags = DDNF_MAP;
			switch (fynp->key->type) {
			case FYNT_SCALAR:
				aflags = fy_token_text_analyze(fynp->key->scalar);
				if (aflags & FYTTAF_CAN_BE_SIMPLE_KEY)
					flags |= DDNF_SIMPLE | DDNF_SIMPLE_SCALAR_KEY;
				break;
			case FYNT_SEQUENCE:
				if (fy_node_list_empty(&fynp->key->sequence))
					flags |= DDNF_SIMPLE;
				break;
			case FYNT_MAPPING:
				if (fy_node_pair_list_empty(&fynp->key->mapping))
					flags |= DDNF_SIMPLE;
				break;
			}

			/* complex? */
			if (!(flags & DDNF_SIMPLE))
				fy_emit_write_indicator(emit, di_question_mark, flags, indent, fyewt_indicator);

			fy_emit_node_internal(emit, fynp->key, flags, indent);

			/* if the key is an alias, always output an extra whitespace */
			if (fynp->key->type == FYNT_SCALAR && fynp->key->style == FYNS_ALIAS)
				fy_emit_write_ws(emit);

			flags &= ~DDNF_MAP;
		}

		fy_emit_write_indicator(emit, di_colon, flags, indent, fyewt_indicator);

		tmp_indent = indent;
		if (fy_emit_node_has_comment(emit, fynp->key, fycp_right)) {

			if (!flow && !json)
				tmp_indent = fy_emit_increase_indent(emit, flags, indent);

			fy_emit_node_comment(emit, fynp->key, flags, tmp_indent, fycp_right);
			fy_emit_write_indent(emit, tmp_indent);
		}

		flags = DDNF_MAP;

		if (fynp->value)
			fy_emit_node_internal(emit, fynp->value, flags, indent);

		if ((flow || json) && fynpn)
			fy_emit_write_indicator(emit, di_comma, flags, indent, fyewt_indicator);

		fy_emit_node_comment(emit, fynp->value, flags, indent, fycp_right);

		if (!fynpn && (flow || json) && !oneline)
			fy_emit_write_indent(emit, old_indent);

		flags &= ~DDNF_MAP;
	}

	if (fynpp)
		fy_node_mapping_sort_release_array(fyn, fynpp);

	if (flow || json) {
		if (!oneline && !empty)
			fy_emit_write_indent(emit, old_indent);
		fy_emit_write_indicator(emit, di_right_brace, flags, old_indent, fyewt_indicator);
	}
}

int fy_emit_document_start(struct fy_emitter *emit, struct fy_document *fyd,
			   struct fy_node *fyn_root)
{
	struct fy_document_state *fyds;
	struct fy_node *root;
	struct fy_token *fyt_chk;
	const char *td_handle, *td_prefix;
	size_t td_handle_size, td_prefix_size;
	enum fy_emitter_cfg_flags flags = emit->cfg->flags;
	enum fy_emitter_cfg_flags vd_flags = flags & FYECF_VERSION_DIR(FYECF_VERSION_DIR_MASK);
	enum fy_emitter_cfg_flags td_flags = flags & FYECF_TAG_DIR(FYECF_TAG_DIR_MASK);
	enum fy_emitter_cfg_flags dsm_flags = flags & FYECF_DOC_START_MARK(FYECF_DOC_START_MARK_MASK);
	bool vd, td, dsm;
	bool root_tag_or_anchor __attribute__((__unused__));
	bool had_non_default_tag = false;

	if (!emit || !fyd || emit->fyd || !fyd->fyds)
		return -1;

	root = fyn_root ? : fy_document_root(fyd);

	emit->fyd = fyd;
	fyds = fyd->fyds;

	vd = ((vd_flags == FYECF_VERSION_DIR_AUTO && fyds->version_explicit) ||
	       vd_flags == FYECF_VERSION_DIR_ON) &&
	      !(emit->cfg->flags & FYECF_STRIP_DOC);
	td = ((td_flags == FYECF_TAG_DIR_AUTO && fyds->tags_explicit) ||
	       td_flags == FYECF_TAG_DIR_ON) &&
	      !(emit->cfg->flags & FYECF_STRIP_DOC);

	/* if either a version or directive tags exist, and no previous
	 * explicit document end existed, output one now
	 */
	if (!fy_emit_is_json_mode(emit) && (vd || td) && !(emit->flags & FYEF_HAD_DOCUMENT_END)) {
		if (emit->column)
			fy_emit_putc(emit, fyewt_linebreak, '\n');
		if (!(emit->cfg->flags & FYECF_STRIP_DOC)) {
			fy_emit_puts(emit, fyewt_document_indicator, "...");
			emit->flags &= ~FYEF_WHITESPACE;
			emit->flags |= FYEF_HAD_DOCUMENT_END;
		}
	}

	if (!fy_emit_is_json_mode(emit) && vd) {
		if (emit->column)
			fy_emit_putc(emit, fyewt_linebreak, '\n');
		fy_emit_printf(emit, fyewt_version_directive, "%%YAML %d.%d",
					fyds->version.major, fyds->version.minor);
		fy_emit_putc(emit, fyewt_linebreak, '\n');
		emit->flags = FYEF_WHITESPACE | FYEF_INDENTATION;
	}

	if (!fy_emit_is_json_mode(emit) && td) {

		for (fyt_chk = fy_token_list_first(&fyds->fyt_td); fyt_chk; fyt_chk = fy_token_next(&fyds->fyt_td, fyt_chk)) {

			td_handle = fy_tag_directive_token_handle(fyt_chk, &td_handle_size);
			td_prefix = fy_tag_directive_token_prefix(fyt_chk, &td_prefix_size);
			assert(td_handle && td_prefix);

			if (fy_tag_is_default(td_handle, td_handle_size, td_prefix, td_prefix_size))
				continue;

			had_non_default_tag = true;

			if (emit->column)
				fy_emit_putc(emit, fyewt_linebreak, '\n');
			fy_emit_printf(emit, fyewt_tag_directive, "%%TAG %.*s %.*s",
					(int)td_handle_size, td_handle,
					(int)td_prefix_size, td_prefix);
			fy_emit_putc(emit, fyewt_linebreak, '\n');
			emit->flags = FYEF_WHITESPACE | FYEF_INDENTATION;
		}
	}

	/* NOTE we can force tags and anchors on the --- line */
	root_tag_or_anchor = (root && (root->tag || fy_document_lookup_anchor_by_node(fyd, root)));

	/* always output document start indicator:
	 * - was explicit
	 * - document has tags
	 * - document has an explicit version
	 * - root exists & has a tag or an anchor
	 */
	dsm = (dsm_flags == FYECF_DOC_START_MARK_AUTO &&
			(!fyds->start_implicit ||
			  fyds->tags_explicit || fyds->version_explicit ||
			  had_non_default_tag)) ||
	       dsm_flags == FYECF_DOC_START_MARK_ON;

	/* if there was previous output without document end */
	if (!dsm && (emit->flags & FYEF_HAD_DOCUMENT_OUTPUT) &&
	           !(emit->flags & FYEF_HAD_DOCUMENT_END))
		dsm = true;

	if (!fy_emit_is_json_mode(emit) && dsm) {
		if (emit->column)
			fy_emit_putc(emit, fyewt_linebreak, '\n');
		if (!(emit->cfg->flags & FYECF_STRIP_DOC)) {
			fy_emit_puts(emit, fyewt_document_indicator, "---");
			emit->flags &= ~FYEF_WHITESPACE;
			emit->flags |= FYEF_HAD_DOCUMENT_START;
		}
	} else
		emit->flags &= ~FYEF_HAD_DOCUMENT_START;

	/* clear that in any case */
	emit->flags &= ~FYEF_HAD_DOCUMENT_END;

	return 0;
}

int fy_emit_document_end(struct fy_emitter *emit)
{
	struct fy_document *fyd;
	const struct fy_document_state *fyds;
	enum fy_emitter_cfg_flags flags = emit->cfg->flags;
	enum fy_emitter_cfg_flags dem_flags = flags & FYECF_DOC_END_MARK(FYECF_DOC_END_MARK_MASK);
	bool dem;

	if (!emit || !emit->fyd || !emit->fyd->fyds)
		return -1;

	fyd = emit->fyd;
	fyds = fyd->fyds;

	if (emit->column != 0) {
		fy_emit_putc(emit, fyewt_linebreak, '\n');
		emit->flags = FYEF_WHITESPACE | FYEF_INDENTATION;
	}

	dem = ((dem_flags == FYECF_DOC_END_MARK_AUTO && !fyds->end_implicit) ||
	        dem_flags == FYECF_DOC_END_MARK_ON) &&
	       !(emit->cfg->flags & FYECF_STRIP_DOC);
	if (!fy_emit_is_json_mode(emit) && dem) {
		fy_emit_puts(emit, fyewt_document_indicator, "...");
		fy_emit_putc(emit, fyewt_linebreak, '\n');
		emit->flags = FYEF_WHITESPACE | FYEF_INDENTATION;
		emit->flags |= FYEF_HAD_DOCUMENT_END;
	} else
		emit->flags &= ~FYEF_HAD_DOCUMENT_END;

	/* stop our association with the document */
	emit->fyd = NULL;

	/* mark that we did output a document earlier */
	emit->flags |= FYEF_HAD_DOCUMENT_OUTPUT;

	return 0;
}

int fy_emit_explicit_document_end(struct fy_emitter *emit)
{
	if (!emit)
		return -1;

	if (emit->column != 0) {
		fy_emit_putc(emit, fyewt_linebreak, '\n');
		emit->flags = FYEF_WHITESPACE | FYEF_INDENTATION;
	}

	if (!fy_emit_is_json_mode(emit)) {
		fy_emit_puts(emit, fyewt_document_indicator, "...");
		fy_emit_putc(emit, fyewt_linebreak, '\n');
		emit->flags = FYEF_WHITESPACE | FYEF_INDENTATION;
		emit->flags |= FYEF_HAD_DOCUMENT_END;
	} else
		emit->flags &= ~FYEF_HAD_DOCUMENT_END;

	/* stop our association with the document */
	emit->fyd = NULL;

	/* mark that we did output a document earlier */
	emit->flags |= FYEF_HAD_DOCUMENT_OUTPUT;

	return 0;
}

void fy_emit_setup(struct fy_emitter *emit, const struct fy_emitter_cfg *cfg)
{
	memset(emit, 0, sizeof(*emit));
	emit->cfg = cfg;
	emit->flags = FYEF_WHITESPACE | FYEF_INDENTATION;

	/* start as if there was a previous document with an explicit end */
	/* this allows implicit documents start without an indicator */
	emit->flags |= FYEF_HAD_DOCUMENT_END;
}

void fy_emit_cleanup(struct fy_emitter *emit)
{
	/* nothing */
}

int fy_emit_node(struct fy_emitter *emit, struct fy_node *fyn)
{
	if (fyn)
		fy_emit_node_internal(emit, fyn, DDNF_ROOT, -1);
	return 0;
}

int fy_emit_root_node(struct fy_emitter *emit, struct fy_node *fyn)
{
	if (!emit || !fyn)
		return -1;

	/* top comment first */
	fy_emit_node_comment(emit, fyn, DDNF_ROOT, -1, fycp_top);

	fy_emit_node_internal(emit, fyn, DDNF_ROOT, -1);

	/* right comment next */
	fy_emit_node_comment(emit, fyn, DDNF_ROOT, -1, fycp_right);

	/* bottom comment last */
	fy_emit_node_comment(emit, fyn, DDNF_ROOT, -1, fycp_bottom);

	return 0;
}

int fy_emit_document(struct fy_emitter *emit, struct fy_document *fyd)
{
	int rc;

	rc = fy_emit_document_start(emit, fyd, NULL);
	if (rc)
		return rc;

	rc = fy_emit_root_node(emit, fyd->root);
	if (rc)
		return rc;

	rc = fy_emit_document_end(emit);

	return rc;
}

const struct fy_emitter_cfg *fy_emitter_get_cfg(struct fy_emitter *emit)
{
	if (!emit)
		return NULL;

	return emit->cfg;
}

struct fy_emitter *fy_emitter_create(struct fy_emitter_cfg *cfg)
{
	struct fy_emitter *emit;

	if (!cfg)
		return NULL;

	emit = malloc(sizeof(*emit));
	if (!emit)
		return NULL;

	fy_emit_setup(emit, cfg);

	return emit;
}

void fy_emitter_destroy(struct fy_emitter *emit)
{
	if (!emit)
		return;

	fy_emit_cleanup(emit);

	free(emit);
}

struct fy_emit_buffer_state {
	char *buf;
	int size;
	int pos;
	int need;
	bool grow;
};

static int do_buffer_output(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *str, int len, void *userdata)
{
	struct fy_emit_buffer_state *state = emit->cfg->userdata;
	int left;
	int pagesize = 0;
	int size;
	char *bufnew;

	state->need += len;
	left = state->size - state->pos;
	if (left < len) {
		if (!state->grow)
			return 0;

		pagesize = sysconf(_SC_PAGESIZE);
		size = state->need + pagesize - 1;
		size = size - size % pagesize;

		bufnew = realloc(state->buf, size);
		if (!bufnew)
			return -1;
		state->buf = bufnew;
		state->size = size;
		left = state->size - state->pos;

	}

	if (len > left)
		len = left;
	if (state->buf)
		memcpy(state->buf + state->pos, str, len);
	state->pos += len;

	return len;
}

static int fy_emit_str_internal(struct fy_document *fyd,
				enum fy_emitter_cfg_flags flags,
				struct fy_node *fyn, char **bufp, int *sizep,
				bool grow)
{
	struct fy_emitter emit_state, *emit = &emit_state;
	struct fy_emitter_cfg emit_cfg;
	struct fy_emit_buffer_state state;
	int rc = -1;

	memset(&emit_cfg, 0, sizeof(emit_cfg));
	memset(&state, 0, sizeof(state));

	emit_cfg.output = do_buffer_output;
	emit_cfg.userdata = &state;
	emit_cfg.flags = flags;
	state.buf = *bufp;
	state.size = *sizep;
	state.grow = grow;

	fy_emit_setup(emit, &emit_cfg);
	rc = fyd ? fy_emit_document(emit, fyd) : fy_emit_node(emit, fyn);
	fy_emit_cleanup(emit);

	if (rc)
		goto out_err;

	/* terminating zero */
	rc = do_buffer_output(emit, fyewt_terminating_zero, "\0", 1, emit->cfg->userdata);

	if (rc != 1) {
		rc = -1;
		goto out_err;
	}

	*sizep = state.need;

	if (!grow)
		return 0;

	*bufp = realloc(state.buf, *sizep);
	if (!*bufp) {
		rc = -1;
		goto out_err;
	}

	return 0;

out_err:
	if (grow && state.buf)
		free(state.buf);
	*bufp = NULL;
	*sizep = 0;

	return rc;
}

int fy_emit_document_to_buffer(struct fy_document *fyd, enum fy_emitter_cfg_flags flags, char *buf, int size)
{
	int rc;

	rc = fy_emit_str_internal(fyd, flags, NULL, &buf, &size, false);
	if (rc != 0)
		return -1;
	return size;
}

char *fy_emit_document_to_string(struct fy_document *fyd, enum fy_emitter_cfg_flags flags)
{
	char *buf = NULL;
	int rc, size = 0;

	rc = fy_emit_str_internal(fyd, flags, NULL, &buf, &size, true);
	if (rc != 0)
		return NULL;
	return buf;
}

static int do_file_output(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *str, int len, void *userdata)
{
	FILE *fp = userdata;

	return fwrite(str, 1, len, fp);
}

int fy_emit_document_to_fp(struct fy_document *fyd, enum fy_emitter_cfg_flags flags,
			   FILE *fp)
{
	struct fy_emitter emit_state, *emit = &emit_state;
	struct fy_emitter_cfg emit_cfg;
	int rc;

	if (!fp)
		return -1;

	memset(&emit_cfg, 0, sizeof(emit_cfg));
	emit_cfg.output = do_file_output;
	emit_cfg.userdata = fp;
	emit_cfg.flags = flags;
	fy_emit_setup(emit, &emit_cfg);
	rc = fy_emit_document(emit, fyd);
	fy_emit_cleanup(emit);

	return rc ? rc : 0;
}

int fy_emit_document_to_file(struct fy_document *fyd,
			     enum fy_emitter_cfg_flags flags,
			     const char *filename)
{
	FILE *fp;
	int rc;

	fp = filename ? fopen(filename, "wa") : stdout;
	if (!fp)
		return -1;

	rc = fy_emit_document_to_fp(fyd, flags, fp);

	if (fp != stdout)
		fclose(fp);

	return rc ? rc : 0;
}

int fy_emit_node_to_buffer(struct fy_node *fyn, enum fy_emitter_cfg_flags flags, char *buf, int size)
{
	int rc;

	rc = fy_emit_str_internal(NULL, flags, fyn, &buf, &size, false);
	if (rc != 0)
		return -1;
	return size;
}

char *fy_emit_node_to_string(struct fy_node *fyn, enum fy_emitter_cfg_flags flags)
{
	char *buf = NULL;
	int rc, size = 0;

	rc = fy_emit_str_internal(NULL, flags, fyn, &buf, &size, true);
	if (rc != 0)
		return NULL;
	return buf;
}
