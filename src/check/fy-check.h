/*
 * fy-check.h - a small test framework, in a header
 *
 * The tests of libfyaml need little: START_TEST/END_TEST, the ck_assert
 * macros, suites of test cases, and a runner that prints TAP. This file has
 * all of it, so that no test library is needed. The names of the macros are
 * those of libcheck.
 *
 * A failed assertion stops its test (longjmp) and the runner goes on with the
 * next one. There is no process for each test: ctest runs one test for each
 * process (the test name is the argument of the program), and a test that
 * crashes stops the run with a message. A test case timeout is only kept for
 * the callers; ctest has the timeouts of the tests.
 *
 * One file of the program defines FY_CHECK_IMPLEMENTATION before it includes
 * this header; it holds the state of the runner.
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_CHECK_H
#define FY_CHECK_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
#include "fy-win32.h"
#endif

#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <setjmp.h>
#include <string.h>

#include "fy-typelist.h"
#include "fy-types.h"

#if defined(__GNUC__) || defined(__clang__)
#define FY_CHECK_UNUSED __attribute__((unused))
#else
#define FY_CHECK_UNUSED
#endif

/* what START_TEST makes for a test */
struct fy_check_ttest {
	const char *name;
	void (*fn)(int);
	const char *file;
	int line;
};

struct fy_check_test;
struct fy_check_testcase;
struct fy_check_suite;
struct fy_check_runner;

FY_TYPE_FWD_DECL_LIST(check_test);
struct fy_check_test {
	struct list_head node;
	struct fy_check_testcase *testcase;
	const struct fy_check_ttest *test;
	char *name;
};
FY_TYPE_DECL_LIST(check_test);

FY_TYPE_FWD_DECL_LIST(check_testcase);
struct fy_check_testcase {
	struct list_head node;
	struct fy_check_suite *suite;
	double timeout;
	char *name;
	struct fy_check_test_list tests;
};
FY_TYPE_DECL_LIST(check_testcase);

struct fy_check_suite {
	struct fy_check_runner *runner;
	char *name;
	struct fy_check_testcase_list testcases;
	int argc;
	char **argv;
};

struct fy_check_runner {
	struct fy_check_suite *suite;
};

static inline void *
fy_check_malloc(size_t size)
{
	void *ptr;

	ptr = malloc(size);
	assert(ptr);
	if (!ptr)
		abort();
	memset(ptr, 0, size);
	return ptr;
}

static inline void
fy_check_free(void *ptr)
{
	if (ptr)
		free(ptr);
}

static inline char *
fy_check_strdup(const char *str)
{
	char *copy;

	copy = strdup(str);
	assert(copy);
	if (!copy)
		abort();
	return copy;
}

static inline void
fy_check_test_destroy(struct fy_check_test *ct)
{
	if (!ct)
		return;
	fy_check_free(ct->name);
	fy_check_free(ct);
}

static inline struct fy_check_test *
fy_check_test_create(struct fy_check_testcase *ctc, const struct fy_check_ttest *test)
{
	struct fy_check_test *ct = fy_check_malloc(sizeof(*ct));
	ct->testcase = ctc;
	ct->test = test;
	ct->name = fy_check_strdup(test->name);
	return ct;
}

static inline struct fy_check_test *
fy_check_testcase_add_test(struct fy_check_testcase *ctc, const struct fy_check_ttest *test)
{
	struct fy_check_test *ct;
	struct fy_check_suite *cs;
	int i;

	if (!ctc)
		return NULL;

	/* filter out tests */
	cs = ctc->suite;
	if (cs->argc > 0) {
		for (i = 0; i < cs->argc; i++) {
			if (!strcmp(test->name, cs->argv[i]))
				break;
		}
		if (i >= cs->argc)
			return NULL;
	}

	ct = fy_check_test_create(ctc, test);
	fy_check_test_list_add_tail(&ctc->tests, ct);
	return ct;
}

static inline void
fy_check_testcase_destroy(struct fy_check_testcase *ctc)
{
	struct fy_check_test *ct;

	if (!ctc)
		return;

	while ((ct = fy_check_test_list_pop(&ctc->tests)) != NULL)
		fy_check_test_destroy(ct);
	fy_check_free(ctc->name);
	fy_check_free(ctc);
}

static inline struct fy_check_testcase *
fy_check_testcase_create(struct fy_check_suite *cs, const char *name)
{
	struct fy_check_testcase *ctc = fy_check_malloc(sizeof(*ctc));
	ctc->suite = cs;
	ctc->name = fy_check_strdup(name);
	fy_check_test_list_init(&ctc->tests);

	return ctc;
}

