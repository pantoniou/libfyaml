/*
 * fy-docbuilder-diag.c - document builder diagnostics
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
#include "fy-docbuilder.h"

int fy_document_builder_vdiag(struct fy_document_builder *fydb, unsigned int flags,
		      const char *file, int line, const char *func,
		      const char *fmt, va_list ap)
{
	struct fy_diag_ctx fydc;
	int rc;

	if (!fydb || !fmt || !fydb->cfg.diag)
		return -1;

	/* perform the enable tests early to avoid the overhead */
	if ((int)((flags & FYDF_LEVEL_MASK) >> FYDF_LEVEL_SHIFT) < (int)fydb->cfg.diag->cfg.level)
		return 0;

	/* fill in fy_diag_ctx */
	memset(&fydc, 0, sizeof(fydc));

	fydc.level = (flags & FYDF_LEVEL_MASK) >> FYDF_LEVEL_SHIFT;
	fydc.module = (flags & FYDF_MODULE_MASK) >> FYDF_MODULE_SHIFT;
	fydc.source_file = file;
	fydc.source_line = line;
	fydc.source_func = func;
	fydc.line = -1;
	fydc.column = -1;

	rc = fy_vdiag(fydb->cfg.diag, &fydc, fmt, ap);

	return rc;
}

int fy_document_builder_diag(struct fy_document_builder *fydb, unsigned int flags,
		     const char *file, int line, const char *func,
		     const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = fy_document_builder_vdiag(fydb, flags, file, line, func, fmt, ap);
	va_end(ap);

	return rc;
}

void fy_document_builder_diag_vreport(struct fy_document_builder *fydb,
			      const struct fy_diag_report_ctx *fydrc,
			      const char *fmt, va_list ap)
{
	if (!fydb || !fydb->cfg.diag || !fydrc || !fmt)
		return;

	fy_diag_vreport(fydb->cfg.diag, fydrc, fmt, ap);
}

void fy_document_builder_diag_report(struct fy_document_builder *fydb,
			     const struct fy_diag_report_ctx *fydrc,
			     const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_document_builder_diag_vreport(fydb, fydrc, fmt, ap);
	va_end(ap);
}

