/*
 * libfyaml-test-generic-scalars.c - scalar resolution tests across schemas
 *
 * Tests fy_gb_create_scalar_from_text() behavior across:
 *   FYGS_YAML1_2_CORE, FYGS_YAML1_1, FYGS_YAML1_1_PYYAML, FYGS_JSON
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
#include <math.h>
#include <float.h>

#include <check.h>

#include <libfyaml.h>

#include "fy-generic.h"
#include "fy-generic-decoder.h"
#include "fy-generic-encoder.h"

#include "fy-check.h"

/* make it less noisy, this is a test-suite after all */
FY_DIAG_PUSH
FY_DIAG_IGNORE_UNUSED_VARIABLE
FY_DIAG_IGNORE_UNUSED_PARAMETER

/*
 * Test infrastructure for scalar resolution across schemas.
 *
 * Each test case specifies:
 *   - input text (plain scalar)
 *   - expected result per schema (type + value)
 *
 * We use fy_gb_create_scalar_from_text(gb, text, len, FYGT_INVALID)
 * which performs implicit tag resolution - exactly what happens during
 * YAML document parsing for plain scalars.
 */

enum expected_type {
	E_NULL,		/* expect null */
	E_BOOL,		/* expect bool (true/false) */
	E_INT,		/* expect int64_t */
	E_FLOAT,	/* expect double */
	E_INF_POS,	/* expect +infinity */
	E_INF_NEG,	/* expect -infinity */
	E_NAN,		/* expect NaN */
	E_STR,		/* expect string (same as input) */
	E_STRS,	/* expect specific string (different from input, e.g. quoted) */
};

struct expected_value {
	enum expected_type type;
	union {
		bool b;
		long long i;
		double f;
		const char *s;	/* for E_STRS */
	};
};

/* Convenience macros for building expected values */
#define ENULL		{ .type = E_NULL }
#define EBOOL(val)	{ .type = E_BOOL, .b = (val) }
#define EINT(val)	{ .type = E_INT, .i = (val) }
#define EFLOAT(val)	{ .type = E_FLOAT, .f = (val) }
#define EINF_POS	{ .type = E_INF_POS }
#define EINF_NEG	{ .type = E_INF_NEG }
#define ENAN		{ .type = E_NAN }
#define ESTR		{ .type = E_STR }
#define ESTRS(val)	{ .type = E_STRS, .s = (val) }

struct scalar_test {
	const char *input;
	struct expected_value yaml12;
	struct expected_value yaml11;
	struct expected_value pyyaml;
	struct expected_value json;
};

static struct fy_generic_builder *
create_builder_with_schema(enum fy_generic_schema schema, char *buf, size_t bufsz)
{
	unsigned int flags;

	switch (schema) {
	case FYGS_YAML1_2_CORE:
		flags = FYGBCF_SCHEMA_YAML1_2_CORE;
		break;
	case FYGS_YAML1_1:
		flags = FYGBCF_SCHEMA_YAML1_1;
		break;
	case FYGS_YAML1_1_PYYAML:
		flags = FYGBCF_SCHEMA_YAML1_1_PYYAML;
		break;
	case FYGS_JSON:
		flags = FYGBCF_SCHEMA_JSON;
		break;
	default:
		flags = FYGBCF_SCHEMA_AUTO;
		break;
	}

	return fy_generic_builder_create_in_place(
		flags | FYGBCF_SCOPE_LEADER, NULL, buf, bufsz);
}

/* Check whether a generic value matches the expected value */
static bool
check_result(fy_generic result, const struct expected_value *exp, const char *input)
{
	double dv;

	switch (exp->type) {
	case E_NULL:
		return fy_generic_is_null_type(result);

	case E_BOOL:
		if (!fy_generic_is_bool_type(result))
			return false;
		return fy_cast(result, (_Bool)false) == exp->b;

	case E_INT:
		if (!fy_generic_is_int_type(result))
			return false;
		return fy_cast(result, (long long)-1) == exp->i;

	case E_FLOAT:
		if (!fy_generic_is_float_type(result))
			return false;
		dv = fy_cast(result, (double)0.0);
		if (exp->f == 0.0)
			return dv == 0.0;
		/* relative comparison for floating point */
		return fabs(dv - exp->f) < fabs(exp->f) * 1e-10;

	case E_INF_POS:
		if (!fy_generic_is_float_type(result))
			return false;
		dv = fy_cast(result, 0.0);
		return isinf(dv) && dv > 0;

	case E_INF_NEG:
		if (!fy_generic_is_float_type(result))
			return false;
		dv = fy_cast(result, 0.0);
		return isinf(dv) && dv < 0;

	case E_NAN:
		if (!fy_generic_is_float_type(result))
			return false;
		return isnan(fy_cast(result, 0.0));

	case E_STR:
		if (!fy_generic_is_string(result))
			return false;
		/* string value should equal input */
		return strcmp(fy_cast(result, ""), input) == 0;

	case E_STRS:
		if (!fy_generic_is_string(result))
			return false;
		return strcmp(fy_cast(result, ""), exp->s) == 0;
	}

	return false;
}

static const char *expected_type_name(enum expected_type t)
{
	switch (t) {
	case E_NULL:	return "null";
	case E_BOOL:	return "bool";
	case E_INT:	return "int";
	case E_FLOAT:	return "float";
	case E_INF_POS:	return "+inf";
	case E_INF_NEG:	return "-inf";
	case E_NAN:	return "nan";
	case E_STR:	return "str";
	case E_STRS:	return "str(specific)";
	}
	return "?";
}

