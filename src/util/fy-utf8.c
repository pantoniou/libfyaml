/*
 * fy-utf8.c - utf8 handling methods
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "fy-utf8.h"

/* to avoid dragging in libfyaml.h */
#ifndef FY_BIT
#define FY_BIT(x) (1U << (x))
#endif

const int8_t fy_utf8_width_table[32] = {
	1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 0, 0, 0, 0,
	2, 2, 2, 2, 3, 3, 4, 0,
};

int fy_utf8_get_generic(const void *ptr, size_t left, int *widthp)
{
	const uint8_t *p = ptr;
	int i, width, value;

	if (left < 1)
		return FYUG_EOF;

	/* this is the slow path */
	width = fy_utf8_width_by_first_octet(p[0]);
	if (!width)
		return FYUG_INV;
	if ((size_t)width > left)
		return FYUG_PARTIAL;

	/* initial value */
	value = *p++ & (0xff >> width);
	for (i = 1; i < width; i++) {
		if ((*p & 0xc0) != 0x80)
			return FYUG_INV;
		value = (value << 6) | (*p++ & 0x3f);
	}

	if (!fy_utf8_is_valid(value))
		return FYUG_INV;

	*widthp = width;

	return value;
}

int fy_utf8_get_right_generic(const void *ptr, size_t left, int *widthp)
{
	const uint8_t *s, *e;
	uint8_t v;

	s = ptr;
	e =  s + left;

	if (left < 1)
		return FYUG_EOF;

	/* single byte sequence */
	v = e[-1];
	if ((v & 0x80) == 0) {
		if (widthp)
			*widthp = 1;
		return (int)v & 0x7f;
	}

	/* the last byte must be & 0xc0 == 0x80 */
	if ((v & 0xc0) != 0x80)
		return FYUG_INV;

	/* at least two byte sequence */
	if (left < 2)
		return FYUG_EOF;

	v = e[-2];
	/* the first byte of the sequence (must be a two byte sequence) */
	if ((v & 0xc0) != 0x80) {
		/* two byte start is 110x_xxxx */
		if ((v & 0xe0) != 0xc0)
			return FYUG_INV;
		return fy_utf8_get(e - 2, 2, widthp);
	}

	/* at least three byte sequence */
	if (left < 3)
		return FYUG_EOF;

	v = e[-3];
	/* the first byte of the sequence (must be a three byte sequence) */
	if ((v & 0xc0) != 0x80) {
		/* three byte start is 1110_xxxx */
		if ((v & 0xf0) != 0xe0)
			return FYUG_INV;
		return fy_utf8_get(e - 3, 3, widthp);
	}

	/* at least four byte sequence */
	if (left < 4)
		return FYUG_EOF;

	v = e[-4];

	/* the first byte of the sequence (must be a four byte sequence) */
	/* four byte start is 1111_0xxx */
	if ((v & 0xf8) != 0xf0) {
		return FYUG_INV;
	}
	return fy_utf8_get(e - 4, 4, widthp);
}

struct fy_utf8_fmt_esc_map {
	const int *ch;
	const int *map;
};

static const struct fy_utf8_fmt_esc_map esc_all = {
	.ch  = (const int []){ '\\', '\0', '\b', '\r', '\t', '\f', '\n', '\v', '\a', '\x1b', 0x85, 0xa0, 0x2028, 0x2029, -1 },
	.map = (const int []){ '\\',  '0',  'b',  'r',  't',  'f',  'n',  'v',  'a',  'e',  'N',  '_',    'L',    'P',  0 }
};

static inline int esc_map(const struct fy_utf8_fmt_esc_map *esc_map, int c)
{
	const int *ch;
	int cc;

	ch = esc_map->ch;
	while ((cc = *ch++) >= 0) {
		if (cc == c)
			return esc_map->map[(ch - esc_map->ch) - 1];
	}
	return -1;
}

