/*
 * fy-allocator-dedup.c - dedup allocator
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdalign.h>
#include <time.h>
#include <inttypes.h>
#include <math.h>
#include <limits.h>

#include <stdio.h>

/* for container_of */
#include "fy-align.h"
#include "fy-list.h"
#include "fy-utils.h"
#include "fy-win32.h"
#include "xxhash.h"

#include "fy-allocator-dedup.h"

// #define DEBUG_GROWS

#ifdef DEBUG_GROWS

#undef BEFORE
#define BEFORE() \
	do { \
		clock_gettime(CLOCK_MONOTONIC, &before); \
	} while(0)

#undef AFTER
#ifdef _MSC_VER
static __inline int64_t fy_dedup_after_calc(const struct timespec *before, const struct timespec *after)
{
	return (int64_t)(after->tv_sec - before->tv_sec) * (int64_t)1000000000UL + (int64_t)(after->tv_nsec - before->tv_nsec);
}
#define AFTER() \
	(clock_gettime(CLOCK_MONOTONIC, &after), fy_dedup_after_calc(&before, &after))
#else
#define AFTER() \
	({ \
		clock_gettime(CLOCK_MONOTONIC, &after); \
		(int64_t)(after.tv_sec - before.tv_sec) * (int64_t)1000000000UL + (int64_t)(after.tv_nsec - before.tv_nsec); \
	})
#endif

#endif

/*
 * bit_to_chain_length_map[b] - the bucket chain length that triggers a grow,
 * indexed by bucket_count_bits b (so m = 1<<b buckets).
 *
 * Why the trigger is NOT a constant
 * ---------------------------------
 * Growth fires when the ONE chain we just walked exceeds the trigger, i.e. on
 * the per-bucket MAXIMUM chain length, not the average. With a good hash a
 * bucket's length is Poisson(alpha), alpha = n/m the load factor. The average
 * chain is alpha, but the maximum over all m buckets grows with m even at
 * fixed alpha (balls-in-bins). So a constant trigger would fire on random tail
 * spikes in a large, still-mostly-empty table. The trigger must therefore
 * track the EXPECTED MAXIMUM chain length at the load factor we want to grow
 * at - hence it rises with b.
 *
 * Derivation
 * ----------
 * Pick a target load factor alpha* (the fill at which we want to grow) and a
 * tolerance lambda (expected number of buckets allowed to reach the trigger,
 * ~1). The trigger is the smallest t with
 *
 *     m * P(Poisson(alpha*) >= t) <= lambda          (m = 1<<b)
 *
 * P(Poisson(alpha*) >= t) is the Poisson survival = Q(t, alpha*), the
 * regularized upper incomplete gamma. Reading: grow when a chain is so long
 * that, at load alpha*, you'd expect fewer than lambda such buckets in the
 * whole table - so its existence is evidence of real overload, not luck.
 *
 * Closed forms (alpha* <~ 1, the regime we live in):
 *     t(m) ~= ln(m/lambda) / W( ln(m/lambda) / (e*alpha*) )   (Lambert W)
 *     t(m) ~= ln m / ln ln m                                  (alpha*=1, lambda=1)
 * Heavily loaded (alpha* >> ln m): t(m) ~= alpha* + sqrt(2*alpha* ln m).
 *
 * What this hand-tuned table actually encodes
 * -------------------------------------------
 * Inverting the criterion on the entries below gives an IMPLIED alpha* that is
 * not constant: ~0.18 for small/medium tables, rising toward ~1.0 for huge
 * ones (b=6->~0.18, b=14->~0.20, b=18->~0.37, b=20->~0.66, b=22->~0.99).
 *
 * That rise is deliberate and correct for THIS structure: a grow does not
 * rehash, it prepends a doubled layer (old entries stay; lookups walk layers
 * newest->oldest behind bloom gates). So the real trade-off is
 *     (#layers * bloom_probe_cost) + chain_walk_cost + memory_cost.
 * For small/medium tables layers and walks are cheap, so we keep chains short
 * (alpha*~0.2, t=2..3). For huge tables a grow allocates a large bucket+bloom
 * array and memory dominates, so we tolerate denser packing (alpha*->1) to
 * grow less often. A single fixed load-factor trigger would throw that away.
 *
 * To revisit: choose alpha*(b) and lambda, then regenerate this array from the
 * criterion above instead of hand-tuning - the runtime cost stays an O(1)
 * lookup, but the constants become a documented consequence of two knobs.
 *
 * Empirical note (measured: 1.5M unique ~30B keys, mremap parent, single
 * thread; build/hit/miss-insert ns + peak RSS swept over several tables):
 *   - Dedup-HIT latency is essentially INDEPENDENT of this trigger (~58ns
 *     across everything from very aggressive to flat alpha*=1). Hits resolve
 *     in the newest layer behind the bloom gate, so the chain policy can't
 *     speed them up - do not tune this expecting faster hits.
 *   - LESS aggressive than this table (longer chains, the formula tables for
 *     lambda=1 or flat alpha*=1) is strictly worse: slower build, slower miss
 *     scans, and NO memory win (metadata is negligible vs content here).
 *   - MORE aggressive (shorter chains) only speeds the MISS path (full chain
 *     walked before concluding absent), at a real +10..30% RSS cost from more
 *     and larger bloom/bucket arrays, with no hit benefit.
 * So this hand-tuned table sits at the knee. If revisiting, the only useful
 * lever is a single bias: grow MORE aggressively only when miss-heavy AND
 * memory-rich; growing less aggressively saves no memory, so don't.
 */
static const unsigned int bit_to_chain_length_map[] = {
	[0] = 1,	/* 1 */
	[1] = 1,	/* 2 */
	[2] = 1,	/* 4 */
	[3] = 1,	/* 8 */
	[4] = 1,	/* 16 */
	[5] = 1,	/* 32 */
	[6] = 2,	/* 64 */
	[7] = 2,	/* 128 */
	[8] = 2,	/* 256 */
	[9] = 2,	/* 512 */
	[10] = 3,	/* 1024 */
	[11] = 3,	/* 2048 */
	[12] = 3,	/* 2048 */
	[13] = 3,	/* 2048 */
	[14] = 4,	/* 4096 */
	[15] = 4,	/* 8192 */
	[16] = 5,	/* 16384 */
	[17] = 5,	/* 32768 */
	[18] = 6,	/* 65536 */
	[19] = 7,	/* 65536 */
	[20] = 8,	/* 131072 */
	[21] = 9,	/* 262144*/
	[22] = 10,	/* 524288 */
	[23] = INT_MAX	/* infinite from now on */
};

void fy_dedup_tag_data_destroy(struct fy_dedup_allocator *da, struct fy_dedup_tag_data *dtd, int entries_tag);

static inline struct fy_dedup_tag *
fy_dedup_tag_from_tag(struct fy_dedup_allocator *da, int tag)
{
	if (!da)
		return NULL;

	if ((unsigned int)tag >= da->tag_count)
		return NULL;

	if (!fy_id_is_used(da->ids, da->tag_id_count, tag))
		return NULL;

	return &da->tags[tag];
}

static void fy_dedup_tag_data_cleanup(struct fy_dedup_allocator *da, struct fy_dedup_tag_data *dtd,
				      int entries_tag)
{
	fy_allocator_free(da->parent_allocator, entries_tag, (void *)dtd->buckets);
	fy_allocator_free(da->parent_allocator, entries_tag, (void *)dtd->bloom_id);
}

static int fy_dedup_tag_data_setup(struct fy_dedup_allocator *da,
				   struct fy_dedup_tag_data *dtd,
				   const struct fy_dedup_tag_data_cfg *cfg,
				   int entries_tag)
{
	unsigned int bloom_filter_bits, bucket_count_bits, chain_length_grow_trigger;
	size_t dedup_threshold;
	size_t tmpsz;
	unsigned int i;

	assert(dtd);
	assert(cfg);

	bloom_filter_bits = cfg->bloom_filter_bits;
	bucket_count_bits = cfg->bucket_count_bits;
	dedup_threshold = cfg->dedup_threshold;
	chain_length_grow_trigger = cfg->chain_length_grow_trigger;

	memset(dtd, 0, sizeof(*dtd));
	/* @cfg is fully initialised by callers (padding included), so a struct
	 * copy persists no uninitialised bytes into an arena-resident dtd. */
	dtd->cfg = *cfg;
	atomic_store(&dtd->in_flight_state, 0);

	dtd->bloom_filter_bits = bloom_filter_bits;
	dtd->bucket_count_bits = bucket_count_bits;
	dtd->dedup_threshold = dedup_threshold;
	dtd->chain_length_grow_trigger = chain_length_grow_trigger;
	if (!dtd->chain_length_grow_trigger) {
		if (bucket_count_bits >= ARRAY_SIZE(bit_to_chain_length_map))
			dtd->chain_length_grow_trigger = bit_to_chain_length_map[ARRAY_SIZE(bit_to_chain_length_map) - 1];
		else
			dtd->chain_length_grow_trigger = bit_to_chain_length_map[bucket_count_bits];

		if (dtd->chain_length_grow_trigger == 0)
			dtd->chain_length_grow_trigger++;
	}

	dtd->bloom_filter_mask = ((unsigned int)1 << dtd->bloom_filter_bits) - 1;
	dtd->bucket_count_mask = ((unsigned int)1 << dtd->bucket_count_bits) - 1;

	dtd->bloom_id_count = (((size_t)1 << dtd->bloom_filter_bits) + FY_ID_BITS_BITS - 1) / FY_ID_BITS_BITS;
	assert(dtd->bloom_id_count > 0);
	tmpsz = dtd->bloom_id_count * sizeof(*dtd->bloom_id);
	dtd->bloom_id = fy_allocator_alloc_nocheck(da->parent_allocator, entries_tag, tmpsz, _Alignof(fy_id_bits));
	if (!dtd->bloom_id)
		goto err_out;
	fy_id_reset(dtd->bloom_id, dtd->bloom_id_count);

	dtd->bucket_count = (size_t)1 << dtd->bucket_count_bits;
	assert(dtd->bucket_count);

	tmpsz = sizeof(*dtd->buckets) * dtd->bucket_count;
	dtd->buckets = fy_allocator_alloc_nocheck(da->parent_allocator, entries_tag, tmpsz, _Alignof(struct fy_dedup_entry *));
	if (!dtd->buckets)
		goto err_out;
	for (i = 0; i < dtd->bucket_count; i++)
		atomic_store(&dtd->buckets[i], NULL);

	return 0;

err_out:
	fy_dedup_tag_data_cleanup(da, dtd, entries_tag);
	return -1;
}

