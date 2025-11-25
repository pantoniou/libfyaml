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

static inline void
fy_malloc_tag_list_lock(struct fy_malloc_tag *mt)
{
	int loops FY_DEBUG_UNUSED;

	loops = 0;
	while (!fy_atomic_flag_test_and_set(&mt->lock)) {
		loops++;
		assert(loops < 10000000);
		fy_cpu_relax();
	}
}

static inline void
fy_malloc_tag_list_unlock(struct fy_malloc_tag *mt)
{
	fy_atomic_flag_clear(&mt->lock);
}

static inline struct fy_malloc_tag *
fy_malloc_tag_from_tag(struct fy_malloc_allocator *ma, int tag)
{
	if (!ma)
		return NULL;

	if ((unsigned int)tag >= ma->tag_count)
		return NULL;

	if (!fy_id_is_used(ma->ids, ma->tag_id_count, (int)tag))
		return NULL;

	return &ma->tags[tag];
}

static void fy_malloc_tag_setup(struct fy_malloc_allocator *ma, struct fy_malloc_tag *mt)
{
	fy_malloc_entry_list_init(&mt->entries);
	atomic_flag_clear(&mt->lock);
}

static void fy_malloc_tag_cleanup(struct fy_malloc_allocator *ma, struct fy_malloc_tag *mt)
{
	struct fy_malloc_entry *me;

	/* cleanup should happen from a single thread */
	while ((me = fy_malloc_entry_list_pop(&mt->entries)) != NULL)
		free(me->mem);
}

static void fy_malloc_cleanup(struct fy_allocator *a)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	unsigned int i;

	if (!a)
		return;

	ma = container_of(a, struct fy_malloc_allocator, a);

	/* no need for locks, this is one thread only */
	for (i = 0; i < ma->tag_count; i++) {
		mt = fy_malloc_tag_from_tag(ma, i);
		if (!mt)
			continue;
		fy_malloc_tag_cleanup(ma, mt);
	}

	fy_parent_allocator_free(&ma->a, ma->ids);
	fy_parent_allocator_free(&ma->a, ma->tags);
}

static int fy_malloc_setup(struct fy_allocator *a, struct fy_allocator *parent, int parent_tag, const void *data)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	size_t tmpsz;

	if (!a)
		return -1;

	ma = container_of(a, struct fy_malloc_allocator, a);
	memset(ma, 0, sizeof(*ma));
	ma->a.name = "malloc";
	ma->a.ops = &fy_malloc_allocator_ops;
	ma->a.parent = parent;
	ma->a.parent_tag = parent_tag;

	ma->tag_count = FY_MALLOC_TAG_MAX;
	ma->tag_id_count = (ma->tag_count + FY_ID_BITS_BITS - 1) / FY_ID_BITS_BITS;

	tmpsz = ma->tag_id_count * sizeof(*ma->ids);
	ma->ids = fy_parent_allocator_alloc(&ma->a, tmpsz, _Alignof(fy_id_bits));
	if (!ma->ids)
		goto err_out;
	fy_id_reset(ma->ids, ma->tag_id_count);

	tmpsz = ma->tag_count * sizeof(*ma->tags);
	ma->tags = fy_parent_allocator_alloc(&ma->a, tmpsz, _Alignof(struct fy_malloc_tag));
	if (!ma->tags)
		goto err_out;

	/* start with tag 0 as general use */
	fy_id_set_used(ma->ids, ma->tag_id_count, 0);
	mt = fy_malloc_tag_from_tag(ma, 0);
	fy_malloc_tag_setup(ma, mt);

	return 0;

err_out:
	fy_malloc_cleanup(a);
	return -1;
}

struct fy_allocator *fy_malloc_create(struct fy_allocator *parent, int parent_tag, const void *cfg)
{
	struct fy_malloc_allocator *ma = NULL;
	int rc;

	ma = fy_early_parent_allocator_alloc(parent, parent_tag, sizeof(*ma), _Alignof(struct fy_malloc_allocator));
	if (!ma)
		goto err_out;

	rc = fy_malloc_setup(&ma->a, parent, parent_tag, cfg);
	if (rc)
		goto err_out;

	return &ma->a;

err_out:
	fy_early_parent_allocator_free(parent, parent_tag, ma);
	return NULL;
}

void fy_malloc_destroy(struct fy_allocator *a)
{
	struct fy_malloc_allocator *ma;

	ma = container_of(a, struct fy_malloc_allocator, a);
	fy_malloc_cleanup(a);

	fy_parent_allocator_free(a, ma);
}

