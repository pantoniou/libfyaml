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
#include <limits.h>
#include <assert.h>

#include <check.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-generic.h>
#ifdef HAVE_REFLECTION
#include <libfyaml/libfyaml-reflection.h>
#include "fy-reflection-private.h"
#endif

#include "fy-check.h"
#include "libfyaml-test-alloc-fail.h"

#if defined(__linux__) && defined(HAVE_GENERIC)
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

/* A packed "struct foo { int a; struct { int b; } in; int arr[4]; }" blob. */
static const unsigned char packed_struct_foo_blob[] = {
	0x46, 0x59, 0x50, 0x47, 0x01, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x96, 0x10, 0x01, 0x1b, 0x00, 0x00, 0x07, 0x04,
	0x16, 0x03, 0x01, 0x01, 0x00, 0x00, 0x00, 0x07,
	0x00, 0x07, 0x01, 0x00, 0x01, 0x01, 0x02, 0x03,
	0x00, 0x07, 0x00, 0x07, 0x07, 0x00, 0x07, 0x01,
	0x00, 0x09, 0x00, 0x07, 0x01, 0x01, 0x0c, 0x00,
	0x00, 0x62, 0x00, 0x66, 0x6f, 0x6f, 0x00, 0x61,
	0x00, 0x69, 0x6e, 0x00, 0x61, 0x72, 0x72, 0x00,
};

static struct fy_type_info *
packed_struct_foo_lookup(struct fy_reflection *rfl, const char *name)
{
	const struct fy_type_info *ti;
	void *prev = NULL;

	while ((ti = fy_type_info_iterate(rfl, &prev)) != NULL) {
		if (!strcmp(ti->name, name))
			return (struct fy_type_info *)ti;
	}
	return NULL;
}

/* Test: gh#353 - an anonymous record must not contain itself. */
START_TEST(fuzz_issue_353_anonymous_record_cycle_repro)
{
	static const unsigned char blob[] = {
		0x46, 0x59, 0x50, 0x47, 0x02, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2f,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33,
		0x00, 0x00, 0xff, 0xf6, 0x38, 0x39, 0x35, 0x00,
		0x1a, 0x00, 0x01, 0x08, 0x1a, 0x00, 0x01, 0x00,
		0x3c, 0x01, 0x01, 0x08, 0x16, 0x07, 0x1a, 0x00,
		0x01, 0x03, 0x19, 0x04, 0x01, 0x04, 0x1a, 0x05,
		0x00, 0x27, 0x1c, 0x06, 0x00, 0x07, 0x9a, 0xe7,
		0x24, 0x01, 0x04, 0x98, 0x0e, 0x04, 0x01, 0x27,
		0x98, 0x64, 0x07, 0x00, 0x07, 0x16, 0x07, 0x05,
		0x01, 0x02, 0x01, 0x00, 0x01, 0x01, 0x03, 0x08,
		0x00, 0x07, 0x00, 0x07, 0x0c, 0x00, 0x05, 0x01,
		0x05, 0x0e, 0x00, 0x05, 0x01, 0x06, 0x12, 0x00,
		0x05, 0x01, 0x07, 0x18, 0x00, 0x01, 0x01, 0xf4,
		0x1d, 0x01, 0x07, 0x01, 0x0a, 0x21, 0x00, 0x07,
		0x01, 0x09, 0x0e, 0x00, 0x07, 0x00, 0x80, 0x25,
		0x00, 0x07, 0x01, 0x09, 0x27, 0x00, 0x07, 0x01,
		0x09, 0x2a, 0x00, 0x07, 0x01, 0x09, 0x2e, 0x00,
		0x00, 0x75, 0x77, 0x6e, 0x74, 0x70, 0xba, 0xba,
		0xba, 0xba, 0xba, 0xba, 0xba, 0xba, 0xba, 0xba,
		0xba, 0xba, 0xba, 0xba, 0xba, 0xba, 0xba, 0xba,
		0xba, 0xba, 0xba, 0xba, 0xba, 0xba, 0xba, 0xba,
		0xba, 0xba, 0xba, 0xba, 0xba, 0x07, 0x05, 0x01,
		0x13, 0x01, 0x03, 0x03, 0x00, 0x21, 0x01, 0x0a,
		0x2e, 0x00, 0x00, 0x75,
	};
	struct fy_reflection *rfl;
	struct fy_type_info *ti;
	struct fy_field_info *fi;
	char *generated;

	/* the reported blob marks an enum as an anonymous record */
	rfl = fy_reflection_from_packed_blob(blob, sizeof(blob), NULL);
	ck_assert_ptr_eq(rfl, NULL);

	/* make the anonymous record of struct foo contain itself */
	rfl = fy_reflection_from_packed_blob(packed_struct_foo_blob,
			sizeof(packed_struct_foo_blob), NULL);
	ck_assert_ptr_ne(rfl, NULL);
	ti = packed_struct_foo_lookup(rfl, "struct @anonymous-1");
	ck_assert_ptr_ne(ti, NULL);
	ck_assert_uint_eq(ti->count, 1);
	fi = (struct fy_field_info *)&ti->fields[0];
	ti->flags |= FYTIF_ANONYMOUS_RECORD_DECL;
	fi->type_info = ti;

	generated = fy_reflection_generate_c_string(rfl,
			FYCGF_INDENT_TAB | FYCGF_COMMENT_NONE);
	ck_assert_ptr_eq(generated, NULL);
	fy_reflection_destroy(rfl);
}
END_TEST

/* Test: gh#354 - an anonymous record field must be a record. */
START_TEST(fuzz_issue_354_anonymous_record_kind_repro)
{
	static const unsigned char blob[] = {
		0x46, 0x59, 0x50, 0x47, 0x02, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2f,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33,
		0x00, 0x00, 0xff, 0xf6, 0x38, 0x39, 0x35, 0x00,
		0x1a, 0x00, 0x01, 0x08, 0x19, 0x01, 0x01, 0x00,
		0x3c, 0x01, 0x01, 0x08, 0x16, 0x07, 0x1a, 0x00,
		0x01, 0x03, 0x19, 0x04, 0x01, 0x04, 0x1a, 0x05,
		0x00, 0x27, 0x1c, 0x06, 0x00, 0x07, 0x9b, 0x26,
		0x24, 0x01, 0x04, 0x98, 0x19, 0x05, 0x00, 0x27,
		0x98, 0xe7, 0x07, 0x00, 0x07, 0x16, 0x07, 0x05,
		0x01, 0x02, 0x01, 0x00, 0x01, 0x15, 0x03, 0x08,
		0x00, 0x07, 0x00, 0x07, 0x0c, 0x00, 0x05, 0x01,
		0x05, 0x0e, 0x00, 0x05, 0x01, 0x06, 0x12, 0x00,
		0x05, 0x01, 0x07, 0x18, 0x00, 0x01, 0x01, 0xf4,
		0x1d, 0x00, 0x07, 0x01, 0x0a, 0x21, 0x00, 0x07,
		0x01, 0x08, 0xf5, 0x00, 0x07, 0x00, 0x80, 0x25,
		0x00, 0x07, 0x01, 0x09, 0x27, 0x00, 0x07, 0x01,
		0x09, 0x2a, 0x00, 0x07, 0x01, 0x09, 0x2e, 0x00,
		0x00, 0x75, 0x77, 0x6e, 0x74, 0x70, 0x70, 0x00,
		0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1a, 0x00,
		0x00, 0x08, 0x1a, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x2d, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00,
		0x33, 0x35, 0x38, 0x00, 0x80, 0x25, 0x00, 0x07,
		0x01, 0x06, 0x02, 0x01,
	};
	struct fy_reflection *rfl;
	struct fy_type_info *ti;
	char *generated;

	/* the reported blob marks a constant array as an anonymous record */
	rfl = fy_reflection_from_packed_blob(blob, sizeof(blob), NULL);
	ck_assert_ptr_eq(rfl, NULL);

	/* mark the int[4] field type of struct foo as an anonymous record */
	rfl = fy_reflection_from_packed_blob(packed_struct_foo_blob,
			sizeof(packed_struct_foo_blob), NULL);
	ck_assert_ptr_ne(rfl, NULL);
	ti = packed_struct_foo_lookup(rfl, "int[4]");
	ck_assert_ptr_ne(ti, NULL);
	ck_assert_ptr_eq(ti->fields, NULL);
	ti->flags |= FYTIF_ANONYMOUS_RECORD_DECL;

	generated = fy_reflection_generate_c_string(rfl,
			FYCGF_INDENT_TAB | FYCGF_COMMENT_NONE);
	ck_assert_ptr_eq(generated, NULL);
	fy_reflection_destroy(rfl);
}
END_TEST

static unsigned int packed_type_info_count(struct fy_reflection *rfl)
{
	void *prev = NULL;
	unsigned int count = 0;

	while (fy_type_info_iterate(rfl, &prev) != NULL)
		count++;
	return count;
}

/* Test: gh#355 - type info iteration must not change after an error. */
START_TEST(fuzz_issue_355_type_info_iteration_repro)
{
	static const unsigned char blob[] = {
		0x46, 0x59, 0x50, 0x47, 0x01, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2f,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x1a, 0x00, 0x00, 0x08, 0x1a, 0x00, 0x01, 0x00,
		0x19, 0x01, 0x01, 0x01, 0x16, 0x02, 0x1a, 0x00,
		0x01, 0x02, 0x19, 0x04, 0x01, 0x0a, 0x19, 0x05,
		0x00, 0x27, 0x19, 0x06, 0x01, 0x06, 0x98, 0xeb,
		0xfb, 0x01, 0x04, 0x99, 0x09, 0x05, 0x00, 0x27,
		0x98, 0xfa, 0x06, 0x00, 0x07, 0x16, 0x07, 0x05,
		0x01, 0x02, 0x01, 0x00, 0x01, 0x01, 0x03, 0x08,
		0x00, 0x07, 0x00, 0x07, 0x0c, 0x00, 0x05, 0x01,
		0x05, 0x0e, 0x00, 0x05, 0x01, 0x06, 0x13, 0x00,
		0x05, 0x01, 0x07, 0x18, 0x00, 0x01, 0x01, 0x0b,
		0x1d, 0x00, 0x07, 0x01, 0x02, 0x21, 0x00, 0x07,
		0x01, 0x08, 0x0e, 0x00, 0x07, 0x00, 0x27, 0x25,
		0x00, 0x07, 0x01, 0x06, 0x27, 0x00, 0x07, 0x01,
		0x09, 0x2a, 0x00, 0x07, 0x01, 0x0a, 0x2e, 0x00,
		0x00, 0x75, 0x69, 0x6e, 0x74, 0x70, 0x70, 0x00,
		0x62, 0x61, 0x72, 0x00, 0x62, 0x00, 0x62, 0x61,
		0x72, 0x70, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x00,
		0x00, 0x63, 0x00, 0x63, 0x63, 0x00, 0x6f, 0x6f,
		0x00, 0x61, 0x70, 0x70, 0x69, 0x6e, 0x74, 0x69,
		0x00, 0x66, 0x63, 0x63, 0x63, 0x00, 0x63, 0x63,
		0x63, 0x63, 0x00,
	};
	struct fy_reflection *rfl;
	struct fy_type_info *ti;
	struct fy_type *ft;
	struct fy_decl *decl;

	/* the reported blob marks a typedef as an anonymous record */
	rfl = fy_reflection_from_packed_blob(blob, sizeof(blob), NULL);
	ck_assert_ptr_eq(rfl, NULL);

	/* make the type info update of struct foo fail */
	rfl = fy_reflection_from_packed_blob(packed_struct_foo_blob,
			sizeof(packed_struct_foo_blob), NULL);
	ck_assert_ptr_ne(rfl, NULL);
	ti = packed_struct_foo_lookup(rfl, "struct foo");
	ck_assert_ptr_ne(ti, NULL);
	ft = fy_type_from_info(ti);
	ck_assert_ptr_ne(ft, NULL);
	decl = ft->decl;
	ft->decl = NULL;
	ft->flags &= ~FYTF_TYPE_INFO_UPDATED;

	/* every iteration must stop at the same type */
	ck_assert_uint_eq(packed_type_info_count(rfl), 3);
	ck_assert_uint_eq(packed_type_info_count(rfl), 3);

	ft->decl = decl;
	fy_reflection_destroy(rfl);
}
END_TEST