static void fy_dedup_tag_cleanup(struct fy_dedup_allocator *da, struct fy_dedup_tag *dt)
{
	struct fy_dedup_tag_data *dtd;

	if (!da || !dt)
		return;

	while ((dtd = fy_atomic_load(&dt->tag_datas)) != NULL) {

		/* only do it if you are the winner */
		if (!fy_atomic_compare_exchange_strong(&dt->tag_datas, &dtd, dtd->next))
			continue;

#ifdef DEBUG_GROWS
		fprintf(stderr, "%s: dump of state at close\n", __func__);
		fprintf(stderr, "%s: bloom count=%u used=%lu\n",
				__func__, 1 << dtd->bloom_filter_bits,
				fy_id_count_used(dtd->bloom_id, dtd->bloom_id_count));
		fprintf(stderr, "%s: bucket count=%u\n",
				__func__, 1 << dtd->bucket_count_bits);
#endif

		/*
		 * A restored read-only base layer lives in a mapping owned by
		 * the caller (e.g. the parse cache entry); we only unlink it.
		 */
		if (dtd->read_only)
			continue;

		fy_dedup_tag_data_destroy(da, dtd, dt->entries_tag);
	}

	/* we just release the tags, the underlying allocator should free everything */
	if (dt->content_tag != FY_ALLOC_TAG_NONE)
		fy_allocator_release_tag(da->parent_allocator, dt->content_tag);

	if (dt->content_tag != dt->entries_tag && dt->entries_tag != FY_ALLOC_TAG_NONE)
		fy_allocator_release_tag(da->parent_allocator, dt->entries_tag);

}

struct fy_dedup_tag_data *
fy_dedup_tag_data_create(struct fy_dedup_allocator *da, const struct fy_dedup_tag_data_cfg *cfg,
			 int entries_tag)
{
	struct fy_dedup_tag_data *dtd;
	int ret;

	dtd = fy_allocator_alloc_nocheck(da->parent_allocator, entries_tag, sizeof(*dtd),
					 _Alignof(struct fy_dedup_tag_data));
	if (!dtd)
		return NULL;

	ret = fy_dedup_tag_data_setup(da, dtd, cfg, entries_tag);
	if (ret)
		goto err_out;

	return dtd;

err_out:
	fy_allocator_free(da->parent_allocator, entries_tag, dtd);
	return NULL;
}

void fy_dedup_tag_data_destroy(struct fy_dedup_allocator *da, struct fy_dedup_tag_data *dtd,
			       int entries_tag)
{
	if (!dtd)
		return;

	fy_dedup_tag_data_cleanup(da, dtd, entries_tag);
	fy_allocator_free(da->parent_allocator, entries_tag, dtd);
}

static int fy_dedup_tag_setup(struct fy_dedup_allocator *da, struct fy_dedup_tag *dt)
{
	struct fy_dedup_tag_data_cfg dcfg;
	struct fy_dedup_tag_data *dtd;

	assert(da);
	assert(dt);

	memset(dt, 0, sizeof(*dt));

	fy_atomic_flag_clear(&dt->growing);
	if (da->cfg.use_explicit_tags) {
		/* caller pins the parent tags (e.g. durable separate-index region) */
		dt->content_tag = da->cfg.content_tag;
		dt->entries_tag = da->cfg.entries_tag;
	} else if (da->parent_caps & FYACF_CAN_FREE_TAG) {
		dt->content_tag = fy_allocator_get_tag(da->parent_allocator);
		if (dt->content_tag == FY_ALLOC_TAG_ERROR)
			goto err_out;
		dt->entries_tag = fy_allocator_get_tag(da->parent_allocator);
		if (dt->entries_tag == FY_ALLOC_TAG_ERROR)
			goto err_out;
	} else {
		dt->content_tag = FY_ALLOC_TAG_DEFAULT;
		dt->entries_tag = FY_ALLOC_TAG_DEFAULT;
	}

	fy_atomic_store(&dt->tag_datas, NULL);

	/* fully zero (padding included) so nothing uninitialised reaches the dtd */
	memset(&dcfg, 0, sizeof(dcfg));
	dcfg.bloom_filter_bits = da->bloom_filter_bits;
	dcfg.bucket_count_bits = da->bucket_count_bits;
	dcfg.dedup_threshold = da->dedup_threshold;
	dcfg.chain_length_grow_trigger = da->chain_length_grow_trigger;

	dtd = fy_dedup_tag_data_create(da, &dcfg, dt->entries_tag);
	if (!dtd)
		goto err_out;

	/* start with this one */
	fy_atomic_store(&dt->tag_datas, dtd);

	return 0;

err_out:
	fy_dedup_tag_cleanup(da, dt);
	return -1;
}

static int
fy_dedup_tag_prepare_new(struct fy_dedup_allocator *da, struct fy_dedup_tag_data *dtd,
			 struct fy_dedup_tag_data *new_dtd,
			 int bloom_filter_adjust_bits, int bucket_adjust_bits,
			 int entries_tag)
{
	unsigned int bit_shift, new_bucket_count_bits, new_bloom_filter_bits;
	struct fy_dedup_tag_data_cfg dcfg;
	int rc;

	bit_shift = (unsigned int)fy_id_ffs(FY_ID_BITS_BITS);

	new_bucket_count_bits = (unsigned int)((int)dtd->bucket_count_bits + bucket_adjust_bits);
	if (new_bucket_count_bits > (sizeof(int) * 8 - 1))
		new_bucket_count_bits = (sizeof(int) * 8) - 1;
	else if (new_bucket_count_bits < bit_shift)
		new_bucket_count_bits = bit_shift;

	new_bloom_filter_bits = (unsigned int)((int)dtd->bloom_filter_bits + bloom_filter_adjust_bits);
	if (new_bloom_filter_bits > (sizeof(int) * 8 - 1))
		new_bloom_filter_bits = (sizeof(int) * 8) - 1;
	else if (new_bloom_filter_bits < new_bucket_count_bits)
		new_bloom_filter_bits = new_bucket_count_bits;

	/* setup the new data; fully zero cfg (padding included) for an arena dtd */
	memset(&dcfg, 0, sizeof(dcfg));
	dcfg.bloom_filter_bits = new_bloom_filter_bits;
	dcfg.bucket_count_bits = new_bucket_count_bits;
	dcfg.dedup_threshold = da->dedup_threshold;
	dcfg.chain_length_grow_trigger = da->chain_length_grow_trigger;

	rc = fy_dedup_tag_data_setup(da, new_dtd, &dcfg, entries_tag);
	return rc;
}


