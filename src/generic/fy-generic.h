/*
 * fy-generic.h - space efficient generics
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_GENERIC_H
#define FY_GENERIC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdalign.h>
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <limits.h>
#include <stddef.h>
#include <float.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-generic.h>

#include "fy-event.h"
#include "fy-token.h"

struct fy_generic_iterator;

struct fy_generic_builder {
	struct fy_generic_builder_cfg cfg;
	enum fy_generic_schema schema;
	enum fy_gb_flags flags;
	struct fy_allocator *allocator;
	int alloc_tag;
	void *linear;	/* when making it linear */
	FY_ATOMIC(uint64_t) allocation_failures;
	void *userdata;
};

enum fy_generic_iterator_state {
	FYGIS_WAITING_STREAM_START,
	FYGIS_WAITING_DOCUMENT_START,
	FYGIS_WAITING_BODY_START_OR_DOCUMENT_END,
	FYGIS_BODY,
	FYGIS_WAITING_DOCUMENT_END,
	FYGIS_WAITING_STREAM_END_OR_DOCUMENT_START,
	FYGIS_ENDED,
	FYGIS_ERROR,
};

struct fy_generic_iterator_body_state {
	fy_generic v;
	size_t idx;	/* either the sequence or collection index */
	bool processed_key : 1;	/* for mapping only */
};

struct fy_generic_iterator {
	struct fy_generic_iterator_cfg cfg;
	enum fy_generic_iterator_state state;
	struct fy_document_state *fyds;
	fy_generic vds;
	fy_generic iterate_root;
	size_t idx;	/* document index */
	size_t count;	/* document count */

	bool suppress_recycling_force : 1;
	bool suppress_recycling : 1;

	struct fy_eventp_list recycled_eventp;
	struct fy_token_list recycled_token;

	struct fy_eventp_list *recycled_eventp_list;	/* NULL when suppressing */
	struct fy_token_list *recycled_token_list;	/* NULL when suppressing */

	unsigned int stack_top;
	unsigned int stack_alloc;
	struct fy_generic_iterator_body_state *stack;
	struct fy_generic_iterator_body_state in_place[FYPCF_GUARANTEED_MINIMUM_DEPTH_LIMIT];

#define FYGIGF_GENERATED_SS	FY_BIT(0)
#define FYGIGF_GENERATED_DS	FY_BIT(1)
#define FYGIGF_GENERATED_DE	FY_BIT(2)
#define FYGIGF_GENERATED_SE	FY_BIT(3)
#define FYGIGF_GENERATED_BODY	FY_BIT(4)
#define FYGIGF_GENERATED_NULL	FY_BIT(5)
#define FYGIGF_ENDS_AFTER_BODY	FY_BIT(6)
#define FYGIGF_ENDS_AFTER_DOC	FY_BIT(7)
#define FYGIGF_WANTS_STREAM	FY_BIT(8)
#define FYGIGF_WANTS_DOC	FY_BIT(9)
	unsigned int generator_state;
};

int fy_generic_iterator_setup(struct fy_generic_iterator *fygi, const struct fy_generic_iterator_cfg *cfg);
void fy_generic_iterator_cleanup(struct fy_generic_iterator *fygi);
struct fy_generic_iterator *fy_generic_iterator_create(void);
void fy_generic_iterator_destroy(struct fy_generic_iterator *fygi);

struct fy_generic_iterator_body_result {
	fy_generic v;
	bool end;
};

bool
fy_generic_iterator_body_next_internal(struct fy_generic_iterator *fygi,
					struct fy_generic_iterator_body_result *res);

struct fy_token *
fy_document_state_generic_create_token(struct fy_document_state *fyds, fy_generic v, enum fy_token_type type);

#endif
