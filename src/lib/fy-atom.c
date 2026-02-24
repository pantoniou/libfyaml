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
#include <ctype.h>

#include <libfyaml.h>

#include "fy-parse.h"
#include "fy-doc.h"
#include "fy-input.h"

#include "fy-atom.h"

const char *fy_atom_data(const struct fy_atom *atom)
{
	if (!atom)
		return NULL;

	return (const char *)fy_input_start(atom->fyi) + atom->start_mark.input_pos;
}


struct fy_atom *fy_reader_fill_atom(struct fy_reader *fyr, int advance, struct fy_atom *handle)
{
	/* start mark */
	fy_reader_fill_atom_start(fyr, handle);

	/* advance the given number of characters */
	if (advance > 0)
		fy_reader_advance_by(fyr, advance);

	fy_reader_fill_atom_end(fyr, handle);

	return handle;
}

int fy_reader_advance_mark(struct fy_reader *fyr, int advance, struct fy_mark *m)
{
	int c, tabsize, w;
	bool is_line_break;

	tabsize = fy_reader_tabsize(fyr);
	while (advance-- > 0) {
		c = fy_reader_peek_at_offset_width(fyr, m->input_pos, &w);
		if (c == -1)
			return -1;
		m->input_pos += fy_utf8_width(c);

		/* first check for CR/LF */
		if (c == '\r' &&
			fy_reader_peek_at_offset(fyr, m->input_pos + (size_t)w) == '\n') {
			m->input_pos++;
			is_line_break = true;
		} else if (fy_reader_is_lb(fyr, c))
			is_line_break = true;
		else
			is_line_break = false;

		if (is_line_break) {
			m->column = 0;
			m->line++;
		} else if (tabsize > 0 && fy_is_tab(c))
			m->column += (tabsize - (fy_reader_column(fyr) % tabsize));
		else
			m->column++;
	}

	return 0;
}

struct fy_atom *fy_reader_fill_atom_mark(struct fy_reader *fyr,
					 const struct fy_mark *start_mark,
					 const struct fy_mark *end_mark,
					 struct fy_atom *handle)
{
	if (!fyr || !start_mark || !end_mark || !handle)
		return NULL;

	memset(handle, 0, sizeof(*handle));

	handle->start_mark = *start_mark;
	handle->end_mark = *end_mark;
	handle->fyi = fy_reader_current_input(fyr);
	handle->fyi_generation = fy_reader_current_input_generation(fyr);

	/* default is plain, modify at return */
	handle->style = FYAS_PLAIN;
	handle->chomp = FYAC_CLIP;
	/* by default we don't do storage hints, it's the job of the caller */
	handle->storage_hint = 0;
	handle->storage_hint_valid = false;

	return handle;
}

struct fy_atom *fy_reader_fill_atom_at(struct fy_reader *fyr, int advance, int count, struct fy_atom *handle)
{
	struct fy_mark start_mark, end_mark;
	int rc;

	if (!fyr || !handle || !fyr->current_input)
		return NULL;

	/* start mark */
	fy_reader_get_mark(fyr, &start_mark);
	rc = fy_reader_advance_mark(fyr, advance, &start_mark);
	(void)rc;
	/* ignore the return, if the advance failed, it's the end of input */

	/* end mark */
	end_mark = start_mark;
	rc = fy_reader_advance_mark(fyr, count, &end_mark);
	(void)rc;
	/* ignore the return, if the advance failed, it's the end of input */

	return fy_reader_fill_atom_mark(fyr, &start_mark, &end_mark, handle);
}

static inline void
fy_atom_iter_chunk_reset(struct fy_atom_iter *iter)
{
	iter->top = 0;
	iter->read = 0;
}

static int
fy_atom_iter_grow_chunk(struct fy_atom_iter *iter)
{
	struct fy_atom_iter_chunk *chunks, *c;
	size_t asz;
	const char *old_s, *old_e, *ss;
	unsigned int i;
	size_t offset;

	old_s = (const char *)iter->chunks;
	old_e = (const char *)(iter->chunks + iter->alloc);

	asz = sizeof(*chunks) * iter->alloc * 2;
	chunks = realloc(iter->chunks == iter->startup_chunks ? NULL : iter->chunks, asz);
	if (!chunks)	/* out of memory */
		return -1;
	if (iter->chunks == iter->startup_chunks)
		memcpy(chunks, iter->startup_chunks, sizeof(iter->startup_chunks));

	/* for chunks that point to the inplace buffer, reassign pointers */
	for (ss = old_s, c = chunks, i = 0; i < iter->top; ss += sizeof(*c), c++, i++) {
		if (c->ic.str < old_s || c->ic.str >= old_e ||
		    c->ic.len > sizeof(c->inplace_buf))
			continue;

		/* get offset */
		offset = (size_t)(c->ic.str - ss);

		/* verify that it points to the inplace_buf area */
		assert(offset >= offsetof(struct fy_atom_iter_chunk, inplace_buf));
		offset -= offsetof(struct fy_atom_iter_chunk, inplace_buf);

		c->ic.str = c->inplace_buf + offset;
	}

	iter->alloc *= 2;
	iter->chunks = chunks;

	return 0;
}

static int
_fy_atom_iter_add_chunk(struct fy_atom_iter *iter, const char *str, size_t len)
{
	struct fy_atom_iter_chunk *c;
	int ret;

	if (!len)
		return 0;

	/* grow iter chunks? */
	if (iter->top >= iter->alloc) {
		ret = fy_atom_iter_grow_chunk(iter);
		if (ret)
			return ret;
	}
	assert(iter->top < iter->alloc);
	c = &iter->chunks[iter->top++];

	c->ic.str = str;
	c->ic.len = len;

	return 0;
}

static int
_fy_atom_iter_add_chunk_copy(struct fy_atom_iter *iter, const char *str, size_t len)
{
	struct fy_atom_iter_chunk *c;
	int ret;

	if (!len)
		return 0;

	assert(len <= sizeof(c->inplace_buf));

	if (iter->top >= iter->alloc) {
		ret = fy_atom_iter_grow_chunk(iter);
		if (ret)
			return ret;
	}
	assert(iter->top < iter->alloc);
	c = &iter->chunks[iter->top++];

	memcpy(c->inplace_buf, str, len);

	c->ic.str = c->inplace_buf;
	c->ic.len = len;

	return 0;
}

/* optimized linebreaks */
static int
_fy_atom_iter_add_lb(struct fy_atom_iter *iter, int c)
{
	switch (c) {
	/* those are generic linebreaks */
	case '\r':
	case '\n':
	case 0x85:
		return _fy_atom_iter_add_chunk(iter, "\n", 1);
	/* these are specific linebreaks */
	case 0x2028:
		return _fy_atom_iter_add_chunk(iter, "\xe2\x80\xa8", 3);
	case 0x2029:
		return _fy_atom_iter_add_chunk(iter, "\xe2\x80\xa9", 3);
	}
	/* not a linebreak */
	return -1;
}

// #define DEBUG_CHUNK

#ifndef DEBUG_CHUNK
#define fy_atom_iter_add_chunk _fy_atom_iter_add_chunk
#define fy_atom_iter_add_chunk_copy _fy_atom_iter_add_chunk_copy
#define fy_atom_iter_add_lb _fy_atom_iter_add_lb
#else
#define fy_atom_iter_add_chunk(_iter, _str, _len) \
	({ \
		const char *__str = (_str); \
		size_t __len2 = (_len); \
		char *__out = NULL; \
		int __ret = 0; \
		\
		if (__len2 > 0) { \
			__out = fy_utf8_format_text_alloc(__str, __len2, fyue_doublequote); \
			assert(__out); \
			fprintf(stderr, "%s:%d chunk #%zu \"%s\"\n", __func__, __LINE__, __len2, __out); \
			__ret = _fy_atom_iter_add_chunk((_iter), __str, __len2); \
			free(__out); \
		} \
		__ret; \
	})

