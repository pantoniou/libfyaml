/*
 * fy-walk.c - path walker
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
#include <assert.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include <libfyaml.h>

#include "fy-parse.h"
#include "fy-doc.h"
#include "fy-walk.h"

#include "fy-utils.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) ((sizeof(x)/sizeof((x)[0])))
#endif

/* NOTE that walk results do not take references and it is invalid to
 * use _any_ call that modifies the document structure
 */
struct fy_walk_result *fy_walk_result_alloc(void)
{
	struct fy_walk_result *fwr = NULL;

	fwr = malloc(sizeof(*fwr));
	if (!fwr)
		return NULL;
	memset(fwr, 0, sizeof(*fwr));
	return fwr;
}

void fy_walk_result_free(struct fy_walk_result *fwr)
{
	if (!fwr)
		return;
	free(fwr);
}

void fy_walk_result_list_free(struct fy_walk_result_list *results)
{
	struct fy_walk_result *fwr;

	while ((fwr = fy_walk_result_list_pop(results)) != NULL)
		fy_walk_result_free(fwr);
}

int fy_walk_result_add(struct fy_walk_result_list *results, struct fy_node *fyn)
{
	struct fy_walk_result *fwr;

	/* do not add if fyn is NULL, it's a NOP */
	if (!fyn)
		return 0;

	/* do not add multiple times */
	for (fwr = fy_walk_result_list_head(results); fwr; fwr = fy_walk_result_next(results, fwr)) {
		if (fwr->fyn == fyn)
			return 0;
	}

	fwr = fy_walk_result_alloc();
	if (!fwr)
		return -1;
	fwr->fyn = fyn;
	fy_walk_result_list_add_tail(results, fwr);
	return 0;
}

int fy_walk_result_add_recursive(struct fy_walk_result_list *results, struct fy_node *fyn, bool leaf_only)
{
	struct fy_node *fyni;
	struct fy_node_pair *fynp;
	int ret;

	if (!fyn)
		return 0;

	if (fy_node_is_scalar(fyn))
		return fy_walk_result_add(results, fyn);

	if (!leaf_only) {
		ret = fy_walk_result_add(results, fyn);
		if (ret)
			return ret;
	}

	if (fy_node_is_sequence(fyn)) {
		for (fyni = fy_node_list_head(&fyn->sequence); fyni;
				fyni = fy_node_next(&fyn->sequence, fyni)) {

			ret = fy_walk_result_add_recursive(results, fyni, leaf_only);
			if (ret)
				return ret;
		}
	} else {
		for (fynp = fy_node_pair_list_head(&fyn->mapping); fynp;
				fynp = fy_node_pair_next(&fyn->mapping, fynp)) {

			ret = fy_walk_result_add_recursive(results, fynp->value, leaf_only);
			if (ret)
				return ret;
		}
	}
	return 0;
}

int fy_walk_result_list_move(struct fy_walk_result_list *to, struct fy_walk_result_list *from)
{
	struct fy_walk_result *fwr;
	struct fy_node *fyn;
	int ret;

	while ((fwr = fy_walk_result_list_pop(from)) != NULL) {
		fyn = fwr->fyn;
		fy_walk_result_free(fwr);
		ret = fy_walk_result_add(to, fyn);
		if (ret)
			return ret;
	}
	return 0;
}

struct fy_path_expr *fy_path_expr_alloc(void)
{
	struct fy_path_expr *expr = NULL;

	expr = malloc(sizeof(*expr));
	if (!expr)
		return NULL;
	memset(expr, 0, sizeof(*expr));
	fy_path_expr_list_init(&expr->children);

	return expr;
}

void fy_path_expr_free(struct fy_path_expr *expr)
{
	struct fy_path_expr *exprn;

	if (!expr)
		return;

	while ((exprn = fy_path_expr_list_pop(&expr->children)) != NULL)
		fy_path_expr_free(exprn);

	fy_token_unref(expr->fyt);

	free(expr);
}

struct fy_path_expr *fy_path_expr_alloc_recycle(struct fy_path_parser *fypp)
{
	struct fy_path_expr *expr;

	if (!fypp || fypp->suppress_recycling)
		return fy_path_expr_alloc();

	expr = fy_path_expr_list_pop(&fypp->expr_recycle);
	if (expr)
		return expr;

	return fy_path_expr_alloc();
}

void fy_path_expr_free_recycle(struct fy_path_parser *fypp, struct fy_path_expr *expr)
{
	struct fy_path_expr *exprn;

	if (!fypp || fypp->suppress_recycling) {
		fy_path_expr_free(expr);
		return;
	}

	while ((exprn = fy_path_expr_list_pop(&expr->children)) != NULL)
		fy_path_expr_free_recycle(fypp, exprn);

	if (expr->fyt) {
		fy_token_unref(expr->fyt);
		expr->fyt = NULL;
	}
	fy_path_expr_list_add_tail(&fypp->expr_recycle, expr);
}

const char *path_expr_type_txt[] = {
	[fpet_none]			= "none",
	/* */
	[fpet_root]			= "root",
	[fpet_this]			= "this",
	[fpet_parent]			= "parent",
	[fpet_every_child]		= "every-child",
	[fpet_every_child_r]		= "every-child-recursive",
	[fpet_filter_collection]	= "assert-collection",
	[fpet_filter_scalar]		= "assert-scalar",
	[fpet_filter_sequence]		= "assert-sequence",
	[fpet_filter_mapping]		= "assert-mapping",
	[fpet_seq_index]		= "seq-index",
	[fpet_seq_slice]		= "seq-slice",
	[fpet_alias]			= "alias",

	[fpet_map_key]			= "map-key",

	[fpet_multi]			= "multi",
	[fpet_chain]			= "chain",
	[fpet_logical_or]		= "logical-or",
	[fpet_logical_and]		= "logical-and",
};

bool fy_token_type_is_component_start(enum fy_token_type type)
{
	return type == FYTT_PE_ROOT ||
	       type == FYTT_PE_THIS ||
	       type == FYTT_PE_PARENT ||
	       type == FYTT_PE_MAP_KEY ||
	       type == FYTT_PE_SEQ_INDEX ||
	       type == FYTT_PE_SEQ_SLICE ||
	       type == FYTT_PE_EVERY_CHILD ||
	       type == FYTT_PE_EVERY_CHILD_R ||
	       type == FYTT_PE_ALIAS;
}

bool fy_token_type_next_slash_is_root(enum fy_token_type type)
{
	return type == FYTT_NONE ||
	       type == FYTT_STREAM_START ||
	       type == FYTT_PE_BARBAR ||
	       type == FYTT_PE_AMPAMP ||
	       type == FYTT_PE_LPAREN;
}

bool fy_token_type_is_filter(enum fy_token_type type)
{
	return type == FYTT_PE_SCALAR_FILTER ||
	       type == FYTT_PE_COLLECTION_FILTER ||
	       type == FYTT_PE_SEQ_FILTER ||
	       type == FYTT_PE_MAP_FILTER;
}

static struct fy_diag *fy_path_parser_reader_get_diag(struct fy_reader *fyr)
{
	struct fy_path_parser *fypp = container_of(fyr, struct fy_path_parser, reader);
	return fypp->cfg.diag;
}

static const struct fy_reader_ops fy_path_parser_reader_ops = {
	.get_diag = fy_path_parser_reader_get_diag,
};

