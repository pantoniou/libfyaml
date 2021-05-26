/*
 * fy-input.h - YAML input methods
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_INPUT_H
#define FY_INPUT_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include <libfyaml.h>

#include "fy-typelist.h"
#include "fy-ctype.h"

#ifndef NDEBUG
#define __FY_DEBUG_UNUSED__	/* nothing */
#else
#define __FY_DEBUG_UNUSED__	__attribute__((__unused__))
#endif

struct fy_atom;
struct fy_parser;

enum fy_input_type {
	fyit_file,
	fyit_stream,
	fyit_memory,
	fyit_alloc,
	fyit_callback,
};

struct fy_input_cfg {
	enum fy_input_type type;
	void *userdata;
	union {
		struct {
			const char *filename;
		} file;
		struct {
			const char *name;
			FILE *fp;
			size_t chunk;
			bool ignore_stdio;
		} stream;
		struct {
			const void *data;
			size_t size;
		} memory;
		struct {
			void *data;
			size_t size;
		} alloc;
		struct {
			/* negative return is error, 0 is EOF */
			ssize_t (*input)(void *user, void *buf, size_t count);
		} callback;
	};
};

enum fy_input_state {
	FYIS_NONE,
	FYIS_QUEUED,
	FYIS_PARSE_IN_PROGRESS,
	FYIS_PARSED,
};

FY_TYPE_FWD_DECL_LIST(input);
struct fy_input {
	struct list_head node;
	enum fy_input_state state;
	struct fy_input_cfg cfg;
	char *name;
	void *buffer;		/* when the file can't be mmaped */
	uint64_t generation;
	size_t allocated;
	size_t read;
	size_t chunk;
	FILE *fp;
	int refs;
	void *addr;		/* mmaped for files, allocated for streams */
	bool eof : 1;		/* got EOF */
	bool err : 1;		/* got an error */

	/* propagated */
	enum fy_lb_mode lb_mode;
	enum fy_flow_ws_mode fws_mode;

	union {
		struct {
			int fd;			/* fd for file and stream */
			size_t length;
		} file;
	};
};
FY_TYPE_DECL_LIST(input);

static inline const void *fy_input_start(const struct fy_input *fyi)
{
	const void *ptr = NULL;

	switch (fyi->cfg.type) {
	case fyit_file:
		if (fyi->addr) {
			ptr = fyi->addr;
			break;
		}
		/* fall-through */

	case fyit_stream:
	case fyit_callback:
		ptr = fyi->buffer;
		break;

	case fyit_memory:
		ptr = fyi->cfg.memory.data;
		break;

	case fyit_alloc:
		ptr = fyi->cfg.alloc.data;
		break;

	default:
		break;
	}
	assert(ptr);
	return ptr;
}

static inline size_t fy_input_size(const struct fy_input *fyi)
{
	size_t size;

	switch (fyi->cfg.type) {
	case fyit_file:
		if (fyi->addr) {
			size = fyi->file.length;
			break;
		}
		/* fall-through */

	case fyit_stream:
	case fyit_callback:
		size = fyi->read;
		break;

	case fyit_memory:
		size = fyi->cfg.memory.size;
		break;

	case fyit_alloc:
		size = fyi->cfg.alloc.size;
		break;

	default:
		size = 0;
		break;
	}
	return size;
}

struct fy_input *fy_input_alloc(void);
void fy_input_free(struct fy_input *fyi);
struct fy_input *fy_input_ref(struct fy_input *fyi);
void fy_input_unref(struct fy_input *fyi);

static inline enum fy_input_state fy_input_get_state(struct fy_input *fyi)
{
	return fyi->state;
}

struct fy_input *fy_input_create(const struct fy_input_cfg *fyic);

const char *fy_input_get_filename(struct fy_input *fyi);

struct fy_input *fy_input_from_data(const char *data, size_t size,
				    struct fy_atom *handle, bool simple);
struct fy_input *fy_input_from_malloc_data(char *data, size_t size,
					   struct fy_atom *handle, bool simple);

