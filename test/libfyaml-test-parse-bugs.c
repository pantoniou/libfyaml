/*
 * libfyaml-test-parse-bugs.c - parser bug regression tests
 *
 * Each test exercises a specific parser defect and asserts the
 * correct behaviour.  All failures here are parser bugs.
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
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

#include <check.h>

#include <libfyaml.h>

#include "fy-check.h"

/* ═══════════════════════════════════════════════════════════════════
 * Bug 14 (PARSER): NEL (U+0085) in block scalar trailing break
 *                  produces spurious null byte
 *
 * In YAML 1.1 mode, U+0085 (NEL) is a line break character.
 * When a block scalar's trailing line break is NEL, the parser
 * normalizes it to \n but appends a spurious \0 null byte.
 * This affects clip (default |) and keep (|+) chomping.
 * Strip (|-) is unaffected because it removes the break entirely.
 *
 * Root cause: NEL is 2 bytes in UTF-8 (\xc2\x85) but normalizes
 * to 1 byte (\n). The parser appears to account for the 2-byte
 * input length, leaving a stale null byte in the output buffer.
 *
 * Reproduces PyYAML spec-09-22 and spec-09-23 test failures.
 * ═══════════════════════════════════════════════════════════════════ */

/*
 * Parse a YAML string in YAML 1.1 mode, extract the nth scalar value
 * (0-indexed). Returns allocated string in *out_val, length in *out_len.
 * Returns 0 on success, -1 on failure.
 */
static int parse_yaml11_get_scalar(const char *input, size_t input_len,
				   int scalar_index,
				   char **out_val, size_t *out_len)
{
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_DEFAULT_VERSION_1_1 |
			 FYPCF_SLOPPY_FLOW_INDENTATION |
			 FYPCF_ALLOW_DUPLICATE_KEYS,
	};
	struct fy_parser *fyp;
	struct fy_event *fye;
	int idx = 0;
	char *val = NULL;
	size_t val_len = 0;
	int rc;

	fyp = fy_parser_create(&cfg);
	if (!fyp)
		return -1;

	rc = fy_parser_set_string(fyp, input, input_len);
	if (rc) {
		fy_parser_destroy(fyp);
		return -1;
	}

	while ((fye = fy_parser_parse(fyp)) != NULL) {
		if (fye->type == FYET_SCALAR) {
			if (idx == scalar_index) {
				const char *text;
				size_t tlen;
				text = fy_token_get_text(fye->scalar.value, &tlen);
				if (text) {
					val = malloc(tlen + 1);
					memcpy(val, text, tlen);
					val[tlen] = '\0';
					val_len = tlen;
				}
			}
			idx++;
		}
		fy_parser_event_free(fyp, fye);
	}

	fy_parser_destroy(fyp);

	if (!val)
		return -1;

	if (out_val)
		*out_val = val;
	else
		free(val);
	if (out_len)
		*out_len = val_len;
	return 0;
}

/* clip chomping (|) with NEL as trailing break: expect "text\n", not "text\n\0" */
START_TEST(parse_bug_nel_clip_chomping)
{
	/* "x: |\n  text<NEL>"  where NEL = U+0085 = \xc2\x85 */
	const char input[] = "x: |\n  text\xc2\x85";
	char *val = NULL;
	size_t val_len = 0;
	int rc;

	rc = parse_yaml11_get_scalar(input, sizeof(input) - 1, 1, &val, &val_len);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(val, NULL);

	fprintf(stderr, "clip+NEL: got %zu bytes, repr=", val_len);
	for (size_t i = 0; i < val_len; i++)
		fprintf(stderr, "\\x%02x", (unsigned char)val[i]);
	fprintf(stderr, "\n");

	/* expected: "text\n" = 5 bytes */
	ck_assert_msg(val_len == 5 && memcmp(val, "text\n", 5) == 0,
		      "clip+NEL: expected \"text\\n\" (5 bytes), got %zu bytes", val_len);
	free(val);
}
END_TEST

