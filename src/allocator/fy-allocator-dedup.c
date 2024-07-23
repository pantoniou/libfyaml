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
#define AFTER() \
	({ \
		clock_gettime(CLOCK_MONOTONIC, &after); \
		(int64_t)(after.tv_sec - before.tv_sec) * (int64_t)1000000000UL + (int64_t)(after.tv_nsec - before.tv_nsec); \
	})

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

static inline struct fy_dedup_tag *
fy_dedup_tag_from_tag(struct fy_dedup_allocator *da, int tag)
{
	if (!da)
		return NULL;

	if ((unsigned int)tag >= ARRAY_SIZE(da->tags))
		return NULL;

	if (!fy_id_is_used(da->ids, ARRAY_SIZE(da->ids), (int)tag))
		return NULL;

	return &da->tags[tag];
}

static void fy_dedup_tag_data_cleanup(struct fy_dedup_allocator *da, struct fy_dedup_tag_data *dtd)
{
	if (dtd->buckets)
		free(dtd->buckets);
	if (dtd->bloom_id)
		free(dtd->bloom_id);
	if (dtd->buckets_in_use)
		free(dtd->buckets_in_use);
	memset(dtd, 0, sizeof(*dtd));
}

static int fy_dedup_tag_data_setup(struct fy_dedup_allocator *da, struct fy_dedup_tag_data *dtd,
		unsigned int bloom_filter_bits, unsigned int bucket_count_bits,
		size_t dedup_threshold, unsigned int chain_length_grow_trigger)
{
	size_t buckets_size;

	assert(da);
	assert(dtd);
	assert(dtd);

	memset(dtd, 0, sizeof(*dtd));

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

	dtd->bloom_filter_mask = (1U << dtd->bloom_filter_bits) - 1;
	dtd->bucket_count_mask = (1U << dtd->bucket_count_bits) - 1;

	dtd->bloom_id_count = (1U << dtd->bloom_filter_bits) / FY_ID_BITS_BITS;
	dtd->bloom_id = malloc(2 * dtd->bloom_id_count * sizeof(*dtd->bloom_id));
	if (!dtd->bloom_id)
		goto err_out;
	dtd->bloom_update_id = dtd->bloom_id + dtd->bloom_id_count;
	fy_id_reset(dtd->bloom_id, dtd->bloom_id_count);
	fy_id_reset(dtd->bloom_update_id, dtd->bloom_id_count);

	dtd->bucket_count = 1U << dtd->bucket_count_bits;

	buckets_size = sizeof(*dtd->buckets) * dtd->bucket_count;
	dtd->buckets = malloc(buckets_size);
	if (!dtd->buckets)
		goto err_out;
	dtd->buckets_end = dtd->buckets + dtd->bucket_count;

	dtd->bucket_id_count = (1U << dtd->bucket_count_bits) / FY_ID_BITS_BITS;
	dtd->buckets_in_use = malloc(2 * dtd->bucket_id_count * sizeof(*dtd->buckets_in_use));
	if (!dtd->buckets_in_use)
		goto err_out;
	dtd->buckets_collision = dtd->buckets_in_use + dtd->bucket_id_count;
	fy_id_reset(dtd->buckets_in_use, dtd->bucket_id_count);
	fy_id_reset(dtd->buckets_collision, dtd->bucket_id_count);

	return 0;

err_out:
	fy_dedup_tag_data_cleanup(da, dtd);
	return -1;
}

