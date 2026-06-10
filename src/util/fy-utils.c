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
#include <ctype.h>

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#endif

#include "fy-win32.h"
#include "fy-utf8.h"
#include "fy-ctype.h"
#include "fy-utils.h"

#include "xxhash.h"

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

bool fy_tag_uri_is_valid(const char *data, size_t len)
{
	const char *s, *e;
	int w, j, k, width, c;
	uint8_t octet, esc_octets[4];

	s = data;
	e = s + len;

	while ((c = fy_utf8_get(s, e - s, &w)) >= 0) {
		if (c != '%') {
			s += w;
			continue;
		}

		width = 0;
		k = 0;
		do {
			/* short URI escape */
			if ((e - s) < 3)
				return false;

			if (width > 0) {
				c = fy_utf8_get(s, e - s, &w);
				if (c != '%')
					return false;
			}

			s += w;

			octet = 0;

			for (j = 0; j < 2; j++) {
				c = fy_utf8_get(s, e - s, &w);
				if (!fy_is_hex(c))
					return false;
				s += w;

				octet <<= 4;
				if (c >= '0' && c <= '9')
					octet |= c - '0';
				else if (c >= 'a' && c <= 'f')
					octet |= 10 + c - 'a';
				else
					octet |= 10 + c - 'A';
			}
			if (!width) {
				width = fy_utf8_width_by_first_octet(octet);

				if (width < 1 || width > 4)
					return false;
				k = 0;
			}
			esc_octets[k++] = octet;

		} while (--width > 0);

		/* now convert to utf8 */
		c = fy_utf8_get(esc_octets, k, &w);

		if (c < 0)
			return false;
	}

	return true;
}

int fy_tag_handle_length(const char *data, size_t len)
{
	const char *s, *e;
	int c, w;

	s = data;
	e = s + len;

	c = fy_utf8_get(s, e - s, &w);
	if (c != '!')
		return -1;
	s += w;

	c = fy_utf8_get(s, e - s, &w);
	if (c == -1)
		return (int)len;

	if (fy_is_ws(c))
		return (int)(s - data);
	/* if first character is !, empty handle */
	if (c == '!') {
		s += w;
		return (int)(s - data);
	}
	if (!fy_is_first_alpha(c))
		return -1;
	s += w;
	while (fy_is_alnum(c = fy_utf8_get(s, e - s, &w)))
		s += w;
	if (c == '!')
		s += w;

	return (int)(s - data);
}

int fy_tag_uri_length(const char *data, size_t len)
{
	const char *s, *e;
	int c, w, cn, wn, uri_length;

	s = data;
	e = s + len;

	while (fy_is_uri(c = fy_utf8_get(s, e - s, &w))) {
		cn = fy_utf8_get(s + w, e - (s + w), &wn);
		if ((fy_is_z(cn) || fy_is_blank(cn) || fy_is_any_lb(cn)) && fy_utf8_strchr(",}]", c))
			break;
		s += w;
	}
	uri_length = (int)(s - data);

	if (!fy_tag_uri_is_valid(data, uri_length))
		return -1;

	return uri_length;
}

int fy_tag_scan(const char *data, size_t len, struct fy_tag_scan_info *info)
{
	const char *s, *e;
	int total_length, handle_length, uri_length, prefix_length, suffix_length;
	int c, cn, w, wn;
	bool bare;

	s = data;
	e = s + len;

	prefix_length = 0;

	/* it must start with '!' */
	c = fy_utf8_get(s, e - s, &w);
	if (c == '!') {
		bare = false;
		cn = fy_utf8_get(s + w, e - (s + w), &wn);
		if (cn == '<') {
			prefix_length = 2;
			suffix_length = 1;
		} else
			prefix_length = suffix_length = 0;

		if (prefix_length) {
			handle_length = 0; /* set the handle to '' */
			s += prefix_length;
		} else {
			/* either !suffix or !handle!suffix */
			/* we scan back to back, and split handle/suffix */
			handle_length = fy_tag_handle_length(s, e - s);
			if (handle_length < 0)
				return -1;
			s += handle_length;
		}
	} else {
		bare = true;
		/* it's just a uri */
		prefix_length = 0;
		handle_length = 0;
		suffix_length = 0;
	}

	uri_length = fy_tag_uri_length(s, e - s);
	if (uri_length < 0)
		return -1;

	/* a handle? */
	if (!bare && !prefix_length && (handle_length == 0 || data[handle_length - 1] != '!')) {
		/* special case, '!', handle set to '' and suffix to '!' */
		if (handle_length == 1 && uri_length == 0) {
			handle_length = 0;
			uri_length = 1;
		} else {
			uri_length = handle_length - 1 + uri_length;
			handle_length = 1;
		}
	}
	total_length = prefix_length + handle_length + uri_length + suffix_length;

	if (total_length != (int)len)
		return -1;

	info->total_length = total_length;
	info->handle_length = handle_length;
	info->uri_length = uri_length;
	info->prefix_length = prefix_length;
	info->suffix_length = suffix_length;

	return 0;
}