static inline int fy_utf8_esc_map(int c, enum fy_utf8_escape esc)
{
	if (esc == fyue_none)
		return -1;
	if (esc == fyue_singlequote && c == '\'')
		return '\'';
	if (fy_utf8_escape_is_any_doublequote(esc) && c == '"')
		return '"';
	return esc_map(&esc_all, c);
}

size_t fy_utf8_format_text_length(const char *buf, size_t len,
			          enum fy_utf8_escape esc)
{
	int c, w;
	ssize_t l;
	const char *s, *e;

	s = buf;
	e = buf + len;
	l = 0;
	while (s < e) {
		c = fy_utf8_get(s, e - s, &w);
		if (!w || c < 0)
			break;
		s += w;
		if (fy_utf8_esc_map(c, esc))
			w = 2;
		l += w;
	}
	return l + 1;
}

char *fy_utf8_format_text(const char *buf, size_t len,
			  char *out, size_t maxsz,
			  enum fy_utf8_escape esc)
{
	int c, w, cc;
	const char *s, *e;
	char *os, *oe;

	s = buf;
	e = buf + len;
	os = out;
	oe = out + maxsz - 1;
	while (s < e) {
		c = fy_utf8_get(s, e - s, &w);
		if (!w || c < 0)
			break;
		s += w;

		if ((cc = fy_utf8_esc_map(c, esc)) > 0) {
			if (os + 2 > oe)
				break;
			*os++ = '\\';
			*os++ = cc;
			continue;
		}

		if (os + w > oe)
			break;

		os = fy_utf8_put_unchecked(os, c);
	}
	*os++ = '\0';
	return out;
}

char *fy_utf8_format(int c, char *buf, enum fy_utf8_escape esc)
{
	int cc;
	char *s;

	if (!fy_utf8_is_valid(c)) {
		*buf = '\0';
		return buf;
	}

	s = buf;
	if ((cc = fy_utf8_esc_map(c, esc)) > 0) {
		*s++ = '\\';
		*s++ = cc;
	} else
		s = fy_utf8_put_unchecked(s, c);
	*s = '\0';
	return buf;
}

char *fy_utf8_format_text_alloc(const char *buf, size_t len, enum fy_utf8_escape esc)
{
	size_t outsz;
	char *out;

	outsz = fy_utf8_format_text_length(buf, len, esc);
	if ((ssize_t)outsz < 0)
		return NULL;
	out = malloc(outsz);
	if (!out)
		return NULL;
	fy_utf8_format_text(buf, len, out, outsz, esc);

	return out;
}

const void *fy_utf8_memchr_generic(const void *s, int c, size_t n)
{
	int cc, w;
	const char *sc, *e;

	sc = (const char *)s;
	e = sc + n;
	while (sc < e && (cc = fy_utf8_get(sc, e - sc, &w)) >= 0) {
		if (c == cc)
			return sc;
		sc += w;
	}

	return NULL;
}