void fy_path_parser_setup(struct fy_path_parser *fypp, const struct fy_path_parse_cfg *pcfg)
{
	if (!fypp)
		return;

	memset(fypp, 0, sizeof(*fypp));
	if (pcfg)
		fypp->cfg = *pcfg;
	fy_reader_setup(&fypp->reader, &fy_path_parser_reader_ops);
	fy_token_list_init(&fypp->queued_tokens);
	fypp->last_queued_token_type = FYTT_NONE;

	/* use the static stack at first, faster */
	fypp->operators = fypp->operators_static;
	fypp->operands = fypp->operands_static;

	fypp->operator_alloc = ARRAY_SIZE(fypp->operators_static);
	fypp->operand_alloc = ARRAY_SIZE(fypp->operands_static);

	fy_path_expr_list_init(&fypp->expr_recycle);
	fypp->suppress_recycling = (fypp->cfg.flags & FYPPCF_DISABLE_RECYCLING) || getenv("FY_VALGRIND");
}

void fy_path_parser_cleanup(struct fy_path_parser *fypp)
{
	struct fy_path_expr *expr;
	struct fy_token *fyt;

	if (!fypp)
		return;

	while (fypp->operator_top > 0) {
		fyt = fypp->operators[--fypp->operator_top];
		fypp->operators[fypp->operator_top] = NULL;
		fy_token_unref(fyt);
	}

	if (fypp->operators != fypp->operators_static)
		free(fypp->operators);
	fypp->operators = fypp->operators_static;
	fypp->operator_alloc = ARRAY_SIZE(fypp->operators_static);

	while (fypp->operand_top > 0) {
		expr = fypp->operands[--fypp->operand_top];
		fypp->operands[fypp->operand_top] = NULL;
		fy_path_expr_free(expr);
	}

	if (fypp->operands != fypp->operands_static)
		free(fypp->operands);
	fypp->operands = fypp->operands_static;
	fypp->operand_alloc = ARRAY_SIZE(fypp->operands_static);

	fy_reader_cleanup(&fypp->reader);
	fy_token_list_unref_all(&fypp->queued_tokens);

	while ((expr = fy_path_expr_list_pop(&fypp->expr_recycle)) != NULL)
		fy_path_expr_free(expr);

	fypp->last_queued_token_type = FYTT_NONE;
}

int fy_path_parser_open(struct fy_path_parser *fypp,
			struct fy_input *fyi, const struct fy_reader_input_cfg *icfg)
{
	if (!fypp)
		return -1;

	return fy_reader_input_open(&fypp->reader, fyi, icfg);
}

void fy_path_parser_close(struct fy_path_parser *fypp)
{
	if (!fypp)
		return;

	fy_reader_input_done(&fypp->reader);
}

struct fy_token *fy_path_token_vqueue(struct fy_path_parser *fypp, enum fy_token_type type, va_list ap)
{
	struct fy_token *fyt;

	fyt = fy_token_list_vqueue(&fypp->queued_tokens, type, ap);
	if (fyt) {
		fypp->token_activity_counter++;
		fypp->last_queued_token_type = type;
	}
	return fyt;
}

struct fy_token *fy_path_token_queue(struct fy_path_parser *fypp, enum fy_token_type type, ...)
{
	va_list ap;
	struct fy_token *fyt;

	va_start(ap, type);
	fyt = fy_path_token_vqueue(fypp, type, ap);
	va_end(ap);

	return fyt;
}

int fy_path_fetch_seq_index_or_slice(struct fy_path_parser *fypp, int c)
{
	struct fy_reader *fyr;
	struct fy_token *fyt;
	bool neg;
	int i, j, val, nval, digits, indices[2];

	fyr = &fypp->reader;

	/* verify that the called context is correct */
	assert(fy_is_num(c) || (c == '-' && fy_is_num(fy_reader_peek_at(fyr, 1))));

	i = 0;
	indices[0] = indices[1] = -1;

	j = 0;
	while (j < 2) {

		neg = false;
		if (c == '-') {
			neg = true;
			i++;
		}

		digits = 0;
		val = 0;
		while (fy_is_num((c = fy_reader_peek_at(fyr, i)))) {
			nval = (val * 10) | (c - '0');
			FYR_PARSE_ERROR_CHECK(fyr, 0, i, FYEM_SCAN,
					nval >= val && nval >= 0, err_out,
					"illegal sequence index (overflow)");
			val = nval;
			i++;
			digits++;
		}
		FYR_PARSE_ERROR_CHECK(fyr, 0, i, FYEM_SCAN,
				(val == 0 && digits == 1) || (val > 0), err_out,
				"bad number");
		if (neg)
			val = -val;

		indices[j] = val;

		/* continue only on slice : */
		if (c == ':') {
			c = fy_reader_peek_at(fyr, i + 1);
			if (fy_is_num(c) || (c == '-' && fy_is_num(fy_reader_peek_at(fyr, i + 2)))) {
				i++;
				j++;
				continue;
			}
		}

		break;
	}

	if (j >= 1)
		fyt = fy_path_token_queue(fypp, FYTT_PE_SEQ_SLICE, fy_reader_fill_atom_a(fyr, i), indices[0], indices[1]);
	else
		fyt = fy_path_token_queue(fypp, FYTT_PE_SEQ_INDEX, fy_reader_fill_atom_a(fyr, i), indices[0]);

	fyr_error_check(fyr, fyt, err_out, "fy_path_token_queue() failed\n");

	return 0;

err_out:
	fypp->stream_error = true;
	return -1;
}

int fy_path_fetch_simple_map_key(struct fy_path_parser *fypp, int c)
{
	struct fy_reader *fyr;
	struct fy_token *fyt;
	int i;

	fyr = &fypp->reader;

	/* verify that the called context is correct */
	assert(fy_is_first_alpha(c));
	i = 1;
	while (fy_is_alnum(fy_reader_peek_at(fyr, i)))
		i++;

	/* document is NULL, is a simple key */
	fyt = fy_path_token_queue(fypp, FYTT_PE_MAP_KEY, fy_reader_fill_atom_a(fyr, i), NULL);
	fyr_error_check(fyr, fyt, err_out, "fy_path_token_queue() failed\n");

	return 0;

err_out:
	fypp->stream_error = true;
	return -1;
}

int fy_path_fetch_flow_map_key(struct fy_path_parser *fypp, int c)
{
	struct fy_reader *fyr;
	struct fy_token *fyt;
	struct fy_document *fyd;
	struct fy_atom handle;
	struct fy_parser fyp_data, *fyp = &fyp_data;
	struct fy_parse_cfg cfg_data, *cfg = NULL;
	int rc;

	fyr = &fypp->reader;

	/* verify that the called context is correct */
	assert(fy_is_path_flow_key_start(c));

	fy_reader_fill_atom_start(fyr, &handle);

	cfg = &cfg_data;
	memset(cfg, 0, sizeof(*cfg));
	cfg->flags = FYPCF_DEFAULT_PARSE;
	cfg->diag = fypp->cfg.diag;

	rc = fy_parse_setup(fyp, cfg);
	fyr_error_check(fyr, !rc, err_out, "fy_parse_setup() failed\n");

	/* associate with reader and set flow mode */
	fy_parser_set_reader(fyp, fyr);
	fy_parser_set_flow_only_mode(fyp, true);

	fyd = fy_parse_load_document(fyp);

	/* cleanup the parser no matter what */
	fy_parse_cleanup(fyp);

	fyr_error_check(fyr, fyd, err_out, "fy_parse_load_document() failed\n");

	fy_reader_fill_atom_end(fyr, &handle);

	/* document is NULL, is a simple key */
	fyt = fy_path_token_queue(fypp, FYTT_PE_MAP_KEY, &handle, fyd);
	fyr_error_check(fyr, fyt, err_out, "fy_path_token_queue() failed\n");

	return 0;

err_out:
	fypp->stream_error = true;
	return -1;
}

