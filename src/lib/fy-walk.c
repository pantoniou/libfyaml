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
#include <math.h>
#include <limits.h>

#include <libfyaml.h>

#include "fy-parse.h"
#include "fy-doc.h"
#include "fy-walk.h"

#include "fy-utils.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) ((sizeof(x)/sizeof((x)[0])))
#endif

// #undef DEBUG_EXPR
#define DEBUG_EXPR

const char *fy_walk_result_type_txt[FWRT_COUNT] = {
	[fwrt_node_ref]	= "node-ref",
	[fwrt_number]	= "number",
	[fwrt_string]	= "string",
	[fwrt_doc]	= "doc",
	[fwrt_refs]	= "refs",
};

void fy_walk_result_dump(struct fy_walk_result *fwr, struct fy_diag *diag, enum fy_error_type errlevel, int level,
		const char *fmt, ...);

void fy_walk_result_vdump(struct fy_walk_result *fwr, struct fy_diag *diag, enum fy_error_type errlevel, int level,
		const char *fmt, va_list ap)
{
	struct fy_walk_result *fwr2;
	char *banner;
	char *texta = NULL;
	const char *text;
	size_t len;
	bool save_on_error;
	char buf[30];

	if (!diag)
		return;

	if (errlevel < diag->cfg.level)
		return;

	save_on_error = diag->on_error;
	diag->on_error = true;

	if (fmt) {
		banner = NULL;
		(void)vasprintf(&banner, fmt, ap);
		assert(banner);
		fy_diag_diag(diag, errlevel, "%-*s%s", level*2, "", banner);
		free(banner);
	}

	switch (fwr->type) {
	case fwrt_node_ref:
		texta = fy_node_get_path(fwr->fyn);
		assert(texta);
		text = texta;
		break;
	case fwrt_number:
		snprintf(buf, sizeof(buf), "%f", fwr->number);
		text = buf;
		break;
	case fwrt_string:
		text = fwr->string;
		break;
	case fwrt_doc:
		texta = fy_emit_document_to_string(fwr->fyd, FYECF_WIDTH_INF | FYECF_INDENT_DEFAULT | FYECF_MODE_FLOW_ONELINE);
		assert(texta);
		text = texta;
		break;
	case fwrt_refs:
		text="";
		break;
	}

	len = strlen(text);

	fy_diag_diag(diag, errlevel, "%-*s%s%s%.*s",
			(level + 1) * 2, "",
			fy_walk_result_type_txt[fwr->type],
			len ? " " : "",
			(int)len, text);

	if (texta)
		free(texta);

	if (fwr->type == fwrt_refs) {
		for (fwr2 = fy_walk_result_list_head(&fwr->refs); fwr2; fwr2 = fy_walk_result_next(&fwr->refs, fwr2))
			fy_walk_result_dump(fwr2, diag, errlevel, level + 1, NULL);
	}

	diag->on_error = save_on_error;
}

void fy_walk_result_dump(struct fy_walk_result *fwr, struct fy_diag *diag, enum fy_error_type errlevel, int level,
		const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_walk_result_vdump(fwr, diag, errlevel, level, fmt, ap);
	va_end(ap);
}

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
	fwr->type = fwrt_node_ref;	/* by default it's a node ref */
	return fwr;
}

void fy_walk_result_clean(struct fy_walk_result *fwr)
{
	struct fy_walk_result *fwrn;

	if (!fwr)
		return;

	switch (fwr->type) {
	case fwrt_node_ref:
	case fwrt_number:
		break;
	case fwrt_string:
		if (fwr->string)
			free(fwr->string);
		break;
	case fwrt_doc:
		fy_document_destroy(fwr->fyd);
		break;
	case fwrt_refs:
		while ((fwrn = fy_walk_result_list_pop(&fwr->refs)) != NULL)
			fy_walk_result_free(fwrn);
		break;
	}
	memset(fwr, 0, sizeof(*fwr));
}

struct fy_walk_result *fy_walk_result_clone(struct fy_walk_result *fwr)
{
	struct fy_walk_result *fwrn = NULL, *fwrn2 = NULL, *fwrn3;

	if (!fwr)
		return NULL;
	fwrn = fy_walk_result_alloc();
	if (!fwrn)
		return NULL;

	fwrn->type = fwr->type;
	switch (fwr->type) {
	case fwrt_node_ref:
		fwrn->fyn = fwr->fyn;
		break;
	case fwrt_number:
		fwrn->number = fwr->number;
		break;
	case fwrt_string:
		fwrn->string = strdup(fwr->string);
		if (!fwrn->string)
			goto err_out;
		break;
	case fwrt_doc:
		fwrn->fyd = fy_document_clone(fwr->fyd);
		if (!fwrn->fyd)
			goto err_out;
		break;
	case fwrt_refs:

		fy_walk_result_list_init(&fwrn->refs);

		for (fwrn2 = fy_walk_result_list_head(&fwr->refs); fwrn2;
			fwrn2 = fy_walk_result_next(&fwr->refs, fwrn2)) {

			fwrn3 = fy_walk_result_clone(fwrn2);
			if (!fwrn3)
				goto err_out;

			fy_walk_result_list_add_tail(&fwrn->refs, fwrn3);
		}
		break;
	}
	return fwrn;
err_out:
	if (fwrn)
		fy_walk_result_free(fwrn);
	return NULL;
}

void fy_walk_result_free(struct fy_walk_result *fwr)
{
	struct fy_walk_result *fwrn;

	if (!fwr)
		return;

	switch (fwr->type) {
	case fwrt_node_ref:
	case fwrt_number:
		break;
	case fwrt_string:
		if (fwr->string)
			free(fwr->string);
		break;
	case fwrt_doc:
		if (fwr->fyd)
			fy_document_destroy(fwr->fyd);
		break;
	case fwrt_refs:
		while ((fwrn = fy_walk_result_list_pop(&fwr->refs)) != NULL)
			fy_walk_result_free(fwrn);
		break;
	}

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
		if (fwr->type == fwrt_node_ref && fwr->fyn == fyn)
			return 0;
	}

	fwr = fy_walk_result_alloc();
	if (!fwr)
		return -1;
	fwr->type = fwrt_node_ref;
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

		fyn = NULL;
		if (fwr->type == fwrt_node_ref)
			fyn = fwr->fyn;

		fy_walk_result_free(fwr);

		if (!fyn)
			continue;

		ret = fy_walk_result_add(to, fyn);
		if (ret)
			return ret;
	}
	return 0;
}

const char *fy_path_expr_type_txt[FPET_COUNT] = {
	[fpet_none]			= "none",
	/* */
	[fpet_root]			= "root",
	[fpet_this]			= "this",
	[fpet_parent]			= "parent",
	[fpet_every_child]		= "every-child",
	[fpet_every_child_r]		= "every-child-recursive",
	[fpet_filter_collection]	= "filter-collection",
	[fpet_filter_scalar]		= "filter-scalar",
	[fpet_filter_sequence]		= "filter-sequence",
	[fpet_filter_mapping]		= "filter-mapping",
	[fpet_filter_unique]		= "filter-unique",
	[fpet_seq_index]		= "seq-index",
	[fpet_seq_slice]		= "seq-slice",
	[fpet_alias]			= "alias",

	[fpet_map_key]			= "map-key",

	[fpet_multi]			= "multi",
	[fpet_chain]			= "chain",
	[fpet_logical_or]		= "logical-or",
	[fpet_logical_and]		= "logical-and",

	[fpet_eq]			= "equals",
	[fpet_neq]			= "not-equals",
	[fpet_lt]			= "less-than",
	[fpet_gt]			= "greater-than",
	[fpet_lte]			= "less-or-equal-than",
	[fpet_gte]			= "greater-or-equal-than",

	[fpet_scalar]			= "scalar",

	[fpet_plus]			= "plus",
	[fpet_minus]			= "minus",
	[fpet_mult]			= "multiply",
	[fpet_div]			= "divide",

	[fpet_lparen]			= "left-parentheses",
	[fpet_rparen]			= "right-parentheses",

	[fpet_method]			= "method",
	[fpet_expr]			= "expr",
};

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

void fy_expr_stack_setup(struct fy_expr_stack *stack)
{
	if (!stack)
		return;

	memset(stack, 0, sizeof(*stack));
	stack->items = stack->items_static;
	stack->alloc = ARRAY_SIZE(stack->items_static);
}

void fy_expr_stack_cleanup(struct fy_expr_stack *stack)
{
	if (!stack)
		return;

	while (stack->top > 0)
		fy_path_expr_free(stack->items[--stack->top]);

	if (stack->items != stack->items_static)
		free(stack->items);
	stack->items = stack->items_static;
	stack->alloc = ARRAY_SIZE(stack->items_static);
}

void fy_expr_stack_dump(struct fy_diag *diag, struct fy_expr_stack *stack)
{
	struct fy_path_expr *expr;
	unsigned int i;

	if (!stack)
		return;

	if (!stack->top)
		return;

	i = stack->top;
	do {
		expr = stack->items[--i];
		fy_path_expr_dump(expr, diag, FYET_NOTICE, 0, NULL);
	} while (i > 0);
}

int fy_expr_stack_size(struct fy_expr_stack *stack)
{
	if (!stack || stack->top >= (unsigned int)INT_MAX)
		return -1;
	return (int)stack->top;
}

int fy_expr_stack_push(struct fy_expr_stack *stack, struct fy_path_expr *expr)
{
	struct fy_path_expr **items_new;
	unsigned int alloc;
	size_t size;

	if (!stack || !expr)
		return -1;

	assert(stack->items);
	assert(stack->alloc > 0);

	assert(expr->fyt);

	/* grow the stack if required */
	if (stack->top >= stack->alloc) {
		alloc = stack->alloc;
		size = alloc * sizeof(*items_new);

		if (stack->items == stack->items_static) {
			items_new = malloc(size * 2);
			if (items_new)
				memcpy(items_new, stack->items_static, size);
		} else
			items_new = realloc(stack->items, size * 2);

		if (!items_new)
			return -1;

		stack->alloc = alloc * 2;
		stack->items = items_new;
	}

	stack->items[stack->top++] = expr;

	return 0;
}

struct fy_path_expr *fy_expr_stack_peek_at(struct fy_expr_stack *stack, unsigned int pos)
{
	if (!stack || stack->top <= pos)
		return NULL;
	return stack->items[stack->top - 1 - pos];
}

struct fy_path_expr *fy_expr_stack_peek(struct fy_expr_stack *stack)
{
	return fy_expr_stack_peek_at(stack, 0);
}

struct fy_path_expr *fy_expr_stack_pop(struct fy_expr_stack *stack)
{
	if (!stack || !stack->top)
		return NULL;

	return stack->items[--stack->top];
}

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
	       type == FYTT_PE_LPAREN ||
	       type == FYTT_PE_EQEQ ||
	       type == FYTT_PE_NOTEQ ||
	       type == FYTT_PE_LT ||
	       type == FYTT_PE_GT ||
	       type == FYTT_PE_LTE ||
	       type == FYTT_PE_GTE;
}

bool fy_token_type_is_filter(enum fy_token_type type)
{
	return type == FYTT_PE_SCALAR_FILTER ||
	       type == FYTT_PE_COLLECTION_FILTER ||
	       type == FYTT_PE_SEQ_FILTER ||
	       type == FYTT_PE_MAP_FILTER ||
	       type == FYTT_PE_UNIQUE_FILTER;
}

const char *path_parser_scan_mode_txt[FYPPSM_COUNT] = {
	[fyppsm_none]		= "none",
	[fyppsm_path_expr]	= "path_expr",
	[fyppsm_scalar_expr]	= "scalar_expr",
};

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

	fy_expr_stack_setup(&fypp->operators);
	fy_expr_stack_setup(&fypp->operands);

	fy_path_expr_list_init(&fypp->expr_recycle);
	fypp->suppress_recycling = (fypp->cfg.flags & FYPPCF_DISABLE_RECYCLING) || getenv("FY_VALGRIND");

	fypp->scan_mode = fyppsm_path_expr;
	fypp->paren_nest_level = 0;
}