void fy_input_close(struct fy_input *fyi);

struct fy_reader;

enum fy_reader_mode {
	fyrm_yaml,
	fyrm_json,
	fyrm_yaml_1_1,	/* yaml 1.1 mode */
};

struct fy_reader_ops {
	struct fy_diag *(*get_diag)(struct fy_reader *fyr);
	int (*file_open)(struct fy_reader *fyr, const char *filename);
};

struct fy_reader_input_cfg {
	bool disable_mmap_opt;
};

struct fy_reader {
	const struct fy_reader_ops *ops;
	enum fy_reader_mode mode;

	struct fy_reader_input_cfg current_input_cfg;
	struct fy_input *current_input;

	size_t current_pos;		/* from start of stream */
	size_t current_input_pos;	/* from start of input */
	const void *current_ptr;	/* current pointer into the buffer */
	int current_c;			/* current utf8 character at current_ptr (-1 if not cached) */
	int current_w;			/* current utf8 character width */
	size_t current_left;		/* currently left characters into the buffer */

	int line;			/* always on input */
	int column;
	int nontab_column;		/* column without accounting for tabs */

	int tabsize;			/* very experimental tab size for indent purposes */

	struct fy_diag *diag;
};

void fy_reader_reset(struct fy_reader *fyr);
void fy_reader_setup(struct fy_reader *fyr, const struct fy_reader_ops *ops);
void fy_reader_cleanup(struct fy_reader *fyr);

int fy_reader_input_open(struct fy_reader *fyr, struct fy_input *fyi, const struct fy_reader_input_cfg *icfg);
int fy_reader_input_done(struct fy_reader *fyr);

const void *fy_reader_ptr_slow_path(struct fy_reader *fyr, size_t *leftp);
const void *fy_reader_ensure_lookahead_slow_path(struct fy_reader *fyr, size_t size, size_t *leftp);

static inline void
fy_reader_apply_mode_to_input(struct fy_reader *fyr)
{
	if (!fyr || !fyr->current_input)
		return;

	/* set input mode from the current reader settings */
	switch (fyr->mode) {
	case fyrm_yaml:
		fyr->current_input->lb_mode = fylb_cr_nl;
		fyr->current_input->fws_mode = fyfws_space_tab;
		break;
	case fyrm_json:
		fyr->current_input->lb_mode = fylb_cr_nl;
		fyr->current_input->fws_mode = fyfws_space;
		break;
	case fyrm_yaml_1_1:
		fyr->current_input->lb_mode = fylb_cr_nl_N_L_P;
		fyr->current_input->fws_mode = fyfws_space_tab;
		break;
	}
}

static inline enum fy_reader_mode
fy_reader_get_mode(const struct fy_reader *fyr)
{
	if (!fyr)
		return fyrm_yaml;
	return fyr->mode;
}

static inline void
fy_reader_set_mode(struct fy_reader *fyr, enum fy_reader_mode mode)
{
	if (!fyr)
		return;

	fyr->mode = mode;
	fy_reader_apply_mode_to_input(fyr);
}

static inline struct fy_input *
fy_reader_current_input(const struct fy_reader *fyr)
{
	if (!fyr)
		return NULL;
	return fyr->current_input;
}

static inline uint64_t
fy_reader_current_input_generation(const struct fy_reader *fyr)
{
	if (!fyr || !fyr->current_input)
		return 0;
	return fyr->current_input->generation;
}

static inline int
fy_reader_column(const struct fy_reader *fyr)
{
	if (!fyr)
		return -1;
	return fyr->column;
}

static inline int
fy_reader_tabsize(const struct fy_reader *fyr)
{
	if (!fyr)
		return -1;
	return fyr->tabsize;
}

static inline int
fy_reader_line(const struct fy_reader *fyr)
{
	if (!fyr)
		return -1;
	return fyr->line;
}

/* force new line at the end of stream */
static inline void fy_reader_stream_end(struct fy_reader *fyr)
{
	if (!fyr)
		return;

	/* force new line */
	if (fyr->column) {
		fyr->column = 0;
		fyr->nontab_column = 0;
		fyr->line++;
	}
}