int fy_path_fetch_tokens(struct fy_path_parser *fypp)
{
	enum fy_token_type type;
	struct fy_token *fyt;
	struct fy_reader *fyr;
	int c, rc, simple_token_count;

	fyr = &fypp->reader;
	if (!fypp->stream_start_produced) {

		fyt = fy_path_token_queue(fypp, FYTT_STREAM_START, fy_reader_fill_atom_a(fyr, 0));
		fyr_error_check(fyr, fyt, err_out, "fy_path_token_queue() failed\n");

		fypp->stream_start_produced = true;
		return 0;
	}

	/* XXX scan to next token? */

	c = fy_reader_peek(fyr);

	if (fy_is_z(c)) {

		if (c >= 0)
			fy_reader_advance(fyr, c);

		/* produce stream end continuously */
		fyt = fy_path_token_queue(fypp, FYTT_STREAM_END, fy_reader_fill_atom_a(fyr, 0));
		fyr_error_check(fyr, fyt, err_out, "fy_path_token_queue() failed\n");

		return 0;
	}

	fyt = NULL;
	type = FYTT_NONE;
	simple_token_count = 0;

	switch (c) {
	case '/':
		type = FYTT_PE_SLASH;
		simple_token_count = 1;
		break;

	case '^':
		type = FYTT_PE_ROOT;
		simple_token_count = 1;
		break;

	case ':':
		type = FYTT_PE_SIBLING;
		simple_token_count = 1;
		break;

	case '$':
		type = FYTT_PE_SCALAR_FILTER;
		simple_token_count = 1;
		break;

	case '%':
		type = FYTT_PE_COLLECTION_FILTER;
		simple_token_count = 1;
		break;

	case '[':
		if (fy_reader_peek_at(fyr, 1) == ']') {
			type = FYTT_PE_SEQ_FILTER;
			simple_token_count = 2;
		}
		break;

	case '{':
		if (fy_reader_peek_at(fyr, 1) == '}') {
			type = FYTT_PE_MAP_FILTER;
			simple_token_count = 2;
		}
		break;

	case ',':
		type = FYTT_PE_COMMA;
		simple_token_count = 1;
		break;

	case '.':
		if (fy_reader_peek_at(fyr, 1) == '.') {
			type = FYTT_PE_PARENT;
			simple_token_count = 2;
		} else {
			type = FYTT_PE_THIS;
			simple_token_count = 1;
		}
		break;

	case '*':
		if (fy_reader_peek_at(fyr, 1) == '*') {
			type = FYTT_PE_EVERY_CHILD_R;
			simple_token_count = 2;
		} else if (!fy_is_first_alpha(fy_reader_peek_at(fyr, 1))) {
			type = FYTT_PE_EVERY_CHILD;
			simple_token_count = 1;
		} else {
			type = FYTT_PE_ALIAS;
			simple_token_count = 2;
			while (fy_is_alnum(fy_reader_peek_at(fyr, simple_token_count)))
				simple_token_count++;
		}
		break;

	case '|':
		if (fy_reader_peek_at(fyr, 1) == '|') {
			type = FYTT_PE_BARBAR;
			simple_token_count = 2;
			break;
		}
		break;

	case '&':
		if (fy_reader_peek_at(fyr, 1) == '&') {
			type = FYTT_PE_AMPAMP;
			simple_token_count = 2;
			break;
		}
		break;

	case '(':
		type = FYTT_PE_LPAREN;
		simple_token_count = 1;
		break;

	case ')':
		type = FYTT_PE_RPAREN;
		simple_token_count = 1;
		break;

	default:
		break;
	}

	/* simple tokens */
	if (simple_token_count > 0) {
		fyt = fy_path_token_queue(fypp, type, fy_reader_fill_atom_a(fyr, simple_token_count));
		fyr_error_check(fyr, fyt, err_out, "fy_path_token_queue() failed\n");

		return 0;
	}

	if (fy_is_first_alpha(c))
		return fy_path_fetch_simple_map_key(fypp, c);

	if (fy_is_path_flow_key_start(c))
		return fy_path_fetch_flow_map_key(fypp, c);

	if (fy_is_num(c) || (c == '-' && fy_is_num(fy_reader_peek_at(fyr, 1))))
		return fy_path_fetch_seq_index_or_slice(fypp, c);

	FYR_PARSE_ERROR(fyr, 0, 1, FYEM_SCAN, "bad path expression starts here");

err_out:
	fypp->stream_error = true;
	rc = -1;
	return rc;
}

struct fy_token *fy_path_scan_peek(struct fy_path_parser *fypp, struct fy_token *fyt_prev)
{
	struct fy_token *fyt;
	struct fy_reader *fyr;
	int rc, last_token_activity_counter;

	fyr = &fypp->reader;

	/* nothing if stream end produced (and no stream end token in queue) */
	if (!fyt_prev && fypp->stream_end_produced && fy_token_list_empty(&fypp->queued_tokens)) {

		fyt = fy_token_list_head(&fypp->queued_tokens);
		if (fyt && fyt->type == FYTT_STREAM_END)
			return fyt;

		return NULL;
	}

	for (;;) {
		if (!fyt_prev)
			fyt = fy_token_list_head(&fypp->queued_tokens);
		else
			fyt = fy_token_next(&fypp->queued_tokens, fyt_prev);
		if (fyt)
			break;

		/* on stream error we're done */
		if (fypp->stream_error)
			return NULL;

		/* keep track of token activity, if it didn't change
		* after the fetch tokens call, the state machine is stuck
		*/
		last_token_activity_counter = fypp->token_activity_counter;

		/* fetch more then */
		rc = fy_path_fetch_tokens(fypp);
		if (rc) {
			fy_error(fypp->cfg.diag, "fy_path_fetch_tokens() failed\n");
			goto err_out;
		}
		if (last_token_activity_counter == fypp->token_activity_counter) {
			fy_error(fypp->cfg.diag, "out of tokens and failed to produce anymore");
			goto err_out;
		}
	}

	switch (fyt->type) {
	case FYTT_STREAM_START:
		fypp->stream_start_produced = true;
		break;
	case FYTT_STREAM_END:
		fypp->stream_end_produced = true;

		rc = fy_reader_input_done(fyr);
		if (rc) {
			fy_error(fypp->cfg.diag, "fy_parse_input_done() failed");
			goto err_out;
		}
		break;
	default:
		break;
	}

	return fyt;

err_out:
	return NULL;
}

struct fy_token *fy_path_scan_remove(struct fy_path_parser *fypp, struct fy_token *fyt)
{
	if (!fypp || !fyt)
		return NULL;

	fy_token_list_del(&fypp->queued_tokens, fyt);

	return fyt;
}

struct fy_token *fy_path_scan_remove_peek(struct fy_path_parser *fypp, struct fy_token *fyt)
{
	fy_token_unref(fy_path_scan_remove(fypp, fyt));

	return fy_path_scan_peek(fypp, NULL);
}

struct fy_token *fy_path_scan(struct fy_path_parser *fypp)
{
	return fy_path_scan_remove(fypp, fy_path_scan_peek(fypp, NULL));
}

void fy_path_expr_dump(struct fy_path_expr *expr, struct fy_diag *diag, enum fy_error_type errlevel, int level, const char *banner)
{
	struct fy_path_expr *expr2;
	const char *text;
	size_t len;
	bool save_on_error;

	if (errlevel < diag->cfg.level)
		return;

	save_on_error = diag->on_error;
	diag->on_error = true;

	if (banner)
		fy_diag_diag(diag, errlevel, "%-*s%s", level*2, "", banner);

	text = fy_token_get_text(expr->fyt, &len);

	fy_diag_diag(diag, errlevel, "> %-*s%s%s%.*s",
			level*2, "",
			path_expr_type_txt[expr->type],
			len ? " " : "",
			(int)len, text);

	for (expr2 = fy_path_expr_list_head(&expr->children); expr2; expr2 = fy_path_expr_next(&expr->children, expr2))
		fy_path_expr_dump(expr2, diag, errlevel, level + 1, NULL);

	diag->on_error = save_on_error;
}

