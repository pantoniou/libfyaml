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
#define RECREATING_DEFAULT      false
#define JSON_DEFAULT			"auto"
#define DISABLE_ACCEL_DEFAULT		false
#define DISABLE_BUFFERING_DEFAULT	false
#define DISABLE_DEPTH_LIMIT_DEFAULT	false
#define SLOPPY_FLOW_INDENTATION_DEFAULT	false
#define PREFER_RECURSIVE_DEFAULT	false
#define YPATH_ALIASES_DEFAULT		false
#define DISABLE_FLOW_MARKERS_DEFAULT	false
#define DUMP_PATH_DEFAULT		false
#define DOCUMENT_EVENT_STREAM_DEFAULT	false
#define COLLECT_ERRORS_DEFAULT		false
#define ALLOW_DUPLICATE_KEYS_DEFAULT	false
#define STRIP_EMPTY_KV_DEFAULT		false
#define TSV_FORMAT_DEFAULT		false

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
#define OPT_CFLAGS			2022
#define OPT_TYPE_DUMP			2023
#define OPT_IMPORT_BLOB			2024
#define OPT_GENERATE_BLOB		2025
#define OPT_PRUNE_SYSTEM		2026
#define OPT_TYPE_INCLUDE		2027
#define OPT_TYPE_EXCLUDE		2028
#define OPT_IMPORT_C_FILE		2029
#define OPT_ENTRY_TYPE			2030

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
	{"dump-pathexpr",	no_argument,		0,	OPT_DUMP_PATHEXPR },
	{"document-event-stream",no_argument,		0,	OPT_DOCUMENT_EVENT_STREAM },
	{"noexec",		no_argument,		0,	OPT_NOEXEC },
	{"null-output",		no_argument,		0,	OPT_NULL_OUTPUT },
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
	{"type-dump",           no_argument,	        0,      OPT_TYPE_DUMP },
	{"entry-type",		required_argument,	0,	OPT_ENTRY_TYPE },
	{"cflags",              required_argument,      0,      OPT_CFLAGS },
	{"generate-blob",       required_argument,      0,      OPT_GENERATE_BLOB },
	{"import-blob",         required_argument,      0,      OPT_IMPORT_BLOB },
	{"import-c-file",	required_argument,	0,	OPT_IMPORT_C_FILE },
	{"prune-system",	no_argument,		0,	OPT_PRUNE_SYSTEM },
	{"type-include",        required_argument,      0,      OPT_TYPE_INCLUDE },
	{"type-exclude",        required_argument,      0,      OPT_TYPE_EXCLUDE },

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
	fprintf(fp, "\t--show-diag <x>         : Show diag option <x>\n");
	fprintf(fp, "\t--hide-diag <x>         : Hide diag optione <x>\n");

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
		fprintf(fp, "\t--mode, -m <mode>        : Output mode can be one of original, block, flow, flow-oneline, json, json-tp, json-oneline, dejson, pretty|yamlfmt"
							" (default %s)\n",
							MODE_DEFAULT);
		fprintf(fp, "\t--disable-flow-markers   : Disable testsuite's flow-markers"
							" (default %s)\n",
							DISABLE_FLOW_MARKERS_DEFAULT ? "true" : "false");
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

	if (tool_mode == OPT_REFLECT) {
		fprintf(fp, "\t--type-dump              : Dump types from the reflection\n");
		fprintf(fp, "\t--generate-blob <blob>   : Generate packed blob from C source files\n");
		fprintf(fp, "\t--import-blob <blob>     : Import a packed blob as a reflection source\n");
		fprintf(fp, "\t--import-c-file <file>   : Import a C file as a reflection source\n");
		fprintf(fp, "\t--cflags <cflags>        : The C flags to use for the import\n");
		fprintf(fp, "\t--entry-type <type>      : The C type that is the entry point (i.e. the document)\n");
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
		fprintf(fp, "\tParse and dump generated YAML document from the input string\n");
		fprintf(fp, "\t$ %s --compose -mjson \">foo: bar\"\n", progname);
		fprintf(fp, "\t{\n\t  \"foo\": \"bar\"\n\t}\n");
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

/* ANSI colors and escapes */
#define A_RESET			"\x1b[0m"
#define A_BLACK			"\x1b[30m"
#define A_RED			"\x1b[31m"
#define A_GREEN			"\x1b[32m"
#define A_YELLOW		"\x1b[33m"
#define A_BLUE			"\x1b[34m"
#define A_MAGENTA		"\x1b[35m"
#define A_CYAN			"\x1b[36m"
#define A_LIGHT_GRAY		"\x1b[37m"	/* dark white is gray */
#define A_GRAY			"\x1b[1;30m"
#define A_BRIGHT_RED		"\x1b[1;31m"
#define A_BRIGHT_GREEN		"\x1b[1;32m"
#define A_BRIGHT_YELLOW		"\x1b[1;33m"
#define A_BRIGHT_BLUE		"\x1b[1;34m"
#define A_BRIGHT_MAGENTA	"\x1b[1;35m"
#define A_BRIGHT_CYAN		"\x1b[1;36m"
#define A_WHITE			"\x1b[1;37m"

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

void dump_testsuite_event(struct fy_parser *fyp,
			  struct fy_event *fye, bool colorize,
			  struct fy_token_iter *iter,
			  bool disable_flow_markers, bool tsv_format)
{
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
		/* no flow markers for TSV */
		disable_flow_markers = true;
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
		assert(0);
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
		if (!fy_document_event_is_implicit(fye))
			printf("%c---", separator);
		break;
	case FYET_DOCUMENT_END:
		if (!fy_document_event_is_implicit(fye))
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
		style = fy_token_scalar_style(fye->scalar.value);
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
		fprintf(stderr, "%s: %c%c%c%c%c %3d - %-32s\n",
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

static void comment_dump(int level, const char *comment)
{
	size_t len;
	const char *s, *e, *le;

	if (!comment)
		return;

	len = strlen(comment);
	s = comment;
	e = s + len;
	while (s < e) {
		le = strchr(s, '\n');
		len = le ? (size_t)(le - s) : strlen(s);
		printf("%*s// %.*s\n", (int)(level * 4), "", (int)len, s);
		s += len + 1;
	}
}

static void type_info_dump(const struct fy_type_info *ti, int level)
{
	const struct fy_field_info *fi;
	size_t i;

	comment_dump(level, fy_type_info_get_comment(ti));
	printf("%s size=%zu align=%zu", ti->fullname, ti->size, ti->align);
	if (ti->dependent_type)
		printf(" -> %s", ti->dependent_type->fullname);
	printf("\n");

	if (fy_type_kind_has_fields(ti->kind)) {
		for (i = 0, fi = ti->fields; i < ti->count; i++, fi++) {
			comment_dump(level + 1, fy_field_info_get_comment(fi));
			printf("%*s%s %s", (level + 1) * 4, "", fi->type_info->fullname, fi->name);
			if (!(fi->flags & FYFIF_BITFIELD))
				printf(" offset=%zu", fi->offset);
			else
				printf(" bit_offset=%zu bit_width=%zu", fi->bit_offset, fi->bit_width);
			printf("\n");
		}
	}
}

void reflection_type_info_dump(struct fy_reflection *rfl)
{
	const struct fy_type_info *ti;
	void *prev = NULL;

	while ((ti = fy_type_info_iterate(rfl, &prev)) != NULL) {
		type_info_dump(ti, 0);
	}
}

static void type_info_c_with_fields_dump(const struct fy_type_info *ti, int level, const char *field_name, bool no_first_pad)
{
	const struct fy_type_kind_info *tki;
	const struct fy_field_info *fi;
	char *name;
	size_t i, e_offset;;

	if (!ti || !fy_type_kind_has_fields(ti->kind))
		return;

	tki = fy_type_kind_info_get(ti->kind);
	assert(tki);

	if (!no_first_pad) {
		comment_dump(level, fy_type_info_get_comment(ti));
		printf("%*s", level * 4, "");
	}
	printf("%s", tki->name);
	if (!(ti->flags & FYTIF_ANONYMOUS))
		printf(" %s", ti->name);
	printf(" {");
	printf("\t/* ");
	if (ti->flags & FYTIF_ANONYMOUS) {
		e_offset = fy_type_info_eponymous_offset(ti);
		printf("offset=%zu, ", e_offset);
	} else {
		e_offset = 0;
	}
	printf("size=%zu, align=%zu */", ti->size, ti->align);
	printf("\n");
	for (i = 0, fi = ti->fields; i < ti->count; i++, fi++) {
		comment_dump(level+1, fy_field_info_get_comment(fi));
		if (!(fi->type_info->flags & FYTIF_ANONYMOUS)) {

			printf("%*s", (level + 1) * 4, "");
			if (ti->kind == FYTK_ENUM) {
				printf("%s", fi->name);
				if (fi->flags & FYFIF_ENUM_UNSIGNED)
					printf(" = %llu", fi->uval);
				else
					printf(" = %lld", fi->sval);
				printf(",\n");
			} else {
				name = fy_type_info_generate_name(fi->type_info, fi->name, false);
				assert(name);
				if (!(fi->flags & FYFIF_BITFIELD)) {
					printf("%s;", name);
					printf("\t/* offset=%zu, size=%zu */", e_offset + fi->offset, fi->type_info->size);
				} else {
					printf("%s ", name);
					printf(": %zu;", fi->bit_width);
					printf("\t/* bit_offset=%zu, byte_offset=%zu, byte_bit_offset=%zu */",
							(e_offset * 8 + fi->bit_offset),
							(e_offset * 8 + fi->bit_offset) / 8,
							fi->bit_offset % 8);
				}
				free(name);
				printf("\n");
			}
		} else {
			type_info_c_with_fields_dump(fi->type_info, level + 1, fi->name, false);
		}
	}

	printf("%*s", level * 4, "");
	if (!field_name || !field_name[0])
		printf("}");
	else
		printf("} %s", field_name);

	printf(";");

	if (ti->flags & FYTIF_ANONYMOUS) {
		printf("\t/* anonymous */");
	}
	printf("\n");
}

static void type_info_c_typedef_dump(const struct fy_type_info *ti, int level)
{
	char *name;

	if (!ti || ti->kind != FYTK_TYPEDEF)
		return;

	comment_dump(level, fy_type_info_get_comment(ti));
	printf("%*stypedef ", level * 4, "");

	assert(ti->dependent_type);
	if (!(ti->dependent_type->flags & FYTIF_ANONYMOUS)) {
		name = fy_type_info_generate_name(ti->dependent_type, ti->name, false);
		assert(name);
		printf("%s;", name);
		free(name);
		printf("\t/* size=%zu, align=%zu */", ti->size, ti->align);
		printf("\n");
	} else {
		type_info_c_with_fields_dump(ti->dependent_type, level, ti->name, true);
	}
}

void reflection_type_info_c_dump(struct fy_reflection *rfl)
{
	const struct fy_type_info *ti;
	void *prev = NULL;

	prev = NULL;
	while ((ti = fy_type_info_iterate(rfl, &prev)) != NULL) {
		if (ti->flags & FYTIF_ANONYMOUS)
			continue;
		if (ti->kind == FYTK_TYPEDEF) {
			type_info_c_typedef_dump(ti, 0);
		} else if (fy_type_kind_has_fields(ti->kind)) {
			type_info_c_with_fields_dump(ti, 0, NULL, false);
		}
	}
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
			ret = regexec(&type_include_reg, ti->fullname, 0, NULL, 0);
			include_match = ret == 0;
		} else
			include_match = true;

		if (type_exclude) {
			ret = regexec(&type_include_reg, ti->fullname, 0, NULL, 0);
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

const struct fy_type_info *
reflection_lookup_type_by_name(struct fy_reflection *rfl, const char *name)
{
	const struct fy_type_info *ti;
	void *prev = NULL;
	char *nname;

	/* INVALID means that it's already having a prefix */
	nname = fy_type_name_normalize(FYTK_INVALID, name);
	if (!nname)
		return NULL;

	prev = NULL;
	while ((ti = fy_type_info_iterate(rfl, &prev)) != NULL) {
		if (!strcmp(ti->normalized_name, nname))
			break;
	}
	free(nname);
	return ti;
}

const struct fy_type_info *
reflection_type_resolve_type(const struct fy_type_info *ti)
{
	if (!ti)
		return NULL;
	while (ti && ti->kind == FYTK_TYPEDEF)
		ti = ti->dependent_type;
	return ti;
}

struct reflection_type_data;
struct reflection_field_data;
struct reflection_decoder;
struct reflection_object;
struct reflection_encoder;

struct reflection_object_ops {
	int (*setup)(struct reflection_object *ro, struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path);
	void (*cleanup)(struct reflection_object *ro);
	int (*finish)(struct reflection_object *ro);
	struct reflection_object *(*create_child)(struct reflection_object *ro, struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path);
	int (*scalar_child)(struct reflection_object *ro, struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path);
};

struct reflection_object {
	// struct reflection_decoder *rd;
	struct reflection_object *parent;
	const void *parent_addr;
	struct reflection_type_data *rtd;
	const struct reflection_object_ops *ops;
	void *instance_data;
	void *data;
	size_t data_size;
};

struct reflection_field_data {
	struct reflection_type_data *parent_rtd;
	const struct fy_field_info *fi;
};

struct reflection_type_ops {
	const struct reflection_object_ops *(*object_ops)(struct reflection_type_data *rtd);
	int (*emit)(struct reflection_type_data *rtd, struct fy_emitter *fye, const void *data, size_t data_size);
};

struct reflection_type_data {
	const struct fy_type_info *ti;
	const struct reflection_type_ops *ops;
	void *data;
	size_t fields_count;
	struct reflection_field_data fields[];
};

struct reflection_decoder {
	bool null_output;
	bool document_ready;
	bool verbose;
	bool single_document;

	/* bindable */
	struct reflection_type_data *entry;
	void *data;
	size_t data_size;
	bool data_allocated;
};

struct reflection_type_data *
reflection_type_data_get_dependent(struct reflection_type_data *rtd)
{
	if (!rtd || !rtd->ti || !rtd->ti->dependent_type)
		return NULL;

	return fy_type_info_get_userdata(rtd->ti->dependent_type);
}

struct reflection_field_data *
reflection_type_data_lookup_field(struct reflection_type_data *rtd, const char *field)
{
	const struct fy_field_info *fi;
	int idx;

	if (!rtd || !field)
		return NULL;

	fi = fy_type_info_lookup_field(rtd->ti, field);
	assert(fi);
	if (!fi)
		return NULL;

	idx = fy_field_info_index(fi);
	assert(idx >= 0);
	if (idx < 0)
		return NULL;

	assert((unsigned int)idx < rtd->fields_count);
	return &rtd->fields[idx];
}

struct reflection_field_data *
reflection_type_data_lookup_field_by_enum_value(struct reflection_type_data *rtd, long long val)
{
	int idx;

	if (!rtd)
		return NULL;

	idx = fy_field_info_index(fy_type_info_lookup_field_by_enum_value(rtd->ti, val));
	if (idx < 0)
		return NULL;

	assert((unsigned int)idx < rtd->fields_count);
	return &rtd->fields[idx];
}

struct reflection_field_data *
reflection_type_data_lookup_field_by_unsigned_enum_value(struct reflection_type_data *rtd, unsigned long long val)
{
	int idx;

	if (!rtd)
		return NULL;

	idx = fy_field_info_index(fy_type_info_lookup_field_by_unsigned_enum_value(rtd->ti, val));
	if (idx < 0)
		return NULL;

	assert((unsigned int)idx < rtd->fields_count);
	return &rtd->fields[idx];
}

struct reflection_object *
reflection_object_create_from_type(struct reflection_object *ro_parent, const void *parent_addr,
				   struct reflection_type_data *rtd,
				   struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path,
				   void *data, size_t data_size);

union integer_scalar {
	intmax_t sval;
	uintmax_t uval;
};

union float_scalar {
	float f;
	double d;
	long double ld;
};

static int
common_scalar_setup(struct reflection_object *ro, struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path)
{
	struct fy_token *fyt;
	enum fy_scalar_style style;
	enum fy_type_kind type_kind;
	size_t size, align;
	const char *text0;
	char *endptr;

	type_kind = ro->rtd->ti->kind;

	if (fye->type != FYET_SCALAR || !ro->data || !fy_type_kind_is_valid(type_kind)) {
		fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
				"%s:%d internal error", __FILE__, __LINE__);
		goto err_internal;
	}

	size = fy_type_kind_size(type_kind);
	align = fy_type_kind_align(type_kind);

	if (ro->data_size != size || ((size_t)(uintptr_t)ro->data & (align - 1)) != 0) {
		fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
				"%s:%d internal error", __FILE__, __LINE__);
		goto err_internal;
	}

	fyt = fy_event_get_token(fye);
	style = fy_token_scalar_style(fyt);

	text0 = fy_token_get_text0(fyt);
	if (!text0) {
		fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
				"%s:%d unable to get token of the event", __FILE__, __LINE__);
		goto err_nomem;
	}

	if (text0[0] == '\0') {
		fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR, "Invalid empty scalar");
		goto err_inval;
	}

	if (fy_type_kind_is_integer(type_kind)) {

		if (style != FYSS_PLAIN) {
			fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
					"only plain style allowed for integers");
			goto err_inval;
		}

		/* nothing larger than this */
		if (size > sizeof(intmax_t)) {
			fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
					"integer type too large (>sizeof(intmax_t))");
			goto err_inval;
		}

		if (fy_type_kind_is_signed(type_kind)) {
			intmax_t sval;
		       
			errno = 0;
			sval = strtoimax(text0, &endptr, 10);

			if ((sval == INTMAX_MIN || sval == INTMAX_MAX) && errno == ERANGE) {
				fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
						"integer value out of intmax_t range");
				goto err_range;
			}

			if (size < sizeof(intmax_t)) {
				const intmax_t minv = INTMAX_MIN >> ((sizeof(intmax_t) - size) * 8);
				const intmax_t maxv = INTMAX_MAX >> ((sizeof(intmax_t) - size) * 8);

				if (sval < minv || sval > maxv) {
					fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
							"integer value out of range (min=%jd, max=%jd)", minv, maxv);
					goto err_range;
				}
			}

			switch (type_kind) {
			case FYTK_CHAR:
				*(char *)ro->data = sval;
				break;
			case FYTK_SCHAR:
				*(signed char *)ro->data = sval;
				break;
			case FYTK_SHORT:
				*(short *)ro->data = sval;
				break;
			case FYTK_INT:
				*(int *)ro->data = sval;
				break;
			case FYTK_LONG:
				*(long *)ro->data = sval;
				break;
			case FYTK_LONGLONG:
				*(long long *)ro->data = sval;
				break;
			default:
				goto err_inval;
			}
		} else {
			uintmax_t uval;

			uval = strtoumax(text0, &endptr, 10);
			if (uval == UINTMAX_MAX && errno == ERANGE) {
				fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
						"integer value out of uintmax_t range");
				goto err_range;
			}

			if (size < sizeof(uintmax_t)) {
				const uintmax_t maxv = UINTMAX_MAX >> ((sizeof(uintmax_t) - size) * 8);
				if (uval > maxv) {
					fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
							"integer value out of range (max=%ju)", maxv);
					goto err_range;
				}
			}

			switch (type_kind) {
			case FYTK_CHAR:
				*(char *)ro->data = uval;
				break;
			case FYTK_UCHAR:
				*(unsigned char *)ro->data = uval;
				break;
			case FYTK_USHORT:
				*(unsigned short *)ro->data = uval;
				break;
			case FYTK_UINT:
				*(unsigned int *)ro->data = uval;
				break;
			case FYTK_ULONG:
				*(unsigned long *)ro->data = uval;
				break;
			case FYTK_ULONGLONG:
				*(unsigned long long *)ro->data = uval;
				break;
			default:
				goto err_inval;
			}
		}

		if (*endptr != '\0') {
			fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
					"invalid integer format");
			goto err_inval;
		}

	} else if (fy_type_kind_is_float(type_kind)) {

		union float_scalar u;

		if (style != FYSS_PLAIN) {
			fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
					"only plain style allowed for doubles");
			goto err_inval;
		}

		/* nothing larger than this */
		if (size > sizeof(long double)) {
			fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
					"double type too large (>sizeof(long double))");
			goto err_inval;
		}

		/* just do in sequence */
		switch (type_kind) {
		case FYTK_FLOAT:
			u.f = strtof(text0, &endptr);
			if ((u.f <= FLT_MIN || u.f >= FLT_MAX) && errno == ERANGE) {
				fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
						"float value out of range");
				goto err_range;
			}
			*(float *)ro->data = u.f;
			break;
		case FYTK_DOUBLE:
			u.d = strtod(text0, &endptr);
			if ((u.d <= DBL_MIN || u.d >= DBL_MAX) && errno == ERANGE) {
				fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
						"double value out of range");
				goto err_range;
			}
			*(double *)ro->data = u.d;
			break;
		case FYTK_LONGDOUBLE:
			u.ld = strtold(text0, &endptr);
			if ((u.ld <= LDBL_MIN || u.ld >= LDBL_MAX) && errno == ERANGE) {
				fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
						"long double value out of range");
				goto err_out;
			}
			*(long double *)ro->data = u.ld;
			break;

		default:
			goto err_inval;
		}

		if (*endptr != '\0')
			goto err_inval;

	} else if (type_kind == FYTK_BOOL) {
		_Bool v;

		if (style != FYSS_PLAIN) {
			fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
					"only plain style allowed for booleans");
			goto err_inval;
		}

		v = false;
		if (!strcmp(text0, "true")) {
			v = true;
		} else if (!strcmp(text0, "false")) {
			v = false;
		} else {
			fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
					"invalid boolean");
			goto err_inval;
		}

		*(_Bool *)ro->data = v;

	} else {
		fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
				"unsupported kind %s", fy_type_kind_name(type_kind));
		goto err_inval;
	}

	return 0;

