/*
 * fy-allocator-auto.c - automatic allocator
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdalign.h>
#include <time.h>
#include <inttypes.h>
#include <math.h>

#include <stdio.h>

#include "fy-utils.h"
#include "fy-allocator-mremap.h"
#include "fy-allocator-dedup.h"
#include "fy-allocator-linear.h"
#include "fy-allocator-auto.h"

/* fixed parameters */
#ifndef AUTO_ALLOCATOR_BIG_ALLOC_THRESHOLD
#define AUTO_ALLOCATOR_BIG_ALLOC_THRESHOLD	SIZE_MAX
#endif

#ifndef AUTO_ALLOCATOR_EMPTY_THRESHOLD
#define AUTO_ALLOCATOR_EMPTY_THRESHOLD		64
#endif

#ifndef AUTO_ALLOCATOR_GROW_RATIO
#define AUTO_ALLOCATOR_GROW_RATIO		1.5
#endif

#ifndef AUTO_ALOCATOR_BALLOON_RATIO
#define AUTO_ALOCATOR_BALLOON_RATIO		8.0
#endif

#ifndef AUTO_ALLOCATOR_ARENA_TYPE
#define AUTO_ALLOCATOR_ARENA_TYPE		FYMRAT_MMAP
#endif

#ifndef AUTO_ALLOCATOR_MINIMUM_ARENA_SIZE
#define AUTO_ALLOCATOR_MINIMUM_ARENA_SIZE	((size_t)16 << 20)	/* 16 MB */
#endif

#ifndef AUTO_ALLOCATOR_DEFAULT_ESTIMATED_MAX_SIZE
#define AUTO_ALLOCATOR_DEFAULT_ESTIMATED_MAX_SIZE ((size_t)1 << 20)	/* 1MB */
#endif

#ifndef AUTO_ALLOCATOR_DEFAULT_BLOOM_FILTER_BITS
#define AUTO_ALLOCATOR_DEFAULT_BLOOM_FILTER_BITS	0
#endif

#ifndef AUTO_ALLOCATOR_DEFAULT_BUCKET_COUNT_BITS
#define AUTO_ALLOCATOR_DEFAULT_BUCKET_COUNT_BITS	0
#endif

#ifndef AUTO_ALLOCATOR_DEFAULT_DEDUP_THRESHOLD
#define AUTO_ALLOCATOR_DEFAULT_DEDUP_THRESHOLD		0	/* dedup everything */
#endif

#ifndef AUTO_ALLOCATOR_DEFAULT_CHAIN_LENGTH_GROW_TRIGGER
#define AUTO_ALLOCATOR_DEFAULT_CHAIN_LENGTH_GROW_TRIGGER	0	/* let dedup decide */
#endif

static const struct fy_auto_allocator_cfg default_cfg = {
	.scenario = FYAST_PER_TAG_FREE_DEDUP,
	.estimated_max_size = AUTO_ALLOCATOR_DEFAULT_ESTIMATED_MAX_SIZE,
};

static void fy_auto_cleanup(struct fy_allocator *a);

