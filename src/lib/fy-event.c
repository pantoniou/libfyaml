/*
 * fy-event.c - YAML event methods
 *
 * Copyright (c) 2021 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

#include <libfyaml.h>

#include "fy-parse.h"
#include "fy-emit.h"
#include "fy-doc.h"

#include "fy-ctype.h"
#include "fy-utf8.h"
#include "fy-utils.h"

#include "fy-generic.h"

#include "fy-event.h"

struct fy_eventp *fy_eventp_alloc(void)
{
	struct fy_eventp *fyep;

	fyep = malloc(sizeof(*fyep));
	if (!fyep)
		return NULL;

	fyep->e.type = FYET_NONE;

	return fyep;
}

void fy_eventp_clean_rl(struct fy_token_list *fytl, struct fy_eventp *fyep)
{
	struct fy_event *fye;

	if (!fyep)
		return;

	fye = &fyep->e;
	switch (fye->type) {
	case FYET_NONE:
		break;
	case FYET_STREAM_START:
		fy_token_unref_rl(fytl, fye->stream_start.stream_start);
		break;
	case FYET_STREAM_END:
		fy_token_unref_rl(fytl, fye->stream_end.stream_end);
		break;
	case FYET_DOCUMENT_START:
		fy_token_unref_rl(fytl, fye->document_start.document_start);
		fy_document_state_unref(fye->document_start.document_state);
		break;
	case FYET_DOCUMENT_END:
		fy_token_unref_rl(fytl, fye->document_end.document_end);
		break;
	case FYET_MAPPING_START:
		fy_token_unref_rl(fytl, fye->mapping_start.anchor);
		fy_token_unref_rl(fytl, fye->mapping_start.tag);
		fy_token_unref_rl(fytl, fye->mapping_start.mapping_start);
		break;
	case FYET_MAPPING_END:
		fy_token_unref_rl(fytl, fye->mapping_end.mapping_end);
		break;
	case FYET_SEQUENCE_START:
		fy_token_unref_rl(fytl, fye->sequence_start.anchor);
		fy_token_unref_rl(fytl, fye->sequence_start.tag);
		fy_token_unref_rl(fytl, fye->sequence_start.sequence_start);
		break;
	case FYET_SEQUENCE_END:
		fy_token_unref_rl(fytl, fye->sequence_end.sequence_end);
		break;
	case FYET_SCALAR:
		fy_token_unref_rl(fytl, fye->scalar.anchor);
		fy_token_unref_rl(fytl, fye->scalar.tag);
		fy_token_unref_rl(fytl, fye->scalar.value);
		break;
	case FYET_ALIAS:
		fy_token_unref_rl(fytl, fye->alias.anchor);
		break;
	}

	fye->type = FYET_NONE;
}

void fy_parse_eventp_clean(struct fy_parser *fyp, struct fy_eventp *fyep)
{
	if (!fyp || !fyep)
		return;

	fy_eventp_clean_rl(fyp->recycled_token_list, fyep);
}

void fy_emit_eventp_clean(struct fy_emitter *emit, struct fy_eventp *fyep)
{
	if (!emit || !fyep)
		return;

	fy_eventp_clean_rl(emit->recycled_token_list, fyep);
}

void fy_eventp_free(struct fy_eventp *fyep)
{
	if (!fyep)
		return;

	/* clean, safe to do */
	fy_eventp_clean_rl(NULL, fyep);

	free(fyep);
}

void fy_eventp_release(struct fy_eventp *fyep)
{
	fy_eventp_free(fyep);
}

struct fy_eventp *fy_parse_eventp_alloc(struct fy_parser *fyp)
{
	struct fy_eventp *fyep = NULL;

	if (!fyp)
		return NULL;

	if (fyp->recycled_eventp_list)
		fyep = fy_eventp_list_pop(fyp->recycled_eventp_list);
	if (!fyep)
		fyep = fy_eventp_alloc();
	if (!fyep)
		return NULL;

	fyep->e.type = FYET_NONE;

	return fyep;
}

void fy_parse_eventp_recycle(struct fy_parser *fyp, struct fy_eventp *fyep)
{
	if (!fyp || !fyep)
		return;

	/* clean, safe to do */
	fy_parse_eventp_clean(fyp, fyep);

	/* and push to the parser recycle list */
	if (fyp->recycled_eventp_list)
		fy_eventp_list_push(fyp->recycled_eventp_list, fyep);
	else
		fy_eventp_free(fyep);
}

void fy_parser_event_free(struct fy_parser *fyp, struct fy_event *fye)
{
	struct fy_eventp *fyep;

	if (!fyp || !fye)
		return;

	if (fy_reader_generates_events(fyp->reader)) {
		fy_reader_event_free(fyp->reader, fye);
		return;
	}

	fyep = container_of(fye, struct fy_eventp, e);

	fy_parse_eventp_recycle(fyp, fyep);
}

void fy_emit_eventp_recycle(struct fy_emitter *emit, struct fy_eventp *fyep)
{
	if (!emit || !fyep)
		return;

	/* clean, safe to do */
	fy_emit_eventp_clean(emit, fyep);

	if (emit->recycled_eventp_list)
		fy_eventp_list_push(emit->recycled_eventp_list, fyep);
	else
		fy_eventp_free(fyep);
}

void fy_emit_event_free(struct fy_emitter *emit, struct fy_event *fye)
{
	struct fy_eventp *fyep;

	if (!emit || !fye)
		return;

	fyep = container_of(fye, struct fy_eventp, e);

	fy_emit_eventp_recycle(emit, fyep);
}

