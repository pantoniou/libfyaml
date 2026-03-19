/*
 * fy-reflect-meta.h - Reflection meta-type system (library-private internal header)
 *
 * Copyright (c) 2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_REFLECT_META_H
#define FY_REFLECT_META_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-reflection.h>
#include "fy-reflection-private.h"

#include "fy-reflection-private.h"
#include "fy-reflection-util.h"

struct reflection_type_system;
struct reflection_type_data;
struct reflection_field_data;
struct reflection_type_ops;
struct reflection_walker;
struct reflection_any_value;
struct reflection_meta;
struct reflection_type_system_config;
struct reflection_type_system_ops;

/* generic reflection */
void reflection_type_info_dump(struct fy_reflection *rfl);

/* type system */

#define RTSCF_ANNOTATION_MODE_SHIFT	3
#define RTSCF_ANNOTATION_MODE_MASK	((1U << 2) - 1)
#define RTSCF_ANNOTATION_MODE(x)	(((unsigned int)(x) & RTSCF_ANNOTATION_MODE_MASK) << RTSCF_ANNOTATION_MODE_SHIFT)

enum reflection_type_system_config_flags {
	RTSCF_DUMP_REFLECTION 		= FY_BIT(0),
	RTSCF_DUMP_TYPE_SYSTEM		= FY_BIT(1),
	RTSCF_DEBUG			= FY_BIT(2),
	RTSCF_ANNOTATION_MODE_YAML_1_2	= RTSCF_ANNOTATION_MODE(0),
	RTSCF_ANNOTATION_MODE_YAML_1_1	= RTSCF_ANNOTATION_MODE(1),
	RTSCF_ANNOTATION_MODE_JSON	= RTSCF_ANNOTATION_MODE(2),
	RTSCF_ANNOTATION_MODE_DEFAULT	= RTSCF_ANNOTATION_MODE_YAML_1_2,
	/* Treat unknown annotation keys as errors rather than notices */
	RTSCF_STRICT_ANNOTATIONS	= FY_BIT(5),
};

static inline enum fy_parser_mode
reflection_type_system_config_flags_to_parse_mode(enum reflection_type_system_config_flags flags)
{
	switch (flags & (RTSCF_ANNOTATION_MODE_MASK << RTSCF_ANNOTATION_MODE_SHIFT)) {
	case RTSCF_ANNOTATION_MODE_YAML_1_1:
		return fypm_yaml_1_1;
	case RTSCF_ANNOTATION_MODE_JSON:
		return fypm_json;
	default:
		break;
	}
	return fypm_yaml_1_2;
}

struct reflection_type_system_ops {
	void *(*malloc)(struct reflection_type_system *rts, size_t size);
	void *(*realloc)(struct reflection_type_system *rts, void *ptr, size_t size);
	void (*free)(struct reflection_type_system *rts, void *ptr);
	const void *(*store)(struct reflection_type_system *rts, const void *ptr, size_t size);
	const void *(*lookup)(struct reflection_type_system *rts, const void *ptr, size_t size);
};

struct reflection_type_system_config {
	struct fy_reflection *rfl;
	const char *entry_type;
	const char *entry_meta;
	const struct reflection_type_system_ops *ops;
	void *user;
	unsigned int flags;
	struct fy_diag *diag;
};

struct reflection_reference {
	struct reflection_type_system *rts;
	char *name;
	char *meta;
	struct reflection_type_data *rtd_root;
	struct reflection_field_data *rfd_root;
	struct fy_type_info rfd_root_ti;
	struct fy_field_info rfd_root_fi;
};

struct reflection_type_system {
	struct reflection_type_system_config cfg;
	struct reflection_reference *root_ref;
	int rtd_next_idx;
	struct fy_diag *diag;
	bool had_unknown_annotations;
};

enum reflection_parse_flags {
	RPF_VERBOSE			= FY_BIT(0),
	RPF_SILENT_INVALID_INPUT	= FY_BIT(1),	/* when doing input probing */
	RPF_SILENT_ALL			= FY_BIT(2),	/* no diagnostics */
	RPF_NO_STORE			= FY_BIT(3),	/* do not store object */
	RPF_DEBUG_0			= FY_BIT(16),
	RPF_DEBUG_1			= FY_BIT(17),
};

