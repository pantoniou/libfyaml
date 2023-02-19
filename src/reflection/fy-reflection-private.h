/*
 * fy-reflection-private.h - Generic type reflection library header
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_REFLECTION_PRIVATE_H
#define FY_REFLECTION_PRIVATE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <float.h>
#include <assert.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>

#include "fy-endian.h"
#include "fy-typelist.h"
#include "fy-diag.h"

#include <libfyaml.h>
#include <libfyaml/libfyaml-reflection.h>

/* fwd */
struct fy_reflection;
struct fy_import;
struct fy_source_file;
struct fy_decl;
struct fy_type;
struct fy_location;

extern const struct fy_type_kind_info fy_type_kind_info_table[FYTK_COUNT];

static inline const struct fy_type_kind_info *fy_type_kind_info_get_internal(enum fy_type_kind type_kind)
{
	assert(fy_type_kind_is_valid(type_kind));
	return &fy_type_kind_info_table[type_kind];
}

FY_TYPE_FWD_DECL_LIST(type_info_wrapper);
struct fy_type_info_wrapper {
	struct list_head node;
	bool created;
	struct fy_type_info type_info;
	struct fy_decl **field_decls;
	struct fy_field_info *fields;
};
FY_TYPE_DECL_LIST(type_info_wrapper);

FY_TYPE_FWD_DECL_LIST(unresolved_dep);
struct fy_unresolved_dep {
	struct list_head node;
	struct fy_type_info_wrapper *tiw;
};
FY_TYPE_DECL_LIST(unresolved_dep);

void fy_unresolved_dep_destroy(struct fy_unresolved_dep *udep);
int fy_unresolved_dep_register_wrapper(struct fy_type_info_wrapper *tiw);

int fy_type_register_unresolved(struct fy_type *ft);
void fy_type_unregister_unresolved(struct fy_type *ft);

#define FY_QUALIFIER_CONST_IDX		0
#define FY_QUALIFIER_VOLATILE_IDX	1
#define FY_QUALIFIER_RESTRICT_IDX	2
#define FY_QUALIFIER_COUNT		3

#define FY_QUALIFIER_BIT_START		FYTK_PRIMARY_BITS 
#define FY_QUALIFIER_CONST		FY_BIT(FY_QUALIFIER_BIT_START + FY_QUALIFIER_CONST_IDX)
#define FY_QUALIFIER_VOLATILE		FY_BIT(FY_QUALIFIER_BIT_START + FY_QUALIFIER_VOLATILE_IDX)
#define FY_QUALIFIER_RESTRICT		FY_BIT(FY_QUALIFIER_BIT_START + FY_QUALIFIER_RESTRICT_IDX)

const char *fy_parse_c_qualifiers(const char *s, size_t len, unsigned int *qualsp);
const char *fy_parse_c_primitive_type(const char *s, size_t len,
	     enum fy_type_kind *type_kindp);
const char *fy_parse_c_base_type(const char *s, size_t len,
	     enum fy_type_kind *type_kindp, const char **namep, size_t *name_lenp,
	     unsigned int *qualsp);

enum fy_type_flags {
	/* those must be present */
	FYTF_CONST			= FY_BIT(0),
	FYTF_RESTRICT			= FY_BIT(1),
	FYTF_VOLATILE			= FY_BIT(2),
	FYTF_ELABORATED			= FY_BIT(3),	/* an elaborated type */
	FYTF_ANONYMOUS			= FY_BIT(4),
	FYTF_ANONYMOUS_RECORD_DECL	= FY_BIT(5),
	FYTF_ANONYMOUS_GLOBAL		= FY_BIT(6),	/* a global anonymous type (only enums) */
	FYTF_INCOMPLETE			= FY_BIT(7),
	FYTF_ANONYMOUS_DEP		= FY_BIT(8),
	FYTF_UNRESOLVED			= FY_BIT(9),
	FYTF_FAKE_RESOLVED		= FY_BIT(10),
	FYTF_SYNTHETIC			= FY_BIT(11),
	FYTF_FIXED			= FY_BIT(12),
	FYTF_FIX_IN_PROGRESS		= FY_BIT(13),
	FYTF_MARKER			= FY_BIT(14),
	FYTF_MARK_IN_PROGRESS		= FY_BIT(15),	/* mark in progress */
	FYTF_NEEDS_NAME			= FY_BIT(16),
	FYTF_UPDATE_TYPE_INFO		= FY_BIT(17),
	FYTF_TYPE_INFO_UPDATED		= FY_BIT(18),
	FYTF_TYPE_INFO_UPDATING		= FY_BIT(19),
};

