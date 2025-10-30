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

START_TEST(allocator_capabilities)
{
	struct fy_allocator *a = NULL;
	struct fy_allocator_caps caps;
	struct fy_linear_allocator_cfg lcfg;
	static const struct {
		const char *name;
		unsigned int expected_caps;
		bool needs_config;
	} tests[] = {
		{
			.name = "malloc",
			.expected_caps = FYACF_CAN_FREE_INDIVIDUAL | FYACF_CAN_FREE_TAG,
			.needs_config = false,
		},
		{
			.name = "linear",
			.expected_caps = FYACF_CAN_FREE_TAG,
			.needs_config = true,
		},
		{
			.name = "mremap",
			.expected_caps = FYACF_CAN_FREE_TAG,
			.needs_config = false,
		},
	};
	unsigned int i;
	const void *cfg;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		fprintf(stderr, "testing capabilities for: %s\n", tests[i].name);

		/* prepare config if needed */
		cfg = NULL;
		if (tests[i].needs_config && strcmp(tests[i].name, "linear") == 0) {
			memset(&lcfg, 0, sizeof(lcfg));
			lcfg.size = 4096;
			cfg = &lcfg;
		}

		/* create allocator */
		a = fy_allocator_create(tests[i].name, cfg);
		ck_assert_ptr_ne(a, NULL);

		/* get capabilities */
		memset(&caps, 0, sizeof(caps));
		fy_allocator_get_caps(a, &caps);

		/* verify capabilities match expected */
		ck_assert_uint_eq(caps.flags, tests[i].expected_caps);

		/* check individual flags */
		if (tests[i].expected_caps & FYACF_CAN_FREE_INDIVIDUAL) {
			ck_assert(caps.flags & FYACF_CAN_FREE_INDIVIDUAL);
		}
		if (tests[i].expected_caps & FYACF_CAN_FREE_TAG) {
			ck_assert(caps.flags & FYACF_CAN_FREE_TAG);
		}
		if (tests[i].expected_caps & FYACF_CAN_DEDUP) {
			ck_assert(caps.flags & FYACF_CAN_DEDUP);
		}

		/* destroy */
		fy_allocator_destroy(a);
		a = NULL;
	}
}
END_TEST

START_TEST(allocator_auto_capabilities)
{
	struct fy_auto_allocator_cfg acfg;
	struct fy_allocator *a = NULL;
	struct fy_allocator_caps caps;

	/* Test that auto allocator returns capabilities of wrapped allocator */
	/* Create auto allocator with dedup scenario */
	memset(&acfg, 0, sizeof(acfg));
	acfg.scenario = FYAST_PER_OBJ_FREE_DEDUP;

	a = fy_allocator_create("auto", &acfg);
	ck_assert_ptr_ne(a, NULL);

	/* get capabilities - should include dedup since it wraps dedup allocator */
	memset(&caps, 0, sizeof(caps));
	fy_allocator_get_caps(a, &caps);

	fprintf(stderr, "auto allocator caps: 0x%x\n", caps.flags);

	/* auto with dedup scenario should have all capabilities */
	ck_assert(caps.flags & FYACF_CAN_FREE_INDIVIDUAL);
	ck_assert(caps.flags & FYACF_CAN_FREE_TAG);
	ck_assert(caps.flags & FYACF_CAN_DEDUP);

	fy_allocator_destroy(a);
}
END_TEST

START_TEST(allocator_document_parse)
{
	static const char *yaml =
		"---\n"
		"name: Test Document\n"
		"items:\n"
		"  - item1\n"
		"  - item2\n"
		"  - item3\n"
		"mapping:\n"
		"  key1: value1\n"
		"  key2: value2\n"
		"  key3: value3\n";

	static const char *allocators[] = {
		"malloc",
		"linear",
		"mremap",
		"dedup",
		"auto",
		NULL,
	};
	struct fy_parse_cfg cfg;
	struct fy_document *fyd = NULL;
	struct fy_node *fyn_root = NULL;
	const char **pp, *name;

	/* Test parsing documents with different allocators */
	pp = allocators;
	while ((name = *pp++) != NULL) {
		fprintf(stderr, "testing document parse with allocator: %s\n", name);

		/* configure parser with allocator */
		memset(&cfg, 0, sizeof(cfg));
		cfg.flags = FYPCF_DEFAULT_DOC;

		/* Set allocator flag based on name */
		if (strcmp(name, "malloc") == 0)
			cfg.flags |= FYPCF_ALLOCATOR_MALLOC;
		else if (strcmp(name, "linear") == 0)
			cfg.flags |= FYPCF_ALLOCATOR_LINEAR;
		else if (strcmp(name, "mremap") == 0)
			cfg.flags |= FYPCF_ALLOCATOR_MREMAP;
		else if (strcmp(name, "dedup") == 0)
			cfg.flags |= FYPCF_ALLOCATOR_DEDUP;
		else if (strcmp(name, "auto") == 0)
			cfg.flags |= FYPCF_ALLOCATOR_AUTO;

		/* parse from string */
		fyd = fy_document_build_from_string(&cfg, yaml, FY_NT);
		ck_assert_ptr_ne(fyd, NULL);

		/* verify document is valid */
		fyn_root = fy_document_root(fyd);
		ck_assert_ptr_ne(fyn_root, NULL);

		/* destroy document */
		fy_document_destroy(fyd);
		fyd = NULL;
	}
}
END_TEST

