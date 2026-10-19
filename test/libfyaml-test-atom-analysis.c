/*
 * libfyaml-test-atom-analysis.c - scalar analysis flag coverage tests
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
#include <inttypes.h>

#include <check.h>

#include <libfyaml.h>

#include "fy-atom.h"
#include "fy-token.h"
#include "fy-input.h"
#include "fy-utf8.h"

#include "fy-check.h"

struct atom_analysis_case {
	const char *name;
	const void *data;
	size_t size;
	enum fy_scalar_style style;
	enum fy_lb_mode lb_mode;
	uint64_t set_flags;
	uint64_t clear_flags;
	enum fy_scalar_style preferred_style;
};

static const unsigned char zero_byte[] = { 0x00 };
static const unsigned char cr_byte[] = { '\r' };
static const unsigned char high_ascii_at_end_utf8[] = { 'a', 0xc2, 0xa9 };

static const struct {
	uint64_t flag;
	const char *name;
} atom_flag_names[] = {
	{ FYTTAF_HAS_LB, "HAS_LB" },
	{ FYTTAF_HAS_WS, "HAS_WS" },
	{ FYTTAF_HAS_CONSECUTIVE_LB, "HAS_CONSECUTIVE_LB" },
	{ FYTTAF_HAS_START_LB, "HAS_START_LB" },
	{ FYTTAF_HAS_CONSECUTIVE_WS, "HAS_CONSECUTIVE_WS" },
	{ FYTTAF_EMPTY, "EMPTY" },
	{ FYTTAF_CAN_BE_SIMPLE_KEY, "CAN_BE_SIMPLE_KEY" },
	{ FYTTAF_DIRECT_OUTPUT, "DIRECT_OUTPUT" },
	{ FYTTAF_NO_TEXT_TOKEN, "NO_TEXT_TOKEN" },
	{ FYTTAF_TEXT_TOKEN, "TEXT_TOKEN" },
	{ FYTTAF_CAN_BE_PLAIN, "CAN_BE_PLAIN" },
	{ FYTTAF_CAN_BE_SINGLE_QUOTED, "CAN_BE_SINGLE_QUOTED" },
	{ FYTTAF_CAN_BE_DOUBLE_QUOTED, "CAN_BE_DOUBLE_QUOTED" },
	{ FYTTAF_CAN_BE_LITERAL, "CAN_BE_LITERAL" },
	{ FYTTAF_CAN_BE_FOLDED, "CAN_BE_FOLDED" },
	{ FYTTAF_CAN_BE_PLAIN_FLOW, "CAN_BE_PLAIN_FLOW" },
	{ FYTTAF_QUOTE_AT_0, "QUOTE_AT_0" },
	{ FYTTAF_CAN_BE_UNQUOTED_PATH_KEY, "CAN_BE_UNQUOTED_PATH_KEY" },
	{ FYTTAF_HAS_ANY_LB, "HAS_ANY_LB" },
	{ FYTTAF_HAS_START_IND, "HAS_START_IND" },
	{ FYTTAF_HAS_END_IND, "HAS_END_IND" },
	{ FYTTAF_HAS_NON_PRINT, "HAS_NON_PRINT" },
	{ FYTTAF_ENDS_WITH_COLON, "ENDS_WITH_COLON" },
	{ FYTTAF_ALL_WS_LB, "ALL_WS_LB" },
	{ FYTTAF_ALL_PRINT_ASCII, "ALL_PRINT_ASCII" },
	{ FYTTAF_HAS_START_WS, "HAS_START_WS" },
	{ FYTTAF_SIZE0, "SIZE0" },
	{ FYTTAF_HAS_ZERO, "HAS_ZERO" },
	{ FYTTAF_HAS_NON_NL_LB, "HAS_NON_NL_LB" },
	{ FYTTAF_HAS_END_WS, "HAS_END_WS" },
	{ FYTTAF_HAS_END_LB, "HAS_END_LB" },
	{ FYTTAF_HAS_TRAILING_LB, "HAS_TRAILING_LB" },
	{ FYTTAF_VALID_ANCHOR, "VALID_ANCHOR" },
	{ FYTTAF_JSON_ESCAPE, "JSON_ESCAPE" },
	{ FYTTAF_HIGH_ASCII, "HIGH_ASCII" },
	{ FYTTAF_HAS_INVALID_UTF8, "HAS_INVALID_UTF8" },
	{ FYTTAF_HAS_PARTIAL_UTF8, "HAS_PARTIAL_UTF8" },
	{ FYTTAF_LIKELY_BINARY, "LIKELY_BINARY" },
	{ FYTTAF_ANALYZED, "ANALYZED" },
};

static const uint64_t atom_analysis_coverage_mask =
	FYTTAF_HAS_LB |
	FYTTAF_HAS_WS |
	FYTTAF_HAS_CONSECUTIVE_LB |
	FYTTAF_HAS_START_LB |
	FYTTAF_HAS_CONSECUTIVE_WS |
	FYTTAF_EMPTY |
	FYTTAF_CAN_BE_SIMPLE_KEY |
	FYTTAF_DIRECT_OUTPUT |
	FYTTAF_NO_TEXT_TOKEN |
	FYTTAF_TEXT_TOKEN |
	FYTTAF_CAN_BE_PLAIN |
	FYTTAF_CAN_BE_SINGLE_QUOTED |
	FYTTAF_CAN_BE_DOUBLE_QUOTED |
	FYTTAF_CAN_BE_LITERAL |
	FYTTAF_CAN_BE_FOLDED |
	FYTTAF_CAN_BE_PLAIN_FLOW |
	FYTTAF_QUOTE_AT_0 |
	FYTTAF_CAN_BE_UNQUOTED_PATH_KEY |
	FYTTAF_HAS_ANY_LB |
	FYTTAF_HAS_START_IND |
	FYTTAF_HAS_END_IND |
	FYTTAF_HAS_NON_PRINT |
	FYTTAF_ENDS_WITH_COLON |
	FYTTAF_ALL_WS_LB |
	FYTTAF_ALL_PRINT_ASCII |
	FYTTAF_HAS_START_WS |
	FYTTAF_SIZE0 |
	FYTTAF_HAS_ZERO |
	FYTTAF_HAS_NON_NL_LB |
	FYTTAF_HAS_END_WS |
	FYTTAF_HAS_END_LB |
	FYTTAF_HAS_TRAILING_LB |
	FYTTAF_VALID_ANCHOR |
	FYTTAF_JSON_ESCAPE |
	FYTTAF_HIGH_ASCII |
	FYTTAF_HAS_INVALID_UTF8 |
	FYTTAF_HAS_PARTIAL_UTF8 |
	FYTTAF_LIKELY_BINARY |
	FYTTAF_ANALYZED;

static void format_flags(uint64_t flags, char *buf, size_t bufsz)
{
	size_t i, off = 0;
	int n;

	if (!bufsz)
		return;

	buf[0] = '\0';
	for (i = 0; i < ARRAY_SIZE(atom_flag_names); i++) {
		if (!(flags & atom_flag_names[i].flag))
			continue;
		n = snprintf(buf + off, bufsz - off, "%s%s",
			     off ? "|" : "", atom_flag_names[i].name);
		if (n < 0 || (size_t)n >= bufsz - off) {
			off = bufsz - 1;
			break;
		}
		off += (size_t)n;
	}
	if (!off)
		snprintf(buf, bufsz, "0");
}

static void check_atom_analysis_case(const struct atom_analysis_case *tc)
{
	struct fy_text_analysis ta;
	char got_buf[1024], want_set_buf[1024], want_clear_buf[1024];

	memset(&ta, 0, sizeof(ta));
	fy_analyze_scalar_content(tc->data, tc->size, tc->style, tc->lb_mode, &ta);

	format_flags(ta.flags, got_buf, sizeof(got_buf));
	format_flags(tc->set_flags, want_set_buf, sizeof(want_set_buf));
	format_flags(tc->clear_flags, want_clear_buf, sizeof(want_clear_buf));

	ck_assert_msg((ta.flags & tc->set_flags) == tc->set_flags,
		      "%s: missing expected flags\nhave=0x%016" PRIx64 " (%s)\nwant=0x%016" PRIx64 " (%s)",
		      tc->name, ta.flags, got_buf, tc->set_flags, want_set_buf);
	ck_assert_msg((ta.flags & tc->clear_flags) == 0,
		      "%s: unexpected flags present\nhave=0x%016" PRIx64 " (%s)\nunexpected=0x%016" PRIx64 " (%s)",
		      tc->name, ta.flags, got_buf, tc->clear_flags, want_clear_buf);
	if (tc->preferred_style != FYSS_ANY)
		ck_assert_msg(ta.preferred_style == tc->preferred_style,
			      "%s: preferred_style=%d expected=%d",
			      tc->name, ta.preferred_style, tc->preferred_style);
}

static void check_atom_analysis_flag_case(const struct atom_analysis_case *tc,
					  uint64_t flag, const char *flag_name)
{
	struct fy_text_analysis ta;
	char got_buf[1024];

	memset(&ta, 0, sizeof(ta));
	fy_analyze_scalar_content(tc->data, tc->size, tc->style, tc->lb_mode, &ta);

	format_flags(ta.flags, got_buf, sizeof(got_buf));
	ck_assert_msg(ta.flags & flag,
		      "%s: expected set in %s\nhave=0x%016" PRIx64 " (%s)",
		      flag_name, tc->name, ta.flags, got_buf);
}

/* same case, through an input atom instead of the raw content */
static void check_atom_analysis_case_atom(const struct atom_analysis_case *tc)
{
	struct fy_text_analysis ta;
	struct fy_input *fyi;
	struct fy_atom handle;
	char got_buf[1024];

	memset(&handle, 0, sizeof(handle));
	fyi = fy_input_from_data(tc->data, tc->size, &handle, false);
	ck_assert_ptr_ne(fyi, NULL);

	memset(&ta, 0, sizeof(ta));
	fy_atom_text_analyze(&handle, handle.style, &ta);
	fy_input_unref(fyi);

	format_flags(ta.flags, got_buf, sizeof(got_buf));
	ck_assert_msg((ta.flags & tc->set_flags) == tc->set_flags,
		      "%s (atom): missing expected flags\nhave=0x%016" PRIx64 " (%s)",
		      tc->name, ta.flags, got_buf);
	ck_assert_msg((ta.flags & tc->clear_flags) == 0,
		      "%s (atom): unexpected flags present\nhave=0x%016" PRIx64 " (%s)",
		      tc->name, ta.flags, got_buf);
}

