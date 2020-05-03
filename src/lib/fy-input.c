/*
 * fy-input.c - YAML input methods
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
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <libfyaml.h>

#include "fy-parse.h"
#include "fy-ctype.h"

#include "fy-input.h"

struct fy_input *fy_input_alloc(void)
{
	struct fy_input *fyi;

	fyi = malloc(sizeof(*fyi));
	if (!fyi)
		return NULL;
	memset(fyi, 0, sizeof(*fyi));

	fyi->state = FYIS_NONE;
	fyi->refs = 1;

	return fyi;
}

void fy_input_free(struct fy_input *fyi)
{
	if (!fyi)
		return;

	assert(fyi->refs == 1);

	switch (fyi->state) {
	case FYIS_NONE:
	case FYIS_QUEUED:
		/* nothing to do */
		break;
	case FYIS_PARSE_IN_PROGRESS:
	case FYIS_PARSED:
		fy_input_close(fyi);
		break;
	}

	/* always release the memory of the alloc memory */
	switch (fyi->cfg.type) {
	case fyit_alloc:
		free(fyi->cfg.alloc.data);
		break;

	default:
		break;
	}
	if (fyi->name)
		free(fyi->name);

	free(fyi);
}

struct fy_input *fy_input_ref(struct fy_input *fyi)
{
	if (!fyi)
		return NULL;


	assert(fyi->refs + 1 > 0);

	fyi->refs++;

	return fyi;
}

void fy_input_unref(struct fy_input *fyi)
{
	if (!fyi)
		return;

	assert(fyi->refs > 0);

	if (fyi->refs == 1)
		fy_input_free(fyi);
	else
		fyi->refs--;
}

const char *fy_input_get_filename(struct fy_input *fyi)
{
	if (!fyi)
		return NULL;

	return fyi->name;
}

static void fy_input_from_data_setup(struct fy_input *fyi,
				     struct fy_atom *handle, bool simple)
{
	const char *data;
	size_t size;
	unsigned int aflags;

	/* this is an internal method, you'd better to pass garbage */
	data = fy_input_start(fyi);
	size = fy_input_size(fyi);

	fyi->buffer = NULL;
	fyi->allocated = 0;
	fyi->read = 0;
	fyi->chunk = 0;
	fyi->fp = NULL;

	if (size > 0)
		aflags = fy_analyze_scalar_content(fyi, data, size);
	else
		aflags = FYACF_EMPTY | FYACF_FLOW_PLAIN | FYACF_BLOCK_PLAIN;

	handle->start_mark.input_pos = 0;
	handle->start_mark.line = 0;
	handle->start_mark.column = 0;
	handle->end_mark.input_pos = size;
	handle->end_mark.line = 0;
	handle->end_mark.column = fy_utf8_count(data, size);
	/* if it's plain, all is good */
	if (simple || (aflags & FYACF_FLOW_PLAIN)) {
		handle->storage_hint = size;	/* maximum */
		handle->storage_hint_valid = false;
		handle->direct_output = !!(aflags & FYACF_JSON_ESCAPE);
		handle->style = FYAS_PLAIN;
	} else {
		handle->storage_hint = 0;	/* just calculate */
		handle->storage_hint_valid = false;
		handle->direct_output = false;
		handle->style = FYAS_DOUBLE_QUOTED_MANUAL;
	}
	handle->empty = !!(aflags & FYACF_EMPTY);
	handle->has_lb = !!(aflags & FYACF_LB);
	handle->has_ws = !!(aflags & FYACF_WS);
	handle->starts_with_ws = !!(aflags & FYACF_STARTS_WITH_WS);
	handle->starts_with_lb = !!(aflags & FYACF_STARTS_WITH_LB);
	handle->ends_with_ws = !!(aflags & FYACF_ENDS_WITH_WS);
	handle->ends_with_lb = !!(aflags & FYACF_ENDS_WITH_LB);
	handle->trailing_lb = !!(aflags & FYACF_TRAILING_LB);
	handle->size0 = !!(aflags & FYACF_SIZE0);
	handle->valid_anchor = !!(aflags & FYACF_VALID_ANCHOR);

	handle->chomp = FYAC_STRIP;
	handle->increment = 0;
	handle->fyi = fyi;
	handle->tabsize = 0;

	fyi->state = FYIS_PARSED;
}

struct fy_input *fy_input_from_data(const char *data, size_t size,
				    struct fy_atom *handle, bool simple)
{
	struct fy_input *fyi;

	if (data && size == (size_t)-1)
		size = strlen(data);