/* simple terminal methods; mainly for getting size of terminal */
/* These functions are not available on Windows */
#ifndef _WIN32
static int
fy_term_set_raw(int fd, struct termios *oldt)
{
	struct termios newt, t;
	int ret;

	/* must be a terminal */
	if (!isatty(fd))
		return -1;

	ret = tcgetattr(fd, &t);
	if (ret != 0)
		return ret;

	newt = t;

	cfmakeraw(&newt);

	ret = tcsetattr(fd, TCSANOW, &newt);
	if (ret != 0)
		return ret;

	if (oldt)
		*oldt = t;

	return 0;
}

static int
fy_term_restore(int fd, const struct termios *oldt)
{
	/* must be a terminal */
	if (!isatty(fd))
		return -1;

	return tcsetattr(fd, TCSANOW, oldt);
}

static ssize_t
fy_term_write(int fd, const void *data, size_t count)
{
	ssize_t wrn, r;

	if (!isatty(fd))
		return -1;
	r = 0;
	wrn = 0;
	while (count > 0) {
		do {
			r = write(fd, data, count);
		} while (r == -1 && errno == EAGAIN);
		if (r < 0)
			break;
		wrn += r;
		data += r;
		count -= r;
	}

	/* return the amount written, or the last error code */
	return wrn > 0 ? wrn : r;
}

static int
fy_term_safe_write(int fd, const void *data, size_t count)
{
	if (!isatty(fd))
		return -1;

	return fy_term_write(fd, data, count) == (ssize_t)count ? 0 : -1;
}

static ssize_t
fy_term_read(int fd, void *data, size_t count, int timeout_us)
{
	ssize_t rdn, r;
	struct timeval tv, tvto, *tvp;
	fd_set rdfds;

	if (!isatty(fd))
		return -1;

	FD_ZERO(&rdfds);

	memset(&tvto, 0, sizeof(tvto));
	memset(&tv, 0, sizeof(tv));

	if (timeout_us >= 0) {
		tvto.tv_sec = timeout_us / 1000000;
		tvto.tv_usec = timeout_us % 1000000;
		tvp = &tv;
	} else {
		tvp = NULL;
	}

	r = 0;
	rdn = 0;
	while (count > 0) {
		do {
			FD_SET(fd, &rdfds);
			if (tvp)
				*tvp = tvto;
			r = select(fd + 1, &rdfds, NULL, NULL, tvp);
		} while (r == -1 && errno == EAGAIN);

		/* select ends, or something weird */
		if (r <= 0 || !FD_ISSET(fd, &rdfds))
			break;

		/* now read */
		do {
			r = read(fd, data, count);
		} while (r == -1 && errno == EAGAIN);
		if (r < 0)
			break;

		rdn += r;
		data += r;
		count -= r;
	}

	/* return the amount written, or the last error code */
	return rdn > 0 ? rdn : r;
}

static ssize_t
fy_term_read_escape(int fd, void *buf, size_t count)
{
	char *p;
	int r, rdn;
	char c;

	/* at least 3 characters */
	if (count < 3)
		return -1;

	p = buf;
	rdn = 0;

	/* ESC */
	r = fy_term_read(fd, &c, 1, 100 * 1000);
	if (r != 1 || c != '\x1b')
		return -1;
	*p++ = c;
	count--;
	rdn++;

	/* [ */
	r = fy_term_read(fd, &c, 1, 100 * 1000);
	if (r != 1 || c != '[')
		return rdn;
	*p++ = c;
	count--;
	rdn++;

	/* read until error, out of buffer, or < 0x40 || > 0x7e */
	r = -1;
	while (count > 0) {
		r = fy_term_read(fd, &c, 1, 100 * 1000);
		if (r != 1)
			r = -1;
		if (r != 1)
			break;
		*p++ = c;
		count--;
		rdn++;

		/* end of escape */
		if (c >= 0x40 && c <= 0x7e)
			break;
	}

	return rdn;
}

