/*
 * fy-range-registry.c - process wide address range registry
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#ifndef _WIN32
#include <sched.h>
#endif

#include "fy-win32.h"
#include "fy-utils.h"
#include "fy-align.h"
#include "fy-atomics.h"

#include "fy-range-registry.h"

/*
 * The registry is an immutable array of ranges published through an atomic
 * pointer. Entries are sorted by start address, and for equal starts by
 * size descending, so an enclosing range always precedes the ranges nested
 * in it. Each entry records the index of its innermost enclosing range.
 *
 * A lookup binary searches for the last entry starting at or below the
 * address. If that entry does not contain the address, the innermost
 * range that does (if any) is one of its enclosing ranges, so the lookup
 * follows the parent links outwards.
 *
 * No locks are taken. A writer builds a new array from the current one and
 * publishes it with a compare-and-swap of the table pointer, retrying from
 * the then current array if another writer won. Validation (nesting,
 * duplicates) is done against the exact array being replaced, so a
 * successful swap means it still holds.
 *
 * Arrays are reclaimed with a scheme along the lines of SRCU. Every access
 * to an array (lookups, and writers building a new array) runs inside a
 * read section, announced on one of two reader counters selected by the
 * parity of a phase counter. The writer that replaced an array flips the
 * phase, so new read sections land on the other counter, then waits until
 * it has seen each of the two counters at zero. A read section that could
 * still see the old array incremented one of the counters before the swap,
 * so once both have been seen at zero no one can reach the old array and
 * it is freed.
 *
 * Writers hold their read section across the compare-and-swap, so the array
 * they built from cannot be freed and its address reused under them; that
 * rules out ABA on the table pointer.
 *
 * To keep concurrent lookups from fighting over a single cache line the
 * counters are striped; a read section picks a stripe from its stack address.
 */

struct fy_range_entry {
	uintptr_t start;
	size_t size;
	struct fy_allocator *owner;
	int tag;
	long parent;			/* innermost enclosing entry, or -1 */
};

struct fy_range_table {
	size_t count;
	struct fy_range_entry e[];
};

#define FY_RANGE_STRIPES	16	/* power of two */

struct fy_range_stripe {
	FY_CACHELINE_ALIGN FY_ATOMIC(unsigned long) readers[2];
};

struct fy_range_read {
	struct fy_range_stripe *st;
	unsigned int phase;
};

static FY_ATOMIC(struct fy_range_table *) range_table;
static FY_ATOMIC(unsigned int) range_phase;
static struct fy_range_stripe range_stripes[FY_RANGE_STRIPES];

static inline void fy_range_yield(void)
{
#ifndef _WIN32
	sched_yield();
#else
	SwitchToThread();
#endif
}

static inline void fy_range_read_begin(struct fy_range_read *rd)
{
	char marker;
	uintptr_t x;

	/* threads have distinct stacks; mix the bits above the page offset */
	x = (uintptr_t)&marker >> 12;
	x ^= x >> 7;
	x ^= x >> 17;
	rd->st = &range_stripes[x & (FY_RANGE_STRIPES - 1)];
	rd->phase = fy_atomic_load(&range_phase) & 1;
	fy_atomic_fetch_add(&rd->st->readers[rd->phase], 1);
}

static inline void fy_range_read_end(struct fy_range_read *rd)
{
	fy_atomic_fetch_sub(&rd->st->readers[rd->phase], 1);
}

/* wait until no read section can still see an array replaced before the call */
static void fy_range_synchronize(void)
{
	unsigned int phase, n, i, spins;

	/* steer new read sections away from the phase waited on first */
	phase = fy_atomic_fetch_add(&range_phase, 1) & 1;
	for (n = 0; n < 2; n++, phase ^= 1) {
		for (i = 0; i < FY_RANGE_STRIPES; i++) {
			spins = 0;
			while (fy_atomic_load(&range_stripes[i].readers[phase])) {
				if (++spins < 64)
					fy_cpu_relax();
				else
					fy_range_yield();
			}
		}
		if (!n)
			fy_atomic_fetch_add(&range_phase, 1);
	}
}