static void fy_dedup_tag_cleanup(struct fy_dedup_allocator *da, struct fy_dedup_tag *dt)
{
	struct fy_dedup_tag_data *dtd;
	int id;

	if (!da || !dt)
		return;

	/* get the id from the pointer */
	id = dt - da->tags;
	assert((unsigned int)id < ARRAY_SIZE(da->tags));

	/* already clean? */
	if (fy_id_is_free(da->ids, ARRAY_SIZE(da->ids), id))
		return;

	fy_id_free(da->ids, ARRAY_SIZE(da->ids), id);

	dtd = &dt->data[dt->data_active];

#ifdef DEBUG_GROWS
	fprintf(stderr, "%s: dump of state at close\n", __func__);
	fprintf(stderr, "%s: bloom count=%u used=%lu\n",
			__func__, 1 << dtd->bloom_filter_bits,
			fy_id_count_used(dtd->bloom_id, dtd->bloom_id_count));
	fprintf(stderr, "%s: bucket count=%u used=%lu collision=%lu\n",
			__func__, 1 << dtd->bucket_count_bits,
			fy_id_count_used(dtd->buckets_in_use, dtd->bucket_id_count),
			fy_id_count_used(dtd->buckets_collision, dtd->bucket_id_count));
#endif

	fy_dedup_tag_data_cleanup(da, dtd);

	/* we just release the tags, the underlying allocator should free everything */
	if (dt->entries_tag != FY_ALLOC_TAG_NONE)
		fy_allocator_release_tag(da->entries_allocator, dt->entries_tag);
	if (dt->content_tag != FY_ALLOC_TAG_NONE)
		fy_allocator_release_tag(da->parent_allocator, dt->content_tag);

	memset(dt, 0, sizeof(*dt));
	dt->entries_tag = FY_ALLOC_TAG_NONE;
	dt->content_tag = FY_ALLOC_TAG_NONE;
}

static int fy_dedup_tag_setup(struct fy_dedup_allocator *da, struct fy_dedup_tag *dt)
{
	struct fy_dedup_tag_data *dtd;
	int rc;

	assert(da);
	assert(dt);

	memset(dt, 0, sizeof(*dt));

	dt->entries_tag = FY_ALLOC_TAG_NONE;
	dt->content_tag = FY_ALLOC_TAG_NONE;

	dt->entries_tag = fy_allocator_get_tag(da->entries_allocator);
	if (dt->entries_tag == FY_ALLOC_TAG_ERROR)
		goto err_out;

	dt->content_tag = fy_allocator_get_tag(da->parent_allocator);
	if (dt->content_tag == FY_ALLOC_TAG_ERROR)
		goto err_out;

	dt->data_active = 0;
	dtd = &dt->data[dt->data_active];

	rc = fy_dedup_tag_data_setup(da, dtd, da->bloom_filter_bits, da->bucket_count_bits, da->dedup_threshold, da->chain_length_grow_trigger);
	if (rc)
		goto err_out;

	return 0;

err_out:
	fy_dedup_tag_cleanup(da, dt);
	return -1;
}

