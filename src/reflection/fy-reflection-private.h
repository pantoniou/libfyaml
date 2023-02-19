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

#include "libfyaml.h"

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

FY_TYPE_FWD_DECL_LIST(type);
struct fy_type {
	struct list_head node;
	int id;
	struct fy_reflection *rfl;
	enum fy_type_kind type_kind;
	char *fullname;	/* including the prefix i.e. struct */
	const char *name;	/* points in full name */
	char *normalized_name;
	struct fy_decl *decl;
	size_t size;
	size_t align;
	void *backend;
	unsigned long long element_count;	/* for const-array */
	bool unresolved;
	bool anonymous;
	struct fy_type *dependent_type;
	enum fy_type_kind dependent_type_kind;
	const char *dependent_type_name;
	bool was_fwd_declared;
	bool is_const;
	bool is_restrict;
	bool is_volatile;
	bool is_fake_resolved;
	bool is_synthetic;
	bool is_fixed;
	bool fix_in_progress;
	bool marker;
	void *fake_resolve_data;

	bool has_type_info;
	bool producing_type_info;	/* for handling recursives */
	/* the only public interface */
	struct fy_type_info type_info;
	/* cache so that things are faster */
	struct fy_decl **field_decls;

	void *userdata;
};
FY_TYPE_DECL_LIST(type);

static inline struct fy_type *
fy_type_from_info(const struct fy_type_info *ti)
{
	if (!ti)
		return NULL;
	return container_of(ti, struct fy_type, type_info);
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
	bool marker;
};
FY_TYPE_DECL_LIST(source_file);

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
};
#define FYDT_COUNT (FYDT_ENUM_VALUE + 1)

extern const char *decl_type_txt[FYDT_COUNT];

struct fy_decl_type_info {
	enum fy_decl_type type;
	const char *name;
	const char *enum_name;
};

extern const struct fy_decl_type_info fy_decl_type_info_table[FYDT_COUNT];

