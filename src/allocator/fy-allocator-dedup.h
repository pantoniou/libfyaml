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

#include "fy-atomics.h"
#include "fy-allocator.h"

#define FY_DEDUP_TAG_MAX	128

struct fy_dedup_entry {
	FY_ATOMIC(struct fy_dedup_entry *) next;
	uint64_t hash;
	size_t size;
	void *mem;
};

struct fy_dedup_tag;

/*
 * Pure scalar configuration for a tag-data version. Deliberately holds no
 * process-local pointers (da/dt) so a tag-data living in shared/durable memory
 * is valid across processes; da/dt are passed explicitly to the functions that
 * need them.
 */
struct fy_dedup_tag_data_cfg {
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
	FY_ATOMIC(uint32_t) in_flight_state;
};

#define FY_DEDUP_FROZEN_BIT  ((uint32_t)1 << 31)
#define FY_DEDUP_INFLIGHT_MASK  (FY_DEDUP_FROZEN_BIT - 1)

struct fy_dedup_tag {
	FY_ATOMIC(struct fy_dedup_tag_data *)tag_datas;
	int content_tag;
	int entries_tag;
	fy_atomic_flag growing;
	FY_ATOMIC(uint64_t) unique_stores;	/* unique stores */
	FY_ATOMIC(uint64_t) dup_stores;		/* duplicate lookups */
	FY_ATOMIC(uint64_t) collisions;		/* hash colissions */
	FY_ATOMIC(uint64_t) failures;		/* failed to allocate */
	FY_ATOMIC(uint64_t) retries;		/* link head retried times */
	FY_ATOMIC(uint64_t) dup_retries;	/* duplicate, but found after reservation */
	FY_ATOMIC(uint64_t) lost_entries;	/* lost an entry due to a race */
	FY_ATOMIC(uint64_t) waits_on_frozen;	/* had to wait for a frozen layer */
	FY_ATOMIC(uint64_t) adjustments;	/* times we had to grow index */
};

struct fy_dedup_allocator {
	struct fy_allocator a;
	struct fy_dedup_allocator_cfg cfg;
	struct fy_allocator *parent_allocator;
	enum fy_allocator_cap_flags parent_caps;
	bool external;		/* index/tag owned by an external (arena) root; control is per-process */
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
