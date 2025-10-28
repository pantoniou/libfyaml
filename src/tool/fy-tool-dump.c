/*
 * fy-tool-dump.c - tool utils for dumping
 *
 * Copyright (c) 2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <regex.h>
#include <stdalign.h>
#include <inttypes.h>
#include <float.h>
#include <limits.h>
#include <math.h>

#include <libfyaml.h>

#include "fy-tool-util.h"

void print_escaped(const char *str, size_t length)
{
	const uint8_t *p;
	int i, c, w;

	for (p = (const uint8_t *)str; length > 0; p += w, length -= (size_t)w) {

		/* get width from the first octet */
		w = (p[0] & 0x80) == 0x00 ? 1 :
		    (p[0] & 0xe0) == 0xc0 ? 2 :
		    (p[0] & 0xf0) == 0xe0 ? 3 :
		    (p[0] & 0xf8) == 0xf0 ? 4 : 0;

		/* error, clip it */
		if ((size_t)w > length)
			goto err_out;

		/* initial value */
		c = p[0] & (0xff >> w);
		for (i = 1; i < w; i++) {
			if ((p[i] & 0xc0) != 0x80)
				goto err_out;
			c = (c << 6) | (p[i] & 0x3f);
		}

		/* check for validity */
		if ((w == 4 && c < 0x10000) ||
		    (w == 3 && c <   0x800) ||
		    (w == 2 && c <    0x80) ||
		    (c >= 0xd800 && c <= 0xdfff) || c >= 0x110000)
			goto err_out;

		switch (c) {
		case '\\':
			printf("\\\\");
			break;
		case '\0':
			printf("\\0");
			break;
		case '\b':
			printf("\\b");
			break;
		case '\f':
			printf("\\f");
			break;
		case '\n':
			printf("\\n");
			break;
		case '\r':
			printf("\\r");
			break;
		case '\t':
			printf("\\t");
			break;
		case '\a':
			printf("\\a");
			break;
		case '\v':
			printf("\\v");
			break;
		case '\e':
			printf("\\e");
			break;
		case 0x85:
			printf("\\N");
			break;
		case 0xa0:
			printf("\\_");
			break;
		case 0x2028:
			printf("\\L");
			break;
		case 0x2029:
			printf("\\P");
			break;
		default:
			if ((c >= 0x01 && c <= 0x1f) || c == 0x7f ||	/* C0 */
			    (c >= 0x80 && c <= 0x9f))			/* C1 */
				printf("\\x%02x", c);
			else
				printf("%.*s", w, p);
			break;
		}
	}

	return;
err_out:
	fprintf(stderr, "escape input error\n");
	abort();
}

void dump_token_comments(struct fy_token *fyt, bool colorize, const char *banner)
{
	static const char *placement_txt[] = {
		[fycp_top]    = "top",
		[fycp_right]  = "right",
		[fycp_bottom] = "bottom",
	};
	enum fy_comment_placement placement;
	char buf[4096];
	const char *str;

	if (!fyt)
		return;

	for (placement = fycp_top; placement < fycp_max; placement++) {
		str = fy_token_get_comment(fyt, buf, sizeof(buf), placement);
		if (!str)
			continue;
		fputs("\n", stdout);
		if (colorize)
			fputs(A_RED, stdout);
		printf("\t%s %6s: ", banner, placement_txt[placement]);
		print_escaped(str, strlen(str));
		if (colorize)
			fputs(A_RESET, stdout);
	}
}

