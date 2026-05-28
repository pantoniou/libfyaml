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
#include <ctype.h>
#include <errno.h>

#include <libfyaml.h>

#include "fy-parse.h"
#include "fy-emit.h"
#ifdef HAVE_GENERIC
#include "fy-generic.h"
#endif

static void fy_emitter_fill_default_colors(struct fy_emitter *fye);
static FILE *fy_emitter_get_output_fp(struct fy_emitter *fye);
static int fy_emitter_get_output_fd(struct fy_emitter *fye);
static int fy_emitter_null_output(struct fy_emitter *fye, enum fy_emitter_write_type type, const char *str, int len, void *userdata);

void fy_emit_save_ctx_cleanup(struct fy_emitter *emit, struct fy_emit_save_ctx *sc);

static inline struct fy_token_list *token_recycle_list(struct fy_emitter *emit, struct fy_parser *fyp)
{
	if (fyp && fyp->recycled_token_list)
		return fyp->recycled_token_list;
	if (emit && emit->recycled_token_list)
		return emit->recycled_token_list;
	return NULL;
}

static inline void fy_emit_token_unref(struct fy_emitter *emit, struct fy_parser *fyp, struct fy_token *fyt)
{
	fy_token_unref_rl(token_recycle_list(emit, fyp), fyt);
}

/* fwd decl */
void fy_emit_write(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *str, size_t len);
void fy_emit_printf(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *fmt, ...)
	FY_FORMAT(printf, 3, 4);

static inline bool fy_emit_is_json_mode(const struct fy_emitter *emit)
{
	enum fy_emitter_cfg_flags flags;

	if (emit->force_json)
		return true;

	flags = emit->xcfg.cfg.flags & FYECF_MODE(FYECF_MODE_MASK);
	return flags == FYECF_MODE_JSON || flags == FYECF_MODE_JSON_TP ||
	       flags == FYECF_MODE_JSON_ONELINE || flags == FYECF_MODE_JSON_COMPACT;
}

static inline bool fy_emit_is_flow_mode(const struct fy_emitter *emit)
{
	enum fy_emitter_cfg_flags flags = emit->xcfg.cfg.flags & FYECF_MODE(FYECF_MODE_MASK);

	return flags == FYECF_MODE_FLOW || flags == FYECF_MODE_FLOW_ONELINE ||
	       flags == FYECF_MODE_FLOW_COMPACT || fy_emit_is_json_mode(emit);
}

static inline bool fy_emit_is_block_mode(const struct fy_emitter *emit)
{
	enum fy_emitter_cfg_flags flags = emit->xcfg.cfg.flags & FYECF_MODE(FYECF_MODE_MASK);

	return flags == FYECF_MODE_BLOCK || flags == FYECF_MODE_DEJSON || flags == FYECF_MODE_PRETTY;
}

static inline bool fy_emit_is_oneline(const struct fy_emitter *emit)
{
	enum fy_emitter_cfg_flags flags = emit->xcfg.cfg.flags & FYECF_MODE(FYECF_MODE_MASK);

	return flags == FYECF_MODE_FLOW_ONELINE || flags == FYECF_MODE_JSON_ONELINE;
}

static inline bool fy_emit_is_compact(const struct fy_emitter *emit)
{
	enum fy_emitter_cfg_flags flags = emit->xcfg.cfg.flags & FYECF_MODE(FYECF_MODE_MASK);

	return flags == FYECF_MODE_FLOW_COMPACT || flags == FYECF_MODE_JSON_COMPACT;
}

static inline bool fy_emit_is_oneline_or_compact(const struct fy_emitter *emit)
{
	return fy_emit_is_oneline(emit) || fy_emit_is_compact(emit);
}

static inline bool fy_emit_sc_oneline(const struct fy_emitter *emit,
				      const struct fy_emit_save_ctx *sc)
{
	return fy_emit_is_oneline_or_compact(emit) || sc->oneline_flow;
}

static inline bool fy_emit_preserve_flow_layout(const struct fy_emitter *emit)
{
	return !!(emit->xcfg.xflags & FYEXCF_PRESERVE_FLOW_LAYOUT);
}

static inline bool fy_emit_is_dejson_mode(const struct fy_emitter *emit)
{
	enum fy_emitter_cfg_flags flags = emit->xcfg.cfg.flags & FYECF_MODE(FYECF_MODE_MASK);

	return flags == FYECF_MODE_DEJSON;
}

static inline bool fy_emit_is_pretty_mode(const struct fy_emitter *emit)
{
	enum fy_emitter_cfg_flags flags = emit->xcfg.cfg.flags & FYECF_MODE(FYECF_MODE_MASK);

	return flags == FYECF_MODE_PRETTY;
}

static inline bool fy_emit_is_original_mode(const struct fy_emitter *emit)
{
	enum fy_emitter_cfg_flags flags = emit->xcfg.cfg.flags & FYECF_MODE(FYECF_MODE_MASK);

	return flags == FYECF_MODE_ORIGINAL;
}

static inline bool fy_emit_is_manual(const struct fy_emitter *emit)
{
	enum fy_emitter_cfg_flags flags = emit->xcfg.cfg.flags & FYECF_MODE(FYECF_MODE_MASK);

	return flags == FYECF_MODE_MANUAL;
}

static inline int fy_emit_indent(struct fy_emitter *emit)
{
	int indent;

	indent = (emit->xcfg.cfg.flags & FYECF_INDENT(FYECF_INDENT_MASK)) >> FYECF_INDENT_SHIFT;
	return indent ? indent : 2;
}

static inline bool fy_emit_is_width_infinite(struct fy_emitter *emit)
{
	return fy_emit_is_oneline(emit) ||
	       ((emit->xcfg.cfg.flags & FYECF_WIDTH(FYECF_WIDTH_MASK)) >> FYECF_WIDTH_SHIFT)
			== FYECF_WIDTH_MASK;
}

static inline int fy_emit_width(struct fy_emitter *emit)
{
	int width;

	if (fy_emit_is_width_infinite(emit))
		return INT_MAX;

	width = (emit->xcfg.cfg.flags & FYECF_WIDTH(FYECF_WIDTH_MASK)) >> FYECF_WIDTH_SHIFT;
	if (width == 0)
		return 80;
	if (width == FYECF_WIDTH_MASK)
		return INT_MAX;
	return width;
}

static inline bool fy_emit_output_comments(struct fy_emitter *emit)
{
	return !!(emit->xcfg.cfg.flags & FYECF_OUTPUT_COMMENTS) &&
		!fy_emit_is_oneline_or_compact(emit) &&
		!fy_emit_is_json_mode(emit);
}

static int fy_emit_node_check_json(struct fy_emitter *emit, struct fy_node *fyn)
{
	struct fy_document *fyd;
	struct fy_node *fyni;
	struct fy_node_pair *fynp, *fynpi;
	int ret;

	if (!fyn)
		return 0;

	fyd = fyn->fyd;

	switch (fyn->type) {
	case FYNT_SCALAR:
		FYD_TOKEN_ERROR_CHECK(fyd, fyn->scalar, FYEM_INTERNAL,
				!fy_node_is_alias(fyn), err_out,
				"aliases not allowed in JSON emit mode");
		break;

	case FYNT_SEQUENCE:
		for (fyni = fy_node_list_head(&fyn->sequence); fyni;
				fyni = fy_node_next(&fyn->sequence, fyni)) {
			ret = fy_emit_node_check_json(emit, fyni);
			if (ret)
				return ret;
		}
		break;

	case FYNT_MAPPING:
		for (fynp = fy_node_pair_list_head(&fyn->mapping); fynp; fynp = fynpi) {

			fynpi = fy_node_pair_next(&fyn->mapping, fynp);

			ret = fy_emit_node_check_json(emit, fynp->key);
			if (ret)
				return ret;
			ret = fy_emit_node_check_json(emit, fynp->value);
			if (ret)
				return ret;
		}
		break;
	}
	return 0;
err_out:
	return -1;
}

static int fy_emit_node_check(struct fy_emitter *emit, struct fy_node *fyn)
{
	int ret;

	if (!fyn)
		return 0;

	if (fy_emit_is_json_mode(emit) && !emit->source_json) {
		ret = fy_emit_node_check_json(emit, fyn);
		if (ret)
			return ret;
	}

	return 0;
}

static bool
fy_tokens_oneline(struct fy_token *fyt_start, struct fy_token *fyt_end)
{
	const struct fy_mark *sm, *em;

	if (!fyt_start || !fyt_end)
		return false;

	sm = fy_token_start_mark(fyt_start);
	em = fy_token_end_mark(fyt_end);

	return sm && em && sm->line == em->line && sm->input_pos <= em->input_pos;
}

static bool
fy_node_oneline(struct fy_node *fyn)
{
	if (!fyn)
		return false;

	switch (fyn->type) {
	case FYNT_SEQUENCE:
		return fy_tokens_oneline(fyn->sequence_start, fyn->sequence_end);

	case FYNT_MAPPING:
		return fy_tokens_oneline(fyn->mapping_start, fyn->mapping_end);

	default:
		break;
	}

	return false;
}

static bool
fy_emit_node_oneline_flow(struct fy_emitter *emit, struct fy_node *fyn)
{
	return fy_emit_preserve_flow_layout(emit) &&
	       fyn && fyn->style == FYNS_FLOW &&
	       fy_node_oneline(fyn);
}

static enum fy_node_style
fy_token_node_style(struct fy_token *fyt)
{
	enum fy_token_type type;

	if (!fyt)
		return FYNS_ANY;

	type = fyt->type;
	if (fy_token_type_is_sequence_start(type))
		return type == FYTT_FLOW_SEQUENCE_START ? FYNS_FLOW : FYNS_BLOCK;

	if (fy_token_type_is_sequence_end(type))
		return type == FYTT_FLOW_SEQUENCE_END ? FYNS_FLOW : FYNS_BLOCK;

	if (fy_token_type_is_mapping_start(type))
		return type == FYTT_FLOW_MAPPING_START ? FYNS_FLOW : FYNS_BLOCK;

	if (fy_token_type_is_mapping_end(type))
		return type == FYTT_FLOW_MAPPING_END ? FYNS_FLOW : FYNS_BLOCK;

	/* we return ANY for all others */
	return FYNS_ANY;
}

void fy_emit_node_internal(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent, bool is_key);
void fy_emit_node_scalar(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent, bool is_key);
void fy_emit_node_sequence(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent);
void fy_emit_node_mapping(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent);

void fy_emit_write(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *str, size_t len);
void fy_emit_puts(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *str);
void fy_emit_putc(struct fy_emitter *emit, enum fy_emitter_write_type type, int c);

/* simple write, just ascii, advance column */
void fy_emit_write_simple(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *str, size_t len)
{
	int outlen;

	if (!len)
		return;

	outlen = emit->xcfg.cfg.output(emit, type, str, (int)len, emit->xcfg.cfg.userdata);
	if (outlen != (int)len)
		emit->output_error = true;

	emit->column += (int)len;
}

/* simple puts as above */
void fy_emit_puts_simple(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *str)
{
	fy_emit_write_simple(emit, type, str, strlen(str));
}

void fy_emit_putc_simple(struct fy_emitter *emit, enum fy_emitter_write_type type, int c)
{
	char cc = (char)c;

	fy_emit_write_simple(emit, type, &cc, 1);
	if (cc == '\n') {
		emit->column = 0;
		emit->line++;
	}
}

void fy_emit_write(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *str, size_t len)
{
	int c, w;
	const char *m, *e;
	int outlen;

	if (!len || len > INT_MAX)
		return;

	outlen = emit->xcfg.cfg.output(emit, type, str, (int)len, emit->xcfg.cfg.userdata);
	if (outlen != (int)len)
		emit->output_error = true;

	e = str + len;
	while ((c = fy_utf8_get(str, (e - str), &w)) >= 0) {

		/* special handling for MSDOS */
		if (c == '\r' && (e - str) > 1 && str[1] == '\n') {
			str += 2;
			emit->column = 0;
			emit->line++;
			continue;
		}

		/* regular line break */
		if (fy_is_lb_r_n(c)) {
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
	char buf[128];
	char *str;
	int size;
	va_list ap2;

	va_copy(ap2, ap);

	str = buf;
	size = vsnprintf(str, sizeof(buf) - 1, fmt, ap);
	assert(size >= 0);
	if (size < 0)
		return;

	if ((size_t)size >= sizeof(buf) - 1) {
		str = alloca(size + 1);
		size = vsnprintf(str, size + 1, fmt, ap2);
		assert(size >= 0);
		if (size < 0)
			return;
	}

	fy_emit_write(emit, type, str, size);
}

void fy_emit_printf(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_emit_vprintf(emit, type, fmt, ap);
	va_end(ap);
}

void fy_emit_write_ws(struct fy_emitter *emit)
{
	fy_emit_putc_simple(emit, fyewt_whitespace, ' ');
	emit->flags |= FYEF_WHITESPACE;
}

void fy_emit_write_indent(struct fy_emitter *emit, int indent)
{
	int len;
	char *ws;

	indent = indent > 0 ? indent : 0;

	if (!fy_emit_indentation(emit) || emit->column > indent ||
	    (emit->column == indent && !fy_emit_whitespace(emit)))
		fy_emit_putc_simple(emit, fyewt_linebreak, '\n');

	if (emit->column < indent) {
		len = indent - emit->column;
		ws = alloca(len + 1);
		memset(ws, ' ', len);
		ws[len] = '\0';
		fy_emit_write_simple(emit, fyewt_indent, ws, len);
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
			     int flags FY_UNUSED,
			     int indent FY_UNUSED,
			     enum fy_emitter_write_type wtype)
{
	/* extended indicators mode? */
	if (wtype == fyewt_indicator &&
	    (emit->xcfg.xflags & FYEXCF_EXTENDED_INDICATORS)) {
		wtype = FYEWT_EXTENDED_INDICATORS_FIRST + (unsigned int)indicator;
		assert(wtype >= FYEWT_EXTENDED_INDICATORS_FIRST &&
				wtype <= FYEWT_EXTENDED_INDICATORS_LAST);
	}

	if (emit->flags & FYEF_NEED_WS_BEFORE_IND) {
		emit->flags &= ~FYEF_NEED_WS_BEFORE_IND;
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
	}

	switch (indicator) {

	case di_question_mark:
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
		fy_emit_putc_simple(emit, wtype, '?');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_OPEN_ENDED);
		break;

	case di_colon:
		fy_emit_putc_simple(emit, wtype, ':');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_OPEN_ENDED);
		break;

	case di_dash:
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
		fy_emit_putc_simple(emit, wtype, '-');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_OPEN_ENDED);
		break;

	case di_left_bracket:
	case di_left_brace:
		emit->flow_level++;
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
		fy_emit_putc_simple(emit, wtype, indicator == di_left_bracket ? '[' : '{');
		emit->flags |= FYEF_WHITESPACE;
		emit->flags &= ~(FYEF_INDENTATION | FYEF_OPEN_ENDED);
		break;

	case di_right_bracket:
	case di_right_brace:
		emit->flow_level--;
		fy_emit_putc_simple(emit, wtype, indicator == di_right_bracket ? ']' : '}');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION | FYEF_OPEN_ENDED);
		break;

	case di_comma:
		fy_emit_putc_simple(emit, wtype, ',');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION | FYEF_OPEN_ENDED);
		break;

	case di_bar:
	case di_greater:
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
		fy_emit_putc_simple(emit, wtype, indicator == di_bar ? '|' : '>');
		emit->flags &= ~(FYEF_INDENTATION | FYEF_WHITESPACE | FYEF_OPEN_ENDED);
		break;

	case di_single_quote_start:
	case di_double_quote_start:
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
		fy_emit_putc_simple(emit, wtype, indicator == di_single_quote_start ? '\'' : '"');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION | FYEF_OPEN_ENDED);
		break;

	case di_single_quote_end:
	case di_double_quote_end:
		fy_emit_putc_simple(emit, wtype, indicator == di_single_quote_end ? '\'' : '"');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION | FYEF_OPEN_ENDED);
		break;

	case di_ambersand:
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
		fy_emit_putc_simple(emit, wtype, '&');
		emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION);
		break;

	case di_star:
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
		fy_emit_putc_simple(emit, wtype, '*');
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

void fy_emit_write_comment(struct fy_emitter *emit,
			   int flags FY_UNUSED,
			   int indent,
			   const char *str,
			   size_t len,
			   enum fy_lb_mode lb_mode,
			   bool needs_hash)
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

	if (needs_hash)
		fy_emit_write_simple(emit, fyewt_comment, "# ", 2);

	while (s < e && (c = fy_utf8_get(s, e - s, &w)) > 0) {

		if (fy_is_lb_m(c, lb_mode)) {

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

				if (needs_hash)
					fy_emit_write_simple(emit, fyewt_comment, "# ", 2);
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

struct fy_atom *
fy_emit_token_comment_handle(struct fy_emitter *emit FY_UNUSED,
			     struct fy_token *fyt,
			     enum fy_comment_placement placement)
{
	struct fy_atom *handle;

	handle = fy_token_comment_handle(fyt, placement, false);
	return handle && fy_atom_is_set(handle) ? handle : NULL;
}

void fy_emit_document_start_indicator(struct fy_emitter *emit)
{
	/* do not emit twice */
	if (emit->flags & FYEF_HAD_DOCUMENT_START)
		return;

	/* do not try to emit if it's json mode */
	if (fy_emit_is_json_mode(emit))
		goto no_doc_emit;

	/* output linebreak anyway */
	if (emit->column)
		fy_emit_putc_simple(emit, fyewt_linebreak, '\n');

	/* ok, emit document start indicator */
	fy_emit_puts_simple(emit, fyewt_document_indicator, "---");
	emit->flags &= ~FYEF_WHITESPACE;
	emit->flags |= FYEF_HAD_DOCUMENT_START;
	return;

no_doc_emit:
	emit->flags &= ~FYEF_HAD_DOCUMENT_START;
}

struct fy_token *fy_node_value_token(struct fy_node *fyn)
{
	struct fy_token *fyt;

	if (!fyn)
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

	return fyt;
}

bool fy_emit_token_has_comment(struct fy_emitter *emit, struct fy_token *fyt, enum fy_comment_placement placement)
{
	if (!fyt || !fy_emit_output_comments(emit))
		return false;
	return fy_emit_token_comment_handle(emit, fyt, placement) ? true : false;
}

bool fy_emit_node_has_comment(struct fy_emitter *emit, struct fy_node *fyn, enum fy_comment_placement placement)
{
	return fy_emit_token_has_comment(emit, fy_node_value_token(fyn), placement);
}

void fy_emit_comment_prolog(struct fy_emitter *emit,
			    int flags FY_UNUSED,
		            int indent, int indent_delta,
			    enum fy_comment_placement placement)
{
	int comment_indent;

	if (placement == fycp_top || placement == fycp_bottom) {
		comment_indent = indent > 0 ? indent : 0;
		if (indent >= 0) {
			comment_indent = indent + indent_delta;
			if (comment_indent < 0)
				comment_indent = 0;
		}
		fy_emit_write_indent(emit, comment_indent);
		emit->flags |= FYEF_WHITESPACE;
	}
}

void fy_emit_comment_epilog(struct fy_emitter *emit,
			    int flags FY_UNUSED,
		            int indent,
			    enum fy_comment_placement placement)
{
	emit->flags &= ~FYEF_INDENTATION;

	if (placement == fycp_top || placement == fycp_bottom) {
		fy_emit_write_indent(emit, indent);
		emit->flags |= FYEF_WHITESPACE;
	}
}

void fy_emit_comment(struct fy_emitter *emit, int flags, int indent,
		     enum fy_comment_placement placement,
		     const char *text, size_t len,
		     int indent_delta, enum fy_lb_mode lb_mode,
		     bool needs_hash)
{
	fy_emit_comment_prolog(emit, flags, indent, indent_delta, placement);
	fy_emit_write_comment(emit, flags, indent, text, len, lb_mode, needs_hash);
	fy_emit_comment_epilog(emit, flags, indent, placement);
}

void fy_emit_token_comment(struct fy_emitter *emit, struct fy_token *fyt, int flags, int indent,
			  enum fy_comment_placement placement)
{
	char buf[256];
	struct fy_atom *handle;
	const char *t;
	char *alloc;
	size_t len;

	handle = fy_emit_token_comment_handle(emit, fyt, placement);
	if (!handle)
		return;

	t = fy_atom_format_text(handle, buf, sizeof(buf));
	if (!t) {
		len = fy_atom_format_text_length(handle);
		if ((ssize_t)len < 0)
			return;

		alloc = malloc(len + 1);
		if (!alloc)
			return;

		t = fy_atom_format_text(handle, alloc, len + 1);
		if (!t)
			return;
	} else {
		alloc = NULL;
		len = strlen(t);
	}

	fy_emit_comment(emit, flags, indent, placement, t, len,
			handle->indent_delta, fy_atom_lb_mode(handle), false);

	if (alloc)
		free(alloc);
}

void fy_emit_common_text_preamble(struct fy_emitter *emit,
				  const char *anchor, size_t anchor_len,
				  const char *tag, size_t tag_len,
				  const char *td_handle, size_t td_handle_len,
				  const char *td_prefix, size_t td_prefix_len,
				  int flags, int indent,
				  enum fy_node_type type)
{
	/* content for root always starts on a new line */
	if (!fy_emit_is_oneline(emit) && (flags & DDNF_ROOT) && emit->column != 0 &&
            !(emit->flags & FYEF_HAD_DOCUMENT_START)) {
		fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
		emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;
	}

	/* nothing more to do on JSON mode */
	if (fy_emit_is_json_mode(emit))
		return;

	if (emit->xcfg.cfg.flags & FYECF_STRIP_LABELS) {
		anchor = NULL;
		anchor_len = 0;
	}

	if (emit->xcfg.cfg.flags & FYECF_STRIP_TAGS) {
		tag = td_prefix = td_handle = NULL;
		tag_len = td_prefix_len = td_handle_len = 0;
	}

	if (anchor) {
		fy_emit_write_indicator(emit, di_ambersand, flags, indent, fyewt_anchor);
		fy_emit_write(emit, fyewt_anchor, anchor, anchor_len);
	}

	if (tag) {
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);

		if (!td_handle_len)
			fy_emit_printf(emit, fyewt_tag, "!<%.*s>", (int)tag_len, tag);
		else
			fy_emit_printf(emit, fyewt_tag, "%.*s%.*s",
					(int)td_handle_len, td_handle,
					(int)(tag_len - td_prefix_len), tag + td_prefix_len);

		emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION);
	}

	if (type != FYNT_SCALAR) {
		if ((flags & DDNF_ROOT) && emit->column != 0) {
			fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
			emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;
		}
	} else {
		/* if we're pretty and root at column 0 (meaning it's a single scalar document) output --- */
		if ((flags & DDNF_ROOT) && fy_emit_is_pretty_mode(emit) && !emit->column &&
				!fy_emit_is_flow_mode(emit) && !(flags & DDNF_FLOW))
			fy_emit_document_start_indicator(emit);
	}
}

void fy_emit_common_node_preamble(struct fy_emitter *emit,
				  struct fy_token *fyt_value FY_UNUSED,
				  struct fy_token *fyt_anchor,
				  struct fy_token *fyt_tag,
				  int flags, int indent,
				  enum fy_node_type type)
{
	const char *anchor, *tag, *td_prefix, *td_handle;
	size_t tag_len, anchor_len, td_prefix_len, td_handle_len;

	if (fyt_anchor)
		anchor = fy_token_get_text(fyt_anchor, &anchor_len);
	else {
		anchor = NULL;
		anchor_len = 0;
	}

	if (fyt_tag) {
		tag = fy_token_get_text(fyt_tag, &tag_len);

		td_handle = fy_tag_token_get_directive_handle(fyt_tag, &td_handle_len);
		if (!td_handle)
			return;
		td_prefix = fy_tag_token_get_directive_prefix(fyt_tag, &td_prefix_len);
		if (!td_prefix)
			return;
	} else {
		tag = td_prefix = td_handle = NULL;
		tag_len = td_prefix_len = td_handle_len = 0;
	}

	fy_emit_common_text_preamble(emit, anchor, anchor_len,
				     tag, tag_len,
				     td_handle, td_handle_len,
				     td_prefix, td_prefix_len,
				     flags, indent, type);
}

