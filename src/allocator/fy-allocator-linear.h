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

#include "fy-atomics.h"
#include "fy-allocator.h"

struct fy_linear_allocator {
	FY_ATOMIC(size_t) next FY_CACHE_ALIGNED;	// hot hot hot
	struct fy_allocator a;
	struct fy_linear_allocator_cfg cfg;
	void *alloc;
	void *start;
	bool need_zero;
	/* no need to keep anything else */
	FY_ATOMIC(uint64_t) allocations;
	FY_ATOMIC(uint64_t) allocated;
	FY_ATOMIC(uint64_t) stores;
	FY_ATOMIC(uint64_t) stored;
};

extern const struct fy_allocator_ops fy_linear_allocator_ops;

#endif