struct fy_eventp *
fy_eventp_vcreate_internal(struct fy_eventp_list *recycled_list, struct fy_diag *diag,
			   struct fy_document_state *fyds, enum fy_event_type type, va_list ap)
{
	struct fy_eventp *fyep = NULL;
	struct fy_event *fye = NULL;
	struct fy_document_state *fyds_new = NULL;
	const struct fy_version *vers;
	const struct fy_tag *tag, * const *tagp;
	struct fy_token *fyt;
	enum fy_token_type ttype;
	int rc, tag_count = 0;
	enum fy_node_style style;
	enum fy_scalar_style sstyle;
	struct fy_token **fyt_anchorp = NULL, **fyt_tagp = NULL;
	struct fy_input *fyi = NULL;
	struct fy_atom handle;
	const char *value;
	size_t len;
	char *data = NULL;
	struct fy_tag_scan_info info;
	struct fy_token *fyt_td;

	/* try the recycled list first */
	if (recycled_list)
		fyep = fy_eventp_list_pop(recycled_list);
	/* if not there yet, allocate a fresh one */
	if (!fyep)
		fyep = fy_eventp_alloc();
	if (!fyep)
		return NULL;

	fye = &fyep->e;

	fye->type = type;

	switch (type) {
	case FYET_NONE:
		break;
	case FYET_STREAM_START:
		fye->stream_start.stream_start = NULL;
		break;
	case FYET_STREAM_END:
		fye->stream_end.stream_end = NULL;
		break;
	case FYET_DOCUMENT_START:
		fye->document_start.document_start = NULL;
		fyds_new = fy_document_state_default(fy_document_state_version(fyds), NULL);	/* start with the default state */
		if (!fyds_new) {
			fy_error(diag, "fy_document_state_alloc() failed\n");
			goto err_out;
		}
		fye->document_start.implicit = va_arg(ap, int);
		vers = va_arg(ap, const struct fy_version *);
		if (vers) {
			fyds_new->version = *vers;
			fyds_new->version_explicit = true;
		}
		fyds_new->start_implicit = fye->document_start.implicit;
		fyds_new->end_implicit = false;	/* this is not used right now */
		tag_count = 0;
		tagp = va_arg(ap, const struct fy_tag * const *);
		if (tagp) {
			while ((tag = tagp[tag_count]) != NULL) {
				tag_count++;
				rc = fy_document_state_append_tag(fyds_new, tag->handle, tag->prefix, false);
				if (rc) {
					fy_error(diag, "fy_document_state_append_tag() failed on handle='%s' prefix='%s'\n",
							tag->handle, tag->prefix);
					goto err_out;
				}
			}
		}
		if (tag_count)
			fyds_new->tags_explicit = true;
		fye->document_start.document_state = fyds_new;
		fyds_new = NULL;
		break;
	case FYET_DOCUMENT_END:
		fye->document_end.document_end = NULL;
		fye->document_end.implicit = va_arg(ap, int);
		break;
	case FYET_MAPPING_START:
	case FYET_SEQUENCE_START:
		style = va_arg(ap, enum fy_node_style);
		ttype = FYTT_NONE;

		if (style != FYNS_ANY && style != FYNS_FLOW && style != FYNS_BLOCK) {
			fy_error(diag, "illegal style for %s_START\n",
					type == FYET_MAPPING_START ? "MAPPING" : "SEQUENCE");
			goto err_out;
		}

		if (style != FYNS_ANY) {
			if (style == FYNS_FLOW)
				ttype = type == FYET_MAPPING_START ?
							FYTT_FLOW_MAPPING_START :
							FYTT_FLOW_SEQUENCE_START;
			else
				ttype = type == FYET_MAPPING_START ?
							FYTT_BLOCK_MAPPING_START :
							FYTT_BLOCK_SEQUENCE_START;
			fyt = fy_token_create(ttype, NULL);
			if (!fyt) {
				fy_error(diag, "fy_token_create() failed for %s_START\n",
						type == FYET_MAPPING_START ? "MAPPING" : "SEQUENCE");
				goto err_out;
			}
		} else
			fyt = NULL;

		if (type == FYET_MAPPING_START) {
			fye->mapping_start.mapping_start = fyt;
			fye->mapping_start.anchor = NULL;
			fye->mapping_start.tag = NULL;
			fyt_anchorp = &fye->mapping_start.anchor;
			fyt_tagp = &fye->mapping_start.tag;
		} else {
			fye->sequence_start.sequence_start = fyt;
			fye->sequence_start.anchor = NULL;
			fye->sequence_start.tag = NULL;
			fyt_anchorp = &fye->sequence_start.anchor;
			fyt_tagp = &fye->sequence_start.tag;
		}
		fyt = NULL;
		break;
	case FYET_MAPPING_END:
		fye->mapping_end.mapping_end = NULL;
		break;
	case FYET_SEQUENCE_END:
		fye->sequence_end.sequence_end = NULL;
		break;

	case FYET_SCALAR:
	case FYET_ALIAS:

		if (type == FYET_SCALAR) {
			fye->scalar.value = fye->scalar.anchor = fye->scalar.tag = NULL;
			sstyle = va_arg(ap, enum fy_scalar_style);
			value = va_arg(ap, const char *);
			len = va_arg(ap, size_t);
			if (!value && len) {
				fy_error(diag, "NULL value with len > 0, illegal SCALAR\n");
				goto err_out;
			}
			if (len == FY_NT)
				len = strlen(value);
		} else {
			fye->alias.anchor = NULL;
			sstyle = FYSS_PLAIN;
			value = va_arg(ap, const char *);
			if (!value) {
				fy_error(diag, "NULL value, illegal ALIAS\n");
				goto err_out;
			}
			len = strlen(value);
		}

		fyt = NULL;
		fyi = NULL;

		data = malloc(len + 1);
		if (!data) {
			fy_error(diag, "malloc() failed\n");
			goto err_out;
		}
		memcpy(data, value, len);
		/* always NULL terminate */
		data[len] = '\0';
		fyi = fy_input_from_malloc_data_styled(data, len, &handle, sstyle);
		if (!fyi) {
			fy_error(diag, "fy_input_from_malloc_data() failed\n");
			goto err_out;
		}
		data = NULL;

		if (type == FYET_SCALAR) {
			fyt = fy_token_create(FYTT_SCALAR, &handle, sstyle);
			if (!fyt) {
				fy_error(diag, "fy_token_create() failed for %s\n",
						"SCALAR");
				goto err_out;
			}

			fye->scalar.value = fyt;
			fyt = NULL;

			fye->scalar.anchor = NULL;
			fye->scalar.tag = NULL;
			fyt_anchorp = &fye->scalar.anchor;
			fyt_tagp = &fye->scalar.tag;
		} else {
			fyt = fy_token_create(FYTT_ALIAS, &handle, NULL);
			if (!fyt) {
				fy_error(diag, "fy_token_create() failed for %s\n",
						"ALIAS");
				goto err_out;
			}

			fye->alias.anchor = fyt;
			fyt = NULL;
		}
		fy_input_unref(fyi);
		fyi = NULL;
		break;
	}