/* Test: gh#375 - emit the smallest value of a signed type. */
START_TEST(fuzz_issue_375_integer_scalar_negate_repro)
{
	static const char yaml[] = "  -9223372036854775808";
	struct fy_reflection *rfl;
	struct fy_type_context *ctx;
	struct fy_parser *fyp;
	struct fy_emitter *emit;
	void *data = NULL;
	char *out;

	rfl = fy_reflection_from_null(NULL);
	ck_assert_ptr_ne(rfl, NULL);

	ctx = fy_type_context_create(&(struct fy_type_context_cfg){
		.rfl = rfl,
		.entry_type = "long long",
		.entry_meta = "- -" });
	ck_assert_ptr_ne(ctx, NULL);

	fyp = fy_parser_create(NULL);
	ck_assert_ptr_ne(fyp, NULL);
	ck_assert_int_eq(fy_parser_set_string(fyp, yaml, sizeof(yaml) - 1), 0);
	ck_assert_int_eq(fy_type_context_parse(ctx, fyp, &data), 0);
	ck_assert_ptr_ne(data, NULL);

	emit = fy_emit_to_string(FYECF_DEFAULT);
	ck_assert_ptr_ne(emit, NULL);

	/* the negation of the value must not overflow */
	fy_type_context_emit(ctx, emit, data,
			     FYTCEF_SS | FYTCEF_DS | FYTCEF_DE | FYTCEF_SE);

	out = fy_emit_to_string_collect(emit, NULL);
	if (out)
		free(out);
	fy_emitter_destroy(emit);
	fy_type_context_free_data(ctx, data);
	fy_parser_destroy(fyp);
	fy_type_context_destroy(ctx);
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

/* Test: gh#352 - nested YPath errors must free cloned walk results. */
START_TEST(fuzz_issue_352_ypath_method_args_leak_repro)
{
	static const unsigned char yaml[] = {
		0x23, 0x20, 0x10, 0x20, 0x0d, 0x20, 0x2d, 0x20,
		0x4a, 0x2f, 0x2a, 0x2a, 0x20, 0x0d, 0x20, 0x2d,
		0x20, 0x20, 0x4a, 0x2f, 0x37, 0x7c, 0x62, 0x2a,
		0x2f, 0x2a, 0x29, 0x36, 0x36, 0x30, 0x42, 0x2f,
		0x36, 0x20, 0x0d, 0x0d, 0x20, 0x2d, 0x20, 0x4a,
		0x2f, 0x2a, 0x2a, 0x2f, 0x36, 0x32, 0x0d, 0x20,
		0x2d, 0x20, 0x0d, 0x20, 0x2d, 0x20, 0x2d, 0x20,
		0x2a, 0x2f, 0x37, 0x3c, 0x2a, 0x2f, 0x20, 0x0d,
		0x20, 0x2d, 0x20, 0x2a, 0x2f, 0x37, 0x3c, 0x62,
		0x2a, 0x2f, 0x2a, 0x2a, 0x2f, 0x61, 0x6c, 0x6c,
		0x28, 0x73, 0x75, 0x6d, 0x28, 0x2f, 0x2a, 0x39,
		0x31, 0x38, 0x32, 0x32, 0x34, 0x6b, 0x22, 0x61,
		0x22, 0x26, 0x26, 0x29, 0x3d, 0x3d, 0x6b, 0x22,
		0x61, 0x22, 0x26, 0x26, 0x29, 0x3d, 0x3d, 0x22,
		0x61, 0x22, 0x20, 0x0d, 0x20, 0x2d, 0x20, 0x2a,
		0x2f, 0x37, 0x37, 0x3c, 0x62, 0x2a, 0x2f, 0x2a,
		0x2a, 0x2f, 0x36, 0x2f, 0x42, 0x2f, 0x36, 0x20,
		0x0d, 0x20, 0x2d, 0x20, 0x2a, 0x2f, 0x37, 0x3c,
		0x62, 0x2a, 0x2f, 0x2a, 0x2a, 0x2f, 0x36, 0x2f,
		0x42, 0x0a,
	};
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET | FYPCF_YPATH_ALIASES,
	};
	struct fy_document *fyd, *fyd2;
	struct fy_document_iterator *fydi;
	struct fy_node *fyn;

	fyd = fy_document_build_from_string(&cfg, (const char *)yaml,
			sizeof(yaml));
	ck_assert_ptr_ne(fyd, NULL);

	fyd2 = fy_document_clone(fyd);
	ck_assert_ptr_ne(fyd2, NULL);
	(void)fy_document_resolve(fyd2);

	fydi = fy_document_iterator_create();
	ck_assert_ptr_ne(fydi, NULL);
	fy_document_iterator_node_start(fydi, fy_document_root(fyd));
	while ((fyn = fy_document_iterator_node_next(fydi)) != NULL) {
		if (fy_node_is_alias(fyn)) {
			(void)fy_node_resolve_alias(fyn);
			(void)fy_node_dereference(fyn);
		}
	}
	fy_document_iterator_destroy(fydi);

	fy_document_destroy(fyd2);
	fy_document_destroy(fyd);
}
END_TEST

/* Test: gh#356 - a failed YPath argument must free earlier arguments. */
START_TEST(fuzz_issue_356_ypath_method_args_leak_repro)
{
	static const char yaml[] = "*$(sum(36>/tree/branch/b7\"name\")";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET | FYPCF_YPATH_ALIASES,
	};
	struct fy_document *fyd, *fyd2;
	struct fy_document_iterator *fydi;
	struct fy_node *fyn;

	fyd = fy_document_build_from_string(&cfg, yaml, sizeof(yaml) - 1);
	ck_assert_ptr_ne(fyd, NULL);

	fyd2 = fy_document_clone(fyd);
	ck_assert_ptr_ne(fyd2, NULL);
	(void)fy_document_resolve(fyd2);

	fydi = fy_document_iterator_create();
	ck_assert_ptr_ne(fydi, NULL);
	fy_document_iterator_node_start(fydi, fy_document_root(fyd));
	while ((fyn = fy_document_iterator_node_next(fydi)) != NULL) {
		if (fy_node_is_alias(fyn)) {
			(void)fy_node_resolve_alias(fyn);
			(void)fy_node_dereference(fyn);
		}
	}
	fy_document_iterator_destroy(fydi);

	fy_document_destroy(fyd2);
	fy_document_destroy(fyd);
}
END_TEST

/* Test: gh#358 - an index argument outside the int range matches nothing. */
START_TEST(fuzz_issue_358_ypath_index_range_repro)
{
	/*
	 * The test crashes on Windows with clang, before it runs. The
	 * crash is not reproducible with clang under wine, and the defect
	 * that the test covers is reported by UBSan on the other
	 * platforms.
	 */
#ifndef _WIN32
	static const char yaml[] = "- a\n- b\n";
	struct fy_path_parse_cfg parse_cfg = {
		.flags = FYPPCF_QUIET,
	};
	struct fy_path_exec_cfg xcfg = {
		.flags = FYPXCF_QUIET,
	};
	/* an index that the int range cannot hold */
	static const char path[] = "/index(100000000000000000000)";
	struct fy_document *fyd;
	struct fy_path_expr *expr;
	struct fy_path_exec *fypx;
	struct fy_node *fyn;
	void *iter = NULL;
	unsigned int count = 0;

	fyd = fy_document_build_from_string(NULL, yaml, sizeof(yaml) - 1);
	ck_assert_ptr_ne(fyd, NULL);
	expr = fy_path_expr_build_from_string(&parse_cfg, path, sizeof(path) - 1);
	ck_assert_ptr_ne(expr, NULL);
	fypx = fy_path_exec_create(&xcfg);
	ck_assert_ptr_ne(fypx, NULL);

	fy_path_exec_execute(fypx, expr, fy_document_root(fyd));
	while ((fyn = fy_path_exec_results_iterate(fypx, &iter)) != NULL)
		count++;
	ck_assert_uint_eq(count, 0);

	fy_path_exec_destroy(fypx);
	fy_path_expr_free(expr);
	fy_document_destroy(fyd);
#endif
}
END_TEST

/* Test: gh#359 - removing a comment that is not the first one. */
START_TEST(fuzz_issue_359_token_comment_unlink_repro)
{
	static const char yaml[] = "a\n";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET | FYPCF_KEEP_COMMENTS,
	};
	struct fy_document *fyd;
	struct fy_token *fyt;

	fyd = fy_document_build_from_string(&cfg, yaml, sizeof(yaml) - 1);
	ck_assert_ptr_ne(fyd, NULL);
	fyt = fy_node_get_scalar_token(fy_document_root(fyd));
	ck_assert_ptr_ne(fyt, NULL);

	ck_assert_int_eq(fy_token_set_comment(fyt, fycp_top, "c", 1), 0);
	ck_assert_int_eq(fy_token_set_comment(fyt, fycp_right, "c", 1), 0);
	ck_assert_int_eq(fy_token_set_comment(fyt, fycp_bottom, "c", 1), 0);

	/* remove the one in the middle of the list */
	ck_assert_int_eq(fy_token_set_comment(fyt, fycp_right, NULL, 0), 0);

	/* the others must still be there */
	ck_assert_ptr_ne(fy_token_get_comment(fyt, fycp_top), NULL);
	ck_assert_ptr_eq(fy_token_get_comment(fyt, fycp_right), NULL);
	ck_assert_ptr_ne(fy_token_get_comment(fyt, fycp_bottom), NULL);

	fy_document_destroy(fyd);
}
END_TEST

