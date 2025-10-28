/*
 * fy-list.h - Circular doubly-linked list implementation
 *
 * Copyright (c) 2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_LIST_H
#define FY_LIST_H

#include "fy-utils.h"

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

static inline void list_init(struct list_head *list)
{
	list->next = list->prev = list;
}

static inline void list_add(struct list_head *new_item, struct list_head *head)
{
	struct list_head *prev = head;
	struct list_head *next = head->next;

	next->prev = new_item;
	new_item->next = next;
	new_item->prev = prev;
	prev->next = new_item;
}

static inline void list_add_tail(struct list_head *new_item, struct list_head *head)
{
	struct list_head *prev = head->prev;
	struct list_head *next = head;

	next->prev = new_item;
	new_item->next = next;
	new_item->prev = prev;
	prev->next = new_item;
}

static inline void list_del(struct list_head *entry)
{
	struct list_head *prev = entry->prev;
	struct list_head *next = entry->next;

	next->prev = prev;
	prev->next = next;
}

static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}

static inline int list_is_singular(const struct list_head *head)
{
	return !list_empty(head) && head->next == head->prev;
}

static inline void list_splice(const struct list_head *list,
			       struct list_head *head)
{
	struct list_head *first = list->next;
	struct list_head *last = list->prev;
	struct list_head *prev = head;
	struct list_head *next = head->next;

	if (list_empty(list))
		return;

	first->prev = prev;
	prev->next = first;

	last->next = next;
	next->prev = last;
}

#define list_entry(ptr, type, member) \
	container_of(ptr, type, member)

#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)

#define list_last_entry(ptr, type, member) \
	list_entry((ptr)->prev, type, member)

#endif