enum reflection_emit_flags {
	REF_EMIT_SS 			= FY_BIT(0),
	REF_EMIT_DS 			= FY_BIT(1),
	REF_EMIT_DE 			= FY_BIT(2),
	REF_EMIT_SE 			= FY_BIT(3),
	REF_VERBOSE			= FY_BIT(4),
	REF_SILENT_INVALID_OUTPUT	= FY_BIT(5),	/* when doing input probing */
	REF_SILENT_ALL			= FY_BIT(6),
	REF_DEBUG_0			= FY_BIT(16),
	REF_DEBUG_1			= FY_BIT(17),
};

static inline enum fy_parser_mode
reflection_type_system_parse_mode(struct reflection_type_system *rts)
{
	if (!rts)
		return fypm_yaml_1_2;
	return reflection_type_system_config_flags_to_parse_mode(rts->cfg.flags);
}

struct reflection_type_system *
reflection_type_system_create(const struct reflection_type_system_config *cfg);
void reflection_type_system_destroy(struct reflection_type_system *rts);

void *reflection_malloc(struct reflection_type_system *rts, size_t size);
void *reflection_realloc(struct reflection_type_system *rts, void *ptr, size_t size);
void reflection_free(struct reflection_type_system *rts, void *ptr);

/* any value */
struct reflection_any_value {
	struct reflection_type_system *rts;
	struct fy_document *fyd;
	struct fy_node *fyn;
	struct reflection_type_data *rtd;
	void *value;
	char *str;	/* converted to str */
};

struct reflection_any_value *
reflection_any_value_create(struct reflection_type_system *rts, struct fy_node *fyn);
void reflection_any_value_destroy(struct reflection_any_value *rav);

const char *
reflection_any_value_get_str(struct reflection_any_value *rav);
struct reflection_any_value *
reflection_any_value_copy(struct reflection_any_value *rav_src);

void *reflection_any_value_generate(struct reflection_any_value *rav,
				    struct reflection_type_data *rtd);
int
reflection_any_value_equal_rw(struct reflection_any_value *rav,
			      struct reflection_walker *rw);

/* meta value */
enum reflection_meta_value_id {
	rmvid_invalid,
	/* booleans */
	rmvid_required,
	rmvid_omit_on_emit,
	rmvid_omit_if_empty,
	rmvid_omit_if_default,
	rmvid_omit_if_null,
	rmvid_match_null,
	rmvid_match_seq,
	rmvid_match_map,
	rmvid_match_scalar,
	rmvid_match_always,
	rmvid_not_null_terminated,
	rmvid_not_string,
	rmvid_null_allowed,
	rmvid_field_auto_select,
	rmvid_flatten_field_first_anonymous,
	rmvid_skip_unknown,
	rmvid_enum_or_seq,
	/* strings */
	rmvid_counter,
	rmvid_key,
	rmvid_selector,
	rmvid_name,
	rmvid_remove_prefix,
	rmvid_flatten_field,
	/* any */
	rmvid_terminator,
	rmvid_default,
	rmvid_select,
	rmvid_fill,
};

#define rmvid_count		(rmvid_fill + 1)

#define rmvid_first_valid	rmvid_required
#define rmvid_last_valid	rmvid_fill
#define rmvid_valid_count	(rmvid_last_valid + 1 - rmvid_first_valid)

#define rmvid_first_bool	rmvid_required
#define rmvid_last_bool		rmvid_enum_or_seq
#define rmvid_bool_count	(rmvid_last_bool + 1 - rmvid_first_bool)

#define rmvid_first_str		rmvid_counter
#define rmvid_last_str		rmvid_flatten_field
#define rmvid_str_count		(rmvid_last_str + 1 - rmvid_first_str)

#define rmvid_first_any		rmvid_terminator
#define rmvid_last_any		rmvid_fill
#define rmvid_any_count		(rmvid_last_any + 1 - rmvid_first_any)

static inline bool
reflection_meta_value_id_is_valid(enum reflection_meta_value_id id)
{
	return id >= rmvid_first_valid && id <= rmvid_last_valid;
}

