/*
 * fy-generic.c - space efficient generics
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdalign.h>
#include <ctype.h>
#include <errno.h>

#include <stdio.h>

/* for container_of */
#include "fy-list.h"

#include "fy-allocator.h"
#include "fy-generic.h"

struct fy_generic_builder *fy_generic_builder_create(struct fy_allocator *a, fy_alloc_tag shared_tag)
{
	fy_alloc_tag alloc_tag = FY_ALLOC_TAG_NONE;
	struct fy_generic_builder *gb = NULL;

	if (!a)
		return NULL;

	alloc_tag = shared_tag;
	if (alloc_tag == FY_ALLOC_TAG_NONE) {
		alloc_tag = fy_allocator_get_tag(a, NULL);
		if (alloc_tag == FY_ALLOC_TAG_ERROR)
			goto err_out;
	}

	gb = malloc(sizeof(*gb));
	if (!gb)
		goto err_out;
	memset(gb, 0, sizeof(*gb));

	gb->allocator = a;
	gb->shared_tag = shared_tag;
	gb->alloc_tag = alloc_tag;

	return gb;

err_out:
	if (shared_tag != FY_ALLOC_TAG_NONE)
		fy_allocator_release_tag(a, alloc_tag);
	if (gb)
		free(gb);
	return NULL;
}

void fy_generic_builder_destroy(struct fy_generic_builder *gb)
{
	if (!gb)
		return;

	if (gb->linear)
		free(gb->linear);

	/* if we own the allocator, just destroy it, everything is gone */
	if (gb->allocator && gb->owns_allocator)
		fy_allocator_destroy(gb->allocator);
	else if (gb->shared_tag == FY_ALLOC_TAG_NONE)
		fy_allocator_release_tag(gb->allocator, gb->alloc_tag);

	free(gb);
}

void fy_generic_builder_reset(struct fy_generic_builder *gb)
{
	if (!gb)
		return;

	if (gb->linear)
		free(gb->linear);

	if (gb->shared_tag == FY_ALLOC_TAG_NONE)
		fy_allocator_reset_tag(gb->allocator, gb->alloc_tag);
}

fy_generic fy_generic_float_create(struct fy_generic_builder *gb, double val)
{
	const double *valp;
#ifdef FY_HAS_64BIT_PTR
	float f;
	uint32_t fi;

	if (fy_double_fits_in_float(val)) {
		f = (float)val;
		memcpy(&fi, &f, sizeof(fi));
		return ((fy_generic)fi << FY_FLOAT_INPLACE_SHIFT) | FY_FLOAT_INPLACE_V;
	}
#endif
	valp = fy_generic_builder_store(gb, &val, sizeof(val), FY_SCALAR_ALIGNOF(double));
	if (!valp)
		return fy_invalid;
	assert(((uintptr_t)valp & FY_INPLACE_TYPE_MASK) == 0);
	return (fy_generic)valp | FY_FLOAT_OUTPLACE_V;
}

