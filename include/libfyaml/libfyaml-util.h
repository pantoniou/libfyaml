/*
 * libfyaml-util.h - Various utilities for libfyaml
 *
 * Copyright (c) 2019-2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * NOTE: This file contains internals that are not part of the public API.
 * Things may change at any time. Use at your own peril.
 */

#ifndef LIBFYAML_UTIL_H
#define LIBFYAML_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <float.h>

#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
#include <unistd.h>
#include <sys/uio.h>
#elif defined(_WIN32)
/* Windows compatibility definitions */
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#ifdef _WIN64
typedef __int64 ssize_t;
#else
typedef int ssize_t;
#endif
#endif
/* Windows doesn't have sys/uio.h, provide iovec definition */
#ifndef _FY_IOVEC_DEFINED
#define _FY_IOVEC_DEFINED
struct iovec {
	void *iov_base;
	size_t iov_len;
};
#endif
#else
#include <sys/uio.h>
#endif

/**
 * DOC: General-purpose utility macros and portability helpers
 *
 * This header provides the low-level building blocks shared by all other
 * libfyaml public headers.  It has no libfyaml dependencies of its own and
 * is safe to include in isolation.
 *
 * **Platform portability**
 *
 * - ``ssize_t`` shim for MSVC / Windows toolchains
 * - ``struct iovec`` shim for Windows (``sys/uio.h`` substitute)
 * - ``FY_EXPORT`` / ``FY_DEPRECATED`` / ``FY_FORMAT`` — symbol visibility
 *   and compiler-attribute portability macros
 * - ``FY_UNUSED``, ``FY_ALWAYS_INLINE``, ``FY_DEBUG_UNUSED`` — compiler hint
 *   wrappers with safe fallbacks on non-GCC/Clang toolchains
 * - ``FY_CONSTRUCTOR`` / ``FY_DESTRUCTOR`` — GCC-style constructor/destructor
 *   attributes with availability guards (``FY_HAS_CONSTRUCTOR``,
 *   ``FY_HAS_DESTRUCTOR``)
 *
 * **Core constants and sentinel values**
 *
 * - ``FY_BIT(x)``   — single-bit mask (``1U << x``)
 * - ``FY_NT``       — sentinel meaning "null-terminated"; pass as a length
 *   argument to indicate that the string length should be inferred via strlen
 *
 * **Memory helpers**
 *
 * - ``FY_ALLOCA_COPY_FREE`` / ``FY_ALLOCA_COPY_FREE_NO_NULL`` — copy a
 *   malloc-returned string onto the stack and immediately free the heap
 *   allocation; produces an alloca-lifetime copy in a single expression
 * - ``fy_vsprintfa`` / ``fy_sprintfa`` — printf-style formatting into a
 *   stack-allocated (alloca) buffer; the result is valid for the lifetime
 *   of the enclosing function
 *
 * **Container and array helpers**
 *
 * - ``container_of`` — recover a pointer to an enclosing struct from a
 *   pointer to one of its members
 * - ``ARRAY_SIZE``   — number of elements in a stack-allocated array
 *
 * **Type safety and compile-time checks**
 *
 * - ``FY_SAME_TYPE`` / ``FY_CHECK_SAME_TYPE`` — assert two expressions share
 *   the same C type (wraps ``__builtin_types_compatible_p`` with a fallback)
 * - ``FY_COMPILE_ERROR_ON_ZERO`` — produce a compile-time error when the
 *   argument evaluates to zero; used for macro-level static assertions
 *
 * **Overflow-safe arithmetic**
 *
 * - ``FY_ADD_OVERFLOW``, ``FY_SUB_OVERFLOW``, ``FY_MUL_OVERFLOW`` — wrappers
 *   around ``__builtin_*_overflow`` with portable fallback implementations
 *
 * **Miscellaneous**
 *
 * - ``FY_IMPOSSIBLE_ABORT`` — mark unreachable code paths; aborts at runtime
 * - ``FY_STACK_SAVE`` / ``FY_STACK_RESTORE`` — save and restore the stack
 *   pointer for alloca-based temporary arenas
 * - Lambda / closure macros and a CPP metaprogramming framework for
 *   building variadic generic APIs
 * - Diagnostic helpers and floating-point precision constants
 */

/* make things fail easy on non GCC/clang */
#ifndef __has_builtin
/**
 * __has_builtin() - Fallback for compilers that do not provide __has_builtin.
 *
 * Always evaluates to 0 (false) so that all ``#if __has_builtin(...)`` guards
 * safely fall back to the portable implementation.
 *
 * @x: The builtin name to query.
 *
 * Returns: 0.
 */
#define __has_builtin(x) 0
#endif

/**
 * FY_BIT() - Produce an unsigned bitmask with bit @x set.
 *
 * @x: Zero-based bit position (0–31 for a 32-bit result).
 *
 * Returns: ``1U << x``.
 */
#ifndef FY_BIT
#define FY_BIT(x) (1U << (x))
#endif

/**
 * FY_NT - Sentinel value meaning "null-terminated; compute length at runtime".
 *
 * Pass as the @len argument to any libfyaml function that accepts a
 * ``(const char *str, size_t len)`` pair to indicate that @str is a
 * NUL-terminated C string whose length should be determined with ``strlen()``.
 *
 * Value: ``(size_t)-1``
 */
/* NULL terminated string length specifier */
#define FY_NT	((size_t)-1)

#if defined(__GNUC__) && __GNUC__ >= 4
/**
 * FY_EXPORT - Mark a symbol as part of the shared-library public ABI.
 *
 * On GCC/Clang (version >= 4) expands to ``__attribute__((visibility("default")))``,
 * overriding ``-fvisibility=hidden`` for the annotated symbol.
 * On other compilers expands to nothing (all symbols are visible by default).
 */
#define FY_EXPORT __attribute__ ((visibility ("default")))
/**
 * FY_DEPRECATED - Mark a function or variable as deprecated.
 *
 * On GCC/Clang expands to ``__attribute__((deprecated))``, causing a
 * compile-time warning whenever the annotated symbol is used.
 * On other compilers expands to nothing.
 */
#define FY_DEPRECATED __attribute__ ((deprecated))
/**
 * FY_FORMAT() - Annotate a function with printf-style format checking.
 *
 * On GCC/Clang expands to ``__attribute__((format(_t, _x, _y)))``, enabling
 * the compiler to type-check the format string and variadic arguments.
 *
 * @_t: Format type token (e.g. ``printf``, ``scanf``, ``strftime``).
 * @_x: 1-based index of the format-string parameter.
 * @_y: 1-based index of the first variadic argument (0 for ``va_list`` wrappers).
 */
#define FY_FORMAT(_t, _x, _y) __attribute__ ((format(_t, _x, _y)))
#else
#define FY_EXPORT /* nothing */
#define FY_DEPRECATED /* nothing */
#define FY_FORMAT(_t, x, y)	/* nothing */
#endif

