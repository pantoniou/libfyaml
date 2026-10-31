/*
 * cparse-predefs.c - predefined macros of the compilers that cparse knows
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "cparse-private.h"

/* made by the build from the compiler that builds the library */
#include "cparse-predefs-host.inc"
/* made from clang, see the header of the file */
#include "cparse-predefs-clang.inc"
/* made from Fil-C, see the header of the file */
#include "cparse-predefs-filc.inc"

const struct cp_predef_set *cp_predefs_find(const char *compiler, const struct cp_target *t)
{
	const struct cp_predef_set *s;
	const char *arch = cp_target_arch(t);

	if (!arch)
		return NULL;

	for (s = cp_predef_sets_host; s->compiler; s++)
		if ((!compiler || !strcmp(compiler, s->compiler)) && !strcmp(s->arch, arch))
			return s;

	for (s = cp_predef_sets_targets; s->compiler; s++)
		if (!strcmp(s->arch, arch))
			return s;

	/* the Fil-C targets have their own architecture names */
	for (s = cp_predef_sets_filc; s->compiler; s++)
		if (!strcmp(s->arch, arch))
			return s;

	return NULL;
}
