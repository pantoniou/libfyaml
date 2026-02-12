/*
 * fy-utf8.h - UTF-8 methods
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_UTF8_H
#define FY_UTF8_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include "fy-win32.h"
#include "fy-utils.h"

#define FY_UTF8_MIN_WIDTH 1
#define FY_UTF8_MAX_WIDTH 4

extern const int8_t fy_utf8_width_table[32];

static inline int
fy_utf8_width_by_first_octet_no_table(const uint8_t c)
{
	return (c & 0x80) == 0x00 ? 1 :
	       (c & 0xe0) == 0xc0 ? 2 :
	       (c & 0xf0) == 0xe0 ? 3 :
	       (c & 0xf8) == 0xf0 ? 4 : 0;
}

static inline FY_ALWAYS_INLINE int
fy_utf8_width_by_first_octet(uint8_t c)
{
	return fy_utf8_width_table[c >> 3];
}

/* assumes valid utf8 character */
static inline size_t
fy_utf8_width(const int c)
{
	return 1 + (c >= 0x80) + (c >= 0x800) + (c >= 0x10000);
}

static inline bool
fy_utf8_is_valid(const int c)
{
	return c >= 0 && !((c >= 0xd800 && c <= 0xdfff) || c >= 0x110000);
}

/* generic utf8 decoder (not inlined) */
int fy_utf8_get_generic(const void *ptr, size_t left, int *widthp);

/* -1 for end of input, -2 for invalid character, -3 for partial */
#define FYUG_EOF	-1
#define FYUG_INV	-2
#define FYUG_PARTIAL	-3

#define FY_UTF8_64_C(_x)	((int)(int32_t)(_x))
#define FY_UTF8_64_W(_x)	(((int)((_x) >> 32)))

#define FY_UTF8_64_MAKE(_w, _c)	((int64_t)(((int64_t)((_w)) << 32) | (int64_t)(_c)))

static inline FY_ALWAYS_INLINE int64_t
fy_utf8_get_branch_64(const void *ptr, size_t left)
{
	const uint8_t *s = ptr;
	unsigned int a, b, c, d;
	uint32_t code;

	if (!left)
		return FYUG_EOF;

	a = s[0];
	if (a < 0x80) 				// 1 byte: 0xxxxxxx
		return FY_UTF8_64_MAKE(1, a);

	if (left < 2)
		return FYUG_PARTIAL;

	b = s[1];
	if ((a & 0xe0) == 0xc0) {		// 2 bytes: 110xxxxx 10xxxxxx
		if ((b & 0xc0) != 0x80)
			return FYUG_INV;

		code = (int)(((a & 0x1f) << 6) | (b & 0x3f));
		if (code < 0x80)
			return FYUG_INV;	// overlong

		return FY_UTF8_64_MAKE(2, code);
	}

	if (left < 3)
		return FYUG_PARTIAL;

	c = s[2];
	if ((a & 0xf0) == 0xe0) {		// 3 bytes: 1110xxxx 10xxxxxx 10xxxxxx

		if ((b & 0xc0) != 0x80 || (c & 0xc0) != 0x80)
			return FYUG_INV;

		code = (int)(((a & 0x0f) << 12) | ((b & 0x3f) << 6) | (c & 0x3f));
		if (code < 0x800 || (code >= 0xd800 && code <= 0xdfff))
			return FYUG_INV;	// overlong or surrogate

		return FY_UTF8_64_MAKE(3, code);
	}

	if (left < 4)
		return FYUG_PARTIAL;

	d = s[3];
	if ((a & 0xf8) == 0xf0) {		// 4 bytes: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx

		if ((b & 0xc0) != 0x80 || (c & 0xc0) != 0x80 || (d & 0xc0) != 0x80)
			return FYUG_INV;

		code = ((a & 0x07) << 18) | ((b & 0x3f) << 12) |
		       ((c & 0x3f) <<  6)  | (d & 0x3f);

		if (code < 0x10000 || code > 0x10ffff)
			return FYUG_INV;	// overlong or out of range

		return FY_UTF8_64_MAKE(4, code);
	}

	return FYUG_INV;
}

