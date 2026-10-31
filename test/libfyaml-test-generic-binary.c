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


#include <libfyaml.h>

#include "fy-generic.h"
#include "fy-utils.h"

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

	for (i = 0; i < ARRAY_SIZE(binary_modes); i++) {
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
		snprintf(name, sizeof(name), "alone_%02x", i);
		binary_check_bytes(gb, name, b + 1, 1);
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

	for (i = 0; i < ARRAY_SIZE(cases); i++)
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

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
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

/* RFC 4648 vectors, line wrapping, white space and bad input */
START_TEST(binary_base64_codec)
{
	static const struct {
		const char *data, *b64;
	} vec[] = {
		{ "", "" }, { "f", "Zg==" }, { "fo", "Zm8=" }, { "foo", "Zm9v" },
		{ "foob", "Zm9vYg==" }, { "fooba", "Zm9vYmE=" }, { "foobar", "Zm9vYmFy" },
	};
	static const char * const bad[] = {
		"Zg", "Zg=", "Z===", "=Zg=", "Zg==Zg==", "Zm9v!", "Zg=a", "Zm9vY", "====",
		"Zm9v\x89", "Zm-v", "Zm_v",
	};
	char out[512], back[512];
	unsigned char all[256];
	size_t i, n, sz, ll;
	ssize_t r;
	uint32_t seed;

	for (i = 0; i < ARRAY_SIZE(vec); i++) {
		sz = strlen(vec[i].data);
		ck_assert_uint_eq(fy_base64_encode_size(sz, 0), strlen(vec[i].b64));
		n = fy_base64_encode(out, vec[i].data, sz, 0);
		ck_assert_uint_eq(n, strlen(vec[i].b64));
		ck_assert(!memcmp(out, vec[i].b64, n));
		r = fy_base64_decode(back, sizeof(back), vec[i].b64, n);
		ck_assert_int_eq(r, (ssize_t)sz);
		ck_assert(!memcmp(back, vec[i].data, sz));
	}

	/* a newline ends every line and the last one */
	n = fy_base64_encode(out, "foobar", 6, 4);
	ck_assert_uint_eq(n, 10);
	ck_assert(!memcmp(out, "Zm9v\nYmFy\n", 10));
	ck_assert_uint_eq(fy_base64_encode_size(6, 4), 10);
	n = fy_base64_encode(out, "fooba", 5, 3);
	ck_assert(!memcmp(out, "Zm9\nvYm\nE=\n", n));
	ck_assert_uint_eq(fy_base64_encode_size(5, 3), n);
	ck_assert_uint_eq(fy_base64_encode_size(0, 4), 0);

	/* white space anywhere is skipped */
	r = fy_base64_decode(back, sizeof(back), " Zm 9v\r\nYm\tFy\n ", 15);
	ck_assert_int_eq(r, 6);
	ck_assert(!memcmp(back, "foobar", 6));
	r = fy_base64_decode(back, sizeof(back), "  \n", 3);
	ck_assert_int_eq(r, 0);

	for (i = 0; i < ARRAY_SIZE(bad); i++)
		ck_assert_msg(fy_base64_decode(back, sizeof(back), bad[i], strlen(bad[i])) < 0,
			      "\"%s\" decoded", bad[i]);

	/* too small an output is an error, not an overflow */
	ck_assert_int_lt(fy_base64_decode(back, 5, "Zm9vYmFy", 8), 0);
	ck_assert_int_eq(fy_base64_decode(back, 6, "Zm9vYmFy", 8), 6);
	ck_assert_int_lt(fy_base64_decode(back, 0, "Zg==", 4), 0);

	ck_assert_uint_eq(fy_base64_encode_size(SIZE_MAX, 0), (size_t)-1);
	ck_assert_uint_eq(fy_base64_encode_size(SIZE_MAX / 2, 1), (size_t)-1);

	/* every byte, and random runs with and without lines */
	for (i = 0; i < 256; i++)
		all[i] = (unsigned char)i;
	seed = 1;
	for (sz = 0; sz <= 256; sz++) {
		for (ll = 0; ll <= 8; ll += 4) {
			if (sz && sz % 7 == 0) {
				for (i = 0; i < sz; i++) {
					seed = seed * 1103515245 + 12345;
					all[i] = (unsigned char)(seed >> 16);
				}
			}
			n = fy_base64_encode(out, all, sz, ll ? ll + (sz % 3) : 0);
			ck_assert_uint_eq(n, fy_base64_encode_size(sz, ll ? ll + (sz % 3) : 0));
			ck_assert_uint_le(fy_base64_decode_max_size(n), sizeof(back));
			r = fy_base64_decode(back, fy_base64_decode_max_size(n), out, n);
			ck_assert_int_eq(r, (ssize_t)sz);
			ck_assert(!memcmp(back, all, sz));
		}
	}
}
END_TEST

static bool text_has(fy_generic_sized_string text, const char *what)
{
	size_t i, n = strlen(what);

	for (i = 0; i + n <= text.size; i++)
		if (!memcmp(text.data + i, what, n))
			return true;
	return false;
}

/* with FYOPEF_BINARY_BASE64 mostly-binary strings go out as !!binary */
static void binary_base64_check(struct fy_generic_builder *gb, const char *name, fy_generic v,
				bool expect_b64)
{
	fy_generic_sized_string text;
	fy_generic emitted, parsed;
	enum fy_op_emit_flags emit;
	size_t i, ev;
	bool json;

	for (i = 0; i < ARRAY_SIZE(binary_modes); i++) {
		for (ev = 0; ev < 2; ev++) {
			emit = binary_modes[i].emit | FYOPEF_DISABLE_DIRECTORY | FYOPEF_BINARY_BASE64;
			if (ev)
				emit |= FYOPEF_EMIT_EVENTS;
			json = binary_modes[i].parse == FYOPPF_MODE_JSON;

			emitted = fy_emit(gb, v, emit, NULL);
			ck_assert_msg(fy_is_string(emitted), "%s/%s: emit failed", name, binary_modes[i].name);
			text = fy_castp(&emitted, fy_szstr_empty);

			ck_assert_msg(text_has(text, "!!binary") == (expect_b64 && !json),
				      "%s/%s%s: !!binary %s expected:\n%.*s", name, binary_modes[i].name,
				      ev ? "/events" : "", expect_b64 && !json ? "" : "not",
				      (int)text.size, text.data);

			parsed = fy_parse(gb, text, binary_modes[i].parse | FYOPPF_DISABLE_DIRECTORY, NULL);
			ck_assert_msg(fy_generic_compare(v, parsed) == 0,
				      "%s/%s%s: changed on the round trip:\n%.*s",
				      name, binary_modes[i].name, ev ? "/events" : "",
				      (int)text.size, text.data);
		}
	}
}

START_TEST(binary_base64_emit)
{
	static const struct {
		const char *name;
		const char *data;
		size_t size;
		bool b64;
	} cases[] = {
		/* 7 of 22 bytes bad; a real png header alone is only 1 in 8 */
		{ "png", "\x89PNG\r\n\x1a\n\0\0\0\rIHDR\xff\xfe\x80\x81\x90\x91", 22, true },
		{ "bad_pair", "\x89\xff", 2, true },
		{ "one_in_three", "ab\x89", 3, true },
		{ "one_in_four", "abc\x89", 4, false },	/* not likely binary, escapes */
		{ "text", "hello", 5, false },
		{ "utf8", "\xc2\xa9\xe2\x82\xac", 5, false },
		{ "empty", "", 0, false },
	};
	struct fy_generic_builder *gb;
	fy_generic_sized_string bytes;
	unsigned char buf[3000];
	fy_generic v;
	uint32_t seed;
	size_t i;

	gb = fy_generic_builder_create(NULL);
	ck_assert_ptr_nonnull(gb);

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		bytes.data = cases[i].data;
		bytes.size = cases[i].size;
		v = fy_value(gb, bytes);
		binary_base64_check(gb, cases[i].name, v, cases[i].b64);
		binary_base64_check(gb, cases[i].name, fy_mapping(gb, "data", v), cases[i].b64);
		binary_base64_check(gb, cases[i].name, fy_sequence(gb, v, "x", v), cases[i].b64);
		binary_base64_check(gb, cases[i].name, fy_mapping(gb, v, "key"), cases[i].b64);
	}

	/* every byte once, and a large random blob */
	for (i = 0; i < 256; i++)
		buf[i] = (unsigned char)(255 - i);
	bytes.data = (const char *)buf;
	bytes.size = 256;
	binary_base64_check(gb, "all_bytes", fy_mapping(gb, "data", fy_value(gb, bytes)), true);

	seed = 7;
	for (i = 0; i < sizeof(buf); i++) {
		seed = seed * 1103515245 + 12345;
		buf[i] = (unsigned char)(seed >> 16);
	}
	bytes.size = sizeof(buf);
	v = fy_value(gb, bytes);
	binary_base64_check(gb, "random", fy_mapping(gb, "a", fy_mapping(gb, "b", fy_sequence(gb, v))), true);

	fy_generic_builder_destroy(gb);
}
END_TEST

