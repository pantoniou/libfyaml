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
#include "fy-utf8.h"

#include "fy-allocator.h"
#include "fy-generic.h"
#include "fy-generic-encoder.h"

// when to switch to malloc instead of alloca
#define COPY_MALLOC_CUTOFF	256

extern __thread struct fy_generic_builder *fy_current_gb;

static const struct fy_generic_builder_cfg default_generic_builder_cfg = {
	.flags = FYGBCF_SCHEMA_AUTO | FYGBCF_OWNS_ALLOCATOR,
	.allocator = NULL,	/* use default */
	.shared_tag = FY_ALLOC_TAG_NONE,
};

void fy_generic_builder_cleanup(struct fy_generic_builder *gb)
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
}

int fy_generic_builder_setup(struct fy_generic_builder *gb, const struct fy_generic_builder_cfg *cfg)
{
	struct fy_allocator *a;
	enum fy_generic_schema schema;
	bool owns_allocator;
	int shared_tag, alloc_tag;

	if (!gb)
		return -1;

	if (!cfg)
		cfg = &default_generic_builder_cfg;

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

	return 0;

err_out:
	fy_generic_builder_cleanup(gb);
	return -1;
}

struct fy_generic_builder *fy_generic_builder_create(const struct fy_generic_builder_cfg *cfg)
{
	struct fy_generic_builder *gb;
	int rc;

	gb = malloc(sizeof(*gb));
	if (!gb)
		return NULL;

	rc = fy_generic_builder_setup(gb, cfg);
	if (rc) {
		free(gb);
		gb = NULL;
	}

	return gb;
}

void fy_generic_builder_destroy(struct fy_generic_builder *gb)
{
	if (!gb)
		return;

	fy_generic_builder_cleanup(gb);

	free(gb);
}

struct fy_generic_builder *
fy_generic_builder_create_linear_in_place(enum fy_gb_cfg_flags flags, void *buffer, size_t size)
{
	struct fy_generic_builder_cfg cfg;
	struct fy_generic_builder *gb;
	struct fy_allocator *a;
	void *s, *e;
	int rc;

	if (!buffer || size < FY_GENERIC_BUILDER_LINEAR_IN_PLACE_MIN_SIZE)
		return NULL;

	s = buffer;
	e = s + size;

	/* find place in the buffer to put the builder */
	s = fy_ptr_align(s, _Alignof(struct fy_generic_builder));
	if ((size_t)(e - s) < sizeof(*gb))
		return NULL;
	gb = s;

	/* skip over the gb and then put down the allocator */
	s += sizeof(*gb);
	size = (size_t)(e - s);
	a = fy_linear_allocator_create_in_place(s, size);
	if (!a)
		return NULL;
	memset(&cfg, 0, sizeof(cfg));
	flags |=  FYGBCF_OWNS_ALLOCATOR;
	flags &= ~FYGBCF_DEDUP_ENABLED;
	cfg.flags = flags;
	cfg.allocator = a;
	cfg.shared_tag = FY_ALLOC_TAG_NONE;
	cfg.diag = NULL;

	rc = fy_generic_builder_setup(gb, &cfg);
	if (rc)
		return NULL;

	return gb;
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

fy_generic
fy_gb_string_vcreate(struct fy_generic_builder *gb, const char *fmt, va_list ap)
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

	return fy_gb_string_size_create(gb, str, size);
}

fy_generic
fy_gb_string_createf(struct fy_generic_builder *gb, const char *fmt, ...)
{
	va_list ap;
	fy_generic v;

	va_start(ap, fmt);
	v = fy_gb_string_vcreate(gb, fmt, ap);
	va_end(ap);

	return v;
}

fy_generic fy_gb_internalize_out_of_place(struct fy_generic_builder *gb, fy_generic v)
{
	const void *ptr;
	fy_generic vi, new_v;
	const fy_generic_sequence *seqs;
	const fy_generic_mapping *maps;
	struct iovec iov[2];
	enum fy_generic_type type;
	size_t size, i, count;
	const void *valp;
	const fy_generic *itemss;
	fy_generic *items;

	if (v.v == fy_invalid_value)
		return fy_invalid;

	/* if it's in place just return it */
	if (fy_generic_is_in_place(v))
		return v;

	/* Check if the pointer is already in the builder's allocator arena */
	ptr = fy_generic_resolve_ptr(v);
	if (fy_allocator_contains(gb->allocator, gb->alloc_tag, ptr))
		return v;

	/* indirects are handled here (note, aliases are indirect too) */
	if (fy_generic_is_indirect(v)) {
		fy_generic_indirect gi;

		fy_generic_indirect_get(v, &gi);

		gi.value = fy_gb_internalize(gb, gi.value);
		if (gi.value.v == fy_invalid_value)
			return fy_invalid;

		gi.anchor = fy_gb_internalize(gb, gi.anchor);
		if (gi.anchor.v == fy_invalid_value)
			return fy_invalid;

		gi.tag = fy_gb_internalize(gb, gi.tag);
		if (gi.tag.v == fy_invalid_value)
			return fy_invalid;

		return fy_gb_indirect_create(gb, &gi);
	}

	type = fy_generic_get_type(v);

	/* if we got to here, it's going to be a copy */
	new_v = fy_invalid;

	switch (type) {

	case FYGT_SEQUENCE:
		seqs = fy_generic_resolve_collection_ptr(v);
		if (!seqs) {
			new_v = fy_seq_empty;
			break;
		}
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
			vi = fy_gb_internalize(gb, itemss[i]);
			if (vi.v == fy_invalid_value)
				break;
			items[i] = vi;
		}

		if (i >= count) {
			iov[0].iov_base = (void *)seqs;
			iov[0].iov_len = sizeof(*seqs);
			iov[1].iov_base = items;
			iov[1].iov_len = size;
			valp = fy_gb_storev(gb, iov, ARRAY_SIZE(iov),
					FY_CONTAINER_ALIGNOF(fy_generic_sequence));
		} else
			valp = NULL;

		if (size > COPY_MALLOC_CUTOFF)
			free(items);

		if (!valp)
			break;

		new_v = (fy_generic){ .v = (uintptr_t)valp | FY_SEQ_V };
		break;

	case FYGT_MAPPING:
		maps = fy_generic_resolve_collection_ptr(v);
		if (!maps) {
			new_v = fy_map_empty;
			break;
		}
		count = maps->count * 2;
		itemss = &maps->pairs[0].items[0];	// we rely on the specific map pair layout

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

			vi = fy_gb_internalize(gb, itemss[i]);
			if (vi.v == fy_invalid_value)
				break;

			items[i] = vi;
		}

		if (i >= count) {
			iov[0].iov_base = (void *)maps;
			iov[0].iov_len = sizeof(*maps);
			iov[1].iov_base = items;
			iov[1].iov_len = size,
			valp = fy_gb_storev(gb, iov, ARRAY_SIZE(iov),
					FY_CONTAINER_ALIGNOF(fy_generic_mapping));
		} else
			valp = NULL;

		if (size > COPY_MALLOC_CUTOFF)
			free(items);

		if (!valp)
			break;

		new_v = (fy_generic){ .v = (uintptr_t)valp | FY_MAP_V };
		break;

	case FYGT_INVALID:
		break;

	default:
		/* scalars just call copy */
		new_v = fy_gb_copy(gb, v);
		break;
	}

	return new_v;
}

