/*
 * libfyaml-test-fuzzing.c - libfyaml fuzzing regression testing harness
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

#include <check.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-generic.h>
#ifdef HAVE_REFLECTION
#include <libfyaml/libfyaml-reflection.h>
#include "fy-reflection-private.h"
#endif

#include "fy-check.h"

#if defined(__linux__)
/* Test: gh#344 - dump a deep collection with source markers. */
START_TEST(fuzz_issue_344_deep_primitive_dump_repro)
{
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_DISABLE_MMAP_OPT |
			 FYPCF_DISABLE_DEPTH_LIMIT | FYPCF_DISABLE_BUFFERING |
			 FYPCF_YPATH_ALIASES | FYPCF_CREATE_MARKERS |
			 FYPCF_RELAXED_FLOW_DOC | FYPCF_KEEP_ANCHORS |
			 FYPCF_ENABLE_CACHE | FYPCF_DEFAULT_VERSION_AUTO |
			 FYPCF_JSON_NONE,
	};
	struct fy_generic_document_builder_cfg gdb_cfg = {0};
	struct fy_generic_document_builder *gdb;
	struct fy_generic_builder *gb;
	struct fy_parser *parser;
	unsigned char blob[9996];
	fy_generic v;
	FILE *fp;
	unsigned int count = 0;

	memcpy(blob, "\x5b\x02\x1d\x06\x1d\x06\x5d", 7);
	memset(blob + 7, '[', sizeof(blob) - 7);
	fp = fopen("/dev/null", "w");
	ck_assert_ptr_ne(fp, NULL);
	parser = fy_parser_create(&cfg);
	ck_assert_ptr_ne(parser, NULL);
	ck_assert_int_eq(fy_parser_set_string(parser,
			(const char *)blob, sizeof(blob)), 0);
	gb = fy_generic_builder_create(NULL);
	ck_assert_ptr_ne(gb, NULL);
	gdb_cfg.parse_cfg = cfg;
	gdb_cfg.gb = gb;
	gdb_cfg.flags = FYGDBF_DEFAULT | FYGDBF_CREATE_MARKERS;
	gdb = fy_generic_document_builder_create(&gdb_cfg);
	ck_assert_ptr_ne(gdb, NULL);
	while (fy_generic_is_valid(v =
	       fy_generic_document_builder_load_document(gdb, parser))) {
		fy_generic_dump_primitive(fp, 0, v);
		count++;
	}
	ck_assert_uint_gt(count, 0);
	ck_assert_int_eq(ferror(fp), 0);
	fy_generic_document_builder_destroy(gdb);
	fy_generic_builder_destroy(gb);
	fy_parser_destroy(parser);
	fclose(fp);
}
END_TEST
#endif

/* Test: parse "*********&&&&&&" with RESOLVE_DOCUMENT | YPATH_ALIASES */
START_TEST(fuzz_resolve_aliases_stars_amps)
{
	char buf[] = "*********&&&&&&";
	struct fy_parse_cfg cfg = {0};
	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_YPATH_ALIASES;
	fy_document_destroy(fy_document_build_from_string(&cfg, buf, FY_NT));
}
END_TEST

#if defined(__linux__)
/* Test: gh#340 - an alias path ends after it resolves to a sequence. */
START_TEST(fuzz_issue_340_alias_path_end_repro)
{
	static const char doc[23] =
		"-\r\n-\t*/3/1%:/.:\n-\r\n-\t*/";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_COLLECT_DIAG | FYPCF_RESOLVE_DOCUMENT |
			 FYPCF_DISABLE_RECYCLING | FYPCF_DISABLE_DEPTH_LIMIT |
			 FYPCF_YPATH_ALIASES | FYPCF_CREATE_MARKERS |
			 FYPCF_KEEP_STYLE | FYPCF_ENABLE_CACHE |
			 FYPCF_DEFAULT_VERSION_1_3 | FYPCF_JSON_AUTO,
	};
	struct fy_document *fyd;
	FILE *f;

	f = fmemopen((void *)doc, sizeof(doc), "r");
	ck_assert_ptr_ne(f, NULL);
	fyd = fy_document_build_from_fp(&cfg, f);
	fy_document_destroy(fyd);
	fclose(f);
}
END_TEST
#endif

#ifdef HAVE_REFLECTION
/* Test: gh#341 - a dependent type cycle must not loop forever. */
START_TEST(fuzz_issue_341_dependent_type_cycle_repro)
{
	struct fy_type a = { .type_kind = FYTK_PTR };
	struct fy_type b = { .type_kind = FYTK_PTR };
	char *decl;

	a.dependent_type = &b;
	b.dependent_type = &a;
	decl = fy_type_generate_c_declaration(&a, NULL, 0);
	ck_assert_ptr_eq(decl, NULL);
}
END_TEST

/* Test: gh#342 - reflection equality must accept nullable type names. */
START_TEST(fuzz_issue_342_null_type_name_repro)
{
	static const unsigned char blob[] = {
		0x46, 0x59, 0x50, 0x47, 0x01, 0x00, 0x00, 0x01,
		0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x77, 0x6a,
		0x79, 0x61, 0x01, 0x01, 0x17, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01,
	};
	struct fy_reflection *rfl, *rfl2;
	void *blob2;
	size_t blob2_size;

	rfl = fy_reflection_from_packed_blob(blob, sizeof(blob), NULL);
	ck_assert_ptr_ne(rfl, NULL);
	blob2 = fy_reflection_to_packed_blob(rfl, &blob2_size, true, true);
	ck_assert_ptr_ne(blob2, NULL);
	rfl2 = fy_reflection_from_packed_blob(blob2, blob2_size, NULL);
	ck_assert_ptr_ne(rfl2, NULL);
	(void)fy_reflection_equal(rfl, rfl2);
	fy_reflection_destroy(rfl2);
	free(blob2);
	fy_reflection_destroy(rfl);
}
END_TEST

/* Test: gh#348 - C generation must accept an INT64_MAX enum value. */
START_TEST(fuzz_issue_348_enum_int64_max_repro)
{
	static const unsigned char blob[] = {
		0x46, 0x59, 0x50, 0x47, 0x01, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2d,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5c,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x16, 0x01, 0x16, 0x04, 0x16, 0x08, 0x1a, 0x00,
		0x01, 0x01, 0x1a, 0x00, 0x01, 0x00, 0x38, 0x01,
		0x00, 0x02, 0x01, 0x01, 0x00, 0x01, 0x00, 0x07,
		0x00, 0x07, 0x05, 0x00, 0x07, 0x01, 0x03, 0x0f,
		0x13, 0x01, 0x01, 0x01, 0x0f, 0x00, 0x07, 0x00,
		0x07, 0x28, 0x00, 0x07, 0x01, 0x04, 0x01, 0x13,
		0x07, 0x01, 0x05, 0x32, 0x36, 0x01, 0x01, 0x02,
		0x32, 0x00, 0x07, 0x01, 0x03, 0x0f, 0x13, 0x00,
		0x66, 0x6f, 0x6f, 0x00, 0x66, 0x6f, 0x6f, 0x5f,
		0x76, 0x61, 0x6c, 0x75, 0x65, 0x00, 0x62, 0x61,
		0x72, 0x00, 0x7b, 0x6e, 0x75, 0x6c, 0x6c, 0x2d,
		0x00, 0x00, 0x00, 0x00, 0x46, 0x59, 0x50, 0x47,
		0x0a, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x62, 0x61, 0x7a, 0x00, 0x7b, 0x6e, 0x75,
		0x6c, 0x6c, 0x2d, 0x61, 0x6c, 0x6c, 0x6f, 0x77,
		0x65, 0x64, 0x3a, 0x20, 0x74, 0x72, 0x75, 0x65,
		0x2c, 0x20, 0x72, 0x65, 0x71, 0x75, 0x69, 0x72,
		0x65, 0x64, 0x3a, 0x20, 0x66, 0x61, 0x6c, 0x73,
		0x65, 0x7d, 0x00,
	};
	struct fy_reflection *rfl;
	char *generated;

	rfl = fy_reflection_from_packed_blob(blob, sizeof(blob), NULL);
	ck_assert_ptr_ne(rfl, NULL);
	generated = fy_reflection_generate_c_string(rfl,
			FYCGF_INDENT_TAB | FYCGF_COMMENT_YAML);
	ck_assert_ptr_ne(generated, NULL);
	ck_assert_ptr_ne(strstr(generated, "bar = 9223372036854775807,"),
			 NULL);
	free(generated);
	fy_reflection_destroy(rfl);
}
END_TEST
#endif

/* Test: gh#347 - merge alias path resolution must finish quickly. */
START_TEST(fuzz_issue_347_merge_alias_path_repro)
{
	static const char yaml[] =
		"<<:\n"
		"- */z\n"
		"- */z\n"
		"- */z\n"
		"- */z\n";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET | FYPCF_RESOLVE_DOCUMENT,
	};

	fy_document_destroy(fy_document_build_from_string(&cfg, yaml,
			sizeof(yaml) - 1));
}
END_TEST

/*
 * Parse a stream and keep the text of its first scalar.
 * Return the number of scalars.
 */
static unsigned int
parse_first_scalar(const char *yaml, size_t size, char *text, size_t text_size,
		   size_t *lenp, bool *errorp)
{
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET,
	};
	struct fy_parser *fyp;
	struct fy_event *fye;
	const char *t;
	size_t len;
	unsigned int count = 0;

	*lenp = 0;
	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);
	ck_assert_int_eq(fy_parser_set_string(fyp, yaml, size), 0);
	while ((fye = fy_parser_parse(fyp)) != NULL) {
		if (fye->type == FYET_SCALAR) {
			t = fy_token_get_text(fye->scalar.value, &len);
			ck_assert_ptr_ne(t, NULL);
			if (!count) {
				ck_assert_uint_le(len, text_size);
				memcpy(text, t, len);
				*lenp = len;
			}
			count++;
		}
		fy_parser_event_free(fyp, fye);
	}
	*errorp = fy_parser_get_stream_error(fyp);
	fy_parser_destroy(fyp);

	return count;
}

/* Test: gh#349 - a line below the indentation indicator ends the scalar. */
START_TEST(fuzz_issue_349_block_scalar_indicator_repro)
{
	static const char yaml[] =
		"|9\n[\x01 # comment\0\0\x04\0[[]:]]";
	static const char under[] = "|2\n abc\n";
	static const char valid[] = "|2\n  abc\n";
	char text[16];
	size_t len;
	bool error;

	/* the reported line is less indented than the indicator */
	ck_assert_uint_eq(parse_first_scalar(yaml, sizeof(yaml) - 1,
			text, sizeof(text), &len, &error), 0);
	ck_assert(error);

	ck_assert_uint_eq(parse_first_scalar(under, sizeof(under) - 1,
			text, sizeof(text), &len, &error), 0);
	ck_assert(error);

	ck_assert_uint_eq(parse_first_scalar(valid, sizeof(valid) - 1,
			text, sizeof(text), &len, &error), 1);
	ck_assert(!error);
	ck_assert_uint_eq(len, 4);
	ck_assert(!memcmp(text, "abc\n", 4));
}
END_TEST

/* Test: gh#350 - an empty clipped block scalar must stay empty. */
START_TEST(fuzz_issue_350_empty_clipped_block_scalar_repro)
{
	static const char * const yamls[] = {
		" >\n  ",
		" |\n  ",
		" >\n  \n",
	};
	static const char keep[] = "- |+\n  ";
	char text[16];
	size_t len;
	bool error;
	unsigned int i;

	for (i = 0; i < sizeof(yamls) / sizeof(yamls[0]); i++) {
		ck_assert_uint_eq(parse_first_scalar(yamls[i], strlen(yamls[i]),
				text, sizeof(text), &len, &error), 1);
		ck_assert(!error);
		ck_assert_uint_eq(len, 0);
	}

	/* keep still has the line break (test suite JEF9) */
	ck_assert_uint_eq(parse_first_scalar(keep, sizeof(keep) - 1,
			text, sizeof(text), &len, &error), 1);
	ck_assert(!error);
	ck_assert_uint_eq(len, 1);
	ck_assert_int_eq(text[0], '\n');
}
END_TEST

/* Test: gh#351 - a NUL after a block scalar header is an error. */
START_TEST(fuzz_issue_351_block_scalar_header_nul_repro)
{
	static const char yaml[] = "\n>\0lu";
	static const char literal[] = "|\0lu";
	char text[16];
	size_t len;
	bool error;

	/* the scalar is empty, and the NUL gives an error */
	ck_assert_uint_eq(parse_first_scalar(yaml, sizeof(yaml) - 1,
			text, sizeof(text), &len, &error), 1);
	ck_assert(error);
	ck_assert_uint_eq(len, 0);

	ck_assert_uint_eq(parse_first_scalar(literal, sizeof(literal) - 1,
			text, sizeof(text), &len, &error), 1);
	ck_assert(error);
	ck_assert_uint_eq(len, 0);
}
END_TEST

/* Test: parse ":\n*.." with RESOLVE_DOCUMENT | DISABLE_BUFFERING | YPATH_ALIASES | ALLOW_DUPLICATE_KEYS */
START_TEST(fuzz_resolve_disable_buffering_colon_star)
{
	struct fy_parse_cfg cfg = {0};
	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_DISABLE_BUFFERING | FYPCF_YPATH_ALIASES | FYPCF_ALLOW_DUPLICATE_KEYS;
	fy_document_destroy(fy_document_build_from_string(&cfg, ":\n*..", FY_NT));
}
END_TEST

/* Test: parse special chars with RESOLVE_DOCUMENT | YPATH_ALIASES */
START_TEST(fuzz_resolve_aliases_special_chars)
{
	char buf[] = ": *$...!/*$///";
	struct fy_parse_cfg cfg = {0};
	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_YPATH_ALIASES;
	fy_document_destroy(fy_document_build_from_string(&cfg, buf, FY_NT));
}
END_TEST

