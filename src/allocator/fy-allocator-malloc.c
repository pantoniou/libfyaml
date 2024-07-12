/*
 * fy-allocator-malloc.c - malloc allocator
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

#include <stdio.h>

/* for container_of */
#include "fy-list.h"
#include "fy-utils.h"

#include "fy-allocator-malloc.h"

static inline struct fy_malloc_tag *
fy_malloc_tag_from_tag(struct fy_malloc_allocator *ma, int tag)
{
	if (!ma)
		return NULL;

	if ((unsigned int)tag >= ARRAY_SIZE(ma->tags))
		return NULL;

	if (!fy_id_is_used(ma->ids, ARRAY_SIZE(ma->ids), (int)tag))
		return NULL;

	return &ma->tags[tag];
}

static int fy_malloc_setup(struct fy_allocator *a, const void *data)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	unsigned int i;

	if (!a)
		return -1;

	ma = container_of(a, struct fy_malloc_allocator, a);
	memset(ma, 0, sizeof(*ma));
	ma->a.name = "malloc";
	ma->a.ops = &fy_malloc_allocator_ops;

	fy_id_reset(ma->ids, ARRAY_SIZE(ma->ids));
	for (i = 0, mt = ma->tags; i < ARRAY_SIZE(ma->tags); i++, mt++)
		fy_malloc_entry_list_init(&mt->entries);

	return 0;
}

static void fy_malloc_cleanup(struct fy_allocator *a)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_entry *me;
	struct fy_malloc_tag *mt;
	unsigned int i;

	if (!a)
		return;

	ma = container_of(a, struct fy_malloc_allocator, a);

	for (i = 0, mt = ma->tags; i < ARRAY_SIZE(ma->tags); i++, mt++) {
		while ((me = fy_malloc_entry_list_pop(&mt->entries)) != NULL)
			free(me);
	}
}

struct fy_allocator *fy_malloc_create(const void *cfg)
{
	struct fy_malloc_allocator *ma = NULL;
	int rc;

	ma = malloc(sizeof(*ma));
	if (!ma)
		goto err_out;

	rc = fy_malloc_setup(&ma->a, cfg);
	if (rc)
		goto err_out;

	return &ma->a;

err_out:
	if (ma)
		free(ma);

	return NULL;
}

void fy_malloc_destroy(struct fy_allocator *a)
{
	struct fy_malloc_allocator *ma;

	if (!a)
		return;

	ma = container_of(a, struct fy_malloc_allocator, a);
	fy_malloc_cleanup(a);
	free(ma);
}

void fy_malloc_dump(struct fy_allocator *a)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	struct fy_malloc_entry *me;
	unsigned int i;
	size_t count, total, system_total;

	if (!a)
		return;

	ma = container_of(a, struct fy_malloc_allocator, a);

	fprintf(stderr, "malloc: ");
	for (i = 0, mt = ma->tags; i < ARRAY_SIZE(ma->tags); i++, mt++)
		fprintf(stderr, "%c", fy_malloc_entry_list_empty(&mt->entries) ? '.' : 'x');
	fprintf(stderr, "\n");

	for (i = 0, mt = ma->tags; i < ARRAY_SIZE(ma->tags); i++, mt++) {
		if (fy_malloc_entry_list_empty(&mt->entries))
			continue;
		count = total = system_total = 0;
		for (me = fy_malloc_entry_list_head(&mt->entries); me; me = fy_malloc_entry_next(&mt->entries, me)) {
			count++;
			total += me->size;
			system_total += sizeof(*me) + me->size;
		}
		fprintf(stderr, "  %d: count %zu total %zu system %zu overhead %zu (%2.2f%%)\n", i,
				count, total, system_total, system_total - total,
				100.0 * (double)(system_total - total) / (double)system_total);
	}
}

