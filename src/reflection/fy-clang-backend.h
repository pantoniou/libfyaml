/*
 * fy-clang-backend.h - Clang reflection backend header
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_CLANG_BACKEND_H
#define FY_CLANG_BACKEND_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>

struct fy_clang_backend_reflection_cfg {
	bool display_diagnostics;
};

struct fy_clang_backend_import_cfg {
	const char *file;
	int argc;
	const char * const *argv;
};

#endif