enum fy_path_expr_type fy_map_token_to_path_expr_type(enum fy_token_type type)
{
	switch (type) {
	case FYTT_PE_ROOT:
		return fpet_root;
	case FYTT_PE_THIS:
		return fpet_this;
	case FYTT_PE_PARENT:
	case FYTT_PE_SIBLING:	/* sibling maps to a chain of fpet_parent */
		return fpet_parent;
	case FYTT_PE_MAP_KEY:
		return fpet_map_key;
	case FYTT_PE_SEQ_INDEX:
		return fpet_seq_index;
	case FYTT_PE_SEQ_SLICE:
		return fpet_seq_slice;
	case FYTT_PE_EVERY_CHILD:
		return fpet_every_child;
	case FYTT_PE_EVERY_CHILD_R:
		return fpet_every_child_r;
	case FYTT_PE_ALIAS:
		return fpet_alias;
	case FYTT_PE_SCALAR_FILTER:
		return fpet_filter_scalar;
	case FYTT_PE_COLLECTION_FILTER:
		return fpet_filter_collection;
	case FYTT_PE_SEQ_FILTER:
		return fpet_filter_sequence;
	case FYTT_PE_MAP_FILTER:
		return fpet_filter_mapping;
	case FYTT_PE_COMMA:
		return fpet_multi;
	case FYTT_PE_SLASH:
		return fpet_chain;
	case FYTT_PE_BARBAR:
		return fpet_logical_or;
	case FYTT_PE_AMPAMP:
		return fpet_logical_and;
	default:
		/* note parentheses do not have an expression */
		break;
	}
	return fpet_none;
}

bool fy_token_type_is_operand(enum fy_token_type type)
{
	return type == FYTT_PE_ROOT ||
	       type == FYTT_PE_THIS ||
	       type == FYTT_PE_PARENT ||
	       type == FYTT_PE_MAP_KEY ||
	       type == FYTT_PE_SEQ_INDEX ||
	       type == FYTT_PE_SEQ_SLICE ||
	       type == FYTT_PE_EVERY_CHILD ||
	       type == FYTT_PE_EVERY_CHILD_R ||
	       type == FYTT_PE_ALIAS;
}

bool fy_token_type_is_operator(enum fy_token_type type)
{
	return type == FYTT_PE_SLASH ||
	       type == FYTT_PE_SCALAR_FILTER ||
	       type == FYTT_PE_COLLECTION_FILTER ||
	       type == FYTT_PE_SEQ_FILTER ||
	       type == FYTT_PE_MAP_FILTER ||
	       type == FYTT_PE_SIBLING ||
	       type == FYTT_PE_COMMA ||
	       type == FYTT_PE_BARBAR ||
	       type == FYTT_PE_AMPAMP ||
	       type == FYTT_PE_LPAREN ||
	       type == FYTT_PE_RPAREN;
}

bool fy_token_type_is_operand_or_operator(enum fy_token_type type)
{
	return fy_token_type_is_operand(type) ||
	       fy_token_type_is_operator(type);
}

int fy_token_type_operator_prec(enum fy_token_type type)
{
	switch (type) {
	case FYTT_PE_SLASH:
		return 10;
	case FYTT_PE_SCALAR_FILTER:
	case FYTT_PE_COLLECTION_FILTER:
	case FYTT_PE_SEQ_FILTER:
	case FYTT_PE_MAP_FILTER:
		return 5;
	case FYTT_PE_SIBLING:
		return 20;
	case FYTT_PE_COMMA:
		return 15;
	case FYTT_PE_BARBAR:
	case FYTT_PE_AMPAMP:
		return 4;
	case FYTT_PE_LPAREN:
	case FYTT_PE_RPAREN:
		return 30;
	default:
		break;
	}
	return -1;
}

#if 0
static void dump_operator_stack(struct fy_path_parser *fypp)
{
	struct fy_token *fyt;
	unsigned int i;

	if (!fypp->operator_top)
		return;

	i = fypp->operator_top;
	do {
		fyt = fypp->operators[--i];
		fy_notice(fypp->cfg.diag, "! [%d] %.*s (%2d)\n", i, 20, fy_token_debug_text_a(fyt),
				fy_token_type_operator_prec(fyt->type));
	} while (i > 0);
}

static void dump_operand_stack(struct fy_path_parser *fypp)
{
	struct fy_path_expr *expr;
	unsigned int i;

	if (!fypp->operand_top)
		return;

	i = fypp->operand_top;
	do {
		expr = fypp->operands[--i];
		fy_path_expr_dump(fypp, expr, 0, NULL);
	} while (i > 0);
}
#endif

static int
push_operand(struct fy_path_parser *fypp, struct fy_path_expr *expr)
{
	struct fy_path_expr **exprs;
	unsigned int alloc;
	size_t size;

	/* grow the stack if required */
	if (fypp->operand_top >= fypp->operand_alloc) {
		alloc = fypp->operand_alloc;
		size = alloc * sizeof(*exprs);

		if (fypp->operands == fypp->operands_static) {
			exprs = malloc(size * 2);
			if (exprs)
				memcpy(exprs, fypp->operands_static, size);
		} else
			exprs = realloc(fypp->operands, size * 2);

		if (!exprs)
			return -1;

		fypp->operand_alloc = alloc * 2;
		fypp->operands = exprs;
	}

	fypp->operands[fypp->operand_top++] = expr;

	return 0;
}

static struct fy_path_expr *
peek_operand_at(struct fy_path_parser *fypp, unsigned int pos)
{
	if (fypp->operand_top <= pos)
		return NULL;
	return fypp->operands[fypp->operand_top - 1 - pos];
}


static struct fy_path_expr *
peek_operand(struct fy_path_parser *fypp)
{
	if (fypp->operand_top == 0)
		return NULL;
	return fypp->operands[fypp->operand_top - 1];
}

static struct fy_path_expr *
pop_operand(struct fy_path_parser *fypp)
{
	struct fy_path_expr *expr;

	if (fypp->operand_top == 0)
		return NULL;

	expr = fypp->operands[--fypp->operand_top];
	fypp->operands[fypp->operand_top] = NULL;

	return expr;
}

static struct fy_token *
peek_operator(struct fy_path_parser *fypp)
{
	if (fypp->operator_top == 0)
		return NULL;
	return fypp->operators[fypp->operator_top - 1];
}

static int
push_operator(struct fy_path_parser *fypp, struct fy_token *fyt)
{
	struct fy_token **fyts;
	unsigned int alloc;
	size_t size;

	assert(fy_token_type_is_operator(fyt->type));

	/* grow the stack if required */
	if (fypp->operator_top >= fypp->operator_alloc) {
		alloc = fypp->operator_alloc;
		size = alloc * sizeof(*fyts);

		if (fypp->operators == fypp->operators_static) {
			fyts = malloc(size * 2);
			if (fyts)
				memcpy(fyts, fypp->operators_static, size);
		} else
			fyts = realloc(fypp->operators, size * 2);

		if (!fyts)
			return -1;

		fypp->operator_alloc = alloc * 2;
		fypp->operators = fyts;
	}

	fypp->operators[fypp->operator_top++] = fyt;

	return 0;
}

static struct fy_token *
pop_operator(struct fy_path_parser *fypp)
{
	struct fy_token *fyt;

	if (fypp->operator_top == 0)
		return NULL;

	fyt = fypp->operators[--fypp->operator_top];
	fypp->operators[fypp->operator_top] = NULL;

	return fyt;
}

#define PREFIX	0
#define INFIX	1
#define SUFFIX	2

