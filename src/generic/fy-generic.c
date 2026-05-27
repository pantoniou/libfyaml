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
#include <math.h>

#include <stdio.h>

/* for container_of */
#include "fy-list.h"
#include "fy-utf8.h"

#include "fy-allocator.h"
#include "fy-thread.h"

#include "fy-generic.h"
#include "fy-generic-encoder.h"
#include "fy-generic-decoder.h"

#include "fy-input.h"

// when to switch to malloc instead of alloca
#define COPY_MALLOC_CUTOFF	256

static int fy_arena_reloc_src_compare(const void *va, const void *vb)
{
	const struct fy_arena_reloc *a = va, *b = vb;

	return a->src.i < b->src.i ? -1 :
	       a->src.i > b->src.i ?  1 : 0;
}

static const struct fy_arena_reloc *
fy_arena_locate_by_src(const struct fy_arena_reloc *arenas, unsigned int count, const void *ptr)
{
	uintptr_t p = (uintptr_t)ptr;
	unsigned int mid;

	while (count > 0) {
		mid = count / 2;
		if (p >= arenas[mid].src.i) {
			if (p < arenas[mid].srce.i)
				return &arenas[mid];
			arenas += mid + 1;
			count -= mid + 1;
		} else
			count = mid;
	}

	return NULL;
}

static fy_generic
fy_generic_arena_relocate_ptr_internal(const struct fy_arena_reloc *arenas,
				       unsigned int num_arenas, fy_generic v,
				       unsigned int mask)
{
	const void *p;
	const struct fy_arena_reloc *arena;
	uintptr_t tag, off;

	p = mask == FY_COLLECTION_MASK ?
		fy_generic_resolve_collection_ptr(v) : fy_generic_resolve_ptr(v);
	arena = fy_arena_locate_by_src(arenas, num_arenas, p);
	if (!arena)
		return fy_invalid;

	tag = v.v & mask;
	off = (uintptr_t)p - arena->src.i;
	v.v = (fy_generic_value)(arena->dst.i + off) | tag;
	return v;
}

