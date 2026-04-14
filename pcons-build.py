#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["pcons>=0.14.0"]
# ///
"""pcons build script for libfyaml — a YAML 1.2 parser/emitter library.

Ported from CMakeLists.txt. Builds the static library, shared library,
and fy-tool CLI.

Usage:
    uvx pcons          # configure + generate + build
"""

import os
from pathlib import Path

from pcons import Project, configure_file, find_c_toolchain, get_platform, get_variant
from pcons.configure.config import Configure
from pcons.configure.checks import ToolChecks
from pcons.core.subst import PathToken

# =============================================================================
# Configuration
# =============================================================================

project_dir = Path(__file__).parent
variant = get_variant("release")
build_dir = Path(os.environ.get("PCONS_BUILD_DIR", "build")) / variant
src_dir = project_dir / "src"
plat = get_platform()

# Read version from .tarball-version
version_file = project_dir / ".tarball-version"
if version_file.exists():
    PROJECT_VERSION = version_file.read_text().strip()
else:
    PROJECT_VERSION = "0.0.0"

config = Configure(build_dir=build_dir)
toolchain = find_c_toolchain()

if not config.get("configured") or os.environ.get("PCONS_RECONFIGURE"):
    toolchain.configure(config)
    config.set("configured", True)
    config.save()

project = Project("libfyaml", root_dir=project_dir, build_dir=build_dir)
env = project.Environment(toolchain=toolchain)
env.set_variant(variant)

# =============================================================================
# Feature detection
# =============================================================================

checks = ToolChecks(config, env, "cc")

# Headers
have_alloca_h = checks.check_header("alloca.h").success
have_byteswap_h = checks.check_header("byteswap.h").success

# Built-in functions
have_bswap16 = checks.check_function("__builtin_bswap16").success
have_bswap32 = checks.check_function("__builtin_bswap32").success
have_bswap64 = checks.check_function("__builtin_bswap64").success

# Library functions
have_qsort_r = checks.check_function("qsort_r").success
have_mremap = checks.check_function("mremap", headers=["sys/mman.h"]).success

# environ declaration
have_decl_environ = checks.try_compile("""
#include <unistd.h>
int main(void) { (void)environ; return 0; }
""").success

# Statement expressions (GCC/Clang extension)
have_statement_expressions = checks.try_compile("""
int main(void) {
    int x = ({ int y = 1; y + 1; });
    return x - 2;
}
""").success

# Compiler flags
have_wno_unused_function = checks.check_flag("-Wno-unused-function").success
have_wno_stringop_overflow = checks.check_flag("-Wno-stringop-overflow").success
have_wno_tautological = checks.check_flag(
    "-Wno-tautological-constant-out-of-range-compare"
).success
have_gnu2x = checks.check_flag("-std=gnu2x").success
have_c2x = checks.check_flag("-std=c2x").success
have_fblocks = checks.check_flag("-fblocks").success

# On non-Apple platforms, -fblocks requires linking libBlocksRuntime
need_blocks_runtime = False
if have_fblocks and not plat.is_macos:
    have_fblocks = checks.try_compile(
        '#include <Block.h>\nint main(void) { void (^b)(void) = ^{ }; b(); return 0; }',
        extra_flags=["-fblocks", "-lBlocksRuntime"],
        link=True,
    ).success
    if have_fblocks:
        need_blocks_runtime = True

# Heap trampolines (GCC 14+)
have_heap_trampolines = checks.try_compile(
    """
void call(void (*fn)(void)) { fn(); }
int main(void) {
    int x = 0;
    void nested(void) { x = 1; }
    call(nested);
    return x ? 0 : 1;
}
""",
    extra_flags=["-ftrampoline-impl=heap"],
).success

# SIMD detection
is_x86_64 = plat.arch == "x86_64"
is_arm64 = plat.arch == "arm64"
is_arm32 = plat.arch == "arm"

target_has_sse2 = is_x86_64 and checks.try_compile(
    '#include <emmintrin.h>\nint main(void) { __m128i x = _mm_setzero_si128(); (void)x; return 0; }',
    extra_flags=["-msse2"],
).success

target_has_sse41 = is_x86_64 and checks.try_compile(
    '#include <smmintrin.h>\nint main(void) { __m128i x = _mm_setzero_si128(); (void)x; return 0; }',
    extra_flags=["-msse4.1"],
).success