void dump_testsuite_event(struct fy_event *fye, enum dump_testsuite_event_flags dump_flags)
{
	bool colorize, disable_flow_markers, disable_doc_markers, disable_scalar_styles, tsv_format;
	const char *anchor = NULL;
	const char *tag = NULL;
	const char *text = NULL;
	const char *alias = NULL;
	size_t anchor_len = 0, tag_len = 0, text_len = 0, alias_len = 0;
	enum fy_scalar_style style;
	const struct fy_mark *sm, *em = NULL;
	char separator;
	size_t spos, epos;
	int sline, eline, scolumn, ecolumn;

	colorize = !!(dump_flags & DTEF_COLORIZE);
	disable_flow_markers = !!(dump_flags & DTEF_DISABLE_FLOW_MARKERS);
	disable_doc_markers = !!(dump_flags & DTEF_DISABLE_DOC_MARKERS);
	disable_scalar_styles = !!(dump_flags & DTEF_DISABLE_SCALAR_STYLES);
	tsv_format = !!(dump_flags & DTEF_TSV_FORMAT);

	if (!tsv_format) {
		separator = ' ';
		spos = epos = (size_t)-1;
		sline = eline = -1;
		scolumn = ecolumn = -1;
	} else {
		sm = fy_event_start_mark(fye);
		if (sm) {
			spos = sm->input_pos;
			sline = sm->line + 1;
			scolumn = sm->column + 1;
		} else {
			spos = (size_t)-1;
			sline = -1;
			scolumn = -1;
		}


		em = fy_event_end_mark(fye);
		if (em) {
			epos = em->input_pos;
			eline = em->line + 1;
			ecolumn = em->column + 1;
		} else {
			epos = (size_t)-1;
			eline = -1;
			ecolumn = -1;
		}
		separator = '\t';
		/* no colors for TSV */
		colorize = false;
		/* no flow or doc markers for TSV */
		disable_flow_markers = true;
		disable_doc_markers = true;
		disable_scalar_styles = true;
	}

	/* event type */
	switch (fye->type) {
	case FYET_NONE:
		if (colorize)
			fputs(A_BRIGHT_RED, stdout);
		printf("???");
		break;
	case FYET_STREAM_START:
		if (colorize)
			fputs(A_CYAN, stdout);
		printf("+%s", !tsv_format ? "STR" : "str");
		break;
	case FYET_STREAM_END:
		if (colorize)
			fputs(A_CYAN, stdout);
		printf("-%s", !tsv_format ? "STR" : "str");
		break;
	case FYET_DOCUMENT_START:
		if (colorize)
			fputs(A_CYAN, stdout);
		printf("+%s", !tsv_format ? "DOC" : "doc");
		break;
	case FYET_DOCUMENT_END:
		if (colorize)
			fputs(A_CYAN, stdout);
		printf("-%s", !tsv_format ? "DOC" : "doc");
		break;
	case FYET_MAPPING_START:
		if (colorize)
			fputs(A_BRIGHT_CYAN, stdout);
		printf("+%s", !tsv_format ? "MAP" : "map");
		if (fye->mapping_start.anchor)
			anchor = fy_token_get_text(fye->mapping_start.anchor, &anchor_len);
		if (fye->mapping_start.tag)
			tag = fy_token_get_text(fye->mapping_start.tag, &tag_len);
		if (!disable_flow_markers && fy_event_get_node_style(fye) == FYNS_FLOW)
			printf("%c{}", separator);
		break;
	case FYET_MAPPING_END:
		if (colorize)
			fputs(A_BRIGHT_CYAN, stdout);
		printf("-%s", !tsv_format ? "MAP" : "map");
		break;
	case FYET_SEQUENCE_START:
		if (colorize)
			fputs(A_BRIGHT_YELLOW, stdout);
		printf("+%s", !tsv_format ? "SEQ" : "seq");
		if (fye->sequence_start.anchor)
			anchor = fy_token_get_text(fye->sequence_start.anchor, &anchor_len);
		if (fye->sequence_start.tag)
			tag = fy_token_get_text(fye->sequence_start.tag, &tag_len);
		if (!disable_flow_markers && fy_event_get_node_style(fye) == FYNS_FLOW)
			printf("%c[]", separator);
		break;
	case FYET_SEQUENCE_END:
		if (colorize)
			fputs(A_BRIGHT_YELLOW, stdout);
		printf("-%s", !tsv_format ? "SEQ" : "seq");
		break;
	case FYET_SCALAR:
		if (colorize)
			fputs(A_WHITE, stdout);
		printf("=%s", !tsv_format ? "VAL" : "val");
		if (fye->scalar.anchor)
			anchor = fy_token_get_text(fye->scalar.anchor, &anchor_len);
		if (fye->scalar.tag)
			tag = fy_token_get_text(fye->scalar.tag, &tag_len);
		break;
	case FYET_ALIAS:
		if (colorize)
			fputs(A_GREEN, stdout);
		printf("=%s", !tsv_format ? "ALI" : "ali");
		break;
	default:
		abort();
		break;
	}

	/* (position) anchor and tag */
	if (!tsv_format) {
		if (anchor) {
			if (colorize)
				fputs(A_GREEN, stdout);
			printf("%c&%.*s", separator, (int)anchor_len, anchor);
		}
		if (tag) {
			if (colorize)
				fputs(A_GREEN, stdout);
			printf("%c<%.*s>", separator, (int)tag_len, tag);
		}
	} else {
		if (!anchor) {
			anchor = "-";
			anchor_len = 1;
		}
		if (!tag) {
			tag = "-";
			tag_len = 1;
		}
		printf("%c%zd%c%d%c%d", separator, (ssize_t)spos,
			separator, sline, separator, scolumn);
		printf("%c%zd%c%d%c%d", separator, (ssize_t)epos,
			separator, eline, separator, ecolumn);
		printf("%c%.*s", separator, (int)anchor_len, anchor);
		printf("%c%.*s", separator, (int)tag_len, tag);
	}

	/* style hint */
	switch (fye->type) {
	default:
		break;
	case FYET_DOCUMENT_START:
		if (!fy_document_event_is_implicit(fye) && !disable_doc_markers)
			printf("%c---", separator);
		break;
	case FYET_DOCUMENT_END:
		if (!fy_document_event_is_implicit(fye) && !disable_doc_markers)
			printf("%c...", separator);
		break;
	case FYET_MAPPING_START:
		if (!tsv_format)
			break;
		printf("%c%s", separator, fy_event_get_node_style(fye) == FYNS_FLOW ? "{}" : "");
		break;
	case FYET_SEQUENCE_START:
		if (!tsv_format)
			break;
		printf("%c%s", separator, fy_event_get_node_style(fye) == FYNS_FLOW ? "[]" : "");
		break;
	case FYET_SCALAR:
		if (!disable_scalar_styles)
			style = fy_token_scalar_style(fye->scalar.value);
		else
			style = FYSS_DOUBLE_QUOTED;	/* double quoted can handle anything */
		switch (style) {
		case FYSS_PLAIN:
			if (colorize)
				fputs(A_WHITE, stdout);
			printf("%c:", separator);
			break;
		case FYSS_SINGLE_QUOTED:
			if (colorize)
				fputs(A_YELLOW, stdout);
			printf("%c'", separator);
			break;
		case FYSS_DOUBLE_QUOTED:
			if (colorize)
				fputs(A_YELLOW, stdout);
			printf("%c\"", separator);
			break;
		case FYSS_LITERAL:
			if (colorize)
				fputs(A_YELLOW, stdout);
			printf("%c|", separator);
			break;
		case FYSS_FOLDED:
			if (colorize)
				fputs(A_YELLOW, stdout);
			printf("%c>", separator);
			break;
		default:
			abort();
		}
		break;
	case FYET_ALIAS:
		if (tsv_format)
			printf("%c*", separator);
		break;
	}

	/* content */
	switch (fye->type) {
	default:
		break;
	case FYET_SCALAR:
		if (tsv_format)
			printf("%c", separator);
		text = fy_token_get_text(fye->scalar.value, &text_len);
		if (text && text_len > 0)
			print_escaped(text, text_len);
		break;
	case FYET_ALIAS:
		alias = fy_token_get_text(fye->alias.anchor, &alias_len);
		printf("%c%s%.*s", separator, !tsv_format ? "*" : "", (int)alias_len, alias);
		break;
	}

	if (colorize)
		fputs(A_RESET, stdout);
	fputs("\n", stdout);
}