static int
fy_dedup_tag_adjust(struct fy_dedup_allocator *da, struct fy_dedup_tag *dt,
		    int bloom_filter_adjust_bits, int bucket_adjust_bits)
{
	struct fy_dedup_tag_data *dtd, *new_dtd = NULL, *expected_dtd;
	size_t bloom_count, bloom_used;
	float occupancy_ratio;
	uint32_t in_flight;
	bool swapped FY_DEBUG_UNUSED;	/* this is unused on non-debug builds */
	int rc;

	rc = -1;

	assert(da);
	assert(dt);

	dtd = fy_atomic_load(&dt->tag_datas);
	if (!dtd)
		return -1;

	if (fy_atomic_flag_test_and_set(&dt->growing)) {
#ifdef DEBUG_GROWS
		fprintf(stderr, "grow: abort due another grow in progress\n");
#endif
		return -1;
	}

	/*
	 * The caller only reaches here on the chain-length trigger. When an
	 * occupancy floor is configured, gate the actual resize on the
	 * bloom-filter fill ratio: a long chain on a sparse table is just a hash
	 * collision and growing would not help, so only grow once the bloom is at
	 * least minimum_bucket_occupancy full. A zero floor grows on the trigger
	 * alone (and skips the popcount).
	 */
	if (da->cfg.minimum_bucket_occupancy > 0.0f) {
		bloom_count = (size_t)1 << dtd->bloom_filter_bits;
		bloom_used = fy_id_count_used(dtd->bloom_id, dtd->bloom_id_count);
		occupancy_ratio = (float)((double)bloom_used / (double)bloom_count);
		if (occupancy_ratio < da->cfg.minimum_bucket_occupancy) {
#ifdef DEBUG_GROWS
			fprintf(stderr, "grow: abort, bloom %2.2f%% < %2.2f%% full\n",
					100.0 * occupancy_ratio, 100.0 * da->cfg.minimum_bucket_occupancy);
#endif
			rc = 0;
			goto out;
		}
	}

	new_dtd = fy_allocator_alloc_nocheck(da->parent_allocator, dt->entries_tag,
					     sizeof(*new_dtd), _Alignof(struct fy_dedup_tag_data));
	if (!new_dtd)
		goto err_out;

	/* prepare the new one */
	rc = fy_dedup_tag_prepare_new(da, dtd, new_dtd, bloom_filter_adjust_bits, bucket_adjust_bits,
				      dt->entries_tag);
	if (rc)
		goto err_out;

#ifdef DEBUG_GROWS
	{
		size_t bloom_count, new_bloom_count, bloom_used;
		size_t bucket_count, new_bucket_count;

		bloom_count = 1U << dtd->bloom_filter_bits;
		new_bloom_count = 1U << new_dtd->bloom_filter_bits;
		bloom_used = fy_id_count_used(dtd->bloom_id, dtd->bloom_id_count);
		bucket_count = 1U << dtd->bucket_count_bits;
		new_bucket_count = 1U << new_dtd->bucket_count_bits;

		fprintf(stderr, "grow: chain_length_grow_trigger=%u->%u bloom %zu->%zu used %zu (%2.2f%%) ",
				dtd->chain_length_grow_trigger, new_dtd->chain_length_grow_trigger,
				bloom_count, new_bloom_count,
				bloom_used, 100.0*(double)bloom_used/(double)bloom_count);
		fprintf(stderr, "bucket %zu->%zu\n", bucket_count, new_bucket_count);
	}
#endif

	/* Freeze the current head so no NEW publisher can acquire a slot,
	 * then drain any in-flight publishers. After both loops, no publisher
	 * can land an entry into @dtd: the existing ones have all returned,
	 * and new ones bounce off the FROZEN bit in fy_dedup_drain_acquire().
	 */
	in_flight = fy_atomic_load(&dtd->in_flight_state);
	while (!fy_atomic_compare_exchange_weak(&dtd->in_flight_state, &in_flight, in_flight | FY_DEDUP_FROZEN_BIT))
		fy_cpu_relax();
	while ((fy_atomic_load(&dtd->in_flight_state) & FY_DEDUP_INFLIGHT_MASK) != 0)
		fy_cpu_relax();

	new_dtd->next = dtd;
	/* The @growing mutex above serializes resize, so the tag_datas swap
	 * below is uncontested by other resizers - the CAS therefore cannot
	 * lose. An assert documents the invariant; if a future change allows
	 * concurrent resizers, the swap will need a loser-path that unfreezes
	 * @dtd before backing off. */
	expected_dtd = dtd;
	swapped = fy_atomic_compare_exchange_strong(&dt->tag_datas, &expected_dtd, new_dtd);
	assert(swapped);	/* @growing serializes resize; the swap is uncontested */

	rc = 0;
out:
	fy_atomic_flag_clear(&dt->growing);
	return rc;

err_out:
	fy_allocator_free(da->parent_allocator, dt->entries_tag, new_dtd);
	rc = -1;
	goto out;
}

static void fy_dedup_tag_trim(struct fy_dedup_allocator *da, struct fy_dedup_tag *dt)
{
	if (!da || !dt)
		return;

	/* just pass them trim down to the parent */
	if (da->parent_caps & FYACF_CAN_FREE_TAG) {
		fy_allocator_trim_tag(da->parent_allocator, dt->content_tag);
		fy_allocator_trim_tag(da->parent_allocator, dt->entries_tag);
	}
}

static void fy_dedup_tag_reset(struct fy_dedup_allocator *da, struct fy_dedup_tag *dt)
{
	struct fy_dedup_tag_data *dtd, *dtd_head;
	size_t i;

	if (!da || !dt)
		return;

	/* just pass them reset down to the parent */
	if (da->parent_caps & FYACF_CAN_FREE_TAG) {
		fy_allocator_reset_tag(da->parent_allocator, dt->content_tag);
		fy_allocator_reset_tag(da->parent_allocator, dt->entries_tag);
	}

	/* pop the head which we will keep */

	dtd_head = NULL;
	while ((dtd = fy_atomic_load(&dt->tag_datas)) != NULL) {
		if (!fy_atomic_compare_exchange_strong(&dt->tag_datas, &dtd, dtd->next))
			continue;

		dtd->next = NULL;

		/* keep the head as is */
		if (!dtd_head)
			dtd_head = dtd;
		else
			fy_dedup_tag_data_destroy(da, dtd, dt->entries_tag);
	}

	if (dtd_head) {
		dtd = dtd_head;

		dtd->next = NULL;
		fy_id_reset(dtd->bloom_id, dtd->bloom_id_count);
		for (i = 0; i < dtd->bucket_count; i++)
			atomic_store(&dtd->buckets[i], NULL);
		fy_atomic_store(&dt->tag_datas, dtd);
	}
}

static void fy_dedup_cleanup(struct fy_allocator *a);

#define BUCKET_ESTIMATE_DIV 1024
#define BLOOM_ESTIMATE_DIV 128

static void fy_dedup_cleanup(struct fy_allocator *a)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	unsigned int i;

	if (!a)
		return;

	da = container_of(a, struct fy_dedup_allocator, a);

	/*
	 * External mode: the tag/index lives in caller-owned (arena) memory shared
	 * across processes - never tear it down here. Only the per-process control
	 * (the id bitmap, heap-allocated) is released; da->tags is not ours to free.
	 */
	if (da->external) {
		fy_align_free((void *)da->ids);
		return;
	}

	for (i = 0; i < da->tag_count; i++) {
		dt = fy_dedup_tag_from_tag(da, i);
		if (!dt)
			continue;
		fy_dedup_tag_cleanup(da, dt);
	}

	fy_parent_allocator_free(&da->a, (void *)da->ids);
	fy_parent_allocator_free(&da->a, da->tags);
}

/*
 * Common setup shared by the owning (fy_dedup_setup) and external
 * (fy_dedup_create_external) paths: derive geometry, init the allocator fields,
 * and allocate the per-process id bitmap. Does NOT allocate the tag array or set
 * up tag 0 - the caller does that according to ownership. When @external, the id
 * bitmap is heap-allocated (per-process) rather than from @parent.
 */
static int fy_dedup_setup_core(struct fy_dedup_allocator *da, struct fy_allocator *parent,
			       int parent_tag, const struct fy_dedup_allocator_cfg *cfg,
			       bool external)
{
	unsigned int bloom_filter_bits, bucket_count_bits;
	unsigned int bit_shift, chain_length_grow_trigger;
	size_t dedup_threshold, tmpsz;
	bool has_estimate;
	int rc;

	if (!cfg->parent_allocator)
		return -1;

	has_estimate = cfg->estimated_content_size && cfg->estimated_content_size != SIZE_MAX;

	bit_shift = (unsigned int)fy_id_ffs(FY_ID_BITS_BITS);

	bucket_count_bits = cfg->bucket_count_bits;
	if (!bucket_count_bits && has_estimate) {
		bucket_count_bits = 1;
		while (((size_t)1 << bucket_count_bits) < (cfg->estimated_content_size / BUCKET_ESTIMATE_DIV))
			bucket_count_bits++;
	}
	if (bucket_count_bits < bit_shift)
		bucket_count_bits = bit_shift;
	if (bucket_count_bits > (sizeof(int) * 8 - 1))
		bucket_count_bits = (sizeof(int) * 8) - 1;

	bloom_filter_bits = cfg->bloom_filter_bits;
	if (!bloom_filter_bits && has_estimate) {
		bloom_filter_bits = 1;
		while (((size_t)1 << bloom_filter_bits) < (cfg->estimated_content_size / BLOOM_ESTIMATE_DIV))
			bloom_filter_bits++;
	}
	if (bloom_filter_bits < bucket_count_bits)
		bloom_filter_bits = bucket_count_bits + 3;
	if (bloom_filter_bits > (sizeof(int) * 8 - 1))
		bloom_filter_bits = (sizeof(int) * 8) - 1;

	dedup_threshold = cfg->dedup_threshold;
	chain_length_grow_trigger = cfg->chain_length_grow_trigger;

	memset(da, 0, sizeof(*da));
	da->a.name = "dedup";
	da->a.ops = &fy_dedup_allocator_ops;
	da->a.parent = parent;
	da->a.parent_tag = parent_tag;
	da->cfg = *cfg;
	da->external = external;

	da->parent_allocator = cfg->parent_allocator;
	da->parent_caps = fy_allocator_get_caps(da->parent_allocator);

	da->entries_tag = cfg->use_explicit_tags ? cfg->entries_tag : parent_tag;

	da->bloom_filter_bits = bloom_filter_bits;
	da->bucket_count_bits = bucket_count_bits;
	da->dedup_threshold = dedup_threshold;
	da->chain_length_grow_trigger = chain_length_grow_trigger;

	rc = fy_allocator_get_tag_count(da->parent_allocator);
	if (rc <= 0)
		return -1;
	da->tag_count = (unsigned int)rc;
	da->tag_id_count = (da->tag_count + FY_ID_BITS_BITS - 1) / FY_ID_BITS_BITS;

	tmpsz = da->tag_id_count * sizeof(*da->ids);
	da->ids = external ? fy_align_alloc(_Alignof(fy_id_bits), tmpsz)
			   : fy_parent_allocator_alloc(&da->a, tmpsz, _Alignof(fy_id_bits));
	if (!da->ids)
		return -1;
	fy_id_reset(da->ids, da->tag_id_count);

	return 0;
}

