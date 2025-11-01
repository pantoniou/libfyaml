/*
 * libfyaml-test-thread.c - libfyaml threading tests
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

#include <stdatomic.h>

/* Test: Basic thread pool creation and destruction */
START_TEST(thread_pool_create_destroy)
{
	struct fy_thread_pool_cfg cfg;
	struct fy_thread_pool *tp;

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = 0;
	cfg.num_threads = 2;
	cfg.userdata = NULL;

	tp = fy_thread_pool_create(&cfg);
	ck_assert_ptr_ne(tp, NULL);

	/* Verify we can get the configuration */
	const struct fy_thread_pool_cfg *got_cfg = fy_thread_pool_get_cfg(tp);
	ck_assert_ptr_ne(got_cfg, NULL);
	ck_assert_int_eq(got_cfg->num_threads, 2);

	/* Verify we can get the number of threads */
	int num_threads = fy_thread_pool_get_num_threads(tp);
	ck_assert_int_eq(num_threads, 2);

	fy_thread_pool_destroy(tp);
}
END_TEST

/* Worker function that atomically increments a counter */
static void atomic_increment_worker(void *arg)
{
	_Atomic(int) *p = arg;
	int v, exp_v;

	/* Atomically increase the counter */
	v = atomic_load(p);
	for (;;) {
		exp_v = v;
		if (atomic_compare_exchange_strong(p, &exp_v, v + 1))
			return;
		v = exp_v;
	}
}

/* Test: Thread reserve, submit work, wait, unreserve */
START_TEST(thread_reserve_submit_wait)
{
	struct fy_thread_pool_cfg cfg;
	struct fy_thread_pool *tp;
	struct fy_thread *threads[4];
	struct fy_thread_work works[4];
	_Atomic(int) counter = 0;
	unsigned int i;

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = 0;
	cfg.num_threads = 4;
	cfg.userdata = NULL;

	tp = fy_thread_pool_create(&cfg);
	ck_assert_ptr_ne(tp, NULL);

	/* Reserve all threads */
	for (i = 0; i < 4; i++) {
		threads[i] = fy_thread_reserve(tp);
		ck_assert_ptr_ne(threads[i], NULL);
	}

	/* Submit work to all threads */
	for (i = 0; i < 4; i++) {
		works[i].fn = atomic_increment_worker;
		works[i].arg = &counter;
		fy_thread_submit_work(threads[i], &works[i]);
	}

	/* Wait for all threads to complete */
	for (i = 0; i < 4; i++) {
		fy_thread_wait_work(threads[i]);
	}

	/* Verify counter was incremented 4 times */
	ck_assert_int_eq(atomic_load(&counter), 4);

	/* Unreserve all threads */
	for (i = 0; i < 4; i++) {
		fy_thread_unreserve(threads[i]);
	}

	fy_thread_pool_destroy(tp);
}
END_TEST

/* Test: Thread arg join */
START_TEST(thread_arg_join)
{
	struct fy_thread_pool_cfg cfg;
	struct fy_thread_pool *tp;
	_Atomic(int) counter = 0;
	unsigned int num_tasks = 8;

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = 0;
	cfg.num_threads = 4;
	cfg.userdata = NULL;

	tp = fy_thread_pool_create(&cfg);
	ck_assert_ptr_ne(tp, NULL);

	/* Use arg_join to execute the same function multiple times */
	fy_thread_arg_join(tp, atomic_increment_worker, NULL, &counter, num_tasks);

	/* Verify counter was incremented num_tasks times */
	ck_assert_int_eq(atomic_load(&counter), (int)num_tasks);

	fy_thread_pool_destroy(tp);
}
END_TEST

/* Worker function for sum test */
struct sum_arg {
	const int *values;
	int count;
	int result;
};

static void sum_worker(void *arg)
{
	struct sum_arg *s = arg;
	int i;
	int sum = 0;

	for (i = 0; i < s->count; i++) {
		sum += s->values[i];
	}
	s->result = sum;
}

/* Test: Thread array join with different arguments */
START_TEST(thread_arg_array_join)
{
	struct fy_thread_pool_cfg cfg;
	struct fy_thread_pool *tp;
	int values[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	struct sum_arg args[2];
	int total_sum;

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = 0;
	cfg.num_threads = 2;
	cfg.userdata = NULL;

	tp = fy_thread_pool_create(&cfg);
	ck_assert_ptr_ne(tp, NULL);

	/* Split the work into two tasks */
	args[0].values = values;
	args[0].count = 5;
	args[0].result = 0;

	args[1].values = values + 5;
	args[1].count = 5;
	args[1].result = 0;

	/* Execute both tasks in parallel */
	fy_thread_arg_array_join(tp, sum_worker, NULL, args, sizeof(struct sum_arg), 2);

	/* Verify results */
	total_sum = args[0].result + args[1].result;
	ck_assert_int_eq(total_sum, 55);  /* 1+2+3+...+10 = 55 */
	ck_assert_int_eq(args[0].result, 15);  /* 1+2+3+4+5 = 15 */
	ck_assert_int_eq(args[1].result, 40);  /* 6+7+8+9+10 = 40 */

	fy_thread_pool_destroy(tp);
}
END_TEST

/* Worker function for steal mode test */
static void steal_mode_worker(void *arg)
{
	_Atomic(int) *p = arg;
	int v, exp_v;
	int i;

	/* Increment counter 100 times */
	for (i = 0; i < 100; i++) {
		v = atomic_load(p);
		for (;;) {
			exp_v = v;
			if (atomic_compare_exchange_strong(p, &exp_v, v + 1))
				break;
			v = exp_v;
		}
	}
}

/* Test: Work stealing mode */
START_TEST(thread_steal_mode)
{
	struct fy_thread_pool_cfg cfg;
	struct fy_thread_pool *tp;
	_Atomic(int) counter = 0;
	unsigned int num_tasks = 16;  /* More tasks than threads */

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = FYTPCF_STEAL_MODE;
	cfg.num_threads = 4;
	cfg.userdata = NULL;

	tp = fy_thread_pool_create(&cfg);
	ck_assert_ptr_ne(tp, NULL);

	/* Execute many tasks with work stealing enabled */
	fy_thread_arg_join(tp, steal_mode_worker, NULL, &counter, num_tasks);

	/* Verify all tasks completed: 16 tasks * 100 increments each */
	ck_assert_int_eq(atomic_load(&counter), (int)(num_tasks * 100));

	fy_thread_pool_destroy(tp);
}
END_TEST

TCase *libfyaml_case_thread(void)
{
	TCase *tc;

	tc = tcase_create("thread");

	/* Basic thread pool tests */
	tcase_add_test(tc, thread_pool_create_destroy);

	/* Thread work submission tests */
	tcase_add_test(tc, thread_reserve_submit_wait);

	/* Join API tests */
	tcase_add_test(tc, thread_arg_join);
	tcase_add_test(tc, thread_arg_array_join);

	/* Work stealing tests */
	tcase_add_test(tc, thread_steal_mode);

	return tc;
}
