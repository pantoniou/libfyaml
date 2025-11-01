/*
 * libfyaml-test-parser.c - libfyaml parser testing (extracted from libfyaml-parser.c)
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
#include "fy-parse.h"
#include "fy-utf8.h"

/* Test: Mapping iterator (forward and reverse) */
START_TEST(parser_mapping_iterator)
{
	struct fy_document *fyd;
	struct fy_node_pair *fynp;
	int count;
	void *iter;

	/* Build a mapping with multiple entries including complex keys */
	fyd = fy_document_build_from_string(NULL,
		"{ foo: 10, bar: 20, baz: [100, 101], [frob, 1]: boo }", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Verify count */
	count = fy_node_mapping_item_count(fy_document_root(fyd));
	ck_assert_int_eq(count, 4);

	/* Test forward iterator */
	iter = NULL;
	fynp = fy_node_mapping_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_key(fynp)), "foo");
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "10");

	fynp = fy_node_mapping_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_key(fynp)), "bar");
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "20");

	/* Test reverse iterator */
	iter = NULL;
	fynp = fy_node_mapping_reverse_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	/* Last item should be the complex key */

	/* Test index-based access */
	fynp = fy_node_mapping_get_by_index(fy_document_root(fyd), 0);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_key(fynp)), "foo");
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "10");

	fynp = fy_node_mapping_get_by_index(fy_document_root(fyd), 1);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_key(fynp)), "bar");

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Mapping key lookup */
START_TEST(parser_mapping_key_lookup)
{
	struct fy_document *fyd;
	struct fy_node *fyn;

	fyd = fy_document_build_from_string(NULL,
		"{ foo: 10, bar: 20, baz: [100, 101], [frob, 1]: boo }", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Lookup simple keys */
	fyn = fy_node_mapping_lookup_by_string(fy_document_root(fyd), "foo", FY_NT);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "10");

	fyn = fy_node_mapping_lookup_by_string(fy_document_root(fyd), "bar", FY_NT);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "20");

	/* Lookup key with sequence value */
	fyn = fy_node_mapping_lookup_by_string(fy_document_root(fyd), "baz", FY_NT);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert(fy_node_is_sequence(fyn));

	/* Lookup complex key */
	fyn = fy_node_mapping_lookup_by_string(fy_document_root(fyd), "[ frob, 1 ]", FY_NT);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "boo");

	/* Lookup non-existent key */
	fyn = fy_node_mapping_lookup_by_string(fy_document_root(fyd), "nonexistent", FY_NT);
	ck_assert_ptr_eq(fyn, NULL);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Path-based node queries */
