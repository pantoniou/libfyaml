/*
 * libfyaml-test-reflection.c - libfyaml reflection API tests
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
#include <stdalign.h>

#include <check.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-reflection.h>

#include "fy-utils.h"
#include "fy-check.h"

/* suppress -Wunused warnings in test bodies */
FY_DIAG_PUSH
FY_DIAG_IGNORE_UNUSED_VARIABLE
FY_DIAG_IGNORE_UNUSED_PARAMETER

/*
 * Helper: create a quiet parser from a YAML string.
 * Caller must fy_parser_destroy() the returned parser.
 */
static struct fy_parser *make_parser_from_string(const char *yaml)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_QUIET };
	struct fy_parser *fyp;

	fyp = fy_parser_create(&cfg);
	if (!fyp)
		return NULL;
	if (fy_parser_set_string(fyp, yaml, FY_NT) != 0) {
		fy_parser_destroy(fyp);
		return NULL;
	}
	return fyp;
}

static char *write_temp_header(const char *content)
{
	char path[PATH_MAX + 1];
	char *hdr;
	int r;

	r = fy_create_tmpfile(path, sizeof(path), content, strlen(content));
	ck_assert_int_eq(r, 0);

	hdr = strdup(path);
	ck_assert_ptr_ne(hdr, NULL);
	return hdr;
}

/* ------------------------------------------------------------------ */
/* Test: null backend creation and fy_type_info accessors              */
/* ------------------------------------------------------------------ */

/* Test: create a null reflection and verify int type info accessors */
START_TEST(reflection_null_backend_int_info)
{
	struct fy_reflection *rfl;
	const struct fy_type_info *ti;

	rfl = fy_reflection_from_null(NULL);
	ck_assert_ptr_ne(rfl, NULL);

	ti = fy_type_info_lookup(rfl, "int");
	ck_assert_ptr_ne(ti, NULL);

	ck_assert_int_eq(fy_type_info_get_kind(ti), FYTK_INT);
	ck_assert_str_eq(fy_type_info_get_name(ti), "int");
	ck_assert_int_eq((int)fy_type_info_get_size(ti), (int)sizeof(int));
	ck_assert_int_eq((int)fy_type_info_get_align(ti), (int)alignof(int));

	/* primitives have no dependent type, no fields, zero count */
	ck_assert_ptr_eq(fy_type_info_get_dependent_type(ti), NULL);
	ck_assert_int_eq((int)fy_type_info_get_count(ti), 0);
	ck_assert_ptr_eq(fy_type_info_get_field_at(ti, 0), NULL);

	fy_reflection_destroy(rfl);
}
END_TEST

/* Test: look up unsigned int from null backend */
START_TEST(reflection_null_backend_uint_info)
{
	struct fy_reflection *rfl;
	const struct fy_type_info *ti;

	rfl = fy_reflection_from_null(NULL);
	ck_assert_ptr_ne(rfl, NULL);

	ti = fy_type_info_lookup(rfl, "unsigned int");
	ck_assert_ptr_ne(ti, NULL);

	ck_assert_int_eq(fy_type_info_get_kind(ti), FYTK_UINT);
	ck_assert_str_eq(fy_type_info_get_name(ti), "unsigned int");
	ck_assert_int_eq((int)fy_type_info_get_size(ti), (int)sizeof(unsigned int));

	fy_reflection_destroy(rfl);
}
END_TEST

/* ------------------------------------------------------------------ */
/* Test: parse primitives via fy_type_context                          */
/* ------------------------------------------------------------------ */

/* Test: parse int "10" → C int value == 10  (mirrors 001JRQ7/00) */
START_TEST(reflection_parse_int)
{
	struct fy_reflection *rfl;
	struct fy_type_context_cfg ctx_cfg = { 0 };
	struct fy_type_context *ctx;
	struct fy_parser *fyp;
	void *data;
	int rc;

	rfl = fy_reflection_from_null(NULL);
	ck_assert_ptr_ne(rfl, NULL);

	ctx_cfg.rfl = rfl;
	ctx_cfg.entry_type = "int";
	ctx = fy_type_context_create(&ctx_cfg);
	ck_assert_ptr_ne(ctx, NULL);

	fyp = make_parser_from_string("10");
	ck_assert_ptr_ne(fyp, NULL);

	data = NULL;
	rc = fy_type_context_parse(ctx, fyp, &data);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(data, NULL);

	ck_assert_int_eq(*(int *)data, 10);

	fy_type_context_free_data(ctx, data);
	fy_parser_destroy(fyp);
	fy_type_context_destroy(ctx);
	fy_reflection_destroy(rfl);
}
END_TEST

