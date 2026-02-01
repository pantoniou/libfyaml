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

/*
 * Define FY_CPP_SHORT_NAMES to use shortened macro names in the implementation.
 * This significantly reduces expansion buffer usage during recursive macro expansion,
 * which can help avoid internal compiler errors and speed up compilation.
 *
 * The public API (FY_CPP_*) remains the same regardless of this setting.
 */

#define FY_CPP_SHORT_NAMES

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
		const __typeof__(((type *)0)->member) *__mptr = (ptr); \
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

/* make things fail easy on non GCC/clang */
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

/* true, if types are the same, false otherwise (depends on builtin_types_compatible_p) */
#if __has_builtin(__builtin_types_compatible_p)
#define FY_SAME_TYPE(_a, _b) __builtin_types_compatible_p(__typeof__(_a), __typeof__(_b))
#else
#define FY_SAME_TYPE(_a, _b) true
#endif

/* compile error if types are not the same */
#define FY_CHECK_SAME_TYPE(_a, _b) \
    FY_COMPILE_ERROR_ON_ZERO(!FY_SAME_TYPE(_a, _b))

/* type safe add overflow */
#if __has_builtin(__builtin_add_overflow)
#define FY_ADD_OVERFLOW __builtin_add_overflow
#else
#define FY_ADD_OVERFLOW(_a, _b, _resp) \
({ \
	__typeof__(_a) __a = (_a), __res; \
	__typeof__(_b) __b = (_b); \
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
#if __has_builtin(__builtin_sub_overflow)
#define FY_SUB_OVERFLOW __builtin_sub_overflow
#else
#define FY_SUB_OVERFLOW(_a, _b, _resp) \
({ \
	__typeof__(_a) __a = (_a), __res; \
	__typeof__(_b) __b = (_b); \
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
#if __has_builtin(__builtin_mul_overflow)
#define FY_MUL_OVERFLOW __builtin_mul_overflow
#else
#define FY_MUL_OVERFLOW(_a, _b, _resp) \
({ \
	__typeof__(_a) __a = (_a), __res; \
	__typeof__(_b) __b = (_b); \
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

static inline void fy_strip_trailing_nl(char *str)
{
	char *s;

	if (str) {
		s = str + strlen(str);
		while (s > str && s[-1] == '\n')
			*--s = '\0';
	}
}

/* alloca formatted print methods */
#define fy_vsprintfa(_fmt, _ap) \
	({ \
		const char *__fmt = (_fmt); \
		va_list _ap_orig; \
		int _size; \
		char *_buf; \
		\
		va_copy(_ap_orig, (_ap)); \
		_size = vsnprintf(NULL, 0, __fmt, _ap_orig); \
		va_end(_ap_orig); \
		_buf = alloca(_size + 1); \
		(void)vsnprintf(_buf, _size + 1, __fmt, (_ap)); \
		_buf; \
	})

#define fy_sprintfa(_fmt, ...) \
	({ \
		const char *__fmt = (_fmt); \
		int _size; \
		char *_buf; \
		\
		_size = snprintf(NULL, 0, __fmt, ## __VA_ARGS__); \
		_buf = alloca(_size + 1); \
		(void)snprintf(_buf, _size + 1, __fmt, __VA_ARGS__); \
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

#ifdef FY_CPP_SHORT_NAMES

/* Short-name implementation - reduces expansion buffer usage by ~60% */

#define _E1(...)	__VA_ARGS__
#define _E2(...)	_E1(_E1(__VA_ARGS__))
#define _E4(...)	_E2(_E2(__VA_ARGS__))
#define _E8(...)	_E4(_E4(__VA_ARGS__))
#define _E16(...)	_E8(_E8(__VA_ARGS__))
#define _E32(...)	_E16(_E16(__VA_ARGS__))
#define _E(...)		_E32(_E32(__VA_ARGS__))

#define _FMT()
#define _FP1(m) m _FMT()
#define _FP2(a, m) m _FMT()

#define _F1(_1, ...)		_1
#define _F1F(...)			_F1(__VA_OPT__(__VA_ARGS__ ,) 0)

#define _F2(_1, _2, ...)	_2
#define _F2F(...)			_F2(__VA_OPT__(__VA_ARGS__ ,) 0, 0)

#define _F3(_1, _2, _3, ...)		_3
#define _F3F(...)			_F3(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0)

#define _F4(_1, _2, _3, _4, ...)	_4
#define _F4F(...)			_F4(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0, 0)

#define _F5(_1, _2, _3, _4, _5, ...)	_5
#define _F5F(...) \
	_F5(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0, 0, 0)

#define _F6(_1, _2, _3, _4, _5, _6, ...)	_6
#define _F6F(...) \
	_F6(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0, 0, 0, 0)

#define _FR(x, ...) __VA_ARGS__
#define _FRF(...)     __VA_OPT__(_FR(__VA_ARGS__))

#define _FM(m, ...) \
    __VA_OPT__(_E(_FM1(m, __VA_ARGS__)))
#define _FM1(m, x, ...) m(x) \
    __VA_OPT__(_FP1(_FMI)()(m, __VA_ARGS__))
#define _FMI() _FM1

#define _FCB(x) +1
#define _FVC(...)  (__VA_OPT__((_FM(_FCB, __VA_ARGS__) + 0)) + 0)

#define _FI1(a) a
#define _FIL(a) , _FI1(a)
#define _FILS(...) _FM(_FIL, __VA_ARGS__)

#define _FVIS(...) \
    _FI1(_F1F(__VA_ARGS__)) \
    _FILS(_FRF(__VA_ARGS__))

#define _FVISF(_t, ...) \
	((_t [_FVC(__VA_ARGS__)]) { _FVIS(__VA_ARGS__) })

#define _FM2(a, m, ...) \
    __VA_OPT__(_E(_FM12(a, m, __VA_ARGS__)))

#define _FM12(a, m, x, ...) m(a, x) \
    __VA_OPT__(_FP2(a, _FMI2)()(a, m, __VA_ARGS__))
#define _FMI2() _FM12

/* Public API - maps to short names */
#define FY_CPP_EVAL1		_E1
#define FY_CPP_EVAL2		_E2
#define FY_CPP_EVAL4		_E4
#define FY_CPP_EVAL8		_E8
#if !defined(__clang__)
#define FY_CPP_EVAL16		_E16
#endif
#define FY_CPP_EVAL		_E
#define FY_CPP_EMPTY		_FMT
#define FY_CPP_POSTPONE1	_FP1
#define FY_CPP_POSTPONE2	_FP2

#define FY_CPP_FIRST		_F1F
#define FY_CPP_SECOND		_F2F
#define FY_CPP_THIRD		_F3F
#define FY_CPP_FOURTH		_F4F
#define FY_CPP_FIFTH		_F5F
#define FY_CPP_SIXTH		_F6F
#define FY_CPP_REST		_FRF

#define FY_CPP_MAP		_FM
#define _FY_CPP_MAP_ONE		_FM1
#define _FY_CPP_MAP_INDIRECT	_FMI
#define _FY_CPP_COUNT_BODY	_FCB
#define FY_CPP_VA_COUNT		_FVC
#define _FY_CPP_ITEM_ONE	_FI1
#define _FY_CPP_ITEM_LATER_ARG	_FIL
#define _FY_CPP_ITEM_LIST	_FILS
#define _FY_CPP_VA_ITEMS	_FVIS
#define FY_CPP_VA_ITEMS		_FVISF
#define FY_CPP_MAP2		_FM2
#define _FY_CPP_MAP_ONE2	_FM12
#define _FY_CPP_MAP_INDIRECT2	_FMI2

#else /* !FY_CPP_SHORT_NAMES */

/* Original long-name implementation */

#define FY_CPP_EVAL1(...)	__VA_ARGS__
#define FY_CPP_EVAL2(...)	FY_CPP_EVAL1(FY_CPP_EVAL1(__VA_ARGS__))
#define FY_CPP_EVAL4(...)	FY_CPP_EVAL2(FY_CPP_EVAL2(__VA_ARGS__))
#define FY_CPP_EVAL8(...)	FY_CPP_EVAL4(FY_CPP_EVAL4(__VA_ARGS__))
#if !defined(__clang__)
// gcc is better, goes to 16
#define FY_CPP_EVAL16(...)	FY_CPP_EVAL8(FY_CPP_EVAL8(__VA_ARGS__))
#define FY_CPP_EVAL(...)	FY_CPP_EVAL16(FY_CPP_EVAL16(__VA_ARGS__))
#else
// clang craps out at 8
#define FY_CPP_EVAL(...)	FY_CPP_EVAL8(FY_CPP_EVAL8(__VA_ARGS__))
#endif

#define FY_CPP_EMPTY()
#define FY_CPP_POSTPONE1(macro) macro FY_CPP_EMPTY()

#define FY_CPP_MAP(macro, ...) \
    __VA_OPT__(FY_CPP_EVAL(_FY_CPP_MAP_ONE(macro, __VA_ARGS__)))
#define _FY_CPP_MAP_ONE(macro, x, ...) macro(x) \
    __VA_OPT__(FY_CPP_POSTPONE1(_FY_CPP_MAP_INDIRECT)()(macro, __VA_ARGS__))
#define _FY_CPP_MAP_INDIRECT() _FY_CPP_MAP_ONE

#define FY_CPP_POSTPONE2(a, macro) macro FY_CPP_EMPTY()

#define _FY_CPP_FIRST(_1, ...)			_1
#define FY_CPP_FIRST(...)			_FY_CPP_FIRST(__VA_OPT__(__VA_ARGS__ ,) 0)

#define _FY_CPP_SECOND(_1, _2, ...)		_2
#define FY_CPP_SECOND(...)			_FY_CPP_SECOND(__VA_OPT__(__VA_ARGS__ ,) 0, 0)

#define _FY_CPP_THIRD(_1, _2, _3, ...)		_3
#define FY_CPP_THIRD(...)			_FY_CPP_THIRD(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0)

#define _FY_CPP_FOURTH(_1, _2, _3, _4, ...)	_4
#define FY_CPP_FOURTH(...)			_FY_CPP_FOURTH(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0, 0)

#define _FY_CPP_FIFTH(_1, _2, _3, _4, _5, ...)	_5
#define FY_CPP_FIFTH(...) \
	_FY_CPP_FIFTH(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0, 0, 0)

#define _FY_CPP_SIXTH(_1, _2, _3, _4, _5, _6, ...)	_6
#define FY_CPP_SIXTH(...) \
	_FY_CPP_SIXTH(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0, 0, 0, 0)

#define _FY_CPP_REST(x, ...) __VA_ARGS__
#define FY_CPP_REST(...)     __VA_OPT__(_FY_CPP_REST(__VA_ARGS__))

#define _FY_CPP_COUNT_BODY(x) +1
#define FY_CPP_VA_COUNT(...)  (__VA_OPT__((FY_CPP_MAP(_FY_CPP_COUNT_BODY, __VA_ARGS__) + 0)) + 0)

#define _FY_CPP_ITEM_ONE(arg) arg
#define _FY_CPP_ITEM_LATER_ARG(arg) , _FY_CPP_ITEM_ONE(arg)
#define _FY_CPP_ITEM_LIST(...) FY_CPP_MAP(_FY_CPP_ITEM_LATER_ARG, __VA_ARGS__)

#define _FY_CPP_VA_ITEMS(...)          \
    _FY_CPP_ITEM_ONE(FY_CPP_FIRST(__VA_ARGS__)) \
    _FY_CPP_ITEM_LIST(FY_CPP_REST(__VA_ARGS__))

#define FY_CPP_VA_ITEMS(_type, ...) \
	((_type [FY_CPP_VA_COUNT(__VA_ARGS__)]) { _FY_CPP_VA_ITEMS(__VA_ARGS__) })

#define FY_CPP_MAP2(a, macro, ...) \
    __VA_OPT__(FY_CPP_EVAL(_FY_CPP_MAP_ONE2(a, macro, __VA_ARGS__)))

#define _FY_CPP_MAP_ONE2(a, macro, x, ...) macro(a, x) \
    __VA_OPT__(FY_CPP_POSTPONE2(a, _FY_CPP_MAP_INDIRECT2)()(a, macro, __VA_ARGS__))
#define _FY_CPP_MAP_INDIRECT2() _FY_CPP_MAP_ONE2

#endif /* FY_CPP_SHORT_NAMES */

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

#define fy_alloca_align(_sz, _align) \
	({ \
		const size_t __sz = (_sz); \
		const size_t __align = (_align); \
		void *__p; \
		__p = __align <= sizeof(max_align_t) ? alloca(__sz) : fy_ptr_align(alloca(__sz + __align - 1), __align); \
		__p; \
	})

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

	for (i = 0; i < iovcnt; i++, dst = (void *)((char *)dst + size)) {
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

	for (i = 0; i < iovcnt; i++, src = (const void *)((const char *)src + size)) {
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

	for (i = 0; i < iovcnt; i++, s = (const void *)((const char *)s + size)) {
		size = iov[i].iov_len;
		ret = memcmp(iov[i].iov_base, s, size);
		if (ret)
			return ret;
	}
	return 0;
}

/* safe bet for 2025 */
#define FY_CACHE_LINE_SZ	64
#define FY_CACHE_ALIGNED	__attribute__((aligned(FY_CACHE_LINE_SZ)))

uint64_t fy_iovec_xxhash64(const struct iovec *iov, int iovcnt);

#define FY_CONCAT(_a, _b) FY_CONCAT_INNER(_a, _b)
#define FY_CONCAT_INNER(_a, _b) _a ## _b
#define FY_UNIQUE(_base) FY_CONCAT(_base, __COUNTER__)
#define FY_LUNIQUE(_base) FY_CONCAT(_base, __LINE__)

#if __has_builtin(__builtin_stack_save) && __has_builtin(__builtin_stack_restore)
#define FY_STACK_SAVE()		__builtin_stack_save()
#define FY_STACK_RESTORE(_x)	__builtin_stack_restore((_x))
#else
/* no builtin? just ignore */
#define FY_STACK_SAVE()		((void *)NULL)
#define FY_STACK_RESTORE(_x)	do { (void)(_x); } while(0)
#endif

/* possible have lambdas, either gcc or clang with block support */
#ifdef __clang__
#ifdef __BLOCKS__
#define FY_HAVE_LAMBDAS
#define FY_HAVE_BLOCK_LAMBDAS
#endif
#elif defined(__GNUC__)
#define FY_HAVE_LAMBDAS
#define FY_HAVE_NESTED_FUNC_LAMBDAS
#endif

/* wrapper for memstream, on all platforms */
struct fy_memstream;	// opaque

struct fy_memstream *fy_memstream_open(FILE **fpp);
char *fy_memstream_close(struct fy_memstream *fyms, size_t *sizep);

#endif
