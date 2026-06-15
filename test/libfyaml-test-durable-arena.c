/*
 * libfyaml-test-durable-arena.c - durable arena substrate tests
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _WIN32
struct fy_check_suite;
void libfyaml_case_durable_arena(struct fy_check_suite *cs);

void libfyaml_case_durable_arena(struct fy_check_suite *cs)
{
	(void)cs;
}
#else

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

#include <check.h>

/* not present on macOS; a plain hinted mmap there has no-clobber semantics */
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0
#endif

#include <libfyaml.h>

#include "fy-check.h"
#include "fy-allocator.h"
#include "fy-allocator-dedup.h"

#ifdef HAVE_GENERIC
#include "fy-generic.h"
#endif

/*
 * Tests use a distinct high-half base and a deliberately small geometry
 * (1 MiB chunks) so grows happen quickly. If the chosen base cannot be
 * reserved in the test sandbox the test soft-skips (returns as pass) rather
 * than failing, since address availability is environmental.
 */
#define TEST_REGION_BASE	0x520000000000ULL
#define TEST_REGION_SIZE	(256ULL << 20)	/* 256 MiB -> 256 chunks */
#define TEST_CHUNK_SIZE		(1ULL << 20)	/* 1 MiB */

/* separate dedup-index region, well clear of the content region above */
#define TEST_INDEX_REGION_BASE	0x560000000000ULL
#define TEST_INDEX_REGION_SIZE	(64ULL << 20)	/* 64 MiB */
#define TEST_INDEX_CHUNK_SIZE	(1ULL << 20)	/* 1 MiB */

static char *make_tmpdir(char *buf, size_t bufsz)
{
	const char *tmp = getenv("TMPDIR");

	snprintf(buf, bufsz, "%s/fy-durable-XXXXXX", tmp && *tmp ? tmp : "/tmp");
	return mkdtemp(buf);
}

static void rm_rf(const char *dir)
{
	char cmd[512];

	/* test-only cleanup of a known mkdtemp directory */
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	if (system(cmd) != 0)
		(void)0;
}

static struct fy_allocator *
open_test_arena(const char *dir, uint64_t base, unsigned int extra_flags)
{
	struct fy_durable_allocator_cfg cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.dir = dir;
	cfg.region_base = base;
	cfg.region_size = TEST_REGION_SIZE;
	cfg.chunk_size = TEST_CHUNK_SIZE;
	cfg.flags = FY_DURABLE_ARENA_CREATE | extra_flags;

	return fy_allocator_create("durable", &cfg);
}

/* open helper that also configures the separate dedup-index region */
static struct fy_allocator *
open_test_arena_sep(const char *dir, uint64_t base, uint64_t index_base,
		    unsigned int extra_flags)
{
	struct fy_durable_allocator_cfg cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.dir = dir;
	cfg.region_base = base;
	cfg.region_size = TEST_REGION_SIZE;
	cfg.chunk_size = TEST_CHUNK_SIZE;
	cfg.index_region_base = index_base;
	cfg.index_region_size = TEST_INDEX_REGION_SIZE;
	cfg.index_chunk_size = TEST_INDEX_CHUNK_SIZE;
	cfg.flags = FY_DURABLE_ARENA_CREATE | extra_flags;

	return fy_allocator_create("durable", &cfg);
}

/* does a chunk file named "<prefix>-<gen>.bin" exist in the arena dir? */
static bool durable_chunk_file_exists(const char *dir, const char *prefix, unsigned int gen)
{
	char path[400];
	struct stat st;

	snprintf(path, sizeof(path), "%s/%s-%u.bin", dir, prefix, gen);
	return stat(path, &st) == 0;
}

/*
 * Run @count work items in parallel, one reserved fy_thread per item - the
 * portable 1-1 analogue of a pthread_create/pthread_join fan-out, using the
 * libfyaml thread abstraction (non-stealing/reservation mode) so the stress
 * tests build and run on Windows too. @jobs is an array of @count items each
 * @jobsz bytes; @fn is invoked once per item with &jobs[i].
 */
#define DT_MAX_THREADS	16
static void
dt_run_parallel(fy_work_exec_fn fn, void *jobs, size_t jobsz, int count)
{
	struct fy_thread_pool_cfg tpcfg;
	struct fy_thread_pool *tp;
	struct fy_thread *t[DT_MAX_THREADS];
	struct fy_thread_work work[DT_MAX_THREADS];
	int k;

	ck_assert_int_le(count, DT_MAX_THREADS);

	memset(&tpcfg, 0, sizeof(tpcfg));
	tpcfg.num_threads = count;
	tp = fy_thread_pool_create(&tpcfg);
	ck_assert_ptr_ne(tp, NULL);

	/* reserve + submit maps onto pthread_create ... */
	for (k = 0; k < count; k++) {
		t[k] = fy_thread_reserve(tp);
		ck_assert_ptr_ne(t[k], NULL);
		work[k].fn = fn;
		work[k].arg = (char *)jobs + (size_t)k * jobsz;
		work[k].wp = NULL;
		ck_assert_int_eq(fy_thread_submit_work(t[k], &work[k]), 0);
	}
	/* ... wait + unreserve maps onto pthread_join */
	for (k = 0; k < count; k++) {
		ck_assert_int_eq(fy_thread_wait_work(t[k]), 0);
		fy_thread_unreserve(t[k]);
	}

	fy_thread_pool_destroy(tp);
}

START_TEST(durable_roundtrip_fixed_base)
{
	char dir[256];
	struct fy_allocator *da;
	struct fy_allocator *a;
	const char *s1, *s2;
	uintptr_t a1, a2;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);

	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		/* base unavailable in sandbox: soft-skip */
		rm_rf(dir);
		return;
	}
	a = da;
	ck_assert_ptr_ne(a, NULL);

	/* the durable allocator advertises the durability capability */
	ck_assert(fy_allocator_get_caps_nocheck(a) & FYACF_DURABLE);

	/* store two payloads, record their fixed addresses */
	s1 = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, "hello durable", 14, 16);
	s2 = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, "second payload", 15, 16);
	ck_assert_ptr_ne(s1, NULL);
	ck_assert_ptr_ne(s2, NULL);
	ck_assert(fy_allocator_contains_nocheck(a, FY_ALLOC_TAG_DEFAULT, s1));
	a1 = (uintptr_t)s1;
	a2 = (uintptr_t)s2;

	/* addresses must be inside the fixed region */
	ck_assert_uint_ge(a1, TEST_REGION_BASE);
	ck_assert_uint_lt(a1, TEST_REGION_BASE + TEST_REGION_SIZE);

	ck_assert_int_eq(fy_allocator_sync(da), 0);
	fy_allocator_destroy(da);

	/* reopen: payloads must be at the very same addresses, intact */
	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	ck_assert_ptr_ne(da, NULL);
	ck_assert_uint_eq(fy_allocator_region_base(da), TEST_REGION_BASE);
	ck_assert_uint_eq(fy_allocator_region_size(da), TEST_REGION_SIZE);

	ck_assert_mem_eq((const void *)a1, "hello durable", 14);
	ck_assert_mem_eq((const void *)a2, "second payload", 15);

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