#ifdef HAVE_LINKER_WRAP_MALLOC

/*
 * Run a scenario once for every allocation that it makes, failing a
 * different one each time. The scenario arms the failure itself, so that
 * it covers only the calls that are under test.
 */
static void
alloc_fail_sweep(void (*fn)(unsigned int nth), unsigned int max)
{
	unsigned int n;

	for (n = 1; n <= max; n++) {
		fn(n);
		fy_alloc_fail_disarm();
		/* the scenario made fewer allocations than that */
		if (fy_alloc_fail_seen() < n)
			return;
	}
	ck_abort_msg("the scenario makes more than %u allocations", max);
}

static void issue_357_scenario(unsigned int nth)
{
	static const char yaml[] = "key: value\n";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET,
	};
	struct fy_document *fyd;

	fyd = fy_document_build_from_string(&cfg, yaml, sizeof(yaml) - 1);
	ck_assert_ptr_ne(fyd, NULL);
	fy_alloc_fail_arm(nth);
	fy_node_by_path(fy_document_root(fyd), ".///key", FY_NT, FYNWF_PTR_YPATH);
	fy_alloc_fail_disarm();
	fy_document_destroy(fyd);
}

static void issue_360_scenario(unsigned int nth)
{
	static const char yaml[] = "l";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET,
	};
	struct fy_document *fyd;
	struct fy_tag **tags;

	fyd = fy_document_build_from_string(&cfg, yaml, sizeof(yaml) - 1);
	ck_assert_ptr_ne(fyd, NULL);
	fy_alloc_fail_arm(nth);
	tags = fy_document_state_tag_directives(fy_document_get_document_state(fyd));
	fy_alloc_fail_disarm();
	if (tags)
		free(tags);
	fy_document_destroy(fyd);
}

static void issue_361_scenario(unsigned int nth)
{
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET | FYPCF_RESOLVE_DOCUMENT |
			 FYPCF_PREFER_RECURSIVE,
	};

	fy_alloc_fail_arm(nth);
	fy_document_destroy(fy_document_build_from_string(&cfg, ":", 1));
	fy_alloc_fail_disarm();
}

static void issue_362_scenario(unsigned int nth)
{
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET,
	};

	fy_alloc_fail_arm(nth);
	fy_document_destroy(fy_document_build_from_string(&cfg, ":", 1));
	fy_alloc_fail_disarm();
}

static void issue_363_scenario(unsigned int nth)
{
	static const char yaml[] = "eede&:de&:\n[&:\n!";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_COLLECT_DIAG | FYPCF_RESOLVE_DOCUMENT |
			 FYPCF_DISABLE_MMAP_OPT | FYPCF_DISABLE_RECYCLING |
			 FYPCF_DISABLE_ACCELERATORS |
			 FYPCF_SLOPPY_FLOW_INDENTATION |
			 FYPCF_RELAXED_FLOW_DOC | FYPCF_KEEP_ANCHORS |
			 FYPCF_ENABLE_CACHE,
	};
	struct fy_parser *fyp;
	struct fy_event *fyev;
	struct fy_token *tag;
	size_t len;

	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);
	ck_assert_int_eq(fy_parser_set_string(fyp, yaml, sizeof(yaml) - 1), 0);
	fy_alloc_fail_arm(nth);
	while ((fyev = fy_parser_parse(fyp)) != NULL) {
		tag = fy_event_get_tag_token(fyev);
		if (tag)
			fy_tag_token_suffix(tag, &len);
		fy_parser_event_free(fyp, fyev);
	}
	fy_alloc_fail_disarm();
	fy_parser_destroy(fyp);
}

static void issue_364_scenario(unsigned int nth)
{
	struct fy_parser *fyp;

	fy_alloc_fail_arm(nth);
	fyp = fy_parser_create(NULL);
	fy_alloc_fail_disarm();
	if (fyp)
		fy_parser_destroy(fyp);
}

static void issue_365_scenario(unsigned int nth)
{
	static const char yaml[] = "a: b\n";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET,
	};
	struct fy_parser *fyp;
	struct fy_event *fyev;

	fy_alloc_fail_arm(nth);
	fyp = fy_parser_create(&cfg);
	if (fyp) {
		if (!fy_parser_set_string(fyp, yaml, sizeof(yaml) - 1)) {
			while ((fyev = fy_parser_parse(fyp)) != NULL)
				fy_parser_event_free(fyp, fyev);
		}
		fy_parser_destroy(fyp);
	}
	fy_alloc_fail_disarm();
}

static void issue_366_scenario(unsigned int nth)
{
	static const char yaml[] = "/////0";
	struct fy_parser *fyp;
	struct fy_parser_checkpoint *fypchk;
	struct fy_event *fyev;
	FILE *fp;
	int i;

	/* a stream input, so that the reader allocates a buffer of its own */
	fp = tmpfile();
	if (!fp)
		return;
	if (fwrite(yaml, 1, sizeof(yaml) - 1, fp) != sizeof(yaml) - 1) {
		fclose(fp);
		return;
	}
	rewind(fp);

	fyp = fy_parser_create(&(struct fy_parse_cfg){ .flags = FYPCF_QUIET });
	if (!fyp) {
		fclose(fp);
		return;
	}

	fy_alloc_fail_arm(nth);

	if (!fy_parser_set_input_fp(fyp, NULL, fp)) {

		/* pull one event so that the checkpoint is taken mid-stream */
		fyev = fy_parser_parse(fyp);
		if (fyev)
			fy_parser_event_free(fyp, fyev);

		fypchk = fy_parser_checkpoint_create(fyp);
		if (fypchk) {
			for (i = 0; i < 2; i++) {
				fyev = fy_parser_parse(fyp);
				if (!fyev)
					break;
				fy_parser_event_free(fyp, fyev);
			}
			fy_parser_rollback(fyp, fypchk);
			fy_parser_checkpoint_destroy(fypchk);
		}

		while ((fyev = fy_parser_parse(fyp)) != NULL)
			fy_parser_event_free(fyp, fyev);
	}

	/* reuse the parser over the same input */
	fy_parser_reset(fyp);

	rewind(fp);
	if (!fy_parser_set_input_fp(fyp, NULL, fp)) {
		while ((fyev = fy_parser_parse(fyp)) != NULL)
			fy_parser_event_free(fyp, fyev);
	}

	fy_alloc_fail_disarm();
	fy_parser_destroy(fyp);
	fclose(fp);
}

static void issue_369_scenario(unsigned int nth)
{
	static const char yaml[] = ":[                 : &b\n-\n  ";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET | FYPCF_DISABLE_RECYCLING |
			 FYPCF_DEFAULT_VERSION_AUTO,
	};
	struct fy_parser *fyp;
	struct fy_event *fyev;

	fyp = fy_parser_create(&cfg);
	if (!fyp)
		return;

	if (fy_parser_set_string(fyp, yaml, sizeof(yaml) - 1)) {
		fy_parser_destroy(fyp);
		return;
	}

	fy_alloc_fail_arm(nth);
	while ((fyev = fy_parser_parse(fyp)) != NULL)
		fy_parser_event_free(fyp, fyev);
	fy_alloc_fail_disarm();

	fy_parser_destroy(fyp);
}

static void issue_374_scenario(unsigned int nth)
{
	struct fy_emitter *emit;
	char *out;

	emit = fy_emit_to_string(FYECF_DEFAULT);
	if (!emit)
		return;

	fy_emit_eventf(emit, FYET_STREAM_START);

	fy_alloc_fail_arm(nth);
	fy_emit_eventf(emit, FYET_DOCUMENT_START, 0, NULL, NULL);
	fy_alloc_fail_disarm();

	out = fy_emit_to_string_collect(emit, NULL);
	if (out)
		free(out);
	fy_emitter_destroy(emit);
}

static void issue_376_scenario(unsigned int nth)
{
	static const char yaml[] = "'a\tb'\n";
	struct fy_document *fyd;
	FILE *fp;

	fyd = fy_document_build_from_string(NULL, yaml, sizeof(yaml) - 1);
	ck_assert_ptr_ne(fyd, NULL);

	fp = tmpfile();
	ck_assert_ptr_ne(fp, NULL);

	fy_alloc_fail_arm(nth);
	fy_emit_document_to_fp(fyd, FYECF_DEFAULT, fp);
	fy_alloc_fail_disarm();

	fclose(fp);
	fy_document_destroy(fyd);
}

static void issue_377_scenario(unsigned int nth)
{
	static const char yaml[] = "OOOc";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET | FYPCF_PREFER_RECURSIVE,
	};
	struct fy_document *fyd;

	fyd = fy_document_build_from_string(&cfg, yaml, sizeof(yaml) - 1);
	ck_assert_ptr_ne(fyd, NULL);

	fy_alloc_fail_arm(nth);
	fy_node_set_anchor(fy_document_root(fyd), yaml, sizeof(yaml) - 1);
	fy_alloc_fail_disarm();

	fy_document_destroy(fyd);
}

static void issue_378_scenario(unsigned int nth)
{
	static const char yaml[] = {
		0x20, 0x20, 0x20, 0x20, 0x1f, 0x20, 0x26, 0x7e, 0x01, 0x2d,
		0x2a, 0x3a, 0x20, 0x26, 0x61, 0x0a,
	};
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET | FYPCF_RESOLVE_DOCUMENT |
			 FYPCF_DISABLE_RECYCLING | FYPCF_SLOPPY_FLOW_INDENTATION |
			 FYPCF_PREFER_RECURSIVE | FYPCF_RELAXED_FLOW_DOC |
			 FYPCF_KEEP_ANCHORS | FYPCF_ENABLE_CACHE |
			 FYPCF_DEFAULT_VERSION_1_3 | FYPCF_JSON_NONE,
	};

	fy_alloc_fail_arm(nth);
	fy_document_destroy(fy_document_build_from_string(&cfg, yaml, sizeof(yaml)));
	fy_alloc_fail_disarm();
}

