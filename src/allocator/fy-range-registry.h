/*
 * fy-range-registry.h - process wide address range registry
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_RANGE_REGISTRY_H
#define FY_RANGE_REGISTRY_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct fy_allocator;

/*
 * Process wide registry of the address ranges allocators obtain, mapping
 * an address back to its owner and tag. Ranges either nest or are
 * disjoint; lookups return the innermost one. Nothing takes a lock, but
 * updates are expensive: register arenas, not allocations.
 *
 * A range may have a logical base other than its runtime address (a
 * durable region, which persisted data addresses by its region_base).
 */

struct fy_range_info {
	const void *start;
	size_t size;
	struct fy_allocator *owner;
	int tag;
	uintptr_t logical;	/* logical address of start */
};

/* 0 on success, -1 on a partial overlap, a duplicate or no memory */
int fy_range_register(const void *start, size_t size,
		      struct fy_allocator *owner, int tag);
int fy_range_register_logical(const void *start, size_t size,
			      struct fy_allocator *owner, int tag,
			      uintptr_t logical);

/* in place grow/trim; -1 if no such range, or on a partial overlap */
int fy_range_resize(const void *start, struct fy_allocator *owner, size_t size);

/* -1 if no such range */
int fy_range_unregister(const void *start, struct fy_allocator *owner);

/* the innermost range containing ptr (owned by owner) */
bool fy_range_lookup(const void *ptr, struct fy_range_info *info);
bool fy_range_lookup_owner(const void *ptr, struct fy_allocator *owner,
			   struct fy_range_info *info);

/* 1 if ptr is in a range, 0 if not (the logical address is ptr then),
 * -1 if the result would read back as part of another translated range */
int fy_range_ptr_to_logical(const void *ptr, uintptr_t *logicalp);

/* a pointer derived from the start of the range holding the logical
 * address (with the range's capability under FIL-C), or NULL */
void *fy_range_logical_to_ptr(uintptr_t addr);

/* pointer slots in memory other processes may map (a durable dedup index) */
#define FY_SLOT_LOAD(_slot)		(*(_slot))
#define FY_SLOT_STORE(_slot, _p)	(*(_slot) = (_p))
#define FY_SLOT_ALOAD(_slot)		fy_atomic_load(_slot)
#define FY_SLOT_ASTORE(_slot, _p)	fy_atomic_store((_slot), (_p))
#define FY_SLOT_CAS(_slot, _expp, _d)	fy_atomic_compare_exchange_strong((_slot), (_expp), (_d))

/* allocators track their ranges only where pointers are resolved through
 * them; nothing does yet */
#define FY_RANGE_TRACKING	0

static inline int fy_range_track_logical(const void *start, size_t size,
					 struct fy_allocator *owner, int tag,
					 uintptr_t logical)
{
	if (!FY_RANGE_TRACKING)
		return 0;
	return fy_range_register_logical(start, size, owner, tag, logical);
}

static inline int fy_range_track(const void *start, size_t size,
				 struct fy_allocator *owner, int tag)
{
	return fy_range_track_logical(start, size, owner, tag, (uintptr_t)start);
}

static inline int fy_range_track_resize(const void *start, struct fy_allocator *owner, size_t size)
{
	if (!FY_RANGE_TRACKING)
		return 0;
	return fy_range_resize(start, owner, size);
}

static inline void fy_range_untrack(const void *start, struct fy_allocator *owner)
{
	if (FY_RANGE_TRACKING)
		(void)fy_range_unregister(start, owner);
}

/* number of registered ranges (for diagnostics and tests) */
size_t fy_range_count(void);

#endif