void fy_path_parser_cleanup(struct fy_path_parser *fypp)
{
	struct fy_path_expr *expr;

	if (!fypp)
		return;

	fy_expr_stack_cleanup(&fypp->operands);
	fy_expr_stack_cleanup(&fypp->operators);

	fy_reader_cleanup(&fypp->reader);
	fy_token_list_unref_all(&fypp->queued_tokens);

	while ((expr = fy_path_expr_list_pop(&fypp->expr_recycle)) != NULL)
		fy_path_expr_free(expr);

	fypp->last_queued_token_type = FYTT_NONE;
}

int fy_path_parser_open(struct fy_path_parser *fypp,
			struct fy_input *fyi, const struct fy_reader_input_cfg *icfg)
{
	int ret;
	if (!fypp)
		return -1;

	ret = fy_reader_input_open(&fypp->reader, fyi, icfg);
	if (ret)
		return ret;
	/* take a reference to the input */
	fypp->fyi = fy_input_ref(fyi);
	return 0;
}

void fy_path_parser_close(struct fy_path_parser *fypp)
{
	if (!fypp)
		return;

	fy_input_unref(fypp->fyi);

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

int fy_path_fetch_simple_alnum(struct fy_path_parser *fypp, int c, enum fy_token_type type)
{
	struct fy_reader *fyr;
	struct fy_token *fyt;
	struct fy_atom *handlep;
	int i;

	fyr = &fypp->reader;

	/* verify that the called context is correct */
	assert(fy_is_first_alpha(c));
	i = 1;
	while (fy_is_alnum(fy_reader_peek_at(fyr, i)))
		i++;

	handlep = fy_reader_fill_atom_a(fyr, i);
	if (type == FYTT_SCALAR) {
		fyt = fy_path_token_queue(fypp, FYTT_SCALAR, handlep, FYSS_PLAIN, NULL);
		fyr_error_check(fyr, fyt, err_out, "fy_path_token_queue() failed\n");
		fyt->scalar.number_hint = false;
	} else {
		fyt = fy_path_token_queue(fypp, type, handlep, NULL);
		fyr_error_check(fyr, fyt, err_out, "fy_path_token_queue() failed\n");
	}

	return 0;

err_out:
	fypp->stream_error = true;
	return -1;
}

int fy_path_fetch_simple_map_key(struct fy_path_parser *fypp, int c)
{
	return fy_path_fetch_simple_alnum(fypp, c, FYTT_PE_MAP_KEY);
}

int fy_path_fetch_plain_scalar(struct fy_path_parser *fypp, int c)
{
	return fy_path_fetch_simple_alnum(fypp, c, FYTT_SCALAR);
}

int fy_path_fetch_dot_method(struct fy_path_parser *fypp, int c)
{
	struct fy_reader *fyr;
	struct fy_token *fyt;
	struct fy_atom *handlep;
	static const struct {
		const char *method;
		enum fy_token_type type;
	} shorthand[] = {
		{
			.method = ".uniq",
			.type	= FYTT_PE_UNIQUE_FILTER,
		}
	};
	int i;
	const char *method;
	enum fy_token_type fytt;
	unsigned int j;

	fyr = &fypp->reader;

	assert(c == '.' && fy_is_first_alpha(fy_reader_peek_at(fyr, 1)));

	/* verify that the called context is correct */
	i = 2;
	while (fy_is_alnum(fy_reader_peek_at(fyr, i)))
		i++;

	method = NULL;
	fytt = FYTT_NONE;
	for (j = 0; j < ARRAY_SIZE(shorthand); j++) {
		method = shorthand[j].method;

		if ((size_t)i != strlen(method))
			continue;

		fyr_notice(fyr, "%s: method = \"%s\", i=%d strlen(method)=%zu, \"%.*s\"\n", __func__,
				method, i, strlen(method),
				i, (char *)fy_reader_ensure_lookahead(fyr, i, NULL));

		if (!memcmp(fy_reader_ensure_lookahead(fyr, i, NULL), method, i))
			break;
	}

	FYR_PARSE_ERROR_CHECK(fyr, 0, i, FYEM_SCAN,
			j < ARRAY_SIZE(shorthand), err_out,
			"unknown dot method");

	fytt = shorthand[j].type;

	fyr_notice(fyr, "%s: found method = %s\n", __func__, method);

	handlep = fy_reader_fill_atom_a(fyr, i);

	fyt = fy_path_token_queue(fypp, fytt, handlep, NULL);
	fyr_error_check(fyr, fyt, err_out, "fy_path_token_queue() failed\n");

	return 0;

err_out:
	fypp->stream_error = true;
	return -1;
}

int fy_path_fetch_flow_document(struct fy_path_parser *fypp, int c, enum fy_token_type fytt)
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
	fyt = fy_path_token_queue(fypp, fytt, &handle, fyd);
	fyr_error_check(fyr, fyt, err_out, "fy_path_token_queue() failed\n");

	return 0;

err_out:
	fypp->stream_error = true;
	return -1;
}

int fy_path_fetch_flow_map_key(struct fy_path_parser *fypp, int c)
{
	return fy_path_fetch_flow_document(fypp, c, FYTT_PE_MAP_KEY);
}

int fy_path_fetch_flow_scalar(struct fy_path_parser *fypp, int c)
{
	struct fy_reader *fyr;
	struct fy_token *fyt;
	struct fy_atom handle;
	bool is_single;
	int rc = -1;

	fyr = &fypp->reader;

	/* verify that the called context is correct */
	assert(fy_is_path_flow_scalar_start(c));

	is_single = c == '\'';

	rc = fy_reader_fetch_flow_scalar_handle(fyr, c, 0, &handle);
	if (rc)
		goto err_out_rc;

	/* document is NULL, is a simple key */
	fyt = fy_path_token_queue(fypp, FYTT_SCALAR, &handle, is_single ? FYSS_SINGLE_QUOTED : FYSS_DOUBLE_QUOTED);
	fyr_error_check(fyr, fyt, err_out, "fy_path_token_queue() failed\n");

	fyt->scalar.number_hint = false;	/* hint it's a string */

	return 0;

err_out:
	rc = -1;
err_out_rc:
	fypp->stream_error = true;
	return rc;
}

int fy_path_fetch_number(struct fy_path_parser *fypp, int c)
{
	struct fy_reader *fyr;
	struct fy_token *fyt;
	int i, digits;

	fyr = &fypp->reader;

	/* verify that the called context is correct */
	assert(fy_is_num(c) || (c == '-' && fy_is_num(fy_reader_peek_at(fyr, 1))));

	i = 0;
	if (c == '-')
		i++;

	digits = 0;
	while (fy_is_num((c = fy_reader_peek_at(fyr, i)))) {
		i++;
		digits++;
	}
	FYR_PARSE_ERROR_CHECK(fyr, 0, i, FYEM_SCAN,
			digits > 0, err_out,
			"bad number");

	fyt = fy_path_token_queue(fypp, FYTT_SCALAR, fy_reader_fill_atom_a(fyr, i), FYSS_PLAIN);
	fyr_error_check(fyr, fyt, err_out, "fy_path_token_queue() failed\n");

	fyt->scalar.number_hint = true;	/* hint it's a number */

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
	int c, cn, rc, simple_token_count;

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

	switch (fypp->scan_mode) {
	case fyppsm_none:
		assert(0);	/* should never happen */
		break;

	case fyppsm_path_expr:

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
			cn = fy_reader_peek_at(fyr, 1);
			if (cn == '.') {
				type = FYTT_PE_PARENT;
				simple_token_count = 2;
			} else if (!fy_is_first_alpha(cn)) {
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

		case '=':
			cn = fy_reader_peek_at(fyr, 1);
			if (cn == '=') {
				type = FYTT_PE_EQEQ;
				simple_token_count = 2;
				break;
			}
			break;
		
		case '!':
			cn = fy_reader_peek_at(fyr, 1);
			if (cn == '=') {
				type = FYTT_PE_NOTEQ;
				simple_token_count = 2;
				break;
			}
			type = FYTT_PE_UNIQUE_FILTER;
			simple_token_count = 1;
			break;

		case '>':
			cn = fy_reader_peek_at(fyr, 1);
			if (cn == '=') {
				type = FYTT_PE_GTE;
				simple_token_count = 2;
				break;
			}
			type = FYTT_PE_GT;
			simple_token_count = 1;
			break;

		case '<':
			cn = fy_reader_peek_at(fyr, 1);
			if (cn == '=') {
				type = FYTT_PE_LTE;
				simple_token_count = 2;
				break;
			}
			type = FYTT_PE_LT;
			simple_token_count = 1;
			break;

		default:
			break;
		}
		break;

	case fyppsm_scalar_expr:

		switch (c) {
		case '(':
			type = FYTT_PE_LPAREN;
			simple_token_count = 1;
			break;

		case ')':
			type = FYTT_PE_RPAREN;
			simple_token_count = 1;
			break;

		case '+':
			type = FYTT_SE_PLUS;
			simple_token_count = 1;
			break;

		case '-':
			cn = fy_reader_peek_at(fyr, 1);
			if (!fy_is_num(cn)) {
				type = FYTT_SE_PLUS;
				simple_token_count = 1;
				break;
			}
			break;

		case '*':
			type = FYTT_SE_MULT;
			simple_token_count = 1;
			break;

		case '/':
			type = FYTT_SE_DIV;
			simple_token_count = 1;
			break;

		default:
			break;
		}
		break;
	}

	/* simple tokens */
	if (simple_token_count > 0) {
		fyt = fy_path_token_queue(fypp, type, fy_reader_fill_atom_a(fyr, simple_token_count));
		fyr_error_check(fyr, fyt, err_out, "fy_path_token_queue() failed\n");

		return 0;
	}

	switch (fypp->scan_mode) {
	case fyppsm_none:
		assert(0);	/* should never happen */
		break;

	case fyppsm_path_expr:
		if (fy_is_first_alpha(c))
			return fy_path_fetch_simple_map_key(fypp, c);

		if (fy_is_path_flow_key_start(c))
			return fy_path_fetch_flow_map_key(fypp, c);

		if (fy_is_num(c) || (c == '-' && fy_is_num(fy_reader_peek_at(fyr, 1))))
			return fy_path_fetch_seq_index_or_slice(fypp, c);

		if (c == '.' && fy_is_first_alpha(fy_reader_peek_at(fyr, 1)))
			return fy_path_fetch_dot_method(fypp, c);

		break;

	case fyppsm_scalar_expr:

		if (fy_is_first_alpha(c))
			return fy_path_fetch_plain_scalar(fypp, c);

		if (fy_is_path_flow_scalar_start(c))
			return fy_path_fetch_flow_scalar(fypp, c);

		if (fy_is_num(c) || (c == '-' && fy_is_num(fy_reader_peek_at(fyr, 1))))
			return fy_path_fetch_number(fypp, c);

		break;
	}

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
	const char *style = "";
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

	style = "";
	if (expr->type == fpet_scalar) {
		switch (fy_scalar_token_get_style(expr->fyt)) {
		case FYSS_SINGLE_QUOTED:
			style = "'";
			break;
		case FYSS_DOUBLE_QUOTED:
			style = "\"";
			break;
		default:
			style = "";
			break;
		}
	}

	fy_diag_diag(diag, errlevel, "> %-*s%s%s%s%.*s%s",
			level*2, "",
			fy_path_expr_type_txt[expr->type],
			len ? " " : "",
			style, (int)len, text, style);

	for (expr2 = fy_path_expr_list_head(&expr->children); expr2; expr2 = fy_path_expr_next(&expr->children, expr2))
		fy_path_expr_dump(expr2, diag, errlevel, level + 1, NULL);

	diag->on_error = save_on_error;
}

