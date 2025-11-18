/*
 * libfyaml-test-generic.c - libfyaml generics tests
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
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

#include "fy-generic.h"

/* Test: Basic thread pool creation and destruction */
START_TEST(generic_basics)
{
}
END_TEST

TCase *libfyaml_case_generic(void)
{
	TCase *tc;

	tc = tcase_create("generic");

	/* baby steps first */
	tcase_add_test(tc, generic_basics);

	return tc;
}
