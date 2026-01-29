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

#ifdef _WIN32
#include "fy-win32.h"
#endif

#include <stdio.h>

/* for container_of */
#include "fy-list.h"
#include "fy-utils.h"

#include "fy-allocator-dedup.h"

// #define DEBUG_GROWS

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

void fy_dedup_tag_data_destroy(struct fy_dedup_tag_data *dtd);

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

static void fy_dedup_tag_data_cleanup(struct fy_dedup_tag_data *dtd)
{
	struct fy_dedup_allocator *da = dtd->cfg.da;

	fy_parent_allocator_free(&da->a, dtd->buckets);
	fy_parent_allocator_free(&da->a, dtd->bloom_id);
	fy_parent_allocator_free(&da->a, dtd->buckets_in_use);
}

static int fy_dedup_tag_data_setup(struct fy_dedup_tag_data *dtd,
				   const struct fy_dedup_tag_data_cfg *cfg)
{
	struct fy_dedup_allocator *da;
	unsigned int bloom_filter_bits, bucket_count_bits, chain_length_grow_trigger;
	size_t dedup_threshold;
	size_t tmpsz;
	unsigned int i;

	assert(dtd);
	assert(cfg);

	da = cfg->da;

	bloom_filter_bits = cfg->bloom_filter_bits;
	bucket_count_bits = cfg->bucket_count_bits;
	dedup_threshold = cfg->dedup_threshold;
	chain_length_grow_trigger = cfg->chain_length_grow_trigger;

	memset(dtd, 0, sizeof(*dtd));
	dtd->cfg = *cfg;

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
	dtd->bloom_id = fy_parent_allocator_alloc(&da->a, tmpsz, _Alignof(fy_id_bits));
	if (!dtd->bloom_id)
		goto err_out;
	fy_id_reset(dtd->bloom_id, dtd->bloom_id_count);

	dtd->bucket_count = (size_t)1 << dtd->bucket_count_bits;
	assert(dtd->bucket_count);

	tmpsz = sizeof(*dtd->buckets) * dtd->bucket_count;
	dtd->buckets = fy_parent_allocator_alloc(&da->a, tmpsz, _Alignof(struct fy_dedup_entry *));
	if (!dtd->buckets)
		goto err_out;
	for (i = 0; i < dtd->bucket_count; i++)
		atomic_store(&dtd->buckets[i], NULL);

	dtd->bucket_id_count = (((size_t)1 << dtd->bucket_count_bits) + FY_ID_BITS_BITS - 1) / FY_ID_BITS_BITS;
	assert(dtd->bucket_id_count > 0);
	tmpsz = dtd->bucket_id_count * sizeof(*dtd->buckets_in_use);
	dtd->buckets_in_use = fy_parent_allocator_alloc(&da->a, tmpsz, _Alignof(fy_id_bits));
	if (!dtd->buckets_in_use)
		goto err_out;
	fy_id_reset(dtd->buckets_in_use, dtd->bucket_id_count);

	return 0;

err_out:
	fy_dedup_tag_data_cleanup(dtd);
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
		fprintf(stderr, "%s: bucket count=%u used=%lu\n",
				__func__, 1 << dtd->bucket_count_bits,
				fy_id_count_used(dtd->buckets_in_use, dtd->bucket_id_count));
#endif

		fy_dedup_tag_data_destroy(dtd);
	}

	/* we just release the tags, the underlying allocator should free everything */
	if (dt->content_tag != FY_ALLOC_TAG_NONE)
		fy_allocator_release_tag(da->parent_allocator, dt->content_tag);

}

struct fy_dedup_tag_data *
fy_dedup_tag_data_create(const struct fy_dedup_tag_data_cfg *cfg)
{
	struct fy_dedup_allocator *da = cfg->da;
	struct fy_dedup_tag_data *dtd;
	int ret;

	dtd = fy_parent_allocator_alloc(&da->a, sizeof(*dtd), _Alignof(struct fy_dedup_tag_data));
	if (!dtd)
		return NULL;

	ret = fy_dedup_tag_data_setup(dtd, cfg);
	if (ret)
		goto err_out;

	return dtd;

err_out:
	fy_parent_allocator_free(&da->a, dtd);
	return NULL;
}