err_out:
	return -1;

err_inval:
	errno = EINVAL;
	return -1;

err_nomem:
	errno = ENOMEM;
	return -1;

err_range:
	errno = ERANGE;
	goto err_out;

err_internal:
	errno = EFAULT;
	goto err_out;
}

static int integer_scalar_emit(struct fy_emitter *fye, enum fy_type_kind type_kind, union integer_scalar num)
{
	bool is_signed;
	char buf[3 * sizeof(uintmax_t) + 3];	/* maximum buffer space */
	char *s, *e;
	bool neg;
	size_t len;
	uintmax_t val;

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

	return fy_emit_event(fye, fy_emit_event_create(fye, FYET_SCALAR, FYSS_PLAIN, s, len, NULL, NULL));
}

static int float_scalar_emit(struct fy_emitter *fye, enum fy_type_kind type_kind, union float_scalar num)
{
	char buf_local[80], *buf;
	int len;

	switch (type_kind) {
	case FYTK_FLOAT:
		num.ld = (long double)num.f;
		break;
	case FYTK_DOUBLE:
		num.ld = (long double)num.d;
		break;
	case FYTK_LONGDOUBLE:
		break;
	default:
		errno = EINVAL;
		return -1;
	}

	buf = buf_local;
	len = snprintf(buf_local, sizeof(buf_local), "%Lf", num.ld);
	if (len >= (int)sizeof(buf_local)) {
		len = snprintf(NULL, 0, "%Lf", num.ld);
		buf = alloca(len + 1);
		len = snprintf(buf, len, "%Lf", num.ld);
	}

	return fy_emit_event(fye, fy_emit_event_create(fye, FYET_SCALAR, FYSS_PLAIN, buf, len, NULL, NULL));
}

