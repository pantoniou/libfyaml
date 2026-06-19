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

/* MSVC's _mm_pause()/__yield() intrinsics used by fy_cpu_relax() require
 * <intrin.h>; pull it in here so the inline definition compiles cleanly in
 * any translation unit (including C++ TUs that don't otherwise include it). */
#if defined(_MSC_VER)
#include <intrin.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FY_HAVE_STDATOMIC_H - Defined when ``<stdatomic.h>`` is available.
 *
 * When defined, the standard ``atomic_*`` types and functions are in scope
 * and all ``fy_atomic_*`` macros delegate to them.
 *
 * C mode  : STDC version >= 201112L, GCC >= 4.9, Clang >= 3.6, MSVC >= 1928
 * C++ mode: only when the active C++ standard is C++23 or later
 *           (__cplusplus >= 202302L, or MSVC's _MSVC_LANG >= 202302L), since
 *           that is the first standard version to officially mandate
 *           <stdatomic.h>/<atomic> interoperability. Before C++23 this header
 *           uses its own private (fy_priv_atomic_*) fallback implementation
 *           instead, built on GCC/Clang statement-expression and __typeof__
 *           extensions -- both compilers support these even pre-C++23.
 *
 *           Mixing the two pre-C++23 is unsafe in general: some C++ standard
 *           libraries (e.g. recent libc++ on Xcode 16 / macOS 15) now emit a
 *           hard #error if <stdatomic.h> was already included when <atomic>
 *           is processed (which can happen transitively, e.g. via <optional>
 *           or <string_view>, in any C++ translation unit downstream of this
 *           header). Before that hard error existed, this same combination
 *           could instead silently collide on libc++ via unprefixed names
 *           like atomic_load/atomic_store declared by <atomic> -- which is
 *           why this header no longer defines its own fallback macros under
 *           those bare names (see fy_priv_atomic_* below).
 *
 * The check is required only for _really_ old compilers in C mode.
 */
#if !defined(__STDC__NO_ATOMICS__) && \
	((!defined(__cplusplus) && \
	  ((defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L) || \
	   (defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 9))) || \
	   (defined(__clang__) && (__clang_major__ > 3 || (__clang_major__ == 3 && __clang_minor__ >= 6))) || \
	   (defined(_MSC_VER) && _MSC_VER >= 1928))) || \
	 (defined(__cplusplus) && \
	  ((__cplusplus >= 202302L) || \
	   (defined(_MSC_VER) && _MSC_VER >= 1938 && \
	    defined(_MSVC_LANG) && _MSVC_LANG >= 202302L))))
#define FY_HAVE_STDATOMIC_H
#endif

/* Detect standard C11 atomics */
#ifdef FY_HAVE_STDATOMIC_H
/* <stdatomic.h> may contain templates (MSVC C++23) or other C++-incompatible
 * constructs when included inside an extern "C" linkage scope.  Use
 * ``extern "C++"`` to escape *any* depth of enclosing extern "C" blocks --
 * not just the one this header opens at the top, but also any opened by an
 * umbrella header that included us (e.g. libfyaml.h wraps all sub-headers in
 * its own extern "C"). When the brace closes, the enclosing C-linkage scope
 * is automatically restored.
 *
 * Function declarations inside <stdatomic.h> (e.g. atomic_thread_fence) get
 * C++ linkage in this block, but they are overshadowed by macros in the same
 * header on every supported compiler, so no symbol is ever referenced. */
#ifdef __cplusplus
extern "C++" {
#endif
#include <stdatomic.h>
#ifdef __cplusplus
} /* close extern "C++" */
#endif
/*
 * FY_HAVE_C11_ATOMICS - Defined when the ``_Atomic`` qualifier is usable.
 *
 * Set when C11 ``<stdatomic.h>`` is available, or when the compiler supports
 * ``_Atomic`` as an extension (GCC without ``-std=c11``, Clang).
 * When not defined, ``_Atomic(_x)`` is a no-op and operations are non-atomic.
 *
 * We unconditionally enable it if we have stdatomic.h
 */
#define FY_HAVE_C11_ATOMICS
#elif defined(__cplusplus) && defined(__clang__) && \
	(__clang_major__ > 3 || (__clang_major__ == 3 && __clang_minor__ >= 6))
