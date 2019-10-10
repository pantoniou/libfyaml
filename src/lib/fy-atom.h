/*
 * fy-atom.h - internal YAML atom methods
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_ATOM_H
#define FY_ATOM_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

#include <fy-list.h>

#include <libfyaml.h>

struct fy_parser;
struct fy_input;

enum fy_atom_style {
	FYAS_PLAIN,
	FYAS_SINGLE_QUOTED,
	FYAS_DOUBLE_QUOTED,
	FYAS_LITERAL,
	FYAS_FOLDED,
	FYAS_URI,	/* special style for URIs */
	FYAS_DOUBLE_QUOTED_MANUAL,
	FYAS_COMMENT	/* (possibly multi line) comment */
};

static inline bool fy_atom_style_is_quoted(enum fy_atom_style style)
{
	return style == FYAS_SINGLE_QUOTED || style == FYAS_DOUBLE_QUOTED;
}

static inline bool fy_atom_style_is_block(enum fy_atom_style style)
{
	return style == FYAS_LITERAL || style == FYAS_FOLDED;
}

enum fy_atom_chomp {
	FYAC_STRIP,
	FYAC_CLIP,
	FYAC_KEEP,
};

struct fy_atom {
	struct fy_mark start_mark;
	struct fy_mark end_mark;
	size_t storage_hint;	/* guaranteed to fit in this amount of bytes */
	struct fy_input *fyi;	/* input on which atom is on */
	unsigned int increment;
	/* save a little bit of space with bitfields */
	enum fy_atom_style style : 6;
	enum fy_atom_chomp chomp : 4;
	bool direct_output : 1;		/* can directly output */
	bool storage_hint_valid : 1;
	bool empty : 1;			/* atom contains ws_lb_only */
};

static inline bool fy_atom_is_set(const struct fy_atom *atom)
{
	return atom && atom->fyi;
}

int fy_atom_format_text_length(struct fy_atom *atom);
int fy_atom_format_text_length_hint(struct fy_atom *atom);
const char *fy_atom_format_text(struct fy_atom *atom, char *buf, size_t maxsz);

#define fy_atom_get_text_a(_atom) \
	({ \
		struct fy_atom *_a = (_atom); \
		int _len; \
		char *_buf; \
		const char *_txt = ""; \
		\
		if (!_a->direct_output) { \
			_len = fy_atom_format_text_length(_a); \
			if (_len > 0) { \
				_buf = alloca(_len + 1); \
				memset(_buf, 0, _len + 1); \
				fy_atom_format_text(_a, _buf, _len + 1); \
				_buf[_len] = '\0'; \
				_txt = _buf; \
			} \
		} else { \
			_len = fy_atom_size(_a); \
			_buf = alloca(_len + 1); \
			memset(_buf, 0, _len + 1); \
			memcpy(_buf, fy_atom_data(_a), _len); \
			_buf[_len] = '\0'; \
			_txt = _buf; \
		} \
		_txt; \
	})

void fy_fill_atom_start(struct fy_parser *fyp, struct fy_atom *handle);
void fy_fill_atom_end_at(struct fy_parser *fyp, struct fy_atom *handle, struct fy_mark *end_mark);
void fy_fill_atom_end(struct fy_parser *fyp, struct fy_atom *handle);
struct fy_atom *fy_fill_atom(struct fy_parser *fyp, int advance, struct fy_atom *handle);

#define fy_fill_atom_a(_fyp, _advance)  fy_fill_atom((_fyp), (_advance), alloca(sizeof(struct fy_atom)))

/* internals */
int fy_atom_format_internal(const struct fy_atom *atom, void *out, size_t *outszp);

#endif
