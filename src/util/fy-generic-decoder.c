/*
 * fy-generic-decoder.h - generic decoder (yaml -> generic)
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

#include <stdio.h>

#include "fy-docstate.h"
#include "fy-diag.h"
#include "fy-parse.h"

#include "fy-generic.h"
#include "fy-generic-decoder.h"

static struct fy_generic_decoder_obj *fy_generic_decoder_object_create(struct fy_generic_decoder *gd,
		enum fy_generic_decoder_object_type type, fy_generic anchor, fy_generic tag);
static void fy_generic_decoder_object_destroy(struct fy_generic_decoder_obj *gdo);
static fy_generic fy_generic_decoder_object_finalize(struct fy_generic_decoder *gd, struct fy_generic_decoder_obj *gdo);
static fy_generic fy_generic_decoder_object_finalize_and_destroy(struct fy_generic_decoder *gd,
		struct fy_generic_decoder_obj *gdo);

static int fy_generic_decoder_object_add_item(struct fy_generic_decoder_obj *gdo, fy_generic item);
static fy_generic fy_generic_decoder_create_scalar(struct fy_generic_decoder *gd, struct fy_event *fye,
		fy_generic va, fy_generic vt);

static int fy_generic_decoder_anchor_register(struct fy_generic_decoder *fygd, fy_generic anchor, fy_generic content);
static fy_generic fy_generic_decoder_alias_resolve(struct fy_generic_decoder *fygd, fy_generic anchor);
static bool fy_generic_decoder_alias_is_collecting(struct fy_generic_decoder *fygd, fy_generic anchor);
static void fy_generic_decoder_anchor_collection_starts(struct fy_generic_decoder *fygd);
static void fy_generic_decoder_anchor_collection_ends(struct fy_generic_decoder *fygd, fy_generic v);

struct fy_generic_decoder_obj *
fy_generic_decoder_object_create(struct fy_generic_decoder *gd, enum fy_generic_decoder_object_type type,
		fy_generic anchor, fy_generic tag)
{
	struct fy_generic_decoder_obj *gdo;

	if (!gd || !fy_generic_decoder_object_type_is_valid(type))
		return NULL;

	gdo = malloc(sizeof(*gdo));
	if (!gdo)
		return NULL;

	memset(gdo, 0, sizeof(*gdo));
	gdo->type = type;
	gdo->anchor = anchor;
	gdo->tag = tag;

	gdo->v = fy_invalid;
	gdo->vds = fy_invalid;

	return gdo;
}

static void
fy_generic_decoder_object_destroy(struct fy_generic_decoder_obj *gdo)
{
	if (!gdo)
		return;

	if (gdo->items)
		free(gdo->items);
	if (gdo->fyds)
		fy_document_state_unref(gdo->fyds);
	free(gdo);
}

static fy_generic
fy_generic_decoder_object_finalize(struct fy_generic_decoder *gd, struct fy_generic_decoder_obj *gdo)
{
	fy_generic v, vi;
	bool needs_indirect;
	const struct fy_version *vers;
	struct fy_tag **tags;
	size_t i, count;
	fy_generic *vtags_items;
	fy_generic vtags;
	bool version_explicit;
	bool tags_explicit;

	v = fy_invalid;

	assert(gdo);

	switch (gdo->type) {

	case FYGDOT_ROOT:
		if (gdo->count > 1)
			return fy_invalid;

		if (gdo->count == 0)
			v = fy_null;
		else
			v = gdo->items[0];

		break;

	case FYGDOT_SEQUENCE:
		v = fy_generic_sequence_create(gd->gb, gdo->count, gdo->items);
		break;

	case FYGDOT_MAPPING:
		assert((gdo->count % 2) == 0);
		v = fy_generic_mapping_create(gd->gb, gdo->count / 2, gdo->items);
		break;

	default:
		assert(0);
		abort();
		break;
	}

	needs_indirect = !gd->resolve &&
		         ((gdo->anchor != fy_null && gdo->anchor != fy_invalid) ||
			 (gdo->tag != fy_null && gdo->tag != fy_invalid));

	if (needs_indirect) {
		struct fy_generic_indirect gi = {
			.value = v,
			.anchor = gdo->anchor,
			.tag = gdo->tag,
		};

		vi = fy_generic_indirect_create(gd->gb, &gi);
		assert(vi != fy_invalid);
		v = vi;
	}

	if (gdo->items)
		free(gdo->items);

	gdo->items = NULL;
	gdo->alloc = 0;
	gdo->count = 0;

	gdo->v = v;

	/* root object, create the document state too */
	if (gdo->type == FYGDOT_ROOT && gdo->fyds) {
		vers = fy_document_state_version(gdo->fyds);
		assert(vers);

		tags = fy_document_state_tag_directives(gdo->fyds);
		assert(tags);
		count = 0;
		while (tags[count])
			count++;

		version_explicit = fy_document_state_version_explicit(gdo->fyds);
		tags_explicit = fy_document_state_tags_explicit(gdo->fyds);

		vtags_items = alloca(sizeof(*vtags_items) * count);
		for (i = 0; i < count; i++)
			vtags_items[i] = fy_generic_mapping_create(gd->gb, 2, (fy_generic[]) {
						fy_generic_string_create(gd->gb, "handle"), fy_generic_string_create(gd->gb, tags[i]->handle),
						fy_generic_string_create(gd->gb, "prefix"), fy_generic_string_create(gd->gb, tags[i]->prefix)});

		vtags = fy_generic_sequence_create(gd->gb, count, vtags_items);

		gdo->vds = fy_generic_mapping_create(gd->gb, 4, (fy_generic[]) {
				fy_generic_string_create(gd->gb, "version"), fy_generic_mapping_create(gd->gb, 2, (fy_generic[]) {
						fy_generic_string_create(gd->gb, "major"), fy_generic_int_create(gd->gb, vers->major),
						fy_generic_string_create(gd->gb, "minor"), fy_generic_int_create(gd->gb, vers->minor)}),
				fy_generic_string_create(gd->gb, "version-explicit"), fy_generic_bool_create(gd->gb, version_explicit),
				fy_generic_string_create(gd->gb, "tags"), vtags,
				fy_generic_string_create(gd->gb, "tags-explicit"), fy_generic_bool_create(gd->gb, tags_explicit)});

		free(tags);
	}

	return v;
}

