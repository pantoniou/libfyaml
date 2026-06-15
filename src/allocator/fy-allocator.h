/*
 * fy-allocator.h - allocators
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_ALLOCATOR_H
#define FY_ALLOCATOR_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#ifndef _WIN32
#include <sys/uio.h>
#endif

#include "fy-typelist.h"
#include "fy-utils.h"
#include "fy-win32.h"

#include <libfyaml.h>

struct fy_allocator;
struct fy_allocator_stats;

struct fy_allocator_stats;
struct fy_allocator_info;
struct fy_allocator_snapshot;

struct fy_allocator_ops {
	int (*setup)(struct fy_allocator *a, struct fy_allocator *parent, int parent_tag, const void *cfg);
	void (*cleanup)(struct fy_allocator *a);
	struct fy_allocator *(*create)(struct fy_allocator *parent, int parent_tag, const void *cfg);
	void (*destroy)(struct fy_allocator *a);
	void (*dump)(struct fy_allocator *a);
	void *(*alloc)(struct fy_allocator *a, int tag, size_t size, size_t align);
	void (*free)(struct fy_allocator *a, int tag, void *data);
	int (*update_stats)(struct fy_allocator *a, int tag, struct fy_allocator_stats *stats);
	const void *(*storev)(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align, uint64_t hash);
	const void *(*lookupv)(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align, uint64_t hash);
	void (*release)(struct fy_allocator *a, int tag, const void *data, size_t size);
	int (*get_tag)(struct fy_allocator *a);
	void (*release_tag)(struct fy_allocator *a, int tag);
	int (*get_tag_count)(struct fy_allocator *a);
	int (*set_tag_count)(struct fy_allocator *a, unsigned int tag_count);
	void (*trim_tag)(struct fy_allocator *a, int tag);
	void (*reset_tag)(struct fy_allocator *a, int tag);
	struct fy_allocator_info *(*get_info)(struct fy_allocator *a, int tag);
	enum fy_allocator_cap_flags (*get_caps)(struct fy_allocator *a);	/* Get capability flags (FYACF_*) */
	bool (*contains)(struct fy_allocator *a, int tag, const void *ptr);
	/* durable snapshot support (optional; NULL if unsupported) */
	int (*snapshot)(struct fy_allocator *a, int tag, struct fy_allocator_snapshot *snap);
	void (*snapshot_release)(struct fy_allocator *a, struct fy_allocator_snapshot *snap);
	/* durable arena operations */
	int (*sync)(struct fy_allocator *a);				/* flush dirty content to durable storage */
	uint64_t (*refs_get)(struct fy_allocator *a);			/* read the persistent refs head word */
	int (*refs_publish)(struct fy_allocator *a, uint64_t expected,	/* CAS-publish the refs head word */
			    uint64_t desired, unsigned int flags);
	uint64_t (*generation)(struct fy_allocator *a);			/* chunk generation counter */
	unsigned int (*chunk_count)(struct fy_allocator *a);		/* live published chunk count */
	uint64_t (*region_base)(struct fy_allocator *a);		/* fixed region base, or 0 */
	uint64_t (*region_size)(struct fy_allocator *a);		/* reserved region size, or 0 */
	uint64_t (*index_region_base)(struct fy_allocator *a);		/* separate dedup-index base, or 0 */
	uint64_t (*index_region_size)(struct fy_allocator *a);		/* separate dedup-index size, or 0 */
	int (*checkpoint)(struct fy_allocator *a);			/* write a new content integrity slot */
	int (*verify)(struct fy_allocator *a);				/* verify against the latest valid slot; 0=ok */
};

/* a durable snapshot of an allocator */
struct fy_allocator_snapshot {
	int tag;		/* the tag captured */
	int content_tag;	/* parent tag holding the content (values) */
	int index_tag;		/* parent tag holding the merged self-contained index */
	void *root;		/* merged tag-data within the index region */
	struct fy_allocator_info *info;	/* content + index regions; freed by snapshot_release */
};

