/*
 * fy-allocator-auto.h - the auto allocator
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_ALLOCATOR_AUTO_H
#define FY_ALLOCATOR_AUTO_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fy-allocator.h"

enum fy_auto_scenario_type {
	FYAST_PER_TAG_FREE,			/* only per tag freeing, no individual obj free		*/
	FYAST_PER_TAG_FREE_DEDUP,		/* per tag freeing, dedup obj store			*/
	FYAST_PER_OBJ_FREE,			/* object freeing allowed, tag freeing still works	*/
	FYAST_PER_OBJ_FREE_DEDUP,		/* per obj freeing, dedup obj store			*/
	FYAST_SINGLE_LINEAR_RANGE,		/* just a single linear range, no frees at all		*/
	FYAST_SINGLE_LINEAR_RANGE_DEDUP,	/* single linear range, with dedup			*/
};

struct fy_auto_setup_data {
	enum fy_auto_scenario_type scenario;
	size_t estimated_max_size;
};

struct fy_auto_allocator {
	struct fy_allocator a;
	struct fy_auto_setup_data d;
	struct fy_allocator *parent_allocator;
	struct fy_allocator *sub_parent_allocator;
};

extern const struct fy_allocator_ops fy_auto_allocator_ops;

#endif
