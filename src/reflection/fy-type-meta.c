/*
 * fy-type-meta.c - Reflection meta/annotation system implementation
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

#include "fy-doc.h"

static int
reflection_meta_set_bool(struct reflection_meta *rm,
			 enum reflection_meta_value_id id, bool v)
{
	unsigned int idx;

	if (!rm || !reflection_meta_value_id_is_bool(id))
		return -1;

	idx = id - rmvid_first_bool;
	if (v)
		rm->bools[idx / 8] |= FY_BIT(idx & 7);
	else
		rm->bools[idx / 8] &= ~FY_BIT(idx & 7);

	/* mark the value as explicitly set */
	if (!reflection_meta_value_get_explicit(rm, id)) {
		reflection_meta_value_set_explicit(rm, id, true);
		rm->explicit_count++;
	}

	return 0;
}

static int
reflection_meta_set_str(struct reflection_meta *rm,
			enum reflection_meta_value_id id, const char *str)
{
	unsigned int idx;
	char *new_str;

	if (!rm || !str || !reflection_meta_value_id_is_str(id))
		return -1;

	idx = id - rmvid_first_str;

	new_str = strdup(str);
	if (!new_str)
		return -1;

	if (rm->strs[idx])
		free(rm->strs[idx]);
	rm->strs[idx] = new_str;

	if (!reflection_meta_value_get_explicit(rm, id)) {
		reflection_meta_value_set_explicit(rm, id, true);
		rm->explicit_count++;
	}

	return 0;
}

int
reflection_meta_set_any_value(struct reflection_meta *rm,
			      enum reflection_meta_value_id id,
			      struct reflection_any_value *rav, bool copy)
{
	struct reflection_any_value *new_rav;
	unsigned int idx;

	if (!rm || !rav || !reflection_meta_value_id_is_any(id))
		return -1;

	idx = id - rmvid_first_any;

	if (copy) {
		new_rav = reflection_any_value_copy(rav);
		if (!new_rav)
			return -1;
	} else {
		new_rav = rav;
	}

	if (rm->anys[idx])
		reflection_any_value_destroy(rm->anys[idx]);
	rm->anys[idx] = new_rav;

	if (!reflection_meta_value_get_explicit(rm, id)) {
		reflection_meta_value_set_explicit(rm, id, true);
		rm->explicit_count++;
	}

	return 0;
}

struct reflection_any_value *
reflection_any_value_create(struct reflection_type_system *rts,
			    struct fy_node *fyn)
{
	struct reflection_any_value *rav;

	assert(rts);
	if (!fyn)
		return NULL;

	rav = malloc(sizeof(*rav));
	RTS_ASSERT(rav);
	memset(rav, 0, sizeof(*rav));

	rav->rts = rts;
	rav->fyd = fy_document_create(NULL);
	RTS_ASSERT(rav->fyd);

	rav->fyn = fy_node_copy(rav->fyd, fyn);
	RTS_ASSERT(rav->fyn);

	return rav;

err_out:
	reflection_any_value_destroy(rav);
	return NULL;
}

void
reflection_any_value_destroy(struct reflection_any_value *rav)
{
	if (!rav)
		return;

	if (rav->value && rav->rtd) {
		reflection_type_data_free(rav->rtd, rav->value);
		rav->value = NULL;
		rav->rtd = NULL;
	}
	assert(!rav->value);

	if (rav->str)
		free(rav->str);

	fy_node_free(rav->fyn);
	fy_document_destroy(rav->fyd);

	free(rav);
}

const char *
reflection_any_value_get_str(struct reflection_any_value *rav)
{
	if (!rav || !rav->fyn)
		return NULL;

	if (!rav->str)
		rav->str = fy_emit_node_to_string(rav->fyn,
				FYECF_WIDTH_INF | FYECF_INDENT_DEFAULT |
				FYECF_MODE_FLOW_ONELINE | FYECF_NO_ENDING_NEWLINE);

	return rav->str;
}

struct reflection_any_value *
reflection_any_value_copy(struct reflection_any_value *rav_src)
{
	if (!rav_src)
		return NULL;

	return reflection_any_value_create(rav_src->rts, rav_src->fyn);
}

