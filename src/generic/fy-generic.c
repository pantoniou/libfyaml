/*
 * fy-generic.c - space efficient generics
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
#include <ctype.h>
#include <errno.h>

#include <stdio.h>

/* for container_of */
#include "fy-list.h"

#include "fy-allocator.h"
#include "fy-generic.h"

// when to switch to malloc instead of alloca
#define COPY_MALLOC_CUTOFF	256

static const struct fy_generic_builder_cfg default_generic_builder_cfg = {
	.flags = FYGBCF_SCHEMA_AUTO | FYGBCF_OWNS_ALLOCATOR,
	.allocator = NULL,	/* use default */
	.shared_tag = FY_ALLOC_TAG_NONE,
};

struct fy_generic_builder *fy_generic_builder_create(const struct fy_generic_builder_cfg *cfg)
{
	struct fy_allocator *a;
	enum fy_generic_schema schema;
	struct fy_generic_builder *gb = NULL;
	bool owns_allocator;
	int shared_tag, alloc_tag;

	if (!cfg)
		cfg = &default_generic_builder_cfg;

	gb = malloc(sizeof(*gb));
	if (!gb)
		goto err_out;
	memset(gb, 0, sizeof(*gb));

	gb->cfg = *cfg;

	shared_tag = cfg->shared_tag;
	owns_allocator = !!(cfg->flags & FYGBCF_OWNS_ALLOCATOR);

	schema = (cfg->flags & FYGBCF_SCHEMA_MASK) >> FYGBCF_SCHEMA_SHIFT;
	if (schema >= FYGS_COUNT)
		goto err_out;

	a = cfg->allocator;
	if (!a) {
		a = fy_allocator_create("auto", NULL);	// let the system decide
		if (!a)
			goto err_out;
		owns_allocator = true;
	}

	alloc_tag = shared_tag;
	if (alloc_tag == FY_ALLOC_TAG_NONE) {
		alloc_tag = fy_allocator_get_tag(a);
		if (alloc_tag == FY_ALLOC_TAG_ERROR)
			goto err_out;
	}

	/* ok, update */
	gb->schema = schema;
	gb->allocator = a;
	gb->shared_tag = shared_tag;
	gb->alloc_tag = alloc_tag;
	gb->owns_allocator = owns_allocator;

	return gb;

err_out:
	fy_generic_builder_destroy(gb);
	return NULL;
}

void fy_generic_builder_destroy(struct fy_generic_builder *gb)
{
	if (!gb)
		return;

	if (gb->linear)
		free(gb->linear);

	/* if we own the allocator, just destroy it, everything is gone */
	if (gb->allocator && gb->owns_allocator)
		fy_allocator_destroy(gb->allocator);
	else if (gb->shared_tag == FY_ALLOC_TAG_NONE)
		fy_allocator_release_tag(gb->allocator, gb->alloc_tag);

	free(gb);
}

void fy_generic_builder_reset(struct fy_generic_builder *gb)
{
	if (!gb)
		return;

	if (gb->linear) {
		free(gb->linear);
		gb->linear = NULL;
	}

	if (gb->shared_tag == FY_ALLOC_TAG_NONE)
		fy_allocator_reset_tag(gb->allocator, gb->alloc_tag);
}

fy_generic fy_generic_int_create_out_of_place(struct fy_generic_builder *gb, long long val)
{
	const long long *valp;

	valp = fy_generic_builder_store(gb, &val, sizeof(val), FY_SCALAR_ALIGNOF(long long));
	if (!valp)
		return fy_invalid;
	assert(((uintptr_t)valp & FY_INPLACE_TYPE_MASK) == 0);
	return (fy_generic)valp | FY_INT_OUTPLACE_V;
}


fy_generic fy_generic_float_create_out_of_place(struct fy_generic_builder *gb, double val)
{
	const double *valp;
	valp = fy_generic_builder_store(gb, &val, sizeof(val), FY_SCALAR_ALIGNOF(double));
	if (!valp)
		return fy_invalid;
	assert(((uintptr_t)valp & FY_INPLACE_TYPE_MASK) == 0);
	return (fy_generic)valp | FY_FLOAT_OUTPLACE_V;
}

fy_generic fy_generic_string_size_create_out_of_place(struct fy_generic_builder *gb, const char *str, size_t len)
{
	uint8_t lenbuf[FYGT_SIZE_ENCODING_MAX];
	struct iovec iov[3];
	const void *s;
	void *p;

	p = fy_encode_size(lenbuf, sizeof(lenbuf), len);
	assert(p);

	iov[0].iov_base = lenbuf;
	iov[0].iov_len = (size_t)((uint8_t *)p - lenbuf) ;
	iov[1].iov_base = (void *)str;
	iov[1].iov_len = len;
	iov[2].iov_base = "\x00";	/* null terminate always */
	iov[2].iov_len = 1;

	/* strings are aligned at 8 always */
	s = fy_generic_builder_storev(gb, iov, ARRAY_SIZE(iov), 8);
	if (!s)
		return fy_invalid;

	assert(((uintptr_t)s & FY_INPLACE_TYPE_MASK) == 0);
	return (fy_generic)s | FY_STRING_OUTPLACE_V;
}


fy_generic
fy_generic_string_vcreate(struct fy_generic_builder *gb, const char *fmt, va_list ap)
{
	va_list ap2;
	char *str;
	size_t size;

	va_copy(ap2, ap);

	size = vsnprintf(NULL, 0, fmt, ap);
	if (size < 0)
		return fy_invalid;

	str = alloca(size + 1);
	size = vsnprintf(str, size + 1, fmt, ap2);
	if (size < 0)
		return fy_invalid;

	return fy_generic_string_size_create(gb, str, size);
}

fy_generic
fy_generic_string_createf(struct fy_generic_builder *gb, const char *fmt, ...)
{
	va_list ap;
	fy_generic v;

	va_start(ap, fmt);
	v = fy_generic_string_vcreate(gb, fmt, ap);
	va_end(ap);

	return v;
}