void fy_emit_node_internal(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent, bool is_key)
{
	enum fy_node_type type;
	struct fy_anchor *fya;
	struct fy_token *fyt_anchor, *fyt_value;

	if (!(emit->xcfg.cfg.flags & FYECF_STRIP_LABELS)) {
		fya = fy_document_lookup_anchor_by_node(emit->fyd, fyn);
		fyt_anchor = fya ? fya->anchor : NULL;
	} else
		fyt_anchor = NULL;

	type = fyn ? fyn->type : FYNT_SCALAR;

	switch (type) {
	case FYNT_SCALAR:
		fyt_value = fyn ? fyn->scalar : NULL;
		break;
	case FYNT_SEQUENCE:
		fyt_value = fyn->sequence_start;
		break;
	case FYNT_MAPPING:
		fyt_value = fyn->mapping_start;
		break;
	default:
		FY_IMPOSSIBLE_ABORT();
	}

	fy_emit_common_node_preamble(emit, fyt_value,
				     fyt_anchor, fyn->tag,
				     flags, indent, type);

	switch (type) {
	case FYNT_SCALAR:
		fy_emit_node_scalar(emit, fyn, flags, indent, is_key);
		break;
	case FYNT_SEQUENCE:
	case FYNT_MAPPING:
		FYD_TOKEN_ERROR_CHECK(fyn->fyd, fyt_value, FYEM_INTERNAL,
				!is_key || !fy_emit_is_json_mode(emit), err_out,
				"JSON does not allow %s as keys",
					type == FYNT_SEQUENCE ? "sequences" : "mappings");
		if (type == FYNT_SEQUENCE)
			fy_emit_node_sequence(emit, fyn, flags, indent);
		else
			fy_emit_node_mapping(emit, fyn, flags, indent);
		break;
	}
err_out:
	/* nothing */
	return;
}

struct fy_emit_write_state {
	enum fy_node_style style;
	enum fy_lb_mode lb_mode;
	const struct fy_text_analysis *ta;
	void *user;
	const struct fy_emit_write_ops *ops;
};

struct fy_emit_write_ops {
	const char *(*get_direct)(struct fy_emitter *emit, struct fy_emit_write_state *state, size_t *lenp);
	void (*iter_start)(struct fy_emitter *emit, struct fy_emit_write_state *state);
	void (*iter_end)(struct fy_emitter *emit, struct fy_emit_write_state *state);
	int (*iter_getc)(struct fy_emitter *emit, struct fy_emit_write_state *state);
	int (*iter_peekc)(struct fy_emitter *emit, struct fy_emit_write_state *state);
	int (*iter_getc_dq)(struct fy_emitter *emit, struct fy_emit_write_state *state, uint8_t *buf, size_t *lenp);
};

/* synthetics; not generated by token analysis */
#define FYTTAF_X_NULL_SCALAR		FYTTAF_BIT(FYTTAF_USER_BIT_START + 0)
#define FYTTAF_X_JSON_PLAIN		FYTTAF_BIT(FYTTAF_USER_BIT_START + 1)
#define FYTTAF_X_SIZE0			FYTTAF_BIT(FYTTAF_USER_BIT_START + 2)
#define FYTTAF_X_TAG_DQ			FYTTAF_BIT(FYTTAF_USER_BIT_START + 3)
#define FYTTAF_X_OVER_MIN_LITERAL_LBS	FYTTAF_BIT(FYTTAF_USER_BIT_START + 4)
#define FYTTAF_X_EXPLICIT_STRIP_CHOMP	FYTTAF_BIT(FYTTAF_USER_BIT_START + 5)
#define FYTTAF_X_EXPLICIT_KEEP_CHOMP	FYTTAF_BIT(FYTTAF_USER_BIT_START + 6)

void fy_emit_write_plain_with_state(struct fy_emitter *emit, int flags, int indent,
				    struct fy_emit_write_state *state)
{
	const struct fy_emit_write_ops *ops = state->ops;
	bool allow_breaks, spaces, breaks, should_indent;
	int c;
	const char *str;
	size_t len;
	enum fy_emitter_write_type wtype;

	wtype = (flags & DDNF_SIMPLE_SCALAR_KEY) ?
			fyewt_plain_scalar_key :
			fyewt_plain_scalar;
	allow_breaks = !(flags & DDNF_SIMPLE) &&
			!fy_emit_is_json_mode(emit) &&
			!fy_emit_is_oneline(emit);

	/* very very simple case first (90% of cases) */
	str = ops->get_direct(emit, state, &len);
	if (str && (!allow_breaks || (fy_emit_accum_column(&emit->ea) + (int)len <= fy_emit_width(emit)))) {
		fy_emit_write_simple(emit, wtype, str, len);
		goto out;
	}

	spaces = false;
	breaks = false;

	ops->iter_start(emit, state);

	fy_emit_accum_start(&emit->ea, emit->column, state->lb_mode);
	while ((c = ops->iter_getc(emit, state)) > 0) {

		if (fy_is_ws(c)) {

			should_indent = allow_breaks && !spaces &&
					fy_emit_accum_column(&emit->ea) > fy_emit_width(emit);

			if (should_indent && !fy_is_ws(ops->iter_peekc(emit, state))) {
				fy_emit_output_accum(emit, wtype, &emit->ea);
				emit->flags &= ~FYEF_INDENTATION;
				fy_emit_write_indent(emit, indent);
				fy_emit_output_col_sync(emit, &emit->ea);
			} else
				fy_emit_accum_utf8_put(&emit->ea, c);

			spaces = true;

		} else if (fy_is_lb_m(c, emit->ea.lb_mode)) {

			/* blergh */
			if (!allow_breaks)
				break;

			/* output run */
			if (!breaks) {
				fy_emit_output_accum(emit, wtype, &emit->ea);
				fy_emit_write_indent(emit, indent);
				fy_emit_output_col_sync(emit, &emit->ea);
			}

			emit->flags &= ~FYEF_INDENTATION;
			fy_emit_write_indent(emit, indent);
			fy_emit_output_col_sync(emit, &emit->ea);

			breaks = true;

		} else {

			if (breaks) {
				fy_emit_write_indent(emit, indent);
				fy_emit_output_col_sync(emit, &emit->ea);
			}

			fy_emit_accum_utf8_put(&emit->ea, c);

			emit->flags &= ~FYEF_INDENTATION;

			spaces = false;
			breaks = false;
		}
	}
	fy_emit_output_accum(emit, wtype, &emit->ea);
	fy_emit_accum_finish(&emit->ea);

	ops->iter_end(emit, state);
out:
	emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION);
}

void fy_emit_write_alias_with_state(struct fy_emitter *emit, int flags, int indent,
				    struct fy_emit_write_state *state)
{
	const struct fy_emit_write_ops *ops = state->ops;
	const char *str = NULL;
	size_t len = 0;
	int c;

	fy_emit_write_indicator(emit, di_star, flags, indent, fyewt_alias);

	/* try direct output first (99% of cases) */
	str = ops->get_direct(emit, state, &len);
	if (str) {
		fy_emit_write(emit, fyewt_alias, str, len);
		goto out;
	}

	/* corner case, use iterator */
	ops->iter_start(emit, state);

	fy_emit_accum_start(&emit->ea, emit->column, state->lb_mode);
	while ((c = ops->iter_getc(emit, state)) > 0)
		fy_emit_accum_utf8_put(&emit->ea, c);
	fy_emit_output_accum(emit, fyewt_alias, &emit->ea);
	fy_emit_accum_finish(&emit->ea);

	ops->iter_end(emit, state);

out:
	/* make sure a whitespace is generate */
	emit->flags &= ~FYEF_INDENTATION;
	emit->flags |= FYEF_NEED_WS_BEFORE_IND;
}

/* only use FYTTAF_DIRECT_OUTPUT && maxcol */
void fy_emit_write_single_quoted_with_state(struct fy_emitter *emit, int flags, int indent,
				     struct fy_emit_write_state *state)
{
	const struct fy_emit_write_ops *ops = state->ops;
	const struct fy_text_analysis *ta = state->ta;
	bool allow_breaks, spaces, breaks;
	int c;
	enum fy_emitter_write_type wtype;
	const char *str = NULL;
	size_t len = 0;
	bool should_indent;
	int emit_width;

	assert(ta);

	wtype = (flags & DDNF_SIMPLE_SCALAR_KEY) ?
		fyewt_single_quoted_scalar_key : fyewt_single_quoted_scalar;

	fy_emit_write_indicator(emit, di_single_quote_start, flags, indent, wtype);

	/* note that if the original target style and the target differ
	 * we can note use direct output
	 */
	allow_breaks = !(flags & DDNF_SIMPLE) && !fy_emit_is_json_mode(emit) && !fy_emit_is_oneline(emit);
	emit_width = fy_emit_width(emit);

	/* simple case of direct output (large amount of cases) */
	str = ops->get_direct(emit, state, &len);
	if (str && (ta->flags & FYTTAF_DIRECT_OUTPUT) && emit->column + ta->maxcol < emit_width) {
		fy_emit_write(emit, wtype, str, len);
		goto out;
	}

	spaces = false;
	breaks = false;

	ops->iter_start(emit, state);

	fy_emit_accum_start(&emit->ea, emit->column, state->lb_mode);
	for (;;) {
		c = ops->iter_getc(emit, state);
		if (c <= 0)
			break;

		if (fy_is_ws(c)) {
			should_indent = allow_breaks && !spaces &&
					fy_emit_accum_column(&emit->ea) >= emit_width;

			if (should_indent && fy_is_ws(ops->iter_peekc(emit, state))) {
				fy_emit_output_accum(emit, wtype, &emit->ea);

				emit->flags &= ~FYEF_INDENTATION;
				fy_emit_write_indent(emit, indent);
				fy_emit_output_col_sync(emit, &emit->ea);
			} else
				fy_emit_accum_utf8_put(&emit->ea, c);

			spaces = true;
			breaks = false;

		} else if (fy_is_lb_m(c, state->lb_mode)) {

			/* blergh */
			if (!allow_breaks)
				break;

			/* output run */
			if (!breaks) {
				fy_emit_output_accum(emit, wtype, &emit->ea);
				fy_emit_write_indent(emit, indent);
				fy_emit_output_col_sync(emit, &emit->ea);
			}

			emit->flags &= ~FYEF_INDENTATION;
			fy_emit_write_indent(emit, indent);
			fy_emit_output_col_sync(emit, &emit->ea);

			breaks = true;
		} else {
			/* output run */
			if (breaks) {
				fy_emit_output_accum(emit, wtype, &emit->ea);
				fy_emit_write_indent(emit, indent);
				fy_emit_output_col_sync(emit, &emit->ea);

			} else if (allow_breaks && fy_emit_accum_column(&emit->ea) >= emit_width) {

				fy_emit_output_accum(emit, wtype, &emit->ea);

				fy_emit_putc_simple(emit, wtype, '\\');

				emit->flags &= ~FYEF_INDENTATION;
				fy_emit_write_indent(emit, indent);
				fy_emit_output_col_sync(emit, &emit->ea);
			}

			/* escape ' */
			if (c == '\'')
				fy_emit_accum_utf8_put(&emit->ea, '\'');
			fy_emit_accum_utf8_put(&emit->ea, c);

			emit->flags &= ~FYEF_INDENTATION;
			spaces = false;
			breaks = false;
		}
	}
	fy_emit_output_accum(emit, wtype, &emit->ea);
	fy_emit_accum_finish(&emit->ea);

	ops->iter_end(emit, state);

out:
	fy_emit_write_indicator(emit, di_single_quote_end, flags, indent, wtype);

	emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION);
}

/* only use FYTTAF_DIRECT_OUTPUT && maxcol */
void fy_emit_write_double_quoted_with_state(struct fy_emitter *emit, int flags, int indent,
					    struct fy_emit_write_state *state)
{
	const struct fy_emit_write_ops *ops = state->ops;
	const struct fy_text_analysis *ta = state->ta;
	bool allow_breaks, spaces, breaks;
	int c, i, w, digit;
	enum fy_emitter_write_type wtype;
	const char *str = NULL;
	size_t len = 0;
	bool should_indent, done_esc;
	uint32_t hi_surrogate, lo_surrogate;
	uint8_t non_utf8[4];
	size_t non_utf8_len, k;
	int emit_width;

	assert(ta);

	wtype = (flags & DDNF_SIMPLE_SCALAR_KEY) ?
		fyewt_double_quoted_scalar_key : fyewt_double_quoted_scalar;

	fy_emit_write_indicator(emit, di_double_quote_start, flags, indent, wtype);

	/* note that if the original target style and the target differ
	 * we can note use direct output
	 */
	allow_breaks = !(flags & DDNF_SIMPLE) && !fy_emit_is_json_mode(emit) && !fy_emit_is_oneline(emit);
	emit_width = fy_emit_width(emit);

	/* simple case of direct output (large amount of cases) */
	str = ops->get_direct(emit, state, &len);
	if (str && (ta->flags & FYTTAF_DIRECT_OUTPUT) && emit->column + ta->maxcol < emit_width) {
		fy_emit_write(emit, wtype, str, len);
		goto out;
	}

	spaces = false;
	breaks = false;

	ops->iter_start(emit, state);

	fy_emit_accum_start(&emit->ea, emit->column, state->lb_mode);
	for (;;) {
		non_utf8_len = sizeof(non_utf8);
		c = ops->iter_getc_dq(emit, state, non_utf8, &non_utf8_len);
		if (c < 0)
			break;

		if (c == 0 && non_utf8_len > 0) {

			for (k = 0; k < non_utf8_len; k++) {
				c = (int)non_utf8[k] & 0xff;
				fy_emit_accum_utf8_put(&emit->ea, '\\');
				if (c != 0) {
					fy_emit_accum_utf8_put(&emit->ea, 'x');
					digit = ((unsigned int)c >> 4) & 15;
					fy_emit_accum_utf8_put(&emit->ea,
							digit <= 9 ? ('0' + digit) : ('A' + digit - 10));
					digit = (unsigned int)c & 15;
					fy_emit_accum_utf8_put(&emit->ea,
							digit <= 9 ? ('0' + digit) : ('A' + digit - 10));
				} else
					fy_emit_accum_utf8_put(&emit->ea, '0');
			}
			continue;
		}

		if (fy_is_space(c)) {
			should_indent = allow_breaks && !spaces &&
					fy_emit_accum_column(&emit->ea) >= emit_width;

			if (should_indent) {
				fy_emit_output_accum(emit, wtype, &emit->ea);

				if (fy_is_ws(ops->iter_peekc(emit, state)))
					fy_emit_putc_simple(emit, wtype, '\\');

				emit->flags &= ~FYEF_INDENTATION;
				fy_emit_write_indent(emit, indent);
				fy_emit_output_col_sync(emit, &emit->ea);
			} else
				fy_emit_accum_utf8_put(&emit->ea, c);

			spaces = true;
			breaks = false;

		} else {
			/* output run */
			if (breaks) {
				fy_emit_output_accum(emit, wtype, &emit->ea);
				fy_emit_write_indent(emit, indent);
				fy_emit_output_col_sync(emit, &emit->ea);

			} else if (allow_breaks && fy_emit_accum_column(&emit->ea) >= emit_width) {

				fy_emit_output_accum(emit, wtype, &emit->ea);

				fy_emit_putc_simple(emit, wtype, '\\');

				emit->flags &= ~FYEF_INDENTATION;
				fy_emit_write_indent(emit, indent);
				fy_emit_output_col_sync(emit, &emit->ea);
			}

			/* escape */
			if ((!fy_is_printq(c) || c == '"' || c == '\\') ||
			    (fy_emit_is_json_mode(emit) && !fy_is_json_unescaped(c))) {

				fy_emit_accum_utf8_put(&emit->ea, '\\');

				/* common YAML and JSON escapes */
				done_esc = false;
				switch (c) {
				case '\b':
					fy_emit_accum_utf8_put(&emit->ea, 'b');
					done_esc = true;
					break;
				case '\f':
					fy_emit_accum_utf8_put(&emit->ea, 'f');
					done_esc = true;
					break;
				case '\n':
					fy_emit_accum_utf8_put(&emit->ea, 'n');
					done_esc = true;
					break;
				case '\r':
					fy_emit_accum_utf8_put(&emit->ea, 'r');
					done_esc = true;
					break;
				case '\t':
					fy_emit_accum_utf8_put(&emit->ea, 't');
					done_esc = true;
					break;
				case '"':
					fy_emit_accum_utf8_put(&emit->ea, '"');
					done_esc = true;
					break;
				case '\\':
					fy_emit_accum_utf8_put(&emit->ea, '\\');
					done_esc = true;
					break;
				}

				if (done_esc)
					goto done;

				if (!fy_emit_is_json_mode(emit)) {
					switch (c) {
					case '\0':
						fy_emit_accum_utf8_put(&emit->ea, '0');
						break;
					case '\a':
						fy_emit_accum_utf8_put(&emit->ea, 'a');
						break;
					case '\v':
						fy_emit_accum_utf8_put(&emit->ea, 'v');
						break;
					case '\x1b':	// \e
						fy_emit_accum_utf8_put(&emit->ea, 'e');
						break;
					case 0x85:
						fy_emit_accum_utf8_put(&emit->ea, 'N');
						break;
					case 0xa0:
						fy_emit_accum_utf8_put(&emit->ea, '_');
						break;
					case 0x2028:
						fy_emit_accum_utf8_put(&emit->ea, 'L');
						break;
					case 0x2029:
						fy_emit_accum_utf8_put(&emit->ea, 'P');
						break;
					default:
						if ((unsigned int)c <= 0xff) {
							fy_emit_accum_utf8_put(&emit->ea, 'x');
							w = 2;
						} else if ((unsigned int)c <= 0xffff) {
							fy_emit_accum_utf8_put(&emit->ea, 'u');
							w = 4;
						} else if ((unsigned int)c <= 0xffffffff) {
							fy_emit_accum_utf8_put(&emit->ea, 'U');
							w = 8;
						}

						for (i = w - 1; i >= 0; i--) {
							digit = ((unsigned int)c >> (i * 4)) & 15;
							fy_emit_accum_utf8_put(&emit->ea,
									digit <= 9 ? ('0' + digit) : ('A' + digit - 10));
						}
						break;
					}

				} else {
					/* JSON escapes all others in \uXXXX and \uXXXX\uXXXX */
					w = 4;
					if ((unsigned int)c <= 0xffff) {
						fy_emit_accum_utf8_put(&emit->ea, 'u');
						for (i = w - 1; i >= 0; i--) {
							digit = ((unsigned int)c >> (i * 4)) & 15;
							fy_emit_accum_utf8_put(&emit->ea,
									digit <= 9 ? ('0' + digit) : ('A' + digit - 10));
						}
					} else {
						hi_surrogate = 0xd800 | ((((c >> 16) & 0x1f) - 1) << 6) | ((c >> 10) & 0x3f);
						lo_surrogate = 0xdc00 | (c & 0x3ff);

						fy_emit_accum_utf8_put(&emit->ea, 'u');
						for (i = w - 1; i >= 0; i--) {
							digit = ((unsigned int)hi_surrogate >> (i * 4)) & 15;
							fy_emit_accum_utf8_put(&emit->ea,
									digit <= 9 ? ('0' + digit) : ('A' + digit - 10));
						}

						fy_emit_accum_utf8_put(&emit->ea, '\\');
						fy_emit_accum_utf8_put(&emit->ea, 'u');
						for (i = w - 1; i >= 0; i--) {
							digit = ((unsigned int)lo_surrogate >> (i * 4)) & 15;
							fy_emit_accum_utf8_put(&emit->ea,
									digit <= 9 ? ('0' + digit) : ('A' + digit - 10));
						}
					}
				}
			} else
				fy_emit_accum_utf8_put(&emit->ea, c);
done:
			emit->flags &= ~FYEF_INDENTATION;
			spaces = false;
			breaks = false;
		}
	}
	fy_emit_output_accum(emit, wtype, &emit->ea);
	fy_emit_accum_finish(&emit->ea);

	ops->iter_end(emit, state);

out:
	fy_emit_write_indicator(emit, di_double_quote_end, flags, indent, wtype);

	emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION);
}

bool fy_emit_write_block_hints_with_state(struct fy_emitter *emit, int flags FY_UNUSED, int indent FY_UNUSED,
					  struct fy_emit_write_state *state, char *chompp)
{
	const struct fy_text_analysis *ta = state->ta;
	bool explicit_chomp = false;
	char chomp;

	/* nothing? */
	if (!(ta->flags & FYTTAF_SIZE0)) {

		if (ta->flags & FYTTAF_NEEDS_EXPLICIT_CHOMP) {
			fy_emit_printf(emit, fyewt_indicator, "%d", fy_emit_indent(emit));
			explicit_chomp = true;
		}

		if (ta->flags & FYTTAF_X_EXPLICIT_STRIP_CHOMP)
			chomp = '-';
		else if (ta->flags & FYTTAF_X_EXPLICIT_KEEP_CHOMP)
			chomp = '+';
		else if (!(ta->flags & FYTTAF_HAS_END_LB))
			chomp = '-';
		else if (ta->flags & (FYTTAF_HAS_TRAILING_LB | FYTTAF_ALL_WS_LB))
			chomp = '+';
		else
			chomp = '\0';
	} else
		chomp = '-';	/* size0 */

	switch (chomp) {
	default:
	case '-':
		emit->flags &= ~FYEF_OPEN_ENDED;
		break;
	case '+':
		emit->flags |= FYEF_OPEN_ENDED;
		break;
	}
	if (chomp)
		fy_emit_putc_simple(emit, fyewt_indicator, chomp);
	*chompp = chomp;
	return explicit_chomp;
}

void fy_emit_write_literal_with_state(struct fy_emitter *emit, int flags, int indent,
				      struct fy_emit_write_state *state)
{
	const struct fy_emit_write_ops *ops = state->ops;
	bool breaks;
	int c;
	char chomp;

	fy_emit_write_indicator(emit, di_bar, flags, indent, fyewt_indicator);

	fy_emit_write_block_hints_with_state(emit, flags, indent, state, &chomp);

	if (flags & DDNF_ROOT)
		indent += fy_emit_indent(emit);

	fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
	emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;

	breaks = true;

	ops->iter_start(emit, state);

	fy_emit_accum_start(&emit->ea, emit->column, state->lb_mode);
	while ((c = ops->iter_getc(emit, state)) > 0) {

		if (fy_is_lb_m(c, state->lb_mode)) {
			fy_emit_output_accum(emit, fyewt_literal_scalar, &emit->ea);
			fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
			emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;
			breaks = true;
		} else {
			if (breaks) {
				fy_emit_write_indent(emit, indent);
				fy_emit_output_col_sync(emit, &emit->ea);
				breaks = false;
			}
			fy_emit_accum_utf8_put(&emit->ea, c);
		}
	}
	fy_emit_output_accum(emit, fyewt_literal_scalar, &emit->ea);
	fy_emit_accum_finish(&emit->ea);

	ops->iter_end(emit, state);
}

void fy_emit_write_folded_with_state(struct fy_emitter *emit, int flags, int indent,
				     struct fy_emit_write_state *state)
{
	const struct fy_emit_write_ops *ops = state->ops;
	bool leading_spaces, breaks;
	int c, nrbreaks, nrbreakslim;
	char chomp;

	fy_emit_write_indicator(emit, di_greater, flags, indent, fyewt_indicator);

	fy_emit_write_block_hints_with_state(emit, flags, indent, state, &chomp);
	if (flags & DDNF_ROOT)
		indent += fy_emit_indent(emit);

	fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
	emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;

	breaks = true;
	leading_spaces = true;

	ops->iter_start(emit, state);

	fy_emit_accum_start(&emit->ea, emit->column, state->lb_mode);
	while ((c = ops->iter_getc(emit, state)) > 0) {

		if (fy_is_lb_m(c, state->lb_mode)) {

			/* output run */
			if (!fy_emit_accum_empty(&emit->ea)) {
				fy_emit_output_accum(emit, fyewt_literal_scalar, &emit->ea);
				/* do not output a newline (indent) if at the end or
				 * this is a leading spaces line */
				if (!fy_is_z(ops->iter_peekc(emit, state)) && !leading_spaces) {
					fy_emit_write_indent(emit, indent);
					fy_emit_output_col_sync(emit, &emit->ea);
				}
			}

			/* count the number of consecutive breaks */
			nrbreaks = 1;
			while (fy_is_lb_m((c = ops->iter_peekc(emit, state)), state->lb_mode)) {
				nrbreaks++;
				(void)ops->iter_getc(emit, state);
			}

			/* NOTE: Because the number of indents is tricky
			 * if it's a non blank, non end, it's the number of breaks
			 * if it's a blank, it's the number of breaks minus 1
			 * if it's the end, it's the number of breaks minus 2
			 */
			nrbreakslim = fy_is_z(c) ? 2 : fy_is_blank(c) ? 1 : 0;
			while (nrbreaks-- > nrbreakslim) {
				emit->flags &= ~FYEF_INDENTATION;
				fy_emit_write_indent(emit, indent);
				fy_emit_output_col_sync(emit, &emit->ea);
			}

			breaks = true;

		} else {

			/* if we had a break, output an indent */
			if (breaks) {
				fy_emit_write_indent(emit, indent);
				fy_emit_output_col_sync(emit, &emit->ea);

				/* if this line starts with whitespace we need to know */
				leading_spaces = fy_is_ws(c);
			}

			if (!breaks && fy_is_space(c) &&
			    !fy_is_space(ops->iter_peekc(emit, state)) &&
			    fy_emit_accum_column(&emit->ea) > fy_emit_width(emit)) {
				fy_emit_output_accum(emit, fyewt_folded_scalar, &emit->ea);
				emit->flags &= ~FYEF_INDENTATION;
				fy_emit_write_indent(emit, indent);
				fy_emit_output_col_sync(emit, &emit->ea);
			} else
				fy_emit_accum_utf8_put(&emit->ea, c);

			breaks = false;
		}
	}
	fy_emit_output_accum(emit, fyewt_folded_scalar, &emit->ea);
	fy_emit_accum_finish(&emit->ea);

