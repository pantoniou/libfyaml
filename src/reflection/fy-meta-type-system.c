/*
 * fy-meta-type-system.c - Reflection type data management (allocation, construction,
 *                         destruction, collection, and memory helpers)
 *
 * Copyright (c) 2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_REFLECTION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#if HAVE_ALLOCA
#include <alloca.h>
#endif

#include <libfyaml.h>

#include "fy-reflection-private.h"
#include "fy-reflection-util.h"

#include "fy-reflect-meta.h"

#include "fy-type-meta.h"

void *reflection_malloc(struct reflection_type_system *rts, size_t size)
{
	void *p;

	if (!rts)
		return NULL;

	if (!rts->cfg.ops || !rts->cfg.ops->malloc)
		p = malloc(size);
	else
		p = rts->cfg.ops->malloc(rts, size);

	return p;
}

void *reflection_realloc(struct reflection_type_system *rts, void *ptr, size_t size)
{
	void *p;

	if (!rts)
		return NULL;

	if (!rts->cfg.ops || !rts->cfg.ops->realloc)
		p = realloc(ptr, size);
	else
		p = rts->cfg.ops->realloc(rts, ptr, size);

	return p;
}

void reflection_free(struct reflection_type_system *rts, void *ptr)
{
	if (!rts)
		return;

	if (!rts->cfg.ops || !rts->cfg.ops->free)
		free(ptr);
	else
		rts->cfg.ops->free(rts, ptr);
}

void *
reflection_type_data_alloc(struct reflection_type_data *rtd)
{
	void *ptr;

	if (!rtd)
		return NULL;

	ptr = reflection_malloc(rtd->rts, rtd->ti->size);
	if (ptr)
		memset(ptr, 0, rtd->ti->size);
	return ptr;
}

void *
reflection_type_data_alloc_array(struct reflection_type_data *rtd, size_t count)
{
	size_t item_size, size;
	void *ptr;

	if (!rtd)
		return NULL;

	item_size = rtd->ti->size;

	if (FY_MUL_OVERFLOW(item_size, count, &size))
		return NULL;

	ptr = reflection_malloc(rtd->rts, size);
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

bool reflection_type_data_equal(const struct reflection_type_data *rtd1,
				const struct reflection_type_data *rtd2)
{
	struct reflection_field_data *rfd1, *rfd2;
	struct reflection_type_data *rtd_dep1, *rtd_dep2;
	bool recursive1, recursive2, eq;
	int i;

	if (rtd1 == rtd2)
		return true;

	if (!rtd1 || !rtd2)
		return false;

	eq = rtd1->rts == rtd2->rts &&
	     rtd1->ti == rtd2->ti &&
	     rtd1->ops == rtd2->ops &&
	     rtd1->flags == rtd2->flags &&
	     rtd1->flat_field_idx == rtd2->flat_field_idx &&
	     rtd1->has_anonymous_field == rtd2->has_anonymous_field &&
	     rtd1->first_anonymous_field_idx == rtd2->first_anonymous_field_idx &&
	     rtd1->rtd_dep_recursive == rtd2->rtd_dep_recursive &&
	     reflection_type_data_equal(rtd1->rtd_selector, rtd2->rtd_selector) &&
	     rtd1->selector_field_idx == rtd2->selector_field_idx &&
	     rtd1->union_field_idx == rtd2->union_field_idx &&
	     rtd1->fields_count == rtd2->fields_count &&
	     reflection_meta_compare(rtd1->meta, rtd2->meta);

	if (!eq)
		return false;

	/* recursive check */
	for (i = 0; i < rtd1->fields_count + (rtd1->rtd_dep ? 1 : 0); i++) {
		if (i < rtd1->fields_count) {
			rfd1 = rtd1->fields[i];
			rtd_dep1 = rfd1->rtd;
			recursive1 = rfd1->rtd_recursive;
			rfd2 = rtd2->fields[i];
			rtd_dep2 = rfd2->rtd;
			recursive2 = rfd2->rtd_recursive;
		} else {
			rtd_dep1 = rtd1->rtd_dep;
			recursive1 = rtd1->rtd_dep_recursive;
			rtd_dep2 = rtd2->rtd_dep;
			recursive2 = rtd2->rtd_dep_recursive;
		}

		/* recursive ones, are equal only if the dep pointers match */
		if ((recursive1 || recursive2) && rtd_dep1 != rtd_dep2)
			return false;

		eq = reflection_type_data_equal(rtd_dep1, rtd_dep2);
		if (!eq)
			return false;
	}

	return true;
}

