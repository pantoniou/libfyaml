/*
 * fy-thread.c - thread testing internal utility
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <alloca.h>
#include <stdbool.h>
#include <getopt.h>
#include <ctype.h>
#include <assert.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>

#include <stdatomic.h>

#include <libfyaml.h>

#include "fy-thread.h"

static void test_worker_thread_fn(void *arg)
{
	_Atomic(int) *p = arg;
	int v, exp_v;

	/* atomically increase the counter */
	v = atomic_load(p);
	for (;;) {
		exp_v = v;
		if (atomic_compare_exchange_strong(p, &exp_v, v + 1))
			return;
		v = exp_v;
	}
}

void test_worker_threads(unsigned int num_threads)
{
	struct fy_thread_pool_cfg tp_cfg;
	struct fy_thread_pool *tp;
	struct fy_thread **threads, *t;
	struct fy_thread_work *works;
	long scval;
	unsigned int i, count, num_cpus;
	int rc, test_count;

	(void)rc;

	if (num_threads == 0) {
		scval = sysconf(_SC_NPROCESSORS_ONLN);
		assert(scval > 0);
		num_cpus = (unsigned int)scval;
	} else
		num_cpus = num_threads;

	memset(&tp_cfg, 0, sizeof(tp_cfg));
	tp_cfg.flags = 0;
	tp_cfg.num_threads = num_cpus;
	tp_cfg.userdata = NULL;

	fprintf(stderr, "calling: fy_thread_pool_create()\n");

	tp = fy_thread_pool_create(&tp_cfg);
	assert(tp);

	count = tp->num_threads;
	threads = alloca(count * sizeof(*threads));
	works = alloca(count * sizeof(*works));

	test_count = 0;

	for (i = 0; i < count; i++) {
		fprintf(stderr, "calling: fy_thread_reserve(#%u)\n", i);
		threads[i] = fy_thread_reserve(tp);
		assert(threads[i]);
		t = threads[i];
		assert(t->id == i);
	}

	for (i = 0; i < count; i++) {
		t = threads[i];
		fprintf(stderr, "calling: fy_thread_submit_work(#%u)\n", i);
		works[i].fn = test_worker_thread_fn;
		works[i].arg = &test_count;
		fy_thread_submit_work(t, &works[i]);
	}

	for (i = 0; i < count; i++) {
		t = threads[i];
		fprintf(stderr, "calling: fy_thread_wait_work(#%u)\n", i);
		fy_thread_wait_work(t);
	}

	fprintf(stderr, "%s: test_count=%d\n", __func__, test_count);
	if (test_count != (int)num_cpus) {
		fprintf(stderr, "error: test_count=%d expected %d\n", test_count, (int)num_cpus);
		abort();
	}

	for (i = 0; i < count; i++) {
		t = threads[i];
		fprintf(stderr, "calling: fy_thread_pool_unreserve(#%u)\n", i);
		fy_thread_unreserve(t);
	}

	fprintf(stderr, "calling: fy_thread_pool_destroy()\n");
	fy_thread_pool_destroy(tp);
}

void test_thread_join(unsigned int num_threads)
{
	struct fy_thread_pool_cfg tp_cfg;
	struct fy_thread_pool *tp;
	void **args;
	long scval;
	unsigned int count, num_cpus;
	int rc, test_count;

	(void)rc;

	if (num_threads == 0) {
		scval = sysconf(_SC_NPROCESSORS_ONLN);
		assert(scval > 0);
		num_cpus = (unsigned int)scval;
	} else
		num_cpus = num_threads;

	memset(&tp_cfg, 0, sizeof(tp_cfg));
	tp_cfg.flags = 0;
	tp_cfg.num_threads = num_cpus;
	tp_cfg.userdata = NULL;

	fprintf(stderr, "calling: fy_thread_pool_create()\n");
	tp = fy_thread_pool_create(&tp_cfg);
	assert(tp);

	count = tp->num_threads;
	args = alloca(count * sizeof(*args));

	test_count = 0;

	fy_thread_arg_join(tp, test_worker_thread_fn, NULL, &test_count, count);

	fprintf(stderr, "%s: test_count=%d\n", __func__, test_count);
	assert(test_count == (int)num_cpus);

	fprintf(stderr, "calling: fy_thread_pool_destroy()\n");
	fy_thread_pool_destroy(tp);
}

struct thread_latency_state {
	struct timespec reserve;
	struct timespec reserve_done;
	struct timespec submit;
	struct timespec execute;
	struct timespec wait;
	struct timespec wait_done;
	struct timespec unreserve;
	struct timespec unreserve_done;
};