/* base64 lines fit the width, however deep the value */
START_TEST(binary_base64_width)
{
	static const struct {
		enum fy_op_emit_flags flags;
		size_t max;
	} widths[] = {
		{ FYOPEF_WIDTH_80, 80 },
		{ FYOPEF_WIDTH_132, 132 },
		{ FYOPEF_WIDTH_80 | FYOPEF_INDENT_8, 80 },
		{ FYOPEF_WIDTH_80 | FYOPEF_INDENT_4, 80 },
		{ 0, 76 + 2 * 12 },	/* default: 76 wide lines, plus the indent */
	};
	struct fy_generic_builder *gb;
	fy_generic_sized_string bytes, text;
	unsigned char buf[600];
	fy_generic v, emitted, parsed;
	size_t i, j, w, maxw, ev;
	unsigned int depth;

	gb = fy_generic_builder_create(NULL);
	ck_assert_ptr_nonnull(gb);

	for (i = 0; i < sizeof(buf); i++)
		buf[i] = (unsigned char)(0x80 + i * 13);
	bytes.data = (const char *)buf;
	bytes.size = sizeof(buf);

	for (depth = 0; depth <= 12; depth++) {
		v = fy_value(gb, bytes);
		for (j = 0; j < depth; j++)
			v = (j & 1) ? fy_sequence(gb, v) : fy_mapping(gb, "k", v);

		for (i = 0; i < ARRAY_SIZE(widths); i++) {
			for (ev = 0; ev < 2; ev++) {
				emitted = fy_emit(gb, v, widths[i].flags | FYOPEF_STYLE_BLOCK |
						  FYOPEF_DISABLE_DIRECTORY | FYOPEF_BINARY_BASE64 |
						  (ev ? FYOPEF_EMIT_EVENTS : 0), NULL);
				ck_assert(fy_is_string(emitted));
				text = fy_castp(&emitted, fy_szstr_empty);

				maxw = w = 0;
				for (j = 0; j < text.size; j++) {
					if (text.data[j] == '\n') {
						if (w > maxw)
							maxw = w;
						w = 0;
					} else
						w++;
				}
				ck_assert_msg(maxw <= widths[i].max,
					      "depth %u width %zu%s: a line of %zu:\n%.*s",
					      depth, widths[i].max, ev ? " (events)" : "", maxw,
					      (int)text.size, text.data);

				parsed = fy_parse(gb, text, FYOPPF_DISABLE_DIRECTORY, NULL);
				ck_assert(fy_generic_compare(v, parsed) == 0);
			}
		}
	}

	/* infinite width is one line */
	emitted = fy_emit(gb, fy_mapping(gb, "k", fy_value(gb, bytes)),
			  FYOPEF_WIDTH_INF | FYOPEF_DISABLE_DIRECTORY | FYOPEF_BINARY_BASE64, NULL);
	text = fy_castp(&emitted, fy_szstr_empty);
	ck_assert_uint_eq(text.size, strlen("k: !!binary |\n  ") + 800 + 1);

	fy_generic_builder_destroy(gb);
}
END_TEST

