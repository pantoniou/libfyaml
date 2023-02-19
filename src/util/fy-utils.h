/*
 * fy-utils.h - internal utilities header file
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_UTILS_H
#define FY_UTILS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>
#include <termios.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

#if defined(__linux__)
#include <sys/sysmacros.h>
#endif

#include <libfyaml/fy-internal.h>

#if !defined(NDEBUG) && defined(HAVE_DEVMODE) && HAVE_DEVMODE
#define FY_DEVMODE
#else
#undef FY_DEVMODE
#endif

#ifdef FY_DEVMODE
#define __FY_DEBUG_UNUSED__	/* nothing */
#else
#if defined(__GNUC__) && __GNUC__ >= 4
#define __FY_DEBUG_UNUSED__	__attribute__((__unused__))
#else
#define __FY_DEBUG_UNUSED__	/* nothing */
#endif
#endif

#if defined(__APPLE__) && (_POSIX_C_SOURCE < 200809L)
FILE *open_memstream(char **ptr, size_t *sizeloc);
#endif

int fy_tag_handle_length(const char *data, size_t len);
bool fy_tag_uri_is_valid(const char *data, size_t len);
int fy_tag_uri_length(const char *data, size_t len);

struct fy_tag_scan_info {
	int total_length;
	int handle_length;
	int uri_length;
	int prefix_length;
	int suffix_length;
};

int fy_tag_scan(const char *data, size_t len, struct fy_tag_scan_info *info);

int fy_term_set_raw(int fd, struct termios *oldt);
int fy_term_restore(int fd, const struct termios *oldt);
ssize_t fy_term_write(int fd, const void *data, size_t count);
int fy_term_safe_write(int fd, const void *data, size_t count);
ssize_t fy_term_read(int fd, void *data, size_t count, int timeout_us);
ssize_t fy_term_read_escape(int fd, void *buf, size_t count);

/* the raw methods require the terminal to be in raw mode */
int fy_term_query_size_raw(int fd, int *rows, int *cols);

/* the non raw methods will set the terminal to raw and then restore */
int fy_term_query_size(int fd, int *rows, int *cols);

struct fy_comment_iter {
	const char *start;
	size_t size;
	const char *end;
	const char *next;
	int line;
};

int fy_comment_iter_begin(const char *comment, size_t size, struct fy_comment_iter *iter);
const char *fy_comment_iter_next_line(struct fy_comment_iter *iter, size_t *lenp);
void fy_comment_iter_end(struct fy_comment_iter *iter);

char *fy_get_cooked_comment(const char *raw_comment, size_t size);

struct fy_keyword_iter {
	const char *keyword;
	size_t keyword_len;
	const char *start;
	size_t size;
	const char *end;
	const char *next;
	int pc;
};

int fy_keyword_iter_begin(const char *text, size_t size, const char *keyword, struct fy_keyword_iter *iter);
const char *fy_keyword_iter_next(struct fy_keyword_iter *iter);
void fy_keyword_iter_advance(struct fy_keyword_iter *iter, size_t advance);
void fy_keyword_iter_end(struct fy_keyword_iter *iter);

#if !defined(S_ISREG) && defined(S_IFMT) && defined(S_IFREG)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#if !defined(S_ISDIR) && defined(S_IFMT) && defined(S_IFDIR)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

static inline void fy_strip_trailing_nl(char *str)
{
	char *s;

	if (str) {
		s = str + strlen(str);
		while (s > str && s[-1] == '\n')
			*--s = '\0';
	}
}

static inline size_t
fy_iovec_size(const struct iovec *iov, int iovcnt)
{
	size_t size;
	int i;

	size = 0;
	for (i = 0; i < iovcnt; i++) {
		if (FY_ADD_OVERFLOW(size, iov[i].iov_len, &size))
			return SIZE_MAX;
	}
	return size;
}

static inline void *
fy_iovec_copy_from(const struct iovec *iov, int iovcnt, void *dst)
{
	size_t size;
	int i;

	for (i = 0; i < iovcnt; i++, dst += size) {
		size = iov[i].iov_len;
		memcpy(dst, iov[i].iov_base, size);
	}
	return dst;
}

static inline const void *
fy_iovec_copy_to(const struct iovec *iov, int iovcnt, const void *src)
{
	size_t size;
	int i;

	for (i = 0; i < iovcnt; i++, src += size) {
		size = iov[i].iov_len;
		memcpy(iov[i].iov_base, src, size);
	}
	return src;
}

static inline int
fy_iovec_cmp(const struct iovec *iov, int iovcnt, const void *data)
{
	const void *s = data;
	size_t size;
	int i, ret;

	for (i = 0; i < iovcnt; i++) {
		size = iov[i].iov_len;
		ret = memcmp(iov[i].iov_base, s, size);
		if (ret)
			return ret;
		s += size;
	}
	return 0;
}

uint64_t fy_iovec_xxhash64(const struct iovec *iov, int iovcnt);

/* detect whether we're running under asan */
static inline bool
fy_is_asan_enabled(void)
{
#if defined(__clang__) || defined(__GNUC__)
#ifdef __SANITIZE_ADDRESS__
	return true;
#else
	extern void __asan_init(void) __attribute__((weak));
	return __asan_init != NULL;
#endif
#else
	return false;
#endif
}

#endif
