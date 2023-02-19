/*
 * generic-parallel-transform.c - Parallel filter/map/reduce with generics
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-generic.h>
#include <libfyaml/libfyaml-thread.h>

static long long delta_ns(struct timespec before, struct timespec after)
{
	return (after.tv_sec - before.tv_sec) * 1000000000LL +
	       (after.tv_nsec - before.tv_nsec);
}

static void print_yaml(const char *title, fy_generic v)
{
	fy_generic emitted;

	emitted = fy_emit(v,
			  FYOPEF_DISABLE_DIRECTORY |
			  FYOPEF_MODE_YAML_1_2 |
			  FYOPEF_STYLE_BLOCK,
			  NULL);
	if (fy_generic_is_invalid(emitted)) {
		fprintf(stderr, "Failed to emit %s\n", title);
		return;
	}

	printf("%s:\n%s\n", title, fy_cast(emitted, ""));
}

static bool parse_size_t_arg(const char *arg, size_t *valuep)
{
	char *end;
	unsigned long long value;

	errno = 0;
	value = strtoull(arg, &end, 10);
	if (errno || !end || *end || value == 0)
		return false;

	*valuep = (size_t)value;
	return true;
}

int main(int argc, char *argv[])
{
	struct fy_generic_builder *parallel_gb;
	struct fy_generic_builder *serial_gb;
	struct fy_thread_pool *tp;
	struct fy_generic_builder_cfg parallel_cfg = {
		.flags = FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER,
	};
	struct fy_generic_builder_cfg serial_cfg = {
		.flags = FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER,
	};
	struct fy_thread_pool_cfg tp_cfg = {
		.flags = FYTPCF_STEAL_MODE,
		.num_threads = 0,
	};
	struct timespec parallel_before;
	struct timespec parallel_filter_after;
	struct timespec parallel_map_after;
	struct timespec parallel_reduce_after;
	struct timespec serial_before;
	struct timespec serial_filter_after;
	struct timespec serial_map_after;
	struct timespec serial_reduce_after;
	long long parallel_filter_ns;
	long long parallel_map_ns;
	long long parallel_reduce_ns;
	long long parallel_total_ns;
	long long serial_filter_ns;
	long long serial_map_ns;
	long long serial_reduce_ns;
	long long serial_total_ns;
	double speedup;
	fy_generic parallel_values;
	fy_generic parallel_filtered;
	fy_generic parallel_doubled;
	fy_generic parallel_total;
	fy_generic serial_values;
	fy_generic serial_filtered;
	fy_generic serial_doubled;
	fy_generic serial_total;
	fy_generic result;
	fy_generic first_filtered;
	fy_generic last_filtered;
	fy_generic *items;
	size_t item_count = 32768;
	size_t i;
	const size_t min_parallel_count = 1024;

	if (argc > 1 && !parse_size_t_arg(argv[1], &item_count)) {
		fprintf(stderr, "Usage: %s [item-count] [thread-count]\n", argv[0]);
		return EXIT_FAILURE;
	}
	if (argc > 2) {
		size_t thread_count;

		if (!parse_size_t_arg(argv[2], &thread_count)) {
			fprintf(stderr, "Usage: %s [item-count] [thread-count]\n", argv[0]);
			return EXIT_FAILURE;
		}
		tp_cfg.num_threads = (unsigned int)thread_count;
	}

	parallel_cfg.estimated_max_size = item_count * 64;
	serial_cfg.estimated_max_size = item_count * 64;

	items = malloc(item_count * sizeof(*items));
	if (!items) {
		fprintf(stderr, "Failed to allocate %zu generic items\n", item_count);
		return EXIT_FAILURE;
	}

	parallel_gb = fy_generic_builder_create(&parallel_cfg);
	if (!parallel_gb) {
		fprintf(stderr, "Failed to create parallel generic builder\n");
		free(items);
		return EXIT_FAILURE;
	}

	serial_gb = fy_generic_builder_create(&serial_cfg);
	if (!serial_gb) {
		fprintf(stderr, "Failed to create serial generic builder\n");
		fy_generic_builder_destroy(parallel_gb);
		free(items);
		return EXIT_FAILURE;
	}

	tp = fy_thread_pool_create(&tp_cfg);
	if (!tp) {
		fprintf(stderr, "Failed to create thread pool\n");
		fy_generic_builder_destroy(serial_gb);
		fy_generic_builder_destroy(parallel_gb);
		free(items);
		return EXIT_FAILURE;
	}

	for (i = 0; i < item_count; i++)
		items[i] = fy_value((long long)i);
	parallel_values = fy_gb_sequence_create(parallel_gb, item_count, items);
	serial_values = fy_gb_sequence_create(serial_gb, item_count, items);

	clock_gettime(CLOCK_MONOTONIC, &parallel_before);
	parallel_filtered = fy_pfilter_lambda(
		parallel_gb, parallel_values, tp,
		fy_cast(v, 0LL) > 100);
	clock_gettime(CLOCK_MONOTONIC, &parallel_filter_after);
	parallel_doubled = fy_pmap_lambda(
		parallel_gb, parallel_filtered, tp,
		fy_value(fy_cast(v, 0LL) * 2));
	clock_gettime(CLOCK_MONOTONIC, &parallel_map_after);
	parallel_total = fy_preduce_lambda(
		parallel_gb, parallel_doubled, 0LL, tp,
		fy_value(fy_cast(acc, 0LL) + fy_cast(v, 0LL)));
	clock_gettime(CLOCK_MONOTONIC, &parallel_reduce_after);

	clock_gettime(CLOCK_MONOTONIC, &serial_before);
	serial_filtered = fy_filter_lambda(
		serial_gb, serial_values,
		fy_cast(v, 0LL) > 100);
	clock_gettime(CLOCK_MONOTONIC, &serial_filter_after);
	serial_doubled = fy_map_lambda(
		serial_gb, serial_filtered,
		fy_value(fy_cast(v, 0LL) * 2));
	clock_gettime(CLOCK_MONOTONIC, &serial_map_after);
	serial_total = fy_reduce_lambda(
		serial_gb, serial_doubled, 0LL,
		fy_value(fy_cast(acc, 0LL) + fy_cast(v, 0LL)));
	clock_gettime(CLOCK_MONOTONIC, &serial_reduce_after);

	if (fy_compare(parallel_filtered, serial_filtered) != 0 ||
	    fy_compare(parallel_doubled, serial_doubled) != 0 ||
	    fy_compare(parallel_total, serial_total) != 0) {
		fprintf(stderr, "Parallel and serial generic pipelines diverged\n");
		fy_thread_pool_destroy(tp);
		fy_generic_builder_destroy(serial_gb);
		fy_generic_builder_destroy(parallel_gb);
		free(items);
		return EXIT_FAILURE;
	}

	parallel_filter_ns = delta_ns(parallel_before, parallel_filter_after);
	parallel_map_ns = delta_ns(parallel_filter_after, parallel_map_after);
	parallel_reduce_ns = delta_ns(parallel_map_after, parallel_reduce_after);
	parallel_total_ns = delta_ns(parallel_before, parallel_reduce_after);
	serial_filter_ns = delta_ns(serial_before, serial_filter_after);
	serial_map_ns = delta_ns(serial_filter_after, serial_map_after);
	serial_reduce_ns = delta_ns(serial_map_after, serial_reduce_after);
	serial_total_ns = delta_ns(serial_before, serial_reduce_after);
	speedup = parallel_total_ns > 0 ?
		(double)serial_total_ns / (double)parallel_total_ns : 0.0;

	first_filtered = fy_len(parallel_filtered) ? fy_get(parallel_filtered, 0, fy_invalid) : fy_null;
	last_filtered = fy_len(parallel_filtered) ? fy_get(parallel_filtered, fy_len(parallel_filtered) - 1, fy_invalid) : fy_null;

	result = fy_gb_mapping(
		parallel_gb,
		"thread_count", (long long)fy_thread_pool_get_num_threads(tp),
		"source_count", (long long)fy_len(parallel_values),
		"filtered_count", (long long)fy_len(parallel_filtered),
		"first_filtered", first_filtered,
		"last_filtered", last_filtered,
		"sum_of_doubled", parallel_total,
		"timings_ns", fy_gb_mapping(
			parallel_gb,
			"parallel", fy_gb_mapping(
				parallel_gb,
				"filter", parallel_filter_ns,
				"map", parallel_map_ns,
				"reduce", parallel_reduce_ns,
				"total", parallel_total_ns),
			"serial", fy_gb_mapping(
				parallel_gb,
				"filter", serial_filter_ns,
				"map", serial_map_ns,
				"reduce", serial_reduce_ns,
				"total", serial_total_ns),
			"speedup_vs_serial", speedup));

	printf("thread pool threads: %d\n", fy_thread_pool_get_num_threads(tp));
	printf("using lambda APIs: fy_filter_lambda/fy_map_lambda/fy_reduce_lambda and "
	       "fy_pfilter_lambda/fy_pmap_lambda/fy_preduce_lambda\n");
	printf("source count: %zu\n", fy_len(parallel_values));
	if (item_count <= min_parallel_count)
		printf("note: workloads at or below %zu items are usually dominated by setup overhead\n",
		       min_parallel_count);
	printf("filtered count (>100): %zu\n", fy_len(parallel_filtered));
	printf("sum of doubled filtered values: %lld\n", fy_cast(parallel_total, 0LL));
	printf("parallel timings: filter=%.3f ms map=%.3f ms reduce=%.3f ms total=%.3f ms\n",
	       parallel_filter_ns / 1000000.0,
	       parallel_map_ns / 1000000.0,
	       parallel_reduce_ns / 1000000.0,
	       parallel_total_ns / 1000000.0);
	printf("serial timings:   filter=%.3f ms map=%.3f ms reduce=%.3f ms total=%.3f ms\n",
	       serial_filter_ns / 1000000.0,
	       serial_map_ns / 1000000.0,
	       serial_reduce_ns / 1000000.0,
	       serial_total_ns / 1000000.0);
	printf("serial/parallel total ratio: %.2fx\n", speedup);

	print_yaml("parallel transform summary", result);

	fy_thread_pool_destroy(tp);
	fy_generic_builder_destroy(serial_gb);
	fy_generic_builder_destroy(parallel_gb);
	free(items);
	return EXIT_SUCCESS;
}