int fy_gb_internalize_array(struct fy_generic_builder *gb, size_t count, fy_generic *vp)
{
	fy_generic v;
	size_t i;

	for (i = 0; i < count; i++) {
		v = fy_gb_internalize(gb, vp[i]);
		if (v.v == fy_invalid_value)
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
	rc = fy_gb_internalize_array(gb, count, items_local);
	if (rc) {
		if (*items_allocp) {
			free(*items_allocp);
			*items_allocp = NULL;
		}
		return NULL;
	}

	return items_local;
}

fy_generic fy_validate_out_of_place(fy_generic v)
{
	fy_generic vi;
	const fy_generic_sequence *seqs;
	const fy_generic_mapping *maps;
	enum fy_generic_type type;
	size_t i, count;
	const fy_generic *itemss;

	if (v.v == fy_invalid_value)
		return fy_invalid;

	/* if it's in place just return it */
	if (fy_generic_is_in_place(v))
		return v;

	/* indirects are handled here (note, aliases are indirect too) */
	if (fy_generic_is_indirect(v)) {
		fy_generic_indirect gi;

		fy_generic_indirect_get(v, &gi);

		gi.value = fy_validate(gi.value);
		if (gi.value.v == fy_invalid_value)
			return fy_invalid;

		gi.anchor = fy_validate(gi.anchor);
		if (gi.anchor.v == fy_invalid_value)
			return fy_invalid;

		gi.tag = fy_validate(gi.tag);
		if (gi.tag.v == fy_invalid_value)
			return fy_invalid;

		return v;
	}

	type = fy_generic_get_type(v);

	if (type == FYGT_SEQUENCE || type == FYGT_MAPPING) {
		if (type == FYGT_SEQUENCE) {
			seqs = fy_generic_resolve_collection_ptr(v);
			if (seqs) {
				count = seqs->count;
				itemss = seqs->items;
			} else {
				count = 0;
				itemss = NULL;
			}
		} else {
			maps = fy_generic_resolve_collection_ptr(v);
			if (maps) {
				count = maps->count * 2;
				itemss = &maps->pairs[0].items[0];	// we rely on the specific map pair layout
			} else {
				count = 0;
				itemss = NULL;
			}
		}

		for (i = 0; i < count; i++) {
			vi = fy_validate(itemss[i]);
			if (vi.v == fy_invalid_value)
				return fy_invalid;
		}
	}

	return v;
}

int fy_validate_array(size_t count, const fy_generic *vp, bool deep)
{
	fy_generic v;
	size_t i;

	if (!deep) {
		for (i = 0; i < count; i++) {
			if (vp[i].v == fy_invalid_value)
				return -1;
		}
	} else {
		for (i = 0; i < count; i++) {
			v = fy_validate(vp[i]);
			if (v.v == fy_invalid_value)
				return -1;
		}
	}
	return 0;
}

fy_generic fy_gb_collection_op(struct fy_generic_builder *gb, enum fy_gb_flags flags, ...)
{
	union {
		fy_generic_sequence s;
		fy_generic_mapping m;
	} u;
	const void *p;
	struct iovec iov[8];
	int iovcnt;
	fy_generic *items_alloc = NULL, items_buf[64];
	fy_generic in, out;
	fy_generic_value col_mark;
	unsigned int op;
	size_t count, item_count, idx;
	size_t in_count, in_item_count, out_count;
	size_t col_item_size, tmp;
	size_t remain_idx, remain_count;
	const fy_generic *items, *in_items;
	enum fy_generic_type type;
	va_list ap;
	int rc;

#undef MULSZ
#define MULSZ(_x, _y) \
	({ \
		size_t __size; \
		if (FY_MUL_OVERFLOW((_x), (_y), &__size)) \
			goto err_out; \
		__size; \
	})

#undef ADDSZ
#define ADDSZ(_x, _y) \
	({ \
		size_t __size; \
		if (FY_ADD_OVERFLOW((_x), (_y), &__size)) \
			goto err_out; \
		__size; \
	})

#undef SUBSZ
#define SUBSZ(_x, _y) \
	({ \
		size_t __size; \
		if (FY_SUB_OVERFLOW((_x), (_y), &__size)) \
			goto err_out; \
		__size; \
	})

	op = ((flags >> FYGBF_OP_SHIFT) & FYGBF_OP_MASK);
	if (op >= FYGBOP_COUNT)
		return fy_invalid;

	out = in = fy_invalid;
	idx = SIZE_MAX;

	va_start(ap, src);
	switch (op) {
	case FYGBOP_CREATE_SEQ:
	case FYGBOP_CREATE_MAP:
		in = op == FYGBOP_CREATE_SEQ ? fy_seq_empty : fy_map_empty;
		count = va_arg(ap, size_t);
		items = va_arg(ap, const fy_generic *);
		break;

	case FYGBOP_INSERT:
	case FYGBOP_REPLACE:
		in = va_arg(ap, fy_generic);
		idx = va_arg(ap, size_t);
		count = va_arg(ap, size_t);
		items = va_arg(ap, const fy_generic *);
		break;

	case FYGBOP_APPEND:
		in = va_arg(ap, fy_generic);
		count = va_arg(ap, size_t);
		items = va_arg(ap, const fy_generic *);
		break;

	default:
		goto err_out;
	}
	va_end(ap);

	if (in.v == fy_invalid_value)
		return fy_invalid;

	/* if no items, no modification are done */
	if (!count)
		return in;	/* quickly return the empty collection */

	/* we must have items, otherwise it's an error */
	if (!items)
		goto err_out;

	if (fy_generic_is_indirect(in))
		in = fy_generic_indirect_get_value(in);

	type = fy_get_type(in);
	switch (type) {

	case FYGT_SEQUENCE:
		col_item_size = sizeof(fy_generic);
		item_count = count;
		col_mark = FY_SEQ_V;
		in_items = fy_generic_sequence_get_items(in, &in_item_count);
		in_count = in_item_count;
		break;

	case FYGT_MAPPING:
		col_item_size = sizeof(fy_generic) * 2;
		item_count = MULSZ(count, 2);
		col_mark = FY_MAP_V;
		in_items = fy_generic_mapping_get_items(in, &in_item_count);
		in_count = in_item_count / 2;
		break;

	default:
		goto err_out;
	}

	if (!(flags & FYGBF_DONT_INTERNALIZE)) {
		items = fy_internalize_items(gb, item_count, items,
				items_buf, ARRAY_SIZE(items_buf), &items_alloc);
		if (!items)
			goto err_out;
	} else {
		rc = fy_validate_array(item_count, items, !!(flags & FYGBF_DEEP_VALIDATE));
		if (rc)
			goto err_out;
	}

	switch (op) {
	case FYGBOP_CREATE_SEQ:
	case FYGBOP_CREATE_MAP:
		/* sequence overlaps map counter */
		u.s.count = count;
		iov[0].iov_base = &u.s;
		iov[0].iov_len = sizeof(u.s);
		iov[1].iov_base = (void *)items;
		iov[1].iov_len = MULSZ(count, col_item_size);
		iovcnt = 2;
		break;

	case FYGBOP_INSERT:
	case FYGBOP_REPLACE:
	case FYGBOP_APPEND:

		switch (op) {
		case FYGBOP_INSERT:
		case FYGBOP_APPEND:
			/* any insert after extend, will be an append */
			if (op == FYGBOP_APPEND)
				idx = in_count;
			if (idx > in_count)
				idx = in_count;
			remain_idx = idx;
			remain_count = in_count - remain_idx;
			out_count = ADDSZ(in_count, count);
			break;

		case FYGBOP_REPLACE:
			/* index over the limits is at the end */
			if (idx > in_count)
				idx = in_count;
			tmp = ADDSZ(idx, count);
			if (tmp > in_count) {
				out_count = tmp;
				remain_idx = in_count;
			} else {
				out_count = in_count;
				remain_idx = tmp;
			}
			break;
		}

		/* sequence overlaps map counter */
		u.s.count = out_count;
		iov[0].iov_base = &u.s;
		iov[0].iov_len = sizeof(u.s);
		/* before */
		iov[1].iov_base = (void *)in_items;
		iov[1].iov_len = MULSZ(idx, col_item_size);
		/* replacement */
		iov[2].iov_base = (void *)items;
		iov[2].iov_len = MULSZ(count, col_item_size);
		/* after */
		iov[3].iov_base = (void *)in_items + remain_idx * col_item_size;
		iov[3].iov_len = MULSZ(remain_count, col_item_size);
		iovcnt = 4;
		break;

	default:
		goto err_out;
	}

	p = fy_gb_storev(gb, iov, iovcnt, FY_GENERIC_CONTAINER_ALIGN);
	if (!p)
		goto err_out;

	out = (fy_generic){ .v = (uintptr_t)p | col_mark };

out:
	if (items_alloc)
		free(items_alloc);

	return out;

err_out:
	out = fy_invalid;
	goto out;
}

fy_generic fy_gb_collection_create(struct fy_generic_builder *gb, bool is_map, size_t count, const fy_generic *items, bool internalize)
{
	enum fy_gb_flags flags;

	flags = (!is_map ? FYGBF_CREATE_SEQ : FYGBF_CREATE_MAP) |
		(!internalize ? FYGBF_DONT_INTERNALIZE : 0);

	return fy_gb_collection_op(gb, flags, count, items);
}

static bool fy_collection_prepare(fy_generic col, size_t *countp, const fy_generic **itemsp, bool *is_mapp)
{
	enum fy_generic_type type;
	const void *p;

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
		const fy_generic_sequence *s = p;
		*countp = s->count;
		*itemsp = s->items;
	} else {
		const fy_generic_mapping *m = p;
		*countp = m->count;
		*itemsp = &m->pairs[0].items[0];
	}

	return true;
}

