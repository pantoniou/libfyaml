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

const char *fy_token_type_txt[] = {
	[FYTT_NONE]			= "<NONE>",
	[FYTT_STREAM_START]		= "STRM+",
	[FYTT_STREAM_END]		= "STRM-",
	[FYTT_VERSION_DIRECTIVE]	= "VRSD",
	[FYTT_TAG_DIRECTIVE]		= "TAGD",
	[FYTT_DOCUMENT_START]		= "DOC+",
	[FYTT_DOCUMENT_END]		= "DOC-",
	[FYTT_BLOCK_SEQUENCE_START]	= "BSEQ+",
	[FYTT_BLOCK_MAPPING_START]	= "BMAP+",
	[FYTT_BLOCK_END]		= "BEND",
	[FYTT_FLOW_SEQUENCE_START]	= "FSEQ+",
	[FYTT_FLOW_SEQUENCE_END]	= "FSEQ-",
	[FYTT_FLOW_MAPPING_START]	= "FMAP+",
	[FYTT_FLOW_MAPPING_END]		= "FMAP-",
	[FYTT_BLOCK_ENTRY]		= "BENTR",
	[FYTT_FLOW_ENTRY]		= "FENTR",
	[FYTT_KEY]			= "KEY",
	[FYTT_SCALAR]			= "SCLR",
	[FYTT_VALUE]			= "VAL",
	[FYTT_ALIAS]			= "ALIAS",
	[FYTT_ANCHOR]			= "ANCHR",
	[FYTT_TAG]			= "TAG",
	[FYTT_INPUT_MARKER]		= "INPUT_MARKER",

	[FYTT_PE_SLASH]			= "PE_SLASH",
	[FYTT_PE_ROOT]			= "PE_ROOT",
	[FYTT_PE_THIS]			= "PE_THIS",
	[FYTT_PE_PARENT]		= "PE_PARENT",
	[FYTT_PE_MAP_KEY]		= "PE_MAP_KEY",
	[FYTT_PE_SEQ_INDEX]		= "PE_SEQ_INDEX",
	[FYTT_PE_SEQ_SLICE]		= "PE_SEQ_SLICE",
	[FYTT_PE_SCALAR_FILTER]		= "PE_SCALAR_FILTER",
	[FYTT_PE_COLLECTION_FILTER]	= "PE_COLLECTION_FILTER",
	[FYTT_PE_SEQ_FILTER]		= "PE_SEQ_FILTER",
	[FYTT_PE_MAP_FILTER]		= "PE_MAP_FILTER",
	[FYTT_PE_EVERY_CHILD]		= "PE_EVERY_CHILD",
	[FYTT_PE_EVERY_CHILD_R]		= "PE_EVERY_CHILD_R",
	[FYTT_PE_ALIAS]			= "PE_ALIAS",
	[FYTT_PE_SIBLING]		= "PE_SIBLING",
	[FYTT_PE_COMMA]			= "PE_COMMA",
	[FYTT_PE_BARBAR]		= "PE_BARBAR",
	[FYTT_PE_AMPAMP]		= "PE_AMPAMP",
	[FYTT_PE_LPAREN]		= "PE_LPAREN",
	[FYTT_PE_RPAREN]		= "PE_RPAREN",
};

char *fy_token_dump_format(struct fy_token *fyt, char *buf, size_t bufsz)
{
	const char *s = NULL;

	if (fyt && (unsigned int)fyt->type < sizeof(fy_token_type_txt)/
					     sizeof(fy_token_type_txt[0]))
		s = fy_token_type_txt[fyt->type];

	if (!s)
		s = "<NULL>";

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

		if (s >= (e - 1))
			break;

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

		if (s >= (e - 1))
			break;

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

void fyp_debug_dump_token_list(struct fy_parser *fyp, struct fy_token_list *fytl,
		struct fy_token *fyt_highlight, const char *banner)
{
	char buf[4096];

	if (!fyp || !fyp->diag || FYET_DEBUG < fyp->diag->cfg.level)
		return;

	fyp_scan_debug(fyp, "%s%s\n", banner,
			fy_token_list_dump_format(fytl, fyt_highlight, buf, sizeof(buf)));
}

void fyp_debug_dump_token(struct fy_parser *fyp, struct fy_token *fyt, const char *banner)
{
	char buf[80];

	if (!fyp || !fyp->diag || FYET_DEBUG < fyp->diag->cfg.level)
		return;

	fyp_scan_debug(fyp, "%s%s\n", banner,
			fy_token_dump_format(fyt, buf, sizeof(buf)));
}

void fyp_debug_dump_simple_key_list(struct fy_parser *fyp, struct fy_simple_key_list *fyskl,
		struct fy_simple_key *fysk_highlight, const char *banner)
{
	char buf[4096];

	if (!fyp || !fyp->diag || FYET_DEBUG < fyp->diag->cfg.level)
		return;

	fyp_scan_debug(fyp, "%s%s\n", banner,
			fy_simple_key_list_dump_format(fyp, fyskl, fysk_highlight, buf, sizeof(buf)));
}

void fyp_debug_dump_simple_key(struct fy_parser *fyp, struct fy_simple_key *fysk, const char *banner)
{
	char buf[80];

	if (!fyp || !fyp->diag || FYET_DEBUG < fyp->diag->cfg.level)
		return;

	fyp_scan_debug(fyp, "%s%s\n", banner,
			fy_simple_key_dump_format(fyp, fysk, buf, sizeof(buf)));
}

void fyp_debug_dump_input(struct fy_parser *fyp, const struct fy_input_cfg *fyic,
		const char *banner)
{
	switch (fyic->type) {
	case fyit_file:
		fyp_scan_debug(fyp, "%s: filename=\"%s\"\n", banner,
				fyic->file.filename);
		break;
	case fyit_stream:
		fyp_scan_debug(fyp, "%s: stream=\"%s\" fileno=%d chunk=%zu\n", banner,
				fyic->stream.name, fileno(fyic->stream.fp),
				fyic->stream.chunk);
		break;
	case fyit_memory:
		fyp_scan_debug(fyp, "%s: start=%p size=%zu\n", banner,
				fyic->memory.data, fyic->memory.size);
		break;
	case fyit_alloc:
		fyp_scan_debug(fyp, "%s: start=%p size=%zu\n", banner,
				fyic->alloc.data, fyic->alloc.size);
		break;
	default:
		break;
	}
}

#endif
