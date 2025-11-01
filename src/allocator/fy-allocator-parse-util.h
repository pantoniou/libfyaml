/*
 * fy-allocator-parse-util.h - Allocator configuration parsing utilities
 *
 * Copyright (c) 2024 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_ALLOCATOR_PARSE_UTIL_H
#define FY_ALLOCATOR_PARSE_UTIL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stddef.h>

/**
 * fy_parse_size_suffix() - Parse a size string with optional K/M/G suffix
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
int fy_parse_size_suffix(const char *str, size_t *sizep);

/**
 * fy_parse_float_value() - Parse a floating point value
 *
 * @str: The string to parse
 * @floatp: Pointer to store the parsed float
 *
 * Returns:
 * 0 on success, -1 on error
 */
int fy_parse_float_value(const char *str, float *floatp);

/**
 * fy_parse_unsigned_value() - Parse an unsigned integer value
 *
 * @str: The string to parse
 * @valp: Pointer to store the parsed value
 *
 * Returns:
 * 0 on success, -1 on error
 */
int fy_parse_unsigned_value(const char *str, unsigned int *valp);

#endif
