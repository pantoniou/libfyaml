/*
 * libfyaml-test-core.c - libfyaml core testing harness
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
#include <getopt.h>
#include <unistd.h>
#include <limits.h>

#include <check.h>

#include <libfyaml.h>

START_TEST(doc_build_simple)
{
	struct fy_document *fyd;

	/* setup */
	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	/* cleanup */
	fy_document_destroy(fyd);
}
END_TEST

START_TEST(doc_build_parse_check)
{
	struct fy_document *fyd;
	char *buf;

	/* build document (with comments, newlines etc) */
	fyd = fy_document_build_from_string(NULL, "#comment\n[ 42,  \n  12 ] # comment\n", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* convert to string */
	buf = fy_emit_document_to_string(fyd, FYECF_MODE_FLOW_ONELINE);
	ck_assert_ptr_ne(buf, NULL);

	/* compare with expected result */
	ck_assert_str_eq(buf, "[42, 12]\n");

	free(buf);

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(doc_build_scalar)
{
	struct fy_document *fyd;

	/* build document */
	fyd = fy_document_build_from_string(NULL, "plain scalar # comment", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* compare with expected result */
	ck_assert_str_eq(fy_node_get_scalar0(fy_document_root(fyd)), "plain scalar");

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(doc_build_sequence)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	int count;
	void *iter;

	/* build document */
	fyd = fy_document_build_from_string(NULL, "[ 10, 11, foo ] # comment", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* check for correct count value */
	count = fy_node_sequence_item_count(fy_document_root(fyd));
	ck_assert_int_eq(count, 3);

	/* try forward iterator first */
	iter = NULL;

	/* 0 */
	fyn = fy_node_sequence_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "10");

	/* 1 */
	fyn = fy_node_sequence_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "11");

	/* 2 */
	fyn = fy_node_sequence_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "foo");

	/* final, iterator must return NULL */
	fyn = fy_node_sequence_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_eq(fyn, NULL);

	/* reverse iterator */
	iter = NULL;

	/* 2 */
	fyn = fy_node_sequence_reverse_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "foo");

	/* 1 */
	fyn = fy_node_sequence_reverse_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "11");

	/* 0 */
	fyn = fy_node_sequence_reverse_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "10");

	/* final, iterator must return NULL */
	fyn = fy_node_sequence_reverse_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_eq(fyn, NULL);

	/* do forward index based accesses */

	/* 0 */
	fyn = fy_node_sequence_get_by_index(fy_document_root(fyd), 0);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "10");

	/* 1 */
	fyn = fy_node_sequence_get_by_index(fy_document_root(fyd), 1);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "11");

	/* 2 */
	fyn = fy_node_sequence_get_by_index(fy_document_root(fyd), 2);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "foo");

	/* 3, it must not exist */
	fyn = fy_node_sequence_get_by_index(fy_document_root(fyd), 3);
	ck_assert_ptr_eq(fyn, NULL);

	/* do backward index based accesses */

	/* 2 */
	fyn = fy_node_sequence_get_by_index(fy_document_root(fyd), -1);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "foo");

	/* 1 */
	fyn = fy_node_sequence_get_by_index(fy_document_root(fyd), -2);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "11");

	/* 0 */
	fyn = fy_node_sequence_get_by_index(fy_document_root(fyd), -3);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "10");

	/* -1, it must not exist */
	fyn = fy_node_sequence_get_by_index(fy_document_root(fyd), -4);
	ck_assert_ptr_eq(fyn, NULL);

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(doc_build_mapping)
{
	struct fy_document *fyd;
	struct fy_node_pair *fynp;
	int count;
	void *iter;

	fyd = fy_document_build_from_string(NULL, "{ foo: 10, bar : 20, baz: [100, 101], [frob, 1]: boo }", FY_NT);
	assert(fyd);

	/* check for correct count value */
	count = fy_node_mapping_item_count(fy_document_root(fyd));
	ck_assert_int_eq(count, 4);

	/* forward iterator first */
	iter = NULL;

	/* 0 */
	fynp = fy_node_mapping_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_key(fynp)), "foo");
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "10");

	/* 1 */
	fynp = fy_node_mapping_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_key(fynp)), "bar");
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "20");

	/* 2 */
	fynp = fy_node_mapping_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_key(fynp)), "baz");
	ck_assert_int_eq(fy_node_sequence_item_count(fy_node_pair_value(fynp)), 2);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_sequence_get_by_index(fy_node_pair_value(fynp), 0)), "100");
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_sequence_get_by_index(fy_node_pair_value(fynp), 1)), "101");

	/* 3 */
	fynp = fy_node_mapping_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_int_eq(fy_node_sequence_item_count(fy_node_pair_key(fynp)), 2);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_sequence_get_by_index(fy_node_pair_key(fynp), 0)), "frob");
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_sequence_get_by_index(fy_node_pair_key(fynp), 1)), "1");
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "boo");

	/* 4, it must not exist */
	fynp = fy_node_mapping_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_eq(fynp, NULL);

	/* reverse iterator */
	iter = NULL;

	/* 3 */
	fynp = fy_node_mapping_reverse_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_int_eq(fy_node_sequence_item_count(fy_node_pair_key(fynp)), 2);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_sequence_get_by_index(fy_node_pair_key(fynp), 0)), "frob");
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_sequence_get_by_index(fy_node_pair_key(fynp), 1)), "1");
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "boo");

	/* 2 */
	fynp = fy_node_mapping_reverse_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_key(fynp)), "baz");
	ck_assert_int_eq(fy_node_sequence_item_count(fy_node_pair_value(fynp)), 2);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_sequence_get_by_index(fy_node_pair_value(fynp), 0)), "100");
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_sequence_get_by_index(fy_node_pair_value(fynp), 1)), "101");

	/* 1 */
	fynp = fy_node_mapping_reverse_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_key(fynp)), "bar");
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "20");

	/* 0 */
	fynp = fy_node_mapping_reverse_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_key(fynp)), "foo");
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "10");

	/* -1, it must not exist */
	fynp = fy_node_mapping_reverse_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_eq(fynp, NULL);

	/* key lookups (note how only the contents are compared) */
	ck_assert(fy_node_compare_string(fy_node_mapping_lookup_by_string(fy_document_root(fyd), "foo", FY_NT), "10", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_mapping_lookup_by_string(fy_document_root(fyd), "bar", FY_NT), "20", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_mapping_lookup_by_string(fy_document_root(fyd), "baz", FY_NT), "- 100\n- 101", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_mapping_lookup_by_string(fy_document_root(fyd), "- 'frob'\n- \"\x31\"", FY_NT), "boo", FY_NT) == true);

	fy_document_destroy(fyd);
	fyd = NULL;
}
END_TEST

