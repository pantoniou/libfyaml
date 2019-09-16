/*
 * fy-emit.h - internal YAML emitter header
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_EMIT_H
#define FY_EMIT_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <libfyaml.h>

#define FYEF_WHITESPACE		0x0001
#define FYEF_INDENTATION	0x0002
#define FYEF_OPEN_ENDED		0x0004
#define FYEF_HAD_DOCUMENT_START	0x0008
#define FYEF_HAD_DOCUMENT_END	0x0010

struct fy_document;

struct fy_emitter {
	int line;
	int column;
	int flow_level;
	unsigned int flags;
	bool output_error : 1;
	/* current document */
	const struct fy_emitter_cfg *cfg;
	struct fy_document *fyd;
};

static inline bool fy_emit_whitespace(struct fy_emitter *emit)
{
	return !!(emit->flags & FYEF_WHITESPACE);
}

static inline bool fy_emit_indentation(struct fy_emitter *emit)
{
	return !!(emit->flags & FYEF_INDENTATION);
}

static inline bool fy_emit_open_ended(struct fy_emitter *emit)
{
	return !!(emit->flags & FYEF_OPEN_ENDED);
}

#endif