#ifdef HAVE_GENERIC
START_TEST(durable_builder_roundtrip)
{
	char dir[256];
	struct fy_allocator *da;
	struct fy_allocator *a;
	struct fy_generic_builder_cfg gb_cfg;
	struct fy_generic_builder *gb;
	fy_generic v;
	const void *p;
	uintptr_t addr;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);

	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		rm_rf(dir);
		return;
	}
	a = da;

	memset(&gb_cfg, 0, sizeof(gb_cfg));
	gb_cfg.flags = FYGBCF_SCOPE_LEADER;
	gb_cfg.allocator = a;
	gb = fy_generic_builder_create(&gb_cfg);
	ck_assert_ptr_ne(gb, NULL);

	/* an out-of-place string is interned into the durable arena */
	v = fy_gb_string_create_out_of_place(gb, "a durable interned string");
	ck_assert(!fy_generic_is_invalid(v));
	p = fy_generic_resolve_ptr(v);
	ck_assert(fy_allocator_contains_nocheck(a, FY_ALLOC_TAG_DEFAULT, p));
	addr = (uintptr_t)p;
	ck_assert_uint_ge(addr, TEST_REGION_BASE);
	ck_assert_uint_lt(addr, TEST_REGION_BASE + TEST_REGION_SIZE);

	ck_assert_str_eq(fy_generic_get_string_alloca(v), "a durable interned string");

	fy_generic_builder_destroy(gb);
	fy_allocator_sync(da);
	fy_allocator_destroy(da);

	/* reopen and read the string straight from its fixed address */
	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	ck_assert_ptr_ne(da, NULL);
	/* the encoded string bytes (vlsize prefix + chars) live at addr */
	ck_assert_mem_eq((const char *)addr + 1, "a durable interned string", 25);
	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST
#endif /* HAVE_GENERIC */

START_TEST(durable_multi_chunk_grow)
{
	char dir[256];
	struct fy_allocator *da;
	struct fy_allocator *a;
	const size_t blk = 8192;
	const int nblk = 1024;	/* ~8 MiB -> several 1 MiB chunks */
	int i;
	const void *p;
	unsigned int chunks;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);

	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		rm_rf(dir);
		return;
	}
	a = da;

	for (i = 0; i < nblk; i++) {
		p = fy_allocator_alloc_nocheck(a, FY_ALLOC_TAG_DEFAULT, blk, 16);
		ck_assert_ptr_ne(p, NULL);
		ck_assert(fy_allocator_contains_nocheck(a, FY_ALLOC_TAG_DEFAULT, p));
		memset((void *)p, (i & 0xff), blk);
	}

	chunks = fy_allocator_chunk_count(da);
	ck_assert_uint_gt(chunks, 1);
	/* generation counter >= chunk count (ids consumed monotonically) */
	ck_assert_uint_ge(fy_allocator_generation(da), chunks);

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

START_TEST(durable_concurrent_grow)
{
	char dir[256];
	struct fy_allocator *da;
	const int nkids = 4;
	const int per = 512;
	const size_t blk = 4096;
	int i, k, status, fails = 0;
	pid_t pid;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);

	/* create chunk 0 up-front so children only grow */
	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		rm_rf(dir);
		return;
	}
	fy_allocator_destroy(da);

	for (k = 0; k < nkids; k++) {
		pid = fork();
		if (pid == 0) {
			struct fy_allocator *cda;
			struct fy_allocator *ca;
			uint64_t tag = ((uint64_t)getpid() << 20);

			cda = open_test_arena(dir, TEST_REGION_BASE, 0);
			if (!cda)
				_exit(2);
			ca = cda;
			for (i = 0; i < per; i++) {
				uint64_t *q = (uint64_t *)
					fy_allocator_alloc_nocheck(ca, FY_ALLOC_TAG_DEFAULT, blk, 16);
				if (!q)
					_exit(3);
				/* write a unique value, then read it back: a clobber
				 * from an overlapping allocation would be caught */
				*q = tag | (uint64_t)i;
				if (*q != (tag | (uint64_t)i))
					_exit(4);
			}
			fy_allocator_destroy(cda);
			_exit(0);
		}
		ck_assert_int_ge(pid, 0);
	}

	for (k = 0; k < nkids; k++) {
		ck_assert_int_ge(wait(&status), 0);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			fails++;
	}
	ck_assert_int_eq(fails, 0);

	/* reopen and confirm the arena grew under contention */
	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	ck_assert_ptr_ne(da, NULL);
	ck_assert_uint_gt(fy_allocator_chunk_count(da), 1);
	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

START_TEST(durable_crash_recovery)
{
	char dir[256];
	struct fy_allocator *da;
	struct fy_allocator *a;
	char path[320];
	int fd, i;
	unsigned int chunks_before;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);

	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		rm_rf(dir);
		return;
	}
	a = da;
	/* force at least one grow so a real chunk list exists */
	for (i = 0; i < 512; i++)
		ck_assert_ptr_ne(fy_allocator_alloc_nocheck(a, FY_ALLOC_TAG_DEFAULT, 4096, 16), NULL);
	chunks_before = fy_allocator_chunk_count(da);
	ck_assert_uint_gt(chunks_before, 1);
	fy_allocator_destroy(da);

	/* plant an orphan .tmp and an orphan unpublished chunk file */
	snprintf(path, sizeof(path), "%s/arena-99.bin.tmp.1234", dir);
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	ck_assert_int_ge(fd, 0);
	close(fd);

	snprintf(path, sizeof(path), "%s/arena-200.bin", dir);
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	ck_assert_int_ge(fd, 0);
	close(fd);

	/* reopen runs recovery */
	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	ck_assert_ptr_ne(da, NULL);
	ck_assert_uint_eq(fy_allocator_chunk_count(da), chunks_before);
	fy_allocator_destroy(da);

	/* both orphans must be gone */
	snprintf(path, sizeof(path), "%s/arena-99.bin.tmp.1234", dir);
	ck_assert_int_ne(access(path, F_OK), 0);
	snprintf(path, sizeof(path), "%s/arena-200.bin", dir);
	ck_assert_int_ne(access(path, F_OK), 0);

	rm_rf(dir);
}
END_TEST

