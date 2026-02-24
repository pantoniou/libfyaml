/*
 * fy-token.c - YAML token methods
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
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

#include "fy-ctype.h"
#include "fy-utf8.h"
#include "fy-emit-accum.h"

#include "fy-walk.h"

#include "fy-token.h"

enum fy_scalar_style fy_token_scalar_style(struct fy_token *fyt)
{
	return fy_token_scalar_style_inline(fyt);
}

enum fy_collection_style fy_token_collection_style(struct fy_token *fyt)
{
	if (!fyt)
		return FYCS_ANY;

	switch (fyt->type) {
	case FYTT_FLOW_SEQUENCE_START:
	case FYTT_FLOW_SEQUENCE_END:
	case FYTT_FLOW_MAPPING_START:
	case FYTT_FLOW_MAPPING_END:
		return FYCS_FLOW;

	case FYTT_BLOCK_SEQUENCE_START:
	case FYTT_BLOCK_MAPPING_START:
	case FYTT_BLOCK_END:
		return FYCS_BLOCK;

	default:
		break;
	}

	return FYCS_ANY;
}

enum fy_token_type fy_token_get_type(struct fy_token *fyt)
{
	return fy_token_get_type_inline(fyt);
}

void fy_token_clean_rl(struct fy_token_list *fytl, struct fy_token *fyt)
{
	struct fy_token_comment *tk;

	if (!fyt)
		return;

	/* release reference */
	fy_input_unref(fyt->handle.fyi);
	fyt->handle.fyi = NULL;

	/* release the token comments */
	while ((tk = fyt->token_comment) != NULL) {
		fyt->token_comment = tk->next;
		fy_input_unref(tk->handle.fyi);
		fy_atom_reset(&tk->handle);
		if (tk->comment)
			free(tk->comment);
		free(tk);
	}

	if (fyt->comments) {
		free(fyt->comments);
		fyt->comments = NULL;
	}

	switch (fyt->type) {
	case FYTT_TAG:
		fy_token_unref(fyt->tag.fyt_td);
		fyt->tag.fyt_td = NULL;
		if (fyt->tag.handle0) {
			free(fyt->tag.handle0);
			fyt->tag.handle0 = NULL;
		}
		if (fyt->tag.suffix0) {
			free(fyt->tag.suffix0);
			fyt->tag.suffix0 = NULL;
		}
		if (fyt->tag.short0) {
			free(fyt->tag.short0);
			fyt->tag.short0 = NULL;
		}
		break;

	case FYTT_TAG_DIRECTIVE:
		if (fyt->tag_directive.prefix0) {
			free(fyt->tag_directive.prefix0);
			fyt->tag_directive.prefix0 = NULL;
		}
		if (fyt->tag_directive.handle0) {
			free(fyt->tag_directive.handle0);
			fyt->tag_directive.handle0 = NULL;
		}
		break;

	case FYTT_PE_MAP_KEY:
		fy_document_destroy(fyt->map_key.fyd);
		fyt->map_key.fyd = NULL;
		break;

	case FYTT_SCALAR:
		if (fyt->scalar.path_key_storage) {
			free(fyt->scalar.path_key_storage);
			fyt->scalar.path_key_storage = NULL;
		}
		break;

	case FYTT_ALIAS:
		if (fyt->alias.expr) {
			fy_path_expr_free(fyt->alias.expr);
			fyt->alias.expr = NULL;
		}
		break;

	default:
		break;
	}

	if (fyt->text0) {
		free(fyt->text0);
		fyt->text0 = NULL;
	}

	fyt->type = FYTT_NONE;
	memset(&fyt->analysis, 0, sizeof(fyt->analysis));
	fyt->text_len = 0;
	fyt->text = NULL;
}

void fy_token_list_unref_all_rl(struct fy_token_list *fytl, struct fy_token_list *fytl_tofree)
{
	struct fy_token *fyt;

	while ((fyt = fy_token_list_pop(fytl_tofree)) != NULL)
		fy_token_unref_rl(fytl, fyt);
}

static bool fy_token_text_needs_rebuild(struct fy_token *fyt)
{
	const struct fy_atom *fya;

	if (!fy_token_text_is_direct(fyt))
		return false;

	fya = fy_token_atom(fyt);
	if (!fya || !fya->fyi)
		return false;

	return fya->fyi_generation != fya->fyi->generation;
}

#define O_CPY(_src, _len) \
	do { \
		size_t _l = (_len); \
		if (o && _l) { \
			size_t _cl = _l; \
			if (_cl > (size_t)(oe - o)) \
				_cl = (size_t)(oe - o); \
			memcpy(o, (_src), _cl); \
			o += _cl; \
		} \
		len += _l; \
	} while(0)

static size_t fy_tag_token_format_internal(const struct fy_token *fyt, void *out, size_t *outszp)
{
	char *o = NULL, *oe = NULL;
	size_t outsz;
	const char *handle, *suffix;
	size_t len, rlen, handle_size, suffix_size;
	int code_length;
	uint8_t code[4];
	const char *t, *s, *e;

	if (!fyt || fyt->type != FYTT_TAG)
		return 0;

	if (out && *outszp <= 0)
		return 0;

	if (out) {
		outsz = *outszp;
		o = out;
		oe = (char *)out + outsz;
	}

	if (!fyt->tag.fyt_td)
		return 0;

	handle = fy_tag_directive_token_prefix(fyt->tag.fyt_td, &handle_size);
	if (!handle)
		return 0;

	suffix = fy_atom_data(&fyt->handle) + fyt->tag.skip + fyt->tag.handle_length;
	suffix_size = fyt->tag.suffix_length;

	len = 0;
	O_CPY(handle, handle_size);

	/* escape suffix as a URI */
	s = suffix;
	e = s + suffix_size;
	while (s < e) {
		/* find next escape */
		t = memchr(s, '%', e - s);
		rlen = (size_t)((t ? t : e) - s);
		O_CPY(s, rlen);

		/* end of string */
		if (!t)
			break;
		s = t;

		code_length = sizeof(code);
		t = fy_uri_esc(s, e - s, code, &code_length);
		if (!t)
			break;

		/* output escaped utf8 */
		O_CPY(code, code_length);
		s = t;
	}

	return len;
}

size_t fy_tag_token_format_text_length(const struct fy_token *fyt)
{
	return fy_tag_token_format_internal(fyt, NULL, NULL);
}

const char *fy_tag_token_format_text(const struct fy_token *fyt, char *buf, size_t maxsz)
{
	if (maxsz > 0)
		buf[0] = '\0';
	fy_tag_token_format_internal(fyt, buf, &maxsz);
	return buf;
}

static size_t fy_tag_directive_token_format_internal(const struct fy_token *fyt,
			void *out, size_t *outszp)
{
	char *o = NULL, *oe = NULL;
	size_t outsz;
	size_t len;
	const char *handle, *prefix;
	size_t handle_size, prefix_size;

	if (!fyt || fyt->type != FYTT_TAG_DIRECTIVE)
		return 0;

	if (out && *outszp <= 0)
		return 0;

	if (out) {
		outsz = *outszp;
		o = out;
		oe = (char *)out + outsz;
	}

	len = 0;

	handle = fy_atom_data(&fyt->handle);
	handle_size = fy_atom_size(&fyt->handle);

	prefix = handle + handle_size - fyt->tag_directive.uri_length;
	prefix_size = fyt->tag_directive.uri_length;
	handle_size = fyt->tag_directive.tag_length;

	if (handle_size)
		O_CPY(handle, handle_size);
	else
		O_CPY("!<", 2);
	O_CPY(prefix, prefix_size);
	if (!handle_size)
		O_CPY(">", 1);

	return len;
}
#undef O_CPY

