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

#ifndef _WIN32
#include <unistd.h>
#include <regex.h>
#else
#include "fy-win32.h"
#endif

#include <getopt.h>

#include "fy-valgrind.h"
#include "fy-tool-util.h"
#include "fy-tool-reflect.h"

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
#define OPT_REFLECT			1011

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
	{"help",		no_argument,		0,	'h' },
	{"version",		no_argument,		0,	'v' },
	{0,			0,			0,	 0  },
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
	fprintf(fp, "\t--disable-diag <x>       : Disable diag error module <x>\n");
	fprintf(fp, "\t--enable-diag <x>        : Enable diag error module <x>\n");
	fprintf(fp, "\t--show-diag <x>          : Show diag option <x> (source, position, type, module)\n");
	fprintf(fp, "\t--hide-diag <x>          : Hide diag optione <x> (source, position, type, module)\n");

	fprintf(fp, "\t--indent, -i <indent>    : Set dump indent to <indent>"
						" (default indent %d)\n",
						INDENT_DEFAULT);
	fprintf(fp, "\t--width, -w <width>      : Set dump width to <width> - inf for infinite"
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
	fprintf(fp, "\t--no-ending-newline      : Do not generate a final newline\n");
	fprintf(fp, "\t--preserve-flow-layout   : Preserve layout of single line flow collections\n");
	fprintf(fp, "\t--indented-seq-in-map    : Indent block sequences in block mappings\n");
	fprintf(fp, "\t--collect-errors         : Collect errors instead of outputting directly"
						" (default %s)\n",
						COLLECT_ERRORS_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--allow-duplicate-keys   : Allow duplicate keys"
						" (default %s)\n",
						ALLOW_DUPLICATE_KEYS_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--strip-empty-kv         : Strip keys with empty values when emitting (not available in streaming mode)"
						" (default %s)\n",
						STRIP_EMPTY_KV_DEFAULT ? "true" : "false");
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
			fprintf(fp, "\t--streaming              : Use streaming output mode (default)");
			fprintf(fp, "\t--no-streaming           : Don't use streaming output mode");
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

	if (tool_mode == OPT_REFLECT) {
		fprintf(fp, "\t--generate-c             : Generate C definitions from reflection input\n");
		fprintf(fp, "\t--generate-blob <blob>   : Generate packed blob from C source files\n");
		fprintf(fp, "\t--import-blob <blob>     : Import a packed blob as a reflection source\n");
		fprintf(fp, "\t--import-c-file <file>   : Import a C file as a reflection source\n");
		fprintf(fp, "\t--cflags <cflags>        : The C flags to use for the import\n");
		fprintf(fp, "\t--entry-type <type>      : The C type that is the entry point (i.e. the document)\n");
		fprintf(fp, "\t--packed-roundtrip       : Roundtrip the C reflection to packed type for debugging\n");
		fprintf(fp, "\t--dump-reflection        : Dump reflection structures\n");
		fprintf(fp, "\t--debug-reflection       : Debug messages during reflection processing\n");
	}

	if (tool_mode == OPT_GENERIC || tool_mode == OPT_GENERIC_TESTSUITE ||
	    tool_mode == OPT_GENERIC_PARSE_DUMP) {
		fprintf(fp, "\t--generic                : Generics dump\n");
		fprintf(fp, "\t--generic-testsuite      : Generics testsuite\n");
		fprintf(fp, "\t--builder-policy         : Builder policy\n");
		fprintf(fp, "\t--generic-parse-dump     : Generics parse-dump (shows comments etc)\n");
		fprintf(fp, "\t--dedup                  : Dedup mode on (default %s)\n",
							  DEDUP_DEFAULT ? "true" : "false");
		fprintf(fp, "\t--no-dedup               : Dedup mode off\n");
		fprintf(fp, "\t--dump-primitives        : Dump primitives\n");
		fprintf(fp, "\t--create-markerss        : Create markers\n");
		fprintf(fp, "\t--pyyaml-compat          : PYYAML compatibility mode\n");
		fprintf(fp, "\t--keep-style             : Do not strip style\n");
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

	case OPT_REFLECT:
		fprintf(fp, "\tReflection parsing a C header and dumping type info\n");
		fprintf(fp, "\t$ %s [--cflags=<>] header.h\n\t...\n", progname);
		fprintf(fp, "\n");
		fprintf(fp, "\tReflection parsing a C header and dumping type info\n");
		fprintf(fp, "\t$ %s blob.bin\n\t...\n", progname);
		fprintf(fp, "\n");
		fprintf(fp, "\tReflection convert C header files definition to a blob\n");
		fprintf(fp, "\t$ %s --reflect [--cflags=<>] --generate-blob=blob.bin header1.h header2.h\n\t...\n", progname);
		fprintf(fp, "\n");
		break;

	case OPT_GENERIC:
		fprintf(fp, "\tGeneric\n");
		fprintf(fp, "\n");
		break;

	case OPT_GENERIC_TESTSUITE:
		fprintf(fp, "\tGeneric testsuite\n");
		fprintf(fp, "\n");
		break;

	case OPT_GENERIC_PARSE_DUMP:
		fprintf(fp, "\tGeneric parse dump\n");
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

void reflection_type_info_dump(struct fy_reflection *rfl)
{
	const struct fy_type_info *ti;
	void *prev = NULL;

	while ((ti = fy_type_info_iterate(rfl, &prev)) != NULL)
		type_info_dump(ti, 0);
}

void reflection_prune_system(struct fy_reflection *rfl)
{
	const struct fy_type_info *ti;
	void *prev = NULL;

	fy_reflection_clear_all_markers(rfl);

	/* mark all non system and keep them */
	prev = NULL;
	while ((ti = fy_type_info_iterate(rfl, &prev)) != NULL) {
		if (ti->flags & FYTIF_SYSTEM_HEADER)
			continue;
		/* mark all non system structs, unions, enums and typedefs */
		if (fy_type_kind_has_fields(ti->kind) || ti->kind == FYTK_TYPEDEF)
			fy_type_info_mark(ti);
	}
	fy_reflection_prune_unmarked(rfl);
}

int reflection_type_filter(struct fy_reflection *rfl,
		const char *type_include, const char *type_exclude)
{
	const struct fy_type_info *ti;
	void *prev = NULL;
	regex_t type_include_reg, type_exclude_reg;
	bool type_include_reg_compiled = false, type_exclude_reg_compiled = false;
	bool include_match, exclude_match;
	int ret;

	if (!type_include && !type_exclude)
		return 0;

	if (type_include) {
		ret = regcomp(&type_include_reg, type_include, REG_EXTENDED | REG_NOSUB);
		if (ret) {
			fprintf(stderr, "Bad type-include regexp '%s'\n", type_include);
			goto err_out;
		}
		type_include_reg_compiled = true;
	}

	if (type_exclude) {
		ret = regcomp(&type_exclude_reg, type_exclude, REG_EXTENDED | REG_NOSUB);
		if (ret) {
			fprintf(stderr, "Bad type-exclude regexp '%s'\n", type_exclude);
			goto err_out;
		}
		type_exclude_reg_compiled = true;
	}

	fy_reflection_clear_all_markers(rfl);
	prev = NULL;
	while ((ti = fy_type_info_iterate(rfl, &prev)) != NULL) {
		if (type_include) {
			ret = regexec(&type_include_reg, ti->name, 0, NULL, 0);
			include_match = ret == 0;
		} else
			include_match = true;

		if (type_exclude) {
			ret = regexec(&type_include_reg, ti->name, 0, NULL, 0);
			exclude_match = ret == 0;
		} else
			exclude_match = false;

		if (include_match && !exclude_match)
			fy_type_info_mark(ti);
	}
	fy_reflection_prune_unmarked(rfl);

	ret = 0;
err_out:
	if (type_exclude_reg_compiled)
		regfree(&type_exclude_reg);

	if (type_include_reg_compiled)
		regfree(&type_include_reg);

	return ret;
}

static bool type_info_equal(const struct fy_type_info *ti_a, const struct fy_type_info *ti_b, bool recurse)
{
	const struct fy_field_info *fi_a, *fi_b;
	const char *comment_a, *comment_b;
	enum fy_type_info_flags tif_a, tif_b;
	size_t i;
	bool ret;

	assert(ti_a);
	assert(ti_b);

	// printf("%s: '%s'/'%s'\n", __func__, ti_a->name, ti_b->name);

	if (ti_a->kind != ti_b->kind) {
		fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
		return false;
	}

	ret = true;

	/* do not compare with the bits about positioning */
	tif_a = ti_a->flags & ~(FYTIF_MAIN_FILE | FYTIF_SYSTEM_HEADER);
	tif_b = ti_b->flags & ~(FYTIF_MAIN_FILE | FYTIF_SYSTEM_HEADER);
	if (tif_a != tif_b) {
		fprintf(stderr, "%s:%d %u %u\n", __FILE__, __LINE__, ti_a->flags, ti_b->flags);
		ret = false;
	}

	/* names must match, if they're not anonymous */
	if (!(ti_a->flags & ti_b->flags & (FYTIF_ANONYMOUS | FYTIF_ANONYMOUS_DEP))) {
		if (strcmp(ti_a->name, ti_b->name)) {
			fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
			ret = false;
		}
	}

	if (ti_a->size != ti_b->size) {
		fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
		ret = false;
	}

	if (ti_a->align != ti_b->align) {
		fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
		ret = false;
	}

	if (ti_a->count != ti_b->count) {
		fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
		ret = false;
	}

	if (ti_a->dependent_type || ti_b->dependent_type) {
		if (!ti_a->dependent_type || !ti_b->dependent_type) {
			fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
			ret = false;
		}
		/* recurse is single shot */
		if (recurse && !type_info_equal(ti_a->dependent_type, ti_b->dependent_type, false)) {
			fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
			ret = false;
		}
	}

	comment_a = fy_type_info_get_yaml_comment(ti_a);
	if (!comment_a)
		comment_a = "";
	comment_b = fy_type_info_get_yaml_comment(ti_b);
	if (!comment_b)
		comment_b = "";

	if (strcmp(comment_a, comment_b)) {
		fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
		ret = false;
	}

	if (fy_type_kind_has_fields(ti_a->kind)) {
		/* ti_a->count == ti_b->count here */
		for (i = 0, fi_a = ti_a->fields, fi_b = ti_b->fields; i < ti_a->count; i++, fi_a++, fi_b++) {

			if (fi_a->flags != fi_b->flags) {
				fprintf(stderr, "%s:%d fi_a->name=%s\n", __FILE__, __LINE__, fi_a->name);
				ret = false;
			}

			/* recurse is single shot */
			if (recurse && !type_info_equal(fi_a->type_info, fi_b->type_info, false)) {
				fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
				ret = false;
			}

			if (!(fi_a->type_info->flags & fi_b->type_info->flags & FYTIF_ANONYMOUS)) {
				if (strcmp(fi_a->name, fi_b->name)) {
					fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
					ret = false;
				}
			}

			if (ti_a->kind == FYTK_ENUM) {
				if (fi_a->uval != fi_b->uval) {
					fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
					ret = false;
				}
			} else if (!(fi_a->flags & FYFIF_BITFIELD)) {
				if (fi_a->offset != fi_b->offset) {
					fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
					ret = false;
				}
			} else {
				if (fi_a->bit_offset != fi_b->bit_offset) {
					fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
					ret = false;
				}
				if (fi_a->bit_width != fi_b->bit_width) {
					fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
					ret = false;
				}
			}

			comment_a = fy_field_info_get_yaml_comment(fi_a);
			if (!comment_a)
				comment_a = "";
			comment_b = fy_field_info_get_yaml_comment(fi_b);
			if (!comment_b)
				comment_b = "";

			if (strcmp(comment_a, comment_b)) {
				fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
				ret = false;
			}
		}
	}

	return ret;
}

bool reflection_equal(struct fy_reflection *rfl_a, struct fy_reflection *rfl_b)
{
	const struct fy_type_info *ti_a, *ti_b;
	void *prev_a = NULL, *prev_b = NULL;

	for (;;) {
		ti_a = fy_type_info_iterate(rfl_a, &prev_a);
		ti_b = fy_type_info_iterate(rfl_b, &prev_b);
		if (!ti_a && !ti_b)	/* done */
			break;
		if (!ti_a || !ti_b)	/* premature end of either */
			return false;

		if (!type_info_equal(ti_a, ti_b, true)) {
			fprintf(stderr, "%s: types '%s' '%s' differ\n", __func__,
					ti_a->name, ti_b->name);
			printf("=== a\n");
			reflection_type_info_dump(rfl_a);
			printf("=== b\n");
			reflection_type_info_dump(rfl_b);
			return false;
		}
	}

	return true;
}

struct reflection_any_value *
reflection_any_value_create(struct reflection_type_system *rts, struct fy_node *fyn)
{
	struct reflection_any_value *rav;

	assert(rts);
	if (!fyn)
		return NULL;

	rav = malloc(sizeof(*rav));
	RTS_ASSERT(rav);
	memset(rav, 0, sizeof(*rav));

	rav->rts = rts;
	rav->fyd = fy_document_create(NULL);
	RTS_ASSERT(rav->fyd);

	rav->fyn = fy_node_copy(rav->fyd, fyn);
	RTS_ASSERT(rav->fyn);

	return rav;

err_out:
	reflection_any_value_destroy(rav);
	return NULL;
}

void reflection_any_value_destroy(struct reflection_any_value *rav)
{
	if (!rav)
		return;

	if (rav->value && rav->rtd) {
		// fprintf(stderr, "%s: %p unref rtd %s\n", __func__, rav, rav->rtd->ti->name);
		reflection_type_data_free(rav->rtd, rav->value);
		rav->value = NULL;
		rav->rtd = NULL;
	}
	assert(!rav->value);

	if (rav->str)
		free(rav->str);

	fy_node_free(rav->fyn);
	fy_document_destroy(rav->fyd);

	free(rav);
}

const char *
reflection_any_value_get_str(struct reflection_any_value *rav)
{
	if (!rav || !rav->fyn)
		return NULL;

	if (!rav->str)
		rav->str = fy_emit_node_to_string(rav->fyn,
				FYECF_WIDTH_INF | FYECF_INDENT_DEFAULT |
				FYECF_MODE_FLOW_ONELINE | FYECF_NO_ENDING_NEWLINE);

	return rav->str;
}

struct reflection_any_value *
reflection_any_value_copy(struct reflection_any_value *rav_src)
{
	if (!rav_src)
		return NULL;

	return reflection_any_value_create(rav_src->rts, rav_src->fyn);
}

struct reflection_meta *
reflection_meta_create(struct reflection_type_system *rts)
{
	struct reflection_meta *rm;

	assert(rts);
	rm = malloc(sizeof(*rm));
	RTS_ASSERT(rm);

	memset(rm, 0, sizeof(*rm));
	rm->rts = rts;
	return rm;

err_out:
	return NULL;
}

void
reflection_meta_destroy(struct reflection_meta *rm)
{
	struct reflection_any_value *rav;
	unsigned int i;
	char *str;

	if (!rm)
		return;

	for (i = 0; i < rmvid_str_count; i++) {
		str = rm->strs[i];
		if (str)
			free(str);
	}
	for (i = 0; i < rmvid_any_count; i++) {
		rav = rm->anys[i];
		if (rav)
			reflection_any_value_destroy(rav);
	}

	free(rm);
}

const char *
reflection_meta_value_str(struct reflection_meta *rm, enum reflection_meta_value_id id)
{
	if (!rm)
		return NULL;

	assert(reflection_meta_value_id_is_valid(id));
	if (reflection_meta_value_id_is_bool(id))
		return reflection_meta_get_bool(rm, id) ? "true" : "false";
	if (reflection_meta_value_id_is_str(id))
		return reflection_meta_get_str(rm, id);
	if (reflection_meta_value_id_is_any(id))
		return reflection_any_value_get_str(reflection_meta_get_any_value(rm, id));
	return NULL;
}

void
reflection_meta_dump(struct reflection_meta *rm)
{
	enum reflection_meta_value_id id;
	const char *name;

	for (id = rmvid_first_valid; id <= rmvid_last_valid; id++) {
		name = reflection_meta_value_id_get_name(id);
		assert(name);

		fprintf(stderr, "%s: %c %s\n", name,
				reflection_meta_value_get_explicit(rm, id) ? 'E' : '-',
				reflection_meta_value_str(rm, id) ? : "<NULL>");
	}
}

int
reflection_meta_set_bool(struct reflection_meta *rm, enum reflection_meta_value_id id, bool v)
{
	unsigned int idx;

	if (!rm || !reflection_meta_value_id_is_bool(id))
		return -1;

	idx = id - rmvid_first_bool;
	if (v)
		rm->bools[idx / 8] |= BIT(idx & 7);
	else
		rm->bools[idx / 8] &= ~BIT(idx & 7);

	/* mark the value as explicitly set */
	if (!reflection_meta_value_get_explicit(rm, id)) {
		reflection_meta_value_set_explicit(rm, id, true);
		rm->explicit_count++;
	}

	return 0;
}

int reflection_meta_set_str(struct reflection_meta *rm, enum reflection_meta_value_id id, const char *str)
{
	unsigned int idx;
	char *new_str;

	if (!rm || !str || !reflection_meta_value_id_is_str(id))
		return -1;

	idx = id - rmvid_first_str;

	new_str = strdup(str);
	if (!new_str)
		return -1;

	if (rm->strs[idx])
		free(rm->strs[idx]);
	rm->strs[idx] = new_str;

	if (!reflection_meta_value_get_explicit(rm, id)) {
		reflection_meta_value_set_explicit(rm, id, true);
		rm->explicit_count++;
	}

	return 0;
}

int reflection_meta_set_any_value(struct reflection_meta *rm, enum reflection_meta_value_id id, struct reflection_any_value *rav, bool copy)
{
	struct reflection_any_value *new_rav;
	unsigned int idx;

	if (!rm || !rav || !reflection_meta_value_id_is_any(id))
		return -1;

	idx = id - rmvid_first_any;

	if (copy) {
		new_rav = reflection_any_value_copy(rav);
		if (!new_rav)
			return -1;
	} else {
		new_rav = rav;
	}

	if (rm->anys[idx])
		reflection_any_value_destroy(rm->anys[idx]);
	rm->anys[idx] = new_rav;

	if (!reflection_meta_value_get_explicit(rm, id)) {
		reflection_meta_value_set_explicit(rm, id, true);
		rm->explicit_count++;
	}

	return 0;
}

int
reflection_meta_fill(struct reflection_meta *rm, struct fy_node *fyn_root)
{
	struct reflection_type_system *rts;
	struct fy_token *fyt;
	struct fy_node *fyn;
	const char *name, *text0;
	enum reflection_meta_value_id id;
	enum fy_parser_mode mode;
	bool v;
	int rc;

	assert(rm);

	/* nothing? that's find, use the defaults */
	if (!fyn_root)
		return 0;

	rts = rm->rts;
	assert(rts);

	mode = reflection_type_system_parse_mode(rts);

	for (id = rmvid_first_valid; id <= rmvid_last_valid; id++) {

		/* if the value is already set, don't try again */
		if (reflection_meta_value_get_explicit(rm, id))
			continue;

		name = reflection_meta_value_id_get_name(id);
		assert(name);

		fyn = fy_node_by_path(fyn_root, name, FY_NT, FYNWF_DONT_FOLLOW);
		if (!fyn)
			continue;

		if (!reflection_meta_value_id_is_any(id)) {
			fyt = fy_node_get_scalar_token(fyn);
			RTS_ASSERT(fyt);

			text0 = fy_token_get_text0(fyt);
			RTS_ASSERT(text0);

			if (reflection_meta_value_id_is_bool(id)) {

				rc = parse_boolean_scalar(text0, mode, &v);
				RTS_ASSERT(!rc);

				rc = reflection_meta_set_bool(rm, id, v);
				RTS_ASSERT(!rc);

			} else if (reflection_meta_value_id_is_str(id)) {

				rc = reflection_meta_set_str(rm, id, text0);
				RTS_ASSERT(!rc);

			} else
				RTS_ASSERT(false);
		} else {
			rc = reflection_meta_set_any_value(rm, id, reflection_any_value_create(rm->rts, fyn), false);
			RTS_ASSERT(!rc);
		}
	}

	return 0;

err_out:
	return -1;
}

bool
reflection_meta_compare(struct reflection_meta *rm_a, struct reflection_meta *rm_b)
{
	enum reflection_meta_value_id id;
	unsigned int idx;
	char *str_a, *str_b;
	struct reflection_any_value *rav_a, *rav_b;
	int r;

	if (!rm_a || !rm_b || rm_a->rts != rm_b->rts)
		return false;

	/* compare the presense map */
	r = memcmp(rm_a->explicit_map, rm_b->explicit_map, ARRAY_SIZE(rm_a->explicit_map));
	if (r)
		return false;

	/* fast bool compare */
	r = memcmp(rm_a->bools, rm_b->bools, ARRAY_SIZE(rm_a->bools));
	if (r)
		return false;

	/* compare each string */
	for (id = rmvid_first_str; id <= rmvid_last_str; id++) {
		idx = id - rmvid_first_str;
		str_a = rm_a->strs[idx];
		str_b = rm_b->strs[idx];
		if (!str_a && !str_b)
			continue;
		if (str_a && !str_b)
			return false;
		if (!str_a && str_b)
			return false;
		r = strcmp(str_a, str_b);
		if (r)
			return false;
	}

	for (id = rmvid_first_any; id <= rmvid_last_any; id++) {
		idx = id - rmvid_first_any;
		rav_a = rm_a->anys[idx];
		rav_b = rm_b->anys[idx];
		if (!rav_a && !rav_b)
			continue;
		if (rav_a && !rav_b)
			return false;
		if (!rav_a && rav_b)
			return false;
		if (!fy_node_compare(rav_a->fyn, rav_b->fyn))
			return false;
	}

	return true;
}

struct reflection_meta *
reflection_meta_copy(struct reflection_meta *rm_src)
{
	struct reflection_type_system *rts;
	struct reflection_meta *rm;
	enum reflection_meta_value_id id;
	unsigned int idx;

	if (!rm_src)
		return NULL;

	rts = rm_src->rts;
	assert(rts);

	rm = reflection_meta_create(rts);
	if (!rm)
		return NULL;

	rm->explicit_count = rm_src->explicit_count;
	memcpy(rm->explicit_map, rm_src->explicit_map, sizeof(rm->explicit_map));
	memcpy(rm->bools, rm_src->bools, sizeof(rm->bools));

	for (id = rmvid_first_str; id <= rmvid_last_str; id++) {
		idx = id - rmvid_first_str;
		if (rm_src->strs[idx]) {
			rm->strs[idx] = strdup(rm_src->strs[idx]);
			RTS_ASSERT(rm->strs[idx]);
		}
	}

	for (id = rmvid_first_any; id <= rmvid_last_any; id++) {
		idx = id - rmvid_first_any;
		if (rm_src->anys[idx]) {
			rm->anys[idx] = reflection_any_value_copy(rm_src->anys[idx]);
			RTS_ASSERT(rm->anys[idx]);
		}
	}

	return rm;

err_out:
	reflection_meta_destroy(rm);
	return NULL;
}

struct fy_document *
reflection_meta_get_document(struct reflection_meta *rm)
{
	struct reflection_type_system *rts;
	struct fy_document *fyd = NULL;
	struct fy_node *fyn_map, *fyn_key, *fyn_value;
	enum reflection_meta_value_id id;
	bool v;
	const char *name, *str;
	struct reflection_any_value *rav;
	int rc;

	if (!rm)
		return NULL;

	rts = rm->rts;
	assert(rts);

	fyn_map = fyn_key = fyn_value = NULL;

	fyd = fy_document_create(NULL);
	RTS_ASSERT(fyd);

	fyn_map = fy_node_create_mapping(fyd);
	RTS_ASSERT(fyn_map);

	for (id = rmvid_first_valid; id <= rmvid_last_valid; id++) {

		/* if the value is not explicitly set, do not include it */
		if (!reflection_meta_value_get_explicit(rm, id))
			continue;

		name = reflection_meta_value_id_get_name(id);
		RTS_ASSERT(name);

		if (reflection_meta_value_id_is_bool(id)) {

			v = reflection_meta_get_bool(rm, id);

			fyn_value = fy_node_create_scalar(fyd, v ? "true" : "false", FY_NT);
			RTS_ASSERT(fyn_value);

		} else if (reflection_meta_value_id_is_str(id)) {

			str = reflection_meta_get_str(rm, id);
			if (!str)
				continue;

			fyn_value = fy_node_create_scalar(fyd, str, FY_NT);
			RTS_ASSERT(fyn_value);

		} else if (reflection_meta_value_id_is_any(id)) {

			rav = reflection_meta_get_any_value(rm, id);
			if (!rav || !rav->fyn)
				continue;

			fyn_value = fy_node_copy(fyd, rav->fyn);
			RTS_ASSERT(fyn_value);
		} else
			continue;

		fyn_key = fy_node_create_scalar(fyd, name, FY_NT);
		RTS_ASSERT(fyn_key);

		rc = fy_node_mapping_append(fyn_map, fyn_key, fyn_value);
		RTS_ASSERT(!rc);

		fyn_key = fyn_value = NULL;
	}

	rc = fy_document_set_root(fyd, fyn_map);
	RTS_ASSERT(!rc);

	return fyd;

err_out:
	fy_node_free(fyn_key);
	fy_node_free(fyn_value);
	fy_node_free(fyn_map);
	fy_document_destroy(fyd);
	return NULL;
}

char *
reflection_meta_get_document_str(struct reflection_meta *rm)
{
	struct fy_document *fyd;
	char *str;

	if (!rm)
		return NULL;

	fyd = reflection_meta_get_document(rm);
	if (!fyd)
		return NULL;

	str = fy_emit_node_to_string(fy_document_root(fyd),
			FYECF_WIDTH_INF | FYECF_INDENT_DEFAULT |
			FYECF_MODE_FLOW_ONELINE | FYECF_NO_ENDING_NEWLINE);

	fy_document_destroy(fyd);
	return str;
}

#define reflection_meta_get_document_str_alloca(_rm) \
	FY_ALLOCA_COPY_FREE(reflection_meta_get_document_str(_rm), FY_NT)

void *reflection_any_value_generate(struct reflection_any_value *rav,
				    struct reflection_type_data *rtd)
{
	struct reflection_type_system *rts;

	if (!rav)
		return NULL;

	rts = rav->rts;
	assert(rts);

	/* if we generated for this rtd before, return it */
	if (rtd && rav->rtd == rtd)
		return rav->value;

	if (rav->value && rav->rtd) {
		reflection_type_data_free(rav->rtd, rav->value);
		rav->value = NULL;
		rav->rtd = NULL;
	}
	assert(!rav->value);

	if (!rtd)
		return NULL;

	rav->value = reflection_type_data_generate_value(rtd, rav->fyn);
	RTS_ASSERT(rav->value);

	rav->rtd = rtd;

	return rav->value;

err_out:
	return NULL;
}

int
reflection_any_value_equal_rw(struct reflection_any_value *rav, struct reflection_walker *rw)
{
	void *data;
	int rc;

	if (!rav || !rw)
		return -1;

	data = reflection_any_value_generate(rav, rw->rtd);
	if (!data)
		return -1;

	rc = reflection_eq_rw(rw, reflection_rw_value_alloca(rw->rtd, data));
	if (rc < 0)
		return -1;

	return rc;
}

struct reflection_field_data *
reflection_type_data_lookup_field(struct reflection_type_data *rtd, const char *field)
{
	struct reflection_field_data *rfd;
	int i;

	if (!rtd || !field)
		return NULL;

	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];
		if (!strcmp(rfd->field_name, field))
			return rfd;
	}
	return NULL;
}

struct reflection_field_data *
reflection_type_data_lookup_next_anonymous_field(struct reflection_type_data *rtd, const char *field)
{
	struct reflection_field_data *rfd, *rfdt;
	int i;

	if (!rtd || !field)
		return NULL;

	/* lookup by name directly */
	rfdt = reflection_type_data_lookup_field(rtd, field);
	if (rfdt)
		return rfdt;

	/* test each anonymous field in sequence */
	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];
		if (!(rfd->fi->type_info->flags & FYTIF_ANONYMOUS_RECORD_DECL))
			continue;

		rfdt = reflection_type_data_lookup_next_anonymous_field(rfd_rtd(rfd), field);
		if (rfdt)
			return rfd;	/* next step */
	}

	return NULL;
}

struct reflection_field_data *
reflection_type_data_lookup_field_by_enum_value(struct reflection_type_data *rtd, intmax_t val)
{
	int idx;

	if (!rtd)
		return NULL;

	idx = fy_field_info_index(fy_type_info_lookup_field_by_enum_value(rtd->ti, val));
	if (idx < 0 || idx >= rtd->fields_count)
		return NULL;

	return rtd->fields[idx];
}

struct reflection_field_data *
reflection_type_data_lookup_field_by_unsigned_enum_value(struct reflection_type_data *rtd, uintmax_t val)
{
	int idx;

	if (!rtd)
		return NULL;

	idx = fy_field_info_index(fy_type_info_lookup_field_by_unsigned_enum_value(rtd->ti, val));
	if (idx < 0 || idx >= rtd->fields_count)
		return NULL;

	return rtd->fields[idx];
}