START_TEST(durable_base_conflict)
{
	char dir[256];
	struct fy_allocator *da;
	uint64_t base = 0x528000000000ULL;	/* separate from the others */
	void *occupy;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);

	/* occupy the chosen base so the reservation must fail */
	occupy = mmap((void *)(uintptr_t)base, TEST_CHUNK_SIZE, PROT_NONE,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE, -1, 0);
	if (occupy != (void *)(uintptr_t)base) {
		/* couldn't even occupy it; environmental, soft-skip */
		if (occupy != MAP_FAILED)
			munmap(occupy, TEST_CHUNK_SIZE);
		rm_rf(dir);
		return;
	}

	da = open_test_arena(dir, base, 0);
	ck_assert_ptr_eq(da, NULL);	/* must fail gracefully, no crash, no reloc */

	munmap(occupy, TEST_CHUNK_SIZE);
	rm_rf(dir);
}
END_TEST

/*
 * Intra-process, multi-threaded stress of one shared allocator. This exercises
 * the lock-free ensure_mapped() first-touch race and the lock-free grow path
 * under real thread contention (the fork-based test uses separate per-process
 * chunk tables and does not). Each thread tags every block with a unique value
 * and re-reads it; after join every block must still hold its own tag, which a
 * clobber from an overlapping allocation would break.
 */
#define MT_NTHREADS	8
#define MT_PER		20000
#define MT_BLK		64

struct mt_rec { uint64_t *ptr; uint64_t tag; };

struct mt_job {
	struct fy_allocator *a;
	int tid;
	int fail;
	struct mt_rec *recs;
};

static void mt_worker(void *arg)
{
	struct mt_job *j = arg;
	int i, w;

	for (i = 0; i < MT_PER; i++) {
		uint64_t tag = ((uint64_t)j->tid << 40) | (uint64_t)i;
		uint64_t *p = fy_allocator_alloc_nocheck(j->a, FY_ALLOC_TAG_DEFAULT,
							 MT_BLK, 16);
		if (!p || ((uintptr_t)p & 15)) {
			j->fail = 1;
			return;
		}
		for (w = 0; w < MT_BLK / 8; w++)
			p[w] = tag;
		if (p[0] != tag || p[MT_BLK / 8 - 1] != tag) {
			j->fail = 1;
			return;
		}
		j->recs[i].ptr = p;
		j->recs[i].tag = tag;
	}
}

START_TEST(durable_threaded_grow)
{
	char dir[256];
	struct fy_allocator *da;
	struct fy_allocator *a;
	struct mt_job jobs[MT_NTHREADS];
	int k, i, w;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);

	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		rm_rf(dir);
		return;
	}
	a = da;

	for (k = 0; k < MT_NTHREADS; k++) {
		jobs[k].a = a;
		jobs[k].tid = k;
		jobs[k].fail = 0;
		jobs[k].recs = malloc(sizeof(struct mt_rec) * MT_PER);
		ck_assert_ptr_ne(jobs[k].recs, NULL);
	}
	dt_run_parallel(mt_worker, jobs, sizeof(jobs[0]), MT_NTHREADS);

	/* no thread hit a failed/misaligned alloc or an immediate clobber */
	for (k = 0; k < MT_NTHREADS; k++)
		ck_assert_int_eq(jobs[k].fail, 0);

	/* every block must still hold its own tag: overlapping allocations
	 * (a broken bump/grow) would have clobbered someone's tag */
	/*
	 * Accumulate mismatches over the inner loops and assert once per thread:
	 * a per-element ck_assert emits a libcheck mark-point (a write() syscall)
	 * on every success, and at 8 * 20000 * 9 = 1.44M iterations that harness
	 * IPC - not the allocator - is what dominated this test's runtime.
	 */
	for (k = 0; k < MT_NTHREADS; k++) {
		int bad = 0;
		for (i = 0; i < MT_PER; i++) {
			uint64_t *p = jobs[k].recs[i].ptr;
			if (!fy_allocator_contains_nocheck(a, FY_ALLOC_TAG_DEFAULT, p))
				bad++;
			for (w = 0; w < MT_BLK / 8; w++)
				if (p[w] != jobs[k].recs[i].tag)
					bad++;
		}
		ck_assert_int_eq(bad, 0);
		free(jobs[k].recs);
	}

	/* contention must have driven several grows */
	ck_assert_uint_gt(fy_allocator_chunk_count(da), 1);

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

/*
 * Isolate the external-root dedup wiring from the durable arena: create the
 * index over a plain mremap parent with a local root slot, then a second layer
 * attaches to the same slot and must see the first layer's stores (shared index).
 */
START_TEST(dedup_external_attach)
{
	struct fy_allocator *base, *d1, *d2;
	struct fy_dedup_allocator_cfg dcfg;
	FY_ATOMIC(struct fy_dedup_tag *) root = NULL;
	const char payload[] = "a sufficiently long dedup payload value";
	const void *p1, *p2, *p3;

	base = fy_allocator_create("mremap", NULL);
	ck_assert_ptr_ne(base, NULL);

	memset(&dcfg, 0, sizeof(dcfg));
	dcfg.parent_allocator = base;
	dcfg.dedup_threshold = 8;

	/* create: publishes the shared root */
	d1 = fy_dedup_create_external(base, FY_ALLOC_TAG_DEFAULT, &dcfg, &root);
	ck_assert_ptr_ne(d1, NULL);
	ck_assert_ptr_ne((void *)root, NULL);

	p1 = fy_allocator_store_nocheck(d1, FY_ALLOC_TAG_DEFAULT, payload, sizeof(payload), 16);
	ck_assert_ptr_ne(p1, NULL);

	/* attach: same root slot -> shared index */
	d2 = fy_dedup_create_external(base, FY_ALLOC_TAG_DEFAULT, &dcfg, &root);
	ck_assert_ptr_ne(d2, NULL);

	/* d2 sees d1's store */
	p2 = fy_allocator_lookup_nocheck(d2, FY_ALLOC_TAG_DEFAULT, payload, sizeof(payload), 16);
	ck_assert_ptr_eq(p2, p1);

	/* storing the same payload via d2 dedups to the same pointer */
	p3 = fy_allocator_store_nocheck(d2, FY_ALLOC_TAG_DEFAULT, payload, sizeof(payload), 16);
	ck_assert_ptr_eq(p3, p1);

	fy_allocator_destroy(d2);
	fy_allocator_destroy(d1);
	fy_allocator_destroy(base);
}
END_TEST

