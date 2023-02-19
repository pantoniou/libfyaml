/*
 * libfyaml-align.h - various align related stuff
 *
 * Copyright (c) 2023-2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LIBFYAML_ALIGN_H
#define LIBFYAML_ALIGN_H

#include <stdint.h>
#include <stdlib.h>
#ifdef _WIN32
#include <malloc.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DOC: Alignment macros and aligned allocation helpers
 *
 * This header provides portable utilities for working with memory alignment
 * requirements, from compile-time attributes to runtime allocation and
 * pointer-rounding helpers.  It has no libfyaml dependencies.
 *
 * **Compile-time attributes**
 *
 * - ``FY_ALIGNED_TO(x)``   — apply an alignment attribute to a variable or
 *   type; expands to ``__attribute__((aligned(x)))`` on GCC/Clang and
 *   ``__declspec(align(x))`` on MSVC
 * - ``FY_CACHELINE_ALIGN`` — shorthand for cache-line (64-byte) alignment,
 *   useful for preventing false sharing between fields accessed concurrently
 *   by different threads
 *
 * **Value rounding**
 *
 * - ``FY_ALIGN(align, x)``         — round an integer up to the next
 *   multiple of ``align`` (must be a power of two)
 * - ``FY_CACHELINE_SIZE_ALIGN(x)``  — round up to the next cache-line boundary
 * - ``fy_ptr_align(p, align)``      — round a pointer up to alignment
 * - ``fy_size_t_align(sz, align)``  — round a size_t value up to alignment
 *
 * **Heap allocation**
 *
 * - ``fy_align_alloc(align, size)`` / ``fy_align_free(p)`` — allocate and
 *   free memory with an explicit alignment (``posix_memalign`` on POSIX,
 *   ``_aligned_malloc`` on Windows)
 * - ``fy_cacheline_alloc(size)``    / ``fy_cacheline_free(p)`` — convenience
 *   wrappers that fix the alignment at ``FY_CACHELINE_SIZE``
 *
 * **Stack allocation**
 *
 * - ``fy_alloca_align(sz, align)`` — allocate a block on the stack with a
 *   specified alignment; uses plain ``alloca`` when alignment fits within
 *   ``max_align_t``, otherwise over-allocates and advances the pointer
 */

/**
 * FY_ALIGNED_TO() - Declare that a variable or type has a minimum alignment.
 *
 * Expands to the appropriate compiler-specific alignment attribute:
 * - GCC/Clang: ``__attribute__((aligned(x)))``
 * - MSVC:      ``__declspec(align(x))``
 * - Other:     empty (no enforced alignment)
 *
 * @x: Required alignment in bytes (must be a power of two).
 */
#if defined(__GNUC__) || defined(__clang__)
#define FY_ALIGNED_TO(x) __attribute__ ((aligned(x)))
#elif defined(_MSC_VER)
#define FY_ALIGNED_TO(x) __declspec(align(x))
#else
#define FY_ALIGNED_TO(x) /* nothing */
#endif

/**
 * FY_ALIGN() - Round @_x up to the next multiple of @_align.
 *
 * @_align must be a power of two. The result is always >= @_x and
 * is the smallest multiple of @_align that is >= @_x.
 *
 * @_align: Alignment boundary (power of two).
 * @_x:     Value to round up.
 *
 * Returns: @_x rounded up to the nearest multiple of @_align.
 */
#define FY_ALIGN(_align, _x) (((_x) + ((_align) - 1)) & ~((_align) - 1))

/**
 * FY_CACHELINE_SIZE - Size of a CPU cache line in bytes.
 *
 * Universally 64 bytes on all currently supported architectures
 * (x86, x86_64, ARM, ARM64, PowerPC).
 */
#define FY_CACHELINE_SIZE 64

/**
 * FY_CACHELINE_SIZE_ALIGN() - Round @_x up to the next cache-line boundary.
 *
 * @_x: Value to round up.
 *
 * Returns: @_x rounded up to the nearest multiple of %FY_CACHELINE_SIZE.
 */
#define FY_CACHELINE_SIZE_ALIGN(_x) FY_ALIGN(FY_CACHELINE_SIZE, _x)

