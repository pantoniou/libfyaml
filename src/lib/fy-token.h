/*
 * fy-token.h - YAML token methods header
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_TOKEN_H
#define FY_TOKEN_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

#include <libfyaml.h>

struct fy_parser;
struct fy_token;
struct fy_document;

enum fy_token_type {
	/* non-content token types */
	FYTT_NONE,
	FYTT_STREAM_START,
	FYTT_STREAM_END,
	FYTT_VERSION_DIRECTIVE,
	FYTT_TAG_DIRECTIVE,
	FYTT_DOCUMENT_START,
	FYTT_DOCUMENT_END,
	/* content token types */
	FYTT_BLOCK_SEQUENCE_START,
	FYTT_BLOCK_MAPPING_START,
	FYTT_BLOCK_END,
	FYTT_FLOW_SEQUENCE_START,
	FYTT_FLOW_SEQUENCE_END,
	FYTT_FLOW_MAPPING_START,
	FYTT_FLOW_MAPPING_END,
	FYTT_BLOCK_ENTRY,
	FYTT_FLOW_ENTRY,
	FYTT_KEY,
	FYTT_VALUE,
	FYTT_ALIAS,
	FYTT_ANCHOR,
	FYTT_TAG,
	FYTT_SCALAR,
};

static inline bool fy_token_type_is_content(enum fy_token_type type)
{
	return type >= FYTT_BLOCK_SEQUENCE_START;
}

/* analyze content flags */
#define FYACF_EMPTY		0x00001	/* is empty (only ws & lb) */
#define FYACF_LB		0x00002	/* has a linebreak */
#define FYACF_BLOCK_PLAIN	0x00004	/* can be a plain scalar in block context */
#define FYACF_FLOW_PLAIN	0x00008	/* can be a plain scalar in flow context */
#define FYACF_PRINTABLE		0x00010	/* every character is printable */
#define FYACF_SINGLE_QUOTED	0x00020	/* can be a single quoted scalar */
#define FYACF_DOUBLE_QUOTED	0x00040	/* can be a double quoted scalar */
#define FYACF_CONTAINS_ZERO	0x00080	/* contains a zero */
#define FYACF_DOC_IND		0x00100	/* contains document indicators */
#define FYACF_CONSECUTIVE_LB 	0x00200	/* has consecutive linebreaks */
#define FYACF_SIMPLE_KEY	0x00400	/* can be a simple key */
#define FYACF_WS		0x00800	/* has at least one whitespace */
#define FYACF_STARTS_WITH_WS	0x01000	/* starts with whitespace */
#define FYACF_STARTS_WITH_LB	0x02000	/* starts with whitespace */
#define FYACF_ENDS_WITH_WS	0x04000	/* ends with whitespace */
#define FYACF_ENDS_WITH_LB	0x08000	/* ends with linebreak */
#define FYACF_TRAILING_LB	0x10000	/* ends with trailing lb > 1 */
#define FYACF_SIZE0		0x20000 /* contains absolutely nothing */

enum fy_comment_placement {
	fycp_top,
	fycp_right,
	fycp_bottom,
	fycp_max,
};

struct fy_token {
	struct list_head node;
	enum fy_token_type type;
	int refs;		/* when on document, we switch to reference counting */
	int analyze_flags;	/* cache of the analysis flags */
	size_t text_len;
	const char *text;
	char *text0;		/* this is allocated */
	struct fy_atom handle;
	struct fy_atom comment[fycp_max];
	union  {
		struct {
			unsigned int tag_length;	/* from start */
			unsigned int uri_length;	/* from end */
		} tag_directive;
		struct {
			enum fy_scalar_style style;
		} scalar;
		struct {
			unsigned int skip;
			unsigned int handle_length;
			unsigned int suffix_length;
			struct fy_token *fyt_td;
		} tag;
	};
};
FY_PARSE_TYPE_DECL(token);

struct fy_token *fy_token_alloc(struct fy_document_state *fyds);
void fy_token_free(struct fy_token *fyt);
struct fy_token *fy_token_ref(struct fy_token *fyt);
void fy_token_unref(struct fy_token *fyt);
void fy_token_list_unref_all(struct fy_token_list *fytl);

struct fy_token *fy_parse_token_alloc(struct fy_parser *fyp);
struct fy_token *fy_parse_token_new(struct fy_parser *fyp, enum fy_token_type type);
void fy_parse_token_free(struct fy_parser *fyp, struct fy_token *fyt);

void fy_parse_token_free(struct fy_parser *fyp, struct fy_token *fyt);

int fy_tag_token_format_text_length(const struct fy_token *fyt);
const char *fy_tag_token_format_text(const struct fy_token *fyt, char *buf, size_t maxsz);

struct fy_token *fy_token_create(struct fy_parser *fyp, enum fy_token_type type, ...);
struct fy_token *fy_token_vqueue(struct fy_parser *fyp, enum fy_token_type type, va_list ap);
struct fy_token *fy_token_queue(struct fy_parser *fyp, enum fy_token_type type, ...);

