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

FY_TYPE_FWD_DECL_LIST(mremap_arena);
struct fy_mremap_arena {
	struct list_head node;
	size_t size;	/* includes the arena header */
	size_t next;
	uint64_t mem[] __attribute__((aligned(16)));
};
FY_TYPE_DECL_LIST(mremap_arena);

#define FY_MREMAP_ARENA_OVERHEAD (offsetof(struct fy_mremap_arena, mem))

struct fy_mremap_tag {
	struct fy_mremap_arena_list arenas;
	struct fy_mremap_arena_list full_arenas;
	size_t next_arena_sz;
	struct fy_allocator_stats stats;
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
	fy_id_bits ids[FY_ID_BITS_ARRAY_COUNT_BITS(FY_MREMAP_TAG_MAX)];
	struct fy_mremap_tag tags[FY_MREMAP_TAG_MAX];
};

extern const struct fy_allocator_ops fy_mremap_allocator_ops;

#endif
