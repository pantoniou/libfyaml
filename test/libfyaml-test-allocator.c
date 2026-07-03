/*
 * libfyaml-test-allocator.c - libfyaml test public allocator interface
 *
 * Copyright (c) 2024 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
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
#include <limits.h>
#include <stdint.h>

#include <check.h>

#include <libfyaml.h>

#include "fy-check.h"

#include "fy-allocator.h"
#include "fy-allocator-dedup.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) ((sizeof(x)/sizeof((x)[0])))
#endif

static const char *builtin_allocators[] = {
	"linear",
	"malloc",
	"mremap",
	"dedup",
	"auto",
	NULL,
};

START_TEST(allocator_builtins)
{
	const char **pp, *p;

	/* verify that all the builtins are available */
	pp = builtin_allocators;
	while ((p = *pp++) != NULL) {
		fprintf(stderr, "checking builtin allocator: %s\n", p);
		ck_assert(fy_allocator_is_available(p));
	}
}
END_TEST

static void test_allocator_alignment(struct fy_allocator *a, int tag)
{
	size_t sz, align;
	void *p;

	/* 1, 2, 4, 8, 16 bytes allocations */
	for (sz = 1; sz <= 16; sz <<= 1) {

		align = sz;

		/* allocate and check alignment */
		p = fy_allocator_alloc(a, tag, sz, align);
		ck_assert_ptr_ne(p, NULL);
		ck_assert(((uintptr_t)p & (align - 1)) == 0);
	}
}

START_TEST(allocator_linear_buf)
{
	/* align to 64 bits */
	union {
		char buf[1024];
		uint64_t dummy;
	} u;
	struct fy_linear_allocator_cfg lcfg;
	struct fy_allocator *a = NULL;
	int tag = -1;
	void *p;

	memset(&lcfg, 0, sizeof(lcfg));
	lcfg.buf = u.buf;
	lcfg.size = sizeof(u.buf);

	/* create */
	a = fy_allocator_create("linear", &lcfg);
	ck_assert_ptr_ne(a, NULL);

	/* get the tag */
	tag = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag, -1);

	test_allocator_alignment(a, tag);

	/* allocate something too large to fit */
	p = fy_allocator_alloc(a, tag, sizeof(u.buf) + 1, 16);
	ck_assert_ptr_eq(p, NULL);

	/* destroy */
	fy_allocator_destroy(a);
}
END_TEST

START_TEST(allocator_linear_alloc)
{
	struct fy_linear_allocator_cfg lcfg;
	struct fy_allocator *a = NULL;
	int tag = -1;
	void *p;

	memset(&lcfg, 0, sizeof(lcfg));
	lcfg.size = 1024;

	/* create */
	a = fy_allocator_create("linear", &lcfg);
	ck_assert_ptr_ne(a, NULL);

	/* get the tag */
	tag = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag, -1);

	test_allocator_alignment(a, tag);

	/* allocate something too large to fit */
	p = fy_allocator_alloc(a, tag, 1024 + 1, 16);
	ck_assert_ptr_eq(p, NULL);

	/* destroy */
	fy_allocator_destroy(a);
}
END_TEST

START_TEST(allocator_malloc)
{
	struct fy_allocator *a = NULL;
	int tag0 = -1, tag1 = -1;

	/* create */
	a = fy_allocator_create("malloc", NULL);
	ck_assert_ptr_ne(a, NULL);

	/* get the first tag */
	tag0 = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag0, -1);

	/* get the second tag */
	tag1 = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag1, -1);

	/* tags must be different */
	ck_assert_int_ne(tag0, tag1);

	test_allocator_alignment(a, tag0);

	test_allocator_alignment(a, tag1);

	/* destroy */
	fy_allocator_destroy(a);
}
END_TEST

START_TEST(allocator_malloc_free_user_pointer)
{
	struct fy_allocator *a = NULL;
	int tag = -1;
	void *p;

	a = fy_allocator_create("malloc", NULL);
	ck_assert_ptr_ne(a, NULL);

	tag = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag, -1);

	p = fy_allocator_alloc(a, tag, 400, 8);
	ck_assert_ptr_ne(p, NULL);
	memset(p, 0xab, 400);
	ck_assert(fy_allocator_contains(a, tag, p));
	ck_assert(fy_allocator_contains(a, tag, (char *)p + 399));
	ck_assert(!fy_allocator_contains(a, tag, (char *)p + 400));

	fy_allocator_free(a, tag, p);
	ck_assert(!fy_allocator_contains(a, tag, p));
	fy_allocator_destroy(a);
}
END_TEST