	if (fyt_anchorp && (value = va_arg(ap, const char *)) != NULL) {

		len = strlen(value);
		data = malloc(len + 1);
		if (!data) {
			fy_error(diag, "malloc() failed\n");
			goto err_out;
		}
		memcpy(data, value, len);
		/* always NULL terminate */
		data[len] = '\0';

		fyi = fy_input_from_malloc_data(data, len, &handle, false);
		if (!fyi) {
			fy_error(diag, "fy_input_from_malloc_data() failed\n");
			goto err_out;
		}
		data = NULL;

		/* make sure the input as valid as an anchor */
		if (!handle.valid_anchor) {
			fy_error(diag, "input was not valid as anchor\n");
			goto err_out;
		}

		fyt = fy_token_create(FYTT_ANCHOR, &handle);
		if (!fyt) {
			fy_error(diag, "fy_token_create() failed\n");
			goto err_out;
		}
		*fyt_anchorp = fyt;
		fyt = NULL;
		fy_input_unref(fyi);
		fyi = NULL;
	}

	if (fyt_tagp && (value = va_arg(ap, const char *)) != NULL) {

		len = strlen(value);
		data = malloc(len + 1);
		if (!data) {
			fy_error(diag, "malloc() failed\n");
			goto err_out;
		}
		memcpy(data, value, len);
		/* always NULL terminate */
		data[len] = '\0';

		rc = fy_tag_scan(data, len, &info);
		if (rc) {
			fy_error(diag, "invalid tag %s (tag_scan)\n", value);
			goto err_out;
		}

		fyt_td = fy_document_state_lookup_tag_directive(fyds, data + info.prefix_length, info.handle_length);
		if (!fyt_td) {
			fy_error(diag, "invalid tag %s (lookup tag directive)\n", value);
			goto err_out;
		}

		fyi = fy_input_from_malloc_data(data, len, &handle, false);
		if (!fyi)
			goto err_out;
		data = NULL;

		handle.style = FYAS_URI;
		handle.direct_output = false;
		handle.storage_hint = 0;
		handle.storage_hint_valid = false;

		fyt = fy_token_create(FYTT_TAG, &handle, info.prefix_length,
				info.handle_length, info.uri_length, fyt_td);
		if (!fyt) {
			fy_error(diag, "fy_token_create() failed\n");
			goto err_out;
		}
		*fyt_tagp = fyt;
		fyt = NULL;
		fy_input_unref(fyi);
		fyi = NULL;
	}

	return fyep;

err_out:
	fy_input_unref(fyi);
	if (data)
		free(data);
	fy_document_state_unref(fyds_new);
	/* don't bother with recycling on error */
	fy_eventp_free(fyep);
	return NULL;
}

struct fy_eventp *
fy_eventp_create_internal(struct fy_eventp_list *recycled_list, struct fy_diag *diag,
			  struct fy_document_state *fyds,
			  enum fy_event_type type, ...)
{
	struct fy_eventp *fyep;
	va_list ap;

	va_start(ap, type);
	fyep = fy_eventp_vcreate_internal(recycled_list, diag, fyds, type, ap);
	va_end(ap);

	return fyep;
}

struct fy_event *
fy_emit_event_vcreate(struct fy_emitter *emit, enum fy_event_type type, va_list ap)
{
	struct fy_eventp *fyep;

	if (!emit)
		return NULL;

	fyep = fy_eventp_vcreate_internal(emit->recycled_eventp_list, emit->diag, emit->fyds, type, ap);
	if (!fyep)
		return NULL;

	return &fyep->e;
}

struct fy_event *
fy_emit_event_create(struct fy_emitter *emit, enum fy_event_type type, ...)
{
	struct fy_event *fye;
	va_list ap;

	va_start(ap, type);
	fye = fy_emit_event_vcreate(emit, type, ap);
	va_end(ap);

	return fye;
}

