/*
 * fy-check.h - Wrapper over check to make it better
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_CHECK_H
#define FY_CHECK_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <check.h>

#include "fy-typelist.h"
#include "fy-types.h"

struct fy_check_test;
struct fy_check_testcase;
struct fy_check_suite;
struct fy_check_runner;

FY_TYPE_FWD_DECL_LIST(check_test);
struct fy_check_test {
	struct list_head node;
	struct fy_check_testcase *testcase;
	const struct TTest *test;
	char *name;
};
FY_TYPE_DECL_LIST(check_test);

FY_TYPE_FWD_DECL_LIST(check_testcase);
struct fy_check_testcase {
	struct list_head node;
	struct fy_check_suite *suite;
	struct TCase *testcase;
	char *name;
	struct fy_check_test_list tests;
};
FY_TYPE_DECL_LIST(check_testcase);

struct fy_check_suite {
	struct fy_check_runner *runner;
	char *name;
	struct Suite *suite;
	struct fy_check_testcase_list testcases;
	int argc;
	char **argv;
};

struct fy_check_runner {
	struct fy_check_suite *suite;
	SRunner *runner;
};

static inline void *
fy_check_malloc(size_t size)
{
	void *ptr;

	ptr = malloc(size);
	assert(ptr);
	if (!ptr)
		abort();
	memset(ptr, 0, size);
	return ptr;
}

static inline void
fy_check_free(void *ptr)
{
	if (ptr)
		free(ptr);
}

static inline char *
fy_check_strdup(const char *str)
{
	char *copy;

	copy = strdup(str);
	assert(copy);
	if (!copy)
		abort();
	return copy;
}

static inline void
fy_check_test_destroy(struct fy_check_test *ct)
{
	if (!ct)
		return;
	fy_check_free(ct->name);
	fy_check_free(ct);
}

static inline struct fy_check_test *
fy_check_test_create(struct fy_check_testcase *ctc, const struct TTest *test)
{
	struct fy_check_test *ct = fy_check_malloc(sizeof(*ct));
	ct->testcase = ctc;
	ct->test = test;
	ct->name = fy_check_strdup(test->name);
	return ct;
}

static inline struct fy_check_test *
fy_check_testcase_add_test(struct fy_check_testcase *ctc, const struct TTest *test)
{
	struct fy_check_test *ct;
	struct fy_check_suite *cs;
	int i;

	if (!ctc)
		return NULL;

	/* filter out tests */
	cs = ctc->suite;
	if (cs->argc > 0) {
		for (i = 0; i < cs->argc; i++) {
			if (!strcmp(test->name, cs->argv[i]))
				break;
		}
		if (i >= cs->argc)
			return NULL;
	}

	ct = fy_check_test_create(ctc, test);
	fy_check_test_list_add_tail(&ctc->tests, ct);
	tcase_add_test(ctc->testcase, ct->test);
	return ct;
}

static inline void
fy_check_testcase_destroy(struct fy_check_testcase *ctc)
{
	struct fy_check_test *ct;

	if (!ctc)
		return;

	while ((ct = fy_check_test_list_pop(&ctc->tests)) != NULL)
		fy_check_test_destroy(ct);
	fy_check_free(ctc->name);
	fy_check_free(ctc);
}

static inline struct fy_check_testcase *
fy_check_testcase_create(struct fy_check_suite *cs, const char *name)
{
	struct fy_check_testcase *ctc = fy_check_malloc(sizeof(*ctc));
	ctc->suite = cs;
	ctc->name = fy_check_strdup(name);
	ctc->testcase = tcase_create(ctc->name);
	assert(ctc->testcase);
	fy_check_test_list_init(&ctc->tests);

	return ctc;
}

static inline TCase *
fy_check_testcase_get_TCase(struct fy_check_testcase *ctc)
{
	return ctc ? ctc->testcase : NULL;
}

static inline void
fy_check_suite_destroy(struct fy_check_suite *cs)
{
	struct fy_check_testcase *tc;

	if (!cs)
		return;

	while ((tc = fy_check_testcase_list_pop(&cs->testcases)) != NULL)
		fy_check_testcase_destroy(tc);
	fy_check_free(cs->name);
	fy_check_free(cs);
}

static inline struct fy_check_suite *
fy_check_suite_create(const char *name, int argc, char *argv[])
{
	struct fy_check_suite *cs = fy_check_malloc(sizeof(*cs));

	cs->name = fy_check_strdup(name);
	cs->suite = suite_create(cs->name);
	assert(cs->suite);
	cs->runner = NULL;
	fy_check_testcase_list_init(&cs->testcases);
	if (argc > 0) {
		cs->argc = argc;
		cs->argv = argv;
	}
	return cs;
}

static inline struct fy_check_testcase *
fy_check_suite_add_test_case(struct fy_check_suite *cs, const char *name)
{
	struct fy_check_testcase *ctc;
	if (!cs)
		return NULL;

	ctc = fy_check_testcase_create(cs, name);
	fy_check_testcase_list_add_tail(&cs->testcases, ctc);
	suite_add_tcase(cs->suite, ctc->testcase);
	return ctc;
}

static inline void
fy_check_runner_destroy(struct fy_check_runner *cr)
{
	if (!cr)
		return;

	fy_check_suite_destroy(cr->suite);
	if (cr->runner)
		srunner_free(cr->runner);
	fy_check_free(cr);
}

static inline struct fy_check_runner *
fy_check_runner_create(struct fy_check_suite *cs)
{
	struct fy_check_runner *cr = fy_check_malloc(sizeof(*cr));
	cr->suite = cs;
	cr->runner = srunner_create(cr->suite->suite);
	assert(cr->runner);
	cs->runner = cr;
	return cr;
}

static inline SRunner *
fy_check_runner_get_SRunner(struct fy_check_runner *cr)
{
	if (!cr)
		return NULL;
	return cr->runner;
}

static inline Suite *
fy_check_runner_get_Suite(struct fy_check_runner *cr)
{
	if (!cr || !cr->suite)
		return NULL;
	return cr->suite->suite;
}

static inline struct fy_check_testcase *
fy_check_suite_get_testcase_by_name(struct fy_check_suite *cs, const char *name)
{
	struct fy_check_testcase *ctc;

	if (!cs || !name)
		return NULL;

	for (ctc = fy_check_testcase_list_head(&cs->testcases); ctc;
	     ctc = fy_check_testcase_next(&cs->testcases, ctc)) {
		if (!strcmp(ctc->name, name))
			return ctc;
	}

	return NULL;
}

static inline struct fy_check_test *
fy_check_suite_get_test_by_name(struct fy_check_suite *cs, const char *name)
{
	struct fy_check_testcase *ctc;
	struct fy_check_test *ct;

	if (!cs || !name)
		return NULL;

	for (ctc = fy_check_testcase_list_head(&cs->testcases); ctc;
	     ctc = fy_check_testcase_next(&cs->testcases, ctc)) {
		for (ct = fy_check_test_list_head(&ctc->tests); ct;
		     ct = fy_check_test_next(&ctc->tests, ct)) {
			if (!strcmp(ct->name, name))
				return ct;
		}
	}

	return NULL;
}

#endif
