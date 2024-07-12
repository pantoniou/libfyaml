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

struct fy_auto_allocator {
	struct fy_allocator a;
	struct fy_auto_allocator_cfg cfg;
	struct fy_allocator *parent_allocator;
	struct fy_allocator *sub_parent_allocator;
};

extern const struct fy_allocator_ops fy_auto_allocator_ops;

#endif