int
fy_emit_vevent(struct fy_emitter *emit, enum fy_event_type type, va_list ap)
{
	struct fy_event *fye;

	if (!emit)
		return -1;

	fye = fy_emit_event_vcreate(emit, type, ap);
	if (!fye)
		return -1;

	return fy_emit_event(emit, fye);
}

int
fy_emit_eventf(struct fy_emitter *emit, enum fy_event_type type, ...)
{
	int rc;
	va_list ap;

	va_start(ap, type);
	rc = fy_emit_vevent(emit, type, ap);
	va_end(ap);

	return rc;
}

int
fy_emit_scalar_write(struct fy_emitter *fye, enum fy_scalar_style style,
		     const char *anchor, const char *tag,
		     const char *text, size_t len)
{
	return fy_emit_eventf(fye, FYET_SCALAR, style, text, len, anchor, tag);
}

int
fy_emit_scalar_vprintf(struct fy_emitter *fye, enum fy_scalar_style style,
		       const char *anchor, const char *tag,
		       const char *fmt, va_list ap)
{
	char *buf;
	size_t len;
	int rc;

	rc = vasprintf(&buf, fmt, ap);
	if (rc < 0)
		return -1;
	len = (size_t)rc;

	rc = fy_emit_scalar_write(fye, style, anchor, tag, buf, len);
	free(buf);

	return rc;
}

int
fy_emit_scalar_printf(struct fy_emitter *fye, enum fy_scalar_style style,
		      const char *anchor, const char *tag,
		      const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = fy_emit_scalar_vprintf(fye, style, anchor, tag, fmt, ap);
	va_end(ap);

	return rc;
}

struct fy_event *
fy_parse_event_vcreate(struct fy_parser *fyp, enum fy_event_type type, va_list ap)
{
	struct fy_eventp *fyep;

	if (!fyp)
		return NULL;

	fyep = fy_eventp_vcreate_internal(fyp->recycled_eventp_list, fyp->diag, fyp->current_document_state, type, ap);
	if (!fyep)
		return NULL;

	return &fyep->e;
}

struct fy_event *
fy_parse_event_create(struct fy_parser *fyp, enum fy_event_type type, ...)
{
	struct fy_event *fye;
	va_list ap;

	va_start(ap, type);
	fye = fy_parse_event_vcreate(fyp, type, ap);
	va_end(ap);

	return fye;
}

bool fy_event_is_implicit(struct fy_event *fye)
{
	/* NULL event is implicit */
	if (!fye)
		return true;

	switch (fye->type) {

	case FYET_DOCUMENT_START:
		return fye->document_start.implicit;

	case FYET_DOCUMENT_END:
		return fye->document_end.implicit;

	case FYET_MAPPING_START:
	case FYET_MAPPING_END:
	case FYET_SEQUENCE_START:
	case FYET_SEQUENCE_END:
		return fy_event_get_node_style(fye) == FYNS_BLOCK;

	default:
		break;
	}

	return false;
}

bool fy_document_event_is_implicit(const struct fy_event *fye)
{
	if (fye->type == FYET_DOCUMENT_START)
		return fye->document_start.implicit;

	if (fye->type == FYET_DOCUMENT_END)
		return fye->document_end.implicit;

	return false;
}

struct fy_token *fy_event_get_token(struct fy_event *fye)
{
	if (!fye)
		return NULL;

	switch (fye->type) {
	case FYET_NONE:
		break;

	case FYET_STREAM_START:
		return fye->stream_start.stream_start;

	case FYET_STREAM_END:
		return fye->stream_end.stream_end;

	case FYET_DOCUMENT_START:
		return fye->document_start.document_start;

	case FYET_DOCUMENT_END:
		return fye->document_end.document_end;

	case FYET_MAPPING_START:
		return fye->mapping_start.mapping_start;

	case FYET_MAPPING_END:
		return fye->mapping_end.mapping_end;

	case FYET_SEQUENCE_START:
		return fye->sequence_start.sequence_start;

	case FYET_SEQUENCE_END:
		return fye->sequence_end.sequence_end;

	case FYET_SCALAR:
		return fye->scalar.value;

	case FYET_ALIAS:
		return fye->alias.anchor;

	}

	return NULL;
}

struct fy_token *fy_event_get_anchor_token(struct fy_event *fye)
{
	if (!fye)
		return NULL;

	switch (fye->type) {
	case FYET_MAPPING_START:
		return fye->mapping_start.anchor;
	case FYET_SEQUENCE_START:
		return fye->sequence_start.anchor;
	case FYET_SCALAR:
		return fye->scalar.anchor;
	default:
		break;
	}

	return NULL;
}

struct fy_token *fy_event_get_and_clear_anchor_token(struct fy_event *fye)
{
	struct fy_token *fyt = NULL;

	if (!fye)
		return NULL;

	switch (fye->type) {
	case FYET_MAPPING_START:
		fyt = fye->mapping_start.anchor;
		fye->mapping_start.anchor = NULL;
		break;

	case FYET_SEQUENCE_START:
		fyt = fye->sequence_start.anchor;
		fye->sequence_start.anchor = NULL;
		break;

	case FYET_SCALAR:
		fyt = fye->scalar.anchor;
		fye->scalar.anchor = NULL;
		break;

	default:
		break;
	}

	return fyt;
}

struct fy_token *fy_event_get_tag_token(struct fy_event *fye)
{
	if (!fye)
		return NULL;

	switch (fye->type) {
	case FYET_MAPPING_START:
		return fye->mapping_start.tag;
	case FYET_SEQUENCE_START:
		return fye->sequence_start.tag;
	case FYET_SCALAR:
		return fye->scalar.tag;
	default:
		break;
	}

