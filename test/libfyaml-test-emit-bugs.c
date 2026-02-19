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

/* ═══════════════════════════════════════════════════════════════════
 * Bug 14 (PARSER): NEL (U+0085) in block scalar trailing break
 *                  produces spurious null byte
 *
 * In YAML 1.1 mode, U+0085 (NEL) is a line break character.
 * When a block scalar's trailing line break is NEL, the parser
 * normalizes it to \n but appends a spurious \0 null byte.
 * This affects clip (default |) and keep (|+) chomping.
 * Strip (|-) is unaffected because it removes the break entirely.
 *
 * Root cause: NEL is 2 bytes in UTF-8 (\xc2\x85) but normalizes
 * to 1 byte (\n). The parser appears to account for the 2-byte
 * input length, leaving a stale null byte in the output buffer.
 *
 * Reproduces PyYAML spec-09-22 and spec-09-23 test failures.
 * ═══════════════════════════════════════════════════════════════════ */

/*
 * Parse a YAML string in YAML 1.1 mode, extract the nth scalar value
 * (0-indexed). Returns allocated string in *out_val, length in *out_len.
 * Returns 0 on success, -1 on failure.
 */
static int parse_yaml11_get_scalar(const char *input, size_t input_len,
				   int scalar_index,
				   char **out_val, size_t *out_len)
{
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_DEFAULT_VERSION_1_1 |
			 FYPCF_SLOPPY_FLOW_INDENTATION |
			 FYPCF_ALLOW_DUPLICATE_KEYS,
	};
	struct fy_parser *fyp;
	struct fy_event *fye;
	int idx = 0;
	char *val = NULL;
	size_t val_len = 0;
	int rc;

	fyp = fy_parser_create(&cfg);
	if (!fyp)
		return -1;

	rc = fy_parser_set_string(fyp, input, input_len);
	if (rc) {
		fy_parser_destroy(fyp);
		return -1;
	}

	while ((fye = fy_parser_parse(fyp)) != NULL) {
		if (fye->type == FYET_SCALAR) {
			if (idx == scalar_index) {
				const char *text;
				size_t tlen;
				text = fy_token_get_text(fye->scalar.value, &tlen);
				if (text) {
					val = malloc(tlen + 1);
					memcpy(val, text, tlen);
					val[tlen] = '\0';
					val_len = tlen;
				}
			}
			idx++;
		}
		fy_parser_event_free(fyp, fye);
	}

	fy_parser_destroy(fyp);

	if (!val)
		return -1;

	if (out_val)
		*out_val = val;
	else
		free(val);
	if (out_len)
		*out_len = val_len;
	return 0;
}

/* clip chomping (|) with NEL as trailing break: expect "text\n", not "text\n\0" */
START_TEST(parse_bug_nel_clip_chomping)
{
	/* "x: |\n  text<NEL>"  where NEL = U+0085 = \xc2\x85 */
	const char input[] = "x: |\n  text\xc2\x85";
	char *val = NULL;
	size_t val_len = 0;
	int rc;

	rc = parse_yaml11_get_scalar(input, sizeof(input) - 1, 1, &val, &val_len);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(val, NULL);

	fprintf(stderr, "clip+NEL: got %zu bytes, repr=", val_len);
	for (size_t i = 0; i < val_len; i++)
		fprintf(stderr, "\\x%02x", (unsigned char)val[i]);
	fprintf(stderr, "\n");

	/* expected: "text\n" = 5 bytes */
	ck_assert_msg(val_len == 5 && memcmp(val, "text\n", 5) == 0,
		      "clip+NEL: expected \"text\\n\" (5 bytes), got %zu bytes", val_len);
	free(val);
}
END_TEST

/* keep chomping (|+) with NEL as trailing break: expect "text\n", not "text\n\0" */
START_TEST(parse_bug_nel_keep_chomping)
{
	const char input[] = "x: |+\n  text\xc2\x85";
	char *val = NULL;
	size_t val_len = 0;
	int rc;

	rc = parse_yaml11_get_scalar(input, sizeof(input) - 1, 1, &val, &val_len);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(val, NULL);

	fprintf(stderr, "keep+NEL: got %zu bytes, repr=", val_len);
	for (size_t i = 0; i < val_len; i++)
		fprintf(stderr, "\\x%02x", (unsigned char)val[i]);
	fprintf(stderr, "\n");

	/* expected: "text\n" = 5 bytes */
	ck_assert_msg(val_len == 5 && memcmp(val, "text\n", 5) == 0,
		      "keep+NEL: expected \"text\\n\" (5 bytes), got %zu bytes", val_len);
	free(val);
}
END_TEST

