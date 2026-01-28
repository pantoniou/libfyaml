/*
 * fy-input-diag.c - input and reader diagnostics
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
#include "fy-input.h"

int fy_reader_vdiag(struct fy_reader *fyr, unsigned int flags,
		    const char *file, int line, const char *func,
		    const char *fmt, va_list ap)
{
	struct fy_diag_ctx fydc;
	int fydc_level, fyd_level;

	if (!fyr || !fyr->diag || !fmt)
		return -1;

	/* perform the enable tests early to avoid the overhead */
	fydc_level = (flags & FYDF_LEVEL_MASK) >> FYDF_LEVEL_SHIFT;
	fyd_level = fyr->diag->cfg.level;

	if (fydc_level < fyd_level)
		return 0;

	/* fill in fy_diag_ctx */
	memset(&fydc, 0, sizeof(fydc));

	fydc.level = fydc_level;
	fydc.module = FYEM_SCAN;	/* reader is always scanner */
	fydc.source_file = file;
	fydc.source_line = line;
	fydc.source_func = func;
	fydc.line = fyr->line;
	fydc.column = fyr->column;

	return fy_vdiag(fyr->diag, &fydc, fmt, ap);
}

int fy_reader_diag(struct fy_reader *fyr, unsigned int flags,
		   const char *file, int line, const char *func,
		   const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = fy_reader_vdiag(fyr, flags, file, line, func, fmt, ap);
	va_end(ap);

	return rc;
}

void fy_reader_diag_vreport(struct fy_reader *fyr,
		            const struct fy_diag_report_ctx *fydrc,
			    const char *fmt, va_list ap)
{
	if (!fyr || !fyr->diag || !fydrc || !fmt)
		return;

	fy_diag_vreport(fyr->diag, fydrc, fmt, ap);
}

void fy_reader_diag_report(struct fy_reader *fyr,
			   const struct fy_diag_report_ctx *fydrc,
			   const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_reader_diag_vreport(fyr, fydrc, fmt, ap);
	va_end(ap);
}
