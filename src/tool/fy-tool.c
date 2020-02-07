/*
 * fy-tool.c - libfyaml YAML manipulation/dumping utility
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
#include <ctype.h>
#include <unistd.h>

#include <libfyaml.h>

#include "fy-valgrind.h"

#define QUIET_DEFAULT			false
#define INCLUDE_DEFAULT			""
#define DEBUG_LEVEL_DEFAULT		3
#define COLOR_DEFAULT			"auto"
#define INDENT_DEFAULT			2
#define WIDTH_DEFAULT			80
#define RESOLVE_DEFAULT			false
#define SORT_DEFAULT			false
#define COMMENT_DEFAULT			false
#define VISIBLE_DEFAULT			false
#define MODE_DEFAULT			"original"
#define TO_DEFAULT			"/"
#define FROM_DEFAULT			"/"
#define TRIM_DEFAULT			"/"
#define FOLLOW_DEFAULT			false
#define STRIP_LABELS_DEFAULT		false
#define STRIP_TAGS_DEFAULT		false
#define STRIP_DOC_DEFAULT		false
#define STREAMING_DEFAULT		false
#define TAB_DEFAULT			"auto"
#define JSON_DEFAULT			"auto"
#define DISABLE_ACCEL_DEFAULT		false

#define OPT_DUMP			1000
#define OPT_TESTSUITE			1001
#define OPT_FILTER			1002
#define OPT_JOIN			1003
#define OPT_TOOL			1004

#define OPT_STRIP_LABELS		2000
#define OPT_STRIP_TAGS			2001
#define OPT_STRIP_DOC			2002
#define OPT_STREAMING			2003
#define OPT_TAB				2004
#define OPT_DISABLE_ACCEL		2005

static struct option lopts[] = {
	{"include",		required_argument,	0,	'I' },
	{"debug-level",		required_argument,	0,	'd' },
	{"indent",		required_argument,	0,	'i' },
	{"width",		required_argument,	0,	'w' },
	{"resolve",		no_argument,		0,	'r' },
	{"sort",		no_argument,		0,	's' },
	{"comment",		no_argument,		0,	'c' },
	{"color",		required_argument,	0,	'C' },
	{"visible",		no_argument,		0,	'V' },
	{"mode",		required_argument,	0,	'm' },
	{"tab",			required_argument,	0,	OPT_TAB },
	{"json",		required_argument,	0,	'j' },
	{"file",		required_argument,	0,	'f' },
	{"trim",		required_argument,	0,	't' },
	{"follow",		no_argument,		0,	'l' },
	{"dump",		no_argument,		0,	OPT_DUMP },
	{"testsuite",		no_argument,		0,	OPT_TESTSUITE },
	{"filter",		no_argument,		0,	OPT_FILTER },
	{"join",		no_argument,		0,	OPT_JOIN },
	{"strip-labels",	no_argument,		0,	OPT_STRIP_LABELS },
	{"strip-tags",		no_argument,		0,	OPT_STRIP_TAGS },
	{"strip-doc",		no_argument,		0,	OPT_STRIP_DOC },
	{"streaming",		no_argument,		0,	OPT_STREAMING },
	{"disable-accel",	no_argument,		0,	OPT_DISABLE_ACCEL },
	{"to",			required_argument,	0,	'T' },
	{"from",		required_argument,	0,	'F' },
	{"quiet",		no_argument,		0,	'q' },
	{"help",		no_argument,		0,	'h' },
	{"version",		no_argument,		0,	'v' },
	{0,			0,              	0,	 0  },
};

static void display_usage(FILE *fp, char *progname, int tool_mode)
{
	fprintf(fp, "Usage: %s [options] [args]\n", progname);
	fprintf(fp, "\nOptions:\n\n");
	fprintf(fp, "\t--include, -I <path>     : Add directory to include path "
						"(default path \"%s\")\n",
						INCLUDE_DEFAULT);
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
	fprintf(fp, "\t--color, -C <mode>       : Color output can be one of on, off, auto"
						" (default %s)\n",
						COLOR_DEFAULT);
	fprintf(fp, "\t--visible, -V            : Make all whitespace and linebreaks visible"
						" (default %s)\n",
						VISIBLE_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--follow, -l             : Follow aliases when using paths"
						" (default %s)\n",
						FOLLOW_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--strip-labels           : Strip labels when emitting"
						" (default %s)\n",
						STRIP_LABELS_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--strip-tags             : Strip tags when emitting"
						" (default %s)\n",
						STRIP_TAGS_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--strip-doc              : Strip document headers and indicators when emitting"
						" (default %s)\n",
						STRIP_DOC_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--disable-accel          : Disable access accelerators (slower but uses less memory)"
						" (default %s)\n",
						DISABLE_ACCEL_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--tab                    : (Very experimental) tab for indent option\n"
		    "\t                           Allowed values none, auto, [1-9] (default %s)\n",
						TAB_DEFAULT);
	fprintf(fp, "\t--json, -j               : JSON input mode (no | force | auto)"
						" (default %s)\n",
						JSON_DEFAULT);
	fprintf(fp, "\t--quiet, -q              : Quiet operation, do not "
						"output messages (default %s)\n",
						QUIET_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--version, -v            : Display libfyaml version\n");
	fprintf(fp, "\t--help, -h               : Display  help message\n");

	if (tool_mode == OPT_TOOL || tool_mode != OPT_TESTSUITE) {
		fprintf(fp, "\t--sort, -s               : Perform mapping key sort (valid for dump)"
							" (default %s)\n",
							SORT_DEFAULT ? "true" : "false");
		fprintf(fp, "\t--comment, -c            : Output comments (experimental)"
							" (default %s)\n",
							COMMENT_DEFAULT ? "true" : "false");
		fprintf(fp, "\t--mode, -m <mode>        : Output mode can be one of original, block, flow, flow-oneline, json, json-tp, json-oneline, dejson"
							" (default %s)\n",
							MODE_DEFAULT);
		if (tool_mode == OPT_TOOL || tool_mode == OPT_DUMP)
			fprintf(fp, "\t--streaming              : Use streaming output mode"
								" (default %s)\n",
								STREAMING_DEFAULT ? "true" : "false");
	}

	if (tool_mode == OPT_TOOL || (tool_mode != OPT_DUMP && tool_mode != OPT_TESTSUITE)) {
		fprintf(fp, "\t--file, -f <file>        : Use given file instead of <stdin>\n"
			    "\t                           Note that using a string with a leading '>' is equivalent to a file with the trailing content\n"
			    "\t                           --file \">foo: bar\" is as --file file.yaml with file.yaml \"foo: bar\"\n");
	}

	if (tool_mode == OPT_TOOL || tool_mode == OPT_JOIN) {
		fprintf(fp, "\t--to, -T <path>          : Join to <path> (default %s)\n",
							TO_DEFAULT);
		fprintf(fp, "\t--from, -F <path>        : Join from <path> (default %s)\n",
							FROM_DEFAULT);
		fprintf(fp, "\t--trim, -t <path>        : Output given path (default %s)\n",
							TRIM_DEFAULT);
	}

	if (tool_mode == OPT_TOOL) {
		fprintf(fp, "\t--dump                   : Dump mode, [arguments] are file names\n");
		fprintf(fp, "\t--testsuite              : Testsuite mode, [arguments] are <file>s to output parse events\n");
		fprintf(fp, "\t--filter                 : Filter mode, <stdin> is input, [arguments] are <path>s, outputs to stdout\n");
		fprintf(fp, "\t--join                   : Join mode, [arguments] are <path>s, outputs to stdout\n");
	}

	fprintf(fp, "\n");

	switch (tool_mode) {
	case OPT_TOOL:
	default:
		break;
	case OPT_TESTSUITE:
		fprintf(fp, "\tParse and dump test-suite event format\n");
		fprintf(fp, "\t$ %s input.yaml\n\t...\n", progname);
		fprintf(fp, "\n");
		fprintf(fp, "\tParse and dump of event example\n");
		fprintf(fp, "\t$ echo \"foo: bar\" | %s -\n", progname);
		fprintf(fp, "\t+STR\n\t+DOC\n\t+MAP\n\t=VAL :foo\n\t=VAL :bar\n\t-MAP\n\t-DOC\n\t-STR\n");
		break;
	case OPT_DUMP:
		fprintf(fp, "\tParse and dump generated YAML document tree in the original YAML form\n");
		fprintf(fp, "\t$ %s input.yaml\n\t...\n", progname);
		fprintf(fp, "\n");
		fprintf(fp, "\tParse and dump generated YAML document tree in block YAML form (and make whitespace visible)\n");
		fprintf(fp, "\t$ %s -V -mblock input.yaml\n\t...\n", progname);
		fprintf(fp, "\n");
		fprintf(fp, "\tParse and dump generated YAML document from the input string\n");
		fprintf(fp, "\t$ %s -mjson \">foo: bar\"\n", progname);
		fprintf(fp, "\t{\n\t  \"foo\": \"bar\"\n\t}\n");
		break;
	case OPT_FILTER:
		fprintf(fp, "\tParse and filter YAML document tree starting from the '/foo' path followed by the '/bar' path\n");
		fprintf(fp, "\t$ %s --file input.yaml /foo /bar\n\t...\n", progname);
		fprintf(fp, "\n");
		fprintf(fp, "\tParse and filter for two paths (note how a multi-document stream is produced)\n");
		fprintf(fp, "\t$ %s --file -mblock --filter --file \">{ foo: bar, baz: [ frooz, whee ] }\" /foo /baz\n", progname);
		fprintf(fp, "\tbar\n\t---\n\t- frooz\n\t- whee\n");
		fprintf(fp, "\n");
		fprintf(fp, "\tParse and filter YAML document in stdin (note how the key may be complex)\n");
		fprintf(fp, "\t$ echo \"{ foo: bar }: baz\" | %s \"/{foo: bar}/\"\n", progname);
		fprintf(fp, "\tbaz\n");
		break;
	case OPT_JOIN:
		fprintf(fp, "\tParse and join two YAML files\n");
		fprintf(fp, "\t$ %s file1.yaml file2.yaml\n\t...\n", progname);
		fprintf(fp, "\n");
		fprintf(fp, "\tParse and join two YAML maps\n");
		fprintf(fp, "\t$ %s \">foo: bar\" \">baz: frooz\"\n", progname);
		fprintf(fp, "\tfoo: bar\n\tbaz: frooz\n");
		fprintf(fp, "\n");
		fprintf(fp, "\tParse and join two YAML sequences\n");
		fprintf(fp, "\t$ %s -mblock \">[ foo ]\" \">[ bar ]\"\n", progname);
		fprintf(fp, "\t- foo\n\t- bar\n");
		fprintf(fp, "\n");
		break;
	}
}

static int modify_debug_level_flags(const char *what, unsigned int *flagsp)
{
	static const struct {
		const char *name;
		unsigned int set;
		unsigned int clr;
	} lf[] = {
		{ .name = "default",	.set = FYPCF_DEBUG_LEVEL(DEBUG_LEVEL_DEFAULT), .clr = FYPCF_DEBUG_LEVEL(DEBUG_LEVEL_DEFAULT) },
		{ .name = "debug",	.set = FYPCF_DEBUG_LEVEL_DEBUG, .clr = FYPCF_DEBUG_LEVEL_DEBUG },
		{ .name = "info",	.set = FYPCF_DEBUG_LEVEL_INFO, .clr = FYPCF_DEBUG_LEVEL_INFO },
		{ .name = "notice",	.set = FYPCF_DEBUG_LEVEL_NOTICE, .clr = FYPCF_DEBUG_LEVEL_NOTICE },
		{ .name = "warning",	.set = FYPCF_DEBUG_LEVEL_WARNING, .clr = FYPCF_DEBUG_LEVEL_WARNING },
		{ .name = "error",	.set = FYPCF_DEBUG_LEVEL_ERROR, .clr = FYPCF_DEBUG_LEVEL_ERROR },
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

static int apply_mode_flags(const char *what, enum fy_emitter_cfg_flags *flagsp)
{
	static const struct {
		const char *name;
		unsigned int value;
	} mf[] = {
		{ .name = "original",		.value = FYECF_MODE_ORIGINAL },
		{ .name = "block",		.value = FYECF_MODE_BLOCK },
		{ .name = "flow",		.value = FYECF_MODE_FLOW },
		{ .name = "flow-oneline",	.value = FYECF_MODE_FLOW_ONELINE },
		{ .name = "json",		.value = FYECF_MODE_JSON },
		{ .name = "json-tp",		.value = FYECF_MODE_JSON_TP },
		{ .name = "json-oneline",	.value = FYECF_MODE_JSON_ONELINE },
		{ .name = "dejson",		.value = FYECF_MODE_DEJSON },
	};
	unsigned int i;

	if (!what || !flagsp)
		return -1;

	if (!strcmp(what, "default"))
		what = MODE_DEFAULT;

	for (i = 0; i < sizeof(mf)/sizeof(mf[0]); i++) {
		if (!strcmp(what, mf[i].name)) {
			*flagsp &= ~FYECF_MODE(FYECF_MODE_MASK);
			*flagsp |=  mf[i].value;
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

struct dump_userdata {
	FILE *fp;
	bool colorize;
	bool visible;
};

static inline int
utf8_width_by_first_octet(uint8_t c)
{
	return (c & 0x80) == 0x00 ? 1 :
	       (c & 0xe0) == 0xc0 ? 2 :
	       (c & 0xf0) == 0xe0 ? 3 :
	       (c & 0xf8) == 0xf0 ? 4 : 0;
}

static int do_output(struct fy_emitter *fye, enum fy_emitter_write_type type, const char *str, int len, void *userdata)
{
	struct dump_userdata *du = userdata;
	FILE *fp = du->fp;
	int ret, w;
	const char *color = NULL;
	const char *s, *e;

	s = str;
	e = str + len;
	if (du->colorize) {
		switch (type) {
		case fyewt_document_indicator:
			color = "\x1b[36m";
			break;
		case fyewt_tag_directive:
		case fyewt_version_directive:
			color = "\x1b[33m";
			break;
		case fyewt_indent:
			if (du->visible) {
				fputs("\x1b[32m", fp);
				while (s < e && (w = utf8_width_by_first_octet(((uint8_t)*s))) > 0) {
					/* open box - U+2423 */
					fputs("\xe2\x90\xa3", fp);
					s += w;
				}
				fputs("\x1b[0m", fp);
				return len;
			}
			break;
		case fyewt_indicator:
			if (len == 1 && (str[0] == '\'' || str[0] == '"'))
				color = "\x1b[33m";
			else if (len == 1 && str[0] == '&')
				color = "\x1b[32;1m";
			else
				color = "\x1b[35m";
			break;
		case fyewt_whitespace:
			if (du->visible) {
				fputs("\x1b[32m", fp);
				while (s < e && (w = utf8_width_by_first_octet(((uint8_t)*s))) > 0) {
					/* symbol for space - U+2420 */
					/* symbol for interpunct - U+00B7 */
					fputs("\xc2\xb7", fp);
					s += w;
				}
				fputs("\x1b[0m", fp);
				return len;
			}
			break;
		case fyewt_plain_scalar:
			color = "\x1b[37;1m";
			break;
		case fyewt_single_quoted_scalar:
		case fyewt_double_quoted_scalar:
			color = "\x1b[33m";
			break;
		case fyewt_literal_scalar:
		case fyewt_folded_scalar:
			color = "\x1b[33m";
			break;
		case fyewt_anchor:
		case fyewt_tag:
		case fyewt_alias:
			color = "\x1b[32;1m";
			break;
		case fyewt_linebreak:
			if (du->visible) {
				fputs("\x1b[32m", fp);
				while (s < e && (w = utf8_width_by_first_octet(((uint8_t)*s))) > 0) {
					/* symbol for space - ^M */
					/* fprintf(fp, "^M\n"); */
					/* down arrow - U+2193 */
					fputs("\xe2\x86\x93\n", fp);
					s += w;
				}
				fputs("\x1b[0m", fp);
				return len;
			}
			color = NULL;
			break;
		case fyewt_terminating_zero:
			color = NULL;
			break;
		case fyewt_plain_scalar_key:
		case fyewt_single_quoted_scalar_key:
		case fyewt_double_quoted_scalar_key:
			color = "\x1b[36;1m";
			break;
		case fyewt_comment:
			color = "\x1b[34;1m";
			break;
		}
	}

	/* don't output the terminating zero */
	if (type == fyewt_terminating_zero)
		return len;

	if (color)
		fputs(color, fp);

	ret = fwrite(str, 1, len, fp);

	if (color)
		fputs("\x1b[0m", fp);

	return ret;
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