static int bool_scalar_emit(struct fy_emitter *fye, enum fy_type_kind type_kind, _Bool v)
{
	const char *bool_txt[2] = { "false", "true" };
	size_t bool_txt_sz[2] = { 5, 4 };
	const char *str;
	size_t len;

	str = bool_txt[!!v];
	len = bool_txt_sz[!!v];
	return fy_emit_event(fye, fy_emit_event_create(fye, FYET_SCALAR, FYSS_PLAIN, str, len, NULL, NULL));
}

static int common_scalar_emit(struct reflection_type_data *rtd, struct fy_emitter *fye, const void *data, size_t data_size)
{
	enum fy_type_kind type_kind;
	size_t size, align;
	int ret;

	type_kind = rtd->ti->kind;

	size = fy_type_kind_size(type_kind);
	align = fy_type_kind_align(type_kind);

	if (data_size != size || ((size_t)(uintptr_t)data & (align - 1)) != 0) {
		errno = EINVAL;
		return -1;
	}

	if (fy_type_kind_is_integer(type_kind)) {
		bool is_signed;
		union integer_scalar num;

		/* only allow up to uintmax */
		if (size > sizeof(uintmax_t))
			goto err_inval;

		is_signed = fy_type_kind_is_signed(type_kind);
		if (is_signed) {
			switch (type_kind) {
			case FYTK_CHAR:
				num.sval = *(const char *)data;
				break;
			case FYTK_SCHAR:
				num.sval = *(const signed char *)data;
				break;
			case FYTK_SHORT:
				num.sval = *(const short *)data;
				break;
			case FYTK_INT:
				num.sval = *(const int *)data;
				break;
			case FYTK_LONG:
				num.sval = *(const long *)data;
				break;
			case FYTK_LONGLONG:
				num.sval = *(const long long *)data;
				break;
			default:
				goto err_inval;
			}
		} else {
			switch (type_kind) {
			case FYTK_CHAR:
				num.uval = *(const char *)data;
				break;
			case FYTK_UCHAR:
				num.uval = *(const unsigned char *)data;
				break;
			case FYTK_USHORT:
				num.uval = *(const unsigned short *)data;
				break;
			case FYTK_UINT:
				num.uval = *(const unsigned int *)data;
				break;
			case FYTK_ULONG:
				num.uval = *(const unsigned long *)data;
				break;
			case FYTK_ULONGLONG:
				num.uval = *(const unsigned long long *)data;
				break;
			default:
				goto err_inval;
			}
		}

		ret = integer_scalar_emit(fye, type_kind, num);
		if (ret)
			goto err_out;

	} else if (fy_type_kind_is_float(type_kind)) {

		union float_scalar num;

		/* only allow up to long double */
		if (size > sizeof(long double))
			goto err_inval;

		/* just do in sequence */
		switch (type_kind) {
		case FYTK_FLOAT:
			num.f = *(const float *)data;
			break;
		case FYTK_DOUBLE:
			num.d = *(const double *)data;
			break;
		case FYTK_LONGDOUBLE:
			num.ld = *(const long double *)data;
			break;

		default:
			goto err_inval;
		}

		ret = float_scalar_emit(fye, type_kind, num);
		if (ret)
			goto err_out;

	} else if (type_kind == FYTK_BOOL) {

		ret = bool_scalar_emit(fye, type_kind, *(const _Bool *)data);
		if (ret)
			goto err_out;

	} else
		goto err_inval;

	return 0;
err_out:
	return -1;

err_inval:
	errno = EINVAL;
	return -1;
}

