/*
 * libfyaml-cpp.h - C preprocessor metaprogramming framework (FY_CPP_*)
 *
 * Copyright (c) 2019-2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
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
#ifndef LIBFYAML_CPP_H
#define LIBFYAML_CPP_H

/**
 * DOC: C preprocessor metaprogramming framework
 *
 * This section implements a portable ``FY_CPP_*`` macro toolkit that enables
 * applying an operation to every argument in a ``__VA_ARGS__`` list — the
 * equivalent of a compile-time ``map()`` function.
 *
 * **Implementation**: arity dispatch.  The number of arguments is counted
 * positionally (``_FVN``) and pasted onto a per-arity worker macro
 * (``_FM_1`` .. ``_FM_64``), each of which handles one element and chains to
 * the next-lower arity.  No token stream is ever re-scanned, so the expansion
 * cost is linear in the output size.  (The previous implementation forced up
 * to 64 re-expansion passes via an ``FY_CPP_EVALn`` ladder, which exhausted
 * Clang's source-location address space on large translation units:
 * "fatal error: translation unit is too large for Clang to process".)
 *
 * The maximum list length is **64 arguments** on all compilers.  Exceeding it
 * (up to 96) produces a compile error mentioning
 * ``FY_CPP_MAP_too_many_arguments``.  The tables are mechanical and can be
 * extended if ever needed.
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
 *
 * **Legacy helpers** kept for source compatibility (no longer used by the
 * map machinery itself): ``FY_CPP_EVALn``/``FY_CPP_EVAL`` (forced
 * re-expansion passes), ``FY_CPP_EMPTY()``, ``FY_CPP_POSTPONE1/2()``.
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
 * FY_CPP_SHORT_NAMES is kept defined for compatibility: libfyaml-generic.h
 * selects its short-name item-builder branch based on it.  The short internal
 * names (_FM, _FM2, _FVC, ...) are the only implementation now.
 */
#define FY_CPP_SHORT_NAMES

/* Legacy forced re-expansion ladder; kept for source compatibility only. */
#define _E1(...)	__VA_ARGS__
#define _E2(...)	_E1(_E1(__VA_ARGS__))
#define _E4(...)	_E2(_E2(__VA_ARGS__))
#define _E8(...)	_E4(_E4(__VA_ARGS__))
#define _E16(...)	_E8(_E8(__VA_ARGS__))
#define _E32(...)	_E16(_E16(__VA_ARGS__))
#if !defined(__SIZEOF_POINTER__) || __SIZEOF_POINTER__ >= 8
#define _E(...)		_E32(_E32(__VA_ARGS__))
#else
#define _E(...)		_E16(_E16(__VA_ARGS__))
#endif

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

/* Positional argument counter: raw token (no parentheses), suitable for pasting. */
#define _FVN_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, \
	_15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, \
	_30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, \
	_45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, \
	_60, _61, _62, _63, _64, _65, _66, _67, _68, _69, _70, _71, _72, _73, _74, \
	_75, _76, _77, _78, _79, _80, _81, _82, _83, _84, _85, _86, _87, _88, _89, \
	_90, _91, _92, _93, _94, _95, _96, _N, ...) _N
#define _FVN(...) _FVN_(__VA_ARGS__, TOOMANY, TOOMANY, TOOMANY, TOOMANY, \
	TOOMANY, TOOMANY, TOOMANY, TOOMANY, TOOMANY, TOOMANY, TOOMANY, TOOMANY, \
	TOOMANY, TOOMANY, TOOMANY, TOOMANY, TOOMANY, TOOMANY, TOOMANY, TOOMANY, \
	TOOMANY, TOOMANY, TOOMANY, TOOMANY, TOOMANY, TOOMANY, TOOMANY, TOOMANY, \
	TOOMANY, TOOMANY, TOOMANY, TOOMANY, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, \
	54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, \
	35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, \
	16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)

/* paste after full expansion (local helper; FY_CONCAT lives in libfyaml-util.h) */
#define _FMC(_a, _b) _FMC_(_a, _b)
#define _FMC_(_a, _b) _a ## _b