#define fy_atom_iter_add_chunk_copy(_iter, _str, _len) \
	({ \
		const char *__str = (_str); \
		size_t __len2 = (_len); \
		char *__out = NULL; \
		int __ret = 0; \
		\
		if (__len2 > 0) { \
			__out = fy_utf8_format_text_alloc(__str, __len2, fyue_doublequote); \
			assert(__out); \
			fprintf(stderr, "%s:%d chunk-copy #%zu \"%s\"\n", __func__, __LINE__, __len2, __out); \
			/* fprintf(stderr, "%s:%d chunk-copy \"%.*s\"\n", __func__, __LINE__, (int)__len, __str); */ \
			__ret = _fy_atom_iter_add_chunk_copy((_iter), __str, __len2); \
			free(__out); \
		} \
		__ret; \
	})
#define fy_atom_iter_add_lb(_iter, _c) \
	({ \
		int __c = (_c); \
		fprintf(stderr, "%s:%d lb 0x%02x\n", __func__, __LINE__, __c); \
		_fy_atom_iter_add_lb((_iter), (_c)); \
	})
#endif

static void
fy_atom_iter_line_analyze(struct fy_atom_iter *iter, struct fy_atom_iter_line_info *li,
			  const char *line_start, size_t len)
{
	const struct fy_atom *atom = iter->atom;
	const char *s, *e, *ss;
	int col, c, cn, w, wn, ts, cws, advws;
	bool last_was_ws, is_block;
	int lastc;

	s = line_start;
	e = line_start + len;

	is_block = fy_atom_style_is_block(atom->style);

	/* short circuit non multiline, non ws atoms */
	if ((atom->direct_output && !atom->has_lb && !atom->has_ws) ||
	    (atom->style & FYAS_MANUAL_MARK)) {
		li->start = s;
		li->end = e;
		li->nws_start = s;
		li->nws_end = e;
		li->chomp_start = s;
		li->final = true;
		li->empty = atom->empty;
		li->trailing_breaks = 0;
		li->trailing_breaks_ws = false;
		li->start_ws = 0;
		li->end_ws = 0;
		li->indented = false;
		li->lb_end = is_block ? atom->ends_with_lb : false;
		li->final = true;
		li->actual_lb = -1;
		li->s_tb = li->e_tb = NULL;
		li->ends_with_backslash = false;
		return;
	}

	li->start = s;
	li->end = NULL;
	li->nws_start = NULL;
	li->nws_end = NULL;
	li->chomp_start = NULL;
	li->empty = true;
	li->trailing_breaks = 0;
	li->trailing_breaks_ws = false;
	li->first = false;
	li->start_ws = (size_t)-1;
	li->end_ws = (size_t)-1;
	li->indented = false;
	li->lb_end = false;
	li->final = false;
	li->actual_lb = -1;
	li->ends_with_backslash = false;

	last_was_ws = false;

	ts = atom->tabsize ? atom->tabsize : 8;	/* pick it up from the atom (if there is) */

	/* consecutive whitespace */
	cws = 0;

	lastc = -1;
	li->s_tb = s;
	for (col = 0, ss = s; (c = fy_utf8_get(ss, (e - ss), &w)) >= 0; ss += w) {

		lastc = c;

		/* mark start of chomp */
		if (is_block && !li->chomp_start && (unsigned int)col >= iter->chomp) {
			li->chomp_start = ss;

			/* if the character at the chomp point is whitespace
			 * then we're indented
			 */
			li->indented = fy_is_ws(c);

#if defined(DEBUG_CHUNK)
			fprintf(stderr, "%s:%d chomp_start=%d\n", __FILE__, __LINE__, (int)(li->chomp_start - li->start));
#endif

		}

		if (fy_is_lb_m(c, atom->lb_mode)) {

#ifdef DEBUG_CHUNK
			fprintf(stderr, "%s:%d lb=0x%x\n", __FILE__, __LINE__, c);
#endif

			col = 0;
			if (!li->end) {

				li->end = ss;
				li->end_ws = cws;
				li->lb_end = true;
				li->actual_lb = c;

#ifdef DEBUG_CHUNK
				fprintf(stderr, "%s:%d set actual_lb=0x%x\n", __FILE__, __LINE__, li->actual_lb);
#endif

				cws = 0;
			}

			/* no chomp point hit, use whatever we have here */
			if (is_block && !li->chomp_start) {
				li->chomp_start = ss;
#if defined(DEBUG_CHUNK)
				fprintf(stderr, "%s:%d chomp_start=%d\n", __FILE__, __LINE__, (int)(li->chomp_start - li->start));
#endif

			}

			if (!last_was_ws) {
				cws = 0;
				li->nws_end = ss;
				last_was_ws = true;
			}

#if defined(DEBUG_CHUNK)
			/* special CR/LF handling */
			if (c == '\r') {
				cn = ss < e ? fy_utf8_get(ss + w, (e - ss - w), &wn) : FYUG_EOF;
				if (cn == '\n') {
					ss += w;
					w = wn;
				}
			}
#endif

		} else if (fy_is_space(c)) {

			col++;
			cws++;

			if (!last_was_ws) {
				li->nws_end = ss;
				last_was_ws = true;
			}

		} else if (fy_is_tab(c)) {

			bool can_be_nws_end;

			advws = ts - (col % ts);
			col += advws;

#if defined(DEBUG_CHUNK)
			fprintf(stderr, "%s:%d tab col=%d chomp=%d\n", __FILE__, __LINE__, col, (int)(li->chomp_start - li->start));
#endif

			if (fy_atom_style_is_block(atom->style) && col >= (int)(li->chomp_start - li->start)) {
#if defined(DEBUG_CHUNK)
				fprintf(stderr, "%s:%d tab col=%d chomp=%d\n", __FILE__, __LINE__, col, (int)(li->chomp_start - li->start));
#endif
				goto do_nws;
			}

			cws += advws;

			can_be_nws_end = true;
			if (atom->style == FYAS_DOUBLE_QUOTED && ss > li->start && ss[-1] == '\\') {
				can_be_nws_end = false;
#ifdef DEBUG_CHUNK
				fprintf(stderr, "%s:%d backslashed tab\n", __FILE__, __LINE__);
#endif
			}

			if (can_be_nws_end && !last_was_ws) {
				li->nws_end = ss;
				last_was_ws = true;
			}

		} else {

			col++;
do_nws:
			/* mark start of non whitespace */
			if (!li->nws_start)
				li->nws_start = ss;

			if (li->empty)
				li->empty = false;

			if (li->start_ws == (size_t)-1)
				li->start_ws = cws;

			last_was_ws = false;
			cws = 0;
		}

		/* if we got both break */
		if (li->end && iter->chomp >= 0)
			break;
	}
	li->e_tb = ss;

	li->final = c < 0;

	if (li->final && atom->ends_with_eof) {

		/* mark start of chomp */
		if (is_block && !li->chomp_start && (unsigned int)col >= iter->chomp) {
			li->chomp_start = ss;

			/* if the character at the chomp point is whitespace
			 * then we're indented
			 */
			li->indented = fy_is_ws(lastc);
#ifdef DEBUG_CHUNK
			fprintf(stderr, "%s:%d is_block && !li->chomp_start && (unsigned int)col >= iter->chomp\n", __FILE__, __LINE__);
#endif
#if defined(DEBUG_CHUNK)
			fprintf(stderr, "%s:%d chomp_start=%d\n", __FILE__, __LINE__, (int)(li->chomp_start - li->start));
#endif

		}

		if (!li->end) {
			li->end = ss;
			li->end_ws = cws;
			li->lb_end = true;

#ifdef DEBUG_CHUNK
			fprintf(stderr, "%s:%d li->final && atom->ends_with_eof && !li->end\n", __FILE__, __LINE__);
#endif
			cws = 0;
		}

		/* no chomp point hit, use whatever we have here */
		if (is_block && !li->chomp_start) {
			li->chomp_start = ss;
#ifdef DEBUG_CHUNK
			fprintf(stderr, "%s:%d li->final && atom->ends_with_eof && !li->chomp_start\n", __FILE__, __LINE__);
#endif
#if defined(DEBUG_CHUNK)
			fprintf(stderr, "%s:%d chomp_start=%d\n", __FILE__, __LINE__, (int)(li->chomp_start - li->start));
#endif
		}

		if (!last_was_ws) {
			cws = 0;
			li->nws_end = ss;
			last_was_ws = true;
#ifdef DEBUG_CHUNK
			fprintf(stderr, "%s:%d li->final && atom->ends_with_eof && !last_was_ws\n", __FILE__, __LINE__);
#endif
		}
	}

	if (!last_was_ws)
		li->nws_end = ss;

	if (!li->nws_start)
		li->nws_start = ss;

	if (!li->nws_end)
		li->nws_end = ss;

	/* if we haven't hit the chomp, point use whatever we're now */
	if (is_block && !li->chomp_start) {
		li->chomp_start = ss;
#if defined(DEBUG_CHUNK)
		fprintf(stderr, "%s:%d chomp_start=%d\n", __FILE__, __LINE__, (int)(li->chomp_start - li->start));
#endif
	}

	if (li->start_ws == (size_t)-1)
		li->start_ws = 0;

	if (li->end_ws == (size_t)-1)
		li->end_ws = 0;

	/* mark next line to the end if no linebreak found */
	if (!li->end) {
		li->end = iter->e;
		li->last = true;
		li->end_ws = cws;
		li->lb_end = false;
		goto out;
	}

	/* find out if any trailing breaks exist afterwards */
	for (; (c = fy_utf8_get(ss, (e - ss), &w)) >= 0 && (fy_is_ws(c) || fy_is_lb_m(c, atom->lb_mode)); ss += w) {

		if (!li->trailing_breaks_ws && is_block && (unsigned int)col > iter->chomp)
			li->trailing_breaks_ws = true;

		if (fy_is_lb_m(c, atom->lb_mode)) {
			/* special CRLF handling */
			if (c == '\r') {
				cn = ss < e ? fy_utf8_get(ss + w, (e - ss - w), &wn) : FYUG_EOF;
				if (cn == '\n') {
#if defined(DEBUG_CHUNK)
					fprintf(stderr, "%s:%d trailing CRLF\n", __FILE__, __LINE__);
#endif
					ss += w;
					w = wn;
				}
			}
			li->trailing_breaks++;
			col = 0;
		} else {
			/* indented whitespace counts as break */
			if (fy_is_tab(c))
				col += (ts - (col % ts));
			else
				col++;
		}

	}

	/* and mark as last if only whitespace and breaks after this point */
	li->last = ss >= e;

out:
	assert(li->start);
	assert(li->end);
	assert(li->nws_start);
	assert(li->nws_end);
	assert(!is_block || li->chomp_start);

	li->ends_with_backslash = atom->style == FYAS_DOUBLE_QUOTED &&
				  !li->empty &&
				  (li->nws_end > li->nws_start && li->nws_end[-1] == '\\') &&
				  ((li->nws_end - li->nws_start) <= 1 || li->nws_end[-2] != '\\');
#ifdef DEBUG_CHUNK
	fprintf(stderr, "%s:%d ends_with_backslash=%s\n", __FILE__, __LINE__, li->ends_with_backslash ? "true" : "false");
#endif
}