START_TEST(allocator_mremap)
{
	struct fy_allocator *a = NULL;
	int tag0 = -1, tag1 = -1;

	/* create (default everything) */
	a = fy_allocator_create("mremap", NULL);
	ck_assert_ptr_ne(a, NULL);

	/* get the first tag */
	tag0 = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag0, -1);

	/* get the second tag */
	tag1 = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag1, -1);

	/* tags must be different */
	ck_assert_int_ne(tag0, tag1);

	test_allocator_alignment(a, tag0);

	test_allocator_alignment(a, tag1);

	/* destroy */
	fy_allocator_destroy(a);
}
END_TEST

static inline bool
scenario_is_single_tagged(int scenario)
{
	return scenario >= FYAST_SINGLE_LINEAR_RANGE &&
	       scenario <= FYAST_SINGLE_LINEAR_RANGE_DEDUP;
}

static inline FY_UNUSED bool
scenario_is_dedup(int scenario)
{
	return scenario == FYAST_PER_TAG_FREE_DEDUP ||
	       scenario == FYAST_PER_OBJ_FREE_DEDUP ||
	       scenario == FYAST_SINGLE_LINEAR_RANGE_DEDUP;
}

static inline bool
scenario_is_fixed_size(int scenario)
{
	return scenario >= FYAST_SINGLE_LINEAR_RANGE &&
	       scenario <= FYAST_SINGLE_LINEAR_RANGE_DEDUP;
}

START_TEST(allocator_auto)
{
	static const struct {
		int scenario;
		const char *name;
	} auto_scenarios[] = {
		{
			.scenario = FYAST_PER_TAG_FREE,
			.name = "per-tag-free",
		}, {
			.scenario = FYAST_PER_TAG_FREE_DEDUP,
			.name = "per-tag-free-dedup",
		}, {
			.scenario = FYAST_PER_OBJ_FREE,
			.name = "per-obj-free",
		}, {
			.scenario = FYAST_PER_OBJ_FREE_DEDUP,
			.name = "per-obj-free-dedup",
		}, {
			.scenario = FYAST_SINGLE_LINEAR_RANGE,
			.name = "single-linear-range",
		}, {
			.scenario = FYAST_SINGLE_LINEAR_RANGE_DEDUP,
			.name = "single-linear-range-dedup",
		}
	};
	struct fy_auto_allocator_cfg acfg;
	struct fy_allocator *a = NULL;
	enum fy_auto_allocator_scenario_type sc;
	int tag0 = -1, tag1 = -1;
	unsigned int i;

	/* create (default everything) */
	for (i = 0; i < ARRAY_SIZE(auto_scenarios); i++) {

		printf("scenario #%d %s\n", i, auto_scenarios[i].name);
		sc = auto_scenarios[i].scenario;

		memset(&acfg, 0, sizeof(acfg));
		acfg.scenario = sc;

		/* for fixed sizes, make it 1MB */
		if (scenario_is_fixed_size(sc))
			acfg.estimated_max_size = 1 << 20;

		a = fy_allocator_create("auto", &acfg);
		ck_assert_ptr_ne(a, NULL);

		/* get the first tag */
		tag0 = fy_allocator_get_tag(a);
		ck_assert_int_ne(tag0, -1);

		if (!scenario_is_single_tagged(sc)) {
			/* get the second tag */
			tag1 = fy_allocator_get_tag(a);
			ck_assert_int_ne(tag1, -1);

			/* tags must be different */
			ck_assert_int_ne(tag0, tag1);
		}

		test_allocator_alignment(a, tag0);

		if (!scenario_is_single_tagged(sc))
			test_allocator_alignment(a, tag1);

		/* destroy */
		fy_allocator_destroy(a);
		a = NULL;
	}
}
END_TEST

START_TEST(allocator_linear_inplace)
{
	struct fy_allocator *a;
	char buf[FY_LINEAR_ALLOCATOR_IN_PLACE_MIN_SIZE];
	int tag;
	const void *p;

	a = fy_linear_allocator_create_in_place(buf, sizeof(buf));
	ck_assert_ptr_ne(a, NULL);

	tag = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag, -1);

	p = fy_allocator_store(a, tag, "Hello", 6, 1);
	ck_assert_ptr_ne(p, NULL);

	ck_assert(!strcmp(p, "Hello"));

	/* no release */
}
END_TEST

