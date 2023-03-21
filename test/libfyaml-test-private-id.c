/*
 * libfyaml-test-private-id.c - libfyaml id allocation and handling
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
#include <alloca.h>

#include <check.h>

#include <libfyaml.h>

#include "fy-utils.h"
#include "fy-id.h"

/* check ffs works */
START_TEST(id_ffs)
{
	static const struct {
		fy_id_bits v;
		int r;
	} ffs_check[] = {
		{ .v = 0, .r = -1 },
		{ .v = ~(fy_id_bits)0, .r = 0 },
		{ .v = ((fy_id_bits)1 << 0), .r = 0 },
		{ .v = ((fy_id_bits)1 << (FY_ID_BITS_BITS - 1)), .r = (FY_ID_BITS_BITS - 1) },
		{ .v = ((fy_id_bits)1 << 0) | ((fy_id_bits)1 << (FY_ID_BITS_BITS - 1)), .r = 0 },
		{ .v = ((fy_id_bits)1 << (FY_ID_BITS_BITS / 2)), .r = (FY_ID_BITS_BITS / 2), },
	};
	unsigned int i;
	int r;

	for (i = 0; i < ARRAY_SIZE(ffs_check); i++) {
		r = fy_id_ffs(ffs_check[i].v);
		ck_assert_int_eq(r, ffs_check[i].r);
	}
}
END_TEST

/* a random bit number for the following tests, not a power of 2 */
#define BA_BITS 67

#define BA_DECL \
	size_t ba_count = FY_ID_BITS_ARRAY_COUNT(BA_BITS); \
	fy_id_bits *ba = alloca(ba_count * sizeof(*ba))

/* verify that reset clears everything */
START_TEST(id_reset)
{
	BA_DECL;
	unsigned int i;

	fy_id_reset(ba, ba_count);
	for (i = 0; i < ba_count; i++)
		ck_assert_int_eq(ba[i], 0);
}
END_TEST

/* verify that allocation to the full works, and then fails */
START_TEST(id_alloc_full)
{
	BA_DECL;
	unsigned int ba_bits_actual = FY_ID_BITS_ARRAY_COUNT_BITS(BA_BITS);
	unsigned int i;
	int id, expected_id;

	fy_id_reset(ba, ba_count);

	/* allocate all, verify that we get numbers in sequence */
	expected_id = 0;
	for (i = 0; i < ba_bits_actual; i++) {
		id = fy_id_alloc(ba, ba_count);
		ck_assert_int_eq(id, expected_id);
		expected_id++;
	}

	/* full, it must fail now */
	id = fy_id_alloc(ba, ba_count);
	ck_assert_int_eq(id, -1);
}
END_TEST

/* verify that allocation when almost full works, and then fails */
START_TEST(id_alloc_almost_full)
{
	BA_DECL;
	unsigned int ba_bits_actual = FY_ID_BITS_ARRAY_COUNT_BITS(BA_BITS);
	unsigned int i;
	int id, expected_id;

	/* fill the array */
	fy_id_reset(ba, ba_count);
	for (i = 0; i < ba_bits_actual; i++)
		fy_id_set_used(ba, ba_count, i);

	/* allocate all, verify that we get numbers in sequence */
	expected_id = 0;
	for (i = 0; i < ba_bits_actual; i++) {
		/* free one, and allocate, it must succeed at the exact spot */
		fy_id_free(ba, ba_count, i);
		id = fy_id_alloc(ba, ba_count);
		ck_assert_int_eq(id, expected_id);
		expected_id++;

		/* now it must fail */
		id = fy_id_alloc(ba, ba_count);
		ck_assert_int_eq(id, -1);
	}
}
END_TEST

