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
#include "fy-thread.h"

#include "fy-generic.h"
#include "fy-generic-encoder.h"
#include "fy-generic-decoder.h"

// when to switch to malloc instead of alloca
#define COPY_MALLOC_CUTOFF	256

enum fy_generic_type fy_generic_get_type_indirect(fy_generic v)
{
	const fy_generic_value *p;

	assert(fy_generic_is_indirect(v));

	p = fy_generic_resolve_collection_ptr(v);
	if (!(p[0] & FYGIF_VALUE))
		return FYGT_ALIAS;
	v.v = p[1];
	if (fy_generic_is_indirect(v))
		return FYGT_INVALID;
	return fy_generic_get_direct_type(v);
}

void fy_generic_indirect_get(fy_generic v, fy_generic_indirect *gi)
{
	const fy_generic_value *p;

	memset(gi, 0, sizeof(*gi));

	if (!fy_generic_is_indirect(v)) {
		gi->flags = FYGIF_VALUE;
		gi->value = v;
		gi->anchor.v = fy_invalid_value;
		gi->tag.v = fy_invalid_value;
		return;
	}

	p = fy_generic_resolve_collection_ptr(v);

	/* get flags */
	gi->flags = *p++;
	gi->value.v = (gi->flags & FYGIF_VALUE) ? *p++ : fy_invalid_value;
	gi->anchor.v = (gi->flags & FYGIF_ANCHOR) ? *p++ : fy_invalid_value;
	gi->tag.v = (gi->flags & FYGIF_TAG) ? *p++ : fy_invalid_value;
}

const fy_generic *fy_genericp_indirect_get_valuep_nocheck(const fy_generic *vp)
{
	const fy_generic_value *p;
	uintptr_t flags;

	p = fy_generic_resolve_collection_ptr(*vp);
	flags = *p++;
	return (flags & FYGIF_VALUE) ? (const fy_generic *)p : NULL;
}


const fy_generic *fy_genericp_indirect_get_valuep(const fy_generic *vp)
{
	const fy_generic_value *p;
	uintptr_t flags;

	if (!vp)
		return NULL;

	if (!fy_generic_is_indirect(*vp))
		return vp;

	p = fy_generic_resolve_collection_ptr(*vp);
	flags = *p++;
	return (flags & FYGIF_VALUE) ? (const fy_generic *)p : NULL;
}

fy_generic fy_generic_indirect_get_value_nocheck(const fy_generic v)
{
	const fy_generic_value *p;

	p = fy_generic_resolve_collection_ptr(v);
	return (p[0] & FYGIF_VALUE) ? *(const fy_generic *)&p[1] : fy_invalid;
}

fy_generic fy_generic_indirect_get_value(const fy_generic v)
{
	const fy_generic_value *p;

	if (!fy_generic_is_indirect(v))
		return v;

	p = fy_generic_resolve_collection_ptr(v);
	return (p[0] & FYGIF_VALUE) ? *(const fy_generic *)&p[1] : fy_invalid;
}

fy_generic fy_generic_indirect_get_anchor(fy_generic v)
{
	fy_generic_indirect gi;

	fy_generic_indirect_get(v, &gi);
	return gi.anchor;
}

fy_generic fy_generic_indirect_get_tag(fy_generic v)
{
	fy_generic_indirect gi;

	fy_generic_indirect_get(v, &gi);
	return gi.tag;
}

fy_generic fy_generic_get_anchor(fy_generic v)
{
	fy_generic va;

	if (!fy_generic_is_indirect(v))
		return fy_null;

	va = fy_generic_indirect_get_anchor(v);
	assert(va.v == fy_null_value || va.v == fy_invalid_value || fy_generic_get_type(va) == FYGT_STRING);
	return va;
}

fy_generic fy_generic_get_tag(fy_generic v)
{
	fy_generic vt;

	if (!fy_generic_is_indirect(v))
		return fy_null;

	vt = fy_generic_indirect_get_tag(v);
	assert(vt.v == fy_null_value || vt.v == fy_invalid_value || fy_generic_get_type(vt) == FYGT_STRING);
	return vt;
}

FY_GENERIC_IS_TEMPLATE_NON_INLINE(null_type);
FY_GENERIC_IS_TEMPLATE_NON_INLINE(bool_type);

FY_GENERIC_IS_TEMPLATE_NON_INLINE(int_type);
FY_GENERIC_IS_TEMPLATE_NON_INLINE(uint_type);

FY_GENERIC_IS_TEMPLATE_NON_INLINE(float_type);

FY_GENERIC_IS_TEMPLATE_NON_INLINE(string);
FY_GENERIC_IS_TEMPLATE_NON_INLINE(string_type);

FY_GENERIC_IS_TEMPLATE_NON_INLINE(sequence);
FY_GENERIC_IS_TEMPLATE_NON_INLINE(sequence_type);

FY_GENERIC_IS_TEMPLATE_NON_INLINE(mapping);
FY_GENERIC_IS_TEMPLATE_NON_INLINE(mapping_type);

FY_GENERIC_IS_TEMPLATE_NON_INLINE(collection);

FY_GENERIC_IS_TEMPLATE_NON_INLINE(alias);

const fy_generic_sequence *
fy_generic_sequence_resolve_outofplace(fy_generic seq)
{
	if (fy_generic_is_indirect(seq))
		seq = fy_generic_indirect_get_value(seq);

	if (!fy_generic_is_direct_sequence(seq))
		return NULL;
	return fy_generic_resolve_collection_ptr(seq);
}

const fy_generic_mapping *
fy_generic_mapping_resolve_outofplace(fy_generic map)
{
	if (fy_generic_is_indirect(map))
		map = fy_generic_indirect_get_value(map);

	if (!fy_generic_is_direct_mapping(map))
		return NULL;
	return fy_generic_resolve_collection_ptr(map);
}

extern __thread struct fy_generic_builder *fy_current_gb;

static const struct fy_generic_builder_cfg default_generic_builder_cfg = {
	.flags = FYGBCF_SCHEMA_AUTO | FYGBCF_OWNS_ALLOCATOR,
	.allocator = NULL,	/* use default */
};

void fy_generic_builder_cleanup(struct fy_generic_builder *gb)
{
	if (!gb)
		return;

	if (gb->linear)
		free(gb->linear);

	/* if we own the allocator, just destroy it, everything is gone */
	if (gb->flags & FYGBF_OWNS_ALLOCATOR)
		fy_allocator_destroy(gb->allocator);
	else if (gb->flags & FYGBF_CREATED_TAG)
		fy_allocator_release_tag(gb->allocator, gb->alloc_tag);
}

int fy_generic_builder_setup(struct fy_generic_builder *gb, const struct fy_generic_builder_cfg *cfg)
{
	enum fy_generic_schema schema;
	struct fy_auto_allocator_cfg auto_cfg;

	if (!gb)
		return -1;

	if (!cfg)
		cfg = &default_generic_builder_cfg;

	/* get and verify schema */
	schema = (cfg->flags & FYGBCF_SCHEMA_MASK) >> FYGBCF_SCHEMA_SHIFT;
	if (schema >= FYGS_COUNT)
		goto err_out;

	memset(gb, 0, sizeof(*gb));
	gb->cfg = *cfg;

	gb->flags = ((cfg->flags & FYGBCF_SCOPE_LEADER) ? FYGBF_SCOPE_LEADER : 0) |
		    ((cfg->flags & FYGBCF_DEDUP_ENABLED) ? FYGBF_DEDUP_ENABLED : 0) |
		    ((cfg->flags & FYGBCF_OWNS_ALLOCATOR) ? FYGBF_OWNS_ALLOCATOR : 0);

	/* turn on the dedup chain bit if no parent, or the parent has it too */
	if ((gb->flags & FYGBF_DEDUP_ENABLED) &&
		(!cfg->parent || (cfg->parent->flags & FYGBF_DEDUP_CHAIN)))
		gb->flags |= FYGBF_DEDUP_CHAIN;

	if (!cfg->allocator) {
		memset(&auto_cfg, 0, sizeof(auto_cfg));
		auto_cfg.scenario = (gb->flags & FYGBF_DEDUP_ENABLED) ?
					FYAST_SINGLE_LINEAR_RANGE_DEDUP :
					FYAST_SINGLE_LINEAR_RANGE;
		auto_cfg.estimated_max_size = cfg->estimated_max_size;
		gb->allocator = fy_allocator_create("auto", &auto_cfg);	// let the system decide
		if (!gb->allocator)
			goto err_out;
		gb->flags |= FYGBF_OWNS_ALLOCATOR;
	} else {
		gb->flags &= ~FYGBF_OWNS_ALLOCATOR;
		gb->allocator = cfg->allocator;
	}

	if (cfg->flags & FYGBCF_CREATE_TAG) {
		gb->alloc_tag = fy_allocator_get_tag(gb->allocator);
		if (gb->alloc_tag == FY_ALLOC_TAG_ERROR)
			goto err_out;
		gb->flags |= FYGBF_CREATED_TAG;
	} else
		gb->alloc_tag = FY_ALLOC_TAG_DEFAULT;

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
fy_generic_builder_create_in_place(enum fy_gb_cfg_flags flags, struct fy_generic_builder *parent, void *buffer, size_t size)
{
	struct fy_generic_builder_cfg cfg;
	struct fy_generic_builder *gb;
	struct fy_allocator *a;
	void *s, *e;
	int rc;

	if (!buffer)
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

	/* create the proper allocator according to flags (DEDUP enabled enabled dedup) */
	a = !(flags & FYGBCF_DEDUP_ENABLED) ?
		fy_linear_allocator_create_in_place(s, size) :
		fy_dedup_allocator_create_in_place(s, size);
	if (!a)
		return NULL;

	memset(&cfg, 0, sizeof(cfg));
	flags |= FYGBCF_OWNS_ALLOCATOR;
	cfg.flags = flags;
	cfg.allocator = a;
	cfg.parent = parent;

	rc = fy_generic_builder_setup(gb, &cfg);
	if (rc)
		return NULL;

	return gb;
}

struct fy_allocator *fy_generic_builder_get_allocator(struct fy_generic_builder *gb)
{
	if (!gb)
		return NULL;
	return gb->allocator;
}

size_t
fy_generic_builder_get_free(struct fy_generic_builder *gb)
{
	struct fy_allocator_info *info;
	size_t freesz;

	if (!gb)
		return 0;

	info = fy_allocator_get_info(fy_generic_builder_get_allocator(gb), FY_ALLOC_TAG_NONE);
	if (!info)
		return 0;
	freesz = info->free;	
	free(info);

	return freesz;
}

void fy_generic_builder_reset(struct fy_generic_builder *gb)
{
	if (!gb)
		return;

	if (gb->linear) {
		free(gb->linear);
		gb->linear = NULL;
	}

	if (gb->flags & FYGBF_CREATED_TAG)
		fy_allocator_reset_tag(gb->allocator, gb->alloc_tag);
}

bool fy_generic_builder_contains_out_of_place(struct fy_generic_builder *gb, fy_generic v)
{
	const void *ptr;

	/* invalids never contained */
	if (v.v == fy_invalid_value)
		return false;

	/* inplace always contained (implicitly) */
	if (fy_generic_is_in_place(v))
		return true;

	ptr = fy_generic_resolve_ptr(v);

	/* find if any of the builders in scope contain it */
	while (gb) {
		if (fy_allocator_contains(gb->allocator, gb->alloc_tag, ptr))
			return true;
		gb = gb->cfg.parent;
	}
	return false;
}

struct fy_generic_builder *
fy_generic_builder_get_scope_leader(struct fy_generic_builder *gb)
{
	while (gb && !(gb->flags & FYGBF_SCOPE_LEADER))
		gb = gb->cfg.parent;
	return gb;
}

struct fy_generic_builder *fy_generic_builder_get_export_builder(struct fy_generic_builder *gb)
{
	gb = fy_generic_builder_get_scope_leader(gb);
	if (!gb)
		return NULL;
	return gb->cfg.parent;
}

fy_generic fy_generic_builder_export(struct fy_generic_builder *gb, fy_generic v)
{
	struct fy_generic_builder *gb_export;

	gb_export = fy_generic_builder_get_export_builder(gb);
	if (!gb_export)
		return fy_invalid;
	return fy_gb_internalize(gb_export, v);
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
	fy_generic vi, new_v;
	const fy_generic_collection *colp;
	struct iovec iov[2];
	enum fy_generic_type type;
	size_t size, i, count;
	const void *valp;
	const uint8_t *str, *p;
	size_t len;
	const fy_generic *itemss;
	fy_generic *items;

	if (v.v == fy_invalid_value)
		return fy_invalid;

	/* if it's in place just return it */
	if (fy_generic_is_in_place(v))
		return v;

	/* either in place, or contained in scope */
	if (fy_generic_builder_contains(gb, v))
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
	valp = NULL;
	p = fy_generic_resolve_ptr(v);
	switch (type) {
	case FYGT_INT:
		if (!p)
			break;
		valp = fy_gb_lookup(gb, p, sizeof(fy_generic_decorated_int),
				FY_SCALAR_ALIGNOF(fy_generic_decorated_int));
		if (!valp)
			valp = fy_gb_store(gb, p, sizeof(fy_generic_decorated_int),
					FY_SCALAR_ALIGNOF(fy_generic_decorated_int));
		if (!valp)
			break;

		new_v = (fy_generic){ .v = (uintptr_t)valp | FY_INT_OUTPLACE_V };
		break;

	case FYGT_FLOAT:
		if (!p)
			break;
		valp = fy_gb_lookup(gb, p, sizeof(double), FY_SCALAR_ALIGNOF(double));
		if (!valp)
			valp = fy_gb_store(gb, p, sizeof(double), FY_SCALAR_ALIGNOF(double));
		if (!valp)
			break;

		new_v = (fy_generic){ .v = (uintptr_t)valp | FY_FLOAT_OUTPLACE_V };
		break;

	case FYGT_STRING:
		if (!p)
			break;
		str = fy_decode_size(p, FYGT_SIZE_ENCODING_MAX, &len);
		if (!str)
			break;

		size = (size_t)(str - p) + len;

		valp = fy_gb_lookup(gb, p, size + 1, FY_GENERIC_SCALAR_ALIGN);
		if (!valp)
			valp = fy_gb_store(gb, p, size + 1, FY_GENERIC_SCALAR_ALIGN);
		if (!valp)
			break;

		new_v = (fy_generic){ .v = (uintptr_t)valp | FY_STRING_OUTPLACE_V };
		break;

	case FYGT_SEQUENCE:
	case FYGT_MAPPING:
		colp = fy_generic_resolve_collection_ptr(v);
		if (!colp || !colp->count) {
			new_v = type == FYGT_SEQUENCE ? fy_seq_empty : fy_map_empty;
			break;
		}
		itemss = fy_generic_collectionp_get_items(type, colp, &count);
		size = sizeof(*items) * count;

		iov[0].iov_base = (void *)colp;
		iov[0].iov_len = sizeof(*colp);
		iov[1].iov_base = (void *)itemss;
		iov[1].iov_len = size;

		valp = fy_gb_lookupv(gb, iov, ARRAY_SIZE(iov), FY_GENERIC_CONTAINER_ALIGN);
		if (!valp) {
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
				iov[1].iov_base = items;
				valp = fy_gb_storev(gb, iov, ARRAY_SIZE(iov),
							FY_GENERIC_CONTAINER_ALIGN);
			}

			if (size > COPY_MALLOC_CUTOFF)
				free(items);

			if (!valp)
				break;
		}

		new_v = (fy_generic){ .v = (uintptr_t)valp | (type == FYGT_SEQUENCE ? FY_SEQ_V : FY_MAP_V) };
		break;

	case FYGT_INVALID:
		break;

	case FYGT_ALIAS:
		/* impossible since it's indirect */
		FY_IMPOSSIBLE_ABORT();

	default:
		FY_IMPOSSIBLE_ABORT();
	}

	return new_v;
}