START_TEST(doc_path_access)
{
	struct fy_document *fyd;
	struct fy_node *fyn;

	/* build document */
	fyd = fy_document_build_from_string(NULL, "{ "
		"foo: 10, bar : 20, baz:{ frob: boo }, "
		"frooz: [ seq1, { key: value} ], \"zero\\0zero\" : 0, "
		"{ key2: value2 }: { key3: value3 } "
		"}", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* check that getting root node works */
	fyn = fy_node_by_path(fy_document_root(fyd), "/", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_ptr_eq(fyn, fy_document_root(fyd));

	/* check access to scalars */
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/foo", FY_NT, FYNWF_DONT_FOLLOW), "10", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "bar", FY_NT, FYNWF_DONT_FOLLOW), "20", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "baz/frob", FY_NT, FYNWF_DONT_FOLLOW), "boo", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/frooz/[0]", FY_NT, FYNWF_DONT_FOLLOW), "seq1", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/frooz/[1]/key", FY_NT, FYNWF_DONT_FOLLOW), "value", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "\"zero\\0zero\"", FY_NT, FYNWF_DONT_FOLLOW), "0", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/{ key2: value2 }/key3", FY_NT, FYNWF_DONT_FOLLOW), "value3", FY_NT) == true);

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(doc_path_node)
{
	struct fy_document *fyd;
	char *path;

	/* build document */
	fyd = fy_document_build_from_string(NULL, "{ "
		"foo: 10, bar : 20, baz:{ frob: boo }, "
		"frooz: [ seq1, { key: value} ], \"zero\\0zero\" : 0, "
		"{ key2: value2 }: { key3: value3 } "
		"}", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	path = fy_node_get_path(fy_node_by_path(fy_document_root(fyd), "/", FY_NT, FYNWF_DONT_FOLLOW));
	ck_assert_str_eq(path, "/");
	free(path);

	path = fy_node_get_path(fy_node_by_path(fy_document_root(fyd), "/frooz", FY_NT, FYNWF_DONT_FOLLOW));
	ck_assert_str_eq(path, "/frooz");
	free(path);

	path = fy_node_get_path(fy_node_by_path(fy_document_root(fyd), "/frooz/[0]", FY_NT, FYNWF_DONT_FOLLOW));
	ck_assert_str_eq(path, "/frooz/0");
	free(path);

	path = fy_node_get_path(fy_node_by_path(fy_document_root(fyd), "/{ key2: value2 }/key3", FY_NT, FYNWF_DONT_FOLLOW));
	ck_assert_str_eq(path, "/{key2: value2}/key3");
	free(path);

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(doc_create_empty_seq1)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	char *buf;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fyn = fy_node_build_from_string(fyd, "[ ]", FY_NT);
	ck_assert_ptr_ne(fyn, NULL);

	fy_document_set_root(fyd, fyn);

	/* convert to string */
	buf = fy_emit_node_to_string(fy_document_root(fyd), FYECF_MODE_FLOW_ONELINE);
	ck_assert_ptr_ne(buf, NULL);

	/* compare with expected result */
	ck_assert_str_eq(buf, "[]");

	free(buf);

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(doc_create_empty_seq2)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	char *buf;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fyn = fy_node_create_sequence(fyd);
	ck_assert_ptr_ne(fyn, NULL);

	fy_document_set_root(fyd, fyn);

	/* convert to string */
	buf = fy_emit_node_to_string(fy_document_root(fyd), FYECF_MODE_FLOW_ONELINE);
	ck_assert_ptr_ne(buf, NULL);

	/* compare with expected result */
	ck_assert_str_eq(buf, "[]");

	free(buf);

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(doc_create_empty_map1)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	char *buf;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fyn = fy_node_build_from_string(fyd, "{ }", FY_NT);
	ck_assert_ptr_ne(fyn, NULL);

	fy_document_set_root(fyd, fyn);

	/* convert to string */
	buf = fy_emit_node_to_string(fy_document_root(fyd), FYECF_MODE_FLOW_ONELINE);
	ck_assert_ptr_ne(buf, NULL);

	/* compare with expected result */
	ck_assert_str_eq(buf, "{}");

	free(buf);

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(doc_create_empty_map2)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	char *buf;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fyn = fy_node_create_mapping(fyd);
	ck_assert_ptr_ne(fyn, NULL);

	fy_document_set_root(fyd, fyn);

	/* convert to string */
	buf = fy_emit_node_to_string(fy_document_root(fyd), FYECF_MODE_FLOW_ONELINE);
	ck_assert_ptr_ne(buf, NULL);

	/* compare with expected result */
	ck_assert_str_eq(buf, "{}");

	free(buf);

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(doc_create_test_seq1)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	int ret;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fyn = fy_node_create_sequence(fyd);
	ck_assert_ptr_ne(fyn, NULL);

	ret = fy_node_sequence_append(fyn, fy_node_create_scalar(fyd, "foo", FY_NT));
	ck_assert_int_eq(ret, 0);

	ret = fy_node_sequence_append(fyn, fy_node_create_scalar(fyd, "bar", FY_NT));
	ck_assert_int_eq(ret, 0);

	ret = fy_node_sequence_append(fyn, fy_node_build_from_string(fyd, "{ baz: frooz }", FY_NT));
	ck_assert_int_eq(ret, 0);

	fy_document_set_root(fyd, fyn);

	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/[0]", FY_NT, FYNWF_DONT_FOLLOW), "foo", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/[1]", FY_NT, FYNWF_DONT_FOLLOW), "bar", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/[2]/baz", FY_NT, FYNWF_DONT_FOLLOW), "frooz", FY_NT) == true);

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(doc_create_test_map1)
{
	struct fy_document *fyd;
	struct fy_node *fyn, *fyn1, *fyn2, *fyn3;
	int ret;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fyn = fy_node_create_mapping(fyd);
	ck_assert_ptr_ne(fyn, NULL);

	ret = fy_node_mapping_append(fyn,
			fy_node_build_from_string(fyd, "seq", FY_NT),
			fy_node_build_from_string(fyd, "[ zero, one ]", FY_NT));
	ck_assert_int_eq(ret, 0);

	ret = fy_node_mapping_append(fyn, NULL,
			fy_node_build_from_string(fyd, "value-of-null-key", FY_NT));
	ck_assert_int_eq(ret, 0);

	ret = fy_node_mapping_append(fyn,
			fy_node_build_from_string(fyd, "key-of-null-value", FY_NT), NULL);
	ck_assert_int_eq(ret, 0);

	fy_document_set_root(fyd, fyn);

	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/seq/[0]", FY_NT, FYNWF_DONT_FOLLOW), "zero", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/seq/[1]", FY_NT, FYNWF_DONT_FOLLOW), "one", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/''/", FY_NT, FYNWF_DONT_FOLLOW), "value-of-null-key", FY_NT) == true);

	fyn1 = fy_node_by_path(fy_document_root(fyd), "/key-of-null-value", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_eq(fyn1, NULL);

	/* try to append duplicate key (it should fail) */
	fyn2 = fy_node_build_from_string(fyd, "seq", FY_NT);
	ck_assert_ptr_ne(fyn2, NULL);
	fyn3 = fy_node_create_scalar(fyd, "dupl", FY_NT);
	ck_assert_ptr_ne(fyn3, NULL);
	ret = fy_node_mapping_append(fyn, fyn2, fyn3);
	ck_assert_int_ne(ret, 0);

	fy_node_free(fyn3);
	fy_node_free(fyn2);

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(doc_insert_remove_seq)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	int ret;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	fy_document_set_root(fyd, fy_node_build_from_string(fyd, "[ one, two, four ]", FY_NT));

	/* check that the order is correct */
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/[0]", FY_NT, FYNWF_DONT_FOLLOW), "one", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/[1]", FY_NT, FYNWF_DONT_FOLLOW), "two", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/[2]", FY_NT, FYNWF_DONT_FOLLOW), "four", FY_NT) == true);

	ret = fy_node_sequence_append(fy_document_root(fyd), fy_node_build_from_string(fyd, "five", FY_NT));
	ck_assert_int_eq(ret, 0);

	ret = fy_node_sequence_prepend(fy_document_root(fyd), fy_node_build_from_string(fyd, "zero", FY_NT));
	ck_assert_int_eq(ret, 0);

	ret = fy_node_sequence_insert_after(fy_document_root(fyd),
			fy_node_by_path(fy_document_root(fyd), "/[2]", FY_NT, FYNWF_DONT_FOLLOW),
			fy_node_build_from_string(fyd, "three", FY_NT));
	ck_assert_int_eq(ret, 0);

	ret = fy_node_sequence_insert_before(fy_document_root(fyd),
			fy_node_by_path(fy_document_root(fyd), "/[3]", FY_NT, FYNWF_DONT_FOLLOW),
			fy_node_build_from_string(fyd, "two-and-a-half", FY_NT));
	ck_assert_int_eq(ret, 0);

	fyn = fy_node_sequence_remove(fy_document_root(fyd),
			fy_node_by_path(fy_document_root(fyd), "/[3]", FY_NT, FYNWF_DONT_FOLLOW));
	ck_assert_ptr_ne(fyn, NULL);

	fy_node_free(fyn);

	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/[0]", FY_NT, FYNWF_DONT_FOLLOW), "zero", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/[1]", FY_NT, FYNWF_DONT_FOLLOW), "one", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/[2]", FY_NT, FYNWF_DONT_FOLLOW), "two", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/[3]", FY_NT, FYNWF_DONT_FOLLOW), "three", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/[4]", FY_NT, FYNWF_DONT_FOLLOW), "four", FY_NT) == true);

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(doc_insert_remove_map)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	int ret;

	fyd = fy_document_build_from_string(NULL, "{ one: 1, two: 2, four: 4 }", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	/* check that the order is correct */
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/one", FY_NT, FYNWF_DONT_FOLLOW), "1", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/two", FY_NT, FYNWF_DONT_FOLLOW), "2", FY_NT) == true);
	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/four", FY_NT, FYNWF_DONT_FOLLOW), "4", FY_NT) == true);

	ret = fy_node_mapping_append(fy_document_root(fyd),
			fy_node_build_from_string(fyd, "three", FY_NT),
			fy_node_build_from_string(fyd, "3", FY_NT));
	ck_assert_int_eq(ret, 0);

	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/three", FY_NT, FYNWF_DONT_FOLLOW), "3", FY_NT) == true);

	ret = fy_node_mapping_prepend(fy_document_root(fyd),
			fy_node_build_from_string(fyd, "zero", FY_NT),
			fy_node_build_from_string(fyd, "0", FY_NT));
	ck_assert_int_eq(ret, 0);

	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/zero", FY_NT, FYNWF_DONT_FOLLOW), "0", FY_NT) == true);

	ret = fy_node_mapping_append(fy_document_root(fyd),
			fy_node_build_from_string(fyd, "two-and-a-half", FY_NT),
			fy_node_build_from_string(fyd, "2.5", FY_NT));
	ck_assert_int_eq(ret, 0);

	ck_assert(fy_node_compare_string(fy_node_by_path(fy_document_root(fyd), "/two-and-a-half", FY_NT, FYNWF_DONT_FOLLOW), "2.5", FY_NT) == true);

	fyn = fy_node_mapping_remove_by_key(fy_document_root(fyd),
			fy_node_build_from_string(fyd, "two-and-a-half", FY_NT));
	ck_assert_ptr_ne(fyn, NULL);

	fy_node_free(fyn);

	/* it must be removed */
	fyn = fy_node_by_path(fy_document_root(fyd), "/two-and-a-half", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_eq(fyn, NULL);

	fy_document_destroy(fyd);
}
END_TEST

