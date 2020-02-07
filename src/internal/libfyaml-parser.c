/*
 * libfyaml-parser.c - swiss army knife testing of libfyaml+libyaml
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
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
#include <unistd.h>
#include <limits.h>
#include <ctype.h>

#include <libfyaml.h>

#if defined(HAVE_LIBYAML) && HAVE_LIBYAML
#include <yaml.h>
#endif

#include "fy-parse.h"

#include "fy-valgrind.h"

#include "xxhash.h"

#define QUIET_DEFAULT			false
#define INCLUDE_DEFAULT			""
#define MODE_DEFAULT			"parse"
#define DEBUG_LEVEL_DEFAULT		FYET_WARNING
#define INDENT_DEFAULT			2
#define WIDTH_DEFAULT			80
#define RESOLVE_DEFAULT			false
#define SORT_DEFAULT			false
#define CHUNK_DEFAULT			0
#define COLOR_DEFAULT			"auto"
#define MMAP_DISABLE_DEFAULT		false

#define OPT_DISABLE_MMAP		128

static struct option lopts[] = {
	{"include",		required_argument,	0,	'I' },
	{"mode",		required_argument,	0,	'm' },
	{"debug-level",		required_argument,	0,	'd' },
	{"indent",		required_argument,	0,	'i' },
	{"width",		required_argument,	0,	'w' },
	{"resolve",		no_argument,		0,	'r' },
	{"sort",		no_argument,		0,	's' },
	{"chunk",		required_argument,	0,	'c' },
	{"color",		required_argument,	0,	'C' },
	{"diag",		required_argument,	0,	'D' },
	{"module",		required_argument,	0,	'M' },
	{"disable-mmap",	no_argument,		0,	OPT_DISABLE_MMAP },
	{"quiet",		no_argument,		0,	'q' },
	{"help",		no_argument,		0,	'h' },
	{0,			0,              	0,	 0  },
};

#if defined(HAVE_LIBYAML) && HAVE_LIBYAML
#define LIBYAML_MODES	"|libyaml-scan|libyaml-parse|libyaml-testsuite|libyaml-dump"
#else
#define LIBYAML_MODES	""
#endif

#define MODES	"parse|scan|copy|testsuite|dump|build" LIBYAML_MODES

static void display_usage(FILE *fp, char *progname)
{
	fprintf(fp, "Usage: %s [options] [files]\n", progname);
	fprintf(fp, "\nOptions:\n\n");
	fprintf(fp, "\t--include, -I <path>     : Add directory to include path "
						" (default path \"%s\")\n",
						INCLUDE_DEFAULT);
	fprintf(fp, "\t--mode, -m <mode>        : Set mode [" MODES "]"
						" (default mode \"%s\")\n",
						MODE_DEFAULT);
	fprintf(fp, "\t--debug-level, -d <lvl>  : Set debug level to <lvl>"
						"(default level %d)\n",
						DEBUG_LEVEL_DEFAULT);
	fprintf(fp, "\t--indent, -i <indent>    : Set dump indent to <indent>"
						" (default indent %d)\n",
						INDENT_DEFAULT);
	fprintf(fp, "\t--width, -w <width>      : Set dump width to <width>"
						" (default width %d)\n",
						WIDTH_DEFAULT);
	fprintf(fp, "\t--resolve, -r            : Perform anchor and merge key resolution"
						" (default %s)\n",
						RESOLVE_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--sort, -s               : Perform mapping key sort (valid for dump)"
						" (default %s)\n",
						SORT_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--color, -C <mode>       : Color output can be one of on, off, auto"
						" (default %s)\n",
						COLOR_DEFAULT);
	fprintf(fp, "\t--chunk, -c <size>       : Set buffer chunk to <size>"
						" (default is %d - 0 means PAGE_SIZE)\n",
						CHUNK_DEFAULT);
	fprintf(fp, "\t--diag, -D <diag[,diag]> : Set debug message diagnostic meta"
						" (source, position, type, module, all, none)\n");
	fprintf(fp, "\t--module, -M <mod,[mod]> : Set debug message module enable"
						" (unknown, atom, scan, parse, doc, build, internal, system, all, none)\n");
	fprintf(fp, "\t--quiet, -q              : Quiet operation, do not "
						"output messages (default %s)\n",
						QUIET_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--help, -h               : Display  help message\n");
	fprintf(fp, "\ne.g. %s input.yaml\n", progname);

	if (fp == stderr)
		exit(EXIT_FAILURE);
}

void print_escaped(const char *str, int length)
{
	int i;
	char c;

	if (length < 0)
		length = strlen(str);
	for (i = 0; i < length; i++) {
		c = *str++;
		if (c == '\\')
			printf("\\\\");
		else if (c == '\0')
			printf("\\0");
		else if (c == '\b')
			printf("\\b");
		else if (c == '\n')
			printf("\\n");
		else if (c == '\r')
			printf("\\r");
		else if (c == '\t')
			printf("\\t");
		else
			printf("%c", c);
	}
}

static int txt2esc_internal(const char *s, int l, char *out, int *outszp, int delim)
{
	const char *e;
	int ll;
	char c;
	char *o = NULL, *oe = NULL;

	e = s + l;
	if (out) {
		o = out;
		oe = o + *outszp;
	}

#define O_CH(_c) \
	do { \
		ll++; \
		if (o && (oe - o) > 0) \
			*o++ = (_c); \
	} while(0)

	ll = 0;
	while (s < e) {
		c = *s++;
		if (delim > 0 && c == delim) {
			O_CH('\\');
		} else if (c == '\0' || strchr("\a\b\t\n\v\f\r\e", c)) {
			/* normal 1 -> 2 character escapes */
			O_CH('\\');
			switch (c) {
			case '\0':
				c = '0';
				break;
			case '\a':
				c = 'a';
				break;
			case '\b':
				c = 'b';
				break;
			case '\t':
				c = 't';
				break;
			case '\n':
				c = 'n';
				break;
			case '\v':
				c = 'v';
				break;
			case '\f':
				c = 'f';
				break;
			case '\r':
				c = 'r';
				break;
			case '\e':
				c = 'e';
				break;
			}
		} else if ((e - s) >= 1 && (uint8_t)c == 0xc2 &&
			   ((uint8_t)s[1] == 0x85 || (uint8_t)s[1] == 0xa0)) {
			/* \N & \_ unicode escapes 2 -> 1 */
			O_CH('\\');
			if ((uint8_t)s[1] == 0x85)
				c = 'N';
			else
				c = '_';
		} else if ((e - s) >= 2 && (uint8_t)c == 0xe2 && (uint8_t)s[1] == 0x80 &&
			   ((uint8_t)s[2] == 0xa8 || (uint8_t)s[2] == 0xa9)) {
			/* \L & \P unicode escapes 3 -> 1 */
			O_CH('\\');
			if ((uint8_t)s[2] == 0xa8)
				c = 'L';
			else
				c = 'P';
		}
		O_CH(c);
	}
	/* terminating \0 */
	O_CH('\0');

	if (out)
		*outszp = oe - o;

	return ll;
}

static int txt2esc_length(const char *s, int l, int delim)
{
	if (l < 0)
		l = strlen(s);
	return txt2esc_internal(s, l, NULL, NULL, delim);
}

static char *txt2esc_format(const char *s, int l, char *buf, int maxsz, int delim)
{
	if (l < 0)
		l = strlen(s);

	txt2esc_internal(s, l, buf, &maxsz, delim);
	return buf;
}

#define fy_atom_get_text_a(_atom) \
	({ \
		struct fy_atom *_a = (_atom); \
		int _len; \
		char *_buf; \
		const char *_txt = ""; \
		\
		if (!_a->direct_output) { \
			_len = fy_atom_format_text_length(_a); \
			if (_len > 0) { \
				_buf = alloca(_len + 1); \
				memset(_buf, 0, _len + 1); \
				fy_atom_format_text(_a, _buf, _len + 1); \
				_buf[_len] = '\0'; \
				_txt = _buf; \
			} \
		} else { \
			_len = fy_atom_size(_a); \
			_buf = alloca(_len + 1); \
			memset(_buf, 0, _len + 1); \
			memcpy(_buf, fy_atom_data(_a), _len); \
			_buf[_len] = '\0'; \
			_txt = _buf; \
		} \
		_txt; \
	})

#define txt2esc_a(_s, _l) \
	({ \
	 	const char *__s = (const void *)(_s); \
	 	int __l = (_l); \
		int _ll = txt2esc_length(__s, __l, '\''); \
	 	txt2esc_format(__s, __l, alloca(_ll + 1), _ll + 1, '\''); \
	})

#define fy_atom_get_esc_text_a(_atom) txt2esc_a(fy_atom_get_text_a(_atom), -1)
#define fy_token_get_esc_text_a(_atom) txt2esc_a(fy_token_get_text0(_atom), -1)

void dump_event(struct fy_parser *fyp, struct fy_event *fye)
{
	char mbuf[40];
	char *mm;
	char *anchor = NULL, *tag = NULL, *value = NULL;

	snprintf(mbuf, sizeof(mbuf), " %10s-%-10s ",
			"", "");
	mm = mbuf;
	mm = " ";

	switch (fye->type) {
	case FYET_NONE:
		printf("NO\n");
		break;
	case FYET_STREAM_START:
		printf("%-14s%s|\n", "STREAM_START", mm);
		break;
	case FYET_STREAM_END:
		printf("%-14s%s|\n", "STREAM_END", mm);
		break;
	case FYET_DOCUMENT_START:
		printf("%-14s%s|\n", "DOCUMENT_START", mm);
		break;
	case FYET_DOCUMENT_END:
		printf("%-14s%s|\n", "DOCUMENT_END", mm);
		break;
	case FYET_ALIAS:
		anchor = fy_token_get_esc_text_a(fye->alias.anchor);
		printf("%-14s%s| '%s'\n", "ALIAS", mm, anchor);
		break;
	case FYET_SCALAR:
		if (fye->scalar.anchor)
			anchor = fy_token_get_esc_text_a(fye->scalar.anchor);
		if (fye->scalar.tag)
			tag = fy_token_get_esc_text_a(fye->scalar.tag);
		if (fye->scalar.value)
			value = fy_token_get_esc_text_a(fye->scalar.value);
		printf("%-14s%s|%s%s%s%s%s%s '%s'\n", "SCALAR", mm,
			anchor ? " anchor='" : "", anchor ? : "", anchor ? "'" : "",
			   tag ?    " tag='" : "",    tag ? : "",    tag ? "'" : "",
			value ? : "");
		break;
	case FYET_SEQUENCE_START:
		if (fye->sequence_start.anchor)
			anchor = fy_token_get_esc_text_a(fye->sequence_start.anchor);
		if (fye->sequence_start.tag)
			tag = fy_token_get_esc_text_a(fye->sequence_start.tag);
		printf("%-14s%s|%s%s%s%s%s%s\n", "SEQUENCE_START", mm,
			anchor ? " anchor='" : "", anchor ? : "", anchor ? "'" : "",
			   tag ?    " tag='" : "",    tag ? : "",    tag ? "'" : "");
		break;
	case FYET_SEQUENCE_END:
		printf("%-14s%s|\n", "SEQUENCE_END", mm);
		break;
	case FYET_MAPPING_START:
		if (fye->mapping_start.anchor)
			anchor = fy_token_get_esc_text_a(fye->mapping_start.anchor);
		if (fye->mapping_start.tag)
			tag = fy_token_get_esc_text_a(fye->mapping_start.tag);
		printf("%-14s%s|%s%s%s%s%s%s\n", "MAPPING_START", mm,
			anchor ? " anchor='" : "", anchor ? : "", anchor ? "'" : "",
			   tag ?    " tag='" : "",    tag ? : "",    tag ? "'" : "");
		break;
	case FYET_MAPPING_END:
		printf("%-14s%s|\n", "MAPPING_END", mm);
		break;
	default:
		assert(0);
	}
}