size_t fy_tag_directive_token_format_text_length(const struct fy_token *fyt)
{
	return fy_tag_directive_token_format_internal(fyt, NULL, NULL);
}

const char *fy_tag_directive_token_format_text(const struct fy_token *fyt, char *buf, size_t maxsz)
{
	fy_tag_directive_token_format_internal(fyt, buf, &maxsz);
	return buf;
}

const char *fy_tag_directive_token_prefix(struct fy_token *fyt, size_t *lenp)
{
	const char *ptr;
	size_t len;

	if (!fyt || fyt->type != FYTT_TAG_DIRECTIVE) {
		*lenp = 0;
		return NULL;
	}
	ptr = fy_atom_data(&fyt->handle);
	len = fy_atom_size(&fyt->handle);
	ptr = ptr + len - fyt->tag_directive.uri_length;
	*lenp = fyt->tag_directive.uri_length;

	return ptr;
}

const char *fy_tag_directive_token_prefix0(struct fy_token *fyt)
{
	char *text0;
	const char *text;
	size_t len;

	if (!fyt || fyt->type != FYTT_TAG_DIRECTIVE)
		return NULL;

	/* use the cache if it's there (and doesn't need a rebuild) */
	if (fyt->tag_directive.prefix0 && !fy_token_text_needs_rebuild(fyt))
		return fyt->tag_directive.prefix0;

	if (fyt->tag_directive.prefix0) {
		free(fyt->tag_directive.prefix0);
		fyt->tag_directive.prefix0 = NULL;
	}

	text = fy_tag_directive_token_prefix(fyt, &len);
	if (!text)
		return NULL;

	text0 = malloc(len + 1);
	if (!text0)
		return NULL;
	memcpy(text0, text, len);
	text0[len] = '\0';

	fyt->tag_directive.prefix0 = text0;

	return fyt->tag_directive.prefix0;
}

const char *fy_tag_directive_token_handle(struct fy_token *fyt, size_t *lenp)
{
	const char *ptr;

	if (!fyt || fyt->type != FYTT_TAG_DIRECTIVE) {
		*lenp = 0;
		return NULL;
	}
	ptr = fy_atom_data(&fyt->handle);
	*lenp = fyt->tag_directive.tag_length;
	return ptr;
}

const char *fy_tag_directive_token_handle0(struct fy_token *fyt)
{
	char *text0;
	const char *text;
	size_t len;

	if (!fyt || fyt->type != FYTT_TAG_DIRECTIVE)
		return NULL;

	/* use the cache if it's there (and doesn't need a rebuild) */
	if (fyt->tag_directive.handle0 && !fy_token_text_needs_rebuild(fyt))
		return fyt->tag_directive.handle0;

	if (fyt->tag_directive.handle0) {
		free(fyt->tag_directive.handle0);
		fyt->tag_directive.handle0 = NULL;
	}

	text = fy_tag_directive_token_handle(fyt, &len);
	if (!text)
		return NULL;

	text0 = malloc(len + 1);
	if (!text0)
		return NULL;
	memcpy(text0, text, len);
	text0[len] = '\0';

	fyt->tag_directive.handle0 = text0;

	return fyt->tag_directive.handle0;
}

struct fy_token *fy_token_vcreate_rl(struct fy_token_list *fytl, enum fy_token_type type, va_list ap)
{
	struct fy_token *fyt = NULL;
	struct fy_atom *handle;
	struct fy_token *fyt_td;

	if ((unsigned int)type >= FYTT_COUNT)
		goto err_out;

	fyt = fy_token_alloc_rl(fytl);
	if (!fyt)
		goto err_out;
	fyt->type = type;
	fyt->handle.fyi = NULL;

	handle = va_arg(ap, struct fy_atom *);
	if (handle) {
		fyt->handle = *handle;
		fy_input_ref(fyt->handle.fyi);
	} else
		fy_atom_reset(&fyt->handle);

	switch (fyt->type) {
	case FYTT_TAG_DIRECTIVE:
		fyt->tag_directive.tag_length = va_arg(ap, unsigned int);
		fyt->tag_directive.uri_length = va_arg(ap, unsigned int);
		fyt->tag_directive.is_default = va_arg(ap, int) ? true : false;
		fyt->tag_directive.prefix0 = NULL;
		fyt->tag_directive.handle0 = NULL;
		break;
	case FYTT_SCALAR:
		fyt->scalar.path_key = NULL;
		fyt->scalar.path_key_len = 0;
		fyt->scalar.path_key_storage = NULL;
		fyt->scalar.is_null = false;	/* by default the scalar is not NULL */
		fyt->scalar.style = va_arg(ap, enum fy_scalar_style);
		/* by default it's the same as the content */
		fyt->scalar.style_start = handle->start_mark;
		fyt->scalar.style_end = handle->end_mark;
		if (fyt->scalar.style != FYSS_ANY && (unsigned int)fyt->scalar.style >= FYSS_MAX)
			goto err_out;
		break;
	case FYTT_TAG:
		fyt->tag.skip = va_arg(ap, unsigned int);
		fyt->tag.handle_length = va_arg(ap, unsigned int);
		fyt->tag.suffix_length = va_arg(ap, unsigned int);

		fyt_td = va_arg(ap, struct fy_token *);
		if (!fyt_td)
			goto err_out;
		fyt->tag.fyt_td = fy_token_ref(fyt_td);
		fyt->tag.handle0 = NULL;
		fyt->tag.suffix0 = NULL;
		fyt->tag.short0 = NULL;
		break;

	case FYTT_VERSION_DIRECTIVE:
		fyt->version_directive.vers = *va_arg(ap, struct fy_version *);
		break;

	case FYTT_ALIAS:
		fyt->alias.expr = va_arg(ap, struct fy_path_expr *);
		fyt->alias.style_start = fyt->handle.start_mark;
		break;

	case FYTT_KEY:
		fyt->key.flow_level = va_arg(ap, int);
		break;

	case FYTT_PE_MAP_KEY:
		fyt->map_key.fyd = va_arg(ap, struct fy_document *);
		break;

	case FYTT_PE_SEQ_INDEX:
		fyt->seq_index.index = va_arg(ap, int);
		break;

	case FYTT_PE_SEQ_SLICE:
		fyt->seq_slice.start_index = va_arg(ap, int);
		fyt->seq_slice.end_index = va_arg(ap, int);
		break;

	case FYTT_NONE:
		goto err_out;

	case FYTT_ANCHOR:
		fyt->anchor.style_start = fyt->handle.start_mark;
		break;

	default:
		break;
	}

	return fyt;

err_out:
	fy_token_unref(fyt);

	return NULL;
}

struct fy_token *fy_token_create_rl(struct fy_token_list *fytl, enum fy_token_type type, ...)
{
	struct fy_token *fyt;
	va_list ap;

	va_start(ap, type);
	fyt = fy_token_vcreate_rl(fytl, type, ap);
	va_end(ap);

	return fyt;
}

struct fy_token *fy_token_vcreate(enum fy_token_type type, va_list ap)
{
	return fy_token_vcreate_rl(NULL, type, ap);
}

