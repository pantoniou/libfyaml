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

#include <stdlib.h>

#include "libfyaml-test-alloc-fail.h"

#ifdef HAVE_LINKER_WRAP_MALLOC

extern void *__real_malloc(size_t size);

static unsigned int alloc_seen, alloc_fail_nth;

void *__wrap_malloc(size_t size)
{
	if (alloc_fail_nth && ++alloc_seen == alloc_fail_nth)
		return NULL;
	return __real_malloc(size);
}

void fy_alloc_fail_arm(unsigned int nth)
{
	alloc_seen = 0;
	alloc_fail_nth = nth;
}

void fy_alloc_fail_disarm(void)
{
	alloc_fail_nth = 0;
}

unsigned int fy_alloc_fail_seen(void)
{
	return alloc_seen;
}

#endif