fy_generic fy_generic_internalize_out_of_place(struct fy_generic_builder *gb, fy_generic v)
{
	const struct fy_generic_sequence *seqs;
	fy_generic vi, new_v;
	const struct fy_generic_mapping *maps;
	struct iovec iov[2];
	enum fy_generic_type type;
	size_t size, i, count;
	const void *valp;
	const fy_generic *itemss;
	fy_generic *items;

	if (v == fy_invalid)
		return fy_invalid;

	/* indirects are handled here (note, aliases are indirect too) */
	if (fy_generic_is_indirect(v)) {
		struct fy_generic_indirect gi;

		fy_generic_indirect_get(v, &gi);

		gi.value = fy_generic_internalize(gb, gi.value);
		gi.anchor = fy_generic_internalize(gb, gi.anchor);
		gi.tag = fy_generic_internalize(gb, gi.tag);

		return fy_generic_indirect_create(gb, &gi);
	}

	/* if it's in place just return it */
	if (fy_generic_is_in_place(v))
		return v;

	type = fy_generic_get_type(v);

	/* if we got to here, it's going to be a copy */
	new_v = fy_invalid;
	switch (type) {

	case FYGT_SEQUENCE:
		seqs = fy_generic_resolve_collection_ptr(v);
		count = seqs->count;
		itemss = seqs->items;

		size = sizeof(*items) * count;
		if (size <= COPY_MALLOC_CUTOFF)
			items = alloca(size);
		else {
			items = malloc(size);
			if (!items)
				break;
		}

		for (i = 0; i < count; i++) {
			vi = fy_generic_internalize(gb, itemss[i]);
			if (vi == fy_invalid)
				break;
			items[i] = vi;
		}

		if (i >= count) {
			iov[0].iov_base = (void *)seqs;
			iov[0].iov_len = sizeof(*seqs);
			iov[1].iov_base = items;
			iov[1].iov_len = size;
			valp = fy_generic_builder_storev(gb, iov, ARRAY_SIZE(iov),
					FY_CONTAINER_ALIGNOF(struct fy_generic_sequence));
		} else
			valp = NULL;

		if (size > COPY_MALLOC_CUTOFF)
			free(items);

		if (!valp)
			break;

		new_v = (fy_generic)valp | FY_SEQ_V;
		break;

	case FYGT_MAPPING:
		maps = fy_generic_resolve_collection_ptr(v);
		count = maps->count * 2;
		itemss = maps->pairs;

		size = sizeof(*items) * count;
		if (size <= COPY_MALLOC_CUTOFF)
			items = alloca(size);
		else {
			items = malloc(size);
			if (!items)
				break;
		}

		/* copy both keys and values */
		for (i = 0; i < count; i++) {

			vi = fy_generic_internalize(gb, itemss[i]);
			if (vi == fy_invalid)
				break;

			items[i] = vi;
		}

		if (i >= count) {
			iov[0].iov_base = (void *)maps;
			iov[0].iov_len = sizeof(*maps);
			iov[1].iov_base = items;
			iov[1].iov_len = size,
			valp = fy_generic_builder_storev(gb, iov, ARRAY_SIZE(iov),
					FY_CONTAINER_ALIGNOF(struct fy_generic_mapping));
		} else
			valp = NULL;

		if (size > COPY_MALLOC_CUTOFF)
			free(items);

		if (!valp)
			break;

		new_v = (fy_generic)valp | FY_MAP_V;
		break;

	default:
		/* scalars just call copy */
		new_v = fy_generic_builder_copy(gb, v);
		break;
	}

	return new_v;
}

int fy_generic_internalize_array(struct fy_generic_builder *gb, size_t count, fy_generic *vp)
{
	fy_generic v;
	size_t i;

	for (i = 0; i < count; i++) {
		v = fy_generic_internalize(gb, vp[i]);
		if (v == fy_invalid)
			return -1;
		vp[i] = v;
	}
	return 0;
}

static const fy_generic *
fy_internalize_items(struct fy_generic_builder *gb, size_t count, const fy_generic *items,
		     fy_generic *items_buf, size_t buf_count, fy_generic **items_allocp)
{
	fy_generic *items_local;
	int rc;

	if (count <= buf_count)
		items_local = items_buf;
	else {
		*items_allocp = malloc(sizeof(*items_local) * count);
		if (!items_allocp)
			return NULL;
		items_local = *items_allocp;
	}

	memcpy(items_local, items, sizeof(*items_local) * count);
	rc = fy_generic_internalize_array(gb, count, items_local);
	if (rc) {
		if (*items_allocp) {
			free(*items_allocp);
			*items_allocp = NULL;
		}
		return NULL;
	}

	return items_local;
}

fy_generic fy_generic_collection_create(struct fy_generic_builder *gb, bool is_map, size_t count, const fy_generic *items, bool internalize)
{
	union {
		struct fy_generic_sequence s;
		struct fy_generic_mapping m;
	} u;
	const void *p;
	struct iovec iov[2];
	fy_generic v, *items_alloc = NULL, items_buf[64];
	size_t i;

	if (count && !items)
		return fy_invalid;

	if (is_map)
		count *= 2;

	for (i = 0; i < count; i++) {
		if (items[i] == fy_invalid)
			return fy_invalid;
	}

	if (internalize) {
		items = fy_internalize_items(gb, count, items, items_buf, ARRAY_SIZE(items_buf), &items_alloc);
		if (!items)
			return fy_invalid;
	}

	memset(&u, 0, sizeof(u));
	if (!is_map) {
		u.s.count = count;
		iov[0].iov_base = &u.s;
		iov[0].iov_len = sizeof(u.s);
	} else {
		u.m.count = count / 2;
		iov[0].iov_base = &u.m;
		iov[0].iov_len = sizeof(u.m);
	}

	iov[1].iov_base = (void *)items;
	iov[1].iov_len = count * sizeof(fy_generic);

	p = fy_generic_builder_storev(gb, iov, ARRAY_SIZE(iov), FY_CONTAINER_ALIGNOF(struct fy_generic_sequence));
	if (!p)
		goto err_out;

	v = (fy_generic)p | (!is_map ? FY_SEQ_V : FY_MAP_V);

out:
	if (items_alloc)
		free(items_alloc);

	return v;

err_out:
	v = fy_invalid;
	goto out;
}

static bool fy_collection_prepare(fy_generic col, size_t *countp, fy_generic **itemsp, bool *is_mapp)
{
	enum fy_generic_type type;
	void *p;

	type = fy_generic_get_type(col);
	if (type == FYGT_SEQUENCE)
		*is_mapp = false;
	else if (type == FYGT_MAPPING)
		*is_mapp = true;
	else
		return false;

	if (fy_generic_is_indirect(col))
		col = fy_generic_indirect_get_value(col);

	p = fy_generic_resolve_collection_ptr(col);

	if (!*is_mapp) {
		*countp = ((struct fy_generic_sequence *)p)->count;
		*itemsp = ((struct fy_generic_sequence *)p)->items;
	} else {
		*countp = ((struct fy_generic_mapping *)p)->count;
		*itemsp = ((struct fy_generic_mapping *)p)->pairs;
	}

	return true;
}

