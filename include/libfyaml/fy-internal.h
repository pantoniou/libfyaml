/*
 * fy-internal.h - Internal libfyaml header utils
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

#ifndef FY_INTERNAL_H
#define FY_INTERNAL_H

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
#endif

#include <sys/uio.h>

/* make things fail easy on non GCC/clang */
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

/*
 * NOTE: This file contains internals that are not part of the public API
 * Things may change at anytime (and they will). Use at your own peril.
 */

#ifndef FY_BIT
#define FY_BIT(x) (1U << (x))
#endif

/* NULL terminated string length specifier */
#define FY_NT	((size_t)-1)

#if defined(__GNUC__) && __GNUC__ >= 4
#define FY_EXPORT __attribute__ ((visibility ("default")))
#define FY_DEPRECATED __attribute__ ((deprecated))
#define FY_FORMAT(_t, _x, _y) __attribute__ ((format(_t, _x, _y)))
#else
#define FY_EXPORT /* nothing */
#define FY_DEPRECATED /* nothing */
#define FY_FORMAT(_t, x, y)	/* nothing */
#endif

/* make a copy of an allocated string and return it on stack
 * note that this is a convenience, and should not be called
 * in a loop. The string is always '\0' terminated.
 * If the _str pointer is NULL, then NULL will be returned
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

/* same as above but when _str == NULL return "" */
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

#ifndef container_of
#define container_of(ptr, type, member) \
	({ \
		const __typeof__(((type *)0)->member) *__mptr = (ptr); \
		(type *)((void *)__mptr - offsetof(type,member)); \
	 })
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) ((sizeof(x)/sizeof((x)[0])))
#endif

#if defined(__GNUC__) && __GNUC__ >= 4
#define FY_UNUSED __attribute__((unused))
#else
#define FY_UNUSED /* nothing */
#endif

#if defined(__GNUC__) && __GNUC__ >= 4
#define FY_CONSTRUCTOR __attribute__((constructor))
#define FY_DESTRUCTOR __attribute__((destructor))
#define FY_HAS_CONSTRUCTOR
#define FY_HAS_DESTRUCTOR
#else
#define FY_CONSTRUCTOR /* nothing */
#define FY_DESTRUCTOR /* nothing */
#endif

#if defined(NDEBUG) && (defined(__GNUC__) && __GNUC__ >= 4)
#define FY_ALWAYS_INLINE __attribute__((always_inline))
#else
#define FY_ALWAYS_INLINE /* nothing */
#endif

#if defined(NDEBUG) && defined(__GNUC__) && __GNUC__ >= 4
#define FY_DEBUG_UNUSED __attribute__((unused))
#else
#define FY_DEBUG_UNUSED /* nothing */
#endif

#if defined(FY_DESTRUCTOR) && defined(NDEBUG)
#define FY_DESTRUCTOR_SHOW_LEFTOVERS
#endif

/* something impossible is happening, assert/abort */
#define FY_IMPOSSIBLE_ABORT() \
	do { \
		assert(0); \
		abort(); \
	} while(0)

/* if expression is zero, then the build will break */
#define FY_COMPILE_ERROR_ON_ZERO(_e) ((void)(sizeof(char[1 - 2*!!(_e)])))

/* true, if types are the same, false otherwise (depends on builtin_types_compatible_p) */
#if __has_builtin(__builtin_types_compatible_p)
#define FY_SAME_TYPE(_a, _b) __builtin_types_compatible_p(__typeof__(_a), __typeof__(_b))
#else
#define FY_SAME_TYPE(_a, _b) true
#endif

/* compile error if types are not the same */
#define FY_CHECK_SAME_TYPE(_a, _b) \
    FY_COMPILE_ERROR_ON_ZERO(!FY_SAME_TYPE(_a, _b))

/* type safe add overflow */
#if __has_builtin(__builtin_add_overflow)
#define FY_ADD_OVERFLOW __builtin_add_overflow
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

/* type safe sub overflow */
#if __has_builtin(__builtin_sub_overflow)
#define FY_SUB_OVERFLOW __builtin_sub_overflow
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

/* type safe multiply overflow */
#if __has_builtin(__builtin_mul_overflow)
#define FY_MUL_OVERFLOW __builtin_mul_overflow
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
#define FY_CPP_EVAL1		_E1
#define FY_CPP_EVAL2		_E2
#define FY_CPP_EVAL4		_E4
#define FY_CPP_EVAL8		_E8
#if !defined(__clang__)
#define FY_CPP_EVAL16		_E16
#endif
#define FY_CPP_EVAL		_E
#define FY_CPP_EMPTY		_FMT
#define FY_CPP_POSTPONE1	_FP1
#define FY_CPP_POSTPONE2	_FP2

