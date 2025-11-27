/*
 * libfyaml-test-generic.c - libfyaml generics tests
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
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
#include <unistd.h>
#include <limits.h>

#include <check.h>

#include <libfyaml.h>

#include "fy-generic.h"
#include "fy-generic-decoder.h"
#include "fy-generic-encoder.h"

/* Test: Basic generic types, sanity testing */
START_TEST(generic_basics)
{
	fy_generic v;
	fy_generic_sequence_handle seqh;
	fy_generic_mapping_handle maph;
	int i;
	const char *str;

	v = fy_invalid;
	ck_assert(!fy_generic_is_valid(v));

	/* null */
	v = fy_null;
	ck_assert(fy_generic_is_null_type(v));
	ck_assert(fy_generic_is_in_place(v));

	/* bool */
	v = fy_local_bool(true);
	ck_assert(fy_generic_is_bool_type(v));
	ck_assert(fy_generic_is_in_place(v));

	/* int (in place) */
	v = fy_local_int(100);
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(fy_generic_is_in_place(v));

	/* int (out of place) */
	v = fy_local_int(FYGT_INT_INPLACE_MAX+1);
	ck_assert(fy_get_type(v) == FYGT_INT);
	ck_assert(!fy_generic_is_in_place(v));

	/* float (in place in 64 bit), out of place for 32 bit */
	v = fy_local_float(100.0);
	ck_assert(fy_generic_is_float_type(v));

#ifdef FYGT_GENERIC_64
	ck_assert(fy_generic_is_in_place(v));
#else
	ck_assert(!fy_generic_is_in_place(v));
#endif

	/* double (out of place for both) */
	v = fy_local_float(DBL_MIN);
	ck_assert(fy_generic_is_float_type(v));
	ck_assert(!fy_generic_is_in_place(v));

	/* string of length 2 (3 with \0) in place always */
	v = fy_local_string("sh");
	ck_assert(fy_generic_is_string(v));
	ck_assert(fy_generic_is_in_place(v));

	/* string of length 6 (7 with \0) in place for 64 bit */
	v = fy_local_string("short1");
	ck_assert(fy_generic_is_string(v));
#ifdef FYGT_GENERIC_64
	ck_assert(fy_generic_is_in_place(v));
#else
	ck_assert(!fy_generic_is_in_place(v));
#endif

	/* long string is always out of place */
	v = fy_local_string("long string out of place");
	ck_assert(fy_generic_is_string(v));
	ck_assert(!fy_generic_is_in_place(v));

	/* sequence */
	v = fy_local_sequence(fy_local_int(10), fy_local_string("a string to test"));
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(!fy_generic_is_in_place(v));

	/* check that the sequence is correct */
	seqh = fy_cast(v, fy_seq_handle_null);
	ck_assert_ptr_ne(seqh, NULL);
	ck_assert(seqh->count == 2);
	ck_assert(fy_len(seqh) == 2);
	i = fy_cast(seqh->items[0], -1);
	ck_assert(i == 10);
	str = fy_castp(&seqh->items[1], "");
	ck_assert(!strcmp(str, "a string to test"));

	/* mapping */
	v = fy_local_mapping(fy_local_int(10), fy_local_string("a string to test"));
	ck_assert(fy_generic_is_mapping(v));
	ck_assert(!fy_generic_is_in_place(v));

	/* check that the mapping is correct */
	maph = fy_cast(v, fy_map_handle_null);
	ck_assert_ptr_ne(maph, NULL);
	ck_assert(maph->count == 1);
	ck_assert(fy_len(maph) == 1);
	i = fy_cast(maph->pairs[0].key, -1);
	ck_assert(i == 10);
	str = fy_castp(&maph->pairs[0].value, "");
	ck_assert(!strcmp(str, "a string to test"));
}
END_TEST

/* Test: testing bool range */
START_TEST(generic_bool_range)
{
	static const bool btable[2] = {
		false, true,
	};
	bool pass, test_fail;
	unsigned int i;
	fy_generic v;
	bool res, bv;

	test_fail = false;
	for (i = 0; i < ARRAY_SIZE(btable); i++) {
		bv = btable[i];
		v = fy_bool(bv);
		res = fy_cast(v, false);

		pass = bv == res;
		test_fail |= !pass;

		printf("boolean/%s = %016lx %s - %s\n",
				bv ? "true" : "false", v.v,
				res ? "true" : "false",
				pass ? "PASS" : "FAIL");
	}

	ck_assert(!test_fail);
}
END_TEST

/* Test: testing int range */
START_TEST(generic_int_range)
{
	static const long long itable[] = {
		0, 1, -1, LLONG_MAX, LLONG_MIN,
		FYGT_INT_INPLACE_MAX, FYGT_INT_INPLACE_MIN,
		FYGT_INT_INPLACE_MAX+1, FYGT_INT_INPLACE_MIN-1,
	};
	bool pass, test_fail;
	unsigned int i;
	fy_generic v;
	long long res, iv;

	test_fail = false;
	for (i = 0; i < ARRAY_SIZE(itable); i++) {
		iv = itable[i];
		v = fy_int(iv);
		res = fy_cast(v, (long long)0);

		pass = iv == res;
		test_fail |= !pass;

		printf("int/%lld = %016lx %lld - %s\n",
				iv, v.v, res,
				pass ? "PASS" : "FAIL");
	}

	ck_assert(!test_fail);
}
END_TEST

/* Test: testing float range */
START_TEST(generic_float_range)
{
	static const double ftable[] = {
		0.0, -0,0, 1.0, -1.0, 0.1, -0.1,
		128.0, -128.0,
		256.1, -256.1,
		INFINITY, -INFINITY,
		NAN, -NAN,
		100000.00001,	/* does not fit in 32 bit float */
		FLT_MIN, FLT_MAX,
		DBL_MIN, DBL_MAX,
	};
	bool pass, test_fail;
	unsigned int i;
	fy_generic v;
	double res, fv;

	test_fail = false;
	for (i = 0; i < ARRAY_SIZE(ftable); i++) {
		fv = ftable[i];
		v = fy_float(fv);
		res = fy_cast(v, (double)0.0);

		if (isfinite(fv))
			pass = fv == res;
		else if (isinf(fv))
			pass = isinf(fv) == isinf(res);
		else if (isnan(fv))
			pass = isnan(fv) == isnan(res);
		else
			pass = false;

		test_fail |= !pass;

		printf("float/%g = %016lx %g - %s\n",
				fv, v.v, res,
				pass ? "PASS" : "FAIL");
	}

	ck_assert(!test_fail);
}
END_TEST

/* Test: testing size encoding */
START_TEST(generic_size_encoding)
{
	static const size_t sztable[] = {
		0,
		((size_t)1 <<  7) - 1, ((size_t)1 <<  7), ((size_t)1 <<  7) + 1,
		((size_t)1 << 14) - 1, ((size_t)1 << 14), ((size_t)1 << 14) + 1,
		((size_t)1 << 21) - 1, ((size_t)1 << 21), ((size_t)1 << 21) + 1,
		((size_t)1 << 28) - 1, ((size_t)1 << 28), ((size_t)1 << 28) + 1,
		((size_t)1 << 29) - 1, ((size_t)1 << 29), ((size_t)1 << 29) + 1,
		((size_t)1 << 35) - 1, ((size_t)1 << 35), ((size_t)1 << 35) + 1,
		((size_t)1 << 42) - 1, ((size_t)1 << 42), ((size_t)1 << 42) + 1,
		((size_t)1 << 49) - 1, ((size_t)1 << 49), ((size_t)1 << 49) + 1,
		((size_t)1 << 56) - 1, ((size_t)1 << 56), ((size_t)1 << 56) + 1,
		((size_t)1 << 57) - 1, ((size_t)1 << 57), ((size_t)1 << 57) + 1,
		(size_t)UINT32_MAX,
		(size_t)UINT64_MAX,
	};
	uint8_t size_buf[FYGT_SIZE_ENCODING_MAX_64];
	unsigned int i, j, k;
	size_t sz, szd;
	uint32_t sz32d;
	uint8_t *szp;

	for (i = 0; i < ARRAY_SIZE(sztable); i++) {
		sz = sztable[i];
		printf("size_t/%zx =", sz);
		j = fy_encode_size_bytes(sz);
		ck_assert(j <= sizeof(size_buf));
		printf(" (%d)", j);

		memset(size_buf, 0, sizeof(size_buf));
		szp = fy_encode_size(size_buf, sizeof(size_buf), sz);
		ck_assert_ptr_ne(szp, NULL);
		ck_assert((unsigned int)(szp - size_buf) == j);
		for (k = 0; k < j; k++)
			printf(" %02x", size_buf[k] & 0xff);

		szd = 0;
		fy_decode_size(size_buf, sizeof(size_buf), &szd);
		printf(" decoded=%zx", szd);

		szd = 0;
		fy_decode_size_nocheck(size_buf, &szd);
		printf(" decoded_nocheck=%zx", szd);

		printf("\n");

		/* decoding must match */
		ck_assert(szd == sz);
	}

	for (i = 0; i < ARRAY_SIZE(sztable); i++) {
		sz = sztable[i];
		if (sz > UINT32_MAX)
			continue;
		printf("uint32_t/%zx =", sz);
		j = fy_encode_size32_bytes((uint32_t)sz);
		ck_assert(j <= sizeof(size_buf));
		printf(" (%d)", j);

		memset(size_buf, 0, sizeof(size_buf));
		szp = fy_encode_size32(size_buf, sizeof(size_buf), (uint32_t)sz);
		ck_assert_ptr_ne(szp, NULL);
		ck_assert((unsigned int)(szp - size_buf) == j);
		for (k = 0; k < j; k++)
			printf(" %02x", size_buf[k] & 0xff);

		sz32d = 0;
		fy_decode_size32(size_buf, sizeof(size_buf), &sz32d);
		printf(" decoded=%zx", (size_t)sz32d);

		printf("\n");

		/* decoding must match */
		ck_assert(sz32d == (uint32_t)sz);
	}
}
END_TEST

/* Test: testing string encoding */
START_TEST(generic_string_range)
{
	static const char *stable[] = {
		"",	/* empty string */
		"0",
		"01",
		"012",
		"0123",
		"01234",
		"012345",
		"0123456",
		"01234567",
		"This is a string",
		"invoice",
		/* a longer than 128 characters string */
		"Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor "
			"incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, "
			"quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo "
			"consequat. Duis aute irure dolor in reprehenderit in voluptate velit "
			"esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat "
			"cupidatat non proident, sunt in culpa qui officia deserunt mollit anim "
			"id est laborum."
	};
	bool pass, test_fail;
	unsigned int i;
	fy_generic v;
	const char *res, *sv;

	/* try normal cast at first */
	test_fail = false;
	for (i = 0; i < ARRAY_SIZE(stable); i++) {
		sv = stable[i];
		v = fy_string(sv);
		res = fy_cast(v, "invalid");

		pass = !strcmp(sv, res);
		test_fail |= !pass;

		printf("cast(v) string/%s = %016lx %s - %s\n",
				sv, v.v, res,
				pass ? "PASS" : "FAIL");
	}

	/* now use castp */
	for (i = 0; i < ARRAY_SIZE(stable); i++) {
		sv = stable[i];
		v = fy_string(sv);
		res = fy_castp(&v, "invalid");

		pass = !strcmp(sv, res);
		test_fail |= !pass;

		printf("cast(&v) string/%s = %016lx %s - %s\n",
				sv, v.v, res,
				pass ? "PASS" : "FAIL");
	}

	ck_assert(!test_fail);
}
END_TEST