static void
run_scalar_tests(const struct scalar_test *tests, size_t count,
		 const char *category)
{
	char buf[65536];
	struct fy_generic_builder *gb;
	fy_generic result;
	size_t i;
	int failures = 0;

	static const struct {
		const char *name;
		enum fy_generic_schema schema;
		size_t offset; /* offset of expected_value within scalar_test */
	} schemas[] = {
		{ "yaml1.2",       FYGS_YAML1_2_CORE,  offsetof(struct scalar_test, yaml12) },
		{ "yaml1.1",       FYGS_YAML1_1,       offsetof(struct scalar_test, yaml11) },
		{ "yaml1.1-pyyaml", FYGS_YAML1_1_PYYAML, offsetof(struct scalar_test, pyyaml) },
		{ "json",          FYGS_JSON,           offsetof(struct scalar_test, json) },
	};

	printf("\n> Scalar tests: %s (%zu cases)\n", category, count);

	for (i = 0; i < count; i++) {
		const struct scalar_test *t = &tests[i];
		size_t s;

		for (s = 0; s < sizeof(schemas) / sizeof(schemas[0]); s++) {
			const struct expected_value *exp =
				(const struct expected_value *)
				((const char *)t + schemas[s].offset);

			gb = create_builder_with_schema(schemas[s].schema, buf, sizeof(buf));
			ck_assert_ptr_ne(gb, NULL);

			result = fy_gb_create_scalar_from_text(
				gb, t->input, strlen(t->input), FYGT_INVALID);

			if (!check_result(result, exp, t->input)) {
				printf("  FAIL [%s] input=%s expected=%s",
				       schemas[s].name, t->input,
				       expected_type_name(exp->type));
				switch (exp->type) {
				case E_BOOL:
					printf("(%s)", exp->b ? "true" : "false");
					break;
				case E_INT:
					printf("(%lld)", (long long)exp->i);
					break;
				case E_FLOAT:
					printf("(%g)", exp->f);
					break;
				case E_STRS:
					printf("(\"%s\")", exp->s);
					break;
				default:
					break;
				}
				printf(" got=");
				fy_generic_emit_default(result);
				failures++;
			}
		}
	}

	ck_assert_msg(failures == 0,
		      "%s: %d scalar resolution failures", category, failures);
	printf("> %s: all %zu cases passed across 4 schemas\n", category, count);
}

/* =========================================================================
 * BOOLEANS
 * ========================================================================= */

static const struct scalar_test boolean_tests[] = {
	/* Core booleans - all YAML schemas agree */
	{ "true",  EBOOL(true),  EBOOL(true),  EBOOL(true),  EBOOL(true) },
	{ "false", EBOOL(false), EBOOL(false), EBOOL(false), EBOOL(false) },

	/*
	 * YAML 1.2 Core Schema (spec 10.3.2) defines bool as:
	 *   true | True | TRUE | false | False | FALSE
	 * so these ARE spec-compliant for yaml1.2.
	 *
	 * JSON (RFC 8259) only has true/false (lowercase).
	 */
	{ "True",  EBOOL(true),  EBOOL(true),  EBOOL(true),  ESTR },
	{ "False", EBOOL(false), EBOOL(false), EBOOL(false), ESTR },
	{ "TRUE",  EBOOL(true),  EBOOL(true),  EBOOL(true),  ESTR },
	{ "FALSE", EBOOL(false), EBOOL(false), EBOOL(false), ESTR },

	/* YAML 1.1 yes/no/on/off - not in yaml1.2 or json */
	{ "yes",   ESTR,         EBOOL(true),  EBOOL(true),  ESTR },
	{ "Yes",   ESTR,         EBOOL(true),  EBOOL(true),  ESTR },
	{ "YES",   ESTR,         EBOOL(true),  EBOOL(true),  ESTR },
	{ "no",    ESTR,         EBOOL(false), EBOOL(false), ESTR },
	{ "No",    ESTR,         EBOOL(false), EBOOL(false), ESTR },
	{ "NO",    ESTR,         EBOOL(false), EBOOL(false), ESTR },
	{ "on",    ESTR,         EBOOL(true),  EBOOL(true),  ESTR },
	{ "On",    ESTR,         EBOOL(true),  EBOOL(true),  ESTR },
	{ "ON",    ESTR,         EBOOL(true),  EBOOL(true),  ESTR },
	{ "off",   ESTR,         EBOOL(false), EBOOL(false), ESTR },
	{ "Off",   ESTR,         EBOOL(false), EBOOL(false), ESTR },
	{ "OFF",   ESTR,         EBOOL(false), EBOOL(false), ESTR },

	/*
	 * Single-letter y/Y/n/N: part of YAML 1.1 spec boolean set.
	 * DEVIATION(pyyaml): PyYAML does NOT accept single-letter y/Y/n/N
	 * as booleans despite the YAML 1.1 spec including them.
	 */
	{ "y",     ESTR,         EBOOL(true),  ESTR,         ESTR },
	{ "Y",     ESTR,         EBOOL(true),  ESTR,         ESTR },
	{ "n",     ESTR,         EBOOL(false), ESTR,         ESTR },
	{ "N",     ESTR,         EBOOL(false), ESTR,         ESTR },

	/* Mixed case - never booleans in any schema */
	{ "yEs",   ESTR,         ESTR,         ESTR,         ESTR },
	{ "nO",    ESTR,         ESTR,         ESTR,         ESTR },
	{ "tRue",  ESTR,         ESTR,         ESTR,         ESTR },
	{ "oN",    ESTR,         ESTR,         ESTR,         ESTR },
};

START_TEST(scalar_booleans)
{
	run_scalar_tests(boolean_tests,
			 sizeof(boolean_tests) / sizeof(boolean_tests[0]),
			 "booleans");
}
END_TEST

/* =========================================================================
 * NULLS
 * ========================================================================= */

static const struct scalar_test null_tests[] = {
	/*
	 * YAML 1.2 Core Schema (spec 10.3.2) null: null | Null | NULL | ~ | ""
	 * YAML 1.1 spec: same set.
	 * JSON (RFC 8259): only "null" (lowercase).
	 */
	{ "null",  ENULL,  ENULL,  ENULL,  ENULL },
	{ "Null",  ENULL,  ENULL,  ENULL,  ESTR },
	{ "NULL",  ENULL,  ENULL,  ENULL,  ESTR },
	{ "~",     ENULL,  ENULL,  ENULL,  ESTR },
	/* empty string: YAML null, JSON string */
	{ "",      ENULL,  ENULL,  ENULL,  ESTR },
};

START_TEST(scalar_nulls)
{
	run_scalar_tests(null_tests,
			 sizeof(null_tests) / sizeof(null_tests[0]),
			 "nulls");
}
END_TEST

/* =========================================================================
 * INTEGERS - Decimal
 * ========================================================================= */

