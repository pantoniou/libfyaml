/*
 * fy-allocator.h - allocators
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_ALLOCATOR_H
#define FY_ALLOCATOR_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include "fy-typelist.h"

struct fy_allocator;
struct fy_allocator_stats;

typedef int fy_alloc_tag;

#define FY_ALLOC_TAG_ERROR	((fy_alloc_tag)-1)
#define FY_ALLOC_TAG_NONE	FY_ALLOC_TAG_ERROR

struct fy_iovecw {
	const void *data;
	size_t size;
};

struct fy_allocator_stats;
struct fy_allocator_info;

struct fy_allocator_ops {
	int (*setup)(struct fy_allocator *a, const void *setupdata);
	void (*cleanup)(struct fy_allocator *a);
	struct fy_allocator *(*create)(const void *setupdata);
	void (*destroy)(struct fy_allocator *a);
	void (*dump)(struct fy_allocator *a);
	void *(*alloc)(struct fy_allocator *a, fy_alloc_tag tag, size_t size, size_t align);
	void (*free)(struct fy_allocator *a, fy_alloc_tag tag, void *data);
	int (*update_stats)(struct fy_allocator *a, fy_alloc_tag tag, struct fy_allocator_stats *stats);
	const void *(*store)(struct fy_allocator *a, fy_alloc_tag tag, const void *data, size_t size, size_t align);
	const void *(*storev)(struct fy_allocator *a, fy_alloc_tag tag, const struct fy_iovecw *iov, unsigned int iovcnt, size_t align);
	void (*release)(struct fy_allocator *a, fy_alloc_tag tag, const void *data, size_t size);
	fy_alloc_tag (*get_tag)(struct fy_allocator *a, const void *tag_config);
	void (*release_tag)(struct fy_allocator *a, fy_alloc_tag tag);
	void (*trim_tag)(struct fy_allocator *a, fy_alloc_tag tag);
	void (*reset_tag)(struct fy_allocator *a, fy_alloc_tag tag);
	struct fy_allocator_info *(*get_info)(struct fy_allocator *a, fy_alloc_tag tag);
};

struct fy_allocator_stats {
	union {
		struct {
			uint64_t allocations;
			uint64_t allocated;
			uint64_t frees;
			uint64_t freed;
			uint64_t stores;
			uint64_t stored;
			uint64_t releases;
			uint64_t released;
			uint64_t dup_stores;
			uint64_t dup_saved;
			uint64_t system_claimed;
			uint64_t system_free;
		};
		uint64_t counters[12];
	};
};

struct fy_allocator_arena_info {
	size_t free, used, total;
	void *data;
	size_t size;
};

struct fy_allocator_tag_info {
	fy_alloc_tag tag;
	size_t free, used, total;
	unsigned int num_arena_infos;
	struct fy_allocator_arena_info *arena_infos;
};

struct fy_allocator_info {
	size_t free, used, total;
	unsigned int num_tag_infos;
	struct fy_allocator_tag_info *tag_infos;
};

struct fy_allocator {
	const char *name;
	const struct fy_allocator_ops *ops;
};

struct fy_allocator *fy_allocator_create(const char *name, const void *setupdata);

static inline void fy_allocator_destroy(struct fy_allocator *a)
{
	if (!a)
		return;
	a->ops->destroy(a);
}

static inline void fy_allocator_dump(struct fy_allocator *a)
{
	if (!a)
		return;
	a->ops->dump(a);
}

static inline int fy_allocator_update_stats(struct fy_allocator *a, fy_alloc_tag tag, struct fy_allocator_stats *stats)
{
	if (!a)
		return -1;
	return a->ops->update_stats(a, tag, stats);
}

static inline void *fy_allocator_alloc(struct fy_allocator *a, fy_alloc_tag tag, size_t size, size_t align)
{
	if (!a)
		return NULL;
	return a->ops->alloc(a, tag, size, align);
}

static inline void fy_allocator_free(struct fy_allocator *a, fy_alloc_tag tag, void *ptr)
{
	if (!a || !ptr)
		return;
	a->ops->free(a, tag, ptr);
}

static inline const void *fy_allocator_store(struct fy_allocator *a, fy_alloc_tag tag, const void *data, size_t size, size_t align)
{
	if (!a)
		return NULL;
	return a->ops->store(a, tag, data, size, align);
}

static inline const void *fy_allocator_storev(struct fy_allocator *a, fy_alloc_tag tag, const struct fy_iovecw *iov, unsigned int iovcnt, size_t align)
{
	if (!a)
		return NULL;
	return a->ops->storev(a, tag, iov, iovcnt, align);
}

static inline void fy_allocator_release(struct fy_allocator *a, fy_alloc_tag tag, const void *ptr, size_t size)
{
	if (!a || !ptr)
		return;
	a->ops->release(a, tag, ptr, size);
}

static inline fy_alloc_tag fy_allocator_get_tag(struct fy_allocator *a, const void *tag_config)
{
	if (!a)
		return 0;
	return a->ops->get_tag(a, tag_config);
}

static inline void fy_allocator_release_tag(struct fy_allocator *a, fy_alloc_tag tag)
{
	if (!a)
		return;
	a->ops->release_tag(a, tag);
}

static inline void fy_allocator_trim_tag(struct fy_allocator *a, fy_alloc_tag tag)
{
	if (!a)
		return;
	a->ops->trim_tag(a, tag);
}

static inline void fy_allocator_reset_tag(struct fy_allocator *a, fy_alloc_tag tag)
{
	if (!a)
		return;
	a->ops->reset_tag(a, tag);
}

static inline struct fy_allocator_info *
fy_allocator_get_info(struct fy_allocator *a, fy_alloc_tag tag)
{
	if (!a)
		return NULL;
	return a->ops->get_info(a, tag);
}

FY_TYPE_FWD_DECL_LIST(registered_allocator_entry);
struct fy_registered_allocator_entry {
	struct list_head node;
	const char *name;
	const struct fy_allocator_ops *ops;
};
FY_TYPE_DECL_LIST(registered_allocator_entry);

int fy_allocator_register(const char *name, const struct fy_allocator_ops *ops);
int fy_allocator_unregister(const char *name);

const char *fy_allocator_iterate(const char **prevp);
bool fy_allocator_is_available(const char *allocator);
char *fy_allocator_get_names(void);

#endif