static inline FY_ALWAYS_INLINE int
fy_utf8_get_branch(const void *ptr, size_t left, int *widthp)
{
	const uint8_t *s = ptr;
	unsigned int a, b, c, d;
	int code;

	if (!left)
		goto err_eof;

	a = s[0];
	if (a < 0x80) {				// 1 byte: 0xxxxxxx
		code = (int)a;
		*widthp = 1;
		return code;
	}

	if (left < 2)
		goto err_partial;

	b = s[1];
	if ((a & 0xe0) == 0xc0) {		// 2 bytes: 110xxxxx 10xxxxxx
		if ((b & 0xc0) != 0x80)
			goto err_inv;

		code = (int)(((a & 0x1f) << 6) | (b & 0x3f));
		if (code < 0x80)
			goto err_inv;		// overlong

		*widthp = 2;
		return code;
	}

	if (left < 3)
		goto err_partial;

	c = s[2];
	if ((a & 0xf0) == 0xe0) {		// 3 bytes: 1110xxxx 10xxxxxx 10xxxxxx

		if ((b & 0xc0) != 0x80 || (c & 0xc0) != 0x80)
			goto err_inv;

		code = (int)(((a & 0x0f) << 12) | ((b & 0x3f) << 6) | (c & 0x3f));
		if (code < 0x800 || (code >= 0xd800 && code <= 0xdfff))
			goto err_inv;		// overlong or surrogate

		*widthp = 3;
		return code;
	}

	if (left < 4)
		goto err_partial;

	d = s[3];
	if ((a & 0xf8) == 0xf0) {		// 4 bytes: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx

		if ((b & 0xc0) != 0x80 || (c & 0xc0) != 0x80 || (d & 0xc0) != 0x80)
			goto err_inv;

		code = ((a & 0x07) << 18) | ((b & 0x3f) << 12) |
		       ((c & 0x3f) <<  6)  | (d & 0x3f);

		if (code < 0x10000 || code > 0x10ffff)
			goto err_inv;		// overlong or out of range

		*widthp = 4;
		return code;
	}

err_inv:
	*widthp = 0;
	return FYUG_INV;

err_partial:
	*widthp = 0;
	return FYUG_PARTIAL;

err_eof:
	*widthp = 0;
	return FYUG_EOF;
}

static inline FY_ALWAYS_INLINE int
fy_utf8_get_table(const void *ptr, size_t left, int *widthp)
{
	const uint8_t *s = ptr;
	unsigned int a, b, c, d;
	int code, w;

	if (!left)
		goto err_eof;

	w = fy_utf8_width_by_first_octet(s[0]);
	if ((size_t)w > left)
		goto err_partial;

	*widthp = w;

	switch (w) {
	case 1:
		a = s[0];
		code = (int)a;
		return code;
	case 2:
		a = s[0];
		b = s[1];
		if ((b & 0xc0) != 0x80)
			goto err_inv;

		code = (int)(((a & 0x1f) << 6) | (b & 0x3f));
		if (code < 0x80)
			goto err_inv;		// overlong

		return code;
	case 3:
		a = s[0];
		b = s[1];
		c = s[2];

		if (((b | c) & 0xc0) != 0x80)
			goto err_inv;

		code = (int)(((a & 0x0f) << 12) | ((b & 0x3f) << 6) | (c & 0x3f));
		if (code < 0x800 || (code >= 0xd800 && code <= 0xdfff))
			goto err_inv;		// overlong or surrogate

		return code;
	case 4:
		a = s[0];
		b = s[1];
		c = s[2];
		d = s[3];

		if (((b | c | d) & 0xc0) != 0x80)
			goto err_inv;

		code = ((a & 0x07) << 18) | ((b & 0x3f) << 12) |
		       ((c & 0x3f) <<  6) | (d & 0x3f);

		if (code < 0x10000 || code > 0x10ffff)
			goto err_inv;		// overlong or out of range

		return code;

	default:
		break;
	}

err_inv:
	*widthp = 0;
	return FYUG_INV;

err_partial:
	*widthp = 0;
	return FYUG_PARTIAL;

err_eof:
	*widthp = 0;
	return FYUG_EOF;
}