static const struct scalar_test int_decimal_tests[] = {
	/*
	 * YAML 1.2 Core Schema int: [-+]?[0-9]+
	 * YAML 1.1 int (decimal): [-+]?(0|[1-9][0-9_]*)
	 * JSON (RFC 8259): -?(0|[1-9][0-9]*) — no leading zeros, no +prefix.
	 */
	{ "0",         EINT(0),         EINT(0),         EINT(0),         EINT(0) },
	{ "1",         EINT(1),         EINT(1),         EINT(1),         EINT(1) },
	{ "42",        EINT(42),        EINT(42),        EINT(42),        EINT(42) },
	{ "-42",       EINT(-42),       EINT(-42),       EINT(-42),       EINT(-42) },
	{ "+42",       EINT(42),        EINT(42),        EINT(42),        ESTR },
	{ "123456789", EINT(123456789), EINT(123456789), EINT(123456789), EINT(123456789) },

	/*
	 * Leading zeros:
	 * - YAML 1.2: matches [-+]?[0-9]+ so resolves as decimal integer.
	 * - YAML 1.1: matches 0[0-7]+ (octal); "007" = octal 7 = decimal 7.
	 * - JSON (RFC 8259): leading zeros are forbidden — number is
	 *   defined as -?(0|[1-9][0-9]*), so only bare "0" may start
	 *   with 0. libfyaml correctly returns string for these in JSON mode.
	 */
	{ "007",       EINT(7),         EINT(7),         EINT(7),         ESTR },
	{ "00",        EINT(0),         EINT(0),         EINT(0),         ESTR },
};

START_TEST(scalar_int_decimal)
{
	run_scalar_tests(int_decimal_tests,
			 sizeof(int_decimal_tests) / sizeof(int_decimal_tests[0]),
			 "integers_decimal");
}
END_TEST

/* =========================================================================
 * INTEGERS - Octal (YAML 1.1 style: 0NNN)
 * ========================================================================= */

static const struct scalar_test int_octal_tests[] = {
	/*
	 * YAML 1.1 octal: [-+]?0[0-7_]+ (base 8).
	 * YAML 1.2: no special octal syntax with leading zero; [-+]?[0-9]+
	 *   matches, so "010" = decimal 10.
	 *
	 * DEVIATION(json): RFC 8259 forbids leading zeros entirely.
	 * libfyaml parses "010" as decimal 10 in JSON mode instead of
	 * rejecting it as a string. This is lenient behavior.
	 */
	{ "010",    EINT(10),   EINT(8),    EINT(8),    EINT(10) },
	{ "052",    EINT(52),   EINT(42),   EINT(42),   EINT(52) },
	{ "0777",   EINT(777),  EINT(511),  EINT(511),  EINT(777) },
	{ "-010",   EINT(-10),  EINT(-8),   EINT(-8),   EINT(-10) },
	{ "+0777",  EINT(777),  EINT(511),  EINT(511),  ESTR },

	/* Invalid octal digits in YAML 1.1 - still decimal in YAML 1.2/JSON */
	{ "089",    EINT(89),   ESTR,       ESTR,       EINT(89) },
	{ "0999",   EINT(999),  ESTR,       ESTR,       EINT(999) },
};

START_TEST(scalar_int_octal)
{
	run_scalar_tests(int_octal_tests,
			 sizeof(int_octal_tests) / sizeof(int_octal_tests[0]),
			 "integers_octal");
}
END_TEST

/* =========================================================================
 * INTEGERS - Octal (YAML 1.2 style: 0oNNN)
 * ========================================================================= */

static const struct scalar_test int_octal_0o_tests[] = {
	/*
	 * YAML 1.2 Core Schema octal: 0o[0-7]+
	 * This is a YAML 1.2-only syntax; YAML 1.1 uses 0[0-7]+ (no 'o').
	 */
	{ "0o10",   EINT(8),    ESTR,       ESTR,       ESTR },
	{ "0o52",   EINT(42),   ESTR,       ESTR,       ESTR },
	{ "0o777",  EINT(511),  ESTR,       ESTR,       ESTR },
};

START_TEST(scalar_int_octal_0o)
{
	run_scalar_tests(int_octal_0o_tests,
			 sizeof(int_octal_0o_tests) / sizeof(int_octal_0o_tests[0]),
			 "integers_octal_0o");
}
END_TEST

/* =========================================================================
 * INTEGERS - Hexadecimal
 * ========================================================================= */

static const struct scalar_test int_hex_tests[] = {
	/*
	 * YAML 1.2 Core Schema hex: 0x[0-9a-fA-F]+
	 * YAML 1.1 hex: 0x[0-9a-fA-F_]+
	 * JSON: no hex support.
	 */
	{ "0x0",        EINT(0),          EINT(0),          EINT(0),          ESTR },
	{ "0x2A",       EINT(42),         EINT(42),         EINT(42),         ESTR },
	{ "0x2a",       EINT(42),         EINT(42),         EINT(42),         ESTR },
	{ "0xFF",       EINT(255),        EINT(255),        EINT(255),        ESTR },
	{ "0xDEADBEEF", EINT(0xDEADBEEF), EINT(0xDEADBEEF), EINT(0xDEADBEEF), ESTR },
	{ "-0x2A",      EINT(-42),        EINT(-42),        EINT(-42),        ESTR },
	{ "+0x2A",      EINT(42),         EINT(42),         EINT(42),         ESTR },

	/* Invalid hex */
	{ "0xGG",       ESTR,             ESTR,             ESTR,             ESTR },
};

START_TEST(scalar_int_hex)
{
	run_scalar_tests(int_hex_tests,
			 sizeof(int_hex_tests) / sizeof(int_hex_tests[0]),
			 "integers_hex");
}
END_TEST

/* =========================================================================
 * INTEGERS - Binary (YAML 1.1 only)
 * ========================================================================= */

static const struct scalar_test int_binary_tests[] = {
	/*
	 * YAML 1.1 binary: 0b[0-1_]+
	 * Not in YAML 1.2 Core Schema or JSON.
	 */
	{ "0b0",        ESTR,       EINT(0),    EINT(0),    ESTR },
	{ "0b1",        ESTR,       EINT(1),    EINT(1),    ESTR },
	{ "0b101010",   ESTR,       EINT(42),   EINT(42),   ESTR },
	{ "0b11111111", ESTR,       EINT(255),  EINT(255),  ESTR },
	{ "-0b101010",  ESTR,       EINT(-42),  EINT(-42),  ESTR },

	/* Invalid binary */
	{ "0b102",      ESTR,       ESTR,       ESTR,       ESTR },
};

START_TEST(scalar_int_binary)
{
	run_scalar_tests(int_binary_tests,
			 sizeof(int_binary_tests) / sizeof(int_binary_tests[0]),
			 "integers_binary");
}
END_TEST

/* =========================================================================
 * INTEGERS - Underscores (YAML 1.1 extension)
 * ========================================================================= */

