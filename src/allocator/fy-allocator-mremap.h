/*
 * fy-allocator-mremap.h - the mremap allocator
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_ALLOCATOR_MREMAP_H
#define FY_ALLOCATOR_MREMAP_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fy-typelist.h"
#include "fy-id.h"

#include "fy-atomics.h"
#include "fy-allocator.h"

struct fy_mremap_tag;

#define FY_MREMAP_TAG_MAX	32

static inline bool fy_mremap_arena_type_is_growable(enum fy_mremap_arena_type type)
{
	return type == FYMRAT_MMAP;
}

static inline bool fy_mremap_arena_type_is_trimmable(enum fy_mremap_arena_type type)
{
	return type == FYMRAT_MMAP;
}

#define FYMRAF_FULL		FY_BIT(0)
#define FYMRAF_GROWING		FY_BIT(1)
#define FYMRAF_CANT_GROW	FY_BIT(2)

struct fy_mremap_arena {
	struct fy_mremap_arena *next_arena;
	size_t size;	/* includes the arena header */
	FY_ATOMIC(uint64_t) flags;
	FY_ATOMIC(size_t) next;
	uint64_t mem[] __attribute__((aligned(16)));
};

#define FY_MREMAP_ARENA_OVERHEAD (offsetof(struct fy_mremap_arena, mem))

struct fy_mremap_tag {
	FY_ATOMIC(struct fy_mremap_arena *) arenas;
	FY_ATOMIC(size_t) next_arena_sz;
	FY_ATOMIC(uint64_t) allocations;
	FY_ATOMIC(uint64_t) allocated;
	FY_ATOMIC(uint64_t) stores;
	FY_ATOMIC(uint64_t) stored;
};

struct fy_mremap_allocator {
	struct fy_allocator a;
	struct fy_mremap_allocator_cfg cfg;
	size_t pagesz;
	size_t pageshift;
	size_t big_alloc_threshold;	/* bigger than that and a new allocation */
	size_t empty_threshold;		/* less than that and get moved to full */
	size_t minimum_arena_size;	/* the minimum arena size */
	float grow_ratio;
	float balloon_ratio;
	enum fy_mremap_arena_type arena_type;
	fy_id_bits *ids;
	unsigned int tag_id_count;
	struct fy_mremap_tag *tags;
	unsigned int tag_count;
};

extern const struct fy_allocator_ops fy_mremap_allocator_ops;

#endif
