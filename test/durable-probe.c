/*
 * durable-probe.c - durable arena operations for cross-process tests
 *
 * Each invocation performs one operation on a durable arena and exits;
 * the durable-probe tests run sequences of them, optionally alternating
 * with a probe built for another runtime (FIL-C and native).
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/resource.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-allocator.h>
#include <libfyaml/libfyaml-generic.h>

#define REGION_BASE	UINT64_C(0x300000000000)
#define REGION_SIZE	(UINT64_C(16) << 20)
#define CHUNK_SIZE	(UINT64_C(64) << 10)
#define RECORD_MAGIC	UINT64_C(0x4659435245433031)
#define GC_ITEMS	32
#define MAX_WRITERS	8

/* records form a chain from the refs head, linked by handles */
struct record {
	uint64_t magic;
	uint64_t prev;
	uint64_t id;
	fy_generic number;
	unsigned char payload[16384];
};

#define CHECK(_x) \
	do { \
		if (!(_x)) { \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #_x); \
			exit(1); \
		} \
	} while (0)

static uint64_t last_sum;

static struct fy_allocator *open_arena(const char *dir, unsigned int flags)
{
	struct fy_durable_allocator_cfg cfg;
	struct fy_allocator *a;

	memset(&cfg, 0, sizeof(cfg));
	cfg.dir = dir;
	cfg.region_base = REGION_BASE;
	cfg.region_size = REGION_SIZE;
	cfg.chunk_size = CHUNK_SIZE;
	cfg.flags = FY_DURABLE_ARENA_CREATE | FY_DURABLE_ARENA_SPARSE | flags;
	a = fy_allocator_create("durable", &cfg);
	if (!a) {
		perror("open durable arena");
		exit(2);
	}
	return a;
}

/* signal readiness, then wait for the gate file to appear */
static void gate_wait(const char *gate)
{
	char ready[4096];
	unsigned int tries;
	int fd;

	CHECK(snprintf(ready, sizeof(ready), "%s.ready.%ld", gate, (long)getpid()) < (int)sizeof(ready));
	fd = open(ready, O_WRONLY | O_CREAT | O_EXCL, 0600);
	CHECK(fd >= 0);
	close(fd);

	for (tries = 0; access(gate, F_OK); tries++) {
		CHECK(errno == ENOENT && tries < 10000);
		usleep(1000);
	}
}

static const struct record *record_get(struct fy_allocator *a, uint64_t handle)
{
	/* map the chunks other processes have published since we opened */
	(void)fy_allocator_chunk_count(a);
	return fy_allocator_handle_to_ptr(a, handle);
}

static void append(struct fy_allocator *a, uint64_t id)
{
	struct fy_allocator *ma;
	struct record *r;
	const void *p;
	uint64_t old;
	int rc;

	ma = fy_allocator_create("malloc", NULL);
	CHECK(ma);
	r = fy_allocator_alloc(ma, FY_ALLOC_TAG_DEFAULT, sizeof(*r), 16);
	CHECK(r);
	memset(r, 0, sizeof(*r));
	r->magic = RECORD_MAGIC;
	r->id = id;
	r->number.v = fy_generic_in_place_uint_type(id);
	memset(r->payload, (unsigned char)id, sizeof(r->payload));

	do {
		old = fy_allocator_refs_get(a);
		r->prev = old;
		p = fy_allocator_store(a, FY_ALLOC_TAG_DEFAULT, r, sizeof(*r), 16);
		CHECK(p);
		rc = fy_allocator_refs_publish(a, old, fy_allocator_ptr_to_handle(a, p),
					       FY_ALLOC_REFS_CHECKPOINT);
		CHECK(rc >= 0);
	} while (rc);

	fy_allocator_destroy(ma);
}

/* walk the chain, verifying every record; returns the number of records */
static size_t check(struct fy_allocator *a)
{
	const struct record *r;
	uint64_t handle;
	size_t i, n;

	last_sum = 0;
	for (n = 0, handle = fy_allocator_refs_get(a); handle; n++, handle = r->prev) {
		CHECK(n < 10000);
		r = record_get(a, handle);
		CHECK(r && r->magic == RECORD_MAGIC);
		CHECK(fy_generic_get_uint_type_no_check(r->number) == r->id);
		for (i = 0; i < sizeof(r->payload); i++)
			CHECK(r->payload[i] == (unsigned char)r->id);
		last_sum += r->id;
	}
	printf("RECORDS=%zu SUM=%llu CHUNKS=%u\n", n,
	       (unsigned long long)last_sum, fy_allocator_chunk_count(a));
	return n;
}