void fy_dedup_tag_data_destroy(struct fy_dedup_tag_data *dtd)
{
	struct fy_dedup_allocator *da;

	if (!dtd)
		return;

	da = dtd->cfg.da;

	fy_dedup_tag_data_cleanup(dtd);
	fy_parent_allocator_free(&da->a, dtd);
}

static int fy_dedup_tag_setup(struct fy_dedup_allocator *da, struct fy_dedup_tag *dt)
{
	struct fy_dedup_tag_data *dtd;

	assert(da);
	assert(dt);

	memset(dt, 0, sizeof(*dt));

	fy_atomic_flag_clear(&dt->growing);
	if (da->parent_caps & FYACF_CAN_FREE_TAG) {
		dt->content_tag = fy_allocator_get_tag(da->parent_allocator);
		if (dt->content_tag == FY_ALLOC_TAG_ERROR)
			goto err_out;
	} else
		dt->content_tag = FY_ALLOC_TAG_DEFAULT;

	fy_atomic_store(&dt->tag_datas, NULL);

	dtd = fy_dedup_tag_data_create(
			&(struct fy_dedup_tag_data_cfg) {
				.da = da,
				.dt = dt,
				.bloom_filter_bits = da->bloom_filter_bits,
				.bucket_count_bits = da->bucket_count_bits,
				.dedup_threshold = da->dedup_threshold,
				.chain_length_grow_trigger = da->chain_length_grow_trigger
			});
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
fy_dedup_tag_prepare_new(struct fy_dedup_tag_data *dtd, struct fy_dedup_tag_data *new_dtd,
			 int bloom_filter_adjust_bits, int bucket_adjust_bits)
{
	unsigned int bit_shift, new_bucket_count_bits, new_bloom_filter_bits;
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

	/* setup the new data */
	rc = fy_dedup_tag_data_setup(new_dtd,
			&(struct fy_dedup_tag_data_cfg) {
				.da = dtd->cfg.da,
				.dt = dtd->cfg.dt,
				.bloom_filter_bits = new_bloom_filter_bits,
				.bucket_count_bits = new_bucket_count_bits,
				.dedup_threshold = dtd->cfg.da->dedup_threshold,
				.chain_length_grow_trigger = dtd->cfg.da->chain_length_grow_trigger
			});
	return rc;
}


static int
fy_dedup_tag_adjust(struct fy_dedup_allocator *da, struct fy_dedup_tag *dt,
		    int bloom_filter_adjust_bits, int bucket_adjust_bits)
{
	size_t bucket_count, bucket_used;
	struct fy_dedup_tag_data *dtd, *new_dtd = NULL;
	float occupancy_ratio;
	int rc;

	rc = -1;

	assert(da);
	assert(dt);

	dtd = fy_atomic_load(&dt->tag_datas);
	if (!dtd)
		return -1;

	if (!fy_atomic_flag_test_and_set(&dt->growing)) {
#ifdef DEBUG_GROWS
		fprintf(stderr, "grow: abort due another grow in progress\n");
#endif
		return -1;
	}

	bucket_count = 1U << dtd->bucket_count_bits;
	bucket_used = fy_id_count_used(dtd->buckets_in_use, dtd->bucket_id_count);
	occupancy_ratio = (double)bucket_used/(double)bucket_count;

	/* do not grow until we're over 60% full */
	if (occupancy_ratio < da->cfg.minimum_bucket_occupancy) {
#ifdef DEBUG_GROWS
		fprintf(stderr, "grow: abort due to less that %f%% full (is %f)\n",
				100 * da->cfg.minimum_bucket_occupancy,
				100 * occupancy_ratio);
#endif
		goto out_ok;
	}

	new_dtd = fy_parent_allocator_alloc(&da->a, sizeof(*new_dtd), _Alignof(struct fy_dedup_tag_data));
	if (!new_dtd)
		goto err_out;

	/* prepare the new one */
	rc = fy_dedup_tag_prepare_new(dtd, new_dtd, bloom_filter_adjust_bits, bucket_adjust_bits);
	if (rc)
		goto err_out;

#ifdef DEBUG_GROWS
	{
		size_t bloom_count, new_bloom_count, bloom_used;
		size_t bucket_count, new_bucket_count, bucket_used;

		bloom_count = 1U << dtd->bloom_filter_bits;
		new_bloom_count = 1U << new_dtd->bloom_filter_bits;
		bloom_used = fy_id_count_used(dtd->bloom_id, dtd->bloom_id_count);
		bucket_count = 1U << dtd->bucket_count_bits;
		new_bucket_count = 1U << new_dtd->bucket_count_bits;
		bucket_used = fy_id_count_used(dtd->buckets_in_use, dtd->bucket_id_count);

		fprintf(stderr, "grow: chain_length_grow_trigger=%u->%u bloom %zu->%zu used %zu (%2.2f%%) ",
				dtd->chain_length_grow_trigger, new_dtd->chain_length_grow_trigger,
				bloom_count, new_bloom_count,
				bloom_used, 100.0*(double)bloom_used/(double)bloom_count);
		fprintf(stderr, "bucket %zu->%zu used %zu (%2.2f%%)\n", bucket_count, new_bucket_count,
				bucket_used, 100.0*(double)bucket_used/(double)bucket_count);
	}
#endif

	/* try to add it, if the head changed, then drop our stuff and pretend nothing happened */
	new_dtd->next = dtd;
	if (!fy_atomic_compare_exchange_strong(&dt->tag_datas, &dtd, new_dtd)) {
#ifdef DEBUG_GROWS
		fprintf(stderr, "grow: Update cancelled, someone beat us to it\n");
#endif
		fy_dedup_tag_data_cleanup(new_dtd);
		fy_parent_allocator_free(&da->a, new_dtd);
	}

out_ok:
	rc = 0;
out:
	fy_atomic_flag_clear(&dt->growing);
	return rc;

err_out:
	fy_parent_allocator_free(&da->a, new_dtd);
	rc = -1;
	goto out;
}

static void fy_dedup_tag_trim(struct fy_dedup_allocator *da, struct fy_dedup_tag *dt)
{
	if (!da || !dt)
		return;

	/* just pass them trim down to the parent */
	if (da->parent_caps & FYACF_CAN_FREE_TAG)
		fy_allocator_trim_tag(da->parent_allocator, dt->content_tag);
}

static void fy_dedup_tag_reset(struct fy_dedup_allocator *da, struct fy_dedup_tag *dt)
{
	struct fy_dedup_tag_data *dtd, *dtd_head;
	size_t i;

	if (!da || !dt)
		return;

	/* just pass them reset down to the parent */
	if (da->parent_caps & FYACF_CAN_FREE_TAG)
		fy_allocator_reset_tag(da->parent_allocator, dt->content_tag);

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
			fy_dedup_tag_data_destroy(dtd);
	}

	if (dtd_head) {
		dtd = dtd_head;

		dtd->next = NULL;
		fy_id_reset(dtd->bloom_id, dtd->bloom_id_count);
		for (i = 0; i < dtd->bucket_count; i++)
			atomic_store(&dtd->buckets[i], NULL);
		fy_id_reset(dtd->buckets_in_use, dtd->bucket_id_count);
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

	for (i = 0; i < da->tag_count; i++) {
		dt = fy_dedup_tag_from_tag(da, i);
		if (!dt)
			continue;
		fy_dedup_tag_cleanup(da, dt);
	}

	fy_parent_allocator_free(&da->a, da->ids);
	fy_parent_allocator_free(&da->a, da->tags);
}

static int fy_dedup_setup(struct fy_allocator *a, struct fy_allocator *parent, int parent_tag, const void *cfg_data)
{
	struct fy_dedup_allocator *da = NULL;
	const struct fy_dedup_allocator_cfg *cfg;
	unsigned int bloom_filter_bits, bucket_count_bits;
	unsigned int bit_shift, chain_length_grow_trigger;
	size_t dedup_threshold, tmpsz;
	bool has_estimate;
	struct fy_dedup_tag *dt;
	int rc;

	if (!a || !cfg_data)
		return -1;

	cfg = cfg_data;
	if (!cfg->parent_allocator)
		return -1;

	has_estimate = cfg->estimated_content_size && cfg->estimated_content_size != SIZE_MAX;

	/* power of two so ffs = log2 */
	bit_shift = (unsigned int)fy_id_ffs(FY_ID_BITS_BITS);

	bucket_count_bits = cfg->bucket_count_bits;
	if (!bucket_count_bits && has_estimate) {
		bucket_count_bits = 1;
		while (((size_t)1 << bucket_count_bits) < (cfg->estimated_content_size / BUCKET_ESTIMATE_DIV))
			bucket_count_bits++;
#ifdef DEBUG_GROWS
		fprintf(stderr, "bucket_count_bits %u\n", bucket_count_bits);
#endif
	}
	/* at least that amount */
	if (bucket_count_bits < bit_shift)
		bucket_count_bits = bit_shift;
	/* keep the bucket count bits in signed int range */
	if (bucket_count_bits > (sizeof(int) * 8 - 1))
		bucket_count_bits = (sizeof(int) * 8) - 1;

	bloom_filter_bits = cfg->bloom_filter_bits;
	if (!bloom_filter_bits && has_estimate) {
		bloom_filter_bits = 1;
		while (((size_t)1 << bloom_filter_bits) < (cfg->estimated_content_size / BLOOM_ESTIMATE_DIV))
			bloom_filter_bits++;
#ifdef DEBUG_GROWS
		fprintf(stderr, "bloom_filter_bits %u\n", bloom_filter_bits);
#endif
	}
	/* must be more than bucket count bits */
	if (bloom_filter_bits < bucket_count_bits)
		bloom_filter_bits = bucket_count_bits + 3;	/* minimum fanout */
	/* keep the bloom filter bits in signed int range */
	if (bloom_filter_bits > (sizeof(int) * 8 - 1))
		bloom_filter_bits = (sizeof(int) * 8) - 1;

	dedup_threshold = cfg->dedup_threshold;
	chain_length_grow_trigger = cfg->chain_length_grow_trigger;

	da = container_of(a, struct fy_dedup_allocator, a);
	memset(da, 0, sizeof(*da));

	da->a.name = "dedup";
	da->a.ops = &fy_dedup_allocator_ops;
	da->a.parent = parent;
	da->a.parent_tag = parent_tag;
	da->cfg = *cfg;

	da->parent_allocator = cfg->parent_allocator;
	da->parent_caps = fy_allocator_get_caps(da->parent_allocator);

	da->bloom_filter_bits = bloom_filter_bits;
	da->bucket_count_bits = bucket_count_bits;
	da->dedup_threshold = dedup_threshold;
	da->chain_length_grow_trigger = chain_length_grow_trigger;

	/* we use as many tags as the parent allocator */
	rc = fy_allocator_get_tag_count(da->parent_allocator);
	if (rc <= 0)
		goto err_out;
	da->tag_count = (unsigned int)rc;
	da->tag_id_count = (da->tag_count + FY_ID_BITS_BITS - 1) / FY_ID_BITS_BITS;

	tmpsz = da->tag_id_count * sizeof(*da->ids);
	da->ids = fy_parent_allocator_alloc(&da->a, tmpsz, _Alignof(fy_id_bits));
	if (!da->ids)
		goto err_out;
	fy_id_reset(da->ids, da->tag_id_count);

	tmpsz = da->tag_count * sizeof(*da->tags);
	da->tags = fy_parent_allocator_alloc(&da->a, tmpsz, _Alignof(struct fy_dedup_tag));
	if (!da->tags)
		goto err_out;

	/* start with tag 0 as general use */
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

	fy_parent_allocator_free(a, da);
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

	stats->unique_stores += fy_atomic_get_and_clear_counter(&dt->unique_stores);
	stats->dup_stores += fy_atomic_get_and_clear_counter(&dt->dup_stores);
	stats->collisions += fy_atomic_get_and_clear_counter(&dt->collisions);

	return 0;
}

/* we can lookup! */
static const void *fy_dedup_lookupv(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align, uint64_t hash)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	struct fy_dedup_tag_data *dtd;
	struct fy_dedup_entry *de;
	int bloom_pos, bucket_pos;
	bool bloom_hit;
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

	for (dtd = fy_atomic_load(&dt->tag_datas); dtd; dtd = dtd->next) {
		bloom_pos = (int)(hash & (uint64_t)dtd->bloom_filter_mask);
		bloom_hit = fy_id_is_used(dtd->bloom_id, dtd->bloom_id_count, bloom_pos);
		if (bloom_hit) {
			bucket_pos = (int)(hash & (uint64_t)dtd->bucket_count_mask);
			for (de = fy_atomic_load(&dtd->buckets[bucket_pos]); de; de = de->next) {
				if (de->hash == hash && total_size == de->size &&
						!fy_iovec_cmp(iov, iovcnt, de->mem) &&
						((uintptr_t)de->mem & (align - 1)) == 0)
					return de->mem;
			}
		}
	}
	return NULL;
}