long long delta_ns(struct timespec before, struct timespec after)
{
	if ((before.tv_sec == 0 && before.tv_nsec == 0) ||
	    (after.tv_sec == 0 && after.tv_nsec == 0))
		return -1;
	return (long long)((int64_t)(after.tv_sec - before.tv_sec) * (int64_t)1000000000UL + (int64_t)(after.tv_nsec - before.tv_nsec));
}

static void test_latency_worker_thread_fn(void *arg)
{
	struct thread_latency_state *s = arg;

	clock_gettime(CLOCK_MONOTONIC, &s->execute);
}

void test_thread_latency(unsigned int num_threads)
{
	struct fy_thread_pool_cfg tp_cfg;
	struct fy_thread_pool *tp;
	struct fy_thread **threads, *t;
	struct fy_thread_work *works;
	long scval;
	unsigned int i, count, num_cpus;
	int rc;
	struct thread_latency_state *states, *s;

	(void)rc;

	if (num_threads == 0) {
		scval = sysconf(_SC_NPROCESSORS_ONLN);
		assert(scval > 0);
		num_cpus = (unsigned int)scval;
	} else
		num_cpus = num_threads;

	memset(&tp_cfg, 0, sizeof(tp_cfg));
	tp_cfg.flags = 0;
	tp_cfg.num_threads = num_cpus;
	tp_cfg.userdata = NULL;

	tp = fy_thread_pool_create(&tp_cfg);
	assert(tp);


	count = tp->num_threads;
	threads = alloca(count * sizeof(*threads));
	works = alloca(count * sizeof(*works));
	states = alloca(count * sizeof(*states));

	memset(states, 0, count * sizeof(*states));

	for (i = 0; i < count; i++) {
		s = &states[i];

		clock_gettime(CLOCK_MONOTONIC, &s->reserve);

		threads[i] = fy_thread_reserve(tp);
		assert(threads[i]);
		t = threads[i];
		assert(t->id == i);

		clock_gettime(CLOCK_MONOTONIC, &s->reserve_done);

	}

	for (i = 0; i < count; i++) {
		s = &states[i];
		clock_gettime(CLOCK_MONOTONIC, &s->submit);

		t = threads[i];
		works[i].fn = test_latency_worker_thread_fn;
		works[i].arg = s;
		fy_thread_submit_work(t, &works[i]);
	}

	for (i = 0; i < count; i++) {
		s = &states[i];
		clock_gettime(CLOCK_MONOTONIC, &s->wait);

		t = threads[i];
		fy_thread_wait_work(t);

		clock_gettime(CLOCK_MONOTONIC, &s->wait_done);
	}

	for (i = 0; i < count; i++) {

		s = &states[i];
		clock_gettime(CLOCK_MONOTONIC, &s->unreserve);

		t = threads[i];
		fy_thread_unreserve(t);

		clock_gettime(CLOCK_MONOTONIC, &s->unreserve_done);
	}

	fy_thread_pool_destroy(tp);

	fprintf(stderr, "latency results\n");
	for (i = 0; i < count; i++) {
		s = &states[i];

		fprintf(stderr, "#%2u: reserve:%10lld submit-execute:%10lld execute-waitdone:%10lld wait:%10lld unreserve:%10lld\n", i,
				delta_ns(s->reserve, s->reserve_done),
				delta_ns(s->submit, s->execute),
				delta_ns(s->execute, s->wait_done),
				delta_ns(s->wait, s->wait_done),
				delta_ns(s->unreserve, s->unreserve_done));
	}
}

// #define STEAL_LOOP_COUNT 100000000
#define STEAL_LOOP_COUNT 10000
static void test_worker_thread_steal_fn(void *arg)
{
	_Atomic(int) *p = arg;
	int v, exp_v;
	unsigned int i;

	/* atomically increase the counter STEAL_LOOP_COUNT times */
	for (i = 0; i < STEAL_LOOP_COUNT; i++) {
		v = atomic_load(p);
		for (;;) {
			exp_v = v;
			if (atomic_compare_exchange_strong(p, &exp_v, v + 1))
				break;
			v = exp_v;
		}
	}
}

