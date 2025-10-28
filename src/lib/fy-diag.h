/*
 * fy-diag.h - diagnostics
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_DIAG_H
#define FY_DIAG_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

#include <libfyaml.h>

#include "fy-utils.h"

#include "fy-list.h"
#include "fy-token.h"

/* error flags (above 0x100 is library specific) */
#define FYEF_SOURCE	0x0001
#define FYEF_POSITION	0x0002
#define FYEF_TYPE	0x0004
#define FYEF_USERSTART	0x0100

#define FYDF_LEVEL_SHIFT	0
#define FYDF_LEVEL_MASK		(0x0f << FYDF_LEVEL_SHIFT)
#define FYDF_LEVEL(x)		(((unsigned int)(x) << FYDF_LEVEL_SHIFT) & FYDF_LEVEL_MASK)
#define FYDF_DEBUG		FYDF_LEVEL(FYET_DEBUG)
#define FYDF_INFO		FYDF_LEVEL(FYET_INFO)
#define FYDF_NOTICE		FYDF_LEVEL(FYET_NOTICE)
#define FYDF_WARNING		FYDF_LEVEL(FYET_WARNING)
#define FYDF_ERROR		FYDF_LEVEL(FYET_ERROR)

#define FYDF_MODULE_SHIFT	4
#define FYDF_MODULE_MASK	(0x0f << FYDF_MODULE_SHIFT)
#define FYDF_MODULE(x)		(((unsigned int)(x) << FYDF_MODULE_SHIFT) & FYDF_MODULE_MASK)
#define FYDF_ATOM		FYDF_MODULE(FYEM_ATOM)
#define FYDF_SCANNER		FYDF_MODULE(FYEM_SCANNER)
#define FYDF_PARSER		FYDF_MODULE(FYEM_PARSER)
#define FYDF_TREE		FYDF_MODULE(FYEM_TREE)
#define FYDF_BUILDER		FYDF_MODULE(FYEM_BUILDER)
#define FYDF_INTERNAL		FYDF_MODULE(FYEM_INTERNAL)
#define FYDF_SYSTEM		FYDF_MODULE(FYEM_SYSTEM)
#define FYDF_MODULE_USER_MASK	7
#define FYDF_MODULE_USER(x)	FYDF_MODULE(8 + ((x) & FYDF_MODULE_USER_MASK))

struct fy_diag_term_info {
	int rows;
	int columns;
};

struct fy_diag_report_ctx {
	enum fy_error_type type;
	enum fy_error_module module;
	struct fy_token *fyt;
	bool has_override;
	const char *override_file;
	int override_line;
	int override_column;
};

FY_TYPE_FWD_DECL_LIST(diag_errorp);
struct fy_diag_errorp {
	struct list_head node;
	char *space;
	struct fy_diag_error e;
};
FY_TYPE_DECL_LIST(diag_errorp);

struct fy_diag {
	struct fy_diag_cfg cfg;
	int refs;
	bool on_error : 1;
	bool destroyed : 1;
	bool collect_errors : 1;
	bool terminal_probed : 1;
	struct fy_diag_term_info term_info;
	struct fy_diag_errorp_list errors;
};

void fy_diag_free(struct fy_diag *diag);

void fy_diag_vreport(struct fy_diag *diag,
		     const struct fy_diag_report_ctx *fydrc,
		     const char *fmt, va_list ap);
void fy_diag_report(struct fy_diag *diag,
		    const struct fy_diag_report_ctx *fydrc,
		    const char *fmt, ...)
			__attribute__((format(printf, 3, 4)));

static inline bool
fy_diag_log_level_is_enabled(struct fy_diag *diag, enum fy_error_type level, enum fy_error_module module)
{
	if (!diag)
		return false;

	/* check level */
	if ((unsigned int)level < FYET_MAX) {

		/* turn errors into debugs while not reset */
		if (level >= FYET_ERROR && diag->on_error)
			level = FYET_DEBUG;

		if (level < diag->cfg.level)
			return false;
	}

	/* check module enable mask */
	if ((unsigned int)module < FYEM_MAX) {
		if (!(diag->cfg.module_mask & FY_BIT(module)))
			return false;
	}

	/* ok, clear to generate */
	return true;
}

void fy_diag_error_atom_display(struct fy_diag *diag, enum fy_error_type type,
				 struct fy_atom *atom);
void fy_diag_error_token_display(struct fy_diag *diag, enum fy_error_type type,
				 struct fy_token *fyt);

void fy_diag_cfg_from_parser_flags(struct fy_diag_cfg *cfg, enum fy_parse_cfg_flags pflags);

#endif
