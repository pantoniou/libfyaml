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

/* struct fy_arena_reloc and the arena relocation primitives live here */
#include "fy-allocator.h"

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
	fy_generic_builder_cleanup_fn cleanup;
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

/* dense identity map of out-of-place generic values. */
struct fy_generic_idset_entry {
	fy_generic_value key;
	uintmax_t payload;
};

/*
 * High bit of the payload: marks a key as a duplicate (occurs > once) for the
 * auto-anchor set. The remaining low bits hold the sequential anchor id once
 * assigned (0 = duplicate seen but no anchor emitted yet).
 */
#define FY_GENERIC_IDSET_DUP_BIT ((uintmax_t)1 << (sizeof(uintmax_t) * 8 - 1))

struct fy_generic_idset_bucket {
	struct fy_generic_idset_bucket *next;
	size_t count;
	struct fy_generic_idset_entry entries[];
};

struct fy_generic_idset {
	struct fy_generic_idset_bucket **heads;
	size_t nheads;
};

int fy_generic_idset_setup(struct fy_generic_idset *set);
/* like setup() but sizes the head array from an estimate of the keyed bytes */
int fy_generic_idset_setup_hint(struct fy_generic_idset *set, size_t est_bytes);
void fy_generic_idset_cleanup(struct fy_generic_idset *set);
void fy_generic_idset_reset(struct fy_generic_idset *set);
/* returns 1 if already present, 0 if newly inserted, -1 on allocation failure */
int fy_generic_idset_add(struct fy_generic_idset *set, fy_generic_value key);
bool fy_generic_idset_contains(struct fy_generic_idset *set, fy_generic_value key);
/* pointer to the stored id/payload of @key, or NULL if absent */
uintmax_t *fy_generic_idset_idp(struct fy_generic_idset *set, fy_generic_value key);

/*
 * Auto-anchor decision for @key against a set previously filled by
 * fy_generic_find_duplicates(). On the first appearance of a duplicate the next
 * sequential id is assigned (pre-incrementing *@counter); *@idp receives it.
 * Returns 0 if @key is not a duplicate (emit normally), 1 on the first
 * appearance (emit with anchor a<id>), 2 on a later appearance (emit alias
 * *a<id>), -1 on error.
 */
int fy_generic_idset_anchor(struct fy_generic_idset *set, fy_generic_value key,
			    uintmax_t *counter, uintmax_t *idp);

/*
 * Mark, within a single @set, every out-of-place value that occurs more than
 * once under @root with FY_GENERIC_IDSET_DUP_BIT (the values that warrant an
 * anchor/alias). The set is reset first. Returns 0 on success, -1 on
 * allocation failure.
 */
int fy_generic_find_duplicates(struct fy_generic_idset *set, fy_generic root);

/* sequential anchor name ("a1", "a2", ...) for @id, written into @buf */
const char *fy_generic_auto_anchor_name(char *buf, size_t bufsz, uintmax_t id);

struct fy_generic_iterator {
	struct fy_generic_iterator_cfg cfg;
	enum fy_generic_iterator_state state;
	struct fy_document_state *fyds;
	enum fy_generic_schema schema;
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

	/* auto-anchor (FYGICF_AUTO_ANCHOR) */
	bool auto_anchor;
	struct fy_generic_idset anchor_set;	/* dup-bit + assigned anchor id */
	uintmax_t anchor_counter;		/* last assigned anchor id */
	char anchor_name_buf[24];
};

int fy_generic_iterator_setup(struct fy_generic_iterator *fygi, const struct fy_generic_iterator_cfg *cfg);
void fy_generic_iterator_cleanup(struct fy_generic_iterator *fygi);
struct fy_generic_iterator *fy_generic_iterator_create(void);
void fy_generic_iterator_destroy(struct fy_generic_iterator *fygi);
struct fy_event *
fy_generic_iterator_generate_emit_next(struct fy_generic_iterator *fygi, struct fy_emitter *emit);

const void *fy_generic_builder_linearize(struct fy_generic_builder *gb, fy_generic *vp, size_t *sizep);
fy_generic fy_generic_arena_relocate(const struct fy_arena_reloc *arenas,
				     unsigned int num_arenas, void *start,
				     size_t size, fy_generic v);

struct fy_generic_iterator_body_result {
	fy_generic v;
	bool end;
	bool is_alias;		/* emit v as an alias to .anchor (auto-anchor) */
	const char *anchor;	/* generated anchor/alias name, or NULL */
};

bool
fy_generic_iterator_body_next_internal(struct fy_generic_iterator *fygi,
					struct fy_generic_iterator_body_result *res);

struct fy_token *
fy_document_state_generic_create_token(struct fy_document_state *fyds, fy_generic v,
				       enum fy_token_type type, enum fy_scalar_style style);
size_t
fy_document_state_format_tag(struct fy_document_state *fyds,
			     const char *tag, size_t tag_size,
			     char *buf, size_t maxsz);
char *
fy_document_state_format_tag_alloc(struct fy_document_state *fyds,
				   const char *tag, size_t tag_size,
				   size_t *formatted_tag_sizep);

#endif
