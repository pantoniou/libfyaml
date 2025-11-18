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

	/* mapping */
	v = fy_local_mapping(fy_local_int(10), fy_local_string("a string to test"));
	ck_assert(fy_generic_is_mapping(v));
	ck_assert(!fy_generic_is_in_place(v));
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
		0.0, 1.0, -1.0, 0.1, -0.1,
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

		if (isfinite(fv)) {
			pass = fv == res;
		} else if (isinf(fv))
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

TCase *libfyaml_case_generic(void)
{
	TCase *tc;

	tc = tcase_create("generic");

	/* baby steps first */
	tcase_add_test(tc, generic_basics);
	tcase_add_test(tc, generic_bool_range);
	tcase_add_test(tc, generic_int_range);
	tcase_add_test(tc, generic_float_range);

	return tc;
}