struct fy_token *fy_token_create(enum fy_token_type type, ...)
{
	struct fy_token *fyt;
	va_list ap;

	va_start(ap, type);
	fyt = fy_token_vcreate_rl(NULL, type, ap);
	va_end(ap);

	return fyt;
}

struct fy_token *fy_parse_token_create(struct fy_parser *fyp, enum fy_token_type type, ...)
{
	struct fy_token *fyt;
	va_list ap;

	if (!fyp)
		return NULL;

	va_start(ap, type);
	fyt = fy_token_vcreate_rl(fyp->recycled_token_list, type, ap);
	va_end(ap);

	return fyt;
}

size_t fy_token_format_text_length(struct fy_token *fyt)
{
	ssize_t length;

	if (!fyt)
		return 0;

	switch (fyt->type) {

	case FYTT_TAG:
		return fy_tag_token_format_text_length(fyt);

	case FYTT_TAG_DIRECTIVE:
		return fy_tag_directive_token_format_text_length(fyt);

	default:
		break;
	}

	length = fy_atom_format_text_length(&fyt->handle);

	/* when something is seriously wrong cop out */
	if (length < 0)
		return 0;

	return (size_t)length;
}

const char *fy_token_format_text(struct fy_token *fyt, char *buf, size_t maxsz)
{
	const char *str;

	if (maxsz == 0)
		return buf;

	if (!fyt) {
		if (maxsz > 0)
			buf[0] = '\0';
		return buf;
	}

	switch (fyt->type) {

	case FYTT_TAG:
		return fy_tag_token_format_text(fyt, buf, maxsz);

	case FYTT_TAG_DIRECTIVE:
		return fy_tag_directive_token_format_text(fyt, buf, maxsz);

	default:
		break;
	}

	str = fy_atom_format_text(&fyt->handle, buf, maxsz);

	return str;
}

size_t fy_token_format_utf8_length(struct fy_token *fyt)
{
	const char *str;
	size_t len;

	if (!fyt)
		return 0;

	switch (fyt->type) {

	case FYTT_TAG:
	case FYTT_TAG_DIRECTIVE:
		str = fy_token_get_text(fyt, &len);
		if (!str)
			return 0;
		return fy_utf8_count(str, len);

	default:
		break;
	}

	return fy_atom_format_utf8_length(&fyt->handle);
}

struct fy_atom *fy_token_atom(struct fy_token *fyt)
{
	return fyt ? &fyt->handle : NULL;
}

const struct fy_mark *fy_token_start_mark(struct fy_token *fyt)
{
	const struct fy_atom *atom;

	atom = fy_token_atom(fyt);
	if (atom)
		return &atom->start_mark;

	/* something we don't track */
	return NULL;
}

const struct fy_mark *fy_token_end_mark(struct fy_token *fyt)
{
	const struct fy_atom *atom;

	atom = fy_token_atom(fyt);
	if (atom)
		return &atom->end_mark;

	/* something we don't track */
	return NULL;
}

const struct fy_token_analysis *
fy_token_text_analyze(struct fy_token *fyt)
{
	static const struct fy_token_analysis null_analysis = {
		.flags = FYTTAF_CAN_BE_SIMPLE_KEY | FYTTAF_DIRECT_OUTPUT |
		         FYTTAF_EMPTY | FYTTAF_CAN_BE_DOUBLE_QUOTED |
		         FYTTAF_ANALYZED,
		.maxspan = 0,
		.maxcol = 0,
	};
	if (!fyt)
		return &null_analysis;

	if (fyt->analysis.flags & FYTTAF_ANALYZED)
		return &fyt->analysis;

	/* only tokens that can generate text */
	if (fyt->type == FYTT_SCALAR || fyt->type == FYTT_TAG ||
	    fyt->type == FYTT_ANCHOR || fyt->type == FYTT_ALIAS) {
		fyt->analysis.flags = fy_atom_text_analyze(fy_token_atom(fyt), fy_token_atom_style(fyt),
				&fyt->analysis.maxspan, &fyt->analysis.maxcol);
	} else {
		fyt->analysis.flags = FYTTAF_NO_TEXT_TOKEN;
		fyt->analysis.maxspan = 0;
		fyt->analysis.maxcol = 0;
	}

	fyt->analysis.flags |= FYTTAF_ANALYZED;
	return &fyt->analysis;
}

const char *fy_tag_token_get_directive_handle(struct fy_token *fyt, size_t *td_handle_sizep)
{
	if (!fyt || fyt->type != FYTT_TAG || !fyt->tag.fyt_td)
		return NULL;

	return fy_tag_directive_token_handle(fyt->tag.fyt_td, td_handle_sizep);
}

const char *fy_tag_token_get_directive_prefix(struct fy_token *fyt, size_t *td_prefix_sizep)
{
	if (!fyt || fyt->type != FYTT_TAG || !fyt->tag.fyt_td)
		return NULL;

	return fy_tag_directive_token_prefix(fyt->tag.fyt_td, td_prefix_sizep);
}

const char *fy_token_get_direct_output(struct fy_token *fyt, size_t *sizep)
{
	const struct fy_atom *fya;

	fya = fy_token_atom(fyt);
	if (!fya || !fya->direct_output ||
	    (fyt->type == FYTT_TAG || fyt->type == FYTT_TAG_DIRECTIVE) ) {
		*sizep = 0;
		return NULL;
	}
	*sizep = fy_atom_size(fya);
	return fy_atom_data(fya);
}

const char *fy_token_get_direct_simple_output(struct fy_token *fyt, size_t *sizep)
{
	const struct fy_atom *handle;

	handle = fy_token_atom(fyt);

	if (!(handle->style == FYAS_PLAIN && handle->storage_hint_valid &&
	      handle->direct_output && !handle->high_ascii &&
	      !handle->has_lb && !handle->has_ws && !handle->empty)) {
		*sizep = 0;
		return NULL;
	}

	*sizep = fy_atom_size(handle);
	return fy_atom_data(handle);
}

const char *fy_tag_token_handle(struct fy_token *fyt, size_t *lenp)
{
	return fy_tag_token_get_directive_handle(fyt, lenp);
}

const char *fy_tag_token_suffix(struct fy_token *fyt, size_t *lenp)
{
	const char *tag, *prefix, *handle, *suffix;
	size_t tag_len, prefix_len, handle_len, suffix_len;

	if (!fyt || fyt->type != FYTT_TAG) {
		*lenp = 0;
		return NULL;
	}

	tag = fy_token_get_text(fyt, &tag_len);
	if (!tag)
		return NULL;
	prefix = fy_tag_token_get_directive_prefix(fyt, &prefix_len);
	if (!prefix)
		return NULL;
	handle = fy_tag_token_handle(fyt, &handle_len);
	if (!handle || !handle_len) {
		suffix = tag;
		suffix_len = tag_len;
	} else {
		assert(prefix_len <= tag_len);
		assert(tag_len >= prefix_len);
		suffix = tag + prefix_len;
		suffix_len = tag_len - prefix_len;
	}
	*lenp = suffix_len;
	return suffix;
}

