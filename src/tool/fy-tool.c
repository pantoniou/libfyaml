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
#include <ctype.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdalign.h>
#include <inttypes.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>

#include <libfyaml.h>
#ifdef HAVE_GENERIC
#include <libfyaml/libfyaml-generic.h>
#endif
#ifdef HAVE_REFLECTION
#include <libfyaml/libfyaml-reflection.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#include <regex.h>
#else
#include "fy-win32.h"
#endif

#include <getopt.h>

#include "fy-valgrind.h"
#include "fy-tool-util.h"

#ifdef _WIN32
#undef SORT_DEFAULT
#endif

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
#define STREAMING_DEFAULT		true
#define RECREATING_DEFAULT		false
#define JSON_DEFAULT			"auto"
#define DISABLE_ACCEL_DEFAULT		false
#define DISABLE_BUFFERING_DEFAULT	false
#define DISABLE_DEPTH_LIMIT_DEFAULT	false
#define SLOPPY_FLOW_INDENTATION_DEFAULT	false
#define PREFER_RECURSIVE_DEFAULT	false
#define YPATH_ALIASES_DEFAULT		false
#define DISABLE_FLOW_MARKERS_DEFAULT	false
#define DISABLE_DOC_MARKERS_DEFAULT	false
#define DISABLE_SCALAR_STYLES_DEFAULT	false
#define DUMP_PATH_DEFAULT		false
#define DOCUMENT_EVENT_STREAM_DEFAULT	false
#define COLLECT_ERRORS_DEFAULT		false
#define ALLOW_DUPLICATE_KEYS_DEFAULT	false
#define STRIP_EMPTY_KV_DEFAULT		false
#define TSV_FORMAT_DEFAULT		false
#define NO_ENDING_NEWLINE_DEFAULT	false
#define DEDUP_DEFAULT			false

#define OPT_DUMP			1000
#define OPT_TESTSUITE			1001
#define OPT_FILTER			1002
#define OPT_JOIN			1003
#define OPT_TOOL			1004
#define OPT_YPATH			1005
#define OPT_SCAN_DUMP			1006
#define OPT_PARSE_DUMP			1007
#define OPT_YAML_VERSION_DUMP		1008
#define OPT_COMPOSE			1009
#define OPT_B3SUM			1010
#ifdef HAVE_REFLECTION
#define OPT_REFLECT			1011
#endif

#define OPT_STRIP_LABELS		2000
#define OPT_STRIP_TAGS			2001
#define OPT_STRIP_DOC			2002
#define OPT_STREAMING			2003
#define OPT_RECREATING			2004
#define OPT_DISABLE_ACCEL		2005
#define OPT_DISABLE_BUFFERING		2006
#define OPT_DISABLE_DEPTH_LIMIT		2007
#define OPT_SLOPPY_FLOW_INDENTATION	2008
#define OPT_PREFER_RECURSIVE		2009
#define OPT_DUMP_PATHEXPR		2010
#define OPT_NOEXEC			2011
#define OPT_NULL_OUTPUT			2012
#define OPT_YPATH_ALIASES		2013
#define OPT_DISABLE_FLOW_MARKERS	2014
#define OPT_DUMP_PATH			2015
#define OPT_DOCUMENT_EVENT_STREAM	2016
#define OPT_COLLECT_ERRORS		2017
#define OPT_ALLOW_DUPLICATE_KEYS	2018
#define OPT_STRIP_EMPTY_KV		2019
#define OPT_DISABLE_MMAP		2020
#define OPT_TSV_FORMAT			2021
#define OPT_DISABLE_DOC_MARKERS		2022
#define OPT_DISABLE_SCALAR_STYLES	2023
#define OPT_NO_STREAMING		2024
#define OPT_NO_ENDING_NEWLINE		2025
#define OPT_PRESERVE_FLOW_LAYOUT	2026
#define OPT_INDENTED_SEQ_IN_MAP		2027
#ifdef HAVE_REFLECTION
#define OPT_CFLAGS			2028
#define OPT_GENERATE_C			2029
#define OPT_IMPORT_BLOB			2030
#define OPT_GENERATE_BLOB		2031
#define OPT_NO_PRUNE_SYSTEM		2032
#define OPT_TYPE_INCLUDE		2033
#define OPT_TYPE_EXCLUDE		2034
#define OPT_IMPORT_C_FILE		2035
#define OPT_ENTRY_TYPE			2036
#define OPT_ENTRY_META			2037
#define OPT_PACKED_ROUNDTRIP		2038
#define OPT_DRY_RUN			2039
#define OPT_DUMP_REFLECTION		2040
#define OPT_DEBUG_REFLECTION		2041
#define OPT_STRICT_ANNOTATIONS		2042
#endif

#define OPT_DISABLE_DIAG		3000
#define OPT_ENABLE_DIAG			3001
#define OPT_SHOW_DIAG			3002
#define OPT_HIDE_DIAG			3003

#define OPT_YAML_1_1			4000
#define OPT_YAML_1_2			4001
#define OPT_YAML_1_3			4002

/* b3sum options */
#define OPT_CHECK			5000
#define OPT_DERIVE_KEY			5001
#define OPT_NO_NAMES			5002
#define OPT_RAW				5003
#define OPT_KEYED			5005
#define OPT_LENGTH			5006
#define OPT_LIST_BACKENDS		5007
#define OPT_BACKEND			5008
#define OPT_NUM_THREADS			5009
#define OPT_FILE_BUFFER			5010
#define OPT_MMAP_MIN_CHUNK		5011
#define OPT_MMAP_MAX_CHUNK		5012

#ifdef HAVE_GENERIC
/* generic options */
#define OPT_GENERIC			6000
#define OPT_BUILDER_POLICY		6001
#define OPT_DEDUP			6002
#define OPT_NO_DEDUP			6003
#define OPT_GENERIC_TESTSUITE		6004
#define OPT_DUMP_PRIMITIVES		6005
#define OPT_GENERIC_PARSE_DUMP		6006
#define OPT_CREATE_MARKERS		6007
#define OPT_PYYAML_COMPAT		6008
#define OPT_KEEP_STYLE			6009
#endif

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
	{"json",		required_argument,	0,	'j' },
	{"file",		required_argument,	0,	'f' },
	{"trim",		required_argument,	0,	't' },
	{"follow",		no_argument,		0,	'l' },
	{"dump",		no_argument,		0,	OPT_DUMP },
	{"testsuite",		no_argument,		0,	OPT_TESTSUITE },
	{"filter",		no_argument,		0,	OPT_FILTER },
	{"join",		no_argument,		0,	OPT_JOIN },
	{"ypath",		no_argument,		0,	OPT_YPATH },
	{"scan-dump",		no_argument,		0,	OPT_SCAN_DUMP },
	{"parse-dump",		no_argument,		0,	OPT_PARSE_DUMP },
	{"compose",		no_argument,		0,	OPT_COMPOSE },
	{"dump-path",		no_argument,		0,	OPT_DUMP_PATH },
	{"yaml-version-dump",	no_argument,		0,	OPT_YAML_VERSION_DUMP },
	{"b3sum",		no_argument,		0,	OPT_B3SUM },
	{"strip-labels",	no_argument,		0,	OPT_STRIP_LABELS },
	{"strip-tags",		no_argument,		0,	OPT_STRIP_TAGS },
	{"strip-doc",		no_argument,		0,	OPT_STRIP_DOC },
	{"streaming",		no_argument,		0,	OPT_STREAMING },
	{"no-streaming",	no_argument,		0,	OPT_NO_STREAMING },
	{"recreating",		no_argument,		0,	OPT_RECREATING },
	{"disable-accel",	no_argument,		0,	OPT_DISABLE_ACCEL },
	{"disable-buffering",	no_argument,		0,	OPT_DISABLE_BUFFERING },
	{"disable-depth-limit",	no_argument,		0,	OPT_DISABLE_DEPTH_LIMIT },
	{"disable-mmap",	no_argument,		0,	OPT_DISABLE_MMAP },
	{"disable-diag",	required_argument,	0,	OPT_DISABLE_DIAG },
	{"enable-diag",		required_argument,	0,	OPT_ENABLE_DIAG },
	{"show-diag",		required_argument,	0,	OPT_SHOW_DIAG },
	{"hide-diag",		required_argument,	0,	OPT_HIDE_DIAG },
	{"yaml-1.1",		no_argument,		0,	OPT_YAML_1_1 },
	{"yaml-1.2",		no_argument,		0,	OPT_YAML_1_2 },
	{"yaml-1.3",		no_argument,		0,	OPT_YAML_1_3 },
	{"sloppy-flow-indentation", no_argument,	0,	OPT_SLOPPY_FLOW_INDENTATION },
	{"prefer-recursive",	no_argument,		0,	OPT_PREFER_RECURSIVE },
	{"ypath-aliases",	no_argument,		0,	OPT_YPATH_ALIASES },
	{"disable-flow-markers",no_argument,		0,	OPT_DISABLE_FLOW_MARKERS },
	{"disable-doc-markers", no_argument,		0,	OPT_DISABLE_DOC_MARKERS },
	{"disable-scalar-styles",no_argument,		0,	OPT_DISABLE_SCALAR_STYLES },
	{"dump-pathexpr",	no_argument,		0,	OPT_DUMP_PATHEXPR },
	{"document-event-stream",no_argument,		0,	OPT_DOCUMENT_EVENT_STREAM },
	{"noexec",		no_argument,		0,	OPT_NOEXEC },
	{"null-output",		no_argument,		0,	OPT_NULL_OUTPUT },
	{"no-ending-newline",	no_argument,		0,	OPT_NO_ENDING_NEWLINE },
	{"preserve-flow-layout",no_argument,		0,	OPT_PRESERVE_FLOW_LAYOUT },
	{"indented-seq-in-map", no_argument,		0,	OPT_INDENTED_SEQ_IN_MAP },
	{"collect-errors",	no_argument,		0,	OPT_COLLECT_ERRORS },
	{"allow-duplicate-keys",no_argument,		0,	OPT_ALLOW_DUPLICATE_KEYS },
	{"strip-empty-kv",	no_argument,		0,	OPT_STRIP_EMPTY_KV },
	{"tsv-format",		no_argument,		0,	OPT_TSV_FORMAT },
	{"to",			required_argument,	0,	'T' },
	{"from",		required_argument,	0,	'F' },
	{"quiet",		no_argument,		0,	'q' },

	{"check",		no_argument,		0,	OPT_CHECK },
	{"derive-key",		required_argument,	0,	OPT_DERIVE_KEY },
	{"no-names",		no_argument,		0,	OPT_NO_NAMES },
	{"raw",			no_argument,		0,	OPT_RAW },
	{"length",		required_argument,	0,	OPT_LENGTH },
	{"keyed",		no_argument,		0,	OPT_KEYED },
	{"list-backends",	no_argument,		0,	OPT_LIST_BACKENDS },
	{"backend",		required_argument,	0,	OPT_BACKEND },
	{"num-threads",		required_argument,	0,	OPT_NUM_THREADS },
	{"file-buffer",		required_argument,	0,	OPT_FILE_BUFFER },
	{"mmap-min-chunk",	required_argument,	0,	OPT_MMAP_MIN_CHUNK },
	{"mmap-max-chunk",	required_argument,	0,	OPT_MMAP_MAX_CHUNK },

#ifdef HAVE_REFLECTION
	{"reflect",		no_argument,		0,	OPT_REFLECT },
	{"generate-c",		no_argument,	        0,      OPT_GENERATE_C },
	{"entry-type",		required_argument,	0,	OPT_ENTRY_TYPE },
	{"entry-meta",		required_argument,	0,	OPT_ENTRY_META },
	{"packed-roundtrip",	no_argument,		0,	OPT_PACKED_ROUNDTRIP },
	{"cflags",              required_argument,      0,      OPT_CFLAGS },
	{"generate-blob",       required_argument,      0,      OPT_GENERATE_BLOB },
	{"import-blob",         required_argument,      0,      OPT_IMPORT_BLOB },
	{"import-c-file",	required_argument,	0,	OPT_IMPORT_C_FILE },
	{"no-prune-system",	no_argument,		0,	OPT_NO_PRUNE_SYSTEM },
	{"type-include",        required_argument,      0,      OPT_TYPE_INCLUDE },
	{"type-exclude",        required_argument,      0,      OPT_TYPE_EXCLUDE },
	{"dry-run",		no_argument,	        0,      OPT_DRY_RUN },
	{"dump-reflection",	no_argument,	        0,      OPT_DUMP_REFLECTION },
	{"debug-reflection",	no_argument,	        0,      OPT_DEBUG_REFLECTION },
	{"strict-annotations",	no_argument,	        0,      OPT_STRICT_ANNOTATIONS },
#endif
#ifdef HAVE_GENERIC
	{"generic",		no_argument,		0,	OPT_GENERIC },
	{"builder-policy",	required_argument,	0,	OPT_BUILDER_POLICY },
	{"dedup",		no_argument,		0,	OPT_DEDUP },
	{"no-dedup",		no_argument,		0,	OPT_NO_DEDUP },
	{"generic-testsuite",	no_argument,		0,	OPT_GENERIC_TESTSUITE },
	{"dump-primitives",	no_argument,		0,	OPT_DUMP_PRIMITIVES },
	{"generic-parse-dump",	no_argument,		0,	OPT_GENERIC_PARSE_DUMP },
	{"create-markers",	no_argument,		0,	OPT_CREATE_MARKERS },
	{"pyyaml-compat",	no_argument,		0,	OPT_PYYAML_COMPAT },
	{"keep-style",		no_argument,		0,	OPT_KEEP_STYLE },