FY_TYPE_FWD_DECL_LIST(type);
struct fy_type {
	struct list_head node;
	int id;
	struct fy_reflection *rfl;
	enum fy_type_kind type_kind;
	char *fullname;	/* including the prefix i.e. struct */
	size_t fullname_len;
	char *backend_name;
	struct fy_decl *decl;
	struct fy_type *unqualified_type;	/* when clone */
	struct fy_type *qualified_types[1 << FY_QUALIFIER_COUNT];

	size_t size;
	size_t align;
	void *backend;
	uintmax_t element_count;	/* for const-array */
	struct fy_type *dependent_type;

	enum fy_type_flags flags;
	int marker;

	/* keep everything related to type_info here */
	struct fy_type_info_wrapper tiw;
};
FY_TYPE_DECL_LIST(type);

static inline struct fy_decl *
fy_type_decl(struct fy_type *ft)
{
	if (!ft)
		return NULL;

	/* for an elaborated type, the declaration is at the unqualified type */
	if (ft->flags & FYTF_ELABORATED) {
		ft = ft->unqualified_type;
		if (!ft)
			return NULL;
	}
	assert(!(ft->flags & FYTF_ELABORATED));
	return ft->decl;
}

static inline struct fy_type_info_wrapper *
fy_type_info_wrapper_from_info(const struct fy_type_info *ti)
{
	if (!ti)
		return NULL;
	return container_of(ti, struct fy_type_info_wrapper, type_info);
}

static inline struct fy_type *
fy_type_from_info_wrapper(struct fy_type_info_wrapper *tiw)
{
	if (!tiw)
		return NULL;
	return container_of(tiw, struct fy_type, tiw);
}

static inline struct fy_type *
fy_type_from_info(const struct fy_type_info *ti)
{
	return fy_type_from_info_wrapper(fy_type_info_wrapper_from_info(ti));
}

FY_TYPE_FWD_DECL_LIST(source_file);
struct fy_source_file {
	struct list_head node;
	int id;
	char *filename;
	char *realpath;
	time_t filetime;
	bool system_header;
	bool main_file;
	int marker;
};
FY_TYPE_DECL_LIST(source_file);

struct fy_source_range {
	struct fy_source_file *source_file;
	unsigned int start_line, end_line;
	unsigned int start_column, end_column;
	size_t start_offset, end_offset;
};

struct fy_source_location {
	struct fy_source_file *source_file;
	unsigned int line;
	unsigned int column;
	size_t offset;
};

enum fy_decl_type {
	/* no declaration */
	FYDT_NONE,

	/* declarations */
	FYDT_STRUCT,
	FYDT_UNION,
	FYDT_CLASS,
	FYDT_ENUM,
	FYDT_TYPEDEF,
	FYDT_FUNCTION,

	/* fields */
	FYDT_FIELD,
	FYDT_BITFIELD,
	FYDT_ENUM_VALUE,

	/* synthetic primitive */
	FYDT_PRIMITIVE,
	/* the primary types, i.e. char, int, float... */
	FYDT_PRIMARY,
};
#define FYDT_COUNT	(FYDT_PRIMARY + 1)

#define FYDT_BITS	4
#if FYDT_COUNT > (1 << FYDT_BITS)
#error FYDT bits is wrong
#endif

extern const char *decl_type_txt[FYDT_COUNT];

struct fy_decl_type_info {
	enum fy_decl_type type;
	const char *name;
	const char *enum_name;
};

extern const struct fy_decl_type_info fy_decl_type_info_table[FYDT_COUNT];

static inline bool fy_decl_type_is_valid(enum fy_decl_type type)
{
	return type >= FYDT_STRUCT && type <= FYDT_PRIMARY;
}

static inline bool fy_decl_type_has_fields(enum fy_decl_type type)
{
	return type >= FYDT_STRUCT && type <= FYDT_ENUM;
}

static inline bool fy_decl_type_is_field(enum fy_decl_type type)
{
	return type >= FYDT_FIELD && type <= FYDT_ENUM_VALUE;
}

static inline bool fy_decl_type_has_children(enum fy_decl_type type)
{
	return type >= FYDT_STRUCT && type <= FYDT_ENUM;
}

static inline bool fy_decl_type_has_parent(enum fy_decl_type type)
{
	return type >= FYDT_FIELD && type <= FYDT_ENUM_VALUE;
}

static inline bool fy_decl_type_has_name(enum fy_decl_type type)
{
	return type >= FYDT_STRUCT && type <= FYDT_ENUM_VALUE;
}

