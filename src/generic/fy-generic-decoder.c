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

static fy_generic fy_generic_decoder_object_finalize(struct fy_generic_decoder *gd, struct fy_generic_decoder_obj *gdo);
static fy_generic fy_generic_decoder_object_finalize_and_destroy(struct fy_generic_decoder *gd,
		struct fy_generic_decoder_obj *gdo);

static int fy_generic_decoder_object_add_item(struct fy_generic_decoder_obj *gdo, fy_generic item);
static fy_generic fy_generic_decoder_create_scalar(struct fy_generic_decoder *gd, struct fy_event *fye,
		fy_generic va, fy_generic vt, fy_generic vcomment,
		fy_generic vstyle, fy_generic vfailsafe_str,
		fy_generic vmarker,
		bool *is_empty_plain_scalarp);

static int fy_generic_decoder_anchor_register(struct fy_generic_decoder *gd, fy_generic anchor, fy_generic content);
static fy_generic fy_generic_decoder_alias_resolve(struct fy_generic_decoder *gd, fy_generic anchor);
static bool fy_generic_decoder_alias_is_collecting(struct fy_generic_decoder *gd, fy_generic anchor);
static void fy_generic_decoder_anchor_collection_starts(struct fy_generic_decoder *gd);
static void fy_generic_decoder_anchor_collection_ends(struct fy_generic_decoder *gd, fy_generic v);

void fy_generic_decoder_object_cleanup(struct fy_generic_decoder_obj *gdo)
{
	if (gdo->type == FYGDOT_INVALID)	// already clean
		return;

	if (gdo->items)
		free(gdo->items);
	if (gdo->fyds)
		fy_document_state_unref(gdo->fyds);
	gdo->type = FYGDOT_INVALID;
	gdo->alloc = 0;
	gdo->count = 0;
	gdo->items = 0;
	gdo->v = fy_invalid;
	gdo->anchor = fy_invalid;
	gdo->tag = fy_invalid;
	gdo->marker = fy_invalid;
	gdo->comment = fy_invalid;
	gdo->style = fy_invalid;
	gdo->failsafe_str = fy_invalid;
	gdo->marker_start = fy_invalid;
	gdo->marker_end = fy_invalid;
	gdo->fyds = NULL;
	gdo->vds = fy_invalid;
	gdo->supports_merge_key = false;
	gdo->next_is_merge_args = false;
	gdo->last_key_was_empty_plain_scalar = false;
}

struct fy_generic_decoder_obj *fy_generic_decoder_object_alloc(struct fy_generic_decoder *gd)
{
	struct fy_generic_decoder_obj *gdo;

	if (!gd)
		return NULL;

	gdo = fy_generic_decoder_obj_list_pop(&gd->recycled_gdos);
	if (!gdo) {
		gdo = malloc(sizeof(*gdo));
		if (!gdo)
			return NULL;
		memset(gdo, 0, sizeof(*gdo));
	}

	/* not yet instantiated */
	gdo->type = FYGDOT_INVALID;
	gdo->anchor = fy_invalid;
	gdo->tag = fy_invalid;
	gdo->marker = fy_invalid;
	gdo->comment = fy_invalid;
	gdo->style = fy_invalid;
	gdo->failsafe_str = fy_invalid;
	gdo->marker_start = fy_invalid;
	gdo->marker_end = fy_invalid;
	gdo->v = fy_invalid;
	gdo->vds = fy_invalid;

	return gdo;
}

static void
fy_generic_decoder_object_free(struct fy_generic_decoder_obj *gdo)
{
	if (!gdo)
		return;

	fy_generic_decoder_object_cleanup(gdo);
	free(gdo);
}

void fy_generic_decoder_object_recycle(struct fy_generic_decoder *gd, struct fy_generic_decoder_obj *gdo)
{
	if (!gdo)
		return;

	fy_generic_decoder_object_cleanup(gdo);

	if (!gd)
		fy_generic_decoder_object_free(gdo);
	else
		fy_generic_decoder_obj_list_push(&gd->recycled_gdos, gdo);
}