void dump_testsuite_event(struct fy_parser *fyp, struct fy_event *fye, bool colorize,
			  struct fy_token_iter *iter)
{
	const char *anchor = NULL;
	const char *tag = NULL;
	size_t anchor_len = 0, tag_len = 0;
	enum fy_scalar_style style;
	const struct fy_iter_chunk *ic;
	int ret;

	switch (fye->type) {
	case FYET_NONE:
		if (colorize)
			fputs("\x1b[31;1m", stdout);
		printf("???");
		break;
	case FYET_STREAM_START:
		if (colorize)
			fputs("\x1b[36m", stdout);
		printf("+STR");
		break;
	case FYET_STREAM_END:
		if (colorize)
			fputs("\x1b[36m", stdout);
		printf("-STR");
		break;
	case FYET_DOCUMENT_START:
		if (colorize)
			fputs("\x1b[36m", stdout);
		printf("+DOC%s", !fy_document_event_is_implicit(fye) ? " ---" : "");
		break;
	case FYET_DOCUMENT_END:
		if (colorize)
			fputs("\x1b[36m", stdout);
		printf("-DOC%s", !fy_document_event_is_implicit(fye) ? " ..." : "");
		break;
	case FYET_MAPPING_START:
		if (fye->mapping_start.anchor)
			anchor = fy_token_get_text(fye->mapping_start.anchor, &anchor_len);
		if (fye->mapping_start.tag)
			tag = fy_token_get_text(fye->mapping_start.tag, &tag_len);
		if (colorize)
			fputs("\x1b[36;1m", stdout);
		printf("+MAP");
		if (anchor) {
			if (colorize)
				fputs("\x1b[32m", stdout);
			printf(" &%.*s", (int)anchor_len, anchor);
		}
		if (tag) {
			if (colorize)
				fputs("\x1b[32m", stdout);
			printf(" <%.*s>", (int)tag_len, tag);
		}
		break;
	case FYET_MAPPING_END:
		if (colorize)
			fputs("\x1b[36;1m", stdout);
		printf("-MAP");
		break;
	case FYET_SEQUENCE_START:
		if (fye->sequence_start.anchor)
			anchor = fy_token_get_text(fye->sequence_start.anchor, &anchor_len);
		if (fye->sequence_start.tag)
			tag = fy_token_get_text(fye->sequence_start.tag, &tag_len);
		if (colorize)
			fputs("\x1b[33;1m", stdout);
		printf("+SEQ");
		if (anchor) {
			if (colorize)
				fputs("\x1b[32m", stdout);
			printf(" &%.*s", (int)anchor_len, anchor);
		}
		if (tag) {
			if (colorize)
				fputs("\x1b[32m", stdout);
			printf(" <%.*s>", (int)tag_len, tag);
		}
		break;
	case FYET_SEQUENCE_END:
		if (colorize)
			fputs("\x1b[33;1m", stdout);
		printf("-SEQ");
		break;
	case FYET_SCALAR:
		if (fye->scalar.anchor)
			anchor = fy_token_get_text(fye->scalar.anchor, &anchor_len);
		if (fye->scalar.tag)
			tag = fy_token_get_text(fye->scalar.tag, &tag_len);

		if (colorize)
			fputs("\x1b[37;1m", stdout);
		printf("=VAL");
		if (anchor) {
			if (colorize)
				fputs("\x1b[32m", stdout);
			printf(" &%.*s", (int)anchor_len, anchor);
		}
		if (tag) {
			if (colorize)
				fputs("\x1b[32m", stdout);
			printf(" <%.*s>", (int)tag_len, tag);
		}

		style = fy_token_scalar_style(fye->scalar.value);
		switch (style) {
		case FYSS_PLAIN:
			if (colorize)
				fputs("\x1b[37;1m", stdout);
			printf(" :");
			break;
		case FYSS_SINGLE_QUOTED:
			if (colorize)
				fputs("\x1b[33m", stdout);
			printf(" '");
			break;
		case FYSS_DOUBLE_QUOTED:
			if (colorize)
				fputs("\x1b[33m", stdout);
			printf(" \"");
			break;
		case FYSS_LITERAL:
			if (colorize)
				fputs("\x1b[33m", stdout);
			printf(" |");
			break;
		case FYSS_FOLDED:
			if (colorize)
				fputs("\x1b[33m", stdout);
			printf(" >");
			break;
		default:
			abort();
		}

		fy_token_iter_start(fye->scalar.value, iter);
		ic = NULL;
		while ((ic = fy_token_iter_chunk_next(iter, ic, &ret)) != NULL)
			print_escaped(ic->str, ic->len);
		fy_token_iter_finish(iter);

		break;
	case FYET_ALIAS:
		anchor = fy_token_get_text(fye->alias.anchor, &anchor_len);
		if (colorize)
			fputs("\x1b[32m", stdout);
		printf("=ALI *%.*s", (int)anchor_len, anchor);
		break;
	default:
		assert(0);
	}
	if (colorize)
		fputs("\x1b[0m", stdout);
	fputs("\n", stdout);
}