struct reflection_field_data *
reflection_type_data_lookup_field_by_scalar_enum_value(struct reflection_type_data *rtd,
		union integer_scalar val, bool is_signed)
{
	return is_signed ?
		reflection_type_data_lookup_field_by_enum_value(rtd, val.sval) :
		reflection_type_data_lookup_field_by_unsigned_enum_value(rtd, val.uval);
}

const struct reflection_type_ops reflection_ops_table[FYTK_COUNT];

void
fy_tool_vlog(struct fy_tool_log_ctx *ctx, enum fy_error_type error_type,
		const char *fmt, va_list ap)
{
	struct fy_diag *diag;
	struct fy_event *fye;
	struct fy_diag_ctx *diag_ctx;
	bool diag_needs_unref, saved_error;
	int rc __attribute__((unused));

	assert(ctx);

	diag_needs_unref = false;
	switch (ctx->op) {
	case rfltop_rts:
		assert(ctx->rts);
		diag = ctx->rts->diag;
		break;

	case rfltop_rw:
		assert(ctx->rw);
		assert(ctx->rw->rtd);
		assert(ctx->rw->rtd->rts);
		diag = ctx->rw->rtd->rts->diag;
		break;

	case rfltop_parse:
		assert(ctx->fyp);
		diag = fy_parser_get_diag(ctx->fyp);
		diag_needs_unref = true;
		break;

	case rfltop_emit:
		assert(ctx->emit);
		diag = fy_emitter_get_diag(ctx->emit);
		diag_needs_unref = true;
		break;
	default:
		diag = NULL;
		break;
	}

	/* can't do much without a diag */
	if (!diag) {
		vfprintf(stderr, fmt, ap);
		return;
	}

	saved_error = ctx->save_error && fy_diag_got_error(diag);

	fye = ctx->fye;
	if (!fye && ctx->needs_event && ctx->fyp)
		fye = (struct fy_event *)fy_parser_parse_peek(ctx->fyp);

	if (!fye) {
		if (ctx->has_diag_ctx)
			diag_ctx = &ctx->diag_ctx;
		else {
			diag_ctx = alloca(sizeof(*diag_ctx));
			memset(diag_ctx, 0, sizeof(*diag_ctx));
			diag_ctx->level = error_type;
			diag_ctx->module = FYEM_UNKNOWN;
		}
		rc = fy_vdiag(diag, diag_ctx, fmt, ap);
		assert(rc >= 0);
	} else {
		fy_diag_event_vreport(diag, fye, ctx->event_part, error_type, fmt, ap);
	}

	if (diag_needs_unref)
		fy_diag_unref(diag);

	if (ctx->save_error)
		fy_diag_set_error(diag, saved_error);
}

void fy_tool_log(struct fy_tool_log_ctx *ctx, enum fy_error_type error_type,
		   const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_tool_vlog(ctx, error_type, fmt, ap);
	va_end(ap);
}

struct reflection_field_data *
reflection_get_field(struct reflection_walker *rw,
		     struct reflection_walker **rw_fieldp,
		     struct reflection_walker **rw_basep)
{
	struct reflection_walker *rwf, *rws, *rwb;
	struct reflection_field_data *rfd;
	uintmax_t field_idx;

	if (rw_fieldp)
		*rw_fieldp = NULL;

	if (rw_basep)
		*rw_basep = NULL;

	rwf = rw;
	rws = rw->parent;
	rwb = rw;
	rfd = NULL;

	/* first find the base (when directly dependent) */
	while (rwf->parent && fy_type_kind_is_direct_dependent(rwf->parent->rtd->ti->kind)) {
		rwf = rwf->parent;
	}

	/* the base type */
	if (rwf->parent && !fy_type_kind_is_direct_dependent(rwf->parent->rtd->ti->kind))
		rwb = rwf->parent;

	rws = (rwf->parent && fy_type_kind_has_direct_fields(rwf->parent->rtd->ti->kind)) ? rwf->parent : NULL;

	/* NOTE this may happen when copying */
	if (!rws)
		return NULL;

	assert(rwf->flags & RWF_FIELD_IDX);
	field_idx = rwf->idx;

	RW_ASSERT(fy_type_kind_has_fields(rws->rtd->ti->kind));
	RW_ASSERT(field_idx < (uintmax_t)rws->rtd->fields_count);
	rfd = rws->rtd->fields[field_idx];
	RW_ASSERT(rfd);

	if (rw_fieldp)
		*rw_fieldp = rwf;
	if (rw_basep)
		*rw_basep = rwb;
	return rfd;

err_out:
	return NULL;
}

static int
common_scalar_parse(struct fy_parser *fyp, struct reflection_walker *rw,
		    enum reflection_parse_flags flags)
{
	struct fy_event *fye = NULL;
	struct fy_token *fyt;
	enum fy_scalar_style style;
	enum fy_type_kind type_kind;
	const char *text0;
	int rc = -1;

	assert(rw);
	assert(rw->rtd);
	assert(rw->data || (flags & RPF_NO_STORE));

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

	RP_INPUT_CHECK(fye->type == FYET_SCALAR,
		       "expecting FYET_SCALAR, got %s\n", fy_event_type_get_text(fye->type));

	type_kind = rw->rtd->ti->kind;
	fy_type_kind_is_valid(type_kind);

	fyt = fy_event_get_token(fye);
	style = fy_token_scalar_style(fyt);

	RP_INPUT_CHECK(fye->type == FYET_SCALAR, "invalid empty scalar\n");

	/* validate type */
	RP_ASSERT(fy_type_kind_is_integer(type_kind) ||
		  fy_type_kind_is_float(type_kind) || type_kind == FYTK_BOOL);

	/* intercept char */
	if (type_kind == FYTK_CHAR && (style == FYSS_SINGLE_QUOTED || style == FYSS_DOUBLE_QUOTED)) {
		const char *text;
		const uint8_t *p;
		size_t text_len;
		int i, c, w;
		union integer_scalar numi;

		text = fy_token_get_text(fyt, &text_len);
		RP_ASSERT(text != NULL);

		RP_INPUT_CHECK(text_len != 0, "character value must exist");

		p = (const uint8_t *)text;
		/* get width from the first octet */
		w = (p[0] & 0x80) == 0x00 ? 1 :
		    (p[0] & 0xe0) == 0xc0 ? 2 :
		    (p[0] & 0xf0) == 0xe0 ? 3 :
		    (p[0] & 0xf8) == 0xf0 ? 4 : 0;

		RP_INPUT_CHECK((size_t)w == text_len, "UTF8 text too big to fit in char");
		RP_INPUT_CHECK((size_t)w <= text_len, "malformed UTF8 text (start)");

		c = p[0] & (0xff >> w);
		for (i = 1; i < w; i++) {
			RP_INPUT_CHECK((p[i] & 0xc0) == 0x80, "malformed UTF8 text (middle)");
			c = (c << 6) | (p[i] & 0x3f);
		}

		/* check for validity */
		RP_INPUT_CHECK(!((w == 4 && c < 0x10000) ||
			         (w == 3 && c <   0x800) ||
				 (w == 2 && c <    0x80) ||
				 (c >= 0xd800 && c <= 0xdfff) || c >= 0x110000),
				"malformed UTF8 text (end)");

		RP_INPUT_CHECK(c >= 0, "malformed UTF8 text (negative value)");

		RP_INPUT_CHECK(c <= UCHAR_MAX, "UTF8 code %d does not fit (max %d)", c, UCHAR_MAX);

		if (!(flags & RPF_NO_STORE)) {
			numi.uval = (unsigned int)c;
			store_integer_scalar_no_check_rw(rw, numi);
		}

		rc = 0;
		goto out;
	}

	text0 = fy_token_get_text0(fyt);
	RP_ASSERT(text0 != NULL);

	if (fy_type_kind_is_integer(type_kind)) {
		union integer_scalar numi, mini, maxi;

		RP_INPUT_CHECK(style == FYSS_PLAIN, "only plain style allowed for integers");
		RP_INPUT_CHECK(*text0 != '\0', "integer must not be empty scalar");

		rc = parse_integer_scalar(type_kind, text0, fy_parser_get_mode(fyp), &numi);
		RP_INPUT_CHECK(rc != -ERANGE, "%s integer scalar value out of range",
				fy_type_kind_is_signed(type_kind) ? "signed" : "unsigned");
		RP_INPUT_CHECK(!rc, "malformed integer scalar value");

		rc = store_integer_scalar_check_rw(rw, numi, &mini, &maxi);
		RP_INPUT_CHECK(!(rc == -ERANGE && fy_type_kind_is_signed(type_kind)),
				"signed value %jd out of range (min=%jd, max=%jd)",
			       numi.sval, mini.sval, maxi.sval);
		RP_INPUT_CHECK(!(rc == -ERANGE && fy_type_kind_is_unsigned(type_kind)),
				"unsigned value %ju out of range (max=%ju)",
			       numi.sval, maxi.uval);
		RP_ASSERT(!rc);

		if (!(flags & RPF_NO_STORE))
			store_integer_scalar_no_check_rw(rw, numi);

	} else if (fy_type_kind_is_float(type_kind)) {

		union float_scalar numf;

		RP_INPUT_CHECK(style == FYSS_PLAIN,
			       "only plain style allowed for floating point numbers");
		RP_INPUT_CHECK(*text0 != '\0', "float must not be empty scalar");

		rc = parse_float_scalar(type_kind, text0, fy_parser_get_mode(fyp), &numf);
		RP_INPUT_CHECK(rc != -ERANGE, "float value out of range");
		RP_INPUT_CHECK(rc == 0, "malformed float value");

		if (!(flags & RPF_NO_STORE))
			store_float_scalar(type_kind, rw->data, numf);

	} else if (type_kind == FYTK_BOOL) {
		bool v;

		RP_INPUT_CHECK(style == FYSS_PLAIN, "only plain style allowed for booleans");
		RP_INPUT_CHECK(*text0 != '\0', "bool must not be empty scalar");

		rc = parse_boolean_scalar(text0, fy_parser_get_mode(fyp), &v);
		RP_INPUT_CHECK(rc == 0, "invalid boolean format");

		if (!(flags & RPF_NO_STORE))
			store_boolean_scalar_rw(rw, v);

	} else {
		/* should never get here */
		abort();
	}

	fy_parser_event_free(fyp, fye);
	return 0;

out:
	fy_parser_event_free(fyp, fye);
	return rc;

err_input:
	rc = -EINVAL;
	goto out;

err_out:
	rc = -1;
	goto out;
}

static int
null_scalar_parse(struct fy_parser *fyp, struct reflection_walker *rw,
		  enum reflection_parse_flags flags)
{
	struct fy_event *fye = NULL;
	struct fy_token *fyt;
	enum fy_scalar_style style;
	const char *text0;
	int rc;

	assert(rw);
	assert(rw->rtd);
	assert(rw->data || (flags & RPF_NO_STORE));

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

	RP_INPUT_CHECK(fye->type == FYET_SCALAR,
		       "expecting FYET_SCALAR, got %s\n", fy_event_type_get_text(fye->type));

	fyt = fy_event_get_token(fye);
	style = fy_token_scalar_style(fyt);

	text0 = fy_token_get_text0(fyt);
	RP_ASSERT(text0 != NULL);

	RP_INPUT_CHECK(style == FYSS_PLAIN, "only plain style allowed for null");

	rc = parse_null_scalar(text0, fy_parser_get_mode(fyp));

	RP_INPUT_CHECK(rc == 0, "invalid null format");

out:
	fy_parser_event_free(fyp, fye);
	return rc;

err_input:
	rc = -EINVAL;
	goto out;

err_out:
	rc = -1;
	goto out;
}

static int
integer_scalar_emit(struct fy_emitter *emit, enum fy_type_kind type_kind, union integer_scalar num,
		    struct reflection_walker *rw, enum reflection_emit_flags flags)
{
	bool is_signed;
	char buf[3 * sizeof(uintmax_t) + 3];	/* maximum buffer space */
	char *s, *e;
	bool neg;
	size_t len;
	uintmax_t val;
	int rc;

	is_signed = fy_type_kind_is_signed(type_kind);

	if (is_signed && num.sval < 0) {
		val = (uintmax_t)-num.sval;
		neg = true;
	} else {
		val = num.uval;
		neg = false;
	}

#undef PUTD
#define PUTD(_c) \
	do { \
		assert(s > buf); \
		*--s = (_c); \
	} while(0)

	e = buf + sizeof(buf);
	s = e;
	while (val) {
		PUTD('0' + val % 10);
		val /= 10;
	}
	if (s == e)
		PUTD('0');
	if (neg)
		PUTD('-');
	len = e - s;
#undef PUTD

	rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_PLAIN, s, len, NULL, NULL);
	RE_ASSERT(!rc);

	return 0;

err_out:
	return -1;
}

static int
float_scalar_emit(struct fy_emitter *emit, enum fy_type_kind type_kind, union float_scalar num,
		  struct reflection_walker *rw, enum reflection_emit_flags flags)
{
	enum {
		eft_normal,
		eft_plus_inf,
		eft_minus_inf,
		eft_nan,
	} emit_float_type;
	static const char *abnormal_values[] = {
		[eft_plus_inf] = ".inf",
		[eft_minus_inf] = "-.inf",
		[eft_nan] = ".nan",
	};
	const char *text;
	int rc;

	RE_ASSERT(fy_type_kind_is_float(type_kind));

	switch (type_kind) {
	case FYTK_FLOAT:
		emit_float_type = isnan(num.f) ? eft_nan :
			          isinf(num.f) ? (signbit(num.f) ? eft_minus_inf : eft_plus_inf) :
				  eft_normal;
		break;
	case FYTK_DOUBLE:
		emit_float_type = isnan(num.d) ? eft_nan :
			          isinf(num.d) ? (signbit(num.d) ? eft_minus_inf : eft_plus_inf) :
				  eft_normal;
		break;
	case FYTK_LONGDOUBLE:
		emit_float_type = isnan(num.ld) ? eft_nan :
			          isinf(num.ld) ? (signbit(num.ld) ? eft_minus_inf : eft_plus_inf) :
				  eft_normal;
		break;
	default:
		abort();
	}

	if (emit_float_type != eft_normal) {

		assert((size_t)emit_float_type < sizeof(abnormal_values)/sizeof(abnormal_values[0]));
		text = abnormal_values[emit_float_type];
		assert(text);

		rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_PLAIN, text, FY_NT, NULL, NULL);
		RE_ASSERT(!rc);

		return 0;
	}

	switch (type_kind) {
	case FYTK_FLOAT:
		rc = fy_emit_scalar_printf(emit, FYSS_PLAIN, NULL, NULL, "%.*g", FY_FLT_DECIMAL_DIG, num.f);
		RE_ASSERT(!rc);
		break;
	case FYTK_DOUBLE:
		rc = fy_emit_scalar_printf(emit, FYSS_PLAIN, NULL, NULL, "%.*lg", FY_DBL_DECIMAL_DIG, num.d);
		RE_ASSERT(!rc);
		break;
	case FYTK_LONGDOUBLE:
		rc = fy_emit_scalar_printf(emit, FYSS_PLAIN, NULL, NULL, "%.*Lg", FY_LDBL_DECIMAL_DIG, num.ld);
		RE_ASSERT(!rc);
		break;
	default:
		abort();
	}

	return 0;
err_out:
	return -1;
}

static int
bool_scalar_emit(struct fy_emitter *emit, enum fy_type_kind type_kind, bool v,
		 struct reflection_walker *rw, enum reflection_emit_flags flags)
{
	const char *bool_txt[2] = { "false", "true" };
	size_t bool_txt_sz[2] = { 5, 4 };
	const char *str;
	size_t len;
	int rc;

	str = bool_txt[!!v];
	len = bool_txt_sz[!!v];

	rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_PLAIN, str, len, NULL, NULL);
	RE_ASSERT(!rc);

	return 0;

err_out:
	return -1;
}

static int
common_scalar_emit(struct fy_emitter *emit, struct reflection_walker *rw,
		   enum reflection_emit_flags flags)
{
	enum fy_type_kind type_kind;

	type_kind = rw->rtd->ti->kind;

	if (fy_type_kind_is_integer(type_kind))
		return integer_scalar_emit(emit, type_kind,
					   load_integer_scalar_rw(rw),
					   rw, flags);

	if (fy_type_kind_is_float(type_kind)) {
		assert(!(rw->flags & RWF_BITFIELD_DATA));
		return float_scalar_emit(emit, type_kind,
					 load_float_scalar(type_kind, rw->data),
					 rw, flags);
	}

	if (type_kind == FYTK_BOOL)
		return bool_scalar_emit(emit, type_kind,
					load_boolean_scalar_rw(rw),
					rw, flags);
	return -1;
}

static inline int
scalar_cmp(enum fy_type_kind type_kind, const void *p_a, const void *p_b)
{
	switch (type_kind) {
	case FYTK_BOOL:
		return *(_Bool *)p_a > *(_Bool *)p_b ?  1 :
		       *(_Bool *)p_a < *(_Bool *)p_b ? -1 : 0;
	case FYTK_CHAR:
		return *(char *)p_a > *(char *)p_b ?  1 :
		       *(char *)p_a < *(char *)p_b ? -1 : 0;
	case FYTK_SCHAR:
		return *(signed char *)p_a > *(signed char *)p_b ?  1 :
		       *(signed char *)p_a < *(signed char *)p_b ? -1 : 0;
	case FYTK_UCHAR:
		return *(unsigned char *)p_a > *(unsigned char *)p_b ?  1 :
		       *(unsigned char *)p_a < *(unsigned char *)p_b ? -1 : 0;
	case FYTK_SHORT:
		return *(short *)p_a > *(short *)p_b ?  1 :
		       *(short *)p_a < *(short *)p_b ? -1 : 0;
	case FYTK_USHORT:
		return *(unsigned short *)p_a > *(unsigned short *)p_b ?  1 :
		       *(unsigned short *)p_a < *(unsigned short *)p_b ? -1 : 0;
	case FYTK_INT:
		return *(int *)p_a > *(int *)p_b ?  1 :
		       *(int *)p_a < *(int *)p_b ? -1 : 0;
	case FYTK_UINT:
		return *(unsigned int *)p_a > *(unsigned int *)p_b ?  1 :
		       *(unsigned int *)p_a < *(unsigned int *)p_b ? -1 : 0;
	case FYTK_LONG:
		return *(long *)p_a > *(long *)p_b ?  1 :
		       *(long *)p_a < *(long *)p_b ? -1 : 0;
	case FYTK_ULONG:
		return *(unsigned long *)p_a > *(unsigned long *)p_b ?  1 :
		       *(unsigned long *)p_a < *(unsigned long *)p_b ? -1 : 0;
	case FYTK_LONGLONG:
		return *(long long *)p_a > *(long long *)p_b ?  1 :
		       *(long long *)p_a < *(long long *)p_b ? -1 : 0;
	case FYTK_ULONGLONG:
		return *(unsigned long long *)p_a > *(unsigned long long *)p_b ?  1 :
		       *(unsigned long long *)p_a < *(unsigned long long *)p_b ? -1 : 0;
	case FYTK_FLOAT:
		return *(float *)p_a > *(float *)p_b ?  1 :
		       *(float *)p_a < *(float *)p_b ? -1 : 0;
	case FYTK_DOUBLE:
		return *(double *)p_a > *(double *)p_b ?  1 :
		       *(double *)p_a < *(double *)p_b ? -1 : 0;
	case FYTK_LONGDOUBLE:
		return *(long double *)p_a > *(long double *)p_b ?  1 :
		       *(long double *)p_a < *(long double *)p_b ? -1 : 0;
	default:
		break;
	}

	return -1;
}

static inline int
scalar_eq(enum fy_type_kind type_kind, const void *p_a, const void *p_b)
{
	switch (type_kind) {
	case FYTK_BOOL:
		return *(_Bool *)p_a == *(_Bool *)p_b;
	case FYTK_CHAR:
		return *(char *)p_a == *(char *)p_b;
	case FYTK_SCHAR:
		return *(signed char *)p_a == *(signed char *)p_b;
	case FYTK_UCHAR:
		return *(unsigned char *)p_a == *(unsigned char *)p_b;
	case FYTK_SHORT:
		return *(short *)p_a == *(short *)p_b;
	case FYTK_USHORT:
		return *(unsigned short *)p_a == *(unsigned short *)p_b;
	case FYTK_INT:
		return *(int *)p_a == *(int *)p_b;
	case FYTK_UINT:
		return *(unsigned int *)p_a == *(unsigned int *)p_b;
	case FYTK_LONG:
		return *(long *)p_a == *(long *)p_b;
	case FYTK_ULONG:
		return *(unsigned long *)p_a == *(unsigned long *)p_b;
	case FYTK_LONGLONG:
		return *(long long *)p_a == *(long long *)p_b;
	case FYTK_ULONGLONG:
		return *(unsigned long long *)p_a == *(unsigned long long *)p_b;
	case FYTK_FLOAT:
		return *(float *)p_a == *(float *)p_b;
	case FYTK_DOUBLE:
		return *(double *)p_a == *(double *)p_b;
	case FYTK_LONGDOUBLE:
		return *(long double *)p_a == *(long double *)p_b;
	default:
		break;
	}

	return -1;
}

static inline int
scalar_copy(enum fy_type_kind type_kind, const void *p_dst, const void *p_src)
{

	switch (type_kind) {
	case FYTK_BOOL:
		*(_Bool *)p_dst = *(_Bool *)p_src;
		return 0;
	case FYTK_CHAR:
		*(char *)p_dst = *(char *)p_src;
		return 0;
	case FYTK_SCHAR:
		*(signed char *)p_dst = *(signed char *)p_src;
		return 0;
	case FYTK_UCHAR:
		*(unsigned char *)p_dst = *(unsigned char *)p_src;
		return 0;
	case FYTK_SHORT:
		*(short *)p_dst = *(short *)p_src;
		return 0;
	case FYTK_USHORT:
		*(unsigned short *)p_dst = *(unsigned short *)p_src;
		return 0;
	case FYTK_INT:
		*(int *)p_dst = *(int *)p_src;
		return 0;
	case FYTK_UINT:
		*(unsigned int *)p_dst = *(unsigned int *)p_src;
		return 0;
	case FYTK_LONG:
		*(long *)p_dst = *(long *)p_src;
		return 0;
	case FYTK_ULONG:
		*(unsigned long *)p_dst = *(unsigned long *)p_src;
		return 0;
	case FYTK_LONGLONG:
		*(long long *)p_dst = *(long long *)p_src;
		return 0;
	case FYTK_ULONGLONG:
		*(unsigned long long *)p_dst = *(unsigned long long *)p_src;
		return 0;
	case FYTK_FLOAT:
		*(float *)p_dst = *(float *)p_src;
		return 0;
	case FYTK_DOUBLE:
		*(double *)p_dst = *(double *)p_src;
		return 0;
	case FYTK_LONGDOUBLE:
		*(long double *)p_dst = *(long double *)p_src;
		return 0;
	default:
		break;
	}

	return -1;
}

static int
common_scalar_copy(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	enum fy_type_kind type_kind;

	assert(rw_dst);
	assert(rw_src);
	assert(rw_dst->rtd);
	assert(rw_dst->rtd == rw_src->rtd);

	type_kind = rw_dst->rtd->ti->kind;
	/* non-bitfields always simple */
	if (!(rw_dst->flags & rw_src->flags & RWF_BITFIELD_DATA))
		return scalar_copy(type_kind, rw_dst->data, rw_src->data);

	/* only integers may be bitfield */
	assert(fy_type_kind_is_integer(type_kind));

	/* slower but handle bitfields */
	return store_integer_scalar_rw(rw_dst, load_integer_scalar_rw(rw_src));
}

static int
common_scalar_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	enum fy_type_kind type_kind;
	union integer_scalar val_a, val_b;

	assert(rw_a);
	assert(rw_b);
	assert(rw_a->rtd);
	assert(rw_a->rtd == rw_b->rtd);

	type_kind = rw_a->rtd->ti->kind;
	/* non-bitfields always simple */
	if (!(rw_a->flags & rw_b->flags & RWF_BITFIELD_DATA))
		return scalar_cmp(rw_a->rtd->ti->kind, rw_a->data, rw_b->data);

	/* only integers may be bitfield */
	assert(fy_type_kind_is_integer(type_kind));

	val_a = load_integer_scalar_rw(rw_a);
	val_b = load_integer_scalar_rw(rw_b);

	if (fy_type_kind_is_signed(type_kind))
		return val_a.sval > val_b.sval ?  1 :
		       val_a.sval < val_b.sval ? -1 : 0;
	else
		return val_a.uval > val_b.uval ?  1 :
		       val_a.uval < val_b.uval ? -1 : 0;
}

static int
common_scalar_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	enum fy_type_kind type_kind;
	union integer_scalar val_a, val_b;

	assert(rw_a);
	assert(rw_b);
	assert(rw_a->rtd);
	assert(rw_a->rtd == rw_b->rtd);

	type_kind = rw_a->rtd->ti->kind;
	if (!(rw_a->flags & rw_b->flags & RWF_BITFIELD_DATA))
		return scalar_eq(type_kind, rw_a->data, rw_b->data);

	/* only integers may be bitfield */
	assert(fy_type_kind_is_integer(type_kind));

	val_a = load_integer_scalar_rw(rw_a);
	val_b = load_integer_scalar_rw(rw_b);

	if (fy_type_kind_is_signed(type_kind))
		return val_a.sval == val_b.sval;
	else
		return val_a.uval == val_b.uval;
}

static int
null_scalar_emit(struct fy_emitter *emit, struct reflection_walker *rw,
		 enum reflection_emit_flags flags)
{
	const char *str;
	size_t len;
	int rc;

	str = "null";
	len = 4;

	rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_PLAIN, str, len, NULL, NULL);
	RE_ASSERT(!rc);

	return 0;

err_out:
	return -1;
}

/* NULLs are always equal to themselves */
static int
null_scalar_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return 0;
}

/* NULLs are always equal to themselves */
static int
null_scalar_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return 1;
}

/* nothing to copy */
static int
null_scalar_copy(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return 0;
}

/* walk down the dependency chain until we hit a non-dependent type */
struct reflection_type_data *
reflection_type_data_final_dependent(struct reflection_type_data *rtd)
{
	if (!rtd)
		return NULL;

	/* walk down dependent types until we get to the final */
	while (rtd->rtd_dep)
		rtd = rtd->rtd_dep;
	return rtd;
}

struct reflection_type_data *
reflection_type_data_resolved_typedef(struct reflection_type_data *rtd)
{
	if (!rtd)
		return NULL;

	while (rtd->rtd_dep && rtd->ti->kind == FYTK_TYPEDEF)
		rtd = rtd->rtd_dep;
	return rtd;
}

uintmax_t integer_field_load(struct reflection_field_data *rfd, const void *data)
{
	assert(rfd);

	if (rfd_is_bitfield(rfd))
		return load_bitfield_le(data, rfd->fi->bit_offset, rfd->fi->bit_width, rfd->is_signed);
	return load_le(data + rfd->fi->offset, rfd->fi->type_info->size, rfd->is_signed);
}

int unsigned_integer_field_store_check(struct reflection_field_data *rfd, uintmax_t v)
{
	uintmax_t maxv;
	int bit_width;

	assert(rfd);
	assert(rfd->is_unsigned);

	bit_width = !rfd_is_bitfield(rfd) ?
		    rfd->fi->type_info->size * 8 :
		    rfd->fi->bit_width;

	maxv = unsigned_integer_max_from_bit_width(bit_width);
	return v <= maxv ? 0 : -ERANGE;
}

int signed_integer_field_store_check(struct reflection_field_data *rfd, intmax_t v)
{
	intmax_t minv, maxv;
	int bit_width;

	assert(rfd);
	assert(rfd->is_signed);

	bit_width = !rfd_is_bitfield(rfd) ?
		    rfd->fi->type_info->size * 8 :
		    rfd->fi->bit_width;

	minv = signed_integer_min_from_bit_width(bit_width);
	maxv = signed_integer_max_from_bit_width(bit_width);
	return v >= minv && v <= maxv ? 0 : -ERANGE;
}

int integer_field_store_check(struct reflection_field_data *rfd, uintmax_t v)
{
	assert(rfd);

	return rfd->is_unsigned ? unsigned_integer_field_store_check(rfd, v) :
	       rfd->is_signed ? signed_integer_field_store_check(rfd, (intmax_t)v) :
	       -EINVAL;
}

void integer_field_store(struct reflection_field_data *rfd, uintmax_t v, void *data)
{
	assert(rfd);

	if (rfd_is_bitfield(rfd))
		store_bitfield_le(data, rfd->fi->bit_offset, rfd->fi->bit_width, v);
	else
		store_le(data + rfd->fi->offset, rfd->fi->type_info->size, v);
}

