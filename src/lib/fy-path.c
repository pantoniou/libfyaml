/*
 * fy-path.c - Internal ypath support
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
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
#include <errno.h>
#include <stdarg.h>

#include <libfyaml.h>

#include "fy-parse.h"
#include "fy-doc.h"

#include "fy-utils.h"

#undef DBG
// #define DBG fyp_notice
#define DBG fyp_scan_debug

int fy_path_setup(struct fy_path *fypp)
{
	memset(fypp, 0, sizeof(*fypp));

	fy_path_component_list_init(&fypp->recycled_component);
	fy_path_component_list_init(&fypp->components);

	fy_emit_accum_init(&fypp->ea, &fypp->ea_inplace_buf, sizeof(fypp->ea_inplace_buf), 0, fylb_cr_nl);

	return 0;
}

void fy_path_cleanup(struct fy_path *fypp)
{
	struct fy_path_component *fypc;

	if (!fypp)
		return;

	if (fypp->fydb) {
		fy_document_builder_destroy(fypp->fydb);
		fypp->fydb = NULL;
	}

	fy_emit_accum_cleanup(&fypp->ea);

	while ((fypc = fy_path_component_list_pop(&fypp->components)) != NULL)
		fy_path_component_free(fypc);

	while ((fypc = fy_path_component_list_pop(&fypp->recycled_component)) != NULL)
		fy_path_component_free(fypc);
}

struct fy_path *fy_path_create(void)
{
	struct fy_path *fypp;
	int rc;

	fypp = malloc(sizeof(*fypp));
	if (!fypp)
		return NULL;

	rc = fy_path_setup(fypp);
	if (rc)
		return NULL;

	return fypp;
}

void fy_path_destroy(struct fy_path *fypp)
{
	if (!fypp)
		return;

	fy_path_cleanup(fypp);

	free(fypp);
}

void fy_path_reset(struct fy_path *fypp)
{
	struct fy_path_component *fypc;

	if (!fypp)
		return;

	while ((fypc = fy_path_component_list_pop(&fypp->components)) != NULL)
		fy_path_component_free(fypc);

	/* reset the text cache, but don't free the buffer */
	fy_emit_accum_reset(&fypp->ea);
}

struct fy_path_component *fy_path_component_alloc(struct fy_path *fypp)
{
	struct fy_path_component *fypc;

	if (!fypp)
		return NULL;

	fypc = fy_path_component_list_pop(&fypp->recycled_component);
	if (!fypc) {
		fypc = malloc(sizeof(*fypc));
		if (!fypc)
			return NULL;
		memset(fypc, 0, sizeof(*fypc));
	}

	/* not yet instantiated */
	fypc->type = FYPCT_NONE;

	return fypc;
}

void fy_path_component_clear_state(struct fy_path_component *fypc)
{
	if (!fypc)
		return;

	switch (fypc->type) {
	case FYPCT_NONE:
		/* nothing */
		break;

	case FYPCT_MAP:
		if (fypc->map.text_storage)
			free(fypc->map.text_storage);
		fypc->map.got_key = false;
		fypc->map.is_complex_key = false;
		fypc->map.accumulating_complex_key = false;
		fypc->map.text = NULL;
		fypc->map.size = 0;
		fypc->map.text_storage = NULL;
		break;

	case FYPCT_SEQ:
		fypc->seq.buf[0] = '\0';
		fypc->seq.buflen = 0;
		break;
	}
}

void fy_path_component_cleanup(struct fy_path_component *fypc)
{
	if (!fypc)
		return;

	fy_path_component_clear_state(fypc);
	fypc->type = FYPCT_NONE;
}

void fy_path_component_free(struct fy_path_component *fypc)
{
	if (!fypc)
		return;

	fy_path_component_cleanup(fypc);
	free(fypc);
}

void fy_path_component_destroy(struct fy_path_component *fypc)
{
	if (!fypc)
		return;

	fy_path_component_cleanup(fypc);
	fy_path_component_free(fypc);
}

