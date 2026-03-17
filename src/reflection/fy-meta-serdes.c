/*
 * fy-meta-serdes.c - Reflection type serialization/deserialization implementation
 *                    (parse/emit, type setup, field operations, type system management)
 *
 * Copyright (c) 2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_REFLECTION

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdalign.h>
#include <inttypes.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>

#ifndef _WIN32
#include <unistd.h>
#include <regex.h>
#endif

#if HAVE_ALLOCA
#include <alloca.h>
#endif

#include <libfyaml.h>
#include <libfyaml/libfyaml-reflection.h>

#include "fy-reflection-private.h"
#include "fy-reflection-util.h"

#include "fy-reflect-meta.h"
#include "fy-type-meta.h"

void reflection_type_info_dump(struct fy_reflection *rfl)
{
	const struct fy_type_info *ti;
	void *prev = NULL;

	while ((ti = fy_type_info_iterate(rfl, &prev)) != NULL)
		type_info_dump(ti, 0);
}

void fy_reflection_prune_system(struct fy_reflection *rfl)
{
	const struct fy_type_info *ti;
	void *prev = NULL;

	fy_reflection_clear_all_markers(rfl);

	/* mark all non system and keep them */
	prev = NULL;
	while ((ti = fy_type_info_iterate(rfl, &prev)) != NULL) {
		if (ti->flags & FYTIF_SYSTEM_HEADER)
			continue;
		/* mark all non system structs, unions, enums and typedefs */
		if (fy_type_kind_has_fields(ti->kind) || ti->kind == FYTK_TYPEDEF)
			fy_type_info_mark(ti);
	}
	fy_reflection_prune_unmarked(rfl);
}

int fy_reflection_type_filter(struct fy_reflection *rfl,
		const char *type_include, const char *type_exclude)
{
#ifndef _WIN32
	const struct fy_type_info *ti;
	void *prev = NULL;
	regex_t type_include_reg, type_exclude_reg;
	bool type_include_reg_compiled = false, type_exclude_reg_compiled = false;
	bool include_match, exclude_match;
	int ret;

	if (!type_include && !type_exclude)
		return 0;

	if (type_include) {
		ret = regcomp(&type_include_reg, type_include, REG_EXTENDED | REG_NOSUB);
		RFL_ERROR_CHECK(!ret, "Bad type-include regexp '%s'\n", type_include);
		type_include_reg_compiled = true;
	}

	if (type_exclude) {
		ret = regcomp(&type_exclude_reg, type_exclude, REG_EXTENDED | REG_NOSUB);
		RFL_ERROR_CHECK(!ret, "Bad type-exclude regexp '%s'\n", type_exclude);
		type_exclude_reg_compiled = true;
	}

	fy_reflection_clear_all_markers(rfl);
	prev = NULL;
	while ((ti = fy_type_info_iterate(rfl, &prev)) != NULL) {
		if (type_include) {
			ret = regexec(&type_include_reg, ti->name, 0, NULL, 0);
			include_match = ret == 0;
		} else
			include_match = true;

		if (type_exclude) {
			ret = regexec(&type_include_reg, ti->name, 0, NULL, 0);
			exclude_match = ret == 0;
		} else
			exclude_match = false;

		if (include_match && !exclude_match)
			fy_type_info_mark(ti);
	}
	fy_reflection_prune_unmarked(rfl);

	ret = 0;
out:
	if (type_exclude_reg_compiled)
		regfree(&type_exclude_reg);

	if (type_include_reg_compiled)
		regfree(&type_include_reg);

	return ret;

err_out:
	ret = -1;
	goto out;
#else
	/* POSIX regex not available on Windows; type filtering not supported */
	(void)rfl;
	(void)type_include;
	(void)type_exclude;
	return 0;
#endif
}

static bool type_info_equal(const struct fy_type_info *ti_a, const struct fy_type_info *ti_b, bool recurse)
{
	const struct fy_field_info *fi_a, *fi_b;
	const char *comment_a, *comment_b;
	enum fy_type_info_flags tif_a, tif_b;
	size_t i;

	assert(ti_a);
	assert(ti_b);

	if (ti_a->kind != ti_b->kind)
		return false;

	/* do not compare with the bits about positioning */
	tif_a = ti_a->flags & ~(FYTIF_MAIN_FILE | FYTIF_SYSTEM_HEADER);
	tif_b = ti_b->flags & ~(FYTIF_MAIN_FILE | FYTIF_SYSTEM_HEADER);
	if (tif_a != tif_b)
		return false;

	/* names must match, if they're not anonymous */
	if (!(ti_a->flags & ti_b->flags & (FYTIF_ANONYMOUS | FYTIF_ANONYMOUS_DEP))) {
		if (strcmp(ti_a->name, ti_b->name))
			return false;
	}

	if (ti_a->size != ti_b->size)
		return false;

	if (ti_a->align != ti_b->align)
		return false;

	if (ti_a->count != ti_b->count)
		return false;

	if (ti_a->dependent_type || ti_b->dependent_type) {
		if (!ti_a->dependent_type || !ti_b->dependent_type)
			return false;

		/* recurse is single shot */
		if (recurse && !type_info_equal(ti_a->dependent_type, ti_b->dependent_type, false))
			return false;
	}

	comment_a = fy_type_info_get_yaml_comment(ti_a);
	if (!comment_a)
		comment_a = "";
	comment_b = fy_type_info_get_yaml_comment(ti_b);
	if (!comment_b)
		comment_b = "";

	if (strcmp(comment_a, comment_b))
		return false;

	if (fy_type_kind_has_fields(ti_a->kind)) {
		/* ti_a->count == ti_b->count here */
		for (i = 0, fi_a = ti_a->fields, fi_b = ti_b->fields; i < ti_a->count; i++, fi_a++, fi_b++) {

			if (fi_a->flags != fi_b->flags)
				return false;

			/* recurse is single shot */
			if (recurse && !type_info_equal(fi_a->type_info, fi_b->type_info, false))
				return false;

			if (!(fi_a->type_info->flags & fi_b->type_info->flags & FYTIF_ANONYMOUS)) {
				if (strcmp(fi_a->name, fi_b->name))
					return false;
			}

			if (ti_a->kind == FYTK_ENUM) {
				if (fi_a->uval != fi_b->uval)
					return false;
			} else if (!(fi_a->flags & FYFIF_BITFIELD)) {
				if (fi_a->offset != fi_b->offset)
					return false;
			} else {
				if (fi_a->bit_offset != fi_b->bit_offset)
					return false;
				if (fi_a->bit_width != fi_b->bit_width)
					return false;
			}

			comment_a = fy_field_info_get_yaml_comment(fi_a);
			if (!comment_a)
				comment_a = "";
			comment_b = fy_field_info_get_yaml_comment(fi_b);
			if (!comment_b)
				comment_b = "";

			if (strcmp(comment_a, comment_b))
				return false;
		}
	}

	return true;
}

bool fy_reflection_equal(struct fy_reflection *rfl_a, struct fy_reflection *rfl_b)
{
	const struct fy_type_info *ti_a, *ti_b;
	void *prev_a = NULL, *prev_b = NULL;

	for (;;) {
		ti_a = fy_type_info_iterate(rfl_a, &prev_a);
		ti_b = fy_type_info_iterate(rfl_b, &prev_b);
		if (!ti_a && !ti_b)	/* done */
			break;
		if (!ti_a || !ti_b)	/* premature end of either */
			return false;

		if (!type_info_equal(ti_a, ti_b, true))
			return false;
	}

	return true;
}

struct reflection_field_data *
reflection_type_data_lookup_field(struct reflection_type_data *rtd, const char *field)
{
	struct reflection_field_data *rfd;
	int i;

	if (!rtd || !field)
		return NULL;

	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];
		if (!strcmp(rfd->field_name, field))
			return rfd;
	}
	return NULL;
}

struct reflection_field_data *
reflection_type_data_lookup_next_anonymous_field(struct reflection_type_data *rtd, const char *field)
{
	struct reflection_field_data *rfd, *rfdt;
	int i;

	if (!rtd || !field)
		return NULL;

	/* lookup by name directly */
	rfdt = reflection_type_data_lookup_field(rtd, field);
	if (rfdt)
		return rfdt;

	/* test each anonymous field in sequence */
	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];
		if (!(rfd->fi->type_info->flags & FYTIF_ANONYMOUS_RECORD_DECL))
			continue;

		rfdt = reflection_type_data_lookup_next_anonymous_field(rfd_rtd(rfd), field);
		if (rfdt)
			return rfd;	/* next step */
	}

	return NULL;
}

struct reflection_field_data *
reflection_type_data_lookup_field_by_enum_value(struct reflection_type_data *rtd, intmax_t val)
{
	int idx;

	if (!rtd)
		return NULL;

	idx = fy_field_info_index(fy_type_info_lookup_field_by_enum_value(rtd->ti, val));
	if (idx < 0 || idx >= rtd->fields_count)
		return NULL;

	return rtd->fields[idx];
}

struct reflection_field_data *
reflection_type_data_lookup_field_by_unsigned_enum_value(struct reflection_type_data *rtd, uintmax_t val)
{
	int idx;

	if (!rtd)
		return NULL;

	idx = fy_field_info_index(fy_type_info_lookup_field_by_unsigned_enum_value(rtd->ti, val));
	if (idx < 0 || idx >= rtd->fields_count)
		return NULL;

	return rtd->fields[idx];
}

struct reflection_field_data *
reflection_type_data_lookup_field_by_scalar_enum_value(struct reflection_type_data *rtd,
		union integer_scalar val, bool is_signed)
{
	return is_signed ?
		reflection_type_data_lookup_field_by_enum_value(rtd, val.sval) :
		reflection_type_data_lookup_field_by_unsigned_enum_value(rtd, val.uval);
}

const struct reflection_type_ops reflection_ops_table[FYTK_COUNT];

struct reflection_field_data *
reflection_get_field(struct reflection_walker *rw,
		     struct reflection_walker **rw_fieldp,
		     struct reflection_walker **rw_basep)
{
	struct reflection_walker *rwf, *rws, *rwb;
	struct reflection_field_data *rfd;
	uintmax_t field_idx;

	if (rw_fieldp)
		*rw_fieldp = NULL;

	if (rw_basep)
		*rw_basep = NULL;

	rwf = rw;
	rws = rw->parent;
	rwb = rw;
	rfd = NULL;

	/* first find the base (when directly dependent) */
	while (rwf->parent && fy_type_kind_is_direct_dependent(rwf->parent->rtd->ti->kind)) {
		rwf = rwf->parent;
	}

	/* the base type */
	if (rwf->parent && !fy_type_kind_is_direct_dependent(rwf->parent->rtd->ti->kind))
		rwb = rwf->parent;

	rws = (rwf->parent && fy_type_kind_has_direct_fields(rwf->parent->rtd->ti->kind)) ? rwf->parent : NULL;

	/* NOTE this may happen when copying */
	if (!rws)
		return NULL;

	assert(rwf->flags & RWF_FIELD_IDX);
	field_idx = rwf->idx;

	RW_ASSERT(fy_type_kind_has_fields(rws->rtd->ti->kind));
	RW_ASSERT(field_idx < (uintmax_t)rws->rtd->fields_count);
	rfd = rws->rtd->fields[field_idx];
	RW_ASSERT(rfd);

	if (rw_fieldp)
		*rw_fieldp = rwf;
	if (rw_basep)
		*rw_basep = rwb;
	return rfd;

err_out:
	return NULL;
}

static int
common_scalar_parse(struct fy_parser *fyp, struct reflection_walker *rw,
		    enum reflection_parse_flags flags)
{
	struct fy_event *fye = NULL;
	struct fy_token *fyt;
	enum fy_scalar_style style;
	enum fy_type_kind type_kind;
	const char *text0;
	int rc = -1;

	assert(rw);
	assert(rw->rtd);
	assert(rw->data || (flags & RPF_NO_STORE));

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

	RP_INPUT_CHECK(fye->type == FYET_SCALAR,
		       "expecting FYET_SCALAR, got %s\n", fy_event_type_get_text(fye->type));

	type_kind = rw->rtd->ti->kind;
	fy_type_kind_is_valid(type_kind);

	fyt = fy_event_get_token(fye);
	style = fy_token_scalar_style(fyt);

	RP_INPUT_CHECK(fye->type == FYET_SCALAR, "invalid empty scalar\n");

	/* validate type */
	RP_ASSERT(fy_type_kind_is_integer(type_kind) ||
		  fy_type_kind_is_float(type_kind) || type_kind == FYTK_BOOL);

	/* intercept char */
	if (type_kind == FYTK_CHAR && (style == FYSS_SINGLE_QUOTED || style == FYSS_DOUBLE_QUOTED)) {
		const char *text;
		const uint8_t *p;
		size_t text_len;
		int i, c, w;
		union integer_scalar numi;

		text = fy_token_get_text(fyt, &text_len);
		RP_ASSERT(text != NULL);

		RP_INPUT_CHECK(text_len != 0, "character value must exist");

		p = (const uint8_t *)text;
		/* get width from the first octet */
		w = (p[0] & 0x80) == 0x00 ? 1 :
		    (p[0] & 0xe0) == 0xc0 ? 2 :
		    (p[0] & 0xf0) == 0xe0 ? 3 :
		    (p[0] & 0xf8) == 0xf0 ? 4 : 0;

		RP_INPUT_CHECK((size_t)w == text_len, "UTF8 text too big to fit in char");
		RP_INPUT_CHECK((size_t)w <= text_len, "malformed UTF8 text (start)");

		c = p[0] & (0xff >> w);
		for (i = 1; i < w; i++) {
			RP_INPUT_CHECK((p[i] & 0xc0) == 0x80, "malformed UTF8 text (middle)");
			c = (c << 6) | (p[i] & 0x3f);
		}

		/* check for validity */
		RP_INPUT_CHECK(!((w == 4 && c < 0x10000) ||
			         (w == 3 && c <   0x800) ||
				 (w == 2 && c <    0x80) ||
				 (c >= 0xd800 && c <= 0xdfff) || c >= 0x110000),
				"malformed UTF8 text (end)");

		RP_INPUT_CHECK(c >= 0, "malformed UTF8 text (negative value)");

		RP_INPUT_CHECK(c <= UCHAR_MAX, "UTF8 code %d does not fit (max %d)", c, UCHAR_MAX);

		if (!(flags & RPF_NO_STORE)) {
			numi.uval = (unsigned int)c;
			store_integer_scalar_no_check_rw(rw, numi);
		}

		rc = 0;
		goto out;
	}

	text0 = fy_token_get_text0(fyt);
	RP_ASSERT(text0 != NULL);

	if (fy_type_kind_is_integer(type_kind)) {
		union integer_scalar numi, mini, maxi;

		RP_INPUT_CHECK(style == FYSS_PLAIN, "only plain style allowed for integers");
		RP_INPUT_CHECK(*text0 != '\0', "integer must not be empty scalar");

		rc = parse_integer_scalar(type_kind, text0, fy_parser_get_mode(fyp), &numi);
		RP_INPUT_CHECK(rc != -ERANGE, "%s integer scalar value out of range",
				fy_type_kind_is_signed(type_kind) ? "signed" : "unsigned");
		RP_INPUT_CHECK(!rc, "malformed integer scalar value");

		rc = store_integer_scalar_check_rw(rw, numi, &mini, &maxi);
		RP_INPUT_CHECK(!(rc == -ERANGE && fy_type_kind_is_signed(type_kind)),
				"signed value %jd out of range (min=%jd, max=%jd)",
			       numi.sval, mini.sval, maxi.sval);
		RP_INPUT_CHECK(!(rc == -ERANGE && fy_type_kind_is_unsigned(type_kind)),
				"unsigned value %ju out of range (max=%ju)",
			       numi.sval, maxi.uval);
		RP_ASSERT(!rc);

		if (!(flags & RPF_NO_STORE))
			store_integer_scalar_no_check_rw(rw, numi);

	} else if (fy_type_kind_is_float(type_kind)) {

		union float_scalar numf;

		RP_INPUT_CHECK(style == FYSS_PLAIN,
			       "only plain style allowed for floating point numbers");
		RP_INPUT_CHECK(*text0 != '\0', "float must not be empty scalar");

		rc = parse_float_scalar(type_kind, text0, fy_parser_get_mode(fyp), &numf);
		RP_INPUT_CHECK(rc != -ERANGE, "float value out of range");
		RP_INPUT_CHECK(rc == 0, "malformed float value");

		if (!(flags & RPF_NO_STORE))
			store_float_scalar(type_kind, rw->data, numf);

	} else if (type_kind == FYTK_BOOL) {
		bool v;

		RP_INPUT_CHECK(style == FYSS_PLAIN, "only plain style allowed for booleans");
		RP_INPUT_CHECK(*text0 != '\0', "bool must not be empty scalar");

		rc = parse_boolean_scalar(text0, fy_parser_get_mode(fyp), &v);
		RP_INPUT_CHECK(rc == 0, "invalid boolean format");

		if (!(flags & RPF_NO_STORE))
			store_boolean_scalar_rw(rw, v);

	} else {
		/* should never get here */
		abort();
	}

	fy_parser_event_free(fyp, fye);
	return 0;

out:
	fy_parser_event_free(fyp, fye);
	return rc;

err_input:
	rc = -EINVAL;
	goto out;

err_out:
	rc = -1;
	goto out;
}

static int
null_scalar_parse(struct fy_parser *fyp, struct reflection_walker *rw,
		  enum reflection_parse_flags flags)
{
	struct fy_event *fye = NULL;
	struct fy_token *fyt;
	enum fy_scalar_style style;
	const char *text0;
	int rc;

	assert(rw);
	assert(rw->rtd);
	assert(rw->data || (flags & RPF_NO_STORE));

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

	RP_INPUT_CHECK(fye->type == FYET_SCALAR,
		       "expecting FYET_SCALAR, got %s\n", fy_event_type_get_text(fye->type));

	fyt = fy_event_get_token(fye);
	style = fy_token_scalar_style(fyt);

	text0 = fy_token_get_text0(fyt);
	RP_ASSERT(text0 != NULL);

	RP_INPUT_CHECK(style == FYSS_PLAIN, "only plain style allowed for null");

	rc = parse_null_scalar(text0, fy_parser_get_mode(fyp));

	RP_INPUT_CHECK(rc == 0, "invalid null format");

out:
	fy_parser_event_free(fyp, fye);
	return rc;

err_input:
	rc = -EINVAL;
	goto out;

err_out:
	rc = -1;
	goto out;
}

static int
integer_scalar_emit(struct fy_emitter *emit, enum fy_type_kind type_kind, union integer_scalar num,
		    struct reflection_walker *rw, enum reflection_emit_flags flags)
{
	bool is_signed;
	char buf[3 * sizeof(uintmax_t) + 3];	/* maximum buffer space */
	char *s, *e;
	bool neg;
	size_t len;
	uintmax_t val;
	int rc;

	is_signed = fy_type_kind_is_signed(type_kind);

	if (is_signed && num.sval < 0) {
		val = (uintmax_t)-num.sval;
		neg = true;
	} else {
		val = num.uval;
		neg = false;
	}

#undef PUTD
#define PUTD(_c) \
	do { \
		assert(s > buf); \
		*--s = (_c); \
	} while(0)

	e = buf + sizeof(buf);
	s = e;
	while (val) {
		PUTD('0' + val % 10);
		val /= 10;
	}
	if (s == e)
		PUTD('0');
	if (neg)
		PUTD('-');
	len = e - s;
#undef PUTD

	rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_PLAIN, s, len, NULL, NULL);
	RE_ASSERT(!rc);

	return 0;

err_out:
	return -1;
}

static int
float_scalar_emit(struct fy_emitter *emit, enum fy_type_kind type_kind, union float_scalar num,
		  struct reflection_walker *rw, enum reflection_emit_flags flags)
{
	enum {
		eft_normal,
		eft_plus_inf,
		eft_minus_inf,
		eft_nan,
	} emit_float_type;
	static const char *abnormal_values[] = {
		[eft_plus_inf] = ".inf",
		[eft_minus_inf] = "-.inf",
		[eft_nan] = ".nan",
	};
	const char *text;
	int rc;

	RE_ASSERT(fy_type_kind_is_float(type_kind));

	switch (type_kind) {
	case FYTK_FLOAT:
		emit_float_type = isnan(num.f) ? eft_nan :
			          isinf(num.f) ? (signbit(num.f) ? eft_minus_inf : eft_plus_inf) :
				  eft_normal;
		break;
	case FYTK_DOUBLE:
		emit_float_type = isnan(num.d) ? eft_nan :
			          isinf(num.d) ? (signbit(num.d) ? eft_minus_inf : eft_plus_inf) :
				  eft_normal;
		break;
	case FYTK_LONGDOUBLE:
		emit_float_type = isnan(num.ld) ? eft_nan :
			          isinf(num.ld) ? (signbit(num.ld) ? eft_minus_inf : eft_plus_inf) :
				  eft_normal;
		break;
	default:
		abort();
	}

	if (emit_float_type != eft_normal) {

		assert((size_t)emit_float_type < sizeof(abnormal_values)/sizeof(abnormal_values[0]));
		text = abnormal_values[emit_float_type];
		assert(text);

		rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_PLAIN, text, FY_NT, NULL, NULL);
		RE_ASSERT(!rc);

		return 0;
	}

	switch (type_kind) {
	case FYTK_FLOAT:
		rc = fy_emit_scalar_printf(emit, FYSS_PLAIN, NULL, NULL, "%.*g", FY_FLT_DECIMAL_DIG, num.f);
		RE_ASSERT(!rc);
		break;
	case FYTK_DOUBLE:
		rc = fy_emit_scalar_printf(emit, FYSS_PLAIN, NULL, NULL, "%.*lg", FY_DBL_DECIMAL_DIG, num.d);
		RE_ASSERT(!rc);
		break;
	case FYTK_LONGDOUBLE:
		rc = fy_emit_scalar_printf(emit, FYSS_PLAIN, NULL, NULL, "%.*Lg", FY_LDBL_DECIMAL_DIG, num.ld);
		RE_ASSERT(!rc);
		break;
	default:
		abort();
	}

	return 0;
err_out:
	return -1;
}

static int
bool_scalar_emit(struct fy_emitter *emit, enum fy_type_kind type_kind, bool v,
		 struct reflection_walker *rw, enum reflection_emit_flags flags)
{
	const char *bool_txt[2] = { "false", "true" };
	size_t bool_txt_sz[2] = { 5, 4 };
	const char *str;
	size_t len;
	int rc;

	str = bool_txt[!!v];
	len = bool_txt_sz[!!v];

	rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_PLAIN, str, len, NULL, NULL);
	RE_ASSERT(!rc);

	return 0;

err_out:
	return -1;
}

static int
common_scalar_emit(struct fy_emitter *emit, struct reflection_walker *rw,
		   enum reflection_emit_flags flags)
{
	enum fy_type_kind type_kind;