int do_parse(struct fy_parser *fyp)
{
	struct fy_eventp *fyep;

	while ((fyep = fy_parse_private(fyp)) != NULL) {
		dump_event(fyp, &fyep->e);
		fy_parse_eventp_recycle(fyp, fyep);
	}

	return fyp->stream_error ? -1 : 0;
}

void dump_testsuite_event(struct fy_parser *fyp, struct fy_event *fye)
{
	const char *anchor = NULL, *tag = NULL, *value = NULL;
	size_t anchor_len = 0, tag_len = 0, value_len = 0;
	enum fy_scalar_style style;

	switch (fye->type) {
	case FYET_NONE:
		printf("???\n");
		break;
	case FYET_STREAM_START:
		printf("+STR\n");
		break;
	case FYET_STREAM_END:
		printf("-STR\n");
		break;
	case FYET_DOCUMENT_START:
		printf("+DOC%s\n", !fy_document_event_is_implicit(fye) ? " ---" : "");
		break;
	case FYET_DOCUMENT_END:
		printf("-DOC%s\n", !fy_document_event_is_implicit(fye) ? " ..." : "");
		break;
	case FYET_MAPPING_START:
		if (fye->mapping_start.anchor)
			anchor = fy_token_get_text(fye->mapping_start.anchor, &anchor_len);
		if (fye->mapping_start.tag)
			tag = fy_token_get_text(fye->mapping_start.tag, &tag_len);
		printf("+MAP");
		if (anchor)
			printf(" &%.*s", (int)anchor_len, anchor);
		if (tag)
			printf(" <%.*s>", (int)tag_len, tag);
		printf("\n");
		break;
	case FYET_MAPPING_END:
		printf("-MAP\n");
		break;
	case FYET_SEQUENCE_START:
		if (fye->sequence_start.anchor)
			anchor = fy_token_get_text(fye->sequence_start.anchor, &anchor_len);
		if (fye->sequence_start.tag)
			tag = fy_token_get_text(fye->sequence_start.tag, &tag_len);
		printf("+SEQ");
		if (anchor)
			printf(" &%.*s", (int)anchor_len, anchor);
		if (tag)
			printf(" <%.*s>", (int)tag_len, tag);
		printf("\n");
		break;
	case FYET_SEQUENCE_END:
		printf("-SEQ\n");
		break;
	case FYET_SCALAR:
		if (fye->scalar.anchor)
			anchor = fy_token_get_text(fye->scalar.anchor, &anchor_len);
		if (fye->scalar.tag)
			tag = fy_token_get_text(fye->scalar.tag, &tag_len);

		if (fye->scalar.value)
			value = fy_token_get_text(fye->scalar.value, &value_len);

		printf("=VAL");
		if (anchor)
			printf(" &%.*s", (int)anchor_len, anchor);
		if (tag)
			printf(" <%.*s>", (int)tag_len, tag);

		style = fy_token_scalar_style(fye->scalar.value);
		switch (style) {
		case FYAS_PLAIN:
			printf(" :");
			break;
		case FYAS_SINGLE_QUOTED:
			printf(" '");
			break;
		case FYAS_DOUBLE_QUOTED:
			printf(" \"");
			break;
		case FYAS_LITERAL:
			printf(" |");
			break;
		case FYAS_FOLDED:
			printf(" >");
			break;
		default:
			abort();
		}
		print_escaped(value, value_len);
		printf("\n");
		break;
	case FYET_ALIAS:
		anchor = fy_token_get_text(fye->alias.anchor, &anchor_len);
		printf("=ALI *%.*s\n", (int)anchor_len, anchor);
		break;
	default:
		assert(0);
	}
}

int do_testsuite(struct fy_parser *fyp)
{
	struct fy_eventp *fyep;

	while ((fyep = fy_parse_private(fyp)) != NULL) {
		dump_testsuite_event(fyp, &fyep->e);
		fy_parse_eventp_recycle(fyp, fyep);
	}

	return fyp->stream_error ? -1 : 0;
}

static void dump_token(struct fy_parser *fyp, struct fy_token *fyt)
{
	const char *style;

	switch (fyt->type) {
	case FYTT_NONE:
		printf("%s\n", "NONE");
		break;
	case FYTT_STREAM_START:
		printf("%s\n", "STREAM_START");
		break;
	case FYTT_STREAM_END:
		printf("%s\n", "STREAM_END");
		break;
	case FYTT_VERSION_DIRECTIVE:
		printf("%s handle=%s\n", "VERSION_DIRECTIVE",
				fy_atom_get_esc_text_a(&fyt->handle));
		break;
	case FYTT_TAG_DIRECTIVE:
		printf("%s handle='%s'\n", "TAG_DIRECTIVE",
				fy_atom_get_esc_text_a(&fyt->handle));
		break;
	case FYTT_DOCUMENT_START:
		printf("%s\n", "DOCUMENT_START");
		break;
	case FYTT_DOCUMENT_END:
		printf("%s\n", "DOCUMENT_END");
		break;
	case FYTT_BLOCK_SEQUENCE_START:
		printf("%s\n", "BLOCK_SEQUENCE_START");
		break;
	case FYTT_BLOCK_MAPPING_START:
		printf("%s\n", "BLOCK_MAPPING_START");
		break;
	case FYTT_BLOCK_END:
		printf("%s\n", "BLOCK_END");
		break;
	case FYTT_FLOW_SEQUENCE_START:
		printf("%s\n", "FLOW_SEQUENCE_START");
		break;
	case FYTT_FLOW_SEQUENCE_END:
		printf("%s\n", "FLOW_SEQUENCE_END");
		break;
	case FYTT_FLOW_MAPPING_START:
		printf("%s\n", "FLOW_MAPPING_START");
		break;
	case FYTT_FLOW_MAPPING_END:
		printf("%s\n", "FLOW_MAPPING_END");
		break;
	case FYTT_BLOCK_ENTRY:
		printf("%s\n", "BLOCK_ENTRY");
		break;
	case FYTT_FLOW_ENTRY:
		printf("%s\n", "FLOW_ENTRY");
		break;
	case FYTT_KEY:
		printf("%s\n", "KEY");
		break;
	case FYTT_VALUE:
		printf("%s\n", "VALUE");
		break;
	case FYTT_ALIAS:
		printf("%s value='%s'\n", "ALIAS",
				fy_atom_get_esc_text_a(&fyt->handle));
		break;
	case FYTT_ANCHOR:
		printf("%s value='%s'\n", "ANCHOR",
				fy_atom_get_esc_text_a(&fyt->handle));
		break;
	case FYTT_TAG:
		printf("%s tag='%s''\n", "TAG",
				fy_atom_get_esc_text_a(&fyt->handle));
		break;
	case FYTT_SCALAR:
		switch (fyt->scalar.style) {
		case FYSS_ANY:
			style = "ANY";
			break;
		case FYSS_PLAIN:
			style = "PLAIN";
			break;
		case FYSS_SINGLE_QUOTED:
			style = "SINGLE_QUOTED";
			break;
		case FYSS_DOUBLE_QUOTED:
			style = "DOUBLE_QUOTED";
			break;
		case FYSS_LITERAL:
			style = "LITERAL";
			break;
		case FYSS_FOLDED:
			style = "FOLDED";
			break;
		default:
			style = "*illegal*";
			break;
		}
		printf("%s value='%s' style=%s\n", "SCALAR",
				fy_atom_get_esc_text_a(&fyt->handle),
				style);
		break;

	case FYTT_INPUT_MARKER:
		printf("%s value='%s'\n", "INPUT_MARKER",
				fy_atom_get_esc_text_a(&fyt->handle));
		break;
	}
}

int do_scan(struct fy_parser *fyp)
{
	struct fy_token *fyt;

	while ((fyt = fy_scan(fyp)) != NULL) {
		dump_token(fyp, fyt);
		fy_token_unref(fyt);
	}

	return 0;
}

int do_copy(struct fy_parser *fyp)
{
	int c, count, line, column;
	char buf[5], *s;
	const char *str;

	count = 0;
	for (;;) {
		line = fyp->line;
		column = fyp->column;

		c = fy_parse_get(fyp);
		if (c < 0) {
			break;
		}
		if (c == '\\') {
			str = "\\\\";
		} else if (c == '\0') {
			str = "\\0";
		} else if (c == '"') {
			str = "\\\"";
		} else if (c == '\b') {
			str = "\\b";
		} else if (c == '\r') {
			str = "\\r";
		} else if (c == '\t') {
			str = "\\t";
		} else if (c == '\n') {
			str = "\\n";
		} else {
			s = buf;
			if (c < 0x80)
				*s++ = c;
			else if (c < 0x800) {
				*s++ = (c >> 6) | 0xc0;
				*s++ = (c & 0x3f) | 0x80;
			} else if (c < 0x10000) {
				*s++ = (c >> 12) | 0xe0;
				*s++ = ((c >> 6) & 0x3f) | 0x80;
				*s++ = (c & 0x3f) | 0x80;
			} else {
				*s++ = (c >> 18) | 0xf0;
				*s++ = ((c >> 12) & 0x3f) | 0x80;
				*s++ = ((c >> 6) & 0x3f) | 0x80;
				*s++ = (c & 0x3f) | 0x80;
			}
			*s = '\0';
			str = buf;
		}

		printf("[%2d,%2d] = \"%s\"\n", line, column, str);

		count++;
	}
	printf("\ncount=%d\n", count);

	return 0;
}

int do_dump(struct fy_parser *fyp, int indent, int width, bool resolve, bool sort)
{
	struct fy_document *fyd;
	unsigned int flags;
	int rc, count;

	flags = 0;
	if (sort)
		flags |= FYECF_SORT_KEYS;
	flags |= FYECF_INDENT(indent) | FYECF_WIDTH(width);

	count = 0;
	while ((fyd = fy_parse_load_document(fyp)) != NULL) {

		if (resolve) {
			rc = fy_document_resolve(fyd);
			if (rc)
				return -1;
		}

		fy_emit_document_to_file(fyd, flags, NULL);

		fy_parse_document_destroy(fyp, fyd);

		count++;
	}

	return count > 0 ? 0 : -1;
}

#if defined(HAVE_LIBYAML) && HAVE_LIBYAML

