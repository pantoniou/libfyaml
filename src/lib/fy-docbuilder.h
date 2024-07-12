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

#endif
