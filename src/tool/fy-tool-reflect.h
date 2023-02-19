/*
 * fy-tool-reflect.h - reflect for tool
 *
 * Copyright (c) 2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_TOOL_REFLECT_H
#define FY_TOOL_REFLECT_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>

#include <libfyaml.h>
#include "fy-tool-util.h"

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
void reflection_prune_system(struct fy_reflection *rfl);
int reflection_type_filter(struct fy_reflection *rfl,
		const char *type_include, const char *type_exclude);
bool reflection_equal(struct fy_reflection *rfl_a, struct fy_reflection *rfl_b);

/* type system */

#define RTSCF_ANNOTATION_MODE_SHIFT	3
#define RTSCF_ANNOTATION_MODE_MASK	((1U << 2) - 1)
#define RTSCF_ANNOTATION_MODE(x)	(((unsigned int)(x) & RTSCF_ANNOTATION_MODE_MASK) << RTSCF_ANNOTATION_MODE_SHIFT)

enum reflection_type_system_config_flags {
	RTSCF_DUMP_REFLECTION 		= BIT(0),
	RTSCF_DUMP_TYPE_SYSTEM		= BIT(1),
	RTSCF_DEBUG			= BIT(2),
	RTSCF_ANNOTATION_MODE_YAML_1_2	= RTSCF_ANNOTATION_MODE(0),
	RTSCF_ANNOTATION_MODE_YAML_1_1	= RTSCF_ANNOTATION_MODE(1),
	RTSCF_ANNOTATION_MODE_JSON	= RTSCF_ANNOTATION_MODE(2),
	RTSCF_ANNOTATION_MODE_DEFAULT	= RTSCF_ANNOTATION_MODE_YAML_1_2,
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

	return !!(rm->explicit_map[i / 8] & BIT(i & 7));
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
		rm->explicit_map[i / 8] |= BIT(i & 7);
	else
		rm->explicit_map[i / 8] &= ~BIT(i & 7);
}

static inline bool
reflection_meta_get_bool_default(struct reflection_meta *rm, enum reflection_meta_value_id id)
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
	return !!(rm->bools[i / 8] & BIT(i & 7));
}

static inline const char *
reflection_meta_get_str_default(struct reflection_meta *rm, enum reflection_meta_value_id id)
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
reflection_meta_get_any_value_default(struct reflection_meta *rm, enum reflection_meta_value_id id)
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

	kind = rtd_kind(rtd);
	if (kind == FYTK_ENUM)
		return fy_type_kind_signess(rtd_kind(rtd->rtd_dep));
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

/* logging */
enum fy_tool_op {
	rfltop_rts,
	rfltop_rw,
	rfltop_parse,
	rfltop_emit,
};

struct fy_tool_log_ctx {
	enum fy_tool_op op;
	union {
		struct reflection_type_system *rts;
		struct fy_parser *fyp;
		struct fy_emitter *emit;
	};
	struct reflection_walker *rw;
	struct fy_event *fye;
	bool needs_event;
	enum fy_event_part event_part;
	struct fy_token *fyt;
	struct fy_diag_ctx diag_ctx;
	bool has_diag_ctx;
	bool save_error;
};

void
fy_tool_vlog(struct fy_tool_log_ctx *ctx, enum fy_error_type error_type,
		const char *fmt, va_list ap);

void
fy_tool_log(struct fy_tool_log_ctx *ctx, enum fy_error_type error_type,
		const char *fmt, ...)
	FY_FORMAT(printf, 3, 4);