struct worker {
	struct fy_allocator *a;
	unsigned int id;
};

static void *worker(void *arg)
{
	struct worker *w = arg;
	unsigned int i;

	for (i = 0; i < 8; i++)
		append(w->a, w->id * 100 + i);
	return NULL;
}

static bool first_checkpoint(uint64_t slot, uint64_t head, uint64_t bump,
			     uint64_t refs, void *arg)
{
	(void)head;
	(void)bump;
	(void)refs;
	*(uint64_t *)arg = slot;
	return false;
}

/* the same payloads stored twice dedup to one location; the sum of their
 * handles, kept in <dir>.sum, must agree across runs and runtimes */
static int do_dedup(const char *dir)
{
	struct fy_allocator *a;
	unsigned long long sum = 0, prev;
	const void *p, *q;
	uint64_t h;
	char buf[64], sumfile[4096];
	unsigned int i;
	FILE *fp;
	int len;

	a = open_arena(dir, FY_DURABLE_ARENA_DEDUP);
	for (i = 0; i < 64; i++) {
		len = snprintf(buf, sizeof(buf), "a deduplicated durable payload %u", i);
		p = fy_allocator_store(a, FY_ALLOC_TAG_DEFAULT, buf, (size_t)len + 1, 16);
		q = fy_allocator_store(a, FY_ALLOC_TAG_DEFAULT, buf, (size_t)len + 1, 16);
		CHECK(p && p == q && !strcmp(p, buf));
		h = fy_allocator_ptr_to_handle(a, p);
		CHECK(h >= REGION_BASE && h < REGION_BASE + REGION_SIZE);
		sum += h;
	}
	CHECK(!fy_allocator_sync(a));
	fy_allocator_destroy(a);

	CHECK(snprintf(sumfile, sizeof(sumfile), "%s.sum", dir) < (int)sizeof(sumfile));
	fp = fopen(sumfile, "r");
	if (fp) {
		CHECK(fscanf(fp, "%llu", &prev) == 1);
		fclose(fp);
		CHECK(prev == sum);
	} else {
		fp = fopen(sumfile, "w");
		CHECK(fp);
		fprintf(fp, "%llu\n", sum);
		fclose(fp);
	}
	return 0;
}

static pid_t spawn(char *argv[])
{
	pid_t pid;

	pid = fork();
	CHECK(pid >= 0);
	if (!pid) {
		execv(argv[0], argv);
		perror("exec");
		_exit(127);
	}
	return pid;
}

/* writer processes, one per probe, and a live reader start together */
static int do_concurrent(const char *self, const char *dir, int nprobes, char *probes[])
{
	struct fy_allocator *a;
	char gate[4096], ready[4096 + 64], count[32], start[32], *argv[8];
	pid_t pids[MAX_WRITERS + 1];
	uint64_t total;
	int i, n, status, tries, failed;

	CHECK(nprobes > 0 && nprobes <= MAX_WRITERS);
	CHECK(snprintf(gate, sizeof(gate), "%s.gate.%ld", dir, (long)getpid()) < (int)sizeof(gate));
	snprintf(count, sizeof(count), "%d", 1 + nprobes * 8);

	total = 42;
	for (i = 0; i < nprobes; i++) {
		snprintf(start, sizeof(start), "%d", 1000 * (i + 1));
		argv[0] = probes[i];
		argv[1] = "append";
		argv[2] = (char *)dir;
		argv[3] = "8";
		argv[4] = start;
		argv[5] = gate;
		argv[6] = NULL;
		pids[i] = spawn(argv);
		total += 8000 * (i + 1) + 28;
	}
	argv[0] = (char *)self;
	argv[1] = "watch";
	argv[2] = (char *)dir;
	argv[3] = count;
	argv[4] = gate;
	argv[5] = NULL;
	pids[nprobes] = spawn(argv);
	n = nprobes + 1;

	/* open the gate once everyone is ready */
	for (i = 0, tries = 0; i < n; ) {
		snprintf(ready, sizeof(ready), "%s.ready.%ld", gate, (long)pids[i]);
		if (!access(ready, F_OK)) {
			i++;
			continue;
		}
		CHECK(++tries < 10000);
		usleep(1000);
	}
	fclose(fopen(gate, "w"));

	failed = 0;
	for (i = 0; i < n; i++) {
		CHECK(waitpid(pids[i], &status, 0) == pids[i]);
		if (!WIFEXITED(status) || WEXITSTATUS(status)) {
			fprintf(stderr, "%s %d failed (status 0x%x)\n",
				i < nprobes ? "writer" : "reader", i, status);
			failed = 1;
		}
		snprintf(ready, sizeof(ready), "%s.ready.%ld", gate, (long)pids[i]);
		unlink(ready);
	}
	unlink(gate);
	CHECK(!failed);

	a = open_arena(dir, 0);
	CHECK(check(a) == (size_t)(1 + nprobes * 8));
	CHECK(last_sum == total);
	fy_allocator_destroy(a);
	return 0;
}