static const struct reflection_object_ops *
common_scalar_object_ops(struct reflection_type_data *rtd)
{
	static const struct reflection_object_ops ops = {
		.setup = common_scalar_setup,
	};
	return &ops;
}

static int const_array_setup(struct reflection_object *ro, struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path)
{
	if (fye->type != FYET_SEQUENCE_START) {
		assert(0);
		return -1;
	}

	assert(ro->data);
	ro->instance_data = (void *)(uintptr_t)-1;	/* last index of const array */

	return 0;
}

static int const_array_finish(struct reflection_object *ro)
{
	int last_idx;

	last_idx = (int)(uintptr_t)ro->instance_data;
	if (last_idx != (int)(ro->rtd->ti->count - 1))	/* verify all filled */
		return -1;

	return 0;
}

static void const_array_cleanup(struct reflection_object *ro)
{
	ro->instance_data = NULL;
}

struct reflection_object *const_array_create_child(struct reflection_object *ro_parent,
						   struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path)
{
	struct reflection_object *ro;
	struct reflection_type_data *rtd_dep;
	size_t item_size;
	int idx;
	void *data;

	assert(fy_path_in_sequence(path));
	idx = fy_path_component_sequence_get_index(fy_path_last_not_collection_root_component(path));
	if (idx < 0)
		return NULL;
	if ((unsigned int)idx >= ro_parent->rtd->ti->count)
		return NULL;

	rtd_dep = reflection_type_data_get_dependent(ro_parent->rtd);
	assert(rtd_dep);

	item_size = rtd_dep->ti->size;
	data = ro_parent->data + item_size * idx;

	ro = reflection_object_create_from_type(ro_parent, (const void *)(uintptr_t)idx,
						rtd_dep,
						fyp, fye, path, data, item_size);
	if (!ro)
		return NULL;

	ro_parent->instance_data = (void *)(uintptr_t)idx;	/* last index of const array */

	return ro;
}

const struct reflection_object_ops *const_array_object_ops(struct reflection_type_data *rtd)
{
	static const struct reflection_object_ops ops = {
		.setup = const_array_setup,
		.cleanup = const_array_cleanup,
		.finish = const_array_finish,
		.create_child = const_array_create_child,
	};

	return &ops;
}

int const_array_emit(struct reflection_type_data *rtd, struct fy_emitter *fye, const void *data, size_t data_size)
{
	struct reflection_type_data *rtd_dep;
	size_t idx;
	int rc;

	rtd_dep = reflection_type_data_get_dependent(rtd);
	assert(rtd_dep);

	rc = fy_emit_event(fye, fy_emit_event_create(fye, FYET_SEQUENCE_START, FYNS_ANY, NULL, NULL));
	if (rc)
		goto err_out;

	for (idx = 0; idx < rtd->ti->count; idx++, data += rtd_dep->ti->size) {
		assert(rtd_dep->ops->emit);
		rc = rtd_dep->ops->emit(rtd_dep, fye, data, rtd_dep->ti->size);
		if (rc)
			goto err_out;
	}

	rc = fy_emit_event(fye, fy_emit_event_create(fye, FYET_SEQUENCE_END));
	if (rc)
		goto err_out;

	return 0;
err_out:
	return -1;
}

struct struct_type_data {
	uint8_t *required_map;
	uint8_t *optional_map;
	uint8_t maps[];
};

struct struct_instance_data {
	size_t present_map_size;
	uint8_t present_map[];
};

static int struct_setup(struct reflection_object *ro, struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path)
{
	struct struct_instance_data *id;
	size_t present_map_size, size;

	if (fye->type != FYET_MAPPING_START) {
		assert(0);
		return -1;
	}

	assert(ro->data);

	present_map_size = (ro->rtd->ti->count + (8 - 1)) / 8;
	size = sizeof(*id) + present_map_size;
	id = malloc(size);
	assert(id);
	memset(id, 0, size);
	id->present_map_size = present_map_size;

	ro->instance_data = id;

	return 0;
}

static void struct_cleanup(struct reflection_object *ro)
{
	struct struct_instance_data *id;

	id = ro->instance_data;
	if (id)
		free(id);
	ro->instance_data = NULL;
}

static int struct_finish(struct reflection_object *ro)
{
	assert(ro->instance_data);

	return 0;
}

struct reflection_object *struct_create_child(struct reflection_object *ro_parent,
					      struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path)
{
	struct reflection_object *ro;
	struct reflection_type_data *rtd;
	struct reflection_field_data *rfd;
	const struct fy_field_info *fi;
	const struct fy_type_info *ti;
	struct fy_token *fyt_key;
	const char *field;

	assert(fy_path_in_mapping(path));
	assert(!fy_path_in_mapping_key(path));

	fyt_key = fy_path_component_mapping_get_scalar_key(fy_path_last_not_collection_root_component(path));
	assert(fyt_key);

	field = fy_token_get_text0(fyt_key);
	assert(field);

	rtd = ro_parent->rtd;
	assert(rtd);
	rfd = reflection_type_data_lookup_field(rtd, field);
	assert(rtd);
	if (!rfd) {
		fprintf(stderr, "reflection_type_data_lookup_field() failed\n");
		return NULL;
	}
	fi = rfd->fi;
	ti = fi->type_info;
	/* no bitfields */
	assert((fi->flags & FYFIF_BITFIELD) == 0);

	ro = reflection_object_create_from_type(ro_parent, fi,
						fy_type_info_get_userdata(ti),
						fyp, fye, path,
						ro_parent->data + rfd->fi->offset, ti->size);
	if (!ro)
		return NULL;

	return ro;
}

const struct reflection_object_ops *struct_object_ops(struct reflection_type_data *rtd)
{
	static const struct reflection_object_ops ops = {
		.setup = struct_setup,
		.cleanup = struct_cleanup,
		.finish = struct_finish,
		.create_child = struct_create_child,
	};

	return &ops;
}

int struct_emit(struct reflection_type_data *rtd, struct fy_emitter *fye, const void *data, size_t data_size)
{
	struct reflection_field_data *rfd;
	struct reflection_type_data *rtd_field;
	const char *field_name;
	size_t i;
	int rc;

	rc = fy_emit_event(fye, fy_emit_event_create(fye, FYET_MAPPING_START, FYNS_ANY, NULL, NULL));
	if (rc)
		goto err_out;

	for (i = 0, rfd = &rtd->fields[0]; i < rtd->fields_count; i++, rfd++) {

		field_name = fy_field_info_get_yaml_name(rfd->fi);
		if (!field_name)
			field_name = rfd->fi->name;

		rc = fy_emit_event(fye, fy_emit_event_create(fye, FYET_SCALAR, FYSS_PLAIN, field_name, FY_NT, NULL, NULL));
		if (rc)
			goto err_out;

		rtd_field = fy_type_info_get_userdata(rfd->fi->type_info);
		assert(rtd_field);

		assert(rtd_field->ops->emit);
		rc = rtd_field->ops->emit(rtd_field, fye, data + rfd->fi->offset, rtd_field->ti->size);
		if (rc)
			goto err_out;
	}

	rc = fy_emit_event(fye, fy_emit_event_create(fye, FYET_MAPPING_END));
	if (rc)
		goto err_out;

	return 0;

err_out:
	return -1;
}