/* deterministic, distinct, >= dedup-threshold payload for a given index */
static size_t dd_payload(char *buf, size_t bufsz, int idx)
{
	int n = snprintf(buf, bufsz, "durable-dedup-payload-%010d-aaaaaaaaaaaaaaaa", idx);
	return (size_t)n + 1;
}

START_TEST(dedup_same_pointer)
{
	char dir[256], pl[80];
	struct fy_allocator *da;
	struct fy_allocator *a;
	const int K = 256;
	const void *first[256];
	int i, j;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);
	da = open_test_arena(dir, TEST_REGION_BASE, FY_DURABLE_ARENA_DEDUP);
	if (!da) { rm_rf(dir); return; }
	a = da;

	/* dedup-over-durable propagates durability and advertises dedup */
	ck_assert(fy_allocator_get_caps_nocheck(a) & FYACF_DURABLE);
	ck_assert(fy_allocator_get_caps_nocheck(a) & FYACF_CAN_DEDUP);

	for (i = 0; i < K; i++) {
		size_t len = dd_payload(pl, sizeof(pl), i);
		const void *p = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, pl, len, 16);
		const void *q = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, pl, len, 16);
		ck_assert_ptr_ne(p, NULL);
		ck_assert_ptr_eq(q, p);		/* same content -> same pointer */
		first[i] = p;
	}
	/* distinct payloads get distinct pointers */
	for (i = 0; i < K; i++)
		for (j = i + 1; j < K; j++)
			ck_assert_ptr_ne(first[i], first[j]);

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

START_TEST(dedup_cross_session)
{
	char dir[256], pl[80];
	struct fy_allocator *da;
	struct fy_allocator *a;
	const int K = 256;
	uintptr_t addr[256];
	const void *n1, *n2;
	int i;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);
	da = open_test_arena(dir, TEST_REGION_BASE, FY_DURABLE_ARENA_DEDUP);
	if (!da) { rm_rf(dir); return; }
	a = da;
	for (i = 0; i < K; i++) {
		size_t len = dd_payload(pl, sizeof(pl), i);
		const void *p = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, pl, len, 16);
		ck_assert_ptr_ne(p, NULL);
		addr[i] = (uintptr_t)p;
	}
	fy_allocator_sync(da);
	fy_allocator_destroy(da);

	/* reopen: the same content must dedup to the identical address */
	da = open_test_arena(dir, TEST_REGION_BASE, FY_DURABLE_ARENA_DEDUP);
	ck_assert_ptr_ne(da, NULL);
	a = da;
	for (i = 0; i < K; i++) {
		size_t len = dd_payload(pl, sizeof(pl), i);
		const void *p = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, pl, len, 16);
		ck_assert_uint_eq((uintptr_t)p, addr[i]);
	}
	/* a brand-new payload is unique and itself dedups on repeat */
	{
		size_t len = dd_payload(pl, sizeof(pl), K + 1);
		n1 = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, pl, len, 16);
		n2 = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, pl, len, 16);
		ck_assert_ptr_ne(n1, NULL);
		ck_assert_ptr_eq(n2, n1);
	}
	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

START_TEST(dedup_cross_process)
{
	char dir[256], path[320], pl[80];
	struct fy_allocator *da;
	const int NK = 4, MSHARED = 200, UNIQ = 200;
	uintptr_t ref[200], got[200];
	int k, j, status, fails = 0, fd;
	pid_t pid;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);
	/* create the arena + index up front so children only attach */
	da = open_test_arena(dir, TEST_REGION_BASE, FY_DURABLE_ARENA_DEDUP);
	if (!da) { rm_rf(dir); return; }
	fy_allocator_destroy(da);

	for (k = 0; k < NK; k++) {
		pid = fork();
		if (pid == 0) {
			struct fy_allocator *cda = open_test_arena(dir, TEST_REGION_BASE, FY_DURABLE_ARENA_DEDUP);
			struct fy_allocator *ca;
			uintptr_t addrs[200];
			char cpath[340];
			int i, cfd;

			if (!cda) _exit(2);
			ca = cda;
			/* shared payloads: every child stores the same content */
			for (i = 0; i < MSHARED; i++) {
				size_t len = dd_payload(pl, sizeof(pl), i);
				const void *p = fy_allocator_store_nocheck(ca, FY_ALLOC_TAG_DEFAULT, pl, len, 16);
				if (!p) _exit(3);
				addrs[i] = (uintptr_t)p;
			}
			/* per-child unique payloads add concurrent-insert pressure */
			for (i = 0; i < UNIQ; i++) {
				size_t len = dd_payload(pl, sizeof(pl), 100000 + k * 1000 + i);
				if (!fy_allocator_store_nocheck(ca, FY_ALLOC_TAG_DEFAULT, pl, len, 16))
					_exit(4);
			}
			snprintf(cpath, sizeof(cpath), "%s/child-%d.bin", dir, k);
			cfd = open(cpath, O_RDWR | O_CREAT | O_TRUNC, 0644);
			if (cfd < 0) _exit(5);
			if (write(cfd, addrs, sizeof(addrs)) != (ssize_t)sizeof(addrs)) _exit(6);
			close(cfd);
			fy_allocator_destroy(cda);
			_exit(0);
		}
		ck_assert_int_ge(pid, 0);
	}
	for (k = 0; k < NK; k++) {
		ck_assert_int_ge(wait(&status), 0);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			fails++;
	}
	ck_assert_int_eq(fails, 0);

	/* every child must have resolved each shared payload to the SAME address
	 * (canonical identity held across processes) */
	snprintf(path, sizeof(path), "%s/child-0.bin", dir);
	fd = open(path, O_RDONLY);
	ck_assert_int_ge(fd, 0);
	ck_assert_int_eq(read(fd, ref, sizeof(ref)), (ssize_t)sizeof(ref));
	close(fd);
	for (k = 1; k < NK; k++) {
		snprintf(path, sizeof(path), "%s/child-%d.bin", dir, k);
		fd = open(path, O_RDONLY);
		ck_assert_int_ge(fd, 0);
		ck_assert_int_eq(read(fd, got, sizeof(got)), (ssize_t)sizeof(got));
		close(fd);
		for (j = 0; j < MSHARED; j++)
			ck_assert_uint_eq(got[j], ref[j]);
	}

	rm_rf(dir);
}
END_TEST