static int set_parser_input(struct fy_parser *fyp, const char *what,
		bool default_string)
{
	int rc;

	if (!strcmp(what, "-"))
		rc = fy_parser_set_input_fp(fyp, "stdin", stdin);
	else if (*what == '<')
		rc = fy_parser_set_input_file(fyp, what + 1);
	else if (*what == '>')
		rc = fy_parser_set_string(fyp, what + 1, FY_NT);
	else
		rc = fy_parser_set_input_file(fyp, what);

	return rc;
}

int main(int argc, char *argv[])
{
	struct fy_parse_cfg cfg = {
		.search_path = INCLUDE_DEFAULT,
		.flags =
			(QUIET_DEFAULT ? FYPCF_QUIET : 0) |
			FYPCF_DEBUG_LEVEL(DEBUG_LEVEL_DEFAULT) |
			FYPCF_DEBUG_DIAG_DEFAULT | FYPCF_DEBUG_DEFAULT |
			(RESOLVE_DEFAULT ? FYPCF_RESOLVE_DOCUMENT : 0) |
			(DISABLE_ACCEL_DEFAULT ? FYPCF_DISABLE_ACCELERATORS : 0),
	};
	struct fy_emitter_cfg emit_cfg;
	struct fy_parser *fyp = NULL;
	struct fy_emitter *fye = NULL;
	int rc, exitcode = EXIT_FAILURE, opt, lidx, count, i, j, step = 1;
	int indent = INDENT_DEFAULT;
	int width = WIDTH_DEFAULT;
	bool visible = VISIBLE_DEFAULT;
	bool follow = FOLLOW_DEFAULT;
	const char *to = TO_DEFAULT;
	const char *from = FROM_DEFAULT;
	const char *color = COLOR_DEFAULT;
	const char *file = NULL, *trim = TRIM_DEFAULT;
	char *tmp, *s, *progname;
	struct fy_document *fyd, *fyd_join = NULL;
	struct dump_userdata du;
	enum fy_emitter_cfg_flags emit_flags = 0;
	struct fy_node *fyn, *fyn_emit, *fyn_to, *fyn_from;
	int count_ins = 0;
	struct fy_document **fyd_ins = NULL;
	int tool_mode = OPT_TOOL;
	struct fy_event *fyev;
	bool join_resolve = RESOLVE_DEFAULT;
	struct fy_token_iter *iter;
	bool streaming = STREAMING_DEFAULT;

	fy_valgrind_check(&argc, &argv);

	/* select the appropriate tool mode */
	progname = argv[0];
	progname = strrchr(argv[0], '/');
	if (!progname)
		progname = argv[0];
	else
		progname++;

	/* default mode is dump */
	if (!strcmp(progname, "fy-filter"))
		tool_mode = OPT_FILTER;
	else if (!strcmp(progname, "fy-testsuite"))
		tool_mode = OPT_TESTSUITE;
	else if (!strcmp(progname, "fy-dump"))
		tool_mode = OPT_DUMP;
	else if (!strcmp(progname, "fy-join"))
		tool_mode = OPT_JOIN;
	else
		tool_mode = OPT_TOOL;

	emit_flags = (SORT_DEFAULT ? FYECF_SORT_KEYS : 0) |
		     (COMMENT_DEFAULT ? FYECF_OUTPUT_COMMENTS : 0) |
		     (STRIP_LABELS_DEFAULT ? FYECF_STRIP_LABELS : 0) |
		     (STRIP_TAGS_DEFAULT ? FYECF_STRIP_TAGS : 0) |
		     (STRIP_DOC_DEFAULT ? FYECF_STRIP_DOC : 0);
	apply_mode_flags(MODE_DEFAULT, &emit_flags);

	if (!strcmp(TAB_DEFAULT, "none"))
		cfg.flags |= FYPCF_TAB_NONE;
	else if (!strcmp(TAB_DEFAULT, "auto"))
		cfg.flags |= FYPCF_TAB_AUTO;
	else if (strlen(TAB_DEFAULT) == 1 && TAB_DEFAULT[0] > '1' && TAB_DEFAULT[1] <= '9')
		cfg.flags |= FYPCF_TAB(TAB_DEFAULT[0] - '0');

	while ((opt = getopt_long_only(argc, argv,
					"I:" "d:" "i:" "w:" "rsc" "C:" "m:" "V" "f:" "t:" "T:F:" "j:" "qhvl",
					lopts, &lidx)) != -1) {
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
		case 'i':
			indent = atoi(optarg);
			if (indent < 0 || indent > FYECF_INDENT_MASK) {
				fprintf(stderr, "bad indent option %s\n", optarg);
				display_usage(stderr, progname, tool_mode);
				return EXIT_FAILURE;
			}

			break;
		case 'w':
			width = atoi(optarg);
			if (width < 0 || width > FYECF_WIDTH_MASK) {
				fprintf(stderr, "bad width option %s\n", optarg);
				display_usage(stderr, progname, tool_mode);
				return EXIT_FAILURE;
			}
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
				display_usage(stderr, progname, tool_mode);
				return EXIT_FAILURE;
			}

			break;
		case 'r':
			cfg.flags |= FYPCF_RESOLVE_DOCUMENT;
			break;
		case 's':
			emit_flags |= FYECF_SORT_KEYS;
			break;
		case 'c':
			cfg.flags |= FYPCF_PARSE_COMMENTS;
			emit_flags |= FYECF_OUTPUT_COMMENTS;
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
				display_usage(stderr, progname, tool_mode);
				return EXIT_FAILURE;
			}
			break;
		case 'm':
			rc = apply_mode_flags(optarg, &emit_flags);
			if (rc) {
				fprintf(stderr, "bad mode option %s\n", optarg);
				display_usage(stderr, progname, tool_mode);
				return EXIT_FAILURE;
			}
			break;
		case 'V':
			visible = true;
			break;
		case 'l':
			follow = true;
			break;
		case 'q':
			cfg.flags |= FYPCF_QUIET;
			break;
		case 'f':
			file = optarg;
			break;
		case 't':
			trim = optarg;
			break;
		case 'T':
			to = optarg;
			break;
		case 'F':
			from = optarg;
			break;
		case OPT_TESTSUITE:
		case OPT_FILTER:
		case OPT_DUMP:
		case OPT_JOIN:
		case OPT_TOOL:
			tool_mode = opt;
			break;
		case OPT_STRIP_LABELS:
			emit_flags |= FYECF_STRIP_LABELS;
			break;
		case OPT_STRIP_TAGS:
			emit_flags |= FYECF_STRIP_TAGS;
			break;
		case OPT_STRIP_DOC:
			emit_flags |= FYECF_STRIP_DOC;
			break;
		case OPT_STREAMING:
			streaming = true;
			break;
		case OPT_TAB:
			cfg.flags &= ~(FYPCF_TAB_MASK << FYPCF_TAB_SHIFT);
			if (!strcmp(optarg, "none"))
				cfg.flags |= FYPCF_TAB_NONE;
			else if (!strcmp(optarg, "auto"))
				cfg.flags |= FYPCF_TAB_AUTO;
			else if (strlen(optarg) == 1 && optarg[0] > '1' && optarg[1] <= '9')
				cfg.flags |= FYPCF_TAB(optarg[0] - '0');
			else {
				fprintf(stderr, "bad tab option %s\n", optarg);
				display_usage(stderr, progname, tool_mode);
				return EXIT_FAILURE;
			}
			break;
		case 'j':
			cfg.flags &= ~(FYPCF_JSON_MASK << FYPCF_JSON_SHIFT);
			if (!strcmp(optarg, "no"))
				cfg.flags |= FYPCF_JSON_NONE;
			else if (!strcmp(optarg, "auto"))
				cfg.flags |= FYPCF_JSON_AUTO;
			else if (!strcmp(optarg, "force"))
				cfg.flags |= FYPCF_JSON_FORCE;
			else {
				fprintf(stderr, "bad json option %s\n", optarg);
				display_usage(stderr, progname, tool_mode);
				return EXIT_FAILURE;
			}
			break;
		case OPT_DISABLE_ACCEL:
			cfg.flags |= FYPCF_DISABLE_ACCELERATORS;
			break;
		case 'h' :
		default:
			if (opt != 'h')
				fprintf(stderr, "Unknown option '%c' %d\n", opt, opt);
			display_usage(opt == 'h' ? stdout : stderr, progname, tool_mode);
			return opt == 'h' ? EXIT_SUCCESS : EXIT_FAILURE;
		case 'v':
			printf("%s\n", fy_library_version());
			return EXIT_SUCCESS;
		}
	}

	/* if we're still in tool mode, switch to dump */
	if (tool_mode == OPT_TOOL)
		tool_mode = OPT_DUMP;

	/* as a special case for join, we resolve the document once */
	if (tool_mode == OPT_JOIN) {
		join_resolve = !!(cfg.flags & FYPCF_RESOLVE_DOCUMENT);
		cfg.flags &= ~FYPCF_RESOLVE_DOCUMENT;
	}

	/* set default parser configuration for diagnostics without a parser */
	fy_set_default_parser_cfg_flags(cfg.flags);

	fyp = fy_parser_create(&cfg);
	if (!fyp) {
		fprintf(stderr, "fy_parser_create() failed\n");
		goto cleanup;
	}

	exitcode = EXIT_FAILURE;

	memset(&du, 0, sizeof(du));
	du.fp = stdout;
	du.colorize = false;
	if ((cfg.flags & FYPCF_COLOR(FYPCF_COLOR_MASK)) == FYPCF_COLOR_AUTO)
		du.colorize = isatty(fileno(du.fp));
	else if ((cfg.flags & FYPCF_COLOR(FYPCF_COLOR_MASK)) == FYPCF_COLOR_FORCE)
		du.colorize = true;
	else if ((cfg.flags & FYPCF_COLOR(FYPCF_COLOR_MASK)) == FYPCF_COLOR_NONE)
		du.colorize = false;
	du.visible = visible;

	if (tool_mode != OPT_TESTSUITE) {

		memset(&emit_cfg, 0, sizeof(emit_cfg));
		emit_cfg.flags = emit_flags |
				FYECF_INDENT(indent) | FYECF_WIDTH(width);

		emit_cfg.output = do_output;
		emit_cfg.userdata = &du;

		fye = fy_emitter_create(&emit_cfg);
		if (!fye)
			goto cleanup;

	}

	switch (tool_mode) {

	case OPT_TESTSUITE:
		if (optind >= argc || !strcmp(argv[optind], "-"))
			rc = fy_parser_set_input_fp(fyp, "stdin", stdin);
		else
			rc = fy_parser_set_input_file(fyp, argv[optind]);
		if (rc) {
			fprintf(stderr, "failed to set testsuite input\n");
			goto cleanup;
		}

		iter = fy_token_iter_create(NULL);
		if (!iter) {
			fprintf(stderr, "failed to create token iterator\n");
			goto cleanup;
		}
		while ((fyev = fy_parser_parse(fyp)) != NULL) {
			dump_testsuite_event(fyp, fyev, du.colorize, iter);
			fy_parser_event_free(fyp, fyev);
		}
		fy_token_iter_destroy(iter);
		iter = NULL;
		if (fy_parser_get_stream_error(fyp))
			goto cleanup;
		break;

	case OPT_DUMP:
		if (optind >= argc) {
			fprintf(stderr, "missing yaml file to dump\n");
			goto cleanup;
		}

		count = 0;
		for (i = optind; i < argc; i++) {
			rc = set_parser_input(fyp, argv[i], false);
			if (rc) {
				fprintf(stderr, "failed to set parser input to '%s' for dump\n", argv[i]);
				goto cleanup;
			}

			if (!streaming) {
				while ((fyd = fy_parse_load_document(fyp)) != NULL) {

					rc = fy_emit_document(fye, fyd);
					fy_parse_document_destroy(fyp, fyd);
					if (rc)
						goto cleanup;

					count++;
				}
			} else {
				while ((fyev = fy_parser_parse(fyp)) != NULL) {
					rc = fy_emit_event(fye, fyev);
					if (rc)
						goto cleanup;
				}
				count++;
			}

			if (fy_parser_get_stream_error(fyp))
				goto cleanup;
		}
		break;

	case OPT_FILTER:
		step = 1;
		if (optind >= argc || (argc - optind) % step) {
			fprintf(stderr, "illegal arguments\n");
			goto cleanup;
		}

		if (!file)
			rc = fy_parser_set_input_fp(fyp, "stdin", stdin);
		else
			rc = set_parser_input(fyp, file, false);

		if (rc) {
			fprintf(stderr, "failed to set parser input to %s for filter\n",
					file ? file : "stdin");
			goto cleanup;
		}

		count = 0;
		while ((fyd = fy_parse_load_document(fyp)) != NULL) {

			for (i = optind, j = 0; i < argc; i += step, j++) {

				fyn = fy_node_by_path(fy_document_root(fyd), argv[i], FY_NT,
						      follow ? FYNWF_FOLLOW : FYNWF_DONT_FOLLOW);

				/* ignore not found paths */
				if (!fyn) {
					if (!(cfg.flags & FYPCF_QUIET))
						fprintf(stderr, "filter: could not find '%s'\n",
								argv[i]);
					continue;
				}

				fyn_emit = fyn;
				if (!fyn_emit)
					continue;

				rc = fy_emit_document_start(fye, fyd, fyn_emit);
				if (rc)
					goto cleanup;

				rc = fy_emit_root_node(fye, fyn_emit);
				if (rc)
					goto cleanup;

				rc = fy_emit_document_end(fye);
				if (rc)
					goto cleanup;

				count++;
			}

			fy_parse_document_destroy(fyp, fyd);
		}

		if (fy_parser_get_stream_error(fyp))
			goto cleanup;
		break;

	case OPT_JOIN:
		if (optind >= argc) {
			fprintf(stderr, "missing yaml file(s) to join\n");
			goto cleanup;
		}

		fyd_join = NULL;
		for (i = optind; i < argc; i++) {
			rc = set_parser_input(fyp, argv[i], false);
			if (rc) {
				fprintf(stderr, "failed to set parser input to '%s' for join\n", argv[i]);
				goto cleanup;
			}

			while ((fyd = fy_parse_load_document(fyp)) != NULL) {

				if (!fyd_join) {
					fyd_join = fyd;
					continue;
				}

				fyn_to = fy_node_by_path(fy_document_root(fyd_join), to, FY_NT,
							 follow ? FYNWF_FOLLOW : FYNWF_DONT_FOLLOW);
				if (!fyn_to) {
					fprintf(stderr, "unable to find to=%s\n", to);
					goto cleanup;
				}

				fyn_from = fy_node_by_path(fy_document_root(fyd), from, FY_NT,
							   follow ? FYNWF_FOLLOW : FYNWF_DONT_FOLLOW);

				if (!fyn_from) {
					fprintf(stderr, "unable to find from=%s\n", from);
					goto cleanup;
				}

				rc = fy_node_insert(fyn_to, fyn_from);
				if (rc) {
					fprintf(stderr, "fy_node_insert() failed\n");
					goto cleanup;
				}

				fy_document_destroy(fyd);
			}

			if (fy_parser_get_stream_error(fyp))
				goto cleanup;
		}

		/* perform the resolution at the end */
		if (join_resolve) {
			rc = fy_document_resolve(fyd_join);
			if (rc)
				goto cleanup;
		}

		fyn_emit = fy_node_by_path(fy_document_root(fyd_join), trim, FY_NT,
					   follow ? FYNWF_FOLLOW : FYNWF_DONT_FOLLOW);

		/* nothing to output ? */
		if (!fyn_emit) {
			if (!(cfg.flags & FYPCF_QUIET))
				fprintf(stderr, "warning: empty document\n");
		}

		rc = fy_emit_document_start(fye, fyd_join, fyn_emit);
		if (rc)
			goto cleanup;

		rc = fy_emit_root_node(fye, fyn_emit);
		if (rc)
			goto cleanup;

		rc = fy_emit_document_end(fye);
		if (rc)
			goto cleanup;

		break;
	}
	exitcode = EXIT_SUCCESS;

cleanup:
	if (fyd_join)
		fy_document_destroy(fyd_join);

	if (fyd_ins) {
		for (j = 0; j < count_ins; j++)
			fy_document_destroy(fyd_ins[j]);
	}

	if (fye)
		fy_emitter_destroy(fye);

	if (fyp)
		fy_parser_destroy(fyp);

	return exitcode;
}