fy_generic fy_gb_validate_out_of_place(struct fy_generic_builder *gb, fy_generic v)
{
	fy_generic vi;
	const fy_generic_collection *colp;
	enum fy_generic_type type;
	size_t i, count;
	const fy_generic *itemss;

	if (v.v == fy_invalid_value)
		return fy_invalid;

	/* if it's in place just return it */
	if (fy_generic_is_in_place(v))
		return v;

	/* if it's builder contained, then it's valid */
	if (fy_generic_builder_contains(gb, v))
		return v;

	/* indirects are handled here (note, aliases are indirect too) */
	if (fy_generic_is_indirect(v)) {
		fy_generic_indirect gi;

		fy_generic_indirect_get(v, &gi);

		gi.value = fy_gb_validate(gb, gi.value);
		if (gi.value.v == fy_invalid_value)
			return fy_invalid;

		gi.anchor = fy_gb_validate(gb, gi.anchor);
		if (gi.anchor.v == fy_invalid_value)
			return fy_invalid;

		gi.tag = fy_gb_validate(gb, gi.tag);
		if (gi.tag.v == fy_invalid_value)
			return fy_invalid;

		return v;
	}

	type = fy_generic_get_type(v);

	if (type == FYGT_SEQUENCE || type == FYGT_MAPPING) {
		colp = fy_generic_resolve_collection_ptr(v);
		itemss = fy_generic_collectionp_get_items(type, colp, &count);
		for (i = 0; i < count; i++) {
			vi = fy_gb_validate(gb, itemss[i]);
			if (vi.v == fy_invalid_value)
				return fy_invalid;
		}
	}

	return v;
}

int fy_gb_validate_array(struct fy_generic_builder *gb, size_t count, const fy_generic *vp)
{
	fy_generic v;
	size_t i;

	for (i = 0; i < count; i++) {
		v = fy_gb_validate(gb, vp[i]);
		if (v.v == fy_invalid_value)
			return -1;
	}
	return 0;
}

fy_generic fy_validate_out_of_place(fy_generic v)
{
	return fy_gb_validate_out_of_place(NULL, v);
}

int fy_validate_array(size_t count, const fy_generic *vp)
{
	return fy_gb_validate_array(NULL, count, vp);
}

static int fy_generic_seqmap_qsort_cmp(const void *a, const void *b)
{
	const fy_generic *vpa = a, *vpb = b;
	/* the keys are first for maps */
	return fy_generic_compare(*vpa, *vpb);
}

#define FY_GB_OP_IOV_INPLACE		8
#define FY_GB_OP_ITEMS_INPLACE		64
#define FY_GB_OP_WORK_ITEMS_INPLACE	64

/* Threshold for parallelization - below this, use sequential */
#define FY_PARALLEL_THRESHOLD		100

typedef union {
	fy_generic_filter_pred_fn filter_pred;
	fy_generic_map_xform_fn map_xform;
	fy_generic_reducer_fn reducer;
	void (*fn)(void);
#if defined(__BLOCKS__)
	fy_generic_filter_pred_block filter_pred_blk;
	fy_generic_map_xform_block map_xform_blk;
	fy_generic_reducer_block reducer_blk;
	void (^blk)(void);
#endif
} fy_op_fn;

struct fy_op_work_arg {
	unsigned int op;
	struct fy_generic_builder *gb;
	enum fy_generic_type type;
#if defined(__BLOCKS__)
	bool is_block;
#endif
	fy_op_fn fn;
	fy_generic *work_items;
	size_t work_item_count;
	size_t removed_items;
	fy_generic vresult;
};

static void
fy_op_map_filter_work(void *varg)
{
	struct fy_op_work_arg *arg = varg;
	size_t i, j, stride;
	fy_generic key, value;
	bool ok;

	/* unified traversal for seq/map */
	stride = arg->type == FYGT_SEQUENCE ? 1 : 2;
	j = 0;
	key = fy_invalid;
	for (i = 0; i < arg->work_item_count; i += stride) {
		if (stride <= 1) {
			value = arg->work_items[i];
		} else {
			key = arg->work_items[i + 0];
			value = arg->work_items[i + 1];
		}
		switch (arg->op) {
		case FYGBOP_FILTER:
#if defined(__BLOCKS__)
			if (arg->is_block)
				ok = arg->fn.filter_pred_blk(arg->gb, value);
			else
#endif
				ok = arg->fn.filter_pred(arg->gb, value);

			if (!ok) {
				key = value = fy_invalid;
				j += stride;
			}
			break;
		case FYGBOP_MAP:
#if defined(__BLOCKS__)
			if (arg->is_block)
				value = arg->fn.map_xform_blk(arg->gb, value);
			else
#endif
				value = arg->fn.map_xform(arg->gb, value);
			// an invalid value stops everything
			if (value.v == fy_invalid_value) {
				arg->vresult = fy_invalid;
				return;
			}
			break;
		case FYGBOP_MAP_FILTER:
#if defined(__BLOCKS__)
			if (arg->is_block)
				value = arg->fn.map_xform_blk(arg->gb, value);
			else
#endif
				value = arg->fn.map_xform(arg->gb, value);
			// an invalid value removes element
			if (value.v == fy_invalid_value) {
				key = fy_invalid;
				j += stride;
			}
			break;
		default:
			break;
		}
		if (stride <= 1) {
			arg->work_items[i] = value;
		} else {
			arg->work_items[i + 0] = key;
			arg->work_items[i + 1] = value;
		}
	}
	arg->removed_items = j;
	arg->vresult = fy_true;
}

static void
fy_op_reduce_work(void *varg)
{
	struct fy_op_work_arg *arg = varg;
	size_t i, stride, valoffset;
	fy_generic acc;

	/* unified traversal for seq/map */
	stride = arg->type == FYGT_SEQUENCE ? 1 : 2;
	valoffset = stride - 1;
	acc = arg->vresult;
	for (i = 0; i < arg->work_item_count; i += stride) {
#if defined(__BLOCKS__)
		if (arg->is_block)
			acc = arg->fn.reducer_blk(arg->gb, acc, arg->work_items[i + valoffset]);
		else
#endif
			acc = arg->fn.reducer(arg->gb, acc, arg->work_items[i + valoffset]);
		if (acc.v == fy_invalid_value)
			break;
	}
	arg->vresult = acc;
}

struct fy_generic_op_desc;

typedef fy_generic (*fy_generic_op_handler)(const struct fy_generic_op_desc *desc,
					    struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
					    fy_generic in, const struct fy_generic_op_args *args);

struct fy_generic_op_desc {
	unsigned int op;
	enum fy_gb_op_flags flags_mask;
	const char *op_name;
	enum fy_generic_type_mask in_mask;
	enum fy_generic_type_mask out_mask;
	fy_generic_op_handler handler;
};

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

static inline fy_generic
fy_generic_op_internalize(struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
			  fy_generic v)
{
	if (flags & FYGBOPF_NO_CHECKS)
		return v;

	if (flags & FYGBOPF_DONT_INTERNALIZE)
		return fy_gb_validate(gb, v);

	return fy_gb_internalize(gb, v);
}

/* iov_flags */
#define FYGCODF_IOV_DIRECT	FY_BIT(0)	/* direct IOV from input (safe to do) */

struct fy_generic_collection_op_data {
	struct fy_generic_builder *gb;
	enum fy_gb_op_flags flags;
	fy_generic in;
	unsigned xflags;
	fy_generic_value col_mark;
	size_t col_item_size;
	const struct fy_generic_op_args *args;
	enum fy_generic_type type;		// in type
	enum fy_generic_type out_type;		// out
	size_t count;	// this is the count of the collection
	size_t item_count;
	const fy_generic *items;
	size_t in_count;
	const fy_generic *in_items;
	size_t in_item_count;
	unsigned int iov_flags;
	const struct iovec *iov;
	int iovcnt;
	size_t iov_item_count;
	const fy_generic *iov_items;
	fy_generic *iov_items_alloc;
	fy_generic iov_items_local[32];
	struct iovec iov_local[2];
	size_t work_item_all_count;
	fy_generic work_items_local[32];
	fy_generic *work_items_alloc;
	fy_generic *work_items_all;
	size_t work_in_items_offset;
	fy_generic *work_in_items;
	size_t work_items_offset;
	fy_generic *work_items;
	size_t work_items_expanded_offset;
	size_t work_items_expanded_count;
	fy_generic *work_items_expanded;
	size_t work_in_items_div2_offset;
	fy_generic *work_in_items_div2;
};