static const struct scalar_test int_underscore_tests[] = {
	/*
	 * YAML 1.1 allows underscores as digit separators in integers:
	 *   decimal: [-+]?(0|[1-9][0-9_]*)
	 *   hex: 0x[0-9a-fA-F_]+
	 *   octal: 0[0-7_]+
	 *   binary: 0b[0-1_]+
	 * Not in YAML 1.2 Core Schema or JSON.
	 */
	{ "1_000",          ESTR,              EINT(1000),        EINT(1000),        ESTR },
	{ "1_000_000",      ESTR,              EINT(1000000),     EINT(1000000),     ESTR },
	{ "0x_2A",          ESTR,              EINT(42),          EINT(42),          ESTR },
	{ "0x_DE_AD_BE_EF", ESTR,              EINT(0xDEADBEEF),  EINT(0xDEADBEEF),  ESTR },
	{ "0b_1010_0111",   ESTR,              EINT(167),         EINT(167),         ESTR },
	{ "0_52",           ESTR,              EINT(42),          EINT(42),          ESTR },

	/* Leading underscore: always string (not a valid number prefix) */
	{ "_1000",          ESTR,              ESTR,              ESTR,              ESTR },
	/*
	 * Trailing and consecutive underscores: the YAML 1.1 regex
	 * [-+]?(0|[1-9][0-9_]*) uses a flat character class [0-9_]*
	 * which allows any mix of digits and underscores, so "1000_"
	 * and "1__000" are technically valid matches.
	 */
	{ "1000_",          ESTR,              EINT(1000),        EINT(1000),        ESTR },
	{ "1__000",         ESTR,              EINT(1000),        EINT(1000),        ESTR },
};

START_TEST(scalar_int_underscore)
{
	run_scalar_tests(int_underscore_tests,
			 sizeof(int_underscore_tests) / sizeof(int_underscore_tests[0]),
			 "integers_underscore");
}
END_TEST

/* =========================================================================
 * INTEGERS - Sexagesimal (base 60, YAML 1.1)
 * ========================================================================= */

static const struct scalar_test int_sexagesimal_tests[] = {
	/*
	 * YAML 1.1 sexagesimal integer: [-+]?[1-9][0-9_]*(:[0-5]?[0-9])+
	 * Not in YAML 1.2 or JSON.
	 * e.g. "190:20:30" = 190*3600 + 20*60 + 30 = 685230
	 *
	 * DEVIATION(yaml1.1, pyyaml): Sexagesimal integers are not
	 * implemented in the C library. All return strings instead of
	 * the expected integer values.
	 */
	{ "1:0",       ESTR,          ESTR,         ESTR,         ESTR },
	{ "1:30",      ESTR,          ESTR,         ESTR,         ESTR },
	{ "1:00:00",   ESTR,          ESTR,         ESTR,         ESTR },
	{ "190:20:30", ESTR,          ESTR,         ESTR,         ESTR },
	{ "-1:30",     ESTR,          ESTR,         ESTR,         ESTR },

	/* Invalid (segment > 59 except first) */
	{ "1:60",      ESTR,          ESTR,         ESTR,         ESTR },
	{ "1:99",      ESTR,          ESTR,         ESTR,         ESTR },
};

START_TEST(scalar_int_sexagesimal)
{
	run_scalar_tests(int_sexagesimal_tests,
			 sizeof(int_sexagesimal_tests) / sizeof(int_sexagesimal_tests[0]),
			 "integers_sexagesimal");
}
END_TEST

/* =========================================================================
 * FLOATS - Basic
 * ========================================================================= */

static const struct scalar_test float_basic_tests[] = {
	/*
	 * YAML 1.2 Core Schema float: [-+]?(\.[0-9]+|[0-9]+(\.[0-9]*)?)([eE][-+]?[0-9]+)?
	 * YAML 1.1 float: [-+]?([0-9][0-9_]*)?\.[0-9_]*([eE][-+][0-9]+)?
	 * JSON (RFC 8259): -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
	 *   — no + prefix, requires leading digit.
	 */
	{ "0.0",              EFLOAT(0.0),              EFLOAT(0.0),              EFLOAT(0.0),              EFLOAT(0.0) },
	{ "1.0",              EFLOAT(1.0),              EFLOAT(1.0),              EFLOAT(1.0),              EFLOAT(1.0) },
	{ "3.14",             EFLOAT(3.14),             EFLOAT(3.14),             EFLOAT(3.14),             EFLOAT(3.14) },
	{ "-3.14",            EFLOAT(-3.14),            EFLOAT(-3.14),            EFLOAT(-3.14),            EFLOAT(-3.14) },
	{ "+3.14",            EFLOAT(3.14),             EFLOAT(3.14),             EFLOAT(3.14),             ESTR },
	{ "3.14159265358979", EFLOAT(3.14159265358979), EFLOAT(3.14159265358979), EFLOAT(3.14159265358979), EFLOAT(3.14159265358979) },
};

START_TEST(scalar_float_basic)
{
	run_scalar_tests(float_basic_tests,
			 sizeof(float_basic_tests) / sizeof(float_basic_tests[0]),
			 "floats_basic");
}
END_TEST

/* =========================================================================
 * FLOATS - Dot prefix (no integer part)
 * ========================================================================= */

static const struct scalar_test float_dot_prefix_tests[] = {
	/*
	 * Dot-prefix floats (no integer part before decimal point).
	 *
	 * YAML 1.2 Core Schema: [-+]?(\.[0-9]+|...) — explicitly allows
	 * dot-prefix. YAML 1.1: ([0-9][0-9_]*)?\.[0-9_]* — also allows it.
	 * JSON: requires leading digit — no dot-prefix.
	 */
	{ ".0",   EFLOAT(0.0),   EFLOAT(0.0),  EFLOAT(0.0),  ESTR },
	{ ".5",   EFLOAT(0.5),   EFLOAT(0.5),  EFLOAT(0.5),  ESTR },
	{ ".123", EFLOAT(0.123), EFLOAT(0.123), EFLOAT(0.123), ESTR },
	/*
	 * DEVIATION(yaml1.1, yaml1.2): Both specs allow signed dot-prefix
	 * floats (e.g. "-.5" matches [-+]?\.[0-9]+). libfyaml does not
	 * resolve these, returning strings instead.
	 */
	{ "-.5",  ESTR,          ESTR,         ESTR,         ESTR },
	{ "+.5",  ESTR,          ESTR,         ESTR,         ESTR },
};

START_TEST(scalar_float_dot_prefix)
{
	run_scalar_tests(float_dot_prefix_tests,
			 sizeof(float_dot_prefix_tests) / sizeof(float_dot_prefix_tests[0]),
			 "floats_dot_prefix");
}
END_TEST

/* =========================================================================
 * FLOATS - Trailing dot (no fractional part)
 * ========================================================================= */