target_has_avx2 = is_x86_64 and checks.try_compile(
    '#include <immintrin.h>\nint main(void) { __m256i x = _mm256_setzero_si256(); (void)x; return 0; }',
    extra_flags=["-mavx2"],
).success

target_has_avx512 = is_x86_64 and checks.try_compile(
    '#include <immintrin.h>\nint main(void) { __m512i x = _mm512_setzero_si512(); (void)x; return 0; }',
    extra_flags=["-mavx512f", "-mavx512vl"],
).success

target_has_neon = (is_arm64 or is_arm32) and checks.try_compile(
    '#include <arm_neon.h>\nint main(void) { int32x4_t x = vdupq_n_s32(0); (void)x; return 0; }'
).success

# Generic subsystem: needs statement expressions + 64-bit little-endian
# (We detect little-endian via a compile check)
is_little_endian = checks.try_compile("""
#include <stdint.h>
int main(void) {
    uint32_t x = 1;
    return *((uint8_t*)&x) == 1 ? 0 : 1;
}
""", link=True).success

have_generic = have_statement_expressions and plat.is_64bit and is_little_endian
have_reflection = have_statement_expressions
# Skip libclang for now — optional and complex
have_libclang = False

config.save()

# =============================================================================
# Generate config.h
# =============================================================================

config_vars: dict[str, str] = {
    "HAVE_ALLOCA_H": "1" if have_alloca_h else "",
    "HAVE_BYTESWAP_H": "1" if have_byteswap_h else "",
    "HAVE___BUILTIN_BSWAP16": "1" if have_bswap16 else "",
    "HAVE___BUILTIN_BSWAP32": "1" if have_bswap32 else "",
    "HAVE___BUILTIN_BSWAP64": "1" if have_bswap64 else "",
    "HAVE_QSORT_R": "1" if have_qsort_r else "0",
    "HAVE_MREMAP": "1" if have_mremap else "0",
    "HAVE_DECL_ENVIRON": "1" if have_decl_environ else "",
    "HAVE_LIBYAML": "",
    "HAVE_CHECK": "",
    "HAVE_COMPATIBLE_CHECK": "",
    "HAVE_STATIC": "1",
    "HAVE_STATIC_TOOLS": "",
    "HAVE_ASAN": "",
    "HAVE_DEVMODE": "",
    "HAVE_PORTABLE_TARGET": "",
    "TARGET_HAS_SSE2": "1" if target_has_sse2 else "0",
    "TARGET_HAS_SSE41": "1" if target_has_sse41 else "0",
    "TARGET_HAS_AVX2": "1" if target_has_avx2 else "0",
    "TARGET_HAS_AVX512": "1" if target_has_avx512 else "0",
    "TARGET_HAS_NEON": "1" if target_has_neon else "0",
    "HAVE_GIT": "",
    "HAVE_JQ": "",
    "HAVE_LIBCLANG": "1" if have_libclang else "0",
    "HAVE_CLANG_BLOCKS": "1" if have_fblocks else "",
    "HAVE_HEAP_TRAMPOLINES": "1" if have_heap_trampolines else "0",
    "PROJECT_VERSION": PROJECT_VERSION,
}

configure_file(
    project_dir / "cmake" / "config.h.in",
    build_dir / "config.h",
    config_vars,
    strict=False,
)

# =============================================================================
# Compiler flags
# =============================================================================

# C standard
if have_gnu2x:
    env.cc.flags.append("-std=gnu2x")
elif have_c2x:
    env.cc.flags.append("-std=c2x")

# Warnings
env.cc.flags.extend(["-Wall", "-Wsign-compare", "-Wextra"])
if have_wno_unused_function:
    env.cc.flags.append("-Wno-unused-function")
if have_wno_stringop_overflow:
    env.cc.flags.append("-Wno-stringop-overflow")
if have_wno_tautological:
    env.cc.flags.append("-Wno-tautological-constant-out-of-range-compare")

# Visibility for shared libs
env.cc.flags.append("-fvisibility=hidden")

# Platform defines
common_defines = ["HAVE_CONFIG_H"]
if plat.is_posix:
    common_defines.append("_GNU_SOURCE")
if plat.is_windows:
    common_defines.extend(["WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS"])
if have_statement_expressions:
    common_defines.append("HAVE_STATEMENT_EXPRESSIONS")
if have_generic:
    common_defines.append("HAVE_GENERIC")
if have_reflection:
    common_defines.append("HAVE_REFLECTION")