#endif
	{"help",		no_argument,		0,	'h' },
	{"version",		no_argument,		0,	'v' },
	{0,			0,			0,	 0  },
};

static bool usage_colorize(FILE *fp)
{
#ifdef _WIN32
	if (fp == stdout || fp == stderr) {
		HANDLE h = GetStdHandle(fp == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
		DWORD dwMode;

		return h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &dwMode);
	}
	return _isatty(_fileno(fp)) == 1;
#else
	return isatty(fileno(fp)) == 1;
#endif
}

static void usage_print_style(FILE *fp, bool colorize, const char *style, const char *text)
{
	if (colorize && style && *style)
		fputs(style, fp);
	fputs(text, fp);
	if (colorize && style && *style)
		fputs(A_RESET, fp);
}

static const char *usage_option_style(const char *text, size_t len)
{
	if (!len)
		return NULL;
	if (len >= 2 && text[0] == '-' && text[1] == '-')
		return A_CYAN;
	if (text[0] == '-')
		return A_GREEN;
	if (text[0] == '<' && text[len - 1] == '>')
		return A_GREEN;
	return NULL;
}

static void usage_print_option(FILE *fp, bool colorize, const char *opt)
{
	const char *s, *e, *style;
	size_t len;

	for (s = opt; *s; s = e) {
		if (*s == ' ' || *s == ',') {
			fputc(*s, fp);
			e = s + 1;
			continue;
		}

		for (e = s; *e && *e != ' ' && *e != ','; e++)
			;

		len = (size_t)(e - s);
		style = usage_option_style(s, len);
		if (colorize && style)
			fputs(style, fp);
		fwrite(s, 1, len, fp);
		if (colorize && style)
			fputs(A_RESET, fp);
	}
}

static void usage_print_option_padded(FILE *fp, bool colorize, const char *opt, size_t width)
{
	size_t len, i;

	usage_print_option(fp, colorize, opt);

	len = strlen(opt);
	for (i = len; i < width; i++)
		fputc(' ', fp);
}

static void display_usage(FILE *fp, char *progname, int tool_mode)
{
	bool colorize;
	const char *hclr, *dclr, *vclr, *xclr, *reset;

	colorize = usage_colorize(fp);

	hclr = colorize ? A_BRIGHT_YELLOW : "";
	dclr = "";
	vclr = colorize ? A_GREEN : "";
	xclr = colorize ? A_CYAN : "";
	reset = colorize ? A_RESET : "";

#define USAGE_SECTION(title) \
	fprintf(fp, "\n%s%s%s:\n", hclr, (title), reset)
#define USAGE_ITEM(opt, desc) \
	do { \
		fprintf(fp, "  "); \
		usage_print_option_padded(fp, colorize, (opt), 32); \
		fprintf(fp, " : %s%s%s\n", dclr, (desc), reset); \
	} while (0)
#define USAGE_ITEM_DEFAULT(opt, desc, defval) \
	do { \
		fprintf(fp, "  "); \
		usage_print_option_padded(fp, colorize, (opt), 32); \
		fprintf(fp, " : %s%s%s (default %s%s%s)\n", \
			dclr, (desc), reset, vclr, (defval), reset); \
	} while (0)
#define USAGE_ITEM_DEFAULT_INT(opt, desc, defval) \
	do { \
		fprintf(fp, "  "); \
		usage_print_option_padded(fp, colorize, (opt), 32); \
		fprintf(fp, " : %s%s%s (default %s%d%s)\n", \
			dclr, (desc), reset, vclr, (defval), reset); \
	} while (0)
#define USAGE_ITEM_MAX_UINT(opt, desc, maxval) \
	do { \
		fprintf(fp, "  "); \
		usage_print_option_padded(fp, colorize, (opt), 32); \
		fprintf(fp, " : %s%s%s (max %s%u%s)\n", \
			dclr, (desc), reset, vclr, (maxval), reset); \
	} while (0)
#define USAGE_CONT(text) \
	fprintf(fp, "  %-32s   %s%s%s\n", "", dclr, (text), reset)
#define USAGE_EXAMPLES() \
	fprintf(fp, "%sExamples%s:\n\n", hclr, reset)
#define USAGE_EXAMPLE_TEXT(text) \
	fprintf(fp, "  %s\n", (text))
#define USAGE_EXAMPLE_CMD(fmt, ...) \
	fprintf(fp, "  %s$ " fmt "%s\n", xclr, __VA_ARGS__, reset)

	fprintf(fp, "%sUsage%s: ", hclr, reset);
	usage_print_style(fp, colorize, xclr, progname);
	fprintf(fp, " [options] [args]\n");
	USAGE_SECTION("General options");
	USAGE_ITEM("--help, -h", "Display this help message");
	USAGE_ITEM("--version, -v", "Display libfyaml version");
	USAGE_ITEM_DEFAULT("--quiet, -q", "Quiet operation", QUIET_DEFAULT ? "true" : "false");
	USAGE_ITEM_DEFAULT_INT("--debug-level, -d <lvl>", "Set debug level", DEBUG_LEVEL_DEFAULT);
	USAGE_ITEM_DEFAULT("--color, -C <mode>", "Color output (on, off, auto)", COLOR_DEFAULT);
	USAGE_ITEM_DEFAULT("--visible, -V", "Make whitespace and linebreaks visible", VISIBLE_DEFAULT ? "true" : "false");
	USAGE_ITEM("--dry-run", "Do not parse/emit");

	USAGE_SECTION("Diagnostic options");
	USAGE_ITEM("--disable-diag <x>", "Disable diag error module <x>");
	USAGE_ITEM("--enable-diag <x>", "Enable diag error module <x>");
	USAGE_ITEM("--show-diag <x>", "Show diag option <x> (source, position, type, module)");
	USAGE_ITEM("--hide-diag <x>", "Hide diag option <x> (source, position, type, module)");

	USAGE_SECTION("Parser options");

	USAGE_ITEM_DEFAULT("--include, -I <path>", "Add directory to include path", INCLUDE_DEFAULT);
	USAGE_ITEM_DEFAULT("--json, -j <mode>", "JSON input mode (no, force, auto)", JSON_DEFAULT);
	USAGE_ITEM_DEFAULT("--resolve, -r", "Perform anchor and merge key resolution", RESOLVE_DEFAULT ? "true" : "false");
	USAGE_ITEM("--yaml-1.1", "Enable YAML 1.1");
	USAGE_ITEM("--yaml-1.2", "Enable YAML 1.2");
	USAGE_ITEM("--yaml-1.3", "Enable YAML 1.3");
	USAGE_ITEM_DEFAULT("--follow, -l", "Follow aliases when using paths", FOLLOW_DEFAULT ? "true" : "false");
	USAGE_ITEM_DEFAULT("--sloppy-flow-indentation", "Enable sloppy indentation in flow mode", SLOPPY_FLOW_INDENTATION_DEFAULT ? "true" : "false");
	USAGE_ITEM_DEFAULT("--prefer-recursive", "Prefer recursive instead of iterative algorithms", PREFER_RECURSIVE_DEFAULT ? "true" : "false");
	USAGE_ITEM_DEFAULT("--ypath-aliases", "Use YPATH aliases", YPATH_ALIASES_DEFAULT ? "true" : "false");
	USAGE_ITEM_DEFAULT("--allow-duplicate-keys", "Allow duplicate keys", ALLOW_DUPLICATE_KEYS_DEFAULT ? "true" : "false");
	USAGE_ITEM_DEFAULT("--collect-errors", "Collect errors instead of outputting directly", COLLECT_ERRORS_DEFAULT ? "true" : "false");
	USAGE_ITEM_DEFAULT("--disable-accel", "Disable access accelerators", DISABLE_ACCEL_DEFAULT ? "true" : "false");
	USAGE_ITEM_DEFAULT("--disable-buffering", "Disable buffering (use unix fd)", DISABLE_BUFFERING_DEFAULT ? "true" : "false");
	USAGE_ITEM("--disable-mmap", "Disable mmap usage");
	USAGE_ITEM_DEFAULT("--disable-depth-limit", "Disable depth limit", DISABLE_DEPTH_LIMIT_DEFAULT ? "true" : "false");

	if (tool_mode == OPT_TOOL || tool_mode != OPT_TESTSUITE) {
		USAGE_SECTION("Emitter options");
		USAGE_ITEM_DEFAULT_INT("--indent, -i <indent>", "Set dump indent", INDENT_DEFAULT);
		USAGE_ITEM_DEFAULT_INT("--width, -w <width>", "Set dump width (inf for infinite)", WIDTH_DEFAULT);
		USAGE_ITEM_DEFAULT("--mode, -m <mode>", "Output mode", MODE_DEFAULT);
		USAGE_CONT("(original, block, flow, flow-oneline, json, json-tp, json-oneline, dejson, pretty, flow-compact, json-compact)");
		USAGE_ITEM_DEFAULT("--sort, -s", "Perform mapping key sort", SORT_DEFAULT ? "true" : "false");
		USAGE_ITEM_DEFAULT("--comment, -c", "Output comments (experimental)", COMMENT_DEFAULT ? "true" : "false");
		USAGE_ITEM_DEFAULT("--strip-labels", "Strip labels when emitting", STRIP_LABELS_DEFAULT ? "true" : "false");
		USAGE_ITEM_DEFAULT("--strip-tags", "Strip tags when emitting", STRIP_TAGS_DEFAULT ? "true" : "false");
		USAGE_ITEM_DEFAULT("--strip-doc", "Strip document headers and indicators", STRIP_DOC_DEFAULT ? "true" : "false");
		USAGE_ITEM_DEFAULT("--strip-empty-kv", "Strip keys with empty values", STRIP_EMPTY_KV_DEFAULT ? "true" : "false");
		USAGE_ITEM("--null-output", "Do not generate output");
		USAGE_ITEM("--no-ending-newline", "Do not generate a final newline");
		USAGE_ITEM("--preserve-flow-layout", "Preserve layout of single line flow collections");
		USAGE_ITEM("--indented-seq-in-map", "Indent block sequences in block mappings");

		if (tool_mode == OPT_TOOL || tool_mode == OPT_DUMP) {
			USAGE_ITEM_DEFAULT("--streaming", "Use streaming output mode", STREAMING_DEFAULT ? "true" : "false");
			USAGE_ITEM("--no-streaming", "Don't use streaming output mode");
			USAGE_ITEM_DEFAULT("--recreating", "Recreate streaming events", RECREATING_DEFAULT ? "true" : "false");
		}
	}

	if (tool_mode == OPT_TOOL || tool_mode == OPT_TESTSUITE) {
		USAGE_SECTION("Testsuite options");
		USAGE_ITEM_DEFAULT("--disable-flow-markers", "Disable testsuite's flow-markers", DISABLE_FLOW_MARKERS_DEFAULT ? "true" : "false");
		USAGE_ITEM_DEFAULT("--disable-doc-markers", "Disable testsuite's document-markers", DISABLE_DOC_MARKERS_DEFAULT ? "true" : "false");
		USAGE_ITEM_DEFAULT("--disable-scalar-styles", "Disable testsuite's scalar styles", DISABLE_SCALAR_STYLES_DEFAULT ? "true" : "false");
		USAGE_ITEM_DEFAULT("--document-event-stream", "Generate a document and then produce events", DOCUMENT_EVENT_STREAM_DEFAULT ? "true" : "false");
		USAGE_ITEM_DEFAULT("--tsv-format", "Display testsuite in TSV format", TSV_FORMAT_DEFAULT ? "true" : "false");
	}

	if (tool_mode == OPT_TOOL || (tool_mode != OPT_DUMP && tool_mode != OPT_TESTSUITE)) {
		USAGE_SECTION("Input options");
		USAGE_ITEM("--file, -f <file>", "Use given file instead of <stdin>");
		USAGE_CONT("(a leading '>' in the string is treated as literal content)");
	}

	if (tool_mode == OPT_TOOL || tool_mode == OPT_JOIN) {
		USAGE_SECTION("Join options");
		USAGE_ITEM_DEFAULT("--to, -T <path>", "Join to path", TO_DEFAULT);
		USAGE_ITEM_DEFAULT("--from, -F <path>", "Join from path", FROM_DEFAULT);
		USAGE_ITEM_DEFAULT("--trim, -t <path>", "Output given path", TRIM_DEFAULT);
	}

	if (tool_mode == OPT_TOOL || tool_mode == OPT_YPATH) {
		USAGE_SECTION("YPATH options");
		USAGE_ITEM_DEFAULT("--from, -F <path>", "Start from path", FROM_DEFAULT);
		USAGE_ITEM("--dump-pathexpr", "Dump the path expression before the results");
		USAGE_ITEM("--noexec", "Do not execute the expression");
	}

	if (tool_mode == OPT_TOOL || tool_mode == OPT_COMPOSE) {
		USAGE_SECTION("Compose options");
		USAGE_ITEM("--dump-path", "Dump the path while composing");
	}

	if (tool_mode == OPT_TOOL || tool_mode == OPT_B3SUM) {
		USAGE_SECTION("B3SUM options");
		USAGE_ITEM("--b3sum", "BLAKE3 hash b3sum mode");
		USAGE_ITEM("--derive-key <context>", "Key derivation mode");
		USAGE_ITEM("--no-names", "Omit filenames");
		USAGE_ITEM("--raw", "Output result in raw bytes");
		USAGE_ITEM_MAX_UINT("--length <n>", "Output only this amount of bytes", FY_BLAKE3_OUT_LEN);
		USAGE_ITEM("--check", "Read files with BLAKE3 checksums and check");
		USAGE_ITEM("--keyed", "Keyed mode (secret key from stdin)");
		USAGE_ITEM("--backend <backend>", "Select a BLAKE3 backend");
		USAGE_ITEM("--list-backends", "Print available backends");
		USAGE_ITEM("--num-threads <n>", "Number of threads (-1: disable, 0: auto)");
		USAGE_ITEM("--file-buffer <n>", "Size of file I/O buffer");
		USAGE_ITEM("--mmap-min-chunk <n>", "Minimum mmap chunk size");
		USAGE_ITEM("--mmap-max-chunk <n>", "Maximum mmap chunk size");
	}

#ifdef HAVE_REFLECTION
	if (tool_mode == OPT_TOOL || tool_mode == OPT_REFLECT) {
		USAGE_SECTION("Reflection options");
		USAGE_ITEM("--reflect", "Enable reflection mode");
		USAGE_ITEM("--generate-c", "Generate C definitions from reflection input");
		USAGE_ITEM("--generate-blob <blob>", "Generate packed blob from C source files");
		USAGE_ITEM("--import-blob <blob>", "Import a packed blob as a reflection source");
		USAGE_ITEM("--import-c-file <file>", "Import a C file as a reflection source");
		USAGE_ITEM("--cflags <cflags>", "The C flags to use for the import");
		USAGE_ITEM("--entry-type <type>", "The C type that is the entry point");
		USAGE_ITEM("--packed-roundtrip", "Roundtrip reflection to packed type for debugging");
		USAGE_ITEM("--no-prune-system", "Do not prune system types");
		USAGE_ITEM("--type-include <regex>", "Include only types matching regex");
		USAGE_ITEM("--type-exclude <regex>", "Exclude types matching regex");
		USAGE_ITEM("--dump-reflection", "Dump reflection structures");
		USAGE_ITEM("--debug-reflection", "Debug messages during reflection processing");
	}
#endif

#ifdef HAVE_GENERIC
	if (tool_mode == OPT_TOOL || tool_mode == OPT_GENERIC ||
	    tool_mode == OPT_GENERIC_TESTSUITE || tool_mode == OPT_GENERIC_PARSE_DUMP) {
		USAGE_SECTION("Generic options");
		USAGE_ITEM("--generic", "Generics dump");
		USAGE_ITEM("--generic-testsuite", "Generics testsuite");
		USAGE_ITEM("--builder-policy", "Builder policy");
		USAGE_ITEM("--generic-parse-dump", "Generics parse-dump");
		USAGE_ITEM_DEFAULT("--dedup", "Dedup mode on", DEDUP_DEFAULT ? "true" : "false");
		USAGE_ITEM("--no-dedup", "Dedup mode off");
		USAGE_ITEM("--dump-primitives", "Dump primitives");
		USAGE_ITEM("--create-markers", "Create markers");
		USAGE_ITEM("--pyyaml-compat", "PYYAML compatibility mode");
		USAGE_ITEM("--keep-style", "Do not strip style");
	}
#endif

	if (tool_mode == OPT_TOOL) {
		USAGE_SECTION("Modes");
		USAGE_ITEM("--dump", "Dump mode (default)");
		USAGE_ITEM("--testsuite", "Testsuite mode");
		USAGE_ITEM("--filter", "Filter mode");
		USAGE_ITEM("--join", "Join mode");
		USAGE_ITEM("--ypath", "YPATH mode");
		USAGE_ITEM("--scan-dump", "Scan-dump mode (internal)");
		USAGE_ITEM("--parse-dump", "Parse-dump mode (internal)");
		USAGE_ITEM("--compose", "Composer driver dump mode");
		USAGE_ITEM("--yaml-version-dump", "Information about YAML versions");
#ifdef HAVE_REFLECTION
		USAGE_ITEM("--reflect", "Reflection mode");
#endif
#ifdef HAVE_GENERIC
		USAGE_ITEM("--generic", "Generic mode");
#endif
	}

	fprintf(fp, "\n");

	switch (tool_mode) {
	case OPT_TOOL:
	default:
		break;
	case OPT_TESTSUITE:
		USAGE_EXAMPLES();
		USAGE_EXAMPLE_TEXT("  Parse and dump test-suite event format");
		USAGE_EXAMPLE_CMD("%s input.yaml", progname);
		fprintf(fp, "\n");
		USAGE_EXAMPLE_TEXT("  Parse and dump of event example from stdin");
		USAGE_EXAMPLE_CMD("echo \"foo: bar\" | %s -", progname);
		fprintf(fp, "  +STR\n  +DOC\n  +MAP\n  =VAL :foo\n  =VAL :bar\n  -MAP\n  -DOC\n  -STR\n");
		break;
	case OPT_DUMP:
		USAGE_EXAMPLES();
		USAGE_EXAMPLE_TEXT("  Parse and dump generated YAML document tree in the original YAML form");
		USAGE_EXAMPLE_CMD("%s input.yaml", progname);
		fprintf(fp, "\n");
		USAGE_EXAMPLE_TEXT("  Parse and dump generated YAML document tree in block YAML form (and make whitespace visible)");
		USAGE_EXAMPLE_CMD("%s -V -mblock input.yaml", progname);
		fprintf(fp, "\n");
		USAGE_EXAMPLE_TEXT("  Parse and dump generated YAML document from the input string");
		USAGE_EXAMPLE_CMD("%s -mjson \">foo: bar\"", progname);
		fprintf(fp, "  {\n    \"foo\": \"bar\"\n  }\n");
		break;
	case OPT_FILTER:
		USAGE_EXAMPLES();
		USAGE_EXAMPLE_TEXT("  Parse and filter YAML document tree starting from the '/foo' path followed by the '/bar' path");
		USAGE_EXAMPLE_CMD("%s --file input.yaml /foo /bar", progname);
		fprintf(fp, "\n");
		USAGE_EXAMPLE_TEXT("  Parse and filter for two paths (note how a multi-document stream is produced)");
		USAGE_EXAMPLE_CMD("%s --file -mblock --filter --file \">{ foo: bar, baz: [ frooz, whee ] }\" /foo /baz", progname);
		fprintf(fp, "  bar\n  ---\n  - frooz\n  - whee\n\n");
		USAGE_EXAMPLE_TEXT("  Parse and filter YAML document in stdin (note how the key may be complex)");
		USAGE_EXAMPLE_CMD("echo \"{ foo: bar }: baz\" | %s \"/{foo: bar}/\"", progname);
		fprintf(fp, "  baz\n");
		break;
	case OPT_JOIN:
		USAGE_EXAMPLES();
		USAGE_EXAMPLE_TEXT("  Parse and join two YAML files");
		USAGE_EXAMPLE_CMD("%s file1.yaml file2.yaml", progname);
		fprintf(fp, "\n");
		USAGE_EXAMPLE_TEXT("  Parse and join two YAML maps");
		USAGE_EXAMPLE_CMD("%s \">foo: bar\" \">baz: frooz\"", progname);
		fprintf(fp, "  foo: bar\n  baz: frooz\n\n");
		USAGE_EXAMPLE_TEXT("  Parse and join two YAML sequences");
		USAGE_EXAMPLE_CMD("%s -mblock \">[ foo ]\" \">[ bar ]\"", progname);
		fprintf(fp, "  - foo\n  - bar\n");
		break;
	case OPT_YPATH:
		USAGE_EXAMPLES();
		USAGE_EXAMPLE_TEXT("  Parse and filter YAML with the ypath expression that results to /foo followed by /bar");
		USAGE_EXAMPLE_CMD("%s --ypath /foo,bar input.yaml", progname);
		break;
	case OPT_SCAN_DUMP:
		USAGE_EXAMPLES();
		USAGE_EXAMPLE_TEXT("  Parse and dump YAML scanner tokens (internal)");
		break;
	case OPT_PARSE_DUMP:
		USAGE_EXAMPLES();
		USAGE_EXAMPLE_TEXT("  Parse and dump YAML parser events (internal)");
		break;
	case OPT_COMPOSE:
		USAGE_EXAMPLES();
		USAGE_EXAMPLE_TEXT("  Parse and dump generated YAML document tree using the composer api");
		USAGE_EXAMPLE_CMD("%s input.yaml", progname);
		fprintf(fp, "\n");
		USAGE_EXAMPLE_TEXT("  Parse and dump generated YAML document tree in block YAML form (and make whitespace visible)");
		USAGE_EXAMPLE_CMD("%s --compose -V -mblock input.yaml", progname);
		fprintf(fp, "\n");
		USAGE_EXAMPLE_TEXT("  Parse and dump generated YAML document from the input string");
		USAGE_EXAMPLE_CMD("%s --compose -mjson \">foo: bar\"", progname);
		fprintf(fp, "  {\n    \"foo\": \"bar\"\n  }\n");
		break;
	case OPT_YAML_VERSION_DUMP:
		USAGE_EXAMPLES();
		USAGE_EXAMPLE_TEXT("  Display information about the YAML versions libfyaml supports");
		break;

	case OPT_B3SUM:
		USAGE_EXAMPLES();
		USAGE_EXAMPLE_TEXT("  BLAKE3 hash b3sum utility");
		break;

#ifdef HAVE_REFLECTION
	case OPT_REFLECT:
		USAGE_EXAMPLES();
		USAGE_EXAMPLE_TEXT("  Reflection parsing a C header and dumping type info");
		USAGE_EXAMPLE_CMD("%s [--cflags=<>] header.h", progname);
		fprintf(fp, "\n");
		USAGE_EXAMPLE_TEXT("  Reflection parsing a C header and dumping type info");
		USAGE_EXAMPLE_CMD("%s blob.bin", progname);
		fprintf(fp, "\n");
		USAGE_EXAMPLE_TEXT("  Reflection convert C header files definition to a blob");
		USAGE_EXAMPLE_CMD("%s --reflect [--cflags=<>] --generate-blob=blob.bin header1.h header2.h", progname);
		break;
#endif

#ifdef HAVE_GENERIC
	case OPT_GENERIC:
		USAGE_EXAMPLES();
		USAGE_EXAMPLE_TEXT("  Generic dump");
		break;

	case OPT_GENERIC_TESTSUITE:
		USAGE_EXAMPLES();
		USAGE_EXAMPLE_TEXT("  Generic testsuite");
		break;

	case OPT_GENERIC_PARSE_DUMP:
		USAGE_EXAMPLES();
		USAGE_EXAMPLE_TEXT("  Generic parse dump");
		break;
#endif
	}

#undef USAGE_SECTION
#undef USAGE_ITEM
#undef USAGE_ITEM_DEFAULT
#undef USAGE_ITEM_DEFAULT_INT
#undef USAGE_ITEM_MAX_UINT
#undef USAGE_CONT
#undef USAGE_EXAMPLES
#undef USAGE_EXAMPLE_TEXT
#undef USAGE_EXAMPLE_CMD
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
		{ .name = "pretty",		.value = FYECF_MODE_PRETTY },
		{ .name = "yamlfmt",		.value = FYECF_MODE_PRETTY },	/* alias for pretty */
		{ .name = "flow-compact",	.value = FYECF_MODE_FLOW_COMPACT },
		{ .name = "json-compact",	.value = FYECF_MODE_JSON_COMPACT },
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
	size_t len;
	int rc;

	if (!arg || !flagsp || !modify_flags)
		return -1;

	s = arg;
	e = arg + strlen(s);
	while (s < e) {
		sn = strchr(s, ',');
		if (!sn)
			sn = e;

		len = (size_t)(sn - s);
		targ = alloca(len + 1);
		memcpy(targ, s, len);
		targ[len] = '\0';

		rc = modify_flags(targ, flagsp);
		if (rc)
			return rc;

		s = sn < e ? (sn + 1) : sn;
	}

	return 0;
}

