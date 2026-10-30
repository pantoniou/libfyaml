/*
 * cparse-target.c - C declaration front end: target ABIs
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * The default target is "native": its ABI is filled in from what the compiler
 * cparse was built with says (sizeof, alignment, member offsets, the signedness
 * of char), so it is right on any host. The others, for cross targets, are a
 * table of the ones that matter; adding one is an entry (plus a layout rule if it
 * has an odd one). The values are what the psABIs of the Linux targets say; the
 * test cases compare the layouts with libclang.
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <stdalign.h>
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>

#include "cparse-arch.h"
#include "cparse-private.h"

#define ABI(_s, _a)	{ (_s), (_a) }

/*
 * The Linux ABIs of x86-64 and AArch64. Fil-C has the same layout for its
 * x86_64-filc and aarch64-filc targets: a pointer is 8 bytes (the capability is
 * not in the object).
 */
#define CP_ABI_X86_64_LP64 \
		.ptr = ABI(8, 8), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4), \
		.long_ = ABI(8, 8), .llong_ = ABI(8, 8), .int128 = ABI(16, 16), \
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(16, 16), \
		.float16 = ABI(2, 2), .float128 = ABI(16, 16), \
		.char_unsigned = false, .has_int128 = true, .va_list = CPVA_X86_64, \
		.member_align_cap = 0, .max_vector_align = 0,

#define CP_ABI_AARCH64_LP64 \
		.ptr = ABI(8, 8), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4), \
		.long_ = ABI(8, 8), .llong_ = ABI(8, 8), .int128 = ABI(16, 16), \
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(16, 16), \
		.float16 = ABI(2, 2), .float128 = ABI(16, 16), \
		.char_unsigned = true, .has_int128 = true, .va_list = CPVA_AARCH64, \
		.unnamed_bitfield_align = true, .member_align_cap = 0, .max_vector_align = 16,

/*
 * WebAssembly: ILP32 (wasm32) or LP64 (wasm64), a 16 byte IEEE quad long
 * double, and a plain char that is signed. Emscripten and WASI have the same
 * ABI but for one thing: Emscripten aligns the long double and __float128 to 8
 * bytes, for compatibility with its older versions.
 */
#define CP_ABI_WASM32(_quad_align) \
	.ptr = ABI(4, 4), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4), \
	.long_ = ABI(4, 4), .llong_ = ABI(8, 8), .int128 = ABI(16, 16), \
	.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(16, _quad_align), \
	.float16 = ABI(2, 2), .float128 = ABI(16, _quad_align), \
	.char_unsigned = false, .has_int128 = true, .va_list = CPVA_PTR_VOID, \
	.member_align_cap = 0, .max_vector_align = 0,

#define CP_ABI_WASM64 \
	.ptr = ABI(8, 8), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4), \
	.long_ = ABI(8, 8), .llong_ = ABI(8, 8), .int128 = ABI(16, 16), \
	.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(16, 16), \
	.float16 = ABI(2, 2), .float128 = ABI(16, 16), \
	.char_unsigned = false, .has_int128 = true, .va_list = CPVA_PTR_VOID, \
	.member_align_cap = 0, .max_vector_align = 0,

