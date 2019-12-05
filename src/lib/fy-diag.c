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

static const char *fy_error_type_str(enum fy_error_type type)
{
	static const char *txt[] = {
		[FYET_DEBUG]   = "debug",
		[FYET_INFO]    = "info",
		[FYET_NOTICE]  = "notice",
		[FYET_WARNING] = "warning",
		[FYET_ERROR]   = "error",
	};

	if ((unsigned int)type >= FYET_MAX)
		return "*unknown*";
	return txt[type];
}

#define alloca_vsprintf(_fmt, _ap) \
	({ \
		const char *__fmt = (_fmt); \
		va_list _ap_orig; \
		int _size; \
		int _sizew __FY_DEBUG_UNUSED__; \
		char *_buf = NULL, *_s; \
		\
		va_copy(_ap_orig, (_ap)); \
		_size = vsnprintf(NULL, 0, __fmt, _ap_orig); \
		va_end(_ap_orig); \
		if (_size != -1) { \
			_buf = alloca(_size + 1); \
			_sizew = vsnprintf(_buf, _size + 1, __fmt, _ap); \
			assert(_size == _sizew); \
			_s = _buf + strlen(_buf); \
			while (_s > _buf && _s[-1] == '\n') \
				*--_s = '\0'; \
		} \
		_buf; \
	})