static fy_generic
fy_generic_decoder_object_finalize(struct fy_generic_decoder *gd, struct fy_generic_decoder_obj *gdo)
{
	struct fy_parser *fyp = gd->fyp;
	fy_generic v, vi;
	uintptr_t gi_flags;
	bool needs_indirect;

	v = fy_invalid;

	assert(gdo);

	switch (gdo->type) {

	case FYGDOT_ROOT:
		fyp_error_check(fyp, gdo->count <= 1, err_out,
				"bad root finalize");

		if (gdo->count == 0)
			v = fy_null;
		else
			v = gdo->items[0];

		break;

	case FYGDOT_SEQUENCE:
	case FYGDOT_MAPPING: {
		/* we know that the items in the collection are created
		 * using the provided builder, so we can dispense with the
		 * internalization checks
		 */
		struct fy_generic_op_args args = {
			.common.count = gdo->count,
			.common.items = gdo->items,
		};
		v = fy_generic_op_args(gd->gb,
				(gdo->type == FYGDOT_SEQUENCE ?
					FYGBOPF_CREATE_SEQ :
					FYGBOPF_CREATE_MAP) |
					FYGBOPF_NO_CHECKS | FYGBOPF_MAP_ITEM_COUNT,
				fy_null, &args);
		fyp_error_check(fyp, fy_generic_is_valid(v), err_out,
				"unable to create collection");

		if (fy_generic_is_valid(gdo->marker_start) && fy_generic_is_valid(gdo->marker_end)) {

			fy_generic_sequence_handle starth = fy_cast(gdo->marker_start, fy_seq_handle_null);
			fy_generic_sequence_handle endh = fy_cast(gdo->marker_end, fy_seq_handle_null);

			/* verify things are sane (they should be) */
			assert(starth);
			assert(endh);
			assert(starth->count == 6);
			assert(endh->count == 6);

			gdo->marker = fy_gb_sequence(gd->gb,
					starth->items[0], starth->items[1], starth->items[2],
					endh->items[3], endh->items[4], endh->items[5]);
		}

		} break;

	default:
		FY_IMPOSSIBLE_ABORT();
	}

	gi_flags = 0;
	if (fy_generic_is_valid(gdo->anchor))
		gi_flags |= FYGIF_ANCHOR;
	if (fy_generic_is_valid(gdo->tag))
		gi_flags |= FYGIF_TAG;
	if (fy_generic_is_valid(gdo->marker))
		gi_flags |= FYGIF_MARKER;
	if (fy_generic_is_valid(gdo->comment))
		gi_flags |= FYGIF_COMMENT;
	if (fy_generic_is_valid(gdo->style))
		gi_flags |= FYGIF_STYLE;
	if (fy_generic_is_valid(gdo->failsafe_str))
		gi_flags |= FYGIF_FAILSAFE_STR;

	needs_indirect = gi_flags != 0;

	if (needs_indirect) {
		fy_generic_indirect gi = {
			.flags = FYGIF_VALUE | gi_flags,
			.value = v,
			.anchor = gdo->anchor,
			.tag = gdo->tag,
			.marker = gdo->marker,
			.comment = gdo->comment,
			.style = gdo->style,
			.failsafe_str = gdo->failsafe_str,
		};

		vi = fy_gb_indirect_create(gd->gb, &gi);
		fyp_error_check(fyp, fy_generic_is_valid(vi), err_out,
				"fy_gb_indirect_create() failed");

		v = vi;
	}

	if (gdo->items)
		free(gdo->items);

	gdo->items = NULL;
	gdo->alloc = 0;
	gdo->count = 0;

	gdo->v = v;

	return v;

err_out:
	return fy_invalid;
}

