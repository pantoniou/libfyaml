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
#include "fy-tool-reflect.h"

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

int parse_integer_scalar(enum fy_type_kind type_kind, const char *str,
			 enum fy_parser_mode mode, union integer_scalar *nump)
{
	static const strtox_intmax_func func_intmax[fypm_json + 1] = {
		[fypm_yaml_1_1] = str_to_intmax_1_1,
		[fypm_yaml_1_2] = str_to_intmax,
		[fypm_yaml_1_3] = str_to_intmax,
		[fypm_json] = str_to_intmax_json,
	};
	static const strtox_uintmax_func func_uintmax[fypm_json + 1] = {
		[fypm_yaml_1_1] = str_to_uintmax_1_1,
		[fypm_yaml_1_2] = str_to_uintmax,
		[fypm_yaml_1_3] = str_to_uintmax,
		[fypm_json] = str_to_uintmax_json,
	};
	int rc;

	if (fy_type_kind_is_signed(type_kind)) {
		assert((size_t)mode <= ARRAY_SIZE(func_intmax));
		assert(func_intmax[mode]);
		rc = func_intmax[mode](str, &nump->sval);
		if (rc)
			return rc;
		if ((nump->sval > 0 && nump->sval > signed_integer_max(type_kind)) ||
		    (nump->sval < 0 && nump->sval < signed_integer_min(type_kind)))
			return -ERANGE;
	} else {
		assert((size_t)mode <= ARRAY_SIZE(func_uintmax));
		assert(func_uintmax[mode]);
		rc = func_uintmax[mode](str, &nump->uval);
		if (rc)
			return rc;
		if (nump->uval > unsigned_integer_max(type_kind))
			return -ERANGE;
	}

	return 0;
}

static void
store_signed_integer(enum fy_type_kind type_kind, void *data, intmax_t sval)
{
	switch (type_kind) {
	case FYTK_CHAR:
		*(char *)data = (char)sval;
		break;
	case FYTK_SCHAR:
		*(signed char *)data = (signed char)sval;
		break;
	case FYTK_SHORT:
		*(short *)data = (short)sval;
		break;
	case FYTK_INT:
		*(int *)data = (int)sval;
		break;
	case FYTK_LONG:
		*(long *)data = (long)sval;
		break;
	case FYTK_LONGLONG:
		*(long long *)data = (long long)sval;
		break;
	default:
		abort();
		break;
	}
}

static void
store_unsigned_integer(enum fy_type_kind type_kind, void *data, uintmax_t uval)
{
	switch (type_kind) {
	case FYTK_CHAR:
		*(char *)data = (char)uval;	/* NOTE: on some platforms this is unsigned */
		break;
	case FYTK_UCHAR:
		*(unsigned char *)data = (unsigned char)uval;
		break;
	case FYTK_USHORT:
		*(unsigned short *)data = (unsigned short)uval;
		break;
	case FYTK_UINT:
		*(unsigned int *)data = (unsigned int)uval;
		break;
	case FYTK_ULONG:
		*(unsigned long *)data = (unsigned long)uval;
		break;
	case FYTK_ULONGLONG:
		*(unsigned long long *)data = (unsigned long long)uval;
		break;
	default:
		abort();
		break;
	}
}

void
store_integer_scalar(enum fy_type_kind type_kind, void *data, union integer_scalar num)
{
	assert(fy_type_kind_is_integer(type_kind));

	/* don't handle anything wide */
	assert(fy_type_kind_size(type_kind) <= sizeof(uintmax_t));

	return fy_type_kind_is_signed(type_kind) ?
		store_signed_integer(type_kind, data, num.sval) :
		store_unsigned_integer(type_kind, data, num.uval);
}

static intmax_t
load_signed_integer(enum fy_type_kind type_kind, const void *data)
{
	switch (type_kind) {
	case FYTK_CHAR:
		return *(char *)data;
	case FYTK_SCHAR:
		return *(signed char *)data;
	case FYTK_SHORT:
		return *(short *)data;
	case FYTK_INT:
		return *(int *)data;
	case FYTK_LONG:
		return *(long *)data;
	case FYTK_LONGLONG:
		return *(long long *)data;
	default:
		abort();
	}
	return 0;
}

static uintmax_t
load_unsigned_integer(enum fy_type_kind type_kind, const void *data)
{
	switch (type_kind) {
	case FYTK_CHAR:
		return *(char *)data;
	case FYTK_UCHAR:
		return *(unsigned char *)data;
	case FYTK_USHORT:
		return *(unsigned short *)data;
	case FYTK_UINT:
		return *(unsigned int *)data;
	case FYTK_ULONG:
		return *(unsigned long *)data;
	case FYTK_ULONGLONG:
		return *(unsigned long long *)data;
	default:
		abort();
	}
	return 0;
}