void fy_atom_iter_start(const struct fy_atom *atom, struct fy_atom_iter *iter)
{
	struct fy_atom_iter_line_info *li;
	size_t len;

	if (!atom || !iter)
		return;

	memset(iter, 0, sizeof(*iter));

	iter->atom = atom;
	iter->s = fy_atom_data(atom);
	len = fy_atom_size(atom);
	iter->e = iter->s + len;

	iter->chomp = atom->increment;

	/* default tab size is 8 */
	iter->tabsize = atom->tabsize ? atom->tabsize : 8;

	memset(iter->li, 0, sizeof(iter->li));
	li = &iter->li[1];
	fy_atom_iter_line_analyze(iter, li, iter->s, len);
	li->first = true;

	/* if there's single quote at the start of a line ending the atom */
	iter->dangling_end_quote = atom->end_mark.column == 0;
	iter->single_line = atom->start_mark.line == atom->end_mark.line;
	iter->empty = atom->empty;
	iter->last_ends_with_backslash = li->ends_with_backslash;
#ifdef DEBUG_CHUNK
	fprintf(stderr, "%s:%d single_line=%s empty=%s last_ends_with_backslash=%s\n", __FILE__, __LINE__,
			iter->single_line ? "true" : "false",
			iter->empty ? "true" : "false",
			iter->last_ends_with_backslash ? "true" : "false");
#endif

	/* current is 0, next is 1 */
	iter->current = 0;

	iter->alloc = sizeof(iter->startup_chunks)/sizeof(iter->startup_chunks[0]);
	iter->top = 0;
	iter->read = 0;
	iter->chunks = iter->startup_chunks;

	iter->done = false;

	iter->unget_c = -1;
}

void fy_atom_iter_finish(struct fy_atom_iter *iter)
{
	if (iter->chunks && iter->chunks != iter->startup_chunks)
		free(iter->chunks);

	iter->chunks = NULL;
}

static const struct fy_atom_iter_line_info *
fy_atom_iter_line(struct fy_atom_iter *iter)
{
	const struct fy_atom *atom = iter->atom;
	struct fy_atom_iter_line_info *li, *nli;
	const char *ss;

	/* return while there's a next line */
	if (!iter)
		return NULL;

	/* make next line the current one */
	iter->current = !iter->current;

	li = &iter->li[iter->current];

	/* if we're out, we're out */
	if (li->start >= iter->e)
		return NULL;

	/* scan next line (special handling for '\r\n') */
	ss = li->end;
	if (!ss)
		return NULL;

	if (ss < iter->e) {
		if (*ss == '\r' && (ss + 1) < iter->e && ss[1] == '\n')
			ss += 2;
		else
			ss += fy_utf8_width_by_first_octet((uint8_t)*ss);
	}

	/* get current and next line */
	fy_atom_iter_line_analyze(iter, &iter->li[!iter->current], ss, iter->e - ss);

	/* if no more, mark the next as NULL */
	nli = &iter->li[!iter->current];
	if (nli->start >= iter->e)
		nli = NULL;

	/* for quoted, output the white space start */
	if (fy_atom_style_is_quoted(atom->style)) {
		li->s = li->first ? li->start : li->nws_start;
		li->e = li->last ? li->end : li->nws_end;

		/* just empty */
		if (li->empty && li->first && li->last && !iter->single_line)
			li->s = li->e;

	} else if (fy_atom_style_is_block(atom->style)) {
		li->s = li->chomp_start;
		li->e = li->end;
		if (li->empty && li->first && li->last)
			li->s = li->e;
	} else {
		li->s = li->nws_start;
		li->e = li->nws_end;
	}

	/* bah, I hate this, */
	if (li->s > li->e)
		li->s = li->e;

	assert(li->s <= li->e);

	/* we never fold LS or PS linebreaks (on yaml 1.1) */
	li->need_nl = fy_is_lb_LS_PS(li->actual_lb) && fy_is_lb_m(li->actual_lb, iter->atom->lb_mode) && !li->ends_with_backslash;
	li->need_sep = false;

#ifdef DEBUG_CHUNK
	fprintf(stderr, "%s:%d need_nl=%s\n", __FILE__, __LINE__, li->need_nl ? "true" : "false");
#endif
	if (li->need_nl)
		return li;

	switch (atom->style) {
	case FYAS_PLAIN:
	case FYAS_URI:
		li->need_nl = !li->last && li->empty;
		li->need_sep = !li->need_nl && nli && !nli->empty;
		break;

	case FYAS_COMMENT:
		li->need_nl = !li->final;
		li->need_sep = false;
		break;

	case FYAS_DOUBLE_QUOTED:
#ifdef DEBUG_CHUNK
		fprintf(stderr, "%s:%d ends_with_backslash=%s\n", __FILE__, __LINE__, li->ends_with_backslash ? "true" : "false");
#endif
		if (li->ends_with_backslash) {

			li->need_nl = false;
			li->need_sep = false;
			break;
		}

		/* fall-through */
	case FYAS_SINGLE_QUOTED:
		li->need_nl = (!li->last && !li->first && li->empty) ||
				(nli && iter->empty && !li->first);

		if (li->need_nl)
			break;

		li->need_sep = (nli && !nli->empty) ||
				(!nli && li->last && iter->dangling_end_quote) ||
				(nli && nli->final && nli->empty);

		break;
	case FYAS_LITERAL:
		li->need_nl = true;
		break;
	case FYAS_FOLDED:
		li->need_nl = !li->last && (li->empty || li->indented || li->trailing_breaks_ws || (nli && nli->indented));
		if (li->need_nl)
			break;
		li->need_sep = nli && !nli->indented && !nli->empty;
		break;

	case FYAS_PLAIN_MANUAL:
		li->need_nl = false;
		li->need_sep = false;
		break;

	case FYAS_SINGLE_QUOTED_MANUAL:
		li->need_nl = false;
		li->need_sep = false;
		break;

	case FYAS_DOUBLE_QUOTED_MANUAL:
		li->need_nl = false;
		li->need_sep = false;
		break;

	case FYAS_LITERAL_MANUAL:
		li->need_nl = false;
		li->need_sep = false;
		break;

	case FYAS_FOLDED_MANUAL:
		li->need_nl = false;
		li->need_sep = false;
		break;

	default:
		if (atom->style & FYAS_MANUAL_MARK) {
			li->need_nl = false;
			li->need_sep = false;
		}
		break;
	}

	return li;
}