	return NULL;
}

const struct fy_mark *
fy_event_start_mark(struct fy_event *fye)
{
	if (!fye)
		return NULL;

	switch (fye->type) {
	case FYET_NONE:
		break;

	case FYET_STREAM_START:
		return fy_token_start_mark(fye->stream_start.stream_start);

	case FYET_STREAM_END:
		return fy_token_start_mark(fye->stream_end.stream_end);

	case FYET_DOCUMENT_START:
		return fy_token_start_mark(fye->document_start.document_start);

	case FYET_DOCUMENT_END:
		return fy_token_start_mark(fye->document_end.document_end);

	case FYET_MAPPING_START:
		return fy_token_start_mark(fye->mapping_start.mapping_start);

	case FYET_MAPPING_END:
		return fy_token_start_mark(fye->mapping_end.mapping_end);

	case FYET_SEQUENCE_START:
		return fy_token_start_mark(fye->sequence_start.sequence_start);

	case FYET_SEQUENCE_END:
		return fy_token_start_mark(fye->sequence_end.sequence_end);

	case FYET_SCALAR:
		return fy_token_start_mark(fye->scalar.value);

	case FYET_ALIAS:
		return fy_token_start_mark(fye->alias.anchor);

	}

	return NULL;
}

const struct fy_mark *
fy_event_end_mark(struct fy_event *fye)
{
	if (!fye)
		return NULL;

	switch (fye->type) {
	case FYET_NONE:
		break;

	case FYET_STREAM_START:
		return fy_token_end_mark(fye->stream_start.stream_start);

	case FYET_STREAM_END:
		return fy_token_end_mark(fye->stream_end.stream_end);

	case FYET_DOCUMENT_START:
		return fy_token_end_mark(fye->document_start.document_start);

	case FYET_DOCUMENT_END:
		return fy_token_end_mark(fye->document_end.document_end);

	case FYET_MAPPING_START:
		return fy_token_end_mark(fye->mapping_start.mapping_start);

	case FYET_MAPPING_END:
		return fy_token_end_mark(fye->mapping_end.mapping_end);

	case FYET_SEQUENCE_START:
		return fy_token_end_mark(fye->sequence_start.sequence_start);

	case FYET_SEQUENCE_END:
		return fy_token_end_mark(fye->sequence_end.sequence_end);

	case FYET_SCALAR:
		return fy_token_end_mark(fye->scalar.value);

	case FYET_ALIAS:
		return fy_token_end_mark(fye->alias.anchor);

	}

	return NULL;
}

const struct fy_mark *
fy_event_style_start_mark(struct fy_event *fye)
{
	if (!fye)
		return NULL;

	switch (fye->type) {
	case FYET_NONE:
		break;

	case FYET_STREAM_START:
		return fy_token_style_start_mark(fye->stream_start.stream_start);

	case FYET_STREAM_END:
		return fy_token_style_start_mark(fye->stream_end.stream_end);

	case FYET_DOCUMENT_START:
		return fy_token_style_start_mark(fye->document_start.document_start);

	case FYET_DOCUMENT_END:
		return fy_token_style_start_mark(fye->document_end.document_end);

	case FYET_MAPPING_START:
		if (fye->mapping_start.tag)
			return fy_token_style_start_mark(fye->mapping_start.tag);
		return fy_token_style_start_mark(fye->mapping_start.mapping_start);

	case FYET_MAPPING_END:
		return fy_token_style_start_mark(fye->mapping_end.mapping_end);

	case FYET_SEQUENCE_START:
		if (fye->sequence_start.tag)
			return fy_token_style_start_mark(fye->sequence_start.tag);
		return fy_token_style_start_mark(fye->sequence_start.sequence_start);

	case FYET_SEQUENCE_END:
		return fy_token_style_start_mark(fye->sequence_end.sequence_end);

	case FYET_SCALAR:
		if (fye->scalar.tag)
			return fy_token_style_start_mark(fye->scalar.tag);
		return fy_token_style_start_mark(fye->scalar.value);

	case FYET_ALIAS:
		return fy_token_style_start_mark(fye->alias.anchor);

	}

	return NULL;
}

const struct fy_mark *
fy_event_style_end_mark(struct fy_event *fye)
{
	if (!fye)
		return NULL;

	switch (fye->type) {
	case FYET_NONE:
		break;

	case FYET_STREAM_START:
		return fy_token_style_end_mark(fye->stream_start.stream_start);

	case FYET_STREAM_END:
		return fy_token_style_end_mark(fye->stream_end.stream_end);

	case FYET_DOCUMENT_START:
		return fy_token_style_end_mark(fye->document_start.document_start);

	case FYET_DOCUMENT_END:
		return fy_token_style_end_mark(fye->document_end.document_end);

	case FYET_MAPPING_START:
		return fy_token_style_end_mark(fye->mapping_start.mapping_start);

	case FYET_MAPPING_END:
		return fy_token_style_end_mark(fye->mapping_end.mapping_end);

	case FYET_SEQUENCE_START:
		return fy_token_style_end_mark(fye->sequence_start.sequence_start);

	case FYET_SEQUENCE_END:
		return fy_token_style_end_mark(fye->sequence_end.sequence_end);

	case FYET_SCALAR:
		return fy_token_style_end_mark(fye->scalar.value);

	case FYET_ALIAS:
		return fy_token_style_end_mark(fye->alias.anchor);

	}

	return NULL;
}