#define DRT_NTHREADS	8
#define DRT_PER		5000

struct drt_job {
	struct fy_allocator *a;
	int tid, fail;
	uintptr_t *addrs;
};

static void drt_worker(void *arg)
{
	struct drt_job *j = arg;
	char pl[80];
	int i;

	for (i = 0; i < DRT_PER; i++) {
		size_t len = dd_payload(pl, sizeof(pl), j->tid * DRT_PER + i);
		const void *p = fy_allocator_store_nocheck(j->a, FY_ALLOC_TAG_DEFAULT, pl, len, 16);
		if (!p) { j->fail = 1; return; }
		j->addrs[i] = (uintptr_t)p;
	}
}

START_TEST(dedup_resize_under_load)
{
	char dir[256], pl[80];
	struct fy_allocator *da;
	struct fy_allocator *a;
	struct drt_job jobs[DRT_NTHREADS];
	int k, i;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);
	da = open_test_arena(dir, TEST_REGION_BASE, FY_DURABLE_ARENA_DEDUP);
	if (!da) { rm_rf(dir); return; }
	a = da;

	for (k = 0; k < DRT_NTHREADS; k++) {
		jobs[k].a = a;
		jobs[k].tid = k;
		jobs[k].fail = 0;
		jobs[k].addrs = malloc(sizeof(uintptr_t) * DRT_PER);
		ck_assert_ptr_ne(jobs[k].addrs, NULL);
	}
	dt_run_parallel(drt_worker, jobs, sizeof(jobs[0]), DRT_NTHREADS);
	for (k = 0; k < DRT_NTHREADS; k++)
		ck_assert_int_eq(jobs[k].fail, 0);

	/* after many distinct stores (forcing index resizes), every payload must
	 * still dedup to the address recorded at insert time. Accumulate over the
	 * inner loop and assert once: a per-iteration ck_assert emits a libcheck
	 * mark-point (a write() syscall) on every success, which would dominate
	 * the runtime of this 40k-iteration check. */
	for (k = 0; k < DRT_NTHREADS; k++) {
		int bad = 0;
		for (i = 0; i < DRT_PER; i++) {
			size_t len = dd_payload(pl, sizeof(pl), k * DRT_PER + i);
			const void *p = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, pl, len, 16);
			if ((uintptr_t)p != jobs[k].addrs[i])
				bad++;
		}
		ck_assert_int_eq(bad, 0);
		free(jobs[k].addrs);
	}

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

/* ------------------------------------------------------------------ */
/* refs head: atomic publish + durability ordering barrier (S1.10)    */
/* ------------------------------------------------------------------ */

START_TEST(refs_publish_basic)
{
	char dir[256];
	struct fy_allocator *da;
	struct fy_allocator *a;
	const char *p1, *p2;
	uint64_t w1, w2;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);

	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		rm_rf(dir);
		return;
	}
	a = da;

	/* fresh arena has no refs head */
	ck_assert_uint_eq(fy_allocator_refs_get(da), 0);

	p1 = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, "refs-one", 9, 16);
	p2 = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, "refs-two", 9, 16);
	ck_assert_ptr_ne(p1, NULL);
	ck_assert_ptr_ne(p2, NULL);
	w1 = (uint64_t)(uintptr_t)p1;
	w2 = (uint64_t)(uintptr_t)p2;

	/* publish the first head (expected == 0) */
	ck_assert_int_eq(fy_allocator_refs_publish(da, 0, w1, 0), 0);
	ck_assert_uint_eq(fy_allocator_refs_get(da), w1);

	/* a stale-expected publish loses and leaves the head unchanged */
	ck_assert_int_eq(fy_allocator_refs_publish(da, 0, w2, 0), 1);
	ck_assert_uint_eq(fy_allocator_refs_get(da), w1);

	/* a correct compare-and-swap advances the head */
	ck_assert_int_eq(fy_allocator_refs_publish(da, w1, w2, 0), 0);
	ck_assert_uint_eq(fy_allocator_refs_get(da), w2);

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

START_TEST(refs_readonly_rejected)
{
	char dir[256];
	struct fy_allocator *da;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);

	/* create (writable), publish a head, close */
	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		rm_rf(dir);
		return;
	}
	ck_assert_int_eq(fy_allocator_refs_publish(da, 0, 0x1234, FY_ALLOC_REFS_CHECKPOINT), 0);
	fy_allocator_destroy(da);

	/* reopen read-only: publish must be refused, read still works */
	da = open_test_arena(dir, TEST_REGION_BASE, FY_DURABLE_ARENA_READONLY);
	ck_assert_ptr_ne(da, NULL);
	ck_assert_uint_eq(fy_allocator_refs_get(da), 0x1234);
	ck_assert_int_eq(fy_allocator_refs_publish(da, 0x1234, 0x5678, 0), -1);
	ck_assert_uint_eq(fy_allocator_refs_get(da), 0x1234);

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

START_TEST(refs_cross_session)
{
	char dir[256];
	struct fy_allocator *da;
	struct fy_allocator *a;
	const char *p;
	uint64_t w;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);

	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		rm_rf(dir);
		return;
	}
	a = da;

	p = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, "refs-payload", 13, 16);
	ck_assert_ptr_ne(p, NULL);
	w = (uint64_t)(uintptr_t)p;

	/* checkpoint: content + head durable, ordered */
	ck_assert_int_eq(fy_allocator_refs_publish(da, 0, w, FY_ALLOC_REFS_CHECKPOINT), 0);
	fy_allocator_destroy(da);

	/* reopen: head word identical, and the pointed-at content intact at the
	 * very same fixed address */
	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	ck_assert_ptr_ne(da, NULL);
	ck_assert_uint_eq(fy_allocator_refs_get(da), w);
	ck_assert_mem_eq((const void *)(uintptr_t)w, "refs-payload", 13);

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