/* Test: parse unsigned int "42" → C unsigned int value == 42  (mirrors 005KROY) */
START_TEST(reflection_parse_uint)
{
	struct fy_reflection *rfl;
	struct fy_type_context_cfg ctx_cfg = { 0 };
	struct fy_type_context *ctx;
	struct fy_parser *fyp;
	void *data;
	int rc;

	rfl = fy_reflection_from_null(NULL);
	ck_assert_ptr_ne(rfl, NULL);

	ctx_cfg.rfl = rfl;
	ctx_cfg.entry_type = "unsigned int";
	ctx = fy_type_context_create(&ctx_cfg);
	ck_assert_ptr_ne(ctx, NULL);

	fyp = make_parser_from_string("42");
	ck_assert_ptr_ne(fyp, NULL);

	data = NULL;
	rc = fy_type_context_parse(ctx, fyp, &data);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(data, NULL);

	ck_assert_uint_eq(*(unsigned int *)data, 42U);

	fy_type_context_free_data(ctx, data);
	fy_parser_destroy(fyp);
	fy_type_context_destroy(ctx);
	fy_reflection_destroy(rfl);
}
END_TEST

/* Test: parse int pointer "10" → *data == 10  (mirrors 001JRQ7/01) */
START_TEST(reflection_parse_int_ptr)
{
	struct fy_reflection *rfl;
	struct fy_type_context_cfg ctx_cfg = { 0 };
	struct fy_type_context *ctx;
	struct fy_parser *fyp;
	void *data;
	int rc;

	rfl = fy_reflection_from_null(NULL);
	ck_assert_ptr_ne(rfl, NULL);

	ctx_cfg.rfl = rfl;
	ctx_cfg.entry_type = "int *";
	ctx = fy_type_context_create(&ctx_cfg);
	ck_assert_ptr_ne(ctx, NULL);

	fyp = make_parser_from_string("10");
	ck_assert_ptr_ne(fyp, NULL);

	data = NULL;
	rc = fy_type_context_parse(ctx, fyp, &data);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(data, NULL);

	/* data is int** — the allocated pointer-to-int */
	ck_assert_ptr_ne(*(int **)data, NULL);
	ck_assert_int_eq(**(int **)data, 10);

	fy_type_context_free_data(ctx, data);
	fy_parser_destroy(fyp);
	fy_type_context_destroy(ctx);
	fy_reflection_destroy(rfl);
}
END_TEST

/* ------------------------------------------------------------------ */
/* Test: clang backend — struct field info  (requires HAVE_LIBCLANG)  */
/* ------------------------------------------------------------------ */

#ifdef HAVE_LIBCLANG

/*
 * Definition matching test/reflection-data/025HY3Q/00/definition.h
 * The local copy lets us cast the parsed void* to a concrete type.
 */
struct test_foo {
	char *key;
	int value;
};

struct test_baz {
	int count;
	struct test_foo *foos;
};

static const char baz_header[] =
	"struct foo {\n"
	"	char *key;\n"
	"	int value;\n"
	"};\n"
	"\n"
	"struct baz {\n"
	"	int count;\n"
	"	struct foo *foos;\t// yaml: { key: key, counter: count }\n"
	"};\n";