/*
 * Clang supports the ``_Atomic`` qualifier as a native compiler builtin in
 * C++ mode too, independently of whether <stdatomic.h> is included (unlike
 * GCC, which only understands ``_Atomic`` via the macros <stdatomic.h>
 * itself defines). This must stay decoupled from FY_HAVE_STDATOMIC_H: when
 * the latter is unset pre-C++23 (see above), Clang still natively supports
 * ``_Atomic``, and libc++'s own <atomic> implementation relies internally
 * on that exact builtin qualifier. If FY_HAVE_C11_ATOMICS were left unset
 * here, the #else branch below would redefine ``_Atomic(_x)`` as a
 * no-op macro, which corrupts libc++'s internal use of ``_Atomic(_Tp)``
 * the moment <atomic> is included later in the same translation unit.
 */
#define FY_HAVE_C11_ATOMICS
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

/* Alias the real <stdatomic.h> names under our own fy_priv_ prefix so that
 * fy_atomic_*() below can delegate uniformly regardless of backend, without
 * ever introducing macros under the bare (unprefixed) C11 names itself --
 * see the rationale in the #else branches for why that matters. */
#define fy_priv_atomic_flag atomic_flag
#define fy_priv_atomic_load(_ptr) atomic_load((_ptr))
#define fy_priv_atomic_store(_ptr, _v) atomic_store((_ptr), (_v))
#define fy_priv_atomic_exchange(_ptr, _v) atomic_exchange((_ptr), (_v))
#define fy_priv_atomic_compare_exchange_strong(_ptr, _exp, _des) \
	atomic_compare_exchange_strong((_ptr), (_exp), (_des))
#define fy_priv_atomic_compare_exchange_weak(_ptr, _exp, _des) \
	atomic_compare_exchange_weak((_ptr), (_exp), (_des))
#define fy_priv_atomic_fetch_add(_ptr, _v) atomic_fetch_add((_ptr), (_v))
#define fy_priv_atomic_fetch_sub(_ptr, _v) atomic_fetch_sub((_ptr), (_v))
#define fy_priv_atomic_fetch_or(_ptr, _v) atomic_fetch_or((_ptr), (_v))
#define fy_priv_atomic_fetch_xor(_ptr, _v) atomic_fetch_xor((_ptr), (_v))
#define fy_priv_atomic_fetch_and(_ptr, _v) atomic_fetch_and((_ptr), (_v))
#define fy_priv_atomic_flag_clear(_ptr) atomic_flag_clear((_ptr))
#define fy_priv_atomic_flag_set(_ptr) atomic_flag_set((_ptr))
#define fy_priv_atomic_flag_test_and_set(_ptr) atomic_flag_test_and_set((_ptr))

#elif defined(_MSC_VER) && defined(__cplusplus)

/* MSVC C++ fallback (C++17 and earlier, where <stdatomic.h> is unavailable).
 * MSVC does not support GCC statement-expressions ({ }) or __typeof__, so we
 * use decltype and immediately-invoked C++ lambdas for the fetch-and-modify
 * operations.  These are plain (non-atomic) loads/stores; safe only in
 * single-threaded contexts.
 *
 * These are deliberately defined under a fy_priv_ prefix rather than the
 * bare C11 names (atomic_load, atomic_compare_exchange_strong, ...): the
 * bare names collide with unrelated, non-std::-namespaced overloads of the
 * same names that some C++ standard libraries declare for std::shared_ptr
 * (e.g. libstdc++'s <bits/shared_ptr_atomic.h>). Because the preprocessor
 * doesn't parse template angle brackets, a comma inside a template argument
 * list such as __shared_ptr<_Tp, _Lp> is seen as an extra macro argument,
 * silently corrupting those declarations if such a header is included later
 * in the same translation unit. */

#undef FY_HAVE_SAFE_ATOMIC_OPS

typedef bool fy_priv_atomic_flag;

#define fy_priv_atomic_load(_ptr) \
	(*(_ptr))

#define fy_priv_atomic_store(_ptr, _val) \
	do { \
		*(_ptr) = (_val); \
	} while(0)

#define fy_priv_atomic_exchange(_ptr, _v) \
	([&]() -> decltype(*(_ptr)) { decltype(*(_ptr)) __old = *(_ptr); *(_ptr) = (_v); return __old; }())

