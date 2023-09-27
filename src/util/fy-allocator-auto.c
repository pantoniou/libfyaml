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

static const struct fy_auto_setup_data default_setup_data = {
	.scenario = FYAST_BALANCED,
	.estimated_max_size = 1048576,	/* 1MB */
};

static void fy_auto_cleanup(struct fy_allocator *a);

static int fy_auto_setup(struct fy_allocator *a, const void *data)
{
	size_t pagesz;
	struct fy_auto_allocator *aa;
	const struct fy_auto_setup_data *d;
	struct fy_mremap_setup_data mrsetupdata;
	struct fy_dedup_setup_data dsetupdata;
	struct fy_allocator *mra = NULL, *da = NULL;

	if (!a)
		return -1;

	pagesz = sysconf(_SC_PAGESIZE);
	d = data ? data : &default_setup_data;

	aa = container_of(a, struct fy_auto_allocator, a);
	memset(aa, 0, sizeof(*aa));
	aa->a.name = "auto";
	aa->a.ops = &fy_auto_allocator_ops;

	memset(&mrsetupdata, 0, sizeof(mrsetupdata));
	mrsetupdata.big_alloc_threshold = SIZE_MAX;
	mrsetupdata.empty_threshold = 64;
	mrsetupdata.grow_ratio = 1.5;
	mrsetupdata.balloon_ratio = 8.0;
	mrsetupdata.arena_type = FYMRAT_MMAP;

	if (d->estimated_max_size && d->estimated_max_size != SIZE_MAX)
		mrsetupdata.minimum_arena_size = fy_size_t_align(d->estimated_max_size, pagesz);
	else
		mrsetupdata.minimum_arena_size = fy_size_t_align(16 << 20, pagesz);	/* 16 MB */

	fprintf(stderr, "mrsetupdata.minimum_arena_size=%zu\n", mrsetupdata.minimum_arena_size);

	mra = fy_allocator_create("mremap", &mrsetupdata);
	if (!mra)
		goto err_out;

	/* TODO switch to malloc for valgrind and asan check mode */
	if (d->scenario == FYAST_FASTEST) {
		aa->parent_allocator = mra;
		aa->sub_parent_allocator = NULL;
		mra = NULL;
	} else {
		memset(&dsetupdata, 0, sizeof(dsetupdata));
		dsetupdata.parent_allocator = mra;
		dsetupdata.bloom_filter_bits = 0;	/* use default */
		dsetupdata.bucket_count_bits = 0;
		dsetupdata.estimated_content_size = mrsetupdata.minimum_arena_size;

		da = fy_allocator_create("dedup", &dsetupdata);
		if (!da)
			goto err_out;

		aa->parent_allocator = da;
		aa->sub_parent_allocator = mra;

		da = NULL;
		mra = NULL;
	}

	return 0;
err_out:
	if (da)
		fy_allocator_destroy(da);
	if (mra)
		fy_allocator_destroy(mra);
	fy_auto_cleanup(a);
	return -1;
}

static void fy_auto_cleanup(struct fy_allocator *a)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return;

	aa = container_of(a, struct fy_auto_allocator, a);

	if (aa->sub_parent_allocator) {
		fy_allocator_destroy(aa->sub_parent_allocator);
		aa->sub_parent_allocator = NULL;
	}

	if (aa->parent_allocator) {
		fy_allocator_destroy(aa->parent_allocator);
		aa->parent_allocator = NULL;
	}
}

struct fy_allocator *fy_auto_create(const void *setupdata)
{
	struct fy_auto_allocator *aa = NULL;
	int rc;

	aa = malloc(sizeof(*aa));
	if (!aa)
		goto err_out;

	rc = fy_auto_setup(&aa->a, setupdata);
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

static void *fy_auto_alloc(struct fy_allocator *a, fy_alloc_tag tag, size_t size, size_t align)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return NULL;

	aa = container_of(a, struct fy_auto_allocator, a);

	return fy_allocator_alloc(aa->parent_allocator, tag, size, align);
}

static void fy_auto_free(struct fy_allocator *a, fy_alloc_tag tag, void *data)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return;

	aa = container_of(a, struct fy_auto_allocator, a);

	fy_allocator_free(aa->parent_allocator, tag, data);
}

static int fy_auto_update_stats(struct fy_allocator *a, fy_alloc_tag tag, struct fy_allocator_stats *stats)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return -1;

	aa = container_of(a, struct fy_auto_allocator, a);

	return fy_allocator_update_stats(aa->parent_allocator, tag, stats);
}

static const void *fy_auto_store(struct fy_allocator *a, fy_alloc_tag tag, const void *data, size_t size, size_t align)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return NULL;

	aa = container_of(a, struct fy_auto_allocator, a);

	return fy_allocator_store(aa->parent_allocator, tag, data, size, align);
}

static const void *fy_auto_storev(struct fy_allocator *a, fy_alloc_tag tag, const struct fy_iovecw *iov, unsigned int iovcnt, size_t align)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return NULL;

	aa = container_of(a, struct fy_auto_allocator, a);

	return fy_allocator_storev(aa->parent_allocator, tag, iov, iovcnt, align);
}

static void fy_auto_release(struct fy_allocator *a, fy_alloc_tag tag, const void *data, size_t size)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return;

	aa = container_of(a, struct fy_auto_allocator, a);

	fy_allocator_release(aa->parent_allocator, tag, data, size);
}

static fy_alloc_tag fy_auto_get_tag(struct fy_allocator *a, const void *tag_config)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return FY_ALLOC_TAG_ERROR;

	/* TODO, convert tag config? */

	aa = container_of(a, struct fy_auto_allocator, a);

	return fy_allocator_get_tag(aa->parent_allocator, NULL);
}

static void fy_auto_release_tag(struct fy_allocator *a, fy_alloc_tag tag)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return;

	aa = container_of(a, struct fy_auto_allocator, a);

	fy_allocator_release_tag(aa->parent_allocator, tag);
}

static void fy_auto_trim_tag(struct fy_allocator *a, fy_alloc_tag tag)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return;

	aa = container_of(a, struct fy_auto_allocator, a);

	fy_allocator_trim_tag(aa->parent_allocator, tag);
}

static void fy_auto_reset_tag(struct fy_allocator *a, fy_alloc_tag tag)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return;

	aa = container_of(a, struct fy_auto_allocator, a);

	fy_allocator_reset_tag(aa->parent_allocator, tag);
}

static struct fy_allocator_info *
fy_auto_get_info(struct fy_allocator *a, fy_alloc_tag tag)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return NULL;

	aa = container_of(a, struct fy_auto_allocator, a);

	return fy_allocator_get_info(aa->parent_allocator, tag);
}

static const void *fy_auto_get_single_area(struct fy_allocator *a, fy_alloc_tag tag, size_t *sizep, size_t *startp, size_t *allocp)
{
	struct fy_auto_allocator *aa;

	if (!a)
		return NULL;

	aa = container_of(a, struct fy_auto_allocator, a);

	return fy_allocator_get_single_area(aa->parent_allocator, tag, sizep, startp, allocp);
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
	.get_single_area = fy_auto_get_single_area,
};