/* Test: Automatic generic type promotion */
START_TEST(generic_type_promotion)
{
	fy_generic v;
	fy_generic_sequence_handle seqh;
	fy_generic_mapping_handle maph;
	int i;
	const char *str;
	bool ok;

	/* almost the same as basic test, but with auto type promotion now */

	/* null */
	v = fy_value((void *)NULL);
	ck_assert(fy_generic_is_null_type(v));
	ck_assert(fy_generic_is_in_place(v));

	/* bool */
	v.v = fy_generic_in_place_bool((_Bool)true);
	ck_assert(fy_generic_is_bool_type(v));
	ck_assert(fy_generic_is_in_place(v));
	ck_assert(fy_cast(v, false) == true);
	ck_assert(fy_castp(&v, false) == true);

	v = fy_value((_Bool)false);
	ck_assert(fy_generic_is_bool_type(v));
	ck_assert(fy_generic_is_in_place(v));
	ck_assert(fy_cast(v, true) == false);
	ck_assert(fy_castp(&v, true) == false);

	/* int (in place) */
	v = fy_value(100);
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(fy_generic_is_in_place(v));
	ck_assert(fy_cast(v, -1) == 100);
	ck_assert(fy_castp(&v, -1) == 100);

	/* int (out of place) */
	v = fy_value((long long)(FYGT_INT_INPLACE_MAX+1));
	ck_assert(fy_get_type(v) == FYGT_INT);
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(fy_cast(v, (long long)-1) == FYGT_INT_INPLACE_MAX+1);
	ck_assert(fy_castp(&v, (long long)-1) == FYGT_INT_INPLACE_MAX+1);

	/* float (in place in 64 bit), out of place for 32 bit */
	v = fy_value((float)100.0f);
	ck_assert(fy_generic_is_float_type(v));
#ifdef FYGT_GENERIC_64
	ck_assert(fy_generic_is_in_place(v));
#else
	ck_assert(!fy_generic_is_in_place(v));
#endif
	ck_assert(fy_cast(v, (float)NAN) == 100.0);
	ck_assert(fy_castp(&v, (float)NAN) == 100.0);

	/* double (out of place for both) */
	v = fy_value((double)DBL_MIN);
	ck_assert(fy_generic_is_float_type(v));
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(fy_cast(v, (double)NAN) == DBL_MIN);
	ck_assert(fy_castp(&v, (double)NAN) == DBL_MIN);

	/* string of length 2 (3 with \0) in place always */
	v = fy_value("sh");
	ck_assert(fy_generic_is_string(v));
	ck_assert(fy_generic_is_in_place(v));
	ok = !strcmp(fy_cast(v, ""), "sh");
	ck_assert(ok);
	ok = !strcmp(fy_castp(&v, ""), "sh");
	ck_assert(ok);

	/* string of length 6 (7 with \0) in place for 64 bit */
	v = fy_value("short1");
	ck_assert(fy_generic_is_string(v));
#ifdef FYGT_GENERIC_64
	ck_assert(fy_generic_is_in_place(v));
#else
	ck_assert(!fy_generic_is_in_place(v));
#endif
	ok = !strcmp(fy_cast(v, ""), "short1");
	ck_assert(ok);
	ok = !strcmp(fy_castp(&v, ""), "short1");
	ck_assert(ok);

	/* long string is always out of place */
	v = fy_value("long string out of place");
	ck_assert(fy_generic_is_string(v));
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(!strcmp(fy_cast(v, ""), "long string out of place"));
	ck_assert(!strcmp(fy_castp(&v, ""), "long string out of place"));

	/* sequence */
	v = fy_local_sequence(10, "a string to test");
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(!fy_generic_is_in_place(v));

	/* check that the sequence is correct */
	seqh = fy_cast(v, fy_seq_handle_null);
	ck_assert_ptr_ne(seqh, NULL);
	ck_assert(seqh->count == 2);
	ck_assert(fy_len(seqh) == 2);
	i = fy_cast(seqh->items[0], -1);
	ck_assert(i == 10);
	i = fy_castp(&seqh->items[0], -1);
	ck_assert(i == 10);
	str = fy_cast(seqh->items[1], "");
	ck_assert(!strcmp(str, "a string to test"));
	str = fy_castp(&seqh->items[1], "");
	ck_assert(!strcmp(str, "a string to test"));

	/* mapping */
	v = fy_local_mapping(10, "a string to test");
	ck_assert(fy_generic_is_mapping(v));
	ck_assert(!fy_generic_is_in_place(v));

	/* check that the mapping is correct */
	maph = fy_cast(v, fy_map_handle_null);
	ck_assert_ptr_ne(maph, NULL);
	ck_assert(maph->count == 1);
	ck_assert(fy_len(maph) == 1);
	i = fy_cast(maph->pairs[0].key, -1);
	ck_assert(i == 10);
	i = fy_castp(&maph->pairs[0].key, -1);
	ck_assert(i == 10);
	str = fy_cast(maph->pairs[0].value, "");
	ck_assert(!strcmp(str, "a string to test"));
	str = fy_castp(&maph->pairs[0].value, "");
	ck_assert(!strcmp(str, "a string to test"));
}
END_TEST

/* Test: testing valid, invalid propagation */
START_TEST(generic_invalid_propagation)
{
	fy_generic v, vv;

	/* valid value passes through */
	v = fy_value(100);
	vv = fy_validate(v);
	ck_assert(fy_generic_is_valid(vv));
	ck_assert(v.v == vv.v);

	/* invalid value is invalid */
	v = fy_invalid;
	vv = fy_validate(v);
	ck_assert(fy_generic_is_invalid(vv));

	/* a sequence with all valid items is valid */
	v = fy_local_sequence(true, false, "string", 100000);
	ck_assert(fy_generic_is_sequence(v));
	vv = fy_validate(v);
	ck_assert(fy_generic_is_valid(vv));
	ck_assert(v.v == vv.v);

	/* a sequence with an invalid item is invalid */
	v = fy_local_sequence(true, false, fy_invalid, 100000);
	ck_assert(fy_generic_is_sequence(v));
	vv = fy_validate(v);
	ck_assert(fy_generic_is_invalid(vv));

	/* a mapping with all valid items is valid */
	v = fy_local_mapping("foo", false, "bar", true);
	ck_assert(fy_generic_is_mapping(v));
	vv = fy_validate(v);
	ck_assert(fy_generic_is_valid(vv));
	ck_assert(v.v == vv.v);

	/* a mapping with an invalid key is invalid */
	v = fy_local_mapping("foo", false, fy_invalid, true);
	ck_assert(fy_generic_is_mapping(v));
	vv = fy_validate(v);
	ck_assert(fy_generic_is_invalid(vv));

	/* a mapping with an invalid value is invalid */
	v = fy_local_mapping("foo", false, "bar", fy_invalid);
	ck_assert(fy_generic_is_mapping(v));
	vv = fy_validate(v);
	ck_assert(fy_generic_is_invalid(vv));

	/* a sequence with all deep items valid is valid */
	v = fy_local_sequence(true, false,
			fy_local_mapping("foo", "bar"),
			100000);
	ck_assert(fy_generic_is_sequence(v));
	vv = fy_validate(v);
	ck_assert(fy_generic_is_valid(vv));
	ck_assert(v.v == vv.v);

	/* a sequence with a deep invalid is invalid */
	v = fy_local_sequence(true, false,
			fy_local_mapping("foo", fy_invalid),
			100000);
	ck_assert(fy_generic_is_sequence(v));
	vv = fy_validate(v);
	ck_assert(fy_generic_is_invalid(vv));
}
END_TEST

/* Test: sized string */
START_TEST(generic_sized_string)
{
	const char *str0_short = "H\0A";
	const size_t str0_short_sz = 3;
	const char *str0_long = "Hello\0There\0Long\0String";
	const size_t str0_long_sz = 23;
	const fy_generic_sized_string szstr0_short = { .data = str0_short, .size = str0_short_sz };
	const fy_generic_sized_string szstr0_long = { .data = str0_long, .size = str0_long_sz };
	fy_generic_sized_string szstr;
	fy_generic v;

	/* test short sized string */
	v = fy_value(szstr0_short);
	ck_assert(fy_generic_is_string(v));
	szstr = fy_cast(v, fy_szstr_empty);

	/* check it roundtripped */
	ck_assert(szstr.size == szstr0_short.size);
	ck_assert(!memcmp(szstr.data, szstr0_short.data, szstr0_short.size));

	/* test long sized string */
	v = fy_value(szstr0_long);
	ck_assert(fy_generic_is_string(v));
	szstr = fy_cast(v, fy_szstr_empty);

	/* check it roundtripped */
	ck_assert(szstr.size == szstr0_long.size);
	ck_assert(!memcmp(szstr.data, szstr0_long.data, szstr0_long.size));
}
END_TEST

/* Test: decorated int (full range int) */
START_TEST(generic_decorated_int)
{
	fy_generic_decorated_int dint;
	unsigned long long ullv;
	fy_generic v;

	/* first test in place */
	v = fy_value(1LLU);
	ck_assert(fy_generic_is_int_type(v));
	ullv = fy_cast(v, 0LLU);
	ck_assert(ullv == 1LLU);
	dint = fy_cast(v, fy_dint_empty);
	ck_assert(dint.uv == 1LLU);

	/* test out of place, but still in signed range */
	v = fy_value((unsigned long long)LLONG_MAX);
	ck_assert(fy_generic_is_int_type(v));
	ullv = fy_cast(v, 0LLU);
	ck_assert(ullv == (unsigned long long)LLONG_MAX);
	dint = fy_cast(v, fy_dint_empty);
	ck_assert(dint.uv == (unsigned long long)LLONG_MAX);

	/* test out of place, but now in unsigned range */
	v = fy_value((unsigned long long)LLONG_MAX + 1);
	ck_assert(fy_generic_is_int_type(v));
	ullv = fy_cast(v, 0LLU);
	ck_assert(ullv == (unsigned long long)LLONG_MAX + 1);
	dint = fy_cast(v, fy_dint_empty);
	ck_assert(dint.uv == (unsigned long long)LLONG_MAX + 1);
	ck_assert(dint.is_unsigned);	/* must be marked as unsigned */

	/* test maximum */
	v = fy_value(ULLONG_MAX);
	ck_assert(fy_generic_is_int_type(v));
	ullv = fy_cast(v, 0LLU);
	ck_assert(ullv == ULLONG_MAX);
	dint = fy_cast(v, fy_dint_empty);
	ck_assert(dint.uv == ULLONG_MAX);
	ck_assert(dint.is_unsigned);	/* must be marked as unsigned */

}
END_TEST