START_TEST(doc_sort)
{
	struct fy_document *fyd;
	void *fynp;
	void *iter;
	int ret, count;

	fyd = fy_document_build_from_string(NULL, "{ a: 5, { z: bar }: 1, z: 7, "
				      "[ a, b, c] : 3, { a: whee } : 2 , "
				      "b: 6, [ z ]: 4 }", FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	ret = fy_node_sort(fy_document_root(fyd), NULL, NULL);
	ck_assert_int_eq(ret, 0);

	/* check for correct count value */
	count = fy_node_mapping_item_count(fy_document_root(fyd));
	ck_assert_int_eq(count, 7);

	/* forward iterator first */
	iter = NULL;

	fynp = fy_node_mapping_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "1");

	fynp = fy_node_mapping_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "2");

	fynp = fy_node_mapping_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "3");

	fynp = fy_node_mapping_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "4");

	fynp = fy_node_mapping_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "5");

	fynp = fy_node_mapping_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "6");

	fynp = fy_node_mapping_iterate(fy_document_root(fyd), &iter);
	ck_assert_ptr_ne(fynp, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fy_node_pair_value(fynp)), "7");

	fy_document_destroy(fyd);
}
END_TEST

static char *join_docs(const char *tgt_text, const char *tgt_path,
		       const char *src_text, const char *src_path,
		       const char *emit_path)
{
	struct fy_document *fyd_tgt, *fyd_src;
	struct fy_node *fyn_tgt, *fyn_src, *fyn_emit;
	char *output;
	int ret;

	/* insert which overwrites root ( <map> <- <scalar> ) */
	fyd_tgt = fy_document_build_from_string(NULL, tgt_text, FY_NT);
	ck_assert_ptr_ne(fyd_tgt, NULL);

	fyd_src = fy_document_build_from_string(NULL, src_text, FY_NT);
	ck_assert_ptr_ne(fyd_src, NULL);

	fyn_tgt = fy_node_by_path(fy_document_root(fyd_tgt), tgt_path, FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn_tgt, NULL);

	fyn_src = fy_node_by_path(fy_document_root(fyd_src), src_path, FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn_src, NULL);

	ret = fy_node_insert(fyn_tgt, fyn_src);
	ck_assert_int_eq(ret, 0);

	ret = fy_document_set_parent(fyd_tgt, fyd_src);
	ck_assert_int_eq(ret, 0);

	fyn_emit = fy_node_by_path(fy_document_root(fyd_tgt), emit_path, FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn_emit, NULL);

	output = fy_emit_node_to_string(fyn_emit, FYECF_MODE_FLOW_ONELINE | FYECF_WIDTH_INF);
	ck_assert_ptr_ne(output, NULL);

	fy_document_destroy(fyd_tgt);

	return output;
}