/* a string with its own tag can't also be !!binary; it keeps the escapes */
START_TEST(binary_base64_user_tag)
{
	struct fy_generic_builder *gb;
	fy_generic_sized_string bytes = { "\x89\xff\x80", 3 }, text;
	fy_generic_indirect gi;
	fy_generic v, emitted, parsed;

	gb = fy_generic_builder_create(NULL);
	ck_assert_ptr_nonnull(gb);

	fy_generic_indirect_reset(&gi);
	gi.flags = FYGIF_VALUE | FYGIF_TAG;
	gi.value = fy_value(gb, bytes);
	gi.tag = fy_value(gb, "!foo");
	v = fy_gb_indirect_create(gb, &gi);
	ck_assert(fy_generic_is_valid(v));

	emitted = fy_emit(gb, fy_mapping(gb, "k", v),
			  FYOPEF_DISABLE_DIRECTORY | FYOPEF_BINARY_BASE64, NULL);
	text = fy_castp(&emitted, fy_szstr_empty);
	ck_assert_msg(text_has(text, "!foo") && text_has(text, "\\uDC89") && !text_has(text, "!!binary"),
		      "unexpected:\n%.*s", (int)text.size, text.data);

	parsed = fy_parse(gb, text, FYOPPF_DISABLE_DIRECTORY, NULL);
	ck_assert(fy_generic_compare(fy_mapping(gb, "k", fy_value(gb, bytes)), parsed) == 0);

	fy_generic_builder_destroy(gb);
}
END_TEST

