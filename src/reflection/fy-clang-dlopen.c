/*
 * fy-clang-dlopen.c - load libclang on the first use of the clang backend
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define FY_CLANG_DLOPEN_IMPLEMENTATION
#include "fy-clang-dlopen.h"

#ifdef FY_LIBCLANG_DLOPEN

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Linking libclang makes the dynamic loader initialise libclang and LLVM in
 * every process that loads libfyaml, whether it uses reflection or not. With
 * FY_LIBCLANG_DLOPEN the library keeps a table of the libclang functions that
 * the backend calls and fills it on the first use of the backend. Each name
 * below is then a member of that table. The list must hold every libclang
 * function that fy-clang-backend.c calls.
 *
 * Try the configured FY_LIBCLANG_PATH, names for FY_LIBCLANG_MAJOR, then
 * platform-specific library names. The environment variable
 * FY_LIBCLANG overrides this search.
 */

struct fy_clang_api fy_clang_api;
static pthread_once_t fy_clang_api_once = PTHREAD_ONCE_INIT;
static int fy_clang_api_rc = -1;

static void *fy_clang_api_open(void)
{
	static const char *const names[] = {
#ifdef FY_LIBCLANG_PATH
		FY_LIBCLANG_PATH,
#endif
#ifdef __APPLE__
		"libclang.dylib",
#else
#ifdef FY_LIBCLANG_MAJOR
		"libclang-" FY_LIBCLANG_MAJOR ".so." FY_LIBCLANG_MAJOR,
		"libclang-" FY_LIBCLANG_MAJOR ".so.1",
		"libclang.so." FY_LIBCLANG_MAJOR,
#endif
		"libclang.so.1", "libclang.so",
#endif
		NULL
	};
	const char *env = getenv("FY_LIBCLANG");
	void *h = NULL;
	int i;

	if (env && *env)
		return dlopen(env, RTLD_NOW | RTLD_LOCAL);
	for (i = 0; !h && names[i]; i++)
		h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
	return h;
}

static void fy_clang_api_init(void)
{
	struct fy_clang_api api;
	void *h, *sym;

	h = fy_clang_api_open();
	if (!h) {
		fprintf(stderr, "fyaml: cannot load libclang: %s\n", dlerror());
		return;
	}
#define X(n) \
	sym = dlsym(h, #n); \
	if (!sym) { \
		fprintf(stderr, "fyaml: libclang has no %s\n", #n); \
		dlclose(h); \
		return; \
	} \
	api.n = (__typeof__(api.n))sym;
	FY_CLANG_FUNCS(X)
#undef X
	fy_clang_api = api;
	fy_clang_api_rc = 0;
}

/* Return 0 when libclang is loaded, else -1. */
int fy_clang_api_load(void)
{
	pthread_once(&fy_clang_api_once, fy_clang_api_init);
	return fy_clang_api_rc;
}

#else

int fy_clang_api_load(void)
{
	return 0;
}

#endif
