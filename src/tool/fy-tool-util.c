/*
 * fy-tool-util.c - tool utils
 *
 * Copyright (c) 2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <regex.h>
#include <stdalign.h>
#include <inttypes.h>
#include <float.h>
#include <limits.h>
#include <math.h>

#include <libfyaml.h>

#include "fy-tool-util.h"

uintmax_t load_le(const void *ptr, size_t width, bool is_signed)
{
	uintmax_t v;
	const uint8_t *p;
	size_t off;

	assert(width <= sizeof(uintmax_t));

	switch (width) {
	case sizeof(uint8_t):
		v = (uintmax_t)*(uint8_t *)ptr;
		break;
	case sizeof(uint16_t):
		v = (uintmax_t)*(uint16_t *)ptr;
		break;
	case sizeof(uint32_t):
		v = (uintmax_t)*(uint32_t *)ptr;
		break;
	case sizeof(uint64_t):
		v = (uintmax_t)*(uint64_t *)ptr;
		break;
	default:
		for (v = 0, p = ptr, off = 0; off < width; off++)
			v |= (uintmax_t)p[off] << off;
		break;
	}

	/* sign extension? */
	if (is_signed && width < sizeof(uintmax_t) && (v & ((uintmax_t)1 << (width * 8 - 1))))
		v |= (uintmax_t)-1 << (width * 8);

	return v;
}

void store_le(void *ptr, size_t width, uintmax_t v)
{
	uint8_t *p;
	size_t off;

	switch (width) {
	case sizeof(uint8_t):
		*(uint8_t *)ptr = (uint8_t)v;
		break;
	case sizeof(uint16_t):
		*(uint16_t *)ptr = (uint16_t)v;
		break;
	case sizeof(uint32_t):
		*(uint32_t *)ptr = (uint32_t)v;
		break;
	case sizeof(uint64_t):
		*(uint64_t *)ptr = (uint64_t)v;
		break;
	default:
		for (p = ptr, off = 0; off < width; off++)
			p[off] = (uint8_t)(v >> (8 * off));
		break;
	}
}

uintmax_t load_bitfield_le(const void *ptr, size_t bit_offset, size_t bit_width, bool is_signed)
{
	const uint8_t *p;
	size_t off, width, space, use;
	uint8_t bmask;
	uintmax_t v;

	v = 0;

	width = bit_width;
	p = ptr + bit_offset / 8;
	off = bit_offset & 7;
	if (off) {
		space = 8 - off;
		use = width > space ? space : width;

		bmask = (((uint8_t)1 << use) - 1) << off;
		width -= use;

		v = (*p++ & bmask) >> off;
		off = use;

		// fprintf(stderr, "%s: 0. [%02x] use=%zu v=%jx\n", __func__, p[-1] & 0xff, use, v);
	}
	while (width >= 8) {
		v |= (uintmax_t)*p++ << off;
		width -= 8;
		off += 8;
		// fprintf(stderr, "%s: 1. [%02x] v=%jx\n", __func__, p[-1] & 0xff, v);
	}
	if (width) {
		v |= (uintmax_t)(*p & ((1 << width) - 1)) << off;
		// fprintf(stderr, "%s: 2. [%02x] off=%zu v=%jx\n", __func__, p[0], off, v);
	}

	/* sign extension? */
	if (bit_width < sizeof(uintmax_t) * 8) {
		if (is_signed) {
			if (v & ((uintmax_t)1 << (bit_width - 1)))
				v |= (uintmax_t)-1 << bit_width;
		} else {
			v &= ~((uintmax_t)-1 << bit_width);
		}
	}

	return v;
}

void store_bitfield_le(void *ptr, size_t bit_offset, size_t bit_width, uintmax_t v)
{
	uint8_t *p;
	size_t off, width, space, use;
	uint8_t bmask;

	width = bit_width;
	p = ptr + bit_offset / 8;
	off = bit_offset & 7;
	if (off) {
		space = 8 - off;
		use = width > space ? space : width;

		bmask = (((uint8_t)1 << use) - 1) << off;

		*p = (*p & ~bmask) | ((uint8_t)(v << off) & bmask);
		p++;

		// fprintf(stderr, "%s: 0. [%02x] bmask=%02x off=%zu %02x v=%jx\n", __func__, p[-1] & 0xff, bmask, off, (uint8_t)(v << off) & 0xff, v);

		v >>= use;
		width -= use;
	}
	while (width >= 8) {
		*p++ = (uint8_t)v;

		// fprintf(stderr, "%s: 1. [%02x] v=%jx\n", __func__, p[-1] & 0xff, v);

		v >>= 8;
		width -= 8;
	}
	if (width) {
		bmask = (1 << width) - 1;
		*p = (*p & ~bmask) | ((uint8_t)v & bmask);

		// fprintf(stderr, "%s: 1. [%02x] v=%jx\n", __func__, p[0] & 0xff, v);
	}
}