static inline bool fy_decl_type_is_valid(enum fy_decl_type type)
{
	return type >= FYDT_STRUCT && type <= FYDT_ENUM_VALUE;
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

FY_TYPE_FWD_DECL_LIST(decl);
struct fy_decl {
	struct list_head node;
	struct fy_reflection *rfl;
	struct fy_import *imp;
	struct fy_decl *parent;
	enum fy_decl_type decl_type;
	char *name;
	const struct fy_source_location *source_location;
	const char *spelling;
	const char *display_name;
	const char *signature;
	struct fy_decl_list children;
	const char *raw_comment;
	struct fy_type *type;
	bool anonymous;
	int id;
	void *backend;
	bool marker;

	bool in_system_header;
	bool from_main_file;
	bool is_synthetic;
	void *userdata;

	/* must be freed if not NULL */
	char *cooked_comment;
	struct fy_document *fyd_yaml;	/* the YAML keyworded */
	bool fyd_yaml_parsed;
	char *yaml_comment;

	union {
		struct {
			enum fy_type_kind type_kind;
		} enum_decl;
		struct {
			enum fy_type_kind type_kind;
			union {
				long long s;
				unsigned long long u;
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
	bool marker;
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

struct fy_reflection_cfg {
	const void *backend_cfg;
	const struct fy_reflection_backend *backend;
};

struct fy_reflection {
	struct fy_reflection_cfg cfg;
	struct fy_import_list imports;
	struct fy_source_file_list source_files;
	struct fy_type_list types;
	struct fy_decl_list decls;
	int unresolved_types_count;
	int next_type_id;
	int next_decl_id;
	int next_source_file_id;
	void *backend;
	struct fy_import *imp_curr;	/* the current import */
};

struct fy_type *fy_type_create(struct fy_reflection *rfl, enum fy_type_kind type_kind, const char *name, struct fy_decl *decl, void *user);
void fy_type_destroy(struct fy_type *ft);
void fy_type_fixup_size_align(struct fy_type *ft);

bool fy_type_is_pointer(struct fy_type *ft);
bool fy_type_is_array(struct fy_type *ft);
bool fy_type_is_constant_array(struct fy_type *ft);
bool fy_type_is_incomplete_array(struct fy_type *ft);
int fy_type_get_constant_array_element_count(struct fy_type *ft);
struct fy_type *fy_type_get_dependent_type(struct fy_type *ft);
size_t fy_type_get_sizeof(struct fy_type *ft);
size_t fy_type_get_alignof(struct fy_type *ft);
int fy_type_get_field_count(struct fy_type *ft);
struct fy_decl *fy_type_get_field_decl_by_name(struct fy_type *ft, const char *field);
struct fy_decl *fy_type_get_field_decl_by_enum_value(struct fy_type *ft, long long val);
struct fy_decl *fy_type_get_field_decl_by_unsigned_enum_value(struct fy_type *ft, unsigned long long val);
int fy_type_get_field_index_by_name(struct fy_type *ft, const char *field);
struct fy_decl *fy_type_get_field_decl_by_idx(struct fy_type *ft, unsigned int idx);
int fy_type_get_field_idx_by_decl(struct fy_type *ft, struct fy_decl *decl);
const struct fy_source_location *fy_type_get_decl_location(struct fy_type *ft);
const char *fy_type_get_raw_comment(struct fy_type *ft);
const char *fy_type_get_cooked_comment(struct fy_type *ft);
struct fy_document *fy_type_get_yaml_annotation(struct fy_type *ft);
const char *fy_type_get_yaml_comment(struct fy_type *ft);

void fy_decl_destroy(struct fy_decl *decl);
struct fy_decl *fy_decl_create(struct fy_reflection *rfl, struct fy_import *imp,
		struct fy_decl *parent, enum fy_decl_type decl_type, const char *name, void *user);
const char *fy_decl_get_type_kind_spelling(struct fy_decl *decl);
const char *fy_decl_get_type_spelling(struct fy_decl *decl);
bool fy_decl_enum_value_is_unsigned(struct fy_decl *decl);
long long fy_decl_enum_value_signed(struct fy_decl *decl);
unsigned long long fy_decl_enum_value_unsigned(struct fy_decl *decl);
bool fy_decl_field_is_bitfield(struct fy_decl *decl);
size_t fy_decl_field_offsetof(struct fy_decl *decl);
size_t fy_decl_field_bit_offsetof(struct fy_decl *decl);
size_t fy_decl_field_sizeof(struct fy_decl *decl);
size_t fy_decl_field_bit_width(struct fy_decl *decl);
bool fy_decl_is_in_system_header(struct fy_decl *decl);
bool fy_decl_is_from_main_file(struct fy_decl *decl);
const struct fy_source_location *fy_decl_get_location(struct fy_decl *decl);
const char *fy_decl_get_spelling(struct fy_decl *decl);
const char *fy_decl_get_display_name(struct fy_decl *decl);
const char *fy_decl_get_signature(struct fy_decl *decl);
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

struct fy_reflection *fy_reflection_create(const struct fy_reflection_cfg *rflc);
int fy_reflection_import(struct fy_reflection *rfl, const void *user);
struct fy_type *fy_reflection_lookup_type(struct fy_reflection *rfl, enum fy_type_kind type_kind, const char *name);
void fy_reflection_renumber(struct fy_reflection *rfl);
void fy_reflection_fix_unresolved(struct fy_reflection *rfl);
void fy_reflection_update_type_info(struct fy_reflection *rfl);

void fy_reflection_fixup_size_align(struct fy_reflection *rfl);
void fy_reflection_dump(struct fy_reflection *rfl, bool marked_only, bool no_location);

static inline int fy_type_id(const struct fy_type *ft)
{
	return ft->id;
}

static inline bool fy_type_is_anonymous(const struct fy_type *ft)
{
	return ft->anonymous;
}

static inline bool fy_type_is_declared(const struct fy_type *ft)
{
	return ft && !fy_type_kind_is_primitive(ft->type_kind) && ft->decl;
}

static inline bool fy_type_is_resolved(const struct fy_type *ft)
{
	return ft && !ft->unresolved;
}

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

char *fy_type_generate_name(struct fy_type *ft, const char *field, bool normalized);
struct fy_decl *fy_type_get_anonymous_parent_decl(struct fy_type *ft);
size_t fy_type_eponymous_offset(struct fy_type *ft);

static inline void fy_type_set_userdata(struct fy_type *ft, void *data)
{
	if (!ft)
		return;
	ft->userdata = data;
}

static inline void *fy_type_get_userdata(struct fy_type *ft)
{
	return ft ? ft->userdata : NULL;
}

static inline void fy_decl_set_userdata(struct fy_decl *decl, void *data)
{
	if (!decl)
		return;
	decl->userdata = data;
}

static inline void *fy_decl_get_userdata(struct fy_decl *decl)
{
	return decl ? decl->userdata : NULL;
}

static inline struct fy_decl *fy_decl_from_field_info(const struct fy_field_info *fi)
{
	struct fy_type *ft;
	int idx;

	if (!fi)
		return NULL;
	ft = fy_type_from_info(fi->parent);
	assert(ft);
	idx = fy_field_info_index(fi);
	assert((size_t)idx < fi->parent->count && ft->field_decls);
	return ft->field_decls[idx];
}

enum fy_reflection_record_missing_fields {
	FYRRMF_FORBIDDEN, /* yaml: { name: forbidden } */
	FYRRMF_PERMITTED, /* yaml: { name: permitted } */
};

struct fy_reflection_record_schema {
	enum fy_reflection_record_missing_fields missing_fields;	/* yaml: { name: missing-fields } */
};

#endif