void fy_path_component_recycle(struct fy_path *fypp, struct fy_path_component *fypc)
{
	if (!fypc)
		return;

	fy_path_component_cleanup(fypc);

	if (!fypp)
		fy_path_component_free(fypc);
	else
		fy_path_component_list_push(&fypp->recycled_component, fypc);
}

struct fy_path_component *fy_path_component_create_mapping(struct fy_path *fypp)
{
	struct fy_path_component *fypc;

	if (!fypp)
		return NULL;

	fypc = fy_path_component_alloc(fypp);
	if (!fypc)
		return NULL;

	fypc->type = FYPCT_MAP;
	fypc->map.got_key = false;
	fypc->map.is_complex_key = false;
	fypc->map.accumulating_complex_key = false;

	fypc->map.text = NULL;
	fypc->map.size = 0;
	fypc->map.text_storage = NULL;

	return fypc;
}

struct fy_path_component *fy_path_component_create_sequence(struct fy_path *fypp)
{
	struct fy_path_component *fypc;

	if (!fypp)
		return NULL;

	fypc = fy_path_component_alloc(fypp);
	if (!fypc)
		return NULL;

	fypc->type = FYPCT_SEQ;
	fypc->seq.idx = -1;

	return fypc;
}

bool fy_path_component_is_complete(struct fy_path_component *fypc)
{
	if (!fypc)
		return false;

	switch (fypc->type) {
	case FYPCT_NONE:
		break;
	case FYPCT_MAP:
		return fypc->map.text != NULL;
	case FYPCT_SEQ:
		return fypc->seq.idx >= 0;
	}

	return false;
}

int fy_path_component_build_text(struct fy_path_component *fypc, void *arg)
{
	struct fy_token *fyt;
	enum fy_token_type type;
	struct fy_document *fyd;
	int *idxp;
	const char *txt;
	char *dst;
	char prefix;

	assert(fypc);

	switch (fypc->type) {
	case FYPCT_MAP:
		if (!fypc->map.is_complex_key) {

			fyt = arg;

			prefix = 0;
			txt = NULL;
			type = fyt ? fyt->type : FYTT_SCALAR;
			switch (type) {
			case FYTT_SCALAR:
				if (fyt) {
					txt = fy_token_get_scalar_path_key(fyt, &fypc->map.size);
				} else {
					txt = "";
					fypc->map.size = 0;
				}
				assert(txt);
				break;
			case FYTT_ALIAS:
				assert(fyt);
				txt = fy_token_get_text(fyt, &fypc->map.size);
				assert(txt);
				fypc->map.size++;
				prefix = '*';
				break;
			default:
				assert(0);
				break;
			}
			if (fypc->map.size + 1 > sizeof(fypc->map.buf)) {
				/* keep a malloc copy if too large */
				fypc->map.text_storage = malloc(fypc->map.size + 1);
				assert(fypc->map.text_storage);
				dst = fypc->map.text_storage;
			} else {
				dst = fypc->map.buf;
			}
			if (!prefix) {
				memcpy(dst, txt, fypc->map.size);
			} else {
				dst[0] = prefix;
				memcpy(dst + 1, txt, fypc->map.size - 1);
			}
			dst[fypc->map.size] = '\0';
			fypc->map.text = dst;
		} else {
			fyd = arg;
			assert(fyd);

			/* a complex key is always allocated (since it's very rare) */
			fypc->map.text_storage =
				fy_emit_document_to_string(fyd,
					FYECF_WIDTH_INF | FYECF_INDENT_DEFAULT |
					FYECF_MODE_FLOW_ONELINE | FYECF_NO_ENDING_NEWLINE);
			assert(fypc->map.text_storage);
			fypc->map.size = strlen(fypc->map.text_storage);
			fypc->map.text = fypc->map.text_storage;
		}
		break;

	case FYPCT_SEQ:
		/* first element not encountered yet */
		assert(arg);
		idxp = arg;
		assert(*idxp >= 0);

		fypc->seq.buflen = snprintf(fypc->seq.buf, sizeof(fypc->seq.buf) - 1, "%d", *idxp);
		break;

	default:
		assert(0);
		/* cannot happen */
		break;

	}

	return 0;
}