	ops->iter_end(emit, state);
}

/* token */

/* for token */
struct token_state {
	struct fy_token *fyt;
	struct fy_atom_iter iter;
};

/* simple run of characters, fast and 90% of the plain cases */
static const char *
token_get_direct(struct fy_emitter *emit, struct fy_emit_write_state *state, size_t *lenp)
{
	struct token_state *tstate = state->user;
	struct fy_token *fyt = tstate->fyt;

	switch (state->style) {
	case FYNS_PLAIN:
		/* JSON null */
		if (fy_emit_is_json_mode(emit) && (!fyt || fyt->scalar.is_null)) {
			*lenp = 4;
			return "null";
		}

		/* null - output nothing for YAML */
		if (!fyt) {
			*lenp = 0;
			return "";
		}

		return fy_token_get_direct_simple_output(fyt, lenp);

	case FYNS_SINGLE_QUOTED:
	case FYNS_DOUBLE_QUOTED:
		/* null - output nothing for YAML */
		if (!fyt) {
			*lenp = 0;
			return "";
		}

		return fy_token_get_direct_output(fyt, lenp);

	default:
		break;
	}

	*lenp = 0;
	return NULL;
}

static void
token_iter_start(struct fy_emitter *emit FY_UNUSED, struct fy_emit_write_state *state)
{
	struct token_state *tstate = state->user;
	fy_atom_iter_start(fy_token_atom(tstate->fyt), &tstate->iter);
}

static void
token_iter_end(struct fy_emitter *emit FY_UNUSED, struct fy_emit_write_state *state)
{
	struct token_state *tstate = state->user;
	fy_atom_iter_finish(&tstate->iter);
}

static int
token_iter_getc(struct fy_emitter *emit FY_UNUSED, struct fy_emit_write_state *state)
{
	struct token_state *tstate = state->user;
	return fy_atom_iter_utf8_get(&tstate->iter);
}

static int
token_iter_peekc(struct fy_emitter *emit FY_UNUSED, struct fy_emit_write_state *state)
{
	struct token_state *tstate = state->user;
	return fy_atom_iter_utf8_peek(&tstate->iter);
}

static int token_iter_getc_dq(struct fy_emitter *emit FY_UNUSED, struct fy_emit_write_state *state, uint8_t *buf, size_t *lenp)
{
	struct token_state *tstate = state->user;
	return fy_atom_iter_utf8_quoted_get(&tstate->iter, lenp, buf);
}

static const struct fy_emit_write_ops token_ops = {
	.get_direct = token_get_direct,
	.iter_start = token_iter_start,
	.iter_end = token_iter_end,
	.iter_getc = token_iter_getc,
	.iter_peekc = token_iter_peekc,
	.iter_getc_dq = token_iter_getc_dq,
};

void fy_emit_token_write_plain(struct fy_emitter *emit, struct fy_token *fyt, int flags, int indent)
{
	struct token_state tstate;
	struct fy_emit_write_state state;

	tstate.fyt = fyt;
	/* NOTE iter not initialized; it is on iter_start */

	state.style = FYNS_PLAIN;
	state.lb_mode = fy_token_atom_lb_mode(fyt);
	state.ta = NULL;
	state.user = &tstate;
	state.ops = &token_ops;

	fy_emit_write_plain_with_state(emit, flags, indent, &state);
}

void fy_emit_token_write_alias(struct fy_emitter *emit, struct fy_token *fyt, int flags, int indent)
{
	struct token_state tstate;
	struct fy_emit_write_state state;

	if (!fyt)
		return;

	tstate.fyt = fyt;
	/* NOTE iter not initialized; it is on iter_start */

	state.style = FYNS_ALIAS;
	state.lb_mode = fy_token_atom_lb_mode(fyt);
	state.ta = NULL;
	state.user = &tstate;
	state.ops = &token_ops;

	fy_emit_write_alias_with_state(emit, flags, indent, &state);
}

void fy_emit_token_write_single_quoted(struct fy_emitter *emit, struct fy_token *fyt, int flags, int indent)
{
	struct token_state tstate;
	struct fy_emit_write_state state;

	tstate.fyt = fyt;
	/* NOTE iter not initialized; it is on iter_start */

	state.style = FYNS_SINGLE_QUOTED;
	state.lb_mode = fy_token_atom_lb_mode(fyt);
	state.ta = fy_token_text_analyze(fyt);
	state.user = &tstate;
	state.ops = &token_ops;

	fy_emit_write_single_quoted_with_state(emit, flags, indent, &state);
}

void fy_emit_token_write_double_quoted(struct fy_emitter *emit, struct fy_token *fyt, int flags, int indent)
{
	struct token_state tstate;
	struct fy_emit_write_state state;

	tstate.fyt = fyt;
	/* NOTE iter not initialized; it is on iter_start */

	state.style = FYNS_DOUBLE_QUOTED;
	state.lb_mode = fy_token_atom_lb_mode(fyt);
	state.ta = fy_token_text_analyze(fyt);
	state.user = &tstate;
	state.ops = &token_ops;

	fy_emit_write_double_quoted_with_state(emit, flags, indent, &state);
}

static void
fy_emit_token_write_block_analysis(struct fy_token *fyt, struct fy_text_analysis *ta)
{
	struct fy_atom *atom;

	*ta = *fy_token_text_analyze(fyt);

	atom = fy_token_atom(fyt);
	if (atom && fy_atom_style_is_block(atom->style) && atom->chomp_explicit) {
		/* atom was parsed as a block scalar; trust the stored chomp */
		switch ((enum fy_atom_chomp)atom->chomp) {
		case FYAC_STRIP:
			ta->flags |= FYTTAF_X_EXPLICIT_STRIP_CHOMP;
			break;
		case FYAC_KEEP:
			ta->flags |= FYTTAF_X_EXPLICIT_KEEP_CHOMP;
			break;
		default:
			break;
		}
	}
}

void fy_emit_token_write_literal(struct fy_emitter *emit, struct fy_token *fyt, int flags, int indent)
{
	struct token_state tstate;
	struct fy_emit_write_state state;
	struct fy_text_analysis ta;

	tstate.fyt = fyt;
	/* NOTE iter not initialized; it is on iter_start */

	fy_emit_token_write_block_analysis(fyt, &ta);

	state.style = FYNS_LITERAL;
	state.lb_mode = fy_token_atom_lb_mode(fyt);
	state.ta = &ta;
	state.user = &tstate;
	state.ops = &token_ops;

	fy_emit_write_literal_with_state(emit, flags, indent, &state);
}

bool fy_emit_token_write_block_hints(struct fy_emitter *emit,
				     struct fy_token *fyt,
				     int flags FY_UNUSED,
				     int indent FY_UNUSED,
				     char *chompp)
{
	char chomp = '\0';
	bool explicit_chomp = false;
	struct fy_atom *atom;

	atom = fy_token_atom(fyt);
	if (!atom) {
		emit->flags &= ~FYEF_OPEN_ENDED;
		chomp = '-';
		goto out;
	}

	if (atom->starts_with_ws || atom->starts_with_lb) {
		fy_emit_printf(emit, fyewt_indicator, "%d", fy_emit_indent(emit));
		explicit_chomp = true;
	}

	if (fy_atom_style_is_block(atom->style) && atom->chomp_explicit) {
		/* atom was parsed as a block scalar; trust the stored chomp */
		switch ((enum fy_atom_chomp)atom->chomp) {
		case FYAC_STRIP:
			emit->flags &= ~FYEF_OPEN_ENDED;
			chomp = '-';
			break;
		case FYAC_KEEP:
			emit->flags |= FYEF_OPEN_ENDED;
			chomp = '+';
			break;
		case FYAC_CLIP:
		default:
			emit->flags &= ~FYEF_OPEN_ENDED;
			break;
		}
	} else {
		/* atom was not a block scalar; derive chomp from content */
		if (!atom->ends_with_lb) {
			emit->flags &= ~FYEF_OPEN_ENDED;
			chomp = '-';
		} else if (atom->trailing_lb) {
			emit->flags |= FYEF_OPEN_ENDED;
			chomp = '+';
		} else {
			emit->flags &= ~FYEF_OPEN_ENDED;
		}
	}

out:
	if (chomp)
		fy_emit_putc_simple(emit, fyewt_indicator, chomp);
	*chompp = chomp;
	return explicit_chomp;
}

static inline bool fy_emit_can_use_original_folded_breaks(struct fy_emitter *emit,
                                                          struct fy_atom *atom)
{
	return fy_emit_is_original_mode(emit) && atom->fyi &&
	       atom->chomp_explicit &&
	       (atom->style & ~FYAS_MANUAL_MARK) == FYAS_FOLDED;
}

/* a complete separate original folded implementation */
static void
fy_emit_token_write_folded_original(struct fy_emitter *emit, struct fy_token *fyt, int flags, int indent)
{
	struct fy_atom *atom;
	struct fy_atom_raw_line_iter rliter;
	const struct fy_raw_line *rl;
	char chomp;
	int w;
	int deferred_blanks = 0;

	atom = fy_token_atom(fyt);

	fy_emit_write_indicator(emit, di_greater, flags, indent, fyewt_indicator);

	fy_emit_token_write_block_hints(emit, fyt, flags, indent, &chomp);
	if (flags & DDNF_ROOT)
		indent += fy_emit_indent(emit);

	fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
	emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;

	fy_atom_raw_line_iter_start(atom, &rliter);
	fy_emit_accum_start(&emit->ea, emit->column, fy_token_atom_lb_mode(fyt));

	const unsigned int orig_indent = atom->increment;

	while ((rl = fy_atom_raw_line_iter_next(&rliter)) != NULL) {

		/* Partial final fragment: parser lookahead consumed indent bytes of the
		 * next element; no trailing linebreak means this is not scalar content. */
		if (rl->line_len_lb == rl->line_len)
			break;

		/* Compute content after stripping base indentation */
		const char *s = rl->content_start;
		const char *e = s + rl->content_len;
		{
			unsigned int skip = orig_indent;
			while (skip > 0 && s < e &&
			       (!atom->tabsize ? (*s == ' ') : fy_utf8_is_ws((unsigned char)*s))) {
				s++;
				skip--;
			}
		}

		if (s >= e) {
			/* blank line: defer emission */
			deferred_blanks++;
			continue;
		}

		/* non-blank content line: flush any deferred blank lines first */
		while (deferred_blanks > 0) {
			fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
			emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;
			deferred_blanks--;
		}

		fy_emit_write_indent(emit, indent);
		fy_emit_output_col_sync(emit, &emit->ea);

		while (s < e) {
			const int c = fy_utf8_get(s, (size_t)(e - s), &w);
			if (c <= 0)
				break;
			fy_emit_accum_utf8_put(&emit->ea, c);
			s += w;
		}
		fy_emit_output_accum(emit, fyewt_folded_scalar, &emit->ea);
		fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
		emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;
	}

	/* trailing blank lines: only keep-chomp retains them */
	if (atom->chomp == FYAC_KEEP) {
		while (deferred_blanks > 0) {
			fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
			emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;
			deferred_blanks--;
		}
	}

	fy_emit_accum_finish(&emit->ea);
	fy_atom_raw_line_iter_finish(&rliter);
}

void fy_emit_token_write_folded(struct fy_emitter *emit, struct fy_token *fyt, int flags, int indent)
{
	struct token_state tstate;
	struct fy_emit_write_state state;
	struct fy_text_analysis ta;

	tstate.fyt = fyt;
	/* NOTE iter not initialized; it is on iter_start */

	fy_emit_token_write_block_analysis(fyt, &ta);

	state.style = FYNS_FOLDED;
	state.lb_mode = fy_token_atom_lb_mode(fyt);
	state.ta = &ta;
	state.user = &tstate;
	state.ops = &token_ops;

	fy_emit_write_folded_with_state(emit, flags, indent, &state);
}

static inline bool
fy_emit_token_is_null_scalar(struct fy_emitter *emit FY_UNUSED,
			     struct fy_token *fyt)
{
	return !fyt || (fyt->type == FYTT_SCALAR && fyt->scalar.is_null);
}

static inline bool
fy_emit_token_is_size0(struct fy_emitter *emit FY_UNUSED,
		       struct fy_token *fyt)
{
	return !fyt || fy_token_atom(fyt)->size0;
}

static inline bool
fy_emit_token_is_json_plain(struct fy_emitter *emit, struct fy_token *fyt)
{
	struct fy_atom *atom;

	/* must be one of these JSON modes */
	if (!(fy_emit_is_json_mode(emit) || emit->source_json || fy_emit_is_dejson_mode(emit)))
		return false;

	atom = fy_token_atom(fyt);

	return fy_emit_token_is_null_scalar(emit, fyt) ||
	       !fy_atom_strcmp(atom, "false") ||
	       !fy_atom_strcmp(atom, "true") ||
	       !fy_atom_strcmp(atom, "null") ||
	       fy_atom_is_number(atom);
}

static inline bool
fy_emit_tag_text_is_double_quoted(struct fy_emitter *emit FY_UNUSED,
				  const char *tag,
				  size_t tag_len)
{
	/* XXX hardcoded string tag resultion */
	return tag && tag_len &&
	       ((tag_len == 1 && *tag == '!') ||
	        (tag_len == 21 && !memcmp(tag, "tag:yaml.org,2002:str", 21)));
}

static inline bool
fy_emit_tag_token_is_double_quoted(struct fy_emitter *emit FY_UNUSED,
				   struct fy_token *fyt_tag)
{
	const char *tag;
	size_t tag_len;

	if (!fyt_tag)
		return false;

	tag = fy_token_get_text(fyt_tag, &tag_len);

	return fy_emit_tag_text_is_double_quoted(emit, tag, tag_len);
}

#ifndef FY_EMIT_MIN_LITERAL_LBS
#define FY_EMIT_MIN_LITERAL_LBS 5
#endif

static enum fy_node_style
fy_emit_scalar_style_from_analysis(struct fy_emitter *emit, int flags, int indent,
				   enum fy_node_style style,
				   const struct fy_text_analysis *ta)
{
	bool json, flow;
	uint64_t ta_flags;

	ta_flags = ta->flags;

	json = fy_emit_is_json_mode(emit);
	flow = fy_emit_is_flow_mode(emit) || (flags & DDNF_FLOW);

	/* check if style is allowed (i.e. no block styles in flow context) */
	if (flow && (style == FYNS_LITERAL || style == FYNS_FOLDED))
		style = FYNS_ANY;

	if (json) {
		/* literal in JSON mode is output as quoted */
		if (style == FYNS_LITERAL || style == FYNS_FOLDED)
			return FYNS_DOUBLE_QUOTED;

		if (ta_flags & FYTTAF_X_TAG_DQ)
			return FYNS_DOUBLE_QUOTED;

		/* JSON NULL, but with plain style */
		if ((style == FYNS_PLAIN || style == FYNS_ANY) &&
		    ((ta_flags & FYTTAF_X_NULL_SCALAR) ||
		     ((ta_flags & FYTTAF_X_JSON_PLAIN) && !(ta_flags & FYTTAF_SIZE0))))
			return FYNS_PLAIN;

		return FYNS_DOUBLE_QUOTED;
	}

	/* if the style is block and we're a simple scalar key, this is not going to work */
	if ((style == FYNS_LITERAL || style == FYNS_FOLDED) && (flags & DDNF_SIMPLE_SCALAR_KEY))
		style = (ta_flags & FYTTAF_CAN_BE_PLAIN) ? FYNS_PLAIN : FYNS_DOUBLE_QUOTED;

	if (flow && (style == FYNS_ANY || style == FYNS_LITERAL || style == FYNS_FOLDED)) {

		/* if there's a linebreak, or ends in a colon, use double quoted style */
		if (ta_flags & (FYTTAF_HAS_ANY_LB | FYTTAF_ENDS_WITH_COLON)) {
			style = FYNS_DOUBLE_QUOTED;
			goto out;
		}

		/* anything not empty is double quoted here */
		style = (ta_flags & FYTTAF_EMPTY) ? FYNS_PLAIN :
			((ta_flags & FYTTAF_CAN_BE_PLAIN) ? FYNS_PLAIN : FYNS_DOUBLE_QUOTED);
	}

	/* try to pretify */
	if (!flow && fy_emit_is_pretty_mode(emit) &&
		(style == FYNS_ANY || style == FYNS_DOUBLE_QUOTED || style == FYNS_SINGLE_QUOTED)) {

		/* can we make it a folded or literal scalar? */
		if ((ta_flags & (FYTTAF_CAN_BE_FOLDED | FYTTAF_CAN_BE_LITERAL)) &&
		    (ta_flags & FYTTAF_X_OVER_MIN_LITERAL_LBS)) {

			style = (emit->column + ta->maxcol < fy_emit_width(emit)) ?
					FYNS_LITERAL : FYNS_FOLDED;
			goto out;
		}

		/* any original style can be a plain, but contains linebreaks, do a literal */
		if ((ta_flags & (FYTTAF_CAN_BE_PLAIN | FYTTAF_HAS_LB)) == (FYTTAF_CAN_BE_PLAIN | FYTTAF_HAS_LB)) {
			style = FYNS_LITERAL;
			goto out;
		}

		/* any style, can be just a plain, just make it so */
		if ((ta_flags & (FYTTAF_CAN_BE_PLAIN | FYTTAF_HAS_LB)) == FYTTAF_CAN_BE_PLAIN) {
			style = FYNS_PLAIN;
			goto out;
		}
	}

	if (!flow && emit->source_json && fy_emit_is_dejson_mode(emit)) {
		if ((ta_flags & FYTTAF_X_JSON_PLAIN) || (ta_flags & (FYTTAF_CAN_BE_PLAIN | FYTTAF_HAS_LB)) == FYTTAF_CAN_BE_PLAIN) {
			style = FYNS_PLAIN;
			goto out;
		}
	}

out:
	/* any zero, non newline linebreak, or invalid UTF-8 -> double quoted */
	if (ta_flags & (FYTTAF_HAS_ZERO |
			FYTTAF_HAS_NON_NL_LB |
			FYTTAF_HAS_INVALID_UTF8 |
			FYTTAF_HAS_PARTIAL_UTF8))
		style = FYNS_DOUBLE_QUOTED;

	if (style == FYNS_ANY && (ta_flags & FYTTAF_CAN_BE_PLAIN)) {
		if (!flow || (ta_flags & FYTTAF_CAN_BE_PLAIN_FLOW))
			style = FYNS_PLAIN;
	}

	if (style == FYNS_PLAIN) {
		if (flow && !(ta_flags & FYTTAF_CAN_BE_PLAIN_FLOW))
			style = (ta_flags & FYTTAF_CAN_BE_SINGLE_QUOTED) ? FYNS_SINGLE_QUOTED : FYNS_DOUBLE_QUOTED;

		/* plains in flow mode not being able to be plains
		 * - plain in block mode that can't be plain in flow mode
		 * - special handling for plains on start of line
		 */
		if ((flow && !(ta_flags & FYTTAF_CAN_BE_PLAIN_FLOW) &&
			     !(ta_flags & FYTTAF_CAN_BE_SIMPLE_KEY) &&
			     !(ta_flags & FYTTAF_X_NULL_SCALAR)) ||
		    ((ta_flags & FYTTAF_QUOTE_AT_0) && indent == 0))
			style = FYNS_DOUBLE_QUOTED;

		/* make it double quoted, but only if not already over the width (and contains a space/lb) */
		if (style == FYNS_PLAIN && (ta_flags & (FYTTAF_HAS_LB | FYTTAF_HAS_WS)) &&
			emit->column < fy_emit_width(emit) && (emit->column + ta->maxspan) > fy_emit_width(emit))
			style = FYNS_DOUBLE_QUOTED;

		if (style == FYNS_PLAIN && !(ta_flags & FYTTAF_CAN_BE_PLAIN)) {
			style = FYNS_DOUBLE_QUOTED;
		}
	}

	if (style == FYNS_ANY && (ta_flags & FYTTAF_CAN_BE_SINGLE_QUOTED))
		style = FYNS_SINGLE_QUOTED;

	if (style == FYNS_SINGLE_QUOTED && !(ta_flags & FYTTAF_CAN_BE_SINGLE_QUOTED))
		style = FYNS_DOUBLE_QUOTED;

	if (style == FYNS_ANY && (ta_flags & FYTTAF_CAN_BE_DOUBLE_QUOTED))
		style = FYNS_DOUBLE_QUOTED;

	/* should never happen, but still */
	if (style == FYNS_ANY)
		style = FYNS_DOUBLE_QUOTED;

	return style;
}

static enum fy_node_style
fy_emit_token_scalar_style(struct fy_emitter *emit, struct fy_token *fyt,
			   int flags, int indent, enum fy_node_style style,
			   struct fy_token *fyt_tag)
{
	const struct fy_text_analysis *ta = NULL;
	struct fy_text_analysis ta_mod;

	ta = fy_token_text_analyze(fyt);
	ta_mod = *ta;

	if (fy_emit_token_is_null_scalar(emit, fyt))
		ta_mod.flags |= FYTTAF_X_NULL_SCALAR;
	if (fy_emit_token_is_json_plain(emit, fyt))
		ta_mod.flags |= FYTTAF_X_JSON_PLAIN;
	if ((ta_mod.flags & FYTTAF_X_JSON_PLAIN) && fy_emit_tag_token_is_double_quoted(emit, fyt_tag))
		ta_mod.flags |= FYTTAF_X_TAG_DQ;
	if (ta_mod.lbs >= FY_EMIT_MIN_LITERAL_LBS)
		ta_mod.flags |= FYTTAF_X_OVER_MIN_LITERAL_LBS;

	return fy_emit_scalar_style_from_analysis(emit, flags, indent, style, &ta_mod);
}

int fy_emit_token_scalar_prolog(struct fy_emitter *emit, struct fy_token *fyt, int flags, int indent)
{
	struct fy_emit_save_ctx *sc = &emit->s_sc;

	if (sc->flags & DDNF_HANGING_INDENT)
		fy_emit_write_indent(emit, sc->indent);

	/* root scalar with a comment needs special handling */
	if ((flags & DDNF_ROOT) && fy_emit_token_has_comment(emit, fyt, fycp_top))
		fy_emit_token_comment(emit, fyt, flags, indent, fycp_top);

	indent = fy_emit_increase_indent(emit, flags, indent);

	if (!fy_emit_whitespace(emit))
		fy_emit_write_ws(emit);

	return indent;
}

void fy_emit_token_scalar_epilog(struct fy_emitter *emit, struct fy_token *fyt, int flags, int indent)
{
	struct fy_emit_save_ctx *sc = &emit->s_sc;

	/* root scalar with a comment needs special handling */
	if ((flags & DDNF_ROOT) && fy_emit_token_has_comment(emit, fyt, fycp_right)) {
		fy_emit_token_comment(emit, fyt, flags, indent, fycp_right);
		sc->flags |= DDNF_HANGING_INDENT;
	}
}

void fy_emit_token_scalar(struct fy_emitter *emit, struct fy_token *fyt, int flags, int indent,
			  enum fy_node_style style, struct fy_token *fyt_tag)
{
	assert(style != FYNS_FLOW && style != FYNS_BLOCK);
	style = fy_emit_token_scalar_style(emit, fyt, flags, indent, style, fyt_tag);

	indent = fy_emit_token_scalar_prolog(emit, fyt, flags, indent);

	/* in original mode with a parsed-as-folded atom, preserve original line breaks;
	 * chomp_explicit distinguishes parsed block scalars from event-created atoms */
	if (style == FYNS_FOLDED && fy_emit_can_use_original_folded_breaks(emit, fy_token_atom(fyt))) {
		fy_emit_token_write_folded_original(emit, fyt, flags, indent);
		goto do_epilog;
	}