void dump_libyaml_token(yaml_token_t *token)
{
	const char *style;

	switch (token->type) {
	case YAML_NO_TOKEN:
		printf("NO\n");
		break;
	case YAML_STREAM_START_TOKEN:
		printf("STREAM_START\n");
		break;
	case YAML_STREAM_END_TOKEN:
		printf("STREAM_END\n");
		break;
	case YAML_VERSION_DIRECTIVE_TOKEN:
		printf("VERSION_DIRECTIVE value=%d.%d\n",
				token->data.version_directive.major,
				token->data.version_directive.minor);
		break;
	case YAML_TAG_DIRECTIVE_TOKEN:
		printf("TAG_DIRECTIVE handle='%s' prefix='%s'\n",
				txt2esc_a(token->data.tag_directive.handle, -1),
				txt2esc_a(token->data.tag_directive.prefix, -1));
		break;
	case YAML_DOCUMENT_START_TOKEN:
		printf("DOCUMENT_START\n");
		break;
	case YAML_DOCUMENT_END_TOKEN:
		printf("DOCUMENT_END\n");
		break;
	case YAML_BLOCK_SEQUENCE_START_TOKEN:
		printf("BLOCK_SEQUENCE_START\n");
		break;
	case YAML_BLOCK_MAPPING_START_TOKEN:
		printf("BLOCK_MAPPING_START\n");
		break;
	case YAML_BLOCK_END_TOKEN:
		printf("BLOCK_END\n");
		break;
	case YAML_FLOW_SEQUENCE_START_TOKEN:
		printf("FLOW_SEQUENCE_START\n");
		break;
	case YAML_FLOW_SEQUENCE_END_TOKEN:
		printf("FLOW_SEQUENCE_END\n");
		break;
	case YAML_FLOW_MAPPING_START_TOKEN:
		printf("FLOW_MAPPING_START\n");
		break;
	case YAML_FLOW_MAPPING_END_TOKEN:
		printf("FLOW_MAPPING_END\n");
		break;
	case YAML_BLOCK_ENTRY_TOKEN:
		printf("BLOCK_ENTRY\n");
		break;
	case YAML_FLOW_ENTRY_TOKEN:
		printf("FLOW_ENTRY\n");
		break;
	case YAML_KEY_TOKEN:
		printf("KEY\n");
		break;
	case YAML_VALUE_TOKEN:
		printf("VALUE\n");
		break;
	case YAML_ALIAS_TOKEN:
		printf("ALIAS value='%s'\n",
				txt2esc_a(token->data.alias.value, -1));
		break;
	case YAML_ANCHOR_TOKEN:
		printf("ANCHOR value='%s'\n",
				txt2esc_a(token->data.anchor.value, -1));
		break;
	case YAML_TAG_TOKEN:
		printf("TAG handle='%s' suffix='%s'\n",
				txt2esc_a(token->data.tag.handle, -1),
				txt2esc_a(token->data.tag.suffix, -1));
		break;
	case YAML_SCALAR_TOKEN:
		switch (token->data.scalar.style) {
		case YAML_ANY_SCALAR_STYLE:
			style = "ANY";
			break;
		case YAML_PLAIN_SCALAR_STYLE:
			style = "PLAIN";
			break;
		case YAML_SINGLE_QUOTED_SCALAR_STYLE:
			style = "SINGLE_QUOTED";
			break;
		case YAML_DOUBLE_QUOTED_SCALAR_STYLE:
			style = "DOUBLE_QUOTED";
			break;
		case YAML_LITERAL_SCALAR_STYLE:
			style = "LITERAL";
			break;
		case YAML_FOLDED_SCALAR_STYLE:
			style = "FOLDED";
			break;
		default:
			style = "*ERROR*";
			break;
		}
		printf("SCALAR value='%s' style=%s\n",
				txt2esc_a(token->data.scalar.value, token->data.scalar.length),
				style);
		break;
	}
}

int do_libyaml_scan(yaml_parser_t *parser)
{
        yaml_token_t token;
        int done = 0;

        while (!done)
        {
            if (!yaml_parser_scan(parser, &token))
		    return -1;

	    dump_libyaml_token(&token);

            done = (token.type == YAML_STREAM_END_TOKEN);

            yaml_token_delete(&token);
        }

	return 0;
}

#define mark_a(_m) \
	({ \
		yaml_mark_t *__m = (_m); \
	 	char *_s = alloca(30); \
	 	snprintf(_s, 30, "%zu/%zu/%zu", __m->index, __m->line, __m->column); \
	 	_s; \
	 })

void dump_libyaml_event(yaml_event_t *event)
{
	char mbuf[40];
	char *mm;
	char *anchor = NULL, *tag = NULL, *value = NULL;

	snprintf(mbuf, sizeof(mbuf), " %10s-%-10s ",
			mark_a(&event->start_mark),
			mark_a(&event->end_mark));
	mm = mbuf;
	mm = " ";

	switch (event->type) {
	case YAML_NO_EVENT:
		printf("NO\n");
		break;
	case YAML_STREAM_START_EVENT:
		printf("%-14s%s|\n", "STREAM_START", mm);
		break;
	case YAML_STREAM_END_EVENT:
		printf("%-14s%s|\n", "STREAM_END", mm);
		break;
	case YAML_DOCUMENT_START_EVENT:
		printf("%-14s%s|\n", "DOCUMENT_START", mm);
		break;
	case YAML_DOCUMENT_END_EVENT:
		printf("%-14s%s|\n", "DOCUMENT_END", mm);
		break;
	case YAML_ALIAS_EVENT:
		anchor = txt2esc_a((char *)event->data.alias.anchor, -1);
		printf("%-14s%s| '%s'\n", "ALIAS", mm, anchor);
		break;
	case YAML_SCALAR_EVENT:
		if (event->data.scalar.anchor)
			anchor = txt2esc_a((char *)event->data.scalar.anchor, -1);
		if (event->data.scalar.tag)
			tag = txt2esc_a((char *)event->data.scalar.tag, -1);
		value = txt2esc_a((char *)event->data.scalar.value, -1);
		printf("%-14s%s|%s%s%s%s%s%s '%s'\n", "SCALAR", mm,
			anchor ? " anchor='" : "", anchor ? : "", anchor ? "'" : "",
			   tag ?    " tag='" : "",    tag ? : "",    tag ? "'" : "",
			value);
		break;
	case YAML_SEQUENCE_START_EVENT:
		if (event->data.sequence_start.anchor)
			anchor = txt2esc_a((char *)event->data.sequence_start.anchor, -1);
		if (event->data.sequence_start.tag)
			tag = txt2esc_a((char *)event->data.sequence_start.tag, -1);
		printf("%-14s%s|%s%s%s%s%s%s\n", "SEQUENCE_START", mm,
			anchor ? " anchor='" : "", anchor ? : "", anchor ? "'" : "",
			   tag ?    " tag='" : "",    tag ? : "",    tag ? "'" : "");
		break;
	case YAML_SEQUENCE_END_EVENT:
		printf("%-14s%s|\n", "SEQUENCE_END", mm);
		break;
	case YAML_MAPPING_START_EVENT:
		if (event->data.mapping_start.anchor)
			anchor = txt2esc_a((char *)event->data.mapping_start.anchor, -1);
		if (event->data.mapping_start.tag)
			tag = txt2esc_a((char *)event->data.mapping_start.tag, -1);
		printf("%-14s%s|%s%s%s%s%s%s\n", "MAPPING_START", mm,
			anchor ? " anchor='" : "", anchor ? : "", anchor ? "'" : "",
			   tag ?    " tag='" : "",    tag ? : "",    tag ? "'" : "");
		break;
	case YAML_MAPPING_END_EVENT:
		printf("%-14s%s|\n", "MAPPING_END", mm);
		break;
	default:
		assert(0);
	}
}

int do_libyaml_parse(yaml_parser_t *parser)
{
        yaml_event_t event;
        int done = 0;

        while (!done)
        {
            if (!yaml_parser_parse(parser, &event))
		    return -1;

	    dump_libyaml_event(&event);

            done = (event.type == YAML_STREAM_END_EVENT);

            yaml_event_delete(&event);
        }

	return 0;
}

void dump_libyaml_testsuite_event(yaml_event_t *event)
{
	switch (event->type) {
	case YAML_NO_EVENT:
		printf("???\n");
		break;
	case YAML_STREAM_START_EVENT:
		printf("+STR\n");
		break;
	case YAML_STREAM_END_EVENT:
		printf("-STR\n");
		break;
	case YAML_DOCUMENT_START_EVENT:
		printf("+DOC");
		if (!event->data.document_start.implicit)
			printf(" ---");
		printf("\n");
		break;
	case YAML_DOCUMENT_END_EVENT:
		printf("-DOC");
		if (!event->data.document_end.implicit)
			printf(" ...");
		printf("\n");
		break;
	case YAML_MAPPING_START_EVENT:
		printf("+MAP");
		if (event->data.mapping_start.anchor)
			printf(" &%s", event->data.mapping_start.anchor);
		if (event->data.mapping_start.tag)
			printf(" <%s>", event->data.mapping_start.tag);
		printf("\n");
		break;
	case YAML_MAPPING_END_EVENT:
      		printf("-MAP\n");
		break;
	case YAML_SEQUENCE_START_EVENT:
		printf("+SEQ");
		if (event->data.sequence_start.anchor)
			printf(" &%s", event->data.sequence_start.anchor);
		if (event->data.sequence_start.tag)
			printf(" <%s>", event->data.sequence_start.tag);
		printf("\n");
		break;
	case YAML_SEQUENCE_END_EVENT:
		printf("-SEQ\n");
		break;
	case YAML_SCALAR_EVENT:
		printf("=VAL");
		if (event->data.scalar.anchor)
			printf(" &%s", event->data.scalar.anchor);
		if (event->data.scalar.tag)
			printf(" <%s>", event->data.scalar.tag);
		switch (event->data.scalar.style) {
		case YAML_PLAIN_SCALAR_STYLE:
			printf(" :");
			break;
		case YAML_SINGLE_QUOTED_SCALAR_STYLE:
			printf(" '");
			break;
		case YAML_DOUBLE_QUOTED_SCALAR_STYLE:
			printf(" \"");
			break;
		case YAML_LITERAL_SCALAR_STYLE:
			printf(" |");
			break;
		case YAML_FOLDED_SCALAR_STYLE:
			printf(" >");
			break;
		case YAML_ANY_SCALAR_STYLE:
			abort();
		}
		print_escaped((char *)event->data.scalar.value, event->data.scalar.length);
		printf("\n");
		break;
	case YAML_ALIAS_EVENT:
		printf("=ALI *%s\n", event->data.alias.anchor);
		break;
	default:
		assert(0);
	}
}

int do_libyaml_testsuite(yaml_parser_t *parser)
{
        yaml_event_t event;
        int done = 0;

        while (!done) {

		if (!yaml_parser_parse(parser, &event))
			return -1;

		dump_libyaml_testsuite_event(&event);

		done = (event.type == YAML_STREAM_END_EVENT);

		yaml_event_delete(&event);
        }

	return 0;
}

int do_libyaml_dump(yaml_parser_t *parser, yaml_emitter_t *emitter)
{
        yaml_document_t document;
        int done = 0;
	int counter;

	yaml_emitter_set_canonical(emitter, 0);

	counter = 0;
        while (!done)
        {
		if (!yaml_parser_load(parser, &document))
			return -1;

		done = !yaml_document_get_root_node(&document);

		if (!done) {
			if (counter > 0)
				printf("# document seperator\n");

			yaml_emitter_dump(emitter, &document);
			counter++;

			yaml_emitter_flush(emitter);
		} else
			yaml_document_delete(&document);
        }

	return 0;
}

#endif

struct fy_kv {
	struct list_head node;
	const char *key;
	const char *value;
};
FY_TYPE_FWD_DECL_LIST(kv);
FY_TYPE_DECL_LIST(kv);