static inline void
fy_reader_get_mark(struct fy_reader *fyr, struct fy_mark *fym)
{
	assert(fyr);
	fym->input_pos = fyr->current_input_pos;
	fym->line = fyr->line;
	fym->column = fyr->column;
}

static inline const void *
fy_reader_ptr(struct fy_reader *fyr, size_t *leftp)
{
	if (fyr->current_ptr) {
		if (leftp)
			*leftp = fyr->current_left;
		return fyr->current_ptr;
	}

	return fy_reader_ptr_slow_path(fyr, leftp);
}

static inline bool fy_reader_json_mode(const struct fy_reader *fyr)
{
	return fyr && fyr->mode == fyrm_json;
}

static inline enum fy_lb_mode fy_reader_lb_mode(const struct fy_reader *fyr)
{
	if (!fyr)
		return fylb_cr_nl;

	switch (fyr->mode) {
	case fyrm_yaml:
	case fyrm_json:
		return fylb_cr_nl;
	case fyrm_yaml_1_1:
		return fylb_cr_nl_N_L_P;
	}

	return fylb_cr_nl;
}

static inline enum fy_flow_ws_mode fy_reader_flow_ws_mode(const struct fy_reader *fyr)
{
	if (!fyr)
		return fyfws_space_tab;

	switch (fyr->mode) {
	case fyrm_yaml:
	case fyrm_yaml_1_1:
		return fyfws_space_tab;
	case fyrm_json:
		return fyfws_space;
	}

	return fyfws_space_tab;
}

static inline bool fy_reader_is_lb(const struct fy_reader *fyr, int c)
{
	return fy_is_lb_m(c, fy_reader_lb_mode(fyr));
}

static inline bool fy_reader_is_lbz(const struct fy_reader *fyr, int c)
{
	return fy_is_lbz_m(c, fy_reader_lb_mode(fyr));
}

static inline bool fy_reader_is_blankz(const struct fy_reader *fyr, int c)
{
	return fy_is_blankz_m(c, fy_reader_lb_mode(fyr));
}

static inline bool fy_reader_is_generic_lb(const struct fy_reader *fyr, int c)
{
	return fy_is_generic_lb_m(c, fy_reader_lb_mode(fyr));
}

static inline bool fy_reader_is_generic_lbz(const struct fy_reader *fyr, int c)
{
	return fy_is_generic_lbz_m(c, fy_reader_lb_mode(fyr));
}

static inline bool fy_reader_is_generic_blankz(const struct fy_reader *fyr, int c)
{
	return fy_is_generic_blankz_m(c, fy_reader_lb_mode(fyr));
}

static inline bool fy_reader_is_flow_ws(const struct fy_reader *fyr, int c)
{
	return fy_is_flow_ws_m(c, fy_reader_flow_ws_mode(fyr));
}

static inline bool fy_reader_is_flow_blank(const struct fy_reader *fyr, int c)
{
	return fy_reader_is_flow_ws(fyr, c);	/* same */
}

static inline bool fy_reader_is_flow_blankz(const struct fy_reader *fyr, int c)
{
	return fy_is_flow_ws_m(c, fy_reader_flow_ws_mode(fyr)) ||
	       fy_is_generic_lbz_m(c, fy_reader_lb_mode(fyr));
}

static inline const void *
fy_reader_ensure_lookahead(struct fy_reader *fyr, size_t size, size_t *leftp)
{
	if (fyr->current_ptr && fyr->current_left >= size) {
		if (leftp)
			*leftp = fyr->current_left;
		return fyr->current_ptr;
	}
	return fy_reader_ensure_lookahead_slow_path(fyr, size, leftp);
}

/* advance the given number of ascii characters, not utf8 */
void fy_reader_advance_octets(struct fy_reader *fyr, size_t advance);

