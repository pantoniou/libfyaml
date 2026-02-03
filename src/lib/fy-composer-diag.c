/*
 * fy-composer-diag.c - composer diagnostics
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
#include "fy-composer.h"

int fy_composer_vdiag(struct fy_composer *fyc, unsigned int flags,
		      const char *file, int line, const char *func,
		      const char *fmt, va_list ap)
{
	struct fy_diag_ctx fydc;
	int rc;

	if (!fyc || !fmt || !fyc->cfg.diag)
		return -1;

	/* perform the enable tests early to avoid the overhead */
	if ((int)((flags & FYDF_LEVEL_MASK) >> FYDF_LEVEL_SHIFT) < (int)fyc->cfg.diag->cfg.level)
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

	rc = fy_vdiag(fyc->cfg.diag, &fydc, fmt, ap);

	return rc;
}

int fy_composer_diag(struct fy_composer *fyc, unsigned int flags,
		     const char *file, int line, const char *func,
		     const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = fy_composer_vdiag(fyc, flags, file, line, func, fmt, ap);
	va_end(ap);

	return rc;
}

void fy_composer_diag_vreport(struct fy_composer *fyc,
			      const struct fy_diag_report_ctx *fydrc,
			      const char *fmt, va_list ap)
{
	if (!fyc || !fyc->cfg.diag || !fydrc || !fmt)
		return;

	fy_diag_vreport(fyc->cfg.diag, fydrc, fmt, ap);
}

void fy_composer_diag_report(struct fy_composer *fyc,
			     const struct fy_diag_report_ctx *fydrc,
			     const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_composer_diag_vreport(fyc, fydrc, fmt, ap);
	va_end(ap);
}