START_TEST(allocator_dedup_inplace)
{
	struct fy_allocator *a;
	char buf[FY_DEDUP_ALLOCATOR_IN_PLACE_MIN_SIZE];
	int tag;
	const void *p1, *p2;

	a = fy_dedup_allocator_create_in_place(buf, sizeof(buf));
	ck_assert_ptr_ne(a, NULL);

	tag = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag, -1);

	p1 = fy_allocator_store(a, tag, "Hello", 6, 1);
	ck_assert_ptr_ne(p1, NULL);

	ck_assert(!strcmp(p1, "Hello"));

	p2 = fy_allocator_store(a, tag, "Hello", 6, 1);
	ck_assert_ptr_ne(p2, NULL);

	ck_assert(!strcmp(p2, "Hello"));

	/* dedup must return the same pointer */
	ck_assert(p1 == p2);

	/* no release */
}
END_TEST

static int snap_reloc_cmp(const void *va, const void *vb)
{
	const struct fy_arena_reloc *a = va, *b = vb;

	return a->src.i < b->src.i ? -1 : a->src.i > b->src.i ? 1 : 0;
}

/*
 * In-memory snapshot -> relocate -> restore round trip.
 *
 * Verifies that the dedup index survives being copied to a different base
 * (forcing real relocation) and that the restored allocator deduplicates new
 * content against the restored set, returning the rebased canonical pointer.
 */
#define SNAP_RESTORE_K 64
START_TEST(allocator_dedup_snapshot_restore)
{
	struct fy_auto_allocator_cfg acfg;
	struct fy_allocator *a = NULL, *wparent = NULL, *ra = NULL;
	struct fy_allocator_snapshot snap;
	struct fy_dedup_restore_cfg rcfg;
	struct fy_dedup_tag_data *root2;
	struct fy_arena_reloc *reloc = NULL;
	char payloads[SNAP_RESTORE_K][40];
	const void *orig_mem[SNAP_RESTORE_K];
	void *exp_mem[SNAP_RESTORE_K];
	void *image = NULL;
	size_t total, off, len;
	unsigned int nar, i, j, ai;
	int tag, rc;
	const char *newp = "brand-new-unique-payload-not-in-snapshot";
	const void *p, *pn, *pn2;

	/* dedup over per-tag-free mremap backing */
	memset(&acfg, 0, sizeof(acfg));
	acfg.scenario = FYAST_PER_TAG_FREE_DEDUP;
	a = fy_allocator_create("auto", &acfg);
	ck_assert_ptr_ne(a, NULL);

	tag = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag, -1);

	/* store K distinct payloads */
	for (i = 0; i < SNAP_RESTORE_K; i++) {
		snprintf(payloads[i], sizeof(payloads[i]), "payload-%08u-some-data", i);
		len = strlen(payloads[i]) + 1;
		orig_mem[i] = fy_allocator_store(a, tag, payloads[i], len, 1);
		ck_assert_ptr_ne(orig_mem[i], NULL);
	}

	/* sanity: re-storing returns the same pointer (dedup works pre-snapshot) */
	for (i = 0; i < SNAP_RESTORE_K; i++) {
		len = strlen(payloads[i]) + 1;
		p = fy_allocator_store(a, tag, payloads[i], len, 1);
		ck_assert_ptr_eq(p, orig_mem[i]);
	}

	/* snapshot */
	rc = fy_allocator_snapshot(a, tag, &snap);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(snap.info, NULL);
	ck_assert_ptr_ne(snap.root, NULL);

	/* tally arenas and a 64-byte-aligned total size */
	nar = 0;
	total = 0;
	for (i = 0; i < snap.info->num_tag_infos; i++) {
		for (j = 0; j < snap.info->tag_infos[i].num_arena_infos; j++) {
			total = (total + 63) & ~(size_t)63;
			total += snap.info->tag_infos[i].arena_infos[j].size;
			nar++;
		}
	}
	ck_assert_uint_gt(nar, 0);

	/* copy every arena into a fresh image at a different base (force reloc) */
	image = malloc(total);
	ck_assert_ptr_ne(image, NULL);
	reloc = malloc(sizeof(*reloc) * nar);
	ck_assert_ptr_ne(reloc, NULL);

	off = 0;
	ai = 0;
	for (i = 0; i < snap.info->num_tag_infos; i++) {
		for (j = 0; j < snap.info->tag_infos[i].num_arena_infos; j++) {
			struct fy_allocator_arena_info *arena = &snap.info->tag_infos[i].arena_infos[j];

			off = (off + 63) & ~(size_t)63;
			memcpy((char *)image + off, arena->data, arena->size);
			reloc[ai].src.p = arena->data;
			reloc[ai].srce.p = (char *)arena->data + arena->size;
			reloc[ai].dst.p = (char *)image + off;
			reloc[ai].size = arena->size;
			off += arena->size;
			ai++;
		}
	}
	qsort(reloc, nar, sizeof(*reloc), snap_reloc_cmp);

	/* rebase the merged tag-data and relocate the whole index */
	root2 = fy_arena_reloc_ptr(reloc, nar, NULL, 0, snap.root);
	ck_assert_ptr_ne(root2, NULL);
	rc = fy_dedup_index_relocate(reloc, nar, image, total, root2);
	ck_assert_int_eq(rc, 0);

	/* expected rebased content pointers */
	for (i = 0; i < SNAP_RESTORE_K; i++) {
		exp_mem[i] = fy_arena_reloc_ptr(reloc, nar, NULL, 0, orig_mem[i]);
		ck_assert_ptr_ne(exp_mem[i], NULL);
	}

	/* restore over a fresh writable backing */
	wparent = fy_allocator_create("mremap", NULL);
	ck_assert_ptr_ne(wparent, NULL);

	memset(&rcfg, 0, sizeof(rcfg));
	rcfg.base.parent_allocator = wparent;
	rcfg.base.dedup_threshold = 0;
	rcfg.root = root2;

	ra = fy_dedup_restore(FY_PARENT_ALLOCATOR_MALLOC, FY_ALLOC_TAG_NONE, &rcfg);
	ck_assert_ptr_ne(ra, NULL);

	/* every snapshotted payload must dedup to its rebased pointer */
	for (i = 0; i < SNAP_RESTORE_K; i++) {
		len = strlen(payloads[i]) + 1;
		p = fy_allocator_store(ra, FY_ALLOC_TAG_DEFAULT, payloads[i], len, 1);
		ck_assert_ptr_eq(p, exp_mem[i]);
	}

	/* a brand-new payload is unique, and dedups on a second store */
	len = strlen(newp) + 1;
	pn = fy_allocator_store(ra, FY_ALLOC_TAG_DEFAULT, newp, len, 1);
	ck_assert_ptr_ne(pn, NULL);
	pn2 = fy_allocator_store(ra, FY_ALLOC_TAG_DEFAULT, newp, len, 1);
	ck_assert_ptr_eq(pn, pn2);

	fy_allocator_destroy(ra);
	fy_allocator_destroy(wparent);
	fy_allocator_snapshot_release(a, &snap);
	free(reloc);
	free(image);
	fy_allocator_destroy(a);
}
END_TEST

