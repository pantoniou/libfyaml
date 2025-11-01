/*
 * fy-allocator-config-parse.c - Allocator configuration string parser
 *
 * Copyright (c) 2024 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#include <libfyaml.h>

#include "fy-allocator-config-parse.h"

/**
 * parse_size_suffix() - Parse a size string with optional K/M/G suffix
 *
 * Supports:
 *   - Plain numbers: "1024", "512"
 *   - Binary suffixes: "16K", "4M", "1G"
 *   - Decimal suffixes: "16KB", "4MB", "1GB"
 *
 * @str: The string to parse
 * @sizep: Pointer to store the parsed size
 *
 * Returns:
 * 0 on success, -1 on error
 */
static int parse_size_suffix(const char *str, size_t *sizep)
{
	char *endptr;
	unsigned long long val;
	size_t multiplier = 1;
	const char *p;

	if (!str || !sizep)
		return -1;

	/* Skip leading whitespace */
	while (isspace(*str))
		str++;

	if (!*str)
		return -1;

	errno = 0;
	val = strtoull(str, &endptr, 0);

	if (errno == ERANGE || (errno != 0 && val == 0))
		return -1;

	if (endptr == str)
		return -1;

	/* Check for suffix */
	p = endptr;
	while (isspace(*p))
		p++;

	if (*p) {
		switch (toupper(*p)) {
		case 'K':
			multiplier = 1024ULL;
			p++;
			break;
		case 'M':
			multiplier = 1024ULL * 1024ULL;
			p++;
			break;
		case 'G':
			multiplier = 1024ULL * 1024ULL * 1024ULL;
			p++;
			break;
		case 'T':
			multiplier = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
			p++;
			break;
		default:
			return -1;
		}

		/* Optional 'B' or 'i' after suffix */
		if (*p == 'B' || *p == 'b')
			p++;
		else if (*p == 'i')
			p++;

		/* Must be at end or whitespace */
		while (isspace(*p))
			p++;

		if (*p)
			return -1;
	}

	/* Check for overflow */
	if (val > SIZE_MAX / multiplier)
		return -1;

	*sizep = (size_t)(val * multiplier);
	return 0;
}

/**
 * parse_float_value() - Parse a floating point value
 *
 * @str: The string to parse
 * @floatp: Pointer to store the parsed float
 *
 * Returns:
 * 0 on success, -1 on error
 */
static int parse_float_value(const char *str, float *floatp)
{
	char *endptr;
	double val;

	if (!str || !floatp)
		return -1;

	/* Skip leading whitespace */
	while (isspace(*str))
		str++;

	if (!*str)
		return -1;

	errno = 0;
	val = strtod(str, &endptr);

	if (errno == ERANGE || (errno != 0 && val == 0))
		return -1;

	if (endptr == str)
		return -1;

	/* Skip trailing whitespace */
	while (isspace(*endptr))
		endptr++;

	if (*endptr)
		return -1;

	*floatp = (float)val;
	return 0;
}

/**
 * parse_unsigned_value() - Parse an unsigned integer value
 *
 * @str: The string to parse
 * @valp: Pointer to store the parsed value
 *
 * Returns:
 * 0 on success, -1 on error
 */
static int parse_unsigned_value(const char *str, unsigned int *valp)
{
	char *endptr;
	unsigned long val;

	if (!str || !valp)
		return -1;

	/* Skip leading whitespace */
	while (isspace(*str))
		str++;

	if (!*str)
		return -1;

	errno = 0;
	val = strtoul(str, &endptr, 0);

	if (errno == ERANGE || (errno != 0 && val == 0))
		return -1;

	if (endptr == str)
		return -1;

	/* Skip trailing whitespace */
	while (isspace(*endptr))
		endptr++;

	if (*endptr)
		return -1;

	if (val > UINT_MAX)
		return -1;

	*valp = (unsigned int)val;
	return 0;
}

/**
 * parse_linear_allocator_config() - Parse linear allocator configuration
 *
 * Format: "linear[:size=<size>]"
 *   size: Buffer size (e.g., "16M", "1G")
 *
 * @params: Parameter string (after the "linear:" prefix)
 * @cfgp: Pointer to store allocated config structure
 *
 * Returns:
 * 0 on success, -1 on error
 */