static inline void
struct_setup_field_rw(struct reflection_walker *rw, struct reflection_field_data *rfd)
{
	void *data;
	size_t bit_offset;

	assert(rw);
	assert(rw->parent);
	assert(rw->parent->rtd->ti->kind == FYTK_STRUCT ||
	       rw->parent->rtd->ti->kind == FYTK_UNION);

	data = rw->parent->data;

	rw->rtd = rfd_rtd(rfd);
	rw->idx = rfd->idx;
	rw->flags |= RWF_FIELD_IDX;

	if (!rfd_is_bitfield(rfd)) {
		rw->data = data + (data ? rfd->fi->offset : 0);
		rw->data_size.bytes = rw->rtd->ti->size;
	} else {
		rw->flags |= RWF_BITFIELD_DATA;

		bit_offset = rfd->fi->bit_offset;
		rw->data = data + (data ? (bit_offset >> 3) : 0);
		rw->data_size.bit_offset = bit_offset & 7;
		rw->data_size.bit_width = rfd->fi->bit_width;
	}
}

static inline uintmax_t
struct_integer_field_load(struct reflection_walker *rw_struct, struct reflection_field_data *rfd)
{
	return integer_field_load(rfd, rw_struct->data);
}

static inline void
struct_integer_field_store(struct reflection_walker *rw_struct, struct reflection_field_data *rfd, uintmax_t v)
{
	return integer_field_store(rfd, v, rw_struct->data);
}

/* instance datas */
struct struct_instance_data {
	const char *anonymous_field_name;
	uint64_t *field_present;
	uint64_t field_present_inl[16];
};

static int
struct_instance_data_setup(struct struct_instance_data *id, struct reflection_walker *rw)
{
	size_t size;

	assert(id);
	assert(rw);

	memset(id, 0, sizeof(*id));
	size = ((rw->rtd->fields_count + 63) / 64) * sizeof(uint64_t);
	if (size <= sizeof(id->field_present_inl))
		id->field_present = id->field_present_inl;
	else {
		id->field_present = malloc(size);
		RW_ASSERT(id->field_present);
		memset(id->field_present, 0, size);
	}
	rw->user = id;

	return 0;

err_out:
	return -1;
}

static void
struct_instance_data_cleanup(struct struct_instance_data *id)
{
	if (!id)
		return;

	if (id->field_present && id->field_present != id->field_present_inl)
		free(id->field_present);
}

static inline bool
struct_is_field_present(struct struct_instance_data *id, int field_idx)
{
	return !!(id->field_present[field_idx / 64] & ((uint64_t)1 << (field_idx & 63)));
}

static inline void
struct_set_field_present(struct struct_instance_data *id, int field_idx)
{
	id->field_present[field_idx / 64] |= ((uint64_t)1 << (field_idx & 63));
}

static inline void
struct_clear_field_present(struct struct_instance_data *id, int field_idx)
{
	id->field_present[field_idx / 64] &= ~((uint64_t)1 << (field_idx & 63));
}

static bool
struct_field_is_ptr_null(struct reflection_walker *rw, struct reflection_field_data *rfd)
{
	struct reflection_type_data *rtd;

	assert(rfd);
	assert(rw);
	assert(rw->rtd);
	assert(rw->rtd->ti->kind == FYTK_STRUCT || rw->rtd->ti->kind == FYTK_UNION);
	assert(rfd->rtd_parent == rw->rtd);

	rtd = rfd_rtd(rfd);
	if (rtd->ti->kind != FYTK_PTR)
		return false;

	/* bit field can't store a pointer */
	assert(!rfd_is_bitfield(rfd));
	return *(const void **)(rw->data + rfd->fi->offset) == NULL;
}

struct struct_instance_data *
struct_get_parent_struct_id(struct reflection_walker *rw)
{
	if (!rw || !rw->parent ||
	    (rw->parent->rtd->ti->kind != FYTK_STRUCT && rw->parent->rtd->ti->kind == FYTK_UNION))
		return NULL;

	return rw->parent->user;
}

static const struct reflection_type_ops dyn_array_ops;
static const struct reflection_type_ops dyn_map_ops;
static const struct reflection_type_ops const_map_ops;

enum array_type {
	AT_UNKNOWN,
	AT_DYNAMIC_ARRAY,
	AT_DYNAMIC_MAP,
	AT_CONST_ARRAY,
	AT_CONST_MAP,
};

static inline bool
array_type_is_dynamic(enum array_type type)
{
	return type == AT_DYNAMIC_ARRAY || type == AT_DYNAMIC_MAP;
}

static inline bool
array_type_is_const(enum array_type type)
{
	return type == AT_CONST_ARRAY || type == AT_CONST_MAP;
}

static inline bool
array_type_is_mapping(enum array_type type)
{
	return type == AT_DYNAMIC_MAP || type == AT_CONST_MAP;
}

struct array_info {
	enum array_type type;
	struct reflection_walker *rw;
	struct reflection_type_data *rtd, *rtd_dep, *rtd_final_dep;
	struct reflection_walker *rw_struct, *rw_field;
	struct reflection_field_data *rfd, *rfd_counter, *rfd_key, *rfd_value;
	struct reflection_walker rw_terminator, rw_fill;
	void *value_terminator, *value_fill;
	uintmax_t count;
	enum fy_event_type start_type, end_type;
	bool has_count : 1;
	bool has_terminator : 1;
	bool has_terminator_value : 1;
	bool dep_is_ptr : 1;
	bool dep_is_moveable : 1;
	bool has_fill : 1;
};

int array_info_setup(struct array_info *info, struct reflection_walker *rw)
{
	enum array_type type;
	struct reflection_field_data *rfd;
	struct reflection_type_data *rtd_final_dep, *rtdf;
	const char *counter;
	const char *key;
	void *terminator_value;

	assert(rw);
	assert(rw->rtd);
	assert(rw->rtd->rtd_dep);

	memset(info, 0, sizeof(*info));

	if (rw->rtd->ops == &dyn_array_ops)
		type = AT_DYNAMIC_ARRAY;
	else if (rw->rtd->ops == &dyn_map_ops)
		type = AT_DYNAMIC_MAP;
	else if (rw->rtd->ops == &reflection_ops_table[FYTK_CONSTARRAY])
		type = AT_CONST_ARRAY;
	else if (rw->rtd->ops == &const_map_ops)
		type = AT_CONST_MAP;
	else
		type = AT_UNKNOWN;

	assert(type != AT_UNKNOWN);

	info->type = type;
	info->rw = rw;
	info->rtd = rw->rtd;

	if (!array_type_is_mapping(info->type)) {
		info->start_type = FYET_SEQUENCE_START;
		info->end_type = FYET_SEQUENCE_END;
	} else {
		info->start_type = FYET_MAPPING_START;
		info->end_type = FYET_MAPPING_END;
	}

	info->rtd_dep = info->rtd->rtd_dep;
	info->dep_is_ptr = info->rtd_dep->ti->kind == FYTK_PTR;
	info->dep_is_moveable = info->dep_is_ptr || (info->rtd_dep->flags & RTDF_PURITY_MASK) == 0;

	rfd = reflection_get_field(rw, &info->rw_field, NULL);
	if (rfd) {
		info->rfd = rfd;
		rtdf = rfd_rtd(rfd);
	} else {
		rtdf = NULL;
	}

	if (rfd && info->rw_field)
		info->rw_struct = info->rw_field->parent;

	if (!rtdf)
		return 0;

	counter = NULL;
	terminator_value = NULL;

	if (info->rw_struct &&
	   (counter = reflection_meta_get_str(rtdf->meta, rmvid_counter)) != NULL) {
		info->rfd_counter = reflection_type_data_lookup_field(rfd->rtd_parent, counter);
		if (info->rfd_counter) {
			info->count = struct_integer_field_load(info->rw_struct, info->rfd_counter);
			info->has_count = true;
		}
	}

	info->has_terminator = type == AT_DYNAMIC_ARRAY || type == AT_DYNAMIC_MAP;

	if (info->rtd_dep)
		terminator_value = reflection_meta_generate_any_value(info->rtd->meta, rmvid_terminator, info->rtd_dep);

	if (info->has_count && !terminator_value)
		info->has_terminator = false;

	info->has_terminator_value = info->has_terminator && terminator_value;

	if (terminator_value)
		reflection_rw_value(&info->rw_terminator, info->rtd_dep, terminator_value);

	if (array_type_is_mapping(info->type)) {

		key = reflection_meta_get_str(info->rtd->meta, rmvid_key);

		RW_ASSERT(key);

		rtd_final_dep = reflection_type_data_final_dependent(info->rtd);

		RW_ASSERT(rtd_final_dep && rtd_final_dep->ti->kind == FYTK_STRUCT &&
			  rtd_final_dep->fields_count == 2);

		info->rfd_key = reflection_type_data_lookup_field(rtd_final_dep, key);
		RW_ASSERT(info->rfd_key);
		RW_ASSERT(info->rfd_key->idx >= 0 && info->rfd_key->idx <= 1);

		info->rfd_value = rtd_final_dep->fields[!info->rfd_key->idx];
		RW_ASSERT(info->rfd_value);
	}

	if (array_type_is_const(info->type) &&
	    (info->value_fill = reflection_meta_generate_any_value(info->rtd->meta,
					rmvid_fill, info->rtd_dep)) != NULL) {
		info->has_fill = true;
		reflection_rw_value(&info->rw_fill, info->rtd_dep, info->value_fill);
	}

	return 0;

err_out:
	return -1;
}

void array_info_cleanup(struct array_info *info)
{
	if (!info)
		return;
}

struct array_mapping_info {
	struct array_info *info;
	int depth;
	struct reflection_walker *rw_deps;
	struct reflection_walker rw_deps_inl[8];
	struct struct_instance_data id_struct;
	struct reflection_walker *rw_last_dep;
};

static int
array_mapping_info_setup(struct array_mapping_info *minfo, struct array_info *info,
			 struct reflection_walker *rw)
{
	struct reflection_type_data *rtdt;
	struct reflection_walker *rw_last_dep;
	struct reflection_walker *rw_deps, *rw_dep;
	size_t size;
	int depth;

	assert(minfo);
	assert(info);
	assert(rw);

	memset(minfo, 0, sizeof(*minfo));
	minfo->info = info;

	if (!array_type_is_mapping(info->type))
		return 0;

	depth = 0;
	rtdt = info->rtd_dep->rtd_dep;
	while (rtdt != NULL) {
		depth++;
		rtdt = rtdt->rtd_dep;
	}
	minfo->depth = depth;

	rw_last_dep = rw;

	if (depth > 0) {

		size = sizeof(*rw_deps) * depth;
		if (size <= sizeof(minfo->rw_deps_inl))
			rw_deps = minfo->rw_deps_inl;
		else {
			rw_deps = malloc(sizeof(*rw_deps) * depth);
			RW_ASSERT(rw_deps);
			memset(rw_deps, 0, sizeof(*rw_deps) * depth);
		}
		minfo->rw_deps = rw_deps;

		for (rw_dep = rw_deps, rtdt = info->rtd_dep->rtd_dep;
		     rtdt != NULL; rtdt = rtdt->rtd_dep, rw_dep++) {
			rw_dep->parent = rw_last_dep;

			rw_dep->rtd = rtdt;
			rw_dep->data_size.bytes = rtdt->ti->size;

			rw_last_dep = rw_dep;
		}
	}

	minfo->rw_last_dep = rw_last_dep;
	assert(minfo->rw_last_dep->rtd->ti->kind == FYTK_STRUCT);

	return 0;

err_out:
	if (minfo->rw_deps && minfo->rw_deps != minfo->rw_deps_inl)
		free(minfo->rw_deps);
	return -1;
}

static void
array_mapping_info_cleanup(struct array_mapping_info *minfo)
{
	if (!minfo)
		return;
	if (minfo->rw_deps && minfo->rw_deps != minfo->rw_deps_inl)
		free(minfo->rw_deps);
}

static int
array_mapping_info_parse_item_prolog(struct array_mapping_info *minfo, struct reflection_walker *rw,
				     enum reflection_parse_flags flags)
{
	struct reflection_walker *rw_deps, *rw_last_dep, *rw_dep;
	void *last_data, *ptr;
	int i, depth;

	assert(minfo);
	assert(rw);

	depth = minfo->depth;
	rw_deps = minfo->rw_deps;
	rw_last_dep = minfo->rw_last_dep;

	last_data = rw->data;
	for (i = 0; i < depth + 1; i++) {

		assert(i == 0 || rw_deps != NULL);

		rw_dep = i == 0 ? rw : &rw_deps[i - 1];

		switch (rw_dep->rtd->ti->kind) {
		case FYTK_PTR:
			ptr = reflection_type_data_alloc(rw_dep->rtd->rtd_dep);
			RW_ASSERT(ptr);

			rw_dep->data = last_data;
			if (!(flags & RPF_NO_STORE))
				*(void **)rw_dep->data = ptr;
			last_data = ptr;
			break;
		case FYTK_TYPEDEF:
			rw_dep->data = last_data;
			break;
		default:
			RW_ASSERT(rw_dep == rw_last_dep);
			RW_ASSERT(i == depth);
			rw_last_dep->data = last_data;
			break;
		}
	}

	return 0;

err_out:
	while (i > 1) {
		i--;
		rw_dep = i == 0 ? rw : &rw_deps[i - 1];
		switch (rw_dep->rtd->ti->kind) {
		case FYTK_PTR:
			ptr = *(void **)rw_dep->data;
			reflection_type_data_free(rw_dep->rtd->rtd_dep, ptr);
			break;
		default:
			break;
		}
	}
	return -1;
}

static int
array_mapping_info_parse_item_epilog(struct array_mapping_info *minfo, struct reflection_walker *rw,
				     enum reflection_parse_flags flags, bool force_release)
{
	struct reflection_walker *rw_deps, *rw_dep;
	void *ptr;
	int i, depth;

	depth = minfo->depth;
	rw_deps = minfo->rw_deps;

	if ((flags & RPF_NO_STORE) || force_release) {

		for (i = 0; i < depth + 1; i++) {

			rw_dep = i == 0 ? rw : &rw_deps[i - 1];

			switch (rw_dep->rtd->ti->kind) {
			case FYTK_PTR:
				ptr = *(void **)rw_dep->data;
				reflection_type_data_free(rw_dep->rtd->rtd_dep, ptr);
				break;
			default:
				break;
			}
		}
	}

	/* erase pointers */
	for (i = 0; i < depth + 1; i++) {
		rw_dep = i == 0 ? rw : &rw_deps[i - 1];
		rw_dep->data = NULL;
	}

	return 0;
}

static int
array_info_load_item_count(struct array_info *info, void *data)
{
	struct reflection_walker rw_item;
	void *datat;
	size_t item_size;
	uintmax_t count;
	int rc;

	assert(info);

	if (info->has_count)
		return 0;

	assert(data);

	rc = -1;
	count = 0;

	if (info->has_terminator) {

		item_size = info->rtd_dep->ti->size;

		memset(&rw_item, 0, sizeof(rw_item));
		rw_item.parent = info->rw;
		rw_item.rtd = info->rtd_dep;
		rw_item.data_size.bytes = item_size;
		rw_item.flags = RWF_SEQ_IDX;

		for (datat = data, count = 0; ; datat += item_size) {

			if (count > INT_MAX)
				return -1;

			if (info->dep_is_ptr && *(const void **)datat == NULL)
				break;

			if (info->has_terminator_value) {
				rw_item.idx = count;
				rw_item.data = datat;

				if (reflection_eq_rw(&info->rw_terminator, &rw_item) > 0)
					break;
			} else {
				if (memiszero(datat, item_size))
					break;
			}

			count++;
		}
		rc = 0;
		goto out;
	}

	if (array_type_is_const(info->type)) {
		count = info->rw->rtd->ti->count;
		rc = 0;
		goto out;
	}

out:
	if (!rc) {
		info->count = count;
		info->has_count = true;
	}

	return rc;
}

static int
array_info_store_item_count(struct array_info *info, void *data, uintmax_t count)
{
	struct reflection_walker rw_item;
	size_t item_size;
	int rc;

	if (info->rfd_counter) {
		rc = integer_field_store_check(info->rfd_counter, count);
		if (rc)
			return -ERANGE;
	}

	info->count = count;
	info->has_count = true;

	if (info->rfd_counter)
		struct_integer_field_store(info->rw_struct, info->rfd_counter, info->count);

	if (info->has_terminator || info->has_fill) {
		item_size = info->rtd_dep->ti->size;

		memset(&rw_item, 0, sizeof(rw_item));
		rw_item.rtd = info->rtd_dep;
		rw_item.data_size.bytes = item_size;
		rw_item.parent = info->rw;
		rw_item.flags = RWF_SEQ_IDX;
	}

	if (info->has_terminator) {

		if (info->has_terminator_value) {

			rw_item.idx = count;
			rw_item.data = data + count * item_size;
			count++;

			rc = reflection_copy_rw(&rw_item, &info->rw_terminator);
			if (rc)
				return rc;
		} else
			memset(data + info->count * item_size, 0, item_size);
	}

	if (info->has_fill) {

		while (count < info->rtd->ti->count) {
			rw_item.idx = count;
			rw_item.data = data + count * item_size;
			count++;

			rc = reflection_copy_rw(&rw_item, &info->rw_fill);
			if (rc)
				return rc;
		}
	}

	return 0;
}

static int
array_mapping_info_emit_item_prolog(struct array_mapping_info *minfo, struct reflection_walker *rw_item,
				    enum reflection_emit_flags flags)
{
	struct reflection_walker *rw_deps, *rw_last_dep, *rw_dep;
	void *ptr, *last_data = NULL;
	int depth, i;

	assert(minfo);
	assert(rw_item);

	depth = minfo->depth;
	rw_deps = minfo->rw_deps;
	rw_last_dep = minfo->rw_last_dep;

	assert(rw_last_dep != NULL);

	last_data = rw_item->data;
	for (i = 0, rw_dep = rw_deps; i < depth + 1; i++, rw_dep++) {

		assert(i == 0 || rw_deps != NULL);

		rw_dep = i == 0 ? rw_item : &rw_deps[i - 1];

		switch (rw_dep->rtd->ti->kind) {
		case FYTK_PTR:
			assert(last_data);
			rw_dep->data = last_data;
			ptr = *(void **)rw_dep->data;
			assert(ptr);
			last_data = ptr;
			break;
		case FYTK_TYPEDEF:
			rw_dep->data = last_data;
			break;
		default:
			assert(rw_dep == rw_last_dep);
			assert(i == depth);
			rw_last_dep->data = last_data;
			break;
		}
	}

	assert(rw_last_dep->data != NULL);

	return 0;
}

static int
array_mapping_info_emit_item_epilog(struct array_mapping_info *minfo, struct reflection_walker *rw_item,
				    enum reflection_emit_flags flags)
{
	struct reflection_walker *rw_deps, *rw_dep;
	int i, depth;

	depth = minfo->depth;
	rw_deps = minfo->rw_deps;

	/* erase pointers */
	for (i = 0; i < depth + 1; i++) {
		rw_dep = i == 0 ? rw_item : &rw_deps[i - 1];
		rw_dep->data = NULL;
	}

	return 0;
}

static int
array_parse(struct array_info *info, struct fy_parser *fyp, struct reflection_walker *rw,
	    enum reflection_parse_flags flags)
{
	struct fy_event *fye = NULL;
	const struct fy_event *fye_peek;
	struct reflection_walker rw_item, rw_key, rw_value, *rw_struct = NULL;
	void *data = NULL, *data_new;
	size_t item_size;
	uintmax_t item_count = 0, idx = 0, data_count = 0, new_item_count, new_data_count;
	struct struct_instance_data id_struct_local, *id_struct = &id_struct_local;
	struct array_mapping_info minfo_local, *minfo = &minfo_local;
	bool have_map_prolog = false, have_map_key = false, have_map_value = false;
	int rc = 0;

	item_size = info->rtd_dep->ti->size;

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

	RP_INPUT_CHECK(fye->type == info->start_type,
			"illegal event type (expecting %s start)",
			!array_type_is_mapping(info->type) ? "sequence" : "mapping");

	fy_parser_event_free(fyp, fye);
	fye = NULL;

	if (array_type_is_dynamic(info->type)) {
		if (!info->dep_is_moveable) {
			rc = !array_type_is_mapping(info->type) ?
				fy_parser_count_sequence_items(fyp) :
				fy_parser_count_mapping_items(fyp);

			RP_ASSERT(rc >= 0);

			item_count = (uintmax_t)rc;
		} else
			item_count = 16;	// XXX just initial guess
	} else if (array_type_is_const(info->type)) {
		item_count = info->rtd->ti->count;
	} else {
		RP_ASSERT(false);
	}

	data_count = item_count + info->has_terminator;

	if (array_type_is_dynamic(info->type)) {
		data = reflection_malloc(info->rtd->rts, data_count * item_size);
		RP_ASSERT(data != NULL);

		memset(data, 0, data_count * item_size);
	} else {
		data = info->rw->data;
	}

	memset(&rw_item, 0, sizeof(rw_item));
	rw_item.parent = info->rw;
	rw_item.rtd = info->rtd_dep;
	rw_item.data_size.bytes = item_size;
	rw_item.flags = RWF_SEQ_IDX;
	if (!array_type_is_mapping(info->type))	/* for regular arrays, the idx is the key */
		rw_item.flags |= RWF_SEQ_KEY;
	else
		rw_item.flags |= RWF_MAP;

	if (array_type_is_mapping(info->type)) {
		rc = array_mapping_info_setup(minfo, info, &rw_item);
		RP_ASSERT(!rc);

		if (minfo->rw_last_dep) {
			rc = struct_instance_data_setup(id_struct, minfo->rw_last_dep);
			RP_ASSERT(!rc);
		}
		rw_struct = minfo->rw_last_dep;
		assert(rw_struct);

		memset(&rw_key, 0, sizeof(rw_key));
		rw_key.parent = rw_struct;
		rw_key.flags = RWF_KEY;

		memset(&rw_value, 0, sizeof(rw_value));
		rw_value.parent = rw_struct;
		rw_value.flags = RWF_VALUE | RWF_COMPLEX_KEY;
		rw_value.rw_key = &rw_key;
	}

	for (idx = 0; ; idx++) {

		fye_peek = fy_parser_parse_peek(fyp);
		RP_INPUT_CHECK(fye_peek != NULL,
			       "premature end of input while scanning for items\n");

		RP_INPUT_CHECK(fye_peek->type == FYET_SCALAR ||
			       fye_peek->type == FYET_MAPPING_START ||
			       fye_peek->type == FYET_SEQUENCE_START ||
			       fye_peek->type == info->end_type,
			       "invalid event type");

		if (fye_peek->type == info->end_type)
			break;

		if (info->rfd_counter) {
			rc = integer_field_store_check(info->rfd_counter, idx);
			RP_INPUT_CHECK(!rc, "Too many items (%ju) for given counter field %s",
				       idx, info->rfd_counter->fi->name);
		}

		if (idx  >= data_count) {

			RP_INPUT_CHECK(!array_type_is_const(info->type),
					"Too many items (%ju) for fixed array (%ju)",
				       idx + 1, item_count);

			/* should never get here when moveable */
			assert(!info->dep_is_moveable);

			/* grow */
			new_item_count = item_count * 2;
			new_data_count = new_item_count + info->has_terminator;

			data_new = reflection_realloc(info->rtd->rts, data, new_data_count * item_size);
			RP_ASSERT(data_new != NULL);

			memset(data_new + data_count * item_size, 0, (new_data_count - data_count) * item_size);

			item_count = new_item_count;
			data_count = new_data_count;
			data = data_new;
		}

		rw_item.idx = idx;
		rw_item.data = data + idx * item_size;

		if (!array_type_is_mapping(info->type)) {

			rc = reflection_parse_rw(fyp, &rw_item, flags);
			if (rc)
				goto out;
		} else {
			struct_clear_field_present(id_struct, info->rfd_key->idx);
			struct_clear_field_present(id_struct, info->rfd_value->idx);

			rc = array_mapping_info_parse_item_prolog(minfo, &rw_item, flags);
			RP_ASSERT(!rc);
			have_map_prolog = true;

			struct_setup_field_rw(&rw_key, info->rfd_key);
			rc = reflection_parse_rw(fyp, &rw_key, flags);
			if (rc)
				goto out;
			have_map_key = true;
			struct_set_field_present(id_struct, info->rfd_key->idx);

			struct_setup_field_rw(&rw_value, info->rfd_value);
			rc = reflection_parse_rw(fyp, &rw_value, flags);
			if (rc)
				goto out;
			have_map_value = true;
			struct_set_field_present(id_struct, info->rfd_value->idx);

			rc = array_mapping_info_parse_item_epilog(minfo, &rw_item, flags, false);
			RP_ASSERT(!rc);

			have_map_prolog = have_map_key = have_map_value = false;
		}
	}
	rc = 0;

	item_count = idx;
	data_count = item_count + info->has_terminator;

	if (array_type_is_dynamic(info->type) && info->dep_is_moveable) {
		/* trim the excess */
		data_new = reflection_realloc(info->rtd->rts, data, data_count * item_size);
		RP_ASSERT(data_new != NULL);

		data = data_new;
	}

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

	/* pull the last SEQUENCE_END */
	RP_INPUT_CHECK(fye->type == info->end_type,
		       "invalid event type while expecting %s end (got %s)",
			!array_type_is_mapping(info->type) ? "sequence" : "mapping",
		       fy_event_type_get_text(fye->type));

	/* and commit */
	if (!(flags & RPF_NO_STORE)) {
		rc = array_info_store_item_count(info, data, item_count);
		RP_ASSERT(!rc);

		if (array_type_is_dynamic(info->type))
			*(void **)info->rw->data = data;
	} else {
		if (array_type_is_dynamic(info->type))
			reflection_free(info->rtd->rts, data);
	}

	fy_parser_event_free(fyp, fye);

	return rc;

out:
	if (data) {
		if (have_map_prolog) {
			if (have_map_value)
				reflection_dtor_rw(&rw_value);
			if (have_map_key)
				reflection_dtor_rw(&rw_key);
			array_mapping_info_parse_item_epilog(minfo, &rw_item, flags, true);
		}
		while (idx) {
			rw_item.idx = --idx;
			rw_item.data = data + idx * item_size;
			reflection_dtor_rw(&rw_item);
			if (array_type_is_const(info->type))
				memset(rw_item.data, 0, item_size);
		}
		if (array_type_is_dynamic(info->type))
			reflection_free(info->rtd->rts, data);
	}

	fy_parser_event_free(fyp, fye);
	if (array_type_is_mapping(info->type)) {
		if (minfo->rw_last_dep)
			struct_instance_data_cleanup(id_struct);
		array_mapping_info_cleanup(minfo);
	}
	return rc;

err_out:
	rc = -1;
	goto out;

err_input:
	rc = -EINVAL;
	goto out;
}

static int
array_emit(struct array_info *info, struct fy_emitter *emit,
	   struct reflection_walker *rw, enum reflection_emit_flags flags)
{
	struct reflection_walker rw_item, rw_key, rw_value, *rw_struct = NULL;
	void *data;
	uintmax_t idx, count;
	size_t item_size;
	struct array_mapping_info minfo_local, *minfo = &minfo_local;
	int rc;

	if (array_type_is_dynamic(info->type))
		data = *(void **)info->rw->data;
	else
		data = info->rw->data;

	item_size = info->rtd_dep->ti->size;

	memset(&rw_item, 0, sizeof(rw_item));
	rw_item.parent = info->rw;
	rw_item.rtd = info->rtd_dep;
	rw_item.data_size.bytes = item_size;
	rw_item.flags = RWF_SEQ_IDX;
	if (!array_type_is_mapping(info->type))	/* for regular arrays, the idx is the key */
		rw_item.flags |= RWF_SEQ_KEY;

	if (array_type_is_mapping(info->type)) {
		rc = array_mapping_info_setup(minfo, info, &rw_item);
		RE_ASSERT(!rc);

		rw_struct = minfo->rw_last_dep;
		assert(rw_struct);

		memset(&rw_key, 0, sizeof(rw_key));
		rw_key.parent = rw_struct;
		rw_key.flags = RWF_KEY;

		memset(&rw_value, 0, sizeof(rw_value));
		rw_value.parent = rw_struct;
		rw_value.flags = RWF_VALUE | RWF_COMPLEX_KEY;
		rw_value.rw_key = &rw_key;
	}

	if (data) {
		rc = array_info_load_item_count(info, data);
		RE_ASSERT(!rc);
		RE_ASSERT(info->has_count);
		count = info->count;
	} else {
		count = 0;
	}

	/* do not emit if empty and marked as such (and we're not in a key, value pair) */
	if (reflection_meta_get_bool(info->rtd->meta, rmvid_omit_if_empty)) {
		if (!count && !(info->rw->flags & (RWF_VALUE | RWF_KEY)))
			goto out;
	}

	rc = fy_emit_eventf(emit, info->start_type, FYNS_ANY, NULL, NULL);
	RE_ASSERT(!rc);

	for (idx = 0; idx < count; idx++, data += item_size) {

		rw_item.idx = idx;
		rw_item.data = data;

		if (!array_type_is_mapping(info->type)) {
			rc = reflection_emit_rw(emit, &rw_item, flags);
			RE_ASSERT(!rc);
		} else {

			rc = array_mapping_info_emit_item_prolog(minfo, &rw_item, flags);
			RE_ASSERT(!rc);

			struct_setup_field_rw(&rw_key, info->rfd_key);
			rc = reflection_emit_rw(emit, &rw_key, flags);
			RE_ASSERT(!rc);

			struct_setup_field_rw(&rw_value, info->rfd_value);
			rc = reflection_emit_rw(emit, &rw_value, flags);
			RE_ASSERT(!rc);

			rc = array_mapping_info_emit_item_epilog(minfo, &rw_item, flags);
			RE_ASSERT(!rc);
		}
	}