static const struct cp_target targets[] = {
	{
		.name = "x86_64", .triple = "x86_64-pc-linux-gnu",
		CP_ABI_X86_64_LP64
	}, {
		.name = "aarch64", .triple = "aarch64-unknown-linux-gnu",
		CP_ABI_AARCH64_LP64
	}, {
		.name = "riscv64", .triple = "riscv64-unknown-linux-gnu",
		.ptr = ABI(8, 8), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(8, 8), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(16, 16),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = true, .has_int128 = true, .va_list = CPVA_PTR_VOID,
		.member_align_cap = 0, .max_vector_align = 0,
	}, {
		.name = "i386", .triple = "i386-pc-linux-gnu",
		.ptr = ABI(4, 4), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(4, 4), .llong_ = ABI(8, 4), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 4), .ldouble = ABI(12, 4),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = false, .has_int128 = false, .va_list = CPVA_PTR_CHAR,
		.member_align_cap = 4, .max_vector_align = 0,
	}, {
		.name = "arm", .triple = "armv7-unknown-linux-gnueabihf",
		.ptr = ABI(4, 4), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(4, 4), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(8, 8),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = true, .has_int128 = false, .va_list = CPVA_ARM,
		.unnamed_bitfield_align = true, .member_align_cap = 0, .max_vector_align = 8,
	}, {
		.name = "riscv32", .triple = "riscv32-unknown-linux-gnu",
		.ptr = ABI(4, 4), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(4, 4), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(16, 16),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = true, .has_int128 = false, .va_list = CPVA_PTR_VOID,
		.member_align_cap = 0, .max_vector_align = 0,
	}, {
		.name = "ppc64le", .triple = "powerpc64le-unknown-linux-gnu",
		.ptr = ABI(8, 8), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(8, 8), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(16, 16),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = true, .has_int128 = true, .va_list = CPVA_PTR_CHAR,
		.member_align_cap = 0, .max_vector_align = 0,
	}, {
		.name = "ppc64", .triple = "powerpc64-unknown-linux-gnu",
		.ptr = ABI(8, 8), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(8, 8), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(16, 16),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = true, .has_int128 = true, .va_list = CPVA_PTR_CHAR,
		.member_align_cap = 0, .max_vector_align = 0,
	}, {
		.name = "s390x", .triple = "s390x-unknown-linux-gnu",
		.ptr = ABI(8, 8), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(8, 8), .llong_ = ABI(8, 8), .int128 = ABI(16, 8),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(16, 8),
		.float16 = ABI(2, 2), .float128 = ABI(16, 8),
		.char_unsigned = true, .has_int128 = true, .va_list = CPVA_S390X,
		.member_align_cap = 0, .max_vector_align = 8,
	}, {
		.name = "loongarch64", .triple = "loongarch64-unknown-linux-gnu",
		.ptr = ABI(8, 8), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(8, 8), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(16, 16),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = false, .has_int128 = true, .va_list = CPVA_PTR_VOID,
		.member_align_cap = 0, .max_vector_align = 0,
	}, {
		.name = "mips", .triple = "mipsel-unknown-linux-gnu",
		.ptr = ABI(4, 4), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(4, 4), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(8, 8),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = false, .has_int128 = false, .va_list = CPVA_PTR_VOID,
		.member_align_cap = 0, .max_vector_align = 0,
	}, {
		.name = "mips64", .triple = "mips64el-unknown-linux-gnuabi64",
		.ptr = ABI(8, 8), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(8, 8), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(16, 16),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = false, .has_int128 = true, .va_list = CPVA_PTR_VOID,
		.member_align_cap = 0, .max_vector_align = 0,
	}, {
		.name = "x86_64-windows", .triple = "x86_64-pc-windows-msvc",
		.ptr = ABI(8, 8), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(4, 4), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(8, 8),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = false, .has_int128 = true, .va_list = CPVA_PTR_CHAR,
		.ms_layout = true, .msvc = true, .member_align_cap = 0, .max_vector_align = 0,
	}, {
		.name = "aarch64-windows", .triple = "aarch64-pc-windows-msvc",
		.ptr = ABI(8, 8), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(4, 4), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(8, 8),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = false, .has_int128 = true, .va_list = CPVA_PTR_CHAR,
		.ms_layout = true, .msvc = true, .member_align_cap = 0, .max_vector_align = 16,
	}, {
		.name = "i386-windows", .triple = "i686-pc-windows-msvc",
		.ptr = ABI(4, 4), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(4, 4), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(8, 8),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = false, .has_int128 = false, .va_list = CPVA_PTR_CHAR,
		.ms_layout = true, .msvc = true, .member_align_cap = 0, .max_vector_align = 0,
	}, {
		.name = "x86_64-mingw", .triple = "x86_64-w64-mingw32",
		.ptr = ABI(8, 8), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(4, 4), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(16, 16),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = false, .has_int128 = true, .va_list = CPVA_PTR_CHAR,
		.ms_layout = true, .mingw = true, .member_align_cap = 0, .max_vector_align = 0,
	}, {
		.name = "aarch64-mingw", .triple = "aarch64-w64-mingw32",
		.ptr = ABI(8, 8), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(4, 4), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(8, 8),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = false, .has_int128 = true, .va_list = CPVA_PTR_CHAR,
		.ms_layout = true, .mingw = true, .member_align_cap = 0, .max_vector_align = 16,
	}, {
		.name = "i386-mingw", .triple = "i686-w64-mingw32",
		.ptr = ABI(4, 4), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(4, 4), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(12, 4),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = false, .has_int128 = false, .va_list = CPVA_PTR_CHAR,
		.ms_layout = true, .mingw = true, .member_align_cap = 0, .max_vector_align = 0,
	}, {
		.name = "x86_64-darwin", .triple = "x86_64-apple-macosx",
		.ptr = ABI(8, 8), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(8, 8), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(16, 16),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = false, .has_int128 = true, .va_list = CPVA_X86_64,
		.member_align_cap = 0, .max_vector_align = 16,
	}, {
		.name = "aarch64-darwin", .triple = "arm64-apple-macosx",
		.ptr = ABI(8, 8), .bool_ = ABI(1, 1), .short_ = ABI(2, 2), .int_ = ABI(4, 4),
		.long_ = ABI(8, 8), .llong_ = ABI(8, 8), .int128 = ABI(16, 16),
		.float_ = ABI(4, 4), .double_ = ABI(8, 8), .ldouble = ABI(8, 8),
		.float16 = ABI(2, 2), .float128 = ABI(16, 16),
		.char_unsigned = false, .has_int128 = true, .va_list = CPVA_PTR_CHAR,
		.member_align_cap = 0, .max_vector_align = 16,
	}, {
		.name = "x86_64-filc", .triple = "x86_64-unknown-linux-filc",
		CP_ABI_X86_64_LP64
	}, {
		.name = "aarch64-filc", .triple = "aarch64-unknown-linux-filc",
		CP_ABI_AARCH64_LP64
	}, {
		.name = "wasm32", .triple = "wasm32-unknown-unknown",
		CP_ABI_WASM32(16)
	}, {
		.name = "wasm32-wasi", .triple = "wasm32-unknown-wasi",
		CP_ABI_WASM32(16)
	}, {
		.name = "wasm32-emscripten", .triple = "wasm32-unknown-emscripten",
		CP_ABI_WASM32(8)
	}, {
		.name = "wasm64", .triple = "wasm64-unknown-unknown",
		CP_ABI_WASM64
	}
};