/* keep chomping (|+) with NEL as trailing break: expect "text\n", not "text\n\0" */
START_TEST(parse_bug_nel_keep_chomping)
{
	const char input[] = "x: |+\n  text\xc2\x85";
	char *val = NULL;
	size_t val_len = 0;
	int rc;

	rc = parse_yaml11_get_scalar(input, sizeof(input) - 1, 1, &val, &val_len);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(val, NULL);

	fprintf(stderr, "keep+NEL: got %zu bytes, repr=", val_len);
	for (size_t i = 0; i < val_len; i++)
		fprintf(stderr, "\\x%02x", (unsigned char)val[i]);
	fprintf(stderr, "\n");

	/* expected: "text\n" = 5 bytes */
	ck_assert_msg(val_len == 5 && memcmp(val, "text\n", 5) == 0,
		      "keep+NEL: expected \"text\\n\" (5 bytes), got %zu bytes", val_len);
	free(val);
}
END_TEST

/* strip chomping (|-) with NEL should work fine: expect "text" */
START_TEST(parse_bug_nel_strip_chomping_ok)
{
	const char input[] = "x: |-\n  text\xc2\x85";
	char *val = NULL;
	size_t val_len = 0;
	int rc;

	rc = parse_yaml11_get_scalar(input, sizeof(input) - 1, 1, &val, &val_len);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(val, NULL);

	fprintf(stderr, "strip+NEL: got %zu bytes, repr=", val_len);
	for (size_t i = 0; i < val_len; i++)
		fprintf(stderr, "\\x%02x", (unsigned char)val[i]);
	fprintf(stderr, "\n");

	/* expected: "text" = 4 bytes */
	ck_assert_msg(val_len == 4 && memcmp(val, "text", 4) == 0,
		      "strip+NEL: expected \"text\" (4 bytes), got %zu bytes", val_len);
	free(val);
}
END_TEST

/* spec-09-22 full test: strip/clip/keep with mixed NEL/LS/PS line breaks */
START_TEST(parse_bug_nel_spec_09_22)
{
	/* strip: |-\n  text<PS>clip: |\n  text<NEL>keep: |+\n  text<LS> */
	const char input[] =
		"strip: |-\n  text\xe2\x80\xa9"
		"clip: |\n  text\xc2\x85"
		"keep: |+\n  text\xe2\x80\xa8";
	char *val = NULL;
	size_t val_len = 0;
	int rc;

	/* scalar[0]="strip", scalar[1]=strip value, scalar[2]="clip",
	 * scalar[3]=clip value, scalar[4]="keep", scalar[5]=keep value */

	/* clip value (scalar index 3): should be "text\n" */
	rc = parse_yaml11_get_scalar(input, sizeof(input) - 1, 3, &val, &val_len);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(val, NULL);

	fprintf(stderr, "spec-09-22 clip: got %zu bytes, repr=", val_len);
	for (size_t i = 0; i < val_len; i++)
		fprintf(stderr, "\\x%02x", (unsigned char)val[i]);
	fprintf(stderr, "\n");

	ck_assert_msg(val_len == 5 && memcmp(val, "text\n", 5) == 0,
		      "spec-09-22 clip: expected \"text\\n\" (5 bytes), got %zu bytes", val_len);
	free(val);
}
END_TEST

/* ── Bug 15 (PARSER): Invalid UTF-8 and NUL in input stream ────── */

/*
 * Helper: try to parse input; return 0 if parse succeeds (all events
 * consumed without error), -1 if the parser reports an error.
 */
static int try_parse(const char *input, size_t input_len, unsigned int extra_flags)
{
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_DEFAULT_DOC | extra_flags,
	};
	struct fy_parser *fyp;
	struct fy_event *fye;
	int got_error = 0;

	fyp = fy_parser_create(&cfg);
	if (!fyp)
		return -1;

	if (fy_parser_set_string(fyp, input, input_len)) {
		fy_parser_destroy(fyp);
		return -1;
	}

	while ((fye = fy_parser_parse(fyp)) != NULL)
		fy_parser_event_free(fyp, fye);

	/* If the parser had an error, fy_parser_get_stream_error() returns true */
	got_error = fy_parser_get_stream_error(fyp);
	fy_parser_destroy(fyp);

	return got_error ? -1 : 0;
}