static inline const char *
reflection_meta_value_id_get_name(enum reflection_meta_value_id id)
{
	static const char *reflection_meta_names[rmvid_count] = {
		[rmvid_invalid]				= NULL,

		[rmvid_required]			= "required",
		[rmvid_omit_on_emit]			= "omit-on-emit",
		[rmvid_omit_if_empty]			= "omit-if-empty",
		[rmvid_omit_if_default]			= "omit-if-default",
		[rmvid_omit_if_null]			= "omit-if-null",
		[rmvid_match_null]			= "match-null",
		[rmvid_match_seq]			= "match-seq",
		[rmvid_match_map]			= "match-map",
		[rmvid_match_scalar]			= "match-scalar",
		[rmvid_match_always]			= "match-always",
		[rmvid_not_null_terminated]		= "not-null-terminated",
		[rmvid_not_string]			= "not-string",
		[rmvid_null_allowed]			= "null-allowed",
		[rmvid_field_auto_select]		= "field-auto-select",
		[rmvid_flatten_field_first_anonymous]	= "flatten-field-first-anonymous",
		[rmvid_skip_unknown]			= "skip-unknown",
		[rmvid_enum_or_seq]			= "enum-or-seq",

		[rmvid_counter]				= "counter",
		[rmvid_key]				= "key",
		[rmvid_selector]			= "selector",
		[rmvid_name]				= "name",
		[rmvid_remove_prefix]			= "remove-prefix",
		[rmvid_flatten_field]			= "flatten-field",

		[rmvid_terminator]			= "terminator",
		[rmvid_default]				= "default",
		[rmvid_select]				= "select",
		[rmvid_fill]				= "fill",
	};

	if (!reflection_meta_value_id_is_valid(id))
		return NULL;
	assert((unsigned int)id < ARRAY_SIZE(reflection_meta_names));
	return reflection_meta_names[id];
}

static inline bool
reflection_meta_value_id_is_bool(enum reflection_meta_value_id id)
{
	return id >= rmvid_first_bool && id <= rmvid_last_bool;
}

static inline bool
reflection_meta_value_id_is_str(enum reflection_meta_value_id id)
{
	return id >= rmvid_first_str && id <= rmvid_last_str;
}

static inline bool
reflection_meta_value_id_is_any(enum reflection_meta_value_id id)
{
	return id >= rmvid_first_any && id <= rmvid_last_any;
}

struct reflection_meta {
	struct reflection_type_system *rts;
	int explicit_count;
	uint8_t explicit_map[(rmvid_valid_count + 7) / 8];
	uint8_t bools[(rmvid_bool_count + 7) / 8];
	char *strs[rmvid_str_count];
	struct reflection_any_value *anys[rmvid_any_count];
};

static inline bool
reflection_meta_value_get_explicit(struct reflection_meta *rm, enum reflection_meta_value_id id)
{
	unsigned int i;

	assert(rm);
	assert(reflection_meta_value_id_is_valid(id));
	i = id - rmvid_first_valid;

	return !!(rm->explicit_map[i / 8] & FY_BIT(i & 7));
}

static inline void
reflection_meta_value_set_explicit(struct reflection_meta *rm, enum reflection_meta_value_id id,
				   bool this_explicit)
{
	unsigned int i;

	assert(rm);
	assert(reflection_meta_value_id_is_valid(id));
	i = id - rmvid_first_valid;

	if (this_explicit)
		rm->explicit_map[i / 8] |= FY_BIT(i & 7);
	else
		rm->explicit_map[i / 8] &= ~FY_BIT(i & 7);
}

static inline bool
reflection_meta_get_bool_default(struct reflection_meta *rm FY_UNUSED,
				 enum reflection_meta_value_id id)
{
	assert(reflection_meta_value_id_is_bool(id));

	switch (id) {
	case rmvid_required:
		return true;
	case rmvid_omit_on_emit:
	case rmvid_omit_if_empty:
	case rmvid_omit_if_default:
	case rmvid_omit_if_null:
	case rmvid_match_null:
	case rmvid_match_seq:
	case rmvid_match_map:
	case rmvid_match_scalar:
	case rmvid_match_always:
	case rmvid_not_null_terminated:
	case rmvid_not_string:
	case rmvid_null_allowed:
	case rmvid_field_auto_select:
	case rmvid_flatten_field_first_anonymous:
	case rmvid_skip_unknown:
	case rmvid_enum_or_seq:
		return false;
	default:
		break;
	}
	return false;
}