static int
fy_term_query_size_raw(int fd, int *rows, int *cols)
{
	char buf[32];
	char *s, *e;
	ssize_t r;

	/* must be a terminal */
	if (!isatty(fd))
		return -1;

	*rows = *cols = 0;

	/* query text area */
	r = fy_term_safe_write(fd, "\x1b[18t", 5);
	if (r != 0)
		return r;

	/* read a character */
	r = fy_term_read_escape(fd, buf, sizeof(buf));

	/* return must be ESC[8;<height>;<width>;t */

	if (r < 8 || r >= (int)sizeof(buf) - 2)	/* minimum ESC[8;1;1t */
		return -1;

	s = buf;
	e = s + r;

	/* correct response? starts with ESC[8; */
	if (s[0] != '\x1b' || s[1] != '[' || s[2] != '8' || s[3] != ';')
		return -1;
	s += 4;

	/* must end with t */
	if (e[-1] != 't')
		return -1;
	*--e = '\0';	/* remove trailing t, and zero terminate */

	/* scan two ints separated by ; */
	r = sscanf(s, "%d;%d", rows, cols);
	if (r != 2)
		return -1;

	return 0;
}

int fy_term_query_size(int fd, int *rows, int *cols)
{
	struct termios old_term;
	int ret, r;

	if (!isatty(fd))
		return -1;

	r = fy_term_set_raw(fd, &old_term);
	if (r != 0)
		return -1;

	ret = fy_term_query_size_raw(fd, rows, cols);

	r = fy_term_restore(fd, &old_term);
	if (r != 0)
		return -1;

	return ret;
}

#else /* _WIN32 */

int fy_term_query_size(int fd, int *rows, int *cols)
{
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	HANDLE h;

	if (fd != _fileno(stdout) && fd != _fileno(stderr))
		return -1;

	h = GetStdHandle(STD_OUTPUT_HANDLE);
	if (!GetConsoleScreenBufferInfo(h, &csbi))
		return -1;

	*cols = (int)(csbi.srWindow.Right  - csbi.srWindow.Left + 1);
	*rows = (int)(csbi.srWindow.Bottom - csbi.srWindow.Top  + 1);
	return 0;
}

#endif /* !_WIN32 - end of terminal functions */

int fy_comment_iter_begin(const char *comment, size_t size, struct fy_comment_iter *iter)
{
	if (!comment || !iter)
		return -1;

	memset(iter, 0, sizeof(*iter));
	iter->start = comment;
	iter->size = size == (size_t)-1 ? strlen(comment) : size;
	iter->end = iter->start + iter->size;

	if (!iter->size)
		return -1;

	iter->next = iter->start;
	iter->line = 0;

	return 0;
}

const char *fy_comment_iter_next_line(struct fy_comment_iter *iter, size_t *lenp)
{
	const char *s, *e, *le, *t;

	if (!iter || !lenp || !iter->start)
		return NULL;

	/* no more */
	if (iter->next >= iter->end)
		return NULL;
again:
	*lenp = 0;

	s = iter->next;
	e = iter->end;

	/* skip whitespace */
	while (s < e && isblank((unsigned char)*s))
		s++;

	if (s >= e)
		return NULL;

	/* find end of line */
	le = memchr(s, '\n', e - s);
	if (!le) {
		le = e;
		iter->next = e;
	} else {
		iter->next = le + 1;
	}

	/* final? check if ends in *\/ */
	if (le >= e && (le - s) > 2 && le[-2] == '*' && le[-1] == '/')
		le -= 2;

	/* backtrack while there's space at the end of line */
	while (le > s && isblank((unsigned char)le[-1]))
		le--;

	/* check if the whole line is punctuation so it's formatting */
	t = s;
	while (t < le && ispunct((unsigned char)*t))
		t++;

	/* everything is punctuation? */
	if (t > s && t >= le) {
		if (((le - s) == 2 && s[0] == '/' && s[1] == '/') || 	/* '//' -> empty line */
		    ((le - s) == 1 && s[0] == '*')) {			/* '*' -> empty line */
			iter->line++;
			return "";
		}
		/* for anything else, just try again */
		goto again;
	}

	/* something is there, skip over '// ' or '* ' */
	if ((le - s) > 3 && s[0] == '/' && s[1] == '/' && isblank((unsigned char)s[2]))
		s += 3;
	else if ((le - s) > 2 && s[0] == '*' && isblank((unsigned char)s[1]))
		s += 2;
	else if (iter->line == 0 && (le - s) > 3 && s[0] == '/' && s[1] == '*' && isblank((unsigned char)s[2]))
		s += 3;

	iter->line++;
	*lenp = le - s;
	return s;
}

