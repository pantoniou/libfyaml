/*
 * fy-ctype.h - ctype like macros header
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_CTYPE_H
#define FY_CTYPE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <alloca.h>
#include <string.h>
#include <assert.h>

#include <libfyaml.h>

#include "fy-utf8.h"

static inline bool fy_is_first_alpha(int c)
{
	return (c >= 'a' && c <= 'z') ||
	       (c >= 'A' && c <= 'Z') ||
	       c == '_';
}

static inline bool fy_is_alpha(int c)
{
	return fy_is_first_alpha(c) || c == '-';
}

static inline bool fy_is_num(int c)
{
	return c >= '0' && c <= '9';
}

static inline bool fy_is_first_alnum(int c)
{
	return fy_is_first_alpha(c);
}

static inline bool fy_is_alnum(int c)
{
	return fy_is_alpha(c) || fy_is_num(c);
}

static inline bool fy_is_space(int c)
{
	return c == ' ';
}

static inline bool fy_is_tab(int c)
{
	return c == '\t';
}

static inline bool fy_is_ws(int c)
{
	return fy_is_space(c) || fy_is_tab(c);
}

static inline bool fy_is_hex(int c)
{
	return (c >= '0' && c <= '9') ||
	       (c >= 'a' && c <= 'f') ||
	       (c >= 'A' && c <= 'F');
}

static inline bool fy_is_uri(int c)
{
	return fy_is_alnum(c) || fy_utf8_strchr(";/?:@&=+$,.!~*\'()[]%", c);
}

static inline bool fy_is_json_lb(int c)
{
	return c == '\r' || c == '\n';
}

static inline bool fy_is_yaml12_lb(int c)
{
	/* note that YAML1.2 support NEL #x85, LS #x2028 and PS #x2029 */
	return c == 0x85 || c == 0x2028 || c == 0x2029;
}

static inline bool fy_is_lb(int c)
{
	return fy_is_json_lb(c) || fy_is_yaml12_lb(c);
}

static inline bool fy_is_z(int c)
{
	return c <= 0;
}

static inline bool fy_is_lbz(int c)
{
	return fy_is_lb(c) || fy_is_z(c);
}

static inline bool fy_is_spacez(int c)
{
	return fy_is_space(c) || fy_is_lbz(c);
}

static inline bool fy_is_blank(int c)
{
	return c == ' ' || c == '\t';
}

static inline bool fy_is_blankz(int c)
{
	return fy_is_blank(c) || fy_is_lbz(c);
}

static inline bool fy_is_ws_lb(int c)
{
	return fy_is_ws(c) || fy_is_lb(c);
}

#define FY_UTF8_BOM	0xfeff

static inline bool fy_is_print(int c)
{
	return c == '\n' || c == '\r' ||
	       (c >= 0x0020 && c <= 0x007e) ||
	       (c >= 0x00a0 && c <= 0xd7ff) ||
	       (c >= 0xe000 && c <= 0xfffd && c != FY_UTF8_BOM);
}

static inline bool fy_is_printq(int c)
{
	return c != '\t' && c != 0xa0 && !fy_is_lb(c) &&
	       fy_is_print(c);
}

static inline bool fy_is_nb_char(int c)
{
	return (c >= 0x0020 && c <= 0x007e) ||
	       (c >= 0x00a0 && c <= 0xd7ff) ||
	       (c >= 0xe000 && c <= 0xfffd && c != FY_UTF8_BOM);
}

static inline bool fy_is_ns_char(int c)
{
	return fy_is_nb_char(c) && !fy_is_ws(c);
}

static inline bool fy_is_indicator(int c)
{
	return !!fy_utf8_strchr("-?:,[]{}#&*!|>'\"%%@`", c);
}

static inline bool fy_is_flow_indicator(int c)
{
	return !!fy_utf8_strchr(",[]{}", c);
}

static inline bool fy_is_path_flow_scalar_start(int c)
{
	return c == '\'' || c == '"';
}

static inline bool fy_is_path_flow_key_start(int c)
{
	return c == '"' || c == '\'' || c == '{' || c == '[';
}

static inline bool fy_is_path_flow_key_end(int c)
{
	return c == '"' || c == '\'' || c == '}' || c == ']';
}

static inline bool fy_is_unicode_control(int c)
{
        return (c >= 0 && c <= 0x1f) || (c >= 0x80 && c <= 0x9f);
}

static inline bool fy_is_unicode_space(int c)
{
        return c == 0x20 || c == 0xa0 ||
               (c >= 0x2000 && c <= 0x200a) ||
               c == 0x202f || c == 0x205f || c == 0x3000;
}

static inline bool fy_is_json_unescaped(int c)
{
	return c >= 0x20 && c <= 0x110000 && c != '"' && c != '\\';
}