fy_generic fy_gb_collection_remove(struct fy_generic_builder *gb, fy_generic col, size_t idx, size_t count)
{
	union {
		fy_generic_sequence s;
		fy_generic_mapping m;
	} u;
	const void *p;
	fy_generic v;
	const fy_generic *old_items;
	struct iovec iov[3];
	size_t old_count, tmp;
	bool is_map;

	if (!fy_collection_prepare(col, &old_count, &old_items, &is_map))
		return fy_invalid;

	if (idx >= old_count)
		return fy_invalid;

	if (FY_ADD_OVERFLOW(idx, count, &tmp))
		return fy_invalid;
	if (tmp > old_count)
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
		if (FY_MUL_OVERFLOW(idx, 2, &idx) ||
		    FY_MUL_OVERFLOW(count, 2, &count) ||
		    FY_MUL_OVERFLOW(old_count, 2, &old_count))
			return fy_invalid;
	}

	/* before */
	if (FY_MUL_OVERFLOW(idx, sizeof(fy_generic), &tmp))
		return fy_invalid;
	iov[1].iov_base = (void *)old_items;
	iov[1].iov_len = tmp;

	/* after */
	if (FY_MUL_OVERFLOW(old_count - (idx + count), sizeof(fy_generic), &tmp))
		return fy_invalid;
	iov[2].iov_base = (void *)(old_items + idx + count);
	iov[2].iov_len = tmp;

	p = fy_gb_storev(gb, iov, ARRAY_SIZE(iov), FY_CONTAINER_ALIGNOF(fy_generic_sequence));
	if (!p)
		return fy_invalid;

	v = (fy_generic){ .v = (uintptr_t)p | (!is_map ? FY_SEQ_V : FY_MAP_V) };
	return v;
}