static int set_parser_input(struct fy_parser *fyp,
			    const char *what,
			    bool default_string FY_UNUSED)
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

static void no_diag_output_fn(struct fy_diag *diag FY_UNUSED,
			      void *user FY_UNUSED,
			      const char *buf FY_UNUSED,
			      size_t len FY_UNUSED)
{
	/* nothing */
}

struct composer_data {
	struct fy_parser *fyp;
	struct fy_document *fyd;
	struct fy_emitter *emit;
	bool null_output;
	bool document_ready;
	bool verbose;
	bool single_document;
};

static enum fy_composer_return
compose_process_event(struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path, void *userdata)
{
	struct composer_data *cd = userdata;
	struct fy_document *fyd;
	struct fy_path_component *parent, *last;
	struct fy_node *fyn, *fyn_parent;
	struct fy_node_pair *fynp;
	int rc;

	if (cd->verbose) {
		fy_parser_info(fyp, "%s: %c%c%c%c%c %3d - %-32s\n",
				fy_event_type_get_text(fye->type),
				fy_path_in_root(path) ? 'R' : '-',
				fy_path_in_sequence(path) ? 'S' : '-',
				fy_path_in_mapping(path) ? 'M' : '-',
				fy_path_in_mapping_key(path) ? 'K' :
					fy_path_in_mapping_value(path) ? 'V' : '-',
				fy_path_in_collection_root(path) ? '/' : '-',
				fy_path_depth(path),
				fy_path_get_text_alloca(path));
	}

	switch (fye->type) {
		/* nothing to do for those */
	case FYET_NONE:
	case FYET_STREAM_START:
	case FYET_STREAM_END:
		break;

	case FYET_DOCUMENT_START:
		if (cd->fyd) {
			fy_document_destroy(cd->fyd);
			cd->fyd = NULL;
		}
		cd->document_ready = false;

		cd->fyd = fy_document_create_from_event(fyp, fye);
		assert(cd->fyd);

		break;

	case FYET_DOCUMENT_END:
		rc = fy_document_update_from_event(cd->fyd, fyp, fye);
		assert(!rc);

		cd->document_ready = true;

		if (!cd->null_output && cd->fyd)
			fy_emit_document(cd->emit, cd->fyd);

		fy_document_destroy(cd->fyd);
		cd->fyd = NULL;

		/* on single document mode we stop here */
		if (cd->single_document)
			return FYCR_OK_STOP;
		break;


	case FYET_SCALAR:
	case FYET_ALIAS:
	case FYET_MAPPING_START:
	case FYET_SEQUENCE_START:

		fyd = cd->fyd;
		assert(fyd);

		fyn = fy_node_create_from_event(fyd, fyp, fye);
		assert(fyn);

		switch (fye->type) {
		default:
			/* XXX should now happen */
			break;

		case FYET_SCALAR:
		case FYET_ALIAS:
			last = NULL;
			break;

		case FYET_MAPPING_START:
			last = fy_path_last_component(path);
			assert(last);

			fy_path_component_set_mapping_user_data(last, fyn);
			fy_path_component_set_mapping_key_user_data(last, NULL);

			break;

		case FYET_SEQUENCE_START:

			last = fy_path_last_component(path);
			assert(last);

			fy_path_component_set_sequence_user_data(last, fyn);
			break;
		}

		/* parent */
		parent = fy_path_last_not_collection_root_component(path);

		if (fy_path_in_root(path)) {

			rc = fy_document_set_root(cd->fyd, fyn);
			assert(!rc);

		} else if (fy_path_in_sequence(path)) {

			assert(parent);
			fyn_parent = fy_path_component_get_sequence_user_data(parent);
			assert(fyn_parent);
			assert(fy_node_is_sequence(fyn_parent));

			rc = fy_node_sequence_add_item(fyn_parent, fyn);
			assert(!rc);

		} else {
			/* only thing left */
			assert(fy_path_in_mapping(path));

			assert(parent);
			fyn_parent = fy_path_component_get_mapping_user_data(parent);
			assert(fyn_parent);
			assert(fy_node_is_mapping(fyn_parent));

			if (fy_path_in_mapping_key(path)) {

				fynp = fy_node_pair_create_with_key(fyd, fyn_parent, fyn);
				assert(fynp);

				fy_path_component_set_mapping_key_user_data(parent, fynp);

			} else {

				assert(fy_path_in_mapping_value(path));

				fynp = fy_path_component_get_mapping_key_user_data(parent);
				assert(fynp);

				rc = fy_node_pair_update_with_value(fynp, fyn);
				if (rc)	/* this may happen normally */
					goto err_out;

				fy_path_component_set_mapping_key_user_data(parent, NULL);
			}
		}

		break;

	case FYET_MAPPING_END:
		last = fy_path_last_component(path);
		assert(last);

		fyn = fy_path_component_get_mapping_user_data(last);
		assert(fyn);
		assert(fy_node_is_mapping(fyn));

		rc = fy_node_update_from_event(fyn, fyp, fye);
		assert(!rc);

		break;

	case FYET_SEQUENCE_END:
		last = fy_path_last_component(path);
		assert(last);

		fyn = fy_path_component_get_sequence_user_data(last);
		assert(fyn);
		assert(fy_node_is_sequence(fyn));

		rc = fy_node_update_from_event(fyn, fyp, fye);
		assert(!rc);

		break;
	}

	return FYCR_OK_CONTINUE;

err_out:
	return FYCR_ERROR;
}