/* Test: create reflection from C file, verify struct field info */
START_TEST(reflection_clang_struct_info)
{
	struct fy_reflection *rfl;
	const struct fy_type_info *ti;
	const struct fy_field_info *fi_count, *fi_foos;
	char *hdr_path;

	hdr_path = write_temp_header(baz_header);
	ck_assert_ptr_ne(hdr_path, NULL);

	rfl = fy_reflection_from_c_file_with_cflags(hdr_path, "",
						     false, true, NULL);
	remove(hdr_path);
	free(hdr_path);
	ck_assert_ptr_ne(rfl, NULL);

	ti = fy_type_info_lookup(rfl, "struct baz");
	ck_assert_ptr_ne(ti, NULL);

	ck_assert_int_eq(fy_type_info_get_kind(ti), FYTK_STRUCT);
	ck_assert_str_eq(fy_type_info_get_name(ti), "struct baz");

	/* struct baz has 2 fields: count and foos */
	ck_assert_int_eq((int)fy_type_info_get_count(ti), 2);

	fi_count = fy_type_info_get_field_at(ti, 0);
	ck_assert_ptr_ne(fi_count, NULL);
	ck_assert_str_eq(fy_field_info_get_name(fi_count), "count");

	fi_foos = fy_type_info_get_field_at(ti, 1);
	ck_assert_ptr_ne(fi_foos, NULL);
	ck_assert_str_eq(fy_field_info_get_name(fi_foos), "foos");

	fy_reflection_destroy(rfl);
}
END_TEST

/* Test: parse struct baz from YAML, verify C values (mirrors 025HY3Q/00) */
START_TEST(reflection_clang_struct_parse)
{
	static const char yaml[] =
		"foos:\n"
		"  one: 10\n"
		"  two: 200\n"
		"  three: 300\n";
	struct fy_reflection *rfl;
	struct fy_type_context_cfg ctx_cfg = { 0 };
	struct fy_type_context *ctx;
	struct fy_parser *fyp;
	void *data;
	struct test_baz *baz;
	char *hdr_path;
	int rc;

	hdr_path = write_temp_header(baz_header);
	ck_assert_ptr_ne(hdr_path, NULL);

	rfl = fy_reflection_from_c_file_with_cflags(hdr_path, "",
						     false, true, NULL);
	remove(hdr_path);
	free(hdr_path);
	ck_assert_ptr_ne(rfl, NULL);

	ctx_cfg.rfl = rfl;
	ctx_cfg.entry_type = "struct baz";
	ctx = fy_type_context_create(&ctx_cfg);
	ck_assert_ptr_ne(ctx, NULL);

	fyp = make_parser_from_string(yaml);
	ck_assert_ptr_ne(fyp, NULL);

	data = NULL;
	rc = fy_type_context_parse(ctx, fyp, &data);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(data, NULL);

	baz = (struct test_baz *)data;
	ck_assert_int_eq(baz->count, 3);
	ck_assert_ptr_ne(baz->foos, NULL);

	/* Keys come from the YAML mapping: one, two, three */
	ck_assert_str_eq(baz->foos[0].key, "one");
	ck_assert_int_eq(baz->foos[0].value, 10);

	ck_assert_str_eq(baz->foos[1].key, "two");
	ck_assert_int_eq(baz->foos[1].value, 200);

	ck_assert_str_eq(baz->foos[2].key, "three");
	ck_assert_int_eq(baz->foos[2].value, 300);

	fy_type_context_free_data(ctx, data);
	fy_parser_destroy(fyp);
	fy_type_context_destroy(ctx);
	fy_reflection_destroy(rfl);
}
END_TEST

#endif /* HAVE_LIBCLANG */

/* ------------------------------------------------------------------ */
/* Test: packed backend round-trip                                     */
/* ------------------------------------------------------------------ */