/* _FM_<n>: apply m() to each of n arguments, no separator */
#define _FM_TOOMANY(...) FY_CPP_MAP_too_many_arguments()
#define _FM_1(m, x1) m(x1)
#define _FM_2(m, x1, x2) \
	m(x1) m(x2)
#define _FM_3(m, x1, x2, x3) \
	m(x1) m(x2) m(x3)
#define _FM_4(m, x1, x2, x3, x4) \
	m(x1) m(x2) m(x3) m(x4)
#define _FM_5(m, x1, x2, x3, x4, x5) \
	m(x1) m(x2) m(x3) m(x4) m(x5)
#define _FM_6(m, x1, x2, x3, x4, x5, x6) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6)
#define _FM_7(m, x1, x2, x3, x4, x5, x6, x7) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7)
#define _FM_8(m, x1, x2, x3, x4, x5, x6, x7, x8) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8)
#define _FM_9(m, x1, x2, x3, x4, x5, x6, x7, x8, x9) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9)
#define _FM_10(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10)
#define _FM_11(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11)
#define _FM_12(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12)
#define _FM_13(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13)
#define _FM_14(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14)
#define _FM_15(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15)
#define _FM_16(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16)
#define _FM_17(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17)
#define _FM_18(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18)
#define _FM_19(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19)
#define _FM_20(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20)
#define _FM_21(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21)
#define _FM_22(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22)
#define _FM_23(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23)
#define _FM_24(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24)
#define _FM_25(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25)
#define _FM_26(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26)
#define _FM_27(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27)
#define _FM_28(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28)
#define _FM_29(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29)
#define _FM_30(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30)
#define _FM_31(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31)
#define _FM_32(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32)
#define _FM_33(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33)
#define _FM_34(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34)
#define _FM_35(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35)
#define _FM_36(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36)
#define _FM_37(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37)
#define _FM_38(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38)
#define _FM_39(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39)
#define _FM_40(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40)
#define _FM_41(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41)
#define _FM_42(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42)
#define _FM_43(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43)
#define _FM_44(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44)
#define _FM_45(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45)
#define _FM_46(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46)
#define _FM_47(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47)
#define _FM_48(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48)
#define _FM_49(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49)
#define _FM_50(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49) m(x50)
#define _FM_51(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49) m(x50) m(x51)
#define _FM_52(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49) m(x50) m(x51) m(x52)
#define _FM_53(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49) m(x50) m(x51) m(x52) m(x53)
#define _FM_54(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49) m(x50) m(x51) m(x52) m(x53) m(x54)
#define _FM_55(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49) m(x50) m(x51) m(x52) m(x53) m(x54) m(x55)
#define _FM_56(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49) m(x50) m(x51) m(x52) m(x53) m(x54) m(x55) m(x56)
#define _FM_57(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49) m(x50) m(x51) m(x52) m(x53) m(x54) m(x55) m(x56) \
	m(x57)
#define _FM_58(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57, x58) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49) m(x50) m(x51) m(x52) m(x53) m(x54) m(x55) m(x56) \
	m(x57) m(x58)
#define _FM_59(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57, x58, \
	x59) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49) m(x50) m(x51) m(x52) m(x53) m(x54) m(x55) m(x56) \
	m(x57) m(x58) m(x59)
#define _FM_60(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57, x58, \
	x59, x60) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49) m(x50) m(x51) m(x52) m(x53) m(x54) m(x55) m(x56) \
	m(x57) m(x58) m(x59) m(x60)
#define _FM_61(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57, x58, \
	x59, x60, x61) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49) m(x50) m(x51) m(x52) m(x53) m(x54) m(x55) m(x56) \
	m(x57) m(x58) m(x59) m(x60) m(x61)
#define _FM_62(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57, x58, \
	x59, x60, x61, x62) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49) m(x50) m(x51) m(x52) m(x53) m(x54) m(x55) m(x56) \
	m(x57) m(x58) m(x59) m(x60) m(x61) m(x62)
#define _FM_63(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57, x58, \
	x59, x60, x61, x62, x63) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49) m(x50) m(x51) m(x52) m(x53) m(x54) m(x55) m(x56) \
	m(x57) m(x58) m(x59) m(x60) m(x61) m(x62) m(x63)
