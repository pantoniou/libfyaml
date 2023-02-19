/*
 * fy-reflection-util.h - internal meta type utilities header file
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_REFLECTION_UTIL_H
#define FY_REFLECTION_UTIL_H

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
#include <libfyaml/libfyaml-reflection.h>

static inline bool
memiszero(const void *ptr, size_t size)
{
	const uint8_t *p;
	size_t i;

	for (i = 0, p = ptr; i < size; i++) {
		if (p[i])
			return false;
	}
	return true;
}

static inline int parse_match_value(const char *text0, const char **check_vp)
{
	const char *check;
	int i;

	for (i = 0; (check = *check_vp++) != NULL; i++) {
		if (!strcmp(check, text0))
			return i;
	}
	return -1;
}

union integer_scalar {
	intmax_t sval;
	uintmax_t uval;
};

union float_scalar {
	float f;
	double d;
	long double ld;
};

uintmax_t load_le(const void *ptr, size_t width, bool is_signed);
void store_le(void *ptr, size_t width, uintmax_t v);
uintmax_t load_bitfield_le(const void *ptr, size_t bit_offset, size_t bit_width, bool is_signed);
void store_bitfield_le(void *ptr, size_t bit_offset, size_t bit_width, uintmax_t v);

static inline intmax_t signed_integer_max_from_bit_width(size_t bit_width)
{
	assert(bit_width <= sizeof(intmax_t) * 8);
	return INTMAX_MAX >> (sizeof(intmax_t) * 8 - bit_width);
}

static inline intmax_t signed_integer_min_from_bit_width(size_t bit_width)
{
	assert(bit_width <= sizeof(intmax_t) * 8);
	return INTMAX_MIN >> (sizeof(intmax_t) * 8 - bit_width);
}

static inline uintmax_t unsigned_integer_max_from_bit_width(size_t bit_width)
{
	assert(bit_width <= sizeof(uintmax_t) * 8);
	return UINTMAX_MAX >> (sizeof(uintmax_t) * 8 - bit_width);
}

static inline intmax_t
signed_integer_min_from_size(size_t size)
{
	return signed_integer_min_from_bit_width(size * 8);
}

static inline intmax_t
signed_integer_max_from_size(size_t size)
{
	return signed_integer_max_from_bit_width(size * 8);
}

static inline uintmax_t
unsigned_integer_max_from_size(size_t size)
{
	return unsigned_integer_max_from_bit_width(size * 8);
}

static inline intmax_t
signed_integer_min(enum fy_type_kind type_kind)
{
	return signed_integer_min_from_size(fy_type_kind_size(type_kind));
}

static inline intmax_t
signed_integer_max(enum fy_type_kind type_kind)
{
	return signed_integer_max_from_size(fy_type_kind_size(type_kind));
}

static inline uintmax_t
unsigned_integer_max(enum fy_type_kind type_kind)
{
	return unsigned_integer_max_from_size(fy_type_kind_size(type_kind));
}

static inline bool str_null_eq(const char *s1, const char *s2)
{
	if (s1 == s2)
		return true;
	if (!s1 || !s2)
		return false;
	return !strcmp(s1, s2);
}

int parse_integer_scalar(enum fy_type_kind type_kind, const char *text0, enum fy_parser_mode mode, union integer_scalar *nump);
void store_integer_scalar(enum fy_type_kind type_kind, void *data, union integer_scalar num);
union integer_scalar load_integer_scalar(enum fy_type_kind type_kind, const void *data);

int parse_float_scalar(enum fy_type_kind type_kind, const char *text0, enum fy_parser_mode mode, union float_scalar *numf);
void store_float_scalar(enum fy_type_kind type_kind, void *data, union float_scalar numf);
union float_scalar load_float_scalar(enum fy_type_kind type_kind, const void *data);

int parse_boolean_scalar(const char *text0, enum fy_parser_mode mode, bool *vp);
void store_boolean_scalar(enum fy_type_kind type_kind, void *data, bool v);
bool load_boolean_scalar(enum fy_type_kind type_kind, const void *data);

int parse_null_scalar(const char *text0, enum fy_parser_mode mode);

void type_info_dump(const struct fy_type_info *ti, int level);

struct reflection_walker;

union integer_scalar load_integer_scalar_rw(struct reflection_walker *rw);
int store_integer_scalar_check_rw(struct reflection_walker *rw,
			      union integer_scalar numi,
			      union integer_scalar *minip, union integer_scalar *maxip);
void store_integer_scalar_no_check_rw(struct reflection_walker *rw, union integer_scalar num);
int store_integer_scalar_rw(struct reflection_walker *rw, union integer_scalar num);

void store_boolean_scalar_rw(struct reflection_walker *rw, bool v);
bool load_boolean_scalar_rw(struct reflection_walker *rw);

/* logging infra */
enum fy_rfl_op {
	fy_rfl_op_invalid = -1,
	fy_rfl_op_rfl,
	fy_rfl_op_rts,
	fy_rfl_op_rw,
	fy_rfl_op_parse,
	fy_rfl_op_emit,
};

