/*
 * fy-allocator-mremap.c - mremap allocator
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
#include <sys/mman.h>

#include <stdio.h>

/* for container_of */
#include "fy-list.h"
#include "fy-utils.h"

#include "fy-allocator-mremap.h"

// #define DEBUG_ARENA

// #define DISABLE_MREMAP

#if HAVE_MREMAP && !defined(DISABLE_MREMAP)
#define USE_MREMAP 1
#else
#define USE_MREMAP 0
#endif

#ifndef MREMAP_ALLOCATOR_DEFAULT_BIG_ALLOC_THRESHOLD
#define MREMAP_ALLOCATOR_DEFAULT_BIG_ALLOC_THRESHOLD	SIZE_MAX
#endif

#ifndef MREMAP_ALLOCATOR_DEFAULT_EMPTY_THRESHOLD
#define MREMAP_ALLOCATOR_DEFAULT_EMPTY_THRESHOLD	64
#endif

#ifndef MREMAP_ALLOCATOR_DEFAULT_MINIMUM_ARENA_SIZE
#define MREMAP_ALLOCATOR_DEFAULT_MINIMUM_ARENA_SIZE	(1U << 20)	/* 1M */
#endif

#ifndef MREMAP_ALLOCATOR_DEFAULT_GROW_RATIO
#define MREMAP_ALLOCATOR_DEFAULT_GROW_RATIO		2.0
#endif

#ifndef MREMAP_ALLOCATOR_DEFAULT_BALLON_RATIO
#define MREMAP_ALLOCATOR_DEFAULT_BALLON_RATIO		32.0
#endif

#ifndef MREMAP_ALLOCATOR_DEFAULT_ARENA_TYPE
#define MREMAP_ALLOCATOR_DEFAULT_ARENA_TYPE		FYMRAT_MMAP
#endif

