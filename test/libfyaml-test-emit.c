/*
 * libfyaml-test-emit.c - libfyaml test public emitter interface
 *
 * Copyright (c) 2021 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
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
#include <limits.h>

#include <check.h>

#include <libfyaml.h>

#include "fy-check.h"

struct test_emitter_data {
	struct fy_emitter *emit;
	struct fy_emitter_cfg cfg;
	size_t alloc;
	size_t count;
	char *buf;
};

static int collect_output(struct fy_emitter *emit, enum fy_emitter_write_type type,
			  const char *str, int len, void *userdata)
{
	struct test_emitter_data *data = userdata;
	char *newbuf;
	size_t alloc, need;

	need = data->count + len + 1;
	alloc = data->alloc;
	if (!alloc)
		alloc = 512;	/* start at 512 bytes and double */
	while (need > alloc)
		alloc <<= 1;

	if (alloc > data->alloc) {
		newbuf = realloc(data->buf, alloc);
		if (!newbuf)
			return -1;
		data->buf = newbuf;
		data->alloc = alloc;
	}
	assert(data->alloc >= need);
	memcpy(data->buf + data->count, str, len);
	data->count += len;
	data->buf[data->count] = '\0';

	return len;
}

struct fy_emitter *setup_test_emitter(struct test_emitter_data *data)
{
	memset(data, 0, sizeof(*data));
	data->cfg.output = collect_output;
	data->cfg.userdata = data;
	data->cfg.flags = FYECF_DEFAULT;
	data->emit = fy_emitter_create(&data->cfg);
	return data->emit;

}

static void cleanup_test_emitter(struct test_emitter_data *data)
{
	if (data->emit)
		fy_emitter_destroy(data->emit);
	if (data->buf)
		free(data->buf);
}

START_TEST(emit_simple)
{
	struct test_emitter_data data;
	struct fy_emitter *emit;
	int rc;

	emit = setup_test_emitter(&data);
	ck_assert_ptr_ne(emit, NULL);

	rc = fy_emit_event(emit, fy_emit_event_create(emit, FYET_STREAM_START));
	ck_assert_int_eq(rc, 0);

	rc = fy_emit_event(emit, fy_emit_event_create(emit, FYET_DOCUMENT_START, true, NULL, NULL));
	ck_assert_int_eq(rc, 0);

	rc = fy_emit_event(emit, fy_emit_event_create(emit, FYET_SCALAR, FYSS_PLAIN, "simple", FY_NT, NULL, NULL));
	ck_assert_int_eq(rc, 0);

	rc = fy_emit_event(emit, fy_emit_event_create(emit, FYET_DOCUMENT_END, true, NULL, NULL));
	ck_assert_int_eq(rc, 0);

	rc = fy_emit_event(emit, fy_emit_event_create(emit, FYET_STREAM_END));
	ck_assert_int_eq(rc, 0);

	ck_assert_ptr_ne(data.buf, NULL);

	/* the contents must be 'simple' followed by a trailing newline */
	ck_assert_str_eq(data.buf, "simple\n");

	cleanup_test_emitter(&data);
}
END_TEST