fy_generic fy_generic_collection_remove(struct fy_generic_builder *gb, fy_generic col, size_t idx, size_t count)
{
	union {
		struct fy_generic_sequence s;
		struct fy_generic_mapping m;
	} u;
	const void *p;
	fy_generic v, *old_items;
	struct iovec iov[3];
	size_t old_count;
	bool is_map;

	if (!fy_collection_prepare(col, &old_count, &old_items, &is_map))
		return fy_invalid;

	if (idx >= old_count)
		return fy_invalid;

	if (idx + count > old_count)
		count = old_count - idx;

	memset(&u, 0, sizeof(u));
	if (!is_map) {
		u.s.count = old_count - count;
		iov[0].iov_base = &u.s;
		iov[0].iov_len = sizeof(u.s);
	} else {
		u.m.count = old_count - count;
		iov[0].iov_base = &u.m;
		iov[0].iov_len = sizeof(u.m);
	}

	/* adjust by two for maps */
	if (is_map) {
		idx *= 2;
		count *= 2;
		old_count *= 2;
	}

	/* before */
	iov[1].iov_base = old_items;
	iov[1].iov_len = idx * sizeof(fy_generic);

	/* after */
	iov[2].iov_base = old_items + idx + count;
	iov[2].iov_len = (old_count - (idx + count)) * sizeof(fy_generic);

	p = fy_generic_builder_storev(gb, iov, ARRAY_SIZE(iov), FY_CONTAINER_ALIGNOF(struct fy_generic_sequence));
	if (!p)
		return fy_invalid;

	v = (fy_generic)p | (!is_map ? FY_SEQ_V : FY_MAP_V);

	return v;
}

fy_generic fy_generic_collection_insert_replace(struct fy_generic_builder *gb, fy_generic col, size_t idx, size_t count,
						const fy_generic *items, bool insert, bool internalize)
{
	union {
		struct fy_generic_sequence s;
		struct fy_generic_mapping m;
	} u;
	const void *p;
	fy_generic *items_alloc = NULL, items_buf[64], *old_items, v;
	size_t i, old_count, new_count, remain_idx, remain_count, item_count;
	struct iovec iov[4];
	bool is_map;

	/* nothing to do */
	if (!count || !items)
		return col;

	if (!fy_collection_prepare(col, &old_count, &old_items, &is_map))
		return fy_invalid;

	if (idx > old_count)
		return fy_invalid;

	item_count = count;
	if (is_map)
		item_count *= 2;

	/* check for invalids */
	for (i = 0; i < item_count; i++) {
		if (items[i] == fy_invalid)
			return fy_invalid;
	}

	if (internalize) {
		items = fy_internalize_items(gb, item_count, items, items_buf, ARRAY_SIZE(items_buf), &items_alloc);
		if (!items)
			return fy_invalid;
	}

	if (!insert) {
		/* replace */
		if (idx + count > old_count) {
			new_count = idx + count;
			remain_idx = old_count;
		} else {
			new_count = old_count;
			remain_idx = idx + count;
		}
	} else {
		/* insert */
		new_count = old_count + count;
		remain_idx = idx;
	}
	remain_count = old_count - remain_idx;

	memset(&u, 0, sizeof(u));
	if (!is_map) {
		u.s.count = new_count;
		iov[0].iov_base = &u.s;
		iov[0].iov_len = sizeof(u.s);
	} else {
		u.m.count = new_count;
		iov[0].iov_base = &u.m;
		iov[0].iov_len = sizeof(u.m);
	}

	/* adjust by two for maps */
	if (is_map) {
		idx *= 2;
		count *= 2;
		old_count *= 2;
		new_count *= 2;
		remain_idx *= 2;
		remain_count *= 2;
		item_count *= 2;
	}

	/* before */
	iov[1].iov_base = old_items;
	iov[1].iov_len = idx * sizeof(fy_generic);

	/* replacement */
	iov[2].iov_base = (void *)items;
	iov[2].iov_len = count * sizeof(fy_generic);

	/* remainder */
	iov[3].iov_base = old_items + remain_idx;
	iov[3].iov_len = remain_count * sizeof(fy_generic);

	p = fy_generic_builder_storev(gb, iov, ARRAY_SIZE(iov), FY_CONTAINER_ALIGNOF(struct fy_generic_sequence));
	if (!p)
		return fy_invalid;

	v = (fy_generic)p | (!is_map ? FY_SEQ_V : FY_MAP_V);

	if (items_alloc)
		free(items_alloc);

	return v;
}

const fy_generic *fy_generic_sequence_get_items(fy_generic seq, size_t *countp)
{
	struct fy_generic_sequence *p;

	if (!countp)
	       return NULL;

	if (!fy_generic_is_sequence(seq)) {
		*countp = 0;
		return NULL;
	}

	if (fy_generic_is_indirect(seq))
		seq = fy_generic_indirect_get_value(seq);

	p = fy_generic_resolve_collection_ptr(seq);

	*countp = p->count;
	return &p->items[0];
}

fy_generic fy_generic_sequence_get_item(fy_generic seq, size_t idx)
{
	const fy_generic *items;
	size_t count;

	if (fy_generic_get_type(seq) != FYGT_SEQUENCE)
		return fy_invalid;

	items = fy_generic_sequence_get_items(seq, &count);
	if (!items || idx >= count)
		return fy_invalid;
	return items[idx];
}

size_t fy_generic_sequence_get_item_count(fy_generic seq)
{
	struct fy_generic_sequence *p;

	if (fy_generic_get_type(seq) != FYGT_SEQUENCE)
		return (size_t)-1;

	if (fy_generic_is_indirect(seq))
		seq = fy_generic_indirect_get_value(seq);

	p = fy_generic_resolve_collection_ptr(seq);
	return p->count;
}

fy_generic fy_generic_sequence_create_i(struct fy_generic_builder *gb, bool internalize,
					size_t count, const fy_generic *items)
{
	struct fy_generic_sequence s;
	const struct fy_generic_sequence *p;
	struct iovec iov[2];
	fy_generic v, *items_alloc = NULL, items_buf[64];
	size_t i;

	if (count && !items)
		return fy_invalid;

	for (i = 0; i < count; i++) {
		if (items[i] == fy_invalid)
			return fy_invalid;
	}

	if (internalize) {
		items = fy_internalize_items(gb, count, items, items_buf, ARRAY_SIZE(items_buf), &items_alloc);
		if (!items)
			return fy_invalid;
	}

	memset(&s, 0, sizeof(s));
	s.count = count;

	iov[0].iov_base = &s;
	iov[0].iov_len = sizeof(s);
	iov[1].iov_base = (void *)items;
	iov[1].iov_len = count * sizeof(*items);

	p = fy_generic_builder_storev(gb, iov, ARRAY_SIZE(iov), FY_CONTAINER_ALIGNOF(struct fy_generic_sequence));
	if (!p)
		goto err_out;

	v = (fy_generic)p | FY_SEQ_V;

out:
	if (items_alloc)
		free(items_alloc);

	return v;

err_out:
	v = fy_invalid;
	goto out;
}

fy_generic fy_generic_sequence_create(struct fy_generic_builder *gb, size_t count, const fy_generic *items)
{
	return fy_generic_collection_create(gb, false, count, items, true);
}

fy_generic fy_generic_sequence_remove(struct fy_generic_builder *gb, fy_generic seq, size_t idx, size_t count)
{
	return fy_generic_collection_remove(gb, seq, idx, count);
}