static inline bool
reflection_meta_get_bool(struct reflection_meta *rm, enum reflection_meta_value_id id)
{
	unsigned int i;

	assert(reflection_meta_value_id_is_bool(id));

	if (!rm || !reflection_meta_value_get_explicit(rm, id))
		return reflection_meta_get_bool_default(rm, id);

	i = id - rmvid_first_bool;
	return !!(rm->bools[i / 8] & FY_BIT(i & 7));
}

static inline const char *
reflection_meta_get_str_default(struct reflection_meta *rm FY_UNUSED,
				enum reflection_meta_value_id id)
{
	assert(reflection_meta_value_id_is_str(id));

	switch (id) {
	case rmvid_counter:
	case rmvid_key:
	case rmvid_selector:
	case rmvid_name:
	case rmvid_remove_prefix:
	case rmvid_flatten_field:
		return NULL;
	default:
		break;
	}
	return NULL;
}

static inline const char *
reflection_meta_get_str(struct reflection_meta *rm, enum reflection_meta_value_id id)
{
	assert(reflection_meta_value_id_is_str(id));

	if (!rm || !reflection_meta_value_get_explicit(rm, id))
		return reflection_meta_get_str_default(rm, id);

	return rm->strs[id - rmvid_first_str];
}

static inline struct reflection_any_value *
reflection_meta_get_any_value_default(struct reflection_meta *rm FY_UNUSED,
				      enum reflection_meta_value_id id)
{
	assert(reflection_meta_value_id_is_any(id));

	switch (id) {
	case rmvid_terminator:
	case rmvid_default:
	case rmvid_select:
	case rmvid_fill:
		return NULL;
	default:
		break;
	}
	return NULL;
}

static inline struct reflection_any_value *
reflection_meta_get_any_value(struct reflection_meta *rm, enum reflection_meta_value_id id)
{
	assert(reflection_meta_value_id_is_any(id));

	if (!rm || !reflection_meta_value_get_explicit(rm, id))
		return reflection_meta_get_any_value_default(rm, id);

	return rm->anys[id - rmvid_first_any];
}

static inline bool
reflection_meta_has_explicit(struct reflection_meta *rm)
{
	return rm && rm->explicit_count > 0;
}

static inline void *
reflection_meta_generate_any_value(struct reflection_meta *rm, enum reflection_meta_value_id id,
				   struct reflection_type_data *rtd)
{
	struct reflection_any_value *rav;

	if (!rtd)
		return NULL;

	rav = reflection_meta_get_any_value(rm, id);
	return rav ? reflection_any_value_generate(rav, rtd) : NULL;
}

struct reflection_meta *reflection_meta_create(struct reflection_type_system *rts);
void reflection_meta_destroy(struct reflection_meta *rm);
const char *reflection_meta_value_str(struct reflection_meta *rm, enum reflection_meta_value_id id);
void reflection_meta_dump(struct reflection_meta *rm);
int reflection_meta_fill(struct reflection_meta *rm, struct fy_node *fyn_root);
bool reflection_meta_compare(struct reflection_meta *rm_a, struct reflection_meta *rm_b);
struct reflection_meta *reflection_meta_copy(struct reflection_meta *rm_src);
struct fy_document *reflection_meta_get_document(struct reflection_meta *rm);
char *reflection_meta_get_document_str(struct reflection_meta *rm);
#define reflection_meta_get_document_str_alloca(_rm) \
	FY_ALLOCA_COPY_FREE(reflection_meta_get_document_str(_rm), FY_NT)

/* type/field */

struct reflection_type_ops {
	const char *name;

	/* emitter */
	int (*parse)(struct fy_parser *fyp, struct reflection_walker *rw, enum reflection_parse_flags flags);
	int (*emit)(struct fy_emitter *emit, struct reflection_walker *rw, enum reflection_emit_flags flags);

	/* destructor */
	void (*dtor)(struct reflection_walker *rw);

