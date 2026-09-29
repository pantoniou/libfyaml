/*
 * libfyaml-test-alloc-fail.c - allocation failure injection for tests
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdlib.h>

#include "libfyaml-test-alloc-fail.h"

#ifdef HAVE_LINKER_WRAP_MALLOC

extern void *__real_malloc(size_t size);
extern void *__real_calloc(size_t nmemb, size_t size);
extern void *__real_realloc(void *ptr, size_t size);
extern int __real_posix_memalign(void **memptr, size_t alignment, size_t size);
extern char *__real_strdup(const char *s);

static unsigned int alloc_seen, alloc_fail_nth;
static int fail_empty_strdup;
static int empty_strdup_failed;

/* count each allocation, and fail the one that is armed */
static int alloc_fails(void)
{
	return alloc_fail_nth && ++alloc_seen == alloc_fail_nth;
}

void *__wrap_malloc(size_t size)
{
	return alloc_fails() ? NULL : __real_malloc(size);
}

void *__wrap_calloc(size_t nmemb, size_t size)
{
	return alloc_fails() ? NULL : __real_calloc(nmemb, size);
}

void *__wrap_realloc(void *ptr, size_t size)
{
	return alloc_fails() ? NULL : __real_realloc(ptr, size);
}

int __wrap_posix_memalign(void **memptr, size_t alignment, size_t size)
{
	return alloc_fails() ? ENOMEM : __real_posix_memalign(memptr, alignment, size);
}

char *__wrap_strdup(const char *s)
{
	if (fail_empty_strdup && s && !*s) {
		fail_empty_strdup = 0;
		empty_strdup_failed = 1;
		return NULL;
	}
	return __real_strdup(s);
}

void fy_alloc_fail_arm(unsigned int nth)
{
	alloc_seen = 0;
	alloc_fail_nth = nth;
}

void fy_alloc_fail_disarm(void)
{
	alloc_fail_nth = 0;
	fail_empty_strdup = 0;
}

void fy_alloc_fail_empty_strdup_arm(void)
{
	empty_strdup_failed = 0;
	fail_empty_strdup = 1;
}

int fy_alloc_fail_empty_strdup_seen(void)
{
	return empty_strdup_failed;
}

unsigned int fy_alloc_fail_seen(void)
{
	return alloc_seen;
}

#endif
