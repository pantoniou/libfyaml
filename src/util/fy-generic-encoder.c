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

int fy_generic_encoder_emit(struct fy_generic_encoder *fyge, fy_generic root, fy_generic vds)
{
	return 0;
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

	if (fy_generic_is_indirect(v))
		v = fy_generic_indirect_get_value(v);
	if (v == fy_true) {
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
	if (fy_generic_is_indirect(v))
		v = fy_generic_indirect_get_value(v);
	return fy_emit_scalar_printf(fyge->emit, FYSS_PLAIN, anchor, tag, "%lld",
			fy_generic_get_int(v));
}

int fy_encode_generic_float(struct fy_generic_encoder *fyge, const char *anchor, const char *tag, fy_generic v)
{
	double f;

	if (fy_generic_is_indirect(v))
		v = fy_generic_indirect_get_value(v);
	f = fy_generic_get_float(v);
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

	if (fy_generic_is_indirect(v))
		v = fy_generic_indirect_get_value(v);
	str = fy_generic_get_string_size(&v, &len);
	return fy_emit_scalar_write(fyge->emit, FYSS_ANY, anchor, tag, str, len);
}

int fy_encode_generic_sequence(struct fy_generic_encoder *fyge, const char *anchor, const char *tag, fy_generic v)
{
	const fy_generic *items;
	size_t i, count;
	int rc;

	if (fy_generic_is_indirect(v))
		v = fy_generic_indirect_get_value(v);
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
	const fy_generic *pairs;
	size_t i, count;
	int rc;

	if (fy_generic_is_indirect(v))
		v = fy_generic_indirect_get_value(v);
	rc = fy_emit_eventf(fyge->emit, FYET_MAPPING_START, FYNS_ANY, anchor, tag);
	if (rc)
		goto err_out;

	pairs = fy_generic_mapping_get_pairs(v, &count);
	count *= 2;
	for (i = 0; i < count; i++) {
		rc = fy_encode_generic(fyge, pairs[i]);
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
	return fy_emit_eventf(fyge->emit, FYET_ALIAS, fy_generic_get_alias(v));
}

int fy_encode_generic(struct fy_generic_encoder *fyge, fy_generic v)
{
	struct fy_generic_indirect gi;
	const char *anchor = NULL, *tag = NULL;

	if (fy_generic_is_indirect(v)) {
		fy_generic_indirect_get(v, &gi);
		if (fy_generic_get_type(gi.anchor) == FYGT_STRING)
			anchor = fy_generic_get_string(&gi.anchor);
		if (fy_generic_get_type(gi.tag) == FYGT_STRING)
			tag = fy_generic_get_string(&gi.tag);
	}

	switch (fy_generic_get_type(v)) {
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
		assert(0);
		break;
	}

	abort();	/* not yet */
	return -1;
}

int fy_generic_encoder_emit_document(struct fy_generic_encoder *fyge, fy_generic vroot, fy_generic vds)
{
	const fy_generic *vversionp, *vmajorp, *vminorp, *vtagsp, *vhandlep, *vprefixp;
	const fy_generic *items;
	struct fy_version *vers = NULL, vers_local;
	struct fy_tag **tags = NULL, *tag;
	bool version_explicit, tags_explicit;
	const fy_generic *version_explicitp, *tags_explicitp;
	int rc;
	size_t i, j, count;

	if (!fyge || vroot == fy_invalid)
		return -1;

	/* must not emit stream end twice */
	if (fyge->emitted_stream_end)
		return -1;

	/* the document state must be a mapping */
	if (vds != fy_invalid && fy_generic_get_type(vds) == FYGT_MAPPING) {
		vversionp = fy_generic_mapping_lookup(vds, fy_string("version"));

		if (vversionp) {
			vmajorp = fy_generic_mapping_lookup(*vversionp, fy_string("major"));
			if (!vmajorp || fy_generic_get_type(*vmajorp) != FYGT_INT)
				goto err_out;
			vminorp = fy_generic_mapping_lookup(*vversionp, fy_string("minor"));
			if (!vminorp || fy_generic_get_type(*vminorp) != FYGT_INT)
				goto err_out;

			memset(&vers_local, 0, sizeof(*vers));
			vers_local.major = fy_generic_get_int(*vmajorp);
			vers_local.minor = fy_generic_get_int(*vminorp);
			vers = &vers_local;
		}

		vtagsp = fy_generic_mapping_lookup(vds, fy_string("tags"));
		if (vtagsp && fy_generic_get_type(*vtagsp) == FYGT_SEQUENCE) {
			items = fy_generic_sequence_get_items(*vtagsp, &count);

			tags = alloca((count + 1) * sizeof(*tags));
			j = 0;
			for (i = 0; i < count; i++) {
				vhandlep = fy_generic_mapping_lookup(items[i], fy_string("handle"));
				vprefixp = fy_generic_mapping_lookup(items[i], fy_string("prefix"));

				if (vhandlep && fy_generic_get_type(*vhandlep) == FYGT_STRING &&
				    vprefixp && fy_generic_get_type(*vprefixp) == FYGT_STRING) {

					tag = alloca(sizeof(*tag));
					tag->handle = fy_generic_get_string(vhandlep);
					tag->prefix = fy_generic_get_string(vprefixp);
					tags[j++] = tag;
				}
			}
			if (j > 0)
				tags[j++] = NULL;
			else
				tags = NULL;
		}

		version_explicitp = fy_generic_mapping_lookup(vds, fy_string("version-explicit"));
		version_explicit = version_explicitp && *version_explicitp == fy_true;
		tags_explicitp = fy_generic_mapping_lookup(vds, fy_string("tags-explicit"));
		tags_explicit = tags_explicitp && *tags_explicitp == fy_true;

		if (!version_explicit)
			vers = NULL;
		if (!tags_explicit)
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

int fy_generic_encoder_emit_all_documents(struct fy_generic_encoder *fyge, fy_generic vdir)
{
	size_t i, count;
	const fy_generic *items;
	fy_generic vroot_key, vdocs_key;
	const fy_generic *vrootp, *vdocsp;
	int rc;

	/* must be a sequence */
	if (fy_generic_get_type(vdir) != FYGT_SEQUENCE)
		return -1;

	vroot_key = fy_string("root");
	vdocs_key = fy_string("docs");

	/* no documents? nothing to emit */
	items = fy_generic_sequence_get_items(vdir, &count);
	if (!items || !count)
		return 0;

	for (i = 0; i < count; i++) {

		vrootp = fy_generic_mapping_lookup(items[i], vroot_key);
		if (!vrootp)
			return -1;

		vdocsp = fy_generic_mapping_lookup(items[i], vdocs_key);
		if (!vdocsp)
			return -1;

		rc = fy_generic_encoder_emit_document(fyge, *vrootp, *vdocsp);
		if (rc)
			return rc;
	}

	return 0;
}