void test_thread_join_steal(unsigned int num_threads)
{
	struct fy_thread_pool_cfg tp_cfg;
	struct fy_thread_pool *tp;
	void **args;
	long scval;
	unsigned int count, num_cpus;
	int rc, test_count;

	(void)rc;

	if (num_threads == 0) {
		scval = sysconf(_SC_NPROCESSORS_ONLN);
		assert(scval > 0);
		num_cpus = (unsigned int)scval;
	} else
		num_cpus = num_threads;


	tp_cfg.flags = FYTPCF_STEAL_MODE;
	tp_cfg.num_threads = num_cpus;
	tp_cfg.userdata = NULL;

	fprintf(stderr, "calling: fy_thread_pool_create()\n");
	tp = fy_thread_pool_create(&tp_cfg);
	assert(tp);

	count = tp->num_threads * 4;
	args = alloca(count * sizeof(*args));

	test_count = 0;

	fy_thread_arg_join(tp, test_worker_thread_steal_fn, NULL, &test_count, count);

	fprintf(stderr, "%s: test_count=%d\n", __func__, test_count);
	if (test_count != (int)num_cpus * STEAL_LOOP_COUNT * 4) {
		fprintf(stderr, "error: test_count=%d expected %d\n", test_count, (int)num_cpus * STEAL_LOOP_COUNT);
		abort();
	}

	fprintf(stderr, "calling: fy_thread_pool_destroy()\n");
	fy_thread_pool_destroy(tp);
}

struct sum_args {
	struct fy_thread_pool *tp;
	const uint8_t *values_start;
	unsigned int count_start;
	const uint8_t *values;
	unsigned int count;
	uint64_t sum;
};

static uint64_t calc_sum(const uint8_t *values, unsigned int count)
{
	unsigned int i;
	uint64_t sum = 0;

	for (i = 0; i < count; i++)
		sum += values[i];

	return sum;
}

static void test_worker_thread_sum_fn(void *arg)
{
	struct sum_args *s = arg;
	struct sum_args args[2];
	uint64_t sum;
	unsigned int pos;

	pos = s->values - s->values_start;
	assert(pos <= s->count_start);
	assert(pos + s->count <= s->count_start);

	(void)pos;

	// if (s->count <= (1 << 20) / 8) {
	if (s->count <= 4096) {
		// fprintf(stderr, "S<%06x-%06x>\n", pos, pos + s->count - 1);
		sum = calc_sum(s->values, s->count);
	} else {
		memset(args, 0, sizeof(args));
		args[0].tp = args[1].tp = s->tp;
		args[0].values_start = args[1].values_start = s->values_start;
		args[0].count_start = args[1].count_start = s->count_start;
		args[0].sum = args[1].sum = 0;
		args[0].values = s->values;
		args[0].count = s->count / 2;
		args[1].values = s->values + args[0].count;
		args[1].count = s->count - args[0].count;

		// fprintf(stderr, "M<%06x-%06x,%06x-%06x>\n",
		//		pos,
		//		pos + args[0].count - 1,
		//		pos + args[0].count,
		//		pos + args[0].count + args[1].count - 1);

		fy_thread_arg_array_join(s->tp, test_worker_thread_sum_fn, NULL, &args, sizeof(args[0]), sizeof(args)/sizeof(args[0]));
		sum = args[0].sum + args[1].sum;
	}
	s->sum = sum;
}

