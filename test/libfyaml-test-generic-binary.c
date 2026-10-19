/*
 * libfyaml-test-generic-binary.c - strings that are not UTF-8 round trip
 *
 * A byte that isn't valid UTF-8 is written as a lone surrogate escape,
 * e.g. 0x89 is "\uDC89", and is read back as the byte 0x89.
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
#include <stdint.h>

#include <check.h>

#include <libfyaml.h>

#include "fy-generic.h"

#include "fy-check.h"

static const struct {
	const char *name;
	enum fy_op_emit_flags emit;
	enum fy_op_parse_flags parse;
} binary_modes[] = {
	{ "yaml-1.1",		FYOPEF_MODE_YAML_1_1 | FYOPEF_STYLE_BLOCK,	FYOPPF_MODE_YAML_1_1 },
	{ "yaml-1.1-flow",	FYOPEF_MODE_YAML_1_1 | FYOPEF_STYLE_FLOW,	FYOPPF_MODE_YAML_1_1 },
	{ "yaml-1.2",		FYOPEF_MODE_YAML_1_2 | FYOPEF_STYLE_BLOCK,	FYOPPF_MODE_YAML_1_2 },
	{ "yaml-1.2-flow",	FYOPEF_MODE_YAML_1_2 | FYOPEF_STYLE_FLOW,	FYOPPF_MODE_YAML_1_2 },
	{ "yaml-1.2-80",	FYOPEF_MODE_YAML_1_2 | FYOPEF_WIDTH_80,	FYOPPF_MODE_YAML_1_2 },
	{ "json",		FYOPEF_MODE_JSON,				FYOPPF_MODE_JSON },
	{ "json-oneline",	FYOPEF_MODE_JSON | FYOPEF_STYLE_ONELINE,	FYOPPF_MODE_JSON },
};

/* emit v in every mode, direct and through events, and parse it back */
static void binary_check_roundtrip(struct fy_generic_builder *gb, const char *name, fy_generic v)
{
	enum fy_op_emit_flags emit;
	fy_generic_sized_string text;
	fy_generic emitted, parsed;
	size_t i, ev;

	for (i = 0; i < sizeof(binary_modes) / sizeof(binary_modes[0]); i++) {
		for (ev = 0; ev < 2; ev++) {
			emit = binary_modes[i].emit | FYOPEF_DISABLE_DIRECTORY;
			if (ev)
				emit |= FYOPEF_EMIT_EVENTS;

			emitted = fy_emit(gb, v, emit, NULL);
			ck_assert_msg(fy_is_string(emitted), "%s/%s%s: emit failed",
				      name, binary_modes[i].name, ev ? "/events" : "");
			text = fy_castp(&emitted, fy_szstr_empty);

			parsed = fy_parse(gb, text, binary_modes[i].parse | FYOPPF_DISABLE_DIRECTORY, NULL);
			ck_assert_msg(fy_generic_is_valid(parsed), "%s/%s%s: parse failed:\n%.*s",
				      name, binary_modes[i].name, ev ? "/events" : "",
				      (int)text.size, text.data);
			ck_assert_msg(fy_generic_compare(v, parsed) == 0,
				      "%s/%s%s: changed on the round trip:\n%.*s",
				      name, binary_modes[i].name, ev ? "/events" : "",
				      (int)text.size, text.data);
		}
	}
}

/* as a scalar root, as a value, in a sequence and as a mapping key */
static void binary_check_bytes(struct fy_generic_builder *gb, const char *name,
			       const void *data, size_t size)
{
	fy_generic_sized_string bytes = { data, size }, back;
	fy_generic v;

	v = fy_value(gb, bytes);
	ck_assert(fy_is_string(v));
	back = fy_castp(&v, fy_szstr_empty);
	ck_assert_msg(back.size == size && !memcmp(back.data, data, size),
		      "%s: changed in the arena", name);

	binary_check_roundtrip(gb, name, v);
	binary_check_roundtrip(gb, name, fy_mapping(gb, "data", v));
	binary_check_roundtrip(gb, name, fy_sequence(gb, v, "text", v));
	binary_check_roundtrip(gb, name, fy_mapping(gb, v, v, "other", "x"));
}

