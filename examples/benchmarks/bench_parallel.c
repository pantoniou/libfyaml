#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libfyaml.h>
#include <libfyaml/fy-internal-generic.h>
#include "fy-thread.h"

static fy_generic bench_map_double(struct fy_generic_builder *gb, fy_generic v)
{
	return fy_value(gb, fy_cast(v, 0) * 2);
}

static bool bench_filter_over_100(struct fy_generic_builder *gb, fy_generic v)
{
	(void)gb;
	return fy_cast(v, 0) > 100;
}

static double get_time_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	struct fy_generic_builder_cfg cfg;
	struct fy_generic_builder *gb;
	struct fy_thread_pool_cfg tp_cfg;
	struct fy_thread_pool *tp;
	fy_generic seq, result;
	fy_generic *items;
	size_t size;
	double start, end, serial_time, parallel_time;
	int iterations = 10;

	if (argc > 1)
		size = atoi(argv[1]);
	else
		size = 100000;

	printf("Benchmarking with %zu items, %d iterations\n", size, iterations);
	printf("========================================\n\n");

	// Create thread pool ONCE upfront
	memset(&tp_cfg, 0, sizeof(tp_cfg));
	tp_cfg.flags = FYTPCF_STEAL_MODE;
	tp_cfg.num_threads = 0;  // Auto-detect
	tp = fy_thread_pool_create(&tp_cfg);
	printf("Thread pool created with %d threads\n\n", fy_thread_pool_get_num_threads(tp));

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED;
	gb = fy_generic_builder_create(&cfg);

	// Create test sequence
	items = malloc(sizeof(fy_generic) * size);
	for (size_t i = 0; i < size; i++)
		items[i] = fy_value(gb, i);
	seq = fy_gb_sequence_create(gb, size, items);
	free(items);

	// Warmup parallel (amortize first-use overhead)
	result = fy_gb_pmap(gb, seq, tp, bench_map_double);
	result = fy_gb_pfilter(gb, seq, tp, bench_filter_over_100);

	// Benchmark MAP
	printf("MAP OPERATION (simple double):\n");

	// Serial map
	start = get_time_sec();
	for (int j = 0; j < iterations; j++) {
		result = fy_gb_map(gb, seq, bench_map_double);
	}
	end = get_time_sec();
	serial_time = (end - start) * 1000 / iterations;
	printf("  Serial:   %.3f ms/iter\n", serial_time);

	// Parallel map (reusing thread pool)
	start = get_time_sec();
	for (int j = 0; j < iterations; j++) {
		result = fy_gb_pmap(gb, seq, tp, bench_map_double);
	}
	end = get_time_sec();
	parallel_time = (end - start) * 1000 / iterations;
	printf("  Parallel: %.3f ms/iter\n", parallel_time);
	printf("  Speedup:  %.2fx\n", serial_time / parallel_time);

	printf("\n");

	// Benchmark FILTER
	printf("FILTER OPERATION (> 100):\n");

	// Serial filter
	start = get_time_sec();
	for (int j = 0; j < iterations; j++) {
		result = fy_gb_filter(gb, seq, bench_filter_over_100);
	}
	end = get_time_sec();
	serial_time = (end - start) * 1000 / iterations;
	printf("  Serial:   %.3f ms/iter\n", serial_time);

	// Parallel filter (reusing thread pool)
	start = get_time_sec();
	for (int j = 0; j < iterations; j++) {
		result = fy_gb_pfilter(gb, seq, tp, bench_filter_over_100);
	}
	end = get_time_sec();
	parallel_time = (end - start) * 1000 / iterations;
	printf("  Parallel: %.3f ms/iter\n", parallel_time);
	printf("  Speedup:  %.2fx\n", serial_time / parallel_time);

	(void)result;
	fy_generic_builder_destroy(gb);
	fy_thread_pool_destroy(tp);
	return 0;
}
