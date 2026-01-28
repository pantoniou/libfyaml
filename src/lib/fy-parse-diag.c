/*
 * fy-parse-diag.c - parser diagnostics
 *
 * Copyright (c) 2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>

#include <libfyaml.h>

#include "fy-diag.h"
#include "fy-parse.h"

int fy_parser_vdiag(struct fy_parser *fyp, unsigned int flags,
		    const char *file, int line, const char *func,
		    const char *fmt, va_list ap)
{
	enum fy_error_type level;
	enum fy_error_module module;
	struct fy_diag_ctx fydc;
	int rc;

	if (!fyp || !fyp->diag || !fmt)
		return -1;

	level = (flags & FYDF_LEVEL_MASK) >> FYDF_LEVEL_SHIFT;
	module = (flags & FYDF_MODULE_MASK) >> FYDF_MODULE_SHIFT;

	if (!fy_diag_log_level_is_enabled(fyp->diag, level, module))
		return 0;

	/* fill in fy_diag_ctx */
	memset(&fydc, 0, sizeof(fydc));

	fydc.level = level;
	fydc.module = module;
	fydc.source_file = file;
	fydc.source_line = line;
	fydc.source_func = func;
	fydc.line = fyp_line(fyp);
	fydc.column = fyp_column(fyp);

	rc = fy_vdiag(fyp->diag, &fydc, fmt, ap);

	if (fyp && !fyp->stream_error && fyp->diag->on_error)
		fyp->stream_error = true;

	return rc;
}

int fy_parser_diag(struct fy_parser *fyp, unsigned int flags,
	    const char *file, int line, const char *func,
	    const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = fy_parser_vdiag(fyp, flags, file, line, func, fmt, ap);
	va_end(ap);

	return rc;
}

void fy_parser_diag_vreport(struct fy_parser *fyp,
		            const struct fy_diag_report_ctx *fydrc,
			    const char *fmt, va_list ap)
{
	struct fy_diag *diag;

	if (!fyp || !fyp->diag || !fydrc || !fmt)
		return;

	diag = fyp->diag;

	fy_diag_vreport(diag, fydrc, fmt, ap);

	if (fyp && !fyp->stream_error && diag->on_error)
		fyp->stream_error = true;
}

void fy_parser_diag_report(struct fy_parser *fyp,
			   const struct fy_diag_report_ctx *fydrc,
			   const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_parser_diag_vreport(fyp, fydrc, fmt, ap);
	va_end(ap);
}

void fy_parser_vlog(struct fy_parser *fyp, enum fy_error_type type,
		    const char *fmt, va_list ap)
{
	struct fy_diag_ctx ctx = {
		.level = type,
		.module = FYEM_UNKNOWN,
		.source_func = "",
		.source_file = "",
		.source_line = 0,
		.file = NULL,
		.line = 0,
		.column = 0,
	};

	if (!fyp || !fyp->diag)
		return;

	(void)fy_vdiag(fyp->diag, &ctx, fmt, ap);
}

void fy_parser_log(struct fy_parser *fyp, enum fy_error_type type,
		   const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_parser_vlog(fyp, type, fmt, ap);
	va_end(ap);
}

void fy_parser_vreport(struct fy_parser *fyp, enum fy_error_type type,
		       struct fy_token *fyt, const char *fmt, va_list ap)
{
	struct fy_diag_report_ctx fydrc_local, *fydrc = &fydrc_local;

	if (!fyp || !fyp->diag || !fyt)
		return;

	memset(fydrc, 0, sizeof(*fydrc));
	fydrc->type = type;
	fydrc->module = FYEM_UNKNOWN;
	fydrc->fyt = fy_token_ref(fyt);

	fy_parser_diag_vreport(fyp, fydrc, fmt, ap);
}

void fy_parser_report(struct fy_parser *fyp, enum fy_error_type type,
		      struct fy_token *fyt, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_parser_vreport(fyp, type, fyt, fmt, ap);
	va_end(ap);
}