/* parse an escape and return utf8 value */
int fy_utf8_parse_escape(const char **strp, size_t len, enum fy_utf8_escape esc)
{
	const char *s, *e;
	char c;
	int i, value, code_length, cc, w;
	unsigned int hi_surrogate, lo_surrogate;

	/* why do you bother us? */
	if (esc == fyue_none)
		return -1;

	if (!strp || !*strp || len < 2)
		return -1;

	value = -1;

	s = *strp;
	e = s + len;

	c = *s++;

	if (esc == fyue_singlequote) {
		if (c != '\'')
			goto out;
		c = *s++;
		if (c != '\'')
			goto out;

		value = '\'';
		goto out;
	}

	/* get '\\' */
	if (c != '\\')
		goto out;

	c = *s++;

	/* common YAML & JSON escapes */
	switch (c) {
	case 'b':
		value = '\b';
		break;
	case 'f':
		value = '\f';
		break;
	case 'n':
		value = '\n';
		break;
	case 'r':
		value = '\r';
		break;
	case 't':
		value = '\t';
		break;
	case '"':
		value = '"';
		break;
	case '/':
		value = '/';
		break;
	case '\\':
		value = '\\';
		break;
	default:
		break;
	}

	if (value >= 0)
		goto out;

	if (esc == fyue_doublequote || esc == fyue_doublequote_yaml_1_1) {
		switch (c) {
		case '0':
			value = '\0';
			break;
		case 'a':
			value = '\a';
			break;
		case '\t':
			value = '\t';
			break;
		case 'v':
			value = '\v';
			break;
		case 'e':
			value = '\x1b';
			break;
		case ' ':
			value = ' ';
			break;
		case 'N':
			value = 0x85;	/* NEL */
			break;
		case '_':
			value = 0xa0;
			break;
		case 'L':
			value = 0x2028;	/* LS */
			break;
		case 'P':	/* PS 0x2029 */
			value = 0x2029; /* PS */
			break;
		default:
			/* weird unicode escapes */
			if ((uint8_t)c >= 0x80) {
				/* in non yaml-1.1 mode we don't allow this craziness */
				if (esc == fyue_doublequote)
					goto out;

				cc = fy_utf8_get(s - 1, e - (s - 1), &w);
				switch (cc) {
				case 0x2028:
				case 0x2029:
				case 0x85:
				case 0xa0:
					value = cc;
					break;
				default:
					break;
				}
			}
			break;
		}
		if (value >= 0)
			goto out;
	}

	/* finally try the unicode escapes */
	code_length = 0;

	if (esc == fyue_doublequote || esc == fyue_doublequote_yaml_1_1) {
		switch (c) {
		case 'x':
			code_length = 2;
			break;
		case 'u':
			code_length = 4;
			break;
		case 'U':
			code_length = 8;
			break;
		default:
			return -1;
		}
	} else if (esc == fyue_doublequote_json && c == 'u')
		code_length = 4;

	if (!code_length || code_length > (e - s))
		goto out;

	value = 0;
	for (i = 0; i < code_length; i++) {
		c = *s++;
		value <<= 4;
		if (c >= '0' && c <= '9')
			value |= c - '0';
		else if (c >= 'a' && c <= 'f')
			value |= 10 + c - 'a';
		else if (c >= 'A' && c <= 'F')
			value |= 10 + c - 'A';
		else
			goto out;
	}

	/* hi/lo surrogate pair */
	if (code_length == 4 && value >= 0xd800 && value <= 0xdbff &&
		(e - s) >= 6 && s[0] == '\\' && s[1] == 'u') {
		hi_surrogate = value;

		s += 2;

		value = 0;
		for (i = 0; i < code_length; i++) {
			c = *s++;
			value <<= 4;
			if (c >= '0' && c <= '9')
				value |= c - '0';
			else if (c >= 'a' && c <= 'f')
				value |= 10 + c - 'a';
			else if (c >= 'A' && c <= 'F')
				value |= 10 + c - 'A';
			else
				return -1;
		}
		lo_surrogate = value;
		value = 0x10000 + (hi_surrogate - 0xd800) * 0x400 + (lo_surrogate - 0xdc00);
	}

out:
	*strp = s;

	return value;
}