/* Test: casting checks */
START_TEST(generic_casts)
{
	fy_generic v;
	fy_generic_sequence_handle seqh;
	fy_generic_mapping_handle maph;

	/* first test casts that should succeed */

	/* null */
	v = fy_value((void *)NULL);
	ck_assert(fy_cast(v, (void *)NULL) == (void *)NULL);

	/* bool */
	v = fy_value((_Bool)true);
	ck_assert(fy_cast(v, (_Bool)false) == true);
	v = fy_value((_Bool)false);
	ck_assert(fy_cast(v, (_Bool)true) == false);

	/* char */
	v = fy_value('a');
	ck_assert(fy_cast(v, '\0') == 'a');
	v = fy_value(CHAR_MIN);
	ck_assert(fy_cast(v, '1') == CHAR_MIN);
	v = fy_value(CHAR_MAX);
	ck_assert(fy_cast(v, '1') == CHAR_MAX);

	v = fy_value((signed char)0x61);
	ck_assert(fy_cast(v, (signed char)0x00) == 0x61);
	v = fy_value(SCHAR_MIN);
	ck_assert(fy_cast(v, '1') == SCHAR_MIN);
	v = fy_value(SCHAR_MAX);
	ck_assert(fy_cast(v, '1') == SCHAR_MAX);

	v = fy_value((unsigned char)0xf1);
	ck_assert(fy_cast(v, (unsigned char)0x00) == 0xf1);
	v = fy_value(UCHAR_MAX);
	ck_assert(fy_cast(v, '1') == UCHAR_MAX);

	/* short */
	v = fy_value(SHRT_MIN);
	ck_assert(fy_cast(v, (short)0) == SHRT_MIN);
	v = fy_value(SHRT_MAX);
	ck_assert(fy_cast(v, (short)0) == SHRT_MAX);
	v = fy_value(USHRT_MAX);
	ck_assert(fy_cast(v, (unsigned short)0) == USHRT_MAX);

	/* int */
	v = fy_value(INT_MIN);
	ck_assert(fy_cast(v, (int)0) == INT_MIN);
	v = fy_value(INT_MAX);
	ck_assert(fy_cast(v, (int)0) == INT_MAX);
	v = fy_value(UINT_MAX);
	ck_assert(fy_cast(v, (unsigned int)0) == UINT_MAX);

	/* long */
	v = fy_value(LONG_MIN);
	ck_assert(fy_cast(v, (long)0) == LONG_MIN);
	v = fy_value(LONG_MAX);
	ck_assert(fy_cast(v, (long)0) == LONG_MAX);
	v = fy_value(ULONG_MAX);
	ck_assert(fy_cast(v, (unsigned long)0) == ULONG_MAX);

	/* long long */
	v = fy_value(LLONG_MIN);
	ck_assert(fy_cast(v, (long)0) == LLONG_MIN);
	v = fy_value(LLONG_MAX);
	ck_assert(fy_cast(v, (long)0) == LLONG_MAX);
	v = fy_value(ULLONG_MAX);
	ck_assert(fy_cast(v, (unsigned long)0) == ULLONG_MAX);

	/* float */
	v = fy_value((float)FLT_MIN);
	ck_assert(fy_cast(v, (float)NAN) == (float)FLT_MIN);
	v = fy_value((float)FLT_MAX);
	ck_assert(fy_cast(v, (float)NAN) == (float)FLT_MAX);
	v = fy_value((float)-FLT_MIN);
	ck_assert(fy_cast(v, (float)NAN) == (float)-FLT_MIN);
	v = fy_value((float)-FLT_MAX);
	ck_assert(fy_cast(v, (float)NAN) == (float)-FLT_MAX);

	/* double */
	v = fy_value((double)DBL_MIN);
	ck_assert(fy_cast(v, (double)NAN) == (double)DBL_MIN);
	v = fy_value((double)DBL_MAX);
	ck_assert(fy_cast(v, (double)NAN) == (double)DBL_MAX);
	v = fy_value((double)-DBL_MIN);
	ck_assert(fy_cast(v, (double)NAN) == (double)-DBL_MIN);
	v = fy_value((double)-DBL_MAX);
	ck_assert(fy_cast(v, (double)NAN) == (double)-DBL_MAX);

	/* string */
	v = fy_value("This is a string");
	ck_assert(!strcmp(fy_cast(v, ""), "This is a string"));

	/* sequence */
	v = fy_local_sequence(1, 2, 3);
	seqh = fy_cast(v, fy_seq_handle_null);
	ck_assert(seqh != fy_seq_handle_null);

	/* mapping */
	v = fy_local_mapping("foo", "bar",
			     "baz", true);
	maph = fy_cast(v, fy_map_handle_null);
	ck_assert(maph != fy_map_handle_null);

	/* now test the invalid type casts */
	v = fy_value(true);
	ck_assert(fy_cast(v, 0) == 0);
	ck_assert(!strcmp(fy_cast(v, ""), ""));
	ck_assert(fy_cast(v, 0.0f) == 0.0f);
	ck_assert(fy_cast(v, fy_seq_handle_null) == fy_seq_handle_null);

	/* onwards to the range casts */
	v = fy_value(CHAR_MIN - 1);
	ck_assert(fy_cast(v, (char)'0') == '0');
	v = fy_value(CHAR_MAX + 1);
	ck_assert(fy_cast(v, (char)'0') == '0');
	v = fy_value(SCHAR_MIN - 1);
	ck_assert(fy_cast(v, (signed char)0) == 0);
	v = fy_value(SCHAR_MAX + 1);
	ck_assert(fy_cast(v, (signed char)0) == 0);
	v = fy_value(-1);
	ck_assert(fy_cast(v, (unsigned char)0) == 0);
	v = fy_value(UCHAR_MAX + 1);
	ck_assert(fy_cast(v, (unsigned char)0) == 0);

	v = fy_value(SHRT_MIN - 1);
	ck_assert(fy_cast(v, (signed short)0) == 0);
	v = fy_value(SHRT_MAX + 1);
	ck_assert(fy_cast(v, (signed short)0) == 0);
	v = fy_value(-1);
	ck_assert(fy_cast(v, (unsigned short)0) == 0);
	v = fy_value(USHRT_MAX + 1);
	ck_assert(fy_cast(v, (unsigned short)0) == 0);

	v = fy_value((signed long long)INT_MIN - 1);
	ck_assert(fy_cast(v, (signed int)0) == 0);
	v = fy_value((signed long long)INT_MAX + 1);
	ck_assert(fy_cast(v, (signed int)0) == 0);
	v = fy_value(-1);
	ck_assert(fy_cast(v, (unsigned int)0) == 0);
	v = fy_value((signed long long)UINT_MAX + 1);
	ck_assert(fy_cast(v, (unsigned int)0) == 0);

	/* LONG and LLONG are at the range limit, so don't try to be smart */
}
END_TEST

/* Test: get api */
START_TEST(generic_get)
{
	fy_generic seq, map;
	fy_generic_sequence_handle seqh;
	fy_generic_mapping_handle maph;
	int iv;
	bool bv;
	const char *strv;

	/* sequence */
	seq = fy_local_sequence(-100, true, "sh", "long string");
	ck_assert(fy_generic_is_sequence(seq));

	/* manual access through seq generic value */
	iv = fy_get(seq, 0, -1);
	ck_assert(iv == -100);
	bv = fy_get(seq, 1, false);
	ck_assert(bv == true);
	strv = fy_get(seq, 2, "");
	ck_assert(!strcmp(strv, "sh"));
	strv = fy_get(seq, 3, "");
	ck_assert(!strcmp(strv, "long string"));

	/* manual access through the seq handle (somewhat faster) */
	seqh = fy_cast(seq, fy_seq_handle_null);
	ck_assert(seqh != NULL);

	iv = fy_get(seqh, 0, -1);
	ck_assert(iv == -100);
	bv = fy_get(seqh, 1, false);
	ck_assert(bv == true);
	strv = fy_get(seqh, 2, "");
	ck_assert(!strcmp(strv, "sh"));
	strv = fy_get(seqh, 3, "");
	ck_assert(!strcmp(strv, "long string"));

	/* try to access something that does not exist */
	iv = fy_get(seq, -1, -1);
	ck_assert(iv == -1);
	iv = fy_get(seq, 1000, -1);
	ck_assert(iv == -1);

	/* mapping */
	map = fy_local_mapping("foo", 100, "bar", 200);
	ck_assert(fy_generic_is_mapping(map));

	/* manual access through map generic value */
	iv = fy_get(map, "foo", -1);
	ck_assert(iv == 100);
	iv = fy_get(map, "bar", -1);
	ck_assert(iv == 200);

	/* manual access through the map handle (somewhat faster) */
	maph = fy_cast(map, fy_map_handle_null);
	ck_assert(maph != NULL);
	iv = fy_get(maph, "foo", -1);
	ck_assert(iv == 100);
	iv = fy_get(maph, "bar", -1);
	ck_assert(iv == 200);

	/* try to access something that does not exist */
	iv = fy_get(maph, "dummy", -1);
	ck_assert(iv == -1);
}
END_TEST

/* Test: get_at */
START_TEST(generic_get_at)
{
	fy_generic seq, map, v;
	fy_generic_sequence_handle seqh;
	fy_generic_map_pair mp;
	const fy_generic_map_pair *mpp;
	const char *key;
	size_t i, len;
	int val, sum;

	seq = fy_sequence(10, 100, 1000);
	ck_assert(fy_generic_is_sequence(seq));
	len = fy_len(seq);
	ck_assert(len == 3);
	sum = 0;
	for (i = 0; i < len; i++) {
		val = fy_get_at(seq, i, -1);
		ck_assert(val != -1);
		sum += val;
		printf("%s: [%zu]=%d\n", __func__, i, val);
	}

	printf("%s: sum=%d\n", __func__, sum);
	ck_assert(sum == 1110);

	seqh = fy_cast(seq, fy_seq_handle_null);
	ck_assert(seqh != NULL);
	ck_assert(fy_generic_is_sequence(seq));
	len = fy_len(seqh);
	ck_assert(len == 3);
	sum = 0;
	for (i = 0; i < len; i++) {
		val = fy_get_at(seqh, i, -1);
		ck_assert(val != -1);
		sum += val;
		printf("%s: [%zu]=%d\n", __func__, i, val);
	}

	printf("%s: sum=%d\n", __func__, sum);
	ck_assert(sum == 1110);

	seqh = fy_cast(fy_sequence(800, 900, 2000, 1), fy_seq_handle_null);
	ck_assert(seqh);
	len = fy_len(seqh);
	ck_assert(len == 4);
	sum = 0;
	for (i = 0; i < len; i++) {
		v = fy_get_at(seqh, i, fy_invalid);
		ck_assert(v.v != fy_invalid_value);
		val = fy_cast(v, -1);
		ck_assert(val != -1);
		sum += val;
		printf("%s: [%zu]=%d\n", __func__, i, val);
	}
	printf("%s: sum=%d\n", __func__, sum);
	ck_assert(sum == 3701);

	map = fy_mapping("foo", 10, "bar", 20);
	ck_assert(fy_generic_is_mapping(map));
	len = fy_len(map);
	ck_assert(len == 2);
	sum = 0;
	for (i = 0; i < len; i++) {
		val = fy_get_at(map, i, -1);
		ck_assert(val != -1);
		sum += val;
		printf("%s: [%zu]=%d\n", __func__, i, val);
	}
	printf("%s: sum=%d\n", __func__, sum);
	ck_assert(sum == 30);

	mp = fy_get_at(map, 0, fy_map_pair_invalid);
	ck_assert(fy_generic_is_valid(mp.key));
	ck_assert(fy_generic_is_valid(mp.value));
	key = fy_cast(mp.key, "");
	val = fy_cast(mp.value, -1);
	printf("%s: [0] mp: key=%s value=%d\n", __func__, key, val);
	ck_assert(!strcmp(key, "foo"));
	ck_assert(val == 10);

	mpp = fy_get_at(map, 1, ((fy_generic_map_pair *)NULL));
	ck_assert(mpp);
	ck_assert(fy_generic_is_valid(mpp->key));
	ck_assert(fy_generic_is_valid(mpp->value));
	key = fy_cast(mpp->key, "");
	val = fy_cast(mpp->value, -1);
	printf("%s: [1] *mp: key=%s value=%d\n", __func__, key, val);
	ck_assert(!strcmp(key, "bar"));
	ck_assert(val == 20);
}
END_TEST

