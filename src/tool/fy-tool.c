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
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <regex.h>
#include <stdalign.h>
#include <inttypes.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>

#include <libfyaml.h>

#include "fy-valgrind.h"
#include "fy-tool-util.h"

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
#define ALLOCATOR_DEFAULT		"default"

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
#define OPT_REFLECT			1011

#define OPT_STRIP_LABELS		2000
#define OPT_STRIP_TAGS			2001
#define OPT_STRIP_DOC			2002
#define OPT_STREAMING			2003
#define OPT_RECREATING          2004
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
#define OPT_ALLOCATOR			2024

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
	{"recreating",		no_argument,		0,	OPT_RECREATING },
	{"disable-accel",	no_argument,		0,	OPT_DISABLE_ACCEL },
	{"disable-buffering",	no_argument,		0,	OPT_DISABLE_BUFFERING },
	{"disable-depth-limit",	no_argument,		0,	OPT_DISABLE_DEPTH_LIMIT },
	{"disable-mmap",	no_argument,		0,	OPT_DISABLE_MMAP },
	{"disable-diag",	required_argument,	0,	OPT_DISABLE_DIAG },
	{"enable-diag", 	required_argument,	0,	OPT_ENABLE_DIAG },
	{"show-diag",		required_argument,	0,	OPT_SHOW_DIAG },
	{"hide-diag", 		required_argument,	0,	OPT_HIDE_DIAG },
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
	{"collect-errors",	no_argument,		0,	OPT_COLLECT_ERRORS },
	{"allow-duplicate-keys",no_argument,		0,	OPT_ALLOW_DUPLICATE_KEYS },
	{"strip-empty-kv",	no_argument,		0,	OPT_STRIP_EMPTY_KV },
	{"tsv-format",		no_argument,		0,	OPT_TSV_FORMAT },
	{"allocator",		required_argument,	0,	OPT_ALLOCATOR },
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
	fprintf(fp, "\t--disable-diag <x>      : Disable diag error module <x>\n");
	fprintf(fp, "\t--enable-diag <x>       : Enable diag error module <x>\n");
	fprintf(fp, "\t--show-diag <x>         : Show diag option <x> (source, position, type, module)\n");
	fprintf(fp, "\t--hide-diag <x>         : Hide diag optione <x> (source, position, type, module)\n");

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
	fprintf(fp, "\t--disable-buffering      : Disable buffering (i.e. no stdio file reads, unix fd instead)"
						" (default %s)\n",
						DISABLE_BUFFERING_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--disable-depth-limit    : Disable depth limit"
						" (default %s)\n",
						DISABLE_DEPTH_LIMIT_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--json, -j               : JSON input mode (no | force | auto)"
						" (default %s)\n",
						JSON_DEFAULT);
	fprintf(fp, "\t--yaml-1.1               : Enable YAML 1.1 version instead of the library's default\n");
	fprintf(fp, "\t--yaml-1.2               : Enable YAML 1.2 version instead of the library's default\n");
	fprintf(fp, "\t--yaml-1.3               : Enable YAML 1.3 version instead of the library's default\n");
	fprintf(fp, "\t--sloppy-flow-indentation: Enable sloppy indentation in flow mode)"
						" (default %s)\n",
						SLOPPY_FLOW_INDENTATION_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--prefer-recursive       : Prefer recursive instead of iterative algorighms"
						" (default %s)\n",
						PREFER_RECURSIVE_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--ypath-aliases          : Use YPATH aliases (default %s)\n",
						YPATH_ALIASES_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--null-output            : Do not generate output (for scanner profiling)\n");
	fprintf(fp, "\t--collect-errors         : Collect errors instead of outputting directly"
						" (default %s)\n",
						COLLECT_ERRORS_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--allow-duplicate-keys   : Allow duplicate keys"
						" (default %s)\n",
						ALLOW_DUPLICATE_KEYS_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--strip-empty-kv         : Strip keys with empty values when emitting (not available in streaming mode)"
						" (default %s)\n",
						STRIP_EMPTY_KV_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--allocator <type>       : Use specified allocator for document/node allocations\n");
	fprintf(fp, "\t                           Types: default, malloc, linear, mremap, dedup, auto\n");
	fprintf(fp, "\t                           (default %s)\n", ALLOCATOR_DEFAULT);
	fprintf(fp, "\t--quiet, -q              : Quiet operation, do not "
						"output messages (default %s)\n",
						QUIET_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--dry-run                : Do not parse/emit\n");
	fprintf(fp, "\t--version, -v            : Display libfyaml version\n");
	fprintf(fp, "\t--help, -h               : Display  help message\n");

	if (tool_mode == OPT_TOOL || tool_mode != OPT_TESTSUITE) {
		fprintf(fp, "\t--sort, -s               : Perform mapping key sort (valid for dump)"
							" (default %s)\n",
							SORT_DEFAULT ? "true" : "false");
		fprintf(fp, "\t--comment, -c            : Output comments (experimental)"
							" (default %s)\n",
							COMMENT_DEFAULT ? "true" : "false");
		fprintf(fp, "\t--mode, -m <mode>        : Output mode can be one of original, block, flow, flow-oneline, json, json-tp, json-oneline, dejson, pretty|yamlfmt, flow-compact, json-compact"
							" (default %s)\n",
							MODE_DEFAULT);
		fprintf(fp, "\t--disable-flow-markers   : Disable testsuite's flow-markers"
							" (default %s)\n",
							DISABLE_FLOW_MARKERS_DEFAULT ? "true" : "false");
		fprintf(fp, "\t--disable-doc-markers    : Disable testsuite's document-markers"
							" (default %s)\n",
							DISABLE_DOC_MARKERS_DEFAULT ? "true" : "false");
		fprintf(fp, "\t--disable-scalar-styles  : Disable testsuite's scalar styles (all are double quoted)"
							" (default %s)\n",
							DISABLE_SCALAR_STYLES_DEFAULT ? "true" : "false");
		fprintf(fp, "\t--document-event-stream  : Generate a document and then produce the event stream"
							" (default %s)\n",
							DOCUMENT_EVENT_STREAM_DEFAULT ? "true" : "false");
		fprintf(fp, "\t--tsv-format             : Display testsuite in TSV format"
							" (default %s)\n",
							TSV_FORMAT_DEFAULT ? "true" : "false");
		if (tool_mode == OPT_TOOL || tool_mode == OPT_DUMP) {
			fprintf(fp, "\t--streaming              : Use streaming output mode"
								" (default %s)\n",
								STREAMING_DEFAULT ? "true" : "false");
			fprintf(fp, "\t--recreating             : Recreate streaming events"
								" (default %s)\n",
								RECREATING_DEFAULT ? "true" : "false");
		}
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

	if (tool_mode == OPT_TOOL || tool_mode == OPT_YPATH) {
		fprintf(fp, "\t--from, -F <path>        : Start from <path> (default %s)\n",
							FROM_DEFAULT);
		fprintf(fp, "\t--dump-pathexpr          : Dump the path expresion before the results\n");
		fprintf(fp, "\t--noexec                 : Do not execute the expression\n");
	}

	if (tool_mode == OPT_TOOL || tool_mode == OPT_COMPOSE) {
		fprintf(fp, "\t--dump-path              : Dump the path while composing\n");
	}

	if (tool_mode == OPT_TOOL) {
		fprintf(fp, "\t--dump                   : Dump mode, [arguments] are file names\n");
		fprintf(fp, "\t--testsuite              : Testsuite mode, [arguments] are <file>s to output parse events\n");
		fprintf(fp, "\t--filter                 : Filter mode, <stdin> is input, [arguments] are <path>s, outputs to stdout\n");
		fprintf(fp, "\t--join                   : Join mode, [arguments] are <path>s, outputs to stdout\n");
		fprintf(fp, "\t--ypath                  : YPATH mode, [arguments] are <path>s, file names, outputs to stdout\n");
		fprintf(fp, "\t--scan-dump              : scan-dump mode, [arguments] are file names\n");
		fprintf(fp, "\t--parse-dump             : parse-dump mode, [arguments] are file names\n");
		fprintf(fp, "\t--compose                : composer driver dump mode, [arguments] are file names\n");
		fprintf(fp, "\t--yaml-version           : Information about supported libfyaml's YAML versions\n");
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
	case OPT_YPATH:
		fprintf(fp, "\tParse and filter YAML with the ypath expression that results to /foo followed by /bar\n");
		fprintf(fp, "\t$ %s --ypath /foo,bar input.yaml\n\t...\n", progname);
		fprintf(fp, "\n");
		break;
	case OPT_SCAN_DUMP:
		fprintf(fp, "\tParse and dump YAML scanner tokens (internal)\n");
		fprintf(fp, "\n");
		break;
	case OPT_PARSE_DUMP:
		fprintf(fp, "\tParse and dump YAML parser events (internal)\n");
		fprintf(fp, "\n");
		break;
	case OPT_COMPOSE:
		fprintf(fp, "\tParse and dump generated YAML document tree using the composer api\n");
		fprintf(fp, "\t$ %s input.yaml\n\t...\n", progname);
		fprintf(fp, "\n");
		fprintf(fp, "\tParse and dump generated YAML document tree in block YAML form (and make whitespace visible)\n");
		fprintf(fp, "\t$ %s --compose -V -mblock input.yaml\n\t...\n", progname);
		fprintf(fp, "\n");
		fprintf(fp, "\tParse and dump generated YAML document from the input string\n");
		fprintf(fp, "\t$ %s --compose -mjson \">foo: bar\"\n", progname);
		fprintf(fp, "\t{\n\t  \"foo\": \"bar\"\n\t}\n");
		break;
	case OPT_YAML_VERSION_DUMP:
		fprintf(fp, "\tDisplay information about the YAML versions libfyaml supports\n");
		fprintf(fp, "\n");
		break;

	case OPT_B3SUM:
		fprintf(fp, "\tBLAKE3 hash b3sum utility\n");
		fprintf(fp, "\t--derive-key <context>    : Key derivation mode, with the given context string\n");
		fprintf(fp, "\t--no-names                : Omit filenames\n");
		fprintf(fp, "\t--raw                     : Output result in raw bytes (single input allowed)\n");
		fprintf(fp, "\t--length <n>              : Output only this amount of bytes per output (max %u)\n", FY_BLAKE3_OUT_LEN);
		fprintf(fp, "\t--check                   : Read files with BLAKE3 checksums and check files\n");
		fprintf(fp, "\t--keyed                   : Keyed mode with secret key read from <stdin> (32 raw bytes)\n");
		fprintf(fp, "\t--backend <backend>       : Select a BLAKE3 backend instead of the default\n");
		fprintf(fp, "\t--list-backends           : Print out a list of available backends\n");
		fprintf(fp, "\t--num-threads <n>         : Number of threads, -1 disable, 0 let system decide, >= 1 explicit\n");
		fprintf(fp, "\t--file-buffer <n>         : Size of file I/O buffer (non-mmap case), 0 let system decide\n");
		fprintf(fp, "\t--mmap-min-chunk <n>      : Size of minimum mmap chunk, 0 let system decide\n");
		fprintf(fp, "\t--mmap-max-chunk <n>      : Size of maximum mmap chunk, 0 let system decide\n");
		fprintf(fp, "\n");
		break;
	}
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
	int len, rc;

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

		rc = modify_flags(targ, flagsp);
		if (rc)
			return rc;

		s = sn < e ? (sn + 1) : sn;
	}

	return 0;
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

static void no_diag_output_fn(struct fy_diag *diag, void *user,
				  const char *buf, size_t len)
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
	unsigned int i, j, length;
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
		while (isxdigit(*s))
			s++;

		length = s - linebuf;

		if (length == 0 || length > (FY_BLAKE3_OUT_LEN * 2) || (length % 1) || !isspace(*s)) {
			fprintf(stderr, "Bad line found at file \"%s\" line #%d\n", check_filename, line);
			fprintf(stderr, "%s\n", linebuf);
			goto err_out;
		}

		*s++ = '\0';

		while (isspace(*s))
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
			if (rdn >= 0 && rdn < FY_BLAKE3_KEY_LEN)
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

int main(int argc, char *argv[])
{
	struct fy_parse_cfg cfg = {
		.search_path = INCLUDE_DEFAULT,
		.flags =
			(QUIET_DEFAULT 			 ? FYPCF_QUIET : 0) |
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

	fy_valgrind_check(&argc, &argv);

	/* select the appropriate tool mode */
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
				goto err_out_usage;
			}

			break;
		case 'w':
			width = atoi(optarg);
			if (width < 0 || width > FYECF_WIDTH_MASK) {
				fprintf(stderr, "bad width option %s\n", optarg);
				goto err_out_usage;
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
		case OPT_ALLOCATOR:
			cfg.flags &= ~(FYPCF_ALLOCATOR_MASK << FYPCF_ALLOCATOR_SHIFT);
			if (!strcmp(optarg, "default")) {
				cfg.flags |= FYPCF_ALLOCATOR_DEFAULT;
			} else if (!strcmp(optarg, "malloc")) {
				cfg.flags |= FYPCF_ALLOCATOR_MALLOC;
			} else if (!strcmp(optarg, "linear")) {
				cfg.flags |= FYPCF_ALLOCATOR_LINEAR;
			} else if (!strcmp(optarg, "mremap")) {
				cfg.flags |= FYPCF_ALLOCATOR_MREMAP;
			} else if (!strcmp(optarg, "dedup")) {
				cfg.flags |= FYPCF_ALLOCATOR_DEDUP;
			} else if (!strcmp(optarg, "auto")) {
				cfg.flags |= FYPCF_ALLOCATOR_AUTO;
			} else {
				fprintf(stderr, "bad allocator option %s\n", optarg);
				goto err_out_usage;
			}
			break;

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
		else
			emit_width_flags = FYECF_WIDTH(width);

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
					dump_scan_token(fyp, fyt, dcfg.colorize);
					fy_scan_token_free(fyp, fyt);
				}
			} else {
				while ((fyev = fy_parser_parse(fyp)) != NULL) {
					dump_parse_event(fyp, fyev, dcfg.colorize);
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


	}
	exitcode = EXIT_SUCCESS;

cleanup:
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

	return exitcode;

err_out_usage:
	exitcode = EXIT_FAILURE;
	display_usage(stderr, progname, tool_mode);
	goto cleanup;
}