static int parse_linear_allocator_config(const char *params, void **cfgp)
{
	struct fy_linear_allocator_cfg *cfg;
	char *params_copy, *saveptr, *token, *key, *value;
	int rc = -1;

	cfg = calloc(1, sizeof(*cfg));
	if (!cfg)
		return -1;

	/* Default: NULL buffer, 0 size (allocator will allocate) */
	cfg->buf = NULL;
	cfg->size = 0;

	if (!params || !*params) {
		*cfgp = cfg;
		return 0;
	}

	params_copy = strdup(params);
	if (!params_copy)
		goto err_out;

	/* Parse key=value pairs separated by commas or colons */
	token = strtok_r(params_copy, ",:", &saveptr);
	while (token) {
		/* Split on '=' */
		key = token;
		value = strchr(key, '=');
		if (value) {
			*value++ = '\0';

			/* Trim whitespace */
			while (isspace(*key))
				key++;
			while (isspace(*value))
				value++;

			if (!strcmp(key, "size")) {
				if (parse_size_suffix(value, &cfg->size) < 0) {
					fprintf(stderr, "Invalid size value: %s\n", value);
					goto err_out;
				}
			} else {
				fprintf(stderr, "Unknown linear allocator parameter: %s\n", key);
				goto err_out;
			}
		} else {
			fprintf(stderr, "Invalid parameter format (expected key=value): %s\n", token);
			goto err_out;
		}

		token = strtok_r(NULL, ",:", &saveptr);
	}

	rc = 0;
	*cfgp = cfg;

err_out:
	free(params_copy);
	if (rc < 0)
		free(cfg);
	return rc;
}

/**
 * parse_mremap_allocator_config() - Parse mremap allocator configuration
 *
 * Format: "mremap[:param=value,...]"
 *   big_alloc_threshold: Size threshold for big allocations (e.g., "1M")
 *   empty_threshold: Empty space threshold (e.g., "512K")
 *   minimum_arena_size: Minimum arena size (e.g., "4M")
 *   grow_ratio: Growth ratio (float, e.g., "1.5")
 *   balloon_ratio: Balloon ratio (float, e.g., "2.0")
 *   arena_type: Arena type ("default", "malloc", "mmap")
 *
 * @params: Parameter string
 * @cfgp: Pointer to store allocated config structure
 *
 * Returns:
 * 0 on success, -1 on error
 */