START_TEST(doc_join_scalar_to_scalar)
{
	char *output;

	output = join_docs(
			"foo", "/",	/* target */
			"bar", "/",	/* source */
			"/");		/* emit path */

	ck_assert_str_eq(output, "bar");
	free(output);
}
END_TEST

START_TEST(doc_join_scalar_to_map)
{
	char *output;

	output = join_docs(
			"{ foo: baz }", "/",	/* target */
			"bar", "/",		/* source */
			"/");			/* emit path */

	ck_assert_str_eq(output, "bar");
	free(output);
}
END_TEST

START_TEST(doc_join_scalar_to_seq)
{
	char *output;

	output = join_docs(
			"[ foo, baz ]", "/",	/* target */
			"bar", "/",		/* source */
			"/");			/* emit path */

	ck_assert_str_eq(output, "bar");
	free(output);
}
END_TEST

START_TEST(doc_join_map_to_scalar)
{
	char *output;

	output = join_docs(
			"foo", "/",		/* target */
			"{bar: baz}", "/",		/* source */
			"/");			/* emit path */

	ck_assert_str_eq(output, "{bar: baz}");
	free(output);
}
END_TEST

START_TEST(doc_join_map_to_seq)
{
	char *output;

	output = join_docs(
			"[foo, frooz]", "/",	/* target */
			"{bar: baz}", "/",	/* source */
			"/");			/* emit path */

	ck_assert_str_eq(output, "{bar: baz}");
	free(output);
}
END_TEST

