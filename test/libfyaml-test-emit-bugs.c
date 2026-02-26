/*
 * libfyaml-test-emit-bugs.c - emitter round-trip bug tests
 *
 * Each test emits events with a specific scalar style, parses the
 * output back, and verifies that the scalar value survives the
 * round-trip.  All failures here are C emitter bugs.
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
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

#include <check.h>

#include <libfyaml.h>

#include "fy-utf8.h"
#include "fy-event.h"
#include "fy-check.h"

/* ── helpers ─────────────────────────────────────────────────────── */

#undef EMIT_OR_FAIL
#define EMIT_OR_FAIL(_fye) \
	do { \
		struct fy_event *__fye = (_fye); \
		char *__text; \
		int _rc; \
		\
		__text = fy_event_to_string(__fye); \
		ck_assert_ptr_ne(__text, NULL); \
		fprintf(stderr, "gen> %s\n", __text); \
		free(__text); \
		_rc = fy_emit_event(emit, __fye); \
		ck_assert_int_eq(_rc, 0); \
	} while (0)

/*
 * Emit: stream-start, doc-start, mapping-start(block),
 *       scalar("key",plain), scalar(value,style),
 *       mapping-end, doc-end, stream-end
 *
 * Then parse the output back and return the second scalar's text.
 * Caller must free *out_emitted and *out_got.
 * Returns 0 on success, -1 on emit/parse failure.
 */
static int emit_mapping_value(const char *value, size_t value_len,
			      enum fy_scalar_style style,
			      char **out_emitted, char **out_got, size_t *out_got_len)
{
	struct fy_emitter *emit;
	char *style_mark_text;
	char *yaml;
	size_t yaml_len;
	int rc;

	if (value && value_len == FY_NT)
		value_len = strlen(value);

	emit = fy_emit_to_string(FYECF_DEFAULT);
	if (!emit)
		return -1;

	switch (style) {
	case FYSS_PLAIN:
		style_mark_text = "plain";
		break;
	case FYSS_SINGLE_QUOTED:
		style_mark_text = "single-quoted";
		break;
	case FYSS_DOUBLE_QUOTED:
		style_mark_text = "double-quoted";
		break;
	case FYSS_LITERAL:
		style_mark_text = "literal";
		break;
	case FYSS_FOLDED:
		style_mark_text = "folded";
		break;
	default:
	case FYSS_ANY:
		style_mark_text = "any";
		break;
	}

	fprintf(stderr, "value: %s '%s\n", style_mark_text, fy_utf8_format_text_a(value, value_len, fyue_doublequote));

	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_STREAM_START));
	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_DOCUMENT_START, true, NULL, NULL));
	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_MAPPING_START, FYNS_BLOCK, NULL, NULL));
	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_SCALAR, FYSS_PLAIN, "key", FY_NT, NULL, NULL));
	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_SCALAR, style, value, value_len, NULL, NULL));
	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_MAPPING_END));
	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_DOCUMENT_END, true));
	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_STREAM_END));

	yaml = fy_emit_to_string_collect(emit, &yaml_len);
	fy_emitter_destroy(emit);
	if (!yaml)
		return -1;

	fprintf(stderr, "\nemitted: \"%s\"\n", fy_utf8_format_text_a(yaml, yaml_len, fyue_doublequote));

	if (out_emitted) {
		*out_emitted = malloc(yaml_len + 1);
		ck_assert_ptr_ne(*out_emitted, NULL);
		memcpy(*out_emitted, yaml, yaml_len + 1);
	}

	/* parse back */
	struct fy_parser *fyp;
	struct fy_event *fye;
	int scalar_idx = 0;
	char *got = NULL;
	size_t got_len = 0;

	fyp = fy_parser_create(NULL);
	if (!fyp) { free(yaml); return -1; }

	rc = fy_parser_set_string(fyp, yaml, yaml_len);
	if (rc) { fy_parser_destroy(fyp); free(yaml); return -1; }

	while ((fye = fy_parser_parse(fyp)) != NULL) {
		if (fye->type == FYET_SCALAR) {
			scalar_idx++;
			if (scalar_idx == 2) {
				const char *text;
				size_t tlen;
				text = fy_token_get_text(fye->scalar.value, &tlen);
				if (text) {
					got = malloc(tlen + 1);
					memcpy(got, text, tlen);
					got[tlen] = '\0';
					got_len = tlen;
				}
			}
		}
		fy_parser_event_free(fyp, fye);
	}

	fy_parser_destroy(fyp);
	free(yaml);

	if (out_got)
		*out_got = got;
	else
		free(got);
	if (out_got_len)
		*out_got_len = got_len;

	return 0;
}