static int
fy_atom_iter_format(struct fy_atom_iter *iter)
{
	const struct fy_atom *atom = iter->atom;
	const struct fy_atom_iter_line_info *li;
	const char *s, *e, *t;
	int value, code_length, ret;
	uint8_t code[4], *tt;
	int j, pending_nl;
	int *pending_lb = NULL, *pending_lb_new = NULL;
	int pending_lb_size = 0;
	enum fy_utf8_escape esc_mode;
	size_t rlen, i;

	/* done? */
	li = fy_atom_iter_line(iter);
	if (!li) {
		iter->done = true;
		return 0;
	}
	if (iter->done)
		return 0;

	s = li->s;
	e = li->e;

	switch (atom->style) {

	case FYAS_LITERAL:
	case FYAS_PLAIN:
	case FYAS_FOLDED:
	case FYAS_COMMENT:
		if (s < e) {
			ret = fy_atom_iter_add_chunk(iter, s, e - s);
			if (ret)
				goto out;
		}

		break;

	case FYAS_SINGLE_QUOTED:
		if (li->last)
			e = li->nws_end;
		while (s < e) {
			/* find next single quote */
			t = memchr(s, '\'', e - s);
			rlen = (t ? t : e) - s;

			ret = fy_atom_iter_add_chunk(iter, s, rlen);
			if (ret)
				goto out;

			/* end of string */
			if (!t)
				break;

			s = t;
			/* next character single quote too */
			if ((e - s) >= 2 && s[1] == '\'') {
				fy_atom_iter_add_chunk(iter, s, 1);
				s += 2;	/* skip over the two single quotes */
			} else
				s++;	/* skip over this single quote char */
		}
		break;

	case FYAS_DOUBLE_QUOTED:
		if (li->last)
			e = li->nws_end;

		esc_mode = atom->json_mode ? fyue_doublequote_json :
			   atom->lb_mode == fylb_cr_nl ? fyue_doublequote : fyue_doublequote_yaml_1_1;

		while (s < e) {
			/* find next escape */
			t = memchr(s, '\\', e - s);
			/* copy up to there (or end) */
			rlen = (t ? t : e) - s;
			ret = fy_atom_iter_add_chunk(iter, s, rlen);
			if (ret)
				goto out;

			if (!t || (e - t) < 2)
				break;

			ret = fy_utf8_parse_escape(&t, e - t, esc_mode);
			if (ret < 0)
				goto out;
			s = t;
			value = ret;

			tt = fy_utf8_put(code, sizeof(code), value);
			if (!tt) {
				ret = -1;
				goto out;
			}

			ret = fy_atom_iter_add_chunk_copy(iter, (const char *)code, tt - code);
			if (ret)
				goto out;
		}
		break;

	case FYAS_URI:
		while (s < e) {
			/* find next escape */
			t = memchr(s, '%', e - s);
			rlen = (t ? t : e) - s;
			ret = fy_atom_iter_add_chunk(iter, s, rlen);
			if (ret)
				goto out;

			/* end of string */
			if (!t)
				break;
			s = t;

			code_length = sizeof(code);
			t = fy_uri_esc(s, e - s, code, &code_length);
			if (!t) {
				ret = -1;
				goto out;
			}

			/* output escaped utf8 */
			ret = fy_atom_iter_add_chunk_copy(iter, (const char *)code, code_length);
			if (ret)
				goto out;
			s = t;
		}
		break;

	case FYAS_PLAIN_MANUAL:
	case FYAS_SINGLE_QUOTED_MANUAL:
	case FYAS_DOUBLE_QUOTED_MANUAL:
	case FYAS_LITERAL_MANUAL:
	case FYAS_FOLDED_MANUAL:
		/* manual scalar just goes out */
		if (s < e) {
			ret = fy_atom_iter_add_chunk(iter, s, e - s);
			if (ret)
				goto out;
			s = e;
		}
		break;

	default:
		ret = -1;
		goto out;
	}

	if (li->last) {

		if (fy_atom_style_is_block(atom->style)) {

			switch (atom->chomp) {
			case FYAC_STRIP:
			case FYAC_CLIP:

				pending_lb_size = 16;
				pending_lb = alloca(sizeof(*pending_lb) * pending_lb_size);

				pending_nl = 0;
				if (!li->empty) {
					pending_lb[0] = li->actual_lb > 0 ? li->actual_lb : '\n';
					pending_nl = 1;
				}
				while ((li = fy_atom_iter_line(iter)) != NULL) {
					if (!iter->empty && li->chomp_start < li->end) {
						for (j = 0; j < pending_nl; j++) {
							ret = fy_atom_iter_add_lb(iter, pending_lb[j]);
							if (ret)
								goto out;
						}
						pending_nl = 0;

						ret = fy_atom_iter_add_chunk(iter, li->chomp_start, li->end - li->chomp_start);
						if (ret)
							goto out;
					}
					if (li->lb_end && !iter->empty) {
						if (pending_nl >= pending_lb_size) {
							pending_lb_new = alloca(sizeof(*pending_lb) * pending_lb_size * 2);
							memcpy(pending_lb_new, pending_lb, sizeof(*pending_lb) * pending_lb_size);
							pending_lb_size *= 2;
							pending_lb = pending_lb_new;
						}
						pending_lb[pending_nl] = li->actual_lb > 0 ? li->actual_lb : '\n';
						pending_nl++;
					}
				}

				if (atom->chomp == FYAC_CLIP && (pending_nl || atom->ends_with_eof)) {
					ret = fy_atom_iter_add_lb(iter, pending_lb[0]);
					if (ret)
						goto out;
				}
				break;
			case FYAC_KEEP:
				if (li->lb_end || atom->ends_with_eof) {
					ret = fy_atom_iter_add_lb(iter, li->actual_lb > 0 ? li->actual_lb : '\n');
					if (ret)
						goto out;
				}

				/* nothing more if it's an EOF */
				if (atom->ends_with_eof && atom->empty)
					break;

				while ((li = fy_atom_iter_line(iter)) != NULL) {
					if (!iter->empty && li->chomp_start < li->end) {
						ret = fy_atom_iter_add_chunk(iter, li->chomp_start, li->end - li->chomp_start);
						if (ret)
							goto out;
					}
					if (li->lb_end) {
						ret = fy_atom_iter_add_lb(iter, li->actual_lb > 0 ? li->actual_lb : '\n');
						if (ret)
							goto out;
					}
				}
				break;
			}

			iter->done = true;

		} else {

#ifdef DEBUG_CHUNK
			fprintf(stderr, "%s:%d trailing_breaks=%zu end_ws=%zu empty=%s\n", __func__, __LINE__,
					li->trailing_breaks, li->end_ws,
					li->empty ? "true" : "false");
#endif

			if (li->trailing_breaks == 0 && li->end_ws > 0) {
				/* end of quote in a non-blank line having white space */
				ret = fy_atom_iter_add_chunk(iter, li->nws_end, (size_t)(li->e - li->nws_end));
				if (ret)
					goto out;
			} else if (li->trailing_breaks == 1) {

				if (atom->style == FYAS_DOUBLE_QUOTED) {
#ifdef DEBUG_CHUNK
					fprintf(stderr, "%s:%d %s <>%d single trailing break %c\n", __FILE__, __LINE__, __func__,
							(int)(li->s_tb - li->s), li->s_tb[-1]);
#endif
				}

				if (!li->ends_with_backslash) {
					ret = fy_atom_iter_add_chunk(iter, " ", 1);
					if (ret)
						goto out;
				}
			} else if (li->trailing_breaks > 1) {
				for (i = 0; i < li->trailing_breaks - 1; i++) {
					ret = fy_atom_iter_add_lb(iter, '\n');
					if (ret)
						goto out;
				}
			}

			iter->done = true;
		}

	} else {
		if (li->need_sep) {
			ret = fy_atom_iter_add_chunk(iter, " ", 1);
			if (ret)
				goto out;
		}

		if (li->need_nl) {
			ret = fy_atom_iter_add_lb(iter, li->actual_lb > 0 ? li->actual_lb : '\n');
			if (ret)
				goto out;
		}
	}

	/* got more */
	ret = 1;

out:
	return ret;
}