/* NUL byte (\x00) embedded in a scalar value */
START_TEST(parse_bug_nul_in_stream)
{
	/* "foo: ba\x00r\n" — NUL byte in the middle of a scalar */
	const char input[] = "foo: ba\x00r\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "NUL byte in stream: parser should reject but accepted");
}
END_TEST

/* NUL byte in a YAML comment */
START_TEST(parse_bug_nul_in_comment)
{
	/* "# comment with \x00 null\nfoo: bar\n" */
	const char input[] = "# comment with \x00 null\nfoo: bar\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "NUL byte in comment: parser should reject but accepted");
}
END_TEST

/* Partial (truncated) UTF-8 sequence: 2-byte lead byte without continuation */
START_TEST(parse_bug_partial_utf8_2byte)
{
	/* "foo: abc\xc3\n" — \xc3 is a 2-byte lead, but next byte is \n not 0x80..0xBF */
	const char input[] = "foo: abc\xc3\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "Partial 2-byte UTF-8: parser should reject but accepted");
}
END_TEST

/* Partial (truncated) UTF-8 sequence: 3-byte lead with only 1 continuation */
START_TEST(parse_bug_partial_utf8_3byte)
{
	/* "foo: abc\xe2\x80\n" — \xe2 expects 2 continuation bytes, got only 1 */
	const char input[] = "foo: abc\xe2\x80\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "Partial 3-byte UTF-8: parser should reject but accepted");
}
END_TEST

/* Partial (truncated) UTF-8 sequence: 4-byte lead with only 2 continuations */
START_TEST(parse_bug_partial_utf8_4byte)
{
	/* "foo: \xf0\x9f\x98\n" — \xf0 expects 3 continuation bytes, got only 2 */
	const char input[] = "foo: \xf0\x9f\x98\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "Partial 4-byte UTF-8: parser should reject but accepted");
}
END_TEST

/* Invalid UTF-8: continuation byte without a lead byte */
START_TEST(parse_bug_invalid_utf8_lone_continuation)
{
	/* "foo: abc\x80xyz\n" — \x80 is a continuation byte, not a valid lead */
	const char input[] = "foo: abc\x80xyz\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "Lone continuation byte: parser should reject but accepted");
}
END_TEST

/* Invalid UTF-8: overlong encoding of '/' (U+002F) as 2 bytes */
START_TEST(parse_bug_invalid_utf8_overlong)
{
	/* "foo: \xc0\xaf\n" — overlong encoding of U+002F '/' */
	const char input[] = "foo: \xc0\xaf\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "Overlong UTF-8: parser should reject but accepted");
}
END_TEST

/* Invalid UTF-8: byte 0xFE is never valid in UTF-8 */
START_TEST(parse_bug_invalid_utf8_fe)
{
	/* "foo: \xfe\n" — 0xFE is not a valid UTF-8 byte */
	const char input[] = "foo: \xfe\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "0xFE byte: parser should reject but accepted");
}
END_TEST

/* Invalid UTF-8: byte 0xFF is never valid in UTF-8 */
START_TEST(parse_bug_invalid_utf8_ff)
{
	/* "foo: \xff\n" — 0xFF is not a valid UTF-8 byte */
	const char input[] = "foo: \xff\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "0xFF byte: parser should reject but accepted");
}
END_TEST

/* Valid UTF-8 should still parse OK (sanity check) */
START_TEST(parse_valid_utf8_ok)
{
	/* "foo: café ☕ 🎉\n" — all valid UTF-8 */
	const char input[] = "foo: caf\xc3\xa9 \xe2\x98\x95 \xf0\x9f\x8e\x89\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc == 0,
		      "Valid UTF-8: parser should accept but rejected");
}
END_TEST

/* Partial UTF-8 at end of stream (no trailing newline) */
START_TEST(parse_bug_partial_utf8_at_eof)
{
	/* "foo: abc\xc3" — truncated 2-byte sequence at end of input */
	const char input[] = "foo: abc\xc3";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "Partial UTF-8 at EOF: parser should reject but accepted");
}
END_TEST