#define _FM_64(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57, x58, \
	x59, x60, x61, x62, x63, x64) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8) m(x9) m(x10) m(x11) m(x12) \
	m(x13) m(x14) m(x15) m(x16) m(x17) m(x18) m(x19) m(x20) m(x21) m(x22) m(x23) \
	m(x24) m(x25) m(x26) m(x27) m(x28) m(x29) m(x30) m(x31) m(x32) m(x33) m(x34) \
	m(x35) m(x36) m(x37) m(x38) m(x39) m(x40) m(x41) m(x42) m(x43) m(x44) m(x45) \
	m(x46) m(x47) m(x48) m(x49) m(x50) m(x51) m(x52) m(x53) m(x54) m(x55) m(x56) \
	m(x57) m(x58) m(x59) m(x60) m(x61) m(x62) m(x63) m(x64)

/* _FM2_<n>: apply m(a, x) to each of n arguments, threading fixed arg a */
#define _FM2_TOOMANY(...) FY_CPP_MAP_too_many_arguments()
#define _FM2_1(a, m, x1) m(a, x1)
#define _FM2_2(a, m, x1, x2) \
	m(a, x1) m(a, x2)
#define _FM2_3(a, m, x1, x2, x3) \
	m(a, x1) m(a, x2) m(a, x3)
#define _FM2_4(a, m, x1, x2, x3, x4) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4)
#define _FM2_5(a, m, x1, x2, x3, x4, x5) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5)
#define _FM2_6(a, m, x1, x2, x3, x4, x5, x6) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6)
#define _FM2_7(a, m, x1, x2, x3, x4, x5, x6, x7) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7)
#define _FM2_8(a, m, x1, x2, x3, x4, x5, x6, x7, x8) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8)
#define _FM2_9(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9)
#define _FM2_10(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10)
#define _FM2_11(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11)
#define _FM2_12(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12)
#define _FM2_13(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13)
#define _FM2_14(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14)
#define _FM2_15(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15)
#define _FM2_16(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16)
#define _FM2_17(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17)
#define _FM2_18(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18)
#define _FM2_19(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19)
#define _FM2_20(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20)
#define _FM2_21(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21)
#define _FM2_22(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22)
#define _FM2_23(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23)
#define _FM2_24(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24)
#define _FM2_25(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25)
#define _FM2_26(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26)
#define _FM2_27(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27)
#define _FM2_28(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28)
#define _FM2_29(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29)
#define _FM2_30(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30)
#define _FM2_31(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31)
#define _FM2_32(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32)
#define _FM2_33(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33)
#define _FM2_34(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34)
#define _FM2_35(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35)
#define _FM2_36(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36)
#define _FM2_37(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37)
#define _FM2_38(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38)
#define _FM2_39(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39)
#define _FM2_40(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40)
#define _FM2_41(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41)
#define _FM2_42(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42)
#define _FM2_43(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43)
#define _FM2_44(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44)
#define _FM2_45(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45)
#define _FM2_46(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46)
#define _FM2_47(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47)
#define _FM2_48(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48)
#define _FM2_49(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49)
#define _FM2_50(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49) m(a, x50)
#define _FM2_51(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49) m(a, x50) \
	m(a, x51)
#define _FM2_52(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49) m(a, x50) \
	m(a, x51) m(a, x52)
#define _FM2_53(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49) m(a, x50) \
	m(a, x51) m(a, x52) m(a, x53)
#define _FM2_54(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49) m(a, x50) \
	m(a, x51) m(a, x52) m(a, x53) m(a, x54)
#define _FM2_55(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49) m(a, x50) \
	m(a, x51) m(a, x52) m(a, x53) m(a, x54) m(a, x55)
#define _FM2_56(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49) m(a, x50) \
	m(a, x51) m(a, x52) m(a, x53) m(a, x54) m(a, x55) m(a, x56)
#define _FM2_57(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49) m(a, x50) \
	m(a, x51) m(a, x52) m(a, x53) m(a, x54) m(a, x55) m(a, x56) m(a, x57)
#define _FM2_58(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57, x58) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49) m(a, x50) \
	m(a, x51) m(a, x52) m(a, x53) m(a, x54) m(a, x55) m(a, x56) m(a, x57) \
	m(a, x58)
