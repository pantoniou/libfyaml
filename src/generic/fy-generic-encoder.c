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

	return fyge;
}

void fy_generic_encoder_destroy(struct fy_generic_encoder *fyge)
{
	if (!fyge)
		return;

	(void)fy_generic_encoder_sync(fyge);

	free(fyge);
}

int fy_encode_generic(struct fy_generic_encoder *fyge, fy_generic v);

struct fy_encode_generic_data {
	struct fy_generic_indirect gi FY_GENERIC_CONTAINER_ALIGNMENT;
	struct fy_generic v;
	const char *anchor;
	const char *tag;
	const char *comment;
	int style;
	/* when shortening */
	const char *tag_handle;
	size_t tag_handle_size;
	const char *tag_suffix;
	size_t tag_suffix_size;
	char tag_buf_local[128];
	char *tag_buf_alloc;
} FY_GENERIC_CONTAINER_ALIGNMENT;

static void
fy_encode_generic_get_data(struct fy_generic_encoder *fyge, fy_generic v,
			   struct fy_encode_generic_data *gd)
{
	int r, len;

	memset(gd, 0, sizeof(*gd));
	gd->style = -1;

	gd->v = v;
	if (fy_generic_is_direct(v))
		return;

	fy_generic_indirect_get(v, &gd->gi);
	gd->anchor = fy_castp(&gd->gi.anchor, (const char *)NULL);
	gd->tag = fy_castp(&gd->gi.tag, (const char *)NULL);
	gd->comment = fy_castp(&gd->gi.comment, (const char *)NULL);
	gd->style = fy_cast(gd->gi.style, -1);

	/* shorten the tag if possible */
	if (gd->tag) {
		r = fy_document_state_shorten_tag(fy_emitter_get_document_state(fyge->emit), gd->tag, FY_NT,
				&gd->tag_handle, &gd->tag_handle_size,
				&gd->tag_suffix, &gd->tag_suffix_size);
		if (!r) {
			len = snprintf(gd->tag_buf_local, sizeof(gd->tag_buf_local), "%.*s%.*s",
					(int)gd->tag_handle_size, gd->tag_handle,
					(int)gd->tag_suffix_size, gd->tag_suffix);
			if (len < (int)sizeof(gd->tag_buf_local)) {
				gd->tag = gd->tag_buf_local;
			} else {
				len = asprintf(&gd->tag_buf_alloc, "%.*s%.*s",
					(int)gd->tag_handle_size, gd->tag_handle,
					(int)gd->tag_suffix_size, gd->tag_suffix);
				if (len >= 0) {
					gd->tag = gd->tag_buf_alloc;
				} else {
					/* XXX something very funky, but not fatal */
					gd->tag_buf_alloc = NULL;
				}
			}
		} else {
			gd->tag_handle = gd->tag_suffix = NULL;
			gd->tag_handle_size = gd->tag_suffix_size = 0;
		}
	}
}

static void
fy_encode_generic_cleanup_data(struct fy_encode_generic_data *gd)
{
	if (!gd)
		return;
	if (gd->tag_buf_alloc)
		free(gd->tag_buf_alloc);
}

static int
fy_encode_generic_attach_comments(struct fy_generic_encoder *fyge,
				  struct fy_encode_generic_data *gd,
				  struct fy_event *fye)
{
	struct fy_token *fyt;
	int rc;

	if (!fye)
		return -1;

	if (!gd->comment)
		return 0;

	fyt = fy_event_get_token(fye);
	if (!fyt)
		return -1;

	rc = fy_token_set_comment(fyt, fycp_top, gd->comment, FY_NT);
	return rc;
}

struct fy_event *fy_encode_generic_null_event(struct fy_generic_encoder *fyge, fy_generic v)
{
	struct fy_encode_generic_data gd_local, *gd = &gd_local;
	struct fy_event *fye;

	fy_encode_generic_get_data(fyge, v, gd);
	fye = fy_emit_event_create(fyge->emit, FYET_SCALAR, FYSS_PLAIN, "null", 4, gd->anchor, gd->tag);
	(void)fy_encode_generic_attach_comments(fyge, gd, fye);
	fy_encode_generic_cleanup_data(gd);

	return fye;
}

int fy_encode_generic_null(struct fy_generic_encoder *fyge, fy_generic v)
{
	struct fy_event *fye;

	fye = fy_encode_generic_null_event(fyge, v);
	return fy_emit_event(fyge->emit, fye);
}

struct fy_event *fy_encode_generic_bool_event(struct fy_generic_encoder *fyge, fy_generic v)
{
	struct fy_encode_generic_data gd_local, *gd = &gd_local;
	struct fy_event *fye;
	const char *text;
	size_t sz;
	bool bv;