#define reflection_type_system_log(_rts, _level, _save_error, _fmt, ...) \
	({ \
		const enum fy_error_type __level = (_level); \
		struct fy_tool_log_ctx __ctx = { \
			.op = rfltop_rts, \
			.rts = (_rts), \
			.diag_ctx = { \
				.level = __level, \
				.module = FYEM_REFLECTION, \
				.source_func = __func__, \
				.source_file = __FILE__, \
				.source_line = __LINE__, \
			}, \
			.has_diag_ctx = true, \
			.save_error = (_save_error), \
		}; \
		fy_tool_log(&__ctx, __level, (_fmt) , ## __VA_ARGS__); \
	})

#define reflection_walker_log(_rw, _level, _save_error, _fmt, ...) \
	({ \
		const enum fy_error_type __level = (_level); \
		struct fy_tool_log_ctx __ctx = { \
			.op = rfltop_rw, \
			.rw = (_rw), \
			.diag_ctx = { \
				.level = __level, \
				.module = FYEM_REFLECTION, \
				.source_func = __func__, \
				.source_file = __FILE__, \
				.source_line = __LINE__, \
			}, \
			.has_diag_ctx = true, \
			.save_error = (_save_error), \
		}; \
		fy_tool_log(&__ctx, __level, (_fmt) , ## __VA_ARGS__); \
	})

#define reflection_parse_log(_fyp, _level, _save_error, _fmt, ...) \
	({ \
		const enum fy_error_type __level = (_level); \
		struct fy_tool_log_ctx __ctx = { \
			.op = rfltop_parse, \
			.fyp = (_fyp), \
			.diag_ctx = { \
				.level = __level, \
				.module = FYEM_DECODE, \
				.source_func = __func__, \
				.source_file = __FILE__, \
				.source_line = __LINE__, \
			}, \
			.has_diag_ctx = true, \
			.save_error = (_save_error), \
		}; \
		fy_tool_log(&__ctx, __level, (_fmt) , ## __VA_ARGS__); \
	})

#define reflection_emit_log(_emit, _level, _save_error, _fmt, ...) \
	({ \
		const enum fy_error_type __level = (_level); \
		struct fy_tool_log_ctx __ctx = { \
			.op = rfltop_emit, \
			.emit = (_emit), \
			.diag_ctx = { \
				.level = __level, \
				.module = FYEM_ENCODE, \
				.source_func = __func__, \
				.source_file = __FILE__, \
				.source_line = __LINE__, \
			}, \
			.has_diag_ctx = true, \
			.save_error = (_save_error), \
		}; \
		fy_tool_log(&__ctx, __level, (_fmt) , ## __VA_ARGS__); \
	})

#define _RTS_ERROR_CHECK(_rts, _expr, _label, _save_error, _fmt, ...) \
	do { \
		if (!(_expr)) { \
			reflection_type_system_log((_rts), FYET_ERROR, (_save_error), (_fmt) , ## __VA_ARGS__); \
			goto _label ; \
		} \
	} while(0)

#define _RW_ERROR_CHECK(_rw, _expr, _label, _save_error, _fmt, ...) \
	do { \
		if (!(_expr)) { \
			reflection_walker_log((_rw), FYET_ERROR, (_save_error), (_fmt) , ## __VA_ARGS__); \
			goto _label ; \
		} \
	} while(0)

/* reflection parse check internal error */
#define _RP_ERROR_CHECK(_fyp, _expr, _label, _save_error, _fmt, ...) \
	do { \
		if (!(_expr)) { \
			reflection_parse_log((_fyp), FYET_ERROR, (_save_error), (_fmt) , ## __VA_ARGS__); \
			goto _label ; \
		} \
	} while(0)

/* reflection parse check internal error */
#define _RP_INPUT_CHECK(_fyp, _fye, _expr, _label, _output_err, _fmt, ...) \
	do { \
		if (!(_expr)) { \
			if ((_output_err)) { \
				struct fy_tool_log_ctx __ctx = { \
					.op = rfltop_parse, \
					.fyp = (_fyp), \
					.fye = (_fye), \
					.needs_event = true, \
					.event_part = FYEP_VALUE, \
					.save_error = true, \
				}; \
				fy_tool_log(&__ctx, FYET_ERROR, (_fmt) , ## __VA_ARGS__); \
			} \
			goto _label ; \
		} \
	} while(0)


/* reflection emit check internal error */
#define _RE_ERROR_CHECK(_emit, _expr, _label, _save_error, _fmt, ...) \
	do { \
		if (!(_expr)) { \
			struct fy_tool_log_ctx __ctx = { \
				.op = rfltop_emit, \
				.emit = (_emit), \
				.save_error = (_save_error), \
			}; \
			fy_tool_log(&__ctx, FYET_ERROR, (_fmt) , ## __VA_ARGS__); \
			goto _label ; \
		} \
	} while(0)

#define _RE_ASSERT(_emit, _expr) \
	_RE_ERROR_CHECK((_emit), (_expr), err_out, "%s: %s:%d: assert failed " #_expr "\n", \
			__func__, __FILE__, __LINE__)

/* reflection emit check internal error */
#define _RE_OUTPUT_CHECK(_emit, _expr, _label, _output_err, _fmt, ...) \
	do { \
		if (!(_expr)) { \
			reflection_emit_log((_emit), FYET_ERROR, true, (_fmt),  ## __VA_ARGS__); \
			goto _label ; \
		} \
	} while(0)

/* reflection type system error check, assume rts in scope and an err_out label */
#define RTS_ERROR_CHECK(_expr, _fmt, ...) \
	_RTS_ERROR_CHECK(rts, (_expr), err_out, true, (_fmt) , ## __VA_ARGS__)

/* reflection type system assert, assume rts in scope and an err_out label */
#define RTS_ASSERT(_expr) \
	_RTS_ERROR_CHECK(rts, (_expr), err_out, false, "%s: %s:%d: assert failed " #_expr "\n", \
			__func__, __FILE__, __LINE__)

/* reflection walker error check, assume rw in scope and an err_out label */
#define RW_ERROR_CHECK(_rw, _expr, _label, _fmt, ...) \
	_RW_ERROR_CHECK((_rw), (_expr), _label, true, (_fmt) , ## __VA_ARGS__)

/* reflection walker assert, assume rw in scope and an err_out label */
#define RW_ASSERT(_expr) \
	_RW_ERROR_CHECK(rw, (_expr), err_out, false, "%s: %s:%d: assert failed " #_expr "\n", \
			__func__, __FILE__, __LINE__)

/* reflection parse error check, assume fyp in scope and an err_out label */
#define RP_ERROR_CHECK(_fyp, _expr, _label, _fmt, ...) \
	_RP_ERROR_CHECK((_fyp), (_expr), _label, true, (_fmt) , ## __VA_ARGS__)

/* reflection parse assert, assume fyp and fye in scope and an err_out label */
#define RP_ASSERT(_expr) \
	_RP_ERROR_CHECK(fyp, (_expr), err_out, false, "%s: %s:%d: assert failed " #_expr "\n", \
			__func__, __FILE__, __LINE__)

/* assume fyp, fye, flags are defined as well as an err_input: label */
#define RP_INPUT_CHECK(_expr, _fmt, ...) \
	_RP_INPUT_CHECK(fyp, fye, _expr, err_input, \
			!(flags & RPF_SILENT_INVALID_INPUT), \
			_fmt , ## __VA_ARGS__)

/* reflection emit assert, assume emit and fye in scope and an err_out label */
#define RE_ASSERT(_expr) \
	_RE_ERROR_CHECK(emit, (_expr), err_out, false, "%s: %s:%d: assert failed " #_expr "\n", \
			__func__, __FILE__, __LINE__)

/* reflection emit error check, assume emit in scope and an err_out label */
#define RE_ERROR_CHECK(_emit, _expr, _label, _fmt, ...) \
	_RE_ERROR_CHECK((_emit), (_expr), _label, true, (_fmt) , ## __VA_ARGS__)

/* assume emit, rw, flags are defined as well as an err_input: label */
#define RE_OUTPUT_CHECK(_expr, _fmt, ...) \
	_RE_OUTPUT_CHECK(emit, _expr, err_output, \
			!(flags & REF_SILENT_INVALID_OUTPUT), \
			_fmt , ## __VA_ARGS__)

#define reflection_type_system_diag(_rts, _level, _fmt, ...) \
	({ \
		const enum fy_error_type __level = (_level); \
		struct fy_tool_log_ctx __ctx = { \
			.op = rfltop_rts, \
			.rts = (_rts), \
			.diag_ctx = { \
				.level = __level, \
				.module = FYEM_REFLECTION, \
				.source_func = __func__, \
				.source_file = __FILE__, \
				.source_line = __LINE__, \
			}, \
			.has_diag_ctx = true, \
			.save_error = true, \
		}; \
		fy_tool_log(&__ctx, __level, (_fmt) , ## __VA_ARGS__); \
	})

#ifndef NDEBUG

#define rts_debug(_rts, _fmt, ...) \
	reflection_type_system_diag((_rts), FYET_DEBUG, (_fmt) , ## __VA_ARGS__)
#else

#define rts_debug(_rts, _fmt, ...) \
	do { } while(0)

#endif

#define rts_info(_rts, _fmt, ...) \
	reflection_type_system_diag((_rts), FYET_INFO, (_fmt) , ## __VA_ARGS__)
#define rts_notice(_rts, _fmt, ...) \
	reflection_type_system_diag((_rts), FYET_NOTICE, (_fmt) , ## __VA_ARGS__)
#define rts_warning(_rts, _fmt, ...) \
	reflection_type_system_diag((_rts), FYET_WARNING, (_fmt) , ## __VA_ARGS__)
#define rts_error(_rts, _fmt, ...) \
	reflection_type_system_diag((_rts), FYET_ERROR, (_fmt) , ## __VA_ARGS__)

#endif