const char *fy_tag_token_handle0(struct fy_token *fyt)
{
	char *text0;
	const char *text;
	size_t len;

	if (!fyt || fyt->type != FYTT_TAG)
		return NULL;

	/* use the cache if it's there (and doesn't need a rebuild) */
	if (fyt->tag.handle0 && !fy_token_text_needs_rebuild(fyt))
		return fyt->tag.handle0;

	if (fyt->tag.handle0) {
		free(fyt->tag.handle0);
		fyt->tag.handle0 = NULL;
	}

	text = fy_tag_token_handle(fyt, &len);
	if (!text)
		return NULL;

	text0 = malloc(len + 1);
	if (!text0)
		return NULL;
	memcpy(text0, text, len);
	text0[len] = '\0';

	fyt->tag.handle0 = text0;

	return fyt->tag.handle0;
}

const char *fy_tag_token_suffix0(struct fy_token *fyt)
{
	char *text0;
	const char *text;
	size_t len;

	if (!fyt || fyt->type != FYTT_TAG)
		return NULL;

	/* use the cache if it's there (and doesn't need a rebuild) */
	if (fyt->tag.suffix0 && !fy_token_text_needs_rebuild(fyt))
		return fyt->tag.suffix0;

	if (fyt->tag.suffix0) {
		free(fyt->tag.suffix0);
		fyt->tag.suffix0 = NULL;
	}

	text = fy_tag_token_suffix(fyt, &len);
	if (!text)
		return NULL;

	text0 = malloc(len + 1);
	if (!text0)
		return NULL;
	memcpy(text0, text, len);
	text0[len] = '\0';

	fyt->tag.suffix0 = text0;

	return fyt->tag.suffix0;
}

const char *fy_tag_token_short(struct fy_token *fyt, size_t *lenp)
{
	const char *handle, *suffix;
	size_t handle_len, suffix_len;
	char *text0;

	if (!fyt || fyt->type != FYTT_TAG)
		return NULL;

	/* use the cache if it's there (and doesn't need a rebuild) */
	if (fyt->tag.short0 && !fy_token_text_needs_rebuild(fyt))
		return fyt->tag.short0;

	if (fyt->tag.short0) {
		free(fyt->tag.short0);
		fyt->tag.short0 = NULL;
	}

	handle = fy_tag_token_handle(fyt, &handle_len);
	if (!handle)
		return NULL;

	suffix = fy_tag_token_suffix(fyt, &suffix_len);
	if (!suffix)
		return NULL;

	text0 = malloc(handle_len + suffix_len + 1);
	if (!text0)
		return NULL;
	memcpy(text0, handle, handle_len);
	memcpy(text0 + handle_len, suffix, suffix_len);
	text0[handle_len + suffix_len] = '\0';

	fyt->tag.short0 = text0;
	fyt->tag.short_length = (unsigned int)(handle_len + suffix_len);

	*lenp = fyt->tag.short_length;

	return fyt->tag.short0;
}

const char *fy_tag_token_short0(struct fy_token *fyt)
{
	const char *text;
	size_t len;

	text = fy_tag_token_short(fyt, &len);
	if (!text)
		return NULL;
	return text;
}

const struct fy_version * fy_version_directive_token_version(struct fy_token *fyt)
{
	if (!fyt || fyt->type != FYTT_VERSION_DIRECTIVE)
		return NULL;
	return &fyt->version_directive.vers;
}

static void fy_token_prepare_text(struct fy_token *fyt)
{
	size_t ret;

	assert(fyt);

	/* get text length of this token */
	ret = fy_token_format_text_length(fyt);

	/* no text on this token? */
	if (ret == 0) {
		fyt->text_len = 0;
		fyt->text = fyt->text0 = strdup("");
		return;
	}

	fyt->text0 = malloc(ret + 1);
	if (!fyt->text0) {
		fyt->text_len = 0;
		fyt->text = fyt->text0 = strdup("");
		return;
	}

	fyt->text0[0] = '\0';

	fyt->text_len = ret;

	fy_token_format_text(fyt, fyt->text0, ret + 1);
	fyt->text0[ret] = '\0';

	fyt->text_len = ret;
	fyt->text = fyt->text0;
}

const char *fy_token_get_text(struct fy_token *fyt, size_t *lenp)
{
	/* return empty */
	if (!fyt) {
		*lenp = 0;
		return "";
	}

	/* already found something */
	if (fyt->text && !fy_token_text_needs_rebuild(fyt)) {
		*lenp = fyt->text_len;
		return fyt->text;
	}

	/* try direct output first */
	fyt->text = fy_token_get_direct_output(fyt, &fyt->text_len);
	if (!fyt->text)
		fy_token_prepare_text(fyt);

	*lenp = fyt->text_len;
	return fyt->text;
}

const char *fy_token_get_text0(struct fy_token *fyt)
{
	/* return empty */
	if (!fyt)
		return "";

	/* created text is always zero terminated */
	if (!fyt->text0)
		fy_token_prepare_text(fyt);

	return fyt->text0;
}

size_t fy_token_get_text_length(struct fy_token *fyt)
{
	return fy_token_format_text_length(fyt);
}

enum comment_out_state {
	cos_normal,
	cos_lastnl,
	cos_lastnlhash,
	cos_lastnlhashspc,
};

const char *
fy_token_get_comment(struct fy_token *fyt, enum fy_comment_placement which)
{
	struct fy_token_comment *tk;
	struct fy_atom *handle;
	struct fy_atom_iter iter;
	const struct fy_iter_chunk *ic;
	char *s, *e;
	const char *ss, *ee;
	int c, w, ret, pass;
	size_t size;
	enum comment_out_state state;
	bool output;

	if (!fyt || (unsigned int)which >= fycp_max)
		return NULL;

	for (tk = fyt->token_comment; tk; tk = tk->next) {
		if (tk->placement == which && fy_atom_is_set(&tk->handle))
			break;
	}

	/* not found, we're done */
	if (!tk)
		return NULL;

	/* found and already generated */
	if (tk->comment)
		return tk->comment;

	handle = &tk->handle;

	s = e = NULL;
	size = 0;
	for (pass = 0; pass < 2; pass++) {

		if (pass) {
			tk->comment = malloc(size + 1);
			if (!tk->comment)
				return NULL;
			s = tk->comment;
			e = s + size + 1;
		}

		/* start expecting # */
		state = cos_lastnl;
		fy_atom_iter_start(handle, &iter);
		ic = NULL;
		while ((ic = fy_atom_iter_chunk_next(&iter, ic, &ret)) != NULL) {
			ss = ic->str;
			ee = ss + ic->len;

			while ((c = fy_utf8_get(ss, ee - ss, &w)) > 0) {

				output = true;
				switch (state) {
				case cos_normal:
					if (fy_is_lb_m(c, handle->lb_mode))
						state = cos_lastnl;
					break;

				case cos_lastnl:
					if (c == '#') {
						state = cos_lastnlhash;
						output = false;
						break;
					}
					state = cos_normal;
					break;

				case cos_lastnlhash:
					if (c == ' ') {
						state = cos_lastnlhashspc;
						output = false;
						break;
					}
					if (fy_is_lb_m(c, handle->lb_mode)) {
						state = cos_lastnl;
						output = true;
						break;
					}
					state = cos_normal;
					break;

				case cos_lastnlhashspc:
					state = cos_normal;
					break;
				}

				if (output) {
					if (s) {
						s = fy_utf8_put(s, (size_t)(e - s), c);
						if (!s)
							return NULL;
					}
					size += w;
				}

				ss += w;
			}
		}
		fy_atom_iter_finish(&iter);
	}

	assert(s);
	assert(s < e);
	*s = '\0';

	return tk->comment;
}