enum fy_decl_flags {
	FYDF_IN_SYSTEM_HEADER		= FY_BIT(0),
	FYDF_FROM_MAIN_FILE		= FY_BIT(1),
	FYDF_META_PARSED		= FY_BIT(2),
	FYDF_MARK_IN_PROGRESS		= FY_BIT(3),
};

FY_TYPE_FWD_DECL_LIST(decl);
struct fy_decl {
	struct list_head node;
	int id;
	struct fy_reflection *rfl;
	struct fy_import *imp;
	struct fy_decl *parent;
	enum fy_decl_type decl_type;
	const char *name;
	size_t name_len;
	char *name_alloc;
	const struct fy_source_range *source_range;
	struct fy_decl_list children;
	const char *raw_comment;
	struct fy_type *type;
	void *backend;
	enum fy_decl_flags flags;
	int marker;

	/* must be freed if not NULL */
	char *cooked_comment;
	struct fy_document *fyd_yaml;	/* the YAML keyworded */
	char *yaml_comment;
	bool yaml_comment_generated;

	union {
		struct {
			enum fy_type_kind type_kind;
		} enum_decl;
		struct {
			enum fy_type_kind type_kind;
			union {
				intmax_t s;
				uintmax_t u;
			} val;
		} enum_value_decl;
		struct {
			size_t byte_offset;
		} field_decl;
		struct {
			size_t bit_offset;
			size_t bit_width;
		} bitfield_decl;
	};
};
FY_TYPE_DECL_LIST(decl);

FY_TYPE_FWD_DECL_LIST(import);
struct fy_import {
	struct list_head node;
	struct fy_reflection *rfl;
	const char *name;
	void *backend;
	int marker;
};
FY_TYPE_DECL_LIST(import);

struct fy_reflection_backend_ops {
	int (*reflection_setup)(struct fy_reflection *rfl);
	void (*reflection_cleanup)(struct fy_reflection *rfl);

	int (*import_setup)(struct fy_import *imp, const void *frontend_user);
	void (*import_cleanup)(struct fy_import *imp);

	int (*type_setup)(struct fy_type *ft, void *backend_user);
	void (*type_cleanup)(struct fy_type *ft);

	int (*decl_setup)(struct fy_decl *decl, void *backend_user);
	void (*decl_cleanup)(struct fy_decl *decl);
};

struct fy_reflection_backend {
	const char *name;
	const struct fy_reflection_backend_ops *ops;
};

struct fy_reflection_internal_cfg {
	struct fy_diag *diag;
	const void *backend_cfg;
	const struct fy_reflection_backend *backend;
};

#define FY_PRIMARY_ID_FIRST		0
#define FY_PRIMARY_ID_COUNT		(1 << (FYTK_PRIMARY_BITS + FY_QUALIFIER_COUNT))
#define FY_PRIMARY_ID_LAST		(FY_PRIMARY_ID_COUNT - 1)

#define FY_USER_DEFINED_ID_START	FY_PRIMARY_ID_COUNT

#define FY_TYPE_ID_OFFSET		FY_USER_DEFINED_ID_START
#define FY_DECL_ID_OFFSET		1

struct fy_reflection {
	struct fy_reflection_internal_cfg cfg;
	struct fy_diag *diag;
	struct fy_import_list imports;
	struct fy_source_file_list source_files;
	struct fy_type_list types;
	struct fy_decl_list decls;
	struct fy_unresolved_dep_list unresolved_deps;
	struct fy_type *primary_types[FY_PRIMARY_ID_COUNT];
	int next_type_id;
	int next_decl_id;
	int next_source_file_id;
	int next_anonymous_struct_id;
	int next_anonymous_union_id;
	int next_anonymous_enum_id;
	void *backend;
	struct fy_import *imp_curr;	/* the current import */
	void *userdata;
};

struct fy_type *fy_type_create(struct fy_reflection *rfl, enum fy_type_kind type_kind,
				enum fy_type_flags flags, const char *name, struct fy_decl *decl,
				struct fy_type *ft_dep, void *user,
				uintmax_t element_count);
void fy_type_destroy(struct fy_type *ft);

struct fy_type_info_wrapper *fy_type_get_info_wrapper(struct fy_type *ft, struct fy_decl *decl);

int fy_type_create_info(struct fy_type *ft);

