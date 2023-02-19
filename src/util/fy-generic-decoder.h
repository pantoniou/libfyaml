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

struct fy_generic_decoder {
	struct fy_parser *fyp;
	struct fy_generic_builder *gb;
	bool verbose;
	bool document_ready;
	bool resolve;
	bool single_document;
	fy_generic vroot;
	fy_generic vds;
	struct fy_generic_anchor_list complete_anchors;
	struct fy_generic_anchor_list collecting_anchors;
	struct fy_generic_decoder_obj *gdo_root;
};

enum fy_generic_decoder_object_type {
	FYGDOT_SEQUENCE,
	FYGDOT_MAPPING,
	FYGDOT_ROOT,
};

static inline bool
fy_generic_decoder_object_type_is_valid(enum fy_generic_decoder_object_type type)
{
	return type >= FYGDOT_SEQUENCE && type <= FYGDOT_ROOT;
}

struct fy_generic_decoder_obj {
	enum fy_generic_decoder_object_type type;
	size_t alloc;
	size_t count;
	fy_generic *items;
	fy_generic v;
	fy_generic anchor;
	fy_generic tag;
	/* for the root */
	struct fy_document_state *fyds;
	fy_generic vds;
	bool supports_merge_key : 1;
	/* for mapping, special merge key */
	bool next_is_merge_args : 1;
};

struct fy_generic_decoder *
fy_generic_decoder_create(struct fy_parser *fyp, struct fy_generic_builder *gb, bool verbose);
void fy_generic_decoder_destroy(struct fy_generic_decoder *fygd);
fy_generic fy_generic_decoder_parse_document(struct fy_generic_decoder *fygd, fy_generic *vdsp);
fy_generic fy_generic_decoder_parse_all_documents(struct fy_generic_decoder *fygd);
void fy_generic_decoder_reset(struct fy_generic_decoder *fygd);

#endif
