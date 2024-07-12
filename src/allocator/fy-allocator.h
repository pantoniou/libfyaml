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

#include <sys/uio.h>

#include "fy-typelist.h"

#include <libfyaml.h>

struct fy_allocator;
struct fy_allocator_stats;

struct fy_allocator_stats;
struct fy_allocator_info;

struct fy_allocator_ops {
	int (*setup)(struct fy_allocator *a, const void *cfg);
	void (*cleanup)(struct fy_allocator *a);
	struct fy_allocator *(*create)(const void *cfg);
	void (*destroy)(struct fy_allocator *a);
	void (*dump)(struct fy_allocator *a);
	void *(*alloc)(struct fy_allocator *a, int tag, size_t size, size_t align);
	void (*free)(struct fy_allocator *a, int tag, void *data);
	int (*update_stats)(struct fy_allocator *a, int tag, struct fy_allocator_stats *stats);
	const void *(*store)(struct fy_allocator *a, int tag, const void *data, size_t size, size_t align);
	const void *(*storev)(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align);
	void (*release)(struct fy_allocator *a, int tag, const void *data, size_t size);
	int (*get_tag)(struct fy_allocator *a);
	void (*release_tag)(struct fy_allocator *a, int tag);
	void (*trim_tag)(struct fy_allocator *a, int tag);
	void (*reset_tag)(struct fy_allocator *a, int tag);
	struct fy_allocator_info *(*get_info)(struct fy_allocator *a, int tag);
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
	int tag;
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

/* these private still */
int fy_allocator_update_stats(struct fy_allocator *a, int tag, struct fy_allocator_stats *stats);
struct fy_allocator_info *fy_allocator_get_info(struct fy_allocator *a, int tag);

FY_TYPE_FWD_DECL_LIST(registered_allocator_entry);
struct fy_registered_allocator_entry {
	struct list_head node;
	const char *name;
	const struct fy_allocator_ops *ops;
};
FY_TYPE_DECL_LIST(registered_allocator_entry);

/* these are private still */
char *fy_allocator_get_names(void);
int fy_allocator_register(const char *name, const struct fy_allocator_ops *ops);
int fy_allocator_unregister(const char *name);
void fy_allocator_release(struct fy_allocator *a, int tag, const void *ptr, size_t size);

#endif