const char *
fy_token_get_comments(struct fy_token *fyt)
{
	enum fy_comment_placement placement;
	const char *str;
	char *accum, *new_accum;
	size_t size, accum_size, new_accum_size;
	bool has_nl;

	if (!fyt)
		return NULL;

	/* quick exit if no comments */
	if (!fyt->token_comment)
		return NULL;

	/* already have it generated? */
	if (fyt->comments)
		return fyt->comments;

	str = NULL;
	accum = NULL;
	accum_size = 0;
	for (placement = fycp_top; placement < fycp_max; placement++) {

		str = fy_token_get_comment(fyt, placement);
		if (!str)
			continue;

		size = strlen(str);
		has_nl = size > 0 && str[size-1] == '\n';
		new_accum_size = accum_size + size + !has_nl + 1;
		new_accum = realloc(accum, new_accum_size);
		if (!new_accum)
			goto err_out;
		accum = new_accum;
		memcpy(accum + accum_size, str, size);
		accum_size += size;
		if (!has_nl)
			accum[accum_size++] = '\n';
		accum[accum_size] = '\0';
	}
	return fyt->comments = accum;

err_out:
	if (accum)
		free(accum);
	return NULL;
}

int
fy_token_set_comment(struct fy_token *fyt, enum fy_comment_placement which,
		     const char *text, size_t len)
{
	struct fy_token_comment *tk, *tk_prev;
	struct fy_input *fyi = NULL;
	char *data = NULL;
	const char *s, *e;
	char utf8buf[FY_UTF8_MAX_WIDTH];
	int c, lastc, w;
	size_t sz;
	struct fy_atom *handle;
	struct fy_memstream *fyms = NULL;
	FILE *fp;

	if (!fyt || (unsigned int)which >= fycp_max)
		return -1;

	tk_prev = NULL;
	for (tk = fyt->token_comment; tk; tk = tk->next) {
		if (tk->placement == which)
			break;
		tk_prev = tk;
	}

	/* removal of comment */
	if (!text && tk) {
		if (!tk_prev)
			fyt->token_comment = tk->next;
		else
			tk_prev = tk->next;

		fy_input_unref(tk->handle.fyi);
		fy_atom_reset(&tk->handle);
		if (tk->comment)
			free(tk->comment);
		free(tk);
		return 0;
	}

	if (len == FY_NT)
		len = strlen(text);

	data = NULL;

	fyms = fy_memstream_open(&fp);
	if (!fyms)
		goto err_out;

	s = text;
	e = text + len;
	lastc = -1;
	fputs("# ", fp);
	while ((c = fy_utf8_get(s, e - s, &w)) > 0) {
		s += w;
		if (lastc == '\n')
			fputs("\n# ", fp);
		if (fy_token_is_lb(fyt, c)) {
			c = '\n';
		} else {
			assert((size_t)w <= sizeof(utf8buf));
			fy_utf8_put_unchecked(utf8buf, c);
			fwrite(utf8buf, 1, w, fp);
		}
		lastc = c;
	}

	data = fy_memstream_close(fyms, &sz);
	fyms = NULL;

	len = sz;

	handle = tk ? &tk->handle : fy_token_comment_handle(fyt, which, true);
	if (!handle)
		goto err_out;

	fy_input_unref(handle->fyi);
	fy_atom_reset(handle);

	fyi = fy_input_from_malloc_data(data, len, handle, true);
	if (!fyi)
		goto err_out;

	return 0;

err_out:
	if (fyms) {
		data = fy_memstream_close(fyms, &sz);
		fyms = NULL;
	}
	fy_input_unref(fyi);
	if (data)
		free(data);
	return -1;
}

const char *fy_token_get_scalar_path_key(struct fy_token *fyt, size_t *lenp)
{
	struct fy_atom *atom;
	struct fy_atom_iter iter;
	struct fy_emit_accum ea;	/* use an emit accumulator */
	uint8_t non_utf8[4];
	size_t non_utf8_len, k;
	int c, i, w, digit;
	const struct fy_token_analysis *ta;

	if (!fyt || fyt->type != FYTT_SCALAR) {
		*lenp = 0;
		return NULL;
	}

	/* was it cached? return */
	if (fyt->scalar.path_key) {
		*lenp = fyt->scalar.path_key_len;
		return fyt->scalar.path_key;
	}

	/* analyze the token */
	ta = fy_token_text_analyze(fyt);

	/* simple one? perfect */
	if ((ta->flags & FYTTAF_CAN_BE_UNQUOTED_PATH_KEY) == FYTTAF_CAN_BE_UNQUOTED_PATH_KEY) {
		fyt->scalar.path_key = fy_token_get_text(fyt, &fyt->scalar.path_key_len);
		*lenp = fyt->scalar.path_key_len;
		return fyt->scalar.path_key;
	}

	/* not possible, need to quote (and escape) */

	/* no atom? i.e. empty */
	atom = fy_token_atom(fyt);
	if (!atom) {
		fyt->scalar.path_key = "";
		fyt->scalar.path_key_len = 0;
		*lenp = 0;
		return fyt->scalar.path_key;
	}

	/* no inplace buffer; we will need the malloc'ed contents anyway */
	fy_emit_accum_init(&ea, NULL, 0, 0, fylb_cr_nl);

	fy_atom_iter_start(atom, &iter);
	fy_emit_accum_start(&ea, 0, fy_token_atom_lb_mode(fyt));

	/* output in quoted form */
	fy_emit_accum_utf8_put(&ea, '"');

	for (;;) {
		non_utf8_len = sizeof(non_utf8);
		c = fy_atom_iter_utf8_quoted_get(&iter, &non_utf8_len, non_utf8);
		if (c < 0)
			break;

		if (c == 0 && non_utf8_len > 0) {
			for (k = 0; k < non_utf8_len; k++) {
				c = (int)non_utf8[k] & 0xff;
				fy_emit_accum_utf8_put(&ea, '\\');
				fy_emit_accum_utf8_put(&ea, 'x');
				digit = ((unsigned int)c >> 4) & 15;
				fy_emit_accum_utf8_put(&ea,
						digit <= 9 ? ('0' + digit) : ('A' + digit - 10));
				digit = (unsigned int)c & 15;
				fy_emit_accum_utf8_put(&ea,
						digit <= 9 ? ('0' + digit) : ('A' + digit - 10));
			}
			continue;
		}

		if (!fy_is_printq(c) || c == '"' || c == '\\') {

			fy_emit_accum_utf8_put(&ea, '\\');

			switch (c) {

			/* common YAML & JSON escapes */
			case '\b':
				fy_emit_accum_utf8_put(&ea, 'b');
				break;
			case '\f':
				fy_emit_accum_utf8_put(&ea, 'f');
				break;
			case '\n':
				fy_emit_accum_utf8_put(&ea, 'n');
				break;
			case '\r':
				fy_emit_accum_utf8_put(&ea, 'r');
				break;
			case '\t':
				fy_emit_accum_utf8_put(&ea, 't');
				break;
			case '"':
				fy_emit_accum_utf8_put(&ea, '"');
				break;
			case '\\':
				fy_emit_accum_utf8_put(&ea, '\\');
				break;

			/* YAML only escapes */
			case '\0':
				fy_emit_accum_utf8_put(&ea, '0');
				break;
			case '\a':
				fy_emit_accum_utf8_put(&ea, 'a');
				break;
			case '\v':
				fy_emit_accum_utf8_put(&ea, 'v');
				break;
			case '\x1b':
				fy_emit_accum_utf8_put(&ea, 'e');
				break;
			case 0x85:
				fy_emit_accum_utf8_put(&ea, 'N');
				break;
			case 0xa0:
				fy_emit_accum_utf8_put(&ea, '_');
				break;
			case 0x2028:
				fy_emit_accum_utf8_put(&ea, 'L');
				break;
			case 0x2029:
				fy_emit_accum_utf8_put(&ea, 'P');
				break;

			default:
				/* any kind of binary value */
				if ((unsigned int)c <= 0xff) {
					fy_emit_accum_utf8_put(&ea, 'x');
					w = 2;
				} else if ((unsigned int)c <= 0xffff) {
					fy_emit_accum_utf8_put(&ea, 'u');
					w = 4;
				} else if ((unsigned int)c <= 0xffffffff) {
					fy_emit_accum_utf8_put(&ea, 'U');
					w = 8;
				}

				for (i = w - 1; i >= 0; i--) {
					digit = ((unsigned int)c >> (i * 4)) & 15;
					fy_emit_accum_utf8_put(&ea,
							digit <= 9 ? ('0' + digit) : ('A' + digit - 10));
				}
				break;
			}

			continue;
		}

		/* regular character */
		fy_emit_accum_utf8_put(&ea, c);
	}

	fy_atom_iter_finish(&iter);

	/* closing quote */
	fy_emit_accum_utf8_put(&ea, '"');

	fy_emit_accum_make_0_terminated(&ea);

	/* get the output (note it's now NULL terminated) */
	fyt->scalar.path_key_storage = fy_emit_accum_steal(&ea, &fyt->scalar.path_key_len);
	fyt->scalar.path_key = fyt->scalar.path_key_storage;
	fy_emit_accum_cleanup(&ea);

	*lenp = fyt->scalar.path_key_len;

	return fyt->scalar.path_key;
}