	rc = fy_emit_eventf(emit, info->end_type);
	RE_ASSERT(!rc);
	rc = 0;
out:
	if (array_type_is_mapping(info->type))
		array_mapping_info_cleanup(minfo);
	return rc;

err_out:
	rc = -1;
	goto out;
}

static void
array_dtor(struct array_info *info)
{
	struct reflection_walker rw_item;
	uintmax_t idx, count;
	void *p, *data;
	size_t item_size;
	bool is_terminator;

	if (array_type_is_dynamic(info->type))
		data = *(void **)info->rw->data;
	else
		data = info->rw->data;

	if (!data)
		return;

	if (!reflection_type_data_has_dtor(info->rtd_dep))
		goto out_free;

	if (info->has_count)
		count = info->count;
	else if (info->has_terminator)
		count = INT_MAX;
	else if (array_type_is_const(info->type))
		count = info->rw->rtd->ti->count;
	else
		count = UINTMAX_MAX;

	assert(count != UINTMAX_MAX);

	item_size = info->rtd_dep->ti->size;

	memset(&rw_item, 0, sizeof(rw_item));
	rw_item.parent = info->rw;
	rw_item.rtd = info->rtd_dep;
	rw_item.data_size.bytes = item_size;
	rw_item.flags = RWF_SEQ_IDX;
	if (!array_type_is_mapping(info->type))	/* for regular arrays, the idx is the key */
		rw_item.flags |= RWF_SEQ_KEY;

	/* free in sequence */
	is_terminator = false;
	for (idx = 0, p = data; idx < count; idx++, p += item_size) {

		/* do not follow NULLs */
		if (info->dep_is_ptr && *(const void **)p == NULL)
			break;

		rw_item.idx = idx;
		rw_item.data = p;

		if (info->has_terminator_value)
			is_terminator = reflection_eq_rw(&info->rw_terminator, &rw_item) > 0;
		else if (info->has_terminator)
			is_terminator = memiszero(p, item_size);

		reflection_dtor_rw(&rw_item);

		if (is_terminator)
			break;
	}

out_free:
	if (array_type_is_dynamic(info->type))
		reflection_free(info->rtd->rts, data);
}

static int
array_copy(struct array_info *info_dst, struct array_info *info_src)
{
	struct reflection_walker *rw;
	struct reflection_walker rw_item_dst, rw_item_src;
	void *ptr_dst = NULL, *dst, *ptr_src = NULL, *src;
	size_t item_size;
	uintmax_t idx, data_count, count = 0;
	int rc;

	assert(info_dst);
	assert(info_src);

	rw = info_src->rw;
	assert(rw);

	RW_ASSERT(info_src->rtd == info_dst->rtd);

	if (array_type_is_dynamic(info_src->type))
		ptr_src = *(void **)info_src->rw->data;
	else if (array_type_is_const(info_src->type))
		ptr_src = info_src->rw->data;
	else
		ptr_src = NULL;

	RW_ASSERT(ptr_src);

	if (!ptr_src) {
		if (array_type_is_dynamic(info_dst->type))
			*(void **)info_dst->rw->data = NULL;
		return 0;
	}

	if (array_type_is_dynamic(info_dst->type))
		assert(*(void **)info_dst->rw->data == NULL);

	rc = array_info_load_item_count(info_src, ptr_src);
	RW_ASSERT(!rc);

	count = (int)info_src->count;

	item_size = info_dst->rtd_dep->ti->size;

	memset(&rw_item_dst, 0, sizeof(rw_item_dst));
	memset(&rw_item_src, 0, sizeof(rw_item_src));

	rw_item_dst.data_size.bytes = rw_item_src.data_size.bytes = item_size;
	rw_item_dst.flags = rw_item_src.flags = RWF_SEQ_IDX;
	if (!array_type_is_mapping(info_dst->type)) {	/* for regular arrays, the idx is the key */
		rw_item_dst.flags |= RWF_SEQ_KEY;
		rw_item_src.flags |= RWF_SEQ_KEY;
	}

	rw_item_dst.parent = info_dst->rw;
	rw_item_dst.rtd = info_dst->rtd;

	rw_item_src.parent = info_src->rw;
	rw_item_src.rtd = info_src->rtd;

	/* make sure that the counter can fit */
	if (info_dst->rfd_counter) {
		rc = integer_field_store_check(info_dst->rfd_counter, info_src->count);
		RW_ASSERT(!rc);
	}

	data_count = info_src->count + info_dst->has_terminator;

	if (array_type_is_dynamic(info_dst->type)) {
		ptr_dst = reflection_malloc(info_dst->rw->rtd->rts, data_count * item_size);
		RW_ASSERT(ptr_dst);
	} else
		ptr_dst = info_dst->rw->data;

	for (idx = 0, dst = ptr_dst, src = ptr_src; idx < count;
	     idx++, dst += item_size, src += item_size) {

		rw_item_dst.idx = rw_item_src.idx = idx;

		rw_item_dst.data = dst;
		rw_item_src.data = src;

		rc = reflection_copy_rw(&rw_item_dst, &rw_item_src);
		RW_ASSERT(!rc);
	}

	rc = array_info_store_item_count(info_dst, ptr_dst, info_src->count);
	RW_ASSERT(!rc);

	rc = 0;

out:
	if (array_type_is_dynamic(info_dst->type))
		*(void **)info_dst->rw->data = ptr_dst;

	return rc;

err_out:
	if (ptr_dst) {
		if (idx > 0) {
			assert(dst);
			while (idx--) {
				dst -= item_size;
				rw_item_dst.idx = idx;
				rw_item_dst.data = dst;

				reflection_dtor_rw(&rw_item_dst);
			}
		}
		if (array_type_is_dynamic(info_dst->type)) {
			reflection_free(info_dst->rw->rtd->rts, ptr_dst);
			ptr_dst = NULL;
		}
	}
	rc = -1;
	goto out;
}

static int
array_cmp(struct array_info *info_a, struct array_info *info_b)
{
	struct reflection_walker *rw;
	struct reflection_walker rw_item_a, rw_item_b;
	void *ptr_a, *ptr_b;
	size_t item_size;
	uintmax_t idx, count;
	int rc;

	assert(info_a);
	assert(info_b);

	rw = info_a->rw;
	assert(rw);

	RW_ASSERT(info_a->rtd == info_b->rtd);

	if (array_type_is_dynamic(info_a->type))
		ptr_a = *(void **)info_a->rw->data;
	else if (array_type_is_const(info_a->type))
		ptr_a = info_a->rw->data;
	else
		ptr_a = NULL;

	RW_ASSERT(ptr_a);

	if (array_type_is_dynamic(info_b->type))
		ptr_b = *(void **)info_b->rw->data;
	else if (array_type_is_const(info_b->type))
		ptr_b = info_b->rw->data;
	else
		ptr_b = NULL;

	RW_ASSERT(ptr_b);

	if (ptr_a == ptr_b)
		return 0;

	if (!ptr_a || !ptr_b)
		return -1;

	item_size = info_a->rtd_dep->ti->size;

	rc = array_info_load_item_count(info_a, ptr_a);
	RW_ASSERT(!rc);

	rc = array_info_load_item_count(info_b, ptr_b);
	RW_ASSERT(!rc);

	/* sizes must match */
	if (info_a->count != info_b->count) {
		rc = info_a->count < info_b->count ? -1 : 1;
		goto out;
	}
	count = info_a->count;

	memset(&rw_item_a, 0, sizeof(rw_item_a));
	memset(&rw_item_b, 0, sizeof(rw_item_b));

	rw_item_a.rtd = rw_item_b.rtd = info_a->rtd;
	rw_item_a.data_size.bytes = rw_item_b.data_size.bytes = item_size;
	rw_item_a.flags = rw_item_b.flags = RWF_SEQ_IDX;
	if (!array_type_is_mapping(info_a->type)) {	/* for regular arrays, the idx is the key */
		rw_item_a.flags |= RWF_SEQ_KEY;
		rw_item_b.flags |= RWF_SEQ_KEY;
	}

	rw_item_a.parent = info_a->rw;
	rw_item_b.parent = info_b->rw;

	rc = 0;
	for (idx = 0; idx < count; idx++) {

		rw_item_a.idx = rw_item_b.idx = idx;

		rw_item_a.data = ptr_a + idx * item_size;
		rw_item_b.data = ptr_b + idx * item_size;

		rc = reflection_cmp_rw(&rw_item_a, &rw_item_b);
		if (rc)
			break;
	}
out:
	return rc;

err_out:
	rc = -2;
	goto out;
}

static int
array_eq(struct array_info *info_a, struct array_info *info_b)
{
	struct reflection_walker *rw;
	struct reflection_walker rw_item_a, rw_item_b;
	void *ptr_a, *ptr_b;
	size_t item_size;
	uintmax_t idx, count;
	int rc;

	assert(info_a);
	assert(info_b);

	rw = info_a->rw;
	assert(rw);

	RW_ASSERT(info_a->rtd == info_b->rtd);

	if (array_type_is_dynamic(info_a->type))
		ptr_a = *(void **)info_a->rw->data;
	else if (array_type_is_const(info_a->type))
		ptr_a = info_a->rw->data;
	else
		ptr_a = NULL;
	RW_ASSERT(ptr_a);

	if (array_type_is_dynamic(info_b->type))
		ptr_b = *(void **)info_b->rw->data;
	else if (array_type_is_const(info_b->type))
		ptr_b = info_b->rw->data;
	else
		ptr_b = NULL;
	RW_ASSERT(ptr_b);

	if (ptr_a == ptr_b)
		return 0;

	if (!ptr_a || !ptr_b)
		return -1;

	item_size = info_a->rtd_dep->ti->size;

	rc = array_info_load_item_count(info_a, ptr_a);
	RW_ASSERT(!rc);

	rc = array_info_load_item_count(info_b, ptr_b);
	RW_ASSERT(!rc);

	/* sizes must match */
	if (info_a->count != info_b->count) {
		rc = -1;
		goto out;
	}
	count = info_a->count;

	/* pure array items can just be memcmp'ed */
	if ((info_a->rtd_dep->flags & RTDF_PURITY_MASK) == 0) {
		rc = memcmp(ptr_a, ptr_b, count * item_size);
		return !rc ? 0 : -1;
	}

	memset(&rw_item_a, 0, sizeof(rw_item_a));
	memset(&rw_item_b, 0, sizeof(rw_item_b));

	rw_item_a.rtd = rw_item_b.rtd = info_a->rtd;
	rw_item_a.data_size.bytes = rw_item_b.data_size.bytes = item_size;
	rw_item_a.flags = rw_item_b.flags = RWF_SEQ_IDX;
	if (!array_type_is_mapping(info_a->type)) {	/* for regular arrays, the idx is the key */
		rw_item_a.flags |= RWF_SEQ_KEY;
		rw_item_b.flags |= RWF_SEQ_KEY;
	}

	rw_item_a.parent = info_a->rw;
	rw_item_b.parent = info_b->rw;

	rc = 1;
	for (idx = 0; idx < count; idx++) {

		rw_item_a.idx = rw_item_b.idx = idx;

		rw_item_a.data = ptr_a + idx * item_size;
		rw_item_b.data = ptr_b + idx * item_size;

		rc = reflection_eq_rw(&rw_item_a, &rw_item_b);
		if (rc <= 0)
			break;
	}
out:
	return rc;

err_out:
	rc = -2;
	goto out;
}

static int
common_array_parse(struct fy_parser *fyp, struct reflection_walker *rw,
		  enum reflection_parse_flags flags)
{
	struct array_info info;
	int rc;

	rc = array_info_setup(&info, rw);
	if (!rc)
		rc = array_parse(&info, fyp, rw, flags);
	array_info_cleanup(&info);
	return rc;
}

static int
common_array_emit(struct fy_emitter *emit, struct reflection_walker *rw,
		 enum reflection_emit_flags flags)
{
	struct array_info info;
	int rc;

	rc = array_info_setup(&info, rw);
	if (!rc)
		rc = array_emit(&info, emit, rw, flags);
	array_info_cleanup(&info);
	return rc;
}

static void
common_array_dtor(struct reflection_walker *rw)
{
	struct array_info info;
	int rc;

	rc = array_info_setup(&info, rw);
	if (!rc)
		array_dtor(&info);
	array_info_cleanup(&info);
}

enum common_array_call2_type {
	CAC2T_COPY,
	CAC2T_CMP,
	CAC2T_EQ,
};

static int
common_array_call2(struct reflection_walker *rw_a, struct reflection_walker *rw_b,
		   enum common_array_call2_type call_type)
{
	struct reflection_walker *rw;
	struct array_info info_local_a, *info_a = &info_local_a;
	struct array_info info_local_b, *info_b = &info_local_b;
	bool a_ready = false, b_ready = false;
	int rc;

	assert(rw_a);
	assert(rw_b);

	rw = rw_b;

	RW_ASSERT(rw_b->rtd == rw_a->rtd);

	rc = array_info_setup(info_a, rw_a);
	RW_ASSERT(!rc);
	a_ready = true;

	rc = array_info_setup(info_b, rw_b);
	RW_ASSERT(!rc);
	b_ready = true;

	switch (call_type) {
	case CAC2T_COPY:
		rc = array_copy(info_a, info_b);
		break;
	case CAC2T_CMP:
		rc = array_cmp(info_a, info_b);
		break;
	case CAC2T_EQ:
		rc = array_eq(info_a, info_b);
		break;
	default:
		rc = -2;
		break;
	}

out:
	if (b_ready)
		array_info_cleanup(info_b);
	if (a_ready)
		array_info_cleanup(info_a);

	return rc;

err_out:
	rc = -2;
	goto out;
}

static int
common_array_copy(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	return common_array_call2(rw_dst, rw_src, CAC2T_COPY);
}

static int
common_array_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return common_array_call2(rw_a, rw_b, CAC2T_CMP);
}

static int
common_array_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	int rc;
	rc = common_array_call2(rw_a, rw_b, CAC2T_EQ);
	return rc;
}

struct str_instance_data {
	struct reflection_field_data *rfd;
	struct reflection_field_data *rfd_counter;
	struct reflection_walker *rw;
	struct reflection_walker *rw_field;
	struct reflection_walker *rw_struct;
	bool not_null_terminated;
};

static void
str_instance_data_setup(struct str_instance_data *id, struct reflection_walker *rw)
{
	struct reflection_field_data *rfd;
	const char *counter;

	assert(id);
	assert(rw);

	memset(id, 0, sizeof(*id));

	id->rw = rw;
	rfd = reflection_get_field(rw, &id->rw_field, NULL);
	id->rfd = rfd;
	if (rfd && id->rw_field)
		id->rw_struct = id->rw_field->parent;
	if (rfd && id->rw_struct) {
		counter = reflection_meta_get_str(rw->rtd->meta, rmvid_counter);
		if (counter)
			id->rfd_counter = reflection_type_data_lookup_field(rfd->rtd_parent, counter);
	}

	/* it's not null terminated if there's a counter or the meta value is set */
	id->not_null_terminated = id->rfd_counter || reflection_meta_get_bool(rw->rtd->meta, rmvid_not_null_terminated);
}

static int
str_load_length(struct str_instance_data *id, size_t *lenp)
{
	assert(id);
	assert(lenp);

	return 0;
}

static bool
str_store_length_check(struct str_instance_data *id, size_t len)
{
	assert(id);

	if (!id->rfd_counter)
		return 0;

	return integer_field_store_check(id->rfd_counter, len + (id->not_null_terminated ? 0 : 1));
}

static void
str_store_length(struct str_instance_data *id, const char *str, size_t len)
{
	assert(id);

	if (!id->rfd_counter)
		return;

	struct_integer_field_store(id->rw_struct, id->rfd_counter, len);
}

static int
str_instance_len(struct str_instance_data *id, const char *text, size_t *lenp)
{
	assert(id);
	assert(lenp);

	if (!text)
		return -EINVAL;

	*lenp = !id->rfd_counter ? (uintmax_t)strlen(text) :
				 struct_integer_field_load(id->rw_struct, id->rfd_counter);
	if (*lenp > SIZE_MAX)
		return -ERANGE;

	return 0;
}

static int
const_array_char_parse(struct fy_parser *fyp, struct reflection_walker *rw,
		       enum reflection_parse_flags flags)
{
	struct fy_event *fye = NULL;
	struct reflection_type_data *rtd, *rtd_dep;
	struct fy_token *fyt;
	enum fy_type_kind type_kind __attribute__((unused));
	struct str_instance_data id_local, *id = &id_local;
	const char *text;
	size_t len, lenz;
	void *data;
	int rc;

	rtd = rw->rtd;
	assert(rtd);
	assert(rtd->ti->kind == FYTK_CONSTARRAY);

	rtd_dep = rtd->rtd_dep;
	assert(rtd_dep);

	data = rw->data;
	assert(data);

	type_kind = rtd->ti->kind;
	assert(fy_type_kind_is_valid(type_kind));

	type_kind = rtd_dep->ti->kind;
	assert(type_kind == FYTK_CHAR);

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

	RP_INPUT_CHECK(fye->type == FYET_SCALAR,
		       "expecting FYET_SCALAR, got %s\n", fy_event_type_get_text(fye->type));

	fyt = fy_event_get_token(fye);
	text = fy_token_get_text(fyt, &len);
	RP_ASSERT(text != NULL);

	str_instance_data_setup(id, rw);

	lenz = len + !id->not_null_terminated;

	RP_INPUT_CHECK(lenz <= rtd->ti->count,
		       "string size too large to fit char[%zu] (was %zu)",
		       rtd->ti->count, len);

	if (id->rfd_counter) {
		rc = integer_field_store_check(id->rfd_counter, lenz);
		RP_INPUT_CHECK(!rc, "cannot store length %zu to counter", lenz);
	}

	if (!(flags & RPF_NO_STORE)) {
		memcpy(data, text, len);
		if (len < rtd->ti->count)
			memset(data + len, 0, rtd->ti->count - len);

		if (id->rfd_counter)
			struct_integer_field_store(id->rw_struct, id->rfd_counter, len);
	}

	rc = 0;

out:
	fy_parser_event_free(fyp, fye);
	return rc;

err_out:
	rc = -1;
	goto out;

err_input:
	rc = -EINVAL;
	goto out;
}

static int
const_array_char_emit(struct fy_emitter *emit, struct reflection_walker *rw,
		      enum reflection_emit_flags flags)
{
	struct reflection_type_data *rtd, *rtd_dep;
	enum fy_type_kind type_kind __attribute__((unused));
	struct str_instance_data id_local, *id = &id_local;
	const char *text;
	size_t len;
	int rc;

	rtd = rw->rtd;

	/* verify alignment */
	type_kind = rtd->ti->kind;
	assert(type_kind == FYTK_CONSTARRAY);

	rtd_dep = rtd->rtd_dep;
	assert(rtd_dep);

	type_kind = rtd_dep->ti->kind;
	assert(type_kind == FYTK_CHAR);

	text = rw->data;

	str_instance_data_setup(id, rw);

	len = id->rfd_counter ? struct_integer_field_load(id->rw_struct, id->rfd_counter) :
	      !id->not_null_terminated ? strnlen(text, rtd->ti->count) : rtd->ti->count;
	RE_ASSERT(len <= rtd->ti->count);

	rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_ANY, text, len, NULL, NULL);
	RE_ASSERT(!rc);

	return 0;
err_out:
	return -1;
}

static const struct reflection_type_ops const_array_char_ops = {
	.name = "const_array_char",
	.parse = const_array_char_parse,
	.emit = const_array_char_emit,
};

static int
struct_parse(struct fy_parser *fyp, struct reflection_walker *rw,
	     enum reflection_parse_flags flags)
{
	struct struct_instance_data id_local, *id = &id_local, *id_parent;
	struct fy_parser_checkpoint *fypchk = NULL;
	struct fy_event *fye = NULL;
	const struct fy_event *fye_peek;
	struct reflection_type_data *rtd, *rtdf;
	struct reflection_field_data *rfd, *rfd_flatten;
	struct reflection_field_data *rfd_selector;
	struct reflection_walker rw_field, rw_selector, rw_tmp;
	struct fy_token *fyt_key;
	const char *field;
	int i, rc, field_start_idx, field_end_idx, union_field_idx = -1;
	enum fy_event_type match_type;
	bool input_error, is_union, required, skip_unknown, field_auto_select;
	void *value;

	rtd = rw->rtd;
	assert(rtd);

	assert(rtd->ti->kind == FYTK_STRUCT || rtd->ti->kind == FYTK_UNION);

	rc = struct_instance_data_setup(id, rw);
	RP_ASSERT(!rc);

	is_union = rtd->ti->kind == FYTK_UNION;

	memset(&rw_field, 0, sizeof(rw_field));
	rw_field.parent = rw;

	id_parent = NULL;
	rfd = NULL;

	field = NULL;

	rfd_selector = NULL;

	if (rtd->flat_field_idx >= 0) {

		RP_ASSERT(rtd->flat_field_idx < rtd->fields_count);
		rfd = rtd->fields[rtd->flat_field_idx];

	} else if (rtd->ti->flags & FYTIF_ANONYMOUS_RECORD_DECL) {

		/* get parent struct's stashed anonymous_field_name */
		if (!id_parent)
			id_parent = struct_get_parent_struct_id(rw);
		RP_ASSERT(id_parent);

		field = id_parent->anonymous_field_name;
		RP_ASSERT(field);

		rfd = reflection_type_data_lookup_next_anonymous_field(rtd, field);
		RP_INPUT_CHECK(rfd != NULL,
			       "anonymous struct T%d/'%s' has no field '%s'",
			       rtd->idx, rtd->ti->name, field);
	}

	/* no direct field, but there's an auto-select */
	field_auto_select = reflection_meta_get_bool(rtd->meta, rmvid_field_auto_select);
	if (field_auto_select) {

		RP_ASSERT(!rfd);

		fypchk = fy_parser_checkpoint_create(fyp);
		RP_ASSERT(fypchk);

		memset(rw->data, 0, rw->data_size.bytes);

		rfd = NULL;
		for (i = 0; i < rtd->fields_count; i++) {
			rfd = rtd->fields[i];

			rtdf = rfd_rtd(rfd);
			assert(rtdf);

			if (reflection_meta_get_bool(rtdf->meta, rmvid_match_always))
				break;

			/* short circuit */
			if (reflection_meta_get_bool(rtdf->meta, rmvid_match_scalar))
				match_type = FYET_SCALAR;
			else if (reflection_meta_get_bool(rtdf->meta, rmvid_match_seq))
				match_type = FYET_SEQUENCE_START;
			else if (reflection_meta_get_bool(rtdf->meta, rmvid_match_map))
				match_type = FYET_MAPPING_START;
			else
				match_type = FYET_NONE;

			if (match_type != FYET_NONE) {
				fye_peek = fy_parser_parse_peek(fyp);
				if (fye_peek && fye_peek->type == match_type)
					break;
			}

			/* save the anonymous field name */
			if (rfd->fi->type_info->flags & FYTIF_ANONYMOUS_RECORD_DECL) {

				if (!id_parent)
					id_parent = struct_get_parent_struct_id(rw);
				RP_ASSERT(id_parent);

				field = id_parent->anonymous_field_name;
				id->anonymous_field_name = field;
			}

			struct_setup_field_rw(&rw_field, rfd);

			rc = reflection_parse_rw(fyp, &rw_field,
						 flags | RPF_NO_STORE | RPF_SILENT_INVALID_INPUT);
			input_error = rc == -EINVAL;

			RP_ASSERT(!rc || input_error);

			/* always rollback */
			rc = fy_parser_rollback(fyp, fypchk);
			RP_ASSERT(!rc);

			if (!input_error)
				break;

			field = NULL;
		}

		fy_parser_checkpoint_destroy(fypchk);
		fypchk = NULL;

		if (rfd == NULL)
			fye = fy_parser_parse(fyp);
		RP_INPUT_CHECK(rfd != NULL,
			       "Unable to find field to auto-match for T#%d/%s\n",
			       rtd->idx, rtd->ti->name);
	}

	/* flatten, directly calling parse */
	if (rfd) {
		rfd_flatten = rfd;

		/* union, must select the field */
		if (is_union) {
			if (!id_parent)
				id_parent = struct_get_parent_struct_id(rw);
			RP_ASSERT(id_parent);

			RP_ASSERT(!struct_is_field_present(id_parent, rtd->selector_field_idx));

			RP_ASSERT(rtd->selector_field_idx >= 0);
			RP_ASSERT(rtd->selector_field_idx < rw->parent->rtd->fields_count);

			rfd_selector = rw->parent->rtd->fields[rtd->selector_field_idx];

			memset(&rw_selector, 0, sizeof(rw_selector));
			rw_selector.parent = rw->parent;
			struct_setup_field_rw(&rw_selector, rfd_selector);

			value = reflection_meta_generate_any_value(rfd_rtd(rfd)->meta, rmvid_select, rfd_rtd(rfd_selector));
			RP_ASSERT(value);

			if (!(flags & RPF_NO_STORE)) {
				rc = reflection_copy_rw(&rw_selector, reflection_rw_value(&rw_tmp, rfd_selector->rtd, value));
				RP_ASSERT(!rc);
			}

			struct_set_field_present(id_parent, rtd->selector_field_idx);

			rfd_selector = NULL;
		}

	} else {
		rfd_flatten = NULL;

		/* if it's a union, there must be a union selector */
		if (is_union) {
			if (!id_parent)
				id_parent = struct_get_parent_struct_id(rw);
			RP_ASSERT(id_parent);

			RP_ASSERT(!struct_is_field_present(id_parent, rtd->selector_field_idx));

			RP_ASSERT(rtd->selector_field_idx >= 0);
			RP_ASSERT(rtd->selector_field_idx < rw->parent->rtd->fields_count);

			rfd_selector = rw->parent->rtd->fields[rtd->selector_field_idx];
		}

		fye = fy_parser_parse(fyp);
		RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

		RP_INPUT_CHECK(fye->type == FYET_MAPPING_START,
			       "illegal event type (expecting mapping start)");

		rw->flags |= RWF_MAP;

		field = NULL;
	}

	for (;;) {
		if (!rfd_flatten) {
			fy_parser_event_free(fyp, fye);
			fye = fy_parser_parse(fyp);
			RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

			/* finish? */
			if (fye->type == FYET_MAPPING_END)
				break;

			/* key must be scalar */
			RP_INPUT_CHECK(fye->type == FYET_SCALAR,
				       "struct '%s'/#%d expects scalar key",
				       fy_type_info_prefixless_name(rtd->ti), rtd->idx);

			fyt_key = fy_event_get_token(fye);

			/* get the field */
			field = fy_token_get_text0(fyt_key);
			RP_ASSERT(field != NULL);

			rfd = reflection_type_data_lookup_field(rtd, field);
			if (!rfd && rtd->has_anonymous_field)
				rfd = reflection_type_data_lookup_next_anonymous_field(rtd, field);

			if (!rfd) {
				/* if we're allowing skipping... */
				skip_unknown = reflection_meta_get_bool(rtd->meta, rmvid_skip_unknown);
				RP_INPUT_CHECK(skip_unknown,
					       "no field '%s' found in '%s'",
					       field, rtd->ti->name);

				rc = fy_parser_skip(fyp);
				RP_ASSERT(!rc);

				continue;
			}
		} else {
			rfd = rfd_flatten;
			fye = NULL;
		}

		RP_INPUT_CHECK(!struct_is_field_present(id, rfd->idx) ||
			       (rfd->fi->type_info->flags & FYTIF_ANONYMOUS_RECORD_DECL),
			       "duplicate field '%s' found in '%s'",
			       field ? field : rfd->field_name, rtd->ti->name);

		RP_INPUT_CHECK(!rfd->is_selector,
			       "selector field '%s' in '%s' is not directly settable",
				field ? field : rfd->field_name, rtd->ti->name);

		RP_INPUT_CHECK(!(is_union && union_field_idx >= 0),
			       "multiple union field cannot be set '%s' found in '%s'",
			       field ? field : rfd->field_name, rtd->ti->name);

		/* save the anonymous field name */
		if (rfd->fi->type_info->flags & FYTIF_ANONYMOUS_RECORD_DECL) {
			assert(field);
			id->anonymous_field_name = field;
		}

		rw_field.flags = RWF_VALUE;
		struct_setup_field_rw(&rw_field, rfd);

		if (field && !(rfd->fi->type_info->flags & FYTIF_ANONYMOUS_RECORD_DECL)) {
			rw_field.flags |= RWF_TEXT_KEY;
			rw_field.text_key = field;
		}

		rc = reflection_parse_rw(fyp, &rw_field, flags);
		if (rc)
			goto out;

		struct_set_field_present(id, rfd->idx);

		if (is_union)
			union_field_idx = rfd->idx;

		if (rfd_flatten)
			break;
	}

	RP_INPUT_CHECK(!(is_union && union_field_idx < 0),
		       "no union data was found");