void fy_generic_collection_op_data_cleanup(struct fy_generic_collection_op_data *cod)
{
	if (cod->iov_items_alloc) {
		free(cod->iov_items_alloc);
		cod->iov_items_alloc = NULL;
	}
	if (cod->work_items_alloc) {
		free(cod->work_items_alloc);
		cod->work_items_alloc = NULL;
	}
}

#define FYGCODSF_MAP_ITEM_COUNT_NO_MULT2	FY_BIT(0)
#define FYGCODSF_NEED_WORK_IN_ITEMS		FY_BIT(1)
#define FYGCODSF_NEED_WORK_ITEMS		FY_BIT(2)
#define FYGCODSF_NEED_WORK_ITEMS_EXPANDED	FY_BIT(3)		
#define FYGCODSF_NEED_COPY_WORK_IN_ITEMS	FY_BIT(4)
#define FYGCODSF_NEED_COPY_WORK_ITEMS		FY_BIT(5)
#define FYGCODSF_NEED_COPY_WORK_ITEMS_EXPANDED	FY_BIT(6)
#define FYGCODSF_NEED_WORK_IN_ITEMS_DIV2	FY_BIT(7)

int fy_generic_collection_op_data_setup(struct fy_generic_collection_op_data *cod,
		struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		fy_generic in, enum fy_generic_type out_type, const struct fy_generic_op_args *args,
		unsigned int xflags)
{
	size_t j, k, tmp_item_count;
	const fy_generic *tmp_items;

	memset(cod, 0, sizeof(*cod));
	cod->gb = gb;
	cod->flags = flags;
	cod->in = in;
	cod->args = args;
	cod->xflags = xflags;

	cod->count = args->common.count;
	cod->items = args->common.items;

	cod->in_items = fy_generic_collection_get_items(in, &cod->in_item_count);

	cod->type = fy_generic_get_type(in);
	if (out_type == FYGT_INVALID)
		out_type = cod->type;
	cod->out_type = out_type;

	switch (cod->out_type) {
	case FYGT_NULL:
	case FYGT_BOOL:
	case FYGT_INT:
	case FYGT_FLOAT:
	case FYGT_STRING:
		/* don't do anything */
		break;
	case FYGT_SEQUENCE:
		cod->col_mark = FY_SEQ_V;
		cod->col_item_size = sizeof(fy_generic);
		break;

	case FYGT_MAPPING:
		cod->col_mark = FY_MAP_V;
		cod->col_item_size = 2 * sizeof(fy_generic);
		break;
	default:
		goto err_out;
	}

	switch (cod->type) {
	case FYGT_SEQUENCE:
		cod->item_count = cod->count;
		cod->in_count = cod->in_item_count;
		break;

	case FYGT_MAPPING:
		cod->in_count = cod->in_item_count >> 1;
		/* count was given in items, not pairs */
		if (flags & FYGBOPF_MAP_ITEM_COUNT) {
			/* it must not be odd */
			if (cod->count & 1)
				goto err_out;
			cod->count >>= 1;
		}
		if (xflags & FYGCODSF_MAP_ITEM_COUNT_NO_MULT2)
			cod->item_count = cod->count;
		else
			cod->item_count = MULSZ(cod->count, 2);
		break;
	default:
		goto err_out;
	}

	cod->work_item_all_count = 0;
	if (xflags & FYGCODSF_NEED_WORK_IN_ITEMS) {
		cod->work_in_items_offset = cod->work_item_all_count;
		cod->work_item_all_count += cod->in_item_count;
	}
	if (xflags & FYGCODSF_NEED_WORK_ITEMS) {
		cod->work_items_offset = cod->work_item_all_count;
		cod->work_item_all_count += cod->item_count;
	}
	if (xflags & FYGCODSF_NEED_WORK_IN_ITEMS_DIV2) {
		cod->work_in_items_div2_offset = cod->work_item_all_count;
		cod->work_item_all_count += cod->in_item_count / 2;
	}
	if (xflags & FYGCODSF_NEED_WORK_ITEMS_EXPANDED) {
		cod->work_items_expanded_offset = cod->work_item_all_count;
		cod->work_items_expanded_count = 0;
		for (j = 0; j < cod->item_count; j++) {
			(void)fy_generic_collection_get_items(cod->items[j], &tmp_item_count);
			cod->work_items_expanded_count += tmp_item_count;
		}
		cod->work_item_all_count += cod->work_items_expanded_count;
	}

	if (cod->work_item_all_count > 0) {
		if (cod->work_item_all_count <= ARRAY_SIZE(cod->work_items_local))
			cod->work_items_all = cod->work_items_local;
		else {
			cod->work_items_alloc = malloc(sizeof(*cod->work_items_alloc) * cod->work_item_all_count);
			if (!cod->work_items_alloc)
				goto err_out;
			cod->work_items_all = cod->work_items_alloc;
		}

		if (xflags & FYGCODSF_NEED_WORK_IN_ITEMS)
			cod->work_in_items = cod->work_items_all + cod->work_in_items_offset;

		if (xflags & FYGCODSF_NEED_WORK_ITEMS)
			cod->work_items = cod->work_items_all + cod->work_items_offset;

		if (xflags & FYGCODSF_NEED_WORK_ITEMS_EXPANDED)
			cod->work_items_expanded = cod->work_items_all + cod->work_items_expanded_offset;

		if (xflags & FYGCODSF_NEED_WORK_IN_ITEMS_DIV2)
			cod->work_in_items_div2 = cod->work_items_all + cod->work_in_items_div2_offset;

		if (xflags & FYGCODSF_NEED_COPY_WORK_IN_ITEMS)
			memcpy(cod->work_in_items, cod->in_items, sizeof(*cod->work_items) * cod->in_item_count);

		if (xflags & FYGCODSF_NEED_COPY_WORK_ITEMS)
			memcpy(cod->work_items, cod->items, sizeof(*cod->work_items) * cod->item_count);

		if (xflags & FYGCODSF_NEED_COPY_WORK_ITEMS_EXPANDED) {
			k = 0;
			for (j = 0; j < cod->item_count; j++) {
				tmp_items = fy_generic_collection_get_items(cod->items[j], &tmp_item_count);
				if (tmp_item_count > 0) {
					memcpy(cod->work_items_expanded + k, tmp_items,
							sizeof(*cod->work_items_expanded) * tmp_item_count);
					k += tmp_item_count;
				}
			}
		}
	}

	return 0;
err_out:
	return -1;
}

static inline int
fy_generic_collection_op_prepare_iov(struct fy_generic_collection_op_data *cod,
				     const struct iovec *iov, const int iovcnt)
{
	int i;
	fy_generic v;
	size_t j, count, len, idx;
	const fy_generic *vp;
	fy_generic *items;

	if (iovcnt < 1 || !iov)
		goto err_out;

	/* the first iov must be a single size_t */
	if (iov[0].iov_len != sizeof(struct fy_generic_collection))
		goto err_out;

	/* calculate totals */
	cod->iov_item_count = 0;
	for (i = 1; i < iovcnt; i++) {
		len = iov[i].iov_len;
		/* must be a multiple of sizeof(fy_generic) */
		if (len % (sizeof(fy_generic)))
			goto err_out;
		count = len / sizeof(fy_generic);
		cod->iov_item_count += count;
	}

	cod->iov_flags = 0;
	/* pretend it's direct - will turn off if not so */
	cod->iov_flags |= FYGCODF_IOV_DIRECT;

	/* if no checks, trust (and don't verify) */
	if (!(cod->flags & FYGBOPF_NO_CHECKS)) {
		for (i = 1; i < iovcnt; i++) {
			count = iov[i].iov_len / sizeof(fy_generic);
			vp = iov[i].iov_base;
			for (idx = 0; idx < count; idx++) {
				v = vp[idx];

				/* invalid? error out immediately */
				if (fy_generic_is_invalid(v))
					goto err_out;

				/* if in place or contained in builder we're fine */
				if (fy_generic_is_in_place(v) || fy_generic_builder_contains(cod->gb, v))
					continue;

				/* not direct */
				cod->iov_flags &= ~FYGCODF_IOV_DIRECT;
				break;
			}
		}
	}

	/* the easiest case, we can use the iov provided */
	if (cod->iov_flags & FYGCODF_IOV_DIRECT) {
		cod->iov = iov;
		cod->iovcnt = iovcnt;
		return 0;
	}

	/* we have to internalize each */

	/* get workspace */
	if (cod->iov_item_count <= ARRAY_SIZE(cod->iov_items_local)) {
		items = cod->iov_items_local;
	} else {
		cod->iov_items_alloc = malloc(sizeof(*cod->iov_items) * cod->iov_item_count);
		if (!cod->iov_items_alloc)
			goto err_out;
		items = cod->iov_items_alloc;
	}
	cod->iov_items = items;

	/* internalize items one by one */
	for (j = 0, i = 1; i < iovcnt; i++) {
		count = iov[i].iov_len / sizeof(fy_generic);
		vp = iov[i].iov_base;
		for (idx = 0; idx < count; idx++) {
			v = fy_generic_op_internalize(cod->gb, cod->flags, vp[idx]);
			if (fy_generic_is_invalid(v))
				goto err_out;
			items[j++] = v;
		}
	}
	assert(j == cod->iov_item_count);

	/* copy the collation header iov */
	cod->iov_local[0].iov_base = iov[0].iov_base;
	cod->iov_local[0].iov_len = iov[0].iov_len;
	/* and the rest is a single span */
	cod->iov_local[1].iov_base = items;
	cod->iov_local[1].iov_len = MULSZ(cod->iov_item_count, sizeof(fy_generic));

	/* and this is now our iov */
	cod->iov = cod->iov_local;
	cod->iovcnt = 2;

	return 0;

err_out:
	if (cod->iov_items_alloc) {
		free(cod->iov_items_alloc);
		cod->iov_items_alloc = NULL;
	}
	return -1;
}

static inline fy_generic
fy_generic_collection_op_data_out(struct fy_generic_collection_op_data *cod,
				  const struct iovec *iov, int iovcnt)
{
	fy_generic v;
	const void *p;
	int rc;

	rc = fy_generic_collection_op_prepare_iov(cod, iov, iovcnt);
	if (rc)
		goto err_out;

	/* update with what was generated */
	iov = cod->iov;
	iovcnt = cod->iovcnt;

	p = fy_gb_lookupv(cod->gb, iov, iovcnt, FY_GENERIC_CONTAINER_ALIGN);
	if (!p)
		p = fy_gb_storev(cod->gb, iov, iovcnt, FY_GENERIC_CONTAINER_ALIGN);
	if (!p)
		goto err_out;

	v = (fy_generic){ .v = (uintptr_t)p | cod->col_mark };

	return v;

err_out:
	return fy_invalid;
}

static const struct fy_generic_op_desc op_descs[FYGBOP_COUNT];

static fy_generic
fy_generic_op_create_inv(const struct fy_generic_op_desc *desc,
			 struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
			 fy_generic in, const struct fy_generic_op_args *args)
{
	return fy_invalid;
}

static fy_generic
fy_generic_op_create_null(const struct fy_generic_op_desc *desc,
			  struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
			  fy_generic in, const struct fy_generic_op_args *args)
{
	return fy_null;
}

static fy_generic
fy_generic_op_create_bool(const struct fy_generic_op_desc *desc,
			  struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
			  fy_generic in, const struct fy_generic_op_args *args)
{
	return args->scalar.bval ? fy_true : fy_false;
}

static fy_generic
fy_generic_op_create_int(const struct fy_generic_op_desc *desc,
			  struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
			  fy_generic in, const struct fy_generic_op_args *args)
{
	return fy_value(gb, args->scalar.ival);
}

static fy_generic
fy_generic_op_create_float(const struct fy_generic_op_desc *desc,
			  struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
			  fy_generic in, const struct fy_generic_op_args *args)
{
	return fy_gb_to_generic(gb, args->scalar.fval);
}

static fy_generic
fy_generic_op_create_string(const struct fy_generic_op_desc *desc,
			  struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
			  fy_generic in, const struct fy_generic_op_args *args)
{
	return fy_gb_to_generic(gb, args->scalar.sval);
}