static inline void
fy_check_testcase_set_timeout(struct fy_check_testcase *ctc, double timeout)
{
	if (ctc)
		ctc->timeout = timeout;
}

static inline void
fy_check_suite_destroy(struct fy_check_suite *cs)
{
	struct fy_check_testcase *tc;

	if (!cs)
		return;

	while ((tc = fy_check_testcase_list_pop(&cs->testcases)) != NULL)
		fy_check_testcase_destroy(tc);
	fy_check_free(cs->name);
	fy_check_free(cs);
}

static inline struct fy_check_suite *
fy_check_suite_create(const char *name, int argc, char *argv[])
{
	struct fy_check_suite *cs = fy_check_malloc(sizeof(*cs));

	cs->name = fy_check_strdup(name);
	cs->runner = NULL;
	fy_check_testcase_list_init(&cs->testcases);
	if (argc > 0) {
		cs->argc = argc;
		cs->argv = argv;
	}
	return cs;
}

static inline struct fy_check_testcase *
fy_check_suite_add_test_case(struct fy_check_suite *cs, const char *name)
{
	struct fy_check_testcase *ctc;
	if (!cs)
		return NULL;

	ctc = fy_check_testcase_create(cs, name);
	fy_check_testcase_list_add_tail(&cs->testcases, ctc);
	return ctc;
}

static inline void
fy_check_runner_destroy(struct fy_check_runner *cr)
{
	if (!cr)
		return;

	fy_check_suite_destroy(cr->suite);
	fy_check_free(cr);
}

static inline struct fy_check_runner *
fy_check_runner_create(struct fy_check_suite *cs)
{
	struct fy_check_runner *cr = fy_check_malloc(sizeof(*cr));
	cr->suite = cs;
	cs->runner = cr;
	return cr;
}

static inline struct fy_check_testcase *
fy_check_suite_get_testcase_by_name(struct fy_check_suite *cs, const char *name)
{
	struct fy_check_testcase *ctc;

	if (!cs || !name)
		return NULL;

	for (ctc = fy_check_testcase_list_head(&cs->testcases); ctc;
	     ctc = fy_check_testcase_next(&cs->testcases, ctc)) {
		if (!strcmp(ctc->name, name))
			return ctc;
	}

	return NULL;
}

static inline struct fy_check_test *
fy_check_suite_get_test_by_name(struct fy_check_suite *cs, const char *name)
{
	struct fy_check_testcase *ctc;
	struct fy_check_test *ct;

	if (!cs || !name)
		return NULL;

	for (ctc = fy_check_testcase_list_head(&cs->testcases); ctc;
	     ctc = fy_check_testcase_next(&cs->testcases, ctc)) {
		for (ct = fy_check_test_list_head(&ctc->tests); ct;
		     ct = fy_check_test_next(&ctc->tests, ct)) {
			if (!strcmp(ct->name, name))
				return ct;
		}
	}

	return NULL;
}

/*
 * Tests and assertions
 */

#define START_TEST(_name) \
	static void _name##_fn(int _i FY_CHECK_UNUSED); \
	static const struct fy_check_ttest _name##_ttest = { \
		#_name, _name##_fn, __FILE__, __LINE__ \
	}; \
	static const struct fy_check_ttest *const _name FY_CHECK_UNUSED = &_name##_ttest; \
	static void _name##_fn(int _i FY_CHECK_UNUSED)

/* the body of the test brings its own braces, so there is nothing to close */
#define END_TEST

/*
 * Stop the running test. After the base text comes an optional printf style
 * format and its arguments; the list ends with NULL (NULL for no format).
 */
void fy_check_failed(const char *file, int line, const char *base, ...);

