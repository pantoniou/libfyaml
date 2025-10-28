/*
 * fy-composer.h - YAML composer
 *
 * Copyright (c) 2021 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_COMPOSER_H
#define FY_COMPOSER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>

#include <libfyaml.h>

#include "fy-list.h"
#include "fy-typelist.h"

#include "fy-emit-accum.h"
#include "fy-path.h"

#include "fy-diag.h"

struct fy_composer;
struct fy_token;
struct fy_diag;
struct fy_event;
struct fy_eventp;
struct fy_document_builder;

struct fy_composer {
	struct fy_composer_cfg cfg;
	struct fy_path_list paths;
};

void fy_composer_halt_one(struct fy_composer *fyc, struct fy_path *fypp, enum fy_composer_return rc);
void fy_composer_halt(struct fy_composer *fyc, enum fy_composer_return rc);

/* diagnostics */

static inline bool
fyc_debug_log_level_is_enabled(struct fy_composer *fyc, enum fy_error_module module)
{
	return fyc && fy_diag_log_level_is_enabled(fyc->cfg.diag, FYET_DEBUG, module);
}

int fy_composer_vdiag(struct fy_composer *fyc, unsigned int flags,
		      const char *file, int line, const char *func,
		      const char *fmt, va_list ap);

int fy_composer_diag(struct fy_composer *fyc, unsigned int flags,
		     const char *file, int line, const char *func,
		     const char *fmt, ...)
			__attribute__((format(printf, 6, 7)));

void fy_composer_diag_vreport(struct fy_composer *fyc,
			      const struct fy_diag_report_ctx *fydrc,
			      const char *fmt, va_list ap);
void fy_composer_diag_report(struct fy_composer *fyc,
			     const struct fy_diag_report_ctx *fydrc,
			     const char *fmt, ...)
			__attribute__((format(printf, 3, 4)));

#ifdef FY_DEVMODE

#define fyc_debug(_fyc, _module, _fmt, ...) \
	do { \
		struct fy_composer *__fyc = (_fyc); \
		enum fy_error_module __module = (_module); \
		\
		if (fyc_debug_log_level_is_enabled(__fyc, __module)) \
			fy_composer_diag(__fyc, FYET_DEBUG | FYDF_MODULE(_module), \
					__FILE__, __LINE__, __func__, \
					(_fmt) , ## __VA_ARGS__); \
	} while(0)
#else

#define fyc_debug(_fyc, _module, _fmt, ...) \
	do { } while(0)

#endif

#define fyc_info(_fyc, _fmt, ...) \
	fy_composer_diag((_fyc), FYET_INFO, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)
#define fyc_notice(_fyc, _fmt, ...) \
	fy_composer_diag((_fyc), FYET_NOTICE, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)
#define fyc_warning(_fyc, _fmt, ...) \
	fy_composer_diag((_fyc), FYET_WARNING, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)
#define fyc_error(_fyc, _fmt, ...) \
	fy_composer_diag((_fyc), FYET_ERROR, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)

#define fyc_error_check(_fyc, _cond, _label, _fmt, ...) \
	do { \
		if (!(_cond)) { \
			fyc_error((_fyc), _fmt, ## __VA_ARGS__); \
			goto _label ; \
		} \
	} while(0)

#define _FYC_TOKEN_DIAG(_fyc, _fyt, _type, _module, _fmt, ...) \
	do { \
		struct fy_diag_report_ctx _drc; \
		memset(&_drc, 0, sizeof(_drc)); \
		_drc.type = (_type); \
		_drc.module = (_module); \
		_drc.fyt = (_fyt); \
		fy_composer_diag_report((_fyc), &_drc, (_fmt) , ## __VA_ARGS__); \
	} while(0)

#define FYC_TOKEN_DIAG(_fyc, _fyt, _type, _module, _fmt, ...) \
	_FYC_TOKEN_DIAG(_fyc, fy_token_ref(_fyt), _type, _module, _fmt, ## __VA_ARGS__)

#define FYC_NODE_DIAG(_fyc, _fyn, _type, _module, _fmt, ...) \
	_FYC_TOKEN_DIAG(_fyc, fy_node_token(_fyn), _type, _module, _fmt, ## __VA_ARGS__)

#define FYC_TOKEN_ERROR(_fyc, _fyt, _module, _fmt, ...) \
	FYC_TOKEN_DIAG(_fyc, _fyt, FYET_ERROR, _module, _fmt, ## __VA_ARGS__)

#define FYC_TOKEN_ERROR_CHECK(_fyc, _fyt, _module, _cond, _label, _fmt, ...) \
	do { \
		if (!(_cond)) { \
			FYC_TOKEN_ERROR(_fyc, _fyt, _module, _fmt, ## __VA_ARGS__); \
			goto _label; \
		} \
	} while(0)

#define FYC_TOKEN_WARNING(_fyc, _fyt, _module, _fmt, ...) \
	FYC_TOKEN_DIAG(_fyc, _fyt, FYET_WARNING, _module, _fmt, ## __VA_ARGS__)

#endif