void fy_comment_iter_end(struct fy_comment_iter *iter FY_UNUSED)
{
	/* nothing */
}

char *fy_get_cooked_comment(const char *raw_comment, size_t size)
{
	struct fy_memstream *fyms = NULL;
	struct fy_comment_iter iter;
	FILE *fp;
	char *buf;
	const char *line;
	size_t len, line_len;
	int ret;

	if (!raw_comment)
		return NULL;

	fyms = fy_memstream_open(&fp);
	if (!fyms)
		return NULL;

	ret = 0;
	fy_comment_iter_begin(raw_comment, size, &iter);
	while ((line = fy_comment_iter_next_line(&iter, &line_len)) != NULL) {
		ret = fprintf(fp, "%.*s\n", (int)line_len, line);
		if (ret < 0)
			break;
	}
	fy_comment_iter_end(&iter);

	buf = fy_memstream_close(fyms, &len);
	if (ret < 0) {
		if (buf)
			free(buf);
		return NULL;
	}

	/* must be freed */
	return buf;
}

int fy_keyword_iter_begin(const char *text, size_t size, const char *keyword, struct fy_keyword_iter *iter)
{
	if (!text || !size || !keyword || !iter)
		return -1;

	memset(iter, 0, sizeof(*iter));
	iter->keyword = keyword;
	iter->keyword_len = strlen(keyword);
	iter->start = text;
	iter->size = size == (size_t)-1 ? strlen(text) : size;
	iter->end = iter->start + iter->size;
	iter->pc = '\n';

	if (!iter->size)
		return -1;

	iter->next = iter->start;

	return 0;
}

const char *fy_keyword_iter_next(struct fy_keyword_iter *iter)
{
	const char *keyword;
	size_t keyword_len;
	const char *s, *e;
	int c, w, pc, mode;

	if (!iter || !iter->start)
		return NULL;

	/* no more */
	if (iter->next >= iter->end)
		return NULL;

	s = iter->next;
	e = iter->end;

	keyword = iter->keyword;
	keyword_len = iter->keyword_len;

	mode = 0;
	pc = iter->pc;
	while ((c = fy_utf8_get_s(s, e, &w)) >= 0) {

		/* simple state machine to handle quoting */
		switch (mode) {
		case 0: /* unquoted */
			if (c == keyword[0] && (fy_is_any_lb(pc) || fy_is_ws(pc)) &&
			   (size_t)(e - s) > (keyword_len + 1) && !memcmp(s, keyword, keyword_len) &&
			   (fy_is_ws(s[keyword_len]) || fy_is_any_lb(s[keyword_len]))) {

				iter->next = s;
				iter->pc = pc;
				return s;
			}

			if (c == '\'')
				mode = 1;
			else if (c == '"')
				mode = 3;
			break;
		case 1:	/* single quote */
			if (c == '\\')
				mode = 2;	/* escaped single quote? */
			else if (c == '\'')
				mode = 0;	/* back to unquoted */
			break;
		case 2:	/* single quote backslash */
			mode = 1;	/* back to single quote mode always */
			break;
		case 3: /* double quote */
			if (c == '\\')
				mode = 4;	/* escaped quote? */
			else if (c == '"')
				mode = 0;	/* back to unquoted */
			break;
		case 4:
			mode = 3;	/* back to double quote mode always */
			break;
		}
		s += w;
		pc = c;
	}

	return NULL;
}

void fy_keyword_iter_advance(struct fy_keyword_iter *iter, size_t advance)
{
	int w;
	const char *prev;

	if (!iter || !iter->next)
		return;

	prev = iter->next;

	iter->next += advance;
	if (iter->next >= iter->end)
		iter->next = iter->end;

	iter->pc = fy_utf8_get_right(prev, (size_t)(iter->next - prev), &w);
	if (iter->pc < 0)
		iter->pc = '\n';
}

void fy_keyword_iter_end(struct fy_keyword_iter *iter FY_UNUSED)
{
	/* nothing */
}