static fy_generic
fy_generic_decoder_object_finalize_and_destroy(struct fy_generic_decoder *gd, struct fy_generic_decoder_obj *gdo)
{
	fy_generic v;

	v = fy_generic_decoder_object_finalize(gd, gdo);
	fy_generic_decoder_object_destroy(gdo);

	return v;
}

static inline int
fy_generic_item_append(fy_generic **itemsp, size_t *countp, size_t *allocp, fy_generic v)
{
	size_t new_alloc;
	fy_generic *new_items;

	if (*countp >= *allocp) {
		new_alloc = *allocp * 2;
		if (new_alloc < 32)
			new_alloc = 32;

		new_items = realloc(*itemsp, new_alloc * sizeof(*new_items));
		if (!new_items)
			return -1;
		*itemsp = new_items;
		*allocp = new_alloc;
	}

	(*itemsp)[(*countp)++] = v;
	return 0;
}

static inline bool
fy_generic_decoder_object_mapping_on_key(struct fy_generic_decoder_obj *gdo)
{
	return gdo && gdo->type == FYGDOT_MAPPING && (gdo->count & 1) == 0;
}

static inline bool
fy_generic_decoder_object_mapping_on_value(struct fy_generic_decoder_obj *gdo)
{
	return gdo && gdo->type == FYGDOT_MAPPING && (gdo->count & 1) == 1;
}

