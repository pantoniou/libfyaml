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
#include <errno.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "fy-win32.h"
#include "fy-utils.h"
#include "fy-align.h"
#include "fy-atomics.h"

#include "fy-range-registry.h"

/*
 * An immutable array of ranges sorted by start, outer before inner, each
 * linked to its innermost enclosing range. Writers copy the array and
 * publish the copy with a CAS; a replaced array is freed once the read
 * sections that could see it have drained (striped two-phase counters,
 * SRCU style). Translated entries are also indexed in xlat.
 */

struct fy_range_entry {
	uintptr_t start;
	size_t size;
	struct fy_allocator *owner;
	int tag;
	long parent;			/* innermost enclosing entry, or -1 */
	const void *base;		/* start, as a pointer */
	uintptr_t logical;		/* logical address of start */
};

struct fy_range_table {
	size_t count;
	size_t nxlat;			/* number of translated entries */
	long *xlat;			/* their indices */
	struct fy_range_entry e[];
};

static inline bool fy_range_entry_translated(const struct fy_range_entry *e)
{
	return e->logical != e->start;
}

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
				/* a reader may be preempted in its section; give it the CPU */
				if (++spins < 64)
					fy_cpu_relax();
				else if (spins < 128)
					fy_sched_yield();
				else
					usleep(50);
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

static inline void fy_range_entry_set(struct fy_range_entry *e, const void *base, size_t size,
				      struct fy_allocator *owner, int tag, uintptr_t logical)
{
	e->start = (uintptr_t)base;
	e->size = size;
	e->owner = owner;
	e->tag = tag;
	e->base = base;
	e->logical = logical;
}

static inline bool fy_range_overlap(uintptr_t s1, size_t z1, uintptr_t s2, size_t z2)
{
	return s1 < s2 + z2 && s2 < s1 + z1;
}

/*
 * Build a new array from t, leaving out entry skip (if >= 0) and inserting
 * a new entry if size != 0. Validates that the ranges nest and that the
 * translated ranges don't collide, and computes the parent links.
 * Returns 0 and the new array in *ntp (NULL if empty), or -1 with errno set.
 */
static int fy_range_table_rebuild(const struct fy_range_table *t, long skip,
				  const void *base, size_t size,
				  struct fy_allocator *owner, int tag, uintptr_t logical,
				  struct fy_range_table **ntp)
{
	struct fy_range_table *nt;
	struct fy_range_entry *e, *p, *x;
	size_t count, i, j, top;
	uintptr_t start = (uintptr_t)base;
	long *stack;
	bool inserted;

	*ntp = NULL;

	count = (t ? t->count : 0) - (skip >= 0 ? 1 : 0) + (size ? 1 : 0);
	if (!count)
		return 0;

