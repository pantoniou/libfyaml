/*
 * fy-internal-align.h - various align related stuff
 *
 * Copyright (c) 2023-2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_INTERNAL_ALIGN_H
#define FY_INTERNAL_ALIGN_H

#include <stdint.h>
#include <stdlib.h>

#if defined(__GNUC__) || defined(__clang__)
#define FY_ALIGNED_TO(x) __attribute__ ((aligned(x)))
#elif defined(_MSC_VER)
#define FY_ALIGNED_TO(x) __declspec(align(x))
#else
#define FY_ALIGNED_TO(x) /* nothing */
#endif

#define FY_ALIGN(_align, _x) (((_x) + ((_align) - 1)) & ~((_align) - 1))

/* cachelines are universally 64 bytes */
#define FY_CACHELINE_SIZE 64
#define FY_CACHELINE_SIZE_ALIGN(_x) FY_ALIGN(FY_CACHELINE_SIZE, _x)
#define FY_CACHELINE_ALIGN FY_ALIGNED_TO(FY_CACHELINE_SIZE)

/* provide posix_memalign for platforms that don't have it */
#ifdef _WIN32
static inline int posix_memalign(void **ptr, size_t align, size_t size)
{
	void *p;

	/* must be a power of two */
	if ((size & (size -1)) != 0) {
		*ptr = NULL;
		return EINVAL;
	}

	p = _aligned_malloc(size, align);
	if (!p) {
		*ptr = NULL;
		return ENOMEM;
	}
	return p;
}

static inline void posix_memalign_free(void *p)
{
	_aligned_free(p);
}

#else
/* normal implementations just use free */
static inline void posix_memalign_free(void *p)
{
	free(p);
}
#endif

static inline void *fy_align_alloc(size_t align, size_t size)
{
	void *p;
	int rc;

	size = FY_ALIGN(align, size);
	rc = posix_memalign(&p, align, size);
	if (rc)
		return NULL;
	return p;
}

static inline void fy_align_free(void *p)
{
	if (p)
		posix_memalign_free(p);
}

static inline void *fy_cacheline_alloc(size_t size)
{
	return fy_align_alloc(FY_CACHELINE_SIZE, size);
}

static inline void fy_cacheline_free(void *p)
{
	fy_align_free(p);
}

static inline void *fy_ptr_align(void *p, size_t align)
{
	return (void *)(((uintptr_t)p + (align - 1)) & ~(align - 1));
}

static inline size_t fy_size_t_align(size_t size, size_t align)
{
	return (size + (align - 1)) & ~(align - 1);
}

#define fy_alloca_align(_sz, _align) \
	({ \
		const size_t __sz = (_sz); \
		const size_t __align = (_align); \
		void *__p; \
		__p = __align <= sizeof(max_align_t) ? alloca(__sz) : fy_ptr_align(alloca(__sz + __align - 1), __align); \
		__p; \
	})

#endif