static inline void
fy_generic_decoder_object_mapping_expect_merge_key_value(struct fy_generic_decoder_obj *gdo)
{
	if (!gdo || gdo->type != FYGDOT_MAPPING)
		return;
	gdo->next_is_merge_args = true;
}

static inline bool
fy_generic_decoder_object_mapping_on_merge_key_value(struct fy_generic_decoder_obj *gdo)
{
	return gdo && gdo->type == FYGDOT_MAPPING && gdo->next_is_merge_args;
}

static int
fy_generic_decoder_object_handle_merge_key_value(struct fy_generic_decoder_obj *gdo, fy_generic item)
{
	const fy_generic *pairs, *items;
	fy_generic *tmp_pairs = NULL;
	fy_generic vk, vv;
	size_t i, j, k, l, count, map_count, total_count = 0;
	int rc;

	if (!fy_generic_decoder_object_mapping_on_merge_key_value(gdo))
		return 1;	/* not a merge key value */

	assert(gdo->next_is_merge_args);

	gdo->next_is_merge_args = false;

	if (fy_generic_get_type(item) == FYGT_MAPPING) {
		pairs = fy_generic_mapping_get_pairs(item, &count);
		count *= 2;
		for (i = 0; i < count; i++) {
			rc = fy_generic_item_append(&gdo->items, &gdo->count, &gdo->alloc, pairs[i]);
			if (rc)
				return -1;
		}

		return 0;
	}

	/* it must be a sequence then */
	if (fy_generic_get_type(item) != FYGT_SEQUENCE)
		return -1;

	total_count = 0;
	items = fy_generic_sequence_get_items(item, &map_count);
	for (j = 0; j < map_count; j++) {
		/* must be mapping, and check for it */
		if (fy_generic_get_type(items[j]) != FYGT_MAPPING)
			return -1;
		total_count += fy_generic_mapping_get_pair_count(items[j]);
	}

	/* nothing? alright then */
	if (total_count == 0)
		return 0;

	/* allocate worst case */
	if (total_count <= 32)
		tmp_pairs = alloca(sizeof(*tmp_pairs) * total_count * 2);
	else {
		tmp_pairs = malloc(sizeof(*tmp_pairs) * total_count * 2);
		if (!tmp_pairs)
			return -1;
	}

	k = 0;
	for (j = 0; j < map_count; j++) {
		pairs = fy_generic_mapping_get_pairs(items[j], &count);
		for (i = 0; i < count; i++) {
			vk = pairs[i * 2];
			vv = pairs[i * 2 + 1];

			/* check if key already exists */
			for (l = 0; l < k; l++) {
				if (fy_generic_compare(vk, tmp_pairs[l * 2]) == 0)
					break;
			}
			/* already exists in tmp map, skip */
			if (l < k)
				continue;

			assert(k < total_count);
			tmp_pairs[k * 2] = vk;
			tmp_pairs[k * 2 + 1] = vv;
			k++;
		}
	}

	/* ok, insert whatever is in tmp_pairs to the current map */
	for (l = 0; l < k * 2; l++) {
		rc = fy_generic_item_append(&gdo->items, &gdo->count, &gdo->alloc, tmp_pairs[l]);
		if (rc)
			goto err_out;
	}

	if (total_count > 32)
		free(tmp_pairs);

	return 0;

err_out:
	if (tmp_pairs && total_count > 32)
		free(tmp_pairs);
	return -1;
}

static int
fy_generic_decoder_object_add_item(struct fy_generic_decoder_obj *gdo, fy_generic item)
{
	assert(gdo);
	return fy_generic_item_append(&gdo->items, &gdo->count, &gdo->alloc, item);
}