void *
reflection_any_value_generate(struct reflection_any_value *rav,
			      struct reflection_type_data *rtd)
{
	struct reflection_type_system *rts;

	if (!rav)
		return NULL;

	rts = rav->rts;
	assert(rts);

	/* if we generated for this rtd before, return it */
	if (rtd && rav->rtd == rtd)
		return rav->value;

	if (rav->value && rav->rtd) {
		reflection_type_data_free(rav->rtd, rav->value);
		rav->value = NULL;
		rav->rtd = NULL;
	}
	assert(!rav->value);

	if (!rtd)
		return NULL;

	rav->value = reflection_type_data_generate_value(rtd, rav->fyn);
	RTS_ASSERT(rav->value);

	rav->rtd = rtd;

	return rav->value;

err_out:
	return NULL;
}

int
reflection_any_value_equal_rw(struct reflection_any_value *rav,
			      struct reflection_walker *rw)
{
	void *data;
	int rc;

	if (!rav || !rw)
		return -1;

	data = reflection_any_value_generate(rav, rw->rtd);
	if (!data)
		return -1;

	rc = reflection_eq_rw(rw, reflection_rw_value_alloca(rw->rtd, data));
	if (rc < 0)
		return -1;

	return rc;
}

struct reflection_meta *
reflection_meta_create(struct reflection_type_system *rts)
{
	struct reflection_meta *rm;

	assert(rts);
	rm = malloc(sizeof(*rm));
	RTS_ASSERT(rm);

	memset(rm, 0, sizeof(*rm));
	rm->rts = rts;
	return rm;

err_out:
	return NULL;
}

void
reflection_meta_destroy(struct reflection_meta *rm)
{
	struct reflection_any_value *rav;
	unsigned int i;
	char *str;

	if (!rm)
		return;

	for (i = 0; i < rmvid_str_count; i++) {
		str = rm->strs[i];
		if (str)
			free(str);
	}
	for (i = 0; i < rmvid_any_count; i++) {
		rav = rm->anys[i];
		if (rav)
			reflection_any_value_destroy(rav);
	}

	free(rm);
}

const char *
reflection_meta_value_str(struct reflection_meta *rm,
			  enum reflection_meta_value_id id)
{
	if (!rm)
		return NULL;

	assert(reflection_meta_value_id_is_valid(id));
	if (reflection_meta_value_id_is_bool(id))
		return reflection_meta_get_bool(rm, id) ? "true" : "false";
	if (reflection_meta_value_id_is_str(id))
		return reflection_meta_get_str(rm, id);
	if (reflection_meta_value_id_is_any(id))
		return reflection_any_value_get_str(reflection_meta_get_any_value(rm, id));
	return NULL;
}