if have_fblocks:
    env.cc.flags.append("-fblocks")
if have_heap_trampolines:
    env.cc.flags.append("-ftrampoline-impl=heap")
    common_defines.append("HAVE_HEAP_TRAMPOLINES")

for d in common_defines:
    env.cc.defines.append(d)

# Include directories
env.cc.includes.extend([
    project_dir / "include",
    build_dir,  # for config.h
    src_dir / "lib",
    src_dir / "util",
    src_dir / "xxhash",
    src_dir / "thread",
    src_dir / "allocator",
    src_dir / "blake3",
])
env.cc.includes.append(src_dir / "valgrind")
if have_generic:
    env.cc.includes.append(src_dir / "generic")
if have_reflection:
    env.cc.includes.append(src_dir / "reflection")

# =============================================================================
# Source files
# =============================================================================

lib_sources: list[Path] = [
    src_dir / "lib" / "fy-accel.c",
    src_dir / "lib" / "fy-atom.c",
    src_dir / "lib" / "fy-composer.c",
    src_dir / "lib" / "fy-diag.c",
    src_dir / "lib" / "fy-doc.c",
    src_dir / "lib" / "fy-docbuilder.c",
    src_dir / "lib" / "fy-docstate.c",
    src_dir / "lib" / "fy-dump.c",
    src_dir / "lib" / "fy-emit.c",
    src_dir / "lib" / "fy-event.c",
    src_dir / "lib" / "fy-input.c",
    src_dir / "lib" / "fy-parse.c",
    src_dir / "lib" / "fy-path.c",
    src_dir / "lib" / "fy-token.c",
    src_dir / "lib" / "fy-types.c",
    src_dir / "lib" / "fy-walk.c",
    src_dir / "lib" / "fy-composer-diag.c",
    src_dir / "lib" / "fy-doc-diag.c",
    src_dir / "lib" / "fy-docbuilder-diag.c",
    src_dir / "lib" / "fy-input-diag.c",
    src_dir / "lib" / "fy-parse-diag.c",
    src_dir / "util" / "fy-blob.c",
    src_dir / "util" / "fy-ctype.c",
    src_dir / "util" / "fy-utf8.c",
    src_dir / "util" / "fy-utils.c",
    src_dir / "xxhash" / "xxhash.c",
    src_dir / "thread" / "fy-thread.c",
    src_dir / "allocator" / "fy-allocator.c",
    src_dir / "allocator" / "fy-allocator-linear.c",
    src_dir / "allocator" / "fy-allocator-malloc.c",
    src_dir / "allocator" / "fy-allocator-mremap.c",
    src_dir / "allocator" / "fy-allocator-dedup.c",
    src_dir / "allocator" / "fy-allocator-auto.c",
    src_dir / "blake3" / "blake3_host_state.c",
    src_dir / "blake3" / "blake3_backend.c",
    src_dir / "blake3" / "blake3_be_cpusimd.c",
    src_dir / "blake3" / "fy-blake3.c",
]

if have_generic:
    lib_sources.extend([
        src_dir / "generic" / "fy-generic.c",
        src_dir / "generic" / "fy-generic-decoder.c",
        src_dir / "generic" / "fy-generic-encoder.c",
        src_dir / "generic" / "fy-generic-op.c",
        src_dir / "generic" / "fy-generic-iter.c",
    ])

if have_reflection:
    lib_sources.extend([
        src_dir / "reflection" / "fy-reflection.c",
        src_dir / "reflection" / "fy-packed-backend.c",
        src_dir / "reflection" / "fy-null-backend.c",
        src_dir / "reflection" / "fy-registry.c",
        src_dir / "reflection" / "fy-type-meta.c",
        src_dir / "reflection" / "fy-meta-type-system.c",
        src_dir / "reflection" / "fy-type-context.c",
        src_dir / "reflection" / "fy-meta-serdes.c",
        src_dir / "reflection" / "fy-reflection-util.c",
    ])

# =============================================================================
# BLAKE3 SIMD variants
#
# blake3.c is compiled multiple times with different HASHER_SUFFIX and
# SIMD_DEGREE defines, plus variant-specific compiler flags.
# =============================================================================

obj_suffix = toolchain.get_object_suffix()
blake3_dir = src_dir / "blake3"

# BLAKE3 SIMD variant definitions: (name, sources, defines, extra_flags)
blake3_variants: list[tuple[str, list[Path], list[str], list[str]]] = []