static fy_generic
fy_generic_decoder_create_scalar(struct fy_generic_decoder *gd, struct fy_event *fye, fy_generic va, fy_generic vt)
{
	enum fy_generic_type force_type = FYGT_INVALID;
	struct fy_token *fyt;
	enum fy_scalar_style style;
	bool needs_indirect;
	const char *text;
	size_t len;
	fy_generic v, vi;

	assert(fye);
	assert(fye->type == FYET_SCALAR);

	fyt = fy_event_get_token(fye);
	assert(fyt);

	text = fy_token_get_text(fyt, &len);
	assert(text);

	v = fy_invalid;

	if (vt == fy_null || vt == fy_invalid) {
		/* non-explicit tag */
		style = fy_token_scalar_style(fyt);

		if (style != FYSS_PLAIN) {
			/* non-plain are strings always */
			v = fy_generic_string_size_create(gd->gb, text, len);
		} else {
			/* TODO JSON YAML1.1 YAML1.2 schemas? */

			v = fy_generic_create_scalar_from_text(gd->gb, FYGS_YAML1_2_CORE, text, len, FYGT_INVALID);
		}
	} else {
		if (fy_generic_compare(vt, gd->vnull_tag) == 0)
			force_type = FYGT_NULL;
		else if (fy_generic_compare(vt, gd->vbool_tag) == 0)
			force_type = FYGT_BOOL;
		else if (fy_generic_compare(vt, gd->vint_tag) == 0)
			force_type = FYGT_INT;
		else if (fy_generic_compare(vt, gd->vfloat_tag) == 0)
			force_type = FYGT_FLOAT;
		else if (fy_generic_compare(vt, gd->vstr_tag) == 0)
			force_type = FYGT_STRING;
		else
			force_type = FYGT_INVALID;	/* fall back */
		v = fy_generic_create_scalar_from_text(gd->gb, FYGS_YAML1_2_CORE, text, len, force_type);
	}

	assert(v != fy_invalid);

	needs_indirect = !gd->resolve &&
			((va != fy_null && va != fy_invalid) ||
			 (vt != fy_null && vt != fy_invalid));

	if (needs_indirect) {
		struct fy_generic_indirect gi = {
			.value = v,
			.anchor = va,
			.tag = vt,
		};

		vi = fy_generic_indirect_create(gd->gb, &gi);
		assert(vi != fy_invalid);
		v = vi;
	}

	if (v == fy_invalid)
		return fy_invalid;

	return v;
}

static bool
fy_generic_decoder_is_merge_key(struct fy_generic_decoder *gd, struct fy_generic_decoder_obj *gdop, struct fy_event *fye)
{
	return gd && gdop && fye &&
		fye->type == FYET_SCALAR &&
		gd->gdo_root && gd->resolve && gd->gdo_root->supports_merge_key &&
		fy_generic_decoder_object_mapping_on_key(gdop) &&
		fy_atom_is_merge_key(fy_token_atom(fye->scalar.value));
}

static bool
fy_generic_decoder_is_valid_merge_key_arg(struct fy_generic_decoder *gd, struct fy_generic_decoder_obj *gdop, fy_generic v)
{
	enum fy_generic_type type;
	const fy_generic *items;
	size_t i, count;

	if (!gd || !gd->resolve)
		return false;

	type = fy_generic_get_type(v);

	/* mapping? OK */
	if (type == FYGT_MAPPING)
		return true;

	/* must be a sequence now */
	if (type != FYGT_SEQUENCE)
		return false;

	/* the sequence must be nothing but mappings */
	items = fy_generic_sequence_get_items(v, &count);
	for (i = 0; i < count; i++) {
		if (fy_generic_get_type(items[i]) != FYGT_MAPPING)
			return false;
	}

	/* all well */
	return true;
}

