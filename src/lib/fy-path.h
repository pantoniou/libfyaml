/*
 * fy-path.h - YAML parser private path definitions
 *
 * Copyright (c) 2021 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_PATH_H
#define FY_PATH_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>

#include <libfyaml.h>

#include "fy-list.h"
#include "fy-typelist.h"

#include "fy-emit-accum.h"

FY_TYPE_FWD_DECL_LIST(path_component);

enum fy_path_component_type {
	FYPCT_NONE,	/* not yet instantiated */
	FYPCT_MAP,	/* it's a mapping */
	FYPCT_SEQ,	/* it's a sequence */
};

/* fwd declaration */
struct fy_document;
struct fy_document_builder;

#define FY_PATH_MAPPING_SHORT_KEY	32

struct fy_path_mapping_state {
	bool got_key : 1;
	bool is_complex_key : 1;
	bool accumulating_complex_key : 1;
	char buf[FY_PATH_MAPPING_SHORT_KEY];	/* keep short keys without allocation */
	const char *text;
	size_t size;
	char *text_storage;

};

struct fy_path_sequence_state {
	int idx;
	char buf[22];	/* enough for 64 bit sequence numbers */
	size_t buflen;
};

struct fy_path_component {
	struct list_head node;
	struct fy_emit_accum_state start;
	enum fy_path_component_type type;
	union {
		struct fy_path_mapping_state map;
		struct fy_path_sequence_state seq;
	};
};
FY_TYPE_DECL_LIST(path_component);

struct fy_path {
	struct fy_path_component_list recycled_component;
	struct fy_path_component_list components;
	struct fy_emit_accum ea;
	char ea_inplace_buf[256];	/* the in place accumulator buffer before allocating */
	struct fy_document_builder *fydb;	/* for complex keys */
};

int fy_path_setup(struct fy_path *fypp);
void fy_path_cleanup(struct fy_path *fypp);
struct fy_path *fy_path_create(void);
void fy_path_destroy(struct fy_path *fypp);

void fy_path_reset(struct fy_path *fypp);

struct fy_path_component *fy_path_component_alloc(struct fy_path *fypp);
void fy_path_component_cleanup(struct fy_path_component *fypc);
void fy_path_component_free(struct fy_path_component *fypc);
void fy_path_component_destroy(struct fy_path_component *fypc);
void fy_path_component_recycle(struct fy_path *fypp, struct fy_path_component *fypc);
void fy_path_component_clear_state(struct fy_path_component *fypc);

struct fy_path_component *fy_path_component_create_mapping(struct fy_path *fypp);
struct fy_path_component *fy_path_component_create_sequence(struct fy_path *fypp);


struct fy_path_component *fy_path_component_create_mapping(struct fy_path *fypp);
struct fy_path_component *fy_path_component_create_sequence(struct fy_path *fypp);

void fy_path_component_set_tag(struct fy_path_component *fypc, struct fy_token *tag);
void fy_path_component_set_anchor(struct fy_path_component *fypc, struct fy_token *anchor);

bool fy_path_component_is_complete(struct fy_path_component *fypc);

int fy_path_component_build_text(struct fy_path_component *fypc, void *arg);
const char *fy_path_component_get_text(struct fy_path_component *fypc, size_t *lenp);
const char *fy_path_component_get_text0(struct fy_path_component *fypc);

int fy_path_rebuild(struct fy_path *fypp);

const char *fy_path_get_text(struct fy_path *fypp, size_t *lenp);
const char *fy_path_get_text0(struct fy_path *fypp);

bool fy_path_is_root(struct fy_path *fypp);
bool fy_path_in_sequence(struct fy_path *fypp);
bool fy_path_in_mapping(struct fy_path *fypp);

int fy_path_depth(struct fy_path *fypp);

struct fy_document *
fy_parse_load_document_with_builder(struct fy_parser *fyp);

#endif