void fy_type_update_info_flags(struct fy_type *ft);
int fy_type_update_info(struct fy_type *ft);
int fy_type_update_all_info(struct fy_reflection *rfl);
void fy_type_set_flags(struct fy_type *ft, enum fy_type_flags flags_set, enum fy_type_flags flags_mask);
void fy_type_all_set_flags(struct fy_type *ft, enum fy_type_flags flags_set, enum fy_type_flags flags_mask);

struct fy_type *fy_type_create_ptr_dep(struct fy_type *ft);	/* for final reference resolution */
int fy_type_update_elaborated(struct fy_type *ft, void *user);
int fy_type_update_all_elaborated(struct fy_type *ft);
struct fy_type *fy_type_with_qualifiers(struct fy_type *ft_src, unsigned int quals);
struct fy_type *fy_type_create_with_qualifiers(struct fy_type *ft_src, unsigned int quals, void *user);
struct fy_type *fy_type_unqualified(struct fy_type *ft);

bool fy_c_decl_equal(const char *a, size_t alen, const char *b, size_t blen);
struct fy_type *fy_type_lookup(struct fy_reflection *rfl, const char *name, size_t name_len);
struct fy_type *fy_type_lookup_by_kind(struct fy_reflection *rfl, enum fy_type_kind type_kind,
		       const char *name, size_t name_len, unsigned int quals);
struct fy_type *fy_type_lookup_or_create(struct fy_reflection *rfl, const char *name, size_t name_len);

void fy_decl_destroy(struct fy_decl *decl);
struct fy_decl *fy_decl_create(struct fy_reflection *rfl, struct fy_import *imp,
		struct fy_decl *parent, enum fy_decl_type decl_type, const char *name, void *user);
const char *fy_decl_get_type_kind_spelling(struct fy_decl *decl);
const char *fy_decl_get_type_spelling(struct fy_decl *decl);
bool fy_decl_enum_value_is_unsigned(struct fy_decl *decl);
intmax_t fy_decl_enum_value_signed(struct fy_decl *decl);
uintmax_t fy_decl_enum_value_unsigned(struct fy_decl *decl);
bool fy_decl_field_is_bitfield(struct fy_decl *decl);
size_t fy_decl_field_offsetof(struct fy_decl *decl);
size_t fy_decl_field_bit_offsetof(struct fy_decl *decl);
size_t fy_decl_field_sizeof(struct fy_decl *decl);
size_t fy_decl_field_bit_width(struct fy_decl *decl);
const struct fy_source_range *fy_decl_get_source_range(struct fy_decl *decl);
const char *fy_decl_get_raw_comment(struct fy_decl *decl);
const char *fy_decl_get_cooked_comment(struct fy_decl *decl);
struct fy_document *fy_decl_get_yaml_annotation(struct fy_decl *decl);
const char *fy_decl_get_yaml_comment(struct fy_decl *decl);

struct fy_import *fy_import_create(struct fy_reflection *rfl, const void *user);
void fy_import_destroy(struct fy_import *imp);

const char *fy_import_get_target_triple(struct fy_import *imp);

struct fy_source_file *fy_source_file_create(struct fy_reflection *rfl, const char *filename);
void fy_source_file_destroy(struct fy_source_file *srcf);
struct fy_source_file *fy_reflection_lookup_source_file(struct fy_reflection *rfl, const char *filename);
void fy_source_file_dump(struct fy_source_file *srcf);

struct fy_reflection *fy_reflection_create_internal(const struct fy_reflection_internal_cfg *rflc);
int fy_reflection_import(struct fy_reflection *rfl, const void *user);
void fy_reflection_renumber(struct fy_reflection *rfl);

struct fy_type *fy_reflection_get_primary_type(struct fy_reflection *rfl, enum fy_type_kind type_kind, unsigned int quals);

int fy_type_fixup(struct fy_type *ft);
int fy_reflection_fixup(struct fy_reflection *rfl);

int fy_type_generate_name(struct fy_type *ft);
int fy_type_set_dependent(struct fy_type *ft, struct fy_type *ft_dep);

void fy_reflection_dump(struct fy_reflection *rfl, bool marked_only, bool no_location);
void fy_type_dump(struct fy_type *ft, bool no_location);
void fy_decl_dump(struct fy_decl *decl, int start_level, bool no_location);

const char *fy_import_get_name(struct fy_import *imp);
void fy_import_clear_marker(struct fy_import *imp);
void fy_import_mark(struct fy_import *imp);

void fy_decl_clear_marker(struct fy_decl *decl);
void fy_decl_mark(struct fy_decl *decl);

void fy_type_clear_marker(struct fy_type *ft);
void fy_type_mark(struct fy_type *ft);