static int parse_mremap_allocator_config(const char *params, void **cfgp)
{
	struct fy_mremap_allocator_cfg *cfg;
	char *params_copy, *saveptr, *token, *key, *value;
	int rc = -1;

	cfg = calloc(1, sizeof(*cfg));
	if (!cfg)
		return -1;

	/* Defaults (0 = use system defaults) */
	cfg->big_alloc_threshold = 0;
	cfg->empty_threshold = 0;
	cfg->minimum_arena_size = 0;
	cfg->grow_ratio = 0.0f;
	cfg->balloon_ratio = 0.0f;
	cfg->arena_type = FYMRAT_DEFAULT;

	if (!params || !*params) {
		*cfgp = cfg;
		return 0;
	}

	params_copy = strdup(params);
	if (!params_copy)
		goto err_out;

	token = strtok_r(params_copy, ",:", &saveptr);
	while (token) {
		key = token;
		value = strchr(key, '=');
		if (value) {
			*value++ = '\0';

			/* Trim whitespace */
			while (isspace(*key))
				key++;
			while (isspace(*value))
				value++;

			if (!strcmp(key, "big_alloc_threshold")) {
				if (parse_size_suffix(value, &cfg->big_alloc_threshold) < 0) {
					fprintf(stderr, "Invalid big_alloc_threshold: %s\n", value);
					goto err_out;
				}
			} else if (!strcmp(key, "empty_threshold")) {
				if (parse_size_suffix(value, &cfg->empty_threshold) < 0) {
					fprintf(stderr, "Invalid empty_threshold: %s\n", value);
					goto err_out;
				}
			} else if (!strcmp(key, "minimum_arena_size")) {
				if (parse_size_suffix(value, &cfg->minimum_arena_size) < 0) {
					fprintf(stderr, "Invalid minimum_arena_size: %s\n", value);
					goto err_out;
				}
			} else if (!strcmp(key, "grow_ratio")) {
				if (parse_float_value(value, &cfg->grow_ratio) < 0) {
					fprintf(stderr, "Invalid grow_ratio: %s\n", value);
					goto err_out;
				}
			} else if (!strcmp(key, "balloon_ratio")) {
				if (parse_float_value(value, &cfg->balloon_ratio) < 0) {
					fprintf(stderr, "Invalid balloon_ratio: %s\n", value);
					goto err_out;
				}
			} else if (!strcmp(key, "arena_type")) {
				if (!strcmp(value, "default")) {
					cfg->arena_type = FYMRAT_DEFAULT;
				} else if (!strcmp(value, "malloc")) {
					cfg->arena_type = FYMRAT_MALLOC;
				} else if (!strcmp(value, "mmap")) {
					cfg->arena_type = FYMRAT_MMAP;
				} else {
					fprintf(stderr, "Invalid arena_type: %s (use: default, malloc, mmap)\n", value);
					goto err_out;
				}
			} else {
				fprintf(stderr, "Unknown mremap allocator parameter: %s\n", key);
				goto err_out;
			}
		} else {
			fprintf(stderr, "Invalid parameter format: %s\n", token);
			goto err_out;
		}

		token = strtok_r(NULL, ",:", &saveptr);
	}

	rc = 0;
	*cfgp = cfg;

err_out:
	free(params_copy);
	if (rc < 0)
		free(cfg);
	return rc;
}

/**
 * parse_dedup_allocator_config() - Parse dedup allocator configuration
 *
 * Format: "dedup[:parent=<type>,param=value,...]"
 *   parent: Parent allocator type ("malloc", "linear", "mremap")
 *   bloom_filter_bits: Bloom filter size in bits (e.g., "16")
 *   bucket_count_bits: Bucket count bits (e.g., "10")
 *   dedup_threshold: Minimum size for dedup (e.g., "32")
 *   chain_length_grow_trigger: Chain length before grow (e.g., "8")
 *   estimated_content_size: Estimated total size (e.g., "10M")
 *
 * @params: Parameter string
 * @cfgp: Pointer to store allocated config structure
 *
 * Returns:
 * 0 on success, -1 on error
 */