START_TEST(refs_checkpoint_crash)
{
	char dir[256];
	struct fy_allocator *da;
	int pfd[2], status;
	pid_t pid;
	uint64_t w = 0;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);

	/* create chunk 0 up-front, then probe base availability */
	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		rm_rf(dir);
		return;
	}
	fy_allocator_destroy(da);

	ck_assert_int_eq(pipe(pfd), 0);

	pid = fork();
	if (pid == 0) {
		struct fy_allocator *cda;
		struct fy_allocator *ca;
		const char *p;
		uint64_t cw;

		close(pfd[0]);
		cda = open_test_arena(dir, TEST_REGION_BASE, 0);
		if (!cda)
			_exit(2);
		ca = cda;
		p = fy_allocator_store_nocheck(ca, FY_ALLOC_TAG_DEFAULT, "crash-content", 14, 16);
		if (!p)
			_exit(3);
		cw = (uint64_t)(uintptr_t)p;
		if (fy_allocator_refs_publish(cda, 0, cw, FY_ALLOC_REFS_CHECKPOINT) != 0)
			_exit(4);
		if (write(pfd[1], &cw, sizeof(cw)) != (ssize_t)sizeof(cw))
			_exit(5);
		/* simulate a crash right after a durable checkpoint: no close,
		 * no orderly teardown, just exit */
		_exit(0);
	}
	ck_assert_int_ge(pid, 0);
	close(pfd[1]);
	ck_assert_int_eq(read(pfd[0], &w, sizeof(w)), (ssize_t)sizeof(w));
	close(pfd[0]);
	ck_assert_int_ge(wait(&status), 0);
	ck_assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	/* reopen: the checkpoint made both the head and its content durable */
	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	ck_assert_ptr_ne(da, NULL);
	ck_assert_uint_eq(fy_allocator_refs_get(da), w);
	ck_assert_mem_eq((const void *)(uintptr_t)w, "crash-content", 14);

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

#define RC_NTHREADS	8
#define RC_PER		20000

struct rc_job {
	struct fy_allocator *da;
	int ok;
};

static void rc_worker(void *arg)
{
	struct rc_job *j = arg;
	int i;

	/* Each thread performs RC_PER successful increments of the head word via
	 * the optimistic CAS loop. If two CASes ever "succeeded" against the same
	 * observed value the final total would fall short of RC_NTHREADS*RC_PER. */
	for (i = 0; i < RC_PER; i++) {
		for (;;) {
			uint64_t e = fy_allocator_refs_get(j->da);
			int rc = fy_allocator_refs_publish(j->da, e, e + 1, 0);
			if (rc == 0)
				break;
			if (rc == 1)
				continue;	/* lost the race, retry */
			j->ok = 0;		/* unexpected error */
			return;
		}
	}
}

START_TEST(refs_cas_contention)
{
	char dir[256];
	struct fy_allocator *da;
	struct rc_job jobs[RC_NTHREADS];
	int k;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);

	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		rm_rf(dir);
		return;
	}

	for (k = 0; k < RC_NTHREADS; k++) {
		jobs[k].da = da;
		jobs[k].ok = 1;
	}
	dt_run_parallel(rc_worker, jobs, sizeof(jobs[0]), RC_NTHREADS);

	for (k = 0; k < RC_NTHREADS; k++)
		ck_assert_int_eq(jobs[k].ok, 1);

	/* every successful CAS advanced the word by exactly one; no two threads
	 * won the same round -> the count is exact */
	ck_assert_uint_eq(fy_allocator_refs_get(da),
			  (uint64_t)RC_NTHREADS * RC_PER);

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

/* ------------------------------------------------------------------ */
/* separate dedup-index region (FY_DURABLE_ARENA_SEPARATE_INDEX)       */
/* ------------------------------------------------------------------ */

/*
 * With the separate-index layout the dedup index lives in its own index-N.bin
 * file series at its own base. Content values must still land in the content
 * region (never the index region), and dedup must still work.
 */
START_TEST(durable_separate_index_basic)
{
	char dir[256], pl[80];
	struct fy_allocator *da;
	struct fy_allocator *a;
	uintptr_t cbase, cend, ibase, iend;
	const int K = 400;
	int i, bad_range = 0, bad_dedup = 0;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);
	da = open_test_arena_sep(dir, TEST_REGION_BASE, TEST_INDEX_REGION_BASE,
				 FY_DURABLE_ARENA_DEDUP | FY_DURABLE_ARENA_SEPARATE_INDEX);
	if (!da) {	/* a base was unavailable in the sandbox: soft-skip */
		rm_rf(dir);
		return;
	}

	ck_assert_uint_eq(fy_allocator_region_base(da), TEST_REGION_BASE);
	ck_assert_uint_eq(fy_allocator_region_size(da), TEST_REGION_SIZE);
	ck_assert_uint_eq(fy_allocator_index_region_base(da), TEST_INDEX_REGION_BASE);
	ck_assert_uint_eq(fy_allocator_index_region_size(da), TEST_INDEX_REGION_SIZE);

	a = da;
	ck_assert_ptr_ne(a, NULL);

	cbase = TEST_REGION_BASE;
	cend = cbase + TEST_REGION_SIZE;
	ibase = TEST_INDEX_REGION_BASE;
	iend = ibase + TEST_INDEX_REGION_SIZE;

	for (i = 0; i < K; i++) {
		size_t len = dd_payload(pl, sizeof(pl), i);
		const void *p = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, pl, len, 16);
		const void *q = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, pl, len, 16);

		if (!p || q != p)
			bad_dedup++;
		/* content must be in the content region, never the index region */
		if (!p || (uintptr_t)p < cbase || (uintptr_t)p >= cend)
			bad_range++;
		if (p && (uintptr_t)p >= ibase && (uintptr_t)p < iend)
			bad_range++;
	}
	ck_assert_int_eq(bad_dedup, 0);
	ck_assert_int_eq(bad_range, 0);

	/* the index file series was materialised, separate from arena-0.bin */
	ck_assert(durable_chunk_file_exists(dir, "index", 0));
	ck_assert(durable_chunk_file_exists(dir, "arena", 0));

	fy_allocator_sync(da);
	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

