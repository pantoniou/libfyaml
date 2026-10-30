/*
 * cparse-comments.c - attach comments to declarations
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Comments never enter the token stream, they are kept in tu->comments. After
 * parsing, each declaration gets the raw text of the comment that documents it:
 *
 *  - a comment that follows the declaration on the same line (only blanks,
 *    ';' and ',' between them) wins;
 *  - otherwise the comment block ending right before the declaration, with
 *    nothing but white space between. Consecutive '//' lines form one block.
 *
 * Only fields and enumerators take trailing comments, like libclang. (libclang
 * also hands a comment trailing a struct's closing brace to the last field; that
 * is not reproduced.)
 */
#include <stdlib.h>
#include <string.h>

#include "cparse-private.h"

static int cmp_comment(const void *a, const void *b)
{
	const struct cp_comment *ca = *(const struct cp_comment * const *)a;
	const struct cp_comment *cb = *(const struct cp_comment * const *)b;

	if (ca->file != cb->file)
		return ca->file < cb->file ? -1 : 1;
	if (ca->start != cb->start)
		return ca->start < cb->start ? -1 : 1;
	return 0;
}

/* first comment at or after (file, off) */
static size_t lower_bound(struct cp_comment **idx, size_t n, struct cp_file *file, size_t off)
{
	size_t lo = 0, hi = n;
	size_t mid;
	const struct cp_comment *c;

	while (lo < hi) {
		mid = lo + (hi - lo) / 2;
		c = idx[mid];

		if (c->file < file || (c->file == file && c->start < off))
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

static bool only_blank(const struct cp_file *f, size_t from, size_t to, bool newlines, bool punct)
{
	char c;

	for (; from < to; from++) {
		c = f->contents[from];

		if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v')
			continue;
		if (c == '\n' && newlines)
			continue;
		if (punct && (c == ';' || c == ','))
			continue;
		return false;
	}
	return true;
}

static const char *join(struct cp_tu *tu, struct cp_comment **idx, size_t first, size_t last)
{
	size_t i, len = 0;
	char *s, *p;
	size_t n;

	for (i = first; i <= last; i++)
		len += idx[i]->end - idx[i]->start + 1;
	s = cp_alloc(tu, len + 1);
	if (!s)
		return NULL;
	p = s;
	for (i = first; i <= last; i++) {
		n = idx[i]->end - idx[i]->start;

		if (i > first)
			*p++ = '\n';
		memcpy(p, idx[i]->file->contents + idx[i]->start, n);
		p += n;
	}
	*p = '\0';
	return s;
}

int cp_attach_comments(struct cp_tu *tu)
{
	struct cp_comment **idx;
	size_t i, n = tu->num_comments;
	struct cp_decl *d;
	struct cp_comment *c;
	size_t k;
	size_t first;
	struct cp_comment *p;

	if (!n)
		return 0;
	idx = cp_alloc(tu, n * sizeof(*idx));
	if (!idx)
		return -1;
	for (i = 0; i < n; i++)
		idx[i] = &tu->comments[i];
	qsort(idx, n, sizeof(*idx), cmp_comment);

	for (i = 0; i < tu->num_decls; i++) {
		d = tu->decls[i];

		if (!d->file || d->comment)
			continue;

		/* trailing */
		if (d->kind == CPDK_FIELD || d->kind == CPDK_ENUMERATOR) {
			k = lower_bound(idx, n, d->file, d->end);
			if (k < n) {
				c = idx[k];
				if (c->file == d->file && c->after_code &&
				    only_blank(d->file, d->end, c->start, false, true)) {
					d->comment = join(tu, idx, k, k);
					continue;
				}
			}
		}

		/* leading */
		k = lower_bound(idx, n, d->file, d->start);
		if (!k)
			continue;
		k--;
		c = idx[k];
		if (c->file != d->file || c->after_code || c->end > d->start ||
		    !only_blank(d->file, c->end, d->start, true, false))
			continue;
		first = k;
		while (first > 0) {
			p = idx[first - 1];

			if (p->file != c->file || p->after_code || p->kind != CPCK_LINE ||
			    idx[first]->kind != CPCK_LINE || p->end_line + 1 != idx[first]->line)
				break;
			first--;
		}
		d->comment = join(tu, idx, first, k);
	}
	return 0;
}