START_TEST(atom_analysis_flag_no_text_token)
{
	struct fy_token *fyt;
	const struct fy_text_analysis *ta;

	fyt = fy_token_create(FYTT_STREAM_START, NULL);
	ck_assert_ptr_ne(fyt, NULL);

	ta = fy_token_text_analyze(fyt);
	ck_assert_ptr_ne(ta, NULL);
	ck_assert_msg((ta->flags & (FYTTAF_NO_TEXT_TOKEN | FYTTAF_ANALYZED)) ==
		      (FYTTAF_NO_TEXT_TOKEN | FYTTAF_ANALYZED),
		      "non-text token flags=0x%016" PRIx64, ta->flags);
	ck_assert_msg((ta->flags & FYTTAF_TEXT_TOKEN) == 0,
		      "non-text token unexpectedly has TEXT_TOKEN set: 0x%016" PRIx64,
		      ta->flags);

	fy_token_unref(fyt);
}
END_TEST

START_TEST(atom_analysis_coverage)
{
	static const uint64_t covered =
		FYTTAF_HAS_LB |
		FYTTAF_HAS_WS |
		FYTTAF_HAS_CONSECUTIVE_LB |
		FYTTAF_HAS_START_LB |
		FYTTAF_HAS_CONSECUTIVE_WS |
		FYTTAF_EMPTY |
		FYTTAF_CAN_BE_SIMPLE_KEY |
		FYTTAF_DIRECT_OUTPUT |
		FYTTAF_NO_TEXT_TOKEN |
		FYTTAF_TEXT_TOKEN |
		FYTTAF_CAN_BE_PLAIN |
		FYTTAF_CAN_BE_SINGLE_QUOTED |
		FYTTAF_CAN_BE_DOUBLE_QUOTED |
		FYTTAF_CAN_BE_LITERAL |
		FYTTAF_CAN_BE_FOLDED |
		FYTTAF_CAN_BE_PLAIN_FLOW |
		FYTTAF_QUOTE_AT_0 |
		FYTTAF_CAN_BE_UNQUOTED_PATH_KEY |
		FYTTAF_HAS_ANY_LB |
		FYTTAF_HAS_START_IND |
		FYTTAF_HAS_END_IND |
		FYTTAF_HAS_NON_PRINT |
		FYTTAF_ENDS_WITH_COLON |
		FYTTAF_ALL_WS_LB |
		FYTTAF_ALL_PRINT_ASCII |
		FYTTAF_HAS_START_WS |
		FYTTAF_SIZE0 |
		FYTTAF_HAS_ZERO |
		FYTTAF_HAS_NON_NL_LB |
		FYTTAF_HAS_END_WS |
		FYTTAF_HAS_END_LB |
		FYTTAF_HAS_TRAILING_LB |
		FYTTAF_VALID_ANCHOR |
		FYTTAF_JSON_ESCAPE |
		FYTTAF_HIGH_ASCII |
		FYTTAF_HAS_INVALID_UTF8 |
		FYTTAF_HAS_PARTIAL_UTF8 |
		FYTTAF_LIKELY_BINARY |
		FYTTAF_ANALYZED;

	ck_assert_msg((covered & atom_analysis_coverage_mask) == atom_analysis_coverage_mask,
		      "missing flag coverage mask=0x%016" PRIx64 " covered=0x%016" PRIx64,
		      atom_analysis_coverage_mask, covered);
}
END_TEST