/**
 * FY_ALLOCA_COPY_FREE() - Copy a heap string onto the stack and free the original.
 *
 * Expands to a statement expression that:
 * 1. Copies @_str (up to @_len bytes, or the full NUL-terminated length when
 *    @_len is %FY_NT) into a NUL-terminated stack buffer via ``alloca()``.
 * 2. Calls ``free(@_str)`` to release the heap allocation.
 * 3. Evaluates to a ``const char *`` pointing to the stack copy.
 *
 * When @_str is NULL the macro evaluates to NULL and performs no copy or free.
 *
 * The stack buffer is valid only for the lifetime of the enclosing function.
 * Do not call this in a loop — each invocation grows the stack frame.
 *
 * @_str: Heap-allocated string to copy and free (may be NULL).
 * @_len: Length in bytes, or %FY_NT to use ``strlen()``.
 *
 * Returns: ``const char *`` to the stack copy, or NULL if @_str was NULL.
 */
#ifndef FY_ALLOCA_COPY_FREE
#define FY_ALLOCA_COPY_FREE(_str, _len)				\
        ({							\
                char *__str = (_str), *__stra = NULL;		\
                size_t __len = (size_t)(_len);			\
								\
		if (__str) {					\
			if (__len == FY_NT) 			\
				__len = strlen(__str);		\
			__stra = alloca(__len + 1);		\
			memcpy(__stra, __str, __len);		\
			__stra[__len] = '\0';			\
			free(__str);				\
		}						\
                (const char *)__stra;				\
        })
#endif

/**
 * FY_ALLOCA_COPY_FREE_NO_NULL() - Like FY_ALLOCA_COPY_FREE() but returns "" for NULL.
 *
 * Identical to %FY_ALLOCA_COPY_FREE but substitutes an empty string literal
 * ``""`` when @_str is NULL, so callers never receive a NULL pointer.
 *
 * @_str: Heap-allocated string to copy and free (may be NULL).
 * @_len: Length in bytes, or %FY_NT to use ``strlen()``.
 *
 * Returns: ``const char *`` to the stack copy, or ``""`` if @_str was NULL.
 */
#ifndef FY_ALLOCA_COPY_FREE_NO_NULL
#define FY_ALLOCA_COPY_FREE_NO_NULL(_str, _len)			\
        ({							\
                const char *__strb;				\
								\
		__strb = FY_ALLOCA_COPY_FREE(_str, _len);	\
		if (!__strb)					\
			__strb = "";				\
		__strb;						\
        })
#endif

/**
 * container_of() - Recover a pointer to a containing struct from a member pointer.
 *
 * Given a pointer @ptr to a field named @member inside a struct of type @type,
 * returns a pointer to the enclosing @type instance.
 *
 * Uses ``__typeof__`` to catch type mismatches at compile time (GCC/Clang).
 *
 * @ptr:    Pointer to the member field.
 * @type:   Type of the containing struct.
 * @member: Name of the member field within @type.
 *
 * Returns: Pointer to the containing struct of type @type.
 */
#ifndef container_of
#define container_of(ptr, type, member) \
	({ \
		const __typeof__(((type *)0)->member) *__mptr = (ptr); \
		(type *)((void *)__mptr - offsetof(type,member)); \
	 })
#endif

/**
 * ARRAY_SIZE() - Compute the number of elements in a stack-allocated array.
 *
 * Evaluates to a compile-time constant. Only valid for arrays with a known
 * size at compile time (not pointers or VLAs).
 *
 * @x: The array expression.
 *
 * Returns: Number of elements, as a ``size_t``.
 */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) ((sizeof(x)/sizeof((x)[0])))
#endif

/**
 * FY_UNUSED - Suppress "unused variable/parameter" warnings.
 *
 * On GCC/Clang (version >= 4) expands to ``__attribute__((unused))``.
 * On other compilers expands to nothing.
 *
 * Use on parameters or local variables that are intentionally unreferenced,
 * e.g. in debug-only code paths.
 */
#if defined(__GNUC__) && __GNUC__ >= 4
#define FY_UNUSED __attribute__((unused))
#else
#define FY_UNUSED /* nothing */
#endif

#if defined(__GNUC__) && __GNUC__ >= 4
/**
 * FY_CONSTRUCTOR - Run a function automatically before main().
 *
 * On GCC/Clang expands to ``__attribute__((constructor))``. Also defines
 * %FY_HAS_CONSTRUCTOR so callers can detect support at compile time.
 * On other compilers expands to nothing and %FY_HAS_CONSTRUCTOR is not defined.
 */
#define FY_CONSTRUCTOR __attribute__((constructor))
/**
 * FY_DESTRUCTOR - Run a function automatically after main() (or on exit()).
 *
 * On GCC/Clang expands to ``__attribute__((destructor))``. Also defines
 * %FY_HAS_DESTRUCTOR so callers can detect support at compile time.
 * On other compilers expands to nothing and %FY_HAS_DESTRUCTOR is not defined.
 */
#define FY_DESTRUCTOR __attribute__((destructor))
#define FY_HAS_CONSTRUCTOR
#define FY_HAS_DESTRUCTOR
#else
#define FY_CONSTRUCTOR /* nothing */
#define FY_DESTRUCTOR /* nothing */
#endif

/*
 * FY_ALWAYS_INLINE - Force inlining of a function regardless of optimisation level.
 *
 * Expands to __attribute__((always_inline)) on GCC/Clang when NDEBUG
 * is defined (i.e. in release builds). In debug builds expands to nothing so
 * that functions remain visible to the debugger.
 */
#if defined(NDEBUG) && (defined(__GNUC__) && __GNUC__ >= 4)
#define FY_ALWAYS_INLINE __attribute__((always_inline))
#else
#define FY_ALWAYS_INLINE /* nothing */
#endif

/**
 * FY_DEBUG_UNUSED - Mark a variable as potentially unused in non-debug builds.
 *
 * Expands to ``__attribute__((unused))`` on GCC/Clang when ``NDEBUG`` is
 * defined, silencing warnings for variables that are only referenced inside
 * ``assert()`` calls (which disappear in release builds).
 * In debug builds (``NDEBUG`` not set) expands to nothing.
 */
#if defined(NDEBUG) && defined(__GNUC__) && __GNUC__ >= 4
#define FY_DEBUG_UNUSED __attribute__((unused))
#else
#define FY_DEBUG_UNUSED /* nothing */
#endif

/*
 * FY_DESTRUCTOR_SHOW_LEFTOVERS - Defined when destructor-based leak reporting is active.
 *
 * Defined when both %FY_DESTRUCTOR (constructor/destructor attribute support)
 * and ``NDEBUG`` are in effect. Internal subsystems use this flag to register
 * ``FY_DESTRUCTOR`` functions that log any objects still alive at process exit,
 * providing a lightweight leak summary in release builds.
 */
#if defined(FY_DESTRUCTOR) && defined(NDEBUG)
#define FY_DESTRUCTOR_SHOW_LEFTOVERS
#endif