static const struct scalar_test float_trailing_dot_tests[] = {
	/*
	 * Trailing-dot floats (no fractional digits after decimal point).
	 *
	 * YAML 1.2 Core Schema: [0-9]+(\.[0-9]*)? — explicitly allows
	 * trailing dot (zero fractional digits).
	 * YAML 1.1: ([0-9][0-9_]*)?\.[0-9_]* — also allows it.
	 * JSON: requires at least one fractional digit after dot.
	 */
	{ "0.",   EFLOAT(0.0),   EFLOAT(0.0),   EFLOAT(0.0),   ESTR },
	{ "1.",   EFLOAT(1.0),   EFLOAT(1.0),   EFLOAT(1.0),   ESTR },
	{ "42.",  EFLOAT(42.0),  EFLOAT(42.0),  EFLOAT(42.0),  ESTR },
	{ "-1.",  EFLOAT(-1.0),  EFLOAT(-1.0),  EFLOAT(-1.0),  ESTR },
};

START_TEST(scalar_float_trailing_dot)
{
	run_scalar_tests(float_trailing_dot_tests,
			 sizeof(float_trailing_dot_tests) / sizeof(float_trailing_dot_tests[0]),
			 "floats_trailing_dot");
}
END_TEST

/* =========================================================================
 * FLOATS - Scientific notation
 * ========================================================================= */

static const struct scalar_test float_scientific_tests[] = {
	/*
	 * Scientific notation.
	 *
	 * YAML 1.2: [0-9]+(\.[0-9]*)?[eE][-+]?[0-9]+ — sign on exponent
	 *   is optional, fractional part is optional.
	 * YAML 1.1: [0-9][0-9_]*(\.[0-9_]*)?[eE][-+][0-9]+ — sign on
	 *   exponent is REQUIRED.
	 * JSON: same as YAML 1.2 pattern (sign optional).
	 */

	/* With explicit sign on exponent (all schemas agree) */
	{ "1.0e+3",   EFLOAT(1000.0),    EFLOAT(1000.0),    EFLOAT(1000.0),    EFLOAT(1000.0) },
	{ "1.0e-3",   EFLOAT(0.001),     EFLOAT(0.001),     EFLOAT(0.001),     EFLOAT(0.001) },
	{ "1.0E+3",   EFLOAT(1000.0),    EFLOAT(1000.0),    EFLOAT(1000.0),    EFLOAT(1000.0) },
	{ "1.0E-3",   EFLOAT(0.001),     EFLOAT(0.001),     EFLOAT(0.001),     EFLOAT(0.001) },
	{ "6.022e+23", EFLOAT(6.022e+23), EFLOAT(6.022e+23), EFLOAT(6.022e+23), EFLOAT(6.022e+23) },
	{ "-1.0e+3",  EFLOAT(-1000.0),   EFLOAT(-1000.0),   EFLOAT(-1000.0),   EFLOAT(-1000.0) },

	/*
	 * Without sign on exponent: YAML 1.2 and JSON allow it.
	 * YAML 1.1 spec requires explicit sign: [eE][-+][0-9]+.
	 * PyYAML and libfyaml yaml1.1 mode both correctly reject these.
	 */
	{ "1.0e3",    EFLOAT(1000.0),    ESTR,              ESTR,              EFLOAT(1000.0) },
	{ "1.0E3",    EFLOAT(1000.0),    ESTR,              ESTR,              EFLOAT(1000.0) },
	{ "1e3",      EFLOAT(1000.0),    ESTR,              ESTR,              EFLOAT(1000.0) },

	/*
	 * Without fractional dot: YAML 1.2 and JSON allow bare integer
	 * with exponent. YAML 1.1 float regex requires a dot in the
	 * mantissa: ([0-9][0-9_]*)?\.[0-9_]*([eE][-+][0-9]+)?
	 * PyYAML and libfyaml yaml1.1 mode both correctly reject these.
	 */
	{ "1e+3",     EFLOAT(1000.0),    ESTR,              ESTR,              EFLOAT(1000.0) },
	{ "1e-3",     EFLOAT(0.001),     ESTR,              ESTR,              EFLOAT(0.001) },
};

START_TEST(scalar_float_scientific)
{
	run_scalar_tests(float_scientific_tests,
			 sizeof(float_scientific_tests) / sizeof(float_scientific_tests[0]),
			 "floats_scientific");
}
END_TEST

/* =========================================================================
 * FLOATS - Underscores (YAML 1.1 extension)
 * ========================================================================= */

static const struct scalar_test float_underscore_tests[] = {
	/*
	 * YAML 1.1 allows underscores in floats:
	 *   [-+]?([0-9][0-9_]*)?\.[0-9_]*([eE][-+][0-9]+)?
	 * Not in YAML 1.2 or JSON.
	 */
	{ "1_000.5",    ESTR,             EFLOAT(1000.5),    EFLOAT(1000.5),    ESTR },
	{ "1_000.5_0",  ESTR,             EFLOAT(1000.5),    EFLOAT(1000.5),    ESTR },
	{ "3.14_15_92", ESTR,             EFLOAT(3.141592),  EFLOAT(3.141592),  ESTR },
};

START_TEST(scalar_float_underscore)
{
	run_scalar_tests(float_underscore_tests,
			 sizeof(float_underscore_tests) / sizeof(float_underscore_tests[0]),
			 "floats_underscore");
}
END_TEST

/* =========================================================================
 * FLOATS - Sexagesimal (base 60, YAML 1.1)
 * ========================================================================= */

static const struct scalar_test float_sexagesimal_tests[] = {
	/*
	 * YAML 1.1 sexagesimal float: [-+]?[0-9][0-9_]*(:[0-5]?[0-9])+\.[0-9_]*
	 * e.g. "190:20:30.15" = 190*3600 + 20*60 + 30.15 = 685230.15
	 * Not in YAML 1.2 or JSON.
	 *
	 * DEVIATION(yaml1.1, pyyaml): Sexagesimal floats are not
	 * implemented in the C library. All return strings instead of
	 * the expected float values.
	 */
	{ "1:30.5",       ESTR,               ESTR,              ESTR,              ESTR },
	{ "190:20:30.15", ESTR,               ESTR,              ESTR,              ESTR },
	{ "-1:30.5",      ESTR,               ESTR,              ESTR,              ESTR },
};

START_TEST(scalar_float_sexagesimal)
{
	run_scalar_tests(float_sexagesimal_tests,
			 sizeof(float_sexagesimal_tests) / sizeof(float_sexagesimal_tests[0]),
			 "floats_sexagesimal");
}
END_TEST

/* =========================================================================
 * FLOATS - Infinity
 * ========================================================================= */