/* Test: fy_emit_document_to_fp with NULL document */
START_TEST(fuzz_emit_null_document)
{
	fy_emit_document_to_fp(NULL, FYECF_EXTENDED_CFG, stdout);
}
END_TEST

#if defined(__linux__)

/* Test: fy_node_build_from_fp with invalid UTF-8 data */
START_TEST(fuzz_node_build_fp_invalid_data)
{
	struct fy_document *fyd = NULL;
	FILE *f = NULL;
	char data[] = "\x7b\x5b\xa8\x59\x3a";

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	f = fmemopen((void *)data, sizeof(data), "r");
	ck_assert_ptr_ne(f, NULL);

	struct fy_node *fyn = fy_node_build_from_fp(fyd, f);
	fy_document_set_root(fyd, fyn);

	if (f) fclose(f);
	fy_document_destroy(fyd);
}
END_TEST

#endif

/* Test: parse sequence with anchors and aliases, recursive resolve */
START_TEST(fuzz_recursive_resolve_anchors_aliases)
{
	char buf[] = "\x2d\x0a\x2d\x20\x0d\x0a\x2d\x20\x26\x2d\x0a\x2d\x20\x0d\x0a\x2d\x20\x26\x2d\x0a\x20\x0d\x0a\x2d\x20\x26\x2d\x0a\x2d\x20\x2a\x2f\x37\x37\x37\x37\x37\x37\x37\x3e\x37\x37\x0a\x2d\x20\x26\x2d\x0a\x2d\x20\x2a\x2f\x39\x32\x36\x38\x30\x33\x3a\x32";
	struct fy_parse_cfg cfg = {0};
	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_PREFER_RECURSIVE | FYPCF_YPATH_ALIASES;
	fy_document_destroy(fy_document_build_from_string(&cfg, buf, FY_NT));
}
END_TEST

#if defined(__linux__)

/* Test: fy_node_build_from_fp with emoji and invalid UTF-8 via file */
START_TEST(fuzz_node_build_fp_emoji_invalid_utf8)
{
	unsigned char test_yaml[] = {
		'-', '-', '-', '\n',
		'-', '\n',
		' ', ' ', '\'', 'e', 'm', 'o', 'j', 'i', ' ',
		0xf0, 0x9f, 0x98, 0x80,
		'\'', ':', ' ', '{', 'w', 'z', 'h', ':', ' ', '[', '"', 't', 'a', 'b', '\t', 's', 'e', 'p', '"', ',', ' ',
		'\'', 'p', 'l', 'a', 'i', 'n', ' ', 's', 'c', 'a', 'l', 'a', 'r', '\'', ',', ' ',
		0x92,
		'd', 't', 'y', ']', '}', '\n',
		0
	};

	FILE *fp = fmemopen((void *)test_yaml, sizeof(test_yaml) - 1, "r");
	ck_assert_ptr_ne(fp, NULL);

	struct fy_document *doc = fy_document_create(NULL);
	ck_assert_ptr_ne(doc, NULL);

	struct fy_node *n = fy_node_build_from_fp(doc, fp);
	fclose(fp);

	if (n)
		fy_node_free(n);

	fy_document_destroy(doc);
}
END_TEST

#endif

/* Test: emit event with invalid scalar style */
START_TEST(fuzz_emit_event_invalid_scalar_style)
{
	struct fy_emitter_cfg cfg = {0};
	cfg.flags = (enum fy_emitter_cfg_flags)0;
	cfg.output = NULL;
	cfg.userdata = NULL;
	cfg.diag = NULL;

	struct fy_emitter *emit = fy_emitter_create(&cfg);
	ck_assert_ptr_ne(emit, NULL);

	struct fy_event *ev = fy_emit_event_create(emit, FYET_STREAM_START);
	ck_assert_ptr_ne(ev, NULL);
	ck_assert_int_eq(fy_emit_event(emit, ev), 0);

	ev = fy_emit_event_create(emit, FYET_DOCUMENT_START, 1, NULL, NULL);
	ck_assert_ptr_ne(ev, NULL);
	ck_assert_int_eq(fy_emit_event(emit, ev), 0);

	enum fy_scalar_style invalid_style = (enum fy_scalar_style)(-2);
	ev = fy_emit_event_create(emit, FYET_SCALAR, invalid_style, "test", (size_t)4, NULL, NULL);
	/* should return NULL for invalid style, not crash */
	(void)ev;
	if (ev)
		fy_emit_event_free(emit, ev);

	fy_emitter_destroy(emit);
}
END_TEST

/* Test: parse binary-like data with recursive resolve and ypath aliases */
START_TEST(fuzz_recursive_resolve_binary_data)
{
	char buf[] = "\x2f\x20\x20\x2d\x2f\x2f\x2a\x2f\x65\x2f\x2f\x2f\x31\x26\x26\x2f\x20\x20\x3a\x0a\x0a\x2a\x2f\x2f\x2e\x2f\x20\xd7\xd0\xd0\xd0\xd0\x19\x3a\x0d\x30\x2e\x3a\x0d\x35\x7a\x3a\x68\x3a\x0d\x04\x26\x18\x3a\x0d\x32\x2e\x2d\x0a\x20\x26\x2d\x32\x26\x2a\x2a\x2d\x0a\x0a\x2d\x0a\x2d\x2a\x20\x5f\x2d\x0a\x2d\x0a\x2d\x20\xf6\xdf\xd2\xdf\xcd\xd9\xd5\xf9\x2d\x0a\x2d\x20\x2d\x20";
	struct fy_parse_cfg cfg = {0};
	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_PREFER_RECURSIVE | FYPCF_YPATH_ALIASES;
	fy_document_destroy(fy_document_build_from_string(&cfg, buf, FY_NT));
}
END_TEST

/* Test: fy_node_by_path with YPATH on sequence */
START_TEST(fuzz_node_by_path_ypath_sequence)
{
	char buf[] = "\x40\x61\x3e\x58\x40";
	struct fy_document *fyd = NULL;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	struct fy_node *fyn = fy_node_create_sequence(fyd);
	fy_document_set_root(fyd, fyn);
	struct fy_node *root = fy_document_root(fyd);
	struct fy_node *node = fy_node_by_path(root, buf, FY_NT, FYNWF_PTR_YPATH);
	(void)node;

	fy_document_destroy(fyd);
}
END_TEST

/* Test: fy_token_iter_getc after fy_token_iter_read */
START_TEST(fuzz_token_iter_getc_after_read)
{
	struct fy_document *fyd = NULL;
	struct fy_parse_cfg cfg = {0};
	struct fy_token_iter *iter = NULL;
	char buf[] = "!n2_";

	fyd = fy_document_build_from_string(&cfg, buf, FY_NT);
	if (!fyd)
		return;

	struct fy_node *root = fy_document_root(fyd);
	if (!root || !fy_node_is_scalar(root))
		goto out;

	struct fy_token *token = fy_node_get_scalar_token(root);
	if (!token)
		goto out;

	iter = fy_token_iter_create(token);
	if (!iter)
		goto out;

	char buf2[256];
	fy_token_iter_read(iter, buf2, sizeof(buf2) - 1);

	int c = fy_token_iter_getc(iter);
	(void)c;

out:
	fy_token_iter_destroy(iter);
	fy_document_destroy(fyd);
}
END_TEST

/* Test: parse ":\n*.." with COLLECT_DIAG and many flags */
START_TEST(fuzz_resolve_collect_diag_colon_star)
{
	char buf[] = ":\n*..";
	struct fy_parse_cfg cfg = {0};
	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_DISABLE_RECYCLING | FYPCF_DISABLE_BUFFERING | FYPCF_YPATH_ALIASES | FYPCF_ALLOW_DUPLICATE_KEYS;
	fy_document_destroy(fy_document_build_from_string(&cfg, buf, FY_NT));
}
END_TEST

/* Test: parse ":\n*.." with COLLECT_DIAG flag included */
START_TEST(fuzz_collect_diag_colon_star)
{
	char buf[] = ":\n*..";
	struct fy_parse_cfg cfg = {0};
	cfg.flags = FYPCF_COLLECT_DIAG | FYPCF_RESOLVE_DOCUMENT | FYPCF_DISABLE_RECYCLING | FYPCF_DISABLE_BUFFERING | FYPCF_YPATH_ALIASES | FYPCF_ALLOW_DUPLICATE_KEYS;
	fy_document_destroy(fy_document_build_from_string(&cfg, buf, FY_NT));
}
END_TEST

/* Test: emit document with STRIP_EMPTY_KV and many mode flags */
START_TEST(fuzz_emit_strip_empty_kv_many_modes)
{
	struct fy_document *fyd = NULL;
	struct fy_parse_cfg cfg = {0};
	char *buf = NULL;

	fyd = fy_document_build_from_string(&cfg, ":\n*$@", FY_NT);
	if (!fyd)
		goto out;

	buf = fy_emit_document_to_string(fyd, FYECF_STRIP_EMPTY_KV | FYECF_MODE_BLOCK | FYECF_MODE_FLOW | FYECF_MODE_FLOW_ONELINE | FYECF_MODE_JSON | FYECF_MODE_JSON_TP | FYECF_MODE_JSON_ONELINE | FYECF_MODE_DEJSON | FYECF_MODE_PRETTY | FYECF_MODE_MANUAL | FYECF_MODE_FLOW_COMPACT | FYECF_MODE_JSON_COMPACT | FYECF_DOC_START_MARK_OFF | FYECF_VERSION_DIR_ON);

out:
	free(buf);
	fy_document_destroy(fyd);
}
END_TEST

/* Test: fy_path_expr_build_from_string with "!***" */
START_TEST(fuzz_path_expr_triple_star)
{
	struct fy_path_parse_cfg parse_cfg = {0};
	struct fy_path_expr *expr = fy_path_expr_build_from_string(&parse_cfg, "!***", FY_NT);
	fy_path_expr_free(expr);
}
END_TEST

/* Test: parse star-slash-bang with RESOLVE_DOCUMENT | PREFER_RECURSIVE | YPATH_ALIASES | ALLOW_DUPLICATE_KEYS */
START_TEST(fuzz_resolve_recursive_star_slash_bang)
{
	struct fy_parse_cfg cfg = {0};
	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_PREFER_RECURSIVE | FYPCF_YPATH_ALIASES | FYPCF_ALLOW_DUPLICATE_KEYS;
	fy_document_destroy(fy_document_build_from_string(&cfg, "*//!!", FY_NT));
}
END_TEST

/* Test: fy_node_by_path with "*@" via JSON/RELJSON/YPATH flags */
START_TEST(fuzz_node_by_path_star_at)
{
	struct fy_document *fyd = NULL;
	struct fy_parse_cfg cfg = {0};

	cfg.flags = FYPCF_YPATH_ALIASES;

	fyd = fy_document_build_from_string(&cfg, ":", FY_NT);
	if (!fyd)
		return;

	struct fy_node *root = fy_document_root(fyd);
	struct fy_node *node = fy_node_by_path(root, "*@", FY_NT, FYNWF_PTR_JSON | FYNWF_PTR_RELJSON | FYNWF_PTR_YPATH);
	(void)node;

	fy_document_destroy(fyd);
}
END_TEST

/* Test: fy_node_by_path with "**@" and emit to JSON */
START_TEST(fuzz_node_by_path_double_star_at_emit)
{
	struct fy_document *fyd = NULL;
	struct fy_parse_cfg cfg = {0};
	cfg.flags = FYPCF_PREFER_RECURSIVE | FYPCF_JSON_NONE;

	fyd = fy_document_build_from_string(&cfg, ":", FY_NT);
	if (!fyd)
		return;

	struct fy_node *root = fy_document_root(fyd);
	struct fy_node *node = fy_node_by_path(root, "**@", FY_NT, FYNWF_PTR_JSON | FYNWF_PTR_RELJSON | FYNWF_PTR_YPATH);
	char *b = fy_emit_node_to_string(node, FYECF_STRIP_LABELS | FYECF_MODE_JSON | FYECF_MODE_JSON_TP | FYECF_MODE_JSON_ONELINE | FYECF_MODE_DEJSON);

	fy_document_destroy(fyd);
	free(b);
}
END_TEST

/* Test: fy_node_by_path with "*_Y" on empty sequence */
START_TEST(fuzz_node_by_path_star_underscore_sequence)
{
	struct fy_document *fyd = NULL;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	struct fy_node *fyn = fy_node_create_sequence(fyd);
	fy_document_set_root(fyd, fyn);
	struct fy_node *root = fy_document_root(fyd);
	struct fy_node *node = fy_node_by_path(root, "*_Y", FY_NT, FYNWF_FOLLOW | FYNWF_PTR_JSON | FYNWF_PTR_RELJSON | FYNWF_PTR_YPATH);
	(void)node;

	fy_document_destroy(fyd);
}
END_TEST

/* Test: parse with many ypath alias patterns, recursive resolve + duplicate keys */
START_TEST(fuzz_ypath_aliases_complex_pattern)
{
	char buf[] = "\x2a\x2a\x40\x28\x28\x29\x30\x30\x28\x29\x30\x2a\x28\x2d\x2a\x2a\x40\x28\x28\x2a\x29\x2a\x30\x40\x40\x2a\x28\x30\x28\x28\x40\x29\x2d\x30\x29\x2d\x40\x30\x37\x29\x40\x40\x30\x28\x29\x30\x28\x28\x2a\x29\x2a\x30\x40\x40\x2a\x28\x30\x28\x28\x40\x29\x2d\x30\x29\x2d\x40\x30\x37\x29\x40\x40\x30\x28\x29\x30\x28\x71\x71\x71\x2d\x40\x40\x2a\x2a\x33\x40\x2a\x2a\x30\x2a\x40\x2a\x2d\x2a\x2a\x30\x30\x00\x00\x00\x00";
	struct fy_parse_cfg cfg = {0};
	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_YPATH_ALIASES | FYPCF_ALLOW_DUPLICATE_KEYS;
	fy_document_destroy(fy_document_build_from_string(&cfg, buf, FY_NT));
}
END_TEST

