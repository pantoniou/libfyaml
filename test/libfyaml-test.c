/*
 * libfyaml-test.c - C API testing harness for libyaml
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include <check.h>

#include "fy-valgrind.h"

#define QUIET_DEFAULT			false

static void display_usage(FILE *fp, char *progname)
{
	fprintf(fp, "Usage: %s [options] [files]\n", progname);
	fprintf(fp, "\nOptions:\n\n");
	fprintf(fp, "\t--quiet, -q              : Quiet operation, do not "
						"output messages (default %s)\n",
						QUIET_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--help, -h               : Display  help message\n");
	fprintf(fp, "\ne.g. %s\n", progname);
}

#if defined(HAVE_STATIC) && HAVE_STATIC
extern TCase *libfyaml_case_private(void);
extern TCase *libfyaml_case_private_id(void);
#endif
extern TCase *libfyaml_case_core(void);
extern TCase *libfyaml_case_meta(void);
extern TCase *libfyaml_case_emit(void);
extern TCase *libfyaml_case_allocator(void);
extern TCase *libfyaml_case_parser(void);
extern TCase *libfyaml_case_thread(void);

Suite *libfyaml_suite(void)
{
	Suite *s;

	s = suite_create("libfyaml");

#if defined(HAVE_STATIC) && HAVE_STATIC
	suite_add_tcase(s, libfyaml_case_private());
	suite_add_tcase(s, libfyaml_case_private_id());
#endif
	suite_add_tcase(s, libfyaml_case_core());
	suite_add_tcase(s, libfyaml_case_meta());
	suite_add_tcase(s, libfyaml_case_emit());
	suite_add_tcase(s, libfyaml_case_allocator());
	suite_add_tcase(s, libfyaml_case_parser());
	suite_add_tcase(s, libfyaml_case_thread());

	return s;
}

int main(int argc, char *argv[])
{
	int exitcode = EXIT_FAILURE;
	bool quiet = QUIET_DEFAULT;
	int i, number_failed;
	Suite *s;
	SRunner *sr;

	fy_valgrind_check(&argc, &argv);

	/* don't use getopt - it's not there in windows */
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-q") || !strcmp(argv[1], "--quiet")) {
			quiet = true;
		} else if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			display_usage(stdout, argv[0]);
			return EXIT_SUCCESS;
		} else {
			display_usage(stderr, argv[0]);
			return EXIT_FAILURE;
		}
	}

	s = libfyaml_suite();
	sr = srunner_create(s);
	srunner_set_tap(sr, "-");
	srunner_run_all(sr, quiet ? CK_SILENT : CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	exitcode = !number_failed ? EXIT_SUCCESS : EXIT_FAILURE;

	return exitcode;
}