	switch (style) {
	case FYNS_ALIAS:
		fy_emit_token_write_alias(emit, fyt, flags, indent);
		break;
	case FYNS_PLAIN:
		fy_emit_token_write_plain(emit, fyt, flags, indent);
		break;
	case FYNS_DOUBLE_QUOTED:
		fy_emit_token_write_double_quoted(emit, fyt, flags, indent);
		break;
	case FYNS_SINGLE_QUOTED:
		fy_emit_token_write_single_quoted(emit, fyt, flags, indent);
		break;
	case FYNS_LITERAL:
		fy_emit_token_write_literal(emit, fyt, flags, indent);
		break;
	case FYNS_FOLDED:
		fy_emit_token_write_folded(emit, fyt, flags, indent);
		break;
	default:
		break;
	}

do_epilog:
	fy_emit_token_scalar_epilog(emit, fyt, flags, indent);
}

void fy_emit_node_scalar(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent, bool is_key)
{
	enum fy_node_style style;

	/* default style */
	style = fyn ? fyn->style : FYNS_ANY;

	/* all JSON keys are double quoted */
	if (fy_emit_is_json_mode(emit) && is_key)
		style = FYNS_DOUBLE_QUOTED;

	fy_emit_token_scalar(emit,
			fyn ? fyn->scalar : NULL,
			flags, indent,
			style, fyn->tag);
}

static void fy_emit_sequence_prolog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc)
{
	bool json = fy_emit_is_json_mode(emit);
	bool was_flow = sc->flow;

	sc->old_indent = sc->indent;
	if (!json) {
		if (fy_emit_is_manual(emit))
			sc->flow = (sc->xstyle == FYNS_BLOCK && was_flow) || sc->xstyle == FYNS_FLOW;
		else if (fy_emit_is_block_mode(emit))
			sc->flow = sc->empty;
		else
			sc->flow = fy_emit_is_flow_mode(emit) || emit->flow_level || sc->flow_token || sc->empty;
	} else
		sc->flow = true;

	if (sc->flow) {
		if (!emit->flow_level) {
			sc->indent = fy_emit_increase_indent(emit, sc->flags, sc->indent);
			sc->old_indent = sc->indent;
		}

		sc->flags = (sc->flags | DDNF_FLOW) | (sc->flags & ~DDNF_INDENTLESS);
		fy_emit_write_indicator(emit, di_left_bracket, sc->flags, sc->indent, fyewt_indicator);

		/* we need an indent afterward if not compact */
		if (!fy_emit_sc_oneline(emit, sc))
			sc->flags |= DDNF_HANGING_INDENT;
	} else {
		sc->flags = (sc->flags & ~DDNF_FLOW);
	}

	if (!fy_emit_sc_oneline(emit, sc)) {
		if (was_flow || (sc->flags & (DDNF_ROOT | DDNF_SEQ))
		    || ((sc->flags & DDNF_MAP)
			&& (emit->xcfg.xflags & FYEXCF_INDENTED_SEQ_IN_MAP)))
			sc->indent = fy_emit_increase_indent(emit, sc->flags, sc->indent);
	}

	sc->flags &= ~DDNF_ROOT;
}


static void fy_emit_token_sequence_prolog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					  struct fy_token *fyt)
{
	/* skip top comment if parent sequence_item_prolog already emitted it */
	if (!(sc->flags & DDNF_SEQ) && fy_emit_token_has_comment(emit, fyt, fycp_top)) {
		fy_emit_token_comment(emit, fyt, sc->flags, sc->indent, fycp_top);
		sc->flags |= DDNF_HANGING_INDENT;
	}

	fy_emit_sequence_prolog(emit, sc);
}

static void fy_emit_sequence_epilog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc)
{
	if (sc->flow && (sc->flags & DDNF_HANGING_INDENT) && !fy_emit_sc_oneline(emit, sc) && !sc->empty)
		fy_emit_write_indent(emit, sc->old_indent);

	if (sc->flow || fy_emit_is_json_mode(emit)) {
		fy_emit_write_indicator(emit, di_right_bracket, sc->flags, sc->old_indent, fyewt_indicator);
	}
}

static void fy_emit_token_sequence_epilog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					  struct fy_token *fyt)
{
	fy_emit_sequence_epilog(emit, sc);

	/* emit trailing comment attached to the block-end token;
	 * use old_indent (parent scope) so the trailing indent
	 * doesn't produce an extra blank line */
	if (fy_emit_token_has_comment(emit, fyt, fycp_top))
		fy_emit_token_comment(emit, fyt, sc->flags, sc->old_indent, fycp_top);

	/* emit right-comment attached to the closing bracket token */
	if (fy_emit_token_has_comment(emit, fyt, fycp_right))
		fy_emit_token_comment(emit, fyt, sc->flags, sc->indent, fycp_right);
}

static void fy_emit_sequence_item_prolog_before_comment(struct fy_emitter *emit, struct fy_emit_save_ctx *sc)
{
	sc->flags |= DDNF_SEQ;

	if (!fy_emit_sc_oneline(emit, sc) ||
	    ((fy_emit_is_compact(emit) || sc->flow) && emit->column >= fy_emit_width(emit)))
		fy_emit_write_indent(emit, sc->indent);
}

static void fy_emit_sequence_item_prolog_after_comment(struct fy_emitter *emit, struct fy_emit_save_ctx *sc)
{
	if (!sc->flow && !fy_emit_is_json_mode(emit))
		fy_emit_write_indicator(emit, di_dash, sc->flags, sc->indent, fyewt_indicator);
}

static void fy_emit_token_sequence_item_prolog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					       struct fy_token *fyt_value)
{
	fy_emit_sequence_item_prolog_before_comment(emit, sc);

	if (fy_emit_token_has_comment(emit, fyt_value, fycp_top)) {
		fy_emit_token_comment(emit, fyt_value, sc->flags, sc->indent, fycp_top);
		sc->flags |= DDNF_HANGING_INDENT;
	}

	fy_emit_sequence_item_prolog_after_comment(emit, sc);
}

static void fy_emit_sequence_item_epilog_before_comment(struct fy_emitter *emit, struct fy_emit_save_ctx *sc, bool last FY_UNUSED)
{
	if ((sc->flow || fy_emit_is_json_mode(emit)) && !last)
		fy_emit_write_indicator(emit, di_comma, sc->flags, sc->indent, fyewt_indicator);

}

static void fy_emit_sequence_item_epilog_after_comment(struct fy_emitter *emit, struct fy_emit_save_ctx *sc, bool last)
{
	bool needs_hanging_indent;

	needs_hanging_indent = sc->flow && !fy_emit_sc_oneline(emit, sc) && !sc->empty;

	if (last && needs_hanging_indent && (sc->flags & DDNF_HANGING_INDENT))
		fy_emit_write_indent(emit, sc->old_indent);
	else if (needs_hanging_indent)
		sc->flags |= DDNF_HANGING_INDENT;

	sc->flags &= ~DDNF_SEQ;
}

static void fy_emit_token_sequence_item_epilog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					       bool last, struct fy_token *fyt_value)
{
	fy_emit_sequence_item_epilog_before_comment(emit, sc, last);

	if (fy_emit_token_has_comment(emit, fyt_value, fycp_right)) {
		fy_emit_token_comment(emit, fyt_value, sc->flags, sc->indent, fycp_right);
		sc->flags |= DDNF_HANGING_INDENT;
	}

	fy_emit_sequence_item_epilog_after_comment(emit, sc, last);
}

void fy_emit_node_sequence(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent)
{
	struct fy_node *fyni, *fynin;
	struct fy_token *fyt_value;
	bool last;
	struct fy_emit_save_ctx sct, *sc = &sct;

	memset(sc, 0, sizeof(*sc));

	sc->flags = flags;
	sc->indent = indent;
	sc->empty = fy_node_list_empty(&fyn->sequence);
	sc->flow_token = fyn->style == FYNS_FLOW;
	sc->oneline_flow = fy_emit_node_oneline_flow(emit, fyn);
	sc->flow = !!(flags & DDNF_FLOW);
	sc->xstyle = fyn->style;
	sc->old_indent = sc->indent;

	fy_emit_token_sequence_prolog(emit, sc, fyn->sequence_start);

	for (fyni = fy_node_list_head(&fyn->sequence); fyni; fyni = fynin) {

		fynin = fy_node_next(&fyn->sequence, fyni);
		last = !fynin;
		fyt_value = fy_node_value_token(fyni);

		fy_emit_token_sequence_item_prolog(emit, sc, fyt_value);

		fy_emit_node_internal(emit, fyni, sc->flags & ~DDNF_ROOT, sc->indent, false);
		fy_emit_token_sequence_item_epilog(emit, sc, last, fyt_value);
	}

	fy_emit_token_sequence_epilog(emit, sc, fyn->sequence_end);
}

static void fy_emit_mapping_prolog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc)
{
	bool json = fy_emit_is_json_mode(emit);
	bool was_flow = sc->flow;

	sc->old_indent = sc->indent;
	if (!json) {
		if (fy_emit_is_manual(emit))
			sc->flow = (sc->xstyle == FYNS_BLOCK && was_flow) || sc->xstyle == FYNS_FLOW;
		else if (fy_emit_is_block_mode(emit))
			sc->flow = sc->empty;
		else
			sc->flow = fy_emit_is_flow_mode(emit) || emit->flow_level || sc->flow_token || sc->empty;
	} else
		sc->flow = true;

	if (sc->flow) {
		if (!emit->flow_level) {
			sc->indent = fy_emit_increase_indent(emit, sc->flags, sc->indent);
			sc->old_indent = sc->indent;
		}

		sc->flags = (sc->flags | DDNF_FLOW) | (sc->flags & ~DDNF_INDENTLESS);
		fy_emit_write_indicator(emit, di_left_brace, sc->flags, sc->indent, fyewt_indicator);

		/* we need an indent afterward if not compact */
		if (!fy_emit_sc_oneline(emit, sc))
			sc->flags |= DDNF_HANGING_INDENT;
	} else {
		sc->flags &= ~(DDNF_FLOW | DDNF_INDENTLESS);
	}

	if (!fy_emit_sc_oneline(emit, sc) && !sc->empty)
		sc->indent = fy_emit_increase_indent(emit, sc->flags, sc->indent);

	sc->flags &= ~DDNF_ROOT;
}

static void fy_emit_token_mapping_prolog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					 struct fy_token *fyt)
{
	/* skip top comment if parent sequence_item_prolog already emitted it */
	if (!(sc->flags & DDNF_SEQ) && fy_emit_token_has_comment(emit, fyt, fycp_top)) {
		fy_emit_token_comment(emit, fyt, sc->flags, sc->indent, fycp_top);
		sc->flags |= DDNF_HANGING_INDENT;
	}

	fy_emit_mapping_prolog(emit, sc);
}

static void fy_emit_mapping_epilog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc)
{
	if (sc->flow || fy_emit_is_json_mode(emit)) {
		if ((sc->flags & DDNF_HANGING_INDENT) && !fy_emit_sc_oneline(emit, sc) && !sc->empty)
			fy_emit_write_indent(emit, sc->old_indent);
		fy_emit_write_indicator(emit, di_right_brace, sc->flags, sc->old_indent, fyewt_indicator);
	}
}

static void fy_emit_token_mapping_epilog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					 struct fy_token *fyt)
{
	fy_emit_mapping_epilog(emit, sc);

	/* emit trailing comment attached to the block-end token;
	 * use old_indent (parent scope) so the trailing indent
	 * doesn't produce an extra blank line */
	if (fy_emit_token_has_comment(emit, fyt, fycp_top))
		fy_emit_token_comment(emit, fyt, sc->flags, sc->old_indent, fycp_top);

	/* emit right-comment attached to the closing brace token */
	if (fy_emit_token_has_comment(emit, fyt, fycp_right))
		fy_emit_token_comment(emit, fyt, sc->flags, sc->indent, fycp_right);

}

static void fy_emit_token_mapping_key_prolog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					     struct fy_token *fyt_key, bool simple_key)
{
	const struct fy_text_analysis *ta;
	bool do_indent, key_over, has_comment;

	sc->flags = DDNF_MAP | (sc->flags & DDNF_FLOW);

	if (!fy_emit_sc_oneline(emit, sc) ||
	    ((fy_emit_is_compact(emit) || sc->flow) && emit->column >= fy_emit_width(emit)))
		fy_emit_write_indent(emit, sc->indent);

	/* emit top comment on key token (interstitial mapping comment) */
	if (!sc->flow && fy_emit_token_has_comment(emit, fyt_key, fycp_top)) {
		fy_emit_token_comment(emit, fyt_key, sc->flags, sc->indent, fycp_top);
		sc->flags |= DDNF_HANGING_INDENT;
	}

	/* if we have a right comment then it's by a definition not a simple key */
	has_comment = fy_emit_token_has_comment(emit, fyt_key, fycp_right);
	if (simple_key /* && !has_comment */ ) {
		sc->flags |= DDNF_SIMPLE;
		if (fyt_key && fyt_key->type == FYTT_SCALAR)
			sc->flags |= DDNF_SIMPLE_SCALAR_KEY;
	} else {
		/* do not emit the ? in flow modes at all */
		if (fy_emit_is_flow_mode(emit))
			sc->flags |= DDNF_SIMPLE;
	}

	do_indent = false;
	if (!has_comment && !fy_emit_sc_oneline(emit, sc)) {
		if (fyt_key && fyt_key->type != FYTT_SCALAR)
			/* always indent on non scalar keys */
			key_over = true;
		else {
			/* for scalar keys, check if key + 2 + quotes go over */
			ta = fy_token_text_analyze(fyt_key);
			key_over = (emit->column + ta->maxspan + 2 + (!simple_key ? 2 : 0)) >= fy_emit_width(emit);
		}

		do_indent = key_over;
		if (!do_indent && !fy_emit_is_compact(emit))
			do_indent = !sc->flow || (sc->flags & DDNF_HANGING_INDENT);
	} else
		do_indent = false;

	if (do_indent)
		fy_emit_write_indent(emit, sc->indent);

	/* complex? */
	if (!(sc->flags & DDNF_SIMPLE)) {
		fy_emit_write_indicator(emit, di_question_mark, sc->flags, sc->indent, fyewt_indicator);
	}
}

static void fy_emit_token_mapping_key_epilog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					     struct fy_token *fyt_key)
{
	/* if the key is an alias, always output an extra whitespace */
	if (fyt_key && fyt_key->type == FYTT_ALIAS)
		fy_emit_write_ws(emit);

	sc->flags &= ~DDNF_MAP;

	if (!(sc->flags & DDNF_SIMPLE)) {

		if (fy_emit_token_has_comment(emit, fyt_key, fycp_right)) {
			fy_emit_token_comment(emit, fyt_key, sc->flags, sc->indent, fycp_right);
			sc->flags |= DDNF_HANGING_INDENT;
		}

		if (!emit->flow_level && !fy_emit_is_oneline_or_compact(emit))
			fy_emit_write_indent(emit, sc->indent);
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
	} else {
	}

	fy_emit_write_indicator(emit, di_colon, sc->flags, sc->indent, fyewt_indicator);

	sc->flags &= ~DDNF_HANGING_INDENT;
	if ((sc->flags & DDNF_SIMPLE) && fy_emit_token_has_comment(emit, fyt_key, fycp_right)) {
		fy_emit_token_comment(emit, fyt_key, sc->flags, sc->indent, fycp_right);
		sc->flags |= DDNF_HANGING_INDENT;
	}

	sc->flags = DDNF_MAP | (sc->flags & (DDNF_FLOW | DDNF_HANGING_INDENT));
}

static void fy_emit_token_mapping_value_prolog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					       struct fy_token *fyt_value)
{
	const struct fy_text_analysis *ta;
	bool value_over;

	if (sc->flags & DDNF_HANGING_INDENT)
		fy_emit_write_indent(emit, sc->indent);

	/* don't do anything for those cases */
	if (!sc->flow || fy_emit_sc_oneline(emit, sc) || !fyt_value || fyt_value->type != FYTT_SCALAR)
		return;

	ta = fy_token_text_analyze(fyt_value);
	value_over = (emit->column + ta->maxspan) >= fy_emit_width(emit);

	if (value_over) {
		sc->indent = fy_emit_increase_indent(emit, sc->flags, sc->indent);
		fy_emit_write_indent(emit, sc->indent);
	}
}

static void fy_emit_token_mapping_value_epilog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					       bool last, struct fy_token *fyt_value)
{
	bool needs_hanging_indent;

	if ((sc->flow || fy_emit_is_json_mode(emit)) && !last)
		fy_emit_write_indicator(emit, di_comma, sc->flags, sc->indent, fyewt_indicator);

	if (fy_emit_token_has_comment(emit, fyt_value, fycp_right)) {
		fy_emit_token_comment(emit, fyt_value, sc->flags, sc->indent, fycp_right);
		sc->flags |= DDNF_HANGING_INDENT;
	}

	needs_hanging_indent = sc->flow && !fy_emit_sc_oneline(emit, sc) && !sc->empty;

	if (last && needs_hanging_indent && (sc->flags & DDNF_HANGING_INDENT))
		fy_emit_write_indent(emit, sc->old_indent);
	else if (needs_hanging_indent)
		sc->flags |= DDNF_HANGING_INDENT;

	sc->flags &= ~DDNF_MAP;
}

void fy_emit_node_mapping(struct fy_emitter *emit, struct fy_node *fyn, int flags, int indent)
{
	struct fy_node_pair *fynp, *fynpn, **fynpp = NULL;
	struct fy_token *fyt_key, *fyt_value;
	bool last, simple_key, used_malloc = false;
	const struct fy_text_analysis *tak = NULL, *tav;
	int i, count;
	struct fy_emit_save_ctx sct, *sc = &sct;

	memset(sc, 0, sizeof(*sc));

	sc->flags = flags;
	sc->indent = indent;
	sc->empty = fy_node_pair_list_empty(&fyn->mapping);
	sc->flow_token = fyn->style == FYNS_FLOW;
	sc->oneline_flow = fy_emit_node_oneline_flow(emit, fyn);
	sc->flow = !!(flags & DDNF_FLOW);
	sc->xstyle = fyn->style;
	sc->old_indent = sc->indent;

	fy_emit_token_mapping_prolog(emit, sc, fyn->mapping_start);

	if (!(emit->xcfg.cfg.flags & (FYECF_SORT_KEYS | FYECF_STRIP_EMPTY_KV))) {
		fynp = fy_node_pair_list_head(&fyn->mapping);
		fynpp = NULL;
	} else {
		count = fy_node_mapping_item_count(fyn);

		/* heuristic, avoid allocation for small maps */
		if (count > 64) {
			fynpp = malloc((count + 1) * sizeof(*fynpp));
			fyd_error_check(fyn->fyd, fynpp, err_out,
					"malloc() failed");
			used_malloc = true;
		} else
			fynpp = alloca((count + 1) * sizeof(*fynpp));

		/* fill (removing empty KVs) */
		i = 0;
		for (fynp = fy_node_pair_list_head(&fyn->mapping); fynp;
				fynp = fy_node_pair_next(&fyn->mapping, fynp)) {

			/* strip key/value pair from the output if it's empty */
			if ((emit->xcfg.cfg.flags & FYECF_STRIP_EMPTY_KV) && fy_node_is_empty(fynp->value))
				continue;

			fynpp[i++] = fynp;
		}
		count = i;
		fynpp[count] = NULL;

		/* sort the keys */
		if (emit->xcfg.cfg.flags & FYECF_SORT_KEYS)
			fy_node_mapping_perform_sort(fyn, NULL, NULL, fynpp, count);

		i = 0;
		fynp = fynpp[i];
	}

	for (; fynp; fynp = fynpn) {

		if (!fynpp)
			fynpn = fy_node_pair_next(&fyn->mapping, fynp);
		else
			fynpn = fynpp[++i];

		last = !fynpn;
		fyt_key = fy_node_value_token(fynp->key);
		fyt_value = fy_node_value_token(fynp->value);

		FYD_NODE_ERROR_CHECK(fynp->fyd, fynp->key, FYEM_INTERNAL,
				!fy_emit_is_json_mode(emit) ||
					(fynp->key && fynp->key->type == FYNT_SCALAR),
					err_out, "Non scalar keys are not allowed in JSON emit mode");

		simple_key = false;
		if (fynp->key) {
			switch (fynp->key->type) {
			case FYNT_SCALAR:
				tak = fy_token_text_analyze(fynp->key->scalar);
				simple_key = fy_emit_is_json_mode(emit) ||
					     !!(tak->flags & FYTTAF_CAN_BE_SIMPLE_KEY);
				break;
			case FYNT_SEQUENCE:
				simple_key = fy_node_list_empty(&fynp->key->sequence);
				break;
			case FYNT_MAPPING:
				simple_key = fy_node_pair_list_empty(&fynp->key->mapping);
				break;
			}
		}

		/* special handling for compact mode simple key + scalar value*/
		if (fy_emit_is_compact(emit) && simple_key &&
		    fynp->value && fynp->value->type == FYNT_SCALAR && tak) {
			tav = fy_token_text_analyze(fynp->value->scalar);

			/* for plain key + value if we go over the width */
			if ((tav->flags & FYTTAF_CAN_BE_PLAIN) &&
			   (emit->column + tak->maxspan + 2 + tav->maxspan + 1) >= fy_emit_width(emit))
				fy_emit_write_indent(emit, sc->indent);
		}

		fy_emit_token_mapping_key_prolog(emit, sc, fyt_key, simple_key);
		if (fynp->key)
			fy_emit_node_internal(emit, fynp->key, (sc->flags & ~DDNF_ROOT), sc->indent, true);
		fy_emit_token_mapping_key_epilog(emit, sc, fyt_key);

		fy_emit_token_mapping_value_prolog(emit, sc, fyt_value);
		if (fynp->value)
			fy_emit_node_internal(emit, fynp->value, (sc->flags & ~DDNF_ROOT), sc->indent, false);
		fy_emit_token_mapping_value_epilog(emit, sc, last, fyt_value);
	}

	fy_emit_token_mapping_epilog(emit, sc, fyn->mapping_end);

err_out:
	if (fynpp && used_malloc)
		free(fynpp);
	return;
}

int fy_emit_common_document_start(struct fy_emitter *emit,
				  struct fy_document_state *fyds,
				  bool root_tag_or_anchor FY_UNUSED)
{
	struct fy_emit_save_ctx *sc = &emit->s_sc;
	struct fy_token *fyt_chk;
	const char *td_handle, *td_prefix;
	size_t td_handle_size, td_prefix_size;
	enum fy_emitter_cfg_flags flags = emit->xcfg.cfg.flags;
	enum fy_emitter_cfg_flags vd_flags = flags & FYECF_VERSION_DIR(FYECF_VERSION_DIR_MASK);
	enum fy_emitter_cfg_flags td_flags = flags & FYECF_TAG_DIR(FYECF_TAG_DIR_MASK);
	enum fy_emitter_cfg_flags dsm_flags = flags & FYECF_DOC_START_MARK(FYECF_DOC_START_MARK_MASK);
	bool vd, td, dsm;
	bool had_non_default_tag = false;
	bool strip_doc = false;

	if (!emit || !fyds || emit->fyds)
		return -1;

	strip_doc = !!(emit->xcfg.cfg.flags & FYECF_STRIP_DOC);

	emit->fyds = fy_document_state_ref(fyds);

	vd = ((vd_flags == FYECF_VERSION_DIR_AUTO && fyds->version_explicit) ||
	       vd_flags == FYECF_VERSION_DIR_ON) &&
	      !strip_doc;
	td = ((td_flags == FYECF_TAG_DIR_AUTO && fyds->tags_explicit) ||
	       td_flags == FYECF_TAG_DIR_ON) &&
	      !strip_doc;

	/* if either a version or directive tags exist, and no previous
	 * explicit document end existed, output one now
	 */
	if (!fy_emit_is_json_mode(emit) && (vd || td) &&
		(emit->flags & (FYEF_HAD_DOCUMENT_END | FYEF_HAD_DOCUMENT_OUTPUT)) ==
			(FYEF_HAD_DOCUMENT_END | FYEF_HAD_DOCUMENT_OUTPUT)) {
		if (emit->column)
			fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
		if (!strip_doc) {
			fy_emit_puts_simple(emit, fyewt_document_indicator, "...");
			emit->flags &= ~FYEF_WHITESPACE;
			emit->flags |= FYEF_HAD_DOCUMENT_END;
		}
	}

	/* top comment */
	if (fy_emit_token_has_comment(emit, fyds->fyt_ds, fycp_top)) {
		if (emit->column)
			fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
		fy_emit_token_comment(emit, fyds->fyt_ds, sc->flags, sc->indent, fycp_top);
	}
	/* right comment */
	if (fy_emit_token_has_comment(emit, fyds->fyt_ds, fycp_right)) {
		if (emit->column)
			fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
		fy_emit_token_comment(emit, fyds->fyt_ds, sc->flags, sc->indent, fycp_right);
		sc->flags |= DDNF_HANGING_INDENT;
	}

	if (!fy_emit_is_json_mode(emit) && vd) {
		if (emit->column)
			fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
		fy_emit_printf(emit, fyewt_version_directive, "%%YAML %d.%d",
					fyds->version.major, fyds->version.minor);
		fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
		emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;
	}

	if (!fy_emit_is_json_mode(emit) && td) {

		for (fyt_chk = fy_token_list_first(&fyds->fyt_td); fyt_chk; fyt_chk = fy_token_next(&fyds->fyt_td, fyt_chk)) {

			td_handle = fy_tag_directive_token_handle(fyt_chk, &td_handle_size);
			td_prefix = fy_tag_directive_token_prefix(fyt_chk, &td_prefix_size);
			assert(td_handle && td_prefix);

			if (fy_tag_is_default_internal(td_handle, td_handle_size, td_prefix, td_prefix_size))
				continue;

			had_non_default_tag = true;

			if (emit->column)
				fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
			fy_emit_printf(emit, fyewt_tag_directive, "%%TAG %.*s %.*s",
					(int)td_handle_size, td_handle,
					(int)td_prefix_size, td_prefix);
			fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
			emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;
		}
	}

	/* always output document start indicator:
	 * - was explicit
	 * - document has tags
	 * - document has an explicit version
	 * - root exists & has a tag or an anchor
	 */
	dsm = (dsm_flags == FYECF_DOC_START_MARK_AUTO &&
			(!fyds->start_implicit ||
			(fyds->tags_explicit && had_non_default_tag) ||
			fyds->version_explicit )) ||
	       dsm_flags == FYECF_DOC_START_MARK_ON;

	/* if there was previous output without document end */
	if (!dsm && (emit->flags & FYEF_HAD_DOCUMENT_OUTPUT) &&
	           !(emit->flags & FYEF_HAD_DOCUMENT_END))
		dsm = true;

	/* there was *any* document end output (and no document end) */
	if (!dsm && (emit->flags & FYEF_HAD_DOCUMENT_END_OUTPUT) &&
		    !(emit->flags & FYEF_HAD_DOCUMENT_END))
		dsm = true;

	/* output document start indicator if we should */
	if (dsm && !fy_emit_is_json_mode(emit))
		fy_emit_document_start_indicator(emit);

	/* clear that in any case */
	emit->flags &= ~FYEF_HAD_DOCUMENT_END;

	return 0;
}