static void *fy_malloc_tag_alloc(struct fy_malloc_allocator *ma, struct fy_malloc_tag *mt, size_t size, size_t align)
{
	struct fy_malloc_entry *me;
	size_t reqsize;
	void *p;
	int ret;

	assert(align <= 16);

	reqsize = size;
	size = size + sizeof(*me);
	ret = posix_memalign(&p, 16, size);
	if (ret)
		goto err_out;

	me = p;
	assert(((uintptr_t)me & 15) == 0);
	assert(((uintptr_t)(&me->mem[0]) & 15) == 0);

	me->size = size;
	me->reqsize = reqsize;
	fy_malloc_entry_list_add_tail(&mt->entries, me);

	return &me->mem[0];

err_out:
	return NULL;
}

static void fy_malloc_tag_free(struct fy_malloc_allocator *ma, struct fy_malloc_tag *mt, void *data)
{
	struct fy_malloc_entry *me;

	me = container_of(data, struct fy_malloc_entry, mem);

	fy_malloc_entry_list_del(&mt->entries, me);
	free(me);
}

static void *fy_malloc_alloc(struct fy_allocator *a, int tag, size_t size, size_t align)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	void *p;

	if (!a || tag < 0)
		return NULL;

	/* maximum align is 16 TODO */
	if (align > 16)
		goto err_out;

	ma = container_of(a, struct fy_malloc_allocator, a);

	mt = fy_malloc_tag_from_tag(ma, tag);
	if (!mt)
		goto err_out;

	p = fy_malloc_tag_alloc(ma, mt, size, align);
	if (!p)
		goto err_out;

	mt->stats.allocations++;
	mt->stats.allocated += size;

	return p;

err_out:
	return NULL;
}

static void fy_malloc_free(struct fy_allocator *a, int tag, void *data)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;

	if (!a || tag < 0 || !data)
		return;

	ma = container_of(a, struct fy_malloc_allocator, a);

	mt = fy_malloc_tag_from_tag(ma, tag);
	if (!mt)
		return;

	fy_malloc_tag_free(ma, mt, data);

	mt->stats.frees++;
}

static int fy_malloc_update_stats(struct fy_allocator *a, int tag, struct fy_allocator_stats *stats)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	unsigned int i;

	if (!a || !stats)
		return -1;

	ma = container_of(a, struct fy_malloc_allocator, a);

	mt = fy_malloc_tag_from_tag(ma, tag);
	if (!mt)
		goto err_out;

	/* and update with this ones */
	for (i = 0; i < ARRAY_SIZE(mt->stats.counters); i++) {
		stats->counters[i] += mt->stats.counters[i];
		mt->stats.counters[i] = 0;
	}

	return 0;

err_out:
	return -1;
}

static const void *fy_malloc_storev(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	void *p, *start;
	int i;
	size_t size, total_size;

	if (!a || !iov)
		return NULL;

	ma = container_of(a, struct fy_malloc_allocator, a);

	mt = fy_malloc_tag_from_tag(ma, tag);
	if (!mt)
		goto err_out;

	total_size = 0;
	for (i = 0; i < iovcnt; i++)
		total_size += iov[i].iov_len;

	start = fy_malloc_tag_alloc(ma, mt, total_size, align);
	if (!start)
		goto err_out;

	for (i = 0, p = start; i < iovcnt; i++, p += size) {
		size = iov[i].iov_len;
		memcpy(p, iov[i].iov_base, size);
	}

	mt->stats.stores++;
	mt->stats.stored += total_size;

	return start;

err_out:
	return NULL;
}

static const void *fy_malloc_store(struct fy_allocator *a, int tag, const void *data, size_t size, size_t align)
{
	struct iovec iov[1];

	if (!a)
		return NULL;

	/* just call the storev */
	iov[0].iov_base = (void *)data;
	iov[0].iov_len = size;
	return fy_malloc_storev(a, tag, iov, 1, align);
}

static void fy_malloc_release(struct fy_allocator *a, int tag, const void *data, size_t size)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;

	if (!a || !data || !size)
		return;

	ma = container_of(a, struct fy_malloc_allocator, a);

	mt = fy_malloc_tag_from_tag(ma, tag);
	if (!mt)
		return;

	/* the malloc's release is just a free */
	fy_malloc_tag_free(ma, mt, (void *)data);

	mt->stats.releases++;
	mt->stats.released += size;
}

