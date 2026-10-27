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
 *
 * A range may also have a logical base that differs from its runtime
 * address. A durable region keeps the same logical addresses in every
 * process, and they are what persisted data stores; natively the region
 * is mapped at that address, but under FIL-C it lives wherever the
 * runtime put it. Ranges registered without a logical base are their own
 * logical base. Translated (logical != runtime) ranges must not overlap
 * one another in logical space, nor overlap any untranslated range.
 */

struct fy_range_info {
	const void *start;
	size_t size;
	struct fy_allocator *owner;
	int tag;
	uintptr_t logical;	/* logical address of start */
};

/* register [start, start + size); returns 0 on success, -1 on error
 * (partial overlap with, or duplicate of, a registered range; or no memory) */
int fy_range_register(const void *start, size_t size,
		      struct fy_allocator *owner, int tag);

/* register [start, start + size) with logical base logical */
int fy_range_register_logical(const void *start, size_t size,
			      struct fy_allocator *owner, int tag,
			      uintptr_t logical);

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

/* translate a runtime pointer to its logical address, applying the
 * innermost translated range enclosing it (ranges nested in a durable
 * region translate with it). Returns 1 with *logicalp set if ptr is in a
 * registered range, 0 if it is in none (*logicalp is then ptr's value),
 * or -1 if the resulting address would be read back as belonging to a
 * translated range it is not part of (a logical/runtime collision) */
int fy_range_ptr_to_logical(const void *ptr, uintptr_t *logicalp);

/* translate a logical address back to a pointer into the registered
 * range holding it, derived from the range's own start pointer (so under
 * FIL-C it carries the range's capability); NULL if no range holds it */
void *fy_range_logical_to_ptr(uintptr_t addr);

/* number of registered ranges (for diagnostics and tests) */
size_t fy_range_count(void);

#endif
