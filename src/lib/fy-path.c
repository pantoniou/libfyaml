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
// #define DBG fypp_notice
#define DBG fypp_debug

int fy_path_setup(struct fy_path *fypp, const struct fy_path_cfg *cfg)
{
	struct fy_diag *diag;

	memset(fypp, 0, sizeof(*fypp));
	if (cfg) {
		if (cfg->diag)
			diag = cfg->diag;
		else if (cfg->fyp)
			diag = cfg->fyp->diag;
		else
			diag = NULL;

		fypp->cfg.diag = fy_diag_ref(diag);
		fypp->cfg.fyp = cfg->fyp;
	}

	fy_path_component_list_init(&fypp->components);
	fypp->count = 0;

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

	while ((fypc = fy_path_component_list_pop(&fypp->components)) != NULL) {
		fy_path_component_free(fypc);
		fypp->count--;
	}

	fypp->seq = 0;
	fypp->textseq = 0;

	if (fypp->cfg.diag)
		fy_diag_unref(fypp->cfg.diag);
}

struct fy_path *fy_path_create(const struct fy_path_cfg *cfg)
{
	struct fy_path *fypp;
	int rc;

	fypp = malloc(sizeof(*fypp));
	if (!fypp)
		return NULL;

	rc = fy_path_setup(fypp, cfg);
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

	while ((fypc = fy_path_component_list_pop(&fypp->components)) != NULL) {
		fy_path_component_free(fypc);
		fypp->count--;
	}

	/* reset the text cache, but don't free the buffer */
	fy_emit_accum_reset(&fypp->ea);
	fypp->seq = 0;
	fypp->textseq = 0;
}

struct fy_path_component *fy_path_component_alloc(struct fy_path *fypp)
{
	struct fy_parser *fyp;
	struct fy_path_component *fypc;

	if (!fypp)
		return NULL;

	fyp = fypp->cfg.fyp;
	if (fyp) {
		fypc = fy_parse_path_component_alloc(fyp);
	} else {
		fypc = malloc(sizeof(*fypc));
		if (!fypc)
			return NULL;
	}

	/* not yet instantiated */
	fypc->fypp = fypp;
	fypc->type = FYPCT_NONE;
	fypc->tag = NULL;
	fypc->anchor = NULL;

	return fypc;
}

void fy_path_component_cleanup(struct fy_path_component *fypc)
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
		fy_token_unref(fypc->map.key);
		fy_document_destroy(fypc->map.fyd);
		fypc->map.got_key = false;
		fypc->map.is_complex_key = false;
		fypc->map.accumulating_complex_key = false;
		fypc->map.key = NULL;
		fypc->map.fyd = NULL;
		fypc->map.text = NULL;
		fypc->map.size = 0;
		fypc->map.text_storage = NULL;
		break;

	case FYPCT_SEQ:
		fypc->seq.idx = -1;
		fypc->seq.bufidx = -1;
		fypc->seq.buf[0] = '\0';
		break;
	}

	if (fypc->tag) {
		fy_token_unref(fypc->tag);
		fypc->tag = NULL;
	}

	if (fypc->anchor) {
		fy_token_unref(fypc->anchor);
		fypc->anchor = NULL;
	}
}

void fy_path_component_free(struct fy_path_component *fypc)
{
	struct fy_path *fypp;
	struct fy_parser *fyp;

	if (!fypc)
		return;

	fy_path_component_cleanup(fypc);

	fypp = fypc->fypp;
	if (!fypp)
		return;

	fyp = fypp->cfg.fyp;
	if (fyp)
		fy_parse_path_component_recycle(fyp, fypc);
	else
		free(fypc);
}