int
reflection_meta_fill(struct reflection_meta *rm, struct fy_node *fyn_root)
{
	struct reflection_type_system *rts;
	struct fy_token *fyt;
	struct fy_node *fyn;
	const char *name, *text0;
	enum reflection_meta_value_id id;
	enum fy_parser_mode mode;
	bool v;
	int rc;

	assert(rm);

	/* nothing? that's fine, use the defaults */
	if (!fyn_root)
		return 0;

	rts = rm->rts;
	assert(rts);

	mode = reflection_type_system_parse_mode(rts);

	for (id = rmvid_first_valid; id <= rmvid_last_valid; id++) {

		/* if the value is already set, don't try again */
		if (reflection_meta_value_get_explicit(rm, id))
			continue;

		name = reflection_meta_value_id_get_name(id);
		assert(name);

		fyn = fy_node_by_path(fyn_root, name, FY_NT, FYNWF_DONT_FOLLOW);
		if (!fyn)
			continue;

		if (!reflection_meta_value_id_is_any(id)) {
			fyt = fy_node_get_scalar_token(fyn);
			RTS_ASSERT(fyt);

			text0 = fy_token_get_text0(fyt);
			RTS_ASSERT(text0);

			if (reflection_meta_value_id_is_bool(id)) {

				rc = parse_boolean_scalar(text0, mode, &v);
				RTS_ASSERT(!rc);

				rc = reflection_meta_set_bool(rm, id, v);
				RTS_ASSERT(!rc);

			} else if (reflection_meta_value_id_is_str(id)) {

				rc = reflection_meta_set_str(rm, id, text0);
				RTS_ASSERT(!rc);

			} else
				RTS_ASSERT(false);
		} else {
			rc = reflection_meta_set_any_value(rm, id,
					reflection_any_value_create(rm->rts, fyn), false);
			RTS_ASSERT(!rc);
		}
	}

	/* Warn about annotation keys that are not recognised.  Unknown keys
	 * are silently ignored by the loop above; emit a NOTICE (or ERROR in
	 * strict mode) here so the user can catch typos in their
	 * // yaml: { ... } comments. */
	{
		void *iter = NULL;
		struct fy_node_pair *fynp;
		const char *key_text;
		bool found;
		bool strict = rts && (rts->cfg.flags & RTSCF_STRICT_ANNOTATIONS);
		bool had_unknown = false;

		while ((fynp = fy_node_mapping_iterate(fyn_root, &iter))) {
			struct fy_node *fyn_key = fy_node_pair_key(fynp);

			key_text = fy_node_get_scalar0(fyn_key);
			if (!key_text)
				continue;

			found = false;
			for (id = rmvid_first_valid; id <= rmvid_last_valid; id++) {
				if (!strcmp(key_text, reflection_meta_value_id_get_name(id))) {
					found = true;
					break;
				}
			}
			if (!found) {
				struct fy_token *fyt = fy_node_token(fyn_key);

				had_unknown = true;
				_FY_RFL_LOG(rts,
					    strict ? FYET_ERROR : FYET_NOTICE,
					    false,
					    fyt,
					    "unknown annotation key '%s'%s\n",
					    key_text, strict ? "" : " (ignored)");

				fy_token_unref(fyt);
			}
		}

		rts->had_unknown_annotations = had_unknown;
	}

	return 0;

err_out:
	return -1;
}

bool
reflection_meta_compare(struct reflection_meta *rm_a, struct reflection_meta *rm_b)
{
	enum reflection_meta_value_id id;
	unsigned int idx;
	char *str_a, *str_b;
	struct reflection_any_value *rav_a, *rav_b;
	int r;

	if (!rm_a || !rm_b || rm_a->rts != rm_b->rts)
		return false;

	/* compare the presence map */
	r = memcmp(rm_a->explicit_map, rm_b->explicit_map,
		   ARRAY_SIZE(rm_a->explicit_map));
	if (r)
		return false;

	/* fast bool compare */
	r = memcmp(rm_a->bools, rm_b->bools, ARRAY_SIZE(rm_a->bools));
	if (r)
		return false;

	/* compare each string */
	for (id = rmvid_first_str; id <= rmvid_last_str; id++) {
		idx = id - rmvid_first_str;
		str_a = rm_a->strs[idx];
		str_b = rm_b->strs[idx];
		if (!str_a && !str_b)
			continue;
		if (str_a && !str_b)
			return false;
		if (!str_a && str_b)
			return false;
		r = strcmp(str_a, str_b);
		if (r)
			return false;
	}

	for (id = rmvid_first_any; id <= rmvid_last_any; id++) {
		idx = id - rmvid_first_any;
		rav_a = rm_a->anys[idx];
		rav_b = rm_b->anys[idx];
		if (!rav_a && !rav_b)
			continue;
		if (rav_a && !rav_b)
			return false;
		if (!rav_a && rav_b)
			return false;
		if (!fy_node_compare(rav_a->fyn, rav_b->fyn))
			return false;
	}

	return true;
}

