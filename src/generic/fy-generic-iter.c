/*
 * fy-generic-iter.c - the generic iterator
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdalign.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

#include <stdio.h>

/* for container_of */
#include "fy-list.h"
#include "fy-utf8.h"

#include "fy-allocator.h"
#include "fy-thread.h"

#include "fy-generic.h"

#include "fy-docstate.h"
#include "fy-input.h"

int fy_generic_iterator_setup(struct fy_generic_iterator *fygi, const struct fy_generic_iterator_cfg *cfg)
{
	memset(fygi, 0, sizeof(*fygi));

	if (cfg)
		fygi->cfg = *cfg;

	fygi->state = FYGIS_WAITING_STREAM_START;
	fygi->vds = fy_invalid;
	fygi->iterate_root = fy_invalid;
	fygi->idx = (size_t)-1;

	/* suppress recycling if we must */
	fygi->suppress_recycling_force = getenv("FY_VALGRIND") && !getenv("FY_VALGRIND_RECYCLING");
	fygi->suppress_recycling = fygi->suppress_recycling_force;

	fy_eventp_list_init(&fygi->recycled_eventp);
	fy_token_list_init(&fygi->recycled_token);

	if (!fygi->suppress_recycling) {
		fygi->recycled_eventp_list = &fygi->recycled_eventp;
		fygi->recycled_token_list = &fygi->recycled_token;
	} else {
		fygi->recycled_eventp_list = NULL;
		fygi->recycled_token_list = NULL;
	}

	/* start with the stack pointing to the in place data */
	fygi->stack_top = (unsigned int)-1;
	fygi->stack_alloc = sizeof(fygi->in_place) / sizeof(fygi->in_place[0]);
	fygi->stack = fygi->in_place;

	/* set generator state accordingly */
	fygi->generator_state = 0;

	/* without configuration, is the default */
	if (!cfg)
		return 0;

	/* set the generator flags accordingly */
	switch (fygi->cfg.flags & FYGICF_WANT_MASK) {
	case FYGICF_WANT_STREAM_DOCUMENT_BODY_EVENTS:
		fygi->generator_state |= FYGIGF_WANTS_STREAM | FYGIGF_WANTS_DOC | FYGIGF_ENDS_AFTER_DOC;
		break;
	case FYGICF_WANT_DOCUMENT_BODY_EVENTS:
		fygi->generator_state |= FYGIGF_WANTS_DOC | FYGIGF_ENDS_AFTER_DOC;
		fygi->state = FYGIS_WAITING_DOCUMENT_START;
		break;
	case FYGICF_WANT_BODY_EVENTS:
		fygi->generator_state |= FYGIGF_ENDS_AFTER_BODY;
		fygi->state = FYGIS_WAITING_BODY_START_OR_DOCUMENT_END;
	default:
		break;
	}

	return 0;
}

void fy_generic_iterator_cleanup(struct fy_generic_iterator *fygi)
{
	struct fy_token *fyt;
	struct fy_eventp *fyep;

	fy_document_state_unref(fygi->fyds);
	fygi->fyds = NULL;

	/* free the stack if it's not the inplace one */
	if (fygi->stack != fygi->in_place)
		free(fygi->stack);
	fygi->stack_top = (unsigned int)-1;
	fygi->stack_alloc = sizeof(fygi->in_place) / sizeof(fygi->in_place[0]);
	fygi->stack = fygi->in_place;

	while ((fyt = fy_token_list_pop(&fygi->recycled_token)) != NULL)
		fy_token_free(fyt);

	while ((fyep = fy_eventp_list_pop(&fygi->recycled_eventp)) != NULL)
		fy_eventp_free(fyep);

	fygi->state = FYGIS_WAITING_STREAM_START;
	fygi->vds = fy_invalid;
	fygi->iterate_root = fy_invalid;
	fygi->idx = 0;
	fygi->count = 0;
}