void fy_path_component_destroy(struct fy_path_component *fypc)
{
	if (!fypc)
		return;

	fy_path_component_cleanup(fypc);

	return fy_path_component_free(fypc);
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
	fypc->map.key = NULL;
	fypc->map.fyd = NULL;

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

void fy_path_component_set_tag(struct fy_path_component *fypc, struct fy_token *tag)
{
	if (!fypc)
		return;
	fy_token_unref(fypc->tag);
	fypc->tag = fy_token_ref(tag);
}

void fy_path_component_set_anchor(struct fy_path_component *fypc, struct fy_token *anchor)
{
	if (!fypc)
		return;
	fy_token_unref(fypc->anchor);
	fypc->anchor = fy_token_ref(anchor);
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

const char *fy_path_component_get_text(struct fy_path_component *fypc, size_t *lenp)
{
	const char *txt;
	char *dst;
	char prefix;

	if (!fypc) {
		*lenp = 0;
		return NULL;
	}

	switch (fypc->type) {
	case FYPCT_MAP:
		if (fypc->map.text) {
			*lenp = fypc->map.size;
			return fypc->map.text;
		}

		if (!fypc->map.key && !fypc->map.fyd) {
			/* not with a key */
			*lenp = 1;
			return "/";
		}

		if (!fypc->map.is_complex_key) {
			assert(fypc->map.key);
			prefix = 0;
			txt = NULL;
			switch (fypc->map.key->type) {
			case FYTT_SCALAR:
				txt = fy_token_get_scalar_path_key(fypc->map.key, &fypc->map.size);
				assert(txt);
				break;
			case FYTT_ALIAS:
				txt = fy_token_get_text(fypc->map.key, &fypc->map.size);
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
			/* a complex key is always allocated (since it's very rare) */
			fypc->map.text_storage =
				fy_emit_document_to_string(fypc->map.fyd,
					FYECF_WIDTH_INF | FYECF_INDENT_DEFAULT |
					FYECF_MODE_FLOW_ONELINE | FYECF_NO_ENDING_NEWLINE);
			assert(fypc->map.text_storage);
			fypc->map.size = strlen(fypc->map.text_storage);
			fypc->map.text = fypc->map.text_storage;
		}
		*lenp = fypc->map.size;
		return fypc->map.text;

	case FYPCT_SEQ:
		/* first element not encountered yet */
		if (fypc->seq.idx < 0) {
			*lenp = 1;
			return "/";
		}

		if (fypc->seq.idx != fypc->seq.bufidx) {
			snprintf(fypc->seq.buf, sizeof(fypc->seq.buf) - 1, "%d", fypc->seq.idx);
			fypc->seq.bufidx = fypc->seq.idx;
			fypc->seq.buflen = strlen(fypc->seq.buf);
		}
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

		fy_emit_accum_utf8_printf_raw(&fypp->ea, "/%.*s", (int)len, text);
	}

	if (fy_emit_accum_empty(&fypp->ea))
		fy_emit_accum_utf8_printf_raw(&fypp->ea, "/");

	return 0;
}

const char *fy_path_get_text(struct fy_path *fypp, size_t *lenp)
{
	fy_path_rebuild(fypp);
	return fy_emit_accum_get(&fypp->ea, lenp);
}

const char *fy_path_get_text0(struct fy_path *fypp)
{
	fy_path_rebuild(fypp);
	return fy_emit_accum_get0(&fypp->ea);
}

int fy_parse_path_event(struct fy_parser *fyp, struct fy_eventp *fyep)
{
	struct fy_path_component *fypc, *fypc_last;
	struct fy_path *fypp;
	struct fy_token *fyt, *tag, *anchor;
	bool is_collection, is_map, is_start, is_end, is_complete;
	char tbuf[80] __attribute__((__unused__));
	const char *txt;
	int rc;

	assert(fyp);
	assert(fyep);

	fypp = &fyp->path;

	switch (fyep->e.type) {
	case FYET_MAPPING_START:
		fyt = fyep->e.mapping_start.mapping_start;
		tag = fyep->e.mapping_start.tag;
		anchor = fyep->e.mapping_start.anchor;
		is_collection = true;
		is_start = true;
		is_end = false;
		is_map = true;
		break;

	case FYET_MAPPING_END:
		fyt = fyep->e.mapping_end.mapping_end;
		tag = NULL;
		anchor = NULL;
		is_collection = true;
		is_start = false;
		is_end = false;
		is_map = true;
		break;

	case FYET_SEQUENCE_START:
		fyt = fyep->e.sequence_start.sequence_start;
		tag = fyep->e.sequence_start.tag;
		anchor = fyep->e.sequence_start.anchor;
		is_collection = true;
		is_start = true;
		is_end = false;
		is_map = false;
		break;

	case FYET_SEQUENCE_END:
		fyt = fyep->e.sequence_end.sequence_end;
		tag = NULL;
		anchor = NULL;
		is_collection = true;
		is_start = false;
		is_end = false;
		is_map = false;
		break;

	case FYET_SCALAR:
		fyt = fyep->e.scalar.value;
		tag = fyep->e.scalar.tag;
		anchor = fyep->e.scalar.anchor;
		is_collection = false;
		is_start = true;
		is_end = true;
		is_map = false;
		break;

	case FYET_ALIAS:
		fyt = fyep->e.alias.anchor;
		tag = NULL;
		anchor = NULL;
		is_collection = false;
		is_start = true;
		is_end = true;
		is_map = false;
		break;

	default:
		// DBG(fypp, "ignoring\n");
		return 0;
	}

	is_complete = true;

	fypc_last = fy_path_component_list_tail(&fypp->components);

	if (fypc_last && fypc_last->type == FYPCT_MAP) {
		// DBG(fypp, "%s: start - %s %s %p\n", fy_path_get_text0(fypp), fy_token_dump_format(fyt, tbuf, sizeof(tbuf)), fypc_last->map.got_key ? "got_key" : "", fypc_last);
	} else {
		// DBG(fypp, "%s: start - %s\n", fy_path_get_text0(fypp), fy_token_dump_format(fyt, tbuf, sizeof(tbuf)));
	}

	if (fypc_last && fypc_last->type == FYPCT_MAP && fypc_last->map.accumulating_complex_key) {

		// DBG(fypp, "accumulating for complex key\n");

		rc = fy_document_builder_process_event(fypp->fydb, fyp, fyep);
		if (!rc) {
			// DBG(fypp, "accumulating still\n");
			return 0;
		}
		// DBG(fypp, "accumulation complete\n");

		/* get the document */
		fypc_last->map.fyd = fy_document_builder_take_document(fypp->fydb);
		assert(fypc_last->map.fyd);

		fypc_last->map.got_key = true;
		fypc_last->map.is_complex_key = true;
		fypc_last->map.accumulating_complex_key = false;

		/* get the text */
		txt = fy_path_component_get_text0(fypc_last);
		assert(txt);
		if (!txt)
			return -1;

		// DBG(fypp, "%s: %s complex KEY %s\n", __func__, fy_path_get_text0(fypp), txt);

		/* and destroy the document immediately */
		fy_document_destroy(fypc_last->map.fyd);
		fypc_last->map.fyd = NULL;

		goto complete;
	}

	if (fypc_last && is_start) {
		switch (fypc_last->type) {
		case FYPCT_MAP:
			/* if there's no key set, set it now (and mark as non-complete) */
			if (!fypc_last->map.got_key) {
				if (is_collection) {
					// DBG(fypp, "Non scalar key - using document builder\n");
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
					break;
				}

				// DBG(fypp, "scalar key: %s\n", fy_token_dump_format(fyt, tbuf, sizeof(tbuf)));
				fypc_last->map.got_key = true;
				fypc_last->map.is_complex_key = false;
				fypc_last->map.key = fy_token_ref(fyt);

				/* get the text */
				txt = fy_path_component_get_text0(fypc_last);
				assert(txt);

				/* and drop the ref immediately */
				fy_token_unref(fypc_last->map.key);
				fypc_last->map.key = NULL;

				// DBG(fypp, "%s: %s KEY %s\n", __func__, fy_path_get_text0(fypp), fy_token_get_scalar_path_key0(fyt) ? : "<null>");
				is_complete = false;
				break;
			}
			break;

		case FYPCT_SEQ:
			if (fypc_last->seq.idx < 0) {
				fypc_last->seq.idx = 0;
				fypc_last->seq.bufidx = -1;
			} else
				fypc_last->seq.idx++;
			// DBG(fypp, "%s: %s SEQ next %d\n", __func__, fy_path_get_text0(fypp), fypc_last->seq.idx);
			break;

		case FYPCT_NONE:
			assert(0);
			break;
		}
	}

	/* not complete yet */
	if (!is_complete) {
		// DBG(fypp, "%s: not-complete\n", fy_path_get_text0(fypp));
		return 0;
	}

	/* collection start */
	if (is_collection && is_start) {
		fypc = is_map ? fy_path_component_create_mapping(fypp) : fy_path_component_create_sequence(fypp);
		assert(fypc);

		if (tag)
			fy_path_component_set_tag(fypc, tag);
		if (anchor)
			fy_path_component_set_anchor(fypc, anchor);

		/* append to the tail */
		fy_path_component_list_add_tail(&fypp->components, fypc);
		fypp->seq++;

		if (fypc_last && fypc_last->type == FYPCT_MAP && fypc_last->map.got_key) {
			// DBG(fypp, "%s: clear got_key %p\n", fy_path_get_text0(fypp), fypc_last);
			fypc_last->map.got_key = false;
		}

		// DBG(fypp, "%s: push %p\n", fy_path_get_text0(fypp), fypc);
	}

	if (!is_collection) {
		// DBG(fypp, "%s: scalar %s\n", fy_path_get_text0(fypp), fy_token_get_scalar_path_key0(fyt) ? : "<null>");
		if (fypc_last && fypc_last->type == FYPCT_MAP && fypc_last->map.got_key) {
			// DBG(fypp, "%s: clear got_key %p\n", fy_path_get_text0(fypp), fypc_last);
			fypc_last->map.got_key = false;
		}

	}

	/* collection end */
	if (is_collection && !is_start) {

		// DBG(fypp, "%s: pop %p\n", fy_path_get_text0(fypp), fypc_last);

		fypc = fy_path_component_list_pop_tail(&fypp->components);
		assert(fypc);
		fypp->seq++;

		/* and destroy it */
		fy_path_component_destroy(fypc_last);
	}

complete:
	fypc_last = fy_path_component_list_tail(&fypp->components);
	if (fypc_last && is_end) {

		// DBG(fypp, "%s: cleanup %p\n", fy_path_get_text0(fypp), fypc_last);

		switch (fypc_last->type) {
		case FYPCT_MAP:
			fy_token_unref(fypc_last->map.key);
			fy_document_destroy(fypc_last->map.fyd);
			fypc_last->map.key = NULL;
			fypc_last->map.fyd = NULL;
			break;

		case FYPCT_SEQ:
			break;

		case FYPCT_NONE:
			assert(0);
			break;
		}
	}

	// DBG(fypp, "%s: exit\n", fy_path_get_text0(fypp));

	return 0;
}

