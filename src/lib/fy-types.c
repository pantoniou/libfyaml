/*
 * fy-types.c - types definition
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <limits.h>

#include <libfyaml.h>

#include "fy-parse.h"

/* parse only types */
FY_PARSE_TYPE_DEFINE_SIMPLE(indent);
FY_PARSE_TYPE_DEFINE_SIMPLE(simple_key);
FY_PARSE_TYPE_DEFINE_SIMPLE(parse_state_log);
FY_PARSE_TYPE_DEFINE_SIMPLE(flow);

FY_ALLOC_TYPE_DEFINE(eventp);
FY_PARSE_TYPE_DEFINE(eventp);

struct fy_eventp *fy_parse_eventp_alloc(struct fy_parser *fyp)
{
	struct fy_eventp *fyep;

	fyep = fy_parse_eventp_alloc_simple(fyp);
	if (!fyep)
		return NULL;
	fyep->fyp = fyp;
	fyep->e.type = FYET_NONE;

	return fyep;
}

void fy_parse_eventp_recycle(struct fy_parser *fyp, struct fy_eventp *fyep)
{
	struct fy_event *fye;

	if (!fyp || !fyep)
		return;

	fye = &fyep->e;
	switch (fye->type) {
	case FYET_NONE:
		break;
	case FYET_STREAM_START:
		fy_token_unref(fye->stream_start.stream_start);
		break;
	case FYET_STREAM_END:
		fy_token_unref(fye->stream_end.stream_end);
		break;
	case FYET_DOCUMENT_START:
		fy_token_unref(fye->document_start.document_start);
		fy_document_state_unref(fye->document_start.document_state);
		break;
	case FYET_DOCUMENT_END:
		fy_token_unref(fye->document_end.document_end);
		break;
	case FYET_MAPPING_START:
		fy_token_unref(fye->mapping_start.anchor);
		fy_token_unref(fye->mapping_start.tag);
		fy_token_unref(fye->mapping_start.mapping_start);
		break;
	case FYET_MAPPING_END:
		fy_token_unref(fye->mapping_end.mapping_end);
		break;
	case FYET_SEQUENCE_START:
		fy_token_unref(fye->sequence_start.anchor);
		fy_token_unref(fye->sequence_start.tag);
		fy_token_unref(fye->sequence_start.sequence_start);
		break;
	case FYET_SEQUENCE_END:
		fy_token_unref(fye->sequence_end.sequence_end);
		break;
	case FYET_SCALAR:
		fy_token_unref(fye->scalar.anchor);
		fy_token_unref(fye->scalar.tag);
		fy_token_unref(fye->scalar.value);
		break;
	case FYET_ALIAS:
		fy_token_unref(fye->alias.anchor);
		break;
	}

	fy_parse_eventp_recycle_simple(fyp, fyep);
}
