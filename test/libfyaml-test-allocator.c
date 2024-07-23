/*
 * libfyaml-test-allocator.c - libfyaml test public allocator interface
 *
 * Copyright (c) 2024 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
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
#include <stdint.h>

#include <check.h>

#include <libfyaml.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) ((sizeof(x)/sizeof((x)[0])))
#endif

START_TEST(allocator_builtins)
{
	static const char *builtin_allocators[] = {
		"linear",
		"malloc",
		"mremap",
		"dedup",
		"auto",
		NULL,
	};
	const char **pp, *p;

	/* verify that all the builtins are available */
	pp = builtin_allocators;
	while ((p = *pp++) != NULL) {
		fprintf(stderr, "checking builtin allocator: %s\n", p);
		ck_assert(fy_allocator_is_available(p));
	}
}
END_TEST

static void test_allocator_alignment(struct fy_allocator *a, int tag)
{
	size_t sz, align;
	void *p;

	/* 1, 2, 4, 8, 16 bytes allocations */
	for (sz = 1; sz <= 16; sz <<= 1) {

		align = sz;

		/* allocate and check alignment */
		p = fy_allocator_alloc(a, tag, sz, align);
		ck_assert_ptr_ne(p, NULL);
		ck_assert(((uintptr_t)p & (align - 1)) == 0);
	}
}

START_TEST(allocator_linear_buf)
{
	/* align to 64 bits */
	union {
		char buf[1024];
		uint64_t dummy;
	} u;
	struct fy_linear_allocator_cfg lcfg;
	struct fy_allocator *a = NULL;
	int tag = -1;
	void *p;

	memset(&lcfg, 0, sizeof(lcfg));
	lcfg.buf = u.buf;
	lcfg.size = sizeof(u.buf);

	/* create */
	a = fy_allocator_create("linear", &lcfg);
	ck_assert_ptr_ne(a, NULL);

	/* get the tag */
	tag = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag, -1);

	test_allocator_alignment(a, tag);

	/* allocate something too large to fit */
	p = fy_allocator_alloc(a, tag, sizeof(u.buf) + 1, 16);
	ck_assert_ptr_eq(p, NULL);

	/* destroy */
	fy_allocator_destroy(a);
}
END_TEST

START_TEST(allocator_linear_alloc)
{
	struct fy_linear_allocator_cfg lcfg;
	struct fy_allocator *a = NULL;
	int tag = -1;
	void *p;

	memset(&lcfg, 0, sizeof(lcfg));
	lcfg.size = 1024;

	/* create */
	a = fy_allocator_create("linear", &lcfg);
	ck_assert_ptr_ne(a, NULL);

	/* get the tag */
	tag = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag, -1);

	test_allocator_alignment(a, tag);

	/* allocate something too large to fit */
	p = fy_allocator_alloc(a, tag, 1024 + 1, 16);
	ck_assert_ptr_eq(p, NULL);

	/* destroy */
	fy_allocator_destroy(a);
}

START_TEST(allocator_malloc)
{
	struct fy_allocator *a = NULL;
	int tag0 = -1, tag1 = -1;

	/* create */
	a = fy_allocator_create("malloc", NULL);
	ck_assert_ptr_ne(a, NULL);

	/* get the first tag */
	tag0 = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag0, -1);

	/* get the second tag */
	tag1 = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag1, -1);

	/* tags must be different */
	ck_assert_int_ne(tag0, tag1);

	test_allocator_alignment(a, tag0);

	test_allocator_alignment(a, tag1);

	/* destroy */
	fy_allocator_destroy(a);
}

START_TEST(allocator_mremap)
{
	struct fy_allocator *a = NULL;
	int tag0 = -1, tag1 = -1;

	/* create (default everything) */
	a = fy_allocator_create("mremap", NULL);
	ck_assert_ptr_ne(a, NULL);

	/* get the first tag */
	tag0 = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag0, -1);

	/* get the second tag */
	tag1 = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag1, -1);

	/* tags must be different */
	ck_assert_int_ne(tag0, tag1);

	test_allocator_alignment(a, tag0);

	test_allocator_alignment(a, tag1);

	/* destroy */
	fy_allocator_destroy(a);
}

static inline bool
scenario_is_single_tagged(int scenario)
{
	return scenario >= FYAST_SINGLE_LINEAR_RANGE && 
	       scenario <= FYAST_SINGLE_LINEAR_RANGE_DEDUP;
}

static inline bool
scenario_is_dedup(int scenario)
{
	return scenario == FYAST_PER_TAG_FREE_DEDUP ||
	       scenario == FYAST_PER_OBJ_FREE_DEDUP ||
	       scenario == FYAST_SINGLE_LINEAR_RANGE_DEDUP;
}

static inline bool
scenario_is_fixed_size(int scenario)
{
	return scenario >= FYAST_SINGLE_LINEAR_RANGE && 
	       scenario <= FYAST_SINGLE_LINEAR_RANGE_DEDUP;
}

START_TEST(allocator_auto)
{
	static const struct {
		int scenario;
		const char *name;
	} auto_scenarios[] = {
		{
			.scenario = FYAST_PER_TAG_FREE,
			.name = "per-tag-free",
		}, {
			.scenario = FYAST_PER_TAG_FREE_DEDUP,
			.name = "per-tag-free-dedup",
		}, {
			.scenario = FYAST_PER_OBJ_FREE,
			.name = "per-obj-free",
		}, {
			.scenario = FYAST_PER_OBJ_FREE_DEDUP,
			.name = "per-obj-free-dedup",
		}, {
			.scenario = FYAST_SINGLE_LINEAR_RANGE,
			.name = "single-linear-range",
		}, {
			.scenario = FYAST_SINGLE_LINEAR_RANGE_DEDUP,
			.name = "single-linear-range-dedup",
		}
	};
	struct fy_auto_allocator_cfg acfg;
	struct fy_allocator *a = NULL;
	enum fy_auto_allocator_scenario_type sc;
	int tag0 = -1, tag1 = -1;
	unsigned int i;

	/* create (default everything) */
	for (i = 0; i < ARRAY_SIZE(auto_scenarios); i++) {

		printf("scenario #%d %s\n", i, auto_scenarios[i].name);
		sc = auto_scenarios[i].scenario;

		memset(&acfg, 0, sizeof(acfg));
		acfg.scenario = sc;

		/* for fixed sizes, make it 1MB */
		if (scenario_is_fixed_size(sc))
			acfg.estimated_max_size = 1 << 20;

		a = fy_allocator_create("auto", &acfg);
		ck_assert_ptr_ne(a, NULL);

		/* get the first tag */
		tag0 = fy_allocator_get_tag(a);
		ck_assert_int_ne(tag0, -1);

		if (!scenario_is_single_tagged(sc)) {
			/* get the second tag */
			tag1 = fy_allocator_get_tag(a);
			ck_assert_int_ne(tag1, -1);

			/* tags must be different */
			ck_assert_int_ne(tag0, tag1);
		}

		test_allocator_alignment(a, tag0);

		if (!scenario_is_single_tagged(sc))
			test_allocator_alignment(a, tag1);

		/* destroy */
		fy_allocator_destroy(a);
		a = NULL;
	}
}

TCase *libfyaml_case_allocator(void)
{
	TCase *tc;

	tc = tcase_create("allocator");

	tcase_add_test(tc, allocator_builtins);

	tcase_add_test(tc, allocator_linear_buf);
	tcase_add_test(tc, allocator_linear_alloc);
	tcase_add_test(tc, allocator_malloc);
	tcase_add_test(tc, allocator_mremap);
	tcase_add_test(tc, allocator_auto);

	return tc;
}
