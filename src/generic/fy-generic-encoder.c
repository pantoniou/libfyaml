/*
 * fy-generic-encoder.h - generic encoder (generic -> yaml)
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

#include "fy-diag.h"
#include "fy-emit.h"
#include "fy-docstate.h"

#include "fy-generic.h"
#include "fy-generic-encoder.h"

struct fy_generic_encoder *
fy_generic_encoder_create(struct fy_emitter *emit)
{
	struct fy_generic_encoder *fyge;

	if (!emit)
		return NULL;

	fyge = malloc(sizeof(*fyge));
	if (!fyge)
		return NULL;
	memset(fyge, 0, sizeof(*fyge));

	fyge->emit = emit;
	fyge->schema = FYGS_AUTO;

	return fyge;
}

void fy_generic_encoder_set_schema(struct fy_generic_encoder *fyge,
				   enum fy_generic_schema schema)
{
	if (!fyge || !fy_generic_schema_is_valid(schema))
		return;

	fyge->schema = schema;
}

void fy_generic_encoder_destroy(struct fy_generic_encoder *fyge)
{
	if (!fyge)
		return;

	(void)fy_generic_encoder_sync(fyge);

	free(fyge);
}

int fy_encode_generic(struct fy_generic_encoder *fyge, fy_generic v)
{
	struct fy_generic_iterator_cfg cfg;
	struct fy_generic_iterator *fygi = NULL;
	struct fy_event *fye = NULL;
	int rc;

	if (!fyge || fy_generic_is_invalid(v))
		return -1;

	if (fyge->emitted_stream_end)
		return -1;

	if (!fyge->emitted_stream_start) {
		rc = fy_emit_eventf(fyge->emit, FYET_STREAM_START);
		if (rc)
			return -1;
		fyge->emitted_stream_start = true;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = FYGICF_WANT_DOCUMENT_BODY_EVENTS | FYGICF_HAS_FULL_DIRECTORY;
	cfg.vdir = fy_local_mapping("root", v);
	cfg.schema = fyge->schema;

	fygi = fy_generic_iterator_create_cfg(&cfg);
	if (!fygi)
		return -1;

	while ((fye = fy_generic_iterator_generate_emit_next(fygi, fyge->emit)) != NULL) {
		rc = fy_emit_event(fyge->emit, fye);
		if (rc)
			goto err_out;
	}

	rc = fy_generic_iterator_get_error(fygi) ? -1 : 0;
	fy_generic_iterator_destroy(fygi);

	return rc;

err_out:
	fy_generic_iterator_destroy(fygi);
	return -1;
}

static int
fy_generic_encoder_emit_vdir(struct fy_generic_encoder *fyge, fy_generic vdir)
{
	struct fy_generic_iterator_cfg cfg;
	struct fy_generic_iterator *fygi = NULL;
	struct fy_event *fye = NULL;
	int rc;

	if (!fyge || fy_generic_is_invalid(vdir))
		return -1;

	/* must not emit stream end twice */
	if (fyge->emitted_stream_end)
		return -1;

	if (!fyge->emitted_stream_start) {
		rc = fy_emit_eventf(fyge->emit, FYET_STREAM_START);
		if (rc)
			goto err_out;
		fyge->emitted_stream_start = true;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = FYGICF_WANT_DOCUMENT_BODY_EVENTS | FYGICF_HAS_FULL_DIRECTORY;
	cfg.vdir = vdir;
	cfg.schema = fyge->schema;

	fygi = fy_generic_iterator_create_cfg(&cfg);
	if (!fygi)
		goto err_out;

	while ((fye = fy_generic_iterator_generate_emit_next(fygi, fyge->emit)) != NULL) {
		rc = fy_emit_event(fyge->emit, fye);
		if (rc)
			goto err_out;
	}

	rc = fy_generic_iterator_get_error(fygi) ? -1 : 0;
	fy_generic_iterator_destroy(fygi);

	return rc;
err_out:
	fy_generic_iterator_destroy(fygi);
	return -1;
}

int fy_generic_encoder_sync(struct fy_generic_encoder *fyge)
{
	int rc;

	if (!fyge)
		return -1;

	/* if we have done a stream start and no stream do it now */
	if (fyge->emitted_stream_start && !fyge->emitted_stream_end) {
		rc = fy_emit_eventf(fyge->emit, FYET_STREAM_END);
		if (rc)
			return -1;
		fyge->emitted_stream_end = true;
	}

	return 0;
}

