/*
 * fy-null-backend.c - Null basic type backend
 *
 * Copyright (c) 2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <float.h>
#include <assert.h>
#include <limits.h>
#include <ctype.h>

#if HAVE_ALLOCA
#include <alloca.h>
#endif

#include "fy-reflection-private.h"

#include "fy-packed-backend.h"

static int null_reflection_setup(struct fy_reflection *rfl);
static void null_reflection_cleanup(struct fy_reflection *rfl);

static int null_import_setup(struct fy_import *imp, const void *user);
static void null_import_cleanup(struct fy_import *imp);

static int null_decl_setup(struct fy_decl *decl, void *user);
static void null_decl_cleanup(struct fy_decl *decl);

static int null_type_setup(struct fy_type *ft, void *user);
static void null_type_cleanup(struct fy_type *ft);

static const struct fy_reflection_backend_ops null_ops = {
	.reflection_setup = null_reflection_setup,
	.reflection_cleanup = null_reflection_cleanup,
	.import_setup = null_import_setup,
	.import_cleanup = null_import_cleanup,
	.type_setup = null_type_setup,
	.type_cleanup = null_type_cleanup,
	.decl_setup = null_decl_setup,
	.decl_cleanup = null_decl_cleanup,
};

const struct fy_reflection_backend fy_reflection_null_backend = {
	.name = "null",
	.ops = &null_ops,
};

static int null_reflection_setup(struct fy_reflection *rfl)
{
	/* nothing */
	return 0;
}

static void null_reflection_cleanup(struct fy_reflection *rfl)
{
	/* nothing */
}

static int null_import_setup(struct fy_import *imp, const void *user)
{
	assert(imp);

	/* forward */
	imp->name = "null";

	return 0;
}

static void null_import_cleanup(struct fy_import *imp)
{
	/* nothing */
}

static int null_decl_setup(struct fy_decl *decl, void *user)
{
	/* nothing */
	return 0;
}

static void null_decl_cleanup(struct fy_decl *decl)
{
	/* nothing */
}

static int null_type_setup(struct fy_type *ft, void *user)
{
	/* nothing */
	return 0;
}

static void null_type_cleanup(struct fy_type *ft)
{
	/* nothing */
}