#define fy_priv_atomic_compare_exchange_strong(_ptr, _exp, _des) \
	([&]() -> bool { \
		bool __res; \
		if (*(_ptr) == *(_exp)) { *(_ptr) = (_des); __res = true; } \
		else __res = false; \
		return __res; \
	}())

#define fy_priv_atomic_compare_exchange_weak(_ptr, _exp, _des) \
	fy_priv_atomic_compare_exchange_strong((_ptr), (_exp), (_des))

#define fy_priv_atomic_fetch_add(_ptr, _v) \
	([&]() -> decltype(*(_ptr)) { decltype(*(_ptr)) __old = *(_ptr); *(_ptr) += (_v); return __old; }())

#define fy_priv_atomic_fetch_sub(_ptr, _v) \
	([&]() -> decltype(*(_ptr)) { decltype(*(_ptr)) __old = *(_ptr); *(_ptr) -= (_v); return __old; }())

#define fy_priv_atomic_fetch_or(_ptr, _v) \
	([&]() -> decltype(*(_ptr)) { decltype(*(_ptr)) __old = *(_ptr); *(_ptr) |= (_v); return __old; }())

#define fy_priv_atomic_fetch_xor(_ptr, _v) \
	([&]() -> decltype(*(_ptr)) { decltype(*(_ptr)) __old = *(_ptr); *(_ptr) ^= (_v); return __old; }())

#define fy_priv_atomic_fetch_and(_ptr, _v) \
	([&]() -> decltype(*(_ptr)) { decltype(*(_ptr)) __old = *(_ptr); *(_ptr) &= (_v); return __old; }())

#define fy_priv_atomic_flag_clear(_ptr) \
	do { \
		*(_ptr) = false; \
	} while(0)

#define fy_priv_atomic_flag_set(_ptr) \
	do { \
		*(_ptr) = true; \
	} while(0)

#define fy_priv_atomic_flag_test_and_set(_ptr) \
	([&]() -> bool { volatile fy_priv_atomic_flag *__p = (_ptr); bool __ret = *__p; *__p = true; return __ret; }())

#elif defined(__cplusplus) && defined(__clang__)

/* Clang C++ pre-C++23 (FY_HAVE_STDATOMIC_H is intentionally unset here, see
 * above, to avoid mixing <stdatomic.h> with <atomic> before C++23). Unlike
 * GCC, Clang still genuinely qualifies FY_ATOMIC()-declared storage as
 * ``_Atomic`` (see FY_HAVE_C11_ATOMICS above), so the naive __typeof__-based
 * fallback used in the last-resort branch below does not apply here: in
 * C++, ``__typeof__(*(_ptr))`` on a real ``_Atomic``-qualified lvalue yields
 * an ``_Atomic``-qualified type too, and C++ (unlike C) has no implicit
 * conversion stripping that qualifier back to a plain type, so e.g.
 * ``int old = fy_atomic_fetch_add(&v, 3);`` fails to compile.
 *
 * Use Clang's ``__c11_atomic_*`` builtins instead: they operate directly on
 * genuinely ``_Atomic``-qualified objects, are real (memory-ordered) atomic
 * operations, and -- being compiler builtins rather than functions declared
 * by a header -- never pull in <stdatomic.h> or collide with anything
 * <atomic> declares. */

#define FY_HAVE_SAFE_ATOMIC_OPS

/* Plain (non-_Atomic-qualified) bool: braced/zero-initializers such as
 * ``fy_priv_atomic_flag flag = {};`` are not valid on an _Atomic-qualified
 * type in C++ (it is not an aggregate there). The flag/clear/set/test_and_set
 * macros below reinterpret the pointer as _Atomic(bool)* only at the point
 * of each __c11_atomic_*() call. */
typedef bool fy_priv_atomic_flag;

#define fy_priv_atomic_load(_ptr) \
	__c11_atomic_load((_ptr), __ATOMIC_SEQ_CST)

#define fy_priv_atomic_store(_ptr, _val) \
	__c11_atomic_store((_ptr), (_val), __ATOMIC_SEQ_CST)

#define fy_priv_atomic_exchange(_ptr, _v) \
	__c11_atomic_exchange((_ptr), (_v), __ATOMIC_SEQ_CST)

#define fy_priv_atomic_compare_exchange_strong(_ptr, _exp, _des) \
	__c11_atomic_compare_exchange_strong((_ptr), (_exp), (_des), __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)