const uint8_t fy_utf8_low_ascii_flags[256] = {
	[0x00] = 0,					// NUL '\0' (null character)
	[0x01] = 0, 					// SOH (start of heading)
	[0x02] = 0,					// STX (start of text)
	[0x03] = 0,					// ETX (end of text)
	[0x04] = 0,					// EOT (end of transmission)
	[0x05] = 0,					// ENQ (enquiry)
	[0x06] = 0,					// ACK (acknowledge)
	[0x07] = 0,					// BEL '\a' (bell)
	[0x08] = 0,					// BS  '\b' (backspace)
	[0x09] = F_WS,					// HT  '\t' (horizontal tab)
	[0x0A] = F_LB,					// LF  '\n' (new line)
	[0x0B] = 0,					// VT  '\v' (vertical tab)
	[0x0C] = 0,					// FF  '\f' (form feed)
	[0x0D] = F_LB,					// CR  '\r' (carriage ret)
	[0x0E] = 0,					// SO  (shift out)
	[0x0F] = 0,					// SI  (shift in)
	[0x10] = 0,					// DLE (data link escape)
	[0x11] = 0,					// DC1 (device control 1)
	[0x12] = 0,					// DC2 (device control 2)
	[0x13] = 0,					// DC3 (device control 3)
	[0x14] = 0,					// DC4 (device control 4)
	[0x15] = 0,					// NAK (negative ack.)
	[0x16] = 0,					// SYN (synchronous idle)
	[0x17] = 0,					// ETB (end of trans. blk)
	[0x18] = 0,					// CAN (cancel)
	[0x19] = 0,					// EM  (end of medium)
	[0x1A] = 0,					// SUB (substitute)
	[0x1B] = 0,					// ESC (escape)
	[0x1C] = 0,					// FS  (file separator)
	[0x1D] = 0,					// GS  (group separator)
	[0x1E] = 0,					// RS  (record separator)
	[0x1F] = 0,					// US  (unit separator)
	[' ']  = F_DIRECT_PRINT | F_WS,
	['!']  = F_DIRECT_PRINT,
	['"']  = F_DIRECT_PRINT,
	['#']  = F_DIRECT_PRINT,
	['$']  = F_DIRECT_PRINT,
	['%']  = F_DIRECT_PRINT,
	['&']  = F_DIRECT_PRINT,
	['\''] = F_DIRECT_PRINT,
	['(']  = F_DIRECT_PRINT,
	[')']  = F_DIRECT_PRINT,
	['*']  = F_DIRECT_PRINT,
	['+']  = F_DIRECT_PRINT,
	[',']  = F_DIRECT_PRINT | F_FLOW_INDICATOR,
	['-']  = F_DIRECT_PRINT,
	['.']  = F_DIRECT_PRINT,
	['/']  = F_DIRECT_PRINT,
	['0']  = F_DIRECT_PRINT | F_DIGIT | F_XDIGIT | F_SIMPLE_SCALAR,
	['1']  = F_DIRECT_PRINT | F_DIGIT | F_XDIGIT | F_SIMPLE_SCALAR,
	['2']  = F_DIRECT_PRINT | F_DIGIT | F_XDIGIT | F_SIMPLE_SCALAR,
	['3']  = F_DIRECT_PRINT | F_DIGIT | F_XDIGIT | F_SIMPLE_SCALAR,
	['4']  = F_DIRECT_PRINT | F_DIGIT | F_XDIGIT | F_SIMPLE_SCALAR,
	['5']  = F_DIRECT_PRINT | F_DIGIT | F_XDIGIT | F_SIMPLE_SCALAR,
	['6']  = F_DIRECT_PRINT | F_DIGIT | F_XDIGIT | F_SIMPLE_SCALAR,
	['7']  = F_DIRECT_PRINT | F_DIGIT | F_XDIGIT | F_SIMPLE_SCALAR,
	['8']  = F_DIRECT_PRINT | F_DIGIT | F_XDIGIT | F_SIMPLE_SCALAR,
	['9']  = F_DIRECT_PRINT | F_DIGIT | F_XDIGIT | F_SIMPLE_SCALAR,
	[':']  = F_DIRECT_PRINT,
	[';']  = F_DIRECT_PRINT,
	['<']  = F_DIRECT_PRINT,
	['=']  = F_DIRECT_PRINT,
	['>']  = F_DIRECT_PRINT,
	['?']  = F_DIRECT_PRINT,
	['@']  = F_DIRECT_PRINT,
	['A']  = F_DIRECT_PRINT | F_LETTER | F_XDIGIT | F_SIMPLE_SCALAR,
	['B']  = F_DIRECT_PRINT | F_LETTER | F_XDIGIT | F_SIMPLE_SCALAR,
	['C']  = F_DIRECT_PRINT | F_LETTER | F_XDIGIT | F_SIMPLE_SCALAR,
	['D']  = F_DIRECT_PRINT | F_LETTER | F_XDIGIT | F_SIMPLE_SCALAR,
	['E']  = F_DIRECT_PRINT | F_LETTER | F_XDIGIT | F_SIMPLE_SCALAR,
	['F']  = F_DIRECT_PRINT | F_LETTER | F_XDIGIT | F_SIMPLE_SCALAR,
	['G']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['H']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['I']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['J']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['K']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['L']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['M']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['N']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['O']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['P']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['Q']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['R']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['S']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['T']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['U']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['V']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['W']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['X']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['Y']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['Z']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['[']  = F_DIRECT_PRINT | F_FLOW_INDICATOR,
	['\\'] = F_DIRECT_PRINT,  				// '\\'
	[']']  = F_DIRECT_PRINT | F_FLOW_INDICATOR,
	['^']  = F_DIRECT_PRINT,
	['_']  = F_DIRECT_PRINT                       | F_SIMPLE_SCALAR,
	['`']  = F_DIRECT_PRINT,
	['a']  = F_DIRECT_PRINT | F_LETTER | F_XDIGIT | F_SIMPLE_SCALAR,
	['b']  = F_DIRECT_PRINT | F_LETTER | F_XDIGIT | F_SIMPLE_SCALAR,
	['c']  = F_DIRECT_PRINT | F_LETTER | F_XDIGIT | F_SIMPLE_SCALAR,
	['d']  = F_DIRECT_PRINT | F_LETTER | F_XDIGIT | F_SIMPLE_SCALAR,
	['e']  = F_DIRECT_PRINT | F_LETTER | F_XDIGIT | F_SIMPLE_SCALAR,
	['f']  = F_DIRECT_PRINT | F_LETTER | F_XDIGIT | F_SIMPLE_SCALAR,
	['g']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['h']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['i']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['j']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['k']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['l']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['m']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['n']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['o']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['p']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['q']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['r']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['s']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['t']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['u']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['v']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['w']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['x']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['y']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['z']  = F_DIRECT_PRINT | F_LETTER            | F_SIMPLE_SCALAR,
	['{']  = F_DIRECT_PRINT | F_FLOW_INDICATOR,
	['|']  = F_DIRECT_PRINT,
	['}']  = F_DIRECT_PRINT | F_FLOW_INDICATOR,
	['~']  = F_DIRECT_PRINT,
	[0x7F] = 0,					// DEL
	/* the rest are zero */
};