static int fy_dedup_setup(struct fy_allocator *a, struct fy_allocator *parent, int parent_tag, const void *cfg_data)
{
	struct fy_dedup_allocator *da = NULL;
	const struct fy_dedup_allocator_cfg *cfg;
	size_t tmpsz;
	struct fy_dedup_tag *dt;
	int rc;

	if (!a || !cfg_data)
		return -1;

	cfg = cfg_data;

	da = container_of(a, struct fy_dedup_allocator, a);
	if (fy_dedup_setup_core(da, parent, parent_tag, cfg, false) != 0)
		goto err_out;

	/* owning layer: allocate the tag array from the parent and set up tag 0 */
	tmpsz = da->tag_count * sizeof(*da->tags);
	da->tags = fy_parent_allocator_alloc(&da->a, tmpsz, _Alignof(struct fy_dedup_tag));
	if (!da->tags)
		goto err_out;

	fy_id_set_used(da->ids, da->tag_id_count, 0);
	dt = fy_dedup_tag_from_tag(da, 0);
	rc = fy_dedup_tag_setup(da, dt);
	if (rc)
		goto err_out;

	return 0;
err_out:
	fy_dedup_cleanup(a);
	return -1;
}

struct fy_allocator *fy_dedup_create(struct fy_allocator *parent, int parent_tag, const void *cfg)
{
	struct fy_dedup_allocator *da = NULL;
	int rc;

	da = fy_early_parent_allocator_alloc(parent, parent_tag, sizeof(*da), _Alignof(struct fy_dedup_allocator));
	if (!da)
		goto err_out;

	rc = fy_dedup_setup(&da->a, parent, parent_tag, cfg);
	if (rc)
		goto err_out;

	return &da->a;

err_out:
	fy_early_parent_allocator_free(parent, parent_tag, da);
	return NULL;
}

void fy_dedup_destroy(struct fy_allocator *a)
{
	struct fy_dedup_allocator *da;

	da = container_of(a, struct fy_dedup_allocator, a);
	fy_dedup_cleanup(a);

	/* external control objects are heap-allocated (per-process), not from parent */
	if (da->external)
		fy_align_free(da);
	else
		fy_parent_allocator_free(a, da);
}

struct fy_allocator *fy_dedup_create_external(struct fy_allocator *parent, int parent_tag,
					      const struct fy_dedup_allocator_cfg *cfg,
					      FY_ATOMIC(struct fy_dedup_tag *) *root_slot)
{
	struct fy_dedup_allocator *da = NULL;
	struct fy_dedup_tag *dt;
	struct fy_dedup_tag_data *head;

	if (!cfg || !cfg->parent_allocator || !root_slot)
		return NULL;

	/* control object lives on the process heap, never in the (arena) parent */
	da = fy_align_alloc(_Alignof(struct fy_dedup_allocator), sizeof(*da));
	if (!da)
		return NULL;

	if (fy_dedup_setup_core(da, parent, parent_tag, cfg, true) != 0)
		goto err_out;

	dt = fy_atomic_load(root_slot);
	if (dt) {
		/* attach to the existing shared index (single tag 0) */
		da->tags = dt;
		fy_id_set_used(da->ids, da->tag_id_count, 0);
		/* adopt the persisted threshold so all attachers agree */
		head = fy_atomic_load(&dt->tag_datas);
		if (head)
			da->dedup_threshold = da->cfg.dedup_threshold = head->dedup_threshold;
	} else {
		/* create the shared tag + initial index in the index region */
		da->tags = fy_allocator_alloc_nocheck(da->parent_allocator, da->entries_tag,
						      sizeof(*da->tags), _Alignof(struct fy_dedup_tag));
		if (!da->tags)
			goto err_out;
		fy_id_set_used(da->ids, da->tag_id_count, 0);
		dt = fy_dedup_tag_from_tag(da, 0);
		if (fy_dedup_tag_setup(da, dt) != 0)
			goto err_out;
		/* publish the shared root for other processes to attach to */
		fy_atomic_store(root_slot, dt);
	}

	return &da->a;

err_out:
	fy_dedup_destroy(&da->a);
	return NULL;
}

struct fy_allocator *fy_dedup_restore(struct fy_allocator *parent, int parent_tag,
				      const struct fy_dedup_restore_cfg *cfg)
{
	struct fy_allocator *a;
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	struct fy_dedup_tag_data *head;

	if (!cfg || !cfg->root)
		return NULL;

	/* fresh, fully-writable dedup layer for content stored after restore */
	a = fy_dedup_create(parent, parent_tag, &cfg->base);
	if (!a)
		return NULL;

	da = container_of(a, struct fy_dedup_allocator, a);
	dt = fy_dedup_tag_from_tag(da, 0);
	if (!dt)
		goto err_out;

	head = fy_atomic_load(&dt->tag_datas);
	if (!head)
		goto err_out;

	/* make the root read only */
	if (!cfg->root->read_only)
		cfg->root->read_only = true;

	/* and chain */
	if (cfg->root->next)
		cfg->root->next = NULL;
	head->next = cfg->root;

	return a;

err_out:
	fy_dedup_destroy(a);
	return NULL;
}

void fy_dedup_dump(struct fy_allocator *a)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	unsigned int i;

	da = container_of(a, struct fy_dedup_allocator, a);

	fprintf(stderr, "dedup: ");
	for (i = 0; i < da->tag_count; i++) {
		dt = fy_dedup_tag_from_tag(da, i);
		if (!dt)
			continue;
		fprintf(stderr, "%c", fy_id_is_free(da->ids, da->tag_id_count, i) ? '.' : 'x');
	}
	fprintf(stderr, "\n");

	for (i = 0; i < da->tag_count; i++) {
		dt = fy_dedup_tag_from_tag(da, i);
		if (!dt)
			continue;
		fprintf(stderr, "  %d: tags: content=%d\n", i, dt->content_tag);
		fprintf(stderr, "  %d: tags: entries=%d\n", i, dt->entries_tag);
	}

	fprintf(stderr, "dedup: dumping parent allocator\n");
	fy_allocator_dump(da->parent_allocator);
}

static void *fy_dedup_alloc(struct fy_allocator *a, int tag, size_t size, size_t align)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	void *p;

	da = container_of(a, struct fy_dedup_allocator, a);
	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		goto err_out;

	/* just pass to the parent allocator using the content tag */
	p = fy_allocator_alloc_nocheck(da->parent_allocator, dt->content_tag, size, align);
	if (!p)
		goto err_out;

	return p;

err_out:
	return NULL;
}

static void fy_dedup_free(struct fy_allocator *a, int tag, void *data)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;

	da = container_of(a, struct fy_dedup_allocator, a);
	if (!(da->parent_caps & FYACF_CAN_FREE_INDIVIDUAL))
		return;

	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		return;

	/* just pass to the parent allocator */
	fy_allocator_free(da->parent_allocator, dt->content_tag, data);
}

static int fy_dedup_update_stats(struct fy_allocator *a, int tag, struct fy_allocator_stats *stats)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	int r;

	da = container_of(a, struct fy_dedup_allocator, a);

	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		return -1;

	r = fy_allocator_update_stats(da->parent_allocator, dt->content_tag, stats);
	if (r)
		return -1;
	if (dt->content_tag != dt->entries_tag) {
		r = fy_allocator_update_stats(da->parent_allocator, dt->entries_tag, stats);
		if (r)
			return -1;
	}

	stats->unique_stores += fy_atomic_get_and_clear_counter(&dt->unique_stores);
	stats->dup_stores += fy_atomic_get_and_clear_counter(&dt->dup_stores);
	stats->collisions += fy_atomic_get_and_clear_counter(&dt->collisions);

	return 0;
}

static inline bool fy_dedup_entry_match_after_hash(const struct fy_dedup_entry *de,
					const struct iovec *iov, int iovcnt,
					size_t size, size_t align)
{
	return size == de->size &&				/* size match */
	       ((uintptr_t)de->mem & (align - 1)) == 0 &&	/* align match */
	       !fy_iovec_cmp(iov, iovcnt, de->mem);		/* content match */
}

static inline bool fy_dedup_entry_match(const struct fy_dedup_entry *de,
					const struct iovec *iov, int iovcnt,
					size_t size, size_t align, uint64_t hash)
{
	return de->hash == hash &&				/* hash match */
	       fy_dedup_entry_match_after_hash(de, iov, iovcnt, size, align);
}