struct fy_allocator_stats {
	union {
		struct {
			uint64_t allocations;
			uint64_t allocated;
			uint64_t frees;
			uint64_t freed;
			uint64_t stores;
			uint64_t stored;
			uint64_t releases;
			uint64_t released;
			uint64_t dup_stores;
			uint64_t dup_saved;
			uint64_t system_claimed;
			uint64_t system_free;
			uint64_t collisions;
			uint64_t unique_stores;
		};
		uint64_t counters[14];
	};
};

struct fy_allocator_arena_info {
	size_t free, used, total;
	void *data;
	size_t size;
};

struct fy_allocator_tag_info {
	int tag;
	size_t free, used, total;
	unsigned int num_arena_infos;
	struct fy_allocator_arena_info *arena_infos;
};

struct fy_allocator_info {
	size_t free, used, total;
	unsigned int num_tag_infos;
	struct fy_allocator_tag_info *tag_infos;
};

/*
 * Arena relocation primitive.
 *
 * Describes a contiguous source memory range and where it has been (or will be)
 * copied to. A sorted array of these is used to rebase pointers that point into
 * the source ranges to their corresponding destination ranges. Used by the
 * generic value relocator and by allocators (e.g. dedup) that need to relocate
 * their own internal pointers when an arena image is saved/restored.
 */
union fy_arena_reloc_ptr {
	void *p;
	uintptr_t i;
};

struct fy_arena_reloc {
	union fy_arena_reloc_ptr src;
	union fy_arena_reloc_ptr srce;
	union fy_arena_reloc_ptr dst;
	size_t size;
};

/* binary search a sorted-by-src reloc array for the range containing ptr */
const struct fy_arena_reloc *
fy_arena_locate_by_src(const struct fy_arena_reloc *arenas, unsigned int count, const void *ptr);

/*
 * Rebase a raw pointer through the reloc array.
 *
 * Returns the rebased pointer if it falls within one of the source ranges.
 * If it does not, but lies within [start, start+size) (i.e. already points at
 * the destination image - the fixed-base no-op case), it is returned unchanged.
 * Otherwise NULL is returned to signal an error. A NULL input returns NULL.
 */
void *fy_arena_reloc_ptr(const struct fy_arena_reloc *arenas, unsigned int count,
			 const void *start, size_t size, const void *ptr);

enum fy_allocator_flags {
	FYAF_KEEP_STATS = FY_BIT(0),
	FYAF_TRACE	= FY_BIT(1),
	FYAF_OWNS_PARENT = FY_BIT(2),
};

struct fy_allocator {
	enum fy_allocator_flags flags;
	const char *name;
	const struct fy_allocator_ops *ops;
	struct fy_allocator *parent;
	int parent_tag;
};

/* these private still */
int fy_allocator_update_stats(struct fy_allocator *a, int tag, struct fy_allocator_stats *stats);
struct fy_allocator_info *fy_allocator_get_info(struct fy_allocator *a, int tag);

static inline void fy_allocator_set_keep_stats(struct fy_allocator *a, bool keep_flags)
{
	if (!a)
		return;

	if (keep_flags)
		a->flags |= FYAF_KEEP_STATS;
	else
		a->flags &= ~FYAF_KEEP_STATS;
}

static inline void fy_allocator_set_trace(struct fy_allocator *a, bool trace)
{
	if (!a)
		return;

	if (trace)
		a->flags |= FYAF_TRACE;
	else
		a->flags &= ~FYAF_TRACE;
}

FY_TYPE_FWD_DECL_LIST(registered_allocator_entry);
struct fy_registered_allocator_entry {
	struct list_head node;
	const char *name;
	const struct fy_allocator_ops *ops;
};
FY_TYPE_DECL_LIST(registered_allocator_entry);

/* these are private still */
char *fy_allocator_get_names(void);
int fy_allocator_register(const char *name, const struct fy_allocator_ops *ops);
int fy_allocator_unregister(const char *name);
void fy_allocator_release(struct fy_allocator *a, int tag, const void *ptr, size_t size);

