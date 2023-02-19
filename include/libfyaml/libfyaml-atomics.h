/*
 * libfyaml-atomic.h - libfyaml atomics
 *
 * Copyright (c) 2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * DOC: Portable atomic operations
 *
 * This header provides a thin, portable abstraction over C11 ``<stdatomic.h>``
 * with graceful fallback for compilers that support ``_Atomic`` as an extension
 * (GCC without ``-std=c11``, Clang) and a last-resort non-atomic fallback for
 * toolchains that support neither.
 *
 * **Detection macros** (defined by this header, not meant for direct use):
 *
 * - ``FY_HAVE_STDATOMIC_H``   — ``<stdatomic.h>`` was successfully included;
 *                               the standard ``atomic_*`` functions and types
 *                               are available.
 * - ``FY_HAVE_C11_ATOMICS``   — the ``_Atomic`` qualifier is available, either
 *                               from ``<stdatomic.h>`` or as a compiler extension.
 * - ``FY_HAVE_ATOMICS``       — effective atomics are available (``_Atomic``
 *                               maps to a real atomic type).  When not defined,
 *                               ``_Atomic(_x)`` expands to plain ``_x`` and all
 *                               operations are non-atomic single-threaded stubs.
 * - ``FY_HAVE_SAFE_ATOMIC_OPS`` — the underlying operations are properly
 *                               memory-ordered (i.e. ``<stdatomic.h>`` is in
 *                               use).  Without this, operations are performed
 *                               as plain loads/stores with no memory barriers.
 *
 * **Public API** — all ``fy_atomic_*`` macros delegate to the selected backend:
 *
 * - FY_ATOMIC()                     — qualify a type as atomic
 * - fy_atomic_flag                  — boolean flag type
 * - fy_atomic_load() / fy_atomic_store()
 * - fy_atomic_exchange()
 * - fy_atomic_compare_exchange_strong() / fy_atomic_compare_exchange_weak()
 * - fy_atomic_fetch_add() / fy_atomic_fetch_sub()
 * - fy_atomic_fetch_or() / fy_atomic_fetch_xor() / fy_atomic_fetch_and()
 * - fy_atomic_flag_clear() / fy_atomic_flag_set() / fy_atomic_flag_test_and_set()
 * - fy_cpu_relax()                  — emit a CPU relaxation hint (PAUSE/YIELD)
 * - fy_atomic_get_and_clear_counter() — atomically read and subtract a counter
 */

#ifndef LIBFYAML_ATOMICS_H
#define LIBFYAML_ATOMICS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Detect standard C11 atomics */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
/*
 * FY_HAVE_STDATOMIC_H - Defined when ``<stdatomic.h>`` is available.
 *
 * When defined, the standard ``atomic_*`` types and functions are in scope
 * and all ``fy_atomic_*`` macros delegate to them.
 */
#define FY_HAVE_STDATOMIC_H
/*
 * FY_HAVE_C11_ATOMICS - Defined when the ``_Atomic`` qualifier is usable.
 *
 * Set when C11 ``<stdatomic.h>`` is available, or when the compiler supports
 * ``_Atomic`` as an extension (GCC without ``-std=c11``, Clang).
 * When not defined, ``_Atomic(_x)`` is a no-op and operations are non-atomic.
 */
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
/*
 * FY_HAVE_ATOMICS - Defined when real atomic types are available.
 *
 * ``FY_ATOMIC(_x)`` expands to ``_Atomic(_x)`` when this is defined.
 * When not defined, ``FY_ATOMIC(_x)`` is a no-op and all ``fy_atomic_*``
 * operations degenerate to plain non-atomic loads and stores.
 */
#define FY_HAVE_ATOMICS
#else
#undef FY_HAVE_ATOMICS
#define _Atomic(_x) _x
#endif

#if defined(FY_HAVE_STDATOMIC_H)
/*
 * FY_HAVE_SAFE_ATOMIC_OPS - Defined when atomic operations are memory-ordered.
 *
 * Set only when ``<stdatomic.h>`` is in use.  Without this, the fallback
 * implementations perform plain loads/stores with no memory barriers and are
 * therefore only safe in single-threaded contexts.
 */
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

/**
 * FY_ATOMIC() - Qualify a type as atomic.
 *
 * Expands to ``_Atomic(_x)`` when %FY_HAVE_ATOMICS is defined, or to plain
 * ``_x`` otherwise (non-atomic fallback).
 *
 * Example::
 *
 *   FY_ATOMIC(uint64_t) refcount;
 *
 * @_x: The underlying C type.
 */
#define FY_ATOMIC(_x) _Atomic(_x)

/**
 * fy_atomic_flag - A boolean flag that can be set/cleared/tested atomically.
 *
 * Backed by ``atomic_flag`` from ``<stdatomic.h>`` when available, or a plain
 * ``bool`` in the fallback path.
 */
#define fy_atomic_flag atomic_flag

/**
 * fy_atomic_load() - Atomically load the value at @_ptr.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified variable.
 *
 * Returns: The current value.
 */
#define fy_atomic_load(_ptr) \
	atomic_load((_ptr))

/**
 * fy_atomic_store() - Atomically store @_v at @_ptr.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified variable.
 * @_v:   Value to store.
 */
#define fy_atomic_store(_ptr, _v) \
	atomic_store((_ptr), (_v))

/**
 * fy_atomic_exchange() - Atomically replace the value at @_ptr with @_v.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified variable.
 * @_v:   New value to store.
 *
 * Returns: The old value that was at @_ptr before the exchange.
 */
#define fy_atomic_exchange(_ptr, _v) \
	atomic_exchange((_ptr), (_v))