	type_kind = rw->rtd->ti->kind;

	if (fy_type_kind_is_integer(type_kind))
		return integer_scalar_emit(emit, type_kind,
					   load_integer_scalar_rw(rw),
					   rw, flags);

	if (fy_type_kind_is_float(type_kind)) {
		assert(!(rw->flags & RWF_BITFIELD_DATA));
		return float_scalar_emit(emit, type_kind,
					 load_float_scalar(type_kind, rw->data),
					 rw, flags);
	}

	if (type_kind == FYTK_BOOL)
		return bool_scalar_emit(emit, type_kind,
					load_boolean_scalar_rw(rw),
					rw, flags);
	return -1;
}

static inline int
scalar_cmp(enum fy_type_kind type_kind, const void *p_a, const void *p_b)
{
	switch (type_kind) {
	case FYTK_BOOL:
		return *(_Bool *)p_a > *(_Bool *)p_b ?  1 :
		       *(_Bool *)p_a < *(_Bool *)p_b ? -1 : 0;
	case FYTK_CHAR:
		return *(char *)p_a > *(char *)p_b ?  1 :
		       *(char *)p_a < *(char *)p_b ? -1 : 0;
	case FYTK_SCHAR:
		return *(signed char *)p_a > *(signed char *)p_b ?  1 :
		       *(signed char *)p_a < *(signed char *)p_b ? -1 : 0;
	case FYTK_UCHAR:
		return *(unsigned char *)p_a > *(unsigned char *)p_b ?  1 :
		       *(unsigned char *)p_a < *(unsigned char *)p_b ? -1 : 0;
	case FYTK_SHORT:
		return *(short *)p_a > *(short *)p_b ?  1 :
		       *(short *)p_a < *(short *)p_b ? -1 : 0;
	case FYTK_USHORT:
		return *(unsigned short *)p_a > *(unsigned short *)p_b ?  1 :
		       *(unsigned short *)p_a < *(unsigned short *)p_b ? -1 : 0;
	case FYTK_INT:
		return *(int *)p_a > *(int *)p_b ?  1 :
		       *(int *)p_a < *(int *)p_b ? -1 : 0;
	case FYTK_UINT:
		return *(unsigned int *)p_a > *(unsigned int *)p_b ?  1 :
		       *(unsigned int *)p_a < *(unsigned int *)p_b ? -1 : 0;
	case FYTK_LONG:
		return *(long *)p_a > *(long *)p_b ?  1 :
		       *(long *)p_a < *(long *)p_b ? -1 : 0;
	case FYTK_ULONG:
		return *(unsigned long *)p_a > *(unsigned long *)p_b ?  1 :
		       *(unsigned long *)p_a < *(unsigned long *)p_b ? -1 : 0;
	case FYTK_LONGLONG:
		return *(long long *)p_a > *(long long *)p_b ?  1 :
		       *(long long *)p_a < *(long long *)p_b ? -1 : 0;
	case FYTK_ULONGLONG:
		return *(unsigned long long *)p_a > *(unsigned long long *)p_b ?  1 :
		       *(unsigned long long *)p_a < *(unsigned long long *)p_b ? -1 : 0;
	case FYTK_FLOAT:
		return *(float *)p_a > *(float *)p_b ?  1 :
		       *(float *)p_a < *(float *)p_b ? -1 : 0;
	case FYTK_DOUBLE:
		return *(double *)p_a > *(double *)p_b ?  1 :
		       *(double *)p_a < *(double *)p_b ? -1 : 0;
	case FYTK_LONGDOUBLE:
		return *(long double *)p_a > *(long double *)p_b ?  1 :
		       *(long double *)p_a < *(long double *)p_b ? -1 : 0;
	default:
		break;
	}

	return -1;
}

static inline int
scalar_eq(enum fy_type_kind type_kind, const void *p_a, const void *p_b)
{
	switch (type_kind) {
	case FYTK_BOOL:
		return *(_Bool *)p_a == *(_Bool *)p_b;
	case FYTK_CHAR:
		return *(char *)p_a == *(char *)p_b;
	case FYTK_SCHAR:
		return *(signed char *)p_a == *(signed char *)p_b;
	case FYTK_UCHAR:
		return *(unsigned char *)p_a == *(unsigned char *)p_b;
	case FYTK_SHORT:
		return *(short *)p_a == *(short *)p_b;
	case FYTK_USHORT:
		return *(unsigned short *)p_a == *(unsigned short *)p_b;
	case FYTK_INT:
		return *(int *)p_a == *(int *)p_b;
	case FYTK_UINT:
		return *(unsigned int *)p_a == *(unsigned int *)p_b;
	case FYTK_LONG:
		return *(long *)p_a == *(long *)p_b;
	case FYTK_ULONG:
		return *(unsigned long *)p_a == *(unsigned long *)p_b;
	case FYTK_LONGLONG:
		return *(long long *)p_a == *(long long *)p_b;
	case FYTK_ULONGLONG:
		return *(unsigned long long *)p_a == *(unsigned long long *)p_b;
	case FYTK_FLOAT:
		return *(float *)p_a == *(float *)p_b;
	case FYTK_DOUBLE:
		return *(double *)p_a == *(double *)p_b;
	case FYTK_LONGDOUBLE:
		return *(long double *)p_a == *(long double *)p_b;
	default:
		break;
	}

	return -1;
}

static inline int
scalar_copy(enum fy_type_kind type_kind, const void *p_dst, const void *p_src)
{

	switch (type_kind) {
	case FYTK_BOOL:
		*(_Bool *)p_dst = *(_Bool *)p_src;
		return 0;
	case FYTK_CHAR:
		*(char *)p_dst = *(char *)p_src;
		return 0;
	case FYTK_SCHAR:
		*(signed char *)p_dst = *(signed char *)p_src;
		return 0;
	case FYTK_UCHAR:
		*(unsigned char *)p_dst = *(unsigned char *)p_src;
		return 0;
	case FYTK_SHORT:
		*(short *)p_dst = *(short *)p_src;
		return 0;
	case FYTK_USHORT:
		*(unsigned short *)p_dst = *(unsigned short *)p_src;
		return 0;
	case FYTK_INT:
		*(int *)p_dst = *(int *)p_src;
		return 0;
	case FYTK_UINT:
		*(unsigned int *)p_dst = *(unsigned int *)p_src;
		return 0;
	case FYTK_LONG:
		*(long *)p_dst = *(long *)p_src;
		return 0;
	case FYTK_ULONG:
		*(unsigned long *)p_dst = *(unsigned long *)p_src;
		return 0;
	case FYTK_LONGLONG:
		*(long long *)p_dst = *(long long *)p_src;
		return 0;
	case FYTK_ULONGLONG:
		*(unsigned long long *)p_dst = *(unsigned long long *)p_src;
		return 0;
	case FYTK_FLOAT:
		*(float *)p_dst = *(float *)p_src;
		return 0;
	case FYTK_DOUBLE:
		*(double *)p_dst = *(double *)p_src;
		return 0;
	case FYTK_LONGDOUBLE:
		*(long double *)p_dst = *(long double *)p_src;
		return 0;
	default:
		break;
	}

	return -1;
}

static int
common_scalar_copy(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	enum fy_type_kind type_kind;

	assert(rw_dst);
	assert(rw_src);
	assert(rw_dst->rtd);
	assert(rw_dst->rtd == rw_src->rtd);

	type_kind = rw_dst->rtd->ti->kind;
	/* non-bitfields always simple */
	if (!(rw_dst->flags & rw_src->flags & RWF_BITFIELD_DATA))
		return scalar_copy(type_kind, rw_dst->data, rw_src->data);

	/* only integers may be bitfield */
	assert(fy_type_kind_is_integer(type_kind));

	/* slower but handle bitfields */
	return store_integer_scalar_rw(rw_dst, load_integer_scalar_rw(rw_src));
}

static int
common_scalar_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	enum fy_type_kind type_kind;
	union integer_scalar val_a, val_b;

	assert(rw_a);
	assert(rw_b);
	assert(rw_a->rtd);
	assert(rw_a->rtd == rw_b->rtd);

	type_kind = rw_a->rtd->ti->kind;
	/* non-bitfields always simple */
	if (!(rw_a->flags & rw_b->flags & RWF_BITFIELD_DATA))
		return scalar_cmp(rw_a->rtd->ti->kind, rw_a->data, rw_b->data);

	/* only integers may be bitfield */
	assert(fy_type_kind_is_integer(type_kind));

	val_a = load_integer_scalar_rw(rw_a);
	val_b = load_integer_scalar_rw(rw_b);

	if (fy_type_kind_is_signed(type_kind))
		return val_a.sval > val_b.sval ?  1 :
		       val_a.sval < val_b.sval ? -1 : 0;
	else
		return val_a.uval > val_b.uval ?  1 :
		       val_a.uval < val_b.uval ? -1 : 0;
}

static int
common_scalar_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	enum fy_type_kind type_kind;
	union integer_scalar val_a, val_b;

	assert(rw_a);
	assert(rw_b);
	assert(rw_a->rtd);
	assert(rw_a->rtd == rw_b->rtd);

	type_kind = rw_a->rtd->ti->kind;
	if (!(rw_a->flags & rw_b->flags & RWF_BITFIELD_DATA))
		return scalar_eq(type_kind, rw_a->data, rw_b->data);

	/* only integers may be bitfield */
	assert(fy_type_kind_is_integer(type_kind));

	val_a = load_integer_scalar_rw(rw_a);
	val_b = load_integer_scalar_rw(rw_b);

	if (fy_type_kind_is_signed(type_kind))
		return val_a.sval == val_b.sval;
	else
		return val_a.uval == val_b.uval;
}

static int
null_scalar_emit(struct fy_emitter *emit, struct reflection_walker *rw,
		 enum reflection_emit_flags flags)
{
	const char *str;
	size_t len;
	int rc;

	str = "null";
	len = 4;

	rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_PLAIN, str, len, NULL, NULL);
	RE_ASSERT(!rc);

	return 0;

err_out:
	return -1;
}

/* NULLs are always equal to themselves */
static int
null_scalar_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return 0;
}

/* NULLs are always equal to themselves */
static int
null_scalar_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return 1;
}

/* nothing to copy */
static int
null_scalar_copy(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return 0;
}

/* walk down the dependency chain until we hit a non-dependent type */
struct reflection_type_data *
reflection_type_data_final_dependent(struct reflection_type_data *rtd)
{
	if (!rtd)
		return NULL;

	/* walk down dependent types until we get to the final */
	while (rtd->rtd_dep)
		rtd = rtd->rtd_dep;
	return rtd;
}

struct reflection_type_data *
reflection_type_data_resolved_typedef(struct reflection_type_data *rtd)
{
	if (!rtd)
		return NULL;

	while (rtd->rtd_dep && rtd->ti->kind == FYTK_TYPEDEF)
		rtd = rtd->rtd_dep;
	return rtd;
}

uintmax_t integer_field_load(struct reflection_field_data *rfd, const void *data)
{
	assert(rfd);

	if (rfd_is_bitfield(rfd))
		return load_bitfield_le(data, rfd->fi->bit_offset, rfd->fi->bit_width, rfd->is_signed);
	return load_le(data + rfd->fi->offset, rfd->fi->type_info->size, rfd->is_signed);
}

int unsigned_integer_field_store_check(struct reflection_field_data *rfd, uintmax_t v)
{
	uintmax_t maxv;
	int bit_width;

	assert(rfd);
	assert(rfd->is_unsigned);

	bit_width = !rfd_is_bitfield(rfd) ?
		    rfd->fi->type_info->size * 8 :
		    rfd->fi->bit_width;

	maxv = unsigned_integer_max_from_bit_width(bit_width);
	return v <= maxv ? 0 : -ERANGE;
}

int signed_integer_field_store_check(struct reflection_field_data *rfd, intmax_t v)
{
	intmax_t minv, maxv;
	int bit_width;

	assert(rfd);
	assert(rfd->is_signed);

	bit_width = !rfd_is_bitfield(rfd) ?
		    rfd->fi->type_info->size * 8 :
		    rfd->fi->bit_width;

	minv = signed_integer_min_from_bit_width(bit_width);
	maxv = signed_integer_max_from_bit_width(bit_width);
	return v >= minv && v <= maxv ? 0 : -ERANGE;
}

int integer_field_store_check(struct reflection_field_data *rfd, uintmax_t v)
{
	assert(rfd);

	return rfd->is_unsigned ? unsigned_integer_field_store_check(rfd, v) :
	       rfd->is_signed ? signed_integer_field_store_check(rfd, (intmax_t)v) :
	       -EINVAL;
}

void integer_field_store(struct reflection_field_data *rfd, uintmax_t v, void *data)
{
	assert(rfd);

	if (rfd_is_bitfield(rfd))
		store_bitfield_le(data, rfd->fi->bit_offset, rfd->fi->bit_width, v);
	else
		store_le(data + rfd->fi->offset, rfd->fi->type_info->size, v);
}

static inline void
struct_setup_field_rw(struct reflection_walker *rw, struct reflection_field_data *rfd)
{
	void *data;
	size_t bit_offset;

	assert(rw);
	assert(rw->parent);
	assert(rw->parent->rtd->ti->kind == FYTK_STRUCT ||
	       rw->parent->rtd->ti->kind == FYTK_UNION);

	data = rw->parent->data;

	rw->rtd = rfd_rtd(rfd);
	rw->idx = rfd->idx;
	rw->flags |= RWF_FIELD_IDX;

	if (!rfd_is_bitfield(rfd)) {
		rw->data = data + (data ? rfd->fi->offset : 0);
		rw->data_size.bytes = rw->rtd->ti->size;
	} else {
		rw->flags |= RWF_BITFIELD_DATA;

		bit_offset = rfd->fi->bit_offset;
		rw->data = data + (data ? (bit_offset >> 3) : 0);
		rw->data_size.bit_offset = bit_offset & 7;
		rw->data_size.bit_width = rfd->fi->bit_width;
	}
}

static inline uintmax_t
struct_integer_field_load(struct reflection_walker *rw_struct, struct reflection_field_data *rfd)
{
	return integer_field_load(rfd, rw_struct->data);
}

static inline void
struct_integer_field_store(struct reflection_walker *rw_struct, struct reflection_field_data *rfd, uintmax_t v)
{
	return integer_field_store(rfd, v, rw_struct->data);
}

/* instance datas */
struct struct_instance_data {
	const char *anonymous_field_name;
	uint64_t *field_present;
	uint64_t field_present_inl[16];
};

static int
struct_instance_data_setup(struct struct_instance_data *id, struct reflection_walker *rw)
{
	size_t size;

	assert(id);
	assert(rw);

	memset(id, 0, sizeof(*id));
	size = ((rw->rtd->fields_count + 63) / 64) * sizeof(uint64_t);
	if (size <= sizeof(id->field_present_inl))
		id->field_present = id->field_present_inl;
	else {
		id->field_present = malloc(size);
		RW_ASSERT(id->field_present);
		memset(id->field_present, 0, size);
	}
	rw->user = id;

	return 0;

err_out:
	return -1;
}

static void
struct_instance_data_cleanup(struct struct_instance_data *id)
{
	if (!id)
		return;

	if (id->field_present && id->field_present != id->field_present_inl)
		free(id->field_present);
}

static inline bool
struct_is_field_present(struct struct_instance_data *id, int field_idx)
{
	return !!(id->field_present[field_idx / 64] & ((uint64_t)1 << (field_idx & 63)));
}

static inline void
struct_set_field_present(struct struct_instance_data *id, int field_idx)
{
	id->field_present[field_idx / 64] |= ((uint64_t)1 << (field_idx & 63));
}

static inline void
struct_clear_field_present(struct struct_instance_data *id, int field_idx)
{
	id->field_present[field_idx / 64] &= ~((uint64_t)1 << (field_idx & 63));
}

static bool
struct_field_is_ptr_null(struct reflection_walker *rw, struct reflection_field_data *rfd)
{
	struct reflection_type_data *rtd;

	assert(rfd);
	assert(rw);
	assert(rw->rtd);
	assert(rw->rtd->ti->kind == FYTK_STRUCT || rw->rtd->ti->kind == FYTK_UNION);
	assert(rfd->rtd_parent == rw->rtd);

	rtd = rfd_rtd(rfd);
	if (rtd->ti->kind != FYTK_PTR)
		return false;

	/* bit field can't store a pointer */
	assert(!rfd_is_bitfield(rfd));
	return *(const void **)(rw->data + rfd->fi->offset) == NULL;
}

struct struct_instance_data *
struct_get_parent_struct_id(struct reflection_walker *rw)
{
	if (!rw || !rw->parent ||
	    (rw->parent->rtd->ti->kind != FYTK_STRUCT && rw->parent->rtd->ti->kind == FYTK_UNION))
		return NULL;

	return rw->parent->user;
}

static const struct reflection_type_ops dyn_array_ops;
static const struct reflection_type_ops dyn_map_ops;
static const struct reflection_type_ops const_map_ops;

enum array_type {
	AT_UNKNOWN,
	AT_DYNAMIC_ARRAY,
	AT_DYNAMIC_MAP,
	AT_CONST_ARRAY,
	AT_CONST_MAP,
};

static inline bool
array_type_is_dynamic(enum array_type type)
{
	return type == AT_DYNAMIC_ARRAY || type == AT_DYNAMIC_MAP;
}

static inline bool
array_type_is_const(enum array_type type)
{
	return type == AT_CONST_ARRAY || type == AT_CONST_MAP;
}

static inline bool
array_type_is_mapping(enum array_type type)
{
	return type == AT_DYNAMIC_MAP || type == AT_CONST_MAP;
}

struct array_info {
	enum array_type type;
	struct reflection_walker *rw;
	struct reflection_type_data *rtd, *rtd_dep, *rtd_final_dep;
	struct reflection_walker *rw_struct, *rw_field;
	struct reflection_field_data *rfd, *rfd_counter, *rfd_key, *rfd_value;
	struct reflection_walker rw_terminator, rw_fill;
	void *value_terminator, *value_fill;
	uintmax_t count;
	enum fy_event_type start_type, end_type;
	bool has_count : 1;
	bool has_terminator : 1;
	bool has_terminator_value : 1;
	bool dep_is_ptr : 1;
	bool dep_is_moveable : 1;
	bool has_fill : 1;
};

int array_info_setup(struct array_info *info, struct reflection_walker *rw)
{
	enum array_type type;
	struct reflection_field_data *rfd;
	struct reflection_type_data *rtd_final_dep, *rtdf;
	const char *counter;
	const char *key;
	void *terminator_value;

	assert(rw);
	assert(rw->rtd);
	assert(rw->rtd->rtd_dep);

	memset(info, 0, sizeof(*info));

	if (rw->rtd->ops == &dyn_array_ops)
		type = AT_DYNAMIC_ARRAY;
	else if (rw->rtd->ops == &dyn_map_ops)
		type = AT_DYNAMIC_MAP;
	else if (rw->rtd->ops == &reflection_ops_table[FYTK_CONSTARRAY])
		type = AT_CONST_ARRAY;
	else if (rw->rtd->ops == &const_map_ops)
		type = AT_CONST_MAP;
	else
		type = AT_UNKNOWN;

	assert(type != AT_UNKNOWN);

	info->type = type;
	info->rw = rw;
	info->rtd = rw->rtd;

	if (!array_type_is_mapping(info->type)) {
		info->start_type = FYET_SEQUENCE_START;
		info->end_type = FYET_SEQUENCE_END;
	} else {
		info->start_type = FYET_MAPPING_START;
		info->end_type = FYET_MAPPING_END;
	}

	info->rtd_dep = info->rtd->rtd_dep;
	info->dep_is_ptr = info->rtd_dep->ti->kind == FYTK_PTR;
	info->dep_is_moveable = info->dep_is_ptr || (info->rtd_dep->flags & RTDF_PURITY_MASK) == 0;

	rfd = reflection_get_field(rw, &info->rw_field, NULL);
	if (rfd) {
		info->rfd = rfd;
		rtdf = rfd_rtd(rfd);
	} else {
		rtdf = NULL;
	}

	if (rfd && info->rw_field)
		info->rw_struct = info->rw_field->parent;

	if (!rtdf)
		return 0;

	counter = NULL;
	terminator_value = NULL;

	if (info->rw_struct &&
	   (counter = reflection_meta_get_str(rtdf->meta, rmvid_counter)) != NULL) {
		info->rfd_counter = reflection_type_data_lookup_field(rfd->rtd_parent, counter);
		if (info->rfd_counter) {
			info->count = struct_integer_field_load(info->rw_struct, info->rfd_counter);
			info->has_count = true;
		}
	}

	info->has_terminator = type == AT_DYNAMIC_ARRAY || type == AT_DYNAMIC_MAP;

	if (info->rtd_dep)
		terminator_value = reflection_meta_generate_any_value(info->rtd->meta, rmvid_terminator, info->rtd_dep);

	if (info->has_count && !terminator_value)
		info->has_terminator = false;

	info->has_terminator_value = info->has_terminator && terminator_value;

	if (terminator_value)
		reflection_rw_value(&info->rw_terminator, info->rtd_dep, terminator_value);

	if (array_type_is_mapping(info->type)) {

		key = reflection_meta_get_str(info->rtd->meta, rmvid_key);

		RW_ASSERT(key);

		rtd_final_dep = reflection_type_data_final_dependent(info->rtd);

		RW_ASSERT(rtd_final_dep && rtd_final_dep->ti->kind == FYTK_STRUCT &&
			  rtd_final_dep->fields_count == 2);

		info->rfd_key = reflection_type_data_lookup_field(rtd_final_dep, key);
		RW_ASSERT(info->rfd_key);
		RW_ASSERT(info->rfd_key->idx >= 0 && info->rfd_key->idx <= 1);

		info->rfd_value = rtd_final_dep->fields[!info->rfd_key->idx];
		RW_ASSERT(info->rfd_value);
	}

	if (array_type_is_const(info->type) &&
	    (info->value_fill = reflection_meta_generate_any_value(info->rtd->meta,
					rmvid_fill, info->rtd_dep)) != NULL) {
		info->has_fill = true;
		reflection_rw_value(&info->rw_fill, info->rtd_dep, info->value_fill);
	}

	return 0;

err_out:
	return -1;
}

void array_info_cleanup(struct array_info *info)
{
	if (!info)
		return;
}

struct array_mapping_info {
	struct array_info *info;
	int depth;
	struct reflection_walker *rw_deps;
	struct reflection_walker rw_deps_inl[8];
	struct struct_instance_data id_struct;
	struct reflection_walker *rw_last_dep;
};

static int
array_mapping_info_setup(struct array_mapping_info *minfo, struct array_info *info,
			 struct reflection_walker *rw)
{
	struct reflection_type_data *rtdt;
	struct reflection_walker *rw_last_dep;
	struct reflection_walker *rw_deps, *rw_dep;
	size_t size;
	int depth;