/* respects the parent allocator (or uses posix_memalign if NULL) */
void *fy_early_parent_allocator_alloc(struct fy_allocator *parent, int parent_tag, size_t size, size_t align);
void fy_early_parent_allocator_free(struct fy_allocator *parent, int parent_tag, void *ptr);
void *fy_parent_allocator_alloc(struct fy_allocator *a, size_t size, size_t align);
void fy_parent_allocator_free(struct fy_allocator *a, void *ptr);

#define FY_PARENT_ALLOCATOR_MALLOC	((struct fy_allocator *)NULL)
#define FY_PARENT_ALLOCATOR_INPLACE	((struct fy_allocator *)(uintptr_t)-1)

static inline struct fy_allocator *
fy_allocator_get_parent(struct fy_allocator *a)
{
	if (!a || !a->parent || a->parent == FY_PARENT_ALLOCATOR_INPLACE)
		return NULL;
	return a->parent;
}

struct fy_allocator *
fy_allocator_create_internal(const char *name, struct fy_allocator *parent, int parent_tag, const void *cfg);

static inline void fy_allocator_destroy_nocheck(struct fy_allocator *a)
{
	struct fy_allocator *parent_to_destroy = NULL;

	parent_to_destroy = (a->flags & FYAF_OWNS_PARENT) ? fy_allocator_get_parent(a) : NULL;

	a->ops->destroy(a);

	if (parent_to_destroy)
		fy_allocator_destroy_nocheck(parent_to_destroy);
}

static inline void fy_allocator_dump_nocheck(struct fy_allocator *a)
{
	a->ops->dump(a);
}

static inline int fy_allocator_update_stats_nocheck(struct fy_allocator *a, int tag, struct fy_allocator_stats *stats)
{
	return a->ops->update_stats(a, tag, stats);
}

static inline void *fy_allocator_alloc_nocheck(struct fy_allocator *a, int tag, size_t size, size_t align)
{
	return a->ops->alloc(a, tag, size, align);
}

static inline void fy_allocator_free_nocheck(struct fy_allocator *a, int tag, void *ptr)
{
	a->ops->free(a, tag, ptr);
}

static inline const void *fy_allocator_store_nocheck(struct fy_allocator *a, int tag, const void *data, size_t size, size_t align)
{
	struct iovec iov[1];

	/* just call the storev */
	iov[0].iov_base = (void *)data;
	iov[0].iov_len = size;

	return a->ops->storev(a, tag, iov, 1, align, 0);
}

static inline const void *fy_allocator_lookup_nocheck(struct fy_allocator *a, int tag, const void *data, size_t size, size_t align)
{
	struct iovec iov[1];

	/* just call the storev */
	iov[0].iov_base = (void *)data;
	iov[0].iov_len = size;

	return a->ops->lookupv(a, tag, iov, 1, align, 0);
}
static inline const void *fy_allocator_lookupv_nocheck(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align, uint64_t hash)
{
	return a->ops->lookupv(a, tag, iov, iovcnt, align, hash);
}

static inline const void *fy_allocator_storev_hash_nocheck(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align, uint64_t hash)
{
	return a->ops->storev(a, tag, iov, iovcnt, align, hash);
}

static inline const void *fy_allocator_storev_nocheck(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align)
{
	return a->ops->storev(a, tag, iov, iovcnt, align, 0);
}

static inline void fy_allocator_release_nocheck(struct fy_allocator *a, int tag, const void *ptr, size_t size)
{
	a->ops->release(a, tag, ptr, size);
}

static inline int fy_allocator_get_tag_nocheck(struct fy_allocator *a)
{
	return a->ops->get_tag(a);
}

static inline void fy_allocator_release_tag_nocheck(struct fy_allocator *a, int tag)
{
	a->ops->release_tag(a, tag);
}

static inline int fy_allocator_get_tag_count_nocheck(struct fy_allocator *a)
{
	return a->ops->get_tag_count(a);
}

static inline int fy_allocator_set_tag_count_nocheck(struct fy_allocator *a, unsigned int count)
{
	return a->ops->set_tag_count(a, count);
}