/*
 * Snapshot/relocate/restore round-trip helpers, factored so several tests can
 * exercise the index merge + relocation with different source topologies
 * (in particular a tag whose index has grown into MULTIPLE tag-data layers).
 */
struct dsr_item {
	const char *buf;
	size_t len;
	const void *orig;	/* pointer the live allocator returned for buf */
};

/* count the tag-data (dtd) layers chained on a dedup tag */
static unsigned int dedup_count_dtds(struct fy_allocator *a, int tag)
{
	struct fy_dedup_allocator *da = container_of(a, struct fy_dedup_allocator, a);
	struct fy_dedup_tag *dt = &da->tags[tag];
	struct fy_dedup_tag_data *dtd;
	unsigned int n = 0;

	for (dtd = fy_atomic_load(&dt->tag_datas); dtd; dtd = dtd->next)
		n++;
	return n;
}

/*
 * Snapshot @a:@tag, copy every snapshot arena into a fresh image at a different
 * base (forcing real relocation), relocate the merged index, and restore over a
 * fresh writable backing. Verifies that every item dedups to its relocated
 * canonical pointer and that a brand-new payload is unique and self-dedups.
 *
 * The snapshot merges all of the tag's dtd layers into one table, so when @a's
 * index has several layers this drives the multi-dtd merge + relocate path end
 * to end.
 */