/* Test: comparisons */
START_TEST(generic_compare)
{
	fy_generic v1, v2;
	fy_generic_sequence_handle seqh1, seqh2;
	fy_generic_mapping_handle maph1, maph2;
	int ret;

	/* nulls match always */
	v1 = fy_value((void *)NULL);
	v2 = fy_value((void *)NULL);
	ck_assert(fy_compare(v1, v2) == 0);
	ck_assert(fy_compare(v1, (void *)NULL) == 0);
	ck_assert(fy_compare((void *)NULL, (void *)NULL) == 0);

	/* false == false */
	v1 = fy_value((_Bool)false);
	v2 = fy_value((_Bool)false);
	ck_assert(fy_compare(v1, v2) == 0);
	ck_assert(fy_compare(v1, (_Bool)false) == 0);
	ck_assert(fy_compare((_Bool)false, (_Bool)false) == 0);
	/* true == true */
	v1 = fy_value((_Bool)true);
	v2 = fy_value((_Bool)true);
	ck_assert(fy_compare(v1, v2) == 0);
	ck_assert(fy_compare(v1, (_Bool)true) == 0);
	ck_assert(fy_compare((_Bool)true, (_Bool)true) == 0);
	/* false < true */
	v1 = fy_value((_Bool)false);
	v2 = fy_value((_Bool)true);
	ck_assert(fy_compare(v1, v2) < 0);
	ck_assert(fy_compare(v1, (_Bool)true) < 0);
	ck_assert(fy_compare((_Bool)false, (_Bool)true) < 0);
	/* true > false */
	v1 = fy_value((_Bool)true);
	v2 = fy_value((_Bool)false);
	ck_assert(fy_compare(v1, v2) > 0);
	ck_assert(fy_compare(v1, (_Bool)false) > 0);
	ck_assert(fy_compare((_Bool)true, (_Bool)false) > 0);

	/* 0 == 0 */
	v1 = fy_value(0);
	v2 = fy_value(0);
	ck_assert(fy_compare(v1, v2) == 0);
	ck_assert(fy_compare(v1, 0) == 0);
	ck_assert(fy_compare(0, 0) == 0);
	/* 100 > -10 */
	v1 = fy_value(100);
	v2 = fy_value(-10);
	ck_assert(fy_compare(v1, v2) > 0);
	ck_assert(fy_compare(v1, -10) > 0);
	ck_assert(fy_compare(100, -10) > 0);
	/* 100 < 999 */
	v1 = fy_value(100);
	v2 = fy_value(999);
	ck_assert(fy_compare(v1, v2) < 0);
	ck_assert(fy_compare(v1, 999) < 0);
	ck_assert(fy_compare(100, 990) < 0);
	/* unsigned LLONG_MAX + 1 > LLONG_MAX */
	v1 = fy_value((unsigned long long)LLONG_MAX + 1);
	v2 = fy_value((long long)LLONG_MAX);
	ck_assert(fy_compare(v1, v2) > 0);
	ck_assert(fy_compare(v1, (long long)LONG_MAX) > 0);
	ck_assert(fy_compare((unsigned long long)LLONG_MAX + 1, (long long)LLONG_MAX) > 0);

	/* abc == abc */
	v1 = fy_value("abc");
	v2 = fy_value("abc");
	ck_assert(fy_compare(v1, v2) == 0);
	ck_assert(fy_compare(v1, "abc") == 0);
	ck_assert(fy_compare("abc", "abc") == 0);
	/* abc < zxc */
	v1 = fy_value("abc");
	v2 = fy_value("zxc");
	ck_assert(fy_compare(v1, v2) < 0);
	ck_assert(fy_compare(v1, "zxc") < 0);
	ck_assert(fy_compare("abc", "zxc") < 0);
	/* zxc > abc */
	v1 = fy_value("zxc");
	v2 = fy_value("abc");
	ck_assert(fy_compare(v1, v2) > 0);
	ck_assert(fy_compare(v1, "abc") > 0);
	ck_assert(fy_compare("zxc", "abc") > 0);

	/* zxc000 > zxc */
	v1 = fy_value("zxc000");
	v2 = fy_value("zxc");
	ck_assert(fy_compare(v1, v2) > 0);
	ck_assert(fy_compare(v1, "zxc") > 0);
	ck_assert(fy_compare("zxc000", "zxc") > 0);

	/* "" == "" */
	v1 = fy_value("");
	v2 = fy_value("");
	ck_assert(fy_compare(v1, v2) == 0);
	ck_assert(fy_compare(v1, "") == 0);
	ck_assert(fy_compare("", "") == 0);

	/* "a" > "" */
	v1 = fy_value("a");
	v2 = fy_value("");
	ck_assert(fy_compare(v1, v2) > 0);
	ck_assert(fy_compare(v1, "") > 0);
	ck_assert(fy_compare("a", "") > 0);

	/* sequence equality */
	v1 = fy_local_sequence(1, 2, 3);
	v2 = fy_local_sequence(1, 2, 3);
	ret = fy_compare(v1, v2);
	ck_assert(ret == 0);
	seqh1 = fy_cast(v1, fy_seq_handle_null);
	seqh2 = fy_cast(v2, fy_seq_handle_null);
	ret = fy_compare(seqh1, v2);
	ck_assert(ret == 0);
	ret = fy_compare(v1, seqh2);
	ck_assert(ret == 0);
	ret = fy_compare(seqh1, seqh2);
	ck_assert(ret == 0);

	/* sequence > */
	v1 = fy_local_sequence(1, 8, 3);
	v2 = fy_local_sequence(1, 2, 10);
	ret = fy_compare(v1, v2);
	ck_assert(ret > 0);
	ret = fy_compare(v1, fy_cast(fy_local_sequence(1, 2, 10), fy_seq_handle_null));
	ck_assert(ret > 0);
	seqh1 = fy_cast(v1, fy_seq_handle_null);
	seqh2 = fy_cast(v2, fy_seq_handle_null);
	ret = fy_compare(seqh1, v2);
	ck_assert(ret > 0);
	ret = fy_compare(v1, seqh2);
	ck_assert(ret > 0);
	ret = fy_compare(seqh1, seqh2);
	ck_assert(ret > 0);

	/* mapping equality (easy) */
	v1 = fy_local_mapping("foo", 10, "bar", 100);
	v2 = fy_local_mapping("foo", 10, "bar", 100);
	ret = fy_compare(v1, v2);
	ck_assert(ret == 0);
	maph1 = fy_cast(v1, fy_map_handle_null);
	maph2 = fy_cast(v2, fy_map_handle_null);
	ret = fy_compare(maph1, v2);
	ck_assert(ret == 0);
	ret = fy_compare(v1, maph2);
	ck_assert(ret == 0);
	ret = fy_compare(maph1, maph2);
	ck_assert(ret == 0);

	/* mapping equality (reorders) */
	v1 = fy_local_mapping("foo", 10, "bar", 100);
	v2 = fy_local_mapping("bar", 100, "foo", 10);
	ret = fy_compare(v1, v2);
	ck_assert(ret == 0);
	maph1 = fy_cast(v1, fy_map_handle_null);
	maph2 = fy_cast(v2, fy_map_handle_null);
	ret = fy_compare(maph1, v2);
	ck_assert(ret == 0);
	ret = fy_compare(v1, maph2);
	ck_assert(ret == 0);
	ret = fy_compare(maph1, maph2);
	ck_assert(ret == 0);

	/* mapping inequality (reorders) */
	v1 = fy_local_mapping("foo", 10, "bar", 101);
	v2 = fy_local_mapping("bar", 100, "foo", 10);
	ret = fy_compare(v1, v2);
	ck_assert(ret > 0);
	maph1 = fy_cast(v1, fy_map_handle_null);
	maph2 = fy_cast(v2, fy_map_handle_null);
	ret = fy_compare(maph1, v2);
	ck_assert(ret > 0);
	ret = fy_compare(v1, maph2);
	ck_assert(ret > 0);
	ret = fy_compare(maph1, maph2);
	ck_assert(ret > 0);

	/* NOTE: GCC miscompiles the complicated compare cast sequences */
	/* file a bug report? */
}
END_TEST

/* Test: Basic builder operation */
START_TEST(gb_basics)
{
	char buf[8192];
	struct fy_generic_builder *gb;
	fy_generic v, seq;
	unsigned int i, len;

	gb = fy_generic_builder_create_in_place(FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER, NULL,
			buf, sizeof(buf));
	ck_assert(gb != NULL);

	/* verify that creating inplace still makes it inplace */
	/* int (in place) */
	v = fy_to_generic(gb, 100);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(fy_generic_is_in_place(v));

	/* int (out of place) */
	v = fy_to_generic(gb, FYGT_INT_INPLACE_MAX+1);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_get_type(v) == FYGT_INT);
	ck_assert(!fy_generic_is_in_place(v));

	/* verify that the builder contains this out of place v */
	ck_assert(fy_generic_builder_contains(gb, v));

	/* same but using polymorphic fy_value() */
	v = fy_value(gb, 100);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(fy_generic_is_in_place(v));

	/* int (out of place) */
	v = fy_value(gb, FYGT_INT_INPLACE_MAX+1);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_get_type(v) == FYGT_INT);
	ck_assert(!fy_generic_is_in_place(v));

	/* verify that the builder contains this out of place v */
	ck_assert(fy_generic_builder_contains(gb, v));

	// long string
	v = fy_value(gb, "long string out of place");
	ck_assert(fy_generic_is_string(v));
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(!strcmp(fy_cast(v, ""), "long string out of place"));
	ck_assert(!strcmp(fy_castp(&v, ""), "long string out of place"));

	/* verify that the builder contains this out of place v */
	ck_assert(fy_generic_builder_contains(gb, v));

	/* a sequence is always out of place */
	v = fy_gb_sequence(gb, 1, 10, 100);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(fy_len(v) == 3);

	/* verify that the builder contains this out of place v */
	ck_assert(fy_generic_builder_contains(gb, v));

	/* same with mapping */
	v = fy_gb_mapping(gb, "foo", 100, "bar", 200);
	ck_assert(fy_generic_is_mapping(v));
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(fy_len(v) == 2);

	/* verify that the builder contains this out of place v */
	ck_assert(fy_generic_builder_contains(gb, v));

	/* now verify that everyhing in a collection is contained in the builder */
	seq = fy_gb_sequence(gb, 1, 10, 100, "Long string that should belong in the builder");

	len = fy_len(seq);
	for (i = 0; i < len; i++) {
		v = fy_get(seq, i, fy_invalid);
		ck_assert(fy_generic_is_valid(v));
		ck_assert(fy_generic_builder_contains(gb, v));
	}
}
END_TEST