	if (!is_union) {
		field_start_idx = 0;
		field_end_idx = rtd->fields_count;
	} else {
		field_start_idx = union_field_idx;
		RP_INPUT_CHECK(field_start_idx >= 0,
			       "no selected union field was found");

		field_end_idx = field_start_idx + 1;

		/* union and with a field selector, apply it */
		if (rfd_selector) {
			memset(&rw_selector, 0, sizeof(rw_selector));
			rw_selector.parent = rw->parent;
			struct_setup_field_rw(&rw_selector, rfd_selector);

			value = reflection_meta_generate_any_value(rfd_rtd(rfd)->meta, rmvid_select, rfd_rtd(rfd_selector));
			RP_ASSERT(value);

			if (!(flags & RPF_NO_STORE)) {
				rc = reflection_copy_rw(&rw_selector, reflection_rw_value(&rw_tmp, rfd_selector->rtd, value));
				RP_ASSERT(!rc);
			}

			struct_set_field_present(id_parent, rtd->selector_field_idx);
		}
	}

	for (i = field_start_idx; i < field_end_idx; i++) {

		rfd = rtd->fields[i];
		rtdf = rfd_rtd(rfd);

		/* fill-in-default */
		if (!struct_is_field_present(id, i) &&
		    (value = reflection_meta_generate_any_value(rtdf->meta,
								rmvid_default, rtdf)) != NULL) {

			if (!(flags & RPF_NO_STORE)) {
				rw_field.flags = RWF_VALUE;
				struct_setup_field_rw(&rw_field, rfd);

				rc = reflection_copy_rw(&rw_field,
						reflection_rw_value(&rw_tmp, rtdf, value));
				RP_ASSERT(!rc);
			}

			struct_set_field_present(id, i);
		}

		required = !rfd_is_anonymous_bitfield(rfd) &&
			   !rfd->is_counter &&
			   reflection_meta_get_bool(rfd_rtd(rfd)->meta, rmvid_required);

		RP_INPUT_CHECK(!required || struct_is_field_present(id, i),
				"missing required field '%s' of '%s'",
				rfd->field_name, rtd->ti->name);
	}
	rc = 0;

out:
	fy_parser_event_free(fyp, fye);
	struct_instance_data_cleanup(id);
	return rc;

err_out:
	rc = -1;
	goto out;

err_input:
	rc = -EINVAL;
	goto out;
}

static int
struct_get_union_selection_index(struct reflection_walker *rw)
{
	struct reflection_type_data *rtd, *rtd_parent;
	struct reflection_field_data *rfd, *rfd_selector;
	struct reflection_type_data *rtdf;
	struct reflection_walker rw_field, rw_tmp;
	void *value;
	int i;

	assert(rw);
	assert(rw->data);

	rtd = rw->rtd;
	if (rtd->ti->kind != FYTK_UNION)
		return -1;

	rtd_parent = rw->parent ? rw->parent->rtd : NULL;

	if (!rtd_parent)
		return -1;

	assert(rtd_parent);
	assert(rtd->rtd_selector);

	assert(rtd->selector_field_idx < rtd_parent->fields_count);
	rfd_selector = rtd_parent->fields[rtd->selector_field_idx];
	assert(rfd_selector);

	assert(rtd->union_field_idx < rtd_parent->fields_count);

	/* point to the selector */
	memset(&rw_field, 0, sizeof(rw_field));
	rw_field.parent = rw->parent;
	struct_setup_field_rw(&rw_field, rfd_selector);

	/* iterate until there's a match */
	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];
		rtdf = rfd_rtd(rfd);
		value = reflection_meta_generate_any_value(rtdf->meta, rmvid_select, rfd_rtd(rfd_selector));
		if (reflection_eq_rw(&rw_field, reflection_rw_value(&rw_tmp, rfd_selector->rtd, value)) > 0)
			return i;
	}
	return -1;
}

static int
struct_emit(struct fy_emitter *emit, struct reflection_walker *rw,
	    enum reflection_emit_flags flags)
{
	struct reflection_walker rw_field;
	struct reflection_meta *rmf;
	struct reflection_type_data *rtd, *rtdf;
	struct reflection_field_data *rfd, *rfd_flatten;
	const struct fy_field_info *fi;
	int i, field_start_idx, field_end_idx, union_field_idx;
	int rc;
	bool is_union, emit_start_end, emit_key;
	struct reflection_any_value *rav_default;

	rtd = rw->rtd;

	is_union = rtd->ti->kind == FYTK_UNION;
	if (is_union) {
		union_field_idx = struct_get_union_selection_index(rw);
		RE_ASSERT(union_field_idx >= 0);
	} else
		union_field_idx = -1;

	if (rtd->flat_field_idx >= 0) {
		RE_ASSERT(rtd->flat_field_idx >= 0 && rtd->flat_field_idx < rtd->fields_count);
		rfd_flatten = rtd->fields[rtd->flat_field_idx];
	} else if (is_union && reflection_meta_get_bool(rtd->meta, rmvid_field_auto_select)) {
		RE_ASSERT(union_field_idx >= 0 && union_field_idx < rtd->fields_count);
		rfd_flatten = rtd->fields[union_field_idx];
	} else
		rfd_flatten = NULL;

	emit_start_end = !rfd_flatten && !(rtd->ti->flags & FYTIF_ANONYMOUS_RECORD_DECL);

	if (rfd_flatten) {
		field_start_idx = rfd_flatten->idx;
		field_end_idx = field_start_idx + 1;
	} else if (!is_union) {
		field_start_idx = 0;
		field_end_idx = rtd->fields_count;
	} else {
		field_start_idx = union_field_idx;
		RE_ASSERT(field_start_idx >= 0);
		field_end_idx = field_start_idx + 1;
	}

	if (emit_start_end) {
		rc = fy_emit_eventf(emit, FYET_MAPPING_START, FYNS_ANY, NULL, NULL);
		RE_ASSERT(!rc);
	}

	memset(&rw_field, 0, sizeof(rw_field));
	rw_field.parent = rw;

	for (i = field_start_idx; i < field_end_idx; i++) {

		rfd = rtd->fields[i];

		fi = rfd->fi;

		/* unnamed field (bitfield) */
		if (fi->name[0] == '\0')
			continue;

		rtdf = rfd_rtd(rfd);
		rmf = rtdf ? rtdf->meta : NULL;

		/* field that should not appear */
		if (reflection_meta_get_bool(rmf, rmvid_omit_on_emit))
			continue;

		if (rfd->is_counter || rfd->is_selector)
			continue;

		/* omit if pointer and NULL (can't omit if no key is output) */
		if (reflection_meta_get_bool(rmf, rmvid_omit_if_null) &&
		    struct_field_is_ptr_null(rw, rfd))
			continue;

		rw_field.flags = RWF_VALUE | RWF_TEXT_KEY;
		struct_setup_field_rw(&rw_field, rfd);

		/* omit if default value */
		if (reflection_meta_get_bool(rmf, rmvid_omit_if_default) &&
		   (rav_default = reflection_meta_get_any_value(rmf, rmvid_default)) != NULL) {

			rc = reflection_any_value_equal_rw(rav_default, &rw_field);
			RE_ASSERT(rc >= 0);

			if (rc > 0)	/* equal */
				continue;
		}

		emit_key = !rfd_flatten && !(rfd->fi->type_info->flags & FYTIF_ANONYMOUS_RECORD_DECL);
		if (emit_key) {
			rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_ANY, rfd->field_name, FY_NT, NULL, NULL);
			RE_ASSERT(!rc);
		}

		rc = reflection_emit_rw(emit, &rw_field, flags);
		RE_ASSERT(!rc);
	}

	if (emit_start_end) {
		rc = fy_emit_eventf(emit, FYET_MAPPING_END);
		RE_ASSERT(!rc);
	}

	return 0;

err_out:
	return -1;
}

static void
struct_dtor(struct reflection_walker *rw)
{
	struct reflection_type_data *rtd;
	struct reflection_field_data *rfd;
	struct reflection_walker rw_field;
	int i, field_start_idx, field_end_idx;
	bool is_union;

	rtd = rw->rtd;

	is_union = rtd->ti->kind == FYTK_UNION;

	if (!is_union) {
		field_start_idx = 0;
		field_end_idx = rtd->fields_count;
	} else {
		field_start_idx = struct_get_union_selection_index(rw);
		assert(field_start_idx >= 0);

		field_end_idx = field_start_idx + 1;
	}

	memset(&rw_field, 0, sizeof(rw_field));
	rw_field.parent = rw;

	for (i = field_start_idx; i < field_end_idx; i++) {

		rfd = rtd->fields[i];

		struct_setup_field_rw(&rw_field, rfd);
		reflection_dtor_rw(&rw_field);
	}
}

enum struct_call2_type {
	SC2T_COPY,
	SC2T_CMP,
	SC2T_EQ,
};

static int
struct_call2(struct reflection_walker *rw_a, struct reflection_walker *rw_b,
	     enum struct_call2_type call_type)
{
	struct reflection_type_data *rtd;
	struct reflection_field_data *rfd;
	struct reflection_walker rw_field_a, rw_field_b;
	int rc, i, field_start_idx, field_end_idx, union_select_a, union_select_b;
	bool is_union;

	rtd = rw_b->rtd;
	assert(rtd);
	assert(rtd == rw_a->rtd);

	/* pure? can just be mem'ed for copy and eq */
	if ((rtd->flags & RTDF_PURITY_MASK) == 0) {
		switch (call_type) {
		case SC2T_COPY:
			memcpy(rw_a->data, rw_b->data, rtd->ti->size);
			return 0;
		case SC2T_EQ:
			return !memcmp(rw_a->data, rw_b->data, rtd->ti->size);
		default:
			break;
		}
	}

	is_union = rtd->ti->kind == FYTK_UNION;

	if (!is_union) {
		field_start_idx = 0;
		field_end_idx = rtd->fields_count;
	} else {
		union_select_a = struct_get_union_selection_index(rw_a);
		assert(union_select_a >= 0);

		union_select_b = struct_get_union_selection_index(rw_b);
		assert(union_select_b >= 0);

		/* unions, but with different fields selected */
		if (union_select_a != union_select_b)
			return -1;

		field_start_idx = union_select_a;
		field_end_idx = field_start_idx + 1;
	}

	memset(&rw_field_a, 0, sizeof(rw_field_a));
	rw_field_a.parent = rw_a;

	memset(&rw_field_b, 0, sizeof(rw_field_b));
	rw_field_b.parent = rw_b;

	rc = 0;
	for (i = field_start_idx; i < field_end_idx; i++) {

		rfd = rtd->fields[i];

		struct_setup_field_rw(&rw_field_a, rfd);
		struct_setup_field_rw(&rw_field_b, rfd);

		switch (call_type) {
		case SC2T_COPY:
			rc = reflection_copy_rw(&rw_field_a, &rw_field_b);
			break;
		case SC2T_CMP:
			rc = reflection_cmp_rw(&rw_field_a, &rw_field_b);
			break;
		case SC2T_EQ:
			rc = reflection_eq_rw(&rw_field_a, &rw_field_b);
			break;
		default:
			rc = -1;
			break;
		}
		if (rc)
			break;
	}

	return rc;
}

static int
struct_copy(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	return struct_call2(rw_dst, rw_src, SC2T_COPY);
}

static int
struct_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return struct_call2(rw_a, rw_b, SC2T_CMP);
}

static int
struct_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return struct_call2(rw_a, rw_b, SC2T_EQ);
}

static int
enum_parse_single_scalar(struct fy_parser *fyp, struct reflection_walker *rw,
			 enum reflection_parse_flags flags, union integer_scalar *valp,
			 struct reflection_type_data *rtd_enum)
{
	struct fy_event *fye = NULL;
	struct reflection_field_data *rfd;
	const char *text0;
	union integer_scalar mini, maxi;
	enum fy_type_kind type_kind;
	int rc;

	type_kind = rw->rtd->rtd_dep->ti->kind;

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

	RP_INPUT_CHECK(fye->type == FYET_SCALAR,
		       "expecting FYET_SCALAR, got %s\n", fy_event_type_get_text(fye->type));

	text0 = fy_token_get_text0(fy_event_get_token(fye));
	RP_ASSERT(text0 != NULL);

	rfd = reflection_type_data_lookup_field(rtd_enum, text0);
	RP_INPUT_CHECK(rfd != NULL,
			"No enumeration value named %s in '%s'",
			text0, rw->rtd->ti->name);

	if (fy_type_kind_is_signed(type_kind))
		valp->sval = rfd->fi->sval;
	else
		valp->uval = rfd->fi->uval;

	rc = store_integer_scalar_check_rw(rw, *valp, &mini, &maxi);
	RP_INPUT_CHECK(!(rc == -ERANGE && fy_type_kind_is_signed(type_kind)),
			"%s value %s (%jd) cannot fit (min=%jd, max=%jd)",
			rtd_enum->ti->name, rfd->field_name, valp->sval, mini.sval, maxi.sval);
	RP_INPUT_CHECK(!(rc == -ERANGE && fy_type_kind_is_unsigned(type_kind)),
			"%s value %s (%ju) cannot fit (max=%ju)",
			rtd_enum->ti->name, rfd->field_name, valp->uval, maxi.uval);

	rc = 0;
out:
	fy_parser_event_free(fyp, fye);
	return rc;

err_out:
	rc = -1;
	goto out;

err_input:
	rc = -EINVAL;
	goto out;
}

static int
enum_parse(struct fy_parser *fyp, struct reflection_walker *rw,
	   enum reflection_parse_flags flags)
{
	const struct fy_event *fye_peek = NULL;
	struct fy_event *fye = NULL;
	struct reflection_walker rw_num;
	union integer_scalar val, tval, mini, maxi;
	enum fy_type_kind type_kind;
	int rc;

	assert(rw->rtd);
	assert(rw->rtd->rtd_dep);

	type_kind = rw->rtd->rtd_dep->ti->kind;

	fye_peek = fy_parser_parse_peek(fyp);
	RP_INPUT_CHECK(fye_peek != NULL, "premature end of input\n");
	RP_INPUT_CHECK(fye_peek->type == FYET_SCALAR || fye_peek->type == FYET_SEQUENCE_START,
			"enum illegal event\n");

	if (fye_peek->type == FYET_SCALAR) {
		rc = enum_parse_single_scalar(fyp, rw, flags, &val, rw->rtd);
		if (rc)
			goto out;
	} else {
		RP_ASSERT(fye_peek->type == FYET_SEQUENCE_START);
		RP_INPUT_CHECK(reflection_meta_get_bool(rw->rtd->meta, rmvid_enum_or_seq),
				"enum-or-seq but not marked as such");

		fye = fy_parser_parse(fyp);
		RP_ASSERT(fye && fye->type == FYET_SEQUENCE_START);
		fy_parser_event_free(fyp, fye);
		fye = NULL;

		val.uval = 0;
		while ((fye_peek = fy_parser_parse_peek(fyp)) != NULL && fye_peek->type == FYET_SCALAR) {
			rc = enum_parse_single_scalar(fyp, rw, flags, &tval, rw->rtd);
			if (rc)
				goto out;

			if (fy_type_kind_is_signed(type_kind))
				val.sval |= tval.sval;
			else
				val.uval |= tval.uval;
		}

		RP_INPUT_CHECK(fye_peek && fye_peek->type == FYET_SEQUENCE_END,
				"enum sequence not ended correctly");

		fye = fy_parser_parse(fyp);
		RP_ASSERT(fye && fye->type == FYET_SEQUENCE_END);
		fy_parser_event_free(fyp, fye);
		fye = NULL;
	}

	rc = store_integer_scalar_check_rw(rw, val, &mini, &maxi);
	RP_INPUT_CHECK(!(rc == -ERANGE && fy_type_kind_is_signed(type_kind)),
			"%s (%jd) cannot fit (min=%jd, max=%jd)",
			rw->rtd->ti->name, val.sval, mini.sval, maxi.sval);
	RP_INPUT_CHECK(!(rc == -ERANGE && fy_type_kind_is_unsigned(type_kind)),
			"%s (%ju) cannot fit (max=%ju)",
			rw->rtd->ti->name, val.uval, maxi.uval);

	if (!(flags & RPF_NO_STORE))
		store_integer_scalar_no_check_rw(reflection_rw_dep(&rw_num, rw), val);

out:
	fy_parser_event_free(fyp, fye);
	return rc;

err_input:
	rc = -EINVAL;
	goto out;

err_out:
	rc = -1;
	goto out;
}

static int
enum_emit_single_scalar(struct fy_emitter *emit, struct reflection_walker *rw,
			enum reflection_emit_flags flags, union integer_scalar val,
			struct reflection_type_data *rtd_enum)
{
	struct reflection_field_data *rfd;
	int rc;

	rfd = reflection_type_data_lookup_field_by_scalar_enum_value(rtd_enum, val,
			fy_type_kind_is_signed(rw->rtd->rtd_dep->ti->kind));
	if (!rfd)
		return -1;

	rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_ANY, rfd->field_name, FY_NT, NULL, NULL);
	RE_ASSERT(!rc);

	return 0;
err_out:
	return -1;
}

static int
enum_emit(struct fy_emitter *emit, struct reflection_walker *rw,
	  enum reflection_emit_flags flags)
{
	struct reflection_walker rw_num;
	struct reflection_field_data *rfd;
	union integer_scalar val, tval;
	bool is_signed;
	int i, rc;

	assert(rw->rtd);
	assert(rw->rtd->rtd_dep);

	is_signed = fy_type_kind_is_signed(rw->rtd->rtd_dep->ti->kind);

	val = load_integer_scalar_rw(reflection_rw_dep(&rw_num, rw));

	/* can do it directly? do it */
	rfd = reflection_type_data_lookup_field_by_scalar_enum_value(rw->rtd, val, is_signed);
	if (rfd) {
		rc = enum_emit_single_scalar(emit, rw, flags, val, rw->rtd);
		RE_ASSERT(!rc);
		goto out;
	}

	RE_OUTPUT_CHECK(reflection_meta_get_bool(rw->rtd->meta, rmvid_enum_or_seq),
			"enum-or-seq but not marked as such");

	rc = fy_emit_eventf(emit, FYET_SEQUENCE_START, FYNS_FLOW, NULL, NULL);
	RE_ASSERT(!rc);

	for (;;) {
		if (is_signed) {
			if (!val.sval)
				break;
		} else {
			if (!val.uval)
				break;
		}

		rfd = NULL;
		tval.uval = 0;
		for (i = 0; i < rw->rtd->fields_count; i++) {
			rfd = rw->rtd->fields[i];
			if (is_signed) {
				tval.sval = rfd->fi->sval;
				if (tval.sval && (val.sval & tval.sval) == tval.sval)
					break;
			} else {
				tval.uval = rfd->fi->uval;
				if (tval.uval && (val.uval & tval.uval) == tval.uval)
					break;
			}
		}

		RE_OUTPUT_CHECK(i < rw->rtd->fields_count, "unable to deduce enum sequence");
		RE_ASSERT(rfd);

		rc = enum_emit_single_scalar(emit, rw, flags, tval, rw->rtd);
		RE_ASSERT(!rc);

		if (is_signed)
			val.sval &= ~tval.sval;
		else
			val.uval &= ~tval.uval;
	}

	rc = fy_emit_eventf(emit, FYET_SEQUENCE_END, FYNS_ANY, NULL, NULL);
	RE_ASSERT(!rc);

	rc = 0;

out:
	return rc;

err_out:
	rc = -1;
	goto out;

err_output:
	rc = -EINVAL;
	goto out;
}

static int
enum_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	struct reflection_walker rw_num_a, rw_num_b;
	union integer_scalar val_a, val_b;
	int rc;

	assert(rw_a);
	assert(rw_b);
	assert(rw_a->rtd);
	assert(rw_a->rtd == rw_b->rtd);

	val_a = load_integer_scalar_rw(reflection_rw_dep(&rw_num_a, rw_a));
	val_b = load_integer_scalar_rw(reflection_rw_dep(&rw_num_b, rw_b));

	if (fy_type_kind_is_signed(rw_a->rtd->rtd_dep->ti->kind))
		rc = val_a.sval > val_b.sval ?  1 :
		     val_a.sval < val_b.sval ? -1 : 0;
	else
		rc = val_a.uval > val_b.uval ?  1 :
		     val_a.uval < val_b.uval ? -1 : 0;
	return rc;
}

static int
enum_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return !enum_cmp(rw_a, rw_b);
}

static int
enum_copy(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	struct reflection_walker rw_num_dst, rw_num_src;
	union integer_scalar val;

	assert(rw_dst);
	assert(rw_src);
	assert(rw_dst->rtd);
	assert(rw_dst->rtd == rw_src->rtd);

	val = load_integer_scalar_rw(reflection_rw_dep(&rw_num_src, rw_src));
	return store_integer_scalar_rw(reflection_rw_dep(&rw_num_dst, rw_dst), val);
}

static inline bool text_is_null(struct fy_parser *fyp, const char *text, size_t len)
{
	return (len == 1 && *text == '~') ||
	       (len == 4 && (!memcmp(text, "null", 4) || !memcmp(text, "Null", 4) || !memcmp(text, "NULL", 4)));
}

static inline bool fy_event_is_null(struct fy_parser *fyp, struct fy_event *fye)
{
	struct fy_token *fyt;
	const char *text;
	size_t len;

	if (fye->type != FYET_SCALAR || (fyt = fy_event_get_token(fye)) == NULL || fy_token_scalar_style(fyt) != FYSS_PLAIN)
		return false;

	text = fy_token_get_text(fyt, &len);
	if (!text)
		return false;

	return text_is_null(fyp, text, len);
}

static int
emit_ptr_NULL(struct fy_emitter *emit, struct reflection_walker *rw)
{
	int rc;

	rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_PLAIN, "null", 4, NULL, NULL);
	RE_ASSERT(!rc);

	return 0;

err_out:
	return -1;
}

static int
ptr_parse(struct fy_parser *fyp, struct reflection_walker *rw,
	  enum reflection_parse_flags flags)
{
	struct fy_event *fye = NULL;
	const struct fy_event *fye_peek = NULL;
	struct reflection_type_data *rtd, *rtd_dep;
	void *data, *p = NULL;
	struct reflection_walker rw_dep;
	int rc;

	rtd = rw->rtd;
	assert(rtd);

	rtd_dep = rtd->rtd_dep;
	assert(rtd_dep);

	RP_INPUT_CHECK(!(rtd_dep->ti->flags & FYTIF_UNRESOLVED),
			"Pointer to unresolved type '%s' cannot be serialized",
			rtd_dep->ti->name);

	assert(!(rw->flags & RWF_BITFIELD_DATA));
	data = rw->data;

	if (reflection_meta_get_bool(rtd->meta, rmvid_null_allowed)) {
		/* check if next event is NULL, directly set the pointer */
		fye_peek = fy_parser_parse_peek(fyp);
		RP_INPUT_CHECK(fye_peek != NULL,
			       "premature end of input while scanning for ptr\n");
		if (fy_event_is_null(fyp, (struct fy_event *)fye_peek)) {
			fye = fy_parser_parse(fyp);
			fy_parser_event_free(fyp, fye);
			return 0;
		}
	}

	p = reflection_type_data_alloc(rtd_dep);
	RP_ASSERT(p != NULL);

	memset(&rw_dep, 0, sizeof(rw_dep));
	rw_dep.parent = rw;
	rw_dep.rtd = rtd_dep;
	rw_dep.data = p;
	rw_dep.data_size.bytes = rtd_dep->ti->size;

	rc = reflection_parse_rw(fyp, &rw_dep, flags);
	if (rc)
		goto out;

	if (!(flags & RPF_NO_STORE))
		*(void **)data = p;
	else
		reflection_type_data_free(rtd_dep, p);

	return 0;

out:
	if (p)
		reflection_type_data_free(rtd_dep, p);
	return rc;

err_out:
	rc = -1;
	goto out;

err_input:
	rc = -EINVAL;
	goto out;
}

static void
ptr_dtor(struct reflection_walker *rw)
{
	struct reflection_walker rw_dep;
	void *ptr;

	ptr = *(void **)rw->data;
	if (!ptr)
		return;
	*(void **)rw->data = NULL;

	memset(&rw_dep, 0, sizeof(rw_dep));
	rw_dep.parent = rw;
	rw_dep.rtd = rw->rtd->rtd_dep;
	rw_dep.data = ptr;
	rw_dep.data_size.bytes = rw->rtd->rtd_dep->ti->size;

	reflection_free_rw(&rw_dep);
}

static int
ptr_copy(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	struct reflection_walker *rw;
	struct reflection_type_data *rtd = NULL, *rtd_dep;
	struct reflection_walker rw_dep_dst, rw_dep_src;
	void *src = NULL, *dst = NULL;
	int rc;

	assert(rw_dst);
	assert(rw_dst->data);
	assert(rw_src);
	assert(rw_src->data);

	rw = rw_src;

	RW_ASSERT(rw_src->rtd);
	RW_ASSERT(rw_dst->rtd == rw_src->rtd);

	rtd = rw_src->rtd;
	rtd_dep = rtd->rtd_dep;

	src = *(void **)rw_src->data;

	*(void **)rw_dst->data = NULL;

	if (!src)
		return 0;

	dst = reflection_type_data_alloc(rtd);
	RW_ASSERT(dst);

	memset(&rw_dep_dst, 0, sizeof(rw_dep_dst));
	memset(&rw_dep_src, 0, sizeof(rw_dep_src));

	rw_dep_dst.rtd = rw_dep_src.rtd = rtd_dep;
	rw_dep_dst.data_size.bytes = rw_dep_src.data_size.bytes = rtd_dep->ti->size;

	rw_dep_dst.parent = rw_dst;
	rw_dep_dst.data = dst;

	rw_dep_src.parent = rw_src;
	rw_dep_src.data = src;

	rc = reflection_copy_rw(&rw_dep_dst, &rw_dep_src);
	RW_ASSERT(!rc);

	*(void **)rw_dst->data = dst;

	return 0;

err_out:
	reflection_type_data_free(rtd, dst);
	return -1;
}

enum pointer_call2_type {
	PC2T_CMP,
	PC2T_EQ,
};

static int
ptr_call2(struct reflection_walker *rw_a, struct reflection_walker *rw_b,
	  enum pointer_call2_type call_type)
{
	struct reflection_type_data *rtd;
	struct reflection_walker rw_dep_a, rw_dep_b;
	void *a, *b;
	int rc;

	assert(rw_a);
	assert(rw_a->data);
	assert(rw_b);
	assert(rw_b->data);

	a = *(void **)rw_a->data;
	b = *(void **)rw_b->data;

	if (a == b)
		return 0;

	if (!a || !b)
		return -1;

	rtd = rw_b->rtd;
	assert(rtd);
	assert(rw_a->rtd == rtd);

	memset(&rw_dep_a, 0, sizeof(rw_dep_a));
	memset(&rw_dep_b, 0, sizeof(rw_dep_b));

	rw_dep_a.rtd = rw_dep_b.rtd = rtd->rtd_dep;
	rw_dep_b.data_size.bytes = rtd->rtd_dep->ti->size;

	rw_dep_a.parent = rw_a;
	rw_dep_a.data = a;

	rw_dep_b.parent = rw_b;
	rw_dep_b.data = b;

	switch (call_type) {
	case PC2T_CMP:
		rc = reflection_cmp_rw(&rw_dep_a, &rw_dep_b);
		break;
	case PC2T_EQ:
		rc = reflection_eq_rw(&rw_dep_a, &rw_dep_b);
		break;
	default:
		rc = -1;
		break;
	}
	return rc;
}

static int
ptr_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return ptr_call2(rw_a, rw_b, PC2T_CMP);
}

static int
ptr_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return ptr_call2(rw_a, rw_b, PC2T_EQ);
}

static int
ptr_char_parse(struct fy_parser *fyp, struct reflection_walker *rw,
	       enum reflection_parse_flags flags)
{
	struct fy_event *fye = NULL;
	struct fy_token *fyt;
	struct reflection_type_data *rtd, *rtd_dep;
	enum fy_type_kind type_kind;
	size_t size __attribute__((unused)), align __attribute__((unused));
	const char *text;
	size_t len, lenz;
	char *p = NULL;
	void *data;
	size_t data_size __attribute__((unused));
	struct str_instance_data id_local, *id = &id_local;
	int rc;

	rtd = rw->rtd;
	rtd_dep = rtd->rtd_dep;
	RP_ASSERT(rtd_dep);

	data = rw->data;
	RP_ASSERT(!(rw->flags & RWF_BITFIELD_DATA));
	data_size = rw->data_size.bytes;

	type_kind = rtd->ti->kind;
	RP_ASSERT(fy_type_kind_is_valid(type_kind));

	size = fy_type_kind_size(type_kind);
	align = fy_type_kind_align(type_kind);

	RP_ASSERT(data_size == size && ((size_t)(uintptr_t)data & (align - 1)) == 0);

	type_kind = rtd_dep->ti->kind;
	RW_ASSERT(type_kind == FYTK_CHAR);

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input\n");

	RP_INPUT_CHECK(fye->type == FYET_SCALAR,
		       "expecting FYET_SCALAR, got %s\n", fy_event_type_get_text(fye->type));

	fyt = fy_event_get_token(fye);
	text = fy_token_get_text(fyt, &len);
	RP_ASSERT(text != NULL);

	str_instance_data_setup(id, rw);