	assert(minfo);
	assert(info);
	assert(rw);

	memset(minfo, 0, sizeof(*minfo));
	minfo->info = info;

	if (!array_type_is_mapping(info->type))
		return 0;

	depth = 0;
	rtdt = info->rtd_dep->rtd_dep;
	while (rtdt != NULL) {
		depth++;
		rtdt = rtdt->rtd_dep;
	}
	minfo->depth = depth;

	rw_last_dep = rw;

	if (depth > 0) {

		size = sizeof(*rw_deps) * depth;
		if (size <= sizeof(minfo->rw_deps_inl))
			rw_deps = minfo->rw_deps_inl;
		else {
			rw_deps = malloc(sizeof(*rw_deps) * depth);
			RW_ASSERT(rw_deps);
			memset(rw_deps, 0, sizeof(*rw_deps) * depth);
		}
		minfo->rw_deps = rw_deps;

		for (rw_dep = rw_deps, rtdt = info->rtd_dep->rtd_dep;
		     rtdt != NULL; rtdt = rtdt->rtd_dep, rw_dep++) {
			rw_dep->parent = rw_last_dep;

			rw_dep->rtd = rtdt;
			rw_dep->data_size.bytes = rtdt->ti->size;

			rw_last_dep = rw_dep;
		}
	}

	minfo->rw_last_dep = rw_last_dep;
	assert(minfo->rw_last_dep->rtd->ti->kind == FYTK_STRUCT);

	return 0;

err_out:
	if (minfo->rw_deps && minfo->rw_deps != minfo->rw_deps_inl)
		free(minfo->rw_deps);
	return -1;
}

static void
array_mapping_info_cleanup(struct array_mapping_info *minfo)
{
	if (!minfo)
		return;
	if (minfo->rw_deps && minfo->rw_deps != minfo->rw_deps_inl)
		free(minfo->rw_deps);
}

static int
array_mapping_info_parse_item_prolog(struct array_mapping_info *minfo, struct reflection_walker *rw,
				     enum reflection_parse_flags flags)
{
	struct reflection_walker *rw_deps, *rw_last_dep, *rw_dep;
	void *last_data, *ptr;
	int i, depth;

	assert(minfo);
	assert(rw);

	depth = minfo->depth;
	rw_deps = minfo->rw_deps;
	rw_last_dep = minfo->rw_last_dep;

	last_data = rw->data;
	for (i = 0; i < depth + 1; i++) {

		assert(i == 0 || rw_deps != NULL);

		rw_dep = i == 0 ? rw : &rw_deps[i - 1];

		switch (rw_dep->rtd->ti->kind) {
		case FYTK_PTR:
			ptr = reflection_type_data_alloc(rw_dep->rtd->rtd_dep);
			RW_ASSERT(ptr);

			rw_dep->data = last_data;
			if (!(flags & RPF_NO_STORE))
				*(void **)rw_dep->data = ptr;
			last_data = ptr;
			break;
		case FYTK_TYPEDEF:
			rw_dep->data = last_data;
			break;
		default:
			RW_ASSERT(rw_dep == rw_last_dep);
			RW_ASSERT(i == depth);
			rw_last_dep->data = last_data;
			break;
		}
	}

	return 0;

err_out:
	while (i > 1) {
		i--;
		rw_dep = i == 0 ? rw : &rw_deps[i - 1];
		switch (rw_dep->rtd->ti->kind) {
		case FYTK_PTR:
			ptr = *(void **)rw_dep->data;
			reflection_type_data_free(rw_dep->rtd->rtd_dep, ptr);
			break;
		default:
			break;
		}
	}
	return -1;
}

static int
array_mapping_info_parse_item_epilog(struct array_mapping_info *minfo, struct reflection_walker *rw,
				     enum reflection_parse_flags flags, bool force_release)
{
	struct reflection_walker *rw_deps, *rw_dep;
	void *ptr;
	int i, depth;

	depth = minfo->depth;
	rw_deps = minfo->rw_deps;

	if ((flags & RPF_NO_STORE) || force_release) {

		for (i = 0; i < depth + 1; i++) {

			rw_dep = i == 0 ? rw : &rw_deps[i - 1];

			switch (rw_dep->rtd->ti->kind) {
			case FYTK_PTR:
				ptr = *(void **)rw_dep->data;
				reflection_type_data_free(rw_dep->rtd->rtd_dep, ptr);
				break;
			default:
				break;
			}
		}
	}

	/* erase pointers */
	for (i = 0; i < depth + 1; i++) {
		rw_dep = i == 0 ? rw : &rw_deps[i - 1];
		rw_dep->data = NULL;
	}

	return 0;
}

static int
array_info_load_item_count(struct array_info *info, void *data)
{
	struct reflection_walker rw_item;
	void *datat;
	size_t item_size;
	uintmax_t count;
	int rc;

	assert(info);

	if (info->has_count)
		return 0;

	assert(data);

	rc = -1;
	count = 0;

	if (info->has_terminator) {

		item_size = info->rtd_dep->ti->size;

		memset(&rw_item, 0, sizeof(rw_item));
		rw_item.parent = info->rw;
		rw_item.rtd = info->rtd_dep;
		rw_item.data_size.bytes = item_size;
		rw_item.flags = RWF_SEQ_IDX;

		for (datat = data, count = 0; ; datat += item_size) {

			if (count > INT_MAX)
				return -1;

			if (info->dep_is_ptr && *(const void **)datat == NULL)
				break;

			if (info->has_terminator_value) {
				rw_item.idx = count;
				rw_item.data = datat;

				if (reflection_eq_rw(&info->rw_terminator, &rw_item) > 0)
					break;
			} else {
				if (memiszero(datat, item_size))
					break;
			}

			count++;
		}
		rc = 0;
		goto out;
	}

	if (array_type_is_const(info->type)) {
		count = info->rw->rtd->ti->count;
		rc = 0;
		goto out;
	}

out:
	if (!rc) {
		info->count = count;
		info->has_count = true;
	}

	return rc;
}

static int
array_info_store_item_count(struct array_info *info, void *data, uintmax_t count)
{
	struct reflection_walker rw_item;
	size_t item_size;
	int rc;

	if (info->rfd_counter) {
		rc = integer_field_store_check(info->rfd_counter, count);
		if (rc)
			return -ERANGE;
	}

	info->count = count;
	info->has_count = true;

	if (info->rfd_counter)
		struct_integer_field_store(info->rw_struct, info->rfd_counter, info->count);

	if (info->has_terminator || info->has_fill) {
		item_size = info->rtd_dep->ti->size;

		memset(&rw_item, 0, sizeof(rw_item));
		rw_item.rtd = info->rtd_dep;
		rw_item.data_size.bytes = item_size;
		rw_item.parent = info->rw;
		rw_item.flags = RWF_SEQ_IDX;
	}

	if (info->has_terminator) {

		if (info->has_terminator_value) {

			rw_item.idx = count;
			rw_item.data = data + count * item_size;
			count++;

			rc = reflection_copy_rw(&rw_item, &info->rw_terminator);
			if (rc)
				return rc;
		} else
			memset(data + info->count * item_size, 0, item_size);
	}

	if (info->has_fill) {

		while (count < info->rtd->ti->count) {
			rw_item.idx = count;
			rw_item.data = data + count * item_size;
			count++;

			rc = reflection_copy_rw(&rw_item, &info->rw_fill);
			if (rc)
				return rc;
		}
	}

	return 0;
}

static int
array_mapping_info_emit_item_prolog(struct array_mapping_info *minfo, struct reflection_walker *rw_item,
				    enum reflection_emit_flags flags)
{
	struct reflection_walker *rw_deps, *rw_last_dep, *rw_dep;
	void *ptr, *last_data = NULL;
	int depth, i;

	assert(minfo);
	assert(rw_item);

	depth = minfo->depth;
	rw_deps = minfo->rw_deps;
	rw_last_dep = minfo->rw_last_dep;

	assert(rw_last_dep != NULL);

	last_data = rw_item->data;
	for (i = 0, rw_dep = rw_deps; i < depth + 1; i++, rw_dep++) {

		assert(i == 0 || rw_deps != NULL);

		rw_dep = i == 0 ? rw_item : &rw_deps[i - 1];

		switch (rw_dep->rtd->ti->kind) {
		case FYTK_PTR:
			assert(last_data);
			rw_dep->data = last_data;
			ptr = *(void **)rw_dep->data;
			assert(ptr);
			last_data = ptr;
			break;
		case FYTK_TYPEDEF:
			rw_dep->data = last_data;
			break;
		default:
			assert(rw_dep == rw_last_dep);
			assert(i == depth);
			rw_last_dep->data = last_data;
			break;
		}
	}

	assert(rw_last_dep->data != NULL);

	return 0;
}

static int
array_mapping_info_emit_item_epilog(struct array_mapping_info *minfo, struct reflection_walker *rw_item,
				    enum reflection_emit_flags flags)
{
	struct reflection_walker *rw_deps, *rw_dep;
	int i, depth;

	depth = minfo->depth;
	rw_deps = minfo->rw_deps;

	/* erase pointers */
	for (i = 0; i < depth + 1; i++) {
		rw_dep = i == 0 ? rw_item : &rw_deps[i - 1];
		rw_dep->data = NULL;
	}

	return 0;
}

static int
array_parse(struct array_info *info, struct fy_parser *fyp, struct reflection_walker *rw,
	    enum reflection_parse_flags flags)
{
	struct fy_event *fye = NULL;
	const struct fy_event *fye_peek;
	struct reflection_walker rw_item, rw_key, rw_value, *rw_struct = NULL;
	void *data = NULL, *data_new;
	size_t item_size;
	uintmax_t item_count = 0, idx = 0, data_count = 0, new_item_count, new_data_count;
	struct struct_instance_data id_struct_local, *id_struct = &id_struct_local;
	struct array_mapping_info minfo_local, *minfo = &minfo_local;
	bool have_map_prolog = false, have_map_key = false, have_map_value = false;
	int rc = 0;

	item_size = info->rtd_dep->ti->size;

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

	RP_INPUT_CHECK(fye->type == info->start_type,
			"illegal event type (expecting %s start)",
			!array_type_is_mapping(info->type) ? "sequence" : "mapping");

	fy_parser_event_free(fyp, fye);
	fye = NULL;

	if (array_type_is_dynamic(info->type)) {
		if (!info->dep_is_moveable) {
			rc = !array_type_is_mapping(info->type) ?
				fy_parser_count_sequence_items(fyp) :
				fy_parser_count_mapping_items(fyp);

			RP_ASSERT(rc >= 0);

			item_count = (uintmax_t)rc;
		} else
			item_count = 16;	// XXX just initial guess
	} else if (array_type_is_const(info->type)) {
		item_count = info->rtd->ti->count;
	} else {
		RP_ASSERT(false);
	}

	data_count = item_count + info->has_terminator;

	if (array_type_is_dynamic(info->type)) {
		data = reflection_malloc(info->rtd->rts, data_count * item_size);
		RP_ASSERT(data != NULL);

		memset(data, 0, data_count * item_size);
	} else {
		data = info->rw->data;
	}

	memset(&rw_item, 0, sizeof(rw_item));
	rw_item.parent = info->rw;
	rw_item.rtd = info->rtd_dep;
	rw_item.data_size.bytes = item_size;
	rw_item.flags = RWF_SEQ_IDX;
	if (!array_type_is_mapping(info->type))	/* for regular arrays, the idx is the key */
		rw_item.flags |= RWF_SEQ_KEY;
	else
		rw_item.flags |= RWF_MAP;

	if (array_type_is_mapping(info->type)) {
		rc = array_mapping_info_setup(minfo, info, &rw_item);
		RP_ASSERT(!rc);

		if (minfo->rw_last_dep) {
			rc = struct_instance_data_setup(id_struct, minfo->rw_last_dep);
			RP_ASSERT(!rc);
		}
		rw_struct = minfo->rw_last_dep;
		assert(rw_struct);

		memset(&rw_key, 0, sizeof(rw_key));
		rw_key.parent = rw_struct;
		rw_key.flags = RWF_KEY;

		memset(&rw_value, 0, sizeof(rw_value));
		rw_value.parent = rw_struct;
		rw_value.flags = RWF_VALUE | RWF_COMPLEX_KEY;
		rw_value.rw_key = &rw_key;
	}

	for (idx = 0; ; idx++) {

		fye_peek = fy_parser_parse_peek(fyp);
		RP_INPUT_CHECK(fye_peek != NULL,
			       "premature end of input while scanning for items\n");

		RP_INPUT_CHECK(fye_peek->type == FYET_SCALAR ||
			       fye_peek->type == FYET_MAPPING_START ||
			       fye_peek->type == FYET_SEQUENCE_START ||
			       fye_peek->type == info->end_type,
			       "invalid event type");

		if (fye_peek->type == info->end_type)
			break;

		if (info->rfd_counter) {
			rc = integer_field_store_check(info->rfd_counter, idx);
			RP_INPUT_CHECK(!rc, "Too many items (%ju) for given counter field %s",
				       idx, info->rfd_counter->fi->name);
		}

		if (idx  >= data_count) {

			RP_INPUT_CHECK(!array_type_is_const(info->type),
					"Too many items (%ju) for fixed array (%ju)",
				       idx + 1, item_count);

			/* should never get here when moveable */
			assert(!info->dep_is_moveable);

			/* grow */
			new_item_count = item_count * 2;
			new_data_count = new_item_count + info->has_terminator;

			data_new = reflection_realloc(info->rtd->rts, data, new_data_count * item_size);
			RP_ASSERT(data_new != NULL);

			memset(data_new + data_count * item_size, 0, (new_data_count - data_count) * item_size);

			item_count = new_item_count;
			data_count = new_data_count;
			data = data_new;
		}

		rw_item.idx = idx;
		rw_item.data = data + idx * item_size;

		if (!array_type_is_mapping(info->type)) {

			rc = reflection_parse_rw(fyp, &rw_item, flags);
			if (rc)
				goto out;
		} else {
			struct_clear_field_present(id_struct, info->rfd_key->idx);
			struct_clear_field_present(id_struct, info->rfd_value->idx);

			rc = array_mapping_info_parse_item_prolog(minfo, &rw_item, flags);
			RP_ASSERT(!rc);
			have_map_prolog = true;

			struct_setup_field_rw(&rw_key, info->rfd_key);
			rc = reflection_parse_rw(fyp, &rw_key, flags);
			if (rc)
				goto out;
			have_map_key = true;
			struct_set_field_present(id_struct, info->rfd_key->idx);

			struct_setup_field_rw(&rw_value, info->rfd_value);
			rc = reflection_parse_rw(fyp, &rw_value, flags);
			if (rc)
				goto out;
			have_map_value = true;
			struct_set_field_present(id_struct, info->rfd_value->idx);

			rc = array_mapping_info_parse_item_epilog(minfo, &rw_item, flags, false);
			RP_ASSERT(!rc);

			have_map_prolog = have_map_key = have_map_value = false;
		}
	}
	rc = 0;

	item_count = idx;
	data_count = item_count + info->has_terminator;

	if (array_type_is_dynamic(info->type) && info->dep_is_moveable) {
		/* trim the excess */
		data_new = reflection_realloc(info->rtd->rts, data, data_count * item_size);
		RP_ASSERT(data_new != NULL);

		data = data_new;
	}

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

	/* pull the last SEQUENCE_END */
	RP_INPUT_CHECK(fye->type == info->end_type,
		       "invalid event type while expecting %s end (got %s)",
			!array_type_is_mapping(info->type) ? "sequence" : "mapping",
		       fy_event_type_get_text(fye->type));

	/* and commit */
	if (!(flags & RPF_NO_STORE)) {
		rc = array_info_store_item_count(info, data, item_count);
		RP_ASSERT(!rc);

		if (array_type_is_dynamic(info->type))
			*(void **)info->rw->data = data;
	} else {
		if (array_type_is_dynamic(info->type))
			reflection_free(info->rtd->rts, data);
	}

	fy_parser_event_free(fyp, fye);

	return rc;

out:
	if (data) {
		if (have_map_prolog) {
			if (have_map_value)
				reflection_dtor_rw(&rw_value);
			if (have_map_key)
				reflection_dtor_rw(&rw_key);
			array_mapping_info_parse_item_epilog(minfo, &rw_item, flags, true);
		}
		while (idx) {
			rw_item.idx = --idx;
			rw_item.data = data + idx * item_size;
			reflection_dtor_rw(&rw_item);
			if (array_type_is_const(info->type))
				memset(rw_item.data, 0, item_size);
		}
		if (array_type_is_dynamic(info->type))
			reflection_free(info->rtd->rts, data);
	}

	fy_parser_event_free(fyp, fye);
	if (array_type_is_mapping(info->type)) {
		if (minfo->rw_last_dep)
			struct_instance_data_cleanup(id_struct);
		array_mapping_info_cleanup(minfo);
	}
	return rc;

err_out:
	rc = -1;
	goto out;

err_input:
	rc = -EINVAL;
	goto out;
}

static int
array_emit(struct array_info *info, struct fy_emitter *emit,
	   struct reflection_walker *rw, enum reflection_emit_flags flags)
{
	struct reflection_walker rw_item, rw_key, rw_value, *rw_struct = NULL;
	void *data;
	uintmax_t idx, count;
	size_t item_size;
	struct array_mapping_info minfo_local, *minfo = &minfo_local;
	int rc;

	if (array_type_is_dynamic(info->type))
		data = *(void **)info->rw->data;
	else
		data = info->rw->data;

	item_size = info->rtd_dep->ti->size;

	memset(&rw_item, 0, sizeof(rw_item));
	rw_item.parent = info->rw;
	rw_item.rtd = info->rtd_dep;
	rw_item.data_size.bytes = item_size;
	rw_item.flags = RWF_SEQ_IDX;
	if (!array_type_is_mapping(info->type))	/* for regular arrays, the idx is the key */
		rw_item.flags |= RWF_SEQ_KEY;

	if (array_type_is_mapping(info->type)) {
		rc = array_mapping_info_setup(minfo, info, &rw_item);
		RE_ASSERT(!rc);

		rw_struct = minfo->rw_last_dep;
		assert(rw_struct);

		memset(&rw_key, 0, sizeof(rw_key));
		rw_key.parent = rw_struct;
		rw_key.flags = RWF_KEY;

		memset(&rw_value, 0, sizeof(rw_value));
		rw_value.parent = rw_struct;
		rw_value.flags = RWF_VALUE | RWF_COMPLEX_KEY;
		rw_value.rw_key = &rw_key;
	}

	if (data) {
		rc = array_info_load_item_count(info, data);
		RE_ASSERT(!rc);
		RE_ASSERT(info->has_count);
		count = info->count;
	} else {
		count = 0;
	}

	/* do not emit if empty and marked as such (and we're not in a key, value pair) */
	if (reflection_meta_get_bool(info->rtd->meta, rmvid_omit_if_empty)) {
		if (!count && !(info->rw->flags & (RWF_VALUE | RWF_KEY)))
			goto out;
	}

	rc = fy_emit_eventf(emit, info->start_type, FYNS_ANY, NULL, NULL);
	RE_ASSERT(!rc);

	for (idx = 0; idx < count; idx++, data += item_size) {

		rw_item.idx = idx;
		rw_item.data = data;

		if (!array_type_is_mapping(info->type)) {
			rc = reflection_emit_rw(emit, &rw_item, flags);
			RE_ASSERT(!rc);
		} else {

			rc = array_mapping_info_emit_item_prolog(minfo, &rw_item, flags);
			RE_ASSERT(!rc);

			struct_setup_field_rw(&rw_key, info->rfd_key);
			rc = reflection_emit_rw(emit, &rw_key, flags);
			RE_ASSERT(!rc);

			struct_setup_field_rw(&rw_value, info->rfd_value);
			rc = reflection_emit_rw(emit, &rw_value, flags);
			RE_ASSERT(!rc);

			rc = array_mapping_info_emit_item_epilog(minfo, &rw_item, flags);
			RE_ASSERT(!rc);
		}
	}

	rc = fy_emit_eventf(emit, info->end_type);
	RE_ASSERT(!rc);
	rc = 0;
out:
	if (array_type_is_mapping(info->type))
		array_mapping_info_cleanup(minfo);
	return rc;

err_out:
	rc = -1;
	goto out;
}

static void
array_dtor(struct array_info *info)
{
	struct reflection_walker rw_item;
	uintmax_t idx, count;
	void *p, *data;
	size_t item_size;
	bool is_terminator;

	if (array_type_is_dynamic(info->type))
		data = *(void **)info->rw->data;
	else
		data = info->rw->data;

	if (!data)
		return;

	if (!reflection_type_data_has_dtor(info->rtd_dep))
		goto out_free;

	if (info->has_count)
		count = info->count;
	else if (info->has_terminator)
		count = INT_MAX;
	else if (array_type_is_const(info->type))
		count = info->rw->rtd->ti->count;
	else
		count = UINTMAX_MAX;

	assert(count != UINTMAX_MAX);

	item_size = info->rtd_dep->ti->size;

	memset(&rw_item, 0, sizeof(rw_item));
	rw_item.parent = info->rw;
	rw_item.rtd = info->rtd_dep;
	rw_item.data_size.bytes = item_size;
	rw_item.flags = RWF_SEQ_IDX;
	if (!array_type_is_mapping(info->type))	/* for regular arrays, the idx is the key */
		rw_item.flags |= RWF_SEQ_KEY;

	/* free in sequence */
	is_terminator = false;
	for (idx = 0, p = data; idx < count; idx++, p += item_size) {

		/* do not follow NULLs */
		if (info->dep_is_ptr && *(const void **)p == NULL)
			break;

		rw_item.idx = idx;
		rw_item.data = p;

		if (info->has_terminator_value)
			is_terminator = reflection_eq_rw(&info->rw_terminator, &rw_item) > 0;
		else if (info->has_terminator)
			is_terminator = memiszero(p, item_size);

		reflection_dtor_rw(&rw_item);

		if (is_terminator)
			break;
	}

out_free:
	if (array_type_is_dynamic(info->type))
		reflection_free(info->rtd->rts, data);
}