static int fy_dedup_tag_adjust(struct fy_dedup_allocator *da, struct fy_dedup_tag *dt, int bloom_filter_adjust_bits, int bucket_adjust_bits)
{
	struct fy_dedup_tag_data *dtd, *new_dtd;
	unsigned int bit_shift, new_bucket_count_bits, new_bloom_filter_bits;
	unsigned int bloom_pos, bucket_pos;
	struct fy_dedup_entry *de;
	struct fy_dedup_entry_list *del, *new_del;
	struct fy_id_iter iter;
	int rc, id;

#ifdef DEBUG_GROWS
	int64_t ns;
	struct timespec before, after;
	BEFORE();
#endif

	bit_shift = (unsigned int)fy_id_ffs(FY_ID_BITS_BITS);

	assert(da);
	assert(dt);
	dtd = &dt->data[dt->data_active];
	new_dtd = &dt->data[!dt->data_active];

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
	rc = fy_dedup_tag_data_setup(da, new_dtd, new_bloom_filter_bits, new_bucket_count_bits, da->dedup_threshold, da->chain_length_grow_trigger);
	if (rc)
		goto err_out;

	fy_id_iter_begin(dtd->buckets_in_use, dtd->bucket_id_count, &iter);
	while ((id = fy_id_iter_next(dtd->buckets_in_use, dtd->bucket_id_count, &iter)) >= 0) {

		assert(fy_id_is_used(dtd->buckets_in_use, dtd->bucket_id_count, id));

		del = dtd->buckets + id;

		while ((de = fy_dedup_entry_list_pop(del)) != NULL) {

			bloom_pos = (unsigned int)de->hash & new_dtd->bloom_filter_mask;
			assert((int)bloom_pos >= 0);
			fy_id_set_used(new_dtd->bloom_id, new_dtd->bloom_id_count, bloom_pos);

			bucket_pos = (unsigned int)de->hash & new_dtd->bucket_count_mask;
			assert((int)bucket_pos >= 0);

			new_del = new_dtd->buckets + bucket_pos;
			if (!fy_id_is_used(new_dtd->buckets_in_use, new_dtd->bucket_id_count, bucket_pos)) {
				assert(FY_ID_OFFSET(bucket_pos) < new_dtd->bucket_id_count);
				fy_id_set_used(new_dtd->buckets_in_use, new_dtd->bucket_id_count, bucket_pos);
				fy_dedup_entry_list_init(new_del);
			} else {
				assert(FY_ID_OFFSET(bucket_pos) < new_dtd->bucket_id_count);
				fy_id_set_used(new_dtd->buckets_collision, new_dtd->bucket_id_count, bucket_pos);
			}
			fy_dedup_entry_list_add(new_del, de);
		}
	}
	fy_id_iter_end(dtd->buckets_in_use, dtd->bucket_id_count, &iter);

#ifdef DEBUG_GROWS

	ns = AFTER();
	fprintf(stderr, "%s: operation took place in %"PRId64"ns\n", __func__, ns);

	{
		const char *ban[2] = { "old", "new" };
		struct fy_dedup_tag_data *arr[2] = { dtd, new_dtd };
		struct fy_dedup_tag_data *d;
		size_t bloom_count, bloom_used;
		size_t bucket_count, bucket_used, bucket_collision;
		unsigned int i;

		for (i = 0; i < 2; i++) {
			d = arr[i];
			bloom_count = 1U << d->bloom_filter_bits;
			bloom_used = fy_id_count_used(d->bloom_id, d->bloom_id_count);
			bucket_count = 1U << d->bucket_count_bits;
			bucket_used = fy_id_count_used(d->buckets_in_use, d->bucket_id_count);
			bucket_collision = fy_id_count_used(d->buckets_collision, d->bucket_id_count);


			fprintf(stderr, "%s:  bloom %zu used %zu (%2.2f%%) ", ban[i], bloom_count,
					bloom_used, 100.0*(double)bloom_used/(double)bloom_count);
			fprintf(stderr, "bucket %zu used %zu (%2.2f%%) coll %zu (%2.2f%%)\n", bucket_count,
					bucket_used, 100.0*(double)bucket_used/(double)bucket_count,
					bucket_collision, 100.0*(double)bucket_collision/(double)bucket_count);
		}
	}
#endif

	/* cleanup the old data */
	fy_dedup_tag_data_cleanup(da, dtd);

	/* switch to the new one */
	dt->data_active = !dt->data_active;

	return 0;

err_out:
	return -1;
}

static void fy_dedup_tag_trim(struct fy_dedup_allocator *da, struct fy_dedup_tag *dt)
{
	if (!da || !dt)
		return;

	/* just pass them trim down to the parent */
	fy_allocator_trim_tag(da->entries_allocator, dt->entries_tag);
	fy_allocator_trim_tag(da->parent_allocator, dt->content_tag);
}

static void fy_dedup_tag_reset(struct fy_dedup_allocator *da, struct fy_dedup_tag *dt)
{
	struct fy_dedup_tag_data *dtd;

	if (!da || !dt)
		return;

	/* just pass them reset down to the parent */
	fy_allocator_reset_tag(da->entries_allocator, dt->entries_tag);
	fy_allocator_reset_tag(da->parent_allocator, dt->content_tag);

	dtd = &dt->data[dt->data_active];

	fy_id_reset(dtd->bloom_id, dtd->bloom_id_count);
	fy_id_reset(dtd->bloom_update_id, dtd->bloom_id_count);
	fy_id_reset(dtd->buckets_in_use, dtd->bucket_id_count);
	fy_id_reset(dtd->buckets_collision, dtd->bucket_id_count);
}