static int parse_dedup_allocator_config(const char *params, void **cfgp)
{
	struct fy_dedup_allocator_cfg *cfg;
	char *params_copy, *saveptr, *token, *key, *value;
	const char *parent_type = "malloc";
	int rc = -1;

	cfg = calloc(1, sizeof(*cfg));
	if (!cfg)
		return -1;

	/* Defaults (0 = use system defaults) */
	cfg->parent_allocator = NULL;  /* Will be created below */
	cfg->bloom_filter_bits = 0;
	cfg->bucket_count_bits = 0;
	cfg->dedup_threshold = 0;
	cfg->chain_length_grow_trigger = 0;
	cfg->estimated_content_size = 0;

	if (!params || !*params) {
		/* Use default parent (malloc) */
		cfg->parent_allocator = fy_allocator_create(parent_type, NULL);
		if (!cfg->parent_allocator)
			goto err_out;
		*cfgp = cfg;
		return 0;
	}

	params_copy = strdup(params);
	if (!params_copy)
		goto err_out;

	token = strtok_r(params_copy, ",:", &saveptr);
	while (token) {
		key = token;
		value = strchr(key, '=');
		if (value) {
			*value++ = '\0';

			/* Trim whitespace */
			while (isspace(*key))
				key++;
			while (isspace(*value))
				value++;

			if (!strcmp(key, "parent")) {
				parent_type = strdup(value);
				if (!parent_type)
					goto err_out;
			} else if (!strcmp(key, "bloom_filter_bits")) {
				if (parse_unsigned_value(value, &cfg->bloom_filter_bits) < 0) {
					fprintf(stderr, "Invalid bloom_filter_bits: %s\n", value);
					goto err_out;
				}
			} else if (!strcmp(key, "bucket_count_bits")) {
				if (parse_unsigned_value(value, &cfg->bucket_count_bits) < 0) {
					fprintf(stderr, "Invalid bucket_count_bits: %s\n", value);
					goto err_out;
				}
			} else if (!strcmp(key, "dedup_threshold")) {
				if (parse_size_suffix(value, &cfg->dedup_threshold) < 0) {
					fprintf(stderr, "Invalid dedup_threshold: %s\n", value);
					goto err_out;
				}
			} else if (!strcmp(key, "chain_length_grow_trigger")) {
				if (parse_unsigned_value(value, &cfg->chain_length_grow_trigger) < 0) {
					fprintf(stderr, "Invalid chain_length_grow_trigger: %s\n", value);
					goto err_out;
				}
			} else if (!strcmp(key, "estimated_content_size")) {
				if (parse_size_suffix(value, &cfg->estimated_content_size) < 0) {
					fprintf(stderr, "Invalid estimated_content_size: %s\n", value);
					goto err_out;
				}
			} else {
				fprintf(stderr, "Unknown dedup allocator parameter: %s\n", key);
				goto err_out;
			}
		} else {
			fprintf(stderr, "Invalid parameter format: %s\n", token);
			goto err_out;
		}

		token = strtok_r(NULL, ",:", &saveptr);
	}

	/* Create parent allocator */
	cfg->parent_allocator = fy_allocator_create(parent_type, NULL);
	if (!cfg->parent_allocator) {
		fprintf(stderr, "Failed to create parent allocator: %s\n", parent_type);
		goto err_out;
	}

	rc = 0;
	*cfgp = cfg;

err_out:
	free(params_copy);
	if (rc < 0) {
		if (cfg->parent_allocator)
			fy_allocator_destroy(cfg->parent_allocator);
		free(cfg);
	}
	return rc;
}

/**
 * parse_auto_allocator_config() - Parse auto allocator configuration
 *
 * Format: "auto[:scenario=<type>,estimated_max_size=<size>]"
 *   scenario: Scenario type (see enum fy_auto_allocator_scenario_type)
 *     - "per_tag_free"
 *     - "per_tag_free_dedup"
 *     - "per_obj_free"
 *     - "per_obj_free_dedup"
 *     - "single_linear"
 *     - "single_linear_dedup"
 *   estimated_max_size: Estimated maximum content size (e.g., "100M")
 *
 * @params: Parameter string
 * @cfgp: Pointer to store allocated config structure
 *
 * Returns:
 * 0 on success, -1 on error
 */