static struct fy_node *
fy_path_expr_to_node_internal(struct fy_document *fyd, struct fy_path_expr *expr)
{
	struct fy_path_expr *expr2;
	const char *style = "";
	const char *text;
	size_t len;
	struct fy_node *fyn = NULL, *fyn2, *fyn_seq = NULL;
	int rc;

	text = fy_token_get_text(expr->fyt, &len);

	/* by default use double quoted style */
	style = "\"";
	if (expr->type == fpet_scalar) {
		switch (fy_scalar_token_get_style(expr->fyt)) {
		case FYSS_SINGLE_QUOTED:
			style = "'";
			break;
		case FYSS_DOUBLE_QUOTED:
			style = "\"";
			break;
		default:
			style = "";
			break;
		}
	}

	/* list is empty this is a terminal */
	if (fy_path_expr_list_empty(&expr->children)) {

		fyn = fy_node_buildf(fyd, "%s: %s%.*s%s",
			fy_path_expr_type_txt[expr->type],
			style, (int)len, text, style);
		if (!fyn)
			return NULL;

		return fyn;
	}

	fyn = fy_node_create_mapping(fyd);
	if (!fyn)
		goto err_out;

	fyn_seq = fy_node_create_sequence(fyd);
	if (!fyn_seq)
		goto err_out;

	for (expr2 = fy_path_expr_list_head(&expr->children); expr2; expr2 = fy_path_expr_next(&expr->children, expr2)) {
		fyn2 = fy_path_expr_to_node_internal(fyd, expr2);
		if (!fyn2)
			goto err_out;
		rc = fy_node_sequence_append(fyn_seq, fyn2);
		if (rc)
			goto err_out;
	}

	rc = fy_node_mapping_append(fyn,
			fy_node_create_scalar(fyd, fy_path_expr_type_txt[expr->type], FY_NT),
			fyn_seq);
	if (rc)
		goto err_out;

	return fyn;

err_out:
	fy_node_free(fyn_seq);
	fy_node_free(fyn);
	return NULL;
}

struct fy_document *fy_path_expr_to_document(struct fy_path_expr *expr)
{
	struct fy_document *fyd = NULL;

	if (!expr)
		return NULL;

	fyd = fy_document_create(NULL);
	if (!fyd)
		return NULL;

	fyd->root = fy_path_expr_to_node_internal(fyd, expr);
	if (!fyd->root)
		goto err_out;

	return fyd;

err_out:
	fy_document_destroy(fyd);
	return NULL;
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
	case FYTT_PE_UNIQUE_FILTER:
		return fpet_filter_unique;
	case FYTT_PE_COMMA:
		return fpet_multi;
	case FYTT_PE_SLASH:
		return fpet_chain;
	case FYTT_PE_BARBAR:
		return fpet_logical_or;
	case FYTT_PE_AMPAMP:
		return fpet_logical_and;

	case FYTT_PE_EQEQ:
		return fpet_eq;
	case FYTT_PE_NOTEQ:
		return fpet_neq;
	case FYTT_PE_LT:
		return fpet_lt;
	case FYTT_PE_GT:
		return fpet_gt;
	case FYTT_PE_LTE:
		return fpet_lte;
	case FYTT_PE_GTE:
		return fpet_gte;

	case FYTT_SCALAR:
		return fpet_scalar;

	case FYTT_SE_PLUS:
		return fpet_plus;
	case FYTT_SE_MINUS:
		return fpet_minus;
	case FYTT_SE_MULT:
		return fpet_mult;
	case FYTT_SE_DIV:
		return fpet_div;

	case FYTT_PE_LPAREN:
		return fpet_lparen;
	case FYTT_PE_RPAREN:
		return fpet_rparen;

	default:
		/* note parentheses do not have an expression */
		assert(0);
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
	       type == FYTT_PE_ALIAS ||

	       type == FYTT_SCALAR;
}

bool fy_token_type_is_operator(enum fy_token_type type)
{
	return type == FYTT_PE_SLASH ||
	       type == FYTT_PE_SCALAR_FILTER ||
	       type == FYTT_PE_COLLECTION_FILTER ||
	       type == FYTT_PE_SEQ_FILTER ||
	       type == FYTT_PE_MAP_FILTER ||
	       type == FYTT_PE_UNIQUE_FILTER ||
	       type == FYTT_PE_SIBLING ||
	       type == FYTT_PE_COMMA ||
	       type == FYTT_PE_BARBAR ||
	       type == FYTT_PE_AMPAMP ||
	       type == FYTT_PE_LPAREN ||
	       type == FYTT_PE_RPAREN ||

	       type == FYTT_PE_EQEQ ||
	       type == FYTT_PE_NOTEQ ||
	       type == FYTT_PE_LT ||
	       type == FYTT_PE_GT ||
	       type == FYTT_PE_LTE ||
	       type == FYTT_PE_GTE ||

	       type == FYTT_SE_PLUS ||
	       type == FYTT_SE_MINUS ||
	       type == FYTT_SE_MULT ||
	       type == FYTT_SE_DIV;
}

bool fy_token_type_is_conditional(enum fy_token_type type)
{
	return type == FYTT_PE_EQEQ ||
	       type == FYTT_PE_NOTEQ ||
	       type == FYTT_PE_LT ||
	       type == FYTT_PE_GT ||
	       type == FYTT_PE_LTE ||
	       type == FYTT_PE_GTE;
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
	case FYTT_PE_UNIQUE_FILTER:
		return 5;
	case FYTT_PE_SIBLING:
		return 20;
	case FYTT_PE_COMMA:
		return 15;
	case FYTT_PE_BARBAR:
	case FYTT_PE_AMPAMP:
		return 4;
	case FYTT_PE_EQEQ:
	case FYTT_PE_NOTEQ:
	case FYTT_PE_LT:
	case FYTT_PE_GT:
	case FYTT_PE_LTE:
	case FYTT_PE_GTE:
		return 3;
	case FYTT_PE_LPAREN:
	case FYTT_PE_RPAREN:
		return 30;
	case FYTT_SE_MULT:
	case FYTT_SE_DIV:
		return 9;
	case FYTT_SE_PLUS:
	case FYTT_SE_MINUS:
		return 8;
	default:
		break;
	}
	return -1;
}

int fy_path_expr_type_prec(enum fy_path_expr_type type)
{
	switch (type) {
	default:
		return -1;	/* terminals */
	case fpet_filter_collection:
	case fpet_filter_scalar:
	case fpet_filter_sequence:
	case fpet_filter_mapping:
	case fpet_filter_unique:
		return 5;
	case fpet_logical_or:
	case fpet_logical_and:
		return 4;
	case fpet_multi:
		return 15;
	case fpet_eq:
	case fpet_neq:
	case fpet_lt:
	case fpet_gt:
	case fpet_lte:
	case fpet_gte:
		return 3;
	case fpet_mult:
	case fpet_div:
		return 9;
	case fpet_plus:
	case fpet_minus:
		return 8;
	case fpet_chain:
		return 10;
	case fpet_lparen:
	case fpet_rparen:
		return 1000;
	}
	return -1;
}

enum fy_path_parser_scan_mode fy_token_type_scan_mode(enum fy_token_type type)
{
	/* parentheses are for both modes */
	if (type == FYTT_PE_LPAREN || type == FYTT_PE_RPAREN)
		return fyppsm_none;

	if (fy_token_type_is_path_expr(type))
		return fyppsm_path_expr;
	if (fy_token_type_is_scalar_expr(type))
		return fyppsm_scalar_expr;
	return fyppsm_none;
}

static inline void
dump_operand_stack(struct fy_path_parser *fypp)
{
	return fy_expr_stack_dump(fypp->cfg.diag, &fypp->operands);
}

static inline int
push_operand(struct fy_path_parser *fypp, struct fy_path_expr *expr)
{
	return fy_expr_stack_push(&fypp->operands, expr);
}

static inline struct fy_path_expr *
peek_operand_at(struct fy_path_parser *fypp, unsigned int pos)
{
	return fy_expr_stack_peek_at(&fypp->operands, pos);
}

static inline struct fy_path_expr *
peek_operand(struct fy_path_parser *fypp)
{
	return fy_expr_stack_peek(&fypp->operands);
}

static inline struct fy_path_expr *
pop_operand(struct fy_path_parser *fypp)
{
	return fy_expr_stack_pop(&fypp->operands);
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
	case FYTT_PE_EQEQ:
	case FYTT_PE_NOTEQ:
	case FYTT_PE_LT:
	case FYTT_PE_GT:
	case FYTT_PE_LTE:
	case FYTT_PE_GTE:
	case FYTT_SE_PLUS:
	case FYTT_SE_MINUS:
	case FYTT_SE_MULT:
	case FYTT_SE_DIV:
		return INFIX;
	case FYTT_PE_SCALAR_FILTER:
	case FYTT_PE_COLLECTION_FILTER:
	case FYTT_PE_SEQ_FILTER:
	case FYTT_PE_MAP_FILTER:
	case FYTT_PE_UNIQUE_FILTER:
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
	if (!expr)
		return NULL;

	return fy_token_start_mark(expr->fyt);
}

const struct fy_mark *fy_path_expr_end_mark(struct fy_path_expr *expr)
{
	if (!expr)
		return NULL;

	return fy_token_end_mark(expr->fyt);
}

struct fy_token *
expr_to_token_mark(struct fy_path_expr *expr, struct fy_input *fyi)
{
	const struct fy_mark *ms, *me;
	struct fy_atom handle;

	if (!expr || !fyi)
		return NULL;

	ms = fy_path_expr_start_mark(expr);
	assert(ms);
	me = fy_path_expr_end_mark(expr);
	assert(me);

	memset(&handle, 0, sizeof(handle));
	handle.start_mark = *ms;
	handle.end_mark = *me;
	handle.fyi = fyi;
	handle.style = FYAS_PLAIN;
	handle.chomp = FYAC_CLIP;

	return fy_token_create(FYTT_INPUT_MARKER, &handle);
}

struct fy_token *
expr_lr_to_token_mark(struct fy_path_expr *exprl, struct fy_path_expr *exprr, struct fy_input *fyi)
{
	const struct fy_mark *ms, *me;
	struct fy_atom handle;

	if (!exprl || !exprr || !fyi)
		return NULL;

	ms = fy_path_expr_start_mark(exprl);
	assert(ms);
	me = fy_path_expr_end_mark(exprr);
	assert(me);

	memset(&handle, 0, sizeof(handle));
	handle.start_mark = *ms;
	handle.end_mark = *me;
	handle.fyi = fyi;
	handle.style = FYAS_PLAIN;
	handle.chomp = FYAC_CLIP;

	return fy_token_create(FYTT_INPUT_MARKER, &handle);
}

int
fy_path_expr_order(struct fy_path_expr *expr1, struct fy_path_expr *expr2)
{
	const struct fy_mark *m1 = NULL, *m2 = NULL;

	if (expr1)
		m1 = fy_path_expr_start_mark(expr1);

	if (expr2)
		m2 = fy_path_expr_start_mark(expr2);

	if (m1 == m2)
		return 0;

	if (!m1)
		return -1;

	if (!m2)
		return 1;

	return m1->input_pos == m2->input_pos ? 0 :
	       m1->input_pos  < m2->input_pos ? -1 : 1;
}

#if 0
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