size_t fy_token_get_scalar_path_key_length(struct fy_token *fyt)
{
	const char *text;
	size_t len;

	text = fy_token_get_scalar_path_key(fyt, &len);
	if (!text)
		return 0;
	return len;
}

const char *fy_token_get_scalar_path_key0(struct fy_token *fyt)
{
	const char *text;
	size_t len;

	if (!fyt || fyt->type != FYTT_SCALAR) {
		return NULL;
	}

	/* storage is \0 terminated */
	if (fyt->scalar.path_key_storage)
		return fyt->scalar.path_key_storage;

	text = fyt->scalar.path_key;
	len = fyt->scalar.path_key_len;
	if (!text)
		text = fy_token_get_scalar_path_key(fyt, &len);

	/* something is catastrophically wrong */
	if (!text)
		return NULL;

	if (fyt->scalar.path_key_storage)
		return fyt->scalar.path_key_storage;

	fyt->scalar.path_key_storage = malloc(len + 1);
	if (!fyt->scalar.path_key_storage)
		return NULL;

	memcpy(fyt->scalar.path_key_storage, text, len);
	fyt->scalar.path_key_storage[len] = '\0';

	return fyt->scalar.path_key_storage;
}

unsigned int fy_analyze_scalar_content(const char *data, size_t size,
		bool json_mode, enum fy_lb_mode lb_mode, enum fy_flow_ws_mode fws_mode)
{
	const char *s, *e;
	int c, lastc, nextc, w, ww, col, break_run;
	unsigned int flags;
	bool first;

	if (!size)
		return FYACF_EMPTY | FYACF_FLOW_PLAIN | FYACF_BLOCK_PLAIN | FYACF_SIZE0;

	flags = FYACF_EMPTY | FYACF_BLOCK_PLAIN | FYACF_FLOW_PLAIN |
		FYACF_PRINTABLE | FYACF_SINGLE_QUOTED | FYACF_DOUBLE_QUOTED |
		FYACF_SIZE0 | FYACF_VALID_ANCHOR;

	s = data;
	e = data + size;

	/* if it ends in : can't be a plain */
	if (e > s && e[-1] == ':') {
		flags &= ~(FYACF_BLOCK_PLAIN | FYACF_FLOW_PLAIN);
		flags |= FYACF_ENDS_WITH_COLON;
	}

	col = 0;
	first = true;
	lastc = -1;
	break_run = 0;
	while (s < e && (c = fy_utf8_get(s, e - s, &w)) >= 0) {

		flags &= ~FYACF_SIZE0;

		lastc = c;

		if (first) {
			if (fy_is_ws(c))
				flags |= FYACF_STARTS_WITH_WS;
			else if (fy_is_generic_lb_m(c, lb_mode))
				flags |= FYACF_STARTS_WITH_LB;
			/* scalars starting with & or * must be quoted */
			if (c == '&' || c == '*')
				flags &= ~(FYACF_BLOCK_PLAIN | FYACF_FLOW_PLAIN);
			first = false;
		}
		nextc = fy_utf8_get(s + w, e - (s + w), &ww);

		/* anything other than white space or linebreak */
		if ((flags & FYACF_EMPTY) &&
		    !fy_is_ws(c) && !fy_is_generic_lb_m(c, lb_mode))
			flags &= ~FYACF_EMPTY;

		if ((flags & FYACF_VALID_ANCHOR) &&
		    (fy_utf8_strchr(",[]{}&*:", c) || fy_is_ws(c) ||
		     fy_is_any_lb(c) || fy_is_unicode_control(c) ||
		     fy_is_unicode_space(c)))
			flags &= ~FYACF_VALID_ANCHOR;

		/* linebreak */
		if (fy_is_generic_lb_m(c, lb_mode)) {
			flags |= FYACF_LB;
			if (!(flags & FYACF_CONSECUTIVE_LB) &&
			    fy_is_generic_lb_m(nextc, lb_mode))
				flags |= FYACF_CONSECUTIVE_LB;
			break_run++;
		} else
			break_run = 0;

		/* white space */
		if (!(flags & FYACF_WS) && fy_is_ws(c)) {
			flags |= FYACF_WS;
			flags &= ~FYACF_VALID_ANCHOR;
		}

		/* anything not printable (or \r, \n) */
		if ((flags & FYACF_PRINTABLE) &&
		    !fy_is_printq(c)) {
			flags &= ~FYACF_PRINTABLE;
			flags &= ~(FYACF_BLOCK_PLAIN | FYACF_FLOW_PLAIN |
				   FYACF_SINGLE_QUOTED | FYACF_VALID_ANCHOR);
		}

		/* check for document indicators (at column 0) */
		if (!(flags & FYACF_DOC_IND) &&
		    ((col == 0 && (e - s) >= 3 &&
		      (!strncmp(s, "---", 3) || !strncmp(s, "...", 3))))) {
			flags |= FYACF_DOC_IND;
			flags &= ~(FYACF_BLOCK_PLAIN | FYACF_FLOW_PLAIN | FYACF_VALID_ANCHOR);
		}

		/* comment indicator can't be present after a space or lb */
		/* : followed by blank can't be any plain */
		if ((flags & (FYACF_BLOCK_PLAIN | FYACF_FLOW_PLAIN)) &&
		    (((fy_is_blank(c) || fy_is_generic_lb_m(c, lb_mode)) && nextc == '#') ||
		     (c == ':' && fy_is_blankz_m(nextc, lb_mode))))
			flags &= ~(FYACF_BLOCK_PLAIN | FYACF_FLOW_PLAIN);

		/* : followed by flow markers can't be a plain in flow context */
		if ((flags & FYACF_FLOW_PLAIN) &&
		    (fy_utf8_strchr(",[]{}", c) || (c == ':' && fy_utf8_strchr(",[]{}", nextc))))
			flags &= ~FYACF_FLOW_PLAIN;

		/* , followed by space can't be a plain in flow context */
		if ((flags & FYACF_FLOW_PLAIN) && c == ',')
			flags &= ~FYACF_FLOW_PLAIN;

		if (!(flags & FYACF_JSON_ESCAPE) && !fy_is_json_unescaped(c))
			flags |= FYACF_JSON_ESCAPE;

		if (fy_is_generic_lb_m(c, lb_mode))
			col = 0;
		else
			col++;

		s += w;
	}

	/* this contains arbitrary binany values, mark it as such */
	if (s < e)
		return FYACF_DOUBLE_QUOTED;

	if (fy_is_ws(lastc))
		flags |= FYACF_ENDS_WITH_WS;
	else if (fy_is_generic_lb_m(lastc, lb_mode))
		flags |= FYACF_ENDS_WITH_LB;

	if (break_run > 1)
		flags |= FYACF_TRAILING_LB;

	/* if it's empty it can be plain */
	if (!(flags & FYACF_EMPTY) && (flags & (FYACF_STARTS_WITH_WS | FYACF_STARTS_WITH_LB |
		      FYACF_ENDS_WITH_WS | FYACF_ENDS_WITH_LB)))
		flags &= ~(FYACF_FLOW_PLAIN | FYACF_BLOCK_PLAIN);

	return flags;
}