	fy_encode_generic_get_data(fyge, v, gd);

	bv = fy_cast(v, (_Bool)false);

	if (bv) {
		text = "true";
		sz = 4;
	} else {
		text = "false";
		sz = 5;
	}

	fye = fy_emit_event_create(fyge->emit, FYET_SCALAR, FYSS_PLAIN, text, sz, gd->anchor, gd->tag);
	(void)fy_encode_generic_attach_comments(fyge, gd, fye);

	fy_encode_generic_cleanup_data(gd);

	return fye;
}

int fy_encode_generic_bool(struct fy_generic_encoder *fyge, fy_generic v)
{
	struct fy_event *fye;
	fye = fy_encode_generic_bool_event(fyge, v);
	return fy_emit_event(fyge->emit, fye);
}

struct fy_event *fy_encode_generic_int_event(struct fy_generic_encoder *fyge, fy_generic v)
{
	struct fy_encode_generic_data gd_local, *gd = &gd_local;
	struct fy_event *fye;
	fy_generic_decorated_int dint;
	char buf[32];
	int len;

	fy_encode_generic_get_data(fyge, v, gd);

	dint = fy_cast(v, fy_dint_empty);

	if (!(dint.flags & FYGDIF_UNSIGNED_RANGE_EXTEND))
		len = snprintf(buf, sizeof(buf), "%lld", dint.sv);
	else
		len = snprintf(buf, sizeof(buf), "%llu", dint.uv);
	buf[sizeof(buf) - 1] = '\0';
	fye = fy_emit_event_create(fyge->emit, FYET_SCALAR, FYSS_PLAIN,
			buf, (size_t)len, gd->anchor, gd->tag);
	(void)fy_encode_generic_attach_comments(fyge, gd, fye);

	fy_encode_generic_cleanup_data(gd);

	return fye;
}

int fy_encode_generic_int(struct fy_generic_encoder *fyge, fy_generic v)
{
	struct fy_event *fye;
	fye = fy_encode_generic_int_event(fyge, v);
	return fy_emit_event(fyge->emit, fye);
}

struct fy_event *fy_encode_generic_float_event(struct fy_generic_encoder *fyge, fy_generic v)
{
	struct fy_encode_generic_data gd_local, *gd = &gd_local;
	struct fy_event *fye;
	double f;
	char buf[32];
	int len;

	fy_encode_generic_get_data(fyge, v, gd);

	f = fy_cast(v, (double)NAN);

	if (isfinite(f))
		len = snprintf(buf, sizeof(buf), "%g", f);
	else if (isnan(f))
		len = snprintf(buf, sizeof(buf), ".nan");
	else if (isinf(f) > 0)
		len = snprintf(buf, sizeof(buf), ".inf");
	else
		len = snprintf(buf, sizeof(buf), "-.inf");
	buf[sizeof(buf) - 1] = '\0';
	fye = fy_emit_event_create(fyge->emit, FYET_SCALAR, FYSS_PLAIN, buf, (size_t)len, gd->anchor, gd->tag);
	(void)fy_encode_generic_attach_comments(fyge, gd, fye);

	fy_encode_generic_cleanup_data(gd);

	return fye;
}

int fy_encode_generic_float(struct fy_generic_encoder *fyge, fy_generic v)
{
	struct fy_event *fye;
	fye = fy_encode_generic_float_event(fyge, v);
	return fy_emit_event(fyge->emit, fye);
}

struct fy_event *fy_encode_generic_string_event(struct fy_generic_encoder *fyge, fy_generic v)
{
	struct fy_encode_generic_data gd_local, *gd = &gd_local;
	struct fy_event *fye;
	fy_generic_sized_string szstr;
	enum fy_scalar_style ss;

	fy_encode_generic_get_data(fyge, v, gd);

	szstr = fy_cast(v, fy_szstr_empty);

	ss = FYSS_ANY;
	if (gd->style >= 0 && gd->style < FYSS_MAX)
		ss = (enum fy_scalar_style)gd->style;

	fye = fy_emit_event_create(fyge->emit, FYET_SCALAR, ss, szstr.data, szstr.size, gd->anchor, gd->tag);
	(void)fy_encode_generic_attach_comments(fyge, gd, fye);

	fy_encode_generic_cleanup_data(gd);

	return fye;
}

int fy_encode_generic_string(struct fy_generic_encoder *fyge, fy_generic v)
{
	struct fy_event *fye;
	fye = fy_encode_generic_string_event(fyge, v);
	return fy_emit_event(fyge->emit, fye);
}