static inline FY_ALWAYS_INLINE int
fy_utf8_get(const void *ptr, size_t left, int *widthp)
{
	return fy_utf8_get_branch(ptr, left, widthp);
}

static inline FY_ALWAYS_INLINE int64_t
fy_utf8_get_64(const void *ptr, size_t left)
{
	return fy_utf8_get_branch_64(ptr, left);
}

static inline FY_ALWAYS_INLINE int
fy_utf8_get_no_width(const void *ptr, size_t left)
{
	int w;

	return fy_utf8_get(ptr, left, &w);
}

static inline FY_ALWAYS_INLINE int
fy_utf8_get_end(const void *ptr, const void *ptr_end, int *widthp)
{
	return fy_utf8_get(ptr, (size_t)((const char *)ptr_end - (const char *)ptr), widthp);
}

static inline FY_ALWAYS_INLINE int
fy_utf8_get_end_no_width(const void *ptr, const void *ptr_end)
{
	int w;

	return fy_utf8_get(ptr, (size_t)((const char *)ptr_end - (const char *)ptr), &w);
}

int fy_utf8_get_right_generic(const void *ptr, size_t left, int *widthp);

static inline int fy_utf8_get_right(const void *ptr, size_t left, int *widthp)
{
	const uint8_t *p = (const uint8_t *)ptr + left;

	/* single byte (hot path) */
	if (left > 0 && !(p[-1] & 0x80)) {
		if (widthp)
			*widthp = 1;
		return p[-1] & 0x7f;
	}
	return fy_utf8_get_right_generic(ptr, left, widthp);
}


/* for when you _know_ that there's enough room and c is valid */
static inline void *fy_utf8_put_unchecked(void *ptr, int c)
{
	uint8_t *s = ptr;

	assert(c >= 0);
	if (c < 0x80)
		*s++ = c;
	else if (c < 0x800) {
		*s++ = (c >> 6) | 0xc0;
		*s++ = (c & 0x3f) | 0x80;
	} else if (c < 0x10000) {
		*s++ = (c >> 12) | 0xe0;
		*s++ = ((c >> 6) & 0x3f) | 0x80;
		*s++ = (c & 0x3f) | 0x80;
	} else {
		*s++ = (c >> 18) | 0xf0;
		*s++ = ((c >> 12) & 0x3f) | 0x80;
		*s++ = ((c >> 6) & 0x3f) | 0x80;
		*s++ = (c & 0x3f) | 0x80;
	}
	return s;
}

static inline void *fy_utf8_put(void *ptr, size_t left, int c)
{
	if (!fy_utf8_is_valid(c) || fy_utf8_width(c) > left)
		return NULL;

	return fy_utf8_put_unchecked(ptr, c);
}

/* buffer must contain at least 5 characters */
#define FY_UTF8_FORMAT_BUFMIN	5
enum fy_utf8_escape {
	fyue_none,
	fyue_singlequote,
	fyue_doublequote,
	fyue_doublequote_json,
	fyue_doublequote_yaml_1_1,
};

static inline bool fy_utf8_escape_is_any_doublequote(const enum fy_utf8_escape esc)
{
	return esc >= fyue_doublequote && esc <= fyue_doublequote_yaml_1_1;
}

char *fy_utf8_format(int c, char *buf, const enum fy_utf8_escape esc);

#ifdef _MSC_VER
/* MSVC doesn't support GCC statement expressions - use static buffers */
static __declspec(thread) char fy_utf8_format_a_buf[FY_UTF8_FORMAT_BUFMIN];
static __declspec(thread) char fy_utf8_format_text_a_buf[4096];

#define fy_utf8_format_a(_c, _esc) \
	strcpy(alloca(FY_UTF8_FORMAT_BUFMIN + 1), \
		fy_utf8_format((_c), fy_utf8_format_a_buf, (_esc)))

#define fy_utf8_format_text_a(_buf, _len, _esc) \
	strcpy(alloca(sizeof(fy_utf8_format_text_a_buf) + 1), \
		fy_utf8_format_text((_buf), (_len), \
			fy_utf8_format_text_a_buf, sizeof(fy_utf8_format_text_a_buf) - 1, (_esc)))
