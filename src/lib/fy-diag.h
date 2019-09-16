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

#include <fy-list.h>

struct fy_parser;
struct fy_token;
struct fy_token_list;
struct fy_simple_key;
struct fy_simple_key_list;
struct fy_input_cfg;

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

int fy_vdiag(struct fy_parser *fyp, unsigned int flags,
	     const char *file, int line, const char *func,
	     const char *fmt, va_list ap);

int fy_diag(struct fy_parser *fyp, unsigned int flags,
	    const char *file, int line, const char *func,
	    const char *fmt, ...)
		__attribute__((format(printf, 6, 7)));

#ifndef NDEBUG

#define fy_debug(_fyp, _module, _fmt, ...) \
	fy_diag((_fyp), FYET_DEBUG | FYDF_MODULE(_module), __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)

#define __FY_DEBUG_UNUSED__	/* nothing */

#else

#define fy_debug(_fyp, _fmt, ...) \
	do { } while(0);

#define __FY_DEBUG_UNUSED__	__attribute__((__unused__))

#endif

#define fy_info(_fyp, _fmt, ...) \
	fy_diag((_fyp), FYET_INFO, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)
#define fy_notice(_fyp, _fmt, ...) \
	fy_diag((_fyp), FYET_NOTICE, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)
#define fy_warning(_fyp, _fmt, ...) \
	fy_diag((_fyp), FYET_WARNING, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)
#define fy_error(_fyp, _fmt, ...) \
	fy_diag((_fyp), FYET_ERROR, __FILE__, __LINE__, __func__, \
			(_fmt) , ## __VA_ARGS__)

#define fy_atom_debug(_fyp, _fmt, ...) \
	fy_debug((_fyp), FYEM_ATOM, (_fmt) , ## __VA_ARGS__)
#define fy_atom_notice(_fyp, _fmt, ...) \
	fy_notice((_fyp), FYEM_ATOM, (_fmt) , ## __VA_ARGS__)

#define fy_scan_debug(_fyp, _fmt, ...) \
	fy_debug((_fyp), FYEM_SCAN, (_fmt) , ## __VA_ARGS__)
#define fy_scan_notice(_fyp, _fmt, ...) \
	fy_notice((_fyp), FYEM_SCAN, (_fmt) , ## __VA_ARGS__)

#define fy_parse_debug(_fyp, _fmt, ...) \
	fy_debug((_fyp), FYEM_PARSE, (_fmt) , ## __VA_ARGS__)
#define fy_parse_notice(_fyp, _fmt, ...) \
	fy_notice((_fyp), FYEM_PARSE, (_fmt) , ## __VA_ARGS__)

#define fy_doc_debug(_fyp, _fmt, ...) \
	fy_debug((_fyp), FYEM_DOC, (_fmt) , ## __VA_ARGS__)
#define fy_doc_notice(_fyp, _fmt, ...) \
	fy_notice((_fyp), FYEM_DOC, (_fmt) , ## __VA_ARGS__)

#define fy_build_debug(_fyp, _fmt, ...) \
	fy_debug((_fyp), FYEM_BUILD, (_fmt) , ## __VA_ARGS__)
#define fy_build_notice(_fyp, _fmt, ...) \
	fy_notice((_fyp), FYEM_BUILD, (_fmt) , ## __VA_ARGS__)

#define fy_internal_debug(_fyp, _fmt, ...) \
	fy_debug((_fyp), FYEM_INTERNAL, (_fmt) , ## __VA_ARGS__)
#define fy_internal_notice(_fyp, _fmt, ...) \
	fy_notice((_fyp), FYEM_INTERNAL, (_fmt) , ## __VA_ARGS__)

#define fy_system_debug(_fyp, _fmt, ...) \
	fy_debug((_fyp), FYEM_SYSTEM, (_fmt) , ## __VA_ARGS__)
#define fy_system_notice(_fyp, _fmt, ...) \
	fy_notice((_fyp), FYEM_SYSTEM, (_fmt) , ## __VA_ARGS__)

/* topical debug methods */

char *fy_token_dump_format(struct fy_token *fyt, char *buf, size_t bufsz);
char *fy_token_list_dump_format(struct fy_token_list *fytl,
		struct fy_token *fyt_highlight, char *buf, size_t bufsz);

char *fy_simple_key_dump_format(struct fy_parser *fyp, struct fy_simple_key *fysk, char *buf, size_t bufsz);
char *fy_simple_key_list_dump_format(struct fy_parser *fyp, struct fy_simple_key_list *fyskl,
		struct fy_simple_key *fysk_highlight, char *buf, size_t bufsz);

#ifndef NDEBUG

void fy_debug_dump_token_list(struct fy_parser *fyp, struct fy_token_list *fytl,
		struct fy_token *fyt_highlight, const char *banner);
void fy_debug_dump_token(struct fy_parser *fyp, struct fy_token *fyt, const char *banner);

void fy_debug_dump_simple_key_list(struct fy_parser *fyp, struct fy_simple_key_list *fyskl,
		struct fy_simple_key *fysk_highlight, const char *banner);
void fy_debug_dump_simple_key(struct fy_parser *fyp, struct fy_simple_key *fysk, const char *banner);

void fy_debug_dump_input(struct fy_parser *fyp, const struct fy_input_cfg *fyic,
		const char *banner);

#else

static inline void
fy_debug_dump_token_list(struct fy_parser *fyp, struct fy_token_list *fytl,
			 struct fy_token *fyt_highlight, const char *banner)
{
	/* nothing */
}

static inline void
fy_debug_dump_token(struct fy_parser *fyp, struct fy_token *fyt, const char *banner)
{
	/* nothing */
}

static inline void
fy_debug_dump_simple_key_list(struct fy_parser *fyp, struct fy_simple_key_list *fyskl,
			      struct fy_simple_key *fysk_highlight, const char *banner)
{
	/* nothing */
}

static inline void
fy_debug_dump_simple_key(struct fy_parser *fyp, struct fy_simple_key *fysk, const char *banner)
{
	/* nothing */
}

static inline void
fy_debug_dump_input(struct fy_parser *fyp, const struct fy_input_cfg *fyic,
		    const char *banner)
{
	/* nothing */
}

#endif

#endif
