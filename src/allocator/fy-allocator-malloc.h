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

#include "fy-id.h"

#include "fy-atomics.h"
#include "fy-allocator.h"

struct fy_malloc_tag;

struct fy_malloc_entry {
	void *ptr;
	size_t size;
};

struct fy_malloc_ptr_bucket {
	struct fy_malloc_ptr_bucket *next;
	size_t count;
	struct fy_malloc_entry entries[];
};

struct fy_malloc_ptr_set {
	struct fy_malloc_ptr_bucket **heads;
	size_t nheads;
	size_t count;
};

#define FY_MALLOC_TAG_MAX	32

struct fy_malloc_tag {
	fy_atomic_flag lock;
	struct fy_malloc_ptr_set ptrs;
	struct fy_allocator_stats stats;
};

struct fy_malloc_allocator {
	struct fy_allocator a;
	fy_id_bits *ids;
	unsigned int tag_id_count;
	struct fy_malloc_tag *tags;
	unsigned int tag_count;
};

extern const struct fy_allocator_ops fy_malloc_allocator_ops;

#endif