	RP_INPUT_CHECK(id->rfd_counter || !memchr(text, 0, len),
		"string contains \\0 which can't be stored without a counter\n");
	RP_INPUT_CHECK(id->rfd_counter || !id->not_null_terminated,
		"cannot have a string not null terminated without a counter\n");

	lenz = len + !id->not_null_terminated;

	if (id->rfd_counter) {
		rc = integer_field_store_check(id->rfd_counter, lenz);
		RP_INPUT_CHECK(!rc, "cannot store length %zu to counter", lenz);
	}

	if (!(flags & RPF_NO_STORE)) {

		p = reflection_malloc(rtd->rts, lenz);
		RP_ASSERT(p != NULL);

		memcpy(p, text, len);

		if (!id->not_null_terminated)
			p[len] = '\0';

		if (id->rfd_counter)
			struct_integer_field_store(id->rw_struct, id->rfd_counter, len);

		*(char **)data = p;
		p = NULL;
	}

	rc = 0;

out:
	if (p)
		reflection_free(rtd->rts, p);
	fy_parser_event_free(fyp, fye);
	return rc;

err_out:
	rc = -1;
	goto out;

err_input:
	rc = -EINVAL;
	goto out;
}

static int
ptr_char_emit(struct fy_emitter *emit, struct reflection_walker *rw,
	      enum reflection_emit_flags flags)
{
	struct reflection_type_data *rtd, *rtd_dep;
	enum fy_type_kind type_kind __attribute__((unused));
	struct str_instance_data id_local, *id = &id_local;
	const char *text;
	size_t len;
	int rc;

	rtd = rw->rtd;

	/* verify alignment */
	type_kind = rtd->ti->kind;
	assert(type_kind == FYTK_PTR);

	assert(rtd->ti->kind == FYTK_PTR);
	rtd_dep = rtd->rtd_dep;
	assert(rtd_dep);

	type_kind = rtd_dep->ti->kind;
	assert(type_kind == FYTK_CHAR);

	text = *(const char **)rw->data;
	if (!text)
		text = "null";

	str_instance_data_setup(id, rw);

	rc = str_instance_len(id, text, &len);
	RE_ASSERT(!rc);

	rc = fy_emit_eventf(emit, FYET_SCALAR, FYSS_ANY, text, len, NULL, NULL);
	RE_ASSERT(!rc);

	return 0;
err_out:
	return -1;
}

static void
ptr_char_dtor(struct reflection_walker *rw)
{
	void *ptr;

	ptr = *(void **)rw->data;
	if (!ptr)
		return;

	*(void **)rw->data = NULL;

	reflection_free(rw->rtd->rts, ptr);
}

static int
ptr_char_copy(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	struct reflection_walker *rw;
	struct reflection_type_data *rtd;
	struct str_instance_data id_src_local, *id_src = &id_src_local;
	struct str_instance_data id_dst_local, *id_dst = &id_dst_local;
	const char *str_src;
	size_t src_len, dst_lenz;
	char *p;
	int rc;

	assert(rw_dst);
	assert(rw_src);
	rw = rw_src;

	RW_ASSERT(rw_src->rtd);
	RW_ASSERT(rw_src->rtd == rw_dst->rtd);

	rtd = rw_src->rtd;

	RW_ASSERT(rw_src->data);
	RW_ASSERT(rw_dst->data);

	str_src = *(const char **)rw_src->data;

	/* nothing must be there before */
	RW_ASSERT(*(char **)rw_dst->data == NULL);

	if (str_src) {

		str_instance_data_setup(id_src, rw_src);
		str_instance_data_setup(id_dst, rw_dst);

		rc = str_instance_len(id_src, str_src, &src_len);
		RW_ASSERT(!rc);

		dst_lenz = src_len + !id_dst->not_null_terminated;

		if (id_dst->rfd_counter) {
			rc = integer_field_store_check(id_dst->rfd_counter, dst_lenz);
			RW_ASSERT(!rc);
		}

		p = reflection_malloc(rtd->rts, dst_lenz);
		RW_ASSERT(p);

		memcpy(p, str_src, src_len);

		if (!id_dst->not_null_terminated)
			p[src_len] = '\0';

		if (id_dst->rfd_counter)
			struct_integer_field_store(id_dst->rw_struct, id_dst->rfd_counter, src_len);
	} else
		p = NULL;

	*(char **)rw_dst->data = p;

	return 0;

err_out:
	return -1;
}

static int
ptr_char_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	struct reflection_walker *rw;
	struct str_instance_data id_a_local, *id_a = &id_a_local;
	struct str_instance_data id_b_local, *id_b = &id_b_local;
	const char *astr, *bstr;
	size_t alen, blen, len;
	int rc;

	assert(rw_a);
	assert(rw_b);

	rw = rw_a;

	RW_ASSERT(rw_a->data);
	RW_ASSERT(rw_b->data);

	astr = *(const char **)rw_a->data;
	bstr = *(const char **)rw_b->data;

	RW_ASSERT(astr && bstr);

	str_instance_data_setup(id_a, rw_a);
	str_instance_data_setup(id_b, rw_b);

	rc = str_instance_len(id_a, astr, &alen);
	RW_ASSERT(!rc);

	rc = str_instance_len(id_b, bstr, &blen);
	RW_ASSERT(!rc);

	len = alen < blen ? alen : blen;

	rc = memcmp(astr, bstr, len);
	if (rc)
		return rc > 0 ? 1 : -1;

	return alen < blen ? -1 :
	       alen > blen ?  1 : 0;

err_out:
	return -2;
}

static int
ptr_char_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	int rc;

	rc = ptr_char_cmp(rw_a, rw_b);
	if (rc <= -2)
		return rc;
	return !rc;
}

static const struct reflection_type_ops ptr_char_ops = {
	.name = "ptr_char",
	.parse = ptr_char_parse,
	.emit = ptr_char_emit,
	.dtor = ptr_char_dtor,
	.copy = ptr_char_copy,
	.cmp = ptr_char_cmp,
	.eq = ptr_char_eq,
};

static int
ptr_emit(struct fy_emitter *emit, struct reflection_walker *rw,
	 enum reflection_emit_flags flags)
{
	struct reflection_type_data *rtd, *rtd_dep;
	void *data;
	size_t data_size;
	struct reflection_walker rw_dep;
	int rc;

	rtd = rw->rtd;
	data = rw->data;
	assert(!(rw->flags & RWF_BITFIELD_DATA));
	data_size = rw->data_size.bytes;

	assert(rtd->ti->kind == FYTK_PTR);

	rtd_dep = rtd->rtd_dep;
	assert(rtd_dep);

	data = *(void **)data;
	if (!data)
		return emit_ptr_NULL(emit, rw);

	data_size = rtd_dep->ti->size;

	memset(&rw_dep, 0, sizeof(rw_dep));
	rw_dep.parent = rw;
	rw_dep.rtd = rtd_dep;
	rw_dep.data = data;
	rw_dep.data_size.bytes = data_size;

	rc = reflection_emit_rw(emit, &rw_dep, flags);
	RE_ASSERT(!rc);

	return 0;
err_out:
	return -1;
}

static int
typedef_parse(struct fy_parser *fyp, struct reflection_walker *rw,
	      enum reflection_parse_flags flags)
{
	struct reflection_walker rw_dep;
	struct reflection_walker *rw_new;

	rw_new = reflection_rw_dep(&rw_dep, rw);
	return reflection_parse_rw(fyp, rw_new, flags);
}

static int
typedef_emit(struct fy_emitter *emit, struct reflection_walker *rw,
	     enum reflection_emit_flags flags)
{
	struct reflection_walker rw_dep;

	return reflection_emit_rw(emit, reflection_rw_dep(&rw_dep, rw), flags);
}

static void
typedef_dtor(struct reflection_walker *rw)
{
	struct reflection_walker rw_dep;

	reflection_dtor_rw(reflection_rw_dep(&rw_dep, rw));
}

enum typedef_call2_type {
	TC2T_COPY,
	TC2T_CMP,
	TC2T_EQ,
};

static int
typedef_call2(struct reflection_walker *rw_a, struct reflection_walker *rw_b,
	      enum typedef_call2_type call_type)
{
	struct reflection_walker rw_dep_a, rw_dep_b;
	int rc;

	reflection_rw_dep(&rw_dep_a, rw_a);
	reflection_rw_dep(&rw_dep_b, rw_b);

	switch (call_type) {
	case TC2T_COPY:
		rc = reflection_copy_rw(&rw_dep_a, &rw_dep_b);
	case TC2T_CMP:
		rc = reflection_cmp_rw(&rw_dep_a, &rw_dep_b);
		break;
	case TC2T_EQ:
		rc = reflection_eq_rw(&rw_dep_a, &rw_dep_b);
		break;
	default:
		rc = -1;
		break;
	}

	return rc;
}

static int
typedef_copy(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	return typedef_call2(rw_dst, rw_src, TC2T_COPY);
}

static int
typedef_cmp(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return typedef_call2(rw_a, rw_b, TC2T_CMP);
}

static int
typedef_eq(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	return typedef_call2(rw_a, rw_b, TC2T_EQ);
}

static const struct reflection_type_ops dyn_array_ops = {
	.name = "dyn_array",
	.parse = common_array_parse,
	.emit = common_array_emit,
	.dtor = common_array_dtor,
	.copy = common_array_copy,
	.cmp = common_array_cmp,
	.eq = common_array_eq,
};

static const struct reflection_type_ops dyn_map_ops = {
	.name = "dyn_map",
	.parse = common_array_parse,
	.emit = common_array_emit,
	.dtor = common_array_dtor,
	.copy = common_array_copy,
	.cmp = common_array_cmp,
	.eq = common_array_eq,
};

static const struct reflection_type_ops const_map_ops = {
	.name = "const_map",
	.parse = common_array_parse,
	.emit = common_array_emit,
	.dtor = common_array_dtor,
	.copy = common_array_copy,
	.cmp = common_array_cmp,
	.eq = common_array_eq,
};

static int
ptr_doc_parse(struct fy_parser *fyp, struct reflection_walker *rw,
	      enum reflection_parse_flags flags)
{
	struct reflection_type_data *rtd, *rtd_dep;
	enum fy_type_kind type_kind;
	size_t size __attribute__((unused)), align __attribute__((unused));
	void *data;
	size_t data_size __attribute__((unused));
	struct fy_document_builder *fydb = NULL;
	struct fy_document *fyd;

	rtd = rw->rtd;

	assert(rtd->ti->kind == FYTK_PTR);

	rtd_dep = rtd->rtd_dep;
	assert(rtd_dep);

	data = rw->data;
	assert(!(rw->flags & RWF_BITFIELD_DATA));
	data_size = rw->data_size.bytes;

	assert(data);

	type_kind = rtd->ti->kind;
	assert(fy_type_kind_is_valid(type_kind));

	size = fy_type_kind_size(type_kind);
	align = fy_type_kind_align(type_kind);

	assert(data_size == size && ((size_t)(uintptr_t)data & (align - 1)) == 0);

	type_kind = rtd_dep->ti->kind;
	assert(type_kind == FYTK_VOID);

	if (!(flags & RPF_NO_STORE))
		*(char **)data = NULL;

	fydb = fy_document_builder_create_on_parser(fyp);
	RP_ASSERT(fydb != NULL);

	fyd = fy_document_builder_load_document(fydb, fyp);

	fy_document_builder_destroy(fydb);

	if (!fyd)
		return -1;

	if (!(flags & RPF_NO_STORE))
		*(void **)data = fyd;
	else
		fy_document_destroy(fyd);

	return 0;

err_out:
	fy_document_builder_destroy(fydb);
	return -1;
}

static int
ptr_doc_emit(struct fy_emitter *emit, struct reflection_walker *rw,
	     enum reflection_emit_flags flags)
{
	struct reflection_type_data *rtd, *rtd_dep;
	enum fy_type_kind type_kind __attribute__((unused));
	struct fy_document *fyd;
	int rc;

	rtd = rw->rtd;

	/* verify alignment */
	type_kind = rtd->ti->kind;
	assert(type_kind == FYTK_PTR);

	assert(rtd->ti->kind == FYTK_PTR);
	rtd_dep = rtd->rtd_dep;
	assert(rtd_dep);

	type_kind = rtd_dep->ti->kind;
	assert(type_kind == FYTK_VOID);

	fyd = *(struct fy_document **)rw->data;
	if (!fyd)
		return emit_ptr_NULL(emit, rw);

	rc = fy_emit_body_node(emit, fy_document_root(fyd));
	RE_ASSERT(!rc);

	return 0;

err_out:
	return -1;
}

static void
ptr_doc_dtor(struct reflection_walker *rw)
{
	struct fy_document *fyd;

	fyd = *(void **)rw->data;
	if (!fyd)
		return;

	*(void **)rw->data = NULL;

	fy_document_destroy(fyd);
}

static const struct reflection_type_ops ptr_doc_ops = {
	.name = "ptr_doc",
	.parse = ptr_doc_parse,
	.emit = ptr_doc_emit,
	.dtor = ptr_doc_dtor,
};

const struct reflection_type_ops reflection_ops_table[FYTK_COUNT] = {
	[FYTK_INVALID] = {
		.name = "invalid",
	},
	[FYTK_VOID] = {
		.name = "void",
		/* we can match null for void as entry point */
		.parse = null_scalar_parse,
		.emit = null_scalar_emit,
		.cmp = null_scalar_cmp,
		.eq = null_scalar_eq,
		.copy = null_scalar_copy,
	},
	[FYTK_NULL] = {
		.name = "null",
		.parse = null_scalar_parse,
		.emit = null_scalar_emit,
		.cmp = null_scalar_cmp,
		.eq = null_scalar_eq,
		.copy = null_scalar_copy,
	},
	[FYTK_BOOL] = {
		.name = "bool",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_CHAR] = {
		.name = "char",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_SCHAR] = {
		.name = "schar",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_UCHAR] = {
		.name = "uchar",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_SHORT] = {
		.name = "short",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_USHORT] = {
		.name = "ushort",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_INT] = {
		.name = "int",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_UINT] = {
		.name = "uint",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_LONG] = {
		.name = "long",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_ULONG] = {
		.name = "ulong",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_LONGLONG] = {
		.name = "longlong",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_ULONGLONG] = {
		.name = "ulonglong",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
#ifdef FY_HAS_INT128
	[FYTK_INT128] = {
		.name = "int128",
	},
	[FYTK_UINT128] = {
		.name = "uint128",
	},
#else
	[FYTK_INT128] = {
		.name = "int128",
	},
	[FYTK_UINT128] = {
		.name = "uint128",
	},
#endif
	[FYTK_FLOAT] = {
		.name = "float",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_DOUBLE] = {
		.name = "double",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
	[FYTK_LONGDOUBLE] = {
		.name = "longdouble",
		.parse = common_scalar_parse,
		.emit = common_scalar_emit,
		.cmp = common_scalar_cmp,
		.eq = common_scalar_eq,
		.copy = common_scalar_copy,
	},
#ifdef FY_HAS_FP16
	[FYTK_FLOAT16] = {
		.name = "float16",
	},
#else
	[FYTK_FLOAT16] = {
		.name = "float16",
	},
#endif
#ifdef FY_HAS_FLOAT128
	[FYTK_FLOAT128] = {
		.name = "float128",
	},
#else
	[FYTK_FLOAT128] = {
		.name = "float128",
	},
#endif
	/* these are templates */
	[FYTK_RECORD] = {
		.name = "record",
	},

	[FYTK_STRUCT] = {
		.name = "struct",
		.parse = struct_parse,
		.emit = struct_emit,
		.dtor = struct_dtor,
		.copy = struct_copy,
		.cmp = struct_cmp,
		.eq = struct_eq,
	},
	[FYTK_UNION] = {
		.name = "union",
		.parse = struct_parse,
		.emit = struct_emit,
		.dtor = struct_dtor,
		.copy = struct_copy,
		.cmp = struct_cmp,
		.eq = struct_eq,
	},
	[FYTK_ENUM] = {
		.name = "enum",
		.parse = enum_parse,
		.emit = enum_emit,
		.cmp = enum_cmp,
		.eq = enum_eq,
		.copy = enum_copy,
	},
	[FYTK_TYPEDEF] = {
		.name = "typedef",
		.parse = typedef_parse,
		.emit = typedef_emit,
		.dtor = typedef_dtor,
		.copy = typedef_copy,
		.cmp = typedef_cmp,
		.eq = typedef_eq,
	},
	[FYTK_PTR] = {
		.name = "ptr",
		.parse = ptr_parse,
		.emit = ptr_emit,
		.dtor = ptr_dtor,
		.copy = ptr_copy,
		.cmp = ptr_cmp,
		.eq = ptr_eq,
	},
	[FYTK_CONSTARRAY] = {
		.name = "const_array",
		.parse = common_array_parse,
		.emit = common_array_emit,
		.dtor = common_array_dtor,
		.copy = common_array_copy,
		.cmp = common_array_cmp,
		.eq = common_array_eq,
	},
	[FYTK_INCOMPLETEARRAY] = {
		.name = "incomplete_array",
	},
	[FYTK_FUNCTION] = {
		.name = "function",
	},
};

bool reflection_type_data_equal(const struct reflection_type_data *rtd1,
				const struct reflection_type_data *rtd2)
{
	struct reflection_field_data *rfd1, *rfd2;
	struct reflection_type_data *rtd_dep1, *rtd_dep2;
	bool recursive1, recursive2, eq;
	int i;

	if (rtd1 == rtd2)
		return true;

	if (!rtd1 || !rtd2)
		return false;

	eq = rtd1->rts == rtd2->rts &&
	     rtd1->ti == rtd2->ti &&
	     rtd1->ops == rtd2->ops &&
	     rtd1->flags == rtd2->flags &&
	     rtd1->flat_field_idx == rtd2->flat_field_idx &&
	     rtd1->has_anonymous_field == rtd2->has_anonymous_field &&
	     rtd1->first_anonymous_field_idx == rtd2->first_anonymous_field_idx &&
	     rtd1->rtd_dep_recursive == rtd2->rtd_dep_recursive &&
	     reflection_type_data_equal(rtd1->rtd_selector, rtd2->rtd_selector) &&
	     rtd1->selector_field_idx == rtd2->selector_field_idx &&
	     rtd1->union_field_idx == rtd2->union_field_idx &&
	     rtd1->fields_count == rtd2->fields_count &&
	     reflection_meta_compare(rtd1->meta, rtd2->meta);

	if (!eq)
		return false;

	/* recursive check */
	for (i = 0; i < rtd1->fields_count + (rtd1->rtd_dep ? 1 : 0); i++) {
		if (i < rtd1->fields_count) {
			rfd1 = rtd1->fields[i];
			rtd_dep1 = rfd1->rtd;
			recursive1 = rfd1->rtd_recursive;
			rfd2 = rtd2->fields[i];
			rtd_dep2 = rfd2->rtd;
			recursive2 = rfd2->rtd_recursive;
		} else {
			rtd_dep1 = rtd1->rtd_dep;
			recursive1 = rtd1->rtd_dep_recursive;
			rtd_dep2 = rtd2->rtd_dep;
			recursive2 = rtd2->rtd_dep_recursive;
		}

		/* recursive ones, are equal only if the dep pointers match */
		if ((recursive1 || recursive2) && rtd_dep1 != rtd_dep2)
			return false;

		eq = reflection_type_data_equal(rtd_dep1, rtd_dep2);
		if (!eq)
			return false;
	}

	return true;
}

static int
reflection_type_data_count_recursive(struct reflection_type_data *rtd)
{
	struct reflection_field_data *rfd;
	int i, count;

	if (!rtd)
		return 0;

	count = 1;	/* self */
	if (rtd->rtd_dep && !rtd->rtd_dep_recursive)
		count += reflection_type_data_count_recursive(rtd->rtd_dep);

	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];
		count += reflection_type_data_count_recursive(rfd_rtd(rfd));
	}
	return count;
}

static struct reflection_type_data **
reflection_type_data_collect_recursive(struct reflection_type_data *rtd,
				       struct reflection_type_data **rtds,
				       struct reflection_type_data **rtds_start,
				       struct reflection_type_data **rtds_end)
{
	struct reflection_type_data **rtdst;
	struct reflection_field_data *rfd;
	int i;

	if (!rtd || !rtds || !rtds_end || rtds >= rtds_end)
		return NULL;

	/* do not collect twice */
	for (rtdst = rtds_start; rtdst < rtds_end; rtdst++) {
		if (*rtdst == rtd)
			return rtds;
	}

	*rtds++ = rtd;	/* self */

	if (rtd->rtd_dep && !rtd->rtd_dep_recursive) {
		rtds = reflection_type_data_collect_recursive(rtd->rtd_dep, rtds, rtds_start, rtds_end);
		if (!rtds)
			return NULL;
	}

	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];
		rtds = reflection_type_data_collect_recursive(rfd_rtd(rfd), rtds, rtds_start, rtds_end);
		if (!rtds)
			return NULL;
	}

	return rtds;
}

static void
reflection_type_data_replace_by(struct reflection_type_data *rtd,
				struct reflection_type_data *rtd_replacement,
				struct reflection_type_data *rtd_to_replace)
{
	struct reflection_field_data *rfd;
	int i;

	if (!rtd || !rtd_replacement || !rtd_to_replace)
		return;

	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];
		if (rfd->rtd) {
			if (rfd->rtd == rtd_to_replace) {
				rfd->rtd = reflection_type_data_ref(rtd_replacement);
				reflection_type_data_unref(rtd_to_replace);
			}
			reflection_type_data_replace_by(rfd->rtd, rtd_replacement, rtd_to_replace);
		}
	}

	if (rtd->rtd_dep && !rtd->rtd_dep_recursive) {
		if (rtd->rtd_dep == rtd_to_replace) {
			rtd->rtd_dep = reflection_type_data_ref(rtd_replacement);
			reflection_type_data_unref(rtd_to_replace);
		}
		reflection_type_data_replace_by(rtd->rtd_dep, rtd_replacement, rtd_to_replace);
	}

	if (rtd->rtd_selector) {
		if (rtd->rtd_selector == rtd_to_replace) {
			rtd->rtd_selector = reflection_type_data_ref(rtd_replacement);
			reflection_type_data_unref(rtd_to_replace);
		}
		/* no recursion for rtd_selector */
	}
}

void *
reflection_type_data_alloc(struct reflection_type_data *rtd)
{
	void *ptr;

	if (!rtd)
		return NULL;

	ptr = reflection_malloc(rtd->rts, rtd->ti->size);
	if (ptr)
		memset(ptr, 0, rtd->ti->size);
	return ptr;
}

