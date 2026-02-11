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
	*(char *)(data->buf + data->count) = '\0';
	data->count++;

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

	/* the contents must be 'simple' (without a newline) */
	ck_assert_str_eq(data.buf, "simple");

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

TCase *libfyaml_case_emit(void)
{
	TCase *tc;

	tc = tcase_create("emit");

	tcase_add_test(tc, emit_simple);
	tcase_add_test(tc, emit_interstitial_comment_single);
	tcase_add_test(tc, emit_interstitial_comment_multiple);
	tcase_add_test(tc, emit_interstitial_and_inline_comment);
	tcase_add_test(tc, emit_interstitial_comment_nested);
	tcase_add_test(tc, emit_interstitial_comment_first_key);
	tcase_add_test(tc, emit_interstitial_comment_multiline);
	tcase_add_test(tc, emit_interstitial_comment_with_sort);
	tcase_add_test(tc, emit_interstitial_comment_flow);

	return tc;
}