void fy_source_file_clear_marker(struct fy_source_file *srcf);
void fy_source_file_mark(struct fy_source_file *srcf);

const struct fy_reflection_backend *
fy_reflection_backend_lookup(const char *name);

struct fy_decl *fy_type_get_anonymous_parent_decl(struct fy_type *ft);
size_t fy_type_eponymous_offset(struct fy_type *ft);

static inline struct fy_decl *fy_decl_from_field_info(const struct fy_field_info *fi)
{
	struct fy_type_info_wrapper *tiw;
	int idx;

	if (!fi)
		return NULL;
	tiw = fy_type_info_wrapper_from_info(fi->parent);
	assert(tiw);
	idx = fy_field_info_index(fi);
	assert((size_t)idx < fi->parent->count && tiw->field_decls);
	return tiw->field_decls[idx];
}

#define FYTGTF_NO_TYPE		FY_BIT(0)
#define FYTGTF_NO_FIELD		FY_BIT(1)
#define FYTGTF_DEBUG		FY_BIT(2)

char *fy_type_generate_c_declaration(struct fy_type *ft, const char *field, unsigned int flags);

struct fy_reflection_log_ctx {
	struct fy_reflection *rfl;
	struct fy_diag_ctx diag_ctx;
	bool has_diag_ctx;
	bool save_error;
};

void
fy_reflection_vlog(struct fy_reflection_log_ctx *ctx, enum fy_error_type error_type,
		   const char *fmt, va_list ap);

void
fy_reflection_log(struct fy_reflection_log_ctx *ctx, enum fy_error_type error_type,
		  const char *fmt, ...)
	FY_FORMAT(printf, 3, 4);

#define _RFL_ERROR_CHECK(_rfl, _expr, _label, _save_error, _fmt, ...) \
	do { \
		if (!(_expr)) { \
			struct fy_reflection_log_ctx __ctx = { \
				.rfl = (_rfl), \
				.diag_ctx = { \
					.level = FYET_ERROR, \
					.module = FYEM_REFLECTION, \
					.source_func = __func__, \
					.source_file = __FILE__, \
					.source_line = __LINE__, \
				}, \
				.has_diag_ctx = true, \
				.save_error = (_save_error), \
			}; \
			fy_reflection_log(&__ctx, FYET_ERROR, (_fmt), ## __VA_ARGS__); \
			goto _label ; \
		} \
	} while(0)

/* reflection error check, assume rfl in scope and an err_out label */
#define RFL_ERROR_CHECK(_expr, _fmt, ...) \
	_RFL_ERROR_CHECK(rfl, (_expr), err_out, true, (_fmt) , ## __VA_ARGS__)

/* reflection assert, assume rfl in scope and an err_out label */
#define RFL_ASSERT(_expr) \
	_RFL_ERROR_CHECK(rfl, (_expr), err_out, false, "%s: %s:%d: assert failed " #_expr "\n", \
			__func__, __FILE__, __LINE__)

#define fy_reflection_diag(_rfl, _level, _fmt, ...) \
	({ \
		const enum fy_error_type __level = (_level); \
		struct fy_reflection_log_ctx __ctx = { \
			.rfl = (_rfl), \
			.diag_ctx = { \
				.level = __level, \
				.module = FYEM_REFLECTION, \
				.source_func = __func__, \
				.source_file = __FILE__, \
				.source_line = __LINE__, \
			}, \
			.has_diag_ctx = true, \
		}; \
		fy_reflection_log(&__ctx, __level, (_fmt), ## __VA_ARGS__); \
	})

#ifndef NDEBUG

#define rfl_debug(_rfl, _fmt, ...) \
	fy_reflection_diag((_rfl), FYET_DEBUG, (_fmt) , ## __VA_ARGS__)
#else

#define rfl_debug(_rfl, _fmt, ...) \
	do { } while(0)

#endif

#define rfl_info(_rfl, _fmt, ...) \
	fy_reflection_diag((_rfl), FYET_INFO, (_fmt) , ## __VA_ARGS__)
#define rfl_notice(_rfl, _fmt, ...) \
	fy_reflection_diag((_rfl), FYET_NOTICE, (_fmt) , ## __VA_ARGS__)
#define rfl_warning(_rfl, _fmt, ...) \
	fy_reflection_diag((_rfl), FYET_WARNING, (_fmt) , ## __VA_ARGS__)
#define rfl_error(_rfl, _fmt, ...) \
	fy_reflection_diag((_rfl), FYET_ERROR, (_fmt) , ## __VA_ARGS__)

#endif