void test_thread_join_sum(unsigned int num_threads, unsigned int count, bool steal_mode, unsigned int times)
{
	struct fy_thread_pool_cfg tp_cfg;
	struct fy_thread_pool *tp;
	struct timespec before, after;
	unsigned int i, num_cpus;
	long scval;
	uint8_t *values;
	int rc;
	uint64_t sum_single, sum_multi;
	struct sum_args args[2];
	long long table_multi[times];
	long long ns;

	(void)rc;

	fprintf(stderr, "**********************************************************************\n");
	fprintf(stderr, "%s: steal_mode=%s\n", __func__, steal_mode ? "true" : "false");

	values = malloc(count * sizeof(*values));
	assert(values);

	clock_gettime(CLOCK_MONOTONIC, &before);
	for (i = 0; i < count; i++)
		values[i] = (uint8_t)rand();

	clock_gettime(CLOCK_MONOTONIC, &after);
	fprintf(stderr, "%s: seeding done in %lldus\n", __func__, delta_ns(before, after) / 1000);

	clock_gettime(CLOCK_MONOTONIC, &before);
	sum_single = calc_sum(values, count);
	clock_gettime(CLOCK_MONOTONIC, &after);
	ns = delta_ns(before, after);
	fprintf(stderr, "%s: calculated sum=%"PRIu64" (single threaded) done in %lldus\n", __func__, sum_single,  ns / 1000);

	if (num_threads == 0) {
		scval = sysconf(_SC_NPROCESSORS_ONLN);
		assert(scval > 0);
		num_cpus = (unsigned int)scval;
	} else
		num_cpus = num_threads;

	memset(&tp_cfg, 0, sizeof(tp_cfg));
	tp_cfg.flags = steal_mode ? FYTPCF_STEAL_MODE : 0;
	tp_cfg.num_threads = num_cpus;
	tp_cfg.userdata = NULL;

	tp = fy_thread_pool_create(&tp_cfg);
	assert(tp);

	fprintf(stderr, "%s: calculating (multi threaded) -", __func__);
	for (i = 0; i < times; i++) {
		clock_gettime(CLOCK_MONOTONIC, &before);

		memset(args, 0, sizeof(args));
		args[0].tp = args[1].tp = tp;
		args[0].values_start = args[1].values_start = values;
		args[0].count_start = args[1].count_start = count;
		args[0].sum = args[1].sum = 0;

		args[0].values = values;
		args[0].count = count / 2;
		args[1].values = values + args[0].count;
		args[1].count = count - args[0].count;

		// fprintf(stderr, "M<%06x-%06x,%06x-%06x>\n",
		//		0,
		//		args[0].count - 1,
		//		args[0].count,
		//		args[0].count + args[1].count - 1);

		fy_thread_arg_array_join(tp, test_worker_thread_sum_fn, NULL, &args, sizeof(args[0]), sizeof(args)/sizeof(args[0]));

		sum_multi = args[0].sum + args[1].sum;
		if (sum_multi != sum_single) {
			fprintf(stderr, "\nFailed sum_multi %"PRIu64" should be %"PRIu64"\n", sum_multi, sum_single);
			abort();
		}

		clock_gettime(CLOCK_MONOTONIC, &after);
		ns = delta_ns(before, after);
		fprintf(stderr, " %lldus", ns / 1000);
		fflush(stderr);
		table_multi[i] = ns;
	}
	ns = 0;
	for (i = 0; i < times; i++)
		ns += table_multi[i];
	ns /= times;
	fprintf(stderr, " : average %lldus\n", ns / 1000);

	fy_thread_pool_destroy(tp);

	free(values);
}

int thread_test(unsigned int num_threads)
{
#if 0
	test_worker_threads(num_threads);
	test_thread_join(num_threads);
	test_thread_latency(num_threads);
	test_thread_join_steal(num_threads);
#endif
	test_thread_join_sum(num_threads, 1 << 20, false, 10);	/* 1M of values */
	test_thread_join_sum(num_threads, 1 << 20, true, 10);	/* 1M of values */

	return 0;
}

#define OPT_NUM_THREADS		128

static struct option lopts[] = {
	{"num-threads",		required_argument,	0,	OPT_NUM_THREADS },
	{"help",		no_argument,		0,	'h' },
	{0,			0,              	0,	 0  },
};

static void display_usage(FILE *fp, const char *progname)
{
	const char *s;

	s = strrchr(progname, '/');
	if (s != NULL)
		progname = s + 1;

	fprintf(fp, "Usage:\n\t%s [options]\n", progname);
	fprintf(fp, "\noptions:\n");
	fprintf(fp, "\t--num-threads <n>         : Number of threads to use (default: number of CPUs * 3 / 2)\n");
	fprintf(fp, "\t--help, -h                : Display help message\n");
	fprintf(fp, "\n");
}

int main(int argc, char *argv[])
{
	int opt, lidx, rc;
	unsigned int num_threads = 0;
	int exitcode = EXIT_FAILURE, opti;

	while ((opt = getopt_long_only(argc, argv, "h", lopts, &lidx)) != -1) {
		switch (opt) {
		case OPT_NUM_THREADS:
			opti = atoi(optarg);
			if (opti < 0) {
				fprintf(stderr, "Error: bad num_threads=%d (must be >= 0)\n\n", opti);
				goto err_out_usage;
			}
			num_threads = (unsigned int)opti;
			break;
		case 'h' :
			display_usage(stdout, argv[0]);
			goto ok_out;
		default:
			goto err_out_usage;
		}
	}

	rc = thread_test(num_threads);
	if (rc) {
		fprintf(stderr, "Error: thread_test() failed\n");
		goto err_out;
	}

ok_out:
	exitcode = EXIT_SUCCESS;

out:
	return exitcode;

err_out_usage:
	display_usage(stderr, argv[0]);
err_out:
	exitcode = EXIT_FAILURE;
	goto out;
}