const struct fy_iter_chunk *
fy_atom_iter_peek_chunk(struct fy_atom_iter *iter)
{
	if (iter->read >= iter->top)
		return NULL;

	return &iter->chunks[iter->read].ic;
}

void fy_atom_iter_advance(struct fy_atom_iter *iter, size_t len)
{
	struct fy_atom_iter_chunk *ac;
	size_t rlen;

	/* while more and not out */
	while (len > 0 && iter->read < iter->top) {

		ac = iter->chunks + iter->read;

		/* get next run length */
		rlen = len > ac->ic.len ? ac->ic.len : len;

		/* remove from chunk */
		ac->ic.str += rlen;
		ac->ic.len -= rlen;

		/* advance if out of data */
		if (ac->ic.len == 0)
			iter->read++;

		/* remove run from length */
		len -= rlen;
	}

	/* reset when everything is gone */
	if (iter->read >= iter->top)
		fy_atom_iter_chunk_reset(iter);
}

const struct fy_iter_chunk *
fy_atom_iter_chunk_next(struct fy_atom_iter *iter, const struct fy_iter_chunk *curr, int *errp)
{
	const struct fy_iter_chunk *ic;
	int ret;

	ic = fy_atom_iter_peek_chunk(iter);
	if (curr && curr == ic)
		fy_atom_iter_advance(iter, ic->len);

	/* need to pull in data? */
	ic = fy_atom_iter_peek_chunk(iter);
	if (!curr || !ic) {
		fy_atom_iter_chunk_reset(iter);

		do {
			ret = fy_atom_iter_format(iter);

			/* either end or error, means we don't have data */
			if (ret <= 0) {
				if (errp)
					*errp = ret < 0 ? -1 : 0;
				return NULL;
			}

		} while (!fy_atom_iter_peek_chunk(iter));
	}
	ic = fy_atom_iter_peek_chunk(iter);
	if (errp)
		*errp = 0;
	return ic;
}

ssize_t fy_atom_format_text_length(struct fy_atom *atom)
{
	struct fy_atom_iter iter;
	const struct fy_iter_chunk *ic;
	size_t len;
	int ret;

	if (!atom)
		return -1;

	if (atom->storage_hint_valid)
		return (ssize_t)atom->storage_hint;

	len = 0;
	fy_atom_iter_start(atom, &iter);
	ic = NULL;
	while ((ic = fy_atom_iter_chunk_next(&iter, ic, &ret)) != NULL)
		len += ic->len;
	fy_atom_iter_finish(&iter);

	/* something funky going on here */
	if ((ssize_t)len < 0)
		return -1;

	if (ret != 0)
		return ret;

	atom->storage_hint = (size_t)len;
	atom->storage_hint_valid = true;
	return (ssize_t)len;
}

const char *fy_atom_format_text(struct fy_atom *atom, char *buf, size_t maxsz)
{
	struct fy_atom_iter iter;
	const struct fy_iter_chunk *ic;
	char *s, *e;
	int ret;

	if (!atom || !buf)
		return NULL;

	s = buf;
	e = s + maxsz;
	fy_atom_iter_start(atom, &iter);
	ic = NULL;
	while ((ic = fy_atom_iter_chunk_next(&iter, ic, &ret)) != NULL) {
		/* must fit (something is really off) */
		if ((size_t)(e - s) < ic->len) {
			ret = -1;
			break;
		}
		memcpy(s, ic->str, ic->len);
		s += ic->len;
	}
	fy_atom_iter_finish(&iter);

	if (ret != 0 || s >= e)
		return NULL;
	*s = '\0';

	return buf;
}

int fy_atom_format_utf8_length(struct fy_atom *atom)
{
	struct fy_atom_iter iter;
	const struct fy_iter_chunk *ic;
	const char *s, *e;
	size_t len, run, rem;
	int ret, w;

	if (!atom)
		return -1;

	len = 0;
	rem = 0;
	fy_atom_iter_start(atom, &iter);
	ic = NULL;
	while ((ic = fy_atom_iter_chunk_next(&iter, ic, &ret)) != NULL) {
		s = ic->str;
		e = s + ic->len;

		/* add the remainder */
		run = (size_t)(e - s) > rem ? rem : (size_t)(e - s);
		s += run;

		/* count utf8 characters */
		while (s < e) {
			w = fy_utf8_width_by_first_octet(*(uint8_t *)s);

			/* how many bytes of this run */
			run = (e - s) > w ? w : (e - s);
			/* the remainder of this run */
			rem = w - run;
			/* one more character */
			len++;
			/* and advance */
			s += run;
		}
	}
	fy_atom_iter_finish(&iter);

	/* something funky going on here */
	if ((int)len < 0)
		return -1;

	if (ret != 0)
		return ret;

	return (int)len;
}


struct fy_atom_iter *
fy_atom_iter_create(const struct fy_atom *atom)
{
	struct fy_atom_iter *iter;

	iter = malloc(sizeof(*iter));
	if (!iter)
		return NULL;
	if (atom)
		fy_atom_iter_start(atom, iter);
	else
		memset(iter, 0, sizeof(*iter));
	return iter;
}

void fy_atom_iter_destroy(struct fy_atom_iter *iter)
{
	if (!iter)
		return;

	fy_atom_iter_finish(iter);
	free(iter);
}

ssize_t fy_atom_iter_read(struct fy_atom_iter *iter, void *buf, size_t count)
{
	ssize_t nread;
	size_t nrun;
	const struct fy_iter_chunk *ic;
	int ret;

	if (!iter || !buf)
		return -1;

	ret = 0;
	nread = 0;
	while (count > 0) {
		ic = fy_atom_iter_peek_chunk(iter);
		if (ic) {
			nrun = count > ic->len ? ic->len : count;
			memcpy(buf, ic->str, nrun);
			nread += nrun;
			count -= nrun;
			fy_atom_iter_advance(iter, nrun);
			continue;
		}

		fy_atom_iter_chunk_reset(iter);
		do {
			ret = fy_atom_iter_format(iter);

			/* either end or error, means we don't have data */
			if (ret <= 0)
				return ret == 0 ? nread : -1;

		} while (!fy_atom_iter_peek_chunk(iter));
	}

	return nread;
}

int fy_atom_iter_getc(struct fy_atom_iter *iter)
{
	uint8_t buf;
	ssize_t nread;
	int c;

	if (!iter)
		return -1;

	/* first try the pushed ungetc */
	if (iter->unget_c >= 0) {
		c = iter->unget_c;
		/* unmatched getc/ungetc */
		if (fy_utf8_width(c) != 1)
			return -1;
		iter->unget_c = -1;
		return c;
	}

	/* read first octet */
	nread = fy_atom_iter_read(iter, &buf, 1);
	if (nread != 1)
		return -1;

	return (int)buf & 0xff;
}

int fy_atom_iter_ungetc(struct fy_atom_iter *iter, int c)
{
	if (!iter || c >= 0x80)
		return -1;

	if (iter->unget_c >= 0)
		return -1;
	if (c < 0) {
		iter->unget_c = -1;
		return 0;
	}
	iter->unget_c = c;
	return c;
}

int fy_atom_iter_peekc(struct fy_atom_iter *iter)
{
	int c;

	c = fy_atom_iter_getc(iter);
	if (c == -1)
		return -1;

	return fy_atom_iter_ungetc(iter, c);
}