int fy_token_type_operator_placement(enum fy_token_type type)
{
	switch (type) {
	case FYTT_PE_SLASH:	/* SLASH is special at the start of the expression */
	case FYTT_PE_COMMA:
	case FYTT_PE_BARBAR:
	case FYTT_PE_AMPAMP:
		return INFIX;
	case FYTT_PE_SCALAR_FILTER:
	case FYTT_PE_COLLECTION_FILTER:
	case FYTT_PE_SEQ_FILTER:
	case FYTT_PE_MAP_FILTER:
		return SUFFIX;
	case FYTT_PE_SIBLING:
		return PREFIX;
	default:
		break;
	}
	return -1;
}

const struct fy_mark *fy_path_expr_start_mark(struct fy_path_expr *expr)
{
	struct fy_path_expr *exprn;

	if (!expr)
		return NULL;

	if (!fy_path_expr_type_is_parent(expr->type))
		return fy_token_start_mark(expr->fyt);

	exprn = fy_path_expr_list_head(&expr->children);
	if (!exprn)
		return NULL;

	return fy_path_expr_start_mark(exprn);
}

const struct fy_mark *fy_path_expr_end_mark(struct fy_path_expr *expr)
{
	struct fy_path_expr *exprn;

	if (!expr)
		return NULL;

	if (!fy_path_expr_type_is_parent(expr->type))
		return fy_token_end_mark(expr->fyt);

	exprn = fy_path_expr_list_tail(&expr->children);
	if (!exprn)
		return NULL;

	return fy_path_expr_end_mark(exprn);
}

#if 0
static struct fy_token *
expr_to_token_mark(struct fy_path_expr *expr, struct fy_input *fyi)
{
	const struct fy_mark *ms, *me;
	struct fy_atom handle;

	ms = fy_path_expr_start_mark(expr);
	me = fy_path_expr_end_mark(expr);
	assert(ms);
	assert(me);

	memset(&handle, 0, sizeof(handle));
	handle.start_mark = *ms;
	handle.end_mark = *me;
	handle.fyi = fyi;
	handle.style = FYAS_PLAIN;
	handle.chomp = FYAC_CLIP;

	return fy_token_create(FYTT_INPUT_MARKER, &handle);
}
#endif

static bool
expr_is_before_token(struct fy_path_expr *expr, struct fy_token *fyt)
{
	const struct fy_mark *me, *mt;

	if (!expr || !fyt)
		return false;

	me = fy_path_expr_end_mark(expr);
	if (!me)
		return false;

	mt = fy_token_start_mark(fyt);
	if (!mt)
		return false;

	return me->input_pos <= mt->input_pos;
}

static bool
expr_is_after_token(struct fy_path_expr *expr, struct fy_token *fyt)
{
	const struct fy_mark *me, *mt;

	if (!expr || !fyt)
		return false;

	me = fy_path_expr_start_mark(expr);
	if (!me)
		return false;

	mt = fy_token_end_mark(fyt);
	if (!mt)
		return false;

	return me->input_pos >= mt->input_pos;
}