void dump_parse_event(struct fy_parser *fyp, struct fy_event *fye, bool colorize)
{
	struct fy_token *fyt_tag = NULL, *fyt_anchor = NULL;
	const char *anchor = NULL;
	const char *tag = NULL;
	const char *value = NULL;
	size_t anchor_len = 0, tag_len = 0, len = 0;
	enum fy_scalar_style style;
	const struct fy_version *vers;
	const struct fy_tag *tagp = NULL;
	void *iterp;
	struct fy_document_state *fyds;

	fyt_anchor = fy_event_get_anchor_token(fye);
	if (fyt_anchor) {
		anchor = fy_token_get_text(fyt_anchor, &anchor_len);
		assert(anchor);
	}

	fyt_tag = fy_event_get_tag_token(fye);
	if (fyt_tag) {
		tag = fy_token_get_text(fyt_tag, &tag_len);
		assert(tag);
		tagp = fy_tag_token_tag(fyt_tag);
		assert(tagp);
	}

	switch (fye->type) {
	case FYET_NONE:
		if (colorize)
			fputs(A_BRIGHT_RED, stdout);
		printf("???");
		break;
	case FYET_STREAM_START:
		if (colorize)
			fputs(A_CYAN, stdout);
		printf("STREAM_START");
		dump_token_comments(fye->stream_start.stream_start, colorize, "");
		break;
	case FYET_STREAM_END:
		if (colorize)
			fputs(A_CYAN, stdout);
		printf("STREAM_END");
		dump_token_comments(fye->stream_end.stream_end, colorize, "");
		break;
	case FYET_DOCUMENT_START:
		if (colorize)
			fputs(A_CYAN, stdout);

		printf("DOCUMENT_START implicit=%s",
				fye->document_start.implicit ? "true" : "false");

		fyds = fye->document_start.document_state;
		assert(fyds);
		vers = fy_document_state_version(fyds);
		assert(vers);
		printf("( V=%d.%d VE=%s TE=%s", vers->major, vers->minor,
				fy_document_state_version_explicit(fyds) ? "true" : "false",
				fy_document_state_tags_explicit(fyds) ? "true" : "false");
		iterp = NULL;
		if ((tagp = fy_document_state_tag_directive_iterate(fyds, &iterp)) != NULL) {
			printf(" TDs: [");
			do {
				printf(" \"%s\",\"%s\"", tagp->handle, tagp->prefix);
			} while ((tagp = fy_document_state_tag_directive_iterate(fyds, &iterp)) != NULL);
			printf(" ]");
		}
		printf(" )");
		dump_token_comments(fye->document_start.document_start, colorize, "");
		break;

	case FYET_DOCUMENT_END:
		if (colorize)
			fputs(A_CYAN, stdout);
		printf("DOCUMENT_END implicit=%s",
				fye->document_end.implicit ? "true" : "false");
		dump_token_comments(fye->document_end.document_end, colorize, "");
		break;
	case FYET_MAPPING_START:
		if (colorize)
			fputs(A_BRIGHT_CYAN, stdout);
		printf("MAPPING_START");
		if (anchor) {
			if (colorize)
				fputs(A_GREEN, stdout);
			printf(" &%.*s", (int)anchor_len, anchor);
		}
		if (tag) {
			if (colorize)
				fputs(A_GREEN, stdout);
			printf(" <%.*s> (\"%s\",\"%s\")",
				(int)tag_len, tag,
				tagp->handle, tagp->prefix);
		}
		dump_token_comments(fye->mapping_start.mapping_start, colorize, "");
		break;
	case FYET_MAPPING_END:
		if (colorize)
			fputs(A_BRIGHT_CYAN, stdout);
		printf("MAPPING_END");
		dump_token_comments(fye->mapping_end.mapping_end, colorize, "");
		break;
	case FYET_SEQUENCE_START:
		if (colorize)
			fputs(A_BRIGHT_YELLOW, stdout);
		printf("SEQUENCE_START");
		if (anchor) {
			if (colorize)
				fputs(A_GREEN, stdout);
			printf(" &%.*s", (int)anchor_len, anchor);
		}
		if (tag) {
			if (colorize)
				fputs(A_GREEN, stdout);
			printf(" <%.*s> (\"%s\",\"%s\")",
				(int)tag_len, tag,
				tagp->handle, tagp->prefix);
		}
		dump_token_comments(fye->sequence_start.sequence_start, colorize, "");
		break;
	case FYET_SEQUENCE_END:
		if (colorize)
			fputs(A_BRIGHT_YELLOW, stdout);
		printf("SEQUENCE_END");
		dump_token_comments(fye->sequence_end.sequence_end, colorize, "");
		break;
	case FYET_SCALAR:
		if (colorize)
			fputs(A_WHITE, stdout);
		printf("SCALAR");
		if (anchor) {
			if (colorize)
				fputs(A_GREEN, stdout);
			printf(" &%.*s", (int)anchor_len, anchor);
		}
		if (tag) {
			if (colorize)
				fputs(A_GREEN, stdout);
			printf(" <%.*s> (\"%s\",\"%s\")",
				(int)tag_len, tag,
				tagp->handle, tagp->prefix);
		}

		style = fy_token_scalar_style(fye->scalar.value);
		switch (style) {
		case FYSS_PLAIN:
			if (colorize)
				fputs(A_WHITE, stdout);
			printf(" ");
			break;
		case FYSS_SINGLE_QUOTED:
			if (colorize)
				fputs(A_YELLOW, stdout);
			printf(" '");
			break;
		case FYSS_DOUBLE_QUOTED:
			if (colorize)
				fputs(A_YELLOW, stdout);
			printf(" \"");
			break;
		case FYSS_LITERAL:
			if (colorize)
				fputs(A_YELLOW, stdout);
			printf(" |");
			break;
		case FYSS_FOLDED:
			if (colorize)
				fputs(A_YELLOW, stdout);
			printf(" >");
			break;
		default:
			abort();
		}
		value = fy_token_get_text(fye->scalar.value, &len);
		if (value && len > 0)
			print_escaped(value, len);
		dump_token_comments(fye->scalar.value, colorize, "");
		break;
	case FYET_ALIAS:
		anchor = fy_token_get_text(fye->alias.anchor, &anchor_len);
		if (colorize)
			fputs(A_GREEN, stdout);
		printf("ALIAS *%.*s", (int)anchor_len, anchor);
		dump_token_comments(fye->alias.anchor, colorize, "");
		break;
	default:
		/* ignored */
		break;
	}
	if (colorize)
		fputs(A_RESET, stdout);
	fputs("\n", stdout);
}