/* !!binary decodes in yaml 1.1 always and in 1.2 unless FYOPPF_NO_YAML_1_2_BINARY */
START_TEST(binary_parse_tag)
{
	static const struct {
		const char *text;
		enum fy_op_parse_flags parse;
		const char *data;	/* NULL: must fail */
		size_t size;
		bool tagged;		/* kept as tagged base64 text */
	} cases[] = {
		{ "!!binary UE5H", FYOPPF_MODE_YAML_1_2, "PNG", 3, false },
		{ "!!binary UE5H", FYOPPF_MODE_YAML_1_1, "PNG", 3, false },
		{ "!!binary UE5H", FYOPPF_MODE_AUTO, "PNG", 3, false },
		{ "!!binary UE5H", FYOPPF_MODE_YAML_1_1_PYYAML, "PNG", 3, false },
		{ "!!binary UE5H", FYOPPF_MODE_YAML_1_2 | FYOPPF_NO_YAML_1_2_BINARY, "UE5H", 4, true },
		{ "!!binary UE5H", FYOPPF_MODE_YAML_1_1 | FYOPPF_NO_YAML_1_2_BINARY, "PNG", 3, false },
		{ "%YAML 1.1\n---\n!!binary UE5H", FYOPPF_MODE_AUTO | FYOPPF_NO_YAML_1_2_BINARY, "PNG", 3, false },
		{ "!<tag:yaml.org,2002:binary> UE5H", FYOPPF_MODE_YAML_1_2, "PNG", 3, false },
		{ "!!binary |\n  UE5H\n  iQ==\n", FYOPPF_MODE_YAML_1_2, "PNG\x89", 4, false },
		{ "!!binary >\n  UE5H\n  iQ==\n", FYOPPF_MODE_YAML_1_2, "PNG\x89", 4, false },
		{ "!!binary \"UE5H\"", FYOPPF_MODE_YAML_1_2, "PNG", 3, false },
		{ "!!binary 'UE 5H'", FYOPPF_MODE_YAML_1_2, "PNG", 3, false },
		{ "!!binary \"\"", FYOPPF_MODE_YAML_1_2, "", 0, false },
		{ "!!binary", FYOPPF_MODE_YAML_1_2, "", 0, false },
		{ "!!binary UE5", FYOPPF_MODE_YAML_1_2, NULL, 0, false },
		{ "!!binary U@5H", FYOPPF_MODE_YAML_1_2, NULL, 0, false },
		{ "!!binary UE5H=", FYOPPF_MODE_YAML_1_2, NULL, 0, false },
		{ "!!binary UE5", FYOPPF_MODE_YAML_1_1, NULL, 0, false },
		/* opted out, bad base64 is just text */
		{ "!!binary UE5", FYOPPF_MODE_YAML_1_2 | FYOPPF_NO_YAML_1_2_BINARY, "UE5", 3, true },
		/* other tags are untouched */
		{ "!binary UE5H", FYOPPF_MODE_YAML_1_2, "UE5H", 4, true },
		{ "UE5H", FYOPPF_MODE_YAML_1_2, "UE5H", 4, false },
	};
	struct fy_generic_builder *gb;
	fy_generic_sized_string text, back;
	fy_generic parsed, vt;
	size_t i;

	gb = fy_generic_builder_create(NULL);
	ck_assert_ptr_nonnull(gb);

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
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
		vt = fy_generic_get_tag(parsed);
		ck_assert_msg(fy_generic_is_string(vt) == cases[i].tagged,
			      "case %zu: %s tag %s", i, cases[i].text,
			      fy_generic_is_string(vt) ? "kept" : "dropped");
	}

	fy_generic_builder_destroy(gb);
}
END_TEST

