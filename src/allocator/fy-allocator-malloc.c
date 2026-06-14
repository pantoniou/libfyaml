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
#include <stdbool.h>

/* for container_of */
#include "fy-list.h"
#include "fy-utils.h"
#include "fy-align.h"
#include "fy-win32.h"

#include "xxhash.h"

#include "fy-allocator-malloc.h"

#define FY_MALLOC_PTR_BUCKET_SIZE	4096
#define FY_MALLOC_PTR_DEFAULT_HEADS	512

#define FY_MALLOC_PTR_BUCKET_CAP \
	((FY_MALLOC_PTR_BUCKET_SIZE - sizeof(struct fy_malloc_ptr_bucket)) / \
	 sizeof(struct fy_malloc_entry))

static int
fy_malloc_ptr_set_setup(struct fy_malloc_ptr_set *set, size_t est_size)
{
	size_t nheads;

	nheads = est_size ? est_size / FY_MALLOC_PTR_BUCKET_SIZE :
			    FY_MALLOC_PTR_DEFAULT_HEADS;
	if (nheads < 1)
		nheads = 1;

	set->nheads = nheads;
	set->count = 0;
	set->heads = calloc(nheads, sizeof(*set->heads));
	return set->heads ? 0 : -1;
}

static void
fy_malloc_ptr_set_cleanup(struct fy_malloc_ptr_set *set)
{
	struct fy_malloc_ptr_bucket *b, *next;
	size_t i, j;

	if (!set->heads)
		return;

	for (i = 0; i < set->nheads; i++) {
		for (b = set->heads[i]; b; b = next) {
			next = b->next;
			for (j = 0; j < b->count; j++)
				fy_align_free(b->entries[j].ptr);
			free(b);
		}
	}
	free(set->heads);
	memset(set, 0, sizeof(*set));
}

static inline bool
fy_malloc_ptr_bucket_find(const struct fy_malloc_ptr_bucket *b,
			  void *ptr, size_t *idxp)
{
	uintptr_t key, k;
	size_t lo, mid, hi;

	key = (uintptr_t)ptr;
	lo = 0;
	hi = b->count;

	while (lo < hi) {
		mid = lo + (hi - lo) / 2;
		k = (uintptr_t)b->entries[mid].ptr;
		if (k == key) {
			*idxp = mid;
			return true;
		}
		if (key < k)
			hi = mid;
		else
			lo = mid + 1;
	}
	*idxp = lo;
	return false;
}

static int
fy_malloc_ptr_set_add(struct fy_malloc_ptr_set *set, void *ptr, size_t size)
{
	struct fy_malloc_ptr_bucket **lastp;
	struct fy_malloc_ptr_bucket *b, *last;
	uintptr_t key;
	size_t head, idx;

	key = (uintptr_t)ptr;
	head = XXH64(&key, sizeof(key), 0) % set->nheads;
	lastp = &set->heads[head];
	idx = 0;
	last = NULL;

	for (b = set->heads[head]; b; b = b->next) {
		if (fy_malloc_ptr_bucket_find(b, ptr, &idx))
			return 1;
		lastp = &b->next;
		last = b;
	}

	if (!last || last->count >= FY_MALLOC_PTR_BUCKET_CAP) {
		b = malloc(FY_MALLOC_PTR_BUCKET_SIZE);
		if (!b)
			return -1;
		b->next = NULL;
		b->count = 0;
		*lastp = b;
		last = b;
		idx = 0;
	} else {
		(void)fy_malloc_ptr_bucket_find(last, ptr, &idx);
	}

	if (idx < last->count)
		memmove(&last->entries[idx + 1], &last->entries[idx],
			(last->count - idx) * sizeof(last->entries[0]));
	last->entries[idx].ptr = ptr;
	last->entries[idx].size = size;
	last->count++;
	set->count++;
	return 0;
}

static bool
fy_malloc_ptr_set_del(struct fy_malloc_ptr_set *set, void *ptr, size_t *sizep)
{
	struct fy_malloc_ptr_bucket **bp;
	struct fy_malloc_ptr_bucket *b;
	uintptr_t key;
	size_t head, idx;

	key = (uintptr_t)ptr;
	head = XXH64(&key, sizeof(key), 0) % set->nheads;
	bp = &set->heads[head];

	for (b = *bp; b; bp = &b->next, b = b->next) {
		if (!fy_malloc_ptr_bucket_find(b, ptr, &idx))
			continue;

		if (sizep)
			*sizep = b->entries[idx].size;
		if (idx + 1 < b->count)
			memmove(&b->entries[idx], &b->entries[idx + 1],
				(b->count - idx - 1) * sizeof(b->entries[0]));
		b->count--;
		set->count--;
		if (!b->count) {
			*bp = b->next;
			free(b);
		}
		return true;
	}

	return false;
}

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