#define fy_priv_atomic_compare_exchange_weak(_ptr, _exp, _des) \
	__c11_atomic_compare_exchange_weak((_ptr), (_exp), (_des), __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)

#define fy_priv_atomic_fetch_add(_ptr, _v) \
	__c11_atomic_fetch_add((_ptr), (_v), __ATOMIC_SEQ_CST)

#define fy_priv_atomic_fetch_sub(_ptr, _v) \
	__c11_atomic_fetch_sub((_ptr), (_v), __ATOMIC_SEQ_CST)

#define fy_priv_atomic_fetch_or(_ptr, _v) \
	__c11_atomic_fetch_or((_ptr), (_v), __ATOMIC_SEQ_CST)

#define fy_priv_atomic_fetch_xor(_ptr, _v) \
	__c11_atomic_fetch_xor((_ptr), (_v), __ATOMIC_SEQ_CST)

#define fy_priv_atomic_fetch_and(_ptr, _v) \
	__c11_atomic_fetch_and((_ptr), (_v), __ATOMIC_SEQ_CST)

#define fy_priv_atomic_flag_clear(_ptr) \
	__c11_atomic_store((_Atomic(bool) *)(_ptr), false, __ATOMIC_SEQ_CST)

#define fy_priv_atomic_flag_set(_ptr) \
	__c11_atomic_store((_Atomic(bool) *)(_ptr), true, __ATOMIC_SEQ_CST)

#define fy_priv_atomic_flag_test_and_set(_ptr) \
	__c11_atomic_exchange((_Atomic(bool) *)(_ptr), true, __ATOMIC_SEQ_CST)

#else

/* Last-resort fallback for toolchains with neither <stdatomic.h> nor GCC
 * statement-expression/__typeof__ support outside of MSVC C++ (e.g. GCC in
 * C++ mode, which intentionally avoids <stdatomic.h> -- see the comment on
 * FY_HAVE_STDATOMIC_H above). These are deliberately defined under a
 * fy_priv_ prefix rather than the bare C11 names; see the rationale in the
 * MSVC C++ branch above -- the same collision with e.g. libstdc++'s
 * <bits/shared_ptr_atomic.h> applies here. */

#undef FY_HAVE_SAFE_ATOMIC_OPS

typedef bool fy_priv_atomic_flag;

#define fy_priv_atomic_load(_ptr) \
	(*(_ptr))

#define fy_priv_atomic_store(_ptr, _val) \
	do { \
		*(_ptr) = (_val); \
	} while(0)

#define fy_priv_atomic_exchange(_ptr, _v) \
	({ \
		__typeof__(_ptr) __ptr = (_ptr); \
		__typeof__(*(_ptr)) __old = *__ptr; \
		*__ptr = (_v); \
		__old; \
	})

#define fy_priv_atomic_compare_exchange_strong(_ptr, _exp, _des) \
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

#define fy_priv_atomic_compare_exchange_weak(_ptr, _exp, _des) \
	fy_priv_atomic_compare_exchange_strong((_ptr), (_exp), (_des))

#define fy_priv_atomic_fetch_add(_ptr, _v) \
	({ \
		__typeof__(_ptr) __ptr = (_ptr); \
		__typeof__(*(_ptr)) __old = *__ptr; \
		*__ptr += (_v); \
		__old; \
	})

#define fy_priv_atomic_fetch_sub(_ptr, _v) \
	({ \
		__typeof__(_ptr) __ptr = (_ptr); \
		__typeof__(*(_ptr)) __old = *__ptr; \
		*__ptr -= (_v); \
		__old; \
	})

#define fy_priv_atomic_fetch_or(_ptr, _v) \
	({ \
		__typeof__(_ptr) __ptr = (_ptr); \
		__typeof__(*(_ptr)) __old = *__ptr; \
		*__ptr |= (_v); \
		__old; \
	})

#define fy_priv_atomic_fetch_xor(_ptr, _v) \
	({ \
		__typeof__(_ptr) __ptr = (_ptr); \
		__typeof__(*(_ptr)) __old = *__ptr; \
		*__ptr ^= (_v); \
		__old; \
	})

#define fy_priv_atomic_fetch_and(_ptr, _v) \
	({ \
		__typeof__(_ptr) __ptr = (_ptr); \
		__typeof__(*(_ptr)) __old = *__ptr; \
		*__ptr &= (_v); \
		__old; \
	})