static const struct scalar_test float_infinity_tests[] = {
	/*
	 * YAML 1.2 Core Schema (spec 10.3.2): [-+]?(\.inf|\.Inf|\.INF)
	 * YAML 1.1: [-+]?\.(inf|Inf|INF)
	 * JSON: no infinity representation.
	 *
	 * All three casings (.inf, .Inf, .INF) are spec-compliant for
	 * both YAML 1.1 and 1.2.
	 */
	{ ".inf",   EINF_POS, EINF_POS, EINF_POS, ESTR },
	{ ".Inf",   EINF_POS, EINF_POS, EINF_POS, ESTR },
	{ ".INF",   EINF_POS, EINF_POS, EINF_POS, ESTR },
	{ "+.inf",  EINF_POS, EINF_POS, EINF_POS, ESTR },
	{ "+.Inf",  EINF_POS, EINF_POS, EINF_POS, ESTR },
	{ "-.inf",  EINF_NEG, EINF_NEG, EINF_NEG, ESTR },
	{ "-.Inf",  EINF_NEG, EINF_NEG, EINF_NEG, ESTR },

	/* Invalid casing - mixed case never recognized */
	{ ".iNf",   ESTR,     ESTR,     ESTR,     ESTR },
};

START_TEST(scalar_float_infinity)
{
	run_scalar_tests(float_infinity_tests,
			 sizeof(float_infinity_tests) / sizeof(float_infinity_tests[0]),
			 "floats_infinity");
}
END_TEST

/* =========================================================================
 * FLOATS - NaN
 * ========================================================================= */

static const struct scalar_test float_nan_tests[] = {
	/*
	 * YAML 1.2 Core Schema (spec 10.3.2): \.nan|\.NaN|\.NAN
	 * YAML 1.1: \.(nan|NaN|NAN)
	 * JSON: no NaN representation.
	 *
	 * Only three exact casings are specified: .nan, .NaN, .NAN
	 */
	{ ".nan",  ENAN, ENAN, ENAN, ESTR },
	{ ".NaN",  ENAN, ENAN, ENAN, ESTR },
	{ ".NAN",  ENAN, ENAN, ENAN, ESTR },

	/*
	 * ".Nan" - not in the spec's enumerated set (.nan|.NaN|.NAN).
	 * libfyaml correctly rejects this.
	 */
	{ ".Nan",  ESTR, ESTR, ESTR, ESTR },

	/* Mixed case - never NaN */
	{ ".nAn",  ESTR, ESTR, ESTR, ESTR },
};

START_TEST(scalar_float_nan)
{
	run_scalar_tests(float_nan_tests,
			 sizeof(float_nan_tests) / sizeof(float_nan_tests[0]),
			 "floats_nan");
}
END_TEST

/* =========================================================================
 * STRINGS - Plain (should remain strings in all schemas)
 * ========================================================================= */

static const struct scalar_test string_plain_tests[] = {
	{ "hello",       ESTR, ESTR, ESTR, ESTR },
	{ "hello world", ESTR, ESTR, ESTR, ESTR },
	{ "foo_bar",     ESTR, ESTR, ESTR, ESTR },
	{ "foo-bar",     ESTR, ESTR, ESTR, ESTR },
	{ "foo.bar",     ESTR, ESTR, ESTR, ESTR },
};

START_TEST(scalar_string_plain)
{
	run_scalar_tests(string_plain_tests,
			 sizeof(string_plain_tests) / sizeof(string_plain_tests[0]),
			 "strings_plain");
}
END_TEST

/* =========================================================================
 * EDGE CASES - Lookalikes that must remain strings
 * ========================================================================= */

static const struct scalar_test edge_case_tests[] = {
	/* Almost booleans */
	{ "truee",  ESTR, ESTR, ESTR, ESTR },
	{ "yess",   ESTR, ESTR, ESTR, ESTR },
	{ "noo",    ESTR, ESTR, ESTR, ESTR },

	/* Almost numbers */
	{ "1.2.3",  ESTR, ESTR, ESTR, ESTR },
	{ "1..0",   ESTR, ESTR, ESTR, ESTR },
	{ "++1",    ESTR, ESTR, ESTR, ESTR },
	{ "--1",    ESTR, ESTR, ESTR, ESTR },
	{ "+-1",    ESTR, ESTR, ESTR, ESTR },

	/* Almost hex */
	{ "0xZZ",   ESTR, ESTR, ESTR, ESTR },
	{ "0x",     ESTR, ESTR, ESTR, ESTR },

	/* Almost scientific */
	{ "1.0e",   ESTR, ESTR, ESTR, ESTR },
	{ "1.0e+",  ESTR, ESTR, ESTR, ESTR },
	{ "e3",     ESTR, ESTR, ESTR, ESTR },

	/* Almost timestamps */
	{ "2024-",    ESTR, ESTR, ESTR, ESTR },
	{ "2024-01",  ESTR, ESTR, ESTR, ESTR },
	{ "2024-01-", ESTR, ESTR, ESTR, ESTR },

	/* Version strings */
	{ "1.0.0",   ESTR, ESTR, ESTR, ESTR },
	{ "v1.2.3",  ESTR, ESTR, ESTR, ESTR },

	/* IP addresses */
	{ "192.168.1.1", ESTR, ESTR, ESTR, ESTR },

	/* Port number (is an int) */
	{ "8080",    EINT(8080), EINT(8080), EINT(8080), EINT(8080) },

	/* UUID */
	{ "550e8400-e29b-41d4-a716-446655440000", ESTR, ESTR, ESTR, ESTR },
};

START_TEST(scalar_edge_cases)
{
	run_scalar_tests(edge_case_tests,
			 sizeof(edge_case_tests) / sizeof(edge_case_tests[0]),
			 "edge_cases");
}
END_TEST

/* =========================================================================
 * SPECIAL VALUES
 * ========================================================================= */

static const struct scalar_test special_tests[] = {
	/* Merge key and value indicator - always strings as plain scalars */
	{ "<<",    ESTR, ESTR, ESTR, ESTR },
	{ "=",     ESTR, ESTR, ESTR, ESTR },
};

START_TEST(scalar_special)
{
	run_scalar_tests(special_tests,
			 sizeof(special_tests) / sizeof(special_tests[0]),
			 "special");
}
END_TEST

/* =========================================================================
 * JSON-specific: only null/true/false are special, everything else is
 * parsed by JSON rules (no hex, no octal, no bool variants)
 * ========================================================================= */