	int (*copy)(struct reflection_walker *rw_dst, struct reflection_walker *rw_src);
	int (*cmp)(struct reflection_walker *rw_a, struct reflection_walker *rw_b);
	int (*eq)(struct reflection_walker *rw_a, struct reflection_walker *rw_b);
};

struct reflection_field_data {
	struct reflection_type_system *rts;
	struct reflection_type_data *rtd_parent;
	int idx;
	struct reflection_type_data *rtd;
	const struct fy_field_info *fi;

	char *field_name;

	bool rtd_recursive;
	bool is_counter;	/* is a counter of another field */
	bool is_selector;	/* is a selector of another field */
	bool is_unsigned;	/* signed */
	bool is_signed;		/* unsigned */

	struct fy_document *yaml_annotation;			/* of the field */
	const char *yaml_annotation_str;			/* the annotation as a string */

	struct reflection_meta *meta;
};

static inline struct reflection_type_data *
rfd_rtd(const struct reflection_field_data *rfd)
{
	assert(rfd);
	assert(rfd->rtd);
	return rfd->rtd;
}

static inline bool
rfd_is_bitfield(const struct reflection_field_data *rfd)
{
	return rfd && rfd->fi && (rfd->fi->flags & FYFIF_BITFIELD);
}

static inline bool
rfd_is_anonymous_bitfield(const struct reflection_field_data *rfd)
{
	return rfd_is_bitfield(rfd) && rfd->fi->name && !rfd->fi->name[0];
}

enum reflection_type_data_flags {
	RTDF_IMPURE		= FY_BIT(0),	/* needs cleanup			*/

	RTDF_PURITY_MASK	= RTDF_IMPURE,

	RTDF_MUTATED		= FY_BIT(2),
	RTDF_SPECIALIZED	= FY_BIT(3),
	RTDF_SPECIALIZING	= FY_BIT(4),
	RTDF_ROOT		= FY_BIT(5),
};

struct reflection_type_data {
	int refs;
	int idx;
	struct reflection_type_system *rts;
	const struct fy_type_info *ti;
	const struct reflection_type_ops *ops;
	enum reflection_type_data_flags flags;			/* flags of the type */

	int flat_field_idx;					/* index of the flatten field */
	bool has_anonymous_field;				/* an anonymous field exists */
	int first_anonymous_field_idx;				/* the index of the first anonymous field */
	struct reflection_type_data *rtd_selector;		/* selector type for unions */
	int selector_field_idx;					/* the index of the selector in the parent struct*/
	int union_field_idx;					/* the index of the union in the parent struct */

	struct fy_document *yaml_annotation;			/* the yaml annotation */
	const char *yaml_annotation_str;			/* the annotation as a string */

	struct reflection_type_data *rtd_dep;			/* the dependent type */
	bool rtd_dep_recursive;
	int fields_count;
	struct reflection_field_data **fields;

	struct reflection_meta *meta;
};

static inline enum fy_type_kind
rtd_kind(const struct reflection_type_data *rtd)
{
	return rtd && rtd->ti ? rtd->ti->kind : FYTK_INVALID;
}

static inline int
rtd_signess(const struct reflection_type_data *rtd)
{
	enum fy_type_kind kind;
	const struct fy_type_info *ti;
	const struct fy_field_info *fi;
	size_t i;

	kind = rtd_kind(rtd);
	if (kind == FYTK_ENUM) {
		/* The underlying integer type's signedness is implementation-defined
		 * (e.g. signed on MSVC/Windows, unsigned on Linux/GCC). For cross-platform
		 * consistency, determine signedness from the actual enum values: if none
		 * are negative, treat the enum as unsigned. */
		ti = rtd && rtd->ti ? rtd->ti : NULL;
		if (ti) {
			for (i = 0, fi = ti->fields; i < ti->count; i++, fi++) {
				if (!(fi->flags & FYFIF_ENUM_UNSIGNED) && fi->sval < 0)
					return -1;	/* has negative values: signed */
			}
			return 1;	/* all values non-negative: unsigned */
		}
		return fy_type_kind_signess(rtd_kind(rtd->rtd_dep));
	}
	return fy_type_kind_signess(kind);
}

static inline bool
rtd_is_signed(const struct reflection_type_data *rtd)
{
	return rtd_signess(rtd) < 0;
}

