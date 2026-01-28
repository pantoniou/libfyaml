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
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

#ifdef _WIN32
#include "fy-win32.h"
#else
#include <unistd.h>
#include <termios.h>
#endif

/* to avoid dragging in libfyaml.h */
#ifndef FY_BIT
#define FY_BIT(x) (1U << (x))
#endif

/* MSVC doesn't support __attribute__, so disable it */
#ifdef _MSC_VER
#ifndef __attribute__
#define __attribute__(x)
#endif
#endif

#if defined(__linux__)
#include <sys/sysmacros.h>
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

/* container_of - use a fallback for MSVC which doesn't support typeof */
#ifndef container_of
#if defined(_MSC_VER)
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))
#else
#define container_of(ptr, type, member) \
	({ \
		const typeof(((type *)0)->member) *__mptr = (ptr); \
		(type *)((void *)__mptr - offsetof(type,member)); \
	 })
#endif /* _MSC_VER */
#endif /* container_of */

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) ((sizeof(x)/sizeof((x)[0])))
#endif

#if defined(NDEBUG) && (defined(__GNUC__) && __GNUC__ >= 4)
#define FY_ALWAYS_INLINE __attribute__((always_inline))
#else
#define FY_ALWAYS_INLINE /* nothing */
#endif

#if defined(__GNUC__) && __GNUC__ >= 4
#define FY_UNUSED __attribute__((unused))
#else
#define FY_UNUSED /* nothing */
#endif

#if defined(NDEBUG) && defined(__GNUC__) && __GNUC__ >= 4
#define FY_DEBUG_UNUSED __attribute__((unused))
#else
#define FY_DEBUG_UNUSED /* nothing */
#endif

#if defined(__GNUC__) && __GNUC__ >= 4
#define FY_CONSTRUCTOR __attribute__((constructor))
#define FY_DESTRUCTOR __attribute__((destructor))
#define FY_HAS_CONSTRUCTOR
#define FY_HAS_DESTRUCTOR
#else
#define FY_CONSTRUCTOR /* nothing */
#define FY_DESTRUCTOR /* nothing */
#endif

#if defined(FY_DESTRUCTOR) && defined(NDEBUG)
#define FY_DESTRUCTOR_SHOW_LEFTOVERS
#endif

/* something impossible is happening, assert/abort */
#define FY_IMPOSSIBLE_ABORT() \
	do { \
		assert(0); \
		abort(); \
	} while(0)

static inline void *fy_ptr_align(void *p, size_t align)
{
	return (void *)(((uintptr_t)p + (align - 1)) & ~(align - 1));
}

static inline size_t fy_size_t_align(size_t size, size_t align)
{
	return (size + (align - 1)) & ~(align - 1);
}

/* Terminal functions - only available on Unix-like systems */
#ifndef _WIN32
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
#endif /* !_WIN32 */

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

/* if expression is zero, then the build will break */
#define FY_COMPILE_ERROR_ON_ZERO(_e) ((void)(sizeof(char[1 - 2*!!(_e)])))

/* true, if types are the same, false otherwise (depends on builtin_types_compatible_p) */
#if !defined(_MSC_VER) && defined(__has_builtin)
#if __has_builtin(__builtin_types_compatible_p)
#define FY_SAME_TYPE(_a, _b) __builtin_types_compatible_p(typeof(_a), typeof(_b))
#endif
#endif
#ifndef FY_SAME_TYPE
#define FY_SAME_TYPE(_a, _b) true
#endif

/* compile error if types are not the same */
#define FY_CHECK_SAME_TYPE(_a, _b) \
    FY_COMPILE_ERROR_ON_ZERO(!FY_SAME_TYPE(_a, _b))

/* type safe add overflow */
#if !defined(_MSC_VER) && defined(__has_builtin)
#if __has_builtin(__builtin_add_overflow)
#define FY_ADD_OVERFLOW __builtin_add_overflow
#endif
#endif
#ifdef _MSC_VER
/* MSVC: simple implementation, returns false (no overflow detection) */
#define FY_ADD_OVERFLOW(_a, _b, _resp) ((void)((*(_resp) = (_a) + (_b))), false)
#elif !defined(FY_ADD_OVERFLOW)
#define FY_ADD_OVERFLOW(_a, _b, _resp) \
({ \
	typeof (_a) __a = (_a), __res; \
	typeof (_b) __b = (_b); \
	bool __overflow; \
	\
	FY_CHECK_SAME_TYPE(__a, __b); \
	\
	__res = __a + __b; \
	/* overflow when signs of a, b same, but results different */ \
	__overflow = ((__a ^ __result) & (__b & __result)) < 0; \
	*(_resp) = __res; \
	__overflow; \
})
#endif