/* strip chomping (|-) with NEL should work fine: expect "text" */
START_TEST(parse_bug_nel_strip_chomping_ok)
{
	const char input[] = "x: |-\n  text\xc2\x85";
	char *val = NULL;
	size_t val_len = 0;
	int rc;

	rc = parse_yaml11_get_scalar(input, sizeof(input) - 1, 1, &val, &val_len);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(val, NULL);

	fprintf(stderr, "strip+NEL: got %zu bytes, repr=", val_len);
	for (size_t i = 0; i < val_len; i++)
		fprintf(stderr, "\\x%02x", (unsigned char)val[i]);
	fprintf(stderr, "\n");

	/* expected: "text" = 4 bytes */
	ck_assert_msg(val_len == 4 && memcmp(val, "text", 4) == 0,
		      "strip+NEL: expected \"text\" (4 bytes), got %zu bytes", val_len);
	free(val);
}
END_TEST

/* spec-09-22 full test: strip/clip/keep with mixed NEL/LS/PS line breaks */
START_TEST(parse_bug_nel_spec_09_22)
{
	/* strip: |-\n  text<PS>clip: |\n  text<NEL>keep: |+\n  text<LS> */
	const char input[] =
		"strip: |-\n  text\xe2\x80\xa9"
		"clip: |\n  text\xc2\x85"
		"keep: |+\n  text\xe2\x80\xa8";
	char *val = NULL;
	size_t val_len = 0;
	int rc;

	/* scalar[0]="strip", scalar[1]=strip value, scalar[2]="clip",
	 * scalar[3]=clip value, scalar[4]="keep", scalar[5]=keep value */

	/* clip value (scalar index 3): should be "text\n" */
	rc = parse_yaml11_get_scalar(input, sizeof(input) - 1, 3, &val, &val_len);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(val, NULL);

	fprintf(stderr, "spec-09-22 clip: got %zu bytes, repr=", val_len);
	for (size_t i = 0; i < val_len; i++)
		fprintf(stderr, "\\x%02x", (unsigned char)val[i]);
	fprintf(stderr, "\n");

	ck_assert_msg(val_len == 5 && memcmp(val, "text\n", 5) == 0,
		      "spec-09-22 clip: expected \"text\\n\" (5 bytes), got %zu bytes", val_len);
	free(val);
}
END_TEST

/* ── Bug 15 (PARSER): Invalid UTF-8 and NUL in input stream ────── */

/*
 * Helper: try to parse input; return 0 if parse succeeds (all events
 * consumed without error), -1 if the parser reports an error.
 */
static int try_parse(const char *input, size_t input_len, unsigned int extra_flags)
{
	struct fy_parse_cfg cfg = {
		.flags = FYPCF_DEFAULT_DOC | extra_flags,
	};
	struct fy_parser *fyp;
	struct fy_event *fye;
	int got_error = 0;

	fyp = fy_parser_create(&cfg);
	if (!fyp)
		return -1;

	if (fy_parser_set_string(fyp, input, input_len)) {
		fy_parser_destroy(fyp);
		return -1;
	}

	while ((fye = fy_parser_parse(fyp)) != NULL)
		fy_parser_event_free(fyp, fye);

	/* If the parser had an error, fy_parser_get_stream_error() returns true */
	got_error = fy_parser_get_stream_error(fyp);
	fy_parser_destroy(fyp);

	return got_error ? -1 : 0;
}

/* NUL byte (\x00) embedded in a scalar value */
START_TEST(parse_bug_nul_in_stream)
{
	/* "foo: ba\x00r\n" — NUL byte in the middle of a scalar */
	const char input[] = "foo: ba\x00r\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "NUL byte in stream: parser should reject but accepted");
}
END_TEST

/* NUL byte in a YAML comment */
START_TEST(parse_bug_nul_in_comment)
{
	/* "# comment with \x00 null\nfoo: bar\n" */
	const char input[] = "# comment with \x00 null\nfoo: bar\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "NUL byte in comment: parser should reject but accepted");
}
END_TEST

/* Partial (truncated) UTF-8 sequence: 2-byte lead byte without continuation */
START_TEST(parse_bug_partial_utf8_2byte)
{
	/* "foo: abc\xc3\n" — \xc3 is a 2-byte lead, but next byte is \n not 0x80..0xBF */
	const char input[] = "foo: abc\xc3\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "Partial 2-byte UTF-8: parser should reject but accepted");
}
END_TEST