START_TEST(atom_analysis_empty)
{
	static const struct atom_analysis_case tc = {
		.name = "empty",
		.data = "",
		.size = 0,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_EMPTY | FYTTAF_SIZE0 |
			     FYTTAF_CAN_BE_SIMPLE_KEY | FYTTAF_DIRECT_OUTPUT |
			     FYTTAF_CAN_BE_PLAIN | FYTTAF_CAN_BE_SINGLE_QUOTED |
			     FYTTAF_CAN_BE_DOUBLE_QUOTED | FYTTAF_CAN_BE_LITERAL |
			     FYTTAF_CAN_BE_FOLDED | FYTTAF_CAN_BE_PLAIN_FLOW |
			     FYTTAF_CAN_BE_UNQUOTED_PATH_KEY | FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_HAS_LB | FYTTAF_HAS_WS | FYTTAF_ALL_WS_LB |
			       FYTTAF_ALL_PRINT_ASCII | FYTTAF_VALID_ANCHOR,
		.preferred_style = FYSS_PLAIN,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_plain_ascii)
{
	static const struct atom_analysis_case tc = {
		.name = "plain_ascii",
		.data = "abc123",
		.size = 6,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_CAN_BE_SIMPLE_KEY |
			     FYTTAF_DIRECT_OUTPUT | FYTTAF_CAN_BE_PLAIN |
			     FYTTAF_CAN_BE_SINGLE_QUOTED | FYTTAF_CAN_BE_DOUBLE_QUOTED |
			     FYTTAF_CAN_BE_LITERAL | FYTTAF_CAN_BE_FOLDED |
			     FYTTAF_CAN_BE_PLAIN_FLOW | FYTTAF_CAN_BE_UNQUOTED_PATH_KEY |
			     FYTTAF_ALL_PRINT_ASCII | FYTTAF_VALID_ANCHOR |
			     FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_EMPTY | FYTTAF_SIZE0 | FYTTAF_HAS_LB |
			       FYTTAF_HAS_WS | FYTTAF_JSON_ESCAPE | FYTTAF_HIGH_ASCII,
		.preferred_style = FYSS_PLAIN,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_single_space)
{
	static const struct atom_analysis_case tc = {
		.name = "single_space",
		.data = " ",
		.size = 1,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_EMPTY | FYTTAF_HAS_WS |
			     FYTTAF_CAN_BE_SIMPLE_KEY | FYTTAF_DIRECT_OUTPUT |
			     FYTTAF_CAN_BE_SINGLE_QUOTED | FYTTAF_CAN_BE_DOUBLE_QUOTED |
			     FYTTAF_CAN_BE_LITERAL | FYTTAF_CAN_BE_FOLDED |
			     FYTTAF_ALL_WS_LB | FYTTAF_HAS_START_WS |
			     FYTTAF_HAS_END_WS | FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_HAS_LB | FYTTAF_CAN_BE_PLAIN |
			       FYTTAF_CAN_BE_PLAIN_FLOW |
			       FYTTAF_CAN_BE_UNQUOTED_PATH_KEY | FYTTAF_VALID_ANCHOR |
			       FYTTAF_ALL_PRINT_ASCII | FYTTAF_JSON_ESCAPE,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_double_space)
{
	static const struct atom_analysis_case tc = {
		.name = "double_space",
		.data = "  ",
		.size = 2,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_EMPTY | FYTTAF_HAS_WS |
			     FYTTAF_CAN_BE_SIMPLE_KEY | FYTTAF_DIRECT_OUTPUT |
			     FYTTAF_CAN_BE_SINGLE_QUOTED | FYTTAF_CAN_BE_DOUBLE_QUOTED |
			     FYTTAF_CAN_BE_LITERAL | FYTTAF_CAN_BE_FOLDED |
			     FYTTAF_ALL_WS_LB | FYTTAF_HAS_START_WS |
			     FYTTAF_HAS_END_WS | FYTTAF_HAS_CONSECUTIVE_WS |
			     FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_HAS_LB | FYTTAF_CAN_BE_PLAIN |
			       FYTTAF_CAN_BE_PLAIN_FLOW |
			       FYTTAF_CAN_BE_UNQUOTED_PATH_KEY | FYTTAF_VALID_ANCHOR |
			       FYTTAF_ALL_PRINT_ASCII | FYTTAF_JSON_ESCAPE,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_tail_double_space)
{
	static const struct atom_analysis_case tc = {
		.name = "tail_double_space",
		.data = "a  ",
		.size = 3,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_HAS_WS | FYTTAF_DIRECT_OUTPUT |
			     FYTTAF_CAN_BE_SINGLE_QUOTED | FYTTAF_CAN_BE_DOUBLE_QUOTED |
			     FYTTAF_CAN_BE_LITERAL | FYTTAF_CAN_BE_FOLDED |
			     FYTTAF_HAS_END_WS | FYTTAF_HAS_CONSECUTIVE_WS |
			     FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_EMPTY | FYTTAF_HAS_LB | FYTTAF_CAN_BE_PLAIN |
			       FYTTAF_CAN_BE_PLAIN_FLOW |
			       FYTTAF_CAN_BE_UNQUOTED_PATH_KEY | FYTTAF_VALID_ANCHOR |
			       FYTTAF_ALL_WS_LB | FYTTAF_ALL_PRINT_ASCII |
			       FYTTAF_JSON_ESCAPE,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_single_newline)
{
	static const struct atom_analysis_case tc = {
		.name = "single_newline",
		.data = "\n",
		.size = 1,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_HAS_LB | FYTTAF_HAS_START_LB |
			     FYTTAF_HAS_END_LB | FYTTAF_HAS_ANY_LB | FYTTAF_ALL_WS_LB |
			     FYTTAF_CAN_BE_SINGLE_QUOTED | FYTTAF_CAN_BE_DOUBLE_QUOTED |
			     FYTTAF_CAN_BE_LITERAL | FYTTAF_CAN_BE_FOLDED |
			     FYTTAF_JSON_ESCAPE | FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_CAN_BE_SIMPLE_KEY | FYTTAF_DIRECT_OUTPUT |
			       FYTTAF_CAN_BE_PLAIN | FYTTAF_CAN_BE_PLAIN_FLOW |
			       FYTTAF_CAN_BE_UNQUOTED_PATH_KEY |
			       FYTTAF_ALL_PRINT_ASCII,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_double_newline)
{
	static const struct atom_analysis_case tc = {
		.name = "double_newline",
		.data = "\n\n",
		.size = 2,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_HAS_LB | FYTTAF_HAS_START_LB |
			     FYTTAF_HAS_END_LB | FYTTAF_HAS_ANY_LB |
			     FYTTAF_HAS_CONSECUTIVE_LB | FYTTAF_HAS_TRAILING_LB |
			     FYTTAF_ALL_WS_LB | FYTTAF_CAN_BE_SINGLE_QUOTED |
			     FYTTAF_CAN_BE_DOUBLE_QUOTED | FYTTAF_CAN_BE_LITERAL |
			     FYTTAF_CAN_BE_FOLDED | FYTTAF_JSON_ESCAPE |
			     FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_CAN_BE_SIMPLE_KEY | FYTTAF_DIRECT_OUTPUT |
			       FYTTAF_CAN_BE_PLAIN | FYTTAF_CAN_BE_PLAIN_FLOW |
			       FYTTAF_CAN_BE_UNQUOTED_PATH_KEY |
			       FYTTAF_ALL_PRINT_ASCII,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_tail_double_newline)
{
	static const struct atom_analysis_case tc = {
		.name = "tail_double_newline",
		.data = "a\n\n",
		.size = 3,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_HAS_LB | FYTTAF_HAS_END_LB |
			     FYTTAF_HAS_ANY_LB | FYTTAF_HAS_CONSECUTIVE_LB |
			     FYTTAF_HAS_TRAILING_LB | FYTTAF_CAN_BE_SINGLE_QUOTED |
			     FYTTAF_CAN_BE_DOUBLE_QUOTED | FYTTAF_CAN_BE_LITERAL |
			     FYTTAF_CAN_BE_FOLDED | FYTTAF_JSON_ESCAPE |
			     FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_EMPTY | FYTTAF_CAN_BE_SIMPLE_KEY |
			       FYTTAF_DIRECT_OUTPUT | FYTTAF_CAN_BE_PLAIN |
			       FYTTAF_CAN_BE_PLAIN_FLOW |
			       FYTTAF_CAN_BE_UNQUOTED_PATH_KEY | FYTTAF_ALL_WS_LB |
			       FYTTAF_ALL_PRINT_ASCII,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_carriage_return)
{
	static const struct atom_analysis_case tc = {
		.name = "carriage_return",
		.data = cr_byte,
		.size = sizeof(cr_byte),
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_HAS_LB | FYTTAF_HAS_START_LB |
			     FYTTAF_HAS_END_LB | FYTTAF_HAS_ANY_LB |
			     FYTTAF_HAS_NON_NL_LB | FYTTAF_ALL_WS_LB |
			     FYTTAF_CAN_BE_SINGLE_QUOTED | FYTTAF_CAN_BE_DOUBLE_QUOTED |
			     FYTTAF_CAN_BE_LITERAL | FYTTAF_CAN_BE_FOLDED |
			     FYTTAF_JSON_ESCAPE | FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_CAN_BE_SIMPLE_KEY | FYTTAF_DIRECT_OUTPUT |
			       FYTTAF_CAN_BE_PLAIN | FYTTAF_CAN_BE_PLAIN_FLOW |
			       FYTTAF_CAN_BE_UNQUOTED_PATH_KEY |
			       FYTTAF_ALL_PRINT_ASCII,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_hash_comment_plain_block)
{
	static const struct atom_analysis_case tc = {
		.name = "hash_comment_plain_block",
		.data = "a #",
		.size = 3,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_HAS_WS | FYTTAF_DIRECT_OUTPUT |
			     FYTTAF_CAN_BE_SINGLE_QUOTED | FYTTAF_CAN_BE_DOUBLE_QUOTED |
			     FYTTAF_CAN_BE_LITERAL | FYTTAF_CAN_BE_FOLDED |
			     FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_EMPTY | FYTTAF_HAS_LB | FYTTAF_CAN_BE_PLAIN |
			       FYTTAF_CAN_BE_PLAIN_FLOW |
			       FYTTAF_CAN_BE_UNQUOTED_PATH_KEY | FYTTAF_VALID_ANCHOR |
			       FYTTAF_ALL_WS_LB | FYTTAF_ALL_PRINT_ASCII |
			       FYTTAF_JSON_ESCAPE,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_flow_comma_space)
{
	static const struct atom_analysis_case tc = {
		.name = "flow_comma_space",
		.data = "a, ",
		.size = 3,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_HAS_WS | FYTTAF_HAS_END_WS |
			     FYTTAF_CAN_BE_SIMPLE_KEY | FYTTAF_DIRECT_OUTPUT |
			     FYTTAF_CAN_BE_SINGLE_QUOTED | FYTTAF_CAN_BE_DOUBLE_QUOTED |
			     FYTTAF_CAN_BE_LITERAL | FYTTAF_CAN_BE_FOLDED |
			     FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_EMPTY | FYTTAF_HAS_LB | FYTTAF_CAN_BE_PLAIN |
			       FYTTAF_CAN_BE_PLAIN_FLOW |
			       FYTTAF_CAN_BE_UNQUOTED_PATH_KEY | FYTTAF_VALID_ANCHOR |
			       FYTTAF_ALL_PRINT_ASCII | FYTTAF_JSON_ESCAPE,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_ends_with_colon)
{
	static const struct atom_analysis_case tc = {
		.name = "ends_with_colon",
		.data = "a:",
		.size = 2,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_CAN_BE_SIMPLE_KEY |
			     FYTTAF_DIRECT_OUTPUT | FYTTAF_CAN_BE_SINGLE_QUOTED |
			     FYTTAF_CAN_BE_DOUBLE_QUOTED | FYTTAF_CAN_BE_LITERAL |
			     FYTTAF_CAN_BE_FOLDED | FYTTAF_ENDS_WITH_COLON |
			     FYTTAF_ALL_PRINT_ASCII | FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_EMPTY | FYTTAF_HAS_LB | FYTTAF_HAS_WS |
			       FYTTAF_CAN_BE_PLAIN | FYTTAF_CAN_BE_PLAIN_FLOW |
			       FYTTAF_CAN_BE_UNQUOTED_PATH_KEY,
		.preferred_style = FYSS_SINGLE_QUOTED,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_colon_before_flow_indicator)
{
	static const struct atom_analysis_case tc = {
		.name = "colon_before_flow_indicator",
		.data = "a:]",
		.size = 3,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_CAN_BE_SIMPLE_KEY |
			     FYTTAF_DIRECT_OUTPUT | FYTTAF_CAN_BE_PLAIN |
			     FYTTAF_CAN_BE_SINGLE_QUOTED | FYTTAF_CAN_BE_DOUBLE_QUOTED |
			     FYTTAF_CAN_BE_LITERAL | FYTTAF_CAN_BE_FOLDED |
			     FYTTAF_ALL_PRINT_ASCII | FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_EMPTY | FYTTAF_HAS_LB | FYTTAF_HAS_WS |
			       FYTTAF_CAN_BE_PLAIN_FLOW |
			       FYTTAF_CAN_BE_UNQUOTED_PATH_KEY,
		.preferred_style = FYSS_SINGLE_QUOTED,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_start_indicator)
{
	static const struct atom_analysis_case tc = {
		.name = "start_indicator",
		.data = "---\n",
		.size = 4,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_HAS_LB | FYTTAF_HAS_END_LB |
			     FYTTAF_HAS_ANY_LB | FYTTAF_HAS_START_IND |
			     FYTTAF_QUOTE_AT_0 | FYTTAF_CAN_BE_SINGLE_QUOTED |
			     FYTTAF_CAN_BE_DOUBLE_QUOTED | FYTTAF_CAN_BE_LITERAL |
			     FYTTAF_CAN_BE_FOLDED | FYTTAF_JSON_ESCAPE |
			     FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_EMPTY | FYTTAF_CAN_BE_SIMPLE_KEY |
			       FYTTAF_DIRECT_OUTPUT | FYTTAF_CAN_BE_PLAIN |
			       FYTTAF_CAN_BE_PLAIN_FLOW | FYTTAF_ALL_PRINT_ASCII,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_end_indicator)
{
	static const struct atom_analysis_case tc = {
		.name = "end_indicator",
		.data = "...\n",
		.size = 4,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_HAS_LB | FYTTAF_HAS_END_LB |
			     FYTTAF_HAS_ANY_LB | FYTTAF_HAS_END_IND |
			     FYTTAF_QUOTE_AT_0 | FYTTAF_CAN_BE_SINGLE_QUOTED |
			     FYTTAF_CAN_BE_DOUBLE_QUOTED | FYTTAF_CAN_BE_LITERAL |
			     FYTTAF_CAN_BE_FOLDED | FYTTAF_JSON_ESCAPE |
			     FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_EMPTY | FYTTAF_CAN_BE_SIMPLE_KEY |
			       FYTTAF_DIRECT_OUTPUT | FYTTAF_CAN_BE_PLAIN |
			       FYTTAF_CAN_BE_PLAIN_FLOW | FYTTAF_ALL_PRINT_ASCII,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_zero_byte)
{
	static const struct atom_analysis_case tc = {
		.name = "zero_byte",
		.data = zero_byte,
		.size = sizeof(zero_byte),
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_HAS_ZERO | FYTTAF_HAS_NON_PRINT |
			     FYTTAF_CAN_BE_DOUBLE_QUOTED | FYTTAF_JSON_ESCAPE |
			     FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_DIRECT_OUTPUT | FYTTAF_CAN_BE_PLAIN |
			       FYTTAF_CAN_BE_SINGLE_QUOTED | FYTTAF_CAN_BE_LITERAL |
			       FYTTAF_CAN_BE_FOLDED | FYTTAF_CAN_BE_PLAIN_FLOW |
			       FYTTAF_VALID_ANCHOR,
		.preferred_style = FYSS_DOUBLE_QUOTED,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

START_TEST(atom_analysis_high_ascii)
{
	static const struct atom_analysis_case tc = {
		.name = "high_ascii",
		.data = high_ascii_at_end_utf8,
		.size = sizeof(high_ascii_at_end_utf8),
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN | FYTTAF_CAN_BE_SIMPLE_KEY |
			     FYTTAF_DIRECT_OUTPUT | FYTTAF_CAN_BE_PLAIN |
			     FYTTAF_CAN_BE_SINGLE_QUOTED | FYTTAF_CAN_BE_DOUBLE_QUOTED |
			     FYTTAF_CAN_BE_LITERAL | FYTTAF_CAN_BE_FOLDED |
			     FYTTAF_CAN_BE_PLAIN_FLOW | FYTTAF_HIGH_ASCII |
			     FYTTAF_ANALYZED,
		.clear_flags = FYTTAF_ALL_PRINT_ASCII |
			       FYTTAF_CAN_BE_UNQUOTED_PATH_KEY,
		.preferred_style = FYSS_PLAIN,
	};

	check_atom_analysis_case(&tc);
}
END_TEST

/* a scalar with bad bytes can only be double quoted, and is never direct */
#define BAD_UTF8_CLEAR \
	(FYTTAF_DIRECT_OUTPUT | FYTTAF_CAN_BE_PLAIN | FYTTAF_CAN_BE_PLAIN_FLOW | \
	 FYTTAF_CAN_BE_SINGLE_QUOTED | FYTTAF_CAN_BE_LITERAL | FYTTAF_CAN_BE_FOLDED)

static const struct atom_analysis_case bad_utf8_cases[] = {
	{
		/* the line break after the bad byte is still seen */
		.name = "invalid_mid",
		.data = "ab\x89 c\nd", .size = 7,
		.set_flags = FYTTAF_HAS_INVALID_UTF8 | FYTTAF_CAN_BE_DOUBLE_QUOTED |
			     FYTTAF_JSON_ESCAPE | FYTTAF_HAS_LB | FYTTAF_HAS_WS,
		.clear_flags = FYTTAF_HAS_PARTIAL_UTF8 | BAD_UTF8_CLEAR,
	}, {
		.name = "zero_after_invalid",
		.data = "\x89\0", .size = 2,
		.set_flags = FYTTAF_HAS_INVALID_UTF8 | FYTTAF_HAS_ZERO,
		.clear_flags = FYTTAF_HAS_PARTIAL_UTF8 | BAD_UTF8_CLEAR,
	}, {
		/* U+20AC cut short */
		.name = "partial_end",
		.data = "ab\xe2\x82", .size = 4,
		.set_flags = FYTTAF_HAS_PARTIAL_UTF8 | FYTTAF_CAN_BE_DOUBLE_QUOTED,
		.clear_flags = FYTTAF_HAS_INVALID_UTF8 | BAD_UTF8_CLEAR,
	}, {
		.name = "partial_lead_only",
		.data = "ab\xf0", .size = 3,
		.set_flags = FYTTAF_HAS_PARTIAL_UTF8,
		.clear_flags = FYTTAF_HAS_INVALID_UTF8 | BAD_UTF8_CLEAR,
	}, {
		.name = "partial_4byte",
		.data = "\xf0\x9f\x98", .size = 3,
		.set_flags = FYTTAF_HAS_PARTIAL_UTF8,
		.clear_flags = FYTTAF_HAS_INVALID_UTF8 | BAD_UTF8_CLEAR,
	}, {
		/* a lead byte followed by ascii is not partial */
		.name = "lead_then_ascii",
		.data = "\xe2\x41", .size = 2,
		.set_flags = FYTTAF_HAS_INVALID_UTF8,
		.clear_flags = FYTTAF_HAS_PARTIAL_UTF8 | BAD_UTF8_CLEAR,
	}, {
		/* a stray continuation at the end is not partial either */
		.name = "continuation_at_end",
		.data = "a\x80", .size = 2,
		.set_flags = FYTTAF_HAS_INVALID_UTF8,
		.clear_flags = FYTTAF_HAS_PARTIAL_UTF8 | BAD_UTF8_CLEAR,
	}, {
		/* U+D800 encoded; never a valid start, even cut short */
		.name = "encoded_surrogate",
		.data = "\xed\xa0\x80", .size = 3,
		.set_flags = FYTTAF_HAS_INVALID_UTF8,
		.clear_flags = FYTTAF_HAS_PARTIAL_UTF8 | BAD_UTF8_CLEAR,
	}, {
		.name = "encoded_surrogate_cut",
		.data = "\xed\xa0", .size = 2,
		.set_flags = FYTTAF_HAS_INVALID_UTF8,
		.clear_flags = FYTTAF_HAS_PARTIAL_UTF8 | BAD_UTF8_CLEAR,
	}, {
		.name = "overlong",
		.data = "\xc0\x80", .size = 2,
		.set_flags = FYTTAF_HAS_INVALID_UTF8,
		.clear_flags = FYTTAF_HAS_PARTIAL_UTF8 | BAD_UTF8_CLEAR,
	}, {
		.name = "overlong_cut",
		.data = "\xe0\x80", .size = 2,
		.set_flags = FYTTAF_HAS_INVALID_UTF8,
		.clear_flags = FYTTAF_HAS_PARTIAL_UTF8 | BAD_UTF8_CLEAR,
	}, {
		/* above U+10FFFF */
		.name = "out_of_range_cut",
		.data = "\xf4\x90", .size = 2,
		.set_flags = FYTTAF_HAS_INVALID_UTF8,
		.clear_flags = FYTTAF_HAS_PARTIAL_UTF8 | BAD_UTF8_CLEAR,
	}, {
		.name = "invalid_and_partial",
		.data = "\x89" "a\xe2\x82", .size = 4,
		.set_flags = FYTTAF_HAS_INVALID_UTF8 | FYTTAF_HAS_PARTIAL_UTF8,
		.clear_flags = BAD_UTF8_CLEAR,
	}, {
		.name = "valid_utf8",
		.data = "\xc2\xa9\xe2\x82\xac", .size = 5,
		.set_flags = FYTTAF_HIGH_ASCII,
		.clear_flags = FYTTAF_HAS_INVALID_UTF8 | FYTTAF_HAS_PARTIAL_UTF8,
	},
};

START_TEST(atom_analysis_bad_utf8)
{
	struct atom_analysis_case tc;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(bad_utf8_cases); i++) {
		tc = bad_utf8_cases[i];
		tc.style = FYSS_ANY;
		tc.lb_mode = fylb_cr_nl;
		tc.preferred_style = (tc.set_flags & (FYTTAF_HAS_INVALID_UTF8 |
						      FYTTAF_HAS_PARTIAL_UTF8)) ?
				     FYSS_DOUBLE_QUOTED : FYSS_ANY;
		check_atom_analysis_case(&tc);
		check_atom_analysis_case_atom(&tc);
	}
}
END_TEST

/* every high byte alone: a lead byte that could start a sequence is partial */
START_TEST(atom_analysis_bad_utf8_every_byte)
{
	struct atom_analysis_case tc;
	char name[32];
	unsigned char b[3];
	unsigned int i;
	bool lead;

	for (i = 0x80; i <= 0xff; i++) {
		b[0] = 'a';
		b[1] = (unsigned char)i;
		b[2] = 'b';
		lead = i >= 0xc2 && i <= 0xf4;

		/* in the middle it is always invalid */
		snprintf(name, sizeof(name), "mid_%02x", i);
		memset(&tc, 0, sizeof(tc));
		tc.name = name;
		tc.data = b;
		tc.size = 3;
		tc.style = FYSS_ANY;
		tc.lb_mode = fylb_cr_nl;
		tc.set_flags = FYTTAF_HAS_INVALID_UTF8 | FYTTAF_CAN_BE_DOUBLE_QUOTED;
		tc.clear_flags = FYTTAF_HAS_PARTIAL_UTF8 | BAD_UTF8_CLEAR;
		tc.preferred_style = FYSS_DOUBLE_QUOTED;
		check_atom_analysis_case(&tc);
		check_atom_analysis_case_atom(&tc);

		/* at the end it is partial only if it can start a sequence */
		snprintf(name, sizeof(name), "end_%02x", i);
		tc.size = 2;
		tc.set_flags = lead ? FYTTAF_HAS_PARTIAL_UTF8 : FYTTAF_HAS_INVALID_UTF8;
		tc.clear_flags = (lead ? FYTTAF_HAS_INVALID_UTF8 : FYTTAF_HAS_PARTIAL_UTF8) |
				 BAD_UTF8_CLEAR;
		check_atom_analysis_case(&tc);
		check_atom_analysis_case_atom(&tc);
	}
}
END_TEST

START_TEST(atom_analysis_utf8_get_bytes)
{
	static const struct {
		const char *data;
		size_t size;
		int c, w, err;
	} cases[] = {
		{ "", 0, FYUG_EOF, 0, 0 },
		{ "a", 1, 'a', 1, 0 },
		{ "\xc2\xa9", 2, 0xa9, 2, 0 },
		{ "\xf0\x9f\x98\x80", 4, 0x1f600, 4, 0 },
		{ "\x89", 1, 0xdc89, 1, FYUG_INV },
		{ "\xff", 1, 0xdcff, 1, FYUG_INV },
		{ "\x80", 1, 0xdc80, 1, FYUG_INV },
		{ "\xc0\x80", 2, 0xdcc0, 1, FYUG_INV },
		{ "\xed\xb2\x89", 3, 0xdced, 1, FYUG_INV },
		{ "\xe2\x82", 2, 0xdce2, 1, FYUG_PARTIAL },
		{ "\xe2\x41", 2, 0xdce2, 1, FYUG_INV },
	};
	size_t i;
	int c, w, err;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		w = -1;
		err = -1;
		c = fy_utf8_get_bytes(cases[i].data, cases[i].size, &w, &err);
		ck_assert_msg(c == cases[i].c, "case %zu: c=%x expected %x", i, c, cases[i].c);
		ck_assert_msg(err == cases[i].err, "case %zu: err=%d expected %d", i, err, cases[i].err);
		if (c >= 0)
			ck_assert_msg(w == cases[i].w, "case %zu: w=%d expected %d", i, w, cases[i].w);
	}

	for (i = 0; i < 0x100; i++) {
		c = fy_utf8_byte_mark((uint8_t)i);
		ck_assert_int_eq(fy_utf8_is_byte_mark(c), i >= 0x80);
		ck_assert_uint_eq(fy_utf8_byte_mark_value(c), i);
	}
}
END_TEST

/* every strict decoder says partial only for a real start that got cut off */
START_TEST(atom_analysis_utf8_get_partial)
{
	static const struct {
		const char *data;
		size_t size;
		int err;
	} cases[] = {
		{ "\xe2\x82", 2, FYUG_PARTIAL },
		{ "\xe2", 1, FYUG_PARTIAL },
		{ "\xc2", 1, FYUG_PARTIAL },
		{ "\xf0\x9f\x98", 3, FYUG_PARTIAL },
		{ "\xf4\x8f", 2, FYUG_PARTIAL },
		{ "\x89", 1, FYUG_INV },
		{ "\xff", 1, FYUG_INV },
		{ "\xc0", 1, FYUG_INV },
		{ "\xc1", 1, FYUG_INV },
		{ "\xf5", 1, FYUG_INV },
		{ "\xe2\x41", 2, FYUG_INV },
		{ "\xe0\x80", 2, FYUG_INV },
		{ "\xed\xa0", 2, FYUG_INV },
		{ "\xf0\x80", 2, FYUG_INV },
		{ "\xf4\x90", 2, FYUG_INV },
		{ "\xf0\x9f\x41", 3, FYUG_INV },
	};
	const char *d;
	size_t i, n;
	int w, err;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		d = cases[i].data;
		n = cases[i].size;
		err = cases[i].err;

		ck_assert_msg(fy_utf8_get(d, n, &w) == err, "get case %zu", i);
		ck_assert_msg(fy_utf8_get_branch(d, n, &w) == err, "branch case %zu", i);
		ck_assert_msg(fy_utf8_get_64(d, n) == err, "64 case %zu", i);
		ck_assert_msg(fy_utf8_get_table(d, n, &w) == err, "table case %zu", i);
		ck_assert_msg(fy_utf8_get_generic(d, n, &w) == err, "generic case %zu", i);
		ck_assert_msg(fy_utf8_get_generic_s(d, d + n, &w) == err, "generic_s case %zu", i);
	}
}
END_TEST

/* more than 1 in 4 bytes bad is likely binary; bytes, not characters, are counted */
START_TEST(atom_analysis_likely_binary)
{
	static const struct {
		const char *data;
		size_t size;
		bool binary;
	} cases[] = {
		{ "", 0, false },
		{ "abc", 3, false },
		{ "\xc2\xa9\xc2\xa9", 4, false },	/* valid utf8 is never binary */
		{ "\x89", 1, true },
		{ "ab\x89", 3, true },			/* 1 in 3 */
		{ "abc\x89", 4, false },		/* exactly 1 in 4 */
		{ "abcd\x89", 5, false },
		{ "\xc2\xa9\x89", 3, true },		/* 1 in 3 bytes, 1 in 2 characters */
		{ "\xe2\x82\xac\x89", 4, false },	/* 1 in 4 bytes, 1 in 2 characters */
		{ "\x89PNG\r\n\x1a\n", 8, false },
		{ "\x89\xff\x00\x01\xfe\x80", 6, true },
		{ "ab\xe2\x82", 4, true },		/* partial counts as bad bytes */
	};
	struct atom_analysis_case tc;
	char name[32];
	size_t i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		snprintf(name, sizeof(name), "likely_binary_%zu", i);
		memset(&tc, 0, sizeof(tc));
		tc.name = name;
		tc.data = cases[i].data;
		tc.size = cases[i].size;
		tc.style = FYSS_ANY;
		tc.lb_mode = fylb_cr_nl;
		tc.preferred_style = FYSS_ANY;
		if (cases[i].binary)
			tc.set_flags = FYTTAF_LIKELY_BINARY;
		else
			tc.clear_flags = FYTTAF_LIKELY_BINARY;
		check_atom_analysis_case(&tc);
		check_atom_analysis_case_atom(&tc);
	}
}
END_TEST

START_TEST(atom_analysis_flag_likely_binary)
{
	static const struct atom_analysis_case tc = {
		.name = "likely_binary",
		.data = "\x89\xff", .size = 2,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_LIKELY_BINARY,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_LIKELY_BINARY, "FYTTAF_LIKELY_BINARY");
}
END_TEST

START_TEST(atom_analysis_flag_has_invalid_utf8)
{
	static const struct atom_analysis_case tc = {
		.name = "has_invalid_utf8",
		.data = "\x89", .size = 1,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_INVALID_UTF8,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_INVALID_UTF8, "FYTTAF_HAS_INVALID_UTF8");
}
END_TEST

START_TEST(atom_analysis_flag_has_partial_utf8)
{
	static const struct atom_analysis_case tc = {
		.name = "has_partial_utf8",
		.data = "\xe2\x82", .size = 2,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_PARTIAL_UTF8,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_PARTIAL_UTF8, "FYTTAF_HAS_PARTIAL_UTF8");
}
END_TEST

START_TEST(atom_analysis_flag_has_lb)
{
	static const struct atom_analysis_case tc = {
		.name = "has_lb",
		.data = "\n",
		.size = 1,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_LB,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_LB, "FYTTAF_HAS_LB");
}
END_TEST

START_TEST(atom_analysis_flag_has_ws)
{
	static const struct atom_analysis_case tc = {
		.name = "has_ws",
		.data = " ",
		.size = 1,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_WS,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_WS, "FYTTAF_HAS_WS");
}
END_TEST

START_TEST(atom_analysis_flag_has_consecutive_lb)
{
	static const struct atom_analysis_case tc = {
		.name = "has_consecutive_lb",
		.data = "\n\n",
		.size = 2,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_CONSECUTIVE_LB,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_CONSECUTIVE_LB,
				      "FYTTAF_HAS_CONSECUTIVE_LB");
}
END_TEST

START_TEST(atom_analysis_flag_has_start_lb)
{
	static const struct atom_analysis_case tc = {
		.name = "has_start_lb",
		.data = "\n",
		.size = 1,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_START_LB,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_START_LB, "FYTTAF_HAS_START_LB");
}
END_TEST

START_TEST(atom_analysis_flag_has_consecutive_ws)
{
	static const struct atom_analysis_case tc = {
		.name = "has_consecutive_ws",
		.data = "  ",
		.size = 2,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_CONSECUTIVE_WS,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_CONSECUTIVE_WS,
				      "FYTTAF_HAS_CONSECUTIVE_WS");
}
END_TEST

START_TEST(atom_analysis_flag_empty)
{
	static const struct atom_analysis_case tc = {
		.name = "empty",
		.data = "",
		.size = 0,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_EMPTY,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_EMPTY, "FYTTAF_EMPTY");
}
END_TEST

START_TEST(atom_analysis_flag_can_be_simple_key)
{
	static const struct atom_analysis_case tc = {
		.name = "can_be_simple_key",
		.data = "abc123",
		.size = 6,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_CAN_BE_SIMPLE_KEY,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_CAN_BE_SIMPLE_KEY,
				      "FYTTAF_CAN_BE_SIMPLE_KEY");
}
END_TEST

START_TEST(atom_analysis_flag_direct_output)
{
	static const struct atom_analysis_case tc = {
		.name = "direct_output",
		.data = "abc123",
		.size = 6,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_DIRECT_OUTPUT,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_DIRECT_OUTPUT, "FYTTAF_DIRECT_OUTPUT");
}
END_TEST

START_TEST(atom_analysis_flag_text_token)
{
	static const struct atom_analysis_case tc = {
		.name = "text_token",
		.data = "abc123",
		.size = 6,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_TEXT_TOKEN,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_TEXT_TOKEN, "FYTTAF_TEXT_TOKEN");
}
END_TEST

START_TEST(atom_analysis_flag_can_be_plain)
{
	static const struct atom_analysis_case tc = {
		.name = "can_be_plain",
		.data = "abc123",
		.size = 6,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_CAN_BE_PLAIN,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_CAN_BE_PLAIN, "FYTTAF_CAN_BE_PLAIN");
}
END_TEST

START_TEST(atom_analysis_flag_can_be_single_quoted)
{
	static const struct atom_analysis_case tc = {
		.name = "can_be_single_quoted",
		.data = "abc123",
		.size = 6,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_CAN_BE_SINGLE_QUOTED,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_CAN_BE_SINGLE_QUOTED,
				      "FYTTAF_CAN_BE_SINGLE_QUOTED");
}
END_TEST

START_TEST(atom_analysis_flag_can_be_double_quoted)
{
	static const struct atom_analysis_case tc = {
		.name = "can_be_double_quoted",
		.data = "abc123",
		.size = 6,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_CAN_BE_DOUBLE_QUOTED,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_CAN_BE_DOUBLE_QUOTED,
				      "FYTTAF_CAN_BE_DOUBLE_QUOTED");
}
END_TEST

START_TEST(atom_analysis_flag_can_be_literal)
{
	static const struct atom_analysis_case tc = {
		.name = "can_be_literal",
		.data = "abc123",
		.size = 6,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_CAN_BE_LITERAL,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_CAN_BE_LITERAL, "FYTTAF_CAN_BE_LITERAL");
}
END_TEST

START_TEST(atom_analysis_flag_can_be_folded)
{
	static const struct atom_analysis_case tc = {
		.name = "can_be_folded",
		.data = "abc123",
		.size = 6,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_CAN_BE_FOLDED,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_CAN_BE_FOLDED, "FYTTAF_CAN_BE_FOLDED");
}
END_TEST

START_TEST(atom_analysis_flag_can_be_plain_flow)
{
	static const struct atom_analysis_case tc = {
		.name = "can_be_plain_flow",
		.data = "abc123",
		.size = 6,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_CAN_BE_PLAIN_FLOW,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_CAN_BE_PLAIN_FLOW,
				      "FYTTAF_CAN_BE_PLAIN_FLOW");
}
END_TEST

START_TEST(atom_analysis_flag_quote_at_0)
{
	static const struct atom_analysis_case tc = {
		.name = "quote_at_0",
		.data = "---\n",
		.size = 4,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_QUOTE_AT_0,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_QUOTE_AT_0, "FYTTAF_QUOTE_AT_0");
}
END_TEST

START_TEST(atom_analysis_flag_can_be_unquoted_path_key)
{
	static const struct atom_analysis_case tc = {
		.name = "can_be_unquoted_path_key",
		.data = "abc123",
		.size = 6,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_CAN_BE_UNQUOTED_PATH_KEY,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_CAN_BE_UNQUOTED_PATH_KEY,
				      "FYTTAF_CAN_BE_UNQUOTED_PATH_KEY");
}
END_TEST

START_TEST(atom_analysis_flag_has_any_lb)
{
	static const struct atom_analysis_case tc = {
		.name = "has_any_lb",
		.data = "\n",
		.size = 1,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_ANY_LB,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_ANY_LB, "FYTTAF_HAS_ANY_LB");
}
END_TEST

START_TEST(atom_analysis_flag_has_start_ind)
{
	static const struct atom_analysis_case tc = {
		.name = "has_start_ind",
		.data = "---\n",
		.size = 4,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_START_IND,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_START_IND, "FYTTAF_HAS_START_IND");
}
END_TEST

START_TEST(atom_analysis_flag_has_end_ind)
{
	static const struct atom_analysis_case tc = {
		.name = "has_end_ind",
		.data = "...\n",
		.size = 4,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_END_IND,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_END_IND, "FYTTAF_HAS_END_IND");
}
END_TEST

START_TEST(atom_analysis_flag_has_non_print)
{
	static const struct atom_analysis_case tc = {
		.name = "has_non_print",
		.data = zero_byte,
		.size = sizeof(zero_byte),
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_NON_PRINT,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_NON_PRINT, "FYTTAF_HAS_NON_PRINT");
}
END_TEST

START_TEST(atom_analysis_flag_ends_with_colon)
{
	static const struct atom_analysis_case tc = {
		.name = "ends_with_colon",
		.data = "a:",
		.size = 2,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_ENDS_WITH_COLON,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_ENDS_WITH_COLON, "FYTTAF_ENDS_WITH_COLON");
}
END_TEST

START_TEST(atom_analysis_flag_all_ws_lb)
{
	static const struct atom_analysis_case tc = {
		.name = "all_ws_lb",
		.data = " ",
		.size = 1,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_ALL_WS_LB,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_ALL_WS_LB, "FYTTAF_ALL_WS_LB");
}
END_TEST

START_TEST(atom_analysis_flag_all_print_ascii)
{
	static const struct atom_analysis_case tc = {
		.name = "all_print_ascii",
		.data = "abc123",
		.size = 6,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_ALL_PRINT_ASCII,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_ALL_PRINT_ASCII, "FYTTAF_ALL_PRINT_ASCII");
}
END_TEST

START_TEST(atom_analysis_flag_has_start_ws)
{
	static const struct atom_analysis_case tc = {
		.name = "has_start_ws",
		.data = " ",
		.size = 1,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_START_WS,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_START_WS, "FYTTAF_HAS_START_WS");
}
END_TEST

START_TEST(atom_analysis_flag_size0)
{
	static const struct atom_analysis_case tc = {
		.name = "size0",
		.data = "",
		.size = 0,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_SIZE0,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_SIZE0, "FYTTAF_SIZE0");
}
END_TEST

START_TEST(atom_analysis_flag_has_zero)
{
	static const struct atom_analysis_case tc = {
		.name = "has_zero",
		.data = zero_byte,
		.size = sizeof(zero_byte),
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_ZERO,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_ZERO, "FYTTAF_HAS_ZERO");
}
END_TEST

START_TEST(atom_analysis_flag_has_non_nl_lb)
{
	static const struct atom_analysis_case tc = {
		.name = "has_non_nl_lb",
		.data = cr_byte,
		.size = sizeof(cr_byte),
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_NON_NL_LB,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_NON_NL_LB,
				      "FYTTAF_HAS_NON_NL_LB");
}
END_TEST

START_TEST(atom_analysis_flag_has_end_ws)
{
	static const struct atom_analysis_case tc = {
		.name = "has_end_ws",
		.data = " ",
		.size = 1,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_END_WS,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_END_WS, "FYTTAF_HAS_END_WS");
}
END_TEST

START_TEST(atom_analysis_flag_has_end_lb)
{
	static const struct atom_analysis_case tc = {
		.name = "has_end_lb",
		.data = "\n",
		.size = 1,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_END_LB,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_END_LB, "FYTTAF_HAS_END_LB");
}
END_TEST

START_TEST(atom_analysis_flag_has_trailing_lb)
{
	static const struct atom_analysis_case tc = {
		.name = "has_trailing_lb",
		.data = "\n\n",
		.size = 2,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HAS_TRAILING_LB,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HAS_TRAILING_LB,
				      "FYTTAF_HAS_TRAILING_LB");
}
END_TEST

START_TEST(atom_analysis_flag_valid_anchor)
{
	static const struct atom_analysis_case tc = {
		.name = "valid_anchor",
		.data = "abc123",
		.size = 6,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_VALID_ANCHOR,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_VALID_ANCHOR, "FYTTAF_VALID_ANCHOR");
}
END_TEST

START_TEST(atom_analysis_flag_json_escape)
{
	static const struct atom_analysis_case tc = {
		.name = "json_escape",
		.data = zero_byte,
		.size = sizeof(zero_byte),
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_JSON_ESCAPE,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_JSON_ESCAPE, "FYTTAF_JSON_ESCAPE");
}
END_TEST

START_TEST(atom_analysis_flag_high_ascii)
{
	static const struct atom_analysis_case tc = {
		.name = "high_ascii",
		.data = high_ascii_at_end_utf8,
		.size = sizeof(high_ascii_at_end_utf8),
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_HIGH_ASCII,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_HIGH_ASCII, "FYTTAF_HIGH_ASCII");
}
END_TEST

START_TEST(atom_analysis_flag_analyzed)
{
	static const struct atom_analysis_case tc = {
		.name = "analyzed",
		.data = "abc123",
		.size = 6,
		.style = FYSS_PLAIN,
		.lb_mode = fylb_cr_nl,
		.set_flags = FYTTAF_ANALYZED,
		.clear_flags = 0,
		.preferred_style = FYSS_ANY,
	};

	check_atom_analysis_flag_case(&tc, FYTTAF_ANALYZED, "FYTTAF_ANALYZED");
}
END_TEST

void libfyaml_case_atom_analysis(struct fy_check_suite *cs)
{
	struct fy_check_testcase *ctc;

	ctc = fy_check_suite_add_test_case(cs, "atom-analysis");
	fy_check_testcase_add_test(ctc, atom_analysis_empty);
	fy_check_testcase_add_test(ctc, atom_analysis_plain_ascii);
	fy_check_testcase_add_test(ctc, atom_analysis_single_space);
	fy_check_testcase_add_test(ctc, atom_analysis_double_space);
	fy_check_testcase_add_test(ctc, atom_analysis_tail_double_space);
	fy_check_testcase_add_test(ctc, atom_analysis_single_newline);
	fy_check_testcase_add_test(ctc, atom_analysis_double_newline);
	fy_check_testcase_add_test(ctc, atom_analysis_tail_double_newline);
	fy_check_testcase_add_test(ctc, atom_analysis_carriage_return);
	fy_check_testcase_add_test(ctc, atom_analysis_hash_comment_plain_block);
	fy_check_testcase_add_test(ctc, atom_analysis_flow_comma_space);
	fy_check_testcase_add_test(ctc, atom_analysis_ends_with_colon);
	fy_check_testcase_add_test(ctc, atom_analysis_colon_before_flow_indicator);
	fy_check_testcase_add_test(ctc, atom_analysis_start_indicator);
	fy_check_testcase_add_test(ctc, atom_analysis_end_indicator);
	fy_check_testcase_add_test(ctc, atom_analysis_zero_byte);
	fy_check_testcase_add_test(ctc, atom_analysis_high_ascii);
	fy_check_testcase_add_test(ctc, atom_analysis_bad_utf8);
	fy_check_testcase_add_test(ctc, atom_analysis_bad_utf8_every_byte);
	fy_check_testcase_add_test(ctc, atom_analysis_utf8_get_bytes);
	fy_check_testcase_add_test(ctc, atom_analysis_utf8_get_partial);
	fy_check_testcase_add_test(ctc, atom_analysis_likely_binary);

	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_lb);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_ws);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_consecutive_lb);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_start_lb);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_consecutive_ws);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_empty);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_can_be_simple_key);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_direct_output);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_text_token);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_can_be_plain);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_can_be_single_quoted);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_can_be_double_quoted);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_can_be_literal);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_can_be_folded);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_can_be_plain_flow);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_quote_at_0);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_can_be_unquoted_path_key);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_any_lb);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_start_ind);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_end_ind);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_non_print);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_ends_with_colon);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_all_ws_lb);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_all_print_ascii);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_start_ws);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_size0);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_zero);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_non_nl_lb);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_end_ws);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_end_lb);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_trailing_lb);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_valid_anchor);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_json_escape);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_high_ascii);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_invalid_utf8);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_has_partial_utf8);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_likely_binary);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_analyzed);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_no_text_token);
	fy_check_testcase_add_test(ctc, atom_analysis_coverage);
}
