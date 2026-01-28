/*
 * fy-allocator-linear.c - linear allocator
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

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif


#include <stdio.h>

#include "fy-utils.h"
#include "fy-allocator-linear.h"

static int fy_linear_setup(struct fy_allocator *a, struct fy_allocator *parent, int parent_tag, const void *cfg_data)
{
	struct fy_linear_allocator *la;
	const struct fy_linear_allocator_cfg *cfg;
	void *buf, *alloc = NULL;
	bool need_zero;

	if (!a || !cfg_data)
		return -1;

	cfg = cfg_data;
	if (!cfg->size)
		return -1;

	if (!cfg->buf) {
		alloc = fy_early_parent_allocator_alloc(parent, parent_tag, cfg->size, 0);
		if (!alloc)
			goto err_out;
		buf = alloc;
		need_zero = true;
	} else {
		buf = cfg->buf;
		need_zero = false;
	}

	la = container_of(a, struct fy_linear_allocator, a);
	memset(la, 0, sizeof(*la));
	la->a.name = "linear";
	la->a.ops = &fy_linear_allocator_ops;
	la->a.parent = parent;
	la->a.parent_tag = parent_tag;
	la->cfg = *cfg;
	la->alloc = alloc;
	la->start = buf;
	la->need_zero = need_zero;

	fy_atomic_store(&la->next, 0);

	return 0;
err_out:
	fy_early_parent_allocator_free(parent, parent_tag, alloc);
	return -1;
}

static void fy_linear_cleanup(struct fy_allocator *a)
{
	struct fy_linear_allocator *la;

	if (!a)
		return;

	la = container_of(a, struct fy_linear_allocator, a);
	fy_parent_allocator_free(&la->a, la->alloc);
	la->alloc = NULL;
}

struct fy_allocator *fy_linear_create(struct fy_allocator *parent, int parent_tag, const void *cfg_data)
{
	struct fy_linear_allocator *la;
	const struct fy_linear_allocator_cfg *cfg;
	struct fy_linear_allocator_cfg newcfg;
	void *s, *e, *buf, *alloc = NULL;
	int rc;

	if (!cfg_data)
		return NULL;

	cfg = cfg_data;
	if (!cfg->size)
		return NULL;

	if (!cfg->buf) {
		alloc = fy_early_parent_allocator_alloc(parent, parent_tag, cfg->size, 0);
		if (!alloc)
			goto err_out;
		buf = alloc;
	} else
		buf = cfg->buf;

	s = buf;
	e = (char *)s + cfg->size;

	s = fy_ptr_align(s, _Alignof(struct fy_linear_allocator));
	if ((size_t)((char *)e - (char *)s) < sizeof(*la))
		goto err_out;

	la = s;
	s = (char *)s + sizeof(*la);

	memset(&newcfg, 0, sizeof(newcfg));
	newcfg.buf = s;
	newcfg.size = (size_t)((char *)e - (char *)s);

	rc = fy_linear_setup(&la->a, parent, parent_tag, &newcfg);
	if (rc)
		goto err_out;

	la->alloc = alloc;

	return &la->a;

err_out:
	fy_early_parent_allocator_free(parent, parent_tag, alloc);
	return NULL;
}

void fy_linear_destroy(struct fy_allocator *a)
{
	struct fy_linear_allocator *la;
	struct fy_allocator *parent;
	int parent_tag;
	void *alloc;

	la = container_of(a, struct fy_linear_allocator, a);

	/* take out the allocation of create */
	alloc = la->alloc;
	la->alloc = NULL;
	parent = la->a.parent;
	parent_tag = la->a.parent_tag;

	fy_linear_cleanup(a);

	fy_early_parent_allocator_free(parent, parent_tag, alloc);
}

void fy_linear_dump(struct fy_allocator *a)
{
	struct fy_linear_allocator *la;
	size_t next;

	la = container_of(a, struct fy_linear_allocator, a);

	next = fy_atomic_load(&la->next);
	fprintf(stderr, "linear: total %zu used %zu free %zu\n",
			la->cfg.size, next, la->cfg.size - la->next);
}

static void *fy_linear_alloc(struct fy_allocator *a, int tag, size_t size, size_t align)
{
	struct fy_linear_allocator *la;
	size_t next, new_next, real_size;
	void *s;

	assert(a);

	la = container_of(a, struct fy_linear_allocator, a);

	/* atomically update the pointer */
	do {
		next = fy_atomic_load(&la->next);
		s = fy_ptr_align(la->start + next, align);
		new_next = (size_t)(((char *)s + size) - la->start);
		/* handle both overflow and underflow */
		if (new_next > la->cfg.size)
			goto err_out;
	} while (!fy_atomic_compare_exchange_strong(&la->next, &next, new_next));

	real_size = new_next - next;

	/* only update stats if someone asked for it */
	if (a->flags & FYAF_KEEP_STATS) {
		fy_atomic_fetch_add(&la->allocations, 1);
		fy_atomic_fetch_add(&la->allocated, real_size);
	}

	/* zero out buffer if not guaranteed */
	if (la->need_zero)
		memset(s, 0, size);

	return s;

err_out:
	return NULL;
}

static void fy_linear_free(struct fy_allocator *a, int tag, void *data)
{
	/* linear allocator does not free anything */
}