static int
reflection_type_data_count_recursive(struct reflection_type_data *rtd)
{
	struct reflection_field_data *rfd;
	int i, count;

	if (!rtd)
		return 0;

	count = 1;	/* self */
	if (rtd->rtd_dep && !rtd->rtd_dep_recursive)
		count += reflection_type_data_count_recursive(rtd->rtd_dep);

	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];
		count += reflection_type_data_count_recursive(rfd_rtd(rfd));
	}
	return count;
}

static struct reflection_type_data **
reflection_type_data_collect_recursive(struct reflection_type_data *rtd,
				       struct reflection_type_data **rtds,
				       struct reflection_type_data **rtds_start,
				       struct reflection_type_data **rtds_end)
{
	struct reflection_type_data **rtdst;
	struct reflection_field_data *rfd;
	int i;

	if (!rtd || !rtds || !rtds_end || rtds >= rtds_end)
		return NULL;

	/* do not collect twice */
	for (rtdst = rtds_start; rtdst < rtds_end; rtdst++) {
		if (*rtdst == rtd)
			return rtds;
	}

	*rtds++ = rtd;	/* self */

	if (rtd->rtd_dep && !rtd->rtd_dep_recursive) {
		rtds = reflection_type_data_collect_recursive(rtd->rtd_dep, rtds, rtds_start, rtds_end);
		if (!rtds)
			return NULL;
	}

	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];
		rtds = reflection_type_data_collect_recursive(rfd_rtd(rfd), rtds, rtds_start, rtds_end);
		if (!rtds)
			return NULL;
	}

	return rtds;
}

static void
reflection_type_data_replace_by(struct reflection_type_data *rtd,
				struct reflection_type_data *rtd_replacement,
				struct reflection_type_data *rtd_to_replace)
{
	struct reflection_field_data *rfd;
	int i;

	if (!rtd || !rtd_replacement || !rtd_to_replace)
		return;

	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];
		if (rfd->rtd) {
			if (rfd->rtd == rtd_to_replace) {
				rfd->rtd = reflection_type_data_ref(rtd_replacement);
				reflection_type_data_unref(rtd_to_replace);
			}
			reflection_type_data_replace_by(rfd->rtd, rtd_replacement, rtd_to_replace);
		}
	}

	if (rtd->rtd_dep && !rtd->rtd_dep_recursive) {
		if (rtd->rtd_dep == rtd_to_replace) {
			rtd->rtd_dep = reflection_type_data_ref(rtd_replacement);
			reflection_type_data_unref(rtd_to_replace);
		}
		reflection_type_data_replace_by(rtd->rtd_dep, rtd_replacement, rtd_to_replace);
	}

	if (rtd->rtd_selector) {
		if (rtd->rtd_selector == rtd_to_replace) {
			rtd->rtd_selector = reflection_type_data_ref(rtd_replacement);
			reflection_type_data_unref(rtd_to_replace);
		}
		/* no recursion for rtd_selector */
	}
}

enum reflection_type_data_sort {
	RTDS_DEPTH_ORDER,
	RTDS_ID,
	RTDS_TYPE_INFO,
	RTDS_TYPE_INFO_ID,
};

static int rtd_collect_compare_id(const void *va, const void *vb)
{
	const struct reflection_type_data * const *rtda = va, * const *rtdb = vb;

	return (*rtda)->idx < (*rtdb)->idx ? -1 :
	       (*rtda)->idx > (*rtdb)->idx ?  1 :
	       0;
}

static int rtd_collect_compare_type_info(const void *va, const void *vb)
{
	const struct reflection_type_data * const *rtda = va, * const *rtdb = vb;

	return (*rtda)->ti < (*rtdb)->ti ? -1 :
	       (*rtda)->ti > (*rtdb)->ti ?  1 :
	       0;
}

/* sort by type info, and then by id */
static int rtd_collect_compare_type_info_id(const void *va, const void *vb)
{
	int rc;

	rc = rtd_collect_compare_type_info(va, vb);
	if (rc)
		return rc;

	return rtd_collect_compare_id(va, vb);
}