int fy_emit_document_start(struct fy_emitter *emit, struct fy_document *fyd,
			   struct fy_node *fyn_root)
{
	struct fy_node *root;
	bool root_tag_or_anchor;
	int ret;

	if (!emit || !fyd || !fyd->fyds)
		return -1;

	root = fyn_root ? fyn_root : fy_document_root(fyd);

	root_tag_or_anchor = root && (root->tag || fy_document_lookup_anchor_by_node(fyd, root));

	ret = fy_emit_common_document_start(emit, fyd->fyds, root_tag_or_anchor);
	if (ret)
		return ret;

	emit->fyd = fyd;

	return 0;
}

int fy_emit_common_document_end(struct fy_emitter *emit, bool override_state, bool implicit_override)
{
	const struct fy_document_state *fyds;
	enum fy_emitter_cfg_flags flags = emit->xcfg.cfg.flags;
	enum fy_emitter_cfg_flags dem_flags = flags & FYECF_DOC_END_MARK(FYECF_DOC_END_MARK_MASK);
	bool implicit, dem;

	if (!emit || !emit->fyds)
		return -1;

	fyds = emit->fyds;

	implicit = fyds->end_implicit;
	if (emit->s_flags & DDNF_FORCE_EXPLICIT) {	/* force explicit */
		emit->s_flags &= ~DDNF_FORCE_EXPLICIT;
		implicit = false;
	} else if (override_state)
		implicit = implicit_override;

	dem = ((dem_flags == FYECF_DOC_END_MARK_AUTO && !implicit) ||
		dem_flags == FYECF_DOC_END_MARK_ON) &&
	       !(emit->xcfg.cfg.flags & FYECF_STRIP_DOC);

	if (!(emit->xcfg.cfg.flags & FYECF_NO_ENDING_NEWLINE)) {
		if (emit->column != 0) {
			fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
			emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;
		}

		if (!fy_emit_is_json_mode(emit) && dem) {
			fy_emit_puts_simple(emit, fyewt_document_indicator, "...");
			fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
			emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;
			emit->flags |= FYEF_HAD_DOCUMENT_END;
		} else
			emit->flags &= ~FYEF_HAD_DOCUMENT_END;
	} else {
		if (!fy_emit_is_json_mode(emit) && dem) {
			if (emit->column != 0) {
				fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
				emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;
			}
			fy_emit_puts_simple(emit, fyewt_document_indicator, "...");
			emit->flags &= ~(FYEF_WHITESPACE | FYEF_INDENTATION);
			emit->flags |= FYEF_HAD_DOCUMENT_END;
		} else
			emit->flags &= ~FYEF_HAD_DOCUMENT_END;
	}

	/* mark that we did output a document earlier */
	emit->flags |= FYEF_HAD_DOCUMENT_OUTPUT;

	/* stop our association with the document */
	fy_document_state_unref(emit->fyds);
	emit->fyds = NULL;

	return 0;
}

int fy_emit_document_end(struct fy_emitter *emit)
{
	int ret;

	ret = fy_emit_common_document_end(emit, false, false);
	if (ret)
		return ret;

	emit->fyd = NULL;
	return 0;
}

int fy_emit_common_explicit_document_end(struct fy_emitter *emit)
{
	if (!emit)
		return -1;

	if (emit->column != 0) {
		fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
		emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;
	}

	if (!fy_emit_is_json_mode(emit)) {
		fy_emit_puts_simple(emit, fyewt_document_indicator, "...");
		fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
		emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;
		emit->flags |= FYEF_HAD_DOCUMENT_END;
	} else
		emit->flags &= ~FYEF_HAD_DOCUMENT_END;

	/* mark that we did output a document earlier */
	emit->flags |= FYEF_HAD_DOCUMENT_OUTPUT;

	/* stop our association with the document */
	fy_document_state_unref(emit->fyds);
	emit->fyds = NULL;

	return 0;
}

int fy_emit_explicit_document_end(struct fy_emitter *emit)
{
	int ret;

	ret = fy_emit_common_explicit_document_end(emit);
	if (ret)
		return ret;

	emit->fyd = NULL;
	return 0;
}

void fy_emit_reset(struct fy_emitter *emit, bool reset_events)
{
	struct fy_eventp *fyep;

	emit->line = 0;
	emit->column = 0;
	emit->flow_level = 0;
	emit->output_error = 0;
	/* clear/set flags for a fresh start */
	emit->flags &= ~(FYEF_OPEN_ENDED | FYEF_HAD_DOCUMENT_START | FYEF_HAD_DOCUMENT_OUTPUT);
	emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;

	emit->state = FYES_NONE;

	/* reset the accumulator */
	fy_emit_accum_reset(&emit->ea);

	/* streaming mode indent */
	emit->s_indent = -1;
	/* streaming mode flags */
	emit->s_flags = DDNF_ROOT;

	fy_emit_save_ctx_cleanup(emit, &emit->s_sc);
	while (emit->sc_stack_top > 0)
		fy_emit_save_ctx_cleanup(emit, &emit->sc_stack[--emit->sc_stack_top]);

	emit->state_stack_top = 0;

	/* and release any queued events */
	if (reset_events) {
		while ((fyep = fy_eventp_list_pop(&emit->queued_events)) != NULL)
			fy_eventp_release(fyep);
	}
}

int fy_emit_setup(struct fy_emitter *emit, const struct fy_emitter_cfg *cfg)
{
	struct fy_diag *diag;

	if (!cfg)
		return -1;

	memset(emit, 0, sizeof(*emit));
	emit->output_fd = -1;

	/* is it extended config or not? */
	if (!(cfg->flags & FYECF_EXTENDED_CFG))
		emit->xcfg.cfg = *cfg;
	else
		emit->xcfg = *container_of(cfg, struct fy_emitter_xcfg, cfg);

	fy_emitter_fill_default_colors(emit);
	emit->output_fp = fy_emitter_get_output_fp(emit);
	if (!emit->output_fp)
		emit->output_fd = fy_emitter_get_output_fd(emit);

	if (!emit->xcfg.cfg.output) {
		if (!emit->output_fp && emit->output_fd < 0) {
			emit->xcfg.cfg.output = fy_emitter_null_output;
		} else {
			emit->xcfg.cfg.output = fy_emitter_default_output;
			/* for the default, turn on extended indicators */
			emit->xcfg.xflags |= FYEXCF_EXTENDED_INDICATORS;

			switch (emit->xcfg.xflags & (FYEXCF_COLOR_MASK << FYEXCF_COLOR_SHIFT)) {
			case FYEXCF_COLOR_AUTO:
				emit->output_colorize = emit->output_fp ?
						isatty(fileno(emit->output_fp)) :
						isatty(emit->output_fd);
				break;
			case FYEXCF_COLOR_FORCE:
				emit->output_colorize = true;
				break;
			default:
				emit->output_colorize = false;
				break;
			}
		}
	}

	diag = cfg->diag;

	if (!diag) {
		diag = fy_diag_create(NULL);
		if (!diag)
			return -1;
	} else
		fy_diag_ref(diag);

	emit->diag = diag;

	fy_emit_accum_init(&emit->ea, emit->ea_inplace_buf, sizeof(emit->ea_inplace_buf), 0, fylb_cr_nl);
	fy_eventp_list_init(&emit->queued_events);

	emit->state_stack = emit->state_stack_inplace;
	emit->state_stack_alloc = sizeof(emit->state_stack_inplace)/sizeof(emit->state_stack_inplace[0]);

	emit->sc_stack = emit->sc_stack_inplace;
	emit->sc_stack_alloc = sizeof(emit->sc_stack_inplace)/sizeof(emit->sc_stack_inplace[0]);

	fy_eventp_list_init(&emit->recycled_eventp);
	fy_token_list_init(&emit->recycled_token);

	/* suppress recycling if we must */
	emit->suppress_recycling_force = getenv("FY_VALGRIND") && !getenv("FY_VALGRIND_RECYCLING");
	emit->suppress_recycling = emit->suppress_recycling_force;

	if (!emit->suppress_recycling) {
		emit->recycled_eventp_list = &emit->recycled_eventp;
		emit->recycled_token_list = &emit->recycled_token;
	} else {
		emit->recycled_eventp_list = NULL;
		emit->recycled_token_list = NULL;
	}

	fy_emit_reset(emit, false);

	return 0;
}

void fy_emit_save_ctx_cleanup(struct fy_emitter *emit, struct fy_emit_save_ctx *sc)
{
	fy_token_unref_rl(emit->recycled_token_list, sc->fyt_last_key);
	sc->fyt_last_key = NULL;
	fy_token_unref_rl(emit->recycled_token_list, sc->fyt_last_value);
	sc->fyt_last_value = NULL;
	memset(sc, 0, sizeof(*sc));
}

void fy_emit_cleanup(struct fy_emitter *emit)
{
	struct fy_eventp *fyep;
	struct fy_token *fyt;

	/* call the finalizer if it exists */
	if (emit->finalizer)
		emit->finalizer(emit);

	fy_emit_save_ctx_cleanup(emit, &emit->s_sc);
	while (emit->sc_stack_top > 0)
		fy_emit_save_ctx_cleanup(emit, &emit->sc_stack[--emit->sc_stack_top]);

	while ((fyt = fy_token_list_pop(&emit->recycled_token)) != NULL)
		fy_token_free(fyt);

	while ((fyep = fy_eventp_list_pop(&emit->recycled_eventp)) != NULL)
		fy_eventp_free(fyep);

	fy_document_state_unref(emit->fyds);
	emit->fyds = NULL;

	fy_emit_accum_cleanup(&emit->ea);

	while ((fyep = fy_eventp_list_pop(&emit->queued_events)) != NULL)
		fy_eventp_release(fyep);

	if (emit->state_stack && emit->state_stack != emit->state_stack_inplace)
		free(emit->state_stack);

	if (emit->sc_stack && emit->sc_stack != emit->sc_stack_inplace)
		free(emit->sc_stack);

	fy_diag_unref(emit->diag);

	if (emit->owned_output_fp)
		fclose(emit->owned_output_fp);
}

int fy_emit_node_no_check(struct fy_emitter *emit, struct fy_node *fyn)
{
	if (fyn)
		fy_emit_node_internal(emit, fyn, DDNF_ROOT, -1, false);
	return 0;
}

int fy_emit_node(struct fy_emitter *emit, struct fy_node *fyn)
{
	int ret;

	ret = fy_emit_node_check(emit, fyn);
	if (ret)
		return ret;

	return fy_emit_node_no_check(emit, fyn);
}

int fy_emit_root_node_no_check(struct fy_emitter *emit, struct fy_node *fyn)
{
	if (!emit || !fyn)
		return -1;

	fy_emit_node_internal(emit, fyn, DDNF_ROOT, -1, false);

	return 0;
}

int fy_emit_root_node(struct fy_emitter *emit, struct fy_node *fyn)
{
	int ret;

	if (!emit || !fyn)
		return -1;

	ret = fy_emit_node_check(emit, fyn);
	if (ret)
		return ret;

	return fy_emit_root_node_no_check(emit, fyn);
}

void fy_emit_prepare_document_state(struct fy_emitter *emit, struct fy_document_state *fyds)
{
	if (!emit || !fyds)
		return;

	/* if the original document was JSON and the mode is ORIGINAL turn on JSON mode */
	emit->source_json = fyds && fyds->json_mode;
	emit->force_json = (emit->xcfg.cfg.flags & FYECF_MODE(FYECF_MODE_MASK)) == FYECF_MODE_ORIGINAL &&
			   emit->source_json;
}

int fy_emit_document_no_check(struct fy_emitter *emit, struct fy_document *fyd)
{
	int rc;

	rc = fy_emit_document_start(emit, fyd, NULL);
	if (rc)
		return rc;

	rc = fy_emit_root_node_no_check(emit, fyd->root);
	if (rc)
		return rc;

	rc = fy_emit_document_end(emit);

	fy_emit_reset(emit, false);

	return rc;
}

int fy_emit_document(struct fy_emitter *emit, struct fy_document *fyd)
{
	int ret;

	if (!emit)
		return -1;

	if (fyd) {
		fy_emit_prepare_document_state(emit, fyd->fyds);

		if (fyd->root) {
			ret = fy_emit_node_check(emit, fyd->root);
			if (ret)
				return ret;
		}
	}

	return fy_emit_document_no_check(emit, fyd);
}

struct fy_emitter *fy_emitter_create(const struct fy_emitter_cfg *cfg)
{
	struct fy_emitter *emit;
	int rc;

	if (!cfg)
		return NULL;

	emit = malloc(sizeof(*emit));
	if (!emit)
		return NULL;

	rc = fy_emit_setup(emit, cfg);
	if (rc) {
		free(emit);
		return NULL;
	}

	return emit;
}

void fy_emitter_destroy(struct fy_emitter *emit)
{
	if (!emit)
		return;

	fy_emit_cleanup(emit);

	free(emit);
}

const struct fy_emitter_cfg *fy_emitter_get_cfg(struct fy_emitter *emit)
{
	if (!emit)
		return NULL;

	return &emit->xcfg.cfg;
}

struct fy_diag *fy_emitter_get_diag(struct fy_emitter *emit)
{
	if (!emit || !emit->diag)
		return NULL;
	return fy_diag_ref(emit->diag);
}

int fy_emitter_set_diag(struct fy_emitter *emit, struct fy_diag *diag)
{
	struct fy_diag_cfg dcfg;

	if (!emit)
		return -1;

	/* default? */
	if (!diag) {
		fy_diag_cfg_default(&dcfg);
		diag = fy_diag_create(&dcfg);
		if (!diag)
			return -1;
	}

	fy_diag_unref(emit->diag);
	emit->diag = fy_diag_ref(diag);

	return 0;
}

void fy_emitter_set_finalizer(struct fy_emitter *emit,
		void (*finalizer)(struct fy_emitter *emit))
{
	if (!emit)
		return;
	emit->finalizer = finalizer;
}

struct fy_emit_buffer_state {
	char **bufp;
	size_t *sizep;
	char *buf;
	size_t size;
	size_t pos;
	size_t need;
	bool allocate_buffer;
	size_t maxsize;
};

