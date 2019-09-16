/*
 * fy-talloc.h - tracked alloc methods header
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_TALLOC_H
#define FY_TALLOC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <libfyaml.h>

#include "fy-list.h"
#include "fy-typelist.h"

/* generic memory allocator tracker */
/* will be freed when parser is released */

FY_TYPE_FWD_DECL_LIST(talloc);
struct fy_talloc {
	struct list_head node;
	struct fy_talloc_list *list;
	uint64_t data[0];
};
FY_TYPE_DECL_LIST(talloc);

void *fy_talloc(struct fy_talloc_list *fytal, size_t size);
int fy_tfree(struct fy_talloc_list *fytal, void *data);
void fy_tfree_all(struct fy_talloc_list *fytal);
int fy_talloc_move(struct fy_talloc_list *to_fytal,
		   struct fy_talloc_list *from_fytal,
		   void *data);
void *fy_same_talloc(void *ptr, size_t size);
int fy_same_tfree(void *ptr);

#endif