/* terminated with NULL */
struct reflection_type_data **
reflection_type_data_collect(struct reflection_type_data *rtd, enum reflection_type_data_sort sort)
{
	struct reflection_type_data **rtds, **rtds_start, **rtds_end;
	int (*cmpf)(const void *va, const void *vb);
	size_t sz;
	int count;

	/* concervative count */
	count = reflection_type_data_count_recursive(rtd);
	if (count <= 0)
		return NULL;

	sz = sizeof(*rtds_start) * (count + 1);
	rtds_start = malloc(sizeof(*rtds_start) * (count + 1));
	if (!rtds_start)
		return NULL;
	memset(rtds_start, 0, sz);

	rtds_end = rtds_start + count;
	rtds = reflection_type_data_collect_recursive(rtd, rtds_start, rtds_start, rtds_end);
	if (!rtds || rtds > rtds_end) {
		free(rtds_start);
		return NULL;
	}
	count = rtds - rtds_start;
	*rtds++ = NULL;	/* terminator */

	switch (sort) {
	case RTDS_ID:
		cmpf = rtd_collect_compare_id;
		break;
	case RTDS_TYPE_INFO:
		cmpf = rtd_collect_compare_type_info;
		break;
	case RTDS_TYPE_INFO_ID:
		cmpf = rtd_collect_compare_type_info_id;
		break;
	case RTDS_DEPTH_ORDER:
	default:
		cmpf = NULL;
		break;
	}

	if (cmpf)
		qsort(rtds_start, count, sizeof(*rtds_start), cmpf);

	return rtds_start;
}

void reflection_type_data_dump(struct reflection_type_data *rtd_root)
{
	struct reflection_type_system *rts;
	struct reflection_type_data **rtds = NULL, **rtdsp;
	const struct reflection_field_data *rfd, *rfd_ref_root;
	const struct reflection_type_data *rtd, *rtdf, *rtd_ref_root;
	int j;

	if (!rtd_root)
		return;

	rts = rtd_root->rts;

	rtd_ref_root = rts->root_ref ? rts->root_ref->rtd_root : NULL;

	printf("%s: root #%d:'%s'", __func__, rtd_root->idx, rtd_root->ti->name);
	if (rtd_ref_root == rtd_root) {
		rfd_ref_root = rts->root_ref->rfd_root;
#if 0
		if (rfd_ref_root && rfd_ref_root->yaml_annotation)
			printf(" A=%s", fy_emit_document_to_string_alloca(
					rfd_ref_root->yaml_annotation,
					FYECF_WIDTH_INF | FYECF_INDENT_DEFAULT |
					FYECF_MODE_FLOW_ONELINE | FYECF_NO_ENDING_NEWLINE));
#endif
		if (rfd_ref_root && reflection_meta_has_explicit(rfd_ref_root->meta))
			printf(" M=%s", reflection_meta_get_document_str_alloca(rfd_ref_root->meta));
	}
	printf("\n");

	rtds = reflection_type_data_collect(rtd_root, RTDS_ID);
	if (!rtds) {
		printf("FAILED to collect type data for print!\n");
		return;
	}

	rtdsp = rtds;
	while ((rtd = *rtdsp++) != NULL) {

		printf("#%d:'%s' r%d (%s)", rtd->idx, rtd->ti->name, rtd->refs, rtd->ops->name);

		printf("%s%s%s%s%s",
				(rtd->flags & RTDF_PURITY_MASK) == 0 ? " PURE" : "",
				(rtd->flags & RTDF_IMPURE) ? " IMPURE" : "",
				(rtd->flags & RTDF_MUTATED) ? " MUTATED" : "",
				(rtd->flags & RTDF_SPECIALIZED) ? " SPECIALIZED" : "",
				(rtd->flags & RTDF_SPECIALIZING) ? " SPECIALIZING" : "");
		printf("%s%s",
				(rtd->ti->flags & FYTIF_ANONYMOUS) ? " ANONYMOUS" : "",
				(rtd->ti->flags & FYTIF_ANONYMOUS_RECORD_DECL) ? " ANONYMOUS_RECORD_DECL" : "");
		if (rtd->rtd_dep)
			printf(" dep: #%d:'%s'%s", rtd->rtd_dep->idx, rtd->rtd_dep->ti->name,
					rtd->rtd_dep_recursive ? " (recursive)" : "");

		if (reflection_meta_has_explicit(rtd->meta))
			printf(" M=%s", reflection_meta_get_document_str_alloca(rtd->meta));

		printf("\n");
		for (j = 0; j < rtd->fields_count; j++) {
			rfd = rtd->fields[j];
			rtdf = rfd_rtd(rfd);
			printf("\t#%d:'%s' %s", rtdf->idx, rtdf->ti->name, rfd->fi->name);
			if (strcmp(rfd->fi->name, rfd->field_name))
				printf(" (%s)", rfd->field_name);
			if (rfd->rtd_recursive)
				printf(" (recursive)");
			printf("%s%s%s",
					(rfd->fi->type_info->flags & FYTIF_CONST) ? " CONST" : "",
					(rfd->fi->type_info->flags & FYTIF_VOLATILE) ? " VOLATILE" : "",
					(rfd->fi->type_info->flags & FYTIF_RESTRICT) ? " RESTRICT" : "");

			printf("\n");
		}
	}

	free(rtds);
}