enum fy_node_style
fy_event_get_node_style(struct fy_event *fye)
{
	struct fy_token *fyt;

	fyt = fy_event_get_token(fye);
	if (!fyt)
		return FYNS_ANY;

	switch (fye->type) {
		/* unstyled events */
	case FYET_NONE:
	case FYET_STREAM_START:
	case FYET_STREAM_END:
	case FYET_DOCUMENT_START:
	case FYET_DOCUMENT_END:
		return FYNS_ANY;

	case FYET_MAPPING_START:
		return fyt && fyt->type == FYTT_FLOW_MAPPING_START ? FYNS_FLOW : FYNS_BLOCK;

	case FYET_MAPPING_END:
		return fyt && fyt->type == FYTT_FLOW_MAPPING_END ? FYNS_FLOW : FYNS_BLOCK;

	case FYET_SEQUENCE_START:
		return fyt && fyt->type == FYTT_FLOW_SEQUENCE_START ? FYNS_FLOW : FYNS_BLOCK;

	case FYET_SEQUENCE_END:
		return fyt && fyt->type == FYTT_FLOW_SEQUENCE_END ? FYNS_FLOW : FYNS_BLOCK;

	case FYET_SCALAR:
		return fyt ? fy_node_style_from_scalar_style(fyt->scalar.style) : FYNS_PLAIN;

	case FYET_ALIAS:
		return FYNS_ALIAS;

	}

	return FYNS_ANY;
}

struct fy_eventp *fy_parse_eventp_clone(struct fy_parser *fyp, struct fy_eventp *fyep_src, bool strip_anchors)
{
	struct fy_eventp *fyep = NULL;
	struct fy_event *fye, *fye_src;

	if (!fyp || !fyep_src)
		return NULL;

	if (fyp->recycled_eventp_list)
		fyep = fy_eventp_list_pop(fyp->recycled_eventp_list);
	if (!fyep)
		fyep = fy_eventp_alloc();
	if (!fyep)
		return NULL;

	fye_src = &fyep_src->e;
	fye = &fyep->e;

	fye->type = fye_src->type;
	switch (fye->type) {
	case FYET_NONE:
		break;
	case FYET_STREAM_START:
		fye->stream_start.stream_start = fy_token_ref(fye_src->stream_start.stream_start);
		break;
	case FYET_STREAM_END:
		fye->stream_end.stream_end = fy_token_ref(fye_src->stream_end.stream_end);
		break;
	case FYET_DOCUMENT_START:
		fye->document_start.document_start = fy_token_ref(fye_src->document_start.document_start);
		fye->document_start.document_state = fy_document_state_ref(fye_src->document_start.document_state);
		break;
	case FYET_DOCUMENT_END:
		fye->document_end.document_end = fy_token_ref(fye_src->document_end.document_end);
		break;
	case FYET_MAPPING_START:
		if (!strip_anchors)
			fye->mapping_start.anchor = fy_token_ref(fye_src->mapping_start.anchor);
		else
			fye->mapping_start.anchor = NULL;
		fye->mapping_start.tag = fy_token_ref(fye_src->mapping_start.tag);
		fye->mapping_start.mapping_start = fy_token_ref(fye_src->mapping_start.mapping_start);
		break;
	case FYET_MAPPING_END:
		fye->mapping_end.mapping_end = fy_token_ref(fye_src->mapping_end.mapping_end);
		break;
	case FYET_SEQUENCE_START:
		if (!strip_anchors)
			fye->sequence_start.anchor = fy_token_ref(fye_src->sequence_start.anchor);
		else
			fye->sequence_start.anchor = NULL;
		fye->sequence_start.tag = fy_token_ref(fye_src->sequence_start.tag);
		fye->sequence_start.sequence_start = fy_token_ref(fye_src->sequence_start.sequence_start);
		break;
	case FYET_SEQUENCE_END:
		fye->sequence_end.sequence_end = fy_token_ref(fye_src->sequence_end.sequence_end);
		break;
	case FYET_SCALAR:
		if (!strip_anchors)
			fye->scalar.anchor = fy_token_ref(fye_src->scalar.anchor);
		else
			fye->scalar.anchor = NULL;
		fye->scalar.tag = fy_token_ref(fye_src->scalar.tag);
		fye->scalar.value = fy_token_ref(fye_src->scalar.value);
		break;
	case FYET_ALIAS:
		fye->alias.anchor = fy_token_ref(fye_src->alias.anchor);
		break;
	}
	return fyep;
}

const char *fy_event_get_anchor(struct fy_event *fye, size_t *anchor_lenp)
{
	struct fy_token *anchor;

	anchor = fy_event_get_anchor_token(fye);
	if (!anchor)
		return NULL;

	return fy_token_get_text(anchor, anchor_lenp);
}

const struct fy_version *
fy_document_start_event_version(struct fy_event *fye)
{
	/* return the default if not set */
	if (!fye || fye->type != FYET_DOCUMENT_START)
		return &fy_default_version;
	return fy_document_state_version(fye->document_start.document_state);
}