/* compare string at the current point (n max) */
static inline int
fy_reader_strncmp(struct fy_reader *fyr, const char *str, size_t n)
{
	const char *p;
	int ret;

	p = fy_reader_ensure_lookahead(fyr, n, NULL);
	if (!p)
		return -1;
	ret = strncmp(p, str, n);
	return ret ? 1 : 0;
}

static inline int
fy_reader_peek_at_offset(struct fy_reader *fyr, size_t offset)
{
	const uint8_t *p;
	size_t left;
	int w;

	if (offset == 0 && fyr->current_w && fyr->current_c >= 0)
		return fyr->current_c;

	/* ensure that the first octet at least is pulled in */
	p = fy_reader_ensure_lookahead(fyr, offset + 1, &left);
	if (!p)
		return FYUG_EOF;

	/* get width by first octet */
	w = fy_utf8_width_by_first_octet(p[offset]);
	if (!w)
		return FYUG_INV;

	/* make sure that there's enough to cover the utf8 width */
	if (offset + w > left) {
		p = fy_reader_ensure_lookahead(fyr, offset + w, &left);
		if (!p)
			return FYUG_PARTIAL;
	}

	return fy_utf8_get(p + offset, left - offset, &w);
}

static inline int
fy_reader_peek_at_internal(struct fy_reader *fyr, int pos, ssize_t *offsetp)
{
	int i, c;
	size_t offset;

	if (!offsetp || *offsetp < 0) {
		for (i = 0, offset = 0; i < pos; i++, offset += fy_utf8_width(c)) {
			c = fy_reader_peek_at_offset(fyr, offset);
			if (c < 0)
				return c;
		}
	} else
		offset = (size_t)*offsetp;

	c = fy_reader_peek_at_offset(fyr, offset);

	if (offsetp)
		*offsetp = offset + fy_utf8_width(c);

	return c;
}

static inline bool
fy_reader_is_blank_at_offset(struct fy_reader *fyr, size_t offset)
{
	return fy_is_blank(fy_reader_peek_at_offset(fyr, offset));
}

static inline bool
fy_reader_is_blankz_at_offset(struct fy_reader *fyr, size_t offset)
{
	return fy_reader_is_blankz(fyr, fy_reader_peek_at_offset(fyr, offset));
}

static inline int
fy_reader_peek_at(struct fy_reader *fyr, int pos)
{
	return fy_reader_peek_at_internal(fyr, pos, NULL);
}

static inline int
fy_reader_peek(struct fy_reader *fyr)
{
	return fy_reader_peek_at_offset(fyr, 0);
}

static inline void
fy_reader_advance(struct fy_reader *fyr, int c)
{
	bool is_line_break = false;

	/* skip this character */
	fy_reader_advance_octets(fyr, fy_utf8_width(c));

	/* first check for CR/LF */
	if (c == '\r' && fy_reader_peek(fyr) == '\n') {
		fy_reader_advance_octets(fyr, 1);
		is_line_break = true;
	} else if (fy_reader_is_lb(fyr, c))
		is_line_break = true;

	if (is_line_break) {
		fyr->column = 0;
		fyr->nontab_column = 0;
		fyr->line++;
	} else if (fyr->tabsize && fy_is_tab(c)) {
		fyr->column += (fyr->tabsize - (fyr->column % fyr->tabsize));
		fyr->nontab_column++;
	} else {
		fyr->column++;
		fyr->nontab_column++;
	}
}

static inline int
fy_reader_get(struct fy_reader *fyr)
{
	int value;

	value = fy_reader_peek(fyr);
	if (value < 0)
		return value;

	fy_reader_advance(fyr, value);

	return value;
}

static inline int
fy_reader_advance_by(struct fy_reader *fyr, int count)
{
	int i, c;

	for (i = 0; i < count; i++) {
		c = fy_reader_get(fyr);
		if (c < 0)
			break;
	}
	return i ? i : -1;
}

/* compare string at the current point */
static inline bool
fy_reader_strcmp(struct fy_reader *fyr, const char *str)
{
	return fy_reader_strncmp(fyr, str, strlen(str));
}

#endif