static fy_generic
fy_generic_op_create_sequence(const struct fy_generic_op_desc *desc,
			  struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
			  fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_sequence seqh;
	struct iovec iov[2];
	fy_generic out;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			fy_seq_empty, FYGT_SEQUENCE, args, 0);
	if (rc)
		return fy_invalid;

	if (!args->common.count) {
		out = fy_seq_empty;
		goto out;
	}

	seqh.count = args->common.count;
	iov[0].iov_base = &seqh;
	iov[0].iov_len = sizeof(seqh);
	iov[1].iov_base = (void *)args->common.items;
	iov[1].iov_len = MULSZ(seqh.count, sizeof(fy_generic));
	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;
err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_create_mapping(const struct fy_generic_op_desc *desc,
			  struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
			  fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_mapping maph;
	struct iovec iov[2];
	size_t count;
	fy_generic out;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			fy_map_empty, FYGT_MAPPING, args, 0);
	if (rc)
		return fy_invalid;

	if (!args->common.count) {
		out = fy_map_empty;
		goto out;
	}

	count = args->common.count;

	/* count was given in items, not pairs */
	if (flags & FYGBOPF_MAP_ITEM_COUNT) {
		/* it must not be odd */
		if (count & 1)
			goto err_out;
		count >>= 1;
	}

	maph.count = count;
	iov[0].iov_base = &maph;
	iov[0].iov_len = sizeof(maph);
	iov[1].iov_base = (void *)args->common.items;
	iov[1].iov_len = MULSZ(maph.count, (2 * sizeof(fy_generic)));
	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;
err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_insert(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_collection col;
	struct iovec iov[4];
	size_t idx, remain_count, out_count;
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_INVALID, args, 0);
	if (rc)
		return fy_invalid;

	if (!args->common.count) {
		out = fy_generic_op_internalize(gb, flags, in);
		goto out;
	}

	if (!args->common.items)
		goto err_out;

	idx = args->insert_replace_get_set_at.idx;
	if (idx > cod->in_count)
		idx = cod->in_count;

	remain_count = cod->in_count - idx;
	out_count = ADDSZ(cod->in_count, cod->count);

	/* sequence overlaps map counter */
	col.count = out_count;
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);
	/* before */
	iov[1].iov_base = (void *)cod->in_items;
	iov[1].iov_len = MULSZ(idx, cod->col_item_size);
	/* replacement */
	iov[2].iov_base = (void *)cod->items;
	iov[2].iov_len = MULSZ(cod->count, cod->col_item_size);
	/* after */
	iov[3].iov_base = (void *)cod->in_items + MULSZ(idx, cod->col_item_size);
	iov[3].iov_len = MULSZ(remain_count, cod->col_item_size);

	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_replace(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_collection col;
	struct iovec iov[4];
	size_t idx, remain_idx, remain_count, out_count, tmp;
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_INVALID, args, 0);
	if (rc)
		return fy_invalid;

	if (!args->common.count) {
		out = fy_generic_op_internalize(gb, flags, in);
		goto out;
	}

	if (!args->common.items)
		goto err_out;

	idx = args->insert_replace_get_set_at.idx;

	/* index over the limits is at the end */
	if (idx > cod->in_count)
		idx = cod->in_count;

	tmp = ADDSZ(idx, cod->count);
	if (tmp > cod->in_count) {
		out_count = tmp;
		remain_idx = cod->in_count;
	} else {
		out_count = cod->in_count;
		remain_idx = tmp;
	}
	remain_count = cod->in_count - remain_idx;

	/* sequence overlaps map counter */
	col.count = out_count;
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);
	/* before */
	iov[1].iov_base = (void *)cod->in_items;
	iov[1].iov_len = MULSZ(idx, cod->col_item_size);
	/* replacement */
	iov[2].iov_base = (void *)cod->items;
	iov[2].iov_len = MULSZ(cod->count, cod->col_item_size);
	/* after */
	iov[3].iov_base = (void *)cod->in_items + MULSZ(remain_idx, cod->col_item_size);
	iov[3].iov_len = MULSZ(remain_count, cod->col_item_size);

	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_append(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_collection col;
	struct iovec iov[3];
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_INVALID, args, 0);
	if (rc)
		return fy_invalid;

	if (!args->common.count) {
		out = fy_generic_op_internalize(gb, flags, in);
		goto out;
	}

	if (!args->common.items)
		goto err_out;

	/* sequence overlaps map counter */
	col.count = ADDSZ(cod->in_count, cod->count);
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);
	/* before */
	iov[1].iov_base = (void *)cod->in_items;
	iov[1].iov_len = MULSZ(cod->in_count, cod->col_item_size);
	/* append */
	iov[2].iov_base = (void *)cod->items;
	iov[2].iov_len = MULSZ(cod->count, cod->col_item_size);

	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_assoc(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_collection col;
	struct iovec iov[2];
	fy_generic out = fy_invalid;
	size_t i, j, left_item_count;
	fy_generic key, value;
	int rc;

	/* copy in + items -> work_items */
	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_INVALID, args, 
			FYGCODSF_NEED_WORK_IN_ITEMS | FYGCODSF_NEED_WORK_ITEMS |
			FYGCODSF_NEED_COPY_WORK_ITEMS);
	if (rc)
		return fy_invalid;

	/* only mappings */
	if (!fy_generic_is_mapping(in))
		goto err_out;

	if (!args->common.count) {
		out = fy_generic_op_internalize(gb, flags, in);
		goto out;
	}

	if (!args->common.items)
		goto err_out;

	/* go over the keys in the input mapping */
	/* replaces the values by what was in items */
	left_item_count = cod->item_count;
	for (i = 0; i < cod->in_item_count; i += 2) {
		key = cod->in_items[i + 0];
		value = cod->in_items[i + 1];
		if (left_item_count > 0) {
			for (j = 0; j < cod->item_count; j += 2) {
				if (fy_generic_compare(cod->work_items[j + 0], key))
					continue;
				value = cod->work_items[j + 1];
				/* remove it from the items */
				cod->work_items[j + 0] = fy_invalid;
				cod->work_items[j + 1] = fy_invalid;
				left_item_count -= 2;
				break;
			}
		}
		cod->work_items_all[i + 0] = key;
		cod->work_items_all[i + 1] = value;
	}
	/* now append whatever is left */
	if (left_item_count > 0) {
		for (j = 0; j < cod->item_count; j += 2) {
			key = cod->work_items[j + 0];
			if (key.v == fy_invalid_value)
				continue;
			cod->work_items_all[i + 0] = key;
			cod->work_items_all[i + 1] = cod->work_items[j + 1];
			i += 2;
		}
	}
	/* the collection header */
	col.count = i / 2;
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);
	/* the content */
	iov[1].iov_base = cod->work_items_all;
	iov[1].iov_len = MULSZ(col.count, cod->col_item_size);

	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_disassoc(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_collection col;
	struct iovec iov[2];
	fy_generic out = fy_invalid;
	size_t i, j, k, left_item_count;
	fy_generic key, value;
	int rc;

	/* copy in + items -> work_items */
	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_INVALID, args, 
			FYGCODSF_MAP_ITEM_COUNT_NO_MULT2 |
			FYGCODSF_NEED_WORK_IN_ITEMS | FYGCODSF_NEED_WORK_ITEMS |
			FYGCODSF_NEED_COPY_WORK_ITEMS);
	if (rc)
		return fy_invalid;

	/* only mappings */
	if (!fy_generic_is_mapping(in))
		goto err_out;

	if (!args->common.count) {
		out = fy_generic_op_internalize(gb, flags, in);
		goto out;
	}

	if (!args->common.items)
		goto err_out;

	/* go over the keys in the input mapping */
	/* replaces the values by what was in items */
	left_item_count = cod->item_count;
	for (i = 0, k = 0; i < cod->in_item_count; i += 2) {
		key = cod->in_items[i + 0];
		value = cod->in_items[i + 1];
		if (left_item_count > 0) {
			for (j = 0; j < cod->item_count; j += 2) {
				if (fy_generic_compare(cod->work_items[j], key))
					continue;
				cod->work_items[j] = fy_invalid;
				left_item_count--;
				break;
			}
			if (j < cod->item_count)
				continue;
		}
		cod->work_items_all[k + 0] = key;
		cod->work_items_all[k + 1] = value;
		k += 2;
	}
	/* if everything is gone, don't bother */
	if (!k) {
		out = fy_map_empty;
		goto out;
	}

	/* nothing changed... */
	if (left_item_count == cod->item_count) {
		out = fy_generic_op_internalize(gb, flags, in);
		goto out;
	}

	/* the collection header */
	col.count = k / 2;
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);
	/* the content */
	iov[1].iov_base = cod->work_items_all;
	iov[1].iov_len = MULSZ(col.count, cod->col_item_size);

	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_keys(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_collection col;
	size_t i, j;
	struct iovec iov[2];
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_SEQUENCE, args,
			FYGCODSF_NEED_WORK_IN_ITEMS_DIV2);
	if (rc)
		return fy_invalid;

	/* only mappings */
	if (!fy_generic_is_mapping(in))
		goto err_out;

	/* nothing? return empty sequence */
	j = cod->in_count;
	if (!j) {
		out = fy_seq_empty;
		goto out;
	}
	/* fill in keys */
	for (i = 0; i < j; i++)
		cod->work_in_items_div2[i] = cod->in_items[i * 2 + 0];

	col.count = j;
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);
	/* the content */
	iov[1].iov_base = cod->work_in_items_div2;
	iov[1].iov_len = MULSZ(col.count, cod->col_item_size);

	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_values(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_collection col;
	size_t i, j;
	struct iovec iov[2];
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_SEQUENCE, args,
			FYGCODSF_NEED_WORK_IN_ITEMS_DIV2);
	if (rc)
		return fy_invalid;

	/* only mappings */
	if (!fy_generic_is_mapping(in))
		goto err_out;

	/* nothing? return empty sequence */
	j = cod->in_count;
	if (!j) {
		out = fy_seq_empty;
		goto out;
	}
	/* fill in values */
	for (i = 0; i < j; i++)
		cod->work_in_items_div2[i] = cod->in_items[i * 2 + 1];

	col.count = j;
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);
	/* the content */
	iov[1].iov_base = cod->work_in_items_div2;
	iov[1].iov_len = MULSZ(col.count, cod->col_item_size);

	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_items(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_collection col;
	size_t i, j;
	struct iovec iov[2];
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_SEQUENCE, args,
			FYGCODSF_NEED_WORK_IN_ITEMS_DIV2);
	if (rc)
		return fy_invalid;

	/* only mappings */
	if (!fy_generic_is_mapping(in))
		goto err_out;

	/* nothing? return empty sequence */
	j = cod->in_count;
	if (!j) {
		out = fy_seq_empty;
		goto out;
	}
	/* fill in (key, value) */
	for (i = 0; i < j; i++) {
		cod->work_in_items_div2[i] = fy_gb_sequence(gb,
				cod->in_items[i * 2 + 0], cod->in_items[i * 2 + 1]);
		if (fy_generic_is_invalid(cod->work_in_items_div2[i]))
			goto err_out;
	}

	col.count = j;
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);
	/* the content */
	iov[1].iov_base = cod->work_in_items_div2;
	iov[1].iov_len = MULSZ(col.count, cod->col_item_size);

	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_contains(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	size_t i, j, k;
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_BOOL, args, FYGCODSF_MAP_ITEM_COUNT_NO_MULT2);
	if (rc)
		return fy_invalid;

	/* nothing? so it's not contained */
	if (!args->common.count) {
		out = fy_false;
		goto out;
	}

	if (!args->common.items)
		goto err_out;

	k = cod->type == FYGT_SEQUENCE ? 1 : 2;
	for (i = 0; i < cod->in_item_count; i += k) {
		for (j = 0; j < cod->item_count; j++) {
			if (!fy_generic_compare(cod->in_items[i], cod->items[j])) {
				out = fy_true;
				goto out;
			}
		}
	}
	out = fy_false;
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_concat(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_collection col;
	struct iovec *iov;
	struct iovec *iov_alloc = NULL;
	const fy_generic *tmp_items;
	size_t tmp_item_count;
	int iovcnt;
	size_t i, j, total;
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_INVALID, args, 0);
	if (rc)
		return fy_invalid;

	if (!args->common.count) {
		out = fy_generic_op_internalize(gb, flags, in);
		goto out;
	}

	if (!args->common.items)
		goto err_out;

	iovcnt = 1;	/* header */

	/* verify that the concatenated items are the same type, and get the total */
	total = 0;

	/* input */
	if (cod->in_item_count) {
		total += cod->in_item_count;
		iovcnt++;
	}
	/* and all the items */
	for (j = 0; j < cod->count; j++) {
		if (fy_generic_get_type(cod->items[j]) != cod->type)
			goto err_out;
		tmp_items = fy_generic_collection_get_items(cod->items[j], &tmp_item_count);
		if (tmp_item_count) {
			total += tmp_item_count;
			iovcnt++;
		}
	}
	/* nothing? just return empty */
	if (total == 0) {
		out = cod->type == FYGT_SEQUENCE ? fy_seq_empty : fy_map_empty;
		goto out;
	}

	assert(iovcnt > 1);
	if (iovcnt <= 16) {
		iov = alloca(sizeof(*iov) * iovcnt);
	} else {
		iov_alloc = malloc(sizeof(*iov) * iovcnt);
		if (!iov_alloc)
			goto err_out;
		iov = iov_alloc;
	}

	/* sequence overlaps map counter */
	i = 0;
	col.count = cod->type == FYGT_SEQUENCE ? total : (total / 2);
	iov[i].iov_base = &col;
	iov[i].iov_len = sizeof(col);
	i++;

	/* the in items */
	if (cod->in_item_count > 0) {
		iov[i].iov_base = (void *)cod->in_items;
		iov[i].iov_len = MULSZ(cod->in_item_count, sizeof(fy_generic));
		i++;
	}

	/* now all the items */
	for (j = 0; j < cod->count; j++) {
		tmp_items = fy_generic_collection_get_items(cod->items[j], &tmp_item_count);
		if (tmp_item_count > 0) {
			iov[i].iov_base = (void *)tmp_items;
			iov[i].iov_len = MULSZ(tmp_item_count, sizeof(fy_generic));
			i++;
		}
	}
	assert((int)i == iovcnt);

	out = fy_generic_collection_op_data_out(cod, iov, iovcnt);