#define FY_XXHASH64_SEED	((uint64_t)0x1973198120142019U)

uint64_t fy_iovec_xxhash64(const struct iovec *iov, int iovcnt)
{
	XXH64_state_t xxstate;
	uint64_t hash;
	int i;

	if (iovcnt == 1) {
		/* for the common case this is much faster for small inputs */
		hash = XXH64(iov[0].iov_base, iov[0].iov_len, FY_XXHASH64_SEED);
	} else {
		XXH64_reset(&xxstate, FY_XXHASH64_SEED);
		for (i = 0; i < iovcnt; i++)
			XXH64_update(&xxstate, iov[i].iov_base, iov[i].iov_len);
		hash = XXH64_digest(&xxstate);
	}

	/* XXX we never return a hash value of zero */
	if (!hash)
		hash++;
	return hash;
}

struct fy_memstream {
	FILE *fp;
	char *buf;
	size_t size;
};

struct fy_memstream *fy_memstream_open(FILE **fpp)
{
	struct fy_memstream *fyms;

	if (!fpp)
		return NULL;

	*fpp = NULL;

	fyms = malloc(sizeof(*fyms));
	if (!fyms)
		return NULL;
	memset(fyms, 0, sizeof(*fyms));
#if !defined(_WIN32)
	fyms->fp = open_memstream(&fyms->buf, &fyms->size);
#else
	fyms->fp = tmpfile();
#endif
	if (!fyms->fp) {
		free(fyms);
		return NULL;
	}
	*fpp = fyms->fp;
	return fyms;
}

char *fy_memstream_close(struct fy_memstream *fyms, size_t *sizep)
{
	char *buf;
#if defined(_WIN32)
	long sz;
#endif

	if (!fyms) {
		*sizep = 0;
		return NULL;
	}

#if defined(_WIN32)
	if (fyms->fp && fflush(fyms->fp) == 0 &&
	    (sz = ftell(fyms->fp)) >= 0 &&
	    (fyms->buf = malloc((size_t)sz + 1)) != NULL) {
		rewind(fyms->fp);
		fyms->size = fread(fyms->buf, 1, (size_t)sz, fyms->fp);
		fyms->buf[fyms->size] = '\0';
	}
#endif

	if (fyms->fp)
		fclose(fyms->fp);

	/* the buffer is updated only after close */
	buf = fyms->buf;
	*sizep = buf ? fyms->size : 0;

	free(fyms);

	return buf;
}

#ifdef _WIN32
wchar_t *fy_win32_u8tow(const char *s)
{
	int n;
	wchar_t *w;

	n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, 0, 0);
	if (!n) {
		errno = EINVAL;
		return NULL;
	}

	w = malloc(n * sizeof(*w));
	if (!w)
		return NULL;

	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) <= 0) {
		free(w);
		errno = EINVAL;
		return NULL;
	}
	return w;
}

char *fy_win32_wtou8(const wchar_t *w)
{
	int n;
	char *s;

	n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
	if (n <= 0)
		return NULL;

	s = malloc((size_t)n);
	if (!s)
		return NULL;

	if (WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL) <= 0) {
		free(s);
		return NULL;
	}

	return s;
}
#endif

// this function is merely a helper - do not use it for security stuff
FILE *fy_tmpfile(char *path, size_t pathsz)
{
	int fd = -1;
	FILE *fp = NULL;
	char temppath[PATH_MAX + 1];
#ifdef _WIN32
	char tempdir[PATH_MAX + 1];
	DWORD rc;
#else
	const char *tmpdir = NULL;
#endif

	if (!path || !pathsz)
		return NULL;

	path[0] = '\0';

#ifdef _WIN32
	rc = GetTempPathA(sizeof(tempdir), tempdir);
	if (rc == 0 || rc >= sizeof(tempdir))
		goto err_out;

	rc = GetTempFileNameA(tempdir, "fyh", 0, temppath);
	if (rc == 0)
		goto err_out;
#else
	tmpdir = getenv("TMPDIR");
	if (!tmpdir || !*tmpdir)
		tmpdir = "/tmp";

	snprintf(temppath, sizeof(temppath), "%s/libfyaml_XXXXXX", tmpdir);
	temppath[sizeof(temppath) - 1] = '\0';
#endif

	if (strlen(temppath) + 1 > pathsz)
		goto err_out;
	strncpy(path, temppath, pathsz);

#ifdef _WIN32
	fd = open(path, O_BINARY | O_CREAT | O_EXCL | O_RDWR);
#else
	fd = mkstemp(path);
#endif
	if (fd < 0)
		goto err_out;

	fp = fdopen(fd, "wb");
	if (!fp)
		goto err_out;

	return fp;

err_out:
	if (fd < 0)
		close(fd);
	if (fp)
		fclose(fp);
	if (path && path[0])
		(void)remove(path);
	return NULL;
}

