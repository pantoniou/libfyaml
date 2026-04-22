/*
 * fy-generic-decoder.c - generic decoder (yaml -> generic)
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
#include <assert.h>

#include <libfyaml.h>

#include "fy-parse.h"
#include "fy-generic.h"
#include "fy-generic-decoder.h"

static enum fy_generic_document_builder_flags
fy_generic_decoder_to_builder_flags(enum fy_generic_decoder_parse_flags flags)
{
	enum fy_generic_document_builder_flags builder_flags = 0;

	if (flags & FYGDPF_DISABLE_DIRECTORY)
		builder_flags |= FYGDBF_DISABLE_DIRECTORY;
	if (flags & FYGDPF_TRACE)
		builder_flags |= FYGDBF_TRACE;
	if (flags & FYGDPF_KEEP_COMMENTS)
		builder_flags |= FYGDBF_KEEP_COMMENTS;
	if (flags & FYGDPF_CREATE_MARKERS)
		builder_flags |= FYGDBF_CREATE_MARKERS;
	if (flags & FYGDPF_PYYAML_COMPAT)
		builder_flags |= FYGDBF_PYYAML_COMPAT;
	if (flags & FYGDPF_KEEP_STYLE)
		builder_flags |= FYGDBF_KEEP_STYLE;
	if (flags & FYGDPF_KEEP_FAILSAFE_STR)
		builder_flags |= FYGDBF_KEEP_FAILSAFE_STR;

	return builder_flags;
}

void fy_generic_decoder_destroy(struct fy_generic_decoder *gd)
{
	if (!gd)
		return;

	free(gd);
}

struct fy_generic_decoder *
fy_generic_decoder_create(struct fy_parser *fyp, struct fy_generic_builder *gb)
{
	struct fy_generic_decoder *gd = NULL;

	if (!fyp || !gb)
		return NULL;

	gd = malloc(sizeof(*gd));
	if (!gd)
		goto err_out;
	memset(gd, 0, sizeof(*gd));

	gd->fyp = fyp;
	gd->gb = gb;
	gd->parse_cfg = fyp->cfg;
	gd->resolve = !!(fyp->cfg.flags & FYPCF_RESOLVE_DOCUMENT);

	return gd;

err_out:
	fy_generic_decoder_destroy(gd);
	return NULL;
}

void fy_generic_decoder_reset(struct fy_generic_decoder *gd)
{
	if (!gd)
		return;

	if (gd->gb)
		fy_generic_builder_reset(gd->gb);
}

fy_generic fy_generic_decoder_parse(struct fy_generic_decoder *gd,
				    enum fy_generic_decoder_parse_flags flags)
{
	struct fy_generic_document_builder_cfg cfg;
	struct fy_generic_document_builder *fygdb = NULL;
	fy_generic *items = NULL, *items_new;
	fy_generic v, vdoc;
	size_t count, alloc;

	if (!gd || !gd->fyp || !gd->gb)
		return fy_invalid;

	memset(&cfg, 0, sizeof(cfg));
	cfg.parse_cfg = gd->parse_cfg;
	cfg.diag = fy_diag_ref(gd->parse_cfg.diag);
	cfg.gb = gd->gb;
	cfg.flags = fy_generic_decoder_to_builder_flags(flags);

	fygdb = fy_generic_document_builder_create(&cfg);
	if (!fygdb)
		goto err_out;

	count = 0;
	alloc = 0;

	while ((vdoc = fy_generic_document_builder_load_document(fygdb, gd->fyp)),
	       fy_generic_is_valid(vdoc)) {
		if (count >= alloc) {
			if (!alloc)
				alloc = 8;
			alloc *= 2;
			items_new = realloc(items, alloc * sizeof(*items));
			if (!items_new)
				goto err_out;
			items = items_new;
		}
		assert(count < alloc);
		items[count++] = vdoc;

		if (!(flags & FYGDPF_MULTI_DOCUMENT))
			break;
	}

	if (fy_parser_get_stream_error(gd->fyp))
		goto err_out;

	if (!count) {
		v = fy_null;
		goto out;
	}

	if (!(flags & FYGDPF_MULTI_DOCUMENT)) {
		v = items[0];
	} else {
		struct fy_generic_op_args args = {
			.common.count = count,
			.common.items = items,
		};

		v = fy_generic_op_args(gd->gb, FYGBOPF_CREATE_SEQ | FYGBOPF_NO_CHECKS,
				       fy_null, &args);
		if (fy_generic_is_invalid(v))
			goto err_out;
	}

out:
	if (items)
		free(items);
	fy_generic_document_builder_destroy(fygdb);
	return v;

err_out:
	if (!fygdb)
		fy_diag_unref(cfg.diag);
	v = fy_invalid;
	goto out;
}