/*
 * The default (combined) layout is unchanged: no index region, no index-N.bin,
 * and the index colocates with content.
 */
START_TEST(durable_combined_no_index)
{
	char dir[256], pl[80];
	struct fy_allocator *da;
	struct fy_allocator *a;
	const int K = 200;
	int i;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);
	da = open_test_arena(dir, TEST_REGION_BASE, FY_DURABLE_ARENA_DEDUP);
	if (!da) {
		rm_rf(dir);
		return;
	}

	/* combined layout: no separate index region advertised */
	ck_assert_uint_eq(fy_allocator_index_region_base(da), 0);
	ck_assert_uint_eq(fy_allocator_index_region_size(da), 0);

	a = da;
	for (i = 0; i < K; i++) {
		size_t len = dd_payload(pl, sizeof(pl), i);
		const void *p = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, pl, len, 16);
		ck_assert_ptr_ne(p, NULL);
	}

	/* no index file series exists in combined mode */
	ck_assert(!durable_chunk_file_exists(dir, "index", 0));

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

/*
 * Separate index survives close/reopen: the recorded layout, the index series
 * and the deduped content all come back, and the same payloads dedup to the
 * identical (fixed) content addresses across sessions.
 */
START_TEST(durable_separate_index_cross_session)
{
	char dir[256], pl[80];
	struct fy_allocator *da;
	struct fy_allocator *a;
	const int K = 300;
	uintptr_t addr[300];
	int i, bad = 0;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);
	da = open_test_arena_sep(dir, TEST_REGION_BASE, TEST_INDEX_REGION_BASE,
				 FY_DURABLE_ARENA_DEDUP | FY_DURABLE_ARENA_SEPARATE_INDEX);
	if (!da) {
		rm_rf(dir);
		return;
	}
	a = da;
	for (i = 0; i < K; i++) {
		size_t len = dd_payload(pl, sizeof(pl), i);
		const void *p = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, pl, len, 16);
		ck_assert_ptr_ne(p, NULL);
		addr[i] = (uintptr_t)p;
	}
	ck_assert_int_eq(fy_allocator_sync(da), 0);
	fy_allocator_destroy(da);

	/* reopen WITHOUT passing the index geometry: it must come from boot */
	da = open_test_arena(dir, TEST_REGION_BASE, FY_DURABLE_ARENA_DEDUP);
	ck_assert_ptr_ne(da, NULL);
	ck_assert_uint_eq(fy_allocator_index_region_base(da), TEST_INDEX_REGION_BASE);

	a = da;
	for (i = 0; i < K; i++) {
		size_t len = dd_payload(pl, sizeof(pl), i);
		const void *p = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, pl, len, 16);
		if ((uintptr_t)p != addr[i])
			bad++;
	}
	ck_assert_int_eq(bad, 0);

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

/*
 * The index region grows on its own file series, independently of content:
 * enough distinct payloads (each adds an index entry) push the index past its
 * first 1 MiB chunk, so index-1.bin appears.
 */
START_TEST(durable_separate_index_grow)
{
	char dir[256], pl[80];
	struct fy_durable_allocator_cfg cfg;
	struct fy_allocator *da;
	struct fy_allocator *a;
	const int K = 20000;
	int i, bad = 0;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);

	/* a deliberately tiny index chunk so the index series grows on its own
	 * after only a few thousand entries, independently of content growth */
	memset(&cfg, 0, sizeof(cfg));
	cfg.dir = dir;
	cfg.region_base = TEST_REGION_BASE;
	cfg.region_size = TEST_REGION_SIZE;
	cfg.chunk_size = TEST_CHUNK_SIZE;
	cfg.index_region_base = TEST_INDEX_REGION_BASE;
	cfg.index_region_size = TEST_INDEX_REGION_SIZE;
	cfg.index_chunk_size = 0x10000;	/* 64 KiB */
	cfg.flags = FY_DURABLE_ARENA_CREATE | FY_DURABLE_ARENA_DEDUP |
		    FY_DURABLE_ARENA_SEPARATE_INDEX;
	da = fy_allocator_create("durable", &cfg);
	if (!da) {
		rm_rf(dir);
		return;
	}
	a = da;
	for (i = 0; i < K; i++) {
		size_t len = dd_payload(pl, sizeof(pl), i);
		const void *p = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, pl, len, 16);
		if (!p)
			bad++;
	}
	ck_assert_int_eq(bad, 0);

	/* the index file series grew beyond its first chunk */
	ck_assert(durable_chunk_file_exists(dir, "index", 1));

	fy_allocator_sync(da);
	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

/* ------------------------------------------------------------------ */
/* checkpoint / verify tests                                            */
/* ------------------------------------------------------------------ */

/*
 * Basic: checkpoint + verify pass on a fresh single-chunk arena.
 */
START_TEST(verify_checkpoint_basic)
{
	char dir[256];
	struct fy_allocator *da;
	struct fy_allocator *a;
	const char *p;
	int i;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);
	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		rm_rf(dir);
		return;
	}
	a = da;

	/* write some content */
	for (i = 0; i < 100; i++) {
		p = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, "hello", 6, 8);
		ck_assert_ptr_ne(p, NULL);
	}

	/* no checkpoint yet: verify must fail */
	ck_assert_int_eq(fy_allocator_verify(da), -1);

	/* take a checkpoint then verify should pass */
	ck_assert_int_eq(fy_allocator_checkpoint(da), 0);
	ck_assert_int_eq(fy_allocator_verify(da), 0);

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

/*
 * verify must detect a modification made to arena content after the checkpoint.
 */
START_TEST(verify_detects_corruption)
{
	char dir[256];
	struct fy_allocator *da;
	struct fy_allocator *a;
	char *p;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);
	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		rm_rf(dir);
		return;
	}
	a = da;

	p = (char *)fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, "original", 9, 8);
	ck_assert_ptr_ne(p, NULL);

	ck_assert_int_eq(fy_allocator_checkpoint(da), 0);
	ck_assert_int_eq(fy_allocator_verify(da), 0);

	/* corrupt the stored content */
	p[0] = 'X';

	/* verify must now fail */
	ck_assert_int_eq(fy_allocator_verify(da), -1);

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