static void checkpoint_alloc_scenario(unsigned int nth)
{
	static const char yaml[] = "a: [b, c]\n";
	struct fy_parser *fyp;
	struct fy_parser_checkpoint *fypchk;
	struct fy_event *fyev;

	fyp = fy_parser_create(&(struct fy_parse_cfg){ .flags = FYPCF_QUIET });
	if (!fyp)
		return;

	if (fy_parser_set_string(fyp, yaml, sizeof(yaml) - 1)) {
		fy_parser_destroy(fyp);
		return;
	}

	fyev = fy_parser_parse(fyp);
	if (fyev)
		fy_parser_event_free(fyp, fyev);

	fy_alloc_fail_arm(nth);
	fypchk = fy_parser_checkpoint_create(fyp);
	fy_alloc_fail_disarm();

	if (fypchk) {
		fy_parser_rollback(fyp, fypchk);
		fy_parser_checkpoint_destroy(fypchk);
	}

	while ((fyev = fy_parser_parse(fyp)) != NULL)
		fy_parser_event_free(fyp, fyev);

	fy_parser_destroy(fyp);
}

static enum fy_composer_return
compose_continue_cb(struct fy_parser *fyp FY_UNUSED, struct fy_event *fye FY_UNUSED,
		    struct fy_path *path FY_UNUSED, void *userdata FY_UNUSED)
{
	return FYCR_OK_CONTINUE;
}

static void issue_379_scenario(unsigned int nth)
{
	static const char yaml[] = "a: b\n";
	struct fy_parser *fyp;

	fyp = fy_parser_create(&(struct fy_parse_cfg){ .flags = FYPCF_QUIET });
	ck_assert_ptr_ne(fyp, NULL);
	ck_assert_int_eq(fy_parser_set_string(fyp, yaml, sizeof(yaml) - 1), 0);
	fy_alloc_fail_arm(nth);
	fy_parse_compose(fyp, compose_continue_cb, NULL);
	fy_alloc_fail_disarm();
	fy_parser_destroy(fyp);
}

/* Parse a stream input to the end, with an optional rollback in the middle. */
static void stream_parse_scenario(unsigned int nth, const char *yaml, size_t len,
				  enum fy_parse_cfg_flags flags, bool rollback)
{
	struct fy_parser *fyp;
	struct fy_parser_checkpoint *fypchk;
	struct fy_event *fyev;
	FILE *fp;
	int i;

	/* a stream input, so that the reader allocates a buffer of its own */
	fp = tmpfile();
	if (!fp)
		return;
	if (fwrite(yaml, 1, len, fp) != len) {
		fclose(fp);
		return;
	}
	rewind(fp);

	fyp = fy_parser_create(&(struct fy_parse_cfg){ .flags = flags | FYPCF_QUIET });
	ck_assert_ptr_ne(fyp, NULL);

	fy_alloc_fail_arm(nth);

	if (!fy_parser_set_input_fp(fyp, NULL, fp)) {
		if (rollback) {
			fyev = fy_parser_parse(fyp);
			if (fyev)
				fy_parser_event_free(fyp, fyev);

			fypchk = fy_parser_checkpoint_create(fyp);
			if (fypchk) {
				for (i = 0; i < 2; i++) {
					fyev = fy_parser_parse(fyp);
					if (!fyev)
						break;
					fy_parser_event_free(fyp, fyev);
				}
				fy_parser_rollback(fyp, fypchk);
				fy_parser_checkpoint_destroy(fypchk);
			}
		}

		while ((fyev = fy_parser_parse(fyp)) != NULL)
			fy_parser_event_free(fyp, fyev);
	}

	fy_alloc_fail_disarm();
	fy_parser_destroy(fyp);
	fclose(fp);
}

static void issue_380_scenario(unsigned int nth)
{
	static const char yaml[] = "l";

	stream_parse_scenario(nth, yaml, sizeof(yaml) - 1, 0, false);
}

static void issue_381_scenario(unsigned int nth)
{
	static const char yaml[] =
		"\"top1\" : \n"
		"  \"key1\" : &alias1 scalar1\n"
		"'top2' : \n"
		"  'key2' : &alias2 scalar2\n"
		"top3: &node3 \n"
		"  *alias1 : scalar3\n"
		"top4: \n"
		"  *alias2 : scalar4\n"
		"top5   :    \n"
		"  scalar5\n"
		"top6: \n"
		"  &anchor6 'key6' : scalar6\n";

	fy_alloc_fail_arm(nth);
	fy_document_destroy(fy_document_build_from_string(
			&(struct fy_parse_cfg){ .flags = FYPCF_QUIET },
			yaml, sizeof(yaml) - 1));
	fy_alloc_fail_disarm();
}

/* Resolve the clone of a document, which gets the ypath aliases of it. */
static void resolve_clone_scenario(unsigned int nth, const char *yaml, size_t len,
				   enum fy_parse_cfg_flags flags)
{
	struct fy_parse_cfg cfg = {
		.flags = flags | FYPCF_QUIET,
	};
	struct fy_document *fyd, *fyd2;

	fyd = fy_document_build_from_string(&cfg, yaml, len);
	ck_assert_ptr_ne(fyd, NULL);
	fyd2 = fy_document_clone(fyd);
	ck_assert_ptr_ne(fyd2, NULL);

	fy_alloc_fail_arm(nth);
	fy_document_resolve(fyd2);
	fy_alloc_fail_disarm();

	fy_document_destroy(fyd2);
	fy_document_destroy(fyd);
}

static void issue_382_scenario(unsigned int nth)
{
	static const char yaml[] =
		" &F\n"
		" fooOO- << : *F%%%'a''b'%%%%%%%%%%%%%%%%%%%%%--- ";

	resolve_clone_scenario(nth, yaml, sizeof(yaml) - 1,
			FYPCF_DISABLE_MMAP_OPT | FYPCF_DISABLE_RECYCLING |
			FYPCF_KEEP_COMMENTS | FYPCF_YPATH_ALIASES |
			FYPCF_ALLOW_DUPLICATE_KEYS | FYPCF_ENABLE_CACHE |
			FYPCF_DEFAULT_VERSION_1_3 | FYPCF_JSON_NONE);
}

#ifdef HAVE_GENERIC
static void issue_383_scenario(unsigned int nth)
{
	static const char yaml[] =
		"bulk/*/name[[[[[[[]]^]]e: &b\n"
		"# nCms: ancho]]]]]]\n";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET | FYPCF_RESOLVE_DOCUMENT |
			 FYPCF_DISABLE_MMAP_OPT | FYPCF_DISABLE_RECYCLING |
			 FYPCF_KEEP_COMMENTS | FYPCF_DISABLE_DEPTH_LIMIT |
			 FYPCF_DISABLE_ACCELERATORS |
			 FYPCF_SLOPPY_FLOW_INDENTATION | FYPCF_PREFER_RECURSIVE |
			 FYPCF_YPATH_ALIASES | FYPCF_ALLOW_DUPLICATE_KEYS |
			 FYPCF_KEEP_STYLE | FYPCF_RELAXED_FLOW_DOC |
			 FYPCF_ENABLE_CACHE | FYPCF_DEFAULT_VERSION_1_1 |
			 FYPCF_JSON_NONE,
	};
	struct fy_generic_builder_cfg gb_cfg = {
		.flags = FYGBCF_CREATE_TAG | FYGBCF_SCHEMA_YAML1_2_FAILSAFE,
	};
	struct fy_generic_document_builder_cfg gdb_cfg = {0};
	struct fy_generic_document_builder *gdb;
	struct fy_generic_builder *gb;
	struct fy_parser *fyp;

	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);
	ck_assert_int_eq(fy_parser_set_string(fyp, yaml, sizeof(yaml) - 1), 0);
	gb = fy_generic_builder_create(&gb_cfg);
	ck_assert_ptr_ne(gb, NULL);
	gdb_cfg.parse_cfg = cfg;
	gdb_cfg.gb = gb;
	gdb_cfg.flags = FYGDBF_KEEP_COMMENTS | FYGDBF_CREATE_MARKERS |
			FYGDBF_PYYAML_COMPAT | FYGDBF_KEEP_STYLE |
			FYGDBF_KEEP_FAILSAFE_STR;
	gdb = fy_generic_document_builder_create(&gdb_cfg);
	ck_assert_ptr_ne(gdb, NULL);

	fy_alloc_fail_arm(nth);
	while (fy_generic_is_valid(fy_generic_document_builder_load_document(gdb, fyp)))
		;
	fy_alloc_fail_disarm();

	fy_generic_document_builder_destroy(gdb);
	fy_generic_builder_destroy(gb);
	fy_parser_destroy(fyp);
}
#endif

static void issue_384_scenario(unsigned int nth)
{
	static const char yaml[] =
		"\n"
		"[R\x07:\n"
		"[:\xc2\x86:\xc2\x85]l!l!";
	struct fy_document_builder *fydb;
	struct fy_document *fyd;
	struct fy_parser *fyp;
	struct fy_event *fyev;
	FILE *fp;

	/* a stream input, which is parsed twice with the same parser */
	fp = tmpfile();
	if (!fp)
		return;
	if (fwrite(yaml, 1, sizeof(yaml) - 1, fp) != sizeof(yaml) - 1) {
		fclose(fp);
		return;
	}
	rewind(fp);

	fyp = fy_parser_create(&(struct fy_parse_cfg){
			.flags = FYPCF_QUIET | FYPCF_KEEP_COMMENTS |
				 FYPCF_DISABLE_DEPTH_LIMIT |
				 FYPCF_SLOPPY_FLOW_INDENTATION |
				 FYPCF_CREATE_MARKERS | FYPCF_KEEP_STYLE |
				 FYPCF_RELAXED_FLOW_DOC | FYPCF_KEEP_ANCHORS |
				 FYPCF_DEFAULT_VERSION_1_3 | FYPCF_JSON_AUTO });
	ck_assert_ptr_ne(fyp, NULL);

	fy_alloc_fail_arm(nth);

	/* the events of this pass are recycled for the next one */
	if (!fy_parser_set_input_fp(fyp, NULL, fp)) {
		while ((fyev = fy_parser_parse(fyp)) != NULL) {
			fy_event_get_comments(fyev);
			fy_parser_event_free(fyp, fyev);
		}
	}

	fy_parser_reset(fyp);

	rewind(fp);
	if (!fy_parser_set_input_fp(fyp, NULL, fp)) {
		fydb = fy_document_builder_create_on_parser(fyp);
		if (fydb) {
			while ((fyd = fy_document_builder_load_document(fydb, fyp)) != NULL)
				fy_document_destroy(fyd);
			fy_document_builder_destroy(fydb);
		}
	}

	fy_parser_destroy(fyp);
	fy_alloc_fail_disarm();
	fclose(fp);
}

