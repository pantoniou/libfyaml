/*
 * fy-types.h - common types builder
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_TYPES_H
#define FY_TYPES_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <libfyaml.h>

#include "fy-list.h"
#include "fy-talloc.h"

struct fy_parser;

/* define type methods */
#define FY_TALLOC_TYPE_DEFINE(_type) \
\
struct fy_ ## _type *fy_ ## _type ## _alloc_simple_internal( \
		struct fy_ ## _type ## _list *_rl, \
		struct fy_talloc_list *_tl) \
{ \
	struct fy_ ## _type *_n; \
	\
	_n = fy_ ## _type ## _list_pop(_rl); \
	if (_n) \
		return _n; \
	_n = fy_talloc(_tl, sizeof(*_n)); \
	if (_n) \
		INIT_LIST_HEAD(&_n->node); \
	return _n; \
} \
\
void fy_ ## _type ## _recycle_internal(struct fy_ ## _type ## _list *_rl, \
		struct fy_ ## _type *_n) \
{ \
	if (_n) \
		fy_ ## _type ## _list_push(_rl, _n); \
} \
\
void fy_ ## _type ## _vacuum_internal(struct fy_ ## _type ## _list *_rl, \
		struct fy_talloc_list *_tl) \
{ \
	struct fy_ ## _type *_n; \
	\
	while ((_n = fy_ ## _type ## _list_pop(_rl)) != NULL) \
		fy_tfree(_tl, _n); \
} \
\
struct __useless_struct_to_allow_semicolon

/* declarations for alloc */

#define FY_TALLOC_TYPE_ALLOC(_type) \
struct fy_ ## _type *fy_ ## _type ## _alloc_simple_internal( \
		struct fy_ ## _type ## _list *_rl, \
		struct fy_talloc_list *_tl); \
void fy_ ## _type ## _recycle_internal(struct fy_ ## _type ## _list *_rl, \
		struct fy_ ## _type *_n); \
void fy_ ## _type ## _vacuum_internal(struct fy_ ## _type ## _list *_rl, \
		struct fy_talloc_list *_tl); \
struct __useless_struct_to_allow_semicolon

/* parser type methods */
#define FY_PARSE_TYPE_DECL_ALLOC(_type) \
\
struct fy_ ## _type *fy_parse_ ## _type ## _alloc(struct fy_parser *fyp); \
void fy_parse_ ## _type ## _vacuum(struct fy_parser *fyp); \
void fy_parse_ ## _type ## _recycle(struct fy_parser *fyp, struct fy_ ## _type *_n); \
void fy_parse_ ## _type ## _list_recycle_all(struct fy_parser *fyp, struct fy_ ## _type ## _list *_l); \
\
struct __useless_struct_to_allow_semicolon

#define FY_PARSE_TYPE_DECL(_type) \
FY_TYPE_FWD_DECL_LIST(_type); \
FY_TYPE_DECL_LIST(_type); \
FY_PARSE_TYPE_DECL_ALLOC(_type); \
struct __useless_struct_to_allow_semicolon

#define FY_PARSE_TYPE_DECL_AFTER_FWD(_type) \
FY_TYPE_DECL_LIST(_type); \
FY_PARSE_TYPE_DECL_ALLOC(_type); \
struct __useless_struct_to_allow_semicolon

/* define type methods */
#define FY_PARSE_TYPE_DEFINE(_type) \
\
struct fy_ ## _type *fy_parse_ ## _type ## _alloc_simple(struct fy_parser *fyp) \
{ \
	return fy_ ## _type ## _alloc_simple_internal(&fyp->recycled_ ## _type, \
			&fyp->tallocs); \
} \
\
void fy_parse_ ## _type ## _vacuum(struct fy_parser *fyp) \
{ \
	fy_ ## _type ## _vacuum_internal(&fyp->recycled_ ## _type, &fyp->tallocs); \
} \
\
void fy_parse_ ## _type ## _list_recycle_all(struct fy_parser *fyp, struct fy_ ## _type ## _list *_l) \
{ \
	struct fy_ ## _type *_n; \
	\
	while ((_n = fy_ ## _type ## _list_pop(_l)) != NULL) \
		fy_parse_ ## _type ## _recycle(fyp, _n); \
} \
\
void fy_parse_ ## _type ## _recycle_simple(struct fy_parser *fyp, struct fy_ ## _type *_n) \
{ \
	fy_ ## _type ## _recycle_internal(&fyp->recycled_ ## _type, _n); \
} \
\
struct __useless_struct_to_allow_semicolon

#define FY_PARSE_TYPE_DEFINE_ALLOC_SIMPLE(_type) \
struct fy_ ## _type *fy_parse_ ## _type ## _alloc(struct fy_parser *_fyp) \
{ \
	return fy_parse_ ## _type ## _alloc_simple(_fyp); \
} \
\
void fy_parse_ ## _type ## _recycle(struct fy_parser *_fyp, struct fy_ ## _type *_n) \
{ \
	if (_n) \
		fy_parse_ ## _type ## _recycle_simple(_fyp, _n); \
} \
\
struct __useless_struct_to_allow_semicolon

#define FY_PARSE_TYPE_DEFINE_SIMPLE(_type) \
\
FY_TALLOC_TYPE_DEFINE(_type); \
FY_PARSE_TYPE_DEFINE(_type); \
FY_PARSE_TYPE_DEFINE_ALLOC_SIMPLE(_type); \
\
struct __useless_struct_to_allow_semicolon

#endif