#else
/* GCC/Clang version with statement expressions */
#define fy_utf8_format_a(_c, _esc) \
	({ \
	 	char *_buf = alloca(FY_UTF8_FORMAT_BUFMIN); \
	 	fy_utf8_format((_c), _buf, _esc); \
	})
#endif

size_t fy_utf8_format_text_length(const char *buf, size_t len,
			          enum fy_utf8_escape esc);
char *fy_utf8_format_text(const char *buf, size_t len,
			  char *out, size_t maxsz,
			  enum fy_utf8_escape esc);

#ifndef _MSC_VER
#define fy_utf8_format_text_a(_buf, _len, _esc) \
	({ \
		const char *__buf = (_buf); \
		size_t __len = (_len); \
		enum fy_utf8_escape __esc = (_esc); \
		size_t _outsz = fy_utf8_format_text_length(__buf, __len, __esc); \
		char *_out = alloca(_outsz + 1); \
		fy_utf8_format_text(__buf, __len, _out, _outsz, __esc); \
	})
#endif

char *fy_utf8_format_text_alloc(const char *buf, size_t len, const enum fy_utf8_escape esc);

const void *fy_utf8_memchr_generic(const void *s, int c, size_t n);

static inline const void *fy_utf8_memchr(const void *s, int c, size_t n)
{
	if (c < 0 || !n)
		return NULL;
	if (c < 0x80)
		return memchr(s, c, n);
	return fy_utf8_memchr_generic(s, c, n);
}

static inline const void *fy_utf8_strchr(const void *s, int c)
{
	if (c < 0)
		return NULL;
	if (c < 0x80)
		return strchr(s, c);
	return fy_utf8_memchr_generic(s, c, strlen(s));
}

static inline int fy_utf8_count(const void *ptr, size_t len)
{
	const uint8_t *s = (const uint8_t *)ptr, *e = (const uint8_t *)ptr + len;
	int w, count;

	count = 0;
	while (s < e) {
		w = fy_utf8_width_by_first_octet(*s);

		/* malformed? */
		if (!w || s + w > e)
			break;
		s += w;

		count++;
	}

	return count;
}

int fy_utf8_parse_escape(const char **strp, size_t len, const enum fy_utf8_escape esc);

#define F_NONE			0
#define F_SIMPLE_SCALAR		FY_BIT(0)	/* part of simple scalar 0-9a-zA-Z_ */
#define F_DIRECT_PRINT		FY_BIT(1)	/* 0x20..0x7e */
#define F_LB			FY_BIT(2)	/* is a linebreak */
#define F_WS			FY_BIT(3)	/* is a whitespace */
#define F_LETTER		FY_BIT(4)	/* is a letter a..z A..Z */
#define F_DIGIT			FY_BIT(5)	/* is a digit 0..9 */
#define F_XDIGIT		FY_BIT(6)	/* is a hex digit 0..9 a-f A-F */
#define F_FLOW_INDICATOR	FY_BIT(7)	/* is ,[]{} */

extern const uint8_t fy_utf8_low_ascii_flags[256];

void *fy_utf8_split_posix(const char *str, int *argcp, const char * const *argvp[]);

int fy_utf8_get_generic_s(const void *ptr, const void *ptr_end, int *widthp);
int fy_utf8_get_generic_s_nocheck(const void *ptr, int *widthp);

static inline int fy_utf8_get_s(const void *ptr, const void *ptr_end, int *widthp)
{
	const uint8_t *p = ptr;

	/* single byte (hot path) */
	if (ptr >= ptr_end) {
		*widthp = 0;
		return FYUG_EOF;
	}

	if (!(p[0] & 0x80)) {
		*widthp = 1;
		return p[0];
	}
	return fy_utf8_get_generic_s(ptr, ptr_end, widthp);
}

static inline int fy_utf8_get_s_nocheck(const void *ptr, int *widthp)
{
	const uint8_t *p = ptr;

	if (!(p[0] & 0x80)) {
		*widthp = 1;
		return p[0];
	}
	return fy_utf8_get_generic_s_nocheck(ptr, widthp);
}

/* for most 64 bit arches this will fit in a single register */
struct fy_utf8_result {
	int c;
	int w;
};

