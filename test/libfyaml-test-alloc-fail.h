/*
 * libfyaml-test-alloc-fail.h - allocation failure injection for tests
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LIBFYAML_TEST_ALLOC_FAIL_H
#define LIBFYAML_TEST_ALLOC_FAIL_H

#ifdef HAVE_LINKER_WRAP_MALLOC

/* Fail the nth allocation (malloc, calloc or realloc) from this point;
 * nth of 0 disables the failure. */
void fy_alloc_fail_arm(unsigned int nth);

/* Stop failing allocations. */
void fy_alloc_fail_disarm(void);

/* The number of allocations that were seen since the last arm. */
unsigned int fy_alloc_fail_seen(void);

#endif

#endif
