/*
 * fy-cache.h - Internal transparent parse cache
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_CACHE_H
#define FY_CACHE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stddef.h>
#include <stdbool.h>

struct fy_parser;
struct fy_generic_iterator;
struct fy_generic_builder;
struct fy_generic_document_builder;
struct fy_parse_cache_entry;
struct fy_eventp;

#ifdef HAVE_GENERIC
#include <libfyaml/libfyaml-generic.h>

struct fy_parse_cache_entry {
	struct fy_generic_iterator *fygi;
	fy_generic vdir;
	void *arena;
	size_t arena_size;
	void *map;
	size_t map_size;
	bool mmaped;
};

bool fy_parse_cache_enabled(const struct fy_parser *fyp, const char *file);
void fy_parse_cache_build_cleanup(struct fy_parser *fyp);
void fy_parse_cache_build_process_event_private(struct fy_parser *fyp, struct fy_eventp *fyep);
void fy_parse_cache_entry_destroy(struct fy_parse_cache_entry *entry);

struct fy_parse_cache_entry *
fy_parse_cache_load_data_cfg(const struct fy_parse_cfg *cfg, const void *data, size_t size);
struct fy_parse_cache_entry *
fy_parse_cache_load_cfg(const struct fy_parse_cfg *cfg, const char *file);

void fy_parse_cache_store_data_generic(const struct fy_parse_cfg *cfg,
				       const void *data, size_t size,
				       const char *source_name,
				       struct fy_generic_builder *gb,
				       fy_generic vdir);

int fy_parse_cache_set_input_file(struct fy_parser *fyp, const char *file);

#else

static inline bool
fy_parse_cache_enabled(const struct fy_parser *fyp FY_UNUSED,
		       const char *file FY_UNUSED)
{
	return false;
}

static inline void
fy_parse_cache_build_cleanup(struct fy_parser *fyp FY_UNUSED)
{
	/* nothing */
}

static inline void
fy_parse_cache_build_process_event_private(struct fy_parser *fyp FY_UNUSED,
					   struct fy_eventp *fyep FY_UNUSED)
{
	/* nothing */
}

static inline void fy_parse_cache_entry_destroy(struct fy_parse_cache_entry *entry FY_UNUSED)
{
	/* nothing */
}

static inline int fy_parse_cache_set_input_file(struct fy_parser *fyp FY_UNUSED,
						const char *file FY_UNUSED)
{
	/* can never cache */
	return 0;
}

#endif

#define FY_PARSE_CACHE_MIN_FILE_SIZE	(1U << 20)

#endif