blake3_variants.append(("portable", [
    blake3_dir / "blake3_portable.c",
    blake3_dir / "blake3.c",
], ["HASHER_SUFFIX=portable", "SIMD_DEGREE=1"], []))

if target_has_sse2:
    blake3_variants.append(("sse2", [
        blake3_dir / "blake3_sse2.c",
        blake3_dir / "blake3_sse2_x86-64_unix.S",
        blake3_dir / "blake3.c",
    ], ["HASHER_SUFFIX=sse2", "SIMD_DEGREE=4"], ["-msse2"]))

if target_has_sse41:
    blake3_variants.append(("sse41", [
        blake3_dir / "blake3_sse41.c",
        blake3_dir / "blake3_sse41_x86-64_unix.S",
        blake3_dir / "blake3.c",
    ], ["HASHER_SUFFIX=sse41", "SIMD_DEGREE=4"], ["-msse4.1"]))

if target_has_avx2:
    blake3_variants.append(("avx2", [
        blake3_dir / "blake3_avx2.c",
        blake3_dir / "blake3_avx2_x86-64_unix.S",
        blake3_dir / "blake3.c",
    ], ["HASHER_SUFFIX=avx2", "SIMD_DEGREE=8"], ["-mavx2"]))

if target_has_avx512:
    blake3_variants.append(("avx512", [
        blake3_dir / "blake3_avx512.c",
        blake3_dir / "blake3_avx512_x86-64_unix.S",
        blake3_dir / "blake3.c",
    ], ["HASHER_SUFFIX=avx512", "SIMD_DEGREE=16"], ["-mavx512f", "-mavx512vl"]))

if target_has_neon:
    neon_flags = ["-mfpu=neon"] if is_arm32 else []
    blake3_variants.append(("neon", [
        blake3_dir / "blake3_neon.c",
        blake3_dir / "blake3.c",
    ], ["HASHER_SUFFIX=neon", "SIMD_DEGREE=4"], neon_flags))


def compile_blake3_variants(base_env: object, obj_dir: str) -> list:
    """Compile all BLAKE3 SIMD variants with the given environment.

    Each target (static, shared) needs its own objects so that the shared
    library gets -fPIC on platforms that require it.
    """
    objs: list = []
    for name, sources, defines, extra_flags in blake3_variants:
        with base_env.override() as variant_env:  # type: ignore[union-attr]
            for d in defines:
                variant_env.cc.defines.append(d)
            for f in extra_flags:
                variant_env.cc.flags.append(f)
            for src in sources:
                obj_name = f"blake3_{name}_{src.stem}{obj_suffix}"
                obj = variant_env.cc.Object(
                    build_dir / obj_dir / obj_name, src
                )[0]
                objs.append(obj)
    return objs


# =============================================================================
# Static library
# =============================================================================

blake3_static_objs = compile_blake3_variants(env, "blake3_static")

fyaml_static = project.StaticLibrary("fyaml", env, sources=lib_sources)
fyaml_static.add_sources(blake3_static_objs)
fyaml_static.public.include_dirs.append(project_dir / "include")

# Link pthreads on Unix
if plat.is_posix:
    fyaml_static.public.link_libs.append("pthread")
# Link libm on Linux (for trunc())
if plat.is_linux:
    fyaml_static.public.link_libs.append("m")
# Link BlocksRuntime on non-Apple platforms when using -fblocks
if need_blocks_runtime:
    fyaml_static.public.link_libs.append("BlocksRuntime")

# =============================================================================
# Shared library
# =============================================================================

blake3_shared_objs = compile_blake3_variants(env, "blake3_shared")

fyaml_shared = project.SharedLibrary("fyaml_shared", env, sources=lib_sources)
fyaml_shared.output_name = "fyaml"
fyaml_shared.add_sources(blake3_shared_objs)
fyaml_shared.public.include_dirs.append(project_dir / "include")

if plat.is_posix:
    fyaml_shared.public.link_libs.append("pthread")
if plat.is_linux:
    fyaml_shared.public.link_libs.append("m")
if need_blocks_runtime:
    fyaml_shared.public.link_libs.append("BlocksRuntime")

# =============================================================================
# fy-tool CLI
# =============================================================================

tool_sources: list[Path | object] = [
    src_dir / "tool" / "fy-tool.c",
    src_dir / "tool" / "fy-tool-dump.c",
]
if plat.is_windows:
    tool_sources.append(src_dir / "getopt" / "getopt.c")

