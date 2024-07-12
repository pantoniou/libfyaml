/*
 * fy-allocator-malloc.h - the malloc allocator
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_ALLOCATOR_MALLOC_H
#define FY_ALLOCATOR_MALLOC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fy-typelist.h"
#include "fy-id.h"

#include "fy-allocator.h"

struct fy_malloc_tag;

FY_TYPE_FWD_DECL_LIST(malloc_entry);
struct fy_malloc_entry {
	struct list_head node;
	size_t size;
	size_t reqsize;
	uint64_t mem[] __attribute__((aligned(16)));
};
FY_TYPE_DECL_LIST(malloc_entry);

#define FY_MALLOC_TAG_MAX	32

struct fy_malloc_tag {
	struct fy_malloc_entry_list entries;
	struct fy_allocator_stats stats;
};

struct fy_malloc_allocator {
	struct fy_allocator a;
	struct fy_malloc_entry_list entries;
	fy_id_bits ids[FY_ID_BITS_ARRAY_COUNT_BITS(FY_MALLOC_TAG_MAX)];
	struct fy_malloc_tag tags[FY_MALLOC_TAG_MAX];
};

extern const struct fy_allocator_ops fy_malloc_allocator_ops;

#endif
