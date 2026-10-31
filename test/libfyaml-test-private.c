/*
 * libfyaml-test-private.c - libfyaml private API test harness
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
#include <limits.h>


#ifndef _WIN32
#include <unistd.h>
#include <errno.h>
#endif

#include <libfyaml.h>
#include "fy-parse.h"
#include "fy-doc.h"
#include "fy-utils.h"

#include "fy-check.h"

static const struct fy_parse_cfg default_parse_cfg = {
	.search_path = "",
	.flags = FYPCF_QUIET,
};

START_TEST(parser_setup)
{
	struct fy_parser ctx, *fyp = &ctx;
	const struct fy_parse_cfg *cfg = &default_parse_cfg;
	int rc;

	/* setup */
	rc = fy_parse_setup(fyp, cfg);
	ck_assert_int_eq(rc, 0);

	/* cleanup */
	fy_parse_cleanup(fyp);
}
END_TEST

START_TEST(scan_simple)
{
	struct fy_parser ctx, *fyp = &ctx;
	const struct fy_parse_cfg *cfg = &default_parse_cfg;
	static const struct fy_input_cfg fyic = {
		.type		= fyit_memory,
		.memory.data	= "42",
		.memory.size	= 2,
	};
	struct fy_token *fyt;
	int rc;

	/* setup */
	rc = fy_parse_setup(fyp, cfg);
	ck_assert_int_eq(rc, 0);

	/* add the input */
	rc = fy_parse_input_append(fyp, &fyic);
	ck_assert_int_eq(rc, 0);

	/* STREAM_START */
	fyt = fy_scan(fyp);
	ck_assert_ptr_ne(fyt, NULL);
	ck_assert(fyt->type == FYTT_STREAM_START);
	fy_token_unref(fyt);

	/* SCALAR */
	fyt = fy_scan(fyp);
	ck_assert_ptr_ne(fyt, NULL);
	ck_assert(fyt->type == FYTT_SCALAR);
	ck_assert(fyt->scalar.style == FYSS_PLAIN);
	ck_assert_str_eq(fy_token_get_text0(fyt), "42");
	fy_token_unref(fyt);

	/* STREAM_END */
	fyt = fy_scan(fyp);
	ck_assert_ptr_ne(fyt, NULL);
	ck_assert(fyt->type == FYTT_STREAM_END);
	fy_token_unref(fyt);

	/* EOF */
	fyt = fy_scan(fyp);
	ck_assert_ptr_eq(fyt, NULL);

	/* cleanup */
	fy_parse_cleanup(fyp);
}
END_TEST

START_TEST(parse_simple)
{
	struct fy_parser ctx, *fyp = &ctx;
	const struct fy_parse_cfg *cfg = &default_parse_cfg;
	static const struct fy_input_cfg fyic = {
		.type		= fyit_memory,
		.memory.data	= "42",
		.memory.size	= 2,
	};
	struct fy_eventp *fyep;
	int rc;

	/* setup */
	rc = fy_parse_setup(fyp, cfg);
	ck_assert_int_eq(rc, 0);

	/* add the input */
	rc = fy_parse_input_append(fyp, &fyic);
	ck_assert_int_eq(rc, 0);

	/* STREAM_START */
	fyep = fy_parse_private(fyp);
	ck_assert_ptr_ne(fyep, NULL);
	ck_assert(fyep->e.type == FYET_STREAM_START);
	fy_parse_eventp_recycle(fyp, fyep);

	/* DOCUMENT_START */
	fyep = fy_parse_private(fyp);
	ck_assert_ptr_ne(fyep, NULL);
	ck_assert(fyep->e.type == FYET_DOCUMENT_START);
	fy_parse_eventp_recycle(fyp, fyep);

	/* SCALAR */
	fyep = fy_parse_private(fyp);
	ck_assert_ptr_ne(fyep, NULL);
	ck_assert(fyep->e.type == FYET_SCALAR);
	ck_assert_str_eq(fy_token_get_text0(fyep->e.scalar.value), "42");
	fy_parse_eventp_recycle(fyp, fyep);

	/* DOCUMENT_END */
	fyep = fy_parse_private(fyp);
	ck_assert_ptr_ne(fyep, NULL);
	ck_assert(fyep->e.type == FYET_DOCUMENT_END);
	fy_parse_eventp_recycle(fyp, fyep);

	/* STREAM_END */
	fyep = fy_parse_private(fyp);
	ck_assert_ptr_ne(fyep, NULL);
	ck_assert(fyep->e.type == FYET_STREAM_END);
	fy_parse_eventp_recycle(fyp, fyep);

	/* EOF */
	fyep = fy_parse_private(fyp);
	ck_assert_ptr_eq(fyep, NULL);

	/* cleanup */
	fy_parse_cleanup(fyp);
}
END_TEST