/* Test: testing whether is_unsigned for outofplace ints is proper */
START_TEST(generic_is_unsigned)
{
	char buf[8192];
	struct fy_generic_builder *gb;
	const struct fy_generic_decorated_int *p;
	fy_generic v;
	static const unsigned long long tab[] = {
		FYGT_INT_INPLACE_MAX+1,
		(unsigned long long)LLONG_MAX,
		(unsigned long long)LLONG_MAX + 1,
		ULLONG_MAX
	};
	size_t i;
	unsigned long long uv;
	bool is_unsigned;

	/* first try constant regular signed in place */
	v = fy_value(100);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(fy_generic_is_in_place(v));

	/* now this is out of place */
	v = fy_value((long long)FYGT_INT_INPLACE_MAX+1);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(!fy_generic_is_in_place(v));

	/* manually resolve and check */
	p = fy_generic_resolve_ptr(v);
	ck_assert(p->sv == (long long)FYGT_INT_INPLACE_MAX+1);
	ck_assert(p->is_unsigned == false);

	/* again for LLONG_MAX (should still be signed */
	v = fy_value((long long)LLONG_MAX);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(!fy_generic_is_in_place(v));

	/* manually resolve and check */
	p = fy_generic_resolve_ptr(v);
	ck_assert(p->sv == LLONG_MAX);
	ck_assert(p->is_unsigned == false);

	/* now try LLONG_MAX+1 but unsigned */
	v = fy_value((unsigned long long)LLONG_MAX + 1);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(!fy_generic_is_in_place(v));

	/* manually resolve and check */
	p = fy_generic_resolve_ptr(v);
	ck_assert(p->uv == (unsigned long long)LLONG_MAX + 1);
	ck_assert(p->is_unsigned == true);

	/* finally try ULLONG_MAX */
	v = fy_value(ULLONG_MAX);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(!fy_generic_is_in_place(v));

	/* manually resolve and check */
	p = fy_generic_resolve_ptr(v);
	ck_assert(p->uv == ULLONG_MAX);
	ck_assert(p->is_unsigned == true);

	/* again but using a variable */
	for (i = 0; i < ARRAY_SIZE(tab); i++) {
		uv = tab[i];
		is_unsigned = uv > (unsigned long long)LLONG_MAX;

		printf("uv=0x%llx is_unsigned=%s\n", uv, is_unsigned ? "true" : "false");

		v = fy_value(uv);
		ck_assert(fy_generic_is_valid(v));
		ck_assert(fy_generic_is_int_type(v));
		ck_assert(!fy_generic_is_in_place(v));

		/* manually resolve and check */
		p = fy_generic_resolve_ptr(v);
		ck_assert(p->uv == uv);
		ck_assert(p->is_unsigned == is_unsigned);
	}

	/* the same, but with a builder... */
	gb = fy_generic_builder_create_in_place(FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER, NULL,
			buf, sizeof(buf));
	ck_assert(gb != NULL);

	/* first try constant regular signed in place */
	v = fy_value(gb, 100);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(fy_generic_is_in_place(v));

	/* now this is out of place */
	v = fy_value(gb, (long long)FYGT_INT_INPLACE_MAX+1);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(fy_generic_builder_contains(gb, v));

	/* manually resolve and check */
	p = fy_generic_resolve_ptr(v);
	ck_assert(p->sv == (long long)FYGT_INT_INPLACE_MAX+1);
	ck_assert(p->is_unsigned == false);

	/* again for LLONG_MAX (should still be signed */
	v = fy_value(gb, (long long)LLONG_MAX);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(fy_generic_builder_contains(gb, v));

	/* manually resolve and check */
	p = fy_generic_resolve_ptr(v);
	ck_assert(p->sv == LLONG_MAX);
	ck_assert(p->is_unsigned == false);

	/* now try LLONG_MAX+1 but unsigned */
	v = fy_value(gb, (unsigned long long)LLONG_MAX + 1);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(fy_generic_builder_contains(gb, v));

	/* manually resolve and check */
	p = fy_generic_resolve_ptr(v);
	ck_assert(p->uv == (unsigned long long)LLONG_MAX + 1);
	ck_assert(p->is_unsigned == true);

	/* finally try ULLONG_MAX */
	v = fy_value(gb, ULLONG_MAX);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(fy_generic_builder_contains(gb, v));

	/* manually resolve and check */
	p = fy_generic_resolve_ptr(v);
	ck_assert(p->uv == ULLONG_MAX);
	ck_assert(p->is_unsigned == true);

	/* again but using a variable */
	for (i = 0; i < ARRAY_SIZE(tab); i++) {
		uv = tab[i];
		is_unsigned = uv > (unsigned long long)LLONG_MAX;

		printf("uv=0x%llx is_unsigned=%s\n", uv, is_unsigned ? "true" : "false");

		v = fy_value(gb, uv);
		ck_assert(fy_generic_is_valid(v));
		ck_assert(fy_generic_is_int_type(v));
		ck_assert(!fy_generic_is_in_place(v));
		ck_assert(fy_generic_builder_contains(gb, v));

		/* manually resolve and check */
		p = fy_generic_resolve_ptr(v);
		ck_assert(p->uv == uv);
		ck_assert(p->is_unsigned == is_unsigned);
	}
}
END_TEST

/* Test: Basic dedup builder operation */
START_TEST(gb_dedup_basics)
{
	char buf[8192];
	struct fy_generic_builder *gb;
	fy_generic v, seq, map;
	fy_generic v_int_out_of_place, v_str_out_of_place, vseq1, vseq2, vmap1;
	unsigned int i, len;

	(void)vseq1;
	(void)vseq2;
	(void)vmap1;

	gb = fy_generic_builder_create_in_place(
			FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED, NULL,
			buf, sizeof(buf));
	ck_assert(gb != NULL);

	/* verify that creating inplace still makes it inplace */
	/* int (in place) */
	v = fy_to_generic(gb, 100);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(fy_generic_is_in_place(v));

	v = fy_to_generic(gb, FYGT_INT_INPLACE_MAX+1);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_get_type(v) == FYGT_INT);
	ck_assert(!fy_generic_is_in_place(v));

	/* save this */
	v_int_out_of_place = v;

	/* verify that the builder contains this out of place v */
	ck_assert(fy_generic_builder_contains(gb, v));

	/* same but using polymorphic fy_value() */
	v = fy_value(gb, 100);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_int_type(v));
	ck_assert(fy_generic_is_in_place(v));

	/* int (out of place) */
	v = fy_value(gb, (long long)FYGT_INT_INPLACE_MAX+1);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_get_type(v) == FYGT_INT);
	ck_assert(!fy_generic_is_in_place(v));

	/* verify that the builder contains this out of place v */
	ck_assert(fy_generic_builder_contains(gb, v));

	/* verify that the value is the same one as previously */
	ck_assert(v.v == v_int_out_of_place.v);

	// printf("0x%016lx 0x%016lx\n", v.v, v_int_out_of_place.v);
	ck_assert(v.v == v_int_out_of_place.v);

	/* long string */
	v = fy_value(gb, "long string out of place");
	ck_assert(fy_generic_is_string(v));
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(!strcmp(fy_cast(v, ""), "long string out of place"));
	ck_assert(!strcmp(fy_castp(&v, ""), "long string out of place"));

	/* verify that the builder contains this out of place v */
	ck_assert(fy_generic_builder_contains(gb, v));

	/* save this */
	v_str_out_of_place = v;

	/* a sequence is always out of place */
	v = fy_gb_sequence(gb, 1, 10, 100);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(fy_len(v) == 3);

	/* verify that the builder contains this out of place v */
	ck_assert(fy_generic_builder_contains(gb, v));

	/* save it for later */
	vseq1 = v;

	/* same with mapping */
	v = fy_gb_mapping(gb, "foo", 100, "bar", 200);
	ck_assert(fy_generic_is_mapping(v));
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(fy_len(v) == 2);

	/* save it for later */
	vmap1 = v;

	/* verify that the builder contains this out of place v */
	ck_assert(fy_generic_builder_contains(gb, v));

	/* now verify that everyhing in a collection is contained in the builder */
	seq = fy_gb_sequence(gb, 1, 10, 100, "Long string that should belong in the builder", "long string out of place");

	len = fy_len(seq);
	for (i = 0; i < len; i++) {
		v = fy_get(seq, i, fy_invalid);
		ck_assert(fy_generic_is_valid(v));
		ck_assert(fy_generic_builder_contains(gb, v));
	}

	/* get the 4th item, it should be the one that was stored earlier */
	v = fy_get(seq, 4, fy_invalid);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(v_str_out_of_place.v == v.v);

	/* verify that an internalized local sequence is deduped */
	seq = fy_local_sequence(1, 10, 100);
	v = fy_gb_internalize(gb, seq);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_sequence(v));

	/* it must be the exact same value */
	ck_assert(vseq1.v == v.v);

	/* verify that an internalized local mapping is deduped */
	map = fy_local_mapping("foo", 100, "bar", 200);
	v = fy_gb_internalize(gb, map);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(fy_generic_is_mapping(v));

	/* it must be the exact same value */
	ck_assert(vmap1.v == v.v);

	/* verify that values are stored once */
	v = fy_value(gb, FYGT_INT_INPLACE_MAX+1);
	ck_assert(fy_generic_is_valid(v));
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(fy_get_type(v) == FYGT_INT);

	ck_assert(v.v == v_int_out_of_place.v);

	v = fy_gb_to_generic(gb, "long string out of place");
	ck_assert(fy_generic_is_valid(v));
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(fy_get_type(v) == FYGT_STRING);

	ck_assert(v.v == v_str_out_of_place.v);
}
END_TEST

static fy_generic calculate_seq_sum(enum fy_gb_cfg_flags flags, struct fy_generic_builder *parent_gb,
				    fy_generic seq)
{
	char buf[8192];
	struct fy_generic_builder *gb;
	fy_generic v;
	size_t i, len;
	long long val, sum;

	gb = fy_generic_builder_create_in_place(FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | flags, parent_gb,
			buf, sizeof(buf));
	ck_assert(gb != NULL);

	len = fy_len(seq);
	sum = 0;
	for (i = 0; i < len; i++) {
		val = fy_get(seq, i, (long long)-1);
		ck_assert(val != -1);
		sum += val;
	}

	/* we know that the value must be out of place */
	v = fy_value(gb, sum);
	ck_assert(!fy_generic_is_in_place(v));
	ck_assert(fy_generic_builder_contains(gb, v));

	/* export the value back */
	return fy_generic_builder_export(gb, v);
}

/* Test: Builder scoping */
START_TEST(gb_scoping)
{
	char buf[8192];
	struct fy_generic_builder *gb;
	fy_generic seq, v;
	long long expected, result;

	gb = fy_generic_builder_create_in_place(FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER, NULL,
			buf, sizeof(buf));
	ck_assert(gb != NULL);

	seq = fy_gb_sequence(gb, 100, 200, 300, FYGT_INT_INPLACE_MAX+1);
	ck_assert(fy_generic_is_sequence(seq));

	expected = 100 + 200 + 300 + FYGT_INT_INPLACE_MAX+1;

	/* calculate */
	v = calculate_seq_sum(0, gb, seq);

	/* verify the result is out of place */
	ck_assert(!fy_generic_is_in_place(v));

	/* verify that our builder contains it */
	ck_assert(fy_generic_builder_contains(gb, v));

	result = fy_cast(v, (long long)0);

	printf("expected=%lld result=%lld\n", expected, result);
	ck_assert(expected == result);
}
END_TEST