/* Single quote stream of 's */
START_TEST(parse_bug_single_quoted_single_quotes)
{
	/* ''''''' -> "''" */
	const char input[] = "''''''";
	struct fy_document *fyd = NULL;
	struct fy_parse_cfg cfg = {0};
	struct fy_node *root = NULL;
	const char *text;

	cfg.flags = FYPCF_DEFAULT_PARSE;

	fyd = fy_document_build_from_string(&cfg, input, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	root = fy_document_root(fyd);
	ck_assert_ptr_ne(root, NULL);
	ck_assert(fy_node_is_scalar(root));
	text = fy_node_get_scalar0(root);
	ck_assert_ptr_ne(text, NULL);
	ck_assert_str_eq(text, "''");

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(parse_bug_nested_explicit_key)
{
	/* the inner explicit key must not end the outer one */
	static const char * const inputs[] = {
		"? ? b\n  : d\n: 23\n",
		"? - ? b\n    : d\n: 23\n",
		"? ? ? ? a\n      : b\n    : c\n  : e\n: 23\n",
	};
	struct fy_document *fyd;
	struct fy_node *fyn;
	struct fy_node_pair *fynp;
	unsigned int i;

	for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
		fyd = fy_document_build_from_string(NULL, inputs[i], FY_NT);
		ck_assert_ptr_ne(fyd, NULL);

		fyn = fy_document_root(fyd);
		ck_assert(fy_node_is_mapping(fyn));
		ck_assert_int_eq(fy_node_mapping_item_count(fyn), 1);
		fynp = fy_node_mapping_get_by_index(fyn, 0);
		ck_assert_ptr_ne(fynp, NULL);
		ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "23");

		fy_document_destroy(fyd);
	}
}
END_TEST

/* a shorthand tag needs a suffix and a verbatim tag needs a URI */
START_TEST(parse_bug_tag_without_suffix)
{
	static const char * const invalid[] = {
		"!!\n",
		"!! a\n",
		"key: !! value\n",
		"- !!\n",
		"%TAG !e! tag:example.com,2000:\n--- !e! a\n",
		"!<> a\n",
	};
	static const char * const valid[] = {
		"! a\n",
		"!!str a\n",
		"!local a\n",
		"%TAG !e! tag:example.com,2000:\n--- !e!x a\n",
		"!<tag:yaml.org,2002:str> a\n",
	};
	unsigned int i;

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
		ck_assert_msg(try_parse(invalid[i], strlen(invalid[i]), 0) != 0,
			      "accepted invalid input #%u", i);

	for (i = 0; i < sizeof(valid) / sizeof(valid[0]); i++)
		ck_assert_msg(try_parse(valid[i], strlen(valid[i]), 0) == 0,
			      "rejected valid input #%u", i);
}
END_TEST

START_TEST(parse_bug_folded_empty_lines_before_more_indented)
{
	/* the break before empty lines is kept when the next text line is more indented */
	static const struct {
		const char *input;
		const char *expected;
	} cases[] = {
		{ ">\n a\n\n  b\n", "a\n\n b\n" },
		{ ">\n a\n\n\n  b\n", "a\n\n\n b\n" },
		{ ">\n a\n b\n\n  c\n d\n", "a b\n\n c\nd\n" },
		{ ">-\n a\n\n  b\n\n c\n", "a\n\n b\n\nc" },
		{ ">\n a\n\n b\n", "a\nb\n" },
	};
	struct fy_document *fyd;
	unsigned int i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fyd = fy_document_build_from_string(NULL, cases[i].input, FY_NT);
		ck_assert_ptr_ne(fyd, NULL);
		ck_assert_str_eq(fy_node_get_scalar0(fy_document_root(fyd)), cases[i].expected);
		fy_document_destroy(fyd);
	}
}
END_TEST

/* ═══════════════════════════════════════════════════════════════════
 * Bug 16: escaped space (\ ) followed by newline in double-quoted scalar
 *
 * In a double-quoted scalar, `\ ` is an escape sequence for a space
 * character (U+0020).  When it is immediately followed by a line break,
 * the line break is folded to an additional space.  The combined result
 * is therefore two spaces, optionally followed by the content of the
 * next line.
 *
 * The bug was that the parser dropped the folded-newline space, producing
 * only a single space instead of two.
 * ═══════════════════════════════════════════════════════════════════ */