/* Test: gh#394 - the document builder and the API create accelerated documents. */
START_TEST(document_accelerated)
{
	static const char yaml[] = "a: 1\nb: { c: 2 }\n";
	struct fy_parse_cfg cfg = { .flags = FYPCF_QUIET };
	struct fy_parse_cfg cfg_off = { .flags = FYPCF_QUIET | FYPCF_DISABLE_ACCELERATORS };
	struct fy_document *fyd;
	struct fy_node *fyn;

	/* built through the document builder */
	fyd = fy_document_build_from_string(&cfg, yaml, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);
	ck_assert(fy_document_is_accelerated(fyd));
	ck_assert_ptr_ne(fy_document_root(fyd)->xl, NULL);
	fyn = fy_node_by_path(fy_document_root(fyd), "/b", FY_NT, FYNWF_DONT_FOLLOW);
	ck_assert_ptr_ne(fyn, NULL);
	ck_assert_ptr_ne(fyn->xl, NULL);
	ck_assert_ptr_eq(fy_node_mapping_lookup_by_string(fyn, "c", FY_NT),
			 fy_node_by_path(fyn, "/c", FY_NT, FYNWF_DONT_FOLLOW));
	fy_document_destroy(fyd);

	/* created empty */
	fyd = fy_document_create(&cfg);
	ck_assert_ptr_ne(fyd, NULL);
	ck_assert(fy_document_is_accelerated(fyd));
	fy_document_destroy(fyd);

	/* unless disabled */
	fyd = fy_document_build_from_string(&cfg_off, yaml, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);
	ck_assert(!fy_document_is_accelerated(fyd));
	ck_assert_ptr_eq(fy_document_root(fyd)->xl, NULL);
	fy_document_destroy(fyd);
}
END_TEST

#ifndef _WIN32
static void write_file(const char *path, const char *content)
{
	FILE *fp;

	fp = fopen(path, "w");
	ck_assert_ptr_ne(fp, NULL);
	ck_assert_int_ge(fputs(content, fp), 0);
	ck_assert_int_eq(fclose(fp), 0);
}

static bool file_has(const char *path, const char *content)
{
	char buf[64];
	FILE *fp;
	size_t n;

	fp = fopen(path, "r");
	if (!fp)
		return false;
	n = fread(buf, 1, sizeof(buf) - 1, fp);
	fclose(fp);
	buf[n] = '\0';
	return !strcmp(buf, content);
}

/* a no-replace rename must never overwrite (musl has no RENAME_NOREPLACE) */
START_TEST(rename_no_replace)
{
	char dir[] = "/tmp/fy-rename-XXXXXX";
	char a[64], b[64];

	ck_assert_ptr_ne(mkdtemp(dir), NULL);
	snprintf(a, sizeof(a), "%s/a", dir);
	snprintf(b, sizeof(b), "%s/b", dir);

	write_file(a, "new");
	write_file(b, "old");

	/* the target exists: refused, and both files are untouched */
	ck_assert_int_eq(fy_rename(a, b, true), -1);
	ck_assert(file_has(a, "new"));
	ck_assert(file_has(b, "old"));

	/* plain rename replaces */
	ck_assert_int_eq(fy_rename(a, b, false), 0);
	ck_assert(file_has(b, "new"));
	ck_assert_int_ne(access(a, F_OK), 0);

	/* no-replace onto a missing target just renames */
	ck_assert_int_eq(fy_rename(b, a, true), 0);
	ck_assert(file_has(a, "new"));
	ck_assert_int_ne(access(b, F_OK), 0);

	unlink(a);
	rmdir(dir);
}
END_TEST
#endif

void libfyaml_case_private(struct fy_check_suite *cs)
{
	struct fy_check_testcase *ctc;

	ctc = fy_check_suite_add_test_case(cs, "private");

	fy_check_testcase_add_test(ctc, parser_setup);
	fy_check_testcase_add_test(ctc, scan_simple);
	fy_check_testcase_add_test(ctc, parse_simple);
	fy_check_testcase_add_test(ctc, document_accelerated);
#ifndef _WIN32
	fy_check_testcase_add_test(ctc, rename_no_replace);
#endif
}