START_TEST(binary_every_byte)
{
	struct fy_generic_builder *gb;
	unsigned char b[3];
	char name[32];
	unsigned int i;

	gb = fy_generic_builder_create(NULL);
	ck_assert_ptr_nonnull(gb);

	for (i = 0; i < 0x100; i++) {
		b[0] = 'a';
		b[1] = (unsigned char)i;
		b[2] = 'z';
		snprintf(name, sizeof(name), "mid_%02x", i);
		binary_check_bytes(gb, name, b, 3);
		/*
		 * alone only for the high bytes; a lone "0" or "~" is emitted
		 * plain and reads back as an int or null, a separate problem
		 */
		if (i >= 0x80) {
			snprintf(name, sizeof(name), "alone_%02x", i);
			binary_check_bytes(gb, name, b + 1, 1);
		}
		snprintf(name, sizeof(name), "end_%02x", i);
		binary_check_bytes(gb, name, b, 2);
	}

	fy_generic_builder_destroy(gb);
}
END_TEST

START_TEST(binary_cases)
{
	static const struct {
		const char *name;
		const char *data;
		size_t size;
	} cases[] = {
		{ "empty", "", 0 },
		{ "png_header", "\x89PNG\r\n\x1a\n", 8 },
		{ "nul_and_bad", "\0\x01\x89\xffPNG", 7 },
		{ "partial_2", "ab\xc2", 3 },
		{ "partial_3", "ab\xe2\x82", 4 },
		{ "partial_4", "ab\xf0\x9f\x98", 5 },
		{ "lead_then_ascii", "\xe2" "A", 2 },
		{ "overlong", "\xc0\xaf", 2 },
		{ "overlong_3", "\xe0\x80\xaf", 3 },
		{ "surrogate_d800", "\xed\xa0\x80", 3 },
		{ "byte_mark_utf8", "\xed\xb2\x89", 3 },	/* U+DC89 as UTF-8 is 3 bad bytes */
		{ "out_of_range", "\xf4\x90\x80\x80", 4 },
		{ "pair_then_byte", "\xf0\x9f\x98\x80\x89", 5 },
		{ "valid_utf8", "\xc2\xa9 \xe2\x82\xac \xf0\x9f\x98\x80", 11 },
		{ "spaces_bad", "  \x89  \x89\n\n\x89 ", 11 },
		{ "lb_bad", "\x89\n", 2 },
		{ "looks_like_escape", "\\uDC89", 6 },	/* text, not a byte */
		{ "quote_bad", "\"\x89'", 3 },
	};
	struct fy_generic_builder *gb;
	size_t i;

	gb = fy_generic_builder_create(NULL);
	ck_assert_ptr_nonnull(gb);

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
		binary_check_bytes(gb, cases[i].name, cases[i].data, cases[i].size);

	fy_generic_builder_destroy(gb);
}
END_TEST

/* all 256 bytes in one string, and long random runs that get line wrapped */
START_TEST(binary_blobs)
{
	struct fy_generic_builder *gb;
	unsigned char *buf;
	uint32_t seed;
	size_t i, sz;
	char name[32];

	gb = fy_generic_builder_create(NULL);
	ck_assert_ptr_nonnull(gb);

	buf = malloc(8192);
	ck_assert_ptr_nonnull(buf);

	for (i = 0; i < 256; i++)
		buf[i] = (unsigned char)i;
	binary_check_bytes(gb, "all_bytes", buf, 256);
	for (i = 0; i < 256; i++)
		buf[i] = (unsigned char)(255 - i);
	binary_check_bytes(gb, "all_bytes_reversed", buf, 256);

	seed = 0x12345678;
	for (sz = 1; sz <= 8192; sz *= 4) {
		for (i = 0; i < sz; i++) {
			seed = seed * 1103515245 + 12345;
			buf[i] = (unsigned char)(seed >> 16);
		}
		snprintf(name, sizeof(name), "random_%zu", sz);
		binary_check_bytes(gb, name, buf, sz);
	}

	free(buf);
	fy_generic_builder_destroy(gb);
}
END_TEST