fy_generic fy_generic_sequence_insert_replace_i(struct fy_generic_builder *gb, bool insert, bool internalize,
						fy_generic seq, size_t idx, size_t count, const fy_generic *items)
{
	return fy_generic_collection_insert_replace(gb, seq, idx, count, items, insert, internalize);
}

fy_generic fy_generic_sequence_insert_replace(struct fy_generic_builder *gb, bool insert,
					      fy_generic seq, size_t idx, size_t count, const fy_generic *items)
{
	return fy_generic_sequence_insert_replace_i(gb, insert, true, seq, idx, count, items);
}

fy_generic fy_generic_sequence_insert_i(struct fy_generic_builder *gb, bool internalize,
					fy_generic seq, size_t idx, size_t count, const fy_generic *items)
{
	return fy_generic_sequence_insert_replace_i(gb, true, internalize, seq, idx, count, items);
}

fy_generic fy_generic_sequence_insert(struct fy_generic_builder *gb, fy_generic seq, size_t idx, size_t count,
				      const fy_generic *items)
{
	return fy_generic_sequence_insert_i(gb, false, seq, idx, count, items);
}

fy_generic fy_generic_sequence_replace_i(struct fy_generic_builder *gb, bool internalize,
					 fy_generic seq, size_t idx, size_t count, const fy_generic *items)
{
	return fy_generic_sequence_insert_replace_i(gb, false, internalize, seq, idx, count, items);
}

fy_generic fy_generic_sequence_replace(struct fy_generic_builder *gb, fy_generic seq, size_t idx, size_t count,
				      const fy_generic *items)
{
	return fy_generic_sequence_replace_i(gb, false, seq, idx, count, items);
}

fy_generic fy_generic_sequence_append_i(struct fy_generic_builder *gb, bool internalize,
					fy_generic seq, size_t count, const fy_generic *items)
{
	size_t idx;

	idx = fy_generic_sequence_get_item_count(seq);
	return fy_generic_sequence_insert_replace_i(gb, true, internalize, seq, idx, count, items);
}

fy_generic fy_generic_sequence_append(struct fy_generic_builder *gb, fy_generic seq, size_t count, const fy_generic *items)
{
	return fy_generic_sequence_append_i(gb, true, seq, count, items);
}

fy_generic fy_generic_sequence_set_item_i(struct fy_generic_builder *gb, bool internalize,
					  fy_generic seq, size_t idx, fy_generic item)
{
	if (seq == fy_invalid || item == fy_invalid)
		return fy_invalid;

	return fy_generic_mapping_replace_i(gb, internalize, seq, idx, 1, &item);
}

fy_generic fy_generic_sequence_set_item(struct fy_generic_builder *gb,
					  fy_generic seq, size_t idx, fy_generic item)
{
	return fy_generic_sequence_set_item_i(gb, true, seq, idx, item);
}

const fy_generic *fy_generic_mapping_get_pairs(fy_generic map, size_t *countp)
{
	struct fy_generic_mapping *p;

	if (!countp)
		return NULL;

	if (!fy_generic_is_mapping(map)) {
		*countp = 0;
		return NULL;
	}

	if (fy_generic_is_indirect(map))
		map = fy_generic_indirect_get_value(map);

	assert(fy_generic_get_type(map) == FYGT_MAPPING);

	p = fy_generic_resolve_collection_ptr(map);

	*countp = p->count;
	return p->pairs;
}

size_t fy_generic_mapping_get_pair_count(fy_generic map)
{
	struct fy_generic_mapping *p;

	if (fy_generic_get_type(map) != FYGT_MAPPING)
		return (size_t)-1;

	if (fy_generic_is_indirect(map))
		map = fy_generic_indirect_get_value(map);

	p = fy_generic_resolve_collection_ptr(map);
	return p->count;
}

fy_generic fy_generic_mapping_create_i(struct fy_generic_builder *gb, bool internalize,
				       size_t count, const fy_generic *pairs)
{
	return fy_generic_collection_create(gb, true, count, pairs, internalize);
}

fy_generic fy_generic_mapping_create(struct fy_generic_builder *gb, size_t count, const fy_generic *pairs)
{
	return fy_generic_mapping_create_i(gb, true, count, pairs);
}

fy_generic fy_generic_mapping_remove(struct fy_generic_builder *gb, fy_generic map, size_t idx, size_t count)
{
	return fy_generic_collection_remove(gb, map, idx, count);
}

fy_generic fy_generic_mapping_insert_replace_i(struct fy_generic_builder *gb, bool insert, bool internalize,
						fy_generic map, size_t idx, size_t count, const fy_generic *pairs)
{
	return fy_generic_collection_insert_replace(gb, map, idx, count, pairs, insert, internalize);
}

fy_generic fy_generic_mapping_insert_replace(struct fy_generic_builder *gb, bool insert,
					      fy_generic map, size_t idx, size_t count, const fy_generic *pairs)
{
	return fy_generic_mapping_insert_replace_i(gb, insert, true, map, idx, count, pairs);
}

fy_generic fy_generic_mapping_insert_i(struct fy_generic_builder *gb, bool internalize,
					fy_generic map, size_t idx, size_t count, const fy_generic *pairs)
{
	return fy_generic_mapping_insert_replace_i(gb, true, internalize, map, idx, count, pairs);
}

fy_generic fy_generic_mapping_insert(struct fy_generic_builder *gb, fy_generic map, size_t idx, size_t count,
				      const fy_generic *pairs)
{
	return fy_generic_mapping_insert_i(gb, false, map, idx, count, pairs);
}

fy_generic fy_generic_mapping_replace_i(struct fy_generic_builder *gb, bool internalize,
					 fy_generic map, size_t idx, size_t count, const fy_generic *pairs)
{
	return fy_generic_mapping_insert_replace_i(gb, false, internalize, map, idx, count, pairs);
}

fy_generic fy_generic_mapping_replace(struct fy_generic_builder *gb, fy_generic map, size_t idx, size_t count,
				      const fy_generic *pairs)
{
	return fy_generic_mapping_replace_i(gb, false, map, idx, count, pairs);
}

fy_generic fy_generic_mapping_append_i(struct fy_generic_builder *gb, bool internalize,
					fy_generic map, size_t count, const fy_generic *pairs)
{
	size_t idx;

	idx = fy_generic_mapping_get_pair_count(map);
	return fy_generic_mapping_insert_replace_i(gb, true, internalize, map, idx, count, pairs);
}

fy_generic fy_generic_mapping_append(struct fy_generic_builder *gb, fy_generic map, size_t count, const fy_generic *pairs)
{
	return fy_generic_mapping_append_i(gb, true, map, count, pairs);
}