/* Test: Builder dedup scoping */
START_TEST(gb_dedup_scoping)
{
	char buf[8192];
	struct fy_generic_builder *gb;
	fy_generic seq, v, vexp;
	long long expected;

	gb = fy_generic_builder_create_in_place(
			FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED, NULL,
			buf, sizeof(buf));
	ck_assert(gb != NULL);

	seq = fy_gb_sequence(gb, 100, 200, 300, FYGT_INT_INPLACE_MAX+1);
	ck_assert(fy_generic_is_sequence(seq));

	expected = 100 + 200 + 300 + FYGT_INT_INPLACE_MAX+1;

	/* internalize the expected value */
	vexp = fy_gb_internalize(gb, fy_value(expected));
	ck_assert(fy_generic_is_long_long(vexp));

	fprintf(stderr, "vexp=0x%016lx\n", vexp.v);

	/* verify the result is out of place */
	ck_assert(!fy_generic_is_in_place(vexp));

	/* calculate (but with dedup enabled) */
	v = calculate_seq_sum(FYGBCF_DEDUP_ENABLED, gb, seq);

	fprintf(stderr, "v=0x%016lx\n", v.v);

	/* verify the result is out of place */
	ck_assert(!fy_generic_is_in_place(v));

	/* verify that the result is exactly the same one that we internalized */
	ck_assert(vexp.v == v.v);
}
END_TEST

/* Test: Builder dedup scoping2 */
START_TEST(gb_dedup_scoping2)
{
	char buf1[8192], buf2[8192];
	struct fy_generic_builder *gb1, *gb2;
	fy_generic v1, v2;

	/* create two stacked dedup builders */
	gb1 = fy_generic_builder_create_in_place(
			FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED, NULL,
			buf1, sizeof(buf1));
	ck_assert(gb1 != NULL);

	gb2 = fy_generic_builder_create_in_place(
			FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED, gb1,
			buf2, sizeof(buf2));
	ck_assert(gb2 != NULL);

	/* verify that the top one has dedup chain enabled */
	ck_assert((gb2->flags & FYGBF_DEDUP_CHAIN) != 0);

	/* store a long string in the first builder */
	v1 = fy_value(gb1, "This is a long string");
	ck_assert(fy_generic_is_valid(v1));
	ck_assert(fy_generic_builder_contains(gb1, v1));

	/* store the same string in the second */
	v2 = fy_value(gb2, "This is a long string");
	ck_assert(fy_generic_is_valid(v2));
	ck_assert(fy_generic_builder_contains(gb2, v2));

	/* both of them should now contain it */
	ck_assert(fy_generic_builder_contains(gb1, v2));

	/* and it must be the same value */
	ck_assert(v1.v == v2.v);

	/* do the same but with a collection */
	v1 = fy_sequence(gb1, 100, 200, "This is a long string");
	ck_assert(fy_generic_is_valid(v1));
	ck_assert(fy_generic_is_sequence(v1));
	ck_assert(fy_generic_builder_contains(gb1, v1));

	v2 = fy_sequence(gb2, 100, 200, "This is a long string");
	ck_assert(fy_generic_is_valid(v2));
	ck_assert(fy_generic_is_sequence(v2));
	ck_assert(fy_generic_builder_contains(gb1, v2));

	ck_assert(fy_generic_builder_contains(gb1, v2));

	/* and it must be the same value */
	ck_assert(v1.v == v2.v);
}
END_TEST

/* Test: polymorphics */
START_TEST(gb_polymorphics)
{
	char buf[8192];
	struct fy_generic_builder *gb;
	fy_generic seq, map;

	/* empty sequence */
	seq = fy_sequence();
	ck_assert(fy_generic_is_sequence(seq));
	ck_assert(fy_len(seq) == 0);

	/* empty mapping */
	map = fy_mapping();
	ck_assert(fy_generic_is_mapping(map));
	ck_assert(fy_len(map) == 0);

	seq = fy_sequence(10);
	ck_assert(fy_generic_is_sequence(seq));

	seq = fy_sequence(10, true, "Hello there", fy_sequence(-1, -2));
	ck_assert(fy_generic_is_sequence(seq));

	gb = fy_generic_builder_create_in_place(FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER, NULL,
			buf, sizeof(buf));
	ck_assert(gb != NULL);

	/* empty sequence */
	seq = fy_sequence(gb);
	ck_assert(fy_generic_is_sequence(seq));
	ck_assert(fy_len(seq) == 0);

	/* empty mapping */
	map = fy_mapping(gb);
	ck_assert(fy_generic_is_mapping(map));
	ck_assert(fy_len(map) == 0);

	seq = fy_sequence(1, 2, 3);
	ck_assert(fy_generic_is_sequence(seq));

	/* verify that our builder does not contain it */
	ck_assert(!fy_generic_builder_contains(gb, seq));

	seq = fy_sequence(gb, 1, 2, 3);
	ck_assert(fy_generic_is_sequence(seq));

	/* verify that our builder now contains it */
	ck_assert(fy_generic_builder_contains(gb, seq));
}
END_TEST

/* Test: basic collection operations */
START_TEST(gb_basic_collection_ops)
{
	char buf[16384];
	struct fy_generic_builder *gb;
	fy_generic items[16];
	fy_generic v;
	fy_generic seq, map;
	size_t len;
	int val;

	gb = fy_generic_builder_create_in_place(FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER, NULL,
			buf, sizeof(buf));
	ck_assert(gb != NULL);

	/* manual sequence creation */
	items[0] = fy_value(1000);
	items[1] = fy_value("Hello");
	items[2] = fy_value(false);

	v = fy_gb_collection_op(gb, FYGBOPF_CREATE_SEQ, 3, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 3);
	ck_assert(fy_get(v, 0, -1) == 1000);
	ck_assert(!strcmp(fy_get(v, 1, ""), "Hello"));
	ck_assert(fy_get(v, 2, true) == false);

	printf("> seq-create: ");
	fy_generic_emit_default(v);

	/* manual mapping creation */
	items[0] = fy_value("foo");
	items[1] = fy_value(100);
	items[2] = fy_value("bar");
	items[3] = fy_value(200);

	v = fy_gb_collection_op(gb, FYGBOPF_CREATE_MAP, 2, items);
	ck_assert(fy_generic_is_mapping(v));
	ck_assert(fy_len(v) == 2);
	ck_assert(fy_get(v, "foo", -1) == 100);
	ck_assert(fy_get(v, "bar", -1) == 200);

	printf("> map-create: ");
	fy_generic_emit_default(v);

	/* testing insert (start with empty sequence) () -> (99) */
	seq = fy_sequence(gb);
	items[0] = fy_value(99);
	v = fy_gb_collection_op(gb, FYGBOPF_INSERT, seq, 0, 1, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 1);
	ck_assert(fy_get(v, 0, -1) == 99);

	printf("> seq-insert-1: ");
	fy_generic_emit_default(v);

	/* testing insert (start with empty sequence) () -> (99, 999) */
	seq = fy_sequence(gb);
	items[0] = fy_value(99);
	items[1] = fy_value(999);
	v = fy_gb_collection_op(gb, FYGBOPF_INSERT, seq, 0, 2, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 2);
	ck_assert(fy_get(v, 0, -1) == 99);
	ck_assert(fy_get(v, 1, -1) == 999);

	printf("> seq-insert-2: ");
	fy_generic_emit_default(v);

	/* testing insert at the beginning (1, 2) -> (100, 1, 2) */
	seq = fy_sequence(gb, 1, 2);
	items[0] = fy_value(100);
	v = fy_gb_collection_op(gb, FYGBOPF_INSERT, seq, 0, 1, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 3);
	ck_assert(fy_get(v, 0, -1) == 100);
	ck_assert(fy_get(v, 1, -1) == 1);
	ck_assert(fy_get(v, 2, -1) == 2);

	printf("> seq-insert-begin: ");
	fy_generic_emit_default(v);

	/* testing insert at the end (1, 2) -> (1, 2, 100) */
	seq = fy_sequence(gb, 1, 2);
	items[0] = fy_value(100);
	v = fy_gb_collection_op(gb, FYGBOPF_INSERT, seq, 2, 1, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 3);
	ck_assert(fy_get(v, 0, -1) == 1);
	ck_assert(fy_get(v, 1, -1) == 2);
	ck_assert(fy_get(v, 2, -1) == 100);

	printf("> seq-insert-end: ");
	fy_generic_emit_default(v);

	/* testing insert at the middle (1, 2) -> (1, 300, 2) */
	seq = fy_sequence(gb, 1, 2);
	items[0] = fy_value(300);
	v = fy_gb_collection_op(gb, FYGBOPF_INSERT, seq, 1, 1, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 3);
	ck_assert(fy_get(v, 0, -1) == 1);
	ck_assert(fy_get(v, 1, -1) == 300);
	ck_assert(fy_get(v, 2, -1) == 2);

	printf("> seq-insert-middle: ");
	fy_generic_emit_default(v);

	/* testing insert (start with empty map) () -> ("foo": 10) */
	map = fy_mapping(gb);
	items[0] = fy_value("foo");
	items[1] = fy_value(10);
	v = fy_gb_collection_op(gb, FYGBOPF_INSERT, map, 0, 1, items);
	ck_assert(fy_generic_is_mapping(v));
	len = fy_len(v);
	ck_assert(len == 1);
	val = fy_get(v, "foo", -1);
	ck_assert(val == 10);
	val = fy_get_at(v, 0, -1);
	ck_assert(val == 10);

	printf("> map-insert-1: ");
	fy_generic_emit_default(v);

	/* testing insert (start with empty mapping) () -> ("foo": 100, "baz": -100) */
	map = fy_mapping(gb);
	items[0] = fy_value("foo");
	items[1] = fy_value(100);
	items[2] = fy_value("baz");
	items[3] = fy_value(-100);
	v = fy_gb_collection_op(gb, FYGBOPF_INSERT, map, 0, 2, items);
	ck_assert(fy_generic_is_mapping(v));
	len = fy_len(v);
	ck_assert(len == 2);
	val = fy_get(v, "foo", -1);
	ck_assert(val == 100);
	val = fy_get_at(v, 0, -1);
	ck_assert(val == 100);
	val = fy_get(v, "baz", -1);
	ck_assert(val == -100);
	val = fy_get_at(v, 1, -1);
	ck_assert(val == -100);

	printf("> map-insert-2: ");
	fy_generic_emit_default(v);

	/* testing insert at the beginning ("foo": 1, "bar": 2) -> ("baz": 100, "foo": 1, "bar": 2) */
	map = fy_mapping(gb, "foo", 1, "bar", 2);
	items[0] = fy_value("baz");
	items[1] = fy_value(100);
	v = fy_gb_collection_op(gb, FYGBOPF_INSERT, map, 0, 1, items);
	ck_assert(fy_generic_is_mapping(v));
	ck_assert(fy_len(v) == 3);
	ck_assert(fy_get(v, "foo", -1) == 1);
	ck_assert(fy_get_at(v, 1, -1) == 1);
	ck_assert(fy_get(v, "bar", -1) == 2);
	ck_assert(fy_get_at(v, 2, -1) == 2);
	ck_assert(fy_get(v, "baz", -1) == 100);
	ck_assert(fy_get_at(v, 0, -1) == 100);

	printf("> map-insert-begin: ");
	fy_generic_emit_default(v);

	/* testing insert at the end ("foo": 1, "bar": 2 ) -> ("foo" 1, "bar":2, "baz": 99) */
	map = fy_mapping(gb, "foo", 1, "bar", 2);
	items[0] = fy_value("baz");
	items[1] = fy_value(99);
	v = fy_gb_collection_op(gb, FYGBOPF_INSERT, map, 2, 1, items);
	ck_assert(fy_generic_is_mapping(v));
	ck_assert(fy_len(v) == 3);
	ck_assert(fy_get(v, "foo", -1) == 1);
	ck_assert(fy_get_at(v, 0, -1) == 1);
	ck_assert(fy_get(v, "bar", -1) == 2);
	ck_assert(fy_get_at(v, 1, -1) == 2);
	ck_assert(fy_get(v, "baz", -1) == 99);
	ck_assert(fy_get_at(v, 2, -1) == 99);

	printf("> map-insert-end: ");
	fy_generic_emit_default(v);

	/* testing insert at the middle ("foo": 1, "bar": 2) -> ("foo": 1, "baz": 300, "bar": 2) */
	map = fy_mapping(gb, "foo", 1, "bar", 2);
	items[0] = fy_value("baz");
	items[1] = fy_value(300);
	v = fy_gb_collection_op(gb, FYGBOPF_INSERT, map, 1, 1, items);
	ck_assert(fy_generic_is_mapping(v));
	ck_assert(fy_len(v) == 3);
	ck_assert(fy_get(v, "foo", -1) == 1);
	ck_assert(fy_get_at(v, 0, -1) == 1);
	ck_assert(fy_get(v, "bar", -1) == 2);
	ck_assert(fy_get_at(v, 2, -1) == 2);
	ck_assert(fy_get(v, "baz", -1) == 300);
	ck_assert(fy_get_at(v, 1, -1) == 300);

	printf("> map-insert-middle: ");
	fy_generic_emit_default(v);

	/* testing replace (start with empty sequence) () -> (99) */
	seq = fy_sequence(gb);
	items[0] = fy_value(99);
	v = fy_gb_collection_op(gb, FYGBOPF_REPLACE, seq, 0, 1, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 1);
	ck_assert(fy_get(v, 0, -1) == 99);

	printf("> seq-replace-1: ");
	fy_generic_emit_default(v);

	/* testing replace (single item, replace it) (100) -> (99) */
	seq = fy_sequence(gb, 100);
	items[0] = fy_value(99);
	v = fy_gb_collection_op(gb, FYGBOPF_REPLACE, seq, 0, 1, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 1);
	ck_assert(fy_get(v, 0, -1) == 99);

	printf("> seq-replace-2: ");
	fy_generic_emit_default(v);

	/* testing replace (single item, replace in the beginning) (100, 200) -> (98, 200) */
	seq = fy_sequence(gb, 100, 200);
	items[0] = fy_value(98);
	v = fy_gb_collection_op(gb, FYGBOPF_REPLACE, seq, 0, 1, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 2);
	ck_assert(fy_get(v, 0, -1) == 98);
	ck_assert(fy_get(v, 1, -1) == 200);

	printf("> seq-replace-3: ");
	fy_generic_emit_default(v);

	/* testing replace (single item, replace in the middle) (100, 1000, 200) -> (100, 1, 200) */
	seq = fy_sequence(gb, 100, 1000, 200);
	items[0] = fy_value(1);
	v = fy_gb_collection_op(gb, FYGBOPF_REPLACE, seq, 1, 1, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 3);
	ck_assert(fy_get(v, 0, -1) == 100);
	ck_assert(fy_get(v, 1, -1) == 1);
	ck_assert(fy_get(v, 2, -1) == 200);

	printf("> seq-replace-3: ");
	fy_generic_emit_default(v);

	/* testing append (single item) (100, 1000, 200) -> (100, 1000, 200, 1) */
	seq = fy_sequence(gb, 100, 1000, 200);
	items[0] = fy_value(1);
	v = fy_gb_collection_op(gb, FYGBOPF_APPEND, seq, 1, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 4);
	ck_assert(fy_get(v, 0, -1) == 100);
	ck_assert(fy_get(v, 1, -1) == 1000);
	ck_assert(fy_get(v, 2, -1) == 200);
	ck_assert(fy_get(v, 3, -1) == 1);

	printf("> seq-append-1: ");
	fy_generic_emit_default(v);
}
END_TEST