out:
	if (iov_alloc)
		free(iov_alloc);
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_reverse(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_collection col;
	struct iovec iov[2];
	const fy_generic *tmp_items;
	size_t tmp_item_count;
	size_t i, j, k;
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_INVALID, args,
			FYGCODSF_NEED_WORK_IN_ITEMS | FYGCODSF_NEED_WORK_ITEMS_EXPANDED);
	if (rc)
		return fy_invalid;

	if (args->common.count && !args->common.items)
		goto err_out;

	k = 0;
	if (cod->type == FYGT_SEQUENCE) {
		/* the extra arguments */
		for (i = cod->item_count - 1; (ssize_t)i >= 0; i--) {
			if (!fy_generic_is_sequence(cod->items[i]))
				goto err_out;
			tmp_items = fy_generic_sequence_get_items(cod->items[i], &tmp_item_count);
			for (j = tmp_item_count - 1; (ssize_t)j >= 0; j--)
				cod->work_items_all[k++] = tmp_items[j];
		}
		/* the original */
		for (j = cod->in_item_count - 1; (ssize_t)j >= 0; j--)
			cod->work_items_all[k++] = cod->in_items[j];
	} else {
		/* the extra arguments */
		for (i = cod->item_count - 1; (ssize_t)i >= 0; i--) {
			if (!fy_generic_is_mapping(cod->items[i]))
				goto err_out;
			tmp_items = fy_generic_mapping_get_items(cod->items[i], &tmp_item_count);
			for (j = tmp_item_count - 2; (ssize_t)j >= 0; j -= 2) {
				cod->work_items_all[k++] = tmp_items[j];
				cod->work_items_all[k++] = tmp_items[j + 1];
			}
		}
		/* the original */
		for (i = cod->in_item_count - 2; (ssize_t)j >= 0; j -= 2) {
			cod->work_items_all[k++] = cod->in_items[j];
			cod->work_items_all[k++] = cod->in_items[j + 1];
		}
	}
	/* sanity? */
	assert(k == cod->work_item_all_count);

	/* nothing? return empty */
	if (k == 0)  {
		out = cod->type == FYGT_SEQUENCE ? fy_seq_empty : fy_map_empty;
		goto out;
	}

	/* the collection header */
	col.count = cod->type == FYGT_SEQUENCE ? k : (k / 2);
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);
	/* the content */
	iov[1].iov_base = cod->work_items_all;
	iov[1].iov_len = MULSZ(col.count, cod->col_item_size);

	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_merge(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_collection col;
	struct iovec iov[2];
	fy_generic key, value, key2;
	size_t i, j, k;
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_MAPPING, args,
			FYGCODSF_MAP_ITEM_COUNT_NO_MULT2 |
			FYGCODSF_NEED_WORK_IN_ITEMS | FYGCODSF_NEED_WORK_ITEMS_EXPANDED |
			FYGCODSF_NEED_COPY_WORK_IN_ITEMS | FYGCODSF_NEED_COPY_WORK_ITEMS_EXPANDED);
	if (rc)
		return fy_invalid;

	if (args->common.count && !args->common.items)
		goto err_out;

	if (cod->type != FYGT_MAPPING)
		goto err_out;

	/* verify that the extra arguments are mappings too */
	for (i = 0; i < cod->item_count; i++) {
		if (!fy_generic_is_mapping(cod->items[i]))
			goto err_out;
	}

	/* now go over each key in the work area, and compare with all the following */
	k = 0;
	for (i = 0; i < cod->work_item_all_count; i += 2) {
		key = cod->work_items_all[i + 0];
		value = cod->work_items_all[i + 1];

		for (j = i + 2; j < cod->work_item_all_count; j += 2) {
			key2 = cod->work_items_all[j + 0];
			if (key2.v == fy_invalid_value || fy_generic_compare(key, key2))
				continue;
			value = cod->work_items_all[j + 1];
			/* erase this pair */
			cod->work_items_all[j + 0] = fy_invalid;
			cod->work_items_all[j + 1] = fy_invalid;
			k += 2;
		}
		cod->work_items_all[i + 1] = value;
	}
	/* were there removed items? process removing invalids */
	if (k > 0) {
		k = 0;
		j = cod->work_item_all_count;
		for (i = 0; i < j; i += 2) {
			key = cod->work_items_all[i + 0];
			if (key.v == fy_invalid_value)
				continue;
			cod->work_items_all[k++] = key;
			cod->work_items_all[k++] = cod->work_items_all[i + 1];
		}
	} else
		k = cod->work_item_all_count;

	/* nothing? */
	if (k == 0) {
		out = fy_map_empty;
		goto out;
	}

	/* the collection header */
	col.count = k / 2;
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);
	/* the content */
	iov[1].iov_base = cod->work_items_all;
	iov[1].iov_len = MULSZ(col.count, cod->col_item_size);

	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_unique(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_collection col;
	struct iovec iov[2];
	fy_generic value, v;
	size_t i, j, k;
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_SEQUENCE, args,
			FYGCODSF_NEED_WORK_IN_ITEMS | FYGCODSF_NEED_WORK_ITEMS_EXPANDED |
			FYGCODSF_NEED_COPY_WORK_IN_ITEMS | FYGCODSF_NEED_COPY_WORK_ITEMS_EXPANDED);
	if (rc)
		return fy_invalid;

	if (args->common.count && !args->common.items)
		goto err_out;

	if (cod->type != FYGT_SEQUENCE)
		goto err_out;

	/* verify that the extra arguments are sequences too */
	for (i = 0; i < cod->item_count; i++) {
		if (!fy_generic_is_sequence(cod->items[i]))
			goto err_out;
	}

	/* now go over each item in the work area, and compare with all the following */
	k = 0;
	for (i = 0; i < cod->work_item_all_count; i++) {
		v = cod->work_items_all[i];
		for (j = i + 1; j < cod->work_item_all_count; j++) {
			value = cod->work_items_all[j];
			if (value.v == fy_invalid_value || fy_generic_compare(v, value))
				continue;
			cod->work_items_all[j] = fy_invalid;
			k++;
		}
	}

	/* if no removed items, return the original */
	if (!k) {
		out = fy_generic_op_internalize(gb, flags, in);
		goto out;
	}

	/* there were removed items */
	k = 0;
	for (i = 0; i < cod->work_item_all_count; i++) {
		v = cod->work_items_all[i];
		if (v.v == fy_invalid_value)
			continue;
		cod->work_items_all[k++] = v;
	}

	/* nothing? */
	if (k == 0) {
		out = fy_seq_empty;
		goto out;
	}

	/* the collection header */
	col.count = k;
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);
	/* the content */
	iov[1].iov_base = cod->work_items_all;
	iov[1].iov_len = MULSZ(col.count, cod->col_item_size);

	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_sort(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_collection col;
	struct iovec iov[2];
	size_t i, k;
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_INVALID, args,
			FYGCODSF_MAP_ITEM_COUNT_NO_MULT2 |
			FYGCODSF_NEED_WORK_IN_ITEMS | FYGCODSF_NEED_WORK_ITEMS_EXPANDED |
			FYGCODSF_NEED_COPY_WORK_IN_ITEMS | FYGCODSF_NEED_COPY_WORK_ITEMS_EXPANDED);
	if (rc)
		return fy_invalid;

	if (args->common.count && !args->common.items)
		goto err_out;

	/* nothing? return empty */
	if (cod->work_item_all_count == 0)  {
		out = cod->type == FYGT_SEQUENCE ? fy_seq_empty : fy_map_empty;
		goto out;
	}

	k = cod->work_item_all_count;
	if (cod->type == FYGT_SEQUENCE) {
		/* the extra arguments */
		for (i = 0; i < cod->item_count; i++) {
			if (!fy_generic_is_sequence(cod->items[i]))
				goto err_out;
		}

		qsort(cod->work_items_all, k, sizeof(fy_generic), fy_generic_seqmap_qsort_cmp);
	} else {
		/* the extra arguments */
		for (i = 0; i < cod->item_count; i++) {
			if (!fy_generic_is_mapping(cod->items[i]))
				goto err_out;
		}

		qsort(cod->work_items_all, k / 2, 2 * sizeof(fy_generic), fy_generic_seqmap_qsort_cmp);
	}

	/* the collection header */
	col.count = cod->type == FYGT_SEQUENCE ? k : (k / 2);
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);
	/* the content */
	iov[1].iov_base = cod->work_items_all;
	iov[1].iov_len = MULSZ(col.count, cod->col_item_size);

	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_set(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_collection col;
	struct iovec iov[2];
	size_t i, j, left_item_count;
	unsigned long long idx;
	fy_generic key, value;
	fy_generic items_local[64];
	fy_generic *items_alloc = NULL;
	fy_generic *items;
	size_t item_count;
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_INVALID, args, FYGCODSF_MAP_ITEM_COUNT_NO_MULT2);
	if (rc)
		return fy_invalid;

	if (args->common.count && !args->common.items)
		goto err_out;

	/* nothing, return in */
	if (!args->common.count) {
		out = fy_generic_op_internalize(gb, flags, in);
		goto out;
	}

	/* must be index/key, value */
	if (args->common.count & 1)
		goto err_out;

	/* find maximum item count (and verify that key is index) */
	item_count = cod->in_item_count;

	if (cod->type == FYGT_SEQUENCE) {
		for (i = 0; i < cod->item_count; i += 2) {
			idx = fy_cast(cod->items[i], (unsigned long long)-1);
			if ((signed long long)idx < 0)
				goto err_out;
			if (idx >= item_count)
				item_count = (size_t)idx + 1;
		}
	} else {
		/* for mapping, start with all the items  */
		item_count += cod->item_count;
	}

	if (item_count <= ARRAY_SIZE(items_local)) {
		items = items_local;
	} else {
		items_alloc = malloc(sizeof(*items) * item_count);
		if (!items_alloc)
			goto err_out;
		items = items_alloc;
	}

	/* fill up to in_item_count with the input */
	for (i = 0; i < cod->in_item_count; i++)
		items[i] = cod->in_items[i];

	if (cod->type == FYGT_SEQUENCE) {

		/* fill up to the maximum with fy_null */
		for (; i < item_count; i++)
			items[i] = fy_null;

		/* now fill in at the correct index */
		for (i = 0; i < cod->item_count; i += 2) {
			idx = fy_cast(cod->items[i], (unsigned long long)-1);
			j = (size_t)idx;
			assert(j < item_count);
			items[j] = cod->items[i + 1];
		}
	} else {
		/* tack on the items */
		for (j = 0; i < item_count; i++, j++)
			items[i] = cod->items[j];

		/* go over the keys in the input mapping */
		/* replaces the values by what was in items */
		left_item_count = cod->item_count;

		for (i = 0; i < cod->in_item_count; i += 2) {
			key = items[i + 0];
			value = items[i + 1];
			if (left_item_count > 0) {
				for (j = cod->in_item_count; j < item_count; j += 2) {
					if (fy_generic_compare(items[j + 0], key))
						continue;
					value = items[j + 1];
					/* remove it from the items */
					items[j + 0] = fy_invalid;
					items[j + 1] = fy_invalid;
					left_item_count -= 2;
					break;
				}
			}
			items[i + 0] = key;
			items[i + 1] = value;
		}
		/* now append whatever is left */
		if (left_item_count > 0) {
			for (j = cod->in_item_count; j < item_count; j += 2) {
				key = items[j + 0];
				if (key.v == fy_invalid_value)
					continue;
				items[i + 0] = key;
				items[i + 1] = items[j + 1];
				i += 2;
			}
		}
		item_count = i;
	}

	/* the collection header */
	col.count = cod->type == FYGT_SEQUENCE ? item_count : (item_count / 2);
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);
	/* the content */
	iov[1].iov_base = items;
	iov[1].iov_len = MULSZ(col.count, cod->col_item_size);

	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	if (items_alloc)
		free(items_alloc);
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static const struct fy_generic_op_desc op_descs[FYGBOP_COUNT] = {
	[FYGBOP_CREATE_INV] = {
		.op = FYGBOP_CREATE_INV,
		.op_name = "create_inv",
		.in_mask = FYGTM_ANY,
		.out_mask = FYGTM_INVALID,
		.handler = fy_generic_op_create_inv,
	},
	[FYGBOP_CREATE_NULL] = {
		.op = FYGBOP_CREATE_NULL,
		.op_name = "create_null",
		.in_mask = FYGTM_ANY,
		.out_mask = FYGTM_NULL,
		.handler = fy_generic_op_create_null,
	},
	[FYGBOP_CREATE_BOOL] = {
		.op = FYGBOP_CREATE_BOOL,
		.op_name = "create_null",
		.in_mask = FYGTM_ANY,
		.out_mask = FYGTM_BOOL,
		.handler = fy_generic_op_create_bool,
	},
	[FYGBOP_CREATE_INT] = {
		.op = FYGBOP_CREATE_INT,
		.op_name = "create_int",
		.in_mask = FYGTM_ANY,
		.out_mask = FYGTM_INT,
		.handler = fy_generic_op_create_int,
	},
	[FYGBOP_CREATE_FLT] = {
		.op = FYGBOP_CREATE_FLT,
		.op_name = "create_int",
		.in_mask = FYGTM_ANY,
		.out_mask = FYGTM_FLOAT,
		.handler = fy_generic_op_create_float,
	},
	[FYGBOP_CREATE_STR] = {
		.op = FYGBOP_CREATE_STR,
		.op_name = "create_str",
		.in_mask = FYGTM_ANY,
		.out_mask = FYGTM_STRING,
		.handler = fy_generic_op_create_string,
	},
	[FYGBOP_CREATE_SEQ] = {
		.op = FYGBOP_CREATE_SEQ,
		.op_name = "create_seq",
		.in_mask = FYGTM_ANY,
		.out_mask = FYGTM_SEQUENCE,
		.handler = fy_generic_op_create_sequence,
	},
	[FYGBOP_CREATE_MAP] = {
		.op = FYGBOP_CREATE_MAP,
		.op_name = "create_seq",
		.in_mask = FYGTM_ANY,
		.out_mask = FYGTM_MAPPING,
		.handler = fy_generic_op_create_mapping,
	},
	[FYGBOP_INSERT] = {
		.op = FYGBOP_INSERT,
		.op_name = "insert",
		.in_mask = FYGTM_COLLECTION,
		.out_mask = FYGTM_COLLECTION,
		.handler = fy_generic_op_insert,
	},
	[FYGBOP_REPLACE] = {
		.op = FYGBOP_REPLACE,
		.op_name = "replace",
		.in_mask = FYGTM_COLLECTION,
		.out_mask = FYGTM_COLLECTION,
		.handler = fy_generic_op_replace,
	},
	[FYGBOP_APPEND] = {
		.op = FYGBOP_APPEND,
		.op_name = "append",
		.in_mask = FYGTM_COLLECTION,
		.out_mask = FYGTM_COLLECTION,
		.handler = fy_generic_op_append,
	},
	[FYGBOP_ASSOC] = {
		.op = FYGBOP_ASSOC,
		.op_name = "assoc",
		.in_mask = FYGTM_MAPPING,
		.out_mask = FYGTM_MAPPING,
		.handler = fy_generic_op_assoc,
	},
	[FYGBOP_DISASSOC] = {
		.op = FYGBOP_DISASSOC,
		.op_name = "disassoc",
		.in_mask = FYGTM_MAPPING,
		.out_mask = FYGTM_MAPPING,
		.handler = fy_generic_op_disassoc,
	},
	[FYGBOP_KEYS] = {
		.op = FYGBOP_KEYS,
		.op_name = "keys",
		.in_mask = FYGTM_MAPPING,
		.out_mask = FYGTM_SEQUENCE,
		.handler = fy_generic_op_keys,
	},
	[FYGBOP_VALUES] = {
		.op = FYGBOP_VALUES,
		.op_name = "values",
		.in_mask = FYGTM_MAPPING,
		.out_mask = FYGTM_SEQUENCE,
		.handler = fy_generic_op_values,
	},
	[FYGBOP_ITEMS] = {
		.op = FYGBOP_ITEMS,
		.op_name = "items",
		.in_mask = FYGTM_MAPPING,
		.out_mask = FYGTM_SEQUENCE,
		.handler = fy_generic_op_items,
	},
	[FYGBOP_CONTAINS] = {
		.op = FYGBOP_CONTAINS,
		.op_name = "contains",
		.in_mask = FYGTM_COLLECTION,
		.out_mask = FYGTM_BOOL,
		.handler = fy_generic_op_contains,
	},
	[FYGBOP_CONCAT] = {
		.op = FYGBOP_CONCAT,
		.op_name = "concat",
		.in_mask = FYGTM_COLLECTION,	// concat works on mappings too
		.out_mask = FYGTM_COLLECTION,
		.handler = fy_generic_op_concat,
	},
	[FYGBOP_REVERSE] = {
		.op = FYGBOP_REVERSE,
		.op_name = "reverse",
		.in_mask = FYGTM_COLLECTION,	// reverse works on mappings too
		.out_mask = FYGTM_COLLECTION,
		.handler = fy_generic_op_reverse,
	},
	[FYGBOP_MERGE] = {
		.op = FYGBOP_MERGE,
		.op_name = "merge",
		.in_mask = FYGTM_MAPPING,
		.out_mask = FYGTM_MAPPING,
		.handler = fy_generic_op_merge,
	},
	[FYGBOP_UNIQUE] = {
		.op = FYGBOP_UNIQUE,
		.op_name = "unique",
		.in_mask = FYGTM_SEQUENCE,
		.out_mask = FYGTM_SEQUENCE,
		.handler = fy_generic_op_unique,
	},
	[FYGBOP_SORT] = {
		.op = FYGBOP_SORT,
		.op_name = "sort",
		.in_mask = FYGTM_COLLECTION,	// sort works on mappings too
		.out_mask = FYGTM_COLLECTION,
		.handler = fy_generic_op_sort,
	},
	[FYGBOP_SET] = {
		.op = FYGBOP_SET,
		.op_name = "set",
		.in_mask = FYGTM_COLLECTION,	// sort works on mappings too
		.out_mask = FYGTM_COLLECTION,
		.handler = fy_generic_op_set,
	},
};