static void dedup_snapshot_roundtrip_verify(struct fy_allocator *a, int tag,
					    const struct dsr_item *items, unsigned int n)
{
	struct fy_allocator_snapshot snap;
	struct fy_dedup_restore_cfg rcfg;
	struct fy_dedup_tag_data *root2;
	struct fy_arena_reloc *reloc;
	struct fy_allocator *wparent, *ra;
	void *image;
	size_t total, off;
	unsigned int nar, i, j, ai, bad;
	int rc;
	const char *newp = "dsr-brand-new-unique-payload-not-in-any-snapshot";
	const void *pn, *pn2;

	rc = fy_allocator_snapshot(a, tag, &snap);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(snap.info, NULL);
	ck_assert_ptr_ne(snap.root, NULL);

	/* tally arenas + a 64-byte-aligned total */
	nar = 0;
	total = 0;
	for (i = 0; i < snap.info->num_tag_infos; i++) {
		for (j = 0; j < snap.info->tag_infos[i].num_arena_infos; j++) {
			total = (total + 63) & ~(size_t)63;
			total += snap.info->tag_infos[i].arena_infos[j].size;
			nar++;
		}
	}
	ck_assert_uint_gt(nar, 0);

	image = malloc(total);
	ck_assert_ptr_ne(image, NULL);
	reloc = malloc(sizeof(*reloc) * nar);
	ck_assert_ptr_ne(reloc, NULL);

	off = 0;
	ai = 0;
	for (i = 0; i < snap.info->num_tag_infos; i++) {
		for (j = 0; j < snap.info->tag_infos[i].num_arena_infos; j++) {
			struct fy_allocator_arena_info *arena = &snap.info->tag_infos[i].arena_infos[j];

			off = (off + 63) & ~(size_t)63;
			memcpy((char *)image + off, arena->data, arena->size);
			reloc[ai].src.p = arena->data;
			reloc[ai].srce.p = (char *)arena->data + arena->size;
			reloc[ai].dst.p = (char *)image + off;
			reloc[ai].size = arena->size;
			off += arena->size;
			ai++;
		}
	}
	qsort(reloc, nar, sizeof(*reloc), snap_reloc_cmp);

	root2 = fy_arena_reloc_ptr(reloc, nar, NULL, 0, snap.root);
	ck_assert_ptr_ne(root2, NULL);
	rc = fy_dedup_index_relocate(reloc, nar, image, total, root2);
	ck_assert_int_eq(rc, 0);

	wparent = fy_allocator_create("mremap", NULL);
	ck_assert_ptr_ne(wparent, NULL);

	memset(&rcfg, 0, sizeof(rcfg));
	rcfg.base.parent_allocator = wparent;
	rcfg.base.dedup_threshold = 0;
	rcfg.root = root2;

	ra = fy_dedup_restore(FY_PARENT_ALLOCATOR_MALLOC, FY_ALLOC_TAG_NONE, &rcfg);
	ck_assert_ptr_ne(ra, NULL);

	/*
	 * Every snapshotted payload - regardless of which source layer its entry
	 * came from - must dedup to its relocated canonical pointer. Accumulate
	 * over the loop and assert once (a per-item ck_assert would emit a
	 * libcheck mark-point syscall on every success).
	 */
	bad = 0;
	for (i = 0; i < n; i++) {
		const void *exp = fy_arena_reloc_ptr(reloc, nar, NULL, 0, items[i].orig);
		const void *p;

		if (!exp) {
			bad++;
			continue;
		}
		p = fy_allocator_store(ra, FY_ALLOC_TAG_DEFAULT, items[i].buf, items[i].len, 1);
		if (p != exp)
			bad++;
	}
	ck_assert_uint_eq(bad, 0);

	/* a brand-new payload is unique, and dedups on a second store */
	pn = fy_allocator_store(ra, FY_ALLOC_TAG_DEFAULT, newp, strlen(newp) + 1, 1);
	ck_assert_ptr_ne(pn, NULL);
	pn2 = fy_allocator_store(ra, FY_ALLOC_TAG_DEFAULT, newp, strlen(newp) + 1, 1);
	ck_assert_ptr_eq(pn, pn2);

	fy_allocator_destroy(ra);
	fy_allocator_destroy(wparent);
	fy_allocator_snapshot_release(a, &snap);
	free(reloc);
	free(image);
}

/*
 * Build a dedup allocator over mremap with a deliberately tiny table and a low
 * grow trigger so that storing @n distinct payloads forces several index
 * resizes - each resize prepends a new tag-data layer. Returns the allocator
 * (via *@dp / *@basep) and fills @items with the stored payloads.
 */