char *fy_token_debug_text(struct fy_token *fyt)
{
	const char *typetxt;
	const char *text;
	char *buf;
	size_t length;
	int wlen;
	int rc __FY_DEBUG_UNUSED__;

	if (!fyt || !fy_token_type_is_valid(fyt->type)) {
		typetxt = "<NULL>";
		goto out;
	}
	typetxt = fy_token_type_txt[fyt->type];

	/* should never happen really */
	assert(typetxt);

out:
	text = fy_token_get_text(fyt, &length);

	wlen = length > 8 ? 8 : (int)length;

	rc = asprintf(&buf, "%s:%.*s%s", typetxt, wlen, text, wlen < (int)length ? "..." : "");
	assert(rc != -1);

	return buf;
}

int fy_token_memcmp(struct fy_token *fyt, const void *ptr, size_t len)
{
	const char *value = NULL;
	size_t tlen = 0;

	/* special zero length handling */
	if (len == 0 && fyt && fy_token_get_text_length(fyt) == 0)
		return 0;

	/* handle NULL cases */
	if (!fyt && (!ptr || !len))
		return 0;

	if (!fyt && (ptr || len))
		return -1;

	if (fyt && (!ptr || !len))
		return 1;

	/* those two are special */
	if (fyt->type == FYTT_TAG || fyt->type == FYTT_TAG_DIRECTIVE) {
		value = fy_token_get_text(fyt, &tlen);
		if (!value)
			return -1;
		return tlen == len ? memcmp(value, ptr, tlen) : tlen < len ? -1 : 1;
	}

	return fy_atom_memcmp(fy_token_atom(fyt), ptr, len);
}

int fy_token_strcmp(struct fy_token *fyt, const char *str)
{
	size_t len;

	len = str ? strlen(str) : 0;

	return fy_token_memcmp(fyt, str, len);
}

int fy_token_cmp(struct fy_token *fyt1, struct fy_token *fyt2)
{
	const char *t1, *t2;
	size_t l1, l2, l;
	int ret;
	bool aoa;	/* anchor or alias */

	/* handles both NULL */
	if (fyt1 == fyt2)
		return 0;

	/* fyt1 is null, 2 wins */
	if (!fyt1 && fyt2)
		return -1;

	/* fyt2 is null, 1 wins */
	if (fyt1 && !fyt2)
		return 1;

	/* special case for comparing anchors and aliases */
	aoa = (fyt1->type == FYTT_ANCHOR || fyt1->type == FYTT_ALIAS) &&
	      (fyt2->type == FYTT_ANCHOR || fyt2->type == FYTT_ALIAS);

	/* tokens with different types can't be equal */
	if (!aoa && fyt1->type != fyt2->type)
		return fyt2->type > fyt1->type ? -1 : 1;

	/* special case, these can't use the atom comparisons */
	if (fyt1->type == FYTT_TAG || fyt1->type == FYTT_TAG_DIRECTIVE) {
		t1 = fy_token_get_text(fyt1, &l1);
		t2 = fy_token_get_text(fyt2, &l2);
		l = l1 > l2 ? l2 : l1;
		ret = memcmp(t1, t2, l);
		if (ret)
			return ret;
		return l1 == l2 ? 0 : l2 > l1 ? -1 : 1;
	}

	/* just pass it to the atom comparison methods */
	return fy_atom_cmp(fy_token_atom(fyt1), fy_token_atom(fyt2));
}

void fy_token_iter_start(struct fy_token *fyt, struct fy_token_iter *iter)
{
	if (!iter)
		return;

	memset(iter, 0, sizeof(*iter));

	iter->unget_c = -1;

	if (!fyt)
		return;

	iter->fyt = fyt;

	/* TAG or TAG_DIRECTIVE may only work by getting the text */
	if (fyt->type == FYTT_TAG || fyt->type == FYTT_TAG_DIRECTIVE)
		iter->ic.str = fy_token_get_text(fyt, &iter->ic.len);
	else /* try the direct output next  */
		iter->ic.str = fy_token_get_direct_output(fyt, &iter->ic.len);

	/* got it */
	if (iter->ic.str) {
		memset(&iter->atom_iter, 0, sizeof(iter->atom_iter));
		return;
	}

	assert(fyt->type != FYTT_TAG && fyt->type != FYTT_TAG_DIRECTIVE);

	/* fall back to the atom iterator */
	fy_atom_iter_start(fy_token_atom(fyt), &iter->atom_iter);
}

void fy_token_iter_finish(struct fy_token_iter *iter)
{
	if (!iter)
		return;

	if (!iter->ic.str)
		fy_atom_iter_finish(&iter->atom_iter);
}

struct fy_token_iter *
fy_token_iter_create(struct fy_token *fyt)
{
	struct fy_token_iter *iter;

	iter = malloc(sizeof(*iter));
	if (!iter)
		return NULL;
	fy_token_iter_start(fyt, iter);
	return iter;
}

void fy_token_iter_destroy(struct fy_token_iter *iter)
{
	if (!iter)
		return;

	fy_token_iter_finish(iter);
	free(iter);
}

const struct fy_iter_chunk *fy_token_iter_peek_chunk(struct fy_token_iter *iter)
{
	if (!iter)
		return NULL;

	/* direct mode? */
	if (iter->ic.str)
		return &iter->ic;

	/* fallback to the atom iterator */
	return fy_atom_iter_peek_chunk(&iter->atom_iter);
}

void fy_token_iter_advance(struct fy_token_iter *iter, size_t len)
{
	if (!iter)
		return;

	/* direct mode? */
	if (iter->ic.str) {
		if (len > iter->ic.len)
			len = iter->ic.len;
		iter->ic.str += len;
		iter->ic.len -= len;
		return;
	}

	/* fallback to the atom iterator */
	fy_atom_iter_advance(&iter->atom_iter, len);
}