/* sort order: start ascending, then size descending (outer before inner) */
static inline bool fy_range_before(uintptr_t s1, size_t z1, uintptr_t s2, size_t z2)
{
	return s1 < s2 || (s1 == s2 && z1 > z2);
}

/* index of the last entry with start <= addr, or -1 */
static long fy_range_table_find(const struct fy_range_table *t, uintptr_t addr)
{
	size_t lo, hi, mid;

	if (!t)
		return -1;

	lo = 0;
	hi = t->count;
	while (lo < hi) {
		mid = lo + (hi - lo) / 2;
		if (t->e[mid].start <= addr)
			lo = mid + 1;
		else
			hi = mid;
	}
	return (long)lo - 1;
}

/* index of the entry with this start and owner, or -1 */
static long fy_range_table_find_exact(const struct fy_range_table *t, uintptr_t start,
				      struct fy_allocator *owner)
{
	long i;

	/* all entries with the same start are adjacent, ending at i */
	for (i = fy_range_table_find(t, start); i >= 0 && t->e[i].start == start; i--) {
		if (t->e[i].owner == owner)
			return i;
	}
	return -1;
}

static inline void fy_range_entry_set(struct fy_range_entry *e, uintptr_t start, size_t size,
				      struct fy_allocator *owner, int tag)
{
	e->start = start;
	e->size = size;
	e->owner = owner;
	e->tag = tag;
}

/*
 * Build a new array from t, leaving out entry skip (if >= 0) and inserting
 * a new entry if size != 0. Validates that the ranges nest and computes the
 * parent links. Returns 0 and the new array in *ntp (NULL if empty), or -1
 * with errno set.
 */
static int fy_range_table_rebuild(const struct fy_range_table *t, long skip,
				  uintptr_t start, size_t size,
				  struct fy_allocator *owner, int tag,
				  struct fy_range_table **ntp)
{
	struct fy_range_table *nt;
	struct fy_range_entry *e, *p;
	size_t count, i, j, top;
	long *stack;
	bool inserted;

	*ntp = NULL;

	count = (t ? t->count : 0) - (skip >= 0 ? 1 : 0) + (size ? 1 : 0);
	if (!count)
		return 0;

	nt = malloc(sizeof(*nt) + count * sizeof(nt->e[0]));
	stack = malloc(count * sizeof(*stack));
	if (!nt || !stack) {
		errno = ENOMEM;
		goto err_out;
	}

	/* merge the old entries and the new one, in order */
	inserted = !size;
	for (i = 0, j = 0; t && i < t->count; i++) {
		if ((long)i == skip)
			continue;
		if (!inserted && fy_range_before(start, size, t->e[i].start, t->e[i].size)) {
			fy_range_entry_set(&nt->e[j++], start, size, owner, tag);
			inserted = true;
		}
		nt->e[j++] = t->e[i];
	}
	if (!inserted)
		fy_range_entry_set(&nt->e[j++], start, size, owner, tag);
	assert(j == count);
	nt->count = count;

	/* the stack holds the chain of ranges enclosing the current one */
	for (i = 0, top = 0; i < count; i++) {
		e = &nt->e[i];

		/* close the ranges that end at or before this one starts */
		while (top > 0 && e->start - nt->e[stack[top - 1]].start >= nt->e[stack[top - 1]].size)
			top--;

		if (top > 0) {
			p = &nt->e[stack[top - 1]];
			/* must be fully inside, and not a duplicate */
			if (e->start - p->start + e->size > p->size ||
			    (p->start == e->start && p->size == e->size)) {
				errno = EEXIST;
				goto err_out;
			}
		}

		e->parent = top > 0 ? stack[top - 1] : -1;
		stack[top++] = (long)i;
	}