static inline bool
rtd_is_unsigned(const struct reflection_type_data *rtd)
{
	return rtd_signess(rtd) > 0;
}

static inline size_t
rtd_size(const struct reflection_type_data *rtd)
{
	return rtd && rtd->ti ? rtd->ti->size : 0;
}

static inline size_t
rtd_align(const struct reflection_type_data *rtd)
{
	return rtd && rtd->ti ? rtd->ti->align : 1;
}

static inline bool
reflection_type_data_has_dtor(struct reflection_type_data *rtd)
{
	return rtd && (rtd->flags & RTDF_PURITY_MASK) != 0 && rtd->ops && rtd->ops->dtor;
}

extern const struct reflection_type_ops reflection_ops_table[FYTK_COUNT];

static inline bool
reflection_type_data_has_default_ops(struct reflection_type_data *rtd)
{
	return rtd && rtd->ti && rtd->ops == &reflection_ops_table[rtd->ti->kind];
}

void reflection_type_data_destroy(struct reflection_type_data *rtd);

static inline struct reflection_type_data *
reflection_type_data_ref(struct reflection_type_data *rtd)
{
	if (!rtd)
		return NULL;
	rtd->refs++;
	assert(rtd->refs > 0);
	return rtd;
}

static inline void
reflection_type_data_unref(struct reflection_type_data *rtd)
{
	if (!rtd)
		return;

	assert(rtd->refs > 0);
	if (--rtd->refs == 0)
		reflection_type_data_destroy(rtd);
}

void *reflection_type_data_alloc(struct reflection_type_data *rtd);
void reflection_type_data_free(struct reflection_type_data *rtd, void *ptr);
void *reflection_type_data_alloc_array(struct reflection_type_data *rtd, size_t count);

bool reflection_type_data_equal(const struct reflection_type_data *rtd1,
				const struct reflection_type_data *rtd2);

int reflection_parse(struct fy_parser *fyp, struct reflection_type_data *rtd, void **datap,
		     enum reflection_parse_flags flags);
int reflection_parse_into(struct fy_parser *fyp, struct reflection_type_data *rtd, void *data,
			  enum reflection_parse_flags flags);

int reflection_emit(struct fy_emitter *emit, struct reflection_type_data *rtd, const void *data,
		    enum reflection_emit_flags flags);

void reflection_type_data_dump(struct reflection_type_data *rtd_root);

void *reflection_type_data_generate_value(struct reflection_type_data *rtd, struct fy_node *fyn);
void *reflection_type_data_generate_value_from_string(struct reflection_type_data *rtd, const char *str, size_t len, struct fy_document **fydp);
int reflection_type_data_generate_value_into(struct reflection_type_data *rtd, struct fy_node *fyn, void *data);
int reflection_type_data_put_value_into(struct reflection_type_data *rtd, void *data, struct fy_node *fyn_value, void *value);

/* type data stack */
struct reflection_type_data_stack {
	int top;
	int alloc;
	struct reflection_type_data **rtds;
};

static inline void
reflection_type_data_stack_setup(struct reflection_type_data_stack *rtds)
{
	if (!rtds)
		return;
	memset(rtds, 0, sizeof(*rtds));
}

static inline void
reflection_type_data_stack_cleanup(struct reflection_type_data_stack *rtds)
{
	if (!rtds)
		return;

	if (rtds->rtds)
		free(rtds->rtds);
}

static inline struct reflection_type_data *
reflection_type_data_stack_pop(struct reflection_type_data_stack *rtds)
{
	if (!rtds || rtds->top <= 0)
		return NULL;

	return rtds->rtds[--rtds->top];
}

static inline int
reflection_type_data_stack_push(struct reflection_type_data_stack *rtds, struct reflection_type_data *rtd)
{
	int new_alloc;
	struct reflection_type_data **new_rtds;

	if (!rtds || !rtd)
		return -1;

	/* add */
	if (rtds->top >= rtds->alloc) {
		new_alloc = rtds->alloc * 2;
		if (!new_alloc)
			new_alloc = 16;
		if (new_alloc < 0)	/* overflow */
			return -1;
		new_rtds = realloc(rtds->rtds, sizeof(*rtds->rtds) * new_alloc);
		if (!new_rtds)
			return -1;
		rtds->rtds = new_rtds;
		rtds->alloc = new_alloc;
	}
	rtds->rtds[rtds->top++] = rtd;
	return 0;
}