void dump_scan_token(struct fy_parser *fyp, struct fy_token *fyt, bool colorize)
{
	const char *anchor = NULL, *value = NULL;
	size_t anchor_len = 0, len = 0;
	enum fy_scalar_style style;
	const struct fy_version *vers;
	const struct fy_tag *tag;

	switch (fy_token_get_type(fyt)) {
	case FYTT_NONE:
		if (colorize)
			fputs(A_BRIGHT_RED, stdout);
		printf("NONE");
		break;
	case FYTT_STREAM_START:
		if (colorize)
			fputs(A_CYAN, stdout);
		printf("STREAM_START");
		break;
	case FYTT_STREAM_END:
		if (colorize)
			fputs(A_CYAN, stdout);
		printf("STREAM_END");
		break;
	case FYTT_VERSION_DIRECTIVE:
		if (colorize)
			fputs(A_CYAN, stdout);
		vers = fy_version_directive_token_version(fyt);
		assert(vers);
		printf("VERSION_DIRECTIVE major=%d minor=%d", vers->major, vers->minor);
		break;
	case FYTT_TAG_DIRECTIVE:
		if (colorize)
			fputs(A_CYAN, stdout);
		tag = fy_tag_directive_token_tag(fyt);
		assert(tag);
		printf("TAG_DIRECTIVE handle=\"%s\" prefix=\"%s\"", tag->handle, tag->prefix);
		break;
	case FYTT_DOCUMENT_START:
		if (colorize)
			fputs(A_CYAN, stdout);
		printf("DOCUMENT_START");
		break;
	case FYTT_DOCUMENT_END:
		if (colorize)
			fputs(A_CYAN, stdout);
		printf("DOCUMENT_END");
		break;
	case FYTT_BLOCK_SEQUENCE_START:
		if (colorize)
			fputs(A_BRIGHT_CYAN, stdout);
		printf("BLOCK_SEQUENCE_START");
		break;
	case FYTT_BLOCK_MAPPING_START:
		if (colorize)
			fputs(A_BRIGHT_CYAN, stdout);
		printf("BLOCK_MAPPING_START");
		break;
	case FYTT_BLOCK_END:
		if (colorize)
			fputs(A_BRIGHT_CYAN, stdout);
		printf("BLOCK_END");
		break;
	case FYTT_FLOW_SEQUENCE_START:
		if (colorize)
			fputs(A_BRIGHT_YELLOW, stdout);
		printf("FLOW_SEQUENCE_START");
		break;
	case FYTT_FLOW_SEQUENCE_END:
		if (colorize)
			fputs(A_BRIGHT_YELLOW, stdout);
		printf("FLOW_SEQUENCE_END");
		break;
	case FYTT_FLOW_MAPPING_START:
		if (colorize)
			fputs(A_BRIGHT_YELLOW, stdout);
		printf("FLOW_MAPPING_START");
		break;
	case FYTT_FLOW_MAPPING_END:
		if (colorize)
			fputs(A_BRIGHT_YELLOW, stdout);
		printf("FLOW_MAPPING_END");
		break;
	case FYTT_BLOCK_ENTRY:
		if (colorize)
			fputs(A_BRIGHT_CYAN, stdout);
		printf("BLOCK_ENTRY");
		break;
	case FYTT_FLOW_ENTRY:
		if (colorize)
			fputs(A_BRIGHT_YELLOW, stdout);
		printf("BLOCK_ENTRY");
		break;
	case FYTT_KEY:
		if (colorize)
			fputs(A_BRIGHT_YELLOW, stdout);
		printf("KEY");
		break;
	case FYTT_VALUE:
		if (colorize)
			fputs(A_BRIGHT_YELLOW, stdout);
		printf("KEY");
		break;
	case FYTT_ALIAS:
		anchor = fy_token_get_text(fyt, &anchor_len);
		assert(anchor);
		if (colorize)
			fputs(A_GREEN, stdout);
		printf("ALIAS *%.*s", (int)anchor_len, anchor);
		break;
	case FYTT_ANCHOR:
		anchor = fy_token_get_text(fyt, &anchor_len);
		assert(anchor);
		if (colorize)
			fputs(A_GREEN, stdout);
		printf("ANCHOR &%.*s", (int)anchor_len, anchor);
		break;
	case FYTT_TAG:
		tag = fy_tag_token_tag(fyt);
		assert(tag);
		if (colorize)
			fputs(A_GREEN, stdout);
		/* prefix is a suffix for tag */
		printf("TAG handle=\"%s\" suffix=\"%s\"", tag->handle, tag->prefix);
		break;
	case FYTT_SCALAR:
		if (colorize)
			fputs(A_WHITE, stdout);

		printf("SCALAR ");
		value = fy_token_get_text(fyt, &len);
		assert(value);
		style = fy_token_scalar_style(fyt);
		switch (style) {
		case FYSS_PLAIN:
			if (colorize)
				fputs(A_WHITE, stdout);
			printf(" ");
			break;
		case FYSS_SINGLE_QUOTED:
			if (colorize)
				fputs(A_YELLOW, stdout);
			printf(" '");
			break;
		case FYSS_DOUBLE_QUOTED:
			if (colorize)
				fputs(A_YELLOW, stdout);
			printf(" \"");
			break;
		case FYSS_LITERAL:
			if (colorize)
				fputs(A_YELLOW, stdout);
			printf(" |");
			break;
		case FYSS_FOLDED:
			if (colorize)
				fputs(A_YELLOW, stdout);
			printf(" >");
			break;
		default:
			abort();
		}
		printf("%.*s", (int)len, value);
		break;
	default:
		/* not handled; should not be produced by scan */
		break;
	}
	if (colorize)
		fputs(A_RESET, stdout);
	fputs("\n", stdout);
}
