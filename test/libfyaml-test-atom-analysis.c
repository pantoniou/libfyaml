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
	int rc;

	memset(&ta, 0, sizeof(ta));
	rc = fy_analyze_scalar_content(tc->data, tc->size, tc->style, tc->lb_mode, &ta);
	ck_assert_msg(rc == 0, "%s: fy_analyze_scalar_content() rc=%d", tc->name, rc);

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
	int rc;

	memset(&ta, 0, sizeof(ta));
	rc = fy_analyze_scalar_content(tc->data, tc->size, tc->style, tc->lb_mode, &ta);
	ck_assert_msg(rc == 0, "%s/%s: fy_analyze_scalar_content() rc=%d",
		      flag_name, tc->name, rc);

	format_flags(ta.flags, got_buf, sizeof(got_buf));
	ck_assert_msg(ta.flags & flag,
		      "%s: expected set in %s\nhave=0x%016" PRIx64 " (%s)",
		      flag_name, tc->name, ta.flags, got_buf);
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
	fy_check_testcase_add_test(ctc, atom_analysis_flag_analyzed);
	fy_check_testcase_add_test(ctc, atom_analysis_flag_no_text_token);
	fy_check_testcase_add_test(ctc, atom_analysis_coverage);
}