#define STRTOXF_IS_UNSIGNED		BIT(0)
#define STRTOXF_IS_SIGNED		BIT(1)
#define STRTOXF_SKIP_UNDERSCORE		BIT(2)
#define STRTOXF_ALLOW_BASE2		BIT(3)
#define STRTOXF_ALLOW_BASE8		BIT(4)
#define STRTOXF_ALLOW_BASE16		BIT(5)
#define STRTOXF_SINGLE_ZERO		BIT(6)

#define STRTOXF_YAML			(STRTOXF_ALLOW_BASE8 | STRTOXF_ALLOW_BASE16)
#define STRTOXF_YAML_1_1		(STRTOXF_ALLOW_BASE2 | STRTOXF_ALLOW_BASE8 | \
					 STRTOXF_ALLOW_BASE16 | STRTOXF_SKIP_UNDERSCORE)
#define STRTOXF_JSON			(STRTOXF_SINGLE_ZERO)

#define STRTOX_DECLARE(_type, _typename, _flags) \
int str_to_ ## _typename (const char *str, _type *res) \
{ \
	int base, dv; \
	bool negative; \
	char c; \
	_type v; \
	\
	negative = false; \
	if (*str == '+' || *str == '-') { \
		negative = *str == '-'; \
		str++; \
		if (!((_flags) & STRTOXF_IS_SIGNED) && negative) \
			return -EINVAL; \
	} \
	base = 10; \
	if (*str == '0') { \
		if (((_flags) & STRTOXF_ALLOW_BASE16) && str[1] == 'x') { \
			str += 2; \
			base = 16; \
		} else if (((_flags) & STRTOXF_ALLOW_BASE8) && str[1] == 'o') { \
			str += 2; \
			base = 8; \
		} else if (((_flags) & STRTOXF_ALLOW_BASE2) && str[1] == 'b') { \
			str += 2; \
			base = 2; \
		} else if (((_flags) & STRTOXF_SINGLE_ZERO) && str[1] == '\0') \
			return -EINVAL; \
	} else if (*str == '\0') \
		return -EINVAL;	/* empty number without digits */ \
	v = 0; \
	while ((c = *str++) != '\0') { \
		if (((_flags) & STRTOXF_SKIP_UNDERSCORE) && c == '_') \
			continue; \
		dv = c >= '0' && c <= '9' ? (c - '0') : \
		     c >= 'a' && c <= 'z' ? (10 + c - 'a') : \
		     c >= 'A' && c <= 'Z' ? (10 + c - 'A') : -1; \
		if (dv < 0 || dv >= (int)base) \
			return -EINVAL; \
		if (MUL_OVERFLOW(v, (_type)base, &v) || \
		    ADD_OVERFLOW(v, (_type)(!negative ? dv : -dv), &v)) \
			return -ERANGE; \
	} \
	*res = v; \
	return 0; \
} \
struct __useless_struct_to_allow_semicolon

typedef int (*strtox_intmax_func)(const char *str, intmax_t *res);
typedef int (*strtox_uintmax_func)(const char *str, uintmax_t *res);

STRTOX_DECLARE(intmax_t, intmax, STRTOXF_IS_SIGNED | STRTOXF_YAML);
STRTOX_DECLARE(uintmax_t, uintmax, STRTOXF_IS_UNSIGNED | STRTOXF_YAML);

STRTOX_DECLARE(intmax_t, intmax_1_1, STRTOXF_IS_SIGNED | STRTOXF_YAML_1_1);
STRTOX_DECLARE(uintmax_t, uintmax_1_1, STRTOXF_IS_UNSIGNED | STRTOXF_YAML_1_1);

STRTOX_DECLARE(intmax_t, intmax_json, STRTOXF_IS_SIGNED | STRTOXF_JSON);
STRTOX_DECLARE(uintmax_t, uintmax_json, STRTOXF_IS_UNSIGNED | STRTOXF_JSON);