static int fy_dedup_tag_update_stats(struct fy_dedup_allocator *da, struct fy_dedup_tag *dt, struct fy_allocator_stats *stats)
{
	unsigned int i;

	if (!da || !dt)
		return -1;

	/* collect the underlying stats */
	fy_allocator_update_stats(da->entries_allocator, dt->entries_tag, stats);
	fy_allocator_update_stats(da->parent_allocator, dt->content_tag, stats);

	/* and update with these ones */
	for (i = 0; i < ARRAY_SIZE(dt->stats.counters); i++) {
		stats->counters[i] += dt->stats.counters[i];
		dt->stats.counters[i] = 0;
	}

	return 0;
}

static void fy_dedup_cleanup(struct fy_allocator *a);

#define BUCKET_ESTIMATE_DIV 1024
#define BLOOM_ESTIMATE_DIV 128

static int fy_dedup_setup(struct fy_allocator *a, const void *cfg_data)
{
	struct fy_dedup_allocator *da = NULL;
	const struct fy_dedup_allocator_cfg *cfg;
	unsigned int bloom_filter_bits, bucket_count_bits;
	unsigned int bit_shift, chain_length_grow_trigger;
	size_t dedup_threshold;
	bool has_estimate;

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
		while ((1LU << bucket_count_bits) < (cfg->estimated_content_size / BUCKET_ESTIMATE_DIV))
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
		while ((1LU << bloom_filter_bits) < (cfg->estimated_content_size / BLOOM_ESTIMATE_DIV))
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
	da->cfg = *cfg;

	da->parent_allocator = cfg->parent_allocator;

	/* just create a default mremap allocator for the entries */
	/* we don't care if they are contiguous */
	da->entries_allocator = fy_allocator_create("mremap", NULL);
	if (!da->entries_allocator)
		goto err_out;

	da->bloom_filter_bits = bloom_filter_bits;
	da->bucket_count_bits = bucket_count_bits;
	da->dedup_threshold = dedup_threshold;
	da->chain_length_grow_trigger = chain_length_grow_trigger;

	/* just a seed, perhaps it should be configurable? */
	da->xxseed = ((uint64_t)rand() << 32) | (uint32_t)rand();
	/* start with the state already initialized */
	XXH64_reset(&da->xxstate_template, da->xxseed);

	fy_id_reset(da->ids, ARRAY_SIZE(da->ids));

	return 0;
err_out:
	fy_dedup_cleanup(a);
	return -1;
}

static void fy_dedup_cleanup(struct fy_allocator *a)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	unsigned int i;

	if (!a)
		return;

	da = container_of(a, struct fy_dedup_allocator, a);

	for (i = 0, dt = da->tags; i < ARRAY_SIZE(da->tags); i++, dt++)
		fy_dedup_tag_cleanup(da, dt);

	if (da->entries_allocator)
		fy_allocator_destroy(da->entries_allocator);

}

struct fy_allocator *fy_dedup_create(const void *cfg)
{
	struct fy_dedup_allocator *da = NULL;
	int rc;

	da = malloc(sizeof(*da));
	if (!da)
		goto err_out;

	rc = fy_dedup_setup(&da->a, cfg);
	if (rc)
		goto err_out;

	return &da->a;

err_out:
	if (da)
		free(da);

	return NULL;
}

void fy_dedup_destroy(struct fy_allocator *a)
{
	struct fy_dedup_allocator *da;

	if (!a)
		return;

	da = container_of(a, struct fy_dedup_allocator, a);
	fy_dedup_cleanup(a);
	free(da);
}

