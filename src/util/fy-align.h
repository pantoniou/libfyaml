/*
 * fy-align.h - various align related stuff
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_ALIGN_H
#define FY_ALIGN_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>

#if defined(__GNUC__) || defined(__clang__)
#define FY_ALIGNED_TO(x) __attribute__ ((aligned(x)))
#elif defined(_MSC_VER)
#define FY_ALIGNED_TO(x) /* nothing - MSVC doesn't support trailing alignment */
#else
#define FY_ALIGNED_TO(x) /* nothing */
#endif

#define FY_ALIGN(_align, _x) (((_x) + ((_align) - 1)) & ~((_align) - 1))

/* cachelines are universally 64 bytes */
#define FY_CACHELINE_SIZE 64
#define FY_CACHELINE_SIZE_ALIGN(_x) FY_ALIGN(FY_CACHELINE_SIZE, _x)
#define FY_CACHELINE_ALIGN FY_ALIGNED_TO(FY_CACHELINE_SIZE)

static inline void *fy_align_alloc(size_t align, size_t size)
{
	void *p;

	size = FY_ALIGN(align, size);
#ifndef _WIN32
	if (posix_memalign(&p, align, size))
		return NULL;
#else
	p = _aligned_malloc(size, align);
	if (!p)
		return NULL;
#endif
	return p;
}

static inline void fy_align_free(void *p)
{
	if (!p)
		return;
#ifndef _WIN32
	free(p);
#else
	_aligned_free(p);
#endif
}

static inline void *fy_cacheline_alloc(size_t size)
{
	return fy_align_alloc(FY_CACHELINE_SIZE, size);
}

static inline void fy_cacheline_free(void *p)
{
	fy_align_free(p);
}

#endif