fy_generic
fy_generic_op_args(struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		   fy_generic in, const struct fy_generic_op_args *args)
{
	struct iovec *iov, *iov_alloc = NULL, iov_buf[FY_GB_OP_IOV_INPLACE];
	fy_generic *items_alloc = NULL, items_buf[FY_GB_OP_ITEMS_INPLACE];
	fy_generic *work_items_alloc = NULL, work_items_buf[FY_GB_OP_WORK_ITEMS_INPLACE];
	fy_generic_collection col;
	fy_generic v;
	const void *p;
	int iovcnt, num_threads;
	fy_generic in_direct, out, key, value, acc;
	fy_generic_value col_mark;
	unsigned int op;
	size_t count, item_count, tmp_item_count, idx, i, j, k;
	size_t in_item_count, work_item_count;
	size_t col_item_size;
	const fy_generic *items, *in_items, *tmp_items;
	fy_generic *items_mod, *work_items;	/* modifiable items (for assoc/deassoc) */
	fy_op_fn fn;
	struct fy_thread_pool *tp = NULL;
	struct fy_thread_pool_cfg tp_cfg;
	bool need_work_items, need_items_mod, has_args, needs_thread_pool, owns_thread_pool;
	bool need_copy_work_items;
	enum fy_generic_type type;
	struct fy_op_work_arg *work_args;
	struct fy_thread_work *works;
	size_t chunk_size, start_idx, count_items;

	op = ((flags >> FYGBOPF_OP_SHIFT) & FYGBOPF_OP_MASK);
	if (op >= FYGBOP_COUNT)
		return fy_invalid;

	/* XXX dispatch */
	if (op < ARRAY_SIZE(op_descs) && op_descs[op].handler)
		return op_descs[op].handler(&op_descs[op], gb, flags, in, args);

	out = fy_invalid;
	idx = SIZE_MAX;
	memset(&fn, 0, sizeof(fn));

	need_work_items = false;
	need_copy_work_items = false;
	need_items_mod = !(flags & FYGBOPF_DONT_INTERNALIZE);	// if we internalize, need to mod
	has_args = true;
	needs_thread_pool = false;
	owns_thread_pool = false;
	items_mod = NULL;
	work_item_count = 0;
	acc = fy_invalid;

	/* common for all */
	count = args->common.count;
	items = args->common.items;
	tp = args->common.tp;

	/* load args */
	switch (op) {
	case FYGBOP_SET:
		if (!count)
			return in;
		if (!items)
			return fy_invalid;
		break;

	case FYGBOP_FILTER:
	case FYGBOP_MAP:
	case FYGBOP_MAP_FILTER:
	case FYGBOP_REDUCE:
		if (!(flags & FYGBOPF_BLOCK_FN)) {
			fn.fn = args->filter_map_reduce_common.fn;
			if (!fn.fn)
				return fy_invalid;
		} else {
#if defined(__BLOCKS__)
			fn.blk = args->filter_map_reduce_common.blk;
			if (!fn.blk)
				return fy_invalid;
#else
			return fy_invalid;
#endif
		}
		if (op == FYGBOP_REDUCE)
			acc = args->reduce.acc;
		if (count && !items)
			return fy_invalid;
		if (op == FYGBOP_REDUCE)
			flags |= FYGBOPF_DONT_INTERNALIZE;	/* don't internalize for reduce */
		need_work_items = true;
		need_copy_work_items = true;
		break;

	case FYGBOP_GET:
		/* get needs a key */
		if (!count || !items)
			return fy_invalid;
		break;

	case FYGBOP_GET_AT:
		idx = args->insert_replace_get_set_at.idx;
		need_work_items = false;
		has_args = false;
		break;

	case FYGBOP_GET_AT_PATH:
		if (!count)
			return in;

		if (!items)
			return fy_invalid;
		need_work_items = false;
		flags |= FYGBOPF_DONT_INTERNALIZE;
		break;

	case FYGBOP_SET_AT:
		idx = args->insert_replace_get_set_at.idx;
		if (!count || !items)
			return fy_invalid;
		break;

	case FYGBOP_SET_AT_PATH:
		/* set_at_path needs path components and value */
		if (count < 1 || !items)
			return fy_invalid;
		need_work_items = true;
		break;

	case FYGBOP_PARSE:
		/* parse doesn't require items */
		break;

	case FYGBOP_EMIT:
		/* emit doesn't require items */
		break;

	default:
		goto err_out;
	}

	/* internalize or validate */
	v = in;
	if (!(flags & FYGBOPF_NO_CHECKS))
		v = !(flags & FYGBOPF_DONT_INTERNALIZE) ?
			fy_gb_internalize(gb, v) : fy_gb_validate(gb, v);
	if (v.v == fy_invalid_value)
		return fy_invalid;
	in = v;

	/* PARSE and EMIT don't work on collections, skip the collection handling */
	if (op == FYGBOP_PARSE || op == FYGBOP_EMIT)
		goto do_operation;

	in_direct = in;
	if (fy_generic_is_indirect(in_direct))
		in_direct = fy_generic_indirect_get_value(in);

	type = fy_get_type(in_direct);
	switch (type) {

	case FYGT_SEQUENCE:

		col_item_size = sizeof(fy_generic);
		item_count = count;
		col_mark = FY_SEQ_V;
		in_items = fy_generic_sequence_get_items(in_direct, &in_item_count);
		break;

	case FYGT_MAPPING:

		/* those work only on sequences */
		if (op == FYGBOP_SET_AT)
			goto err_out;

		col_item_size = sizeof(fy_generic) * 2;

		/* the count was given in items not pairs */
		if (flags & FYGBOPF_MAP_ITEM_COUNT)
			count /= 2;

		item_count = (op != FYGBOP_FILTER && op != FYGBOP_MAP && op != FYGBOP_MAP_FILTER &&
			      op != FYGBOP_REDUCE && op != FYGBOP_GET_AT_PATH && op != FYGBOP_SET_AT_PATH) ?
					MULSZ(count, 2) : count;
		col_mark = FY_MAP_V;
		in_items = fy_generic_mapping_get_items(in_direct, &in_item_count);
		break;

	default:
		goto err_out;
	}

	if (has_args) {
		/* if we need to modify the items, or we internalize we need a copy */
		if (need_items_mod) {
			if (item_count <= ARRAY_SIZE(items_buf)) {
				items_mod = items_buf;
			} else {
				items_alloc = malloc(sizeof(*items_alloc) * item_count);
				if (!items_alloc)
					goto err_out;
				items_mod = items_alloc;
			}
			memcpy(items_mod, items, sizeof(*items_mod) * item_count);
		}

		if (!(flags & FYGBOPF_NO_CHECKS)) {
			if (!(flags & FYGBOPF_DONT_INTERNALIZE)) {
				assert(items_mod);
				for (i = 0; i < item_count; i++) {
					value = fy_gb_internalize(gb, items[i]);
					if (value.v == fy_invalid_value)
						goto err_out;
					items_mod[i] = value;
				}
				items = items_mod;
			} else {
				/* we validate directly on the input */
				for (i = 0; i < item_count; i++) {
					value = fy_gb_validate(gb, items[i]);
					if (value.v == fy_invalid_value)
						goto err_out;
				}
			}
		}
	}

	if (need_work_items) {

		switch (op) {
		case FYGBOP_FILTER:
		case FYGBOP_MAP:
		case FYGBOP_MAP_FILTER:
		case FYGBOP_REDUCE:
			work_item_count = in_item_count;
			for (i = 0; i < item_count; i++) {
				v = items[i];
				if (type == FYGT_SEQUENCE) {
					if (!fy_generic_is_sequence(v))
						goto err_out;
				} else {
					if (!fy_generic_is_mapping(v))
						goto err_out;
				}
				(void)fy_generic_collection_get_items(v, &tmp_item_count);
				work_item_count += tmp_item_count;
			}
			break;

		case FYGBOP_SET_AT_PATH:
			work_item_count = item_count - 1;
			break;
		default:
			goto err_out;
		}

		/* worst case, all items are added */
		if (work_item_count <= ARRAY_SIZE(work_items_buf))
			work_items = work_items_buf;
		else {
			work_items_alloc = malloc(sizeof(*work_items) * work_item_count);
			if (!work_items_alloc)
				goto err_out;
			work_items = work_items_alloc;
		}

		/* if not enough work, punt */
		if (work_item_count < FY_PARALLEL_THRESHOLD) {
			flags &= ~FYGBOPF_PARALLEL;
			needs_thread_pool = false;
		}

		if (need_copy_work_items) {
			k = 0;
			memcpy(work_items + k, in_items, sizeof(*work_items) * in_item_count);
			k += in_item_count;
			for (j = 0; j < item_count; j++) {
				tmp_items = fy_generic_collection_get_items(items[j], &tmp_item_count);
				memcpy(work_items + k, tmp_items, sizeof(*work_items) * tmp_item_count);
				k += tmp_item_count;
			}
			assert(k == work_item_count);
		}
	} else
		work_items = NULL;

	if (needs_thread_pool && !tp) {
		memset(&tp_cfg, 0, sizeof(tp_cfg));
		tp_cfg.flags = FYTPCF_STEAL_MODE;
		tp_cfg.num_threads = 0;
		tp = fy_thread_pool_create(&tp_cfg);
		if (!tp)
			goto err_out;
		owns_thread_pool = true;
	}

	num_threads = tp ? fy_thread_pool_get_num_threads(tp) : 1;

	iov = iov_buf;

do_operation:

	switch (op) {
	case FYGBOP_FILTER:
	case FYGBOP_MAP:
	case FYGBOP_MAP_FILTER:
		assert(work_items);
		assert(items_mod);
		assert(need_copy_work_items);

		key = fy_invalid;

		assert(num_threads > 0);

		work_args = alloca(sizeof(*work_args) * num_threads);
		memset(work_args, 0, sizeof(*work_args) * num_threads); 

		/* distribute evenly */
		chunk_size = (work_item_count + num_threads - 1) / num_threads;

		/* for mappings the chunk must be even */
		if (type == FYGT_MAPPING && (chunk_size & 1))
			chunk_size++;

		assert(chunk_size <= work_item_count);

		for (i = 0; i < (size_t)num_threads; i++) {
			start_idx = i * chunk_size;
			count_items = chunk_size;

			/* Last chunk might be smaller */
			if (start_idx >= work_item_count)
				break;
			if (start_idx + count_items > work_item_count)
				count_items = work_item_count - start_idx;

			work_args[i].op = op;
			work_args[i].gb = gb;
			work_args[i].type = type;
#if defined(__BLOCKS__)
			work_args[i].is_block = !!(flags & FYGBOPF_BLOCK_FN);
#endif
			work_args[i].fn = fn;
			work_args[i].work_items = work_items + start_idx;
			work_args[i].work_item_count = count_items;
			work_args[i].removed_items = 0;
			work_args[i].vresult = fy_invalid;

		}

		/* single threaded, or parallel */
		if (num_threads > 1) {
			works = alloca(sizeof(*works) * num_threads);
			for (i = 0; i < (size_t)num_threads; i++) {
				works[i].fn = fy_op_map_filter_work;
				works[i].arg = &work_args[i];
				works[i].wp = NULL;  /* Set by work_join */
			}
			/* Execute in parallel with work stealing */
			fy_thread_work_join(tp, works, num_threads, NULL);
		} else 
			fy_op_map_filter_work(work_args);

		/* collect the amount of removed counts */
		j = 0;
		for (i = 0; i < (size_t)num_threads; i++) {
			if (fy_generic_is_invalid(work_args[i].vresult))
				goto err_out;
			j += work_args[i].removed_items;
		}

		/* if everything removed */
		if ((op == FYGBOP_FILTER || op == FYGBOP_MAP_FILTER) && j == work_item_count) {
			out = type == FYGT_SEQUENCE ? fy_seq_empty : fy_map_empty;
			goto out;
		}

		/* if nothing removed */
		if (op == FYGBOP_FILTER && j == 0) {
			out = in;
			goto out;
		}

		/* something was removed, go over the items and remove the invalids */
		if ((op == FYGBOP_FILTER || op == FYGBOP_MAP_FILTER) && j > 0) {
			/* simple pass writing the non invalid values */
			k = 0; 
			for (i = 0; i < work_item_count; i++) {
				v = work_items[i];
				if (v.v != fy_invalid_value)
					work_items[k++] = v;
			}
			work_item_count = k;
		}

		col.count = type == FYGT_SEQUENCE ? work_item_count : (work_item_count / 2);
		if (!col.count) {
			out = type == FYGT_SEQUENCE ? fy_seq_empty : fy_map_empty;
			goto out;
		}
		iov[0].iov_base = &col;
		iov[0].iov_len = sizeof(col);
		/* the content */
		iov[1].iov_base = work_items;
		iov[1].iov_len = MULSZ(col.count, col_item_size);
		iovcnt = 2;
		break;

	case FYGBOP_REDUCE:
		assert(work_items);
		assert(need_copy_work_items);

		/* empty? return the initial accumulator */
		if (!work_item_count) {
			out = acc;
			goto out;
		}
		work_args = alloca(sizeof(*work_args) * num_threads);
		memset(work_args, 0, sizeof(*work_args) * num_threads); 

		/* distribute evenly */
		chunk_size = (work_item_count + num_threads - 1) / num_threads;

		/* for mappings the chunk must be even */
		if (type == FYGT_MAPPING && (chunk_size & 1))
			chunk_size++;

		assert(chunk_size <= work_item_count);

		assert(num_threads);

		for (i = 0; i < (size_t)num_threads; i++) {
			start_idx = i * chunk_size;
			count_items = chunk_size;

			/* Last chunk might be smaller */
			if (start_idx >= work_item_count)
				break;
			if (start_idx + count_items > work_item_count)
				count_items = work_item_count - start_idx;

			work_args[i].op = op;
			work_args[i].gb = gb;
			work_args[i].type = type;
			work_args[i].fn = fn;
			work_args[i].work_items = work_items + start_idx;
			work_args[i].work_item_count = count_items;
			work_args[i].removed_items = 0;
			work_args[i].vresult = acc;
		}

		/* single threaded, or parallel */
		if (num_threads > 1) {
			works = alloca(sizeof(*works) * num_threads);
			for (i = 0; i < (size_t)num_threads; i++) {
				works[i].fn = fy_op_reduce_work;
				works[i].arg = &work_args[i];
				works[i].wp = NULL;  /* Set by work_join */
			}
			/* Execute in parallel with work stealing */
			fy_thread_work_join(tp, works, num_threads, NULL);

			/* now perform the final reduce step */
			fy_generic *partial = alloca(sizeof(*partial) * num_threads);
			for (i = 0; i < (size_t)num_threads; i++) {
				v = work_args[i].vresult;
				if (v.v == fy_invalid_value) {
					out = fy_invalid;
					goto out;
				}
				partial[i] = v;
			}
			work_args[0].vresult = acc;
			work_args[0].work_items = partial;
			work_args[0].work_item_count = (size_t)num_threads;
			fy_op_reduce_work(work_args);
			acc = work_args->vresult;
		} else {
			fy_op_reduce_work(work_args);
			acc = work_args->vresult;
		}
		out = acc;
		goto out;

	case FYGBOP_GET:
		/* Get value by key (for mappings) or index (for sequences) */
		key = items[0];
		out = fy_invalid;
		switch (type) {
		case FYGT_SEQUENCE:
			idx = fy_cast(key, (size_t)-1);
			if (idx == (size_t)-1)
				break;
			out = fy_generic_sequence_get_item_generic(in, idx);
			break;

		case FYGT_MAPPING:
			fy_generic_emit_default(key);
			out = fy_generic_mapping_get_value(in, key);
			break;

		default:
			break;
		}
		goto out;

	case FYGBOP_GET_AT:
		out = fy_get_at(in, idx, fy_invalid);
		goto out;

	case FYGBOP_GET_AT_PATH:
		out = in;
		for (i = 0; i < item_count && fy_generic_is_valid(out); i++) {

			if ((flags & FYGBOPF_FLATTEN_KEYS) && fy_generic_is_sequence(items[i]))
				k = fy_len(items[i]);
			else
				k = 1;
				
			for (j = 0; j < k; j++) {

				if ((flags & FYGBOPF_FLATTEN_KEYS) && fy_generic_is_sequence(items[i]))
					key = fy_get_at(items[i], j, fy_invalid);
				else
					key = items[i];

				if (fy_generic_is_invalid(key))
					goto err_out;

				if (fy_generic_is_sequence(out)) {
					idx = fy_cast(key, (size_t)-1);
					if (idx == (size_t)-1)
						goto err_out;
					out = fy_generic_sequence_get_item_generic(out, idx);
				} else if (fy_generic_is_mapping(in)) {
					out = fy_generic_mapping_get_value(out, key);
				} else
					goto err_out;
			}
		}
		goto out;

	case FYGBOP_SET:
		out = in;
		for (i = 0; i < item_count && fy_generic_is_valid(out); i += 2) {

			key = items[i + 0];
			value = items[i + 1];

			if (type == FYGT_SEQUENCE) {
				out = fy_generic_op(gb, FYGBOPF_REPLACE, out, 1, (fy_generic []) { value }, fy_cast(key, -1) );
			} else if (type == FYGT_MAPPING) {
				out = fy_generic_op(gb, FYGBOPF_ASSOC, out, 1, (fy_generic []) { key, value });
			} else
				goto err_out;
		}
		goto out;

	case FYGBOP_SET_AT:
		out = fy_generic_op(gb, FYGBOPF_REPLACE, in, 1, (fy_generic []) { items[0] }, idx);
		goto out;

	case FYGBOP_SET_AT_PATH:
		assert(work_items);
		assert(work_item_count);

		/* last item is the value */
		value = items[item_count - 1];

		j = item_count - 1;
		printf("item_count=%zu, j=%zu\n", item_count, j);
		/* first pass, collections up to the point */
		v = in;
		for (i = 0; i < j - 1; i++) {
			work_items[i] = v;

			printf("%zu: v,work_items[%zu] - ", i, i); fy_generic_emit_default(v);
			printf("%zu: items[%zu] - ", i, i); fy_generic_emit_default(items[i]);

			v = fy_generic_op(gb, FYGBOPF_GET |  FYGBOPF_MAP_ITEM_COUNT, work_items[i],
					1, (fy_generic []){ items[i] });
			if (fy_generic_is_invalid(v))
				goto err_out;

			printf("%zu: new-v - ", i); fy_generic_emit_default(v);
		}

		printf("done collecting\n");

		/* move backwards, setting */
		for (;;) {
			printf("%zu: v - ", i); fy_generic_emit_default(v);
			printf("%zu: value - ", i); fy_generic_emit_default(value);
			printf("%zu: items[%zu] - ", i, i); fy_generic_emit_default(items[i]);
			value = fy_generic_op(gb, FYGBOPF_SET | FYGBOPF_MAP_ITEM_COUNT, v,
					2, (fy_generic []) { items[i], value });
			if (fy_generic_is_invalid(value))
				goto err_out;
			if (i == 0)
				break;
			v = work_items[--i];
			printf("%zu: new-v - ", i); fy_generic_emit_default(v);
		}

		/* the last value out is the new thing */
		out = value;
		goto out;

	case FYGBOP_PARSE:
		/* Parse YAML/JSON string to generic value */
		{
			fy_generic_sized_string szstr;
			struct fy_parse_cfg parse_cfg;
			struct fy_parser *fyp = NULL;
			struct fy_generic_decoder *fygd = NULL;
			enum fy_generic_decoder_parse_flags parse_flags = 0;
			enum fy_parser_mode parser_mode = fypm_yaml_1_2;

			/* Input must be a string */
			if (fy_get_type(in) != FYGT_STRING) {
				out = fy_invalid;
				goto out;
			}

			/* Get string data */
			szstr = fy_generic_cast_sized_string_default(in, fy_szstr_empty);
			if (!szstr.data) {
				out = fy_invalid;
				goto out;
			}

			/* Get parser mode from args */
			if (args && args->parse.parser_mode != 0)
				parser_mode = args->parse.parser_mode;

			/* Setup parse configuration */
			memset(&parse_cfg, 0, sizeof(parse_cfg));
			parse_cfg.flags = FYPCF_DEFAULT_PARSE;

			/* Set parser mode */
			switch (parser_mode) {
			case fypm_json:
				parse_cfg.flags |= FYPCF_JSON_MASK;
				break;
			case fypm_yaml_1_1:
			case fypm_yaml_1_2:
			case fypm_yaml_1_3:
			default:
				/* YAML is default */
				break;
			}

			/* Create parser */
			fyp = fy_parser_create(&parse_cfg);
			if (!fyp) {
				out = fy_invalid;
				goto out;
			}

			/* Set input string */
			if (fy_parser_set_string(fyp, szstr.data, szstr.size) != 0) {
				fy_parser_destroy(fyp);
				out = fy_invalid;
				goto out;
			}

			/* Create decoder */
			fygd = fy_generic_decoder_create(fyp, gb, false);
			if (!fygd) {
				fy_parser_destroy(fyp);
				out = fy_invalid;
				goto out;
			}

			/* Set parse flags from args */
			if (args && args->parse.multi_document)
				parse_flags |= FYGDPF_MULTI_DOCUMENT;

			/* Parse the input */
			out = fy_generic_decoder_parse(fygd, parse_flags);

			/* Cleanup */
			fy_generic_decoder_destroy(fygd);
			fy_parser_destroy(fyp);
		}
		goto out;

	case FYGBOP_EMIT:
		/* Emit generic value to YAML/JSON string */
		{
			struct fy_emitter *emit = NULL;
			struct fy_generic_encoder *fyge = NULL;
			enum fy_emitter_cfg_flags emit_flags = FYECF_DEFAULT;
			enum fy_generic_encoder_emit_flags encoder_flags = 0;
			char *output_str = NULL;
			size_t output_len = 0;

			/* Get emit flags from args */
			if (args && args->emit.emit_flags != 0)
				emit_flags = args->emit.emit_flags;

			/* Create string emitter */
			emit = fy_emit_to_string(emit_flags);
			if (!emit) {
				out = fy_invalid;
				goto out;
			}

			/* Create encoder */
			fyge = fy_generic_encoder_create(emit, false);
			if (!fyge) {
				fy_emitter_destroy(emit);
				out = fy_invalid;
				goto out;
			}

			/* Emit the value */
			if (fy_generic_encoder_emit(fyge, encoder_flags, in) != 0) {
				fy_generic_encoder_destroy(fyge);
				fy_emitter_destroy(emit);
				out = fy_invalid;
				goto out;
			}

			/* Sync the encoder */
			if (fy_generic_encoder_sync(fyge) != 0) {
				fy_generic_encoder_destroy(fyge);
				fy_emitter_destroy(emit);
				out = fy_invalid;
				goto out;
			}

			/* Collect the output */
			output_str = fy_emit_to_string_collect(emit, &output_len);

			/* Cleanup encoder and emitter */
			fy_generic_encoder_destroy(fyge);
			fy_emitter_destroy(emit);

			if (!output_str) {
				out = fy_invalid;
				goto out;
			}

			/* Create string generic from output */
			out = fy_gb_string_size_create(gb, output_str, output_len);

			/* Free the output string */
			free(output_str);
		}
		goto out;

	default:
		goto err_out;
	}

	if (iovcnt > 0) {
		p = fy_gb_lookupv(gb, iov, iovcnt, FY_GENERIC_CONTAINER_ALIGN);
		if (!p)
			p = fy_gb_storev(gb, iov, iovcnt, FY_GENERIC_CONTAINER_ALIGN);
		if (!p)
			goto err_out;

		out = (fy_generic){ .v = (uintptr_t)p | col_mark };
	}

out:
	if (owns_thread_pool && tp)
		fy_thread_pool_destroy(tp);

	if (work_items_alloc)
		free(work_items_alloc);

	if (items_alloc)
		free(items_alloc);

	if (iov_alloc)
		free(iov_alloc);

	return out;

err_out:
	out = fy_invalid;
	goto out;
}