/*
 * Emit: stream-start, doc-start, mapping-start(block),
 *       scalar(value,style), scalar("val",plain),
 *       mapping-end, doc-end, stream-end
 *
 * Then parse back and return the first scalar's text.
 */
static int emit_mapping_key(const char *value, size_t value_len,
			    enum fy_scalar_style style,
			    char **out_emitted, char **out_got, size_t *out_got_len)
{
	struct fy_emitter *emit;
	char *yaml;
	size_t yaml_len;
	int rc;

	emit = fy_emit_to_string(FYECF_DEFAULT);
	if (!emit)
		return -1;

	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_STREAM_START));
	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_DOCUMENT_START, true, NULL, NULL));
	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_MAPPING_START, FYNS_BLOCK, NULL, NULL));
	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_SCALAR, style, value, value_len, NULL, NULL));
	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_SCALAR, FYSS_PLAIN, "val", FY_NT, NULL, NULL));
	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_MAPPING_END));
	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_DOCUMENT_END, true));
	EMIT_OR_FAIL(fy_emit_event_create(emit, FYET_STREAM_END));

	yaml = fy_emit_to_string_collect(emit, &yaml_len);
	fy_emitter_destroy(emit);
	if (!yaml)
		return -1;

	if (out_emitted) {
		*out_emitted = malloc(yaml_len + 1);
		memcpy(*out_emitted, yaml, yaml_len + 1);
	}

	/* parse back — grab first scalar */
	struct fy_parser *fyp;
	struct fy_event *fye;
	char *got = NULL;
	size_t got_len = 0;

	fyp = fy_parser_create(NULL);
	if (!fyp) { free(yaml); return -1; }

	rc = fy_parser_set_string(fyp, yaml, yaml_len);
	if (rc) { fy_parser_destroy(fyp); free(yaml); return -1; }

	while ((fye = fy_parser_parse(fyp)) != NULL) {
		if (fye->type == FYET_SCALAR && !got) {
			const char *text;
			size_t tlen;
			text = fy_token_get_text(fye->scalar.value, &tlen);
			if (text) {
				got = malloc(tlen + 1);
				memcpy(got, text, tlen);
				got[tlen] = '\0';
				got_len = tlen;
			}
		}
		fy_parser_event_free(fyp, fye);
	}

	fy_parser_destroy(fyp);
	free(yaml);

	if (out_got)
		*out_got = got;
	else
		free(got);
	if (out_got_len)
		*out_got_len = got_len;

	return 0;
}

/* convenience: check mapping-value round-trip with memcmp */
#define ASSERT_MAPPING_VALUE_RT(val, val_len, style) do { \
	char *_emitted = NULL, *_got = NULL; \
	size_t _got_len = 0; \
	int _rc = emit_mapping_value((val), (val_len), (style), \
				     &_emitted, &_got, &_got_len); \
	ck_assert_msg(_rc == 0, "emit/parse failed; emitted=%s", \
		      _emitted ? _emitted : "(null)"); \
	ck_assert_msg(_got != NULL, "no scalar parsed back; emitted=%s", \
		      _emitted ? _emitted : "(null)"); \
	ck_assert_msg(_got_len == (size_t)(val_len == FY_NT ? strlen(val) : (size_t)(val_len)) && \
		      memcmp(_got, (val), _got_len) == 0, \
		      "round-trip mismatch: expected %zu bytes, got %zu; emitted=\n%s", \
		      (size_t)(val_len == FY_NT ? strlen(val) : (size_t)(val_len)), \
		      _got_len, _emitted ? _emitted : "(null)"); \
	free(_emitted); free(_got); \
} while (0)

#define ASSERT_MAPPING_KEY_RT(val, val_len, style) do { \
	char *_emitted = NULL, *_got = NULL; \
	size_t _got_len = 0; \
	int _rc = emit_mapping_key((val), (val_len), (style), \
				   &_emitted, &_got, &_got_len); \
	ck_assert_msg(_rc == 0, "emit/parse failed; emitted=%s", \
		      _emitted ? _emitted : "(null)"); \
	ck_assert_msg(_got != NULL, "no scalar parsed back; emitted=%s", \
		      _emitted ? _emitted : "(null)"); \
	ck_assert_msg(_got_len == (size_t)(val_len == FY_NT ? strlen(val) : (size_t)(val_len)) && \
		      memcmp(_got, (val), _got_len) == 0, \
		      "round-trip mismatch: expected %zu bytes, got %zu; emitted=\n%s", \
		      (size_t)(val_len == FY_NT ? strlen(val) : (size_t)(val_len)), \
		      _got_len, _emitted ? _emitted : "(null)"); \
	free(_emitted); free(_got); \
} while (0)

