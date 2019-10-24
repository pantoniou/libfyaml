/*
 * fy-event.h - YAML parser private event definition
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_EVENT_H
#define FY_EVENT_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>

#include <libfyaml.h>

#include "fy-list.h"
#include "fy-typelist.h"

struct fy_parser;

/* private event type */
struct fy_eventp {
	struct list_head node;
	struct fy_parser *fyp;
	struct fy_event e;
};
FY_PARSE_TYPE_DECL(eventp);

void fy_eventp_release(struct fy_eventp *fyep);

#endif