struct fy_rfl_log_ctx {
	enum fy_rfl_op op;
	union {
		void *ptr;
		struct fy_reflection *rfl;
		struct reflection_type_system *rts;
		struct reflection_walker *rw;
		struct fy_parser *fyp;
		struct fy_emitter *emit;
	};
	struct fy_event *fye;
	bool needs_event;
	enum fy_event_part event_part;
	struct fy_token *fyt;
	struct fy_diag_ctx diag_ctx;
	bool has_diag_ctx;
	bool save_error;
};

void
fy_rfl_vlog(struct fy_rfl_log_ctx *ctx, enum fy_error_type error_type,
	    const char *fmt, va_list ap);

void
fy_rfl_log(struct fy_rfl_log_ctx *ctx, enum fy_error_type error_type,
	   const char *fmt, ...)
	FY_FORMAT(printf, 3, 4);

#define _FY_RFL_SELECT_OP(_ptr) \
	_Generic((_ptr), \
		struct fy_reflection *: fy_rfl_op_rfl, \
		struct reflection_type_system *: fy_rfl_op_rts, \
		struct reflection_walker *: fy_rfl_op_rw, \
		struct fy_parser *: fy_rfl_op_parse, \
		struct fy_emit *: fy_rfl_op_emit, \
		default: fy_rfl_op_invalid /* ( _Static_assert(0, "Unsupported type"), 0) */ )

#define _FY_RFL_NEEDS_EVENT(_ptr) \
	_Generic((_ptr), \
		struct fy_event *: true, \
		default: false)

#define _FY_RFL_EVENT(_ptr) \
	_Generic((_ptr), \
		struct fy_event *: (_ptr), \
		default: NULL)

#define _FY_RFL_EVENT_PART(_ptr) \
	_Generic((_ptr), \
		struct fy_event *: FYEP_VALUE, \
		default: 0)

#define _FY_RFL_TOKEN(_ptr) \
	_Generic((_ptr), \
		struct fy_token *: (_ptr), \
		default: NULL)