static int enum_setup(struct reflection_object *ro, struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path)
{
	struct reflection_type_data *rtd_dep;
	struct reflection_field_data *rfd;
	size_t size, align;
	const char *text0;
	union { unsigned long long u; signed long long s; } val;


	if (fye->type != FYET_SCALAR)
		return -1;

	assert(ro->rtd->ti->kind == FYTK_ENUM);
	rtd_dep = reflection_type_data_get_dependent(ro->rtd);
	assert(rtd_dep);

	assert(ro->data);

	/* verify alignment */
	size = rtd_dep->ti->size;
	align = rtd_dep->ti->align;
	assert(ro->data_size == size);
	assert(((uintptr_t)ro->data & (align - 1)) == 0);
	(void)size;
	(void)align;

	text0 = fy_token_get_text0(fy_event_get_token(fye));
	assert(text0);

	rfd = reflection_type_data_lookup_field(ro->rtd, text0);
	assert(rfd);

	if (fy_type_kind_is_signed(rfd->fi->type_info->kind)) {
		val.s = rfd->fi->sval;
		// fprintf(stderr, "%lld\n", val.s);
	} else {
		val.u = rfd->fi->uval;
		// fprintf(stderr, "%llu\n", val.u);
	}

	switch (rtd_dep->ti->kind) {
	case FYTK_CHAR:
		if (CHAR_MIN < 0) {
			assert(val.s >= CHAR_MIN && val.s <= CHAR_MAX);
			*(char *)ro->data = (char)val.s;
		} else {
			assert(val.u <= CHAR_MAX);
			*(char *)ro->data = (char)val.u;
		}
		break;
	case FYTK_SCHAR:
		assert(val.s >= SCHAR_MIN && val.s <= SCHAR_MAX);
		*(signed char *)ro->data = (signed char)val.s;
		break;
	case FYTK_UCHAR:
		assert(val.u <= UCHAR_MAX);
		*(unsigned char *)ro->data = (unsigned char)val.u;
		break;
	case FYTK_SHORT:
		assert(val.s >= SHRT_MIN && val.s <= SHRT_MAX);
		*(short *)ro->data = (short)val.s;
		break;
	case FYTK_USHORT:
		assert(val.u <= USHRT_MAX);
		*(unsigned short *)ro->data = (unsigned short)val.u;
		break;
	case FYTK_INT:
		assert(val.s >= INT_MIN && val.s <= INT_MAX);
		*(int *)ro->data = (int)val.s;
		break;
	case FYTK_UINT:
		assert(val.u <= UINT_MAX);
		*(unsigned int *)ro->data = (unsigned int)val.u;
		break;
	case FYTK_LONG:
		assert(val.s >= LONG_MIN && val.s <= LONG_MAX);
		*(long *)ro->data = (long)val.s;
		break;
	case FYTK_ULONG:
		assert(val.u <= ULONG_MAX);
		*(unsigned long *)ro->data = (unsigned long)val.u;
		break;
	case FYTK_LONGLONG:
		*(long long *)ro->data = val.s;
		break;
	case FYTK_ULONGLONG:
		*(unsigned long long *)ro->data = val.u;
		break;

	default:
		assert(0);	/* err, no more */
		abort();
	}

	return 0;
}

const struct reflection_object_ops *enum_object_ops(struct reflection_type_data *rtd)
{
	static const struct reflection_object_ops ops = {
		.setup = enum_setup,
	};

	return &ops;
}

int enum_emit(struct reflection_type_data *rtd, struct fy_emitter *fye, const void *data, size_t data_size)
{
	struct reflection_type_data *rtd_dep;
	struct reflection_field_data *rfd;
	size_t size, align;
	union { unsigned long long u; signed long long s; } val;
	const char *text;
	size_t len;

	assert(rtd->ti->kind == FYTK_ENUM);
	rtd_dep = reflection_type_data_get_dependent(rtd);
	assert(rtd_dep);

	/* verify alignment */
	size = rtd_dep->ti->size;
	align = rtd_dep->ti->align;
	assert(data_size == size);
	assert(((uintptr_t)data & (align - 1)) == 0);
	(void)size;
	(void)align;

	switch (rtd_dep->ti->kind) {
	case FYTK_CHAR:
		if (CHAR_MIN < 0)
			val.s = *(char *)data;
		else
			val.u = *(char *)data;
		break;
	case FYTK_SCHAR:
		val.s = *(signed char *)data;
		break;
	case FYTK_UCHAR:
		val.u = *(unsigned char *)data;
		break;
	case FYTK_SHORT:
		val.s = *(short *)data;
		break;
	case FYTK_USHORT:
		val.u = *(unsigned short *)data;
		break;
	case FYTK_INT:
		val.s = *(int *)data;
		break;
	case FYTK_UINT:
		val.u = *(unsigned int *)data;
		break;
	case FYTK_LONG:
		val.s = *(long *)data;
		break;
	case FYTK_ULONG:
		val.u = *(unsigned long *)data;
		break;
	case FYTK_LONGLONG:
		val.s = *(long long *)data;
		break;
	case FYTK_ULONGLONG:
		val.u = *(unsigned long long *)data;
		break;

	default:
		assert(0);	/* err, no more */
		abort();
	}

	if (fy_type_kind_is_signed(rtd_dep->ti->kind))
		rfd = reflection_type_data_lookup_field_by_enum_value(rtd, val.s);
	else
		rfd = reflection_type_data_lookup_field_by_unsigned_enum_value(rtd, val.u);

	assert(rfd);
	text = rfd->fi->name;
	len = strlen(text);

	return fy_emit_event(fye, fy_emit_event_create(fye, FYET_SCALAR, FYSS_ANY, text, len, NULL, NULL));
}

static int ptr_setup(struct reflection_object *ro, struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path)
{
	struct fy_token *fyt;
	struct reflection_type_data *rtd_dep;
	enum fy_type_kind type_kind;
	size_t size, align;
	const char *text0;
	void *p;

	assert(ro->rtd->ti->kind == FYTK_PTR);

	rtd_dep = reflection_type_data_get_dependent(ro->rtd);
	assert(rtd_dep);
	assert(ro->data);

	type_kind = ro->rtd->ti->kind;
	assert(fy_type_kind_is_valid(type_kind));

	size = fy_type_kind_size(type_kind);
	align = fy_type_kind_align(type_kind);

	fprintf(stderr, "size=%zu align=%zu\n", size, align);

	assert(ro->data_size == size && ((size_t)(uintptr_t)ro->data & (align - 1)) == 0);

	type_kind = rtd_dep->ti->kind;

	fprintf(stderr, "rtd_dep->ti->kind = %s size=%zu\n", fy_type_kind_name(type_kind), size);

	if (type_kind == FYTK_CHAR && fye->type == FYET_SCALAR) {

		fprintf(stderr, "char *\n");

		fyt = fy_event_get_token(fye);
		assert(fyt);

		text0 = fy_token_get_text0(fyt);
		assert(text0);

		p = strdup(text0);
		assert(p);

		*(uintptr_t *)ro->data = (uintptr_t)p;

	} else
		return -1;

	return 0;

#if 0
err_internal:
	errno = EINVAL;
	return -1;
#endif
}

static void ptr_cleanup(struct reflection_object *ro)
{
}

static int ptr_finish(struct reflection_object *ro)
{
	return 0;
}

const struct reflection_object_ops *ptr_object_ops(struct reflection_type_data *rtd)
{
	static const struct reflection_object_ops ops = {
		.setup = ptr_setup,
		.cleanup = ptr_cleanup,
		.finish = ptr_finish,
	};

	return &ops;
}

int ptr_emit(struct reflection_type_data *rtd, struct fy_emitter *fye, const void *data, size_t data_size)
{
	struct reflection_type_data *rtd_dep;
	enum fy_type_kind type_kind;
	size_t size, align;
	const char *text;
	size_t len;

	/* verify alignment */
	type_kind = rtd->ti->kind;
	assert(type_kind == FYTK_PTR);

	size = rtd->ti->size;
	align = rtd->ti->align;

	fprintf(stderr, "size=%zu align=%zu data_size=%zu\n", size, align, data_size);

	assert(rtd->ti->kind == FYTK_PTR);
	rtd_dep = reflection_type_data_get_dependent(rtd);
	assert(rtd_dep);

	type_kind = rtd_dep->ti->kind;

	if (type_kind == FYTK_CHAR) {
		text = *(const char **)data;
		len = strlen(text);
		return fy_emit_event(fye, fy_emit_event_create(fye, FYET_SCALAR, FYSS_ANY, text, len, NULL, NULL));
	}

	return -1;
}