fy_generic fy_gb_collection_insert_replace(struct fy_generic_builder *gb, fy_generic col, size_t idx, size_t count,
						const fy_generic *items, bool insert, bool internalize)
{
	union {
		fy_generic_sequence s;
		fy_generic_mapping m;
	} u;
	const void *p;
	fy_generic *items_alloc = NULL, items_buf[64], v;
	const fy_generic *old_items;
	size_t i, old_count, new_count, remain_idx, remain_count, item_count, tmp;
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
	if (is_map) {
		// item_count *= 2;
		if (FY_MUL_OVERFLOW(item_count, 2, &item_count))
			return fy_invalid;
	}

	/* check for invalids */
	for (i = 0; i < item_count; i++) {
		if (items[i].v == fy_invalid_value)
			return fy_invalid;
	}

	if (internalize) {
		items = fy_internalize_items(gb, item_count, items, items_buf, ARRAY_SIZE(items_buf), &items_alloc);
		if (!items)
			return fy_invalid;
	}

	if (!insert) {
		/* replace */
		/* tmp = idx + count */
		if (FY_ADD_OVERFLOW(idx, count, &tmp))
			return fy_invalid;
		if (tmp > old_count) {
			new_count = tmp;
			remain_idx = old_count;
		} else {
			new_count = old_count;
			remain_idx = tmp;
		}
	} else {
		/* insert */
		/* new_count = old_count + count; */
		if (FY_ADD_OVERFLOW(old_count, count, &new_count))
			return fy_invalid;
		remain_idx = idx;
	}
	// remain_count = old_count - remain_idx;
	if (FY_SUB_OVERFLOW(old_count, remain_idx, &remain_count))
		return fy_invalid;

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
		if (FY_MUL_OVERFLOW(idx, 2, &idx) ||
		    FY_MUL_OVERFLOW(count, 2, &count) ||
		    FY_MUL_OVERFLOW(old_count, 2, &old_count) ||
		    FY_MUL_OVERFLOW(new_count, 2, &new_count) ||
		    FY_MUL_OVERFLOW(remain_idx, 2, &remain_idx) ||
		    FY_MUL_OVERFLOW(remain_count, 2, &remain_count) ||
		    FY_MUL_OVERFLOW(item_count, 2, &item_count))
			return fy_invalid;
	}

	/* before */
	if (FY_MUL_OVERFLOW(idx, sizeof(fy_generic), &tmp))
		return fy_invalid;
	iov[1].iov_base = (void *)old_items;
	iov[1].iov_len = tmp;

	/* replacement */
	if (FY_MUL_OVERFLOW(count, sizeof(fy_generic), &tmp))
		return fy_invalid;
	iov[2].iov_base = (void *)items;
	iov[2].iov_len = tmp;

	/* remainder */
	if (FY_MUL_OVERFLOW(remain_count, sizeof(fy_generic), &tmp))
		return fy_invalid;
	iov[3].iov_base = (void *)(old_items + remain_idx);
	iov[3].iov_len = tmp;

	p = fy_gb_storev(gb, iov, ARRAY_SIZE(iov), FY_CONTAINER_ALIGNOF(fy_generic_sequence));
	if (!p)
		return fy_invalid;

	v = (fy_generic){ .v = (uintptr_t)p | (!is_map ? FY_SEQ_V : FY_MAP_V) };

	if (items_alloc)
		free(items_alloc);

	return v;
}

fy_generic fy_gb_sequence_create_i(struct fy_generic_builder *gb, bool internalize,
					size_t count, const fy_generic *items)
{
	return fy_gb_collection_create(gb, false, count, items, internalize);
}

fy_generic fy_gb_sequence_create(struct fy_generic_builder *gb, size_t count, const fy_generic *items)
{
	return fy_gb_collection_create(gb, false, count, items, true);
}

fy_generic fy_gb_sequence_remove(struct fy_generic_builder *gb, fy_generic seq, size_t idx, size_t count)
{
	return fy_gb_collection_remove(gb, seq, idx, count);
}

fy_generic fy_gb_sequence_insert_replace_i(struct fy_generic_builder *gb, bool insert, bool internalize,
						fy_generic seq, size_t idx, size_t count, const fy_generic *items)
{
	return fy_gb_collection_insert_replace(gb, seq, idx, count, items, insert, internalize);
}

fy_generic fy_gb_sequence_insert_replace(struct fy_generic_builder *gb, bool insert,
					      fy_generic seq, size_t idx, size_t count, const fy_generic *items)
{
	return fy_gb_sequence_insert_replace_i(gb, insert, true, seq, idx, count, items);
}

fy_generic fy_gb_sequence_insert_i(struct fy_generic_builder *gb, bool internalize,
					fy_generic seq, size_t idx, size_t count, const fy_generic *items)
{
	return fy_gb_sequence_insert_replace_i(gb, true, internalize, seq, idx, count, items);
}

fy_generic fy_gb_sequence_insert(struct fy_generic_builder *gb, fy_generic seq, size_t idx, size_t count,
				      const fy_generic *items)
{
	return fy_gb_sequence_insert_i(gb, false, seq, idx, count, items);
}

fy_generic fy_gb_sequence_replace_i(struct fy_generic_builder *gb, bool internalize,
					 fy_generic seq, size_t idx, size_t count, const fy_generic *items)
{
	return fy_gb_sequence_insert_replace_i(gb, false, internalize, seq, idx, count, items);
}

fy_generic fy_gb_sequence_replace(struct fy_generic_builder *gb, fy_generic seq, size_t idx, size_t count,
				      const fy_generic *items)
{
	return fy_gb_sequence_replace_i(gb, false, seq, idx, count, items);
}