	free(stack);
	*ntp = nt;
	return 0;

err_out:
	free(stack);
	free(nt);
	return -1;
}

enum fy_range_op {
	FYRO_REGISTER,
	FYRO_RESIZE,
	FYRO_UNREGISTER,
};

static int fy_range_update(enum fy_range_op op, uintptr_t start, size_t size,
			   struct fy_allocator *owner, int tag)
{
	struct fy_range_read rd;
	struct fy_range_table *t, *nt;
	long pos;
	int rc;

	for (;;) {
		fy_range_read_begin(&rd);

		t = fy_atomic_load(&range_table);

		pos = -1;
		if (op != FYRO_REGISTER) {
			pos = fy_range_table_find_exact(t, start, owner);
			if (pos < 0) {
				fy_range_read_end(&rd);
				errno = ENOENT;
				return -1;
			}
			if (op == FYRO_RESIZE)
				tag = t->e[pos].tag;
		}

		rc = fy_range_table_rebuild(t, pos, start, size, owner, tag, &nt);
		if (rc) {
			fy_range_read_end(&rd);
			/* unregistering must not fail; the memory is going away */
			if (op == FYRO_UNREGISTER && errno == ENOMEM) {
				fy_range_yield();
				continue;
			}
			return -1;
		}

		if (fy_atomic_compare_exchange_strong(&range_table, &t, nt)) {
			fy_range_read_end(&rd);
			break;
		}

		/* another writer won; retry against its array */
		fy_range_read_end(&rd);
		free(nt);
	}

	if (t) {
		fy_range_synchronize();
		free(t);
	}
	return 0;
}

int fy_range_register(const void *start, size_t size,
		      struct fy_allocator *owner, int tag)
{
	uintptr_t s = (uintptr_t)start;

	if (!start || !size || s + size < s) {
		errno = EINVAL;
		return -1;
	}
	return fy_range_update(FYRO_REGISTER, s, size, owner, tag);
}

int fy_range_resize(const void *start, struct fy_allocator *owner, size_t size)
{
	uintptr_t s = (uintptr_t)start;

	if (!start || !size || s + size < s) {
		errno = EINVAL;
		return -1;
	}
	return fy_range_update(FYRO_RESIZE, s, size, owner, 0);
}

int fy_range_unregister(const void *start, struct fy_allocator *owner)
{
	return fy_range_update(FYRO_UNREGISTER, (uintptr_t)start, 0, owner, 0);
}

static bool fy_range_lookup_internal(const void *ptr, struct fy_allocator *owner,
				     bool match_owner, struct fy_range_info *info)
{
	struct fy_range_read rd;
	const struct fy_range_table *t;
	const struct fy_range_entry *e;
	uintptr_t addr = (uintptr_t)ptr;
	bool found;
	long pos;

	fy_range_read_begin(&rd);

	found = false;
	t = fy_atomic_load(&range_table);
	for (pos = fy_range_table_find(t, addr); pos >= 0; pos = e->parent) {
		e = &t->e[pos];
		if (addr - e->start >= e->size)
			continue;
		if (match_owner && e->owner != owner)
			continue;
		if (info) {
			info->start = (const void *)e->start;
			info->size = e->size;
			info->owner = e->owner;
			info->tag = e->tag;
		}
		found = true;
		break;
	}

	fy_range_read_end(&rd);

	return found;
}

bool fy_range_lookup(const void *ptr, struct fy_range_info *info)
{
	return fy_range_lookup_internal(ptr, NULL, false, info);
}

bool fy_range_lookup_owner(const void *ptr, struct fy_allocator *owner,
			   struct fy_range_info *info)
{
	return fy_range_lookup_internal(ptr, owner, true, info);
}

size_t fy_range_count(void)
{
	struct fy_range_read rd;
	const struct fy_range_table *t;
	size_t count;

	fy_range_read_begin(&rd);
	t = fy_atomic_load(&range_table);
	count = t ? t->count : 0;
	fy_range_read_end(&rd);

	return count;
}
