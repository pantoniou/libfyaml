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
#include <ctype.h>

#include <libfyaml.h>

#include "fy-parse.h"

static const char *error_type_txt[] = {
	[FYET_DEBUG]   = "debug",
	[FYET_INFO]    = "info",
	[FYET_NOTICE]  = "notice",
	[FYET_WARNING] = "warning",
	[FYET_ERROR]   = "error",
};

const char *fy_error_type_to_string(enum fy_error_type type)
{

	if ((unsigned int)type >= FYET_MAX)
		return "";
	return error_type_txt[type];
}

enum fy_error_type fy_string_to_error_type(const char *str)
{
	unsigned int i;
	int level;

	if (!str)
		return FYET_MAX;

	if (isdigit(*str)) {
		level = atoi(str);
		if (level >= 0 && level < FYET_MAX)
			return (enum fy_error_type)level;
	}

	for (i = 0; i < FYET_MAX; i++) {
		if (!strcmp(str, error_type_txt[i]))
			return (enum fy_error_type)i;
	}

	return FYET_MAX;
}

static const char *error_module_txt[] = {
	[FYEM_UNKNOWN]	= "unknown",
	[FYEM_ATOM]	= "atom",
	[FYEM_SCAN]	= "scan",
	[FYEM_PARSE]	= "parse",
	[FYEM_DOC]	= "doc",
	[FYEM_BUILD]	= "build",
	[FYEM_INTERNAL]	= "internal",
	[FYEM_SYSTEM]	= "system",
};

const char *fy_error_module_to_string(enum fy_error_module module)
{

	if ((unsigned int)module >= FYEM_MAX)
		return "";
	return error_module_txt[module];
}

enum fy_error_module fy_string_to_error_module(const char *str)
{
	unsigned int i;

	if (!str)
		return FYEM_MAX;

	for (i = 0; i < FYEM_MAX; i++) {
		if (!strcmp(str, error_module_txt[i]))
			return (enum fy_error_module)i;
	}

	return FYEM_MAX;
}

