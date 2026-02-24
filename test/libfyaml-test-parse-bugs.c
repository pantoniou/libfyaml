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

/* ── registration ────────────────────────────────────────────────── */

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

	/* extra parse bugs */
	fy_check_testcase_add_test(ctc, parse_bug_single_quoted_single_quotes);
}