static const struct scalar_test json_specific_tests[] = {
	/*
	 * JSON (RFC 8259) value literals: null, true, false.
	 * JSON numbers: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
	 *   — no + prefix, no leading zeros (except bare "0"), no hex/octal.
	 */
	{ "null",  ENULL,      ENULL,      ENULL,      ENULL },
	{ "true",  EBOOL(true),  EBOOL(true),  EBOOL(true),  EBOOL(true) },
	{ "false", EBOOL(false), EBOOL(false), EBOOL(false), EBOOL(false) },
	{ "0",     EINT(0),    EINT(0),    EINT(0),    EINT(0) },
	{ "-1",    EINT(-1),   EINT(-1),   EINT(-1),   EINT(-1) },
	{ "1.5",   EFLOAT(1.5), EFLOAT(1.5), EFLOAT(1.5), EFLOAT(1.5) },
	/* JSON does not support + prefix */
	{ "+1",    EINT(1),    EINT(1),    EINT(1),    ESTR },
	/*
	 * DEVIATION(json): RFC 8259 forbids leading zeros in numbers
	 * (only bare "0" may start with 0). libfyaml parses "01" as
	 * integer 1 in JSON mode instead of treating it as a string.
	 */
	{ "01",    EINT(1),    EINT(1),    EINT(1),    EINT(1) },
};

START_TEST(scalar_json_specific)
{
	run_scalar_tests(json_specific_tests,
			 sizeof(json_specific_tests) / sizeof(json_specific_tests[0]),
			 "json_specific");
}
END_TEST

/* =========================================================================
 * EMIT - a string that reads back as another type is quoted, e.g. "0"
 * ========================================================================= */

static const char * const lookalike_strings[] = {
	"", "0", "1", "-1", "+1", "010", "0o17", "0x10", "0b101", "1_000",
	"1.5", "-1.5", ".5", "1e+3", "1.0e+3", ".inf", "-.Inf", "+.INF", ".nan", ".NaN",
	"~", "null", "Null", "NULL", "true", "True", "TRUE", "false", "False",
	"y", "Y", "n", "N", "yes", "Yes", "YES", "no", "No", "on", "On", "ON", "off", "OFF",
};

static const struct {
	const char *name;
	enum fy_op_emit_flags emit;
	enum fy_op_parse_flags parse;
} lookalike_modes[] = {
	{ "yaml-1.1",		FYOPEF_MODE_YAML_1_1 | FYOPEF_STYLE_BLOCK,	FYOPPF_MODE_YAML_1_1 },
	{ "yaml-1.1-flow",	FYOPEF_MODE_YAML_1_1 | FYOPEF_STYLE_FLOW,	FYOPPF_MODE_YAML_1_1 },
	{ "yaml-1.2",		FYOPEF_MODE_YAML_1_2 | FYOPEF_STYLE_BLOCK,	FYOPPF_MODE_YAML_1_2 },
	{ "yaml-1.2-flow",	FYOPEF_MODE_YAML_1_2 | FYOPEF_STYLE_FLOW,	FYOPPF_MODE_YAML_1_2 },
	{ "json",		FYOPEF_MODE_JSON,				FYOPPF_MODE_JSON },
};

static void lookalike_check(struct fy_generic_builder *gb, const char *str, fy_generic v)
{
	fy_generic_sized_string text;
	fy_generic emitted, parsed;
	enum fy_op_emit_flags emit;
	size_t i, ev;

	for (i = 0; i < sizeof(lookalike_modes) / sizeof(lookalike_modes[0]); i++) {
		for (ev = 0; ev < 2; ev++) {
			emit = lookalike_modes[i].emit | FYOPEF_DISABLE_DIRECTORY;
			if (ev)
				emit |= FYOPEF_EMIT_EVENTS;

			emitted = fy_emit(gb, v, emit, NULL);
			ck_assert(fy_is_string(emitted));
			text = fy_castp(&emitted, fy_szstr_empty);

			parsed = fy_parse(gb, text, lookalike_modes[i].parse | FYOPPF_DISABLE_DIRECTORY, NULL);
			ck_assert_msg(fy_generic_compare(v, parsed) == 0,
				      "\"%s\" %s%s: type changed on the round trip:\n%.*s",
				      str, lookalike_modes[i].name, ev ? "/events" : "",
				      (int)text.size, text.data);
		}
	}
}

START_TEST(scalar_emit_quotes_lookalikes)
{
	struct fy_generic_builder *gb;
	fy_generic_indirect gi;
	fy_generic v, vplain;
	size_t i;

	gb = fy_generic_builder_create(NULL);
	ck_assert_ptr_nonnull(gb);

	for (i = 0; i < sizeof(lookalike_strings) / sizeof(lookalike_strings[0]); i++) {
		v = fy_value(gb, lookalike_strings[i]);
		ck_assert(fy_is_string(v));

		lookalike_check(gb, lookalike_strings[i], v);
		lookalike_check(gb, lookalike_strings[i], fy_mapping(gb, "k", v));
		lookalike_check(gb, lookalike_strings[i], fy_mapping(gb, v, "v"));
		lookalike_check(gb, lookalike_strings[i], fy_sequence(gb, v, v));

		/* asking for plain doesn't help, it is still quoted */
		fy_generic_indirect_reset(&gi);
		gi.flags = FYGIF_VALUE | FYGIF_STYLE;
		gi.value = v;
		gi.style.v = fy_generic_in_place_unsigned_int((unsigned int)FYSS_PLAIN);
		vplain = fy_gb_indirect_create(gb, &gi);
		ck_assert(fy_generic_is_valid(vplain));
		lookalike_check(gb, lookalike_strings[i], fy_mapping(gb, "k", vplain));
	}

	fy_generic_builder_destroy(gb);
}
END_TEST

/* strings that can't be read as anything else stay plain */
START_TEST(scalar_emit_plain_strings)
{
	static const char * const strs[] = {
		"abc", "1:20", "=", "<<", "0abc", "1.2.3", "nope", "yess", "truex", ".",
		"+", "0x", "1e3x", "~~", "Off-road",
	};
	struct fy_generic_builder *gb;
	fy_generic_sized_string text;
	fy_generic emitted;
	char expect[64];
	size_t i, ev;

	gb = fy_generic_builder_create(NULL);
	ck_assert_ptr_nonnull(gb);

	for (i = 0; i < sizeof(strs) / sizeof(strs[0]); i++) {
		for (ev = 0; ev < 2; ev++) {
			emitted = fy_emit(gb, fy_mapping(gb, "k", strs[i]),
					  FYOPEF_DISABLE_DIRECTORY | FYOPEF_STYLE_BLOCK |
					  (ev ? FYOPEF_EMIT_EVENTS : 0), NULL);
			ck_assert(fy_is_string(emitted));
			text = fy_castp(&emitted, fy_szstr_empty);
			snprintf(expect, sizeof(expect), "k: %s\n", strs[i]);
			ck_assert_msg(text.size == strlen(expect) && !memcmp(text.data, expect, text.size),
				      "\"%s\"%s: expected plain, got:\n%.*s",
				      strs[i], ev ? " (events)" : "", (int)text.size, text.data);
		}
	}

	fy_generic_builder_destroy(gb);
}
END_TEST