static void issue_388_scenario(unsigned int nth)
{
	static const char yaml[] =
		"{7[[[[[[[C]]]]]Y]] ics: tBst\n"
		"n: 1\n"
		"d:";

	stream_parse_scenario(nth, yaml, sizeof(yaml) - 1,
			FYPCF_RESOLVE_DOCUMENT | FYPCF_DISABLE_RECYCLING |
			FYPCF_DISABLE_DEPTH_LIMIT | FYPCF_YPATH_ALIASES |
			FYPCF_ALLOW_DUPLICATE_KEYS | FYPCF_CREATE_MARKERS |
			FYPCF_KEEP_STYLE | FYPCF_RELAXED_FLOW_DOC |
			FYPCF_KEEP_ANCHORS | FYPCF_DEFAULT_VERSION_AUTO |
			FYPCF_JSON_AUTO, true);
}

/* The parse flags of the merge key reports. */
#define MERGE_KEY_REPORT_FLAGS \
	(FYPCF_RESOLVE_DOCUMENT | FYPCF_DISABLE_RECYCLING | \
	 FYPCF_KEEP_COMMENTS | FYPCF_DISABLE_DEPTH_LIMIT | \
	 FYPCF_DISABLE_ACCELERATORS | FYPCF_SLOPPY_FLOW_INDENTATION | \
	 FYPCF_YPATH_ALIASES | FYPCF_ALLOW_DUPLICATE_KEYS | \
	 FYPCF_CREATE_MARKERS | FYPCF_KEEP_STYLE | FYPCF_RELAXED_FLOW_DOC | \
	 FYPCF_DEFAULT_VERSION_1_1 | FYPCF_JSON_NONE)

static void issue_389_scenario(unsigned int nth)
{
	static const char yaml[] =
		"<< : &b\n"
		"-   << : &b\n"
		"-  :  - - - - - - :  - - - - - - -%YAML 1.2  [ *DIG :*LEF/.T,"
		"/.T, \x1b/MALL ]";

	stream_parse_scenario(nth, yaml, sizeof(yaml) - 1,
			MERGE_KEY_REPORT_FLAGS, true);
}

static void issue_390_scenario(unsigned int nth)
{
	static const char yaml[] =
		"\\< : &b\n"
		"-   << : &b*-  :  - - - - - - :  - - - - - 2/- - J[ *BIG :*LE"
		"F/*a: vT, \x1b/MALL ]";

	stream_parse_scenario(nth, yaml, sizeof(yaml) - 1,
			MERGE_KEY_REPORT_FLAGS, true);
}

static void issue_391_scenario(unsigned int nth)
{
	static const char yaml[] =
		"*******valu(((e(4999ue((()////,e\":\"\xc3\xa9\\t\"*\x00\x10-="
		"*+910000000(I////,*\x0b,\x00\x10\x00*-,*\x00*<,*+,\x00\x00" "d"
		"e";

	resolve_clone_scenario(nth, yaml, sizeof(yaml) - 1,
			FYPCF_DISABLE_MMAP_OPT | FYPCF_DISABLE_RECYCLING |
			FYPCF_DISABLE_DEPTH_LIMIT | FYPCF_PREFER_RECURSIVE |
			FYPCF_YPATH_ALIASES | FYPCF_KEEP_STYLE |
			FYPCF_RELAXED_FLOW_DOC | FYPCF_DEFAULT_VERSION_AUTO |
			FYPCF_JSON_AUTO);
}

static void issue_392_scenario(unsigned int nth)
{
	static const char yaml[] =
		"<< : &b\n"
		"-   << : &b*-  :  - - - - - - :  - - - - - 2//M5LL ]";

	stream_parse_scenario(nth, yaml, sizeof(yaml) - 1,
			MERGE_KEY_REPORT_FLAGS, true);
}

/* Load and clone anchored collections, which register their anchors. */
static void anchor_collection_scenario(unsigned int nth)
{
	static const char yaml[] = "&a [ &b { k: &c v }, *c ]";
	struct fy_document *fyd, *fyd2;
	struct fy_node *fyn;

	fyd = fy_document_create(&(struct fy_parse_cfg){ .flags = FYPCF_QUIET });
	ck_assert_ptr_ne(fyd, NULL);

	fy_alloc_fail_arm(nth);
	fyn = fy_node_build_from_string(fyd, yaml, sizeof(yaml) - 1);
	if (fyn && fy_document_set_root(fyd, fyn))
		fy_node_free(fyn);
	fyd2 = fy_document_clone(fyd);
	fy_alloc_fail_disarm();

	fy_document_destroy(fyd2);
	fy_document_destroy(fyd);
}

static void issue_395_scenario(unsigned int nth)
{
	static const char data[] = "ch-mar: true &node3 \n";
	struct fy_document *fyd;
	struct fy_node *fyn;

	fyd = fy_document_create(&(struct fy_parse_cfg){ .flags = FYPCF_QUIET });
	ck_assert_ptr_ne(fyd, NULL);

	fy_alloc_fail_arm(nth);
	fyn = fy_node_create_scalar(fyd, data, sizeof(data) - 1);
	fy_alloc_fail_disarm();

	fy_node_free(fyn);
	fy_document_destroy(fyd);
}

static void issue_396_scenario(unsigned int nth)
{
	static const char yaml[] = "&A [ */**!][,,]\n";
	struct fy_document *fyd;

	fyd = fy_document_build_from_string(
			&(struct fy_parse_cfg){ .flags = FYPCF_QUIET | FYPCF_YPATH_ALIASES },
			yaml, sizeof(yaml) - 1);
	ck_assert_ptr_ne(fyd, NULL);

	fy_alloc_fail_arm(nth);
	fy_document_resolve(fyd);
	fy_alloc_fail_disarm();

	fy_document_destroy(fyd);
}

static void issue_399_scenario(unsigned int nth)
{
	static const char yaml[] = "42\n   \t#\rc[[[''\x0f'''&\x10yyb";
	struct fy_document *fyd;

	fy_alloc_fail_arm(nth);
	fyd = fy_document_build_from_string(
			&(struct fy_parse_cfg){
				.flags = FYPCF_QUIET | FYPCF_PARSE_COMMENTS },
			yaml, sizeof(yaml) - 1);
	fy_alloc_fail_disarm();

	fy_document_destroy(fyd);
}

static void issue_401_scenario(unsigned int nth)
{
	/* a multi line single quoted scalar holding escaped quotes */
	static const char yaml[] = "'''''''''''''''''@    \n    '\n";
	struct fy_document *fyd, *fydx;
	const char *text, *textx;
	size_t len, lenx;

	fydx = fy_document_build_from_string(
			&(struct fy_parse_cfg){ .flags = FYPCF_QUIET },
			yaml, sizeof(yaml) - 1);
	ck_assert_ptr_ne(fydx, NULL);
	textx = fy_node_get_scalar(fy_document_root(fydx), &lenx);
	ck_assert_ptr_ne(textx, NULL);

	fyd = fy_document_build_from_string(
			&(struct fy_parse_cfg){ .flags = FYPCF_QUIET },
			yaml, sizeof(yaml) - 1);
	ck_assert_ptr_ne(fyd, NULL);

	fy_alloc_fail_arm(nth);
	text = fy_node_get_scalar(fy_document_root(fyd), &len);
	fy_alloc_fail_disarm();

	/* either the full text or nothing, never a part of the buffer */
	if (text && len) {
		ck_assert_uint_eq(len, lenx);
		ck_assert_int_eq(memcmp(text, textx, len), 0);
	}

	fy_document_destroy(fyd);
	fy_document_destroy(fydx);
}

static void issue_406_scenario(unsigned int nth)
{
	static const char pathexpr[] = "*((()/)//\xd0/#//\xd0/#_";
	struct fy_path_parse_cfg cfg = {
		.flags = FYPPCF_QUIET | FYPPCF_DISABLE_RECYCLING |
			 FYPPCF_DISABLE_ACCELERATORS,
	};
	struct fy_path_expr *expr;
	struct fy_document *fyd;

	fy_alloc_fail_arm(nth);
	expr = fy_path_expr_build_from_string(&cfg, pathexpr, sizeof(pathexpr) - 1);
	if (expr) {
		fyd = fy_path_expr_to_document(expr);
		fy_document_destroy(fyd);
	}
	fy_path_expr_free(expr);
	fy_alloc_fail_disarm();
}

static void issue_407_scenario(unsigned int nth)
{
	static const char yaml[] = "- [ *%/.///****.select**.select..//////]";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET | FYPCF_RESOLVE_DOCUMENT |
			 FYPCF_DISABLE_ACCELERATORS | FYPCF_PREFER_RECURSIVE |
			 FYPCF_YPATH_ALIASES,
	};
	struct fy_document *fyd;

	fy_alloc_fail_arm(nth);
	fyd = fy_document_build_from_string(&cfg, yaml, sizeof(yaml) - 1);
	fy_alloc_fail_disarm();

	fy_document_destroy(fyd);
}

static void issue_408_scenario(unsigned int nth)
{
	static const char yaml[] = "*%/(select(\"\")";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET | FYPCF_RESOLVE_DOCUMENT |
			 FYPCF_DISABLE_RECYCLING | FYPCF_PREFER_RECURSIVE |
			 FYPCF_YPATH_ALIASES,
	};
	struct fy_document *fyd;

	fy_alloc_fail_arm(nth);
	fyd = fy_document_build_from_string(&cfg, yaml, sizeof(yaml) - 1);
	fy_alloc_fail_disarm();

	fy_document_destroy(fyd);
}

#endif /* HAVE_LINKER_WRAP_MALLOC */

/* Test: gh#357 - a failed ypath operand must not be freed twice. */
START_TEST(fuzz_issue_357_ypath_operand_double_free_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_357_scenario, 256);
#endif
}
END_TEST

/* Test: gh#360 - a tag directive that cannot be generated. */
START_TEST(fuzz_issue_360_tag_directives_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_360_scenario, 256);
#endif
}
END_TEST

/* Test: gh#361 - the document must not be used after it is destroyed. */
START_TEST(fuzz_issue_361_document_create_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_361_scenario, 256);
#endif
}
END_TEST

/* Test: gh#362 - a failed node pair allocation while building. */
START_TEST(fuzz_issue_362_node_pair_alloc_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_362_scenario, 256);
#endif
}
END_TEST

/* Test: gh#363 - a tag suffix of a tag that is shorter than its prefix. */
START_TEST(fuzz_issue_363_tag_suffix_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_363_scenario, 256);
#endif
}
END_TEST

/* Test: gh#364 - a parser setup that fails after the diagnostic. */
START_TEST(fuzz_issue_364_parser_setup_diag_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_364_scenario, 256);
#endif
}
END_TEST

/* Test: gh#365 - a failed token allocation while scanning. */
START_TEST(fuzz_issue_365_scan_token_leak_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_365_scenario, 256);
#endif
}
END_TEST

/* Test: a checkpoint that cannot be taken reports it instead of aborting. */
START_TEST(fuzz_checkpoint_alloc_failure)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(checkpoint_alloc_scenario, 256);
#endif
}
END_TEST