void fy_dedup_dump(struct fy_allocator *a)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	unsigned int i;

	if (!a)
		return;

	da = container_of(a, struct fy_dedup_allocator, a);

	fprintf(stderr, "dedup: ");
	for (i = 0, dt = da->tags; i < ARRAY_SIZE(da->tags); i++, dt++)
		fprintf(stderr, "%c", fy_id_is_free(da->ids, ARRAY_SIZE(da->ids), i) ? '.' : 'x');
	fprintf(stderr, "\n");

	for (i = 0, dt = da->tags; i < ARRAY_SIZE(da->tags); i++, dt++) {
		if (fy_id_is_free(da->ids, ARRAY_SIZE(da->ids), i))
			continue;

		fprintf(stderr, "  %d: tags: content=%d entries=%d\n", i,
				dt->content_tag, dt->entries_tag);

	}

	fprintf(stderr, "dedup: dumping parent allocator\n");
	fy_allocator_dump(da->parent_allocator);
	fprintf(stderr, "dedup: dumping entries allocator\n");
	fy_allocator_dump(da->entries_allocator);
}

static void *fy_dedup_alloc(struct fy_allocator *a, int tag, size_t size, size_t align)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	void *p;

	if (!a)
		return NULL;

	da = container_of(a, struct fy_dedup_allocator, a);

	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		goto err_out;

	/* just pass to the parent allocator using the content tag */
	p = fy_allocator_alloc(da->parent_allocator, dt->content_tag, size, align);
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

	if (!a)
		return;

	if (!data)
		return;

	da = container_of(a, struct fy_dedup_allocator, a);

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

	if (!a || !stats)
		return -1;

	da = container_of(a, struct fy_dedup_allocator, a);

	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		return -1;

	return fy_dedup_tag_update_stats(da, dt, stats);
}

