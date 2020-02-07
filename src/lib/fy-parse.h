/*
 * fy-parse.h - YAML parser internal header file
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_PARSE_H
#define FY_PARSE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <libfyaml.h>

#include "fy-ctype.h"
#include "fy-utf8.h"
#include "fy-list.h"
#include "fy-typelist.h"
#include "fy-types.h"
#include "fy-diag.h"
#include "fy-dump.h"
#include "fy-atom.h"
#include "fy-input.h"
#include "fy-ctype.h"
#include "fy-token.h"
#include "fy-event.h"
#include "fy-docstate.h"
#include "fy-doc.h"
#include "fy-emit.h"
#include "fy-accel.h"

struct fy_parser;
struct fy_input;

enum fy_flow_type {
	FYFT_NONE,
	FYFT_MAP,
	FYFT_SEQUENCE,
};

struct fy_flow {
	struct list_head node;
	enum fy_flow_type flow;
	int pending_complex_key_column;
	struct fy_mark pending_complex_key_mark;
	int parent_indent;
};
FY_PARSE_TYPE_DECL(flow);

struct fy_indent {
	struct list_head node;
	int indent;
	bool generated_block_map : 1;
};
FY_PARSE_TYPE_DECL(indent);

struct fy_token;

struct fy_simple_key {
	struct list_head node;
	struct fy_mark mark;
	struct fy_mark end_mark;
	struct fy_token *token;	/* associated token */
	int flow_level;
	bool required : 1;
	bool possible : 1;
	bool empty : 1;
};
FY_PARSE_TYPE_DECL(simple_key);

struct fy_document_state;

enum fy_parser_state {
	/** none state */
	FYPS_NONE,
	/** Expect STREAM-START. */
	FYPS_STREAM_START,
	/** Expect the beginning of an implicit document. */
	FYPS_IMPLICIT_DOCUMENT_START,
	/** Expect DOCUMENT-START. */
	FYPS_DOCUMENT_START,
	/** Expect the content of a document. */
	FYPS_DOCUMENT_CONTENT,
	/** Expect DOCUMENT-END. */
	FYPS_DOCUMENT_END,
	/** Expect a block node. */
	FYPS_BLOCK_NODE,
	/** Expect a block node or indentless sequence. */
	FYPS_BLOCK_NODE_OR_INDENTLESS_SEQUENCE,
	/** Expect a flow node. */
	FYPS_FLOW_NODE,
	/** Expect the first entry of a block sequence. */
	FYPS_BLOCK_SEQUENCE_FIRST_ENTRY,
	/** Expect an entry of a block sequence. */
	FYPS_BLOCK_SEQUENCE_ENTRY,
	/** Expect an entry of an indentless sequence. */
	FYPS_INDENTLESS_SEQUENCE_ENTRY,
	/** Expect the first key of a block mapping. */
	FYPS_BLOCK_MAPPING_FIRST_KEY,
	/** Expect a block mapping key. */
	FYPS_BLOCK_MAPPING_KEY,
	/** Expect a block mapping value. */
	FYPS_BLOCK_MAPPING_VALUE,
	/** Expect the first entry of a flow sequence. */
	FYPS_FLOW_SEQUENCE_FIRST_ENTRY,
	/** Expect an entry of a flow sequence. */
	FYPS_FLOW_SEQUENCE_ENTRY,
	/** Expect a key of an ordered mapping. */
	FYPS_FLOW_SEQUENCE_ENTRY_MAPPING_KEY,
	/** Expect a value of an ordered mapping. */
	FYPS_FLOW_SEQUENCE_ENTRY_MAPPING_VALUE,
	/** Expect the and of an ordered mapping entry. */
	FYPS_FLOW_SEQUENCE_ENTRY_MAPPING_END,
	/** Expect the first key of a flow mapping. */
	FYPS_FLOW_MAPPING_FIRST_KEY,
	/** Expect a key of a flow mapping. */
	FYPS_FLOW_MAPPING_KEY,
	/** Expect a value of a flow mapping. */
	FYPS_FLOW_MAPPING_VALUE,
	/** Expect an empty value of a flow mapping. */
	FYPS_FLOW_MAPPING_EMPTY_VALUE,
	/** Expect only stream end */
	FYPS_SINGLE_DOCUMENT_END,
	/** Expect nothing. */
	FYPS_END
};