struct fy_generic_iterator *
fy_generic_iterator_create_cfg(const struct fy_generic_iterator_cfg *cfg)
{
	struct fy_generic_iterator *fygi = NULL;
	int rc;

	fygi = malloc(sizeof(*fygi));
	if (!fygi)
		goto err_out;

	rc = fy_generic_iterator_setup(fygi, cfg);
	if (rc)
		goto err_out;

	return fygi;

err_out:
	fy_generic_iterator_destroy(fygi);
	return NULL;
}

struct fy_generic_iterator *fy_generic_iterator_create(void)
{
	return fy_generic_iterator_create_cfg(NULL);
}

void fy_generic_iterator_destroy(struct fy_generic_iterator *fygi)
{
	if (!fygi)
		return;
	fy_generic_iterator_cleanup(fygi);
	free(fygi);
}

static struct fy_token *
fygi_create_token(struct fy_generic_iterator *fygi, fy_generic v, enum fy_token_type type)
{
	if (!fygi)
		return NULL;

	return fy_document_state_generic_create_token(fygi->fyds, v, type);
}

static struct fy_event *
fygi_event_create(struct fy_generic_iterator *fygi, fy_generic v, bool start)
{
	struct fy_eventp *fyep;
	struct fy_event *fye;
	struct fy_token *fyt = NULL;
	struct fy_token *tag = NULL;
	struct fy_token *anchor = NULL;
	fy_generic vtag, vanchor, vcomment;
	enum fy_generic_type type;
	enum fy_token_type ttype;
	int rc;

	fyep = fy_generic_iterator_eventp_alloc(fygi);
	if (!fyep) {
		fygi->state = FYGIS_ERROR;
		return NULL;
	}
	fye = &fyep->e;

	type = fy_generic_get_type(v);

	vanchor = fy_invalid;
	vtag = fy_invalid;
	vcomment = fy_invalid;

	if (fy_generic_is_indirect(v)) {

		if (!(fygi->cfg.flags & FYGICF_STRIP_LABELS))
			vanchor = fy_generic_indirect_get_anchor(v);
		if (!(fygi->cfg.flags & FYGICF_STRIP_TAGS))
			vtag = fy_generic_indirect_get_tag(v);
		if (!(fygi->cfg.flags & FYGICF_STRIP_COMMENTS))
			vcomment = fy_generic_indirect_get_comment(v);
	}

	if (fy_generic_is_string(vanchor))
		anchor = fygi_create_token(fygi, vanchor, FYTT_ANCHOR);

	if (fy_generic_is_string(vtag))
		tag = fygi_create_token(fygi, vtag, FYTT_TAG);

	fyt = NULL;
	if (fy_generic_type_is_scalar(type) || type == FYGT_ALIAS) {

		fyt = fygi_create_token(fygi, v, type != FYGT_ALIAS ? FYTT_SCALAR : FYTT_ALIAS);
		if (!fyt)
			goto err_out;

	} else if (fy_generic_type_is_collection(type) && fy_generic_is_valid(vcomment)) {

		ttype = type == FYGT_SEQUENCE ?
			(start ? FYTT_FLOW_SEQUENCE_START : FYTT_FLOW_SEQUENCE_END) :
			(start ? FYTT_FLOW_MAPPING_START : FYTT_FLOW_MAPPING_END);
		fyt = fy_token_create(ttype, NULL, 0);
		if (!fyt)
			goto err_out;
	}

	if (fyt && fy_generic_is_string(vcomment)) {
		rc = fy_token_set_comment(fyt, fycp_top, fy_cast(vcomment, ""), FY_NT);
		if (rc)
			goto err_out;
	}

	switch (type) {

	case FYGT_NULL:
	case FYGT_BOOL:
	case FYGT_INT:
	case FYGT_FLOAT:
	case FYGT_STRING:
	case FYGT_ALIAS:

		if (type != FYGT_ALIAS) {
			fye->type = FYET_SCALAR;
			fye->scalar.anchor = anchor;
			fye->scalar.tag = tag;
			fye->scalar.value = fyt;
			anchor = tag = fyt = NULL;
		} else {
			fye->type = FYET_ALIAS;
			fye->alias.anchor = fyt;
		}
		fyt = NULL;
		break;


	case FYGT_SEQUENCE:
		if (start) {
			fye->type = FYET_SEQUENCE_START;
			fye->sequence_start.anchor = anchor;
			fye->sequence_start.tag = tag;
			fye->sequence_start.sequence_start = fyt;
			anchor = tag = NULL;
		} else {
			fye->type = FYET_SEQUENCE_END;
			fye->sequence_end.sequence_end = fyt;
		}
		fyt = NULL;
		break;

	case FYGT_MAPPING:
		if (start) {
			fye->type = FYET_MAPPING_START;
			fye->mapping_start.anchor = anchor;
			fye->mapping_start.tag = tag;
			fye->mapping_start.mapping_start = fyt;
			anchor = tag = NULL;
		} else {
			fye->type = FYET_MAPPING_END;
			fye->mapping_end.mapping_end = fyt;
		}
		fyt = NULL;
		break;

	default:
		return NULL;
	}

	fy_token_unref(anchor);
	fy_token_unref(tag);
	return fye;

err_out:
	fy_token_unref(fyt);
	fy_token_unref(tag);
	fy_token_unref(anchor);
	return NULL;
}