const struct fy_iter_chunk *
fy_token_iter_chunk_next(struct fy_token_iter *iter, const struct fy_iter_chunk *curr, int *errp)
{
	if (!iter)
		return NULL;

	if (errp)
		*errp = 0;

	/* first time in */
	if (!curr) {
		if (iter->ic.str)
			return iter->ic.len ? &iter->ic : NULL;
		return fy_atom_iter_chunk_next(&iter->atom_iter, NULL, errp);
	}

	/* direct, all consumed */
	if (curr == &iter->ic) {
		iter->ic.str += iter->ic.len;
		iter->ic.len = 0;
		return NULL;
	}

	/* fallback */
	return fy_atom_iter_chunk_next(&iter->atom_iter, curr, errp);
}

ssize_t fy_token_iter_read(struct fy_token_iter *iter, void *buf, size_t count)
{
	if (!iter || !buf)
		return -1;

	/* direct mode */
	if (iter->ic.str) {
		if (count > iter->ic.len)
			count = iter->ic.len;
		memcpy(buf, iter->ic.str, count);
		iter->ic.str += count;
		iter->ic.len -= count;
		return count;
	}

	return fy_atom_iter_read(&iter->atom_iter, buf, count);
}

int fy_token_iter_getc(struct fy_token_iter *iter)
{
	int c;

	if (!iter)
		return -1;

	/* first try the pushed ungetc */
	if (iter->unget_c >= 0) {
		c = iter->unget_c;
		/* unmatched getc/ungetc */
		if (fy_utf8_width(c) != 1)
			return -1;
		iter->unget_c = -1;
		return c;
	}

	/* direct mode */
	if (iter->ic.str) {
		if (!iter->ic.len)
			return -1;
		c = *iter->ic.str++;
		iter->ic.len--;
		return c;
	}

	return fy_atom_iter_getc(&iter->atom_iter);
}

int fy_token_iter_ungetc(struct fy_token_iter *iter, int c)
{
	if (!iter || c >= 0x80)
		return -1;

	if (iter->unget_c >= 0)
		return -1;

	if (c < 0) {
		iter->unget_c = -1;
		return 0;
	}
	iter->unget_c = c;
	return c;
}

int fy_token_iter_peekc(struct fy_token_iter *iter)
{
	int c;

	c = fy_token_iter_getc(iter);
	if (c == -1)
		return -1;

	return fy_token_iter_ungetc(iter, c);
}

int fy_token_iter_utf8_get(struct fy_token_iter *iter)
{
	int c, w, w1;

	if (!iter)
		return -1;

	/* first try the pushed ungetc */
	if (iter->unget_c >= 0) {
		c = iter->unget_c;
		iter->unget_c = -1;
		return c;
	}

	/* direct */
	if (iter->ic.str) {

		/* not even 1 octet */
		if (!iter->ic.len)
			return -1;

		/* get width by the first octet */
		w = fy_utf8_width_by_first_octet((uint8_t)*iter->ic.str);
		if (!w || (unsigned int)w > iter->ic.len)
			return -1;

		/* get the next character */
		c = fy_utf8_get(iter->ic.str, w, &w1);

		iter->ic.str += w;
		iter->ic.len -= w;

		return c;
	}

	return fy_atom_iter_utf8_get(&iter->atom_iter);
}

int fy_token_iter_utf8_unget(struct fy_token_iter *iter, int c)
{
	if (!iter)
		return -1;

	if (iter->unget_c >= 0)
		return -1;

	if (c < 0) {
		iter->unget_c = -1;
		return 0;
	}
	if (!fy_utf8_is_valid(c))
		return -1;

	iter->unget_c = c;
	return c;
}

int fy_token_iter_utf8_peek(struct fy_token_iter *iter)
{
	int c;

	c = fy_token_iter_utf8_get(iter);
	if (c == -1)
		return -1;

	return fy_token_iter_utf8_unget(iter, c);
}

enum fy_scalar_style
fy_scalar_token_get_style(struct fy_token *fyt)
{
	if (!fyt || fyt->type != FYTT_SCALAR)
		return FYSS_ANY;
	return fyt->scalar.style;
}

const struct fy_tag *fy_tag_token_tag(struct fy_token *fyt)
{
	if (!fyt || fyt->type != FYTT_TAG)
		return NULL;

	/* always refresh, should be relatively infrequent */
	fyt->tag.tag.handle = fy_tag_token_handle0(fyt);
	fyt->tag.tag.prefix = fy_tag_token_suffix0(fyt);

	return &fyt->tag.tag;
}

const struct fy_tag *
fy_tag_directive_token_tag(struct fy_token *fyt)
{
	if (!fyt || fyt->type != FYTT_TAG_DIRECTIVE)
		return NULL;

	/* always refresh, should be relatively infrequent */
	fyt->tag_directive.tag.handle = fy_tag_directive_token_handle0(fyt);
	fyt->tag_directive.tag.prefix = fy_tag_directive_token_prefix0(fyt);

	return &fyt->tag_directive.tag;
}

struct fy_atom *fy_token_comment_handle(struct fy_token *fyt, enum fy_comment_placement placement, bool alloc)
{
	struct fy_token_comment *tk;

	if (!fyt || (unsigned int)placement >= fycp_max)
		return NULL;

	for (tk = fyt->token_comment; tk; tk = tk->next) {
		if (tk->placement == placement)
			return &tk->handle;
	}

	if (!alloc)
		return NULL;

	tk = malloc(sizeof(*tk));
	if (!tk)
		return NULL;
	memset(tk, 0, sizeof(*tk));
	tk->placement = placement;

	/* simple list add */
	tk->next = fyt->token_comment;
	fyt->token_comment = tk;

	return &tk->handle;
}

bool fy_token_has_any_comment(struct fy_token *fyt)
{
	struct fy_token_comment *tk;

	if (!fyt)
		return false;

	for (tk = fyt->token_comment; tk; tk = tk->next) {
		if (fy_atom_is_set(&tk->handle))
			return true;
	}
	return false;
}

bool
fy_token_scalar_is_null(struct fy_token *fyt)
{
	return !fyt || fyt->type != FYTT_SCALAR || fyt->scalar.is_null;
}

const struct fy_mark *
fy_token_style_start_mark(struct fy_token *fyt)
{
	if (!fyt)
		return NULL;

	switch (fyt->type) {
	case FYTT_SCALAR:
		return &fyt->scalar.style_start;
	case FYTT_ALIAS:
		return &fyt->alias.style_start;
	case FYTT_ANCHOR:
		return &fyt->anchor.style_start;
	default:
		break;
	}
	return fy_token_start_mark(fyt);
}

const struct fy_mark *
fy_token_style_end_mark(struct fy_token *fyt)
{
	if (!fyt)
		return NULL;

	if (fyt->type != FYTT_SCALAR)
		return fy_token_end_mark(fyt);
	return &fyt->scalar.style_end;
}

struct fy_atom *
fy_token_get_style_atom(struct fy_token *fyt, struct fy_atom *dst_handle)
{
	const struct fy_atom *handle;
	const struct fy_mark *mark;

	if (!fyt || !dst_handle)
		return NULL;

	handle = fy_token_atom(fyt);
	if (!handle)
		return NULL;
	*dst_handle = *handle;
	fy_input_ref(dst_handle->fyi);
	mark = fy_token_style_start_mark(fyt);
	if (mark)
		dst_handle->start_mark = *mark;
	mark = fy_token_style_end_mark(fyt);
	if (mark)
		dst_handle->end_mark = *mark;

	return dst_handle;
}
