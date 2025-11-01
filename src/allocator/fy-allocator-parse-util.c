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