static int evaluate(struct fy_path_parser *fypp)
{
	struct fy_reader *fyr;
	struct fy_token *fyt_top = NULL, *fyt_peek;
#if 0
	struct fy_token *fyt_markl, *fyt_markr;
#endif
	struct fy_path_expr *exprl = NULL, *exprr = NULL, *chain = NULL, *expr = NULL;
	struct fy_path_expr *parent = NULL;
	enum fy_path_expr_type etype;
	int ret;

	fyr = &fypp->reader;

	fyt_top = pop_operator(fypp);
	fyr_error_check(fyr, fyt_top, err_out,
			"pop_operator() failed to find token operator to evaluate\n");

	exprl = NULL;
	exprr = NULL;
	switch (fyt_top->type) {

	case FYTT_PE_SLASH:

		/* dump_operand_stack(fypp); */
		/* dump_operator_stack(fypp); */

		/* try to figure out if this slash is the root or a chain operator */
		exprr = peek_operand(fypp);
		exprl = peek_operand_at(fypp, 1);
		fyt_peek = peek_operator(fypp);

		/* remove expressions that are before this */
		if (fyt_peek && fy_token_type_next_slash_is_root(fyt_peek->type)) {
			if (exprr && expr_is_before_token(exprr, fyt_peek)) {
				/*  fyr_notice(fyr, "exprr before token, removing from scan\n"); */
				exprr = NULL;
			}
			if (exprl && expr_is_before_token(exprl, fyt_peek)) {
				/* fyr_notice(fyr, "exprl before token, removing from scan\n"); */
				exprl = NULL;
			}
		}

		if (exprr && !exprl && expr_is_before_token(exprr, fyt_top)) {
			/* fyr_notice(fyr, "exprl == NULL && exprr before token, means it's at the left\n"); */
			exprl = exprr;
			exprr = NULL;
		}

		if (exprl && !exprr && expr_is_after_token(exprl, fyt_top)) {
			/* fyr_notice(fyr, "exprr == NULL && exprl after token, means it's at the right\n"); */
			exprl = exprr;
			exprr = NULL;
		}

#if 0
		fyt_markl = NULL;
		fyt_markr = NULL;

		if (exprr) {
			fyt_markr = expr_to_token_mark(exprr, fyt_top->handle.fyi);
			assert(fyt_markr);
		}

		if (exprl) {
			fyt_markl = expr_to_token_mark(exprl, fyt_top->handle.fyi);
			assert(fyt_markl);
		}

		FYR_TOKEN_DIAG(fyr, fyt_top,
			FYDF_NOTICE, FYEM_PARSE, "location of fyt_top");

		if (fyt_peek) {
			FYR_TOKEN_DIAG(fyr, fyt_peek,
				FYDF_NOTICE, FYEM_PARSE, "location of fyt_peek");
		} else
			fyr_notice(fyr, "fyt_peek=<NULL>\n");

		if (fyt_markl)
			FYR_TOKEN_DIAG(fyr, fyt_markl,
				FYDF_NOTICE, FYEM_PARSE, "location of exprl");

		if (fyt_markr)
			FYR_TOKEN_DIAG(fyr, fyt_markr,
				FYDF_NOTICE, FYEM_PARSE, "location of exprr");

		fy_token_unref(fyt_markl);
		fy_token_unref(fyt_markr);
#endif

		if (exprl && exprr) {
			// fyr_notice(fyr, "CHAIN operator\n");
			etype = fpet_chain;
			goto do_infix;
		}

		if (exprl) {
			// fyr_notice(fyr, "COLLECTION operator\n");
			etype = fpet_filter_collection;
			goto do_suffix;
		}

		if (exprr) {
			// fyr_notice(fyr, "ROOT operator (with arguments)\n");
			etype = fpet_root;
			goto do_prefix;
		}

		// fyr_notice(fyr, "ROOT value (with no arguments)\n");

		exprr = fy_path_expr_alloc_recycle(fypp);
		fyr_error_check(fyr, exprr, err_out,
				"fy_path_expr_alloc_recycle() failed\n");

		exprr->type = fpet_root;
		exprr->fyt = fyt_top;
		fyt_top = NULL;

		ret = push_operand(fypp, exprr);
		fyr_error_check(fyr, !ret, err_out,
				"push_operand() failed\n");
		exprr = NULL;
		break;

	case FYTT_PE_SIBLING:

		/* get mapping expression type */
		etype = fy_map_token_to_path_expr_type(fyt_top->type);
do_prefix:
		exprr = pop_operand(fypp);

		FYR_TOKEN_ERROR_CHECK(fyr, fyt_top, FYEM_PARSE,
				exprr, err_out,
				"sibling operator without argument");

		if (fyt_top->type == FYTT_PE_SIBLING) {
			FYR_TOKEN_ERROR_CHECK(fyr, fyt_top, FYEM_PARSE,
					exprr->fyt && exprr->fyt->type == FYTT_PE_MAP_KEY, err_out,
					"sibling operator on non-map key");
		}

		/* chaining */
		chain = fy_path_expr_alloc_recycle(fypp);
		fyr_error_check(fyr, chain, err_out,
				"fy_path_expr_alloc_recycle() failed\n");

		chain->type = fpet_chain;
		chain->fyt = NULL;

		exprl = fy_path_expr_alloc_recycle(fypp);
		fyr_error_check(fyr, exprl, err_out,
				"fy_path_expr_alloc_recycle() failed\n");

		exprl->type = etype;
		exprl->fyt = fyt_top;
		fyt_top = NULL;

		fy_path_expr_list_add_tail(&chain->children, exprl);
		exprl = NULL;
		fy_path_expr_list_add_tail(&chain->children, exprr);
		exprr = NULL;

		ret = push_operand(fypp, chain);
		fyr_error_check(fyr, !ret, err_out,
				"push_operand() failed\n");
		chain = NULL;

		break;

	case FYTT_PE_COMMA:
	case FYTT_PE_BARBAR:
	case FYTT_PE_AMPAMP:

		/* get mapping expression type */
		etype = fy_map_token_to_path_expr_type(fyt_top->type);
do_infix:
		/* verify we got one */
		assert(etype != fpet_none);
		/* and that it's one with children */
		assert(fy_path_expr_type_is_parent(etype));

		exprr = pop_operand(fypp);
		FYR_TOKEN_ERROR_CHECK(fyr, fyt_top, FYEM_PARSE,
				exprr, err_out,
				"operator without operands (rhs)");

		exprl = pop_operand(fypp);
		FYR_TOKEN_ERROR_CHECK(fyr, fyt_top, FYEM_PARSE,
				exprl, err_out,
				"operator without operands (lhs)");

		/* optimize parent */
		if (exprl->type != etype) {

			/* parent */
			parent = fy_path_expr_alloc_recycle(fypp);
			fyr_error_check(fyr, parent, err_out,
					"fy_path_expr_alloc_recycle() failed\n");

			parent->type = etype;
			parent->fyt = fyt_top;
			fyt_top = NULL;

			fy_path_expr_list_add_tail(&parent->children, exprl);
			exprl = NULL;
		} else {
			/* reuse lhs */
			parent = exprl;
			exprl = NULL;
		}

		if (exprr->type != etype) {
			/* not the same type, append */
			fy_path_expr_list_add_tail(&parent->children, exprr);
			exprr = NULL;
		} else {
			/* move the contents of the chain */
			while ((expr = fy_path_expr_list_pop(&exprr->children)) != NULL)
				fy_path_expr_list_add_tail(&parent->children, expr);
			fy_path_expr_free_recycle(fypp, exprr);
			exprr = NULL;
		}

		ret = push_operand(fypp, parent);
		fyr_error_check(fyr, !ret, err_out,
				"push_operand() failed\n");
		parent = NULL;

		fy_token_unref(fyt_top);
		fyt_top = NULL;

		break;

	case FYTT_PE_SCALAR_FILTER:
	case FYTT_PE_COLLECTION_FILTER:
	case FYTT_PE_SEQ_FILTER:
	case FYTT_PE_MAP_FILTER:

		etype = fy_map_token_to_path_expr_type(fyt_top->type);
do_suffix:
		exprl = pop_operand(fypp);
		FYR_TOKEN_ERROR_CHECK(fyr, fyt_top, FYEM_PARSE,
				exprl, err_out,
				"filter operator without argument");

		if (exprl->type != fpet_chain) {
			/* chaining */
			chain = fy_path_expr_alloc_recycle(fypp);
			fyr_error_check(fyr, chain, err_out,
					"fy_path_expr_alloc_recycle() failed\n");

			chain->type = fpet_chain;
			chain->fyt = NULL;

			fy_path_expr_list_add_tail(&chain->children, exprl);
			exprl = NULL;
		} else {
			chain = exprl;
			exprl = NULL;
		}

		exprr = fy_path_expr_alloc_recycle(fypp);
		fyr_error_check(fyr, exprr, err_out,
				"fy_path_expr_alloc_recycle() failed\n");

		exprr->type = etype;
		exprr->fyt = fyt_top;
		fyt_top = NULL;

		fy_path_expr_list_add_tail(&chain->children, exprr);
		exprr = NULL;

		ret = push_operand(fypp, chain);
		fyr_error_check(fyr, !ret, err_out,
				"push_operand() failed\n");
		chain = NULL;

		break;

	case FYTT_PE_LPAREN:
		FYR_TOKEN_ERROR(fyr, fyt_top, FYEM_PARSE,
				"Mismatched left parentheses");
		goto err_out;

	case FYTT_PE_RPAREN:
		while ((fyt_peek = peek_operator(fypp)) != NULL) {
			if (fyt_peek->type == FYTT_PE_LPAREN)
				break;
			ret = evaluate(fypp);
			fyr_error_check(fyr, !ret, err_out,
					"evaluate() failed\n");
		}

		FYR_TOKEN_ERROR_CHECK(fyr, fyt_top, FYEM_PARSE,
				fyt_peek, err_out,
				"Missing left parentheses");

		FYR_TOKEN_ERROR_CHECK(fyr, fyt_peek, FYEM_PARSE,
				fyt_peek->type == FYTT_PE_LPAREN, err_out,
				"Mismatched right parentheses");

		fy_token_unref(fyt_top);

		fyt_top = pop_operator(fypp);
		fy_token_unref(fyt_top);
		return 0;

	default:
		fyr_error(fyr, "Unknown token %s\n", fy_token_debug_text_a(fyt_top));
		goto err_out;
	}

	return 0;

err_out:
	fy_token_unref(fyt_top);
	fy_path_expr_free(exprl);
	fy_path_expr_free(exprr);
	fy_path_expr_free(chain);
	fy_path_expr_free(parent);

	// fy_notice(fypp->cfg.diag, "operator stack\n");
	// dump_operator_stack(fypp);
	// fy_notice(fypp->cfg.diag, "operand stack\n");
	// dump_operand_stack(fypp);

	return -1;
}