START_TEST(emit_interstitial_comment_single)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;

	fyd = fy_document_build_from_string(&cfg,
		"zebra: z\n# above apple\napple: a\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "# above apple"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_interstitial_comment_multiple)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;

	fyd = fy_document_build_from_string(&cfg,
		"a: 1\n# before b\nb: 2\n# before c\nc: 3\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "# before b"), NULL);
	ck_assert_ptr_ne(strstr(output, "# before c"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_interstitial_and_inline_comment)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;

	fyd = fy_document_build_from_string(&cfg,
		"zebra: z # inline\n# above apple\napple: a\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "# above apple"), NULL);
	ck_assert_ptr_ne(strstr(output, "# inline"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_interstitial_comment_nested)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;

	fyd = fy_document_build_from_string(&cfg,
		"outer:\n  a: 1\n  # before b\n  b: 2\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "# before b"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_interstitial_comment_first_key)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;
	const char *first, *second;

	fyd = fy_document_build_from_string(&cfg,
		"# before first\nfirst: 1\nsecond: 2\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);

	/* comment should appear exactly once */
	first = strstr(output, "# before first");
	ck_assert_ptr_ne(first, NULL);
	second = strstr(first + 1, "# before first");
	ck_assert_ptr_eq(second, NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_interstitial_comment_multiline)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;

	fyd = fy_document_build_from_string(&cfg,
		"a: 1\n# line one\n# line two\nb: 2\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "# line one"), NULL);
	ck_assert_ptr_ne(strstr(output, "# line two"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_interstitial_comment_with_sort)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	struct fy_node *root;
	char *output;
	int rc;

	fyd = fy_document_build_from_string(&cfg,
		"zebra: z\n# above apple\napple: a\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	root = fy_document_root(fyd);
	ck_assert_ptr_ne(root, NULL);

	rc = fy_node_mapping_sort(root, NULL, NULL);
	ck_assert_int_eq(rc, 0);

	output = fy_emit_document_to_string(fyd, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "# above apple"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_interstitial_comment_flow)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;

	fyd = fy_document_build_from_string(&cfg,
		"zebra: z\n# above apple\napple: a\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* top comments are line-oriented and cannot be represented in flow style;
	 * they should be silently dropped rather than producing invalid output */
	output = fy_emit_document_to_string(fyd,
		FYECF_OUTPUT_COMMENTS | FYECF_MODE_FLOW_ONELINE);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_eq(strstr(output, "# above apple"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_comment_no_duplicate_mapping_in_seq)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;
	const char *first, *second;

	fyd = fy_document_build_from_string(&cfg,
		"- name: zebra\n  val: z\n# above apple entry\n- name: apple\n  val: a\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);

	/* comment must appear exactly once */
	first = strstr(output, "# above apple entry");
	ck_assert_ptr_ne(first, NULL);
	second = strstr(first + 1, "# above apple entry");
	ck_assert_ptr_eq(second, NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_comment_no_duplicate_seq_in_mapping)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;
	const char *first, *second;

	fyd = fy_document_build_from_string(&cfg,
		"key1: val1\n# above list\nkey2:\n  - a\n  - b\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);

	/* comment must appear exactly once */
	first = strstr(output, "# above list");
	ck_assert_ptr_ne(first, NULL);
	second = strstr(first + 1, "# above list");
	ck_assert_ptr_eq(second, NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_indented_seq_in_map)
{
	struct fy_parse_cfg pcfg = { .flags = 0 };
	struct fy_document *fyd;
	struct fy_emitter_xcfg xcfg;
	struct fy_emitter *emit;
	struct test_emitter_data data;
	int rc;

	fyd = fy_document_build_from_string(&pcfg,
		"key:\n- a\n- b\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	memset(&data, 0, sizeof(data));
	memset(&xcfg, 0, sizeof(xcfg));
	xcfg.cfg.output = collect_output;
	xcfg.cfg.userdata = &data;
	xcfg.cfg.flags = FYECF_DEFAULT | FYECF_EXTENDED_CFG;
	xcfg.xflags = FYEXCF_INDENTED_SEQ_IN_MAP;

	emit = fy_emitter_create(&xcfg.cfg);
	ck_assert_ptr_ne(emit, NULL);

	rc = fy_emit_document(emit, fyd);
	ck_assert_int_eq(rc, 0);

	fy_emitter_destroy(emit);
	ck_assert_ptr_ne(data.buf, NULL);
	ck_assert_ptr_ne(strstr(data.buf, "key:\n  - a\n  - b"), NULL);

	free(data.buf);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_indented_seq_in_map_default)
{
	struct fy_parse_cfg pcfg = { .flags = 0 };
	struct fy_document *fyd;
	char *output;

	fyd = fy_document_build_from_string(&pcfg,
		"key:\n- a\n- b\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_DEFAULT);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "key:\n- a\n- b"), NULL);
	/* must NOT have the indented form */
	ck_assert_ptr_eq(strstr(output, "key:\n  - a"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_right_comment_on_flow_sequence_value)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;

	fyd = fy_document_build_from_string(&cfg,
		"colors: [red, green] # primary\ncount: 3\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd,
		FYECF_MODE_ORIGINAL | FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "# primary"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_right_comment_on_flow_mapping_value)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;

	fyd = fy_document_build_from_string(&cfg,
		"settings: {verbose: true} # defaults\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd,
		FYECF_MODE_ORIGINAL | FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "# defaults"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_nested_mapping_top_comment)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;

	fyd = fy_document_build_from_string(&cfg,
		"jobs:\n  build:\n    # comment before runs-on\n    runs-on: ubuntu-latest\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "# comment before runs-on"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_nested_sequence_top_comment)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;

	fyd = fy_document_build_from_string(&cfg,
		"parent:\n  # comment before first item\n  - item1\n  - item2\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "# comment before first item"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_deeply_nested_top_comment)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;

	fyd = fy_document_build_from_string(&cfg,
		"a:\n  b:\n    c:\n      # deep comment\n      d: value\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "# deep comment"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_root_top_comment_still_works)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;

	fyd = fy_document_build_from_string(&cfg,
		"# root comment\nkey: value\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "# root comment"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_block_scalar_clip_chomp_preserved)
{
	struct fy_document *fyd;
	char *output;

	/* literal block with default (clip) chomping: | */
	fyd = fy_document_build_from_string(NULL,
		"key: |\n  hello\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_DEFAULT);
	ck_assert_ptr_ne(output, NULL);
	/* must contain "|\n" (clip) not "|+\n" (keep) */
	ck_assert_ptr_ne(strstr(output, "|\n"), NULL);
	ck_assert_ptr_eq(strstr(output, "|+"), NULL);
	ck_assert_ptr_eq(strstr(output, "|-"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_block_scalar_strip_chomp_preserved)
{
	struct fy_document *fyd;
	char *output;

	/* literal block with strip chomping: |- */
	fyd = fy_document_build_from_string(NULL,
		"key: |-\n  hello\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_DEFAULT);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "|-"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_block_scalar_keep_chomp_preserved)
{
	struct fy_document *fyd;
	char *output;

	/* literal block with keep chomping: |+ */
	fyd = fy_document_build_from_string(NULL,
		"key: |+\n  hello\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_DEFAULT);
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "|+"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_comment_preserves_original_indentation)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	char *output;

	/* comment between sequence items at column 2; sequence indent is 0 */
	fyd = fy_document_build_from_string(&cfg,
		"- a: b\n  # indented comment\n- c: d\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	output = fy_emit_document_to_string(fyd, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);
	/* the 2-space indent before # must be preserved */
	ck_assert_ptr_ne(strstr(output, "  # indented comment"), NULL);
	/* but it must NOT appear at column 0 */
	ck_assert_ptr_eq(strstr(output, "\n# indented comment"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

/* Helper: emit document via extended config with PRESERVE_FLOW_LAYOUT.
 * Caller must free() the returned buffer. */
static char *emit_document_preserve_flow(const char *input)
{
	struct fy_parse_cfg pcfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	struct fy_emitter_xcfg xcfg;
	struct fy_emitter *emit;
	struct test_emitter_data data;
	int rc;

	fyd = fy_document_build_from_string(&pcfg, input, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	memset(&data, 0, sizeof(data));
	memset(&xcfg, 0, sizeof(xcfg));
	xcfg.cfg.output = collect_output;
	xcfg.cfg.userdata = &data;
	xcfg.cfg.flags = FYECF_MODE_ORIGINAL | FYECF_OUTPUT_COMMENTS |
			 FYECF_WIDTH_INF | FYECF_EXTENDED_CFG;
	xcfg.xflags = FYEXCF_PRESERVE_FLOW_LAYOUT;

	emit = fy_emitter_create(&xcfg.cfg);
	ck_assert_ptr_ne(emit, NULL);

	rc = fy_emit_document(emit, fyd);
	ck_assert_int_eq(rc, 0);

	fy_emitter_destroy(emit);
	fy_document_destroy(fyd);

	return data.buf;
}

START_TEST(emit_original_flow_sequence_oneline)
{
	char *output;

	output = emit_document_preserve_flow(
		"on: [push, pull_request]\n");
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "[push, pull_request]"), NULL);

	free(output);
}
END_TEST

START_TEST(emit_original_flow_sequence_with_comment)
{
	char *output;

	/* Test that a flow sequence stays oneline even when a sibling key
	 * has a comment; the inline comment on a scalar is preserved */
	output = emit_document_preserve_flow(
		"on: [push, pull_request]\nname: ci # the name\n");
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "[push, pull_request]"), NULL);
	ck_assert_ptr_ne(strstr(output, "# the name"), NULL);

	free(output);
}
END_TEST

START_TEST(emit_original_flow_mapping_oneline)
{
	char *output;

	output = emit_document_preserve_flow(
		"env: {FOO: bar, BAZ: qux}\n");
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "{FOO: bar, BAZ: qux}"), NULL);

	free(output);
}
END_TEST

START_TEST(emit_original_empty_flow)
{
	char *output;

	output = emit_document_preserve_flow(
		"empty_seq: []\nempty_map: {}\n");
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "[]"), NULL);
	ck_assert_ptr_ne(strstr(output, "{}"), NULL);

	free(output);
}
END_TEST

START_TEST(emit_original_nested_flow)
{
	char *output;

	output = emit_document_preserve_flow(
		"matrix: [[1, 2], [3, 4]]\n");
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "[[1, 2], [3, 4]]"), NULL);

	free(output);
}
END_TEST

START_TEST(emit_original_multiline_flow_stays_multiline)
{
	char *output;

	/* A flow sequence that spans two lines in the source */
	output = emit_document_preserve_flow(
		"items: [alpha,\n  beta]\n");
	ck_assert_ptr_ne(output, NULL);

	/* It should NOT be collapsed to a single line */
	ck_assert_ptr_eq(strstr(output, "[alpha, beta]"), NULL);

	free(output);
}
END_TEST

/* Helper: streaming parse→emit round-trip with PRESERVE_FLOW_LAYOUT.
 * Caller must free() the returned buffer. */
static char *streaming_roundtrip(const char *input)
{
	struct fy_parse_cfg pcfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_emitter_xcfg xcfg;
	struct fy_parser *fyp;
	struct test_emitter_data data;
	struct fy_event *fye;
	int rc;

	memset(&data, 0, sizeof(data));
	memset(&xcfg, 0, sizeof(xcfg));
	xcfg.cfg.output = collect_output;
	xcfg.cfg.userdata = &data;
	xcfg.cfg.flags = FYECF_MODE_ORIGINAL | FYECF_OUTPUT_COMMENTS |
			 FYECF_WIDTH_INF | FYECF_EXTENDED_CFG;
	xcfg.xflags = FYEXCF_PRESERVE_FLOW_LAYOUT;

	data.emit = fy_emitter_create(&xcfg.cfg);
	ck_assert_ptr_ne(data.emit, NULL);

	fyp = fy_parser_create(&pcfg);
	ck_assert_ptr_ne(fyp, NULL);

	rc = fy_parser_set_string(fyp, input, FY_NT);
	ck_assert_int_eq(rc, 0);

	while ((fye = fy_parser_parse(fyp)) != NULL) {
		rc = fy_emit_event_from_parser(data.emit, fyp, fye);
		ck_assert_int_eq(rc, 0);
	}

	fy_parser_destroy(fyp);
	fy_emitter_destroy(data.emit);
	data.emit = NULL;

	return data.buf;
}

START_TEST(emit_streaming_oneline_flow_sequence)
{
	char *output;

	output = streaming_roundtrip(
		"colors: [red, green]\ncount: 3\n");
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "[red, green]"), NULL);

	free(output);
}
END_TEST

START_TEST(emit_streaming_oneline_flow_mapping)
{
	char *output;

	output = streaming_roundtrip(
		"settings: {verbose: true}\ncount: 3\n");
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "{verbose: true}"), NULL);

	free(output);
}
END_TEST

START_TEST(emit_streaming_multiline_flow_stays_multiline)
{
	char *output;

	output = streaming_roundtrip(
		"items: [alpha,\n  beta]\n");
	ck_assert_ptr_ne(output, NULL);
	/* Should NOT be collapsed to a single line */
	ck_assert_ptr_eq(strstr(output, "[alpha, beta]"), NULL);

	free(output);
}
END_TEST

START_TEST(emit_streaming_nested_flow_oneline)
{
	char *output;

	output = streaming_roundtrip(
		"x: [[1, 2], [3, 4]]\n");
	ck_assert_ptr_ne(output, NULL);
	ck_assert_ptr_ne(strstr(output, "[[1, 2], [3, 4]]"), NULL);

	free(output);
}
END_TEST

START_TEST(emit_subtree_comment_indent)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_PARSE_COMMENTS };
	struct fy_document *fyd;
	struct fy_node *root, *inner;
	char *output;

	/* Parse: comment at col 2 inside nested mapping */
	fyd = fy_document_build_from_string(&cfg,
		"outer:\n  a: 1\n  # before b\n  b: 2\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Emit just the inner mapping (the value of "outer") */
	root = fy_document_root(fyd);
	ck_assert_ptr_ne(root, NULL);
	inner = fy_node_by_path(root, "/outer", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(inner, NULL);

	output = fy_emit_node_to_string(inner, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);

	/* Comment was at col 2 in source, but now the subtree is emitted
	 * at root level — comment should be at col 0 (same as keys) */
	ck_assert_ptr_ne(strstr(output, "# before b"), NULL);
	ck_assert_ptr_eq(strstr(output, "  # before b"), NULL);  /* NOT indented */

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(emit_constructed_comment_indent)
{
	struct fy_document *fyd;
	struct fy_node *root, *outer_key, *inner_map, *inner_key, *inner_val;
	struct fy_token *fyt;
	char *output;
	int rc;

	/* Build a nested mapping programmatically */
	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	root = fy_node_create_mapping(fyd);
	outer_key = fy_node_create_scalar(fyd, "outer", FY_NT);
	inner_map = fy_node_create_mapping(fyd);
	inner_key = fy_node_create_scalar(fyd, "key", FY_NT);
	inner_val = fy_node_create_scalar(fyd, "value", FY_NT);

	rc = fy_node_mapping_append(inner_map, inner_key, inner_val);
	ck_assert_int_eq(rc, 0);
	rc = fy_node_mapping_append(root, outer_key, inner_map);
	ck_assert_int_eq(rc, 0);
	fy_document_set_root(fyd, root);

	/* Attach constructed comment to inner key */
	fyt = fy_node_get_scalar_token(inner_key);
	ck_assert_ptr_ne(fyt, NULL);
	rc = fy_token_set_comment(fyt, fycp_top, "constructed comment", FY_NT);
	ck_assert_int_eq(rc, 0);

	output = fy_emit_document_to_string(fyd, FYECF_OUTPUT_COMMENTS);
	ck_assert_ptr_ne(output, NULL);

	/* Comment should be at scope indent (col 2), not col 0 */
	ck_assert_ptr_ne(strstr(output, "  # constructed comment"), NULL);

	free(output);
	fy_document_destroy(fyd);
}
END_TEST

void libfyaml_case_emit(struct fy_check_suite *cs)
{
	struct fy_check_testcase *ctc;

	ctc = fy_check_suite_add_test_case(cs, "emit");

	fy_check_testcase_add_test(ctc, emit_simple);
	fy_check_testcase_add_test(ctc, emit_interstitial_comment_single);
	fy_check_testcase_add_test(ctc, emit_interstitial_comment_multiple);
	fy_check_testcase_add_test(ctc, emit_interstitial_and_inline_comment);
	fy_check_testcase_add_test(ctc, emit_interstitial_comment_nested);
	fy_check_testcase_add_test(ctc, emit_interstitial_comment_first_key);
	fy_check_testcase_add_test(ctc, emit_interstitial_comment_multiline);
	fy_check_testcase_add_test(ctc, emit_interstitial_comment_with_sort);
	fy_check_testcase_add_test(ctc, emit_interstitial_comment_flow);
	fy_check_testcase_add_test(ctc, emit_comment_no_duplicate_mapping_in_seq);
	fy_check_testcase_add_test(ctc, emit_comment_no_duplicate_seq_in_mapping);
	fy_check_testcase_add_test(ctc, emit_indented_seq_in_map);
	fy_check_testcase_add_test(ctc, emit_indented_seq_in_map_default);
	fy_check_testcase_add_test(ctc, emit_right_comment_on_flow_sequence_value);
	fy_check_testcase_add_test(ctc, emit_right_comment_on_flow_mapping_value);
	fy_check_testcase_add_test(ctc, emit_nested_mapping_top_comment);
	fy_check_testcase_add_test(ctc, emit_nested_sequence_top_comment);
	fy_check_testcase_add_test(ctc, emit_deeply_nested_top_comment);
	fy_check_testcase_add_test(ctc, emit_root_top_comment_still_works);
	fy_check_testcase_add_test(ctc, emit_block_scalar_clip_chomp_preserved);
	fy_check_testcase_add_test(ctc, emit_block_scalar_strip_chomp_preserved);
	fy_check_testcase_add_test(ctc, emit_block_scalar_keep_chomp_preserved);
	fy_check_testcase_add_test(ctc, emit_comment_preserves_original_indentation);
	fy_check_testcase_add_test(ctc, emit_original_flow_sequence_oneline);
	fy_check_testcase_add_test(ctc, emit_original_flow_sequence_with_comment);
	fy_check_testcase_add_test(ctc, emit_original_flow_mapping_oneline);
	fy_check_testcase_add_test(ctc, emit_original_empty_flow);
	fy_check_testcase_add_test(ctc, emit_original_nested_flow);
	fy_check_testcase_add_test(ctc, emit_original_multiline_flow_stays_multiline);
	fy_check_testcase_add_test(ctc, emit_streaming_oneline_flow_sequence);
	fy_check_testcase_add_test(ctc, emit_streaming_oneline_flow_mapping);
	fy_check_testcase_add_test(ctc, emit_streaming_multiline_flow_stays_multiline);
	fy_check_testcase_add_test(ctc, emit_streaming_nested_flow_oneline);
	fy_check_testcase_add_test(ctc, emit_subtree_comment_indent);
	fy_check_testcase_add_test(ctc, emit_constructed_comment_indent);
}