static void fy_dedup_trace(struct fy_allocator *a,
		           const struct fy_dedup_entry *de,
			   const struct iovec *iov, int iovcnt,
			   const char *banner)
{
	int i;
	size_t total_size, j;
	uint64_t hash;

	total_size = fy_iovec_size(iov, iovcnt);
	assert(total_size != SIZE_MAX);

	hash = fy_iovec_xxhash64(iov, iovcnt);

	printf("%p: %s %p 0x%016"PRIx64" %zx:",
			a, banner, de ? de->mem : NULL, hash, total_size);
	for (i = 0; i < iovcnt; i++) {
		for (j = 0; j < iov[i].iov_len; j++) {
			printf("%02x", (int)(((uint8_t *)iov[i].iov_base)[j]) & 0xff);
		}
	}
	printf("\n");
}

int fy_dedup_index_relocate(const struct fy_arena_reloc *arenas, unsigned int num_arenas,
			    void *start, size_t size, struct fy_dedup_tag_data *dtd)
{
	FY_ATOMIC(struct fy_dedup_entry *) *buckets;
	struct fy_dedup_entry *de;
	void *p;
	size_t i;

	if (!dtd || !arenas || !num_arenas)
		return -1;

	/* a relocated snapshot is a single, quiescent, read-only version. */
	dtd->next = NULL;
	dtd->bloom_update_id = NULL;
	dtd->read_only = true;

	/* rebase the index arrays - all must be present */
	p = fy_arena_reloc_ptr(arenas, num_arenas, start, size, dtd->bloom_id);
	if (!p)
		return -1;
	dtd->bloom_id = p;

	p = fy_arena_reloc_ptr(arenas, num_arenas, start, size, dtd->buckets);
	if (!p)
		return -1;
	dtd->buckets = p;
	buckets = dtd->buckets;

	/* rebase every non-empty bucket chain head and walk each chain */
	for (i = 0; i < dtd->bucket_count; i++) {
		de = fy_atomic_load(&buckets[i]);
		if (!de)
			continue;

		p = fy_arena_reloc_ptr(arenas, num_arenas, start, size, de);
		if (!p)
			return -1;
		de = p;
		fy_atomic_store(&buckets[i], de);

		while (de) {
			/* rebase content pointer (the canonical-identity pointer) */
			p = fy_arena_reloc_ptr(arenas, num_arenas, start, size, de->mem);
			if (!p)
				return -1;
			de->mem = p;

			/* rebase the chain link (NULL terminates the chain) */
			if (de->next) {
				p = fy_arena_reloc_ptr(arenas, num_arenas, start, size, de->next);
				if (!p)
					return -1;
				de->next = p;
			}
			de = de->next;
		}
	}

	return 0;
}

/*
 * Store @iov content into the dedup index, returning a stable canonical
 * pointer: storing equal content always yields the SAME pointer, so value
 * equality is pointer equality - even across threads (and, over a durable
 * arena, across processes). That invariant is the whole point of the dedup
 * allocator, and it must hold without locks on the read path.
 *
 * Data model. A tag is a singly linked list of "tag-data" layers
 * (@dt->tag_datas, newest first). Each layer is an open-addressed bucket array
 * of entry chains plus a bloom filter. Entries are immutable and only ever
 * prepended to a bucket; they are never moved or unlinked. The index grows by
 * prepending a new, empty layer at the head (fy_dedup_tag_adjust()); the head
 * pointer therefore changes ONLY on a resize.
 *
 * Lookup. Hash the content, then walk every layer newest->oldest: a bloom miss
 * skips a layer cheaply; a bloom hit scans that bucket's chain for a byte-equal
 * entry. The first match is returned (a dedup hit; the common, lock-free path).
 *
 * Insert (content not found). Correctness hinges on funnelling all racers that
 * store the same new value through ONE bucket-head compare-and-swap - the only
 * point that can pick a single winner. To guarantee that:
 *
 *   - Target the newest WRITABLE layer (normally the list head), never a
 *     "least full" layer. All racers for a value thus pick the same bucket.
 *   - Snapshot the target bucket head (@de_head) at scan time. The publishing
 *     CAS expects @de_head, so any prepend since the scan - an equal racer's
 *     entry or an unrelated one - makes the CAS fail and we restart, this
 *     time finding and returning the existing entry.
 *   - Set the bloom / in-use bits BEFORE linking, so a concurrent scan that can
 *     reach the entry through the chain is guaranteed to also see the bloom bit
 *     (a re-scan can never false-miss an already-linked entry).
 *
 * Drain quiescence vs. resize. The hard race used to be a publisher whose
 * bucket-head CAS landed into a layer that was demoted by a concurrent
 * resize: the entry stayed linked in the now-stale layer, and scanners that
 * bloom-missed the new head fell through and returned a different @mem from
 * scanners that bloom-hit it. We close this race by making resize
 * cooperative rather than reactive:
 *
 *   - Each layer holds an atomic @in_flight_state (high bit FY_DEDUP_FROZEN_BIT,
 *     low bits in-flight publisher count). Every storev call begins with
 *     fy_dedup_drain_acquire(), which CAS-bumps the head layer's counter -
 *     refusing if FROZEN is set - and returns the captured head as @drain_head.
 *     Every return path calls fy_dedup_drain_release() exactly once via the
 *     @out label.
 *   - fy_dedup_tag_adjust() CAS-sets FROZEN on the current head, spins until
 *     the in-flight count drains to zero, and only then CAS-swaps tag_datas
 *     to prepend the new (empty) head. While we hold a slot, the head we
 *     captured cannot be demoted, so @head0 == fy_atomic_load(&dt->tag_datas)
 *     holds throughout this function and no post-CAS recheck is needed.
 *   - If storev itself fires the chain-length grow trigger at the end, it
 *     decrements its own slot before calling fy_dedup_tag_adjust() to avoid
 *     self-deadlocking against the drain spin.
 *   - Note that this only happens when the first _non_ locking scan of an
 *     existing entry fails, so the contention on the atomic cache lines is
 *     only when there is a very high probability of an insert. So lookups
 *     are _non_ locking for the vast majority of cases.
 *
 * A lost race may leave a redundant equal copy in the arena (an append-only
 * arena cannot reclaim it; GC does so later) - that is accepted. What is NOT
 * accepted, and what the above guarantees, is two callers ever returning
 * different pointers for the same value.
 *
 * Sub-threshold content (< dedup_threshold) is not indexed at all: it is just
 * copied into the parent allocator and returned, since deduplicating tiny
 * values costs more than it saves. This path returns before acquiring a
 * drain slot, since it doesn't touch the dedup index at all.
 */

/* dedup store action flags */
#define FYDSAF_NEEDS_ADJUST		FY_BIT(0)	/* insert over the chain length limit */
#define FYDSAF_DUPLICATE		FY_BIT(1)	/* duplicate found, not inserted */
#define FYDASF_DUPLICATE_RETRIED	FY_BIT(2)	/* duplicate found on retry */
#define FYDSAF_WAIT_ON_FROZEN		FY_BIT(3)	/* hit a frozen layer and had to reload */
#define FYDASF_HASH_COLISSION		FY_BIT(4)	/* insert caused a hash colision */
#define FYDASF_LOST_ENTRY		FY_BIT(5)	/* entry allocated but lost due to a race */
#define FYDASF_LINK_RETRIED		FY_BIT(6)	/* times that entry link had to be retried */

static inline const struct fy_dedup_entry *
fy_dedup_tag_lookupv_internal(struct fy_dedup_tag *dt,
			      const struct iovec *iov, int iovcnt,
			      const size_t total_size, const size_t align,
			      const uint64_t hash)
{
	struct fy_dedup_tag_data *dtd;
	struct fy_dedup_entry *de;
	int bloom_pos, bucket_pos;
	bool bloom_hit;

	for (dtd = fy_atomic_load(&dt->tag_datas); dtd; dtd = dtd->next) {
		bloom_pos = (int)(hash & (uint64_t)dtd->bloom_filter_mask);
		bloom_hit = fy_id_is_used(dtd->bloom_id, dtd->bloom_id_count, bloom_pos);
		if (!bloom_hit)
			continue;

		bucket_pos = (int)(hash & (uint64_t)dtd->bucket_count_mask);
		for (de = fy_atomic_load(&dtd->buckets[bucket_pos]); de; de = fy_atomic_load(&de->next)) {
			if (fy_dedup_entry_match(de, iov, iovcnt, total_size, align, hash))
				return de;
		}
	}

	return NULL;
}

static const void *fy_dedup_lookupv(struct fy_allocator *a, int tag,
				    const struct iovec *iov, int iovcnt,
				    size_t align, uint64_t hash)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	const struct fy_dedup_entry *de;
	size_t total_size;

	da = container_of(a, struct fy_dedup_allocator, a);
	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		return NULL;

	/* calculate data total size */
	total_size = fy_iovec_size(iov, iovcnt);
	if (total_size == SIZE_MAX)
		return NULL;

	/* if it's under the dedup threshold just allocate and copy */
	if (total_size < da->cfg.dedup_threshold)
		return NULL;

	/* calculate hash if not given */
	if (!hash)
		hash = fy_iovec_xxhash64(iov, iovcnt);

	de = fy_dedup_tag_lookupv_internal(dt, iov, iovcnt, total_size, align, hash);

	return de ? de->mem : NULL;
}