START_TEST(parse_bug_escaped_space_before_newline_only)
{
	/* "\ \n" (backslash-space-newline-closequote) -> two spaces */
	const char input[] = "\"\\ \n\"";
	struct fy_document *fyd = NULL;
	struct fy_parse_cfg cfg = {0};
	struct fy_node *root = NULL;
	const char *text;

	cfg.flags = FYPCF_DEFAULT_PARSE;

	fyd = fy_document_build_from_string(&cfg, input, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	root = fy_document_root(fyd);
	ck_assert_ptr_ne(root, NULL);
	ck_assert(fy_node_is_scalar(root));
	text = fy_node_get_scalar0(root);
	ck_assert_ptr_ne(text, NULL);
	ck_assert_str_eq(text, "  ");

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(parse_bug_escaped_space_before_newline_with_content)
{
	/* "\ \na" (backslash-space-newline-a-closequote) -> two spaces + a */
	const char input[] = "\"\\ \na\"";
	struct fy_document *fyd = NULL;
	struct fy_parse_cfg cfg = {0};
	struct fy_node *root = NULL;
	const char *text;

	cfg.flags = FYPCF_DEFAULT_PARSE;

	fyd = fy_document_build_from_string(&cfg, input, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	root = fy_document_root(fyd);
	ck_assert_ptr_ne(root, NULL);
	ck_assert(fy_node_is_scalar(root));
	text = fy_node_get_scalar0(root);
	ck_assert_ptr_ne(text, NULL);
	ck_assert_str_eq(text, "  a");

	fy_document_destroy(fyd);
}
END_TEST

struct diag_text {
	char buf[1024];
	size_t len;
};

static void diag_text_output(struct fy_diag *diag, void *user, const char *buf, size_t len)
{
	struct diag_text *dt = user;

	(void)diag;
	if (len > sizeof(dt->buf) - 1 - dt->len)
		len = sizeof(dt->buf) - 1 - dt->len;
	memcpy(dt->buf + dt->len, buf, len);
	dt->len += len;
	dt->buf[dt->len] = '\0';
}

/* The line of an error shows each bad byte as U+FFFD, and all of the line. */
START_TEST(parse_bug_invalid_utf8_context_line)
{
	static const struct {
		const char *yaml;
		const char *line;
	} cases[] = {
		{ "a: b\x89" "c\n", "\na: b\xef\xbf\xbd" "c\n" },
		{ "a: \"b\x89" "c\"\n", "\na: \"b\xef\xbf\xbd" "c\"\n" },
		{ "# b\x89" "c\na: 1\n", "\n# b\xef\xbf\xbd" "c\n" },
		{ "a: |\n  b\x89" "c\n", "\n  b\xef\xbf\xbd" "c\n" },
		/* a surrogate in UTF-8 is three bad bytes */
		{ "a: \xed\xa0\x80 x\n", "\na: \xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd x\n" },
		{ "\x80\n", "\xef\xbf\xbd\n" },
	};
	struct fy_diag_cfg dcfg;
	struct fy_parse_cfg cfg;
	struct diag_text dt;
	struct fy_document *fyd;
	struct fy_diag *diag;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		memset(&dt, 0, sizeof(dt));
		fy_diag_cfg_default(&dcfg);
		dcfg.fp = NULL;
		dcfg.output_fn = diag_text_output;
		dcfg.user = &dt;
		dcfg.colorize = false;
		diag = fy_diag_create(&dcfg);
		ck_assert_ptr_ne(diag, NULL);

		memset(&cfg, 0, sizeof(cfg));
		cfg.diag = diag;
		fyd = fy_document_build_from_string(&cfg, cases[i].yaml, FY_NT);
		ck_assert_ptr_eq(fyd, NULL);
		ck_assert_msg(strstr(dt.buf, cases[i].line),
			      "case %zu: no whole line in:\n%s", i, dt.buf);

		fy_diag_destroy(diag);
	}
}
END_TEST

#if defined(__linux__)
START_TEST(parse_bug_ypath_alias_path_key_at_eof)
{
	const char data[] = "\x32\x3a\x09\x2a\x30";
	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd = NULL;
	FILE *fp = NULL;

	cfg.flags = FYPCF_QUIET |
		    FYPCF_RESOLVE_DOCUMENT |
		    FYPCF_DISABLE_MMAP_OPT |
		    FYPCF_DEFAULT_VERSION_1_1 |
		    FYPCF_PREFER_RECURSIVE |
		    FYPCF_JSON_NONE |
		    FYPCF_YPATH_ALIASES |
		    FYPCF_ALLOW_DUPLICATE_KEYS |
		    FYPCF_ENABLE_CACHE;

	fp = fmemopen((void *)data, sizeof(data) - 1, "r");
	ck_assert_ptr_ne(fp, NULL);

	fyd = fy_document_build_from_fp(&cfg, fp);

	fy_document_destroy(fyd);
	fclose(fp);
}
END_TEST
#endif

/* a shorthand tag in a flow collection ends at a flow indicator (gh#454) */
static void check_flow_tag(const char *yaml, const char *path, const char *tag)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_QUIET };
	struct fy_document *fyd;
	struct fy_node *fyn;
	const char *text;
	size_t len;

	fyd = fy_document_build_from_string(&cfg, yaml, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	fyn = fy_node_by_path(fy_document_root(fyd), path, FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);

	text = fy_node_get_tag(fyn, &len);
	ck_assert_ptr_ne(text, NULL);
	ck_assert_uint_eq(len, strlen(tag));
	ck_assert_int_eq(memcmp(text, tag, len), 0);

	fy_document_destroy(fyd);
}

START_TEST(parse_bug_flow_tag_before_flow_end)
{
	/* the tag is !b, the ']' and '}' close the collections */
	check_flow_tag("{a: [!b]}", "/a/0", "!b");
}
END_TEST

START_TEST(parse_bug_flow_tag_before_comma)
{
	check_flow_tag("[!b,c]", "/0", "!b");
}
END_TEST

START_TEST(parse_bug_flow_verbatim_tag_keeps_indicators)
{
	/* a verbatim tag may hold flow indicators */
	check_flow_tag("[!<tag:x,y]>]", "/0", "tag:x,y]");
}
END_TEST

/* a long number in a %YAML directive must not overflow (gh#466) */
START_TEST(parse_bug_yaml_version_long_number)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_QUIET };
	struct fy_document *fyd;

	/* the version is rejected, the digits must not overflow an int */
	fyd = fy_document_build_from_string(&cfg,
			"%YAML 11111111111.22222222222\n---\na\n", FY_NT);
	ck_assert_ptr_eq(fyd, NULL);
}
END_TEST