#define NATIVE_ABI(_t)	((struct cp_type_abi) ABI((int)sizeof(_t), (int)alignof(_t)))

/* the alignment a scalar gets as a struct member, which a few ABIs make smaller */
#define MEMBER_ALIGN(_t) \
	((int)offsetof(struct { char c; _t v; }, v))

static struct cp_target native;
static bool native_ready;

static int min_member_cap(int cap, int type_align, int member_align)
{
	if (member_align < type_align && (!cap || member_align < cap))
		return member_align;
	return cap;
}

/* the table entry of an architecture */
static const struct cp_target *entry_by_name(const char *name)
{
	size_t i;

	for (i = 0; name && i < ARRAY_SIZE(targets); i++)
		if (!strcmp(targets[i].name, name))
			return &targets[i];
	return NULL;
}

/* the size of a va_list of a kind, 0 when it depends on the pointer size */
static size_t va_list_bytes(enum cp_va_list_kind kind)
{
	switch (kind) {
	case CPVA_X86_64: return 24;
	case CPVA_AARCH64: return 32;
	case CPVA_S390X: return 32;
	case CPVA_ARM: return 4;
	default: return sizeof(void *);
	}
}

const char *cp_target_arch(const struct cp_target *t)
{
	if (strcmp(t->name, "native"))
		return t->name;
#ifdef CP_HOST_ARCH
	return CP_HOST_ARCH;
#else
	return NULL;
#endif
}