static const void *fy_dedup_storev(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align)
{
	XXH64_state_t xxstate;
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	struct fy_dedup_tag_data *dtd;
	struct fy_dedup_entry *de;
	uint64_t hash;
	unsigned int bloom_pos, bucket_pos;
	bool bloom_hit;
	struct fy_dedup_entry_list *del;
	unsigned int chain_length;
	void *s, *e, *p, *mem = NULL;
	size_t size, total_size;
	int i;

	if (!a)
		return NULL;

	da = container_of(a, struct fy_dedup_allocator, a);

	if (!iov)
		goto err_out;

	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		goto err_out;

	dtd = &dt->data[dt->data_active];

	/* calculate data total size */
	total_size = 0;
	for (i = 0; i < iovcnt; i++)
		total_size += iov[i].iov_len;

	/* if it's under the dedup threshold just allocate and copy */
	if (total_size < dtd->dedup_threshold) {

		/* just pass to the parent allocator using the content tag */
		p = fy_allocator_alloc(da->parent_allocator, dt->content_tag, total_size, align);
		if (!p)
			goto err_out;

		for (i = 0, s = p; i < iovcnt; i++) {
			size = iov[i].iov_len;
			memcpy(s, iov[i].iov_base, size);
			s += size;
		}

		return p;
	}

	xxstate = da->xxstate_template;
	for (i = 0; i < iovcnt; i++)
		XXH64_update(&xxstate, iov[i].iov_base, iov[i].iov_len);
	hash = XXH64_digest(&xxstate);

	/* first check in the bloom filter */
	bloom_pos = (unsigned int)hash & dtd->bloom_filter_mask;
	assert((int)bloom_pos >= 0);
	bloom_hit = fy_id_is_used(dtd->bloom_id, dtd->bloom_id_count, (int)bloom_pos);

	bucket_pos = (unsigned int)hash & dtd->bucket_count_mask;
	assert((int)bucket_pos >= 0);

	del = dtd->buckets + bucket_pos;
	assert(del < dtd->buckets_end);

	chain_length = 0;
	if (bloom_hit) {

		if (!fy_id_is_used(dtd->buckets_in_use, dtd->bucket_id_count, (int)bucket_pos)) {
			/* this is possible when there was a delete and bloom filter is not updated */
			goto new_entry;
		}

		for (de = fy_dedup_entry_list_head(del); de; de = fy_dedup_entry_next(del, de)) {

			/* match hash */
			if (de->hash != hash) {
				/* mark that we had a collision here */
				fy_id_set_used(dtd->buckets_collision, dtd->bucket_id_count, (int)bucket_pos);
			} else {
				/* match content */
				s = de->mem;
				e = s + de->size;
				for (i = 0; i < iovcnt; i++) {
					size = iov[i].iov_len;
					if ((s + size) > e || memcmp(iov[i].iov_base, s, size))
						break;
					s += size;
				}
				/* match */
				if (i == iovcnt && s == e)
					break;
			}
			chain_length++;
		}

		if (de) {
			/* increase the reference count */
			de->refs++;

			/* update stats */
			dt->stats.dup_stores++;
			dt->stats.dup_saved += total_size;

			return de->mem;
		}
	}

new_entry:
	mem = fy_allocator_alloc(da->parent_allocator, dt->content_tag, total_size, align);
	if (!mem)
		goto err_out;

	/* verify it's aligned correctly */
	assert(((uintptr_t)mem & (align - 1)) == 0);

	de = fy_allocator_alloc(da->entries_allocator, dt->entries_tag, sizeof(*de), alignof(struct fy_dedup_entry));
	if (!de)
		goto err_out;

	de->hash = hash;
	de->refs = 1;
	de->size = total_size;
	de->mem = mem;
	mem = NULL;

	/* and copy the data */
	s = de->mem;
	for (i = 0; i < iovcnt; i++) {
		size = iov[i].iov_len;
		memcpy(s, iov[i].iov_base, size);
		s += size;
	}

	if (!fy_id_is_used(dtd->buckets_in_use, dtd->bucket_id_count, (int)bucket_pos)) {
		fy_id_set_used(dtd->buckets_in_use, dtd->bucket_id_count, (int)bucket_pos);
		fy_dedup_entry_list_init(del);
	}

	/* and add to the bucket */
	fy_dedup_entry_list_add(del, de);

	/* turn the update bit for the bloom position */
	if (!bloom_hit)
		fy_id_set_used(dtd->bloom_id, dtd->bloom_id_count, (int)bloom_pos);

	/* adjust by one bit, if we've hit the trigger */
	if (chain_length > dtd->chain_length_grow_trigger)
		fy_dedup_tag_adjust(da, dt, 1, 1);

	/* update stats */
	dt->stats.stores++;
	dt->stats.stored += total_size;

	return de->mem;

err_out:
	if (mem)
		fy_allocator_free(da->parent_allocator, dt->content_tag, mem);

	return NULL;
}

static const void *fy_dedup_store(struct fy_allocator *a, int tag, const void *data, size_t size, size_t align)
{
	struct iovec iov[1];

	if (!a)
		return NULL;

	/* just call the storev */
	iov[0].iov_base = (void *)data;
	iov[0].iov_len = size;
	return fy_dedup_storev(a, tag, iov, 1, align);
}

