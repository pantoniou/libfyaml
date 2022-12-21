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
#include <getopt.h>
#include <unistd.h>
#include <limits.h>

#include <check.h>

#include <libfyaml.h>
#include "fy-parse.h"

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

struct fy_test_struct_foo {
	int foo;
};

struct fy_test_struct_bar {
	int frooz;
};

struct fy_test_struct_baz {
	int one;
	struct fy_test_struct_bar two;
	struct fy_test_struct_foo three;
};

START_TEST(container_of)
{
	struct fy_test_struct_baz ftsbaz;
	struct fy_test_struct_bar *ftsbar;
	struct fy_test_struct_baz *ftsbazp;
	ftsbar = &ftsbaz.two;
	ftsbazp = fy_container_of(ftsbar, struct fy_test_struct_baz, two);
	ck_assert_ptr_eq(&ftsbaz, ftsbazp);
}
END_TEST

START_TEST(list)
{
	struct fy_list_head head, other_head, one, two, three, four, five, six;
	bool ret;

	/* add head */
	fy_list_init_head(&head);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == true);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.prev, &head);
	ck_assert_ptr_eq(head.next, &head);

	fy_list_add_head(&one, &head);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == true);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.next, &one);
	ck_assert_ptr_eq(head.next->next, &head);
	ck_assert_ptr_eq(head.prev, &one);
	ck_assert_ptr_eq(head.prev->prev, &head);

	fy_list_add_head(&two, &head);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == false);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.next, &two);
	ck_assert_ptr_eq(head.next->next, &one);
	ck_assert_ptr_eq(head.next->next->next, &head);
	ck_assert_ptr_eq(head.prev, &one);
	ck_assert_ptr_eq(head.prev->prev, &two);
	ck_assert_ptr_eq(head.prev->prev->prev, &head);

	fy_list_add_head(&three, &head);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == false);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.next, &three);
	ck_assert_ptr_eq(head.next->next, &two);
	ck_assert_ptr_eq(head.next->next->next, &one);
	ck_assert_ptr_eq(head.next->next->next->next, &head);
	ck_assert_ptr_eq(head.prev, &one);
	ck_assert_ptr_eq(head.prev->prev, &two);
	ck_assert_ptr_eq(head.prev->prev->prev, &three);
	ck_assert_ptr_eq(head.prev->prev->prev->prev, &head);

	/* add tail */
	fy_list_init_head(&head);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == true);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.prev, &head);
	ck_assert_ptr_eq(head.next, &head);

	fy_list_add_tail(&one, &head);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == true);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.next, &one);
	ck_assert_ptr_eq(head.next->next, &head);
	ck_assert_ptr_eq(head.prev, &one);
	ck_assert_ptr_eq(head.prev->prev, &head);

	fy_list_add_tail(&two, &head);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == false);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.next, &one);
	ck_assert_ptr_eq(head.next->next, &two);
	ck_assert_ptr_eq(head.next->next->next, &head);
	ck_assert_ptr_eq(head.prev, &two);
	ck_assert_ptr_eq(head.prev->prev, &one);
	ck_assert_ptr_eq(head.prev->prev->prev, &head);

	fy_list_add_tail(&three, &head);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == false);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.next, &one);
	ck_assert_ptr_eq(head.next->next, &two);
	ck_assert_ptr_eq(head.next->next->next, &three);
	ck_assert_ptr_eq(head.next->next->next->next, &head);
	ck_assert_ptr_eq(head.prev, &three);
	ck_assert_ptr_eq(head.prev->prev, &two);
	ck_assert_ptr_eq(head.prev->prev->prev, &one);
	ck_assert_ptr_eq(head.prev->prev->prev->prev, &head);

	/* delete */
	fy_list_init_head(&head);
	fy_list_add_head(&one, &head);
	fy_list_del(&one);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == true);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.next, &head);
	ck_assert_ptr_eq(head.prev, &head);

	fy_list_init_head(&head);
	fy_list_add_head(&one, &head);
	fy_list_add_head(&two, &head);
	fy_list_del(&one);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == true);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.next, &two);
	ck_assert_ptr_eq(head.next->next, &head);
	ck_assert_ptr_eq(head.prev, &two);
	ck_assert_ptr_eq(head.prev->prev, &head);

	fy_list_init_head(&head);
	fy_list_add_head(&one, &head);
	fy_list_add_head(&two, &head);
	fy_list_del(&two);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == true);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.next, &one);
	ck_assert_ptr_eq(head.next->next, &head);
	ck_assert_ptr_eq(head.prev, &one);
	ck_assert_ptr_eq(head.prev->prev, &head);

	fy_list_init_head(&head);
	fy_list_add_head(&one, &head);
	fy_list_add_head(&two, &head);
	fy_list_add_head(&three, &head);
	fy_list_del(&two);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == false);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.next, &three);
	ck_assert_ptr_eq(head.next->next, &one);
	ck_assert_ptr_eq(head.next->next->next, &head);
	ck_assert_ptr_eq(head.prev, &one);
	ck_assert_ptr_eq(head.prev->prev, &three);
	ck_assert_ptr_eq(head.prev->prev->prev, &head);

	/* splice */
	fy_list_init_head(&head);
	fy_list_init_head(&other_head);
	fy_list_splice(&other_head, &one);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == false);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == true);
	ck_assert_ptr_eq(head.next, &head);
	ck_assert_ptr_eq(head.prev, &head);

	fy_list_init_head(&head);
	fy_list_add_head(&one, &head);
	fy_list_init_head(&other_head);
	fy_list_splice(&other_head, &one);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == true);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.next, &one);
	ck_assert_ptr_eq(head.next->next, &head);
	ck_assert_ptr_eq(head.prev, &one);
	ck_assert_ptr_eq(head.prev->prev, &head);

	fy_list_init_head(&head);
	fy_list_init_head(&other_head);
	fy_list_add_head(&four, &head);
	fy_list_splice(&other_head, &one);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == true);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.next, &four);
	ck_assert_ptr_eq(head.next->next, &head);
	ck_assert_ptr_eq(head.prev, &four);
	ck_assert_ptr_eq(head.prev->prev, &head);

	fy_list_init_head(&head);
	fy_list_add_head(&one, &head);
	fy_list_add_head(&two, &head);
	fy_list_init_head(&other_head);
	fy_list_add_head(&four, &other_head);
	fy_list_add_head(&five, &other_head);
	fy_list_splice(&other_head, &two);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == false);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.next, &two);
	ck_assert_ptr_eq(head.next->next, &five);
	ck_assert_ptr_eq(head.next->next->next, &four);
	ck_assert_ptr_eq(head.next->next->next->next, &one);
	ck_assert_ptr_eq(head.next->next->next->next->next, &head);
	ck_assert_ptr_eq(head.prev, &one);
	ck_assert_ptr_eq(head.prev->prev, &four);
	ck_assert_ptr_eq(head.prev->prev->prev, &five);
	ck_assert_ptr_eq(head.prev->prev->prev->prev, &two);
	ck_assert_ptr_eq(head.prev->prev->prev->prev->prev, &head);

	fy_list_init_head(&head);
	fy_list_add_head(&one, &head);
	fy_list_add_head(&two, &head);
	fy_list_add_head(&three, &head);
	fy_list_init_head(&other_head);
	fy_list_add_head(&four, &other_head);
	fy_list_add_head(&five, &other_head);
	fy_list_add_head(&six, &other_head);
	fy_list_splice(&other_head, &two);
	ret = fy_list_is_singular(&head);
	ck_assert(ret == false);
	ret = fy_list_is_empty(&head);
	ck_assert(ret == false);
	ck_assert_ptr_eq(head.next, &three);
	ck_assert_ptr_eq(head.next->next, &two);
	ck_assert_ptr_eq(head.next->next->next, &six);
	ck_assert_ptr_eq(head.next->next->next->next, &five);
	ck_assert_ptr_eq(head.next->next->next->next->next, &four);
	ck_assert_ptr_eq(head.next->next->next->next->next->next, &one);
	ck_assert_ptr_eq(head.next->next->next->next->next->next->next, &head);
	ck_assert_ptr_eq(head.prev, &one);
	ck_assert_ptr_eq(head.prev->prev, &four);
	ck_assert_ptr_eq(head.prev->prev->prev, &five);
	ck_assert_ptr_eq(head.prev->prev->prev->prev, &six);
	ck_assert_ptr_eq(head.prev->prev->prev->prev->prev, &two);
	ck_assert_ptr_eq(head.prev->prev->prev->prev->prev->prev, &three);
	ck_assert_ptr_eq(head.prev->prev->prev->prev->prev->prev->prev, &head);
}
END_TEST

TCase *libfyaml_case_private(void)
{
	TCase *tc;

	tc = tcase_create("private");

	tcase_add_test(tc, parser_setup);
	tcase_add_test(tc, scan_simple);
	tcase_add_test(tc, parse_simple);

	tcase_add_test(tc, container_of);
	tcase_add_test(tc, list);

	return tc;
}