static int do_buffer_output(struct fy_emitter *emit,
			    enum fy_emitter_write_type type FY_UNUSED,
			    const char *str,
			    int leni,
			    void *userdata FY_UNUSED)
{
	struct fy_emit_buffer_state *state = emit->xcfg.cfg.userdata;
	size_t left, pagesize, size, len;
	char *bufnew;

	if (leni < 0)
		return -1;

	/* convert to unsigned and use that */
	len = (size_t)leni;

	/* no funky business */
	if ((ssize_t)len < 0)
		return -1;

	state->need += len;
	left = state->size - state->pos;
	if (left < len) {
		if (!state->allocate_buffer)
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

	return (int)len;
}

static void
fy_emitter_str_finalizer(struct fy_emitter *emit)
{
	struct fy_emit_buffer_state *state;

	if (!emit || !(state = emit->xcfg.cfg.userdata))
		return;

	/* if the buffer is allowed to allocate_buffer... */
	if (state->allocate_buffer && state->buf)
		free(state->buf);
	free(state);

	emit->xcfg.cfg.userdata = NULL;
}

static struct fy_emitter *
fy_emitter_create_str_internal(enum fy_emitter_cfg_flags flags, char **bufp, size_t *sizep, bool allocate_buffer)
{
	struct fy_emitter *emit;
	struct fy_emitter_cfg emit_cfg;
	struct fy_emit_buffer_state *state;

	if (flags & FYECF_EXTENDED_CFG)
		return NULL;

	state = malloc(sizeof(*state));
	if (!state)
		return NULL;

	/* if any of these NULL, it's a allocation case */
	if ((!bufp || !sizep) && !allocate_buffer)
		return NULL;

	if (bufp && sizep) {
		state->bufp = bufp;
		state->buf = *bufp;
		state->sizep = sizep;
		state->size = *sizep;
		state->maxsize = state->size;
	} else {
		state->bufp = NULL;
		state->buf = NULL;
		state->sizep = NULL;
		state->size = 0;
		state->maxsize = 0;
	}
	state->pos = 0;
	state->need = 0;
	state->allocate_buffer = allocate_buffer;

	memset(&emit_cfg, 0, sizeof(emit_cfg));
	emit_cfg.output = do_buffer_output;
	emit_cfg.userdata = state;
	emit_cfg.flags = flags;

	emit = fy_emitter_create(&emit_cfg);
	if (!emit)
		goto err_out;

	/* set finalizer to cleanup */
	fy_emitter_set_finalizer(emit, fy_emitter_str_finalizer);

	return emit;

err_out:
	if (state)
		free(state);
	return NULL;
}

static int
fy_emitter_collect_str_internal(struct fy_emitter *emit, char **bufp, size_t *sizep)
{
	struct fy_emit_buffer_state *state;
	char *buf;
	size_t size;
	int rc;

	state = emit->xcfg.cfg.userdata;
	assert(state);

	/* if NULL, then use the values stored on the state */
	if (!bufp)
		bufp = state->bufp;
	if (!sizep)
		sizep = state->sizep;

	/* terminating zero */
	rc = do_buffer_output(emit, fyewt_terminating_zero, "\0", 1, state);
	if (rc != 1)
		goto err_out;

	/* if we are on a fixed buffer don't output */
	if (state->maxsize > 0 && state->need > state->maxsize)
		goto err_out;

	state->size = state->need;

	if (state->allocate_buffer) {
		/* resize */
		buf = realloc(state->buf, state->size);
		/* very likely since we shrink the buffer, but make sure we don't error out */
		if (buf)
			state->buf = buf;
	}

	/* retreive the buffer and size */
	size = state->size;
	if (size > 0 && state->buf[size-1] == '\0')
		size--;
	*sizep = size;
	*bufp = state->buf;

	/* reset the buffer, ownership now to the caller */
	state->buf = NULL;
	state->size = 0;
	state->pos = 0;
	state->bufp = NULL;
	state->sizep = NULL;

	return 0;

err_out:
	*bufp = NULL;
	*sizep = 0;
	return -1;
}

static int fy_emit_str_internal(struct fy_document *fyd,
				enum fy_emitter_cfg_flags flags,
				struct fy_node *fyn, char **bufp, size_t *sizep,
				bool allocate_buffer)
{
	struct fy_emitter *emit = NULL;
	int rc = -1;

	emit = fy_emitter_create_str_internal(flags, bufp, sizep, allocate_buffer);
	if (!emit)
		goto out_err;

	if (fyd) {
		fy_emit_prepare_document_state(emit, fyd->fyds);
		rc = 0;
		if (fyd->root)
			rc = fy_emit_node_check(emit, fyd->root);
		if (!rc)
			rc = fy_emit_document_no_check(emit, fyd);
	} else {
		rc = fy_emit_node_check(emit, fyn);
		if (!rc)
			rc = fy_emit_node_no_check(emit, fyn);
	}

	if (rc)
		goto out_err;

	rc = fy_emitter_collect_str_internal(emit, NULL, NULL);
	if (rc)
		goto out_err;

	/* OK, all done */

out_err:
	fy_emitter_destroy(emit);
	return rc;
}

int fy_emit_document_to_buffer(struct fy_document *fyd, enum fy_emitter_cfg_flags flags, char *buf, size_t size)
{
	int rc;

	if (!buf)
		return -1;

	rc = fy_emit_str_internal(fyd, flags, NULL, &buf, &size, false);
	if (rc != 0)
		return -1;
	if ((int)size < 0)
		return -1;
	return (int)size;
}

char *fy_emit_document_to_string(struct fy_document *fyd, enum fy_emitter_cfg_flags flags)
{
	char *buf;
	size_t size;
	int rc;

	buf = NULL;
	size = 0;
	rc = fy_emit_str_internal(fyd, flags, NULL, &buf, &size, true);
	if (rc != 0)
		return NULL;
	return buf;
}

struct fy_emitter *
fy_emit_to_buffer(enum fy_emitter_cfg_flags flags, char *buf, size_t size)
{
	if (!buf)
		return NULL;

	return fy_emitter_create_str_internal(flags, &buf, &size, false);
}

char *
fy_emit_to_buffer_collect(struct fy_emitter *emit, size_t *sizep)
{
	int rc;
	char *buf;

	if (!emit || !sizep)
		return NULL;

	rc = fy_emitter_collect_str_internal(emit, &buf, sizep);
	if (rc) {
		*sizep = 0;
		return NULL;
	}
	return buf;
}

struct fy_emitter *
fy_emit_to_string(enum fy_emitter_cfg_flags flags)
{
	return fy_emitter_create_str_internal(flags, NULL, NULL, true);
}

char *
fy_emit_to_string_collect(struct fy_emitter *emit, size_t *sizep)
{
	int rc;
	char *buf;

	if (!emit || !sizep)
		return NULL;

	rc = fy_emitter_collect_str_internal(emit, &buf, sizep);
	if (rc) {
		*sizep = 0;
		return NULL;
	}
	return buf;
}

static int do_file_output(struct fy_emitter *emit FY_UNUSED,
			  enum fy_emitter_write_type type FY_UNUSED,
			  const char *str,
			  int leni,
			  void *userdata)
{
	FILE *fp = userdata;
	size_t len, wrn;

	/* no funky stuff */
	if (leni < 0)
		return -1;

	len = (size_t)leni;
	wrn = fwrite(str, 1, len, fp);
	return (int)wrn;
}

int fy_emit_document_to_fp(struct fy_document *fyd, enum fy_emitter_cfg_flags flags,
			   FILE *fp)
{
	struct fy_emitter emit_state, *emit = &emit_state;
	struct fy_emitter_cfg emit_cfg;
	int rc;

	/* we don't allow an extended configuration */
	if (!fp || (flags & FYECF_EXTENDED_CFG))
		return -1;

	memset(&emit_cfg, 0, sizeof(emit_cfg));
	emit_cfg.output = do_file_output;
	emit_cfg.userdata = fp;
	emit_cfg.flags = flags;
	fy_emit_setup(emit, &emit_cfg);

	fy_emit_prepare_document_state(emit, fyd->fyds);

	rc = 0;
	if (fyd->root)
		rc = fy_emit_node_check(emit, fyd->root);

	rc = fy_emit_document_no_check(emit, fyd);

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

static int do_fd_output(struct fy_emitter *emit FY_UNUSED,
			enum fy_emitter_write_type type FY_UNUSED,
			const char *str,
			int leni,
			void *userdata)
{
	size_t len;
	int fd;
	ssize_t wrn;
	int total;

	len = (size_t)leni;

	/* no funky stuff */
	if ((ssize_t)len < 0)
		return -1;

	/* get the file descriptor */
	fd = (int)(uintptr_t)userdata;
	if (fd < 0)
		return -1;

	/* loop output to fd */
	total = 0;
	while (len > 0) {

		do {
			wrn = write(fd, str, len);
		} while (wrn == -1 && errno == EAGAIN);

		if (wrn == -1)
			return -1;

		if (wrn == 0)
			return total;

		len -= wrn;
		str += wrn;
		total += (int)wrn;
	}

	return total;
}

int fy_emit_document_to_fd(struct fy_document *fyd, enum fy_emitter_cfg_flags flags, int fd)
{
	struct fy_emitter emit_state, *emit = &emit_state;
	struct fy_emitter_cfg emit_cfg;
	int rc;

	if (fd < 0 || (flags & FYECF_EXTENDED_CFG))
		return -1;

	memset(&emit_cfg, 0, sizeof(emit_cfg));
	emit_cfg.output = do_fd_output;
	emit_cfg.userdata = (void *)(uintptr_t)fd;
	emit_cfg.flags = flags;
	fy_emit_setup(emit, &emit_cfg);

	fy_emit_prepare_document_state(emit, fyd->fyds);

	rc = 0;
	if (fyd->root)
		rc = fy_emit_node_check(emit, fyd->root);

	rc = fy_emit_document_no_check(emit, fyd);

	fy_emit_cleanup(emit);

	return rc ? rc : 0;
}

int fy_emit_node_to_buffer(struct fy_node *fyn, enum fy_emitter_cfg_flags flags, char *buf, size_t size)
{
	int rc;

	rc = fy_emit_str_internal(NULL, flags, fyn, &buf, &size, false);
	if (rc != 0)
		return -1;
	return (int)size;
}

char *fy_emit_node_to_string(struct fy_node *fyn, enum fy_emitter_cfg_flags flags)
{
	char *buf;
	size_t size;
	int rc;

	buf = NULL;
	size = 0;
	rc = fy_emit_str_internal(NULL, flags, fyn, &buf, &size, true);
	if (rc != 0)
		return NULL;
	return buf;
}

static bool fy_emit_preserve_flow_layout_ready(struct fy_emitter *emit)
{
	struct fy_eventp *fyep;
	struct fy_token *start_token, *end_token;
	enum fy_node_style style;
	int depth;

	/* no preserve flow, always ready */
	if (!fy_emit_preserve_flow_layout(emit))
		return true;

	/* When the head event starts a flow collection in ORIGINAL mode,
	 * buffer events on the same source line so that oneline_flow
	 * detection can compare start/end line marks before the prolog
	 * runs.  We only buffer events that share the start token's line;
	 * as soon as an event is on a different line we know the collection
	 * is multi-line and can proceed immediately.  This keeps the buffer
	 * bounded to a single line's worth of events rather than the
	 * entire collection.
	 */
	fyep = fy_eventp_list_head(&emit->queued_events);
	if (!fyep)
		return true;

	if (fyep->e.type == FYET_SEQUENCE_START)
		start_token = fyep->e.sequence_start.sequence_start;
	else if (fyep->e.type == FYET_MAPPING_START)
		start_token = fyep->e.mapping_start.mapping_start;
	else
		return true;	/* non collection start, ready */

	/* we must be on flow style */
	style = fy_token_node_style(start_token);
	if (style != FYNS_FLOW)
		return true;

	depth = 1;
	for (fyep = fy_eventp_next(&emit->queued_events, fyep); fyep;
			fyep = fy_eventp_next(&emit->queued_events, fyep)) {

		/* if any token is not on the same line, we're done */
		end_token = fy_event_get_token(&fyep->e);
		if (!fy_tokens_oneline(start_token, end_token))
			return true;

		/* track the depth, to know when to end */
		switch (fyep->e.type) {
		case FYET_SEQUENCE_START:
		case FYET_MAPPING_START:
			depth++;
			break;
		case FYET_SEQUENCE_END:
		case FYET_MAPPING_END:
			if (--depth <= 0)
				return true;	/* ready... */
			break;
		default:
			break;
		}
	}
	return false;
}

/* we can consume events only if the lookaheads
 * are satisfied. The lookaheads depends on the
 * styling mode. Some styling modes do not require
 * a lookahead at all but we do it anyway.
 */
static bool fy_emit_ready(struct fy_emitter *emit)
{
	struct fy_eventp *fyep;
	int need, count;

	/* if we're preserving the layout we might have to wait a bit */
	if (fy_emit_preserve_flow_layout(emit) &&
	    !fy_emit_preserve_flow_layout_ready(emit))
		return false;

	count = 0;
	need = -1;
	for (fyep = fy_eventp_list_head(&emit->queued_events); fyep;
			fyep = fy_eventp_next(&emit->queued_events, fyep)) {

		count++;
		switch (fyep->e.type) {
		case FYET_DOCUMENT_START:
			need++;
			break;

		case FYET_SEQUENCE_START:
			if (need < 0)
				need = 2;
			break;

		case FYET_MAPPING_START:
			if (need < 0)
				need = 2;
			break;

		default:
			need = 0;
			break;
		}

		if (count >= need)
			return true;

	}

	return false;
}

extern const char *fy_event_type_txt[];

const char *fy_emitter_state_txt[] = {
	[FYES_NONE]			= "NONE",
	[FYES_STREAM_START]		= "STREAM_START",
	[FYES_FIRST_DOCUMENT_START]	= "FIRST_DOCUMENT_START",
	[FYES_DOCUMENT_START]		= "DOCUMENT_START",
	[FYES_DOCUMENT_CONTENT]		= "DOCUMENT_CONTENT",
	[FYES_DOCUMENT_END]		= "DOCUMENT_END",
	[FYES_SEQUENCE_FIRST_ITEM]	= "SEQUENCE_FIRST_ITEM",
	[FYES_SEQUENCE_ITEM]		= "SEQUENCE_ITEM",
	[FYES_MAPPING_FIRST_KEY]	= "MAPPING_FIRST_KEY",
	[FYES_MAPPING_KEY]		= "MAPPING_KEY",
	[FYES_MAPPING_SIMPLE_VALUE]	= "MAPPING_SIMPLE_VALUE",
	[FYES_MAPPING_VALUE]		= "MAPPING_VALUE",
	[FYES_END]			= "END",
};

struct fy_eventp *
fy_emit_next_event(struct fy_emitter *emit)
{
	if (!fy_emit_ready(emit))
		return NULL;

	return fy_eventp_list_pop(&emit->queued_events);
}

struct fy_eventp *
fy_emit_peek_next_event(struct fy_emitter *emit)
{
	return fy_eventp_list_head(&emit->queued_events);
}

bool fy_emit_streaming_sequence_empty(struct fy_emitter *emit)
{
	struct fy_eventp *fyepn;

	fyepn = fy_emit_peek_next_event(emit);

	/* should never happen, check on debugging mode */
	assert(fyepn);
	if (!fyepn)
		return false;

	return fyepn->e.type == FYET_SEQUENCE_END;
}

bool fy_emit_streaming_mapping_empty(struct fy_emitter *emit)
{
	struct fy_eventp *fyepn;

	fyepn = fy_emit_peek_next_event(emit);

	/* should never happen, check on debugging mode */
	assert(fyepn);
	if (!fyepn)
		return false;

	return fyepn->e.type == FYET_MAPPING_END;
}

static struct fy_token *
fy_emit_streaming_find_end_token(struct fy_emitter *emit,
				 struct fy_token *start_token)
{
	struct fy_eventp *fyep;
	enum fy_node_type type;
	int depth;

	if (!start_token)
		return NULL;

	if (fy_token_type_is_sequence_start(start_token->type))
		type = FYNT_SEQUENCE;
	else if (fy_token_type_is_mapping_start(start_token->type))
		type = FYNT_MAPPING;
	else
		type = FYNT_SCALAR;

	/* non sequence/mapping is the same */
	if (type == FYNT_SCALAR)
		return start_token;

	depth = 1;
	for (fyep = fy_eventp_list_head(&emit->queued_events); fyep;
			fyep = fy_eventp_next(&emit->queued_events, fyep)) {

		switch (fyep->e.type) {
		case FYET_SEQUENCE_START:
		case FYET_MAPPING_START:
			depth++;
			break;
		case FYET_SEQUENCE_END:
			if (--depth == 0)
				return type == FYNT_SEQUENCE ?
					fyep->e.sequence_end.sequence_end :
					NULL;
			break;
		case FYET_MAPPING_END:
			if (--depth == 0)
				return type == FYNT_MAPPING ?
					fyep->e.mapping_end.mapping_end :
					NULL;
			break;
		default:
			break;
		}
	}

	return NULL;
}

static bool fy_emit_streaming_oneline_flow(struct fy_emitter *emit,
					   struct fy_token *start_token)
{
	struct fy_token *end_token;
	enum fy_node_style style;

	/* only for preserve flow cases */
	if (!fy_emit_preserve_flow_layout(emit) || !start_token)
		return false;

	style = fy_token_node_style(start_token);
	if (style != FYNS_FLOW)
		return false;

	end_token = fy_emit_streaming_find_end_token(emit, start_token);
	return fy_tokens_oneline(start_token, end_token);
}

static void fy_emit_goto_state(struct fy_emitter *emit, enum fy_emitter_state state)
{
	if (emit->state == state)
		return;

	emit->state = state;
}

static int fy_emit_push_state(struct fy_emitter *emit, enum fy_emitter_state state)
{
	enum fy_emitter_state *states;

	if (emit->state_stack_top >= emit->state_stack_alloc) {
		states = realloc(emit->state_stack == emit->state_stack_inplace ? NULL : emit->state_stack,
				sizeof(emit->state_stack[0]) * emit->state_stack_alloc * 2);
		if (!states)
			return -1;

		if (emit->state_stack == emit->state_stack_inplace)
			memcpy(states, emit->state_stack, sizeof(emit->state_stack[0]) * emit->state_stack_top);
		emit->state_stack = states;
		emit->state_stack_alloc *= 2;
	}
	emit->state_stack[emit->state_stack_top++] = state;

	return 0;
}

enum fy_emitter_state fy_emit_pop_state(struct fy_emitter *emit)
{
	if (!emit->state_stack_top)
		return FYES_NONE;

	return emit->state_stack[--emit->state_stack_top];
}

int fy_emit_push_sc(struct fy_emitter *emit, struct fy_emit_save_ctx *sc)
{
	struct fy_emit_save_ctx *scs;

	if (emit->sc_stack_top >= emit->sc_stack_alloc) {
		scs = realloc(emit->sc_stack == emit->sc_stack_inplace ? NULL : emit->sc_stack,
				sizeof(emit->sc_stack[0]) * emit->sc_stack_alloc * 2);
		if (!scs)
			return -1;

		if (emit->sc_stack == emit->sc_stack_inplace)
			memcpy(scs, emit->sc_stack, sizeof(emit->sc_stack[0]) * emit->sc_stack_top);
		emit->sc_stack = scs;
		emit->sc_stack_alloc *= 2;
	}
	emit->sc_stack[emit->sc_stack_top++] = *sc;

	return 0;
}

int fy_emit_pop_sc(struct fy_emitter *emit, struct fy_emit_save_ctx *sc)
{
	if (!emit->sc_stack_top)
		return -1;

	*sc = emit->sc_stack[--emit->sc_stack_top];

	return 0;
}

static int fy_emit_streaming_node(struct fy_emitter *emit,
				  struct fy_parser *fyp FY_UNUSED,
				  struct fy_eventp *fyep,
				  int flags)
{
	struct fy_event *fye = &fyep->e;
	struct fy_emit_save_ctx *sc = &emit->s_sc;
	enum fy_node_style style, xstyle;
	int ret, s_flags, s_indent;

	/* force collections after --- to start at a new line */
	if (!fy_emit_is_oneline(emit) &&
			fye->type != FYET_ALIAS && fye->type != FYET_SCALAR &&
			(emit->s_flags & DDNF_ROOT) && emit->column != 0) {
		fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
		emit->flags |= FYEF_WHITESPACE | FYEF_INDENTATION;
	}

	emit->s_flags = flags;

	switch (fye->type) {
	case FYET_ALIAS:
		fy_emit_token_scalar(emit, fye->alias.anchor, emit->s_flags, emit->s_indent, FYNS_ALIAS, NULL);
		fy_emit_goto_state(emit, fy_emit_pop_state(emit));
		break;

	case FYET_SCALAR:
		/* if we're pretty and at column 0 (meaning it's a single scalar document) output --- */
		if ((emit->s_flags & DDNF_ROOT) && fy_emit_is_pretty_mode(emit) && !emit->column &&
				!fy_emit_is_flow_mode(emit) && !(emit->s_flags & DDNF_FLOW)) {
			fy_emit_document_start_indicator(emit);
			emit->s_flags |= DDNF_FORCE_EXPLICIT;
		}
		fy_emit_common_node_preamble(emit, fye->scalar.value,
					     fye->scalar.anchor, fye->scalar.tag,
					     emit->s_flags, emit->s_indent,
					     FYNT_SCALAR);
		style = fye->scalar.value ?
				fy_node_style_from_scalar_style(fye->scalar.value->scalar.style) :
				FYNS_PLAIN;
		fy_emit_token_scalar(emit, fye->scalar.value, emit->s_flags, emit->s_indent, style, fye->scalar.tag);
		fy_emit_goto_state(emit, fy_emit_pop_state(emit));
		break;

	case FYET_SEQUENCE_START:

		/* save this context */
		ret = fy_emit_push_sc(emit, sc);
		if (ret)
			return ret;

		s_flags = emit->s_flags;
		s_indent = emit->s_indent;

		xstyle = fy_token_node_style(fye->sequence_start.sequence_start);

		fy_emit_common_node_preamble(emit, fye->sequence_start.sequence_start,
					     fye->sequence_start.anchor, fye->sequence_start.tag,
					     emit->s_flags, emit->s_indent,
					     FYNT_SEQUENCE);

		/* create new context */
		memset(sc, 0, sizeof(*sc));
		sc->flags = emit->s_flags & (DDNF_ROOT | DDNF_SEQ | DDNF_MAP);
		sc->indent = emit->s_indent;
		sc->empty = fy_emit_streaming_sequence_empty(emit);
		sc->flow_token = xstyle == FYNS_FLOW;
		sc->oneline_flow = fy_emit_streaming_oneline_flow(emit, fye->sequence_start.sequence_start);
		sc->flow = !!(s_flags & DDNF_FLOW);
		sc->xstyle = xstyle;
		sc->old_indent = sc->indent;
		sc->s_flags = s_flags;
		sc->s_indent = s_indent;

		fy_emit_token_sequence_prolog(emit, sc, fye->sequence_start.sequence_start);
		sc->flags &= ~DDNF_MAP;
		sc->flags |= DDNF_SEQ;

		emit->s_flags = sc->flags;
		emit->s_indent = sc->indent;

		fy_emit_goto_state(emit, FYES_SEQUENCE_FIRST_ITEM);
		break;

	case FYET_MAPPING_START:
		/* save this context */
		ret = fy_emit_push_sc(emit, sc);
		if (ret)
			return ret;

		s_flags = emit->s_flags;
		s_indent = emit->s_indent;

		xstyle = fy_token_node_style(fye->mapping_start.mapping_start);

		fy_emit_common_node_preamble(emit, fye->mapping_start.mapping_start,
					     fye->mapping_start.anchor, fye->mapping_start.tag,
					     emit->s_flags, emit->s_indent,
					     FYNT_MAPPING);

		/* create new context */
		memset(sc, 0, sizeof(*sc));
		sc->flags = emit->s_flags & (DDNF_ROOT | DDNF_SEQ | DDNF_MAP);
		sc->indent = emit->s_indent;
		sc->empty = fy_emit_streaming_mapping_empty(emit);
		sc->flow_token = xstyle == FYNS_FLOW;
		sc->oneline_flow = fy_emit_streaming_oneline_flow(emit, fye->mapping_start.mapping_start);
		sc->flow = !!(s_flags & DDNF_FLOW);
		sc->xstyle = xstyle;
		sc->old_indent = sc->indent;
		sc->s_flags = s_flags;
		sc->s_indent = s_indent;

		fy_emit_token_mapping_prolog(emit, sc, fye->mapping_start.mapping_start);
		sc->flags &= ~DDNF_SEQ;
		sc->flags |= DDNF_MAP;

		emit->s_flags = sc->flags;
		emit->s_indent = sc->indent;

		fy_emit_goto_state(emit, FYES_MAPPING_FIRST_KEY);
		break;

	default:
		fy_error(emit->diag, "%s: expected ALIAS|SCALAR|SEQUENCE_START|MAPPING_START", __func__);
		return -1;
	}

	return 0;
}

static int fy_emit_handle_stream_start(struct fy_emitter *emit,
				       struct fy_parser *fyp FY_UNUSED,
				       struct fy_eventp *fyep)
{
	struct fy_event *fye = &fyep->e;

	if (fye->type != FYET_STREAM_START) {
		fy_error(emit->diag, "%s: expected FYET_STREAM_START", __func__);
		return -1;
	}

	fy_document_state_unref(emit->fyds);
	emit->fyds = NULL;

	fy_emit_reset(emit, false);

	fy_emit_goto_state(emit, FYES_FIRST_DOCUMENT_START);

	return 0;
}

static int fy_emit_handle_document_start(struct fy_emitter *emit,
					 struct fy_parser *fyp FY_UNUSED,
					 struct fy_eventp *fyep,
					 bool first FY_UNUSED)
{
	struct fy_event *fye = &fyep->e;
	struct fy_document_state *fyds;

	if (fye->type != FYET_DOCUMENT_START &&
	    fye->type != FYET_STREAM_END) {
		fy_error(emit->diag, "%s: expected FYET_DOCUMENT_START|FYET_STREAM_END", __func__);
		return -1;
	}

	if (fye->type == FYET_STREAM_END) {
		fy_emit_goto_state(emit, FYES_END);
		return 0;
	}

	/* transfer ownership to the emitter */
	fyds = fye->document_start.document_state;
	// fye->document_start.document_state = NULL;

	/* prepare (i.e. adapt to the document state) */
	fy_emit_prepare_document_state(emit, fyds);

	fy_emit_common_document_start(emit, fyds, false);

	fy_emit_goto_state(emit, FYES_DOCUMENT_CONTENT);

	return 0;
}

static int fy_emit_handle_document_end(struct fy_emitter *emit,
				       struct fy_parser *fyp FY_UNUSED,
				       struct fy_eventp *fyep)
{
	struct fy_event *fye = &fyep->e;
	int ret;

	if (fye->type != FYET_DOCUMENT_END) {
		fy_error(emit->diag, "%s: expected FYET_DOCUMENT_END", __func__);
		return -1;
	}

	ret = fy_emit_common_document_end(emit, true, fye->document_end.implicit);
	if (ret)
		return ret;

	fy_emit_reset(emit, false);
	fy_emit_goto_state(emit, FYES_DOCUMENT_START);
	emit->flags |= FYEF_HAD_DOCUMENT_END_OUTPUT;
	return 0;
}

static int fy_emit_handle_document_content(struct fy_emitter *emit, struct fy_parser *fyp, struct fy_eventp *fyep)
{
	struct fy_event *fye = &fyep->e;
	int ret;

	/* empty document? */
	if (fye->type == FYET_DOCUMENT_END)
		return fy_emit_handle_document_end(emit, fyp, fyep);

	ret = fy_emit_push_state(emit, FYES_DOCUMENT_END);
	if (ret)
		return ret;

	return fy_emit_streaming_node(emit, fyp, fyep, DDNF_ROOT);
}

static int fy_emit_handle_sequence_item(struct fy_emitter *emit, struct fy_parser *fyp, struct fy_eventp *fyep, bool first)
{
	struct fy_event *fye = &fyep->e;
	struct fy_emit_save_ctx *sc = &emit->s_sc;
	struct fy_token *fyt_item = NULL;
	int ret;

	switch (fye->type) {
	case FYET_SEQUENCE_END:
		fy_emit_token_sequence_item_epilog(emit, sc, true, sc->fyt_last_value);

		fy_emit_token_unref(emit, fyp, sc->fyt_last_value);
		sc->fyt_last_value = NULL;

		/* emit epilog */
		fy_emit_token_sequence_epilog(emit, sc, fye->sequence_end.sequence_end);

		/* restore indent and flags */
		emit->s_indent = sc->s_indent;
		emit->s_flags = sc->s_flags;

		/* pop state */
		ret = fy_emit_pop_sc(emit, sc);
		assert(!ret);

		/* pop state */
		fy_emit_goto_state(emit, fy_emit_pop_state(emit));
		return ret;

	case FYET_ALIAS:
		fyt_item = fye->alias.anchor;
		break;
	case FYET_SCALAR:
		fyt_item = fye->scalar.value;
		break;
	case FYET_SEQUENCE_START:
		fyt_item = fye->sequence_start.sequence_start;
		break;
	case FYET_MAPPING_START:
		fyt_item = fye->mapping_start.mapping_start;
		break;
	default:
		fy_error(emit->diag, "%s: expected SEQUENCE_END|ALIAS|SCALAR|SEQUENCE_START|MAPPING_START", __func__);
		return -1;
	}

	if (!first)
		fy_emit_token_sequence_item_epilog(emit, sc, false, sc->fyt_last_value);
	fy_emit_token_unref(emit, fyp, sc->fyt_last_value);
	sc->fyt_last_value = NULL;

	ret = fy_emit_push_state(emit, FYES_SEQUENCE_ITEM);
	if (ret)
		return ret;

	/* reset indent and flags for each item */
	emit->s_indent = sc->indent;
	emit->s_flags = sc->flags;

	sc->fyt_last_value = fyt_item;

	fy_emit_token_sequence_item_prolog(emit, sc, fyt_item);

	ret = fy_emit_streaming_node(emit, fyp, fyep, sc->flags);

	switch (fye->type) {
	case FYET_ALIAS:
		fye->alias.anchor = NULL;	/* take ownership */
		break;
	case FYET_SCALAR:
		fye->scalar.value = NULL;	/* take ownership */
		break;
	case FYET_SEQUENCE_START:
		fye->sequence_start.sequence_start = NULL;	/* take ownership */
		break;
	case FYET_MAPPING_START:
		fye->mapping_start.mapping_start = NULL;	/* take ownership */
		break;
	default:
		break;
	}

	return ret;
}

static int fy_emit_handle_mapping_key(struct fy_emitter *emit, struct fy_parser *fyp, struct fy_eventp *fyep, bool first)
{
	struct fy_event *fye = &fyep->e;
	struct fy_emit_save_ctx *sc = &emit->s_sc;
	struct fy_token *fyt_key = NULL;
	const struct fy_text_analysis *ta;
	int ret;
	bool simple_key;

	simple_key = false;

	switch (fye->type) {
	case FYET_MAPPING_END:
		fy_emit_token_mapping_value_epilog(emit, sc, true, sc->fyt_last_value);

		/* emit epilog */
		fy_emit_token_mapping_epilog(emit, sc, fye->mapping_end.mapping_end);

		fy_emit_token_unref(emit, fyp, sc->fyt_last_key);
		sc->fyt_last_key = NULL;
		fy_emit_token_unref(emit, fyp, sc->fyt_last_value);
		sc->fyt_last_value = NULL;

		/* restore indent and flags */
		emit->s_indent = sc->s_indent;
		emit->s_flags = sc->s_flags;

		/* pop state */
		ret = fy_emit_pop_sc(emit, sc);
		assert(!ret);

		/* pop state */
		fy_emit_goto_state(emit, fy_emit_pop_state(emit));
		return ret;

	case FYET_ALIAS:
		fyt_key = fye->alias.anchor;
		simple_key = true;
		break;
	case FYET_SCALAR:
		fyt_key = fye->scalar.value;
		ta = fy_token_text_analyze(fyt_key);
		simple_key = !!(ta->flags & FYTTAF_CAN_BE_SIMPLE_KEY);
		break;
	case FYET_SEQUENCE_START:
		fyt_key = fye->sequence_start.sequence_start;
		simple_key = fy_emit_streaming_sequence_empty(emit);
		break;
	case FYET_MAPPING_START:
		fyt_key = fye->mapping_start.mapping_start;
		simple_key = fy_emit_streaming_mapping_empty(emit);
		break;
	default:
		fy_error(emit->diag, "%s: expected MAPPING_END|ALIAS|SCALAR|SEQUENCE_START|MAPPING_START", __func__);
		return -1;
	}

	ret = fy_emit_push_state(emit, FYES_MAPPING_VALUE);
	if (ret)
		return ret;

	/* reset indent and flags for each key/value pair */
	emit->s_indent = sc->indent;
	emit->s_flags = sc->flags;

	if (!first)
		fy_emit_token_mapping_value_epilog(emit, sc, false, sc->fyt_last_value);

	fy_emit_token_unref(emit, fyp, sc->fyt_last_key);
	sc->fyt_last_key = NULL;
	fy_emit_token_unref(emit, fyp, sc->fyt_last_value);
	sc->fyt_last_value = NULL;

	sc->fyt_last_key = fyt_key;

	fy_emit_token_mapping_key_prolog(emit, sc, fyt_key, simple_key);

	ret = fy_emit_streaming_node(emit, fyp, fyep, sc->flags);

	switch (fye->type) {
	case FYET_ALIAS:
		fye->alias.anchor = NULL;	/* take ownership */
		break;
	case FYET_SCALAR:
		fye->scalar.value = NULL;	/* take ownership */
		break;
	case FYET_SEQUENCE_START:
		fye->sequence_start.sequence_start = NULL;	/* take ownership */
		break;
	case FYET_MAPPING_START:
		fye->mapping_start.mapping_start = NULL;	/* take ownership */
		break;
	default:
		break;
	}

	return ret;
}

static int fy_emit_handle_mapping_value(struct fy_emitter *emit,
					struct fy_parser *fyp,
					struct fy_eventp *fyep,
					bool simple FY_UNUSED)
{
	struct fy_event *fye = &fyep->e;
	struct fy_emit_save_ctx *sc = &emit->s_sc;
	struct fy_token *fyt_value = NULL;
	int ret;

	switch (fye->type) {
	case FYET_ALIAS:
		fyt_value = fye->alias.anchor;
		break;
	case FYET_SCALAR:
		fyt_value = fye->scalar.value;	/* take ownership */
		break;
	case FYET_SEQUENCE_START:
		fyt_value = fye->sequence_start.sequence_start;
		break;
	case FYET_MAPPING_START:
		fyt_value = fye->mapping_start.mapping_start;
		break;
	default:
		fy_error(emit->diag, "%s: expected ALIAS|SCALAR|SEQUENCE_START|MAPPING_START", __func__);
		return -1;
	}

	ret = fy_emit_push_state(emit, FYES_MAPPING_KEY);
	if (ret)
		return ret;

	fy_emit_token_mapping_key_epilog(emit, sc, sc->fyt_last_key);

	sc->fyt_last_value = fyt_value;

	fy_emit_token_mapping_value_prolog(emit, sc, fyt_value);

	ret = fy_emit_streaming_node(emit, fyp, fyep, sc->flags);

	switch (fye->type) {
	case FYET_ALIAS:
		fye->alias.anchor = NULL;	/* take ownership */
		break;
	case FYET_SCALAR:
		fye->scalar.value = NULL;	/* take ownership */
		break;
	case FYET_SEQUENCE_START:
		fye->sequence_start.sequence_start = NULL;	/* take ownership */
		break;
	case FYET_MAPPING_START:
		fye->mapping_start.mapping_start = NULL;	/* take ownership */
		break;
	default:
		break;
	}

	return ret;
}

int fy_emit_event_from_parser(struct fy_emitter *emit, struct fy_parser *fyp, struct fy_event *fye)
{
	struct fy_eventp *fyep;
	int ret;

	if (!emit || !fye)
		return -1;

	/* we're using the raw emitter interface, now mark first state */
	if (emit->state == FYES_NONE)
		emit->state = FYES_STREAM_START;

	/* handle reset (parser error recovery) */
	if (fye->type == FYET_STREAM_START && emit->state != FYES_STREAM_START) {
		if (emit->column != 0)
			fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
		fy_emit_puts_simple(emit, fyewt_document_indicator, "...");
		fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
		emit->state = FYES_STREAM_START;
	}

	fyep = container_of(fye, struct fy_eventp, e);

	fy_eventp_list_add_tail(&emit->queued_events, fyep);

	ret = 0;
	while ((fyep = fy_emit_next_event(emit)) != NULL) {

		switch (emit->state) {
		case FYES_STREAM_START:
			ret = fy_emit_handle_stream_start(emit, fyp, fyep);
			break;

		case FYES_FIRST_DOCUMENT_START:
		case FYES_DOCUMENT_START:
			ret = fy_emit_handle_document_start(emit, fyp, fyep,
					emit->state == FYES_FIRST_DOCUMENT_START);
			break;

		case FYES_DOCUMENT_CONTENT:
			ret = fy_emit_handle_document_content(emit, fyp, fyep);
			break;

		case FYES_DOCUMENT_END:
			ret = fy_emit_handle_document_end(emit, fyp, fyep);
			break;

		case FYES_SEQUENCE_FIRST_ITEM:
		case FYES_SEQUENCE_ITEM:
			ret = fy_emit_handle_sequence_item(emit, fyp, fyep,
					emit->state == FYES_SEQUENCE_FIRST_ITEM);
			break;

		case FYES_MAPPING_FIRST_KEY:
		case FYES_MAPPING_KEY:
			ret = fy_emit_handle_mapping_key(emit, fyp, fyep,
					emit->state == FYES_MAPPING_FIRST_KEY);
			break;

		case FYES_MAPPING_SIMPLE_VALUE:
		case FYES_MAPPING_VALUE:
			ret = fy_emit_handle_mapping_value(emit, fyp, fyep,
					emit->state == FYES_MAPPING_SIMPLE_VALUE);
			break;

		case FYES_END:
			ret = -1;
			break;

		default:
			FY_IMPOSSIBLE_ABORT();
		}

		/* always release the event */
		if (!fyp)
			fy_eventp_release(fyep);
		else
			fy_parse_eventp_recycle(fyp, fyep);

		if (ret)
			break;
	}

	return ret;
}

int fy_emit_event(struct fy_emitter *emit, struct fy_event *fye)
{
	return fy_emit_event_from_parser(emit, NULL, fye);
}

struct fy_document_state *
fy_emitter_get_document_state(struct fy_emitter *emit)
{
	return emit ? emit->fyds : NULL;
}

/* ANSI colors and escapes */
#define A_RESET			"\x1b[0m"
#define A_BLACK			"\x1b[30m"
#define A_RED			"\x1b[31m"
#define A_GREEN			"\x1b[32m"
#define A_YELLOW		"\x1b[33m"
#define A_BLUE			"\x1b[34m"
#define A_MAGENTA		"\x1b[35m"
#define A_CYAN			"\x1b[36m"
#define A_LIGHT_GRAY		"\x1b[37m"	/* dark white is gray */
#define A_GRAY			"\x1b[1;30m"
#define A_BRIGHT_RED		"\x1b[1;31m"
#define A_BRIGHT_GREEN		"\x1b[1;32m"
#define A_BRIGHT_YELLOW		"\x1b[1;33m"
#define A_BRIGHT_BLUE		"\x1b[1;34m"
#define A_BRIGHT_MAGENTA	"\x1b[1;35m"
#define A_BRIGHT_CYAN		"\x1b[1;36m"
#define A_WHITE			"\x1b[1;37m"

static const char *default_colors[FYEWT_COUNT] = {
	[fyewt_document_indicator] 		= A_CYAN,
	[fyewt_tag_directive]			= A_YELLOW,
	[fyewt_version_directive]		= A_YELLOW,
	[fyewt_indent]				= A_GREEN,	// when visible only
	[fyewt_indicator]			= A_YELLOW,	// in extended mode we produce more
	[fyewt_whitespace]			= A_GREEN,	// when visible only
	[fyewt_plain_scalar]			= A_WHITE,
	[fyewt_single_quoted_scalar]		= A_YELLOW,
	[fyewt_double_quoted_scalar]		= A_YELLOW,
	[fyewt_literal_scalar]			= A_YELLOW,
	[fyewt_folded_scalar]			= A_YELLOW,
	[fyewt_anchor]				= A_BRIGHT_GREEN,
	[fyewt_tag]				= A_BRIGHT_GREEN,
	[fyewt_linebreak]			= A_GREEN,	// when visible only
	[fyewt_alias]				= A_BRIGHT_GREEN,
	[fyewt_terminating_zero]		= A_BRIGHT_RED,	// when visible only
	[fyewt_plain_scalar_key]		= A_BRIGHT_CYAN,
	[fyewt_single_quoted_scalar_key]	= A_BRIGHT_CYAN,
	[fyewt_double_quoted_scalar_key]	= A_BRIGHT_CYAN,
	[fyewt_comment]				= A_BRIGHT_BLUE,
	// extended
	[fyewt_indicator_question_mark]		= A_MAGENTA,
	[fyewt_indicator_colon]			= A_MAGENTA,
	[fyewt_indicator_dash]			= A_MAGENTA,
	[fyewt_indicator_left_bracket]		= A_MAGENTA,
	[fyewt_indicator_right_bracket]		= A_MAGENTA,
	[fyewt_indicator_left_brace]		= A_MAGENTA,
	[fyewt_indicator_right_brace]		= A_MAGENTA,
	[fyewt_indicator_comma]			= A_MAGENTA,
	[fyewt_indicator_bar]			= A_MAGENTA,
	[fyewt_indicator_greater]		= A_MAGENTA,
	[fyewt_indicator_single_quote_start]	= A_YELLOW,
	[fyewt_indicator_single_quote_end]	= A_YELLOW,
	[fyewt_indicator_double_quote_start]	= A_YELLOW,
	[fyewt_indicator_double_quote_end]	= A_YELLOW,
	[fyewt_indicator_ambersand]		= A_BRIGHT_GREEN,
	[fyewt_indicator_star]			= A_MAGENTA,
	[fyewt_indicator_chomp]			= A_MAGENTA,
	[fyewt_indicator_explicit_indent]	= A_MAGENTA,
};

static void fy_emitter_fill_default_colors(struct fy_emitter *fye)
{
	unsigned int i;

	for (i = 0; i < FYEWT_COUNT; i++) {
		if (!fye->xcfg.colors[i])
			fye->xcfg.colors[i] = default_colors[i];
	}
}

static FILE *fy_emitter_get_output_fp(struct fy_emitter *fye)
{
	switch (fye->xcfg.xflags & (FYEXCF_OUTPUT_MASK << FYEXCF_OUTPUT_SHIFT)) {
	case FYEXCF_OUTPUT_STDOUT:
		return stdout;
	case FYEXCF_OUTPUT_STDERR:
		return stderr;
	case FYEXCF_OUTPUT_FILE:
		return fye->xcfg.output_fp;
	case FYEXCF_OUTPUT_FILENAME:
		if (!fye->owned_output_fp && fye->xcfg.output_filename)
			fye->owned_output_fp = fopen(fye->xcfg.output_filename, "wb");
		return fye->owned_output_fp;
	default:
		break;
	}
	return NULL;
}

static int fy_emitter_get_output_fd(struct fy_emitter *fye)
{
	switch (fye->xcfg.xflags & (FYEXCF_OUTPUT_MASK << FYEXCF_OUTPUT_SHIFT)) {
	case FYEXCF_OUTPUT_STDOUT:
		return STDOUT_FILENO;
	case FYEXCF_OUTPUT_STDERR:
		return STDERR_FILENO;
	case FYEXCF_OUTPUT_FD:
		return fye->xcfg.output_fd;
	default:
		break;
	}
	return -1;
}


static int fy_emitter_null_output(struct fy_emitter *fye FY_UNUSED,
				  enum fy_emitter_write_type type FY_UNUSED,
				  const char *str FY_UNUSED,
				  int len,
				  void *userdata FY_UNUSED)
{
	return len;
}

static inline ssize_t raw_default_output(FILE *fp, int fd, const char *data, size_t len)
{
	ssize_t wrn, twrn;

	if (fp)
		return fwrite(data, 1, len, fp);

	wrn = 0;
	if (fd >= 0) {
		while (len > 0) {
			do {
				twrn = write(fd, data, len);
			} while (twrn == -1 && errno == EAGAIN);
			if (twrn <= 0)
				return wrn;
			data += twrn;
			len -= (size_t)twrn;
			wrn += twrn;
		}
	}
	return wrn;
}

int fy_emitter_default_output(struct fy_emitter *fye, enum fy_emitter_write_type type, const char *str, int len, void *userdata)
{
	struct fy_emitter_default_output_data d_local, *d;
	FILE *fp;
	int fd;
	int ret, w;
	const char *color = NULL;
	const char *s, *e;
	const char *visible;

	d = userdata;
	if (!d) {
		d = &d_local;
		fp = fye->output_fp;
		fd = fye->output_fd;
		d->colorize = fye->output_colorize;
		d->visible = !!(fye->xcfg.xflags & FYEXCF_VISIBLE_WS);
	} else {
		fp = d->fp;
		fd = -1;
	}

	if (!fp && fd < 0)
		return len;

	s = str;
	e = str + len;

	if (d->colorize) {

		color = fye->xcfg.colors[type];

		/* visible whitespace */
		switch (type) {
		case fyewt_indent:
		case fyewt_whitespace:
		case fyewt_linebreak:
		case fyewt_terminating_zero:

			/* if not visible don't bother */
			if (!d->visible) {
				color = NULL;
				break;
			}

			if (color)
				raw_default_output(fp, fd, color, strlen(color));

			switch (type) {
			case fyewt_indent:
				/* open box - U+2423 */
				visible = "\xe2\x90\xa3";
				break;
			case fyewt_whitespace:
				/* symbol for space - U+2420 */
				/* symbol for interpunct - U+00B7 */
				visible = "\xc2\xb7";
				break;
			case fyewt_linebreak:
				/* down arrow - U+2193 */
				visible = "\xe2\x86\x93" "\n";
				break;
			case fyewt_terminating_zero:
				visible = "\\0";
				break;
			default:
				visible = "";	/* should never happen */
				break;

			}

			while (s < e && (w = fy_utf8_width_by_first_octet(((uint8_t)*s))) > 0) {
				raw_default_output(fp, fd, visible, strlen(visible));
				s += w;
			}

			if (color)
				raw_default_output(fp, fd, A_RESET, strlen(A_RESET));
			return len;

		default:
			break;
		}
	} else
		color = NULL;

	/* don't output the terminating zero */
	if (type == fyewt_terminating_zero)
		return len;

	if (color)
		raw_default_output(fp, fd, color, strlen(color));

	ret = (int)raw_default_output(fp, fd, str, len);

	if (color)
		raw_default_output(fp, fd, A_RESET, strlen(A_RESET));

	return ret;
}

int fy_document_default_emit_to_fp(struct fy_document *fyd, FILE *fp)
{
	struct fy_emitter emit_local, *emit = &emit_local;
	struct fy_emitter_cfg ecfg_local, *ecfg = &ecfg_local;
	struct fy_emitter_default_output_data d_local, *d = &d_local;
	int rc;

	memset(d, 0, sizeof(*d));
	d->fp = fp;
	d->colorize = isatty(fileno(fp));
	d->visible = false;

	memset(ecfg, 0, sizeof(*ecfg));
	ecfg->diag = fyd->diag;
	ecfg->userdata = d;

	rc = fy_emit_setup(emit, ecfg);
	if (rc)
		goto err_setup;

	fy_emit_prepare_document_state(emit, fyd->fyds);

	rc = 0;
	if (fyd->root)
		rc = fy_emit_node_check(emit, fyd->root);

	rc = fy_emit_document_no_check(emit, fyd);
	if (rc)
		goto err_emit;

	fy_emit_cleanup(emit);

	return 0;

err_emit:
	fy_emit_cleanup(emit);
err_setup:
	return -1;
}

int fy_emit_body_node(struct fy_emitter *emit, struct fy_node *fyn)
{
	struct fy_document_iterator *fydi = NULL;
	struct fy_event *fye;
	struct fy_document_iterator_cfg cfg;
	int rc;

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = FYDICF_WANT_BODY_EVENTS;
	cfg.fyd = fyn->fyd;
	cfg.iterate_root = fyn;

	fydi = fy_document_iterator_create_cfg(&cfg);
	if (!fydi)
		goto err_out;

	while ((fye = fy_document_iterator_generate_next(fydi)) != NULL) {
		rc = fy_emit_event(emit, fye);
		if (rc)
			goto err_out;
	}

	fy_document_iterator_destroy(fydi);
	return 0;

err_out:
	fy_document_iterator_destroy(fydi);
	return -1;
}

#ifdef HAVE_GENERIC

static inline bool
fy_emit_generic_is_null_scalar(struct fy_emitter *emit FY_UNUSED,
			       fy_generic v)
{
	return fy_generic_is_string(v) && fy_len(v) == 0;
}

static inline bool
fy_emit_generic_is_json_plain(struct fy_emitter *emit, fy_generic v)
{
	/* must be one of these JSON modes */
	if (!(fy_emit_is_json_mode(emit) || emit->source_json || fy_emit_is_dejson_mode(emit)))
		return false;

	return fy_emit_generic_is_null_scalar(emit, v) ||
	       fy_generic_is_null_type(v) ||
	       fy_generic_is_bool_type(v) ||
	       fy_generic_is_int_type(v) ||
	       fy_generic_is_float_type(v);
}

static enum fy_node_style
fy_emit_generic_scalar_style(struct fy_emitter *emit, fy_generic v, fy_generic_sized_string szstr,
			     int flags, int indent, enum fy_node_style style,
			     fy_generic_sized_string szstr_tag, struct fy_text_analysis *ta)
{
	struct fy_text_analysis ta_mod;

	/* analyze only if needed */
	if (!ta || !(ta->flags & FYTTAF_ANALYZED))
		fy_analyze_scalar_content(szstr.data, szstr.size, FYSS_ANY, fylb_cr_nl, &ta_mod);
	else /* already analyzed, use that */
		ta_mod = *ta;

	if (fy_emit_generic_is_null_scalar(emit, v))
		ta_mod.flags |= FYTTAF_X_NULL_SCALAR;
	if (fy_emit_generic_is_json_plain(emit, v))
		ta_mod.flags |= FYTTAF_X_JSON_PLAIN;
	if ((ta_mod.flags & FYTTAF_X_JSON_PLAIN) &&
			fy_emit_tag_text_is_double_quoted(emit, szstr_tag.data, szstr_tag.size))
		ta_mod.flags |= FYTTAF_X_TAG_DQ;
	if (ta_mod.lbs >= FY_EMIT_MIN_LITERAL_LBS)
		ta_mod.flags |= FYTTAF_X_OVER_MIN_LITERAL_LBS;

	return fy_emit_scalar_style_from_analysis(emit, flags, indent, style, &ta_mod);
}

bool fy_emit_generic_has_comment(struct fy_emitter *emit, fy_generic v,
				 enum fy_comment_placement placement)
{
	return (emit->xcfg.cfg.flags & FYECF_OUTPUT_COMMENTS) &&
		fy_generic_is_valid(fy_generic_get_comment(v, placement));
}

void fy_emit_generic_comment(struct fy_emitter *emit, fy_generic v, int flags, int indent,
			     enum fy_comment_placement placement)
{
	fy_generic vcomm;
	fy_generic_sized_string szstr;

	vcomm = fy_generic_get_comment(v, placement);
	if (fy_generic_is_invalid(vcomm))
		return;

	szstr = fy_cast(vcomm, fy_szstr_empty);
	fy_emit_comment(emit, flags, indent, placement,
			szstr.data, szstr.size, 0, fylb_cr_nl, true);
}

int fy_emit_generic_scalar_prolog(struct fy_emitter *emit, fy_generic v, int flags, int indent)
{
	struct fy_emit_save_ctx *sc = &emit->s_sc;

	if (sc->flags & DDNF_HANGING_INDENT)
		fy_emit_write_indent(emit, sc->indent);

	/* root scalar with a comment needs special handling */
	if ((flags & DDNF_ROOT) && fy_emit_generic_has_comment(emit, v, fycp_top))
		fy_emit_generic_comment(emit, v, flags, indent, fycp_top);

	indent = fy_emit_increase_indent(emit, flags, indent);

	if (!fy_emit_whitespace(emit))
		fy_emit_write_ws(emit);

	return indent;
}

void fy_emit_generic_scalar_epilog(struct fy_emitter *emit FY_UNUSED, fy_generic v, int flags, int indent)
{
	struct fy_emit_save_ctx *sc = &emit->s_sc;

	/* root scalar with a comment needs special handling */
	if ((flags & DDNF_ROOT) && fy_emit_generic_has_comment(emit, v, fycp_right)) {
		fy_emit_generic_comment(emit, v, flags, indent, fycp_right);
		sc->flags |= DDNF_HANGING_INDENT;
	}
}

/* generic */

/* for generic */
struct generic_state {
	fy_generic v;
	fy_generic_sized_string szstr;
	const char *s;
	const char *e;
};

/* simple run of characters, fast and 90% of the plain cases */
static const char *
generic_get_direct(struct fy_emitter *emit FY_UNUSED, struct fy_emit_write_state *state, size_t *lenp)
{
	struct generic_state *gstate = state->user;

	/* scalars that are not strings are directs */
	if (!fy_generic_is_string(gstate->v)) {
		*lenp = gstate->szstr.size;
		return gstate->szstr.data;
	}

	/* maybe do something smart for strings */
	*lenp = 0;
	return NULL;
}

static void
generic_iter_start(struct fy_emitter *emit FY_UNUSED, struct fy_emit_write_state *state)
{
	struct generic_state *gstate = state->user;
	gstate->s = gstate->szstr.data;
	gstate->e = gstate->s + gstate->szstr.size;
}

static void
generic_iter_end(struct fy_emitter *emit FY_UNUSED, struct fy_emit_write_state *state FY_UNUSED)
{
	/* nothing */
}

static int
generic_iter_getc(struct fy_emitter *emit FY_UNUSED, struct fy_emit_write_state *state)
{
	struct generic_state *gstate = state->user;
	int c, w;

	c = fy_utf8_get(gstate->s, (size_t)(gstate->e - gstate->s), &w);
	if (c < 0)
		return c;

	gstate->s += w;
	return c;
}

static int
generic_iter_peekc(struct fy_emitter *emit FY_UNUSED, struct fy_emit_write_state *state)
{
	struct generic_state *gstate = state->user;
	int w;

	/* do not advance */
	return fy_utf8_get(gstate->s, (size_t)(gstate->e - gstate->s), &w);
}

static int generic_iter_getc_dq(struct fy_emitter *emit FY_UNUSED, struct fy_emit_write_state *state, uint8_t *buf, size_t *lenp)
{
	struct generic_state *gstate = state->user;
	int c, w, ww;
	size_t avail;
	uint8_t b;

	if (!lenp || !buf || *lenp < 4)
		return -1;

	avail = (size_t)(gstate->e - gstate->s);

	/* EOF */
	if (!avail)
		return FYUG_EOF;

	b = *(uint8_t *)gstate->s;
	w = fy_utf8_width_by_first_octet(b);

	/* illegal utf8 */
	if (!w) {
		buf[0] = b;
		*lenp = 1;
		gstate->s++;
		return 0;	/* illegal */
	}

	/* not enough in avail */
	if ((size_t)w > avail) {
		memcpy(buf, gstate->s, avail);
		*lenp = avail;
		gstate->s = gstate->e;
		return 0;
	}

	c = fy_utf8_get(gstate->s, (size_t)(gstate->e - gstate->s), &ww);
	if (c < 0) {	/* any kind of error */
		memcpy(buf, gstate->s, w);
		*lenp = w;
		gstate->s += w;
		return 0;
	}

	gstate->s += w;
	if (c == 0) {
		buf[0] = '\0';
		*lenp = 1;
	}

	return c;
}

static const struct fy_emit_write_ops generic_ops = {
	.get_direct = generic_get_direct,
	.iter_start = generic_iter_start,
	.iter_end = generic_iter_end,
	.iter_getc = generic_iter_getc,
	.iter_peekc = generic_iter_peekc,
	.iter_getc_dq = generic_iter_getc_dq,
};

void fy_emit_generic_write_plain(struct fy_emitter *emit, fy_generic v,
				 fy_generic_sized_string szstr,
				 struct fy_text_analysis *ta,
				 int flags, int indent)
{
	struct generic_state gstate;
	struct fy_emit_write_state state;

	gstate.v = v;
	gstate.szstr = szstr;
	/* NOTE s/e/ not initialized; it is on iter_start */

	state.style = FYNS_PLAIN;
	state.lb_mode = fylb_cr_nl;
	state.ta = ta;
	state.user = &gstate;
	state.ops = &generic_ops;

	fy_emit_write_plain_with_state(emit, flags, indent, &state);
}

void fy_emit_generic_write_alias(struct fy_emitter *emit, fy_generic v,
				 fy_generic_sized_string szstr,
				 struct fy_text_analysis *ta,
				 int flags, int indent)
{
	struct generic_state gstate;
	struct fy_emit_write_state state;

	gstate.v = v;
	gstate.szstr = szstr;
	/* NOTE s/e/ not initialized; it is on iter_start */

	state.style = FYNS_ALIAS;
	state.lb_mode = fylb_cr_nl;
	state.ta = ta;
	state.user = &gstate;
	state.ops = &generic_ops;

	fy_emit_write_alias_with_state(emit, flags, indent, &state);
}

void fy_emit_generic_write_single_quoted(struct fy_emitter *emit, fy_generic v,
					 fy_generic_sized_string szstr,
					 struct fy_text_analysis *ta,
					 int flags, int indent)
{
	struct generic_state gstate;
	struct fy_emit_write_state state;
	struct fy_text_analysis ta_local;

	gstate.v = v;
	gstate.szstr = szstr;
	/* NOTE iter not initialized; it is on iter_start */

	if (!ta || !(ta->flags & FYTTAF_ANALYZED)) {
		fy_analyze_scalar_content(szstr.data, szstr.size, FYSS_ANY, fylb_cr_nl, &ta_local);
		ta = &ta_local;
	}

	state.style = FYNS_SINGLE_QUOTED;
	state.lb_mode = fylb_cr_nl;
	state.ta = ta;
	state.user = &gstate;
	state.ops = &generic_ops;

	fy_emit_write_single_quoted_with_state(emit, flags, indent, &state);
}

void fy_emit_generic_write_double_quoted(struct fy_emitter *emit, fy_generic v,
					 fy_generic_sized_string szstr,
					 struct fy_text_analysis *ta,
					 int flags, int indent)
{
	struct generic_state gstate;
	struct fy_emit_write_state state;
	struct fy_text_analysis ta_local;

	gstate.v = v;
	gstate.szstr = szstr;
	/* NOTE iter not initialized; it is on iter_start */

	if (!ta || !(ta->flags & FYTTAF_ANALYZED)) {
		fy_analyze_scalar_content(szstr.data, szstr.size, FYSS_ANY, fylb_cr_nl, &ta_local);
		ta = &ta_local;
	}

	state.style = FYNS_DOUBLE_QUOTED;
	state.lb_mode = fylb_cr_nl;
	state.ta = ta;
	state.user = &gstate;
	state.ops = &generic_ops;

	fy_emit_write_double_quoted_with_state(emit, flags, indent, &state);
}

void fy_emit_generic_write_literal(struct fy_emitter *emit, fy_generic v,
				   fy_generic_sized_string szstr,
				   struct fy_text_analysis *ta,
				   int flags, int indent)
{
	struct generic_state gstate;
	struct fy_emit_write_state state;
	struct fy_text_analysis ta_local;

	gstate.v = v;
	gstate.szstr = szstr;
	/* NOTE iter not initialized; it is on iter_start */

	if (!ta || !(ta->flags & FYTTAF_ANALYZED)) {
		fy_analyze_scalar_content(szstr.data, szstr.size, FYSS_ANY, fylb_cr_nl, &ta_local);
		ta = &ta_local;
	}

	state.style = FYNS_LITERAL;
	state.lb_mode = fylb_cr_nl;
	state.ta = ta;
	state.user = &gstate;
	state.ops = &generic_ops;

	fy_emit_write_literal_with_state(emit, flags, indent, &state);
}

void fy_emit_generic_write_folded(struct fy_emitter *emit, fy_generic v,
				  fy_generic_sized_string szstr,
				  struct fy_text_analysis *ta,
				  int flags, int indent)
{
	struct generic_state gstate;
	struct fy_emit_write_state state;
	struct fy_text_analysis ta_local;

	gstate.v = v;
	gstate.szstr = szstr;
	/* NOTE iter not initialized; it is on iter_start */

	if (!ta || !(ta->flags & FYTTAF_ANALYZED)) {
		fy_analyze_scalar_content(szstr.data, szstr.size, FYSS_ANY, fylb_cr_nl, &ta_local);
		ta = &ta_local;
	}

	state.style = FYNS_FOLDED;
	state.lb_mode = fylb_cr_nl;
	state.ta = ta;
	state.user = &gstate;
	state.ops = &generic_ops;

	fy_emit_write_folded_with_state(emit, flags, indent, &state);
}

void fy_emit_generic_scalar(struct fy_emitter *emit, fy_generic v, int flags, int indent,
			    enum fy_node_style style, fy_generic_sized_string szstr_tag,
			    fy_generic_sized_string *szstrp, struct fy_text_analysis *ta)
{
	fy_generic cv;
	fy_generic_sized_string szstr;

	assert(style != FYNS_FLOW && style != FYNS_BLOCK);

	/* convert everything for now to string */
	if (!szstrp) {
		cv = fy_convert(v, FYGT_STRING);
		szstr = fy_cast(cv, fy_szstr_empty);
	} else {
		szstr = *szstrp;	/* avoid double conversion */
	}

	if (style != FYNS_ALIAS)
		style = fy_emit_generic_scalar_style(emit, v, szstr, flags, indent, style, szstr_tag, ta);

	indent = fy_emit_generic_scalar_prolog(emit, v, flags, indent);

	switch (style) {
	case FYNS_ALIAS:
		fy_emit_generic_write_alias(emit, v, szstr, ta, flags, indent);
		break;
	case FYNS_PLAIN:
		fy_emit_generic_write_plain(emit, v, szstr, ta, flags, indent);
		break;
	case FYNS_DOUBLE_QUOTED:
		fy_emit_generic_write_double_quoted(emit, v, szstr, ta, flags, indent);
		break;
	case FYNS_SINGLE_QUOTED:
		fy_emit_generic_write_single_quoted(emit, v, szstr, ta, flags, indent);
		break;
	case FYNS_LITERAL:
		fy_emit_generic_write_literal(emit, v, szstr, ta, flags, indent);
		break;
	case FYNS_FOLDED:
		fy_emit_generic_write_folded(emit, v, szstr, ta, flags, indent);
		break;

	default:
		break;
	}

	fy_emit_generic_scalar_epilog(emit, v, flags, indent);
}

static void fy_emit_generic_collection_comment_prolog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
						     fy_generic v)
{
	if (!(sc->flags & DDNF_SEQ) && fy_emit_generic_has_comment(emit, v, fycp_top)) {
		fy_emit_generic_comment(emit, v, sc->flags, sc->indent, fycp_top);
		sc->flags |= DDNF_HANGING_INDENT;
	}
}

static void fy_emit_generic_collection_comment_epilog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
						      fy_generic v)
{
	if (fy_emit_generic_has_comment(emit, v, fycp_bottom))
		fy_emit_generic_comment(emit, v, sc->flags, sc->old_indent, fycp_bottom);
}

