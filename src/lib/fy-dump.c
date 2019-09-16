/*
 * fy-dump.c - various debugging methods
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
#include "fy-ctype.h"
#include "fy-utf8.h"

char *fy_token_dump_format(struct fy_token *fyt, char *buf, size_t bufsz)
{
	const char *s = "-";

	if (!fyt) {
		s = "";
		goto out;
	}

	switch (fyt->type) {
	case FYTT_NONE:
		break;
	case FYTT_STREAM_START:
		s = "STRM+";
		break;
	case FYTT_STREAM_END:
		s = "STRM-";
		break;
	case FYTT_VERSION_DIRECTIVE:
		s = "VRSD";
		break;
	case FYTT_TAG_DIRECTIVE:
		s = "TAGD";
		break;
	case FYTT_DOCUMENT_START:
		s = "DOC+";
		break;
	case FYTT_DOCUMENT_END:
		s = "DOC-";
		break;
	case FYTT_BLOCK_SEQUENCE_START:
		s = "BSEQ+";
		break;
	case FYTT_BLOCK_MAPPING_START:
		s = "BMAP+";
		break;
	case FYTT_BLOCK_END:
		s = "BEND";
		break;
	case FYTT_FLOW_SEQUENCE_START:
		s = "FSEQ+";
		break;
	case FYTT_FLOW_SEQUENCE_END:
		s = "FSEQ-";
		break;
	case FYTT_FLOW_MAPPING_START:
		s = "FMAP+";
		break;
	case FYTT_FLOW_MAPPING_END:
		s = "FMAP-";
		break;
	case FYTT_BLOCK_ENTRY:
		s = "BENTR";
		break;
	case FYTT_FLOW_ENTRY:
		s = "FENTR";
		break;
	case FYTT_KEY:
		s = "KEY";
		break;
	case FYTT_SCALAR:
		s = "SCLR";
		break;
	case FYTT_VALUE:
		s = "VAL";
		break;
	case FYTT_ALIAS:
		s = "ALIAS";
		break;
	case FYTT_ANCHOR:
		s = "ANCHR";
		break;
	case FYTT_TAG:
		s = "TAG";
		break;
	}

out:
	snprintf(buf, bufsz, "%s", s);

	return buf;
}

char *fy_token_list_dump_format(struct fy_token_list *fytl,
		struct fy_token *fyt_highlight, char *buf, size_t bufsz)
{
	char *s, *e;
	struct fy_token *fyt;

	s = buf;
	e = buf + bufsz - 1;
	for (fyt = fy_token_list_first(fytl); fyt; fyt = fy_token_next(fytl, fyt)) {

		s += snprintf(s, e - s, "%s%s",
				fyt != fy_token_list_first(fytl) ? "," : "",
				fyt_highlight == fyt ? "*" : "");

		fy_token_dump_format(fyt, s, e - s);

		s += strlen(s);
	}
	*s = '\0';

	return buf;
}

char *fy_simple_key_dump_format(struct fy_parser *fyp, struct fy_simple_key *fysk, char *buf, size_t bufsz)
{
	char tbuf[80];

	if (!fysk) {
		if (bufsz > 0)
			*buf = '\0';
		return buf;
	}

	fy_token_dump_format(fysk->token, tbuf, sizeof(tbuf));

	snprintf(buf, bufsz, "%s/%c%c/%d/<%d-%d,%d-%d>", tbuf,
			fysk->required ? 'R' : '-',
			fysk->possible ? 'P' : '-',
			fysk->flow_level,
			fysk->mark.line, fysk->mark.column,
			fysk->end_mark.line, fysk->end_mark.column);
	return buf;
}

char *fy_simple_key_list_dump_format(struct fy_parser *fyp, struct fy_simple_key_list *fyskl,
		struct fy_simple_key *fysk_highlight, char *buf, size_t bufsz)
{
	char *s, *e;
	struct fy_simple_key *fysk;

	s = buf;
	e = buf + bufsz - 1;
	for (fysk = fy_simple_key_list_first(fyskl); fysk; fysk = fy_simple_key_next(fyskl, fysk)) {

		s += snprintf(s, e - s, "%s%s",
				fysk != fy_simple_key_list_first(fyskl) ? "," : "",
				fysk_highlight == fysk ? "*" : "");

		fy_simple_key_dump_format(fyp, fysk, s, e - s);

		s += strlen(s);
	}
	*s = '\0';

	return buf;
}

#ifndef NDEBUG

void fy_debug_dump_token_list(struct fy_parser *fyp, struct fy_token_list *fytl,
		struct fy_token *fyt_highlight, const char *banner)
{
	char buf[1024];

	if (FYET_DEBUG < FYPCF_GET_DEBUG_LEVEL(fyp->cfg.flags))
		return;

	fy_scan_debug(fyp, "%s%s\n", banner,
			fy_token_list_dump_format(fytl, fyt_highlight, buf, sizeof(buf)));
}

void fy_debug_dump_token(struct fy_parser *fyp, struct fy_token *fyt, const char *banner)
{
	char buf[80];

	if (FYET_DEBUG < FYPCF_GET_DEBUG_LEVEL(fyp->cfg.flags))
		return;

	fy_scan_debug(fyp, "%s%s\n", banner,
			fy_token_dump_format(fyt, buf, sizeof(buf)));
}

void fy_debug_dump_simple_key_list(struct fy_parser *fyp, struct fy_simple_key_list *fyskl,
		struct fy_simple_key *fysk_highlight, const char *banner)
{
	char buf[1024];

	if (FYET_DEBUG < FYPCF_GET_DEBUG_LEVEL(fyp->cfg.flags))
		return;

	fy_scan_debug(fyp, "%s%s\n", banner,
			fy_simple_key_list_dump_format(fyp, fyskl, fysk_highlight, buf, sizeof(buf)));
}

void fy_debug_dump_simple_key(struct fy_parser *fyp, struct fy_simple_key *fysk, const char *banner)
{
	char buf[80];

	if (FYET_DEBUG < FYPCF_GET_DEBUG_LEVEL(fyp->cfg.flags))
		return;

	fy_scan_debug(fyp, "%s%s\n", banner,
			fy_simple_key_dump_format(fyp, fysk, buf, sizeof(buf)));
}

void fy_debug_dump_input(struct fy_parser *fyp, const struct fy_input_cfg *fyic,
		const char *banner)
{
	switch (fyic->type) {
	case fyit_file:
		fy_scan_debug(fyp, "%s: filename=\"%s\"\n", banner,
				fyic->file.filename);
		break;
	case fyit_stream:
		fy_scan_debug(fyp, "%s: stream=\"%s\" fileno=%d chunk=%zu\n", banner,
				fyic->stream.name, fileno(fyic->stream.fp),
				fyic->stream.chunk);
		break;
	case fyit_memory:
		fy_scan_debug(fyp, "%s: start=%p size=%zu\n", banner,
				fyic->memory.data, fyic->memory.size);
		break;
	default:
		break;
	}
}

#endif
