/* SPDX-License-Identifier: MIT */
/*
 * libfyaml-test-cpp-include-tu.cpp - C++ TU inclusion test for libfyaml
 *
 * Regression test for building libfyaml from a C++ translation unit, which
 * is the usage pattern in MRPT (yaml.cpp includes <libfyaml.h> directly).
 *
 * The unfixed libfyaml-atomics.h (v1.0.0-alpha6) fails to compile here
 * because <stdatomic.h> is pulled in while still inside extern "C".  On
 * compilers where <stdatomic.h> uses C++-incompatible constructs at that
 * linkage scope (e.g. _Atomic typedefs on GCC with -std=c++17), this
 * produces errors such as:
 *
 *   error: '_Atomic' was not declared in this scope
 *   error: expected primary-expression before ')' token
 *
 * After the fix the same file compiles without modification.
 *
 * Include order note: on Clang/libc++, <stdatomic.h> defines atomic_load,
 * atomic_store, atomic_is_lock_free etc. as function-like macros.  If any
 * C++ STL header that transitively includes <atomic> is then compiled while
 * those macros are active, libc++'s template function declarations get
 * macro-expanded and cause parse errors.  Keep <libfyaml.h> before any
 * such STL headers, or (safer) avoid STL headers that reach <atomic> in
 * the same TU.  This test deliberately uses only <cstdio>/<cstring> to
 * stay portable across all STL implementations.
 *
 * In addition to the compile check this test performs a minimal parse so
 * that the binary can be run as part of the CTest suite.
 */

/* libfyaml.h pulls in libfyaml-atomics.h; if the latter is broken the
 * compilation stops here with the errors described above. */
#include <libfyaml.h>

#include <cstdio>
#include <cstring>

int main(void)
{
    static const char yaml_src[] = "key: value\n";

    struct fy_document *fyd =
        fy_document_build_from_string(nullptr, yaml_src,
                                      sizeof(yaml_src) - 1);
    if (!fyd) {
        std::fprintf(stderr, "fy_document_build_from_string failed\n");
        return 1;
    }

    /* Look up the scalar at /key and verify its value. */
    char buf[64] = {};
    int rc = fy_document_scanf(fyd, "/key %63s", buf);
    fy_document_destroy(fyd);

    if (rc != 1) {
        std::fprintf(stderr, "fy_document_scanf returned %d, expected 1\n", rc);
        return 1;
    }
    if (std::strcmp(buf, "value") != 0) {
        std::fprintf(stderr, "unexpected value: '%s'\n", buf);
        return 1;
    }

    return 0;
}
