/*
 * fy-type-meta.h - Reflection meta/annotation system (internal header)
 *
 * Copyright (c) 2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_TYPE_META_H
#define FY_TYPE_META_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-reflection.h>

#include "fy-reflection-private.h"

/* fwd decls */
struct reflection_type_system;
struct reflection_type_data;
struct reflection_field_data;
struct reflection_walker;
struct reflection_any_value;
struct reflection_meta;

struct reflection_any_value *
reflection_any_value_create(struct reflection_type_system *rts, struct fy_node *fyn);

void
reflection_any_value_destroy(struct reflection_any_value *rav);

const char *
reflection_any_value_get_str(struct reflection_any_value *rav);

struct reflection_any_value *
reflection_any_value_copy(struct reflection_any_value *rav_src);

void *
reflection_any_value_generate(struct reflection_any_value *rav,
			      struct reflection_type_data *rtd);

int
reflection_any_value_equal_rw(struct reflection_any_value *rav,
			      struct reflection_walker *rw);

struct reflection_meta *
reflection_meta_create(struct reflection_type_system *rts);

void
reflection_meta_destroy(struct reflection_meta *rm);

int
reflection_meta_fill(struct reflection_meta *rm, struct fy_node *fyn_root);

bool
reflection_meta_compare(struct reflection_meta *rm_a, struct reflection_meta *rm_b);

struct reflection_meta *
reflection_meta_copy(struct reflection_meta *rm_src);

struct fy_document *
reflection_meta_get_document(struct reflection_meta *rm);

char *
reflection_meta_get_document_str(struct reflection_meta *rm);

const char *
reflection_meta_value_str(struct reflection_meta *rm, enum reflection_meta_value_id id);

void
reflection_meta_dump(struct reflection_meta *rm);

int reflection_meta_set_any_value(struct reflection_meta *rm,
				  enum reflection_meta_value_id id,
				  struct reflection_any_value *rav, bool copy);

#endif /* FY_TYPE_META_H */