struct fy_parse_state_log {
	struct list_head node;
	enum fy_parser_state state;
};
FY_PARSE_TYPE_DECL(parse_state_log);

struct fy_parser {
	struct fy_parse_cfg cfg;

	struct fy_input_list queued_inputs;	/* all the inputs queued */
	struct fy_input *current_input;
	size_t current_pos;		/* from start of stream */
	size_t current_input_pos;	/* from start of input */
	const void *current_ptr;	/* current pointer into the buffer */
	int current_c;			/* current utf8 character at current_ptr (-1 if not cached) */
	int current_w;			/* current utf8 character width */
	size_t current_left;		/* currently left characters into the buffer */

	int line;			/* always on input */
	int column;
	int tabsize;			/* very experimental tab size for indent purposes */
	int nontab_column;		/* column without accounting for tabs */

	bool suppress_recycling : 1;
	bool stream_start_produced : 1;
	bool stream_end_produced : 1;
	bool simple_key_allowed : 1;
	bool stream_error : 1;
	bool generated_block_map : 1;
	bool last_was_comma : 1;
	bool document_has_content : 1;
	bool document_first_content_token : 1;
	bool bare_document_only : 1;		/* no document start indicators allowed, no directives */
	bool stream_has_content : 1;
	int flow_level;
	int pending_complex_key_column;
	struct fy_mark pending_complex_key_mark;
	int last_block_mapping_key_line;
	struct fy_mark last_comma_mark;

	/* copy of stream_end token */
	struct fy_token *stream_end_token;

	/* produced tokens, but not yet consumed */
	struct fy_token_list queued_tokens;
	int token_activity_counter;

	/* last comment */
	struct fy_atom last_comment;

	/* indent stack */
	struct fy_indent_list indent_stack;
	int indent;
	int parent_indent;
	/* simple key stack */
	struct fy_simple_key_list simple_keys;
	/* state stack */
	enum fy_parser_state state;
	struct fy_parse_state_log_list state_stack;

	/* current parse document */
	struct fy_document_state *current_document_state;
	struct fy_document_state *default_document_state;
	bool next_single_document;

	/* flow stack */
	enum fy_flow_type flow;
	struct fy_flow_list flow_stack;

	/* recycling lists */
	struct fy_indent_list recycled_indent;
	struct fy_simple_key_list recycled_simple_key;
	struct fy_parse_state_log_list recycled_parse_state_log;
	struct fy_eventp_list recycled_eventp;
	struct fy_flow_list recycled_flow;

	/* the diagnostic object */
	struct fy_diag *diag;

	int err_term_width;
	int err_term_height;
};

static inline bool fyp_json_mode(const struct fy_parser *fyp)
{
	return fyp && fyp->current_input && fyp->current_input->json_mode;
}

static inline bool fyp_is_lb(const struct fy_parser *fyp, int c)
{
	return fyp && fy_input_is_lb(fyp->current_input, c);
}

static inline bool fyp_is_lbz(const struct fy_parser *fyp, int c)
{
	return fyp && fy_input_is_lbz(fyp->current_input, c);
}

static inline bool fyp_is_blankz(const struct fy_parser *fyp, int c)
{
	return fyp && fy_input_is_blankz(fyp->current_input, c);
}

static inline bool fyp_is_flow_ws(const struct fy_parser *fyp, int c)
{
	return fyp && fy_input_is_flow_ws(fyp->current_input, c);
}

static inline bool fyp_is_flow_blank(const struct fy_parser *fyp, int c)
{
	return fyp_is_flow_ws(fyp, c);
}

static inline bool fyp_is_flow_blankz(const struct fy_parser *fyp, int c)
{
	return fyp && fy_input_is_flow_blankz(fyp->current_input, c);
}