static inline struct reflection_type_data *
reflection_type_data_stack_find_by_type_info(struct reflection_type_data_stack *rtds,
					     const struct fy_type_info *ti)
{
	struct reflection_type_data *rtd;
	int i;

	if (!rtds || !ti)
		return NULL;

	for (i = 0; i < rtds->top; i++) {
		rtd = rtds->rtds[i];
		if (rtd->ti == ti)
			return rtd;
	}
	return NULL;
}

static inline struct reflection_type_data *
reflection_type_data_stack_get_top(struct reflection_type_data_stack *rtds)
{
	return (rtds && rtds->top > 0) ? rtds->rtds[rtds->top - 1] : NULL;
}

/* reflection field data stack */

struct reflection_field_data_stack {
	int top;
	int alloc;
	struct reflection_field_data **rfds;
};

static inline void
reflection_field_data_stack_setup(struct reflection_field_data_stack *rfds)
{
	if (!rfds)
		return;
	memset(rfds, 0, sizeof(*rfds));
}

static inline void
reflection_field_data_stack_cleanup(struct reflection_field_data_stack *rfds)
{
	if (!rfds)
		return;

	if (rfds->rfds)
		free(rfds->rfds);
}

static inline struct reflection_field_data *
reflection_field_data_stack_pop(struct reflection_field_data_stack *rfds)
{
	if (!rfds || rfds->top <= 0)
		return NULL;

	return rfds->rfds[--rfds->top];
}

static inline int
reflection_field_data_stack_push(struct reflection_field_data_stack *rfds, struct reflection_field_data *rfd)
{
	int new_alloc;
	struct reflection_field_data **new_rfds;

	if (!rfds || !rfd)
		return -1;

	/* add */
	if (rfds->top >= rfds->alloc) {
		new_alloc = rfds->alloc * 2;
		if (!new_alloc)
			new_alloc = 16;
		if (new_alloc < 0)	/* overflow */
			return -1;
		new_rfds = realloc(rfds->rfds, sizeof(*rfds->rfds) * new_alloc);
		if (!new_rfds)
			return -1;
		rfds->rfds = new_rfds;
		rfds->alloc = new_alloc;
	}
	rfds->rfds[rfds->top++] = rfd;
	return 0;
}

static inline struct reflection_field_data *
reflection_field_data_stack_get_top(struct reflection_field_data_stack *rfds)
{
	return (rfds && rfds->top > 0) ? rfds->rfds[rfds->top - 1] : NULL;
}

/* setup type ctx */

struct reflection_setup_type_ctx {
	struct reflection_reference *rr;
	struct reflection_type_data_stack rtds;
	struct reflection_field_data_stack rfds;
};

static inline void
reflection_setup_type_ctx_setup(struct reflection_setup_type_ctx *rstc, struct reflection_reference *rr)
{
	assert(rstc);
	assert(rr);

	memset(rstc, 0, sizeof(*rstc));
	rstc->rr = rr;
	reflection_type_data_stack_setup(&rstc->rtds);
	reflection_field_data_stack_setup(&rstc->rfds);
}

static inline void
reflection_setup_type_ctx_cleanup(struct reflection_setup_type_ctx *rstc)
{
	if (!rstc)
		return;

	reflection_field_data_stack_cleanup(&rstc->rfds);
	reflection_type_data_stack_cleanup(&rstc->rtds);
}

static inline struct reflection_type_data *
reflection_setup_type_ctx_top_type(struct reflection_setup_type_ctx *rstc)
{
	assert(rstc);
	return reflection_type_data_stack_get_top(&rstc->rtds);
}

static inline struct reflection_field_data *
reflection_setup_type_ctx_top_field(struct reflection_setup_type_ctx *rstc)
{
	assert(rstc);
	return reflection_field_data_stack_get_top(&rstc->rfds);
}


/* walker */