const struct reflection_type_ops reflection_ops_table[FYTK_COUNT] = {
	[FYTK_INVALID] = {
	},
	[FYTK_VOID] = {
	},
	[FYTK_BOOL] = {
		.object_ops = common_scalar_object_ops,
		.emit = common_scalar_emit,
	},
	[FYTK_CHAR] = {
		.object_ops = common_scalar_object_ops,
		.emit = common_scalar_emit,
	},
	[FYTK_SCHAR] = {
		.object_ops = common_scalar_object_ops,
		.emit = common_scalar_emit,
	},
	[FYTK_UCHAR] = {
		.object_ops = common_scalar_object_ops,
		.emit = common_scalar_emit,
	},
	[FYTK_SHORT] = {
		.object_ops = common_scalar_object_ops,
		.emit = common_scalar_emit,
	},
	[FYTK_USHORT] = {
		.object_ops = common_scalar_object_ops,
		.emit = common_scalar_emit,
	},
	[FYTK_INT] = {
		.object_ops = common_scalar_object_ops,
		.emit = common_scalar_emit,
	},
	[FYTK_UINT] = {
		.object_ops = common_scalar_object_ops,
		.emit = common_scalar_emit,
	},
	[FYTK_LONG] = {
		.object_ops = common_scalar_object_ops,
		.emit = common_scalar_emit,
	},
	[FYTK_ULONG] = {
		.object_ops = common_scalar_object_ops,
		.emit = common_scalar_emit,
	},
	[FYTK_LONGLONG] = {
		.object_ops = common_scalar_object_ops,
		.emit = common_scalar_emit,
	},
	[FYTK_ULONGLONG] = {
		.object_ops = common_scalar_object_ops,
		.emit = common_scalar_emit,
	},
#ifdef FY_HAS_INT128
	[FYTK_INT128] = {
	},
	[FYTK_UINT128] = {
	},
#else
	[FYTK_INT128] = {
	},
	[FYTK_UINT128] = {
	},
#endif
	[FYTK_FLOAT] = {
		.object_ops = common_scalar_object_ops,
		.emit = common_scalar_emit,
	},
	[FYTK_DOUBLE] = {
		.object_ops = common_scalar_object_ops,
		.emit = common_scalar_emit,
	},
	[FYTK_LONGDOUBLE] = {
		.object_ops = common_scalar_object_ops,
		.emit = common_scalar_emit,
	},
#ifdef FY_HAS_FP16
	[FYTK_FLOAT16] = {
	},
#else
	[FYTK_FLOAT16] = {
	},
#endif
#ifdef FY_HAS_FLOAT128
	[FYTK_FLOAT128] = {
	},
#else
	[FYTK_FLOAT128] = {
	},
#endif
	/* these are templates */
	[FYTK_RECORD] = {
	},

	[FYTK_STRUCT] = {
		.object_ops = struct_object_ops,
		.emit = struct_emit,
	},
	[FYTK_UNION] = {
	},
	[FYTK_ENUM] = {
		.object_ops = enum_object_ops,
		.emit = enum_emit,
	},
	[FYTK_TYPEDEF] = {
	},
	[FYTK_PTR] = {
		.object_ops = ptr_object_ops,
		.emit = ptr_emit,
	},
	[FYTK_CONSTARRAY] = {
		.object_ops = const_array_object_ops,
		.emit = const_array_emit,
	},
	[FYTK_INCOMPLETEARRAY] = {
	},
	[FYTK_FUNCTION] = {
	},
};

struct reflection_object *
reflection_object_create_internal(struct reflection_object *parent, const void *parent_addr,
				  struct reflection_type_data *rtd,
				  struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path,
				  const struct reflection_object_ops *ops, void *data, size_t data_size);

struct root_instance_data {
	// struct reflection_decoder *rd;
	int dummy;
};

static int root_setup(struct reflection_object *ro, struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path)
{
	struct root_instance_data *id;

	assert(ro);

	id = malloc(sizeof(*id));
	if (!id) {
		assert(0);
		return -1;
	}
	memset(id, 0, sizeof(*id));
	// id->rd = ro->rd;
	ro->instance_data = id;

	return 0;
}

static void root_cleanup(struct reflection_object *ro)
{
	struct root_instance_data *id;

	id = ro->instance_data;

	if (ro->instance_data) {
		id = ro->instance_data;
		ro->instance_data = NULL;
		free(id);
	}
}

struct reflection_object *root_create_child(struct reflection_object *ro_parent,
					    struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path)
{
	struct reflection_object *ro;
	struct reflection_type_data *rtd;

	/* pointer */
	switch (ro_parent->rtd->ti->kind) {
	case FYTK_PTR:
		rtd = reflection_type_data_get_dependent(ro_parent->rtd);
		break;
	case FYTK_INT:
		rtd = ro_parent->rtd;
		break;
	case FYTK_CONSTARRAY:
		rtd = ro_parent->rtd;
		break;
	case FYTK_STRUCT:
	case FYTK_UNION:
		rtd = ro_parent->rtd;
		break;

	case FYTK_ENUM:
		rtd = ro_parent->rtd;
		break;

	default:
		assert(0);
		abort();
		break;
	}

	ro = reflection_object_create_from_type(ro_parent, NULL,
						rtd,
						fyp, fye, path,
						ro_parent->data, ro_parent->data_size);
	if (!ro)
		return NULL;
	return ro;
}

static const struct reflection_object_ops root_ops = {
	.setup = root_setup,
	.cleanup = root_cleanup,
	.create_child = root_create_child,
};

void
reflection_object_destroy(struct reflection_object *ro)
{
	if (!ro)
		return;
	if (ro->ops && ro->ops->cleanup)
		ro->ops->cleanup(ro);
	free(ro);
}

int
reflection_object_finish(struct reflection_object *ro)
{
	if (!ro)
		return 0;
	if (!ro->ops->finish)
		return 0;

	return ro->ops->finish(ro);
}

int
reflection_object_finish_and_destroy(struct reflection_object *ro)
{
	int rc;

	if (!ro)
		return 0;

	rc = reflection_object_finish(ro);
	reflection_object_destroy(ro);
	return rc;
}

struct reflection_object *
reflection_object_create_internal(struct reflection_object *parent, const void *parent_addr,
				  struct reflection_type_data *rtd,
				  struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path,
				  const struct reflection_object_ops *ops,
				  void *data, size_t data_size)
{
	struct reflection_object *ro = NULL;
	int ret;

	if (!fye || !path || !ops) {
		errno = EINVAL;
		return NULL;
	}

	ro = malloc(sizeof(*ro));
	if (!ro)
		return NULL;

	memset(ro, 0, sizeof(*ro));
	ro->rtd = rtd;
	ro->parent = parent;
	ro->parent_addr = parent_addr;
	ro->ops = ops;
	ro->data = data;
	ro->data_size = data_size;
	assert(ro->ops->setup);
	ret = ro->ops->setup(ro, fyp, fye, path);
	if (ret)
		goto err_out;

	return ro;
err_out:
	reflection_object_destroy(ro);
	return NULL;
}

struct reflection_object *
reflection_object_create_from_type(struct reflection_object *ro_parent, const void *parent_addr,
				   struct reflection_type_data *rtd,
				   struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path,
				   void *data, size_t data_size)
{
	struct reflection_object *ro;
	const struct reflection_object_ops *ops;

	if (!ro_parent || !fye || !path) {
		errno = EINVAL;
		return NULL;
	}

	assert(rtd->ops->object_ops);
	ops = rtd->ops->object_ops(rtd);
	assert(ops);

	ro = reflection_object_create_internal(ro_parent, parent_addr,
					       rtd,
					       fyp, fye, path, ops,
					       data, data_size);
	if (!ro)
		return NULL;

	return ro;
}

struct reflection_object *
reflection_object_create_child(struct reflection_object *parent,
			       struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path)
{
	if (!parent || !fye || !path) {
		errno = EINVAL;
		return NULL;
	}

	return parent->ops->create_child(parent, fyp, fye, path);
}

int
reflection_object_scalar_child(struct reflection_object *parent,
			       struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path)
{
	struct reflection_object *ro;
	int ret;

	if (!parent || !fye || !path)
		return -1;

	/* shortcut exists */
	if (parent->ops->scalar_child)
		return parent->ops->scalar_child(parent, fyp, fye, path);

	/* create and destroy cycle */
	assert(parent->ops->create_child);
	ro = parent->ops->create_child(parent, fyp, fye, path);
	if (!ro)
		return -1;

	ret = reflection_object_finish_and_destroy(ro);
	assert(!ret);
	return ret;
}

void reflection_type_data_cleanup(struct reflection_type_data *rtd)
{
	struct reflection_field_data *rfd;
	size_t i;

	if (!rtd)
		return;

	fy_type_info_set_userdata(rtd->ti, NULL);
	for (i = 0, rfd = &rtd->fields[0]; i < rtd->fields_count; i++, rfd++)
		fy_field_info_set_userdata(rfd->fi, NULL);

	free(rtd);
}