static int hd_accel_kv_hash(struct fy_accel *xl, const void *key, void *userdata, void *hash)
{
	unsigned int *hashp = hash;

	*hashp = XXH32(key, strlen(key), 2654435761U);

	// printf("%s key=%s hash=%08x\n", __func__, (const char *)key, *hashp);
	return 0;
}

static bool hd_accel_kv_eq(struct fy_accel *xl, const void *hash, const void *key1, const void *key2, void *userdata)
{
	return !strcmp(key1, key2);
}

struct fy_kv_store {
	struct fy_kv_list l;
	struct fy_accel xl;
	unsigned int count;
};

static const struct fy_hash_desc hd_kv_store = {
	.size = sizeof(unsigned int),
	.max_bucket_grow_limit = 8,
	.hash = hd_accel_kv_hash,
	.eq = hd_accel_kv_eq,
};

int fy_kv_store_setup(struct fy_kv_store *kvs,
		      unsigned int min_buckets)
{
	int rc;

	if (!kvs)
		return -1;
	memset(kvs, 0, sizeof(*kvs));
	fy_kv_list_init(&kvs->l);

	rc = fy_accel_setup(&kvs->xl, &hd_kv_store, kvs, min_buckets);
	return rc;
}

void fy_kv_store_cleanup(struct fy_kv_store *kvs)
{
	struct fy_kv *kv;
	int rc __FY_DEBUG_UNUSED__;

	if (!kvs)
		return;

	while ((kv = fy_kv_list_pop(&kvs->l)) != NULL) {
		// printf("%s: removing %s: %s\n", __func__, kv->key, kv->value);
		rc = fy_accel_remove(&kvs->xl, kv->key);
		assert(!rc);
		free(kv);
	}

	fy_accel_cleanup(&kvs->xl);
}

int fy_kv_store_insert(struct fy_kv_store *kvs, const char *key, const char *value)
{
	struct fy_kv *kv;
	size_t klen, vlen, size;
	char *s;
	int rc;

	if (!kvs || !key || !value)
		return -1;

	/* no more that UINT_MAX */
	if (kvs->count == UINT_MAX)
		return -1;

	klen = strlen(key);
	vlen = strlen(value);
	size = sizeof(*kv) + klen + 1 + vlen + 1;

	kv = malloc(size);
	if (!kv)
		return -1;
	s = (char *)(kv + 1);
	kv->key = s;
	memcpy(s, key, klen + 1);
	s += klen + 1;
	kv->value = s;
	memcpy(s, value, vlen + 1);

	rc = fy_accel_insert(&kvs->xl, kv->key, kv);
	if (rc) {
		free(kv);
		return rc;
	}
	fy_kv_list_add_tail(&kvs->l, kv);
	kvs->count++;

	// printf("%s: %s: %s #%d\n", __func__, kv->key, kv->value, kvs->count);

	return 0;
}

const char *fy_kv_store_lookup(struct fy_kv_store *kvs, const char *key)
{
	const struct fy_kv *kv;

	if (!kvs || !key)
		return NULL;

	kv = fy_accel_lookup(&kvs->xl, key);
	if (!kv)
		return NULL;

	return kv->value;
}

int fy_kv_store_remove(struct fy_kv_store *kvs, const char *key)
{
	struct fy_kv *kv;
	struct fy_accel_entry *xle;

	if (!kvs || !key)
		return -1;

	xle = fy_accel_entry_lookup(&kvs->xl, key);
	if (!xle) {
		printf("%s:%d: %s key=%s\n", __FILE__, __LINE__, __func__, key);
		return -1;
	}
	kv = (void *)xle->value;
	fy_accel_entry_remove(&kvs->xl, xle);

	fy_kv_list_del(&kvs->l, kv);
	assert(kvs->count > 0);
	kvs->count--;

	// printf("%s: %s: %s #%d\n", __func__, kv->key, kv->value, kvs->count);

	free(kv);

	return 0;
}

const struct fy_kv *fy_kv_store_by_index(struct fy_kv_store *kvs, unsigned int index)
{
	unsigned int i;
	struct fy_kv *kv;

	if (!kvs || index >= kvs->count)
		return NULL;

	for (i = 0, kv = fy_kv_list_first(&kvs->l); kv && i < index; i++, kv = fy_kv_next(&kvs->l, kv))
		;
	return kv;
}

const char *fy_kv_store_key_by_index(struct fy_kv_store *kvs, unsigned int index)
{
	const struct fy_kv *kv;

	kv = fy_kv_store_by_index(kvs, index);
	return kv ? kv->key : NULL;
}

static void do_accel_kv(const struct fy_parse_cfg *cfg, int argc, char *argv[])
{
	struct fy_kv_store kvs;
	unsigned int seed, idx;
	int i, count;
	int rc __FY_DEBUG_UNUSED__;
	char keybuf[16], valbuf[16];
	const char *key;

	seed = 0;	/* we don't care much about seed practices right now */
	rc = fy_kv_store_setup(&kvs, 8);
	assert(!rc);

	count = 1000;

	printf("creating #%d KVs\n", count);

	for (i = 0; i < count; i++) {
		snprintf(keybuf, sizeof(keybuf), "%s-%08x", "key", rand_r(&seed));
		snprintf(valbuf, sizeof(valbuf), "%s-%08x", "val", rand_r(&seed));

		printf("inserting %s: %s\n", keybuf, valbuf);

		rc = fy_kv_store_insert(&kvs, keybuf, valbuf);
		assert(rc == 0);
	}

	while (count > 0) {
		idx = (unsigned int)rand_r(&seed) % count;

		key = fy_kv_store_key_by_index(&kvs, idx);
		assert(key);

		printf("removing #%d - %s\n", idx, key);

		rc = fy_kv_store_remove(&kvs, key);
		assert(!rc);

		count--;
	}

	printf("\n");
	fy_kv_store_cleanup(&kvs);
}

static int do_accel_test(const struct fy_parse_cfg *cfg, int argc, char *argv[])
{
	do_accel_kv(cfg, argc, argv);

	return 0;
}

#if 0
static void test_diag_output(struct fy_diag *diag, void *user, const char *buf, size_t len)
{
	FILE *fp = user;
	static int counter = 0;

	fprintf(fp, "%d: %.*s",  ++counter, (int)len, buf);
}
#endif