enum reflection_walker_flags {
	RWF_FIELD_IDX = FY_BIT(0),
	RWF_SEQ_IDX = FY_BIT(1),
	RWF_KEY = FY_BIT(2),
	RWF_VALUE = FY_BIT(3),
	RWF_TEXT_KEY = FY_BIT(4),
	RWF_SEQ_KEY = FY_BIT(5),
	RWF_MAP = FY_BIT(6),
	RWF_SEQ = FY_BIT(7),
	RWF_COMPLEX_KEY = FY_BIT(8),
	RWF_BITFIELD_DATA = FY_BIT(9),
	RWF_ROOT = FY_BIT(10),		/* root of reflection */
};

struct reflection_walker {
	struct reflection_walker *parent;
	struct reflection_type_data *rtd;
	uintmax_t idx;
	enum reflection_walker_flags flags;
	union {
		struct reflection_walker *rw_key;
		const char *text_key;
		uintmax_t seq_key;
	};
	void *data;
	union {
		size_t bytes;
		struct {
			unsigned short bit_width;
			unsigned char bit_offset;
		};
	} data_size;
	void *user;
};

static inline struct reflection_walker *
reflection_rw_value(struct reflection_walker *rw, struct reflection_type_data *rtd, void *data)
{
	assert(rw);
	assert(rtd);

	memset(rw, 0, sizeof(*rw));
	rw->rtd = rtd;
	rw->data_size.bytes = rtd->ti->size;
	rw->data = data;
	return rw;
}
#define reflection_rw_value_alloca(_rtd, _data) \
	reflection_rw_value(alloca(sizeof(struct reflection_walker)), (_rtd), (_data))

static inline struct reflection_walker *
reflection_rw_dep(struct reflection_walker *rw, struct reflection_walker *rw_parent)
{
	assert(rw);
	assert(rw_parent);
	assert(rw_parent->rtd->rtd_dep);

	memset(rw, 0, sizeof(*rw));
	rw->parent = rw_parent;
	rw->rtd = rw_parent->rtd->rtd_dep;
	rw->flags = rw_parent->flags;	/* we must inherit the flags */
	rw->data = rw_parent->data;
	rw->data_size = rw_parent->data_size;
	return rw;
}
#define reflection_rw_dep_alloca(_rw_parent) \
	reflection_rw_dep(alloca(sizeof(struct reflection_walker)), (_rw_parent));

int reflection_parse_rw(struct fy_parser *fyp, struct reflection_walker *rw, enum reflection_parse_flags flags);
int reflection_emit_rw(struct fy_emitter *emit, struct reflection_walker *rw, enum reflection_emit_flags flags);
void reflection_dtor_rw(struct reflection_walker *rw);
int reflection_copy_rw(struct reflection_walker *rw_dst, struct reflection_walker *rw_src);
int reflection_cmp_rw(struct reflection_walker *rw_a, struct reflection_walker *rw_b);
int reflection_eq_rw(struct reflection_walker *rw_a, struct reflection_walker *rw_b);

void reflection_free_rw(struct reflection_walker *rw);
void reflection_walker_print_path(struct reflection_walker *rw);

struct reflection_field_data *
reflection_get_field(struct reflection_walker *rw,
		     struct reflection_walker **rw_fieldp,
		     struct reflection_walker **rw_basep);

struct reflection_root_ctx {
	struct reflection_walker rw_root_local, rwr_local;
	struct fy_type_info ti_local;
	struct fy_field_info fi_local;
	struct reflection_type_data rtd_root_local;
	struct reflection_field_data rfd_local;
	struct reflection_field_data *field_tab[1];
};

struct reflection_walker *
reflection_root_ctx_setup(struct reflection_root_ctx *rctx,
			  struct reflection_type_data *rtd, void *data, struct reflection_meta *meta);

/* methods */

struct reflection_field_data *
reflection_field_data_create(struct reflection_type_data *rtd_parent, int idx);
void reflection_field_data_destroy(struct reflection_field_data *rfd);

struct reflection_type_data *reflection_type_data_ref(struct reflection_type_data *rtd);
void reflection_type_data_unref(struct reflection_type_data *rtd);

struct reflection_field_data *
reflection_field_data_create_internal(struct reflection_type_system *rts,
				      struct reflection_type_data *rtd_parent, int idx);

int reflection_type_data_simplify(struct reflection_type_data *rtd_root);

void reflection_reference_destroy(struct reflection_reference *rr);

#endif