fy_generic fy_gb_sequence_append_i(struct fy_generic_builder *gb, bool internalize,
					fy_generic seq, size_t count, const fy_generic *items)
{
	size_t idx;

	idx = fy_generic_sequence_get_item_count(seq);
	return fy_gb_sequence_insert_replace_i(gb, true, internalize, seq, idx, count, items);
}

fy_generic fy_gb_sequence_append(struct fy_generic_builder *gb, fy_generic seq, size_t count, const fy_generic *items)
{
	return fy_gb_sequence_append_i(gb, true, seq, count, items);
}

fy_generic fy_gb_sequence_set_item_i(struct fy_generic_builder *gb, bool internalize,
					  fy_generic seq, size_t idx, fy_generic item)
{
	if (seq.v == fy_invalid_value || item.v == fy_invalid_value)
		return fy_invalid;

	return fy_gb_mapping_replace_i(gb, internalize, seq, idx, 1, &item);
}

fy_generic fy_gb_sequence_set_item(struct fy_generic_builder *gb,
					  fy_generic seq, size_t idx, fy_generic item)
{
	return fy_gb_sequence_set_item_i(gb, true, seq, idx, item);
}

fy_generic fy_gb_mapping_create_i(struct fy_generic_builder *gb, bool internalize,
				       size_t count, const fy_generic *pairs)
{
	return fy_gb_collection_create(gb, true, count, pairs, internalize);
}

fy_generic fy_gb_mapping_create(struct fy_generic_builder *gb, size_t count, const fy_generic *pairs)
{
	return fy_gb_mapping_create_i(gb, true, count, pairs);
}

fy_generic fy_gb_mapping_remove(struct fy_generic_builder *gb, fy_generic map, size_t idx, size_t count)
{
	return fy_gb_collection_remove(gb, map, idx, count);
}

fy_generic fy_gb_mapping_insert_replace_i(struct fy_generic_builder *gb, bool insert, bool internalize,
						fy_generic map, size_t idx, size_t count, const fy_generic *pairs)
{
	return fy_gb_collection_insert_replace(gb, map, idx, count, pairs, insert, internalize);
}

fy_generic fy_gb_mapping_insert_replace(struct fy_generic_builder *gb, bool insert,
					      fy_generic map, size_t idx, size_t count, const fy_generic *pairs)
{
	return fy_gb_mapping_insert_replace_i(gb, insert, true, map, idx, count, pairs);
}

fy_generic fy_gb_mapping_insert_i(struct fy_generic_builder *gb, bool internalize,
					fy_generic map, size_t idx, size_t count, const fy_generic *pairs)
{
	return fy_gb_mapping_insert_replace_i(gb, true, internalize, map, idx, count, pairs);
}

fy_generic fy_gb_mapping_insert(struct fy_generic_builder *gb, fy_generic map, size_t idx, size_t count,
				      const fy_generic *pairs)
{
	return fy_gb_mapping_insert_i(gb, false, map, idx, count, pairs);
}

fy_generic fy_gb_mapping_replace_i(struct fy_generic_builder *gb, bool internalize,
					 fy_generic map, size_t idx, size_t count, const fy_generic *pairs)
{
	return fy_gb_mapping_insert_replace_i(gb, false, internalize, map, idx, count, pairs);
}

fy_generic fy_gb_mapping_replace(struct fy_generic_builder *gb, fy_generic map, size_t idx, size_t count,
				      const fy_generic *pairs)
{
	return fy_gb_mapping_replace_i(gb, false, map, idx, count, pairs);
}

fy_generic fy_gb_mapping_append_i(struct fy_generic_builder *gb, bool internalize,
					fy_generic map, size_t count, const fy_generic *pairs)
{
	size_t idx;

	idx = fy_generic_mapping_get_pair_count(map);
	return fy_gb_mapping_insert_replace_i(gb, true, internalize, map, idx, count, pairs);
}

fy_generic fy_gb_mapping_append(struct fy_generic_builder *gb, fy_generic map, size_t count, const fy_generic *pairs)
{
	return fy_gb_mapping_append_i(gb, true, map, count, pairs);
}

fy_generic fy_gb_mapping_set_value_i(struct fy_generic_builder *gb, bool internalize,
					  fy_generic map, fy_generic key, fy_generic value)
{
	size_t idx;
	fy_generic old_value;

	if (map.v == fy_invalid_value || key.v == fy_invalid_value || value.v == fy_invalid_value)
		return fy_invalid;

	old_value = fy_generic_mapping_get_value_index(map, key, &idx);

	/* found? replace */
	if (old_value.v != fy_invalid_value)
		return fy_gb_mapping_replace_i(gb, internalize, map, idx, 1, (fy_generic[]){ key, value });

	/* not found? append */
	idx = fy_generic_mapping_get_pair_count(map);
	return fy_gb_mapping_insert_i(gb, internalize, map, idx, 1, (fy_generic[]){ key, value });
}

fy_generic fy_gb_mapping_set_value(struct fy_generic_builder *gb,
					  fy_generic map, fy_generic key, fy_generic value)
{
	return fy_gb_mapping_set_value_i(gb, true, map, key, value);
}

fy_generic fy_gb_indirect_create(struct fy_generic_builder *gb, const fy_generic_indirect *gi)
{
	const void *p;
	struct iovec iov[4];
	size_t cnt;
	uintptr_t flags;

	cnt = 0;

	flags = 0;
	if (gi->value.v != fy_invalid_value)
		flags |= FYGIF_VALUE;
	if (gi->anchor.v != fy_null_value && gi->anchor.v != fy_invalid_value)
		flags |= FYGIF_ANCHOR;
	if (gi->tag.v != fy_null_value && gi->tag.v != fy_invalid_value)
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

	p = fy_gb_storev(gb, iov, cnt, FY_CONTAINER_ALIGNOF(fy_generic));	/* must be at least 16 */
	if (!p)
		return fy_invalid;

	return (fy_generic){ .v = (uintptr_t)p | FY_INDIRECT_V };
}

fy_generic fy_gb_alias_create(struct fy_generic_builder *gb, fy_generic anchor)
{
	fy_generic_indirect gi = {
		.value = fy_invalid,
		.anchor = anchor,
		.tag = fy_invalid,
	};

	return fy_gb_indirect_create(gb, &gi);
}

