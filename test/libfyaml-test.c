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
#include "fy-check.h"

#define QUIET_DEFAULT			false

static void display_usage(FILE *fp, char *progname)
{
	fprintf(fp, "Usage: %s [options] [files]\n", progname);
	fprintf(fp, "\nOptions:\n\n");
	fprintf(fp, "\t--quiet, -q              : Quiet operation, do not "
						"output messages (default %s)\n",
						QUIET_DEFAULT ? "true" : "false");
	fprintf(fp, "\t--list, -l               : List all tests\n");
	fprintf(fp, "\t--full-list              : List all suites/testcases/tests\n");
	fprintf(fp, "\t--help, -h               : Display  help message\n");
	fprintf(fp, "\ne.g. %s\n", progname);
}

// #if defined(HAVE_STATIC) && HAVE_STATIC
void libfyaml_case_private(struct fy_check_suite *cs);
void libfyaml_case_private_id(struct fy_check_suite *cs);
// #endif
void libfyaml_case_core(struct fy_check_suite *cs);
void libfyaml_case_meta(struct fy_check_suite *cs);
void libfyaml_case_emit(struct fy_check_suite *cs);
void libfyaml_case_emit_bugs(struct fy_check_suite *cs);

void libfyaml_case_allocator(struct fy_check_suite *cs);

void libfyaml_case_parser(struct fy_check_suite *cs);
void libfyaml_case_thread(struct fy_check_suite *cs);
void libfyaml_case_fuzzing(struct fy_check_suite *cs);

struct fy_check_suite *libfyaml_suite(int argc, char **argv)
{
	struct fy_check_suite *cs = fy_check_suite_create("libfyaml", argc, argv);

// #if defined(HAVE_STATIC) && HAVE_STATIC
	libfyaml_case_private(cs);
	libfyaml_case_private_id(cs);
// #endif
	libfyaml_case_core(cs);
	libfyaml_case_meta(cs);
	libfyaml_case_emit(cs);
	libfyaml_case_emit_bugs(cs);
	libfyaml_case_allocator(cs);
	libfyaml_case_parser(cs);
	libfyaml_case_thread(cs);
	libfyaml_case_fuzzing(cs);

	return cs;
}

static void
list_tests(struct fy_check_suite *cs, bool tests_only)
{
	struct fy_check_testcase *ctc;
	struct fy_check_test *ct;

	if (!tests_only)
		printf("suite: %s\n", cs->name);
	for (ctc = fy_check_testcase_list_head(&cs->testcases); ctc;
	     ctc = fy_check_testcase_next(&cs->testcases, ctc)) {
		if (!tests_only)
			printf("+ testcase: %s\n", ctc->name);
		for (ct = fy_check_test_list_head(&ctc->tests); ct;
		     ct = fy_check_test_next(&ctc->tests, ct)) {
			if (!tests_only)
				printf("  + test: %s\n", ct->name);
			else
				printf("%s\n", ct->name);
		}
	}
}

int main(int argc, char *argv[])
{
	int exitcode = EXIT_FAILURE;
	bool quiet = QUIET_DEFAULT;
	bool do_list = false;
	bool tests_only = true;
	int i, start_of_tests, number_failed;
	struct fy_check_runner *cr;
	SRunner *sr;

	fy_valgrind_check(&argc, &argv);

	/* don't use getopt - it's not there in windows */
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-q") || !strcmp(argv[1], "--quiet")) {
			quiet = true;
		} else if (!strcmp(argv[i], "-l") || !strcmp(argv[1], "--list")) {
			do_list = true;
			tests_only = true;
		} else if (!strcmp(argv[1], "--full-list")) {
			do_list = true;
			tests_only = false;
		} else if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			display_usage(stdout, argv[0]);
			return EXIT_SUCCESS;
		} else if (argv[i][0] == '-') {
			display_usage(stderr, argv[0]);
			return EXIT_FAILURE;
		} else
			break;
	}
	start_of_tests = i;

	cr = fy_check_runner_create(libfyaml_suite(argc - start_of_tests, argv + start_of_tests));

	number_failed = 0;

	if (!do_list) {
		sr = fy_check_runner_get_SRunner(cr);
		srunner_set_tap(sr, "-");
		srunner_run_all(sr, quiet ? CK_SILENT : CK_NORMAL);
		number_failed += srunner_ntests_failed(sr);
	} else if (do_list) {
		list_tests(cr->suite, tests_only);
	}

	fy_check_runner_destroy(cr);

	exitcode = !number_failed ? EXIT_SUCCESS : EXIT_FAILURE;

	return exitcode;
}