/**
 * fy_atomic_compare_exchange_strong() - Strong CAS: replace @_ptr's value if it equals @_e.
 *
 * If ``*_ptr == *_e``, stores @_d into @_ptr and returns true.
 * Otherwise, loads the current value into @_e and returns false.
 * The strong variant never spuriously fails.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified variable.
 * @_e:   Pointer to the expected value (updated on failure).
 * @_d:   Desired value to store on success.
 *
 * Returns: true if the exchange succeeded, false otherwise.
 */
#define fy_atomic_compare_exchange_strong(_ptr, _e, _d) \
	atomic_compare_exchange_strong((_ptr), (_e), (_d))

/**
 * fy_atomic_compare_exchange_weak() - Weak CAS: may spuriously fail.
 *
 * Like fy_atomic_compare_exchange_strong() but may fail even when
 * ``*_ptr == *_e``. Prefer in retry loops where a spurious failure
 * is harmless and performance matters.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified variable.
 * @_e:   Pointer to the expected value (updated on failure).
 * @_d:   Desired value to store on success.
 *
 * Returns: true if the exchange succeeded, false otherwise.
 */
#define fy_atomic_compare_exchange_weak(_ptr, _e, _d) \
	atomic_compare_exchange_weak((_ptr), (_e), (_d))

/**
 * fy_atomic_fetch_add() - Atomically add @_v to @_ptr and return the old value.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified integer variable.
 * @_v:   Value to add.
 *
 * Returns: The value of @_ptr before the addition.
 */
#define fy_atomic_fetch_add(_ptr, _v) \
	atomic_fetch_add((_ptr), (_v))

/**
 * fy_atomic_fetch_sub() - Atomically subtract @_v from @_ptr and return the old value.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified integer variable.
 * @_v:   Value to subtract.
 *
 * Returns: The value of @_ptr before the subtraction.
 */
#define fy_atomic_fetch_sub(_ptr, _v) \
	atomic_fetch_sub((_ptr), (_v))

/**
 * fy_atomic_fetch_or() - Atomically OR @_v into @_ptr and return the old value.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified integer variable.
 * @_v:   Value to OR in.
 *
 * Returns: The value of @_ptr before the operation.
 */
#define fy_atomic_fetch_or(_ptr, _v) \
	atomic_fetch_or((_ptr), (_v))

/**
 * fy_atomic_fetch_xor() - Atomically XOR @_v into @_ptr and return the old value.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified integer variable.
 * @_v:   Value to XOR in.
 *
 * Returns: The value of @_ptr before the operation.
 */
#define fy_atomic_fetch_xor(_ptr, _v) \
	atomic_fetch_xor((_ptr), (_v))

/**
 * fy_atomic_fetch_and() - Atomically AND @_v into @_ptr and return the old value.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified integer variable.
 * @_v:   Value to AND in.
 *
 * Returns: The value of @_ptr before the operation.
 */
#define fy_atomic_fetch_and(_ptr, _v) \
	atomic_fetch_and((_ptr), (_v))

/**
 * fy_atomic_flag_clear() - Atomically clear a flag (set to false).
 *
 * @_ptr: Pointer to an fy_atomic_flag.
 */
#define fy_atomic_flag_clear(_ptr) \
	atomic_flag_clear((_ptr))

/**
 * fy_atomic_flag_set() - Atomically set a flag (set to true).
 *
 * Note: this is a libfyaml extension; standard ``<stdatomic.h>`` only
 * provides ``atomic_flag_test_and_set()``.  In the fallback path this
 * is implemented as a plain store.
 *
 * @_ptr: Pointer to an fy_atomic_flag.
 */
#define fy_atomic_flag_set(_ptr) \
	atomic_flag_set((_ptr))

/**
 * fy_atomic_flag_test_and_set() - Atomically set a flag and return its old value.
 *
 * Sets the flag to true and returns the value it held before the operation.
 * This is the standard test-and-set primitive.
 *
 * @_ptr: Pointer to an fy_atomic_flag.
 *
 * Returns: true if the flag was already set, false if it was clear.
 */
#define fy_atomic_flag_test_and_set(_ptr) \
	atomic_flag_test_and_set((_ptr))

/**
 * fy_cpu_relax() - Emit a CPU relaxation hint inside a spin-wait loop.
 *
 * Reduces power consumption and improves hyper-threading performance on
 * x86/x86_64 (``PAUSE``), signals a yield on AArch64/ARM (``YIELD``),
 * and emits a low-priority hint on PowerPC (``or 27,27,27``).
 * Falls back to a compiler memory barrier on unsupported architectures.
 *
 * Use inside tight spin loops to avoid memory-ordering penalties and
 * allow sibling hardware threads to make progress::
 *
 *   while (!fy_atomic_load(&ready))
 *       fy_cpu_relax();
 */
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

/*
 * fy_atomic_get_and_clear_counter() - Atomically read and drain a counter.
 *
 * Reads the current value of @ptr and subtracts that same value from it in
 * one logical step, effectively resetting the counter to zero while returning
 * how many units were accumulated since the last drain.
 *
 * Note: The read and subtract are two separate atomic operations, so another
 * thread may increment the counter between them. The returned value therefore
 * represents a snapshot taken at the moment of the load, not a strict
 * atomic drain. This is intentional and sufficient for statistics / rate
 * accounting where approximate values are acceptable.
 *
 * @ptr: Pointer to an FY_ATOMIC(uint64_t) counter.
 *
 * Returns: The value of the counter at the time of the load.
 */
static inline uint64_t
fy_atomic_get_and_clear_counter(FY_ATOMIC(uint64_t) *ptr)
{
	uint64_t v;

	v = fy_atomic_load(ptr);
	fy_atomic_fetch_sub(ptr, v);
	return v;
}

#ifdef __cplusplus
}
#endif

#endif	// LIBFYAML_ATOMICS_H