/*
 * Emit events using fy_emit_to_string(), parse back, count events.
 * Returns emitted string in *out_yaml (caller frees), event count in *out_count.
 */
static int emit_and_count_events(struct fy_emitter *emit,
				 char **out_yaml, int *out_count)
{
	char *yaml;
	char *text;
	size_t yaml_len;
	int rc;

	yaml = fy_emit_to_string_collect(emit, &yaml_len);
	fy_emitter_destroy(emit);
	if (!yaml)
		return -1;

	if (out_yaml) {
		*out_yaml = malloc(yaml_len + 1);
		memcpy(*out_yaml, yaml, yaml_len + 1);
	}

	struct fy_parser *fyp = fy_parser_create(NULL);
	if (!fyp) { free(yaml); return -1; }

	rc = fy_parser_set_string(fyp, yaml, yaml_len);
	if (rc) { fy_parser_destroy(fyp); free(yaml); return -1; }

	int count = 0;
	struct fy_event *fye;
	fprintf(stderr, "%s: dump and count events:\n", __func__);
	while ((fye = fy_parser_parse(fyp)) != NULL) {
		count++;
		text = fy_event_to_string(fye);
		ck_assert_ptr_ne(text, NULL);
		fprintf(stderr, "  %s\n", text);
		free(text);
		fy_parser_event_free(fyp, fye);
	}

	fy_parser_destroy(fyp);
	free(yaml);

	if (out_count)
		*out_count = count;
	return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 * Bug 1: Plain style drops trailing newline
 *
 * Plain scalars cannot represent trailing newlines — the emitter
 * should fall back to a quoted or block style but doesn't.
 * ═══════════════════════════════════════════════════════════════════ */

START_TEST(emit_bug_plain_drops_trailing_newline)
{
	ASSERT_MAPPING_VALUE_RT("text\n", FY_NT, FYSS_PLAIN);
}
END_TEST

START_TEST(emit_bug_plain_drops_trailing_newline_multiline)
{
	ASSERT_MAPPING_VALUE_RT("line1\nline2\n", FY_NT, FYSS_PLAIN);
}
END_TEST

/* ═══════════════════════════════════════════════════════════════════
 * Bug 2: Plain style drops leading whitespace
 * ═══════════════════════════════════════════════════════════════════ */

START_TEST(emit_bug_plain_drops_leading_space)
{
	ASSERT_MAPPING_VALUE_RT(" leading", FY_NT, FYSS_PLAIN);
}
END_TEST

START_TEST(emit_bug_plain_drops_leading_spaces)
{
	ASSERT_MAPPING_VALUE_RT("  two spaces", FY_NT, FYSS_PLAIN);
}
END_TEST

/* ═══════════════════════════════════════════════════════════════════
 * Bug 3: Plain style drops trailing whitespace
 * ═══════════════════════════════════════════════════════════════════ */

START_TEST(emit_bug_plain_drops_trailing_space)
{
	ASSERT_MAPPING_VALUE_RT("trailing ", FY_NT, FYSS_PLAIN);
}
END_TEST

START_TEST(emit_bug_plain_drops_trailing_spaces)
{
	ASSERT_MAPPING_VALUE_RT("two  ", FY_NT, FYSS_PLAIN);
}
END_TEST

/* ═══════════════════════════════════════════════════════════════════
 * Bug 4: Plain style drops leading newlines
 * ═══════════════════════════════════════════════════════════════════ */

START_TEST(emit_bug_plain_drops_leading_newlines)
{
	ASSERT_MAPPING_VALUE_RT("\n\ntext\n", FY_NT, FYSS_PLAIN);
}
END_TEST

START_TEST(emit_bug_plain_drops_single_leading_newline)
{
	ASSERT_MAPPING_VALUE_RT("\ntext", FY_NT, FYSS_PLAIN);
}
END_TEST

/* ═══════════════════════════════════════════════════════════════════
 * Bug 5: Plain style doesn't escape comment indicators
 *
 * '#' at start or ' #' mid-string are parsed as comments.
 * ═══════════════════════════════════════════════════════════════════ */

START_TEST(emit_bug_plain_hash_start)
{
	ASSERT_MAPPING_VALUE_RT("# comment-like", FY_NT, FYSS_PLAIN);
}
END_TEST

START_TEST(emit_bug_plain_inline_hash)
{
	ASSERT_MAPPING_VALUE_RT("text # rest", FY_NT, FYSS_PLAIN);
}
END_TEST

/* ═══════════════════════════════════════════════════════════════════
 * Bug 6: Single-quoted style loses indentation in multiline
 * ═══════════════════════════════════════════════════════════════════ */

START_TEST(emit_bug_single_quoted_loses_indent)
{
	ASSERT_MAPPING_VALUE_RT("line1\n  indented\n", FY_NT, FYSS_SINGLE_QUOTED);
}
END_TEST

START_TEST(emit_bug_single_quoted_loses_bullet_indent)
{
	ASSERT_MAPPING_VALUE_RT("text\n\n  * bullet\n  * list\n\nend\n", FY_NT, FYSS_SINGLE_QUOTED);
}
END_TEST

START_TEST(emit_bug_single_quoted_loses_deep_indent)
{
	ASSERT_MAPPING_VALUE_RT("line1\n  two\n    four\nline4\n", FY_NT, FYSS_SINGLE_QUOTED);
}
END_TEST

/* ═══════════════════════════════════════════════════════════════════
 * Bug 7: Single-quoted style loses tabs
 * ═══════════════════════════════════════════════════════════════════ */

START_TEST(emit_bug_single_quoted_tab_multiline)
{
	ASSERT_MAPPING_VALUE_RT("text\n \tlines\n", FY_NT, FYSS_SINGLE_QUOTED);
}
END_TEST

START_TEST(emit_bug_single_quoted_leading_tab)
{
	ASSERT_MAPPING_VALUE_RT("\t\ndetected\n", FY_NT, FYSS_SINGLE_QUOTED);
}
END_TEST

/* ═══════════════════════════════════════════════════════════════════
 * Bug 8: Unicode line separators (U+2028/U+2029)
 *
 * The C emitter treats these as real line breaks inside block and
 * plain scalars, truncating or corrupting the content.
 *
 * Note: These pass in pure C round-trip because libfyaml's parser
 * also treats U+2028/U+2029 as line breaks consistently. The bug
 * manifests when interoperating with YAML 1.2 strict parsers
 * (e.g. PyYAML) that don't treat these as line breaks.
 * ═══════════════════════════════════════════════════════════════════ */

START_TEST(emit_bug_u2028_literal)
{
	ASSERT_MAPPING_VALUE_RT("text\xe2\x80\xa8more", FY_NT, FYSS_LITERAL);
}
END_TEST

START_TEST(emit_bug_u2028_folded)
{
	ASSERT_MAPPING_VALUE_RT("text\xe2\x80\xa8more", FY_NT, FYSS_FOLDED);
}
END_TEST

START_TEST(emit_bug_u2028_plain)
{
	ASSERT_MAPPING_VALUE_RT("text\xe2\x80\xa8more", FY_NT, FYSS_PLAIN);
}
END_TEST

START_TEST(emit_bug_u2029_folded)
{
	ASSERT_MAPPING_VALUE_RT("text\xe2\x80\xa9more", FY_NT, FYSS_FOLDED);
}
END_TEST

START_TEST(emit_bug_u2028_folded_trailing)
{
	/* U+2028 followed by newline and more content */
	ASSERT_MAPPING_VALUE_RT("trimmed\nspecific\xe2\x80\xa8\nnone", FY_NT, FYSS_FOLDED);
}
END_TEST

/* positive control: double-quoted handles U+2028 correctly */
START_TEST(emit_bug_u2028_double_quoted_ok)
{
	ASSERT_MAPPING_VALUE_RT("text\xe2\x80\xa8more", FY_NT, FYSS_DOUBLE_QUOTED);
}
END_TEST

/* ═══════════════════════════════════════════════════════════════════
 * Bug 9: NUL character (\x00) truncates block scalars
 *
 * NUL bytes pass through into block output and truncate on re-parse.
 * Double-quoted correctly emits \0 escape.
 * ═══════════════════════════════════════════════════════════════════ */

START_TEST(emit_bug_nul_literal)
{
	ASSERT_MAPPING_VALUE_RT("text\x00" "end", 8, FYSS_LITERAL);
}
END_TEST

START_TEST(emit_bug_nul_folded)
{
	ASSERT_MAPPING_VALUE_RT("text\x00" "end", 8, FYSS_FOLDED);
}
END_TEST

/* positive control */
START_TEST(emit_bug_nul_double_quoted_ok)
{
	ASSERT_MAPPING_VALUE_RT("text\x00" "end", 8, FYSS_DOUBLE_QUOTED);
}
END_TEST

#undef EMIT_EV
#define EMIT_EV(_fye) \
	do { \
		struct fy_event *__fye = (_fye); \
		char *__text; \
		int _rc; \
		\
		__text = fy_event_to_string(__fye); \
		ck_assert_ptr_ne(__text, NULL); \
		fprintf(stderr, "gen> %s\n", __text); \
		free(__text); \
		_rc = fy_emit_event(emit, __fye); \
		ck_assert_int_eq(_rc, 0); \
	} while (0)

/* ═══════════════════════════════════════════════════════════════════
 * Bug 10: Block scalars produce broken YAML structure
 *
 * Certain patterns cause the emitter to produce YAML that the parser
 * reads back as a different number of events.
 * ═══════════════════════════════════════════════════════════════════ */

START_TEST(emit_bug_literal_root_u2028_structure)
{
	/*
	 * Root scalar with U+2028 in literal style — the emitter writes
	 * U+2028 as a real line break, and the parser misinterprets it.
	 * Expected: 5 events. May get fewer.
	 *
	 * Note: passes in pure C round-trip (parser treats U+2028 as
	 * line break consistently) but fails with PyYAML's parser.
	 */
	struct fy_emitter *emit;
	char *yaml = NULL;
	int count = 0, rc;

	const char *val = "specific\xe2\x80\xa8trimmed\n\n\nas space";

	emit = fy_emit_to_string(FYECF_DEFAULT);
	ck_assert_ptr_ne(emit, NULL);

	EMIT_EV(fy_emit_event_create(emit, FYET_STREAM_START));
	EMIT_EV(fy_emit_event_create(emit, FYET_DOCUMENT_START, true, NULL, NULL));
	EMIT_EV(fy_emit_event_create(emit, FYET_SCALAR, FYSS_LITERAL, val, FY_NT, NULL, NULL));
	EMIT_EV(fy_emit_event_create(emit, FYET_DOCUMENT_END, true));
	EMIT_EV(fy_emit_event_create(emit, FYET_STREAM_END));

	rc = emit_and_count_events(emit, &yaml, &count);
	ck_assert_int_eq(rc, 0);
	ck_assert_msg(count == 5,
		      "expected 5 events, got %d; emitted=\n%s",
		      count, yaml ? yaml : "(null)");
	free(yaml);
}
END_TEST

START_TEST(emit_bug_literal_empty_in_sequence_structure)
{
	/*
	 * Empty string '' as literal in a sequence followed by a mapping.
	 * Expected: 13 events. Gets fewer due to parser confusion.
	 */
	struct fy_emitter *emit;
	char *yaml = NULL;
	int count = 0, rc;

	emit = fy_emit_to_string(FYECF_DEFAULT);
	ck_assert_ptr_ne(emit, NULL);

	EMIT_EV(fy_emit_event_create(emit, FYET_STREAM_START));
	EMIT_EV(fy_emit_event_create(emit, FYET_DOCUMENT_START, true, NULL, NULL));
	EMIT_EV(fy_emit_event_create(emit, FYET_SEQUENCE_START, FYNS_BLOCK, NULL, NULL));
	EMIT_EV(fy_emit_event_create(emit, FYET_SCALAR, FYSS_LITERAL, "", (size_t)0, NULL, NULL));
	EMIT_EV(fy_emit_event_create(emit, FYET_MAPPING_START, FYNS_BLOCK, NULL, NULL));
	EMIT_EV(fy_emit_event_create(emit, FYET_SCALAR, FYSS_LITERAL, "foo", FY_NT, NULL, NULL));
	EMIT_EV(fy_emit_event_create(emit, FYET_SCALAR, FYSS_LITERAL, "", (size_t)0, NULL, NULL));
	EMIT_EV(fy_emit_event_create(emit, FYET_SCALAR, FYSS_LITERAL, "", (size_t)0, NULL, NULL));
	EMIT_EV(fy_emit_event_create(emit, FYET_SCALAR, FYSS_LITERAL, "bar", FY_NT, NULL, NULL));
	EMIT_EV(fy_emit_event_create(emit, FYET_MAPPING_END));
	EMIT_EV(fy_emit_event_create(emit, FYET_SEQUENCE_END));
	EMIT_EV(fy_emit_event_create(emit, FYET_DOCUMENT_END, true));
	EMIT_EV(fy_emit_event_create(emit, FYET_STREAM_END));

	rc = emit_and_count_events(emit, &yaml, &count);
	ck_assert_int_eq(rc, 0);
	ck_assert_msg(count == 13,
		      "expected 13 events, got %d; emitted=\n%s",
		      count, yaml ? yaml : "(null)");
	free(yaml);
}
END_TEST

START_TEST(emit_bug_folded_root_u2028_structure)
{
	/* Same as literal but with folded style */
	struct fy_emitter *emit;
	char *yaml = NULL;
	int count = 0, rc;

	const char *val = "specific\xe2\x80\xa8trimmed\n\n\nas space";

	emit = fy_emit_to_string(FYECF_DEFAULT);
	ck_assert_ptr_ne(emit, NULL);

	EMIT_EV(fy_emit_event_create(emit, FYET_STREAM_START));
	EMIT_EV(fy_emit_event_create(emit, FYET_DOCUMENT_START, true, NULL, NULL));
	EMIT_EV(fy_emit_event_create(emit, FYET_SCALAR, FYSS_FOLDED, val, FY_NT, NULL, NULL));
	EMIT_EV(fy_emit_event_create(emit, FYET_DOCUMENT_END, true));
	EMIT_EV(fy_emit_event_create(emit, FYET_STREAM_END));

	rc = emit_and_count_events(emit, &yaml, &count);
	ck_assert_int_eq(rc, 0);
	ck_assert_msg(count == 5,
		      "expected 5 events, got %d; emitted=\n%s",
		      count, yaml ? yaml : "(null)");
	free(yaml);
}
END_TEST

/* ═══════════════════════════════════════════════════════════════════
 * Bug 11: Newline-only scalar ('\n') loses content
 *
 * '\n' emitted in literal/folded/plain round-trips to '' (empty).
 * ═══════════════════════════════════════════════════════════════════ */

START_TEST(emit_bug_newline_only_literal)
{
	ASSERT_MAPPING_VALUE_RT("\n", FY_NT, FYSS_LITERAL);
}
END_TEST

/* NOTE Folded is special, it doesn't work like normal values */

START_TEST(emit_bug_newline_only_plain)
{
	ASSERT_MAPPING_VALUE_RT("\n", FY_NT, FYSS_PLAIN);
}
END_TEST

/* ═══════════════════════════════════════════════════════════════════
 * Bug 12: Carriage return (\r) normalized to \n in block/plain
 *
 * The emitter should fall back to double-quoted style which can
 * represent \r via escape, but instead normalizes it.
 * ═══════════════════════════════════════════════════════════════════ */

START_TEST(emit_bug_cr_literal)
{
	ASSERT_MAPPING_VALUE_RT("a \r b", FY_NT, FYSS_LITERAL);
}
END_TEST

START_TEST(emit_bug_cr_folded)
{
	ASSERT_MAPPING_VALUE_RT("a \r b", FY_NT, FYSS_FOLDED);
}
END_TEST

START_TEST(emit_bug_cr_plain)
{
	ASSERT_MAPPING_VALUE_RT("a \r b", FY_NT, FYSS_PLAIN);
}
END_TEST

/* ═══════════════════════════════════════════════════════════════════
 * Bug 13: Plain multiline mapping key loses content
 *
 * Multi-line plain scalars as mapping keys can lose lines.
 * ═══════════════════════════════════════════════════════════════════ */

START_TEST(emit_bug_plain_multiline_key_hashbang)
{
	ASSERT_MAPPING_KEY_RT("#!/usr/bin/perl\nprint \"hi\";\n", FY_NT, FYSS_PLAIN);
}
END_TEST

START_TEST(emit_bug_unquoted_flow_comma)
{
	const char input[] = "- foo, bar, baz\n";
	struct fy_document *fyd = NULL;
	struct fy_parse_cfg cfg = {0};
	char *buf;

	cfg.flags = FYPCF_DEFAULT_PARSE;
	fyd = fy_document_build_from_string(&cfg, input, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	buf = fy_emit_document_to_string(fyd, FYECF_MODE_FLOW_ONELINE | FYECF_WIDTH_INF | FYECF_STRIP_LABELS | FYECF_STRIP_TAGS | FYECF_STRIP_DOC | FYECF_DOC_START_MARK_OFF);

	/* verify that is now quoted */
	ck_assert_str_eq(buf, "['foo, bar, baz']\n");

	free(buf);
	fy_document_destroy(fyd);
}
END_TEST

/* ── Bug 14: comment indent loss on block sequence in mapping ────── */

struct emit_bugs_collect_data {
	size_t alloc;
	size_t count;
	char *buf;
};

static int emit_bugs_collect_output(struct fy_emitter *emit, enum fy_emitter_write_type type,
				    const char *str, int len, void *userdata)
{
	struct emit_bugs_collect_data *data = userdata;
	char *newbuf;
	size_t alloc, need;

	need = data->count + len + 1;
	alloc = data->alloc;
	if (!alloc)
		alloc = 512;
	while (need > alloc)
		alloc <<= 1;

	if (alloc != data->alloc) {
		newbuf = realloc(data->buf, alloc);
		assert(newbuf);
		data->buf = newbuf;
		data->alloc = alloc;
	}
	memcpy(data->buf + data->count, str, len);
	data->count += len;
	data->buf[data->count] = '\0';

	return len;
}

/*
 * Round-trip with comment preservation and indented-seq-in-map.
 * Caller must free the returned string.
 */
static char *roundtrip_comments_indented_seq(const char *input)
{
	struct fy_parse_cfg pcfg = {
		.flags = FYPCF_DEFAULT_PARSE | FYPCF_PARSE_COMMENTS
	};
	struct fy_document *fyd;
	struct fy_emitter_xcfg xcfg;
	struct fy_emitter *emit;
	struct emit_bugs_collect_data data;
	int rc;

	fyd = fy_document_build_from_string(&pcfg, input, FY_NT);
	ck_assert_ptr_ne(fyd, NULL);

	memset(&data, 0, sizeof(data));
	memset(&xcfg, 0, sizeof(xcfg));
	xcfg.cfg.output = emit_bugs_collect_output;
	xcfg.cfg.userdata = &data;
	xcfg.cfg.flags = FYECF_MODE_ORIGINAL | FYECF_OUTPUT_COMMENTS
		       | FYECF_WIDTH_INF | FYECF_INDENT_2 | FYECF_EXTENDED_CFG;
	xcfg.xflags = FYEXCF_INDENTED_SEQ_IN_MAP;

	emit = fy_emitter_create(&xcfg.cfg);
	ck_assert_ptr_ne(emit, NULL);

	rc = fy_emit_document(emit, fyd);
	ck_assert_int_eq(rc, 0);

	fy_emitter_destroy(emit);
	fy_document_destroy(fyd);

	ck_assert_ptr_ne(data.buf, NULL);
	return data.buf;
}

START_TEST(emit_bug_comment_indent_seq_in_map_simple)
{
	const char input[] = "root:\n  # a comment\n  - item\n";
	char *output = roundtrip_comments_indented_seq(input);

	/* The comment must be at indent 2, same as "- item" */
	ck_assert_ptr_ne(strstr(output, "  # a comment\n"), NULL);
	/* Must NOT have the comment at indent 0 */
	ck_assert_ptr_eq(strstr(output, "\n# a comment\n"), NULL);

	free(output);
}
END_TEST

START_TEST(emit_bug_comment_indent_seq_in_map_nested)
{
	/* Mimics GitHub Actions steps structure */
	const char input[] =
		"jobs:\n"
		"  test:\n"
		"    steps:\n"
		"      # step comment\n"
		"      - name: foo\n";
	char *output = roundtrip_comments_indented_seq(input);

	/* Comment must be at indent 6 (6 spaces before #) */
	ck_assert_ptr_ne(strstr(output, "      # step comment\n"), NULL);
	/* Must NOT be at indent 4 (the mapping indent); anchor with newline */
	ck_assert_ptr_eq(strstr(output, "\n    # step comment\n"), NULL);

	free(output);
}
END_TEST

START_TEST(emit_bug_comment_indent_seq_in_map_multiline)
{
	const char input[] =
		"root:\n"
		"  # line 1\n"
		"  # line 2\n"
		"  - item\n";
	char *output = roundtrip_comments_indented_seq(input);

	ck_assert_ptr_ne(strstr(output, "  # line 1\n"), NULL);
	ck_assert_ptr_ne(strstr(output, "  # line 2\n"), NULL);

	free(output);
}
END_TEST

/* ── registration ────────────────────────────────────────────────── */

void libfyaml_case_emit_bugs(struct fy_check_suite *cs)
{
	struct fy_check_testcase *ctc;

	ctc = fy_check_suite_add_test_case(cs, "emit-bugs");

	/* Bug 1: plain drops trailing newline */
	fy_check_testcase_add_test(ctc, emit_bug_plain_drops_trailing_newline);
	fy_check_testcase_add_test(ctc, emit_bug_plain_drops_trailing_newline_multiline);

	/* Bug 2: plain drops leading space */
	fy_check_testcase_add_test(ctc, emit_bug_plain_drops_leading_space);
	fy_check_testcase_add_test(ctc, emit_bug_plain_drops_leading_spaces);

	/* Bug 3: plain drops trailing space */
	fy_check_testcase_add_test(ctc, emit_bug_plain_drops_trailing_space);
	fy_check_testcase_add_test(ctc, emit_bug_plain_drops_trailing_spaces);

	/* Bug 4: plain drops leading newlines */
	fy_check_testcase_add_test(ctc, emit_bug_plain_drops_leading_newlines);
	fy_check_testcase_add_test(ctc, emit_bug_plain_drops_single_leading_newline);

	/* Bug 5: plain doesn't escape comment indicators */
	fy_check_testcase_add_test(ctc, emit_bug_plain_hash_start);
	fy_check_testcase_add_test(ctc, emit_bug_plain_inline_hash);

	/* Bug 6: single-quoted loses indentation */
	fy_check_testcase_add_test(ctc, emit_bug_single_quoted_loses_indent);
	fy_check_testcase_add_test(ctc, emit_bug_single_quoted_loses_bullet_indent);
	fy_check_testcase_add_test(ctc, emit_bug_single_quoted_loses_deep_indent);

	/* Bug 7: single-quoted loses tabs */
	fy_check_testcase_add_test(ctc, emit_bug_single_quoted_tab_multiline);
	fy_check_testcase_add_test(ctc, emit_bug_single_quoted_leading_tab);

	/* Bug 8: unicode line separators */
	fy_check_testcase_add_test(ctc, emit_bug_u2028_literal);
	fy_check_testcase_add_test(ctc, emit_bug_u2028_folded);
	fy_check_testcase_add_test(ctc, emit_bug_u2028_plain);
	fy_check_testcase_add_test(ctc, emit_bug_u2029_folded);
	fy_check_testcase_add_test(ctc, emit_bug_u2028_folded_trailing);
	fy_check_testcase_add_test(ctc, emit_bug_u2028_double_quoted_ok);

	/* Bug 9: NUL truncation */
	fy_check_testcase_add_test(ctc, emit_bug_nul_literal);
	fy_check_testcase_add_test(ctc, emit_bug_nul_folded);
	fy_check_testcase_add_test(ctc, emit_bug_nul_double_quoted_ok);

	/* Bug 10: block scalar broken structure */
	fy_check_testcase_add_test(ctc, emit_bug_literal_root_u2028_structure);
	fy_check_testcase_add_test(ctc, emit_bug_literal_empty_in_sequence_structure);
	fy_check_testcase_add_test(ctc, emit_bug_folded_root_u2028_structure);

	/* Bug 11: newline-only scalar */
	fy_check_testcase_add_test(ctc, emit_bug_newline_only_literal);
	fy_check_testcase_add_test(ctc, emit_bug_newline_only_plain);

	/* Bug 12: carriage return */
	fy_check_testcase_add_test(ctc, emit_bug_cr_literal);
	fy_check_testcase_add_test(ctc, emit_bug_cr_folded);
	fy_check_testcase_add_test(ctc, emit_bug_cr_plain);

	/* Bug 13: plain multiline key */
	fy_check_testcase_add_test(ctc, emit_bug_plain_multiline_key_hashbang);

	/* Bug 14: comment indent loss on block sequence in mapping */
	fy_check_testcase_add_test(ctc, emit_bug_comment_indent_seq_in_map_simple);
	fy_check_testcase_add_test(ctc, emit_bug_comment_indent_seq_in_map_nested);
	fy_check_testcase_add_test(ctc, emit_bug_comment_indent_seq_in_map_multiline);

	/* other kind of emit bugs */
	fy_check_testcase_add_test(ctc, emit_bug_unquoted_flow_comma);
}
