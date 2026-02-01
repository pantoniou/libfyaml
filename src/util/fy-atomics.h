/*
 * fy-atomic.h - fy atomics
 *
 * Copyright (c) 2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_ATOMIC_H
#define FY_ATOMIC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>

#include "fy-utils.h"

/* Detect standard C11 atomics */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
#define FY_HAVE_STDATOMIC_H
#define FY_HAVE_C11_ATOMICS
#endif

/* Detect compiler extensions for _Atomic */
#if !defined(FY_HAVE_C11_ATOMICS)
#undef FY_HAVE_STDATOMIC_H

#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
#define FY_HAVE_C11_ATOMICS
#elif defined(__clang__) && defined(__has_extension)
#if __has_extension(c_atomic)
#define FY_HAVE_C11_ATOMICS
#endif
#endif

#endif

/* Decide macro */
#ifdef FY_HAVE_C11_ATOMICS
#define FY_HAVE_ATOMICS
#else
#undef FY_HAVE_ATOMICS
#define _Atomic(_x) _x
#endif

#if defined(FY_HAVE_STDATOMIC_H)
#define FY_HAVE_SAFE_ATOMIC_OPS
#else

#undef FY_HAVE_SAFE_ATOMIC_OPS

typedef bool atomic_flag;

#define atomic_load(_ptr) \
	(*(_ptr))

#define atomic_store(_ptr, _val) \
	do { \
		*(_obj) = (_val); \
	} while(0)

#define atomic_exchange(_ptr, _v) \
	({ \
		__typeof__(_ptr) __ptr = (_ptr); \
		__typeof__(*(_ptr)) __old = *__ptr; \
		*__ptr = (_v); \
		__old; \
	})

#define atomic_compare_exchange_strong(_ptr, _exp, _des) \
	({ \
		__typeof__(_ptr) __ptr = (_ptr); \
		__typeof__(*(_ptr)) __old = *__ptr; \
		bool __res; \
		if (*__ptr == *(_exp)) { \
			*__ptr = (_des); \
			__res = true; \
		} else \
			*__res = false; \
		__res; \
	})

#define atomic_compare_exchange_weak(_ptr, _exp, _des) \
	atomic_compare_exchange_strong((_ptr), (_exp), (_des))

#define atomic_fetch_add(_ptr, _v) \
	({ \
		__typeof__(*(_ptr)) __old = *__ptr; \
		*__ptr += (_v); \
		__old; \
	})

#define atomic_fetch_sub(_ptr, _v) \
	({ \
		__typeof__(*(_ptr)) __old = *__ptr; \
		*__ptr -= (_v); \
		__old; \
	})

#define atomic_fetch_or(_ptr, _v) \
	({ \
		__typeof__(*(_ptr)) __old = *__ptr; \
		*__ptr |= (_v); \
		__old; \
	})

#define atomic_fetch_xor(_ptr, _v) \
	({ \
		__typeof__(*(_ptr)) __old = *__ptr; \
		*__ptr ^= (_v); \
		__old; \
	})

#define atomic_fetch_and(_ptr, _v) \
	({ \
		__typeof__(*(_ptr)) __old = *__ptr; \
		*__ptr &= (_v); \
		__old; \
	})

#define atomic_flag_clear(_ptr) \
	do { \
		*(_ptr) = false; \
	} while(0)

#define atomic_flag_set(_ptr) \
	do { \
		*(_ptr) = true; \
	} while(0)


#define atomic_flag_test_and_set(_ptr) \
	({ \
		volatile atomic_flag *__ptr = (_ptr); \
		if (!*__ptr) { \
			*__ptr = true; \
			__ret = true; \
		} else \
			__ret = false; \
		__ret; \
	}

#endif

/* OK, now define FY version */
#define FY_ATOMIC(_x) _Atomic(_x)
#define fy_atomic_flag atomic_flag

#define fy_atomic_load(_ptr) \
	atomic_load((_ptr))
#define fy_atomic_store(_ptr, _v) \
	atomic_store((_ptr), (_v))
#define fy_atomic_exchange(_ptr, _v) \
	atomic_exchange((_ptr), (_v))
#define fy_atomic_compare_exchange_strong(_ptr, _e, _d) \
	atomic_compare_exchange_strong((_ptr), (_e), (_d))
#define fy_atomic_compare_exchange_weak(_ptr, _e, _d) \
	atomic_compare_exchange_weak((_ptr), (_e), (_d))
#define fy_atomic_fetch_add(_ptr, _v) \
	atomic_fetch_add((_ptr), (_v))
#define fy_atomic_fetch_sub(_ptr, _v) \
	atomic_fetch_sub((_ptr), (_v))
#define fy_atomic_fetch_or(_ptr, _v) \
	atomic_fetch_or((_ptr), (_v))
#define fy_atomic_fetch_xor(_ptr, _v) \
	atomic_fetch_xor((_ptr), (_v))
#define fy_atomic_fetch_and(_ptr, _v) \
	atomic_fetch_and((_ptr), (_v))
#define fy_atomic_flag_clear(_ptr) \
	atomic_flag_clear((_ptr))
#define fy_atomic_flag_set(_ptr) \
	atomic_flag_set((_ptr))
#define fy_atomic_flag_test_and_set(_ptr) \
	atomic_flag_test_and_set((_ptr))

static inline void fy_cpu_relax(void)
{
#if defined(__x86_64__) || defined(__i386__)
	__builtin_ia32_pause();
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
	_mm_pause();
#elif defined(__aarch64__) || defined(__arm__)
	__asm__ volatile ("yield");
#elif defined(__powerpc__)
	__asm__ volatile ("or 27,27,27");
#else
	__asm__ volatile ("" : : : "memory");
#endif
}

static inline uint64_t
fy_atomic_get_and_clear_counter(FY_ATOMIC(uint64_t) *ptr)
{
	uint64_t v;

	v = fy_atomic_load(ptr);
	fy_atomic_fetch_sub(ptr, v);
	return v;
}

#endif