int fy_create_tmpfile(char *path, size_t pathsz, const void *data, size_t datasz)
{
	FILE *fp = NULL;
	size_t wrn;
	int r;

	fp = fy_tmpfile(path, pathsz);
	if (!fp)
		return -1;

	if (data && datasz > 0) {
		wrn = fwrite(data, 1, datasz, fp);
		if (wrn != datasz)
			goto err_out;
	}
	r = fclose(fp);
	if (r)
		goto err_out;

	return 0;

err_out:
	if (fp) {
		fclose(fp);
		if (path[0])
			remove(path);
	}
	return -1;
}

int fy_rename(const char *old_file, const char *new_file, bool no_replace)
{
	int rc;

#if defined(__linux__) && defined(RENAME_NOREPLACE)
	/* libc method exists */
	rc = renameat2(AT_FDCWD, old_file, AT_FDCWD, new_file, no_replace ? RENAME_NOREPLACE : 0);
#elif defined(__linux__) && defined(SYS_renameat2)
	/* go with a raw syscall */
	rc = syscall(SYS_renameat2, AT_FDCWD, old_file, AT_FDCWD, new_file,
			no_replace ? (1U << 0) : 0); // RENAME_NOREPLACE
#elif defined (__APPLE__) && defined (__MACH__)
	rc = renameatx_np(AT_FDCWD, old_file, AT_FDCWD, new_file, no_replace ? RENAME_EXCL : 0);
#elif defined(_WIN32)
	/* here comes the pain */
	wchar_t *oldw, *neww;
	BOOL result;
	DWORD error;

	oldw = fy_win32_u8tow(old_file);
	neww = fy_win32_u8tow(new_file);

	if (oldw && neww) {
		result = MoveFileExW(oldw, neww, no_replace ? MOVEFILE_REPLACE_EXISTING : 0);
		if (result) {
			rc = 0;
		} else {
			rc = -1;
			error = GetLastError();
			/* if it already exists on atomic is fine */
			if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
				errno = EEXIST;
			else
				errno = EINVAL;
		}
	} else {
		rc = -1;
		errno = ENOMEM;
	}

	if (oldw)
		free(oldw);
	if (neww)
		free(neww);
#else
	/* portable fallback - this one will overwrite always */
	rc = rename(old_file, new_file);
#endif
	return rc;
}