fy_generic fy_generic_op(struct fy_generic_builder *gb, enum fy_gb_op_flags flags, ...)
{
	struct fy_generic_op_args args_local, *args = &args_local;
	va_list ap;
	fy_generic in;
	unsigned int op;

	op = ((flags >> FYGBOPF_OP_SHIFT) & FYGBOPF_OP_MASK);
	if (op >= FYGBOP_COUNT)
		return fy_invalid;

	memset(args, 0, sizeof(*args));

	va_start(ap, flags);
	if (op == FYGBOP_CREATE_SEQ)
		in = fy_seq_empty;
	else if (op == FYGBOP_CREATE_MAP)
		in = fy_map_empty;
	else
		in = va_arg(ap, fy_generic);

	if (op == FYGBOP_KEYS || op == FYGBOP_VALUES || op == FYGBOP_ITEMS) {
		args->common.count = 0;
		args->common.items = NULL;
	} else {
		args->common.count = va_arg(ap, size_t);
		args->common.items = va_arg(ap, const fy_generic *);
	}

	if (flags & FYGBOPF_PARALLEL)
		args->common.tp = va_arg(ap, struct fy_thread_pool *);

	switch (op) {
	case FYGBOP_CREATE_SEQ:
	case FYGBOP_CREATE_MAP:
	case FYGBOP_APPEND:
	case FYGBOP_ASSOC:
	case FYGBOP_DISASSOC:
	case FYGBOP_CONTAINS:
	case FYGBOP_CONCAT:
	case FYGBOP_REVERSE:
	case FYGBOP_MERGE:
	case FYGBOP_UNIQUE:
	case FYGBOP_SORT:
	case FYGBOP_KEYS:
	case FYGBOP_VALUES:
	case FYGBOP_ITEMS:
	case FYGBOP_GET:
	case FYGBOP_GET_AT_PATH:
	case FYGBOP_SET:
	case FYGBOP_SET_AT_PATH:
	case FYGBOP_PARSE:
	case FYGBOP_EMIT:
		break;

	case FYGBOP_INSERT:
	case FYGBOP_REPLACE:
	case FYGBOP_GET_AT:
	case FYGBOP_SET_AT:
		args->insert_replace_get_set_at.idx = va_arg(ap, size_t);
		break;

	case FYGBOP_FILTER:
	case FYGBOP_MAP:
	case FYGBOP_MAP_FILTER:
	case FYGBOP_REDUCE:
#if defined(__BLOCKS__)
		if (flags & FYGBOPF_BLOCK_FN)
			args->filter_map_reduce_common.blk = va_arg(ap, void (^)(void));
		else
#endif
			args->filter_map_reduce_common.fn = va_arg(ap, void (*)(void));
		if (op == FYGBOP_REDUCE)
			args->reduce.acc = va_arg(ap, fy_generic);
		break;

	default:
		return fy_invalid;
	}
	va_end(ap);

	return fy_generic_op_args(gb, flags, in, args);
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
	/* the copy is just an internalization */
	return fy_gb_internalize(gb, v);
}

fy_generic fy_generic_relocate(void *start, void *end, fy_generic v, ptrdiff_t d)
{
	const void *p;
	fy_generic_indirect *gi;
	enum fy_generic_type type;
	fy_generic *items;
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
	type = fy_generic_get_type(v);
	switch (type) {
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
	case FYGT_MAPPING:

		p = fy_generic_resolve_collection_ptr(v);
		if (!p)
			return v;	/* either empty map or seq */

		if (p >= start && p < end)
			return v;

		items = (void *)fy_generic_collectionp_get_items(type, p, &count);

		v.v = fy_generic_relocate_collection_ptr(v, d).v | (type == FYGT_SEQUENCE ? FY_SEQ_V : FY_MAP_V);
		for (i = 0; i < count; i++)
			items[i] = fy_generic_relocate(start, end, items[i], d);
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
