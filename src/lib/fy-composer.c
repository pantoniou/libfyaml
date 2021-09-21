/*
 * fy-composer.c - Composer support
 *
 * Copyright (c) 2021 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
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