/* Test: an anchored collection whose anchor fails to be registered. */
START_TEST(fuzz_anchor_collection_failure)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(anchor_collection_scenario, 1024);
#endif
}
END_TEST

/* Test: gh#379 - a composer that fails to be created. */
START_TEST(fuzz_issue_379_composer_create_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_379_scenario, 256);
#endif
}
END_TEST

/* Test: gh#380 - an input that fails after the reader allocated a buffer. */
START_TEST(fuzz_issue_380_input_done_failure_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_380_scenario, 256);
#endif
}
END_TEST

/* Test: gh#381 - anchors that fail to be registered in a document. */
START_TEST(fuzz_issue_381_docbuilder_anchor_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_381_scenario, 1024);
#endif
}
END_TEST

/* Test: gh#382 - a ypath flow document whose token fails. */
START_TEST(fuzz_issue_382_ypath_flow_document_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_382_scenario, 1024);
#endif
}
END_TEST

/* Test: gh#383 - a bottom comment that fails to be copied. */
START_TEST(fuzz_issue_383_bottom_comment_repro)
{
#if defined(HAVE_LINKER_WRAP_MALLOC) && defined(HAVE_GENERIC)
	alloc_fail_sweep(issue_383_scenario, 1024);
#endif
}
END_TEST

/* Test: gh#384 - an event whose token fails to be allocated. */
START_TEST(fuzz_issue_384_event_token_failure_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_384_scenario, 1024);
#endif
}
END_TEST

/*
 * Count the diagnostics of a parse. A build that keeps the assertions
 * checks the size hint of every atom and corrects it, and only warns
 * when it was wrong, so the count is the signal.
 */
static void count_diag_output(struct fy_diag *diag FY_UNUSED, void *user,
			      const char *buf FY_UNUSED, size_t len FY_UNUSED)
{
	(*(unsigned int *)user)++;
}

/*
 * Build a document and check the text of the root scalar. The formatter
 * ends the text after the real content, and a build that keeps the
 * assertions warns about a wrong size hint, so both show a bad hint.
 */
static void check_root_scalar_text(enum fy_parse_cfg_flags flags,
				   const char *yaml, size_t yaml_len,
				   const char *expected, size_t expected_len)
{
	unsigned int messages = 0;
	struct fy_diag_cfg dcfg;
	struct fy_parse_cfg cfg;
	struct fy_diag *diag;
	struct fy_document *fyd;
	struct fy_token *fyt;
	const char *text, *text0;
	size_t len;

	fy_diag_cfg_default(&dcfg);
	dcfg.fp = NULL;
	dcfg.output_fn = count_diag_output;
	dcfg.user = &messages;
	dcfg.level = FYET_WARNING;
	diag = fy_diag_create(&dcfg);
	ck_assert_ptr_ne(diag, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = flags;
	cfg.diag = diag;

	fyd = fy_document_build_from_string(&cfg, yaml, yaml_len);
	ck_assert_ptr_ne(fyd, NULL);

	fyt = fy_node_get_scalar_token(fy_document_root(fyd));
	ck_assert_ptr_ne(fyt, NULL);

	/* the terminated text is formatted in a buffer of the hint size */
	text0 = fy_token_get_text0(fyt);
	ck_assert_ptr_ne(text0, NULL);
	text = fy_token_get_text(fyt, &len);
	ck_assert_ptr_ne(text, NULL);
	ck_assert_uint_eq(strlen(text0), len);
	ck_assert_int_eq(memcmp(text0, text, len), 0);
	if (expected) {
		ck_assert_uint_eq(len, expected_len);
		ck_assert_int_eq(memcmp(text, expected, len), 0);
	}
	ck_assert_uint_eq(messages, 0);

	fy_document_destroy(fyd);
	fy_diag_unref(diag);
}

/* Test: gh#385 - a kept literal block scalar that starts at column zero. */
START_TEST(fuzz_issue_385_literal_keep_text_repro)
{
	static const char yaml[] = "--- |+\n=ab\n \n  \x0b...\n";

	check_root_scalar_text(0, yaml, sizeof(yaml) - 1, NULL, 0);
}
END_TEST

/* Test: gh#386 - an alias whose name has a 4-octet character. */
START_TEST(fuzz_issue_386_alias_utf8_text_repro)
{
	static const char yaml[] = " *Ox.\xf4\x8f\xbf\xbf";
	static const char expected[] = "Ox.\xf4\x8f\xbf\xbf";

	check_root_scalar_text(FYPCF_PREFER_RECURSIVE, yaml, sizeof(yaml) - 1,
			       expected, sizeof(expected) - 1);
}
END_TEST

/* Test: gh#387 - a YAML 1.1 double-quoted scalar that ends with U+2028. */
START_TEST(fuzz_issue_387_quoted_ls_text_repro)
{
	static const char *yamls[] = {
		"\"\xe2\x80\xa8\"",
		"\"a\xe2\x80\xa8\"",
		"\"a\xe2\x80\xa9\"",
		"'a\xe2\x80\xa8'",
		"\"\xe2\x80\xa8\xe2\x80\xa8\"",
		"\"a\xe2\x80\xa8\n\"",
	};
	unsigned int i;

	for (i = 0; i < sizeof(yamls) / sizeof(yamls[0]); i++)
		check_root_scalar_text(FYPCF_DEFAULT_VERSION_1_1, yamls[i],
				       strlen(yamls[i]), NULL, 0);
}
END_TEST

/* Test: gh#388 - a rollback while simple keys are pending. */
START_TEST(fuzz_issue_388_rollback_simple_keys_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_388_scenario, 1024);
#endif
}
END_TEST

/* Test: gh#389 - a merge key document that fails to be built. */
START_TEST(fuzz_issue_389_merge_key_document_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_389_scenario, 2048);
#endif
}
END_TEST

/* Test: gh#390 - a merge key event that fails to be collected. */
START_TEST(fuzz_issue_390_resolve_collect_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_390_scenario, 2048);
#endif
}
END_TEST

/* Test: gh#391 - a walk result number whose input fails. */
START_TEST(fuzz_issue_391_walk_number_input_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_391_scenario, 1024);
#endif
}
END_TEST

/* Test: gh#392 - a merge key document that fails to be iterated. */
START_TEST(fuzz_issue_392_merge_key_iterator_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_392_scenario, 2048);
#endif
}
END_TEST

/* Test: gh#393 - a duplicate key that the builder rejects is freed once. */
START_TEST(fuzz_issue_393_duplicate_key_free_repro)
{
	static const char meta[] = "  ? |\r  ? |\r ";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET,
	};

	fy_document_destroy(fy_flow_document_build_from_string(NULL, meta, FY_NT, NULL));
	ck_assert_ptr_eq(fy_document_build_from_string(&cfg, "a: 1\nb: 2\na: 3\n", FY_NT), NULL);
	ck_assert_ptr_eq(fy_document_build_from_string(&cfg, "{a: 1, b: {c: 1, c: 2}}", FY_NT), NULL);
}
END_TEST

/* Test: a duplicate key that is allowed, in a mapping with a lookup table. */
START_TEST(fuzz_duplicate_keys_allowed_accelerated)
{
	static const char yaml[] = "a: 1\nb: 2\na: 3\n";
	static const enum fy_parse_cfg_flags flags[] = {
		FYPCF_QUIET | FYPCF_ALLOW_DUPLICATE_KEYS,
		FYPCF_QUIET | FYPCF_ALLOW_DUPLICATE_KEYS | FYPCF_PREFER_RECURSIVE,
	};
	struct fy_document *fyd;
	unsigned int i;

	for (i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
		fyd = fy_document_build_from_string(
				&(struct fy_parse_cfg){ .flags = flags[i] }, yaml, FY_NT);
		ck_assert_ptr_ne(fyd, NULL);
		ck_assert_int_eq(fy_node_mapping_item_count(fy_document_root(fyd)), 3);
		/* the first pair is found, as without a lookup table */
		ck_assert_str_eq(fy_node_get_scalar0(fy_node_by_path(fy_document_root(fyd),
						"/a", FY_NT, FYNWF_DONT_FOLLOW)), "1");
		fy_document_destroy(fyd);
	}
}
END_TEST

/* Test: gh#395 - a scalar node whose token fails to be created. */
START_TEST(fuzz_issue_395_create_scalar_input_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_395_scenario, 256);
#endif
}
END_TEST

/* Test: gh#396 - a ypath alias whose results fail to be flattened. */
START_TEST(fuzz_issue_396_ypath_flatten_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_396_scenario, 1024);
#endif
}
END_TEST

/* Test: gh#399 - a comment handle that fails to be allocated. */
START_TEST(fuzz_issue_399_comment_handle_loop_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_399_scenario, 1024);
#endif
}
END_TEST

/* Test: gh#400 - path aliases that loop through a merge key. */
START_TEST(fuzz_issue_400_merge_key_alias_loop_repro)
{
	static const char yaml[] =
		"<<:\n- -:ln \x7f </-0:-8\n\n\n-\n */-0//8*/-0\n-\n */-0:8\n\n"
		"\n-\n */-0/-0\n-\n */-0:8\n\n\n\n\n-\n *nul\n-\n */-<:\n-  *OBa<<:\n"
		"-  */!e!suffi%Ba";
	static const char yaml2[] =
		"a: &a { x: 1 }\n"
		"b: &b { <<: *a, y: 2 }\n"
		"c: { <<: [ *b ], z: 3 }\n";
	struct fy_document *fyd;
	struct fy_node *fyn;

	fyd = fy_document_build_from_string(
			&(struct fy_parse_cfg){ .flags = FYPCF_QUIET },
			yaml, sizeof(yaml) - 1);
	ck_assert_ptr_ne(fyd, NULL);

#ifdef HAVE_LINKER_WRAP_MALLOC
	/* count the allocations; the loop made millions of them */
	fy_alloc_fail_arm(UINT_MAX);
#endif
	ck_assert_int_ne(fy_document_resolve(fyd), 0);
#ifdef HAVE_LINKER_WRAP_MALLOC
	fy_alloc_fail_disarm();
	ck_assert_uint_lt(fy_alloc_fail_seen(), 100000);
#endif

	fy_document_destroy(fyd);

	/* merge keys that do not loop still find their keys */
	fyd = fy_document_build_from_string(NULL, yaml2, sizeof(yaml2) - 1);
	ck_assert_ptr_ne(fyd, NULL);

	fyn = fy_node_by_path(fy_document_root(fyd), "/b/x", FY_NT,
			      FYNWF_FOLLOW | FYNWF_PTR_YAML);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "1");

	fyn = fy_node_by_path(fy_document_root(fyd), "/c/y", FY_NT,
			      FYNWF_FOLLOW | FYNWF_PTR_YAML);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "2");

	fy_document_destroy(fyd);
}
END_TEST