	nt = malloc(sizeof(*nt) + count * (sizeof(nt->e[0]) + sizeof(nt->xlat[0])));
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
			fy_range_entry_set(&nt->e[j++], base, size, owner, tag, logical);
			inserted = true;
		}
		nt->e[j++] = t->e[i];
	}
	if (!inserted)
		fy_range_entry_set(&nt->e[j++], base, size, owner, tag, logical);
	nt->count = count;
	nt->xlat = (long *)&nt->e[count];
	nt->nxlat = 0;

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

		if (fy_range_entry_translated(e))
			nt->xlat[nt->nxlat++] = (long)i;
	}

	/* a translated range's logical addresses must not be mistaken for
	 * another translated range's, nor for an untranslated range's */
	for (i = 0; i < nt->nxlat; i++) {
		x = &nt->e[nt->xlat[i]];
		for (j = 0; j < count; j++) {
			e = &nt->e[j];
			if (e == x)
				continue;
			if (fy_range_entry_translated(e) ?
			    fy_range_overlap(x->logical, x->size, e->logical, e->size) :
			    fy_range_overlap(x->logical, x->size, e->start, e->size)) {
				errno = EEXIST;
				goto err_out;
			}
		}
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

static int fy_range_update(enum fy_range_op op, const void *base, size_t size,
			   struct fy_allocator *owner, int tag, uintptr_t logical)
{
	uintptr_t start = (uintptr_t)base;
	struct fy_range_read rd;
	struct fy_range_table *t, *nt;
	long pos;
	bool swapped;

	for (;;) {
		fy_range_read_begin(&rd);

		t = fy_atomic_load(&range_table);

		pos = -1;
		if (op != FYRO_REGISTER) {
			pos = fy_range_table_find_exact(t, start, owner);
			if (pos < 0) {
				errno = ENOENT;
				goto err_out;
			}
			if (op == FYRO_RESIZE) {
				tag = t->e[pos].tag;
				base = t->e[pos].base;
				logical = t->e[pos].logical;
			}
		}

		if (fy_range_table_rebuild(t, pos, base, size, owner, tag, logical, &nt)) {
			/* unregistering must not fail; the memory is going away */
			if (op != FYRO_UNREGISTER || errno != ENOMEM)
				goto err_out;
			fy_range_read_end(&rd);
			fy_sched_yield();
			continue;
		}

		/* inside the read section t can't be freed and reused: no ABA */
		swapped = fy_atomic_compare_exchange_strong(&range_table, &t, nt);
		fy_range_read_end(&rd);
		if (swapped)
			break;

		/* another writer won; retry against its array */
		free(nt);
	}

	if (t) {
		fy_range_synchronize();
		free(t);
	}
	return 0;

err_out:
	fy_range_read_end(&rd);
	return -1;
}

int fy_range_register_logical(const void *start, size_t size,
			      struct fy_allocator *owner, int tag,
			      uintptr_t logical)
{
	uintptr_t s = (uintptr_t)start;

	if (!start || !size || s + size < s || logical + size < logical) {
		errno = EINVAL;
		return -1;
	}
	return fy_range_update(FYRO_REGISTER, start, size, owner, tag, logical);
}

int fy_range_register(const void *start, size_t size,
		      struct fy_allocator *owner, int tag)
{
	return fy_range_register_logical(start, size, owner, tag, (uintptr_t)start);
}

int fy_range_resize(const void *start, struct fy_allocator *owner, size_t size)
{
	uintptr_t s = (uintptr_t)start;

	if (!start || !size || s + size < s) {
		errno = EINVAL;
		return -1;
	}
	return fy_range_update(FYRO_RESIZE, start, size, owner, 0, 0);
}

int fy_range_unregister(const void *start, struct fy_allocator *owner)
{
	return fy_range_update(FYRO_UNREGISTER, start, 0, owner, 0, 0);
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
			info->logical = e->logical;
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

/* the translated range whose logical addresses hold addr, or NULL */
static const struct fy_range_entry *
fy_range_table_find_logical(const struct fy_range_table *t, uintptr_t addr)
{
	const struct fy_range_entry *x;
	size_t i;

	for (i = 0; t && i < t->nxlat; i++) {
		x = &t->e[t->xlat[i]];
		if (addr - x->logical < x->size)
			return x;
	}
	return NULL;
}

int fy_range_ptr_to_logical(const void *ptr, uintptr_t *logicalp)
{
	struct fy_range_read rd;
	const struct fy_range_table *t;
	const struct fy_range_entry *e;
	uintptr_t addr = (uintptr_t)ptr;
	long pos;
	int ret;

	fy_range_read_begin(&rd);

	t = fy_atomic_load(&range_table);

	/* the innermost range containing it */
	for (pos = fy_range_table_find(t, addr); pos >= 0; pos = t->e[pos].parent) {
		if (addr - t->e[pos].start < t->e[pos].size)
			break;
	}
	ret = pos >= 0 ? 1 : 0;

	/* its enclosing ranges contain it too; the innermost translated one decides */
	for (; pos >= 0; pos = t->e[pos].parent) {
		e = &t->e[pos];
		if (fy_range_entry_translated(e)) {
			*logicalp = e->logical + (addr - e->start);
			goto out;
		}
	}

	/* untranslated; it must not read back as part of a translated range */
	*logicalp = addr;
	if (fy_range_table_find_logical(t, addr))
		ret = -1;
out:
	fy_range_read_end(&rd);
	return ret;
}

void *fy_range_logical_to_ptr(uintptr_t addr)
{
	struct fy_range_read rd;
	const struct fy_range_table *t;
	const struct fy_range_entry *e;
	void *p = NULL;
	long pos;

	fy_range_read_begin(&rd);

	t = fy_atomic_load(&range_table);
	e = fy_range_table_find_logical(t, addr);
	if (e) {
		p = (char *)e->base + (addr - e->logical);
	} else {
		for (pos = fy_range_table_find(t, addr); pos >= 0; pos = t->e[pos].parent) {
			e = &t->e[pos];
			if (addr - e->start < e->size) {
				p = (char *)e->base + (addr - e->start);
				break;
			}
		}
	}

	fy_range_read_end(&rd);
	return p;
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
