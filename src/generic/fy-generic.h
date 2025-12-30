/*
 * fy-generic.h - space efficient generics
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_GENERIC_H
#define FY_GENERIC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdalign.h>
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <limits.h>
#include <stddef.h>
#include <float.h>

#include <libfyaml.h>

#include <libfyaml/fy-internal-generic.h>

struct fy_generic_builder {
	struct fy_generic_builder_cfg cfg;
	enum fy_generic_schema schema;
	enum fy_gb_flags flags;
	struct fy_allocator *allocator;
	int alloc_tag;
	void *linear;	/* when making it linear */
	FY_ATOMIC(uint64_t) allocation_failures;
	void *userdata;
};

#endif
