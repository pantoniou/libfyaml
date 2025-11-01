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

/**
 * fy_strtok_bracket_r() - Bracket-aware tokenizer
 *
 * Like strtok_r(), but respects brackets and doesn't split on delimiters
 * inside brackets. Handles nested brackets properly using depth tracking.
 *
 * Examples:
 *   "a=1,b=2,c=3" with delim="," -> tokens: "a=1", "b=2", "c=3"
 *   "a=1,b=[x,y],c=3" with delim="," -> tokens: "a=1", "b=[x,y]", "c=3"
 *   "a=[b=[c,d],e],f=2" with delim="," -> tokens: "a=[b=[c,d],e]", "f=2"
 *
 * @str: String to tokenize (first call) or NULL (subsequent calls)
 * @delim: Delimiter characters (typically ",")
 * @saveptr: Pointer to save position (like strtok_r)
 *
 * Returns:
 * Pointer to next token, or NULL when no more tokens
 */
char *fy_strtok_bracket_r(char *str, const char *delim, char **saveptr);

/**
 * fy_extract_bracketed_value() - Extract content from bracketed value
 *
 * If value starts with '[', extracts content between matching brackets
 * using proper depth tracking. If value doesn't start with '[', returns NULL.
 *
 * Examples:
 *   "[linear:size=16M]" -> "linear:size=16M" (newly allocated)
 *   "[a:[b,c]]" -> "a:[b,c]" (newly allocated)
 *   "plain" -> NULL (no brackets)
 *
 * @value: String to extract from (not modified)
 *
 * Returns:
 * Newly allocated string with extracted content, or NULL if no brackets
 * or on error. Caller must free the returned string.
 */
char *fy_extract_bracketed_value(const char *value);

#endif
