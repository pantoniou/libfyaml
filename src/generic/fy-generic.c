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
} fy_op_fn;

struct fy_op_work_arg {
	unsigned int op;
	struct fy_generic_builder *gb;
	enum fy_generic_type type;
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
			if (!arg->fn.filter_pred(arg->gb, value)) {
				key = value = fy_invalid;
				j += stride;
			}
			break;
		case FYGBOP_MAP:
			value = arg->fn.map_xform(arg->gb, value);
			// an invalid value stops everything
			if (value.v == fy_invalid_value) {
				arg->vresult = fy_invalid;
				return;
			}
			break;
		case FYGBOP_MAP_FILTER:
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
		acc = arg->fn.reducer(arg->gb, acc, arg->work_items[i + valoffset]);
		if (acc.v == fy_invalid_value)
			break;
	}
	arg->vresult = acc;
}

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
	fy_generic in_direct, out, key, key2, value, acc;
	fy_generic_value col_mark;
	unsigned int op;
	size_t count, item_count, tmp_item_count, left_item_count, idx, i, j, k;
	size_t in_count, in_item_count, out_count, work_item_count;
	size_t col_item_size, tmp;
	size_t remain_idx, remain_count;
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

	op = ((flags >> FYGBOPF_OP_SHIFT) & FYGBOPF_OP_MASK);
	if (op >= FYGBOP_COUNT)
		return fy_invalid;

	out = fy_invalid;
	idx = SIZE_MAX;
	fn.fn = NULL;

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
	case FYGBOP_CREATE_SEQ:
		in = fy_seq_empty;
		break;

	case FYGBOP_CREATE_MAP:
		in = fy_map_empty;
		break;

	case FYGBOP_INSERT:
	case FYGBOP_REPLACE:
	case FYGBOP_APPEND:
	case FYGBOP_ASSOC:
	case FYGBOP_DISASSOC:
		if (op == FYGBOP_INSERT || op == FYGBOP_REPLACE)
			idx = args->insert_replace.idx;
		if (!count)
			return in;
		if (!items)
			return fy_invalid;
		if (op == FYGBOP_ASSOC || op == FYGBOP_DISASSOC) {
			need_work_items = true;
			need_items_mod = true;
		}
		break;

	case FYGBOP_KEYS:
	case FYGBOP_VALUES:
	case FYGBOP_ITEMS:
		has_args = false;
		need_work_items = true;
		break;

	case FYGBOP_CONTAINS:
		flags |= FYGBOPF_DONT_INTERNALIZE;	/* don't internalize */
		if (!count)
			return fy_false;
		if (!items)
			return fy_invalid;
		break;

	case FYGBOP_CONCAT:
		if (!count)
			return in;			/* single? */
		if (!items)
			return fy_invalid;
		need_work_items = true;
		break;

	case FYGBOP_REVERSE:
	case FYGBOP_MERGE:
	case FYGBOP_UNIQUE:
	case FYGBOP_SORT:
		if (count && !items)
			return fy_invalid;
		need_work_items = true;
		need_copy_work_items = true;
		break;

	case FYGBOP_FILTER:
	case FYGBOP_MAP:
	case FYGBOP_MAP_FILTER:
	case FYGBOP_REDUCE:
		fn.fn = args->filter_map_reduce_common.fn;
		if (op == FYGBOP_REDUCE)
			acc = args->reduce.acc;
		if (!fn.fn || (count && !items))
			return fy_invalid;
		if (op == FYGBOP_REDUCE)
			flags |= FYGBOPF_DONT_INTERNALIZE;	/* don't internalize for reduce */
		need_work_items = true;
		need_copy_work_items = true;
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

	in_direct = in;
	if (fy_generic_is_indirect(in_direct))
		in_direct = fy_generic_indirect_get_value(in);

	type = fy_get_type(in_direct);
	switch (type) {

	case FYGT_SEQUENCE:

		/* those work only on mappings */
		if (op == FYGBOP_ASSOC || op == FYGBOP_DISASSOC || op == FYGBOP_MERGE)
			goto err_out;

		col_item_size = sizeof(fy_generic);
		item_count = count;
		col_mark = FY_SEQ_V;
		in_items = fy_generic_sequence_get_items(in_direct, &in_item_count);
		in_count = in_item_count;
		break;

	case FYGT_MAPPING:

		/* those work only on sequences */
		if (op == FYGBOP_CONCAT || op == FYGBOP_REVERSE || op == FYGBOP_UNIQUE)
			goto err_out;

		/* the count was given in items not pairs */
		if (flags & FYGBOPF_MAP_ITEM_COUNT)
			count /= 2;

		col_item_size = sizeof(fy_generic) * 2;
		item_count = (op != FYGBOP_DISASSOC && op != FYGBOP_CONTAINS &&
			      op != FYGBOP_MERGE && op != FYGBOP_SORT &&
			      op != FYGBOP_FILTER && op != FYGBOP_MAP && op != FYGBOP_MAP_FILTER &&
			      op != FYGBOP_REDUCE) ?
					MULSZ(count, 2) : count;
		col_mark = FY_MAP_V;
		in_items = fy_generic_mapping_get_items(in_direct, &in_item_count);
		in_count = in_item_count / 2;
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
		case FYGBOP_ASSOC:
			work_item_count = in_item_count + item_count;
			break;
		case FYGBOP_DISASSOC:
			work_item_count = in_item_count;
			break;

		case FYGBOP_KEYS:
		case FYGBOP_VALUES:
		case FYGBOP_ITEMS:
			work_item_count = in_item_count / 2;

			/* nothing? just return empty seq */
			if (!work_item_count) {
				out = fy_seq_empty;
				goto out;
			}
			break;

		case FYGBOP_CONCAT:
			work_item_count = 0;
			for (i = 0; i < item_count; i++) {
				v = items[i];
				if (!fy_generic_is_sequence(v))
					goto err_out;
				work_item_count += fy_len(v);
			}
			/* all of them are empty? just return the same */
			if (!work_item_count) {
				out = in;
				goto out;
			}
			break;

		case FYGBOP_MERGE:
			work_item_count = in_item_count;
			for (i = 0; i < item_count; i++) {
				v = items[i];
				if (!fy_generic_is_mapping(v))
					goto err_out;

				(void)fy_generic_mapping_get_items(v, &tmp_item_count);
				work_item_count += tmp_item_count;
			}
			/* it's ok if there are no extra items, we might make a dup filter */
			break;

		case FYGBOP_UNIQUE:
		case FYGBOP_REVERSE:
			work_item_count = in_item_count;
			for (i = 0; i < item_count; i++) {
				v = items[i];
				if (!fy_generic_is_sequence(v))
					goto err_out;

				(void)fy_generic_sequence_get_items(v, &tmp_item_count);
				work_item_count += tmp_item_count;
			}
			break;

		case FYGBOP_SORT:
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

	switch (op) {
	case FYGBOP_CREATE_SEQ:
	case FYGBOP_CREATE_MAP:
		/* sequence overlaps map counter */
		col.count = count;
		iov[0].iov_base = &col;
		iov[0].iov_len = sizeof(col);
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
			remain_count = in_count - remain_idx;
			break;
		}

		/* sequence overlaps map counter */
		col.count = out_count;
		iov[0].iov_base = &col;
		iov[0].iov_len = sizeof(col);
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

	case FYGBOP_ASSOC:
		/* first copy all in_item to work_items */
		assert(work_items);
		assert(work_item_count >= in_item_count);
		assert(items_mod);

		/* go over the keys in the input mapping */
		/* replaces the values by what was in items */
		left_item_count = item_count;
		for (i = 0; i < in_item_count; i += 2) {
			key = in_items[i + 0];
			value = in_items[i + 1];
			if (left_item_count > 0) {
				for (j = 0; j < item_count; j += 2) {
					if (fy_generic_compare(items_mod[j + 0], key))
						continue;
					value = items_mod[j + 1];
					/* remove it from the items */
					items_mod[j + 0] = fy_invalid;
					items_mod[j + 1] = fy_invalid;
					left_item_count -= 2;
					break;
				}
			}
			work_items[i + 0] = key;
			work_items[i + 1] = value;
		}
		/* now append whatever is left */
		if (left_item_count > 0) {
			for (j = 0; j < item_count; j += 2) {
				key = items_mod[j + 0];
				if (key.v == fy_invalid_value)
					continue;
				work_items[i + 0] = key;
				work_items[i + 1] = items_mod[j + 1];
				i += 2;
			}
		}
		/* update the new count */
		work_item_count = i;
		/* the collection header */
		col.count = work_item_count / 2;
		iov[0].iov_base = &col;
		iov[0].iov_len = sizeof(col);
		/* the content */
		iov[1].iov_base = work_items;
		iov[1].iov_len = MULSZ(col.count, col_item_size);
		iovcnt = 2;
		break;

	case FYGBOP_DISASSOC:
		/* first copy all in_item to work_items */
		assert(work_items);
		assert(work_item_count >= in_item_count);
		assert(items_mod);

		/* go over the keys in the input mapping */
		/* removes all pairs with keys that match */
		left_item_count = item_count;
		for (i = 0, k = 0; i < in_item_count; i += 2) {
			key = in_items[i + 0];
			value = in_items[i + 1];
			if (left_item_count > 0) {
				for (j = 0; j < item_count; j++) {
					if (fy_generic_compare(items_mod[j], key))
						continue;
					items_mod[j] = fy_invalid;
					left_item_count--;
					break;
				}
				if (j < item_count)
					continue;
			}
			work_items[k + 0] = key;
			work_items[k + 1] = value;
			k += 2;
		}
		/* update the new count */
		work_item_count = k;

		/* if everything is gone, don't bother */
		if (!work_item_count) {
			out = fy_map_empty;
			goto out;
		}

		/* nothing changed... */
		if (left_item_count == item_count) {
			out = in;
			goto out;
		}

		/* the collection header */
		col.count = work_item_count / 2;
		iov[0].iov_base = &col;
		iov[0].iov_len = sizeof(col);
		/* the content */
		iov[1].iov_base = work_items;
		iov[1].iov_len = MULSZ(col.count, col_item_size);
		iovcnt = 2;
		break;

	case FYGBOP_KEYS:
	case FYGBOP_VALUES:
	case FYGBOP_ITEMS:

		/* if everything is gone, don't bother */
		if (!work_item_count) {
			out = fy_seq_empty;
			goto out;
		}

		switch (op) {
		case FYGBOP_KEYS:
			for (i = 0; i < work_item_count; i++)
				work_items[i] = in_items[i * 2 + 0];
			break;
		case FYGBOP_VALUES:
			for (i = 0; i < work_item_count; i++)
				work_items[i] = in_items[i * 2 + 1];
			break;
		case FYGBOP_ITEMS:
			for (i = 0; i < work_item_count; i++) {
				v = fy_gb_sequence(gb, in_items[i * 2 + 0], in_items[i * 2 + 1]);
				if (v.v == fy_invalid_value)
					goto err_out;
				work_items[i] = v;
			}
			break;
		default:
			goto err_out;
		}

		/* always return a sequence */
		col_mark = FY_SEQ_V;
		col_item_size = sizeof(fy_generic);

		col.count = work_item_count;
		iov[0].iov_base = &col;
		iov[0].iov_len = sizeof(col);
		/* the content */
		iov[1].iov_base = work_items;
		iov[1].iov_len = MULSZ(col.count, col_item_size);
		iovcnt = 2;

		break;

	case FYGBOP_CONTAINS:
		/* works on both sequences and mappings */
		k = type == FYGT_SEQUENCE ? 1 : 2;
		for (i = 0; i < in_item_count; i += k) {
			for (j = 0; j < item_count; j++) {
				if (!fy_generic_compare(in_items[i], items[j])) {
					out = fy_true;
					goto out;
				}
			}
		}
		out = fy_false;
		goto out;

	case FYGBOP_CONCAT:
		k = 0;
		for (i = 0; i < item_count; i++) {
			tmp_items = fy_generic_sequence_get_items(items[i], &tmp_item_count);
			if (!tmp_item_count)
				continue;
			for (j = 0; j < tmp_item_count; j++) {
				v = tmp_items[j];
				if (!(flags & FYGBOPF_NO_CHECKS))
					v = !(flags & FYGBOPF_DONT_INTERNALIZE) ?
						fy_gb_internalize(gb, v) : fy_gb_validate(gb, v);
				if (v.v == fy_invalid_value)
					goto err_out;
				assert(k < work_item_count);
				work_items[k++] = v;
			}
		}
		assert(k == work_item_count);

		/* always return a sequence */
		col_mark = FY_SEQ_V;
		col_item_size = sizeof(fy_generic);

		col.count = in_item_count + work_item_count;
		iov[0].iov_base = &col;
		iov[0].iov_len = sizeof(col);
		/* the original */
		iov[1].iov_base = (void *)in_items;
		iov[1].iov_len = MULSZ(in_item_count, col_item_size);
		/* the extra content */
		iov[2].iov_base = work_items;
		iov[2].iov_len = MULSZ(work_item_count, col_item_size);
		iovcnt = 3;
		break;

	case FYGBOP_REVERSE:
		k = 0;
		/* the extra arguments */
		for (i = item_count - 1; (ssize_t)i >= 0; i--) {
			tmp_items = fy_generic_sequence_get_items(items[i], &tmp_item_count);
			if (!tmp_item_count)
				continue;
			for (j = tmp_item_count - 1; (ssize_t)j >= 0; j--) {
				v = tmp_items[j];
				if (!(flags & FYGBOPF_NO_CHECKS))
					v = !(flags & FYGBOPF_DONT_INTERNALIZE) ?
						fy_gb_internalize(gb, v) : fy_gb_validate(gb, v);
				if (v.v == fy_invalid_value)
					goto err_out;
				assert(k < work_item_count);
				work_items[k++] = v;
			}
		}
		/* the original */
		for (i = in_item_count - 1; (ssize_t)i >= 0; i--) {
			v = in_items[i];
			work_items[k++] = v;
		}
		assert(k == work_item_count);

		/* always return a sequence */
		col_mark = FY_SEQ_V;
		col_item_size = sizeof(fy_generic);

		col.count = work_item_count;
		if (!col.count) {
			/* nothing? */
			out = fy_seq_empty;
			goto out;
		}
		iov[0].iov_base = &col;
		iov[0].iov_len = sizeof(col);
		/* the extra content */
		iov[1].iov_base = work_items;
		iov[1].iov_len = MULSZ(work_item_count, col_item_size);
		iovcnt = 2;
		break;

	case FYGBOP_MERGE:
		assert(work_items);
		assert(items_mod);
		assert(need_copy_work_items);

		/* now go over each key in the work area, and compare with all the following */
		k = 0;
		for (i = 0; i < work_item_count; i += 2) {
			key = work_items[i + 0];
			value = work_items[i + 1];

			for (j = i + 2; j < work_item_count; j += 2) {
				key2 = work_items[j + 0];
				if (key2.v == fy_invalid_value || fy_generic_compare(key, key2))
					continue;
				value = work_items[j + 1];
				/* erase this pair */
				work_items[j + 0] = fy_invalid;
				work_items[j + 1] = fy_invalid;
				k += 2;
			}
			work_items[i + 1] = value;
		}

		/* were there removed items? */
		if (k > 0) {
			k = 0;
			for (i = 0; i < work_item_count; i += 2) {
				key = work_items[i + 0];
				if (key.v != fy_invalid_value) {
					work_items[k++] = key;
					work_items[k++] = work_items[i + 1];
				}
			}
			work_item_count = k;
		}

		/* the collection header */
		col.count = work_item_count / 2;
		if (!col.count) {
			/* nothing? */
			out = fy_map_empty;
			goto out;
		}

		iov[0].iov_base = &col;
		iov[0].iov_len = sizeof(col);
		/* the content */
		iov[1].iov_base = work_items;
		iov[1].iov_len = MULSZ(col.count, col_item_size);
		iovcnt = 2;
		break;

	case FYGBOP_UNIQUE:
		assert(work_items);
		assert(items_mod);
		assert(need_copy_work_items);

		/* now go over each item in the work area, and compare with all the following */
		k = 0;
		for (i = 0; i < work_item_count; i++) {
			v = work_items[i];
			for (j = i + 1; j < work_item_count; j++) {
				value = work_items[j];
				if (value.v == fy_invalid_value || fy_generic_compare(v, value))
					continue;
				work_items[j] = fy_invalid;
				k++;
			}
		}

		/* there were removed items */
		if (k > 0) {
			k = 0;
			for (i = 0; i < work_item_count; i++) {
				v = work_items[i];
				if (v.v != fy_invalid_value)
					work_items[k++] = v;
			}
			work_item_count = k;
		}

		/* the collection header */
		col.count = work_item_count;
		if (!col.count) {
			/* nothing? */
			out = fy_seq_empty;
			goto out;
		}

		iov[0].iov_base = &col;
		iov[0].iov_len = sizeof(col);
		/* the content */
		iov[1].iov_base = work_items;
		iov[1].iov_len = MULSZ(col.count, col_item_size);
		iovcnt = 2;
		break;

	case FYGBOP_SORT:
		assert(work_items);
		assert(items_mod);
		assert(need_copy_work_items);

		/* the collection header */
		if (type == FYGT_SEQUENCE) {
			col.count = work_item_count;
			if (!col.count) {
				/* nothing? */
				out = fy_seq_empty;
				goto out;
			}
			qsort(work_items, work_item_count, sizeof(*work_items), fy_generic_seqmap_qsort_cmp);
		} else {
			col.count = work_item_count / 2;
			if (!col.count) {
				/* nothing? */
				out = fy_map_empty;
				goto out;
			}
			qsort(work_items, work_item_count / 2, sizeof(*work_items) * 2, fy_generic_seqmap_qsort_cmp);
		}

		iov[0].iov_base = &col;
		iov[0].iov_len = sizeof(col);
		/* the content */
		iov[1].iov_base = work_items;
		iov[1].iov_len = MULSZ(col.count, col_item_size);
		iovcnt = 2;
		break;

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
		break;

	case FYGBOP_INSERT:
	case FYGBOP_REPLACE:
		args->insert_replace.idx = va_arg(ap, size_t);
		break;

	case FYGBOP_FILTER:
	case FYGBOP_MAP:
	case FYGBOP_MAP_FILTER:
	case FYGBOP_REDUCE:
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