static void dedup_make_layered(struct fy_allocator **basep, struct fy_allocator **dp,
			       int *tagp, char (*payloads)[48], struct dsr_item *items,
			       unsigned int n, unsigned int bucket_count_bits,
			       unsigned int grow_trigger)
{
	struct fy_dedup_allocator_cfg dcfg;
	struct fy_allocator *base, *d;
	unsigned int i;
	int tag;

	base = fy_allocator_create("mremap", NULL);
	ck_assert_ptr_ne(base, NULL);

	memset(&dcfg, 0, sizeof(dcfg));
	dcfg.parent_allocator = base;
	dcfg.dedup_threshold = 0;		/* always dedup */
	dcfg.bucket_count_bits = bucket_count_bits;
	dcfg.bloom_filter_bits = bucket_count_bits;
	dcfg.chain_length_grow_trigger = grow_trigger;

	d = fy_allocator_create("dedup", &dcfg);
	ck_assert_ptr_ne(d, NULL);

	tag = fy_allocator_get_tag(d);
	ck_assert_int_ne(tag, -1);

	for (i = 0; i < n; i++) {
		snprintf(payloads[i], sizeof(payloads[i]), "layered-dedup-payload-%010u", i);
		items[i].buf = payloads[i];
		items[i].len = strlen(payloads[i]) + 1;
		items[i].orig = fy_allocator_store(d, tag, payloads[i], items[i].len, 1);
		ck_assert_ptr_ne(items[i].orig, NULL);
	}

	*basep = base;
	*dp = d;
	*tagp = tag;
}

/*
 * Snapshot + relocate + restore when the source index has grown into MULTIPLE
 * tag-data layers. The snapshot must re-bucket entries from every layer into
 * one merged table and relocation must rebase all of them; every payload,
 * including those whose canonical entry lives in an older (non-head) layer,
 * must still resolve after restore.
 */
#define MDM_K 1500
START_TEST(allocator_dedup_snapshot_multi_dtd)
{
	static char payloads[MDM_K][48];
	static struct dsr_item items[MDM_K];
	struct fy_allocator *base, *d;
	int tag, i, bad;

	dedup_make_layered(&base, &d, &tag, payloads, items, MDM_K,
			   4 /* 16 buckets */, 2 /* grow trigger */);

	/* the tiny table + low grow trigger must have produced several layers */
	ck_assert_uint_gt(dedup_count_dtds(d, tag), 1);

	/* the earliest payload now lives in an older, non-head layer; the layered
	 * lookup must still find it (pre-snapshot sanity) */
	{
		const void *p = fy_allocator_store(d, tag, payloads[0], items[0].len, 1);
		ck_assert_ptr_eq(p, items[0].orig);
	}

	/* the multi-dtd merge + relocate + restore round trip */
	dedup_snapshot_roundtrip_verify(d, tag, items, MDM_K);

	/* snapshot is non-destructive: the live, still-layered source keeps
	 * deduping every payload to its original pointer */
	bad = 0;
	for (i = 0; i < MDM_K; i++) {
		const void *p = fy_allocator_store(d, tag, payloads[i], items[i].len, 1);
		if (p != items[i].orig)
			bad++;
	}
	ck_assert_int_eq(bad, 0);

	fy_allocator_destroy(d);
	fy_allocator_destroy(base);
}
END_TEST

/*
 * Same merge/relocate path pushed harder: an even tinier table and the lowest
 * possible grow trigger drive a deep stack of tag-data layers, so the merge
 * collapses many versions (and the resulting collision chains in the merged
 * table) into one relocatable index.
 */
#define DDM_K 4000
START_TEST(allocator_dedup_snapshot_deep_dtd)
{
	static char payloads[DDM_K][48];
	static struct dsr_item items[DDM_K];
	struct fy_allocator *base, *d;
	int tag;

	dedup_make_layered(&base, &d, &tag, payloads, items, DDM_K,
			   2 /* 4 buckets */, 1 /* lowest grow trigger */);

	/* a deep layer stack */
	ck_assert_uint_ge(dedup_count_dtds(d, tag), 3);

	dedup_snapshot_roundtrip_verify(d, tag, items, DDM_K);

	fy_allocator_destroy(d);
	fy_allocator_destroy(base);
}
END_TEST

#define DCS_NTHREADS	16
#define DCS_NPAYLOAD	8000

/*
 * The 16-thread/8000-payload defaults keep the normal run cheap. To make the
 * dedup insert/grow race far more likely to surface (e.g. when hunting a flake),
 * oversubscribe the scheduler and/or grow the table harder via env vars:
 *   FY_TEST_DCS_THREADS   - worker count (default 16; try 128-512 to oversubscribe)
 *   FY_TEST_DCS_PAYLOADS  - unique payloads per worker (default 8000; drives grows)
 *   FY_TEST_DCS_ROUNDS    - repeat the concurrent phase N times (default 1)
 */
static int dcs_env_int(const char *name, int dflt, int lo, int hi)
{
	const char *s = getenv(name);
	long v;
	char *end;

	if (!s || !*s)
		return dflt;
	v = strtol(s, &end, 10);
	if (*end || v < lo)
		return dflt;
	if (v > hi)
		v = hi;
	return (int)v;
}