/**
 * FY_IMPOSSIBLE_ABORT() - Assert that an unreachable code path has been reached.
 *
 * Calls ``assert(0)`` followed by ``abort()`` to terminate the process
 * immediately. Use to mark code paths that must never execute in a correct
 * program, such as the default branch of a switch that covers all enum values.
 *
 * The double invocation ensures termination even when assertions are disabled
 * (``NDEBUG``).
 */
/* something impossible is happening, assert/abort */
#define FY_IMPOSSIBLE_ABORT() \
	do { \
		assert(0); \
		abort(); \
	} while(0)

/**
 * FY_COMPILE_ERROR_ON_ZERO() - Trigger a compile error if expression @_e is zero.
 *
 * Evaluates @_e at compile time. If @_e is zero, ``sizeof(char[-1])`` is
 * ill-formed and the build fails. If @_e is non-zero, the expression is a
 * no-op void cast.
 *
 * Prefer %FY_CHECK_SAME_TYPE or ``static_assert`` for clearer error messages;
 * use this primitive when a compile-time boolean is needed in a macro context.
 *
 * @_e: Compile-time expression that must be non-zero.
 */
/* if expression is zero, then the build will break */
#define FY_COMPILE_ERROR_ON_ZERO(_e) ((void)(sizeof(char[1 - 2*!!(_e)])))

/**
 * FY_SAME_TYPE() - Test whether two expressions have the same type.
 *
 * On GCC/Clang uses ``__builtin_types_compatible_p`` to compare the types of
 * @_a and @_b (via ``__typeof__``). On compilers without this builtin always
 * evaluates to true to avoid false negatives.
 *
 * @_a: First expression.
 * @_b: Second expression.
 *
 * Returns: Non-zero (true) if the types match, zero (false) otherwise.
 */
/* true, if types are the same, false otherwise (depends on builtin_types_compatible_p) */
#if __has_builtin(__builtin_types_compatible_p)
#define FY_SAME_TYPE(_a, _b) __builtin_types_compatible_p(__typeof__(_a), __typeof__(_b))
#else
#define FY_SAME_TYPE(_a, _b) true
#endif

/**
 * FY_CHECK_SAME_TYPE() - Trigger a compile error if two expressions have different types.
 *
 * Combines %FY_SAME_TYPE and %FY_COMPILE_ERROR_ON_ZERO. Use in macros that
 * require two operands to have the same type (e.g. overflow-safe arithmetic).
 *
 * @_a: First expression.
 * @_b: Second expression.
 */
/* compile error if types are not the same */
#define FY_CHECK_SAME_TYPE(_a, _b) \
    FY_COMPILE_ERROR_ON_ZERO(!FY_SAME_TYPE(_a, _b))

/**
 * FY_ADD_OVERFLOW() - Checked addition: detect signed/unsigned integer overflow.
 *
 * On GCC/Clang maps directly to ``__builtin_add_overflow(_a, _b, _resp)``,
 * which is a single compiler intrinsic with full type generality.
 *
 * On other compilers a portable fallback is used that:
 * - Requires @_a and @_b to have the same type (enforced at compile time via
 *   %FY_CHECK_SAME_TYPE).
 * - Computes ``*_resp = _a + _b`` and returns true if the addition wrapped.
 *
 * @_a:    First operand.
 * @_b:    Second operand (must have the same type as @_a).
 * @_resp: Pointer to receive the result (written even on overflow).
 *
 * Returns: true if the addition overflowed, false otherwise.
 */
/* type safe add overflow */
#if __has_builtin(__builtin_add_overflow)
#define FY_ADD_OVERFLOW(_a, _b, _resp) __builtin_add_overflow(_a, _b, _resp)
#else
#define FY_ADD_OVERFLOW(_a, _b, _resp) \
({ \
	__typeof__(_a) __a = (_a), __res; \
	__typeof__(_b) __b = (_b); \
	bool __overflow; \
	\
	FY_CHECK_SAME_TYPE(__a, __b); \
	\
	__res = __a + __b; \
	/* overflow when signs of a, b same, but results different */ \
	__overflow = ((__a ^ __result) & (__b & __result)) < 0; \
	*(_resp) = __res; \
	__overflow; \
})
#endif

/**
 * FY_SUB_OVERFLOW() - Checked subtraction: detect signed/unsigned integer overflow.
 *
 * On GCC/Clang maps to ``__builtin_sub_overflow(_a, _b, _resp)``.
 * The portable fallback requires @_a and @_b to have the same type.
 *
 * @_a:    Minuend.
 * @_b:    Subtrahend (must have the same type as @_a).
 * @_resp: Pointer to receive the result (written even on overflow).
 *
 * Returns: true if the subtraction overflowed, false otherwise.
 */
/* type safe sub overflow */
#if __has_builtin(__builtin_sub_overflow)
#define FY_SUB_OVERFLOW(_a, _b, _resp) __builtin_sub_overflow(_a, _b, _resp)
#else
#define FY_SUB_OVERFLOW(_a, _b, _resp) \
({ \
	__typeof__(_a) __a = (_a), __res; \
	__typeof__(_b) __b = (_b); \
	bool __overflow; \
	\
	FY_CHECK_SAME_TYPE(__a, __b); \
	\
	__res = __a - __b; \
	/* overflow when signs of a, b differ, but results different from minuend */ \
	__overflow = ((__a ^ __b) & (__a & __result)) < 0; \
	*(_resp) = __res; \
	__overflow; \
})
#endif

/**
 * FY_MUL_OVERFLOW() - Checked multiplication: detect signed/unsigned integer overflow.
 *
 * On GCC/Clang maps to ``__builtin_mul_overflow(_a, _b, _resp)``.
 * The portable fallback requires @_a and @_b to have the same type and detects
 * overflow by dividing the product back and comparing with the original operand.
 *
 * @_a:    First factor.
 * @_b:    Second factor (must have the same type as @_a).
 * @_resp: Pointer to receive the result (written even on overflow).
 *
 * Returns: true if the multiplication overflowed, false otherwise.
 */
/* type safe multiply overflow */
#if __has_builtin(__builtin_mul_overflow)
#define FY_MUL_OVERFLOW(_a, _b, _resp) __builtin_mul_overflow(_a, _b, _resp)
#else
#define FY_MUL_OVERFLOW(_a, _b, _resp) \
({ \
	__typeof__(_a) __a = (_a), __res; \
	__typeof__(_b) __b = (_b); \
	bool __overflow; \
	\
	FY_CHECK_SAME_TYPE(__a, __b); \
	\
	if (!__a || !__b) { \
	    __overflow = false; \
	    __res = 0; \
	} else { \
	    __res = __a * __b; \
	    /* overflow when division of the result differs */ \
	    __overflow = (__res / __a) != __b; \
	} \
	*(_resp) = __res; \
	__overflow; \
})
#endif

