/*
 * fy-composer.h - YAML composer
 *
 * Copyright (c) 2021 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_COMPOSER_H
#define FY_COMPOSER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>

#include <libfyaml.h>

#include "fy-list.h"
#include "fy-typelist.h"

#include "fy-emit-accum.h"
#include "fy-path.h"

struct fy_composer;
struct fy_parser;
struct fy_token;
struct fy_document_state;
struct fy_diag;
struct fy_event;
struct fy_eventp;

struct fy_composer_ops {
	int (*stream_start)(struct fy_composer *fyc);
	int (*stream_end)(struct fy_composer *fyc);
	int (*document_start)(struct fy_composer *fyc, struct fy_document_state *fyds);
	int (*document_end)(struct fy_composer *fyc);
	int (*scalar)(struct fy_composer *fyc, struct fy_path *path, struct fy_token *tag, struct fy_token *fyt);
	int (*mapping_start)(struct fy_composer *fyc, struct fy_path *path, struct fy_token *tag, struct fy_token *fyt);
	int (*mapping_end)(struct fy_composer *fyc, struct fy_path *path, struct fy_token *fyt);
	int (*sequence_start)(struct fy_composer *fyc, struct fy_path *path, struct fy_token *tag, struct fy_token *fyt);
	int (*sequence_end)(struct fy_composer *fyc, struct fy_path *path, struct fy_token *fyt);
};

struct fy_composer_cfg {
	const struct fy_composer_ops *ops;
	void *user;
	struct fy_diag *diag;
};

struct fy_composer {
	struct fy_composer_cfg cfg;
	struct fy_path fypp;
};

struct fy_composer *fy_composer_create(struct fy_composer_cfg *cfg);
void fy_composer_destroy(struct fy_composer *fyc);
int fy_composer_process_event(struct fy_composer *fyc, struct fy_parser *fyp, struct fy_event *fye);

int fy_composer_process_event_private(struct fy_composer *fyc, struct fy_parser *fyp, struct fy_eventp *fyep);


#endif