/* verify that allocation of even bits works */
START_TEST(id_alloc_even)
{
	BA_DECL;
	unsigned int ba_bits_actual = FY_ID_BITS_ARRAY_COUNT_BITS(BA_BITS);
	unsigned int i, j;
	int id, expected_id;

	/* fill the array */
	fy_id_reset(ba, ba_count);
	for (i = 0; i < ba_bits_actual; i++)
		fy_id_set_used(ba, ba_count, i);

	/* free the even ids */
	j = ba_bits_actual / 2;
	for (i = 0; i < j; i++)
		fy_id_free(ba, ba_count, i * 2);

	/* allocate all, verify that we get numbers in sequence */
	expected_id = 0;
	for (i = 0; i < j; i++) {
		id = fy_id_alloc(ba, ba_count);
		ck_assert_int_eq(id, expected_id);
		expected_id += 2;
	}
}
END_TEST

/* verify that allocation of odd bits works */
START_TEST(id_alloc_odd)
{
	BA_DECL;
	unsigned int ba_bits_actual = FY_ID_BITS_ARRAY_COUNT_BITS(BA_BITS);
	unsigned int i, j;
	int id, expected_id;

	/* fill the array */
	fy_id_reset(ba, ba_count);
	for (i = 0; i < ba_bits_actual; i++)
		fy_id_set_used(ba, ba_count, i);

	/* free the even ids */
	j = ba_bits_actual / 2;
	for (i = 0; i < j; i++)
		fy_id_free(ba, ba_count, i * 2 + 1);

	/* allocate all, verify that we get numbers in sequence */
	expected_id = 1;
	for (i = 0; i < j; i++) {
		id = fy_id_alloc(ba, ba_count);
		ck_assert_int_eq(id, expected_id);
		expected_id += 2;
	}
}
END_TEST

/* verify that a specific allocation sequence works */
START_TEST(id_alloc_seq)
{
	BA_DECL;
	unsigned int ba_bits_actual = FY_ID_BITS_ARRAY_COUNT_BITS(BA_BITS);
	unsigned int i;
	int id, expected_id;
	int check_ids[] = {
		0,
		ba_bits_actual / 2 - 1,
		ba_bits_actual / 2,
		ba_bits_actual / 2 + 1,
		ba_bits_actual - 1
	};

	/* fill the array */
	fy_id_reset(ba, ba_count);
	for (i = 0; i < ba_bits_actual; i++)
		fy_id_set_used(ba, ba_count, i);

	/* free those specific bits */
	for (i = 0; i < ARRAY_SIZE(check_ids); i++)
		fy_id_free(ba, ba_count, check_ids[i]);

	/* now allocate in sequence */
	expected_id = 0;
	for (i = 0; i < ARRAY_SIZE(check_ids); i++) {
		expected_id = check_ids[i];
		id = fy_id_alloc(ba, ba_count);
		ck_assert_int_eq(id, expected_id);
	}
}
END_TEST

/* verify that a iterator works for a single bit in array */
START_TEST(id_iter_single)
{
	BA_DECL;
	unsigned int ba_bits_actual = FY_ID_BITS_ARRAY_COUNT_BITS(BA_BITS);
	unsigned int i;
	int id, expected_id, found_id;
	struct fy_id_iter iter;

	for (i = 0; i < ba_bits_actual; i++) {
		fy_id_reset(ba, ba_count);
		fy_id_set_used(ba, ba_count, i);

		expected_id = i;
		found_id = -1;
		fy_id_iter_begin(ba, ba_count, &iter);
		while ((id = fy_id_iter_next(ba, ba_count, &iter)) >= 0) {
			/* must find a single one */
			ck_assert_int_lt(found_id, 0);
			found_id = id;
			ck_assert_int_eq(id, expected_id);
		}
		fy_id_iter_end(ba, ba_count, &iter);
	}

}
END_TEST

