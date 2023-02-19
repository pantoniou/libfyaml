/*
 * fy-generic-op.c - Generic operations
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

static int fy_generic_seqmap_qsort_cmp(const void *a, const void *b)
{
	const fy_generic *vpa = a, *vpb = b;
	/* the keys are first for maps */
	return fy_generic_compare(*vpa, *vpb);
}

#define FY_GB_OP_IOV_INPLACE		8
#define FY_GB_OP_ITEMS_INPLACE		64
#define FY_GB_OP_WORK_ITEMS_INPLACE	64

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
	fy_op_fn fn;
	fy_generic *work_items;
	size_t work_item_count;
	size_t removed_items;
	fy_generic vresult;
};

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
#define FYGCODSF_CHECK_MATCHING_COLLECTION_ITEM	FY_BIT(8)

int fy_generic_collection_op_data_setup(struct fy_generic_collection_op_data *cod,
		struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		fy_generic in, enum fy_generic_type out_type, const struct fy_generic_op_args *args,
		unsigned int xflags)
{
	size_t i, j, k, tmp_item_count;
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

	/* verify that the collections types match */
	if (xflags & FYGCODSF_CHECK_MATCHING_COLLECTION_ITEM) {
		if (cod->type == FYGT_SEQUENCE) {
			for (i = 0; i < cod->item_count; i++) {
				if (!fy_generic_is_sequence(cod->items[i]))
					goto err_out;
			}
		} else if (cod->type == FYGT_MAPPING) {
			for (i = 0; i < cod->item_count; i++) {
				if (!fy_generic_is_mapping(cod->items[i]))
					goto err_out;
			}
		} else
			goto err_out;
	}

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
	if (!p) {
		p = fy_gb_storev(cod->gb, iov, iovcnt, FY_GENERIC_CONTAINER_ALIGN);
		if (!p)
			goto err_out;
	}

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
			FYGCODSF_NEED_WORK_IN_ITEMS | FYGCODSF_NEED_WORK_ITEMS_EXPANDED |
			FYGCODSF_CHECK_MATCHING_COLLECTION_ITEM);
	if (rc)
		return fy_invalid;

	if (args->common.count && !args->common.items)
		goto err_out;

	k = 0;
	if (cod->type == FYGT_SEQUENCE) {
		/* the extra arguments */
		for (i = cod->item_count - 1; (ssize_t)i >= 0; i--) {
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
			tmp_items = fy_generic_mapping_get_items(cod->items[i], &tmp_item_count);
			for (j = tmp_item_count - 2; (ssize_t)j >= 0; j -= 2) {
				cod->work_items_all[k++] = tmp_items[j];
				cod->work_items_all[k++] = tmp_items[j + 1];
			}
		}
		/* the original */
		for (j = cod->in_item_count - 2; (ssize_t)j >= 0; j -= 2) {
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
			FYGCODSF_NEED_COPY_WORK_IN_ITEMS | FYGCODSF_NEED_COPY_WORK_ITEMS_EXPANDED |
			FYGCODSF_CHECK_MATCHING_COLLECTION_ITEM);
	if (rc)
		return fy_invalid;

	if (args->common.count && !args->common.items)
		goto err_out;

	if (cod->type != FYGT_SEQUENCE)
		goto err_out;

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
	size_t k;
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_INVALID, args,
			FYGCODSF_MAP_ITEM_COUNT_NO_MULT2 |
			FYGCODSF_NEED_WORK_IN_ITEMS | FYGCODSF_NEED_WORK_ITEMS_EXPANDED |
			FYGCODSF_NEED_COPY_WORK_IN_ITEMS | FYGCODSF_NEED_COPY_WORK_ITEMS_EXPANDED |
			FYGCODSF_CHECK_MATCHING_COLLECTION_ITEM);
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
	if (cod->type == FYGT_SEQUENCE)
		qsort(cod->work_items_all, k, sizeof(fy_generic), fy_generic_seqmap_qsort_cmp);
	else
		qsort(cod->work_items_all, k / 2, 2 * sizeof(fy_generic), fy_generic_seqmap_qsort_cmp);

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

static fy_generic
fy_generic_op_set_at(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_op_args args_local, *args_new = &args_local;

	if (!args->common.items || args->common.count != 1)
		return fy_invalid;

	memset(args_new, 0, sizeof(*args_new));

	args_new->common.count = 1;
	args_new->common.items = args->common.items;
	args_new->common.tp = NULL;
	args_new->insert_replace_get_set_at.idx = args->insert_replace_get_set_at.idx;

	return fy_generic_op_args(gb, FYGBOPF_REPLACE, in, args);
}