/* a pointer kept across a reopen; under FIL-C reading it traps */
static int do_stale(const char *dir)
{
	struct rlimit rl = { 0, 0 };
	struct fy_allocator *a;
	const struct record *r;
	int status;
	pid_t pid;

	pid = fork();
	CHECK(pid >= 0);
	if (!pid) {
		setrlimit(RLIMIT_CORE, &rl);
		a = open_arena(dir, 0);
		r = record_get(a, fy_allocator_refs_get(a));
		CHECK(r);
		fy_allocator_destroy(a);
		a = open_arena(dir, 0);
		CHECK(check(a));
		printf("STALE=%llu\n", (unsigned long long)((volatile const struct record *)r)->id);
		_exit(0);
	}
	CHECK(waitpid(pid, &status, 0) == pid);
#ifdef __FILC__
	CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTRAP);
#else
	CHECK(WIFEXITED(status) && !WEXITSTATUS(status));
#endif
	return 0;
}

/* publish a sequence of strings as a generic root */
static int do_gc_seed(const char *dir)
{
	struct fy_generic_builder_cfg gb_cfg;
	struct fy_generic_builder *gb;
	struct fy_allocator *a;
	fy_generic items[GC_ITEMS], root;
	char buf[64];
	unsigned int i;

	a = open_arena(dir, 0);
	memset(&gb_cfg, 0, sizeof(gb_cfg));
	gb_cfg.flags = FYGBCF_SCOPE_LEADER;
	gb_cfg.allocator = a;
	gb = fy_generic_builder_create(&gb_cfg);
	CHECK(gb);
	for (i = 0; i < GC_ITEMS; i++) {
		snprintf(buf, sizeof(buf), "a string the collector must keep %u", i);
		items[i] = fy_gb_string_create_out_of_place(gb, buf);
		CHECK(fy_generic_is_valid(items[i]));
	}
	root = fy_gb_sequence_create(gb, GC_ITEMS, items);
	CHECK(fy_generic_is_valid(root));
	CHECK(!fy_generic_refs_publish(a, fy_invalid, root, FY_ALLOC_REFS_CHECKPOINT));
	fy_generic_builder_destroy(gb);
	fy_allocator_destroy(a);
	return 0;
}

static int do_gc_read(const char *dir)
{
	struct fy_allocator *a;
	const fy_generic *items;
	fy_generic root;
	size_t count;
	char buf[64];
	unsigned int i;

	a = open_arena(dir, 0);
	root = fy_generic_refs_get(a);
	CHECK(fy_generic_is_sequence(root));
	items = fy_generic_sequence_get_items(root, &count);
	CHECK(count == GC_ITEMS);
	for (i = 0; i < GC_ITEMS; i++) {
		snprintf(buf, sizeof(buf), "a string the collector must keep %u", i);
		CHECK(!strcmp(fy_cast(items[i], ""), buf));
	}
	fy_allocator_destroy(a);
	return 0;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: durable-probe runtime\n"
		"       durable-probe <op> <dir> [args]\n"
		"       durable-probe concurrent <dir> <probe>...\n"
		"ops: seed crash append read readonly watch cas threads checkpoint\n"
		"     verify recover stale dedup gc-seed gc gc-read\n");
	exit(2);
}