struct fy_event *
fy_generic_iterator_stream_start(struct fy_generic_iterator *fygi)
{
	struct fy_event *fye;

	if (!fygi || fygi->state == FYGIS_ERROR)
		return NULL;

	/* both none and stream start are the same for this */
	if (fygi->state != FYGIS_WAITING_STREAM_START &&
	    fygi->state != FYGIS_WAITING_STREAM_END_OR_DOCUMENT_START)
		goto err_out;

	fye = fy_generic_iterator_event_create(fygi, FYET_STREAM_START);
	if (!fye)
		goto err_out;

	fygi->state = FYGIS_WAITING_DOCUMENT_START;
	return fye;

err_out:
	fygi->state = FYGIS_ERROR;
	return NULL;
}

struct fy_event *
fy_generic_iterator_stream_end(struct fy_generic_iterator *fygi)
{
	struct fy_event *fye;

	if (!fygi || fygi->state == FYGIS_ERROR)
		return NULL;

	if (fygi->state != FYGIS_WAITING_STREAM_END_OR_DOCUMENT_START &&
	    fygi->state != FYGIS_WAITING_DOCUMENT_START)
		goto err_out;

	fye = fy_generic_iterator_event_create(fygi, FYET_STREAM_END);
	if (!fye)
		goto err_out;

	fygi->state = FYGIS_WAITING_STREAM_START;
	return fye;

err_out:
	fygi->state = FYGIS_ERROR;
	return NULL;
}

struct fy_event *
fy_generic_iterator_document_start_internal(struct fy_generic_iterator *fygi)
{
	struct fy_event *fye = NULL;
	struct fy_eventp *fyep;

	if (!fygi || fygi->state == FYGIS_ERROR)
		return NULL;

	/* we can transition to document start only from document start or stream end */
	if (fygi->state != FYGIS_WAITING_DOCUMENT_START &&
	    fygi->state != FYGIS_WAITING_STREAM_END_OR_DOCUMENT_START)
		goto err_out;

	fyep = fy_generic_iterator_eventp_alloc(fygi);
	if (!fyep)
		goto err_out;

	fye = &fyep->e;

	/* suppress recycling if we must */
	fygi->suppress_recycling = fygi->suppress_recycling_force;

	if (!fygi->suppress_recycling) {
		fygi->recycled_eventp_list = &fygi->recycled_eventp;
		fygi->recycled_token_list = &fygi->recycled_token;
	} else {
		fygi->recycled_eventp_list = NULL;
		fygi->recycled_token_list = NULL;
	}

	fye->type = FYET_DOCUMENT_START;
	fye->document_start.document_start = NULL;
	fye->document_start.document_state = fy_document_state_ref(fygi->fyds);
	fye->document_start.implicit = true;

	/* and go into body */
	fygi->state = FYGIS_WAITING_BODY_START_OR_DOCUMENT_END;

	return fye;

err_out:
	fy_generic_iterator_event_free(fygi, fye);
	fygi->state = FYGIS_ERROR;
	return NULL;
}