struct dcs_job {
	struct fy_allocator *a;
	int tag;
	int npayload;
	char (*payloads)[48];
	const void **canon;	/* this thread's returned pointer per payload */
	int fail;
};

static void dcs_worker(void *arg)
{
	struct dcs_job *j = arg;
	int i;

	for (i = 0; i < j->npayload; i++) {
		size_t len = strlen(j->payloads[i]) + 1;
		const void *p = fy_allocator_store(j->a, j->tag, j->payloads[i], len, 16);

		if (!p) {
			j->fail = 1;
			return;
		}
		/* the stored bytes must match what we asked for */
		if (memcmp(p, j->payloads[i], len) != 0) {
			j->fail = 2;
			return;
		}
		j->canon[i] = p;
	}
}

START_TEST(allocator_dedup_concurrent_store)
{
	struct fy_dedup_allocator_cfg dcfg;
	struct fy_allocator *base, *d;
	struct fy_thread_pool_cfg tpcfg;
	struct fy_thread_pool *tp;
	struct fy_thread **th;
	struct fy_thread_work *work;
	struct dcs_job *jobs;
	char (*payloads)[48];
	const void **canon;	/* nthreads * npayload, row-major */
	const void **ref;
	int nthreads, npayload, rounds, r, k, i, tag, bad;

	nthreads = dcs_env_int("FY_TEST_DCS_THREADS", DCS_NTHREADS, 1, 4096);
	npayload = dcs_env_int("FY_TEST_DCS_PAYLOADS", DCS_NPAYLOAD, 1, 1 << 24);
	rounds   = dcs_env_int("FY_TEST_DCS_ROUNDS", 1, 1, 1 << 20);

	th = calloc(nthreads, sizeof(*th));
	work = calloc(nthreads, sizeof(*work));
	jobs = calloc(nthreads, sizeof(*jobs));
	payloads = malloc((size_t)npayload * sizeof(*payloads));
	canon = calloc((size_t)nthreads * npayload, sizeof(*canon));
	ref = calloc(npayload, sizeof(*ref));
	ck_assert(th && work && jobs && payloads && canon && ref);

	base = fy_allocator_create("mremap", NULL);
	ck_assert_ptr_ne(base, NULL);

	memset(&dcfg, 0, sizeof(dcfg));
	dcfg.parent_allocator = base;
	dcfg.dedup_threshold = 8;

	d = fy_allocator_create("dedup", &dcfg);
	ck_assert_ptr_ne(d, NULL);

	tag = fy_allocator_get_tag(d);
	ck_assert_int_ne(tag, -1);

	for (i = 0; i < npayload; i++)
		snprintf(payloads[i], sizeof(payloads[i]),
			 "dedup-concurrent-payload-%010d", i);

	memset(&tpcfg, 0, sizeof(tpcfg));
	tpcfg.num_threads = nthreads;
	tp = fy_thread_pool_create(&tpcfg);
	ck_assert_ptr_ne(tp, NULL);

	for (k = 0; k < nthreads; k++) {
		th[k] = fy_thread_reserve(tp);
		ck_assert_ptr_ne(th[k], NULL);
	}

	/*
	 * Each round re-runs the concurrent store over the same payloads. Round 0
	 * grows the table from empty (insert race); later rounds hammer the
	 * lookup/insert race against a populated table. More rounds -> a wider
	 * window for a scheduling-sensitive race to surface.
	 */
	for (r = 0; r < rounds; r++) {
		for (k = 0; k < nthreads; k++) {
			jobs[k].a = d;
			jobs[k].tag = tag;
			jobs[k].npayload = npayload;
			jobs[k].payloads = payloads;
			jobs[k].canon = canon + (size_t)k * npayload;
			jobs[k].fail = 0;

			work[k].fn = dcs_worker;
			work[k].arg = &jobs[k];
			work[k].wp = NULL;
			ck_assert_int_eq(fy_thread_submit_work(th[k], &work[k]), 0);
		}
		for (k = 0; k < nthreads; k++)
			ck_assert_int_eq(fy_thread_wait_work(th[k]), 0);

		/* no thread hit a NULL store (fail==1) or a content mismatch
		 * (fail==2). Distinguish the two: fail==1 is an allocation failure
		 * from the parent (memory pressure), fail==2 is corruption. */
		{
			int nullfail = 0, mismatchfail = 0;

			for (k = 0; k < nthreads; k++) {
				if (jobs[k].fail == 1)
					nullfail++;
				else if (jobs[k].fail == 2)
					mismatchfail++;
			}
			if (nullfail || mismatchfail)
				fprintf(stderr, "dcs round %d: null-store=%d content-mismatch=%d\n",
					r, nullfail, mismatchfail);
			ck_assert_int_eq(mismatchfail, 0);
			ck_assert_int_eq(nullfail, 0);
		}

		/*
		 * canonical identity: every thread that stored a given payload must
		 * have received the identical pointer, even under the insert race.
		 */
		bad = 0;
		for (i = 0; i < npayload; i++) {
			ref[i] = canon[i];	/* thread 0, payload i */
			if (!ref[i])
				bad++;
			for (k = 1; k < nthreads; k++)
				if (canon[(size_t)k * npayload + i] != ref[i])
					bad++;
		}
		ck_assert_int_eq(bad, 0);
	}

	for (k = 0; k < nthreads; k++)
		fy_thread_unreserve(th[k]);
	fy_thread_pool_destroy(tp);

	/* distinct payloads -> distinct pointers, and a serial re-store still
	 * dedups to the same canonical pointer */
	bad = 0;
	for (i = 1; i < npayload; i++)
		if (ref[i] == ref[i - 1])
			bad++;
	ck_assert_int_eq(bad, 0);

	bad = 0;
	for (i = 0; i < npayload; i++) {
		size_t len = strlen(payloads[i]) + 1;
		const void *p = fy_allocator_store(d, tag, payloads[i], len, 16);
		if (p != ref[i])
			bad++;
	}
	ck_assert_int_eq(bad, 0);

	fy_allocator_destroy(d);
	fy_allocator_destroy(base);

	free(ref);
	free(canon);
	free(payloads);
	free(jobs);
	free(work);
	free(th);
}
END_TEST

