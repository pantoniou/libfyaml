/*
 * fy-tool-utils.h - internal utilities header file
 *
 * Copyright (c) 2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_TOOL_UTILS_H
#define FY_TOOL_UTILS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <termios.h>
#include <stdint.h>
#include <string.h>

#if defined(__linux__)
#include <sys/sysmacros.h>
#endif

#ifndef BIT
#define BIT(x) (1U << (x))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) ((sizeof(x)/sizeof((x)[0])))
#endif

#if defined(NDEBUG) && (defined(__GNUC__) && __GNUC__ >= 4)
#define ALWAYS_INLINE __attribute__((always_inline))
#else
#define ALWAYS_INLINE /* nothing */
#endif

#if defined(__GNUC__) && __GNUC__ >= 4
#define UNUSED __attribute__((unused))
#else
#define UNUSED /* nothing */
#endif

#if defined(NDEBUG) && defined(__GNUC__) && __GNUC__ >= 4
#define DEBUG_UNUSED __attribute__((unused))
#else
#define DEBUG_UNUSED /* nothing */
#endif

/* if expression is zero, then the build will break */
#define COMPILE_ERROR_ON_ZERO(_e) ((void)(sizeof(char[1 - 2*!!(_e)])))

/* true, if types are the same, false otherwise (depends on builtin_types_compatible_p) */
#if defined(__has_builtin) && __has_builtin(__builtin_types_compatible_p)
#define SAME_TYPE(_a, _b) __builtin_types_compatible_p(typeof(_a), typeof(_b))
#else
#define SAME_TYPE(_a, _b) true
#endif

/* compile error if types are not the same */
#define CHECK_SAME_TYPE(_a, _b) \
    COMPILE_ERROR_ON_ZERO(!SAME_TYPE(_a, _b))

/* type safe add overflow */
#if defined(__has_builtin) && __has_builtin(__builtin_add_overflow)
#define ADD_OVERFLOW __builtin_add_overflow
#else
#define ADD_OVERFLOW(_a, _b, _resp) \
({ \
	typeof (_a) __a = (_a), __res; \
	typeof (_b) __b = (_b); \
	bool __overflow; \
	\
	CHECK_SAME_TYPE(__a, __b); \
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
#define SUB_OVERFLOW __builtin_sub_overflow
#else
#define SUB_OVERFLOW(_a, _b, _resp) \
({ \
	typeof (_a) __a = (_a), __res; \
	typeof (_b) __b = (_b); \
	bool __overflow; \
	\
	CHECK_SAME_TYPE(__a, __b); \
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
#define MUL_OVERFLOW __builtin_mul_overflow
#else
#define MUL_OVERFLOW(_a, _b, _resp) \
({ \
	typeof (_a) __a = (_a), __res; \
	typeof (_b) __b = (_b); \
	bool __overflow; \
	\
	CHECK_SAME_TYPE(__a, __b); \
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

/* ANSI colors and escapes */
#define A_RESET			"\x1b[0m"
#define A_BLACK			"\x1b[30m"
#define A_RED			"\x1b[31m"
#define A_GREEN			"\x1b[32m"
#define A_YELLOW		"\x1b[33m"
#define A_BLUE			"\x1b[34m"
#define A_MAGENTA		"\x1b[35m"
#define A_CYAN			"\x1b[36m"
#define A_LIGHT_GRAY		"\x1b[37m"	/* dark white is gray */
#define A_GRAY			"\x1b[1;30m"
#define A_BRIGHT_RED		"\x1b[1;31m"
#define A_BRIGHT_GREEN		"\x1b[1;32m"
#define A_BRIGHT_YELLOW		"\x1b[1;33m"
#define A_BRIGHT_BLUE		"\x1b[1;34m"
#define A_BRIGHT_MAGENTA	"\x1b[1;35m"
#define A_BRIGHT_CYAN		"\x1b[1;36m"
#define A_WHITE			"\x1b[1;37m"

static inline bool
memiszero(const void *ptr, size_t size)
{
	const uint8_t *p;
	size_t i;

	for (i = 0, p = ptr; i < size; i++) {
		if (p[i])
			return false;
	}
	return true;
}

/* in fy-tool-dump.c */
void print_escaped(const char *str, size_t length);

enum dump_testsuite_event_flags {
	DTEF_COLORIZE = FY_BIT(0),
	DTEF_DISABLE_FLOW_MARKERS = FY_BIT(1),
	DTEF_DISABLE_DOC_MARKERS = FY_BIT(2),
	DTEF_DISABLE_SCALAR_STYLES = FY_BIT(3),
	DTEF_TSV_FORMAT = FY_BIT(4),
};

void dump_token_comments(struct fy_token *fyt, bool colorize, const char *banner);
void dump_testsuite_event(struct fy_event *fye, enum dump_testsuite_event_flags dump_flags);
void dump_parse_event(struct fy_parser *fyp, struct fy_event *fye, bool colorize);
void dump_scan_token(struct fy_parser *fyp, struct fy_token *fyt, bool colorize);

/* */

static inline int parse_match_value(const char *text0, const char **check_vp)
{
	const char *check;
	int i;

	for (i = 0; (check = *check_vp++) != NULL; i++) {
		if (!strcmp(check, text0))
			return i;
	}
	return -1;
}

union integer_scalar {
	intmax_t sval;
	uintmax_t uval;
};

union float_scalar {
	float f;
	double d;
	long double ld;
};

uintmax_t load_le(const void *ptr, size_t width, bool is_signed);
void store_le(void *ptr, size_t width, uintmax_t v);
uintmax_t load_bitfield_le(const void *ptr, size_t bit_offset, size_t bit_width, bool is_signed);
void store_bitfield_le(void *ptr, size_t bit_offset, size_t bit_width, uintmax_t v);

static inline intmax_t signed_integer_max_from_bit_width(int bit_width)
{
	assert((size_t)bit_width <= sizeof(intmax_t) * 8);
	return INTMAX_MAX >> (sizeof(intmax_t) * 8 - bit_width);
}

static inline intmax_t signed_integer_min_from_bit_width(int bit_width)
{
	assert((size_t)bit_width <= sizeof(intmax_t) * 8);
	return INTMAX_MIN >> (sizeof(intmax_t) * 8 - bit_width);
}

static inline uintmax_t unsigned_integer_max_from_bit_width(int bit_width)
{
	assert((size_t)bit_width <= sizeof(uintmax_t) * 8);
	return UINTMAX_MAX >> (sizeof(uintmax_t) * 8 - bit_width);
}

static inline intmax_t
signed_integer_min_from_size(size_t size)
{
	return signed_integer_min_from_bit_width(size * 8);
}

static inline intmax_t
signed_integer_max_from_size(size_t size)
{
	return signed_integer_max_from_bit_width(size * 8);
}

static inline uintmax_t
unsigned_integer_max_from_size(size_t size)
{
	return unsigned_integer_max_from_bit_width(size * 8);
}

static inline bool str_null_eq(const char *s1, const char *s2)
{
	if (s1 == s2)
		return true;
	if (!s1 || !s2)
		return false;
	return !strcmp(s1, s2);
}

#endif