#define FY_CPP_FIRST		_F1F
#define FY_CPP_SECOND		_F2F
#define FY_CPP_THIRD		_F3F
#define FY_CPP_FOURTH		_F4F
#define FY_CPP_FIFTH		_F5F
#define FY_CPP_SIXTH		_F6F
#define FY_CPP_REST		_FRF

#define FY_CPP_MAP		_FM
#define _FY_CPP_MAP_ONE		_FM1
#define _FY_CPP_MAP_INDIRECT	_FMI
#define _FY_CPP_COUNT_BODY	_FCB
#define FY_CPP_VA_COUNT		_FVC
#define _FY_CPP_ITEM_ONE	_FI1
#define _FY_CPP_ITEM_LATER_ARG	_FIL
#define _FY_CPP_ITEM_LIST	_FILS
#define _FY_CPP_VA_ITEMS	_FVIS
#define FY_CPP_VA_ITEMS		_FVISF
#define FY_CPP_MAP2		_FM2
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

#define FY_CONCAT(_a, _b) FY_CONCAT_INNER(_a, _b)
#define FY_CONCAT_INNER(_a, _b) _a ## _b
#define FY_UNIQUE(_base) FY_CONCAT(_base, __COUNTER__)
#define FY_LUNIQUE(_base) FY_CONCAT(_base, __LINE__)

#if __has_builtin(__builtin_stack_save) && __has_builtin(__builtin_stack_restore)
#define FY_STACK_SAVE()		__builtin_stack_save()
#define FY_STACK_RESTORE(_x)	__builtin_stack_restore((_x))
#else
/* no builtin? just ignore */
#define FY_STACK_SAVE()		((void *)NULL)
#define FY_STACK_RESTORE(_x)	do { (void)(_x); } while(0)
#endif

/* possible have lambdas, either gcc or clang with block support */
#ifdef __clang__
#ifdef __BLOCKS__
#define FY_HAVE_LAMBDAS
#define FY_HAVE_BLOCK_LAMBDAS
#endif
#elif defined(__GNUC__)
#define FY_HAVE_LAMBDAS
#define FY_HAVE_NESTED_FUNC_LAMBDAS
#endif

/* */
/* Compiler diagnostic helpers */

#if defined(__clang__) || defined(__GNUC__)
#define FY_DIAG_PUSH           _Pragma("GCC diagnostic push")
#define FY_DIAG_POP            _Pragma("GCC diagnostic pop")
#define FY_DIAG_IGNORE(_warn)   _Pragma(_warn)

#define FY_DIAG_IGNORE_ARRAY_BOUNDS \
	_Pragma("GCC diagnostic ignored \"-Warray-bounds\"")

#define FY_DIAG_IGNORE_UNUSED_VARIABLE \
	_Pragma("GCC diagnostic ignored \"-Wunused-variable\"")

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

/* float point digits abstraction */
#if defined(FLT_MANT_DIG)
#define FY_FLT_MANT_DIG	FLT_MANT_DIG
#elif defined(__FLT_MANT_DIG__)
#define FY_FLT_MANT_DIG	__FLT_MANT_DIG__
#else
#define FY_FLT_MANT_DIG	9
#endif

#if defined(DBL_MANT_DIG)
#define FY_DBL_MANT_DIG	DBL_MANT_DIG
#elif defined(__DBL_MANT_DIG__)
#define FY_DBL_MANT_DIG	__DBL_MANT_DIG__
#else
#define FY_DBL_MANT_DIG	17
#endif

#if defined(LDBL_MANT_DIG)
#define FY_LDBL_MANT_DIG	LDBL_MANT_DIG
#elif defined(__LDBL_MANT_DIG__)
#define FY_LDBL_MANT_DIG	__LDBL_MANT_DIG__
#else
#define FY_LDBL_MANT_DIG	17
#endif

#if defined(FLT_DECIMAL_DIG)
#define FY_FLT_DECIMAL_DIG	FLT_DECIMAL_DIG
#elif defined(__FLT_DECIMAL_DIG__)
#define FY_FLT_DECIMAL_DIG	__FLT_DECIMAL_DIG__
#else
#define FY_FLT_DECIMAL_DIG	9
#endif

#if defined(DBL_DECIMAL_DIG)
#define FY_DBL_DECIMAL_DIG	DBL_DECIMAL_DIG
#elif defined(__DBL_DECIMAL_DIG__)
#define FY_DBL_DECIMAL_DIG	__DBL_DECIMAL_DIG__
#else
#define FY_DBL_DECIMAL_DIG	17
#endif

#if defined(LDBL_DECIMAL_DIG)
#define FY_LDBL_DECIMAL_DIG	LDBL_DECIMAL_DIG
#elif defined(__LDBL_DECIMAL_DIG__)
#define FY_LDBL_DECIMAL_DIG	__LDBL_DECIMAL_DIG__
#else
#define FY_LDBL_DECIMAL_DIG	17	// same as double
#endif

#ifdef __cplusplus
}
#endif

#endif
