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
#include <limits.h>
#include <stdint.h>

#include <check.h>

#include <libfyaml.h>

#include "fy-check.h"

#include "fy-allocator.h"
#include "fy-allocator-dedup.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) ((sizeof(x)/sizeof((x)[0])))
#endif

static const char *builtin_allocators[] = {
	"linear",
	"malloc",
	"mremap",
	"dedup",
	"auto",
	NULL,
};

START_TEST(allocator_builtins)
{
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
END_TEST

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
END_TEST

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
END_TEST

static inline bool
scenario_is_single_tagged(int scenario)
{
	return scenario >= FYAST_SINGLE_LINEAR_RANGE &&
	       scenario <= FYAST_SINGLE_LINEAR_RANGE_DEDUP;
}

static inline FY_UNUSED bool
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
END_TEST

START_TEST(allocator_linear_inplace)
{
	struct fy_allocator *a;
	char buf[FY_LINEAR_ALLOCATOR_IN_PLACE_MIN_SIZE];
	int tag;
	const void *p;

	a = fy_linear_allocator_create_in_place(buf, sizeof(buf));
	ck_assert_ptr_ne(a, NULL);

	tag = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag, -1);

	p = fy_allocator_store(a, tag, "Hello", 6, 1);
	ck_assert_ptr_ne(p, NULL);

	ck_assert(!strcmp(p, "Hello"));

	/* no release */
}
END_TEST

START_TEST(allocator_dedup_inplace)
{
	struct fy_allocator *a;
	char buf[FY_DEDUP_ALLOCATOR_IN_PLACE_MIN_SIZE];
	int tag;
	const void *p1, *p2;

	a = fy_dedup_allocator_create_in_place(buf, sizeof(buf));
	ck_assert_ptr_ne(a, NULL);

	tag = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag, -1);

	p1 = fy_allocator_store(a, tag, "Hello", 6, 1);
	ck_assert_ptr_ne(p1, NULL);

	ck_assert(!strcmp(p1, "Hello"));

	p2 = fy_allocator_store(a, tag, "Hello", 6, 1);
	ck_assert_ptr_ne(p2, NULL);

	ck_assert(!strcmp(p2, "Hello"));

	/* dedup must return the same pointer */
	ck_assert(p1 == p2);

	/* no release */
}
END_TEST

#define DCS_NTHREADS	16
#define DCS_NPAYLOAD	8000

struct dcs_job {
	struct fy_allocator *a;
	int tag;
	char (*payloads)[48];
	const void **canon;	/* this thread's returned pointer per payload */
	int fail;
};

static void dcs_worker(void *arg)
{
	struct dcs_job *j = arg;
	int i;

	for (i = 0; i < DCS_NPAYLOAD; i++) {
		size_t len = strlen(j->payloads[i]) + 1;
		const void *p = fy_allocator_store(j->a, j->tag, j->payloads[i], len, 16);

		if (!p) {
			j->fail = 1;
			return;
		}
		/* the stored bytes must match what we asked for */
		if (memcmp(p, j->payloads[i], len) != 0) {
			j->fail = 2;
			return;
		}
		j->canon[i] = p;
	}
}

START_TEST(allocator_dedup_concurrent_store)
{
	struct fy_dedup_allocator_cfg dcfg;
	struct fy_allocator *base, *d;
	struct fy_thread_pool_cfg tpcfg;
	struct fy_thread_pool *tp;
	struct fy_thread *th[DCS_NTHREADS];
	struct fy_thread_work work[DCS_NTHREADS];
	struct dcs_job jobs[DCS_NTHREADS];
	static char payloads[DCS_NPAYLOAD][48];
	static const void *canon[DCS_NTHREADS][DCS_NPAYLOAD];
	const void *ref[DCS_NPAYLOAD];
	int k, i, tag;

	base = fy_allocator_create("mremap", NULL);
	ck_assert_ptr_ne(base, NULL);

	memset(&dcfg, 0, sizeof(dcfg));
	dcfg.parent_allocator = base;
	dcfg.dedup_threshold = 8;

	d = fy_allocator_create("dedup", &dcfg);
	ck_assert_ptr_ne(d, NULL);

	tag = fy_allocator_get_tag(d);
	ck_assert_int_ne(tag, -1);

	for (i = 0; i < DCS_NPAYLOAD; i++)
		snprintf(payloads[i], sizeof(payloads[i]),
			 "dedup-concurrent-payload-%010d", i);

	memset(&tpcfg, 0, sizeof(tpcfg));
	tpcfg.num_threads = DCS_NTHREADS;
	tp = fy_thread_pool_create(&tpcfg);
	ck_assert_ptr_ne(tp, NULL);

	for (k = 0; k < DCS_NTHREADS; k++) {
		jobs[k].a = d;
		jobs[k].tag = tag;
		jobs[k].payloads = payloads;
		jobs[k].canon = canon[k];
		jobs[k].fail = 0;

		th[k] = fy_thread_reserve(tp);
		ck_assert_ptr_ne(th[k], NULL);

		work[k].fn = dcs_worker;
		work[k].arg = &jobs[k];
		work[k].wp = NULL;
		ck_assert_int_eq(fy_thread_submit_work(th[k], &work[k]), 0);
	}
	for (k = 0; k < DCS_NTHREADS; k++) {
		ck_assert_int_eq(fy_thread_wait_work(th[k]), 0);
		fy_thread_unreserve(th[k]);
	}

	fy_thread_pool_destroy(tp);

	/* no thread hit a NULL store (the crash) or a content mismatch */
	for (k = 0; k < DCS_NTHREADS; k++)
		ck_assert_int_eq(jobs[k].fail, 0);

	/*
	 * canonical identity: every thread that stored a given payload must have
	 * received the identical pointer, even under the insert race.
	 */
	int bad = 0;
	for (i = 0; i < DCS_NPAYLOAD; i++) {
		ref[i] = canon[0][i];
		if (!ref[i])
			bad++;
		for (k = 1; k < DCS_NTHREADS; k++)
			if (canon[k][i] != ref[i])
				bad++;
	}
	ck_assert_int_eq(bad, 0);

	/* distinct payloads -> distinct pointers, and a serial re-store still
	 * dedups to the same canonical pointer */
	bad = 0;
	for (i = 1; i < DCS_NPAYLOAD; i++)
		if (ref[i] == ref[i - 1])
			bad++;
	ck_assert_int_eq(bad, 0);

	bad = 0;
	for (i = 0; i < DCS_NPAYLOAD; i++) {
		size_t len = strlen(payloads[i]) + 1;
		const void *p = fy_allocator_store(d, tag, payloads[i], len, 16);
		if (p != ref[i])
			bad++;
	}
	ck_assert_int_eq(bad, 0);

	fy_allocator_destroy(d);
	fy_allocator_destroy(base);
}
END_TEST

void libfyaml_case_allocator(struct fy_check_suite *cs)
{
	struct fy_check_testcase *ctc;

	ctc = fy_check_suite_add_test_case(cs, "allocator");

	fy_check_testcase_add_test(ctc, allocator_builtins);
	fy_check_testcase_add_test(ctc, allocator_linear_buf);
	fy_check_testcase_add_test(ctc, allocator_linear_alloc);
	fy_check_testcase_add_test(ctc, allocator_malloc);
	fy_check_testcase_add_test(ctc, allocator_mremap);
	fy_check_testcase_add_test(ctc, allocator_auto);
	fy_check_testcase_add_test(ctc, allocator_linear_inplace);
	fy_check_testcase_add_test(ctc, allocator_dedup_inplace);
	fy_check_testcase_add_test(ctc, allocator_dedup_concurrent_store);
}