/* verify that a iterator works for a full array */
START_TEST(id_iter_full)
{
	BA_DECL;
	unsigned int ba_bits_actual = FY_ID_BITS_ARRAY_COUNT_BITS(BA_BITS);
	unsigned int i;
	int id, expected_id;
	struct fy_id_iter iter;

	fy_id_reset(ba, ba_count);
	for (i = 0; i < ba_bits_actual; i++)
		fy_id_set_used(ba, ba_count, i);

	expected_id = 0;
	fy_id_iter_begin(ba, ba_count, &iter);
	while ((id = fy_id_iter_next(ba, ba_count, &iter)) >= 0) {
		ck_assert_int_eq(id, expected_id);
		expected_id++;
	}

	/* we must have run through the whole array */
	ck_assert_int_eq(expected_id, ba_bits_actual);

	fy_id_iter_end(ba, ba_count, &iter);
}
END_TEST

/* verify that a iterator works for sequences up to 3 bits */
START_TEST(id_iter_seq)
{
	BA_DECL;
	unsigned int ba_bits_actual = FY_ID_BITS_ARRAY_COUNT_BITS(BA_BITS);
	int idtab[][3] = {
		{ 0, ba_bits_actual - 1, -1 },
		{ 0, 1, -1 },
		{ ba_bits_actual - 2, ba_bits_actual - 1, -1 },
		{ 0, FY_ID_BITS_BITS - 1, -1 },
		{ 0, FY_ID_BITS_BITS, -1 },
		{ FY_ID_BITS_BITS - 1, FY_ID_BITS_BITS, -1 },
		{ FY_ID_BITS_BITS, FY_ID_BITS_BITS + 1, -1 },
		{ 0, 1, 2 },
		{ 0, ba_bits_actual - 2, ba_bits_actual - 1 },
		{ ba_bits_actual - 3, ba_bits_actual - 2, ba_bits_actual - 1 },
	};
	unsigned int i;
	int id, expected_id;
	int p0, p1, p2;
	struct fy_id_iter iter;

	for (i = 0; i < ARRAY_SIZE(idtab); i++) {

		p0 = idtab[i][0];
		p1 = idtab[i][1];
		p2 = idtab[i][2];

		fy_id_reset(ba, ba_count);
		if (p0 >= 0)
			fy_id_set_used(ba, ba_count, p0);
		if (p1 >= 0)
			fy_id_set_used(ba, ba_count, p1);
		if (p2 >= 0)
			fy_id_set_used(ba, ba_count, p2);

		fy_id_iter_begin(ba, ba_count, &iter);

		if (p0 >= 0) {
			id = fy_id_iter_next(ba, ba_count, &iter);
			expected_id = p0;
			ck_assert_int_eq(id, expected_id);
			fy_id_set_free(ba, ba_count, expected_id);
		}

		if (p1 >= 0) {
			id = fy_id_iter_next(ba, ba_count, &iter);
			expected_id = p1;
			ck_assert_int_eq(id, expected_id);
			fy_id_set_free(ba, ba_count, expected_id);
		}

		if (p2 >= 0) {
			id = fy_id_iter_next(ba, ba_count, &iter);
			expected_id = p2;
			ck_assert_int_eq(id, expected_id);
			fy_id_set_free(ba, ba_count, expected_id);
		}
		fy_id_iter_end(ba, ba_count, &iter);

		/* and now it should be empty */
		fy_id_iter_begin(ba, ba_count, &iter);
		id = fy_id_iter_next(ba, ba_count, &iter);
		ck_assert_int_eq(id, -1);
		fy_id_iter_end(ba, ba_count, &iter);
	}
}
END_TEST

TCase *libfyaml_case_private_id(void)
{
	TCase *tc;

	tc = tcase_create("private-id");

	tcase_add_test(tc, id_ffs);

	tcase_add_test(tc, id_reset);
	tcase_add_test(tc, id_alloc_full);
	tcase_add_test(tc, id_alloc_almost_full);
	tcase_add_test(tc, id_alloc_even);
	tcase_add_test(tc, id_alloc_odd);
	tcase_add_test(tc, id_alloc_seq);

	tcase_add_test(tc, id_iter_single);
	tcase_add_test(tc, id_iter_full);
	tcase_add_test(tc, id_iter_seq);

	return tc;
}