static int parse_auto_allocator_config(const char *params, void **cfgp)
{
	struct fy_auto_allocator_cfg *cfg;
	char *params_copy, *saveptr, *token, *key, *value;
	int rc = -1;

	cfg = calloc(1, sizeof(*cfg));
	if (!cfg)
		return -1;

	/* Defaults */
	cfg->scenario = FYAST_PER_TAG_FREE;
	cfg->estimated_max_size = 0;

	if (!params || !*params) {
		*cfgp = cfg;
		return 0;
	}

	params_copy = strdup(params);
	if (!params_copy)
		goto err_out;

	token = strtok_r(params_copy, ",:", &saveptr);
	while (token) {
		key = token;
		value = strchr(key, '=');
		if (value) {
			*value++ = '\0';

			/* Trim whitespace */
			while (isspace(*key))
				key++;
			while (isspace(*value))
				value++;

			if (!strcmp(key, "scenario")) {
				if (!strcmp(value, "per_tag_free")) {
					cfg->scenario = FYAST_PER_TAG_FREE;
				} else if (!strcmp(value, "per_tag_free_dedup")) {
					cfg->scenario = FYAST_PER_TAG_FREE_DEDUP;
				} else if (!strcmp(value, "per_obj_free")) {
					cfg->scenario = FYAST_PER_OBJ_FREE;
				} else if (!strcmp(value, "per_obj_free_dedup")) {
					cfg->scenario = FYAST_PER_OBJ_FREE_DEDUP;
				} else if (!strcmp(value, "single_linear") || !strcmp(value, "single_linear_range")) {
					cfg->scenario = FYAST_SINGLE_LINEAR_RANGE;
				} else if (!strcmp(value, "single_linear_dedup") || !strcmp(value, "single_linear_range_dedup")) {
					cfg->scenario = FYAST_SINGLE_LINEAR_RANGE_DEDUP;
				} else {
					fprintf(stderr, "Invalid scenario: %s\n", value);
					fprintf(stderr, "Valid scenarios: per_tag_free, per_tag_free_dedup, per_obj_free, per_obj_free_dedup, single_linear, single_linear_dedup\n");
					goto err_out;
				}
			} else if (!strcmp(key, "estimated_max_size")) {
				if (parse_size_suffix(value, &cfg->estimated_max_size) < 0) {
					fprintf(stderr, "Invalid estimated_max_size: %s\n", value);
					goto err_out;
				}
			} else {
				fprintf(stderr, "Unknown auto allocator parameter: %s\n", key);
				goto err_out;
			}
		} else {
			fprintf(stderr, "Invalid parameter format: %s\n", token);
			goto err_out;
		}

		token = strtok_r(NULL, ",:", &saveptr);
	}

	rc = 0;
	*cfgp = cfg;

err_out:
	free(params_copy);
	if (rc < 0)
		free(cfg);
	return rc;
}

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
 * @config_str: The configuration string to parse
 * @allocator_name: Pointer to store allocator name (caller must free)
 * @config: Pointer to store allocator-specific config structure (caller must free)
 *
 * Returns:
 * 0 on success, -1 on error
 */
int fy_allocator_parse_config_string(const char *config_str,
				      char **allocator_name,
				      void **config)
{
	char *str_copy, *name, *params;
	int rc = -1;

	if (!config_str || !allocator_name || !config)
		return -1;

	*allocator_name = NULL;
	*config = NULL;

	str_copy = strdup(config_str);
	if (!str_copy)
		return -1;

	/* Split on first ':' */
	name = str_copy;
	params = strchr(name, ':');
	if (params)
		*params++ = '\0';

	/* Allocator name is required */
	if (!*name) {
		fprintf(stderr, "Empty allocator name\n");
		goto err_out;
	}

	*allocator_name = strdup(name);
	if (!*allocator_name)
		goto err_out;

	/* Parse allocator-specific configuration */
	if (!strcmp(name, "default") || !strcmp(name, "malloc")) {
		/* No configuration needed */
		*config = NULL;
		rc = 0;
	} else if (!strcmp(name, "linear")) {
		rc = parse_linear_allocator_config(params, config);
	} else if (!strcmp(name, "mremap")) {
		rc = parse_mremap_allocator_config(params, config);
	} else if (!strcmp(name, "dedup")) {
		rc = parse_dedup_allocator_config(params, config);
	} else if (!strcmp(name, "auto")) {
		rc = parse_auto_allocator_config(params, config);
	} else {
		fprintf(stderr, "Unknown allocator type: %s\n", name);
		fprintf(stderr, "Valid types: default, malloc, linear, mremap, dedup, auto\n");
		goto err_out;
	}

err_out:
	if (rc < 0) {
		free(*allocator_name);
		*allocator_name = NULL;
		*config = NULL;
	}
	free(str_copy);
	return rc;
}

/**
 * fy_allocator_free_config() - Free allocator configuration
 *
 * Frees configuration structure created by fy_allocator_parse_config_string()
 *
 * @allocator_name: The allocator name
 * @config: The configuration structure to free
 */
void fy_allocator_free_config(const char *allocator_name, void *config)
{
	struct fy_dedup_allocator_cfg *dedup_cfg;

	if (!config)
		return;

	/* Special handling for dedup - must destroy parent allocator */
	if (allocator_name && !strcmp(allocator_name, "dedup")) {
		dedup_cfg = config;
		if (dedup_cfg->parent_allocator)
			fy_allocator_destroy(dedup_cfg->parent_allocator);
	}

	free(config);
}
