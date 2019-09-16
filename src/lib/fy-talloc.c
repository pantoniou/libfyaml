/*
 * fy-talloc.c - tracked alloc methods
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <stdlib.h>

#include "fy-talloc.h"

void *fy_talloc(struct fy_talloc_list *fytal, size_t size)
{
	struct fy_talloc *fyta;
	size_t asize;

	asize = offsetof(struct fy_talloc, data) + size;

	fyta = malloc(asize);
	if (!fyta)
		return NULL;

	fyta->list = fytal;
	fy_talloc_list_push(fytal, fyta);
	return &fyta->data[0];
}

int fy_tfree(struct fy_talloc_list *fytal, void *data)
{
	struct fy_talloc *fyta;

	if (!fytal)
		return -1;

	if (!data)
		return 0;

	fyta = container_of(data, struct fy_talloc, data);

	/* always verify that we're on the same list */
	assert(fyta->list == fytal);

	fy_talloc_list_del(fytal, fyta);

	fyta->list = NULL;

	free(fyta);

	return 0;
}

void fy_tfree_all(struct fy_talloc_list *fytal)
{
	struct fy_talloc *fyta;

	while ((fyta = fy_talloc_list_pop(fytal)) != NULL)
		free(fyta);
}

int fy_talloc_move(struct fy_talloc_list *to_fytal,
		   struct fy_talloc_list *from_fytal,
		   void *data)
{
	struct fy_talloc *fyta;

	if (!to_fytal || !from_fytal || !data)
		return 0;

	fyta = container_of(data, struct fy_talloc, data);

	/* if already on same list, we're OK */
	if (fyta->list == to_fytal)
		return 0;

	/* verify that the from list is correct */
	if (fyta->list != from_fytal)
		return -1;

	/* delete from one list and insert to the other */
	fy_talloc_list_del(from_fytal, fyta);
	fy_talloc_list_add(to_fytal, fyta);
	fyta->list = to_fytal;

	return 0;
}

void *fy_same_talloc(void *ptr, size_t size)
{
	struct fy_talloc *fyta;

	if (!ptr)
		return 0;

	fyta = container_of(ptr, struct fy_talloc, data);
	return fy_talloc(fyta->list, size);
}

int fy_same_tfree(void *ptr)
{
	struct fy_talloc *fyta;

	if (!ptr)
		return -1;

	fyta = container_of(ptr, struct fy_talloc, data);
	return fy_tfree(fyta->list, ptr);
}
