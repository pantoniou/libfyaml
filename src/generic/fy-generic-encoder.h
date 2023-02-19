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

struct fy_generic_encoder {
	struct fy_emitter *emit;
	bool verbose;
	bool emitted_stream_start;
	bool emitted_stream_end;
};

struct fy_generic_encoder *
fy_generic_encoder_create(struct fy_emitter *emit, bool verbose);
void fy_generic_encoder_destroy(struct fy_generic_encoder *fyge);

int fy_generic_encoder_emit_document(struct fy_generic_encoder *fyge, fy_generic root, fy_generic vds);
int fy_generic_encoder_emit_all_documents(struct fy_generic_encoder *fyge, fy_generic vdir);
int fy_generic_encoder_sync(struct fy_generic_encoder *fyge);

#endif