fy_generic fy_gb_create_scalar_from_text(struct fy_generic_builder *gb, const char *text, size_t len, enum fy_generic_type force_type)
{
	enum fy_generic_schema schema;
	fy_generic v;
	const char *s, *e;
	const char *dec, *fract, *exp;
	char sign, exp_sign;
	size_t dec_count, fract_count, exp_count;
	int base;
	long long lv;
	unsigned long long ulv;
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
				v = fy_gb_to_generic(gb, INFINITY);
				break;
		    }
		    if (!memcmp(text, ".nan", 4) || !memcmp(text, ".Nan", 4) || !memcmp(text, ".NAN", 4)) {
				v = fy_gb_to_generic(gb, NAN);
				break;
		    }
		}
		if (len == 5) {
		    if (!memcmp(text, "false", 5) || !memcmp(text, "False", 5) || !memcmp(text, "FALSE", 5)) {
			    v = fy_false;
			    break;
		    }
		    if (!memcmp(text, "+.inf", 5) || !memcmp(text, "+.Inf", 5) || !memcmp(text, "+.INF", 5)) {
				v = fy_gb_to_generic(gb, INFINITY);
				break;
		    }
		    if (!memcmp(text, "-.inf", 5) || !memcmp(text, "-.Inf", 5) || !memcmp(text, "-.INF", 5)) {
				v = fy_gb_to_generic(gb, -INFINITY);
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
				v = fy_gb_to_generic(gb, INFINITY);
				break;
		    }
		    if (!memcmp(text, ".nan", 4) || !memcmp(text, ".Nan", 4) || !memcmp(text, ".NAN", 4)) {
				v = fy_gb_to_generic(gb, NAN);
				break;
		    }
		}
		if (len == 5) {
		    if (!memcmp(text, "false", 5) || !memcmp(text, "False", 5) || !memcmp(text, "FALSE", 5)) {
			    v = fy_false;
			    break;
		    }
		    if (!memcmp(text, "+.inf", 5) || !memcmp(text, "+.Inf", 5) || !memcmp(text, "+.INF", 5)) {
				v = fy_gb_to_generic(gb, INFINITY);
				break;
		    }
		    if (!memcmp(text, "-.inf", 5) || !memcmp(text, "-.Inf", 5) || !memcmp(text, "-.INF", 5)) {
				v = fy_gb_to_generic(gb, -INFINITY);
				break;
		    }
		}

		break;


	default:
		break;
	}

	if (v.v != fy_invalid_value)
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
		if (errno == 0) {
			v = fy_gb_to_generic(gb, lv);
			goto do_check_cast;
		}
		if (errno == ERANGE) {
			errno = 0;
			ulv = strtoull(t, NULL, base);
			if (errno == 0) {
				v = fy_gb_to_generic(gb, ulv);
				goto do_check_cast;
			}
		}
		goto do_string;
	} else {
		errno = 0;
		dv = strtod(t, NULL);
		if (errno == ERANGE)
			goto do_string;

		v = fy_gb_to_generic(gb, dv);
		goto do_check_cast;
	}

do_string:
	/* everything tried, just a string */
	v = fy_gb_string_size_create(gb, text, len);

do_check_cast:
	if (force_type != FYGT_INVALID && fy_generic_get_type(v) != force_type) {
		fprintf(stderr, "cast failed\n");
		return fy_invalid;
	}
	return v;
}

static int fy_generic_sequence_compare(fy_generic seqa, fy_generic seqb)
{
	size_t i, counta, countb;
	const fy_generic *itemsa, *itemsb;
	int ret;

	if (seqa.v == seqb.v)
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
	return seqa.v > seqb.v ? 1 : -1;	/* keep order, but it's just address based */
}

static int fy_generic_mapping_compare(fy_generic mapa, fy_generic mapb)
{
	size_t i, counta, countb;
	const fy_generic_map_pair *pairsa, *pairsb;
	fy_generic key, vala, valb;
	int ret;

	if (mapa.v == mapb.v)
		return 0;

	pairsa = fy_generic_mapping_get_pairs(mapa, &counta);
	pairsb = fy_generic_mapping_get_pairs(mapb, &countb);

	if (!pairsa || !pairsb || counta != countb)
		goto out;

	/* empty? just fine */
	if (counta == 0)
		return 0;

	/* try to cheat by comparing contents */
	ret = memcmp(pairsa, pairsb, counta * sizeof(*pairsa));
	if (!ret)
		return 0;	/* great! binary match */

	/* have to do it the hard way */
	for (i = 0; i < counta; i++) {
		key = pairsa[i].key;
		vala = pairsa[i].value;

		/* find if the key exists in the other mapping */
		valb = fy_generic_mapping_get_value(mapb, key);
		if (valb.v == fy_invalid_value)
			goto out;

		/* compare values */
		ret = fy_generic_compare(vala, valb);
		if (ret)
			goto out;
	}

	/* all the keys value pairs match */

	return 0;
out:
	return mapa.v > mapb.v ? 1 : -1;	/* keep order, but it's just address based */
}

static inline int fy_generic_bool_compare(fy_generic a, fy_generic b)
{
	int ba, bb;

	ba = (int)fy_cast(a, false);
	bb = (int)fy_cast(b, false);
	return ba > bb ?  1 :
	       ba < bb ? -1 : 0;
}

static inline int fy_generic_int_compare(fy_generic a, fy_generic b)
{
	fy_generic_decorated_int dia, dib;
	long long ia, ib;

	/* if both are in place, they're signed quick exit */
	if (fy_generic_is_in_place(a) && fy_generic_is_in_place(b)) {
		ia = fy_cast(a, (long long)0);
		ib = fy_cast(b, (long long)0);
		return ia > ib ?  1 :
		       ia < ib ? -1 : 0;
	}

	/* need to check more thourougly */
	dia = fy_cast(a, fy_dint_empty);
	dib = fy_cast(b, fy_dint_empty);

	/* if bit patterns match, we're good immediately */
	if (dia.sv == dib.sv)
		return 0;

	/* signed, unsigned match, compare */
	if (dia.is_unsigned && dib.is_unsigned)
		return dia.uv < dib.uv ? -1 : 1;
	if (!dia.is_unsigned && !dib.is_unsigned)
		return dia.sv < dib.sv ? -1 : 1;

	/* one is signed the other is unsigned */

	/* if any of them is signed and less than 0 */
	if (!dia.is_unsigned && dia.sv < 0)
		return -1;
	if (!dib.is_unsigned && dib.sv < 0)
		return 1;

	/* they are both positive, just check unsigned */
	return dia.uv < dib.uv ? -1 : 1;
}