/* Test: pack reflection and restore; verify struct baz still introspectable */
/* (seeds from C file, so also requires HAVE_LIBCLANG) */
#ifdef HAVE_LIBCLANG
START_TEST(reflection_packed_roundtrip)
{
	struct fy_reflection *rfl, *rfl2;
	const struct fy_type_info *ti, *ti2;
	void *blob;
	size_t blob_size;
	char *hdr_path;

	hdr_path = write_temp_header(baz_header);
	ck_assert_ptr_ne(hdr_path, NULL);

	rfl = fy_reflection_from_c_file_with_cflags(hdr_path, "",
						     false, true, NULL);
	remove(hdr_path);
	free(hdr_path);
	ck_assert_ptr_ne(rfl, NULL);

	/* look up the type before packing */
	ti = fy_type_info_lookup(rfl, "struct baz");
	ck_assert_ptr_ne(ti, NULL);
	ck_assert_int_eq((int)fy_type_info_get_count(ti), 2);

	/* pack to blob */
	blob = fy_reflection_to_packed_blob(rfl, &blob_size, false, false);
	ck_assert_ptr_ne(blob, NULL);
	ck_assert_int_gt((int)blob_size, 0);

	fy_reflection_destroy(rfl);

	/* restore from blob */
	rfl2 = fy_reflection_from_packed_blob(blob, blob_size, NULL);
	free(blob);
	ck_assert_ptr_ne(rfl2, NULL);

	/* same type must be present with same field count */
	ti2 = fy_type_info_lookup(rfl2, "struct baz");
	ck_assert_ptr_ne(ti2, NULL);
	ck_assert_int_eq(fy_type_info_get_kind(ti2), FYTK_STRUCT);
	ck_assert_int_eq((int)fy_type_info_get_count(ti2), 2);

	/* field names must survive */
	ck_assert_str_eq(fy_field_info_get_name(fy_type_info_get_field_at(ti2, 0)), "count");
	ck_assert_str_eq(fy_field_info_get_name(fy_type_info_get_field_at(ti2, 1)), "foos");

	fy_reflection_destroy(rfl2);
}
END_TEST

#endif /* HAVE_LIBCLANG */

/* Test: packed backend parse — same YAML parse via packed reflection */
/* (seeds from null backend, so no libclang required) */
START_TEST(reflection_packed_parse_int)
{
	struct fy_reflection *rfl, *rfl2;
	struct fy_type_context_cfg ctx_cfg = { 0 };
	struct fy_type_context *ctx;
	struct fy_parser *fyp;
	void *data, *blob;
	size_t blob_size;
	int rc;

	/* build original reflection */
	rfl = fy_reflection_from_null(NULL);
	ck_assert_ptr_ne(rfl, NULL);

	blob = fy_reflection_to_packed_blob(rfl, &blob_size, false, false);
	ck_assert_ptr_ne(blob, NULL);
	fy_reflection_destroy(rfl);

	/* restore from packed */
	rfl2 = fy_reflection_from_packed_blob(blob, blob_size, NULL);
	free(blob);
	ck_assert_ptr_ne(rfl2, NULL);

	/* parse using the restored reflection */
	ctx_cfg.rfl = rfl2;
	ctx_cfg.entry_type = "int";
	ctx = fy_type_context_create(&ctx_cfg);
	ck_assert_ptr_ne(ctx, NULL);

	fyp = make_parser_from_string("-7");
	ck_assert_ptr_ne(fyp, NULL);

	data = NULL;
	rc = fy_type_context_parse(ctx, fyp, &data);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(data, NULL);

	ck_assert_int_eq(*(int *)data, -7);

	fy_type_context_free_data(ctx, data);
	fy_parser_destroy(fyp);
	fy_type_context_destroy(ctx);
	fy_reflection_destroy(rfl2);
}
END_TEST

FY_DIAG_POP

/* ------------------------------------------------------------------ */
/* Test case registration                                              */
/* ------------------------------------------------------------------ */

void libfyaml_case_reflection(struct fy_check_suite *cs)
{
	struct fy_check_testcase *ctc;

	/* null backend / accessor tests (no libclang required) */
	ctc = fy_check_suite_add_test_case(cs, "reflection-basics");

	fy_check_testcase_add_test(ctc, reflection_null_backend_int_info);
	fy_check_testcase_add_test(ctc, reflection_null_backend_uint_info);
	fy_check_testcase_add_test(ctc, reflection_parse_int);
	fy_check_testcase_add_test(ctc, reflection_parse_uint);
	fy_check_testcase_add_test(ctc, reflection_parse_int_ptr);

#ifdef HAVE_LIBCLANG
	/* clang backend tests */
	ctc = fy_check_suite_add_test_case(cs, "reflection-clang");

	fy_check_testcase_add_test(ctc, reflection_clang_struct_info);
	fy_check_testcase_add_test(ctc, reflection_clang_struct_parse);
#endif

	/* packed backend tests */
	ctc = fy_check_suite_add_test_case(cs, "reflection-packed");

#ifdef HAVE_LIBCLANG
	fy_check_testcase_add_test(ctc, reflection_packed_roundtrip);
#endif
	fy_check_testcase_add_test(ctc, reflection_packed_parse_int);
}