/* Partial (truncated) UTF-8 sequence: 3-byte lead with only 1 continuation */
START_TEST(parse_bug_partial_utf8_3byte)
{
	/* "foo: abc\xe2\x80\n" — \xe2 expects 2 continuation bytes, got only 1 */
	const char input[] = "foo: abc\xe2\x80\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "Partial 3-byte UTF-8: parser should reject but accepted");
}
END_TEST

/* Partial (truncated) UTF-8 sequence: 4-byte lead with only 2 continuations */
START_TEST(parse_bug_partial_utf8_4byte)
{
	/* "foo: \xf0\x9f\x98\n" — \xf0 expects 3 continuation bytes, got only 2 */
	const char input[] = "foo: \xf0\x9f\x98\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "Partial 4-byte UTF-8: parser should reject but accepted");
}
END_TEST

/* Invalid UTF-8: continuation byte without a lead byte */
START_TEST(parse_bug_invalid_utf8_lone_continuation)
{
	/* "foo: abc\x80xyz\n" — \x80 is a continuation byte, not a valid lead */
	const char input[] = "foo: abc\x80xyz\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "Lone continuation byte: parser should reject but accepted");
}
END_TEST

/* Invalid UTF-8: overlong encoding of '/' (U+002F) as 2 bytes */
START_TEST(parse_bug_invalid_utf8_overlong)
{
	/* "foo: \xc0\xaf\n" — overlong encoding of U+002F '/' */
	const char input[] = "foo: \xc0\xaf\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "Overlong UTF-8: parser should reject but accepted");
}
END_TEST

/* Invalid UTF-8: byte 0xFE is never valid in UTF-8 */
START_TEST(parse_bug_invalid_utf8_fe)
{
	/* "foo: \xfe\n" — 0xFE is not a valid UTF-8 byte */
	const char input[] = "foo: \xfe\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "0xFE byte: parser should reject but accepted");
}
END_TEST

/* Invalid UTF-8: byte 0xFF is never valid in UTF-8 */
START_TEST(parse_bug_invalid_utf8_ff)
{
	/* "foo: \xff\n" — 0xFF is not a valid UTF-8 byte */
	const char input[] = "foo: \xff\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "0xFF byte: parser should reject but accepted");
}
END_TEST

/* Valid UTF-8 should still parse OK (sanity check) */
START_TEST(parse_valid_utf8_ok)
{
	/* "foo: café ☕ 🎉\n" — all valid UTF-8 */
	const char input[] = "foo: caf\xc3\xa9 \xe2\x98\x95 \xf0\x9f\x8e\x89\n";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc == 0,
		      "Valid UTF-8: parser should accept but rejected");
}
END_TEST

/* Partial UTF-8 at end of stream (no trailing newline) */
START_TEST(parse_bug_partial_utf8_at_eof)
{
	/* "foo: abc\xc3" — truncated 2-byte sequence at end of input */
	const char input[] = "foo: abc\xc3";
	int rc;

	rc = try_parse(input, sizeof(input) - 1, 0);
	ck_assert_msg(rc != 0,
		      "Partial UTF-8 at EOF: parser should reject but accepted");
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

	/* Bug 14 (PARSER): NEL block scalar spurious null byte */
	fy_check_testcase_add_test(ctc, parse_bug_nel_clip_chomping);
	fy_check_testcase_add_test(ctc, parse_bug_nel_keep_chomping);
	fy_check_testcase_add_test(ctc, parse_bug_nel_strip_chomping_ok);
	fy_check_testcase_add_test(ctc, parse_bug_nel_spec_09_22);

	/* Bug 15 (PARSER): Invalid UTF-8 and NUL in input stream */
	fy_check_testcase_add_test(ctc, parse_bug_nul_in_stream);
	fy_check_testcase_add_test(ctc, parse_bug_nul_in_comment);
	fy_check_testcase_add_test(ctc, parse_bug_partial_utf8_2byte);
	fy_check_testcase_add_test(ctc, parse_bug_partial_utf8_3byte);
	fy_check_testcase_add_test(ctc, parse_bug_partial_utf8_4byte);
	fy_check_testcase_add_test(ctc, parse_bug_invalid_utf8_lone_continuation);
	fy_check_testcase_add_test(ctc, parse_bug_invalid_utf8_overlong);
	fy_check_testcase_add_test(ctc, parse_bug_invalid_utf8_fe);
	fy_check_testcase_add_test(ctc, parse_bug_invalid_utf8_ff);
	fy_check_testcase_add_test(ctc, parse_valid_utf8_ok);
	fy_check_testcase_add_test(ctc, parse_bug_partial_utf8_at_eof);
}
