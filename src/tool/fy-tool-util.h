/*
 * fy-tool-utils.h - internal utilities header file
 *
 * Copyright (c) 2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_TOOL_UTILS_H
#define FY_TOOL_UTILS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdbool.h>
#ifdef _WIN32
#include "fy-win32.h"
#include <io.h>
#else
#include <unistd.h>
#include <termios.h>
#endif
#include <stdint.h>
#include <string.h>

#if defined(__linux__)
#include <sys/sysmacros.h>
#endif

#include <libfyaml.h>

/* ANSI colors and escapes */
#define A_RESET			"\x1b[0m"
#define A_BLACK			"\x1b[30m"
#define A_RED			"\x1b[31m"
#define A_GREEN			"\x1b[32m"
#define A_YELLOW		"\x1b[33m"
#define A_BLUE			"\x1b[34m"
#define A_MAGENTA		"\x1b[35m"
#define A_CYAN			"\x1b[36m"
#define A_LIGHT_GRAY		"\x1b[37m"	/* dark white is gray */
#define A_GRAY			"\x1b[1;30m"
#define A_BRIGHT_RED		"\x1b[1;31m"
#define A_BRIGHT_GREEN		"\x1b[1;32m"
#define A_BRIGHT_YELLOW		"\x1b[1;33m"
#define A_BRIGHT_BLUE		"\x1b[1;34m"
#define A_BRIGHT_MAGENTA	"\x1b[1;35m"
#define A_BRIGHT_CYAN		"\x1b[1;36m"
#define A_WHITE			"\x1b[1;37m"

/* in fy-tool-dump.c */
void print_escaped(const char *str, size_t length);

enum dump_testsuite_event_flags {
	DTEF_COLORIZE = FY_BIT(0),
	DTEF_DISABLE_FLOW_MARKERS = FY_BIT(1),
	DTEF_DISABLE_DOC_MARKERS = FY_BIT(2),
	DTEF_DISABLE_SCALAR_STYLES = FY_BIT(3),
	DTEF_TSV_FORMAT = FY_BIT(4),
};

void dump_token_comments(struct fy_token *fyt, bool colorize, const char *banner);
void dump_testsuite_event(struct fy_event *fye, enum dump_testsuite_event_flags dump_flags);
void dump_parse_event(struct fy_event *fye, bool colorize);
void dump_scan_token(struct fy_token *fyt, bool colorize);

#endif
