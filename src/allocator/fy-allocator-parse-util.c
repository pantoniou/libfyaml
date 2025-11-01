/*
 * fy-allocator-parse-util.c - Allocator configuration parsing utilities
 *
 * Copyright (c) 2024 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#include "fy-allocator-parse-util.h"

int fy_parse_size_suffix(const char *str, size_t *sizep)
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

int fy_parse_float_value(const char *str, float *floatp)
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

int fy_parse_unsigned_value(const char *str, unsigned int *valp)
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

char *fy_extract_bracketed_value(const char *value)
{
	size_t len, i;
	int depth = 0;
	int start = -1, end = -1;

	if (!value || value[0] != '[')
		return NULL;

	len = strlen(value);

	/* Find matching closing bracket using depth tracking */
	for (i = 0; i < len; i++) {
		if (value[i] == '[') {
			if (depth == 0)
				start = i;
			depth++;
		} else if (value[i] == ']') {
			depth--;
			if (depth == 0) {
				end = i;
				break;  /* Found matching bracket */
			}
		}
	}

	/* Validate: start must be 0, end must be last char, depth must be 0 */
	if (start != 0 || end != (int)len - 1 || depth != 0) {
		fprintf(stderr, "Unmatched brackets in config: '%s'\n", value);
		return NULL;
	}

	/* Extract content between brackets */
	if (end - start <= 1)
		return strdup("");  /* Empty brackets [] */

	return strndup(value + start + 1, end - start - 1);
}

char *fy_strtok_bracket_r(char *str, const char *delim, char **saveptr)
{
	char *token_start, *p;
	int depth;

	/* First call: use provided string; subsequent calls: use saved position */
	if (str != NULL)
		*saveptr = str;
	else
		str = *saveptr;

	if (!str || !*str)
		return NULL;

	/* Skip leading delimiters */
	while (*str && strchr(delim, *str))
		str++;

	if (!*str) {
		*saveptr = str;
		return NULL;
	}

	/* Start of token */
	token_start = str;
	depth = 0;

	/* Scan until we find a delimiter at depth 0, or end of string */
	for (p = str; *p; p++) {
		if (*p == '[') {
			depth++;
		} else if (*p == ']') {
			depth--;
			if (depth < 0) {
				fprintf(stderr, "Unmatched closing bracket in config\n");
				return NULL;
			}
		} else if (depth == 0 && strchr(delim, *p)) {
			/* Found delimiter at bracket depth 0 */
			*p = '\0';
			*saveptr = p + 1;
			return token_start;
		}
	}

	/* Reached end of string */
	if (depth != 0) {
		fprintf(stderr, "Unmatched opening bracket in config\n");
		return NULL;
	}

	*saveptr = p;  /* Points to '\0' */
	return token_start;
}