static inline void fy_allocator_trim_tag_nocheck(struct fy_allocator *a, int tag)
{
	a->ops->trim_tag(a, tag);
}

static inline void fy_allocator_reset_tag_nocheck(struct fy_allocator *a, int tag)
{
	a->ops->reset_tag(a, tag);
}

static inline struct fy_allocator_info *
fy_allocator_get_info_nocheck(struct fy_allocator *a, int tag)
{
	return a->ops->get_info(a, tag);
}

static inline enum fy_allocator_cap_flags
fy_allocator_get_caps_nocheck(struct fy_allocator *a)
{
	return a->ops->get_caps(a);
}

static inline bool
fy_allocator_contains_nocheck(struct fy_allocator *a, int tag, const void *ptr)
{
	return a->ops->contains(a, tag, ptr);
}

static inline int
fy_allocator_snapshot_nocheck(struct fy_allocator *a, int tag, struct fy_allocator_snapshot *snap)
{
	if (!a->ops->snapshot)
		return -1;
	return a->ops->snapshot(a, tag, snap);
}

static inline void
fy_allocator_snapshot_release_nocheck(struct fy_allocator *a, struct fy_allocator_snapshot *snap)
{
	if (a->ops->snapshot_release)
		a->ops->snapshot_release(a, snap);
}

static inline int
fy_allocator_snapshot(struct fy_allocator *a, int tag, struct fy_allocator_snapshot *snap)
{
	if (!a || !snap)
		return -1;
	return fy_allocator_snapshot_nocheck(a, tag, snap);
}

static inline void
fy_allocator_snapshot_release(struct fy_allocator *a, struct fy_allocator_snapshot *snap)
{
	if (!a || !snap)
		return;
	fy_allocator_snapshot_release_nocheck(a, snap);
}

/* flags for fy_allocator_refs_publish() */
#define FY_ALLOC_REFS_CHECKPOINT	(1u << 0)	/* enforce the durability ordering barrier */

static inline int fy_allocator_sync(struct fy_allocator *a)
{
	if (!a || !a->ops->sync)
		return -1;
	return a->ops->sync(a);
}

static inline uint64_t fy_allocator_refs_get(struct fy_allocator *a)
{
	if (!a || !a->ops->refs_get)
		return 0;
	return a->ops->refs_get(a);
}

static inline int fy_allocator_refs_publish(struct fy_allocator *a, uint64_t expected,
					    uint64_t desired, unsigned int flags)
{
	if (!a || !a->ops->refs_publish)
		return -1;
	return a->ops->refs_publish(a, expected, desired, flags);
}

static inline uint64_t fy_allocator_generation(struct fy_allocator *a)
{
	if (!a || !a->ops->generation)
		return 0;
	return a->ops->generation(a);
}

static inline unsigned int fy_allocator_chunk_count(struct fy_allocator *a)
{
	if (!a || !a->ops->chunk_count)
		return 0;
	return a->ops->chunk_count(a);
}

static inline uint64_t fy_allocator_region_base(struct fy_allocator *a)
{
	if (!a || !a->ops->region_base)
		return 0;
	return a->ops->region_base(a);
}

static inline uint64_t fy_allocator_region_size(struct fy_allocator *a)
{
	if (!a || !a->ops->region_size)
		return 0;
	return a->ops->region_size(a);
}

static inline uint64_t fy_allocator_index_region_base(struct fy_allocator *a)
{
	if (!a || !a->ops->index_region_base)
		return 0;
	return a->ops->index_region_base(a);
}

static inline uint64_t fy_allocator_index_region_size(struct fy_allocator *a)
{
	if (!a || !a->ops->index_region_size)
		return 0;
	return a->ops->index_region_size(a);
}

static inline int fy_allocator_checkpoint(struct fy_allocator *a)
{
	if (!a || !a->ops->checkpoint)
		return -1;
	return a->ops->checkpoint(a);
}

static inline int fy_allocator_verify(struct fy_allocator *a)
{
	if (!a || !a->ops->verify)
		return -1;
	return a->ops->verify(a);
}

#endif