/*
 * Rotating FIFO: take more than FY_DURABLE_VERIFY_SLOTS (16) checkpoints and
 * confirm that verify still finds a valid recent slot.
 */
START_TEST(verify_slot_rotation)
{
	char dir[256];
	struct fy_allocator *da;
	struct fy_allocator *a;
	int i;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);
	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		rm_rf(dir);
		return;
	}
	a = da;

	/* fill with some data so each checkpoint covers real content */
	for (i = 0; i < 200; i++) {
		const char *p = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, "data", 5, 8);
		ck_assert_ptr_ne(p, NULL);
	}

	/* take 20 checkpoints (> 16 slots) */
	for (i = 0; i < 20; i++)
		ck_assert_int_eq(fy_allocator_checkpoint(da), 0);

	/* latest slot must still be valid */
	ck_assert_int_eq(fy_allocator_verify(da), 0);

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

/*
 * Multi-chunk: force a grow so chunk 0 gets sealed, then checkpoint + verify.
 * The seal must compute chunk 0's content_hash; verify must replay correctly.
 */
START_TEST(verify_multi_chunk)
{
	char dir[256];
	struct fy_allocator *da;
	struct fy_allocator *a;
	size_t fill, chunk_cap;
	const char pad[64];

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);
	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		rm_rf(dir);
		return;
	}
	a = da;
	memset((void *)pad, 0xAB, sizeof(pad));

	/* stuff chunk 0 until it overflows into a second chunk */
	chunk_cap = TEST_CHUNK_SIZE;
	for (fill = 0; fill + sizeof(pad) < chunk_cap; fill += sizeof(pad)) {
		const char *p = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT,
							   pad, sizeof(pad), 8);
		ck_assert_ptr_ne(p, NULL);
	}
	/* a couple more to trigger the grow */
	ck_assert_ptr_ne(fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, pad, sizeof(pad), 8), NULL);
	ck_assert_ptr_ne(fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, pad, sizeof(pad), 8), NULL);

	/* must have grown to chunk 1 */
	ck_assert(durable_chunk_file_exists(dir, "arena", 1));

	ck_assert_int_eq(fy_allocator_checkpoint(da), 0);
	ck_assert_int_eq(fy_allocator_verify(da), 0);

	fy_allocator_destroy(da);
	rm_rf(dir);
}
END_TEST

/*
 * Cross-session: write + checkpoint, reopen read-only, verify must pass.
 * This validates that content_hash and verify_slots survive close/reopen.
 */
START_TEST(verify_cross_session)
{
	char dir[256];
	struct fy_allocator *da;
	struct fy_allocator *a;
	const char *p;

	ck_assert_ptr_ne(make_tmpdir(dir, sizeof(dir)), NULL);
	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	if (!da) {
		rm_rf(dir);
		return;
	}
	a = da;

	p = fy_allocator_store_nocheck(a, FY_ALLOC_TAG_DEFAULT, "persist-me", 11, 8);
	ck_assert_ptr_ne(p, NULL);

	ck_assert_int_eq(fy_allocator_checkpoint(da), 0);
	fy_allocator_destroy(da);

	/* reopen in write mode: verify must still pass without a new checkpoint */
	da = open_test_arena(dir, TEST_REGION_BASE, 0);
	ck_assert_ptr_ne(da, NULL);
	ck_assert_int_eq(fy_allocator_verify(da), 0);
	fy_allocator_destroy(da);

	rm_rf(dir);
}
END_TEST

void libfyaml_case_durable_arena(struct fy_check_suite *cs);

void libfyaml_case_durable_arena(struct fy_check_suite *cs)
{
	struct fy_check_testcase *ctc;

	ctc = fy_check_suite_add_test_case(cs, "durable-arena");

	/*
	 * Several of these are real multi-thread / multi-process stress tests
	 * (durable_threaded_grow alone is ~2.7s solo). Under a parallel
	 * "ctest -jN" run the box is heavily oversubscribed and they comfortably
	 * blow through libcheck's 4s default per-test timeout - a spurious
	 * "Test timeout expired" failure unrelated to correctness. Give the whole
	 * case a generous wall-clock budget; CK_TIMEOUT_MULTIPLIER still scales it.
	 */
	fy_check_testcase_set_timeout(ctc, 60);

	fy_check_testcase_add_test(ctc, dedup_external_attach);
	fy_check_testcase_add_test(ctc, dedup_same_pointer);
	fy_check_testcase_add_test(ctc, dedup_cross_session);
	fy_check_testcase_add_test(ctc, dedup_cross_process);
	fy_check_testcase_add_test(ctc, dedup_resize_under_load);
	fy_check_testcase_add_test(ctc, durable_roundtrip_fixed_base);
	fy_check_testcase_add_test(ctc, durable_builder_roundtrip);
	fy_check_testcase_add_test(ctc, durable_multi_chunk_grow);
	fy_check_testcase_add_test(ctc, durable_concurrent_grow);
	fy_check_testcase_add_test(ctc, durable_threaded_grow);
	fy_check_testcase_add_test(ctc, durable_crash_recovery);
	fy_check_testcase_add_test(ctc, durable_base_conflict);
	fy_check_testcase_add_test(ctc, refs_publish_basic);
	fy_check_testcase_add_test(ctc, refs_readonly_rejected);
	fy_check_testcase_add_test(ctc, refs_cross_session);
	fy_check_testcase_add_test(ctc, refs_checkpoint_crash);
	fy_check_testcase_add_test(ctc, refs_cas_contention);
	fy_check_testcase_add_test(ctc, durable_separate_index_basic);
	fy_check_testcase_add_test(ctc, durable_combined_no_index);
	fy_check_testcase_add_test(ctc, durable_separate_index_cross_session);
	fy_check_testcase_add_test(ctc, durable_separate_index_grow);
	fy_check_testcase_add_test(ctc, verify_checkpoint_basic);
	fy_check_testcase_add_test(ctc, verify_detects_corruption);
	fy_check_testcase_add_test(ctc, verify_slot_rotation);
	fy_check_testcase_add_test(ctc, verify_multi_chunk);
	fy_check_testcase_add_test(ctc, verify_cross_session);
}

#endif /* !_WIN32 */
