/*
 * fy-allocator-linear.c - linear allocator
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

#include "fy-utils.h"
#include "fy-allocator-linear.h"

static int fy_linear_setup(struct fy_allocator *a, const void *data)
{
	struct fy_linear_allocator *la;
	const struct fy_linear_setup_data *d;
	void *buf, *alloc = NULL;

	if (!a || !data)
		return -1;

	d = data;
	if (!d->size)
		return -1;

	if (!d->buf) {
		alloc = malloc(d->size);
		if (!alloc)
			goto err_out;
		buf = alloc;
	} else
		buf = d->buf;

	la = container_of(a, struct fy_linear_allocator, a);
	memset(la, 0, sizeof(*la));
	la->a.name = "linear";
	la->a.ops = &fy_linear_allocator_ops;
	la->alloc = alloc;
	la->start = buf;
	la->next = buf;
	la->end = buf + d->size;

	return 0;
err_out:
	if (alloc)
		free(alloc);
	return -1;
}

static void fy_linear_cleanup(struct fy_allocator *a)
{
	struct fy_linear_allocator *la;

	if (!a)
		return;

	la = container_of(a, struct fy_linear_allocator, a);
	if (la->alloc) {
		free(la->alloc);
		la->alloc = NULL;
	}
}

struct fy_allocator *fy_linear_create(const void *setupdata)
{
	struct fy_linear_allocator *la;
	const struct fy_linear_setup_data *d;
	struct fy_linear_setup_data newsd;
	void *s, *e, *buf, *alloc = NULL;
	int rc;

	if (!setupdata)
		return NULL;

	d = setupdata;
	if (!d->size)
		return NULL;

	if (!d->buf) {
		alloc = malloc(d->size);
		if (!alloc)
			goto err_out;
		buf = alloc;
	} else
		buf = d->buf;

	s = buf;
	e = s + d->size;

	s = fy_ptr_align(s, alignof(struct fy_linear_allocator));
	if ((size_t)(e - s) < sizeof(*la))
		goto err_out;

	la = s;
	s += sizeof(*la);

	memset(&newsd, 0, sizeof(newsd));
	newsd.buf = s;
	newsd.size = (size_t)(e - s);

	rc = fy_linear_setup(&la->a, &newsd);
	if (rc)
		goto err_out;

	assert(!la->alloc);
	la->alloc = alloc;

	return &la->a;

err_out:
	if (alloc)
		free(alloc);

	return NULL;
}

void fy_linear_destroy(struct fy_allocator *a)
{
	struct fy_linear_allocator *la;
	void *alloc;

	if (!a)
		return;

	la = container_of(a, struct fy_linear_allocator, a);

	/* take out the allocation of create */
	alloc = la->alloc;
	la->alloc = NULL;

	fy_linear_cleanup(a);

	if (alloc)
		free(alloc);
}

void fy_linear_dump(struct fy_allocator *a)
{
	struct fy_linear_allocator *la;

	if (!a)
		return;

	la = container_of(a, struct fy_linear_allocator, a);

	fprintf(stderr, "linear: total %zu used %zu free %zu\n",
			(size_t)(la->end - la->start),
			(size_t)(la->next - la->start),
			(size_t)(la->end - la->next));
}

static void *fy_linear_alloc(struct fy_allocator *a, fy_alloc_tag tag, size_t size, size_t align)
{
	struct fy_linear_allocator *la;
	void *s;

	assert(a);

	la = container_of(a, struct fy_linear_allocator, a);
	if (la->next >= la->end)
		goto err_out;

	s = fy_ptr_align(la->next, align);
	if ((size_t)(la->end - s) < size)
		goto err_out;

	la->stats.allocations++;
	la->stats.allocated += size;

	memset(s, 0, size);

	la->next = s + size;

	return s;

err_out:
	return NULL;
}

static void fy_linear_free(struct fy_allocator *a, fy_alloc_tag tag, void *data)
{
	/* linear allocator does not free anything */
}

static int fy_linear_update_stats(struct fy_allocator *a, fy_alloc_tag tag, struct fy_allocator_stats *stats)
{
	struct fy_linear_allocator *la;
	unsigned int i;

	if (!a || !stats)
		return -1;

	la = container_of(a, struct fy_linear_allocator, a);

	/* and update with this ones */
	for (i = 0; i < ARRAY_SIZE(la->stats.counters); i++) {
		stats->counters[i] += la->stats.counters[i];
		la->stats.counters[i] = 0;
	}

	return 0;
}

static const void *fy_linear_store(struct fy_allocator *a, fy_alloc_tag tag, const void *data, size_t size, size_t align)
{
	void *p;

	if (!a)
		return NULL;

	p = fy_linear_alloc(a, tag, size, align);
	if (!p)
		goto err_out;

	memcpy(p, data, size);

	return p;

err_out:
	return NULL;
}

static const void *fy_linear_storev(struct fy_allocator *a, fy_alloc_tag tag, const struct fy_iovecw *iov, unsigned int iovcnt, size_t align)
{
	void *p, *start;
	unsigned int i;
	size_t size;

	if (!a)
		return NULL;

	size = 0;
	for (i = 0; i < iovcnt; i++)
		size += iov[i].size;

	start = fy_linear_alloc(a, tag, size, align);
	if (!start)
		goto err_out;

	for (i = 0, p = start; i < iovcnt; i++, p += size) {
		size = iov[i].size;
		memcpy(p, iov[i].data, size);
	}

	return start;

err_out:
	return NULL;
}

static void fy_linear_release(struct fy_allocator *a, fy_alloc_tag tag, const void *data, size_t size)
{
	/* nothing */
}

static fy_alloc_tag fy_linear_get_tag(struct fy_allocator *a, const void *tag_config)
{
	if (!a)
		return FY_ALLOC_TAG_ERROR;

	/* always return 0, we don't do tags for linear */
	return 0;
}

static void fy_linear_release_tag(struct fy_allocator *a, fy_alloc_tag tag)
{
	struct fy_linear_allocator *la;

	if (!a)
		return;

	/* we only give out 0 as a tag */
	assert(tag == 0);

	la = container_of(a, struct fy_linear_allocator, a);

	/* we just rewind */
	la->next = la->start;
}

static void fy_linear_trim_tag(struct fy_allocator *a, fy_alloc_tag tag)
{
	/* nothing */
}

static void fy_linear_reset_tag(struct fy_allocator *a, fy_alloc_tag tag)
{
	/* nothing */
}

static ssize_t fy_linear_get_areas(struct fy_allocator *a, fy_alloc_tag tag, struct fy_iovecw *iov, size_t maxiov)
{
	return -1;
}

static const void *fy_linear_get_single_area(struct fy_allocator *a, fy_alloc_tag tag, size_t *sizep, size_t *startp, size_t *allocp)
{
	return NULL;
}

const struct fy_allocator_ops fy_linear_allocator_ops = {
	.setup = fy_linear_setup,
	.cleanup = fy_linear_cleanup,
	.create = fy_linear_create,
	.destroy = fy_linear_destroy,
	.dump = fy_linear_dump,
	.alloc = fy_linear_alloc,
	.free = fy_linear_free,
	.update_stats = fy_linear_update_stats,
	.store = fy_linear_store,
	.storev = fy_linear_storev,
	.release = fy_linear_release,
	.get_tag = fy_linear_get_tag,
	.release_tag = fy_linear_release_tag,
	.trim_tag = fy_linear_trim_tag,
	.reset_tag = fy_linear_reset_tag,
	.get_areas = fy_linear_get_areas,
	.get_single_area = fy_linear_get_single_area,
};