static inline const struct fy_dedup_entry *
fy_dedup_tag_storev_internal(struct fy_dedup_tag *dt,
			     const struct iovec *iov, const int iovcnt,
			     const size_t total_size, const size_t align, const uint64_t hash,
			     struct fy_allocator *parent_allocator,
			     unsigned int *action_flagsp)
{
	struct fy_dedup_tag_data *head0, *head_lucky;
	struct fy_dedup_entry *de = NULL, *de_head, *de_iter;
	const struct fy_dedup_entry *de_lookup;
	FY_ATOMIC(struct fy_dedup_entry *) *de_headp;
	int bloom_pos, bucket_pos;
	bool bloom_hit;
	unsigned int chain_length;
	void *mem;
	size_t de_offset, max_align;
	uint32_t in_flight;
	unsigned int aflags;

	/* try the lucky path, without any reservation. This is the best hit case */
	head_lucky = fy_atomic_load(&dt->tag_datas);
	assert(head_lucky);	/* this must never be NULL, means we haven't initialized properly */

	de_lookup = fy_dedup_tag_lookupv_internal(dt, iov, iovcnt, total_size, align, hash);
	if (de_lookup) {
		*action_flagsp |= FYDSAF_DUPLICATE;
		return de_lookup;
	}

	aflags = *action_flagsp;

	head0 = head_lucky;

	/* Acquire an in-flight slot on the current head.
	 * Resize can never demote the current head, it will set frozen and then
	 * will loop draining any holders.
	 */
	for (;;) {
		in_flight = fy_atomic_load(&head0->in_flight_state);
		if (in_flight & FY_DEDUP_FROZEN_BIT) {
			aflags |= FYDSAF_WAIT_ON_FROZEN;
			head0 = fy_atomic_load(&dt->tag_datas); /* reload */
		} else if (fy_atomic_compare_exchange_weak(&head0->in_flight_state, &in_flight, in_flight + 1))
			break;
		fy_cpu_relax();
	}

	/* head changed, perform a lookup in case a racer inserted our value. */
	if (head0 != head_lucky) {
		de_lookup = fy_dedup_tag_lookupv_internal(dt, iov, iovcnt, total_size, align, hash);
		if (de_lookup) {
			aflags |= FYDSAF_DUPLICATE | FYDASF_DUPLICATE_RETRIED;
			de = (struct fy_dedup_entry *)de_lookup;
			goto out;
		}
	}

	/* those are fixed for our run */
	bloom_pos = (int)(hash & (uint64_t)head0->bloom_filter_mask);
	bucket_pos = (int)(hash & (uint64_t)head0->bucket_count_mask);
	de_headp = &head0->buckets[bucket_pos];

	de_head = fy_atomic_load(de_headp);
	for (;;) {
		/* try again; someone might have raced and inserted our data (but only on this layer) */
		bloom_hit = fy_id_is_used(head0->bloom_id, head0->bloom_id_count, bloom_pos);
		if (bloom_hit) {
			for (de_iter = fy_atomic_load(de_headp); de_iter; de_iter = fy_atomic_load(&de_iter->next)) {
				if (fy_dedup_entry_match(de_iter, iov, iovcnt, total_size, align, hash)) {
					aflags |= FYDSAF_DUPLICATE;

					/* if there's a held entry means we lose track of it */
					if (de)
						aflags |= FYDASF_LOST_ENTRY;
					de = de_iter;
					goto out;
				}
			}
		}

		/* we might be retrying; don't allocate and copy again */
		if (!de) {
			/* place the dedup entry at the aligned offset after the data */

			if (dt->content_tag == dt->entries_tag) {
				/* we don't have seperate content and entries */
				de_offset = fy_size_t_align(total_size, _Alignof(struct fy_dedup_entry));
				max_align = align > _Alignof(struct fy_dedup_entry) ? align : _Alignof(struct fy_dedup_entry);
				mem = fy_allocator_alloc_nocheck(parent_allocator, dt->content_tag, de_offset + sizeof(*de), max_align);
				if (!mem)
					goto out;

				/* zero the content->entry alignment padding so nothing
				 * uninitialised is persisted into a durable arena */
				if (de_offset > total_size)
					memset((char *)mem + total_size, 0, de_offset - total_size);

				de = (void *)((char *)mem + de_offset);
			} else {
				/* separate content and entries */
				mem = fy_allocator_alloc_nocheck(parent_allocator, dt->content_tag,
								 total_size, align);
				if (!mem)
					goto out;

				de = fy_allocator_alloc_nocheck(parent_allocator, dt->entries_tag,
								sizeof(struct fy_dedup_entry), _Alignof(struct fy_dedup_entry));
				if (!de)
					goto out;
			}

			/* verify it's aligned correctly */
			assert(((uintptr_t)mem & (align - 1)) == 0);

			de->hash = hash;
			de->size = total_size;
			de->mem = mem;

			/* and copy the data */
			fy_iovec_copy_from(iov, iovcnt, de->mem);
		}

		/* Publish presence in the bloom filter BEFORE linking */
		fy_id_set_used(head0->bloom_id, head0->bloom_id_count, bloom_pos);

		/* always update the next entry with it */
		fy_atomic_store(&de->next, de_head);

		/* prepend; if the CAS fails @de_head is reloaded and we retry.
		 * deliberately strong, do not change to weak.
		 */
		if (fy_atomic_compare_exchange_strong(de_headp, &de_head, de))
			break;

		/* mark that this happened for statistics */
		aflags |= FYDASF_LINK_RETRIED;
	}

	/* walk the new chain; if larger mark it as needing adjust */
	chain_length = 1;
	for (de_iter = fy_atomic_load(&de->next); de_iter; de_iter = fy_atomic_load(&de_iter->next)) {
		if (de_iter->hash == hash)
			aflags |= FYDASF_HASH_COLISSION;
		chain_length++;
	}
	if (chain_length > head0->chain_length_grow_trigger)
		aflags |= FYDSAF_NEEDS_ADJUST;

out:
	/* release */
	fy_atomic_fetch_sub(&head0->in_flight_state, 1);

	*action_flagsp = aflags;

	return de;
}

static const void *fy_dedup_storev(struct fy_allocator *a, int tag,
				   const struct iovec *iov, int iovcnt,
				   size_t align, uint64_t hash)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	const struct fy_dedup_entry *de;
	size_t total_size;
	unsigned int action_flags;

	da = container_of(a, struct fy_dedup_allocator, a);
	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		return NULL;

	/* calculate data total size */
	total_size = fy_iovec_size(iov, iovcnt);
	if (total_size == SIZE_MAX)
		return NULL;

	/* if it's under the dedup threshold just allocate and copy */
	if (total_size < da->cfg.dedup_threshold) {

		/* just pass to the parent allocator using the content tag */
		void *p = fy_allocator_alloc_nocheck(da->parent_allocator, dt->content_tag, total_size, align);
		if (!p)
			return NULL;

		fy_iovec_copy_from(iov, iovcnt, p);
		return p;
	}

	/* calculate hash if not given */
	if (!hash)
		hash = fy_iovec_xxhash64(iov, iovcnt);

	action_flags = 0;
	de = fy_dedup_tag_storev_internal(dt, iov, iovcnt, total_size, align, hash,
					  da->parent_allocator, &action_flags);

	if (action_flags & FYDSAF_NEEDS_ADJUST)
		fy_dedup_tag_adjust(da, dt, 1, 1);

	/* update flags only if keeping statistics */
	if (a->flags & FYAF_KEEP_STATS) {
		if (!de)
			fy_atomic_fetch_add(&dt->failures, 1);
		else if (action_flags & FYDSAF_DUPLICATE)
			fy_atomic_fetch_add(&dt->dup_stores, 1);
		else
			fy_atomic_fetch_add(&dt->unique_stores, 1);

		if (action_flags & FYDASF_LINK_RETRIED)
			fy_atomic_fetch_add(&dt->retries, 1);
		if (action_flags & FYDASF_DUPLICATE_RETRIED)
			fy_atomic_fetch_add(&dt->dup_retries, 1);
		if (action_flags & FYDASF_LOST_ENTRY)
			fy_atomic_fetch_add(&dt->lost_entries, 1);
		if (action_flags & FYDASF_HASH_COLISSION)
			fy_atomic_fetch_add(&dt->collisions, 1);
		if (action_flags & FYDSAF_WAIT_ON_FROZEN)
			fy_atomic_fetch_add(&dt->waits_on_frozen, 1);
		if (action_flags & FYDSAF_NEEDS_ADJUST)
			fy_atomic_fetch_add(&dt->adjustments, 1);
	}

	if (a->flags & FYAF_TRACE) {
		fy_dedup_trace(a, de, iov, iovcnt,
			!de ? "fail" :
				((action_flags & FYDSAF_DUPLICATE) ? "dup" : "unique"));
	}

	return de ? de->mem : NULL;
}

static void fy_dedup_release(struct fy_allocator *a FY_UNUSED,
			     int tag FY_UNUSED,
			     const void *data FY_UNUSED,
			     size_t size FY_UNUSED)
{
	/* we do nothing */
}

