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

#include "fy-utils.h"
#include "fy-typelist.h"
#include "fy-ctype.h"

#include "fy-atom.h"
#include "fy-diag.h"

struct fy_atom;
struct fy_parser;

enum fy_input_type {
	fyit_file,
	fyit_stream,
	fyit_memory,
	fyit_alloc,
	fyit_callback,
	fyit_fd,
	fyit_dociter,
};

struct fy_input_cfg {
	enum fy_input_type type;
	void *userdata;
	size_t chunk;
	bool ignore_stdio : 1;
	bool no_fclose_fp : 1;
	bool no_close_fd : 1;
	union {
		struct {
			const char *filename;
		} file;
		struct {
			const char *name;
			FILE *fp;
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
		struct {
			int fd;
		} fd;
		struct {
			enum fy_parser_event_generator_flags flags;
			struct fy_document_iterator *fydi;
			struct fy_document *fyd;
			bool owns_iterator;
		} dociter;
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
	int refs;		/* number of referers */
	char *name;
	void *buffer;		/* when the file can't be mmaped */
	uint64_t generation;
	size_t allocated;
	size_t read;
	size_t chunk;
	size_t chop;
	FILE *fp;		/* FILE* for the input if it exists */
	int fd;			/* fd for file and stream */
	size_t length;		/* length of file */
	void *addr;		/* mmaped for files, allocated for streams */
	bool eof : 1;		/* got EOF */
	bool err : 1;		/* got an error */

	/* propagated */
	bool json_mode;
	enum fy_lb_mode lb_mode;
	enum fy_flow_ws_mode fws_mode;
	bool directive0_mode;
};
FY_TYPE_DECL_LIST(input);

static inline const void *
fy_input_start_size(const struct fy_input *fyi, size_t *sizep)
{
	const void *start;
	size_t size;

	/* tokens cannot cross boundaries */
	switch (fyi->cfg.type) {
	case fyit_file:
	case fyit_fd:
		if (fyi->addr) {
			start = fyi->addr;
			size = fyi->length;
			break;
		}

		/* fall-through */

	case fyit_stream:
	case fyit_callback:
		start = fyi->buffer;
		size = fyi->read;
		break;

	case fyit_memory:
		start = fyi->cfg.memory.data;
		size = fyi->cfg.memory.size;
		break;

	case fyit_alloc:
		start = fyi->cfg.alloc.data;
		size = fyi->cfg.alloc.size;
		break;

	default:
		start = NULL;
		size = 0;
		break;
	}

	*sizep = size;
	return start;
}

static inline const void *fy_input_start(const struct fy_input *fyi)
{
	size_t size;
	return fy_input_start_size(fyi, &size);
}

static inline size_t fy_input_size(const struct fy_input *fyi)
{
	size_t size;
	(void)fy_input_start_size(fyi, &size);
	return size;
}

struct fy_input *fy_input_alloc(void);
void fy_input_free(struct fy_input *fyi);

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

static inline struct fy_input *
fy_input_ref(struct fy_input *fyi)
{
	if (!fyi)
		return NULL;


	assert(fyi->refs + 1 > 0);

	fyi->refs++;

	return fyi;
}

static inline void
fy_input_unref(struct fy_input *fyi)
{
	if (!fyi)
		return;

	assert(fyi->refs > 0);

	if (fyi->refs == 1)
		fy_input_free(fyi);
	else
		fyi->refs--;
}

ssize_t fy_input_estimate_queued_size(const struct fy_input *fyi);

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

	size_t this_input_start;	/* this input start */
	const void *current_ptr;	/* current pointer into the buffer */
	const void *current_ptr_start;	/* the start of this input */
	const void *current_ptr_end;	/* the end of this input */

	int line;			/* always on input */
	int column;

	int tabsize;			/* very experimental tab size for indent purposes */

	struct fy_diag *diag;