/* type safe sub overflow */
#if !defined(_MSC_VER) && defined(__has_builtin)
#if __has_builtin(__builtin_sub_overflow)
#define FY_SUB_OVERFLOW __builtin_sub_overflow
#endif
#endif
#ifdef _MSC_VER
/* MSVC: simple implementation, returns false (no overflow detection) */
#define FY_SUB_OVERFLOW(_a, _b, _resp) ((void)((*(_resp) = (_a) - (_b))), false)
#elif !defined(FY_SUB_OVERFLOW)
#define FY_SUB_OVERFLOW(_a, _b, _resp) \
({ \
	typeof (_a) __a = (_a), __res; \
	typeof (_b) __b = (_b); \
	bool __overflow; \
	\
	FY_CHECK_SAME_TYPE(__a, __b); \
	\
	__res = __a - __b; \
	/* overflow when signs of a, b differ, but results different from minuend */ \
	__overflow = ((__a ^ __b) & (__a & __result)) < 0; \
	*(_resp) = __res; \
	__overflow; \
})
#endif

/* type safe multiply overflow */
#if !defined(_MSC_VER) && defined(__has_builtin)
#if __has_builtin(__builtin_mul_overflow)
#define FY_MUL_OVERFLOW __builtin_mul_overflow
#endif
#endif
#ifdef _MSC_VER
/* MSVC: simple implementation, returns false (no overflow detection) */
#define FY_MUL_OVERFLOW(_a, _b, _resp) ((void)((*(_resp) = (_a) * (_b))), false)
#elif !defined(FY_MUL_OVERFLOW)
#define FY_MUL_OVERFLOW(_a, _b, _resp) \
({ \
	typeof (_a) __a = (_a), __res; \
	typeof (_b) __b = (_b); \
	bool __overflow; \
	\
	FY_CHECK_SAME_TYPE(__a, __b); \
	\
	if (!__a || !__b) { \
	    __overflow = false; \
	    __res = 0; \
	} else { \
	    __res = __a * __b; \
	    /* overflow when division of the result differs */ \
	    __overflow = (__res / __a) != __b; \
	} \
	*(_resp) = __res; \
	__overflow; \
})
#endif

/* alloca formatted print methods */
#ifdef _MSC_VER
/*
 * MSVC doesn't support GCC statement expressions, so we provide
 * function-based alternatives. These use a static thread-local buffer
 * which is less flexible but works for typical use cases.
 */
#define FY_ALLOCA_SPRINTF_BUFSZ 4096

static __declspec(thread) char fy_alloca_sprintf_buf[FY_ALLOCA_SPRINTF_BUFSZ];

static inline char *fy_alloca_vsprintf_impl(const char *fmt, va_list ap)
{
	va_list ap_copy;
	int len;
	char *s;

	va_copy(ap_copy, ap);
	len = vsnprintf(fy_alloca_sprintf_buf, FY_ALLOCA_SPRINTF_BUFSZ, fmt, ap_copy);
	va_end(ap_copy);
	if (len < 0)
		return NULL;
	/* strip trailing newlines */
	s = fy_alloca_sprintf_buf + strlen(fy_alloca_sprintf_buf);
	while (s > fy_alloca_sprintf_buf && s[-1] == '\n')
		*--s = '\0';
	return fy_alloca_sprintf_buf;
}

/* For MSVC, we can't use variadic macros that return values easily,
 * so alloca_sprintf needs a different approach */
#define alloca_vsprintf(_fmt, _ap) fy_alloca_vsprintf_impl((_fmt), (_ap))
#define alloca_sprintf(_fmt, ...) (snprintf(fy_alloca_sprintf_buf, FY_ALLOCA_SPRINTF_BUFSZ, (_fmt), __VA_ARGS__) < 0 ? NULL : fy_alloca_sprintf_buf)

#else
/* GCC/Clang version with statement expressions */
#define alloca_vsprintf(_fmt, _ap) \
	({ \
		const char *__fmt = (_fmt); \
		va_list _ap_orig; \
		int _size; \
		int _sizew __FY_DEBUG_UNUSED__; \
		char *_buf = NULL, *_s; \
		\
		va_copy(_ap_orig, (_ap)); \
		_size = vsnprintf(NULL, 0, __fmt, _ap_orig); \
		va_end(_ap_orig); \
		if (_size != -1) { \
			_buf = alloca(_size + 1); \
			_sizew = vsnprintf(_buf, _size + 1, __fmt, _ap); \
			assert(_size == _sizew); \
			_s = _buf + strlen(_buf); \
			while (_s > _buf && _s[-1] == '\n') \
				*--_s = '\0'; \
		} \
		_buf; \
	})

#define alloca_sprintf(_fmt, ...) \
	({ \
		const char *__fmt = (_fmt); \
		int _size; \
		int _sizew __FY_DEBUG_UNUSED__; \
		char *_buf = NULL, *_s; \
		\
		_size = snprintf(NULL, 0, __fmt, ## __VA_ARGS__); \
		if (_size != -1) { \
			_buf = alloca(_size + 1); \
			_sizew = snprintf(_buf, _size + 1, __fmt, __VA_ARGS__); \
			assert(_size == _sizew); \
			_s = _buf + strlen(_buf); \
			while (_s > _buf && _s[-1] == '\n') \
				*--_s = '\0'; \
		} \
		_buf; \
	})
#endif

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

#endif