struct fy_event *
fy_generic_iterator_document_start(struct fy_generic_iterator *fygi, fy_generic vds)
{
	if (!fygi || fy_generic_dir_get_document_count(vds) <= 0)
		goto err_out;

	fygi->vds = vds;
	if (fy_generic_is_invalid(fygi->vds))
		goto err_out;
	fygi->iterate_root = fy_generic_vds_get_root(fygi->vds);
	if (fy_generic_is_invalid(fygi->iterate_root))
		goto err_out;

	/* it's OK if it's NULL */
	fygi->fyds = fy_generic_vds_get_document_state(fygi->vds);
	if (!fygi->fyds)
		goto err_out;

	return fy_generic_iterator_document_start_internal(fygi);

err_out:
	return NULL;
}

struct fy_event *
fy_generic_iterator_document_end(struct fy_generic_iterator *fygi)
{
	struct fy_event *fye;

	if (!fygi || fygi->state == FYGIS_ERROR)
		return NULL;

	if (fy_generic_is_invalid(fygi->vds) || fygi->state != FYGIS_WAITING_DOCUMENT_END)
		goto err_out;

	fye = fy_generic_iterator_event_create(fygi, FYET_DOCUMENT_END, (int)1);
	if (!fye)
		goto err_out;

	fy_document_state_unref(fygi->fyds);
	fygi->fyds = NULL;

	fygi->vds = fy_invalid;
	fygi->iterate_root = fy_invalid;

	fygi->state = FYGIS_WAITING_STREAM_END_OR_DOCUMENT_START;
	return fye;

err_out:
	fygi->state = FYGIS_ERROR;
	return NULL;
}

static bool
fy_generic_iterator_ensure_space(struct fy_generic_iterator *fygi, unsigned int space)
{
	struct fy_generic_iterator_body_state *new_stack;
	size_t new_size, copy_size;
	unsigned int new_stack_alloc;

	/* empty stack should always have enough space */
	if (fygi->stack_top == (unsigned int)-1) {
		assert(fygi->stack_alloc >= space);
		return true;
	}

	if (fygi->stack_top + space < fygi->stack_alloc)
		return true;

	/* make sure we have enough space */
	new_stack_alloc = fygi->stack_alloc * 2;
	while (fygi->stack_top + space >= new_stack_alloc)
		new_stack_alloc *= 2;

	new_size = new_stack_alloc * sizeof(*new_stack);

	if (fygi->stack == fygi->in_place) {
		new_stack = malloc(new_size);
		if (!new_stack)
			return false;
		copy_size = (fygi->stack_top + 1) * sizeof(*new_stack);
		memcpy(new_stack, fygi->stack, copy_size);
	} else {
		new_stack = realloc(fygi->stack, new_size);
		if (!new_stack)
			return false;
	}
	fygi->stack = new_stack;
	fygi->stack_alloc = new_stack_alloc;
	return true;
}

static bool
fygi_push_collection(struct fy_generic_iterator *fygi, fy_generic v)
{
	struct fy_generic_iterator_body_state *s;

	/* make sure there's enough space */
	if (!fy_generic_iterator_ensure_space(fygi, 1))
		return false;

	/* get the next */
	fygi->stack_top++;
	s = &fygi->stack[fygi->stack_top];
	s->v = v;
	s->idx = 0;
	s->processed_key = false;

	return true;
}