void *
reflection_type_data_alloc_array(struct reflection_type_data *rtd, size_t count)
{
	size_t item_size, size;
	void *ptr;

	if (!rtd)
		return NULL;

	item_size = rtd->ti->size;

#if defined(__GNUC__) || defined(__clang__)
	if (__builtin_mul_overflow(item_size, count, &size))
		return NULL;
#else
	size = item_size * count;
#endif
	ptr = reflection_malloc(rtd->rts, size);
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

void reflection_free_rw(struct reflection_walker *rw)
{
	if (!rw || !rw->rtd || !rw->data)
		return;

	if (reflection_type_data_has_dtor(rw->rtd))
		reflection_dtor_rw(rw);
	reflection_free(rw->rtd->rts, rw->data);
}

void reflection_type_data_free(struct reflection_type_data *rtd, void *ptr)
{
	struct reflection_walker *rw;
	struct reflection_root_ctx rctx_local, *rctx = &rctx_local;

	if (!rtd || !ptr)
		return;

	rw = reflection_root_ctx_setup(rctx, rtd, ptr, NULL);
	reflection_free_rw(rw);
}

enum reflection_type_data_sort {
	RTDS_DEPTH_ORDER,
	RTDS_ID,
	RTDS_TYPE_INFO,
	RTDS_TYPE_INFO_ID,
};

static int rtd_collect_compare_id(const void *va, const void *vb)
{
	const struct reflection_type_data * const *rtda = va, * const *rtdb = vb;

	return (*rtda)->idx < (*rtdb)->idx ? -1 :
	       (*rtda)->idx > (*rtdb)->idx ?  1 :
	       0;
}

static int rtd_collect_compare_type_info(const void *va, const void *vb)
{
	const struct reflection_type_data * const *rtda = va, * const *rtdb = vb;

	return (*rtda)->ti < (*rtdb)->ti ? -1 :
	       (*rtda)->ti > (*rtdb)->ti ?  1 :
	       0;
}

/* sort by type info, and then by id */
static int rtd_collect_compare_type_info_id(const void *va, const void *vb)
{
	int rc;

	rc = rtd_collect_compare_type_info(va, vb);
	if (rc)
		return rc;

	return rtd_collect_compare_id(va, vb);
}

/* terminated with NULL */
struct reflection_type_data **
reflection_type_data_collect(struct reflection_type_data *rtd, enum reflection_type_data_sort sort)
{
	struct reflection_type_data **rtds, **rtds_start, **rtds_end;
	int (*cmpf)(const void *va, const void *vb);
	size_t sz;
	int count;

	/* concervative count */
	count = reflection_type_data_count_recursive(rtd);
	if (count <= 0)
		return NULL;

	sz = sizeof(*rtds_start) * (count + 1);
	rtds_start = malloc(sizeof(*rtds_start) * (count + 1));
	if (!rtds_start)
		return NULL;
	memset(rtds_start, 0, sz);

	rtds_end = rtds_start + count;
	rtds = reflection_type_data_collect_recursive(rtd, rtds_start, rtds_start, rtds_end);
	if (!rtds || rtds > rtds_end) {
		free(rtds_start);
		return NULL;
	}
	count = rtds - rtds_start;
	*rtds++ = NULL;	/* terminator */

	switch (sort) {
	case RTDS_ID:
		cmpf = rtd_collect_compare_id;
		break;
	case RTDS_TYPE_INFO:
		cmpf = rtd_collect_compare_type_info;
		break;
	case RTDS_TYPE_INFO_ID:
		cmpf = rtd_collect_compare_type_info_id;
		break;
	case RTDS_DEPTH_ORDER:
	default:
		cmpf = NULL;
		break;
	}

	if (cmpf)
		qsort(rtds_start, count, sizeof(*rtds_start), cmpf);

	return rtds_start;
}

void reflection_type_data_dump(struct reflection_type_data *rtd_root)
{
	struct reflection_type_system *rts;
	struct reflection_type_data **rtds = NULL, **rtdsp;
	const struct reflection_field_data *rfd, *rfd_ref_root;
	const struct reflection_type_data *rtd, *rtdf, *rtd_ref_root;
	int j;

	if (!rtd_root)
		return;

	rts = rtd_root->rts;

	rtd_ref_root = rts->root_ref ? rts->root_ref->rtd_root : NULL;

	printf("%s: root #%d:'%s'", __func__, rtd_root->idx, rtd_root->ti->name);
	if (rtd_ref_root == rtd_root) {
		rfd_ref_root = rts->root_ref->rfd_root;
#if 0
		if (rfd_ref_root && rfd_ref_root->yaml_annotation)
			printf(" A=%s", fy_emit_document_to_string_alloca(
					rfd_ref_root->yaml_annotation,
					FYECF_WIDTH_INF | FYECF_INDENT_DEFAULT |
					FYECF_MODE_FLOW_ONELINE | FYECF_NO_ENDING_NEWLINE));
#endif
		if (rfd_ref_root && reflection_meta_has_explicit(rfd_ref_root->meta))
			printf(" M=%s", reflection_meta_get_document_str_alloca(rfd_ref_root->meta));
	}
	printf("\n");

	rtds = reflection_type_data_collect(rtd_root, RTDS_ID);
	if (!rtds) {
		printf("FAILED to collect type data for print!\n");
		return;
	}

	rtdsp = rtds;
	while ((rtd = *rtdsp++) != NULL) {

		printf("#%d:'%s' r%d (%s)", rtd->idx, rtd->ti->name, rtd->refs, rtd->ops->name);
		// printf(" T#%d", fy_type_info_get_id(rtd->ti));

		printf("%s%s%s%s%s",
				(rtd->flags & RTDF_PURITY_MASK) == 0 ? " PURE" : "",
				(rtd->flags & RTDF_IMPURE) ? " IMPURE" : "",
				(rtd->flags & RTDF_MUTATED) ? " MUTATED" : "",
				(rtd->flags & RTDF_SPECIALIZED) ? " SPECIALIZED" : "",
				(rtd->flags & RTDF_SPECIALIZING) ? " SPECIALIZING" : "");
		printf("%s%s",
				(rtd->ti->flags & FYTIF_ANONYMOUS) ? " ANONYMOUS" : "",
				(rtd->ti->flags & FYTIF_ANONYMOUS_RECORD_DECL) ? " ANONYMOUS_RECORD_DECL" : "");
		if (rtd->rtd_dep)
			printf(" dep: #%d:'%s'%s", rtd->rtd_dep->idx, rtd->rtd_dep->ti->name,
					rtd->rtd_dep_recursive ? " (recursive)" : "");
#if 0
		if (rtd->yaml_annotation_str)
			printf(" A=%s", rtd->yaml_annotation_str);
#endif
		if (reflection_meta_has_explicit(rtd->meta))
			printf(" M=%s", reflection_meta_get_document_str_alloca(rtd->meta));

		printf("\n");
		for (j = 0; j < rtd->fields_count; j++) {
			rfd = rtd->fields[j];
			rtdf = rfd_rtd(rfd);
			printf("\t#%d:'%s' %s", rtdf->idx, rtdf->ti->name, rfd->fi->name);
			if (strcmp(rfd->fi->name, rfd->field_name))
				printf(" (%s)", rfd->field_name);
			if (rfd->rtd_recursive)
				printf(" (recursive)");
			printf("%s%s%s",
					(rfd->fi->type_info->flags & FYTIF_CONST) ? " CONST" : "",
					(rfd->fi->type_info->flags & FYTIF_VOLATILE) ? " VOLATILE" : "",
					(rfd->fi->type_info->flags & FYTIF_RESTRICT) ? " RESTRICT" : "");
#if 0
			if (rfd->yaml_annotation_str)
				printf(" A=%s", rfd->yaml_annotation_str);
			if (reflection_meta_has_explicit(rfd->meta))
				printf(" M=%s", reflection_meta_get_document_str_alloca(rfd->meta));
#endif
			printf("\n");
		}
	}

	free(rtds);
}

void *
reflection_type_data_generate_value(struct reflection_type_data *rtd, struct fy_node *fyn)
{
	static const struct fy_parse_cfg cfg_i = { .search_path = "", .flags = 0, };
	struct reflection_type_system *rts;
	struct fy_parser *fyp_i = NULL;
	struct fy_document_iterator *fydi = NULL;
	void *value;
	int rc;

	assert(rtd);

	rts = rtd->rts;
	assert(rts);

	RTS_ASSERT(fyn);

	fyp_i = fy_parser_create(&cfg_i);
	RTS_ASSERT(fyp_i);

	fydi = fy_document_iterator_create_on_node(fyn);
	RTS_ASSERT(fydi);

	rc = fy_parser_set_document_iterator(fyp_i, FYPEGF_GENERATE_ALL_EVENTS, fydi);
	RTS_ASSERT(!rc);

	rc = reflection_parse(fyp_i, rtd, &value, RPF_SILENT_ALL);
	RTS_ASSERT(!rc);

out:
	fy_document_iterator_destroy(fydi);
	fy_parser_destroy(fyp_i);
	return value;

err_out:
	value = NULL;
	goto out;
}

int
reflection_type_data_generate_value_into(struct reflection_type_data *rtd, struct fy_node *fyn, void *data)
{
	static const struct fy_parse_cfg cfg_i = { .search_path = "", .flags = 0, };
	struct reflection_type_system *rts;
	struct fy_parser *fyp_i = NULL;
	struct fy_document_iterator *fydi = NULL;
	int rc;

	assert(rtd);

	rts = rtd->rts;
	assert(rts);

	RTS_ASSERT(fyn);

	fyp_i = fy_parser_create(&cfg_i);
	RTS_ASSERT(fyp_i);

	fydi = fy_document_iterator_create_on_node(fyn);
	RTS_ASSERT(fydi);

	rc = fy_parser_set_document_iterator(fyp_i, FYPEGF_GENERATE_ALL_EVENTS, fydi);
	RTS_ASSERT(!rc);

	rc = reflection_parse_into(fyp_i, rtd, data, RPF_SILENT_ALL);
	RTS_ASSERT(!rc);

out:
	fy_document_iterator_destroy(fydi);
	fy_parser_destroy(fyp_i);
	return rc;

err_out:
	rc = -1;
	goto out;
}

int
reflection_type_data_put_value_into(struct reflection_type_data *rtd, void *data, struct fy_node *fyn_value, void *value)
{
	struct reflection_type_system *rts;
	struct reflection_walker rw_dst, rw_src;
	int rc;

	assert(rtd);
	rts = rtd->rts;
	assert(rts);

	RTS_ASSERT(data && fyn_value);

	/* copy from the already prepared cache value */
	if (value) {
		rc = reflection_copy_rw(
				reflection_rw_value(&rw_dst, rtd, data),
				reflection_rw_value(&rw_src, rtd, value));
		if (!rc)
			return 0;
		/* we have to generate manually */
	}
	// fprintf(stderr, "%s: generating manually\n", __func__);
	/* no canned default available, must be created each time */
	rc = reflection_type_data_generate_value_into(rtd, fyn_value, data);
	RTS_ASSERT(!rc);

	return 0;
err_out:
	return -1;
}

void
reflection_walker_print_path(struct reflection_walker *rw)
{
	struct reflection_walker **rw_path, *rwt;
	struct reflection_type_data *rtd;
	struct reflection_field_data *rfd;
	int i, count;

	count = 0;
	rwt = rw;
	while (rwt) {
		count++;
		rwt = rwt->parent;
	}

	rw_path = alloca(count * sizeof(*rw_path));
	i = count;
	rwt = rw;
	while (rwt) {
		rw_path[--i] = rwt;
		rwt = rwt->parent;
	}

	fprintf(stderr, A_BRIGHT_YELLOW "%s: " A_RESET "\n", __func__);
	for (i = 0; i < count; i++) {
		rwt = rw_path[i];

		fprintf(stderr, A_BRIGHT_YELLOW "  RWT %s(%s)%s%s%s%s%s%s%s%s%s%s" A_RESET,
				rwt->rtd->ti->name, rwt->rtd->ops->name,
				(rwt->flags & RWF_FIELD_IDX) ? " FIELD_IDX" : "",
				(rwt->flags & RWF_SEQ_IDX) ? " SEQ_IDX" : "",
				(rwt->flags & RWF_KEY) ? " KEY" : "",
				(rwt->flags & RWF_VALUE) ? " VALUE" : "",
				(rwt->flags & RWF_TEXT_KEY) ? " TEXT_KEY" : "",
				(rwt->flags & RWF_SEQ_KEY) ? " SEQ_KEY" : "",
				(rwt->flags & RWF_MAP) ? " MAP" : "",
				(rwt->flags & RWF_SEQ) ? " SEQ" : "",
				(rwt->flags & RWF_COMPLEX_KEY) ? " COMPLEX_KEY" : "",
				(rwt->flags & RWF_BITFIELD_DATA) ? " BITFIELD_DATA" : "");

		if ((rwt->flags & (RWF_VALUE | RWF_TEXT_KEY)) == (RWF_VALUE | RWF_TEXT_KEY)) {
			fprintf(stderr, A_BRIGHT_YELLOW " key=%s" A_RESET,
					rwt->text_key);
		}

		if (rwt->flags & RWF_SEQ_IDX) {
			rtd = rwt->parent->rtd;
			fprintf(stderr, A_BRIGHT_YELLOW " seq-idx=%ju (%s[%ju])" A_RESET,
					rwt->idx, rtd->ti->name, rwt->idx);
		}

		if (rwt->flags & RWF_FIELD_IDX) {
			rtd = rwt->parent->rtd;
			assert(rtd);
			if (rtd->ti->kind == FYTK_STRUCT || rtd->ti->kind == FYTK_UNION) {
				assert(rwt->idx < (uintmax_t)rtd->fields_count);
				rfd = rtd->fields[rwt->idx];
				fprintf(stderr, A_BRIGHT_YELLOW " field-idx=%ju (%s.%s)" A_RESET,
						rwt->idx, rtd->ti->name, rfd->fi->name);
			} else {
				fprintf(stderr, A_BRIGHT_YELLOW " field-idx=%ju NON-STRUCT (%s)" A_RESET,
						rwt->idx, rtd->ti->name);
			}
		}

		fprintf(stderr, "\n" A_RESET);
	}
}

char *
reflection_walker_get_path(struct reflection_walker *rw)
{
	struct reflection_walker **rw_path, *rwt;
	int i, count;
	bool pending_value, in_map, in_seq;
	char idxbuf[64];
	char *s, *e;
	char *buf, *new_buf;
	size_t len, size;
	struct fy_emitter *emit = NULL;
	char *emit_str = NULL;
	int rc;

	if (!rw)
		return NULL;

	count = 0;
	rwt = rw;
	while (rwt) {
		count++;
		rwt = rwt->parent;
	}

	rw_path = alloca(count * sizeof(*rw_path));
	i = count;
	rwt = rw;
	while (rwt) {
		rw_path[--i] = rwt;
		rwt = rwt->parent;
	}

	size = 128;

	buf = malloc(size);
	RW_ASSERT(buf);

	s = buf;
	e = &buf[size];
	*s++ = '/';

#undef APPEND_STR
#define APPEND_STR(_ptr, _len) \
	do { \
		const void *__ptr = (_ptr); \
		size_t __len = (_len); \
		size_t __pos; \
		if (s + __len >= e) { \
			__pos = s - buf; \
			while (__pos + __len < size) \
				size *= 2; \
			new_buf = realloc(buf, size * 2); \
			RW_ASSERT(new_buf); \
			buf = new_buf; \
			s = buf + __pos; \
			e = buf + size; \
		} \
		assert(s + __len <= e); \
		memcpy(s, __ptr, __len); \
		s += __len; \
	} while(0)

#define APPEND_CH(_ch) \
	do { \
		char __ch = (_ch); \
		APPEND_STR(&__ch, 1); \
	} while(0)

	pending_value = true;
	in_map = in_seq = false;
	for (i = 0; i < count; i++) {
		rwt = rw_path[i];

		if ((rwt->flags & (RWF_VALUE | RWF_TEXT_KEY)) == (RWF_VALUE | RWF_TEXT_KEY))  {

			if (s[-1] != '/')
				APPEND_CH('/');

			len = strlen(rwt->text_key);

			// fprintf(stderr, A_BRIGHT_CYAN "%s: TEXT_KEY %.*s" A_RESET "\n", __func__, (int)len, rwt->text_key);

			APPEND_STR(rwt->text_key, len);

			pending_value = true;

		} else if ((rwt->flags & (RWF_VALUE | RWF_COMPLEX_KEY)) == (RWF_VALUE | RWF_COMPLEX_KEY)) {

			if (s[-1] != '/')
				APPEND_CH('/');

			emit = fy_emit_to_string(FYECF_WIDTH_INF | FYECF_INDENT_DEFAULT |
						 FYECF_MODE_FLOW_ONELINE | FYECF_NO_ENDING_NEWLINE |
						 FYECF_DOC_START_MARK_OFF | FYECF_DOC_END_MARK_OFF |
						 FYECF_VERSION_DIR_OFF | FYECF_TAG_DIR_OFF);
			RW_ASSERT(emit);

			rc = fy_emit_eventf(emit, FYET_STREAM_START);
			RW_ASSERT(!rc);

			rc = fy_emit_eventf(emit, FYET_DOCUMENT_START, 1, NULL, NULL);
			RW_ASSERT(!rc);

			rc = reflection_emit_rw(emit, rwt->rw_key, REF_SILENT_ALL);
			RW_ASSERT(!rc);

			rc = fy_emit_eventf(emit, FYET_DOCUMENT_END, 1);
			RW_ASSERT(!rc);

			rc = fy_emit_eventf(emit, FYET_STREAM_END);
			RW_ASSERT(!rc);

			emit_str = fy_emit_to_string_collect(emit, &len);
			RW_ASSERT(emit_str);

			// fprintf(stderr, A_BRIGHT_CYAN "%s: COMPLEX_KEY %.*s" A_RESET "\n", __func__, (int)len, emit_str);

			APPEND_STR(emit_str, len);

			free(emit_str);
			emit_str = NULL;

			fy_emitter_destroy(emit);
			emit = NULL;

			pending_value = true;
		}

		if (rwt->flags & (RWF_MAP | RWF_SEQ)) {

			in_map = (rwt->flags & RWF_MAP);
			in_seq = !in_map;

			if (s[-1] != '/')
				APPEND_CH('/');

			pending_value = !!(rwt->flags & RWF_KEY);

			// fprintf(stderr, A_BRIGHT_CYAN "%s: %s" A_RESET "\n", __func__, in_map ? "MAP" : "SEQ");

		} else if (rwt->flags & RWF_SEQ_KEY) {

			if (pending_value)
				pending_value = false;

			if (!in_seq) {
				if (s[-1] != '/')
					APPEND_CH('/');
			}

			snprintf(idxbuf, sizeof(idxbuf) - 1, "%ju", rwt->idx);
			len = strlen(idxbuf);

			// fprintf(stderr, A_BRIGHT_CYAN "%s: SEQ_KEY %ju" A_RESET "\n", __func__, rwt->idx);

			APPEND_STR(idxbuf, len);
		}
	}

	/* trim trailing / */
	if (s > buf && s[-1] == '/')
		s--;

	APPEND_CH('\0');

	return buf;

err_out:
	if (emit_str)
		free(emit_str);
	if (emit)
		fy_emitter_destroy(emit);
	if (buf)
		free(buf);
	return NULL;
}

#define reflection_walker_get_path_alloca(_rw) \
	FY_ALLOCA_COPY_FREE(reflection_walker_get_path((_rw)), FY_NT)

int
reflection_parse_rw(struct fy_parser *fyp, struct reflection_walker *rw, enum reflection_parse_flags flags)
{
	int rc;

	assert(fyp);
	assert(rw);
	assert(rw->rtd);
	assert(rw->rtd->ti);
	assert(rw->rtd->ops);

	/* does the type have a parse method? if not it's unsupported */
	RW_ERROR_CHECK(rw, rw->rtd->ops->parse != NULL, err_out, "unsupported type/kind %s", rw->rtd->ti->name);

#if 0
	fprintf(stderr, A_BRIGHT_RED "%s: parse T#%d/%s (%s) %s" A_RESET "\n", __func__, rw->rtd->idx,
			rw->rtd->ti->name, rw->rtd->ops->name, reflection_walker_get_path_alloca(rw));
#endif

#if 0
	if (!(flags & RPF_NO_STORE)) {
		reflection_walker_print_path(rw);
	}
#endif

	rc = rw->rtd->ops->parse(fyp, rw, flags);

out:
	return rc;

err_out:
	rc = -EINVAL;
	goto out;
}

int
reflection_emit_rw(struct fy_emitter *emit, struct reflection_walker *rw, enum reflection_emit_flags flags)
{
	int rc;

	assert(emit);
	assert(rw);
	assert(rw->rtd);
	assert(rw->rtd->ops);
	assert(rw->rtd->ops->emit);

	rc = rw->rtd->ops->emit(emit, rw, flags);

	return rc;
}

void reflection_dtor_rw(struct reflection_walker *rw)
{
	assert(rw);
	assert(rw->rtd);

	if (!reflection_type_data_has_dtor(rw->rtd))
		return;

	assert(rw->rtd->ops->dtor);
	rw->rtd->ops->dtor(rw);
}

int reflection_copy_rw(struct reflection_walker *rw_dst, struct reflection_walker *rw_src)
{
	struct reflection_type_data *rtd;
	int rc;

	assert(rw_dst);
	assert(rw_dst->data);
	assert(rw_src);
	assert(rw_src->data);

	assert(rw_src->rtd);
	assert(rw_dst->rtd);
	assert(rw_dst->rtd == rw_src->rtd);

	rtd = rw_src->rtd;

	/* pure, can just copy */
	if ((rtd->flags & RTDF_PURITY_MASK) == 0 &&
	    !(rw_src->flags & rw_dst->flags & RWF_BITFIELD_DATA)) {
		memcpy(rw_dst->data, rw_src->data, rtd->ti->size);
		rc = 0;
	} else {
		assert(rtd->ops->copy);
		rc = rtd->ops->copy(rw_dst, rw_src);
	}

	return rc;
}

/* -1 a < b, 1 a > b, 0 a == b */
int reflection_cmp_rw(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	struct reflection_type_data *rtd;
	int rc;

	assert(rw_a);
	assert(rw_a->data);
	assert(rw_b);
	assert(rw_b->data);

	assert(rw_b->rtd);
	assert(rw_a->rtd);
	assert(rw_a->rtd == rw_b->rtd);

	rtd = rw_b->rtd;

	assert(rtd->ops->cmp);
	rc = rtd->ops->cmp(rw_a, rw_b);

	return rc;
}

int reflection_eq_rw(struct reflection_walker *rw_a, struct reflection_walker *rw_b)
{
	struct reflection_type_data *rtd;
	int rc;

	assert(rw_a);
	assert(rw_a->data);
	assert(rw_b);
	assert(rw_b->data);

	assert(rw_b->rtd);
	assert(rw_a->rtd);
	assert(rw_a->rtd == rw_b->rtd);

	rtd = rw_b->rtd;

	/* pure? memcmp it */
	if ((rtd->flags & RTDF_PURITY_MASK) == 0 &&
	    !(rw_a->flags & rw_b->flags & RWF_BITFIELD_DATA)) {
		rc = !memcmp(rw_a->data, rw_b->data, rtd->ti->size);
		assert(rc == 0 || rc == 1);
	} else {
		assert(rtd->ops->eq);
		rc = rtd->ops->eq(rw_a, rw_b);
	}

	return rc;
}

struct fy_node *
reflection_annotation_get_node(struct fy_document *fyd, const char *path)
{
	struct fy_node *fyn_root;

	fyn_root = fy_document_root(fyd);
	if (!fyn_root)
		return NULL;

	return fy_node_by_path(fyn_root, path, FY_NT, FYNWF_DONT_FOLLOW);
}

const char *
reflection_annotation_get_string(struct fy_document *fyd, const char *path)
{
	struct fy_node *fyn;
	struct fy_token *fyt;
	const char *text0;

	fyn = reflection_annotation_get_node(fyd, path);
	if (!fyn)
		return NULL;

	fyt = fy_node_get_scalar_token(fyn);
	if (!fyt)
		return NULL;

	text0 = fy_token_get_text0(fyt);
	if (!text0)
		return NULL;

	return text0;
}

void reflection_field_data_destroy(struct reflection_field_data *rfd)
{
	struct reflection_type_data *rtd;

	if (!rfd)
		return;

	rtd = rfd_rtd(rfd);

	if (rfd->meta)
		reflection_meta_destroy(rfd->meta);

	if (rtd)
		reflection_type_data_unref(rtd);

	if (rfd->field_name)
		free(rfd->field_name);

	free(rfd);
}

static struct reflection_field_data *
reflection_field_data_create_internal(struct reflection_type_system *rts,
				      struct reflection_type_data *rtd_parent, int idx)
{
	struct reflection_field_data *rfd;
	const struct fy_type_info *ti_parent;
	bool has_fields;
	const char *remove_prefix, *field_name;
	size_t pfx_len, name_len;
	int rc;

	rfd = malloc(sizeof(*rfd));
	RTS_ASSERT(rfd);

	memset(rfd, 0, sizeof(*rfd));

	ti_parent = rtd_parent ? rtd_parent->ti : NULL;
	has_fields = ti_parent && fy_type_kind_has_fields(ti_parent->kind);
	remove_prefix = has_fields ? reflection_meta_get_str(rtd_parent->meta, rmvid_remove_prefix) : NULL;

	rfd->rts = rts;
	rfd->idx = idx;
	rfd->rtd_parent = rtd_parent;
	rfd->fi = has_fields ? &ti_parent->fields[rfd->idx] : NULL;

	rfd->meta = reflection_meta_create(rts);
	RTS_ASSERT(rfd->meta);

	if (rfd->fi) {
		rfd->yaml_annotation = fy_field_info_get_yaml_annotation(rfd->fi);
		rfd->yaml_annotation_str = fy_field_info_get_yaml_comment(rfd->fi);

		rc = reflection_meta_fill(rfd->meta, fy_document_root(rfd->yaml_annotation));
		RTS_ASSERT(!rc);
	}

	if (has_fields) {

		/* assign field name early */
		field_name = reflection_meta_get_str(rfd->meta, rmvid_name);

		if (field_name) {
			rfd->field_name = strdup(field_name);
			RTS_ASSERT(rfd->field_name);
		}

		if (remove_prefix) {
			pfx_len = strlen(remove_prefix);
			name_len = strlen(rfd->fi->name);
			if (name_len > pfx_len && !memcmp(remove_prefix, rfd->fi->name, pfx_len)) {
				name_len -= pfx_len;
				rfd->field_name = malloc(name_len + 1);
				RTS_ASSERT(rfd->field_name);

				/* remove prefix */
				memcpy(rfd->field_name, rfd->fi->name + pfx_len, name_len + 1);
			}
		}

		if (!rfd->field_name) {
			rfd->field_name = strdup(rfd->fi->name);
			RTS_ASSERT(rfd->field_name);
		}
	}

	return rfd;

err_out:
	reflection_field_data_destroy(rfd);
	return NULL;
}

struct reflection_field_data *
reflection_field_data_create(struct reflection_type_data *rtd_parent, int idx)
{
	return reflection_field_data_create_internal(rtd_parent->rts, rtd_parent, idx);
}

void reflection_type_data_destroy(struct reflection_type_data *rtd)
{
	struct reflection_field_data *rfd;
	int i;

	if (!rtd)
		return;

	reflection_meta_destroy(rtd->meta);

	if (rtd->rtd_dep && !rtd->rtd_dep_recursive)
		reflection_type_data_unref(rtd->rtd_dep);

	if (rtd->fields) {
		for (i = rtd->fields_count - 1; i >= 0; i--) {
			rfd = rtd->fields[i];
			reflection_field_data_destroy(rfd);
		}
		free(rtd->fields);
	}

	reflection_type_data_unref(rtd->rtd_selector);

	free(rtd);
}

int
reflection_field_data_specialize(struct reflection_field_data *rfd)
{
	struct reflection_type_system *rts;
	struct reflection_type_data *rtd, *rtd_parent, *rtd_final_dep, *rtd_resolved_typedef;
	struct reflection_field_data *rfd_ref, *rfd_key;
	enum fy_type_kind type_kind, resolved_type_kind, final_dependent_type_kind;
	const char *counter;
	const char *key;
	bool is_dyn_array, is_const_array, is_incomplete_array, is_array, is_map;
	bool has_counter, has_terminator, has_key;

	if (!rfd)
		return -1;

	rtd = rfd_rtd(rfd);
	assert(rtd);

	rts = rtd->rts;
	assert(rts);

	final_dependent_type_kind = reflection_type_data_final_dependent(rtd)->ti->kind;
	rfd->is_unsigned = fy_type_kind_is_unsigned(final_dependent_type_kind);
	rfd->is_signed = fy_type_kind_is_signed(final_dependent_type_kind);

	rtd_parent = rfd->rtd_parent;	// may be NULL for root rfd

	rtd_final_dep = reflection_type_data_final_dependent(rtd);
	assert(rtd_final_dep);

	rtd_resolved_typedef = reflection_type_data_resolved_typedef(rtd);
	assert(rtd_resolved_typedef);

	type_kind = rtd->ti->kind;
	resolved_type_kind = rtd_resolved_typedef->ti->kind;

	// fprintf(stderr, A_RED "%s: %s.%s" A_RESET "\n", __func__, rtd_parent->ti->name, rfd->fi->name);

	if (reflection_meta_get_bool(rtd->meta, rmvid_match_null)) {

		RTS_ERROR_CHECK(!(rtd->flags & RTDF_MUTATED),
				"type '%s' already mutated", rtd->ti->name);

		rtd->ops = &reflection_ops_table[FYTK_NULL];
		rtd->flags |= RTDF_MUTATED;
	}

	switch (resolved_type_kind) {
	case FYTK_UNION:
		rtd->union_field_idx = rfd->idx;
		break;

	default:
		break;

	}

	counter = rtd_parent ? reflection_meta_get_str(rtd->meta, rmvid_counter) : NULL;
	has_counter = counter != NULL;

	has_terminator = reflection_meta_get_any_value(rtd->meta, rmvid_terminator) != NULL;

	/* key only makes sense if the final dep is a struct */
	key = rtd_final_dep->ti->kind == FYTK_STRUCT ? reflection_meta_get_str(rtd->meta, rmvid_key) : NULL;
	has_key = key != NULL;

	if (has_counter) {
		rfd_ref = reflection_type_data_lookup_field(rtd_parent, counter);

		RTS_ERROR_CHECK(rfd_ref,
				"dyn_array counter field %s.%s does not exist\n", rtd_parent->ti->name, counter);

		RTS_ERROR_CHECK(fy_type_kind_is_integer(rfd_ref->fi->type_info->kind),
				"dyn_array counter field (%s) must be integer\n", counter);

		RTS_ERROR_CHECK(!rfd_ref->is_counter,
				"dyn_array counter field (%s) must be a single counter\n", counter);

		rfd_ref->is_counter = true;
	} else
		rfd_ref = NULL;

	/* don't do arrays on typedefs */
	if ((rtd->flags & RTDF_MUTATED) || type_kind == FYTK_TYPEDEF)
		goto skip_array;

	is_dyn_array = resolved_type_kind == FYTK_PTR &&
		       (has_counter || has_terminator || has_key ||
		       (rtd->rtd_dep && rtd->rtd_dep->ti->kind == FYTK_PTR));

	is_const_array = resolved_type_kind == FYTK_CONSTARRAY && !is_dyn_array;

	is_incomplete_array = false;	/* XXX not yet */

	is_array = is_dyn_array || is_const_array || is_incomplete_array;
	is_map = is_array && rtd_final_dep->ti->kind == FYTK_STRUCT && has_key;

	if (is_map) {
		/* the dependent type must be a structure */

		RTS_ASSERT(rtd_final_dep->ti->kind == FYTK_STRUCT);

		rfd_key = reflection_type_data_lookup_field(rtd_final_dep, key);
		RTS_ERROR_CHECK(rfd_key,
				"unable to find key field '%s' in '%s'", key, rtd_final_dep->ti->name);

		RTS_ERROR_CHECK(rtd_final_dep->fields_count == 2,
				"'%s' does not have only two fields (key/value)", rtd_final_dep->ti->name);
	}

	if (is_array) {
		if (is_dyn_array)
			rtd->ops = !is_map ? &dyn_array_ops : &dyn_map_ops;
		else if (is_const_array)
			rtd->ops = !is_map ? &reflection_ops_table[FYTK_CONSTARRAY] : &const_map_ops;
		else
			rtd->ops = NULL;
		RTS_ASSERT(rtd->ops);
		rtd->flags |= RTDF_MUTATED;
	}

skip_array:

	return 0;
err_out:
	fprintf(stderr, A_RED "%s: error" A_RESET "\n", __func__);
	return -1;
}

/* true if pure */
static inline void
reflection_type_data_purity_dep(struct reflection_type_data *rtd,
				struct reflection_type_data *rtd_dep,
				bool dep_recursive)
{
	assert(rtd);
	assert(rtd_dep);

	/* already tainted */
	if ((rtd->flags & RTDF_PURITY_MASK) == RTDF_IMPURE)
		return;

	/* recursives are impure */
	if (dep_recursive || (rtd_dep->flags & RTDF_PURITY_MASK) != 0)
		rtd->flags |= RTDF_IMPURE;
}

int
reflection_type_data_specialize(struct reflection_type_data *rtd,
				struct reflection_setup_type_ctx *rstc)
{
	struct reflection_type_system *rts;
	struct reflection_field_data *rfd, *rfd_flatten, *rfd_key, *rfd_selector, *rfdt;
	struct reflection_type_data *rtd_parent = NULL, *rtd_resolved_typedef = NULL;
	struct reflection_type_data *rtd_resolved_ptr = NULL;
	struct reflection_field_data *rfd_parent = NULL;
	enum fy_type_kind resolved_type_kind;
	const struct reflection_type_ops *ops;
	const char *flatten_field, *key_field, *selector;
	bool flat_anonymous;
	struct fy_document *fyd;
	struct reflection_any_value *rav_select;
	struct reflection_meta *rm;
	int i, rc;

	if (!rtd || !rstc)
		return -1;

	if (rtd->flags & (RTDF_SPECIALIZED | RTDF_SPECIALIZING))
		return 0;

	rts = rtd->rts;
	assert(rts);

	rtd->flags |= RTDF_SPECIALIZING;

	// fprintf(stderr, A_RED "%s: %s" A_RESET "\n", __func__, rtd->ti->name);

	rtd_resolved_typedef = reflection_type_data_resolved_typedef(rtd->rtd_dep);
	resolved_type_kind = rtd_resolved_typedef ? rtd_resolved_typedef->ti->kind : FYTK_INVALID;

	rtd_parent = reflection_setup_type_ctx_top_type(rstc);

	rfd_parent = reflection_setup_type_ctx_top_field(rstc);
	if (!rfd_parent)
		rfd_parent = rstc->rr->rfd_root;
	assert(rfd_parent);

	switch (rtd->ti->kind) {
	case FYTK_ENUM:
		// fprintf(stderr, A_RED "%s enum %s" A_RESET "\n", __func__, rtd->ti->name);
		break;

	case FYTK_PTR:
		// fprintf(stderr, A_RED "%s ptr %s" A_RESET "\n", __func__, rtd->ti->name);
		switch (resolved_type_kind) {
		case FYTK_CHAR:

			/* do we inhibit this conversion? */
			if (reflection_meta_get_bool(rtd->meta, rmvid_not_string))
				break;

			/* char * -> text */
			rtd->ops = &ptr_char_ops;
			rtd->flags |= RTDF_MUTATED;
			break;

		case FYTK_VOID:
			/* void * -> doc */
			rtd->ops = &ptr_doc_ops;
			rtd->flags |= RTDF_MUTATED;
			break;

		case FYTK_PTR:

			/* if it's on a field, it's the job of the field */
			if (rstc->rfds.top || !rtd_resolved_typedef)
				break;

			/* entry is some kind of base type, instantiate simple dynarray if so */
			rtd_resolved_ptr = reflection_type_data_resolved_typedef(rtd_resolved_typedef->rtd_dep);
			if (!rtd_resolved_ptr || rtd_resolved_ptr->ti->kind == FYTK_PTR)
				break;

			rtd->ops = &dyn_array_ops;
			rtd->flags |= RTDF_MUTATED;

			break;

		default:
			break;
		}

		break;

	case FYTK_CONSTARRAY:
		/* only for char[] */
		switch (resolved_type_kind) {
		case FYTK_CHAR:
			if (reflection_meta_get_bool(rtd->meta, rmvid_not_string))
				break;

			rtd->ops = &const_array_char_ops;
			rtd->flags |= RTDF_MUTATED;
			break;

		default:
			break;
		}

		break;

	case FYTK_STRUCT:
	case FYTK_UNION:

		/* check if there are anonymous fields */
		for (i = 0; i < rtd->fields_count; i++) {
			rfdt = rtd->fields[i];
			if (rfdt->fi->type_info->flags & FYTIF_ANONYMOUS_RECORD_DECL) {
				rtd->has_anonymous_field = true;
				rtd->first_anonymous_field_idx = i;
				break;
			}
		}
		// fprintf(stderr, ">>>>>>>>>> has_anonymous_field=%s\n", rtd->has_anonymous_field ? "true" : "false");

		rfd_flatten = NULL;
		flatten_field = reflection_meta_get_str(rtd->meta, rmvid_flatten_field);
		if (flatten_field) {
			rfd_flatten = reflection_type_data_lookup_field(rtd, flatten_field);
			RTS_ERROR_CHECK(rfd_flatten,
					"Unable to find flatten_field %s in '%s'", flatten_field,
					rtd->ti->name);
		}

		flat_anonymous = reflection_meta_get_bool(rtd->meta, rmvid_flatten_field_first_anonymous);
		RTS_ERROR_CHECK(!flat_anonymous || rtd->has_anonymous_field,
				"flat_anonymous enabled for %s but no anonymous_fields in '%s'", flatten_field, rtd->ti->name);

		if (!rfd_flatten && flat_anonymous)
			rfd_flatten = rtd->fields[rtd->first_anonymous_field_idx];

		rtd->flat_field_idx = rfd_flatten ? rfd_flatten->idx : -1;

		rfd_key = NULL;
		key_field = reflection_meta_get_str(rtd->meta, rmvid_key);
		if (key_field) {
			rfd_key = reflection_type_data_lookup_field(rtd, key_field);
			RTS_ERROR_CHECK(rfd_key,
					"key field %s not found in '%s'", key_field, rtd->ti->name);
		}

		/* union selector, must exist for unions */
		if (rtd->ti->kind == FYTK_UNION) {

			/* a union must have a selector field in the enclosing struct */
			RTS_ERROR_CHECK(rtd_parent,
					"'%s' must have a parent", rtd->ti->name);

			selector = reflection_meta_get_str(rtd->meta, rmvid_selector);

			if (selector) {
				rfd_selector = reflection_type_data_lookup_field(rtd_parent, selector);
				RTS_ERROR_CHECK(rfd_selector,
						"selector field '%s' not found in '%s'", selector,
						rtd_parent->ti->name);
			} else {
				RTS_ERROR_CHECK(rfd_parent,
						"no parent field for '%s'", rtd->ti->name);

				RTS_ERROR_CHECK(rfd_parent->idx > 0,
						"'%s' cannot be the first field", rtd->ti->name);

				RTS_ASSERT((rfd_parent->idx - 1) >= 0 && (rfd_parent->idx - 1) < rtd_parent->fields_count);

				rfd_selector = rtd_parent->fields[rfd_parent->idx - 1];
			}

			/* single selector */
			RTS_ERROR_CHECK(!rfd_selector->is_selector,
					"single selector field only for %s", selector);

			rfd_selector->is_selector = true;
			rtd->rtd_selector = reflection_type_data_ref(rfd_selector->rtd);

			rtd->selector_field_idx = rfd_selector->idx;

			/* for each field in the union a select must exist, or be implied */
			for (i = 0; i < rtd->fields_count; i++) {

				rfdt = rtd->fields[i];
				rm = rfd_rtd(rfdt)->meta;

				rav_select = reflection_meta_get_any_value(rm, rmvid_select);

				/* if there's no select create one which is the name of the field */
				if (!rav_select) {
					fyd = fy_document_buildf(NULL, "%s", rfdt->field_name);
					RTS_ASSERT(fyd);

					rav_select = reflection_any_value_create(rtd->rts, fy_document_root(fyd));
					fy_document_destroy(fyd);

					rc = reflection_meta_set_any_value(rm, rmvid_select, rav_select, false);
					RTS_ASSERT(!rc);
				}
			}
		}

		break;

	default:
		break;
	}

	/* is this on a field? */
	rtd->flags &= ~RTDF_PURITY_MASK;

	/* pointers are not pure (but may be point to something pure) */
	if (rtd->ti->kind == FYTK_PTR)
		rtd->flags |= RTDF_IMPURE;

	/* if the type has a non default ops and has a dtor is impure */
	if (!reflection_type_data_has_default_ops(rtd)) {
		ops = rtd->ops;
		if (ops && ops->dtor)
			rtd->flags |= RTDF_IMPURE;
	}

	/* pure dependency? */
	if (rtd->rtd_dep)
		reflection_type_data_purity_dep(rtd, rtd->rtd_dep, rtd->rtd_dep_recursive);

	/* pure fields? */
	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];
		reflection_type_data_purity_dep(rtd, rfd_rtd(rfd), rfd->rtd_recursive);
	}

	rtd->flags &= ~RTDF_SPECIALIZING;
	rtd->flags |= RTDF_SPECIALIZED;

	return 0;
