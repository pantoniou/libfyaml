/*
 * fy-doc.h - YAML document internal header file
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_DOC_H
#define FY_DOC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

#include <libfyaml.h>

#include "fy-ctype.h"
#include "fy-utf8.h"
#include "fy-list.h"
#include "fy-typelist.h"
#include "fy-talloc.h"
#include "fy-types.h"
#include "fy-diag.h"

struct fy_eventp;

FY_TYPE_FWD_DECL_LIST(document);

struct fy_document_state {
	struct list_head node;
	int refs;
	struct fy_version version;
	bool version_explicit : 1;
	bool tags_explicit : 1;
	bool start_implicit : 1;
	bool end_implicit : 1;
	struct fy_mark start_mark;
	struct fy_mark end_mark;
	struct fy_token *fyt_vd;		/* version directive */
	struct fy_token_list fyt_td;		/* tag directives */
};
FY_PARSE_TYPE_DECL(document_state);

struct fy_node;

struct fy_node_pair {
	struct list_head node;
	struct fy_node *key;
	struct fy_node *value;
	struct fy_document *fyd;
	struct fy_node *parent;
};
FY_TYPE_FWD_DECL_LIST(node_pair);
FY_TYPE_DECL_LIST(node_pair);

FY_TYPE_FWD_DECL_LIST(node);
struct fy_node {
	struct list_head node;
	enum fy_node_type type;
	struct fy_token *tag;
	enum fy_node_style style;
	struct fy_node *parent;
	struct fy_document *fyd;
	unsigned int marks;
	union {
		struct fy_token *scalar;
		struct fy_node_list sequence;
		struct fy_node_pair_list mapping;
	};
	union {
		struct fy_token *sequence_start;
		struct fy_token *mapping_start;
	};
	union {
		struct fy_token *sequence_end;
		struct fy_token *mapping_end;
	};
};
FY_TYPE_DECL_LIST(node);

struct fy_node *fy_node_alloc(struct fy_document *fyd, enum fy_node_type type);
struct fy_node_pair *fy_node_pair_alloc(struct fy_document *fyd);
void fy_node_pair_free(struct fy_node_pair *fynp);

struct fy_anchor {
	struct list_head node;
	struct fy_node *fyn;
	struct fy_token *anchor;
};
FY_TYPE_FWD_DECL_LIST(anchor);
FY_TYPE_DECL_LIST(anchor);

struct fy_document {
	struct list_head node;
	struct fy_talloc_list tallocs;
	struct fy_anchor_list anchors;
	struct fy_document_state *fyds;
	struct fy_parser *fyp;
	struct fy_node *root;
	bool owns_parser : 1;
	bool parse_error : 1;

	FILE *errfp;
	char *errbuf;
	size_t errsz;

	struct fy_document *parent;
	struct fy_document_list children;
};
/* only the list declaration/methods */
FY_TYPE_DECL_LIST(document);

struct fy_document_state *fy_document_state_alloc(void);
void fy_document_state_free(struct fy_document_state *fyds);
struct fy_document_state *fy_document_state_ref(struct fy_document_state *fyds);
void fy_document_state_unref(struct fy_document_state *fyds);

struct fy_token *fy_document_state_lookup_tag_directive(struct fy_document_state *fyds,
		const char *handle, size_t handle_size);
struct fy_document *fy_parse_document_create(struct fy_parser *fyp, struct fy_eventp *fyep);

void fy_document_dump_tag_directives(struct fy_document *fyd, const char *banner);

struct fy_node_mapping_sort_ctx {
	fy_node_mapping_sort_fn key_cmp;
	void *arg;
	struct fy_node_pair **fynpp;
	int count;
};

void fy_node_mapping_perform_sort(struct fy_node *fyn_map,
		fy_node_mapping_sort_fn key_cmp, void *arg,
		struct fy_node_pair **fynpp, int count);

struct fy_node_pair **fy_node_mapping_sort_array(struct fy_node *fyn_map,
		fy_node_mapping_sort_fn key_cmp,
		void *arg, int *countp);

void fy_node_mapping_sort_release_array(struct fy_node *fyn_map, struct fy_node_pair **fynpp);

int fy_parser_move_log_to_document(struct fy_parser *fyp, struct fy_document *fyd);
bool fy_document_has_error(struct fy_document *fyd);
const char *fy_document_get_log(struct fy_document *fyd, size_t *sizep);
void fy_document_clear_log(struct fy_document *fyd);

#endif
