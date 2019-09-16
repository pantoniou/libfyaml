/*
 * fy-utils.c - Generic utilities for functionality that's missing
 *              from platforms.
 *
 * For now only used to implement memstream for Apple platforms.
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "fy-utils.h"

#if defined(__APPLE__) && (_POSIX_C_SOURCE < 200809L)

/*
 * adapted from http://piumarta.com/software/memstream/
 *
 * Under the MIT license.
 */

/*
 * ----------------------------------------------------------------------------
 *
 * OPEN_MEMSTREAM(3)      BSD and Linux Library Functions     OPEN_MEMSTREAM(3)
 *
 * SYNOPSIS
 *     #include "memstream.h"
 *
 *     FILE *open_memstream(char **bufp, size_t *sizep);
 *
 * DESCRIPTION
 *     The open_memstream()  function opens a  stream for writing to  a buffer.
 *     The   buffer  is   dynamically  allocated   (as  with   malloc(3)),  and
 *     automatically grows  as required.  After closing the  stream, the caller
 *     should free(3) this buffer.
 *
 *     When  the  stream is  closed  (fclose(3))  or  flushed (fflush(3)),  the
 *     locations  pointed  to  by  bufp  and  sizep  are  updated  to  contain,
 *     respectively,  a pointer  to  the buffer  and  the current  size of  the
 *     buffer.  These values  remain valid only as long  as the caller performs
 *     no further output  on the stream.  If further  output is performed, then
 *     the  stream  must  again  be  flushed  before  trying  to  access  these
 *     variables.
 *
 *     A null byte  is maintained at the  end of the buffer.  This  byte is not
 *     included in the size value stored at sizep.
 *
 *     The stream's  file position can  be changed with fseek(3)  or fseeko(3).
 *     Moving the file position past the  end of the data already written fills
 *     the intervening space with zeros.
 *
 * RETURN VALUE
 *     Upon  successful  completion open_memstream()  returns  a FILE  pointer.
 *     Otherwise, NULL is returned and errno is set to indicate the error.
 *
 * CONFORMING TO
 *     POSIX.1-2008
 *
 * ----------------------------------------------------------------------------
 */

#ifndef min
#define min(X, Y) (((X) < (Y)) ? (X) : (Y))
#endif

struct memstream {
	size_t position;
	size_t size;
	size_t capacity;
	char *contents;
	char **ptr;
	size_t *sizeloc;
};

static int memstream_grow(struct memstream *ms, size_t minsize)
{
	size_t newcap;
	char *newcontents;

	newcap = ms->capacity * 2;
	while (newcap <= minsize + 1)
		newcap *= 2;
	newcontents = realloc(ms->contents, newcap);
	if (!newcontents)
		return -1;
	ms->contents = newcontents;
	memset(ms->contents + ms->capacity, 0, newcap - ms->capacity);
	ms->capacity = newcap;
	*ms->ptr = ms->contents;
	return 0;
}

static int memstream_read(void *cookie, char *buf, int count)
{
	struct memstream *ms = cookie;
	size_t n;

	n = min(ms->size - ms->position, (size_t)count);
	if (n < 1)
		return 0;
	memcpy(buf, ms->contents, n);
	ms->position += n;
	return n;
}

static int memstream_write(void *cookie, const char *buf, int count)
{
	struct memstream *ms = cookie;

	if (ms->capacity <= ms->position + (size_t)count &&
	    memstream_grow(ms, ms->position + (size_t)count) < 0)
		return -1;
	memcpy(ms->contents + ms->position, buf, count);
	ms->position += count;
	ms->contents[ms->position] = '\0';
	if (ms->size < ms->position)
		*ms->sizeloc = ms->size = ms->position;

	return count;
}

static fpos_t memstream_seek(void *cookie, fpos_t offset, int whence)
{
	struct memstream *ms = cookie;
	fpos_t pos= 0;

	switch (whence) {
	case SEEK_SET:
		pos = offset;
		break;
	case SEEK_CUR:
		pos = ms->position + offset;
		break;
	case SEEK_END:
		pos = ms->size + offset;
		break;
	default:
		errno= EINVAL;
		return -1;
	}
	if (pos >= (fpos_t)ms->capacity && memstream_grow(ms, pos) < 0)
		return -1;
	ms->position = pos;
	if (ms->size < ms->position)
		*ms->sizeloc = ms->size = ms->position;
	return pos;
}

static int memstream_close(void *cookie)
{
	struct memstream *ms = cookie;

	ms->size = min(ms->size, ms->position);
	*ms->ptr = ms->contents;
	*ms->sizeloc = ms->size;
	ms->contents[ms->size]= 0;
	/* ms->contents is what's returned */
	free(ms);
	return 0;
}

FILE *open_memstream(char **ptr, size_t *sizeloc)
{
	struct memstream *ms;
	FILE *fp;

	if (!ptr || !sizeloc) {
		errno= EINVAL;
		goto err_out;
	}

	ms = calloc(1, sizeof(struct memstream));
	if (!ms)
		goto err_out;

	ms->position = ms->size= 0;
	ms->capacity = 4096;
	ms->contents = calloc(ms->capacity, 1);
	if (!ms->contents)
		goto err_free_ms;
	ms->ptr = ptr;
	ms->sizeloc = sizeloc;
	fp= funopen(ms, memstream_read, memstream_write,
			memstream_seek, memstream_close);
	if (!fp)
		goto err_free_all;
	*ptr = ms->contents;
	*sizeloc = ms->size;
	return fp;

err_free_all:
	free(ms->contents);
err_free_ms:
	free(ms);
err_out:
	return NULL;
}

#endif /* __APPLE__ && _POSIX_C_SOURCE < 200809L */