int fy_encode_generic_sequence(struct fy_generic_encoder *fyge, fy_generic v)
{
	struct fy_encode_generic_data gd_local, *gd = &gd_local;
	enum fy_collection_style cs;
	enum fy_node_style ns;
	struct fy_event *fye = NULL;
	const fy_generic *items;
	size_t i, count;
	int rc;

	fy_encode_generic_get_data(fyge, v, gd);

	ns = FYNS_ANY;
	if (gd->style >= 0 && gd->style < FYCS_MAX) {
		cs = (enum fy_collection_style)gd->style;
		if (cs == FYCS_FLOW)
			ns = FYNS_FLOW;
		else if (cs == FYCS_BLOCK)
			ns = FYNS_BLOCK;
	}

	/* XXX takes FYNS... - should've taken FYCS */
	fye = fy_emit_event_create(fyge->emit, FYET_SEQUENCE_START, ns, gd->anchor, gd->tag);
	if (!fye)
		goto err_out;

	(void)fy_encode_generic_attach_comments(fyge, gd, fye);

	rc = fy_emit_event(fyge->emit, fye);
	if (rc)
		goto err_out;
	fye = NULL;

	items = fy_generic_sequence_get_items(v, &count);
	for (i = 0; i < count; i++) {
		rc = fy_encode_generic(fyge, items[i]);
		if (rc)
			goto err_out;
	}

	fye = fy_emit_event_create(fyge->emit, FYET_SEQUENCE_END);
	if (!fye)
		goto err_out;

	rc = fy_emit_event(fyge->emit, fye);
	if (rc)
		goto err_out;
	fye = NULL;
	rc = 0;
out:
	fy_encode_generic_cleanup_data(gd);
	return 0;

err_out:
	fy_emit_event_free(fyge->emit, fye);
	fye = NULL;
	rc = -1;
	goto out;
}

int fy_encode_generic_mapping(struct fy_generic_encoder *fyge, fy_generic v)
{
	struct fy_encode_generic_data gd_local, *gd = &gd_local;
	enum fy_collection_style cs;
	enum fy_node_style ns;
	struct fy_event *fye = NULL;
	const fy_generic_map_pair *pairs;
	size_t i, count;
	int rc;

	fy_encode_generic_get_data(fyge, v, gd);

	ns = FYNS_ANY;
	if (gd->style >= 0 && gd->style < FYCS_MAX) {
		cs = (enum fy_collection_style)gd->style;
		if (cs == FYCS_FLOW)
			ns = FYNS_FLOW;
		else if (cs == FYCS_BLOCK)
			ns = FYNS_BLOCK;
	}

	/* XXX takes FYNS... - should've taken FYCS */
	fye = fy_emit_event_create(fyge->emit, FYET_MAPPING_START, ns, gd->anchor, gd->tag);
	if (!fye)
		goto err_out;

	(void)fy_encode_generic_attach_comments(fyge, gd, fye);

	rc = fy_emit_event(fyge->emit, fye);
	if (rc)
		goto err_out;
	fye = NULL;

	pairs = fy_generic_mapping_get_pairs(v, &count);
	for (i = 0; i < count; i++) {
		rc = fy_encode_generic(fyge, pairs[i].key);
		if (rc)
			goto err_out;
		rc = fy_encode_generic(fyge, pairs[i].value);
		if (rc)
			goto err_out;
	}

	fye = fy_emit_event_create(fyge->emit, FYET_MAPPING_END);
	if (!fye)
		goto err_out;

	rc = fy_emit_event(fyge->emit, fye);
	if (rc)
		goto err_out;
	fye = NULL;
	rc = 0;
out:
	fy_encode_generic_cleanup_data(gd);

	return rc;

err_out:
	fy_emit_event_free(fyge->emit, fye);
	fye = NULL;
	rc = -1;
	goto out;
}

int fy_encode_generic_alias(struct fy_generic_encoder *fyge, fy_generic v)
{
	const char *str;

	str = fy_generic_get_alias_alloca(v);
	if (!str || !*str)
		return -1;

	return fy_emit_eventf(fyge->emit, FYET_ALIAS, str);
}