static void fy_emit_generic_sequence_prolog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					    fy_generic v)
{
	fy_emit_generic_collection_comment_prolog(emit, sc, v);
	fy_emit_sequence_prolog(emit, sc);
}

static void fy_emit_generic_sequence_epilog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					    fy_generic v)
{
	fy_emit_sequence_epilog(emit, sc);
	fy_emit_generic_collection_comment_epilog(emit, sc, v);
}

static void fy_emit_generic_sequence_item_prolog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
						 fy_generic v)
{
	fy_emit_sequence_item_prolog_before_comment(emit, sc);

	if (fy_emit_generic_has_comment(emit, v, fycp_top)) {
		fy_emit_generic_comment(emit, v, sc->flags, sc->indent, fycp_top);
		sc->flags |= DDNF_HANGING_INDENT;
	}

	fy_emit_sequence_item_prolog_after_comment(emit, sc);
}

static void fy_emit_generic_sequence_item_epilog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
						 bool last, fy_generic v)
{
	fy_emit_sequence_item_epilog_before_comment(emit, sc, last);

	if (fy_emit_generic_has_comment(emit, v, fycp_right)) {
		fy_emit_generic_comment(emit, v, sc->flags, sc->indent, fycp_right);
		sc->flags |= DDNF_HANGING_INDENT;
	}
	fy_emit_sequence_item_epilog_after_comment(emit, sc, last);
}