static int fy_dedup_get_tag(struct fy_allocator *a)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt = NULL;
	int tag;
	int id, rc;

	da = container_of(a, struct fy_dedup_allocator, a);

	/* for a single tag, just return 0 */
	if (!(da->parent_caps & FYACF_CAN_FREE_TAG))
		return 0;

	/* and one from us */
	id = fy_id_alloc(da->ids, da->tag_id_count);
	if (id < 0)
		goto err_out;

	tag = (int)id;

	dt = fy_dedup_tag_from_tag(da, tag);
	assert(dt);

	rc = fy_dedup_tag_setup(da, dt);
	if (rc)
		goto err_out;

	return tag;

err_out:
	fy_dedup_tag_cleanup(da, dt);
	if (id >= 0)
		fy_id_free(da->ids, da->tag_id_count, id);
	return FY_ALLOC_TAG_ERROR;
}

static void fy_dedup_release_tag(struct fy_allocator *a, int tag)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;

	da = container_of(a, struct fy_dedup_allocator, a);

	/* for a single tag, just return */
	if (!(da->parent_caps & FYACF_CAN_FREE_TAG))
		return;

	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		return;

	fy_dedup_tag_cleanup(da, dt);

	fy_id_free(da->ids, da->tag_count, tag);
}

static int fy_dedup_get_tag_count(struct fy_allocator *a)
{
	struct fy_dedup_allocator *da;

	da = container_of(a, struct fy_dedup_allocator, a);
	return da->tag_count;
}

static int fy_dedup_set_tag_count(struct fy_allocator *a, unsigned int count)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag_data *dtd;
	fy_id_bits *ids = NULL, *alloc_ids = NULL;
	struct fy_dedup_tag *dt, *dt_new, *tags = NULL, *alloc_tags = NULL;
	unsigned int i, tag_count, tag_id_count;
	size_t tmpsz;
	int rc;

	if (count < 1)
		return -1;

	da = container_of(a, struct fy_dedup_allocator, a);

	if (count == da->tag_count)
		return 0;

	tag_count = count;
	tag_id_count = (tag_count + FY_ID_BITS_BITS - 1) / FY_ID_BITS_BITS;

	/* check if all content tag are within limits */
	for (i = 0; i < da->tag_count; i++) {
		dt = fy_dedup_tag_from_tag(da, i);
		if (!dt)
			continue;
		if (dt->content_tag >= (int)tag_count)
			return -1;
		if (dt->entries_tag >= (int)tag_count)
			return -1;
	}

	if (count > da->tag_count) {
		/* we need to grow */
		tmpsz = tag_id_count * sizeof(*da->ids);
		alloc_ids = fy_parent_allocator_alloc(&da->a, tmpsz, _Alignof(fy_id_bits));
		if (!alloc_ids)
			goto err_out;
		fy_id_reset(alloc_ids, tag_id_count);

		tmpsz = tag_count * sizeof(*tags);
		alloc_tags = fy_parent_allocator_alloc(&da->a, tmpsz, _Alignof(struct fy_dedup_tag));
		if (!alloc_tags)
			goto err_out;
	}

	/* shrink?, just clip */
	if (count < da->tag_count) {
		tags = da->tags;
		ids = da->ids;
		for (i = count; i < da->tag_count; i++) {
			dt = fy_dedup_tag_from_tag(da, i);
			if (!dt)
				continue;
			fy_dedup_tag_cleanup(da, dt);
			fy_id_free(da->ids, da->tag_id_count, i);
		}
	} else {
		tags = alloc_tags;
		ids = alloc_ids;
		/* copy over the old entries */
		for (i = 0; i < da->tag_count; i++) {

			dt = fy_dedup_tag_from_tag(da, i);
			if (!dt)
				continue;

			dt_new = &tags[i];

			fy_dedup_tag_setup(da, dt_new);

			/* move the old entries over */
			do {
				dtd = fy_atomic_load(&dt->tag_datas);
			} while (fy_atomic_compare_exchange_strong(&dt->tag_datas, &dtd, NULL));
			fy_atomic_store(&dt_new->tag_datas, dtd);
			dt_new->content_tag = dt->content_tag;
			dt_new->entries_tag = dt->entries_tag;
			fy_atomic_flag_clear(&dt_new->growing);
			fy_atomic_store(&dt_new->unique_stores, fy_atomic_load(&dt->unique_stores));
			fy_atomic_store(&dt_new->dup_stores, fy_atomic_load(&dt->dup_stores));
			fy_atomic_store(&dt_new->collisions, fy_atomic_load(&dt->collisions));

			/* clean the old entry */
			fy_dedup_tag_cleanup(da, dt);

			fy_id_set_used(ids, tag_id_count, i);
		}
	}

	/* ok, drop the parent bits first */
	rc = fy_allocator_set_tag_count(da->parent_allocator, tag_count);
	if (rc)
		goto err_out;

	/* switch */
	if (da->tags != tags) {
		da->tags = tags;
		fy_parent_allocator_free(&da->a, da->tags);
	}

	if (da->ids != ids) {
		da->ids = ids;
		fy_parent_allocator_free(&da->a, (void *)da->ids);
	}

	da->tag_count = tag_count;
	da->tag_id_count = tag_id_count;

	return 0;

err_out:
	fy_parent_allocator_free(&da->a, alloc_tags);
	fy_parent_allocator_free(&da->a, (void *)alloc_ids);
	return -1;
}

static void fy_dedup_trim_tag(struct fy_allocator *a, int tag)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;

	da = container_of(a, struct fy_dedup_allocator, a);
	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		return;

	fy_dedup_tag_trim(da, dt);
}

static void fy_dedup_reset_tag(struct fy_allocator *a, int tag)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;

	da = container_of(a, struct fy_dedup_allocator, a);

	/* if it can't free an individual tag it can't reset it */
	if (!(da->parent_caps & FYACF_CAN_FREE_TAG))
		return;

	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		return;

	fy_dedup_tag_reset(da, dt);
}

/* build a single-allocation info covering the given parent tags */
static struct fy_allocator_info *
fy_dedup_build_combined_info(struct fy_dedup_allocator *da, const int *tags, unsigned int ntags)
{
	struct fy_allocator_info *pinfo[2] = { NULL, NULL };
	struct fy_allocator_info *info = NULL;
	struct fy_allocator_tag_info *ti, *sti;
	struct fy_allocator_arena_info *ai;
	unsigned int i, j, num_tags = 0, num_arenas = 0;
	size_t size;

	if (ntags > ARRAY_SIZE(pinfo))
		return NULL;

	/* gather per-tag info from the parent allocator */
	for (i = 0; i < ntags; i++) {
		pinfo[i] = fy_allocator_get_info(da->parent_allocator, tags[i]);
		if (!pinfo[i])
			goto err_out;
		num_tags += pinfo[i]->num_tag_infos;
		for (j = 0; j < pinfo[i]->num_tag_infos; j++)
			num_arenas += pinfo[i]->tag_infos[j].num_arena_infos;
	}

	/* single contiguous allocation: info + tag_infos[] + arena_infos[] */
	size = sizeof(*info) + sizeof(*ti) * num_tags + sizeof(*ai) * num_arenas;
	info = malloc(size);
	if (!info)
		goto err_out;
	memset(info, 0, sizeof(*info));
	ti = (void *)(info + 1);
	ai = (void *)(ti + num_tags);
	info->tag_infos = ti;
	info->num_tag_infos = 0;

	for (i = 0; i < ntags; i++) {
		for (j = 0; j < pinfo[i]->num_tag_infos; j++) {
			sti = &pinfo[i]->tag_infos[j];
			*ti = *sti;
			ti->arena_infos = ai;
			memcpy(ai, sti->arena_infos, sizeof(*ai) * sti->num_arena_infos);
			ai += sti->num_arena_infos;
			ti++;
			info->num_tag_infos++;
			info->free += sti->free;
			info->used += sti->used;
			info->total += sti->total;
		}
	}

	for (i = 0; i < ntags; i++)
		free(pinfo[i]);
	return info;

err_out:
	for (i = 0; i < ntags; i++)
		free(pinfo[i]);
	free(info);
	return NULL;
}

