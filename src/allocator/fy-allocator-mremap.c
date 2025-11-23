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
#include <alloca.h>

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

static inline size_t fy_mremap_useable_arena_size(struct fy_mremap_allocator *mra, size_t size)
{
	return fy_size_t_align(size + FY_MREMAP_ARENA_OVERHEAD, mra->pagesz) -
		FY_MREMAP_ARENA_OVERHEAD;
}

static inline bool fy_mremap_arena_available(struct fy_mremap_arena *mran)
{
	return mran->size - fy_atomic_load(&mran->next);
}

static inline bool fy_mremap_arena_check_fit(struct fy_mremap_arena *mran, size_t size, size_t align)
{
	size_t old_next, new_next;

	old_next = fy_atomic_load(&mran->next);
	new_next = fy_size_t_align(old_next, align) + size;
	return new_next <= mran->size;
}

static struct fy_mremap_arena *
fy_mremap_arena_create(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt, size_t size)
{
	struct fy_mremap_arena *mran = NULL;
	uint64_t flags;
	void *mem;
	size_t size_page_align, balloon_size;
#if !USE_MREMAP
	int rc FY_DEBUG_UNUSED;
#endif

	if (size < mra->minimum_arena_size)
		size = mra->minimum_arena_size;

	size_page_align = fy_mremap_useable_arena_size(mra, size);
	switch (mra->arena_type) {
	case FYMRAT_MALLOC:
		mran = malloc(size_page_align);
		if (!mran)
			return NULL;
		memset(mran, 0, sizeof(*mran));
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
		FY_IMPOSSIBLE_ABORT();
	}

	mran->next_arena = NULL;
	flags = 0;
	if (!fy_mremap_arena_type_is_growable(mra->arena_type))
		flags |= FYMRAF_CANT_GROW;
	fy_atomic_store(&mran->flags, flags);
	mran->size = size_page_align;
	fy_atomic_store(&mran->next, FY_MREMAP_ARENA_OVERHEAD);

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
		FY_IMPOSSIBLE_ABORT();
	}
}

static inline bool
fy_mremap_arena_should_grow(struct fy_mremap_allocator *mra, struct fy_mremap_arena *mran,
			    size_t size, size_t align)
{
	size_t next;

	if (!mran || !size)
		return false;

	if (!fy_mremap_arena_type_is_growable(mra->arena_type))
		return false;

	/* there's no point trying to grow something this big */
	if (mra->big_alloc_threshold && size >= mra->big_alloc_threshold)
		return false;

	/* if the grow needs to be larger than double, don't bother */
	next = atomic_load(&mran->next);
	if (fy_size_t_align(next, align) + size > 2 * mran->size)
		return false;

	/* go for it */
	return true;
}

static int fy_mremap_arena_grow(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt,
				struct fy_mremap_arena *mran, size_t size, size_t align)
{
	void *mem;

	if (!fy_mremap_arena_should_grow(mra, mran, size, align))
		return -1;

	switch (mra->arena_type) {
	case FYMRAT_MALLOC:
		/* can't grow malloc without moving the pointer */
		break;

	case FYMRAT_MMAP:
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
		return 0;

	default:
		FY_IMPOSSIBLE_ABORT();
	}

	return -1;
}

static int fy_mremap_arena_trim(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt, struct fy_mremap_arena *mran)
{
	size_t next, new_size;
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
		next = fy_atomic_load(&mran->next);
		new_size = fy_size_t_align(next, mra->pagesz);
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
		FY_IMPOSSIBLE_ABORT();
	}

	return -1;
}

static inline struct fy_mremap_tag *
fy_mremap_tag_from_tag(struct fy_mremap_allocator *mra, int tag)
{
	if (!mra)
		return NULL;

	if ((unsigned int)tag >= mra->tag_count)
		return NULL;

	if (!fy_id_is_used(mra->ids, mra->tag_id_count, (int)tag))
		return NULL;

	return &mra->tags[tag];
}