static int
fy_emit_generic_internal(struct fy_emitter *emit, fy_generic v, int flags, int indent, bool is_key,
			 fy_generic_sized_string *scalar_szstr, struct fy_text_analysis *scalar_ta);

void fy_emit_generic_sequence(struct fy_emitter *emit, fy_generic v, int flags, int indent)
{
	fy_generic_sequence_handle seqh;
	fy_generic vitem;
	size_t i;
	bool last;
	struct fy_emit_save_ctx sct, *sc = &sct;

	seqh = fy_cast(v, fy_seq_handle_null);

	memset(sc, 0, sizeof(*sc));

	sc->flags = flags;
	sc->indent = indent;
	sc->flow = !!(flags & DDNF_FLOW);
	sc->old_indent = sc->indent;

	sc->xstyle = fy_generic_get_node_style(v);
	sc->flow_token = sc->xstyle == FYNS_FLOW;
	sc->empty = !seqh || seqh->count == 0;
	sc->oneline_flow = false;	/* yeah, we don't do that */

	fy_emit_generic_sequence_prolog(emit, sc, v);

	if (seqh) {
		for (i = 0; i < seqh->count; i++) {

			vitem = seqh->items[i];
			last = i == (seqh->count - 1);

			fy_emit_generic_sequence_item_prolog(emit, sc, vitem);
			fy_emit_generic_internal(emit, vitem, sc->flags & ~DDNF_ROOT, sc->indent,
						 false, NULL, NULL);
			fy_emit_generic_sequence_item_epilog(emit, sc, last, vitem);
		}
	}

	fy_emit_generic_sequence_epilog(emit, sc, v);
}

static void fy_emit_generic_mapping_prolog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					   fy_generic v)
{
	fy_emit_generic_collection_comment_prolog(emit, sc, v);
	fy_emit_mapping_prolog(emit, sc);
}

static void fy_emit_generic_mapping_epilog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					   fy_generic v)
{
	fy_emit_mapping_epilog(emit, sc);
	fy_emit_generic_collection_comment_epilog(emit, sc, v);
}

static void fy_analyze_generic_scalar_content(fy_generic v, struct fy_text_analysis *ta)
{
	enum fy_scalar_style sstyle;
	fy_generic cv;
	fy_generic_sized_string szstr;

	assert(fy_generic_is_scalar(v));

	if (!ta || (ta->flags & FYTTAF_ANALYZED))
		return;

	sstyle = fy_generic_get_scalar_style(v);

	cv = fy_convert(v, FYGT_STRING);
	szstr = fy_castp(&cv, fy_szstr_empty);

	fy_analyze_scalar_content(szstr.data, szstr.size, sstyle, fylb_cr_nl, ta);
}

static void fy_emit_generic_mapping_key_prolog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					       fy_generic vkey, bool simple_key,
					       struct fy_text_analysis *ta)
{
	bool do_indent, key_over, has_comment;
	struct fy_text_analysis ta_local;

	sc->flags = DDNF_MAP | (sc->flags & DDNF_FLOW);

	if (!fy_emit_sc_oneline(emit, sc) ||
	    ((fy_emit_is_compact(emit) || sc->flow) && emit->column >= fy_emit_width(emit)))
		fy_emit_write_indent(emit, sc->indent);

	/* emit top comment on key token (interstitial mapping comment) */
	if (!sc->flow && fy_emit_generic_has_comment(emit, vkey, fycp_top)) {
		fy_emit_generic_comment(emit, vkey, sc->flags, sc->indent, fycp_top);
		sc->flags |= DDNF_HANGING_INDENT;
	}

	/* if we have a right comment then it's by a definition not a simple key */
	has_comment = fy_emit_generic_has_comment(emit, vkey, fycp_right);

	if (simple_key /* && !has_comment */ ) {
		sc->flags |= DDNF_SIMPLE;
		if (fy_generic_is_scalar(vkey))
			sc->flags |= DDNF_SIMPLE_SCALAR_KEY;
	} else {
		/* do not emit the ? in flow modes at all */
		if (fy_emit_is_flow_mode(emit))
			sc->flags |= DDNF_SIMPLE;
	}

	do_indent = false;
	if (!has_comment && !fy_emit_sc_oneline(emit, sc) && !fy_emit_is_width_infinite(emit)) {

		if (fy_generic_is_collection(vkey))
			/* always indent on non scalar keys */
			key_over = true;
		else {
			if (!ta || !(ta->flags & FYTTAF_ANALYZED)) {
				fy_analyze_generic_scalar_content(vkey, &ta_local);
				ta = &ta_local;
			}
			key_over = (emit->column + ta->maxspan + 2 + (!simple_key ? 2 : 0)) >= fy_emit_width(emit);
		}

		do_indent = key_over;
		if (!do_indent && !fy_emit_is_compact(emit))
			do_indent = !sc->flow || (sc->flags & DDNF_HANGING_INDENT);
	} else
		do_indent = false;

	if (do_indent)
		fy_emit_write_indent(emit, sc->indent);

	/* complex? */
	if (!(sc->flags & DDNF_SIMPLE))
		fy_emit_write_indicator(emit, di_question_mark, sc->flags, sc->indent, fyewt_indicator);
}

static void fy_emit_generic_mapping_key_epilog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
					       fy_generic vkey)
{
	/* if the key is an alias, always output an extra whitespace */
	if (fy_generic_is_alias(vkey))
		fy_emit_write_ws(emit);

	sc->flags &= ~DDNF_MAP;

	if (!(sc->flags & DDNF_SIMPLE)) {

		if (fy_emit_generic_has_comment(emit, vkey, fycp_right)) {
			fy_emit_generic_comment(emit, vkey, sc->flags, sc->indent, fycp_right);
			sc->flags |= DDNF_HANGING_INDENT;
		}

		if (!emit->flow_level && !fy_emit_is_oneline_or_compact(emit))
			fy_emit_write_indent(emit, sc->indent);
		if (!fy_emit_whitespace(emit))
			fy_emit_write_ws(emit);
	} else {
	}

	fy_emit_write_indicator(emit, di_colon, sc->flags, sc->indent, fyewt_indicator);

	sc->flags &= ~DDNF_HANGING_INDENT;

	if ((sc->flags & DDNF_SIMPLE) && fy_emit_generic_has_comment(emit, vkey, fycp_right)) {
		fy_emit_generic_comment(emit, vkey, sc->flags, sc->indent, fycp_right);
		sc->flags |= DDNF_HANGING_INDENT;
	}

	sc->flags = DDNF_MAP | (sc->flags & (DDNF_FLOW | DDNF_HANGING_INDENT));
}

static void fy_emit_generic_mapping_value_prolog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
						 fy_generic vvalue, struct fy_text_analysis *ta)
{
	struct fy_text_analysis ta_local;
	bool value_over;

	if (sc->flags & DDNF_HANGING_INDENT)
		fy_emit_write_indent(emit, sc->indent);

	if (!sc->flow || fy_emit_sc_oneline(emit, sc))
		return;

	/* don't do anything for those cases */
	if (fy_generic_is_scalar(vvalue) && !fy_emit_is_width_infinite(emit)) {
		if (!ta || !(ta->flags & FYTTAF_ANALYZED)) {
			fy_analyze_generic_scalar_content(vvalue, &ta_local);
			ta = &ta_local;
		}
		value_over = (emit->column + ta->maxspan) >= fy_emit_width(emit);
		if (value_over) {
			sc->indent = fy_emit_increase_indent(emit, sc->flags, sc->indent);
			fy_emit_write_indent(emit, sc->indent);
		}
	}
}

static void fy_emit_generic_mapping_value_epilog(struct fy_emitter *emit, struct fy_emit_save_ctx *sc,
						 bool last, fy_generic vvalue)
{
	bool needs_hanging_indent;

	if ((sc->flow || fy_emit_is_json_mode(emit)) && !last)
		fy_emit_write_indicator(emit, di_comma, sc->flags, sc->indent, fyewt_indicator);

	if (fy_emit_generic_has_comment(emit, vvalue, fycp_right)) {
		fy_emit_generic_comment(emit, vvalue, sc->flags, sc->indent, fycp_right);
		sc->flags |= DDNF_HANGING_INDENT;
	}

	needs_hanging_indent = sc->flow && !fy_emit_sc_oneline(emit, sc) && !sc->empty;

	if (last && needs_hanging_indent && (sc->flags & DDNF_HANGING_INDENT))
		fy_emit_write_indent(emit, sc->old_indent);
	else if (needs_hanging_indent)
		sc->flags |= DDNF_HANGING_INDENT;

	sc->flags &= ~DDNF_MAP;
}

void fy_emit_generic_mapping(struct fy_emitter *emit, fy_generic v, int flags, int indent)
{
	fy_generic_mapping_handle maph;
	fy_generic vkey, vvalue;
	bool last, simple_key;
	struct fy_text_analysis tak, tav;
	size_t i;
	struct fy_emit_save_ctx sct, *sc = &sct;

	maph = fy_cast(v, fy_map_handle_null);

	memset(sc, 0, sizeof(*sc));

	sc->flags = flags;
	sc->indent = indent;
	sc->flow = !!(flags & DDNF_FLOW);
	sc->old_indent = sc->indent;

	sc->xstyle = fy_generic_get_node_style(v);
	sc->flow_token = sc->xstyle == FYNS_FLOW;
	sc->empty = !maph || maph->count == 0;
	sc->oneline_flow = false;	/* yeah, we don't do that */

	fy_emit_generic_mapping_prolog(emit, sc, v);

	/* XXX emit->xcfg.cfg.flags & (FYECF_SORT_KEYS | FYECF_STRIP_EMPTY_KV)) */

	if (maph) {
		for (i = 0; i < maph->count; i++) {

			last = i == maph->count - 1;
			vkey = maph->pairs[i].key;
			vvalue = maph->pairs[i].value;

			/* just the flags */
			tak.flags = 0;
			tav.flags = 0;

			simple_key = false;
			if (fy_generic_is_scalar(vkey)) {

				fy_analyze_generic_scalar_content(vkey, &tak);

				simple_key = fy_emit_is_json_mode(emit) ||
					     (tak.flags & FYTTAF_CAN_BE_SIMPLE_KEY);

			} else
				simple_key = fy_len(vkey) == 0;	/* simple when no items */

			/* special handling for compact mode simple key + scalar value*/
			if (!fy_emit_is_width_infinite(emit) &&
					fy_emit_is_compact(emit) && simple_key &&
					fy_generic_is_scalar(vvalue)) {

				fy_analyze_generic_scalar_content(vvalue, &tav);

				/* for plain key + value if we go over the width */
				if ((tav.flags & FYTTAF_CAN_BE_PLAIN) &&
				   (emit->column + tak.maxspan + 2 + tav.maxspan + 1) >= fy_emit_width(emit))
					fy_emit_write_indent(emit, sc->indent);
			}

			fy_emit_generic_mapping_key_prolog(emit, sc, vkey, simple_key, &tak);
			fy_emit_generic_internal(emit, vkey, (sc->flags & ~DDNF_ROOT), sc->indent,
						 true, NULL, &tak);
			fy_emit_generic_mapping_key_epilog(emit, sc, vkey);

			fy_emit_generic_mapping_value_prolog(emit, sc, vvalue, &tav);
			fy_emit_generic_internal(emit, vvalue, (sc->flags & ~DDNF_ROOT), sc->indent,
						 false, NULL, &tav);
			fy_emit_generic_mapping_value_epilog(emit, sc, last, vvalue);
		}
	}

	fy_emit_generic_mapping_epilog(emit, sc, v);
}

static int
fy_emit_generic_internal(struct fy_emitter *emit, fy_generic v, int flags, int indent, bool is_key,
			 fy_generic_sized_string *scalar_szstr, struct fy_text_analysis *scalar_ta)
{
	struct fy_token *fyt_td;
	enum fy_node_type type;
	fy_generic vtag, vanchor;
	fy_generic_sized_string szstr_anchor, szstr_tag, szstr_td_handle, szstr_td_prefix;
	struct fy_tag_scan_info info;
	char *tag_data = NULL;
	size_t tag_size;
	enum fy_node_style style;
	int rc;

	style = FYNS_ANY;
	switch (fy_generic_get_type(v)) {
	case FYGT_INVALID:
		return -1;
	case FYGT_ALIAS:
		style = FYNS_ALIAS;
		/* fall-through */
	case FYGT_NULL:
	case FYGT_BOOL:
	case FYGT_INT:
	case FYGT_FLOAT:
	case FYGT_STRING:
		type = FYNT_SCALAR;
		break;
	case FYGT_SEQUENCE:
		type = FYNT_SEQUENCE;
		break;
	case FYGT_MAPPING:
		type = FYNT_MAPPING;
		break;
	default:
		FY_IMPOSSIBLE_ABORT();
	}

	if (style != FYNS_ALIAS) {
		vanchor = fy_generic_get_anchor(v);
	} else {
		vanchor = fy_invalid;
	}
	vtag = fy_generic_get_tag(v);

	szstr_anchor = fy_cast(vanchor, fy_szstr_empty);
	szstr_tag = fy_cast(vtag, fy_szstr_empty);

	if (szstr_tag.data) {
		tag_data = fy_document_state_format_tag_alloc(emit->fyds, szstr_tag.data, szstr_tag.size, &tag_size);
		if (!tag_data)
			goto err_out;

		memset(&info, 0, sizeof(info));

		rc = fy_tag_scan(tag_data, tag_size, &info);
		if (rc)
			goto err_out;

		fyt_td = fy_document_state_lookup_tag_directive(emit->fyds,
				tag_data + info.prefix_length, info.handle_length);
		if (!fyt_td)
			goto err_out;

		szstr_td_handle.data = fy_tag_directive_token_handle(fyt_td, &szstr_td_handle.size);
		szstr_td_prefix.data = fy_tag_directive_token_prefix(fyt_td, &szstr_td_prefix.size);

	} else {
		tag_data = NULL;
		tag_size = 0;
		szstr_td_handle = szstr_td_prefix = fy_szstr_empty;
	}

	fy_emit_common_text_preamble(emit,
			szstr_anchor.data, szstr_anchor.size,
			szstr_tag.data, szstr_tag.size,
			szstr_td_handle.data, szstr_td_handle.size,
			szstr_td_prefix.data, szstr_td_prefix.size,
			flags, indent, type);

	if (tag_data)
		free(tag_data);
	tag_data = NULL;

	switch (type) {
	case FYNT_SCALAR:

		if (style == FYNS_ANY) {
			style = (fy_emit_is_json_mode(emit) && is_key) ? FYNS_DOUBLE_QUOTED : FYNS_ANY;
			if (style == FYNS_ANY)
				style = fy_generic_get_node_style(v);
		}

		fy_emit_generic_scalar(emit, v, flags, indent, style, szstr_tag,
				       scalar_szstr, scalar_ta);

		break;
	case FYNT_SEQUENCE:
	case FYNT_MAPPING:
		if (type == FYNT_SEQUENCE)
			fy_emit_generic_sequence(emit, v, flags, indent);
		else
			fy_emit_generic_mapping(emit, v, flags, indent);
		break;

	default:
		break;
	}
	return 0;

err_out:
	if (tag_data)
		free(tag_data);
	return -1;
}

int
fy_emit_generic_vdir(struct fy_emitter *emit, fy_generic vdir)
{
	struct fy_document_state *fyds;
	fy_generic vds, vroot;
	int i, count, rc;

	if (!emit || fy_generic_is_invalid(vdir))
		return -1;

	/* FYET_STREAM_START */
	if (emit->state != FYES_STREAM_START) {
		if (emit->column != 0)
			fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
		if (emit->state != FYES_NONE) {
			fy_emit_puts_simple(emit, fyewt_document_indicator, "...");
			fy_emit_putc_simple(emit, fyewt_linebreak, '\n');
		}
		fy_emit_goto_state(emit, FYES_STREAM_START);
	}

	count = fy_generic_dir_get_document_count(vdir);
	if (count < 0)
		goto err_out;

	for (i = 0; i < count; i++) {

		vds = fy_generic_dir_get_document_vds(vdir, (size_t)i);
		if (!fy_generic_is_valid(vds))
			goto err_out;

		fyds = fy_generic_vds_get_document_state(vds);
		if (!fyds)
			goto err_out;

		/* FYET_DOCUMENT_START */
		fy_emit_prepare_document_state(emit, fyds);
		fy_emit_common_document_start(emit, fyds, false);
		fy_emit_goto_state(emit, FYES_DOCUMENT_CONTENT);

		vroot = fy_generic_vds_get_root(vds);

		rc = fy_emit_generic_internal(emit, vroot, DDNF_ROOT, -1, false, NULL, NULL);
		if (rc)
			goto err_out;

		/* FYET_DOCUMENT_END */
		fy_emit_common_document_end(emit, true, fyds->end_implicit);
		fy_emit_reset(emit, false);
		fy_emit_goto_state(emit, FYES_DOCUMENT_START);
		emit->flags |= FYEF_HAD_DOCUMENT_END_OUTPUT;

		fy_document_state_unref(fyds);
	}

	/* FYET_STREAM_END */
	fy_emit_goto_state(emit, FYES_END);

	return 0;

err_out:
	return -1;
}

#endif