const char *fy_realpath(const char *path, char *buf, size_t bufsize)
{
#ifndef _WIN32
	char tmpbuf[PATH_MAX];
	size_t len;
	char *s;

	s = realpath(path, tmpbuf);
	if (!s)
		return NULL;
	len = strlen(s);
	if (len + 1 > bufsize) {
		errno = ENOSPC;
		return NULL;
	}
	strncpy(buf, s, bufsize);
	return buf;
#else
	wchar_t *path_w = NULL;
	wchar_t *resolved_w = NULL;
	char *resolved_utf8 = NULL;
	const char *p;
	HANDLE h = INVALID_HANDLE_VALUE;
	DWORD n, written;
	size_t len;

	path_w = fy_win32_u8tow(path);
	if (!path_w) {
		errno = ENOMEM;
		goto err_out;
	}

	h = CreateFileW(path_w, 0,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	free(path_w);
	path_w = NULL;

	if (h == INVALID_HANDLE_VALUE) {
		errno = EINVAL;
		goto err_out;
	}

	n = GetFinalPathNameByHandleW(h, NULL, 0,
			FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	if (n == 0) {
		errno = EINVAL;
		goto err_out;
	}

	resolved_w = malloc(((size_t)n + 1) * sizeof(wchar_t));
	if (!resolved_w) {
		errno = EINVAL;
		goto err_out;
	}

	written = GetFinalPathNameByHandleW(h, resolved_w,
			n + 1, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);

	CloseHandle(h);
	h = INVALID_HANDLE_VALUE;

	if (written == 0 || written > n) {
		errno = EINVAL;
		goto err_out;
	}

	resolved_w[written] = L'\0';

	resolved_utf8 = fy_win32_wtou8(resolved_w);
	if (!resolved_utf8) {
		errno = ENOMEM;
		goto err_out;
	}

	free(resolved_w);
	resolved_w = NULL;

	/* GetFinalPathNameByHandleW always prepends \\?\ — strip it */
	p = resolved_utf8;
	if (p[0] == '\\' && p[1] == '\\' && p[2] == '?' && p[3] == '\\')
		p += 4;
	len = strlen(p);
	if (len + 1 > bufsize) {
		errno = ENOSPC;
		goto err_out;
	}
	strncpy(buf, p, bufsize);

	free(resolved_utf8);
	resolved_utf8 = NULL;

	return buf;

err_out:
	if (path_w)
		free(path_w);
	if (h != INVALID_HANDLE_VALUE)
		CloseHandle(h);
	if (resolved_w)
		free(resolved_w);
	if (resolved_utf8)
		free(resolved_utf8);
	return NULL;
#endif
}

const char *fy_tmpdir(char *buf, size_t size)
{
#ifndef _WIN32
	const char *tmp;

	tmp = getenv("TMPDIR");
	if (!tmp || !*tmp)
		tmp = "/tmp";

	/* the directory must have a realpath to always be absolute */
	return fy_realpath(tmp, buf, size);
#else
	DWORD n;
	wchar_t *w = NULL;
	char *s = NULL;
	const char *ret;

	n = GetTempPathW(0, NULL);
	if (!n)
		goto err_out;

	w = malloc((size_t)n * sizeof(*w));
	if (!w)
		goto err_out;

	n = GetTempPathW(n, w);
	if (!n)
		goto err_out;

	s = fy_win32_wtou8(w);
	free(w);
	w = NULL;
	if (!s)
		goto err_out;

	ret = fy_realpath(s, buf, size);
	free(s);
	s = NULL;

	return ret;

err_out:
	if (w)
		free(w);
	if (s)
		free(s);
	return NULL;
#endif
}

const char *fy_cachedir(char *buf, size_t size)
{
#ifndef _WIN32
	static const char *cache_home_env = "XDG_CACHE_HOME";
	static const char *home_env = "HOME";
#else
	static const char *cache_home_env = "LOCALAPPDATA";
	static const char *home_env = "USERPROFILE";
#endif
	char tmpbuf[PATH_MAX];
	const char *base, *home;
	const char *tmp;
	int len;

	/* try with base first */
	base = getenv(cache_home_env);
	if (base) {
		/* the directory must have a realpath */
		tmp = fy_realpath(base, buf, size);
		if (tmp)
			return buf;
		/* fall-through */
	}

	/* home */
	home = getenv(home_env);
	if (home) {
		tmp = fy_realpath(home, tmpbuf, sizeof(tmpbuf));
		if (tmp) {
			len = snprintf(buf, (int)size, "%s/.cache", tmp);
			if ((size_t)len + 1 < size)
				return buf;
		}
	}

	return NULL;
}

#ifdef _WIN32
int fy_win32_mktemp(char *tmpl, bool is_dir)
{
	static const char *alnum =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	size_t len;
	char *x;
	unsigned tries;
        DWORD e;
        wchar_t *w = NULL;
        HANDLE h = INVALID_HANDLE_VALUE;
        int i, fd = -1;
	BOOL ok;

	if (!tmpl) {
		errno = EINVAL;
		goto err_out;
	}

	len = strlen(tmpl);
	if (len < 6 || strcmp(tmpl + len - 6, "XXXXXX") != 0) {
		errno = EINVAL;
		goto err_out;
	}
	x = tmpl + len - 6;

	for (tries = 0; tries < 100; tries++) {

		for (i = 0; i < 6; i++)
			x[i] = alnum[rand() % sizeof(alnum - 1)];

		w = fy_win32_u8tow(tmpl);
		if (!w)
			goto err_out;

		if (!is_dir) {
			h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE,
					0, NULL, CREATE_NEW,	/* atomic: fail if already exists */
					FILE_ATTRIBUTE_TEMPORARY, NULL);

			e = GetLastError();
			free(w);
			w = NULL;

			if (h == INVALID_HANDLE_VALUE) {
				/* try again if it exists */
				if (e == ERROR_FILE_EXISTS || e == ERROR_ALREADY_EXISTS)
					continue;
				errno = EACCES;
				goto err_out;
			}

			fd = _open_osfhandle((intptr_t)h, _O_RDWR | _O_BINARY);
			if (fd < 0) {
				errno = EINVAL;
				goto err_out;
			}

			return fd;
		} else {
			ok = CreateDirectoryW(w, NULL);

			e = GetLastError();

			free(w);
			w = NULL;

			if (ok)	/* ok, directory created */
				return 0;

			if (e == ERROR_ALREADY_EXISTS)
				continue;

			errno = EACCES;
			goto err_out;
		}
	}

	/* after all the trouble nothing worked */
	errno = EEXIST;

err_out:
	if (w)
		free(w);
	if (h != INVALID_HANDLE_VALUE)
		CloseHandle(h);
	errno = EEXIST;
	return -1;
}
#endif

int fy_mkstemp(char *tmpl)
{
#ifndef _WIN32
	return mkstemp(tmpl);
#else
	return fy_win32_mktemp(tmpl, false);
#endif
}

char *fy_mkdtemp(char *tmpl)
{
#ifndef _WIN32
	return mkdtemp(tmpl);
#else
	int rc;

	rc = fy_win32_mktemp(tmpl, true);
	return rc ? NULL : tmpl;
#endif
}

/* ------------------------------------------------------------------ */
/* durable, fixed-base arena: platform-dependent helpers              */
/* ------------------------------------------------------------------ */

#if defined(__linux__)
#include <sys/vfs.h>
/* network filesystem magics we refuse (cross-process atomics not guaranteed) */
#ifndef NFS_SUPER_MAGIC
#define NFS_SUPER_MAGIC		0x6969
#endif
#ifndef SMB_SUPER_MAGIC
#define SMB_SUPER_MAGIC		0x517b
#endif
#ifndef SMB2_MAGIC_NUMBER
#define SMB2_MAGIC_NUMBER	0xfe534d42
#endif
#ifndef CIFS_MAGIC_NUMBER
#define CIFS_MAGIC_NUMBER	0xff534d42
#endif
#ifndef FUSE_SUPER_MAGIC
#define FUSE_SUPER_MAGIC	0x65735546
#endif
#elif defined(__APPLE__)
#include <sys/param.h>
#include <sys/mount.h>
#endif

/*
 * Well-separated high canonical-VA bases for large fixed-base mmap regions.
 * The addresses live in a free part of the 64-bit user address space on
 * Linux/macOS; on a 32-bit target there is no room for a multi-GiB fixed
 * reservation, so 0 ("unsupported") is returned. The 4 TiB stride keeps
 * successive regions clear of each other for any practical region size.
 */
uint64_t fy_default_fixed_vm_base(unsigned int region)
{
#if UINTPTR_MAX > 0xffffffffULL
	return 0x500000000000ULL + (uint64_t)region * 0x040000000000ULL;
#else
	(void)region;
	return 0;
#endif
}

uint64_t fy_default_fixed_vm_size(unsigned int region)
{
#if UINTPTR_MAX > 0xffffffffULL
	/* region 0 (content) is large; secondary regions (index, ...) smaller */
	return region == 0 ? (64ULL << 30) : (16ULL << 30);
#else
	(void)region;
	return 0;
#endif
}

bool fy_fd_fs_is_local(int fd)
{
#if defined(__linux__) || defined(__APPLE__)
	struct statfs sfb;

	if (fstatfs(fd, &sfb) != 0)
		return true;	/* can't tell; allow */
#endif

#if defined(__linux__)
	switch ((unsigned long)sfb.f_type) {
	case NFS_SUPER_MAGIC:
	case SMB_SUPER_MAGIC:
	case (unsigned long)SMB2_MAGIC_NUMBER:
	case (unsigned long)CIFS_MAGIC_NUMBER:
	case FUSE_SUPER_MAGIC:
		return false;
	default:
		return true;
	}
#elif defined(__APPLE__)
	/* Darwin reports a filesystem type name rather than a magic number */
	if (!strcmp(sfb.f_fstypename, "nfs") ||
	    !strcmp(sfb.f_fstypename, "smbfs") ||
	    !strcmp(sfb.f_fstypename, "webdav") ||
	    strstr(sfb.f_fstypename, "fuse") != NULL)
		return false;
	return true;
#else
	(void)fd;
	return true;	/* no query available; allow */
#endif
}