struct fy_token *fy_token_queue_internal(struct fy_parser *fyp, struct fy_token_list *fytl,
					 enum fy_token_type type, ...);

int fy_token_format_text_length(struct fy_token *fyt);
const char *fy_token_format_text(struct fy_token *fyt, char *buf, size_t maxsz);

/* non-parser token methods */
struct fy_atom *fy_token_atom(struct fy_token *fyt);
const struct fy_mark *fy_token_start_mark(struct fy_token *fyt);
const struct fy_mark *fy_token_end_mark(struct fy_token *fyt);

static inline size_t fy_token_start_pos(struct fy_token *fyt)
{
	const struct fy_mark *start_mark;

	if (!fyt)
		return (size_t)-1;

	start_mark = fy_token_start_mark(fyt);
	return start_mark ? start_mark->input_pos : (size_t)-1;
}

static inline size_t fy_token_end_pos(struct fy_token *fyt)
{
	const struct fy_mark *end_mark;

	if (!fyt)
		return (size_t)-1;

	end_mark = fy_token_end_mark(fyt);
	return end_mark ? end_mark->input_pos : (size_t)-1;
}

static inline int fy_token_start_line(struct fy_token *fyt)
{
	const struct fy_mark *start_mark;

	if (!fyt)
		return -1;

	start_mark = fy_token_start_mark(fyt);
	return start_mark ? start_mark->line : -1;
}

static inline int fy_token_start_column(struct fy_token *fyt)
{
	const struct fy_mark *start_mark;

	if (!fyt)
		return -1;

	start_mark = fy_token_start_mark(fyt);
	return start_mark ? start_mark->column : -1;
}

static inline int fy_token_end_line(struct fy_token *fyt)
{
	const struct fy_mark *end_mark;

	if (!fyt)
		return -1;

	end_mark = fy_token_end_mark(fyt);
	return end_mark ? end_mark->line : -1;
}

static inline int fy_token_end_column(struct fy_token *fyt)
{
	const struct fy_mark *end_mark;

	if (!fyt)
		return -1;

	end_mark = fy_token_end_mark(fyt);
	return end_mark ? end_mark->column : -1;
}

static inline bool fy_token_is_multiline(struct fy_token *fyt)
{
	const struct fy_mark *start_mark, *end_mark;

	if (!fyt)
		return false;

	start_mark = fy_token_start_mark(fyt);
	end_mark = fy_token_end_mark(fyt);
	return start_mark && end_mark ? end_mark->line > start_mark->line : false;
}

const char *fy_token_get_direct_output(struct fy_token *fyt, size_t *sizep);

static inline struct fy_input *fy_token_get_input(struct fy_token *fyt)
{
	return fyt ? fyt->handle.fyi : NULL;
}

enum fy_atom_style fy_token_atom_style(struct fy_token *fyt);
enum fy_scalar_style fy_token_scalar_style(struct fy_token *fyt);

#define FYTTAF_HAS_LB			0x0001
#define FYTTAF_HAS_WS			0x0002
#define FYTTAF_HAS_CONSECUTIVE_LB	0x0004
#define FYTTAF_HAS_CONSECUTIVE_WS	0x0008
#define FYTTAF_EMPTY			0x0040
#define FYTTAF_CAN_BE_SIMPLE_KEY	0x0080
#define FYTTAF_HAS_ESCAPE		0x0100
#define FYTTAF_DIRECT_OUTPUT		0x0200
#define FYTTAF_NO_TEXT_TOKEN		0x0400
#define FYTTAF_TEXT_TOKEN		0x0800

int fy_token_text_analyze(struct fy_token *fyt);

unsigned int fy_analyze_scalar_content(const char *data, size_t size);

const char *fy_tag_directive_token_prefix(struct fy_token *fyt, size_t *lenp);
const char *fy_tag_directive_token_handle(struct fy_token *fyt, size_t *lenp);

/* must be freed */
char *fy_token_debug_text(struct fy_token *fyt);

#define fy_token_debug_text_a(_fyt) \
	({ \
		struct fy_token *__fyt = (_fyt); \
		char *_buf, *_rbuf = ""; \
		size_t _len; \
		_buf = fy_token_debug_text(__fyt); \
		if (_buf) { \
			_len = strlen(_buf); \
			_rbuf = alloca(_len + 1); \
			memcpy(_rbuf, _buf, _len + 1); \
			free(_buf); \
		} \
		_rbuf; \
	})

int fy_token_memcmp(struct fy_token *fyt, const void *ptr, size_t len);
int fy_token_strcmp(struct fy_token *fyt, const char *str);
int fy_token_cmp(struct fy_token *fyt1, struct fy_token *fyt2);

struct fy_token_iter {
	struct fy_token *fyt;
	struct fy_iter_chunk ic;	/* direct mode */
	struct fy_atom_iter atom_iter;
	int unget_c;
};

#endif