union integer_scalar
load_integer_scalar(enum fy_type_kind type_kind, const void *data)
{
	union integer_scalar num;

	assert(fy_type_kind_is_integer(type_kind));

	/* don't handle anything wide */
	assert(fy_type_kind_size(type_kind) <= sizeof(uintmax_t));

	if (fy_type_kind_is_signed(type_kind))
		num.sval = load_signed_integer(type_kind, data);
	else
		num.uval = load_unsigned_integer(type_kind, data);
	return num;
}

int
parse_float_scalar(enum fy_type_kind type_kind, const char *text0,
		   enum fy_parser_mode mode, union float_scalar *numf)
{
	enum {
		pft_normal,
		pft_plus_inf,
		pft_minus_inf,
		pft_nan,
	} parse_float_type;
	static const char *plus_inf_values[] = {
		".inf", ".Inf", ".INF",
		"+.inf", "+.Inf", "+.INF", NULL
	};
	static const char *minus_inf_values[] = {
		"-.inf", "-.Inf", "-.INF", NULL
	};
	static const char *nan_values[] = {
		".nan", ".NaN", ".NAN", NULL
	};
	char *endptr;
	int errno_value;

	assert(fy_type_kind_is_float(type_kind));

	if (parse_match_value(text0, plus_inf_values) >= 0)
		parse_float_type = pft_plus_inf;
	else if (parse_match_value(text0, minus_inf_values) >= 0)
		parse_float_type = pft_minus_inf;
	else if (parse_match_value(text0, nan_values) >= 0)
		parse_float_type = pft_nan;
	else
		parse_float_type = pft_normal;

	errno = 0;
	switch (type_kind) {

	case FYTK_FLOAT:
		switch (parse_float_type) {
		case pft_plus_inf:
			numf->f = INFINITY;
			break;
		case pft_minus_inf:
			numf->f = -INFINITY;
			break;
		case pft_nan:
			numf->f = NAN;
			break;
		default:
			numf->f = strtof(text0, &endptr);
			break;
		}
		break;

	case FYTK_DOUBLE:
		switch (parse_float_type) {
		case pft_plus_inf:
			numf->d = INFINITY;
			break;
		case pft_minus_inf:
			numf->d = -INFINITY;
			break;
		case pft_nan:
			numf->d = NAN;
			break;
		default:
			numf->d = strtod(text0, &endptr);
			break;
		}
		break;

	case FYTK_LONGDOUBLE:
		switch (parse_float_type) {
		case pft_plus_inf:
			numf->ld = INFINITY;
			break;
		case pft_minus_inf:
			numf->ld = -INFINITY;
			break;
		case pft_nan:
			numf->ld = NAN;
			break;
		default:
			numf->ld = strtold(text0, &endptr);
			break;
		}
		break;

	default:
		abort();
	}
	errno_value = errno;
	errno = 0;

	if (errno_value == ERANGE)
		return -ERANGE;

	if (parse_float_type == pft_normal && *endptr != '\0')
		return -EINVAL;

	return 0;
}

void store_float_scalar(enum fy_type_kind type_kind, void *data, union float_scalar numf)
{
	assert(fy_type_kind_is_float(type_kind));

	switch (type_kind) {
	case FYTK_FLOAT:
		*(float *)data = numf.f;
		break;
	case FYTK_DOUBLE:
		*(double *)data = numf.d;
		break;
	case FYTK_LONGDOUBLE:
		*(long double *)data = numf.ld;
		break;
	default:
		break;
	}
}

union float_scalar
load_float_scalar(enum fy_type_kind type_kind, const void *data)
{
	union float_scalar numf;

	assert(fy_type_kind_is_float(type_kind));

	switch (type_kind) {
	case FYTK_FLOAT:
		numf.f = *(const float *)data;
		break;
	case FYTK_DOUBLE:
		numf.d = *(const double *)data;
		break;
	case FYTK_LONGDOUBLE:
		numf.ld = *(const long double *)data;
		break;
	default:
		numf.ld = 0;
		break;
	}
	return numf;
}

