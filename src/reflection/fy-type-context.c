/*
 * fy-type-context.c - Reflection type system context management
 *                     (reference and type system create/destroy)
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
#include "fy-reflect-meta.h"
#include "fy-type-meta.h"

/* -------------------------------------------------------------------------
 * reflection_reference_destroy
 * -------------------------------------------------------------------------
 */

void
reflection_reference_destroy(struct reflection_reference *rr)
{
	if (!rr)
		return;

	if (rr->rfd_root) {
		fy_document_destroy(rr->rfd_root->yaml_annotation);
		reflection_field_data_destroy(rr->rfd_root);
	}
	if (rr->rtd_root)
		reflection_type_data_unref(rr->rtd_root);
	if (rr->meta)
		free(rr->meta);
	if (rr->name)
		free(rr->name);
	free(rr);
}

/* -------------------------------------------------------------------------
 * reflection_type_system_destroy
 * -------------------------------------------------------------------------
 */

void reflection_type_system_destroy(struct reflection_type_system *rts)
{
	if (!rts)
		return;

	reflection_reference_destroy(rts->root_ref);

	if (rts->diag)
		fy_diag_unref(rts->diag);

	free(rts);
}

/* Cast helpers — defined here to keep the knowledge of the mapping local */
static inline struct reflection_type_system *
ctx_to_rts(struct fy_type_context *ctx)
{
	return (struct reflection_type_system *)ctx;
}

static inline struct fy_type_context *
rts_to_ctx(struct reflection_type_system *rts)
{
	return (struct fy_type_context *)rts;
}

static inline const struct reflection_type_data *
mt_to_rtd(const struct fy_meta_type *mt)
{
	return (const struct reflection_type_data *)mt;
}

static inline const struct fy_meta_type *
rtd_to_mt(const struct reflection_type_data *rtd)
{
	return (const struct fy_meta_type *)rtd;
}

static inline const struct reflection_field_data *
mf_to_rfd(const struct fy_meta_field *mf)
{
	return (const struct reflection_field_data *)mf;
}

static inline const struct fy_meta_field *
rfd_to_mf(const struct reflection_field_data *rfd)
{
	return (const struct fy_meta_field *)rfd;
}

struct fy_type_context *
fy_type_context_create(const struct fy_type_context_cfg *cfg)
{
	struct reflection_type_system *rts = NULL;
	struct reflection_type_system_config rts_cfg;

	if (!cfg)
		return NULL;

	memset(&rts_cfg, 0, sizeof(rts_cfg));
	rts_cfg.rfl = cfg->rfl;
	rts_cfg.entry_type = cfg->entry_type;
	rts_cfg.entry_meta = cfg->entry_meta;
	rts_cfg.diag = cfg->diag;
	if (cfg->flags & FYTCCF_DUMP_REFLECTION)
		rts_cfg.flags |= RTSCF_DUMP_REFLECTION;
	if (cfg->flags & FYTCCF_DUMP_TYPE_SYSTEM)
		rts_cfg.flags |= RTSCF_DUMP_TYPE_SYSTEM;
	if (cfg->flags & FYTCCF_DEBUG)
		rts_cfg.flags |= RTSCF_DEBUG;
	if (cfg->flags & FYTCCF_STRICT_ANNOTATIONS)
		rts_cfg.flags |= RTSCF_STRICT_ANNOTATIONS;

	rts = reflection_type_system_create(&rts_cfg);
	if (!rts)
		goto err_out;

	/* strict mode with unknown annotations */
	if ((rts_cfg.flags & RTSCF_STRICT_ANNOTATIONS) && rts->had_unknown_annotations)
		goto err_out;

	return rts_to_ctx(rts);

err_out:
	reflection_type_system_destroy(rts);
	return NULL;
}

void
fy_type_context_destroy(struct fy_type_context *ctx)
{
	reflection_type_system_destroy(ctx_to_rts(ctx));
}

int
fy_type_context_parse(struct fy_type_context *ctx,
		      struct fy_parser *fyp,
		      void **datap)
{
	struct reflection_type_system *rts = ctx_to_rts(ctx);

	if (!rts || !rts->root_ref || !rts->root_ref->rtd_root)
		return -1;

	return reflection_parse(fyp, rts->root_ref->rtd_root, datap,
				(enum reflection_parse_flags)0);
}

int
fy_type_context_emit(struct fy_type_context *ctx,
		     struct fy_emitter *emit,
		     const void *data,
		     unsigned int flags)
{
	struct reflection_type_system *rts = ctx_to_rts(ctx);
	enum reflection_emit_flags rflags = 0;

	if (!rts || !rts->root_ref || !rts->root_ref->rtd_root)
		return -1;

	/* Map public FYTCEF_* flags → internal REF_EMIT_* flags */
	if (flags & 1U)	/* FYTCEF_SS */
		rflags |= REF_EMIT_SS;
	if (flags & 2U)	/* FYTCEF_DS */
		rflags |= REF_EMIT_DS;
	if (flags & 4U)	/* FYTCEF_DE */
		rflags |= REF_EMIT_DE;
	if (flags & 8U)	/* FYTCEF_SE */
		rflags |= REF_EMIT_SE;

	return reflection_emit(emit, rts->root_ref->rtd_root, data, rflags);
}

void fy_type_context_free_data(struct fy_type_context *ctx, void *data)
{
	struct reflection_type_system *rts = ctx_to_rts(ctx);

	if (!rts || !rts->root_ref || !rts->root_ref->rtd_root || !data)
		return;

	reflection_type_data_free(rts->root_ref->rtd_root, data);
}

const struct fy_meta_type *
fy_type_context_get_root(const struct fy_type_context *ctx)
{
	const struct reflection_type_system *rts =
		(const struct reflection_type_system *)ctx;

	if (!rts || !rts->root_ref)
		return NULL;

	return rtd_to_mt(rts->root_ref->rtd_root);
}

const struct fy_type_info *
fy_meta_type_get_type_info(const struct fy_meta_type *mt)
{
	const struct reflection_type_data *rtd = mt_to_rtd(mt);

	return rtd ? rtd->ti : NULL;
}

int fy_meta_type_get_field_count(const struct fy_meta_type *mt)
{
	const struct reflection_type_data *rtd = mt_to_rtd(mt);

	return rtd ? rtd->fields_count : 0;
}

const struct fy_meta_field *
fy_meta_type_get_field(const struct fy_meta_type *mt, int idx)
{
	const struct reflection_type_data *rtd = mt_to_rtd(mt);

	if (!rtd || idx < 0 || idx >= rtd->fields_count)
		return NULL;

	return rfd_to_mf(rtd->fields[idx]);
}

const struct fy_field_info *
fy_meta_field_get_field_info(const struct fy_meta_field *mf)
{
	const struct reflection_field_data *rfd = mf_to_rfd(mf);

	return rfd ? rfd->fi : NULL;
}

const struct fy_meta_type *
fy_meta_field_get_meta_type(const struct fy_meta_field *mf)
{
	const struct reflection_field_data *rfd = mf_to_rfd(mf);

	if (!rfd)
		return NULL;

	return rtd_to_mt(rfd->rtd);
}

#endif /* HAVE_REFLECTION */