int fy_atom_iter_utf8_get(struct fy_atom_iter *iter)
{
	uint8_t buf[4];	/* maximum utf8 is 4 octets */
	ssize_t nread;
	int c, w;

	if (!iter)
		return -1;

	/* first try the pushed ungetc */
	if (iter->unget_c >= 0) {
		c = iter->unget_c;
		iter->unget_c = -1;
		return c;
	}

	/* read first octet */
	nread = fy_atom_iter_read(iter, &buf[0], 1);
	if (nread != 1)
		return -1;

	/* get width from it (0 means illegal) */
	w = fy_utf8_width_by_first_octet(buf[0]);
	if (!w)
		return -1;

	/* read the rest octets (if possible) */
	if (w > 1) {
		nread = fy_atom_iter_read(iter, buf + 1, w - 1);
		if (nread != (w - 1))
			return -1;
	}

	/* and return the decoded utf8 character */
	return fy_utf8_get(buf, w, &w);
}

int fy_atom_iter_utf8_quoted_get(struct fy_atom_iter *iter, size_t *lenp, uint8_t *buf)
{
	ssize_t nread;
	int c, w, ww;

	if (!iter || !lenp || !buf || *lenp < 4)
		return -1;

	/* first try the pushed ungetc */
	if (iter->unget_c >= 0) {
		c = iter->unget_c;
		iter->unget_c = -1;
		*lenp = 0;
		return c;
	}

	/* read first octet */
	nread = fy_atom_iter_read(iter, &buf[0], 1);
	if (nread != 1)
		return -1;

	/* get width from it (0 means illegal) - return it and mark it */
	w = fy_utf8_width_by_first_octet(buf[0]);
	if (!w) {
		*lenp = 1;
		return 0;
	}

	/* read the rest octets (if possible) */
	if (w > 1) {
		nread = fy_atom_iter_read(iter, buf + 1, w - 1);
		if (nread != (w - 1)) {
			if (nread != -1 && nread < (w - 1))
				*lenp = nread;
			return 0;
		}
	}

	/* and return the decoded utf8 character */
	c = fy_utf8_get(buf, w, &ww);
	if (c >= 0) {
		*lenp = 0;
		return c;
	}
	*lenp = w;
	return 0;
}

int fy_atom_iter_utf8_unget(struct fy_atom_iter *iter, int c)
{
	if (!iter)
		return -1;

	if (iter->unget_c >= 0)
		return -1;
	if (c < 0) {
		iter->unget_c = -1;
		return 0;
	}
	if (!fy_utf8_is_valid(c))
		return -1;
	iter->unget_c = c;
	return c;
}

int fy_atom_iter_utf8_peek(struct fy_atom_iter *iter)
{
	int c;

	c = fy_atom_iter_utf8_get(iter);
	if (c == -1)
		return -1;

	return fy_atom_iter_utf8_unget(iter, c);
}

int fy_atom_memcmp(struct fy_atom *atom, const void *ptr, size_t len)
{
	const char *dstr, *str;
	size_t dlen, tlen;
	struct fy_atom_iter iter;
	int c, ct, ret;

	/* empty? just fine */
	if (!atom && !ptr && !len)
		return 0;

	/* empty atom but not ptr */
	if (!atom && (ptr || len))
		return -1;

	/* non empty atom and empty ptr */
	if (atom && (!ptr || !len))
		return 1;

	/* direct output, nice */
	if (atom->direct_output) {
		dlen = fy_atom_size(atom);
		dstr = fy_atom_data(atom);
		tlen = dlen > len ? len : dlen;
		ret = memcmp(dstr, ptr, tlen);
		if (ret)
			return ret;

		return dlen == len ? 0 : len > dlen ? -1 : 1;
	}

	str = ptr;
	ct = -1;
	fy_atom_iter_start(atom, &iter);
	while ((c = fy_atom_iter_getc(&iter)) >= 0 && len) {
		ct = *str & 0xff;
		if (ct != c)
			break;
		str++;
		len--;
	}
	fy_atom_iter_finish(&iter);

	/* out of data on both */
	if (c == -1 && !len)
		return 0;

	return ct > c ? -1 : 1;
}

int fy_atom_strcmp(struct fy_atom *atom, const char *str)
{
	size_t len;

	len = str ? strlen(str) : 0;

	return fy_atom_memcmp(atom, str, len);
}

bool fy_atom_is_number(struct fy_atom *atom)
{
	struct fy_atom_iter iter;
	int c, len, dec, fract, enot;
	bool first_zero;

	/* empty? just fine */
	if (!atom || atom->size0)
		return false;

	len = 0;

	fy_atom_iter_start(atom, &iter);

	/* skip minus sign if it's there */
	c = fy_atom_iter_utf8_peek(&iter);
	if (c == '-') {
		(void)fy_atom_iter_utf8_get(&iter);
		len++;
	}

	/* skip digits */
	first_zero = false;
	dec = 0;
	while ((c = fy_atom_iter_utf8_peek(&iter)) >= 0 && fy_is_digit(c)) {
		if (dec == 0 && c == '0')
			first_zero = true;
		else if (dec == 1 && first_zero)
			goto err_out;	/* 0[0-9] is bad */
		(void)fy_atom_iter_utf8_get(&iter);
		dec++;
		len++;
	}

	/* no digits is bad */
	if (!dec)
		goto err_out;

	fract = 0;
	/* dot? */
	c = fy_atom_iter_utf8_peek(&iter);
	if (c == '.') {

		(void)fy_atom_iter_utf8_get(&iter);
		len++;
		/* skip decimal part */
		while ((c = fy_atom_iter_utf8_peek(&iter)) >= 0 && fy_is_digit(c)) {
			(void)fy_atom_iter_utf8_get(&iter);
			len++;
			fract++;
		}

		/* . without fractional */
		if (!fract)
			goto err_out;
	}

	enot = 0;
	/* scientific notation */
	c = fy_atom_iter_utf8_peek(&iter);
	if (c == 'e' || c == 'E') {
		(void)fy_atom_iter_utf8_get(&iter);
		len++;

		/* skip sign if it's there */
		c = fy_atom_iter_utf8_peek(&iter);
		if (c == '+' || c == '-') {
			(void)fy_atom_iter_utf8_get(&iter);
			len++;
		}

		/* skip exponent part */
		while ((c = fy_atom_iter_utf8_peek(&iter)) >= 0 && fy_is_digit(c)) {
			(void)fy_atom_iter_utf8_get(&iter);
			len++;
			enot++;
		}

		if (!enot)
			goto err_out;
	}

	c = fy_atom_iter_utf8_peek(&iter);

	fy_atom_iter_finish(&iter);

	/* everything must be consumed (and something must) */
	return c < 0 && len > 0;

err_out:
	fy_atom_iter_finish(&iter);

	return false;
}

int fy_atom_cmp(struct fy_atom *atom1, struct fy_atom *atom2)
{
	struct fy_atom_iter iter1, iter2;
	const char *d1, *d2;
	size_t l1, l2, l;
	int c1, c2, ret;

	/* handles NULL case too */
	if (atom1 == atom2)
		return true;

	/* either null, can't do */
	if (!atom1 || !atom2)
		return false;

	/* direct output? */
	if (atom1->direct_output) {
		d1 = fy_atom_data(atom1);
		l1 = fy_atom_size(atom1);
	} else {
		d1 = NULL;
		l1 = 0;
	}
	if (atom2->direct_output) {
		d2 = fy_atom_data(atom2);
		l2 = fy_atom_size(atom2);
	} else {
		d2 = NULL;
		l2 = 0;
	}

	/* we have both atoms with direct output */
	if (d1 && d2) {
		l = l1 > l2 ? l2 : l1;
		ret = memcmp(d1, d2, l);
		if (ret)
			return ret;
		return l1 == l2 ? 0 : l2 > l1 ? -1 : 1;
	}

	/* only atom2 is direct */
	if (d2)
		return fy_atom_memcmp(atom1, d2, l2);

	/* only atom1 is direct, (note reversing sign) */
	if (d1)
		return -fy_atom_memcmp(atom2, d1, l1);

	/* neither is direct, do it with iterators */
	fy_atom_iter_start(atom1, &iter1);
	fy_atom_iter_start(atom2, &iter2);
	do {
		c1 = fy_atom_iter_getc(&iter1);
		c2 = fy_atom_iter_getc(&iter2);
	} while (c1 == c2 && c1 >= 0 && c2 >= 0);
	fy_atom_iter_finish(&iter2);
	fy_atom_iter_finish(&iter1);

	if (c1 == -1 && c2 == -1)
		return 0;

	return c2 > c1 ? -1 : 1;
}