static inline void
fygi_pop_collection(struct fy_generic_iterator *fygi)
{
	assert(fygi->stack_top != (unsigned int)-1);
	fygi->stack_top--;
}

static inline struct fy_generic_iterator_body_state *
fygi_last_collection(struct fy_generic_iterator *fygi)
{
	if (fygi->stack_top == (unsigned int)-1)
		return NULL;
	return &fygi->stack[fygi->stack_top];
}

bool
fy_generic_iterator_body_next_internal(struct fy_generic_iterator *fygi,
					struct fy_generic_iterator_body_result *res)
{
	struct fy_generic_iterator_body_state *s;
	fy_generic v, vcol;
	bool end;

	if (!fygi || !res || fygi->state == FYGIS_ERROR)
		return false;

	if (fygi->state != FYGIS_WAITING_BODY_START_OR_DOCUMENT_END &&
	    fygi->state != FYGIS_BODY)
		goto err_out;

	end = false;
	s = fygi_last_collection(fygi);
	if (!s) {

		v = fygi->iterate_root;
		/* empty root, or last */
		if (fy_generic_is_invalid(v) || fygi->state == FYGIS_BODY) {
			fygi->state = FYGIS_WAITING_DOCUMENT_END;
			return false;
		}

		/* ok, in body proper */
		fygi->state = FYGIS_BODY;

	} else {

		vcol = s->v;
		assert(fy_generic_is_valid(vcol));

		v = fy_invalid;
		if (fy_generic_is_sequence(vcol)) {
			const fy_generic *items;
			size_t count;

			items = fy_generic_sequence_get_items(vcol, &count);
			if (s->idx < count)
				v = items[s->idx++];
			else
				v = fy_invalid;

		} if (fy_generic_is_mapping(vcol)) {

			const fy_generic_map_pair *pairs;
			size_t count;

			pairs = fy_generic_mapping_get_pairs(vcol, &count);
			if (s->idx < count) {
				if (!s->processed_key) {
					v = pairs[s->idx].key;
					s->processed_key = true;
				} else {
					v = pairs[s->idx++].value;
					s->processed_key = false;
				}
			} else
				v = fy_invalid;
		}

		/* if no next node in the collection, it's the end of the collection */
		if (fy_generic_is_invalid(v)) {
			v = vcol;
			end = true;
		}
	}

	assert(fy_generic_is_valid(v));

	/* only for collections */
	if (fy_generic_is_collection(v) || fy_generic_is_sequence(v)) {
		if (!end) {
			/* push the new sequence */
			if (!fygi_push_collection(fygi, v))
				goto err_out;
		} else
			fygi_pop_collection(fygi);
	}

	res->v = v;
	res->end = end;
	return true;

err_out:
	fygi->state = FYGIS_ERROR;
	return false;
}

struct fy_event *fy_generic_iterator_body_next(struct fy_generic_iterator *fygi)
{
	struct fy_generic_iterator_body_result res;

	if (!fygi)
		return NULL;

	if (!fy_generic_iterator_body_next_internal(fygi, &res))
		return NULL;

	return fygi_event_create(fygi, res.v, !res.end);
}

void
fy_generic_iterator_generic_start(struct fy_generic_iterator *fygi, fy_generic v)
{
	/* do nothing on error */
	if (!fygi || fygi->state == FYGIS_ERROR)
		return;

	/* and go into body */
	fygi->state = FYGIS_WAITING_BODY_START_OR_DOCUMENT_END;
	fygi->iterate_root = v;
	fygi->vds = fy_invalid;
}

fy_generic fy_generic_iterator_generic_next(struct fy_generic_iterator *fygi)
{
	struct fy_generic_iterator_body_result res;

	if (!fygi)
		return fy_invalid;

	/* do not return ending nodes, are not interested in them */
	do {
		if (!fy_generic_iterator_body_next_internal(fygi, &res))
			return fy_invalid;

	} while (res.end);

	return res.v;
}