	/* decoded mode variables; update when changing modes */
	bool json_mode;
	enum fy_lb_mode lb_mode;
	enum fy_flow_ws_mode fws_mode;
	bool directive0_mode;
};

void fy_reader_reset(struct fy_reader *fyr);
void fy_reader_setup(struct fy_reader *fyr, const struct fy_reader_ops *ops);
void fy_reader_cleanup(struct fy_reader *fyr);

int fy_reader_input_open(struct fy_reader *fyr, struct fy_input *fyi, const struct fy_reader_input_cfg *icfg);
int fy_reader_input_done(struct fy_reader *fyr);
int fy_reader_input_scan_token_mark_slow_path(struct fy_reader *fyr);

static FY_ALWAYS_INLINE inline size_t
fy_reader_current_input_pos(const struct fy_reader *fyr)
{
	return (size_t)(fyr->current_ptr - fyr->current_ptr_start);
}

static FY_ALWAYS_INLINE inline size_t
fy_reader_current_left(const struct fy_reader *fyr)
{
	return (size_t)(fyr->current_ptr_end - fyr->current_ptr);
}

static inline bool
fy_reader_input_chop_active(struct fy_reader *fyr)
{
	struct fy_input *fyi;

	assert(fyr);

	fyi = fyr->current_input;
	assert(fyi);

	if (!fyi->chop)
		return false;

	switch (fyi->cfg.type) {
	case fyit_file:
		return !fyi->addr && fyi->fp;	/* non-mmap mode */

	case fyit_stream:
	case fyit_callback:
		return true;

	default:
		/* all the others do not support chop */
		break;
	}

	return false;
}

static inline int
fy_reader_input_scan_token_mark(struct fy_reader *fyr)
{
	/* don't chop until ready */
	if (!fy_reader_input_chop_active(fyr) ||
	    fyr->current_input->chop > fy_reader_current_input_pos(fyr))
		return 0;

	return fy_reader_input_scan_token_mark_slow_path(fyr);
}

const void *fy_reader_ptr_slow_path(struct fy_reader *fyr, size_t *leftp);
const void *fy_reader_ensure_lookahead_slow_path(struct fy_reader *fyr, size_t size, size_t *leftp);

void fy_reader_apply_mode(struct fy_reader *fyr);

static FY_ALWAYS_INLINE inline enum fy_reader_mode
fy_reader_get_mode(const struct fy_reader *fyr)
{
	assert(fyr);
	return fyr->mode;
}

static FY_ALWAYS_INLINE inline void
fy_reader_set_mode(struct fy_reader *fyr, enum fy_reader_mode mode)
{
	assert(fyr);
	fyr->mode = mode;
	fy_reader_apply_mode(fyr);
}

static FY_ALWAYS_INLINE inline enum fy_reader_mode
fy_reader_calculate_mode(const struct fy_version *vers, bool json_mode)
{
	enum fy_reader_mode mode;

	if (!json_mode)
		mode = fy_version_compare(vers, fy_version_make(1, 1)) <= 0 ? fyrm_yaml_1_1 : fyrm_yaml;
	else
		mode = fyrm_json;

	return mode;
}

static FY_ALWAYS_INLINE inline struct fy_input *
fy_reader_current_input(const struct fy_reader *fyr)
{
	assert(fyr);
	return fyr->current_input;
}

static FY_ALWAYS_INLINE inline uint64_t
fy_reader_current_input_generation(const struct fy_reader *fyr)
{
	assert(fyr);
	assert(fyr->current_input);
	return fyr->current_input->generation;
}

static FY_ALWAYS_INLINE inline int
fy_reader_column(const struct fy_reader *fyr)
{
	assert(fyr);
	return fyr->column;
}

static FY_ALWAYS_INLINE inline int
fy_reader_tabsize(const struct fy_reader *fyr)
{
	assert(fyr);
	return fyr->tabsize;
}

static FY_ALWAYS_INLINE inline int
fy_reader_line(const struct fy_reader *fyr)
{
	assert(fyr);
	return fyr->line;
}

static FY_ALWAYS_INLINE inline bool
fy_reader_generates_events(const struct fy_reader *fyr)
{
	struct fy_input *fyi;

	fyi = fy_reader_current_input(fyr);
	if (!fyi)
		return false;

	return fyi->cfg.type == fyit_dociter;
}

struct fy_event *fy_reader_generate_next_event(struct fy_reader *fyr);
void fy_reader_event_free(struct fy_reader *fyr, struct fy_event *fye);

/* force new line at the end of stream */
static inline void fy_reader_stream_end(struct fy_reader *fyr)
{
	assert(fyr);

	/* force new line */
	if (fyr->column) {
		fyr->column = 0;
		fyr->line++;
	}
}

static FY_ALWAYS_INLINE inline void
fy_reader_get_mark(struct fy_reader *fyr, struct fy_mark *fym)
{
	assert(fyr);
	fym->input_pos = fy_reader_current_input_pos(fyr);
	fym->line = fyr->line;
	fym->column = fyr->column;
}

static FY_ALWAYS_INLINE inline const void *
fy_reader_ptr(struct fy_reader *fyr, size_t *leftp)
{
	if (fyr->current_ptr) {
		if (leftp)
			*leftp = fy_reader_current_left(fyr);
		return fyr->current_ptr;
	}

	return fy_reader_ptr_slow_path(fyr, leftp);
}

static FY_ALWAYS_INLINE inline bool
fy_reader_json_mode(const struct fy_reader *fyr)
{
	assert(fyr);
	return fyr->json_mode;
}

static FY_ALWAYS_INLINE inline enum fy_lb_mode
fy_reader_lb_mode(const struct fy_reader *fyr)
{
	assert(fyr);
	return fyr->lb_mode;
}

static FY_ALWAYS_INLINE inline enum fy_flow_ws_mode
fy_reader_flow_ws_mode(const struct fy_reader *fyr)
{
	assert(fyr);
	return fyr->fws_mode;
}

static FY_ALWAYS_INLINE inline enum fy_flow_ws_mode
fy_reader_directive0_mode(const struct fy_reader *fyr)
{
	assert(fyr);
	return fyr->directive0_mode;
}

static FY_ALWAYS_INLINE inline bool
fy_reader_is_lb(const struct fy_reader *fyr, int c)
{
	return fy_is_lb_m(c, fy_reader_lb_mode(fyr));
}

static FY_ALWAYS_INLINE inline bool
fy_reader_is_lbz(const struct fy_reader *fyr, int c)
{
	return fy_is_lbz_m(c, fy_reader_lb_mode(fyr));
}

static FY_ALWAYS_INLINE inline bool
fy_reader_is_blankz(const struct fy_reader *fyr, int c)
{
	return fy_is_blankz_m(c, fy_reader_lb_mode(fyr));
}

static FY_ALWAYS_INLINE inline bool
fy_reader_is_generic_lb(const struct fy_reader *fyr, int c)
{
	return fy_is_generic_lb_m(c, fy_reader_lb_mode(fyr));
}

static FY_ALWAYS_INLINE inline bool
fy_reader_is_generic_lbz(const struct fy_reader *fyr, int c)
{
	return fy_is_generic_lbz_m(c, fy_reader_lb_mode(fyr));
}

static FY_ALWAYS_INLINE inline bool
fy_reader_is_generic_blankz(const struct fy_reader *fyr, int c)
{
	return fy_is_generic_blankz_m(c, fy_reader_lb_mode(fyr));
}

static FY_ALWAYS_INLINE inline bool
fy_reader_is_flow_ws(const struct fy_reader *fyr, int c)
{
	return fy_is_flow_ws_m(c, fy_reader_flow_ws_mode(fyr));
}

static FY_ALWAYS_INLINE inline bool
fy_reader_is_flow_blank(const struct fy_reader *fyr, int c)
{
	return fy_reader_is_flow_ws(fyr, c);	/* same */
}

static FY_ALWAYS_INLINE inline bool
fy_reader_is_flow_blankz(const struct fy_reader *fyr, int c)
{
	return fy_is_flow_ws_m(c, fy_reader_flow_ws_mode(fyr)) ||
	       fy_is_generic_lbz_m(c, fy_reader_lb_mode(fyr));
}

static FY_ALWAYS_INLINE inline const void *
fy_reader_ensure_lookahead(struct fy_reader *fyr, size_t size, size_t *leftp)
{
	size_t current_left = fy_reader_current_left(fyr);

	if (current_left >= size) {
		*leftp = current_left;
		return fyr->current_ptr;
	}
	return fy_reader_ensure_lookahead_slow_path(fyr, size, leftp);
}

/* compare string at the current point (n max) */
static inline int
fy_reader_strncmp(struct fy_reader *fyr, const char *str, size_t n)
{
	const char *p;
	size_t len;
	int ret;

	assert(fyr);
	p = fy_reader_ensure_lookahead(fyr, n, &len);
	if (!p)
		return -1;

	/* not enough? */
	if (n > len)
		return 1;

	ret = strncmp(p, str, n);
	return ret ? 1 : 0;
}

int fy_reader_peek_at_offset_width_slow_path(struct fy_reader *fyr, size_t offset, int *wp);

static FY_ALWAYS_INLINE inline int
fy_reader_peek_at_offset_width(struct fy_reader *fyr, size_t offset, int *wp)
{
	assert(fyr);

	if ((ssize_t)(fy_reader_current_left(fyr) - offset) >= 4)
		return fy_utf8_get(fyr->current_ptr + offset, 4, wp);

	return fy_reader_peek_at_offset_width_slow_path(fyr, offset, wp);
}

int64_t fy_reader_peek_at_offset_width_slow_path_64(struct fy_reader *fyr, size_t offset);

static FY_ALWAYS_INLINE inline int64_t
fy_reader_peek_at_offset_width_64(struct fy_reader *fyr, size_t offset)
{
	assert(fyr);

	if ((ssize_t)(fy_reader_current_left(fyr) - offset) >= 4)
		return fy_utf8_get_64(fyr->current_ptr + offset, 4);

	return fy_reader_peek_at_offset_width_slow_path_64(fyr, offset);
}


static FY_ALWAYS_INLINE inline int
fy_reader_peek_at_offset(struct fy_reader *fyr, size_t offset)
{
	int w;

	return fy_reader_peek_at_offset_width(fyr, offset, &w);
}

static FY_ALWAYS_INLINE inline int
fy_reader_peek_at_width_internal(struct fy_reader *fyr, int pos, ssize_t *offsetp, int *wp)
{
	int i, c, w;
	size_t offset;

	offset = (size_t)*offsetp;
	if ((ssize_t)offset < 0) {
		for (i = 0, offset = 0; i < pos; i++, offset += w) {
			c = fy_reader_peek_at_offset_width(fyr, offset, &w);
			if (c < 0)
				return c;
		}
	}
	c = fy_reader_peek_at_offset_width(fyr, offset, wp);

	*offsetp = offset + *wp;
	return c;
}

static FY_ALWAYS_INLINE inline int
fy_reader_peek_at_internal(struct fy_reader *fyr, int pos, ssize_t *offsetp)
{
	int w;

	return fy_reader_peek_at_width_internal(fyr, pos, offsetp, &w);
}

static FY_ALWAYS_INLINE inline bool
fy_reader_is_blank_at_offset(struct fy_reader *fyr, size_t offset)
{
	return fy_is_blank(fy_reader_peek_at_offset(fyr, offset));
}

static FY_ALWAYS_INLINE inline bool
fy_reader_is_blankz_at_offset(struct fy_reader *fyr, size_t offset)
{
	return fy_reader_is_blankz(fyr, fy_reader_peek_at_offset(fyr, offset));
}

static FY_ALWAYS_INLINE inline int
fy_reader_peek_at_width(struct fy_reader *fyr, int pos, int *wp)
{
	int i, c, w;
	size_t offset;

	assert(fyr);
	for (i = 0, offset = 0; i < pos; i++, offset += w) {
		c = fy_reader_peek_at_offset_width(fyr, offset, &w);
		if (c < 0)
			return c;
	}

	return fy_reader_peek_at_offset_width(fyr, offset, wp);
}

static FY_ALWAYS_INLINE inline int
fy_reader_peek_at(struct fy_reader *fyr, int pos)
{
	int i, c, w;
	size_t offset;

	assert(fyr);
	for (i = 0, offset = 0; i < pos; i++, offset += w) {
		c = fy_reader_peek_at_offset_width(fyr, offset, &w);
		if (c < 0)
			return c;
	}

	return fy_reader_peek_at_offset(fyr, offset);
}

static FY_ALWAYS_INLINE inline int
fy_reader_peek_width(struct fy_reader *fyr, int *wp)
{
	return fy_reader_peek_at_offset_width(fyr, 0, wp);
}

static FY_ALWAYS_INLINE inline int
fy_reader_peek(struct fy_reader *fyr)
{
	int w;
	return fy_reader_peek_width(fyr, &w);
}

static FY_ALWAYS_INLINE inline const void *
fy_reader_peek_block(struct fy_reader *fyr, size_t *lenp)
{
	const void *p;

	/* try to pull at least one utf8 character usually */
	p = fy_reader_ensure_lookahead(fyr, 4, lenp);

	/* not a utf8 character available? try a single byte */
	if (!p)
		p = fy_reader_ensure_lookahead(fyr, 1, lenp);
	if (!*lenp)
		p = NULL;
	return p;
}

static FY_ALWAYS_INLINE inline void
fy_reader_advance_octets(struct fy_reader *fyr, size_t advance)
{
	fyr->current_ptr += advance;
}

void fy_reader_advance_slow_path(struct fy_reader *fyr, int c);

static FY_ALWAYS_INLINE inline void
fy_reader_advance_printable_ascii(struct fy_reader *fyr, int c)
{
	assert(fyr);
	fy_reader_advance_octets(fyr, 1);
	fyr->column++;
}

static FY_ALWAYS_INLINE inline void
fy_reader_update_state_lb_mode(struct fy_reader *fyr, const int c, const enum fy_lb_mode lb_mode)
{
	if (fy_is_lb_m(c, lb_mode)) {
		fyr->column = 0;
		fyr->line++;
	} else if (fy_is_tab(c) && fyr->tabsize)
		fyr->column += (fyr->tabsize - (fyr->column % fyr->tabsize));
	else
		fyr->column++;
}

static FY_ALWAYS_INLINE inline void
fy_reader_advance_lb_mode(struct fy_reader *fyr, const int c, const enum fy_lb_mode lb_mode)
{
	assert(fy_utf8_is_valid(c));
	fy_reader_advance_octets(fyr, fy_utf8_width(c));
	fy_reader_update_state_lb_mode(fyr, c, lb_mode);
}

static FY_ALWAYS_INLINE inline void
fy_reader_advance(struct fy_reader *fyr, const int c)
{
	fy_reader_advance_lb_mode(fyr, c, fy_reader_lb_mode(fyr));
}

static FY_ALWAYS_INLINE inline void
fy_reader_advance_ws(struct fy_reader *fyr, int c)
{
	/* skip this character */
	fy_reader_advance_octets(fyr, fy_utf8_width(c));

	if (fy_is_tab(c) && fyr->tabsize)
		fyr->column += (fyr->tabsize - (fyr->column % fyr->tabsize));
	else
		fyr->column++;
}

static FY_ALWAYS_INLINE inline void
fy_reader_advance_space(struct fy_reader *fyr)
{
	fy_reader_advance_octets(fyr, 1);
	fyr->column++;
}

static FY_ALWAYS_INLINE inline int
fy_reader_get(struct fy_reader *fyr)
{
	int value;

	value = fy_reader_peek(fyr);
	if (value < 0)
		return value;

	fy_reader_advance(fyr, value);

	return value;
}

static FY_ALWAYS_INLINE inline int
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

/* atom */

static inline void
fy_reader_fill_atom_start(struct fy_reader *fyr, struct fy_atom *handle)
{
	/* start mark */
	fy_reader_get_mark(fyr, &handle->start_mark);
	handle->fyi = fy_reader_current_input(fyr);
	handle->fyi_generation = fy_reader_current_input_generation(fyr);

	handle->increment = 0;
	handle->tozero = 0;

	/* note that handle->data may be zero for empty input */
}

static inline void
fy_reader_fill_atom_end_at(struct fy_reader *fyr, struct fy_atom *handle, struct fy_mark *end_mark)
{
	if (end_mark)
		handle->end_mark = *end_mark;
	else
		fy_reader_get_mark(fyr, &handle->end_mark);

	/* default is plain, modify at return */
	handle->style = FYAS_PLAIN;
	handle->chomp = FYAC_CLIP;
	/* by default we don't do storage hints, it's the job of the caller */
	handle->storage_hint = 0;
	handle->storage_hint_valid = false;
	handle->tabsize = fy_reader_tabsize(fyr);
	handle->json_mode = fy_reader_json_mode(fyr);
	handle->lb_mode = fy_reader_lb_mode(fyr);
	handle->fws_mode = fy_reader_flow_ws_mode(fyr);
	handle->directive0_mode = fy_reader_directive0_mode(fyr);
}

static inline void
fy_reader_fill_atom_end(struct fy_reader *fyr, struct fy_atom *handle)
{
	fy_reader_fill_atom_end_at(fyr, handle, NULL);
}

/* diag */

static inline bool
fyr_debug_log_level_is_enabled(struct fy_reader *fyr)
{
	return fyr && fy_diag_log_level_is_enabled(fyr->diag, FYET_DEBUG, FYEM_SCAN);
}

int fy_reader_vdiag(struct fy_reader *fyr, unsigned int flags,
		    const char *file, int line, const char *func,
		    const char *fmt, va_list ap);

int fy_reader_diag(struct fy_reader *fyr, unsigned int flags,
		   const char *file, int line, const char *func,
		   const char *fmt, ...)
			__attribute__((format(printf, 6, 7)));

void fy_reader_diag_vreport(struct fy_reader *fyr,
			    const struct fy_diag_report_ctx *fydrc,
			    const char *fmt, va_list ap);
void fy_reader_diag_report(struct fy_reader *fyr,
			   const struct fy_diag_report_ctx *fydrc,
			   const char *fmt, ...)
		__attribute__((format(printf, 3, 4)));

#ifdef FY_DEVMODE

#define fyr_debug(_fyr, _fmt, ...) \
	do { \
		struct fy_reader *__fyr = (_fyr); \
		\
		if (fyr_debug_log_level_is_enabled(__fyr)) \
			fy_reader_diag(__fyr, FYET_DEBUG, \
					__FILE__, __LINE__, __func__, \
					(_fmt) , ## __VA_ARGS__); \
	} while(0)
#else

#define fyr_debug(_fyr, _fmt, ...) \
	do { } while(0)

#endif

#define fyr_info(_fyr, _fmt, ...) \
	fy_reader_diag((_fyr), FYET_INFO, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)
#define fyr_notice(_fyr, _fmt, ...) \
	fy_reader_diag((_fyr), FYET_NOTICE, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)
#define fyr_warning(_fyr, _fmt, ...) \
	fy_reader_diag((_fyr), FYET_WARNING, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)
#define fyr_error(_fyr, _fmt, ...) \
	fy_reader_diag((_fyr), FYET_ERROR, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)

#define fyr_error_check(_fyr, _cond, _label, _fmt, ...) \
	do { \
		if (!(_cond)) { \
			fyr_error((_fyr), _fmt, ## __VA_ARGS__); \
			goto _label ; \
		} \
	} while(0)

#define _FYR_TOKEN_DIAG(_fyr, _fyt, _type, _module, _fmt, ...) \
	do { \
		struct fy_diag_report_ctx _drc; \
		memset(&_drc, 0, sizeof(_drc)); \
		_drc.type = (_type); \
		_drc.module = (_module); \
		_drc.fyt = (_fyt); \
		fy_reader_diag_report((_fyr), &_drc, (_fmt) , ## __VA_ARGS__); \
	} while(0)

#define FYR_TOKEN_DIAG(_fyr, _fyt, _type, _module, _fmt, ...) \
	_FYR_TOKEN_DIAG(_fyr, fy_token_ref(_fyt), _type, _module, _fmt, ## __VA_ARGS__)

#define FYR_PARSE_DIAG(_fyr, _adv, _cnt, _type, _module, _fmt, ...) \
	_FYR_TOKEN_DIAG(_fyr, \
		fy_token_create(FYTT_INPUT_MARKER, \
			fy_reader_fill_atom_at((_fyr), (_adv), (_cnt), \
			alloca(sizeof(struct fy_atom)))), \
		_type, _module, _fmt, ## __VA_ARGS__)

#define FYR_MARK_DIAG(_fyr, _sm, _em, _type, _module, _fmt, ...) \
	_FYR_TOKEN_DIAG(_fyr, \
		fy_token_create(FYTT_INPUT_MARKER, \
			fy_reader_fill_atom_mark(((_fyr)), (_sm), (_em), \
				alloca(sizeof(struct fy_atom)))), \
		_type, _module, _fmt, ## __VA_ARGS__)

#define FYR_NODE_DIAG(_fyr, _fyn, _type, _module, _fmt, ...) \
	_FYR_TOKEN_DIAG(_fyr, fy_node_token(_fyn), _type, _module, _fmt, ## __VA_ARGS__)

#define FYR_TOKEN_ERROR(_fyr, _fyt, _module, _fmt, ...) \
	FYR_TOKEN_DIAG(_fyr, _fyt, FYET_ERROR, _module, _fmt, ## __VA_ARGS__)

#define FYR_PARSE_ERROR(_fyr, _adv, _cnt, _module, _fmt, ...) \
	FYR_PARSE_DIAG(_fyr, _adv, _cnt, FYET_ERROR, _module, _fmt, ## __VA_ARGS__)

#define FYR_MARK_ERROR(_fyr, _sm, _em, _module, _fmt, ...) \
	FYR_MARK_DIAG(_fyr, _sm, _em, FYET_ERROR, _module, _fmt, ## __VA_ARGS__)

#define FYR_NODE_ERROR(_fyr, _fyn, _module, _fmt, ...) \
	FYR_NODE_DIAG(_fyr, _fyn, FYET_ERROR, _module, _fmt, ## __VA_ARGS__)

#define FYR_TOKEN_ERROR_CHECK(_fyr, _fyt, _module, _cond, _label, _fmt, ...) \
	do { \
		if (!(_cond)) { \
			FYR_TOKEN_ERROR(_fyr, _fyt, _module, _fmt, ## __VA_ARGS__); \
			goto _label; \
		} \
	} while(0)

#define FYR_PARSE_ERROR_CHECK(_fyr, _adv, _cnt, _module, _cond, _label, _fmt, ...) \
	do { \
		if (!(_cond)) { \
			FYR_PARSE_ERROR(_fyr, _adv, _cnt, _module, _fmt, ## __VA_ARGS__); \
			goto _label; \
		} \
	} while(0)

#define FYR_MARK_ERROR_CHECK(_fyr, _sm, _em, _module, _cond, _label, _fmt, ...) \
	do { \
		if (!(_cond)) { \
			FYR_MARK_ERROR(_fyr, _sm, _em, _module, _fmt, ## __VA_ARGS__); \
			goto _label; \
		} \
	} while(0)

#define FYR_NODE_ERROR_CHECK(_fyr, _fyn, _module, _cond, _label, _fmt, ...) \
	do { \
		if (!(_cond)) { \
			FYR_NODE_ERROR(_fyr, _fyn, _module, _fmt, ## __VA_ARGS__); \
			goto _label; \
		} \
	} while(0)

#define FYR_TOKEN_WARNING(_fyr, _fyt, _module, _fmt, ...) \
	FYR_TOKEN_DIAG(_fyr, _fyt, FYET_WARNING, _module, _fmt, ## __VA_ARGS__)

#define FYR_PARSE_WARNING(_fyr, _adv, _cnt, _module, _fmt, ...) \
	FYR_PARSE_DIAG(_fyr, _adv, _cnt, FYET_WARNING, _module, _fmt, ## __VA_ARGS__)

#define FYR_MARK_WARNING(_fyr, _sm, _em, _module, _fmt, ...) \
	FYR_MARK_DIAG(_fyr, _sm, _em, FYET_WARNING, _module, _fmt, ## __VA_ARGS__)

#define FYR_NODE_WARNING(_fyr, _fyn, _type, _module, _fmt, ...) \
	FYR_NODE_DIAG(_fyr, _fyn, FYET_WARNING, _module, _fmt, ## __VA_ARGS__)

#endif