static int
array_copy(struct array_info *info_dst, struct array_info *info_src)
{
	struct reflection_walker *rw;
	struct reflection_walker rw_item_dst, rw_item_src;
	void *ptr_dst = NULL, *dst, *ptr_src = NULL, *src;
	size_t item_size;
	uintmax_t idx, data_count, count = 0;
	int rc;

	assert(info_dst);
	assert(info_src);

	rw = info_src->rw;
	assert(rw);

	RW_ASSERT(info_src->rtd == info_dst->rtd);

	if (array_type_is_dynamic(info_src->type))
		ptr_src = *(void **)info_src->rw->data;
	else if (array_type_is_const(info_src->type))
		ptr_src = info_src->rw->data;
	else
		ptr_src = NULL;

	RW_ASSERT(ptr_src);

	if (!ptr_src) {
		if (array_type_is_dynamic(info_dst->type))
			*(void **)info_dst->rw->data = NULL;
		return 0;
	}

	if (array_type_is_dynamic(info_dst->type))
		assert(*(void **)info_dst->rw->data == NULL);

	rc = array_info_load_item_count(info_src, ptr_src);
	RW_ASSERT(!rc);

	count = (int)info_src->count;

	item_size = info_dst->rtd_dep->ti->size;

	memset(&rw_item_dst, 0, sizeof(rw_item_dst));
	memset(&rw_item_src, 0, sizeof(rw_item_src));

	rw_item_dst.data_size.bytes = rw_item_src.data_size.bytes = item_size;
	rw_item_dst.flags = rw_item_src.flags = RWF_SEQ_IDX;
	if (!array_type_is_mapping(info_dst->type)) {	/* for regular arrays, the idx is the key */
		rw_item_dst.flags |= RWF_SEQ_KEY;
		rw_item_src.flags |= RWF_SEQ_KEY;
	}

	rw_item_dst.parent = info_dst->rw;
	rw_item_dst.rtd = info_dst->rtd;

	rw_item_src.parent = info_src->rw;
	rw_item_src.rtd = info_src->rtd;

	/* make sure that the counter can fit */
	if (info_dst->rfd_counter) {
		rc = integer_field_store_check(info_dst->rfd_counter, info_src->count);
		RW_ASSERT(!rc);
	}

	data_count = info_src->count + info_dst->has_terminator;

	if (array_type_is_dynamic(info_dst->type)) {
		ptr_dst = reflection_malloc(info_dst->rw->rtd->rts, data_count * item_size);
		RW_ASSERT(ptr_dst);
	} else
		ptr_dst = info_dst->rw->data;

	for (idx = 0, dst = ptr_dst, src = ptr_src; idx < count;
	     idx++, dst += item_size, src += item_size) {

		rw_item_dst.idx = rw_item_src.idx = idx;

		rw_item_dst.data = dst;
		rw_item_src.data = src;

		rc = reflection_copy_rw(&rw_item_dst, &rw_item_src);
		RW_ASSERT(!rc);
	}

	rc = array_info_store_item_count(info_dst, ptr_dst, info_src->count);
	RW_ASSERT(!rc);

	rc = 0;

out:
	if (array_type_is_dynamic(info_dst->type))
		*(void **)info_dst->rw->data = ptr_dst;

	return rc;

err_out:
	if (ptr_dst) {
		if (idx > 0) {
			assert(dst);
			while (idx--) {
				dst -= item_size;
				rw_item_dst.idx = idx;
				rw_item_dst.data = dst;

				reflection_dtor_rw(&rw_item_dst);
			}
		}
		if (array_type_is_dynamic(info_dst->type)) {
			reflection_free(info_dst->rw->rtd->rts, ptr_dst);
			ptr_dst = NULL;
		}
	}
	rc = -1;
	goto out;
}

static int
array_cmp(struct array_info *info_a, struct array_info *info_b)
{
	struct reflection_walker *rw;
	struct reflection_walker rw_item_a, rw_item_b;
	void *ptr_a, *ptr_b;
	size_t item_size;
	uintmax_t idx, count;
	int rc;

	assert(info_a);
	assert(info_b);

	rw = info_a->rw;
	assert(rw);

	RW_ASSERT(info_a->rtd == info_b->rtd);

	if (array_type_is_dynamic(info_a->type))
		ptr_a = *(void **)info_a->rw->data;
	else if (array_type_is_const(info_a->type))
		ptr_a = info_a->rw->data;
	else
		ptr_a = NULL;

	RW_ASSERT(ptr_a);

	if (array_type_is_dynamic(info_b->type))
		ptr_b = *(void **)info_b->rw->data;
	else if (array_type_is_const(info_b->type))
		ptr_b = info_b->rw->data;
	else
		ptr_b = NULL;

	RW_ASSERT(ptr_b);

	if (ptr_a == ptr_b)
		return 0;

	if (!ptr_a || !ptr_b)
		return -1;

	item_size = info_a->rtd_dep->ti->size;

	rc = array_info_load_item_count(info_a, ptr_a);
	RW_ASSERT(!rc);

	rc = array_info_load_item_count(info_b, ptr_b);
	RW_ASSERT(!rc);

	/* sizes must match */
	if (info_a->count != info_b->count) {
		rc = info_a->count < info_b->count ? -1 : 1;
		goto out;
	}
	count = info_a->count;

	memset(&rw_item_a, 0, sizeof(rw_item_a));
	memset(&rw_item_b, 0, sizeof(rw_item_b));

	rw_item_a.rtd = rw_item_b.rtd = info_a->rtd;
	rw_item_a.data_size.bytes = rw_item_b.data_size.bytes = item_size;
	rw_item_a.flags = rw_item_b.flags = RWF_SEQ_IDX;
	if (!array_type_is_mapping(info_a->type)) {	/* for regular arrays, the idx is the key */
		rw_item_a.flags |= RWF_SEQ_KEY;
		rw_item_b.flags |= RWF_SEQ_KEY;
	}

	rw_item_a.parent = info_a->rw;
	rw_item_b.parent = info_b->rw;

	rc = 0;
	for (idx = 0; idx < count; idx++) {

		rw_item_a.idx = rw_item_b.idx = idx;

		rw_item_a.data = ptr_a + idx * item_size;
		rw_item_b.data = ptr_b + idx * item_size;

		rc = reflection_cmp_rw(&rw_item_a, &rw_item_b);
		if (rc)
			break;
	}
out:
	return rc;

err_out:
	rc = -2;
	goto out;
}

static int
array_eq(struct array_info *info_a, struct array_info *info_b)
{
	struct reflection_walker *rw;
	struct reflection_walker rw_item_a, rw_item_b;
	void *ptr_a, *ptr_b;
	size_t item_size;
	uintmax_t idx, count;
	int rc;

	assert(info_a);
	assert(info_b);

	rw = info_a->rw;
	assert(rw);

	RW_ASSERT(info_a->rtd == info_b->rtd);

	if (array_type_is_dynamic(info_a->type))
		ptr_a = *(void **)info_a->rw->data;
	else if (array_type_is_const(info_a->type))
		ptr_a = info_a->rw->data;
	else
		ptr_a = NULL;
	RW_ASSERT(ptr_a);

	if (array_type_is_dynamic(info_b->type))
		ptr_b = *(void **)info_b->rw->data;
	else if (array_type_is_const(info_b->type))
		ptr_b = info_b->rw->data;
	else
		ptr_b = NULL;
	RW_ASSERT(ptr_b);

	if (ptr_a == ptr_b)
		return 0;

	if (!ptr_a || !ptr_b)
		return -1;

	item_size = info_a->rtd_dep->ti->size;

	rc = array_info_load_item_count(info_a, ptr_a);
	RW_ASSERT(!rc);

	rc = array_info_load_item_count(info_b, ptr_b);
	RW_ASSERT(!rc);

	/* sizes must match */
	if (info_a->count != info_b->count) {
		rc = -1;
		goto out;
	}
	count = info_a->count;

	/* pure array items can just be memcmp'ed */
	if ((info_a->rtd_dep->flags & RTDF_PURITY_MASK) == 0) {
		rc = memcmp(ptr_a, ptr_b, count * item_size);
		return !rc ? 0 : -1;
	}

	memset(&rw_item_a, 0, sizeof(rw_item_a));
	memset(&rw_item_b, 0, sizeof(rw_item_b));

	rw_item_a.rtd = rw_item_b.rtd = info_a->rtd;
	rw_item_a.data_size.bytes = rw_item_b.data_size.bytes = item_size;
	rw_item_a.flags = rw_item_b.flags = RWF_SEQ_IDX;
	if (!array_type_is_mapping(info_a->type)) {	/* for regular arrays, the idx is the key */
		rw_item_a.flags |= RWF_SEQ_KEY;
		rw_item_b.flags |= RWF_SEQ_KEY;
	}

	rw_item_a.parent = info_a->rw;
	rw_item_b.parent = info_b->rw;

	rc = 1;
	for (idx = 0; idx < count; idx++) {

		rw_item_a.idx = rw_item_b.idx = idx;

		rw_item_a.data = ptr_a + idx * item_size;
		rw_item_b.data = ptr_b + idx * item_size;

		rc = reflection_eq_rw(&rw_item_a, &rw_item_b);
		if (rc <= 0)
			break;
	}
out:
	return rc;

err_out:
	rc = -2;
	goto out;
}

static int
common_array_parse(struct fy_parser *fyp, struct reflection_walker *rw,
		  enum reflection_parse_flags flags)
{
	struct array_info info;
	int rc;

	rc = array_info_setup(&info, rw);
	if (!rc)
		rc = array_parse(&info, fyp, rw, flags);
	array_info_cleanup(&info);
	return rc;
}

static int
common_array_emit(struct fy_emitter *emit, struct reflection_walker *rw,
		 enum reflection_emit_flags flags)
{
	struct array_info info;
	int rc;

	rc = array_info_setup(&info, rw);
	if (!rc)
		rc = array_emit(&info, emit, rw, flags);
	array_info_cleanup(&info);
	return rc;
}

static void
common_array_dtor(struct reflection_walker *rw)
{
	struct array_info info;
	int rc;

	rc = array_info_setup(&info, rw);
	if (!rc)
		array_dtor(&info);
	array_info_cleanup(&info);
}

enum common_array_call2_type {
	CAC2T_COPY,
	CAC2T_CMP,
	CAC2T_EQ,
};

static int
common_array_call2(struct reflection_walker *rw_a, struct reflection_walker *rw_b,
		   enum common_array_call2_type call_type)
{
	struct reflection_walker *rw;
	struct array_info info_local_a, *info_a = &info_local_a;
	struct array_info info_local_b, *info_b = &info_local_b;
	bool a_ready = false, b_ready = false;
	int rc;

	assert(rw_a);
	assert(rw_b);

	rw = rw_b;

	RW_ASSERT(rw_b->rtd == rw_a->rtd);

	rc = array_info_setup(info_a, rw_a);
	RW_ASSERT(!rc);
	a_ready = true;

	rc = array_info_setup(info_b, rw_b);
	RW_ASSERT(!rc);
	b_ready = true;

	switch (call_type) {
	case CAC2T_COPY:
		rc = array_copy(info_a, info_b);
		break;
	case CAC2T_CMP:
		rc = array_cmp(info_a, info_b);
		break;
	case CAC2T_EQ:
		rc = array_eq(info_a, info_b);
		break;
	default:
		rc = -2;
		break;
	}

out:
	if (b_ready)
		array_info_cleanup(info_b);
	if (a_ready)
		array_info_cleanup(info_a);

	return rc;

err_out:
	rc = -2;
	goto out;
}

static int
common_array_copy(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	return common_array_call2(rw_dst, rw_src, CAC2T_COPY);
}

static int
common_array_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return common_array_call2(rw_a, rw_b, CAC2T_CMP);
}

static int
common_array_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	int rc;
	rc = common_array_call2(rw_a, rw_b, CAC2T_EQ);
	return rc;
}

struct str_instance_data {
	struct reflection_field_data *rfd;
	struct reflection_field_data *rfd_counter;
	struct reflection_walker *rw;
	struct reflection_walker *rw_field;
	struct reflection_walker *rw_struct;
	bool not_null_terminated;
};

static void
str_instance_data_setup(struct str_instance_data *id, struct reflection_walker *rw)
{
	struct reflection_field_data *rfd;
	const char *counter;

	assert(id);
	assert(rw);

	memset(id, 0, sizeof(*id));

	id->rw = rw;
	rfd = reflection_get_field(rw, &id->rw_field, NULL);
	id->rfd = rfd;
	if (rfd && id->rw_field)
		id->rw_struct = id->rw_field->parent;
	if (rfd && id->rw_struct) {
		counter = reflection_meta_get_str(rw->rtd->meta, rmvid_counter);
		if (counter)
			id->rfd_counter = reflection_type_data_lookup_field(rfd->rtd_parent, counter);
	}

	/* it's not null terminated if there's a counter or the meta value is set */
	id->not_null_terminated = id->rfd_counter || reflection_meta_get_bool(rw->rtd->meta, rmvid_not_null_terminated);
}

static int
str_load_length(struct str_instance_data *id, size_t *lenp)
{
	assert(id);
	assert(lenp);

	return 0;
}

static bool
str_store_length_check(struct str_instance_data *id, size_t len)
{
	assert(id);

	if (!id->rfd_counter)
		return 0;

	return integer_field_store_check(id->rfd_counter, len + (id->not_null_terminated ? 0 : 1));
}

static void
str_store_length(struct str_instance_data *id, const char *str, size_t len)
{
	assert(id);

	if (!id->rfd_counter)
		return;

	struct_integer_field_store(id->rw_struct, id->rfd_counter, len);
}

static int
str_instance_len(struct str_instance_data *id, const char *text, size_t *lenp)
{
	assert(id);
	assert(lenp);

	if (!text)
		return -EINVAL;

	*lenp = !id->rfd_counter ? (uintmax_t)strlen(text) :
				 struct_integer_field_load(id->rw_struct, id->rfd_counter);
	if (*lenp > SIZE_MAX)
		return -ERANGE;

	return 0;
}

static int
const_array_char_parse(struct fy_parser *fyp, struct reflection_walker *rw,
		       enum reflection_parse_flags flags)
{
	struct fy_event *fye = NULL;
	struct reflection_type_data *rtd, *rtd_dep;
	struct fy_token *fyt;
	enum fy_type_kind type_kind __attribute__((unused));
	struct str_instance_data id_local, *id = &id_local;
	const char *text;
	size_t len, lenz;
	void *data;
	int rc;

	rtd = rw->rtd;
	assert(rtd);
	assert(rtd->ti->kind == FYTK_CONSTARRAY);

	rtd_dep = rtd->rtd_dep;
	assert(rtd_dep);

	data = rw->data;
	assert(data);

	type_kind = rtd->ti->kind;
	assert(fy_type_kind_is_valid(type_kind));

	type_kind = rtd_dep->ti->kind;
	assert(type_kind == FYTK_CHAR);

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

	RP_INPUT_CHECK(fye->type == FYET_SCALAR,
		       "expecting FYET_SCALAR, got %s\n", fy_event_type_get_text(fye->type));

	fyt = fy_event_get_token(fye);
	text = fy_token_get_text(fyt, &len);
	RP_ASSERT(text != NULL);

	str_instance_data_setup(id, rw);

	lenz = len + !id->not_null_terminated;

	RP_INPUT_CHECK(lenz <= rtd->ti->count,
		       "string size too large to fit char[%zu] (was %zu)",
		       rtd->ti->count, len);

	if (id->rfd_counter) {
		rc = integer_field_store_check(id->rfd_counter, lenz);
		RP_INPUT_CHECK(!rc, "cannot store length %zu to counter", lenz);
	}

	if (!(flags & RPF_NO_STORE)) {
		memcpy(data, text, len);
		if (len < rtd->ti->count)
			memset(data + len, 0, rtd->ti->count - len);

		if (id->rfd_counter)
			struct_integer_field_store(id->rw_struct, id->rfd_counter, len);
	}

	rc = 0;

out:
	fy_parser_event_free(fyp, fye);
	return rc;

err_out:
	rc = -1;
	goto out;

err_input:
	rc = -EINVAL;
	goto out;
}

static int
const_array_char_emit(struct fy_emitter *emit, struct reflection_walker *rw,
		      enum reflection_emit_flags flags)
{
	struct reflection_type_data *rtd, *rtd_dep;
	enum fy_type_kind type_kind __attribute__((unused));
	struct str_instance_data id_local, *id = &id_local;
	const char *text;
	size_t len;
	int rc;

	rtd = rw->rtd;

	/* verify alignment */
	type_kind = rtd->ti->kind;
	assert(type_kind == FYTK_CONSTARRAY);

	rtd_dep = rtd->rtd_dep;
	assert(rtd_dep);

	type_kind = rtd_dep->ti->kind;
	assert(type_kind == FYTK_CHAR);

	text = rw->data;

	str_instance_data_setup(id, rw);

	len = id->rfd_counter ? struct_integer_field_load(id->rw_struct, id->rfd_counter) :
	      !id->not_null_terminated ? strnlen(text, rtd->ti->count) : rtd->ti->count;
	RE_ASSERT(len <= rtd->ti->count);

	rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_ANY, text, len, NULL, NULL);
	RE_ASSERT(!rc);

	return 0;
err_out:
	return -1;
}

static const struct reflection_type_ops const_array_char_ops = {
	.name = "const_array_char",
	.parse = const_array_char_parse,
	.emit = const_array_char_emit,
};

static int
struct_parse(struct fy_parser *fyp, struct reflection_walker *rw,
	     enum reflection_parse_flags flags)
{
	struct struct_instance_data id_local, *id = &id_local, *id_parent;
	struct fy_parser_checkpoint *fypchk = NULL;
	struct fy_event *fye = NULL;
	const struct fy_event *fye_peek;
	struct reflection_type_data *rtd, *rtdf;
	struct reflection_field_data *rfd, *rfd_flatten;
	struct reflection_field_data *rfd_selector;
	struct reflection_walker rw_field, rw_selector, rw_tmp;
	struct fy_token *fyt_key;
	const char *field;
	int i, rc, field_start_idx, field_end_idx, union_field_idx = -1;
	enum fy_event_type match_type;
	bool input_error, is_union, required, skip_unknown, field_auto_select;
	void *value;

	rtd = rw->rtd;
	assert(rtd);

	assert(rtd->ti->kind == FYTK_STRUCT || rtd->ti->kind == FYTK_UNION);

	rc = struct_instance_data_setup(id, rw);
	RP_ASSERT(!rc);

	is_union = rtd->ti->kind == FYTK_UNION;

	memset(&rw_field, 0, sizeof(rw_field));
	rw_field.parent = rw;

	id_parent = NULL;
	rfd = NULL;

	field = NULL;

	rfd_selector = NULL;

	if (rtd->flat_field_idx >= 0) {

		RP_ASSERT(rtd->flat_field_idx < rtd->fields_count);
		rfd = rtd->fields[rtd->flat_field_idx];

	} else if (rtd->ti->flags & FYTIF_ANONYMOUS_RECORD_DECL) {

		/* get parent struct's stashed anonymous_field_name */
		if (!id_parent)
			id_parent = struct_get_parent_struct_id(rw);
		RP_ASSERT(id_parent);

		field = id_parent->anonymous_field_name;
		RP_ASSERT(field);

		rfd = reflection_type_data_lookup_next_anonymous_field(rtd, field);
		RP_INPUT_CHECK(rfd != NULL,
			       "anonymous struct T%d/'%s' has no field '%s'",
			       rtd->idx, rtd->ti->name, field);
	}

	/* no direct field, but there's an auto-select */
	field_auto_select = reflection_meta_get_bool(rtd->meta, rmvid_field_auto_select);
	if (field_auto_select) {

		RP_ASSERT(!rfd);

		fypchk = fy_parser_checkpoint_create(fyp);
		RP_ASSERT(fypchk);

		memset(rw->data, 0, rw->data_size.bytes);

		rfd = NULL;
		for (i = 0; i < rtd->fields_count; i++) {
			rfd = rtd->fields[i];

			rtdf = rfd_rtd(rfd);
			assert(rtdf);

			if (reflection_meta_get_bool(rtdf->meta, rmvid_match_always))
				break;

			/* short circuit */
			if (reflection_meta_get_bool(rtdf->meta, rmvid_match_scalar))
				match_type = FYET_SCALAR;
			else if (reflection_meta_get_bool(rtdf->meta, rmvid_match_seq))
				match_type = FYET_SEQUENCE_START;
			else if (reflection_meta_get_bool(rtdf->meta, rmvid_match_map))
				match_type = FYET_MAPPING_START;
			else
				match_type = FYET_NONE;

			if (match_type != FYET_NONE) {
				fye_peek = fy_parser_parse_peek(fyp);
				if (fye_peek && fye_peek->type == match_type)
					break;
			}

			/* save the anonymous field name */
			if (rfd->fi->type_info->flags & FYTIF_ANONYMOUS_RECORD_DECL) {

				if (!id_parent)
					id_parent = struct_get_parent_struct_id(rw);
				RP_ASSERT(id_parent);

				field = id_parent->anonymous_field_name;
				id->anonymous_field_name = field;
			}

			struct_setup_field_rw(&rw_field, rfd);

			rc = reflection_parse_rw(fyp, &rw_field,
						 flags | RPF_NO_STORE | RPF_SILENT_INVALID_INPUT);
			input_error = rc == -EINVAL;

			RP_ASSERT(!rc || input_error);

			/* always rollback */
			rc = fy_parser_rollback(fyp, fypchk);
			RP_ASSERT(!rc);

			if (!input_error)
				break;

			field = NULL;
		}

		fy_parser_checkpoint_destroy(fypchk);
		fypchk = NULL;

		if (rfd == NULL)
			fye = fy_parser_parse(fyp);
		RP_INPUT_CHECK(rfd != NULL,
			       "Unable to find field to auto-match for T#%d/%s\n",
			       rtd->idx, rtd->ti->name);
	}

	/* flatten, directly calling parse */
	if (rfd) {
		rfd_flatten = rfd;

		/* union, must select the field */
		if (is_union) {
			if (!id_parent)
				id_parent = struct_get_parent_struct_id(rw);
			RP_ASSERT(id_parent);

			RP_ASSERT(!struct_is_field_present(id_parent, rtd->selector_field_idx));

			RP_ASSERT(rtd->selector_field_idx >= 0);
			RP_ASSERT(rtd->selector_field_idx < rw->parent->rtd->fields_count);

			rfd_selector = rw->parent->rtd->fields[rtd->selector_field_idx];

			memset(&rw_selector, 0, sizeof(rw_selector));
			rw_selector.parent = rw->parent;
			struct_setup_field_rw(&rw_selector, rfd_selector);

			value = reflection_meta_generate_any_value(rfd_rtd(rfd)->meta, rmvid_select, rfd_rtd(rfd_selector));
			RP_ASSERT(value);

			if (!(flags & RPF_NO_STORE)) {
				rc = reflection_copy_rw(&rw_selector, reflection_rw_value(&rw_tmp, rfd_selector->rtd, value));
				RP_ASSERT(!rc);
			}

			struct_set_field_present(id_parent, rtd->selector_field_idx);

			rfd_selector = NULL;
		}

	} else {
		rfd_flatten = NULL;

		/* if it's a union, there must be a union selector */
		if (is_union) {
			if (!id_parent)
				id_parent = struct_get_parent_struct_id(rw);
			RP_ASSERT(id_parent);

			RP_ASSERT(!struct_is_field_present(id_parent, rtd->selector_field_idx));

			RP_ASSERT(rtd->selector_field_idx >= 0);
			RP_ASSERT(rtd->selector_field_idx < rw->parent->rtd->fields_count);

			rfd_selector = rw->parent->rtd->fields[rtd->selector_field_idx];
		}

		fye = fy_parser_parse(fyp);
		RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

		RP_INPUT_CHECK(fye->type == FYET_MAPPING_START,
			       "illegal event type (expecting mapping start)");

		rw->flags |= RWF_MAP;

		field = NULL;
	}

