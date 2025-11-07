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

/* to avoid dragging in libfyaml.h */
#ifndef FY_BIT
#define FY_BIT(x) (1U << (x))
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

#ifndef container_of
#define container_of(ptr, type, member) \
	({ \
		const typeof(((type *)0)->member) *__mptr = (ptr); \
		(type *)((void *)__mptr - offsetof(type,member)); \
	 })
#endif

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

/* if expression is zero, then the build will break */
#define FY_COMPILE_ERROR_ON_ZERO(_e) ((void)(sizeof(char[1 - 2*!!(_e)])))

/* true, if types are the same, false otherwise (depends on builtin_types_compatible_p) */
#if defined(__has_builtin) && __has_builtin(__builtin_types_compatible_p)
#define FY_SAME_TYPE(_a, _b) __builtin_types_compatible_p(typeof(_a), typeof(_b))
#else
#define FY_SAME_TYPE(_a, _b) true
#endif

/* compile error if types are not the same */
#define FY_CHECK_SAME_TYPE(_a, _b) \
    FY_COMPILE_ERROR_ON_ZERO(!FY_SAME_TYPE(_a, _b))

/* type safe add overflow */
#if defined(__has_builtin) && __has_builtin(__builtin_add_overflow)
#define FY_ADD_OVERFLOW __builtin_add_overflow
#else
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
#if defined(__has_builtin) && __has_builtin(__builtin_sub_overflow)
#define FY_SUB_OVERFLOW __builtin_sub_overflow
#else
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
#if defined(__has_builtin) && __has_builtin(__builtin_mul_overflow)
#define FY_MUL_OVERFLOW __builtin_mul_overflow
#else
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

// C preprocessor magic follows (pilfered and adapted from h4x0r.org)

/* applies an expansion over a varargs list */

#define FY_CPP_EVAL1(...)    __VA_ARGS__
#define FY_CPP_EVAL2(...)    FY_CPP_EVAL1(FY_CPP_EVAL1(__VA_ARGS__))
#define FY_CPP_EVAL4(...)    FY_CPP_EVAL2(FY_CPP_EVAL2(__VA_ARGS__))
#define FY_CPP_EVAL8(...)    FY_CPP_EVAL4(FY_CPP_EVAL4(__VA_ARGS__))
#define FY_CPP_EVAL16(...)   FY_CPP_EVAL8(FY_CPP_EVAL8(__VA_ARGS__))
#define FY_CPP_EVAL32(...)   FY_CPP_EVAL16(FY_CPP_EVAL16(__VA_ARGS__))
#define FY_CPP_EVAL64(...)   FY_CPP_EVAL32(FY_CPP_EVAL32(__VA_ARGS__))
#define FY_CPP_EVAL128(...)  FY_CPP_EVAL64(FY_CPP_EVAL64(__VA_ARGS__))
#define FY_CPP_EVAL(...)     FY_CPP_EVAL128(FY_CPP_EVAL128(__VA_ARGS__))

#define FY_CPP_EMPTY()
#define FY_CPP_POSTPONE1(macro) macro FY_CPP_EMPTY()

#define FY_CPP_MAP(macro, ...) \
    __VA_OPT__(FY_CPP_EVAL(_FY_CPP_MAP_ONE(macro, __VA_ARGS__)))
#define _FY_CPP_MAP_ONE(macro, x, ...) macro(x) \
    __VA_OPT__(FY_CPP_POSTPONE1(_FY_CPP_MAP_INDIRECT)()(macro, __VA_ARGS__))
#define _FY_CPP_MAP_INDIRECT() _FY_CPP_MAP_ONE

#define _FY_CPP_FIRST(x, ...) x
#define FY_CPP_FIRST(...)     __VA_OPT__(_FY_CPP_FIRST(__VA_ARGS__))

#define _FY_CPP_REST(x, ...) __VA_ARGS__
#define FY_CPP_REST(...)     __VA_OPT__(_FY_CPP_REST(__VA_ARGS__))

#define _FY_CPP_COUNT_BODY(x) +1
#define FY_CPP_VA_COUNT(...)  (FY_CPP_MAP(_FY_CPP_COUNT_BODY, __VA_ARGS__) + 0)

#define _FY_CPP_ITEM_ONE(arg) arg
#define _FY_CPP_ITEM_LATER_ARG(arg) , _FY_CPP_ITEM_ONE(arg)
#define _FY_CPP_ITEM_LIST(...) FY_CPP_MAP(_FY_CPP_ITEM_LATER_ARG, __VA_ARGS__)

#define _FY_CPP_VA_ITEMS(...)          \
    _FY_CPP_ITEM_ONE(FY_CPP_FIRST(__VA_ARGS__)) \
    _FY_CPP_ITEM_LIST(FY_CPP_REST(__VA_ARGS__))

#define FY_CPP_VA_ITEMS(_type, ...) \
	((_type [FY_CPP_VA_COUNT(__VA_ARGS__)]) { _FY_CPP_VA_ITEMS(__VA_ARGS__) })

/*
 * example usage:
 *
 * int do_sum(int count, int *items)
 * {
 * 	int i;
 * 	int sum;
 *
 * 	sum = 0;
 * 	for (i = 0; i < count; i++)
 * 		sum += items[i];
 *
 * 	return sum;
 * }
 *
 * #define do_sum_macro(...) \
 * 	do_sum( \
 * 		FY_CPP_VA_COUNT(__VA_ARGS__), \
 * 		FY_CPP_VA_ITEMS(int, __VA_ARGS__))
 *
 *   sum = do_sum_macro(1, 2, 5, 100);
 *
 * will expand to:
 *
 *   sum = do_sum(4, ((int [4]){ 1, 2, 5, 100 }));
 *
 * printf("sum=%d\n", sum);
 */

#endif
