/*
 * fy-generic-encoder.h - generic encoder (generic -> yaml)
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_GENERIC_ENCODER_H
#define FY_GENERIC_ENCODER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libfyaml.h>

#include "fy-allocator.h"
#include "fy-generic.h"

enum fy_generic_encoder_emit_flags {
	FYGEEF_DISABLE_DIRECTORY	= FY_BIT(0),
	FYGEEF_MULTI_DOCUMENT		= FY_BIT(1),
};

struct fy_generic_encoder {
	struct fy_emitter *emit;
	enum fy_generic_encoder_emit_flags emit_flags;
	bool verbose;
	bool emitted_stream_start;
	bool emitted_stream_end;
};

struct fy_generic_encoder *
fy_generic_encoder_create(struct fy_emitter *emit, bool verbose);
void fy_generic_encoder_destroy(struct fy_generic_encoder *fyge);

int fy_generic_encoder_sync(struct fy_generic_encoder *fyge);

int fy_generic_encoder_emit(struct fy_generic_encoder *fyge,
			    enum fy_generic_encoder_emit_flags emit_flags, fy_generic v);

/* quick and dirty to stdout */
int fy_generic_emit(fy_generic v, enum fy_emitter_cfg_flags flags);
int fy_generic_emit_default(fy_generic v);
int fy_generic_emit_compact(fy_generic v);

#endif