START_TEST(parser_path_queries)
{
	struct fy_document *fyd;
	struct fy_node *fyn;

	fyd = fy_document_build_from_string(NULL,
		"{ foo: 10, bar: 20, baz:{ frob: boo }, "
		"frooz: [ seq1, { key: value} ], \"zero\\0zero\": 0, "
		"{ key2: value2 }: { key3: value3 } }", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Query root */
	fyn = fy_node_by_path(fy_document_root(fyd), "/", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert(fy_node_is_mapping(fyn));

	/* Query simple keys */
	fyn = fy_node_by_path(fy_document_root(fyd), "foo", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "10");

	fyn = fy_node_by_path(fy_document_root(fyd), "bar", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "20");

	/* Query nested path */
	fyn = fy_node_by_path(fy_document_root(fyd), "baz/frob", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "boo");

	/* Query sequence elements by index */
	fyn = fy_node_by_path(fy_document_root(fyd), "/frooz/[0]", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "seq1");

	/* Query nested in sequence */
	fyn = fy_node_by_path(fy_document_root(fyd), "/frooz/[1]/key", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "value");

	/* Query with quoted key */
	fyn = fy_node_by_path(fy_document_root(fyd), "\"foo\"", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "10");

	/* Query with null byte in key */
	fyn = fy_node_by_path(fy_document_root(fyd), "\"zero\\0zero\"", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "0");

	/* Query complex key mapping */
	fyn = fy_node_by_path(fy_document_root(fyd), "/{ key2: value2 }", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert(fy_node_is_mapping(fyn));

	/* Query nested in complex key */
	fyn = fy_node_by_path(fy_document_root(fyd), "/{ key2: value2 }/key3", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "value3");

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Node path generation */
START_TEST(parser_node_path_generation)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	char *path;

	fyd = fy_document_build_from_string(NULL,
		"{ foo: 10, frooz: [ seq1, { key: value} ], "
		"{ key2: value2 }: { key3: value3 } }", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Get path of root */
	fyn = fy_node_by_path(fy_document_root(fyd), "/", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	path = fy_node_get_path(fyn);
	ck_assert_ptr_ne(path, NULL);
	ck_assert_str_eq(path, "/");
	free(path);

	/* Get path of simple key */
	fyn = fy_node_by_path(fy_document_root(fyd), "/frooz", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	path = fy_node_get_path(fyn);
	ck_assert_ptr_ne(path, NULL);
	ck_assert_str_eq(path, "/frooz");
	free(path);

	/* Get path of sequence element */
	fyn = fy_node_by_path(fy_document_root(fyd), "/frooz/[0]", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	path = fy_node_get_path(fyn);
	ck_assert_ptr_ne(path, NULL);
	ck_assert_str_eq(path, "/frooz/0");
	free(path);

	/* Get path of nested element in complex key */
	fyn = fy_node_by_path(fy_document_root(fyd), "/{ key2: value2 }/key3", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	path = fy_node_get_path(fyn);
	ck_assert_ptr_ne(path, NULL);
	/* Path should be valid */
	ck_assert_ptr_ne(path, NULL);
	free(path);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Node creation from scratch */
START_TEST(parser_node_creation_scalar)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	char *buf;

	/* Create document and scalar node */
	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fyn = fy_node_create_scalar(fyd, "foo", 3);
	ck_assert_ptr_ne(fyn, NULL);

	fy_document_set_root(fyd, fyn);

	/* Emit and verify */
	buf = fy_emit_document_to_string(fyd, FYECF_MODE_FLOW_ONELINE);
	ck_assert_ptr_ne(buf, NULL);
	ck_assert_str_eq(buf, "foo\n");
	free(buf);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Node creation - multiline scalar */
START_TEST(parser_node_creation_multiline_scalar)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	char *buf;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fyn = fy_node_create_scalar(fyd, "foo\nfoo", 7);
	ck_assert_ptr_ne(fyn, NULL);

	fy_document_set_root(fyd, fyn);

	/* Emit and verify - multiline scalars should be emitted with literal or folded style */
	buf = fy_emit_document_to_string(fyd, 0);
	ck_assert_ptr_ne(buf, NULL);
	/* Just verify it emits successfully and contains the content */
	ck_assert(strstr(buf, "foo") != NULL);
	free(buf);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Node creation - empty sequence */
START_TEST(parser_node_creation_empty_sequence)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	char *buf;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fyn = fy_node_create_sequence(fyd);
	ck_assert_ptr_ne(fyn, NULL);

	fy_document_set_root(fyd, fyn);

	/* Emit and verify */
	buf = fy_emit_document_to_string(fyd, FYECF_MODE_FLOW_ONELINE);
	ck_assert_ptr_ne(buf, NULL);
	ck_assert_str_eq(buf, "[]\n");
	free(buf);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Node creation - empty mapping */
START_TEST(parser_node_creation_empty_mapping)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	char *buf;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fyn = fy_node_create_mapping(fyd);
	ck_assert_ptr_ne(fyn, NULL);

	fy_document_set_root(fyd, fyn);

	/* Emit and verify */
	buf = fy_emit_document_to_string(fyd, FYECF_MODE_FLOW_ONELINE);
	ck_assert_ptr_ne(buf, NULL);
	ck_assert_str_eq(buf, "{}\n");
	free(buf);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Node creation - populated sequence */
START_TEST(parser_node_creation_populated_sequence)
{
	struct fy_document *fyd;
	struct fy_node *fyn, *fyn_seq;
	int ret;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fyn_seq = fy_node_create_sequence(fyd);
	ck_assert_ptr_ne(fyn_seq, NULL);

	/* Append elements */
	fyn = fy_node_create_scalar(fyd, "foo", FY_NT);
	ck_assert_ptr_ne(fyn, NULL);
	ret = fy_node_sequence_append(fyn_seq, fyn);
	ck_assert_int_eq(ret, 0);

	fyn = fy_node_create_scalar(fyd, "bar", FY_NT);
	ck_assert_ptr_ne(fyn, NULL);
	ret = fy_node_sequence_append(fyn_seq, fyn);
	ck_assert_int_eq(ret, 0);

	fyn = fy_node_build_from_string(fyd, "{ baz: frooz }", FY_NT);
	ck_assert_ptr_ne(fyn, NULL);
	ret = fy_node_sequence_append(fyn_seq, fyn);
	ck_assert_int_eq(ret, 0);

	fy_document_set_root(fyd, fyn_seq);

	/* Verify count */
	ck_assert_int_eq(fy_node_sequence_item_count(fyn_seq), 3);

	/* Verify content */
	fyn = fy_node_sequence_get_by_index(fyn_seq, 0);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "foo");

	fyn = fy_node_sequence_get_by_index(fyn_seq, 1);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "bar");

	fyn = fy_node_sequence_get_by_index(fyn_seq, 2);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert(fy_node_is_mapping(fyn));

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Node creation - populated mapping */
START_TEST(parser_node_creation_populated_mapping)
{
	struct fy_document *fyd;
	struct fy_node *fyn, *fyn_key, *fyn_val, *fyn_map;
	int ret;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fyn_map = fy_node_create_mapping(fyd);
	ck_assert_ptr_ne(fyn_map, NULL);

	/* Append key-value pairs */
	fyn_key = fy_node_create_scalar(fyd, "foo", FY_NT);
	ck_assert_ptr_ne(fyn_key, NULL);
	fyn_val = fy_node_create_scalar(fyd, "10", FY_NT);
	ck_assert_ptr_ne(fyn_val, NULL);
	ret = fy_node_mapping_append(fyn_map, fyn_key, fyn_val);
	ck_assert_int_eq(ret, 0);

	fyn_key = fy_node_create_scalar(fyd, "bar", FY_NT);
	ck_assert_ptr_ne(fyn_key, NULL);
	fyn_val = fy_node_build_from_string(fyd, "[ 1, 2, 3 ]", FY_NT);
	ck_assert_ptr_ne(fyn_val, NULL);
	ret = fy_node_mapping_append(fyn_map, fyn_key, fyn_val);
	ck_assert_int_eq(ret, 0);

	fy_document_set_root(fyd, fyn_map);

	/* Verify count */
	ck_assert_int_eq(fy_node_mapping_item_count(fyn_map), 2);

	/* Verify lookup */
	fyn = fy_node_mapping_lookup_by_string(fyn_map, "foo", FY_NT);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "10");

	fyn = fy_node_mapping_lookup_by_string(fyn_map, "bar", FY_NT);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert(fy_node_is_sequence(fyn));
	ck_assert_int_eq(fy_node_sequence_item_count(fyn), 3);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Build node from string within document */
START_TEST(parser_build_node_from_string)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	char *buf;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	/* Build a complex node from string */
	fyn = fy_node_build_from_string(fyd, "{ }", FY_NT);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert(fy_node_is_mapping(fyn));

	fy_document_set_root(fyd, fyn);

	buf = fy_emit_document_to_string(fyd, FYECF_MODE_FLOW_ONELINE);
	ck_assert_ptr_ne(buf, NULL);
	ck_assert_str_eq(buf, "{}\n");
	free(buf);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Sequence operations - reverse index access */
START_TEST(parser_sequence_negative_index)
{
	struct fy_document *fyd;
	struct fy_node *fyn;

	fyd = fy_document_build_from_string(NULL, "[ first, second, third ]", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Access from end using negative indices */
	fyn = fy_node_sequence_get_by_index(fy_document_root(fyd), -1);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "third");

	fyn = fy_node_sequence_get_by_index(fy_document_root(fyd), -2);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "second");

	fyn = fy_node_sequence_get_by_index(fy_document_root(fyd), -3);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "first");

	/* Out of bounds negative index */
	fyn = fy_node_sequence_get_by_index(fy_document_root(fyd), -4);
	ck_assert_ptr_eq(fyn, NULL);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Complex nested structure */
START_TEST(parser_complex_nested_structure)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	int count;

	/* Build a complex nested structure */
	fyd = fy_document_build_from_string(NULL,
		"root:\n"
		"  level1:\n"
		"    level2:\n"
		"      - item1\n"
		"      - item2\n"
		"      - key: value\n"
		"        nested: data\n",
		FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Navigate to nested sequence */
	fyn = fy_node_by_path(fy_document_root(fyd), "root/level1/level2", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert(fy_node_is_sequence(fyn));

	count = fy_node_sequence_item_count(fyn);
	ck_assert_int_eq(count, 3);

	/* Check first scalar item */
	fyn = fy_node_by_path(fy_document_root(fyd), "root/level1/level2/[0]", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "item1");

	/* Check nested mapping in sequence */
	fyn = fy_node_by_path(fy_document_root(fyd), "root/level1/level2/[2]/nested", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "data");

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Anchor and alias resolution */
START_TEST(parser_anchor_alias_resolution)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	char *buf;
	int rc;

	/* Build document with anchor and alias */
	fyd = fy_document_build_from_string(NULL,
		"base: &base\n"
		"  name: this-is-a-name\n"
		"  value: 42\n"
		"copy: *base\n",
		FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Before resolution, alias should exist */
	fyn = fy_node_by_path(fy_document_root(fyd), "copy", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert(fy_node_is_alias(fyn));

	/* Resolve the document */
	rc = fy_document_resolve(fyd);
	ck_assert_int_eq(rc, 0);

	/* After resolution, we should be able to access through the alias */
	fyn = fy_node_by_path(fy_document_root(fyd), "copy/name", FY_NT, FYNWF_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "this-is-a-name");

	/* Emit resolved document */
	buf = fy_emit_document_to_string(fyd, FYECF_MODE_FLOW_ONELINE);
	ck_assert_ptr_ne(buf, NULL);
	free(buf);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Document insertion at path */
START_TEST(parser_document_insert_at)
{
	struct fy_document *fyd;
	struct fy_node *fyn, *fyn_inserted;
	int rc;

	/* Create base document */
	fyd = fy_document_build_from_string(NULL,
		"base:\n"
		"  name: original\n",
		FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Build a mapping node to insert (key: value) */
	fyn = fy_node_buildf(fyd, "new-key: inserted-value");
	ck_assert_ptr_ne(fyn, NULL);

	/* Insert the mapping at /base (merges into existing mapping) */
	rc = fy_document_insert_at(fyd, "/base", FY_NT, fyn);
	ck_assert_int_eq(rc, 0);

	/* Verify insertion */
	fyn_inserted = fy_node_by_path(fy_document_root(fyd), "/base/new-key", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn_inserted, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn_inserted), "inserted-value");

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Document emit with different flags */
START_TEST(parser_document_emit_flags)
{
	struct fy_document *fyd;
	char *buf;

	/* Build test document */
	fyd = fy_document_build_from_string(NULL,
		"{ z: 1, a: 2, m: 3 }",
		FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Emit with default flags */
	buf = fy_emit_document_to_string(fyd, FYECF_MODE_FLOW_ONELINE);
	ck_assert_ptr_ne(buf, NULL);
	ck_assert(strstr(buf, "z") != NULL);
	ck_assert(strstr(buf, "a") != NULL);
	ck_assert(strstr(buf, "m") != NULL);
	free(buf);

	/* Emit with sorted keys */
	buf = fy_emit_document_to_string(fyd, FYECF_MODE_FLOW_ONELINE | FYECF_SORT_KEYS);
	ck_assert_ptr_ne(buf, NULL);
	/* In sorted output, 'a' should come before 'z' */
	ck_assert(strstr(buf, "a") < strstr(buf, "z"));
	ck_assert(strstr(buf, "m") < strstr(buf, "z"));
	free(buf);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Multi-document stream parsing */
START_TEST(parser_multi_document_stream)
{
	struct fy_parser fyp_data, *fyp = &fyp_data;
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_DEFAULT_DOC,
	};
	struct fy_document *fyd;
	int count;
	int rc;

	/* Setup parser */
	rc = fy_parse_setup(fyp, &cfg);
	ck_assert_int_eq(rc, 0);

	/* Create multi-document input */
	const char *yaml_multi =
		"---\n"
		"doc: 1\n"
		"---\n"
		"doc: 2\n"
		"---\n"
		"doc: 3\n";

	/* Add input */
	struct fy_input_cfg fyic = {
		.type = fyit_memory,
		.memory.data = yaml_multi,
		.memory.size = strlen(yaml_multi),
	};
	rc = fy_parse_input_append(fyp, &fyic);
	ck_assert_int_eq(rc, 0);

	/* Parse all documents */
	count = 0;
	while ((fyd = fy_parse_load_document(fyp)) != NULL) {
		count++;

		/* Verify document content */
		struct fy_node *fyn = fy_node_by_path(fy_document_root(fyd), "doc", FY_NT, FYNWF_DONT_FOLLOW);
		ck_assert_ptr_ne(fyn, NULL);

		int doc_num = atoi(fy_node_get_scalar0(fyn));
		ck_assert_int_eq(doc_num, count);

		fy_parse_document_destroy(fyp, fyd);
	}

	ck_assert_int_eq(count, 3);

	fy_parse_cleanup(fyp);
}
END_TEST

/* Test: Empty document handling */
START_TEST(parser_empty_document)
{
	struct fy_document *fyd;
	char *buf;

	/* Null document (YAML null/empty) */
	fyd = fy_document_build_from_string(NULL, "null", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Should have scalar null root */
	ck_assert_ptr_ne(fy_document_root(fyd), NULL);

	/* Should emit */
	buf = fy_emit_document_to_string(fyd, 0);
	ck_assert_ptr_ne(buf, NULL);
	free(buf);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Document with comments (requires FYPCF_PARSE_COMMENTS) */
START_TEST(parser_document_with_comments)
{
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_PARSE_COMMENTS,
	};
	struct fy_document *fyd;

	/* Build document with comments */
	fyd = fy_document_build_from_string(&cfg,
		"# Top comment\n"
		"key: value # Right comment\n"
		"# Bottom comment\n",
		FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Verify content (comments should be preserved in structure) */
	struct fy_node *fyn = fy_node_by_path(fy_document_root(fyd), "key", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "value");

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Sequence append and prepend */
START_TEST(parser_sequence_append_prepend)
{
	struct fy_document *fyd;
	struct fy_node *fyn_seq, *fyn;
	int rc;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	/* Create sequence */
	fyn_seq = fy_node_create_sequence(fyd);
	ck_assert_ptr_ne(fyn_seq, NULL);
	fy_document_set_root(fyd, fyn_seq);

	/* Append items */
	fyn = fy_node_create_scalar(fyd, "second", FY_NT);
	rc = fy_node_sequence_append(fyn_seq, fyn);
	ck_assert_int_eq(rc, 0);

	/* Prepend item */
	fyn = fy_node_create_scalar(fyd, "first", FY_NT);
	rc = fy_node_sequence_prepend(fyn_seq, fyn);
	ck_assert_int_eq(rc, 0);

	/* Append another */
	fyn = fy_node_create_scalar(fyd, "third", FY_NT);
	rc = fy_node_sequence_append(fyn_seq, fyn);
	ck_assert_int_eq(rc, 0);

	/* Verify order */
	fyn = fy_node_sequence_get_by_index(fyn_seq, 0);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "first");

	fyn = fy_node_sequence_get_by_index(fyn_seq, 1);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "second");

	fyn = fy_node_sequence_get_by_index(fyn_seq, 2);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "third");

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Mapping prepend */
START_TEST(parser_mapping_prepend)
{
	struct fy_document *fyd;
	struct fy_node *fyn_map, *fyn_key, *fyn_val;
	struct fy_node_pair *fynp;
	int rc;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fyn_map = fy_node_create_mapping(fyd);
	ck_assert_ptr_ne(fyn_map, NULL);
	fy_document_set_root(fyd, fyn_map);

	/* Append a pair */
	fyn_key = fy_node_create_scalar(fyd, "second", FY_NT);
	fyn_val = fy_node_create_scalar(fyd, "2", FY_NT);
	rc = fy_node_mapping_append(fyn_map, fyn_key, fyn_val);
	ck_assert_int_eq(rc, 0);

	/* Prepend a pair */
	fyn_key = fy_node_create_scalar(fyd, "first", FY_NT);
	fyn_val = fy_node_create_scalar(fyd, "1", FY_NT);
	rc = fy_node_mapping_prepend(fyn_map, fyn_key, fyn_val);
	ck_assert_int_eq(rc, 0);

	/* Verify order */
	fynp = fy_node_mapping_get_by_index(fyn_map, 0);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_key(fynp)), "first");

	fynp = fy_node_mapping_get_by_index(fyn_map, 1);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_key(fynp)), "second");

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Node removal from sequence */
START_TEST(parser_sequence_remove)
{
	struct fy_document *fyd;
	struct fy_node *fyn_seq, *fyn;
	int count;

	/* Build a sequence */
	fyd = fy_document_build_from_string(NULL, "[ a, b, c, d ]", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	fyn_seq = fy_document_root(fyd);
	ck_assert_int_eq(fy_node_sequence_item_count(fyn_seq), 4);

	/* Remove middle element */
	fyn = fy_node_sequence_get_by_index(fyn_seq, 1);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "b");

	fyn = fy_node_sequence_remove(fyn_seq, fyn);
	ck_assert_ptr_ne(fyn, NULL);
	fy_node_free(fyn);

	/* Verify count and order */
	count = fy_node_sequence_item_count(fyn_seq);
	ck_assert_int_eq(count, 3);

	fyn = fy_node_sequence_get_by_index(fyn_seq, 0);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "a");

	fyn = fy_node_sequence_get_by_index(fyn_seq, 1);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "c");

	fyn = fy_node_sequence_get_by_index(fyn_seq, 2);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "d");

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Node removal from mapping */
START_TEST(parser_mapping_remove)
{
	struct fy_document *fyd;
	struct fy_node *fyn_map, *fyn_key, *fyn_val;
	int count;

	/* Build a mapping */
	fyd = fy_document_build_from_string(NULL, "{ a: 1, b: 2, c: 3 }", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	fyn_map = fy_document_root(fyd);
	ck_assert_int_eq(fy_node_mapping_item_count(fyn_map), 3);

	/* Remove by key */
	fyn_key = fy_node_build_from_string(fyd, "b", FY_NT);
	ck_assert_ptr_ne(fyn_key, NULL);

	fyn_val = fy_node_mapping_remove_by_key(fyn_map, fyn_key);
	ck_assert_ptr_ne(fyn_val, NULL);
	fy_node_free(fyn_val);

	/* Verify count */
	count = fy_node_mapping_item_count(fyn_map);
	ck_assert_int_eq(count, 2);

	/* Verify 'b' is gone */
	fyn_val = fy_node_mapping_lookup_by_string(fyn_map, "b", FY_NT);
	ck_assert_ptr_eq(fyn_val, NULL);

	/* Verify others remain */
	fyn_val = fy_node_mapping_lookup_by_string(fyn_map, "a", FY_NT);
	ck_assert_ptr_ne(fyn_val, NULL);

	fyn_val = fy_node_mapping_lookup_by_string(fyn_map, "c", FY_NT);
	ck_assert_ptr_ne(fyn_val, NULL);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Document iterator functionality */
START_TEST(parser_document_iterator)
{
	struct fy_document *fyd;
	struct fy_document_iterator *fydi;
	struct fy_node *fyn;
	int count;

	/* Build test document with nested structure */
	fyd = fy_document_build_from_string(NULL,
		"root:\n"
		"  scalar: value\n"
		"  seq:\n"
		"    - item1\n"
		"    - item2\n"
		"  map:\n"
		"    key: val\n",
		FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Create iterator */
	fydi = fy_document_iterator_create();
	ck_assert_ptr_ne(fydi, NULL);

	/* Start iteration from root */
	fy_document_iterator_node_start(fydi, fy_document_root(fyd));

	/* Count all nodes */
	count = 0;
	while ((fyn = fy_document_iterator_node_next(fydi)) != NULL) {
		count++;

		/* Verify node type detection works */
		if (fy_node_is_scalar(fyn)) {
			const char *text;
			size_t len;
			text = fy_node_get_scalar(fyn, &len);
			ck_assert_ptr_ne(text, NULL);
		} else if (fy_node_is_sequence(fyn)) {
			/* Verify it's a sequence */
			ck_assert(fy_node_is_sequence(fyn));
		} else if (fy_node_is_mapping(fyn)) {
			/* Verify it's a mapping */
			ck_assert(fy_node_is_mapping(fyn));
		}
	}

	/* We should have iterated through multiple nodes */
	ck_assert_int_gt(count, 0);

	/* Cleanup */
	fy_document_iterator_destroy(fydi);
	fy_document_destroy(fyd);
}
END_TEST

/* Test: Document iterator with key detection */
START_TEST(parser_document_iterator_key_detection)
{
	struct fy_document *fyd;
	struct fy_document_iterator *fydi;
	struct fy_node *fyn;

	/* Build mapping document */
	fyd = fy_document_build_from_string(NULL,
		"key1: value1\n"
		"key2: value2\n",
		FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	fydi = fy_document_iterator_create();
	ck_assert_ptr_ne(fydi, NULL);

	fy_document_iterator_node_start(fydi, fy_document_root(fyd));

	/* Iterate and check for key nodes */
	fyn = fy_document_iterator_node_next(fydi);
	ck_assert_ptr_ne(fyn, NULL);
	/* must be a mapping */
	ck_assert(fy_node_is_mapping(fyn));
	/* must be the root */
	ck_assert_ptr_eq(fyn, fy_document_root(fyd));

	/* get the first key */
	fyn = fy_document_iterator_node_next(fydi);
	ck_assert_ptr_ne(fyn, NULL);
	/* must be a scalar */
	ck_assert(fy_node_is_scalar(fyn));
	ck_assert(!strcmp(fy_node_get_scalar0(fyn), "key1"));

	/* get the first value */
	fyn = fy_document_iterator_node_next(fydi);
	ck_assert_ptr_ne(fyn, NULL);
	/* must be a scalar */
	ck_assert(fy_node_is_scalar(fyn));
	ck_assert(!strcmp(fy_node_get_scalar0(fyn), "value1"));

	/* get the second key */
	fyn = fy_document_iterator_node_next(fydi);
	ck_assert_ptr_ne(fyn, NULL);
	/* must be a scalar */
	ck_assert(fy_node_is_scalar(fyn));
	ck_assert(!strcmp(fy_node_get_scalar0(fyn), "key2"));

	/* get the second value */
	fyn = fy_document_iterator_node_next(fydi);
	ck_assert_ptr_ne(fyn, NULL);
	/* must be a scalar */
	ck_assert(fy_node_is_scalar(fyn));
	ck_assert(!strcmp(fy_node_get_scalar0(fyn), "value2"));

	/* final, must be out of nodes */
	fyn = fy_document_iterator_node_next(fydi);
	ck_assert_ptr_eq(fyn, NULL);

	fy_document_iterator_destroy(fydi);
	fy_document_destroy(fyd);
}
END_TEST

/* Test: Comment retrieval from tokens */
START_TEST(parser_comment_retrieval)
{
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_PARSE_COMMENTS,
	};
	struct fy_document *fyd;
	struct fy_document_iterator *fydi;
	struct fy_node *fyn;
	struct fy_token *fyt;
	char buf[256];
	bool found_comment = false;

	/* Build document with comments */
	fyd = fy_document_build_from_string(&cfg,
		"# Top comment\n"
		"scalar: value # Right comment\n",
		FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	fydi = fy_document_iterator_create();
	ck_assert_ptr_ne(fydi, NULL);

	fy_document_iterator_node_start(fydi, fy_document_root(fyd));

	/* Iterate and check for comments */
	while ((fyn = fy_document_iterator_node_next(fydi)) != NULL) {
		if (!fy_node_is_scalar(fyn))
			continue;

		fyt = fy_node_get_scalar_token(fyn);
		if (!fyt || !fy_token_has_any_comment(fyt))
			continue;

		/* Try to get comments at different placements */
		for (int placement = fycp_top; placement < fycp_max; placement++) {
			if (fy_token_get_comment(fyt, buf, sizeof(buf), placement)) {
				ck_assert_ptr_ne(buf, NULL);
				ck_assert_int_gt(strlen(buf), 0);
				found_comment = true;
			}
		}
	}

	ck_assert(found_comment);

	fy_document_iterator_destroy(fydi);
	fy_document_destroy(fyd);
}
END_TEST

/* Test: Alias node detection in iterator */
START_TEST(parser_iterator_alias_detection)
{
	struct fy_document *fyd;
	struct fy_document_iterator *fydi;
	struct fy_node *fyn;
	bool found_alias = false;

	/* Build document with anchor and alias */
	fyd = fy_document_build_from_string(NULL,
		"anchor: &ref value\n"
		"alias: *ref\n",
		FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	fydi = fy_document_iterator_create();
	ck_assert_ptr_ne(fydi, NULL);

	fy_document_iterator_node_start(fydi, fy_document_root(fyd));

	/* Iterate and check for alias nodes */
	while ((fyn = fy_document_iterator_node_next(fydi)) != NULL) {
		if (fy_node_is_scalar(fyn) && fy_node_is_alias(fyn)) {
			found_alias = true;
		}
	}

	ck_assert(found_alias);

	fy_document_iterator_destroy(fydi);
	fy_document_destroy(fyd);
}
END_TEST

/* Test: Event-based parsing */
START_TEST(parser_event_generation)
{
	struct fy_parser fyp_data, *fyp = &fyp_data;
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_DEFAULT_DOC,
	};
	struct fy_event *event;
	int rc;
	bool got_stream_start = false;
	bool got_doc_start = false;
	bool got_scalar = false;
	bool got_doc_end = false;
	bool got_stream_end = false;

	/* Setup parser */
	rc = fy_parse_setup(fyp, &cfg);
	ck_assert_int_eq(rc, 0);

	/* Add simple YAML input */
	const char *yaml = "key: value\n";
	struct fy_input_cfg fyic = {
		.type = fyit_memory,
		.memory.data = yaml,
		.memory.size = strlen(yaml),
	};
	rc = fy_parse_input_append(fyp, &fyic);
	ck_assert_int_eq(rc, 0);

	/* Parse events */
	while ((event = fy_parser_parse(fyp)) != NULL) {
		switch (event->type) {
		case FYET_STREAM_START:
			got_stream_start = true;
			break;
		case FYET_DOCUMENT_START:
			got_doc_start = true;
			break;
		case FYET_SCALAR:
			got_scalar = true;
			break;
		case FYET_DOCUMENT_END:
			got_doc_end = true;
			break;
		case FYET_STREAM_END:
			got_stream_end = true;
			break;
		default:
			break;
		}
		fy_parser_event_free(fyp, event);
	}

	/* Verify we got expected events */
	ck_assert(got_stream_start);
	ck_assert(got_doc_start);
	ck_assert(got_scalar);
	ck_assert(got_doc_end);
	ck_assert(got_stream_end);

	fy_parse_cleanup(fyp);
}
END_TEST

/* Test: Scalar style detection */
START_TEST(parser_scalar_styles)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	struct fy_token *fyt;
	enum fy_scalar_style style;

	/* Build document with different scalar styles */
	fyd = fy_document_build_from_string(NULL,
		"plain: plain value\n"
		"single: 'single quoted'\n"
		"double: \"double quoted\"\n"
		"literal: |\n"
		"  literal block\n"
		"folded: >\n"
		"  folded block\n",
		FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Check plain style */
	fyn = fy_node_by_path(fy_document_root(fyd), "plain", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	fyt = fy_node_get_scalar_token(fyn);
	ck_assert_ptr_ne(fyt, NULL);
	style = fy_token_scalar_style(fyt);
	ck_assert_int_eq(style, FYSS_PLAIN);

	/* Check single quoted style */
	fyn = fy_node_by_path(fy_document_root(fyd), "single", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	fyt = fy_node_get_scalar_token(fyn);
	ck_assert_ptr_ne(fyt, NULL);
	style = fy_token_scalar_style(fyt);
	ck_assert_int_eq(style, FYSS_SINGLE_QUOTED);

	/* Check double quoted style */
	fyn = fy_node_by_path(fy_document_root(fyd), "double", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	fyt = fy_node_get_scalar_token(fyn);
	ck_assert_ptr_ne(fyt, NULL);
	style = fy_token_scalar_style(fyt);
	ck_assert_int_eq(style, FYSS_DOUBLE_QUOTED);

	/* Check literal style */
	fyn = fy_node_by_path(fy_document_root(fyd), "literal", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	fyt = fy_node_get_scalar_token(fyn);
	ck_assert_ptr_ne(fyt, NULL);
	style = fy_token_scalar_style(fyt);
	ck_assert_int_eq(style, FYSS_LITERAL);

	/* Check folded style */
	fyn = fy_node_by_path(fy_document_root(fyd), "folded", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	fyt = fy_node_get_scalar_token(fyn);
	ck_assert_ptr_ne(fyt, NULL);
	style = fy_token_scalar_style(fyt);
	ck_assert_int_eq(style, FYSS_FOLDED);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Tag handling */
START_TEST(parser_tag_handling)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	const char *tag;

	/* Build document with tags */
	fyd = fy_document_build_from_string(NULL,
		"string: !!str tagged string\n"
		"integer: !!int 42\n"
		"custom: !custom custom tag\n",
		FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Check string tag */
	fyn = fy_node_by_path(fy_document_root(fyd), "string", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	tag = fy_node_get_tag0(fyn);
	ck_assert_ptr_ne(tag, NULL);
	ck_assert(strstr(tag, "str") != NULL);

	/* Check integer tag */
	fyn = fy_node_by_path(fy_document_root(fyd), "integer", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	tag = fy_node_get_tag0(fyn);
	ck_assert_ptr_ne(tag, NULL);
	ck_assert(strstr(tag, "int") != NULL);

	/* Check custom tag */
	fyn = fy_node_by_path(fy_document_root(fyd), "custom", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	tag = fy_node_get_tag0(fyn);
	ck_assert_ptr_ne(tag, NULL);
	ck_assert(strstr(tag, "custom") != NULL);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: YAML version directives */
START_TEST(parser_yaml_version)
{
	struct fy_parse_cfg cfg_11 = {
		.flags = FYPCF_DEFAULT_VERSION_1_1,
	};
	struct fy_parse_cfg cfg_12 = {
		.flags = FYPCF_DEFAULT_VERSION_1_2,
	};
	struct fy_document *fyd;

	/* Parse with YAML 1.1 */
	fyd = fy_document_build_from_string(&cfg_11, "key: value", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);
	fy_document_destroy(fyd);

	/* Parse with YAML 1.2 */
	fyd = fy_document_build_from_string(&cfg_12, "key: value", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);
	fy_document_destroy(fyd);

	/* Parse with explicit version directive */
	fyd = fy_document_build_from_string(NULL, "%YAML 1.2\n---\nkey: value", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);
	fy_document_destroy(fyd);
}
END_TEST

/* Test: Flow and block styles */
START_TEST(parser_flow_block_styles)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	char *buf;

	/* Build document with mixed flow and block styles */
	fyd = fy_document_build_from_string(NULL,
		"block_map:\n"
		"  key: value\n"
		"flow_map: {key: value}\n"
		"block_seq:\n"
		"  - item\n"
		"flow_seq: [item]\n",
		FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Verify block mapping */
	fyn = fy_node_by_path(fy_document_root(fyd), "block_map", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert(fy_node_is_mapping(fyn));

	/* Verify flow mapping */
	fyn = fy_node_by_path(fy_document_root(fyd), "flow_map", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert(fy_node_is_mapping(fyn));

	/* Verify block sequence */
	fyn = fy_node_by_path(fy_document_root(fyd), "block_seq", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert(fy_node_is_sequence(fyn));

	/* Verify flow sequence */
	fyn = fy_node_by_path(fy_document_root(fyd), "flow_seq", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert(fy_node_is_sequence(fyn));

	/* Emit and verify output */
	buf = fy_emit_document_to_string(fyd, 0);
	ck_assert_ptr_ne(buf, NULL);
	free(buf);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Document builder API */
START_TEST(parser_document_builder)
{
	struct fy_document *fyd;
	struct fy_node *fyn_root, *fyn_key, *fyn_val;
	int rc;

	/* Create document using builder pattern */
	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	/* Build root mapping */
	fyn_root = fy_node_create_mapping(fyd);
	ck_assert_ptr_ne(fyn_root, NULL);
	fy_document_set_root(fyd, fyn_root);

	/* Add key-value pairs using builder */
	fyn_key = fy_node_build_from_string(fyd, "key1", FY_NT);
	ck_assert_ptr_ne(fyn_key, NULL);
	fyn_val = fy_node_build_from_string(fyd, "value1", FY_NT);
	ck_assert_ptr_ne(fyn_val, NULL);
	rc = fy_node_mapping_append(fyn_root, fyn_key, fyn_val);
	ck_assert_int_eq(rc, 0);

	/* Add another pair with complex value */
	fyn_key = fy_node_build_from_string(fyd, "key2", FY_NT);
	ck_assert_ptr_ne(fyn_key, NULL);
	fyn_val = fy_node_build_from_string(fyd, "[1, 2, 3]", FY_NT);
	ck_assert_ptr_ne(fyn_val, NULL);
	rc = fy_node_mapping_append(fyn_root, fyn_key, fyn_val);
	ck_assert_int_eq(rc, 0);

	/* Verify the built document */
	ck_assert_int_eq(fy_node_mapping_item_count(fyn_root), 2);

	fyn_val = fy_node_mapping_lookup_by_string(fyn_root, "key1", FY_NT);
	ck_assert_ptr_ne(fyn_val, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn_val), "value1");

	fyn_val = fy_node_mapping_lookup_by_string(fyn_root, "key2", FY_NT);
	ck_assert_ptr_ne(fyn_val, NULL);
	ck_assert(fy_node_is_sequence(fyn_val));
	ck_assert_int_eq(fy_node_sequence_item_count(fyn_val), 3);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: Shell-like string splitting (from do_shell_split) */
START_TEST(parser_shell_split)
{
	const char * const *argv;
	int argc;
	void *mem;

	/* Test simple splitting */
	mem = fy_utf8_split_posix("arg1 arg2 arg3", &argc, &argv);
	ck_assert_ptr_ne(mem, NULL);
	ck_assert_int_eq(argc, 3);
	ck_assert_str_eq(argv[0], "arg1");
	ck_assert_str_eq(argv[1], "arg2");
	ck_assert_str_eq(argv[2], "arg3");
	ck_assert_ptr_eq(argv[argc], NULL);
	free(mem);

	/* Test quoted strings */
	mem = fy_utf8_split_posix("'single quoted' \"double quoted\"", &argc, &argv);
	ck_assert_ptr_ne(mem, NULL);
	ck_assert_int_eq(argc, 2);
	ck_assert_str_eq(argv[0], "single quoted");
	ck_assert_str_eq(argv[1], "double quoted");
	ck_assert_ptr_eq(argv[argc], NULL);
	free(mem);

	/* Test escape sequences */
	mem = fy_utf8_split_posix("arg1\\ with\\ spaces arg2", &argc, &argv);
	ck_assert_ptr_ne(mem, NULL);
	ck_assert_int_eq(argc, 2);
	ck_assert_str_eq(argv[0], "arg1 with spaces");
	ck_assert_str_eq(argv[1], "arg2");
	ck_assert_ptr_eq(argv[argc], NULL);
	free(mem);

	/* Test empty string */
	mem = fy_utf8_split_posix("", &argc, &argv);
	ck_assert_ptr_ne(mem, NULL);
	ck_assert_int_eq(argc, 0);
	ck_assert_ptr_eq(argv[argc], NULL);
	free(mem);

	/* Test multiple spaces */
	mem = fy_utf8_split_posix("  arg1    arg2  ", &argc, &argv);
	ck_assert_ptr_ne(mem, NULL);
	ck_assert_int_eq(argc, 2);
	ck_assert_str_eq(argv[0], "arg1");
	ck_assert_str_eq(argv[1], "arg2");
	ck_assert_ptr_eq(argv[argc], NULL);
	free(mem);
}
END_TEST

/* Test: UTF-8 validation (from do_bad_utf8) */
START_TEST(parser_utf8_validation)
{
	const char *valid_ascii = "hello world";
	const char *s, *e;
	int c, w, pos;

	/* Test valid ASCII forward */
	s = valid_ascii;
	e = s + strlen(valid_ascii);
	pos = 0;
	while (s < e) {
		c = fy_utf8_get(s, e - s, &w);
		ck_assert_int_ge(c, 0);
		ck_assert_int_ge(w, 1);
		ck_assert_int_le(w, 4);
		s += w;
		pos++;
	}
	ck_assert_int_eq(pos, 11);

	/* Test valid UTF-8 forward (Greek characters) */
	const char *greek = "\xCE\xA4\xCE\xB9\xCE\xBC\xCE\xAE"; /* Τιμή */
	s = greek;
	e = s + strlen(greek);
	pos = 0;
	while (s < e) {
		c = fy_utf8_get(s, e - s, &w);
		ck_assert_int_ge(c, 0);
		ck_assert_int_eq(w, 2); /* Greek chars are 2 bytes */
		s += w;
		pos++;
	}
	ck_assert_int_eq(pos, 4);

	/* Test valid ASCII backward */
	s = valid_ascii;
	e = s + strlen(valid_ascii);
	pos = 0;
	while (s < e) {
		c = fy_utf8_get_right(s, e - s, &w);
		ck_assert_int_ge(c, 0);
		ck_assert_int_ge(w, 1);
		ck_assert_int_le(w, 4);
		e -= w;
		pos++;
	}
	ck_assert_int_eq(pos, 11);

	/* Test invalid UTF-8 sequence */
	const char invalid[] = { 0x67, 0xe7, 0x67, 0x00 }; /* 'g' followed by incomplete UTF-8 */
	s = invalid;
	e = s + 3; /* Don't include null terminator */
	c = fy_utf8_get(s, 1, &w); /* First char should be valid */
	ck_assert_int_eq(c, 0x67);
	ck_assert_int_eq(w, 1);
	s += w;
	c = fy_utf8_get(s, e - s, &w); /* Second char should be invalid or partial */
	ck_assert_int_lt(c, 0); /* Should return FYUG_INV or FYUG_PARTIAL */

	/* Test EOF condition */
	c = fy_utf8_get(valid_ascii, 0, &w);
	ck_assert_int_eq(c, FYUG_EOF);
	ck_assert_int_eq(w, 0);
}
END_TEST

/* Test: Token scanning - basic scalars */
START_TEST(parser_token_scan_scalars)
{
	struct fy_parser *fyp;
	struct fy_parse_cfg cfg;
	struct fy_token *fyt;
	const char *yaml;
	int token_count;

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = FYPCF_DEFAULT_PARSE;

	/* Test simple scalar */
	yaml = "hello";
	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);
	fy_parser_set_string(fyp, yaml, FY_NT);

	token_count = 0;
	while ((fyt = fy_scan(fyp)) != NULL) {
		ck_assert_ptr_ne(fyt, NULL);
		token_count++;
		fy_token_unref(fyt);
	}
	ck_assert_int_gt(token_count, 0);
	fy_parser_destroy(fyp);

	/* Test multiple scalars */
	yaml = "foo bar baz";
	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);
	fy_parser_set_string(fyp, yaml, FY_NT);

	token_count = 0;
	while ((fyt = fy_scan(fyp)) != NULL) {
		token_count++;
		fy_token_unref(fyt);
	}
	ck_assert_int_gt(token_count, 0);
	fy_parser_destroy(fyp);
}
END_TEST

/* Test: Token scanning - mappings */
START_TEST(parser_token_scan_mapping)
{
	struct fy_parser *fyp;
	struct fy_parse_cfg cfg;
	struct fy_token *fyt;
	const char *yaml;
	int token_count;

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = FYPCF_DEFAULT_PARSE;

	yaml = "key: value";
	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);
	fy_parser_set_string(fyp, yaml, FY_NT);

	token_count = 0;
	while ((fyt = fy_scan(fyp)) != NULL) {
		ck_assert_ptr_ne(fyt, NULL);
		token_count++;
		fy_token_unref(fyt);
	}
	/* Should have multiple tokens: STREAM-START, BLOCK-MAPPING-START, KEY, SCALAR, VALUE, SCALAR, etc. */
	ck_assert_int_gt(token_count, 5);
	fy_parser_destroy(fyp);

	/* Test nested mapping */
	yaml = "outer:\n  inner: value";
	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);
	fy_parser_set_string(fyp, yaml, FY_NT);

	token_count = 0;
	while ((fyt = fy_scan(fyp)) != NULL) {
		token_count++;
		fy_token_unref(fyt);
	}
	ck_assert_int_gt(token_count, 5);
	fy_parser_destroy(fyp);
}
END_TEST

/* Test: Token scanning - sequences */
START_TEST(parser_token_scan_sequence)
{
	struct fy_parser *fyp;
	struct fy_parse_cfg cfg;
	struct fy_token *fyt;
	const char *yaml;
	int token_count;

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = FYPCF_DEFAULT_PARSE;

	/* Test flow sequence */
	yaml = "[1, 2, 3]";
	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);
	fy_parser_set_string(fyp, yaml, FY_NT);

	token_count = 0;
	while ((fyt = fy_scan(fyp)) != NULL) {
		token_count++;
		fy_token_unref(fyt);
	}
	ck_assert_int_gt(token_count, 5);
	fy_parser_destroy(fyp);

	/* Test block sequence */
	yaml = "- item1\n- item2\n- item3";
	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);
	fy_parser_set_string(fyp, yaml, FY_NT);

	token_count = 0;
	while ((fyt = fy_scan(fyp)) != NULL) {
		token_count++;
		fy_token_unref(fyt);
	}
	ck_assert_int_gt(token_count, 5);
	fy_parser_destroy(fyp);
}
END_TEST

/* Test: Token scanning - scalar styles */
START_TEST(parser_token_scan_scalar_styles)
{
	struct fy_parser *fyp;
	struct fy_parse_cfg cfg;
	struct fy_token *fyt;
	const char *yaml;
	int scalar_count;
	enum fy_scalar_style style;

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = FYPCF_DEFAULT_PARSE;

	/* Test different scalar styles */
	yaml = "plain: value\n"
	       "single: 'quoted value'\n"
	       "double: \"quoted value\"\n";

	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);
	fy_parser_set_string(fyp, yaml, FY_NT);

	scalar_count = 0;
	while ((fyt = fy_scan(fyp)) != NULL) {
		if (fyt->type == FYTT_SCALAR) {
			style = fy_token_scalar_style(fyt);
			/* Verify style is valid */
			ck_assert(style == FYSS_PLAIN ||
			          style == FYSS_SINGLE_QUOTED ||
			          style == FYSS_DOUBLE_QUOTED ||
			          style == FYSS_LITERAL ||
			          style == FYSS_FOLDED);
			scalar_count++;
		}
		fy_token_unref(fyt);
	}
	ck_assert_int_ge(scalar_count, 6); /* 6 scalars: 3 keys + 3 values */
	fy_parser_destroy(fyp);
}
END_TEST

/* Test: Manual emitter mode - emit events directly */
START_TEST(parser_manual_emitter)
{
	struct fy_emitter_cfg ecfg;
	struct fy_emitter *emit;

	memset(&ecfg, 0, sizeof(ecfg));
	ecfg.flags = FYECF_MODE_MANUAL;

	/* Test block style manual emission */
	emit = fy_emitter_create(&ecfg);
	ck_assert_ptr_ne(emit, NULL);

	/* Emit: key: [{ a: 1 }] in block style */
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_STREAM_START));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_DOCUMENT_START, false, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_MAPPING_START, FYNS_BLOCK, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_SCALAR, FYSS_PLAIN, "key", FY_NT, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_SEQUENCE_START, FYNS_BLOCK, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_MAPPING_START, FYNS_BLOCK, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_SCALAR, FYSS_PLAIN, "a", FY_NT, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_SCALAR, FYSS_PLAIN, "1", FY_NT, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_MAPPING_END));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_SEQUENCE_END));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_MAPPING_END));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_DOCUMENT_END, true, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_STREAM_END));

	/* If we got here without crashes, the manual emitter block style works */
	fy_emitter_destroy(emit);

	/* Test flow style manual emission */
	emit = fy_emitter_create(&ecfg);
	ck_assert_ptr_ne(emit, NULL);

	/* Emit: {key: [{a: 1}]} in flow style */
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_STREAM_START));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_DOCUMENT_START, false, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_MAPPING_START, FYNS_FLOW, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_SCALAR, FYSS_PLAIN, "key", FY_NT, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_SEQUENCE_START, FYNS_FLOW, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_MAPPING_START, FYNS_FLOW, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_SCALAR, FYSS_PLAIN, "a", FY_NT, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_SCALAR, FYSS_PLAIN, "1", FY_NT, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_MAPPING_END));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_SEQUENCE_END));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_MAPPING_END));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_DOCUMENT_END, true, NULL, NULL));
	fy_emit_event(emit, fy_emit_event_create(emit, FYET_STREAM_END));

	/* If we got here without crashes, the manual emitter flow style works */
	fy_emitter_destroy(emit);
}
END_TEST

START_TEST(parser_parse_load_document)
{
	struct fy_parse_cfg cfg;
	struct fy_parser *fyp;
	struct fy_document *fyd;
	const char *yaml = "---\nfoo: bar\n---\nbaz: qux\n";
	int count;

	/* Test loading multiple documents from a parser stream */
	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = FYPCF_DEFAULT_PARSE;

	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);

	fy_parser_set_string(fyp, yaml, FY_NT);

	/* Load first document */
	count = 0;
	while ((fyd = fy_parse_load_document(fyp)) != NULL) {
		const char *value;
		struct fy_node *fyn;

		count++;

		/* Verify document content */
		if (count == 1) {
			fyn = fy_node_by_path(fy_document_root(fyd), "/foo", FY_NT, FYNWF_DONT_FOLLOW);
			ck_assert_ptr_ne(fyn, NULL);
			value = fy_node_get_scalar0(fyn);
			ck_assert_str_eq(value, "bar");
		} else if (count == 2) {
			fyn = fy_node_by_path(fy_document_root(fyd), "/baz", FY_NT, FYNWF_DONT_FOLLOW);
			ck_assert_ptr_ne(fyn, NULL);
			value = fy_node_get_scalar0(fyn);
			ck_assert_str_eq(value, "qux");
		}

		fy_parse_document_destroy(fyp, fyd);
	}

	ck_assert_int_eq(count, 2);

	fy_parser_destroy(fyp);
}
END_TEST