int reflection_type_data_setup(const struct fy_type_info *ti)
{
	struct reflection_type_data *rtd = NULL;
	struct reflection_field_data *rfd;
	const struct fy_field_info *fi;
	size_t i;
	size_t size;

	size = sizeof(*rtd);
	if (fy_type_kind_has_fields(ti->kind))
		size += ti->count * sizeof(rtd->fields[0]);

	rtd = malloc(size);
	if (!rtd)
		goto err_out;

	memset(rtd, 0, size);
	rtd->ti = ti;
	rtd->ops = &reflection_ops_table[ti->kind];
	if (fy_type_kind_has_fields(ti->kind)) {
		rtd->fields_count = ti->count;
		for (i = 0, fi = ti->fields, rfd = &rtd->fields[0]; i < ti->count; i++, fi++, rfd++) {
			rfd->parent_rtd = rtd;
			rfd->fi = fi;
			fy_field_info_set_userdata(fi, rfd);
		}
	}

	fy_type_info_set_userdata(ti, rtd);

	return 0;
err_out:
	reflection_type_data_cleanup(rtd);
	return -1;
}

void reflection_cleanup_type_system(struct fy_reflection *rfl)
{
	const struct fy_type_info *ti;
	void *prev = NULL;

	prev = NULL;
	while ((ti = fy_type_info_reverse_iterate(rfl, &prev)) != NULL)
		reflection_type_data_cleanup(fy_type_info_get_userdata(ti));
}

int reflection_setup_type_system(struct fy_reflection *rfl)
{
	const struct fy_type_info *ti;
	void *prev = NULL;
	int ret;

	prev = NULL;
	while ((ti = fy_type_info_iterate(rfl, &prev)) != NULL) {
		ret = reflection_type_data_setup(ti);
		if (ret)
			goto err_out;
	}

	return 0;
err_out:
	reflection_cleanup_type_system(rfl);
	return -1;
}

void
reflection_decoder_destroy(struct reflection_decoder *rd)
{
	if (!rd)
		return;

	if (rd->data && rd->data_allocated)
		free(rd->data);

	free(rd);
}

struct reflection_decoder *
reflection_decoder_create(bool verbose)
{
	struct reflection_decoder *rd = NULL;

	rd = malloc(sizeof(*rd));
	if (!rd)
		goto err_out;

	memset(rd, 0, sizeof(*rd));
	rd->verbose = verbose;

	return rd;

err_out:
	reflection_decoder_destroy(rd);
	return NULL;
}