/* in failsafe everything is a string, so "0" and "null" stay plain */
START_TEST(scalar_emit_failsafe_plain)
{
	static const char yaml[] = "a: 0\nb: null\nc: yes\nd: x\n";
	struct fy_generic_builder *gb;
	fy_generic_sized_string in = { yaml, sizeof(yaml) - 1 }, text;
	fy_generic parsed, emitted;
	size_t ev;

	gb = fy_generic_builder_create(NULL);
	ck_assert_ptr_nonnull(gb);

	for (ev = 0; ev < 2; ev++) {
		fy_gb_set_schema(gb, FYGS_YAML1_2_FAILSAFE);
		parsed = fy_parse(gb, in, 0, NULL);
		ck_assert(fy_generic_is_valid(parsed));

		emitted = fy_emit(gb, parsed, ev ? FYOPEF_EMIT_EVENTS : 0, NULL);
		ck_assert(fy_is_string(emitted));
		text = fy_castp(&emitted, fy_szstr_empty);
		ck_assert_msg(text.size == in.size && !memcmp(text.data, yaml, in.size),
			      "failsafe%s: expected plain, got:\n%.*s",
			      ev ? " (events)" : "", (int)text.size, text.data);
	}

	fy_generic_builder_destroy(gb);
}
END_TEST

/* the schema decides, e.g. "yes" is only a bool in yaml 1.1 */
START_TEST(scalar_plain_string_changes_type)
{
	ck_assert(fy_generic_plain_string_changes_type(FYGS_YAML1_1, "yes", FY_NT));
	ck_assert(!fy_generic_plain_string_changes_type(FYGS_YAML1_2_CORE, "yes", FY_NT));
	ck_assert(fy_generic_plain_string_changes_type(FYGS_AUTO, "yes", FY_NT));
	ck_assert(fy_generic_plain_string_changes_type(FYGS_YAML1_2_CORE, "0o17", FY_NT));
	ck_assert(!fy_generic_plain_string_changes_type(FYGS_YAML1_1, "0o17", FY_NT));
	ck_assert(fy_generic_plain_string_changes_type(FYGS_AUTO, "0o17", FY_NT));
	ck_assert(!fy_generic_plain_string_changes_type(FYGS_YAML1_2_FAILSAFE, "0", FY_NT));
	ck_assert(!fy_generic_plain_string_changes_type(FYGS_YAML1_1_FAILSAFE, "yes", FY_NT));
	ck_assert(fy_generic_plain_string_changes_type(FYGS_JSON, "null", FY_NT));
	ck_assert(!fy_generic_plain_string_changes_type(FYGS_JSON, "~", FY_NT));
	ck_assert(fy_generic_plain_string_changes_type(FYGS_AUTO, "", 0));
	ck_assert(!fy_generic_plain_string_changes_type(FYGS_AUTO, "abc", FY_NT));
	ck_assert(!fy_generic_plain_string_changes_type(FYGS_AUTO, "nope", FY_NT));

	ck_assert_int_eq(fy_generic_schema_plain_type(FYGS_YAML1_2_CORE, "0x10", FY_NT), FYGT_INT);
	ck_assert_int_eq(fy_generic_schema_plain_type(FYGS_YAML1_2_CORE, ".inf", FY_NT), FYGT_FLOAT);
	ck_assert_int_eq(fy_generic_schema_plain_type(FYGS_YAML1_1, "off", FY_NT), FYGT_BOOL);
	ck_assert_int_eq(fy_generic_schema_plain_type(FYGS_YAML1_2_CORE, "~", FY_NT), FYGT_NULL);
	ck_assert_int_eq(fy_generic_schema_plain_type(FYGS_YAML1_2_CORE, "x", FY_NT), FYGT_STRING);
}
END_TEST

/* =========================================================================
 * Register all scalar tests
 * ========================================================================= */

void libfyaml_case_generic_scalars(struct fy_check_suite *cs)
{
	struct fy_check_testcase *ctc;

	ctc = fy_check_suite_add_test_case(cs, "generic-scalars");

	/* booleans */
	fy_check_testcase_add_test(ctc, scalar_booleans);

	/* nulls */
	fy_check_testcase_add_test(ctc, scalar_nulls);

	/* integers */
	fy_check_testcase_add_test(ctc, scalar_int_decimal);
	fy_check_testcase_add_test(ctc, scalar_int_octal);
	fy_check_testcase_add_test(ctc, scalar_int_octal_0o);
	fy_check_testcase_add_test(ctc, scalar_int_hex);
	fy_check_testcase_add_test(ctc, scalar_int_binary);
	fy_check_testcase_add_test(ctc, scalar_int_underscore);
	fy_check_testcase_add_test(ctc, scalar_int_sexagesimal);

	/* floats */
	fy_check_testcase_add_test(ctc, scalar_float_basic);
	fy_check_testcase_add_test(ctc, scalar_float_dot_prefix);
	fy_check_testcase_add_test(ctc, scalar_float_trailing_dot);
	fy_check_testcase_add_test(ctc, scalar_float_scientific);
	fy_check_testcase_add_test(ctc, scalar_float_underscore);
	fy_check_testcase_add_test(ctc, scalar_float_sexagesimal);
	fy_check_testcase_add_test(ctc, scalar_float_infinity);
	fy_check_testcase_add_test(ctc, scalar_float_nan);

	/* strings */
	fy_check_testcase_add_test(ctc, scalar_string_plain);

	/* edge cases */
	fy_check_testcase_add_test(ctc, scalar_edge_cases);

	/* special values */
	fy_check_testcase_add_test(ctc, scalar_special);

	/* JSON-specific behavior */
	fy_check_testcase_add_test(ctc, scalar_json_specific);

	/* emitted strings that look like other types */
	fy_check_testcase_add_test(ctc, scalar_emit_quotes_lookalikes);
	fy_check_testcase_add_test(ctc, scalar_emit_plain_strings);
	fy_check_testcase_add_test(ctc, scalar_emit_failsafe_plain);
	fy_check_testcase_add_test(ctc, scalar_plain_string_changes_type);
}

FY_DIAG_POP
