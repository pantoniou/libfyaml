#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libfyaml.h>
#include <libfyaml/fy-internal-generic.h>

static fy_generic bench_map_double(struct fy_generic_builder *gb, fy_generic v)
{
	return fy_value(gb, fy_cast(v, 0) * 2);
}

static bool bench_filter_over_100(struct fy_generic_builder *gb, fy_generic v)
{
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

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED;
	gb = fy_generic_builder_create(&cfg);

	// Create test sequence
	items = malloc(sizeof(fy_generic) * size);
	for (size_t i = 0; i < size; i++)
		items[i] = fy_value(gb, i);
	seq = fy_gb_sequence_create(gb, size, items);
	free(items);

	// Benchmark MAP
	printf("MAP OPERATION:\n");
	
	// Serial map
	start = get_time_sec();
	for (int j = 0; j < iterations; j++) {
		result = fy_gb_map(gb, seq, bench_map_double);
	}
	end = get_time_sec();
	serial_time = (end - start) * 1000 / iterations;
	printf("  Serial:   %.3f ms/iter\n", serial_time);

	// Parallel map
	start = get_time_sec();
	for (int j = 0; j < iterations; j++) {
		result = fy_gb_pmap(gb, seq, NULL, bench_map_double);
	}
	end = get_time_sec();
	parallel_time = (end - start) * 1000 / iterations;
	printf("  Parallel: %.3f ms/iter\n", parallel_time);
	printf("  Speedup:  %.2fx\n", serial_time / parallel_time);

	printf("\n");

	// Benchmark FILTER
	printf("FILTER OPERATION:\n");
	
	// Serial filter
	start = get_time_sec();
	for (int j = 0; j < iterations; j++) {
		result = fy_gb_filter(gb, seq, bench_filter_over_100);
	}
	end = get_time_sec();
	serial_time = (end - start) * 1000 / iterations;
	printf("  Serial:   %.3f ms/iter\n", serial_time);

	// Parallel filter
	start = get_time_sec();
	for (int j = 0; j < iterations; j++) {
		result = fy_gb_pfilter(gb, seq, NULL, bench_filter_over_100);
	}
	end = get_time_sec();
	parallel_time = (end - start) * 1000 / iterations;
	printf("  Parallel: %.3f ms/iter\n", parallel_time);
	printf("  Speedup:  %.2fx\n", serial_time / parallel_time);

	(void)result;  // Suppress unused variable warning
	fy_generic_builder_destroy(gb);
	return 0;
}