void fy_malloc_dump(struct fy_allocator *a)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	struct fy_malloc_entry *me;
	unsigned int i;
	size_t count, total, system_total;

	ma = container_of(a, struct fy_malloc_allocator, a);

	fprintf(stderr, "malloc: ");
	for (i = 0; i < ma->tag_count; i++) {
		mt = fy_malloc_tag_from_tag(ma, i);
		if (!mt)
			continue;
		fy_malloc_tag_list_lock(mt);
		fprintf(stderr, "%c", fy_malloc_entry_list_empty(&mt->entries) ? '.' : 'x');
		fy_malloc_tag_list_unlock(mt);
	}
	fprintf(stderr, "\n");

	for (i = 0; i < ma->tag_count; i++) {
		mt = fy_malloc_tag_from_tag(ma, i);
		if (!mt)
			continue;
		fy_malloc_tag_list_lock(mt);
		if (!fy_malloc_entry_list_empty(&mt->entries)) {
			count = total = system_total = 0;
			for (me = fy_malloc_entry_list_head(&mt->entries); me; me = fy_malloc_entry_next(&mt->entries, me)) {
				count++;
				total += me->size;
				system_total += sizeof(*me) + me->size;
			}
		}
		fy_malloc_tag_list_unlock(mt);
		if (!count)
			continue;
		fprintf(stderr, "  %d: count %zu total %zu system %zu overhead %zu (%2.2f%%)\n", i,
				count, total, system_total, system_total - total,
				100.0 * (double)(system_total - total) / (double)system_total);
	}
}

static void *fy_malloc_tag_alloc(struct fy_malloc_allocator *ma, struct fy_malloc_tag *mt, size_t size, size_t align)
{
	struct fy_malloc_entry *me;
	size_t me_offset, max_align;
	int r;
	void *mem;

	me_offset = fy_size_t_align(size, _Alignof(struct fy_malloc_entry));
	max_align = align > _Alignof(struct fy_malloc_entry) ? align : _Alignof(struct fy_malloc_entry);

	r = posix_memalign(&mem, max_align, me_offset + sizeof(*me));
	if (r)
		return NULL;

	me = mem + me_offset;

	me->size = size;
	fy_malloc_tag_list_lock(mt);
	fy_malloc_entry_list_add_tail(&mt->entries, me);
	fy_malloc_tag_list_unlock(mt);
	me->mem = mem;

	return mem;
}

static void fy_malloc_tag_free(struct fy_malloc_allocator *ma, struct fy_malloc_tag *mt, void *data)
{
	struct fy_malloc_entry *me;

	me = container_of(data, struct fy_malloc_entry, mem);

	fy_malloc_tag_list_lock(mt);
	fy_malloc_entry_list_del(&mt->entries, me);
	fy_malloc_tag_list_unlock(mt);

	free(me->mem);
}

static void *fy_malloc_alloc(struct fy_allocator *a, int tag, size_t size, size_t align)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	void *p;

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

static const void *fy_malloc_storev(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align, uint64_t hash)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	void *start;
	size_t total_size;

	ma = container_of(a, struct fy_malloc_allocator, a);

	mt = fy_malloc_tag_from_tag(ma, tag);
	if (!mt)
		goto err_out;

	total_size = fy_iovec_size(iov, iovcnt);
	if (total_size == SIZE_MAX)
		goto err_out;

	start = fy_malloc_tag_alloc(ma, mt, total_size, align);
	if (!start)
		goto err_out;

	fy_iovec_copy_from(iov, iovcnt, start);

	mt->stats.stores++;
	mt->stats.stored += total_size;

	return start;

err_out:
	return NULL;
}

static void fy_malloc_release(struct fy_allocator *a, int tag, const void *data, size_t size)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;

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

	ma = container_of(a, struct fy_malloc_allocator, a);
	mt = fy_malloc_tag_from_tag(ma, tag);
	if (!mt)
		return;

	fy_malloc_tag_cleanup(ma, mt);

	fy_id_free(ma->ids, ma->tag_id_count, tag);
}

static int fy_malloc_get_tag(struct fy_allocator *a)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	int id;

	ma = container_of(a, struct fy_malloc_allocator, a);

	id = fy_id_alloc(ma->ids, ma->tag_id_count);
	if (id < 0)
		goto err_out;

	mt = fy_malloc_tag_from_tag(ma, id);
	if (!mt)
		goto err_out;

	fy_malloc_tag_setup(ma, mt);

	return (int)id;

err_out:
	return FY_ALLOC_TAG_ERROR;
}

static int fy_malloc_get_tag_count(struct fy_allocator *a)
{
	struct fy_malloc_allocator *ma;

	ma = container_of(a, struct fy_malloc_allocator, a);
	return ma->tag_count;
}