/* ── registration ────────────────────────────────────────────────── */

/*
 * a block scalar with a tag or anchor in front, on a line of its own, takes
 * its indent from the tag, e.g. "!!str |" at the root has content "abc\n"
 */
START_TEST(parse_bug_block_scalar_after_properties)
{
	static const struct {
		const char *yaml;
		int index;		/* of the scalar to check */
		const char *value;
	} cases[] = {
		{ "!!str |\n  abc\n", 0, "abc\n" },
		{ "!!str >\n  abc\n  def\n", 0, "abc def\n" },
		{ "&a |\n  abc\n", 0, "abc\n" },
		{ "&a !!str |-\n  abc\n", 0, "abc" },
		{ "!!binary |\n  UE5H\n", 0, "UE5H\n" },
		{ "!!str |\n abc\n", 0, "abc\n" },
		{ "key:\n  !!str |\n    text\n", 1, "text\n" },
		{ "-\n  &a !!str >\n    text\n", 0, "text\n" },
		{ "- !!str |\n  abc\n", 0, "abc\n" },
		{ "a: !!str |\n  abc\nb: x\n", 1, "abc\n" },
		{ "--- !!str |\n  abc\n", 0, "abc\n" },
	};
	struct fy_parse_cfg cfg = { .flags = FYPCF_DEFAULT_PARSE };
	struct fy_parser *fyp;
	struct fy_event *fye;
	const char *text;
	size_t i, tlen;
	int idx;
	bool found;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		fyp = fy_parser_create(&cfg);
		ck_assert_ptr_ne(fyp, NULL);
		ck_assert_int_eq(fy_parser_set_string(fyp, cases[i].yaml, FY_NT), 0);

		idx = 0;
		found = false;
		while ((fye = fy_parser_parse(fyp)) != NULL) {
			if (fye->type == FYET_SCALAR) {
				if (idx == cases[i].index) {
					text = fy_token_get_text(fye->scalar.value, &tlen);
					ck_assert_msg(text && tlen == strlen(cases[i].value) &&
						      !memcmp(text, cases[i].value, tlen),
						      "case %zu: got \"%.*s\"", i, (int)tlen, text ? text : "");
					found = true;
				}
				idx++;
			}
			fy_parser_event_free(fyp, fye);
		}
		ck_assert_msg(!fy_parser_get_stream_error(fyp), "case %zu: parse error", i);
		ck_assert_msg(found, "case %zu: no scalar %d", i, cases[i].index);
		fy_parser_destroy(fyp);
	}
}
END_TEST