const fy_generic fy_generic_mapping_get_value_index(fy_generic map, fy_generic key, size_t *idxp)
{
	struct fy_generic_mapping *p;
	const fy_generic *pair;
	size_t i;

	if (fy_generic_is_indirect(map))
		map = fy_generic_indirect_get_value(map);
	p = fy_generic_resolve_collection_ptr(map);
	pair = p->pairs;
	for (i = 0; i < p->count; i++, pair += 2) {
		if (fy_generic_compare(key, pair[0]) == 0) {
			if (idxp)
				*idxp = i;
			return pair[1];
		}
	}
	if (idxp)
		*idxp = (size_t)-1;
	return fy_invalid;
}

fy_generic fy_generic_mapping_set_value_i(struct fy_generic_builder *gb, bool internalize,
					  fy_generic map, fy_generic key, fy_generic value)
{
	size_t idx;
	fy_generic old_value;

	if (map == fy_invalid || key == fy_invalid || value == fy_invalid)
		return fy_invalid;

	old_value = fy_generic_mapping_get_value_index(map, key, &idx);

	/* found? replace */
	if (old_value != fy_invalid)
		return fy_generic_mapping_replace_i(gb, internalize, map, idx, 1, (fy_generic[]){ key, value });

	/* not found? append */
	idx = fy_generic_mapping_get_pair_count(map);
	return fy_generic_mapping_insert_i(gb, internalize, map, idx, 1, (fy_generic[]){ key, value });
}

fy_generic fy_generic_mapping_set_value(struct fy_generic_builder *gb,
					  fy_generic map, fy_generic key, fy_generic value)
{
	return fy_generic_mapping_set_value_i(gb, true, map, key, value);
}

const char *fy_generic_get_alias_size(const fy_generic *vp, size_t *lenp)
{
	const fy_generic *p;
	uintptr_t flags;

	if (!vp || !lenp)
		return NULL;

	p = fy_generic_resolve_ptr(*vp);

	flags = *p++;
	if (flags & FYGIF_VALUE)
		p++;
	if (!(flags & FYGIF_ANCHOR))
		return NULL;
	return fy_generic_get_string_size(p, lenp);
}

const char *fy_generic_get_alias(const fy_generic *vp)
{
	const fy_generic *p;
	uintptr_t flags;

	if (!vp)
		return NULL;

	p = fy_generic_resolve_ptr(*vp);

	flags = *p++;
	if (flags & FYGIF_VALUE)
		p++;
	if (!(flags & FYGIF_ANCHOR))
		return NULL;
	return fy_generic_get_string(p);
}

fy_generic fy_generic_indirect_create(struct fy_generic_builder *gb, const struct fy_generic_indirect *gi)
{
	const void *p;
	struct iovec iov[4];
	size_t cnt;
	uintptr_t flags;

	cnt = 0;

	flags = 0;
	if (gi->value != fy_invalid)
		flags |= FYGIF_VALUE;
	if (gi->anchor != fy_null && gi->anchor != fy_invalid)
		flags |= FYGIF_ANCHOR;
	if (gi->tag != fy_null && gi->tag != fy_invalid)
		flags |= FYGIF_TAG;
	iov[cnt].iov_base = &flags;
	iov[cnt++].iov_len = sizeof(flags);
	if (flags & FYGIF_VALUE) {
		iov[cnt].iov_base = (void *)&gi->value;
		iov[cnt++].iov_len = sizeof(gi->value);
	}
	if (flags & FYGIF_ANCHOR) {
		iov[cnt].iov_base = (void *)&gi->anchor;
		iov[cnt++].iov_len = sizeof(gi->anchor);
	}
	if (flags & FYGIF_TAG) {
		iov[cnt].iov_base = (void *)&gi->tag;
		iov[cnt++].iov_len = sizeof(gi->tag);
	}

	p = fy_generic_builder_storev(gb, iov, cnt, FY_SCALAR_ALIGNOF(fy_generic));	/* must be at least 8 */
	if (!p)
		return fy_invalid;

	return (fy_generic)p | FY_INDIRECT_V;
}

fy_generic fy_generic_alias_create(struct fy_generic_builder *gb, fy_generic anchor)
{
	struct fy_generic_indirect gi = {
		.value = fy_invalid,
		.anchor = anchor,
		.tag = fy_invalid,
	};

	return fy_generic_indirect_create(gb, &gi);
}

