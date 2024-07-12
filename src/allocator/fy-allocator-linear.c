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

#include <stdio.h>

#include "fy-utils.h"
#include "fy-allocator-linear.h"

static int fy_linear_setup(struct fy_allocator *a, const void *cfg_data)
{
	struct fy_linear_allocator *la;
	const struct fy_linear_allocator_cfg *cfg;
	void *buf, *alloc = NULL;

	if (!a || !cfg_data)
		return -1;

	cfg = cfg_data;
	if (!cfg->size)
		return -1;

	if (!cfg->buf) {
		alloc = malloc(cfg->size);
		if (!alloc)
			goto err_out;
		buf = alloc;
	} else
		buf = cfg->buf;

	la = container_of(a, struct fy_linear_allocator, a);
	memset(la, 0, sizeof(*la));
	la->a.name = "linear";
	la->a.ops = &fy_linear_allocator_ops;
	la->cfg = *cfg;
	la->alloc = alloc;
	la->start = buf;
	la->next = buf;
	la->end = buf + cfg->size;

	return 0;
err_out:
	if (alloc)
		free(alloc);
	return -1;
}

static void fy_linear_cleanup(struct fy_allocator *a)
{
	struct fy_linear_allocator *la;

	if (!a)
		return;

	la = container_of(a, struct fy_linear_allocator, a);
	if (la->alloc) {
		free(la->alloc);
		la->alloc = NULL;
	}
}

struct fy_allocator *fy_linear_create(const void *cfg_data)
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
		alloc = malloc(cfg->size);
		if (!alloc)
			goto err_out;
		buf = alloc;
	} else
		buf = cfg->buf;

	s = buf;
	e = s + cfg->size;

	s = fy_ptr_align(s, alignof(struct fy_linear_allocator));
	if ((size_t)(e - s) < sizeof(*la))
		goto err_out;

	la = s;
	s += sizeof(*la);

	memset(&newcfg, 0, sizeof(newcfg));
	newcfg.buf = s;
	newcfg.size = (size_t)(e - s);

	rc = fy_linear_setup(&la->a, &newcfg);
	if (rc)
		goto err_out;

	assert(!la->alloc);
	la->alloc = alloc;

	return &la->a;

err_out:
	if (alloc)
		free(alloc);

	return NULL;
}

void fy_linear_destroy(struct fy_allocator *a)
{
	struct fy_linear_allocator *la;
	void *alloc;

	if (!a)
		return;

	la = container_of(a, struct fy_linear_allocator, a);

	/* take out the allocation of create */
	alloc = la->alloc;
	la->alloc = NULL;

	fy_linear_cleanup(a);

	if (alloc)
		free(alloc);
}

void fy_linear_dump(struct fy_allocator *a)
{
	struct fy_linear_allocator *la;

	if (!a)
		return;

	la = container_of(a, struct fy_linear_allocator, a);

	fprintf(stderr, "linear: total %zu used %zu free %zu\n",
			(size_t)(la->end - la->start),
			(size_t)(la->next - la->start),
			(size_t)(la->end - la->next));
}

static void *fy_linear_alloc(struct fy_allocator *a, int tag, size_t size, size_t align)
{
	struct fy_linear_allocator *la;
	void *s;

	assert(a);

	la = container_of(a, struct fy_linear_allocator, a);
	if (la->next >= la->end)
		goto err_out;

	s = fy_ptr_align(la->next, align);
	if (s >= la->end || (size_t)(la->end - s) < size)
		goto err_out;

	la->stats_allocations++;
	la->stats_allocated += size;

	memset(s, 0, size);

	la->next = s + size;

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

	if (!a || !stats)
		return -1;

	la = container_of(a, struct fy_linear_allocator, a);

	stats->allocations = la->stats_allocations;
	stats->allocated = la->stats_allocated;

	la->stats_allocations = 0;
	la->stats_allocated = 0;

	return 0;
}

static const void *fy_linear_store(struct fy_allocator *a, int tag, const void *data, size_t size, size_t align)
{
	void *p;

	if (!a)
		return NULL;

	p = fy_linear_alloc(a, tag, size, align);
	if (!p)
		goto err_out;

	memcpy(p, data, size);

	return p;

err_out:
	return NULL;
}

static const void *fy_linear_storev(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align)
{
	void *p, *start;
	int i;
	size_t size;

	if (!a)
		return NULL;

	size = 0;
	for (i = 0; i < iovcnt; i++)
		size += iov[i].iov_len;

	start = fy_linear_alloc(a, tag, size, align);
	if (!start)
		goto err_out;

	for (i = 0, p = start; i < iovcnt; i++, p += size) {
		size = iov[i].iov_len;
		memcpy(p, iov[i].iov_base, size);
	}

	return start;

err_out:
	return NULL;
}

static void fy_linear_release(struct fy_allocator *a, int tag, const void *data, size_t size)
{
	/* nothing */
}

static int fy_linear_get_tag(struct fy_allocator *a)
{
	if (!a)
		return FY_ALLOC_TAG_ERROR;

	/* always return 0, we don't do tags for linear */
	return 0;
}

static void fy_linear_release_tag(struct fy_allocator *a, int tag)
{
	struct fy_linear_allocator *la;

	if (!a)
		return;

	/* we only give out 0 as a tag */
	assert(tag == 0);

	la = container_of(a, struct fy_linear_allocator, a);

	/* we just rewind */
	la->next = la->start;
}

static void fy_linear_trim_tag(struct fy_allocator *a, int tag)
{
	/* nothing */
}

static void fy_linear_reset_tag(struct fy_allocator *a, int tag)
{
	/* nothing */
}

static struct fy_allocator_info *fy_linear_get_info(struct fy_allocator *a, int tag)
{
	struct fy_linear_allocator *la;
	struct fy_allocator_info *info;
	struct fy_allocator_tag_info *tag_info;
	struct fy_allocator_arena_info *arena_info;
	size_t size;

	if (!a)
		return NULL;

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
	assert(((uintptr_t)tag_info % alignof(struct fy_allocator_tag_info)) == 0);
	arena_info = (void *)(tag_info + 1);
	assert(((uintptr_t)arena_info % alignof(struct fy_allocator_arena_info)) == 0);

	/* fill-in the single arena */
	arena_info->free = (size_t)(la->end - la->next);
	arena_info->used = (size_t)(la->next - la->start);
	arena_info->total = (size_t)(la->end - (void *)la);
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

const struct fy_allocator_ops fy_linear_allocator_ops = {
	.setup = fy_linear_setup,
	.cleanup = fy_linear_cleanup,
	.create = fy_linear_create,
	.destroy = fy_linear_destroy,
	.dump = fy_linear_dump,
	.alloc = fy_linear_alloc,
	.free = fy_linear_free,
	.update_stats = fy_linear_update_stats,
	.store = fy_linear_store,
	.storev = fy_linear_storev,
	.release = fy_linear_release,
	.get_tag = fy_linear_get_tag,
	.release_tag = fy_linear_release_tag,
	.trim_tag = fy_linear_trim_tag,
	.reset_tag = fy_linear_reset_tag,
	.get_info = fy_linear_get_info,
};