static inline bool fy_is_lb_yj(int c, bool json_mode)
{
	/* '\r', '\n' are always linebreaks */
	if (fy_is_json_lb(c))
		return true;
	if (json_mode)
		return false;
	return fy_is_yaml12_lb(c);
}

static inline bool fy_is_lbz_yj(int c, bool json_mode)
{
	return fy_is_lb_yj(c, json_mode) || fy_is_z(c);
}

static inline bool fy_is_blankz_yj(int c, bool json_mode)
{
	return fy_is_ws(c) || fy_is_lbz_yj(c, json_mode);
}

static inline bool fy_is_flow_ws_yj(int c, bool json_mode)
{
	/* space is always allowed */
	if (fy_is_space(c))
		return true;
	/* no other space for JSON */
	if (json_mode)
		return false;
	/* YAML allows tab for WS */
	return fy_is_tab(c);
}

static inline bool fy_is_flow_blank_yj(int c, bool json_mode)
{
	return fy_is_flow_ws_yj(c, json_mode);
}

static inline bool fy_is_flow_blankz_yj(int c, bool json_mode)
{
	return fy_is_flow_ws_yj(c, json_mode) || fy_is_lbz_yj(c, json_mode);
}

#define FY_CTYPE_AT_BUILDER(_kind) \
static inline const void * \
fy_find_ ## _kind (const void *s, size_t len) \
{ \
	const void *e = s + len; \
	int c, w; \
	for (; s < e && (c = fy_utf8_get(s,  e - s, &w)) >= 0; s += w) { \
		assert(w); \
		if (fy_is_ ## _kind (c)) \
			return s; \
	} \
	return NULL; \
} \
static inline const void * \
fy_find_non_ ## _kind (const void *s, size_t len) \
{ \
	const void *e = s + len; \
	int c, w; \
	for (; s < e && (c = fy_utf8_get(s,  e - s, &w)) >= 0; s += w) { \
		assert(w); \
		if (!(fy_is_ ## _kind (c))) \
			return s; \
		assert(w); \
	} \
	return NULL; \
} \
struct useless_struct_for_semicolon

FY_CTYPE_AT_BUILDER(first_alpha);
FY_CTYPE_AT_BUILDER(alpha);
FY_CTYPE_AT_BUILDER(num);
FY_CTYPE_AT_BUILDER(first_alnum);
FY_CTYPE_AT_BUILDER(alnum);
FY_CTYPE_AT_BUILDER(space);
FY_CTYPE_AT_BUILDER(tab);
FY_CTYPE_AT_BUILDER(ws);
FY_CTYPE_AT_BUILDER(hex);
FY_CTYPE_AT_BUILDER(uri);
FY_CTYPE_AT_BUILDER(json_lb);
FY_CTYPE_AT_BUILDER(yaml12_lb);
FY_CTYPE_AT_BUILDER(lb);
FY_CTYPE_AT_BUILDER(z);
FY_CTYPE_AT_BUILDER(spacez);
FY_CTYPE_AT_BUILDER(blank);
FY_CTYPE_AT_BUILDER(blankz);
FY_CTYPE_AT_BUILDER(ws_lb);
FY_CTYPE_AT_BUILDER(print);
FY_CTYPE_AT_BUILDER(printq);
FY_CTYPE_AT_BUILDER(nb_char);
FY_CTYPE_AT_BUILDER(ns_char);
FY_CTYPE_AT_BUILDER(indicator);
FY_CTYPE_AT_BUILDER(flow_indicator);
FY_CTYPE_AT_BUILDER(path_flow_key_start);
FY_CTYPE_AT_BUILDER(path_flow_key_end);
FY_CTYPE_AT_BUILDER(unicode_control);
FY_CTYPE_AT_BUILDER(unicode_space);
FY_CTYPE_AT_BUILDER(json_unescaped);

/*
 * Very special linebreak/ws methods
 * Things get interesting due to \r\n and
 * unicode linebreaks/spaces
 */

/* skip for a _single_ linebreak */
static inline const void *fy_skip_lb(const void *ptr, int left)
{
	int c, width;

	/* get the utf8 character at this point */
	c = fy_utf8_get(ptr, left, &width);
	if (c < 0 || !fy_is_lb(c))
		return NULL;

	/* MS-DOS: check if next character is '\n' */
	if (c == '\r' && left > width && *(char *)ptr == '\n')
		width++;

	return ptr + width;
}

/* given a pointer to a chunk of memory, return pointer to first
 * ws character after the last non-ws character, or the end
 * of the chunk
 */
static inline const void *fy_last_non_ws(const void *ptr, int left)
{
	const char *s, *e;
	int c;

	s = ptr;
	e = s + left;
	while (e > s) {
		c = e[-1];
		if (c != ' ' && c != '\t')
			return e;
		e--;

	}
	return NULL;
}

const char *fy_uri_esc(const char *s, size_t len, uint8_t *code, int *code_len);

#endif