START_TEST(doc_join_map_to_map)
{
	char *output;

	output = join_docs(
			"{foo: frooz}", "/",	/* target */
			"{bar: baz}", "/",	/* source */
			"/");			/* emit path */

	ck_assert_str_eq(output, "{foo: frooz, bar: baz}");
	free(output);
}
END_TEST

START_TEST(doc_join_seq_to_scalar)
{
	char *output;

	output = join_docs(
			"foo", "/",		/* target */
			"[bar, baz]", "/",		/* source */
			"/");			/* emit path */

	ck_assert_str_eq(output, "[bar, baz]");
	free(output);
}
END_TEST

START_TEST(doc_join_seq_to_seq)
{
	char *output;

	output = join_docs(
			"[foo, frooz]", "/",	/* target */
			"[bar, baz]", "/",	/* source */
			"/");			/* emit path */

	ck_assert_str_eq(output, "[foo, frooz, bar, baz]");
	free(output);
}
END_TEST

START_TEST(doc_join_seq_to_map)
{
	char *output;

	output = join_docs(
			"{foo: frooz}", "/",	/* target */
			"[bar, baz]", "/",	/* source */
			"/");			/* emit path */

	ck_assert_str_eq(output, "[bar, baz]");
	free(output);
}
END_TEST

START_TEST(doc_join_tags)
{
	char *output;

	output = join_docs(
		"%TAG !a! tag:a.com,2019:\n"
		"---\n"
		"- !a!foo\n"
		"  foo: bar\n", "/",
		"%TAG !b! tag:b.com,2019:\n"
		"---\n"
		"- !b!bar\n"
		"  something: other\n", "/",
		"/");

	ck_assert_str_eq(output, "[!a!foo {foo: bar}, !b!bar {something: other}]");
	free(output);
}
END_TEST