static inline int fy_generic_float_compare(fy_generic a, fy_generic b)
{
	double da, db;

	da = fy_cast(a, (double)NAN);
	db = fy_cast(b, (double)NAN);
	return da > db ?  1 :
	       da < db ? -1 : 0;
}

static inline int fy_generic_string_compare(fy_generic a, fy_generic b)
{
	fy_generic_sized_string sa, sb;
	int ret;

	sa = fy_cast(a, fy_szstr_empty);
	sb = fy_cast(b, fy_szstr_empty);

	ret = memcmp(sa.data, sb.data, sa.size > sb.size ? sb.size : sa.size);

	if (!ret && sa.size != sb.size)
		ret = sa.size > sb.size ? 1 : -1;
	return ret;
}

static inline int fy_generic_alias_compare(fy_generic a, fy_generic b)
{
	fy_generic aa, ab;
	const char *sa;
	const char *sb;

	aa = fy_generic_indirect_get_anchor(a);
	ab = fy_generic_indirect_get_anchor(b);

	sa = fy_cast(aa, (const char *)"");
	sb = fy_cast(ab, (const char *)"");

	return strcmp(sa, sb);
}

int fy_generic_compare_out_of_place(fy_generic a, fy_generic b)
{
	enum fy_generic_type at, bt;

	/* invalids are always non-matching */
	if (a.v == fy_invalid_value || b.v == fy_invalid_value)
		return -1;

	/* equals? nice - should work for null, bool, in place int, float and strings  */
	/* also for anything that's a pointer */
	if (a.v == b.v)
		return 0;

	at = fy_generic_get_type(a);
	bt = fy_generic_get_type(b);

	/* invalid types, or differing types do not match */
	if (at != bt)
		return at < bt ? -1 : 1;	// keep types

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

fy_generic fy_gb_copy_out_of_place(struct fy_generic_builder *gb, fy_generic v)
{
	const fy_generic_sequence *seqs;
	fy_generic vi, new_v;
	const fy_generic_mapping *maps;
	struct iovec iov[2];
	enum fy_generic_type type;
	size_t size, len, i, count;
	const void *valp;
	const uint8_t *p, *str;
	const long long *ip;
	const fy_generic *itemss;
	fy_generic *items;

	if (v.v == fy_invalid_value)
		return fy_invalid;

	/* indirects are handled here (note, aliases are indirect too) */
	if (fy_generic_is_indirect(v)) {
		fy_generic_indirect gi;

		fy_generic_indirect_get(v, &gi);

		gi.value = fy_gb_copy(gb, gi.value);
		gi.anchor = fy_gb_copy(gb, gi.anchor);
		gi.tag = fy_gb_copy(gb, gi.tag);

		return fy_gb_indirect_create(gb, &gi);
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
		ip = fy_generic_resolve_ptr(v);
		valp = fy_gb_store(gb, ip, sizeof(fy_generic_decorated_int), FY_SCALAR_ALIGNOF(long long));
		if (!valp)
			break;

		new_v = (fy_generic){ .v = (uintptr_t)valp | FY_INT_OUTPLACE_V };
		break;

	case FYGT_FLOAT:
		valp = fy_gb_store(gb, fy_generic_resolve_ptr(v),
				sizeof(double), FY_SCALAR_ALIGNOF(double));
		if (!valp)
			break;

		new_v = (fy_generic){ .v = (uintptr_t)valp | FY_FLOAT_OUTPLACE_V };
		break;

	case FYGT_STRING:
		p = fy_generic_resolve_ptr(v);
		str = fy_decode_size(p, FYGT_SIZE_ENCODING_MAX, &len);
		if (!str)
			break;

		size = (size_t)(str - p) + len;
		valp = fy_gb_store(gb, p, size + 1, 8);
		if (!valp)
			break;

		new_v = (fy_generic){ .v = (uintptr_t)valp | FY_STRING_OUTPLACE_V };
		break;

	case FYGT_SEQUENCE:
		seqs = fy_generic_resolve_collection_ptr(v);
		if (!seqs) {
			new_v = fy_seq_empty;
			break;
		}
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
			vi = fy_gb_copy(gb, itemss[i]);
			if (vi.v == fy_invalid_value)
				break;
			items[i] = vi;
		}

		if (i >= count) {
			iov[0].iov_base = (void *)seqs;
			iov[0].iov_len = sizeof(*seqs);
			iov[1].iov_base = items;
			iov[1].iov_len = size;
			valp = fy_gb_storev(gb, iov, ARRAY_SIZE(iov),
					FY_CONTAINER_ALIGNOF(fy_generic_sequence));
		} else
			valp = NULL;

		if (size > COPY_MALLOC_CUTOFF)
			free(items);

		if (!valp)
			break;

		new_v = (fy_generic){ .v = (uintptr_t)valp | FY_SEQ_V };
		break;

	case FYGT_MAPPING:
		maps = fy_generic_resolve_collection_ptr(v);
		if (!maps) {
			new_v = fy_map_empty;
			break;
		}
		count = maps->count * 2;
		itemss = &maps->pairs[0].items[0];	// we rely on the mapping layout

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

			vi = fy_gb_copy(gb, itemss[i]);
			if (vi.v == fy_invalid_value)
				break;

			items[i] = vi;
		}

		if (i >= count) {
			iov[0].iov_base = (void *)maps;
			iov[0].iov_len = sizeof(*maps);
			iov[1].iov_base = items;
			iov[1].iov_len = size,
			valp = fy_gb_storev(gb, iov, ARRAY_SIZE(iov),
					FY_CONTAINER_ALIGNOF(fy_generic_mapping));
		} else
			valp = NULL;

		if (size > COPY_MALLOC_CUTOFF)
			free(items);

		if (!valp)
			break;

		new_v = (fy_generic){ .v = (uintptr_t)valp | FY_MAP_V };
		break;

	default:
		break;
	}

	/* there must have been a change */
	assert(new_v.v != v.v);

	return new_v;
}

