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

/* Test: Automatic generic type promotion, sanity testing */
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

	tcase_add_test(tc, generic_invalid_propagation);

	return tc;
}
