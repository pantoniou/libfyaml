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

#include "fy-generic.h"
#include "fy-generic-encoder.h"

struct fy_generic_encoder *
fy_generic_encoder_create(struct fy_emitter *emit, bool verbose)
{
	struct fy_generic_encoder *fyge;

	if (!emit)
		return NULL;

	fyge = malloc(sizeof(*fyge));
	if (!fyge)
		return NULL;
	memset(fyge, 0, sizeof(*fyge));

	fyge->emit = emit;
	fyge->verbose = verbose;

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

int fy_encode_generic_null(struct fy_generic_encoder *fyge, const char *anchor, const char *tag, fy_generic v)
{
	return fy_emit_scalar_printf(fyge->emit, FYSS_PLAIN, anchor, tag, "null");
}

int fy_encode_generic_bool(struct fy_generic_encoder *fyge, const char *anchor, const char *tag, fy_generic v)
{
	const char *text;
	size_t sz;

	if (v.v == fy_true_value) {
		text = "true";
		sz = 4;
	} else {
		text = "false";
		sz = 5;
	}
	return fy_emit_scalar_write(fyge->emit, FYSS_PLAIN, anchor, tag, text, sz);
}

int fy_encode_generic_int(struct fy_generic_encoder *fyge, const char *anchor, const char *tag, fy_generic v)
{
	fy_generic_decorated_int dint = fy_generic_cast(v, fy_generic_decorated_int);

	if (!dint.is_unsigned)
		return fy_emit_scalar_printf(fyge->emit, FYSS_PLAIN, anchor, tag, "%lld", dint.sv);
	else
		return fy_emit_scalar_printf(fyge->emit, FYSS_PLAIN, anchor, tag, "%llu", dint.uv);
}

int fy_encode_generic_float(struct fy_generic_encoder *fyge, const char *anchor, const char *tag, fy_generic v)
{
	double f;

	f = fy_generic_cast(v, double);
	if (isfinite(f))
		return fy_emit_scalar_printf(fyge->emit, FYSS_PLAIN, anchor, tag, "%g", f);

	if (isnan(f))
		return fy_emit_scalar_printf(fyge->emit, FYSS_PLAIN, anchor, tag, ".nan");

	if (isinf(f) > 0)
		return fy_emit_scalar_printf(fyge->emit, FYSS_PLAIN, anchor, tag, ".inf");

	return fy_emit_scalar_printf(fyge->emit, FYSS_PLAIN, anchor, tag, "-.inf");
}

int fy_encode_generic_string(struct fy_generic_encoder *fyge, const char *anchor, const char *tag, fy_generic v)
{
	const char *str;
	size_t len;

	str = fy_generic_get_string_size(v, &len);
	return fy_emit_scalar_write(fyge->emit, FYSS_ANY, anchor, tag, str, len);
}

int fy_encode_generic_sequence(struct fy_generic_encoder *fyge, const char *anchor, const char *tag, fy_generic v)
{
	const fy_generic *items;
	size_t i, count;
	int rc;

	rc = fy_emit_eventf(fyge->emit, FYET_SEQUENCE_START, FYNS_ANY, anchor, tag);
	if (rc)
		goto err_out;

	items = fy_generic_sequence_get_items(v, &count);
	for (i = 0; i < count; i++) {
		rc = fy_encode_generic(fyge, items[i]);
		if (rc)
			goto err_out;
	}

	rc = fy_emit_eventf(fyge->emit, FYET_SEQUENCE_END);
	if (rc)
		goto err_out;

	return 0;
err_out:
	return -1;
}

int fy_encode_generic_mapping(struct fy_generic_encoder *fyge, const char *anchor, const char *tag, fy_generic v)
{
	const fy_generic_map_pair *pairs;
	size_t i, count;
	int rc;

	if (fy_generic_is_indirect(v))
		v = fy_generic_indirect_get_value(v);
	rc = fy_emit_eventf(fyge->emit, FYET_MAPPING_START, FYNS_ANY, anchor, tag);
	if (rc)
		goto err_out;

	pairs = fy_generic_mapping_get_pairs(v, &count);
	for (i = 0; i < count; i++) {
		rc = fy_encode_generic(fyge, pairs[i].key);
		if (rc)
			goto err_out;
		rc = fy_encode_generic(fyge, pairs[i].value);
		if (rc)
			goto err_out;
	}
	rc = fy_emit_eventf(fyge->emit, FYET_MAPPING_END);
	if (rc)
		goto err_out;

	return 0;
err_out:
	return -1;
}

int fy_encode_generic_alias(struct fy_generic_encoder *fyge, fy_generic v)
{
	const char *str;

	str = fy_generic_get_alias(v);
	if (!str || !*str)
		return -1;

	return fy_emit_eventf(fyge->emit, FYET_ALIAS, str);
}

int fy_encode_generic(struct fy_generic_encoder *fyge, fy_generic v)
{
	fy_generic_indirect gi;
	const char *anchor = NULL, *tag = NULL;

	if (fy_generic_is_indirect(v)) {
		fy_generic_indirect_get(v, &gi);
		if (fy_generic_get_type(gi.anchor) == FYGT_STRING)
			anchor = fy_generic_get_string(gi.anchor);
		if (fy_generic_get_type(gi.tag) == FYGT_STRING)
			tag = fy_generic_get_string(gi.tag);
	}

	switch (fy_generic_get_type(v)) {
	case FYGT_INVALID:
		return -1;	// invalid passed in

	case FYGT_NULL:
		return fy_encode_generic_null(fyge, anchor, tag, v);

	case FYGT_BOOL:
		return fy_encode_generic_bool(fyge, anchor, tag, v);

	case FYGT_INT:
		return fy_encode_generic_int(fyge, anchor, tag, v);

	case FYGT_FLOAT:
		return fy_encode_generic_float(fyge, anchor, tag, v);

	case FYGT_STRING:
		return fy_encode_generic_string(fyge, anchor, tag, v);

	case FYGT_SEQUENCE:
		return fy_encode_generic_sequence(fyge, anchor, tag, v);

	case FYGT_MAPPING:
		return fy_encode_generic_mapping(fyge, anchor, tag, v);

	case FYGT_ALIAS:
		return fy_encode_generic_alias(fyge, v);

	default:
		fprintf(stderr, "impossible type %d\n", (int)fy_generic_get_type(v));
		FY_IMPOSSIBLE_ABORT();
	}

	abort();	/* not yet */
	return -1;
}

static int
fy_generic_encoder_emit_document(struct fy_generic_encoder *fyge, fy_generic vroot, fy_generic vds)
{
	fy_generic vversion, vmajor, vminor, vtags; //, vhandle, vprefix;
	const fy_generic *items;
	struct fy_version *vers = NULL, vers_local;
	struct fy_tag **tags = NULL, *tag;
	bool is_version_explicit, are_tags_explicit;
	fy_generic version_explicit, tags_explicit;
	int rc;
	size_t i, j, count;

	if (!fyge || vroot.v == fy_invalid_value)
		return -1;

	/* must not emit stream end twice */
	if (fyge->emitted_stream_end)
		return -1;

	if (fy_generic_is_mapping(vds)) {
		vversion = fy_generic_mapping_get_value(vds, fy_string("version"));

		if (fy_generic_is_mapping(vversion)) {
			vmajor = fy_generic_mapping_get_value(vversion, fy_string("major"));
			if (!fy_generic_is_int(vmajor))
				goto err_out;
			vminor = fy_generic_mapping_get_value(vversion, fy_string("minor"));
			if (!fy_generic_is_int(vminor))
				goto err_out;

			memset(&vers_local, 0, sizeof(*vers));
			vers_local.major = fy_generic_cast(vmajor, int);
			vers_local.minor = fy_generic_cast(vminor, int);
			vers = &vers_local;
		}

		vtags = fy_generic_mapping_get_value(vds, fy_string("tags"));
		if (fy_generic_is_sequence(vtags)) {
			items = fy_generic_sequence_get_items(vtags, &count);

			tags = alloca((count + 1) * sizeof(*tags));
			j = 0;
			for (i = 0; i < count; i++) {

				fy_generic *vhandle_prefix = alloca(sizeof(fy_generic) * 2);

				vhandle_prefix[0] = fy_generic_mapping_get_value(items[i], fy_string("handle"));
				vhandle_prefix[1] = fy_generic_mapping_get_value(items[i], fy_string("prefix"));

				if (!fy_generic_is_string(vhandle_prefix[0]) || !fy_generic_is_string(vhandle_prefix[1]))
					goto err_out;

				tag = alloca(sizeof(*tag));
				tag->handle = fy_generic_get_string(vhandle_prefix[0]);
				tag->prefix = fy_generic_get_string(vhandle_prefix[1]);

				tags[j++] = tag;
			}
			if (j > 0)
				tags[j++] = NULL;
			else
				tags = NULL;
		}

		version_explicit = fy_generic_mapping_get_value(vds, fy_string("version-explicit"));
		is_version_explicit = version_explicit.v == fy_true_value;
		tags_explicit = fy_generic_mapping_get_value(vds, fy_string("tags-explicit"));
		are_tags_explicit = tags_explicit.v == fy_true_value;

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

	rc = fy_emit_eventf(fyge->emit, FYET_DOCUMENT_START, 0, vers, tags);
	if (rc)
		goto err_out;

	rc = fy_encode_generic(fyge, vroot);
	if (rc)
		goto err_out;

	rc = fy_emit_eventf(fyge->emit, FYET_DOCUMENT_END, 0);
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
				if (vroot.v == fy_invalid_value)
					return -1;
				rc = fy_generic_encoder_emit_document(fyge, vroot, vds);
				if (rc)
					return rc;
			}
		} else {
			vroot = fy_generic_mapping_get_value(v, vroot_key);
			if (vroot.v == fy_invalid_value)
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

	fyge = fy_generic_encoder_create(emit, false);
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