err_out:
	fprintf(stderr, A_RED "%s: error" A_RESET "\n", __func__);
	return -1;
}

int
reflection_setup_type(struct reflection_type_system *rts,
		      const struct fy_type_info *ti,
		      struct reflection_setup_type_ctx *rstc,
		      struct reflection_type_data **rtdp,
		      struct reflection_meta *parent_meta)
{
	struct reflection_type_data *rtd = NULL, *rtd_dep;
	struct reflection_field_data *rfd, *rfd_parent;
	int i;
	int rc;

	assert(rts);
	assert(ti);
	assert(rtdp);

	/* guard against overflow */
	RTS_ASSERT(rts->rtd_next_idx + 1 > 0);

	*rtdp = NULL;

	// fprintf(stderr, A_BLUE "%s: %s" A_RESET "\n", __func__, ti->name);

	// fprintf(stderr, "%s: ti->name='%s'\n", __func__, ti->name);

	rtd = malloc(sizeof(*rtd));
	RTS_ASSERT(rtd);
	memset(rtd, 0, sizeof(*rtd));

	rtd->refs = 1;
	rtd->rts = rts;
	rtd->idx = rts->rtd_next_idx++;
	rtd->ti = ti;
	rtd->ops = &reflection_ops_table[ti->kind];

	/* do fields */
	rtd->fields_count = fy_type_kind_has_fields(ti->kind) ? ti->count : 0;

	rtd->yaml_annotation = fy_type_info_get_yaml_annotation(rtd->ti);
	rtd->yaml_annotation_str = fy_type_info_get_yaml_comment(rtd->ti);

	rtd->flat_field_idx = -1;
	rtd->selector_field_idx = -1;
	rtd->union_field_idx = -1;
	rtd->first_anonymous_field_idx = -1;

	rtd->meta = parent_meta ? reflection_meta_copy(parent_meta) : reflection_meta_create(rts);
	RTS_ASSERT(rtd->meta);

	if (rtd->yaml_annotation) {
		rc = reflection_meta_fill(rtd->meta, fy_document_root(rtd->yaml_annotation));
		RTS_ASSERT(!rc);
	}

	if (rtd->fields_count > 0) {
		rtd->fields = malloc(sizeof(*rtd->fields) * rtd->fields_count);
		RTS_ASSERT(rtd->fields);
		memset(rtd->fields, 0, sizeof(*rtd->fields) * rtd->fields_count);
	}

	for (i = 0; i < rtd->fields_count; i++) {
		rfd = reflection_field_data_create(rtd, i);
		RTS_ASSERT(rfd);
		rtd->fields[i] = rfd;
	}

	/* add to the in progress list */
	rc = reflection_type_data_stack_push(&rstc->rtds, rtd);
	RTS_ASSERT(!rc);

	/* if a dependent type causes a loop, then the field that is a parent is recursive */
	if (ti->dependent_type) {
		rtd_dep = reflection_type_data_stack_find_by_type_info(&rstc->rtds, ti->dependent_type);
		if (rtd_dep) {

			rtd->rtd_dep_recursive = true;
			rtd->rtd_dep = rtd_dep;

			/* mark all fields in the stack as recursive */
			for (i = rstc->rfds.top - 1; i >= 0; i--) {
				rfd_parent = rstc->rfds.rfds[i];
				assert(rfd_parent);
				rfd_parent->rtd_recursive = true;
			}

		} else {
			rc = reflection_setup_type(rts, ti->dependent_type, rstc, &rtd->rtd_dep,
						   rtd->ti->kind == FYTK_TYPEDEF ? rtd->meta : NULL);
			RTS_ASSERT(!rc);
		}
	}

	for (i = 0; i < rtd->fields_count; i++) {

		rfd = rtd->fields[i];

		rc = reflection_field_data_stack_push(&rstc->rfds, rfd);
		RTS_ASSERT(!rc);

		rc = reflection_setup_type(rts, rfd->fi->type_info, rstc, &rfd->rtd, rfd->meta);
		RTS_ASSERT(!rc);

		(void)reflection_field_data_stack_pop(&rstc->rfds);

	}

	/* save so that specialize can work */
	*rtdp = rtd;

	/* specialize */
	if (rtd->rtd_dep && !rtd->rtd_dep_recursive) {
		rc = reflection_type_data_specialize(rtd->rtd_dep, rstc);
		RTS_ASSERT(!rc);

	}

	for (i = 0; i < rtd->fields_count; i++) {
		rfd = rtd->fields[i];

		rc = reflection_field_data_specialize(rfd);
		RTS_ASSERT(!rc);

		if (!rfd->rtd_recursive) {
			rc = reflection_type_data_specialize(rfd->rtd, rstc);
			RTS_ASSERT(!rc);
		}
	}

	/* and remove it from the setup stack */
	(void)reflection_type_data_stack_pop(&rstc->rtds);

	rc = reflection_type_data_specialize(rtd, rstc);
	RTS_ASSERT(!rc);

	// fprintf(stderr, "%s: ti->name='%s' T#%d OK\n", __func__, ti->name, rtd->idx);

	return 0;

err_out:
	reflection_type_data_destroy(rtd);
	*rtdp = NULL;
	return -1;
}

int
reflection_type_data_simplify(struct reflection_type_data *rtd_root)
{
	struct reflection_type_data **rtds = NULL, **rtdsp1, **rtdsp2, *rtd1, *rtd2;
	bool eq;

	if (!rtd_root)
		return 0;

	/* collect, ordered by type_info and then sorted by id */
again:
	if (rtds)
		free(rtds);
	rtds = reflection_type_data_collect(rtd_root, RTDS_DEPTH_ORDER);
	if (!rtds) {
		printf("FAILED to collect type data for print!\n");
		return -1;
	}

	rtdsp1 = rtds;
	while ((rtd1 = *rtdsp1++) != NULL) {

		rtdsp2 = rtdsp1;
		while ((rtd2 = *rtdsp2++) != NULL) {

			/* for equal types, replace with aliases */
			eq = reflection_type_data_equal(rtd1, rtd2);
			if (eq) {
				reflection_type_data_replace_by(rtd_root, rtd1, rtd2);
				goto again;
			}
		}
	}

	free(rtds);

	return 0;
}

void *reflection_malloc(struct reflection_type_system *rts, size_t size)
{
	void *p;

	if (!rts)
		return NULL;

	if (!rts->cfg.ops || !rts->cfg.ops->malloc)
		p = malloc(size);
	else
		p = rts->cfg.ops->malloc(rts, size);

	// fprintf(stderr, "%s: %p\n", __func__, p);
	return p;
}

void *reflection_realloc(struct reflection_type_system *rts, void *ptr, size_t size)
{
	void *p;

	if (!rts)
		return NULL;

	if (!rts->cfg.ops || !rts->cfg.ops->realloc)
		p = realloc(ptr, size);
	else
		p = rts->cfg.ops->realloc(rts, ptr, size);

	// fprintf(stderr, "%s: %p\n", __func__, p);
	return p;
}

void reflection_free(struct reflection_type_system *rts, void *ptr)
{
	if (!rts)
		return;

	// fprintf(stderr, "%s: %p\n", __func__, ptr);

	if (!rts->cfg.ops || !rts->cfg.ops->free)
		free(ptr);
	else
		rts->cfg.ops->free(rts, ptr);
}

struct reflection_reference *
reflection_reference_create(struct reflection_type_system *rts, const char *name, const char *meta);
void reflection_reference_destroy(struct reflection_reference *rr);

struct reflection_reference *
reflection_reference_create(struct reflection_type_system *rts, const char *name, const char *meta)
{
	struct fy_reflection *rfl;
	struct reflection_field_data *rfd = NULL;
	struct fy_document *fyd_meta = NULL;
	const struct fy_type_info *ti_entry;
	struct reflection_setup_type_ctx rstc;
	struct fy_type_info *ti;
	struct fy_field_info *fi;
	struct reflection_reference *rr = NULL;
	int rc;

	if (!rts || !name)
		return NULL;

	rfl = rts->cfg.rfl;
	assert(rfl);

	/* TODO check if reflection reference already present */

	ti_entry = fy_type_info_lookup(rfl, name);
	RTS_ERROR_CHECK(ti_entry, "no type named '%s' in reflection", name);

	if (meta) {
		fyd_meta = fy_flow_document_build_from_string(NULL, meta, FY_NT, NULL);
		RTS_ERROR_CHECK(fyd_meta, "failed to build meta document from '%s'", meta);
	}

	rr = malloc(sizeof(*rr));
	RTS_ASSERT(rr);
	memset(rr, 0, sizeof(*rr));

	rr->rts = rts;
	rr->name = strdup(name);
	RTS_ASSERT(rr->name);

	if (meta) {
		rr->meta = strdup(meta);
		RTS_ASSERT(rr->meta);
	}

	/* create entry field */
	rfd = reflection_field_data_create_internal(rts, NULL, 0);
	RTS_ASSERT(rfd);

	if (fyd_meta) {
		rfd->yaml_annotation = fyd_meta;
		rfd->yaml_annotation_str = rr->meta;
		fyd_meta = NULL;	/* rfd takes over now */

		rc = reflection_meta_fill(rfd->meta, fy_document_root(rfd->yaml_annotation));
		RTS_ASSERT(!rc);
	}

	ti = &rr->rfd_root_ti;
	fi = &rr->rfd_root_fi;

	/* create a pseudo struct enclosing the entry */
	fi->parent = NULL;
	fi->name = "";
	fi->type_info = ti;
	fi->offset = 0;

	ti->kind = FYTK_STRUCT;
	ti->flags = FYTIF_ANONYMOUS;
	ti->name = "";
	ti->size = ti_entry->size;
	ti->align = ti_entry->align;
	ti->count = 1;
	ti->fields = fi;

	rfd->fi = fi;

	rr->rfd_root = rfd;
	rfd = NULL;

	reflection_setup_type_ctx_setup(&rstc, rr);
	rc = reflection_setup_type(rts, ti_entry, &rstc, &rr->rtd_root, rr->rfd_root->meta);
	reflection_setup_type_ctx_cleanup(&rstc);

	RTS_ASSERT(!rc);

	rr->rfd_root->rtd = reflection_type_data_ref(rr->rtd_root);

	rc = reflection_field_data_specialize(rr->rfd_root);
	RTS_ASSERT(!rc);

	rc = reflection_type_data_simplify(rr->rtd_root);
	RTS_ASSERT(!rc);

	return rr;

err_out:
	reflection_reference_destroy(rr);
	fy_document_destroy(fyd_meta);
	reflection_field_data_destroy(rfd);
	return NULL;
}

void
reflection_reference_destroy(struct reflection_reference *rr)
{
	if (!rr)
		return;

	if (rr->rfd_root) {
		fy_document_destroy(rr->rfd_root->yaml_annotation);
		reflection_field_data_destroy(rr->rfd_root);
	}
	if (rr->rtd_root)
		reflection_type_data_unref(rr->rtd_root);
	if (rr->meta)
		free(rr->meta);
	if (rr->name)
		free(rr->name);
	free(rr);
}

struct reflection_type_system *
reflection_type_system_create(const struct reflection_type_system_config *cfg)
{
	struct fy_diag_cfg diag_default_cfg;
	struct reflection_type_system *rts = NULL;

	if (!cfg || !cfg->rfl || !cfg->entry_type)
		goto err_out;

	rts = malloc(sizeof(*rts));
	if (!rts)
		goto err_out;
	memset(rts, 0, sizeof(*rts));

	memcpy(&rts->cfg, cfg, sizeof(rts->cfg));

	if (!rts->cfg.diag) {
		fy_diag_cfg_default(&diag_default_cfg);
		rts->diag = fy_diag_create(&diag_default_cfg);
		if (!rts->diag)
			goto err_out;
	} else
		rts->diag = fy_diag_ref(rts->cfg.diag);

	/* can use RTS_ASSERT, RTS_ERROR_CHECK now */

	if (rts->cfg.flags & RTSCF_DUMP_REFLECTION) {
		fprintf(stderr, "reflection dump at start\n");
		fy_reflection_dump(rts->cfg.rfl, false, true);
		fprintf(stderr, "\n");
	}

	rts->root_ref = reflection_reference_create(rts, rts->cfg.entry_type, rts->cfg.entry_meta);
	RTS_ERROR_CHECK(rts->root_ref, "failed to create root reference for entry='%s' meta='%s'",
			rts->cfg.entry_type, rts->cfg.entry_meta ? : "");

	if (rts->cfg.flags & RTSCF_DUMP_REFLECTION) {
		fprintf(stderr, "%s: type system dump after simplification\n", __func__);
		reflection_type_data_dump(rts->root_ref->rtd_root);
		fprintf(stderr, "\n");
	}

	return rts;
err_out:
	reflection_type_system_destroy(rts);
	return NULL;
}

void reflection_type_system_destroy(struct reflection_type_system *rts)
{
	if (!rts)
		return;

	reflection_reference_destroy(rts->root_ref);

	if (rts->diag)
		fy_diag_unref(rts->diag);

	free(rts);
}

int
reflection_parse(struct fy_parser *fyp, struct reflection_type_data *rtd, void **datap,
		 enum reflection_parse_flags flags)
{
	void *data = NULL;
	int rc;

	if (!fyp || !rtd || !datap)
		return -1;

	*datap = NULL;

	data = reflection_type_data_alloc(rtd);
	RP_ASSERT(data);

	rc = reflection_parse_into(fyp, rtd, data, flags);
	if (rc < 0)
		goto err_out;

	/* no error, just no data */
	if (rc > 0) {
		reflection_type_data_free(rtd, data);
		data = NULL;
	}

	*datap = data;
	return rc;

err_out:
	reflection_type_data_free(rtd, data);
	return -1;
}

/* create a pseudo struct enclosing the entry */
struct reflection_walker *
reflection_root_ctx_setup(struct reflection_root_ctx *rctx,
			  struct reflection_type_data *rtd, void *data, struct reflection_meta *meta)
{
	struct reflection_type_system *rts;
	struct fy_type_info *ti;
	struct fy_field_info *fi;
	struct reflection_type_data *rtd_root;
	struct reflection_field_data *rfd;
	struct reflection_walker *rw_root, *rwr;

	assert(rtd);
	assert(rtd->rts);
	rts = rtd->rts;

	memset(rctx, 0, sizeof(*rctx));

	ti = &rctx->ti_local;
	fi = &rctx->fi_local;
	rtd_root = &rctx->rtd_root_local;
	rfd = &rctx->rfd_local;
	rw_root = &rctx->rw_root_local;
	rwr = &rctx->rwr_local;

	fi->parent = NULL;
	fi->name = "";
	fi->type_info = ti;

	ti->kind = FYTK_STRUCT;
	ti->flags = FYTIF_ANONYMOUS;
	ti->name = "";
	ti->size = rtd->ti->size;
	ti->align = rtd->ti->align;
	ti->count = 1;
	ti->fields = fi;

	rfd->rts = rts;
	rfd->fi = fi;
	rfd->field_name = (char *)fi->name;
	rfd->rtd = rtd;
	rfd->meta = NULL;	/* XXX */
	rctx->field_tab[0] = rfd;

	rtd_root->refs = 0;
	rtd_root->idx = -1;
	rtd_root->rts = rts;
	rtd_root->ti = ti;
	rtd_root->ops = &reflection_ops_table[FYTK_STRUCT];
	rtd_root->flags = RTDF_ROOT;
	rtd_root->flat_field_idx = -1;
	rtd_root->selector_field_idx = -1;
	rtd_root->union_field_idx = -1;
	rtd_root->fields_count = 1;
	rtd_root->fields = rctx->field_tab;
	rtd_root->meta = meta;

	rw_root->flags = RWF_ROOT | RWF_MAP;
	rw_root->data = data;
	rw_root->data_size.bytes = ti->size;
	rw_root->rtd = rtd_root;

	reflection_rw_value(rwr, rtd, data);
	rwr->parent = rw_root;
	rwr->flags |= RWF_FIELD_IDX;

	return rwr;
}

int
reflection_parse_into(struct fy_parser *fyp, struct reflection_type_data *rtd, void *data,
		      enum reflection_parse_flags flags)
{
	struct reflection_root_ctx rctx_local, *rctx = &rctx_local;
	struct reflection_walker *rw;
	struct fy_event *fye = NULL;

	int rc;

	if (!fyp || !rtd || !data)
		return -1;

	/* nothing, return 1 */
	fye = fy_parser_parse(fyp);
	if (!fye || fye->type == FYET_STREAM_END) {
		fy_parser_event_free(fyp, fye);
		return 1;
	}

	/* stream start, consume */
	if (fye->type == FYET_STREAM_START) {
		fy_parser_event_free(fyp, fye);
		fye = fy_parser_parse(fyp);
	}

	RP_INPUT_CHECK(fye != NULL, "premature end of input");

	RP_INPUT_CHECK(fye->type == FYET_DOCUMENT_START,
			"expected FYET_DOCUMENT_START error (got %s)",
			fy_event_type_get_text(fye->type));

	fy_parser_event_free(fyp, fye);
	fye = NULL;

	memset(data, 0, rtd->ti->size);
	rw = reflection_root_ctx_setup(rctx, rtd, data, NULL);
	rc = reflection_parse_rw(fyp, rw, flags);

	if (!rc && fy_parser_get_stream_error(fyp))
		rc = -1;
	if (rc < 0)
		goto out;

	fye = fy_parser_parse(fyp);
	RP_INPUT_CHECK(fye != NULL, "premature end of input");

	RP_INPUT_CHECK(fye->type == FYET_DOCUMENT_END,
			"expected FYET_DOCUMENT_END error (got %s)",
			fy_event_type_get_text(fye->type));

	fy_parser_event_free(fyp, fye);
	fye = NULL;

	rc = 0;
out:
	fy_parser_event_free(fyp, fye);
	return rc;

err_input:
	rc = -EINVAL;
	goto out;
}

int reflection_emit(struct fy_emitter *emit, struct reflection_type_data *rtd, const void *data,
		    enum reflection_emit_flags flags)
{
	struct reflection_root_ctx rctx_local, *rctx = &rctx_local;
	struct reflection_walker *rw;
	bool emit_ss, emit_ds, emit_de, emit_se;
	int rc;

	emit_ss = !!(flags & REF_EMIT_SS);
	emit_ds = !!(flags & REF_EMIT_DS);
	emit_de = !!(flags & REF_EMIT_DE);
	emit_se = !!(flags & REF_EMIT_SE);

	if (emit_ss) {
		rc = fy_emit_eventf(emit, FYET_STREAM_START);
		RE_ASSERT(!rc);
	}

	if (rtd && data) {

		if (emit_ds) {
			rc = fy_emit_eventf(emit, FYET_DOCUMENT_START, 0, NULL, NULL);
			RE_ASSERT(!rc);
		}

		rw = reflection_root_ctx_setup(rctx, rtd, (void *)data, NULL);
		rc = reflection_emit_rw(emit, rw, flags);
		RE_ASSERT(!rc);

		if (emit_de) {
			rc = fy_emit_eventf(emit, FYET_DOCUMENT_END, 0);
			RE_ASSERT(!rc);
		}
	}

	if (emit_se) {
		rc = fy_emit_eventf(emit, FYET_STREAM_END);
		RE_ASSERT(!rc);
	}

	return 0;

err_out:
	return -1;
}

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
		emit_flags = FYOPEF_MODE_YAML_1_2 | FYOPEF_MULTI_DOCUMENT;

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

		/* XXX must parse input */
		emit_flags |= FYOPEF_MODE_AUTO;

		/* we don't support arbitrary modes/styles */
		switch (gcfg->emit_cfg_flags & (FYECF_MODE_MASK << FYECF_MODE_SHIFT)) {
		case FYECF_MODE_ORIGINAL:
		default:
			emit_flags |= FYOPEF_STYLE_DEFAULT;
			break;
		case FYECF_MODE_BLOCK:
			emit_flags |= FYOPEF_STYLE_BLOCK;
			break;
		case FYECF_MODE_FLOW:
			emit_flags |= FYOPEF_STYLE_FLOW;
			break;
		case FYECF_MODE_PRETTY:
			emit_flags |= FYOPEF_STYLE_PRETTY;
			break;
		case FYECF_MODE_FLOW_COMPACT:
			emit_flags |= FYOPEF_STYLE_COMPACT;
			break;
		case FYECF_MODE_FLOW_ONELINE:
			emit_flags |= FYOPEF_STYLE_ONELINE;
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
	/* generic */
	struct generic_config gcfg = default_generic_cfg;
	bool dedup = DEDUP_DEFAULT;
	bool dump_primitives = false;
	bool create_markers = false;
	bool pyyaml_compat = false;
	bool keep_style = false;
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
	struct reflection_type_system_config rts_cfg = {
		.flags = RTSCF_ANNOTATION_MODE_DEFAULT,
	};
	struct reflection_type_system *rts = NULL;
	struct reflection_type_data *rtd_root;
	void *rd_data = NULL;
	bool emitted_ss;


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
	else if (!strcmp(progname, "fy-reflect"))
		tool_mode = OPT_REFLECT;
	else if (!strcmp(progname, "fy-generic"))
		tool_mode = OPT_GENERIC;
	else if (!strcmp(progname, "fy-generic-testsuite"))
		tool_mode = OPT_GENERIC_TESTSUITE;
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
		case OPT_REFLECT:
		case OPT_GENERIC:
		case OPT_GENERIC_TESTSUITE:
		case OPT_GENERIC_PARSE_DUMP:
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
			rts_cfg.flags |= RTSCF_DUMP_REFLECTION | RTSCF_DUMP_TYPE_SYSTEM;
			break;
		case OPT_DEBUG_REFLECTION:
			rts_cfg.flags |= RTSCF_DEBUG;
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
			reflection_prune_system(rfl);

		if (type_include || type_exclude) {
			rc = reflection_type_filter(rfl, type_include, type_exclude);
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

			eq = reflection_equal(rfl, rfl_packed);

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
			else
				fprintf(stderr, "reflections are NOT identical\n");

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

			rts_cfg.rfl = rfl;
			rts_cfg.entry_type = entry_type;
			rts_cfg.entry_meta = entry_meta;
			rts_cfg.diag = diag;
			rts = reflection_type_system_create(&rts_cfg);
			if (!rts) {
				fprintf(stderr, "reflection_type_system_create() failed!\n");
				goto cleanup;
			}

			emitted_ss = false;

			rtd_root = rts->root_ref->rtd_root;

			while ((rc = reflection_parse(fyp, rtd_root, &rd_data, 0)) == 0) {

				rc = reflection_emit(emit, rtd_root, rd_data,
						     REF_EMIT_DS | REF_EMIT_DE | (!emitted_ss ? REF_EMIT_SS : 0));

				reflection_type_data_free(rtd_root, rd_data);
				rd_data = NULL;

				if (rc) {
					fprintf(stderr, "reflection_emit() failed\n");
					goto cleanup;
				}
				emitted_ss = true;
			}

			if (emitted_ss) {
				rc = reflection_emit(emit, NULL, NULL, REF_EMIT_SE);
				if (rc) {
					fprintf(stderr, "reflection_emit() failed for stream end\n");
					goto cleanup;
				}
			}

			if (rc < 0) {
				fprintf(stderr, "reflection_parse() failed\n");
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

	}
	exitcode = EXIT_SUCCESS;

cleanup:
	if (rts)
		reflection_type_system_destroy(rts);

	if (rfl)
		fy_reflection_destroy(rfl);

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