static enum fy_composer_return
fy_generic_compose_process_event(struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path, void *userdata)
{
	struct fy_generic_decoder *gd = userdata;
	struct fy_generic_decoder_obj *gdo, *gdop = NULL;
	struct fy_token *fyt_anchor, *fyt_tag;
	const struct fy_version *vers;
	const char *anchor, *tag;
	size_t anchor_size, tag_size;
	enum fy_composer_return ret;
	fy_generic v, va, vt;
	int rc __FY_DEBUG_UNUSED__;

	assert(gd);
	if (gd->verbose) {
		fprintf(stderr, "%s: %c%c%c%c%c %3d - %-32s\n",
				fy_event_type_get_text(fye->type),
				fy_path_in_root(path) ? 'R' : '-',
				fy_path_in_sequence(path) ? 'S' : '-',
				fy_path_in_mapping(path) ? 'M' : '-',
				fy_path_in_mapping_key(path) ? 'K' :
					fy_path_in_mapping_value(path) ? 'V' : '-',
				fy_path_in_collection_root(path) ? '/' : '-',
				fy_path_depth(path),
				fy_path_get_text_alloca(path));
	}


	fyt_anchor = fy_event_get_anchor_token(fye);
	if (fyt_anchor) {
		anchor = fy_token_get_text(fyt_anchor, &anchor_size);
		fyp_error_check(fyp, anchor, err_out, "fy_token_get_text() failed");

		va = fy_generic_string_size_create(gd->gb, anchor, anchor_size);
		fyp_error_check(fyp, va != fy_invalid, err_out, "fy_generic_string_size_create() failed");

	} else {
		anchor = NULL;
		anchor_size = 0;
		va = fy_null;
	}

	fyt_tag = fy_event_get_tag_token(fye);
	if (fyt_tag) {
		tag = fy_tag_token_short(fyt_tag, &tag_size);
		fyp_error_check(fyp, tag, err_out, "fy_token_get_text() failed");

		vt = fy_generic_string_size_create(gd->gb, tag, tag_size);
		fyp_error_check(fyp, va != fy_invalid, err_out, "fy_generic_string_size_create() failed");
	} else {
		tag = NULL;
		tag_size = 0;
		vt = fy_null;
	}

	ret = FYCR_OK_CONTINUE;
	switch (fye->type) {

	case FYET_STREAM_START:
	case FYET_STREAM_END:
		ret = FYCR_OK_CONTINUE;
		break;

	case FYET_ALIAS:
		anchor = fy_token_get_text(fy_event_get_token(fye), &anchor_size);
		fyp_error_check(fyp, anchor, err_out, "fy_token_get_text() failed");

		if (gd->resolve) {
			v = fy_generic_decoder_alias_resolve(gd,
					fy_generic_string_size_alloca(anchor, anchor_size));
			if (v == fy_invalid) {
				fy_parser_report_error(fyp, fy_event_get_token(fye),
					!fy_generic_decoder_alias_is_collecting(gd, va) ?
							"Unable to resolve alias" :
							"Recursive reference to alias");
				goto err_out;
			}
		} else {
			v = fy_generic_alias_create(gd->gb,
					fy_generic_string_size_create(gd->gb, anchor, anchor_size));
			fyp_error_check(fyp, v != fy_invalid, err_out, "fy_generic_alias_create() failed");
		}

		anchor = NULL;
		goto add_item;

	case FYET_SCALAR:

		gdop = fy_path_get_parent_user_data(path);

		if (fy_generic_decoder_is_merge_key(gd, gdop, fye)) {
			fy_generic_decoder_object_mapping_expect_merge_key_value(gdop);
			ret = FYCR_OK_CONTINUE;
			break;
		}

		v = fy_generic_decoder_create_scalar(gd, fye, va, vt);
		assert(v != fy_invalid);

		goto add_item;

	case FYET_DOCUMENT_START:

		gdo = fy_generic_decoder_object_create(gd, FYGDOT_ROOT, fy_invalid, fy_invalid);
		fyp_error_check(fyp, gdo, err_out, "fy_generic_decoder_object_create() failed");

		gdo->fyds = fy_document_state_ref(fye->document_start.document_state);
		assert(gdo->fyds);

		fy_path_set_root_user_data(path, gdo);

		vers = fy_document_state_version(gdo->fyds);
		assert(vers);

		gdo->supports_merge_key = vers->major == 1 && vers->minor == 1;

		gd->gdo_root = gdo;

		ret = FYCR_OK_CONTINUE;
		break;

	case FYET_SEQUENCE_START:
	case FYET_MAPPING_START:
		gdo = fy_generic_decoder_object_create(gd,
				fye->type == FYET_SEQUENCE_START ? FYGDOT_SEQUENCE : FYGDOT_MAPPING,
				va, vt);
		fyp_error_check(fyp, gdo, err_out, "fy_generic_decoder_object_create() failed");

		fy_path_set_last_user_data(path, gdo);
		ret = FYCR_OK_CONTINUE;

		if (gd->resolve && anchor) {
			rc = fy_generic_decoder_anchor_register(gd, va, fy_invalid);
			fyp_error_check(fyp, !rc, err_out, "fy_generic_decoder_anchor_register() failed");
			fy_generic_decoder_anchor_collection_starts(gd);
		}

		break;

	case FYET_DOCUMENT_END:
		gdo = fy_path_get_root_user_data(path);
		fy_path_set_root_user_data(path, NULL);

		v = fy_generic_decoder_object_finalize(gd, gdo);
		fyp_error_check(fyp, v != fy_invalid, err_out, "fy_generic_decoder_object_finalize() failed");

		gd->vroot = v;
		gd->vds = gdo->vds;

		fy_generic_decoder_object_destroy(gdo);
		gd->document_ready = true;

		gd->gdo_root = NULL;

		/* we always stop at the end of the document
		 * to give control back to the decoder to
		 * pick up the document */
		ret = FYCR_OK_STOP;
		break;

	case FYET_SEQUENCE_END:
	case FYET_MAPPING_END:

		gdop = fy_path_get_parent_user_data(path);

		gdo = fy_path_get_last_user_data(path);
		fy_path_set_last_user_data(path, NULL);

		v = fy_generic_decoder_object_finalize_and_destroy(gd, gdo);
		fyp_error_check(fyp, v != fy_invalid, err_out, "fy_generic_decoder_object_finalize_and_destroy() failed");

		goto add_item;

	case FYET_NONE:
		/* this is cleanup phase after an error */
		if (!fy_path_in_root(path)) {
			gdo = fy_path_get_last_user_data(path);
			fy_path_set_last_user_data(path, NULL);
		} else {
			gdo = fy_path_get_root_user_data(path);
			fy_path_set_root_user_data(path, NULL);
		}
		if (gdo)
			fy_generic_decoder_object_destroy(gdo);

		break;
	}

	return ret;

add_item:
	if (!gdop)
		gdop = fy_path_get_parent_user_data(path);

	assert(gdop);

	if (gd->resolve && (fye->type == FYET_SEQUENCE_END || fye->type == FYET_MAPPING_END))
		fy_generic_decoder_anchor_collection_ends(gd, v);

	if (gd->resolve && anchor) {
		rc = fy_generic_decoder_anchor_register(gd, va, v);
		fyp_error_check(fyp, !rc, err_out, "fy_generic_decoder_anchor_register() failed");
	}

	if (fy_generic_decoder_object_mapping_on_merge_key_value(gdop)) {

		if (!fy_generic_decoder_is_valid_merge_key_arg(gd, gdop, v)) {
			fy_parser_report_error(fyp, fy_event_get_token(fye),
					"Invalid merge key argument: must be a mapping or a sequence of mappings");
			goto err_out;
		}

		rc = fy_generic_decoder_object_handle_merge_key_value(gdop, v);
		fyp_error_check(fyp, !rc, err_out, "fy_generic_decoder_object_handle_merge_key_value() failed");

	} else {

		rc = fy_generic_decoder_object_add_item(gdop, v);
		fyp_error_check(fyp, !rc, err_out, "fy_generic_decoder_object_add_item() failed");
	}

	return FYCR_OK_CONTINUE;

err_out:
	return FYCR_ERROR;
}