static struct fy_mremap_arena *
fy_mremap_arena_create(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt, size_t size)
{
	struct fy_mremap_arena *mran = NULL;
	void *mem;
	size_t size_page_align, balloon_size;
#if !USE_MREMAP
	int rc FY_DEBUG_UNUSED;
#endif

	if (size < mra->minimum_arena_size)
		size = mra->minimum_arena_size;

	size_page_align = fy_size_t_align(size + FY_MREMAP_ARENA_OVERHEAD, mra->pagesz);
	switch (mra->arena_type) {
	case FYMRAT_MALLOC:
		mran = calloc(1, size_page_align);
		break;

	case FYMRAT_MMAP:
		/* allocate an initial ballooned size */
		balloon_size = fy_size_t_align((size_t)(size_page_align * mra->balloon_ratio), mra->pagesz);
		if (balloon_size == size_page_align)
			balloon_size = size_page_align + mra->pagesz;
		mem = mmap(NULL, balloon_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		if (mem == MAP_FAILED) {
			/* first allocation failed, that's ok, try again */
			mran = mmap(NULL, size_page_align, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		} else {
#if USE_MREMAP
			mran = mremap(mem, balloon_size, size_page_align, 0);
			/* either it fails, or it moves we handle it */
#else
			/* we don't shrink, we just unmap over the limit  */
			rc = munmap(mem + size_page_align, balloon_size - size_page_align);
			if (rc) {
#ifdef DEBUG_ARENA
				fprintf(stderr, "%s: failed to unmap for shink\n", __func__);
#endif
				size_page_align = balloon_size;	/* keep the balloon size? */
			}
			mran = mem;
#endif
		}
		if (mran == MAP_FAILED)
			return NULL;
		break;

	default:
		/* should never happen */
		assert(0);
		abort();
		break;
	}

	if (!mran)
		return NULL;
	mran->size = size_page_align;
	mran->next = FY_MREMAP_ARENA_OVERHEAD;

#ifdef DEBUG_ARENA
	fprintf(stderr, "%s: #%zu created arena %p size=%zu (%zuMB)\n", __func__, mrt - mra->tags, mran, mran->size, mran->size >> 20);
#endif

	return mran;
}

static void fy_mremap_arena_destroy(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt, struct fy_mremap_arena *mran)
{
	if (!mran)
		return;

#ifdef DEBUG_ARENA
	fprintf(stderr, "%s: #%zu destroy arena %p size=%zu (%zuMB)\n", __func__, mrt - mra->tags, mran, mran->size, mran->size >> 20);
#endif
	switch (mra->arena_type) {
	case FYMRAT_MALLOC:
		free(mran);
		break;

	case FYMRAT_MMAP:
		(void)munmap(mran, mran->size);
		break;

	default:
		/* should never happen */
		assert(0);
		abort();
		break;
	}
}

static int fy_mremap_arena_grow(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt, struct fy_mremap_arena *mran,
		size_t size, size_t align)
{
	void *mem;

	if (!mran || !size)
		return -1;

	switch (mra->arena_type) {
	case FYMRAT_MALLOC:
		/* can't grow malloc without moving the pointer */
		break;

	case FYMRAT_MMAP:

		/* if the grow needs to be larger than double, don't bother */
		if (fy_size_t_align(mran->next, align) + size > 2 * mran->size)
			break;

#if USE_MREMAP
		/* double the arena */
		mem = mremap(mran, mran->size, mran->size * 2, 0);
		if (mem == MAP_FAILED)
			break;

		assert(mem == mran);
#else
		/* do a mmap right after the one we have, if it succeeds we have grown */
		mem = mmap((void *)mran + mran->size, mran->size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		if (mem != (void *)mran + mran->size) {
			if (mem != MAP_FAILED)
				munmap(mem, mran->size);
			break;
		}
#endif
		mran->size *= 2;

#ifdef DEBUG_ARENA
		fprintf(stderr, "%s: #%zu grew arena %p size=%zu (%zuMB)\n", __func__, mrt - mra->tags, mran, mran->size, mran->size >> 20);
#endif

		/* verify that we grow right */
		assert(fy_size_t_align(mran->next, align) + size <= mran->size);
		return 0;

	default:
		/* should never happen */
		assert(0);
		abort();
		break;
	}

	return -1;
}

static int fy_mremap_arena_trim(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt, struct fy_mremap_arena *mran)
{
	size_t new_size;
#if USE_MREMAP
	void *mem FY_DEBUG_UNUSED;
#else
	int rc FY_DEBUG_UNUSED;
#endif

	if (!mran)
		return -1;

	switch (mra->arena_type) {
	case FYMRAT_MALLOC:
		/* trim not possible (or is it? should be possible to probe) */
		break;

	case FYMRAT_MMAP:
		/* check the page size */
		new_size = fy_size_t_align(mran->next, mra->pagesz);
		if (new_size >= mran->size)
			break;

#ifdef DEBUG_ARENA
		fprintf(stderr, "trim: %zu -> %zu\n", mran->size, new_size);
#endif

#if USE_MREMAP
		/* failure to shrink a mapping is unthinkable, but check anyway */
		mem = mremap(mran, mran->size, new_size, 0);
		if (mem == MAP_FAILED || mem != mran)
			return -1;
#else
		/* we don't shrink, we just unmap over the limit  */
		rc = munmap((void *)mran + new_size, mran->size - new_size);
		if (rc)
			return -1;
#endif
		mran->size = new_size;
		return 0;

	default:
		/* should never happen */
		assert(0);
		abort();
		break;
	}

	return -1;
}

static inline struct fy_mremap_tag *
fy_mremap_tag_from_tag(struct fy_mremap_allocator *mra, int tag)
{
	if (!mra)
		return NULL;

	if ((unsigned int)tag >= ARRAY_SIZE(mra->tags))
		return NULL;

	if (!fy_id_is_used(mra->ids, ARRAY_SIZE(mra->ids), (int)tag))
		return NULL;

	return &mra->tags[tag];
}

static void fy_mremap_tag_cleanup(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt)
{
	struct fy_mremap_arena *mran;
	int id;
#ifdef DEBUG_ARENA
	struct fy_mremap_arena_list *mranl, *arena_lists[2];
	size_t total_sys_alloc, total_wasted;
	unsigned int j;
#endif

	if (!mra || !mrt)
		return;

	/* get the id from the pointer */
	id = mrt - mra->tags;
	assert((unsigned int)id < ARRAY_SIZE(mra->tags));

	/* already clean? */
	if (fy_id_is_free(mra->ids, ARRAY_SIZE(mra->ids), id))
		return;

	fy_id_free(mra->ids, ARRAY_SIZE(mra->ids), id);

#ifdef DEBUG_ARENA
	total_sys_alloc = 0;
	total_wasted = 0;
	arena_lists[0] = &mrt->arenas;
	arena_lists[1] = &mrt->full_arenas;
	for (j = 0; j < ARRAY_SIZE(arena_lists); j++) {
		mranl = arena_lists[j];
		for (mran = fy_mremap_arena_list_head(mranl); mran; mran = fy_mremap_arena_next(mranl, mran)) {
			total_sys_alloc += mran->size;
			total_wasted += (mran->size - mran->next);
		}
	}
	fprintf(stderr, "total_sys_alloc=%zu total_wasted=%zu\n", total_sys_alloc, total_wasted);
#endif

#ifdef DEBUG_ARENA
	fprintf(stderr, "%s: destroying active arenas\n", __func__);
#endif
	while ((mran = fy_mremap_arena_list_pop(&mrt->arenas)) != NULL)
		fy_mremap_arena_destroy(mra, mrt, mran);

#ifdef DEBUG_ARENA
	fprintf(stderr, "%s: destroying full arenas\n", __func__);
#endif
	while ((mran = fy_mremap_arena_list_pop(&mrt->full_arenas)) != NULL)
		fy_mremap_arena_destroy(mra, mrt, mran);
}

static void fy_mremap_tag_trim(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt)
{
	struct fy_mremap_arena_list *mranl, *arena_lists[2];
	struct fy_mremap_arena *mran;
	unsigned int j;
#ifdef DEBUG_ARENA
	size_t wasted_before, wasted_after;
#endif

	if (!mra || !mrt)
		return;

	if (!fy_mremap_arena_type_is_trimmable(mra->arena_type))
		return;

	arena_lists[0] = &mrt->arenas;
	arena_lists[1] = &mrt->full_arenas;
#ifdef DEBUG_ARENA
	wasted_before = 0;
	wasted_after = 0;
#endif
	for (j = 0; j < ARRAY_SIZE(arena_lists); j++) {
		mranl = arena_lists[j];
		for (mran = fy_mremap_arena_list_head(mranl); mran; mran = fy_mremap_arena_next(mranl, mran)) {
#ifdef DEBUG_ARENA
			wasted_before += (mran->size - mran->next);
#endif
			(void)fy_mremap_arena_trim(mra, mrt, mran);
#ifdef DEBUG_ARENA
			wasted_after += (mran->size - mran->next);
#endif
		}
	}
#ifdef DEBUG_ARENA
	fprintf(stderr, "wasted_before=%zu wasted_after=%zu\n", wasted_before, wasted_after);
#endif
}

static void fy_mremap_tag_reset(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt)
{
	struct fy_mremap_arena *mran;

	if (!mra || !mrt)
		return;

	/* just destroy the arenas */
	while ((mran = fy_mremap_arena_list_pop(&mrt->arenas)) != NULL)
		fy_mremap_arena_destroy(mra, mrt, mran);

	while ((mran = fy_mremap_arena_list_pop(&mrt->full_arenas)) != NULL)
		fy_mremap_arena_destroy(mra, mrt, mran);
}

static void fy_mremap_tag_setup(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt)
{
	assert(mra);
	assert(mrt);

	memset(mrt, 0, sizeof(*mrt));
	fy_mremap_arena_list_init(&mrt->arenas);
	fy_mremap_arena_list_init(&mrt->full_arenas);
	mrt->next_arena_sz = mra->pagesz;
}

static void *fy_mremap_tag_alloc(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt, size_t size, size_t align)
{
	struct fy_mremap_arena *mran;
	size_t left, size_page_align, next_sz;
	void *ptr;
	int rc;

	/* calculate how many pages new allocation is */
	size_page_align = fy_size_t_align(size + FY_MREMAP_ARENA_OVERHEAD, mra->pagesz);

	if (mra->big_alloc_threshold && size_page_align > mra->big_alloc_threshold) {

		mran = fy_mremap_arena_create(mra, mrt, size);
		if (!mran)
			goto err_out;

#ifdef DEBUG_ARENA
		fprintf(stderr, "allocated new big mran->size=%zu size=%zu\n", mran->size, size);
#endif

		fy_mremap_arena_list_add_tail(&mrt->arenas, mran);
		goto do_alloc;
	}

	/* 'small' allocation, try to find an arena that fits first */
	for (mran = fy_mremap_arena_list_head(&mrt->arenas); mran;
			mran = fy_mremap_arena_next(&mrt->arenas, mran)) {
		left = mran->size - fy_size_t_align(mran->next, align);
		if (left >= size) {
			/* make this the new head */
			if (mran != fy_mremap_arena_list_head(&mrt->arenas)) {
				fy_mremap_arena_list_del(&mrt->arenas, mran);
				fy_mremap_arena_list_add(&mrt->arenas, mran);
			}
			goto do_alloc;
		}
	}

	/* not found space in any arena, try to grow */
	if (fy_mremap_arena_type_is_growable(mra->arena_type)) {
		for (mran = fy_mremap_arena_list_head(&mrt->arenas); mran;
				mran = fy_mremap_arena_next(&mrt->arenas, mran)) {

			rc = fy_mremap_arena_grow(mra, mrt, mran, size, align);
			if (rc)
				continue;

#ifdef DEBUG_ARENA
			fprintf(stderr, "grow successful mran->size=%zu size=%zu\n", mran->size, size);
#endif

			left = mran->size - fy_size_t_align(mran->next, align);
			assert(left >= size);

			/* make this the new head */
			if (mran != fy_mremap_arena_list_head(&mrt->arenas)) {
				fy_mremap_arena_list_del(&mrt->arenas, mran);
				fy_mremap_arena_list_add(&mrt->arenas, mran);
			}
			goto do_alloc;
		}

	}

	/* everything failed, we have to allocate a new arena */

	/* increase by the ratio until we're over */
	while (mrt->next_arena_sz < size) {
		next_sz = (size_t)(mrt->next_arena_sz * mra->grow_ratio);
		/* something fishy going on... */
		if (next_sz <= mrt->next_arena_sz)
			goto err_out;
		mrt->next_arena_sz = next_sz;
	}

	/* all failed, just new */
	mran = fy_mremap_arena_create(mra, mrt, mrt->next_arena_sz);
	if (!mran)
		goto err_out;

	mrt->next_arena_sz = (size_t)(mrt->next_arena_sz * mra->grow_ratio);

#ifdef DEBUG_ARENA
	fprintf(stderr, "allocated new %p mran->size=%zu size=%zu\n", mran, mran->size, size);
#endif

	fy_mremap_arena_list_add(&mrt->arenas, mran);

do_alloc:
	mran->next = fy_size_t_align(mran->next, align);
	ptr = (void *)mran + mran->next;
	mran->next += size;
	left = mran->size - mran->next;
	assert((ssize_t)left >= 0);

	/* if it's empty, or almost empty, move it to the full arenas list */
	if (left < mra->empty_threshold) {

		/* if the arena is growable, try to grow it */
		if (fy_mremap_arena_type_is_growable(mra->arena_type)) {
			rc = fy_mremap_arena_grow(mra, mrt, mran, size, align);
			if (!rc)
				left = mran->size - mran->next;
		}

		/* still under the threshold, move it to full */
		if (left < mra->empty_threshold) {
#ifdef DEBUG_ARENA
			fprintf(stderr, "move %p mran->size=%zu left=%zu to free\n", mran, mran->size, left);
#endif
			fy_mremap_arena_list_del(&mrt->arenas, mran);
			fy_mremap_arena_list_add_tail(&mrt->full_arenas, mran);

#ifdef DEBUG_ARENA
			fprintf(stderr, "%s: #%zu moved arena %p size=%zu (%zuMB) to full\n", __func__, mrt - mra->tags, mran, mran->size, mran->size >> 20);
#endif
		}

	}

	return ptr;

err_out:
	return NULL;
}

static const struct fy_mremap_allocator_cfg default_cfg = {
	.big_alloc_threshold = MREMAP_ALLOCATOR_DEFAULT_BIG_ALLOC_THRESHOLD,
	.empty_threshold = MREMAP_ALLOCATOR_DEFAULT_EMPTY_THRESHOLD,
	.minimum_arena_size = MREMAP_ALLOCATOR_DEFAULT_MINIMUM_ARENA_SIZE,
	.grow_ratio = MREMAP_ALLOCATOR_DEFAULT_GROW_RATIO,
	.balloon_ratio = MREMAP_ALLOCATOR_DEFAULT_BALLON_RATIO,
	.arena_type = MREMAP_ALLOCATOR_DEFAULT_ARENA_TYPE,
};

static int fy_mremap_setup(struct fy_allocator *a, const void *cfg_data)
{
	const struct fy_mremap_allocator_cfg *cfg;
	struct fy_mremap_allocator *mra;

	if (!a)
		return -1;

	cfg = cfg_data ? cfg_data : &default_cfg;

	mra = container_of(a, struct fy_mremap_allocator, a);
	memset(mra, 0, sizeof(*mra));
	mra->a.name = "mremap";
	mra->a.ops = &fy_mremap_allocator_ops;
	mra->cfg = *cfg;

	mra->pagesz = sysconf(_SC_PAGESIZE);
	/* pagesz is size of 2 find the first set bit */
	mra->pageshift = fy_id_ffs((fy_id_bits)mra->pagesz);

	mra->big_alloc_threshold = cfg->big_alloc_threshold;
	if (!mra->big_alloc_threshold)
		mra->big_alloc_threshold = MREMAP_ALLOCATOR_DEFAULT_BIG_ALLOC_THRESHOLD;

	mra->empty_threshold = cfg->empty_threshold;
	if (!mra->empty_threshold)
		mra->empty_threshold = MREMAP_ALLOCATOR_DEFAULT_EMPTY_THRESHOLD;

	mra->minimum_arena_size = cfg->minimum_arena_size;
	if (!mra->minimum_arena_size)
		mra->minimum_arena_size = MREMAP_ALLOCATOR_DEFAULT_MINIMUM_ARENA_SIZE;

	mra->grow_ratio = cfg->grow_ratio;
	if (mra->grow_ratio <= 1)
		mra->grow_ratio = MREMAP_ALLOCATOR_DEFAULT_GROW_RATIO;

	mra->balloon_ratio = cfg->balloon_ratio;
	if (mra->balloon_ratio <= 1)
		mra->balloon_ratio = MREMAP_ALLOCATOR_DEFAULT_BALLON_RATIO;

	mra->arena_type = cfg->arena_type;
	if (mra->arena_type == FYMRAT_DEFAULT)
		mra->arena_type = MREMAP_ALLOCATOR_DEFAULT_ARENA_TYPE;

	fy_id_reset(mra->ids, ARRAY_SIZE(mra->ids));
	return 0;
}

static void fy_mremap_cleanup(struct fy_allocator *a)
{
	struct fy_mremap_allocator *mra;
	struct fy_mremap_tag *mrt;
	unsigned int i;

	if (!a)
		return;

	mra = container_of(a, struct fy_mremap_allocator, a);

	for (i = 0, mrt = mra->tags; i < ARRAY_SIZE(mra->tags); i++, mrt++)
		fy_mremap_tag_cleanup(mra, mrt);
}

struct fy_allocator *fy_mremap_create(const void *cfg)
{
	struct fy_mremap_allocator *mra = NULL;
	int rc;

	mra = malloc(sizeof(*mra));
	if (!mra)
		goto err_out;

	rc = fy_mremap_setup(&mra->a, cfg);
	if (rc)
		goto err_out;

	return &mra->a;

err_out:
	if (mra)
		free(mra);

	return NULL;
}

void fy_mremap_destroy(struct fy_allocator *a)
{
	struct fy_mremap_allocator *mra;

	if (!a)
		return;

	mra = container_of(a, struct fy_mremap_allocator, a);
	fy_mremap_cleanup(a);
	free(mra);
}

void fy_mremap_dump(struct fy_allocator *a)
{
	struct fy_mremap_allocator *mra;
	struct fy_mremap_tag *mrt;
	struct fy_mremap_arena *mran;
	struct fy_mremap_arena_list *mranl, *arena_lists[2];
	unsigned int i, j;
	size_t count, active_count, full_count, total, system_total;

	if (!a)
		return;

	mra = container_of(a, struct fy_mremap_allocator, a);

	fprintf(stderr, "mremap: ");
	for (i = 0, mrt = mra->tags; i < ARRAY_SIZE(mra->tags); i++, mrt++)
		fprintf(stderr, "%c", fy_id_is_free(mra->ids, ARRAY_SIZE(mra->ids), i) ? '.' : 'x');
	fprintf(stderr, "\n");

	for (i = 0, mrt = mra->tags; i < ARRAY_SIZE(mra->tags); i++, mrt++) {
		if (fy_id_is_free(mra->ids, ARRAY_SIZE(mra->ids), i))
			continue;

		count = full_count = active_count = total = system_total = 0;
		arena_lists[0] = &mrt->arenas;
		arena_lists[1] = &mrt->full_arenas;
		for (j = 0; j < ARRAY_SIZE(arena_lists); j++) {
			mranl = arena_lists[j];
			for (mran = fy_mremap_arena_list_head(mranl); mran; mran = fy_mremap_arena_next(mranl, mran)) {
				total += mran->next;
				system_total += mran->size;
				count++;
				if (j == 0)
					active_count++;
				else
					full_count++;
			}
		}

		fprintf(stderr, "  %d: count %zu (a=%zu/f=%zu) total %zu system %zu overhead %zu (%2.2f%%)\n", i,
				count, active_count, full_count,
				total, system_total, system_total - total,
				100.0 * (double)(system_total - total) / (double)system_total);
	}
}

static void *fy_mremap_alloc(struct fy_allocator *a, int tag, size_t size, size_t align)
{
	struct fy_mremap_allocator *mra;
	struct fy_mremap_tag *mrt;
	void *ptr;

	if (!a || tag < 0)
		return NULL;

	mra = container_of(a, struct fy_mremap_allocator, a);

	mrt = fy_mremap_tag_from_tag(mra, tag);
	if (!mrt)
		goto err_out;

	ptr = fy_mremap_tag_alloc(mra, mrt, size, align);
	if (!ptr)
		goto err_out;

	mrt->stats.allocations++;
	mrt->stats.allocated += size;

	return ptr;

err_out:
	return NULL;
}

static void fy_mremap_free(struct fy_allocator *a, int tag, void *data)
{
	/* no frees */
}

static int fy_mremap_update_stats(struct fy_allocator *a, int tag, struct fy_allocator_stats *stats)
{
	struct fy_mremap_allocator *mra;
	struct fy_mremap_tag *mrt;
	unsigned int i;

	if (!a || !stats)
		return -1;

	mra = container_of(a, struct fy_mremap_allocator, a);

	mrt = fy_mremap_tag_from_tag(mra, tag);
	if (!mrt)
		goto err_out;

	/* and update with this ones */
	for (i = 0; i < ARRAY_SIZE(mrt->stats.counters); i++) {
		stats->counters[i] += mrt->stats.counters[i];
		mrt->stats.counters[i] = 0;
	}

	return 0;

err_out:
	return -1;
}

static const void *fy_mremap_storev(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align)
{
	struct fy_mremap_allocator *mra;
	struct fy_mremap_tag *mrt;
	void *p, *start;
	int i;
	size_t total_size, size;

	if (!a || !iov)
		return NULL;

	mra = container_of(a, struct fy_mremap_allocator, a);

	mrt = fy_mremap_tag_from_tag(mra, tag);
	if (!mrt)
		goto err_out;

	total_size = 0;
	for (i = 0; i < iovcnt; i++)
		total_size += iov[i].iov_len;

	start = fy_mremap_tag_alloc(mra, mrt, total_size, align);
	if (!start)
		goto err_out;

	for (i = 0, p = start; i < iovcnt; i++, p += size) {
		size = iov[i].iov_len;
		memcpy(p, iov[i].iov_base, size);
	}

	mrt->stats.stores++;
	mrt->stats.stored += total_size;

	return start;

err_out:
	return NULL;
}

static const void *fy_mremap_store(struct fy_allocator *a, int tag, const void *data, size_t size, size_t align)
{
	struct iovec iov[1];

	if (!a)
		return NULL;

	/* just call the storev */
	iov[0].iov_base = (void *)data;
	iov[0].iov_len = size;
	return fy_mremap_storev(a, tag, iov, 1, align);
}

static void fy_mremap_release(struct fy_allocator *a, int tag, const void *data, size_t size)
{
	/* no releases */
}

static void fy_mremap_release_tag(struct fy_allocator *a, int tag)
{
	struct fy_mremap_allocator *mra;
	struct fy_mremap_tag *mrt;

	if (!a)
		return;

	mra = container_of(a, struct fy_mremap_allocator, a);

	mrt = fy_mremap_tag_from_tag(mra, tag);
	if (!mrt)
		return;

	fy_mremap_tag_cleanup(mra, mrt);
}

static int fy_mremap_get_tag(struct fy_allocator *a)
{
	struct fy_mremap_allocator *mra;
	struct fy_mremap_tag *mrt;
	int id;

	if (!a)
		return FY_ALLOC_TAG_ERROR;

	mra = container_of(a, struct fy_mremap_allocator, a);

	id = fy_id_alloc(mra->ids, ARRAY_SIZE(mra->ids));
	if (id < 0)
		goto err_out;

	mrt = fy_mremap_tag_from_tag(mra, id);
	assert(mrt);

	fy_mremap_tag_setup(mra, mrt);

	return (int)id;

err_out:
	return FY_ALLOC_TAG_ERROR;
}

static void fy_mremap_trim_tag(struct fy_allocator *a, int tag)
{
	struct fy_mremap_allocator *mra;
	struct fy_mremap_tag *mrt;

	if (!a)
		return;

	mra = container_of(a, struct fy_mremap_allocator, a);

	mrt = fy_mremap_tag_from_tag(mra, tag);
	if (!mrt)
		return;

	fy_mremap_tag_trim(mra, mrt);
}

static void fy_mremap_reset_tag(struct fy_allocator *a, int tag)
{
	struct fy_mremap_allocator *mra;
	struct fy_mremap_tag *mrt;

	if (!a)
		return;

	mra = container_of(a, struct fy_mremap_allocator, a);

	mrt = fy_mremap_tag_from_tag(mra, tag);
	if (!mrt)
		return;

	fy_mremap_tag_reset(mra, mrt);
}

static struct fy_allocator_info *
fy_mremap_get_info(struct fy_allocator *a, int tag)
{
	struct fy_mremap_allocator *mra;
	struct fy_mremap_tag *mrt;
	struct fy_mremap_arena *mran;
	struct fy_allocator_info *info;
	struct fy_allocator_tag_info *tag_info;
	struct fy_allocator_arena_info *arena_info;
	struct fy_mremap_arena_list *mranl, *arena_lists[2];
	size_t size, free, used, total;
	size_t tag_free, tag_used, tag_total;
	size_t arena_free, arena_used, arena_total;
	unsigned int num_tags, num_arenas, i, j;
	int id;

	if (!a)
		return NULL;

	mra = container_of(a, struct fy_mremap_allocator, a);

	/* allocate for the worst case always */
	num_tags = 0;
	num_arenas = 0;

	free = 0;
	used = 0;
	total = 0;

	/* two passes */
	for (i = 0; i < 2; i++) {

		if (!i) {
			tag_info = NULL;
			arena_info = NULL;

		} else {
			size = sizeof(*info) +
                               sizeof(*tag_info) * num_tags +
			       sizeof(*arena_info) * num_arenas;

			info = malloc(size);
			if (!info)
				return NULL;
			memset(info, 0, sizeof(*info));

			tag_info = (void *)(info + 1);
			assert(((uintptr_t)tag_info % alignof(struct fy_allocator_tag_info)) == 0);
			arena_info = (void *)(tag_info + num_tags);
			assert(((uintptr_t)arena_info % alignof(struct fy_allocator_arena_info)) == 0);

			info->free = free;
			info->used = used;
			info->total = total;

			info->num_tag_infos = 0;
			info->tag_infos = tag_info;
		}

		free = 0;
		used = 0;
		total = sizeof(*mra);

		for (id = 0; id < (int)ARRAY_SIZE(mra->tags); id++) {

			if (!fy_id_is_used(mra->ids, ARRAY_SIZE(mra->ids), id))
				continue;

			mrt = &mra->tags[id];

			tag_free = 0;
			tag_used = 0;
			tag_total = 0;

			if (i) {
				tag_info->num_arena_infos = 0;
				tag_info->arena_infos = arena_info;
			}

			arena_lists[0] = &mrt->arenas;
			arena_lists[1] = &mrt->full_arenas;
			for (j = 0; j < ARRAY_SIZE(arena_lists); j++) {
				mranl = arena_lists[j];
				for (mran = fy_mremap_arena_list_head(mranl); mran; mran = fy_mremap_arena_next(mranl, mran)) {
					arena_free = (size_t)(mran->size - mran->next);
					arena_used = (size_t)(mran->next - FY_MREMAP_ARENA_OVERHEAD);
					arena_total = mran->size;

					tag_free += arena_free;
					tag_used += arena_used;
					tag_total += arena_total;

					if (!i) {
						num_arenas++;
					} else {
						arena_info->free = arena_free;
						arena_info->used = arena_used;
						arena_info->total = arena_total;
						arena_info->data = (void *)mran + FY_MREMAP_ARENA_OVERHEAD;
						arena_info->size = arena_info->used;
						arena_info++;
						tag_info->num_arena_infos++;
					}
				}
			}

			if (!i) {
				num_tags++;
			} else {

				/* only store the tag if there's a match */
				if (tag == FY_ALLOC_TAG_NONE || tag == id) {
					tag_info->tag = id;
					tag_info->free = tag_free;
					tag_info->used = tag_used;
					tag_info->total = tag_total;
					tag_info++;
					info->num_tag_infos++;
				}
			}

			free += tag_free;
			used += tag_used;
			total += tag_total;
		}
	}

	return info;
}

const struct fy_allocator_ops fy_mremap_allocator_ops = {
	.setup = fy_mremap_setup,
	.cleanup = fy_mremap_cleanup,
	.create = fy_mremap_create,
	.destroy = fy_mremap_destroy,
	.dump = fy_mremap_dump,
	.alloc = fy_mremap_alloc,
	.free = fy_mremap_free,
	.update_stats = fy_mremap_update_stats,
	.store = fy_mremap_store,
	.storev = fy_mremap_storev,
	.release = fy_mremap_release,
	.get_tag = fy_mremap_get_tag,
	.release_tag = fy_mremap_release_tag,
	.trim_tag = fy_mremap_trim_tag,
	.reset_tag = fy_mremap_reset_tag,
	.get_info = fy_mremap_get_info,
};