static int fy_auto_setup(struct fy_allocator *a, const void *cfg_data)
{
	size_t pagesz, size;
	struct fy_auto_allocator *aa;
	const struct fy_auto_allocator_cfg *cfg;
	struct fy_mremap_allocator_cfg mrcfg;
	struct fy_dedup_allocator_cfg dcfg;
	struct fy_linear_allocator_cfg lcfg;
	struct fy_allocator *mra = NULL, *da = NULL, *ma = NULL, *la = NULL;
	struct fy_allocator *topa = NULL, *suba = NULL;

	if (!a)
		return -1;

	cfg = cfg_data ? cfg_data : &default_cfg;

	aa = container_of(a, struct fy_auto_allocator, a);
	memset(aa, 0, sizeof(*aa));
	aa->a.name = "auto";
	aa->a.ops = &fy_auto_allocator_ops;
	aa->cfg = *cfg;

	pagesz = sysconf(_SC_PAGESIZE);
	size = cfg->estimated_max_size && cfg->estimated_max_size != SIZE_MAX ?
			cfg->estimated_max_size :
			AUTO_ALLOCATOR_MINIMUM_ARENA_SIZE;

	/* first allocator */
	switch (cfg->scenario) {
	case FYAST_PER_TAG_FREE:
	case FYAST_PER_TAG_FREE_DEDUP:
		memset(&mrcfg, 0, sizeof(mrcfg));
		mrcfg.big_alloc_threshold = AUTO_ALLOCATOR_BIG_ALLOC_THRESHOLD;
		mrcfg.empty_threshold = AUTO_ALLOCATOR_EMPTY_THRESHOLD;
		mrcfg.grow_ratio = AUTO_ALLOCATOR_GROW_RATIO;
		mrcfg.balloon_ratio = AUTO_ALOCATOR_BALLOON_RATIO;
		mrcfg.arena_type = AUTO_ALLOCATOR_ARENA_TYPE;

		mrcfg.minimum_arena_size = fy_size_t_align(size, pagesz);

		mra = fy_allocator_create("mremap", &mrcfg);
		if (!mra)
			goto err_out;
		topa = mra;

		break;

	case FYAST_PER_OBJ_FREE:
	case FYAST_PER_OBJ_FREE_DEDUP:
		ma = fy_allocator_create("malloc", NULL);
		if (!ma)
			goto err_out;
		topa = ma;
		break;

	case FYAST_SINGLE_LINEAR_RANGE:
	case FYAST_SINGLE_LINEAR_RANGE_DEDUP:
		memset(&lcfg, 0, sizeof(lcfg));
		lcfg.size = fy_size_t_align(size, pagesz);

		la = fy_allocator_create("linear", &lcfg);
		if (!la)
			goto err_out;
		topa = la;
		break;

	default:
		goto err_out;
	}

	if (!topa)
		goto err_out;

	/* stack the dedup */
	switch (cfg->scenario) {
	case FYAST_PER_TAG_FREE_DEDUP:
	case FYAST_PER_OBJ_FREE_DEDUP:
	case FYAST_SINGLE_LINEAR_RANGE_DEDUP:
		memset(&dcfg, 0, sizeof(dcfg));
		dcfg.parent_allocator = topa;
		dcfg.bloom_filter_bits = AUTO_ALLOCATOR_DEFAULT_BLOOM_FILTER_BITS;
		dcfg.bucket_count_bits = AUTO_ALLOCATOR_DEFAULT_BUCKET_COUNT_BITS;
		dcfg.dedup_threshold = AUTO_ALLOCATOR_DEFAULT_DEDUP_THRESHOLD;
		dcfg.chain_length_grow_trigger = AUTO_ALLOCATOR_DEFAULT_CHAIN_LENGTH_GROW_TRIGGER;
		dcfg.estimated_content_size = size;

		da = fy_allocator_create("dedup", &dcfg);
		if (!da)
			goto err_out;
		/* the top is sub now */
		suba = topa;
		topa = da;

		break;

		/* nothing to stack for these */
	case FYAST_PER_TAG_FREE:
	case FYAST_PER_OBJ_FREE:
	case FYAST_SINGLE_LINEAR_RANGE:
		break;

	default:
		goto err_out;
	}

	/* OK, assign the allocators now */
	aa->parent_allocator = topa;
	aa->sub_parent_allocator = suba;

	return 0;

err_out:
	fy_allocator_destroy(suba);
	fy_allocator_destroy(topa);
	fy_auto_cleanup(a);
	return -1;
}

static void fy_auto_cleanup(struct fy_allocator *a)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return;

	aa = container_of(a, struct fy_auto_allocator, a);

	/* order is important! */
	if (aa->parent_allocator) {
		fy_allocator_destroy(aa->parent_allocator);
		aa->parent_allocator = NULL;
	}

	if (aa->sub_parent_allocator) {
		fy_allocator_destroy(aa->sub_parent_allocator);
		aa->sub_parent_allocator = NULL;
	}
}

struct fy_allocator *fy_auto_create(const void *cfg)
{
	struct fy_auto_allocator *aa = NULL;
	int rc;

