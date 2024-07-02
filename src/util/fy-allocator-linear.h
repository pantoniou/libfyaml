/*
 * fy-allocator-linear.h - the linear allocator
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_ALLOCATOR_LINEAR_H
#define FY_ALLOCATOR_LINEAR_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fy-allocator.h"

struct fy_linear_setup_data {
	void *buf;
	size_t size;
};

struct fy_linear_allocator {
	struct fy_allocator a;
	struct fy_allocator_stats stats;
	void *alloc;
	void *start;
	void *next;
	void *end;
};

extern const struct fy_allocator_ops fy_linear_allocator_ops;

#endif