	for (;;) {
		if (!rfd_flatten) {
			fy_parser_event_free(fyp, fye);
			fye = fy_parser_parse(fyp);
			RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

			/* finish? */
			if (fye->type == FYET_MAPPING_END)
				break;

			/* key must be scalar */
			RP_INPUT_CHECK(fye->type == FYET_SCALAR,
				       "struct '%s'/#%d expects scalar key",
				       fy_type_info_prefixless_name(rtd->ti), rtd->idx);

			fyt_key = fy_event_get_token(fye);

			/* get the field */
			field = fy_token_get_text0(fyt_key);
			RP_ASSERT(field != NULL);

			rfd = reflection_type_data_lookup_field(rtd, field);
			if (!rfd && rtd->has_anonymous_field)
				rfd = reflection_type_data_lookup_next_anonymous_field(rtd, field);

			if (!rfd) {
				/* if we're allowing skipping... */
				skip_unknown = reflection_meta_get_bool(rtd->meta, rmvid_skip_unknown);
				RP_INPUT_CHECK(skip_unknown,
					       "no field '%s' found in '%s'",
					       field, rtd->ti->name);

				rc = fy_parser_skip(fyp);
				RP_ASSERT(!rc);

				continue;
			}
		} else {
			rfd = rfd_flatten;
			fye = NULL;
		}

		RP_INPUT_CHECK(!struct_is_field_present(id, rfd->idx) ||
			       (rfd->fi->type_info->flags & FYTIF_ANONYMOUS_RECORD_DECL),
			       "duplicate field '%s' found in '%s'",
			       field ? field : rfd->field_name, rtd->ti->name);

		RP_INPUT_CHECK(!rfd->is_selector,
			       "selector field '%s' in '%s' is not directly settable",
				field ? field : rfd->field_name, rtd->ti->name);

		RP_INPUT_CHECK(!(is_union && union_field_idx >= 0),
			       "multiple union field cannot be set '%s' found in '%s'",
			       field ? field : rfd->field_name, rtd->ti->name);

		/* save the anonymous field name */
		if (rfd->fi->type_info->flags & FYTIF_ANONYMOUS_RECORD_DECL) {
			assert(field);
			id->anonymous_field_name = field;
		}

		rw_field.flags = RWF_VALUE;
		struct_setup_field_rw(&rw_field, rfd);

		if (field && !(rfd->fi->type_info->flags & FYTIF_ANONYMOUS_RECORD_DECL)) {
			rw_field.flags |= RWF_TEXT_KEY;
			rw_field.text_key = field;
		}

		rc = reflection_parse_rw(fyp, &rw_field, flags);
		if (rc)
			goto out;

		struct_set_field_present(id, rfd->idx);

		if (is_union)
			union_field_idx = rfd->idx;

		if (rfd_flatten)
			break;
	}

	RP_INPUT_CHECK(!(is_union && union_field_idx < 0),
		       "no union data was found");

	if (!is_union) {
		field_start_idx = 0;
		field_end_idx = rtd->fields_count;
	} else {
		field_start_idx = union_field_idx;
		RP_INPUT_CHECK(field_start_idx >= 0,
			       "no selected union field was found");

		field_end_idx = field_start_idx + 1;

		/* union and with a field selector, apply it */
		if (rfd_selector) {
			memset(&rw_selector, 0, sizeof(rw_selector));
			rw_selector.parent = rw->parent;
			struct_setup_field_rw(&rw_selector, rfd_selector);

			value = reflection_meta_generate_any_value(rfd_rtd(rfd)->meta, rmvid_select, rfd_rtd(rfd_selector));
			RP_ASSERT(value);

			if (!(flags & RPF_NO_STORE)) {
				rc = reflection_copy_rw(&rw_selector, reflection_rw_value(&rw_tmp, rfd_selector->rtd, value));
				RP_ASSERT(!rc);
			}

			struct_set_field_present(id_parent, rtd->selector_field_idx);
		}
	}

	for (i = field_start_idx; i < field_end_idx; i++) {

		rfd = rtd->fields[i];
		rtdf = rfd_rtd(rfd);

		/* fill-in-default */
		if (!struct_is_field_present(id, i) &&
		    (value = reflection_meta_generate_any_value(rtdf->meta,
								rmvid_default, rtdf)) != NULL) {

			if (!(flags & RPF_NO_STORE)) {
				rw_field.flags = RWF_VALUE;
				struct_setup_field_rw(&rw_field, rfd);

				rc = reflection_copy_rw(&rw_field,
						reflection_rw_value(&rw_tmp, rtdf, value));
				RP_ASSERT(!rc);
			}

			struct_set_field_present(id, i);
		}

		required = !rfd_is_anonymous_bitfield(rfd) &&
			   !rfd->is_counter &&
			   reflection_meta_get_bool(rfd_rtd(rfd)->meta, rmvid_required);

		RP_INPUT_CHECK(!required || struct_is_field_present(id, i),
				"missing required field '%s' of '%s'",
				rfd->field_name, rtd->ti->name);
	}
	rc = 0;

out:
	fy_parser_event_free(fyp, fye);
	struct_instance_data_cleanup(id);
	return rc;

err_out:
	rc = -1;
	goto out;

err_input:
	rc = -EINVAL;
	goto out;
}

static int
struct_get_union_selection_index(struct reflection_walker *rw)
{
	struct reflection_type_data *rtd, *rtd_parent;
	struct reflection_field_data *rfd, *rfd_selector;
	struct reflection_type_data *rtdf;
	struct reflection_walker rw_field, rw_tmp;
	void *value;
	int i;

	assert(rw);
	assert(rw->data);

	rtd = rw->rtd;
	if (rtd->ti->kind != FYTK_UNION)
		return -1;

	rtd_parent = rw->parent ? rw->parent->rtd : NULL;

	if (!rtd_parent)
		return -1;

	assert(rtd_parent);
	assert(rtd->rtd_selector);

	assert(rtd->selector_field_idx < rtd_parent->fields_count);
	rfd_selector = rtd_parent->fields[rtd->selector_field_idx];
	assert(rfd_selector);

	assert(rtd->union_field_idx < rtd_parent->fields_count);

	/* point to the selector */
	memset(&rw_field, 0, sizeof(rw_field));
	rw_field.parent = rw->parent;
	struct_setup_field_rw(&rw_field, rfd_selector);

	/* iterate until there's a match */
	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];
		rtdf = rfd_rtd(rfd);
		value = reflection_meta_generate_any_value(rtdf->meta, rmvid_select, rfd_rtd(rfd_selector));
		if (reflection_eq_rw(&rw_field, reflection_rw_value(&rw_tmp, rfd_selector->rtd, value)) > 0)
			return i;
	}
	return -1;
}

static int
struct_emit(struct fy_emitter *emit, struct reflection_walker *rw,
	    enum reflection_emit_flags flags)
{
	struct reflection_walker rw_field;
	struct reflection_meta *rmf;
	struct reflection_type_data *rtd, *rtdf;
	struct reflection_field_data *rfd, *rfd_flatten;
	const struct fy_field_info *fi;
	int i, field_start_idx, field_end_idx, union_field_idx;
	int rc;
	bool is_union, emit_start_end, emit_key;
	struct reflection_any_value *rav_default;

	rtd = rw->rtd;

	is_union = rtd->ti->kind == FYTK_UNION;
	if (is_union) {
		union_field_idx = struct_get_union_selection_index(rw);
		RE_ASSERT(union_field_idx >= 0);
	} else
		union_field_idx = -1;

	if (rtd->flat_field_idx >= 0) {
		RE_ASSERT(rtd->flat_field_idx >= 0 && rtd->flat_field_idx < rtd->fields_count);
		rfd_flatten = rtd->fields[rtd->flat_field_idx];
	} else if (is_union && reflection_meta_get_bool(rtd->meta, rmvid_field_auto_select)) {
		RE_ASSERT(union_field_idx >= 0 && union_field_idx < rtd->fields_count);
		rfd_flatten = rtd->fields[union_field_idx];
	} else
		rfd_flatten = NULL;

	emit_start_end = !rfd_flatten && !(rtd->ti->flags & FYTIF_ANONYMOUS_RECORD_DECL);

	if (rfd_flatten) {
		field_start_idx = rfd_flatten->idx;
		field_end_idx = field_start_idx + 1;
	} else if (!is_union) {
		field_start_idx = 0;
		field_end_idx = rtd->fields_count;
	} else {
		field_start_idx = union_field_idx;
		RE_ASSERT(field_start_idx >= 0);
		field_end_idx = field_start_idx + 1;
	}

	if (emit_start_end) {
		rc = fy_emit_eventf(emit, FYET_MAPPING_START, FYNS_ANY, NULL, NULL);
		RE_ASSERT(!rc);
	}

	memset(&rw_field, 0, sizeof(rw_field));
	rw_field.parent = rw;

	for (i = field_start_idx; i < field_end_idx; i++) {

		rfd = rtd->fields[i];

		fi = rfd->fi;

		/* unnamed field (bitfield) */
		if (fi->name[0] == '\0')
			continue;

		rtdf = rfd_rtd(rfd);
		rmf = rtdf ? rtdf->meta : NULL;

		/* field that should not appear */
		if (reflection_meta_get_bool(rmf, rmvid_omit_on_emit))
			continue;

		if (rfd->is_counter || rfd->is_selector)
			continue;

		/* omit if pointer and NULL (can't omit if no key is output) */
		if (reflection_meta_get_bool(rmf, rmvid_omit_if_null) &&
		    struct_field_is_ptr_null(rw, rfd))
			continue;

		rw_field.flags = RWF_VALUE | RWF_TEXT_KEY;
		struct_setup_field_rw(&rw_field, rfd);

		/* omit if default value */
		if (reflection_meta_get_bool(rmf, rmvid_omit_if_default) &&
		   (rav_default = reflection_meta_get_any_value(rmf, rmvid_default)) != NULL) {

			rc = reflection_any_value_equal_rw(rav_default, &rw_field);
			RE_ASSERT(rc >= 0);

			if (rc > 0)	/* equal */
				continue;
		}

		emit_key = !rfd_flatten && !(rfd->fi->type_info->flags & FYTIF_ANONYMOUS_RECORD_DECL);
		if (emit_key) {
			rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_ANY, rfd->field_name, FY_NT, NULL, NULL);
			RE_ASSERT(!rc);
		}

		rc = reflection_emit_rw(emit, &rw_field, flags);
		RE_ASSERT(!rc);
	}

	if (emit_start_end) {
		rc = fy_emit_eventf(emit, FYET_MAPPING_END);
		RE_ASSERT(!rc);
	}

	return 0;

err_out:
	return -1;
}

static void
struct_dtor(struct reflection_walker *rw)
{
	struct reflection_type_data *rtd;
	struct reflection_field_data *rfd;
	struct reflection_walker rw_field;
	int i, field_start_idx, field_end_idx;
	bool is_union;

	rtd = rw->rtd;

	is_union = rtd->ti->kind == FYTK_UNION;

	if (!is_union) {
		field_start_idx = 0;
		field_end_idx = rtd->fields_count;
	} else {
		field_start_idx = struct_get_union_selection_index(rw);
		assert(field_start_idx >= 0);

		field_end_idx = field_start_idx + 1;
	}

	memset(&rw_field, 0, sizeof(rw_field));
	rw_field.parent = rw;

	for (i = field_start_idx; i < field_end_idx; i++) {

		rfd = rtd->fields[i];

		struct_setup_field_rw(&rw_field, rfd);
		reflection_dtor_rw(&rw_field);
	}
}

enum struct_call2_type {
	SC2T_COPY,
	SC2T_CMP,
	SC2T_EQ,
};

static int
struct_call2(struct reflection_walker *rw_a, struct reflection_walker *rw_b,
	     enum struct_call2_type call_type)
{
	struct reflection_type_data *rtd;
	struct reflection_field_data *rfd;
	struct reflection_walker rw_field_a, rw_field_b;
	int rc, i, field_start_idx, field_end_idx, union_select_a, union_select_b;
	bool is_union;

	rtd = rw_b->rtd;
	assert(rtd);
	assert(rtd == rw_a->rtd);

	/* pure? can just be mem'ed for copy and eq */
	if ((rtd->flags & RTDF_PURITY_MASK) == 0) {
		switch (call_type) {
		case SC2T_COPY:
			memcpy(rw_a->data, rw_b->data, rtd->ti->size);
			return 0;
		case SC2T_EQ:
			return !memcmp(rw_a->data, rw_b->data, rtd->ti->size);
		default:
			break;
		}
	}

	is_union = rtd->ti->kind == FYTK_UNION;

	if (!is_union) {
		field_start_idx = 0;
		field_end_idx = rtd->fields_count;
	} else {
		union_select_a = struct_get_union_selection_index(rw_a);
		assert(union_select_a >= 0);

		union_select_b = struct_get_union_selection_index(rw_b);
		assert(union_select_b >= 0);

		/* unions, but with different fields selected */
		if (union_select_a != union_select_b)
			return -1;

		field_start_idx = union_select_a;
		field_end_idx = field_start_idx + 1;
	}

	memset(&rw_field_a, 0, sizeof(rw_field_a));
	rw_field_a.parent = rw_a;

	memset(&rw_field_b, 0, sizeof(rw_field_b));
	rw_field_b.parent = rw_b;

	rc = 0;
	for (i = field_start_idx; i < field_end_idx; i++) {

		rfd = rtd->fields[i];

		struct_setup_field_rw(&rw_field_a, rfd);
		struct_setup_field_rw(&rw_field_b, rfd);

		switch (call_type) {
		case SC2T_COPY:
			rc = reflection_copy_rw(&rw_field_a, &rw_field_b);
			break;
		case SC2T_CMP:
			rc = reflection_cmp_rw(&rw_field_a, &rw_field_b);
			break;
		case SC2T_EQ:
			rc = reflection_eq_rw(&rw_field_a, &rw_field_b);
			break;
		default:
			rc = -1;
			break;
		}
		if (rc)
			break;
	}

	return rc;
}

static int
struct_copy(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	return struct_call2(rw_dst, rw_src, SC2T_COPY);
}

static int
struct_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return struct_call2(rw_a, rw_b, SC2T_CMP);
}

static int
struct_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return struct_call2(rw_a, rw_b, SC2T_EQ);
}

static int
enum_parse_single_scalar(struct fy_parser *fyp, struct reflection_walker *rw,
			 enum reflection_parse_flags flags, union integer_scalar *valp,
			 struct reflection_type_data *rtd_enum)
{
	struct fy_event *fye = NULL;
	struct reflection_field_data *rfd;
	const char *text0;
	union integer_scalar mini, maxi;
	int rc;

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

	RP_INPUT_CHECK(fye->type == FYET_SCALAR,
		       "expecting FYET_SCALAR, got %s\n", fy_event_type_get_text(fye->type));

	text0 = fy_token_get_text0(fy_event_get_token(fye));
	RP_ASSERT(text0 != NULL);

	rfd = reflection_type_data_lookup_field(rtd_enum, text0);
	RP_INPUT_CHECK(rfd != NULL,
			"No enumeration value named %s in '%s'",
			text0, rw->rtd->ti->name);

	if (rtd_is_signed(rw->rtd))
		valp->sval = rfd->fi->sval;
	else
		valp->uval = rfd->fi->uval;

	rc = store_integer_scalar_check_rw(rw, *valp, &mini, &maxi);
	RP_INPUT_CHECK(!(rc == -ERANGE && rtd_is_signed(rw->rtd)),
			"%s value %s (%jd) cannot fit (min=%jd, max=%jd)",
			rtd_enum->ti->name, rfd->field_name, valp->sval, mini.sval, maxi.sval);
	RP_INPUT_CHECK(!(rc == -ERANGE && rtd_is_unsigned(rw->rtd)),
			"%s value %s (%ju) cannot fit (max=%ju)",
			rtd_enum->ti->name, rfd->field_name, valp->uval, maxi.uval);

	rc = 0;
out:
	fy_parser_event_free(fyp, fye);
	return rc;

err_out:
	rc = -1;
	goto out;

err_input:
	rc = -EINVAL;
	goto out;
}

static int
enum_parse(struct fy_parser *fyp, struct reflection_walker *rw,
	   enum reflection_parse_flags flags)
{
	const struct fy_event *fye_peek = NULL;
	struct fy_event *fye = NULL;
	struct reflection_walker rw_num;
	union integer_scalar val, tval, mini, maxi;
	int rc;

	assert(rw->rtd);
	assert(rw->rtd->rtd_dep);

	fye_peek = fy_parser_parse_peek(fyp);
	RP_INPUT_CHECK(fye_peek != NULL, "premature end of input\n");
	RP_INPUT_CHECK(fye_peek->type == FYET_SCALAR || fye_peek->type == FYET_SEQUENCE_START,
			"enum illegal event\n");

	if (fye_peek->type == FYET_SCALAR) {
		rc = enum_parse_single_scalar(fyp, rw, flags, &val, rw->rtd);
		if (rc)
			goto out;
	} else {
		RP_ASSERT(fye_peek->type == FYET_SEQUENCE_START);
		RP_INPUT_CHECK(reflection_meta_get_bool(rw->rtd->meta, rmvid_enum_or_seq),
				"enum-or-seq but not marked as such");

		fye = fy_parser_parse(fyp);
		RP_ASSERT(fye && fye->type == FYET_SEQUENCE_START);
		fy_parser_event_free(fyp, fye);
		fye = NULL;

		val.uval = 0;
		while ((fye_peek = fy_parser_parse_peek(fyp)) != NULL && fye_peek->type == FYET_SCALAR) {
			rc = enum_parse_single_scalar(fyp, rw, flags, &tval, rw->rtd);
			if (rc)
				goto out;

			if (rtd_is_signed(rw->rtd))
				val.sval |= tval.sval;
			else
				val.uval |= tval.uval;
		}

		RP_INPUT_CHECK(fye_peek && fye_peek->type == FYET_SEQUENCE_END,
				"enum sequence not ended correctly");

		fye = fy_parser_parse(fyp);
		RP_ASSERT(fye && fye->type == FYET_SEQUENCE_END);
		fy_parser_event_free(fyp, fye);
		fye = NULL;
	}

	rc = store_integer_scalar_check_rw(rw, val, &mini, &maxi);
	RP_INPUT_CHECK(!(rc == -ERANGE && rtd_is_signed(rw->rtd)),
			"%s (%jd) cannot fit (min=%jd, max=%jd)",
			rw->rtd->ti->name, val.sval, mini.sval, maxi.sval);
	RP_INPUT_CHECK(!(rc == -ERANGE && rtd_is_unsigned(rw->rtd)),
			"%s (%ju) cannot fit (max=%ju)",
			rw->rtd->ti->name, val.uval, maxi.uval);

	if (!(flags & RPF_NO_STORE))
		store_integer_scalar_no_check_rw((rw->flags & RWF_BITFIELD_DATA) ?
				rw : reflection_rw_dep(&rw_num, rw), val);

out:
	fy_parser_event_free(fyp, fye);
	return rc;

err_input:
	rc = -EINVAL;
	goto out;

err_out:
	rc = -1;
	goto out;
}

static int
enum_emit_single_scalar(struct fy_emitter *emit, struct reflection_walker *rw,
			enum reflection_emit_flags flags, union integer_scalar val,
			struct reflection_type_data *rtd_enum)
{
	struct reflection_field_data *rfd;
	int rc;

	rfd = reflection_type_data_lookup_field_by_scalar_enum_value(rtd_enum, val,
			rtd_is_signed(rw->rtd));
	if (!rfd)
		return -1;

	rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_ANY, rfd->field_name, FY_NT, NULL, NULL);
	RE_ASSERT(!rc);

	return 0;
err_out:
	return -1;
}

static int
enum_emit(struct fy_emitter *emit, struct reflection_walker *rw,
	  enum reflection_emit_flags flags)
{
	struct reflection_walker rw_num;
	struct reflection_field_data *rfd;
	union integer_scalar val, tval;
	bool is_signed;
	int i, rc;

	assert(rw->rtd);
	assert(rw->rtd->rtd_dep);

	is_signed = rtd_is_signed(rw->rtd);

	val = load_integer_scalar_rw((rw->flags & RWF_BITFIELD_DATA) ?
			rw : reflection_rw_dep(&rw_num, rw));

	/* can do it directly? do it */
	rfd = reflection_type_data_lookup_field_by_scalar_enum_value(rw->rtd, val, is_signed);
	if (rfd) {
		rc = enum_emit_single_scalar(emit, rw, flags, val, rw->rtd);
		RE_ASSERT(!rc);
		goto out;
	}

	RE_OUTPUT_CHECK(reflection_meta_get_bool(rw->rtd->meta, rmvid_enum_or_seq),
			"enum-or-seq but not marked as such");

	rc = fy_emit_eventf(emit, FYET_SEQUENCE_START, FYNS_FLOW, NULL, NULL);
	RE_ASSERT(!rc);

	for (;;) {
		if (is_signed) {
			if (!val.sval)
				break;
		} else {
			if (!val.uval)
				break;
		}

		rfd = NULL;
		tval.uval = 0;
		for (i = 0; i < rw->rtd->fields_count; i++) {
			rfd = rw->rtd->fields[i];
			if (is_signed) {
				tval.sval = rfd->fi->sval;
				if (tval.sval && (val.sval & tval.sval) == tval.sval)
					break;
			} else {
				tval.uval = rfd->fi->uval;
				if (tval.uval && (val.uval & tval.uval) == tval.uval)
					break;
			}
		}

		RE_OUTPUT_CHECK(i < rw->rtd->fields_count, "unable to deduce enum sequence");
		RE_ASSERT(rfd);

		rc = enum_emit_single_scalar(emit, rw, flags, tval, rw->rtd);
		RE_ASSERT(!rc);

		if (is_signed)
			val.sval &= ~tval.sval;
		else
			val.uval &= ~tval.uval;
	}

	rc = fy_emit_eventf(emit, FYET_SEQUENCE_END, FYNS_ANY, NULL, NULL);
	RE_ASSERT(!rc);

	rc = 0;

out:
	return rc;

err_out:
	rc = -1;
	goto out;

err_output:
	rc = -EINVAL;
	goto out;
}

