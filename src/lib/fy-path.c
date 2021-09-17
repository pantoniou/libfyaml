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

struct fy_composer *
fy_composer_create(struct fy_composer_cfg *cfg)
{
	struct fy_composer *fyc;
	int rc;

	if (!cfg || !cfg->ops)
		return NULL;

	fyc = malloc(sizeof(*fyc));
	if (!fyc)
		return NULL;
	memset(fyc, 0, sizeof(*fyc));
	fyc->cfg = *cfg;
	rc = fy_path_setup(&fyc->fypp);
	if (rc)
		goto err_no_path;

	return fyc;

err_no_path:
	free(fyc);
	return NULL;

}

void fy_composer_destroy(struct fy_composer *fyc)
{
	if (!fyc)
		return;

	fy_diag_unref(fyc->cfg.diag);
	fy_path_cleanup(&fyc->fypp);
	free(fyc);
}

int fy_composer_process_event_private(struct fy_composer *fyc, struct fy_parser *fyp, struct fy_eventp *fyep)
{
	const struct fy_composer_ops *ops;
	struct fy_path *fypp;
	struct fy_path_component *fypc, *fypc_last;
	struct fy_token *fyt, *tag, *anchor;
	struct fy_document *fyd;
	bool is_collection, is_map, is_start, is_complete;
	char tbuf[80] __attribute__((__unused__));
	const char *text;
	size_t len;
	int rc;

	assert(fyc);
	assert(fyp);
	assert(fyep);

	ops = fyc->cfg.ops;
	assert(ops);

	fypp = &fyc->fypp;

	rc = 0;

	switch (fyep->e.type) {
	case FYET_MAPPING_START:
		fyt = fyep->e.mapping_start.mapping_start;
		tag = fyep->e.mapping_start.tag;
		anchor = fyep->e.mapping_start.anchor;
		is_collection = true;
		is_start = true;
		is_map = true;
		break;

	case FYET_MAPPING_END:
		fyt = fyep->e.mapping_end.mapping_end;
		tag = NULL;
		anchor = NULL;
		is_collection = true;
		is_start = false;
		is_map = true;
		break;

	case FYET_SEQUENCE_START:
		fyt = fyep->e.sequence_start.sequence_start;
		tag = fyep->e.sequence_start.tag;
		anchor = fyep->e.sequence_start.anchor;
		is_collection = true;
		is_start = true;
		is_map = false;
		break;

	case FYET_SEQUENCE_END:
		fyt = fyep->e.sequence_end.sequence_end;
		tag = NULL;
		anchor = NULL;
		is_collection = true;
		is_start = false;
		is_map = false;
		break;

	case FYET_SCALAR:
		fyt = fyep->e.scalar.value;
		tag = fyep->e.scalar.tag;
		anchor = fyep->e.scalar.anchor;
		is_collection = false;
		is_start = true;
		is_map = false;
		break;

	case FYET_ALIAS:
		fyt = fyep->e.alias.anchor;
		tag = NULL;
		anchor = NULL;
		is_collection = false;
		is_start = true;
		is_map = false;
		break;

	case FYET_STREAM_START:
		if (ops->stream_start)
			return ops->stream_start(fyc);
		return 0;

	case FYET_STREAM_END:
		if (ops->stream_end)
			return ops->stream_end(fyc);
		return 0;

	case FYET_DOCUMENT_START:
		if (ops->document_start)
			return ops->document_start(fyc, fyep->e.document_start.document_state);
		return 0;

	case FYET_DOCUMENT_END:
		if (ops->document_end)
			return ops->document_end(fyc);
		return 0;

	default:
		// DBG(fyp, "ignoring\n");
		return 0;
	}

	/* unused for now */
	(void)anchor;

	is_complete = true;

	fypc_last = fy_path_component_list_tail(&fypp->components);

	// DBG(fyp, "%s: start - %s\n", fy_path_get_text0(fypp), fy_token_dump_format(fyt, tbuf, sizeof(tbuf)));

	if (fypc_last && fypc_last->type == FYPCT_MAP && fypc_last->map.accumulating_complex_key) {

		// DBG(fyp, "accumulating for complex key\n");

		rc = fy_document_builder_process_event(fypp->fydb, fyp, fyep);
		if (rc == 0) {
			// DBG(fyp, "accumulating still\n");
			return 0;
		}
		if (rc < 0) {
			// DBG(fyp, "document build error\n");
			return -1;
		}

		// DBG(fyp, "accumulation complete\n");

		/* get the document */
		fyd = fy_document_builder_take_document(fypp->fydb);
		assert(fyd);

		fypc_last->map.got_key = true;
		fypc_last->map.is_complex_key = true;
		fypc_last->map.accumulating_complex_key = false;

		rc = fy_path_component_build_text(fypc_last, fyd);
		if (rc)
			abort();

		// DBG(fyp, "%s: %s complex KEY\n", __func__, fy_path_get_text0(fypp));

		/* and destroy the document immediately */
		fy_document_destroy(fyd);

		if (rc < 0) {
			// DBG(fyp, "%s: %s fy_path_component_build_text() failure\n", __func__, fy_path_get_text0(fypp));
			return -1;
		}

		// append to the string path
		fy_emit_accum_rewind_state(&fypp->ea, &fypc_last->start);
		text = fy_path_component_get_text(fypc_last, &len);
		assert(text);
		fy_emit_accum_utf8_put_raw(&fypp->ea, '/');
		fy_emit_accum_utf8_write_raw(&fypp->ea, text, len);

		return 0;
	}

	if (fypc_last && is_start) {
		switch (fypc_last->type) {
		case FYPCT_MAP:
			/* if there's no key set, set it now (and mark as non-complete) */
			// DBG(fyp, "map.got_key=%s\n", fypc_last->map.got_key ? "true" : "false");
			if (fypc_last->map.got_key) {
				/* XXX maybe count here? */
				break;
			}
			if (is_collection) {
				// DBG(fyp, "Non scalar key - using document builder\n");
				if (!fypp->fydb) {
					fypp->fydb = fy_document_builder_create(&fyp->cfg);
					assert(fypp->fydb);
				}

				/* start with this document state */
				rc = fy_document_builder_set_in_document(fypp->fydb, fy_parser_get_document_state(fyp), true);
				assert(!rc);

				/* and pass the current event; must return 0 since we know it's a collection start */
				rc = fy_document_builder_process_event(fypp->fydb, fyp, fyep);
				assert(!rc);

				is_complete = false;
				fypc_last->map.is_complex_key = true;
				fypc_last->map.accumulating_complex_key = true;
			} else {
				// DBG(fyp, "scalar key: %s\n", fy_token_dump_format(fyt, tbuf, sizeof(tbuf)));
				fypc_last->map.got_key = true;
				fypc_last->map.is_complex_key = false;

				rc = fy_path_component_build_text(fypc_last, fyt);
				assert(!rc);

				// DBG(fyp, "%s: %s KEY %s\n", __func__, fy_path_get_text0(fypp), fy_token_get_scalar_path_key0(fyt) ? : "<null>");
				is_complete = false;

				// append to the string path
				fy_emit_accum_rewind_state(&fypp->ea, &fypc_last->start);
				text = fy_path_component_get_text(fypc_last, &len);
				assert(text);
				fy_emit_accum_utf8_put_raw(&fypp->ea, '/');
				fy_emit_accum_utf8_write_raw(&fypp->ea, text, len);
			}
			break;

		case FYPCT_SEQ:
			if (fypc_last->seq.idx < 0)
				fypc_last->seq.idx = 0;
			else
				fypc_last->seq.idx++;

			rc = fy_path_component_build_text(fypc_last, &fypc_last->seq.idx);
			assert(!rc);

			// DBG(fyp, "%s: %s SEQ next %d\n", __func__, fy_path_get_text0(fypp), fypc_last->seq.idx);
			fy_emit_accum_rewind_state(&fypp->ea, &fypc_last->start);
			text = fy_path_component_get_text(fypc_last, &len);
			assert(text);
			fy_emit_accum_utf8_put_raw(&fypp->ea, '/');
			fy_emit_accum_utf8_write_raw(&fypp->ea, text, len);
			break;

		case FYPCT_NONE:
			assert(0);
			break;
		}
	}

	/* not complete yet */
	if (!is_complete) {
		// DBG(fyp, "%s: not-complete\n", fy_path_get_text0(fypp));
		return 0;
	}

	/* collection start */
	if (is_collection && is_start) {
		fypc = is_map ? fy_path_component_create_mapping(fypp) : fy_path_component_create_sequence(fypp);
		assert(fypc);

		/* append to the tail */
		fy_path_component_list_add_tail(&fypp->components, fypc);

		/* save state of path */
		fy_emit_accum_get_state(&fypp->ea, &fypc->start);

		if (fypc_last && fypc_last->type == FYPCT_MAP && fypc_last->map.got_key) {
			// DBG(fyp, "%s: clear got_key %p\n", fy_path_get_text0(fypp), fypc_last);
			fypc_last->map.got_key = false;
		}

		// DBG(fyp, "%s: push %p\n", fy_path_get_text0(fypp), fypc);

		if (is_map) {
			if (ops->mapping_start)
				return ops->mapping_start(fyc, fypp, tag, fyt);
		} else {
			if (ops->sequence_start)
				return ops->sequence_start(fyc, fypp, tag, fyt);
		}
	}

	if (!is_collection) {
		// DBG(fyp, "%s: scalar %s\n", fy_path_get_text0(fypp), fy_token_get_scalar_path_key0(fyt) ? : "<null>");
		
		if (ops->scalar)
			rc = ops->scalar(fyc, fypp, tag, fyt);
		
		if (fypc_last && fypc_last->type == FYPCT_MAP && fypc_last->map.got_key) {
			// DBG(fyp, "%s: clear got_key %p\n", fy_path_get_text0(fypp), fypc_last);
			fypc_last->map.got_key = false;
		}

	}

	/* collection end */
	if (is_collection && !is_start) {

		fypc = fy_path_component_list_pop_tail(&fypp->components);
		assert(fypc);

		/* rewind */
		fy_emit_accum_rewind_state(&fypp->ea, &fypc->start);
		fy_path_component_recycle(fypp, fypc);

		fypc_last = fy_path_component_list_tail(&fypp->components);
		assert(fypc_last);
		fy_path_component_clear_state(fypc_last);

		// DBG(fyp, "%s: pop\n", fy_path_get_text0(fypp));
		//
		if (is_map) {
			if (ops->mapping_end)
				return ops->mapping_end(fyc, fypp, fyt);
		} else {
			if (ops->sequence_end)
				return ops->sequence_end(fyc, fypp, fyt);
		}
	}

	// DBG(fyp, "%s: exit\n", fy_path_get_text0(fypp));

	return 0;
}

int fy_composer_process_event(struct fy_composer *fyc, struct fy_parser *fyp, struct fy_event *fye)
{
	if (!fye)
		return -1;

	return fy_composer_process_event_private(fyc, fyp, container_of(fye, struct fy_eventp, e));
}
