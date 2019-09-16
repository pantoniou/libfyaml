/*
 * fy-diag.c - diagnostics
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <alloca.h>
#include <unistd.h>

#include <libfyaml.h>

#include "fy-parse.h"

static const char *fy_error_type_str(enum fy_error_type type)
{
	static const char *txt[] = {
		[FYET_DEBUG]   = "DBG",
		[FYET_INFO]    = "INF",
		[FYET_NOTICE]  = "NOT",
		[FYET_WARNING] = "WRN",
		[FYET_ERROR]   = "ERR",
	};

	if ((unsigned int)type >= FYET_MAX)
		return "*unknown*";
	return txt[type];
}

static const char *fy_error_module_str(enum fy_error_module module)
{
	static const char *txt[] = {
		[FYEM_UNKNOWN]	= "UNKWN",
		[FYEM_ATOM]	= "ATOM ",
		[FYEM_SCAN]	= "SCAN ",
		[FYEM_PARSE]	= "PARSE",
		[FYEM_DOC]	= "DOC  ",
		[FYEM_BUILD]	= "BUILD",
		[FYEM_INTERNAL]	= "INTRL",
		[FYEM_SYSTEM]	= "SYSTM",
	};

	if ((unsigned int)module >= FYEM_MAX)
		return "*unknown*";
	return txt[module];
}

int fy_vdiag(struct fy_parser *fyp, unsigned int flags,
	     const char *file, int line, const char *func,
	     const char *fmt, va_list ap)
{
	unsigned int pflags;
	unsigned int type, module;
	char *msg, *s;
	char *source = NULL, *position = NULL, *typestr = NULL, *modulestr = NULL;
	const char *file_stripped;
	int size, rc;
	int sizew __FY_DEBUG_UNUSED__;
	va_list ap_orig;
	FILE *fp;
	bool colorize;

	fp = fy_parser_get_error_fp(fyp);
	pflags = fy_parser_get_cfg_flags(fyp);

	type = (flags & FYDF_LEVEL_MASK) >> FYDF_LEVEL_SHIFT;
	if (type < ((pflags >> FYPCF_DEBUG_LEVEL_SHIFT) & FYPCF_DEBUG_LEVEL_MASK))
		return 0;

	module = (flags & FYDF_MODULE_MASK) >> FYDF_MODULE_SHIFT;
	if (!(pflags & (1U << (module + FYPCF_MODULE_SHIFT))))
		return 0;

	va_copy(ap_orig, ap);
	size = vsnprintf(NULL, 0, fmt, ap_orig);
	if (size == -1) {
		/* there is no way this happens */
		fputs("OUT OF MEMORY when printing diagnostrics!", fp);
		abort();
	}
	va_end(ap_orig);

	msg = alloca(size + 1);
	sizew = vsnprintf(msg, size + 1, fmt, ap);
	assert(size == sizew);	/* check for cosmic rays! */

	file_stripped = strrchr(file, '/');
	if (!file_stripped)
		file_stripped = file;

	/* strip any trailing new lines */
	s = msg + strlen(msg);
	while (s > msg && s[-1] == '\n')
		*--s = '\0';

	/* source part */
	if (pflags & FYPCF_DEBUG_DIAG_SOURCE) {
		fmt = "%s:%d @%s()";
		size = snprintf(NULL, 0, fmt, file_stripped, line, func);
		if (size == -1) {
			/* there is no way this happens */
			fputs("OUT OF MEMORY when printing diagnostrics!", fp);
			abort();
		}
		source = alloca(size + 3);
		sizew = snprintf(source, size + 1, fmt, file_stripped, line, func);
		assert(size == sizew);	/* check for cosmic rays! */
	}

	if ((pflags & FYPCF_COLOR(FYPCF_COLOR_MASK)) == FYPCF_COLOR_AUTO)
		colorize = isatty(fileno(fp));
	else if ((pflags & FYPCF_COLOR(FYPCF_COLOR_MASK)) == FYPCF_COLOR_FORCE)
		colorize = true;
	else if ((pflags & FYPCF_COLOR(FYPCF_COLOR_MASK)) == FYPCF_COLOR_NONE)
		colorize = false;
	else
		colorize = false;

	/* position part */
	if (fyp && (pflags & FYPCF_DEBUG_DIAG_POSITION)) {
		fmt = "<%3d:%2d>";
		size = snprintf(NULL, 0, fmt, fyp->line, fyp->column);
		if (size == -1) {
			/* there is no way this happens */
			fputs("OUT OF MEMORY when printing diagnostrics!", fp);
			abort();
		}
		position = alloca(size + 3);
		sizew = snprintf(position, size + 1, fmt, fyp->line, fyp->column);
		assert(size == sizew);	/* check for cosmic rays! */
	}

	/* type part */
	if (pflags & FYPCF_DEBUG_DIAG_TYPE) {
		fmt = "[%s]";
		size = snprintf(NULL, 0, fmt, fy_error_type_str(type));
		if (size == -1) {
			/* there is no way this happens */
			fputs("OUT OF MEMORY when printing diagnostrics!", fp);
			abort();
		}
		typestr = alloca(size + 3);
		sizew = snprintf(typestr, size + 1, fmt, fy_error_type_str(type));
		assert(size == sizew);	/* check for cosmic rays! */
	}

	/* module part */
	if (pflags & FYPCF_DEBUG_DIAG_MODULE) {
		fmt = "<%s>";
		size = snprintf(NULL, 0, fmt, fy_error_module_str(module));
		if (size == -1) {
			/* there is no way this happens */
			fputs("OUT OF MEMORY when printing diagnostrics!", fp);
			abort();
		}
		modulestr = alloca(size + 3);
		sizew = snprintf(modulestr, size + 1, fmt, fy_error_module_str(module));
		assert(size == sizew);	/* check for cosmic rays! */
	}

	if (position)
		strcat(position, ": ");
	if (typestr)
		strcat(typestr, ": ");
	if (modulestr)
		strcat(modulestr, ": ");

	if (colorize) {
		switch (type) {
		case FYET_DEBUG:
			fputs("\x1b[37m", fp);		/* normal white */
			break;
		case FYET_INFO:
			fputs("\x1b[37;1m", fp);	/* bright white */
			break;
		case FYET_NOTICE:
			fputs("\x1b[34;1m", fp);	/* bright blue */
			break;
		case FYET_WARNING:
			fputs("\x1b[33;1m", fp);	/* bright yellow */
			break;
		case FYET_ERROR:
			fputs("\x1b[31;1m", fp);	/* bright red */
			break;
		}
	}

	rc = fprintf(fp, "%*s" "%*s" "%*s" "%*s" "%s",
			source    ? 50 : 0, source    ? : "",
			position  ? 10 : 0, position  ? : "",
			typestr   ?  5 : 0, typestr   ? : "",
			modulestr ?  6 : 0, modulestr ? : "",
			msg);

	if (colorize)
		fputs("\x1b[0m", fp);

	fputs("\n", fp);

	return rc > 0 ? rc + 1 : rc;
}

int fy_diag(struct fy_parser *fyp, unsigned int flags,
	    const char *file, int line, const char *func,
	    const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = fy_vdiag(fyp, flags, file, line, func, fmt, ap);
	va_end(ap);

	return rc;
}