const struct cp_target *cp_target_host(void)
{
	struct cp_target *t = &native;
	const struct cp_target *e;
	int cap = 0;

	if (native_ready)
		return t;

	memset(t, 0, sizeof(*t));
	t->name = "native";
	/* what the table knows of the architecture; the sizes come from the compiler */
	e = entry_by_name(cp_target_arch(t));
	t->triple = e ? e->triple : "native";
	t->ms_layout = e && e->ms_layout;
	t->msvc = e && e->msvc;
	t->mingw = e && e->mingw;

	t->ptr = NATIVE_ABI(void *);
	t->bool_ = NATIVE_ABI(_Bool);
	t->short_ = NATIVE_ABI(short);
	t->int_ = NATIVE_ABI(int);
	t->long_ = NATIVE_ABI(long);
	t->llong_ = NATIVE_ABI(long long);
	t->float_ = NATIVE_ABI(float);
	t->double_ = NATIVE_ABI(double);
	t->ldouble = NATIVE_ABI(long double);
#ifdef __SIZEOF_INT128__
	t->has_int128 = true;
	t->int128 = NATIVE_ABI(__int128);
#else
	t->int128 = (struct cp_type_abi) ABI(16, 16);
#endif
	/* the compiler may not have these types: the usual layout */
	t->float16 = (struct cp_type_abi) ABI(2, 2);
	t->float128 = (struct cp_type_abi) ABI(16, 16);

	t->char_unsigned = (char)-1 > 0;

	/* the va_list of the architecture, when the compiler agrees on its size */
	if (e && va_list_bytes(e->va_list) == sizeof(va_list))
		t->va_list = e->va_list;
	else
		t->va_list = sizeof(va_list) == sizeof(void *) ? CPVA_PTR_VOID : CPVA_X86_64;

	/* a scalar aligned less as a member than on its own (i386: double, long long) */
	cap = min_member_cap(cap, (int)alignof(double), MEMBER_ALIGN(double));
	cap = min_member_cap(cap, (int)alignof(long long), MEMBER_ALIGN(long long));
	cap = min_member_cap(cap, (int)alignof(long double), MEMBER_ALIGN(long double));
	t->member_align_cap = cap;
	t->unnamed_bitfield_align = alignof(struct { char c; int : 3; }) > 1;

	/* a vector is aligned to its size, up to what the architecture allows */
	t->max_vector_align = e ? e->max_vector_align : 0;

	native_ready = true;
	return t;
}

/*
 * The operating system of a target triple. The table has System V style ABIs for
 * Linux, the BSDs and bare ELF, the Windows ABIs of MSVC and MinGW, and macOS
 * (the iOS triples give the same ABI), Fil-C (an environment named filc, for
 * x86-64 and AArch64 only), and WebAssembly (wasm32 and wasm64, with the systems
 * wasi and emscripten). Another operating system, such as Cygwin,
 * has other sizes and layout rules, and its triple is not accepted.
 */
enum os_kind {
	OS_SYSV,
	OS_MSVC,
	OS_MINGW,
	OS_DARWIN,
	OS_FILC,
	OS_WASI,
	OS_EMSCRIPTEN,
	OS_UNKNOWN,
};

static enum os_kind triple_os(const char *triple)
{
	static const char *const systems[] = { "linux", "freebsd", "netbsd", "openbsd", "elf" };
	static const char *const neutral[] = { "unknown", "pc", "none" };
	const char *p = strchr(triple, '-');
	size_t len, i;
	bool sysv = false, windows = false, mingw = false, darwin = false, filc = false;
	bool wasi = false, emscripten = false, other = false, match;

	/* a triple that holds only the architecture */
	if (!p)
		return OS_SYSV;

