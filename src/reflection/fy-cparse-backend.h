/*
 * fy-cparse-backend.h - cparse reflection backend header
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_CPARSE_BACKEND_H
#define FY_CPARSE_BACKEND_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>
#include <stddef.h>

struct fy_cparse_backend_reflection_cfg {
	bool display_diagnostics;
	/*
	 * The compiler whose predefined macros and system include directories the
	 * backend uses, "gcc" or "clang". The macros are compiled in: the ones of the
	 * compiler that built the library, and the ones of a recent clang for the
	 * other compilers. NULL: the compiler that built the library. The backend
	 * does not run a compiler.
	 */
	const char *compiler;
	/* use only the -D and -I of the arguments */
	bool no_predefs;
};

struct fy_cparse_backend_import_cfg {
	const char *file;
	int argc;
	const char * const *argv;
	/*
	 * Files with the macros of a compiler that is not built in, for example
	 * the output of `cc -dM -E -x c /dev/null` of the compiler that builds the
	 * program. The backend reads them for their macros after the compiled-in
	 * ones and before the file. They have the same effect as the option
	 * -imacros in argv. Set no_predefs in the reflection configuration to read
	 * only these macros.
	 */
	const char * const *macro_files;
	size_t num_macro_files;
};

#endif