const struct fy_raw_line *
fy_atom_raw_line_iter_next(struct fy_atom_raw_line_iter *iter)
{
	struct fy_raw_line *l;
	int c, w, col, col8, count;
	unsigned int ts;
	const char *s;

	if (!iter || !iter->rs || iter->rs > iter->ae)
		return NULL;

	l = &iter->line;

	ts = iter->atom->tabsize;

	/* track back to the start of the line */
	s = iter->rs;

	/* we allow a single zero size iteration */
	if (l->lineno > 0 && iter->rs >= iter->ae)
		return NULL;

	/* this may happen on illegal utf8 */
	if (!iter->is)
		return NULL;

	c = -1;
	while (s > iter->is) {
		c = fy_utf8_get_right(iter->is, (size_t)(s - iter->is), &w);
		if (c <= 0 || fy_is_lb_m(c, iter->atom->lb_mode))
			break;
		s -= w;
	}

	l->line_start = s;
	col = col8 = 0;
	count = 0;
	c = -1;
	w = 0;

	/* track until the start of the content */
	while (s < iter->as) {
		c = fy_utf8_get(s, (size_t)(iter->ae - s), &w);
		/* we should never hit that */
		if (c <= 0)
			return NULL;
		if (fy_is_tab(c)) {
			col8 += (8 - (col8 % 8));
			if (ts)
				col += (ts - (col % ts));
			else
				col++;
		} else if (!fy_is_lb_m(c, iter->atom->lb_mode)) {
			col++;
			col8++;
		} else
			return NULL;
		count++;

		s += w;
	}
	/* mark start of content */
	l->content_start = s;
	l->content_start_col = col;
	l->content_start_col8 = col8;
	l->content_start_count = count;

	/* track until the end of the content (or lb) */
	while (s < iter->ae) {
		c = fy_utf8_get(s, (size_t)(iter->ae - s), &w);
		/* we should never hit that */
		if (c <= 0)
			return NULL;
		if (fy_is_tab(c)) {
			col8 += (8 - (col8 % 8));
			if (ts)
				col += (ts - (col % ts));
			else
				col++;
		} else if (!fy_is_lb_m(c, iter->atom->lb_mode)) {
			col++;
			col8++;
		} else
			break;
		count++;

		s += w;
	}

	l->content_len = (size_t)(s - l->content_start);
	l->content_count = count - l->content_start_count;
	l->content_end_col = col;
	l->content_end_col8 = col8;

	/* if the stop was due to end of the atom */
	if (s >= iter->ae) {
		while (s < iter->ie) {
			c = fy_utf8_get(s, (size_t)(iter->ie - s), &w);
			/* just end of input */
			if (c <= 0)
				break;

			if (fy_is_tab(c)) {
				col8 += (8 - (col8 % 8));
				if (ts)
					col += (ts - (col % ts));
				else
					col++;
			} else if (!fy_is_lb_m(c, iter->atom->lb_mode)) {
				col++;
				col8++;
			} else
				break;
			count++;

			s += w;
		}
	}

	l->line_len = (size_t)(s - l->line_start);
	l->line_count = count;

	if (fy_is_lb_m(c, iter->atom->lb_mode)) {

		s += w;
		/* special case for MSDOS */
		if (c == '\r' && (s + 1 < iter->ie && s[1] == '\n'))
			s++;
		/* len_lb includes the lb */
		l->line_len_lb = (size_t)(s - l->line_start);
	} else
		l->line_len_lb = l->line_len;

	/* start at line #1 */
	l->lineno++;

	iter->rs = s;

	return l;
}

void fy_atom_raw_line_iter_start(const struct fy_atom *atom,
				 struct fy_atom_raw_line_iter *iter)
{
	struct fy_input *fyi;

	if (!atom || !iter)
		return;

	memset(iter, 0, sizeof(*iter));

	fyi = atom->fyi;
	if (!fyi)
		return;

	iter->atom = atom;

	iter->as = fy_atom_data(atom);
	iter->ae = iter->as + fy_atom_size(atom);

	iter->is = fy_input_start(fyi);
	iter->ie = iter->is + fy_input_size(fyi);

	iter->rs = iter->as;
}

void fy_atom_raw_line_iter_finish(struct fy_atom_raw_line_iter *iter)
{
	/* nothing */
}

/* returns the line of the atom, marking exactly where the atom is */
const char *fy_atom_lines_containing(struct fy_atom *atom, size_t *lenp)
{
	const struct fy_raw_line *l, *ln;
	struct fy_atom_raw_line_iter iter;
	bool first_line, last_line;
	const char *start, *end;

	if (!atom || !lenp) {
		if (lenp)
			*lenp = 0;
		return NULL;
	}

	start = end = NULL;

	fy_atom_raw_line_iter_start(atom, &iter);
	l = fy_atom_raw_line_iter_next(&iter);
	for (; l != NULL; l = ln) {
		/* get the next too */
		ln = fy_atom_raw_line_iter_next(&iter);

		first_line = l->lineno <= 1;
		last_line = ln == NULL;

		if (first_line)
			start = l->line_start;
		if (last_line)
			end = l->line_start + l->line_count;
	}
	fy_atom_raw_line_iter_finish(&iter);

	*lenp = end - start;
	return start;
}

