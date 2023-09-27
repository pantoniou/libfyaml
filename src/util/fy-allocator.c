/*
 * fy-allocator.c - allocators
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdalign.h>
#include <time.h>
#include <inttypes.h>
#include <math.h>

#include <stdio.h>

#include <sys/mman.h>

#ifndef FY_NON_LOCKING_REGISTRY
#include <pthread.h>
#endif

/* for container_of */
#include "fy-list.h"
#include "fy-utils.h"

#include "fy-allocator.h"
#include "fy-allocator-linear.h"
#include "fy-allocator-malloc.h"
#include "fy-allocator-mremap.h"
#include "fy-allocator-dedup.h"
#include "fy-allocator-auto.h"

static struct fy_registered_allocator_entry_list allocator_registry_list;
static bool allocator_registry_initialized = false;
static bool allocator_registry_locked = false;

#ifndef FY_NON_LOCKING_REGISTRY
static pthread_mutex_t allocator_registry_mutex = PTHREAD_MUTEX_INITIALIZER;
static inline void allocator_registry_lock(void)
{
	int rc FY_UNUSED;

	rc = pthread_mutex_lock(&allocator_registry_mutex);
	assert(!rc);

	assert(!allocator_registry_locked);
	allocator_registry_locked = true;
}

static inline void allocator_registry_unlock(void)
{
	int rc FY_UNUSED;

	assert(allocator_registry_locked);
	allocator_registry_locked = false;

	rc = pthread_mutex_unlock(&allocator_registry_mutex);
	assert(!rc);
}
#else
static inline void allocator_registry_lock(void)
{
	/* nothing */
}
static inline void allocator_registry_unlock(void)
{
	/* nothing */
}
#endif

static inline void allocator_registry_init(void)
{
	if (allocator_registry_initialized)
		return;

	fy_registered_allocator_entry_list_init(&allocator_registry_list);

	allocator_registry_initialized = true;
}

static struct fy_registered_allocator_entry *
fy_registered_allocator_entry_create(const char *name, const struct fy_allocator_ops *ops)
{
	struct fy_registered_allocator_entry *ae;

	ae = malloc(sizeof(*ae));
	if (!ae)
		return NULL;
	memset(ae, 0, sizeof(*ae));
	ae->name = name;
	ae->ops = ops;

	return ae;
}

static void fy_registered_allocator_entry_destroy(struct fy_registered_allocator_entry *ae)
{
	if (!ae)
		return;
	free(ae);
}

static const struct {
	const char *name;
	const struct fy_allocator_ops *ops;
} builtin_allocators[] = {
	{
		.name = "linear",
		.ops = &fy_linear_allocator_ops,
	}, {
		.name = "malloc",
		.ops = &fy_malloc_allocator_ops,
	}, {
		.name = "mremap",
		.ops = &fy_mremap_allocator_ops,
	}, {
		.name = "dedup",
		.ops = &fy_dedup_allocator_ops,
	}, {
		.name = "auto",
		.ops = &fy_auto_allocator_ops,
	}
};

int fy_allocator_register(const char *name, const struct fy_allocator_ops *ops)
{
	struct fy_registered_allocator_entry *ae;
	unsigned int i;
	int ret;

	if (!name || !ops ||
		!ops->setup ||
		!ops->cleanup ||
		!ops->create ||
		!ops->destroy ||
		!ops->dump ||
		!ops->alloc ||
		!ops->free ||
		!ops->update_stats ||
		!ops->store ||
		!ops->storev ||
		!ops->release |
		!ops->get_tag ||
		!ops->release_tag ||
		!ops->trim_tag ||
		!ops->reset_tag ||
		!ops->get_info)
		return  -1;

	allocator_registry_lock();
	allocator_registry_init();

	ret = -1;

	/* must not clash with the builtins */
	for (i = 0; i < ARRAY_SIZE(builtin_allocators); i++) {
		if (!strcmp(builtin_allocators[i].name, name))
			goto out_unlock;
	}

	/* must not clash with the other entries */
	for (ae = fy_registered_allocator_entry_list_head(&allocator_registry_list); ae;
			ae = fy_registered_allocator_entry_next(&allocator_registry_list, ae)) {
		if (!strcmp(ae->name, name))
			goto out_unlock;
	}

	/* OK, create the entry */
	ae = fy_registered_allocator_entry_create(name, ops);
	if (!ae)
		goto out_unlock;

	/* and add it to the list */
	fy_registered_allocator_entry_list_add(&allocator_registry_list, ae);

	/* all clear */
	ret = 0;

out_unlock:
	allocator_registry_unlock();

	return ret;
}

int fy_allocator_unregister(const char *name)
{
	struct fy_registered_allocator_entry *ae;
	unsigned int i;
	int ret;

	ret = -1;

	allocator_registry_lock();
	allocator_registry_init();

	/* must not try to unregister a builtin */
	for (i = 0; i < ARRAY_SIZE(builtin_allocators); i++) {
		if (!strcmp(builtin_allocators[i].name, name))
			goto out_unlock;
	}

	/* find the entry now */
	for (ae = fy_registered_allocator_entry_list_head(&allocator_registry_list); ae;
			ae = fy_registered_allocator_entry_next(&allocator_registry_list, ae)) {
		if (!strcmp(ae->name, name))
			break;
	}
	if (!ae)
		goto out_unlock;

	fy_registered_allocator_entry_list_del(&allocator_registry_list, ae);

	/* and destroy it */
	fy_registered_allocator_entry_destroy(ae);

	ret = 0;

out_unlock:
	allocator_registry_unlock();

	return ret;
}