/* Test: assoc/deassoc map operations */
START_TEST(gb_assoc_deassoc)
{
	char buf[16384];
	struct fy_generic_builder *gb;
	fy_generic items[16];
	fy_generic map, v;

	gb = fy_generic_builder_create_in_place(FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER, NULL,
			buf, sizeof(buf));
	ck_assert(gb != NULL);

	/* associate, on empty map () -> ( "foo": 10 ) */
	map = fy_mapping();
	items[0] = fy_value("foo");
	items[1] = fy_value(10);
	v = fy_gb_collection_op(gb, FYGBOPF_ASSOC, map, 1, items);
	ck_assert(fy_generic_is_mapping(v));
	ck_assert(fy_len(v) == 1);
	ck_assert(fy_get(v, "foo", -1) == 10);
	ck_assert(fy_get_at(v, 0, -1) == 10);

	printf("> assoc-on-empty: ");
	fy_generic_emit_default(v);

	/* associate, on non empty map ("bar": 100) -> ( "foo": 10 ) */
	map = fy_mapping("bar", 100);
	items[0] = fy_value("foo");
	items[1] = fy_value(10);
	v = fy_gb_collection_op(gb, FYGBOPF_ASSOC, map, 1, items);
	ck_assert(fy_generic_is_mapping(v));
	ck_assert(fy_len(v) == 2);
	ck_assert(fy_get(v, "bar", -1) == 100);
	ck_assert(fy_get_at(v, 0, -1) == 100);
	ck_assert(fy_get(v, "foo", -1) == 10);
	ck_assert(fy_get_at(v, 1, -1) == 10);

	printf("> assoc-on-non-empty: ");
	fy_generic_emit_default(v);

	/* associate, replace value ("bar": 100) -> ( "bar": 10 ) */
	map = fy_mapping("bar", 100);
	items[0] = fy_value("bar");
	items[1] = fy_value(10);
	v = fy_gb_collection_op(gb, FYGBOPF_ASSOC, map, 1, items);
	ck_assert(fy_generic_is_mapping(v));
	ck_assert(fy_len(v) == 1);
	ck_assert(fy_get(v, "bar", -1) == 10);
	ck_assert(fy_get_at(v, 0, -1) == 10);

	printf("> assoc-replace: ");
	fy_generic_emit_default(v);

	/* associate, replace and append value ("foo": 1, "bar": 100) -> ( "foo": 10, "bar": 100, "baz": 1000 ) */
	map = fy_mapping("foo", 1, "bar", 100);
	items[0] = fy_value("foo");
	items[1] = fy_value(10);
	items[2] = fy_value("baz");
	items[3] = fy_value(1000);
	v = fy_gb_collection_op(gb, FYGBOPF_ASSOC, map, 2, items);
	ck_assert(fy_generic_is_mapping(v));
	ck_assert(fy_len(v) == 3);
	ck_assert(fy_get(v, "foo", -1) == 10);
	ck_assert(fy_get_at(v, 0, -1) == 10);
	ck_assert(fy_get(v, "bar", -1) == 100);
	ck_assert(fy_get_at(v, 1, -1) == 100);
	ck_assert(fy_get(v, "baz", -1) == 1000);
	ck_assert(fy_get_at(v, 2, -1) == 1000);

	printf("> assoc-replace-append: ");
	fy_generic_emit_default(v);

	/* disassociate, mapping goes empty ("bar": 100) -> () */
	map = fy_mapping("bar", 100);
	items[0] = fy_value("bar");
	v = fy_gb_collection_op(gb, FYGBOPF_DISASSOC, map, 1, items);
	ck_assert(fy_generic_is_mapping(v));
	ck_assert(fy_len(v) == 0);
	/* must be exactly empty */
	ck_assert(v.v == fy_map_empty_value);

	printf("> disassoc-goes-empty: ");
	fy_generic_emit_default(v);

	/* disassociate, mapping is unchanged ("bar": 100) -> ("bar": 100) */
	map = fy_mapping("bar", 100);
	items[0] = fy_value("foo");
	v = fy_gb_collection_op(gb, FYGBOPF_DISASSOC, map, 1, items);
	ck_assert(fy_generic_is_mapping(v));
	ck_assert(fy_len(v) == 1);
	ck_assert(fy_get(v, "bar", -1) == 100);
	ck_assert(fy_get_at(v, 0, -1) == 100);
	/* must be exactly the same */

	/* will not be the same value, but it will be the same content */
	ck_assert(!fy_generic_compare(map, v));

	printf("> disassoc-nop: ");
	fy_generic_emit_default(v);
}
END_TEST