fy_generic fy_generic_create_scalar_from_text(struct fy_generic_builder *gb, const char *text, size_t len, enum fy_generic_type force_type)
{
	enum fy_generic_schema schema;
	fy_generic v;
	const char *s, *e;
	const char *dec, *fract, *exp;
	char sign, exp_sign;
	size_t dec_count, fract_count, exp_count;
	int base;
	long long lv;
	double dv;
	char *t;
	bool is_json;

	v = fy_invalid;

	schema = gb->schema;

	/* force a string? okie-dokie */
	if (force_type == FYGT_STRING)
		goto do_string;

	/* more than 4K it is definitely a string */
	if (len > 4096)
		goto do_string;

	/* default schema is yaml 1.2 core */
	if (schema == FYGS_AUTO)
		schema = FYGS_YAML1_2_CORE;

	/* first stab at direct matches */
	switch (schema) {
	case FYGS_YAML1_2_FAILSAFE:
		goto do_string;

	case FYGS_YAML1_2_JSON:
	case FYGS_JSON:
		if (len == 4) {
			if (!memcmp(text, "null", 4)) {
				v = fy_null;
				break;
			}
			if (!memcmp(text, "true", 4)) {
				v = fy_true;
				break;
			}
		} else if (len == 5) {
			if (!memcmp(text, "false", 5)) {
				v = fy_false;
				break;
			}
		}
		break;

	case FYGS_YAML1_2_CORE:

		if (len == 0 || (len == 1 && *text == '~')) {
			v = fy_null;
			break;
		}

		if (len == 4) {
		    if (!memcmp(text, "null", 4) || !memcmp(text, "Null", 4) || !memcmp(text, "NULL", 4)) {
			    v = fy_null;
			    break;
		    }
		    if (!memcmp(text, "true", 4) || !memcmp(text, "True", 4) || !memcmp(text, "TRUE", 4)) {
			    v = fy_true;
			    break;
		    }
		    if (!memcmp(text, ".inf", 4) || !memcmp(text, ".Inf", 4) || !memcmp(text, ".INF", 4)) {
				v = fy_generic_float_create(gb, INFINITY);
				break;
		    }
		    if (!memcmp(text, ".nan", 4) || !memcmp(text, ".Nan", 4) || !memcmp(text, ".NAN", 4)) {
				v = fy_generic_float_create(gb, NAN);
				break;
		    }
		}
		if (len == 5) {
		    if (!memcmp(text, "false", 5) || !memcmp(text, "False", 5) || !memcmp(text, "FALSE", 5)) {
			    v = fy_false;
			    break;
		    }
		    if (!memcmp(text, "+.inf", 5) || !memcmp(text, "+.Inf", 5) || !memcmp(text, "+.INF", 5)) {
				v = fy_generic_float_create(gb, INFINITY);
				break;
		    }
		    if (!memcmp(text, "-.inf", 5) || !memcmp(text, "-.Inf", 5) || !memcmp(text, "-.INF", 5)) {
				v = fy_generic_float_create(gb, -INFINITY);
				break;
		    }
		}

		break;

	case FYGS_YAML1_1:

		if (len == 0 || (len == 1 && *text == '~')) {
			v = fy_null;
			break;
		}

		if (len == 1) {
			if (*text == 'y' || *text == 'Y') {
				v = fy_true;
				break;
			}
			if (*text == 'n' || *text == 'N') {
				v = fy_false;
				break;
			}
		}

		if (len == 2) {
			if (!memcmp(text, "on", 2) || !memcmp(text, "On", 2) || !memcmp(text, "ON", 2)) {
				v = fy_true;
				break;
			}
		}

		if (len == 3) {
			if (!memcmp(text, "off", 3) || !memcmp(text, "Off", 3) || !memcmp(text, "OFF", 3)) {
				v = fy_false;
				break;
			}
		}

		if (len == 4) {
		    if (!memcmp(text, "null", 4) || !memcmp(text, "Null", 4) || !memcmp(text, "NULL", 4)) {
			    v = fy_null;
			    break;
		    }
		    if (!memcmp(text, "true", 4) || !memcmp(text, "True", 4) || !memcmp(text, "TRUE", 4)) {
			    v = fy_true;
			    break;
		    }
		    if (!memcmp(text, ".inf", 4) || !memcmp(text, ".Inf", 4) || !memcmp(text, ".INF", 4)) {
				v = fy_generic_float_create(gb, INFINITY);
				break;
		    }
		    if (!memcmp(text, ".nan", 4) || !memcmp(text, ".Nan", 4) || !memcmp(text, ".NAN", 4)) {
				v = fy_generic_float_create(gb, NAN);
				break;
		    }
		}
		if (len == 5) {
		    if (!memcmp(text, "false", 5) || !memcmp(text, "False", 5) || !memcmp(text, "FALSE", 5)) {
			    v = fy_false;
			    break;
		    }
		    if (!memcmp(text, "+.inf", 5) || !memcmp(text, "+.Inf", 5) || !memcmp(text, "+.INF", 5)) {
				v = fy_generic_float_create(gb, INFINITY);
				break;
		    }
		    if (!memcmp(text, "-.inf", 5) || !memcmp(text, "-.Inf", 5) || !memcmp(text, "-.INF", 5)) {
				v = fy_generic_float_create(gb, -INFINITY);
				break;
		    }
		}

		break;


	default:
		break;
	}

	if (v != fy_invalid)
		goto do_check_cast;

	s = text;
	e = s + len;

	dec = fract = exp = NULL;
	dec_count = fract_count = exp_count = 0;
	sign = exp_sign = ' ';
	lv = 0;
	base = 10;

	is_json = fy_generic_schema_is_json(schema);

	/* skip over sign */
	if (s < e && (*s == '-' || (!is_json && *s == '+')))
		sign = *s++;

	(void)sign;

	dec = s;
	if (s < e && *s == '0') {
		s++;
		if (!is_json) {
			if (*s == 'x') {
				base = 16;
				s++;
			} else if (*s == 'o') {
				base = 8;
				s++;
			}
		} else {
			/* json does not allow a 0000... form */
			if (*s == '0')
				goto do_string;
		}
	}

	/* consume digits */
	if (base == 16) {
		while (s < e && isxdigit(*s))
			s++;
	} else if (base == 10) {
		while (s < e && isdigit(*s))
			s++;
	} else if (base == 8) {
		while (s < e && (*s >= 0 && *s <= 7))
			s++;
	}
	dec_count = s - dec;

	/* extract the fractional part */
	if (s < e && *s == '.') {
		if (base != 10)
			goto do_string;
		fract = ++s;
		while (s < e && isdigit(*s))
			s++;
		fract_count = s - fract;
	}

	/* extract the exponent part */ 
	if (s < e && (*s == 'e' || *s == 'E')) {
		if (base != 10)
			goto do_string;
		s++;
		if (s < e && (*s == '+' || *s == '-'))
			exp_sign = *s++;
		exp = s;
		while (s < e && isdigit(*s))
			s++;
		exp_count = s - exp;
	}

	/* not fully consumed? then just a string */
	if (s < e)
		goto do_string;

	// fprintf(stderr, "\n base=%d sign=%c dec=%.*s fract=%.*s exp_sign=%c exp=%.*s\n",
	//		base, sign, (int)dec_count, dec, (int)fract_count, fract, exp_sign, (int)exp_count, exp);

	/* all schemas require decimal digits */
	if (!dec || !dec_count)
		goto do_string;

	t = alloca(len + 1);
	memcpy(t, text, len);
	t[len] = '\0';

	/* it is an integer */
	if (!fract_count && !exp_count) {
		errno = 0;
		lv = strtoll(t, NULL, base);
		if (errno == ERANGE)
			goto do_string;

		v = fy_generic_int_create(gb, lv);
		goto do_check_cast;
	} else {
		errno = 0;
		dv = strtod(t, NULL);
		if (errno == ERANGE)
			goto do_string;

		v = fy_generic_float_create(gb, dv);
		goto do_check_cast;
	}

do_string:
	/* everything tried, just a string */
	v = fy_generic_string_size_create(gb, text, len);

do_check_cast:
	if (force_type != FYGT_INVALID && fy_generic_get_type(v) != force_type) {
		fprintf(stderr, "cast failed\n");
		return fy_invalid;
	}
	return v;
}

int fy_generic_sequence_compare(fy_generic seqa, fy_generic seqb)
{
	size_t i, counta, countb;
	const fy_generic *itemsa, *itemsb;
	int ret;

	if (seqa == seqb)
		return 0;

	itemsa = fy_generic_sequence_get_items(seqa, &counta);
	itemsb = fy_generic_sequence_get_items(seqb, &countb);

	if (!itemsa || !itemsb || counta != countb)
		goto out;

	/* empty? just fine */
	if (counta == 0)
		return 0;

	/* try to cheat by comparing contents */
	ret = memcmp(itemsa, itemsb, counta * sizeof(*itemsa));
	if (!ret)
		return 0;	/* great! binary match */

	/* have to do it the hard way */
	for (i = 0; i < counta; i++) {
		ret = fy_generic_compare(itemsa[i], itemsb[i]);
		if (ret)
			goto out;
	}

	/* exhaustive check */
	return 0;
out:
	return seqa > seqb ? 1 : -1;	/* keep order, but it's just address based */
}