int fy_parse_setup(struct fy_parser *fyp, const struct fy_parse_cfg *cfg);
void fy_parse_cleanup(struct fy_parser *fyp);

int fy_parse_input_append(struct fy_parser *fyp, const struct fy_input_cfg *fyic);

struct fy_token *fy_scan(struct fy_parser *fyp);

const void *fy_ptr_slow_path(struct fy_parser *fyp, size_t *leftp);
const void *fy_ensure_lookahead_slow_path(struct fy_parser *fyp, size_t size, size_t *leftp);

/* only allowed if input does not update */
static inline void fy_get_mark(struct fy_parser *fyp, struct fy_mark *fym)
{
	if (!fyp) {
		memset(fym, 0, sizeof(*fym));
		return;
	}

	fym->input_pos = fyp->current_input_pos;
	fym->line = fyp->line;
	fym->column = fyp->column;
}

static inline const void *fy_ptr(struct fy_parser *fyp, size_t *leftp)
{
	if (fyp->current_ptr) {
		if (leftp)
			*leftp = fyp->current_left;
		return fyp->current_ptr;
	}

	return fy_ptr_slow_path(fyp, leftp);
}

static inline const void *fy_ensure_lookahead(struct fy_parser *fyp, size_t size, size_t *leftp)
{
	if (fyp->current_ptr && fyp->current_left >= size) {
		if (leftp)
			*leftp = fyp->current_left;
		return fyp->current_ptr;
	}
	return fy_ensure_lookahead_slow_path(fyp, size, leftp);
}

/* advance the given number of ascii characters, not utf8 */
static inline void fy_advance_octets(struct fy_parser *fyp, size_t advance)
{
	struct fy_input *fyi;
	size_t left __FY_DEBUG_UNUSED__;

	assert(fyp);
	assert(fyp->current_input);

	assert(fyp->current_left >= advance);

	fyi = fyp->current_input;

	/* tokens cannot cross boundaries */
	switch (fyp->current_input->cfg.type) {
	case fyit_file:
		if (fyi->file.addr) {
			left = fyi->file.length - fyp->current_input_pos;
			break;
		}
		/* fall-through */

	case fyit_stream:
		left = fyi->read - fyp->current_input_pos;
		break;

	case fyit_memory:
		left = fyi->cfg.memory.size - fyp->current_input_pos;
		break;

	case fyit_alloc:
		left = fyi->cfg.alloc.size - fyp->current_input_pos;
		break;

	default:
		assert(0);	/* no streams */
		break;
	}

	assert(left >= advance);

	fyp->current_input_pos += advance;
	fyp->current_ptr += advance;
	fyp->current_left -= advance;
	fyp->current_pos += advance;

	fyp->current_c = fy_utf8_get(fyp->current_ptr, fyp->current_left, &fyp->current_w);
}

/* compare string at the current point (n max) */
static inline int fy_parse_strncmp(struct fy_parser *fyp, const char *str, size_t n)
{
	const char *p;
	int ret;

	p = fy_ensure_lookahead(fyp, n, NULL);
	if (!p)
		return -1;
	ret = strncmp(p, str, n);
	return ret ? 1 : 0;
}

static inline int fy_parse_peek_at_offset(struct fy_parser *fyp, size_t offset)
{
	const uint8_t *p;
	size_t left;
	int w;

	if (offset == 0 && fyp->current_w)
		return fyp->current_c;

	/* ensure that the first octet at least is pulled in */
	p = fy_ensure_lookahead(fyp, offset + 1, &left);
	if (!p)
		return FYUG_EOF;

	/* get width by first octet */
	w = fy_utf8_width_by_first_octet(p[offset]);
	if (!w)
		return FYUG_INV;

	/* make sure that there's enough to cover the utf8 width */
	if (offset + w > left) {
		p = fy_ensure_lookahead(fyp, offset + w, &left);
		if (!p)
			return FYUG_PARTIAL;
	}

	return fy_utf8_get(p + offset, left - offset, &w);
}

