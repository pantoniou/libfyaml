/* SPDX-License-Identifier: MIT */
/*
 * libfyaml-test-cpp-atomics.cpp - C++ compilation test for libfyaml-atomics.h
 *
 * Regression test for the <stdatomic.h> detection bug in libfyaml-atomics.h:
 *
 * Bug 1 (operator-precedence): The FY_HAVE_STDATOMIC_H detection condition
 *   treats the leading !defined(__STDC__NO_ATOMICS__) as only guarding the
 *   first OR clause, so GCC/Clang fire unconditionally in C++ mode — yet the
 *   <stdatomic.h> include was inside extern "C", which breaks C++ compilers
 *   whose <stdatomic.h> contains C++-incompatible constructs (e.g. templates
 *   or _Atomic typedefs) at that linkage scope.
 *
 * Bug 2 (fallback macros): When FY_HAVE_STDATOMIC_H is not defined the header
 *   falls back to macros using ({ }) GNU statement-expressions and __typeof__,
 *   which are GCC extensions unavailable in strict C++ or MSVC.
 *
 * This file must compile cleanly as C++ with -std=c++17 (or later).
 * It fails to compile against the unfixed libfyaml-atomics.h (v1.0.0-alpha6).
 */

#include <libfyaml/libfyaml-atomics.h>

#include <cassert>
#include <cstdint>

static int test_atomic_int(void)
{
    /* Basic load / store round-trip. */
    FY_ATOMIC(int) v = 0;
    fy_atomic_store(&v, 7);
    int got = fy_atomic_load(&v);
    assert(got == 7);

    /* fetch_add returns the old value. */
    int old = fy_atomic_fetch_add(&v, 3);
    assert(old == 7);
    assert(fy_atomic_load(&v) == 10);

    return 0;
}

static int test_atomic_flag(void)
{
    fy_atomic_flag flag = {};
    fy_atomic_flag_clear(&flag);
    /* test_and_set: first call must return false (flag was clear). */
    bool was_set = fy_atomic_flag_test_and_set(&flag);
    assert(!was_set);
    /* second call must return true (flag was already set). */
    was_set = fy_atomic_flag_test_and_set(&flag);
    assert(was_set);
    fy_atomic_flag_clear(&flag);
    return 0;
}

static int test_drain_counter(void)
{
    FY_ATOMIC(uint64_t) ctr = 0;
    fy_atomic_fetch_add(&ctr, 5);
    uint64_t drained = fy_atomic_get_and_clear_counter(&ctr);
    assert(drained == 5);
    /* counter is not guaranteed to be exactly 0 (two-op drain), but must be
     * no greater than the original value. */
    assert(fy_atomic_load(&ctr) <= 5);
    return 0;
}

int main(void)
{
    int rc = 0;
    rc |= test_atomic_int();
    rc |= test_atomic_flag();
    rc |= test_drain_counter();
    return rc;
}