fy_generic fy_generic_string_size_create(struct fy_generic_builder *gb, const char *str, size_t len)
{
	uint8_t lenbuf[FYGT_SIZE_ENCODING_MAX];
	struct fy_iovecw iov[3];
	const void *s;
	void *p;

	switch (len) {
	case 0:
		return ((fy_generic)0) |
			(0 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;
	case 1:
		return ((fy_generic)str[0] << 8) |
			(1 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;
	case 2:
		return ((fy_generic)str[0] << 8) |
		       ((fy_generic)str[1] << 16) |
			(2 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;
	case 3:
		return ((fy_generic)str[0] << 8) |
		       ((fy_generic)str[1] << 16) |
		       ((fy_generic)str[2] << 24) |
			(3 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;
#ifdef FY_HAS_64BIT_PTR
	case 4:
		return ((fy_generic)str[0] << 8) |
		       ((fy_generic)str[1] << 16) |
		       ((fy_generic)str[2] << 24) |
		       ((fy_generic)str[3] << 32) |
			(4 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;
	case 5:
		return ((fy_generic)str[0] << 8) |
		       ((fy_generic)str[1] << 16) |
		       ((fy_generic)str[2] << 24) |
		       ((fy_generic)str[3] << 32) |
		       ((fy_generic)str[4] << 40) |
			(5 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;
	case 6:
		return ((fy_generic)str[0] << 8) |
		       ((fy_generic)str[1] << 16) |
		       ((fy_generic)str[2] << 24) |
		       ((fy_generic)str[3] << 32) |
		       ((fy_generic)str[4] << 40) |
		       ((fy_generic)str[5] << 48) |
			(6 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;
	case 7:
		return ((fy_generic)str[0] << 8) |
		       ((fy_generic)str[1] << 16) |
		       ((fy_generic)str[2] << 24) |
		       ((fy_generic)str[3] << 32) |
		       ((fy_generic)str[4] << 40) |
		       ((fy_generic)str[5] << 48) |
		       ((fy_generic)str[6] << 56) |
			(7 << FY_STRING_INPLACE_SIZE_SHIFT) | FY_STRING_INPLACE_V;
#endif
	default:
		break;
	}

	p = fy_encode_size(lenbuf, sizeof(lenbuf), len);
	assert(p);

	iov[0].data = lenbuf;
	iov[0].size = (size_t)((uint8_t *)p - lenbuf) ;
	iov[1].data = str;
	iov[1].size = len;
	iov[2].data = "\x00";	/* null terminate always */
	iov[2].size = 1;

	/* strings are aligned at 8 always */
	s = fy_generic_builder_storev(gb, iov, ARRAY_SIZE(iov), 8);
	if (!s)
		return fy_invalid;

	assert(((uintptr_t)s & FY_INPLACE_TYPE_MASK) == 0);
	return (fy_generic)s | FY_STRING_OUTPLACE_V;
}

fy_generic
fy_generic_string_vcreate(struct fy_generic_builder *gb, const char *fmt, va_list ap)
{
	va_list ap2;
	char *str;
	size_t size;

	va_copy(ap2, ap);

	size = vsnprintf(NULL, 0, fmt, ap);
	if (size < 0)
		return fy_invalid;

	str = alloca(size + 1);
	size = vsnprintf(str, size + 1, fmt, ap2);
	if (size < 0)
		return fy_invalid;

	return fy_generic_string_size_create(gb, str, size);
}

fy_generic
fy_generic_string_createf(struct fy_generic_builder *gb, const char *fmt, ...)
{
	va_list ap;
	fy_generic v;

	va_start(ap, fmt);
	v = fy_generic_string_vcreate(gb, fmt, ap);
	va_end(ap);

	return v;
}

fy_generic fy_generic_sequence_create(struct fy_generic_builder *gb, size_t count, const fy_generic *items)
{
	struct fy_generic_sequence s;
	const struct fy_generic_sequence *p;
	struct fy_iovecw iov[2];
	size_t i;

	if (count && !items)
		return fy_invalid;

	for (i = 0; i < count; i++) {
		if (items[i] == fy_invalid)
			return fy_invalid;
	}

	memset(&s, 0, sizeof(s));
	s.count = count;

	iov[0].data = &s;
	iov[0].size = sizeof(s);
	iov[1].data = items;
	iov[1].size = count * sizeof(*items);

	p = fy_generic_builder_storev(gb, iov, ARRAY_SIZE(iov), FY_CONTAINER_ALIGNOF(struct fy_generic_sequence));
	if (!p)
		return fy_invalid;

	return (fy_generic)p | FY_SEQ_V;
}

fy_generic fy_generic_mapping_create(struct fy_generic_builder *gb, size_t count, const fy_generic *pairs)
{
	struct fy_generic_mapping m;
	const struct fy_generic_mapping *p;
	struct fy_iovecw iov[2];
	size_t i;

	if (count && !pairs)
		return fy_invalid;

	for (i  = 0; i < count * 2; i++) {
		if (pairs[i] == fy_invalid)
			return fy_invalid;
	}

	memset(&m, 0, sizeof(m));
	m.count = count;

	iov[0].data = &m;
	iov[0].size = sizeof(m);
	iov[1].data = pairs;
	iov[1].size = 2 * count * sizeof(*pairs);

	p = fy_generic_builder_storev(gb, iov, ARRAY_SIZE(iov), FY_CONTAINER_ALIGNOF(struct fy_generic_mapping));
	if (!p)
		return fy_invalid;

	return (fy_generic)p | FY_MAP_V;
}

fy_generic fy_generic_mapping_lookup(fy_generic map, fy_generic key)
{
	struct fy_generic_mapping *p;
	const fy_generic *pair;
	size_t i;

	p = fy_generic_resolve_collection_ptr(map);
	assert(p);
	pair = p->pairs;
	for (i = 0; i < p->count; i++, pair += 2) {
		if (fy_generic_compare(key, pair[0]) == 0)
			return pair[1];
	}
	return fy_invalid;
}

fy_generic fy_generic_indirect_create(struct fy_generic_builder *gb, const struct fy_generic_indirect *gi)
{
	const void *p;
	struct fy_iovecw iov[4];
	size_t cnt;
	uint8_t flags;

	cnt = 0;

	flags = 0;
	if (gi->value != fy_invalid)
		flags |= FYGIF_VALUE;
	if (gi->anchor != fy_null && gi->anchor != fy_invalid)
		flags |= FYGIF_ANCHOR;
	if (gi->tag != fy_null && gi->tag != fy_invalid)
		flags |= FYGIF_TAG;
	iov[cnt].data = &flags;
	iov[cnt++].size = sizeof(flags);
	if (flags & FYGIF_VALUE) {
		iov[cnt].data = &gi->value;
		iov[cnt++].size = sizeof(gi->value);
	}
	if (flags & FYGIF_ANCHOR) {
		iov[cnt].data = &gi->anchor;
		iov[cnt++].size = sizeof(gi->anchor);
	}
	if (flags & FYGIF_TAG) {
		iov[cnt].data = &gi->tag;
		iov[cnt++].size = sizeof(gi->tag);
	}

	p = fy_generic_builder_storev(gb, iov, cnt, FY_SCALAR_ALIGNOF(uint8_t));	/* must be at least 8 */
	if (!p)
		return fy_invalid;

	return (fy_generic)p | FY_INDIRECT_V;
}

fy_generic fy_generic_alias_create(struct fy_generic_builder *gb, fy_generic anchor)
{
	struct fy_generic_indirect gi = {
		.value = fy_invalid,
		.anchor = anchor,
		.tag = fy_invalid,
	};

	return fy_generic_indirect_create(gb, &gi);
}

fy_generic fy_generic_create_scalar_from_text(struct fy_generic_builder *gb, enum fy_generic_schema schema, const char *text, size_t len, enum fy_generic_type force_type)
{
	fy_generic v;
	const char *s, *e;
	const char *dec, *fract, *exp;
	char sign, exp_sign;
	size_t dec_count, fract_count, exp_count;
	int base;
	long long lv;
	double dv;
	char *t;
	bool is_json;

	v = fy_invalid;

	/* force a string? okie-dokie */
	if (force_type == FYGT_STRING)
		goto do_string;

	/* more than 4K it is definitely a string */
	if (len > 4096)
		goto do_string;

	/* first stab at direct matches */
	switch (schema) {
	case FYGS_YAML1_2_FAILSAFE:
		goto do_string;

	case FYGS_YAML1_2_JSON:
	case FYGS_JSON:
		if (len == 4) {
			if (!memcmp(text, "null", 4)) {
				v = fy_null;
				break;
			}
			if (!memcmp(text, "true", 4)) {
				v = fy_true;
				break;
			}
		} else if (len == 5) {
			if (!memcmp(text, "false", 5)) {
				v = fy_false;
				break;
			}
		}
		break;

	case FYGS_YAML1_2_CORE:

		if (len == 0 || (len == 1 && *text == '~')) {
			v = fy_null;
			break;
		}

		if (len == 4) {
		    if (!memcmp(text, "null", 4) || !memcmp(text, "Null", 4) || !memcmp(text, "NULL", 4)) {
			    v = fy_null;
			    break;
		    }
		    if (!memcmp(text, "true", 4) || !memcmp(text, "True", 4) || !memcmp(text, "TRUE", 4)) {
			    v = fy_true;
			    break;
		    }
		    if (!memcmp(text, ".inf", 4) || !memcmp(text, ".Inf", 4) || !memcmp(text, ".INF", 4)) {
				v = fy_generic_float_create(gb, INFINITY);
				break;
		    }
		    if (!memcmp(text, ".nan", 4) || !memcmp(text, ".Nan", 4) || !memcmp(text, ".NAN", 4)) {
				v = fy_generic_float_create(gb, NAN);
				break;
		    }
		}
		if (len == 5) {
		    if (!memcmp(text, "false", 5) || !memcmp(text, "False", 5) || !memcmp(text, "FALSE", 5)) {
			    v = fy_false;
			    break;
		    }
		    if (!memcmp(text, "+.inf", 5) || !memcmp(text, "+.Inf", 5) || !memcmp(text, "+.INF", 5)) {
				v = fy_generic_float_create(gb, INFINITY);
				break;
		    }
		    if (!memcmp(text, "-.inf", 5) || !memcmp(text, "-.Inf", 5) || !memcmp(text, "-.INF", 5)) {
				v = fy_generic_float_create(gb, -INFINITY);
				break;
		    }
		}

		break;

	case FYGS_YAML1_1:
		/* not yet */
		goto do_string;

	default:
		break;
	}

	if (v != fy_invalid)
		goto do_check_cast;

	s = text;
	e = s + len;

	dec = fract = exp = NULL;
	dec_count = fract_count = exp_count = 0;
	sign = exp_sign = ' ';
	lv = 0;
	base = 10;

	is_json = fy_generic_schema_is_json(schema);

	/* skip over sign */
	if (s < e && (*s == '-' || (!is_json && *s == '+')))
		sign = *s++;

	(void)sign;

	dec = s;
	if (s < e && *s == '0') {
		s++;
		if (!is_json) {
			if (*s == 'x') {
				base = 16;
				s++;
			} else if (*s == 'o') {
				base = 8;
				s++;
			}
		} else {
			/* json does not allow a 0000... form */
			if (*s == '0')
				goto do_string;
		}
	}

	/* consume digits */
	if (base == 16) {
		while (s < e && isxdigit(*s))
			s++;
	} else if (base == 10) {
		while (s < e && isdigit(*s))
			s++;
	} else if (base == 8) {
		while (s < e && (*s >= 0 && *s <= 7))
			s++;
	}
	dec_count = s - dec;

	/* extract the fractional part */
	if (s < e && *s == '.') {
		if (base != 10)
			goto do_string;
		fract = ++s;
		while (s < e && isdigit(*s))
			s++;
		fract_count = s - fract;
	}

	/* extract the exponent part */ 
	if (s < e && (*s == 'e' || *s == 'E')) {
		if (base != 10)
			goto do_string;
		s++;
		if (s < e && (*s == '+' || *s == '-'))
			exp_sign = *s++;
		exp = s;
		while (s < e && isdigit(*s))
			s++;
		exp_count = s - exp;
	}

	/* not fully consumed? then just a string */
	if (s < e)
		goto do_string;

	// fprintf(stderr, "\n base=%d sign=%c dec=%.*s fract=%.*s exp_sign=%c exp=%.*s\n",
	//		base, sign, (int)dec_count, dec, (int)fract_count, fract, exp_sign, (int)exp_count, exp);

	/* all schemas require decimal digits */
	if (!dec || !dec_count)
		goto do_string;

	t = alloca(len + 1);
	memcpy(t, text, len);
	t[len] = '\0';

	/* it is an integer */
	if (!fract_count && !exp_count) {
		errno = 0;
		lv = strtoll(t, NULL, base);
		if (errno == ERANGE)
			goto do_string;

		v = fy_generic_int_create(gb, lv);
		goto do_check_cast;
	} else {
		errno = 0;
		dv = strtod(t, NULL);
		if (errno == ERANGE)
			goto do_string;

		v = fy_generic_float_create(gb, dv);
		goto do_check_cast;
	}

do_string:
	/* everything tried, just a string */
	v = fy_generic_string_size_create(gb, text, len);

do_check_cast:
	if (force_type != FYGT_INVALID && fy_generic_get_type(v) != force_type) {
		fprintf(stderr, "cast failed\n");
		return fy_invalid;
	}
	return v;
}

int fy_generic_sequence_compare(fy_generic seqa, fy_generic seqb)
{
	size_t i, counta, countb;
	const fy_generic *itemsa, *itemsb;
	int ret;

	if (seqa == seqb)
		return 0;

	itemsa = fy_generic_sequence_get_items(seqa, &counta);
	itemsb = fy_generic_sequence_get_items(seqb, &countb);

	if (counta != countb)
		goto out;

	/* empty? just fine */
	if (counta == 0)
		return 0;

	/* try to cheat by comparing contents */
	ret = memcmp(itemsa, itemsb, counta * sizeof(*itemsa));
	if (!ret)
		return 0;	/* great! binary match */

	/* have to do it the hard way */
	for (i = 0; i < counta; i++) {
		ret = fy_generic_compare(itemsa[i], itemsb[i]);
		if (ret)
			goto out;
	}

	/* exhaustive check */
	return 0;
out:
	return seqa > seqb ? 1 : -1;	/* keep order, but it's just address based */
}

int fy_generic_mapping_compare(fy_generic mapa, fy_generic mapb)
{
	size_t i, counta, countb;
	const fy_generic *pairsa, *pairsb;
	fy_generic key, vala, valb;
	int ret;

	if (mapa == mapb)
		return 0;

	pairsa = fy_generic_mapping_get_pairs(mapa, &counta);
	pairsb = fy_generic_mapping_get_pairs(mapb, &countb);

	if (counta != countb)
		goto out;

	/* empty? just fine */
	if (counta == 0)
		return 0;

	/* try to cheat by comparing contents */
	ret = memcmp(pairsa, pairsb, counta * 2 * sizeof(*pairsa));
	if (!ret)
		return 0;	/* great! binary match */

	/* have to do it the hard way */
	for (i = 0; i < counta * 2; i++) {
		key = pairsa[i * 2];
		vala = pairsa[i * 2 + 1];

		/* find if the key exists in the other mapping */
		valb = fy_generic_mapping_lookup(mapa, key);
		if (valb == fy_invalid)
			goto out;

		/* compare values */
		ret = fy_generic_compare(vala, valb);
		if (ret)
			goto out;
	}

	/* all the keys value pairs match */

	return 0;
out:
	return mapa > mapb ? 1 : -1;	/* keep order, but it's just address based */
}

static inline int fy_generic_bool_compare(fy_generic a, fy_generic b)
{
	int ba, bb;

	ba = (int)fy_generic_get_bool(a);
	bb = (int)fy_generic_get_bool(b);
	return ba > bb ?  1 :
	       ba < bb ? -1 : 0;
}

static inline int fy_generic_int_compare(fy_generic a, fy_generic b)
{
	long long ia, ib;

	ia = fy_generic_get_int(a);
	ib = fy_generic_get_int(b);
	return ia > ib ?  1 :
	       ia < ib ? -1 : 0;
}

static inline int fy_generic_float_compare(fy_generic a, fy_generic b)
{
	double da, db;

	da = fy_generic_get_float(a);
	db = fy_generic_get_float(b);
	return da > db ?  1 :
	       da < db ? -1 : 0;
}

static inline int fy_generic_string_compare(fy_generic a, fy_generic b)
{
	const char *sa, *sb;
	size_t sza = 0, szb = 0;
	int ret;

	sa = fy_generic_get_string_size_alloca(a, &sza);
	sb = fy_generic_get_string_size_alloca(b, &szb);

	ret = memcmp(sa, sb, sza > szb ? szb : sza);

	if (!ret && sza != szb)
		ret = 1;
	return ret;
}

static inline int fy_generic_alias_compare(fy_generic a, fy_generic b)
{
	const char *sa, *sb;
	size_t sza = 0, szb = 0;
	int ret;

	sa = fy_generic_get_alias_size_alloca(a, &sza);
	sb = fy_generic_get_alias_size_alloca(b, &szb);

	ret = memcmp(sa, sb, sza > szb ? szb : sza);

	if (!ret && sza != szb)
		ret = 1;
	return ret;
}

int fy_generic_compare_out_of_place(fy_generic a, fy_generic b)
{
	enum fy_generic_type at, bt;

	/* invalids are always non-matching */
	if (a == fy_invalid || b == fy_invalid)
		return -1;

	/* equals? nice - should work for null, bool, in place int, float and strings  */
	/* also for anything that's a pointer */
	if (a == b)
		return 0;

	at = fy_generic_get_type(a);
	bt = fy_generic_get_type(b);

	/* invalid types, or differing types do not match */
	if (at != bt)
		return -1;

	switch (fy_generic_get_type(a)) {
	case FYGT_NULL:
		return 0;	/* two nulls are always equal to each other */

	case FYGT_BOOL:
		return fy_generic_bool_compare(a, b);

	case FYGT_INT:
		return fy_generic_int_compare(a, b);

	case FYGT_FLOAT:
		return fy_generic_float_compare(a, b);

	case FYGT_STRING:
		return fy_generic_string_compare(a, b);

	case FYGT_SEQUENCE:
		return fy_generic_sequence_compare(a, b);

	case FYGT_MAPPING:
		return fy_generic_mapping_compare(a, b);

	case FYGT_ALIAS:
		return fy_generic_alias_compare(a, b);

	default:
		/* we don't handle anything else */
		assert(0);
		abort();
	}
}

#define COPY_MALLOC_CUTOFF	256

fy_generic fy_generic_builder_copy_out_of_place(struct fy_generic_builder *gb, fy_generic v)
{
	const struct fy_generic_sequence *seqs;
	fy_generic vi;
	const struct fy_generic_mapping *maps;
	struct fy_iovecw iov[2];
	enum fy_generic_type type;
	size_t size, len, i, count;
	const void *valp;
	const uint8_t *p, *str;
	const fy_generic *itemss;
	fy_generic *items;

	if (v == fy_invalid)
		return fy_invalid;

	/* indirects are handled here (note, aliases are indirect too) */
	if (fy_generic_is_indirect(v)) {
		struct fy_generic_indirect gi;

		fy_generic_indirect_get(v, &gi);

		gi.value = fy_generic_builder_copy(gb, gi.value);
		gi.anchor = fy_generic_builder_copy(gb, gi.anchor);
		gi.tag = fy_generic_builder_copy(gb, gi.tag);

		return fy_generic_indirect_create(gb, &gi);
	}

	type = fy_generic_get_type(v);
	if (type == FYGT_NULL || type == FYGT_BOOL)
		return v;

	/* if we got to here, it's going to be a copy */
	switch (type) {
	case FYGT_INT:
		if (v & FY_INT_INPLACE_V)
			return v;
		valp = fy_generic_builder_store(gb, fy_generic_resolve_ptr(v),
				sizeof(long long), FY_SCALAR_ALIGNOF(long long));
		if (!valp)
			return fy_invalid;
		return (fy_generic)valp | FY_INT_OUTPLACE_V;

	case FYGT_FLOAT:
		if (v & FY_FLOAT_INPLACE_V)
			return v;
		valp = fy_generic_builder_store(gb, fy_generic_resolve_ptr(v),
				sizeof(double), FY_SCALAR_ALIGNOF(double));
		if (!valp)
			return fy_invalid;
		return (fy_generic)valp | FY_FLOAT_OUTPLACE_V;

	case FYGT_STRING:
		if (v & FY_STRING_INPLACE_V)
			return v;
		p = fy_generic_resolve_ptr(v);
		str = fy_decode_size(p, FYGT_SIZE_ENCODING_MAX, &len);
		if (!str)
			return fy_invalid;
		size = (size_t)(str - p) + len;
		valp = fy_generic_builder_store(gb, p, size, 8);
		if (!valp)
			return fy_invalid;
		return (fy_generic)valp | FY_STRING_OUTPLACE_V;

	case FYGT_SEQUENCE:
		seqs = fy_generic_resolve_collection_ptr(v);
		count = seqs->count;
		itemss = seqs->items;

		size = sizeof(*items) * count;
		if (size <= COPY_MALLOC_CUTOFF)
			items = alloca(size);
		else {
			items = malloc(size);
			if (!items)
				return fy_invalid;
		}

		for (i = 0; i < count; i++) {
			vi = fy_generic_builder_copy(gb, itemss[i]);
			if (vi == fy_invalid)
				break;
			items[i] = vi;
		}

		if (i >= count) {
			iov[0].data = seqs;
			iov[0].size = sizeof(*seqs);
			iov[1].data = items;
			iov[1].size = size;
			valp = fy_generic_builder_storev(gb, iov, ARRAY_SIZE(iov),
					FY_CONTAINER_ALIGNOF(struct fy_generic_sequence));
		} else
			valp = NULL;

		if (size > COPY_MALLOC_CUTOFF)
			free(items);

		if (!valp)
			return fy_invalid;

		return (fy_generic)valp | FY_SEQ_V;

	case FYGT_MAPPING:
		maps = fy_generic_resolve_collection_ptr(v);
		count = maps->count * 2;
		itemss = maps->pairs;

		size = sizeof(*items) * count;
		if (size <= COPY_MALLOC_CUTOFF)
			items = alloca(size);
		else {
			items = malloc(size);
			if (!items)
				return fy_invalid;
		}

		if (size > COPY_MALLOC_CUTOFF)
			free(items);

		/* copy both keys and values */
		for (i = 0; i < count; i++) {
			vi = fy_generic_builder_copy(gb, itemss[i]);
			if (vi == fy_invalid)
				break;
			items[i] = vi;
		}

		if (i >= count) {
			iov[0].data = maps;
			iov[0].size = sizeof(*maps);
			iov[1].data = items;
			iov[1].size = size,
			valp = fy_generic_builder_storev(gb, iov, ARRAY_SIZE(iov),
					FY_CONTAINER_ALIGNOF(struct fy_generic_mapping));
		} else
			valp = NULL;

		if (size > COPY_MALLOC_CUTOFF)
			free(items);

		if (!valp)
			return fy_invalid;

		return (fy_generic)valp | FY_MAP_V;

	default:
		break;
	}

	assert(0);
	abort();
	return fy_invalid;
}

fy_generic fy_generic_relocate(void *start, void *end, fy_generic v, ptrdiff_t d)
{
	void *p;
	struct fy_generic_indirect *gi;
	struct fy_generic_sequence *seq;
	struct fy_generic_mapping *map;
	fy_generic *items, *pairs;
	size_t i, count;

	/* the delta can't have those bits */
	assert((d & FY_INPLACE_TYPE_MASK) == 0);

	/* no relocation needed */
	if (d == 0)
		return v;

	/* if it's indirect, resolve the internals */
	if (fy_generic_is_indirect(v)) {

		/* check if already relocated */
		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v = fy_generic_relocate_ptr(v, d) | FY_INDIRECT_V;
		gi = fy_generic_resolve_ptr(v);
		gi->value = fy_generic_relocate(start, end, gi->value, d);
		gi->anchor = fy_generic_relocate(start, end, gi->anchor, d);
		gi->tag = fy_generic_relocate(start, end, gi->tag, d);
		return v;
	}

	/* if it's not indirect, it might be one of the in place formats */
	switch (fy_generic_get_type(v)) {
	case FYGT_NULL:
	case FYGT_BOOL:
		return v;

	case FYGT_INT:
		if ((v & FY_INPLACE_TYPE_MASK) == FY_INT_INPLACE_V)
			return v;

		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v = fy_generic_relocate_ptr(v, d) | FY_INT_OUTPLACE_V;
		break;

	case FYGT_FLOAT:
		if ((v & FY_INPLACE_TYPE_MASK) == FY_FLOAT_INPLACE_V)
			return v;

		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v = fy_generic_relocate_ptr(v, d) | FY_FLOAT_OUTPLACE_V;
		break;

	case FYGT_STRING:
		if ((v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V)
			return v;

		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v = fy_generic_relocate_ptr(v, d) | FY_STRING_OUTPLACE_V;
		break;

	case FYGT_SEQUENCE:
		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v = fy_generic_relocate_collection_ptr(v, d) | FY_SEQ_V;
		seq = fy_generic_resolve_collection_ptr(v);
		count = seq->count;
		items = (fy_generic *)seq->items;
		for (i = 0; i < count; i++)
			items[i] = fy_generic_relocate(start, end, items[i], d);
		break;

	case FYGT_MAPPING:
		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v = fy_generic_relocate_collection_ptr(v, d) | FY_MAP_V;
		map = fy_generic_resolve_collection_ptr(v);
		count = map->count * 2;
		pairs = (fy_generic *)map->pairs;
		for (i = 0; i < count; i++)
			pairs[i] = fy_generic_relocate(start, end, pairs[i], d);
		break;

	default:
		/* should never get here */
		assert(0);
		abort();
		break;
	}

	return v;
}
