/*
 * fy-generic-decoder.h - generic decoder (yaml -> generic)
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_GENERIC_DECODER_H
#define FY_GENERIC_DECODER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libfyaml.h>

#include "fy-docstate.h"
#include "fy-allocator.h"
#include "fy-generic.h"

struct fy_generic_decoder_obj;

FY_TYPE_FWD_DECL_LIST(generic_anchor);
struct fy_generic_anchor {
	struct list_head node;
	fy_generic anchor;
	fy_generic content;
	int nest;
};
FY_TYPE_DECL_LIST(generic_anchor);

enum fy_generic_decoder_object_type {
	FYGDOT_INVALID = -1,
	FYGDOT_SEQUENCE,
	FYGDOT_MAPPING,
	FYGDOT_ROOT,
};

FY_TYPE_FWD_DECL_LIST(generic_decoder_obj);
struct fy_generic_decoder_obj {
	struct list_head node;
	enum fy_generic_decoder_object_type type;
	size_t alloc;
	size_t count;
	fy_generic *items;
	fy_generic v;
	fy_generic anchor;
	fy_generic tag;
	fy_generic marker;
	fy_generic comment;
	fy_generic marker_start;	/* for collections */
	fy_generic marker_end;
	/* for the root */
	struct fy_document_state *fyds;
	fy_generic vds;
	bool supports_merge_key : 1;
	/* for mapping, special merge key */
	bool next_is_merge_args : 1;
	fy_generic in_place_items[16];	// small amount of items in place
};
FY_TYPE_DECL_LIST(generic_decoder_obj);

enum fy_generic_decoder_parse_flags {
	FYGDPF_DISABLE_DIRECTORY	= FY_BIT(0),
	FYGDPF_MULTI_DOCUMENT		= FY_BIT(1),
	FYGDPF_TRACE			= FY_BIT(2),
	FYGDPF_KEEP_COMMENTS		= FY_BIT(3),
	FYGDPF_CREATE_MARKERS		= FY_BIT(4),
	FYGDPF_PYYAML_COMPAT		= FY_BIT(5),
};

struct fy_generic_decoder {
	struct fy_parser *fyp;
	struct fy_generic_decoder_obj_list recycled_gdos;
	enum fy_generic_schema original_schema;
	enum fy_parser_mode curr_parser_mode;
	struct fy_generic_builder *gb;
	enum fy_generic_decoder_parse_flags parse_flags;
	bool resolve;
	bool keep_markers;
	bool keep_comments;
	bool document_ready;
	bool single_document;
	fy_generic vroot;
	fy_generic vds;
	struct fy_generic_anchor_list complete_anchors;
	struct fy_generic_anchor_list collecting_anchors;
	struct fy_generic_decoder_obj *gdo_root;
};

static inline bool
fy_generic_decoder_object_type_is_valid(enum fy_generic_decoder_object_type type)
{
	return type >= FYGDOT_SEQUENCE && type <= FYGDOT_ROOT;
}

struct fy_generic_decoder *
fy_generic_decoder_create(struct fy_parser *fyp, struct fy_generic_builder *gb);
void fy_generic_decoder_destroy(struct fy_generic_decoder *fygd);

fy_generic fy_generic_decoder_parse(struct fy_generic_decoder *fygd,
				    enum fy_generic_decoder_parse_flags flags);

void fy_generic_decoder_reset(struct fy_generic_decoder *fygd);

#endif