START_TEST(doc_build_with_tags)
{
	struct fy_document *fyd;
	struct fy_node *fyn;
	struct fy_token *fyt;
	char *buf;
	int rc;

	/* build document */
	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);

	/* create a sequence and set it as root */
	fyn = fy_node_create_sequence(fyd);
	ck_assert_ptr_ne(fyn, NULL);

	fy_document_set_root(fyd, fyn);
	fyn = NULL;

	/* create a node, containing a new tag */
	fyn = fy_node_build_from_string(fyd, "%TAG !e! tag:example.com,2000:app/\n---\n- foo\n- !e!foo bar\n", FY_NT);
	ck_assert_ptr_ne(fyn, NULL);

	/* append it to the root of the document */
	rc = fy_node_sequence_append(fy_document_root(fyd), fyn);
	ck_assert_int_eq(rc, 0);
	fyn = NULL;

	/* there must be a new tag */
	fyt = fy_document_tag_directive_lookup(fyd, "!e!");
	ck_assert_ptr_ne(fyt, NULL);

	/* try to build another, but with a different !e! prefix, it must fail */
	fyn = fy_node_build_from_string(fyd, "%TAG !e! tag:example.com,2019:app/\n---\n- foo\n- !e!foo bar\n", FY_NT);
	ck_assert_ptr_eq(fyn, NULL);

	/* manually add a tag */
	rc = fy_document_tag_directive_add(fyd, "!f!", "tag:example.com,2019:f/");
	ck_assert_int_eq(rc, 0);

	/* build a node with a tag that's already in the document */
	fyn = fy_node_build_from_string(fyd, "!f!whiz frooz\n", FY_NT);
	ck_assert_ptr_ne(fyn, NULL);

	/* append it to the root of the document */
	rc = fy_node_sequence_append(fy_document_root(fyd), fyn);
	ck_assert_int_eq(rc, 0);
	fyn = NULL;

	/* convert to string */
	buf = fy_emit_document_to_string(fyd, FYECF_MODE_FLOW_ONELINE);
	ck_assert_ptr_ne(buf, NULL);

	free(buf);

	fy_document_destroy(fyd);
}
END_TEST