static void fy_dedup_release(struct fy_allocator *a, int tag, const void *data, size_t size)
{
	XXH64_state_t xxstate;
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;
	struct fy_dedup_tag_data *dtd;
	struct fy_dedup_entry *de;
	struct fy_dedup_entry_list *del;
	uint64_t hash;
	unsigned int bloom_pos, bucket_pos;

	if (!a)
		return;

	da = container_of(a, struct fy_dedup_allocator, a);

	if (!data)
		goto err_out;

	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		goto err_out;
	dtd = &dt->data[dt->data_active];

	/* if it's under the dedup threshold just free */
	if (size < dtd->dedup_threshold) {
		fy_allocator_free(da->parent_allocator, dt->content_tag, (void *)data);
		return;
	}

	xxstate = da->xxstate_template;
	XXH64_update(&xxstate, data, size);
	hash = XXH64_digest(&xxstate);

	/* first check in the bloom filter */
	bloom_pos = (unsigned int)hash & dtd->bloom_filter_mask;
	assert((int)bloom_pos >= 0);
	if (!fy_id_is_used(dtd->bloom_id, dtd->bloom_id_count, (int)bloom_pos))
		goto err_out;

	/* get the bucket */
	bucket_pos = (unsigned int)hash & dtd->bucket_count_mask;
	assert((int)bucket_pos >= 0);

	/* if the bucket is not used bail early */
	if (!fy_id_is_used(dtd->buckets_in_use, dtd->bucket_id_count, (int)bucket_pos))
		goto err_out;

	del = dtd->buckets + bucket_pos;
	assert(del < dtd->buckets_end);

	for (de = fy_dedup_entry_list_head(del); de; de = fy_dedup_entry_next(del, de)) {
		/* we don't have to check the hash, really, we have a pointer */
		if (de->mem == data)
			break;
	}

	/* no such entry found */
	if (!de)
		goto err_out;

	/* take reference */
	assert(de->refs > 0);
	de->refs--;
	if (de->refs > 0)
		goto ok_out;

	/* remove from the bucket */
	fy_dedup_entry_list_del(del, de);

	/* turn off the in use bit if last */
	if (fy_dedup_entry_list_empty(del))
		fy_id_set_free(dtd->buckets_in_use, dtd->bucket_id_count, (int)bucket_pos);

	/* XXX find whether this was the last in bloom filter cluster
	 * XXX and turn off the bloom filter bit
	 */

	/* we need to update the bloom filter */
	dt->bloom_filter_needs_update = true;

	bloom_pos = (unsigned int)de->hash & dtd->bloom_filter_mask;
	assert((int)bloom_pos >= 0);
	fy_id_set_used(dtd->bloom_update_id, dtd->bloom_id_count, (int)bloom_pos);

	/* and free content and entry */
	fy_allocator_free(da->parent_allocator, dt->content_tag, de->mem);
	fy_allocator_free(da->entries_allocator, dt->entries_tag, de);

ok_out:
	dt->stats.releases++;
	dt->stats.released += size;

err_out:
	return;
}

static int fy_dedup_get_tag(struct fy_allocator *a)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt = NULL;
	int tag;
	int id, rc;

	if (!a)
		return FY_ALLOC_TAG_ERROR;

	da = container_of(a, struct fy_dedup_allocator, a);

	/* and one from us */
	id = fy_id_alloc(da->ids, ARRAY_SIZE(da->ids));
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
	return FY_ALLOC_TAG_ERROR;
}

static void fy_dedup_release_tag(struct fy_allocator *a, int tag)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;

	if (!a)
		return;

	da = container_of(a, struct fy_dedup_allocator, a);

	dt = fy_dedup_tag_from_tag(da, tag);
	if (!dt)
		return;

	fy_dedup_tag_cleanup(da, dt);
}

static void fy_dedup_trim_tag(struct fy_allocator *a, int tag)
{
	struct fy_dedup_allocator *da;
	struct fy_dedup_tag *dt;

	if (!a)
		return;

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

	if (!a)
		return;

	da = container_of(a, struct fy_dedup_allocator, a);

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

	if (!a)
		return NULL;

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

const struct fy_allocator_ops fy_dedup_allocator_ops = {
	.setup = fy_dedup_setup,
	.cleanup = fy_dedup_cleanup,
	.create = fy_dedup_create,
	.destroy = fy_dedup_destroy,
	.dump = fy_dedup_dump,
	.alloc = fy_dedup_alloc,
	.free = fy_dedup_free,
	.update_stats = fy_dedup_update_stats,
	.store = fy_dedup_store,
	.storev = fy_dedup_storev,
	.release = fy_dedup_release,
	.get_tag = fy_dedup_get_tag,
	.release_tag = fy_dedup_release_tag,
	.trim_tag = fy_dedup_trim_tag,
	.reset_tag = fy_dedup_reset_tag,
	.get_info = fy_dedup_get_info,
};