int fy_encode_generic(struct fy_generic_encoder *fyge, fy_generic v)
{
	switch (fy_generic_get_type(v)) {
	case FYGT_INVALID:
		return -1;	// invalid passed in

	case FYGT_NULL:
		return fy_encode_generic_null(fyge, v);

	case FYGT_BOOL:
		return fy_encode_generic_bool(fyge, v);

	case FYGT_INT:
		return fy_encode_generic_int(fyge, v);

	case FYGT_FLOAT:
		return fy_encode_generic_float(fyge, v);

	case FYGT_STRING:
		return fy_encode_generic_string(fyge, v);

	case FYGT_SEQUENCE:
		return fy_encode_generic_sequence(fyge, v);

	case FYGT_MAPPING:
		return fy_encode_generic_mapping(fyge, v);

	case FYGT_ALIAS:
		return fy_encode_generic_alias(fyge, v);

	default:
		fprintf(stderr, "impossible type %d\n", (int)fy_generic_get_type(v));
		FY_IMPOSSIBLE_ABORT();
	}

	FY_IMPOSSIBLE_ABORT();
	return -1;
}

static int
fy_generic_encoder_emit_document(struct fy_generic_encoder *fyge, fy_generic vroot, fy_generic vds)
{
	fy_generic_mapping_handle vds_map, maph;
	fy_generic_sequence_handle seqh;
	struct fy_version *vers = NULL, vers_local;
	struct fy_tag **tags = NULL, *tag;
	bool is_version_explicit, are_tags_explicit;
	int rc;
	size_t i, j;

	if (!fyge || fy_generic_is_invalid(vroot))
		return -1;

	/* must not emit stream end twice */
	if (fyge->emitted_stream_end)
		return -1;

	vers = NULL;
	tags = NULL;
	vds_map = fy_cast(vds, fy_map_handle_null);
	if (vds_map) {
		maph = fy_get_default(vds_map, "version", fy_map_handle_null);
		if (maph) {
			memset(&vers_local, 0, sizeof(*vers));
			vers_local.major = fy_get_default(maph, "major", -1);
			vers_local.minor = fy_get_default(maph, "minor", -1);
			vers = &vers_local;
			if (vers_local.major < 0 || vers_local.minor < 0)
				goto err_out;
		}

		seqh = fy_get_default(vds_map, "tags", fy_seq_handle_null);
		if (seqh) {
			tags = alloca((fy_len(seqh) + 1) * sizeof(*tags));
			j = 0;
			for (i = 0; i < fy_len(seqh); i++) {
				tag = alloca(sizeof(*tag));
				maph = fy_get_default(seqh, i, fy_map_handle_null);
				if (maph) {
					tag = alloca(sizeof(*tag));
					tag->handle = fy_get_default(maph, "handle", "");
					tag->prefix = fy_get_default(maph, "prefix", "");
					tags[j++] = tag;
				}
			}
			tags[j++] = NULL;
		}
		is_version_explicit = fy_get_default(vds_map, "version-explicit", false);
		are_tags_explicit = fy_get_default(vds_map, "tags-explicit", false);

		if (!is_version_explicit)
			vers = NULL;
		if (!are_tags_explicit)
			tags = NULL;
	}

	if (!fyge->emitted_stream_start) {
		rc = fy_emit_eventf(fyge->emit, FYET_STREAM_START);
		if (rc)
			goto err_out;
		fyge->emitted_stream_start = true;
	}

	rc = fy_emit_eventf(fyge->emit, FYET_DOCUMENT_START, 1, vers, tags);
	if (rc)
		goto err_out;

	rc = fy_encode_generic(fyge, vroot);
	if (rc)
		goto err_out;

	rc = fy_emit_eventf(fyge->emit, FYET_DOCUMENT_END, 1);
	if (rc)
		goto err_out;

	return 0;
err_out:
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
	fy_generic vroot, vds;
	size_t i, count;
	int rc;

	if ((emit_flags & FYGEEF_DISABLE_DIRECTORY)) {

		if ((emit_flags & FYGEEF_MULTI_DOCUMENT)) {
			if (!fy_generic_is_sequence(v))
				return -1;
			count = fy_generic_sequence_get_item_count(v);
			for (i = 0; i < count; i++) {
				vroot = fy_generic_sequence_get_item_generic(v, i);
				rc = fy_generic_encoder_emit_document(fyge, vroot, fy_null);
				if (rc)
					return rc;
			}
		} else {
			rc = fy_generic_encoder_emit_document(fyge, v, fy_null);
			if (rc)
				return rc;
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
				rc = fy_generic_encoder_emit_document(fyge, vroot, vds);
				if (rc)
					return rc;
			}
		} else {
			vroot = fy_generic_mapping_get_value(v, vroot_key);
			if (fy_generic_is_invalid(vroot))
				return -1;
			rc = fy_generic_encoder_emit_document(fyge, vroot, v);
			if (rc)
				return rc;
		}
	}
	return 0;
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