TCase *libfyaml_case_core(void)
{
	TCase *tc;

	tc = tcase_create("core");

	tcase_add_test(tc, doc_build_simple);
	tcase_add_test(tc, doc_build_parse_check);
	tcase_add_test(tc, doc_build_scalar);
	tcase_add_test(tc, doc_build_sequence);
	tcase_add_test(tc, doc_build_mapping);

	tcase_add_test(tc, doc_path_access);
	tcase_add_test(tc, doc_path_node);

	tcase_add_test(tc, doc_create_empty_seq1);
	tcase_add_test(tc, doc_create_empty_seq2);
	tcase_add_test(tc, doc_create_empty_map1);
	tcase_add_test(tc, doc_create_empty_map2);

	tcase_add_test(tc, doc_create_test_seq1);
	tcase_add_test(tc, doc_create_test_map1);

	tcase_add_test(tc, doc_insert_remove_seq);
	tcase_add_test(tc, doc_insert_remove_map);

	tcase_add_test(tc, doc_sort);

	tcase_add_test(tc, doc_join_scalar_to_scalar);
	tcase_add_test(tc, doc_join_scalar_to_map);
	tcase_add_test(tc, doc_join_scalar_to_seq);

	tcase_add_test(tc, doc_join_map_to_scalar);
	tcase_add_test(tc, doc_join_map_to_seq);
	tcase_add_test(tc, doc_join_map_to_map);

	tcase_add_test(tc, doc_join_seq_to_scalar);
	tcase_add_test(tc, doc_join_seq_to_seq);
	tcase_add_test(tc, doc_join_seq_to_map);

	tcase_add_test(tc, doc_join_tags);

	tcase_add_test(tc, doc_build_with_tags);

	return tc;
}