/*
 * Exercise the external-root dedup wiring directly (independent of the durable
 * arena): create the index over a plain mremap parent with a local root slot,
 * then a second layer attaches to the same slot and must see the first layer's
 * stores (shared index).
 */
START_TEST(allocator_dedup_external_attach)
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

	p1 = fy_allocator_store(d1, FY_ALLOC_TAG_DEFAULT, payload, sizeof(payload), 16);
	ck_assert_ptr_ne(p1, NULL);

	/* attach: same root slot -> shared index */
	d2 = fy_dedup_create_external(base, FY_ALLOC_TAG_DEFAULT, &dcfg, &root);
	ck_assert_ptr_ne(d2, NULL);

	/* d2 sees d1's store */
	p2 = fy_allocator_lookup(d2, FY_ALLOC_TAG_DEFAULT, payload, sizeof(payload), 16);
	ck_assert_ptr_eq(p2, p1);

	/* storing the same payload via d2 dedups to the same pointer */
	p3 = fy_allocator_store(d2, FY_ALLOC_TAG_DEFAULT, payload, sizeof(payload), 16);
	ck_assert_ptr_eq(p3, p1);

	fy_allocator_destroy(d2);
	fy_allocator_destroy(d1);
	fy_allocator_destroy(base);
}
END_TEST

void libfyaml_case_allocator(struct fy_check_suite *cs)
{
	struct fy_check_testcase *ctc;

	ctc = fy_check_suite_add_test_case(cs, "allocator");

	/*
	 * allocator_dedup_concurrent_store is a 16-thread dedup stress test;
	 * under a parallel "ctest -jN" run or on a slow CI runner it can dilate
	 * well past libcheck's 4s default per-test timeout. Give the case a
	 * generous budget (CK_TIMEOUT_MULTIPLIER still scales it).
	 */
	fy_check_testcase_set_timeout(ctc, 120);

	fy_check_testcase_add_test(ctc, allocator_builtins);
	fy_check_testcase_add_test(ctc, allocator_linear_buf);
	fy_check_testcase_add_test(ctc, allocator_linear_alloc);
	fy_check_testcase_add_test(ctc, allocator_malloc);
	fy_check_testcase_add_test(ctc, allocator_malloc_free_user_pointer);
	fy_check_testcase_add_test(ctc, allocator_mremap);
	fy_check_testcase_add_test(ctc, allocator_auto);
	fy_check_testcase_add_test(ctc, allocator_linear_inplace);
	fy_check_testcase_add_test(ctc, allocator_dedup_inplace);
	fy_check_testcase_add_test(ctc, allocator_dedup_snapshot_restore);
	fy_check_testcase_add_test(ctc, allocator_dedup_snapshot_multi_dtd);
	fy_check_testcase_add_test(ctc, allocator_dedup_snapshot_deep_dtd);
	fy_check_testcase_add_test(ctc, allocator_dedup_concurrent_store);
	fy_check_testcase_add_test(ctc, allocator_dedup_external_attach);
}