static fy_generic
fy_generic_op_set_at_path(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	size_t i, path_count;
	fy_generic out = fy_invalid;
	fy_generic items_local[64];
	fy_generic *items_alloc = NULL;
	fy_generic *items;
	const fy_generic *path;
	size_t item_count;
	fy_generic value, v;

	if (args->common.count && !args->common.items)
		goto err_out;

	/* need at least 1 value */
	if (args->common.count < 1)
		goto err_out;

	path = args->common.items;
	path_count = args->common.count - 1;
	value = args->common.items[path_count];

	/* set / - replace */
	if (!path_count) {
		out = fy_generic_op_internalize(gb, flags, value);
		goto out;
	}

	item_count = path_count;

	if (item_count <= ARRAY_SIZE(items_local)) {
		items = items_local;
	} else {
		items_alloc = malloc(sizeof(*items) * item_count);
		if (!items_alloc)
			goto err_out;
		items = items_alloc;
	}

	/*
	 * in = [ 10, [ 100, 200 ] ]
	 * items = [ 1, 1, 2000 ]
	 * value = 2000
	 * path = [ 1, 1 ]
	 *
	 * item_count = 3
	 * path_count = 2
	 *
	 * v = [10, [100,200]]
	 * i = 0: work_items[0] = [10, [100, 200]]
	 *        v = [ 10, [100, 200]]/1 -> [100, 200]
	 *  work_items[1] = v = [ 100, 200 ];
	 *  ---
	 *  work_items[2] = 2000
	 *  work_items = [ [10, [100, 200]], [100, 200], 2000 ]
	 *
	 * v = value : (2000)
	 *
	 *        i = 1 -> 0
	 *        value = set(work_items[i], path[i], value)
	 * i = 1: value = set(work_items[1], path[1], value)
	 *        value = set([100, 200], 1, 2000)
	 *        value = [100, 2000]
	 * i = 0: value = set(work_items[0], path[0], value)
	 *        value = set([10, [100, 200]], 1, [100, 2000])
	 *        value = [10, [100, 2000]]
	 *
	 */

	/* first go down recording as we go */
	v = in;
	for (i = 0; i < path_count - 1; i++) {
		items[i] = v;
		v = fy_generic_op(gb, FYGBOPF_GET, v, 1, (fy_generic []){path[i]});
		if (fy_generic_is_invalid(v))
			goto err_out;
	}
	items[i] = v;
	i++;
	assert(path_count == i);
	while (i-- > 0) {
		value = fy_generic_op(gb, FYGBOPF_SET, items[i], 2, (fy_generic []) { path[i], value });
		if (fy_generic_is_invalid(v))
			goto err_out;
	}
	out = value;
out:
	if (items_alloc)
		free(items_alloc);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_get(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	fy_generic key;
	fy_generic out = fy_invalid;
	size_t idx;

	if (args->common.count && !args->common.items)
		goto err_out;

	/* one key exactly */
	if (args->common.count != 1)
		goto err_out;

	key = args->common.items[0];

	if (fy_generic_is_sequence(in)) {
		idx = fy_cast(key, (size_t)-1);
		if (idx == (size_t)-1)
			goto err_out;
		out = fy_generic_sequence_get_item_generic(in, idx);
	} else if (fy_generic_is_mapping(in))
		out = fy_generic_mapping_get_value(in, key);
	else
		out = fy_invalid;

	if (fy_generic_is_invalid(out))
		goto err_out;

	out = fy_generic_op_internalize(gb, flags, out);
out:
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_get_at(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	size_t idx;
	fy_generic out;

	if (args->common.count)
		goto err_out;

	idx = args->insert_replace_get_set_at.idx;
	if ((ssize_t)idx < 0)
		goto err_out;

	if (fy_generic_is_sequence(in)) {
		const fy_generic_sequence *seqp = fy_generic_sequence_resolve(in);
		if (idx >= seqp->count)
			goto err_out;
		out = seqp->items[idx];
	} else if (fy_generic_is_mapping(in)) {
		const fy_generic_mapping *mapp = fy_generic_mapping_resolve(in);
		if (idx >= mapp->count)
			goto err_out;
		out = mapp->pairs[idx].value;
	} else
		goto err_out;

	out = fy_generic_op_internalize(gb, flags, out);
out:
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_get_at_path(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	size_t i, path_count;
	fy_generic out = fy_invalid;
	const fy_generic *path;
	fy_generic v;

	if (args->common.count && !args->common.items)
		goto err_out;

	path = args->common.items;
	path_count = args->common.count;

	/* need at least 1 path component, if not return self */
	if (!path_count) {
		out = fy_generic_op_internalize(gb, flags, in);
		goto out;
	}

	/* go down */
	v = in;
	for (i = 0; i < path_count; i++) {
		v = fy_generic_op(gb, FYGBOPF_GET, v, 1, (fy_generic []){path[i]});
		if (fy_generic_is_invalid(v))
			goto err_out;
	}
	/* found it */
	out = v;
out:
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static void
fy_op_dummy_work(void *varg)
{
	/* nothing */
}

static void
fy_op_filter_sequence_fn_work(void *varg)
{
	struct fy_op_work_arg *arg = varg;
	fy_generic_filter_pred_fn fn = arg->fn.filter_pred;
	size_t i, j;

	for (i = 0, j = 0; i < arg->work_item_count; i++) {
		if (!fn(arg->gb, arg->work_items[i])) {
			arg->work_items[i] = fy_invalid;
			j++;
		}
	}
	arg->removed_items = j;
}

static void
fy_op_filter_mapping_fn_work(void *varg)
{
	struct fy_op_work_arg *arg = varg;
	fy_generic_filter_pred_fn fn = arg->fn.filter_pred;
	size_t i, j;

	for (i = 0, j = 0; i < arg->work_item_count; i += 2) {
		if (!fn(arg->gb, arg->work_items[i + 1])) {
			arg->work_items[i] = fy_invalid;
			arg->work_items[i + 1] = fy_invalid;
			j += 2;
		}
	}
	arg->removed_items = j;
}

#if defined(__BLOCKS__)
static void
fy_op_filter_sequence_block_work(void *varg)
{
	struct fy_op_work_arg *arg = varg;
	fy_generic_filter_pred_block blk = arg->fn.filter_pred_blk;
	size_t i, j;

	for (i = 0, j = 0; i < arg->work_item_count; i++) {
		if (!blk(arg->gb, arg->work_items[i])) {
			arg->work_items[i] = fy_invalid;
			j++;
		}
	}
	arg->removed_items = j;
}

static void
fy_op_filter_mapping_block_work(void *varg)
{
	struct fy_op_work_arg *arg = varg;
	fy_generic_filter_pred_block blk = arg->fn.filter_pred_blk;
	size_t i, j;

	for (i = 0, j = 0; i < arg->work_item_count; i += 2) {
		if (!blk(arg->gb, arg->work_items[i + 1])) {
			arg->work_items[i] = fy_invalid;
			arg->work_items[i + 1] = fy_invalid;
			j += 2;
		}
	}
	arg->removed_items = j;
}
#endif

static void
fy_op_map_sequence_fn_work(void *varg)
{
	struct fy_op_work_arg *arg = varg;
	fy_generic_map_xform_fn fn = arg->fn.map_xform;
	fy_generic v;
	size_t i;

	for (i = 0; i < arg->work_item_count; i++) {
		v = fn(arg->gb, arg->work_items[i]);
		if (fy_generic_is_invalid(v)) {
			arg->vresult = v;
			return;
		}
		arg->work_items[i] = v;
	}
	arg->vresult = fy_true;
}

static void
fy_op_map_mapping_fn_work(void *varg)
{
	struct fy_op_work_arg *arg = varg;
	fy_generic_map_xform_fn fn = arg->fn.map_xform;
	fy_generic v;
	size_t i;

	for (i = 0; i < arg->work_item_count; i += 2) {
		v = fn(arg->gb, arg->work_items[i + 1]);
		if (fy_generic_is_invalid(v)) {
			arg->vresult = v;;
			return;
		}
		arg->work_items[i + 1] = v;
	}
	arg->vresult = fy_true;
}

#if defined(__BLOCKS__)
static void
fy_op_map_sequence_block_work(void *varg)
{
	struct fy_op_work_arg *arg = varg;
	fy_generic_map_xform_block blk = arg->fn.map_xform_blk;
	fy_generic v;
	size_t i, j;

	for (i = 0, j = 0; i < arg->work_item_count; i++) {
		v = blk(arg->gb, arg->work_items[i]);
		if (fy_generic_is_invalid(v)) {
			arg->vresult = v;
			return;
		}
		arg->work_items[i] = v;
	}
	arg->vresult = fy_true;
}

static void
fy_op_map_mapping_block_work(void *varg)
{
	struct fy_op_work_arg *arg = varg;
	fy_generic_map_xform_block blk = arg->fn.map_xform_blk;
	fy_generic v;
	size_t i, j;

	for (i = 0, j = 0; i < arg->work_item_count; i += 2) {
		v = blk(arg->gb, arg->work_items[i + 1]);
		if (fy_generic_is_invalid(v)) {
			arg->vresult = v;
			return;
		}
		arg->work_items[i + 1] = v;
	}
	arg->vresult = fy_true;
}
#endif

static void
fy_op_reduce_sequence_fn_work(void *varg)
{
	struct fy_op_work_arg *arg = varg;
	fy_generic_reducer_fn fn = arg->fn.reducer;
	fy_generic acc;
	size_t i;

	acc = arg->vresult;
	for (i = 0; i < arg->work_item_count; i++) {
		acc = fn(arg->gb, acc, arg->work_items[i]);
		if (fy_generic_is_invalid(acc))
			break;
	}
	arg->vresult = acc;
}

static void
fy_op_reduce_mapping_fn_work(void *varg)
{
	struct fy_op_work_arg *arg = varg;
	fy_generic_reducer_fn fn = arg->fn.reducer;
	fy_generic acc;
	size_t i;

	acc = arg->vresult;
	for (i = 0; i < arg->work_item_count; i += 2) {
		acc = fn(arg->gb, acc, arg->work_items[i + 1]);
		if (fy_generic_is_invalid(acc))
			break;
	}
	arg->vresult = acc;
}

#if defined(__BLOCKS__)
static void
fy_op_reduce_sequence_block_work(void *varg)
{
	struct fy_op_work_arg *arg = varg;
	fy_generic_reducer_block blk = arg->fn.reducer_blk;
	fy_generic acc;
	size_t i;

	acc = arg->vresult;
	for (i = 0; i < arg->work_item_count; i++) {
		acc = blk(arg->gb, acc, arg->work_items[i]);
		if (fy_generic_is_invalid(acc))
			break;
	}
	arg->vresult = acc;
}

static void
fy_op_reduce_mapping_block_work(void *varg)
{
	struct fy_op_work_arg *arg = varg;
	fy_generic_reducer_block blk = arg->fn.reducer_blk;
	fy_generic acc;
	size_t i;

	acc = arg->vresult;
	for (i = 0; i < arg->work_item_count; i += 2) {
		acc = blk(arg->gb, acc, arg->work_items[i + 1]);
		if (fy_generic_is_invalid(acc))
			break;
	}
	arg->vresult = acc;
}
#endif

static inline fy_work_exec_fn
fy_select_op_exec_fn(enum fy_gb_op_flags flags, enum fy_generic_type type)
{
	unsigned int op;

	op = ((flags >> FYGBOPF_OP_SHIFT) & FYGBOPF_OP_MASK);
	if (op >= FYGBOP_COUNT)
		return NULL;

	switch (op) {
	case FYGBOP_FILTER:
		if (flags & FYGBOPF_BLOCK_FN) {
#if defined(__BLOCKS__)
			return type == FYGT_SEQUENCE ? fy_op_filter_sequence_block_work :
			       type == FYGT_MAPPING  ? fy_op_filter_mapping_block_work : NULL;
#else
			return NULL;
#endif
		}
		return type == FYGT_SEQUENCE ? fy_op_filter_sequence_fn_work :
		       type == FYGT_MAPPING  ? fy_op_filter_mapping_fn_work : NULL;
		break;

	case FYGBOP_MAP:
		if (flags & FYGBOPF_BLOCK_FN) {
#if defined(__BLOCKS__)
			return type == FYGT_SEQUENCE ? fy_op_map_sequence_block_work :
			       type == FYGT_MAPPING  ? fy_op_map_mapping_block_work : NULL;
#else
			return NULL;
#endif
		}
		return type == FYGT_SEQUENCE ? fy_op_map_sequence_fn_work :
		       type == FYGT_MAPPING  ? fy_op_map_mapping_fn_work : NULL;
		break;
	case FYGBOP_REDUCE:
		if (flags & FYGBOPF_BLOCK_FN) {
#if defined(__BLOCKS__)
			return type == FYGT_SEQUENCE ? fy_op_reduce_sequence_block_work :
			       type == FYGT_MAPPING  ? fy_op_reduce_mapping_block_work : NULL;
#else
			return NULL;
#endif
		}
		return type == FYGT_SEQUENCE ? fy_op_reduce_sequence_fn_work :
		       type == FYGT_MAPPING  ? fy_op_reduce_mapping_fn_work : NULL;
		break;
	default:
		break;
	}
	return NULL;
}

struct fy_generic_parallel_op_data {
	struct fy_generic_builder *gb;
	enum fy_gb_op_flags flags;
	struct fy_thread_pool *tp;
	enum fy_generic_type type;
	struct fy_op_work_arg *work_args;
	struct fy_thread_work *works;
	struct fy_op_work_arg work_args_local[32];
	struct fy_thread_work works_local[32];
	struct fy_thread_pool *tp_alloc;
	size_t num_threads, work_num_threads;
};

static void
fy_generic_parallel_op_data_cleanup(struct fy_generic_parallel_op_data *pd)
{
	if (pd->works && pd->works != pd->works_local) {
		free(pd->works);
		pd->works = NULL;
	}
	if (pd->work_args && pd->work_args != pd->work_args_local) {
		free(pd->work_args);
		pd->work_args = NULL;
	}
	if (pd->tp_alloc) {
		fy_thread_pool_destroy(pd->tp_alloc);
		pd->tp_alloc = NULL;
	}
}

static int
fy_generic_parallel_op_data_setup(struct fy_generic_parallel_op_data *pd,
				  struct fy_generic_builder *gb,
				  enum fy_gb_op_flags flags,
				  struct fy_thread_pool *tp,
				  enum fy_generic_type type,
				  fy_generic *work_items, size_t work_item_count)
{
	size_t i, chunk_size, start_idx, count_items;
	size_t num_threads;
	size_t work_num_threads;
	size_t max_num_threads;

	memset(pd, 0, sizeof(*pd));

	pd->gb = gb;
	pd->flags = flags;
	pd->tp = tp;
	pd->type = type;

	/* create thread pool if requested */
	if ((flags & FYGBOPF_PARALLEL) && !tp) {
		pd->tp_alloc = fy_thread_pool_create(
				&(struct fy_thread_pool_cfg){
					.flags = FYTPCF_STEAL_MODE,
					.num_threads = 0 });
		if (!pd->tp_alloc)
			goto err_out;
		tp = pd->tp_alloc;
	}
	pd->tp = tp;

	if (tp) {
		num_threads = fy_thread_pool_get_num_threads(tp);
		if (!num_threads)
			goto err_out;
	} else {
		num_threads = 1;
	}

	max_num_threads = pd->type == FYGT_SEQUENCE ? work_item_count : (work_item_count / 2);

	work_num_threads = num_threads >= max_num_threads ?
				max_num_threads : num_threads;

	pd->num_threads = num_threads;
	pd->work_num_threads = work_num_threads;

	if (work_num_threads <= ARRAY_SIZE(pd->work_args_local))
		pd->work_args = pd->work_args_local;
	else {
		pd->work_args = malloc(sizeof(*pd->work_args) * work_num_threads);
		if (!pd->work_args)
			goto err_out;
		memset(pd->work_args, 0, sizeof(*pd->work_args) * work_num_threads);
	}

	if (num_threads <= ARRAY_SIZE(pd->works_local))
		pd->works = pd->works_local;
	else {
		pd->works = malloc(sizeof(*pd->works) * num_threads);
		if (!pd->works)
			goto err_out;
		memset(pd->works, 0, sizeof(*pd->works) * num_threads);
	}

	if (work_num_threads > 1) {

		/* distribute evenly */
		chunk_size = (work_item_count + work_num_threads - 1) / work_num_threads;

		/* for mappings the chunk must be even */
		if (pd->type == FYGT_MAPPING && (chunk_size & 1))
			chunk_size++;

		assert(chunk_size <= work_item_count);

		for (i = 0; i < work_num_threads; i++) {
			start_idx = i * chunk_size;
			count_items = chunk_size;

			/* Last chunk might be smaller */
			if (start_idx >= work_item_count)
				break;
			if (start_idx + count_items > work_item_count)
				count_items = work_item_count - start_idx;

			pd->work_args[i].gb = pd->gb;
			pd->work_args[i].work_items = work_items + start_idx;
			pd->work_args[i].work_item_count = count_items;

			pd->works[i].arg = &pd->work_args[i];
		}
		/* and the dummy ones */
		for (; i < num_threads; i++)
			pd->works[i].fn = fy_op_dummy_work;

	} else {
		pd->work_args[0].gb = pd->gb;
		pd->work_args[0].work_items = work_items;
		pd->work_args[0].work_item_count = work_item_count;
	}

	return 0;

err_out:
	fy_generic_parallel_op_data_cleanup(pd);
	return -1;
}

static void
fy_generic_parallel_op_data_exec(struct fy_generic_parallel_op_data *pd,
				 fy_work_exec_fn exec_fn, fy_op_fn fn)
{
	size_t i;

	if (pd->work_num_threads > 1) {
		assert(pd->tp);
		/* parallel, multithreaded */
		for (i = 0; i < pd->work_num_threads; i++) {
			pd->work_args[i].fn = fn;
			pd->works[i].fn = exec_fn;
		}
		fy_thread_work_join(pd->tp, pd->works, pd->num_threads, NULL);
	} else {
		/* single threaded */
		pd->work_args[0].fn = fn;
		exec_fn(pd->work_args);
	}
}

static fy_generic
fy_generic_op_filter(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_parallel_op_data pd_local, *pd = &pd_local;
	struct fy_generic_collection col;
	fy_op_fn fn;
	fy_work_exec_fn exec_fn;
	fy_generic *work_items;
	size_t work_item_count;
	struct iovec iov[2];
	size_t i, j, k;
	fy_generic v;
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_INVALID, args,
			FYGCODSF_MAP_ITEM_COUNT_NO_MULT2 |
			FYGCODSF_NEED_WORK_IN_ITEMS | FYGCODSF_NEED_WORK_ITEMS_EXPANDED |
			FYGCODSF_NEED_COPY_WORK_IN_ITEMS | FYGCODSF_NEED_COPY_WORK_ITEMS_EXPANDED |
			FYGCODSF_CHECK_MATCHING_COLLECTION_ITEM);
	if (rc)
		return fy_invalid;

	if (args->common.count && !args->common.items)
		goto err_out;

	/* nothing? return empty */
	if (cod->work_item_all_count == 0)  {
		out = cod->type == FYGT_SEQUENCE ? fy_seq_empty : fy_map_empty;
		goto out;
	}

	if (flags & FYGBOPF_BLOCK_FN) {
#if defined(__BLOCKS__)
		fn.blk = args->filter_map_reduce_common.blk;
		if (!fn.blk)
			goto err_out;
#else
		goto err_out;
#endif
	} else {
		fn.fn = args->filter_map_reduce_common.fn;
		if (!fn.fn)
			goto err_out;
	}

	exec_fn = fy_select_op_exec_fn(flags, cod->type);
	if (!exec_fn)
		goto err_out;

	work_items = cod->work_items_all;
	work_item_count = cod->work_item_all_count;

	rc = fy_generic_parallel_op_data_setup(pd,
			gb, flags, args->common.tp, cod->type,
			work_items, work_item_count);
	if (rc)
		goto err_out;

	fy_generic_parallel_op_data_exec(pd, exec_fn, fn);

	/* collect the amount of removed counts */
	for (i = 0, j = 0; i < pd->work_num_threads; i++)
		j += pd->work_args[i].removed_items;

	fy_generic_parallel_op_data_cleanup(pd);

	/* if everything removed, return empty */
	if (j == work_item_count) {
		out = cod->type == FYGT_SEQUENCE ? fy_seq_empty : fy_map_empty;
		goto out;
	}

	/* if nothing removed, return the input */
	if (j == 0) {
		out = fy_generic_op_internalize(gb, flags, in);
		goto out;
	}

	/* something was removed, go over the items and remove the invalids */
	for (i = 0, k = 0; i < work_item_count; i++) {
		v = work_items[i];
		if (v.v != fy_invalid_value)
			work_items[k++] = v;
	}

	/* the collection header */
	col.count = cod->type == FYGT_SEQUENCE ? k : (k / 2);
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);
	/* the content */
	iov[1].iov_base = work_items;
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
fy_generic_op_map(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_parallel_op_data pd_local, *pd = &pd_local;
	struct fy_generic_collection col;
	fy_op_fn fn;
	fy_work_exec_fn exec_fn;
	fy_generic *work_items;
	size_t work_item_count;
	struct iovec iov[2];
	size_t i, k;
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_INVALID, args,
			FYGCODSF_MAP_ITEM_COUNT_NO_MULT2 |
			FYGCODSF_NEED_WORK_IN_ITEMS | FYGCODSF_NEED_WORK_ITEMS_EXPANDED |
			FYGCODSF_NEED_COPY_WORK_IN_ITEMS | FYGCODSF_NEED_COPY_WORK_ITEMS_EXPANDED |
			FYGCODSF_CHECK_MATCHING_COLLECTION_ITEM);
	if (rc)
		return fy_invalid;

	if (args->common.count && !args->common.items)
		goto err_out;

	/* nothing? return empty */
	if (cod->work_item_all_count == 0)  {
		out = cod->type == FYGT_SEQUENCE ? fy_seq_empty : fy_map_empty;
		goto out;
	}

	if (flags & FYGBOPF_BLOCK_FN) {
#if defined(__BLOCKS__)
		fn.blk = args->filter_map_reduce_common.blk;
		if (!fn.blk)
			goto err_out;
#else
		goto err_out;
#endif
	} else {
		fn.fn = args->filter_map_reduce_common.fn;
		if (!fn.fn)
			goto err_out;
	}

	exec_fn = fy_select_op_exec_fn(flags, cod->type);
	if (!exec_fn)
		goto err_out;

	work_items = cod->work_items_all;
	work_item_count = cod->work_item_all_count;

	rc = fy_generic_parallel_op_data_setup(pd,
			gb, flags, args->common.tp, cod->type,
			work_items, work_item_count);
	if (rc)
		goto err_out;

	fy_generic_parallel_op_data_exec(pd, exec_fn, fn);

	/* check for errors */
	for (i = 0; i < pd->work_num_threads; i++) {
		if (pd->work_args[i].vresult.v != fy_true_value)
			goto err_out;
	}

	fy_generic_parallel_op_data_cleanup(pd);

	k = work_item_count;

	/* the collection header */
	col.count = cod->type == FYGT_SEQUENCE ? k : (k / 2);
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);
	/* the content */
	iov[1].iov_base = work_items;
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
fy_generic_op_reduce(const struct fy_generic_op_desc *desc,
		     struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		     fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_parallel_op_data pd_local, *pd = &pd_local;
	fy_op_fn fn;
	fy_work_exec_fn exec_fn;
	fy_generic *work_items;
	size_t work_item_count;
	size_t i;
	fy_generic acc, v;
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_INVALID, args,
			FYGCODSF_MAP_ITEM_COUNT_NO_MULT2 |
			FYGCODSF_NEED_WORK_IN_ITEMS | FYGCODSF_NEED_WORK_ITEMS_EXPANDED |
			FYGCODSF_NEED_COPY_WORK_IN_ITEMS | FYGCODSF_NEED_COPY_WORK_ITEMS_EXPANDED |
			FYGCODSF_CHECK_MATCHING_COLLECTION_ITEM);
	if (rc)
		return fy_invalid;

	if (args->common.count && !args->common.items)
		goto err_out;

	if (flags & FYGBOPF_BLOCK_FN) {
#if defined(__BLOCKS__)
		fn.reducer_blk = args->reduce.blk;
		if (!fn.blk)
			goto err_out;
#else
		goto err_out;
#endif
	} else {
		fn.reducer = args->reduce.fn;
		if (!fn.fn)
			goto err_out;
	}
	acc = args->reduce.acc;

	/* nothing? return the accumulator */
	if (cod->work_item_all_count == 0) {
		out = fy_generic_op_internalize(gb, flags, acc);
		goto out;
	}

	exec_fn = fy_select_op_exec_fn(flags, cod->type);
	if (!exec_fn)
		goto err_out;

	work_items = cod->work_items_all;
	work_item_count = cod->work_item_all_count;

	rc = fy_generic_parallel_op_data_setup(pd,
			gb, flags, args->common.tp, cod->type,
			work_items, work_item_count);
	if (rc)
		goto err_out;

	/* seed everything with the accumulator */
	for (i = 0; i < pd->work_num_threads; i++)
		pd->work_args[i].vresult = acc;

	/* execute the parallel reduce step */
	fy_generic_parallel_op_data_exec(pd, exec_fn, fn);

	if (pd->work_num_threads > 1) {
		/* final reduce step, collect the results (overwrite work_items) */
		assert(pd->work_num_threads <= work_item_count);
		for (i = 0; i < pd->work_num_threads; i++) {
			v = pd->work_args[i].vresult;
			if (fy_generic_is_invalid(v))
				goto err_out;
			work_items[i] = v;
		}
		work_item_count = pd->work_num_threads;

		/* single threaded final result */
		pd->work_args[0].fn = fn;
		pd->work_args[0].vresult = acc;
		pd->work_args[0].work_items = work_items;
		pd->work_args[0].work_item_count = work_item_count;
		exec_fn(pd->work_args);
	}

	acc = pd->work_args[0].vresult;

	fy_generic_parallel_op_data_cleanup(pd);

	out = fy_generic_op_internalize(gb, flags, acc);
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

/* Internal helper for slicing with normalized indices */
static fy_generic
fy_generic_op_slice_internal(struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
			      fy_generic in, const struct fy_generic_op_args *args,
			      size_t start, size_t end)
{
	struct fy_generic_collection_op_data cod_local, *cod = &cod_local;
	struct fy_generic_collection col;
	struct iovec iov[2];
	size_t slice_count;
	fy_generic out = fy_invalid;
	int rc;

	rc = fy_generic_collection_op_data_setup(cod, gb, flags,
			in, FYGT_SEQUENCE, args, 0);
	if (rc)
		return fy_invalid;

	/* clamp end to sequence length */
	if (end == SIZE_MAX || end > cod->in_count)
		end = cod->in_count;

	/* clamp start to sequence length */
	if (start > cod->in_count)
		start = cod->in_count;

	/* ensure start <= end */
	if (start > end)
		start = end;

	/* calculate slice length */
	slice_count = end - start;

	/* empty slice? */
	if (slice_count == 0) {
		out = fy_seq_empty;
		goto out;
	}

	/* the sequence collection */
	col.count = slice_count;
	iov[0].iov_base = &col;
	iov[0].iov_len = sizeof(col);

	/* the content (offset to start) */
	iov[1].iov_base = (void *)(cod->in_items + start);
	iov[1].iov_len = MULSZ(slice_count, cod->col_item_size);

	out = fy_generic_collection_op_data_out(cod, iov, ARRAY_SIZE(iov));
out:
	fy_generic_collection_op_data_cleanup(cod);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_slice(const struct fy_generic_op_desc *desc,
		    struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		    fy_generic in, const struct fy_generic_op_args *args)
{
	return fy_generic_op_slice_internal(gb, flags, in, args, args->slice.start, args->slice.end);
}

static fy_generic
fy_generic_op_slice_py(const struct fy_generic_op_desc *desc,
		       struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		       fy_generic in, const struct fy_generic_op_args *args)
{
	const fy_generic_sequence *seqp;
	ssize_t start_py, end_py;
	size_t start, end, seq_len;

	/* get the sequence to determine length for negative index conversion */
	if (fy_get_type(in) != FYGT_SEQUENCE)
		return fy_invalid;

	seqp = fy_generic_sequence_resolve(in);
	if (!seqp)
		return fy_invalid;

	seq_len = seqp->count;

	/* get Python-style start and end indices */
	start_py = args ? args->slice_py.start : 0;
	end_py = args ? args->slice_py.end : -1;

	/* convert negative indices (Python-style: -1 is last element index) */
	if (start_py < 0) {
		start_py = (ssize_t)seq_len + start_py;
		if (start_py < 0)
			start_py = 0;
	}
	if (end_py < 0) {
		end_py = (ssize_t)seq_len + end_py;
		if (end_py < 0)
			end_py = 0;
	}

	start = (size_t)start_py;
	end = (size_t)end_py;

	/* delegate to the internal slice function with normalized indices */
	return fy_generic_op_slice_internal(gb, flags, in, args, start, end);
}

static fy_generic
fy_generic_op_take(const struct fy_generic_op_desc *desc,
		   struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		   fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_op_args local_args;
	size_t n;

	/* get number of elements to take */
	n = args->take.n;

	/* construct args for internal slice call */
	memset(&local_args, 0, sizeof(local_args));
	local_args.common = args->common;

	/* delegate to slice: take(n) = slice(0, n) */
	return fy_generic_op_slice_internal(gb, flags, in, &local_args, 0, n);
}

static fy_generic
fy_generic_op_drop(const struct fy_generic_op_desc *desc,
		   struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		   fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_op_args local_args;
	size_t n;

	/* get number of elements to drop */
	n = args->drop.n;

	/* construct args for internal slice call */
	memset(&local_args, 0, sizeof(local_args));
	local_args.common = args->common;

	/* delegate to slice: drop(n) = slice(n, SIZE_MAX) */
	return fy_generic_op_slice_internal(gb, flags, in, &local_args, n, SIZE_MAX);
}

static fy_generic
fy_generic_op_first(const struct fy_generic_op_desc *desc,
		    struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		    fy_generic in, const struct fy_generic_op_args *args)
{
	const fy_generic_sequence *seqp;

	/* check that input is a sequence */
	if (fy_get_type(in) != FYGT_SEQUENCE)
		return fy_invalid;

	seqp = fy_generic_sequence_resolve(in);
	if (!seqp || seqp->count == 0)
		return fy_invalid;

	/* return first element */
	return seqp->items[0];
}

static fy_generic
fy_generic_op_last(const struct fy_generic_op_desc *desc,
		   struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		   fy_generic in, const struct fy_generic_op_args *args)
{
	const fy_generic_sequence *seqp;

	/* check that input is a sequence */
	if (fy_get_type(in) != FYGT_SEQUENCE)
		return fy_invalid;

	seqp = fy_generic_sequence_resolve(in);
	if (!seqp || seqp->count == 0)
		return fy_invalid;

	/* return last element */
	return seqp->items[seqp->count - 1];
}

static fy_generic
fy_generic_op_rest(const struct fy_generic_op_desc *desc,
		   struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		   fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_generic_op_args local_args;

	/* construct args for internal slice call (rest has no args, so args may be NULL) */
	memset(&local_args, 0, sizeof(local_args));

	/* delegate to slice: rest() = slice(1, SIZE_MAX) */
	return fy_generic_op_slice_internal(gb, flags, in, &local_args, 1, SIZE_MAX);
}

static fy_generic
fy_generic_op_parse(const struct fy_generic_op_desc *desc,
		    struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		    fy_generic in, const struct fy_generic_op_args *args)
{
	fy_generic_sized_string szstr;
	struct fy_parse_cfg parse_cfg;
	struct fy_parser *fyp = NULL;
	struct fy_generic_decoder *fygd = NULL;
	enum fy_op_parse_flags parse_flags = 0;
	enum fy_generic_decoder_parse_flags decoder_parse_flags = 0;
	struct fy_diag *collect_diag = NULL;
	struct fy_diag_error *err;
	fy_generic *vp = NULL;
	fy_generic vdiag = fy_invalid;
	void *iter;
	fy_generic out;
	size_t count, i;
	struct fy_generic_indirect gi;

	parse_flags = args->parse.flags;

	if (parse_flags & FYOPPF_COLLECT_DIAG) {
		collect_diag = fy_diag_create(NULL);
		if (!collect_diag)
			goto err_out;
		fy_diag_set_collect_errors(collect_diag, true);
	}

	/* Setup parse configuration */
	memset(&parse_cfg, 0, sizeof(parse_cfg));
	parse_cfg.flags = FYPCF_DEFAULT_PARSE;
	parse_cfg.diag = collect_diag ? collect_diag : gb->cfg.diag;

	if (!(parse_flags & FYOPPF_DONT_RESOLVE))
		parse_cfg.flags |= FYPCF_RESOLVE_DOCUMENT;

	parse_cfg.flags &= ~(FYPCF_DEFAULT_VERSION(FYPCF_DEFAULT_VERSION_MASK) |
			     FYPCF_JSON(FYPCF_JSON_MASK));

	decoder_parse_flags = 0;
	switch (parse_flags & FYOPPF_MODE(FYOPPF_MODE_MASK)) {
	case FYOPPF_MODE_AUTO:
		parse_cfg.flags |= FYPCF_DEFAULT_VERSION_AUTO | FYPCF_JSON_AUTO;
		break;
	case FYOPPF_MODE_YAML_1_1_PYYAML:
		decoder_parse_flags |= FYGDPF_PYYAML_COMPAT;
		/* fall-through */
	case FYOPPF_MODE_YAML_1_1:
		parse_cfg.flags |= FYPCF_DEFAULT_VERSION_1_1 | FYPCF_JSON_NONE;
		break;
	case FYOPPF_MODE_YAML_1_2:
		parse_cfg.flags |= FYPCF_DEFAULT_VERSION_1_2 | FYPCF_JSON_NONE;
		break;
	case FYOPPF_MODE_YAML_1_3:
		parse_cfg.flags |= FYPCF_DEFAULT_VERSION_1_3 | FYPCF_JSON_NONE;
		break;
	case FYOPPF_MODE_JSON:
		parse_cfg.flags |= FYPCF_DEFAULT_VERSION_AUTO | FYPCF_JSON_FORCE;
		break;
	default:
		goto err_out;
	}

	if (parse_flags & FYOPPF_KEEP_COMMENTS)
		parse_cfg.flags |= FYPCF_PARSE_COMMENTS;

	if (parse_flags & FYOPPF_DISABLE_DIRECTORY)
		decoder_parse_flags |= FYGDPF_DISABLE_DIRECTORY;
	if (parse_flags & FYOPPF_MULTI_DOCUMENT)
		decoder_parse_flags |= FYGDPF_MULTI_DOCUMENT;
	if (parse_flags & FYOPPF_TRACE)
		decoder_parse_flags |= FYGDPF_TRACE;
	if (parse_flags & FYOPPF_KEEP_COMMENTS)
		decoder_parse_flags |= FYGDPF_KEEP_COMMENTS;
	if (parse_flags & FYOPPF_CREATE_MARKERS)
		decoder_parse_flags |= FYGDPF_CREATE_MARKERS;
	if (parse_flags & FYOPPF_KEEP_STYLE)
		decoder_parse_flags |= FYGDPF_KEEP_STYLE;
	if (parse_flags & FYOPPF_KEEP_FAILSAFE_STR)
		decoder_parse_flags |= FYGDPF_KEEP_FAILSAFE_STR;

	/* Create parser */
	fyp = fy_parser_create(&parse_cfg);
	if (!fyp)
		goto err_out;

	switch (parse_flags & FYOPPF_INPUT_TYPE(FYOPPF_INPUT_TYPE_MASK)) {
	case FYOPPF_INPUT_TYPE_STRING:
		if (fy_get_type(in) != FYGT_STRING)
			goto err_out;

		/* Use fy_generic_cast_default so inplace strings get alloca'd
		 * into this function's stack frame, giving them the right lifetime
		 * to cover the full parse operation below. */
		szstr = fy_generic_cast_default(in, fy_szstr_empty);
		if (!szstr.data)
			goto err_out;

		/* Set input string */
		if (fy_parser_set_string(fyp, szstr.data, szstr.size) != 0)
			goto err_out;
		break;

	case FYOPPF_INPUT_TYPE_FILENAME:
		if (!args->parse.input_data)
			goto err_out;

		/* set input file is at items[0] */
		if (fy_parser_set_input_file(fyp, args->parse.input_data))
			goto err_out;
		break;

	case FYOPPF_INPUT_TYPE_STDIN:
		/* - is stdin */
		if (fy_parser_set_input_file(fyp, "-"))
			goto err_out;
		break;

	case FYOPPF_INPUT_TYPE_INT_FD:
		/* set input fd */
		if (fy_parser_set_input_fd(fyp, (int)(intptr_t)args->parse.input_data))
			goto err_out;
		break;

	default:
		goto err_out;
	}

	/* Create decoder */
	fygd = fy_generic_decoder_create(fyp, gb);
	if (!fygd)
		goto err_out;

	/* Parse the input */
	out = fy_generic_decoder_parse(fygd, decoder_parse_flags);

out:
	vdiag = fy_invalid;
	if (collect_diag) {
		/* first count the errors */
		count = 0;
		iter = NULL;
		while ((err = fy_diag_errors_iterate(collect_diag, &iter)) != NULL) {
			if (err->type != FYET_ERROR)
				continue;
			count++;
		}

		if (count > 0) {
			vp = malloc(sizeof(*vp) * count);
			assert(vp);

			i = 0;
			iter = NULL;
			while ((err = fy_diag_errors_iterate(collect_diag, &iter)) != NULL) {
				if (err->type != FYET_ERROR)
					continue;

				struct fy_atom *handle = fy_token_atom(err->fyt);
				struct fy_mark start_mark, end_mark;

				if (handle) {
					szstr.data = fy_atom_lines_containing(handle, &szstr.size);
					if (!szstr.data) {
						szstr.data = "";
						szstr.size = 0;
					}
					start_mark = handle->start_mark;
					end_mark = handle->end_mark;

					/* adjust to regular human index0 */
					start_mark.line++;
					start_mark.column++;
					end_mark.line++;
					end_mark.column++;
				} else {
					szstr.data = "";
					szstr.size = 0;
					memset(&start_mark, 0, sizeof(start_mark));
					memset(&end_mark, 0, sizeof(end_mark));
				}

				vp[i] = fy_gb_mapping(gb,
						"message", err->msg,
						"file", err->file,
						"line", err->line,
						"column", err->column,
						"content", szstr,
						"start_mark",
							fy_gb_mapping(gb,
								"line", start_mark.line,
								"column", start_mark.column),
						"end_mark",
							fy_gb_mapping(gb,
								"line", end_mark.line,
								"column", end_mark.column));
				i++;
			}
			vdiag = fy_gb_sequence_create(gb, count, vp);

			free(vp);
			vp = NULL;
		}
	}

	if (fy_generic_is_valid(vdiag)) {
		if (!fy_generic_is_indirect(out)) {
			memset(&gi, 0, sizeof(gi));
			gi.flags = FYGIF_VALUE | FYGIF_DIAG;
			gi.value = out;
			gi.diag = vdiag;
		} else {
			fy_generic_indirect_get(out, &gi);
			gi.flags |= FYGIF_DIAG;
			gi.diag = vdiag;
		}
		out = fy_gb_indirect_create(gb, &gi);
	}
	if (vp)
		free(vp);
	fy_diag_destroy(collect_diag);
	fy_generic_decoder_destroy(fygd);
	fy_parser_destroy(fyp);
	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_emit(const struct fy_generic_op_desc *desc,
		   struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		   fy_generic in, const struct fy_generic_op_args *args)
{
	struct fy_emitter *emit = NULL;
	struct fy_generic_encoder *fyge = NULL;
	struct fy_emitter_xcfg emit_xcfg;
	enum fy_op_emit_flags emit_flags = 0;
	enum fy_emitter_cfg_flags emit_cfg_flags = 0;
	enum fy_emitter_xcfg_flags emit_cfg_xflags = 0;
	enum fy_generic_encoder_emit_flags encoder_emit_flags = 0;
	char *output_str = NULL;
	size_t output_len = 0;
	fy_generic out;
	int rc;

	emit_flags = args->emit.flags;

	/* try to output something pretty */
	emit_cfg_flags = FYECF_WIDTH_INF | FYECF_STRIP_DOC | FYECF_STRIP_LABELS;
	emit_cfg_xflags = FYEXCF_COLOR_AUTO;

	emit_cfg_flags &= ~(FYECF_MODE(FYECF_MODE_MASK) |
			    FYECF_INDENT(FYECF_INDENT_MASK) |
			    FYECF_WIDTH(FYECF_WIDTH_MASK));
	emit_cfg_xflags &= ~(FYEXCF_COLOR(FYEXCF_COLOR_MASK) |
			     FYEXCF_OUTPUT(FYEXCF_OUTPUT_MASK));

	if (emit_flags & FYOPEF_OUTPUT_COMMENTS)
		emit_cfg_flags |= FYECF_OUTPUT_COMMENTS;

	/* styling mode */
	switch (emit_flags & FYOPEF_MODE(FYOPEF_MODE_MASK)) {
	case FYOPEF_MODE_AUTO:
	case FYOPEF_MODE_YAML_1_1:
	case FYOPEF_MODE_YAML_1_1_PYYAML:
	case FYOPEF_MODE_YAML_1_2:
	case FYOPEF_MODE_YAML_1_3:
		/* XXX we only output YAML 1.2 */
		switch (emit_flags & FYOPEF_STYLE(FYOPEF_STYLE_MASK)) {
		case FYOPEF_STYLE_DEFAULT:
			emit_cfg_flags |= FYECF_MODE_ORIGINAL;
			break;
		case FYOPEF_STYLE_PRETTY:
			emit_cfg_flags |= FYECF_MODE_PRETTY;
			break;
		case FYOPEF_STYLE_ONELINE:
			emit_cfg_flags |= FYECF_MODE_FLOW_ONELINE;
			break;
		case FYOPEF_STYLE_COMPACT:
			emit_cfg_flags |= FYECF_MODE_FLOW_COMPACT;
			break;
		case FYOPEF_STYLE_BLOCK:
			emit_cfg_flags |= FYECF_MODE_BLOCK;
			break;
		case FYOPEF_STYLE_FLOW:
			emit_cfg_flags |= FYECF_MODE_FLOW;
			break;
		default:
			emit_cfg_flags |= FYECF_MODE_PRETTY;
			break;
		}
		break;

	case FYOPEF_MODE_JSON:
		switch (emit_flags & FYOPEF_STYLE(FYOPEF_STYLE_MASK)) {
		case FYOPEF_STYLE_ONELINE:
			emit_cfg_flags |= FYECF_MODE_JSON_ONELINE;
			break;
		case FYOPEF_STYLE_COMPACT:
			emit_cfg_flags |= FYECF_MODE_JSON_COMPACT;
			break;
		default:
			emit_cfg_flags |= FYECF_MODE_JSON;
			break;
		}
		break;
	default:
		goto err_out;
	}

	switch (emit_flags & FYOPEF_INDENT(FYOPEF_INDENT_MASK)) {
	case FYOPEF_INDENT_DEFAULT:
		emit_cfg_flags |= FYECF_INDENT_DEFAULT;
		break;
	case FYOPEF_INDENT_1:
		emit_cfg_flags |= FYECF_INDENT_1;
		break;
	case FYOPEF_INDENT_2:
		emit_cfg_flags |= FYECF_INDENT_2;
		break;
	case FYOPEF_INDENT_3:
		emit_cfg_flags |= FYECF_INDENT_3;
		break;
	case FYOPEF_INDENT_4:
		emit_cfg_flags |= FYECF_INDENT_4;
		break;
	case FYOPEF_INDENT_6:
		emit_cfg_flags |= FYECF_INDENT_6;
		break;
	case FYOPEF_INDENT_8:
		emit_cfg_flags |= FYECF_INDENT_8;
		break;
	default:
		emit_cfg_flags |= FYECF_INDENT_DEFAULT;
		break;
	}

	switch (emit_flags & FYOPEF_WIDTH(FYOPEF_WIDTH_MASK)) {
	case FYOPEF_WIDTH_DEFAULT:
		emit_cfg_flags |= FYECF_WIDTH_INF;	// default is infinite
		break;
	case FYOPEF_WIDTH_80:
		emit_cfg_flags |= FYECF_WIDTH_80;
		break;
	case FYOPEF_WIDTH_132:
		emit_cfg_flags |= FYECF_WIDTH_132;
		break;
	case FYOPEF_WIDTH_INF:
		emit_cfg_flags |= FYECF_WIDTH_INF;
		break;
	default:
		emit_cfg_flags |= FYECF_WIDTH_DEFAULT;
		break;
	}

	switch (emit_flags & FYOPEF_COLOR(FYOPEF_COLOR_MASK)) {
	case FYOPEF_COLOR_AUTO:
		emit_cfg_xflags |= FYEXCF_COLOR_AUTO;
		break;
	case FYOPEF_COLOR_NONE:
		emit_cfg_xflags |= FYEXCF_COLOR_NONE;
		break;
	case FYOPEF_COLOR_FORCE:
		emit_cfg_xflags |= FYEXCF_COLOR_FORCE;
		break;
	default:
		emit_cfg_xflags |= FYEXCF_COLOR_AUTO;
		break;
	}

	/* XXX FYOPEF_WIDTH_ADAPT_TO_TERMINAL ignoring adapt to terminal for now */

	if (emit_flags & FYOPEF_NO_ENDING_NEWLINE)
		emit_cfg_flags |= FYECF_NO_ENDING_NEWLINE;

	switch (emit_flags & FYOPEF_OUTPUT_TYPE(FYOPEF_OUTPUT_TYPE_MASK)) {
	case FYOPEF_OUTPUT_TYPE_STRING:
		/* Create string emitter */
		emit = fy_emit_to_string(emit_cfg_flags);
		if (!emit)
			goto err_out;
		break;
	case FYOPEF_OUTPUT_TYPE_STDOUT:
	case FYOPEF_OUTPUT_TYPE_STDERR:
	case FYOPEF_OUTPUT_TYPE_FILENAME:

		memset(&emit_xcfg, 0, sizeof(emit_xcfg));
		emit_xcfg.cfg.flags = emit_cfg_flags | FYECF_EXTENDED_CFG;
		emit_xcfg.xflags = emit_cfg_xflags;
		emit_xcfg.cfg.diag = gb->cfg.diag;

		switch (emit_flags & FYOPEF_OUTPUT_TYPE(FYOPEF_OUTPUT_TYPE_MASK)) {
		case FYOPEF_OUTPUT_TYPE_STDOUT:
			emit_xcfg.xflags |= FYEXCF_OUTPUT_STDOUT;
			break;
		case FYOPEF_OUTPUT_TYPE_STDERR:
			emit_xcfg.xflags |= FYEXCF_OUTPUT_STDERR;
			break;
		case FYOPEF_OUTPUT_TYPE_FILENAME:
			if (!args->emit.output_data)
				goto err_out;
			emit_xcfg.xflags |= FYEXCF_OUTPUT_FILENAME;
			emit_xcfg.output_filename = args->emit.output_data;
			break;
		default:
			goto err_out;
		}

		emit = fy_emitter_create(&emit_xcfg.cfg);
		if (!emit)
			goto err_out;

		break;
	default:
		goto err_out;
	}

	/* Create encoder */
	fyge = fy_generic_encoder_create(emit);
	if (!fyge)
		goto err_out;

	encoder_emit_flags = 0;
	if (emit_flags & FYOPEF_DISABLE_DIRECTORY)
		encoder_emit_flags |= FYGEEF_DISABLE_DIRECTORY;
	if (emit_flags & FYOPEF_MULTI_DOCUMENT)
		encoder_emit_flags |= FYGEEF_MULTI_DOCUMENT;
	if (emit_flags & FYOPEF_TRACE)
		encoder_emit_flags |= FYGEEF_TRACE;

	/* Emit the value */
	rc = fy_generic_encoder_emit(fyge, encoder_emit_flags | FYGEEF_TRACE, in);
	if (rc)
		goto err_out;

	/* Sync the encoder */
	rc = fy_generic_encoder_sync(fyge);
	if (rc)
		goto err_out;

	switch (emit_flags & FYOPEF_OUTPUT_TYPE(FYOPEF_OUTPUT_TYPE_MASK)) {
	case FYOPEF_OUTPUT_TYPE_STRING:

		/* Collect the output */
		output_str = fy_emit_to_string_collect(emit, &output_len);
		if (!output_str)
			goto err_out;

		/* Create string generic from output */
		out = fy_gb_string_size_create(gb, output_str, output_len);
		break;

	case FYOPEF_OUTPUT_TYPE_STDOUT:
	case FYOPEF_OUTPUT_TYPE_STDERR:
	case FYOPEF_OUTPUT_TYPE_FILENAME:
		out = fy_int(0);
		break;

	default:
		goto err_out;
	}

out:
	/* Free the output string */
	if (output_str)
		free(output_str);

	/* Cleanup encoder and emitter */
	fy_generic_encoder_destroy(fyge);
	fy_emitter_destroy(emit);

	return out;

err_out:
	out = fy_invalid;
	goto out;
}

static fy_generic
fy_generic_op_convert(const struct fy_generic_op_desc *desc,
		      struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		      fy_generic in, const struct fy_generic_op_args *args)
{
	fy_generic out = fy_invalid;
	enum fy_generic_type in_type;

	if (fy_generic_is_invalid(in))
		goto err_out;

	/* same type? we're done */
	in_type = fy_generic_get_type(in);
	if (in_type == args->convert.type) {
		/* but we have to strip out the indirect value */
		out = !fy_generic_is_indirect(in) ? in : fy_generic_indirect_get_value(in);
		goto out;
	}

	switch (args->convert.type) {
	case FYGT_BOOL:
		out = fy_gb_to_bool(gb, in);
		break;
	case FYGT_INT:
		out = fy_gb_to_int(gb, in);
		break;
	case FYGT_FLOAT:
		out = fy_gb_to_float(gb, in);
		break;
	case FYGT_STRING:
		out = fy_gb_to_string(gb, in);
		break;
	default:
		goto err_out;
	}
out:
	if (fy_generic_is_valid(out))
		out = fy_generic_op_internalize(gb, flags, out);
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
		.in_mask = FYGTM_COLLECTION,
		.out_mask = FYGTM_COLLECTION,
		.handler = fy_generic_op_set,
	},
	[FYGBOP_SET_AT] = {
		.op = FYGBOP_SET_AT,
		.op_name = "set_at",
		.in_mask = FYGTM_COLLECTION,
		.out_mask = FYGTM_COLLECTION,
		.handler = fy_generic_op_set_at,
	},
	[FYGBOP_SET_AT_PATH] = {
		.op = FYGBOP_SET_AT_PATH,
		.op_name = "set_at_path",
		.in_mask = FYGTM_COLLECTION,
		.out_mask = FYGTM_COLLECTION,
		.handler = fy_generic_op_set_at_path,
	},
	[FYGBOP_GET] = {
		.op = FYGBOP_GET,
		.op_name = "get",
		.in_mask = FYGTM_COLLECTION,
		.out_mask = FYGTM_COLLECTION,
		.handler = fy_generic_op_get,
	},
	[FYGBOP_GET_AT] = {
		.op = FYGBOP_GET_AT,
		.op_name = "get_at",
		.in_mask = FYGTM_COLLECTION,
		.out_mask = FYGTM_COLLECTION,
		.handler = fy_generic_op_get_at,
	},
	[FYGBOP_GET_AT_PATH] = {
		.op = FYGBOP_GET_AT_PATH,
		.op_name = "get_at_path",
		.in_mask = FYGTM_COLLECTION,
		.out_mask = FYGTM_COLLECTION,
		.handler = fy_generic_op_get_at_path,
	},
	[FYGBOP_FILTER] = {
		.op = FYGBOP_FILTER,
		.op_name = "filter",
		.in_mask = FYGTM_COLLECTION,
		.out_mask = FYGTM_COLLECTION,
		.handler = fy_generic_op_filter,
	},
	[FYGBOP_MAP] = {
		.op = FYGBOP_MAP,
		.op_name = "map",
		.in_mask = FYGTM_COLLECTION,
		.out_mask = FYGTM_COLLECTION,
		.handler = fy_generic_op_map,
	},
	[FYGBOP_REDUCE] = {
		.op = FYGBOP_REDUCE,
		.op_name = "reduce",
		.in_mask = FYGTM_COLLECTION,
		.out_mask = FYGTM_ANY,
		.handler = fy_generic_op_reduce,
	},
	[FYGBOP_SLICE] = {
		.op = FYGBOP_SLICE,
		.op_name = "slice",
		.in_mask = FYGTM_SEQUENCE,
		.out_mask = FYGTM_SEQUENCE,
		.handler = fy_generic_op_slice,
	},
	[FYGBOP_SLICE_PY] = {
		.op = FYGBOP_SLICE_PY,
		.op_name = "slice_py",
		.in_mask = FYGTM_SEQUENCE,
		.out_mask = FYGTM_SEQUENCE,
		.handler = fy_generic_op_slice_py,
	},
	[FYGBOP_TAKE] = {
		.op = FYGBOP_TAKE,
		.op_name = "take",
		.in_mask = FYGTM_SEQUENCE,
		.out_mask = FYGTM_SEQUENCE,
		.handler = fy_generic_op_take,
	},
	[FYGBOP_DROP] = {
		.op = FYGBOP_DROP,
		.op_name = "drop",
		.in_mask = FYGTM_SEQUENCE,
		.out_mask = FYGTM_SEQUENCE,
		.handler = fy_generic_op_drop,
	},
	[FYGBOP_FIRST] = {
		.op = FYGBOP_FIRST,
		.op_name = "first",
		.in_mask = FYGTM_SEQUENCE,
		.out_mask = FYGTM_ANY,
		.handler = fy_generic_op_first,
	},
	[FYGBOP_LAST] = {
		.op = FYGBOP_LAST,
		.op_name = "last",
		.in_mask = FYGTM_SEQUENCE,
		.out_mask = FYGTM_ANY,
		.handler = fy_generic_op_last,
	},
	[FYGBOP_REST] = {
		.op = FYGBOP_REST,
		.op_name = "rest",
		.in_mask = FYGTM_SEQUENCE,
		.out_mask = FYGTM_SEQUENCE,
		.handler = fy_generic_op_rest,
	},
	[FYGBOP_PARSE] = {
		.op = FYGBOP_PARSE,
		.op_name = "parse",
		.in_mask = FYGTM_STRING,
		.out_mask = FYGTM_ANY,
		.handler = fy_generic_op_parse,
	},
	[FYGBOP_EMIT] = {
		.op = FYGBOP_EMIT,
		.op_name = "emit",
		.in_mask = FYGTM_ANY,
		.out_mask = FYGTM_STRING,
		.handler = fy_generic_op_emit,
	},
	[FYGBOP_CONVERT] = {
		.op = FYGBOP_EMIT,
		.op_name = "convert",
		.in_mask = FYGTM_ANY,
		.out_mask = FYGTM_ANY,
		.handler = fy_generic_op_convert,
	},
};

fy_generic
fy_generic_op_args(struct fy_generic_builder *gb, enum fy_gb_op_flags flags,
		   fy_generic in, const struct fy_generic_op_args *args)
{
	const unsigned int op = ((flags >> FYGBOPF_OP_SHIFT) & FYGBOPF_OP_MASK);

	if (op >= ARRAY_SIZE(op_descs) || !op_descs[op].handler)
		return fy_invalid;

	return op_descs[op].handler(&op_descs[op], gb, flags, in, args);
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
		break;

	case FYGBOP_INSERT:
	case FYGBOP_REPLACE:
	case FYGBOP_GET_AT:
	case FYGBOP_SET_AT:
		args->insert_replace_get_set_at.idx = va_arg(ap, size_t);
		break;

	case FYGBOP_FILTER:
	case FYGBOP_MAP:
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

	case FYGBOP_SLICE:
		args->slice.start = va_arg(ap, size_t);
		args->slice.end = va_arg(ap, size_t);
		break;

	case FYGBOP_SLICE_PY:
		args->slice_py.start = va_arg(ap, ssize_t);
		args->slice_py.end = va_arg(ap, ssize_t);
		break;

	case FYGBOP_TAKE:
		args->take.n = va_arg(ap, size_t);
		break;

	case FYGBOP_DROP:
		args->drop.n = va_arg(ap, size_t);
		break;

	case FYGBOP_FIRST:
	case FYGBOP_LAST:
	case FYGBOP_REST:
		/* No additional arguments needed */
		break;

	case FYGBOP_PARSE:
		args->parse.flags = va_arg(ap, enum fy_op_parse_flags);
		args->parse.input_data = va_arg(ap, void *);
		break;

	case FYGBOP_EMIT:
		args->emit.flags = va_arg(ap, enum fy_op_emit_flags);
		args->emit.output_data = va_arg(ap, void *);
		break;

	case FYGBOP_CONVERT:
		args->convert.type = va_arg(ap, enum fy_generic_type);
		break;

	default:
		return fy_invalid;
	}
	va_end(ap);

	return fy_generic_op_args(gb, flags, in, args);
}