#define alloca_sprintf(_fmt, ...) \
	({ \
		const char *__fmt = (_fmt); \
		int _size; \
		int _sizew __FY_DEBUG_UNUSED__; \
		char *_buf = NULL, *_s; \
		\
		_size = snprintf(NULL, 0, __fmt, ## __VA_ARGS__); \
		if (_size != -1) { \
			_buf = alloca(_size + 1); \
			_sizew = snprintf(_buf, _size + 1, __fmt, __VA_ARGS__); \
			assert(_size == _sizew); \
			_s = _buf + strlen(_buf); \
			while (_s > _buf && _s[-1] == '\n') \
				*--_s = '\0'; \
		} \
		_buf; \
	})

static void fy_diag_from_parser_flags(struct fy_diag *diag, enum fy_parse_cfg_flags pflags)
{
	diag->level = (pflags >> FYPCF_DEBUG_LEVEL_SHIFT) & FYPCF_DEBUG_LEVEL_MASK;
	diag->module_mask = (pflags >> FYPCF_MODULE_SHIFT) & FYPCF_MODULE_MASK;
	diag->colorize = false;
	diag->show_source = !!(pflags & FYPCF_DEBUG_DIAG_SOURCE);
	diag->show_position = !!(pflags & FYPCF_DEBUG_DIAG_POSITION);
	diag->show_type = !!(pflags & FYPCF_DEBUG_DIAG_TYPE);
	diag->show_module = !!(pflags & FYPCF_DEBUG_DIAG_MODULE);
}

static void fy_diag_default_widths(struct fy_diag *diag)
{
	diag->source_width = 50;
	diag->position_width = 10;
	diag->type_width = 5;
	diag->module_width = 6;
}

void fy_diag_from_parser(struct fy_diag *diag, struct fy_parser *fyp)
{
	if (!diag)
		return;

	diag->fp = fy_parser_get_error_fp(fyp);
	fy_diag_from_parser_flags(diag, fy_parser_get_cfg_flags(fyp));
	diag->colorize = fy_parser_is_colorized(fyp);
	fy_diag_default_widths(diag);
	diag->on_error = fyp && !!fyp->stream_error;
}

void fy_diag_from_document(struct fy_diag *diag, struct fy_document *fyd)
{
	if (!diag || !fyd)
		return;

	diag->fp = fy_document_get_error_fp(fyd);
	fy_diag_from_parser_flags(diag, fy_document_get_cfg_flags(fyd));
	diag->colorize = fy_document_is_colorized(fyd);
	fy_diag_default_widths(diag);
	diag->on_error = false;
}

struct fy_diag *fy_diag_create(void)
{
	struct fy_diag *diag;

	diag = malloc(sizeof(*diag));
	if (!diag)
		return NULL;
	memset(diag, 0, sizeof(*diag));

	/* NULL is allowed */
	fy_diag_from_parser(diag, NULL);

	diag->refs = 1;

	return diag;
}

void fy_diag_free(struct fy_diag *diag)
{
	if (!diag)
		return;
	free(diag);
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

int fy_vdiag_ctx(struct fy_diag *diag, const struct fy_diag_ctx *fydc,
		 const char *fmt, va_list ap)
{
	char *msg = NULL;
	char *source = NULL, *position = NULL, *typestr = NULL, *modulestr = NULL;
	const char *file_stripped = NULL;
	const char *color_start = NULL, *color_end = NULL;
	int rc, level;

	if (!diag || !fydc || !fmt || !diag->fp)
		return -1;

	level = fydc->level;

	/* turn errors into notices while not reset */
	if (level >= FYET_ERROR && diag->on_error)
		level = FYET_NOTICE;

	if (level < diag->level) {
		rc = 0;
		goto out;
	}

	/* check module enable mask */
	if (!(diag->module_mask & FY_BIT(fydc->module))) {
		rc = 0;
		goto out;
	}

	msg = alloca_vsprintf(fmt, ap);

	/* source part */
	if (diag->show_source) {
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
	if (diag->show_position && fydc->line >= 0 && fydc->column >= 0)
		position = alloca_sprintf("<%3d:%2d>%s", fydc->line, fydc->column, ": ");

	/* type part */
	if (diag->show_type)
		typestr = alloca_sprintf("[%s]%s", fy_error_level_str(level), ": ");

	/* module part */
	if (diag->show_module)
		modulestr = alloca_sprintf("<%s>%s", fy_error_module_str(fydc->module), ": ");

	if (diag->colorize) {
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
		}
		if (color_start)
			color_end = "\x1b[0m";
	}

	rc = fprintf(diag->fp, "%s" "%*s" "%*s" "%*s" "%*s" "%s" "%s\n",
			color_start ? : "",
			source    ? diag->source_width : 0, source ? : "",
			position  ? diag->position_width : 0, position ? : "",
			typestr   ? diag->type_width : 0, typestr ? : "",
			modulestr ? diag->module_width : 0, modulestr ? : "",
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

int fy_diag_ctx(struct fy_diag *diag, const struct fy_diag_ctx *fydc,
		const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = fy_vdiag_ctx(diag, fydc, fmt, ap);
	va_end(ap);

	return rc;
}

void fy_diag_vreport(struct fy_diag *diag,
		     const struct fy_diag_report_ctx *fydrc,
		     const char *fmt, va_list ap)
{
	const char *name, *color_start = NULL, *color_end = NULL, *white = NULL;
	char *msg;
	const struct fy_mark *start_mark;
	struct fy_atom_raw_line_iter iter;
	const struct fy_raw_line *l;
	char *tildes;
	int j, k, tildesz = 80;

	if (!diag || !fydrc || !fmt || !diag->fp || !fydrc->fyt)
		return;

	tildes = alloca(tildesz + 1);
	memset(tildes, '~', tildesz);
	tildes[tildesz] = '\0';

	start_mark = fy_token_start_mark(fydrc->fyt);
	name = fy_input_get_filename(fy_token_get_input(fydrc->fyt));

	/* it will strip trailing newlines */
	msg = alloca_vsprintf(fmt, ap);

	if (diag->colorize) {
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

	fprintf(diag->fp, "%s%s%s" "%d:%d: " "%s%s: %s" "%s\n",
		white, name ? : "", name ? ":" : "",
		start_mark->line + 1, start_mark->column + 1,
		color_start, fy_error_type_str(fydrc->type), color_end,
		msg);

	fy_atom_raw_line_iter_start(fy_token_atom(fydrc->fyt), &iter);
	while ((l = fy_atom_raw_line_iter_next(&iter)) != NULL) {
		fprintf(diag->fp, "%.*s\n",
				(int)l->line_len, l->line_start);
		j = l->content_start_col8;
		k = (l->content_end_col8 - l->content_start_col8) - 1;
		if (k > tildesz) {
			tildes = alloca(k + 1);
			memset(tildes, '~', k);
			tildes[k] = '\0';
			tildesz = k;
		}
		fprintf(diag->fp, "%*s%s%c%.*s%s\n",
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
	struct fy_diag *diag = NULL;
	struct fy_diag_ctx fydc;
	unsigned int pflags;
	int fydc_level, fyd_level, fydc_module;
	unsigned int fyd_module_mask;
	int rc;

	if (!fmt)
		return -1;

	pflags = fy_parser_get_cfg_flags(fyp);

	/* perform the enable tests early to avoid the overhead */
	fydc_level = (flags & FYDF_LEVEL_MASK) >> FYDF_LEVEL_SHIFT;
	fyd_level = (pflags >> FYPCF_DEBUG_LEVEL_SHIFT) & FYPCF_DEBUG_LEVEL_MASK;

	if (fydc_level < fyd_level)
		return 0;

	fydc_module = (flags & FYDF_MODULE_MASK) >> FYDF_MODULE_SHIFT;
	fyd_module_mask = (pflags >> FYPCF_MODULE_SHIFT) & FYPCF_MODULE_MASK;

	if (!(fyd_module_mask & FY_BIT(fydc_module)))
		return 0;

	/* for NULL parser, create a diag on the spot */
	if (!fyp) {
		diag = fy_diag_create();
		if (!diag)
			return -1;
		fy_diag_from_parser(diag, NULL);
	} else
		diag = fyp->diag;
	assert(diag);

	/* fill in fy_diag_ctx */
	memset(&fydc, 0, sizeof(fydc));

	fydc.level = fydc_level;
	fydc.module = fydc_module;
	fydc.source_file = file;
	fydc.source_line = line;
	fydc.source_func = func;
	if (fyp && diag->show_position) {
		fydc.line = fyp->line;
		fydc.column = fyp->column;
	} else
		diag->show_position = false;

	rc = fy_vdiag_ctx(diag, &fydc, fmt, ap);

	if (fyp && !fyp->stream_error && diag->on_error)
		fyp->stream_error = true;

	/* release the diag if it was created */
	if (!fyp)
		fy_diag_unref(diag);

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
	unsigned int pflags;
	int fydc_level, fyd_level, fydc_module;
	unsigned int fyd_module_mask;
	int rc;

	if (!fyd || !fmt || !fyd->diag)
		return -1;

	pflags = fy_document_get_cfg_flags(fyd);

	/* perform the enable tests early to avoid the overhead */
	fydc_level = (flags & FYDF_LEVEL_MASK) >> FYDF_LEVEL_SHIFT;
	fyd_level = (pflags >> FYPCF_DEBUG_LEVEL_SHIFT) & FYPCF_DEBUG_LEVEL_MASK;

	if (fydc_level < fyd_level)
		return 0;

	fydc_module = (flags & FYDF_MODULE_MASK) >> FYDF_MODULE_SHIFT;
	fyd_module_mask = (pflags >> FYPCF_MODULE_SHIFT) & FYPCF_MODULE_MASK;

	if (!(fyd_module_mask & FY_BIT(fydc_module)))
		return 0;

	/* fill in fy_diag_ctx */
	memset(&fydc, 0, sizeof(fydc));

	fydc.level = fydc_level;
	fydc.module = fydc_module;
	fydc.source_file = file;
	fydc.source_line = line;
	fydc.source_func = func;
	fydc.line = -1;
	fydc.column = -1;

	rc = fy_vdiag_ctx(fyd->diag, &fydc, fmt, ap);

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

void fy_node_vreport(struct fy_node *fyn, enum fy_error_type type,
		     const char *fmt, va_list ap)
{
	struct fy_diag_report_ctx drc;
	bool save_on_error;

	if (!fyn)
		return;

	save_on_error = fyn->fyd->diag->on_error;
	fyn->fyd->diag->on_error = false;

	memset(&drc, 0, sizeof(drc));
	drc.type = type;
	drc.module = FYEM_UNKNOWN;
	drc.fyt = fy_node_token(fyn);
	fy_document_diag_vreport(fyn->fyd, &drc, fmt, ap);

	fyn->fyd->diag->on_error = save_on_error;
}

void fy_node_report(struct fy_node *fyn, enum fy_error_type type,
		    const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_node_vreport(fyn, type, fmt, ap);
	va_end(ap);
}
