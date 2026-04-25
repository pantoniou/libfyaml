/*
 * fy-generic-docbuilder.c - streaming generic document builder
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>

#include <libfyaml.h>

#include "fy-diag.h"
#include "fy-parse.h"
#include "fy-event.h"
#include "fy-docstate.h"

#include "fy-generic.h"
#include "fy-generic-decoder.h"

struct fy_generic_document_builder {
	struct fy_generic_document_builder_cfg cfg;
	struct fy_parser *fyp;
	struct fy_generic_decoder_obj_list recycled_gdos;
	enum fy_generic_schema original_schema;
	enum fy_parser_mode curr_parser_mode;
	struct fy_generic_anchor_list complete_anchors;
	struct fy_generic_anchor_list collecting_anchors;
	struct fy_generic_decoder_obj *gdo_root;
	struct fy_generic_decoder_obj **stack;
	unsigned int stack_top;
	unsigned int stack_alloc;
	unsigned int max_depth;
	fy_generic document;
	bool resolve;
	bool single_mode;
	bool in_stream;
	bool doc_done;
};

static int fygdb_vdiag(struct fy_generic_document_builder *fygdb, unsigned int flags,
		       const char *file, int line, const char *func,
		       const char *fmt, va_list ap)
{
	struct fy_diag_ctx fydc;

	if (!fygdb || !fmt || !fygdb->cfg.diag)
		return -1;

	if ((int)((flags & FYDF_LEVEL_MASK) >> FYDF_LEVEL_SHIFT) <
	    (int)fygdb->cfg.diag->cfg.level)
		return 0;

	memset(&fydc, 0, sizeof(fydc));
	fydc.level = (flags & FYDF_LEVEL_MASK) >> FYDF_LEVEL_SHIFT;
	fydc.module = (flags & FYDF_MODULE_MASK) >> FYDF_MODULE_SHIFT;
	fydc.source_file = file;
	fydc.source_line = line;
	fydc.source_func = func;
	fydc.line = -1;
	fydc.column = -1;

	return fy_vdiag(fygdb->cfg.diag, &fydc, fmt, ap);
}

static int fygdb_diag(struct fy_generic_document_builder *fygdb, unsigned int flags,
		      const char *file, int line, const char *func,
		      const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = fygdb_vdiag(fygdb, flags, file, line, func, fmt, ap);
	va_end(ap);

	return rc;
}

static void fygdb_diag_vreport(struct fy_generic_document_builder *fygdb,
			       const struct fy_diag_report_ctx *fydrc,
			       const char *fmt, va_list ap)
{
	if (!fydrc || !fmt)
		return;

	if (!fygdb || !fygdb->cfg.diag) {
		fy_token_unref(fydrc->fyt);
		return;
	}

	fy_diag_vreport(fygdb->cfg.diag, fydrc, fmt, ap);
}

static void fygdb_diag_report(struct fy_generic_document_builder *fygdb,
			      const struct fy_diag_report_ctx *fydrc,
			      const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fygdb_diag_vreport(fygdb, fydrc, fmt, ap);
	va_end(ap);
}

#define fygdb_error(_fygdb, _fmt, ...) \
	fygdb_diag((_fygdb), FYET_ERROR | FYDF_MODULE(FYEM_BUILD), __FILE__, __LINE__, __func__, \
		   (_fmt), ## __VA_ARGS__)

#define fygdb_error_check(_fygdb, _cond, _label, _fmt, ...) \
	do { \
		if (!(_cond)) { \
			fygdb_error((_fygdb), (_fmt), ## __VA_ARGS__); \
			goto _label; \
		} \
	} while (0)

#define _FYGDB_TOKEN_DIAG(_fygdb, _fyt, _type, _module, _fmt, ...) \
	do { \
		struct fy_diag_report_ctx _drc; \
		memset(&_drc, 0, sizeof(_drc)); \
		_drc.type = (_type); \
		_drc.module = (_module); \
		_drc.fyt = (_fyt); \
		fygdb_diag_report((_fygdb), &_drc, (_fmt), ## __VA_ARGS__); \
	} while (0)

#define FYGDB_TOKEN_DIAG(_fygdb, _fyt, _type, _module, _fmt, ...) \
	_FYGDB_TOKEN_DIAG((_fygdb), fy_token_ref(_fyt), (_type), (_module), (_fmt), ## __VA_ARGS__)

#define FYGDB_TOKEN_ERROR(_fygdb, _fyt, _module, _fmt, ...) \
	FYGDB_TOKEN_DIAG((_fygdb), (_fyt), FYET_ERROR, (_module), (_fmt), ## __VA_ARGS__)

#define FYGDB_TOKEN_ERROR_CHECK(_fygdb, _fyt, _module, _cond, _label, _fmt, ...) \
	do { \
		if (!(_cond)) { \
			FYGDB_TOKEN_ERROR((_fygdb), (_fyt), (_module), (_fmt), ## __VA_ARGS__); \
			goto _label; \
		} \
	} while (0)

static enum fy_parser_mode
fygdb_parser_mode_from_cfg(const struct fy_parse_cfg *cfg)
{
	unsigned int json_mode, version_mode;

	if (!cfg)
		return fypm_none;

	json_mode = (cfg->flags >> FYPCF_JSON_SHIFT) & FYPCF_JSON_MASK;
	if (json_mode == ((unsigned int)FYPCF_JSON_FORCE >> FYPCF_JSON_SHIFT))
		return fypm_json;

	version_mode = (cfg->flags >> FYPCF_DEFAULT_VERSION_SHIFT) & FYPCF_DEFAULT_VERSION_MASK;
	if (version_mode == ((unsigned int)FYPCF_DEFAULT_VERSION_1_1 >> FYPCF_DEFAULT_VERSION_SHIFT))
		return fypm_yaml_1_1;
	if (version_mode == ((unsigned int)FYPCF_DEFAULT_VERSION_1_3 >> FYPCF_DEFAULT_VERSION_SHIFT))
		return fypm_yaml_1_3;
	if (version_mode == ((unsigned int)FYPCF_DEFAULT_VERSION_1_2 >> FYPCF_DEFAULT_VERSION_SHIFT))
		return fypm_yaml_1_2;

	return fypm_none;
}

static enum fy_parser_mode
fygdb_parser_mode_from_document_state(struct fy_document_state *fyds)
{
	const struct fy_version *vers;

	if (!fyds)
		return fypm_none;

	vers = fy_document_state_version(fyds);
	if (!vers)
		return fypm_none;
	if (vers->major == 1 && vers->minor == 1)
		return fypm_yaml_1_1;
	if (vers->major == 1 && vers->minor == 3)
		return fypm_yaml_1_3;
	return fypm_yaml_1_2;
}

static void
fygdb_restore_schema(struct fy_generic_document_builder *fygdb)
{
	if (!fygdb || !fygdb->cfg.gb)
		return;
	fy_gb_set_schema(fygdb->cfg.gb, fygdb->original_schema);
}

static void
fygdb_update_schema(struct fy_generic_document_builder *fygdb)
{
	enum fy_parser_mode parser_mode;

	if (!fygdb || !fygdb->cfg.gb)
		return;

	if (fygdb->cfg.flags & FYGDBF_PYYAML_COMPAT) {
		fy_gb_set_schema(fygdb->cfg.gb, FYGS_YAML1_1_PYYAML);
		return;
	}

	if (fygdb->original_schema != FYGS_AUTO)
		return;

	parser_mode = fygdb->curr_parser_mode;
	if (parser_mode == fypm_none || parser_mode == fypm_invalid) {
		if (fygdb->fyp)
			parser_mode = fy_parser_get_mode(fygdb->fyp);
		if (parser_mode == fypm_none || parser_mode == fypm_invalid)
			parser_mode = fygdb_parser_mode_from_cfg(&fygdb->cfg.parse_cfg);
	}

	if (parser_mode != fypm_none && parser_mode != fypm_invalid)
		fy_gb_set_schema_from_parser_mode(fygdb->cfg.gb, parser_mode);
}

static void
fygdb_object_cleanup(struct fy_generic_decoder_obj *gdo)
{
	if (gdo->type == FYGDOT_INVALID)
		return;

	if (gdo->items)
		free(gdo->items);
	if (gdo->fyds)
		fy_document_state_unref(gdo->fyds);
	gdo->type = FYGDOT_INVALID;
	gdo->alloc = 0;
	gdo->count = 0;
	gdo->items = NULL;
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

static struct fy_generic_decoder_obj *
fygdb_object_alloc(struct fy_generic_document_builder *fygdb)
{
	struct fy_generic_decoder_obj *gdo;

	if (!fygdb)
		return NULL;

	gdo = fy_generic_decoder_obj_list_pop(&fygdb->recycled_gdos);
	if (!gdo) {
		gdo = malloc(sizeof(*gdo));
		if (!gdo)
			return NULL;
		memset(gdo, 0, sizeof(*gdo));
	}

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
fygdb_object_free(struct fy_generic_decoder_obj *gdo)
{
	if (!gdo)
		return;

	fygdb_object_cleanup(gdo);
	free(gdo);
}

static void
fygdb_object_recycle(struct fy_generic_document_builder *fygdb, struct fy_generic_decoder_obj *gdo)
{
	if (!gdo)
		return;

	fygdb_object_cleanup(gdo);
	if (!fygdb)
		fygdb_object_free(gdo);
	else
		fy_generic_decoder_obj_list_push(&fygdb->recycled_gdos, gdo);
}

static fy_generic
fygdb_object_finalize(struct fy_generic_document_builder *fygdb, struct fy_generic_decoder_obj *gdo)
{
	fy_generic v, vi;
	uintptr_t gi_flags;
	bool needs_indirect;

	v = fy_invalid;

	assert(fygdb);
	assert(gdo);

	switch (gdo->type) {
	case FYGDOT_ROOT:
		fygdb_error_check(fygdb, gdo->count <= 1, err_out, "bad root finalize");
		v = gdo->count == 0 ? fy_null : gdo->items[0];
		break;

	case FYGDOT_SEQUENCE:
	case FYGDOT_MAPPING: {
		struct fy_generic_op_args args = {
			.common.count = gdo->count,
			.common.items = gdo->items,
		};

		v = fy_generic_op_args(fygdb->cfg.gb,
				       (gdo->type == FYGDOT_SEQUENCE ? FYGBOPF_CREATE_SEQ : FYGBOPF_CREATE_MAP) |
					       FYGBOPF_NO_CHECKS | FYGBOPF_MAP_ITEM_COUNT,
				       fy_null, &args);
		fygdb_error_check(fygdb, fy_generic_is_valid(v), err_out,
				  "unable to create collection");

		if (fy_generic_is_valid(gdo->marker_start) &&
		    fy_generic_is_valid(gdo->marker_end)) {
			fy_generic_sequence_handle starth = fy_cast(gdo->marker_start, fy_seq_handle_null);
			fy_generic_sequence_handle endh = fy_cast(gdo->marker_end, fy_seq_handle_null);

			assert(starth);
			assert(endh);
			assert(starth->count == 6);
			assert(endh->count == 6);

			gdo->marker = fy_gb_sequence(fygdb->cfg.gb,
						     starth->items[0], starth->items[1], starth->items[2],
						     endh->items[3], endh->items[4], endh->items[5]);
		}
		break;
	}

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

		vi = fy_gb_indirect_create(fygdb->cfg.gb, &gi);
		fygdb_error_check(fygdb, fy_generic_is_valid(vi), err_out,
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
fygdb_object_finalize_and_destroy(struct fy_generic_document_builder *fygdb,
				  struct fy_generic_decoder_obj *gdo)
{
	fy_generic v;

	v = fygdb_object_finalize(fygdb, gdo);
	fygdb_object_recycle(fygdb, gdo);

	return v;
}

static inline int
fygdb_item_append(fy_generic **itemsp, size_t *countp, size_t *allocp, fy_generic v)
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
fygdb_object_mapping_on_key(struct fy_generic_decoder_obj *gdo)
{
	return gdo && gdo->type == FYGDOT_MAPPING && (gdo->count & 1) == 0;
}

static inline void
fygdb_object_mapping_expect_merge_key_value(struct fy_generic_decoder_obj *gdo)
{
	if (!gdo || gdo->type != FYGDOT_MAPPING)
		return;
	gdo->next_is_merge_args = true;
}

static inline bool
fygdb_object_mapping_on_merge_key_value(struct fy_generic_decoder_obj *gdo)
{
	return gdo && gdo->type == FYGDOT_MAPPING && gdo->next_is_merge_args;
}

static int
fygdb_object_handle_merge_key_value(struct fy_generic_document_builder *fygdb,
				    struct fy_generic_decoder_obj *gdo, fy_generic item)
{
	const fy_generic *items;
	const fy_generic_map_pair *pairs;
	fy_generic_map_pair *tmp_pairs = NULL;
	fy_generic vk;
	size_t i, j, k, l, count, map_count, total_count = 0;
	int rc;

	if (!fygdb_object_mapping_on_merge_key_value(gdo))
		return 1;

	fygdb_error_check(fygdb, gdo->next_is_merge_args, err_out, "missing merge args");
	gdo->next_is_merge_args = false;

	if (fy_generic_get_type(item) == FYGT_MAPPING) {
		pairs = fy_generic_mapping_get_pairs(item, &count);
		for (i = 0; i < count; i++) {
			rc = fygdb_item_append(&gdo->items, &gdo->count, &gdo->alloc, pairs[i].key);
			fygdb_error_check(fygdb, !rc, err_out, "fy_generic_item_append (key) failed");
			rc = fygdb_item_append(&gdo->items, &gdo->count, &gdo->alloc, pairs[i].value);
			fygdb_error_check(fygdb, !rc, err_out, "fy_generic_item_append (value) failed");
		}
		return 0;
	}

	if (fy_generic_get_type(item) != FYGT_SEQUENCE)
		return -1;

	items = fy_generic_sequence_get_items(item, &map_count);
	for (j = 0; j < map_count; j++) {
		if (fy_generic_get_type(items[j]) != FYGT_MAPPING)
			return -1;
		total_count += fy_generic_mapping_get_pair_count(items[j]);
	}

	if (total_count == 0)
		return 0;

	if (total_count <= 32) {
		tmp_pairs = alloca(sizeof(*tmp_pairs) * total_count);
	} else {
		tmp_pairs = malloc(sizeof(*tmp_pairs) * total_count);
		fygdb_error_check(fygdb, tmp_pairs, err_out, "malloc() failed");
	}

	k = 0;
	for (j = 0; j < map_count; j++) {
		pairs = fy_generic_mapping_get_pairs(items[j], &count);
		for (i = 0; i < count; i++) {
			vk = pairs[i].key;
			for (l = 0; l < k; l++) {
				if (fy_generic_compare(vk, tmp_pairs[l].key) == 0)
					break;
			}
			if (l < k)
				continue;

			assert(k < total_count);
			tmp_pairs[k++] = pairs[i];
		}
	}

	for (l = 0; l < k; l++) {
		rc = fygdb_item_append(&gdo->items, &gdo->count, &gdo->alloc, tmp_pairs[l].key);
		fygdb_error_check(fygdb, !rc, err_out, "fy_generic_item_append() (key) failed");
		rc = fygdb_item_append(&gdo->items, &gdo->count, &gdo->alloc, tmp_pairs[l].value);
		fygdb_error_check(fygdb, !rc, err_out, "fy_generic_item_append() (value) failed");
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
fygdb_object_add_item(struct fy_generic_decoder_obj *gdo, fy_generic item)
{
	return fygdb_item_append(&gdo->items, &gdo->count, &gdo->alloc, item);
}

static fy_generic
fygdb_create_scalar(struct fy_generic_document_builder *fygdb, struct fy_event *fye,
		    fy_generic va, fy_generic vt, fy_generic vcomment,
		    fy_generic vstyle, fy_generic vfailsafe_str,
		    fy_generic vmarker,
		    bool *is_empty_plain_scalarp)
{
	const char *tag, *p, *sfx;
	const char *yaml_tag_pfx = "tag:yaml.org,2002";
	const size_t yaml_tag_pfx_size = strlen(yaml_tag_pfx);
	enum fy_generic_type force_type = FYGT_INVALID;
	struct fy_token *fyt;
	enum fy_scalar_style style;
	uintptr_t gi_flags;
	bool needs_indirect;
	const char *text;
	size_t len;
	fy_generic v, vi;

	assert(fygdb);
	assert(fye);
	assert(fye->type == FYET_SCALAR);

	fyt = fy_event_get_token(fye);
	fygdb_error_check(fygdb, fyt, err_out, "fy_event_get_token() failed");

	text = fy_token_get_text(fyt, &len);
	fygdb_error_check(fygdb, text, err_out, "fy_token_get_text() failed");

	v = fy_invalid;
	style = fy_token_scalar_style(fyt);

	if (fy_generic_is_invalid(vt)) {
		if (style != FYSS_PLAIN) {
			v = fy_gb_string_size_create(fygdb->cfg.gb, text, len);
			fygdb_error_check(fygdb, !fy_generic_is_invalid(v), err_out,
					  "invalid scalar created");
		} else {
			v = fy_gb_create_scalar_from_text(fygdb->cfg.gb, text, len, FYGT_INVALID);
			fygdb_error_check(fygdb, !fy_generic_is_invalid(v), err_out,
					  "invalid scalar created");
		}
	} else {
		bool coerce_explicit_tag = true;

		force_type = FYGT_STRING;
		switch (fy_gb_get_schema(fygdb->cfg.gb)) {
		case FYGS_YAML1_2_FAILSAFE:
		case FYGS_YAML1_1_FAILSAFE:
			coerce_explicit_tag = false;
			break;
		default:
			break;
		}

		tag = fy_castp(&vt, "");
		fygdb_error_check(fygdb, tag[0], err_out, "fy_cast() failed");

		sfx = NULL;
		p = strrchr(tag, ':');
		if (!(p && (size_t)(p - tag) == yaml_tag_pfx_size &&
		      !memcmp(tag, yaml_tag_pfx, yaml_tag_pfx_size)))
			goto create_scalar;
		if (!coerce_explicit_tag)
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
		v = fy_gb_create_scalar_from_text(fygdb->cfg.gb, text, len, force_type);
		FYGDB_TOKEN_ERROR_CHECK(fygdb, fy_event_get_token(fye), FYEM_PARSE,
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
			.failsafe_str = vfailsafe_str,
		};

		vi = fy_gb_indirect_create(fygdb->cfg.gb, &gi);
		fygdb_error_check(fygdb, fy_generic_is_valid(vi), err_out,
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
fygdb_is_merge_key(struct fy_generic_document_builder *fygdb,
		   struct fy_generic_decoder_obj *parent, struct fy_event *fye)
{
	return fygdb && parent && fye &&
		fye->type == FYET_SCALAR &&
		fygdb->gdo_root && fygdb->resolve && fygdb->gdo_root->supports_merge_key &&
		fygdb_object_mapping_on_key(parent) &&
		fy_atom_is_merge_key(fy_token_atom(fye->scalar.value));
}

static bool
fygdb_is_valid_merge_key_arg(struct fy_generic_document_builder *fygdb FY_UNUSED,
			     struct fy_generic_decoder_obj *parent FY_UNUSED,
			     fy_generic v)
{
	enum fy_generic_type type;
	const fy_generic *items;
	size_t i, count;

	if (!fygdb || !fygdb->resolve)
		return false;

	type = fy_generic_get_type(v);
	if (type == FYGT_MAPPING)
		return true;
	if (type != FYGT_SEQUENCE)
		return false;

	items = fy_generic_sequence_get_items(v, &count);
	for (i = 0; i < count; i++) {
		if (fy_generic_get_type(items[i]) != FYGT_MAPPING)
			return false;
	}

	return true;
}

static fy_generic
fygdb_strip_non_content(struct fy_generic_document_builder *fygdb, fy_generic v)
{
	fy_generic_indirect gi;

	if (fy_generic_is_direct(v))
		return v;

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

	return fy_gb_indirect_create(fygdb->cfg.gb, &gi);
}

static int
fygdb_anchor_register(struct fy_generic_document_builder *fygdb, fy_generic anchor, fy_generic content)
{
	struct fy_generic_anchor *ga = NULL;

	fygdb_error_check(fygdb, fy_generic_is_string(anchor), err_out, "anchor is not a string");

	ga = malloc(sizeof(*ga));
	fygdb_error_check(fygdb, ga, err_out, "malloc() failed");
	memset(ga, 0, sizeof(*ga));

	ga->anchor = anchor;
	if (fy_generic_is_invalid(content)) {
		ga->content = fy_invalid;
		fy_generic_anchor_list_add(&fygdb->collecting_anchors, ga);
	} else {
		ga->content = fygdb_strip_non_content(fygdb, content);
		fy_generic_anchor_list_add(&fygdb->complete_anchors, ga);
	}

	return 0;

err_out:
	return -1;
}

static fy_generic
fygdb_alias_resolve(struct fy_generic_document_builder *fygdb, fy_generic anchor)
{
	struct fy_generic_anchor *ga;

	for (ga = fy_generic_anchor_list_head(&fygdb->complete_anchors); ga;
	     ga = fy_generic_anchor_next(&fygdb->complete_anchors, ga)) {
		if (!fy_generic_compare(ga->anchor, anchor))
			return ga->content;
	}
	return fy_invalid;
}

static bool
fygdb_alias_is_collecting(struct fy_generic_document_builder *fygdb, fy_generic anchor)
{
	struct fy_generic_anchor *ga;

	for (ga = fy_generic_anchor_list_head(&fygdb->complete_anchors); ga;
	     ga = fy_generic_anchor_next(&fygdb->complete_anchors, ga)) {
		if (!fy_generic_compare(ga->anchor, anchor))
			return true;
	}
	return false;
}

static void
fygdb_anchor_collection_starts(struct fy_generic_document_builder *fygdb)
{
	struct fy_generic_anchor *ga;

	for (ga = fy_generic_anchor_list_head(&fygdb->collecting_anchors); ga;
	     ga = fy_generic_anchor_next(&fygdb->collecting_anchors, ga))
		ga->nest++;
}

static void
fygdb_anchor_collection_ends(struct fy_generic_document_builder *fygdb, fy_generic v)
{
	struct fy_generic_anchor *ga, *gan;

	for (ga = fy_generic_anchor_list_head(&fygdb->collecting_anchors); ga; ga = gan) {
		gan = fy_generic_anchor_next(&fygdb->collecting_anchors, ga);

		assert(ga->nest > 0);
		ga->nest--;
		if (ga->nest > 0)
			continue;

		assert(fy_generic_is_invalid(ga->content));

		fy_generic_anchor_list_del(&fygdb->collecting_anchors, ga);
		ga->content = fygdb_strip_non_content(fygdb, v);
		fy_generic_anchor_list_add(&fygdb->complete_anchors, ga);
	}
}

static int
fygdb_stack_push(struct fy_generic_document_builder *fygdb, struct fy_generic_decoder_obj *gdo,
		 struct fy_token *fyt)
{
	struct fy_generic_decoder_obj **new_stack;

	FYGDB_TOKEN_ERROR_CHECK(fygdb, fyt, FYEM_BUILD,
				!fygdb->max_depth || fygdb->stack_top < fygdb->max_depth,
				err_out, "Max depth (%d) exceeded", fygdb->stack_top);

	if (fygdb->stack_top >= fygdb->stack_alloc) {
		new_stack = realloc(fygdb->stack, fygdb->stack_alloc * 2 * sizeof(*new_stack));
		fygdb_error_check(fygdb, new_stack, err_out, "Unable to grow the context stack");
		fygdb->stack_alloc *= 2;
		fygdb->stack = new_stack;
	}

	assert(fygdb->stack_top < fygdb->stack_alloc);
	fygdb->stack[fygdb->stack_top++] = gdo;
	return 0;

err_out:
	return -1;
}

static struct fy_generic_decoder_obj *
fygdb_stack_top(const struct fy_generic_document_builder *fygdb)
{
	return fygdb && fygdb->stack_top ? fygdb->stack[fygdb->stack_top - 1] : NULL;
}

static fy_generic
fygdb_materialize_result(struct fy_generic_document_builder *fygdb, fy_generic root, struct fy_document_state *fyds)
{
	fy_generic v;

	if (fygdb->cfg.flags & FYGDBF_DISABLE_DIRECTORY)
		return root;

	v = fy_generic_vds_create_from_document_state(fygdb->cfg.gb, root, fyds);
	if (fy_generic_is_invalid(v))
		return fy_invalid;
	return fy_gb_internalize(fygdb->cfg.gb, v);
}

static int
fygdb_finish_document(struct fy_generic_document_builder *fygdb)
{
	fy_generic root, doc;

	if (!fygdb || !fygdb->gdo_root)
		return -1;

	root = fygdb_object_finalize(fygdb, fygdb->gdo_root);
	fygdb_error_check(fygdb, fy_generic_is_valid(root), err_out,
			  "fy_generic_document_builder root finalize failed");

	doc = fygdb_materialize_result(fygdb, root, fygdb->gdo_root->fyds);
	fygdb_error_check(fygdb, fy_generic_is_valid(doc), err_out,
			  "failed to materialize generic document");

	fygdb->document = doc;
	fygdb->doc_done = true;
	fygdb_object_recycle(fygdb, fygdb->gdo_root);
	fygdb->gdo_root = NULL;
	fygdb->stack_top = 0;
	fygdb_restore_schema(fygdb);

	return 0;

err_out:
	fygdb_restore_schema(fygdb);
	return -1;
}

static int
fygdb_maybe_finish_single_document(struct fy_generic_document_builder *fygdb)
{
	if (!fygdb || !fygdb->single_mode || !fygdb->gdo_root)
		return 0;
	if (fygdb->stack_top != 1)
		return 0;
	if (fygdb->gdo_root->count != 1)
		return 0;
	return fygdb_finish_document(fygdb);
}

static const struct fy_generic_document_builder_cfg fygdb_default_cfg = {
	.parse_cfg = {
		.flags = FYPCF_DEFAULT_PARSE,
	},
	.flags = FYGDBF_DEFAULT,
};

struct fy_generic_document_builder *
fy_generic_document_builder_create(const struct fy_generic_document_builder_cfg *cfg)
{
	struct fy_generic_document_builder *fygdb = NULL;

	if (!cfg)
		cfg = &fygdb_default_cfg;
	if (!cfg->gb)
		return NULL;

	fygdb = malloc(sizeof(*fygdb));
	if (!fygdb)
		goto err_out;

	memset(fygdb, 0, sizeof(*fygdb));
	fygdb->cfg = *cfg;
	fygdb->stack_alloc = fy_depth_limit();
	fygdb->max_depth = (cfg->parse_cfg.flags & FYPCF_DISABLE_DEPTH_LIMIT) ? 0 : fy_depth_limit();
	fygdb->resolve = !!(cfg->parse_cfg.flags & FYPCF_RESOLVE_DOCUMENT);
	fygdb->document = fy_invalid;
	fygdb->original_schema = fy_gb_get_schema(cfg->gb);
	fygdb->curr_parser_mode = fygdb_parser_mode_from_cfg(&cfg->parse_cfg);

	fygdb->stack = malloc(fygdb->stack_alloc * sizeof(*fygdb->stack));
	if (!fygdb->stack)
		goto err_out;

	fy_generic_decoder_obj_list_init(&fygdb->recycled_gdos);
	fy_generic_anchor_list_init(&fygdb->complete_anchors);
	fy_generic_anchor_list_init(&fygdb->collecting_anchors);

	return fygdb;

err_out:
	if (fygdb) {
		if (fygdb->stack)
			free(fygdb->stack);
		free(fygdb);
	}
	return NULL;
}

struct fy_generic_document_builder *
fy_generic_document_builder_create_on_parser(struct fy_parser *fyp, struct fy_generic_builder *gb)
{
	struct fy_generic_document_builder_cfg cfg;
	struct fy_generic_document_builder *fygdb;
	struct fy_document_state *fyds;
	int rc;

	if (!fyp || !gb)
		return NULL;

	memset(&cfg, 0, sizeof(cfg));
	cfg.parse_cfg = fyp->cfg;
	cfg.diag = fy_diag_ref(fyp->diag);
	cfg.gb = gb;
	cfg.flags = FYGDBF_DEFAULT;

	if (cfg.parse_cfg.flags & FYPCF_KEEP_COMMENTS)
		cfg.flags |= FYGDBF_KEEP_COMMENTS;
	if (cfg.parse_cfg.flags & FYPCF_CREATE_MARKERS)
		cfg.flags |= FYGDBF_CREATE_MARKERS;
	if (cfg.parse_cfg.flags & FYPCF_KEEP_STYLE)
		cfg.flags |= FYGDBF_KEEP_STYLE;

	fygdb = fy_generic_document_builder_create(&cfg);
	if (!fygdb) {
		fy_diag_unref(cfg.diag);
		return NULL;
	}

	fygdb->fyp = fyp;
	fygdb->curr_parser_mode = fy_parser_get_mode(fyp);

	fyds = fy_parser_get_document_state(fyp);
	if (fyds) {
		rc = fy_generic_document_builder_set_in_document(fygdb, fyds, true);
		if (rc) {
			fy_generic_document_builder_destroy(fygdb);
			return NULL;
		}
	}

	return fygdb;
}

void
fy_generic_document_builder_reset(struct fy_generic_document_builder *fygdb)
{
	struct fy_generic_anchor *ga;
	struct fy_generic_decoder_obj *gdo;

	if (!fygdb)
		return;

	while (fygdb->stack_top > 0) {
		gdo = fygdb->stack[--fygdb->stack_top];
		fygdb_object_recycle(fygdb, gdo);
	}
	fygdb->gdo_root = NULL;

	while ((ga = fy_generic_anchor_list_pop(&fygdb->collecting_anchors)) != NULL)
		free(ga);
	while ((ga = fy_generic_anchor_list_pop(&fygdb->complete_anchors)) != NULL)
		free(ga);

	fygdb->document = fy_invalid;
	fygdb->single_mode = false;
	fygdb->in_stream = false;
	fygdb->doc_done = false;
	fygdb->curr_parser_mode = fygdb->fyp ? fy_parser_get_mode(fygdb->fyp) :
					    fygdb_parser_mode_from_cfg(&fygdb->cfg.parse_cfg);

	fygdb_restore_schema(fygdb);
}

void
fy_generic_document_builder_destroy(struct fy_generic_document_builder *fygdb)
{
	struct fy_generic_decoder_obj *gdo;

	if (!fygdb)
		return;

	fy_generic_document_builder_reset(fygdb);
	while ((gdo = fy_generic_decoder_obj_list_pop(&fygdb->recycled_gdos)) != NULL)
		fygdb_object_free(gdo);

	fy_diag_unref(fygdb->cfg.diag);
	if (fygdb->stack)
		free(fygdb->stack);
	free(fygdb);
}

fy_generic
fy_generic_document_builder_peek_document(struct fy_generic_document_builder *fygdb)
{
	if (!fygdb)
		return fy_invalid;

	if (fygdb->doc_done)
		return fygdb->document;
	if (!fygdb->gdo_root || fygdb->stack_top != 1 || fygdb->gdo_root->count != 1)
		return fy_invalid;

	return fygdb_materialize_result(fygdb, fygdb->gdo_root->items[0], fygdb->gdo_root->fyds);
}

fy_generic
fy_generic_document_builder_get_document(struct fy_generic_document_builder *fygdb)
{
	return fy_generic_document_builder_peek_document(fygdb);
}

bool
fy_generic_document_builder_is_in_stream(struct fy_generic_document_builder *fygdb)
{
	return fygdb && fygdb->in_stream;
}

bool
fy_generic_document_builder_is_in_document(struct fy_generic_document_builder *fygdb)
{
	return fygdb && fygdb->gdo_root != NULL && !fygdb->doc_done;
}

bool
fy_generic_document_builder_is_document_complete(struct fy_generic_document_builder *fygdb)
{
	return fygdb && fygdb->doc_done;
}

fy_generic
fy_generic_document_builder_take_document(struct fy_generic_document_builder *fygdb)
{
	fy_generic v;

	if (!fy_generic_document_builder_is_document_complete(fygdb))
		return fy_invalid;

	v = fygdb->document;
	fygdb->document = fy_invalid;
	fygdb->doc_done = false;
	return v;
}

void
fy_generic_document_builder_set_in_stream(struct fy_generic_document_builder *fygdb)
{
	if (!fygdb)
		return;

	fy_generic_document_builder_reset(fygdb);
	fygdb->in_stream = true;
}

int
fy_generic_document_builder_set_in_document(struct fy_generic_document_builder *fygdb,
					    struct fy_document_state *fyds, bool single)
{
	struct fy_generic_decoder_obj *gdo;
	const struct fy_version *vers;
	int rc;

	if (!fygdb)
		return -1;

	fy_generic_document_builder_reset(fygdb);
	fygdb->in_stream = true;
	fygdb->single_mode = single;
	fygdb->document = fy_invalid;

	gdo = fygdb_object_alloc(fygdb);
	if (!gdo)
		return -1;

	gdo->type = FYGDOT_ROOT;
	gdo->fyds = fy_document_state_ref(fyds);
	vers = gdo->fyds ? fy_document_state_version(gdo->fyds) : NULL;
	gdo->supports_merge_key = vers && vers->major == 1 && vers->minor == 1;

	fygdb->gdo_root = gdo;
	fygdb->curr_parser_mode = fyds ? fygdb_parser_mode_from_document_state(fyds) :
					 fygdb_parser_mode_from_cfg(&fygdb->cfg.parse_cfg);
	fygdb_update_schema(fygdb);

	rc = fygdb_stack_push(fygdb, gdo, NULL);
	if (rc) {
		fygdb_object_recycle(fygdb, gdo);
		fygdb->gdo_root = NULL;
		return -1;
	}

	return 0;
}

int
fy_generic_document_builder_process_event(struct fy_generic_document_builder *fygdb,
					  struct fy_event *fye)
{
	struct fy_generic_builder *gb;
	struct fy_generic_decoder_obj *gdo = NULL, *parent;
	struct fy_token *fyt, *fyt_anchor, *fyt_tag;
	const struct fy_version *vers;
	const char *anchor, *tag, *comment;
	size_t anchor_size, tag_size;
	enum fy_scalar_style ss;
	enum fy_collection_style cs;
	fy_generic v, va, vt, vcomment, vstyle, vfailsafe_str, vmarker;
	const struct fy_mark *start_mark, *end_mark;
	bool is_empty_plain_scalar = false;
	int rc;

	if (!fygdb || !fye)
		return -1;

	gb = fygdb->cfg.gb;
	fyt = fy_event_get_token(fye);

	if (fygdb->cfg.flags & FYGDBF_TRACE)
		fprintf(stderr, "%s depth=%u doc=%u\n",
			fy_event_type_get_text(fye->type), fygdb->stack_top,
			fygdb->gdo_root ? 1U : 0U);

	fyt_anchor = fy_event_get_anchor_token(fye);
	if (fyt_anchor) {
		anchor = fy_token_get_text(fyt_anchor, &anchor_size);
		fygdb_error_check(fygdb, anchor, err_out, "fy_token_get_text() failed");
		va = fy_gb_string_size_create(gb, anchor, anchor_size);
		fygdb_error_check(fygdb, fy_generic_is_valid(va), err_out,
				  "fy_generic_string_size_create() failed");
	} else {
		anchor = NULL;
		anchor_size = 0;
		va = fy_invalid;
	}

	fyt_tag = fy_event_get_tag_token(fye);
	if (fyt_tag) {
		tag = fy_token_get_text(fyt_tag, &tag_size);
		fygdb_error_check(fygdb, tag, err_out, "fy_token_get_text() failed");
		vt = fy_gb_string_size_create(gb, tag, tag_size);
		fygdb_error_check(fygdb, fy_generic_is_valid(vt), err_out,
				  "fy_generic_string_size_create() failed");
	} else {
		tag = NULL;
		tag_size = 0;
		vt = fy_invalid;
	}

	if ((fygdb->cfg.flags & FYGDBF_KEEP_COMMENTS) && fyt && fy_token_has_any_comment(fyt)) {
		comment = fy_token_get_comments(fyt);
		fygdb_error_check(fygdb, comment, err_out, "fy_token_get_comments() failed");
		vcomment = fy_gb_string_create(gb, comment);
		fygdb_error_check(fygdb, fy_generic_is_valid(vcomment), err_out,
				  "fy_generic_string_create() failed");
	} else {
		comment = NULL;
		vcomment = fy_invalid;
	}

	vstyle = fy_invalid;
	if (fygdb->cfg.flags & FYGDBF_KEEP_STYLE) {
		if (fye->type == FYET_SCALAR) {
			ss = fy_token_scalar_style(fyt);
			if (ss != FYSS_ANY)
				vstyle.v = fy_generic_in_place_unsigned_int((unsigned int)ss);
		} else if (fye->type == FYET_SEQUENCE_START || fye->type == FYET_MAPPING_START) {
			cs = fy_token_collection_style(fyt);
			if (cs != FYCS_ANY)
				vstyle.v = fy_generic_in_place_unsigned_int((unsigned int)cs);
		}
	}

	vfailsafe_str = fy_invalid;
	if (fygdb->cfg.flags & FYGDBF_KEEP_FAILSAFE_STR)
		vfailsafe_str = fy_invalid;

	vmarker = fy_invalid;
	if ((fygdb->cfg.flags & FYGDBF_CREATE_MARKERS) &&
	    (start_mark = fy_event_style_start_mark(fye)) != NULL &&
	    (end_mark = fy_event_style_end_mark(fye)) != NULL) {
		vmarker = fy_gb_sequence(gb,
					 start_mark->input_pos, start_mark->line, start_mark->column,
					 end_mark->input_pos, end_mark->line, end_mark->column);
	}

	switch (fye->type) {
	case FYET_STREAM_START:
		FYGDB_TOKEN_ERROR_CHECK(fygdb, fyt, FYEM_BUILD, !fygdb->in_stream, err_out,
					"STREAM_START while already in stream");
		fygdb->in_stream = true;
		return 0;

	case FYET_STREAM_END:
		FYGDB_TOKEN_ERROR_CHECK(fygdb, fyt, FYEM_BUILD, fygdb->in_stream, err_out,
					"STREAM_END while not in stream");
		fygdb->in_stream = false;
		return 1;

	case FYET_DOCUMENT_START:
		FYGDB_TOKEN_ERROR_CHECK(fygdb, fyt, FYEM_BUILD, fygdb->in_stream, err_out,
					"DOCUMENT_START while not in stream");
		FYGDB_TOKEN_ERROR_CHECK(fygdb, fyt, FYEM_BUILD, !fygdb->gdo_root && !fygdb->doc_done,
					err_out, "DOCUMENT_START while already in document");

		gdo = fygdb_object_alloc(fygdb);
		fygdb_error_check(fygdb, gdo, err_out, "fy_generic_decoder_object_alloc() failed");
		gdo->type = FYGDOT_ROOT;
		gdo->fyds = fy_document_state_ref(fye->document_start.document_state);
		vers = gdo->fyds ? fy_document_state_version(gdo->fyds) : NULL;
		gdo->supports_merge_key = vers && vers->major == 1 && vers->minor == 1;

		rc = fygdb_stack_push(fygdb, gdo, fyt);
		fygdb_error_check(fygdb, !rc, err_out, "fygdb_stack_push() failed");

		fygdb->gdo_root = gdo;
		gdo = NULL;
		fygdb->document = fy_invalid;
		fygdb->doc_done = false;
		fygdb->single_mode = false;
		fygdb->curr_parser_mode = fygdb->fyp ? fy_parser_get_mode(fygdb->fyp) :
						       fygdb_parser_mode_from_document_state(fygdb->gdo_root->fyds);
		if (fygdb->curr_parser_mode == fypm_none || fygdb->curr_parser_mode == fypm_invalid)
			fygdb->curr_parser_mode = fygdb_parser_mode_from_cfg(&fygdb->cfg.parse_cfg);
		fygdb_update_schema(fygdb);

		return 0;

	case FYET_DOCUMENT_END:
		FYGDB_TOKEN_ERROR_CHECK(fygdb, fyt, FYEM_BUILD, fygdb->in_stream, err_out,
					"DOCUMENT_END while not in stream");
		if (fygdb->doc_done && !fygdb->gdo_root)
			return 1;
		FYGDB_TOKEN_ERROR_CHECK(fygdb, fyt, FYEM_BUILD,
					fygdb->gdo_root && fygdb->stack_top == 1, err_out,
					"DOCUMENT_END with incomplete document");
		rc = fygdb_finish_document(fygdb);
		if (rc)
			goto err_out;
		return 1;

	case FYET_ALIAS:
		parent = fygdb_stack_top(fygdb);
		FYGDB_TOKEN_ERROR_CHECK(fygdb, fyt, FYEM_BUILD, parent, err_out,
					"ALIAS outside document");

		anchor = fy_token_get_text(fyt, &anchor_size);
		fygdb_error_check(fygdb, anchor, err_out, "fy_token_get_text() failed");

		if (fygdb->resolve) {
			v = fygdb_alias_resolve(fygdb, fy_string_size(anchor, anchor_size));
			if (fy_generic_is_invalid(v)) {
				FYGDB_TOKEN_ERROR(fygdb, fyt, FYEM_BUILD,
						  !fygdb_alias_is_collecting(fygdb, va) ?
							  "Unable to resolve alias" :
							  "Recursive reference to alias");
				goto err_out;
			}
		} else {
			v = fy_gb_alias_create(gb, fy_gb_string_size_create(gb, anchor, anchor_size));
			fygdb_error_check(fygdb, fy_generic_is_valid(v), err_out,
					  "fy_generic_alias_create() failed");
		}
		anchor = NULL;
		break;

	case FYET_SCALAR:
		parent = fygdb_stack_top(fygdb);
		FYGDB_TOKEN_ERROR_CHECK(fygdb, fyt, FYEM_BUILD, parent, err_out,
					"SCALAR outside document");

		if (fygdb_is_merge_key(fygdb, parent, fye)) {
			fygdb_object_mapping_expect_merge_key_value(parent);
			return 0;
		}

		v = fygdb_create_scalar(fygdb, fye,
					fygdb->resolve ? fy_invalid : va,
					vt, vcomment, vstyle,
					vfailsafe_str, vmarker, &is_empty_plain_scalar);
		fygdb_error_check(fygdb, fy_generic_is_valid(v), err_out,
				  "fy_generic_document_builder scalar create failed");
		break;

	case FYET_SEQUENCE_START:
	case FYET_MAPPING_START:
		parent = fygdb_stack_top(fygdb);
		FYGDB_TOKEN_ERROR_CHECK(fygdb, fyt, FYEM_BUILD, parent, err_out,
					"collection start outside document");

		if (fygdb->resolve && anchor) {
			rc = fygdb_anchor_register(fygdb, va, fy_invalid);
			fygdb_error_check(fygdb, !rc, err_out, "fygdb_anchor_register() failed");

			fygdb_anchor_collection_starts(fygdb);
		}

		gdo = fygdb_object_alloc(fygdb);
		fygdb_error_check(fygdb, gdo, err_out, "fy_generic_decoder_object_alloc() failed");

		gdo->type = fye->type == FYET_SEQUENCE_START ? FYGDOT_SEQUENCE : FYGDOT_MAPPING;
		gdo->anchor = fygdb->resolve ? fy_invalid : va;
		gdo->tag = vt;
		gdo->comment = vcomment;
		gdo->style = vstyle;
		gdo->failsafe_str = vfailsafe_str;
		gdo->marker_start = vmarker;

		rc = fygdb_stack_push(fygdb, gdo, fyt);
		fygdb_error_check(fygdb, !rc, err_out, "fygdb_stack_push() failed");

		gdo = NULL;
		return 0;

	case FYET_SEQUENCE_END:
	case FYET_MAPPING_END:
		FYGDB_TOKEN_ERROR_CHECK(fygdb, fyt, FYEM_BUILD, fygdb->stack_top > 1, err_out,
					"unexpected collection end");
		gdo = fygdb_stack_top(fygdb);
		FYGDB_TOKEN_ERROR_CHECK(fygdb, fyt, FYEM_BUILD,
					gdo && ((fye->type == FYET_SEQUENCE_END && gdo->type == FYGDOT_SEQUENCE) ||
						(fye->type == FYET_MAPPING_END && gdo->type == FYGDOT_MAPPING)),
					err_out, "collection end type mismatch");
		fygdb->stack_top--;
		parent = fygdb_stack_top(fygdb);
		FYGDB_TOKEN_ERROR_CHECK(fygdb, fyt, FYEM_BUILD, parent, err_out,
					"missing parent container");

		gdo->marker_end = vmarker;
		v = fygdb_object_finalize_and_destroy(fygdb, gdo);
		fygdb_error_check(fygdb, fy_generic_is_valid(v), err_out,
				  "fy_generic_document_builder finalize failed");
		gdo = NULL;
		is_empty_plain_scalar = false;
		if (fygdb->resolve)
			fygdb_anchor_collection_ends(fygdb, v);
		break;

	default:
		FYGDB_TOKEN_ERROR(fygdb, fyt, FYEM_BUILD, "Unexpected event %s in build mode",
				  fy_event_type_txt[fye->type]);
		goto err_out;
	}

	parent = fygdb_stack_top(fygdb);
	FYGDB_TOKEN_ERROR_CHECK(fygdb, fyt, FYEM_BUILD, parent, err_out, "missing parent");

	if (fygdb->resolve && anchor) {
		rc = fygdb_anchor_register(fygdb, va, v);
		fygdb_error_check(fygdb, !rc, err_out, "fy_generic_document_builder anchor register failed");
	}

	if (fy_gb_get_schema(fygdb->cfg.gb) == FYGS_YAML1_1_PYYAML &&
	    parent->type == FYGDOT_MAPPING) {
		if (!(parent->count & 1)) {
			parent->last_key_was_empty_plain_scalar = is_empty_plain_scalar;
		} else {
			if (parent->last_key_was_empty_plain_scalar && is_empty_plain_scalar) {
				FYGDB_TOKEN_ERROR(fygdb, fyt, FYEM_BUILD,
						  "bare : detected (illegal in pyyaml mode)");
				goto err_out;
			}
			parent->last_key_was_empty_plain_scalar = false;
		}
	}

	if (fygdb_object_mapping_on_merge_key_value(parent)) {
		if (!fygdb_is_valid_merge_key_arg(fygdb, parent, v)) {
			FYGDB_TOKEN_ERROR(fygdb, fyt, FYEM_BUILD,
					  "Invalid merge key argument: must be a mapping or a sequence of mappings");
			goto err_out;
		}
		rc = fygdb_object_handle_merge_key_value(fygdb, parent, v);
		fygdb_error_check(fygdb, !rc, err_out,
				  "fy_generic_document_builder merge key handling failed");
	} else {
		rc = fygdb_object_add_item(parent, v);
		fygdb_error_check(fygdb, !rc, err_out,
				  "fy_generic_document_builder append failed");
	}

	if (fygdb_maybe_finish_single_document(fygdb))
		goto err_out;

	return fygdb->doc_done ? 1 : 0;

err_out:
	fygdb_object_recycle(fygdb, gdo);
	return -1;
}

fy_generic
fy_generic_document_builder_load_document(struct fy_generic_document_builder *fygdb,
					  struct fy_parser *fyp)
{
	struct fy_eventp *fyep = NULL;
	int rc;

	if (!fygdb || !fyp)
		return fy_invalid;
	if (fyp->state == FYPS_END)
		return fy_invalid;

	fygdb->fyp = fyp;
	while (!fy_generic_document_builder_is_document_complete(fygdb) &&
	       (fyep = fy_parse_private(fyp)) != NULL) {
		fygdb->curr_parser_mode = fy_parser_get_mode(fyp);
		rc = fy_generic_document_builder_process_event(fygdb, &fyep->e);
		fy_parse_eventp_recycle(fyp, fyep);
		if (rc < 0) {
			fyp->stream_error = true;
			return fy_invalid;
		}
	}

	return fy_generic_document_builder_take_document(fygdb);
}