/* Test: parse with sloppy flow indentation and many disable flags */
START_TEST(fuzz_sloppy_flow_disable_flags)
{
	char buf[] = "\x20\x2d\x20\x3f\x20\x20\x3a\x20\x20\x2a\x2a\x24\x2e\x2e\x2a\x2a\x2a\x2f\x2f\x2f\x24\x2e\x2e\x2e\x2a\x25\x2f\x2a\x2a\x2f\x2f\x40\x2e\x2a\x24\x24\x2a\x2a\x2e\x2e\x2a\x2a\x2f\x2f\x2f\x2a\x2a\x2f\x6c\x2a\x2f\x2f\x2f\x2f\x2f\x2f\x2f\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
	struct fy_parse_cfg cfg = {0};
	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_DISABLE_MMAP_OPT | FYPCF_DISABLE_RECYCLING | FYPCF_DISABLE_ACCELERATORS | FYPCF_SLOPPY_FLOW_INDENTATION | FYPCF_YPATH_ALIASES;
	fy_document_destroy(fy_document_build_from_string(&cfg, buf, FY_NT));
}
END_TEST

/* Test: parse with disable recycling and ypath aliases */
START_TEST(fuzz_disable_recycling_ypath_aliases)
{
	char buf[] = "\x2d\x0a\x20\x3f\x2c\x20\x20\x2d\x20\x2a\x60\x24\x2e\x2d\x0a\x2d\x20\x2a\x2f\x2f\x2e\x30\x40\x24\x2f\x21\x2f\x2f\x78\x2f\x2f\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
	struct fy_parse_cfg cfg = {0};
	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_DISABLE_RECYCLING | FYPCF_YPATH_ALIASES;
	fy_document_destroy(fy_document_build_from_string(&cfg, buf, FY_NT));
}
END_TEST

#if defined(__linux__)

/* Test: fy_document_build_from_fp with sloppy flow indentation */
START_TEST(fuzz_build_from_fp_sloppy_flow)
{
	struct fy_document *fyd = NULL;
	struct fy_parse_cfg cfg = {0};
	FILE *f = NULL;

	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_SLOPPY_FLOW_INDENTATION | FYPCF_ALLOW_DUPLICATE_KEYS;

	char data[] = "-\n*/-";
	f = fmemopen((void *)data, strlen(data), "r");
	if (!f)
		return;

	fyd = fy_document_build_from_fp(&cfg, f);

	if (f) fclose(f);
	fy_document_destroy(fyd);
}
END_TEST

/* Test: fy_node_build_from_fp with "[\n:]" */
START_TEST(fuzz_node_build_fp_flow_mapping)
{
	char buf[] = "\x5b\x0a\x3a\x5d\x00";
	struct fy_document *fyd = NULL;
	FILE *f = NULL;

	fyd = fy_document_create(NULL);
	if (!fyd)
		return;

	f = fmemopen((void *)buf, sizeof(buf), "r");
	if (!f)
		goto out;

	struct fy_node *fyn = fy_node_build_from_fp(fyd, f);
	if (fyn)
		fy_document_set_root(fyd, fyn);

out:
	if (f) fclose(f);
	fy_document_destroy(fyd);
}
END_TEST

#endif	// __linux__

/* Test: parse complex anchors/aliases with disable buffering and recursive */
START_TEST(fuzz_complex_anchors_recursive_buffering)
{
	char buf[] = "\x2d\x20\x3f\x20\x2d\x20\x2a\x2d\x0a\x23\x0a\x2d\x20\x0d\x0a\x20\x20\x3f\x20\x2d\x20\x2d\x20\x2a\x2d\x0d\x0a\x2d\x20\x2a\x2d\x0a\x2d\x0a\x2d\x20\x20\x2a\x2d\x0a\x23\x0a\x2d\x20\x0d\x0a\x20\x20\x3f\x20\x2d\x20\x3f\x20\x2d\x20\x2a\x2d\x0a\x23\x0a\x2d\x20\x0d\x0a\x20\x20\x3f\x20\x2d\x20\x2d\x20\x2a\x2d\x0a\x2d\x20\x26\x2d\x20\x0a\x20\x2d\x20\x20\x20\x3f\x20\x2d\x20\x3f\x20\x2a\x2f\x2f\x2a\x2a\x40\x00\x00\x00\x00";
	struct fy_parse_cfg cfg = {0};
	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_DISABLE_BUFFERING | FYPCF_SLOPPY_FLOW_INDENTATION | FYPCF_PREFER_RECURSIVE | FYPCF_YPATH_ALIASES;

	struct fy_document *fyd = fy_document_build_from_string(&cfg, buf, FY_NT);
	fy_document_destroy(fyd);
}
END_TEST