#define _FM2_59(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57, x58, \
	x59) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49) m(a, x50) \
	m(a, x51) m(a, x52) m(a, x53) m(a, x54) m(a, x55) m(a, x56) m(a, x57) \
	m(a, x58) m(a, x59)
#define _FM2_60(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57, x58, \
	x59, x60) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49) m(a, x50) \
	m(a, x51) m(a, x52) m(a, x53) m(a, x54) m(a, x55) m(a, x56) m(a, x57) \
	m(a, x58) m(a, x59) m(a, x60)
#define _FM2_61(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57, x58, \
	x59, x60, x61) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49) m(a, x50) \
	m(a, x51) m(a, x52) m(a, x53) m(a, x54) m(a, x55) m(a, x56) m(a, x57) \
	m(a, x58) m(a, x59) m(a, x60) m(a, x61)
#define _FM2_62(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57, x58, \
	x59, x60, x61, x62) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49) m(a, x50) \
	m(a, x51) m(a, x52) m(a, x53) m(a, x54) m(a, x55) m(a, x56) m(a, x57) \
	m(a, x58) m(a, x59) m(a, x60) m(a, x61) m(a, x62)
#define _FM2_63(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57, x58, \
	x59, x60, x61, x62, x63) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49) m(a, x50) \
	m(a, x51) m(a, x52) m(a, x53) m(a, x54) m(a, x55) m(a, x56) m(a, x57) \
	m(a, x58) m(a, x59) m(a, x60) m(a, x61) m(a, x62) m(a, x63)
#define _FM2_64(a, m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, \
	x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, \
	x29, x30, x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, x41, x42, x43, \
	x44, x45, x46, x47, x48, x49, x50, x51, x52, x53, x54, x55, x56, x57, x58, \
	x59, x60, x61, x62, x63, x64) \
	m(a, x1) m(a, x2) m(a, x3) m(a, x4) m(a, x5) m(a, x6) m(a, x7) m(a, x8) \
	m(a, x9) m(a, x10) m(a, x11) m(a, x12) m(a, x13) m(a, x14) m(a, x15) \
	m(a, x16) m(a, x17) m(a, x18) m(a, x19) m(a, x20) m(a, x21) m(a, x22) \
	m(a, x23) m(a, x24) m(a, x25) m(a, x26) m(a, x27) m(a, x28) m(a, x29) \
	m(a, x30) m(a, x31) m(a, x32) m(a, x33) m(a, x34) m(a, x35) m(a, x36) \
	m(a, x37) m(a, x38) m(a, x39) m(a, x40) m(a, x41) m(a, x42) m(a, x43) \
	m(a, x44) m(a, x45) m(a, x46) m(a, x47) m(a, x48) m(a, x49) m(a, x50) \
	m(a, x51) m(a, x52) m(a, x53) m(a, x54) m(a, x55) m(a, x56) m(a, x57) \
	m(a, x58) m(a, x59) m(a, x60) m(a, x61) m(a, x62) m(a, x63) m(a, x64)

/*
 * Flat map dispatch: count the arguments, paste onto the per-arity worker.
 * No re-scanning passes; expansion cost is linear in the output.
 */
#define _FM(m, ...) \
    __VA_OPT__(_FMC(_FM_, _FVN(__VA_ARGS__))(m, __VA_ARGS__))

#define _FM2(a, m, ...) \
    __VA_OPT__(_FMC(_FM2_, _FVN(__VA_ARGS__))(a, m, __VA_ARGS__))

#define _FCB(x) +1
#define _FVC(...)  (0 __VA_OPT__(+ _FVN(__VA_ARGS__)))

#define _FI1(a) a
#define _FIL(a) , _FI1(a)
#define _FILS(...) _FM(_FIL, __VA_ARGS__)

#define _FVIS(...) \
    _FI1(_F1F(__VA_ARGS__)) \
    _FILS(_FRF(__VA_ARGS__))

#define _FVISF(_t, ...) \
	((_t [_FVC(__VA_ARGS__)]) { _FVIS(__VA_ARGS__) })

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

#endif /* LIBFYAML_CPP_H */