void *fy_utf8_split_posix(const char *str, int *argcp, const char * const *argvp[])
{
	enum split_state {
		SS_WS,			/* at whitespace */
		SS_WS_BS,		/* backslash at whitespace */
		SS_UQ,			/* at unquoted */
		SS_UQ_BS,		/* backslash at unquoted */
		SS_SQ,			/* at single quote */
		SS_SQ_BS,		/* backslash at single quote */
		SS_DQ,			/* at double quote */
		SS_DQ_BS,		/* backslash at double quote */
		SS_DQ_BS_OCT1,		/* in \nnn octal escape digit 1 */
		SS_DQ_BS_OCT2,		/* in \nnn octal escape digit 2 */
		SS_DQ_BS_HEX0,		/* in \xNN hex escape digit 0 */
		SS_DQ_BS_HEX1,		/* in \xNN hex escape digit 1 */
		SS_DQ_BS_HEX2,		/* in \xNN hex escape digit 2 */
		SS_DQ_BS_C,		/* in \cN control character escape */
		SS_WS_BS_ERR_EOL,	/* EOL while waiting for char after \ */
		SS_UQ_BS_ERR_EOL,	/* EOL while waiting for char after \ */
		SS_SQ_BS_ERR_EOL,	/* EOL while waiting for char after \ */
		SS_DQ_BS_ERR_EOL,	/* EOL while waiting for char after \ */
		SS_DQ_BS_ERR_BAD,	/* bad escape */
		SS_SQ_NC_ERR,		/* no closing single quote */
		SS_DQ_NC_ERR,		/* no closing double quote */
		SS_DQ_BS_ERR_OCT_BAD,	/* bad octal escape */
		SS_DQ_BS_ERR_HEX_BAD,	/* bad hex escape */
		SS_DQ_BS_ERR_C_BAD,	/* bad control char */
		SS_DONE			/* final state */
	};
	const char *s, *e;
	enum split_state ss;
	int c, w, val, tmp, outc, i;
	size_t arg_alloc = strlen(str) + 1, tmplen;
	char *arg = NULL, *args, *arge, *tmparg;
	int argv_alloc = 64, argv_count = 0;
	char **argv = NULL, **tmpargv;
	void *mem;

	if (!str || !argcp || !argvp)
		return NULL;

	/* the temporary is guaranteed to fit one split */
	arg = alloca(arg_alloc);
	args = arg;
	arge = arg + arg_alloc - 1;

	argv = alloca(sizeof(*argv) * argv_alloc);

	e = str + strlen(str);

#undef OUTC
#define OUTC(_c) \
	do { \
		assert(args < arge); \
		args = fy_utf8_put(args, arge - args, (_c)); \
		assert(args); \
	} while(0)

#undef NEW_ARGV
#define NEW_ARGV() \
	do { \
		if (args > arg) { \
			if (argv_count + 1 >= argv_alloc) { \
				argv_alloc *= 2; \
				tmpargv = alloca((sizeof(*tmpargv) * argv_alloc)); \
				memcpy(tmpargv, argv, argv_count * sizeof(*tmpargv)); \
			} \
			tmplen = args - arg; \
			tmparg = alloca(tmplen + 1); \
			memcpy(tmparg, arg, tmplen); \
			tmparg[tmplen] = '\0'; \
			argv[argv_count] = tmparg; \
			argv[++argv_count] = NULL; \
		} \
		args = arg; \
	} while(0)

#define GOTO(_ss) \
	do { \
		ss = (_ss); \
	} while(0)


	ss = SS_WS;
	s = str;
	val = 0;
	while (!(ss >= SS_WS_BS_ERR_EOL && ss <= SS_DONE)) {
		c = fy_utf8_get(s, e - s, &w);
		switch (ss) {
		case SS_WS:
			if (c < 0) {
				NEW_ARGV();
				GOTO(SS_DONE);
				break;
			}
			switch (c) {
			case ' ':
			case '\t':
				break;	/* no change */
			case '\\':
				GOTO(SS_WS_BS);
				break;
			case '"':
				GOTO(SS_DQ);	/* start double quoted */
				NEW_ARGV();
				break;
			case '\'':
				GOTO(SS_SQ);	/* start single quoted */
				NEW_ARGV();
				break;
			default:
				GOTO(SS_UQ);	/* start unquoted */
				NEW_ARGV();
				OUTC(c);
				break;
			}
			break;

		case SS_WS_BS:
			if (c < 0) {
				GOTO(SS_WS_BS_ERR_EOL);
				break;
			}
			if (c == '\n') {	/* backslash newline, continuation */
				GOTO(SS_WS);
				break;
			}
			GOTO(SS_UQ);
			NEW_ARGV();
			OUTC(c);
			break;

		case SS_UQ:
			if (c <= 0) {
				NEW_ARGV();
				GOTO(SS_DONE);
				break;
			}

			switch (c) {
			case ' ':
			case '\t':
				GOTO(SS_WS);
				NEW_ARGV();
				break;	/* no change */
			case '\\':
				GOTO(SS_UQ_BS);
				break;
			case '"':
				GOTO(SS_DQ);	/* double quoted */
				break;
			case '\'':
				GOTO(SS_SQ);	/* single quoted */
				break;
			default:
				OUTC(c);
				break;
			}
			break;

		case SS_UQ_BS:
			if (c < 0) {
				GOTO(SS_UQ_BS_ERR_EOL);
				break;
			}
			if (c == '\n') {	/* backslash new line only */
				GOTO(SS_UQ);
				break;
			}
			GOTO(SS_UQ);
			OUTC(c);
			break;

		case SS_SQ:
			if (c < 0) {
				GOTO(SS_SQ_NC_ERR);	/* no closing ' */
				break;
			}
			switch (c) {
			case '\'':		/* end of quote, back to unquoted */
				GOTO(SS_UQ);
				break;
			case '\\':
				OUTC(c);
				GOTO(SS_SQ_BS);	/* backslash */
				break;
			default:
				OUTC(c);
				break;
			}
			break;

		case SS_SQ_BS:
			if (c < 0) {
				GOTO(SS_SQ_BS_ERR_EOL);
				break;
			}
			if (c == '\n') {	/* backslash new line only */
				GOTO(SS_SQ);	/* back to single quoted */
				break;
			}
			GOTO(SS_UQ);		/* always back to */
			OUTC(c);
			break;

		case SS_DQ:
			if (c < 0) {
				GOTO(SS_DQ_NC_ERR);	/* no closing ' */
				break;
			}
			switch (c) {
			case '"':		/* end of quote, back to unquoted */
				GOTO(SS_UQ);
				break;
			case '\\':
				GOTO(SS_DQ_BS);	/* backslash */
				break;
			default:
				OUTC(c);
				break;
			}
			break;

		case SS_DQ_BS:
			if (c < 0) {
				GOTO(SS_DQ_BS_ERR_EOL);
				break;
			}
			outc = -1;
			switch (c) {
			case 'a':
				outc = '\a';
				break;
			case 'b':
				outc = '\b';
				break;
			case 'e':
				outc = '\x1b';	/* escape */
				break;
			case 'n':
				outc = '\n';
				break;
			case 'r':
				outc = '\r';
				break;
			case 't':
				outc = '\t';
				break;
			case 'v':
				outc = '\v';
				break;
			case '\\':
				outc = '\\';
				break;
			case '\'':
				outc = '\'';
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
				val = c - '0';
				GOTO(SS_DQ_BS_OCT1);
				break;
			case 'x':
				val = 0;
				GOTO(SS_DQ_BS_HEX0);
				break;
			case 'c':
				val = 0;
				GOTO(SS_DQ_BS_C);
				break;
			default:
				GOTO(SS_DQ_BS_ERR_BAD);	/* unknown escape */
				break;
			}
			if (outc > 0) {
				OUTC(outc);
				GOTO(SS_DQ);
			}
			break;

		case SS_DQ_BS_OCT1:
		case SS_DQ_BS_OCT2:
			if (c < 0) {
				OUTC(val);
				NEW_ARGV();
				GOTO(SS_DONE);
				break;
			}
			if (!(c >= '0' && c <= '7')) {
				OUTC(val);
				GOTO(SS_DQ);
				w = 0;	/* redo, same */
				break;
			}
			val *= 8;
			val += c - '0';
			if (ss == SS_DQ_BS_OCT2) {
				OUTC(val);
				GOTO(SS_DQ);
			} else
				GOTO(SS_DQ_BS_OCT2);
			break;

		case SS_DQ_BS_HEX0:
		case SS_DQ_BS_HEX1:
		case SS_DQ_BS_HEX2:
			tmp = -1;
			if (c >= '0' && c <= '9')
				tmp = c - '0';
			else if (c >= 'a' && c <= 'f')
				tmp = 10 + c - 'a';
			else if (c >= 'A' && c <= 'F')
				tmp = 10 + c - 'A';

			switch (ss) {
			case SS_DQ_BS_HEX0:
				if (tmp < 0) {
					fprintf(stderr, "tmp=%d c=%c\n", tmp, c);
					GOTO(SS_DQ_BS_ERR_HEX_BAD);
					break;
				}
				val = tmp;
				GOTO(SS_DQ_BS_HEX1);
				break;

			case SS_DQ_BS_HEX1:
			case SS_DQ_BS_HEX2:
				if (tmp < 0) {
					GOTO(SS_DQ);
					OUTC(val);
					w = 0;	/* redo */
					break;
				}
				val *= 16;
				val += tmp;
				if (ss == SS_DQ_BS_HEX1)
					GOTO(SS_DQ_BS_HEX2);
				else {
					OUTC(val);
					GOTO(SS_DQ);
				}
				break;

			default:
				FY_IMPOSSIBLE_ABORT();
			}
			break;

		case SS_DQ_BS_C:
			outc = -1;
			if (c >= 'a' && c <= 'z')
				outc = c - 'a' + 1;
			else if (c >= 'A' && c <= 'Z')
				outc = c - 'Z' + 1;
			if (outc > 0x20) {
				outc = -1;
				GOTO(SS_DQ_BS_ERR_C_BAD);
			} else
				GOTO(SS_DQ);
			if (outc > 0)
				OUTC(outc);
			break;
		default:
			FY_IMPOSSIBLE_ABORT();
		}
		s += w;
	}

	/* someething went wrong */
	if (ss != SS_DONE)
		return NULL;

	tmplen = (argv_count + 1) * sizeof(*argv);
	for (i = 0; i < argv_count; i++)
		tmplen += strlen(argv[i]) + 1;

	mem = malloc(tmplen);
	if (!mem)
		return NULL;

	tmpargv = mem;
	tmparg = (char *)mem + (argv_count + 1) * sizeof(*tmpargv);
	for (i = 0; i < argv_count; i++) {
		tmpargv[i] = tmparg;
		strcpy(tmparg, argv[i]);
		tmparg = tmparg + strlen(argv[i]) + 1;
	}
	tmpargv[i] = NULL;

	*argcp = i;
	*argvp = mem;

	return mem;
}