struct fy_path_expr *
fy_path_parse_expression(struct fy_path_parser *fypp)
{
	struct fy_reader *fyr;
	struct fy_token *fyt = NULL, *fyt_top = NULL;
	struct fy_path_expr *expr;
	int ret;

	/* the parser must be in the correct state */
	if (!fypp || fypp->operator_top || fypp->operand_top)
		return NULL;

	fyr = &fypp->reader;

	/* find stream start */
	fyt = fy_path_scan_peek(fypp, NULL);
	FYR_PARSE_ERROR_CHECK(fyr, 0, 1, FYEM_PARSE,
			fyt && fyt->type == FYTT_STREAM_START, err_out,
			"no tokens available or start without stream start");

	/* remove stream start */
	fy_token_unref(fy_path_scan_remove(fypp, fyt));
	fyt = NULL;

	while ((fyt = fy_path_scan_peek(fypp, NULL)) != NULL) {

		if (fyt->type == FYTT_STREAM_END)
			break;

		// FYR_TOKEN_DIAG(fyr, fyt, FYET_NOTICE, FYEM_PARSE, "next token %s", fy_token_debug_text_a(fyt));

		/* if it's an operand convert it to expression and push */
		if (fy_token_type_is_operand(fyt->type)) {

			expr = fy_path_expr_alloc_recycle(fypp);
			fyr_error_check(fyr, expr, err_out,
					"fy_path_expr_alloc_recycle() failed\n");

			expr->fyt = fy_path_scan_remove(fypp, fyt);
			expr->type = fy_map_token_to_path_expr_type(fyt->type);
			fyt = NULL;

			ret = push_operand(fypp, expr);
			fyr_error_check(fyr, !ret, err_out, "push_operand() failed\n");
			expr = NULL;

			continue;
		}

		/* it's an operator */
		for (;;) {
			/* get the top of the operator stack */
			fyt_top = peek_operator(fypp);
			/* if operator stack is empty or the priority of the new operator is larger, push operator */
			if (!fyt_top || fy_token_type_operator_prec(fyt->type) > fy_token_type_operator_prec(fyt_top->type) ||
					fyt_top->type == FYTT_PE_LPAREN) {
				fyt = fy_path_scan_remove(fypp, fyt);
				ret = push_operator(fypp, fyt);
				fyt = NULL;
				fyr_error_check(fyr, !ret, err_out, "push_operator() failed\n");
				break;
			}

			ret = evaluate(fypp);
			/* evaluate will print diagnostic on error */
			if (ret)
				goto err_out;
		}
	}

	FYR_PARSE_ERROR_CHECK(fyr, 0, 1, FYEM_PARSE,
			fypp->stream_error || (fyt && fyt->type == FYTT_STREAM_END), err_out,
			"stream ended without STREAM_END");

	while ((fyt_top = peek_operator(fypp)) != NULL) {
		ret = evaluate(fypp);
		/* evaluate will print diagnostic on error */
		if (ret)
			goto err_out;
	}

	FYR_TOKEN_ERROR_CHECK(fyr, fyt, FYEM_PARSE,
			fypp->operand_top == 1, err_out,
			"invalid operand stack at end");

	/* remove stream end */
	fy_token_unref(fy_path_scan_remove(fypp, fyt));
	fyt = NULL;

	/* and return the last operand */
	return pop_operand(fypp);

err_out:
	// fy_notice(fypp->cfg.diag, "operator stack\n");
	// dump_operator_stack(fypp);
	// fy_notice(fypp->cfg.diag, "operand stack\n");
	// dump_operand_stack(fypp);
	fypp->stream_error = true;
	return NULL;
}

static struct fy_node *
fy_path_expr_execute_single_result(struct fy_diag *diag, struct fy_path_expr *expr, struct fy_node *fyn)
{
	struct fy_token *fyt;
	struct fy_anchor *fya;
	const char *text;
	size_t len;

	assert(expr);

	switch (expr->type) {
	case fpet_root:
		return fyn->fyd->root;

	case fpet_this:
		return fyn;

	case fpet_parent:
		return fyn->parent;

	case fpet_alias:
		fyt = expr->fyt;
		assert(fyt);
		assert(fyt->type == FYTT_PE_ALIAS);

		text = fy_token_get_text(fyt, &len);
		if (!text || len < 1)
			break;

		if (*text == '*') {
			text++;
			len--;
		}
		fya = fy_document_lookup_anchor(fyn->fyd, text, len);
		if (!fya)
			break;
		return fya->fyn;

	case fpet_seq_index:
		fyt = expr->fyt;
		assert(fyt);
		assert(fyt->type == FYTT_PE_SEQ_INDEX);

		/* only on sequence */
		if (!fy_node_is_sequence(fyn))
			break;

		return fy_node_sequence_get_by_index(fyn, fyt->seq_index.index);

	case fpet_map_key:
		fyt = expr->fyt;
		assert(fyt);
		assert(fyt->type == FYTT_PE_MAP_KEY);

		if (!fyt->map_key.fyd) {
			/* simple key */
			text = fy_token_get_text(fyt, &len);
			if (!text || len < 1)
				break;
			return fy_node_mapping_lookup_value_by_simple_key(fyn, text, len);
		}

		return fy_node_mapping_lookup_value_by_key(fyn, fyt->map_key.fyd->root);

	case fpet_filter_scalar:
		if (!(fy_node_is_scalar(fyn) || fy_node_is_alias(fyn)))
			break;
		return fyn;

	case fpet_filter_collection:
		if (!(fy_node_is_mapping(fyn) || fy_node_is_sequence(fyn)))
			break;
		return fyn;

	case fpet_filter_sequence:
		if (!fy_node_is_sequence(fyn))
			break;
		return fyn;

	case fpet_filter_mapping:
		if (!fy_node_is_mapping(fyn))
			break;
		return fyn;

	default:
		break;
	}

	return NULL;
}

/* returns true if the expression is chain or multi and contains only single
 * result expressions
 */
static bool expr_is_leaf_chain_or_multi(struct fy_path_expr *expr)
{
	struct fy_path_expr *exprn;

	if (!expr || !fy_path_expr_type_is_parent(expr->type))
		return false;

	for (exprn = fy_path_expr_list_head(&expr->children); exprn;
		exprn = fy_path_expr_next(&expr->children, exprn)) {

		if (!fy_path_expr_type_is_single_result(exprn->type))
			return false;
	}

	/* all expressions single result */
	return true;
}

int fy_path_expr_execute(struct fy_diag *diag, struct fy_path_expr *expr,
			 struct fy_walk_result_list *results, struct fy_node *fyn)
{
	struct fy_node *fynn, *fyni;
	struct fy_node_pair *fynp;
	struct fy_token *fyt;
	struct fy_path_expr *exprn;
	struct fy_walk_result *fwrn;
	struct fy_walk_result_list tresults, nresults;
	int start, end, count, i;

	/* error */
	if (!expr || !results)
		return -1;

	/* no node, just return */
	if (!fyn)
		return 0;

	/* single result case is common enough to optimize */
	if (fy_path_expr_type_is_single_result(expr->type)) {

		fynn = fy_path_expr_execute_single_result(diag, expr, fyn);
		if (!fynn)
			return 0;
		return fy_walk_result_add(results, fynn);
	}