static int
enum_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	struct reflection_walker rw_num_a, rw_num_b;
	union integer_scalar val_a, val_b;
	int rc;

	assert(rw_a);
	assert(rw_b);
	assert(rw_a->rtd);
	assert(rw_a->rtd == rw_b->rtd);

	val_a = load_integer_scalar_rw((rw_a->flags & RWF_BITFIELD_DATA) ?
			rw_a : reflection_rw_dep(&rw_num_a, rw_a));
	val_b = load_integer_scalar_rw((rw_b->flags & RWF_BITFIELD_DATA) ?
			rw_b : reflection_rw_dep(&rw_num_b, rw_b));

	if (rtd_is_signed(rw_a->rtd))
		rc = val_a.sval > val_b.sval ?  1 :
		     val_a.sval < val_b.sval ? -1 : 0;
	else
		rc = val_a.uval > val_b.uval ?  1 :
		     val_a.uval < val_b.uval ? -1 : 0;
	return rc;
}

static int
enum_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return !enum_cmp(rw_a, rw_b);
}

static int
enum_copy(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	struct reflection_walker rw_num_dst, rw_num_src;
	union integer_scalar val;

	assert(rw_dst);
	assert(rw_src);
	assert(rw_dst->rtd);
	assert(rw_dst->rtd == rw_src->rtd);

	val = load_integer_scalar_rw((rw_src->flags & RWF_BITFIELD_DATA) ?
			rw_src : reflection_rw_dep(&rw_num_src, rw_src));
	return store_integer_scalar_rw((rw_dst->flags & RWF_BITFIELD_DATA) ?
			rw_dst : reflection_rw_dep(&rw_num_dst, rw_dst), val);
}

static inline bool text_is_null(struct fy_parser *fyp, const char *text, size_t len)
{
	return (len == 1 && *text == '~') ||
	       (len == 4 && (!memcmp(text, "null", 4) || !memcmp(text, "Null", 4) || !memcmp(text, "NULL", 4)));
}

static inline bool fy_event_is_null(struct fy_parser *fyp, struct fy_event *fye)
{
	struct fy_token *fyt;
	const char *text;
	size_t len;

	if (fye->type != FYET_SCALAR || (fyt = fy_event_get_token(fye)) == NULL || fy_token_scalar_style(fyt) != FYSS_PLAIN)
		return false;

	text = fy_token_get_text(fyt, &len);
	if (!text)
		return false;

	return text_is_null(fyp, text, len);
}

static int
emit_ptr_NULL(struct fy_emitter *emit, struct reflection_walker *rw)
{
	int rc;

	rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_PLAIN, "null", 4, NULL, NULL);
	RE_ASSERT(!rc);

	return 0;

err_out:
	return -1;
}

static int
ptr_parse(struct fy_parser *fyp, struct reflection_walker *rw,
	  enum reflection_parse_flags flags)
{
	struct fy_event *fye = NULL;
	const struct fy_event *fye_peek = NULL;
	struct reflection_type_data *rtd, *rtd_dep;
	void *data, *p = NULL;
	struct reflection_walker rw_dep;
	int rc;

	rtd = rw->rtd;
	assert(rtd);

	rtd_dep = rtd->rtd_dep;
	assert(rtd_dep);

	RP_INPUT_CHECK(!(rtd_dep->ti->flags & FYTIF_UNRESOLVED),
			"Pointer to unresolved type '%s' cannot be serialized",
			rtd_dep->ti->name);

	assert(!(rw->flags & RWF_BITFIELD_DATA));
	data = rw->data;

	if (reflection_meta_get_bool(rtd->meta, rmvid_null_allowed)) {
		/* check if next event is NULL, directly set the pointer */
		fye_peek = fy_parser_parse_peek(fyp);
		RP_INPUT_CHECK(fye_peek != NULL,
			       "premature end of input while scanning for ptr\n");
		if (fy_event_is_null(fyp, (struct fy_event *)fye_peek)) {
			fye = fy_parser_parse(fyp);
			fy_parser_event_free(fyp, fye);
			return 0;
		}
	}

	p = reflection_type_data_alloc(rtd_dep);
	RP_ASSERT(p != NULL);

	memset(&rw_dep, 0, sizeof(rw_dep));
	rw_dep.parent = rw;
	rw_dep.rtd = rtd_dep;
	rw_dep.data = p;
	rw_dep.data_size.bytes = rtd_dep->ti->size;

	rc = reflection_parse_rw(fyp, &rw_dep, flags);
	if (rc)
		goto out;

	if (!(flags & RPF_NO_STORE))
		*(void **)data = p;
	else
		reflection_type_data_free(rtd_dep, p);

	return 0;

out:
	if (p)
		reflection_type_data_free(rtd_dep, p);
	return rc;

err_out:
	rc = -1;
	goto out;

err_input:
	rc = -EINVAL;
	goto out;
}

static void
ptr_dtor(struct reflection_walker *rw)
{
	struct reflection_walker rw_dep;
	void *ptr;

	ptr = *(void **)rw->data;
	if (!ptr)
		return;
	*(void **)rw->data = NULL;

	memset(&rw_dep, 0, sizeof(rw_dep));
	rw_dep.parent = rw;
	rw_dep.rtd = rw->rtd->rtd_dep;
	rw_dep.data = ptr;
	rw_dep.data_size.bytes = rw->rtd->rtd_dep->ti->size;

	reflection_free_rw(&rw_dep);
}

static int
ptr_copy(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	struct reflection_walker *rw;
	struct reflection_type_data *rtd = NULL, *rtd_dep;
	struct reflection_walker rw_dep_dst, rw_dep_src;
	void *src = NULL, *dst = NULL;
	int rc;

	assert(rw_dst);
	assert(rw_dst->data);
	assert(rw_src);
	assert(rw_src->data);

	rw = rw_src;

	RW_ASSERT(rw_src->rtd);
	RW_ASSERT(rw_dst->rtd == rw_src->rtd);

	rtd = rw_src->rtd;
	rtd_dep = rtd->rtd_dep;

	src = *(void **)rw_src->data;

	*(void **)rw_dst->data = NULL;

	if (!src)
		return 0;

	dst = reflection_type_data_alloc(rtd);
	RW_ASSERT(dst);

	memset(&rw_dep_dst, 0, sizeof(rw_dep_dst));
	memset(&rw_dep_src, 0, sizeof(rw_dep_src));

	rw_dep_dst.rtd = rw_dep_src.rtd = rtd_dep;
	rw_dep_dst.data_size.bytes = rw_dep_src.data_size.bytes = rtd_dep->ti->size;

	rw_dep_dst.parent = rw_dst;
	rw_dep_dst.data = dst;

	rw_dep_src.parent = rw_src;
	rw_dep_src.data = src;

	rc = reflection_copy_rw(&rw_dep_dst, &rw_dep_src);
	RW_ASSERT(!rc);

	*(void **)rw_dst->data = dst;

	return 0;

err_out:
	reflection_type_data_free(rtd, dst);
	return -1;
}

enum pointer_call2_type {
	PC2T_CMP,
	PC2T_EQ,
};

static int
ptr_call2(struct reflection_walker *rw_a, struct reflection_walker *rw_b,
	  enum pointer_call2_type call_type)
{
	struct reflection_type_data *rtd;
	struct reflection_walker rw_dep_a, rw_dep_b;
	void *a, *b;
	int rc;

	assert(rw_a);
	assert(rw_a->data);
	assert(rw_b);
	assert(rw_b->data);

	a = *(void **)rw_a->data;
	b = *(void **)rw_b->data;

	if (a == b)
		return 0;

	if (!a || !b)
		return -1;

	rtd = rw_b->rtd;
	assert(rtd);
	assert(rw_a->rtd == rtd);

	memset(&rw_dep_a, 0, sizeof(rw_dep_a));
	memset(&rw_dep_b, 0, sizeof(rw_dep_b));

	rw_dep_a.rtd = rw_dep_b.rtd = rtd->rtd_dep;
	rw_dep_b.data_size.bytes = rtd->rtd_dep->ti->size;

	rw_dep_a.parent = rw_a;
	rw_dep_a.data = a;

	rw_dep_b.parent = rw_b;
	rw_dep_b.data = b;

	switch (call_type) {
	case PC2T_CMP:
		rc = reflection_cmp_rw(&rw_dep_a, &rw_dep_b);
		break;
	case PC2T_EQ:
		rc = reflection_eq_rw(&rw_dep_a, &rw_dep_b);
		break;
	default:
		rc = -1;
		break;
	}
	return rc;
}

static int
ptr_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return ptr_call2(rw_a, rw_b, PC2T_CMP);
}

static int
ptr_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return ptr_call2(rw_a, rw_b, PC2T_EQ);
}

static int
ptr_char_parse(struct fy_parser *fyp, struct reflection_walker *rw,
	       enum reflection_parse_flags flags)
{
	struct fy_event *fye = NULL;
	struct fy_token *fyt;
	struct reflection_type_data *rtd, *rtd_dep;
	enum fy_type_kind type_kind;
	size_t size __attribute__((unused)), align __attribute__((unused));
	const char *text;
	size_t len, lenz;
	char *p = NULL;
	void *data;
	size_t data_size __attribute__((unused));
	struct str_instance_data id_local, *id = &id_local;
	int rc;

	rtd = rw->rtd;
	rtd_dep = rtd->rtd_dep;
	RP_ASSERT(rtd_dep);

	data = rw->data;
	RP_ASSERT(!(rw->flags & RWF_BITFIELD_DATA));
	data_size = rw->data_size.bytes;

	type_kind = rtd->ti->kind;
	RP_ASSERT(fy_type_kind_is_valid(type_kind));

	size = fy_type_kind_size(type_kind);
	align = fy_type_kind_align(type_kind);

	RP_ASSERT(data_size == size && ((size_t)(uintptr_t)data & (align - 1)) == 0);

	type_kind = rtd_dep->ti->kind;
	RW_ASSERT(type_kind == FYTK_CHAR);

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

	RP_INPUT_CHECK(fye->type == FYET_SCALAR,
		       "expecting FYET_SCALAR, got %s\n", fy_event_type_get_text(fye->type));

	fyt = fy_event_get_token(fye);
	text = fy_token_get_text(fyt, &len);
	RP_ASSERT(text != NULL);

	str_instance_data_setup(id, rw);

	RP_INPUT_CHECK(id->rfd_counter || !memchr(text, 0, len),
		"string contains \\0 which can't be stored without a counter\n");
	RP_INPUT_CHECK(id->rfd_counter || !id->not_null_terminated,
		"cannot have a string not null terminated without a counter\n");

	lenz = len + !id->not_null_terminated;

	if (id->rfd_counter) {
		rc = integer_field_store_check(id->rfd_counter, lenz);
		RP_INPUT_CHECK(!rc, "cannot store length %zu to counter", lenz);
	}

	if (!(flags & RPF_NO_STORE)) {

		p = reflection_malloc(rtd->rts, lenz);
		RP_ASSERT(p != NULL);

		memcpy(p, text, len);

		if (!id->not_null_terminated)
			p[len] = '\0';

		if (id->rfd_counter)
			struct_integer_field_store(id->rw_struct, id->rfd_counter, len);

		*(char **)data = p;
		p = NULL;
	}

	rc = 0;

out:
	if (p)
		reflection_free(rtd->rts, p);
	fy_parser_event_free(fyp, fye);
	return rc;

err_out:
	rc = -1;
	goto out;

err_input:
	rc = -EINVAL;
	goto out;
}

static int
ptr_char_emit(struct fy_emitter *emit, struct reflection_walker *rw,
	      enum reflection_emit_flags flags)
{
	struct reflection_type_data *rtd, *rtd_dep;
	enum fy_type_kind type_kind __attribute__((unused));
	struct str_instance_data id_local, *id = &id_local;
	const char *text;
	size_t len;
	int rc;

	rtd = rw->rtd;

	/* verify alignment */
	type_kind = rtd->ti->kind;
	assert(type_kind == FYTK_PTR);

	assert(rtd->ti->kind == FYTK_PTR);
	rtd_dep = rtd->rtd_dep;
	assert(rtd_dep);

	type_kind = rtd_dep->ti->kind;
	assert(type_kind == FYTK_CHAR);

	text = *(const char **)rw->data;
	if (!text)
		text = "null";

	str_instance_data_setup(id, rw);

	rc = str_instance_len(id, text, &len);
	RE_ASSERT(!rc);

	rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_ANY, text, len, NULL, NULL);
	RE_ASSERT(!rc);

	return 0;
err_out:
	return -1;
}

static void
ptr_char_dtor(struct reflection_walker *rw)
{
	void *ptr;

	ptr = *(void **)rw->data;
	if (!ptr)
		return;

	*(void **)rw->data = NULL;

	reflection_free(rw->rtd->rts, ptr);
}

static int
ptr_char_copy(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	struct reflection_walker *rw;
	struct reflection_type_data *rtd;
	struct str_instance_data id_src_local, *id_src = &id_src_local;
	struct str_instance_data id_dst_local, *id_dst = &id_dst_local;
	const char *str_src;
	size_t src_len, dst_lenz;
	char *p;
	int rc;

	assert(rw_dst);
	assert(rw_src);
	rw = rw_src;

	RW_ASSERT(rw_src->rtd);
	RW_ASSERT(rw_src->rtd == rw_dst->rtd);

	rtd = rw_src->rtd;

	RW_ASSERT(rw_src->data);
	RW_ASSERT(rw_dst->data);

	str_src = *(const char **)rw_src->data;

	/* nothing must be there before */
	RW_ASSERT(*(char **)rw_dst->data == NULL);

	if (str_src) {

		str_instance_data_setup(id_src, rw_src);
		str_instance_data_setup(id_dst, rw_dst);

		rc = str_instance_len(id_src, str_src, &src_len);
		RW_ASSERT(!rc);

		dst_lenz = src_len + !id_dst->not_null_terminated;

		if (id_dst->rfd_counter) {
			rc = integer_field_store_check(id_dst->rfd_counter, dst_lenz);
			RW_ASSERT(!rc);
		}

		p = reflection_malloc(rtd->rts, dst_lenz);
		RW_ASSERT(p);

		memcpy(p, str_src, src_len);

		if (!id_dst->not_null_terminated)
			p[src_len] = '\0';

		if (id_dst->rfd_counter)
			struct_integer_field_store(id_dst->rw_struct, id_dst->rfd_counter, src_len);
	} else
		p = NULL;

	*(char **)rw_dst->data = p;

	return 0;

err_out:
	return -1;
}

static int
ptr_char_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	struct reflection_walker *rw;
	struct str_instance_data id_a_local, *id_a = &id_a_local;
	struct str_instance_data id_b_local, *id_b = &id_b_local;
	const char *astr, *bstr;
	size_t alen, blen, len;
	int rc;

	assert(rw_a);
	assert(rw_b);

	rw = rw_a;

	RW_ASSERT(rw_a->data);
	RW_ASSERT(rw_b->data);

	astr = *(const char **)rw_a->data;
	bstr = *(const char **)rw_b->data;

	RW_ASSERT(astr && bstr);

	str_instance_data_setup(id_a, rw_a);
	str_instance_data_setup(id_b, rw_b);

	rc = str_instance_len(id_a, astr, &alen);
	RW_ASSERT(!rc);

	rc = str_instance_len(id_b, bstr, &blen);
	RW_ASSERT(!rc);

	len = alen < blen ? alen : blen;

	rc = memcmp(astr, bstr, len);
	if (rc)
		return rc > 0 ? 1 : -1;

	return alen < blen ? -1 :
	       alen > blen ?  1 : 0;

err_out:
	return -2;
}

static int
ptr_char_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	int rc;

	rc = ptr_char_cmp(rw_a, rw_b);
	if (rc <= -2)
		return rc;
	return !rc;
}

static const struct reflection_type_ops ptr_char_ops = {
	.name = "ptr_char",
	.parse = ptr_char_parse,
	.emit = ptr_char_emit,
	.dtor = ptr_char_dtor,
	.copy = ptr_char_copy,
	.cmp = ptr_char_cmp,
	.eq = ptr_char_eq,
};

static int
ptr_emit(struct fy_emitter *emit, struct reflection_walker *rw,
	 enum reflection_emit_flags flags)
{
	struct reflection_type_data *rtd, *rtd_dep;
	void *data;
	size_t data_size;
	struct reflection_walker rw_dep;
	int rc;

	rtd = rw->rtd;
	data = rw->data;
	assert(!(rw->flags & RWF_BITFIELD_DATA));
	data_size = rw->data_size.bytes;

	assert(rtd->ti->kind == FYTK_PTR);

	rtd_dep = rtd->rtd_dep;
	assert(rtd_dep);

	data = *(void **)data;
	if (!data)
		return emit_ptr_NULL(emit, rw);

	data_size = rtd_dep->ti->size;

	memset(&rw_dep, 0, sizeof(rw_dep));
	rw_dep.parent = rw;
	rw_dep.rtd = rtd_dep;
	rw_dep.data = data;
	rw_dep.data_size.bytes = data_size;

	rc = reflection_emit_rw(emit, &rw_dep, flags);
	RE_ASSERT(!rc);

	return 0;
err_out:
	return -1;
}

static int
typedef_parse(struct fy_parser *fyp, struct reflection_walker *rw,
	      enum reflection_parse_flags flags)
{
	struct reflection_walker rw_dep;
	struct reflection_walker *rw_new;

	rw_new = reflection_rw_dep(&rw_dep, rw);
	return reflection_parse_rw(fyp, rw_new, flags);
}

static int
typedef_emit(struct fy_emitter *emit, struct reflection_walker *rw,
	     enum reflection_emit_flags flags)
{
	struct reflection_walker rw_dep;

	return reflection_emit_rw(emit, reflection_rw_dep(&rw_dep, rw), flags);
}

static void
typedef_dtor(struct reflection_walker *rw)
{
	struct reflection_walker rw_dep;

	reflection_dtor_rw(reflection_rw_dep(&rw_dep, rw));
}

enum typedef_call2_type {
	TC2T_COPY,
	TC2T_CMP,
	TC2T_EQ,
};

static int
typedef_call2(struct reflection_walker *rw_a, struct reflection_walker *rw_b,
	      enum typedef_call2_type call_type)
{
	struct reflection_walker rw_dep_a, rw_dep_b;
	int rc;

	reflection_rw_dep(&rw_dep_a, rw_a);
	reflection_rw_dep(&rw_dep_b, rw_b);

	switch (call_type) {
	case TC2T_COPY:
		rc = reflection_copy_rw(&rw_dep_a, &rw_dep_b);
	case TC2T_CMP:
		rc = reflection_cmp_rw(&rw_dep_a, &rw_dep_b);
		break;
	case TC2T_EQ:
		rc = reflection_eq_rw(&rw_dep_a, &rw_dep_b);
		break;
	default:
		rc = -1;
		break;
	}

	return rc;
}

static int
typedef_copy(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	return typedef_call2(rw_dst, rw_src, TC2T_COPY);
}

static int
typedef_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return typedef_call2(rw_a, rw_b, TC2T_CMP);
}

static int
typedef_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return typedef_call2(rw_a, rw_b, TC2T_EQ);
}

static const struct reflection_type_ops dyn_array_ops = {
	.name = "dyn_array",
	.parse = common_array_parse,
	.emit = common_array_emit,
	.dtor = common_array_dtor,
	.copy = common_array_copy,
	.cmp = common_array_cmp,
	.eq = common_array_eq,
};

static const struct reflection_type_ops dyn_map_ops = {
	.name = "dyn_map",
	.parse = common_array_parse,
	.emit = common_array_emit,
	.dtor = common_array_dtor,
	.copy = common_array_copy,
	.cmp = common_array_cmp,
	.eq = common_array_eq,
};

static const struct reflection_type_ops const_map_ops = {
	.name = "const_map",
	.parse = common_array_parse,
	.emit = common_array_emit,
	.dtor = common_array_dtor,
	.copy = common_array_copy,
	.cmp = common_array_cmp,
	.eq = common_array_eq,
};

static int
ptr_doc_parse(struct fy_parser *fyp, struct reflection_walker *rw,
	      enum reflection_parse_flags flags)
{
	struct reflection_type_data *rtd, *rtd_dep;
	enum fy_type_kind type_kind;
	size_t size __attribute__((unused)), align __attribute__((unused));
	void *data;
	size_t data_size __attribute__((unused));
	struct fy_document_builder *fydb = NULL;
	struct fy_document *fyd;

	rtd = rw->rtd;

	assert(rtd->ti->kind == FYTK_PTR);

	rtd_dep = rtd->rtd_dep;
	assert(rtd_dep);

	data = rw->data;
	assert(!(rw->flags & RWF_BITFIELD_DATA));
	data_size = rw->data_size.bytes;

	assert(data);

	type_kind = rtd->ti->kind;
	assert(fy_type_kind_is_valid(type_kind));

	size = fy_type_kind_size(type_kind);
	align = fy_type_kind_align(type_kind);

	assert(data_size == size && ((size_t)(uintptr_t)data & (align - 1)) == 0);

	type_kind = rtd_dep->ti->kind;
	assert(type_kind == FYTK_VOID);

	if (!(flags & RPF_NO_STORE))
		*(char **)data = NULL;

	fydb = fy_document_builder_create_on_parser(fyp);
	RP_ASSERT(fydb != NULL);

	fyd = fy_document_builder_load_document(fydb, fyp);

	fy_document_builder_destroy(fydb);

	if (!fyd)
		return -1;

	if (!(flags & RPF_NO_STORE))
		*(void **)data = fyd;
	else
		fy_document_destroy(fyd);

	return 0;

err_out:
	fy_document_builder_destroy(fydb);
	return -1;
}

static int
ptr_doc_emit(struct fy_emitter *emit, struct reflection_walker *rw,
	     enum reflection_emit_flags flags)
{
	struct reflection_type_data *rtd, *rtd_dep;
	enum fy_type_kind type_kind __attribute__((unused));
	struct fy_document *fyd;
	int rc;

	rtd = rw->rtd;

	/* verify alignment */
	type_kind = rtd->ti->kind;
	assert(type_kind == FYTK_PTR);

	assert(rtd->ti->kind == FYTK_PTR);
	rtd_dep = rtd->rtd_dep;
	assert(rtd_dep);

	type_kind = rtd_dep->ti->kind;
	assert(type_kind == FYTK_VOID);

	fyd = *(struct fy_document **)rw->data;
	if (!fyd)
		return emit_ptr_NULL(emit, rw);

	rc = fy_emit_body_node(emit, fy_document_root(fyd));
	RE_ASSERT(!rc);

	return 0;

err_out:
	return -1;
}

static void
ptr_doc_dtor(struct reflection_walker *rw)
{
	struct fy_document *fyd;

	fyd = *(void **)rw->data;
	if (!fyd)
		return;

	*(void **)rw->data = NULL;

	fy_document_destroy(fyd);
}

static const struct reflection_type_ops ptr_doc_ops = {
	.name = "ptr_doc",
	.parse = ptr_doc_parse,
	.emit = ptr_doc_emit,
	.dtor = ptr_doc_dtor,
};