struct reflection_meta *
reflection_meta_copy(struct reflection_meta *rm_src)
{
	struct reflection_type_system *rts;
	struct reflection_meta *rm;
	enum reflection_meta_value_id id;
	unsigned int idx;

	if (!rm_src)
		return NULL;

	rts = rm_src->rts;
	assert(rts);

	rm = reflection_meta_create(rts);
	if (!rm)
		return NULL;

	rm->explicit_count = rm_src->explicit_count;
	memcpy(rm->explicit_map, rm_src->explicit_map, sizeof(rm->explicit_map));
	memcpy(rm->bools, rm_src->bools, sizeof(rm->bools));

	for (id = rmvid_first_str; id <= rmvid_last_str; id++) {
		idx = id - rmvid_first_str;
		if (rm_src->strs[idx]) {
			rm->strs[idx] = strdup(rm_src->strs[idx]);
			RTS_ASSERT(rm->strs[idx]);
		}
	}

	for (id = rmvid_first_any; id <= rmvid_last_any; id++) {
		idx = id - rmvid_first_any;
		if (rm_src->anys[idx]) {
			rm->anys[idx] = reflection_any_value_copy(rm_src->anys[idx]);
			RTS_ASSERT(rm->anys[idx]);
		}
	}

	return rm;

err_out:
	reflection_meta_destroy(rm);
	return NULL;
}

struct fy_document *
reflection_meta_get_document(struct reflection_meta *rm)
{
	struct reflection_type_system *rts;
	struct fy_document *fyd = NULL;
	struct fy_node *fyn_map, *fyn_key, *fyn_value;
	enum reflection_meta_value_id id;
	bool v;
	const char *name, *str;
	struct reflection_any_value *rav;
	int rc;

	if (!rm)
		return NULL;

	rts = rm->rts;
	assert(rts);

	fyn_map = fyn_key = fyn_value = NULL;

	fyd = fy_document_create(NULL);
	RTS_ASSERT(fyd);

	fyn_map = fy_node_create_mapping(fyd);
	RTS_ASSERT(fyn_map);

	for (id = rmvid_first_valid; id <= rmvid_last_valid; id++) {

		/* if the value is not explicitly set, do not include it */
		if (!reflection_meta_value_get_explicit(rm, id))
			continue;

		name = reflection_meta_value_id_get_name(id);
		RTS_ASSERT(name);

		if (reflection_meta_value_id_is_bool(id)) {

			v = reflection_meta_get_bool(rm, id);

			fyn_value = fy_node_create_scalar(fyd,
					v ? "true" : "false", FY_NT);
			RTS_ASSERT(fyn_value);

		} else if (reflection_meta_value_id_is_str(id)) {

			str = reflection_meta_get_str(rm, id);
			if (!str)
				continue;

			fyn_value = fy_node_create_scalar(fyd, str, FY_NT);
			RTS_ASSERT(fyn_value);

		} else if (reflection_meta_value_id_is_any(id)) {

			rav = reflection_meta_get_any_value(rm, id);
			if (!rav || !rav->fyn)
				continue;

			fyn_value = fy_node_copy(fyd, rav->fyn);
			RTS_ASSERT(fyn_value);
		} else
			continue;

		fyn_key = fy_node_create_scalar(fyd, name, FY_NT);
		RTS_ASSERT(fyn_key);

		rc = fy_node_mapping_append(fyn_map, fyn_key, fyn_value);
		RTS_ASSERT(!rc);

		fyn_key = fyn_value = NULL;
	}

	rc = fy_document_set_root(fyd, fyn_map);
	RTS_ASSERT(!rc);

	return fyd;

err_out:
	fy_node_free(fyn_key);
	fy_node_free(fyn_value);
	fy_node_free(fyn_map);
	fy_document_destroy(fyd);
	return NULL;
}

char *
reflection_meta_get_document_str(struct reflection_meta *rm)
{
	struct fy_document *fyd;
	char *str;

	if (!rm)
		return NULL;

	fyd = reflection_meta_get_document(rm);
	if (!fyd)
		return NULL;

	str = fy_emit_node_to_string(fy_document_root(fyd),
			FYECF_WIDTH_INF | FYECF_INDENT_DEFAULT |
			FYECF_MODE_FLOW_ONELINE | FYECF_NO_ENDING_NEWLINE);

	fy_document_destroy(fyd);
	return str;
}

#endif /* HAVE_REFLECTION */