int fy_utf8_get_generic_s(const void *ptr, const void *ptr_end, int *widthp)
{
	const uint8_t *p = ptr;
	int i, width, value;

	if (ptr >= ptr_end)
		return FYUG_EOF;

	/* this is the slow path */
	width = fy_utf8_width_by_first_octet(p[0]);
	if (!width)
		return FYUG_INV;
	if ((const char *)ptr + width > (const char *)ptr_end)
		return FYUG_PARTIAL;

	/* initial value */
	value = *p++ & (0xff >> width);
	for (i = 1; i < width; i++) {
		if ((*p & 0xc0) != 0x80)
			return FYUG_INV;
		value = (value << 6) | (*p++ & 0x3f);
	}

	/* check for validity */
	if ((width == 4 && value < 0x10000) ||
	    (width == 3 && value <   0x800) ||
	    (width == 2 && value <    0x80) ||
	    (value >= 0xd800 && value <= 0xdfff) || value >= 0x110000)
		return FYUG_INV;

	*widthp = width;

	return value;
}

int fy_utf8_get_generic_s_nocheck(const void *ptr, int *widthp)
{
	const uint8_t *p = ptr;
	int i, width, value;

	/* this is the slow path */
	width = fy_utf8_width_by_first_octet(p[0]);
	if (!width)
		return FYUG_INV;

	/* initial value */
	value = *p++ & (0xff >> width);
	for (i = 1; i < width; i++) {
		if ((*p & 0xc0) != 0x80)
			return FYUG_INV;
		value = (value << 6) | (*p++ & 0x3f);
	}

	/* check for validity */
	if ((width == 4 && value < 0x10000) ||
	    (width == 3 && value <   0x800) ||
	    (width == 2 && value <    0x80) ||
	    (value >= 0xd800 && value <= 0xdfff) || value >= 0x110000)
		return FYUG_INV;

	*widthp = width;

	return value;
}