#define fy_priv_atomic_flag_clear(_ptr) \
	do { \
		*(_ptr) = false; \
	} while(0)

#define fy_priv_atomic_flag_set(_ptr) \
	do { \
		*(_ptr) = true; \
	} while(0)


/* C11 atomic_flag_test_and_set returns the *old* value of the flag,
 * unconditionally setting it to true.  The previous implementation had the
 * return values inverted (returning true on a previously-clear flag), which
 * caused fy_atomic_flag_test_and_set() to misreport state on every compiler
 * that falls back to these macros (e.g. GCC in C++ mode). */
#define fy_priv_atomic_flag_test_and_set(_ptr) \
	({ \
		volatile fy_priv_atomic_flag *__ptr = (_ptr); \
		bool __ret = *__ptr; \
		*__ptr = true; \
		__ret; \
	})

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
#define fy_atomic_flag fy_priv_atomic_flag

/**
 * fy_atomic_load() - Atomically load the value at @_ptr.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified variable.
 *
 * Returns: The current value.
 */
#define fy_atomic_load(_ptr) \
	fy_priv_atomic_load((_ptr))

/**
 * fy_atomic_store() - Atomically store @_v at @_ptr.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified variable.
 * @_v:   Value to store.
 */
#define fy_atomic_store(_ptr, _v) \
	fy_priv_atomic_store((_ptr), (_v))

/**
 * fy_atomic_exchange() - Atomically replace the value at @_ptr with @_v.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified variable.
 * @_v:   New value to store.
 *
 * Returns: The old value that was at @_ptr before the exchange.
 */
#define fy_atomic_exchange(_ptr, _v) \
	fy_priv_atomic_exchange((_ptr), (_v))

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
	fy_priv_atomic_compare_exchange_strong((_ptr), (_e), (_d))

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
	fy_priv_atomic_compare_exchange_weak((_ptr), (_e), (_d))

/**
 * fy_atomic_fetch_add() - Atomically add @_v to @_ptr and return the old value.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified integer variable.
 * @_v:   Value to add.
 *
 * Returns: The value of @_ptr before the addition.
 */
#define fy_atomic_fetch_add(_ptr, _v) \
	fy_priv_atomic_fetch_add((_ptr), (_v))

/**
 * fy_atomic_fetch_sub() - Atomically subtract @_v from @_ptr and return the old value.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified integer variable.
 * @_v:   Value to subtract.
 *
 * Returns: The value of @_ptr before the subtraction.
 */
#define fy_atomic_fetch_sub(_ptr, _v) \
	fy_priv_atomic_fetch_sub((_ptr), (_v))

/**
 * fy_atomic_fetch_or() - Atomically OR @_v into @_ptr and return the old value.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified integer variable.
 * @_v:   Value to OR in.
 *
 * Returns: The value of @_ptr before the operation.
 */
#define fy_atomic_fetch_or(_ptr, _v) \
	fy_priv_atomic_fetch_or((_ptr), (_v))

/**
 * fy_atomic_fetch_xor() - Atomically XOR @_v into @_ptr and return the old value.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified integer variable.
 * @_v:   Value to XOR in.
 *
 * Returns: The value of @_ptr before the operation.
 */
#define fy_atomic_fetch_xor(_ptr, _v) \
	fy_priv_atomic_fetch_xor((_ptr), (_v))

/**
 * fy_atomic_fetch_and() - Atomically AND @_v into @_ptr and return the old value.
 *
 * @_ptr: Pointer to an FY_ATOMIC-qualified integer variable.
 * @_v:   Value to AND in.
 *
 * Returns: The value of @_ptr before the operation.
 */
#define fy_atomic_fetch_and(_ptr, _v) \
	fy_priv_atomic_fetch_and((_ptr), (_v))

/**
 * fy_atomic_flag_clear() - Atomically clear a flag (set to false).
 *
 * @_ptr: Pointer to an fy_atomic_flag.
 */
#define fy_atomic_flag_clear(_ptr) \
	fy_priv_atomic_flag_clear((_ptr))

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
	fy_priv_atomic_flag_set((_ptr))

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
	fy_priv_atomic_flag_test_and_set((_ptr))

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
#elif defined(_MSC_VER) && (defined(_M_ARM64) || defined(_M_ARM))
	__yield();
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
