/*
 * Benchmark: Reduce operations (light and heavy workloads)
 *
 * To compile from repository root:
 *   gcc -O3 -DHAVE_CONFIG_H=1 -Ibuild -Isrc/lib -Isrc/generic -Isrc/thread \
 *       -Isrc/allocator -Isrc/util -Isrc/blake3 -Isrc/xxhash -I. \
 *       bench_parallel_reduce.c build/libfyaml_static.a -o /tmp/bench_parallel_reduce \
 *       -lm -lpthread
 *
 * To run:
 *   /tmp/bench_parallel_reduce [size]
 *   Example: /tmp/bench_parallel_reduce 10000
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <libfyaml.h>
#include <libfyaml/fy-internal-generic.h>
#include "fy-thread.h"

// Light reducer: simple sum
static fy_generic bench_reduce_sum(struct fy_generic_builder *gb, fy_generic acc, fy_generic v)
{
	return fy_value(gb, fy_cast(acc, 0) + fy_cast(v, 0));
}

// Heavy reducer: sum with expensive computation
static fy_generic bench_reduce_heavy(struct fy_generic_builder *gb, fy_generic acc, fy_generic v)
{
	int acc_val = fy_cast(acc, 0);
	int v_val = fy_cast(v, 0);
	double result = acc_val + v_val;

	// Do some actual work (100 sin/cos operations)
	for (int i = 0; i < 100; i++) {
		result = sin(result) * cos(result) + acc_val + v_val;
	}

	return fy_value(gb, (int)result);
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
	int iterations_light = 100;
	int iterations_heavy = 5;
	(void)result;  // Suppress unused variable warning

	if (argc > 1)
		size = atoi(argv[1]);
	else
		size = 10000;

	printf("Benchmarking REDUCE operations with %zu items\n", size);
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

		// Benchmark LIGHT REDUCE (simple sum)
		printf("LIGHT REDUCE (simple sum):\n");
		printf("  Iterations: %d\n", iterations_light);

		// Serial reduce
		start = get_time_sec();
		for (int j = 0; j < iterations_light; j++) {
			result = fy_gb_reduce(gb, seq, 0, bench_reduce_sum);
		}
		end = get_time_sec();
		serial_time = (end - start) * 1000 / iterations_light;
		printf("  Serial:   %.3f ms/iter\n", serial_time);

		// Parallel reduce (reusing thread pool!)
		start = get_time_sec();
		for (int j = 0; j < iterations_light; j++) {
			result = fy_gb_preduce(gb, seq, 0, tp, bench_reduce_sum);
		}
		end = get_time_sec();
		parallel_time = (end - start) * 1000 / iterations_light;
		printf("  Parallel: %.3f ms/iter\n", parallel_time);
		printf("  Speedup:  %.2fx\n", serial_time / parallel_time);

		printf("\n");

		// Benchmark HEAVY REDUCE (100 sin/cos per item)
		printf("HEAVY REDUCE (100 sin/cos per reduction):\n");
		printf("  Iterations: %d\n", iterations_heavy);

		// Serial reduce
		start = get_time_sec();
		for (int j = 0; j < iterations_heavy; j++) {
			result = fy_gb_reduce(gb, seq, 0, bench_reduce_heavy);
		}
		end = get_time_sec();
		serial_time = (end - start) * 1000 / iterations_heavy;
		printf("  Serial:   %.1f ms/iter\n", serial_time);

		// Parallel reduce (reusing thread pool!)
		start = get_time_sec();
		for (int j = 0; j < iterations_heavy; j++) {
			result = fy_gb_preduce(gb, seq, 0, tp, bench_reduce_heavy);
		}
		end = get_time_sec();
		parallel_time = (end - start) * 1000 / iterations_heavy;
		printf("  Parallel: %.1f ms/iter\n", parallel_time);
		printf("  Speedup:  %.2fx\n", serial_time / parallel_time);

		fy_generic_builder_destroy(gb);
	}

	fy_thread_pool_destroy(tp);
	return 0;
}