	for (; p; p = strchr(p, '-')) {
		p++;
		len = strcspn(p, "-");

		if (!strncmp(p, "windows", 7) || !strncmp(p, "win32", 5)) {
			windows = true;
			continue;
		}
		if (!strncmp(p, "darwin", 6) || !strncmp(p, "macos", 5) || !strncmp(p, "ios", 3)) {
			darwin = true;
			continue;
		}
		if (len == 5 && !strncmp(p, "apple", 5))
			continue;
		if (len == 4 && !strncmp(p, "filc", 4)) {
			filc = true;
			continue;
		}
		if (!strncmp(p, "wasi", 4)) {	/* wasi, wasip1, wasip2 */
			wasi = true;
			continue;
		}
		if (len == 10 && !strncmp(p, "emscripten", 10)) {
			emscripten = true;
			continue;
		}
		if (!strncmp(p, "mingw", 5)) {
			mingw = true;
			continue;
		}
		if (len == 4 && !strncmp(p, "msvc", 4))
			continue;
		if (windows && len == 3 && !strncmp(p, "gnu", 3)) {
			mingw = true;
			continue;
		}

		match = false;
		for (i = 0; i < ARRAY_SIZE(systems); i++)
			if (!strncmp(p, systems[i], strlen(systems[i])))
				match = true;
		if (match) {
			sysv = true;
			continue;
		}

		for (i = 0; i < ARRAY_SIZE(neutral); i++)
			if (len == strlen(neutral[i]) && !strncmp(p, neutral[i], len))
				match = true;
		if (!match)
			other = true;	/* the environment of a Linux triple too: gnu, musl */
	}
	if (darwin)
		return OS_DARWIN;
	if (mingw)
		return OS_MINGW;
	if (windows)
		return OS_MSVC;
	if (filc)
		return OS_FILC;
	if (wasi)
		return OS_WASI;
	if (emscripten)
		return OS_EMSCRIPTEN;
	return sysv || !other ? OS_SYSV : OS_UNKNOWN;
}

/* the name in the table of the architecture a triple starts with, NULL if there is none */
static const char *triple_arch(const char *triple, size_t n)
{
	static const struct { const char *alias; const char *name; } aliases[] = {
		{ "x86_64", "x86_64" }, { "amd64", "x86_64" },
		{ "aarch64", "aarch64" }, { "arm64", "aarch64" },
		{ "riscv64", "riscv64" }, { "riscv32", "riscv32" },
		{ "powerpc64le", "ppc64le" }, { "ppc64le", "ppc64le" },
		{ "powerpc64", "ppc64" }, { "ppc64", "ppc64" },
		{ "s390x", "s390x" }, { "loongarch64", "loongarch64" },
		{ "wasm32", "wasm32" }, { "wasm64", "wasm64" },
		{ "mips", "mips" }, { "mipsel", "mips" },
		{ "mips64", "mips64" }, { "mips64el", "mips64" },
		{ "i386", "i386" },
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(aliases); i++)
		if (n == strlen(aliases[i].alias) && !strncmp(triple, aliases[i].alias, n))
			return aliases[i].name;

	/* i486, i586, i686 */
	if (n == 4 && triple[0] == 'i' && triple[1] >= '3' && triple[1] <= '6' &&
	    !strncmp(triple + 2, "86", 2))
		return "i386";
	/* arm, armv7, armv7a, thumb: 32 bit ARM with the hard float EABI */
	if ((n >= 3 && !strncmp(triple, "arm", 3) && (n == 3 || triple[3] == 'v')) ||
	    (n >= 5 && !strncmp(triple, "thumb", 5)))
		return "arm";
	return NULL;
}