/**
 * FY_CACHELINE_ALIGN - Alignment attribute for cache-line-aligned objects.
 *
 * Apply to a variable or struct field to ensure it starts on a cache-line
 * boundary, preventing false sharing between concurrent readers/writers.
 *
 * Example::
 *
 *   struct my_data {
 *       FY_CACHELINE_ALIGN int hot_counter;
 *       int cold_field;
 *   };
 */
#define FY_CACHELINE_ALIGN FY_ALIGNED_TO(FY_CACHELINE_SIZE)

/**
 * fy_align_alloc() - Allocate memory with a specific alignment.
 *
 * Allocates @size bytes rounded up to the nearest multiple of @align, with
 * the returned pointer guaranteed to be a multiple of @align.
 *
 * Uses ``posix_memalign()`` on POSIX systems and ``_aligned_malloc()`` on
 * Windows. Free the result with fy_align_free().
 *
 * @align: Required alignment in bytes (must be a power of two and >= ``sizeof(void *)`` ).
 * @size:  Number of bytes to allocate.
 *
 * Returns: Pointer to the allocated block, or NULL on failure.
 */
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

/**
 * fy_align_free() - Free memory allocated by fy_align_alloc().
 *
 * A NULL @p is silently ignored.
 *
 * @p: Pointer previously returned by fy_align_alloc(), or NULL.
 */
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

/**
 * fy_cacheline_alloc() - Allocate cache-line-aligned memory.
 *
 * Equivalent to ``fy_align_alloc(FY_CACHELINE_SIZE, size)``.
 * Free the result with fy_cacheline_free().
 *
 * @size: Number of bytes to allocate.
 *
 * Returns: Cache-line-aligned pointer, or NULL on failure.
 */
static inline void *fy_cacheline_alloc(size_t size)
{
	return fy_align_alloc(FY_CACHELINE_SIZE, size);
}

/**
 * fy_cacheline_free() - Free memory allocated by fy_cacheline_alloc().
 *
 * A NULL @p is silently ignored.
 *
 * @p: Pointer previously returned by fy_cacheline_alloc(), or NULL.
 */
static inline void fy_cacheline_free(void *p)
{
	fy_align_free(p);
}

/**
 * fy_ptr_align() - Round a pointer up to the next multiple of @align.
 *
 * Does not allocate any memory; the caller is responsible for ensuring
 * the underlying buffer extends at least (@align - 1) bytes past @p.
 *
 * @p:     Pointer to align.
 * @align: Alignment boundary in bytes (must be a power of two).
 *
 * Returns: @p rounded up to the nearest multiple of @align.
 */
static inline void *fy_ptr_align(void *p, size_t align)
{
	return (void *)(((uintptr_t)p + (align - 1)) & ~(align - 1));
}

/**
 * fy_size_t_align() - Round a size_t value up to the next multiple of @align.
 *
 * @size:  Value to round up.
 * @align: Alignment boundary in bytes (must be a power of two).
 *
 * Returns: @size rounded up to the nearest multiple of @align.
 */
static inline size_t fy_size_t_align(size_t size, size_t align)
{
	return (size + (align - 1)) & ~(align - 1);
}

/**
 * fy_alloca_align() - Stack-allocate a buffer with a specific alignment.
 *
 * Expands to a statement expression (GCC extension) that allocates @_sz bytes
 * on the stack, aligned to @_align bytes. When @_align <= ``sizeof(max_align_t)``
 * a plain ``alloca()`` is used; otherwise @_sz + @_align - 1 bytes are allocated
 * and the pointer is advanced with fy_ptr_align().
 * This macro does not work on MSVC.
 *
 * The result is valid only for the lifetime of the enclosing function; do not
 * return or store it beyond that scope.
 *
 * @_sz:    Number of bytes to allocate.
 * @_align: Required alignment in bytes (must be a power of two).
 *
 * Returns: Stack pointer aligned to @_align.
 */
#define fy_alloca_align(_sz, _align) \
	({ \
		const size_t __sz = (_sz); \
		const size_t __align = (_align); \
		void *__p; \
		__p = __align <= sizeof(max_align_t) ? alloca(__sz) : fy_ptr_align(alloca(__sz + __align - 1), __align); \
		__p; \
	})

#ifdef __cplusplus
}
#endif

#endif
