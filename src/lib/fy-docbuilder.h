/*
 * fy-docbuilder.h - YAML document builder internal header file
 *
 * Copyright (c) 2022 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_DOCBUILDER_H
#define FY_DOCBUILDER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

#include <libfyaml.h>

#include "fy-doc.h"

#include "fy-diag.h"

enum fy_document_builder_state {
	FYDBS_NODE,
	FYDBS_MAP_KEY,
	FYDBS_MAP_VAL,
	FYDBS_SEQ,
};

struct fy_document_builder_ctx {
	enum fy_document_builder_state s;
	struct fy_node *fyn;
	struct fy_node_pair *fynp;	/* for mapping */
};

struct fy_document_builder {
	struct fy_document_builder_cfg cfg;
	struct fy_document *fyd;
	bool single_mode;
	bool in_stream;
	bool doc_done;
	unsigned int next;
	unsigned int alloc;
	unsigned int max_depth;
	struct fy_document_builder_ctx *stack;
};

/* internal only */
struct fy_document *
fy_document_builder_event_document(struct fy_document_builder *fydb, struct fy_eventp_list *evpl);

/* diagnostics */

static inline bool
fydb_debug_log_level_is_enabled(struct fy_document_builder *fydb, enum fy_error_module module)
{
	return fydb && fy_diag_log_level_is_enabled(fydb->cfg.diag, FYET_DEBUG, module);
}

int fy_document_builder_vdiag(struct fy_document_builder *fydb, unsigned int flags,
			      const char *file, int line, const char *func,
			      const char *fmt, va_list ap);

int fy_document_builder_diag(struct fy_document_builder *fydb, unsigned int flags,
			     const char *file, int line, const char *func,
			     const char *fmt, ...)
			__attribute__((format(printf, 6, 7)));

void fy_document_builder_diag_vreport(struct fy_document_builder *fydb,
				      const struct fy_diag_report_ctx *fydrc,
				      const char *fmt, va_list ap);
void fy_document_builder_diag_report(struct fy_document_builder *fydb,
				     const struct fy_diag_report_ctx *fydrc,
				     const char *fmt, ...)
				__attribute__((format(printf, 3, 4)));

#ifdef FY_DEVMODE

#define fydb_debug(_fydb, _module, _fmt, ...) \
	do { \
		struct fy_document_builder *__fydb = (_fydb); \
		enum fy_error_module __module = (_module); \
		\
		if (fydb_debug_log_level_is_enabled(__fydb, __module)) \
			fy_document_builder_diag(__fydb, FYET_DEBUG | FYDF_MODULE(_module), \
					__FILE__, __LINE__, __func__, \
					(_fmt) , ## __VA_ARGS__); \
	} while(0)
#else

#define fydb_debug(_fydb, _module, _fmt, ...) \
	do { } while(0)

#endif

#define fydb_info(_fydb, _fmt, ...) \
	fy_document_builder_diag((_fydb), FYET_INFO, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)
#define fydb_notice(_fydb, _fmt, ...) \
	fy_document_builder_diag((_fydb), FYET_NOTICE, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)
#define fydb_warning(_fydb, _fmt, ...) \
	fy_document_builder_diag((_fydb), FYET_WARNING, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)
#define fydb_error(_fydb, _fmt, ...) \
	fy_document_builder_diag((_fydb), FYET_ERROR, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)

#define fydb_error_check(_fydb, _cond, _label, _fmt, ...) \
	do { \
		if (!(_cond)) { \
			fydb_error((_fydb), _fmt, ## __VA_ARGS__); \
			goto _label ; \
		} \
	} while(0)

#define _FYDB_TOKEN_DIAG(_fydb, _fyt, _type, _module, _fmt, ...) \
	do { \
		struct fy_diag_report_ctx _drc; \
		memset(&_drc, 0, sizeof(_drc)); \
		_drc.type = (_type); \
		_drc.module = (_module); \
		_drc.fyt = (_fyt); \
		fy_document_builder_diag_report((_fydb), &_drc, (_fmt) , ## __VA_ARGS__); \
	} while(0)

#define FYDB_TOKEN_DIAG(_fydb, _fyt, _type, _module, _fmt, ...) \
	_FYDB_TOKEN_DIAG(_fydb, fy_token_ref(_fyt), _type, _module, _fmt, ## __VA_ARGS__)

#define FYDB_NODE_DIAG(_fydb, _fyn, _type, _module, _fmt, ...) \
	_FYDB_TOKEN_DIAG(_fydb, fy_node_token(_fyn), _type, _module, _fmt, ## __VA_ARGS__)

#define FYDB_TOKEN_ERROR(_fydb, _fyt, _module, _fmt, ...) \
	FYDB_TOKEN_DIAG(_fydb, _fyt, FYET_ERROR, _module, _fmt, ## __VA_ARGS__)

#define FYDB_NODE_ERROR(_fydb, _fyn, _module, _fmt, ...) \
	FYDB_NODE_DIAG(_fydb, _fyn, FYET_ERROR, _module, _fmt, ## __VA_ARGS__)

#define FYDB_TOKEN_ERROR_CHECK(_fydb, _fyt, _module, _cond, _label, _fmt, ...) \
	do { \
		if (!(_cond)) { \
			FYDB_TOKEN_ERROR(_fydb, _fyt, _module, _fmt, ## __VA_ARGS__); \
			goto _label; \
		} \
	} while(0)

#define FYDB_NODE_ERROR_CHECK(_fydb, _fyn, _module, _cond, _label, _fmt, ...) \
	do { \
		if (!(_cond)) { \
			FYDB_NODE_ERROR(_fydb, _fyn, _module, _fmt, ## __VA_ARGS__); \
			goto _label; \
		} \
	} while(0)

#define FYDB_TOKEN_WARNING(_fydb, _fyt, _module, _fmt, ...) \
	FYDB_TOKEN_DIAG(_fydb, _fyt, FYET_WARNING, _module, _fmt, ## __VA_ARGS__)

#endif