	fyi = fy_input_alloc();
	if (!fyi)
		return NULL;

	fyi->cfg.type = fyit_memory;
	fyi->cfg.userdata = NULL;
	fyi->cfg.memory.data = data;
	fyi->cfg.memory.size = size;

	fy_input_from_data_setup(fyi, handle, simple);

	return fyi;
}

struct fy_input *fy_input_from_malloc_data(char *data, size_t size,
					   struct fy_atom *handle, bool simple)
{
	struct fy_input *fyi;

	if (data && size == (size_t)-1)
		size = strlen(data);

	fyi = fy_input_alloc();
	if (!fyi)
		return NULL;

	fyi->cfg.type = fyit_alloc;
	fyi->cfg.userdata = NULL;
	fyi->cfg.alloc.data = data;
	fyi->cfg.alloc.size = size;

	fy_input_from_data_setup(fyi, handle, simple);

	return fyi;
}

void fy_input_close(struct fy_input *fyi)
{
	if (!fyi)
		return;

	switch (fyi->cfg.type) {
	case fyit_file:
		if (fyi->file.fd != -1) {
			close(fyi->file.fd);
			fyi->file.fd = -1;
		}
		if (fyi->file.addr && fyi->file.addr && fyi->file.addr != MAP_FAILED) {
			munmap(fyi->file.addr, fyi->file.length);
			fyi->file.addr = NULL;
		}
		if (fyi->buffer) {
			free(fyi->buffer);
			fyi->buffer = NULL;
		}
		if (fyi->fp) {
			fclose(fyi->fp);
			fyi->fp = NULL;
		}
		break;

	case fyit_stream:
		if (fyi->buffer) {
			free(fyi->buffer);
			fyi->buffer = NULL;
		}
		memset(&fyi->stream, 0, sizeof(fyi->stream));
		break;

	case fyit_memory:
		/* nothing */
		break;

	case fyit_alloc:
		/* nothing */
		break;

	default:
		break;
	}
}

/* open a file for reading respecting the search path */
static int fy_path_open(struct fy_parser *fyp, const char *name, char **fullpathp)
{
	char *sp, *s, *e, *t, *newp;
	size_t len, maxlen;
	int fd;

	if (!fyp || !name || name[0] == '\0')
		return -1;

	/* for a full path, or no search path, open directly */
	if (name[0] == '/' || !fyp->cfg.search_path || !fyp->cfg.search_path[0])
		return open(name, O_RDONLY);

	len = strlen(fyp->cfg.search_path);
	sp = alloca(len + 1);
	memcpy(sp, fyp->cfg.search_path, len + 1);

	/* allocate the maximum possible so that we don't deal with reallocations */
	maxlen = len + 1 + strlen(name);
	newp = malloc(maxlen + 1);
	if (!newp)
		return -1;

	s = sp;
	e = sp + strlen(s);
	while (s < e) {
		/* skip completely empty */
		if (*s == ':') {
			s++;
			continue;
		}

		t = strchr(s, ':');
		if (t)
			*t++ = '\0';
		else
			t = e;

		len = strlen(s) + 1 + strlen(name) + 1;
		snprintf(newp, maxlen, "%s/%s", s, name);

		/* try opening */
		fd = open(newp, O_RDONLY);
		if (fd != -1) {
			fyp_scan_debug(fyp, "opened file %s at %s", name, newp);

			if (fullpathp)
				*fullpathp = newp;
			else
				free(newp);
			return fd;
		}

		s = t;
	}

	if (newp)
		free(newp);
	return -1;
}