static int fy_generic_decoder_anchor_register(struct fy_generic_decoder *fygd, fy_generic anchor, fy_generic content)
{
	struct fy_generic_anchor *ga = NULL;

	ga = malloc(sizeof(*ga));
	if (!ga)
		goto err_out;
	memset(ga, 0, sizeof(*ga));
	ga->anchor = anchor;
	ga->content = content;

	/* no content yet? collecting */
	if (content == fy_invalid)
		fy_generic_anchor_list_add(&fygd->collecting_anchors, ga);
	else
		fy_generic_anchor_list_add(&fygd->complete_anchors, ga);

	return 0;
err_out:
	return -1;
}

static fy_generic fy_generic_decoder_alias_resolve(struct fy_generic_decoder *fygd, fy_generic anchor)
{
	struct fy_generic_anchor *ga;

	for (ga = fy_generic_anchor_list_head(&fygd->complete_anchors); ga;
			ga = fy_generic_anchor_next(&fygd->complete_anchors, ga)) {
		if (!fy_generic_compare(ga->anchor, anchor))
			return ga->content;
	}
	return fy_invalid;
}

static bool fy_generic_decoder_alias_is_collecting(struct fy_generic_decoder *fygd, fy_generic anchor)
{
	struct fy_generic_anchor *ga;

	for (ga = fy_generic_anchor_list_head(&fygd->complete_anchors); ga;
			ga = fy_generic_anchor_next(&fygd->complete_anchors, ga)) {
		if (!fy_generic_compare(ga->anchor, anchor))
			return true;
	}
	return false;
}