tool_env = env.clone()
tool_env.cc.includes.append(src_dir / "tool")
if plat.is_windows:
    tool_env.cc.includes.append(src_dir / "getopt")

fy_tool = project.Program("fy-tool", tool_env, sources=tool_sources)
fy_tool.link(fyaml_static)

# =============================================================================
# Examples
# =============================================================================

examples_dir = project_dir / "examples"

generic_examples = [
    "intro-core-update",
    "intro-generic-update",
    "quick-start",
    "basic-parsing",
    "path-queries",
    "document-manipulation",
    "event-streaming",
    "build-from-scratch",
    "generic-literals",
    "generic-lambda-capture",
    "generic-parallel-transform",
    "generic-transform",
    "generic-roundtrip",
    "generic-adoption-bridge",
]

reflection_examples = ["reflection-packed"] if have_reflection else []

# libclang-backed reflection examples require libclang support
reflection_libclang_examples: list[str] = []
if have_libclang:
    reflection_libclang_examples = [
        "intro-reflection-update",
        "reflection-libclang",
        "reflection-export-packed",
    ]

all_examples = generic_examples + reflection_examples + reflection_libclang_examples
example_programs = []

for name in all_examples:
    prog = project.Program(name, env, sources=[examples_dir / f"{name}.c"])
    prog.link(fyaml_static)
    example_programs.append(prog)

# =============================================================================
# Test programs
# =============================================================================

test_dir = project_dir / "test"
test_programs: list = []


def link_whole_archive(prog: object, static_lib: object) -> None:
    """Link a program against a static library with whole-archive semantics.

    Needed when the program references internal symbols that would otherwise
    be stripped due to hidden visibility.
    """
    lib_file = f"lib{static_lib.name}.a"
    if plat.is_macos:
        prog.public.link_flags.append(
            PathToken(prefix="-Wl,-force_load,", path=lib_file, path_type="build")
        )
    else:
        prog.public.link_flags.extend([
            "-Wl,--whole-archive",
            PathToken(path=lib_file, path_type="build"),
            "-Wl,--no-whole-archive",
        ])


# --- libfyaml-test (requires the check unit testing framework) ---

check_pkg = project.find_package("check", required=False)
have_check = check_pkg is not None

if have_check:
    test_sources: list[Path] = [
        test_dir / "libfyaml-test.c",
        test_dir / "libfyaml-test-core.c",
        test_dir / "libfyaml-test-meta.c",
        test_dir / "libfyaml-test-emit.c",
        test_dir / "libfyaml-test-emit-bugs.c",
        test_dir / "libfyaml-test-parse-bugs.c",
        test_dir / "libfyaml-test-allocator.c",
        test_dir / "libfyaml-test-fuzzing.c",
        test_dir / "libfyaml-test-private.c",
        test_dir / "libfyaml-test-private-id.c",
        test_dir / "libfyaml-test-parser.c",
        test_dir / "libfyaml-test-thread.c",
    ]
    if have_generic:
        test_sources.extend([
            test_dir / "libfyaml-test-generic.c",
            test_dir / "libfyaml-test-generic-scalars.c",
        ])
    if have_reflection:
        test_sources.append(test_dir / "libfyaml-test-reflection.c")

    test_env = env.clone()
    test_env.cc.includes.append(src_dir / "check")

    libfyaml_test = project.Program("libfyaml-test", test_env, sources=test_sources)
    link_whole_archive(libfyaml_test, fyaml_static)
    libfyaml_test.link(fyaml_static)  # for public includes and link libs
    libfyaml_test.link(check_pkg)
    test_programs.append(libfyaml_test)

# --- Internal test tools (no check dependency) ---

internal_dir = src_dir / "internal"

internal_test_env = env.clone()
if plat.is_windows:
    internal_test_env.cc.includes.append(src_dir / "getopt")

for tool_name, tool_src in [
    ("fy-thread", "fy-thread.c"),
    ("fy-b3sum", "fy-b3sum.c"),
    ("fy-allocators", "fy-allocators.c"),
]:
    prog = project.Program(tool_name, internal_test_env, sources=[internal_dir / tool_src])
    link_whole_archive(prog, fyaml_static)
    prog.link(fyaml_static)
    test_programs.append(prog)

# =============================================================================
# Default targets and generate
# =============================================================================

project.Default(fyaml_static, fyaml_shared, fy_tool, *example_programs, *test_programs)
project.generate()