/*
 * Parse events and check that the size hint of each scalar is the length
 * of its text. The text uses the real length, so only the hint shows the
 * error.
 */
static void check_scalar_hints(enum fy_parse_cfg_flags flags,
			       const char *yaml, size_t yaml_len)
{
	struct fy_parse_cfg cfg;
	struct fy_parser *fyp;
	struct fy_event *fye;
	struct fy_token *fyt;
	unsigned int count = 0;
	size_t hint, len;
	const char *text;

	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = flags | FYPCF_QUIET;

	fyp = fy_parser_create(&cfg);
	ck_assert_ptr_ne(fyp, NULL);
	ck_assert_int_eq(fy_parser_set_string(fyp, yaml, yaml_len), 0);

	while ((fye = fy_parser_parse(fyp)) != NULL) {
		if (fye->type == FYET_SCALAR) {
			fyt = fye->scalar.value;
			hint = fy_token_get_text_length(fyt);
			text = fy_token_get_text(fyt, &len);
			ck_assert_ptr_ne(text, NULL);
			ck_assert_uint_eq(hint, len);
			count++;
		}
		fy_parser_event_free(fyp, fye);
	}
	ck_assert_uint_gt(count, 0);

	fy_parser_destroy(fyp);
}

#define CHECK_SCALAR_HINTS(_flags, _yaml) \
	check_scalar_hints((_flags), (_yaml), sizeof(_yaml) - 1)

/* Test: gh#401 - the scalar text when the atom iterator fails. */
START_TEST(fuzz_issue_401_prepare_text_alloc_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_401_scenario, 256);
#endif
}
END_TEST

/* Test: gh#402 - a block scalar with LS or PS in its empty lines. */
START_TEST(fuzz_issue_402_block_ls_ps_hint_repro)
{
	CHECK_SCALAR_HINTS(FYPCF_DEFAULT_VERSION_1_1,
		">\n \t>\n \t\n\xe2\x80\xa9 \n\xe2\x80\xa9 \x7f" "ete");
	CHECK_SCALAR_HINTS(FYPCF_DEFAULT_VERSION_1_1, ">\n a\n\xe2\x80\xa9 b\n");
	CHECK_SCALAR_HINTS(FYPCF_DEFAULT_VERSION_1_1, ">\n a\n\xe2\x80\xa8 \n\xe2\x80\xa8 b\n");
	CHECK_SCALAR_HINTS(FYPCF_DEFAULT_VERSION_1_1, "|\n a\n\xe2\x80\xa9 \n\xe2\x80\xa9 b\n");
}
END_TEST

/* Test: gh#403 - empty lines after an escaped line break. */
START_TEST(fuzz_issue_403_escaped_break_blank_lines_repro)
{
	static const char yaml[] = "\"a\\\n\n\nb\"\n";
	static const char expected[] = "a\n\nb";

	CHECK_SCALAR_HINTS(0,
		"\"?\\\n\n\n?\\\n\n\n\n\n#\n#06u\b \n\n#\n#06u\b 789\"\n");
	CHECK_SCALAR_HINTS(0, "\"a\\\n\n\n\nb\\\n\n\nc\"\n");
	check_root_scalar_text(0, yaml, sizeof(yaml) - 1,
			       expected, sizeof(expected) - 1);
}
END_TEST

/* Test: gh#404 - white space content after an empty line, and a NUL. */
START_TEST(fuzz_issue_404_block_tab_line_nul_repro)
{
	static const char yaml[] = "|\n \n  \t\n";
	static const char expected[] = "\n\t\n";
	static const char yaml2[] = ">\n\n  \t\n";
	static const char expected2[] = "\n\t\n";

	CHECK_SCALAR_HINTS(0, "|\n \n  \t\0\x10\0\0");
	CHECK_SCALAR_HINTS(0, ">\n \n  \t\0");
	check_root_scalar_text(0, yaml, sizeof(yaml) - 1,
			       expected, sizeof(expected) - 1);
	check_root_scalar_text(0, yaml2, sizeof(yaml2) - 1,
			       expected2, sizeof(expected2) - 1);
}
END_TEST

/* Test: gh#405 - nested comparisons of recursive descents. */
START_TEST(fuzz_issue_405_ypath_nested_compare_repro)
{
	static const char path[] = "********========nts/0";
	static char doc[2048], tmp[2 * sizeof(doc) + 16];
	struct fy_document *fyd;
	size_t len;
	int i;

	/* a balanced tree of mappings, with "x" at the leaves */
	strcpy(doc, "x");
	for (i = 0; i < 6; i++) {
		snprintf(tmp, sizeof(tmp), "{a: %s, b: %s}", doc, doc);
		len = strlen(tmp);
		ck_assert_uint_lt(len, sizeof(doc));
		memcpy(doc, tmp, len + 1);
	}

	fyd = fy_document_build_from_string(NULL, doc, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

#ifdef HAVE_LINKER_WRAP_MALLOC
	/* count the allocations; the results grew as a product */
	fy_alloc_fail_arm(UINT_MAX);
#endif
	fy_node_by_path(fy_document_root(fyd), path, sizeof(path) - 1,
			FYNWF_PTR_YPATH);
#ifdef HAVE_LINKER_WRAP_MALLOC
	fy_alloc_fail_disarm();
	ck_assert_uint_lt(fy_alloc_fail_seen(), 100000);
#endif

	fy_document_destroy(fyd);
}
END_TEST

/* Test: gh#406 - an expression dump whose append fails. */
START_TEST(fuzz_issue_406_expr_to_node_alloc_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_406_scenario, 1024);
#endif
}
END_TEST

/* Test: gh#407 - a collection method whose selected item fails to clone. */
START_TEST(fuzz_issue_407_collection_method_alloc_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_407_scenario, 2048);
#endif
}
END_TEST

/* Test: gh#408 - a select whose clone fails, with a string argument. */
START_TEST(fuzz_issue_408_select_clone_alloc_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_408_scenario, 1024);
#endif
}
END_TEST

/* Test: gh#409 - ypath aliases that resolve to a node that holds them. */
START_TEST(fuzz_issue_409_ypath_alias_holds_itself_repro)
{
	static const char yaml[] =
		"[a, b, *//**, *//**, *//**, *//**, *//**, *//**, *//**, *//**]";
	static const char yaml2[] = "a: { x: 1 }\nb: */a/x\nc: */a\n";
	struct fy_document *fyd;
	struct fy_node *fyn;

	fyd = fy_document_build_from_string(
			&(struct fy_parse_cfg){
				.flags = FYPCF_QUIET | FYPCF_YPATH_ALIASES },
			yaml, sizeof(yaml) - 1);
	ck_assert_ptr_ne(fyd, NULL);

#ifdef HAVE_LINKER_WRAP_MALLOC
	/* count the allocations; each pass doubled the document */
	fy_alloc_fail_arm(UINT_MAX);
#endif
	ck_assert_int_ne(fy_document_resolve(fyd), 0);
#ifdef HAVE_LINKER_WRAP_MALLOC
	fy_alloc_fail_disarm();
	ck_assert_uint_lt(fy_alloc_fail_seen(), 100000);
#endif

	fy_document_destroy(fyd);

	/* an alias to a node that does not hold it still resolves */
	fyd = fy_document_build_from_string(
			&(struct fy_parse_cfg){
				.flags = FYPCF_QUIET | FYPCF_YPATH_ALIASES },
			yaml2, sizeof(yaml2) - 1);
	ck_assert_ptr_ne(fyd, NULL);
	ck_assert_int_eq(fy_document_resolve(fyd), 0);

	fyn = fy_node_by_path(fy_document_root(fyd), "/b", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "1");

	fyn = fy_node_by_path(fy_document_root(fyd), "/c/x", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_str_eq(fy_node_get_scalar0(fyn), "1");

	fy_document_destroy(fyd);
}
END_TEST

/* Test: gh#397 - a folded scalar at column zero with a more-indented line. */
START_TEST(fuzz_issue_397_folded_column_zero_text_repro)
{
	static const char yaml[] =
		">\n\x13 foo \n \n\x0f \t baz\n\n  bar\n";
	static const char expected[] =
		"\x13 foo \n \n\x0f \t baz\n\n  bar\n";
	static const char yaml2[] = ">\nxfoo\n baz\n";
	static const char expected2[] = "xfoo\n baz\n";

	check_root_scalar_text(0, yaml, sizeof(yaml) - 1,
			       expected, sizeof(expected) - 1);
	check_root_scalar_text(0, yaml2, sizeof(yaml2) - 1,
			       expected2, sizeof(expected2) - 1);
}
END_TEST

/* Test: gh#398 - a line break after an odd run of backslashes. */
START_TEST(fuzz_issue_398_escaped_break_parity_repro)
{
	/* an escaped backslash pair, and a backslash that escapes the break */
	static const char yaml[] = "\":M\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\xe2\x80\xa9" "c\"";
	static const char expected[] = ":M\\\\\\\\\\\\\\c";
	static const char yaml2[] = "\"a\\\\\\\nb\"";
	static const char expected2[] = "a\\b";
	static const char yaml3[] = "\"a\\\\\nb\"";
	static const char expected3[] = "a\\ b";

	check_root_scalar_text(FYPCF_DEFAULT_VERSION_1_1, yaml, sizeof(yaml) - 1,
			       expected, sizeof(expected) - 1);
	check_root_scalar_text(0, yaml2, sizeof(yaml2) - 1,
			       expected2, sizeof(expected2) - 1);
	check_root_scalar_text(0, yaml3, sizeof(yaml3) - 1,
			       expected3, sizeof(expected3) - 1);
}
END_TEST

/* Test: gh#366 - a rollback keeps the input reference of the parser. */
START_TEST(fuzz_issue_366_rollback_input_ref_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_366_scenario, 256);
#endif
}
END_TEST