struct b3sum_config {
	bool no_names : 1,
	     raw : 1,
	     keyed : 1,
	     check : 1,
	     derive_key : 1,
	     quiet : 1,
	     list_backends : 1,
	     no_mmap : 1;
	size_t file_buffer;
	size_t mmap_min_chunk;
	size_t mmap_max_chunk;
	unsigned int length;
	const char *context;
	size_t context_len;
	const char *backend;
	unsigned int num_threads;
};

struct b3sum_config default_b3sum_cfg = {
	.length = FY_BLAKE3_OUT_LEN,
};

static int do_b3sum_hash_file(struct fy_blake3_hasher *hasher, const char *filename, bool no_names, bool raw, unsigned int length)
{
	static const char *hexb = "0123456789abcdef";
	const uint8_t *output;
	size_t filename_sz, line_sz, outsz;
	ssize_t wrn;
	uint8_t v;
	const void *outp;
	char *line, *s;
	unsigned int i;

	filename_sz = strlen(filename);

	output = fy_blake3_hash_file(hasher, filename);
	if (!output) {
		fprintf(stderr, "Failed to hash file: \"%s\", error: %s\n", filename, strerror(errno));
		return -1;
	}

	if (!raw) {
		/* output line (optimized) */
		line_sz = (length * 2);		/* the hex output */
		if (!no_names)
			line_sz += 2 + filename_sz;	/* 2 spaces + filename */
		line_sz++;	/* '\n' */
		line = alloca(line_sz + 1);
		s = line;
		for (i = 0; i < length; i++) {
			v = output[i];
			*s++ = hexb[v >> 4];
			*s++ = hexb[v & 15];
		}
		if (!no_names) {
			*s++ = ' ';
			*s++ = ' ';
			memcpy(s, filename, filename_sz);
			s += filename_sz;
		}
		*s++ = '\n';
		*s = '\0';
		outp = line;
		outsz = (size_t)(s - line);
	} else {
		outp = output;
		outsz = length;
	}

	wrn = fwrite(outp, 1, outsz, stdout);
	if ((size_t)wrn != outsz) {
		fprintf(stderr, "Unable to write to stdout! error: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static int do_b3sum_check_file(struct fy_blake3_hasher *hasher, const char *check_filename, bool quiet)
{
	char *hash, *filename;
	FILE *fp = NULL;
	char linebuf[8192];	/* maximum size for a line is 8K, should be enough (PATH_MAX is 4K at linux) */
	uint8_t read_hash[FY_BLAKE3_OUT_LEN], v;
	const uint8_t *computed_hash;
	char *s;
	char c;
	unsigned int i, j;
	size_t length;
	size_t linesz;
	int line, exit_code;

	if (check_filename && strcmp(check_filename, "-")) {
		fp = fopen(check_filename, "ra");
		if (!fp) {
			fprintf(stderr, "Failed to open check file: \"%s\", error: %s\n", check_filename, strerror(errno));
			goto err_out;
		}
	} else {
		fp = stdin;
	}

	/* default error code if all is fine */
	exit_code = 0;

	line = 0;
	while (fgets(linebuf, sizeof(linebuf), fp)) {
		/* '\0' terminate always */
		linebuf[(sizeof(linebuf)/sizeof(linebuf[0]))-1] = '\0';

		linesz = strlen(linebuf);
		while (linesz > 0 && linebuf[linesz-1] == '\n')
			linesz--;

		if (!linesz) {
			fprintf(stderr, "Empty line found at file \"%s\" line #%d\n", check_filename, line);
			goto err_out;
		}
		linebuf[linesz] = '\0';

		length = 0;
		s = linebuf;
		while (isxdigit((unsigned char)*s))
			s++;

		length = (size_t)(s - linebuf);

		if (length == 0 || length > (FY_BLAKE3_OUT_LEN * 2) || (length % 1) || !isspace((unsigned char)*s)) {
			fprintf(stderr, "Bad line found at file \"%s\" line #%d\n", check_filename, line);
			fprintf(stderr, "%s\n", linebuf);
			goto err_out;
		}

		*s++ = '\0';

		while (isspace((unsigned char)*s))
			s++;

		length >>= 1;	/* to bytes */
		hash = linebuf;
		filename = s;

		for (i = 0, s = hash; i < length; i++) {
			v = 0;
			for (j = 0; j < 2; j++) {
				v <<= 4;
				c = *s++;
				if (c >= '0' && c <= '9')
					v |= c - '0';
				else if (c >= 'a' && c <= 'f')
					v |= c - 'a' + 10;
				else if (c >= 'A' && c <= 'F')
					v |= c - 'A' + 10;
				else
					v = 0;
			}
			read_hash[i] = v;
		}

		computed_hash = fy_blake3_hash_file(hasher, filename);
		if (!computed_hash) {
			fprintf(stderr, "Failed to hash file: \"%s\", error: %s\n", filename, strerror(errno));
			goto err_out;
		}

		/* constant time comparison */
		v = 0;
		for (i = 0; i < length; i++)
			v |= (read_hash[i] ^ computed_hash[i]);

		if (v) {
			printf("%s: FAILED\n", filename);
			exit_code = -1;
		} else if (!quiet)
			printf("%s: OK\n", filename);
	}

out:
	if (fp && fp != stdin)
		fclose(fp);

	return exit_code;

err_out:
	exit_code = -1;
	goto out;
}

static int
do_b3sum(int argc, char *argv[], int optind, const struct b3sum_config *cfg)
{
	struct fy_blake3_hasher_cfg hcfg;
	struct fy_blake3_hasher *hasher;
	uint8_t key[FY_BLAKE3_OUT_LEN];
	const char *filename;
	int rc, num_inputs, num_ok, i;
	size_t rdn;
	const char *backend, *prev;

	if (cfg->list_backends) {
		prev = NULL;
		while ((backend = fy_blake3_backend_iterate(&prev)) != NULL)
			printf("%s\n", backend);
		return 0;
	}

	if (cfg->quiet && !cfg->check) {
		fprintf(stderr, "Error: --quiet may only be used together with --check\n\n");
		return 1;
	}

	if (cfg->keyed && cfg->derive_key) {
		fprintf(stderr, "Error: --keyed and --derive-key may not be used together\n\n");
		return 1;
	}

	if (cfg->check && cfg->length != FY_BLAKE3_OUT_LEN) {
		fprintf(stderr, "Error: --check and --length may not be used together\n\n");
		return 1;
	}

	if (cfg->keyed) {
		rdn = fread(key, 1, FY_BLAKE3_KEY_LEN, stdin);
		if (rdn != FY_BLAKE3_KEY_LEN) {
			if (rdn > 0 && rdn < FY_BLAKE3_KEY_LEN)
				fprintf(stderr, "Error: could not read secret key from <stdin>: short key\n\n");
			else
				fprintf(stderr, "Error: could not read secret key from <stdin>: error %s\n\n", strerror(errno));
			return 1;
		}
		rc = fgetc(stdin);
		if (rc != EOF) {
			fprintf(stderr, "Error: garbage trailing secret key from <stdin>\n\n");
			return -1;
		}
	}

	num_inputs = argc - optind;
	if (num_inputs <= 0)
		num_inputs = 1;	/* stdin mode */

	if (cfg->raw && num_inputs > 1) {
		fprintf(stderr, "Error: Raw output mode is only supported with a single input\n\n");
		return 1;
	}

	/* we can't handle '-' in keyed mode */
	if (cfg->keyed) {
		for (i = optind; i < argc; i++) {
			if (!strcmp(argv[i], "-")) {
				fprintf(stderr, "Cannot use <stdin> in keyed mode\n");
				return 1;
			}
		}
	}

	memset(&hcfg, 0, sizeof(hcfg));
	hcfg.key = cfg->keyed ? key : NULL;
	hcfg.context = cfg->derive_key ? cfg->context : NULL;
	hcfg.context_len = cfg->derive_key ? cfg->context_len : 0;
	hcfg.backend = cfg->backend;
	hcfg.no_mmap = cfg->no_mmap;
	hcfg.file_buffer = cfg->file_buffer;
	hcfg.mmap_min_chunk = cfg->mmap_min_chunk;
	hcfg.mmap_max_chunk = cfg->mmap_max_chunk;
	hcfg.num_threads = cfg->num_threads;
	hasher = fy_blake3_hasher_create(&hcfg);
	if (!hasher) {
		fprintf(stderr, "unable to create blake3 hasher\n");
		return -1;
	}

	/* we will get in the loop even when no arguments (we'll do stdin instead) */
	num_ok = 0;
	i = optind;
	do {
		/* if no arguments, use stdin */
		filename = i < argc ? argv[i] : "-";

		if (!cfg->check)
			rc = do_b3sum_hash_file(hasher, filename, cfg->no_names, cfg->raw, cfg->length);
		else
			rc = do_b3sum_check_file(hasher, filename, cfg->quiet);
		if (!rc)
			num_ok++;

	} while (++i < argc);

	fy_blake3_hasher_destroy(hasher);

	return num_inputs == num_ok ? 0 : -1;
}

#ifdef HAVE_REFLECTION

/*
 * Reflection meta-type implementation moved to library files:
 *   src/reflection/fy-type-meta.c  - meta/annotation system
 *   src/reflection/fy-meta-type.c  - type data management
 *   src/reflection/fy-type-context.c - type system context
 *   src/reflection/fy-meta-serdes.c - serialization/deserialization
 */

static size_t estimate_max_file_size(int argc, char **argv)
{
	struct stat sb;
	size_t size;
	int i, rc;

	if (argc <= 0)
		return 0;	/* can't estimate without file (probably stdin) */

	size = 0;
	for (i = 0; i < argc; i++) {
		rc = stat(argv[i], &sb);
		if (rc)
			continue;

		/* only do it for regular files */
		if ((sb.st_mode & S_IFMT) != S_IFREG)
			continue;

		if ((size_t)sb.st_size > size)
			size = sb.st_size;
	}

	return size;
}


#endif /* HAVE_REFLECTION */

#ifdef HAVE_GENERIC

struct generic_config {
	bool dedup : 1;
	bool null_output : 1;
	bool dump_primitives : 1;
	bool create_markers : 1;
	bool testsuite : 1;
	bool parse_dump : 1;
	bool pyyaml_compat : 1;
	size_t estimated_max_size;
	enum fy_parse_cfg_flags parse_cfg_flags;
	enum fy_emitter_cfg_flags emit_cfg_flags;
	enum fy_emitter_xcfg_flags emit_xcfg_flags;
	struct fy_diag *diag;
	bool collect_diag;
	bool keep_comments;
	bool keep_style;
};

struct generic_config default_generic_cfg = {
	.dedup = true,
	.null_output = false,
	.estimated_max_size = 0,	/* adapt to input */
};

static int
do_generic(int argc, char **argv, int optind, struct generic_config *gcfg)
{
	/* Create auto allocator with appropriate scenario based on dedup parameter */
	struct fy_allocator *allocator = NULL;
	struct fy_generic_builder *gb = NULL;
	struct fy_generic_iterator_cfg cfg;
	struct fy_generic_iterator *fygi = NULL;
	struct fy_event *fye = NULL;
	struct fy_auto_allocator_cfg auto_cfg;
	struct fy_generic_builder_cfg gb_cfg;
	size_t max_size;
	const char *filename;
	enum fy_op_parse_flags parse_flags;
	enum fy_op_emit_flags emit_flags;
	fy_generic v;
	enum dump_testsuite_event_flags dump_flags = 0;
	bool had_error;
	int i, rc;

	if (gcfg->testsuite || gcfg->parse_dump)
		dump_flags = isatty(fileno(stdout)) ? DTEF_COLORIZE : 0;

	max_size = gcfg->estimated_max_size;
	if (!max_size) {
		max_size = estimate_max_file_size(argc - optind, argv + optind);
		if (!max_size)
			max_size = 1U << 20;	/* 1MB */
	}

	if (max_size < (1U << 20))
		max_size = (1U << 20);

	memset(&auto_cfg, 0, sizeof(auto_cfg));
	auto_cfg.scenario = gcfg->dedup ? FYAST_PER_TAG_FREE_DEDUP : FYAST_PER_TAG_FREE;
	auto_cfg.estimated_max_size = (size_t)(max_size * 1.2);

	allocator = fy_allocator_create("auto", &auto_cfg);
	if (!allocator)
		goto err_out;

	memset(&gb_cfg, 0, sizeof(gb_cfg));
	gb_cfg.allocator = allocator;
	gb_cfg.estimated_max_size = max_size;
	gb_cfg.flags = FYGBCF_SCOPE_LEADER | (gcfg->dedup ? FYGBCF_DEDUP_ENABLED : 0);
	if (!gcfg->pyyaml_compat)
		gb_cfg.flags |= FYGBCF_SCHEMA_AUTO;
	else
		gb_cfg.flags |= FYGBCF_SCHEMA_YAML1_1_PYYAML;
	gb_cfg.diag = gcfg->diag;
	gb = fy_generic_builder_create(&gb_cfg);
	if (!gb)
		goto err_out;

	had_error = false;

	i = optind;
	do {
		filename = i < argc ? argv[i] : "-";

		fy_generic_builder_reset(gb);

		parse_flags = FYOPPF_MULTI_DOCUMENT;
		emit_flags = FYOPEF_MULTI_DOCUMENT;

		if (gcfg->testsuite)
			parse_flags |= FYOPPF_DONT_RESOLVE;

		switch (gcfg->parse_cfg_flags & (FYPCF_JSON_MASK << FYPCF_JSON_SHIFT)) {
		default:
		case FYPCF_JSON_AUTO:
		case FYPCF_JSON_NONE:
			switch (gcfg->parse_cfg_flags & (FYPCF_DEFAULT_VERSION_MASK << FYPCF_DEFAULT_VERSION_SHIFT)) {
			default:
			case FYPCF_DEFAULT_VERSION_AUTO:
				parse_flags |= FYOPPF_MODE_AUTO;
				break;
			case FYPCF_DEFAULT_VERSION_1_1:
				parse_flags |= FYOPPF_MODE_YAML_1_1;
				break;
			case FYPCF_DEFAULT_VERSION_1_2:
				parse_flags |= FYOPPF_MODE_YAML_1_2;
				break;
			case FYPCF_DEFAULT_VERSION_1_3:
				parse_flags |= FYOPPF_MODE_YAML_1_3;
				break;
			}
			break;
		case FYPCF_JSON_FORCE:
			parse_flags |= FYOPPF_MODE_JSON;
			break;
		}

		if (gcfg->collect_diag)
			parse_flags |= FYOPPF_COLLECT_DIAG;

		if (gcfg->keep_comments) {
			parse_flags |= FYOPPF_KEEP_COMMENTS;
			emit_flags |= FYOPEF_OUTPUT_COMMENTS;
		}

		if (gcfg->keep_style)
			parse_flags |= FYOPPF_KEEP_STYLE;

		if (gcfg->create_markers)
			parse_flags |= FYOPPF_CREATE_MARKERS;

		if (!strcmp(filename, "-"))
			v = fy_parse(gb, fy_invalid,
				     parse_flags | FYOPPF_INPUT_TYPE_STDIN, NULL);
		else
			v = fy_parse_file(gb,
					  parse_flags, filename);

		if (fy_generic_is_invalid(v)) {
			had_error = true;
			break;
		}

		/* we don't support arbitrary indents */
		switch (gcfg->emit_cfg_flags & (FYECF_INDENT_MASK << FYECF_INDENT_SHIFT)) {
		case FYECF_INDENT_DEFAULT:
			emit_flags |= FYOPEF_INDENT_DEFAULT;
			break;
		case FYECF_INDENT_1:
			emit_flags |= FYOPEF_INDENT_1;
			break;
		case FYECF_INDENT_2:
			emit_flags |= FYOPEF_INDENT_2;
			break;
		case FYECF_INDENT_3:
			emit_flags |= FYOPEF_INDENT_3;
			break;
		case FYECF_INDENT_4:
		case FYECF_INDENT_5:
			emit_flags |= FYOPEF_INDENT_4;
			break;
		case FYECF_INDENT_6:
		case FYECF_INDENT_7:
			emit_flags |= FYOPEF_INDENT_6;
			break;
		default:
		case FYECF_INDENT_8:
			emit_flags |= FYOPEF_INDENT_8;
			break;
		}

		/* we don't support arbitrary widths */
		switch (gcfg->emit_cfg_flags & (FYECF_WIDTH_MASK << FYECF_WIDTH_SHIFT)) {
		default:
			emit_flags |= FYOPEF_WIDTH_DEFAULT;
			break;
		case FYECF_WIDTH_80:
			emit_flags |= FYOPEF_WIDTH_80;
			break;
		case FYECF_WIDTH_132:
			emit_flags |= FYOPEF_WIDTH_132;
			break;
		case FYECF_WIDTH_INF:
			emit_flags |= FYOPEF_WIDTH_INF;
			break;
		}

		/* generic emit supports JSON plus YAML style selection */
		switch (gcfg->emit_cfg_flags & (FYECF_MODE_MASK << FYECF_MODE_SHIFT)) {
		case FYECF_MODE_ORIGINAL:
		default:
			emit_flags |= FYOPEF_MODE_YAML_1_2;
			emit_flags |= FYOPEF_STYLE_DEFAULT;
			break;
		case FYECF_MODE_BLOCK:
			emit_flags |= FYOPEF_MODE_YAML_1_2;
			emit_flags |= FYOPEF_STYLE_BLOCK;
			break;
		case FYECF_MODE_FLOW:
			emit_flags |= FYOPEF_MODE_YAML_1_2;
			emit_flags |= FYOPEF_STYLE_FLOW;
			break;
		case FYECF_MODE_PRETTY:
			emit_flags |= FYOPEF_MODE_YAML_1_2;
			emit_flags |= FYOPEF_STYLE_PRETTY;
			break;
		case FYECF_MODE_FLOW_COMPACT:
			emit_flags |= FYOPEF_MODE_YAML_1_2;
			emit_flags |= FYOPEF_STYLE_COMPACT;
			break;
		case FYECF_MODE_FLOW_ONELINE:
			emit_flags |= FYOPEF_MODE_YAML_1_2;
			emit_flags |= FYOPEF_STYLE_ONELINE;
			break;
		case FYECF_MODE_JSON:
			emit_flags |= FYOPEF_MODE_JSON;
			emit_flags |= FYOPEF_STYLE_DEFAULT;
			break;
		case FYECF_MODE_JSON_ONELINE:
			emit_flags |= FYOPEF_MODE_JSON;
			emit_flags |= FYOPEF_STYLE_ONELINE;
			break;
		case FYECF_MODE_JSON_COMPACT:
			emit_flags |= FYOPEF_MODE_JSON;
			emit_flags |= FYOPEF_STYLE_COMPACT;
			break;
		}

		switch (gcfg->emit_xcfg_flags & (FYEXCF_COLOR_MASK << FYEXCF_COLOR_SHIFT)) {
		case FYEXCF_COLOR_AUTO:
			emit_flags |= FYOPEF_COLOR_AUTO;
			break;
		case FYEXCF_COLOR_NONE:
			emit_flags |= FYOPEF_COLOR_NONE;
			break;
		case FYEXCF_COLOR_FORCE:
			emit_flags |= FYOPEF_COLOR_FORCE;
			break;
		}

		if (gcfg->emit_cfg_flags & FYECF_NO_ENDING_NEWLINE)
			emit_flags |= FYOPEF_NO_ENDING_NEWLINE;

		if (!gcfg->null_output) {

			if (!gcfg->testsuite && !gcfg->parse_dump) {

				if (!gcfg->dump_primitives)
					fy_emit(gb, v, emit_flags | FYOPEF_OUTPUT_TYPE_STDOUT, NULL);
				else
					fy_generic_dump_primitive(stdout, 0, v);
			} else {
				/* Create iterator */
				memset(&cfg, 0, sizeof(cfg));
				cfg.flags = FYGICF_WANT_STREAM_DOCUMENT_BODY_EVENTS |
					    FYGICF_HAS_FULL_DIRECTORY;
				cfg.vdir = v;
				fygi = fy_generic_iterator_create_cfg(&cfg);
				if (!fygi)
					goto err_out;

				while ((fye = fy_generic_iterator_generate_next(fygi)) != NULL) {
					if (gcfg->testsuite)
						dump_testsuite_event(fye, dump_flags);
					else if (gcfg->parse_dump)
						dump_parse_event(fye, !!(dump_flags & DTEF_COLORIZE));
					fy_generic_iterator_event_free(fygi, fye);
				}

				fy_generic_iterator_destroy(fygi);
			}

		}

	} while (++i < argc);

	if (had_error) {
		fy_generic vdiag = fy_generic_get_diag(v);
		fy_generic_emit_default(vdiag);
	}

	rc = !had_error ? 0 : -1;
out:
	fy_generic_builder_destroy(gb);
	fy_allocator_destroy(allocator);

	return rc;

err_out:
	rc = -1;
	goto out;
}

#endif /* HAVE_GENERIC */

int main(int argc, char *argv[])
{
	struct fy_parse_cfg cfg = {
		.search_path = INCLUDE_DEFAULT,
		.flags =
			(QUIET_DEFAULT			 ? FYPCF_QUIET : 0) |
			(RESOLVE_DEFAULT		 ? FYPCF_RESOLVE_DOCUMENT : 0) |
			(DISABLE_ACCEL_DEFAULT		 ? FYPCF_DISABLE_ACCELERATORS : 0) |
			(DISABLE_BUFFERING_DEFAULT	 ? FYPCF_DISABLE_BUFFERING : 0) |
			(DISABLE_DEPTH_LIMIT_DEFAULT	 ? FYPCF_DISABLE_DEPTH_LIMIT : 0) |
			(SLOPPY_FLOW_INDENTATION_DEFAULT ? FYPCF_SLOPPY_FLOW_INDENTATION : 0) |
			(PREFER_RECURSIVE_DEFAULT	 ? FYPCF_PREFER_RECURSIVE : 0) |
			(YPATH_ALIASES_DEFAULT           ? FYPCF_YPATH_ALIASES : 0),
	};
	struct fy_emitter_xcfg emit_xcfg;
	struct fy_parser *fyp = NULL;
	struct fy_emitter *emit = NULL;
	int rc, exitcode = EXIT_FAILURE, opt, lidx, i, j, step = 1;
	enum fy_error_module errmod;
	unsigned int errmod_mask;
	bool show;
	int indent = INDENT_DEFAULT;
	int width = WIDTH_DEFAULT;
	bool manual_width = false;
	bool follow = FOLLOW_DEFAULT;
	const char *to = TO_DEFAULT;
	const char *from = FROM_DEFAULT;
	const char *color = COLOR_DEFAULT;
	const char *file = NULL, *trim = TRIM_DEFAULT;
	char *tmp, *s, *progname;
	struct fy_document *fyd, *fyd_join = NULL;
	enum fy_emitter_cfg_flags emit_flags = 0;
	enum fy_emitter_xcfg_flags emit_xflags = 0;
	struct fy_node *fyn, *fyn_emit, *fyn_to, *fyn_from;
	int count_ins = 0;
	struct fy_document **fyd_ins = NULL;
	int tool_mode = OPT_TOOL;
	struct fy_event *fyev;
	struct fy_event *fyeev;
	const char *eevtext;
	size_t eevlen;
	struct fy_tag **tags;
	struct fy_token *fyt;
	bool join_resolve = RESOLVE_DEFAULT;
	struct fy_token_iter *iter;
	bool streaming = STREAMING_DEFAULT;
	bool recreating = RECREATING_DEFAULT;
	struct fy_diag_cfg dcfg;
	struct fy_diag *diag = NULL;
	struct fy_path_parse_cfg pcfg;
	struct fy_path_expr *expr = NULL;
	struct fy_path_exec_cfg xcfg;
	struct fy_path_exec *fypx = NULL;
	struct fy_node *fyn_start;
	bool dump_pathexpr = false;
	bool noexec = false;
	bool null_output = false;
	bool dry_run = false;
	bool stdin_input;
	void *res_iter;
	bool disable_flow_markers = DISABLE_FLOW_MARKERS_DEFAULT;
	bool disable_doc_markers = DISABLE_DOC_MARKERS_DEFAULT;
	bool disable_scalar_styles = DISABLE_SCALAR_STYLES_DEFAULT;
	bool document_event_stream = DOCUMENT_EVENT_STREAM_DEFAULT;
	bool collect_errors = COLLECT_ERRORS_DEFAULT;
	bool allow_duplicate_keys = ALLOW_DUPLICATE_KEYS_DEFAULT;
	bool tsv_format = TSV_FORMAT_DEFAULT;
	struct composer_data cd;
	bool dump_path = DUMP_PATH_DEFAULT;
	const char *input_arg;
	enum dump_testsuite_event_flags dump_flags;
	/* b3sum */
	int opti;
	struct b3sum_config b3cfg = default_b3sum_cfg;
#ifdef HAVE_GENERIC
	/* generic */
	struct generic_config gcfg = default_generic_cfg;
	bool dedup = DEDUP_DEFAULT;
	bool dump_primitives = false;
	bool create_markers = false;
	bool pyyaml_compat = false;
	bool keep_style = false;
#endif
#ifdef HAVE_REFLECTION
	/* reflection */
	struct fy_reflection *rfl = NULL;
	const char *cflags = "";
	const char *import_blob = NULL;
	const char *generate_blob = NULL;
	bool generate_c = false, no_prune_system = false, packed_roundtrip = false;
	const char *type_include = NULL, *type_exclude = NULL;
	const char *import_c_file = NULL;
	const char *entry_type = NULL;
	const char *entry_meta = NULL;
	struct fy_type_context_cfg ctx_cfg = { 0 };
	struct fy_type_context *ctx = NULL;
	void *rd_data = NULL;
	bool emitted_ss;
#endif


	fy_valgrind_check(&argc, &argv);

#ifdef _WIN32
	/* On Windows, set stdin/stdout to binary mode to prevent CRLF conversion */
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);

	/* Enable ANSI escape sequence processing for colored output.
	 * Skip this on Wine - the host terminal handles ANSI natively and
	 * enabling VT processing causes Wine to interpret sequences internally. */
	if (!GetModuleHandleA("ntdll.dll") ||
	    !GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version")) {
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
		DWORD dwMode = 0;
		if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode)) {
			dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
			SetConsoleMode(hOut, dwMode);
		}
		if (hErr != INVALID_HANDLE_VALUE && GetConsoleMode(hErr, &dwMode)) {
			dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
			SetConsoleMode(hErr, dwMode);
		}
	}
#endif

	/* select the appropriate tool mode */
	progname = strrchr(argv[0], '/');
#ifdef _WIN32
	/* On Windows, also check for backslash */
	{
		char *p = strrchr(argv[0], '\\');
		if (p && (!progname || p > progname))
			progname = p;
	}
#endif
	if (!progname)
		progname = argv[0];
	else
		progname++;

#ifdef _WIN32
	/* On Windows, strip .exe extension for comparison */
	{
		static char progname_buf[256];
		size_t len = strlen(progname);
		if (len > 4 && !_stricmp(progname + len - 4, ".exe")) {
			if (len - 4 < sizeof(progname_buf)) {
				memcpy(progname_buf, progname, len - 4);
				progname_buf[len - 4] = '\0';
				progname = progname_buf;
			}
		}
	}
#endif

	/* default mode is dump */
	if (!strcmp(progname, "fy-filter"))
		tool_mode = OPT_FILTER;
	else if (!strcmp(progname, "fy-testsuite"))
		tool_mode = OPT_TESTSUITE;
	else if (!strcmp(progname, "fy-dump"))
		tool_mode = OPT_DUMP;
	else if (!strcmp(progname, "fy-join"))
		tool_mode = OPT_JOIN;
	else if (!strcmp(progname, "fy-ypath"))
		tool_mode = OPT_YPATH;
	else if (!strcmp(progname, "fy-scan-dump"))
		tool_mode = OPT_SCAN_DUMP;
	else if (!strcmp(progname, "fy-parse-dump"))
		tool_mode = OPT_PARSE_DUMP;
	else if (!strcmp(progname, "fy-compose"))
		tool_mode = OPT_COMPOSE;
	else if (!strcmp(progname, "fy-yaml-version-dump"))
		tool_mode = OPT_YAML_VERSION_DUMP;
	else if (!strcmp(progname, "fy-b3sum"))
		tool_mode = OPT_B3SUM;
#ifdef HAVE_REFLECTION
	else if (!strcmp(progname, "fy-reflect"))
		tool_mode = OPT_REFLECT;
#endif
#ifdef HAVE_GENERIC
	else if (!strcmp(progname, "fy-generic"))
		tool_mode = OPT_GENERIC;
	else if (!strcmp(progname, "fy-generic-testsuite"))
		tool_mode = OPT_GENERIC_TESTSUITE;
#endif
	else
		tool_mode = OPT_TOOL;

	fy_diag_cfg_default(&dcfg);
	/* XXX remember to modify this if you change COLOR_DEFAULT */

	emit_flags = (SORT_DEFAULT ? FYECF_SORT_KEYS : 0) |
		     (COMMENT_DEFAULT ? FYECF_OUTPUT_COMMENTS : 0) |
		     (STRIP_LABELS_DEFAULT ? FYECF_STRIP_LABELS : 0) |
		     (STRIP_TAGS_DEFAULT ? FYECF_STRIP_TAGS : 0) |
		     (STRIP_DOC_DEFAULT ? FYECF_STRIP_DOC : 0);
	apply_mode_flags(MODE_DEFAULT, &emit_flags);

	emit_xflags = (VISIBLE_DEFAULT ? FYEXCF_VISIBLE_WS : 0) |
		      (!strcmp(COLOR_DEFAULT, "auto") ? FYEXCF_COLOR_AUTO :
		       !strcmp(COLOR_DEFAULT, "on") ? FYEXCF_COLOR_FORCE :
		       FYEXCF_COLOR_NONE) |
		      FYEXCF_OUTPUT_STDOUT;

	while ((opt = getopt_long(argc, argv,
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
			if (indent <= 0 || indent > FYECF_INDENT_MASK) {
				fprintf(stderr, "bad indent option %s\n", optarg);
				goto err_out_usage;
			}

			break;
		case 'w':
			if (!strcmp(optarg, "inf")) {
				width = 0;	/* infinite */
			} else {
				width = atoi(optarg);
				if (width <= 8 || width > FYECF_WIDTH_MASK) {			/* it should fit %YAML 1.3 at least */
					fprintf(stderr, "bad width option %s\n", optarg);
					goto err_out_usage;
				}
			}
			manual_width = true;
			break;
		case 'd':
			dcfg.level = fy_string_to_error_type(optarg);
			if (dcfg.level == FYET_MAX) {
				fprintf(stderr, "bad debug level option %s\n", optarg);
				goto err_out_usage;
			}
			break;
		case OPT_DISABLE_DIAG:
		case OPT_ENABLE_DIAG:
			if (!strcmp(optarg, "all")) {
				errmod_mask = FY_BIT(FYEM_MAX) - 1;
			} else {
				errmod = fy_string_to_error_module(optarg);
				if (errmod == FYEM_MAX) {
					fprintf(stderr, "bad error module option %s\n", optarg);
					goto err_out_usage;
				}
				errmod_mask = FY_BIT(errmod);
			}
			if (opt == OPT_DISABLE_DIAG)
				dcfg.module_mask &= ~errmod_mask;
			else
				dcfg.module_mask |= errmod_mask;
			break;

		case OPT_SHOW_DIAG:
		case OPT_HIDE_DIAG:
			show = opt == OPT_SHOW_DIAG;
			if (!strcmp(optarg, "source")) {
				dcfg.show_source = show;
			} else if (!strcmp(optarg, "position")) {
				dcfg.show_position = show;
			} else if (!strcmp(optarg, "type")) {
				dcfg.show_type = show;
			} else if (!strcmp(optarg, "module")) {
				dcfg.show_module = show;
			} else {
				fprintf(stderr, "bad %s option %s\n",
						show ? "show" : "hide", optarg);
				goto err_out_usage;
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
			if (!strcmp(color, "auto")) {
				dcfg.colorize = isatty(fileno(stderr)) == 1;
				emit_xflags &= ~(FYEXCF_COLOR_MASK << FYEXCF_COLOR_SHIFT);
				emit_xflags |= FYEXCF_COLOR_AUTO;
			}
			else if (!strcmp(color, "yes") || !strcmp(color, "1") || !strcmp(color, "on")) {
				dcfg.colorize = true;
				emit_xflags &= ~(FYEXCF_COLOR_MASK << FYEXCF_COLOR_SHIFT);
				emit_xflags |= FYEXCF_COLOR_FORCE;
			} else if (!strcmp(color, "no") || !strcmp(color, "0") || !strcmp(color, "off")) {
				dcfg.colorize = false;
				emit_xflags &= ~(FYEXCF_COLOR_MASK << FYEXCF_COLOR_SHIFT);
				emit_xflags |= FYEXCF_COLOR_NONE;
			} else {
				fprintf(stderr, "bad color option %s\n", optarg);
				goto err_out_usage;
			}
			break;
		case 'm':
			rc = apply_mode_flags(optarg, &emit_flags);
			if (rc) {
				fprintf(stderr, "bad mode option %s\n", optarg);
				goto err_out_usage;
			}
			break;
		case 'V':
			emit_xflags |= FYEXCF_VISIBLE_WS;
			break;
		case 'l':
			follow = true;
			break;
		case 'q':
			cfg.flags |= FYPCF_QUIET;
			dcfg.output_fn = no_diag_output_fn;
			dcfg.fp = NULL;
			dcfg.colorize = false;
			b3cfg.quiet = true;
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
		case OPT_YPATH:
		case OPT_SCAN_DUMP:
		case OPT_PARSE_DUMP:
		case OPT_COMPOSE:
		case OPT_YAML_VERSION_DUMP:
		case OPT_B3SUM:
#ifdef HAVE_REFLECTION
		case OPT_REFLECT:
#endif
#ifdef HAVE_GENERIC
		case OPT_GENERIC:
		case OPT_GENERIC_TESTSUITE:
		case OPT_GENERIC_PARSE_DUMP:
#endif
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
		case OPT_NO_STREAMING:
			streaming = false;
			break;
		case OPT_RECREATING:
			recreating = true;
			break;
		case OPT_DUMP_PATH:
			dump_path = true;
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
				goto err_out_usage;
			}
			break;
		case OPT_DISABLE_ACCEL:
			cfg.flags |= FYPCF_DISABLE_ACCELERATORS;
			break;
		case OPT_DISABLE_BUFFERING:
			cfg.flags |= FYPCF_DISABLE_BUFFERING;
			break;
		case OPT_DISABLE_DEPTH_LIMIT:
			cfg.flags |= FYPCF_DISABLE_DEPTH_LIMIT;
			break;
		case OPT_DISABLE_MMAP:
			cfg.flags |= FYPCF_DISABLE_MMAP_OPT;
			b3cfg.no_mmap = true;
			break;
		case OPT_DUMP_PATHEXPR:
			dump_pathexpr = true;
			break;
		case OPT_NOEXEC:
			noexec = true;
			break;
		case OPT_NULL_OUTPUT:
			null_output = true;
			emit_xflags |= FYEXCF_NULL_OUTPUT;
			break;
		case OPT_NO_ENDING_NEWLINE:
			emit_flags |= FYECF_NO_ENDING_NEWLINE;
			break;
		case OPT_PRESERVE_FLOW_LAYOUT:
			emit_xflags |= FYEXCF_PRESERVE_FLOW_LAYOUT;
			break;
		case OPT_INDENTED_SEQ_IN_MAP:
			emit_xflags |= FYEXCF_INDENTED_SEQ_IN_MAP;
			break;
		case OPT_YAML_1_1:
			cfg.flags &= ~(FYPCF_DEFAULT_VERSION_MASK << FYPCF_DEFAULT_VERSION_SHIFT);
			cfg.flags |= FYPCF_DEFAULT_VERSION_1_1;
			break;
		case OPT_YAML_1_2:
			cfg.flags &= ~(FYPCF_DEFAULT_VERSION_MASK << FYPCF_DEFAULT_VERSION_SHIFT);
			cfg.flags |= FYPCF_DEFAULT_VERSION_1_2;
			break;
		case OPT_YAML_1_3:
			cfg.flags &= ~(FYPCF_DEFAULT_VERSION_MASK << FYPCF_DEFAULT_VERSION_SHIFT);
			cfg.flags |= FYPCF_DEFAULT_VERSION_1_3;
			break;
		case OPT_SLOPPY_FLOW_INDENTATION:
			cfg.flags |= FYPCF_SLOPPY_FLOW_INDENTATION;
			break;
		case OPT_PREFER_RECURSIVE:
			cfg.flags |= FYPCF_PREFER_RECURSIVE;
			break;
		case OPT_YPATH_ALIASES:
			cfg.flags |= FYPCF_YPATH_ALIASES;
			break;
		case OPT_DISABLE_FLOW_MARKERS:
			disable_flow_markers = true;
			break;
		case OPT_DISABLE_DOC_MARKERS:
			disable_doc_markers = true;
			break;
		case OPT_DISABLE_SCALAR_STYLES:
			disable_scalar_styles = true;
			break;
		case OPT_DOCUMENT_EVENT_STREAM:
			document_event_stream = true;
			break;
		case OPT_COLLECT_ERRORS:
			collect_errors = true;
			break;
		case OPT_ALLOW_DUPLICATE_KEYS:
			allow_duplicate_keys = true;
			break;
		case OPT_STRIP_EMPTY_KV:
			emit_flags |= FYECF_STRIP_EMPTY_KV;
			break;
		case OPT_TSV_FORMAT:
			tsv_format = true;
			break;
#ifdef HAVE_REFLECTION
		case OPT_GENERATE_BLOB:
			generate_blob = optarg;
			break;
		case OPT_IMPORT_BLOB:
			import_blob = optarg;
			break;
		case OPT_GENERATE_C:
			generate_c = true;
			break;
		case OPT_NO_PRUNE_SYSTEM:
			no_prune_system = true;
			break;
		case OPT_CFLAGS:
			cflags = optarg;
			break;
		case OPT_TYPE_INCLUDE:
			type_include = optarg;
			break;
		case OPT_TYPE_EXCLUDE:
			type_exclude = optarg;
			break;
		case OPT_IMPORT_C_FILE:
			import_c_file = optarg;
			break;
		case OPT_ENTRY_TYPE:
			entry_type = optarg;
			break;
		case OPT_ENTRY_META:
			entry_meta = optarg;
			break;
		case OPT_PACKED_ROUNDTRIP:
			packed_roundtrip = true;
			break;
		case OPT_DRY_RUN:
			dry_run = true;
			break;
		case OPT_DUMP_REFLECTION:
			ctx_cfg.flags |= FYTCCF_DUMP_REFLECTION | FYTCCF_DUMP_TYPE_SYSTEM;
			break;
		case OPT_DEBUG_REFLECTION:
			ctx_cfg.flags |= FYTCCF_DEBUG;
			break;
		case OPT_STRICT_ANNOTATIONS:
			ctx_cfg.flags |= FYTCCF_STRICT_ANNOTATIONS;
			break;
#endif

		case OPT_DERIVE_KEY:
			b3cfg.derive_key = true;
			b3cfg.context = optarg;
			b3cfg.context_len = strlen(optarg);
			break;
		case OPT_NO_NAMES:
			b3cfg.no_names = true;
			break;
		case OPT_RAW:
			b3cfg.raw = true;
			break;
		case OPT_CHECK:
			b3cfg.check = true;
			break;
		case OPT_KEYED:
			b3cfg.keyed = true;
			break;

		case OPT_LENGTH:
			opti = atoi(optarg);
			if (opti <= 0 || opti > FY_BLAKE3_OUT_LEN) {
				fprintf(stderr, "Error: bad length=%d (must be > 0 and <= %u)\n\n", opti, FY_BLAKE3_OUT_LEN);
				goto err_out_usage;
			}
			b3cfg.length = (unsigned int)opti;
			break;

		case OPT_LIST_BACKENDS:
			b3cfg.list_backends = true;
			break;

		case OPT_BACKEND:
			b3cfg.backend = optarg;
			break;

		case OPT_NUM_THREADS:
			b3cfg.num_threads = atoi(optarg);
			break;

		case OPT_FILE_BUFFER:
			opti = atoi(optarg);
			if (opti < 0) {
				fprintf(stderr, "Error: bad file-buffer=%d (must be >= 0)\n\n", opti);
				goto err_out_usage;
			}
			b3cfg.file_buffer = (size_t)opti;
			break;

		case OPT_MMAP_MIN_CHUNK:
			opti = atoi(optarg);
			if (opti < 0) {
				fprintf(stderr, "Error: bad mmap-min-chunk=%d (must be >= 0)\n\n", opti);
				goto err_out_usage;
			}
			b3cfg.mmap_min_chunk = (size_t)opti;
			break;

		case OPT_MMAP_MAX_CHUNK:
			opti = atoi(optarg);
			if (opti < 0) {
				fprintf(stderr, "Error: bad mmap-max-chunk=%d (must be >= 0)\n\n", opti);
				goto err_out_usage;
			}
			b3cfg.mmap_max_chunk = (size_t)opti;
			break;

#ifdef HAVE_GENERIC
		case OPT_DEDUP:
			dedup = true;
			break;

		case OPT_NO_DEDUP:
			dedup = false;
			break;

		case OPT_DUMP_PRIMITIVES:
			dump_primitives = true;
			break;

		case OPT_CREATE_MARKERS:
			create_markers = true;
			break;

		case OPT_PYYAML_COMPAT:
			pyyaml_compat = true;
			break;

		case OPT_KEEP_STYLE:
			keep_style = true;
			break;
#endif /* HAVE_GENERIC */

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

	if (tool_mode == OPT_B3SUM) {

		rc = do_b3sum(argc, argv, optind, &b3cfg);
		if (rc == 1) {
			/* display usage */
			goto err_out_usage;
		}
		exitcode = !rc ? EXIT_SUCCESS : EXIT_FAILURE;
		goto cleanup;
	}

	if (tool_mode == OPT_YAML_VERSION_DUMP) {
		const struct fy_version *vers;
		void *iter;

		vers = fy_version_default();
		printf("Default version    : %d.%d\n", vers->major, vers->minor);

		printf("Supported versions :");
		iter = NULL;
		while ((vers = fy_version_supported_iterate(&iter)) != NULL)
			printf(" %d.%d", vers->major, vers->minor);
		printf("\n");
	}

	/* if we're still in tool mode, switch to dump */
	if (tool_mode == OPT_TOOL)
		tool_mode = OPT_DUMP;

	/* as a special case for join, we resolve the document once */
	if (tool_mode == OPT_JOIN) {
		join_resolve = !!(cfg.flags & FYPCF_RESOLVE_DOCUMENT);
		cfg.flags &= ~FYPCF_RESOLVE_DOCUMENT;
	}

	/* create common diagnostic object */
	diag = fy_diag_create(&dcfg);
	if (!diag) {
		fprintf(stderr, "fy_diag_create() failed\n");
		goto cleanup;
	}

	/* collect errors, instead of outputting directly */
	if (collect_errors)
		fy_diag_set_collect_errors(diag, true);

	if (allow_duplicate_keys)
		cfg.flags |= FYPCF_ALLOW_DUPLICATE_KEYS;

	/* all set, use fy_diag for error reporting, debugging now */

	cfg.diag = diag;
	fyp = fy_parser_create(&cfg);
	if (!fyp) {
		fprintf(stderr, "fy_parser_create() failed\n");
		goto cleanup;
	}

	exitcode = EXIT_FAILURE;

	if (tool_mode != OPT_TESTSUITE) {
		enum fy_emitter_cfg_flags emit_width_flags;

		/* if we're dumping to a non tty stdout width is infinite */
		if (tool_mode == OPT_DUMP && !isatty(fileno(stdout)) && !manual_width)
			emit_width_flags = FYECF_WIDTH_INF;
		else if (width > 0)
			emit_width_flags = FYECF_WIDTH(width);
		else
			emit_width_flags = FYECF_WIDTH_INF;

		memset(&emit_xcfg, 0, sizeof(emit_xcfg));
		emit_xcfg.cfg.flags = emit_flags | emit_width_flags |
				FYECF_INDENT(indent) |
				FYECF_EXTENDED_CFG;	// use extended config

		/* unconditionally turn on document start markers for ypath */
		if (tool_mode == OPT_YPATH)
			emit_xcfg.cfg.flags |= FYECF_DOC_START_MARK_ON;

		emit_xcfg.xflags = emit_xflags;

		emit = fy_emitter_create(&emit_xcfg.cfg);
		if (!emit) {
			fprintf(stderr, "fy_emitter_create() failed\n");
			goto cleanup;
		}

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

		dump_flags = (dcfg.colorize && isatty(fileno(stdout)) ? DTEF_COLORIZE : 0) |
			     (disable_flow_markers ? DTEF_DISABLE_FLOW_MARKERS : 0) |
			     (disable_doc_markers ? DTEF_DISABLE_DOC_MARKERS : 0) |
			     (disable_scalar_styles ? DTEF_DISABLE_SCALAR_STYLES : 0) |
			     (tsv_format ? DTEF_TSV_FORMAT : 0);

		if (!document_event_stream) {
			/* regular test suite */
			while ((fyev = fy_parser_parse(fyp)) != NULL) {
				dump_testsuite_event(fyev, dump_flags);
				fy_parser_event_free(fyp, fyev);
			}
		} else {
			struct fy_document_iterator *fydi;

			fydi = fy_document_iterator_create();
			assert(fydi);

			fyev = fy_document_iterator_stream_start(fydi);
			if (!fyev) {
				fprintf(stderr, "failed to create document iterator's stream start event\n");
				goto cleanup;
			}
			dump_testsuite_event(fyev, dump_flags);
			fy_document_iterator_event_free(fydi, fyev);

			/* convert to document and then process the generator event stream it */
			while ((fyd = fy_parse_load_document(fyp)) != NULL) {

				fyev = fy_document_iterator_document_start(fydi, fyd);
				if (!fyev) {
					fprintf(stderr, "failed to create document iterator's document start event\n");
					goto cleanup;
				}
				dump_testsuite_event(fyev, dump_flags);
				fy_document_iterator_event_free(fydi, fyev);

				while ((fyev = fy_document_iterator_body_next(fydi)) != NULL) {
					dump_testsuite_event(fyev, dump_flags);
					fy_document_iterator_event_free(fydi, fyev);
				}

				fyev = fy_document_iterator_document_end(fydi);
				if (!fyev) {
					fprintf(stderr, "failed to create document iterator's stream document end\n");
					goto cleanup;
				}
				dump_testsuite_event(fyev, dump_flags);
				fy_document_iterator_event_free(fydi, fyev);

				fy_parse_document_destroy(fyp, fyd);
				if (rc)
					break;

			}

			fyev = fy_document_iterator_stream_end(fydi);
			if (!fyev) {
				fprintf(stderr, "failed to create document iterator's stream end event\n");
				goto cleanup;
			}
			dump_testsuite_event(fyev, dump_flags);
			fy_document_iterator_event_free(fydi, fyev);

			fy_document_iterator_destroy(fydi);
			fydi = NULL;
		}

		fy_token_iter_destroy(iter);
		iter = NULL;

		if (fy_parser_get_stream_error(fyp))
			goto cleanup;
		break;

	case OPT_DUMP:
		for (i = optind; ; i++) {

			if (optind < argc) {
				if (i >= argc)
					break;
				input_arg = argv[i];
			} else {
				if (i >= argc + 1)
					break;
				input_arg = "-";
			}

			rc = set_parser_input(fyp, input_arg, false);
			if (rc) {
				fprintf(stderr, "failed to set parser input to '%s' for dump\n", input_arg);
				goto cleanup;
			}

			if (!streaming) {
				while ((fyd = fy_parse_load_document(fyp)) != NULL) {
					if (!null_output)
						rc = fy_emit_document(emit, fyd);
					else
						rc = 0;
					fy_parse_document_destroy(fyp, fyd);
					if (rc)
						goto cleanup;

				}
			} else {
				while ((fyev = fy_parser_parse(fyp)) != NULL) {
					if (!null_output) {
						if (recreating) {
							fyeev = NULL;
							switch (fyev->type) {
								case FYET_STREAM_START:
								case FYET_STREAM_END:
								case FYET_MAPPING_END:
								case FYET_SEQUENCE_END:
									fyeev = fy_emit_event_create(emit, fyev->type);
									break;
								case FYET_DOCUMENT_START:
									tags = fy_document_state_tag_directives(fyev->document_start.document_state);
									fyeev = fy_emit_event_create(emit, FYET_DOCUMENT_START,
												fyev->document_start.implicit,
												fy_document_state_version_explicit(fyev->document_start.document_state)
													? fy_document_state_version(fyev->document_start.document_state)
													: NULL,
												fy_document_state_tags_explicit(fyev->document_start.document_state)
													? tags
													: NULL);
									if (tags)
										free(tags);
									break;
								case FYET_DOCUMENT_END:
									fyeev = fy_emit_event_create(emit, FYET_DOCUMENT_END,
												fyev->document_end.implicit);
									break;
								case FYET_MAPPING_START:
								case FYET_SEQUENCE_START:
									fyeev = fy_emit_event_create(emit, fyev->type,
												fy_event_get_node_style(fyev),
												fy_event_get_anchor_token(fyev)
													? fy_token_get_text0(fy_event_get_anchor_token(fyev))
													: NULL,
												fy_event_get_tag_token(fyev)
													? fy_tag_token_short0(fy_event_get_tag_token(fyev))
													: NULL);
									break;
								case FYET_SCALAR:
									eevlen = 0;
									eevtext = fy_token_get_text(fy_event_get_token(fyev), &eevlen);
									if (!eevtext)
										goto cleanup;
									fyeev = fy_emit_event_create(emit, FYET_SCALAR,
											fy_scalar_token_get_style(fy_event_get_token(fyev)),
												eevtext, eevlen,
												fy_event_get_anchor_token(fyev)
													? fy_token_get_text0(fy_event_get_anchor_token(fyev))
													: NULL,
												fy_event_get_tag_token(fyev)
													? fy_tag_token_short0(fy_event_get_tag_token(fyev))
													: NULL);
									break;
								case FYET_ALIAS:
									fyeev = fy_emit_event_create(emit, FYET_ALIAS,
												fy_token_get_text0(fy_event_get_token(fyev)));
									break;
								default:
									goto cleanup;
							}
							fy_parser_event_free(fyp, fyev);
							if (fyeev == NULL) {
								goto cleanup;
							}

							rc = fy_emit_event(emit, fyeev);
						} else {
							rc = fy_emit_event_from_parser(emit, fyp, fyev);
						}

						if (rc)
							goto cleanup;
					} else {
						fy_parser_event_free(fyp, fyev);
					}
				}
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

				rc = fy_emit_document_start(emit, fyd, fyn_emit);
				if (rc)
					goto cleanup;

				rc = fy_emit_root_node(emit, fyn_emit);
				if (rc)
					goto cleanup;

				rc = fy_emit_document_end(emit);
				if (rc)
					goto cleanup;

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
		for (i = optind; ; i++) {

			if (optind < argc) {
				if (i >= argc)
					break;
				input_arg = argv[i];
			} else {
				if (i >= argc + 1)
					break;
				input_arg = "-";
			}

			rc = set_parser_input(fyp, input_arg, false);
			if (rc) {
				fprintf(stderr, "failed to set parser input to '%s' for join\n", input_arg);
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

		rc = fy_emit_document_start(emit, fyd_join, fyn_emit);
		if (rc)
			goto cleanup;

		rc = fy_emit_root_node(emit, fyn_emit);
		if (rc)
			goto cleanup;

		rc = fy_emit_document_end(emit);
		if (rc)
			goto cleanup;

		break;

	case OPT_YPATH:
		step = 1;
		if ((argc - optind) < 1) {
			fprintf(stderr, "missing path expression\n");
			goto cleanup;
		}

		memset(&pcfg, 0, sizeof(pcfg));
		pcfg.diag = diag;

		i = optind++;
		expr = fy_path_expr_build_from_string(&pcfg, argv[i], FY_NT);
		if (!expr) {
			fprintf(stderr, "failed to parse path expression %s\n", argv[i]);
			goto cleanup;
		}

		if (dump_pathexpr) {
			struct fy_document *fyd_pe;

			fy_path_expr_dump(expr, diag, FYET_ERROR, 0, "ypath expression:");

			fyd_pe = fy_path_expr_to_document(expr);
			if (!fyd_pe) {
				fprintf(stderr, "failed to convert path expression to document\n");
				goto cleanup;
			}
			fy_emit_document(emit, fyd_pe);

			fy_document_destroy(fyd_pe);
		}

		/* nothing more */
		if (noexec) {
			exitcode = EXIT_SUCCESS;
			goto cleanup;
		}

		memset(&xcfg, 0, sizeof(xcfg));
		xcfg.diag = diag;

		fypx = fy_path_exec_create(&xcfg);
		if (!fypx) {
			fprintf(stderr, "failed to create a path executor\n");
			goto cleanup;
		}

		/* if no more arguments use stdin */
		if (optind >= argc) {
			rc = fy_parser_set_input_fp(fyp, "stdin", stdin);
			if (rc) {
				fprintf(stderr, "failed to set parser input to %s for ypath\n",
						"stdin");
				goto cleanup;
			}
			stdin_input = true;
		} else
			stdin_input = false;

		for (;;) {

			if (!stdin_input) {
				i = optind++;
				rc = fy_parser_set_input_file(fyp, argv[i]);
				if (rc) {
					fprintf(stderr, "failed to set parser input to %s for ypath\n",
							argv[i]);
					goto cleanup;
				}
			}

			fy_path_exec_reset(fypx);

			while ((fyd = fy_parse_load_document(fyp)) != NULL) {

				fyn_start = fy_node_by_path(fy_document_root(fyd), from, FY_NT,
							follow ? FYNWF_FOLLOW : FYNWF_DONT_FOLLOW);

				/* if from is not found, then it's a null document */
				if (!fyn_start) {
					if (!(cfg.flags & FYPCF_QUIET))
						fprintf(stderr, "filter: could not find starting point'%s'\n",
								from);
					continue;
				}

				rc = fy_path_exec_execute(fypx, expr, fyn_start);
				if (rc) {
					fprintf(stderr, "failed to fy_path_exec_execute() - %d\n", rc);
					goto cleanup;
				}

				res_iter = NULL;
				while ((fyn_emit = fy_path_exec_results_iterate(fypx, &res_iter)) != NULL) {

					rc = fy_emit_document_start(emit, fyd, fyn_emit);
					if (rc)
						goto cleanup;

					rc = fy_emit_root_node(emit, fyn_emit);
					if (rc)
						goto cleanup;

					rc = fy_emit_document_end(emit);
					if (rc)
						goto cleanup;
				}

				fy_path_exec_reset(fypx);

				fy_parse_document_destroy(fyp, fyd);
			}

			if (optind >= argc)
				break;

		}

		if (fy_parser_get_stream_error(fyp))
			goto cleanup;
		break;

	case OPT_SCAN_DUMP:
	case OPT_PARSE_DUMP:
		if (optind >= argc) {
			fprintf(stderr, "missing yaml file to %s-dump\n",
					tool_mode == OPT_SCAN_DUMP ? "scan" : "dump");
			goto cleanup;
		}

		for (i = optind; i < argc; i++) {
			rc = set_parser_input(fyp, argv[i], false);
			if (rc) {
				fprintf(stderr, "failed to set parser input to '%s' for dump\n", argv[i]);
				goto cleanup;
			}

			if (tool_mode == OPT_SCAN_DUMP) {
				while ((fyt = fy_scan(fyp)) != NULL) {
					dump_scan_token(fyt, dcfg.colorize);
					fy_scan_token_free(fyp, fyt);
				}
			} else {
				while ((fyev = fy_parser_parse(fyp)) != NULL) {
					dump_parse_event(fyev, dcfg.colorize);
					fy_parser_event_free(fyp, fyev);
				}
			}

			if (fy_parser_get_stream_error(fyp))
				goto cleanup;
		}
		break;

	case OPT_COMPOSE:
		if (optind >= argc) {
			fprintf(stderr, "missing yaml file to dump\n");
			goto cleanup;
		}

		memset(&cd, 0, sizeof(cd));
		cd.fyp = fyp;
		cd.emit = emit;
		cd.null_output = null_output;
		cd.single_document = false;
		cd.verbose = dump_path;

		for (i = optind; i < argc; i++) {
			rc = set_parser_input(fyp, argv[i], false);
			if (rc) {
				fprintf(stderr, "failed to set parser input to '%s' for dump\n", argv[i]);
				goto cleanup;
			}
		}

		/* ignore errors for now */
		rc = fy_parse_compose(fyp, compose_process_event, &cd);

		/* NULL OK */
		fy_document_destroy(cd.fyd);

		if (rc || fy_parser_get_stream_error(fyp))
			goto cleanup;

		break;

#ifdef HAVE_REFLECTION
	case OPT_REFLECT:
		rfl = NULL;
		if (import_blob) {
			rfl = fy_reflection_from_packed_blob_file(import_blob, diag);
			if (!rfl) {
				fprintf(stderr, "unable to get reflection from blob file %s\n", import_blob);
				goto cleanup;
			}

		} else if (import_c_file) {
			file = import_c_file;
			rfl = fy_reflection_from_c_file_with_cflags(file, cflags, true, true, diag);
			if (!rfl) {
				fprintf(stderr, "unable to perform reflection from file %s\n", file);
				goto cleanup;
			}
		} else {
			rfl = fy_reflection_from_null(diag);
			if (!rfl) {
				fprintf(stderr, "unable to get null reflection\n");
				goto cleanup;
			}
		}

		if (!rfl) {
			fprintf(stderr, "No reflection; provide either --import-blob or --import-c-file option\n");
			goto cleanup;
		}

		/* prune all non referenced */
		if (!no_prune_system)
			fy_reflection_prune_system(rfl);

		if (type_include || type_exclude) {
			rc = fy_reflection_type_filter(rfl, type_include, type_exclude);
			if (rc)
				goto cleanup;
		}

		if (packed_roundtrip) {
			void *blob;
			size_t blob_size;
			struct fy_reflection *rfl_packed;
			bool eq;

			blob = fy_reflection_to_packed_blob(rfl, &blob_size, true, false);
			assert(blob);

			rfl_packed = fy_reflection_from_packed_blob(blob, blob_size, diag);
			assert(rfl_packed);

			eq = fy_reflection_equal(rfl, rfl_packed);

			if (!eq) {
				fprintf(stderr, ">>>> REFLECTION A\n");
				fy_reflection_dump(rfl, false, false);
				fprintf(stderr, ">>>> REFLECTION B\n");
				fy_reflection_dump(rfl_packed, false, false);
			}

			fy_reflection_destroy(rfl_packed);
			free(blob);

			if (eq)
				fprintf(stderr, "reflections are identical\n");
			else {
				fprintf(stderr, "reflections are NOT identical\n");
				goto cleanup;
			}

		}

		if (generate_c) {
			rc = fy_reflection_generate_c(rfl, FYCGF_INDENT_TAB | FYCGF_COMMENT_YAML, stdout);
			if (rc < 0) {
				fprintf(stderr, "unable to generate c to stdout\n");
				goto cleanup;
			}
		}

		if (!dry_run) {
			if (!entry_type) {
				fprintf(stderr, "No entry point type; supply an --entry-type\n");
				goto cleanup;
			}

			if (optind >= argc) {
				fprintf(stderr, "missing yaml file to dump\n");
				goto cleanup;
			}

			for (i = optind; i < argc; i++) {
				rc = set_parser_input(fyp, argv[i], false);
				if (rc) {
					fprintf(stderr, "failed to set parser input to '%s' for dump\n", argv[i]);
					goto cleanup;
				}
			}

			ctx_cfg.rfl = rfl;
			ctx_cfg.entry_type = entry_type;
			ctx_cfg.entry_meta = entry_meta;
			ctx_cfg.diag = diag;
			ctx = fy_type_context_create(&ctx_cfg);
			if (!ctx) {
				fprintf(stderr, "fy_type_context_create() failed!\n");
				goto cleanup;
			}

			emitted_ss = false;

			while ((rc = fy_type_context_parse(ctx, fyp, &rd_data)) == 0) {

				rc = fy_type_context_emit(ctx, emit, rd_data,
						FYTCEF_DS | FYTCEF_DE | (!emitted_ss ? FYTCEF_SS : 0));

				fy_type_context_free_data(ctx, rd_data);
				rd_data = NULL;

				if (rc) {
					fprintf(stderr, "fy_type_context_emit() failed\n");
					goto cleanup;
				}
				emitted_ss = true;
			}

			if (emitted_ss) {
				rc = fy_type_context_emit(ctx, emit, NULL, FYTCEF_SE);
				if (rc) {
					fprintf(stderr, "fy_type_context_emit() failed for stream end\n");
					goto cleanup;
				}
			}

			if (rc < 0) {
				fprintf(stderr, "fy_type_context_parse() failed\n");
				goto cleanup;
			}
		}

		if (generate_blob) {
			rc = fy_reflection_to_packed_blob_file(rfl, generate_blob);
			if (rc) {
				fprintf(stderr, "unable to generate blob to file %s\n", generate_blob);
				goto cleanup;
			}
		}

		/* cleanup will take care of rfl cleanup */
		break;
#endif /* HAVE_REFLECTION */

#ifdef HAVE_GENERIC
	case OPT_GENERIC:
	case OPT_GENERIC_TESTSUITE:
	case OPT_GENERIC_PARSE_DUMP:
		gcfg.dedup = dedup;
		gcfg.null_output = null_output;
		gcfg.dump_primitives = dump_primitives;
		gcfg.create_markers = create_markers;
		gcfg.pyyaml_compat = pyyaml_compat;
		gcfg.emit_cfg_flags = emit_flags | FYECF_INDENT(indent);
		gcfg.emit_xcfg_flags = emit_xflags;
		gcfg.parse_cfg_flags = cfg.flags;
		gcfg.testsuite = tool_mode == OPT_GENERIC_TESTSUITE;
		gcfg.parse_dump = tool_mode == OPT_GENERIC_PARSE_DUMP;
		gcfg.diag = diag;
		gcfg.collect_diag = collect_errors;
		gcfg.keep_comments = !!(cfg.flags & FYPCF_PARSE_COMMENTS);
		gcfg.keep_style = keep_style;
		rc = do_generic(argc, argv, optind, &gcfg);
		if (rc == 1) {
			/* display usage */
			goto err_out_usage;
		}
		exitcode = !rc ? EXIT_SUCCESS : EXIT_FAILURE;
		goto cleanup;
#endif /* HAVE_GENERIC */

	}
	exitcode = EXIT_SUCCESS;

cleanup:
#ifdef HAVE_REFLECTION
	if (ctx)
		fy_type_context_destroy(ctx);

	if (rfl)
		fy_reflection_destroy(rfl);
#endif

	if (fypx)
		fy_path_exec_destroy(fypx);

	if (expr)
		fy_path_expr_free(expr);

	if (fyd_join)
		fy_document_destroy(fyd_join);

	if (fyd_ins) {
		for (j = 0; j < count_ins; j++)
			fy_document_destroy(fyd_ins[j]);
	}

	if (emit)
		fy_emitter_destroy(emit);

	if (fyp)
		fy_parser_destroy(fyp);

	if (diag) {
		if (collect_errors) {
			struct fy_diag_error *err;
			void *iter;

			iter = NULL;
			while ((err = fy_diag_errors_iterate(diag, &iter)) != NULL) {
				fprintf(stderr, "%s:%d:%d %s\n", err->file, err->line, err->column, err->msg);
			}

		}
		fy_diag_destroy(diag);
	}

	/* make valgrind happy */
	fy_shutdown();

	return exitcode;

err_out_usage:
	exitcode = EXIT_FAILURE;
	display_usage(stderr, progname, tool_mode);
	goto cleanup;
}
