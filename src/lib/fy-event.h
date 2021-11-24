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

/* private event type */
FY_TYPE_FWD_DECL_LIST(eventp);
struct fy_eventp {
	struct list_head node;
	struct fy_event e;
};
FY_TYPE_DECL_LIST(eventp);
FY_PARSE_TYPE_DECL_ALLOC(eventp);

struct fy_eventp *fy_eventp_alloc(void);
void fy_eventp_free(struct fy_eventp *fyep);

/* called from internal emitter */
void fy_eventp_release(struct fy_eventp *fyep);

struct fy_eventp *fy_parse_eventp_alloc(struct fy_parser *fyp);
void fy_parse_eventp_recycle(struct fy_parser *fyp, struct fy_eventp *fyep);
void fy_parse_eventp_vacuum(struct fy_parser *fyp);

struct fy_eventp *fy_emit_eventp_alloc(struct fy_emitter *fye);
void fy_emit_eventp_recycle(struct fy_emitter *emit, struct fy_eventp *fyep);
void fy_emit_eventp_vacuum(struct fy_emitter *emit);

#endif