int fy_parse_input_open(struct fy_parser *fyp, struct fy_input *fyi)
{
	struct stat sb;
	int rc;

	if (!fyi)
		return -1;

	assert(fyi->state == FYIS_QUEUED);

	/* reset common data */
	fyi->buffer = NULL;
	fyi->allocated = 0;
	fyi->read = 0;
	fyi->chunk = 0;
	fyi->fp = NULL;

	switch (fyi->cfg.type) {
	case fyit_file:
		memset(&fyi->file, 0, sizeof(fyi->file));
		fyi->file.fd = fy_path_open(fyp, fyi->cfg.file.filename, NULL);
		fyp_error_check(fyp, fyi->file.fd != -1, err_out,
				"failed to open %s",  fyi->cfg.file.filename);

		rc = fstat(fyi->file.fd, &sb);
		fyp_error_check(fyp, rc != -1, err_out,
				"failed to fstat %s", fyi->cfg.file.filename);

		fyi->file.length = sb.st_size;

		/* only map if not zero (and is not disabled) */
		if (sb.st_size > 0 && !(fyp->cfg.flags & FYPCF_DISABLE_MMAP_OPT)) {
			fyi->file.addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE,
					fyi->file.fd, 0);

			/* convert from MAP_FAILED to NULL */
			if (fyi->file.addr == MAP_FAILED)
				fyi->file.addr = NULL;
		}
		/* if we've managed to mmap, we' good */
		if (fyi->file.addr)
			break;

		fyp_scan_debug(fyp, "direct mmap mode unavailable for file %s, switching to stream mode",
				fyi->cfg.file.filename);

		fyi->fp = fdopen(fyi->file.fd, "r");
		fyp_error_check(fyp, rc != -1, err_out,
				"failed to fdopen %s", fyi->cfg.file.filename);

		/* fd ownership assigned to file */
		fyi->file.fd = -1;

		/* switch to stream mode */
		fyi->chunk = sysconf(_SC_PAGESIZE);
		fyi->buffer = malloc(fyi->chunk);
		fyp_error_check(fyp, fyi->buffer, err_out,
				"fy_alloc() failed");
		fyi->allocated = fyi->chunk;
		break;

	case fyit_stream:
		memset(&fyi->stream, 0, sizeof(fyi->stream));
		fyi->chunk = fyi->cfg.stream.chunk;
		if (!fyi->chunk)
			fyi->chunk = sysconf(_SC_PAGESIZE);
		fyi->buffer = malloc(fyi->chunk);
		fyp_error_check(fyp, fyi->buffer, err_out,
				"fy_alloc() failed");
		fyi->allocated = fyi->chunk;
		fyi->fp = fyi->cfg.stream.fp;
		break;

	case fyit_memory:
		/* nothing to do for memory */
		break;

	case fyit_alloc:
		/* nothing to do for memory */
		break;

	default:
		assert(0);
		break;
	}

	fyi->state = FYIS_PARSE_IN_PROGRESS;

	return 0;

err_out:
	fy_input_close(fyi);
	return -1;
}

int fy_parse_input_done(struct fy_parser *fyp)
{
	struct fy_input *fyi;
	void *buf;

	if (!fyp)
		return -1;

	fyi = fyp->current_input;
	if (!fyi)
		return 0;

	switch (fyi->cfg.type) {
	case fyit_file:
		if (fyi->file.addr)
			break;

		/* fall-through */

	case fyit_stream:
		fyp_error_check(fyp, fyp, err_out,
				"no parser associated with input");
		/* chop extra buffer */
		buf = realloc(fyi->buffer, fyp->current_input_pos);
		fyp_error_check(fyp, buf || !fyp->current_input_pos, err_out,
				"realloc() failed");

		fyi->buffer = buf;
		fyi->allocated = fyp->current_input_pos;
		break;
	default:
		break;

	}

	fyp_scan_debug(fyp, "moving current input to parsed inputs");

	fyi->state = FYIS_PARSED;
	fy_input_unref(fyi);

	fyp->current_input = NULL;

	return 0;

err_out:
	return -1;
}