static int fy_linear_update_stats(struct fy_allocator *a, int tag, struct fy_allocator_stats *stats)
{
	struct fy_linear_allocator *la;

	la = container_of(a, struct fy_linear_allocator, a);

	stats->allocations = fy_atomic_get_and_clear_counter(&la->allocations);
	stats->allocated = fy_atomic_get_and_clear_counter(&la->allocated);

	return 0;
}

static const void *fy_linear_storev(struct fy_allocator *a, int tag,
				    const struct iovec *iov, int iovcnt, size_t align,
				    uint64_t hash)
{
	struct fy_linear_allocator *la;
	void *p;
	size_t size;

	size = fy_iovec_size(iov, iovcnt);
	if (size == SIZE_MAX)
		return NULL;

	p = fy_linear_alloc(a, tag, size, align);
	if (!p)
		return NULL;

	fy_iovec_copy_from(iov, iovcnt, p);

	if (a->flags & FYAF_KEEP_STATS) {
		la = container_of(a, struct fy_linear_allocator, a);
		fy_atomic_fetch_add(&la->stores, 1);
		fy_atomic_fetch_add(&la->stores, size);
	}

	return p;
}

static const void *fy_linear_lookupv(struct fy_allocator *a, int tag,
				    const struct iovec *iov, int iovcnt, size_t align,
				    uint64_t hash)
{
	/* no way to lookup */
	return NULL;
}

static void fy_linear_release(struct fy_allocator *a, int tag, const void *data, size_t size)
{
	/* nothing */
}

static int fy_linear_get_tag(struct fy_allocator *a)
{
	/* always return 0, we don't do tags for linear */
	return 0;
}

static void fy_linear_release_tag(struct fy_allocator *a, int tag)
{
	/* nothing */
}

static int fy_linear_get_tag_count(struct fy_allocator *a)
{
	return 1;
}

static int fy_linear_set_tag_count(struct fy_allocator *a, unsigned int count)
{
	if (count != 1)
		return -1;
	return 0;
}

static void fy_linear_trim_tag(struct fy_allocator *a, int tag)
{
	/* nothing */
}

static void fy_linear_reset_tag(struct fy_allocator *a, int tag)
{
	struct fy_linear_allocator *la;

	if (tag)
		return;

	la = container_of(a, struct fy_linear_allocator, a);

	/* we just rewind */
	fy_atomic_store(&la->next, 0);
}

static struct fy_allocator_info *fy_linear_get_info(struct fy_allocator *a, int tag)
{
	struct fy_linear_allocator *la;
	struct fy_allocator_info *info;
	struct fy_allocator_tag_info *tag_info;
	struct fy_allocator_arena_info *arena_info;
	size_t next, size;

	/* only single tag (or all tags with 0) */
	if (tag != 0 && tag != FY_ALLOC_TAG_NONE)
		return NULL;

	la = container_of(a, struct fy_linear_allocator, a);

	/* one of each */
	size = sizeof(*info) +
	       sizeof(*tag_info) +
	       sizeof(*arena_info);

	info = malloc(size);
	if (!info)
		return NULL;
	memset(info, 0, sizeof(*info));

	tag_info = (void *)(info + 1);
	assert(((uintptr_t)tag_info % _Alignof(struct fy_allocator_tag_info)) == 0);
	arena_info = (void *)(tag_info + 1);
	assert(((uintptr_t)arena_info % _Alignof(struct fy_allocator_arena_info)) == 0);

	/* fill-in the single arena */
	next = fy_atomic_load(&la->next);

	arena_info->free = la->cfg.size - next;
	arena_info->used = next;
	arena_info->total = la->cfg.size;
	arena_info->data = la->start;
	arena_info->size = arena_info->used;

	/* there's a single tag for linear */
	tag_info->tag = 0;
	tag_info->free = arena_info->free;
	tag_info->used = arena_info->used;
	tag_info->total = arena_info->total;
	tag_info->num_arena_infos = 1;
	tag_info->arena_infos = arena_info;

	/* fill in the single tag */
	info->free = tag_info->free;
	info->used = tag_info->used;
	info->total = tag_info->total;
	info->num_tag_infos = 1;
	info->tag_infos = tag_info;

	return info;
}

static enum fy_allocator_cap_flags
fy_linear_get_caps(struct fy_allocator *a)
{
	return FYACF_HAS_CONTAINS | FYACF_HAS_EFFICIENT_CONTAINS;
}

static bool fy_linear_contains(struct fy_allocator *a, int tag, const void *ptr)
{
	struct fy_linear_allocator *la;

	la = container_of(a, struct fy_linear_allocator, a);
	return ptr >= la->start && ptr < (la->start + la->cfg.size);
}

const struct fy_allocator_ops fy_linear_allocator_ops = {
	.setup = fy_linear_setup,
	.cleanup = fy_linear_cleanup,
	.create = fy_linear_create,
	.destroy = fy_linear_destroy,
	.dump = fy_linear_dump,
	.alloc = fy_linear_alloc,
	.free = fy_linear_free,
	.update_stats = fy_linear_update_stats,
	.storev = fy_linear_storev,
	.lookupv = fy_linear_lookupv,
	.release = fy_linear_release,
	.get_tag = fy_linear_get_tag,
	.release_tag = fy_linear_release_tag,
	.get_tag_count = fy_linear_get_tag_count,
	.set_tag_count = fy_linear_set_tag_count,
	.trim_tag = fy_linear_trim_tag,
	.reset_tag = fy_linear_reset_tag,
	.get_info = fy_linear_get_info,
	.get_caps = fy_linear_get_caps,
	.contains = fy_linear_contains,
};