char *fy_event_to_string(struct fy_event *fye)
{
	struct fy_memstream *fyms = NULL;
	FILE *fp = NULL;
	char *mbuf = NULL;
	const char *anchor = NULL;
	const char *tag = NULL;
	const char *text = NULL;
	const char *alias = NULL;
	size_t msize, anchor_len = 0, tag_len = 0, text_len = 0, alias_len = 0;
	enum fy_scalar_style style;
	const uint8_t *p;
	int i, c, w;

	if (!fye)
		return NULL;

	fyms = fy_memstream_open(&fp);
	if (!fyms)
		return NULL;

	/* event type */
	switch (fye->type) {
	case FYET_NONE:
		fprintf(fp, "???");
		break;
	case FYET_STREAM_START:
		fprintf(fp, "+STR");
		break;
	case FYET_STREAM_END:
		fprintf(fp, "-STR");
		break;
	case FYET_DOCUMENT_START:
		fprintf(fp, "+DOC");
		break;
	case FYET_DOCUMENT_END:
		fprintf(fp, "-DOC");
		break;
	case FYET_MAPPING_START:
		fprintf(fp, "+MAP");
		if (fye->mapping_start.anchor)
			anchor = fy_token_get_text(fye->mapping_start.anchor, &anchor_len);
		if (fye->mapping_start.tag)
			tag = fy_token_get_text(fye->mapping_start.tag, &tag_len);
		if (fy_event_get_node_style(fye) == FYNS_FLOW)
			fprintf(fp, " {}");
		break;
	case FYET_MAPPING_END:
		fprintf(fp, "-MAP");
		break;
	case FYET_SEQUENCE_START:
		fprintf(fp, "+SEQ");
		if (fye->sequence_start.anchor)
			anchor = fy_token_get_text(fye->sequence_start.anchor, &anchor_len);
		if (fye->sequence_start.tag)
			tag = fy_token_get_text(fye->sequence_start.tag, &tag_len);
		if (fy_event_get_node_style(fye) == FYNS_FLOW)
			fprintf(fp, " []");
		break;
	case FYET_SEQUENCE_END:
		fprintf(fp, "-SEQ");
		break;
	case FYET_SCALAR:
		fprintf(fp, "=VAL");
		if (fye->scalar.anchor)
			anchor = fy_token_get_text(fye->scalar.anchor, &anchor_len);
		if (fye->scalar.tag)
			tag = fy_token_get_text(fye->scalar.tag, &tag_len);
		break;
	case FYET_ALIAS:
		fprintf(fp, "=ALI");
		break;
	default:
		break;
	}

	/* (position) anchor and tag */
	if (anchor)
		fprintf(fp, " &%.*s", (int)anchor_len, anchor);
	if (tag)
		fprintf(fp, " <%.*s>", (int)tag_len, tag);

	/* style hint */
	switch (fye->type) {
	default:
		break;
	case FYET_DOCUMENT_START:
		if (!fy_document_event_is_implicit(fye))
			fprintf(fp, " ---");
		break;
	case FYET_DOCUMENT_END:
		if (!fy_document_event_is_implicit(fye))
			fprintf(fp, " ...");
		break;
	case FYET_MAPPING_START:
		break;
	case FYET_SEQUENCE_START:
		break;
	case FYET_SCALAR:
		style = fy_token_scalar_style(fye->scalar.value);
		switch (style) {
		case FYSS_ANY:
			fprintf(fp, " !");
			break;
		case FYSS_PLAIN:
			fprintf(fp, " :");
			break;
		case FYSS_SINGLE_QUOTED:
			fprintf(fp, " '");
			break;
		case FYSS_DOUBLE_QUOTED:
			fprintf(fp, " \"");
			break;
		case FYSS_LITERAL:
			fprintf(fp, " |");
			break;
		case FYSS_FOLDED:
			fprintf(fp, " >");
			break;
		default:
			break;
		}
		break;
	case FYET_ALIAS:
		break;
	}

	/* content */
	switch (fye->type) {
	default:
		break;
	case FYET_SCALAR:
		text = fy_token_get_text(fye->scalar.value, &text_len);
		for (p = (const uint8_t *)text; text_len > 0; p += w, text_len -= (size_t)w) {

			/* get width from the first octet */
			w = (p[0] & 0x80) == 0x00 ? 1 :
			    (p[0] & 0xe0) == 0xc0 ? 2 :
			    (p[0] & 0xf0) == 0xe0 ? 3 :
			    (p[0] & 0xf8) == 0xf0 ? 4 : 0;

			/* error, clip it */
			if ((size_t)w > text_len)
				break;

			/* initial value */
			c = p[0] & (0xff >> w);
			for (i = 1; i < w; i++) {
				if ((p[i] & 0xc0) != 0x80)
					break;
				c = (c << 6) | (p[i] & 0x3f);
			}

			/* check for validity */
			if ((w == 4 && c < 0x10000) ||
			    (w == 3 && c <   0x800) ||
			    (w == 2 && c <    0x80) ||
			    (c >= 0xd800 && c <= 0xdfff) || c >= 0x110000)
				break;

			switch (c) {
			case '\\':
				fprintf(fp, "\\\\");
				break;
			case '\0':
				fprintf(fp, "\\0");
				break;
			case '\b':
				fprintf(fp, "\\b");
				break;
			case '\f':
				fprintf(fp, "\\f");
				break;
			case '\n':
				fprintf(fp, "\\n");
				break;
			case '\r':
				fprintf(fp, "\\r");
				break;
			case '\t':
				fprintf(fp, "\\t");
				break;
			case '\a':
				fprintf(fp, "\\a");
				break;
			case '\v':
				fprintf(fp, "\\v");
				break;
			case '\x1b':
				fprintf(fp, "\\e");
				break;
			case 0x85:
				fprintf(fp, "\\N");
				break;
			case 0xa0:
				fprintf(fp, "\\_");
				break;
			case 0x2028:
				fprintf(fp, "\\L");
				break;
			case 0x2029:
				fprintf(fp, "\\P");
				break;
			default:
				if ((c >= 0x01 && c <= 0x1f) || c == 0x7f ||	/* C0 */
				    (c >= 0x80 && c <= 0x9f))			/* C1 */
					fprintf(fp, "\\x%02x", c);
				else
					fprintf(fp, "%.*s", w, p);
				break;
			}
		}
		break;
	case FYET_ALIAS:
		alias = fy_token_get_text(fye->alias.anchor, &alias_len);
		fprintf(fp, " %s%.*s", "*", (int)alias_len, alias);
		break;
	}

	mbuf = fy_memstream_close(fyms, &msize);
	return mbuf;
}