unsigned int
fy_atom_text_analyze(struct fy_atom *handle, enum fy_atom_style style, int *maxspanp, int *maxcolp)
{
	unsigned int flags = 0;
	int c, cn, cnn, cp, col;
	uint8_t col0si, col0ei;	/* mask for --- ... at indent 0 */
	int span, maxspan, maxcol;
	struct fy_atom_iter iter;

	if (maxspanp)
		*maxspanp = 0;
	if (maxcolp)
		*maxcolp = 0;

	flags = FYTTAF_TEXT_TOKEN;

	/* hardwired and fast for regular plain scalars */
	if (style == FYAS_PLAIN && handle->storage_hint_valid &&
	    handle->direct_output && !handle->high_ascii &&
	    !handle->has_lb && !handle->has_ws && !handle->empty) {

		flags |= FYTTAF_DIRECT_OUTPUT;

		maxcol = (int)handle->storage_hint;
		maxspan = maxcol - 1;
		flags |=
			FYTTAF_DIRECT_OUTPUT |
			FYTTAF_CAN_BE_SIMPLE_KEY |
			FYTTAF_CAN_BE_PLAIN |
			FYTTAF_CAN_BE_SINGLE_QUOTED |
			FYTTAF_CAN_BE_DOUBLE_QUOTED |
			FYTTAF_CAN_BE_LITERAL |
			FYTTAF_CAN_BE_PLAIN_FLOW |
			FYTTAF_CAN_BE_UNQUOTED_PATH_KEY;

		goto done;
	}

	/* can this token be a simple key initial condition */
	if (!fy_atom_style_is_block(style) && style != FYAS_URI)
		flags |= FYTTAF_CAN_BE_SIMPLE_KEY;

	/* can this token be directly output initial condition */
	if (!fy_atom_style_is_block(style))
		flags |= FYTTAF_DIRECT_OUTPUT;

	fy_atom_iter_start(handle, &iter);

	col = 0;
	maxcol = 0;
	maxspan = 0;
	span = 0;

	/* get first character */
	cn = fy_atom_iter_utf8_get(&iter);
	if (cn < 0) {
		/* empty? */
		flags |= FYTTAF_SIZE0 | FYTTAF_EMPTY | FYTTAF_CAN_BE_UNQUOTED_PATH_KEY | FYTTAF_CAN_BE_SIMPLE_KEY;
		goto out;
	}

	flags |= FYTTAF_CAN_BE_PLAIN |
		 FYTTAF_CAN_BE_SINGLE_QUOTED |
		 FYTTAF_CAN_BE_DOUBLE_QUOTED |
		 FYTTAF_CAN_BE_LITERAL |
		 FYTTAF_CAN_BE_FOLDED |
		 FYTTAF_CAN_BE_PLAIN_FLOW |
		 FYTTAF_CAN_BE_UNQUOTED_PATH_KEY |
		 FYTTAF_ALL_WS_LB |
		 FYTTAF_ALL_PRINT_ASCII |
		 FYTTAF_EMPTY;

	col0si = col0ei = 0;

	/* disable folded right off the bat, it's a pain */
	flags &= ~FYTTAF_CAN_BE_FOLDED;

	/* plain scalars can't start with any indicator (or space/lb) */
	if ((flags & (FYTTAF_CAN_BE_PLAIN | FYTTAF_CAN_BE_PLAIN_FLOW))) {
		if (fy_is_start_indicator(cn) || fy_atom_is_lb(handle, cn) || fy_is_ws(cn))
			flags &= ~(FYTTAF_CAN_BE_PLAIN | FYTTAF_CAN_BE_PLAIN_FLOW);
	}

	/* plain scalars in flow mode can't start with a flow indicator */
	if ((flags & FYTTAF_CAN_BE_PLAIN_FLOW) &&
		fy_is_flow_indicator(cn))
		flags &= ~FYTTAF_CAN_BE_PLAIN_FLOW;

	if ((flags & (FYTTAF_CAN_BE_PLAIN | FYTTAF_CAN_BE_PLAIN_FLOW))) {
		cnn = fy_atom_iter_utf8_peek(&iter);
		if (fy_is_blankz_m(cnn, fy_atom_lb_mode(handle)) && fy_is_indicator_before_space(cn))
			flags &= ~(FYTTAF_CAN_BE_PLAIN | FYTTAF_CAN_BE_PLAIN_FLOW);
	}

	/* plain unquoted path keys can only start with [a-zA-Z_] */
	if ((flags & FYTTAF_CAN_BE_UNQUOTED_PATH_KEY) &&
		!fy_is_first_alpha(cn))
		flags &= ~FYTTAF_CAN_BE_UNQUOTED_PATH_KEY;

	/* if it starts with white space can't be a plain */
	if (fy_is_ws(cn)) {
		flags |= FYTTAF_HAS_START_WS;
		flags &= ~(FYTTAF_CAN_BE_PLAIN |
			   FYTTAF_CAN_BE_PLAIN_FLOW);
	}

	cp = -1;
	for (c = cn; c >= 0; cp = c, c = cn) {

		if (col <= 2) {
			if (cn == '-') {
				col0si |= (uint8_t)1 << col;
				if (col0si == 7)
					flags |= FYTTAF_HAS_START_IND | FYTTAF_QUOTE_AT_0;
			} else if (cn == '.') {
				col0ei |= (uint8_t)1 << col;
				if (col0ei == 7)
					flags |= FYTTAF_HAS_END_IND | FYTTAF_QUOTE_AT_0;
			}
		}

		/* can be -1 on end */
		cn = fy_atom_iter_utf8_get(&iter);

		/* zero can't be output, only in double quoted mode */
		if (c == 0) {
			flags &= ~(FYTTAF_DIRECT_OUTPUT |
				   FYTTAF_CAN_BE_PLAIN |
				   FYTTAF_CAN_BE_SINGLE_QUOTED |
				   FYTTAF_CAN_BE_LITERAL |
				   FYTTAF_CAN_BE_FOLDED |
				   FYTTAF_CAN_BE_PLAIN_FLOW |
				   FYTTAF_CAN_BE_UNQUOTED_PATH_KEY |
				   FYTTAF_ALL_WS_LB |
				   FYTTAF_ALL_PRINT_ASCII);
			flags |= FYTTAF_CAN_BE_DOUBLE_QUOTED;
			flags &= ~FYTTAF_EMPTY;
			flags |= FYTTAF_HAS_ZERO;
		} else if (fy_is_ws(c)) {

			flags |= FYTTAF_HAS_WS;
			if (fy_is_ws(cn)) {
				flags |= FYTTAF_HAS_CONSECUTIVE_WS;

				/* on a manual ' style we can't have linebreak and then consecutive ws */
				if (style == FYAS_SINGLE_QUOTED_MANUAL && fy_atom_is_lb(handle, cp))
					flags &= ~FYTTAF_CAN_BE_SINGLE_QUOTED;
			}

			/* non printable ascii */
			flags &= ~FYTTAF_ALL_PRINT_ASCII;

		} else if (fy_atom_is_lb(handle, c)) {

			flags |= FYTTAF_HAS_LB;
			if (fy_atom_is_lb(handle, cn))
				flags |= FYTTAF_HAS_CONSECUTIVE_LB;

			/* non printable ascii */
			flags &= ~FYTTAF_ALL_PRINT_ASCII;
			flags &= ~FYTTAF_EMPTY;

			/* only non linebreaks can be simple keys */
			flags &= ~FYTTAF_CAN_BE_SIMPLE_KEY;

			/* anything with linebreaks, can't be direct */
			flags &= ~FYTTAF_DIRECT_OUTPUT;

			if (c != '\n')
				flags |= FYTTAF_HAS_NON_NL_LB;
		} else {
			flags &= ~FYTTAF_EMPTY;
			flags &= ~FYTTAF_ALL_WS_LB;
			/* out of our comfort zone */
			if (c < '!' || c > '~')
				flags &= ~FYTTAF_ALL_PRINT_ASCII;
		}

		if ((flags & FYTTAF_CAN_BE_UNQUOTED_PATH_KEY) && !fy_is_alnum(c))
			flags &= ~FYTTAF_CAN_BE_UNQUOTED_PATH_KEY;

		/* illegal plain combination */
		if ((flags & FYTTAF_CAN_BE_PLAIN) &&
			((c == ':' && fy_is_blankz_m(cn, fy_atom_lb_mode(handle))) ||
			 (fy_is_blankz_m(c, fy_atom_lb_mode(handle)) && cn == '#') ||
			 (cp < 0 && c == '#' && cn < 0) ||
			 !fy_is_print(c))) {
			flags &= ~(FYTTAF_CAN_BE_PLAIN |
				   FYTTAF_CAN_BE_PLAIN_FLOW);
		}

		/* illegal plain flow combination */
		if ((flags & FYTTAF_CAN_BE_PLAIN_FLOW) &&
			(fy_is_flow_indicator(c) || (c == ':' && fy_is_flow_indicator(cn))))
			flags &= ~FYTTAF_CAN_BE_PLAIN_FLOW;

		/* non printable characters, turn off these styles */
		if (!fy_is_print(c)) {
			flags &= ~(FYTTAF_CAN_BE_SINGLE_QUOTED | FYTTAF_CAN_BE_LITERAL |
				   FYTTAF_CAN_BE_FOLDED);
			flags |= FYTTAF_HAS_NON_PRINT;
		}

		/* if there's an escape, it can't be direct */
		if ((flags & FYTTAF_DIRECT_OUTPUT) &&
		    ((style == FYAS_URI && c == '%') ||
		     (style == FYAS_SINGLE_QUOTED && c == '\'') ||
		     (style == FYAS_DOUBLE_QUOTED && c == '\\')))
			flags &= ~FYTTAF_DIRECT_OUTPUT;

		if (cn < 0 || !c || fy_atom_is_lb(handle, c) || fy_is_ws(c)) {
			if (span > maxspan)
				maxspan = span;
			span = 0;
		} else
			span++;

		if (fy_atom_is_lb(handle, c)) {
			if (col > maxcol)
				maxcol = col;
			col = 0;
			col0si = col0ei = 0;
		} else
			col++;

		if (fy_is_any_lb(c))
			flags |= FYTTAF_HAS_ANY_LB;

		/* last character */
		if (cn < 0) {
			/* if ends with whitespace or linebreak, or : can't be plain */
			if (fy_is_ws(c) || fy_atom_is_lb(handle, c) || c == ':') {
				flags &= ~(FYTTAF_CAN_BE_PLAIN |
					   FYTTAF_CAN_BE_PLAIN_FLOW);
				if (c == ':')
					flags |= FYTTAF_ENDS_WITH_COLON;
			}
		}
	}

	if (col > maxcol)
		maxcol = col;
	if (span > maxspan)
		maxspan = span;

out:
	/* if it's got nothing, it can be anything */
	if (flags & FYTTAF_SIZE0)
		flags |= FYTTAF_CAN_BE_PLAIN | FYTTAF_CAN_BE_PLAIN_FLOW |
			 FYTTAF_CAN_BE_SINGLE_QUOTED | FYTTAF_CAN_BE_DOUBLE_QUOTED |
			 FYTTAF_CAN_BE_LITERAL |  FYTTAF_CAN_BE_FOLDED;

	fy_atom_iter_finish(&iter);

done:
	if (maxspanp)
		*maxspanp = maxspan;
	if (maxcolp)
		*maxcolp = maxcol;
	return flags;
}
