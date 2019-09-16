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

#include <libfyaml.h>

#include "fy-utf8.h"

int fy_utf8_get_generic(const void *ptr, int left, int *widthp)
{
	const uint8_t *p = ptr;
	int i, width, value;

	if (left < 1)
		return -1;

	/* this is the slow path */
	width = fy_utf8_width_by_first_octet(p[0]);
	if (!width || width > left)
		return -1;

	/* initial value */
	value = *p++ & (0x7f >> width);
	for (i = 1; i < width; i++) {
		if ((*p & 0xc0) != 0x80)
			return -1;
		value = (value << 6) | (*p++ & 0x3f);
	}

	/* check for validity */
	if ((width == 4 && value < 0x10000) ||
	    (width == 3 && value <   0x800) ||
	    (width == 2 && value <    0x80) ||
	    (value >= 0xd800 && value <= 0xdfff) || value >= 0x110000)
		return -1;

	*widthp = width;

	return value;
}

int fy_utf8_get_right_generic(const void *ptr, int left, int *widthp)
{
	const uint8_t *p, *s, *e;

	if (left < 1)
		return -1;

	s = ptr;
	e =  s + left;
	p = e - 1;
	while (p >= s && (e - p) <= 4) {
		if ((*p & 0xc0) != 0x80)
			return fy_utf8_get(p, e - p, widthp);
		p--;
	}

	return -1;
}

char *fy_utf8_format(int c, char *buf, enum fy_utf8_escape esc)
{
	char *s;

	if (!fy_utf8_is_valid(c)) {
		*buf = '\0';
		return buf;
	}

	s = buf;
	if (esc != fyue_none) {
		if (c == '\\') {
			*s++ = '\\';
			*s++ = '\\';
		} else if (c == '\0') {
			*s++ = '\\';
			*s++ = '0';
		} else if (c == '\b') {
			*s++ = '\\';
			*s++ = 'b';
		} else if (c == '\r') {
			*s++ = '\\';
			*s++ = 'r';
		} else if (c == '\t') {
			*s++ = '\\';
			*s++ = 't';
		} else if (c == '\n') {
			*s++ = '\\';
			*s++ = 'n';
		} else if (esc == fyue_singlequote && c == '\'') {
			*s++ = '\\';
			*s++ = '\'';
		} else if (esc == fyue_doublequote && c == '"') {
			*s++ = '\\';
			*s++ = '"';
		}
		/* procesed an escape earlier? */
		if (s > buf) {
			*s = '\0';
			return buf;
		}
	}
	s = fy_utf8_put_unchecked(s, c);
	*s = '\0';
	return buf;
}

int fy_utf8_format_text_length(const char *buf, size_t len,
			       enum fy_utf8_escape esc)
{
	int c, w, l;
	const char *s, *e;

	s = buf;
	e = buf + len;
	l = 0;
	while (s < e) {
		c = fy_utf8_get(s, e - s, &w);
		if (!w || c < 0)
			break;
		s += w;
		if (esc != fyue_none &&
			(c == '\\' || c == '\0' || c == '\b' || c == '\r' || c == '\t' || c == '\n' ||
			 (esc == fyue_singlequote && c == '\'') ||
			 (esc == fyue_doublequote && c == '"')) )
			l += 2;
		else
			l += w;
	}
	return l + 1;
}

char *fy_utf8_format_text(const char *buf, size_t len,
			  char *out, size_t maxsz,
			  enum fy_utf8_escape esc)
{
	int c, w;
	const char *s, *e;
	char *os, *oe, *oss;

	s = buf;
	e = buf + len;
	os = out;
	oe = out + maxsz - 1;
	while (s < e) {
		c = fy_utf8_get(s, e - s, &w);
		if (!w || c < 0)
			break;
		s += w;

		oss = os;
		if (esc != fyue_none) {
			if (c == '\\') {
				if (os + 2 > oe)
					break;
				*os++ = '\\';
				*os++ = '\\';
			} else if (c == '\0') {
				if (os + 2 > oe)
					break;
				*os++ = '\\';
				*os++ = '0';
			} else if (c == '\b') {
				if (os + 2 > oe)
					break;
				*os++ = '\\';
				*os++ = 'b';
			} else if (c == '\r') {
				if (os + 2 > oe)
					break;
				*os++ = '\\';
				*os++ = 'r';
			} else if (c == '\t') {
				if (os + 2 > oe)
					break;
				*os++ = '\\';
				*os++ = 't';
			} else if (c == '\n') {
				if (os + 2 > oe)
					break;
				*os++ = '\\';
				*os++ = 'n';
			} else if (esc == fyue_singlequote && c == '\'') {
				if (os + 2 > oe)
					break;
				*os++ = '\\';
				*os++ = '\'';
			} else if (esc == fyue_doublequote && c == '"') {
				if (os + 2 > oe)
					break;
				*os++ = '\\';
				*os++ = '"';
			}
		}
		if (os > oss)
			continue;

		if (os + w > oe)
			break;

		os = fy_utf8_put_unchecked(os, c);
	}
	*os++ = '\0';
	return out;
}

const void *fy_utf8_memchr_generic(const void *s, int c, size_t n)
{
	int cc, w;
	const void *e;

	e = s + n;
	while (s < e && (cc = fy_utf8_get(s, e - s, &w)) >= 0) {
		if (c == cc)
			return s;
		s += w;
	}

	return NULL;
}