/* Test: map utilities */
START_TEST(gb_map_utils)
{
	char buf[16384];
	struct fy_generic_builder *gb;
	fy_generic items[16];
	fy_generic map, v, seq;
	const char *str;
	int ival;

	gb = fy_generic_builder_create_in_place(FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER, NULL,
			buf, sizeof(buf));
	ck_assert(gb != NULL);

	/* empty map, return keys, values, items as an empty sequence */
	map = fy_mapping();
	v = fy_gb_collection_op(gb, FYGBOPF_KEYS, map);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(v.v == fy_seq_empty_value);
	printf("> map-keys-on-empty: ");
	fy_generic_emit_default(v);

	v = fy_gb_collection_op(gb, FYGBOPF_VALUES, map);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(v.v == fy_seq_empty_value);
	printf("> map-values-on-empty: ");
	fy_generic_emit_default(v);

	v = fy_gb_collection_op(gb, FYGBOPF_ITEMS, map);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(v.v == fy_seq_empty_value);
	printf("> map-items-on-empty: ");
	fy_generic_emit_default(v);

	/* single pair map, return keys, values, items as a single item sequence */
	map = fy_mapping("foo", 100);
	v = fy_gb_collection_op(gb, FYGBOPF_KEYS, map);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 1);
	str = fy_get(v, 0, "");
	ck_assert(!strcmp(str, "foo"));
	printf("> map-keys-on-1: ");
	fy_generic_emit_default(v);

	v = fy_gb_collection_op(gb, FYGBOPF_VALUES, map);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 1);
	ival = fy_get(v, 0, -1);
	ck_assert(ival == 100);
	printf("> map-values-on-1: ");
	fy_generic_emit_default(v);

	v = fy_gb_collection_op(gb, FYGBOPF_ITEMS, map);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 1);
	seq = fy_get(v, 0, fy_invalid);
	ck_assert(fy_generic_is_sequence(seq));
	str = fy_get(seq, 0, "");
	ck_assert(!strcmp(str, "foo"));
	ival = fy_get(seq, 1, -1);
	ck_assert(ival == 100);
	printf("> map-values-on-1: ");
	fy_generic_emit_default(v);

	/* bigger map */
	map = fy_mapping("foo", 100, "bar", 200, "baz", 300, "long string key", false);
	v = fy_gb_collection_op(gb, FYGBOPF_KEYS, map);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 4);
	str = fy_get(v, 0, "");
	ck_assert(!strcmp(str, "foo"));
	str = fy_get(v, 1, "");
	ck_assert(!strcmp(str, "bar"));
	str = fy_get(v, 2, "");
	ck_assert(!strcmp(str, "baz"));
	str = fy_get(v, 3, "");
	ck_assert(!strcmp(str, "long string key"));

	printf("> map-key-on-4: ");
	fy_generic_emit_default(v);

	/* contains on an empty map  */
	map = fy_mapping();
	items[0] = fy_value("foo");
	v = fy_gb_collection_op(gb, FYGBOPF_CONTAINS, map, 1, items);
	ck_assert(v.v == fy_false_value);

	/* contains on an map which does not contain the key */
	map = fy_mapping("bar", 10);
	items[0] = fy_value("foo");
	v = fy_gb_collection_op(gb, FYGBOPF_CONTAINS, map, 1, items);
	ck_assert(v.v == fy_false_value);

	/* contains on an map which contains the key */
	map = fy_mapping("foo", 100);
	items[0] = fy_value("foo");
	v = fy_gb_collection_op(gb, FYGBOPF_CONTAINS, map, 1, items);
	ck_assert(v.v == fy_true_value);

	/* contains on an map which does not contain any of the keys */
	map = fy_mapping("foo", 100, "bar", 1000, "baz", 5);
	items[0] = fy_value("nope");
	items[1] = fy_value("neither");
	v = fy_gb_collection_op(gb, FYGBOPF_CONTAINS, map, 2, items);
	ck_assert(v.v == fy_false_value);

	/* contains on an map which contains the second key */
	map = fy_mapping("foo", 100, "bar", 1000, "baz", 5);
	items[0] = fy_value("nope");
	items[1] = fy_value("bar");
	v = fy_gb_collection_op(gb, FYGBOPF_CONTAINS, map, 2, items);
	ck_assert(v.v == fy_true_value);
}
END_TEST

/* Test: seq utilities */
START_TEST(gb_seq_utils)
{
	char buf[16384];
	struct fy_generic_builder *gb;
	fy_generic items[16];
	fy_generic seq, v;

	gb = fy_generic_builder_create_in_place(FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER, NULL,
			buf, sizeof(buf));
	ck_assert(gb != NULL);

	/* empty sequence, concat with nothing -> empty sequence */
	seq = fy_sequence();
	v = fy_gb_collection_op(gb, FYGBOPF_CONCAT, seq, 0, NULL);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(v.v == fy_seq_empty_value);
	printf("> seq-empty-concat-0: ");
	fy_generic_emit_default(v);

	/* empty sequence, concat with empty sequences -> empty sequence */
	seq = fy_sequence();
	items[0] = fy_sequence();
	items[1] = fy_sequence();
	v = fy_gb_collection_op(gb, FYGBOPF_CONCAT, seq, 2, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(v.v == fy_seq_empty_value);
	printf("> seq-empty-concat-empty: ");
	fy_generic_emit_default(v);

	/* empty sequence, concat with non empty sequences -> non empty sequences */
	seq = fy_sequence();
	items[0] = fy_sequence(1, 2);
	items[1] = fy_sequence(3);
	v = fy_gb_collection_op(gb, FYGBOPF_CONCAT, seq, 2, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 3);
	ck_assert(fy_get(v, 0, -1) == 1);
	ck_assert(fy_get(v, 1, -1) == 2);
	ck_assert(fy_get(v, 2, -1) == 3);
	printf("> seq-empty-concat-non-empty: ");
	fy_generic_emit_default(v);

	/* a non empty sequence, concat with empty sequences -> same sequence */
	seq = fy_sequence(1, 2, 3);
	items[0] = fy_sequence();
	v = fy_gb_collection_op(gb, FYGBOPF_CONCAT, seq, 1, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(!fy_generic_compare(seq, v));
	printf("> seq-concat-empty: ");
	fy_generic_emit_default(v);

	/* a non empty sequence, concat with two non empty sequences */
	seq = fy_sequence(1, 2, 3);
	items[0] = fy_sequence(4, 5);
	items[1] = fy_sequence(6, 7);
	v = fy_gb_collection_op(gb, FYGBOPF_CONCAT, seq, 2, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 7);
	ck_assert(fy_get(v, 0, -1) == 1);
	ck_assert(fy_get(v, 1, -1) == 2);
	ck_assert(fy_get(v, 2, -1) == 3);
	ck_assert(fy_get(v, 3, -1) == 4);
	ck_assert(fy_get(v, 4, -1) == 5);
	ck_assert(fy_get(v, 5, -1) == 6);
	ck_assert(fy_get(v, 6, -1) == 7);

	printf("> seq-concat-regular: ");
	fy_generic_emit_default(v);

	/* a non empty sequence, concat with two non empty sequences and one empty in the middle*/
	seq = fy_sequence(1, 2, 3);
	items[0] = fy_sequence(4, 5);
	items[1] = fy_sequence();
	items[2] = fy_sequence(6, 7);
	v = fy_gb_collection_op(gb, FYGBOPF_CONCAT, seq, 3, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 7);
	ck_assert(fy_get(v, 0, -1) == 1);
	ck_assert(fy_get(v, 1, -1) == 2);
	ck_assert(fy_get(v, 2, -1) == 3);
	ck_assert(fy_get(v, 3, -1) == 4);
	ck_assert(fy_get(v, 4, -1) == 5);
	ck_assert(fy_get(v, 5, -1) == 6);
	ck_assert(fy_get(v, 6, -1) == 7);

	printf("> seq-concat-regular-empty: ");
	fy_generic_emit_default(v);

	/* empty sequence, reverse with nothing -> empty sequence */
	seq = fy_sequence();
	v = fy_gb_collection_op(gb, FYGBOPF_REVERSE, seq, 0, NULL);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(v.v == fy_seq_empty_value);
	printf("> seq-empty-reverse-0: ");
	fy_generic_emit_default(v);

	/* empty sequence, reverse with empty sequences -> empty sequence */
	seq = fy_sequence();
	items[0] = fy_sequence();
	items[1] = fy_sequence();
	v = fy_gb_collection_op(gb, FYGBOPF_REVERSE, seq, 2, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(v.v == fy_seq_empty_value);
	printf("> seq-empty-reverse-empty: ");
	fy_generic_emit_default(v);

	/* empty sequence, reverse with non empty sequences -> non empty sequences */
	seq = fy_sequence();
	items[0] = fy_sequence(1, 2);
	items[1] = fy_sequence(3);
	v = fy_gb_collection_op(gb, FYGBOPF_REVERSE, seq, 2, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 3);
	ck_assert(fy_get(v, 0, -1) == 3);
	ck_assert(fy_get(v, 1, -1) == 2);
	ck_assert(fy_get(v, 2, -1) == 1);
	printf("> seq-empty-reverse-non-empty: ");
	fy_generic_emit_default(v);

	/* a non empty sequence, reverse with empty sequences */
	seq = fy_sequence(1, 2, 3);
	items[0] = fy_sequence();
	v = fy_gb_collection_op(gb, FYGBOPF_REVERSE, seq, 1, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 3);
	ck_assert(fy_get(v, 0, -1) == 3);
	ck_assert(fy_get(v, 1, -1) == 2);
	ck_assert(fy_get(v, 2, -1) == 1);
	printf("> seq-reverse-empty: ");
	fy_generic_emit_default(v);

	/* a non empty sequence, reverse with two non empty sequences */
	seq = fy_sequence(1, 2, 3);
	items[0] = fy_sequence(4, 5);
	items[1] = fy_sequence(6, 7);
	v = fy_gb_collection_op(gb, FYGBOPF_REVERSE, seq, 2, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 7);
	ck_assert(fy_get(v, 0, -1) == 7);
	ck_assert(fy_get(v, 1, -1) == 6);
	ck_assert(fy_get(v, 2, -1) == 5);
	ck_assert(fy_get(v, 3, -1) == 4);
	ck_assert(fy_get(v, 4, -1) == 3);
	ck_assert(fy_get(v, 5, -1) == 2);
	ck_assert(fy_get(v, 6, -1) == 1);

	printf("> seq-reverse-regular: ");
	fy_generic_emit_default(v);

	/* a non empty sequence, reverse with two non empty sequences and one empty in the middle*/
	seq = fy_sequence(1, 2, 3);
	items[0] = fy_sequence(4, 5);
	items[1] = fy_sequence();
	items[2] = fy_sequence(6, 7);
	v = fy_gb_collection_op(gb, FYGBOPF_REVERSE, seq, 3, items);
	ck_assert(fy_generic_is_sequence(v));
	ck_assert(fy_len(v) == 7);
	ck_assert(fy_get(v, 0, -1) == 7);
	ck_assert(fy_get(v, 1, -1) == 6);
	ck_assert(fy_get(v, 2, -1) == 5);
	ck_assert(fy_get(v, 3, -1) == 4);
	ck_assert(fy_get(v, 4, -1) == 3);
	ck_assert(fy_get(v, 5, -1) == 2);
	ck_assert(fy_get(v, 6, -1) == 1);

	printf("> seq-reverse-regular-empty: ");
	fy_generic_emit_default(v);
}
END_TEST

TCase *libfyaml_case_generic(void)
{
	TCase *tc;

	tc = tcase_create("generic");

	/* baby steps first */
	tcase_add_test(tc, generic_basics);
	tcase_add_test(tc, generic_bool_range);
	tcase_add_test(tc, generic_int_range);
	tcase_add_test(tc, generic_float_range);
	tcase_add_test(tc, generic_size_encoding);
	tcase_add_test(tc, generic_string_range);
	tcase_add_test(tc, generic_type_promotion);

	/* invalid propagation tests */
	tcase_add_test(tc, generic_invalid_propagation);

	/* sized string (any kind of data including zeroes) */
	tcase_add_test(tc, generic_sized_string);

	/* decorated int */
	tcase_add_test(tc, generic_decorated_int);

	/* casts */
	tcase_add_test(tc, generic_casts);

	/* get */
	tcase_add_test(tc, generic_get);

	/* get_at testing */
	tcase_add_test(tc, generic_get_at);

	/* compare */
	tcase_add_test(tc, generic_compare);

	/* builders */
	tcase_add_test(tc, gb_basics);

	/* special for is_unsigned */
	tcase_add_test(tc, generic_is_unsigned);

	/* continue with builders */
	tcase_add_test(tc, gb_dedup_basics);
	tcase_add_test(tc, gb_scoping);
	tcase_add_test(tc, gb_dedup_scoping);
	tcase_add_test(tc, gb_dedup_scoping2);

	/* the polymorphic stuff */
	tcase_add_test(tc, gb_polymorphics);

	/* time to do operations on collection */
	tcase_add_test(tc, gb_basic_collection_ops);
	tcase_add_test(tc, gb_assoc_deassoc);

	/* map utils */
	tcase_add_test(tc, gb_map_utils);

	/* seq utils */
	tcase_add_test(tc, gb_seq_utils);

	return tc;
}