static int fy_malloc_tag_setup(struct fy_malloc_allocator *ma FY_UNUSED,
			       struct fy_malloc_tag *mt)
{
	memset(mt, 0, sizeof(*mt));
	if (fy_malloc_ptr_set_setup(&mt->ptrs, 0))
		return -1;
	atomic_flag_clear(&mt->lock);
	return 0;
}

static void fy_malloc_tag_cleanup(struct fy_malloc_allocator *ma FY_UNUSED,
				  struct fy_malloc_tag *mt)
{
	/* cleanup should happen from a single thread */
	fy_malloc_ptr_set_cleanup(&mt->ptrs);
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

	fy_parent_allocator_free(&ma->a, (void *)ma->ids);
	fy_parent_allocator_free(&ma->a, (void *)ma->tags);
}

static int fy_malloc_setup(struct fy_allocator *a,
			   struct fy_allocator *parent,
			   int parent_tag,
			   const void *data FY_UNUSED)
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
	if (fy_malloc_tag_setup(ma, mt))
		goto err_out;

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
	unsigned int i;
	size_t count, total, system_total, j;
	struct fy_malloc_ptr_bucket *b;

	ma = container_of(a, struct fy_malloc_allocator, a);

	fprintf(stderr, "malloc: ");
	for (i = 0; i < ma->tag_count; i++) {
		mt = fy_malloc_tag_from_tag(ma, i);
		if (!mt)
			continue;
		fy_malloc_tag_list_lock(mt);
		fprintf(stderr, "%c", !mt->ptrs.count ? '.' : 'x');
		fy_malloc_tag_list_unlock(mt);
	}
	fprintf(stderr, "\n");

	for (i = 0; i < ma->tag_count; i++) {
		mt = fy_malloc_tag_from_tag(ma, i);
		if (!mt)
			continue;
		fy_malloc_tag_list_lock(mt);
		count = mt->ptrs.count;
		total = system_total = 0;
		for (j = 0; j < mt->ptrs.nheads; j++) {
			for (b = mt->ptrs.heads[j]; b; b = b->next) {
				system_total += FY_MALLOC_PTR_BUCKET_SIZE;
				for (size_t k = 0; k < b->count; k++) {
					total += b->entries[k].size;
					system_total += b->entries[k].size;
				}
			}
		}
		fy_malloc_tag_list_unlock(mt);
		if (!count)
			continue;
		fprintf(stderr, "  %d: count %zu total %zu system %zu overhead %zu (%2.2f%%)\n", i,
				count, total, system_total, system_total - total,
				system_total ? 100.0 * (double)(system_total - total) / (double)system_total : 0.0);
	}
}

static void *fy_malloc_tag_alloc(struct fy_malloc_allocator *ma FY_UNUSED,
				 struct fy_malloc_tag *mt,
				 size_t size, size_t align)
{
	size_t max_align;
	void *mem;
	int rc;

	max_align = align > _Alignof(void *) ? align : _Alignof(void *);

	mem = fy_align_alloc(max_align, size);
	if (!mem)
		return NULL;

	fy_malloc_tag_list_lock(mt);
	rc = fy_malloc_ptr_set_add(&mt->ptrs, mem, size);
	fy_malloc_tag_list_unlock(mt);

	if (rc) {
		fy_align_free(mem);
		return NULL;
	}

	return mem;
}