/* only \uDC80-\uDCFF alone is a byte; other surrogates and raw bad bytes fail */
START_TEST(binary_parse)
{
	static const struct {
		const char *text;
		enum fy_op_parse_flags parse;
		const char *data;	/* NULL: must fail */
		size_t size;
	} cases[] = {
		{ "\"\\uDC89\"", FYOPPF_MODE_YAML_1_2, "\x89", 1 },
		{ "\"\\uDC80\\uDCFF\"", FYOPPF_MODE_YAML_1_2, "\x80\xff", 2 },
		{ "\"\\udc89\"", FYOPPF_MODE_YAML_1_1, "\x89", 1 },
		{ "\"\\uDC89\"", FYOPPF_MODE_JSON, "\x89", 1 },
		{ "\"a\\uDC89b\"", FYOPPF_MODE_JSON, "a\x89" "b", 3 },
		/* a high surrogate before it makes a pair: U+1F489 */
		{ "\"\\uD83D\\uDC89\"", FYOPPF_MODE_YAML_1_2, "\xf0\x9f\x92\x89", 4 },
		{ "\"\\uD83D\\uDC89\"", FYOPPF_MODE_JSON, "\xf0\x9f\x92\x89", 4 },
		/* \x89 is U+0089, not a byte */
		{ "\"\\x89\"", FYOPPF_MODE_YAML_1_2, "\xc2\x89", 2 },
		{ "\"\\uDC7F\"", FYOPPF_MODE_YAML_1_2, NULL, 0 },
		{ "\"\\uDD00\"", FYOPPF_MODE_YAML_1_2, NULL, 0 },
		{ "\"\\uD800\"", FYOPPF_MODE_YAML_1_2, NULL, 0 },
		{ "\"\\uDBFF\"", FYOPPF_MODE_JSON, NULL, 0 },
		{ "\"\\uD800\\u0041\"", FYOPPF_MODE_YAML_1_2, NULL, 0 },
		{ "\"\\uDC7F\"", FYOPPF_MODE_JSON, NULL, 0 },
		{ "\"\\U0000DC89\"", FYOPPF_MODE_YAML_1_2, "\x89", 1 },
		{ "\"\\U0000D800\"", FYOPPF_MODE_YAML_1_2, NULL, 0 },
		/* bare bad bytes are never accepted */
		{ "\"\x89\"", FYOPPF_MODE_YAML_1_2, NULL, 0 },
		{ "a\x89", FYOPPF_MODE_YAML_1_2, NULL, 0 },
		{ "\"\x89\"", FYOPPF_MODE_JSON, NULL, 0 },
	};
	struct fy_generic_builder *gb;
	fy_generic_sized_string text, back;
	fy_generic parsed;
	size_t i;

	gb = fy_generic_builder_create(NULL);
	ck_assert_ptr_nonnull(gb);

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		text.data = cases[i].text;
		text.size = strlen(cases[i].text);
		parsed = fy_parse(gb, text, cases[i].parse | FYOPPF_DISABLE_DIRECTORY, NULL);
		if (!cases[i].data) {
			ck_assert_msg(!fy_generic_is_valid(parsed), "case %zu: %s parsed", i, cases[i].text);
			continue;
		}
		ck_assert_msg(fy_is_string(parsed), "case %zu: %s did not parse", i, cases[i].text);
		back = fy_castp(&parsed, fy_szstr_empty);
		ck_assert_msg(back.size == cases[i].size && !memcmp(back.data, cases[i].data, back.size),
			      "case %zu: %s gave %zu bytes", i, cases[i].text, back.size);
	}

	fy_generic_builder_destroy(gb);
}
END_TEST

void libfyaml_case_generic_binary(struct fy_check_suite *cs)
{
	struct fy_check_testcase *ctc;

	ctc = fy_check_suite_add_test_case(cs, "generic-binary");
	fy_check_testcase_set_timeout(ctc, 120);
	fy_check_testcase_add_test(ctc, binary_every_byte);
	fy_check_testcase_add_test(ctc, binary_cases);
	fy_check_testcase_add_test(ctc, binary_blobs);
	fy_check_testcase_add_test(ctc, binary_parse);
}