int
parse_boolean_scalar(const char *text0, enum fy_parser_mode mode, bool *vp)
{
	static const char *true_values[] = {
		"true", "True", "TRUE", NULL
	};
	static const char *true_values_1_1[] = {
		"y", "Y",
		"yes", "Yes", "YES",
		"true", "True", "TRUE",
		"on", "On", "ON", NULL
	};
	static const char *true_values_json[] = {
		"true", NULL
	};
	static const char *false_values[] = {
		"false", "False", "FALSE", NULL
	};
	static const char *false_values_1_1[] = {
		"n", "N",
		"no", "No", "NO",
		"false", "False", "FALSE",
		"off", "Off", "OFF", NULL
	};
	static const char *false_values_json[] = {
		"false", NULL
	};
	static const char **mode_true_values[fypm_json + 1] = {
		[fypm_yaml_1_1] = true_values_1_1,
		[fypm_yaml_1_2] = true_values,
		[fypm_yaml_1_3] = true_values,
		[fypm_json] = true_values_json,
	};
	static const char **mode_false_values[fypm_json + 1] = {
		[fypm_yaml_1_1] = false_values_1_1,
		[fypm_yaml_1_2] = false_values,
		[fypm_yaml_1_3] = false_values,
		[fypm_json] = false_values_json,
	};
	int match;

	assert((size_t)mode <= ARRAY_SIZE(mode_true_values));
	match = parse_match_value(text0, mode_true_values[mode]);
	if (match >= 0) {
		*vp = true;
		return 0;
	}

	assert((size_t)mode <= ARRAY_SIZE(mode_false_values));
	match = parse_match_value(text0, mode_false_values[mode]);
	if (match >= 0) {
		*vp = false;
		return 0;
	}

	return -EINVAL;
}

void
store_boolean_scalar(enum fy_type_kind type_kind, void *data, bool v)
{
	assert(type_kind == FYTK_BOOL);
	*(bool *)data = v;
}

bool
load_boolean_scalar(enum fy_type_kind type_kind, const void *data)
{
	assert(type_kind == FYTK_BOOL);
	return *(const _Bool *)data;
}

int
parse_null_scalar(const char *text0, enum fy_parser_mode mode)
{
	static const char *null_values[] = {
		"~", "null", "Null", "NULL", NULL
	};
	static const char *null_values_json[] = {
		"null", NULL
	};
	static const char **mode_null_values[fypm_json + 1] = {
		[fypm_yaml_1_1] = null_values,
		[fypm_yaml_1_2] = null_values,
		[fypm_yaml_1_3] = null_values,
		[fypm_json] = null_values_json,
	};
	int match;

	assert((size_t)mode <= ARRAY_SIZE(mode_null_values));
	match = parse_match_value(text0, mode_null_values[mode]);
	if (match >= 0)
		return 0;

	return -EINVAL;
}

static void comment_dump(int level, const char *comment)
{
	size_t len;
	const char *s, *e, *le;

	if (!comment)
		return;

	len = strlen(comment);
	s = comment;
	e = s + len;
	while (s < e) {
		le = strchr(s, '\n');
		len = le ? (size_t)(le - s) : strlen(s);
		printf("%*s// %.*s\n", (int)(level * 4), "", (int)len, s);
		s += len + 1;
	}
}

void type_info_dump(const struct fy_type_info *ti, int level)
{
	const struct fy_field_info *fi;
	size_t i;

	comment_dump(level, fy_type_info_get_yaml_comment(ti));
	printf("'%s'", ti->name);
	printf("%s%s%s%s%s%s%s%s%s%s%s%s",
			(ti->flags & FYTIF_CONST) ? " CONST" : "",
			(ti->flags & FYTIF_VOLATILE) ? " VOLATILE" : "",
			(ti->flags & FYTIF_RESTRICT) ? " RESTRICT" : "",
			(ti->flags & FYTIF_UNRESOLVED) ? " UNRESOLVED" : "",
			(ti->flags & FYTIF_MAIN_FILE) ? " MAIN_FILE" : "",
			(ti->flags & FYTIF_SYSTEM_HEADER) ? " SYSTEM_HEADER" : "",
			(ti->flags & FYTIF_ANONYMOUS) ? " ANONYMOUS" : "",
			(ti->flags & FYTIF_ANONYMOUS_RECORD_DECL) ? " ANONYMOUS_RECORD_DECL" : "",
			(ti->flags & FYTIF_ANONYMOUS_DEP) ? " ANONYMOUS_DEP" : "",
			(ti->flags & FYTIF_INCOMPLETE) ? " INCOMPLETE" : "",
			(ti->flags & FYTIF_ELABORATED) ? " ELABORATED" : "",
			(ti->flags & FYTIF_ANONYMOUS_GLOBAL) ? " ANONYMOUS_GLOBAL" : "");
	printf(" size=%zu align=%zu", ti->size, ti->align);
	if (ti->dependent_type)
		printf(" -> %s", ti->dependent_type->name);
	printf("\n");

	if (fy_type_kind_has_fields(ti->kind)) {
		for (i = 0, fi = ti->fields; i < ti->count; i++, fi++) {
			comment_dump(level + 1, fy_field_info_get_yaml_comment(fi));
			printf("%*s%s %s", (level + 1) * 4, "", fi->type_info->name, fi->name);
			printf("%s%s",
					(fi->flags & FYFIF_BITFIELD) ? " BITFIELD" : "",
					(fi->flags & FYFIF_ENUM_UNSIGNED) ? " ENUM_UNSIGNED" : "");
			if (ti->kind == FYTK_ENUM) {
				if (fi->flags & FYFIF_ENUM_UNSIGNED)
					printf(" value=%ju", fi->uval);
				else
					printf(" value=%jd", fi->sval);
			} else if (!(fi->flags & FYFIF_BITFIELD))
				printf(" offset=%zu", fi->offset);
			else
				printf(" bit_offset=%zu bit_width=%zu", fi->bit_offset, fi->bit_width);
			printf("\n");
		}
	}
}