static int fy_malloc_set_tag_count(struct fy_allocator *a, unsigned int count)
{
	struct fy_malloc_allocator *ma;
	fy_id_bits *ids = NULL;
	struct fy_malloc_tag *mt, *mt_new, *tags = NULL;
	struct fy_malloc_entry *me;
	unsigned int i, tag_count, tag_id_count;
	size_t tmpsz;

	if (count < 1)
		return -1;

	ma = container_of(a, struct fy_malloc_allocator, a);
	if (count == ma->tag_count)
		return 0;

	tag_count = count;
	tag_id_count = (tag_count + FY_ID_BITS_BITS - 1) / FY_ID_BITS_BITS;

	/* shrink?, just clip */
	if (count < ma->tag_count) {
		for (i = count; i < ma->tag_count; i++) {
			mt = fy_malloc_tag_from_tag(ma, i);
			if (!mt)
				continue;
			fy_malloc_tag_cleanup(ma, mt);
			fy_id_free(ma->ids, ma->tag_id_count, i);
		}
		tags = ma->tags;
		ids = ma->ids;
	} else {
		/* we need to grow */
		tmpsz = tag_id_count * sizeof(*ma->ids);
		ids = fy_parent_allocator_alloc(&ma->a, tmpsz, _Alignof(fy_id_bits));
		if (!ids)
			goto err_out;
		fy_id_reset(ids, tag_id_count);

		tmpsz = tag_count * sizeof(*tags);
		tags = fy_parent_allocator_alloc(&ma->a, tmpsz, _Alignof(struct fy_malloc_tag));
		if (!tags)
			goto err_out;

		/* copy over the old entries */
		for (i = 0; i < ma->tag_count; i++) {

			mt = fy_malloc_tag_from_tag(ma, i);
			if (!mt)
				continue;

			mt_new = &tags[i];

			fy_malloc_tag_setup(ma, mt_new);
			/* move the old entries over */
			fy_malloc_tag_list_lock(mt);
			while ((me = fy_malloc_entry_list_pop(&mt->entries)) != NULL)
				fy_malloc_entry_list_add_tail(&mt_new->entries, me);
			fy_malloc_tag_list_unlock(mt);
			memcpy(&mt_new->stats, &mt->stats, sizeof(mt->stats));

			/* clean the old entry */
			fy_malloc_tag_cleanup(ma, mt);

			fy_id_set_used(ids, tag_id_count, i);
		}
	}

	if (ma->tags != tags) {
		ma->tags = tags;
		fy_parent_allocator_free(&ma->a, ma->tags);
	}
	if (ma->ids != ids) {
		ma->ids = ids;
		fy_parent_allocator_free(&ma->a, ma->ids);
	}
	ma->tag_count = tag_count;
	ma->tag_id_count = tag_id_count;
	return 0;

err_out:
	fy_parent_allocator_free(&ma->a, tags);
	fy_parent_allocator_free(&ma->a, ids);
	return -1;
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

	ma = container_of(a, struct fy_malloc_allocator, a);

	mt = fy_malloc_tag_from_tag(ma, tag);
	if (!mt)
		return;

	/* no lock, reset is single thread only */
	while ((me = fy_malloc_entry_list_pop(&mt->entries)) != NULL)
		free(me->mem);
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
		total = sizeof(*ma);

		for (id = 0; id < (int)ma->tag_count; id++) {

			if (!fy_id_is_used(ma->ids, ma->tag_id_count, id))
				continue;

			mt = &ma->tags[id];

			tag_free = 0;
			tag_used = 0;
			tag_total = 0;

			if (i) {
				tag_info->num_arena_infos = 0;
				tag_info->arena_infos = arena_info;
			}

			fy_malloc_tag_list_lock(mt);

			for (me = fy_malloc_entry_list_head(&mt->entries); me; me = fy_malloc_entry_next(&mt->entries, me)) {
				arena_free = 0;
				arena_used = me->size;
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
					arena_info->size = me->size;
					arena_info++;
					tag_info->num_arena_infos++;
				}
			}
			fy_malloc_tag_list_unlock(mt);

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
fy_malloc_get_caps(struct fy_allocator *a)
{
	return FYACF_CAN_FREE_INDIVIDUAL | FYACF_CAN_FREE_TAG |
	       FYACF_HAS_CONTAINS | FYACF_HAS_TAGS;
}

/* malloc allocator actually tracks allocations, it's just very inefficient
 * to scan through. It might be good for debugging though
 */
static bool fy_malloc_contains(struct fy_allocator *a, int tag, const void *ptr)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	struct fy_malloc_entry *me;
	int tag_start, tag_end;

	ma = container_of(a, struct fy_malloc_allocator, a);
	if (tag >= 0) {
		if (tag >= (int)ma->tag_count)
			return false;
		tag_start = tag;
		tag_end = tag_start + 1;
	} else {
		tag_start = 0;
		tag_end = ma->tag_count;
	}

	for (tag = tag_start; tag < tag_end; tag++) {
		mt = fy_malloc_tag_from_tag(ma, tag);
		if (!mt)
			continue;

		fy_malloc_tag_list_lock(mt);
		for (me = fy_malloc_entry_list_head(&mt->entries); me;
		     me = fy_malloc_entry_next(&mt->entries, me)) {

			if (ptr >= (void *)me->mem && ptr < (void *)me->mem + me->size) {
				fy_malloc_tag_list_unlock(mt);
				return true;
			}
		}
		fy_malloc_tag_list_unlock(mt);
	}

	return false;
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
	.storev = fy_malloc_storev,
	.release = fy_malloc_release,
	.get_tag = fy_malloc_get_tag,
	.release_tag = fy_malloc_release_tag,
	.get_tag_count = fy_malloc_get_tag_count,
	.set_tag_count = fy_malloc_set_tag_count,
	.trim_tag = fy_malloc_trim_tag,
	.reset_tag = fy_malloc_reset_tag,
	.get_info = fy_malloc_get_info,
	.get_caps = fy_malloc_get_caps,
	.contains = fy_malloc_contains,
};
