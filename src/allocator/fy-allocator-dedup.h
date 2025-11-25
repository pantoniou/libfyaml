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

#include "fy-atomics.h"
#include "fy-allocator.h"

#define FY_DEDUP_TAG_MAX	128

struct fy_dedup_entry {
	struct fy_dedup_entry *next;
	uint64_t hash;
	size_t size;
	void *mem;
};

struct fy_dedup_tag;

struct fy_dedup_tag_data_cfg {
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	unsigned int bloom_filter_bits;
	unsigned int bucket_count_bits;
	size_t dedup_threshold;
	unsigned int chain_length_grow_trigger;
};

struct fy_dedup_tag_data {
	struct fy_dedup_tag_data *next;
	struct fy_dedup_tag_data_cfg cfg;
	unsigned int bloom_filter_bits;
	unsigned int bloom_filter_mask;
	size_t bloom_id_count;
	fy_id_bits *bloom_id;
	fy_id_bits *bloom_update_id;
	unsigned int bucket_count_bits;
	unsigned int bucket_count_mask;
	size_t bucket_count;
	FY_ATOMIC(struct fy_dedup_entry *) *buckets;
	size_t bucket_id_count;
	fy_id_bits *buckets_in_use;
	size_t dedup_threshold;
	unsigned int chain_length_grow_trigger;
};

struct fy_dedup_tag {
	FY_ATOMIC(struct fy_dedup_tag_data *)tag_datas;
	int content_tag;
	fy_atomic_flag growing;
	FY_ATOMIC(uint64_t) unique_stores;
	FY_ATOMIC(uint64_t) dup_stores;
	FY_ATOMIC(uint64_t) collisions;
};

struct fy_dedup_allocator {
	struct fy_allocator a;
	struct fy_dedup_allocator_cfg cfg;
	struct fy_allocator *parent_allocator;
	enum fy_allocator_cap_flags parent_caps;
	int entries_tag;
	unsigned int bloom_filter_bits;
	unsigned int bucket_count_bits;
	size_t dedup_threshold;
	unsigned int chain_length_grow_trigger;
	fy_id_bits *ids;
	unsigned int tag_id_count;
	struct fy_dedup_tag *tags;
	unsigned int tag_count;
};

extern const struct fy_allocator_ops fy_dedup_allocator_ops;

#define FY_DEDUP_XXHASH64_SEED	FY_XXHASH64_SEED

#endif