fy_generic fy_generic_relocate(void *start, void *end, fy_generic v, ptrdiff_t d)
{
	const void *p;
	fy_generic_indirect *gi;
	fy_generic_sequence *seq;
	fy_generic_mapping *map;
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

		v.v = fy_generic_relocate_collection_ptr(v, d).v | FY_INDIRECT_V;
		gi = (fy_generic_indirect *)fy_generic_resolve_ptr(v);
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
		if ((v.v & FY_INPLACE_TYPE_MASK) == FY_INT_INPLACE_V)
			return v;

		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v.v = fy_generic_relocate_ptr(v, d).v | FY_INT_OUTPLACE_V;
		break;

	case FYGT_FLOAT:
		if ((v.v & FY_INPLACE_TYPE_MASK) == FY_FLOAT_INPLACE_V)
			return v;

		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v.v = fy_generic_relocate_ptr(v, d).v | FY_FLOAT_OUTPLACE_V;
		break;

	case FYGT_STRING:
		if ((v.v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V)
			return v;

		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v.v = fy_generic_relocate_ptr(v, d).v | FY_STRING_OUTPLACE_V;
		break;

	case FYGT_SEQUENCE:
		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v.v = fy_generic_relocate_collection_ptr(v, d).v | FY_SEQ_V;
		seq = (fy_generic_sequence *)fy_generic_resolve_collection_ptr(v);
		count = seq->count;
		items = (fy_generic *)seq->items;
		for (i = 0; i < count; i++)
			items[i] = fy_generic_relocate(start, end, items[i], d);
		break;

	case FYGT_MAPPING:
		p = fy_generic_resolve_ptr(v);
		if (p >= start && p < end)
			return v;

		v.v = fy_generic_relocate_collection_ptr(v, d).v | FY_MAP_V;
		map = (fy_generic_mapping *)fy_generic_resolve_collection_ptr(v);
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

enum fy_generic_schema fy_gb_get_schema(struct fy_generic_builder *gb)
{
	return gb ? gb->schema : FYGS_AUTO;
}

void fy_gb_set_schema(struct fy_generic_builder *gb, enum fy_generic_schema schema)
{
	if ((unsigned int)schema >= FYGS_COUNT)
		return;
	gb->schema = schema;
}

int fy_gb_set_schema_from_parser_mode(struct fy_generic_builder *gb, enum fy_parser_mode parser_mode)
{
	enum fy_generic_schema schema;

	if (!gb)
		return -1;

	schema = fy_gb_get_schema(gb);
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

	fy_gb_set_schema(gb, schema);
	return 0;
}

void fy_generic_dump_primitive(FILE *fp, int level, fy_generic vv)
{
	static const char generic_type_map[] = {
		[FYGT_INVALID] 	= '!',
		[FYGT_NULL]	= 'n',
		[FYGT_BOOL]	= 'b',
		[FYGT_INT]	= 'i',
		[FYGT_FLOAT]	= 'f',
		[FYGT_STRING]	= '"',
		[FYGT_SEQUENCE]	= '[',
		[FYGT_MAPPING]	= '{',
		[FYGT_INDIRECT]	= '^',
		[FYGT_ALIAS]	= '*',
	};
	fy_generic vtag, vanchor, v, iv, key, value;
	const char *tag = NULL, *anchor = NULL;
	const fy_generic *items;
	const fy_generic_map_pair *pairs;
	fy_generic_sized_string szstr;
	size_t i, count;

	vanchor = fy_generic_get_anchor(vv);
	vtag = fy_generic_get_tag(vv);
	anchor = fy_castp(&vanchor, (const char *)NULL);
	tag = fy_castp(&vtag, (const char *)NULL);
	v = fy_generic_is_indirect(vv) ? fy_generic_indirect_get_value(vv) : vv;

	fprintf(fp, "%*s", level * 2, "");

	if (v.v != vv.v)
		fprintf(fp, "(%016lx) ", vv.v);
	if (anchor)
		fprintf(fp, "&%s ", anchor);
	if (tag)
		fprintf(fp, "%s ", tag);

	fprintf(fp, "%016lx ", v.v);
	fprintf(fp, "%c ", generic_type_map[fy_generic_get_type(v)]);


	if (fy_generic_is_invalid(v))
		fprintf(fp, "invalid");

	switch (fy_generic_get_type(v)) {
	case FYGT_NULL:
		fprintf(fp, "%s", "null");
		return;

	case FYGT_BOOL:
		fprintf(fp, "%s", fy_cast(v, false) ? "true" : "false");
		return;

	case FYGT_INT:
		fprintf(fp, "%lld", fy_cast(v, (long long)0));
		return;

	case FYGT_FLOAT:
		fprintf(fp, "%f", fy_cast(v, (double)0.0));
		return;

	case FYGT_STRING:
		szstr = fy_cast(v, fy_szstr_empty);
		fprintf(fp, "%s", fy_utf8_format_text_a(szstr.data, szstr.size, fyue_doublequote));
		return;

	case FYGT_SEQUENCE:
		items = fy_generic_sequence_get_items(v, &count);
		for (i = 0; i < count; i++) {
			iv = items[i];
			fy_generic_dump_primitive(fp, level + 1, iv);
		}
		break;

	case FYGT_MAPPING:
		pairs = fy_generic_mapping_get_pairs(v, &count);
		for (i = 0; i < count; i++) {
			key = pairs[i].key;
			value = pairs[i].value;
			fy_generic_dump_primitive(fp, level + 1, key);
			fprintf(fp, ": ");
			fy_generic_dump_primitive(fp, level + 1, value);
		}
		break;

	case FYGT_ALIAS:
		vanchor = fy_generic_get_anchor(v);
		fprintf(fp, "%s", fy_castp(&vanchor, ""));
		break;

	default:
		FY_IMPOSSIBLE_ABORT();
	}
	fprintf(fp, "\n");
}