const void *fy_parse_input_try_pull(struct fy_parser *fyp, struct fy_input *fyi,
				    size_t pull, size_t *leftp)
{
	const void *p;
	size_t left, pos, size, nread, nreadreq, missing;
	size_t space __FY_DEBUG_UNUSED__;
	void *buf;

	if (!fyp || !fyi) {
		if (leftp)
			*leftp = 0;
		return NULL;
	}

	p = NULL;
	left = 0;
	pos = fyp->current_input_pos;

	switch (fyi->cfg.type) {
	case fyit_file:

		if (fyi->file.addr) {
			assert(fyi->file.length >= pos);

			left = fyi->file.length - pos;
			if (!left) {
				fyp_scan_debug(fyp, "file input exhausted");
				break;
			}
			p = fyi->file.addr + pos;
			break;
		}

		/* fall-through */

	case fyit_stream:

		assert(fyi->read >= pos);

		left = fyi->read - pos;
		p = fyi->buffer + pos;

		/* enough to satisfy directly */
		if (left >= pull)
			break;

		/* no more */
		if (feof(fyi->fp) || ferror(fyi->fp)) {
			if (!left) {
				fyp_scan_debug(fyp, "input exhausted (EOF)");
				p = NULL;
			}
			break;
		}

		space = fyi->allocated - pos;

		/* if we're missing more than the buffer space */
		missing = pull - left;

		fyp_scan_debug(fyp, "input: space=%zu missing=%zu", space, missing);

		if (missing > 0) {

			/* align size to chunk */
			size = fyi->allocated + missing + fyi->chunk - 1;
			size = size - size % fyi->chunk;

			fyp_scan_debug(fyp, "input buffer missing %zu bytes (pull=%zu)",
					missing, pull);
			buf = realloc(fyi->buffer, size);
			fyp_error_check(fyp, buf, err_out,
					"realloc() failed");

			fyp_scan_debug(fyp, "stream read allocated=%zu new-size=%zu",
					fyi->allocated, size);

			fyi->buffer = buf;
			fyi->allocated = size;

			space = fyi->allocated - pos;
			p = fyi->buffer + pos;
		}

		/* always try to read up to the allocated space */
		do {
			nreadreq = fyi->allocated - fyi->read;

			fyp_scan_debug(fyp, "performing read request of %zu", nreadreq);

			nread = fread(fyi->buffer + fyi->read, 1, nreadreq, fyi->fp);

			fyp_scan_debug(fyp, "read returned %zu", nread);

			if (!nread)
				break;

			fyi->read += nread;
			left = fyi->read - pos;
		} while (left < pull);

		/* no more, move it to parsed input chunk list */
		if (!left) {
			fyp_scan_debug(fyp, "input exhausted (can't read enough)");
			p = NULL;
		}
		break;

	case fyit_memory:
		assert(fyi->cfg.memory.size >= pos);

		left = fyi->cfg.memory.size - pos;
		if (!left) {
			fyp_scan_debug(fyp, "memory input exhausted");
			break;
		}
		p = fyi->cfg.memory.data + pos;
		break;

	case fyit_alloc:
		assert(fyi->cfg.alloc.size >= pos);

		left = fyi->cfg.alloc.size - pos;
		if (!left) {
			fyp_scan_debug(fyp, "alloc input exhausted");
			break;
		}
		p = fyi->cfg.alloc.data + pos;
		break;


	default:
		assert(0);
		break;

	}

	if (leftp)
		*leftp = left;
	return p;

err_out:
	if (leftp)
		*leftp = 0;
	return NULL;
}

int fy_parse_input_append(struct fy_parser *fyp, const struct fy_input_cfg *fyic)
{
	struct fy_input *fyi = NULL;
	int ret;

	fyi = fy_input_alloc();
	fyp_error_check(fyp, fyp != NULL, err_out,
			"fy_input_alloc() failed!");

	fyi->cfg = *fyic;

	/* copy filename pointers and switch */
	switch (fyic->type) {
	case fyit_file:
		fyi->name = strdup(fyic->file.filename);
		break;
	case fyit_stream:
		if (fyic->stream.name)
			fyi->name = strdup(fyic->stream.name);
		else if (fyic->stream.fp == stdin)
			fyi->name = strdup("<stdin>");
		else {
			ret = asprintf(&fyi->name, "<stream-%d>",
					fileno(fyic->stream.fp));
			if (ret == -1)
				fyi->name = NULL;
		}
		break;
	case fyit_memory:
		ret = asprintf(&fyi->name, "<memory-@0x%p-0x%p>",
			fyic->memory.data, fyic->memory.data + fyic->memory.size - 1);
		if (ret == -1)
			fyi->name = NULL;
		break;
	case fyit_alloc:
		ret = asprintf(&fyi->name, "<alloc-@0x%p-0x%p>",
			fyic->memory.data, fyic->memory.data + fyic->memory.size - 1);
		if (ret == -1)
			fyi->name = NULL;
		break;
	default:
		assert(0);
		break;
	}
	fyp_error_check(fyp, fyi->name, err_out,
			"fyi->name alloc() failed!");

	fyi->buffer = NULL;
	fyi->allocated = 0;
	fyi->read = 0;
	fyi->chunk = 0;
	fyi->fp = NULL;

	switch (fyi->cfg.type) {
	case fyit_file:
		memset(&fyi->file, 0, sizeof(fyi->file));
		fyi->file.fd = -1;
		fyi->file.addr = MAP_FAILED;
		break;

		/* nothing for those two */
	case fyit_stream:
		memset(&fyi->stream, 0, sizeof(fyi->stream));
		break;

	case fyit_memory:
		/* nothing to do for memory */
		break;

	case fyit_alloc:
		/* nothing to do for memory */
		break;

	default:
		assert(0);
		break;
	}

	fyi->state = FYIS_QUEUED;
	fy_input_list_add_tail(&fyp->queued_inputs, fyi);

	return 0;

err_out:
	fy_input_unref(fyi);
	return -1;
}