int fy_generic_mapping_compare(fy_generic mapa, fy_generic mapb)
{
	size_t i, counta, countb;
	const fy_generic *pairsa, *pairsb;
	fy_generic key, vala, valb;
	int ret;

	if (mapa == mapb)
		return 0;

	pairsa = fy_generic_mapping_get_pairs(mapa, &counta);
	pairsb = fy_generic_mapping_get_pairs(mapb, &countb);

	if (!pairsa || !pairsb || counta != countb)
		goto out;

	/* empty? just fine */
	if (counta == 0)
		return 0;

	/* try to cheat by comparing contents */
	ret = memcmp(pairsa, pairsb, counta * 2 * sizeof(*pairsa));
	if (!ret)
		return 0;	/* great! binary match */

	/* have to do it the hard way */
	for (i = 0; i < counta * 2; i++) {
		key = pairsa[i * 2];
		vala = pairsa[i * 2 + 1];

		/* find if the key exists in the other mapping */
		valb = fy_generic_mapping_get_value(mapa, key);
		if (valb == fy_invalid)
			goto out;

		/* compare values */
		ret = fy_generic_compare(vala, valb);
		if (ret)
			goto out;
	}

	/* all the keys value pairs match */

	return 0;
out:
	return mapa > mapb ? 1 : -1;	/* keep order, but it's just address based */
}

static inline int fy_generic_bool_compare(fy_generic a, fy_generic b)
{
	int ba, bb;

	ba = (int)fy_generic_get_bool(a);
	bb = (int)fy_generic_get_bool(b);
	return ba > bb ?  1 :
	       ba < bb ? -1 : 0;
}

static inline int fy_generic_int_compare(fy_generic a, fy_generic b)
{
	long long ia, ib;

	ia = fy_generic_get_int(a);
	ib = fy_generic_get_int(b);
	return ia > ib ?  1 :
	       ia < ib ? -1 : 0;
}

static inline int fy_generic_float_compare(fy_generic a, fy_generic b)
{
	double da, db;

	da = fy_generic_get_float(a);
	db = fy_generic_get_float(b);
	return da > db ?  1 :
	       da < db ? -1 : 0;
}

static inline int fy_generic_string_compare(fy_generic a, fy_generic b)
{
	const char *sa, *sb;
	size_t sza = 0, szb = 0;
	int ret;

	sa = fy_generic_get_string_size(&a, &sza);
	sb = fy_generic_get_string_size(&b, &szb);

	ret = memcmp(sa, sb, sza > szb ? szb : sza);

	if (!ret && sza != szb)
		ret = 1;
	return ret;
}

static inline int fy_generic_alias_compare(fy_generic a, fy_generic b)
{
	fy_generic aa, ab;
	const char *sa, *sb;
	size_t sza = 0, szb = 0;
	int ret;

	aa = fy_generic_indirect_get_anchor(a);
	ab = fy_generic_indirect_get_anchor(b);

	sa = fy_generic_get_string_size(&aa, &sza);
	sb = fy_generic_get_string_size(&ab, &szb);

	ret = memcmp(sa, sb, sza > szb ? szb : sza);

	if (!ret && sza != szb)
		ret = 1;
	return ret;
}

int fy_generic_compare_out_of_place(fy_generic a, fy_generic b)
{
	enum fy_generic_type at, bt;

	/* invalids are always non-matching */
	if (a == fy_invalid || b == fy_invalid)
		return -1;

	/* equals? nice - should work for null, bool, in place int, float and strings  */
	/* also for anything that's a pointer */
	if (a == b)
		return 0;

	at = fy_generic_get_type(a);
	bt = fy_generic_get_type(b);

	/* invalid types, or differing types do not match */
	if (at != bt)
		return -1;

	switch (fy_generic_get_type(a)) {
	case FYGT_NULL:
		return 0;	/* two nulls are always equal to each other */

	case FYGT_BOOL:
		return fy_generic_bool_compare(a, b);

	case FYGT_INT:
		return fy_generic_int_compare(a, b);

	case FYGT_FLOAT:
		return fy_generic_float_compare(a, b);

	case FYGT_STRING:
		return fy_generic_string_compare(a, b);

	case FYGT_SEQUENCE:
		return fy_generic_sequence_compare(a, b);

	case FYGT_MAPPING:
		return fy_generic_mapping_compare(a, b);

	case FYGT_ALIAS:
		return fy_generic_alias_compare(a, b);

	default:
		FY_IMPOSSIBLE_ABORT();
	}
}

fy_generic fy_generic_builder_copy_out_of_place(struct fy_generic_builder *gb, fy_generic v)
{
	const struct fy_generic_sequence *seqs;
	fy_generic vi, new_v;
	const struct fy_generic_mapping *maps;
	struct iovec iov[2];
	enum fy_generic_type type;
	size_t size, len, i, count;
	const void *valp;
	const uint8_t *p, *str;
	const fy_generic *itemss;
	fy_generic *items;

	if (v == fy_invalid)
		return fy_invalid;

	/* indirects are handled here (note, aliases are indirect too) */
	if (fy_generic_is_indirect(v)) {
		struct fy_generic_indirect gi;

		fy_generic_indirect_get(v, &gi);

		gi.value = fy_generic_builder_copy(gb, gi.value);
		gi.anchor = fy_generic_builder_copy(gb, gi.anchor);
		gi.tag = fy_generic_builder_copy(gb, gi.tag);

		return fy_generic_indirect_create(gb, &gi);
	}

	/* if it's in place just return it */
	if (fy_generic_is_in_place(v))
		return v;

	type = fy_generic_get_type(v);
	if (type == FYGT_NULL || type == FYGT_BOOL)
		return v;

	/* if we got to here, it's going to be a copy */
	new_v = fy_invalid;
	switch (type) {
	case FYGT_INT:
		valp = fy_generic_builder_store(gb, fy_generic_resolve_ptr(v),
				sizeof(long long), FY_SCALAR_ALIGNOF(long long));
		if (!valp)
			break;

		new_v = (fy_generic)valp | FY_INT_OUTPLACE_V;
		break;

	case FYGT_FLOAT:
		valp = fy_generic_builder_store(gb, fy_generic_resolve_ptr(v),
				sizeof(double), FY_SCALAR_ALIGNOF(double));
		if (!valp)
			break;

		new_v = (fy_generic)valp | FY_FLOAT_OUTPLACE_V;
		break;

	case FYGT_STRING:
		p = fy_generic_resolve_ptr(v);
		str = fy_decode_size(p, FYGT_SIZE_ENCODING_MAX, &len);
		if (!str)
			break;

		size = (size_t)(str - p) + len;
		valp = fy_generic_builder_store(gb, p, size + 1, 8);
		if (!valp)
			break;

		new_v = (fy_generic)valp | FY_STRING_OUTPLACE_V;
		break;

	case FYGT_SEQUENCE:
		seqs = fy_generic_resolve_collection_ptr(v);
		count = seqs->count;
		itemss = seqs->items;

		size = sizeof(*items) * count;
		if (size <= COPY_MALLOC_CUTOFF)
			items = alloca(size);
		else {
			items = malloc(size);
			if (!items)
				break;
		}

		for (i = 0; i < count; i++) {
			vi = fy_generic_builder_copy(gb, itemss[i]);
			if (vi == fy_invalid)
				break;
			items[i] = vi;
		}

		if (i >= count) {
			iov[0].iov_base = (void *)seqs;
			iov[0].iov_len = sizeof(*seqs);
			iov[1].iov_base = items;
			iov[1].iov_len = size;
			valp = fy_generic_builder_storev(gb, iov, ARRAY_SIZE(iov),
					FY_CONTAINER_ALIGNOF(struct fy_generic_sequence));
		} else
			valp = NULL;

		if (size > COPY_MALLOC_CUTOFF)
			free(items);

		if (!valp)
			break;

		new_v = (fy_generic)valp | FY_SEQ_V;
		break;

	case FYGT_MAPPING:
		maps = fy_generic_resolve_collection_ptr(v);
		count = maps->count * 2;
		itemss = maps->pairs;

		size = sizeof(*items) * count;
		if (size <= COPY_MALLOC_CUTOFF)
			items = alloca(size);
		else {
			items = malloc(size);
			if (!items)
				break;
		}

		/* copy both keys and values */
		for (i = 0; i < count; i++) {

			vi = fy_generic_builder_copy(gb, itemss[i]);
			if (vi == fy_invalid)
				break;

			items[i] = vi;
		}

		if (i >= count) {
			iov[0].iov_base = (void *)maps;
			iov[0].iov_len = sizeof(*maps);
			iov[1].iov_base = items;
			iov[1].iov_len = size,
			valp = fy_generic_builder_storev(gb, iov, ARRAY_SIZE(iov),
					FY_CONTAINER_ALIGNOF(struct fy_generic_mapping));
		} else
			valp = NULL;

		if (size > COPY_MALLOC_CUTOFF)
			free(items);

		if (!valp)
			break;

		new_v = (fy_generic)valp | FY_MAP_V;
		break;

	default:
		break;
	}

	/* there must have been a change */
	assert(new_v != v);

	return new_v;
}