static void fy_malloc_tag_free(struct fy_malloc_allocator *ma FY_UNUSED,
				       struct fy_malloc_tag *mt,
				       void *data)
{
	bool found;
	size_t size FY_UNUSED;

	fy_malloc_tag_list_lock(mt);
	found = fy_malloc_ptr_set_del(&mt->ptrs, data, &size);
	fy_malloc_tag_list_unlock(mt);

	if (found)
		fy_align_free(data);
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

static const void *fy_malloc_storev(struct fy_allocator *a,
				    int tag,
				    const struct iovec *iov,
				    int iovcnt,
				    size_t align,
				    uint64_t hash FY_UNUSED)
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

static const void *fy_malloc_lookupv(struct fy_allocator *a FY_UNUSED,
				     int tag FY_UNUSED,
				     const struct iovec *iov FY_UNUSED,
				     int iovcnt FY_UNUSED,
				     size_t align FY_UNUSED,
				     uint64_t hash FY_UNUSED)
{
	/* no lookups */
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

	if (fy_malloc_tag_setup(ma, mt))
		goto err_free_id;

	return (int)id;

err_free_id:
	fy_id_free(ma->ids, ma->tag_id_count, id);
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
	struct fy_malloc_tag *old_tags;
	fy_id_bits *old_ids;
	fy_id_bits *ids = NULL;
	struct fy_malloc_tag *mt, *mt_new, *tags = NULL;
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
		memset(tags, 0, tmpsz);

		/* copy over the old entries */
		for (i = 0; i < ma->tag_count; i++) {

			mt = fy_malloc_tag_from_tag(ma, i);
			if (!mt)
				continue;

			mt_new = &tags[i];

			*mt_new = *mt;
			memset(mt, 0, sizeof(*mt));

			fy_id_set_used(ids, tag_id_count, i);
		}
	}

	old_tags = ma->tags;
	old_ids = ma->ids;
	if (ma->tags != tags) {
		ma->tags = tags;
		fy_parent_allocator_free(&ma->a, old_tags);
	}
	if (ma->ids != ids) {
		ma->ids = ids;
		fy_parent_allocator_free(&ma->a, (void *)old_ids);
	}
	ma->tag_count = tag_count;
	ma->tag_id_count = tag_id_count;
	return 0;

err_out:
	fy_parent_allocator_free(&ma->a, tags);
	fy_parent_allocator_free(&ma->a, (void *)ids);
	return -1;
}

static void fy_malloc_trim_tag(struct fy_allocator *a FY_UNUSED,
			       int tag FY_UNUSED)
{
	/* nothing */
}

static void fy_malloc_reset_tag(struct fy_allocator *a, int tag)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;

	ma = container_of(a, struct fy_malloc_allocator, a);

	mt = fy_malloc_tag_from_tag(ma, tag);
	if (!mt)
		return;

	/* no lock, reset is single thread only */
	fy_malloc_tag_cleanup(ma, mt);
	fy_malloc_tag_setup(ma, mt);
}

static struct fy_allocator_info *
fy_malloc_get_info(struct fy_allocator *a, int tag)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	struct fy_allocator_info *info;
	struct fy_allocator_tag_info *tag_info;
	struct fy_allocator_arena_info *arena_info;
	struct fy_malloc_ptr_bucket *b;
	struct fy_malloc_entry *me;
	size_t size, free, used, total;
	size_t tag_free, tag_used, tag_total;
	size_t arena_free, arena_used, arena_total;
	unsigned int num_tags, num_arenas, i, j, k;
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

				for (j = 0; j < mt->ptrs.nheads; j++) {
					for (b = mt->ptrs.heads[j]; b; b = b->next) {
						tag_total += FY_MALLOC_PTR_BUCKET_SIZE;
						for (k = 0; k < b->count; k++) {
							me = &b->entries[k];
							arena_free = 0;
							arena_used = me->size;
							arena_total = me->size;

							tag_free += arena_free;
							tag_used += arena_used;
							tag_total += arena_total;

							if (!i) {
								num_arenas++;
							} else {
								arena_info->free = arena_free;
								arena_info->used = arena_used;
								arena_info->total = arena_total;
								arena_info->data = me->ptr;
								arena_info->size = me->size;
								arena_info++;
								tag_info->num_arena_infos++;
							}
						}
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
fy_malloc_get_caps(struct fy_allocator *a FY_UNUSED)
{
	return FYACF_CAN_FREE_INDIVIDUAL | FYACF_CAN_FREE_TAG |
	       FYACF_HAS_CONTAINS | FYACF_HAS_TAGS;
}

/* malloc allocator tracks allocation ranges in per-tag pointer buckets. */
static bool fy_malloc_contains(struct fy_allocator *a, int tag, const void *ptr)
{
	struct fy_malloc_allocator *ma;
	struct fy_malloc_tag *mt;
	struct fy_malloc_ptr_bucket *b;
	struct fy_malloc_entry *me;
	unsigned int i, j;
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
		for (i = 0; i < mt->ptrs.nheads; i++) {
			for (b = mt->ptrs.heads[i]; b; b = b->next) {
				for (j = 0; j < b->count; j++) {
					me = &b->entries[j];
					if (ptr >= (void *)me->ptr &&
					    ptr < (void *)((char *)me->ptr + me->size)) {
						fy_malloc_tag_list_unlock(mt);
						return true;
					}
				}
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
	.lookupv = fy_malloc_lookupv,
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
