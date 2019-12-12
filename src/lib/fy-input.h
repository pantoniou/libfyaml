/*
 * fy-input.h - YAML input methods
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_INPUT_H
#define FY_INPUT_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include <libfyaml.h>

#include "fy-typelist.h"
#include "fy-ctype.h"

struct fy_atom;
struct fy_parser;

enum fy_input_type {
	fyit_file,
	fyit_stream,
	fyit_memory,
	fyit_alloc,
	fyit_callback,
};

struct fy_input_cfg {
	enum fy_input_type type;
	void *userdata;
	union {
		struct {
			const char *filename;
		} file;
		struct {
			const char *name;
			FILE *fp;
			size_t chunk;
		} stream;
		struct {
			const void *data;
			size_t size;
		} memory;
		struct {
			void *data;
			size_t size;
		} alloc;
		struct {
		} callback;
	};
};

enum fy_input_state {
	FYIS_NONE,
	FYIS_QUEUED,
	FYIS_PARSE_IN_PROGRESS,
	FYIS_PARSED,
};

FY_TYPE_FWD_DECL_LIST(input);
struct fy_input {
	struct list_head node;
	enum fy_input_state state;
	struct fy_input_cfg cfg;
	char *name;
	void *buffer;		/* when the file can't be mmaped */
	size_t allocated;
	size_t read;
	size_t chunk;
	FILE *fp;
	int refs;
	bool json_mode : 1;	/* the input is in JSON format */
	union {
		struct {
			int fd;			/* fd for file and stream */
			void *addr;		/* mmaped for files, allocated for streams */
			size_t length;
		} file;
		struct {
		} stream;
	};
};
FY_TYPE_DECL_LIST(input);

static inline bool fy_input_json_mode(const struct fy_input *fyi)
{
	return fyi && fyi->json_mode;
}

static inline bool fy_input_is_lb(const struct fy_input *fyi, int c)
{
	/* '\r', '\n' are always linebreaks */
	if (fy_is_json_lb(c))
		return true;
	if (fyi && fyi->json_mode)
		return false;
	return fy_is_yaml12_lb(c);
}

static inline bool fy_input_is_lbz(const struct fy_input *fyi, int c)
{
	return fy_input_is_lb(fyi, c) || fy_is_z(c);
}

static inline bool fy_input_is_blankz(const struct fy_input *fyi, int c)
{
	return fy_is_ws(c) || fy_input_is_lbz(fyi, c);
}

static inline bool fy_input_is_flow_ws(const struct fy_input *fyi, int c)
{
	/* space is always allowed */
	if (fy_is_space(c))
		return true;
	/* no other space for JSON */
	if (fyi && fyi->json_mode)
		return false;
	/* YAML allows tab for WS */
	return fy_is_tab(c);
}

static inline bool fy_input_is_flow_blankz(const struct fy_input *fyi, int c)
{
	return fy_input_is_flow_ws(fyi, c) || fy_input_is_lbz(fyi, c);
}

static inline const void *fy_input_start(const struct fy_input *fyi)
{
	const void *ptr = NULL;

	switch (fyi->cfg.type) {
	case fyit_file:
		if (fyi->file.addr) {
			ptr = fyi->file.addr;
			break;
		}
		/* fall-through */

	case fyit_stream:
		ptr = fyi->buffer;
		break;

	case fyit_memory:
		ptr = fyi->cfg.memory.data;
		break;

	case fyit_alloc:
		ptr = fyi->cfg.alloc.data;
		break;

	default:
		break;
	}
	assert(ptr);
	return ptr;
}

static inline size_t fy_input_size(const struct fy_input *fyi)
{
	size_t size;

	switch (fyi->cfg.type) {
	case fyit_file:
		if (fyi->file.addr) {
			size = fyi->file.length;
			break;
		}
		/* fall-through */

	case fyit_stream:
		size = fyi->read;
		break;

	case fyit_memory:
		size = fyi->cfg.memory.size;
		break;

	case fyit_alloc:
		size = fyi->cfg.alloc.size;
		break;

	default:
		size = 0;
		break;
	}
	return size;
}

struct fy_input *fy_input_alloc(void);
void fy_input_free(struct fy_input *fyi);
struct fy_input *fy_input_ref(struct fy_input *fyi);
void fy_input_unref(struct fy_input *fyi);

static inline enum fy_input_state fy_input_get_state(struct fy_input *fyi)
{
	return fyi->state;
}

const char *fy_input_get_filename(struct fy_input *fyi);

struct fy_input *fy_input_from_data(const char *data, size_t size,
				    struct fy_atom *handle, bool simple);
struct fy_input *fy_input_from_malloc_data(char *data, size_t size,
					   struct fy_atom *handle, bool simple);

void fy_input_close(struct fy_input *fyi);

int fy_parse_input_open(struct fy_parser *fyp, struct fy_input *fyi);
int fy_parse_input_done(struct fy_parser *fyp);
const void *fy_parse_input_try_pull(struct fy_parser *fyp, struct fy_input *fyi,
				    size_t pull, size_t *leftp);

int fy_parse_input_append(struct fy_parser *fyp, const struct fy_input_cfg *fyic);

#endif