static const char *fy_error_level_str(enum fy_error_type level)
{
	static const char *txt[] = {
		[FYET_DEBUG]   = "DBG",
		[FYET_INFO]    = "INF",
		[FYET_NOTICE]  = "NOT",
		[FYET_WARNING] = "WRN",
		[FYET_ERROR]   = "ERR",
	};

	if ((unsigned int)level >= FYET_MAX)
		return "*unknown*";
	return txt[level];
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

static const struct fy_diag_cfg default_diag_cfg_template = {
	.fp		= NULL,	/* must be overriden */
	.level		= FYET_INFO,
	.module_mask	= (1U << FYEM_MAX) - 1,	/* all modules */
	.show_source	= false,
	.show_position	= false,
	.show_type	= true,
	.show_module	= false,
	.colorize	= false, /* can be overriden */
	.source_width	= 50,
	.position_width	= 10,
	.type_width	= 5,
	.module_width	= 6,
};

void fy_diag_cfg_default(struct fy_diag_cfg *cfg)
{
	if (!cfg)
		return;

	*cfg = default_diag_cfg_template;
	cfg->fp = stderr;
	cfg->colorize = isatty(fileno(stderr)) == 1;
}

void fy_diag_cfg_from_parser_flags(struct fy_diag_cfg *cfg, enum fy_parse_cfg_flags pflags)
{
	/* nothing */
}

struct fy_diag *fy_diag_create(const struct fy_diag_cfg *cfg)
{
	struct fy_diag *diag;

	diag = malloc(sizeof(*diag));
	if (!diag)
		return NULL;
	memset(diag, 0, sizeof(*diag));

	if (!cfg)
		fy_diag_cfg_default(&diag->cfg);
	else
		diag->cfg = *cfg;
	diag->on_error = false;
	diag->refs = 1;

	return diag;
}

void fy_diag_destroy(struct fy_diag *diag)
{
	if (!diag)
		return;

	diag->destroyed = true;

	return fy_diag_unref(diag);
}

bool fy_diag_got_error(struct fy_diag *diag)
{
	return diag && diag->on_error;
}

void fy_diag_reset_error(struct fy_diag *diag)
{
	if (!diag)
		return;

	diag->on_error = false;
}

/**
 * fy_diag_reset_error() - Reset the error flag of
 * 			   the diagnostic object
 *
 * Clears the error flag which was set by an output
 * of an error level diagnostic
 *
 * @diag: The diagnostic object
 */
void
fy_diag_reset_error(struct fy_diag *diag)
	FY_EXPORT;

void fy_diag_free(struct fy_diag *diag)
{
	if (!diag)
		return;
	free(diag);
}

const struct fy_diag_cfg *fy_diag_get_cfg(struct fy_diag *diag)
{
	if (!diag)
		return NULL;
	return &diag->cfg;
}

void fy_diag_set_cfg(struct fy_diag *diag, const struct fy_diag_cfg *cfg)
{
	if (!diag)
		return;

	if (!cfg) {
		fy_diag_cfg_default(&diag->cfg);
		return;
	}

	diag->cfg = *cfg;
}

void fy_diag_set_level(struct fy_diag *diag, enum fy_error_type level)
{
	if (!diag || (unsigned int)level >= FYET_MAX)
		return;
	diag->cfg.level = level;
}

void fy_diag_set_colorize(struct fy_diag *diag, bool colorize)
{
	if (!diag)
		return;
	diag->cfg.colorize = colorize;
}

struct fy_diag *fy_diag_ref(struct fy_diag *diag)
{
	if (!diag)
		return NULL;

	assert(diag->refs + 1 > 0);
	diag->refs++;

	return diag;
}

void fy_diag_unref(struct fy_diag *diag)
{
	if (!diag)
		return;

	assert(diag->refs > 0);

	if (diag->refs == 1)
		fy_diag_free(diag);
	else
		diag->refs--;
}

int fy_diag_vprintf(struct fy_diag *diag, const char *fmt, va_list ap)
{
	char *buf;
	int rc;

	if (!diag || !fmt)
		return -1;

	/* no more output */
	if (diag->destroyed)
		return 0;

	if (diag->cfg.fp)
		return vfprintf(diag->cfg.fp, fmt, ap);

	if (diag->cfg.output_fn) {
		rc = vasprintf(&buf, fmt, ap);
		if (rc < 0)
			return rc;
		diag->cfg.output_fn(diag, diag->cfg.user, buf, (size_t)rc);
		free(buf);
		return rc;
	}

	return -1;
}

int fy_diag_printf(struct fy_diag *diag, const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = fy_diag_vprintf(diag, fmt, ap);
	va_end(ap);

	return rc;
}

int fy_vdiag(struct fy_diag *diag, const struct fy_diag_ctx *fydc,
	     const char *fmt, va_list ap)
{
	char *msg = NULL;
	char *source = NULL, *position = NULL, *typestr = NULL, *modulestr = NULL;
	const char *file_stripped = NULL;
	const char *color_start = NULL, *color_end = NULL;
	enum fy_error_type level;
	int rc;

	if (!diag || !fydc || !fmt)
		return -1;

	level = fydc->level;

	/* turn errors into notices while not reset */
	if (level >= FYET_ERROR && diag->on_error)
		level = FYET_NOTICE;

	if (level < diag->cfg.level) {
		rc = 0;
		goto out;
	}

	/* check module enable mask */
	if (!(diag->cfg.module_mask & FY_BIT(fydc->module))) {
		rc = 0;
		goto out;
	}

	msg = alloca_vsprintf(fmt, ap);

	/* source part */
	if (diag->cfg.show_source) {
		if (fydc->source_file) {
			file_stripped = strrchr(fydc->source_file, '/');
			if (!file_stripped)
				file_stripped = fydc->source_file;
			else
				file_stripped++;
		} else
			file_stripped = "";
		source = alloca_sprintf("%s:%d @%s()%s",
				file_stripped, fydc->source_line, fydc->source_func, " ");
	}

	/* position part */
	if (diag->cfg.show_position && fydc->line >= 0 && fydc->column >= 0)
		position = alloca_sprintf("<%3d:%2d>%s", fydc->line, fydc->column, ": ");

	/* type part */
	if (diag->cfg.show_type)
		typestr = alloca_sprintf("[%s]%s", fy_error_level_str(level), ": ");

	/* module part */
	if (diag->cfg.show_module)
		modulestr = alloca_sprintf("<%s>%s", fy_error_module_str(fydc->module), ": ");

	if (diag->cfg.colorize) {
		switch (level) {
		case FYET_DEBUG:
			color_start = "\x1b[37m";	/* normal white */
			break;
		case FYET_INFO:
			color_start = "\x1b[37;1m";	/* bright white */
			break;
		case FYET_NOTICE:
			color_start = "\x1b[34;1m";	/* bright blue */
			break;
		case FYET_WARNING:
			color_start = "\x1b[33;1m";	/* bright yellow */
			break;
		case FYET_ERROR:
			color_start = "\x1b[31;1m";	/* bright red */
			break;
		default:	/* handles FYET_MAX */
			break;
		}
		if (color_start)
			color_end = "\x1b[0m";
	}

	rc = fy_diag_printf(diag, "%s" "%*s" "%*s" "%*s" "%*s" "%s" "%s\n",
			color_start ? : "",
			source    ? diag->cfg.source_width : 0, source ? : "",
			position  ? diag->cfg.position_width : 0, position ? : "",
			typestr   ? diag->cfg.type_width : 0, typestr ? : "",
			modulestr ? diag->cfg.module_width : 0, modulestr ? : "",
			msg,
			color_end ? : "");

	if (rc > 0)
		rc++;

out:
	/* if it's the first error we're generating set the
	 * on_error flag until the top caller clears it
	 */
	if (!diag->on_error && fydc->level >= FYET_ERROR)
		diag->on_error = true;

	return rc;
}

int fy_diagf(struct fy_diag *diag, const struct fy_diag_ctx *fydc,
	     const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = fy_vdiag(diag, fydc, fmt, ap);
	va_end(ap);

	return rc;
}

void fy_diag_vreport(struct fy_diag *diag,
		     const struct fy_diag_report_ctx *fydrc,
		     const char *fmt, va_list ap)
{
	const char *name, *color_start = NULL, *color_end = NULL, *white = NULL;
	char *msg_str = NULL, *name_str = NULL;
	const struct fy_mark *start_mark;
	struct fy_atom_raw_line_iter iter;
	const struct fy_raw_line *l;
	char *tildes = "";
	int j, k, tildesz = 0, line, column;

	if (!diag || !fydrc || !fmt || !fydrc->fyt)
		return;

	start_mark = fy_token_start_mark(fydrc->fyt);

	if (fydrc->has_override) {
		name = fydrc->override_file;
		line = fydrc->override_line;
		column = fydrc->override_column;
	} else {
		name = fy_input_get_filename(fy_token_get_input(fydrc->fyt));
		line = start_mark->line + 1;
		column = start_mark->column + 1;
	}

	/* it will strip trailing newlines */
	msg_str = alloca_vsprintf(fmt, ap);

	if (diag->cfg.colorize) {
		switch (fydrc->type) {
		case FYET_DEBUG:
			color_start = "\x1b[37m";	/* normal white */
			break;
		case FYET_INFO:
			color_start = "\x1b[37;1m";	/* bright white */
			break;
		case FYET_NOTICE:
			color_start = "\x1b[34;1m";	/* bright blue */
			break;
		case FYET_WARNING:
			color_start = "\x1b[33;1m";	/* bright yellow */
			break;
		case FYET_ERROR:
			color_start = "\x1b[31;1m";	/* bright red */
			break;
		default:
			break;
		}
		color_end = "\x1b[0m";
		white = "\x1b[37;1m";
	} else
		color_start = color_end = white = "";

	if (name || (line > 0 && column > 0))
		name_str = (line > 0 && column > 0) ?
			alloca_sprintf("%s%s:%d:%d: ", white, name, line, column) :
			alloca_sprintf("%s%s: ", white, name);

	fy_diag_printf(diag, "%s" "%s%s: %s" "%s\n",
		name_str ? : "",
		color_start, fy_error_type_to_string(fydrc->type), color_end,
		msg_str);

	fy_atom_raw_line_iter_start(fy_token_atom(fydrc->fyt), &iter);
	while ((l = fy_atom_raw_line_iter_next(&iter)) != NULL) {
		fy_diag_printf(diag, "%.*s\n",
			       (int)l->line_len, l->line_start);
		j = l->content_start_col8;
		k = (l->content_end_col8 - l->content_start_col8) - 1;
		if (k > tildesz) {
			if (!tildesz)
				tildesz = 32;
			while (tildesz < k)
				tildesz *= 2;
			tildes = alloca(tildesz + 1);
			memset(tildes, '~', tildesz);
			tildes[tildesz] = '\0';
		}
		fy_diag_printf(diag, "%*s%s%c%.*s%s\n",
			       j, "", color_start,
			       l->lineno == 1 ? '^' : '~',
			       k > 0 ? k : 0, tildes, color_end);
	}
	fy_atom_raw_line_iter_finish(&iter);

	fy_token_unref(fydrc->fyt);

	if (!diag->on_error && fydrc->type == FYET_ERROR)
		diag->on_error = true;
}

void fy_diag_report(struct fy_diag *diag,
		    const struct fy_diag_report_ctx *fydrc,
		    const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_diag_vreport(diag, fydrc, fmt, ap);
	va_end(ap);
}

/* parser */

int fy_parser_vdiag(struct fy_parser *fyp, unsigned int flags,
		    const char *file, int line, const char *func,
		    const char *fmt, va_list ap)
{
	struct fy_diag_ctx fydc;
	int rc;

	if (!fyp || !fyp->diag || !fmt)
		return -1;

	/* perform the enable tests early to avoid the overhead */
	if (((flags & FYDF_LEVEL_MASK) >> FYDF_LEVEL_SHIFT) < fyp->diag->cfg.level)
		return 0;

	/* fill in fy_diag_ctx */
	memset(&fydc, 0, sizeof(fydc));

	fydc.level = (flags & FYDF_LEVEL_MASK) >> FYDF_LEVEL_SHIFT;
	fydc.module = (flags & FYDF_MODULE_MASK) >> FYDF_MODULE_SHIFT;
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

/* document */

int fy_document_vdiag(struct fy_document *fyd, unsigned int flags,
		      const char *file, int line, const char *func,
		      const char *fmt, va_list ap)
{
	struct fy_diag_ctx fydc;
	int rc;

	if (!fyd || !fmt || !fyd->diag)
		return -1;

	/* perform the enable tests early to avoid the overhead */
	if (((flags & FYDF_LEVEL_MASK) >> FYDF_LEVEL_SHIFT) < fyd->diag->cfg.level)
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

	rc = fy_vdiag(fyd->diag, &fydc, fmt, ap);

	return rc;
}

int fy_document_diag(struct fy_document *fyd, unsigned int flags,
		     const char *file, int line, const char *func,
		     const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = fy_document_vdiag(fyd, flags, file, line, func, fmt, ap);
	va_end(ap);

	return rc;
}

void fy_document_diag_vreport(struct fy_document *fyd,
			      const struct fy_diag_report_ctx *fydrc,
			      const char *fmt, va_list ap)
{
	if (!fyd || !fyd->diag || !fydrc || !fmt)
		return;

	fy_diag_vreport(fyd->diag, fydrc, fmt, ap);
}

void fy_document_diag_report(struct fy_document *fyd,
			     const struct fy_diag_report_ctx *fydrc,
			     const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_document_diag_vreport(fyd, fydrc, fmt, ap);
	va_end(ap);
}

/* reader */

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

void fy_diag_node_vreport(struct fy_diag *diag, struct fy_node *fyn,
			  enum fy_error_type type, const char *fmt, va_list ap)
{
	struct fy_diag_report_ctx drc;
	bool save_on_error;

	if (!fyn || !diag)
		return;

	save_on_error = diag->on_error;
	diag->on_error = false;

	memset(&drc, 0, sizeof(drc));
	drc.type = type;
	drc.module = FYEM_UNKNOWN;
	drc.fyt = fy_node_token(fyn);
	fy_diag_vreport(diag, &drc, fmt, ap);

	diag->on_error = save_on_error;
}

void fy_diag_node_report(struct fy_diag *diag, struct fy_node *fyn,
			 enum fy_error_type type, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_diag_node_vreport(diag, fyn, type, fmt, ap);
	va_end(ap);
}

void fy_diag_node_override_vreport(struct fy_diag *diag, struct fy_node *fyn,
				   enum fy_error_type type, const char *file,
				   int line, int column,
				   const char *fmt, va_list ap)
{
	struct fy_diag_report_ctx drc;
	bool save_on_error;

	if (!fyn || !diag)
		return;

	save_on_error = diag->on_error;
	diag->on_error = false;

	memset(&drc, 0, sizeof(drc));
	drc.type = type;
	drc.module = FYEM_UNKNOWN;
	drc.fyt = fy_node_token(fyn);
	drc.has_override = true;
	drc.override_file = file;
	drc.override_line = line;
	drc.override_column = column;
	fy_diag_vreport(diag, &drc, fmt, ap);

	diag->on_error = save_on_error;
}

void fy_diag_node_override_report(struct fy_diag *diag, struct fy_node *fyn,
				  enum fy_error_type type, const char *file,
				  int line, int column, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_diag_node_override_vreport(diag, fyn, type, file, line, column, fmt, ap);
	va_end(ap);
}

void fy_node_vreport(struct fy_node *fyn, enum fy_error_type type,
		     const char *fmt, va_list ap)
{
	if (!fyn || !fyn->fyd)
		return;

	fy_diag_node_vreport(fyn->fyd->diag, fyn, type, fmt, ap);
}

void fy_node_report(struct fy_node *fyn, enum fy_error_type type,
		    const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_node_vreport(fyn, type, fmt, ap);
	va_end(ap);
}

void fy_node_override_vreport(struct fy_node *fyn, enum fy_error_type type,
			      const char *file, int line, int column,
			      const char *fmt, va_list ap)
{
	if (!fyn || !fyn->fyd)
		return;

	fy_diag_node_override_vreport(fyn->fyd->diag, fyn, type,
				      file, line, column, fmt, ap);
}

void fy_node_override_report(struct fy_node *fyn, enum fy_error_type type,
			     const char *file, int line, int column,
			     const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_node_override_vreport(fyn, type, file, line, column, fmt, ap);
	va_end(ap);
}