struct fy_eventp *
fy_document_iterator_eventp_alloc(struct fy_document_iterator *fydi)
{
	struct fy_eventp *fyep = NULL;

	if (!fydi)
		return NULL;

	if (fydi->recycled_eventp_list)
		fyep = fy_eventp_list_pop(fydi->recycled_eventp_list);
	if (!fyep)
		fyep = fy_eventp_alloc();
	if (!fyep)
		return NULL;

	fyep->e.type = FYET_NONE;

	return fyep;
}

void fy_document_iterator_eventp_clean(struct fy_document_iterator *fydi, struct fy_eventp *fyep)
{
	if (!fydi || !fyep)
		return;

	fy_eventp_clean_rl(fydi->recycled_token_list, fyep);
}

void fy_document_iterator_eventp_recycle(struct fy_document_iterator *fydi, struct fy_eventp *fyep)
{
	if (!fydi || !fyep)
		return;

	/* clean, safe to do */
	fy_document_iterator_eventp_clean(fydi, fyep);

	if (fydi->recycled_eventp_list)
		fy_eventp_list_push(fydi->recycled_eventp_list, fyep);
	else
		fy_eventp_free(fyep);
}

struct fy_event *
fy_document_iterator_event_vcreate(struct fy_document_iterator *fydi, enum fy_event_type type, va_list ap)
{
	struct fy_eventp *fyep;

	if (!fydi)
		return NULL;

	fyep = fy_eventp_vcreate_internal(fydi->recycled_eventp_list,
			fydi->fyd ? fydi->fyd->diag : NULL,
			fydi->fyd ? fydi->fyd->fyds : NULL,
			type, ap);
	if (!fyep)
		return NULL;

	return &fyep->e;
}

struct fy_event *
fy_document_iterator_event_create(struct fy_document_iterator *fydi, enum fy_event_type type, ...)
{
	struct fy_event *fye;
	va_list ap;

	va_start(ap, type);
	fye = fy_document_iterator_event_vcreate(fydi, type, ap);
	va_end(ap);

	return fye;
}

void fy_document_iterator_event_free(struct fy_document_iterator *fydi, struct fy_event *fye)
{
	struct fy_eventp *fyep;

	if (!fydi || !fye)
		return;

	fyep = container_of(fye, struct fy_eventp, e);

	fy_document_iterator_eventp_recycle(fydi, fyep);
}

const char *
fy_event_get_comments(struct fy_event *fye)
{
	struct fy_token *fyt;

	if (!fye)
		return NULL;

	fyt = fy_event_get_token(fye);
	if (!fyt)
		return NULL;

	return fy_token_get_comments(fyt);
}

struct fy_eventp *
fy_generic_iterator_eventp_alloc(struct fy_generic_iterator *fygi)
{
	struct fy_eventp *fyep = NULL;

	if (!fygi)
		return NULL;

	if (fygi->recycled_eventp_list)
		fyep = fy_eventp_list_pop(fygi->recycled_eventp_list);
	if (!fyep)
		fyep = fy_eventp_alloc();
	if (!fyep)
		return NULL;

	fyep->e.type = FYET_NONE;

	return fyep;
}

void fy_generic_iterator_eventp_clean(struct fy_generic_iterator *fygi, struct fy_eventp *fyep)
{
	if (!fygi || !fyep)
		return;

	fy_eventp_clean_rl(fygi->recycled_token_list, fyep);
}

void fy_generic_iterator_eventp_recycle(struct fy_generic_iterator *fygi, struct fy_eventp *fyep)
{
	if (!fygi || !fyep)
		return;

	/* clean, safe to do */
	fy_generic_iterator_eventp_clean(fygi, fyep);

	if (fygi->recycled_eventp_list)
		fy_eventp_list_push(fygi->recycled_eventp_list, fyep);
	else
		fy_eventp_free(fyep);
}

struct fy_event *
fy_generic_iterator_event_vcreate(struct fy_generic_iterator *fygi, enum fy_event_type type, va_list ap)
{
	struct fy_eventp *fyep;

	if (!fygi)
		return NULL;

	fyep = fy_eventp_vcreate_internal(fygi->recycled_eventp_list,
			NULL,
			NULL,	/* fyds */
			type, ap);
	if (!fyep)
		return NULL;

	return &fyep->e;
}

struct fy_event *
fy_generic_iterator_event_create(struct fy_generic_iterator *fygi, enum fy_event_type type, ...)
{
	struct fy_event *fye;
	va_list ap;

	va_start(ap, type);
	fye = fy_generic_iterator_event_vcreate(fygi, type, ap);
	va_end(ap);

	return fye;
}

void fy_generic_iterator_event_free(struct fy_generic_iterator *fygi, struct fy_event *fye)
{
	struct fy_eventp *fyep;

	if (!fygi || !fye)
		return;

	fyep = container_of(fye, struct fy_eventp, e);

	fy_generic_iterator_eventp_recycle(fygi, fyep);
}