struct fy_allocator *fy_allocator_create(const char *name, const void *setupdata)
{
	struct fy_registered_allocator_entry *ae;
	const struct fy_allocator_ops *ops = NULL;
	unsigned int i;

	if (!name)
		name = builtin_allocators[0].name;

	allocator_registry_lock();
	allocator_registry_init();

	/* try the builtins first */
	for (i = 0; i < ARRAY_SIZE(builtin_allocators); i++) {
		if (!strcmp(builtin_allocators[i].name, name)) {
			ops = builtin_allocators[i].ops;
			break;
		}
	}

	/* if not found there, try the registry */
	if (!ops) {
		for (ae = fy_registered_allocator_entry_list_head(&allocator_registry_list); ae;
				ae = fy_registered_allocator_entry_next(&allocator_registry_list, ae)) {
			if (!strcmp(ae->name, name)) {
				ops = ae->ops;
				break;
			}
		}
	}
	allocator_registry_unlock();
	if (!ops)
		return NULL;

	return ops->create(setupdata);
}

void fy_allocator_registry_cleanup_internal(bool show_leftovers)
{
	struct fy_registered_allocator_entry *ae;

	if (!allocator_registry_initialized)
		return;

	allocator_registry_lock();
	while ((ae = fy_registered_allocator_entry_list_pop(&allocator_registry_list)) != NULL) {
		if (show_leftovers)
			fprintf(stderr, "%s: destroying %s\n", __func__, ae->name);
		fy_registered_allocator_entry_destroy(ae);
	}
	allocator_registry_unlock();
}

void fy_allocator_registry_cleanup(void)
{
	fy_allocator_registry_cleanup_internal(false);
}

#ifdef FY_HAS_CONSTRUCTOR
static FY_CONSTRUCTOR void fy_allocator_registry_constructor(void)
{
	allocator_registry_init();
}
#endif

#ifdef FY_HAS_DESTRUCTOR
static FY_DESTRUCTOR void fy_allocator_registry_destructor(void)
{
	bool show_leftovers = false;

#ifdef FY_DESTRUCTOR_SHOW_LEFTOVERS
	show_leftovers = true;
#endif

	/* make sure the registry is not locked because we will hang */
	if (allocator_registry_locked) {
		if (show_leftovers)
			fprintf(stderr, "%s: refusing to work on locked registry\n", __func__);
		return;
	}

	fy_allocator_registry_cleanup_internal(show_leftovers);
}
#endif

static const char **create_allocator_array_names(void)
{
	unsigned int i, j, count;
	struct fy_registered_allocator_entry *ae;
	const char **names = NULL;

	allocator_registry_lock();

	/* two passes */
	for (j = 0; j < 2; j++) {

		count = 0;

		/* try the builtins first */
		for (i = 0; i < ARRAY_SIZE(builtin_allocators); i++) {
			if (j)
				names[count] = builtin_allocators[i].name;
			count++;

		}
		for (ae = fy_registered_allocator_entry_list_head(&allocator_registry_list); ae;
				ae = fy_registered_allocator_entry_next(&allocator_registry_list, ae)) {
			if (j)
				names[count] = ae->name;
			count++;
		}

		if (!j) {
			names = malloc(sizeof(*names) * (count + 1));
			if (!names)
				return NULL;
			memset(names, 0, sizeof(*names) * (count + 1));
		} else
			names[count++] = NULL;

	}

	allocator_registry_unlock();

	return names;
}

const char *fy_allocator_iterate(const char **prevp)
{
	unsigned int i;
	const char **names;

	if (!prevp)
		return NULL;

	/* yeah, it's not fast, but should be negligible */
	names = create_allocator_array_names();
	if (!names)
		return NULL;

	i = 0;
	if (*prevp) {
		while (names[i] && strcmp(*prevp, names[i]))
			i++;
		if (names[i])
			i++;
	}

	*prevp = names[i];

	free(names);

	return *prevp;
}

bool fy_allocator_is_available(const char *allocator)
{
	const char *name, *prev;

	prev = NULL;
	while ((name = fy_allocator_iterate(&prev)) != NULL) {
		if (!strcmp(allocator, name))
			return true;
	}
	return false;
}

char *fy_allocator_get_names(void)
{
	const char *name, *prev;
	size_t size, len;
	char *names, *s;

	size = 0;
	prev = NULL;
	while ((name = fy_allocator_iterate(&prev)) != NULL)
		size += strlen(name) + 1;

	names = malloc(size + 1);
	if (!names)
		return NULL;

	s = names;
	prev = NULL;
	while ((name = fy_allocator_iterate(&prev)) != NULL) {
		len = strlen(name);
		assert((size_t)(s + len + 1 - names) <= size);
		memcpy(s, name, len);
		s += len;
		*s++ = ' ';
	}
	if (s > names && s[-1] == ' ')
		s[-1] = '\0';
	return names;
}