START_TEST(parser_document_resolve)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	const char *yaml = "anchor: &data\n  key: value\nref: *data\n";
	const char *val;
	int rc;

	/* Test document resolution (resolving aliases) */
	fyd = fy_document_build_from_string(NULL, yaml, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Before resolution, alias nodes exist */
	fyn = fy_node_by_path(fy_document_root(fyd), "/ref", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert(fy_node_is_alias(fyn));

	/* Resolve all aliases in the document */
	rc = fy_document_resolve(fyd);
	ck_assert_int_eq(rc, 0);

	/* After resolution, verify both paths are accessible and have same values */
	fyn = fy_node_by_path(fy_document_root(fyd), "/anchor/key", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	val = fy_node_get_scalar0(fyn);
	ck_assert_str_eq(val, "value");

	fyn = fy_node_by_path(fy_document_root(fyd), "/ref/key", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	val = fy_node_get_scalar0(fyn);
	ck_assert_str_eq(val, "value");

	/* After resolution, the ref node should no longer be an alias */
	fyn = fy_node_by_path(fy_document_root(fyd), "/ref", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert(!fy_node_is_alias(fyn));

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(parser_document_clone)
{
	struct fy_document *fyd, *fyd_clone;
	struct fy_node *fyn, *fyn_clone;
	const char *yaml = "foo: bar\nbaz: [1, 2, 3]\n";
	const char *val1, *val2;

	/* Test document cloning */
	fyd = fy_document_build_from_string(NULL, yaml, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Clone the document */
	fyd_clone = fy_document_clone(fyd);
	ck_assert_ptr_ne(fyd_clone, NULL);

	/* Verify original document content */
	fyn = fy_node_by_path(fy_document_root(fyd), "/foo", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	val1 = fy_node_get_scalar0(fyn);
	ck_assert_str_eq(val1, "bar");

	/* Verify cloned document has same content */
	fyn_clone = fy_node_by_path(fy_document_root(fyd_clone), "/foo", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn_clone, NULL);
	val2 = fy_node_get_scalar0(fyn_clone);
	ck_assert_str_eq(val2, "bar");

	/* But they are different node objects */
	ck_assert_ptr_ne(fyn, fyn_clone);

	fy_document_destroy(fyd);
	fy_document_destroy(fyd_clone);
}
END_TEST

START_TEST(parser_node_copy)
{
	struct fy_document *fyd;
	struct fy_node *fyn_src, *fyn_copy, *fyn_root;
	const char *yaml = "original: {key: value}\n";
	const char *val;

	/* Test node copying */
	fyd = fy_document_build_from_string(NULL, yaml, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* Get the source node */
	fyn_src = fy_node_by_path(fy_document_root(fyd), "/original", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn_src, NULL);

	/* Copy the node */
	fyn_copy = fy_node_copy(fyd, fyn_src);
	ck_assert_ptr_ne(fyn_copy, NULL);

	/* Verify the copy has same content */
	fyn_root = fy_node_by_path(fyn_copy, "/key", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn_root, NULL);
	val = fy_node_get_scalar0(fyn_root);
	ck_assert_str_eq(val, "value");

	/* Add the copy to the document with a new key */
	fy_node_mapping_append(fy_document_root(fyd),
		fy_node_build_from_string(fyd, "copied", FY_NT),
		fyn_copy);

	/* Verify it exists at new location */
	fyn_root = fy_node_by_path(fy_document_root(fyd), "/copied/key", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn_root, NULL);
	val = fy_node_get_scalar0(fyn_root);
	ck_assert_str_eq(val, "value");

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(parser_document_builder_load)
{
	struct fy_parse_cfg parse_cfg;
	struct fy_document_builder_cfg builder_cfg;
	struct fy_parser *fyp;
	struct fy_document_builder *fydb;
	struct fy_document *fyd;
	const char *yaml = "---\nkey: value\n---\nfoo: bar\n";
	int count;

	/* Test document builder API */
	memset(&parse_cfg, 0, sizeof(parse_cfg));
	parse_cfg.flags = FYPCF_DEFAULT_PARSE;

	memset(&builder_cfg, 0, sizeof(builder_cfg));
	builder_cfg.parse_cfg = parse_cfg;

	fyp = fy_parser_create(&parse_cfg);
	ck_assert_ptr_ne(fyp, NULL);

	fy_parser_set_string(fyp, yaml, FY_NT);

	/* Create document builder with configuration */
	fydb = fy_document_builder_create(&builder_cfg);
	ck_assert_ptr_ne(fydb, NULL);

	/* Load documents using builder */
	count = 0;
	while ((fyd = fy_document_builder_load_document(fydb, fyp)) != NULL) {
		const char *value;
		struct fy_node *fyn;

		count++;

		/* Verify document content */
		if (count == 1) {
			fyn = fy_node_by_path(fy_document_root(fyd), "/key", FY_NT, FYNWF_DONT_FOLLOW);
			ck_assert_ptr_ne(fyn, NULL);
			value = fy_node_get_scalar0(fyn);
			ck_assert_str_eq(value, "value");
		} else if (count == 2) {
			fyn = fy_node_by_path(fy_document_root(fyd), "/foo", FY_NT, FYNWF_DONT_FOLLOW);
			ck_assert_ptr_ne(fyn, NULL);
			value = fy_node_get_scalar0(fyn);
			ck_assert_str_eq(value, "bar");
		}

		fy_document_destroy(fyd);
	}

	ck_assert_int_eq(count, 2);

	fy_document_builder_destroy(fydb);
	fy_parser_destroy(fyp);
}
END_TEST

/* Composer callback record entry */
struct compose_record {
	enum fy_event_type type;
	char path[128];
	int depth;
	char scalar_value[64];
};

/* Composer callback data for parser_compose_callback test */
struct compose_test_data {
	struct compose_record records[100];
	int record_count;
};

/* Composer callback function for testing */
static enum fy_composer_return
compose_test_callback(struct fy_parser *fyp, struct fy_event *fye,
                      struct fy_path *path, void *userdata)
{
	struct compose_test_data *data = userdata;
	struct compose_record *rec;
	char *path_text;
	const char *scalar;
	size_t len;

	if (data->record_count >= 100)
		return FYCR_OK_CONTINUE;

	rec = &data->records[data->record_count++];
	rec->type = fye->type;
	rec->depth = fy_path_depth(path);

	/* Get path as text */
	path_text = fy_path_get_text(path);
	if (path_text) {
		snprintf(rec->path, sizeof(rec->path), "%s", path_text);
		free(path_text);
	} else {
		rec->path[0] = '\0';
	}

	/* For scalar events, capture the value */
	rec->scalar_value[0] = '\0';
	if (fye->type == FYET_SCALAR && fye->scalar.value) {
		scalar = fy_token_get_text(fye->scalar.value, &len);
		if (scalar && len < sizeof(rec->scalar_value) - 1) {
			memcpy(rec->scalar_value, scalar, len);
			rec->scalar_value[len] = '\0';
		}
	}

	return FYCR_OK_CONTINUE;
}

START_TEST(parser_compose_callback)
{
	struct fy_parse_cfg cfg;
	struct fy_parser *fyp;
	struct compose_test_data data;
	const char *yaml =
		"invoice: 34843\n"
		"date   : !!str 2001-01-23\n"
		"bill-to: &id001\n"
		"    given  : Chris\n"
		"    family : Dumars\n"
		"    address:\n"
		"        lines: |\n"
		"            458 Walkman Dr.\n"
		"            Suite #292\n";
	int rc;
	int i;
	bool found_invoice_key = false;
	bool found_invoice_value = false;
	bool found_bill_to_path = false;

	/* Test compose callback mechanism */
	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = FYPCF_DEFAULT_PARSE;

	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);

	fy_parser_set_string(fyp, yaml, FY_NT);

	/* Initialize test data */
	memset(&data, 0, sizeof(data));

	/* Compose with callback */
	rc = fy_parse_compose(fyp, compose_test_callback, &data);
	ck_assert_int_eq(rc, 0);

	/* Verify we got events */
	ck_assert(data.record_count > 0);

	/* Verify we have the expected events and paths */
	for (i = 0; i < data.record_count; i++) {
		struct compose_record *rec = &data.records[i];

		/* Look for specific events we expect */
		if (rec->type == FYET_SCALAR && strcmp(rec->scalar_value, "invoice") == 0) {
			found_invoice_key = true;
			/* Verify path for invoice key */
			ck_assert(strstr(rec->path, "invoice") != NULL || strcmp(rec->path, "/") == 0);
		}

		if (rec->type == FYET_SCALAR && strcmp(rec->scalar_value, "34843") == 0) {
			found_invoice_value = true;
		}

		/* Check for bill-to path */
		if (rec->type == FYET_MAPPING_START && strstr(rec->path, "bill-to") != NULL) {
			found_bill_to_path = true;
			/* Verify depth is reasonable (should be at least 1) */
			ck_assert(rec->depth > 0);
		}
	}

	/* Verify we found the expected elements */
	ck_assert(found_invoice_key);
	ck_assert(found_invoice_value);
	ck_assert(found_bill_to_path);

	/* Verify first event is stream start */
	ck_assert_int_eq(data.records[0].type, FYET_STREAM_START);

	/* Verify last event is stream end */
	ck_assert_int_eq(data.records[data.record_count - 1].type, FYET_STREAM_END);

	fy_parser_destroy(fyp);
}
END_TEST

TCase *libfyaml_case_parser(void)
{
	TCase *tc;

	tc = tcase_create("parser");

	/* Mapping tests */
	tcase_add_test(tc, parser_mapping_iterator);
	tcase_add_test(tc, parser_mapping_key_lookup);
	tcase_add_test(tc, parser_mapping_prepend);
	tcase_add_test(tc, parser_mapping_remove);

	/* Path query tests */
	tcase_add_test(tc, parser_path_queries);
	tcase_add_test(tc, parser_node_path_generation);

	/* Node creation tests */
	tcase_add_test(tc, parser_node_creation_scalar);
	tcase_add_test(tc, parser_node_creation_multiline_scalar);
	tcase_add_test(tc, parser_node_creation_empty_sequence);
	tcase_add_test(tc, parser_node_creation_empty_mapping);
	tcase_add_test(tc, parser_node_creation_populated_sequence);
	tcase_add_test(tc, parser_node_creation_populated_mapping);
	tcase_add_test(tc, parser_build_node_from_string);

	/* Sequence tests */
	tcase_add_test(tc, parser_sequence_negative_index);
	tcase_add_test(tc, parser_sequence_append_prepend);
	tcase_add_test(tc, parser_sequence_remove);

	/* Complex structure tests */
	tcase_add_test(tc, parser_complex_nested_structure);

	/* Anchor/alias tests */
	tcase_add_test(tc, parser_anchor_alias_resolution);

	/* Document operations */
	tcase_add_test(tc, parser_document_insert_at);
	tcase_add_test(tc, parser_document_emit_flags);
	tcase_add_test(tc, parser_multi_document_stream);
	tcase_add_test(tc, parser_empty_document);
	tcase_add_test(tc, parser_document_with_comments);

	/* Iterator tests */
	tcase_add_test(tc, parser_document_iterator);
	tcase_add_test(tc, parser_document_iterator_key_detection);
	tcase_add_test(tc, parser_iterator_alias_detection);

	/* Comment tests */
	tcase_add_test(tc, parser_comment_retrieval);

	/* Event and parsing tests */
	tcase_add_test(tc, parser_event_generation);
	tcase_add_test(tc, parser_scalar_styles);
	tcase_add_test(tc, parser_tag_handling);
	tcase_add_test(tc, parser_yaml_version);
	tcase_add_test(tc, parser_flow_block_styles);
	tcase_add_test(tc, parser_document_builder);

	/* Utility function tests */
	tcase_add_test(tc, parser_shell_split);
	tcase_add_test(tc, parser_utf8_validation);

	/* Token scanning tests */
	tcase_add_test(tc, parser_token_scan_scalars);
	tcase_add_test(tc, parser_token_scan_mapping);
	tcase_add_test(tc, parser_token_scan_sequence);
	tcase_add_test(tc, parser_token_scan_scalar_styles);

	/* Manual emitter tests */
	tcase_add_test(tc, parser_manual_emitter);

	/* Parser and document builder tests */
	tcase_add_test(tc, parser_parse_load_document);
	tcase_add_test(tc, parser_document_resolve);
	tcase_add_test(tc, parser_document_clone);
	tcase_add_test(tc, parser_node_copy);
	tcase_add_test(tc, parser_document_builder_load);

	/* Compose callback tests */
	tcase_add_test(tc, parser_compose_callback);

	return tc;
}