static void fy_generic_decoder_anchor_collection_starts(struct fy_generic_decoder *fygd)
{
	struct fy_generic_anchor *ga;

	/* just increase the nest for all collecting */
	for (ga = fy_generic_anchor_list_head(&fygd->collecting_anchors); ga;
			ga = fy_generic_anchor_next(&fygd->collecting_anchors, ga))
		ga->nest++;
}

static void fy_generic_decoder_anchor_collection_ends(struct fy_generic_decoder *fygd, fy_generic v)
{
	struct fy_generic_anchor *ga, *gan;

	for (ga = fy_generic_anchor_list_head(&fygd->collecting_anchors); ga; ga = gan) {
		gan = fy_generic_anchor_next(&fygd->collecting_anchors, ga);

		assert(ga->nest > 0);
		ga->nest--;
		if (ga->nest > 0)
			continue;

		assert(ga->content == fy_invalid);

		/* move from collecting to complete list */
		fy_generic_anchor_list_del(&fygd->collecting_anchors, ga);
		ga->content = v;
		fy_generic_anchor_list_add(&fygd->complete_anchors, ga);
	}
}

void fy_generic_decoder_destroy(struct fy_generic_decoder *fygd)
{
	struct fy_parser *fyp;
	struct fy_generic_anchor *ga;

	if (!fygd)
		return;

	fyp = fygd->fyp;
	if (fyp) {
		if (fygd->resolve)
			fyp->cfg.flags |= FYPCF_RESOLVE_DOCUMENT;
		else
			fyp->cfg.flags &= ~FYPCF_RESOLVE_DOCUMENT;
	}

	while ((ga = fy_generic_anchor_list_pop(&fygd->collecting_anchors)) != NULL)
		free(ga);

	while ((ga = fy_generic_anchor_list_pop(&fygd->complete_anchors)) != NULL)
		free(ga);

	free(fygd);
}