const struct cp_target *cp_target_by_triple(const char *triple)
{
	const char *name;
	char full[32];
	enum os_kind os;
	size_t n = 0;

	if (!triple)
		return NULL;
	if (!strcmp(triple, "native") || !strcmp(triple, "host"))
		return cp_target_host();
	os = triple_os(triple);
	if (os == OS_UNKNOWN)
		return NULL;
	while (triple[n] && triple[n] != '-')
		n++;

	name = triple_arch(triple, n);
	if (!name)
		return NULL;
	if (os == OS_SYSV)
		return entry_by_name(name);

	/* the other entries are named after the architecture and the environment */
	if (snprintf(full, sizeof(full), "%s-%s", name,
		     os == OS_MINGW ? "mingw" : os == OS_DARWIN ? "darwin" :
		     os == OS_FILC ? "filc" : os == OS_WASI ? "wasi" :
		     os == OS_EMSCRIPTEN ? "emscripten" : "windows") >=
	    (int)sizeof(full))
		return NULL;
	return entry_by_name(full);
}

/* the table entry of the architecture of t (the native target is not in the table) */
static const struct cp_target *arch_entry(const struct cp_target *t)
{
	if (t >= targets && t < targets + ARRAY_SIZE(targets))
		return t;
	return cp_target_by_triple(t->triple);
}

/* the 32 and the 64 bit variants of the architectures that have both */
static const struct cp_target *variant(const struct cp_target *t, int bits)
{
	static const char *const pairs[][2] = {
		{ "i386", "x86_64" }, { "riscv32", "riscv64" }, { "mips", "mips64" },
		{ "i386-windows", "x86_64-windows" }, { "i386-mingw", "x86_64-mingw" },
		{ "wasm32", "wasm64" },
	};
	const struct cp_target *e;
	size_t i;

	if (t->ptr.size * 8 == bits)
		return t;
	e = arch_entry(t);
	if (!e)
		return NULL;
	for (i = 0; i < ARRAY_SIZE(pairs); i++)
		if (!strcmp(e->name, pairs[i][bits == 32 ? 1 : 0]))
			return entry_by_name(pairs[i][bits == 32 ? 0 : 1]);
	return NULL;
}

static bool arg_is(const char *arg, const char *s)
{
	return !strcmp(arg, s);
}

int cp_target_from_args(const char *default_triple, int argc, const char *const *argv,
			struct cp_target *out, const char **errp)
{
	const struct cp_target *t = NULL;
	const char *triple = default_triple, *bad = NULL;
	int bits = 0, char_signed = -1, i;
	const char *a;

	/* the last -target wins, then -m32/-m64 modify what it chose */
	for (i = 0; i < argc; i++) {
		a = argv[i];

		if (arg_is(a, "-target") && i + 1 < argc)
			triple = argv[++i];
		else if (!strncmp(a, "--target=", 9))
			triple = a + 9;
		else if (arg_is(a, "-m32"))
			bits = 32;
		else if (arg_is(a, "-m64"))
			bits = 64;
		else if (arg_is(a, "-funsigned-char"))
			char_signed = 0;
		else if (arg_is(a, "-fsigned-char"))
			char_signed = 1;
	}

	if (triple) {
		t = cp_target_by_triple(triple);
		if (!t) {
			bad = triple;
			goto err;
		}
	} else {
		t = cp_target_host();
	}

	if (bits == 32) {
		t = variant(t, 32);
		if (!t) {
			bad = "-m32 for this architecture";
			goto err;
		}
	} else if (bits == 64) {
		t = variant(t, 64);
		if (!t) {
			bad = "-m64 for this architecture";
			goto err;
		}
	}

	*out = *t;
	if (char_signed >= 0)
		out->char_unsigned = !char_signed;
	return 0;

err:
	if (errp)
		*errp = bad;
	return -1;
}

int cp_target_args_filter(int argc, const char *const *argv, const char **out, int max)
{
	int i, n = 0;
	const char *a;
	bool two, one;

	for (i = 0; i < argc; i++) {
		a = argv[i];
		two = arg_is(a, "-target");
		one = !strncmp(a, "--target=", 9) || arg_is(a, "-m32") || arg_is(a, "-m64") ||
			   arg_is(a, "-funsigned-char") || arg_is(a, "-fsigned-char");

		if (!one && !two)
			continue;
		if (n + (two ? 2 : 1) > max)
			break;
		out[n++] = a;
		if (two && i + 1 < argc)
			out[n++] = argv[++i];
	}
	return n;
}