int evaluate_old(struct fy_path_parser *fypp)
{
	struct fy_reader *fyr;
	struct fy_token *fyt_top = NULL, *fyt_peek;
#ifdef DEBUG_EXPR
	struct fy_token *fyt_markl, *fyt_markr;
#endif
	struct fy_path_expr *exprl = NULL, *exprr = NULL, *chain = NULL, *expr = NULL, *exprk = NULL;
	struct fy_path_expr *parent = NULL;
	enum fy_path_expr_type etype;
	int ret;

	fyr = &fypp->reader;

	fyt_top = pop_operator(fypp, NULL);
	fyr_error_check(fyr, fyt_top, err_out,
			"pop_operator() failed to find token operator to evaluate\n");

#ifdef DEBUG_EXPR
	FYR_TOKEN_DIAG(fyr, fyt_top,
		FYDF_NOTICE, FYEM_PARSE, "location of fyt_top");
#endif


	exprl = NULL;
	exprr = NULL;
	switch (fyt_top->type) {

	case FYTT_PE_SLASH:

		/* dump_operand_stack(fypp); */
		/* dump_operator_stack(fypp); */

		/* try to figure out if this slash is the root or a chain operator */
		exprr = peek_operand(fypp);
		exprl = peek_operand_at(fypp, 1);
		fyt_peek = peek_operator(fypp, NULL);

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

#ifdef DEBUG_EXPR
		fyt_markl = NULL;
		fyt_markr = NULL;

		if (exprr) {
			fyt_markr = expr_to_token_mark(exprr, fypp->fyi);
			assert(fyt_markr);
		}

		if (exprl) {
			fyt_markl = expr_to_token_mark(exprl, fypp->fyi);
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
	case FYTT_PE_UNIQUE_FILTER:

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
		while ((fyt_peek = peek_operator(fypp, NULL)) != NULL) {
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

		fyt_top = pop_operator(fypp, NULL);
		fy_token_unref(fyt_top);
		return 0;

	case FYTT_PE_EQEQ:
	case FYTT_PE_NOTEQ:
	case FYTT_PE_LT:
	case FYTT_PE_GT:
	case FYTT_PE_LTE:
	case FYTT_PE_GTE:

	case FYTT_SE_PLUS:
	case FYTT_SE_MINUS:
	case FYTT_SE_MULT:
	case FYTT_SE_DIV:

		(void)exprk;

		exprr = pop_operand(fypp);
		FYR_TOKEN_ERROR_CHECK(fyr, fyt_top, FYEM_PARSE,
				exprr, err_out,
				"infix operator without operands (rhs)");

#if 0
		FYR_TOKEN_ERROR_CHECK(fyr, fyt_top, FYEM_PARSE,
				exprr->type == fpet_scalar, err_out,
				"infix rhs only supports scalar");
#endif

#ifdef DEBUG_EXPR
		fy_path_expr_dump(exprr, fypp->cfg.diag, FYET_NOTICE, 0, "<infix> RHS");
#endif

		exprl = pop_operand(fypp);
		FYR_TOKEN_ERROR_CHECK(fyr, fyt_top, FYEM_PARSE,
				exprl, err_out,
				"comparison operator without operands (lhs)");

#ifdef DEBUG_EXPR
		fy_path_expr_dump(exprl, fypp->cfg.diag, FYET_NOTICE, 0, "<infix> LHS");
#endif

		/* parent */
		parent = fy_path_expr_alloc_recycle(fypp);
		fyr_error_check(fyr, parent, err_out,
				"fy_path_expr_alloc_recycle() failed\n");

		parent->type = fy_map_token_to_path_expr_type(fyt_top->type);
		parent->fyt = fyt_top;
		fyt_top = NULL;

		fy_path_expr_list_add_tail(&parent->children, exprl);
		exprl = NULL;

		/* XXX verify that the operands are valid */

		/* simple expression without parent */
		fy_path_expr_list_add_tail(&parent->children, exprr);
		exprr = NULL;

		ret = push_operand(fypp, parent);
		fyr_error_check(fyr, !ret, err_out,
				"push_operand() failed\n");
		parent = NULL;
		return 0;

	default:
		fyr_error(fyr, "Unknown token %s\n", fy_token_debug_text_a(fyt_top));
		goto err_out;
	}

	return 0;

err_out:

#ifdef DEBUG_EXPR
	if (fyt_top)
		fy_notice(fypp->cfg.diag, "fyt_top: %.*s (%2d)\n", 20, fy_token_debug_text_a(fyt_top),
				fy_token_type_operator_prec(fyt_top->type));
	if (exprl)
		fy_path_expr_dump(exprl, fypp->cfg.diag, FYET_NOTICE, 0, "exprl:");
	if (exprr)
		fy_path_expr_dump(exprr, fypp->cfg.diag, FYET_NOTICE, 0, "exprr:");
	if (chain)
		fy_path_expr_dump(chain, fypp->cfg.diag, FYET_NOTICE, 0, "chain:");
	if (parent)
		fy_path_expr_dump(parent, fypp->cfg.diag, FYET_NOTICE, 0, "parent:");

	fy_notice(fypp->cfg.diag, "operator stack\n");
	dump_operator_stack(fypp);
	fy_notice(fypp->cfg.diag, "operand stack\n");
	dump_operand_stack(fypp);
#endif

	fy_token_unref(fyt_top);
	fy_path_expr_free(exprl);
	fy_path_expr_free(exprr);
	fy_path_expr_free(chain);
	fy_path_expr_free(parent);

	return -1;
}

struct fy_path_expr *
fy_path_parse_expression_old(struct fy_path_parser *fypp)
{
	struct fy_reader *fyr;
	struct fy_token *fyt = NULL, *fyt_top = NULL;
	struct fy_path_expr *expr;
	enum fy_path_parser_scan_mode old_scan_mode;
	enum fy_token_type fytt = FYTT_NONE;
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

#ifdef DEBUG_EXPR
		FYR_TOKEN_DIAG(fyr, fyt, FYET_NOTICE, FYEM_PARSE, "next token %s", fy_token_debug_text_a(fyt));
#endif
		fytt = fyt->type;

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

			/* fall-through */
		}


		old_scan_mode = fypp->scan_mode;
		switch (fypp->scan_mode) {
		case fyppsm_none:
			assert(0);	/* should never happen */
			break;

		case fyppsm_path_expr:
			switch (fytt) {
			case FYTT_PE_EQEQ:
			case FYTT_PE_NOTEQ:
			case FYTT_PE_LT:
			case FYTT_PE_GT:
			case FYTT_PE_LTE:
			case FYTT_PE_GTE:
				fypp->scan_mode = fyppsm_scalar_expr;
				break;
			default:
				break;
			}
			break;
		case fyppsm_scalar_expr:
			switch (fytt) {
			case FYTT_PE_LPAREN:
				fypp->paren_nest_level++;
				break;
			case FYTT_PE_RPAREN:
				FYR_TOKEN_ERROR_CHECK(fyr, fyt, FYEM_PARSE,
						fypp->scalar_expr_nest_level > 0, err_out,
						"unbalanced parenthesis in scalar expr mode");
				fypp->scalar_expr_nest_level--;
				if (fypp->scalar_expr_nest_level == 0) {
#ifdef DEBUG_EXPR
					fyr_notice(fyr, "going back into path expr mode\n");
#endif
					fypp->scan_mode = fyppsm_path_expr;
				}
				break;
			case FYTT_SCALAR:
				if (fypp->scalar_expr_nest_level == 0) {
#ifdef DEBUG_EXPR
					fyr_notice(fyr, "going back into path expr mode\n");
#endif
					fypp->scan_mode = fyppsm_path_expr;
				}
				break;
			default:
				break;
			}
			break;
		}

		if (old_scan_mode != fypp->scan_mode) {
#ifdef DEBUG_EXPR
			fyr_notice(fyr, "scan_mode %s -> %s\n",
					path_parser_scan_mode_txt[old_scan_mode],
					path_parser_scan_mode_txt[fypp->scan_mode]);
#endif
			if (fyt && fyt->type == FYTT_PE_RPAREN) {
				fyt = fy_path_scan_remove(fypp, fyt);
				ret = push_operator(fypp, fyt, NULL);
				if (ret)
					goto err_out;

				fyt = NULL;
				ret = evaluate(fypp);
				if (ret)
					goto err_out;
			}

			/* evaluate */
			for (;;) {
				/* get the top of the operator stack */
				fyt_top = peek_operator(fypp, NULL);
				if (!fyt_top)
					break;

#ifdef DEBUG_EXPR
				fy_notice(fypp->cfg.diag, "> fyt_top: %.*s (%2d)\n", 20, fy_token_debug_text_a(fyt_top),
					fy_token_type_operator_prec(fyt_top->type));

				fyr_notice(fyr, "fy_token_type_scan_mode(fyt_top->type)=%s, old_scan_mode=%s\n", 
						path_parser_scan_mode_txt[fy_token_type_scan_mode(fyt_top->type)],
						path_parser_scan_mode_txt[old_scan_mode]);
#endif

				ret = evaluate(fypp);
				/* evaluate will print diagnostic on error */
				if (ret)
					goto err_out;

				break;
			}

#ifdef DEBUG_EXPR
			fy_notice(fypp->cfg.diag, "operator stack\n");
			dump_operator_stack(fypp);
			fy_notice(fypp->cfg.diag, "operand stack\n");
			dump_operand_stack(fypp);
			fyr_notice(fyr, "> done with scan mode change\n");
#endif
		}

		/* if was an operand and already consumed */
		if (!fyt)
			continue;

		/* it's an operator */
		for (;;) {
			/* get the top of the operator stack */
			fyt_top = peek_operator(fypp, NULL);
			/* if operator stack is empty or the priority of the new operator is larger, push operator */
			if (!fyt_top || fy_token_type_operator_prec(fyt->type) > fy_token_type_operator_prec(fyt_top->type) ||
					fyt_top->type == FYTT_PE_LPAREN) {
				fyt = fy_path_scan_remove(fypp, fyt);
				ret = push_operator(fypp, fyt, NULL);
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
	if (fypp->stream_error)
		goto err_out;

	FYR_PARSE_ERROR_CHECK(fyr, 0, 1, FYEM_PARSE,
			fypp->stream_error || (fyt && fyt->type == FYTT_STREAM_END), err_out,
			"stream ended without STREAM_END");

	while ((fyt_top = peek_operator(fypp, NULL)) != NULL) {
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
#endif

int push_operand_lr(struct fy_path_parser *fypp,
		    enum fy_path_expr_type type,
		    struct fy_path_expr *exprl, struct fy_path_expr *exprr,
		    bool optimize)
{
	struct fy_reader *fyr;
	struct fy_path_expr *expr = NULL, *exprt;
	const struct fy_mark *ms = NULL, *me = NULL;
	struct fy_atom handle;
	int ret;

	optimize = false;
	assert(exprl || exprr);

	fyr = &fypp->reader;

#if 0
	fyr_notice(fyr, ">>> %s <%s> l=<%s> r=<%s>\n", __func__,
			fy_path_expr_type_txt[type],
			exprl ?fy_path_expr_type_txt[exprl->type] : "NULL",
			exprr ?fy_path_expr_type_txt[exprr->type] : "NULL");
#endif

	expr = fy_path_expr_alloc_recycle(fypp);
	fyr_error_check(fyr, expr, err_out,
			"fy_path_expr_alloc_recycle() failed\n");

	expr->type = type;
	expr->fyt = NULL;

	if (exprl) {
		assert(exprl->fyt);
		ms = fy_token_start_mark(exprl->fyt);
		assert(ms);
	} else {
		ms = fy_token_start_mark(exprr->fyt);
		assert(ms);
	}

	if (exprr) {
		assert(exprr->fyt);
		me = fy_token_end_mark(exprr->fyt);
		assert(me);
	} else {
		me = fy_token_end_mark(exprr->fyt);
		assert(me);
	}

	assert(ms && me);

	memset(&handle, 0, sizeof(handle));
	handle.start_mark = *ms;
	handle.end_mark = *me;
	handle.fyi = fypp->fyi;
	handle.style = FYAS_PLAIN;
	handle.chomp = FYAC_CLIP;

	if (exprl) {
		if (type == exprl->type && fy_path_expr_type_is_mergeable(type)) {
			while ((exprt = fy_path_expr_list_pop(&exprl->children)) != NULL)
				fy_path_expr_list_add_tail(&expr->children, exprt);
			fy_path_expr_free_recycle(fypp, exprl);
		} else
			fy_path_expr_list_add_tail(&expr->children, exprl);
		exprl = NULL;
	}

	if (exprr) {
		if (type == exprr->type && fy_path_expr_type_is_mergeable(type)) {
			while ((exprt = fy_path_expr_list_pop(&exprr->children)) != NULL)
				fy_path_expr_list_add_tail(&expr->children, exprt);
			fy_path_expr_free_recycle(fypp, exprr);
		} else
			fy_path_expr_list_add_tail(&expr->children, exprr);
		exprr = NULL;
	}

	expr->fyt = fy_token_create(FYTT_INPUT_MARKER, &handle);
	fyr_error_check(fyr, expr->fyt, err_out,
			"expr_to_token_mark() failed\n");

	ret = push_operand(fypp, expr);
	fyr_error_check(fyr, !ret, err_out,
			"push_operand() failed\n");

#ifdef DEBUG_EXPR
	FYR_TOKEN_DIAG(fyr, expr->fyt,
		FYDF_NOTICE, FYEM_PARSE, "pushed operand");
#endif

	return 0;
err_out:
	fy_path_expr_free(expr);
	fy_path_expr_free(exprl);
	fy_path_expr_free(exprr);
	return -1;
}

int evaluate_new(struct fy_path_parser *fypp)
{
	struct fy_reader *fyr;
	struct fy_path_expr *expr = NULL, *expr_peek, *exprt;
	struct fy_path_expr *exprl = NULL, *exprr = NULL, *chain = NULL;
	struct fy_path_expr *parent = NULL;
	struct fy_token *fyt;
	enum fy_path_expr_type type;
	int ret;

	fyr = &fypp->reader;

	expr = fy_expr_stack_pop(&fypp->operators);
	fyr_error_check(fyr, expr, err_out,
			"pop_operator() failed to find token operator to evaluate\n");

	assert(expr->fyt);

#ifdef DEBUG_EXPR
	FYR_TOKEN_DIAG(fyr, expr->fyt,
		FYDF_NOTICE, FYEM_PARSE, "poped operator expression");
#endif

	exprl = NULL;
	exprr = NULL;
	type = expr->type;
	switch (type) {

	case fpet_chain:

		/* dump_operand_stack(fypp); */
		/* dump_operator_stack(fypp); */

		/* peek the next operator */
		expr_peek = fy_expr_stack_peek(&fypp->operators);

		/* pop the top in either case */
		exprr = fy_expr_stack_pop(&fypp->operands);
		if (!exprr) {
			fyr_notice(fyr, "ROOT value (with no arguments)\n");

			/* conver to root and push to operands */
			expr->type = fpet_root;

			ret = push_operand(fypp, expr);
			fyr_error_check(fyr, !ret, err_out,
					"push_operand() failed\n");
			return 0;
		}

#ifdef DEBUG_EXPR
		FYR_TOKEN_DIAG(fyr, exprr->fyt,
			FYDF_NOTICE, FYEM_PARSE, "exprr");
#endif

		/* expression is to the left, that means it's a root chain */
		if (fy_path_expr_order(expr, exprr) < 0 &&
		    (!(exprl = fy_expr_stack_peek(&fypp->operands)) ||
		     (expr_peek && fy_path_expr_order(exprl, expr_peek) <= 0))) {

			fyr_notice(fyr, "ROOT operator (with arguments)\n");

			exprl = fy_path_expr_alloc_recycle(fypp);
			fyr_error_check(fyr, exprl, err_out,
					"fy_path_expr_alloc_recycle() failed\n");
			exprl->type = fpet_root;

			/* move token to the root */
			exprl->fyt = expr->fyt;
			expr->fyt = NULL;

		} else if (!(exprl = fy_expr_stack_pop(&fypp->operands))) {
			fyr_notice(fyr, "COLLECTION operator\n");

			exprl = exprr;

			exprr = fy_path_expr_alloc_recycle(fypp);
			fyr_error_check(fyr, exprr, err_out,
					"fy_path_expr_alloc_recycle() failed\n");
			exprr->type = fpet_filter_collection;

			/* move token to the filter collection */
			exprr->fyt = expr->fyt;
			expr->fyt = NULL;

		} else {
			assert(exprr && exprl);

			fyr_notice(fyr, "CHAIN operator\n");
		}

		/* we don't need the chain operator now */
		fy_path_expr_free_recycle(fypp, expr);
		expr = NULL;

		ret = push_operand_lr(fypp, fpet_chain, exprl, exprr, true);
		fyr_error_check(fyr, !ret, err_out,
				"push_operand_lr() failed\n");
		return 0;

	case fpet_multi:
	case fpet_logical_or:
	case fpet_logical_and:

		exprr = fy_expr_stack_pop(&fypp->operands);
		fyr_error_check(fyr, exprr, err_out,
				"fy_expr_stack_pop() failed for exprr\n");
		exprl = fy_expr_stack_pop(&fypp->operands);
		fyr_error_check(fyr, exprl, err_out,
				"fy_expr_stack_pop() failed for exprl\n");

		/* we don't need the operator now */
		fy_path_expr_free_recycle(fypp, expr);
		expr = NULL;

		ret = push_operand_lr(fypp, type, exprl, exprr, true);
		fyr_error_check(fyr, !ret, err_out,
				"push_operand_lr() failed\n");

		break;

	case fpet_filter_collection:
	case fpet_filter_scalar:
	case fpet_filter_sequence:
	case fpet_filter_mapping:
	case fpet_filter_unique:

		exprl = fy_expr_stack_pop(&fypp->operands);
		FYR_TOKEN_ERROR_CHECK(fyr, expr->fyt, FYEM_PARSE,
				exprl, err_out,
				"filter operator without argument");

		exprr = fy_path_expr_alloc_recycle(fypp);
		fyr_error_check(fyr, exprr, err_out,
				"fy_path_expr_alloc_recycle() failed\n");
		exprr->type = type;

		/* move token to the filter collection */
		exprr->fyt = expr->fyt;
		expr->fyt = NULL;

		/* we don't need the operator now */
		fy_path_expr_free_recycle(fypp, expr);
		expr = NULL;

		/* push as a chain */
		ret = push_operand_lr(fypp, fpet_chain, exprl, exprr, true);
		fyr_error_check(fyr, !ret, err_out,
				"push_operand_lr() failed\n");

		break;

	case fpet_lparen:

#if 0
		exprl = fy_expr_stack_peek(&fypp->operands);

		FYR_TOKEN_ERROR_CHECK(fyr, expr->fyt, FYEM_PARSE,
				exprl, err_out,
				"empty expression in parentheses");
#endif

		/* we don't need the ) operator now */
		fy_path_expr_free_recycle(fypp, expr);
		expr = NULL;

		return 0;

	case fpet_rparen:

		/* rparen is right hand side of the expression now */
		exprr = expr;
		expr = NULL;

		/* evaluate until we hit a match to the rparen */
		while ((exprl = fy_expr_stack_peek(&fypp->operators)) != NULL) {

			if (exprl->type == fpet_lparen)
				break;

			ret = evaluate_new(fypp);
			if (ret)
				goto err_out;
		}

		FYR_TOKEN_ERROR_CHECK(fyr, exprr->fyt, FYEM_PARSE,
				exprl, err_out,
				"missing matching left parentheses");

		exprl = fy_expr_stack_pop(&fypp->operators);
		assert(exprl);

		exprt = fy_expr_stack_peek(&fypp->operands);

		/* already is an expression, reuse */
		if (exprt && exprt->type == fpet_expr) {

			fyt = expr_lr_to_token_mark(exprl, exprr, fypp->fyi);
			fyr_error_check(fyr, fyt, err_out,
					"expr_lr_to_token_mark() failed\n");
			fy_token_unref(exprt->fyt);
			exprt->fyt = fyt;

			/* we don't need the parentheses operators */
			fy_path_expr_free_recycle(fypp, exprl);
			exprl = NULL;
			fy_path_expr_free_recycle(fypp, exprr);
			exprr = NULL;

			return 0;
		}

		expr = fy_path_expr_alloc_recycle(fypp);
		fyr_error_check(fyr, expr, err_out,
				"fy_path_expr_alloc_recycle() failed\n");
		expr->type = fpet_expr;

		expr->fyt = expr_lr_to_token_mark(exprl, exprr, fypp->fyi);

		exprt = fy_expr_stack_pop(&fypp->operands);

		FYR_TOKEN_ERROR_CHECK(fyr, exprr->fyt, FYEM_PARSE,
				exprt, err_out,
				"empty expression in parentheses");

		/* we don't need the parentheses operators */
		fy_path_expr_free_recycle(fypp, exprl);
		exprl = NULL;
		fy_path_expr_free_recycle(fypp, exprr);
		exprr = NULL;

		fy_path_expr_list_add_tail(&expr->children, exprt);

		ret = push_operand(fypp, expr);
		fyr_error_check(fyr, !ret, err_out,
				"push_operand() failed\n");

#ifdef DEBUG_EXPR
		FYR_TOKEN_DIAG(fyr, expr->fyt,
			FYDF_NOTICE, FYEM_PARSE, "pushed operand");
#endif

		return 0;

		/* shoud never */
	case fpet_method:
	case fpet_expr:
		assert(0);
		abort();


#if 0
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

	case FYTT_PE_EQEQ:
	case FYTT_PE_NOTEQ:
	case FYTT_PE_LT:
	case FYTT_PE_GT:
	case FYTT_PE_LTE:
	case FYTT_PE_GTE:

	case FYTT_SE_PLUS:
	case FYTT_SE_MINUS:
	case FYTT_SE_MULT:
	case FYTT_SE_DIV:

		(void)exprk;

		exprr = pop_operand(fypp);
		FYR_TOKEN_ERROR_CHECK(fyr, fyt_top, FYEM_PARSE,
				exprr, err_out,
				"infix operator without operands (rhs)");

#if 0
		FYR_TOKEN_ERROR_CHECK(fyr, fyt_top, FYEM_PARSE,
				exprr->type == fpet_scalar, err_out,
				"infix rhs only supports scalar");
#endif

#ifdef DEBUG_EXPR
		fy_path_expr_dump(exprr, fypp->cfg.diag, FYET_NOTICE, 0, "<infix> RHS");
#endif

		exprl = pop_operand(fypp);
		FYR_TOKEN_ERROR_CHECK(fyr, fyt_top, FYEM_PARSE,
				exprl, err_out,
				"comparison operator without operands (lhs)");

#ifdef DEBUG_EXPR
		fy_path_expr_dump(exprl, fypp->cfg.diag, FYET_NOTICE, 0, "<infix> LHS");
#endif

		/* parent */
		parent = fy_path_expr_alloc_recycle(fypp);
		fyr_error_check(fyr, parent, err_out,
				"fy_path_expr_alloc_recycle() failed\n");

		parent->type = fy_map_token_to_path_expr_type(fyt_top->type);
		parent->fyt = fyt_top;
		fyt_top = NULL;

		fy_path_expr_list_add_tail(&parent->children, exprl);
		exprl = NULL;

		/* XXX verify that the operands are valid */

		/* simple expression without parent */
		fy_path_expr_list_add_tail(&parent->children, exprr);
		exprr = NULL;

		ret = push_operand(fypp, parent);
		fyr_error_check(fyr, !ret, err_out,
				"push_operand() failed\n");
		parent = NULL;
		return 0;
#endif
	default:
		fyr_error(fyr, "Unknown expression %s\n", fy_path_expr_type_txt[expr->type]);
		goto err_out;
	}

	return 0;

err_out:

#ifdef DEBUG_EXPR
	if (expr)
		fy_path_expr_dump(expr, fypp->cfg.diag, FYET_NOTICE, 0, "expr:");
	if (exprl)
		fy_path_expr_dump(exprl, fypp->cfg.diag, FYET_NOTICE, 0, "exprl:");
	if (exprr)
		fy_path_expr_dump(exprr, fypp->cfg.diag, FYET_NOTICE, 0, "exprr:");
	if (chain)
		fy_path_expr_dump(chain, fypp->cfg.diag, FYET_NOTICE, 0, "chain:");
	if (parent)
		fy_path_expr_dump(parent, fypp->cfg.diag, FYET_NOTICE, 0, "parent:");

	fy_notice(fypp->cfg.diag, "operator stack\n");
	fy_expr_stack_dump(fypp->cfg.diag, &fypp->operators);
	fy_notice(fypp->cfg.diag, "operand stack\n");
	fy_expr_stack_dump(fypp->cfg.diag, &fypp->operands);
#endif

	fy_path_expr_free(expr);
	fy_path_expr_free(exprl);
	fy_path_expr_free(exprr);
	fy_path_expr_free(chain);
	fy_path_expr_free(parent);

	return -1;
}

struct fy_path_expr *
fy_path_parse_expression(struct fy_path_parser *fypp)
{
	struct fy_reader *fyr;
	struct fy_token *fyt = NULL;
	enum fy_token_type fytt;
	struct fy_path_expr *expr, *expr_top, *exprt;
	// enum fy_path_parser_scan_mode old_scan_mode;
	int ret;

	/* the parser must be in the correct state */
	if (!fypp || fy_expr_stack_size(&fypp->operators) > 0 || fy_expr_stack_size(&fypp->operands) > 0)
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

#ifdef DEBUG_EXPR
		FYR_TOKEN_DIAG(fyr, fyt, FYET_NOTICE, FYEM_PARSE, "next token %s", fy_token_debug_text_a(fyt));
#endif
		fytt = fyt->type;

		/* create an expression in either operator/operand case */
		expr = fy_path_expr_alloc_recycle(fypp);
		fyr_error_check(fyr, expr, err_out,
				"fy_path_expr_alloc_recycle() failed\n");

		expr->fyt = fy_path_scan_remove(fypp, fyt);
		/* this it the first attempt, it might not be the final one */
		expr->type = fy_map_token_to_path_expr_type(fyt->type);
		fyt = NULL;

#ifdef DEBUG_EXPR
		fy_path_expr_dump(expr, fypp->cfg.diag, FYET_NOTICE, 0, "-> expr");
#endif

#if 0

		old_scan_mode = fypp->scan_mode;
		switch (fypp->scan_mode) {
		case fyppsm_none:
			assert(0);	/* should never happen */
			break;

		case fyppsm_path_expr:
			switch (fytt) {
			case FYTT_PE_EQEQ:
			case FYTT_PE_NOTEQ:
			case FYTT_PE_LT:
			case FYTT_PE_GT:
			case FYTT_PE_LTE:
			case FYTT_PE_GTE:
				fypp->scan_mode = fyppsm_scalar_expr;
				break;
			default:
				break;
			}
			break;
		case fyppsm_scalar_expr:
			switch (fytt) {
			case FYTT_PE_LPAREN:
				fypp->scalar_expr_nest_level++;
				break;
			case FYTT_PE_RPAREN:
				FYR_TOKEN_ERROR_CHECK(fyr, fyt, FYEM_PARSE,
						fypp->scalar_expr_nest_level > 0, err_out,
						"unbalanced parenthesis in scalar expr mode");
				fypp->scalar_expr_nest_level--;
				if (fypp->scalar_expr_nest_level == 0) {
#ifdef DEBUG_EXPR
					fyr_notice(fyr, "going back into path expr mode\n");
#endif
					fypp->scan_mode = fyppsm_path_expr;
				}
				break;
			case FYTT_SCALAR:
				if (fypp->scalar_expr_nest_level == 0) {
#ifdef DEBUG_EXPR
					fyr_notice(fyr, "going back into path expr mode\n");
#endif
					fypp->scan_mode = fyppsm_path_expr;
				}
				break;
			default:
				break;
			}
			break;
		}

		if (old_scan_mode != fypp->scan_mode) {
#ifdef DEBUG_EXPR
			fyr_notice(fyr, "scan_mode %s -> %s\n",
					path_parser_scan_mode_txt[old_scan_mode],
					path_parser_scan_mode_txt[fypp->scan_mode]);
#endif
			if (fyt && fyt->type == FYTT_PE_RPAREN) {
				fyt = fy_path_scan_remove(fypp, fyt);
				ret = push_operator(fypp, fyt, NULL);
				if (ret)
					goto err_out;

				fyt = NULL;
				ret = evaluate(fypp);
				if (ret)
					goto err_out;
			}

			/* evaluate */
			for (;;) {
				/* get the top of the operator stack */
				fyt_top = peek_operator(fypp, NULL);
				if (!fyt_top)
					break;

#ifdef DEBUG_EXPR
				fy_notice(fypp->cfg.diag, "> fyt_top: %.*s (%2d)\n", 20, fy_token_debug_text_a(fyt_top),
					fy_token_type_operator_prec(fyt_top->type));

				fyr_notice(fyr, "fy_token_type_scan_mode(fyt_top->type)=%s, old_scan_mode=%s\n", 
						path_parser_scan_mode_txt[fy_token_type_scan_mode(fyt_top->type)],
						path_parser_scan_mode_txt[old_scan_mode]);
#endif

				ret = evaluate(fypp);
				/* evaluate will print diagnostic on error */
				if (ret)
					goto err_out;

				break;
			}

#ifdef DEBUG_EXPR
			fy_notice(fypp->cfg.diag, "operator stack\n");
			dump_operator_stack(fypp);
			fy_notice(fypp->cfg.diag, "operand stack\n");
			dump_operand_stack(fypp);
			fyr_notice(fyr, "> done with scan mode change\n");
#endif
		}

		/* if was an operand and already consumed */
		if (!fyt)
			continue;
#else

		/* if it's an operand convert it to expression and push */
		if (fy_token_type_is_operand(fytt)) {

			ret = fy_expr_stack_push(&fypp->operands, expr);
			fyr_error_check(fyr, !ret, err_out, "push_operand() failed\n");
			expr = NULL;

			fy_notice(fypp->cfg.diag, "> pushed as operand\n");
			continue;
		}

#endif
		/* specials for SLASH */

		if (expr->fyt->type == FYTT_PE_SLASH) {

			/* try to get next token */
			fyt = fy_path_scan_peek(fypp, NULL);
			if (!fyt) {
				(void)fy_path_fetch_tokens(fypp);
				fyt = fy_path_scan_peek(fypp, NULL);
			}

			/* last token, it means it's a collection filter (or a root) */
			if (!fyt || fyt->type == FYTT_STREAM_END || fyt->type == FYTT_PE_RPAREN) {
			
				exprt = fy_expr_stack_peek(&fypp->operands);

				/* if no argument exists it's a root */
				if (!exprt) {
					expr->type = fpet_root;

					ret = fy_expr_stack_push(&fypp->operands, expr);
					fyr_error_check(fyr, !ret, err_out, "push_operand() failed\n");
					expr = NULL;
					continue;
				}

				expr->type = fpet_filter_collection;
			}
		}

		fy_notice(fypp->cfg.diag, "operator stack (before)\n");
		fy_expr_stack_dump(fypp->cfg.diag, &fypp->operators);
		fy_notice(fypp->cfg.diag, "operand stack (before)\n");
		fy_expr_stack_dump(fypp->cfg.diag, &fypp->operands);

		/* for rparen, need to push before */
		if (expr->type == fpet_rparen) {

			ret = fy_expr_stack_push(&fypp->operators, expr);
			fyr_error_check(fyr, !ret, err_out, "push_operator() failed\n");
			expr = NULL;

			ret = evaluate_new(fypp);
			/* evaluate will print diagnostic on error */
			if (ret < 0) {
				fy_notice(fypp->cfg.diag, "> evaluate (prec) error\n");
				goto err_out;
			}

		} else if (expr->type == fpet_lparen) {

			/* push the operator */
			ret = fy_expr_stack_push(&fypp->operators, expr);
			fyr_error_check(fyr, !ret, err_out, "push_operator() failed\n");
			expr = NULL;

		} else {

			ret = -1;
			while ((expr_top = fy_expr_stack_peek(&fypp->operators)) != NULL &&
				fy_path_expr_type_prec(expr->type) >= fy_path_expr_type_prec(expr_top->type)) {

				fy_notice(fypp->cfg.diag, "> eval (prec)\n");

				ret = evaluate_new(fypp);
				/* evaluate will print diagnostic on error */
				if (ret < 0) {
					fy_notice(fypp->cfg.diag, "> evaluate (prec) error\n");
					goto err_out;
				}
			}

			/* push the operator */
			ret = fy_expr_stack_push(&fypp->operators, expr);
			fyr_error_check(fyr, !ret, err_out, "push_operator() failed\n");
			expr = NULL;
		}

#if 0
		/* rparen forces an eval immediately */

		if (was_rparen) {
			fy_notice(fypp->cfg.diag, "> eval (rparen)\n");

			ret = evaluate_new(fypp);
			/* evaluate will print diagnostic on error */
			if (ret < 0) {
				fy_notice(fypp->cfg.diag, "> evaluate (rparen) error\n");
				goto err_out;
			}
		}
#endif

		fy_notice(fypp->cfg.diag, "operator stack (after)\n");
		fy_expr_stack_dump(fypp->cfg.diag, &fypp->operators);
		fy_notice(fypp->cfg.diag, "operand stack (after)\n");
		fy_expr_stack_dump(fypp->cfg.diag, &fypp->operands);

	}

	if (fypp->stream_error) {
		fy_notice(fypp->cfg.diag, "> stream error\n");
		goto err_out;
	}

	FYR_PARSE_ERROR_CHECK(fyr, 0, 1, FYEM_PARSE,
			fypp->stream_error || (fyt && fyt->type == FYTT_STREAM_END), err_out,
			"stream ended without STREAM_END");

	/* remove stream end */
	fy_token_unref(fy_path_scan_remove(fypp, fyt));
	fyt = NULL;

#if 0
	FYR_PARSE_ERROR_CHECK(fyr, 0, 1, FYEM_PARSE,
			fypp->paren_nest_level == 0, err_out,
			"Missing right parenthesis");
#endif

	/* drain */
	while ((expr_top = fy_expr_stack_peek(&fypp->operators)) != NULL) {

		// was_lparen = expr_top->type == fpet_lparen;

		ret = evaluate_new(fypp);
		/* evaluate will print diagnostic on error */
		if (ret < 0) {
			fy_notice(fypp->cfg.diag, "> evaluate (rem) error\n");
			goto err_out;
		}

	}

	expr = fy_expr_stack_pop(&fypp->operands);

	FYR_PARSE_ERROR_CHECK(fyr, 0, 1, FYEM_PARSE,
			expr != NULL, err_out,
			"No operands left on operand stack");

	FYR_TOKEN_ERROR_CHECK(fyr, expr->fyt, FYEM_PARSE,
			fy_expr_stack_size(&fypp->operands) == 0, err_out,
			"Operand stack contains more than 1 value at end");

	fy_notice(fypp->cfg.diag, "> return expr\n");

	return expr;

err_out:
	// fy_notice(fypp->cfg.diag, "operator stack\n");
	// dump_operator_stack(fypp);
	// fy_notice(fypp->cfg.diag, "operand stack\n");
	// dump_operand_stack(fypp);
	fy_notice(fypp->cfg.diag, "> error expr\n");
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

static double
token_number(struct fy_token *fyt)
{
	const char *value;

	if (!fyt || fyt->type != FYTT_SCALAR || (value = fy_token_get_text0(fyt)) == NULL)
		return NAN;
	return strtod(value, NULL);
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
	fy_walk_result_free(fypx->result);
	fypx->fyn_start = NULL;
	fypx->result = NULL;
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

	fy_path_parser_close(fypp);

	fy_input_unref(fyi);

	return expr;

err_out:
	fy_path_expr_free(expr);
	fy_path_parser_close(fypp);
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

void fy_walk_result_flatten(struct fy_walk_result *fwr, struct fy_walk_result *fwrf)
{
	struct fy_walk_result *fwr2, *fwr2n;

	if (!fwr || !fwrf || fwr->type != fwrt_refs)
		return;

	for (fwr2 = fy_walk_result_list_head(&fwr->refs); fwr2; fwr2 = fwr2n) {

		fwr2n = fy_walk_result_next(&fwr->refs, fwr2);

		if (fwr2->type != fwrt_refs) {
			fy_walk_result_list_del(&fwr->refs, fwr2);
			fy_walk_result_list_add_tail(&fwrf->refs, fwr2);
			continue;
		}
		fy_walk_result_flatten(fwr2, fwrf);
	}
}

struct fy_walk_result *fy_walk_result_simplify(struct fy_walk_result *fwr)
{
	struct fy_walk_result *fwr2, *fwrf;
	bool recursive;

	/* no fwr */
	if (!fwr)
		return NULL;

	/* non recursive */
	if (fwr->type != fwrt_refs)
		return fwr;

	/* refs, if empty, return NULL */
	if (fy_walk_result_list_empty(&fwr->refs)) {
		fy_walk_result_free(fwr);
		return NULL;
	}

	/* single element, switch it out */
	if (fy_walk_result_list_is_singular(&fwr->refs)) {
		fwr2 = fy_walk_result_list_pop(&fwr->refs);
		assert(fwr2);

		fy_walk_result_free(fwr);
		fwr = fwr2;
	}

	/* non recursive return immediately */
	if (fwr->type != fwrt_refs)
		return fwr;

	/* flatten if recursive */
	recursive = false;
	for (fwr2 = fy_walk_result_list_head(&fwr->refs); fwr2;
		fwr2 = fy_walk_result_next(&fwr->refs, fwr2)) {

		/* refs, recursive */
		if (fwr2->type == fwrt_refs) {
			recursive = true;
			break;
		}
	}

	if (!recursive)
		return fwr;

	fwrf = fy_walk_result_alloc();
	assert(fwrf);
	fwrf->type = fwrt_refs;
	fy_walk_result_list_init(&fwrf->refs);

	fy_walk_result_flatten(fwr, fwrf);

	fy_walk_result_free(fwr);

	return fwrf;
}

int fy_walk_result_all_children_recursive_internal(struct fy_node *fyn, struct fy_walk_result *output)
{
	struct fy_node *fyni;
	struct fy_node_pair *fynp;
	struct fy_walk_result *fwr;
	int ret;

	if (!fyn)
		return 0;

	assert(output);
	assert(output->type == fwrt_refs);

	/* this node */
	fwr = fy_walk_result_alloc();
	if (!fwr)
		return -1;
	fwr->type = fwrt_node_ref;
	fwr->fyn = fyn;
	fy_walk_result_list_add_tail(&output->refs, fwr);

	if (fy_node_is_sequence(fyn)) {

		for (fyni = fy_node_list_head(&fyn->sequence); fyni;
				fyni = fy_node_next(&fyn->sequence, fyni)) {

			ret = fy_walk_result_all_children_recursive_internal(fyni, output);
			if (ret)
				return ret;
		}

	} else if (fy_node_is_mapping(fyn)) {

		for (fynp = fy_node_pair_list_head(&fyn->mapping); fynp;
				fynp = fy_node_pair_next(&fyn->mapping, fynp)) {

			ret = fy_walk_result_all_children_recursive_internal(fynp->value, output);
			if (ret)
				return ret;
		}
	}
	return 0;
}

struct fy_walk_result *fy_walk_result_all_children_recursive(struct fy_node *fyn)
{
	struct fy_walk_result *output;
	int ret;

	if (!fyn)
		return NULL;

	output = fy_walk_result_alloc();
	assert(output);
	output->type = fwrt_refs;
	fy_walk_result_list_init(&output->refs);

	ret = fy_walk_result_all_children_recursive_internal(fyn, output);
	if (ret) {
		fy_walk_result_free(output);
		return NULL;
	}
	return output;
}

bool
fy_walk_result_compare_simple(struct fy_diag *diag, enum fy_path_expr_type type,
			      struct fy_walk_result *fwrl, struct fy_walk_result *fwrr)
{
	struct fy_token *fyt;
	struct fy_walk_result *fwrt;
	const char *str;
	bool match;

	/* both NULL */
	if (!fwrl && !fwrr) {
		switch (type) {
		case fpet_eq:
			return true;
		default:
			break;
		}
		return false;
	}

	/* any NULL */
	if (!fwrl || !fwrr) {
		switch (type) {
		case fpet_neq:
			return true;
		default:
			break;
		}
		return false;
	}

	/* both are non NULL */

	/* none should be multiple */
	assert(fwrl->type != fwrt_refs && fwrr->type != fwrt_refs);

	/* both are the same type */
	if (fwrl->type == fwrr->type) {

		switch (fwrl->type) {
		case fwrt_node_ref:
			switch (type) {
			case fpet_eq:
				return fwrl->fyn == fwrr->fyn;
			case fpet_neq:
				return fwrl->fyn != fwrr->fyn;
			default:
				break;
			}
			break;

		case fwrt_refs:
			assert(0);	/* should not get here */
			break;

		case fwrt_doc:
			switch (type) {
			case fpet_eq:
			case fpet_neq:
				match = false;
				if (fwrl->fyd == fwrr->fyd)
					match = true;
				else if (!fwrl->fyd || !fwrr->fyd)
					match = false;
				else
					match = fy_node_compare(fwrl->fyd->root, fwrr->fyd->root);
				if (type == fpet_neq)
					match = !match;
				return match;
			default:
				break;
			}
			break;

		case fwrt_number:
			switch (type) {
			case fpet_eq:
				return fwrl->number == fwrr->number;
			case fpet_neq:
				return fwrl->number != fwrr->number;
			case fpet_lt:
				return fwrl->number < fwrr->number;
			case fpet_gt:
				return fwrl->number > fwrr->number;
			case fpet_lte:
				return fwrl->number <= fwrr->number;
			case fpet_gte:
				return fwrl->number >= fwrr->number;
			default:
				break;
			}
			break;

		case fwrt_string:
			switch (type) {
			case fpet_eq:
				return strcmp(fwrl->string, fwrr->string) == 0;
			case fpet_neq:
				return strcmp(fwrl->string, fwrr->string) != 0;
			case fpet_lt:
				return strcmp(fwrl->string, fwrr->string) < 0;
			case fpet_gt:
				return strcmp(fwrl->string, fwrr->string) > 0;
			case fpet_lte:
				return strcmp(fwrl->string, fwrr->string) <= 0;
			case fpet_gte:
				return strcmp(fwrl->string, fwrr->string) >= 0;
			default:
				break;
			}
			break;
		}
		return false;
	}

	/* only handle the node refs at the left */
	if (fwrr->type == fwrt_node_ref) {
		switch (type) {
		case fpet_lt:
			type = fpet_gte;
			break;
		case fpet_gt:
			type = fpet_lte;
			break;
		case fpet_lte:
			type = fpet_gt;
			break;
		case fpet_gte:
			type = fpet_lt;
			break;
		default:
			break;
		}

		/* swap left with right */
		return fy_walk_result_compare_simple(diag, type, fwrr, fwrl);
	}

	switch (fwrl->type) {
	case fwrt_node_ref:

		/* non scalar mode, only returns true for non-eq */
		if (!fy_node_is_scalar(fwrl->fyn)) {
			/* XXX case of rhs being a document not handled */
			return type == fpet_neq;
		}

		fyt = fy_node_get_scalar_token(fwrl->fyn);
		assert(fyt);

		str = fy_token_get_text0(fyt);
		assert(str);

		fwrt = NULL;
		/* node ref against */
		switch (fwrr->type) {
		case fwrt_string:
			/* create a new temporary walk result */
			fwrt = fy_walk_result_alloc();
			assert(fwrt);

			fwrt->type = fwrt_string;
			fwrt->string = strdup(str);
			assert(fwrt->string);
			break;

		case fwrt_number:
			/* if it's not a number return true only for non-eq */
			if (!fy_token_is_number(fyt))
				return type == fpet_neq;

			/* create a new temporary walk result */
			fwrt = fy_walk_result_alloc();
			assert(fwrt);

			fwrt->type = fwrt_number;
			fwrt->number = strtod(str, NULL);
			break;

		default:
			break;
		}

		if (!fwrt)
			return false;

		match = fy_walk_result_compare_simple(diag, type, fwrt, fwrr);

		/* free the temporary result */
		fy_walk_result_free(fwrt);

		return match;

	default:
		break;
	}

	return false;
}

struct fy_walk_result *
fy_walk_result_arithmetic_simple(struct fy_diag *diag, enum fy_path_expr_type type,
				 struct fy_walk_result *fwrl, struct fy_walk_result *fwrr)
{
	struct fy_walk_result *output = NULL;
	char *str;
	size_t len, len1, len2;

	if (!fwrl || !fwrr)
		goto out;

	/* node refs are not handled yet */
	if (fwrl->type == fwrt_node_ref || fwrr->type == fwrt_node_ref)
		goto out;

	/* same type */
	if (fwrl->type == fwrr->type) {

		switch (fwrl->type) {

		case fwrt_string:
			/* for strings, only concatenation */
			if (type != fpet_plus)
				break;
			len1 = strlen(fwrl->string);
			len2 = strlen(fwrr->string);
			len = len1 + len2;
			str = malloc(len + 1);
			assert(str);
			memcpy(str, fwrl->string, len1);
			memcpy(str + len1, fwrr->string, len2);
			str[len] = '\0';

			free(fwrl->string);
			fwrl->string = str;

			/* reuse */
			output = fwrl;
			fwrl = NULL;

			break;

		case fwrt_number:
			/* reuse fwrl */
			output = fwrl;
			switch (type) {
			case fpet_plus:
				output->number = fwrl->number + fwrr->number;
				break;
			case fpet_minus:
				output->number = fwrl->number - fwrr->number;
				break;
			case fpet_mult:
				output->number = fwrl->number * fwrr->number;
				break;
			case fpet_div:
				output->number = fwrr->number ? (fwrl->number / fwrr->number) : INFINITY;
				break;
			default:
				assert(0);
				break;
			}
			fwrl = NULL;
			break;

		default:
			fy_error(diag, "fwrl->type=%s\n", fy_walk_result_type_txt[fwrl->type]);
			assert(0);
			break;
		}
	}

out:
	fy_walk_result_free(fwrl);
	fy_walk_result_free(fwrr);
	return output;
}

struct fy_walk_result *
fy_walk_result_lhs_rhs(struct fy_diag *diag, enum fy_path_expr_type type,
		      struct fy_walk_result *fwrl, struct fy_walk_result *fwrr)
{
	struct fy_walk_result *output = NULL, *fwr, *fwrrt;
	bool match;

	/* both NULL */
	if (!fwrl && !fwrr)
		return NULL;

	/* any NULL */
	if (!fwrl || !fwrr) {
		if (type == fpet_neq) {
			output = fwrl;
			fwrl = NULL;
		}
		goto out;
	}

	/* both are non NULL and simple */
	if (fwrl->type != fwrt_refs && fwrr->type != fwrt_refs) {

		if (fy_path_expr_type_is_conditional(type)) {

			match = fy_walk_result_compare_simple(diag, type, fwrl, fwrr);

			if (!match)
				goto out;

			output = fwrl;
			fwrl = NULL;
			goto out;
		}

		if (fy_path_expr_type_is_arithmetic(type))
			return fy_walk_result_arithmetic_simple(diag, type, fwrl, fwrr);

		fy_error(diag, "%s: Not handled, returning NULL\n", __func__);
		goto out;
	}

	if (fwrr->type == fwrt_refs) {

		fy_error(diag, "%s: Not handling RHS refs, returning NULL\n", __func__);
		goto out;

	}

	output = fy_walk_result_alloc();
	assert(output);
	output->type = fwrt_refs;
	fy_walk_result_list_init(&output->refs);

	while ((fwr = fy_walk_result_list_pop(&fwrl->refs)) != NULL) {

		fwrrt = fy_walk_result_clone(fwrr);
		assert(fwrrt);

		fwr = fy_walk_result_lhs_rhs(diag, type, fwr, fwrrt);
		if (fwr)
			fy_walk_result_list_add_tail(&output->refs, fwr);
	}

out:
	fy_walk_result_free(fwrl);
	fy_walk_result_free(fwrr);

	return fy_walk_result_simplify(output);
}

struct fy_walk_result *
fy_path_expr_execute(struct fy_diag *diag, int level, struct fy_path_expr *expr, struct fy_walk_result *input)
{
	struct fy_walk_result *fwr, *fwrn, *fwrt, *fwrtn;
	struct fy_walk_result *output = NULL, *input1, *output1, *input2, *output2;
	struct fy_path_expr *exprn, *exprl, *exprr;
	struct fy_node *fyn, *fynn, *fyni;
	struct fy_node_pair *fynp;
	struct fy_token *fyt;
	int rc, start, end, count, i;
	bool match;

	/* error */
	if (!expr || !input)
		goto out;

#ifdef DEBUG_EXPR
	fy_walk_result_dump(input, diag, FYET_NOTICE, level, "input %s\n", fy_path_expr_type_txt[expr->type]);
#endif

	/* recursive */
	if (input->type == fwrt_refs && !fy_path_expr_type_handles_refs(expr->type)) {

		output = fy_walk_result_alloc();
		assert(output);
		output->type = fwrt_refs;
		fy_walk_result_list_init(&output->refs);

		while ((fwr = fy_walk_result_list_pop(&input->refs)) != NULL) {

			fwrn = fy_path_expr_execute(diag, level + 1, expr, fwr);
			if (fwrn)
				fy_walk_result_list_add_tail(&output->refs, fwrn);
		}
		fy_walk_result_free(input);
		input = NULL;
		goto out;
	}


	/* single result case is common enough to optimize */
	if (fy_path_expr_type_is_single_result(expr->type) && input->type == fwrt_node_ref) {

		fynn = fy_path_expr_execute_single_result(diag, expr, input->fyn);
		if (!fynn)
			goto out;

		fy_walk_result_clean(input);
		output = input;
		output->type = fwrt_node_ref;
		output->fyn = fynn;
		input = NULL;
		goto out;
	}

	/* handle the remaining multi result cases */
	switch (expr->type) {

	case fpet_chain:

		/* iterate over each chain item */
		output = input;
		input = NULL;
		for (exprn = fy_path_expr_list_head(&expr->children); exprn;
			exprn = fy_path_expr_next(&expr->children, exprn)) {

			output = fy_path_expr_execute(diag, level + 1, exprn, output);
			if (!output)
				break;
		}

		break;

	case fpet_multi:

		/* allocate a refs output result */
		output = fy_walk_result_alloc();
		assert(output);
		output->type = fwrt_refs;
		fy_walk_result_list_init(&output->refs);

		/* iterate over each multi item */
		for (exprn = fy_path_expr_list_head(&expr->children); exprn;
			exprn = fy_path_expr_next(&expr->children, exprn)) {

			input2 = fy_walk_result_clone(input);
			assert(input2);

			output2 = fy_path_expr_execute(diag, level + 1, exprn, input2);
			if (!output2)
				continue;

			fy_walk_result_list_add_tail(&output->refs, output2);
		}
		fy_walk_result_free(input);
		input = NULL;
		break;

	case fpet_every_child:

		/* only valid for node ref */
		if (input->type != fwrt_node_ref)
			break;

		fyn = input->fyn;

		/* every scalar/alias is a single result (although it should not happen) */
		if (fy_node_is_scalar(fyn) || fy_node_is_alias(fyn)) {
			output = input;
			input = NULL;
			break;

		}

		/* re-use input for output root */
		fy_walk_result_clean(input);
		output = input;
		input = NULL;

		output->type = fwrt_refs;
		fy_walk_result_list_init(&output->refs);

		if (fy_node_is_sequence(fyn)) {

			for (fyni = fy_node_list_head(&fyn->sequence); fyni;
				fyni = fy_node_next(&fyn->sequence, fyni)) {

				fwr = fy_walk_result_alloc();
				assert(fwr);
				fwr->type = fwrt_node_ref;
				fwr->fyn = fyni;

				fy_walk_result_list_add_tail(&output->refs, fwr);
			}

			break;
		}

		if (fy_node_is_mapping(fyn)) {

			for (fynp = fy_node_pair_list_head(&fyn->mapping); fynp;
					fynp = fy_node_pair_next(&fyn->mapping, fynp)) {

				fwr = fy_walk_result_alloc();
				assert(fwr);
				fwr->type = fwrt_node_ref;
				fwr->fyn = fynp->value;

				fy_walk_result_list_add_tail(&output->refs, fwr);
			}
		}

		break;

	case fpet_every_child_r:

		/* only valid for node ref */
		if (input->type != fwrt_node_ref)
			break;

		fyn = input->fyn;

		/* re-use input for output root */
		fy_walk_result_clean(input);
		output = input;
		input = NULL;

		output->type = fwrt_refs;
		fy_walk_result_list_init(&output->refs);

		rc = fy_walk_result_all_children_recursive_internal(fyn, output);
		assert(!rc);

		break;

	case fpet_seq_slice:

		/* only valid for node ref on a sequence */
		if (input->type != fwrt_node_ref || !fy_node_is_sequence(input->fyn)) {
			break;
		}
		fyn = input->fyn;

		fyt = expr->fyt;
		assert(fyt);
		assert(fyt->type == FYTT_PE_SEQ_SLICE);

		start = fyt->seq_slice.start_index;
		end = fyt->seq_slice.end_index;
		count = fy_node_sequence_item_count(fyn);

		/* don't handle negative slices yet */
		if (start < 0 || end < 1 || start >= end)
			break;

		if (count < end)
			end = count;

		/* re-use input for output root */
		fy_walk_result_clean(input);
		output = input;
		input = NULL;

		output->type = fwrt_refs;
		fy_walk_result_list_init(&output->refs);

		for (i = start; i < end; i++) {

			fynn = fy_node_sequence_get_by_index(fyn, i);
			if (!fynn)
				continue;

			fwr = fy_walk_result_alloc();
			assert(fwr);
			fwr->type = fwrt_node_ref;
			fwr->fyn = fynn;

			fy_walk_result_list_add_tail(&output->refs, fwr);
		}

		break;

	case fpet_eq:
	case fpet_neq:
	case fpet_lt:
	case fpet_gt:
	case fpet_lte:
	case fpet_gte:
	case fpet_plus:
	case fpet_minus:
	case fpet_mult:
	case fpet_div:

		exprl = fy_path_expr_lhs(expr);
		assert(exprl);

		exprr = fy_path_expr_rhs(expr);
		assert(exprr);

		input1 = fy_walk_result_clone(input);
		assert(input1);

		input2 = input;
		input = NULL;

		/* execute LHS and RHS */
		output1 = fy_path_expr_execute(diag, level + 1, exprl, input1);
		output2 = fy_path_expr_execute(diag, level + 1, exprr, input2);

		output = fy_walk_result_lhs_rhs(diag, expr->type, output1, output2);

		break;

	case fpet_scalar:
		/* reuse the input */
		fy_walk_result_clean(input);
		output = input;
		input = NULL;

		/* duck typing! */
		if (fy_token_is_number(expr->fyt)) {
			output->type = fwrt_number;
			output->number = token_number(expr->fyt);
		} else {
			output->type = fwrt_string;
			output->string = strdup(fy_token_get_text0(expr->fyt));
			assert(output->string);
		}

		break;

	case fpet_logical_or:

		/* return the first that is not NULL */
		for (exprn = fy_path_expr_list_head(&expr->children); exprn;
			exprn = fy_path_expr_next(&expr->children, exprn)) {

			input1 = fy_walk_result_clone(input);
			assert(input1);

			output = fy_path_expr_execute(diag, level + 1, exprn, input1);
			if (output)
				break;
		}
		break;

	case fpet_logical_and:
		output = NULL;

		/* return the last that was not NULL */
		for (exprn = fy_path_expr_list_head(&expr->children); exprn;
			exprn = fy_path_expr_next(&expr->children, exprn)) {

			input1 = fy_walk_result_clone(input);
			assert(input1);

			output1 = fy_path_expr_execute(diag, level + 1, exprn, input1);
			if (output1) {
				fy_walk_result_free(output);
				output = output1;
			} else
				break;
		}
		break;

	case fpet_filter_unique:

		/* for non refs, return input */
		if (input->type != fwrt_refs) {
			output = input;
			input = NULL;
			break;
		}

		/* remove duplicates filter */
		for (fwr = fy_walk_result_list_head(&input->refs); fwr;
				fwr = fy_walk_result_next(&input->refs, fwr)) {

			/* do not check recursively */
			if (fwr->type == fwrt_refs)
				continue;

			/* check the entries from this point forward */
			for (fwrt = fy_walk_result_next(&input->refs, fwr); fwrt; fwrt = fwrtn) {

				fwrtn = fy_walk_result_next(&input->refs, fwrt);

				/* do not check recursively (or the same result) */
				if (fwrt->type == fwrt_refs)
					continue;

				assert(fwrt != fwr);

				match = fy_walk_result_compare_simple(diag, fpet_eq, fwr, fwrt);

				if (match) {
					fy_walk_result_list_del(&input->refs, fwrt);
					fy_walk_result_free(fwrt);
				}
			}
		}
		output = input;
		input = NULL;

		break;

	case fpet_expr:
		return fy_path_expr_execute(diag, level + 1, fy_path_expr_list_head(&expr->children), input);

	default:
		fy_error(diag, "%s\n", fy_path_expr_type_txt[expr->type]);
		assert(0);
		break;
	}

out:
	fy_walk_result_free(input);
	output = fy_walk_result_simplify(output);

#ifdef DEBUG_EXPR
	if (output)
		fy_walk_result_dump(output, diag, FYET_NOTICE, level, "output %s\n", fy_path_expr_type_txt[expr->type]);
#endif
	return output;
}

static int fy_path_exec_execute_internal(struct fy_path_exec *fypx,
		struct fy_path_expr *expr, struct fy_node *fyn_start)
{
	struct fy_walk_result *fwr;

	if (!fypx || !expr || !fyn_start)
		return -1;

	fy_walk_result_free(fypx->result);
	fypx->result = NULL;

	fwr = fy_walk_result_alloc();
	assert(fwr);
	fwr->type = fwrt_node_ref;
	fwr->fyn = fyn_start;

	fypx->result = fy_path_expr_execute(fypx->cfg.diag, 0, expr, fwr);
	/* XXX error handling */
	return 0;
}

int fy_path_exec_execute(struct fy_path_exec *fypx, struct fy_path_expr *expr, struct fy_node *fyn_start)
{
	if (!fypx || !expr || !fyn_start)
		return -1;

	fypx->fyn_start = fyn_start;
	return fy_path_exec_execute_internal(fypx, expr, fypx->fyn_start);
}

struct fy_node *
fy_path_exec_results_iterate(struct fy_path_exec *fypx, void **prevp)
{
	struct fy_walk_result *fwr;

	if (!fypx || !prevp)
		return NULL;

	if (!fypx->result)
		return NULL;

	if (fypx->result->type != fwrt_refs) {
		fwr = fypx->result;

		if (fwr->type != fwrt_node_ref)
			return NULL;

		if (!*prevp) {
			*prevp = fwr;
			return fwr->fyn;
		}
		*prevp = NULL;
		return NULL;
	}

	/* loop over non node refs for now */
	do {
		if (!*prevp)
			fwr = fy_walk_result_list_head(&fypx->result->refs);
		else
			fwr = fy_walk_result_next(&fypx->result->refs, *prevp);
		*prevp = fwr;
	} while (fwr && fwr->type != fwrt_node_ref);

	return fwr ? fwr->fyn : NULL;
}