static int fy_dedup_snapshot(struct fy_allocator *a, int tag, struct fy_allocator_snapshot *snap)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	struct fy_dedup_tag_data *head, *dtd, *merged;
	struct fy_dedup_entry *de, *nde;
	int index_tag = FY_ALLOC_TAG_ERROR, tags[2];
	unsigned int ntags;
	size_t b, bucket_pos, bloom_pos;

	if (!snap)
		return -1;
	memset(snap, 0, sizeof(*snap));

	da = container_of(a, struct fy_dedup_allocator, a);
	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		return -1;

	/* we need a dedicated, separately-freeable parent tag for the index */
	if (!(da->parent_caps & FYACF_CAN_FREE_TAG))
		return -1;

	/*
	 * Snapshot is single-threaded by contract: the caller must guarantee no
	 * concurrent writer or resize on this tag (the cache store happens
	 * single-threaded at the end of a parse). We therefore read the layer
	 * chain directly without fencing against a grow.
	 */
	head = fy_atomic_load(&dt->tag_datas);
	if (!head)
		goto err_out;

	index_tag = fy_allocator_get_tag(da->parent_allocator);
	if (index_tag == FY_ALLOC_TAG_ERROR)
		goto err_out;

	/* merged tag-data, sized to the head (largest) geometry */
	merged = fy_allocator_alloc_nocheck(da->parent_allocator, index_tag,
					    sizeof(*merged), _Alignof(struct fy_dedup_tag_data));
	if (!merged)
		goto err_out;
	memset(merged, 0, sizeof(*merged));
	merged->bloom_filter_bits = head->bloom_filter_bits;
	merged->bloom_filter_mask = head->bloom_filter_mask;
	merged->bloom_id_count = head->bloom_id_count;
	merged->bucket_count_bits = head->bucket_count_bits;
	merged->bucket_count_mask = head->bucket_count_mask;
	merged->bucket_count = head->bucket_count;
	merged->dedup_threshold = head->dedup_threshold;
	merged->chain_length_grow_trigger = head->chain_length_grow_trigger;

	merged->bloom_id = fy_allocator_alloc_nocheck(da->parent_allocator, index_tag,
			merged->bloom_id_count * sizeof(*merged->bloom_id), _Alignof(fy_id_bits));
	merged->buckets = fy_allocator_alloc_nocheck(da->parent_allocator, index_tag,
			merged->bucket_count * sizeof(*merged->buckets), _Alignof(struct fy_dedup_entry *));
	if (!merged->bloom_id || !merged->buckets)
		goto err_out;

	fy_id_reset(merged->bloom_id, merged->bloom_id_count);
	for (b = 0; b < merged->bucket_count; b++)
		fy_atomic_store(&merged->buckets[b], NULL);

	/*
	 * Re-bucket every entry from every chain version into the merged
	 * table. Entries are copied into the index tag (non-destructive to the
	 * source, and keeps the index region self-contained). de->mem still
	 * points into the content region.
	 */
	for (dtd = head; dtd; dtd = dtd->next) {
		for (b = 0; b < dtd->bucket_count; b++) {
			for (de = fy_atomic_load(&dtd->buckets[b]); de; de = de->next) {
				nde = fy_allocator_alloc_nocheck(da->parent_allocator, index_tag,
						sizeof(*nde), _Alignof(struct fy_dedup_entry));
				if (!nde)
					goto err_out;
				nde->hash = de->hash;
				nde->size = de->size;
				nde->mem = de->mem;

				bucket_pos = de->hash & merged->bucket_count_mask;
				bloom_pos = de->hash & merged->bloom_filter_mask;
				nde->next = fy_atomic_load(&merged->buckets[bucket_pos]);
				fy_atomic_store(&merged->buckets[bucket_pos], nde);
				fy_id_set_used(merged->bloom_id, merged->bloom_id_count, (int)bloom_pos);
			}
		}
	}
	merged->next = NULL;

	snap->tag = tag;
	snap->content_tag = dt->content_tag;
	snap->index_tag = index_tag;
	snap->root = merged;

	/* regions to serialize: content + the self-contained index */
	ntags = 0;
	tags[ntags++] = dt->content_tag;
	tags[ntags++] = index_tag;
	snap->info = fy_dedup_build_combined_info(da, tags, ntags);
	if (!snap->info)
		goto err_out;

	return 0;

err_out:
	if (index_tag != FY_ALLOC_TAG_ERROR)
		fy_allocator_release_tag(da->parent_allocator, index_tag);
	memset(snap, 0, sizeof(*snap));
	return -1;
}

static void fy_dedup_snapshot_release(struct fy_allocator *a, struct fy_allocator_snapshot *snap)
{
	struct fy_dedup_allocator *da;

	if (!a || !snap)
		return;

	da = container_of(a, struct fy_dedup_allocator, a);

	free(snap->info);
	if (snap->index_tag != FY_ALLOC_TAG_ERROR && snap->index_tag != FY_ALLOC_TAG_NONE)
		fy_allocator_release_tag(da->parent_allocator, snap->index_tag);
	memset(snap, 0, sizeof(*snap));
}

static struct fy_allocator_info *
fy_dedup_get_info(struct fy_allocator *a, int tag)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	struct fy_allocator_info *info;
	struct fy_allocator_tag_info *tag_info, *content_tag_info;
	unsigned int i;

	/* full dump not supported yet */
	if (tag == FY_ALLOC_TAG_NONE)
		return NULL;

	da = container_of(a, struct fy_dedup_allocator, a);

	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		return NULL;

	/* we only return the content tag info */
	info = fy_allocator_get_info(da->parent_allocator, dt->content_tag);
	if (!info)
		return NULL;

	/* we will have to change the tag from content to this one */
	content_tag_info = NULL;
	for (i = 0; i < info->num_tag_infos; i++) {
		tag_info = &info->tag_infos[i];
		if (tag_info->tag == dt->content_tag) {
			content_tag_info = tag_info;
			break;
		}
	}
	if (!content_tag_info)
		return NULL;

	/* copy to the first */
	content_tag_info->tag = tag;
	if (content_tag_info != &info->tag_infos[0])
		info->tag_infos[0] = *content_tag_info;
	info->num_tag_infos = 1;

	return info;
}

static enum fy_allocator_cap_flags
fy_dedup_get_caps(struct fy_allocator *a)
{
	struct fy_dedup_allocator *da;
	enum fy_allocator_cap_flags flags;

	da = container_of(a, struct fy_dedup_allocator, a);

	flags = da->parent_caps | FYACF_CAN_DEDUP | FYACF_CAN_LOOKUP;
	flags &= ~FYACF_CAN_FREE_INDIVIDUAL;

	return flags;
}

static bool fy_dedup_contains(struct fy_allocator *a, int tag, const void *ptr)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	int tag_start, tag_end;

	da = container_of(a, struct fy_dedup_allocator, a);
	if (!(da->parent_caps & FYACF_HAS_CONTAINS))
		return false;

	if (tag >= 0) {
		if (tag >= (int)da->tag_count)
			return false;
		tag_start = tag;
		tag_end = tag + 1;
	} else {
		tag_start = 0;
		tag_end = (int)da->tag_count;
	}

	for (tag = tag_start; tag < tag_end; tag++) {
		dt = fy_dedup_tag_from_tag(da, tag);
		if (!dt)
			continue;

		if (fy_allocator_contains(da->parent_allocator, dt->content_tag, ptr))
			return true;
	}
	return false;
}

static int fy_dedup_op_sync(struct fy_allocator *a)
{
	return fy_allocator_sync(fy_allocator_get_parent(a));
}

static uint64_t fy_dedup_op_refs_get(struct fy_allocator *a)
{
	return fy_allocator_refs_get(fy_allocator_get_parent(a));
}

static int fy_dedup_op_refs_publish(struct fy_allocator *a, uint64_t expected,
				    uint64_t desired, unsigned int flags)
{
	return fy_allocator_refs_publish(fy_allocator_get_parent(a), expected, desired, flags);
}

static uint64_t fy_dedup_op_generation(struct fy_allocator *a)
{
	return fy_allocator_generation(fy_allocator_get_parent(a));
}

static unsigned int fy_dedup_op_chunk_count(struct fy_allocator *a)
{
	return fy_allocator_chunk_count(fy_allocator_get_parent(a));
}

static uint64_t fy_dedup_op_region_base(struct fy_allocator *a)
{
	return fy_allocator_region_base(fy_allocator_get_parent(a));
}

static uint64_t fy_dedup_op_region_size(struct fy_allocator *a)
{
	return fy_allocator_region_size(fy_allocator_get_parent(a));
}

static uint64_t fy_dedup_op_index_region_base(struct fy_allocator *a)
{
	return fy_allocator_index_region_base(fy_allocator_get_parent(a));
}

static uint64_t fy_dedup_op_index_region_size(struct fy_allocator *a)
{
	return fy_allocator_index_region_size(fy_allocator_get_parent(a));
}

const struct fy_allocator_ops fy_dedup_allocator_ops = {
	.setup = fy_dedup_setup,
	.cleanup = fy_dedup_cleanup,
	.create = fy_dedup_create,
	.destroy = fy_dedup_destroy,
	.dump = fy_dedup_dump,
	.alloc = fy_dedup_alloc,
	.free = fy_dedup_free,
	.update_stats = fy_dedup_update_stats,
	.storev = fy_dedup_storev,
	.lookupv = fy_dedup_lookupv,
	.release = fy_dedup_release,
	.get_tag = fy_dedup_get_tag,
	.release_tag = fy_dedup_release_tag,
	.get_tag_count = fy_dedup_get_tag_count,
	.set_tag_count = fy_dedup_set_tag_count,
	.trim_tag = fy_dedup_trim_tag,
	.reset_tag = fy_dedup_reset_tag,
	.get_info = fy_dedup_get_info,
	.get_caps = fy_dedup_get_caps,
	.contains = fy_dedup_contains,
	.snapshot = fy_dedup_snapshot,
	.snapshot_release = fy_dedup_snapshot_release,
	.sync = fy_dedup_op_sync,
	.refs_get = fy_dedup_op_refs_get,
	.refs_publish = fy_dedup_op_refs_publish,
	.generation = fy_dedup_op_generation,
	.chunk_count = fy_dedup_op_chunk_count,
	.region_base = fy_dedup_op_region_base,
	.region_size = fy_dedup_op_region_size,
	.index_region_base = fy_dedup_op_index_region_base,
	.index_region_size = fy_dedup_op_index_region_size,
};