struct fy_generic_decoder *
fy_generic_decoder_create(struct fy_parser *fyp, struct fy_generic_builder *gb, bool verbose)
{
	struct fy_generic_decoder *fygd = NULL;

	if (!fyp || !gb)
		return NULL;

	fygd = malloc(sizeof(*fygd));
	if (!fygd)
		goto err_out;
	memset(fygd, 0, sizeof(*fygd));

	fygd->fyp = fyp;
	fygd->gb = gb;
	fygd->verbose = verbose;
	fygd->resolve = !!(fyp->cfg.flags & FYPCF_RESOLVE_DOCUMENT);
	fygd->vroot = fy_invalid;
	fygd->vds = fy_invalid;

	fy_generic_anchor_list_init(&fygd->complete_anchors);
	fy_generic_anchor_list_init(&fygd->collecting_anchors);

	/* turn off the stream resolve */
	fyp->cfg.flags &= ~FYPCF_RESOLVE_DOCUMENT;

	/* create the scalar tags (note that on 64bits all these do not allocate) */
	fygd->vnull_tag = fy_generic_string_create(fygd->gb, "!!null");
	fygd->vbool_tag = fy_generic_string_create(fygd->gb, "!!bool");
	fygd->vint_tag = fy_generic_string_create(fygd->gb, "!!int");
	fygd->vfloat_tag = fy_generic_string_create(fygd->gb, "!!float");
	fygd->vstr_tag = fy_generic_string_create(fygd->gb, "!!str");

	return fygd;

err_out:
	fy_generic_decoder_destroy(fygd);
	return NULL;
}

fy_generic fy_generic_decoder_parse_document(struct fy_generic_decoder *fygd, fy_generic *vdsp)
{
	fy_generic vroot;
	int rc;

	if (!fygd)
		return fy_invalid;

	rc = fy_parse_compose(fygd->fyp, fy_generic_compose_process_event, fygd);
	if (rc)
		goto err_out;

	if (fy_parser_get_stream_error(fygd->fyp))
		goto err_out;

	vroot = fygd->vroot;

	if (vdsp)
		*vdsp = fygd->vds;

	fygd->vroot = fy_invalid;
	fygd->vds = fy_invalid;

	return vroot;

err_out:
	if (vdsp)
		*vdsp = fy_invalid;
	return fy_invalid;
}

void fy_generic_decoder_reset(struct fy_generic_decoder *fygd)
{
	struct fy_generic_anchor *ga;

	if (!fygd)
		return;

	/* reset the anchors */
	while ((ga = fy_generic_anchor_list_pop(&fygd->collecting_anchors)) != NULL)
		free(ga);

	while ((ga = fy_generic_anchor_list_pop(&fygd->complete_anchors)) != NULL)
		free(ga);

	if (fygd->gb)
		fy_generic_builder_reset(fygd->gb);
}

fy_generic fy_generic_decoder_parse_all_documents(struct fy_generic_decoder *fygd)
{
	fy_generic vroot, vds, ventry, vdir;
	fy_generic *items = NULL, *items_new;
	size_t count, alloc;

	count = 0;
	alloc = 0;
	items = NULL;

	while ((vroot = fy_generic_decoder_parse_document(fygd, &vds)) != fy_invalid) {

		ventry = fy_generic_mapping_create(fygd->gb, 2, (fy_generic[]){
				fy_generic_string_create(fygd->gb, "root"), vroot,
				fy_generic_string_create(fygd->gb, "docs"), vds });

		if (ventry == fy_invalid)
			goto err_out;

		if (count >= alloc) {
			if (!alloc)
				alloc = 8;
			alloc *= 2;
			items_new = realloc(items, alloc * sizeof(*items));
			if (!items_new)
				goto err_out;
			items = items_new;
		}
		assert(count < alloc);
		items[count++] = ventry;
	}

	if (!count)
		return fy_null;

	vdir = fy_generic_sequence_create(fygd->gb, count, items);
	free(items);

	return vdir;

err_out:
	if (items)
		free(items);
	return fy_invalid;
}