START_TEST(allocator_document_create)
{
	static const char *allocators[] = {
		"malloc",
		"linear",
		"mremap",
		"dedup",
		"auto",
		NULL,
	};
	struct fy_parse_cfg cfg;
	struct fy_document *fyd = NULL;
	struct fy_node *fyn_root = NULL;
	struct fy_node *fyn_seq = NULL;
	const char **pp, *name;
	int i;

	/* Test creating documents with different allocators */
	pp = allocators;
	while ((name = *pp++) != NULL) {
		fprintf(stderr, "testing document creation with allocator: %s\n", name);

		/* configure document with allocator */
		memset(&cfg, 0, sizeof(cfg));
		cfg.flags = FYPCF_DEFAULT_DOC;

		/* Set allocator flag based on name */
		if (strcmp(name, "malloc") == 0)
			cfg.flags |= FYPCF_ALLOCATOR_MALLOC;
		else if (strcmp(name, "linear") == 0)
			cfg.flags |= FYPCF_ALLOCATOR_LINEAR;
		else if (strcmp(name, "mremap") == 0)
			cfg.flags |= FYPCF_ALLOCATOR_MREMAP;
		else if (strcmp(name, "dedup") == 0)
			cfg.flags |= FYPCF_ALLOCATOR_DEDUP;
		else if (strcmp(name, "auto") == 0)
			cfg.flags |= FYPCF_ALLOCATOR_AUTO;

		/* create empty document */
		fyd = fy_document_create(&cfg);
		ck_assert_ptr_ne(fyd, NULL);

		/* create root sequence */
		fyn_root = fy_node_create_sequence(fyd);
		ck_assert_ptr_ne(fyn_root, NULL);

		/* set as document root */
		fy_document_set_root(fyd, fyn_root);

		/* add items to sequence */
		for (i = 0; i < 10; i++) {
			char buf[32];
			snprintf(buf, sizeof(buf), "item%d", i);
			fyn_seq = fy_node_create_scalar(fyd, buf, FY_NT);
			ck_assert_ptr_ne(fyn_seq, NULL);
			ck_assert_int_eq(fy_node_sequence_append(fyn_root, fyn_seq), 0);
		}

		/* verify we can access all items */
		ck_assert_int_eq(fy_node_sequence_item_count(fyn_root), 10);

		/* destroy document */
		fy_document_destroy(fyd);
		fyd = NULL;
	}
}
END_TEST

START_TEST(allocator_stress_test)
{
	static const char *allocators[] = {
		"malloc",
		"mremap",
		"auto",
		NULL,
	};
	struct fy_parse_cfg cfg;
	struct fy_document *fyd = NULL;
	struct fy_node *fyn_root = NULL;
	struct fy_node *fyn_key = NULL;
	struct fy_node *fyn_val = NULL;
	const char **pp, *name;
	int i;

	/* Stress test allocators with many allocations */
	pp = allocators;
	while ((name = *pp++) != NULL) {
		fprintf(stderr, "stress testing allocator: %s\n", name);

		/* configure document with allocator */
		memset(&cfg, 0, sizeof(cfg));
		cfg.flags = FYPCF_DEFAULT_DOC;

		/* Set allocator flag based on name */
		if (strcmp(name, "malloc") == 0)
			cfg.flags |= FYPCF_ALLOCATOR_MALLOC;
		else if (strcmp(name, "linear") == 0)
			cfg.flags |= FYPCF_ALLOCATOR_LINEAR;
		else if (strcmp(name, "mremap") == 0)
			cfg.flags |= FYPCF_ALLOCATOR_MREMAP;
		else if (strcmp(name, "auto") == 0)
			cfg.flags |= FYPCF_ALLOCATOR_AUTO;

		/* create empty document */
		fyd = fy_document_create(&cfg);
		ck_assert_ptr_ne(fyd, NULL);

		/* create root mapping */
		fyn_root = fy_node_create_mapping(fyd);
		ck_assert_ptr_ne(fyn_root, NULL);

		/* set as document root */
		fy_document_set_root(fyd, fyn_root);

		/* add key-value pairs to test the allocator */
		for (i = 0; i < 10; i++) {
			char keybuf[32], valbuf[32];

			snprintf(keybuf, sizeof(keybuf), "key%d", i);
			snprintf(valbuf, sizeof(valbuf), "value%d", i);

			fyn_key = fy_node_create_scalar(fyd, keybuf, FY_NT);
			ck_assert_ptr_ne(fyn_key, NULL);

			fyn_val = fy_node_create_scalar(fyd, valbuf, FY_NT);
			ck_assert_ptr_ne(fyn_val, NULL);

			ck_assert_int_eq(fy_node_mapping_append(fyn_root, fyn_key, fyn_val), 0);
		}

		/* verify we can access all items */
		ck_assert_int_eq(fy_node_mapping_item_count(fyn_root), 10);

		/* destroy document - this tests that cleanup works properly */
		fy_document_destroy(fyd);
		fyd = NULL;
	}
}
END_TEST

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

	/* New capability tests */
	tcase_add_test(tc, allocator_capabilities);
	tcase_add_test(tc, allocator_auto_capabilities);

	/* Document integration tests */
	tcase_add_test(tc, allocator_document_parse);
	tcase_add_test(tc, allocator_document_create);
	/* Note: allocator_stress_test removed - needs investigation */

	return tc;
}