fy_generic fy_generic_relocate(void *start, void *end, fy_generic v, ptrdiff_t d)
{
	void *p;
	struct fy_generic_indirect *gi;
	struct fy_generic_sequence *seq;
	struct fy_generic_mapping *map;
	fy_generic *items, *pairs;
	size_t i, count;

	/* the delta can't have those bits */
	assert((d & FY_INPLACE_TYPE_MASK) == 0);

	/* no relocation needed */
	if (d == 0)
		return v;

	/* if it's indirect, resolve the internals */
	if (fy_generic_is_indirect(v)) {

		/* check if already relocated */
		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v = fy_generic_relocate_ptr(v, d) | FY_INDIRECT_V;
		gi = fy_generic_resolve_ptr(v);
		gi->value = fy_generic_relocate(start, end, gi->value, d);
		gi->anchor = fy_generic_relocate(start, end, gi->anchor, d);
		gi->tag = fy_generic_relocate(start, end, gi->tag, d);
		return v;
	}

	/* if it's not indirect, it might be one of the in place formats */
	switch (fy_generic_get_type(v)) {
	case FYGT_NULL:
	case FYGT_BOOL:
		return v;

	case FYGT_INT:
		if ((v & FY_INPLACE_TYPE_MASK) == FY_INT_INPLACE_V)
			return v;

		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v = fy_generic_relocate_ptr(v, d) | FY_INT_OUTPLACE_V;
		break;

	case FYGT_FLOAT:
		if ((v & FY_INPLACE_TYPE_MASK) == FY_FLOAT_INPLACE_V)
			return v;

		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v = fy_generic_relocate_ptr(v, d) | FY_FLOAT_OUTPLACE_V;
		break;

	case FYGT_STRING:
		if ((v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V)
			return v;

		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v = fy_generic_relocate_ptr(v, d) | FY_STRING_OUTPLACE_V;
		break;

	case FYGT_SEQUENCE:
		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v = fy_generic_relocate_collection_ptr(v, d) | FY_SEQ_V;
		seq = fy_generic_resolve_collection_ptr(v);
		count = seq->count;
		items = (fy_generic *)seq->items;
		for (i = 0; i < count; i++)
			items[i] = fy_generic_relocate(start, end, items[i], d);
		break;

	case FYGT_MAPPING:
		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v = fy_generic_relocate_collection_ptr(v, d) | FY_MAP_V;
		map = fy_generic_resolve_collection_ptr(v);
		count = map->count * 2;
		pairs = (fy_generic *)map->pairs;
		for (i = 0; i < count; i++)
			pairs[i] = fy_generic_relocate(start, end, pairs[i], d);
		break;

	default:
		FY_IMPOSSIBLE_ABORT();
	}

	return v;
}

static const char *generic_schema_txt[FYGS_COUNT] = {
	[FYGS_AUTO]		= "auto",
	[FYGS_YAML1_2_FAILSAFE]	= "yaml1.2-failsafe",
	[FYGS_YAML1_2_CORE]	= "yaml1.2-core",
	[FYGS_YAML1_2_JSON]	= "yaml1.2-json",
	[FYGS_YAML1_1]		= "yaml1.1",
	[FYGS_JSON]		= "json",
};

const char *fy_generic_schema_get_text(enum fy_generic_schema schema)
{
	const char *txt;

	txt = (unsigned int)schema < ARRAY_SIZE(generic_schema_txt) ? generic_schema_txt[schema] : NULL;
	return txt ? txt : "";
}

enum fy_generic_schema fy_generic_builder_get_schema(struct fy_generic_builder *gb)
{
	return gb ? gb->schema : FYGS_AUTO;
}

void fy_generic_builder_set_schema(struct fy_generic_builder *gb, enum fy_generic_schema schema)
{
	if ((unsigned int)schema >= FYGS_COUNT)
		return;
	gb->schema = schema;
}

int fy_generic_builder_set_schema_from_parser_mode(struct fy_generic_builder *gb, enum fy_parser_mode parser_mode)
{
	enum fy_generic_schema schema;

	if (!gb)
		return -1;

	schema = fy_generic_builder_get_schema(gb);
	switch (parser_mode) {
	case fypm_yaml_1_1:
		schema = FYGS_YAML1_1;
		break;
	case fypm_yaml_1_2:
	case fypm_yaml_1_3:
		schema = FYGS_YAML1_2_CORE;
		break;
	case fypm_json:
		schema = FYGS_JSON;
		break;
	case fypm_invalid:
	case fypm_none:
	default:
		return -1;

	}

	fy_generic_builder_set_schema(gb, schema);
	return 0;
}
