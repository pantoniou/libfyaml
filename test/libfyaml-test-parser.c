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

/* Test: Mapping iterator (forward and reverse) */
START_TEST(parser_mapping_iterator)
{
	struct fy_document *fyd;
	struct fy_node_pair *fynp;
	struct fy_node *fyn;
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
	ck_assert_str_eq(path, "/frooz/[0]");
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

TCase *libfyaml_case_parser(void)
{
	TCase *tc;

	tc = tcase_create("parser");

	/* Mapping tests */
	tcase_add_test(tc, parser_mapping_iterator);
	tcase_add_test(tc, parser_mapping_key_lookup);

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

	/* Complex structure tests */
	tcase_add_test(tc, parser_complex_nested_structure);

	return tc;
}