fy_generic
fy_generic_arena_relocate(const struct fy_arena_reloc *arenas, unsigned int num_arenas,
			  void *start, size_t size, fy_generic v)
{
	void *end = start + size;
	const void *p;
	const struct fy_arena_reloc *arena;
	uint64_t flags;
	fy_generic *gp, iv;
	enum fy_generic_type type;
	fy_generic *items;
	size_t i, count;

	if (!arenas || !num_arenas)
		return fy_invalid;

	/* all in place values stay as is */
	if (fy_generic_is_in_place(v))
		return v;

	if (fy_generic_is_indirect(v)) {
		p = fy_generic_resolve_ptr(v);
		arena = fy_arena_locate_by_src(arenas, num_arenas, p);
		if (!arena && p >= start && p < end)
			return v;
		if (!arena)
			return fy_invalid;

		v = fy_generic_arena_relocate_ptr_internal(arenas, num_arenas, v,
							   FY_INPLACE_TYPE_MASK);
		if (fy_generic_is_invalid(v))
			return fy_invalid;

		gp = (fy_generic *)fy_generic_resolve_ptr(v);

		flags = (uint64_t)(gp->v & FYGIF_CONTENT_MASK);
		gp++;
		while (flags) {
			i = fy_bit64_lowest(flags);
			flags &= ~FY_BIT64(i);
			iv = *gp++;
			if (!fy_generic_is_in_place(iv))
				gp[-1] = fy_generic_arena_relocate(arenas, num_arenas, start, size, iv);
		}

		return v;
	}

	type = fy_generic_get_type(v);
	switch (type) {
	case FYGT_NULL:
	case FYGT_BOOL:
	case FYGT_INVALID:
		return v;
	case FYGT_INT:
		if ((v.v & FY_INPLACE_TYPE_MASK) == FY_INT_INPLACE_V)
			return v;
		p = fy_generic_resolve_ptr(v);
		arena = fy_arena_locate_by_src(arenas, num_arenas, p);
		if (!arena && p >= start && p < end)
			return v;
		if (!arena)
			return fy_invalid;
		return fy_generic_arena_relocate_ptr_internal(arenas, num_arenas, v,
							      FY_INPLACE_TYPE_MASK);
	case FYGT_FLOAT:
		if ((v.v & FY_INPLACE_TYPE_MASK) == FY_FLOAT_INPLACE_V)
			return v;
		p = fy_generic_resolve_ptr(v);
		arena = fy_arena_locate_by_src(arenas, num_arenas, p);
		if (!arena && p >= start && p < end)
			return v;
		if (!arena)
			return fy_invalid;
		return fy_generic_arena_relocate_ptr_internal(arenas, num_arenas, v,
							      FY_INPLACE_TYPE_MASK);
	case FYGT_STRING:
		if ((v.v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V)
			return v;
		p = fy_generic_resolve_ptr(v);
		arena = fy_arena_locate_by_src(arenas, num_arenas, p);
		if (!arena && p >= start && p < end)
			return v;
		if (!arena)
			return fy_invalid;
		return fy_generic_arena_relocate_ptr_internal(arenas, num_arenas, v,
							      FY_INPLACE_TYPE_MASK);
	case FYGT_SEQUENCE:
	case FYGT_MAPPING:
		p = fy_generic_resolve_collection_ptr(v);
		if (!p)
			return v;
		arena = fy_arena_locate_by_src(arenas, num_arenas, p);
		if (!arena && p >= start && p < end)
			return v;
		if (!arena)
			return fy_invalid;
		v = fy_generic_arena_relocate_ptr_internal(arenas, num_arenas, v,
							   FY_COLLECTION_MASK);
		if (fy_generic_is_invalid(v))
			return fy_invalid;
		items = (void *)fy_generic_collectionp_get_items(type,
				fy_generic_resolve_collection_ptr(v), &count);
		for (i = 0; i < count; i++)
			items[i] = fy_generic_arena_relocate(arenas, num_arenas, start, size, items[i]);
		return v;
	default:
		return fy_invalid;
	}
}

enum fy_generic_type fy_generic_get_type_indirect(fy_generic v)
{
	const fy_generic_value *p;

	assert(fy_generic_is_indirect(v));

	p = fy_generic_resolve_collection_ptr(v);
	if (p[0] & FYGIF_ALIAS)
		return FYGT_ALIAS;
	v.v = p[1];
	if (fy_generic_is_indirect(v))
		return FYGT_INVALID;
	return fy_generic_get_direct_type(v);
}

void fy_generic_indirect_get(fy_generic v, fy_generic_indirect *gi)
{
	const fy_generic_value *p;
	uint64_t flags;
	unsigned int i;

	fy_generic_indirect_reset(gi);

	if (!fy_generic_is_indirect(v)) {
		gi->flags = FYGIF_VALUE;
		gi->value = v;
		return;
	}

	p = fy_generic_resolve_collection_ptr(v);

	gi->flags = *p++;
	flags = (uint64_t)(gi->flags & FYGIF_CONTENT_MASK);
	while (flags) {
		i = fy_bit64_lowest(flags);
		flags &= ~FY_BIT64(i);
		gi->vindirect[i].v = *p++;
	}
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

fy_generic fy_generic_indirect_get_diag(fy_generic v)
{
	fy_generic_indirect gi;

	fy_generic_indirect_get(v, &gi);
	return (gi.flags & FYGIF_DIAG) ? gi.diag : fy_invalid;
}

fy_generic fy_generic_indirect_get_marker(fy_generic v)
{
	fy_generic_indirect gi;

	fy_generic_indirect_get(v, &gi);
	return (gi.flags & FYGIF_MARKER) ? gi.marker : fy_invalid;
}

fy_generic fy_generic_indirect_get_comment(fy_generic v, enum fy_comment_placement placement)
{
	fy_generic_indirect gi;

	fy_generic_indirect_get(v, &gi);
	switch (placement) {
	case fycp_top:
		return (gi.flags & FYGIF_TOP_COMMENT) ? gi.top_comment : fy_invalid;
	case fycp_right:
		return (gi.flags & FYGIF_RIGHT_COMMENT) ? gi.right_comment : fy_invalid;
	case fycp_bottom:
		return (gi.flags & FYGIF_BOTTOM_COMMENT) ? gi.bottom_comment : fy_invalid;
	default:
		break;
	}
	return fy_invalid;
}

fy_generic fy_generic_indirect_get_style(fy_generic v)
{
	fy_generic_indirect gi;

	fy_generic_indirect_get(v, &gi);
	return (gi.flags & FYGIF_STYLE) ? gi.style : fy_invalid;
}

fy_generic fy_generic_indirect_get_failsafe_str(fy_generic v)
{
	fy_generic_indirect gi;

	fy_generic_indirect_get(v, &gi);
	return (gi.flags & FYGIF_FAILSAFE_STR) ? gi.failsafe_str : fy_invalid;
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

fy_generic fy_generic_get_diag(fy_generic v)
{
	fy_generic vd;

	if (!fy_generic_is_indirect(v))
		return fy_invalid;

	vd = fy_generic_indirect_get_diag(v);
	return vd;
}

fy_generic fy_generic_get_marker(fy_generic v)
{
	fy_generic vd;

	if (!fy_generic_is_indirect(v))
		return fy_invalid;

	vd = fy_generic_indirect_get_marker(v);
	return vd;
}

fy_generic fy_generic_get_comment(fy_generic v, enum fy_comment_placement placement)
{
	fy_generic vd;

	if (!fy_generic_is_indirect(v))
		return fy_invalid;

	vd = fy_generic_indirect_get_comment(v, placement);
	return vd;
}

fy_generic fy_generic_get_style(fy_generic v)
{
	fy_generic vd;

	if (!fy_generic_is_indirect(v))
		return fy_invalid;

	vd = fy_generic_indirect_get_style(v);
	return vd;
}

static int fy_generic_get_style_idx(fy_generic v)
{
	fy_generic vstyle;
	int idx;

	vstyle = fy_generic_get_style(v);
	idx = fy_cast(vstyle, -1);
	if (idx < 0)
		return -1;

	if (fy_generic_is_collection(v)) {
		if (idx >= FYCS_MAX)
			return -1;
	} else {
		if (idx >= FYSS_MAX)
			return -1;
	}
	return idx;
}

enum fy_node_style
fy_generic_get_node_style(fy_generic v)
{
	enum fy_node_style style;
	int idx;

	idx = fy_generic_get_style_idx(v);
	if (idx < 0)
		return FYNS_ANY;
	if (fy_generic_is_collection(v))
		style = FYNS_BLOCK - idx;	// XXX unfortunate we have it backwards
	else
		style = FYNS_PLAIN + idx;
	assert(style >= FYNS_FLOW && style < FYNS_ALIAS);
	return style;
}

enum fy_scalar_style
fy_generic_get_scalar_style(fy_generic v)
{
	enum fy_scalar_style sstyle;
	int idx;

	if (fy_generic_is_collection(v))
		return FYSS_ANY;

	idx = fy_generic_get_style_idx(v);
	if (idx < 0)
		return FYSS_ANY;
	sstyle = FYSS_PLAIN + idx;
	assert(sstyle < FYSS_MAX);
	return sstyle;
}

enum fy_collection_style
fy_generic_get_collection_style(fy_generic v)
{
	enum fy_collection_style cstyle;
	int idx;

	if (!fy_generic_is_collection(v))
		return FYCS_ANY;

	idx = fy_generic_get_style_idx(v);
	if (idx < 0)
		return FYCS_ANY;
	cstyle = FYCS_BLOCK + idx;
	assert(cstyle < FYCS_MAX);
	return cstyle;
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

static const struct fy_generic_builder_cfg default_generic_builder_cfg = {
	.flags = FYGBCF_SCHEMA_AUTO | FYGBCF_OWNS_ALLOCATOR,
	.allocator = NULL,	/* use default */
};

void fy_generic_builder_cleanup(struct fy_generic_builder *gb)
{
	if (!gb)
		return;

	if (gb->cleanup)
		gb->cleanup(gb);

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

	gb->schema = schema;
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
					FYAST_PER_TAG_FREE_DEDUP :
					FYAST_PER_TAG_FREE;
		auto_cfg.estimated_max_size = cfg->estimated_max_size;
		gb->allocator = fy_allocator_create("auto", &auto_cfg);	// let the system decide
		if (!gb->allocator)
			goto err_out;
		gb->flags |= FYGBF_OWNS_ALLOCATOR;
	} else {
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

	if (flags & FYGBCF_TRACE)
		fy_allocator_set_trace(a, true);

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

const struct fy_generic_builder_cfg *
fy_generic_builder_get_cfg(struct fy_generic_builder *gb)
{
	if (!gb)
		return NULL;
	return &gb->cfg;
}

enum fy_gb_flags
fy_generic_builder_get_flags(struct fy_generic_builder *gb)
{
	if (!gb)
		return 0;
	return gb->flags;
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

	if (gb->cleanup) {
		gb->cleanup(gb);
		gb->cleanup = NULL;
	}

	gb->userdata = NULL;

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

	/* direct invalids never contained */
	if (fy_generic_is_direct_invalid(v))
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

void *fy_gb_alloc(struct fy_generic_builder *gb, size_t size, size_t align)
{
	void *p;

	p = fy_allocator_alloc_nocheck(gb->allocator, gb->alloc_tag, size, align);
	if (!p)
		fy_atomic_fetch_add(&gb->allocation_failures, 1);
	return p;
}

void *fy_generic_builder_set_userdata(struct fy_generic_builder *gb, void *userdata)
{
	void *old_userdata;

	if (!gb)
		return NULL;
	old_userdata = gb->userdata;
	gb->userdata = userdata;
	return old_userdata;
}

fy_generic_builder_cleanup_fn
fy_generic_builder_set_cleanup(struct fy_generic_builder *gb, fy_generic_builder_cleanup_fn cleanup)
{
	fy_generic_builder_cleanup_fn old_cleanup;

	if (!gb)
		return NULL;

	old_cleanup = gb->cleanup;
	gb->cleanup = cleanup;

	return old_cleanup;
}

void *fy_generic_builder_get_userdata(struct fy_generic_builder *gb)
{
	if (!gb)
		return NULL;
	return gb->userdata;
}

fy_generic_builder_cleanup_fn
fy_generic_builder_get_cleanup(struct fy_generic_builder *gb)
{
	if (!gb)
		return NULL;

	return gb->cleanup;
}

void fy_gb_free(struct fy_generic_builder *gb, void *ptr)
{
	fy_allocator_free_nocheck(gb->allocator, gb->alloc_tag, ptr);
}

void fy_gb_trim(struct fy_generic_builder *gb)
{
	fy_allocator_trim_tag_nocheck(gb->allocator, gb->alloc_tag);
}

const void *fy_gb_store(struct fy_generic_builder *gb, const void *data, size_t size, size_t align)
{
	const void *p;

	p = fy_allocator_store_nocheck(gb->allocator, gb->alloc_tag, data, size, align);
	if (!p)
		fy_atomic_fetch_add(&gb->allocation_failures, 1);
	return p;
}

const void *fy_gb_storev(struct fy_generic_builder *gb, const struct iovec *iov, unsigned int iovcnt, size_t align)
{
	const void *p;

	p = fy_allocator_storev_nocheck(gb->allocator, gb->alloc_tag, iov, iovcnt, align);
	if (!p)
		fy_atomic_fetch_add(&gb->allocation_failures, 1);
	return p;
}

const void *fy_gb_lookupv(struct fy_generic_builder *gb, const struct iovec *iov, unsigned int iovcnt, size_t align)
{
	const void *ptr;
	uint64_t hash;

	if (!(gb->flags & FYGBF_DEDUP_ENABLED))
		return NULL;

	hash = fy_iovec_xxhash64(iov, iovcnt);

	while (gb && (gb->flags & FYGBF_DEDUP_ENABLED)) {
		ptr = fy_allocator_lookupv_nocheck(gb->allocator, gb->alloc_tag, iov, iovcnt, align, hash);
		if (ptr)
			return ptr;
		gb = gb->cfg.parent;
	}

	return NULL;
}

const void *fy_gb_lookup(struct fy_generic_builder *gb, const void *data, size_t size, size_t align)
{
	struct iovec iov[1];

	/* just call the storev */
	iov[0].iov_base = (void *)data;
	iov[0].iov_len = size;

	return fy_gb_lookupv(gb, iov, 1, align);
}

struct fy_allocator_info *
fy_gb_get_allocator_info(struct fy_generic_builder *gb)
{
	return fy_allocator_get_info_nocheck(gb->allocator, gb->alloc_tag);
}

void fy_gb_release(struct fy_generic_builder *gb, const void *ptr, size_t size)
{
	fy_allocator_release_nocheck(gb->allocator, gb->alloc_tag, ptr, size);
}

uint64_t fy_gb_allocation_failures(struct fy_generic_builder *gb)
{
	return fy_atomic_load(&gb->allocation_failures);
}

fy_generic
fy_gb_string_vcreate(struct fy_generic_builder *gb, const char *fmt, va_list ap)
{
	char buf[1024];
	va_list ap2;
	char *str;
	int size;
	bool alloc = false;
	fy_generic v = fy_invalid;

	va_copy(ap2, ap);

	str = buf;
	size = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	if (size < 0)
		return fy_invalid;

	if ((size_t)size >= sizeof(buf) - 1) {
		str = malloc(size + 1);
		if (!str)
			return fy_invalid;
		alloc = true;
		size = vsnprintf(str, size + 1, fmt, ap2);
	}
	if (size >= 0)
		v = fy_gb_string_size_create(gb, str, (size_t)size);

	if (alloc)
		free(str);

	return v;
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

fy_generic
fy_gb_null_type_create_out_of_place(struct fy_generic_builder *gb FY_UNUSED,
				    void *p FY_UNUSED)
{
	return fy_invalid;
}

fy_generic
fy_gb_bool_type_create_out_of_place(struct fy_generic_builder *gb FY_UNUSED,
				    bool state FY_UNUSED)
{
	return fy_invalid;
}

fy_generic fy_gb_dint_type_create_out_of_place(struct fy_generic_builder *gb, fy_generic_decorated_int vald)
{
	const struct fy_generic_decorated_int *valp;

	valp = fy_gb_lookup(gb, &vald, sizeof(vald), FY_GENERIC_SCALAR_ALIGN);
	if (!valp)
		valp = fy_gb_store(gb, &vald, sizeof(vald), FY_GENERIC_SCALAR_ALIGN);
	if (!valp)
		return fy_invalid;
	return (fy_generic){ .v = (uintptr_t)valp | FY_INT_OUTPLACE_V };
}

fy_generic fy_gb_int_type_create_out_of_place(struct fy_generic_builder *gb, long long val)
{
	return fy_gb_dint_type_create_out_of_place(gb,
		(struct fy_generic_decorated_int){ .sv = val, .flags = 0 });
}

fy_generic fy_gb_uint_type_create_out_of_place(struct fy_generic_builder *gb, unsigned long long val)
{
	return fy_gb_dint_type_create_out_of_place(gb,
		(struct fy_generic_decorated_int){ .uv = val,
		.flags = fy_int_is_unsigned(val) ? FYGDIF_UNSIGNED_RANGE_EXTEND	: 0 });
}

fy_generic fy_gb_float_type_create_out_of_place(struct fy_generic_builder *gb, double val)
{
	const double *valp;

	valp = fy_gb_lookup(gb, &val, sizeof(val), FY_SCALAR_ALIGNOF(double));
	if (!valp)
		valp = fy_gb_store(gb, &val, sizeof(val), FY_SCALAR_ALIGNOF(double));
	if (!valp)
		return fy_invalid;
	return (fy_generic){ .v = (uintptr_t)valp | FY_FLOAT_OUTPLACE_V };
}

fy_generic fy_gb_string_size_create_out_of_place(struct fy_generic_builder *gb, const char *str, size_t len)
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
	s = fy_gb_lookupv(gb, iov, ARRAY_SIZE(iov), FY_GENERIC_SCALAR_ALIGN);
	if (!s)
		s = fy_gb_storev(gb, iov, ARRAY_SIZE(iov), FY_GENERIC_SCALAR_ALIGN);
	if (!s)
		return fy_invalid;

	return (fy_generic){ .v = (uintptr_t)s | FY_STRING_OUTPLACE_V };
}

fy_generic fy_gb_string_create_out_of_place(struct fy_generic_builder *gb, const char *str)
{
	return fy_gb_string_size_create_out_of_place(gb, str, str ? strlen(str) : 0);
}

fy_generic fy_gb_szstr_create_out_of_place(struct fy_generic_builder *gb, const fy_generic_sized_string szstr)
{
	return fy_gb_string_size_create_out_of_place(gb, szstr.data, szstr.size);
}

fy_generic fy_gb_internalize_out_of_place(struct fy_generic_builder *gb, fy_generic v)
{
	fy_generic vi, new_v;
	const fy_generic_collection *colp;
	struct iovec iov[2];
	enum fy_generic_type type;
	uint64_t flags;
	size_t size, i, count;
	const void *valp;
	const uint8_t *str, *p;
	size_t len;
	const fy_generic *itemss;
	fy_generic *items;

	/* direct invalid */
	if (fy_generic_is_direct_invalid(v))
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
		flags = (uint64_t)(gi.flags & FYGIF_CONTENT_MASK);
		while (flags) {
			i = fy_bit64_lowest(flags);
			flags &= ~FY_BIT64(i);
			vi = fy_gb_internalize(gb, gi.vindirect[i]);
			if (fy_generic_is_invalid(vi))
				return fy_invalid;
			gi.vindirect[i] = vi;
		}

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
				if (fy_generic_is_invalid(vi))
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
	uint64_t flags;
	size_t i, count;
	const fy_generic *itemss;
	fy_generic_indirect gi;

	if (fy_generic_is_invalid(v))
		return fy_invalid;

	/* if it's in place just return it */
	if (fy_generic_is_in_place(v))
		return v;

	/* if it's builder contained, then it's valid */
	if (fy_generic_builder_contains(gb, v))
		return v;

	/* indirects are handled here (note, aliases are indirect too) */
	if (fy_generic_is_indirect(v)) {

		fy_generic_indirect_get(v, &gi);
		flags = (uint64_t)(gi.flags & FYGIF_CONTENT_MASK);
		while (flags) {
			i = fy_bit64_lowest(flags);
			flags &= ~FY_BIT64(i);
			vi = fy_gb_validate(gb, gi.vindirect[i]);
			if (fy_generic_is_invalid(vi))
				return fy_invalid;
		}

		return v;
	}

	type = fy_generic_get_type(v);

	if (type == FYGT_SEQUENCE || type == FYGT_MAPPING) {
		colp = fy_generic_resolve_collection_ptr(v);
		itemss = fy_generic_collectionp_get_items(type, colp, &count);
		for (i = 0; i < count; i++) {
			vi = fy_gb_validate(gb, itemss[i]);
			if (fy_generic_is_invalid(vi))
				return fy_invalid;
		}
	}

	return v;
}

fy_generic fy_validate_out_of_place(fy_generic v)
{
	return fy_gb_validate_out_of_place(NULL, v);
}

fy_generic fy_gb_indirect_create(struct fy_generic_builder *gb, const fy_generic_indirect *gi)
{
	fy_generic vbuf[1 + FYGIIDX_MAX];	/* flag + maximum content */
	fy_generic *vp;
	uint64_t flags;
	size_t i, total;
	const void *p;

	flags = (uint64_t)(gi->flags & FYGIF_CONTENT_MASK);
	vp = vbuf;
	(vp++)->v = gi->flags;
	while (flags) {
		i = fy_bit64_lowest(flags);
		flags &= ~FY_BIT64(i);
		*vp++ = gi->vindirect[i];
	}
	total = vp - vbuf;

	p = fy_gb_store(gb, vbuf, total * sizeof(vbuf[0]), FY_CONTAINER_ALIGNOF(fy_generic));	/* must be at least 16 */
	if (!p)
		return fy_invalid;

	return (fy_generic){ .v = (uintptr_t)p | FY_INDIRECT_V };
}

fy_generic fy_gb_alias_create(struct fy_generic_builder *gb, fy_generic anchor)
{
	fy_generic_indirect gi;

	fy_generic_indirect_reset(&gi);
	gi.flags = FYGIF_VALUE | FYGIF_ANCHOR | FYGIF_ALIAS;
	gi.value = fy_null;
	gi.anchor = anchor;

	return fy_gb_indirect_create(gb, &gi);
}

fy_generic fy_gb_create_scalar_from_text(struct fy_generic_builder *gb,
					 const char *text, size_t len,
					 enum fy_generic_type force_type)
{
	enum fy_generic_schema schema;
	fy_generic v;
	const char *s, *e, *ss;
	const char *dec, *fract, *exp;
	char sign, exp_sign;
	size_t dec_count, fract_count, exp_count, tlen, i;
	int base;
	long long lv;
	unsigned long long ulv;
	double dv;
	char *tbuf, *t;
	bool is_json, had_dot, had_pfx;
	char cn, underscore_sep;

	if (len == FY_NT)
		len = strlen(text);

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
	had_pfx = false;
	switch (schema) {
	case FYGS_YAML1_2_FAILSAFE:
	case FYGS_YAML1_1_FAILSAFE:
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
		    if (!memcmp(text, ".nan", 4) || !memcmp(text, ".NaN", 4) || !memcmp(text, ".NAN", 4)) {
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
	case FYGS_YAML1_1_PYYAML:

		if (len == 0 || (len == 1 && *text == '~')) {
			v = fy_null;
			break;
		}

		if (schema != FYGS_YAML1_1_PYYAML && len == 1) {
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
			if (!memcmp(text, "no", 2) || !memcmp(text, "No", 2) || !memcmp(text, "NO", 2)) {
				v = fy_false;
				break;
			}
		}

		if (len == 3) {
			if (!memcmp(text, "off", 3) || !memcmp(text, "Off", 3) || !memcmp(text, "OFF", 3)) {
				v = fy_false;
				break;
			}
			if (!memcmp(text, "yes", 3) || !memcmp(text, "Yes", 3) || !memcmp(text, "YES", 3)) {
				v = fy_true;
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
		    if (!memcmp(text, ".nan", 4) || !memcmp(text, ".NaN", 4) || !memcmp(text, ".NAN", 4)) {
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

	case FYGS_PYTHON:
		/* we don't handle the python schema here */
		return fy_invalid;
	default:
		break;
	}

	if (fy_generic_is_valid(v)) {
		if (force_type != FYGT_INVALID && fy_generic_get_type(v) != force_type)
			return fy_invalid;
		return v;
	}

	/* both yaml1.1 and python schemas allow underscores */
	underscore_sep = (fy_generic_schema_is_yaml_1_1(schema) ||
			  schema == FYGS_PYTHON) ? '_' : '\0';

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

	dec = s;
	if (s < e && *s == '0') {
		cn = (s + 1) < e ? s[1] : -1;

		switch (schema) {
		case FYGS_YAML1_2_JSON:
		case FYGS_JSON:
			/* json does not allow a 0000... form */
			if (cn == '0')
				goto do_string;
			break;

		case FYGS_YAML1_2_CORE:
			if (cn == 'x') {
				base = 16;
				s += 2;
				dec = s;
				had_pfx = true;
			} else if (cn == 'o') {
				base = 8;
				s += 2;
				dec = s;
				had_pfx = true;
			}
			break;

		case FYGS_YAML1_1:
		case FYGS_YAML1_1_PYYAML:
			if (cn == 'x') {
				base = 16;
				s += 2;
				dec = s;
				had_pfx = true;
			} else if (cn == 'b') {
				base = 2;
				s += 2;
				dec = s;
				had_pfx = true;
			} else if (cn > 0) {
				/* we have to scan forward; to find out if it's really octal */
				ss = s + 1;
				while (ss < e && ((*ss >= '0' && *ss <= '7') || *ss == underscore_sep))
					ss++;
				if (ss == e) {
					base = 8;
					s++;
					dec = s;
					had_pfx = true;
				} else {
					if (*ss == '.')
						break;
					goto do_string;
				}
			} else {	/* just zero */
				base = 10;
				dec = s;
			}
			break;

		default:
			goto do_string;
		}
	}

	/* consume digits */
	dec_count = 0;
	if (base == 16) {
		while (s < e) {
			if (isxdigit((unsigned char)*s))
				dec_count++;
			else if (*s != underscore_sep)
				break;
			s++;
		}
	} else if (base > 0 && base <= 10) {
		while (s < e) {
			if (*s >= '0' && *s <= ('0' + base - 1))
			       dec_count++;
			else if ((!dec_count && !had_pfx) || *s != underscore_sep)
				break;
			s++;
		}
	}

	/* +.5 is illegal */
	if ((sign == '+' || sign == '-') && !dec_count)
		goto do_string;

	/* extract the fractional part */
	if (s < e && *s == '.') {
		if (base != 10)
			goto do_string;
		had_dot = true;
		fract = ++s;
		fract_count = 0;
		while (s < e) {
		       	if (isdigit((unsigned char)*s))
				fract_count++;
			else if (!fract_count || *s != underscore_sep)
				break;
			s++;
		}
		/* just a . */
		if (!dec_count && !fract_count)
			goto do_string;
	} else
		had_dot = false;

	/* extract the exponent part */
	if (s < e && (dec_count || fract_count) && (*s == 'e' || *s == 'E')) {
		if (base != 10)
			goto do_string;

		/* yaml1.1 expect dot for exps */
		if (fy_generic_schema_is_yaml_1_1(schema) && !had_dot)
			goto do_string;

		s++;
		exp_sign = 0;
		if (s < e && (*s == '+' || *s == '-'))
			exp_sign = *s++;
		if (fy_generic_schema_is_yaml_1_1(schema) && !exp_sign)
			goto do_string;
		exp = s;
		exp_count = 0;
		while (s < e) {
			if (isdigit((unsigned char)*s))
				exp_count++;
			else if (!exp_count || *s != underscore_sep)
				break;
			s++;
		}
		/* 1e or 1e+ */
		if (!exp_count)
			goto do_string;
	}

	/* not fully consumed? then just a string */
	if (s < e)
		goto do_string;

	// fprintf(stderr, "\n base=%d sign=%c dec=%.*s fract=%.*s exp_sign=%c exp=%.*s\n",
	//		base, sign, (int)dec_count, dec, (int)fract_count, fract, exp_sign, (int)exp_count, exp);

	/* no decimal units nor any fractional */
	if (!dec && !fract_count)
		goto do_string;

	/* JSON does not allow .NNN floats or NNN. */
	if (is_json && (!dec_count || (had_dot && !fract_count && !exp_count)))
		goto do_string;

	tlen = (size_t)(s - dec);

	/* nothing? no digits at all, it's a string */
	if (!tlen)
		goto do_string;

	tbuf = alloca(1 + tlen + 1);

	t = tbuf;
	if (sign == '+' || sign == '-')
		*t++ = sign;
	for (i = 0; i < tlen; i++) {
		if (dec[i] != underscore_sep)
			*t++ = dec[i];
	}
	*t = '\0';

	/* it is an integer */
	if ((force_type == FYGT_INVALID || force_type == FYGT_INT) &&
		(!had_dot && !fract_count && !exp_count)) {

		errno = 0;
		lv = strtoll(tbuf, NULL, base);
		if (errno == 0)
			return fy_gb_to_generic(gb, lv);

		if (errno == ERANGE) {
			errno = 0;
			ulv = strtoull(tbuf, NULL, base);
			if (errno == 0)
				return fy_gb_to_generic(gb, ulv);
		}

		if (force_type == FYGT_INVALID)
			goto do_string;
		return fy_invalid;

	} else if (force_type == FYGT_INVALID || force_type == FYGT_FLOAT) {

		errno = 0;
		dv = strtod(tbuf, NULL);
		if (errno == ERANGE)
			goto do_string;

		return fy_gb_to_generic(gb, dv);
	}

do_string:
	if (force_type != FYGT_INVALID && force_type != FYGT_STRING)
		return fy_invalid;

	return fy_gb_string_size_create(gb, text, len);
}

/* Type conversion functions - schema-aware where appropriate */

fy_generic fy_gb_to_int(struct fy_generic_builder *gb, fy_generic v)
{
	enum fy_generic_type type;
	fy_generic_sized_string szstr;
	long long lv;
	double dv;
	_Bool bv;

	/* Handle indirect/alias - recursive */
	if (fy_generic_is_indirect(v))
		v = fy_generic_indirect_get_value(v);

	type = fy_generic_get_type(v);

	switch (type) {
	case FYGT_INT:
		/* Already an int, return as-is */
		return v;

	case FYGT_BOOL:
		/* true → 1, false → 0 */
		bv = fy_cast(v, (_Bool)false);
		return fy_gb_to_generic(gb, bv ? 1LL : 0LL);

	case FYGT_FLOAT:
		/* Truncate toward zero (C-style cast) */
		dv = fy_cast(v, (double)0.0);
		/* Check for NaN/Infinity - error */
		if (isnan(dv) || isinf(dv))
			return fy_invalid;
		lv = (long long)dv;
		return fy_gb_to_generic(gb, lv);

	case FYGT_STRING:
		/* Parse as base-10 integer */
		/* TODO: Support underscore separators in numeric literals (e.g., "1_000_000") */
		szstr = fy_cast(v, fy_szstr_empty);
		if (!szstr.data || szstr.size == 0)
			return fy_invalid;

		return fy_gb_create_scalar_from_text(gb, szstr.data, szstr.size, FYGT_INT);

	case FYGT_NULL:
	case FYGT_SEQUENCE:
	case FYGT_MAPPING:
		/* Error - no valid conversion */
		return fy_invalid;

	case FYGT_ALIAS:
		/* Shouldn't reach here since we handled indirect above */
		return fy_invalid;

	default:
		return fy_invalid;
	}
}

fy_generic fy_gb_to_float(struct fy_generic_builder *gb, fy_generic v)
{
	enum fy_generic_type type;
	fy_generic_decorated_int dint;
	fy_generic_sized_string szstr;
	double dv;
	_Bool bv;

	/* Handle indirect/alias - recursive */
	if (fy_generic_is_indirect(v))
		v = fy_generic_indirect_get_value(v);

	type = fy_generic_get_type(v);

	switch (type) {
	case FYGT_FLOAT:
		/* Already a float, return as-is */
		return v;

	case FYGT_INT:
		/* Convert to double - check for decorated int */
		dint = fy_cast(v, fy_dint_empty);
		if (dint.flags & FYGDIF_UNSIGNED_RANGE_EXTEND) {
			/* Large unsigned value - convert carefully */
			dv = (double)dint.uv;
		} else {
			/* Regular signed */
			dv = (double)dint.sv;
		}
		return fy_gb_to_generic(gb, dv);

	case FYGT_BOOL:
		/* true → 1.0, false → 0.0 */
		bv = fy_cast(v, (_Bool)false);
		return fy_gb_to_generic(gb, bv ? 1.0 : 0.0);

	case FYGT_STRING:
		/* Parse as floating point */
		/* TODO: Support underscore separators in numeric literals (e.g., "1_000.5") */
		szstr = fy_cast(v, fy_szstr_empty);
		if (!szstr.data || szstr.size == 0)
			return fy_invalid;

		return fy_gb_create_scalar_from_text(gb, szstr.data, szstr.size, FYGT_FLOAT);

	case FYGT_NULL:
	case FYGT_SEQUENCE:
	case FYGT_MAPPING:
		/* Error - no valid conversion */
		return fy_invalid;

	case FYGT_ALIAS:
		/* Shouldn't reach here since we handled indirect above */
		return fy_invalid;

	default:
		return fy_invalid;
	}
}

fy_generic fy_gb_to_string(struct fy_generic_builder *gb, fy_generic v)
{
	enum fy_generic_type type;
	enum fy_generic_schema schema;
	fy_generic_decorated_int dint;
	_Bool bv;
	double dv;
	const char *str;

	/* Handle indirect/alias - recursive */
	if (fy_generic_is_indirect(v)) {
		if (fy_generic_get_type(v) == FYGT_ALIAS)
			return fy_generic_indirect_get_anchor(v);
		v = fy_generic_indirect_get_value(v);
	}

	type = fy_generic_get_type(v);
	schema = fy_gb_get_schema(gb);

	switch (type) {
	case FYGT_STRING:
		/* Already a string, return as-is */
		return v;

	case FYGT_INT:
		/* Format as decimal string */
		dint = fy_cast(v, fy_dint_empty);
		if (dint.flags & FYGDIF_UNSIGNED_RANGE_EXTEND)
			return fy_gb_string_createf(gb, "%llu", dint.uv);
		else
			return fy_gb_string_createf(gb, "%lld", dint.sv);

	case FYGT_FLOAT:
		/* Format with full precision */
		dv = fy_cast(v, (double)0.0);
		return fy_gb_string_createf(gb, "%.17g", dv);

	case FYGT_BOOL:
		/* Schema-aware formatting */
		bv = fy_cast(v, (_Bool)false);
		if (schema == FYGS_PYTHON)
			str = bv ? "True" : "False";
		else
			str = bv ? "true" : "false";
		return fy_gb_string_create(gb, str);

	case FYGT_NULL:
		/* Schema-aware formatting */
		if (schema == FYGS_PYTHON)
			str = "None";
		else
			str = "null";
		return fy_gb_string_create(gb, str);

	case FYGT_SEQUENCE:
	case FYGT_MAPPING:
		return fy_gb_emit(gb, v, FYOPPF_DISABLE_DIRECTORY | FYOPEF_STYLE_ONELINE |
				  FYOPEF_OUTPUT_TYPE_STRING | FYOPEF_NO_ENDING_NEWLINE, NULL);

	case FYGT_ALIAS:
		/* Shouldn't reach here since we handled indirect above */
		return fy_invalid;

	default:
		return fy_invalid;
	}
}

fy_generic fy_gb_to_bool(struct fy_generic_builder *gb, fy_generic v)
{
	enum fy_generic_type type;
	enum fy_generic_schema schema;
	fy_generic_sized_string szstr;
	fy_generic_decorated_int dint;
	double dv;
	size_t count;
	_Bool bv;

	/* Handle indirect/alias - recursive */
	if (fy_generic_is_indirect(v))
		v = fy_generic_indirect_get_value(v);

	type = fy_generic_get_type(v);
	schema = fy_gb_get_schema(gb);
	if (schema == FYGS_AUTO)
		schema = FYGS_YAML1_2_CORE;

	switch (type) {
	case FYGT_BOOL:
		/* Already a bool, return as-is */
		return v;

	case FYGT_INT:
		/* 0 → false, non-zero → true (all schemas) */
		dint = fy_cast(v, fy_dint_empty);
		if (dint.flags & FYGDIF_UNSIGNED_RANGE_EXTEND) {
			bv = (dint.uv != 0);
		} else {
			bv = (dint.sv != 0);
		}
		return fy_gb_to_generic(gb, bv);

	case FYGT_FLOAT:
		/* 0.0 → false, non-zero → true (all schemas) */
		dv = fy_cast(v, (double)0.0);
		bv = (dv != 0.0);
		return fy_gb_to_generic(gb, bv);

	case FYGT_STRING:
		szstr = fy_cast(v, fy_szstr_empty);

		/* Python schema: empty "" → False, non-empty → True */
		if (schema == FYGS_PYTHON) {
			bv = (szstr.size > 0);
			return fy_gb_to_generic(gb, bv);
		}
		return fy_gb_create_scalar_from_text(gb, szstr.data, szstr.size, FYGT_BOOL);

	case FYGT_NULL:
		/* Python: False */
		if (schema == FYGS_PYTHON) {
			return fy_gb_to_generic(gb, (_Bool)false);
		}
		/* YAML schemas: Error */
		return fy_invalid;

	case FYGT_SEQUENCE:
		/* Python: empty → False, non-empty → True */
		if (schema == FYGS_PYTHON) {
			(void)fy_generic_sequence_get_items(v, &count);
			bv = (count > 0);
			return fy_gb_to_generic(gb, bv);
		}
		/* YAML schemas: Error */
		return fy_invalid;

	case FYGT_MAPPING:
		/* Python: empty → False, non-empty → True */
		if (schema == FYGS_PYTHON) {
			(void)fy_generic_mapping_get_pairs(v, &count);
			bv = (count > 0);
			return fy_gb_to_generic(gb, bv);
		}
		/* YAML schemas: Error */
		return fy_invalid;

	case FYGT_ALIAS:
		/* Shouldn't reach here since we handled indirect above */
		return fy_invalid;

	default:
		return fy_invalid;
	}
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
		if (fy_generic_is_invalid(valb))
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

	ba = (int)fy_cast(a, (_Bool)false);
	bb = (int)fy_cast(b, (_Bool)false);
	return ba > bb ?  1 :
	       ba < bb ? -1 : 0;
}

static inline int fy_generic_int_compare(fy_generic a, fy_generic b)
{
	fy_generic_decorated_int dia, dib;
	bool dia_unsigned, dib_unsigned;
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

	dia_unsigned = !!(dia.flags & FYGDIF_UNSIGNED_RANGE_EXTEND);
	dib_unsigned = !!(dib.flags & FYGDIF_UNSIGNED_RANGE_EXTEND);

	/* if bit patterns match, we're good immediately */
	if (dia.sv == dib.sv)
		return 0;

	/* signed, unsigned match, compare */
	if (dia_unsigned && dib_unsigned)
		return dia.uv < dib.uv ? -1 : 1;
	if (!dia_unsigned && !dib_unsigned)
		return dia.sv < dib.sv ? -1 : 1;

	/* one is signed the other is unsigned */

	/* if any of them is signed and less than 0 */
	if (!dia_unsigned && dia.sv < 0)
		return -1;
	if (!dib_unsigned && dib.sv < 0)
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

	sa = fy_castp(&aa, "");
	sb = fy_castp(&ab, "");

	return strcmp(sa, sb);
}

int fy_generic_compare_out_of_place(fy_generic a, fy_generic b)
{
	enum fy_generic_type at, bt;

	/* invalids are always non-matching */
	if (fy_generic_is_invalid(a) || fy_generic_is_invalid(b))
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
	fy_generic *vp;
	enum fy_generic_type type;
	fy_generic *items;
	uint64_t flags;
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
		vp = (fy_generic *)fy_generic_resolve_ptr(v);

		flags = (uint64_t)(vp->v & FYGIF_CONTENT_MASK);
		vp++;
		while (flags) {
			i = fy_bit64_lowest(flags);
			flags &= ~FY_BIT64(i);
			*vp = fy_generic_relocate(start, end, *vp, d);
			vp++;
		}

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

const void *fy_generic_builder_linearize(struct fy_generic_builder *gb, fy_generic *vp, size_t *sizep)
{
	struct fy_allocator_info *info = NULL;
	struct fy_allocator_tag_info *tag_info;
	struct fy_allocator_arena_info *arena_info;
	struct fy_arena_reloc *arenas = NULL, *arena;
	const void *linear_data = NULL;
	fy_generic v, linear_v = fy_invalid;
	size_t size = 0, linear_size = 0, offset;
	unsigned int i, j, num_arenas;

	if (!gb || !vp || !sizep)
		return NULL;

	v = *vp;
	*vp = fy_invalid;
	*sizep = 0;

	info = fy_allocator_get_info(gb->allocator, gb->alloc_tag);
	if (!info)
		return NULL;

	if (info->num_tag_infos == 1 && info->tag_infos[0].num_arena_infos == 1) {
		arena_info = &info->tag_infos[0].arena_infos[0];
		linear_v = v;
		linear_data = arena_info->data;
		linear_size = arena_info->size;
		goto out;
	}

	num_arenas = 0;
	for (i = 0; i < info->num_tag_infos; i++) {
		tag_info = &info->tag_infos[i];
		num_arenas += tag_info->num_arena_infos;
		for (j = 0; j < tag_info->num_arena_infos; j++) {
			arena_info = &tag_info->arena_infos[j];
			size = fy_size_t_align(size, FY_GENERIC_CONTAINER_ALIGN);
			size += arena_info->size;
		}
	}

	if (!num_arenas || !size)
		goto out;

	arenas = alloca(sizeof(*arenas) * num_arenas);

	free(gb->linear);
	gb->linear = malloc(size);
	if (!gb->linear)
		goto out;

	arena = arenas;
	offset = 0;
	for (i = 0; i < info->num_tag_infos; i++) {
		tag_info = &info->tag_infos[i];
		for (j = 0; j < tag_info->num_arena_infos; j++) {
			arena_info = &tag_info->arena_infos[j];
			offset = fy_size_t_align(offset, FY_GENERIC_CONTAINER_ALIGN);
			arena->src.p = arena_info->data;
			arena->size = arena_info->size;
			arena->srce.p = arena->src.p + arena->size;
			arena->dst.p = gb->linear + offset;
			memcpy(arena->dst.p, arena->src.p, arena->size);
			offset += arena->size;
			arena++;
		}
	}

	qsort(arenas, num_arenas, sizeof(*arenas), fy_arena_reloc_src_compare);

	linear_v = fy_generic_arena_relocate(arenas, num_arenas, gb->linear, size, v);
	if (fy_generic_is_invalid(linear_v))
		goto out;

	linear_data = gb->linear;
	linear_size = size;

out:
	free(info);
	*vp = linear_v;
	*sizep = linear_size;
	return linear_data;
}

static const char *generic_schema_txt[FYGS_COUNT] = {
	[FYGS_AUTO]		= "auto",
	[FYGS_YAML1_2_FAILSAFE]	= "yaml1.2-failsafe",
	[FYGS_YAML1_2_CORE]	= "yaml1.2-core",
	[FYGS_YAML1_2_JSON]	= "yaml1.2-json",
	[FYGS_YAML1_1_FAILSAFE]	= "yaml1.1-failsafe",
	[FYGS_YAML1_1]		= "yaml1.1",
	[FYGS_YAML1_1_PYYAML]	= "yaml1.1-pyyaml",
	[FYGS_JSON]		= "json",
	[FYGS_PYTHON]		= "python",
};

const char *fy_generic_schema_get_text(enum fy_generic_schema schema)
{
	const char *txt;

	txt = (unsigned int)schema < ARRAY_SIZE(generic_schema_txt) ? generic_schema_txt[schema] : NULL;
	return txt ? txt : "";
}

enum fy_generic_schema
fy_generic_schema_from_text(const char *text)
{
	enum fy_generic_schema schema;

	if (!text || !*text)
		return FYGS_INVALID;

	/* aliases */
	if (!strcmp(text, "failsafe"))
		return FYGS_YAML1_2_FAILSAFE;
	if (!strcmp(text, "core"))
		return FYGS_YAML1_2_CORE;
	if (!strcmp(text, "pyyaml"))
		return FYGS_YAML1_1_PYYAML;

	for (schema = 0; schema < FYGS_COUNT; schema++) {
		if (!strcmp(text, fy_generic_schema_get_text(schema)))
			return schema;
	}

	return FYGS_INVALID;
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
	static const char *scalar_styles_txt[] = {
		[FYSS_PLAIN] = "plain",
		[FYSS_SINGLE_QUOTED] = "single-quoted",
		[FYSS_DOUBLE_QUOTED] = "double-quoted",
		[FYSS_LITERAL] = "literal",
		[FYSS_FOLDED] = "folded",
	};
	static const char *collection_styles_txt[] = {
		[FYCS_BLOCK] = "block",
		[FYCS_FLOW] = "flow",
	};
	const char *style_txt;
	enum fy_generic_type type;
	fy_generic vtag, vanchor, v, iv, key, value;
	fy_generic vdiag, vmark, vstyle;
	fy_generic vtopc, vrightc, vbottomc;
	const char *tag = NULL, *anchor = NULL;
	const fy_generic *items;
	const fy_generic_map_pair *pairs;
	fy_generic_sized_string szstr;
	struct fy_generic_decorated_int dint;
	size_t i, count;

	vanchor = fy_generic_get_anchor(vv);
	vtag = fy_generic_get_tag(vv);
	vdiag = fy_generic_get_diag(vv);
	vmark = fy_generic_get_marker(vv);
	vstyle = fy_generic_get_style(vv);

	vtopc = fy_generic_get_comment(vv, fycp_top);
	vrightc = fy_generic_get_comment(vv, fycp_right);
	vbottomc = fy_generic_get_comment(vv, fycp_bottom);

	anchor = fy_castp(&vanchor, (const char *)NULL);
	tag = fy_castp(&vtag, (const char *)NULL);
	v = fy_generic_is_indirect(vv) ? fy_generic_indirect_get_value(vv) : vv;

	type = fy_generic_get_type(v);

	fprintf(fp, "%*s", level * 2, "");

	if (v.v != vv.v)
		fprintf(fp, "(%016llx) ", (unsigned long long)vv.v);
	if (anchor)
		fprintf(fp, "&%s ", anchor);
	if (tag)
		fprintf(fp, "%s ", tag);
	if (fy_generic_is_valid(vdiag))
		fprintf(fp, "diag: %s ", fygstra(vdiag));
	if (fy_generic_is_valid(vmark))
		fprintf(fp, "mark: %s ", fygstra(vmark));
	if (fy_generic_is_int(vstyle)) {
		i = fy_generic_cast_unsigned_int_default(vstyle, UINT_MAX);
		if (fy_generic_type_is_scalar(type) && i < ARRAY_SIZE(scalar_styles_txt))
			style_txt = scalar_styles_txt[i];
		else if (fy_generic_type_is_collection(type) && i < ARRAY_SIZE(collection_styles_txt))
			style_txt = collection_styles_txt[i];
		else
			style_txt = NULL;
		if (style_txt)
			fprintf(fp, "style=%s ", style_txt);
	}
	if (fy_generic_is_valid(vtopc))
		fprintf(fp, "top#: %s ", fygstra(vtopc));
	if (fy_generic_is_valid(vrightc))
		fprintf(fp, "right#: %s ", fygstra(vrightc));
	if (fy_generic_is_valid(vbottomc))
		fprintf(fp, "bottom#: %s ", fygstra(vbottomc));

	fprintf(fp, "%016llx ", (unsigned long long)v.v);

	switch (type) {
	case FYGT_NULL:
		fprintf(fp, "<null>  %s\n", "null");
		return;

	case FYGT_BOOL:
		fprintf(fp, "<bool>  %s\n", fy_cast(v, false) ? "true" : "false");
		return;

	case FYGT_INT:
		dint = fy_cast(v, fy_dint_empty);
		if (!(dint.flags & FYGDIF_UNSIGNED_RANGE_EXTEND))
			fprintf(fp, "<int>   %lld\n", dint.sv);
		else
			fprintf(fp, "<int>   %llu\n", dint.uv);
		return;

	case FYGT_FLOAT:
		fprintf(fp, "<float> %f\n", fy_cast(v, (double)0.0));
		return;

	case FYGT_STRING:
		szstr = fy_cast(v, fy_szstr_empty);
		fprintf(fp, "<str>   %s\n", fy_utf8_format_text_a(szstr.data, szstr.size, fyue_doublequote));
		return;

	case FYGT_SEQUENCE:
		fprintf(fp, "<seq>   [\n");
		items = fy_generic_sequence_get_items(v, &count);
		for (i = 0; i < count; i++) {
			iv = items[i];
			fy_generic_dump_primitive(fp, level + 1, iv);
		}
		fprintf(fp, "%*s]\n", level * 2, "");
		break;

	case FYGT_MAPPING:
		fprintf(fp, "<map>   {\n");
		pairs = fy_generic_mapping_get_pairs(v, &count);
		for (i = 0; i < count; i++) {
			key = pairs[i].key;
			value = pairs[i].value;
			fy_generic_dump_primitive(fp, level + 1, key);
			fy_generic_dump_primitive(fp, level + 1, value);
		}
		fprintf(fp, "%*s}\n", level * 2, "");
		break;

	case FYGT_ALIAS:
		vanchor = fy_generic_get_anchor(v);
		fprintf(fp, "<alias> %s\n", fy_castp(&vanchor, ""));
		break;

	case FYGT_INVALID:
		fprintf(fp, "<inval> invalid\n");
		break;

	default:
		FY_IMPOSSIBLE_ABORT();
	}
}

bool fy_generic_has_comments(fy_generic v)
{
	fy_generic_indirect gi;

	fy_generic_indirect_get(v, &gi);
	return !!(gi.flags & (FYGIF_TOP_COMMENT | FYGIF_RIGHT_COMMENT | FYGIF_BOTTOM_COMMENT));
}

char *fy_generic_get_comments(fy_generic v)
{
	fy_generic vc;
	enum fy_comment_placement placement;
	const char *str;
	char *accum, *new_accum;
	size_t size, accum_size, new_accum_size;
	bool has_nl;

	if (!fy_generic_has_comments(v))
		return NULL;

	str = NULL;
	accum = NULL;
	accum_size = 0;
	for (placement = fycp_top; placement < fycp_max; placement++) {

		vc = fy_generic_get_comment(v, placement);
		if (!fy_generic_is_string(vc))
			continue;
		str = fy_cast(vc, "");
		size = strlen(str);
		has_nl = size > 0 && str[size-1] == '\n';
		new_accum_size = accum_size + size + !has_nl + 1;
		new_accum = realloc(accum, new_accum_size);
		if (!new_accum) {
			if (accum)
				free(accum);
			return NULL;
		}
		accum = new_accum;
		memcpy(accum + accum_size, str, size);
		accum_size += size;
		if (!has_nl)
			accum[accum_size++] = '\n';
		accum[accum_size] = '\0';
	}

	return accum;
}

int fy_generic_dir_get_document_count(fy_generic vdir)
{
	fy_generic vds;
	size_t idx, len;

	if (fy_generic_is_mapping(vdir))
		return fy_generic_is_valid(fy_generic_vds_get_root(vdir)) ? 1 : -1;
	if (fy_generic_is_sequence(vdir)) {
		len = fy_len(vdir);
		for (idx = 0; idx < len; idx++) {
			vds = fy_get(vdir, idx, fy_invalid);
			if (!fy_generic_is_valid(fy_generic_vds_get_root(vds)))
				break;
		}
		if (!idx)
			return -1;
		return (int)idx;
	}
	return -1;
}

fy_generic
fy_generic_dir_get_document_vds(fy_generic vdir, size_t idx)
{
	if (fy_generic_is_mapping(vdir))
		return vdir;
	if (fy_generic_is_sequence(vdir))
		return fy_get(vdir, idx, fy_invalid);
	return fy_invalid;
}

fy_generic
fy_generic_vds_get_root(fy_generic vds)
{
	return fy_generic_mapping_get_value(vds, fy_string("root"));
}

struct fy_document_state *
fy_generic_vds_get_document_state(fy_generic vds)
{
	struct fy_version vers_local, *vers = NULL;
	struct fy_document_state *fyds;
	const struct fy_tag **tags = NULL;
	struct fy_tag *tag;
	fy_generic vmap, vseq, v;
	size_t i, tag_count;

	if (!fy_generic_is_mapping(vds))
		return fy_document_state_default(NULL, NULL);

	vmap = fy_get(vds, "version", fy_map_empty);
	vers_local.major = fy_get(vmap, "major", -1);
	vers_local.minor = fy_get(vmap, "minor", -1);
	if (vers_local.major < 0 || vers_local.minor < 0) {
		vers_local.major = 1;
		vers_local.minor = 2;
	} else
		vers = &vers_local;

	vseq = fy_get(vds, "tags", fy_map_empty);
	tag_count = fy_len(vseq);
	if (tag_count > 0) {
		tags = alloca(sizeof(*tags) * (tag_count + 1));
		for (i = 0; i < tag_count; i++) {
			tag = alloca(sizeof(*tag));
			v = fy_get(vseq, i, fy_invalid);
			tag->handle = fy_get(v, "handle", "");
			tag->prefix = fy_get(v, "prefix", "");
			tags[i] = tag;
		}
		tags[i] = NULL;
	} else
		tags = NULL;

	fyds = fy_document_state_default(vers, tags);
	if (!fyds)
		return NULL;

	fyds->version_explicit = fy_get(vds, "version-explicit", (_Bool)false);
	fyds->tags_explicit = fy_get(vds, "tags-explicit", (_Bool)false);
	fyds->start_implicit = fy_get(vds, "start-implicit", (_Bool)true);
	fyds->end_implicit = fy_get(vds, "end-implicit", (_Bool)true);

	return fyds;
}

fy_generic
fy_generic_vds_create_from_document_state(struct fy_generic_builder *gb, fy_generic vroot,
					  struct fy_document_state *fyds)
{
	const struct fy_version *vers;
	struct fy_tag **tags;
	size_t i, count;
	fy_generic *vtags_items;
	bool version_explicit;
	bool tags_explicit;
	bool start_implicit;
	bool end_implicit;
	const char *schema_txt;
	fy_generic vds, vtags;

	vers = fy_document_state_version(fyds);

	count = 0;
	tags = fy_document_state_tag_directives(fyds);
	if (tags) {
		while (tags[count])
			count++;
	}

	version_explicit = fy_document_state_version_explicit(fyds);
	tags_explicit = fy_document_state_tags_explicit(fyds);
	start_implicit = fy_document_state_start_implicit(fyds);
	end_implicit = fy_document_state_end_implicit(fyds);

	vtags_items = alloca(sizeof(*vtags_items) * count);
	for (i = 0; i < count; i++)
		vtags_items[i] = fy_gb_mapping(gb,
					"handle", tags[i]->handle,
					"prefix", tags[i]->prefix);

	if (tags)
		free(tags);
	tags = NULL;

	vtags = fy_gb_sequence_create(gb, count, vtags_items);

	schema_txt = fy_generic_schema_get_text(fy_gb_get_schema(gb));

	vds = fy_gb_mapping(gb,
		"root", vroot,
		"version", fy_mapping(gb,
				"major", vers->major,
				"minor", vers->minor),
		"version-explicit", (_Bool)version_explicit,
		"start-implicit", (_Bool)start_implicit,
		"end-implicit", (_Bool)end_implicit,
		"tags", vtags,
		"tags-explicit", (_Bool)tags_explicit,
		"schema", schema_txt);

	return vds;
}

size_t
fy_document_state_format_tag(struct fy_document_state *fyds,
			     const char *tag, size_t tag_size,
			     char *buf, size_t maxsz)
{
	const char *full_tag;
	size_t full_tag_size;
	const char *tag_handle, *tag_suffix;
	size_t tag_handle_size, tag_suffix_size;
	int rc;
	size_t formatted_tag_size;

	if (!tag)
		return 0;

	if (tag_size == FY_NT)
		tag_size = strlen(tag);

	full_tag = tag;
	full_tag_size = tag_size;
	if (tag_size >= 4 && tag[0] == '!' && tag[1] == '<' && tag[tag_size - 1] == '>') {
		full_tag = tag + 2;
		full_tag_size = tag_size - 3;
	}

	rc = fy_document_state_shorten_tag(fyds, full_tag, full_tag_size,
			&tag_handle, &tag_handle_size,
			&tag_suffix, &tag_suffix_size);
	if (!rc) {
		formatted_tag_size = tag_handle_size + tag_suffix_size;
		if (buf && maxsz > 0) {
			if (tag_handle_size >= maxsz)
				tag_handle_size = maxsz - 1;
			memcpy(buf, tag_handle, tag_handle_size);
			if (tag_handle_size < maxsz - 1) {
				size_t copy = tag_suffix_size;
				if (tag_handle_size + copy >= maxsz)
					copy = maxsz - 1 - tag_handle_size;
				memcpy(buf + tag_handle_size, tag_suffix, copy);
				buf[tag_handle_size + copy] = '\0';
			} else
				buf[tag_handle_size] = '\0';
		}
	} else {
		formatted_tag_size = tag_size;
		if (buf && maxsz > 0) {
			size_t copy = tag_size;
			if (copy >= maxsz)
				copy = maxsz - 1;
			memcpy(buf, tag, copy);
			buf[copy] = '\0';
		}
	}

	return formatted_tag_size;
}

char *
fy_document_state_format_tag_alloc(struct fy_document_state *fyds,
				   const char *tag, size_t tag_size,
				   size_t *formatted_tag_sizep)
{
	char *formatted_tag;
	size_t formatted_tag_size;

	formatted_tag_size = fy_document_state_format_tag(fyds, tag, tag_size, NULL, 0);
	if (!formatted_tag_size && !tag)
		return NULL;

	formatted_tag = malloc(formatted_tag_size + 1);
	if (!formatted_tag)
		return NULL;

	(void)fy_document_state_format_tag(fyds, tag, tag_size,
					   formatted_tag, formatted_tag_size + 1);
	if (formatted_tag_sizep)
		*formatted_tag_sizep = formatted_tag_size;

	return formatted_tag;
}

struct fy_token *
fy_document_state_generic_create_token(struct fy_document_state *fyds, fy_generic v,
				       enum fy_token_type type, enum fy_scalar_style style)
{
	struct fy_token *fyt = NULL;
	struct fy_input *fyi = NULL;
	char *data = NULL;
	struct fy_tag_scan_info info;
	int handle_length, uri_length, prefix_length;
	const char *handle_start;
	struct fy_token *fyt_td = NULL;
	fy_generic_sized_string szstr;
	fy_generic vstr;
	struct fy_atom handle;
	int rc;

	vstr = fy_convert(v, FYGT_STRING);
	if (!fy_generic_is_string(vstr))
		goto err_out;

	szstr = fy_cast(vstr, fy_szstr_empty);

	handle_length = 0;
	uri_length = 0;
	prefix_length = 0;

	if (type == FYTT_TAG) {
		data = fy_document_state_format_tag_alloc(fyds, szstr.data, szstr.size, &szstr.size);
		if (!data)
			goto err_out;

		memset(&info, 0, sizeof(info));

		rc = fy_tag_scan(data, szstr.size, &info);
		if (rc)
			goto err_out;

		handle_length = info.handle_length;
		uri_length = info.uri_length;
		prefix_length = info.prefix_length;

		handle_start = data + prefix_length;

		fyt_td = fy_document_state_lookup_tag_directive(fyds,
				handle_start, handle_length);
		if (!fyt_td)
			goto err_out;

		fyi = fy_input_from_malloc_data(data, szstr.size, &handle, false);
		if (!fyi)
			goto err_out;
		data = NULL;

		handle.style = FYAS_URI;

	} else {
		data = malloc(szstr.size);
		if (!data)
			goto err_out;
		memcpy(data, szstr.data, szstr.size);

		fyi = fy_input_from_malloc_data(data, szstr.size, &handle, false);
		if (!fyi)
			goto err_out;
		data = NULL;

	}

	if ((unsigned int)style >= FYSS_MAX)
		style = FYSS_ANY;

	switch (type) {
	case FYTT_SCALAR:
		fyt = fy_token_create(FYTT_SCALAR, &handle, style);
		break;
	case FYTT_ALIAS:
		fyt = fy_token_create(FYTT_ALIAS, &handle, NULL);
		break;
	case FYTT_ANCHOR:
		fyt = fy_token_create(FYTT_ANCHOR, &handle);
		break;
	case FYTT_TAG:
		fyt = fy_token_create(FYTT_TAG, &handle, prefix_length, handle_length, uri_length, fyt_td);
		break;
	default:
		break;
	}
	if (!fyt)
		goto err_out;

	fy_input_unref(fyi);

	return fyt;

err_out:
	if (data)
		free(data);
	fy_input_unref(fyi);
	return NULL;
}