/* !!binary inside collections and as a key; one bad value fails the parse */
START_TEST(binary_parse_nested)
{
	static const char good[] =
		"a: !!binary UE5H\n"
		"b: [!!binary iQ==, x]\n"
		"? !!binary eHk=\n"
		": !!binary ''\n";
	static const char bad[] =
		"a: !!binary UE5H\n"
		"b: [x, {c: !!binary U@5H}]\n";
	struct fy_generic_builder *gb;
	fy_generic_sized_string text, k = { "xy", 2 }, e = { "", 0 }, b89 = { "\x89", 1 };
	fy_generic parsed, expect;

	gb = fy_generic_builder_create(NULL);
	ck_assert_ptr_nonnull(gb);

	text.data = good;
	text.size = sizeof(good) - 1;
	parsed = fy_parse(gb, text, FYOPPF_DISABLE_DIRECTORY, NULL);
	ck_assert(fy_generic_is_valid(parsed));
	expect = fy_mapping(gb, "a", "PNG", "b", fy_sequence(gb, fy_value(gb, b89), "x"),
			    fy_value(gb, k), fy_value(gb, e));
	ck_assert(fy_generic_compare(expect, parsed) == 0);

	text.data = bad;
	text.size = sizeof(bad) - 1;
	parsed = fy_parse(gb, text, FYOPPF_DISABLE_DIRECTORY, NULL);
	ck_assert(!fy_generic_is_valid(parsed));

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
	fy_check_testcase_add_test(ctc, binary_base64_codec);
	fy_check_testcase_add_test(ctc, binary_base64_emit);
	fy_check_testcase_add_test(ctc, binary_base64_width);
	fy_check_testcase_add_test(ctc, binary_base64_user_tag);
	fy_check_testcase_add_test(ctc, binary_parse_tag);
	fy_check_testcase_add_test(ctc, binary_parse_nested);
}