static const void *fy_dedup_storev(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align, uint64_t hash)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	struct fy_dedup_tag_data *dtd, *dtd_best;
	struct fy_dedup_entry *de, *de_head;
	int bloom_pos, bucket_pos;
	bool bloom_hit;
	unsigned int chain_length;
	void *mem = NULL;
	size_t total_size, de_offset, max_align;
	bool at_head;

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

again:
	de = NULL;
	chain_length = 0;

	at_head = true;
	dtd_best = NULL;
	for (dtd = fy_atomic_load(&dt->tag_datas); dtd; dtd = dtd->next) {

		/* first check in the bloom filter */
		bloom_pos = (int)(hash & (uint64_t)dtd->bloom_filter_mask);
		bucket_pos = (int)(hash & (uint64_t)dtd->bucket_count_mask);

		bloom_hit = fy_id_is_used(dtd->bloom_id, dtd->bloom_id_count, bloom_pos);
		if (bloom_hit) {
			for (de = fy_atomic_load(&dtd->buckets[bucket_pos]); de; de = de->next) {
				if (de->hash == hash) {
				       	if (total_size == de->size && !fy_iovec_cmp(iov, iovcnt, de->mem) &&
							((uintptr_t)de->mem & (align - 1)) == 0) {

						/* only update stats if someone asked for it */
						if (a->flags & FYAF_KEEP_STATS)
							fy_atomic_fetch_add(&dt->dup_stores, 1);

						if (a->flags & FYAF_TRACE) {
							int i;
							size_t j;
							printf("%s: %p: dup-store %p 0x%016"PRIx64" %zx:", __func__,
									a, de->mem, hash, total_size);
							for (i = 0; i < iovcnt; i++) {
								for (j = 0; j < iov[i].iov_len; j++) {
									printf("%02x", (int)(((uint8_t *)iov[i].iov_base)[j]) & 0xff);
								}
							}
							printf("\n");
						}

						return de->mem;
					}

					/* mark that we had a collision here */
					if (a->flags & FYAF_KEEP_STATS)
						fy_atomic_fetch_add(&dt->collisions, 1);
				}

				if (at_head)
					chain_length++;
			}
		} else {
			if (!dtd_best)
				dtd_best = dtd;	/* keep whenever we had a empty bloom slot */
		}
		at_head = false;
	}

	/* use the one that had space, otherwise the top */
	if (dtd_best)
		dtd = dtd_best;
	else
		dtd = fy_atomic_load(&dt->tag_datas);

	/* recalc positions for the delected dtd */
	bloom_pos = (int)(hash & (uint64_t)dtd->bloom_filter_mask);
	bucket_pos = (int)(hash & (uint64_t)dtd->bucket_count_mask);

	/* we might be retrying; don't allocate and copy again */
	if (!mem) {
		/* place the dedup entry at the aligned offset after the data */
		de_offset = fy_size_t_align(total_size, _Alignof(struct fy_dedup_entry));
		max_align = align > _Alignof(struct fy_dedup_entry) ? align : _Alignof(struct fy_dedup_entry);
		mem = fy_allocator_alloc_nocheck(da->parent_allocator, dt->content_tag, de_offset + sizeof(*de), max_align);
		if (!mem)
			return NULL;

		/* verify it's aligned correctly */
		assert(((uintptr_t)mem & (align - 1)) == 0);

		de = (void *)((char *)mem + de_offset);

		de->hash = hash;
		de->size = total_size;
		de->mem = mem;

		/* and copy the data */
		fy_iovec_copy_from(iov, iovcnt, de->mem);
	}

	/* set this bucket to used */
	fy_id_set_used(dtd->buckets_in_use, dtd->bucket_id_count, bucket_pos);

	/* turn the update bit for the bloom position */
	fy_id_set_used(dtd->bloom_id, dtd->bloom_id_count, bloom_pos);

	/* add to the bucket last atomically */
	de_head = fy_atomic_load(&dtd->buckets[bucket_pos]);
	de->next = de_head;
	if (!fy_atomic_compare_exchange_strong(&dtd->buckets[bucket_pos], &de_head, de))
		goto again;	// we're leaking the entry, but that's fine

	/* adjust by one bit, if we've hit the trigger */
	if (chain_length > dtd->chain_length_grow_trigger)
		fy_dedup_tag_adjust(da, dt, 1, 1);

	/* only update stats if someone asked for it */
	if (a->flags & FYAF_KEEP_STATS)
		fy_atomic_fetch_add(&dt->unique_stores, 1);

	if (a->flags & FYAF_TRACE) {
		int i;
		size_t j;
		printf("%s: %p: new-store %p 0x%016"PRIx64" %zx:", __func__,
				a, de->mem, hash, total_size);
		for (i = 0; i < iovcnt; i++) {
			for (j = 0; j < iov[i].iov_len; j++) {
				printf("%02x", (int)(((uint8_t *)iov[i].iov_base)[j]) & 0xff);
			}
		}
		printf("\n");
	}

	return de->mem;
}

static void fy_dedup_release(struct fy_allocator *a, int tag, const void *data, size_t size)
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
		fy_parent_allocator_free(&da->a, da->ids);
	}

	da->tag_count = tag_count;
	da->tag_id_count = tag_id_count;

	return 0;

err_out:
	fy_parent_allocator_free(&da->a, alloc_tags);
	fy_parent_allocator_free(&da->a, alloc_ids);
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

static struct fy_allocator_info *
fy_dedup_get_info(struct fy_allocator *a, int tag)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	struct fy_allocator_info *info;
	struct fy_allocator_tag_info *tag_info;
	unsigned int i;

	/* full dump not supported yet */
	if (tag == FY_ALLOC_TAG_NONE)
		return NULL;

	da = container_of(a, struct fy_dedup_allocator, a);

	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		return NULL;

	info = fy_allocator_get_info(da->parent_allocator, dt->content_tag);
	if (!info)
		return NULL;

	/* we will have to change the tag from content to this one */
	for (i = 0; i < info->num_tag_infos; i++) {
		tag_info = &info->tag_infos[i];
		if (tag_info->tag == dt->content_tag)
			tag_info->tag = tag;
	}

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
};