void reflection_field_data_destroy(struct reflection_field_data *rfd)
{
	struct reflection_type_data *rtd;

	if (!rfd)
		return;

	rtd = rfd->rtd;

	if (rfd->meta)
		reflection_meta_destroy(rfd->meta);

	if (rtd)
		reflection_type_data_unref(rtd);

	if (rfd->field_name)
		free(rfd->field_name);

	free(rfd);
}

struct reflection_field_data *
reflection_field_data_create_internal(struct reflection_type_system *rts,
				      struct reflection_type_data *rtd_parent, int idx)
{
	struct reflection_field_data *rfd;
	const struct fy_type_info *ti_parent;
	bool has_fields;
	const char *remove_prefix, *field_name;
	size_t pfx_len, name_len;
	int rc;

	rfd = malloc(sizeof(*rfd));
	RTS_ASSERT(rfd);

	memset(rfd, 0, sizeof(*rfd));

	ti_parent = rtd_parent ? rtd_parent->ti : NULL;
	has_fields = ti_parent && fy_type_kind_has_fields(ti_parent->kind);
	remove_prefix = has_fields ? reflection_meta_get_str(rtd_parent->meta, rmvid_remove_prefix) : NULL;

	rfd->rts = rts;
	rfd->idx = idx;
	rfd->rtd_parent = rtd_parent;
	rfd->fi = has_fields ? &ti_parent->fields[rfd->idx] : NULL;

	rfd->meta = reflection_meta_create(rts);
	RTS_ASSERT(rfd->meta);

	if (rfd->fi) {
		rfd->yaml_annotation = fy_field_info_get_yaml_annotation(rfd->fi);
		rfd->yaml_annotation_str = fy_field_info_get_yaml_comment(rfd->fi);

		rc = reflection_meta_fill(rfd->meta, fy_document_root(rfd->yaml_annotation));
		RTS_ASSERT(!rc);
	}

	if (has_fields) {

		/* assign field name early */
		field_name = reflection_meta_get_str(rfd->meta, rmvid_name);

		if (field_name) {
			rfd->field_name = strdup(field_name);
			RTS_ASSERT(rfd->field_name);
		}

		if (remove_prefix) {
			pfx_len = strlen(remove_prefix);
			name_len = strlen(rfd->fi->name);
			if (name_len > pfx_len && !memcmp(remove_prefix, rfd->fi->name, pfx_len)) {
				name_len -= pfx_len;
				rfd->field_name = malloc(name_len + 1);
				RTS_ASSERT(rfd->field_name);

				/* remove prefix */
				memcpy(rfd->field_name, rfd->fi->name + pfx_len, name_len + 1);
			}
		}

		if (!rfd->field_name) {
			rfd->field_name = strdup(rfd->fi->name);
			RTS_ASSERT(rfd->field_name);
		}
	}

	return rfd;

err_out:
	reflection_field_data_destroy(rfd);
	return NULL;
}

struct reflection_field_data *
reflection_field_data_create(struct reflection_type_data *rtd_parent, int idx)
{
	return reflection_field_data_create_internal(rtd_parent->rts, rtd_parent, idx);
}

void reflection_type_data_destroy(struct reflection_type_data *rtd)
{
	struct reflection_field_data *rfd;
	int i;

	if (!rtd)
		return;

	reflection_meta_destroy(rtd->meta);

	if (rtd->rtd_dep && !rtd->rtd_dep_recursive)
		reflection_type_data_unref(rtd->rtd_dep);

	if (rtd->fields) {
		for (i = rtd->fields_count - 1; i >= 0; i--) {
			rfd = rtd->fields[i];
			reflection_field_data_destroy(rfd);
		}
		free(rtd->fields);
	}

	reflection_type_data_unref(rtd->rtd_selector);

	free(rtd);
}

int
reflection_type_data_simplify(struct reflection_type_data *rtd_root)
{
	struct reflection_type_data **rtds = NULL, **rtdsp1, **rtdsp2, *rtd1, *rtd2;
	bool eq;

	if (!rtd_root)
		return 0;

	/* collect, ordered by type_info and then sorted by id */
again:
	if (rtds)
		free(rtds);
	rtds = reflection_type_data_collect(rtd_root, RTDS_DEPTH_ORDER);
	if (!rtds)
		return -1;

	rtdsp1 = rtds;
	while ((rtd1 = *rtdsp1++) != NULL) {

		rtdsp2 = rtdsp1;
		while ((rtd2 = *rtdsp2++) != NULL) {

			/* for equal types, replace with aliases */
			eq = reflection_type_data_equal(rtd1, rtd2);
			if (eq) {
				reflection_type_data_replace_by(rtd_root, rtd1, rtd2);
				goto again;
			}
		}
	}

	free(rtds);

	return 0;
}

#endif /* HAVE_REFLECTION */