const struct reflection_type_ops reflection_ops_table[FYTK_COUNT] = {
	[FYTK_INVALID] = {
		.name = "invalid",
	},
	[FYTK_VOID] = {
		.name = "void",
		/* we can match null for void as entry point */
		.parse = null_scalar_parse,
		.emit = null_scalar_emit,
		.cmp = null_scalar_cmp,
		.eq = null_scalar_eq,
		.copy = null_scalar_copy,
	},
	[FYTK_NULL] = {
		.name = "null",
		.parse = null_scalar_parse,
		.emit = null_scalar_emit,
		.cmp = null_scalar_cmp,
		.eq = null_scalar_eq,
		.copy = null_scalar_copy,
	},
	[FYTK_BOOL] = {
		.name = "bool",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_CHAR] = {
		.name = "char",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_SCHAR] = {
		.name = "schar",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_UCHAR] = {
		.name = "uchar",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_SHORT] = {
		.name = "short",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_USHORT] = {
		.name = "ushort",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_INT] = {
		.name = "int",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_UINT] = {
		.name = "uint",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_LONG] = {
		.name = "long",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_ULONG] = {
		.name = "ulong",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_LONGLONG] = {
		.name = "longlong",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_ULONGLONG] = {
		.name = "ulonglong",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
#ifdef FY_HAS_INT128
	[FYTK_INT128] = {
		.name = "int128",
	},
	[FYTK_UINT128] = {
		.name = "uint128",
	},
#else
	[FYTK_INT128] = {
		.name = "int128",
	},
	[FYTK_UINT128] = {
		.name = "uint128",
	},
#endif
	[FYTK_FLOAT] = {
		.name = "float",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_DOUBLE] = {
		.name = "double",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_LONGDOUBLE] = {
		.name = "longdouble",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
#ifdef FY_HAS_FP16
	[FYTK_FLOAT16] = {
		.name = "float16",
	},
#else
	[FYTK_FLOAT16] = {
		.name = "float16",
	},
#endif
#ifdef FY_HAS_FLOAT128
	[FYTK_FLOAT128] = {
		.name = "float128",
	},
#else
	[FYTK_FLOAT128] = {
		.name = "float128",
	},
#endif
	/* these are templates */
	[FYTK_RECORD] = {
		.name = "record",
	},

	[FYTK_STRUCT] = {
		.name = "struct",
		.parse = struct_parse,
		.emit = struct_emit,
		.dtor = struct_dtor,
		.copy = struct_copy,
		.cmp = struct_cmp,
		.eq = struct_eq,
	},
	[FYTK_UNION] = {
		.name = "union",
		.parse = struct_parse,
		.emit = struct_emit,
		.dtor = struct_dtor,
		.copy = struct_copy,
		.cmp = struct_cmp,
		.eq = struct_eq,
	},
	[FYTK_ENUM] = {
		.name = "enum",
		.parse = enum_parse,
		.emit = enum_emit,
		.cmp = enum_cmp,
		.eq = enum_eq,
		.copy = enum_copy,
	},
	[FYTK_TYPEDEF] = {
		.name = "typedef",
		.parse = typedef_parse,
		.emit = typedef_emit,
		.dtor = typedef_dtor,
		.copy = typedef_copy,
		.cmp = typedef_cmp,
		.eq = typedef_eq,
	},
	[FYTK_PTR] = {
		.name = "ptr",
		.parse = ptr_parse,
		.emit = ptr_emit,
		.dtor = ptr_dtor,
		.copy = ptr_copy,
		.cmp = ptr_cmp,
		.eq = ptr_eq,
	},
	[FYTK_CONSTARRAY] = {
		.name = "const_array",
		.parse = common_array_parse,
		.emit = common_array_emit,
		.dtor = common_array_dtor,
		.copy = common_array_copy,
		.cmp = common_array_cmp,
		.eq = common_array_eq,
	},
	[FYTK_INCOMPLETEARRAY] = {
		.name = "incomplete_array",
	},
	[FYTK_FUNCTION] = {
		.name = "function",
	},
};

void reflection_free_rw(struct reflection_walker *rw)
{
	if (!rw || !rw->rtd || !rw->data)
		return;

	if (reflection_type_data_has_dtor(rw->rtd))
		reflection_dtor_rw(rw);
	reflection_free(rw->rtd->rts, rw->data);
}

void reflection_type_data_free(struct reflection_type_data *rtd, void *ptr)
{
	struct reflection_walker *rw;
	struct reflection_root_ctx rctx_local, *rctx = &rctx_local;

	if (!rtd || !ptr)
		return;

	rw = reflection_root_ctx_setup(rctx, rtd, ptr, NULL);
	reflection_free_rw(rw);
}

enum reflection_type_data_sort {
	RTDS_DEPTH_ORDER,
	RTDS_ID,
	RTDS_TYPE_INFO,
	RTDS_TYPE_INFO_ID,
};

void *
reflection_type_data_generate_value(struct reflection_type_data *rtd, struct fy_node *fyn)
{
	static const struct fy_parse_cfg cfg_i = { .search_path = "", .flags = 0, };
	struct reflection_type_system *rts;
	struct fy_parser *fyp_i = NULL;
	struct fy_document_iterator *fydi = NULL;
	void *value;
	int rc;

	assert(rtd);

	rts = rtd->rts;
	assert(rts);

	RTS_ASSERT(fyn);

	fyp_i = fy_parser_create(&cfg_i);
	RTS_ASSERT(fyp_i);

	fydi = fy_document_iterator_create_on_node(fyn);
	RTS_ASSERT(fydi);

	rc = fy_parser_set_document_iterator(fyp_i, FYPEGF_GENERATE_ALL_EVENTS, fydi);
	RTS_ASSERT(!rc);

	rc = reflection_parse(fyp_i, rtd, &value, RPF_SILENT_ALL);
	RTS_ASSERT(!rc);

out:
	fy_document_iterator_destroy(fydi);
	fy_parser_destroy(fyp_i);
	return value;

err_out:
	value = NULL;
	goto out;
}

int
reflection_type_data_generate_value_into(struct reflection_type_data *rtd, struct fy_node *fyn, void *data)
{
	static const struct fy_parse_cfg cfg_i = { .search_path = "", .flags = 0, };
	struct reflection_type_system *rts;
	struct fy_parser *fyp_i = NULL;
	struct fy_document_iterator *fydi = NULL;
	int rc;

	assert(rtd);

	rts = rtd->rts;
	assert(rts);

	RTS_ASSERT(fyn);

	fyp_i = fy_parser_create(&cfg_i);
	RTS_ASSERT(fyp_i);

	fydi = fy_document_iterator_create_on_node(fyn);
	RTS_ASSERT(fydi);

	rc = fy_parser_set_document_iterator(fyp_i, FYPEGF_GENERATE_ALL_EVENTS, fydi);
	RTS_ASSERT(!rc);

	rc = reflection_parse_into(fyp_i, rtd, data, RPF_SILENT_ALL);
	RTS_ASSERT(!rc);

out:
	fy_document_iterator_destroy(fydi);
	fy_parser_destroy(fyp_i);
	return rc;

err_out:
	rc = -1;
	goto out;
}

int
reflection_type_data_put_value_into(struct reflection_type_data *rtd, void *data, struct fy_node *fyn_value, void *value)
{
	struct reflection_type_system *rts;
	struct reflection_walker rw_dst, rw_src;
	int rc;

	assert(rtd);
	rts = rtd->rts;
	assert(rts);

	RTS_ASSERT(data && fyn_value);

	/* copy from the already prepared cache value */
	if (value) {
		rc = reflection_copy_rw(
				reflection_rw_value(&rw_dst, rtd, data),
				reflection_rw_value(&rw_src, rtd, value));
		if (!rc)
			return 0;
		/* we have to generate manually */
	}
	/* no canned default available, must be created each time */
	rc = reflection_type_data_generate_value_into(rtd, fyn_value, data);
	RTS_ASSERT(!rc);

	return 0;
err_out:
	return -1;
}

char *
reflection_walker_get_path(struct reflection_walker *rw)
{
	struct reflection_walker **rw_path, *rwt;
	int i, count;
	bool pending_value, in_map, in_seq;
	char idxbuf[64];
	char *s, *e;
	char *buf, *new_buf;
	size_t len, size;
	struct fy_emitter *emit = NULL;
	char *emit_str = NULL;
	int rc;

	if (!rw)
		return NULL;

	count = 0;
	rwt = rw;
	while (rwt) {
		count++;
		rwt = rwt->parent;
	}

	rw_path = alloca(count * sizeof(*rw_path));
	i = count;
	rwt = rw;
	while (rwt) {
		rw_path[--i] = rwt;
		rwt = rwt->parent;
	}

	size = 128;

	buf = malloc(size);
	RW_ASSERT(buf);

	s = buf;
	e = &buf[size];
	*s++ = '/';

#undef APPEND_STR
#define APPEND_STR(_ptr, _len) \
	do { \
		const void *__ptr = (_ptr); \
		size_t __len = (_len); \
		size_t __pos; \
		if (s + __len >= e) { \
			__pos = s - buf; \
			while (__pos + __len < size) \
				size *= 2; \
			new_buf = realloc(buf, size * 2); \
			RW_ASSERT(new_buf); \
			buf = new_buf; \
			s = buf + __pos; \
			e = buf + size; \
		} \
		assert(s + __len <= e); \
		memcpy(s, __ptr, __len); \
		s += __len; \
	} while(0)

#define APPEND_CH(_ch) \
	do { \
		char __ch = (_ch); \
		APPEND_STR(&__ch, 1); \
	} while(0)

	pending_value = true;
	in_map = in_seq = false;
	for (i = 0; i < count; i++) {
		rwt = rw_path[i];

		if ((rwt->flags & (RWF_VALUE | RWF_TEXT_KEY)) == (RWF_VALUE | RWF_TEXT_KEY))  {

			if (s[-1] != '/')
				APPEND_CH('/');

			len = strlen(rwt->text_key);

			APPEND_STR(rwt->text_key, len);

			pending_value = true;

		} else if ((rwt->flags & (RWF_VALUE | RWF_COMPLEX_KEY)) == (RWF_VALUE | RWF_COMPLEX_KEY)) {

			if (s[-1] != '/')
				APPEND_CH('/');

			emit = fy_emit_to_string(FYECF_WIDTH_INF | FYECF_INDENT_DEFAULT |
						 FYECF_MODE_FLOW_ONELINE | FYECF_NO_ENDING_NEWLINE |
						 FYECF_DOC_START_MARK_OFF | FYECF_DOC_END_MARK_OFF |
						 FYECF_VERSION_DIR_OFF | FYECF_TAG_DIR_OFF);
			RW_ASSERT(emit);

			rc = fy_emit_eventf(emit, FYET_STREAM_START);
			RW_ASSERT(!rc);

			rc = fy_emit_eventf(emit, FYET_DOCUMENT_START, 1, NULL, NULL);
			RW_ASSERT(!rc);

			rc = reflection_emit_rw(emit, rwt->rw_key, REF_SILENT_ALL);
			RW_ASSERT(!rc);

			rc = fy_emit_eventf(emit, FYET_DOCUMENT_END, 1);
			RW_ASSERT(!rc);

			rc = fy_emit_eventf(emit, FYET_STREAM_END);
			RW_ASSERT(!rc);

			emit_str = fy_emit_to_string_collect(emit, &len);
			RW_ASSERT(emit_str);

			APPEND_STR(emit_str, len);

			free(emit_str);
			emit_str = NULL;

			fy_emitter_destroy(emit);
			emit = NULL;

			pending_value = true;
		}

		if (rwt->flags & (RWF_MAP | RWF_SEQ)) {

			in_map = (rwt->flags & RWF_MAP);
			in_seq = !in_map;

			if (s[-1] != '/')
				APPEND_CH('/');

			pending_value = !!(rwt->flags & RWF_KEY);

		} else if (rwt->flags & RWF_SEQ_KEY) {

			if (pending_value)
				pending_value = false;

			if (!in_seq) {
				if (s[-1] != '/')
					APPEND_CH('/');
			}

			snprintf(idxbuf, sizeof(idxbuf) - 1, "%ju", rwt->idx);
			len = strlen(idxbuf);

			APPEND_STR(idxbuf, len);
		}
	}

	/* trim trailing / */
	if (s > buf && s[-1] == '/')
		s--;

	APPEND_CH('\0');

	return buf;

err_out:
	if (emit_str)
		free(emit_str);
	if (emit)
		fy_emitter_destroy(emit);
	if (buf)
		free(buf);
	return NULL;
}

#define reflection_walker_get_path_alloca(_rw) \
	FY_ALLOCA_COPY_FREE(reflection_walker_get_path((_rw)), FY_NT)

int
reflection_parse_rw(struct fy_parser *fyp, struct reflection_walker *rw, enum reflection_parse_flags flags)
{
	int rc;

	assert(fyp);
	assert(rw);
	assert(rw->rtd);
	assert(rw->rtd->ti);
	assert(rw->rtd->ops);

	/* does the type have a parse method? if not it's unsupported */
	RW_ASSERT(rw->rtd->ops->parse != NULL);

	rc = rw->rtd->ops->parse(fyp, rw, flags);

out:
	return rc;

err_out:
	rc = -EINVAL;
	goto out;
}

int
reflection_emit_rw(struct fy_emitter *emit, struct reflection_walker *rw, enum reflection_emit_flags flags)
{
	int rc;

	assert(emit);
	assert(rw);
	assert(rw->rtd);
	assert(rw->rtd->ops);
	assert(rw->rtd->ops->emit);

	rc = rw->rtd->ops->emit(emit, rw, flags);

	return rc;
}

void reflection_dtor_rw(struct reflection_walker *rw)
{
	assert(rw);
	assert(rw->rtd);

	if (!reflection_type_data_has_dtor(rw->rtd))
		return;

	assert(rw->rtd->ops->dtor);
	rw->rtd->ops->dtor(rw);
}

int reflection_copy_rw(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	struct reflection_type_data *rtd;
	int rc;

	assert(rw_dst);
	assert(rw_dst->data);
	assert(rw_src);
	assert(rw_src->data);

	assert(rw_src->rtd);
	assert(rw_dst->rtd);
	assert(rw_dst->rtd == rw_src->rtd);

	rtd = rw_src->rtd;

	/* pure, can just copy */
	if ((rtd->flags & RTDF_PURITY_MASK) == 0 &&
	    !(rw_src->flags & rw_dst->flags & RWF_BITFIELD_DATA)) {
		memcpy(rw_dst->data, rw_src->data, rtd->ti->size);
		rc = 0;
	} else {
		assert(rtd->ops->copy);
		rc = rtd->ops->copy(rw_dst, rw_src);
	}

	return rc;
}

/* -1 a < b, 1 a > b, 0 a == b */
int reflection_cmp_rw(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	struct reflection_type_data *rtd;
	int rc;

	assert(rw_a);
	assert(rw_a->data);
	assert(rw_b);
	assert(rw_b->data);

	assert(rw_b->rtd);
	assert(rw_a->rtd);
	assert(rw_a->rtd == rw_b->rtd);

	rtd = rw_b->rtd;

	assert(rtd->ops->cmp);
	rc = rtd->ops->cmp(rw_a, rw_b);

	return rc;
}

int reflection_eq_rw(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	struct reflection_type_data *rtd;
	int rc;

	assert(rw_a);
	assert(rw_a->data);
	assert(rw_b);
	assert(rw_b->data);

	assert(rw_b->rtd);
	assert(rw_a->rtd);
	assert(rw_a->rtd == rw_b->rtd);

	rtd = rw_b->rtd;

	/* pure? memcmp it */
	if ((rtd->flags & RTDF_PURITY_MASK) == 0 &&
	    !(rw_a->flags & rw_b->flags & RWF_BITFIELD_DATA)) {
		rc = !memcmp(rw_a->data, rw_b->data, rtd->ti->size);
		assert(rc == 0 || rc == 1);
	} else {
		assert(rtd->ops->eq);
		rc = rtd->ops->eq(rw_a, rw_b);
	}

	return rc;
}

struct fy_node *
reflection_annotation_get_node(struct fy_document *fyd, const char *path)
{
	struct fy_node *fyn_root;

	fyn_root = fy_document_root(fyd);
	if (!fyn_root)
		return NULL;

	return fy_node_by_path(fyn_root, path, FY_NT, FYNWF_DONT_FOLLOW);
}

const char *
reflection_annotation_get_string(struct fy_document *fyd, const char *path)
{
	struct fy_node *fyn;
	struct fy_token *fyt;
	const char *text0;

	fyn = reflection_annotation_get_node(fyd, path);
	if (!fyn)
		return NULL;

	fyt = fy_node_get_scalar_token(fyn);
	if (!fyt)
		return NULL;

	text0 = fy_token_get_text0(fyt);
	if (!text0)
		return NULL;

	return text0;
}

int
reflection_field_data_specialize(struct reflection_field_data *rfd)
{
	struct reflection_type_system *rts;
	struct reflection_type_data *rtd, *rtd_parent, *rtd_final_dep, *rtd_resolved_typedef;
	struct reflection_field_data *rfd_ref, *rfd_key;
	enum fy_type_kind type_kind, resolved_type_kind, final_dependent_type_kind;
	const char *counter;
	const char *key;
	bool is_dyn_array, is_const_array, is_incomplete_array, is_array, is_map;
	bool has_counter, has_terminator, has_key;

	if (!rfd)
		return -1;

	rtd = rfd_rtd(rfd);
	assert(rtd);

	rts = rtd->rts;
	assert(rts);

	final_dependent_type_kind = reflection_type_data_final_dependent(rtd)->ti->kind;
	rfd->is_unsigned = fy_type_kind_is_unsigned(final_dependent_type_kind);
	rfd->is_signed = fy_type_kind_is_signed(final_dependent_type_kind);

	rtd_parent = rfd->rtd_parent;	// may be NULL for root rfd

	rtd_final_dep = reflection_type_data_final_dependent(rtd);
	assert(rtd_final_dep);

	rtd_resolved_typedef = reflection_type_data_resolved_typedef(rtd);
	assert(rtd_resolved_typedef);

	type_kind = rtd->ti->kind;
	resolved_type_kind = rtd_resolved_typedef->ti->kind;

	if (reflection_meta_get_bool(rtd->meta, rmvid_match_null)) {

		RTS_ERROR_CHECK(!(rtd->flags & RTDF_MUTATED),
				"type '%s' already mutated", rtd->ti->name);

		rtd->ops = &reflection_ops_table[FYTK_NULL];
		rtd->flags |= RTDF_MUTATED;
	}

	switch (resolved_type_kind) {
	case FYTK_UNION:
		rtd->union_field_idx = rfd->idx;
		break;

	default:
		break;

	}

	counter = rtd_parent ? reflection_meta_get_str(rtd->meta, rmvid_counter) : NULL;
	has_counter = counter != NULL;

	has_terminator = reflection_meta_get_any_value(rtd->meta, rmvid_terminator) != NULL;

	/* key only makes sense if the final dep is a struct */
	key = rtd_final_dep->ti->kind == FYTK_STRUCT ? reflection_meta_get_str(rtd->meta, rmvid_key) : NULL;
	has_key = key != NULL;

	if (has_counter) {
		rfd_ref = reflection_type_data_lookup_field(rtd_parent, counter);

		RTS_ERROR_CHECK(rfd_ref,
				"dyn_array counter field %s.%s does not exist\n", rtd_parent->ti->name, counter);

		RTS_ERROR_CHECK(fy_type_kind_is_integer(rfd_ref->fi->type_info->kind),
				"dyn_array counter field (%s) must be integer\n", counter);

		RTS_ERROR_CHECK(!rfd_ref->is_counter,
				"dyn_array counter field (%s) must be a single counter\n", counter);

		rfd_ref->is_counter = true;
	} else
		rfd_ref = NULL;

	/* don't do arrays on typedefs */
	if ((rtd->flags & RTDF_MUTATED) || type_kind == FYTK_TYPEDEF)
		goto skip_array;

	is_dyn_array = resolved_type_kind == FYTK_PTR &&
		       (has_counter || has_terminator || has_key ||
		       (rtd->rtd_dep && rtd->rtd_dep->ti->kind == FYTK_PTR));

	is_const_array = resolved_type_kind == FYTK_CONSTARRAY && !is_dyn_array;

	is_incomplete_array = false;	/* XXX not yet */

	is_array = is_dyn_array || is_const_array || is_incomplete_array;
	is_map = is_array && rtd_final_dep->ti->kind == FYTK_STRUCT && has_key;

	if (is_map) {
		/* the dependent type must be a structure */

		RTS_ASSERT(rtd_final_dep->ti->kind == FYTK_STRUCT);

		rfd_key = reflection_type_data_lookup_field(rtd_final_dep, key);
		RTS_ERROR_CHECK(rfd_key,
				"unable to find key field '%s' in '%s'", key, rtd_final_dep->ti->name);

		RTS_ERROR_CHECK(rtd_final_dep->fields_count == 2,
				"'%s' does not have only two fields (key/value)", rtd_final_dep->ti->name);
	}

	if (is_array) {
		if (is_dyn_array)
			rtd->ops = !is_map ? &dyn_array_ops : &dyn_map_ops;
		else if (is_const_array)
			rtd->ops = !is_map ? &reflection_ops_table[FYTK_CONSTARRAY] : &const_map_ops;
		else
			rtd->ops = NULL;
		RTS_ASSERT(rtd->ops);
		rtd->flags |= RTDF_MUTATED;
	}

skip_array:

	return 0;
err_out:
	return -1;
}

/* true if pure */
static inline void
reflection_type_data_purity_dep(struct reflection_type_data *rtd,
				struct reflection_type_data *rtd_dep,
				bool dep_recursive)
{
	assert(rtd);
	assert(rtd_dep);

	/* already tainted */
	if ((rtd->flags & RTDF_PURITY_MASK) == RTDF_IMPURE)
		return;

	/* recursives are impure */
	if (dep_recursive || (rtd_dep->flags & RTDF_PURITY_MASK) != 0)
		rtd->flags |= RTDF_IMPURE;
}

int
reflection_type_data_specialize(struct reflection_type_data *rtd,
				struct reflection_setup_type_ctx *rstc)
{
	struct reflection_type_system *rts;
	struct reflection_field_data *rfd, *rfd_flatten, *rfd_key, *rfd_selector, *rfdt;
	struct reflection_type_data *rtd_parent = NULL, *rtd_resolved_typedef = NULL;
	struct reflection_type_data *rtd_resolved_ptr = NULL;
	struct reflection_field_data *rfd_parent = NULL;
	enum fy_type_kind resolved_type_kind;
	const struct reflection_type_ops *ops;
	const char *flatten_field, *key_field, *selector;
	bool flat_anonymous;
	struct fy_document *fyd;
	struct reflection_any_value *rav_select;
	struct reflection_meta *rm;
	int i, rc;

	if (!rtd || !rstc)
		return -1;

	if (rtd->flags & (RTDF_SPECIALIZED | RTDF_SPECIALIZING))
		return 0;

	rts = rtd->rts;
	assert(rts);

	rtd->flags |= RTDF_SPECIALIZING;

	rtd_resolved_typedef = reflection_type_data_resolved_typedef(rtd->rtd_dep);
	resolved_type_kind = rtd_resolved_typedef ? rtd_resolved_typedef->ti->kind : FYTK_INVALID;

	rtd_parent = reflection_setup_type_ctx_top_type(rstc);

	rfd_parent = reflection_setup_type_ctx_top_field(rstc);
	if (!rfd_parent)
		rfd_parent = rstc->rr->rfd_root;
	assert(rfd_parent);

	switch (rtd->ti->kind) {
	case FYTK_ENUM:
		break;

	case FYTK_PTR:
		switch (resolved_type_kind) {
		case FYTK_CHAR:

			/* do we inhibit this conversion? */
			if (reflection_meta_get_bool(rtd->meta, rmvid_not_string))
				break;

			/* char * -> text */
			rtd->ops = &ptr_char_ops;
			rtd->flags |= RTDF_MUTATED;
			break;

		case FYTK_VOID:
			/* void * -> doc */
			rtd->ops = &ptr_doc_ops;
			rtd->flags |= RTDF_MUTATED;
			break;

		case FYTK_PTR:

			/* if it's on a field, it's the job of the field */
			if (rstc->rfds.top || !rtd_resolved_typedef)
				break;

			/* entry is some kind of base type, instantiate simple dynarray if so */
			rtd_resolved_ptr = reflection_type_data_resolved_typedef(rtd_resolved_typedef->rtd_dep);
			if (!rtd_resolved_ptr || rtd_resolved_ptr->ti->kind == FYTK_PTR)
				break;

			rtd->ops = &dyn_array_ops;
			rtd->flags |= RTDF_MUTATED;

			break;

		default:
			break;
		}

		break;

	case FYTK_CONSTARRAY:
		/* only for char[] */
		switch (resolved_type_kind) {
		case FYTK_CHAR:
			if (reflection_meta_get_bool(rtd->meta, rmvid_not_string))
				break;

			rtd->ops = &const_array_char_ops;
			rtd->flags |= RTDF_MUTATED;
			break;

		default:
			break;
		}

		break;

	case FYTK_STRUCT:
	case FYTK_UNION:

		/* check if there are anonymous fields */
		for (i = 0; i < rtd->fields_count; i++) {
			rfdt = rtd->fields[i];
			if (rfdt->fi->type_info->flags & FYTIF_ANONYMOUS_RECORD_DECL) {
				rtd->has_anonymous_field = true;
				rtd->first_anonymous_field_idx = i;
				break;
			}
		}

		rfd_flatten = NULL;
		flatten_field = reflection_meta_get_str(rtd->meta, rmvid_flatten_field);
		if (flatten_field) {
			rfd_flatten = reflection_type_data_lookup_field(rtd, flatten_field);
			RTS_ERROR_CHECK(rfd_flatten,
					"Unable to find flatten_field %s in '%s'", flatten_field,
					rtd->ti->name);
		}

		flat_anonymous = reflection_meta_get_bool(rtd->meta, rmvid_flatten_field_first_anonymous);
		RTS_ERROR_CHECK(!flat_anonymous || rtd->has_anonymous_field,
				"flat_anonymous enabled for %s but no anonymous_fields in '%s'", flatten_field, rtd->ti->name);

		if (!rfd_flatten && flat_anonymous)
			rfd_flatten = rtd->fields[rtd->first_anonymous_field_idx];

		rtd->flat_field_idx = rfd_flatten ? rfd_flatten->idx : -1;

		rfd_key = NULL;
		key_field = reflection_meta_get_str(rtd->meta, rmvid_key);
		if (key_field) {
			rfd_key = reflection_type_data_lookup_field(rtd, key_field);
			RTS_ERROR_CHECK(rfd_key,
					"key field %s not found in '%s'", key_field, rtd->ti->name);
		}

		/* union selector, must exist for unions */
		if (rtd->ti->kind == FYTK_UNION) {

			/* a union must have a selector field in the enclosing struct */
			RTS_ERROR_CHECK(rtd_parent,
					"'%s' must have a parent", rtd->ti->name);

			selector = reflection_meta_get_str(rtd->meta, rmvid_selector);

			if (selector) {
				rfd_selector = reflection_type_data_lookup_field(rtd_parent, selector);
				RTS_ERROR_CHECK(rfd_selector,
						"selector field '%s' not found in '%s'", selector,
						rtd_parent->ti->name);
			} else {
				RTS_ERROR_CHECK(rfd_parent,
						"no parent field for '%s'", rtd->ti->name);

				RTS_ERROR_CHECK(rfd_parent->idx > 0,
						"'%s' cannot be the first field", rtd->ti->name);

				RTS_ASSERT((rfd_parent->idx - 1) >= 0 && (rfd_parent->idx - 1) < rtd_parent->fields_count);

				rfd_selector = rtd_parent->fields[rfd_parent->idx - 1];
			}

			/* single selector */
			RTS_ERROR_CHECK(!rfd_selector->is_selector,
					"single selector field only for %s", selector);

			rfd_selector->is_selector = true;
			rtd->rtd_selector = reflection_type_data_ref(rfd_selector->rtd);

			rtd->selector_field_idx = rfd_selector->idx;

			/* for each field in the union a select must exist, or be implied */
			for (i = 0; i < rtd->fields_count; i++) {

				rfdt = rtd->fields[i];
				rm = rfd_rtd(rfdt)->meta;

				rav_select = reflection_meta_get_any_value(rm, rmvid_select);

				/* if there's no select create one which is the name of the field */
				if (!rav_select) {
					fyd = fy_document_buildf(NULL, "%s", rfdt->field_name);
					RTS_ASSERT(fyd);

					rav_select = reflection_any_value_create(rtd->rts, fy_document_root(fyd));
					fy_document_destroy(fyd);

					rc = reflection_meta_set_any_value(rm, rmvid_select, rav_select, false);
					RTS_ASSERT(!rc);
				}
			}
		}

		break;

	default:
		break;
	}

	/* is this on a field? */
	rtd->flags &= ~RTDF_PURITY_MASK;

	/* pointers are not pure (but may be point to something pure) */
	if (rtd->ti->kind == FYTK_PTR)
		rtd->flags |= RTDF_IMPURE;

	/* if the type has a non default ops and has a dtor is impure */
	if (!reflection_type_data_has_default_ops(rtd)) {
		ops = rtd->ops;
		if (ops && ops->dtor)
			rtd->flags |= RTDF_IMPURE;
	}

	/* pure dependency? */
	if (rtd->rtd_dep)
		reflection_type_data_purity_dep(rtd, rtd->rtd_dep, rtd->rtd_dep_recursive);

	/* pure fields? */
	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];
		reflection_type_data_purity_dep(rtd, rfd_rtd(rfd), rfd->rtd_recursive);
	}

	rtd->flags &= ~RTDF_SPECIALIZING;
	rtd->flags |= RTDF_SPECIALIZED;

	return 0;

err_out:
	return -1;
}