#define _FY_RFL_LOG(_ptr, _level, _save_error, _xtra, _fmt, ...) \
	({ \
		const enum fy_error_type __level = (_level); \
		struct fy_rfl_log_ctx __ctx = { \
			.op = _FY_RFL_SELECT_OP(_ptr), \
			.ptr = (_ptr), \
			.diag_ctx = { \
				.level = __level, \
				.module = FYEM_REFLECTION, \
				.source_func = __func__, \
				.source_file = __FILE__, \
				.source_line = __LINE__, \
			}, \
			.has_diag_ctx = true, \
			.save_error = (_save_error), \
			.fye = _FY_RFL_EVENT(_xtra), \
			.needs_event = _FY_RFL_NEEDS_EVENT(_xtra), \
			.event_part = _FY_RFL_EVENT_PART(_xtra), \
			.fyt = _FY_RFL_TOKEN(_xtra), \
		}; \
		fy_rfl_log(&__ctx, __level, (_fmt) , ## __VA_ARGS__); \
	})

#define _FY_RFL_ERROR_CHECK(_ptr, _expr, _label, _save_error, _xtra, _fmt, ...) \
	do { \
		if (!(_expr)) { \
			_FY_RFL_LOG((_ptr), FYET_ERROR, (_save_error), (_xtra), (_fmt), ## __VA_ARGS__); \
			goto _label; \
		} \
	} while(0)

/* fy_reflection * */
#define rfl_diag(_ptr, _level, _fmt, ...) \
	_FY_RFL_LOG((_ptr), (_level), false, NULL, (_fmt), ## __VA_ARGS__)

#ifndef NDEBUG

#define rfl_debug(_rfl, _fmt, ...) \
	rfl_diag((_rfl), FYET_DEBUG, (_fmt) , ## __VA_ARGS__)
#else

#define rfl_debug(_rfl, _fmt, ...) \
	do { } while(0)

#endif

#define rfl_info(_rfl, _fmt, ...) \
	rfl_diag((_rfl), FYET_INFO, (_fmt) , ## __VA_ARGS__)
#define rfl_notice(_rfl, _fmt, ...) \
	rfl_diag((_rfl), FYET_NOTICE, (_fmt) , ## __VA_ARGS__)
#define rfl_warning(_rfl, _fmt, ...) \
	rfl_diag((_rfl), FYET_WARNING, (_fmt) , ## __VA_ARGS__)
#define rfl_error(_rfl, _fmt, ...) \
	rfl_diag((_rfl), FYET_ERROR, (_fmt) , ## __VA_ARGS__)

/* reflection error check, assume rfl in scope and an err_out label */
#define RFL_ERROR_CHECK(_expr, _fmt, ...) \
	_FY_RFL_ERROR_CHECK(rfl, (_expr), err_out, true, NULL, (_fmt) , ## __VA_ARGS__)

/* reflection assert, assume rfl in scope and an err_out label */
#define RFL_ASSERT(_expr) \
	_FY_RFL_ERROR_CHECK(rfl, (_expr), err_out, false, NULL, \
			"%s: %s:%d: assert failed " #_expr "\n", \
			__func__, __FILE__, __LINE__)

/* reflection_type_system * */
#define rts_diag(_ptr, _level, _fmt, ...) \
	_FY_RFL_LOG((_ptr), (_level), false, NULL, (_fmt), ## __VA_ARGS__)

#ifndef NDEBUG

#define rts_debug(_rts, _fmt, ...) \
	rts_diag((_rts), FYET_DEBUG, (_fmt) , ## __VA_ARGS__)
#else

#define rts_debug(_rts, _fmt, ...) \
	do { } while(0)

#endif

#define rts_info(_rts, _fmt, ...) \
	rts_diag((_rts), FYET_INFO, (_fmt) , ## __VA_ARGS__)
#define rts_notice(_rts, _fmt, ...) \
	rts_diag((_rts), FYET_NOTICE, (_fmt) , ## __VA_ARGS__)
#define rts_warning(_rts, _fmt, ...) \
	rts_diag((_rts), FYET_WARNING, (_fmt) , ## __VA_ARGS__)
#define rts_error(_rts, _fmt, ...) \
	rts_diag((_rts), FYET_ERROR, (_fmt) , ## __VA_ARGS__)

/* reflection type system error check, assume rts in scope and an err_out label */
#define RTS_ERROR_CHECK(_expr, _fmt, ...) \
	_FY_RFL_ERROR_CHECK(rts, (_expr), err_out, true, NULL, (_fmt) , ## __VA_ARGS__)

/* reflection assert, assume rfl in scope and an err_out label */
#define RTS_ASSERT(_expr) \
	_FY_RFL_ERROR_CHECK(rts, (_expr), err_out, false, NULL, \
			"%s: %s:%d: assert failed " #_expr "\n", \
			__func__, __FILE__, __LINE__)

/* reflection assert, assume rw in scope and an err_out label */
#define RW_ASSERT(_expr) \
	_FY_RFL_ERROR_CHECK(rw, (_expr), err_out, false, NULL, \
			"%s: %s:%d: assert failed " #_expr "\n", \
			__func__, __FILE__, __LINE__)

/* reflection parse error check, assume fyp in scope and an err_out label */
#define RP_ERROR_CHECK(_fyp, _expr, _label, _fmt, ...) \
	_FY_RFL_ERROR_CHECK((_fyp), (_expr), err_out, true, NULL, \
			(_fmt) , ## __VA_ARGS__)

/* reflection parse assert, assume fyp in scope and an err_out label */
#define RP_ASSERT(_expr) \
	_FY_RFL_ERROR_CHECK(fyp, (_expr), err_out, false, NULL, \
			"%s: %s:%d: assert failed " #_expr "\n", \
			__func__, __FILE__, __LINE__)

/* assume fyp, fye, flags are defined as well as an err_input: label */
#define RP_INPUT_CHECK(_expr, _fmt, ...) \
	_FY_RFL_ERROR_CHECK(fyp, _expr, err_input, \
			!(flags & RPF_SILENT_INVALID_INPUT), fye, \
			_fmt , ## __VA_ARGS__)

/* reflection emit assert, assume emit scope and an err_out label */
#define RE_ASSERT(_expr) \
	_FY_RFL_ERROR_CHECK(emit, (_expr), err_out, false, NULL, \
			"%s: %s:%d: assert failed " #_expr "\n", \
			__func__, __FILE__, __LINE__)

/* reflection emit error check, assume emit in scope and an err_out label */
#define RE_ERROR_CHECK(_emit, _expr, _label, _fmt, ...) \
	_FY_RFL_ERROR_CHECK((_emit), (_expr), _label, true, NULL, \
			(_fmt) , ## __VA_ARGS__)

/* assume emit, flags are defined as well as an err_output: label */
#define RE_OUTPUT_CHECK(_expr, _fmt, ...) \
	_FY_RFL_ERROR_CHECK(emit, _expr, err_output, \
			!(flags & REF_SILENT_INVALID_OUTPUT), NULL, \
			_fmt , ## __VA_ARGS__)

/* print walker path helper */
struct reflection_walker;
struct reflection_meta;

void reflection_walker_print_path(struct reflection_walker *rw);
void reflection_meta_dump(struct reflection_meta *rm);

#endif