static void fy_mremap_tag_cleanup(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt)
{
	struct fy_mremap_arena *mran;
#ifdef DEBUG_ARENA
	size_t total_sys_alloc, total_wasted, next;
#endif

	if (!mra || !mrt)
		return;

#ifdef DEBUG_ARENA
	total_sys_alloc = 0;
	total_wasted = 0;
	for (mran = fy_atomic_load(&mrt->arenas); mran; mran = mran->next_arena) {
		total_sys_alloc += mran->size;
		next = fy_atomic_load(&mran->next);
		total_wasted += (mran->size - next);
	}
	fprintf(stderr, "total_sys_alloc=%zu total_wasted=%zu\n", total_sys_alloc, total_wasted);
#endif

#ifdef DEBUG_ARENA
	fprintf(stderr, "%s: destroying active arenas\n", __func__);
#endif
	/* pop and destroy */
	while ((mran = fy_atomic_load(&mrt->arenas)) != NULL) {
		if (fy_atomic_compare_exchange_strong(&mrt->arenas, &mran, mran->next_arena))
			fy_mremap_arena_destroy(mra, mrt, mran);
	}

	/* nuke it all */
	memset(mrt, 0, sizeof(*mrt));
}

static void fy_mremap_tag_trim(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt)
{
	struct fy_mremap_arena *mran;
#ifdef DEBUG_ARENA
	size_t wasted_before, wasted_after, next;
#endif

	if (!mra || !mrt)
		return;

	if (!fy_mremap_arena_type_is_trimmable(mra->arena_type))
		return;

#ifdef DEBUG_ARENA
	wasted_before = 0;
	wasted_after = 0;
#endif
	for (mran = fy_atomic_load(&mrt->arenas); mran; mran = mran->next_arena) {
#ifdef DEBUG_ARENA
		next = fy_atomic_load(&mran->next);
		wasted_before += (mran->size - next);
#endif
		(void)fy_mremap_arena_trim(mra, mrt, mran);
#ifdef DEBUG_ARENA
		next = fy_atomic_load(&mran->next);
		wasted_after += (mran->size - next);
#endif
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

	/* pop and destroy */
	while ((mran = fy_atomic_load(&mrt->arenas)) != NULL) {
		if (fy_atomic_compare_exchange_strong(&mrt->arenas, &mran, mran->next_arena))
			fy_mremap_arena_destroy(mra, mrt, mran);
	}
}

static void fy_mremap_tag_setup(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt)
{
	assert(mra);
	assert(mrt);

	memset(mrt, 0, sizeof(*mrt));
	fy_atomic_store(&mrt->arenas, NULL);
	fy_atomic_store(&mrt->next_arena_sz, mra->pagesz);
}

static void *fy_mremap_tag_alloc(struct fy_mremap_allocator *mra, struct fy_mremap_tag *mrt, size_t size, size_t align)
{
	struct fy_mremap_arena *mran, *old_arenas;
	size_t next_sz, next_arena_sz, old_next_arena_sz, old_next, new_next, data_pos;
	uint64_t flags;
	void *ptr;
	int rc;

again:
	/* hot path, try to find an arena that fits first */
	for (mran = fy_atomic_load(&mrt->arenas); mran; mran = mran->next_arena) {

		flags = fy_atomic_load(&mran->flags);
		if (flags & FYMRAF_FULL)
			continue;

		if (fy_mremap_arena_check_fit(mran, size, align))
			goto do_alloc;

		/* if it cant grown and is under the threshold, mark it full */
		if ((flags & FYMRAF_CANT_GROW) && fy_mremap_arena_available(mran) < mra->empty_threshold)
			fy_atomic_fetch_or(&mran->flags, FYMRAF_FULL);
	}

	/* this arena type is not growable, skip growing step */
	if (!fy_mremap_arena_type_is_growable(mra->arena_type))
		goto create_arena;

	/* not found space in any arena, try to grow */
	for (mran = fy_atomic_load(&mrt->arenas); mran; mran = mran->next_arena) {

		flags = fy_atomic_load(&mran->flags);
		if (flags & (FYMRAF_FULL | FYMRAF_CANT_GROW | FYMRAF_GROWING))
			continue;

		/* don't even try? */
		if (!fy_mremap_arena_should_grow(mra, mran, size, align))
			continue;

		/* try to grab the growing lock */
		if (!fy_atomic_compare_exchange_strong(&mran->flags, &flags, flags | FYMRAF_GROWING))
			continue;

		/* try to grow, note that the arena is still available */
		rc = fy_mremap_arena_grow(mra, mrt, mran, size, align);
		if (rc) {
#ifdef DEBUG_ARENA
			fprintf(stderr, "failed to grow %p\n", mran);
#endif
			/* release the lock, and mark it as can't grow */
			fy_atomic_fetch_or(&mran->flags, FYMRAF_CANT_GROW);
			fy_atomic_fetch_and(&mran->flags, ~FYMRAF_GROWING);

			continue;
		}

#ifdef DEBUG_ARENA
		fprintf(stderr, "grow successful %p mran->size=%zu size=%zu\n", mran, mran->size, size);
#endif

		/* release the lock */
		fy_atomic_fetch_and(&mran->flags, ~FYMRAF_GROWING);

		if (fy_mremap_arena_check_fit(mran, size, align))
			goto do_alloc;
	}

create_arena:
	/* everything failed, we have to allocate a new arena */

	/* it's a relatively small allocation, try to resize until we fit */
	if (!mra->big_alloc_threshold || size < mra->big_alloc_threshold) {

		do {
			/* increase by the ratio until we're over */
			old_next_arena_sz = fy_atomic_load(&mrt->next_arena_sz);
			next_arena_sz = old_next_arena_sz;
			while (fy_mremap_useable_arena_size(mra, next_arena_sz) < size) {
				next_sz = (size_t)(next_arena_sz * mra->grow_ratio);

				/* very very unlikely */
				if (next_sz <= next_arena_sz)
					goto err_out;

				next_arena_sz = next_sz;
			}

			/* update to the next possible arena size */
			next_sz = (size_t)(next_arena_sz * mra->grow_ratio);
			if (next_sz > next_arena_sz)
				next_arena_sz = next_sz;

		} while (!fy_atomic_compare_exchange_strong(&mrt->next_arena_sz,
					&old_next_arena_sz, next_arena_sz));
	} else
		next_arena_sz = size;	/* something big, just use it as is */

	/* all failed, just new */
	mran = fy_mremap_arena_create(mra, mrt, next_arena_sz);
	if (!mran)
		goto err_out;

#ifdef DEBUG_ARENA
	fprintf(stderr, "allocated new %p mran->size=%zu size=%zu\n", mran, mran->size, size);
#endif

	/* atomically add it */
	do {
		old_arenas = fy_atomic_load(&mrt->arenas);
		mran->next_arena = old_arenas;
	} while (!fy_atomic_compare_exchange_strong(&mrt->arenas, &old_arenas, mran));

do_alloc:

	do {
		old_next = fy_atomic_load(&mran->next);
		data_pos = fy_size_t_align(old_next, align);
		new_next = data_pos + size;
		if (new_next > mran->size) {
#ifdef DEBUG_ARENA
			fprintf(stderr, "failed new %p mran->size=%zu size=%zu failed to fit!\n", mran, mran->size, size);
#endif
			goto again;
		}
		ptr = (void *)mran + data_pos;
	} while (!fy_atomic_compare_exchange_strong(&mran->next, &old_next, new_next));

	/* malloc arenas need to zero out */
	if (mra->arena_type == FYMRAT_MALLOC)
		memset(ptr, 0, size);

#ifdef DEBUG_ARENA
	fprintf(stderr, "allocated OK %p mran->size=%zu ptr=%p size=%zu align=%zu\n", mran, mran->size, ptr, size, align);
#endif
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

static void fy_mremap_cleanup(struct fy_allocator *a)
{
	struct fy_mremap_allocator *mra;
	struct fy_mremap_tag *mrt;
	unsigned int i;

	if (!a)
		return;

	mra = container_of(a, struct fy_mremap_allocator, a);

	for (i = 0; i < mra->tag_count; i++) {
		mrt = fy_mremap_tag_from_tag(mra, i);
		if (!mrt)
			continue;
		fy_mremap_tag_cleanup(mra, mrt);
	}

	fy_parent_allocator_free(&mra->a, mra->ids);
	fy_parent_allocator_free(&mra->a, mra->tags);
}

static int fy_mremap_setup(struct fy_allocator *a, struct fy_allocator *parent, int parent_tag, const void *cfg_data)
{
	const struct fy_mremap_allocator_cfg *cfg;
	struct fy_mremap_allocator *mra;
	struct fy_mremap_tag *mrt;
	size_t tmpsz;

	if (!a)
		return -1;

	cfg = cfg_data ? cfg_data : &default_cfg;

	mra = container_of(a, struct fy_mremap_allocator, a);
	memset(mra, 0, sizeof(*mra));
	mra->a.name = "mremap";
	mra->a.ops = &fy_mremap_allocator_ops;
	mra->a.parent = parent;
	mra->a.parent_tag = parent_tag;
	mra->cfg = *cfg;

	mra->pagesz = sysconf(_SC_PAGESIZE);
	/* pagesz is size of 2 find the first set bit */
	mra->pageshift = fy_bit64_ffs(mra->pagesz);

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

	mra->tag_count = FY_MREMAP_TAG_MAX;
	mra->tag_id_count = (mra->tag_count + FY_ID_BITS_BITS - 1) / FY_ID_BITS_BITS;

	tmpsz = mra->tag_id_count * sizeof(*mra->ids);
	mra->ids = fy_parent_allocator_alloc(&mra->a, tmpsz, _Alignof(fy_id_bits));
	if (!mra->ids)
		goto err_out;
	fy_id_reset(mra->ids, mra->tag_id_count);

	tmpsz = mra->tag_count * sizeof(*mra->tags);
	mra->tags = fy_parent_allocator_alloc(&mra->a, tmpsz, _Alignof(struct fy_mremap_tag));
	if (!mra->tags)
		goto err_out;

	/* start with tag 0 as general use */
	fy_id_set_used(mra->ids, mra->tag_id_count, 0);
	mrt = fy_mremap_tag_from_tag(mra, 0);
	fy_mremap_tag_setup(mra, mrt);

	return 0;

err_out:
	fy_mremap_cleanup(a);
	return -1;
}

struct fy_allocator *fy_mremap_create(struct fy_allocator *parent, int parent_tag, const void *cfg)
{
	struct fy_mremap_allocator *mra = NULL;
	int rc;

	mra = fy_early_parent_allocator_alloc(parent, parent_tag, sizeof(*mra), _Alignof(struct fy_mremap_allocator));
	if (!mra)
		goto err_out;

	rc = fy_mremap_setup(&mra->a, parent, parent_tag, cfg);
	if (rc)
		goto err_out;

	return &mra->a;

err_out:
	fy_early_parent_allocator_free(parent, parent_tag, mra);
	return NULL;
}

void fy_mremap_destroy(struct fy_allocator *a)
{
	struct fy_mremap_allocator *mra;

	if (!a)
		return;

	mra = container_of(a, struct fy_mremap_allocator, a);
	fy_mremap_cleanup(a);
	fy_parent_allocator_free(a, mra);
}

void fy_mremap_dump(struct fy_allocator *a)
{
	struct fy_mremap_allocator *mra;
	struct fy_mremap_tag *mrt;
	struct fy_mremap_arena *mran;
	size_t count, active_count, full_count, total, system_total;
	unsigned int i;

	if (!a)
		return;

	mra = container_of(a, struct fy_mremap_allocator, a);

	fprintf(stderr, "mremap: ");
	for (i = 0; i < mra->tag_count; i++) {
		mrt = fy_mremap_tag_from_tag(mra, i);
		if (!mrt)
			continue;
		fprintf(stderr, "%c", fy_id_is_free(mra->ids, mra->tag_id_count, i) ? '.' : 'x');
	}
	fprintf(stderr, "\n");

	for (i = 0; i < mra->tag_count; i++) {
		mrt = fy_mremap_tag_from_tag(mra, i);
		if (!mrt)
			continue;

		count = full_count = active_count = total = system_total = 0;
		for (mran = fy_atomic_load(&mrt->arenas); mran; mran = mran->next_arena) {
			total += fy_atomic_load(&mran->next);
			system_total += mran->size;
			count++;
			active_count++;
			/* XXX full count */
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
	void *start;
	size_t total_size;

	if (!a || !iov)
		return NULL;

	mra = container_of(a, struct fy_mremap_allocator, a);

	mrt = fy_mremap_tag_from_tag(mra, tag);
	if (!mrt)
		goto err_out;

	total_size = fy_iovec_size(iov, iovcnt);
	if (total_size == SIZE_MAX)
		goto err_out;

	start = fy_mremap_tag_alloc(mra, mrt, total_size, align);
	if (!start)
		goto err_out;

	fy_iovec_copy_from(iov, iovcnt, start);

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

	/* must be last */
	fy_id_free(mra->ids, mra->tag_id_count, tag);
}

static int fy_mremap_get_tag(struct fy_allocator *a)
{
	struct fy_mremap_allocator *mra;
	struct fy_mremap_tag *mrt;
	int id = -1;

	if (!a)
		return FY_ALLOC_TAG_ERROR;

	mra = container_of(a, struct fy_mremap_allocator, a);

	/* this is atomic, so safe for multiple threads */
	id = fy_id_alloc(mra->ids, mra->tag_id_count);
	if (id < 0)
		goto err_out;

	mrt = fy_mremap_tag_from_tag(mra, id);
	if (!mrt)
		goto err_out;

	fy_mremap_tag_setup(mra, mrt);

	return (int)id;

err_out:
	if (id >= 0)
		fy_id_free(mra->ids, mra->tag_id_count, id);
	return FY_ALLOC_TAG_ERROR;
}

static int fy_mremap_get_tag_count(struct fy_allocator *a)
{
	struct fy_mremap_allocator *mra;

	if (!a)
		return -1;

	mra = container_of(a, struct fy_mremap_allocator, a);
	return mra->tag_count;
}

static int fy_mremap_set_tag_count(struct fy_allocator *a, unsigned int count)
{
	struct fy_mremap_allocator *mra;

	if (!a)
		return -1;

	mra = container_of(a, struct fy_mremap_allocator, a);
	(void)mra;
	return -1;
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
	size_t size, free, used, total;
	size_t tag_free, tag_used, tag_total;
	size_t arena_free, arena_used, arena_total;
	unsigned int num_tags, num_arenas, i;
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
			assert(((uintptr_t)tag_info % _Alignof(struct fy_allocator_tag_info)) == 0);
			arena_info = (void *)(tag_info + num_tags);
			assert(((uintptr_t)arena_info % _Alignof(struct fy_allocator_arena_info)) == 0);

			info->free = free;
			info->used = used;
			info->total = total;

			info->num_tag_infos = 0;
			info->tag_infos = tag_info;
		}

		free = 0;
		used = 0;
		total = sizeof(*mra);

		for (id = 0; id < (int)mra->tag_count; id++) {

			if (!fy_id_is_used(mra->ids, mra->tag_id_count, id))
				continue;

			mrt = &mra->tags[id];

			tag_free = 0;
			tag_used = 0;
			tag_total = 0;

			if (i) {
				tag_info->num_arena_infos = 0;
				tag_info->arena_infos = arena_info;
			}

			for (mran = fy_atomic_load(&mrt->arenas); mran; mran = mran->next_arena) {
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

static enum fy_allocator_cap_flags
fy_mremap_get_caps(struct fy_allocator *a)
{
	return FYACF_CAN_FREE_TAG | FYACF_HAS_EFFICIENT_CONTAINS | \
	       FYACF_HAS_CONTAINS | FYACF_HAS_TAGS;
}

static bool mremap_tag_contains(struct fy_mremap_tag *mrt, const void *p)
{
	struct fy_mremap_arena *mra;

	for (mra = fy_atomic_load(&mrt->arenas); mra; mra = mra->next_arena) {
		if (p >= (const void *)mra->mem &&
		    p < (const void *)mra->mem + mra->size)
			return true;
	}

	return false;
}

static bool fy_mremap_contains(struct fy_allocator *a, int tag, const void *ptr)
{
	struct fy_mremap_allocator *mra;
	struct fy_mremap_tag *mrt;
	int tag_start, tag_end;

	if (!a || !ptr)
		return false;

	mra = container_of(a, struct fy_mremap_allocator, a);

	if (tag >= 0) {
		if (tag >= (int)mra->tag_count)
			return false;
		tag_start = tag;
		tag_end = tag + 1;
	} else {
		tag_start = 0;
		tag_end = (int)mra->tag_count;
	}

	for (tag = tag_start; tag < tag_end; tag++) {

		mrt = fy_mremap_tag_from_tag(mra, tag);
		if (!mrt)
			continue;

		if (mremap_tag_contains(mrt, ptr))
			return true;
	}

	return false;
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
	.get_tag_count = fy_mremap_get_tag_count,
	.set_tag_count = fy_mremap_set_tag_count,
	.trim_tag = fy_mremap_trim_tag,
	.reset_tag = fy_mremap_reset_tag,
	.get_info = fy_mremap_get_info,
	.get_caps = fy_mremap_get_caps,
	.contains = fy_mremap_contains,
};
