/*
 * bench_alloc.c - allocation throughput benchmark
 *
 * Compares: malloc, linear (buf), mremap, durable allocators.
 * Each round allocates N objects of varying sizes and measures
 * wall-clock throughput and latency.
 *
 * Build:
 *   cc -O2 -o bench_alloc bench_alloc.c \
 *       -I include -I src/allocator \
 *       -L build -lfyaml -Wl,-rpath,$(pwd)/build
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <libfyaml.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0
#endif

/* benchmark parameters */
#define NALLOCS		500000
#define WARMUP		10000
#define RUNS		5

#define TEST_REGION_BASE	0x520000000000ULL
#define TEST_REGION_SIZE	(256ULL << 20)
#define TEST_CHUNK_SIZE		(16ULL << 20)

/* size distribution: small (8B), medium (64B), large (256B) */
static const size_t size_table[] = { 8, 8, 16, 8, 64, 8, 32, 256, 8, 16 };
#define SIZE_TABLE_LEN (sizeof(size_table) / sizeof(size_table[0]))

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static char *make_tmpdir(void)
{
	char *buf = malloc(64);
	const char *tmp = getenv("TMPDIR");
	snprintf(buf, 64, "%s/fy-bench-XXXXXX", tmp && *tmp ? tmp : "/tmp");
	return mkdtemp(buf) ? buf : NULL;
}

static void rm_rf(const char *dir)
{
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	(void)system(cmd);
}

typedef struct {
	const char *name;
	uint64_t min_ns;
	uint64_t total_ns;
	int runs;
} result_t;

static void print_result(const result_t *r, long nallocs)
{
	double avg_ns  = (double)r->total_ns / r->runs / nallocs;
	double min_ns  = (double)r->min_ns / nallocs;
	double avg_mops = 1e9 / avg_ns / 1e6;
	double max_mops = 1e9 / min_ns / 1e6;

	printf("  %-12s  avg %6.1f ns/alloc  best %6.1f ns/alloc  "
	       "avg %6.2f Mops/s  peak %6.2f Mops/s\n",
	       r->name, avg_ns, min_ns, avg_mops, max_mops);
}

/* ------------------------------------------------------------------ */

static void bench_allocator(struct fy_allocator *a, const char *label,
			    int nallocs, result_t *out)
{
	int tag;
	uint64_t t0, t1, elapsed;
	volatile uint8_t sink = 0;
	int run, i;

	out->name = label;
	out->min_ns = UINT64_MAX;
	out->total_ns = 0;
	out->runs = RUNS;

	/* warm up the tag machinery */
	for (i = 0; i < WARMUP; i++) {
		tag = fy_allocator_get_tag(a);
		if (tag < 0) break;
		size_t sz = size_table[i % SIZE_TABLE_LEN];
		void *p = fy_allocator_alloc(a, tag, sz, 8);
		if (p) sink ^= *(volatile uint8_t *)p;
		fy_allocator_release_tag(a, tag);
	}
	(void)sink;

	for (run = 0; run < RUNS; run++) {
		/* fresh tag each run so the arena is reusable */
		tag = fy_allocator_get_tag(a);
		if (tag < 0) {
			fprintf(stderr, "  [%s] get_tag failed\n", label);
			out->runs = 0;
			return;
		}

		t0 = now_ns();
		for (i = 0; i < nallocs; i++) {
			size_t sz = size_table[i % SIZE_TABLE_LEN];
			void *p = fy_allocator_alloc(a, tag, sz, 8);
			if (!p) {
				fprintf(stderr, "  [%s] alloc failed at i=%d\n", label, i);
				fy_allocator_release_tag(a, tag);
				out->runs = run;
				return;
			}
			/* prevent dead-code elimination */
			*(volatile uint8_t *)p = (uint8_t)i;
		}
		t1 = now_ns();
		elapsed = t1 - t0;

		fy_allocator_release_tag(a, tag);

		if (elapsed < out->min_ns) out->min_ns = elapsed;
		out->total_ns += elapsed;
	}
}

/* ------------------------------------------------------------------ */

