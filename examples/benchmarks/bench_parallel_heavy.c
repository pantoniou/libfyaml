#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <libfyaml.h>
#include <libfyaml/fy-internal-generic.h>
#include "fy-thread.h"

// More expensive map operation
static fy_generic bench_map_heavy(struct fy_generic_builder *gb, fy_generic v)
{
	int val = fy_cast(v, 0);
	double result = val;
	
	// Do some actual work
	for (int i = 0; i < 100; i++) {
		result = sin(result) * cos(result) + val;
	}
	
	return fy_value(gb, (int)result);
}

// More expensive filter operation
static bool bench_filter_heavy(struct fy_generic_builder *gb, fy_generic v)
{
	int val = fy_cast(v, 0);
	double result = val;
	
	// Do some actual work
	for (int i = 0; i < 100; i++) {
		result = sin(result) * cos(result);
	}
	
	return result > 0.0;
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
	int iterations = 5;
	(void)result;  // Suppress unused variable warning

	if (argc > 1)
		size = atoi(argv[1]);
	else
		size = 10000;

	printf("Benchmarking HEAVY operations with %zu items, %d iterations\n", size, iterations);
	printf("(Thread pool created ONCE and reused)\n");
	printf("===========================================\n\n");

	// Create thread pool ONCE (shared across both dedup modes)
	memset(&tp_cfg, 0, sizeof(tp_cfg));
	tp_cfg.flags = FYTPCF_STEAL_MODE;
	tp_cfg.num_threads = 0;  // Auto-detect
	tp = fy_thread_pool_create(&tp_cfg);
	printf("Thread pool created with %d threads\n\n", fy_thread_pool_get_num_threads(tp));

	// Run benchmarks twice: without dedup, then with dedup
	for (int dedup_mode = 0; dedup_mode < 2; dedup_mode++) {
		printf("\n");
		printf("###############################################\n");
		printf("# %s DEDUP\n", dedup_mode == 0 ? "WITHOUT" : "WITH");
		printf("###############################################\n\n");

		memset(&cfg, 0, sizeof(cfg));
		cfg.flags = FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER;
		if (dedup_mode == 1)
			cfg.flags |= FYGBCF_DEDUP_ENABLED;
		gb = fy_generic_builder_create(&cfg);

		// Create test sequence
		items = malloc(sizeof(fy_generic) * size);
		for (size_t i = 0; i < size; i++)
			items[i] = fy_value(gb, i);
		seq = fy_gb_sequence_create(gb, size, items);
		free(items);

		// Benchmark MAP
		printf("MAP OPERATION (100 sin/cos per item):\n");

		// Serial map
		start = get_time_sec();
		for (int j = 0; j < iterations; j++) {
			result = fy_gb_map(gb, seq, bench_map_heavy);
		}
		end = get_time_sec();
		serial_time = (end - start) * 1000 / iterations;
		printf("  Serial:   %.1f ms/iter\n", serial_time);

		// Parallel map (reusing thread pool!)
		start = get_time_sec();
		for (int j = 0; j < iterations; j++) {
			result = fy_gb_pmap(gb, seq, tp, bench_map_heavy);
		}
		end = get_time_sec();
		parallel_time = (end - start) * 1000 / iterations;
		printf("  Parallel: %.1f ms/iter\n", parallel_time);
		printf("  Speedup:  %.2fx\n", serial_time / parallel_time);

		printf("\n");

		// Benchmark FILTER
		printf("FILTER OPERATION (100 sin/cos per item):\n");

		// Serial filter
		start = get_time_sec();
		for (int j = 0; j < iterations; j++) {
			result = fy_gb_filter(gb, seq, bench_filter_heavy);
		}
		end = get_time_sec();
		serial_time = (end - start) * 1000 / iterations;
		printf("  Serial:   %.1f ms/iter\n", serial_time);

		// Parallel filter (reusing thread pool!)
		start = get_time_sec();
		for (int j = 0; j < iterations; j++) {
			result = fy_gb_pfilter(gb, seq, tp, bench_filter_heavy);
		}
		end = get_time_sec();
		parallel_time = (end - start) * 1000 / iterations;
		printf("  Parallel: %.1f ms/iter\n", parallel_time);
		printf("  Speedup:  %.2fx\n", serial_time / parallel_time);

		fy_generic_builder_destroy(gb);
	}

	fy_thread_pool_destroy(tp);
	return 0;
}