/* reflection walker util */
union integer_scalar
load_integer_scalar_rw(struct reflection_walker *rw)
{
	union integer_scalar numi;

	assert(rw);
	assert(rw->rtd);

	if (!(rw->flags & RWF_BITFIELD_DATA))
		numi = load_integer_scalar(rtd_kind(rw->rtd), rw->data);
	else
		numi.uval = load_bitfield_le(rw->data, rw->data_size.bit_offset,
				rw->data_size.bit_width, rtd_is_signed(rw->rtd));
	return numi;
}

int
store_integer_scalar_check_rw(struct reflection_walker *rw,
			      union integer_scalar numi,
			      union integer_scalar *minip, union integer_scalar *maxip)
{
	size_t bit_width;
	union integer_scalar mini, maxi;
	int rc;

	if (!(rw->flags & RWF_BITFIELD_DATA))
		bit_width = rtd_size(rw->rtd) << 3;
	else
		bit_width = rw->data_size.bit_width;

	rc = 0;
	if (rtd_is_signed(rw->rtd)) {

		mini.sval = signed_integer_min_from_bit_width(bit_width);
		maxi.sval = signed_integer_max_from_bit_width(bit_width);

		if (numi.sval < mini.sval || numi.sval > maxi.sval)
			rc = -ERANGE;

		if (minip)
			*minip = mini;
		if (maxip)
			*maxip = maxi;

	} else if (rtd_is_unsigned(rw->rtd)) {

		maxi.uval = unsigned_integer_max_from_bit_width(bit_width);

		if (numi.uval > maxi.uval)
			rc = -ERANGE;

		if (minip)
			minip->uval = 0;
		if (maxip)
			*maxip = maxi;
	} else
		return -EINVAL;

	return rc;
}

void
store_integer_scalar_no_check_rw(struct reflection_walker *rw, union integer_scalar num)
{
	assert(rw);
	assert(rw->rtd);

	if (!(rw->flags & RWF_BITFIELD_DATA))
		store_integer_scalar(rtd_kind(rw->rtd), rw->data, num);
	else
		store_bitfield_le(rw->data, rw->data_size.bit_offset,
				rw->data_size.bit_width, num.uval);
}

int
store_integer_scalar_rw(struct reflection_walker *rw, union integer_scalar num)
{
	int rc;

	rc = store_integer_scalar_check_rw(rw, num, NULL, NULL);
	if (rc)
		return rc;

	store_integer_scalar_no_check_rw(rw, num);

	return 0;
}

void
store_boolean_scalar_rw(struct reflection_walker *rw, bool v)
{
	assert(rw);
	assert(rw->rtd);

	if (!(rw->flags & RWF_BITFIELD_DATA))
		store_boolean_scalar(rtd_kind(rw->rtd), rw->data, v);
	else
		store_bitfield_le(rw->data, rw->data_size.bit_offset,
				rw->data_size.bit_width, (uintmax_t)v);
}

bool
load_boolean_scalar_rw(struct reflection_walker *rw)
{
	bool v;

	assert(rw);
	assert(rw->rtd);

	if (!(rw->flags & RWF_BITFIELD_DATA))
		v = load_boolean_scalar(rtd_kind(rw->rtd), rw->data);
	else
		v = !!load_bitfield_le(rw->data, rw->data_size.bit_offset,
				rw->data_size.bit_width, rtd_is_signed(rw->rtd));
	return v;
}