/* probably the most performant version */
static inline struct fy_utf8_result fy_utf8_get_s_res(const void *ptr, const void *ptr_end)
{
	const uint8_t *p = ptr;
	int c, width;

	/* single byte (hot path) */
	if (ptr >= ptr_end)
		return (struct fy_utf8_result){ FYUG_EOF, 0 };

	if (!(p[0] & 0x80))
		return (struct fy_utf8_result){ p[0], 1 };

	c = fy_utf8_get_generic_s(ptr, ptr_end, &width);
	if (c < 0)
		return (struct fy_utf8_result){ c, 0 };
	return (struct fy_utf8_result){ c, width };
}

static inline bool fy_utf8_is_space(const int c)
{
	return c == ' ';
}

static inline bool fy_utf8_is_tab(const int c)
{
	return c == '\t';
}

static inline bool fy_utf8_is_simple_scalar_no_check(const int c)
{
	return fy_utf8_low_ascii_flags[(uint8_t)c] & F_SIMPLE_SCALAR;
}

static inline bool
fy_utf8_is_printable_ascii_no_check(const int c)
{
	return fy_utf8_low_ascii_flags[(uint8_t)c] & F_DIRECT_PRINT;
}

static inline bool fy_utf8_is_lb_no_check(const int c)
{
	return fy_utf8_low_ascii_flags[(uint8_t)c] & F_LB;
}

static inline bool fy_utf8_is_ws_no_check(const int c)
{
	return fy_utf8_low_ascii_flags[(uint8_t)c] & F_WS;
}

static inline bool fy_utf8_is_ws_lb_no_check(const int c)
{
	return fy_utf8_low_ascii_flags[(uint8_t)c] & (F_WS | F_LB);
}

static inline bool fy_utf8_is_letter_no_check(const int c)
{
	return fy_utf8_low_ascii_flags[(uint8_t)c] & F_LETTER;
}

static inline bool fy_utf8_is_digit_no_check(const int c)
{
	return fy_utf8_low_ascii_flags[(uint8_t)c] & F_DIGIT;
}

static inline bool fy_utf8_is_hex_no_check(const int c)
{
	return fy_utf8_low_ascii_flags[(uint8_t)c] & F_XDIGIT;
}

static inline bool fy_utf8_is_flow_indicator_no_check(const int c)
{
	return fy_utf8_low_ascii_flags[(uint8_t)c] & F_FLOW_INDICATOR;
}

static inline bool fy_utf8_is_low_ascii(const int c)
{
	return (unsigned int)c < 128;
}

static inline bool fy_utf8_is_simple_scalar(const int c)
{
	return fy_utf8_is_low_ascii(c) && fy_utf8_is_simple_scalar_no_check(c);
}

static inline bool
fy_utf8_is_printable_ascii_x(const int c)
{
	return fy_utf8_is_low_ascii(c) && fy_utf8_is_printable_ascii_no_check(c);
}

static inline bool
fy_utf8_is_printable_ascii(const int c)
{
	return c >= 0x20 && c <= 0x7e;
}

static inline bool fy_utf8_is_lb(const int c)
{
	return fy_utf8_is_low_ascii(c) && fy_utf8_is_lb_no_check(c);
}

static inline bool fy_utf8_is_ws(const int c)
{
	return fy_utf8_is_low_ascii(c) && fy_utf8_is_ws_no_check(c);
}

static inline bool fy_utf8_is_ws_lb(const int c)
{
	return fy_utf8_is_low_ascii(c) && fy_utf8_is_ws_lb_no_check(c);
}

static inline bool fy_utf8_is_letter(const int c)
{
	return fy_utf8_is_low_ascii(c) && fy_utf8_is_letter_no_check(c);
}

static inline bool fy_utf8_is_digit(const int c)
{
	return fy_utf8_is_low_ascii(c) && fy_utf8_is_digit_no_check(c);
}

static inline bool fy_utf8_is_hex(const int c)
{
	return fy_utf8_is_low_ascii(c) && fy_utf8_is_hex_no_check(c);
}

static inline bool fy_utf8_is_flow_indicator(const int c)
{
	return fy_utf8_is_low_ascii(c) && fy_utf8_is_flow_indicator_no_check(c);
}

#endif