void libfyaml_case_parse_bugs(struct fy_check_suite *cs)
{
	struct fy_check_testcase *ctc;

	ctc = fy_check_suite_add_test_case(cs, "parse-bugs");

	/* Bug 14: NEL block scalar spurious null byte */
	fy_check_testcase_add_test(ctc, parse_bug_nel_clip_chomping);
	fy_check_testcase_add_test(ctc, parse_bug_nel_keep_chomping);
	fy_check_testcase_add_test(ctc, parse_bug_nel_strip_chomping_ok);
	fy_check_testcase_add_test(ctc, parse_bug_nel_spec_09_22);

	/* Bug 15: Invalid UTF-8 and NUL in input stream */
	fy_check_testcase_add_test(ctc, parse_bug_nul_in_stream);
	fy_check_testcase_add_test(ctc, parse_bug_nul_in_comment);
	fy_check_testcase_add_test(ctc, parse_bug_partial_utf8_2byte);
	fy_check_testcase_add_test(ctc, parse_bug_partial_utf8_3byte);
	fy_check_testcase_add_test(ctc, parse_bug_partial_utf8_4byte);
	fy_check_testcase_add_test(ctc, parse_bug_invalid_utf8_lone_continuation);
	fy_check_testcase_add_test(ctc, parse_bug_invalid_utf8_overlong);
	fy_check_testcase_add_test(ctc, parse_bug_invalid_utf8_fe);
	fy_check_testcase_add_test(ctc, parse_bug_invalid_utf8_ff);
	fy_check_testcase_add_test(ctc, parse_valid_utf8_ok);
	fy_check_testcase_add_test(ctc, parse_bug_partial_utf8_at_eof);
	fy_check_testcase_add_test(ctc, parse_bug_invalid_utf8_context_line);

	/* extra parse bugs */
	fy_check_testcase_add_test(ctc, parse_bug_single_quoted_single_quotes);
	fy_check_testcase_add_test(ctc, parse_bug_nested_explicit_key);
	fy_check_testcase_add_test(ctc, parse_bug_tag_without_suffix);
	fy_check_testcase_add_test(ctc, parse_bug_folded_empty_lines_before_more_indented);

	/* Bug 16: escaped space followed by newline in double-quoted scalar */
	fy_check_testcase_add_test(ctc, parse_bug_escaped_space_before_newline_only);
	fy_check_testcase_add_test(ctc, parse_bug_escaped_space_before_newline_with_content);

	/* Issue #466: a long number in a %YAML directive */
	fy_check_testcase_add_test(ctc, parse_bug_yaml_version_long_number);

	/* Issue #454: a shorthand tag in a flow collection */
	fy_check_testcase_add_test(ctc, parse_bug_flow_tag_before_flow_end);
	fy_check_testcase_add_test(ctc, parse_bug_flow_tag_before_comma);
	fy_check_testcase_add_test(ctc, parse_bug_flow_verbatim_tag_keeps_indicators);

	/* a block scalar with a tag or anchor in front */
	fy_check_testcase_add_test(ctc, parse_bug_block_scalar_after_properties);

#if defined(__linux__)
	/* Issue #309: path key duplication must not read past direct token text */
	fy_check_testcase_add_test(ctc, parse_bug_ypath_alias_path_key_at_eof);
#endif
}