static enum fy_composer_return
reflection_compose_process_event(struct fy_parser *fyp, struct fy_event *fye, struct fy_path *path, void *userdata)
{
	struct reflection_decoder *rd = userdata;
	struct reflection_object *ro, *rop;
	enum fy_composer_return ret;
	int rc;

	assert(rd);
	if (rd->verbose) {
		fprintf(stderr, "%s: %c%c%c%c%c %3d - %-32s\n",
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

	/* if we're in mapping key wait until we get the whole of the key */
	if (fy_path_in_mapping_key(path))
		return FYCR_OK_CONTINUE;

	switch (fye->type) {
		/* nothing to do for those */
	case FYET_NONE:
		/* cleanup path in case of an error */
		ro = fy_path_get_last_user_data(path);
		if (ro) {
			fy_path_set_last_user_data(path, NULL);
			reflection_object_destroy(ro);
		}


		if (fy_path_in_root(path)) {
			ro = fy_path_get_root_user_data(path);
			assert(ro);
			fy_path_set_root_user_data(path, NULL);
			reflection_object_destroy(ro);
		}

		ret = FYCR_OK_CONTINUE;
		break;

	case FYET_STREAM_START:
	case FYET_STREAM_END:
		ret = FYCR_OK_CONTINUE;
		break;

	case FYET_SCALAR:
		rop = fy_path_get_parent_user_data(path);
		assert(rop);

		rc = reflection_object_scalar_child(rop, fyp, fye, path);
		if (rc) {
			ret = FYCR_ERROR;
			break;
		}
		ret = FYCR_OK_CONTINUE;
		break;

	/* alias not supported yet */
	case FYET_ALIAS:
		ret = FYCR_ERROR;
		break;

	case FYET_DOCUMENT_START:
		ro = reflection_object_create_internal(NULL, NULL,
						       rd->entry,
						       fyp, fye, path, &root_ops,
						       rd->data, rd->data_size);
		if (!ro) {
			ret = FYCR_ERROR;
			break;
		}

		fy_path_set_root_user_data(path, ro);
		ret = FYCR_OK_CONTINUE;
		break;

	case FYET_SEQUENCE_START:
	case FYET_MAPPING_START:
		rop = fy_path_get_parent_user_data(path);
		assert(rop);

		ro = reflection_object_create_child(rop, fyp, fye, path);
		if (!ro) {
			ret = FYCR_ERROR;
			break;
		}

		fy_path_set_last_user_data(path, ro);
		ret = FYCR_OK_CONTINUE;
		break;

	case FYET_DOCUMENT_END:
		ro = fy_path_get_root_user_data(path);
		assert(ro);
		fy_path_set_root_user_data(path, NULL);

		rc = reflection_object_finish_and_destroy(ro);
		if (rc) {
			fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
					"reflection_object_finish_and_destroy() failed");
			ret = FYCR_ERROR;
			break;
		}

		rd->document_ready = true;
		/* on single document mode we stop here */
		if (rd->single_document)
			ret = FYCR_OK_STOP;
		else
			ret = FYCR_OK_CONTINUE;
		break;

	case FYET_SEQUENCE_END:
	case FYET_MAPPING_END:
		ro = fy_path_get_last_user_data(path);
		assert(ro);
		fy_path_set_last_user_data(path, NULL);

		rc = reflection_object_finish_and_destroy(ro);
		if (rc) {
			fy_event_report(fyp, fye, FYEP_VALUE, FYET_ERROR,
					"reflection_object_finish_and_destroy() failed");
			ret = FYCR_ERROR;
			break;
		}

		ret = FYCR_OK_CONTINUE;
		break;

	default:
		assert(0);
		abort();
	}

	return ret;
}

int
reflection_decoder_parse(struct reflection_decoder *rd, struct fy_parser *fyp, const struct fy_type_info *ti, void *data, size_t data_size)
{
	;
	struct reflection_type_data *rtd, *rtd_dep;
	size_t dep_size, type_size;
	int rc;

	if (!rd || !fyp || !ti)
		return -1;

	rtd = fy_type_info_get_userdata(ti);

	/* verify it's a pointer (always) */
	if (rtd->ti->kind == FYTK_PTR) {
		/* get the dependent type (if rtd = "int *" rtd_dep = "int") */
		rtd_dep = reflection_type_data_get_dependent(rtd);
		if (!rtd_dep)
			return -1;

		dep_size = rtd_dep->ti->size;
		if (rtd->ti->kind == FYTK_CONSTARRAY)
			dep_size *= rtd_dep->ti->count;

		type_size = dep_size;
	} else {
		type_size = rtd->ti->size;
	}

	if (data) {
		/* verify size and alignment */
		if (type_size < data_size)
			return -1;
		rd->data = data;
		rd->data_size = data_size;
		rd->data_allocated = false;
	} else {
		rd->data_size = type_size;
		rd->data = malloc(rd->data_size);
		if (!rd->data)
			return -1;
		rd->data_allocated = true;
	}

	/* we're good to go */
	memset(rd->data, 0, rd->data_size);
	rd->entry = rtd;

	/* ignore errors for now */
	rc = fy_parse_compose(fyp, reflection_compose_process_event, rd);
	if (rc)
		return rc;

	if (fy_parser_get_stream_error(fyp))
		return -1;

	return 0;
}

struct reflection_encoder {
	bool emitted_stream_start;
	bool emitted_stream_end;
	bool verbose;
	/* bindable */
	struct fy_emitter *fye;
	struct reflection_type_data *entry;
	const void *data;
	size_t data_size;
};

void
reflection_encoder_destroy(struct reflection_encoder *re)
{
	if (!re)
		return;

	free(re);
}

struct reflection_encoder *
reflection_encoder_create(bool verbose)
{
	struct reflection_encoder *re = NULL;

	re = malloc(sizeof(*re));
	if (!re)
		return NULL;

	memset(re, 0, sizeof(*re));
	re->verbose = verbose;

	return re;
}

int
reflection_encoder_emit(struct reflection_encoder *re, struct fy_emitter *fye, struct reflection_type_data *rtd, const void *data, size_t data_size)
{
	int rc;

	rc = fy_emit_event(fye, fy_emit_event_create(fye, FYET_STREAM_START));
	if (rc)
		goto err_out;

	rc = fy_emit_event(fye, fy_emit_event_create(fye, FYET_DOCUMENT_START, 0, NULL, NULL));
	if (rc)
		goto err_out;

	assert(rtd->ops->emit);
	rc = rtd->ops->emit(rtd, fye, data, data_size);
	if (rc)
		goto err_out;

	rc = fy_emit_event(fye, fy_emit_event_create(fye, FYET_DOCUMENT_END, 0));
	if (rc)
		goto err_out;

	rc = fy_emit_event(fye, fy_emit_event_create(fye, FYET_STREAM_END));
	if (rc)
		goto err_out;

	return 0;
err_out:
	return -1;
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
	struct fy_emitter *fye = NULL;
	int rc, exitcode = EXIT_FAILURE, opt, lidx, i, j, step = 1;
	enum fy_error_module errmod;
	unsigned int errmod_mask;
	bool show;
	int indent = INDENT_DEFAULT;
	int width = WIDTH_DEFAULT;
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
	bool document_event_stream = DOCUMENT_EVENT_STREAM_DEFAULT;
	bool collect_errors = COLLECT_ERRORS_DEFAULT;
	bool allow_duplicate_keys = ALLOW_DUPLICATE_KEYS_DEFAULT;
	bool tsv_format = TSV_FORMAT_DEFAULT;
	struct composer_data cd;
	bool dump_path = DUMP_PATH_DEFAULT;
	const char *input_arg;
	/* b3sum */
	int opti;
	struct b3sum_config b3cfg = default_b3sum_cfg;
	/* reflection */
	struct fy_reflection *rfl = NULL;
	const char *cflags = "";
	const char *import_blob = NULL;
	const char *generate_blob = NULL;
	bool type_dump = false, prune_system = false;
	const char *type_include = NULL, *type_exclude = NULL;
	const char *import_c_file = NULL;
	const char *entry_type = NULL;
	struct reflection_decoder *rd = NULL;
	struct reflection_encoder *re = NULL;
	const struct fy_type_info *ti = NULL;

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
	else if (!strcmp(progname, "fy-reflect"))
		tool_mode = OPT_REFLECT;
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
		case OPT_TYPE_DUMP:
			type_dump = true;
			break;
		case OPT_PRUNE_SYSTEM:
			prune_system = true;
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

		memset(&emit_xcfg, 0, sizeof(emit_xcfg));
		emit_xcfg.cfg.flags = emit_flags |
				FYECF_INDENT(indent) | FYECF_WIDTH(width) |
				FYECF_EXTENDED_CFG;	// use extended config

		/* unconditionally turn on document start markers for ypath */
		if (tool_mode == OPT_YPATH)
			emit_xcfg.cfg.flags |= FYECF_DOC_START_MARK_ON;

		emit_xcfg.xflags = emit_xflags;

		fye = fy_emitter_create(&emit_xcfg.cfg);
		if (!fye) {
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

		if (!document_event_stream) {
			/* regular test suite */
			while ((fyev = fy_parser_parse(fyp)) != NULL) {
				dump_testsuite_event(fyp, fyev, dcfg.colorize, iter,
						     disable_flow_markers, tsv_format);
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
			dump_testsuite_event(fyp, fyev, dcfg.colorize, iter,
					     disable_flow_markers, tsv_format);
			fy_document_iterator_event_free(fydi, fyev);

			/* convert to document and then process the generator event stream it */
			while ((fyd = fy_parse_load_document(fyp)) != NULL) {

				fyev = fy_document_iterator_document_start(fydi, fyd);
				if (!fyev) {
					fprintf(stderr, "failed to create document iterator's document start event\n");
					goto cleanup;
				}
				dump_testsuite_event(fyp, fyev, dcfg.colorize, iter,
						     disable_flow_markers, tsv_format);
				fy_document_iterator_event_free(fydi, fyev);

				while ((fyev = fy_document_iterator_body_next(fydi)) != NULL) {
					dump_testsuite_event(fyp, fyev, dcfg.colorize, iter,
							     disable_flow_markers, tsv_format);
					fy_document_iterator_event_free(fydi, fyev);
				}

				fyev = fy_document_iterator_document_end(fydi);
				if (!fyev) {
					fprintf(stderr, "failed to create document iterator's stream document end\n");
					goto cleanup;
				}
				dump_testsuite_event(fyp, fyev, dcfg.colorize, iter,
						     disable_flow_markers, tsv_format);
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
			dump_testsuite_event(fyp, fyev, dcfg.colorize, iter,
					     disable_flow_markers, tsv_format);
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
						rc = fy_emit_document(fye, fyd);
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
									fyeev = fy_emit_event_create(fye, fyev->type);
									break;
								case FYET_DOCUMENT_START:
									tags = fy_document_state_tag_directives(fyev->document_start.document_state);
									fyeev = fy_emit_event_create(fye, FYET_DOCUMENT_START,
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
									fyeev = fy_emit_event_create(fye, FYET_DOCUMENT_END,
												fyev->document_end.implicit);
									break;
								case FYET_MAPPING_START:
								case FYET_SEQUENCE_START:
									fyeev = fy_emit_event_create(fye, fyev->type,
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
									fyeev = fy_emit_event_create(fye, FYET_SCALAR,
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
									fyeev = fy_emit_event_create(fye, FYET_ALIAS,
												fy_token_get_text0(fy_event_get_token(fyev)));
									break;
								default:
									goto cleanup;
							}
							fy_parser_event_free(fyp, fyev);
							if (fyeev == NULL) {
								goto cleanup;
							}

							rc = fy_emit_event(fye, fyeev);
						}
						else {
							rc = fy_emit_event_from_parser(fye, fyp, fyev);
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

				rc = fy_emit_document_start(fye, fyd, fyn_emit);
				if (rc)
					goto cleanup;

				rc = fy_emit_root_node(fye, fyn_emit);
				if (rc)
					goto cleanup;

				rc = fy_emit_document_end(fye);
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
			fy_emit_document(fye, fyd_pe);

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

					rc = fy_emit_document_start(fye, fyd, fyn_emit);
					if (rc)
						goto cleanup;

					rc = fy_emit_root_node(fye, fyn_emit);
					if (rc)
						goto cleanup;

					rc = fy_emit_document_end(fye);
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
		cd.emit = fye;
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
			rfl = fy_reflection_from_packed_blob_file(import_blob);
			if (!rfl) {
				fprintf(stderr, "unable to get reflection from blob file %s\n", import_blob);
				goto cleanup;
			}

		} else if (import_c_file) {
			file = import_c_file;
			rfl = fy_reflection_from_c_file_with_cflags(file, cflags, true, true);
			if (!rfl) {
				fprintf(stderr, "unable to perform reflection from file %s\n", file);
				goto cleanup;
			}
		} else
			rfl = NULL;

		if (!rfl) {
			fprintf(stderr, "No reflection; provide either --import-blob or --import-c-file option\n");
			goto cleanup;
		}

		if (prune_system)
			reflection_prune_system(rfl);

		if (type_include || type_exclude) {
			rc = reflection_type_filter(rfl, type_include, type_exclude);
			if (rc)
				goto cleanup;
		}

		if (type_dump)
			reflection_type_info_c_dump(rfl);
		else {
			rc = reflection_setup_type_system(rfl);
			if (rc) {
				fprintf(stderr, "reflection_setup_type_system() failed!\n");
				goto cleanup;
			}

			if (!entry_type) {
				fprintf(stderr, "No entry point type; supply an --entry-type\n");
				goto cleanup;
			}

			if (optind >= argc) {
				fprintf(stderr, "missing yaml file to dump\n");
				goto cleanup;
			}

			rd = reflection_decoder_create(dump_path);
			if (!rd) {
				fprintf(stderr, "failed to create the decoder\n");
				goto cleanup;
			}

			re = reflection_encoder_create(dump_path);
			if (!re) {
				fprintf(stderr, "failed to create the encoder\n");
				goto cleanup;
			}

			for (i = optind; i < argc; i++) {
				rc = set_parser_input(fyp, argv[i], false);
				if (rc) {
					fprintf(stderr, "failed to set parser input to '%s' for dump\n", argv[i]);
					goto cleanup;
				}
			}

			fprintf(stderr, "looking up entry_type=%s\n", entry_type);
			ti = reflection_lookup_type_by_name(rfl, entry_type);
			if (!ti) {
				fprintf(stderr, "Unable to lookup type info for entry_type '%s'\n", entry_type);
				goto cleanup;
			}

			rc = reflection_decoder_parse(rd, fyp, ti, NULL, 0);
			if (rc) {
				fprintf(stderr, "unable to parse with the decoder\n");
				goto cleanup;
			}

			rc = reflection_encoder_emit(re, fye, rd->entry, rd->data, rd->data_size);
			if (rc) {
				fprintf(stderr, "unable to emit with the encoder\n");
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


	}
	exitcode = EXIT_SUCCESS;

cleanup:
	if (re)
		reflection_encoder_destroy(re);

	if (rd)
		reflection_decoder_destroy(rd);

	if (rfl) {
		reflection_cleanup_type_system(rfl);
		fy_reflection_destroy(rfl);
	}

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

	if (fye)
		fy_emitter_destroy(fye);

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
