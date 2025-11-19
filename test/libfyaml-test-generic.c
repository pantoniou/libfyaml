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

/* Test: comparisons */
START_TEST(generic_compare)
{
	fy_generic v1, v2;
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
	ck_assert(fy_compare(v1, v2) == 0);
	ck_assert(fy_compare(v1, fy_cast(fy_local_sequence(1, 2, 3), fy_seq_handle_null)) == 0);
	ck_assert(fy_compare(fy_cast(fy_local_sequence(1, 2, 3), fy_seq_handle_null),
			     fy_cast(fy_local_sequence(1, 2, 3), fy_seq_handle_null)) == 0);

	/* sequence > */
	v1 = fy_local_sequence(1, 8, 3);
	v2 = fy_local_sequence(1, 2, 10);
	ck_assert(fy_compare(v1, v2) > 0);
	ck_assert(fy_compare(v1, fy_cast(fy_local_sequence(1, 2, 10), fy_seq_handle_null)) > 0);
	ck_assert(fy_compare(fy_cast(fy_local_sequence(1, 8, 3), fy_seq_handle_null),
			     fy_cast(fy_local_sequence(1, 2, 10), fy_seq_handle_null)) > 0);

	/* mapping equality (easy) */
	v1 = fy_local_mapping("foo", 10, "bar", 100);
	v2 = fy_local_mapping("foo", 10, "bar", 100);
	ck_assert(fy_compare(v1, v2) == 0);
	ck_assert(fy_compare(v1, fy_cast(fy_local_mapping("foo", 10, "bar", 100), fy_map_handle_null)) == 0);
	ck_assert(fy_compare(fy_cast(fy_local_mapping("foo", 10, "bar", 100), fy_map_handle_null),
			     fy_cast(fy_local_mapping("foo", 10, "bar", 100), fy_map_handle_null)) == 0);

	/* mapping equality (reorders) */
	v1 = fy_local_mapping("foo", 10, "bar", 100);
	v2 = fy_local_mapping("bar", 100, "foo", 10);
	ck_assert(fy_compare(v1, v2) == 0);
	ck_assert(fy_compare(v1, fy_cast(fy_local_mapping("foo", 10, "bar", 100), fy_map_handle_null)) == 0);
	ck_assert(fy_compare(fy_cast(fy_local_mapping("foo", 10, "bar", 100), fy_map_handle_null),
			     fy_cast(fy_local_mapping("bar", 100, "foo", 10), fy_map_handle_null)) == 0);

	/* mapping inequality (reorders) */
	v1 = fy_local_mapping("foo", 10, "bar", 101);
	v2 = fy_local_mapping("bar", 100, "foo", 10);
	ret = fy_compare(v1, v2);
	ck_assert(ret > 0);
	ret = fy_compare(v1, fy_cast(fy_local_mapping("bar", 100, "foo", 10), fy_map_handle_null));
	ck_assert(ret > 0);
	ret = fy_compare(fy_cast(fy_local_mapping("foo", 10, "bar", 101), fy_map_handle_null),
			 fy_cast(fy_local_mapping("bar", 100, "foo", 10), fy_map_handle_null));
	ck_assert(ret > 0);
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

	/* compare */
	tcase_add_test(tc, generic_compare);

	return tc;
}
