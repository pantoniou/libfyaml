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

struct fy_allocator;

/*
 * The range registry maps an address back to the allocator (and tag)
 * that owns the memory it points into.
 *
 * Allocators register the large ranges they obtain (linear buffers, mremap
 * arenas, durable regions) and unregister them before releasing them.
 * A range is identified by its start address and owner.
 *
 * Ranges nest: any two registered ranges are either disjoint or one
 * contains the other (e.g. a linear allocator set up on a buffer allocated
 * from an mremap arena). Partial overlaps are rejected. A lookup returns
 * the innermost range containing the address.
 *
 * No locks are taken. Lookups are wait-free apart from a few atomic
 * counter updates. Registration, resizing and unregistration are lock-free
 * (copy and compare-and-swap) but each one then waits for the lookups in
 * flight to drain before freeing the replaced table, so they are
 * comparatively expensive; they are meant for arena-sized ranges, not
 * individual allocations.
 *
 * Memory whose lifetime is not under the allocator's control (in-place
 * allocators on caller supplied, usually stack, buffers) must not be
 * registered, since nothing would unregister it.
 */

struct fy_range_info {
	const void *start;
	size_t size;
	struct fy_allocator *owner;
	int tag;
};

/* register [start, start + size); returns 0 on success, -1 on error
 * (partial overlap with, or duplicate of, a registered range; or no memory) */
int fy_range_register(const void *start, size_t size,
		      struct fy_allocator *owner, int tag);

/* change the size of a registered range (in place arena grow/trim);
 * returns 0 on success, -1 if there is no such range, or if the new size
 * would partially overlap another range */
int fy_range_resize(const void *start, struct fy_allocator *owner, size_t size);

/* unregister a range; returns 0 on success, -1 if there is no such range */
int fy_range_unregister(const void *start, struct fy_allocator *owner);

/* find the innermost range containing ptr; fills info (if not NULL) and
 * returns true, or returns false if ptr is not in any registered range */
bool fy_range_lookup(const void *ptr, struct fy_range_info *info);

/* like fy_range_lookup(), but find the innermost range containing ptr
 * that is owned by owner */
bool fy_range_lookup_owner(const void *ptr, struct fy_allocator *owner,
			   struct fy_range_info *info);

/* number of registered ranges (for diagnostics and tests) */
size_t fy_range_count(void);

#endif