/**
 * fy_vsprintfa() - Format a string into a stack buffer using a va_list.
 *
 * Expands to a statement expression that:
 * 1. Calls ``vsnprintf(NULL, 0, ...)`` to compute the required buffer size.
 * 2. Allocates that many bytes + 1 on the stack via ``alloca()``.
 * 3. Calls ``vsnprintf()`` again to fill the buffer.
 * 4. Evaluates to a ``char *`` pointing to the NUL-terminated result.
 *
 * The returned pointer is valid only for the lifetime of the enclosing function.
 * @_ap is consumed; the caller must not use it after this macro.
 *
 * @_fmt: printf-style format string.
 * @_ap:  Initialised ``va_list``; will be advanced past all arguments.
 *
 * Returns: ``char *`` to a NUL-terminated stack buffer containing the formatted
 *          string.
 */
/* alloca formatted print methods */
#define fy_vsprintfa(_fmt, _ap) \
	({ \
		const char *__fmt = (_fmt); \
		va_list _ap_orig; \
		int _size; \
		char *_buf; \
		\
		va_copy(_ap_orig, (_ap)); \
		_size = vsnprintf(NULL, 0, __fmt, _ap_orig); \
		va_end(_ap_orig); \
		_buf = alloca(_size + 1); \
		(void)vsnprintf(_buf, _size + 1, __fmt, (_ap)); \
		_buf; \
	})

/**
 * fy_sprintfa() - Format a string into a stack buffer using inline arguments.
 *
 * Like fy_vsprintfa() but accepts arguments directly (uses ``__VA_ARGS__``).
 * Two calls to ``snprintf()`` are made: the first with a NULL buffer to
 * measure the required length, the second to write the result into a
 * stack-allocated buffer.
 *
 * The returned pointer is valid only for the lifetime of the enclosing function.
 *
 * @_fmt: printf-style format string.
 * @...:  Format arguments.
 *
 * Returns: ``char *`` to a NUL-terminated stack buffer.
 */