int main(int argc, char *argv[])
{
	struct fy_allocator *a;
	struct worker w[4];
	const char *op, *dir;
	unsigned int flags, count, start, i;
	pthread_t t[4];
	uint64_t root, slot;
	size_t n, old;

	if (argc == 2 && !strcmp(argv[1], "runtime")) {
#ifdef __FILC__
		puts("filc");
#else
		puts("native");
#endif
		return 0;
	}
	if (argc < 3)
		usage();

	op = argv[1];
	dir = argv[2];

	if (!strcmp(op, "dedup"))
		return do_dedup(dir);
	if (!strcmp(op, "concurrent"))
		return do_concurrent(argv[0], dir, argc - 3, argv + 3);
	if (!strcmp(op, "stale"))
		return do_stale(dir);
	if (!strcmp(op, "gc-seed"))
		return do_gc_seed(dir);
	if (!strcmp(op, "gc-read"))
		return do_gc_read(dir);
	if (!strcmp(op, "gc")) {
		CHECK(!fy_durable_arena_gc(dir));
		return 0;
	}

	flags = 0;
	if (!strcmp(op, "readonly") || !strcmp(op, "watch"))
		flags |= FY_DURABLE_ARENA_READONLY;
	if (!strcmp(op, "checkpoint") || !strcmp(op, "verify") || !strcmp(op, "recover"))
		flags |= FY_DURABLE_ARENA_CHECKPOINT;
	a = open_arena(dir, flags);

	if (!strcmp(op, "seed") || !strcmp(op, "crash")) {
		CHECK(!fy_allocator_refs_get(a));
		append(a, 42);
		CHECK(check(a) == 1);
		if (!strcmp(op, "crash"))
			_exit(0);	/* no cleanup */
	} else if (!strcmp(op, "append")) {
		count = argc > 3 ? (unsigned int)strtoul(argv[3], NULL, 0) : 24;
		start = argc > 4 ? (unsigned int)strtoul(argv[4], NULL, 0) : 100;
		if (argc > 5)
			gate_wait(argv[5]);
		for (i = 0; i < count; i++)
			append(a, start + i);
		check(a);
	} else if (!strcmp(op, "read") || !strcmp(op, "readonly")) {
		n = check(a);
		if (argc > 3)
			CHECK(n == strtoul(argv[3], NULL, 0));
		if (argc > 4)
			CHECK(last_sum == strtoull(argv[4], NULL, 0));
		root = fy_allocator_refs_get(a);
		if (flags & FY_DURABLE_ARENA_READONLY)
			CHECK(fy_allocator_refs_publish(a, root, root, 0) == -1);
	} else if (!strcmp(op, "watch")) {
		/* a live reader sees only consistent, growing prefixes */
		if (argc != 5)
			usage();
		count = (unsigned int)strtoul(argv[3], NULL, 0);
		old = check(a);
		CHECK(old == 1);
		gate_wait(argv[4]);
		for (i = 0, n = old; n < count; i++) {
			CHECK(i < 10000);
			n = check(a);
			CHECK(n >= old && n <= count);
			old = n;
			if (n < count)
				usleep(1000);
		}
	} else if (!strcmp(op, "cas")) {
		root = fy_allocator_refs_get(a);
		CHECK(root);
		CHECK(fy_allocator_refs_publish(a, 0, root, 0) == 1);
		CHECK(fy_allocator_refs_get(a) == root);
		CHECK(fy_allocator_refs_publish(a, root, root, 0) == 0);
	} else if (!strcmp(op, "threads")) {
		for (i = 0; i < 4; i++) {
			w[i].a = a;
			w[i].id = i + 1;
			CHECK(!pthread_create(&t[i], NULL, worker, &w[i]));
		}
		for (i = 0; i < 4; i++)
			CHECK(!pthread_join(t[i], NULL));
		CHECK(check(a) == 32);
	} else if (!strcmp(op, "checkpoint")) {
		append(a, 100);
		CHECK(!fy_allocator_checkpoint(a));
		CHECK(!fy_allocator_verify(a));
	} else if (!strcmp(op, "verify")) {
		CHECK(!fy_allocator_verify(a));
		check(a);
	} else if (!strcmp(op, "recover")) {
		CHECK(!fy_allocator_refs_get(a));
		append(a, 42);
		CHECK(!fy_allocator_checkpoint(a));
		slot = 0;
		CHECK(!fy_allocator_checkpoint_iterate(a, first_checkpoint, &slot) && slot);
		for (i = 0; i < 8; i++)
			append(a, 100 + i);
		CHECK(!fy_allocator_checkpoint(a));
		CHECK(!fy_allocator_verify(a));
		CHECK(check(a) == 9);
		CHECK(!fy_allocator_checkpoint_recover(a, slot));
		CHECK(check(a) == 1);
	} else
		usage();

	fy_allocator_destroy(a);
	return 0;
}
