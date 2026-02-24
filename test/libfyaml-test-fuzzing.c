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

#include "fy-check.h"

/* Test: parse "*********&&&&&&" with RESOLVE_DOCUMENT | YPATH_ALIASES */
START_TEST(fuzz_resolve_aliases_stars_amps)
{
	char buf[] = "*********&&&&&&";
	struct fy_parse_cfg cfg = {0};
	cfg.flags = FYPCF_RESOLVE_DOCUMENT | FYPCF_YPATH_ALIASES;
	fy_document_destroy(fy_document_build_from_string(&cfg, buf, FY_NT));
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

static void fuzz_dump_testsuite_event(struct fy_parser *fyp, struct fy_event *fye)
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


void libfyaml_case_fuzzing(struct fy_check_suite *cs)
{
	struct fy_check_testcase *ctc;

	ctc = fy_check_suite_add_test_case(cs, "fuzzing");

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
	fy_check_testcase_add_test(ctc, fuzz_parse_comment_with_override);
}