bool fy_generic_iterator_get_error(struct fy_generic_iterator *fygi)
{
	if (!fygi)
		return true;

	if (fygi->state != FYGIS_ERROR)
		return false;

	fy_generic_iterator_cleanup(fygi);

	return true;
}

struct fy_event *
fy_generic_iterator_generate_next(struct fy_generic_iterator *fygi)
{
	struct fy_event *fye = NULL;
	int rc;

	if (!fygi || fygi->state == FYGIS_ERROR)
		return NULL;

	if (fygi->generator_state & FYGIGF_GENERATED_NULL)
		return NULL;

	/* wants stream events and not generated yet */
	if ((fygi->generator_state & (FYGIGF_WANTS_STREAM | FYGIGF_GENERATED_SS)) == FYGIGF_WANTS_STREAM) {

		rc = fy_generic_dir_get_document_count(fygi->cfg.vdir);
		/* 0 documents is valid... */
		fygi->count = rc >= 1 ? (size_t)rc : 0;
		fygi->idx = 0;
		fygi->vds = fy_invalid;
		fygi->iterate_root = fy_invalid;

		fye = fy_generic_iterator_stream_start(fygi);
		if (!fye)
			return NULL;
		fygi->generator_state |= FYGIGF_GENERATED_SS;
		return fye;
	}

	/* wants document events and not generated yet */
	if (fygi->idx < fygi->count &&
	   (fygi->generator_state & (FYGIGF_WANTS_DOC | FYGIGF_GENERATED_DS)) == FYGIGF_WANTS_DOC) {

		fygi->vds = fy_generic_dir_get_document_vds(fygi->cfg.vdir, fygi->idx);
		if (fy_generic_is_invalid(fygi->vds))
			goto err_out;

		fygi->iterate_root = fy_generic_vds_get_root(fygi->vds);
		if (fy_generic_is_invalid(fygi->iterate_root))
			goto err_out;

		fygi->fyds = fy_generic_vds_get_document_state(fygi->vds);
		if (!fygi->fyds)
			goto err_out;

		fye = fy_generic_iterator_document_start_internal(fygi);
		if (!fye)
			return NULL;
		fygi->generator_state |= FYGIGF_GENERATED_DS;
		return fye;
	}

	/* generate body events... */
	if (fy_generic_is_valid(fygi->iterate_root) &&
	    !(fygi->generator_state & FYGIGF_GENERATED_BODY)) {
		fye = fy_generic_iterator_body_next(fygi);
		if (fye)
			return fye;

		fygi->generator_state |= FYGIGF_GENERATED_BODY;
	}

	/* wants document events and not generated yet */
	if (fygi->idx < fygi->count &&
	    (fygi->generator_state & (FYGIGF_WANTS_DOC | FYGIGF_GENERATED_DE)) == FYGIGF_WANTS_DOC) {
		fye = fy_generic_iterator_document_end(fygi);
		if (!fye)
			return NULL;
		fygi->idx++;

		/* more? generate more */
		if (fygi->idx < fygi->count)
			fygi->generator_state &= ~(FYGIGF_GENERATED_DS | FYGIGF_GENERATED_BODY |
						   FYGIGF_GENERATED_DE);
		else
			fygi->generator_state |= FYGIGF_GENERATED_DE;	/* we're out */

		return fye;
	}

	/* wants stream events and not generated yet */
	if ((fygi->generator_state & (FYGIGF_WANTS_STREAM | FYGIGF_GENERATED_SE)) == FYGIGF_WANTS_STREAM) {
		fye = fy_generic_iterator_stream_end(fygi);
		if (!fye)
			return NULL;
		fygi->generator_state |= FYGIGF_GENERATED_SE;
		return fye;
	}

	fygi->generator_state |= FYGIGF_GENERATED_NULL;
	return NULL;

err_out:
	fygi->state = FYGIS_ERROR;
	return NULL;
}