static fy_generic
fy_generic_decoder_object_finalize_and_destroy(struct fy_generic_decoder *gd, struct fy_generic_decoder_obj *gdo)
{
	fy_generic v;

	v = fy_generic_decoder_object_finalize(gd, gdo);
	fy_generic_decoder_object_recycle(gd, gdo);

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
fy_generic_decoder_object_handle_merge_key_value(struct fy_generic_decoder *gd,
		struct fy_generic_decoder_obj *gdo, fy_generic item)
{
	struct fy_parser *fyp;
	const fy_generic *items;
	const fy_generic_map_pair *pairs;
	fy_generic_map_pair *tmp_pairs = NULL;
	fy_generic vk;
	size_t i, j, k, l, count, map_count, total_count = 0;
	int rc;

	if (!fy_generic_decoder_object_mapping_on_merge_key_value(gdo))
		return 1;	/* not a merge key value */

	fyp = gd->fyp;

	fyp_error_check(fyp, gdo->next_is_merge_args, err_out,
			"missing merge args");

	gdo->next_is_merge_args = false;

	if (fy_generic_get_type(item) == FYGT_MAPPING) {
		pairs = fy_generic_mapping_get_pairs(item, &count);
		for (i = 0; i < count; i++) {
			rc = fy_generic_item_append(&gdo->items, &gdo->count, &gdo->alloc, pairs[i].key);
			fyp_error_check(fyp, !rc, err_out,
					"fy_generic_item_append (key) failed\n");

			rc = fy_generic_item_append(&gdo->items, &gdo->count, &gdo->alloc, pairs[i].value);
			fyp_error_check(fyp, !rc, err_out,
					"fy_generic_item_append (value) failed\n");
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
		tmp_pairs = alloca(sizeof(*tmp_pairs) * total_count);
	else {
		tmp_pairs = malloc(sizeof(*tmp_pairs) * total_count);
		fyp_error_check(fyp, tmp_pairs, err_out,
				"malloc() failed");
	}

	k = 0;
	for (j = 0; j < map_count; j++) {
		pairs = fy_generic_mapping_get_pairs(items[j], &count);
		for (i = 0; i < count; i++) {
			vk = pairs[i].key;

			/* check if key already exists */
			for (l = 0; l < k; l++) {
				if (fy_generic_compare(vk, tmp_pairs[l].key) == 0)
					break;
			}
			/* already exists in tmp map, skip */
			if (l < k)
				continue;

			assert(k < total_count);
			tmp_pairs[k] = pairs[i];
			k++;
		}
	}

	/* ok, insert whatever is in tmp_pairs to the current map */
	for (l = 0; l < k; l++) {
		rc = fy_generic_item_append(&gdo->items, &gdo->count, &gdo->alloc, tmp_pairs[l].key);
		fyp_error_check(fyp, !rc, err_out,
				"fy_generic_item_append() (key) failed");

		rc = fy_generic_item_append(&gdo->items, &gdo->count, &gdo->alloc, tmp_pairs[l].value);
		fyp_error_check(fyp, !rc, err_out,
				"fy_generic_item_append() (value) failed");
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
	return fy_generic_item_append(&gdo->items, &gdo->count, &gdo->alloc, item);
}

static fy_generic
fy_generic_decoder_create_scalar(struct fy_generic_decoder *gd, struct fy_event *fye,
				 fy_generic va, fy_generic vt, fy_generic vcomment,
				 fy_generic vstyle, fy_generic vfailsafe_str,
				 fy_generic vmarker,
				 bool *is_empty_plain_scalarp)
{
	const char *tag, *p, *sfx;
	const char *yaml_tag_pfx = "tag:yaml.org,2002";
	const size_t yaml_tag_pfx_size = strlen(yaml_tag_pfx);
	struct fy_parser *fyp;
	enum fy_generic_type force_type = FYGT_INVALID;
	struct fy_token *fyt;
	enum fy_scalar_style style;
	uintptr_t gi_flags;
	bool needs_indirect;
	const char *text;
	size_t len;
	fy_generic v, vi;

	assert(gd);
	assert(fye);
	assert(fye->type == FYET_SCALAR);

	fyp = gd->fyp;

	fyt = fy_event_get_token(fye);
	fyp_error_check(fyp, fyt, err_out,
			"fy_event_get_token() failed");

	text = fy_token_get_text(fyt, &len);
	fyp_error_check(fyp, text, err_out,
			"fy_token_get_text() failed");

	v = fy_invalid;

	style = fy_token_scalar_style(fyt);

	if (fy_generic_is_invalid(vt)) {
		/* non-explicit tag */

		if (style != FYSS_PLAIN) {
			/* non-plain are strings always */
			v = fy_gb_string_size_create(gd->gb, text, len);

			fyp_error_check(fyp, !fy_generic_is_invalid(v), err_out,
					"invalid scalar created");

		} else {
			v = fy_gb_create_scalar_from_text(gd->gb, text, len, FYGT_INVALID);

			fyp_error_check(fyp, !fy_generic_is_invalid(v), err_out,
					"invalid scalar created");

		}
	} else {
		/* default */
		force_type = FYGT_STRING;

		tag = fy_cast(vt, "");
		fyp_error_check(fyp, tag[0], err_out,
				"fy_cast() failed");

		sfx = NULL;
		p = strrchr(tag, ':');
		/* if it's not a core tag then we're just creating a scalar */
		if (!(p && (size_t)(p - tag) == yaml_tag_pfx_size && !memcmp(tag, yaml_tag_pfx, yaml_tag_pfx_size)))
			goto create_scalar;
		sfx = p + 1;
		if (!strcmp(sfx, "null"))
			force_type = FYGT_NULL;
		else if (!strcmp(sfx, "bool"))
			force_type = FYGT_BOOL;
		else if (!strcmp(sfx, "int"))
			force_type = FYGT_INT;
		else if (!strcmp(sfx, "float"))
			force_type = FYGT_FLOAT;
		else if (!strcmp(sfx, "str"))
			force_type = FYGT_STRING;
		else
			force_type = FYGT_INVALID;

create_scalar:
		v = fy_gb_create_scalar_from_text(gd->gb, text, len, force_type);

		FYP_TOKEN_ERROR_CHECK(fyp, fy_event_get_token(fye), FYEM_PARSE,
				fy_generic_is_valid(v), err_out,
				"failed to create scalar with tag %s", tag);
	}

	gi_flags = 0;
	if (fy_generic_is_valid(va))
		gi_flags |= FYGIF_ANCHOR;
	if (fy_generic_is_valid(vt))
		gi_flags |= FYGIF_TAG;
	if (fy_generic_is_valid(vmarker))
		gi_flags |= FYGIF_MARKER;
	if (fy_generic_is_valid(vcomment))
		gi_flags |= FYGIF_COMMENT;
	if (fy_generic_is_valid(vstyle))
		gi_flags |= FYGIF_STYLE;
	if (fy_generic_is_valid(vfailsafe_str))
		gi_flags |= FYGIF_FAILSAFE_STR;

	needs_indirect = gi_flags != 0;

	if (needs_indirect) {
		fy_generic_indirect gi = {
			.flags = FYGIF_VALUE | gi_flags,
			.value = v,
			.anchor = va,
			.tag = vt,
			.marker = vmarker,
			.comment = vcomment,
			.style = vstyle,
			.failsafe_str = fy_invalid,
		};

		vi = fy_gb_indirect_create(gd->gb, &gi);
		fyp_error_check(fyp, fy_generic_is_valid(vi), err_out,
				"invalid indirect scalar created");

		v = vi;
	}

	if (is_empty_plain_scalarp)
		*is_empty_plain_scalarp = style == FYSS_PLAIN && len == 0;

	return v;

err_out:
	return fy_invalid;
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
	struct fy_generic_builder *gb = gd->gb;
	struct fy_generic_decoder_obj *gdo, *gdop = NULL;
	struct fy_token *fyt, *fyt_anchor, *fyt_tag;
	const struct fy_version *vers;
	const char *anchor, *tag, *comment;
	size_t anchor_size, tag_size;
	enum fy_composer_return ret;
	enum fy_scalar_style ss;
	enum fy_collection_style cs;
	fy_generic v, va, vt, vcomment, vstyle, vfailsafe_str, vmarker, vds;
	const struct fy_mark *start_mark, *end_mark;
	bool is_empty_plain_scalar = false;
	int rc __FY_DEBUG_UNUSED__;

	assert(gd);
	if (gd->parse_flags & FYGDPF_TRACE) {
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

	fyt = fy_event_get_token(fye);

	fyt_anchor = fy_event_get_anchor_token(fye);
	if (fyt_anchor) {
		anchor = fy_token_get_text(fyt_anchor, &anchor_size);
		fyp_error_check(fyp, anchor, err_out, "fy_token_get_text() failed");

		va = fy_gb_string_size_create(gb, anchor, anchor_size);
		fyp_error_check(fyp, fy_generic_is_valid(va), err_out,
				"fy_generic_string_size_create() failed");

	} else {
		anchor = NULL;
		anchor_size = 0;
		va = fy_invalid;
	}

	fyt_tag = fy_event_get_tag_token(fye);
	if (fyt_tag) {
		/* get the full tag */
		tag = fy_token_get_text(fyt_tag, &tag_size);
		fyp_error_check(fyp, tag, err_out, "fy_token_get_text() failed");

		vt = fy_gb_string_size_create(gb, tag, tag_size);
		fyp_error_check(fyp, fy_generic_is_valid(vt), err_out,
				"fy_generic_string_size_create() failed");
	} else {
		tag = NULL;
		tag_size = 0;
		vt = fy_invalid;
	}

	if ((gd->parse_flags & FYGDPF_KEEP_COMMENTS) && fy_token_has_any_comment(fyt)) {
		comment = fy_token_get_comments(fyt);
		fyp_error_check(fyp, comment, err_out,
			"fy_token_get_comments() failed");

		vcomment = fy_gb_string_create(gb, comment);
		fyp_error_check(fyp, fy_generic_is_valid(vcomment), err_out,
				"fy_generic_string_create() failed");
	} else {
		vcomment = fy_invalid;
		comment = NULL;
	}

	vstyle = fy_invalid;
	if (gd->parse_flags & FYGDPF_KEEP_STYLE) {
		if (fye->type == FYET_SCALAR) {
			ss = fy_token_scalar_style(fy_event_get_token(fye));
			if (ss != FYSS_ANY)
				vstyle.v = fy_generic_in_place_unsigned_int((unsigned int)ss);
		} else if (fye->type == FYET_SEQUENCE_START || fye->type == FYET_MAPPING_START) {
			cs = fy_token_collection_style(fy_event_get_token(fye));
			if (cs != FYCS_ANY)
				vstyle.v = fy_generic_in_place_unsigned_int((unsigned int)cs);
		}
	}

	vfailsafe_str = fy_invalid;

	vmarker = fy_invalid;
	if ((gd->parse_flags & FYGDPF_CREATE_MARKERS) &&
	    (start_mark = fy_event_style_start_mark(fye)) != NULL &&
	    (end_mark = fy_event_style_end_mark(fye)) != NULL) {
		vmarker = fy_gb_sequence(gb,
				start_mark->input_pos, start_mark->line, start_mark->column,
				end_mark->input_pos, end_mark->line, end_mark->column);
	}

	ret = FYCR_OK_CONTINUE;
	switch (fye->type) {

	case FYET_STREAM_START:
	case FYET_STREAM_END:
		ret = FYCR_OK_CONTINUE;
		break;

	case FYET_ALIAS:
		anchor = fy_token_get_text(fyt, &anchor_size);
		fyp_error_check(fyp, anchor, err_out, "fy_token_get_text() failed");

		if (gd->resolve) {
			v = fy_generic_decoder_alias_resolve(gd, fy_string_size(anchor, anchor_size));
			if (fy_generic_is_invalid(v)) {
				fy_parser_report_error(fyp, fyt,
					!fy_generic_decoder_alias_is_collecting(gd, va) ?
							"Unable to resolve alias" :
							"Recursive reference to alias");
				goto err_out;
			}
		} else {
			v = fy_gb_alias_create(gb,
					fy_gb_string_size_create(gb, anchor, anchor_size));
			fyp_error_check(fyp, fy_generic_is_valid(v), err_out,
					"fy_generic_alias_create() failed");
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

		v = fy_generic_decoder_create_scalar(gd, fye,
				va, vt, vcomment, vstyle, vfailsafe_str, vmarker,
				&is_empty_plain_scalar);
		fyp_error_check(fyp, fy_generic_is_valid(v), err_out,
				"fy_generic_decoder_create_scalar() failed");

		goto add_item;

	case FYET_DOCUMENT_START:

		gdo = fy_generic_decoder_object_alloc(gd);
		fyp_error_check(fyp, gdo, err_out, "fy_generic_decoder_object_alloc() failed");

		gdo->type = FYGDOT_ROOT;
		gdo->fyds = fy_document_state_ref(fye->document_start.document_state);

		fy_path_set_root_user_data(path, gdo);

		vers = fy_document_state_version(gdo->fyds);

		gdo->supports_merge_key = vers->major == 1 && vers->minor == 1;

		gd->gdo_root = gdo;

		/* update schema if possible */
		gd->curr_parser_mode = fy_parser_get_mode(fyp);

		/* if we're tracking what the parser does, set it */
		if (gd->original_schema == FYGS_AUTO)
			fy_gb_set_schema_from_parser_mode(gb, gd->curr_parser_mode);

		ret = FYCR_OK_CONTINUE;
		break;

	case FYET_SEQUENCE_START:
	case FYET_MAPPING_START:
		gdo = fy_generic_decoder_object_alloc(gd);
		fyp_error_check(fyp, gdo, err_out, "fy_generic_decoder_object_alloc() failed");

		gdo->type = fye->type == FYET_SEQUENCE_START ? FYGDOT_SEQUENCE : FYGDOT_MAPPING;
		gdo->anchor = va;
		gdo->tag = vt;
		gdo->comment = vcomment;
		gdo->style = vstyle;
		gdo->marker_start = vmarker;

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
		fyp_error_check(fyp, fy_generic_is_valid(v), err_out,
				"fy_generic_decoder_object_finalize() failed");

		/* root object, create the document state too */
		assert(gdo->type == FYGDOT_ROOT);

		if (!(gd->parse_flags & FYGDPF_DISABLE_DIRECTORY))
			vds = fy_generic_vds_create_from_document_state(gb, v, gdo->fyds);
		else
			vds = fy_null;

		fyp_error_check(fyp, fy_generic_is_valid(vds), err_out,
				"failed to create vds");

		gd->vroot = v;
		gd->vds = fy_gb_internalize(gb, vds);
		fyp_error_check(fyp, !fy_generic_is_invalid(gd->vds), err_out,
				"fy_gb_internalize() failed");

		fy_generic_decoder_object_recycle(gd, gdo);
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

		gdo->marker_end = vmarker;

		v = fy_generic_decoder_object_finalize_and_destroy(gd, gdo);
		fyp_error_check(fyp, fy_generic_is_valid(v), err_out, "fy_generic_decoder_object_finalize_and_destroy() failed");

		goto add_item;

	case FYET_NONE:
		/* this is cleanup phase after an error */
		gdo = fy_path_get_last_user_data(path);
		if (gdo)
			fy_path_set_last_user_data(path, NULL);

		/* the root is pumped twice, one for the user data
		 * once for root */
		if (!gdo && fy_path_in_root(path)) {
			gdo = fy_path_get_root_user_data(path);
			if (gdo)
				fy_path_set_root_user_data(path, NULL);
		}

		if (gdo)
			fy_generic_decoder_object_recycle(gd, gdo);

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

	/* disallow bare : */
	if (fy_gb_get_schema(gd->gb) == FYGS_YAML1_1_PYYAML && gdop->type == FYGDOT_MAPPING) {
		if (!(gdop->count & 1)) {
			/* key... */
			gdop->last_key_was_empty_plain_scalar = is_empty_plain_scalar;
		} else {
			/* value */
			if (gdop->last_key_was_empty_plain_scalar && is_empty_plain_scalar) {
				fy_parser_report_error(fyp, fyt, "bare : detected (illegal in pyyaml mode)");
				goto err_out;
			}
			gdop->last_key_was_empty_plain_scalar = false;
		}
	}

	if (fy_generic_decoder_object_mapping_on_merge_key_value(gdop)) {

		if (!fy_generic_decoder_is_valid_merge_key_arg(gd, gdop, v)) {
			fy_parser_report_error(fyp, fyt,
					"Invalid merge key argument: must be a mapping or a sequence of mappings");
			goto err_out;
		}

		rc = fy_generic_decoder_object_handle_merge_key_value(gd, gdop, v);
		fyp_error_check(fyp, !rc, err_out, "fy_generic_decoder_object_handle_merge_key_value() failed");

	} else {

		rc = fy_generic_decoder_object_add_item(gdop, v);
		fyp_error_check(fyp, !rc, err_out, "fy_generic_decoder_object_add_item() failed");
	}

	return FYCR_OK_CONTINUE;

err_out:
	return FYCR_ERROR;
}

static fy_generic
fy_generic_decoder_strip_non_content(struct fy_generic_decoder *gd, fy_generic v)
{
	fy_generic_indirect gi;

	if (fy_generic_is_direct(v))
		return v;

	/* can it just be the value? */
	fy_generic_indirect_get(v, &gi);
	if (fy_generic_is_invalid(gi.anchor) &&
	    fy_generic_is_invalid(gi.marker) &&
	    fy_generic_is_invalid(gi.comment) &&
	    fy_generic_is_invalid(gi.style) &&
	    fy_generic_is_invalid(gi.failsafe_str))
		return v;

	gi.flags &= ~(FYGIF_ANCHOR | FYGIF_MARKER | FYGIF_COMMENT |
		      FYGIF_STYLE | FYGIF_FAILSAFE_STR);
	gi.anchor = fy_invalid;
	gi.marker = fy_invalid;
	gi.comment = fy_invalid;
	gi.style = fy_invalid;
	gi.failsafe_str = fy_invalid;

	return fy_gb_indirect_create(gd->gb, &gi);
}

static int fy_generic_decoder_anchor_register(struct fy_generic_decoder *gd, fy_generic anchor, fy_generic content)
{
	struct fy_parser *fyp;
	struct fy_generic_anchor *ga = NULL;

	assert(gd);

	fyp = gd->fyp;

	fyp_error_check(fyp, fy_generic_is_string(anchor), err_out,
			"anchor is not a string");

	ga = malloc(sizeof(*ga));
	fyp_error_check(fyp, ga, err_out,
			"malloc() failed");

	memset(ga, 0, sizeof(*ga));
	ga->anchor = anchor;

	/* no content yet? collecting */
	if (fy_generic_is_invalid(content)) {
		ga->content = fy_invalid;
		fy_generic_anchor_list_add(&gd->collecting_anchors, ga);
	} else {
		ga->content = fy_generic_decoder_strip_non_content(gd, content);
		fy_generic_anchor_list_add(&gd->complete_anchors, ga);
	}

	return 0;
err_out:
	return -1;
}

static fy_generic fy_generic_decoder_alias_resolve(struct fy_generic_decoder *gd, fy_generic anchor)
{
	struct fy_generic_anchor *ga;

	for (ga = fy_generic_anchor_list_head(&gd->complete_anchors); ga;
			ga = fy_generic_anchor_next(&gd->complete_anchors, ga)) {
		if (!fy_generic_compare(ga->anchor, anchor))
			return ga->content;
	}
	return fy_invalid;
}

static bool fy_generic_decoder_alias_is_collecting(struct fy_generic_decoder *gd, fy_generic anchor)
{
	struct fy_generic_anchor *ga;

	for (ga = fy_generic_anchor_list_head(&gd->complete_anchors); ga;
			ga = fy_generic_anchor_next(&gd->complete_anchors, ga)) {
		if (!fy_generic_compare(ga->anchor, anchor))
			return true;
	}
	return false;
}

static void fy_generic_decoder_anchor_collection_starts(struct fy_generic_decoder *gd)
{
	struct fy_generic_anchor *ga;

	/* just increase the nest for all collecting */
	for (ga = fy_generic_anchor_list_head(&gd->collecting_anchors); ga;
			ga = fy_generic_anchor_next(&gd->collecting_anchors, ga))
		ga->nest++;
}

static void fy_generic_decoder_anchor_collection_ends(struct fy_generic_decoder *gd, fy_generic v)
{
	struct fy_generic_anchor *ga, *gan;

	for (ga = fy_generic_anchor_list_head(&gd->collecting_anchors); ga; ga = gan) {
		gan = fy_generic_anchor_next(&gd->collecting_anchors, ga);

		assert(ga->nest > 0);
		ga->nest--;
		if (ga->nest > 0)
			continue;

		assert(fy_generic_is_invalid(ga->content));

		/* move from collecting to complete list */
		fy_generic_anchor_list_del(&gd->collecting_anchors, ga);
		ga->content = fy_generic_decoder_strip_non_content(gd, v);
		fy_generic_anchor_list_add(&gd->complete_anchors, ga);
	}
}

void fy_generic_decoder_destroy(struct fy_generic_decoder *gd)
{
	struct fy_parser *fyp;
	struct fy_generic_anchor *ga;
	struct fy_generic_decoder_obj *gdo;

	if (!gd)
		return;

	fyp = gd->fyp;
	if (fyp) {
		if (gd->resolve)
			fyp->cfg.flags |= FYPCF_RESOLVE_DOCUMENT;
		else
			fyp->cfg.flags &= ~FYPCF_RESOLVE_DOCUMENT;
	}

	while ((ga = fy_generic_anchor_list_pop(&gd->collecting_anchors)) != NULL)
		free(ga);

	while ((ga = fy_generic_anchor_list_pop(&gd->complete_anchors)) != NULL)
		free(ga);

	while ((gdo = fy_generic_decoder_obj_list_pop(&gd->recycled_gdos)) != NULL)
		fy_generic_decoder_object_free(gdo);

	free(gd);
}

struct fy_generic_decoder *
fy_generic_decoder_create(struct fy_parser *fyp, struct fy_generic_builder *gb)
{
	struct fy_generic_decoder *gd = NULL;

	if (!fyp || !gb)
		return NULL;

	gd = malloc(sizeof(*gd));
	if (!gd)
		goto err_out;
	memset(gd, 0, sizeof(*gd));

	gd->fyp = fyp;
	fy_generic_decoder_obj_list_init(&gd->recycled_gdos);

	gd->gb = gb;
	gd->original_schema = fy_gb_get_schema(gb);
	gd->curr_parser_mode = fy_parser_get_mode(fyp);

	/* if we're tracking what the parser does, set it */
	if (gd->original_schema == FYGS_AUTO)
		fy_gb_set_schema_from_parser_mode(gb, gd->curr_parser_mode);

	gd->resolve = !!(fyp->cfg.flags & FYPCF_RESOLVE_DOCUMENT);
	gd->vroot = fy_invalid;
	gd->vds = fy_invalid;

	fy_generic_anchor_list_init(&gd->complete_anchors);
	fy_generic_anchor_list_init(&gd->collecting_anchors);

	/* turn off the stream resolve */
	fyp->cfg.flags &= ~FYPCF_RESOLVE_DOCUMENT;

	return gd;

err_out:
	fy_generic_decoder_destroy(gd);
	return NULL;
}

static int
fy_generic_decoder_parse_document(struct fy_generic_decoder *gd,
				  fy_generic *vdsp, fy_generic *vrootp)
{
	int rc;

	if (!gd || !vrootp)
		goto err_out;

	rc = fy_parse_compose(gd->fyp, fy_generic_compose_process_event, gd);
	if (rc)
		goto err_out;

	if (fy_parser_get_stream_error(gd->fyp))
		goto err_out;

	/* invalid root, is end of stream */
	rc = fy_generic_is_valid(gd->vroot) ? 0 : 1;

	if (vdsp)
		*vdsp = gd->vds;
	if (vrootp)
		*vrootp = gd->vroot;

	gd->vroot = fy_invalid;
	gd->vds = fy_invalid;

	return rc;

err_out:
	if (vdsp)
		*vdsp = fy_invalid;
	if (vrootp)
		*vrootp = fy_invalid;
	return -1;
}

void fy_generic_decoder_reset(struct fy_generic_decoder *gd)
{
	struct fy_generic_anchor *ga;

	if (!gd)
		return;

	/* reset the anchors */
	while ((ga = fy_generic_anchor_list_pop(&gd->collecting_anchors)) != NULL)
		free(ga);

	while ((ga = fy_generic_anchor_list_pop(&gd->complete_anchors)) != NULL)
		free(ga);

	if (gd->gb)
		fy_generic_builder_reset(gd->gb);
}

fy_generic fy_generic_decoder_parse(struct fy_generic_decoder *gd,
				    enum fy_generic_decoder_parse_flags flags)
{
	fy_generic vroot, vds, ventry, v;
	fy_generic *items = NULL, *items_new;
	size_t count, alloc;
	enum fy_generic_schema old_schema;
	int rc;

	count = 0;
	alloc = 0;
	items = NULL;

	old_schema = fy_gb_get_schema(gd->gb);

	/* hack - set the pyyaml quirk */
	if (flags & FYGDPF_PYYAML_COMPAT) {
		fy_gb_set_schema(gd->gb, FYGS_YAML1_1_PYYAML);
		gd->original_schema = FYGS_YAML1_1_PYYAML;
	}

	gd->parse_flags = flags;
	while ((rc = fy_generic_decoder_parse_document(gd, &vds, &vroot)) == 0) {

		ventry = !(flags & FYGDPF_DISABLE_DIRECTORY) ? vds : vroot;

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

		if (!(flags & FYGDPF_MULTI_DOCUMENT))
			break;
	}

	/* error parsing */
	if (rc < 0) {
		v = fy_invalid;
		goto err_out;
	}

	if (!count) {
		v = fy_null;
		goto out;
	}

	if (!(flags & FYGDPF_MULTI_DOCUMENT)) {
		v = items[0];
	} else {
		struct fy_generic_op_args args = {
			.common.count = count,
			.common.items = items,
		};
		v = fy_generic_op_args(gd->gb,
				FYGBOPF_CREATE_SEQ | FYGBOPF_NO_CHECKS,
				fy_null, &args);
		if (fy_generic_is_invalid(v))
			goto err_out;
	}

out:
	fy_gb_set_schema(gd->gb, old_schema);

	if (items)
		free(items);

	return v;

err_out:
	v = fy_invalid;
	goto out;
}