	aa = malloc(sizeof(*aa));
	if (!aa)
		goto err_out;

	rc = fy_auto_setup(&aa->a, cfg);
	if (rc)
		goto err_out;

	return &aa->a;

err_out:
	if (aa)
		free(aa);

	return NULL;
}

void fy_auto_destroy(struct fy_allocator *a)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return;

	aa = container_of(a, struct fy_auto_allocator, a);

	fy_auto_cleanup(a);

	free(aa);
}

void fy_auto_dump(struct fy_allocator *a)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return;

	aa = container_of(a, struct fy_auto_allocator, a);

	fy_allocator_dump(aa->parent_allocator);
}

static void *fy_auto_alloc(struct fy_allocator *a, int tag, size_t size, size_t align)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return NULL;

	aa = container_of(a, struct fy_auto_allocator, a);

	return fy_allocator_alloc(aa->parent_allocator, tag, size, align);
}

static void fy_auto_free(struct fy_allocator *a, int tag, void *data)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return;

	aa = container_of(a, struct fy_auto_allocator, a);

	fy_allocator_free(aa->parent_allocator, tag, data);
}

static int fy_auto_update_stats(struct fy_allocator *a, int tag, struct fy_allocator_stats *stats)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return -1;

	aa = container_of(a, struct fy_auto_allocator, a);

	return fy_allocator_update_stats(aa->parent_allocator, tag, stats);
}

static const void *fy_auto_store(struct fy_allocator *a, int tag, const void *data, size_t size, size_t align)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return NULL;

	aa = container_of(a, struct fy_auto_allocator, a);

	return fy_allocator_store(aa->parent_allocator, tag, data, size, align);
}

static const void *fy_auto_storev(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return NULL;

	aa = container_of(a, struct fy_auto_allocator, a);

	return fy_allocator_storev(aa->parent_allocator, tag, iov, iovcnt, align);
}

static void fy_auto_release(struct fy_allocator *a, int tag, const void *data, size_t size)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return;

	aa = container_of(a, struct fy_auto_allocator, a);

	fy_allocator_release(aa->parent_allocator, tag, data, size);
}

static int fy_auto_get_tag(struct fy_allocator *a)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return FY_ALLOC_TAG_ERROR;

	/* TODO, convert tag config? */

	aa = container_of(a, struct fy_auto_allocator, a);

	return fy_allocator_get_tag(aa->parent_allocator);
}

static void fy_auto_release_tag(struct fy_allocator *a, int tag)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return;

	aa = container_of(a, struct fy_auto_allocator, a);

	fy_allocator_release_tag(aa->parent_allocator, tag);
}

static void fy_auto_trim_tag(struct fy_allocator *a, int tag)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return;

	aa = container_of(a, struct fy_auto_allocator, a);

	fy_allocator_trim_tag(aa->parent_allocator, tag);
}

static void fy_auto_reset_tag(struct fy_allocator *a, int tag)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return;

	aa = container_of(a, struct fy_auto_allocator, a);

	fy_allocator_reset_tag(aa->parent_allocator, tag);
}

static struct fy_allocator_info *
fy_auto_get_info(struct fy_allocator *a, int tag)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return NULL;

	aa = container_of(a, struct fy_auto_allocator, a);

	return fy_allocator_get_info(aa->parent_allocator, tag);
}

const struct fy_allocator_ops fy_auto_allocator_ops = {
	.setup = fy_auto_setup,
	.cleanup = fy_auto_cleanup,
	.create = fy_auto_create,
	.destroy = fy_auto_destroy,
	.dump = fy_auto_dump,
	.alloc = fy_auto_alloc,
	.free = fy_auto_free,
	.update_stats = fy_auto_update_stats,
	.store = fy_auto_store,
	.storev = fy_auto_storev,
	.release = fy_auto_release,
	.get_tag = fy_auto_get_tag,
	.release_tag = fy_auto_release_tag,
	.trim_tag = fy_auto_trim_tag,
	.reset_tag = fy_auto_reset_tag,
	.get_info = fy_auto_get_info,
};