	/* handle the remaining multi result cases */
	switch (expr->type) {
	case fpet_chain:

		/* check if it's a leaf chain (i.e. chain with single result expressions only) */
		if (expr_is_leaf_chain_or_multi(expr)) {

			/* optimized single result chain */
			for (exprn = fy_path_expr_list_head(&expr->children); exprn;
				exprn = fy_path_expr_next(&expr->children, exprn)) {

				assert(fy_path_expr_type_is_single_result(exprn->type));

				fynn = fy_path_expr_execute_single_result(diag, exprn, fyn);
				if (!fynn)
					return 0;
				fyn = fynn;
			}

			return fy_walk_result_add(results, fyn);
		}

		/* start with tresults containing the current node */
		fy_walk_result_list_init(&tresults);
		fy_walk_result_add(&tresults, fyn);

		/* iterate over each chain item */
		for (exprn = fy_path_expr_list_head(&expr->children); exprn;
			exprn = fy_path_expr_next(&expr->children, exprn)) {

			/* nresults is the temp list collecting the results of each step */
			fy_walk_result_list_init(&nresults);

			/* for every node in the tresults execute */
			while ((fwrn = fy_walk_result_list_pop(&tresults)) != NULL) {
				fynn = fwrn->fyn;
				fy_walk_result_free(fwrn);

				fy_path_expr_execute(diag, exprn, &nresults, fynn);
			}

			/* move everything from nresults to tresults */
			fy_walk_result_list_move(&tresults, &nresults);
		}

		/* move everything in tresuls to results */
		fy_walk_result_list_move(results, &tresults);

		return 0;

	case fpet_multi:

		/* iterate over each chain item */
		for (exprn = fy_path_expr_list_head(&expr->children); exprn;
			exprn = fy_path_expr_next(&expr->children, exprn)) {

			/* nresults is the temp list collecting the results of each step */
			fy_walk_result_list_init(&nresults);

			fy_path_expr_execute(diag, exprn, &nresults, fyn);

			/* move everything in nresuls to results */
			fy_walk_result_list_move(results, &nresults);
		}

		return 0;

	case fpet_every_child:

		/* every scalar/alias is a single result */
		if (fy_node_is_scalar(fyn) || fy_node_is_alias(fyn)) {

			fy_walk_result_add(results, fyn);

		} else if (fy_node_is_sequence(fyn)) {

			for (fyni = fy_node_list_head(&fyn->sequence); fyni;
				fyni = fy_node_next(&fyn->sequence, fyni)) {
				fy_walk_result_add(results, fyni);
			}

		} else if (fy_node_is_mapping(fyn)) {

			for (fynp = fy_node_pair_list_head(&fyn->mapping); fynp;
					fynp = fy_node_pair_next(&fyn->mapping, fynp)) {
				fy_walk_result_add(results, fynp->value);
			}
		} else
			assert(0);

		return 0;

	case fpet_every_child_r:
		return fy_walk_result_add_recursive(results, fyn, false);

	case fpet_seq_slice:

		fyt = expr->fyt;
		assert(fyt);
		assert(fyt->type == FYTT_PE_SEQ_SLICE);

		/* only on sequence */
		if (!fy_node_is_sequence(fyn))
			return 0;

		start = fyt->seq_slice.start_index;
		end = fyt->seq_slice.end_index;
		count = fy_node_sequence_item_count(fyn);

		/* don't handle negative slices yet */
		if (start < 0 || end < 1 || start >= end)
			return 0;

		if (count < end)
			end = count;

		for (i = start; i < end; i++) {
			fynn = fy_node_sequence_get_by_index(fyn, i);
			fy_walk_result_add(results, fynn);
		}

		return 0;

	case fpet_logical_or:

		/* iterate over each chain item */
		for (exprn = fy_path_expr_list_head(&expr->children); exprn;
			exprn = fy_path_expr_next(&expr->children, exprn)) {

			/* nresults is the temp list collecting the results of each step */
			fy_walk_result_list_init(&nresults);

			fy_path_expr_execute(diag, exprn, &nresults, fyn);

			/* the first non empty result ends */
			if (!fy_walk_result_list_empty(&nresults)) {
				/* move everything in nresuls to results */
				fy_walk_result_list_move(results, &nresults);
				break;
			}
		}

		return 0;

	case fpet_logical_and:

		/* last non null result */
		fy_walk_result_list_init(&nresults);

		/* iterate over each chain item */
		for (exprn = fy_path_expr_list_head(&expr->children); exprn;
			exprn = fy_path_expr_next(&expr->children, exprn)) {

			/* nresults is the temp list collecting the results of each step */
			fy_walk_result_list_init(&tresults);

			fy_path_expr_execute(diag, exprn, &tresults, fyn);

			/* the first non empty result ends */
			if (fy_walk_result_list_empty(&tresults))
				break;

			/* free the previous results */
			fy_walk_result_list_free(&nresults);

			/* move everything in tresuls to nresults */
			fy_walk_result_list_move(&nresults, &tresults);
		}

		/* and move from nresults to results */
		fy_walk_result_list_move(results, &nresults);
		return 0;

	default:
		break;
	}

	return -1;
}

int fy_path_exec_setup(struct fy_path_exec *fypx, const struct fy_path_exec_cfg *xcfg)
{
	if (!fypx)
		return -1;
	memset(fypx, 0, sizeof(*fypx));
	if (xcfg)
		fypx->cfg = *xcfg;
	fy_walk_result_list_init(&fypx->results);
	return 0;
}

void fy_path_exec_cleanup(struct fy_path_exec *fypx)
{
	if (!fypx)
		return;
	fy_walk_result_list_free(&fypx->results);
	fypx->fyn_start = NULL;
}

/* publicly exported methods */
struct fy_path_parser *fy_path_parser_create(const struct fy_path_parse_cfg *pcfg)
{
	struct fy_path_parser *fypp;

	fypp = malloc(sizeof(*fypp));
	if (!fypp)
		return NULL;
	fy_path_parser_setup(fypp, pcfg);
	return fypp;
}

void fy_path_parser_destroy(struct fy_path_parser *fypp)
{
	if (!fypp)
		return;
	fy_path_parser_cleanup(fypp);
	free(fypp);
}

int fy_path_parser_reset(struct fy_path_parser *fypp)
{
	if (!fypp)
		return -1;
	fy_path_parser_cleanup(fypp);
	return 0;
}

struct fy_path_expr *
fy_path_parse_expr_from_string(struct fy_path_parser *fypp,
			       const char *str, size_t len)
{
	struct fy_path_expr *expr = NULL;
	struct fy_input *fyi = NULL;
	int rc;

	if (!fypp || !str || !len)
		return NULL;

	fy_path_parser_reset(fypp);

	fyi = fy_input_from_data(str, len, NULL, false);
	if (!fyi) {
		fy_error(fypp->cfg.diag, "failed to create ypath input from %.*s\n",
				(int)len, str);
		goto err_out;
	}

	rc = fy_path_parser_open(fypp, fyi, NULL);
	if (rc) {
		fy_error(fypp->cfg.diag, "failed to open path parser input from %.*s\n",
				(int)len, str);
		goto err_out;
	}

	expr = fy_path_parse_expression(fypp);
	if (!expr) {
		fy_error(fypp->cfg.diag, "failed to parse path expression %.*s\n",
				(int)len, str);
		goto err_out;
	}
	fy_input_unref(fyi);

	return expr;

err_out:
	fy_path_expr_free(expr);
	fy_input_unref(fyi);
	return NULL;
}

struct fy_path_expr *
fy_path_expr_build_from_string(const struct fy_path_parse_cfg *pcfg,
			       const char *str, size_t len)
{
	struct fy_path_parser fypp_data, *fypp = &fypp_data;
	struct fy_path_expr *expr = NULL;

	if (!str)
		return NULL;

	fy_path_parser_setup(fypp, pcfg);
	expr = fy_path_parse_expr_from_string(fypp, str, len);
	fy_path_parser_cleanup(fypp);

	return expr;
}

struct fy_path_exec *fy_path_exec_create(const struct fy_path_exec_cfg *xcfg)
{
	struct fy_path_exec *fypx;

	fypx = malloc(sizeof(*fypx));
	if (!fypx)
		return NULL;
	fy_path_exec_setup(fypx, xcfg);
	return fypx;
}

void fy_path_exec_destroy(struct fy_path_exec *fypx)
{
	if (!fypx)
		return;
	fy_path_exec_cleanup(fypx);
	free(fypx);
}

int fy_path_exec_reset(struct fy_path_exec *fypx)
{
	if (!fypx)
		return -1;
	fy_path_exec_cleanup(fypx);
	return 0;
}

int fy_path_exec_execute(struct fy_path_exec *fypx, struct fy_path_expr *expr, struct fy_node *fyn_start)
{
	int rc;

	if (!fypx || !expr || !fyn_start)
		return -1;

	fy_walk_result_list_free(&fypx->results);
	fypx->fyn_start = fyn_start;

	rc = fy_path_expr_execute(fypx->cfg.diag, expr, &fypx->results, fypx->fyn_start);
	if (rc) {
		fy_walk_result_list_free(&fypx->results);
		return rc;
	}

	return 0;
}

struct fy_node *
fy_path_exec_results_iterate(struct fy_path_exec *fypx, void **prevp)
{
	struct fy_walk_result *fwr;

	if (!fypx || !prevp)
		return NULL;

	if (!*prevp)
		fwr = fy_walk_result_list_head(&fypx->results);
	else
		fwr = fy_walk_result_next(&fypx->results, *prevp);

	*prevp = fwr;
	return fwr ? fwr->fyn : NULL;
}