/* Test: fy_node_build_from_string with " >\n%" */
START_TEST(fuzz_node_build_string_block_scalar)
{
	struct fy_document *fyd = NULL;

	fyd = fy_document_create(NULL);
	if (!fyd)
		return;

	struct fy_node *fyn = fy_node_build_from_string(fyd, " >\n%\x00", FY_NT);
	if (fyn)
		fy_document_set_root(fyd, fyn);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: fy_node_by_path with "./" and emit with many mode flags */
START_TEST(fuzz_node_by_path_dot_slash_emit)
{
	struct fy_document *fyd = NULL;
	struct fy_parse_cfg cfg = {0};
	char *buf = NULL;

	cfg.flags = FYPCF_COLLECT_DIAG | FYPCF_DISABLE_MMAP_OPT | FYPCF_DISABLE_RECYCLING | FYPCF_DISABLE_BUFFERING;
	int flags2 = FYNWF_PTR_JSON | FYNWF_PTR_RELJSON | FYNWF_PTR_YPATH | FYNWF_URI_ENCODED;
	int flags3 = FYECF_MODE_BLOCK | FYECF_MODE_FLOW | FYECF_MODE_FLOW_ONELINE | FYECF_MODE_JSON_TP | FYECF_MODE_JSON_ONELINE | FYECF_MODE_DEJSON | FYECF_MODE_PRETTY | FYECF_MODE_MANUAL | FYECF_MODE_FLOW_COMPACT | FYECF_MODE_JSON_COMPACT;

	fyd = fy_document_build_from_string(&cfg, "**", FY_NT);
	if (!fyd)
		goto out;

	struct fy_node *root = fy_document_root(fyd);
	struct fy_node *node = fy_node_by_path(root, "./", FY_NT, flags2);

	if (!node)
		goto out;

	buf = fy_emit_node_to_string(node, flags3);

out:
	fy_document_destroy(fyd);
	free(buf);
}
END_TEST

/* Test: fy_node_by_path with "(1()" on sequence */
START_TEST(fuzz_node_by_path_parens_sequence)
{
	char buf[] = "(1()";
	struct fy_document *fyd = NULL;

	fyd = fy_document_create(NULL);
	if (!fyd)
		return;

	struct fy_node *fyn = fy_node_create_sequence(fyd);
	fy_document_set_root(fyd, fyn);
	struct fy_node *root = fy_document_root(fyd);
	struct fy_node *node = fy_node_by_path(root, buf, FY_NT, FYNWF_PTR_JSON | FYNWF_PTR_RELJSON | FYNWF_PTR_YPATH | FYNWF_URI_ENCODED);
	(void)node;

	fy_document_destroy(fyd);
}
END_TEST

/* Test: parse comment-heavy input with PARSE_COMMENTS | PREFER_RECURSIVE and emit with many flags */
START_TEST(fuzz_parse_comments_recursive_emit)
{
	char buf[] = "\x23\x63\x3a\x0d\x0a\x23\x3a\x0a\x23\x24\x0d\x01\x7c\x23\x3a\x09\x52\x25\x42";
	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd = NULL;
	FILE *fp = NULL;

	cfg.flags = FYPCF_PARSE_COMMENTS | FYPCF_DISABLE_ACCELERATORS | FYPCF_PREFER_RECURSIVE;

	fyd = fy_document_build_from_string(&cfg, buf, FY_NT);
	if (!fyd)
		return;

	fp = fopen("/dev/null", "w");
	if (!fp)
		goto out;

	fy_emit_document_to_fp(fyd, FYECF_STRIP_DOC | FYECF_NO_ENDING_NEWLINE | FYECF_MODE_BLOCK | FYECF_MODE_FLOW_ONELINE | FYECF_MODE_JSON | FYECF_MODE_JSON_TP | FYECF_MODE_JSON_ONELINE | FYECF_MODE_DEJSON | FYECF_MODE_MANUAL | FYECF_MODE_JSON_COMPACT, fp);

out:
	if (fp) fclose(fp);
	fy_document_destroy(fyd);
}
END_TEST

#ifdef __linux__

/* Test: fy_document_build_from_fp with ":\r:" and recursive resolve + duplicate keys */
START_TEST(fuzz_build_from_fp_recursive_duplicate_keys)
{
	char buf[] = "\x3a\x0d\x3a";
	struct fy_document *fyd = NULL;
	struct fy_parse_cfg cfg = {0};
	FILE *f = NULL;

	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_DISABLE_MMAP_OPT | FYPCF_DISABLE_ACCELERATORS | FYPCF_PREFER_RECURSIVE | FYPCF_ALLOW_DUPLICATE_KEYS;

	f = fmemopen((void *)buf, strlen(buf), "r");
	if (!f)
		return;

	fyd = fy_document_build_from_fp(&cfg, f);

	if (f) fclose(f);
	fy_document_destroy(fyd);
}
END_TEST

#endif

#if defined(__linux__)

static void fuzz_dump_testsuite_event(struct fy_parser *fyp FY_UNUSED,
				      struct fy_event *fye)
{
	const char *anchor = NULL;
	const char *tag = NULL;
	const char *text = NULL;
	const char *alias = NULL;
	size_t anchor_len = 0, tag_len = 0, text_len = 0, alias_len = 0;
	const struct fy_mark *sm, *em = NULL;

	sm = fy_event_start_mark(fye);
	em = fy_event_end_mark(fye);
	(void)sm; (void)em;

	switch (fye->type) {
	case FYET_NONE:
	case FYET_STREAM_START:
	case FYET_STREAM_END:
	case FYET_DOCUMENT_START:
	case FYET_DOCUMENT_END:
	case FYET_MAPPING_END:
	case FYET_SEQUENCE_END:
	case FYET_ALIAS:
		break;
	case FYET_MAPPING_START:
		if (fye->mapping_start.anchor)
			anchor = fy_token_get_text(fye->mapping_start.anchor, &anchor_len);
		if (fye->mapping_start.tag)
			tag = fy_token_get_text(fye->mapping_start.tag, &tag_len);
		break;
	case FYET_SEQUENCE_START:
		if (fye->sequence_start.anchor)
			anchor = fy_token_get_text(fye->sequence_start.anchor, &anchor_len);
		if (fye->sequence_start.tag)
			tag = fy_token_get_text(fye->sequence_start.tag, &tag_len);
		break;
	case FYET_SCALAR:
		if (fye->scalar.anchor)
			anchor = fy_token_get_text(fye->scalar.anchor, &anchor_len);
		if (fye->scalar.tag)
			tag = fy_token_get_text(fye->scalar.tag, &tag_len);
		break;
	default:
		break;
	}

	switch (fye->type) {
	default:
		break;
	case FYET_SCALAR:
		text = fy_token_get_text(fye->scalar.value, &text_len);
		break;
	case FYET_ALIAS:
		alias = fy_token_get_text(fye->alias.anchor, &alias_len);
		break;
	}

	(void)anchor; (void)anchor_len;
	(void)tag; (void)tag_len;
	(void)text; (void)text_len;
	(void)alias; (void)alias_len;
}

/* Test: parse ">\x00\x09\x0d" via fy_parser_parse event loop */
START_TEST(fuzz_parser_event_loop_block_scalar)
{
	char buf[] = "\x3e\x00\x09\x0d";
	struct fy_parser *fyp = NULL;
	struct fy_parse_cfg cfg = {0};
	struct fy_event *fyev = NULL;
	FILE *f = NULL;

	f = fmemopen((void *)buf, 4, "r");
	if (!f)
		return;

	fyp = fy_parser_create(&cfg);
	if (!fyp)
		goto out;

	if (fy_parser_set_input_fp(fyp, NULL, f) != 0)
		goto out;

	while ((fyev = fy_parser_parse(fyp)) != NULL) {
		fuzz_dump_testsuite_event(fyp, fyev);
		fy_parser_event_free(fyp, fyev);
	}

out:
	if (f) fclose(f);
	fy_parser_destroy(fyp);
}
END_TEST

#endif

/* Test: parse sequence with embedded comments using COLLECT_DIAG | DISABLE_RECYCLING | PARSE_COMMENTS | DISABLE_BUFFERING */
START_TEST(fuzz_collect_diag_parse_comments_sequence)
{
	char data[] = "- foo\n#\n\n#\n- G";
	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd = NULL;

	cfg.flags = FYPCF_COLLECT_DIAG | FYPCF_DISABLE_RECYCLING | FYPCF_PARSE_COMMENTS | FYPCF_DISABLE_BUFFERING | FYPCF_JSON_NONE;

	fyd = fy_document_build_from_string(&cfg, data, FY_NT);
	fy_document_destroy(fyd);
}
END_TEST

/* Test: parse quoted scalar, clone document, and compare nodes via multiple compare APIs */
START_TEST(fuzz_node_compare_clone_quoted_scalar)
{
	char data[] = "'''''''''''' ";
	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd = NULL;
	struct fy_document *fyd2 = NULL;
	struct fy_node *root, *root2;
	bool same, equal, matches, text_matches;

	cfg.flags = FYPCF_PREFER_RECURSIVE | FYPCF_YPATH_ALIASES | FYPCF_ALLOW_DUPLICATE_KEYS;

	fyd = fy_document_build_from_string(&cfg, data, FY_NT);
	if (!fyd)
		return;

	root = fy_document_root(fyd);
	if (!root)
		goto out;

	same = fy_node_compare(root, root);
	(void)same;

	fyd2 = fy_document_clone(fyd);
	if (!fyd2)
		goto out;

	root2 = fy_document_root(fyd2);
	equal = fy_node_compare(root, root2);
	matches = fy_node_compare_string(root, data, FY_NT);
	text_matches = fy_node_compare_text(root, data, FY_NT);
	(void)equal; (void)matches; (void)text_matches;

out:
	fy_document_destroy(fyd);
	fy_document_destroy(fyd2);
}
END_TEST

/* Test: parse comment with override */
START_TEST(fuzz_parse_comment_with_override)
{
	char buf[] = "- a: b\n  # end\n# bottom\n";
	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd;

	cfg.flags = FYPCF_PARSE_COMMENTS | FYPCF_DISABLE_ACCELERATORS | FYPCF_PREFER_RECURSIVE;

	fyd = fy_document_build_from_string(&cfg, buf, FY_NT);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(fuzz_stream_end_token_replay_cleanup)
{
	char data[] = "\x2a\x26\x7b\x7b\x26\x26\x26\x7b\x26\x26\x26\x26\x5b\x26\x26\x26";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_COLLECT_DIAG | FYPCF_DEFAULT_VERSION_1_1 |
			 FYPCF_PREFER_RECURSIVE | FYPCF_JSON_NONE |
			 FYPCF_JSON_FORCE | FYPCF_RELAXED_FLOW_DOC,
	};
	struct fy_document *fyd;

	fyd = fy_document_build_from_string(&cfg, data, sizeof(data));
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(fuzz_issue_298_list_del_uaf_repro)
{
	char data[] =
		"\x5b\x5d\x5b\x5b\x5b\x5b\x00\x6c\x6f\x6e\x67\xff\x00\xbc\xbc\x0a"
		"\xbc\x9c\x2a\x0a\xbc\x43\x43\xbc\xbc\xac\x36\xbc\xbc\xbc\x5b\x5b"
		"\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b"
		"\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b"
		"\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b"
		"\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x67\xff\x00\x5b\x5b\x5b\x5b"
		"\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x74\x5b\x5b"
		"\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x5b\x3f";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_DEFAULT_VERSION_1_1 |
			 FYPCF_SLOPPY_FLOW_INDENTATION |
			 FYPCF_YPATH_ALIASES |
			 FYPCF_ALLOW_DUPLICATE_KEYS |
			 FYPCF_CREATE_MARKERS |
			 FYPCF_RELAXED_FLOW_DOC,
	};
	struct fy_document *fyd;

	fyd = fy_document_build_from_string(&cfg, data, sizeof(data));
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(fuzz_issue_299_token_unref_uaf_repro)
{
	char data[] = "\x2a\x26\x7b\x7b\x26\x26\x26\x7b\x26\x26\x26\x26\x5b\x26\x26\x26";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_COLLECT_DIAG | FYPCF_DEFAULT_VERSION_1_1 |
			 FYPCF_PREFER_RECURSIVE | FYPCF_JSON_NONE |
			 FYPCF_JSON_FORCE | FYPCF_RELAXED_FLOW_DOC,
	};
	struct fy_document *fyd;

	fyd = fy_document_build_from_string(&cfg, data, sizeof(data));
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(fuzz_issue_300_token_unref_overflow_repro)
{
	char data[] = "\x20\x20\x2a\x2a\x09\x5b";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_DEFAULT_VERSION_1_1 |
			 FYPCF_SLOPPY_FLOW_INDENTATION |
			 FYPCF_JSON_NONE |
			 FYPCF_JSON_FORCE |
			 FYPCF_RELAXED_FLOW_DOC |
			 FYPCF_ENABLE_CACHE,
	};
	struct fy_document *fyd;

	fyd = fy_document_build_from_string(&cfg, data, sizeof(data));
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(fuzz_issue_305_malloc_allocator_free_repro)
{
	struct fy_allocator *a;
	int tag;
	void *p;

	a = fy_allocator_create("malloc", NULL);
	ck_assert_ptr_ne(a, NULL);

	tag = fy_allocator_get_tag(a);
	ck_assert_int_ne(tag, FY_ALLOC_TAG_ERROR);

	p = fy_allocator_alloc(a, tag, 400, 8);
	ck_assert_ptr_ne(p, NULL);

	memset(p, 0xab, 400);
	fy_allocator_free(a, tag, p);
	fy_allocator_destroy(a);
}
END_TEST

#if defined(__linux__)
START_TEST(fuzz_issue_309_scalar_path_key_repro)
{
	const char data[] = "\x32\x3a\x09\x2a\x30";
	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd;
	FILE *f;

	cfg.flags = FYPCF_QUIET |
		    FYPCF_RESOLVE_DOCUMENT |
		    FYPCF_DISABLE_MMAP_OPT |
		    FYPCF_DEFAULT_VERSION_1_1 |
		    FYPCF_PREFER_RECURSIVE |
		    FYPCF_JSON_NONE |
		    FYPCF_YPATH_ALIASES |
		    FYPCF_ALLOW_DUPLICATE_KEYS |
		    FYPCF_ENABLE_CACHE;

	f = fmemopen((void *)data, sizeof(data) - 1, "r");
	ck_assert_ptr_ne(f, NULL);

	fyd = fy_document_build_from_fp(&cfg, f);

	fclose(f);
	fy_document_destroy(fyd);
}
END_TEST
#endif

START_TEST(fuzz_resolve_document_ypath_null_alias)
{
	char buf[] = ":\n*.null";
	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd;

	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_YPATH_ALIASES;

	fyd = fy_document_build_from_string(&cfg, buf, FY_NT);
	fy_document_destroy(fyd);
}
END_TEST

#if defined(__linux__)
START_TEST(fuzz_build_from_fp_ypath_aliases_recursive)
{
	char buf[] = "\n? - :\n? - : - */**";
	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd;
	FILE *f;

	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_PREFER_RECURSIVE | FYPCF_YPATH_ALIASES;

	f = fmemopen((void *)buf, strlen(buf), "r");
	fyd = fy_document_build_from_fp(&cfg, f);
	if (f)
		fclose(f);
	fy_document_destroy(fyd);
}
END_TEST
#endif

START_TEST(fuzz_path_expr_build_bang_triple_star)
{
	struct fy_path_parse_cfg parse_cfg = {0};
	struct fy_path_expr *expr;

	expr = fy_path_expr_build_from_string(&parse_cfg, "!***", FY_NT);
	fy_path_expr_free(expr);
}
END_TEST

START_TEST(fuzz_resolve_recursive_ypath_aliases_dup_keys)
{
	char buf[] = "*//!!";
	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd;

	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_PREFER_RECURSIVE |
		    FYPCF_YPATH_ALIASES | FYPCF_ALLOW_DUPLICATE_KEYS;

	fyd = fy_document_build_from_string(&cfg, buf, FY_NT);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(fuzz_disable_recycling_ypath_aliases_dup_keys)
{
	char buf[] = "*((0)/*";
	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd;

	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_DISABLE_RECYCLING |
		    FYPCF_YPATH_ALIASES | FYPCF_ALLOW_DUPLICATE_KEYS;

	fyd = fy_document_build_from_string(&cfg, buf, FY_NT);
	fy_document_destroy(fyd);
}
END_TEST

#if defined(__linux__)
START_TEST(fuzz_build_from_fp_sloppy_recursive_ypath_aliases)
{
	char data[] = "\x2a\x27\x2f\x2a\x27\x27\x24\x09\x09\x3a\x0a\x72\x3a\x0a\x2a\x2f\x72\x2f";
	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd;
	FILE *f;

	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_DISABLE_ACCELERATORS |
		    FYPCF_SLOPPY_FLOW_INDENTATION | FYPCF_PREFER_RECURSIVE |
		    FYPCF_YPATH_ALIASES;

	f = fmemopen((void *)data, strlen(data), "r");
	fyd = fy_document_build_from_fp(&cfg, f);
	if (f)
		fclose(f);
	fy_document_destroy(fyd);
}
END_TEST
#endif

START_TEST(fuzz_parse_comments_emit_many_modes)
{
	char data[] =
		"\x3a\x3a\x20\x3a\x20\x3a\x3a\x20\x3a\x20\x3f\x01\x05\x3a\x20\x3a\x20"
		"\x3f\x20\x3a\x3a\x20\x3a\x20\x3a\x20\x3a\x3a\x20\x3a\x20\x3f\x3a\x20"
		"\x3a\x20\x3b\x20\x3f\x01\x05\x3a\x20\x3a\x20\x3f\x20\x3a\x3a\x20\x3a"
		"\x20\x3a\x20\x3a\x3a\x20\x3a\x20\x3f\x3a\x20\x3a\x20\x3b\x20\x3f\x01"
		"\x05\x3a\x20\x3a\x20\x3f\x20\x3a\x3a\x20\x3a\x20\x3a\x20\x3a\x3a\x20"
		"\x3a\x20\x3f\x3a\x20\x3a\x20\x3b\x20\x3f\x20\x3a\x3f\x01\x05\x3a\x20"
		"\x3a\x20\x3f\x20\x3a\x3a\x20\x3a\x20\x3a\x20\x3a\x3a\x20\x3a\x20\x3a"
		"\x3a\x20\x3a\x20\x3f\x3a\x20\x3a\x20\x3b\x20\x3f\x20\x3a\x3a\x55\x55"
		"\x55\x55\x20\x3a\x3a\x20\x3a\x20\x3a\x20\x3f\x20\x3a\x3a\x20\x3a\x20"
		"\x3a\x20\x3a\x3a\x20\x3a\x20\x3f\x3a\x20\x3a\x20\x3b\x20\x3f\x20\x3a"
		"\x3f\x01\x05\x3a\x20\x3a\x20\x3f\x20\x3a\x3f\x20\x3a\x3f\x01\x05\x20"
		"\x3a\x3a\x20\x20\x3a\x20\x3f";
	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd;
	struct fy_emitter *emitter;
	char *collected;
	char buf[4096];
	int eflags;
	int rc;

	cfg.flags = FYPCF_DISABLE_RECYCLING | FYPCF_PARSE_COMMENTS;
	eflags = FYECF_OUTPUT_COMMENTS |
		 FYECF_WIDTH_DEFAULT | FYECF_WIDTH_80 | FYECF_WIDTH_132 | FYECF_WIDTH_INF |
		 FYECF_MODE_BLOCK | FYECF_MODE_FLOW | FYECF_MODE_FLOW_ONELINE |
		 FYECF_MODE_JSON | FYECF_MODE_JSON_TP | FYECF_MODE_JSON_ONELINE |
		 FYECF_MODE_DEJSON | FYECF_MODE_PRETTY | FYECF_MODE_MANUAL |
		 FYECF_MODE_FLOW_COMPACT | FYECF_MODE_JSON_COMPACT |
		 FYECF_TAG_DIR_OFF | FYECF_TAG_DIR_ON;

	fyd = fy_document_build_from_string(&cfg, data, FY_NT);
	if (!fyd)
		return;

	memset(buf, 0, sizeof(buf));
	rc = fy_emit_document_to_buffer(fyd, eflags, buf, sizeof(buf));
	(void)rc;

	emitter = fy_emit_to_string(eflags);
	if (emitter) {
		fy_emit_document(emitter, fyd);
		size_t out_size;
		collected = fy_emit_to_string_collect(emitter, &out_size);
		free(collected);
	}
	fy_emitter_destroy(emitter);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(fuzz_resolve_recursive_ypath_dup_keys_emit_fp)
{
	char data[] = "\x0a\x2a\x2f\x2a\x2f\x5e\x2f\x2a\x2f\x20\x09\x09\x3a\x0a\x72\x3a"
		      "\x0a\x2a\x5e\x2f\x2a\x2f\x20\x09\x09\x3a\x0a\x2a\x2f\x5e";
	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd;
	FILE *fp;
	int rc;

	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_PREFER_RECURSIVE |
		    FYPCF_YPATH_ALIASES | FYPCF_ALLOW_DUPLICATE_KEYS;

	fyd = fy_document_build_from_string(&cfg, data, FY_NT);
	if (!fyd)
		return;

	fp = fopen("/dev/null", "w");
	rc = fy_emit_document_to_fp(fyd,
		FYECF_MODE_BLOCK | FYECF_MODE_FLOW_ONELINE | FYECF_MODE_JSON |
		FYECF_MODE_JSON_TP | FYECF_MODE_JSON_ONELINE | FYECF_MODE_DEJSON |
		FYECF_MODE_MANUAL | FYECF_MODE_JSON_COMPACT,
		fp);
	(void)rc;
	if (fp)
		fclose(fp);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(fuzz_node_get_type_uaf)
{
	char data[] =
		"\x0a\x20\x2d\x20\x2a\x2f\x2e\x2e\x2a\x0a"
		"\x20\x2d\x20\x2a\x2f\x2a\x2a\x21\x0a"
		"\x20\x2d\x20\x2a\x2f\x2a\x2a\x2f";

	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd = NULL;

	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_PREFER_RECURSIVE |
		    FYPCF_YPATH_ALIASES;

	// this must fail
	fyd = fy_document_build_from_string(&cfg, data, FY_NT);
	ck_assert_ptr_eq(fyd, NULL);
}
END_TEST

START_TEST(fuzz_emit_node_to_string_uaf)
{
	char buf[] =
		"\x2a\x2f\x2a\x5e\x2f\x09\x09\x3a\x0a\x2a\x2f\x5e\x09\x3a\x0a"
		"\x2a\x2f\x2a\x2f\x09\x3a\x0a\x5e\x2a\x09\x3a\x0a\x2a\x2f\x5e";

	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd = NULL;

	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_DISABLE_ACCELERATORS |
		    FYPCF_SLOPPY_FLOW_INDENTATION | FYPCF_PREFER_RECURSIVE |
		    FYPCF_YPATH_ALIASES;

	// this must fail
	fyd = fy_document_build_from_string(&cfg, buf, FY_NT);
	ck_assert_ptr_eq(fyd, NULL);
}
END_TEST

START_TEST(fuzz_document_iterator_cleanup_uaf)
{
	/* Data from the issue report that triggers the merge-key path */
	char data[] =
		"\x0a\x25\x59\x41\x4d\x4c\x09\x31\x2e\x31\x0d\x2d\x2d\x2d\xe2\x80"
		"\xa8\x74\x74\x74\x74\x74\x74\x74\x74\x74\x74\x74\x74\x74\x74\x20"
		"\x20\x20\x20\x1a\x3a\x2d\x2d\x25\x20\x2d\x20\x22\x3a\x0d\x2d\x20"
		"\x3a\x2d\x2d\x20\x2d\x20\x2a\x2f\x5a\x0d\x3c\x3c\x3a\x0d\x2d\x20"
		"\x20\x20\x3a\x20\x20\x2d\x54\x41\x47\x2f\x3a\x0d\x3a\x20\x74\x74"
		"\x74\x74\x74\x74\x74\x74\x74\x74\x74\x74\x74\x3a\x20\x3a\x20\x3a"
		"\x09\x32\x2e\x31\x0d\x2d\x2d\x2d\xe2\x80\xa8\x74\x2d\x20\x2d\x20"
		"\x2a\x2f\x3a\x0d\x3c\x3c\x3a\x0d\x2d\x20\x20\x7b\x7d\x20\x69\x2a"
		"\x21\x7b\x7b\x7d\x7a\x5d\x42\x7b\x7d\x7b\x35\x0a\x74\x74\x74\x74"
		"\x74\x74\x74\x74\x74\x74\x74\x74\x74\x3a\x20\x3a\x20\x3a\x20\x3a"
		"\x20\x3a\x20\x3a\x20\x6d\x20\x43\x20\x74\x74\x74\x74\x74\x74\x74"
		"\x74\x74\x74\x74\x74\x74\x3a\x20\x3a\x20\x3a\x09\x32\x2e\x31\x0d"
		"\x2d\x2d\x2d\xe2\x80\xa8\x74\x2d\x20\x2d\x20\x2a\x2f\x3a\x0d\x3c"
		"\x3c\x3a\x0d\x2d\x20\x20\x74\x74\x74\x74\x74\x74\x74\x74\x74\x74"
		"\x74\x74\x74\x3a\x20\x3a\x20\x3a\x20\x3a\x20\x3a\x20\x3a\x20\x6d"
		"\x20\x43\x20\x3a\x20\x3a\x20\x2d\x20\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00";

	struct fy_parser *fyp = NULL;
	struct fy_parse_cfg cfg = {0};
	struct fy_event *fyev = NULL;
	int rc;

	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_PARSE_COMMENTS |
		    FYPCF_DISABLE_BUFFERING | FYPCF_SLOPPY_FLOW_INDENTATION;

	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);

	rc = fy_parser_set_string(fyp, data, FY_NT);
	ck_assert_int_eq(rc, 0);

	while ((fyev = fy_parser_parse(fyp)) != NULL) {
		fy_event_get_comments(fyev);
		fy_parser_event_free(fyp, fyev);
	}

	fy_parser_destroy(fyp);
}
END_TEST

START_TEST(fuzz_emit_mapping_memory_leak)
{
	char data[] =
		"\x3a\x0a\x0a\x77\x7b\x3a\x0a\x7b\x7d\x3a\x0a\x0a\x0a\x7b\x7d\x3a"
		"\x0a\x3a\x3a\x0a\x0a\x0a\x7b\x7d\x3a\x0a\x3a\x0a\x7b\x7d\x3a\x0a"
		"\x0a\x7b\x7d\x3a\x0a\x3d\x7b\x7b\x7d\x3a\x0a\x3a\x3a\x0a\x0a\x3a"
		"\x3a\x0a\x0a\x0a\x7b\x7d\x3a\x0a\x3a\x0a\x4a\x7b\x7d\x3a\x0a\x0a"
		"\x7b\x7d\x3a\x0a\x0a\x77\x7b\x3a\x0a\x7b\x7d\x3a\x0a\x0a\x0a\x7b"
		"\x7d\x3a\x0a\x3a\x3a\x0a\x0a\x0a\x7b\x7d\x3a\x0a\x3a\x0a\x7b\x7d"
		"\x3a\x0a\x0a\x7b\x7d\x3a\x0a\x3d\x7b\x7b\x7d\x3a\x0a\x3a\x3a\x0a"
		"\x0a\x0a\x7b\x7d\x3a\x0a\x3a\x0a\x7b\x7d\x3a\x0a\x0a\x7b\x7d\x3a"
		"\x0a\x3a\x0a\x7b\x7d\x3a\x0a\x0a\x7b\x7d\x3a\x0a\x0a\x77\x7b\x3a"
		"\x0a\x7b\x7d\x3a\x0a\x0a\x0a\x7b\x7d\x3a\x0a\x3a\x3a\x0a\x0a\x0a"
		"\x7b\x7d\x3a\x0a\x3a\x0a\x7b\x7d\x3a\x0a\x3a\x0a\x7b\x7d\x3a\x0a"
		"\x0a\x7b\x7d\x3a\x0a\x0a\x77\x7b\x3a\x0a\x7b\x7d\x3a\x0a\x0a\x0a"
		"\x7b\x7d\x3a\x0a\x3a\x3a\x0a\x0a\x0a\x7b\x7d\x3a\x0a\x3a\x0a\x7b"
		"\x7d\x3a\x0a\x0a\x7b\x7d\x3a\x0a\x3d\x7b\x7b\x7d\x3a\x0a\x3a\x3a"
		"\x0a\x0a\x0a\x7b\x7d\x3a\x0a\x3a\x0a\x7b\x7d\x3a\x0a\x0a\x7b\x7d"
		"\x3a\x0a\x3a\x0a\x7b\x7d\x3a\x0a\x0a\x7b\x7d\x3a\x0a\x0a\x77\x7b"
		"\x3a\x0a\x7b\x7d\x3a\x0a\x0a\x0a\x7b\x7d\x3a\x0a\x3a\x3a\x0a\x0a"
		"\x0a\x7b\x7d\x3a\x0a\x3a\x0a\x7b\x7d\x3a\x0a\x0a\x7b\x7d\x3a\x0a"
		"\x2d\x2d\x2d\x7b\x7d\x3a\x0a\x3d\x7b\x7b\x7d\x3a\x0a\x3a\x3a\x0a"
		"\x0a\x0a\x7b\x7d\x3a\x0a\x3a\x0a\x7b\x7d\x3a\x0a\x0a\x7b\x7d\x3a"
		"\x0a\x3d\x7b\x7d\x3a\x0a\x3f";

	struct fy_parse_cfg cfg = {
		.flags = FYPCF_YPATH_ALIASES | FYPCF_ALLOW_DUPLICATE_KEYS
	};
	struct fy_document *fyd;
	char *result;

	fyd = fy_document_build_from_string(&cfg, data, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	result = fy_emit_document_to_string(fyd, FYECF_SORT_KEYS | FYECF_MODE_JSON_TP);
	ck_assert_ptr_ne(result, NULL);
	free(result);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(fuzz_walk_number_to_expr_nonfinite)
{
	char data[] = "\x2a\x28\x6e\x2a\x28\x2d\x30\x2f\x30\x29\x2f\x2a\x2a";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_COLLECT_DIAG | FYPCF_RESOLVE_DOCUMENT |
			 FYPCF_PREFER_RECURSIVE | FYPCF_JSON_NONE |
			 FYPCF_YPATH_ALIASES,
	};
	struct fy_document *fyd = NULL;
	FILE *fp = NULL;
	int rc;

	fyd = fy_document_build_from_string(&cfg, data, FY_NT);
	if (!fyd)
		return;

	fp = fopen("/dev/null", "w");
	ck_assert_ptr_ne(fp, NULL);

	rc = fy_emit_document_to_fp(fyd,
		FYECF_STRIP_LABELS | FYECF_WIDTH_DEFAULT | FYECF_WIDTH_80 |
		FYECF_WIDTH_INF | FYECF_MODE_FLOW | FYECF_MODE_FLOW_ONELINE |
		FYECF_MODE_JSON | FYECF_MODE_JSON_TP | FYECF_MODE_JSON_ONELINE |
		FYECF_MODE_DEJSON | FYECF_MODE_FLOW_COMPACT |
		FYECF_MODE_JSON_COMPACT | FYECF_DOC_START_MARK_OFF,
		fp);
	(void)rc;

	fclose(fp);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(fuzz_path_expr_execute_oom)
{
	char data[] =
		"\x2f\x2f\x2c\x2e\x2f\x2f\x2f\x2f\x2e\x2c\x2e\x2f\x2e\x2c\x2e\x2f"
		"\x2f\x2f\x2f\x2e\x2c\x33\x3a\x32\x2c\x2e\x2f\x2f\x2e\x2c\x2e\x2f"
		"\x2f\x2e\x2c\x2e\x2c\x2e\x31\x2f\x2f\x2f\x2e\x2c\x2e\x2f\x2f\x2f"
		"\x2f\x2e\x2c\x33\x3a\x32\x2c\x2f\x2e\x2c\x2e\x2f\x2e\x2c\x2e\x2f"
		"\x2f\x2f\x2f\x2e\x2c\x33\x3a\x32\x2c\x2e\x2f\x2f\x2e\x2c\x2e\x2f"
		"\x2f\x2e\x2c\x2e\x2c\x2e\x31\x2f\x2f\x2f\x2e\x2c\x2e\x2f\x2f\x2f"
		"\x2f\x2e\x2c\x33\x3a\x32\x2c\x2e\x2f\x2f\x2e\x2c\x2e\x2f\x2f\x2e"
		"\x2c\x2e\x2c\x2e\x31\x2f\x2f\x2e\x2c\x2e\x2f\x2e\x2c\x2e\x2f\x2e"
		"\x2c\x2e\x2f\x2f\x2e\x2c\x2e\x2f\x2f\x2e\x2c\x2e\x2c\x2e\x31\x2f"
		"\x2f\x2e\x2c\x2e\x2f\x2e\x2c\x2e\x2f\x2e\x2c\x2e\x2c\x2e\x2f\x2a";
	struct fy_document *fyd = NULL;
	struct fy_node *fyn, *root, *node;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fyn = fy_node_create_sequence(fyd);
	ck_assert_ptr_ne(fyn, NULL);
	fy_document_set_root(fyd, fyn);

	root = fy_document_root(fyd);
	ck_assert_ptr_ne(root, NULL);

	node = fy_node_by_path(root, data, FY_NT,
			FYNWF_FOLLOW | FYNWF_PTR_JSON |
			FYNWF_PTR_RELJSON | FYNWF_PTR_YPATH);
	(void)node;

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(fuzz_anchor_accel_cleanup_scalar_borrowed_input)
{
	char *doc_str;
	struct fy_document *fyd = NULL;
	struct fy_parse_cfg cfg = { .flags = FYPCF_PREFER_RECURSIVE | FYPCF_JSON_NONE };

	doc_str = strdup("&a foo");
	ck_assert_ptr_ne(doc_str, NULL);

	fyd = fy_document_build_from_string(&cfg, doc_str, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	free(doc_str);
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(fuzz_anchor_accel_cleanup_mapping_borrowed_input)
{
	char *doc_str;
	struct fy_document *fyd = NULL;
	struct fy_node *removed_val = NULL;
	struct fy_parse_cfg cfg = { .flags = FYPCF_PREFER_RECURSIVE | FYPCF_JSON_NONE };
	struct fy_node *root, *key_node;

	doc_str = strdup("{ &a foo: bar }");
	ck_assert_ptr_ne(doc_str, NULL);

	fyd = fy_document_build_from_string(&cfg, doc_str, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	root = fy_document_root(fyd);
	ck_assert_ptr_ne(root, NULL);
	ck_assert(fy_node_is_mapping(root));

	key_node = fy_node_build_from_string(fyd, "foo", FY_NT);
	ck_assert_ptr_ne(key_node, NULL);
	removed_val = fy_node_mapping_remove_by_key(root, key_node);
	fy_node_free(key_node);

	fy_node_free(removed_val);
	free(doc_str);
	fy_document_destroy(fyd);
}
END_TEST

#if defined(__linux__)

START_TEST(fuzz_parser_streaming_alias_collection_state_mask)
{
	char data[] =
		"\x7b\x0a\x7b\x26\x25\x3e\x25\x7b\x26\x30\x7a\x3a\x7b\x7b\x26"
		"\x3a\x7b\x7b\x26\x3a\x7b\x66\x3a\x3a\x7b\x7b\x26\x54\x7b\x26"
		"\x3a\x7b\x26\x3a\x7b\x26\x3a\x7b\x7b\x26\x3a\x7b\x26\x3a\x26"
		"\x25\x3e\x25\x7b\x26\x31\x3a\x7b\x7b\x26\x3a\x7b\x32\x26\x3a"
		"\x7b\x66\x3a\x3a\x7b\x7b\x26\x54\x7b\x26\x3a\x7b\x26\x3a\x7b"
		"\x26\x3a\x7b\x7b\x26\x3a\x7b\x26\x3a\x26\x25\x3e\x25\x7b\x26"
		"\x31\x3a\x7b\x28\x26\x3a\x7b\x26\x30\x7a\x3a\x7b\x7b\x26\x3a"
		"\x7b\x7b\x26\x3a\x7b\x66\x3a\x3a\x7b\x7b\x26\x54\x7b\x26\x3a"
		"\x7b\x26\x3a\x7b\x26\x3a\x7b\x7b\x26\x3a\x7b\x26\x3a\x26\x25"
		"\x3e\x60\x7b\x26\x31\x3a\x7b\x7b\x26\x3a\x7b\x32\x26\x3a\x7b"
		"\x66\x3a\x3a\x7b\x7b\x26\x54\x7b\x26\x3a\x7b\x26\x3a\x7b\x26"
		"\x3a\x7b\x7b\x26\x3a\x7b\x26\x3a\x26\x25\x3e\x25\x7b\x26\x31"
		"\x3a\x7b\x7b\x26\x3a\x7b\x32\x6e\x3a\x0a\x25\x59\x41\x4d\x4c"
		"\x09\x31\x2e\x31\xc2\x85\xc2\x85\x27\x29\x23\x56\x56\x31\xc2"
		"\x85\x2e\x2e\x2e\xc2\x85\x2d\x29\x4c\x09\x31\x3a\x7b\x26\x7b"
		"\x3a\x7b\x26\x3a\x7b\x26\x3a\x7b\x0a\x25\x00\x00\x00";
	struct fy_parser *fyp = NULL;
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_PREFER_RECURSIVE |
			 FYPCF_YPATH_ALIASES,
	};
	struct fy_event *fyev;
	FILE *f = NULL;

	f = fmemopen((void *)data, sizeof(data), "r");
	ck_assert_ptr_ne(f, NULL);

	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);

	ck_assert_int_eq(fy_parser_set_input_fp(fyp, NULL, f), 0);

	while ((fyev = fy_parser_parse(fyp)) != NULL) {
		fy_event_get_comments(fyev);
		fy_parser_event_free(fyp, fyev);
	}

	fclose(f);
	fy_parser_destroy(fyp);
}
END_TEST

#endif

/* Test: gh#317 - the path parser honours FYPPCF_QUIET */
START_TEST(fuzz_issue_317_path_parse_quiet_repro)
{
	struct fy_path_parse_cfg pcfg = {0};
	struct fy_path_expr *expr;

	pcfg.flags = FYPPCF_QUIET;

	/* an illegal expression, the error must not reach stderr */
	expr = fy_path_expr_build_from_string(&pcfg, "/**[", FY_NT);
	ck_assert_ptr_eq(expr, NULL);
}
END_TEST

#if defined(__linux__)
/* Test: gh#318 - a diagnostic pulls input and moves the reader buffer */
START_TEST(fuzz_issue_318_reader_pull_uaf_repro)
{
	static const char tail[] =
		"\x0a\x25\x24\x32\x30\x30\x0a\x2e\x00\x2e\xe0\xff\xff\x2e\x2e\x2e"
		"\xe0\x9f\x98\x81\x20\x21\x6d\x20\x0d";
	static char data[2084 + sizeof(tail) - 1];
	struct fy_document *fyd;
	struct fy_node *fyn;
	FILE *f;

	memset(data, ' ', 2084);
	memcpy(data + 2084, tail, sizeof(tail) - 1);

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	f = fmemopen(data, sizeof(data), "r");
	ck_assert_ptr_ne(f, NULL);

	fyn = fy_node_build_from_fp(fyd, f);
	if (fyn)
		fy_document_set_root(fyd, fyn);

	fclose(f);
	fy_document_destroy(fyd);
}
END_TEST
#endif

/* Test: gh#320 - a \U escape with the high bit set overflows the accumulator */
START_TEST(fuzz_issue_320_unicode_escape_shift_repro)
{
	const char data[] = "\x22\x6c\x20\x5c\x55\x66\x65\x66\x66\x66\x66\x66\x66\x66\x20\x22";
	struct fy_document *fyd;
	struct fy_node *fyn;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fyn = fy_node_build_from_string(fyd, data, sizeof(data) - 1);
	ck_assert_ptr_eq(fyn, NULL);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: gh#323 - a deeply nested flow mapping without the depth limit */
START_TEST(fuzz_issue_323_deep_flow_mapping_repro)
{
	static char data[22106];
	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd;

	cfg.flags = FYPCF_QUIET |
		    FYPCF_RESOLVE_DOCUMENT |
		    FYPCF_DISABLE_MMAP_OPT |
		    FYPCF_DISABLE_RECYCLING |
		    FYPCF_KEEP_COMMENTS |
		    FYPCF_DISABLE_DEPTH_LIMIT |
		    FYPCF_DISABLE_ACCELERATORS |
		    FYPCF_DISABLE_BUFFERING |
		    FYPCF_PREFER_RECURSIVE |
		    FYPCF_RELAXED_FLOW_DOC |
		    FYPCF_DEFAULT_VERSION_AUTO |
		    FYPCF_JSON_AUTO;

	memset(data, '{', sizeof(data));

	fyd = fy_document_build_from_string(&cfg, data, sizeof(data));
	ck_assert_ptr_eq(fyd, NULL);
	fy_document_destroy(fyd);
}
END_TEST

/* Test: gh#325 - a merge key whose argument document has no body events */
START_TEST(fuzz_issue_325_merge_key_empty_argument_repro)
{
	static const char data[] =
		"\x25\x20\x6f\x7e\x25\x25\x25\x25\x30\x3a\x0a\x3f\x20\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x48\x53\x5b\x5d\x48"
		"\x53\x5b\x5d\x48\x53\x5b\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x63\x65\x3a\x20\x26\x61"
		"\x6e\x63\x68\x6f\x72\x20\x20\x20\x20\x20\x20\x0a\x20\x20\x20\x20"
		"\x20\x20\x20\x20\x20\x20\x20\x11\x20\x20\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x63\x75\x72\x72\x65\x20\x3c\x37\x20\x3a\x20\x23"
		"\x05\x05\x05\x05\x06\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x63\x65\x3a\x20\x26\x61\x6e\x63"
		"\x68\x6f\x72\x20\x20\x20\x20\x20\x20\x0a\x20\x20\x20\x20\x20\x20"
		"\x20\x20\x20\x20\x20\x3f\x20\x3f\x20\x3a\x20\x3f\x20\x3f\x20\x3f"
		"\x20\x2d\x20\x3a\x20\x3f\x20\x2d\x20\x2d\x20\x3f\x20\x3f\x20\x2d"
		"\x20\x3a\x20\x3f\x20\x3f\x20\x2d\x20\x3a\x20\x3f\x20\x3f\x20\x20"
		"\x3f\x20\x2d\x20\x3a\x20\x3f\x20\x2d\x20\x20\x2d\x20\x20\x3f\x20"
		"\x20\x3f\x20\x2d\x20\x3a\x20\x20\x3f\x20\x2d\x20\x3a\x20\x3f\x20"
		"\x3f\x20\x20\x3f\x20\x2d\x20\x3a\x20\x3f\x20\x2d\x20\x20\x2d\x20"
		"\x20\x3f\x20\x20\x20\x11\x20\x20\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05\x05"
		"\x05\x05\x63\x75\x72\x72\x65\x20\x3c\x37\x20\x3a\x20\x23\x0a\x20"
		"\x20\x21\x65\x20\x20\x3c\x3c\x20\x3a\x20\x23\x0a\x20\x20\x0d\x20"
		"\x20\x20\x20\x20\x20\x20\x2a\x61\x6e\x63\x68\x6f\x72\x0a\x4f\x76"
		"\x65\x65\x20\x61\x6e\x3a\x20\x46\x61\x72\x20\x42\x61\x72\x0a\x52"
		"\x65\x75\x73\x65\x20\x6f\x72\x3a\x20\x2a\x61\x6e\x63\x54\x41\x47"
		"\x20\x21\x20\x6e\xc2\x85\x6f\x72\x20\x14\x21\x65\xa0\x30\x7b\x7d";
	struct fy_parse_cfg cfg = {0};
	struct fy_parser *fyp;
	struct fy_event *fyev;
	int rc;

	cfg.flags = FYPCF_QUIET |
		    FYPCF_COLLECT_DIAG |
		    FYPCF_RESOLVE_DOCUMENT |
		    FYPCF_DISABLE_MMAP_OPT |
		    FYPCF_DISABLE_BUFFERING |
		    FYPCF_YPATH_ALIASES |
		    FYPCF_KEEP_STYLE |
		    FYPCF_RELAXED_FLOW_DOC |
		    FYPCF_KEEP_ANCHORS |
		    FYPCF_DEFAULT_VERSION_1_1 |
		    FYPCF_JSON_AUTO;

	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);

	rc = fy_parser_set_string(fyp, data, sizeof(data) - 1);
	ck_assert_int_eq(rc, 0);

	while ((fyev = fy_parser_parse(fyp)) != NULL)
		fy_parser_event_free(fyp, fyev);

	fy_parser_destroy(fyp);
}
END_TEST

/* Test: gh#327 - fy_node_is_null() read the scalar union of an alias token */
START_TEST(fuzz_issue_327_alias_node_is_null_repro)
{
	static const char data[] = " *8  : : *8  : : *8  : : *ff+";
	static const char path[] = ".null";
	struct fy_parse_cfg cfg = {0};
	struct fy_document *fyd;

	cfg.flags = FYPCF_QUIET |
		    FYPCF_COLLECT_DIAG |
		    FYPCF_DISABLE_MMAP_OPT |
		    FYPCF_DISABLE_RECYCLING |
		    FYPCF_KEEP_COMMENTS |
		    FYPCF_DISABLE_ACCELERATORS |
		    FYPCF_PREFER_RECURSIVE |
		    FYPCF_ALLOW_DUPLICATE_KEYS |
		    FYPCF_KEEP_STYLE |
		    FYPCF_KEEP_ANCHORS |
		    FYPCF_DEFAULT_VERSION_AUTO |
		    FYPCF_JSON_NONE;

	fyd = fy_document_build_from_string(&cfg, data, sizeof(data) - 1);
	if (fyd)
		fy_node_by_path(fy_document_root(fyd), path, sizeof(path) - 1,
				FYNWF_PTR_YPATH);

	fy_document_destroy(fyd);
}
END_TEST

/*
 * Test: gh#337 - a token iterator is used a second time. The finish
 * before the restart must release the heap allocated chunk array of the
 * first iteration.
 */
START_TEST(fuzz_issue_337_token_iter_restart_repro)
{
	/* every escape becomes its own chunk, so the chunk array grows */
	static const char doc[] =
		"\"\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n"
		"\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\"";
	struct fy_document *fyd;
	struct fy_token_iter *iter;
	struct fy_token *fyt;
	char buf[256];

	fyd = fy_document_build_from_string(NULL, doc, sizeof(doc) - 1);
	ck_assert_ptr_ne(fyd, NULL);

	fyt = fy_node_get_scalar_token(fy_document_root(fyd));
	ck_assert_ptr_ne(fyt, NULL);

	iter = fy_token_iter_create(fyt);
	ck_assert_ptr_ne(iter, NULL);

	fy_token_iter_read(iter, buf, sizeof(buf));

	/* finish before the same iterator is used again */
	fy_token_iter_finish(iter);

	fy_token_iter_start(fyt, iter);
	fy_token_iter_read(iter, buf, sizeof(buf));

	fy_token_iter_finish(iter);
	fy_token_iter_destroy(iter);

	fy_document_destroy(fyd);
}
END_TEST

/*
 * Test: gh#339 - a clone shares the document state of its source. Building
 * a node with a %TAG directive in the clone must not leave the source with
 * a tag directive that the clone owns.
 */
START_TEST(fuzz_issue_339_shared_document_state_merge_repro)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_QUIET };
	struct fy_document *fyd, *fydc;
	char *buf;

	fyd = fy_document_build_from_string(&cfg, "a: b", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	buf = strdup("%TAG ! ?\n---");
	ck_assert_ptr_ne(buf, NULL);

	fydc = fy_document_clone(fyd);
	ck_assert_ptr_ne(fydc, NULL);
	fy_node_free(fy_node_build_from_string(fydc, buf, strlen(buf)));
	fy_document_destroy(fydc);
	free(buf);

	fydc = fy_document_clone(fyd);
	ck_assert_ptr_ne(fydc, NULL);
	fy_node_free(fy_node_build_from_string(fydc, "inserted", FY_NT));
	fy_document_destroy(fydc);

	fy_document_destroy(fyd);
}
END_TEST

/*
 * Test: gh#336 - every thread pool used to leak its thread local storage
 * key. Create and destroy more pools than the POSIX key limit allows.
 */
START_TEST(fuzz_issue_336_thread_pool_key_leak_repro)
{
	struct fy_blake3_hasher_cfg cfg;
	struct fy_blake3_hasher *h;
	unsigned int i;

	for (i = 0; i < 1200; i++) {
		memset(&cfg, 0, sizeof(cfg));	/* no thread count, multithreaded */
		h = fy_blake3_hasher_create(&cfg);
		ck_assert_ptr_ne(h, NULL);
		fy_blake3_hasher_update(h, "x", 1);
		fy_blake3_hasher_finalize(h);
		fy_blake3_hasher_destroy(h);
	}
}
END_TEST


#ifdef HAVE_REFLECTION

/* the packed blobs below are all malformed, none must be accepted */
static void fuzz_packed_blob_check(const char *blob, size_t size)
{
	struct fy_reflection *rfl;

	rfl = fy_reflection_from_packed_blob(blob, size, NULL);
	ck_assert_ptr_eq(rfl, NULL);
	fy_reflection_destroy(rfl);
}

/* Test: gh#319 - the type table runs past the end of the blob */
START_TEST(fuzz_issue_319_packed_blob_short_read_repro)
{
	static const char blob[] =
		"\x46\x59\x50\x47\x7f\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x21"
		"\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x6f\x6f\x6f\x6f\x6f\x8a\x6f\x6f\x6f\x6f\x6f\x63\x68\x6f\x3a\x20";

	fuzz_packed_blob_check(blob, sizeof(blob) - 1);
}
END_TEST

/* Test: gh#321 - the blob header declares an illegal id size */
START_TEST(fuzz_issue_321_packed_blob_bad_id_size_repro)
{
	static const char blob[] =
		"\x46\x59\x50\x47\x20\x00\x00\x00\x63\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x1d\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x6f\x6f\x63\x68\x6f\x3a\x30\x6f\xf0\x88"
		"\xa5\x81\x09\x01\x00\x30\x30\x30\x30\x8f\x00\x00\x00\xe9\xe9\xe9"
		"\xe9\xe9\xe9\xe9\xe9\xe9\xe9\xe9\xe9\xe9\xe9\xe9\xe9\xe9\xd7\xe9"
		"\xe9\xe9\xe9\xe9\xe9\xe9\xdb\xe9\xe9\xe9\xe9\xe9\xe9\xe9\xe9\x3e"
		"\x01\x00\x01\x01\x5b\x31\x16\x33\x20\xf0\x88\x30\x30\x30\x0a\x3e"
		"\x2d\xef\x88\x84\x81\x21\x6d\x20\x0d\x88\xa5\x81\x09\x01\x00";

	fuzz_packed_blob_check(blob, sizeof(blob) - 1);
}
END_TEST

/* Test: gh#322 - the blob header declares regions larger than the blob */
START_TEST(fuzz_issue_322_packed_blob_bad_region_size_repro)
{
	static const char blob[] =
		"\x46\x59\x50\x47\x20\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\xf1\x00\x00\x00"
		"\x00\xf9\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\xcd\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x30\x30\x30"
		"\x6f\x6d\x6d\x65\x6e\x74\x0a\x20\x20\x20\x3e\xe0\x90\xa3\x30\x30"
		"\x30\x30";

	fuzz_packed_blob_check(blob, sizeof(blob) - 1);
}
END_TEST

/* Test: gh#324 - a type in the blob refers to a decl that does not exist */
START_TEST(fuzz_issue_324_packed_blob_missing_decl_repro)
{
	static const char blob[] =
		"\x46\x59\x50\x47\x20\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x02"
		"\x00\x00\x30\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x30\x30\x41\x30\x30\x30\x30\x29\x29\x29\x29\x29\x29\x29\x29\x29"
		"\x36\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29"
		"\x29\x29\x29\x29\x29\x29\x29\x29\x10\x29\x29\x29\x29\x29\x29\x29"
		"\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29"
		"\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x31\x29\x29\x29"
		"\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29"
		"\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29\x29"
		"\x29\x29\x29\x64\x29\x29\x29\x29\x29\x29\x29\x29\x30\x0a\x6f\x70"
		"\x6f\x63\x68\x6f\x3a\x02\x02\xff\x7f\x7f\xff";

	fuzz_packed_blob_check(blob, sizeof(blob) - 1);
}
END_TEST

/*
 * Test: gh#326 - a record type in the blob has no declaration at all.
 * The import tolerates the missing declaration, so the blob may load; it
 * must not dereference the declaration.
 */
START_TEST(fuzz_issue_326_packed_blob_record_no_decl_repro)
{
	struct fy_reflection *rfl;
	static const char blob[] =
		"\x46\x59\x50\x47\x4c\x80\x00\x00\x00\x00\x00\x2d\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x20\x00\x00\x00\x00\x00\x00\x00\x41"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xfa\x00\x00\x5e\x5e\x5e"
		"\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x59\xde\x2d\x5e\x5e\x5e\x5e\x5e"
		"\x5e\x5e\x5e\x5e\x5e\x15\x5a\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e"
		"\x5e\x5e\x5e\x5e\x5e\x7e\x5e\x5e\x5e\x5e\x5e\x5e\x00\x5e\x5e\x5e"
		"\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e"
		"\x30\x30\x5c\x75\x2d\x20\x3e\x31";

	rfl = fy_reflection_from_packed_blob(blob, sizeof(blob) - 1, NULL);
	fy_reflection_destroy(rfl);
}
END_TEST

/*
 * Test: gh#330 - a blob type has no declaration. The import must fail
 * instead of leaving a type without a name for the C generator.
 */
START_TEST(fuzz_issue_330_packed_blob_type_no_decl_repro)
{
	struct fy_reflection *rfl;
	static const char blob[] =
		"\x46\x59\x50\x47\x3d\xeb\x02\x00\x00\x00\x00\x00\x00\x1f\x00\x0e"
		"\x00\x00\x00\x00\x00\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00\x0f"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\x80\x00\x04\x5e\x18"
		"\xf9\xd0\x00\x00\x00\x00\x03\x79\x01\x00\x00\x01\x00\x76\x76\x76"
		"\x76\xff\xff\x00\x00\x76\x75\x00\xe3\x1f\xff\x00\x00\x7f\xff\x41"
		"\x00\x00\x0d\xff\x20\xff\x60\xff\x7f\x36\x2b\x48\x48\x48\x48\xff"
		"\x66\x00\x41\x41\x00\x64\x00\xf5\x00\xbf\xbf\xbf\xbf\x68\x95\x95"
		"\x95\x95\x95\x35\x95\x95\x95\x95\x95\x91\x00\x10\x35\x35\x34\x35"
		"\x35\x35\x33\x00\x01\x35\x35\x35\x35\x7a\x7d\x65\x00\x00\x00\x68"
		"\x63\x75\x48\x35\x35\x35\x35\x00\xf5\x80\xff\x35\x35\x35\x1e\x35"
		"\x01\x00\x25\xbf\x01\xff\x00\x47\x55\x00\xf5\x80\x00\x35\x34\x3d"
		"\x35\x35\x35\x25\x35\x36\x29\xf3\x35\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x16\x35\x35\x35\x95\x95\x95\x95"
		"\x00\x10\x95\x95\x95\x95\x95\x95\x95\x35\x35\x35\x35\xf5\x7c\x35"
		"\x35\x35\x36\x19\x35\x35\xf5\x80\x00\xf5\x33\x33\x00\x01\x53\x35"
		"\x35\x35\x35\x27\x35\xd7\xe5\xdf\xf5\xf5\xf5\x34\x68\x00\x80\xff"
		"\xff\x15\x48\x4a\x69\x48\x48\xff\x00\xbf\xbf\x29\x34\x33\x00\x09"
		"\x35\x35\x00\x00\x00\x80\xbf\xfa\x68\x01\x00\x68\x68\xfa\xe6\xfa"
		"\x68\x01\x00\x68\x68\x95\x95\x95\x95\x95\x10\x95\x95\x95\x95\x95"
		"\x95\x95\x35\x35\x35\x34\x35\x35\x35\x33\x00\x01\x35\x35\x35\x35"
		"\x7f\xff\x00\x00\x00\x00\x00\x00\x17\x00\x00\xff\xff\xff\xdd\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x2d\xeb\x00\x64\x00\x00\x00\x77\x01"
		"\x34\x3d\x3d\x01\x3d\x3d\x3d\x5c\x3d\x3e\xc2\x3d\x3d\x3d\x1f\x3d"
		"\x3d\x3d\x3d\x3d\x3d\x3d\x3d\x3d\x3d\x3d\x3d\x00\x00\x00\x00\x21"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x48\x5b\xff\x00\xbf\xbf\xb9\xbf\xbf\xbf"
		"\x20\x00\x00\x41\x41\x41\x7a\x64\x35\x35\xfc\x35\x50\x42\x00\x01"
		"\x01\x01\x01\x01\x00\x00\x00\x34\x68\x68\x75\x7a\x69\x35\x35\x35"
		"\x1c\x00\x80\x35\x0f\x8a\x35\x35";

	rfl = fy_reflection_from_packed_blob(blob, sizeof(blob) - 1, NULL);
	if (rfl)
		free(fy_reflection_generate_c_string(rfl,
				FYCGF_INDENT_TAB | FYCGF_COMMENT_YAML));
	fy_reflection_destroy(rfl);
}
END_TEST

/*
 * Test: gh#331 - a blob record has an empty name, so its generated name
 * is the prefix alone. The C generator must not read past the name.
 */
START_TEST(fuzz_issue_331_packed_blob_empty_record_name_repro)
{
	struct fy_reflection *rfl;
	static const char blob[] =
		"\x46\x59\x50\x47\x66\x00\x00\x00\x00\x00\x00\x00\x13\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x54\x00\x00\x00\x00\x00\x00\x01\x73"
		"\x00\x00\x00\x00\x00\x00\x00\x47\x00\x00\x00\x00\x00\x00\x00\x66"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\xfb\x02\x34\x80\x00\x00\x00\x35\x35\x16\x16\x16\x16\x16\x16\x16"
		"\x16\x16\x16\x16\x16\xaf\x16\x16\x16\x16\x16\x16\x16\x16\x16\x16"
		"\x16\x16\x16\x16\x16\x16\x16\x16\x16\x16\x16\x16\x16\x16\x16\x16"
		"\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5"
		"\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5"
		"\xb5\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x6a\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x15\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x74\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x8b\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x74\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x5a\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x67\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x20\x00\x75\x75"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\xff\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x0a\x0a"
		"\x0a\x0a\x7c\x0d\x0a\x0a\x0a\x2d\x2d\x2d\xf5\xad\xad\x85\x52\x7a"
		"\x35\x31\x35\x35\x35\x00\x00\x68\x5c\x41\x41\xc3\x47\x66\x66\x35"
		"\x66\x35\x35\xca\x35\x31\x76\x75\x00\x00\x7f\xff\x35\x35\xdd\xdd"
		"\xdd\x00\x02\x00\x00\xdd\x00\x00\x00\x00\x54\x00\x00\x00\x00\x00"
		"\x00\x01\x73\x00\x00\x00\x00\x00\x00\x00\x47\x00\xff\x00\x00\x00"
		"\x00\x00\x66\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\xfb\x02\x34\x80\x00\x00\x00\x35\x35\x16\x16\x16\x16"
		"\x16\x16\x16\x65\x65\x65\x65\x65\x6e\x6e\x6e\x6e\x6e\x6e\x6e\x6e"
		"\x6e\x6e\x6e\x6e\x6e\x6e\x6e\x6e\x6e\x6e\x6e\x6e\x6e\x6e\x6e\x6e"
		"\x6e\x6e\x6e\x6e\x6e\x6e\x6e\x65\x65\x65\x65\x65\x65\x64\x65\x66"
		"\x65\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65"
		"\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x36\x35\x35\x31"
		"\x35\x35\x35\x00\x00\x68\x41\x41\x41\xc3\x47\x41\x50\x66\x7a\x68"
		"\x68\x8a\x7a\x77\x65\x00\xff\x00\x68\x68\x75";

	rfl = fy_reflection_from_packed_blob(blob, sizeof(blob) - 1, NULL);
	if (rfl)
		free(fy_reflection_generate_c_string(rfl,
				FYCGF_INDENT_TAB | FYCGF_COMMENT_YAML));
	fy_reflection_destroy(rfl);
}
END_TEST

/*
 * Test: gh#332 - a blob supplies a decl id that overflows when the id
 * offset is subtracted from it.
 */
START_TEST(fuzz_issue_332_packed_blob_decl_id_overflow_repro)
{
	struct fy_reflection *rfl;
	static const char blob[] =
		"\x46\x59\x50\x47\x66\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x47\x00\x00\x00\x00\x00\x00\x01\x73"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\xfb\x01\x35\x80\x02\x00\x00\x35\x35\x35\x35\xdd\xd9\x5e\x5e\x5e"
		"\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e"
		"\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e"
		"\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x5e\x80\x00\x00\x00\x66\x66\x64\x41"
		"\x66\x66\x41\x68\x00\x00\x31\x35\x35\x35\x00\x90\x90\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x43\x41\x41\x41"
		"\x41\x02\x00\x41\x41\x41\x41\x41\x41\x41\x35\x00\x00\x68\x5c\x41"
		"\x41\xc3\x47\x66\x66\x35\x66\x66\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x45\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x4b\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x34\x41\x41\x41\x41\x41\x41\x41\x41\x41\x4f\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\xb1\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x42\x27\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x7e\xb2\x90\x35\x35\x38"
		"\x35\xad\xad\xad\xad\x65\x52\x7a\x35\x31\x35\x35\x34\xed\x00\x68"
		"\x5c\x50\x46\x59\x50\x66\x41\x68\x00\x00\x31\x35\x35\x35\x00\x00"
		"\x68\x5c\x20\x41\xc3\x47\x66\x59\x35\x66\x66\x66\x6a\xff\xff\x41"
		"\xff\x65\x36\x35\x35\x31\x35\x35\x35\x00\x00\x68\x41\x41\x41\xc3"
		"\x47\x41\x50\x66\x7a\x68\x68\x8a\x7a\x77\x65\x00\x00\x00\x68\x68"
		"\x75";

	rfl = fy_reflection_from_packed_blob(blob, sizeof(blob) - 1, NULL);
	fy_reflection_destroy(rfl);
}
END_TEST

/*
 * Test: gh#333 - a blob supplies a type id that overflows when the id
 * offset is subtracted from it.
 */
START_TEST(fuzz_issue_333_packed_blob_type_id_overflow_repro)
{
	struct fy_reflection *rfl;
	static const char blob[] =
		"\x46\x59\x50\x47\x3e\x00\x02\x00\x01\x00\x00\x00\x00\x1f\x00\x0e"
		"\x00\x00\x00\x00\x00\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00\x0f"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x80\x00\xff\xff\x80\x00\x04\x41\x50"
		"\x5e\x18\xf9\xf0\x00\x80\x00\x00\x03\x79\x01\x00\x00\x01\x01\x01"
		"\x01\x01\x01\x01\x01\x01\x01\x01\x01\x06\x01\x01\x01\x01\x01\x01"
		"\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01"
		"\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01"
		"\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01"
		"\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x8e\x41\x41\x41\x72\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x58\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x50\x41\x30\x41\x41\x41\x41\x41\x41\x41\x41\x42\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x7e"
		"\xb2\x90\x35\x35\x38\x35\xad\xad\xad\xad\x65\x52\x7a\x35\x31\x35"
		"\x35\x34\xed\x00\x68\x5c\x41\x41\xc3\x47\x66\x66\x41\x50\x46\x00"
		"\x00\xff\xff\x68\x00\x00\x31\x35\x35\x35\x00\x00\x68\x5c\x20\x41"
		"\xc3\x47\x7a\x00\x00\x00\x01\x66\x6a\xff\xff\x2e\xff\x65\x36\x35"
		"\x35\x31\x35\x35\x35\x00\x00\x68\x41\x41\x40\xc3\x47\x41\x50\x66"
		"\x7a\x68\x58\x8a\x7a\x77\x65\x00\x20\x00\x68\x68\x75";

	rfl = fy_reflection_from_packed_blob(blob, sizeof(blob) - 1, NULL);
	fy_reflection_destroy(rfl);
}
END_TEST

/*
 * Test: gh#334 - a blob function declaration has no return type. The
 * name generation must not read before the empty return type.
 */
START_TEST(fuzz_issue_334_packed_blob_empty_function_return_repro)
{
	struct fy_reflection *rfl;
	static const char blob[] =
		"\x46\x59\x50\x47\x66\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x54\x00\x00\x00\x00\x00\x00\x01\x73"
		"\x00\x00\x00\x00\x00\x00\x00\x47\x00\x00\x00\x00\x00\x00\x00\x66"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\xfb\x02\x34\x80\x00\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x16\x16"
		"\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5"
		"\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5"
		"\xb5\x7e\x7e\x7e\x7e\x7e\x16\x7e\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x75"
		"\x75\x75\x75\x95\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\xe8\x03\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x74"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x28\x0a\x00\x10\x00\x00\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x08\x0a\x0a\x0a\x0a\x0a\x0d\x82\x00\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x24\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x6b\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x00\x00\x35\x35\x16\x16\x16\x7e\x16\x16\x16\x16\x16\x16"
		"\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e"
		"\x7e\xfe\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e"
		"\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x64\x7e\x7e\x7e\x7e\x7e"
		"\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e"
		"\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e"
		"\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x84\x15\x15\x59\x59\x59\x59\x59\x59"
		"\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59"
		"\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a"
		"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x89\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x8a\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x59\x59\x59\x59"
		"\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x5a\x59"
		"\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x00\x00"
		"\x00\x20\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59\x59"
		"\x59\x59\x59\x59\x15\x15\x15\x15\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x00\x04\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x39\x30\x33\x31\x37\x32\x38\x39"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x66\x66\x41\x50\x46\x00\x00\x66\x41\x68\x00\x00"
		"\x31\x35\x35\x35\x00\x00\x68\x5c\x20\x41\xc3\x47\x66\x66\x35\x66"
		"\x66\x66\x6a\xff\xff\x41\xff\x65\x65\x65\x65\x65\x65\x65\x65\x65"
		"\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x34\x4e\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x79\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75\x75"
		"\x75\x75\x75\xb9\xb9\xb9\xb9\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x20\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x4b\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x42\x42\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x00\xeb\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x90\x90\x90\xb2\x90\x35\x35\x38\x35\xad"
		"\xad\xad\xad\x65\x52\x7a\x35\x31\x35\x35\x35\x00\x00\x68\x5c\x41"
		"\x41\xc3\x47\x66\x66\x35\x66\x35\x35\xca\x35\x31\x76\x75\x00\x00"
		"\x7f\xff\x35\x35\xdd\xdd\xdd\x00\x02\x00\x00\xdd\xc1\xdd\xdd\xdd"
		"\xa6\x66\x66\x66\x35\x41\x50\x46\x59\x50\x47\x66\x00\x41\x41\x41"
		"\x7a\x41\xbe\x50\x64\x41\x66\x66\x41\x68\x00\x00\x68\x68\x75\x7a"
		"\x7d\x65\x00\x00\x00\x68\x41\x41\x41\xc3\x47\x66\x66\x66\x6a\xff"
		"\xff\x41\xff\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65"
		"\x65\x65\x65\x65\x64\x65\x66\x65\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x65\x65\x65\x65\x65\x65\x65\x65\x65"
		"\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65"
		"\x65\x65\x65\x36\x35\x35\x31\x35\x35\x35\x00\x00\x68\x41\x41\x41"
		"\xc3\x47\x41\x50\x66\x7a\x68\x68\x8a\x7a\x77\x65\x00\xff\x00\x68"
		"\x68\x75";

	rfl = fy_reflection_from_packed_blob(blob, sizeof(blob) - 1, NULL);
	fy_reflection_destroy(rfl);
}
END_TEST

/*
 * Test: gh#335 - a blob string does not terminate in the string table.
 */
START_TEST(fuzz_issue_335_packed_blob_unterminated_string_repro)
{
	struct fy_reflection *rfl;
	static const char blob[] =
		"\x46\x59\x50\x47\x66\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x54\x00\x00\x00\x00\x00\x00\x01\x73"
		"\x00\x00\x00\x00\x00\x00\x00\x47\x00\x00\x00\x00\x00\x00\x00\x66"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\xfb\x02\x34\x80\x00\x00\x00\x35\x35\x35\x35\x35\x35\x35\x7e\x7e"
		"\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x69\x7e\x7e\x7e\x7e\x7e\x7e\x7e"
		"\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x7e"
		"\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5"
		"\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5\xb5"
		"\xb5\x7e\x7e\x7e\x7e\x7e\x7e\x7e\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x00\x64\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x12\x12\x12\x12\x12\x12"
		"\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57"
		"\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x57\x65"
		"\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65"
		"\x65\x65\x65\x65\x65\x65\x65\x66\x65\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
		"\x41\x41\x41\x41\x41\x41\x41\x41\x65\x65\x65\x65\x65\x65\x65\x65"
		"\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65\x65"
		"\x65\x65\x65\x65\x36\x35\x35\x31\x35\x35\x35\x00\x00\x68\x41\x41"
		"\x41\xc3\x47\x41\x50\x66\x7a\x68\x68\x8a\x7a\x77\x65\x00\xff\x00"
		"\x68\x68\x75";

	rfl = fy_reflection_from_packed_blob(blob, sizeof(blob) - 1, NULL);
	fy_reflection_destroy(rfl);
}
END_TEST

/*
 * Test: gh#338 - a blob declaration comment offset points past the end
 * of the string table.
 */
START_TEST(fuzz_issue_338_packed_blob_comment_past_strtab_repro)
{
	struct fy_reflection *rfl;
	static const char blob[] =
		"\x46\x59\x50\x47\x01\x00\x01\x00\x00\x00\xff\x00\x00\x82\x9a\x01"
		"\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x01"
		"\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x07"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x01\x01\x01\x01\x01\x01\x01\x00"
		"\xd6\x00\x01\x01\x18\x01\x01\x2a\x2a\x00\x01\x01";

	rfl = fy_reflection_from_packed_blob(blob, sizeof(blob) - 1, NULL);
	fy_reflection_destroy(rfl);
}
END_TEST

#endif /* HAVE_REFLECTION */

void libfyaml_case_fuzzing(struct fy_check_suite *cs)
{
	struct fy_check_testcase *ctc;

	ctc = fy_check_suite_add_test_case(cs, "fuzzing");

	/*
	 * The deep flow reproducers do much recursive work under ASAN and
	 * debug builds; give the case a generous wall-clock budget
	 * (CK_TIMEOUT_MULTIPLIER still scales it).
	 */
	fy_check_testcase_set_timeout(ctc, 120);

	fy_check_testcase_add_test(ctc, fuzz_resolve_aliases_stars_amps);
	fy_check_testcase_add_test(ctc, fuzz_resolve_disable_buffering_colon_star);
	fy_check_testcase_add_test(ctc, fuzz_resolve_aliases_special_chars);
	fy_check_testcase_add_test(ctc, fuzz_emit_null_document);
#ifdef __linux__
	fy_check_testcase_add_test(ctc, fuzz_node_build_fp_invalid_data);
#endif
	fy_check_testcase_add_test(ctc, fuzz_recursive_resolve_anchors_aliases);
#ifdef __linux__
	fy_check_testcase_add_test(ctc, fuzz_node_build_fp_emoji_invalid_utf8);
#endif
	fy_check_testcase_add_test(ctc, fuzz_emit_event_invalid_scalar_style);
	fy_check_testcase_add_test(ctc, fuzz_recursive_resolve_binary_data);
	fy_check_testcase_add_test(ctc, fuzz_node_by_path_ypath_sequence);
	fy_check_testcase_add_test(ctc, fuzz_token_iter_getc_after_read);
	fy_check_testcase_add_test(ctc, fuzz_resolve_collect_diag_colon_star);
	fy_check_testcase_add_test(ctc, fuzz_collect_diag_colon_star);
	fy_check_testcase_add_test(ctc, fuzz_emit_strip_empty_kv_many_modes);
	fy_check_testcase_add_test(ctc, fuzz_path_expr_triple_star);
	fy_check_testcase_add_test(ctc, fuzz_resolve_recursive_star_slash_bang);
	fy_check_testcase_add_test(ctc, fuzz_node_by_path_star_at);
	fy_check_testcase_add_test(ctc, fuzz_node_by_path_double_star_at_emit);
	fy_check_testcase_add_test(ctc, fuzz_node_by_path_star_underscore_sequence);
	fy_check_testcase_add_test(ctc, fuzz_ypath_aliases_complex_pattern);
	fy_check_testcase_add_test(ctc, fuzz_sloppy_flow_disable_flags);
	fy_check_testcase_add_test(ctc, fuzz_disable_recycling_ypath_aliases);
#ifdef __linux__
	fy_check_testcase_add_test(ctc, fuzz_build_from_fp_sloppy_flow);
	fy_check_testcase_add_test(ctc, fuzz_node_build_fp_flow_mapping);
#endif
	fy_check_testcase_add_test(ctc, fuzz_complex_anchors_recursive_buffering);
	fy_check_testcase_add_test(ctc, fuzz_node_build_string_block_scalar);
	fy_check_testcase_add_test(ctc, fuzz_node_by_path_dot_slash_emit);
	fy_check_testcase_add_test(ctc, fuzz_node_by_path_parens_sequence);
	fy_check_testcase_add_test(ctc, fuzz_parse_comments_recursive_emit);
#ifdef __linux__
	fy_check_testcase_add_test(ctc, fuzz_build_from_fp_recursive_duplicate_keys);
#endif
#if defined(__linux__)
	fy_check_testcase_add_test(ctc, fuzz_parser_event_loop_block_scalar);
#endif
	fy_check_testcase_add_test(ctc, fuzz_collect_diag_parse_comments_sequence);
	fy_check_testcase_add_test(ctc, fuzz_node_compare_clone_quoted_scalar);
	fy_check_testcase_add_test(ctc, fuzz_parse_comment_with_override);
	fy_check_testcase_add_test(ctc, fuzz_stream_end_token_replay_cleanup);
	fy_check_testcase_add_test(ctc, fuzz_issue_298_list_del_uaf_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_299_token_unref_uaf_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_300_token_unref_overflow_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_305_malloc_allocator_free_repro);
#if defined(__linux__)
	fy_check_testcase_add_test(ctc, fuzz_issue_309_scalar_path_key_repro);
#endif
	fy_check_testcase_add_test(ctc, fuzz_resolve_document_ypath_null_alias);
	fy_check_testcase_add_test(ctc, fuzz_path_expr_build_bang_triple_star);
	fy_check_testcase_add_test(ctc, fuzz_resolve_recursive_ypath_aliases_dup_keys);
	fy_check_testcase_add_test(ctc, fuzz_disable_recycling_ypath_aliases_dup_keys);
	fy_check_testcase_add_test(ctc, fuzz_parse_comments_emit_many_modes);
	fy_check_testcase_add_test(ctc, fuzz_resolve_recursive_ypath_dup_keys_emit_fp);
#if defined(__linux__)
	fy_check_testcase_add_test(ctc, fuzz_build_from_fp_sloppy_recursive_ypath_aliases);
	fy_check_testcase_add_test(ctc, fuzz_build_from_fp_ypath_aliases_recursive);
#endif
	fy_check_testcase_add_test(ctc, fuzz_node_get_type_uaf);
	fy_check_testcase_add_test(ctc, fuzz_emit_node_to_string_uaf);
	fy_check_testcase_add_test(ctc, fuzz_document_iterator_cleanup_uaf);
	fy_check_testcase_add_test(ctc, fuzz_emit_mapping_memory_leak);
	fy_check_testcase_add_test(ctc, fuzz_walk_number_to_expr_nonfinite);
	fy_check_testcase_add_test(ctc, fuzz_path_expr_execute_oom);
	fy_check_testcase_add_test(ctc, fuzz_anchor_accel_cleanup_scalar_borrowed_input);
	fy_check_testcase_add_test(ctc, fuzz_anchor_accel_cleanup_mapping_borrowed_input);
#if defined(__linux__)
	fy_check_testcase_add_test(ctc, fuzz_parser_streaming_alias_collection_state_mask);
#endif
	fy_check_testcase_add_test(ctc, fuzz_issue_317_path_parse_quiet_repro);
#if defined(__linux__)
	fy_check_testcase_add_test(ctc, fuzz_issue_318_reader_pull_uaf_repro);
#endif
	fy_check_testcase_add_test(ctc, fuzz_issue_320_unicode_escape_shift_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_323_deep_flow_mapping_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_325_merge_key_empty_argument_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_327_alias_node_is_null_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_336_thread_pool_key_leak_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_337_token_iter_restart_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_339_shared_document_state_merge_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_347_merge_alias_path_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_349_block_scalar_indicator_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_350_empty_clipped_block_scalar_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_351_block_scalar_header_nul_repro);
#if defined(__linux__)
	fy_check_testcase_add_test(ctc, fuzz_issue_340_alias_path_end_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_344_deep_primitive_dump_repro);
#endif
#ifdef HAVE_REFLECTION
	fy_check_testcase_add_test(ctc, fuzz_issue_341_dependent_type_cycle_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_342_null_type_name_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_348_enum_int64_max_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_319_packed_blob_short_read_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_321_packed_blob_bad_id_size_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_322_packed_blob_bad_region_size_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_324_packed_blob_missing_decl_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_326_packed_blob_record_no_decl_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_330_packed_blob_type_no_decl_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_331_packed_blob_empty_record_name_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_332_packed_blob_decl_id_overflow_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_333_packed_blob_type_id_overflow_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_334_packed_blob_empty_function_return_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_335_packed_blob_unterminated_string_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_338_packed_blob_comment_past_strtab_repro);
#endif
}