static inline int fy_parse_peek_at_internal(struct fy_parser *fyp, int pos, ssize_t *offsetp)
{
	int i, c;
	size_t offset;

	if (!offsetp || *offsetp < 0) {
		for (i = 0, offset = 0; i < pos; i++, offset += fy_utf8_width(c)) {
			c = fy_parse_peek_at_offset(fyp, offset);
			if (c < 0)
				return c;
		}
	} else
		offset = (size_t)*offsetp;

	c = fy_parse_peek_at_offset(fyp, offset);

	if (offsetp)
		*offsetp = offset + fy_utf8_width(c);

	return c;
}

static inline bool fy_is_blank_at_offset(struct fy_parser *fyp, size_t offset)
{
	return fy_is_blank(fy_parse_peek_at_offset(fyp, offset));
}

static inline bool fy_is_blankz_at_offset(struct fy_parser *fyp, size_t offset)
{
	return fyp_is_blankz(fyp, fy_parse_peek_at_offset(fyp, offset));
}

static inline int fy_parse_peek_at(struct fy_parser *fyp, int pos)
{
	return fy_parse_peek_at_internal(fyp, pos, NULL);
}

static inline int fy_parse_peek(struct fy_parser *fyp)
{
	return fy_parse_peek_at_offset(fyp, 0);
}

static inline void fy_advance(struct fy_parser *fyp, int c)
{
	bool is_line_break = false;

	/* skip this character */
	fy_advance_octets(fyp, fy_utf8_width(c));

	/* first check for CR/LF */
	if (c == '\r' && fy_parse_peek(fyp) == '\n') {
		fy_advance_octets(fyp, 1);
		is_line_break = true;
	} else if (fyp_is_lb(fyp, c))
		is_line_break = true;

	if (is_line_break) {
		fyp->column = 0;
		fyp->nontab_column = 0;
		fyp->line++;
	} else if (fyp->tabsize && fy_is_tab(c)) {
		fyp->column += (fyp->tabsize - (fyp->column % fyp->tabsize));
		fyp->nontab_column++;
	} else {
		fyp->column++;
		fyp->nontab_column++;
	}
}

static inline int fy_parse_get(struct fy_parser *fyp)
{
	int value;

	value = fy_parse_peek(fyp);
	if (value < 0)
		return value;

	fy_advance(fyp, value);

	return value;
}

static inline int fy_advance_by(struct fy_parser *fyp, int count)
{
	int i, c;

	for (i = 0; i < count; i++) {
		c = fy_parse_get(fyp);
		if (c < 0)
			break;
	}
	return i ? i : -1;
}

/* compare string at the current point */
static inline bool fy_parse_strcmp(struct fy_parser *fyp, const char *str)
{
	return fy_parse_strncmp(fyp, str, strlen(str));
}

struct fy_eventp *fy_parse_private(struct fy_parser *fyp);

extern const char *fy_event_type_txt[];

FILE *fy_parser_get_error_fp(struct fy_parser *fyp);
enum fy_parse_cfg_flags fy_parser_get_cfg_flags(const struct fy_parser *fyp);
bool fy_parser_is_colorized(struct fy_parser *fyp);

struct fy_token *fy_document_event_get_token(struct fy_event *fye);

#define FY_DEFAULT_YAML_VERSION_MAJOR	1
#define FY_DEFAULT_YAML_VERSION_MINOR	1

extern const struct fy_tag * const fy_default_tags[];

bool fy_tag_handle_is_default(const char *handle, size_t handle_size);
bool fy_tag_is_default(const char *handle, size_t handle_size,
		       const char *prefix, size_t prefix_size);
bool fy_token_tag_directive_is_overridable(struct fy_token *fyt_td);

int fy_parser_set_default_document_state(struct fy_parser *fyp,
					 struct fy_document_state *fyds);
void fy_parser_set_next_single_document(struct fy_parser *fyp);

void *fy_alloc_default(void *userdata, size_t size);
void fy_free_default(void *userdata, void *ptr);
void *fy_realloc_default(void *userdata, void *ptr, size_t size);

#endif