#define fy_sprintfa(_fmt, ...) \
	({ \
		const char *__fmt = (_fmt); \
		int _size; \
		char *_buf; \
		\
		_size = snprintf(NULL, 0, __fmt, ## __VA_ARGS__); \
		_buf = alloca(_size + 1); \
		(void)snprintf(_buf, _size + 1, __fmt, __VA_ARGS__); \
		_buf; \
	})

/**
 * DOC: C preprocessor metaprogramming framework
 *
 * This section implements a portable ``FY_CPP_*`` macro toolkit that enables
 * applying an operation to every argument in a ``__VA_ARGS__`` list — the
 * equivalent of a compile-time ``map()`` function.
 *
 * **The core challenge**: The C preprocessor does not support recursion. A
 * macro cannot call itself. The standard workaround is *deferred evaluation*:
 * a macro is "postponed" so that it is not expanded in the current scan pass,
 * only in a future one.  Multiple passes are forced by the ``FY_CPP_EVALn``
 * ladder which re-expands the token stream a power-of-two number of times.
 *
 * **Evaluation levels** (``FY_CPP_EVAL1`` .. ``FY_CPP_EVAL``):
 *
 * - ``FY_CPP_EVAL1``  — one pass (no re-expansion)
 * - ``FY_CPP_EVAL2``  — 2 passes
 * - ``FY_CPP_EVAL4``  — 4 passes
 * - ``FY_CPP_EVAL8``  — 8 passes
 * - ``FY_CPP_EVAL16`` — 16 passes (GCC only; Clang craps out at 8)
 * - ``FY_CPP_EVAL``   — full depth (32 passes on GCC, 16 on Clang)
 *
 * Each doubling allows mapping over twice as many arguments.  ``FY_CPP_EVAL``
 * supports lists of up to ~32 elements (GCC) or ~16 elements (Clang) in practice.
 *
 * **Argument accessors** — extract positional arguments from ``__VA_ARGS__``:
 *
 * - ``FY_CPP_FIRST(...)``  — first argument (0 if list is empty)
 * - ``FY_CPP_SECOND(...)`` — second argument
 * - ``FY_CPP_THIRD(...)``  — third argument
 * - ``FY_CPP_FOURTH(...)`` — fourth argument
 * - ``FY_CPP_FIFTH(...)``  — fifth argument
 * - ``FY_CPP_SIXTH(...)``  — sixth argument
 * - ``FY_CPP_REST(...)``   — all arguments after the first (empty if < 2)
 *
 * **Map operations**:
 *
 * - ``FY_CPP_MAP(macro, ...)`` — expand ``macro(x)`` for each argument @x.
 * - ``FY_CPP_MAP2(a, macro, ...)`` — expand ``macro(a, x)`` for each @x,
 *   threading a fixed first argument @a through every call.
 *
 * **Utility**:
 *
 * - ``FY_CPP_VA_COUNT(...)``         — number of arguments (integer expression).
 * - ``FY_CPP_VA_ITEMS(_type, ...)``  — compound-literal array of @_type from varargs.
 * - ``FY_CPP_EMPTY()``               — deferred empty token (used to postpone macros).
 * - ``FY_CPP_POSTPONE1(macro)``      — postpone a one-argument macro one pass.
 * - ``FY_CPP_POSTPONE2(a, macro)``   — postpone a two-argument macro one pass.
 *
 * **Short-name mode** (``FY_CPP_SHORT_NAMES``):
 *
 * By default the implementation uses abbreviated internal names (``_E1``,
 * ``_FM``, etc.) to reduce token-expansion buffer pressure. Define
 * ``FY_CPP_SHORT_NAMES_DEBUG`` before including this header to force the
 * original long-name implementation (``FY_CPP_EVAL1``, ``_FY_CPP_MAP_ONE``,
 * etc.) which is easier to read in expansion traces.
 *
 * **Example**::
 *
 *   // Count and collect variadic integer arguments into a C array:
 *   int do_sum(int count, int *items) { ... }
 *
 *   #define do_sum_macro(...) \
 *       do_sum(FY_CPP_VA_COUNT(__VA_ARGS__), \
 *              FY_CPP_VA_ITEMS(int, __VA_ARGS__))
 *
 *   // do_sum_macro(1, 2, 5, 100) expands to:
 *   // do_sum(4, ((int [4]){ 1, 2, 5, 100 }))
 */

// C preprocessor magic follows (pilfered and adapted from h4x0r.org)

/* applies an expansion over a varargs list */

/*
 * Define FY_CPP_SHORT_NAMES to use shortened macro names in the implementation.
 * This significantly reduces expansion buffer usage during recursive macro expansion,
 * which can help avoid internal compiler errors and speed up compilation.
 *
 * The public API (FY_CPP_*) remains the same regardless of this setting.
 */

#ifndef FY_CPP_SHORT_NAMES_DEBUG
#define FY_CPP_SHORT_NAMES
#endif

#ifdef FY_CPP_SHORT_NAMES

/* Short-name implementation - reduces expansion buffer usage by ~60% */

#define _E1(...)	__VA_ARGS__
#define _E2(...)	_E1(_E1(__VA_ARGS__))
#define _E4(...)	_E2(_E2(__VA_ARGS__))
#define _E8(...)	_E4(_E4(__VA_ARGS__))
#define _E16(...)	_E8(_E8(__VA_ARGS__))
#define _E32(...)	_E16(_E16(__VA_ARGS__))
#define _E(...)		_E32(_E32(__VA_ARGS__))

#define _FMT()
#define _FP1(m) m _FMT()
#define _FP2(a, m) m _FMT()

#define _F1(_1, ...)		_1
#define _F1F(...)			_F1(__VA_OPT__(__VA_ARGS__ ,) 0)

#define _F2(_1, _2, ...)	_2
#define _F2F(...)			_F2(__VA_OPT__(__VA_ARGS__ ,) 0, 0)

#define _F3(_1, _2, _3, ...)		_3
#define _F3F(...)			_F3(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0)

#define _F4(_1, _2, _3, _4, ...)	_4
#define _F4F(...)			_F4(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0, 0)

#define _F5(_1, _2, _3, _4, _5, ...)	_5
#define _F5F(...) \
	_F5(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0, 0, 0)

#define _F6(_1, _2, _3, _4, _5, _6, ...)	_6
#define _F6F(...) \
	_F6(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0, 0, 0, 0)

#define _FR(x, ...) __VA_ARGS__
#define _FRF(...)     __VA_OPT__(_FR(__VA_ARGS__))

#define _FM(m, ...) \
    __VA_OPT__(_E(_FM1(m, __VA_ARGS__)))
#define _FM1(m, x, ...) m(x) \
    __VA_OPT__(_FP1(_FMI)()(m, __VA_ARGS__))
#define _FMI() _FM1

#define _FCB(x) +1
#define _FVC(...)  (__VA_OPT__((_FM(_FCB, __VA_ARGS__) + 0)) + 0)

#define _FI1(a) a
#define _FIL(a) , _FI1(a)
#define _FILS(...) _FM(_FIL, __VA_ARGS__)

#define _FVIS(...) \
    _FI1(_F1F(__VA_ARGS__)) \
    _FILS(_FRF(__VA_ARGS__))

#define _FVISF(_t, ...) \
	((_t [_FVC(__VA_ARGS__)]) { _FVIS(__VA_ARGS__) })

#define _FM2(a, m, ...) \
    __VA_OPT__(_E(_FM12(a, m, __VA_ARGS__)))

#define _FM12(a, m, x, ...) m(a, x) \
    __VA_OPT__(_FP2(a, _FMI2)()(a, m, __VA_ARGS__))
#define _FMI2() _FM12

/* Public API - maps to short names */

/**
 * FY_CPP_EVAL1() - Force one additional macro-expansion pass.
 *
 * Expands its argument list once. Use as a building block for deeper
 * evaluation levels; prefer FY_CPP_EVAL() for most uses.
 *
 * @...: Token sequence to re-expand.
 */
#define FY_CPP_EVAL1(...)	_E1(__VA_ARGS__)
/**
 * FY_CPP_EVAL2() - Force two additional macro-expansion passes.
 * @...: Token sequence to re-expand.
 */
#define FY_CPP_EVAL2(...)	_E2(__VA_ARGS__)
/**
 * FY_CPP_EVAL4() - Force four additional macro-expansion passes.
 * @...: Token sequence to re-expand.
 */
#define FY_CPP_EVAL4(...)	_E4(__VA_ARGS__)
/**
 * FY_CPP_EVAL8() - Force eight additional macro-expansion passes.
 * @...: Token sequence to re-expand.
 */
#define FY_CPP_EVAL8(...)	_E8(__VA_ARGS__)
#if !defined(__clang__)
/**
 * FY_CPP_EVAL16() - Force 16 additional macro-expansion passes (GCC only).
 *
 * Not defined on Clang, which exhausts its expansion buffer before reaching
 * 16 levels. Wrap FY_CPP_MAP() / FY_CPP_MAP2() in FY_CPP_EVAL() instead of
 * calling this directly.
 *
 * @...: Token sequence to re-expand.
 */
#define FY_CPP_EVAL16(...)	_E16(__VA_ARGS__)
#endif
/**
 * FY_CPP_EVAL() - Force maximum macro-expansion depth.
 *
 * On GCC performs 32 expansion passes (supports lists up to ~32 elements).
 * On Clang performs 16 passes (supports lists up to ~16 elements).
 *
 * Always use this (rather than a specific EVALn) unless you have a known
 * bound on the number of arguments.
 *
 * @...: Token sequence to fully expand.
 */
#define FY_CPP_EVAL(...)	_E(__VA_ARGS__)

/**
 * FY_CPP_EMPTY() - Produce an empty token sequence (deferred).
 *
 * Expands to nothing. Used in combination with FY_CPP_POSTPONE1() /
 * FY_CPP_POSTPONE2() to defer macro expansion by one scan pass, breaking
 * the preprocessor's blue-paint recursion guard.
 */
#define FY_CPP_EMPTY()		_FMT()
/**
 * FY_CPP_POSTPONE1() - Defer a single-argument macro by one expansion pass.
 *
 * Expands to the macro name @macro followed by a deferred FY_CPP_EMPTY(),
 * so the macro call is not completed until the next pass.
 *
 * @macro: Macro token to postpone.
 */
#define FY_CPP_POSTPONE1(macro)	_FP1(macro)
/**
 * FY_CPP_POSTPONE2() - Defer a two-argument macro by one expansion pass.
 *
 * Like FY_CPP_POSTPONE1() but for macros that take a leading fixed argument @a.
 *
 * @a:     Fixed first argument (carried along, not expanded yet).
 * @macro: Macro token to postpone.
 */
#define FY_CPP_POSTPONE2(a, macro)	_FP2(a, macro)

/**
 * FY_CPP_FIRST() - Extract the first argument from a variadic list.
 *
 * Returns the first argument, or 0 if the list is empty.
 *
 * @...: Variadic argument list.
 *
 * Returns: First argument token, or ``0``.
 */
#define FY_CPP_FIRST(...)	_F1F(__VA_ARGS__)
/**
 * FY_CPP_SECOND() - Extract the second argument from a variadic list.
 *
 * Returns the second argument, or 0 if fewer than two arguments are present.
 *
 * @...: Variadic argument list.
 *
 * Returns: Second argument token, or ``0``.
 */
#define FY_CPP_SECOND(...)	_F2F(__VA_ARGS__)
/**
 * FY_CPP_THIRD() - Extract the third argument.
 * @...: Variadic argument list. Returns: Third argument, or ``0``.
 */
#define FY_CPP_THIRD(...)	_F3F(__VA_ARGS__)
/**
 * FY_CPP_FOURTH() - Extract the fourth argument.
 * @...: Variadic argument list. Returns: Fourth argument, or ``0``.
 */
#define FY_CPP_FOURTH(...)	_F4F(__VA_ARGS__)
/**
 * FY_CPP_FIFTH() - Extract the fifth argument.
 * @...: Variadic argument list. Returns: Fifth argument, or ``0``.
 */
#define FY_CPP_FIFTH(...)	_F5F(__VA_ARGS__)
/**
 * FY_CPP_SIXTH() - Extract the sixth argument.
 * @...: Variadic argument list. Returns: Sixth argument, or ``0``.
 */
#define FY_CPP_SIXTH(...)	_F6F(__VA_ARGS__)
/**
 * FY_CPP_REST() - Return all arguments after the first.
 *
 * Expands to the tail of the variadic list with the first element removed.
 * Expands to nothing if the list has zero or one element.
 *
 * @...: Variadic argument list.
 */
#define FY_CPP_REST(...)	_FRF(__VA_ARGS__)

/**
 * FY_CPP_MAP() - Apply a macro to every argument in a variadic list.
 *
 * Expands ``macro(x)`` for each argument @x in ``__VA_ARGS__``, concatenating
 * all the results. The argument list must be non-empty. Uses FY_CPP_EVAL()
 * internally, so the list length is bounded by the evaluation depth.
 *
 * For example::
 *
 *   #define PRINT_ITEM(x)  printf("%d\n", x);
 *   FY_CPP_MAP(PRINT_ITEM, 1, 2, 3)
 *   // expands to: printf("%d\n", 1); printf("%d\n", 2); printf("%d\n", 3);
 *
 * @macro: Single-argument macro to apply.
 * @...:   Arguments to map over.
 */
#define FY_CPP_MAP(macro, ...)	_FM(macro, __VA_ARGS__)
#define _FY_CPP_MAP_ONE		_FM1
#define _FY_CPP_MAP_INDIRECT	_FMI
#define _FY_CPP_COUNT_BODY	_FCB
/**
 * FY_CPP_VA_COUNT() - Count the number of arguments in a variadic list.
 *
 * Evaluates to a compile-time integer expression equal to the number of
 * arguments. Returns 0 for an empty list.
 *
 * @...: Variadic argument list.
 *
 * Returns: Integer expression giving the argument count.
 */
#define FY_CPP_VA_COUNT(...)	_FVC(__VA_ARGS__)
#define _FY_CPP_ITEM_ONE	_FI1
#define _FY_CPP_ITEM_LATER_ARG	_FIL
#define _FY_CPP_ITEM_LIST	_FILS
#define _FY_CPP_VA_ITEMS	_FVIS
/**
 * FY_CPP_VA_ITEMS() - Build a compound-literal array from variadic arguments.
 *
 * Expands to a compound literal of type ``_type[N]`` (where N is the number
 * of arguments) initialised with the provided values. Useful for passing a
 * variadic argument list as an array to a function.
 *
 * For example::
 *
 *   // FY_CPP_VA_ITEMS(int, 1, 2, 5, 100)
 *   // expands to: ((int [4]){ 1, 2, 5, 100 })
 *
 * @_type: Element type of the resulting array.
 * @...:   Values to place in the array.
 */
#define FY_CPP_VA_ITEMS(_type, ...)	_FVISF(_type, __VA_ARGS__)
/**
 * FY_CPP_MAP2() - Apply a binary macro to every argument, threading a fixed first argument.
 *
 * Expands ``macro(a, x)`` for each argument @x in ``__VA_ARGS__``. The fixed
 * argument @a is passed as the first argument to every invocation.
 *
 * @a:     Fixed first argument threaded into every call.
 * @macro: Two-argument macro to apply.
 * @...:   Arguments to map over.
 */
#define FY_CPP_MAP2(a, macro, ...)	_FM2(a, macro, __VA_ARGS__)
#define _FY_CPP_MAP_ONE2	_FM12
#define _FY_CPP_MAP_INDIRECT2	_FMI2

#else /* !FY_CPP_SHORT_NAMES */

/* Original long-name implementation */

#define FY_CPP_EVAL1(...)	__VA_ARGS__
#define FY_CPP_EVAL2(...)	FY_CPP_EVAL1(FY_CPP_EVAL1(__VA_ARGS__))
#define FY_CPP_EVAL4(...)	FY_CPP_EVAL2(FY_CPP_EVAL2(__VA_ARGS__))
#define FY_CPP_EVAL8(...)	FY_CPP_EVAL4(FY_CPP_EVAL4(__VA_ARGS__))
#if !defined(__clang__)
// gcc is better, goes to 16
#define FY_CPP_EVAL16(...)	FY_CPP_EVAL8(FY_CPP_EVAL8(__VA_ARGS__))
#define FY_CPP_EVAL(...)	FY_CPP_EVAL16(FY_CPP_EVAL16(__VA_ARGS__))
#else
// clang craps out at 8
#define FY_CPP_EVAL(...)	FY_CPP_EVAL8(FY_CPP_EVAL8(__VA_ARGS__))
#endif

#define FY_CPP_EMPTY()
#define FY_CPP_POSTPONE1(macro) macro FY_CPP_EMPTY()

#define FY_CPP_MAP(macro, ...) \
    __VA_OPT__(FY_CPP_EVAL(_FY_CPP_MAP_ONE(macro, __VA_ARGS__)))
#define _FY_CPP_MAP_ONE(macro, x, ...) macro(x) \
    __VA_OPT__(FY_CPP_POSTPONE1(_FY_CPP_MAP_INDIRECT)()(macro, __VA_ARGS__))
#define _FY_CPP_MAP_INDIRECT() _FY_CPP_MAP_ONE

#define FY_CPP_POSTPONE2(a, macro) macro FY_CPP_EMPTY()

#define _FY_CPP_FIRST(_1, ...)			_1
#define FY_CPP_FIRST(...)			_FY_CPP_FIRST(__VA_OPT__(__VA_ARGS__ ,) 0)

#define _FY_CPP_SECOND(_1, _2, ...)		_2
#define FY_CPP_SECOND(...)			_FY_CPP_SECOND(__VA_OPT__(__VA_ARGS__ ,) 0, 0)

#define _FY_CPP_THIRD(_1, _2, _3, ...)		_3
#define FY_CPP_THIRD(...)			_FY_CPP_THIRD(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0)

#define _FY_CPP_FOURTH(_1, _2, _3, _4, ...)	_4
#define FY_CPP_FOURTH(...)			_FY_CPP_FOURTH(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0, 0)

#define _FY_CPP_FIFTH(_1, _2, _3, _4, _5, ...)	_5
#define FY_CPP_FIFTH(...) \
	_FY_CPP_FIFTH(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0, 0, 0)

#define _FY_CPP_SIXTH(_1, _2, _3, _4, _5, _6, ...)	_6
#define FY_CPP_SIXTH(...) \
	_FY_CPP_SIXTH(__VA_OPT__(__VA_ARGS__ ,) 0, 0, 0, 0, 0, 0)

#define _FY_CPP_REST(x, ...) __VA_ARGS__
#define FY_CPP_REST(...)     __VA_OPT__(_FY_CPP_REST(__VA_ARGS__))

#define _FY_CPP_COUNT_BODY(x) +1
#define FY_CPP_VA_COUNT(...)  (__VA_OPT__((FY_CPP_MAP(_FY_CPP_COUNT_BODY, __VA_ARGS__) + 0)) + 0)

#define _FY_CPP_ITEM_ONE(arg) arg
#define _FY_CPP_ITEM_LATER_ARG(arg) , _FY_CPP_ITEM_ONE(arg)
#define _FY_CPP_ITEM_LIST(...) FY_CPP_MAP(_FY_CPP_ITEM_LATER_ARG, __VA_ARGS__)

#define _FY_CPP_VA_ITEMS(...)          \
    _FY_CPP_ITEM_ONE(FY_CPP_FIRST(__VA_ARGS__)) \
    _FY_CPP_ITEM_LIST(FY_CPP_REST(__VA_ARGS__))

#define FY_CPP_VA_ITEMS(_type, ...) \
	((_type [FY_CPP_VA_COUNT(__VA_ARGS__)]) { _FY_CPP_VA_ITEMS(__VA_ARGS__) })

#define FY_CPP_MAP2(a, macro, ...) \
    __VA_OPT__(FY_CPP_EVAL(_FY_CPP_MAP_ONE2(a, macro, __VA_ARGS__)))

#define _FY_CPP_MAP_ONE2(a, macro, x, ...) macro(a, x) \
    __VA_OPT__(FY_CPP_POSTPONE2(a, _FY_CPP_MAP_INDIRECT2)()(a, macro, __VA_ARGS__))
#define _FY_CPP_MAP_INDIRECT2() _FY_CPP_MAP_ONE2

#endif /* FY_CPP_SHORT_NAMES */

/*
 * example usage:
 *
 * int do_sum(int count, int *items)
 * {
 * 	int i;
 * 	int sum;
 *
 * 	sum = 0;
 * 	for (i = 0; i < count; i++)
 * 		sum += items[i];
 *
 * 	return sum;
 * }
 *
 * #define do_sum_macro(...) \
 * 	do_sum( \
 * 		FY_CPP_VA_COUNT(__VA_ARGS__), \
 * 		FY_CPP_VA_ITEMS(int, __VA_ARGS__))
 *
 *   sum = do_sum_macro(1, 2, 5, 100);
 *
 * will expand to:
 *
 *   sum = do_sum(4, ((int [4]){ 1, 2, 5, 100 }));
 *
 * printf("sum=%d\n", sum);
 */

/**
 * FY_CONCAT() - Token-paste two arguments after full macro expansion.
 *
 * Unlike the raw ``##`` operator, this macro forces both @_a and @_b to be
 * fully expanded before concatenation, so macro arguments are substituted
 * correctly.
 *
 * @_a: Left token (expanded before pasting).
 * @_b: Right token (expanded before pasting).
 */
#define FY_CONCAT(_a, _b) FY_CONCAT_INNER(_a, _b)
#define FY_CONCAT_INNER(_a, _b) _a ## _b

/**
 * FY_UNIQUE() - Generate a unique identifier using ``__COUNTER__``.
 *
 * Concatenates @_base with the current value of the ``__COUNTER__``
 * preprocessor counter, which increments by one for each use in a
 * translation unit. Guarantees unique names across multiple macro expansions
 * in the same file.
 *
 * @_base: Identifier prefix.
 */
#define FY_UNIQUE(_base) FY_CONCAT(_base, __COUNTER__)

/**
 * FY_LUNIQUE() - Generate a unique identifier using ``__LINE__``.
 *
 * Like FY_UNIQUE() but uses the current source line number instead of
 * ``__COUNTER__``. Sufficient when only one such identifier is needed per
 * source line; prefer FY_UNIQUE() in general.
 *
 * @_base: Identifier prefix.
 */
#define FY_LUNIQUE(_base) FY_CONCAT(_base, __LINE__)

#if __has_builtin(__builtin_stack_save) && __has_builtin(__builtin_stack_restore)
/**
 * FY_STACK_SAVE() - Save the current stack pointer.
 *
 * On compilers that provide ``__builtin_stack_save()`` (GCC, Clang) returns
 * the current stack pointer as a ``void *``, allowing it to be restored later
 * with FY_STACK_RESTORE(). On other compilers returns ``(void *)NULL``.
 *
 * Use together with FY_STACK_RESTORE() to bound the stack growth of a loop
 * that calls ``alloca()`` on each iteration.
 *
 * Returns: Opaque stack pointer value, or NULL if unsupported.
 */
#define FY_STACK_SAVE()		__builtin_stack_save()
/**
 * FY_STACK_RESTORE() - Restore the stack pointer to a previously saved value.
 *
 * On compilers that provide ``__builtin_stack_restore()`` rewinds the stack
 * pointer to the value captured by FY_STACK_SAVE(). On other compilers this
 * is a no-op that discards @_x.
 *
 * @_x: Value previously returned by FY_STACK_SAVE().
 */
#define FY_STACK_RESTORE(_x)	__builtin_stack_restore((_x))
#else
/* no builtin? just ignore */
#define FY_STACK_SAVE()		((void *)NULL)
#define FY_STACK_RESTORE(_x)	do { (void)(_x); } while(0)
#endif

/* possible have lambdas, either gcc or clang with block support */
#ifdef __clang__
#ifdef __BLOCKS__
/**
 * FY_HAVE_LAMBDAS - Defined when the compiler supports anonymous functions.
 *
 * Set when either:
 * - Clang is compiling with ``-fblocks`` (Blocks extension) — also sets
 *   %FY_HAVE_BLOCK_LAMBDAS; or
 * - GCC is in use — also sets %FY_HAVE_NESTED_FUNC_LAMBDAS (nested functions).
 *
 * Code using lambdas should guard against this macro and provide a fallback
 * for environments where it is not defined.
 */
#define FY_HAVE_LAMBDAS 1
/**
 * FY_HAVE_BLOCK_LAMBDAS - Defined when Clang Block lambdas are available.
 *
 * Set on Clang when compiled with ``-fblocks``. Implies %FY_HAVE_LAMBDAS.
 * Block lambdas use the ``^(args){ body }`` syntax.
 */
#define FY_HAVE_BLOCK_LAMBDAS 1
#endif
#elif defined(__GNUC__)
/* FY_HAVE_LAMBDAS is documented in the __clang__ branch above */
#define FY_HAVE_LAMBDAS 1
/**
 * FY_HAVE_NESTED_FUNC_LAMBDAS - Defined when GCC nested-function lambdas are available.
 *
 * Set on GCC. Implies %FY_HAVE_LAMBDAS. Nested function lambdas are defined
 * as local functions inside the enclosing function and cannot outlive it.
 */
#define FY_HAVE_NESTED_FUNC_LAMBDAS 1
#endif

/**
 * DOC: Compiler diagnostic control helpers
 *
 * Portable wrappers around GCC/Clang ``#pragma GCC diagnostic`` directives.
 * Use FY_DIAG_PUSH() and FY_DIAG_POP() to bracket a region where a specific
 * warning is suppressed::
 *
 *   FY_DIAG_PUSH
 *   FY_DIAG_IGNORE_ARRAY_BOUNDS
 *   ... code that triggers -Warray-bounds ...
 *   FY_DIAG_POP
 *
 * On compilers other than GCC/Clang all macros in this group expand to nothing.
 */

/* Compiler diagnostic helpers */

#if defined(__clang__) || defined(__GNUC__)
/**
 * FY_DIAG_PUSH - Save the current diagnostic state onto the compiler's stack.
 *
 * Always paired with FY_DIAG_POP.
 */
#define FY_DIAG_PUSH           _Pragma("GCC diagnostic push")
/**
 * FY_DIAG_POP - Restore the diagnostic state saved by the last FY_DIAG_PUSH.
 */
#define FY_DIAG_POP            _Pragma("GCC diagnostic pop")
/**
 * FY_DIAG_IGNORE() - Suppress a specific warning by pragma string.
 *
 * @_warn: A string literal suitable for use in a ``_Pragma()`` call,
 *         e.g. ``"GCC diagnostic ignored \"-Wsomething\""`` .
 */
#define FY_DIAG_IGNORE(_warn)   _Pragma(_warn)
/**
 * FY_DIAG_IGNORE_ARRAY_BOUNDS - Suppress ``-Warray-bounds`` in the current region.
 */
#define FY_DIAG_IGNORE_ARRAY_BOUNDS \
	_Pragma("GCC diagnostic ignored \"-Warray-bounds\"")
/**
 * FY_DIAG_IGNORE_UNUSED_VARIABLE - Suppress ``-Wunused-variable`` in the current region.
 */
#define FY_DIAG_IGNORE_UNUSED_VARIABLE \
	_Pragma("GCC diagnostic ignored \"-Wunused-variable\"")
/**
 * FY_DIAG_IGNORE_UNUSED_PARAMETER - Suppress ``-Wunused-parameter`` in the current region.
 */
#define FY_DIAG_IGNORE_UNUSED_PARAMETER \
	_Pragma("GCC diagnostic ignored \"-Wunused-parameter\"")

#else
#define FY_DIAG_PUSH
#define FY_DIAG_POP
#define FY_DIAG_IGNORE(_warn)

#define FY_DIAG_IGNORE_ARRAY_BOUNDS /* nothing */
#define FY_DIAG_IGNORE_UNUSED_VARIABLE /* nothing */
#define FY_DIAG_IGNORE_UNUSED_PARAMETER /* nothing */

#endif

/**
 * DOC: Floating-point precision constants
 *
 * Portable wrappers for the mantissa-digit and decimal-digit counts of
 * ``float``, ``double``, and ``long double``.  Each macro tries three
 * sources in order:
 *
 * 1. The standard ``<float.h>`` macro (e.g. ``FLT_MANT_DIG``).
 * 2. The GCC/Clang predefined macro (e.g. ``__FLT_MANT_DIG__``).
 * 3. A conservative hard-coded fallback.
 *
 * These constants are used when formatting floating-point values as YAML
 * scalars to ensure round-trip fidelity.
 */

/**
 * FY_FLT_MANT_DIG - Number of base-2 mantissa digits in a ``float``.
 *
 * Typically 24 (IEEE 754 single precision). Fallback: 9.
 */
/* float point digits abstraction */
#if defined(FLT_MANT_DIG)
#define FY_FLT_MANT_DIG	FLT_MANT_DIG
#elif defined(__FLT_MANT_DIG__)
#define FY_FLT_MANT_DIG	__FLT_MANT_DIG__
#else
#define FY_FLT_MANT_DIG	9
#endif

/**
 * FY_DBL_MANT_DIG - Number of base-2 mantissa digits in a ``double``.
 *
 * Typically 53 (IEEE 754 double precision). Fallback: 17.
 */
#if defined(DBL_MANT_DIG)
#define FY_DBL_MANT_DIG	DBL_MANT_DIG
#elif defined(__DBL_MANT_DIG__)
#define FY_DBL_MANT_DIG	__DBL_MANT_DIG__
#else
#define FY_DBL_MANT_DIG	17
#endif

/**
 * FY_LDBL_MANT_DIG - Number of base-2 mantissa digits in a ``long double``.
 *
 * Varies by platform (64-bit x87 extended: 64; MSVC/ARM ``== double``: 53).
 * Fallback: 17.
 */
#if defined(LDBL_MANT_DIG)
#define FY_LDBL_MANT_DIG	LDBL_MANT_DIG
#elif defined(__LDBL_MANT_DIG__)
#define FY_LDBL_MANT_DIG	__LDBL_MANT_DIG__
#else
#define FY_LDBL_MANT_DIG	17
#endif

/**
 * FY_FLT_DECIMAL_DIG - Decimal digits required for a round-trip ``float``.
 *
 * The minimum number of significant decimal digits such that converting
 * a ``float`` to decimal and back recovers the original value exactly.
 * Typically 9. Fallback: 9.
 */
#if defined(FLT_DECIMAL_DIG)
#define FY_FLT_DECIMAL_DIG	FLT_DECIMAL_DIG
#elif defined(__FLT_DECIMAL_DIG__)
#define FY_FLT_DECIMAL_DIG	__FLT_DECIMAL_DIG__
#else
#define FY_FLT_DECIMAL_DIG	9
#endif

/**
 * FY_DBL_DECIMAL_DIG - Decimal digits required for a round-trip ``double``.
 *
 * Typically 17. Fallback: 17.
 */
#if defined(DBL_DECIMAL_DIG)
#define FY_DBL_DECIMAL_DIG	DBL_DECIMAL_DIG
#elif defined(__DBL_DECIMAL_DIG__)
#define FY_DBL_DECIMAL_DIG	__DBL_DECIMAL_DIG__
#else
#define FY_DBL_DECIMAL_DIG	17
#endif

/**
 * FY_LDBL_DECIMAL_DIG - Decimal digits required for a round-trip ``long double``.
 *
 * Varies by platform; same as double on most non-x87 targets. Fallback: 17.
 */
#if defined(LDBL_DECIMAL_DIG)
#define FY_LDBL_DECIMAL_DIG	LDBL_DECIMAL_DIG
#elif defined(__LDBL_DECIMAL_DIG__)
#define FY_LDBL_DECIMAL_DIG	__LDBL_DECIMAL_DIG__
#else
#define FY_LDBL_DECIMAL_DIG	17	// same as double
#endif

/* platform detection - needed by both reflection and generic sections */
// define FY_HAS_FP16 if __fp16 is available
#if defined(__is_identifier)
#if ! __is_identifier(__fp16)
#define FY_HAS_FP16
#endif
#endif

// define FY_HAS_FLOAT128 if __fp128 is available
#if defined(__SIZEOF_FLOAT128__)
#if __SIZEOF_FLOAT128__ == 16
#define FY_HAS_FLOAT128
#endif
#endif

// define FY_HAS_INT128 if __int128 is available
#if defined(__SIZEOF_INT128__)
#if __SIZEOF_INT128__ == 16
#define FY_HAS_INT128
#endif
#endif

#ifdef UINTPTR_MAX
#if UINTPTR_MAX == 0xffffffff
#define FY_HAS_32BIT_PTR
#elif UINTPTR_MAX == 0xffffffffffffffff
#define FY_HAS_64BIT_PTR
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif	// LIBFYAML_UTIL_H