int do_build(const struct fy_parse_cfg *cfg, int argc, char *argv[])
{
#if 0
	struct fy_parse_cfg cfg_tmp;
	struct fy_document *fyd;
	struct fy_node *fyn;
	char *buf;
	struct fy_diag_report_ctx drc;
	struct fy_token *fyt;
	void *iter;
	const char *handle, *prefix;
	size_t handle_size, prefix_size;
	int rc __FY_DEBUG_UNUSED__;
	struct fy_atom atom;
#endif

#if 0
	char *path;
	struct fy_node *fynt;
	struct fy_document *fydt;
	struct fy_node_pair *fynp;
	const char *scalar;
	size_t len;
	int count, i, j;
	int rc __FY_DEBUG_UNUSED__;
	int ret __FY_DEBUG_UNUSED__;
	char tbuf[80];
	struct fy_anchor *fya;
	const char *handle, *prefix;
	size_t handle_size, prefix_size;

	/****/

	fydt = fy_document_build_from_string(cfg, "#comment \n[ 42, \n  12 ] # comment\n", FY_NT);
	assert(fydt);

	buf = fy_emit_document_to_string(fydt, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fydt);
	fydt = NULL;

	/****/

	fydt = fy_document_build_from_string(cfg, "plain-scalar # comment\n", FY_NT);
	assert(fydt);

	scalar = fy_node_get_scalar(fy_document_root(fydt), &len);
	printf("root scalar content=\"%.*s\"\n", (int)len, scalar);

	fy_document_destroy(fydt);
	fydt = NULL;

	/****/

	fydt = fy_document_build_from_string(cfg, "[ 10, 11, foo ] # comment", FY_NT);
	assert(fydt);

	buf = fy_emit_node_to_string(fy_document_root(fydt), 0);
	assert(buf);
	printf("resulting node: \"%s\"\n", buf);
	free(buf);

	count = fy_node_sequence_item_count(fy_document_root(fydt));
	printf("count=%d\n", count);
	assert(count == 3);

	/* try iterator first */
	printf("forward iterator:");
	iter = NULL;
	while ((fyn = fy_node_sequence_iterate(fy_document_root(fydt), &iter)) != NULL) {
		buf = fy_emit_node_to_string(fyn, 0);
		assert(buf);
		printf(" \"%s\"", buf);
		free(buf);
	}
	printf("\n");

	printf("reverse iterator:");
	iter = NULL;
	while ((fyn = fy_node_sequence_reverse_iterate(fy_document_root(fydt), &iter)) != NULL) {
		buf = fy_emit_node_to_string(fyn, 0);
		assert(buf);
		printf(" \"%s\"", buf);
		free(buf);
	}
	printf("\n");

	fy_document_destroy(fydt);
	fydt = NULL;

	/*****/

	fydt = fy_document_build_from_string(cfg, "{ foo: 10, bar : 20, baz: [100, 101], [frob, 1]: boo }", FY_NT);
	assert(fydt);

	buf = fy_emit_node_to_string(fy_document_root(fydt), 0);
	assert(buf);
	printf("resulting node: \"%s\"\n", buf);
	free(buf);

	count = fy_node_mapping_item_count(fy_document_root(fydt));
	printf("count=%d\n", count);
	assert(count == 4);

	/* try iterator first */
	printf("forward iterator:");
	iter = NULL;
	while ((fynp = fy_node_mapping_iterate(fy_document_root(fydt), &iter)) != NULL) {
		buf = fy_emit_node_to_string(fynp->key, 0);
		assert(buf);
		printf(" key=\"%s\"", buf);
		free(buf);
		buf = fy_emit_node_to_string(fynp->value, 0);
		assert(buf);
		printf(",value=\"%s\"", buf);
		free(buf);
	}
	printf("\n");

	printf("reverse iterator:");
	iter = NULL;
	while ((fynp = fy_node_mapping_reverse_iterate(fy_document_root(fydt), &iter)) != NULL) {
		buf = fy_emit_node_to_string(fynp->key, 0);
		assert(buf);
		printf(" key=\"%s\"", buf);
		free(buf);
		buf = fy_emit_node_to_string(fynp->value, 0);
		assert(buf);
		printf(",value=\"%s\"", buf);
		free(buf);
	}
	printf("\n");

	printf("index based:");
	for (i = 0; i < count; i++) {
		fynp = fy_node_mapping_get_by_index(fy_document_root(fydt), i);
		assert(fynp);
		buf = fy_emit_node_to_string(fynp->key, 0);
		assert(buf);
		printf(" key=\"%s\"", buf);
		free(buf);
		buf = fy_emit_node_to_string(fynp->value, 0);
		assert(buf);
		printf(",value=\"%s\"", buf);
		free(buf);
	}
	printf("\n");

	printf("key lookup based:");

	fyn = fy_node_mapping_lookup_by_string(fy_document_root(fydt), "foo", FY_NT);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s->\"%s\"\n", "foo", buf);
	free(buf);

	fyn = fy_node_mapping_lookup_by_string(fy_document_root(fydt), "bar", FY_NT);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s->\"%s\"\n", "bar", buf);
	free(buf);

	fyn = fy_node_mapping_lookup_by_string(fy_document_root(fydt), "baz", FY_NT);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s->\"%s\"\n", "baz", buf);
	free(buf);

	fyn = fy_node_mapping_lookup_by_string(fy_document_root(fydt), "[ frob, 1 ]", FY_NT);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s->\"%s\"\n", "[ frob, 1 ]", buf);
	free(buf);

	printf("\n");

	fy_document_destroy(fydt);
	fydt = NULL;

	/*****/

	fydt = fy_document_build_from_string(cfg, "{ "
		"foo: 10, bar : 20, baz:{ frob: boo }, "
		"frooz: [ seq1, { key: value} ], \"zero\\0zero\" : 0, "
		"{ key2: value2 }: { key3: value3 } "
		"}", FY_NT);

	assert(fydt);

	fyn = fy_node_by_path(fy_document_root(fydt), "/", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "/", buf);
	free(buf);

	fyn = fy_node_by_path(fy_document_root(fydt), "foo", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "foo", buf);
	free(buf);

	fyn = fy_node_by_path(fy_document_root(fydt), "bar", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "bar", buf);
	free(buf);

	fyn = fy_node_by_path(fy_document_root(fydt), "baz", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "baz", buf);
	free(buf);

	fyn = fy_node_by_path(fy_document_root(fydt), "baz/frob", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "baz/frob", buf);
	free(buf);

	fyn = fy_node_by_path(fy_document_root(fydt), "frooz", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "frooz", buf);
	free(buf);

	fyn = fy_node_by_path(fy_document_root(fydt), "/frooz/[0]", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "/frooz/[0]", buf);
	free(buf);

	fyn = fy_node_by_path(fy_document_root(fydt), "/frooz/[1]", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "/frooz/[1]", buf);
	free(buf);

	fyn = fy_node_by_path(fy_document_root(fydt), "/frooz/[1]/key", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "/frooz/[1]/key", buf);
	free(buf);

	fyn = fy_node_by_path(fy_document_root(fydt), "\"foo\"", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "\"foo\"", buf);
	free(buf);

	fyn = fy_node_by_path(fy_document_root(fydt), "\"zero\\0zero\"", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "zero\\0zero", buf);
	free(buf);

	fyn = fy_node_by_path(fy_document_root(fydt), "/{ key2: value2 }", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "/{ key2: value2 }", buf);
	free(buf);

	fyn = fy_node_by_path(fy_document_root(fydt), "/{ key2: value2 }/key3", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "/{ key2: value2 }/key3", buf);
	free(buf);

	printf("\npaths....\n");

	path = fy_node_get_path(fy_node_by_path(fy_document_root(fydt), "/", FY_NT, FYNWF_DONT_FOLLOW));
	printf("%s path is %s\n", "/", path);
	if (path)
		free(path);

	path = fy_node_get_path(fy_node_by_path(fy_document_root(fydt), "/frooz", FY_NT, FYNWF_DONT_FOLLOW));
	printf("%s path is %s\n", "/frooz", path);
	if (path)
		free(path);

	path = fy_node_get_path(fy_node_by_path(fy_document_root(fydt), "/frooz/[0]", FY_NT, FYNWF_DONT_FOLLOW));
	printf("%s path is %s\n", "/frooz/[0]", path);
	if (path)
		free(path);

	path = fy_node_get_path(fy_node_by_path(fy_document_root(fydt), "/{ key2: value2 }/key3", FY_NT, FYNWF_DONT_FOLLOW));
	printf("%s path is %s\n", "/{ key2: value2 }/key3", path);
	if (path)
		free(path);

	fy_document_destroy(fydt);
	fydt = NULL;

	/*****/

	fyd = fy_document_create(cfg);
	assert(fyd);

	fyn = fy_node_build_from_string(fyd, "{ }", FY_NT);
	assert(fyn);

	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "/", buf);
	free(buf);

	fy_document_set_root(fyd, fyn);

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/******/

	fyd = fy_document_create(cfg);
	assert(fyd);

	fyn = fy_node_create_scalar(fyd, "foo", 3);
	assert(fyn);
	fy_document_set_root(fyd, fyn);

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/******/

	fyd = fy_document_create(cfg);
	assert(fyd);

	fyn = fy_node_create_scalar(fyd, "foo\nfoo", 7);
	assert(fyn);
	fy_document_set_root(fyd, fyn);

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/******/

	fyd = fy_document_create(cfg);
	assert(fyd);

	fyn = fy_node_create_sequence(fyd);
	assert(fyn);
	fy_document_set_root(fyd, fyn);

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/******/

	fyd = fy_document_create(cfg);
	assert(fyd);

	fyn = fy_node_create_mapping(fyd);
	assert(fyn);
	fy_document_set_root(fyd, fyn);

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/******/

	fyd = fy_document_create(cfg);
	assert(fyd);

	fyn = fy_node_create_sequence(fyd);
	assert(fyn);

	fy_node_sequence_append(fyn, fy_node_create_scalar(fyd, "foo", FY_NT));
	fy_node_sequence_append(fyn, fy_node_create_scalar(fyd, "bar", FY_NT));
	fy_node_sequence_append(fyn, fy_node_build_from_string(fyd, "{ baz: frooz }", FY_NT));

	fy_document_set_root(fyd, fyn);

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/******/

	fyd = fy_document_create(cfg);
	assert(fyd);

	fyn = fy_node_create_mapping(fyd);
	assert(fyn);

	rc = fy_node_mapping_append(fyn, NULL, fy_node_build_from_string(fyd, "[ 0, 1 ]", FY_NT));
	assert(!rc);

	fynt = fy_node_build_from_string(fyd, "foo", FY_NT);
	assert(fynt);
	rc = fy_node_mapping_append(fyn, NULL, fynt);
	assert(rc);
	fy_node_free(fynt);

	rc = fy_node_mapping_append(fyn, fy_node_build_from_string(fyd, "bar", FY_NT), NULL);
	assert(!rc);

	fy_document_set_root(fyd, fyn);

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/******/

	fyd = fy_document_create(cfg);
	assert(fyd);

	fy_document_set_root(fyd, fy_node_build_from_string(fyd, "scalar", FY_NT));

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/******/

	fyd = fy_document_create(cfg);
	assert(fyd);

	fyn = fy_node_create_sequence(fyd);
	assert(fyn);

	fy_node_sequence_append(fyn, fy_node_build_from_string(fyd, "foo", FY_NT));
	fy_node_sequence_append(fyn, fy_node_build_from_string(fyd, "bar", FY_NT));

	fy_document_set_root(fyd, fyn);

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/******/

	fyd = fy_document_build_from_string(cfg, "[ one, two, four ]", FY_NT);

	fy_node_sequence_append(fy_document_root(fyd), fy_node_build_from_string(fyd, "five", FY_NT));
	fy_node_sequence_prepend(fy_document_root(fyd), fy_node_build_from_string(fyd, "zero", FY_NT));

	fy_node_sequence_insert_after(fy_document_root(fyd),
			fy_node_by_path(fy_document_root(fyd), "/[2]", FY_NT, FYNWF_DONT_FOLLOW),
			fy_node_build_from_string(fyd, "three", FY_NT));

	fy_node_sequence_insert_before(fy_document_root(fyd),
			fy_node_by_path(fy_document_root(fyd), "/[3]", FY_NT, FYNWF_DONT_FOLLOW),
			fy_node_build_from_string(fyd, "two-and-a-half", FY_NT));

	fyn = fy_node_sequence_remove(fy_document_root(fyd),
			fy_node_by_path(fy_document_root(fyd), "/[3]", FY_NT, FYNWF_DONT_FOLLOW));

	fy_node_free(fyn);

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/******/

	fyd = fy_document_build_from_string(cfg, "{ one: 1, two: 2, four: 4 }", FY_NT);

	fy_node_mapping_append(fy_document_root(fyd),
			fy_node_build_from_string(fyd, "three", FY_NT),
			fy_node_build_from_string(fyd, "3", FY_NT));

	fy_node_mapping_prepend(fy_document_root(fyd),
			fy_node_build_from_string(fyd, "zero", FY_NT),
			fy_node_build_from_string(fyd, "0", FY_NT));

	fy_node_mapping_append(fy_document_root(fyd),
			fy_node_build_from_string(fyd, "two-and-a-half", FY_NT),
			fy_node_build_from_string(fyd, "2.5", FY_NT));

	fyn = fy_node_mapping_remove_by_key(fy_document_root(fyd),
			fy_node_build_from_string(fyd, "two-and-a-half", FY_NT));
	assert(fyn != NULL);

	fy_node_free(fyn);

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/******/

	fyd = fy_document_build_from_string(cfg, "{ aaa: 1, zzz: 2, bbb: 4 }", FY_NT);

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/******/

	fyd = fy_document_build_from_string(cfg, "\naaa: 1\nzzz: 2\nbbb:\n  ccc: foo\n", FY_NT);

	buf = fy_emit_document_to_string(fyd, FYECF_MODE_FLOW);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/******/

	fyd = fy_document_build_from_string(cfg, "{ aaa: 1, zzz: 2, bbb: 4 }", FY_NT);

	buf = fy_emit_document_to_string(fyd, FYECF_MODE_BLOCK);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/******/

	/* {? {z: bar} : map-value1, ? {a: whee} : map-value2, ? [a, b, c] : seq-value, ? [z] : {frooz: ting}, aaa: 1, bbb: 4, zzz: 2} */
	fyd = fy_document_build_from_string(cfg, "{ a: 5, { z: bar }: 1, z: 7, "
					"[ a, b, c] : 3, { a: whee } : 2 , b: 6, [ z ]: 4 }", FY_NT);

	buf = fy_emit_document_to_string(fyd, FYECF_SORT_KEYS);
	assert(buf);
	printf("resulting document (sorted1):\n");
	fputs(buf, stdout);
	free(buf);

	ret = fy_node_sort(fy_document_root(fyd), NULL, NULL);
	assert(!ret);

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document (sorted2):\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

#if 0
	/******/

	cfg_tmp = *cfg;
	cfg_tmp.flags |= FYPCF_COLLECT_DIAG;

	/* this is an error */
	fyd = fy_document_build_from_string(&cfg_tmp, "{ a: 5 ]");
	assert(fyd);
	assert(!fyd->root);

	fprintf(stderr, "error log:\n%s", fy_document_get_log(fyd, NULL));

	fy_document_destroy(fyd);