int main(void)
{
	struct fy_allocator *a;
	result_t results[8];
	int nresults = 0;
	const long nallocs = NALLOCS;

	printf("libfyaml allocator benchmark\n");
	printf("  %ld allocs/run  x%d runs  size mix: 8,8,16,8,64,8,32,256,8,16\n\n",
	       nallocs, RUNS);

	/* ---- malloc ---- */
	a = fy_allocator_create("malloc", NULL);
	if (a) {
		bench_allocator(a, "malloc", nallocs, &results[nresults++]);
		fy_allocator_destroy(a);
	} else {
		fprintf(stderr, "malloc allocator unavailable\n");
	}

	/* ---- mremap ---- */
	a = fy_allocator_create("mremap", NULL);
	if (a) {
		bench_allocator(a, "mremap", nallocs, &results[nresults++]);
		fy_allocator_destroy(a);
	} else {
		fprintf(stderr, "mremap allocator unavailable\n");
	}

	/* ---- linear (in-place, 128 MiB backing buffer) ---- */
	{
		const size_t bufsz = 128ULL << 20;
		void *buf = mmap(NULL, bufsz, PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
		if (buf != MAP_FAILED) {
			a = fy_linear_allocator_create_in_place(buf, bufsz);
			if (a) {
				bench_allocator(a, "linear", nallocs, &results[nresults++]);
				/* no destroy needed for in-place */
			} else {
				fprintf(stderr, "linear in-place create failed\n");
			}
			munmap(buf, bufsz);
		}
	}

	/* ---- dedup (in-place, 128 MiB) ---- */
	{
		const size_t bufsz = 128ULL << 20;
		void *buf = mmap(NULL, bufsz, PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
		if (buf != MAP_FAILED) {
			a = fy_dedup_allocator_create_in_place(buf, bufsz);
			if (a) {
				bench_allocator(a, "dedup", nallocs, &results[nresults++]);
			} else {
				fprintf(stderr, "dedup in-place create failed\n");
			}
			munmap(buf, bufsz);
		}
	}

	/* ---- durable ---- */
	{
		char *tmpdir = make_tmpdir();
		if (tmpdir) {
			struct fy_durable_allocator_cfg cfg;
			memset(&cfg, 0, sizeof(cfg));
			cfg.dir         = tmpdir;
			cfg.region_base = TEST_REGION_BASE;
			cfg.region_size = TEST_REGION_SIZE;
			cfg.chunk_size  = TEST_CHUNK_SIZE;
			cfg.flags       = FY_DURABLE_ARENA_CREATE | FY_DURABLE_ARENA_SPARSE;

			a = fy_allocator_create("durable", &cfg);
			if (a) {
				bench_allocator(a, "durable", nallocs, &results[nresults++]);
				fy_allocator_destroy(a);
			} else {
				fprintf(stderr, "durable allocator unavailable "
					"(fixed VA 0x%llx may be in use)\n",
					(unsigned long long)TEST_REGION_BASE);
			}
			rm_rf(tmpdir);
			free(tmpdir);
		}
	}

	/* ---- durable+dedup ---- */
	{
		char *tmpdir = make_tmpdir();
		if (tmpdir) {
			struct fy_durable_allocator_cfg cfg;
			memset(&cfg, 0, sizeof(cfg));
			cfg.dir         = tmpdir;
			cfg.region_base = TEST_REGION_BASE;
			cfg.region_size = TEST_REGION_SIZE;
			cfg.chunk_size  = TEST_CHUNK_SIZE;
			cfg.flags       = FY_DURABLE_ARENA_CREATE | FY_DURABLE_ARENA_SPARSE | FY_DURABLE_ARENA_DEDUP;

			a = fy_allocator_create("durable", &cfg);
			if (a) {
				bench_allocator(a, "durable+dedup", nallocs, &results[nresults++]);
				fy_allocator_destroy(a);
			} else {
				fprintf(stderr, "durable+dedup allocator unavailable\n");
			}
			rm_rf(tmpdir);
			free(tmpdir);
		}
	}

	/* ---- print results ---- */
	printf("Results:\n");
	for (int i = 0; i < nresults; i++) {
		if (results[i].runs > 0)
			print_result(&results[i], nallocs);
		else
			printf("  %-12s  (failed)\n", results[i].name);
	}

	printf("\nNote: 'linear' and 'dedup' are in-place arena allocators (no individual frees).\n");
	printf("      'mremap' is a growable arena. 'malloc' is system malloc.\n");
	printf("      'durable' writes to a temp mmap'd file arena.\n");
	return 0;
}