const char *fy_path_component_get_text(struct fy_path_component *fypc, size_t *lenp)
{
	if (!fypc) {
		*lenp = 0;
		return NULL;
	}

	switch (fypc->type) {
	case FYPCT_MAP:
		*lenp = fypc->map.size;
		return fypc->map.text;

	case FYPCT_SEQ:
		*lenp = fypc->seq.buflen;
		return fypc->seq.buf;

	default:
		assert(0);
		/* cannot happen */
		break;

	}

	return NULL;
}

const char *fy_path_component_get_text0(struct fy_path_component *fypc)
{
	size_t len;
	const char *txt;

	txt = fy_path_component_get_text(fypc, &len);
	if (!txt)
		return NULL;

	/* it is guaranteed that it's NULL terminated */
	assert(strlen(txt) == len);

	return txt;
}

int fy_path_rebuild(struct fy_path *fypp)
{
	struct fy_path_component *fypc, *fypc_root;
	const char *text;
	size_t len;

	fy_emit_accum_start(&fypp->ea, 0, fylb_cr_nl);

	/* OK, we have to iterate and rebuild the paths */
	fypc_root = NULL;
	for (fypc = fy_path_component_list_head(&fypp->components);
		fypc && fy_path_component_is_complete(fypc);
		fypc = fy_path_component_next(&fypp->components, fypc)) {

		text = fy_path_component_get_text(fypc, &len);

		if (!fypc_root)
			fypc_root = fypc;

		fy_emit_accum_utf8_put_raw(&fypp->ea, '/');
		fy_emit_accum_utf8_write_raw(&fypp->ea, text, len);
	}

	if (fy_emit_accum_empty(&fypp->ea))
		fy_emit_accum_utf8_printf_raw(&fypp->ea, "/");

	return 0;
}

const char *fy_path_get_text(struct fy_path *fypp, size_t *lenp)
{
	// fy_path_rebuild(fypp);
	if (fy_emit_accum_empty(&fypp->ea)) {
		*lenp = 1;
		return "/";
	}
	return fy_emit_accum_get(&fypp->ea, lenp);
}

const char *fy_path_get_text0(struct fy_path *fypp)
{
	if (fy_emit_accum_empty(&fypp->ea))
		return "/";
	return fy_emit_accum_get0(&fypp->ea);
}

struct fy_path_component *
fy_path_get_last_complete(struct fy_path *fypp)
{
	struct fy_path_component *fypc_last;

	fypc_last = fy_path_component_list_tail(&fypp->components);

	/* if the last one is not complete, get the previous */
	if (fypc_last && !fy_path_component_is_complete(fypc_last))
		fypc_last = fy_path_component_prev(&fypp->components, fypc_last);

	if (!fypc_last)
		return NULL;

	/* must be complete */
	assert(fy_path_component_is_complete(fypc_last));

	return fypc_last;
}

bool fy_path_is_root(struct fy_path *fypp)
{
	if (!fypp)
		return true;

	return fy_path_get_last_complete(fypp) == NULL;
}

bool fy_path_in_sequence(struct fy_path *fypp)
{
	struct fy_path_component *fypc_last;

	if (!fypp)
		return false;

	fypc_last = fy_path_get_last_complete(fypp);
	if (!fypc_last)
		return false;

	return fypc_last->type == FYPCT_SEQ;
}

bool fy_path_in_mapping(struct fy_path *fypp)
{
	struct fy_path_component *fypc_last;

	if (!fypp)
		return false;

	fypc_last = fy_path_get_last_complete(fypp);
	if (!fypc_last)
		return false;

	return fypc_last->type == FYPCT_MAP;
}

int fy_path_depth(struct fy_path *fypp)
{
	struct fy_path_component *fypc;
	int depth;

	depth = 0;
	for (fypc = fy_path_component_list_head(&fypp->components);
		fypc && fy_path_component_is_complete(fypc);
		fypc = fy_path_component_next(&fypp->components, fypc)) {

		depth++;
	}

	return depth;
}
