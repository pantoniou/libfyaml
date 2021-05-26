/*
 * fy-emit.h - internal YAML emitter header
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_EMIT_H
#define FY_EMIT_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <libfyaml.h>

#include "fy-utf8.h"
#include "fy-event.h"

#define FYEF_WHITESPACE			0x0001
#define FYEF_INDENTATION		0x0002
#define FYEF_OPEN_ENDED			0x0004
#define FYEF_HAD_DOCUMENT_START		0x0008
#define FYEF_HAD_DOCUMENT_END		0x0010
#define FYEF_HAD_DOCUMENT_OUTPUT	0x0020

struct fy_document;
struct fy_emitter;
struct fy_document_state;

#define FYEA_INPLACE_SZ	256
struct fy_emit_accum {
	struct fy_emitter *emit;
	char *accum;
	size_t alloc;
	size_t next;
	char inplace[FYEA_INPLACE_SZ];
	int utf8_count;
	int start_col, col;
	int ts;
	enum fy_emitter_write_type type;
};

enum fy_emitter_state {
	FYES_NONE,		/* when not using the raw emitter interface */
	FYES_STREAM_START,
	FYES_FIRST_DOCUMENT_START,
	FYES_DOCUMENT_START,
	FYES_DOCUMENT_CONTENT,
	FYES_DOCUMENT_END,
	FYES_SEQUENCE_FIRST_ITEM,
	FYES_SEQUENCE_ITEM,
	FYES_MAPPING_FIRST_KEY,
	FYES_MAPPING_KEY,
	FYES_MAPPING_SIMPLE_VALUE,
	FYES_MAPPING_VALUE,
	FYES_END,
};

struct fy_emit_save_ctx {
	bool flow_token : 1;
	bool flow : 1;
	bool empty : 1;
	int old_indent;
	int flags;
	int indent;
	struct fy_token *fyt_last_key;
	struct fy_token *fyt_last_value;
	int s_flags;
	int s_indent;
};

/* internal flags */
#define DDNF_ROOT		0x0001
#define DDNF_SEQ		0x0002
#define DDNF_MAP		0x0004
#define DDNF_SIMPLE		0x0008
#define DDNF_FLOW		0x0010
#define DDNF_INDENTLESS		0x0020
#define DDNF_SIMPLE_SCALAR_KEY	0x0040

struct fy_emitter {
	int line;
	int column;
	int flow_level;
	unsigned int flags;
	bool output_error : 1;
	/* current document */
	struct fy_emitter_cfg cfg;	/* yeah, it isn't worth just to save a few bytes */
	struct fy_document *fyd;
	struct fy_document_state *fyds;	/* fyd->fyds when fyd != NULL */
	struct fy_emit_accum ea;
	struct fy_diag *diag;

	/* streaming event mode */
	enum fy_emitter_state state;
	enum fy_emitter_state *state_stack;
	unsigned int state_stack_alloc;
	unsigned int state_stack_top;
	enum fy_emitter_state state_stack_inplace[64];
	struct fy_eventp_list queued_events;
	int s_indent;
	int s_flags;
	struct fy_emit_save_ctx s_sc;
	struct fy_emit_save_ctx *sc_stack;
	unsigned int sc_stack_alloc;
	unsigned int sc_stack_top;
	struct fy_emit_save_ctx sc_stack_inplace[16];

	/* recycled */
	struct fy_eventp_list recycled_eventp;
};

void fy_emit_write(struct fy_emitter *emit, enum fy_emitter_write_type type, const char *str, int len);

static inline bool fy_emit_whitespace(struct fy_emitter *emit)
{
	return !!(emit->flags & FYEF_WHITESPACE);
}

static inline bool fy_emit_indentation(struct fy_emitter *emit)
{
	return !!(emit->flags & FYEF_INDENTATION);
}

static inline bool fy_emit_open_ended(struct fy_emitter *emit)
{
	return !!(emit->flags & FYEF_OPEN_ENDED);
}

static inline void
fy_emit_accum_reset(struct fy_emit_accum *ea)
{
	ea->next = 0;
	ea->utf8_count = 0;
	ea->col = ea->start_col;
}

static inline void fy_emit_accum_start(struct fy_emit_accum *ea, enum fy_emitter_write_type type)
{
	assert(ea->emit);

	ea->start_col = ea->emit->column;
	ea->type = type;
	fy_emit_accum_reset(ea);
}

static inline void fy_emit_accum_init(struct fy_emit_accum *ea, struct fy_emitter *emit)
{
	assert(ea);
	assert(emit);

	memset(ea, 0, sizeof(*ea));

	ea->emit = emit;
	ea->accum = ea->inplace;
	ea->alloc = sizeof(ea->inplace);
	ea->start_col = ea->emit->column;
	ea->ts = 8;	/* XXX for now */
	fy_emit_accum_reset(ea);
}

static inline void fy_emit_accum_cleanup(struct fy_emit_accum *ea)
{
	if (ea->accum && ea->accum != ea->inplace)
		free(ea->accum);
}

static inline void fy_emit_accum_finish(struct fy_emit_accum *ea)
{
	fy_emit_accum_reset(ea);
}

int fy_emit_accum_grow(struct fy_emit_accum *ea);

static inline int
fy_emit_accum_utf8_put_raw(struct fy_emit_accum *ea, int c)
{
	size_t w;
	char *ss;
	int ret;

	/* grow if needed */
	w = fy_utf8_width(c);
	while (w > (ea->alloc - ea->next)) {
		ret = fy_emit_accum_grow(ea);
		if (ret != 0)
			return ret;
	}
	ss = fy_utf8_put(ea->accum + ea->next, ea->alloc - ea->next, c);
	if (!ss)
		return -1;
	ea->next += w;
	ea->utf8_count++;

	return 0;
}

static inline int
fy_emit_accum_utf8_put(struct fy_emit_accum *ea, int c, struct fy_token *fyt)
{
	int ret;

	if (fy_is_lb_m(c, fy_token_atom_lb_mode(fyt))) {
		ret = fy_emit_accum_utf8_put_raw(ea, '\n');
		if (ret)
			return ret;
		ea->col = 0;
	} else if (fy_is_tab(c)) {
		ret = fy_emit_accum_utf8_put_raw(ea, '\t');
		if (ret)
			return ret;
		ea->col += (ea->ts - (ea->col % ea->ts));
	} else {
		ret = fy_emit_accum_utf8_put_raw(ea, c);
		if (ret)
			return ret;
		ea->col++;
	}

	return 0;
}

static inline void
fy_emit_accum_output(struct fy_emit_accum *ea)
{
	if (ea->next > 0)
		fy_emit_write(ea->emit, ea->type, ea->accum, ea->next);
	fy_emit_accum_reset(ea);
}

static inline int
fy_emit_accum_utf8_size(struct fy_emit_accum *ea)
{
	return ea->utf8_count;
}

static inline int
fy_emit_accum_column(struct fy_emit_accum *ea)
{
	return ea->col;
}

#endif
