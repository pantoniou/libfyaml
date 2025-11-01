/*
 * fy-allocator-config-parse.h - Allocator configuration string parser
 *
 * Copyright (c) 2024 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_ALLOCATOR_CONFIG_PARSE_H
#define FY_ALLOCATOR_CONFIG_PARSE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libfyaml.h>

/**
 * fy_allocator_parse_config_string() - Parse allocator configuration string
 *
 * Parses allocator configuration strings of the form:
 *   "allocator_type[:param=value,param=value,...]"
 *
 * Examples:
 *   "linear:size=16M"
 *   "mremap:minimum_arena_size=4M,grow_ratio=1.5"
 *   "dedup:parent=linear,dedup_threshold=32"
 *   "auto:scenario=single_linear,estimated_max_size=100M"
 *   "malloc" (no parameters)
 *   "default" (no parameters)
 *
 * Supported parameters by allocator type:
 *
 * linear:
 *   size=<size>  - Buffer size (e.g., "16M", "1G")
 *
 * mremap:
 *   big_alloc_threshold=<size>      - Size threshold for big allocations
 *   empty_threshold=<size>          - Empty space threshold
 *   minimum_arena_size=<size>       - Minimum arena size
 *   grow_ratio=<float>              - Growth ratio (e.g., "1.5")
 *   balloon_ratio=<float>           - Balloon ratio (e.g., "2.0")
 *   arena_type=<type>               - Arena type: "default", "malloc", "mmap"
 *
 * dedup:
 *   parent=<type>                   - Parent allocator: "malloc", "linear", "mremap"
 *   bloom_filter_bits=<int>         - Bloom filter size in bits
 *   bucket_count_bits=<int>         - Bucket count bits
 *   dedup_threshold=<size>          - Minimum size for deduplication
 *   chain_length_grow_trigger=<int> - Chain length before grow
 *   estimated_content_size=<size>   - Estimated total content size
 *
 * auto:
 *   scenario=<type>                 - Scenario type:
 *                                     "per_tag_free", "per_tag_free_dedup",
 *                                     "per_obj_free", "per_obj_free_dedup",
 *                                     "single_linear", "single_linear_dedup"
 *   estimated_max_size=<size>       - Estimated maximum content size
 *
 * Size suffixes:
 *   Plain numbers: "1024", "512"
 *   Binary: "16K", "4M", "1G", "2T"
 *   Optional 'B' or 'i': "16KB", "4MB", "1Gi"
 *
 * @config_str: The configuration string to parse
 * @allocator_name: Pointer to store allocator name (caller must free)
 * @config: Pointer to store allocator-specific config structure (caller must free with fy_allocator_free_config)
 *
 * Returns:
 * 0 on success, -1 on error
 */
int fy_allocator_parse_config_string(const char *config_str,
				      char **allocator_name,
				      void **config);

/**
 * fy_allocator_free_config() - Free allocator configuration
 *
 * Frees configuration structure created by fy_allocator_parse_config_string()
 *
 * @allocator_name: The allocator name (used to determine config type)
 * @config: The configuration structure to free
 */
void fy_allocator_free_config(const char *allocator_name, void *config);

#endif