int fy_generic_encoder_emit(struct fy_generic_encoder *fyge,
			    enum fy_generic_encoder_emit_flags emit_flags, fy_generic v)
{
	fy_generic vroot, vds, vdir;
	fy_generic *docs;
	size_t i, count;

	if ((emit_flags & FYGEEF_DISABLE_DIRECTORY)) {

		if ((emit_flags & FYGEEF_MULTI_DOCUMENT)) {
			if (!fy_generic_is_sequence(v))
				return -1;
			count = fy_generic_sequence_get_item_count(v);
			if (!count)
				return 0;
			docs = alloca(sizeof(*docs) * count);
			for (i = 0; i < count; i++) {
				vroot = fy_generic_sequence_get_item_generic(v, i);
				docs[i] = fy_local_mapping("root", vroot);
			}
			vdir = fy_local_sequence_create(count, docs);
		} else {
			vdir = fy_local_mapping("root", v);
		}
	} else {
		const fy_generic vroot_key = fy_string("root");

		if ((emit_flags & FYGEEF_MULTI_DOCUMENT)) {
			if (!fy_generic_is_sequence(v))
				return -1;
			count = fy_generic_sequence_get_item_count(v);
			for (i = 0; i < count; i++) {
				vds = fy_generic_sequence_get_item_generic(v, i);
				vroot = fy_generic_mapping_get_value(vds, vroot_key);
				if (fy_generic_is_invalid(vroot))
					return -1;
			}
			vdir = v;
		} else {
			vroot = fy_generic_mapping_get_value(v, vroot_key);
			if (fy_generic_is_invalid(vroot))
				return -1;
			vdir = v;
		}
	}

	return fy_generic_encoder_emit_vdir(fyge, vdir);
}

int fy_generic_emit(fy_generic v, enum fy_emitter_cfg_flags flags)
{
	struct fy_emitter_cfg ecfg_default = {
		.flags = flags,
	};
	struct fy_emitter *emit = NULL;
	struct fy_generic_encoder *fyge = NULL;
	int rc = -1;

	emit = fy_emitter_create(&ecfg_default);
	if (!emit)
		goto out;

	fyge = fy_generic_encoder_create(emit);
	if (!fyge)
		goto out;

	rc = fy_generic_encoder_emit(fyge, FYGEEF_DISABLE_DIRECTORY, v);
	if (rc)
		goto out;

	fy_generic_encoder_sync(fyge);
out:
	fy_generic_encoder_destroy(fyge);
	fy_emitter_destroy(emit);

	return rc;
}

int fy_generic_emit_compact(fy_generic v)
{
	return fy_generic_emit(v, FYECF_WIDTH_INF | FYECF_MODE_FLOW_ONELINE | FYECF_STRIP_DOC);
}

int fy_generic_emit_default(fy_generic v)
{
	// return fy_generic_emit(v, FYECF_DEFAULT);
	return fy_generic_emit_compact(v);
}

char *fy_generic_emit_to_string(fy_generic v, enum fy_emitter_cfg_flags flags, size_t *sizep)
{
	struct fy_emitter *emit = NULL;
	struct fy_generic_encoder *fyge = NULL;
	size_t tmp_size;
	int rc = -1;
	char *buf = NULL;

	if (!sizep)
		sizep = &tmp_size;

	*sizep = 0;

	emit = fy_emit_to_string(flags);
	if (!emit)
		goto out;

	fyge = fy_generic_encoder_create(emit);
	if (!fyge)
		goto out;

	rc = fy_generic_encoder_emit(fyge, FYGEEF_DISABLE_DIRECTORY, v);
	if (rc)
		goto out;

	fy_generic_encoder_sync(fyge);

	buf = fy_emit_to_string_collect(emit, sizep);
out:
	fy_generic_encoder_destroy(fyge);
	fy_emitter_destroy(emit);

	return buf;
}

char *fy_generic_emit_to_string_compact(fy_generic v, size_t *sizep)
{
	return fy_generic_emit_to_string(v,
			FYECF_WIDTH_INF | FYECF_MODE_FLOW_ONELINE |
			FYECF_STRIP_DOC | FYECF_NO_ENDING_NEWLINE, sizep);
}