static void fy_malloc_release_tag(struct fy_allocator *a, int tag)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	struct fy_malloc_entry *me;

	if (!a)
		return;

	ma = container_of(a, struct fy_malloc_allocator, a);

	mt = fy_malloc_tag_from_tag(ma, tag);
	if (!mt)
		return;

	while ((me = fy_malloc_entry_list_pop(&mt->entries)) != NULL)
		free(me);

	fy_id_free(ma->ids, ARRAY_SIZE(ma->ids), tag);
}

static int fy_malloc_get_tag(struct fy_allocator *a)
{
	struct fy_malloc_allocator *ma;
	int id;

	if (!a)
		return FY_ALLOC_TAG_ERROR;

	ma = container_of(a, struct fy_malloc_allocator, a);

	id = fy_id_alloc(ma->ids, ARRAY_SIZE(ma->ids));
	if (id < 0)
		goto err_out;

	return (int)id;

err_out:
	return FY_ALLOC_TAG_ERROR;
}

static void fy_malloc_trim_tag(struct fy_allocator *a, int tag)
{
	/* nothing */
}

static void fy_malloc_reset_tag(struct fy_allocator *a, int tag)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	struct fy_malloc_entry *me;

	if (!a)
		return;

	ma = container_of(a, struct fy_malloc_allocator, a);

	mt = fy_malloc_tag_from_tag(ma, tag);
	if (!mt)
		return;

	while ((me = fy_malloc_entry_list_pop(&mt->entries)) != NULL)
		free(me);
}

static struct fy_allocator_info *
fy_malloc_get_info(struct fy_allocator *a, int tag)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	struct fy_malloc_entry *me;
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

	ma = container_of(a, struct fy_malloc_allocator, a);

	/* allocate for the worst case always */
	num_tags = 0;
	num_arenas = 0;

	free = 0;
	used = 0;
	total = 0;

	/* two passes */
	for (i = 0; i < 2; i++) {

		if (!i) {
			info = NULL;
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
		total = sizeof(*ma);

		for (id = 0; id < (int)ARRAY_SIZE(ma->tags); id++) {

			if (!fy_id_is_used(ma->ids, ARRAY_SIZE(ma->ids), id))
				continue;

			mt = &ma->tags[id];

			tag_free = 0;
			tag_used = 0;
			tag_total = 0;

			if (i) {
				tag_info->num_arena_infos = 0;
				tag_info->arena_infos = arena_info;
			}

			for (me = fy_malloc_entry_list_head(&mt->entries); me; me = fy_malloc_entry_next(&mt->entries, me)) {
				arena_free = 0;
				arena_used = me->reqsize;
				arena_total = sizeof(*me) + me->size;

				tag_free += arena_free;
				tag_used += arena_used;
				tag_total += arena_total;

				if (!i) {
					num_arenas++;
				} else {
					arena_info->free = arena_free;
					arena_info->used = arena_used;
					arena_info->total = arena_total;
					arena_info->data = me->mem;
					arena_info->size = me->reqsize;
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

const struct fy_allocator_ops fy_malloc_allocator_ops = {
	.setup = fy_malloc_setup,
	.cleanup = fy_malloc_cleanup,
	.create = fy_malloc_create,
	.destroy = fy_malloc_destroy,
	.dump = fy_malloc_dump,
	.alloc = fy_malloc_alloc,
	.free = fy_malloc_free,
	.update_stats = fy_malloc_update_stats,
	.store = fy_malloc_store,
	.storev = fy_malloc_storev,
	.release = fy_malloc_release,
	.get_tag = fy_malloc_get_tag,
	.release_tag = fy_malloc_release_tag,
	.trim_tag = fy_malloc_trim_tag,
	.reset_tag = fy_malloc_reset_tag,
	.get_info = fy_malloc_get_info,
};
