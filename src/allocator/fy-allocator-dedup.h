/*
 * fy-allocator-dedup.h - the dedup allocator
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_ALLOCATOR_DEDUP_H
#define FY_ALLOCATOR_DEDUP_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fy-typelist.h"
#include "fy-id.h"
#include "xxhash.h"

#include "fy-allocator.h"

#define FY_DEDUP_TAG_MAX	128

FY_TYPE_FWD_DECL_LIST(dedup_entry);
struct fy_dedup_entry {
	struct list_head node;
	uint64_t hash;
	int64_t refs;
	size_t size;
	void *mem;
};
FY_TYPE_DECL_LIST(dedup_entry);

struct fy_dedup_tag_data {
	unsigned int bloom_filter_bits;
	unsigned int bloom_filter_mask;
	size_t bloom_id_count;
	fy_id_bits *bloom_id;
	fy_id_bits *bloom_update_id;
	unsigned int bucket_count_bits;
	unsigned int bucket_count_mask;
	size_t bucket_count;
	struct fy_dedup_entry_list *buckets;
	struct fy_dedup_entry_list *buckets_end;
	size_t bucket_id_count;
	fy_id_bits *buckets_in_use;
	fy_id_bits *buckets_collision;
	size_t dedup_threshold;
	unsigned int chain_length_grow_trigger;
};

struct fy_dedup_tag {
	bool bloom_filter_needs_update;
	int entries_tag;
	int content_tag;
	struct fy_allocator_stats stats;
	unsigned int data_active;	/* toggle to switch active and in progress */
	struct fy_dedup_tag_data data[2];
};

struct fy_dedup_allocator {
	struct fy_allocator a;
	struct fy_dedup_allocator_cfg cfg;
	struct fy_allocator *entries_allocator;
	struct fy_allocator *parent_allocator;
	unsigned long long xxseed;
	XXH64_state_t xxstate_template;
	unsigned int bloom_filter_bits;
	unsigned int bucket_count_bits;
	size_t dedup_threshold;
	unsigned int chain_length_grow_trigger;
	fy_id_bits ids[FY_ID_BITS_ARRAY_COUNT_BITS(FY_DEDUP_TAG_MAX)];
	struct fy_dedup_tag tags[FY_DEDUP_TAG_MAX];
};

extern const struct fy_allocator_ops fy_dedup_allocator_ops;

#endif