#endif

	/******/

	fyd = fy_document_buildf(cfg, "{ %s: %d, zzz: 2, bbb: 4 }", "aaa", 1);
	assert(fyd);

	buf = fy_emit_document_to_string(fyd, FYECF_MODE_BLOCK);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/******/

	fyd = fy_document_buildf(cfg, "{ %s: %d, zzz: \"this is\n\ntext\", bbb: 4 }", "aaa", 1);
	assert(fyd);

	i = j = -1;
	count = fy_document_scanf(fyd, "aaa %d bbb %d zzz %79[^\xc0]s", &i, &j, tbuf);
	printf("count=%d, aaa=%d bbb=%d zzz=\"%s\"\n", count, i, j, tbuf);

	fy_document_destroy(fyd);

	/******/

	fyd = fy_document_create(cfg);
	assert(fyd);

	rc = fy_document_tag_directive_add(fyd, "!foo!", "tag:bar.com,2019:");
	assert(!rc);

	rc = fy_document_tag_directive_add(fyd, "!e!", "tag%21");
	assert(!rc);

	fyn = fy_node_build_from_string(fyd, "{ foo: bar }", FY_NT);
	assert(fyn);

	fy_document_set_root(fyd, fyn);

	/* rc = fy_node_set_tag(fyn, "!!", -1); */
	/* rc = fy_node_set_tag(fyn, "!!int", -1); */
	/* rc = fy_node_set_tag(fyn, "!<tag:clarkevans.com,2002:>", -1); */
	rc = fy_node_set_tag(fyn, "!foo!baz", FY_NT);
	/* rc = fy_node_set_tag(fyn, "!e!tag%21", -1); */
	/* rc = fy_node_set_tag(fyn, "!e!tag:12:", -1); */
	assert(!rc);

	// fy_document_tag_directive_remove(fyd, "!foo!");

	rc = fy_node_set_anchor(fy_node_by_path(fyn, "/foo", FY_NT, FYNWF_DONT_FOLLOW), "anchor", FY_NT);
	assert(!rc);

	rc = fy_node_mapping_append(fyn,
			fy_node_build_from_string(fyd, "!foo!whoa baz", FY_NT),
			fy_node_build_from_string(fyd, "frooz", FY_NT));
	assert(!rc);

	rc = fy_node_mapping_append(fyn,
			fy_node_build_from_string(fyd, "test", FY_NT),
			fy_node_build_from_string(fyd, "*anchor", FY_NT));
	assert(!rc);

	rc = fy_node_mapping_append(fyn,
			fy_node_build_from_string(fyd, "test-2", FY_NT),
			fy_node_create_alias(fyd, "anchor", FY_NT));
	assert(!rc);

	if (cfg->flags & FYPCF_RESOLVE_DOCUMENT) {
		rc = fy_document_resolve(fyd);
		assert(!rc);
	}

	fprintf(stderr, "tag directives of document\n");
	/* dump directives */
	iter = NULL;
	while ((fyt = fy_document_tag_directive_iterate(fyd, &iter)) != NULL) {
		handle = fy_tag_directive_token_handle(fyt, &handle_size);
		assert(handle);

		prefix = fy_tag_directive_token_prefix(fyt, &prefix_size);
		assert(prefix);

		fprintf(stderr, "tag-directive \"%.*s\" \"%.*s\"\n",
				(int)handle_size, handle,
				(int)prefix_size, prefix);
	}

	fprintf(stderr, "anchors of document\n");
	iter = NULL;
	while ((fya = fy_document_anchor_iterate(fyd, &iter)) != NULL) {

		path = fy_node_get_path(fy_anchor_node(fya));
		assert(path);

		anchor = fy_anchor_get_text(fya, &anchor_size);
		assert(anchor);

		fprintf(stderr, "&%.*s %s\n", (int)anchor_size, anchor, path);

		free(path);
	}

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);

	/*****/

	printf("\nJSON pointer tests\n");

	fyd = fy_document_build_from_string(cfg,
		"{\n"
		"  \"foo\": [\"bar\", \"baz\"],\n"
		"  \"\": 0,\n"
		"  \"a/b\": 1,\n"
		"  \"c%d\": 2,\n"
		"  \"e^f\": 3,\n"
		"  \"g|h\": 4,\n"
		"  \"i\\\\j\": 5,\n"
		"  \"k\\\"l\": 6,\n"
		"  \" \": 7,\n"
		"  \"m~n\": 8\n"
		"}" , FY_NT);

	assert(fyd);

	fyn = fy_node_by_path(fy_document_root(fyd), "/", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "/", buf);
	free(buf);

	{
		const char *json_paths[] = {
			"",
			"/foo",
			"/foo/0",
			"/",
			"/a~1b",
			"/c%d",
			"/e^f",
			"/g|h",
			"/i\\j",
			"/k\"l",
			"/ ",
			"/m~0n"
		};
		unsigned int ii;

		for (ii = 0; ii < sizeof(json_paths)/sizeof(json_paths[0]); ii++) {
			fyn = fy_node_by_path(fy_document_root(fyd), json_paths[ii], FY_NT, FYNWF_PTR_JSON);
			if (!fyn) {
				printf("Unable to lookup JSON path: '%s'\n", json_paths[ii]);
			} else {
				buf = fy_emit_node_to_string(fyn, 0);
				assert(buf);
				printf("JSON path: '%s' is '%s'\n", json_paths[ii], buf);
				free(buf);
			}
			printf("\n");
		}
	}

	fy_document_destroy(fyd);
	fyd = NULL;

	/*****/

	printf("\nRelative JSON pointer tests\n");

	fyd = fy_document_build_from_string(cfg,
		"{\n"
		"  \"foo\": [\"bar\", \"baz\"],\n"
		"  \"highly\": {\n"
		"    \"nested\": {\n"
		"      \"objects\": true\n"
		"    }\n"
		"  }\n"
		"}\n", FY_NT);
	assert(fyd);

	fyn = fy_node_by_path(fy_document_root(fyd), "/", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	buf = fy_emit_node_to_string(fyn, 0);
	assert(buf);
	printf("%s is \"%s\"\n", "/", buf);
	free(buf);

	{
		const char *reljson_paths[] = {
			"0",
			"1/0",
			"2/highly/nested/objects",
		};
		unsigned int ii;

		for (ii = 0; ii < sizeof(reljson_paths)/sizeof(reljson_paths[0]); ii++) {
			fyn = fy_node_by_path(
					fy_node_by_path(fy_document_root(fyd), "/foo/1", FY_NT, FYNWF_DONT_FOLLOW),	/* "baz" */
					reljson_paths[ii], FY_NT, FYNWF_PTR_RELJSON);
			if (!fyn) {
				printf("Unable to lookup relative JSON path: '%s'\n", reljson_paths[ii]);
			} else {
				buf = fy_emit_node_to_string(fyn, 0);
				assert(buf);
				printf("Relative JSON path: '%s' is '%s'\n", reljson_paths[ii], buf);
				free(buf);
			}
			printf("\n");
		}
	}

	{
		const char *reljson_paths[] = {
			"0/objects",
			"1/nested/objects",
			"2/foo/0",
		};
		unsigned int ii;

		for (ii = 0; ii < sizeof(reljson_paths)/sizeof(reljson_paths[0]); ii++) {
			fyn = fy_node_by_path(
					fy_node_by_path(fy_document_root(fyd), "/highly/nested", FY_NT, FYNWF_DONT_FOLLOW),	/* "baz" */
					reljson_paths[ii], FY_NT, FYNWF_PTR_RELJSON);
			if (!fyn) {
				printf("Unable to lookup relative JSON path: '%s'\n", reljson_paths[ii]);
			} else {
				buf = fy_emit_node_to_string(fyn, 0);
				assert(buf);
				printf("Relative JSON path: '%s' is '%s'\n", reljson_paths[ii], buf);
				free(buf);
			}
			printf("\n");
		}
	}

	fy_document_destroy(fyd);
	fyd = NULL;

	/*****/

	fyd = fy_document_create(NULL);
	assert(fyd);

	fyn = fy_node_create_sequence(fyd);
	assert(fyn);

	fy_document_set_root(fyd, fyn);
	fyn = NULL;

	fyn = fy_node_build_from_string(fyd, "%TAG !e! tag:example.com,2000:app/\n---\n- foo\n- !e!foo bar\n", FY_NT);
	assert(fyn);

	rc = fy_node_sequence_append(fy_document_root(fyd), fyn);

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	printf("tag directives:\n");
	iter = NULL;
	while ((fyt = fy_document_tag_directive_iterate(fyd, &iter)) != NULL) {
		handle = fy_tag_directive_token_handle(fyt, &handle_size);
		prefix = fy_tag_directive_token_prefix(fyt, &prefix_size);
		printf("> handle='%.*s' prefix='%.*s'\n",
				(int)handle_size, handle,
				(int)prefix_size, prefix);
	}

	/* try to build another, but with a different !e! prefix, it must fail */
	fyn = fy_node_build_from_string(fyd, "%TAG !e! tag:example.com,2019:app/\n---\n- foo\n- !e!foo bar\n", FY_NT);
	assert(!fyn);

	rc = fy_document_tag_directive_add(fyd, "!f!", "tag:example.com,2019:f/");
	assert(!rc);

	printf("new tag directives:\n");
	iter = NULL;
	while ((fyt = fy_document_tag_directive_iterate(fyd, &iter)) != NULL) {
		handle = fy_tag_directive_token_handle(fyt, &handle_size);
		prefix = fy_tag_directive_token_prefix(fyt, &prefix_size);
		printf("> handle='%.*s' prefix='%.*s'\n",
				(int)handle_size, handle,
				(int)prefix_size, prefix);
	}

	fyn = fy_node_build_from_string(fyd, "!f!whiz frooz\n", FY_NT);
	assert(fyn);

	rc = fy_node_sequence_append(fy_document_root(fyd), fyn);

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("resulting document:\n");
	fputs(buf, stdout);
	free(buf);

	fy_document_destroy(fyd);
	fyd = NULL;

	/***************/

	fyp_debug(NULL,   FYEM_INTERNAL, "(debug)   test");
	fyp_info(NULL,    "(info)    test");
	fyp_notice(NULL,  "(notice)  test");
	fyp_warning(NULL, "(warning) test");
	fyp_error(NULL,   "(error)   test");

	/****/

	fyd = fy_document_build_from_string(cfg,
			"{\n"
			"  { foo: bar }: baz,\n"
			"  frooz: whee,\n"
			"  houston: [ we, have, a, problem ]\n"
			"}", FY_NT);
	assert(fyd);

	printf("***************************\n");
	printf("fy_node_is_synthetic(\"/\") = %s\n",
			fy_node_is_synthetic(fy_document_root(fyd)) ? "true" : "false");
	printf("***************************\n");

	fyn = fy_node_by_path(fy_document_root(fyd), "/{ foo: bar }", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	printf("fy_node_is_synthetic(\"/{ foo: bar }\") = %s\n",
			fy_node_is_synthetic(fyn) ? "true" : "false");

	buf = fy_emit_node_to_string(fyn, FYECF_MODE_FLOW_ONELINE | FYECF_WIDTH_INF);
	assert(buf);
	printf("/{ foo: bar }: %s\n", buf);
	free(buf);

	memset(&drc, 0, sizeof(drc));
	drc.type = FYET_NOTICE;
	drc.module = FYEM_DOC;
	drc.fyt = fy_token_ref(fyn->scalar);
	fy_document_diag_report(fyd, &drc, "Test %d", 12);

	fyn = fy_node_by_path(fy_document_root(fyd), "/houston", FY_NT, FYNWF_DONT_FOLLOW);
	assert(fyn);
	printf("fy_node_is_synthetic(/houston) = %s\n",
			fy_node_is_synthetic(fyn) ? "true" : "false");

	buf = fy_emit_node_to_string(fyn, FYECF_MODE_FLOW_ONELINE | FYECF_WIDTH_INF);
	assert(buf);
	printf("/houston: %s\n", buf);
	free(buf);

	memset(&drc, 0, sizeof(drc));
	drc.type = FYET_NOTICE;
	drc.module = FYEM_DOC;
	drc.fyt = fy_token_ref(fyn->sequence_start);
	fy_document_diag_report(fyd, &drc, "Test %d", 13);

	fyt = fy_node_token(fyn);
	assert(fyt);

	memset(&drc, 0, sizeof(drc));
	drc.type = FYET_NOTICE;
	drc.module = FYEM_DOC;
	drc.fyt = fyt;
	fy_document_diag_report(fyd, &drc, "Test %d", 14);

	printf("***************************\n");
	printf("fy_node_is_synthetic(\"/\") = %s\n",
			fy_node_is_synthetic(fy_document_root(fyd)) ? "true" : "false");
	printf("***************************\n");

	fy_node_sequence_append(fy_node_by_path(fy_document_root(fyd), "/houston", FY_NT, FYNWF_DONT_FOLLOW),
				fy_node_create_scalar(fyd, "synthesonic", FY_NT));

	printf("***************************\n");
	printf("fy_node_is_synthetic(\"/\") = %s\n",
			fy_node_is_synthetic(fy_document_root(fyd)) ? "true" : "false");
	printf("***************************\n");

	fyt = fy_node_token(fy_document_root(fyd));
	assert(fyt);

	memset(&drc, 0, sizeof(drc));
	drc.type = FYET_NOTICE;
	drc.module = FYEM_DOC;
	drc.fyt = fyt;
	fy_document_diag_report(fyd, &drc, "Test %d", 16);

	fy_node_mapping_append(fy_document_root(fyd),
			fy_node_create_scalar(fyd, "key", FY_NT),
			fy_node_create_scalar(fyd, "value", FY_NT));

	printf("***************************\n");
	printf("fy_node_is_synthetic(\"/\") = %s\n",
			fy_node_is_synthetic(fy_document_root(fyd)) ? "true" : "false");
	printf("***************************\n");

	fy_node_sequence_append(fy_node_by_path(fy_document_root(fyd), "/houston", FY_NT, FYNWF_DONT_FOLLOW),
				fy_node_create_scalar(fyd, "item", FY_NT));

	fyt = fy_node_token(fy_node_by_path(fy_document_root(fyd), "/houston", FY_NT, FYNWF_DONT_FOLLOW));
	assert(fyt);

	memset(&drc, 0, sizeof(drc));
	drc.type = FYET_NOTICE;
	drc.module = FYEM_DOC;
	drc.fyt = fyt;
	fy_document_diag_report(fyd, &drc, "Test %d", 17);

	fy_node_report(fy_node_by_path(fy_document_root(fyd), "/houston/0", FY_NT, FYNWF_DONT_FOLLOW),
			FYET_WARNING, "/houston/0 checking report");

	buf = fy_emit_document_to_string(fyd, 0);
	assert(buf);
	printf("/:\n");
	fputs(buf, stdout);
	free(buf);

	buf = fy_emit_document_to_string(fyd, FYECF_MODE_FLOW_ONELINE | FYECF_WIDTH_INF);
	assert(buf);
	printf("%s\n", buf);
	free(buf);

	fy_diag_printf(fyd->diag, "Outputting on document diag\n");

	fy_debug(fyd->diag, "Debug level\n");
	fy_info(fyd->diag, "Info level\n");
	fy_notice(fyd->diag, "Notice level\n");
	fy_warning(fyd->diag, "Warning level\n");
	fy_error(fyd->diag, "Error level\n");

	fy_document_destroy(fyd);
	fyd = NULL;

	{
		struct fy_diag *diag;
		struct fy_diag_cfg dcfg;
		struct fy_parse_cfg pcfg;
		FILE *fp;
		char *mbuf = NULL;
		size_t msize;

		fp = open_memstream(&mbuf, &msize);
		assert(fp);

		fy_diag_cfg_default(&dcfg);
		dcfg.fp = fp;
		dcfg.colorize = isatty(fileno(stderr)) == 1;

		diag = fy_diag_create(&dcfg);
		assert(diag);

		fy_error(diag, "Writting in the diagnostic\n");

		memset(&pcfg, 0, sizeof(pcfg));
		pcfg.flags = FYPCF_DEFAULT_DOC;
		pcfg.diag = diag;
		fyd = fy_document_build_from_string(&pcfg, "{ foo: \"\\xeh\", foo: baz }", FY_NT);
		/* the document must not be created (duplicate key) */
		assert(!fyd);

		fy_diag_destroy(diag);

		fclose(fp);

		assert(mbuf);

		printf("checking diagnostic\n");
		fwrite(mbuf, msize, 1, stdout);

		free(mbuf);
	}

	{
		struct fy_diag *diag;
		struct fy_diag_cfg dcfg;
		struct fy_parse_cfg pcfg;
		FILE *fp;
		char *mbuf = NULL;
		size_t msize;

		fp = open_memstream(&mbuf, &msize);
		assert(fp);

		fy_diag_cfg_default(&dcfg);
		dcfg.fp = NULL;
		dcfg.colorize = isatty(fileno(stderr)) == 1;
		dcfg.output_fn = test_diag_output;
		dcfg.user = stderr;

		diag = fy_diag_create(&dcfg);
		assert(diag);

		fy_error(diag, "Writting in the diagnostic\n");

		memset(&pcfg, 0, sizeof(pcfg));
		pcfg.flags = FYPCF_DEFAULT_DOC;
		pcfg.diag = diag;
		fyd = fy_document_build_from_string(&pcfg, "{ foo: \"\\xeh\", foo: baz }", FY_NT);
		/* the document must not be created (duplicate key) */
		assert(!fyd);

		fy_diag_destroy(diag);

		fclose(fp);

		assert(mbuf);

		printf("checking diagnostic\n");
		fwrite(mbuf, msize, 1, stdout);

		free(mbuf);
	}
#endif
	/*****/

#if 0
	{
//#define MANUAL_SCALAR_STR "val\"quote'\0null\0&the\nrest  "
//#define MANUAL_SCALAR_STR "0\n1"
//#define MANUAL_SCALAR_STR "\\\"\0\a\b\t\v\f\r\e\xc2\x85\xc2\xa0\xe2\x80\xa8\xe2\x80\xa9"
//#define MANUAL_SCALAR_STR "\\\"\0\a\b\t\v\f\r\e\n"
//#define MANUAL_SCALAR_STR "\xc2\x85"
//#define MANUAL_SCALAR_STR "\xc2\xa0"
#define MANUAL_SCALAR_STR "\xff\xff\xff\xff"
//#define MANUAL_SCALAR_STR "foo\xf9\xff\xffzbar\xff\xffwz"

		const char *what = MANUAL_SCALAR_STR;
		size_t what_sz = sizeof(MANUAL_SCALAR_STR) - 1;
		struct fy_document *fyd;
		struct fy_node *fyn;
		char *buf, *buf2;

		fyd = fy_document_create(cfg);
		assert(fyd);

		fyn = fy_node_create_scalar(fyd, what, what_sz);
		assert(fyn);

		fy_document_set_root(fyd, fyn);

		buf = fy_emit_document_to_string(fyd, 0);
		assert(buf);
		fputs(buf, stdout);
		fy_document_destroy(fyd);

		fyd = fy_document_build_from_string(cfg, buf, FY_NT);
		assert(fyd);

		buf2 = fy_emit_document_to_string(fyd, 0);
		assert(buf2);
		fputs(buf2, stdout);
		fy_document_destroy(fyd);

		free(buf);
		free(buf2);
	}
#endif

	do_accel_test(cfg, argc, argv);

	return 0;
}

static int modify_module_flags(const char *what, unsigned int *flagsp)
{
	static const struct {
		const char *name;
		unsigned int set;
		unsigned int clr;
	} mf[] = {
		{ .name = "all",	.set = FYPCF_DEBUG_ALL, },
		{ .name = "none",	.clr = FYPCF_DEBUG_ALL, },
		{ .name = "default",	.set = FYPCF_DEBUG_DEFAULT, .clr = ~FYPCF_DEBUG_DEFAULT },
		{ .name = "unknown",	.set = FYPCF_DEBUG_UNKNOWN },
		{ .name = "atom",	.set = FYPCF_DEBUG_ATOM },
		{ .name = "scan",	.set = FYPCF_DEBUG_SCAN },
		{ .name = "parse",	.set = FYPCF_DEBUG_PARSE },
		{ .name = "doc",	.set = FYPCF_DEBUG_DOC },
		{ .name = "build",	.set = FYPCF_DEBUG_BUILD },
		{ .name = "internal",	.set = FYPCF_DEBUG_INTERNAL },
		{ .name = "system",	.set = FYPCF_DEBUG_SYSTEM },
	};
	unsigned int i;

	if (!what || !flagsp)
		return -1;

	for (i = 0; i < sizeof(mf)/sizeof(mf[0]); i++) {
		if (!strcmp(what, mf[i].name)) {
			*flagsp |=  mf[i].set;
			*flagsp &= ~mf[i].clr;
			return 0;
		}
	}

	return -1;
}

static int modify_debug_diag_flags(const char *what, unsigned int *flagsp)
{
	static const struct {
		const char *name;
		unsigned int set;
		unsigned int clr;
	} df[] = {
		{ .name = "all",	.set = FYPCF_DEBUG_DIAG_ALL, },
		{ .name = "none",	.clr = FYPCF_DEBUG_DIAG_ALL, },
		{ .name = "default",	.set = FYPCF_DEBUG_DIAG_DEFAULT, .clr = ~FYPCF_DEBUG_DIAG_DEFAULT },
		{ .name = "source",	.set = FYPCF_DEBUG_DIAG_SOURCE },
		{ .name = "position",	.set = FYPCF_DEBUG_DIAG_POSITION },
		{ .name = "type",	.set = FYPCF_DEBUG_DIAG_TYPE },
		{ .name = "module",	.set = FYPCF_DEBUG_DIAG_MODULE },
	};
	unsigned int i;

	if (!what || !flagsp)
		return -1;

	for (i = 0; i < sizeof(df)/sizeof(df[0]); i++) {
		if (!strcmp(what, df[i].name)) {
			*flagsp |=  df[i].set;
			*flagsp &= ~df[i].clr;
			return 0;
		}
	}

	return -1;
}

static int modify_debug_level_flags(const char *what, unsigned int *flagsp)
{
	static const struct {
		const char *name;
		unsigned int set;
		unsigned int clr;
	} lf[] = {
		{ .name = "default",	.set = FYPCF_DEBUG_LEVEL(DEBUG_LEVEL_DEFAULT), .clr = ~FYPCF_DEBUG_LEVEL(DEBUG_LEVEL_DEFAULT) },
		{ .name = "debug",	.set = FYPCF_DEBUG_LEVEL_DEBUG, .clr = ~FYPCF_DEBUG_LEVEL_DEBUG },
		{ .name = "info",	.set = FYPCF_DEBUG_LEVEL_INFO, .clr = ~FYPCF_DEBUG_LEVEL_INFO },
		{ .name = "notice",	.set = FYPCF_DEBUG_LEVEL_NOTICE, .clr = ~FYPCF_DEBUG_LEVEL_NOTICE },
		{ .name = "warning",	.set = FYPCF_DEBUG_LEVEL_WARNING, .clr = ~FYPCF_DEBUG_LEVEL_WARNING },
		{ .name = "error",	.set = FYPCF_DEBUG_LEVEL_ERROR, .clr = ~FYPCF_DEBUG_LEVEL_ERROR },
	};
	unsigned int i;

	if (!what || !flagsp)
		return -1;

	for (i = 0; i < sizeof(lf)/sizeof(lf[0]); i++) {
		if (!strcmp(what, lf[i].name)) {
			*flagsp |=  lf[i].set;
			*flagsp &= ~lf[i].clr;
			return 0;
		}
	}

	return -1;
}

int apply_flags_option(const char *arg, unsigned int *flagsp,
		int (*modify_flags)(const char *what, unsigned int *flagsp))
{
	const char *s, *e, *sn;
	char *targ;
	int len, ret;

	if (!arg || !flagsp || !modify_flags)
		return -1;

	s = arg;
	e = arg + strlen(s);
	while (s < e) {
		sn = strchr(s, ',');
		if (!sn)
			sn = e;

		len = sn - s;
		targ = alloca(len + 1);
		memcpy(targ, s, len);
		targ[len] = '\0';

		ret = modify_flags(targ, flagsp);
		if (ret)
			return ret;

		s = sn < e ? (sn + 1) : sn;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct fy_parser ctx, *fyp = &ctx;
	struct fy_parse_cfg cfg = {
		.search_path = INCLUDE_DEFAULT,
		.flags =
			(QUIET_DEFAULT ? FYPCF_QUIET : 0) |
			FYPCF_DEBUG_LEVEL(DEBUG_LEVEL_DEFAULT) |
			FYPCF_DEBUG_DIAG_DEFAULT | FYPCF_DEBUG_DEFAULT,
	};
	struct fy_input_cfg *fyic, *fyic_array = NULL;
	int i, j, icount, rc, exitcode = EXIT_FAILURE, opt, lidx;
	char *tmp, *s;
	const char *mode = MODE_DEFAULT;
	int indent = INDENT_DEFAULT;
	int width = WIDTH_DEFAULT;
	bool resolve = RESOLVE_DEFAULT;
	bool sort = SORT_DEFAULT;
	size_t chunk = CHUNK_DEFAULT;
	const char *color = COLOR_DEFAULT;

	fy_valgrind_check(&argc, &argv);

	while ((opt = getopt_long_only(argc, argv, "I:m:i:w:d:rsc:C:D:M:qh", lopts, &lidx)) != -1) {
		switch (opt) {
		case 'I':
			tmp = alloca(strlen(cfg.search_path) + 1 + strlen(optarg) + 1);
			s = tmp;
			strcpy(s, cfg.search_path);
			if (cfg.search_path && cfg.search_path[0]) {
				s += strlen(cfg.search_path);
				*s++ = ':';
			}
			strcpy(s, optarg);
			s += strlen(optarg);
			*s = '\0';
			cfg.search_path = tmp;
			break;
		case 'm':
			mode = optarg;
			break;
		case 'i':
			indent = atoi(optarg);
			break;
		case 'w':
			width = atoi(optarg);
			break;
		case 'd':
			cfg.flags &= ~FYPCF_DEBUG_LEVEL(FYPCF_DEBUG_LEVEL_MASK);

			if (isdigit(*optarg)) {
				i = atoi(optarg);
				if (i <= FYET_ERROR) {
					cfg.flags |= FYPCF_DEBUG_LEVEL(i);
					rc = 0;
				} else
					rc = -1;
			} else
				rc = apply_flags_option(optarg, &cfg.flags, modify_debug_level_flags);

			if (rc) {
				fprintf(stderr, "bad diag option %s\n", optarg);
				display_usage(stderr, argv[0]);
			}

			break;
		case 'r':
			resolve = true;
			cfg.flags |= FYPCF_RESOLVE_DOCUMENT;
			break;
		case 's':
			sort = true;
			break;
		case 'c':
			chunk = atoi(optarg);
			break;
		case 'C':
			color = optarg;
			cfg.flags &= ~FYPCF_COLOR(FYPCF_COLOR_MASK);
			if (!strcmp(color, "auto"))
				cfg.flags |= FYPCF_COLOR_AUTO;
			else if (!strcmp(color, "yes") || !strcmp(color, "1") || !strcmp(color, "on"))
				cfg.flags |= FYPCF_COLOR_FORCE;
			else if (!strcmp(color, "no") || !strcmp(color, "0") || !strcmp(color, "off"))
				cfg.flags |= FYPCF_COLOR_NONE;
			else {
				fprintf(stderr, "bad color option %s\n", optarg);
				display_usage(stderr, argv[0]);
			}
			break;
		case 'D':
			cfg.flags &= ~FYPCF_DEBUG_DIAG_ALL;
			rc = apply_flags_option(optarg, &cfg.flags, modify_debug_diag_flags);
			if (rc) {
				fprintf(stderr, "bad diag option %s\n", optarg);
				display_usage(stderr, argv[0]);
			}
			break;
		case 'M':
			cfg.flags &= ~FYPCF_DEBUG_DEFAULT;
			rc = apply_flags_option(optarg, &cfg.flags, modify_module_flags);
			if (rc) {
				fprintf(stderr, "bad module option %s\n", optarg);
				display_usage(stderr, argv[0]);
			}
			break;
		case OPT_DISABLE_MMAP:
			cfg.flags |= FYPCF_DISABLE_MMAP_OPT;
			break;
		case 'q':
			cfg.flags |= FYPCF_QUIET;
			break;
		case 'h' :
		default:
			if (opt != 'h')
				fprintf(stderr, "Unknown option\n");
			display_usage(opt == 'h' ? stdout : stderr, argv[0]);
			return EXIT_SUCCESS;
		}
	}

	/* check mode */
	if (strcmp(mode, "parse") &&
	    strcmp(mode, "scan") &&
	    strcmp(mode, "copy") &&
	    strcmp(mode, "testsuite") &&
	    strcmp(mode, "dump") &&
	    strcmp(mode, "build")
#if defined(HAVE_LIBYAML) && HAVE_LIBYAML
	    && strcmp(mode, "libyaml-scan")
	    && strcmp(mode, "libyaml-parse")
	    && strcmp(mode, "libyaml-testsuite")
	    && strcmp(mode, "libyaml-dump")
#endif

	    ) {
		fprintf(stderr, "Unknown mode %s\n", mode);
		display_usage(opt == 'h' ? stdout : stderr, argv[0]);
	}

	/* libyaml options are first */
#if defined(HAVE_LIBYAML) && HAVE_LIBYAML
	if (!strcmp(mode, "libyaml-scan") ||
	    !strcmp(mode, "libyaml-parse") ||
	    !strcmp(mode, "libyaml-testsuite") ||
	    !strcmp(mode, "libyaml-dump")) {
		FILE *fp;
		yaml_parser_t parser;
        	yaml_emitter_t emitter;

		if (optind >= argc) {
			fprintf(stderr, "Missing file argument\n");
			goto cleanup;
		}

		fp = fopen(argv[optind], "rb");
		if (!fp) {
			fprintf(stderr, "Failed to open file %s\n", argv[optind]);
			goto cleanup;
		}

		rc = yaml_parser_initialize(&parser);
		assert(rc);
		rc = yaml_emitter_initialize(&emitter);
		assert(rc);

		yaml_parser_set_input_file(&parser, fp);
		yaml_emitter_set_output_file(&emitter, stdout);

		if (!strcmp(mode, "libyaml-scan")) {
			rc = do_libyaml_scan(&parser);
			if (rc < 0) {
				fprintf(stderr, "do_libyaml_scan() error %d\n", rc);
				fprintf(stderr, "  problem='%s' context='%s'\n", parser.problem, parser.context);
			}
		} else if (!strcmp(mode, "libyaml-parse")) {
			rc = do_libyaml_parse(&parser);
			if (rc < 0) {
				fprintf(stderr, "do_libyaml_parse() error %d\n", rc);
				fprintf(stderr, "  problem='%s' context='%s'\n", parser.problem, parser.context);
			}
		} else if (!strcmp(mode, "libyaml-testsuite")) {
			rc = do_libyaml_testsuite(&parser);
			if (rc < 0) {
				fprintf(stderr, "do_libyaml_testsuite() error %d\n", rc);
				fprintf(stderr, "  problem='%s' context='%s'\n", parser.problem, parser.context);
			}
		} else if (!strcmp(mode, "libyaml-dump")) {
			rc = do_libyaml_dump(&parser, &emitter);
			if (rc < 0) {
				fprintf(stderr, "do_libyaml_dump() error %d\n", rc);
				if (parser.problem)
					fprintf(stderr, "  problem='%s' context='%s'\n", parser.problem, parser.context);
			}
		} else
			rc = -1;

		yaml_parser_delete(&parser);
		yaml_emitter_delete(&emitter);

		fclose(fp);

		return !rc ? EXIT_SUCCESS : EXIT_FAILURE;
	}
#endif

	/* set default parser configuration for diagnostics without a parser */
	fy_set_default_parser_cfg_flags(cfg.flags);

	if (!strcmp(mode, "build")) {
		rc = do_build(&cfg, argc - optind, argv + optind);
		return !rc ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	rc = fy_parse_setup(fyp, &cfg);
	if (rc) {
		fprintf(stderr, "fy_parse_setup() failed\n");
		goto cleanup;
	}

	icount = argc - optind;
	if (!icount)
		icount++;

	fyic_array = alloca(sizeof(*fyic_array) * icount);
	memset(fyic_array, 0, sizeof(*fyic_array) * icount);

	j = 0;
	for (i = optind; i < argc; i++) {
		fyic = &fyic_array[i - optind];
		if (!strcmp(argv[i], "-")) {
			fyic->type = fyit_stream;
			fyic->stream.name = "stdin";
			fyic->stream.fp = stdin;
			fyic->stream.chunk = chunk;
		} else {
			fyic->type = fyit_file;
			fyic->file.filename = argv[i];
		}

		rc = fy_parse_input_append(fyp, fyic);
		if (rc) {
			fprintf(stderr, "fy_input_append() failed\n");
			goto cleanup;
		}
		j++;
	}

	if (!j) {
		fyic = &fyic_array[0];

		fyic->type = fyit_stream;
		fyic->stream.name = "stdin";
		fyic->stream.fp = stdin;
		fyic->stream.chunk = chunk;

		rc = fy_parse_input_append(fyp, fyic);
		if (rc) {
			fprintf(stderr, "fy_input_append() failed\n");
			goto cleanup;
		}
	}

	if (!strcmp(mode, "parse")) {
		rc = do_parse(fyp);
		if (rc < 0) {
			/* fprintf(stderr, "do_parse() error %d\n", rc); */
			goto cleanup;
		}
	} else if (!strcmp(mode, "scan")) {
		rc = do_scan(fyp);
		if (rc < 0) {
			fprintf(stderr, "do_scan() error %d\n", rc);
			goto cleanup;
		}
	} else if (!strcmp(mode, "copy")) {
		rc = do_copy(fyp);
		if (rc < 0) {
			fprintf(stderr, "do_copy() error %d\n", rc);
			goto cleanup;
		}
	} else if (!strcmp(mode, "testsuite")) {
		rc = do_testsuite(fyp);
		if (rc < 0) {
			/* fprintf(stderr, "do_testsuite() error %d\n", rc); */
			goto cleanup;
		}
	} else if (!strcmp(mode, "dump")) {
		rc = do_dump(fyp, indent, width, resolve, sort);
		if (rc < 0) {
			/* fprintf(stderr, "do_dump() error %d\n", rc); */
			goto cleanup;
		}
	}

	exitcode = EXIT_SUCCESS;

cleanup:
	fy_parse_cleanup(&ctx);

	return exitcode;
}