int
reflection_setup_type(struct reflection_type_system *rts,
		      const struct fy_type_info *ti,
		      struct reflection_setup_type_ctx *rstc,
		      struct reflection_type_data **rtdp,
		      struct reflection_meta *parent_meta)
{
	struct reflection_type_data *rtd = NULL, *rtd_dep;
	struct reflection_field_data *rfd, *rfd_parent;
	int i;
	int rc;

	assert(rts);
	assert(ti);
	assert(rtdp);

	/* guard against overflow */
	RTS_ASSERT(rts->rtd_next_idx + 1 > 0);

	*rtdp = NULL;

	rtd = malloc(sizeof(*rtd));
	RTS_ASSERT(rtd);
	memset(rtd, 0, sizeof(*rtd));

	rtd->refs = 1;
	rtd->rts = rts;
	rtd->idx = rts->rtd_next_idx++;
	rtd->ti = ti;
	rtd->ops = &reflection_ops_table[ti->kind];

	/* do fields */
	rtd->fields_count = fy_type_kind_has_fields(ti->kind) ? ti->count : 0;

	rtd->yaml_annotation = fy_type_info_get_yaml_annotation(rtd->ti);
	rtd->yaml_annotation_str = fy_type_info_get_yaml_comment(rtd->ti);

	rtd->flat_field_idx = -1;
	rtd->selector_field_idx = -1;
	rtd->union_field_idx = -1;
	rtd->first_anonymous_field_idx = -1;

	rtd->meta = parent_meta ? reflection_meta_copy(parent_meta) : reflection_meta_create(rts);
	RTS_ASSERT(rtd->meta);

	if (rtd->yaml_annotation) {
		rc = reflection_meta_fill(rtd->meta, fy_document_root(rtd->yaml_annotation));
		RTS_ASSERT(!rc);
	}

	if (rtd->fields_count > 0) {
		rtd->fields = malloc(sizeof(*rtd->fields) * rtd->fields_count);
		RTS_ASSERT(rtd->fields);
		memset(rtd->fields, 0, sizeof(*rtd->fields) * rtd->fields_count);
	}

	for (i = 0; i < rtd->fields_count; i++) {
		rfd = reflection_field_data_create(rtd, i);
		RTS_ASSERT(rfd);
		rtd->fields[i] = rfd;
	}

	/* add to the in progress list */
	rc = reflection_type_data_stack_push(&rstc->rtds, rtd);
	RTS_ASSERT(!rc);

	/* if a dependent type causes a loop, then the field that is a parent is recursive */
	if (ti->dependent_type) {
		rtd_dep = reflection_type_data_stack_find_by_type_info(&rstc->rtds, ti->dependent_type);
		if (rtd_dep) {

			rtd->rtd_dep_recursive = true;
			rtd->rtd_dep = rtd_dep;

			/* mark all fields in the stack as recursive */
			for (i = rstc->rfds.top - 1; i >= 0; i--) {
				rfd_parent = rstc->rfds.rfds[i];
				assert(rfd_parent);
				rfd_parent->rtd_recursive = true;
			}

		} else {
			rc = reflection_setup_type(rts, ti->dependent_type, rstc, &rtd->rtd_dep,
						   rtd->ti->kind == FYTK_TYPEDEF ? rtd->meta : NULL);
			RTS_ASSERT(!rc);
		}
	}

	for (i = 0; i < rtd->fields_count; i++) {

		rfd = rtd->fields[i];

		rc = reflection_field_data_stack_push(&rstc->rfds, rfd);
		RTS_ASSERT(!rc);

		rc = reflection_setup_type(rts, rfd->fi->type_info, rstc, &rfd->rtd, rfd->meta);
		RTS_ASSERT(!rc);

		(void)reflection_field_data_stack_pop(&rstc->rfds);

	}

	/* save so that specialize can work */
	*rtdp = rtd;

	/* specialize */
	if (rtd->rtd_dep && !rtd->rtd_dep_recursive) {
		rc = reflection_type_data_specialize(rtd->rtd_dep, rstc);
		RTS_ASSERT(!rc);

	}

	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];

		rc = reflection_field_data_specialize(rfd);
		RTS_ASSERT(!rc);

		if (!rfd->rtd_recursive) {
			rc = reflection_type_data_specialize(rfd->rtd, rstc);
			RTS_ASSERT(!rc);
		}
	}

	/* and remove it from the setup stack */
	(void)reflection_type_data_stack_pop(&rstc->rtds);

	rc = reflection_type_data_specialize(rtd, rstc);
	RTS_ASSERT(!rc);

	return 0;

err_out:
	reflection_type_data_destroy(rtd);
	*rtdp = NULL;
	return -1;
}

struct reflection_reference *
reflection_reference_create(struct reflection_type_system *rts, const char *name, const char *meta)
{
	struct fy_reflection *rfl;
	struct reflection_field_data *rfd = NULL;
	struct fy_document *fyd_meta = NULL;
	const struct fy_type_info *ti_entry;
	struct reflection_setup_type_ctx rstc;
	struct fy_type_info *ti;
	struct fy_field_info *fi;
	struct reflection_reference *rr = NULL;
	int rc;

	if (!rts || !name)
		return NULL;

	rfl = rts->cfg.rfl;
	assert(rfl);

	/* TODO check if reflection reference already present */

	ti_entry = fy_type_info_lookup(rfl, name);
	RTS_ERROR_CHECK(ti_entry, "no type named '%s' in reflection", name);

	if (meta) {
		fyd_meta = fy_flow_document_build_from_string(NULL, meta, FY_NT, NULL);
		RTS_ERROR_CHECK(fyd_meta, "failed to build meta document from '%s'", meta);
	}

	rr = malloc(sizeof(*rr));
	RTS_ASSERT(rr);
	memset(rr, 0, sizeof(*rr));

	rr->rts = rts;
	rr->name = strdup(name);
	RTS_ASSERT(rr->name);

	if (meta) {
		rr->meta = strdup(meta);
		RTS_ASSERT(rr->meta);
	}

	/* create entry field */
	rfd = reflection_field_data_create_internal(rts, NULL, 0);
	RTS_ASSERT(rfd);

	if (fyd_meta) {
		rfd->yaml_annotation = fyd_meta;
		rfd->yaml_annotation_str = rr->meta;
		fyd_meta = NULL;	/* rfd takes over now */

		rc = reflection_meta_fill(rfd->meta, fy_document_root(rfd->yaml_annotation));
		RTS_ASSERT(!rc);
	}

	ti = &rr->rfd_root_ti;
	fi = &rr->rfd_root_fi;

	/* create a pseudo struct enclosing the entry */
	fi->parent = NULL;
	fi->name = "";
	fi->type_info = ti;
	fi->offset = 0;

	ti->kind = FYTK_STRUCT;
	ti->flags = FYTIF_ANONYMOUS;
	ti->name = "";
	ti->size = ti_entry->size;
	ti->align = ti_entry->align;
	ti->count = 1;
	ti->fields = fi;

	rfd->fi = fi;

	rr->rfd_root = rfd;
	rfd = NULL;

	reflection_setup_type_ctx_setup(&rstc, rr);
	rc = reflection_setup_type(rts, ti_entry, &rstc, &rr->rtd_root, rr->rfd_root->meta);
	reflection_setup_type_ctx_cleanup(&rstc);

	RTS_ASSERT(!rc);

	rr->rfd_root->rtd = reflection_type_data_ref(rr->rtd_root);

	rc = reflection_field_data_specialize(rr->rfd_root);
	RTS_ASSERT(!rc);

	rc = reflection_type_data_simplify(rr->rtd_root);
	RTS_ASSERT(!rc);

	return rr;

err_out:
	reflection_reference_destroy(rr);
	fy_document_destroy(fyd_meta);
	reflection_field_data_destroy(rfd);
	return NULL;
}

struct reflection_type_system *
reflection_type_system_create(const struct reflection_type_system_config *cfg)
{
	struct fy_diag_cfg diag_default_cfg;
	struct reflection_type_system *rts = NULL;

	if (!cfg || !cfg->rfl || !cfg->entry_type)
		goto err_out;

	rts = malloc(sizeof(*rts));
	if (!rts)
		goto err_out;
	memset(rts, 0, sizeof(*rts));

	memcpy(&rts->cfg, cfg, sizeof(rts->cfg));

	if (!rts->cfg.diag) {
		fy_diag_cfg_default(&diag_default_cfg);
		rts->diag = fy_diag_create(&diag_default_cfg);
		if (!rts->diag)
			goto err_out;
	} else
		rts->diag = fy_diag_ref(rts->cfg.diag);

	/* can use RTS_ASSERT, RTS_ERROR_CHECK now */

	if (rts->cfg.flags & RTSCF_DUMP_REFLECTION) {
		fprintf(stderr, "reflection dump at start\n");
		fy_reflection_dump(rts->cfg.rfl, false, true);
		fprintf(stderr, "\n");
	}

	rts->root_ref = reflection_reference_create(rts, rts->cfg.entry_type, rts->cfg.entry_meta);
	RTS_ERROR_CHECK(rts->root_ref, "failed to create root reference for entry='%s' meta='%s'",
			rts->cfg.entry_type, rts->cfg.entry_meta ? : "");

	if (rts->cfg.flags & RTSCF_DUMP_REFLECTION) {
		fprintf(stderr, "%s: type system dump after simplification\n", __func__);
		reflection_type_data_dump(rts->root_ref->rtd_root);
		fprintf(stderr, "\n");
	}

	return rts;
err_out:
	reflection_type_system_destroy(rts);
	return NULL;
}

int
reflection_parse(struct fy_parser *fyp, struct reflection_type_data *rtd, void **datap,
		 enum reflection_parse_flags flags)
{
	void *data = NULL;
	int rc;

	if (!fyp || !rtd || !datap)
		return -1;

	*datap = NULL;

	data = reflection_type_data_alloc(rtd);
	RP_ASSERT(data);

	rc = reflection_parse_into(fyp, rtd, data, flags);
	if (rc < 0)
		goto err_out;

	/* no error, just no data */
	if (rc > 0) {
		reflection_type_data_free(rtd, data);
		data = NULL;
	}

	*datap = data;
	return rc;

err_out:
	reflection_type_data_free(rtd, data);
	return -1;
}

/* create a pseudo struct enclosing the entry */
struct reflection_walker *
reflection_root_ctx_setup(struct reflection_root_ctx *rctx,
			  struct reflection_type_data *rtd, void *data, struct reflection_meta *meta)
{
	struct reflection_type_system *rts;
	struct fy_type_info *ti;
	struct fy_field_info *fi;
	struct reflection_type_data *rtd_root;
	struct reflection_field_data *rfd;
	struct reflection_walker *rw_root, *rwr;

	assert(rtd);
	assert(rtd->rts);
	rts = rtd->rts;

	memset(rctx, 0, sizeof(*rctx));

	ti = &rctx->ti_local;
	fi = &rctx->fi_local;
	rtd_root = &rctx->rtd_root_local;
	rfd = &rctx->rfd_local;
	rw_root = &rctx->rw_root_local;
	rwr = &rctx->rwr_local;

	fi->parent = NULL;
	fi->name = "";
	fi->type_info = ti;

	ti->kind = FYTK_STRUCT;
	ti->flags = FYTIF_ANONYMOUS;
	ti->name = "";
	ti->size = rtd->ti->size;
	ti->align = rtd->ti->align;
	ti->count = 1;
	ti->fields = fi;

	rfd->rts = rts;
	rfd->fi = fi;
	rfd->field_name = (char *)fi->name;
	rfd->rtd = rtd;
	rfd->meta = NULL;	/* XXX */
	rctx->field_tab[0] = rfd;

	rtd_root->refs = 0;
	rtd_root->idx = -1;
	rtd_root->rts = rts;
	rtd_root->ti = ti;
	rtd_root->ops = &reflection_ops_table[FYTK_STRUCT];
	rtd_root->flags = RTDF_ROOT;
	rtd_root->flat_field_idx = -1;
	rtd_root->selector_field_idx = -1;
	rtd_root->union_field_idx = -1;
	rtd_root->fields_count = 1;
	rtd_root->fields = rctx->field_tab;
	rtd_root->meta = meta;

	rw_root->flags = RWF_ROOT | RWF_MAP;
	rw_root->data = data;
	rw_root->data_size.bytes = ti->size;
	rw_root->rtd = rtd_root;

	reflection_rw_value(rwr, rtd, data);
	rwr->parent = rw_root;
	rwr->flags |= RWF_FIELD_IDX;

	return rwr;
}

int
reflection_parse_into(struct fy_parser *fyp, struct reflection_type_data *rtd, void *data,
		      enum reflection_parse_flags flags)
{
	struct reflection_root_ctx rctx_local, *rctx = &rctx_local;
	struct reflection_walker *rw;
	struct fy_event *fye = NULL;

	int rc;

	if (!fyp || !rtd || !data)
		return -1;

	/* nothing, return 1 */
	fye = fy_parser_parse(fyp);
	if (!fye || fye->type == FYET_STREAM_END) {
		fy_parser_event_free(fyp, fye);
		return 1;
	}

	/* stream start, consume */
	if (fye->type == FYET_STREAM_START) {
		fy_parser_event_free(fyp, fye);
		fye = fy_parser_parse(fyp);
	}

	RP_INPUT_CHECK(fye != NULL, "premature end of input");

	RP_INPUT_CHECK(fye->type == FYET_DOCUMENT_START,
			"expected FYET_DOCUMENT_START error (got %s)",
			fy_event_type_get_text(fye->type));

	fy_parser_event_free(fyp, fye);
	fye = NULL;

	memset(data, 0, rtd->ti->size);
	rw = reflection_root_ctx_setup(rctx, rtd, data, NULL);
	rc = reflection_parse_rw(fyp, rw, flags);

	if (!rc && fy_parser_get_stream_error(fyp))
		rc = -1;
	if (rc < 0)
		goto out;

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input");

	RP_INPUT_CHECK(fye->type == FYET_DOCUMENT_END,
			"expected FYET_DOCUMENT_END error (got %s)",
			fy_event_type_get_text(fye->type));

	fy_parser_event_free(fyp, fye);
	fye = NULL;

	rc = 0;
out:
	fy_parser_event_free(fyp, fye);
	return rc;

err_input:
	rc = -EINVAL;
	goto out;
}

int reflection_emit(struct fy_emitter *emit, struct reflection_type_data *rtd, const void *data,
		    enum reflection_emit_flags flags)
{
	struct reflection_root_ctx rctx_local, *rctx = &rctx_local;
	struct reflection_walker *rw;
	bool emit_ss, emit_ds, emit_de, emit_se;
	int rc;

	emit_ss = !!(flags & REF_EMIT_SS);
	emit_ds = !!(flags & REF_EMIT_DS);
	emit_de = !!(flags & REF_EMIT_DE);
	emit_se = !!(flags & REF_EMIT_SE);

	if (emit_ss) {
		rc = fy_emit_eventf(emit, FYET_STREAM_START);
		RE_ASSERT(!rc);
	}

	if (rtd && data) {

		if (emit_ds) {
			rc = fy_emit_eventf(emit, FYET_DOCUMENT_START, 0, NULL, NULL);
			RE_ASSERT(!rc);
		}

		rw = reflection_root_ctx_setup(rctx, rtd, (void *)data, NULL);
		rc = reflection_emit_rw(emit, rw, flags);
		RE_ASSERT(!rc);

		if (emit_de) {
			rc = fy_emit_eventf(emit, FYET_DOCUMENT_END, 0);
			RE_ASSERT(!rc);
		}
	}

	if (emit_se) {
		rc = fy_emit_eventf(emit, FYET_STREAM_END);
		RE_ASSERT(!rc);
	}

	return 0;

err_out:
	return -1;
}


#endif /* HAVE_REFLECTION */