/* Test: gh#367 - the arguments of a failed ypath method are freed once. */
START_TEST(fuzz_issue_367_ypath_method_args_double_free_repro)
{
	static const unsigned char yaml[] = {
		0x20, 0x65, 0x7f, 0x65, 0x65, 0x65, 0x65, 0x20,
		0x20, 0x3c, 0x42, 0x3a, 0x20, 0x2a, 0x2f, 0x2e,
		0x69, 0x6e, 0x64, 0x65, 0x78, 0x2e, 0x0a, 0x64,
		0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
		0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
		0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
		0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x13, 0x7f,
		0x01, 0x62, 0x64, 0x73, 0x65, 0x65, 0x65, 0x65,
		0x65, 0x65, 0x65, 0x65, 0x65, 0x20, 0x74, 0x65,
		0x73, 0x74, 0x0a, 0x25, 0x59, 0x62, 0x0a, 0x09,
		0x20, 0x5b, 0x44, 0x5b, 0x5b, 0x7c, 0x2d, 0x2d,
		0x0a, 0x23, 0x20, 0x6d, 0x6f, 0x72, 0x65, 0x20,
		0x38, 0x39, 0x2a, 0x62, 0x0a, 0x64, 0x64, 0x64,
		0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
		0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
		0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
		0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
		0x64, 0x64, 0x64, 0x00, 0x00, 0x64, 0x64, 0x64,
		0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
		0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
		0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
		0x20, 0x3c, 0x42, 0x3a, 0x20,
	};
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET | FYPCF_YPATH_ALIASES |
			 FYPCF_DEFAULT_VERSION_1_1,
	};
	struct fy_document *fyd, *fyd2;

	fyd = fy_document_build_from_string(&cfg, (const char *)yaml,
			sizeof(yaml));
	ck_assert_ptr_ne(fyd, NULL);

	fyd2 = fy_document_clone(fyd);
	ck_assert_ptr_ne(fyd2, NULL);
	(void)fy_document_resolve(fyd2);

	fy_document_destroy(fyd2);
	fy_document_destroy(fyd);
}
END_TEST

/* Test: gh#368 - a sequence index that the int range cannot hold. */
START_TEST(fuzz_issue_368_seq_index_overflow_repro)
{
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET | FYPCF_YPATH_ALIASES,
	};
	static const char yaml[] =
		"- &a [x, y, z]\n"
		"- *a/333333333333\n";
	struct fy_document *fyd;

	fyd = fy_document_build_from_string(&cfg, yaml, sizeof(yaml) - 1);
	ck_assert_ptr_ne(fyd, NULL);

	/* the index does not resolve, but it must not overflow either */
	ck_assert_int_ne(fy_document_resolve(fyd), 0);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: gh#369 - the start token of a bare sequence event starts cleared. */
START_TEST(fuzz_issue_369_bare_seq_start_token_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_369_scenario, 256);
#endif
}
END_TEST

/* Test: gh#370 - count the characters of a scalar with an invalid octet. */
START_TEST(fuzz_issue_370_utf8_length_invalid_octet_repro)
{
	/* a lone continuation octet starts no character */
	static const char text[] = { 'a', (char)0x80, 'b' };
	struct fy_document *fyd;
	struct fy_node *fyn;

	fyd = fy_document_create(NULL);
	ck_assert_ptr_ne(fyd, NULL);
	fyn = fy_node_create_scalar_copy(fyd, text, sizeof(text));
	ck_assert_ptr_ne(fyn, NULL);
	fy_document_set_root(fyd, fyn);

	/* it must count the octet as one character, and it must return */
	ck_assert_int_eq(fy_node_get_scalar_utf8_length(fyn), 3);

	fy_document_destroy(fyd);
}
END_TEST

/* Test: gh#371 - a block scalar whose content starts at column zero. */
START_TEST(fuzz_issue_371_block_scalar_column_zero_repro)
{
	static const char yaml[] =
		"--- >\n"
		"\xef\xbf\xbd    ' \xef\xbf\xbd\x04   \n"
		"   '\n";
	static const char expected[] =
		"\xef\xbf\xbd    ' \xef\xbf\xbd\x04   \n"
		"   '\n";
	unsigned int messages = 0;
	struct fy_diag_cfg dcfg;
	struct fy_parse_cfg cfg;
	struct fy_diag *diag;
	struct fy_document *fyd;
	const char *text;
	size_t len;

	fy_diag_cfg_default(&dcfg);
	dcfg.fp = NULL;
	dcfg.output_fn = count_diag_output;
	dcfg.user = &messages;
	dcfg.level = FYET_WARNING;
	diag = fy_diag_create(&dcfg);
	ck_assert_ptr_ne(diag, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.diag = diag;

	fyd = fy_document_build_from_string(&cfg, yaml, sizeof(yaml) - 1);
	ck_assert_ptr_ne(fyd, NULL);

	text = fy_node_get_scalar(fy_document_root(fyd), &len);
	ck_assert_ptr_ne(text, NULL);
	ck_assert_uint_eq(len, sizeof(expected) - 1);
	ck_assert_int_eq(memcmp(text, expected, len), 0);

	/* the size hint of the scalar must have been right */
	ck_assert_uint_eq(messages, 0);

	fy_document_destroy(fyd);
	fy_diag_unref(diag);
}
END_TEST

/* Test: gh#373 - the context stack drops a node that the builder frees. */
START_TEST(fuzz_issue_373_docbuilder_depth_repro)
{
	static char yaml[275];
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET,
	};
	size_t o = 0;

	memset(yaml + o, '{', 47); o += 47;
	memset(yaml + o, '}', 4);  o += 4;
	memcpy(yaml + o, ", ", 2); o += 2;
	memset(yaml + o, '{', 7);  o += 7;
	memset(yaml + o, '}', 32); o += 32;
	memcpy(yaml + o, ", ", 2); o += 2;
	memset(yaml + o, '{', 4);  o += 4;
	memset(yaml + o, '}', 6);  o += 6;
	memcpy(yaml + o, ", ", 2); o += 2;
	memset(yaml + o, '{', 10); o += 10;
	memset(yaml + o, '}', 8);  o += 8;
	memcpy(yaml + o, ", ", 2); o += 2;
	memset(yaml + o, '{', 7);  o += 7;
	memset(yaml + o, '}', 9);  o += 9;
	memcpy(yaml + o, ", ", 2); o += 2;
	memset(yaml + o, '{', 82); o += 82;
	memset(yaml + o, '}', 8);  o += 8;
	memcpy(yaml + o, ", ", 2); o += 2;
	memset(yaml + o, '{', 7);  o += 7;
	memset(yaml + o, '}', 31); o += 31;
	yaml[o++] = '\n';

	/* the depth limit stops it, and nothing is freed twice */
	fy_document_destroy(fy_document_build_from_string(&cfg, yaml, o));
}
END_TEST

/* Test: gh#374 - a DOCUMENT-START event whose document state fails. */
START_TEST(fuzz_issue_374_document_start_state_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_374_scenario, 256);
#endif
}
END_TEST

/* Test: gh#376 - an emitter setup that fails while emitting to a file. */
START_TEST(fuzz_issue_376_emit_setup_failure_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_376_scenario, 256);
#endif
}
END_TEST

/* Test: gh#377 - an anchor that fails to be set is released once. */
START_TEST(fuzz_issue_377_set_anchor_failure_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_377_scenario, 256);
#endif
}
END_TEST

/* Test: gh#378 - an anchor that fails to be registered is undone. */
START_TEST(fuzz_issue_378_register_anchor_failure_repro)
{
#ifdef HAVE_LINKER_WRAP_MALLOC
	alloc_fail_sweep(issue_378_scenario, 256);
#endif
}
END_TEST

/* Test: the YAML 1.1 escape of a multi-octet character. */
START_TEST(fuzz_utf8_multi_octet_escape)
{
	/* a backslash before U+00A0, which is the escape of itself */
	static const char yaml[] = "\"\\\xc2\xa0Z\"";
	static const char expected[] = "\xc2\xa0Z";
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_QUIET | FYPCF_DEFAULT_VERSION_1_1,
	};
	struct fy_document *fyd;
	const char *text;
	size_t len;

	fyd = fy_document_build_from_string(&cfg, yaml, sizeof(yaml) - 1);
	ck_assert_ptr_ne(fyd, NULL);

	text = fy_node_get_scalar(fy_document_root(fyd), &len);
	ck_assert_ptr_ne(text, NULL);
	ck_assert_uint_eq(len, sizeof(expected) - 1);
	ck_assert_int_eq(memcmp(text, expected, len), 0);

	fy_document_destroy(fyd);
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
	fy_check_testcase_add_test(ctc, fuzz_issue_352_ypath_method_args_leak_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_356_ypath_method_args_leak_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_358_ypath_index_range_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_359_token_comment_unlink_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_357_ypath_operand_double_free_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_360_tag_directives_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_361_document_create_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_362_node_pair_alloc_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_363_tag_suffix_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_364_parser_setup_diag_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_365_scan_token_leak_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_366_rollback_input_ref_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_367_ypath_method_args_double_free_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_368_seq_index_overflow_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_369_bare_seq_start_token_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_370_utf8_length_invalid_octet_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_371_block_scalar_column_zero_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_373_docbuilder_depth_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_374_document_start_state_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_376_emit_setup_failure_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_377_set_anchor_failure_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_378_register_anchor_failure_repro);
	fy_check_testcase_add_test(ctc, fuzz_utf8_multi_octet_escape);
	fy_check_testcase_add_test(ctc, fuzz_checkpoint_alloc_failure);
	fy_check_testcase_add_test(ctc, fuzz_issue_379_composer_create_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_380_input_done_failure_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_381_docbuilder_anchor_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_382_ypath_flow_document_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_383_bottom_comment_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_384_event_token_failure_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_385_literal_keep_text_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_386_alias_utf8_text_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_387_quoted_ls_text_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_388_rollback_simple_keys_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_389_merge_key_document_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_390_resolve_collect_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_391_walk_number_input_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_392_merge_key_iterator_repro);
	fy_check_testcase_add_test(ctc, fuzz_anchor_collection_failure);
	fy_check_testcase_add_test(ctc, fuzz_issue_393_duplicate_key_free_repro);
	fy_check_testcase_add_test(ctc, fuzz_duplicate_keys_allowed_accelerated);
	fy_check_testcase_add_test(ctc, fuzz_issue_395_create_scalar_input_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_396_ypath_flatten_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_397_folded_column_zero_text_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_398_escaped_break_parity_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_399_comment_handle_loop_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_400_merge_key_alias_loop_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_401_prepare_text_alloc_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_402_block_ls_ps_hint_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_403_escaped_break_blank_lines_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_404_block_tab_line_nul_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_405_ypath_nested_compare_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_406_expr_to_node_alloc_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_407_collection_method_alloc_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_408_select_clone_alloc_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_409_ypath_alias_holds_itself_repro);
#if defined(__linux__)
	fy_check_testcase_add_test(ctc, fuzz_issue_340_alias_path_end_repro);
#ifdef HAVE_GENERIC
	fy_check_testcase_add_test(ctc, fuzz_issue_344_deep_primitive_dump_repro);
#endif
#endif
#ifdef HAVE_REFLECTION
	fy_check_testcase_add_test(ctc, fuzz_issue_341_dependent_type_cycle_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_342_null_type_name_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_348_enum_int64_max_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_353_anonymous_record_cycle_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_354_anonymous_record_kind_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_355_type_info_iteration_repro);
	fy_check_testcase_add_test(ctc, fuzz_issue_375_integer_scalar_negate_repro);
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