#define ck_abort_msg(...) \
	fy_check_failed(__FILE__, __LINE__, "Failed", ## __VA_ARGS__, NULL)
#define ck_abort() ck_abort_msg(NULL)

#define ck_assert_msg(_expr, ...) \
	do { \
		if (!(_expr)) \
			fy_check_failed(__FILE__, __LINE__, "Assertion '" #_expr "' failed", \
					## __VA_ARGS__, NULL); \
	} while (0)
#define ck_assert(_expr) ck_assert_msg(_expr, NULL)

#define _ck_assert_cmp(_t, _fmt, _x, _op, _y) \
	do { \
		_t _ck_x = (_t)(_x); \
		_t _ck_y = (_t)(_y); \
		if (!(_ck_x _op _ck_y)) \
			fy_check_failed(__FILE__, __LINE__, \
					"Assertion '" #_x " " #_op " " #_y "' failed", \
					"%s == " _fmt ", %s == " _fmt, \
					#_x, _ck_x, #_y, _ck_y, NULL); \
	} while (0)

#define _ck_assert_int(_x, _op, _y)	_ck_assert_cmp(intmax_t, "%" PRIdMAX, _x, _op, _y)
#define _ck_assert_uint(_x, _op, _y)	_ck_assert_cmp(uintmax_t, "%" PRIuMAX, _x, _op, _y)
#define _ck_assert_double(_x, _op, _y)	_ck_assert_cmp(double, "%g", _x, _op, _y)

#define ck_assert_int_eq(_x, _y)	_ck_assert_int(_x, ==, _y)
#define ck_assert_int_ne(_x, _y)	_ck_assert_int(_x, !=, _y)
#define ck_assert_int_lt(_x, _y)	_ck_assert_int(_x, <, _y)
#define ck_assert_int_le(_x, _y)	_ck_assert_int(_x, <=, _y)
#define ck_assert_int_gt(_x, _y)	_ck_assert_int(_x, >, _y)
#define ck_assert_int_ge(_x, _y)	_ck_assert_int(_x, >=, _y)

#define ck_assert_uint_eq(_x, _y)	_ck_assert_uint(_x, ==, _y)
#define ck_assert_uint_ne(_x, _y)	_ck_assert_uint(_x, !=, _y)
#define ck_assert_uint_lt(_x, _y)	_ck_assert_uint(_x, <, _y)
#define ck_assert_uint_le(_x, _y)	_ck_assert_uint(_x, <=, _y)
#define ck_assert_uint_gt(_x, _y)	_ck_assert_uint(_x, >, _y)
#define ck_assert_uint_ge(_x, _y)	_ck_assert_uint(_x, >=, _y)

#define ck_assert_double_eq(_x, _y)	_ck_assert_double(_x, ==, _y)
#define ck_assert_double_ne(_x, _y)	_ck_assert_double(_x, !=, _y)
#define ck_assert_double_lt(_x, _y)	_ck_assert_double(_x, <, _y)
#define ck_assert_double_le(_x, _y)	_ck_assert_double(_x, <=, _y)
#define ck_assert_double_gt(_x, _y)	_ck_assert_double(_x, >, _y)
#define ck_assert_double_ge(_x, _y)	_ck_assert_double(_x, >=, _y)

#define _ck_assert_ptr(_x, _op, _y) \
	do { \
		const void *_ck_x = (const void *)(_x); \
		const void *_ck_y = (const void *)(_y); \
		if (!(_ck_x _op _ck_y)) \
			fy_check_failed(__FILE__, __LINE__, \
					"Assertion '" #_x " " #_op " " #_y "' failed", \
					"%s == %p, %s == %p", #_x, _ck_x, #_y, _ck_y, NULL); \
	} while (0)

#define ck_assert_ptr_eq(_x, _y)	_ck_assert_ptr(_x, ==, _y)
#define ck_assert_ptr_ne(_x, _y)	_ck_assert_ptr(_x, !=, _y)
#define ck_assert_ptr_null(_x)		_ck_assert_ptr(_x, ==, NULL)
#define ck_assert_ptr_nonnull(_x)	_ck_assert_ptr(_x, !=, NULL)

#define _ck_assert_str(_x, _op, _y) \
	do { \
		const char *_ck_x = (_x); \
		const char *_ck_y = (_y); \
		if (!(_ck_x && _ck_y && strcmp(_ck_x, _ck_y) _op 0)) \
			fy_check_failed(__FILE__, __LINE__, \
					"Assertion '" #_x " " #_op " " #_y "' failed", \
					"%s == \"%s\", %s == \"%s\"", \
					#_x, _ck_x ? _ck_x : "(null)", \
					#_y, _ck_y ? _ck_y : "(null)", NULL); \
	} while (0)

#define ck_assert_str_eq(_x, _y)	_ck_assert_str(_x, ==, _y)
#define ck_assert_str_ne(_x, _y)	_ck_assert_str(_x, !=, _y)

#define ck_assert_mem_eq(_x, _y, _n) \
	do { \
		const void *_ck_x = (_x); \
		const void *_ck_y = (_y); \
		size_t _ck_n = (size_t)(_n); \
		if (_ck_n && (!_ck_x || !_ck_y || memcmp(_ck_x, _ck_y, _ck_n))) \
			fy_check_failed(__FILE__, __LINE__, \
					"Assertion '" #_x " == " #_y "' failed", \
					"%s != %s for %zu bytes", #_x, #_y, _ck_n, NULL); \
	} while (0)

/*
 * The runner
 */

/* runs the tests of the suite in this process; returns the number of failures */
int fy_check_runner_run(struct fy_check_runner *cr, bool quiet);

#ifdef FY_CHECK_IMPLEMENTATION

#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

static jmp_buf fy_check_jmp;
static bool fy_check_running;
/* the text of the last failure; it has the size it needs, a generic can print a lot */
static char *fy_check_message;
static const struct fy_check_ttest *fy_check_current;
static int fy_check_current_number;
#ifdef _WIN32
static DWORD fy_check_main_thread;
#else
static pthread_t fy_check_main_thread;
#endif

static bool fy_check_in_main_thread(void)
{
#ifdef _WIN32
	return GetCurrentThreadId() == fy_check_main_thread;
#else
	return pthread_equal(pthread_self(), fy_check_main_thread);
#endif
}

void fy_check_failed(const char *file, int line, const char *base, ...)
{
	va_list ap, ap2;
	const char *fmt;
	int head, tail = 0;
	size_t size;
	char *msg;

	va_start(ap, base);
	fmt = va_arg(ap, const char *);

	head = snprintf(NULL, 0, "%s:%d: %s", file, line, base);
	if (head < 0)
		head = 0;
	if (fmt) {
		va_copy(ap2, ap);
		tail = vsnprintf(NULL, 0, fmt, ap2);
		va_end(ap2);
		if (tail < 0)
			tail = 0;
	}

	size = (size_t)head + (fmt ? 2 : 0) + (size_t)tail + 1;
	msg = malloc(size);
	if (!msg) {
		va_end(ap);
		fprintf(stderr, "%s:%d: %s: out of memory for the message\n", file, line, base);
		_Exit(EXIT_FAILURE);
	}
	snprintf(msg, size, "%s:%d: %s", file, line, base);
	if (fmt) {
		msg[head] = ':';
		msg[head + 1] = ' ';
		vsnprintf(msg + head + 2, (size_t)tail + 1, fmt, ap);
	}
	va_end(ap);

	/* the message of an earlier failure is not needed any more */
	free(fy_check_message);
	fy_check_message = msg;

	/* only the thread of the runner can jump back to it; another one ends the run */
	if (!fy_check_running || !fy_check_in_main_thread()) {
		printf("not ok %d - %s: %s\n", fy_check_current_number,
		       fy_check_current ? fy_check_current->name : "?", fy_check_message);
		fflush(stdout);
		_Exit(EXIT_FAILURE);
	}
	longjmp(fy_check_jmp, 1);
}

static void fy_check_crashed(int sig)
{
	printf("not ok %d - %s: crashed (signal %d)\n", fy_check_current_number,
	       fy_check_current ? fy_check_current->name : "?", sig);
	fflush(stdout);
	_Exit(EXIT_FAILURE);
}

int fy_check_runner_run(struct fy_check_runner *cr, bool quiet)
{
	struct fy_check_suite *cs = cr->suite;
	struct fy_check_testcase *ctc;
	struct fy_check_test *ct;
	int total = 0, failed = 0;
	bool ok;

#ifdef _WIN32
	fy_check_main_thread = GetCurrentThreadId();
#else
	fy_check_main_thread = pthread_self();
#endif
	signal(SIGSEGV, fy_check_crashed);
	signal(SIGABRT, fy_check_crashed);
	signal(SIGFPE, fy_check_crashed);
#ifdef SIGBUS
	signal(SIGBUS, fy_check_crashed);
#endif

	if (!quiet)
		printf("Running suite(s): %s\n", cs->name);

	for (ctc = fy_check_testcase_list_head(&cs->testcases); ctc;
	     ctc = fy_check_testcase_next(&cs->testcases, ctc)) {
		for (ct = fy_check_test_list_head(&ctc->tests); ct;
		     ct = fy_check_test_next(&ctc->tests, ct)) {
			total++;
			fy_check_current = ct->test;
			fy_check_current_number = total;

			ok = true;
			fy_check_running = true;
			if (setjmp(fy_check_jmp) == 0)
				ct->test->fn(0);
			else
				ok = false;
			fy_check_running = false;

			if (ok) {
				printf("ok %d - %s:%s:%s: Passed\n", total, ct->test->file,
				       ctc->name, ct->name);
			} else {
				failed++;
				printf("not ok %d - %s:%s:%s: %s\n", total, ct->test->file,
				       ctc->name, ct->name, fy_check_message);
			}
			fflush(stdout);
		}
	}

	free(fy_check_message);
	fy_check_message = NULL;

	if (!quiet)
		printf("%d%%: Checks: %d, Failures: %d, Errors: 0\n",
		       total ? (total - failed) * 100 / total : 100, total, failed);
	printf("1..%d\n", total);
	fflush(stdout);

	return failed;
}

#endif /* FY_CHECK_IMPLEMENTATION */

#endif
