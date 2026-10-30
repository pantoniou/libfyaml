/*
 * cparse-util.c - C declaration front end: allocation, hashmap, TU, diagnostics
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Derived from slimcc, see cparse-private.h and README.md.
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cparse-private.h"

/* allocation */

void *cp_alloc(struct cp_tu *tu, size_t size)
{
	void *p;

	p = fy_allocator_alloc(tu->allocator, tu->tag, size ? size : 1,
			       _Alignof(max_align_t));
	if (!p)
		return NULL;
	memset(p, 0, size);
	return p;
}

char *cp_strndup(struct cp_tu *tu, const char *s, size_t len)
{
	char *p;

	p = cp_alloc(tu, len + 1);
	if (!p)
		return NULL;
	memcpy(p, s, len);
	p[len] = '\0';
	return p;
}

char *cp_vformat(struct cp_tu *tu, const char *fmt, va_list ap)
{
	va_list ap2;
	char *p;
	int len;

	va_copy(ap2, ap);
	len = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	if (len < 0)
		return NULL;

	p = cp_alloc(tu, (size_t)len + 1);
	if (!p)
		return NULL;
	vsnprintf(p, (size_t)len + 1, fmt, ap);
	return p;
}

char *cp_format(struct cp_tu *tu, const char *fmt, ...)
{
	va_list ap;
	char *p;

	va_start(ap, fmt);
	p = cp_vformat(tu, fmt, ap);
	va_end(ap);
	return p;
}

/* growable arrays: a new array is allocated and the old one is left to the tag */
static int cp_array_grow(struct cp_tu *tu, void **arrp, size_t elem_size,
			 size_t num, size_t *capp)
{
	size_t ncap;
	void *n;

	if (num < *capp)
		return 0;
	ncap = *capp ? *capp * 2 : 64;
	n = cp_alloc(tu, ncap * elem_size);
	if (!n)
		return -1;
	if (num)
		memcpy(n, *arrp, num * elem_size);
	*arrp = n;
	*capp = ncap;
	return 0;
}

#define CP_GROW(_tu, _arr, _num, _cap) \
	cp_array_grow((_tu), (void **)&(_arr), sizeof(*(_arr)), (_num), &(_cap))

/* hashmap, open addressing, no delete (a TU only ever grows) */

static uint64_t cp_hash(const char *key, int keylen)
{
	uint64_t h = 0xcbf29ce484222325ULL;	/* FNV-1a */
	int i;

	for (i = 0; i < keylen; i++) {
		h ^= (uint8_t)key[i];
		h *= 0x100000001b3ULL;
	}
	return h;
}

static bool cp_hashmap_match(const struct cp_hashmap_entry *ent,
			     const char *key, int keylen)
{
	return ent->key && ent->keylen == keylen && !memcmp(ent->key, key, keylen);
}

static int cp_hashmap_rehash(struct cp_tu *tu, struct cp_hashmap *map, int capacity)
{
	struct cp_hashmap_entry *nb, *ent;
	int i;
	struct cp_hashmap_entry *dst;

	nb = cp_alloc(tu, sizeof(*nb) * capacity);
	if (!nb)
		return -1;

	for (i = 0; i < map->capacity; i++) {
		ent = &map->buckets[i];
		if (!ent->key)
			continue;
		for (uint64_t h = cp_hash(ent->key, ent->keylen);; h++) {
			dst = &nb[h % capacity];

			if (!dst->key) {
				*dst = *ent;
				break;
			}
		}
	}
	map->buckets = nb;
	map->capacity = capacity;
	return 0;
}

struct cp_hashmap_entry *cp_hashmap_get_or_insert(struct cp_tu *tu,
		struct cp_hashmap *map, const char *key, int keylen)
{
	struct cp_hashmap_entry *ent;
	uint64_t h;

	if (!map->capacity) {
		if (cp_hashmap_rehash(tu, map, 64))
			return NULL;
	} else if (map->used * 100 >= map->capacity * 70) {
		if (cp_hashmap_rehash(tu, map, map->capacity * 2))
			return NULL;
	}

	for (h = cp_hash(key, keylen);; h++) {
		ent = &map->buckets[h % map->capacity];
		if (cp_hashmap_match(ent, key, keylen))
			return ent;
		if (!ent->key) {
			ent->key = cp_strndup(tu, key, keylen);
			if (!ent->key)
				return NULL;
			ent->keylen = keylen;
			map->used++;
			return ent;
		}
	}
}

void *cp_hashmap_get(struct cp_hashmap *map, const char *key, int keylen)
{
	struct cp_hashmap_entry *ent;
	uint64_t h;

	if (!map->capacity)
		return NULL;

	for (h = cp_hash(key, keylen);; h++) {
		ent = &map->buckets[h % map->capacity];
		if (cp_hashmap_match(ent, key, keylen))
			return ent->val;
		if (!ent->key)
			return NULL;
	}
}

/* translation unit */

struct cp_tu *cp_tu_create(const struct cp_options *opts)
{
	struct fy_auto_allocator_cfg acfg;
	struct cp_tu *tu;

	/* the TU handle itself is the only thing not allocated from the allocator */
	tu = calloc(1, sizeof(*tu));
	if (!tu)
		return NULL;
	if (opts)
		tu->opts = *opts;

	if (tu->opts.target) {
		tu->target_copy = *tu->opts.target;
		tu->target = &tu->target_copy;
	} else {
		tu->target = cp_target_host();
	}

	tu->allocator = tu->opts.allocator;
	if (!tu->allocator) {
		memset(&acfg, 0, sizeof(acfg));
		acfg.scenario = FYAST_PER_TAG_FREE;
		tu->allocator = fy_allocator_create("auto", &acfg);
		if (!tu->allocator)
			goto err_out;
		tu->own_allocator = true;
	}

	tu->tag = fy_allocator_get_tag(tu->allocator);
	if (tu->tag == FY_ALLOC_TAG_ERROR)
		goto err_out;

	return tu;

err_out:
	if (tu->own_allocator)
		fy_allocator_destroy(tu->allocator);
	free(tu);
	return NULL;
}

void cp_tu_destroy(struct cp_tu *tu)
{
	if (!tu)
		return;

	/* everything, arrays included, goes away with the tag */
	fy_allocator_release_tag(tu->allocator, tu->tag);
	if (tu->own_allocator)
		fy_allocator_destroy(tu->allocator);
	free(tu);
}

int cp_tu_comment_add(struct cp_tu *tu, const struct cp_comment *cmt)
{
	if (CP_GROW(tu, tu->comments, tu->num_comments, tu->cap_comments))
		return -1;
	tu->comments[tu->num_comments++] = *cmt;
	return 0;
}

int cp_decl_add(struct cp_tu *tu, struct cp_decl *decl)
{
	if (CP_GROW(tu, tu->decls, tu->num_decls, tu->cap_decls))
		return -1;
	tu->decls[tu->num_decls++] = decl;
	return 0;
}

/* locations and diagnostics */

void cp_file_location(const struct cp_file *file, const char *loc,
		      int *linep, int *columnp)
{
	const char *p, *line_start;
	int line = 1;

	line_start = file->contents;
	for (p = file->contents; p < loc; p++) {
		if (*p == '\n') {
			line++;
			line_start = p + 1;
		}
	}
	if (linep)
		*linep = line;
	if (columnp)
		*columnp = (int)(loc - line_start) + 1;
}

void cp_diag_vadd(struct cp_tu *tu, enum cp_diag_severity sev,
		  struct cp_file *file, const char *loc, const char *fmt, va_list ap)
{
	struct cp_diag *d;

	if (sev == CPDS_ERROR)
		tu->num_errors++;

	if (CP_GROW(tu, tu->diags, tu->num_diags, tu->cap_diags))
		return;

	d = &tu->diags[tu->num_diags++];
	memset(d, 0, sizeof(*d));
	d->severity = sev;
	d->file = file;
	if (file && loc)
		cp_file_location(file, loc, &d->line, &d->column);
	d->msg = cp_vformat(tu, fmt, ap);
	if (!d->msg)
		d->msg = "out of memory";
}

void cp_error_at(struct cp_tu *tu, struct cp_file *file, const char *loc,
		 const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	cp_diag_vadd(tu, CPDS_ERROR, file, loc, fmt, ap);
	va_end(ap);
}

void cp_error_tok(struct cp_tu *tu, const struct cp_token *tok,
		  const char *fmt, ...)
{
	va_list ap;

	/* a token made from text reports at the invocation it came from */
	while (!tok->file && tok->origin)
		tok = tok->origin;

	va_start(ap, fmt);
	cp_diag_vadd(tu, CPDS_ERROR, tok->file, tok->file ? tok->loc : NULL, fmt, ap);
	va_end(ap);
}

void cp_warn_tok(struct cp_tu *tu, const struct cp_token *tok,
		 const char *fmt, ...)
{
	va_list ap;

	while (!tok->file && tok->origin)
		tok = tok->origin;

	va_start(ap, fmt);
	cp_diag_vadd(tu, CPDS_WARNING, tok->file, tok->file ? tok->loc : NULL, fmt, ap);
	va_end(ap);
}

/* files */

/* Replaces \r or \r\n with \n. */
static void canonicalize_newline(char *p)
{
	char *first = strchr(p, '\r'), *q;

	if (!first)
		return;

	q = p = first;
	while (*p) {
		if (*p == '\r') {
			*q++ = '\n';
			p += (p[1] == '\n') + 1;
			continue;
		}
		*q++ = *p++;
	}
	*q = '\0';
}

/*
 * Removes backslashes followed by a newline. The removed newlines are
 * re-inserted after the next real newline so that line numbers stay right.
 */
static void remove_backslash_newline(char *p)
{
	char *first = strchr(p, '\\'), *q;
	int n = 0;

	if (!first)
		return;

	q = p = first;
	while (*p) {
		if (*p == '\\' && p[1] == '\n') {
			p += 2;
			n++;
			continue;
		}
		if (*p == '\n') {
			for (; n > 0; n--)
				*q++ = '\n';
		}
		*q++ = *p++;
	}
	*q = '\0';
}

/* register a buffer that is already in the TU allocator; canonicalizes in place */
static struct cp_file *cp_file_register(struct cp_tu *tu, const char *name,
					char *buf, size_t size,
					bool system_header, bool main_file)
{
	struct cp_file *file;

	if (CP_GROW(tu, tu->files, tu->num_files, tu->cap_files))
		return NULL;

	file = cp_alloc(tu, sizeof(*file));
	if (!file)
		return NULL;

	/* buf has room for the terminating newline and NUL (see callers) */
	if (!size || buf[size - 1] != '\n')
		buf[size++] = '\n';
	buf[size] = '\0';

	/* wipe a BOM */
	if (size >= 3 && (uint8_t)buf[0] == 0xef && (uint8_t)buf[1] == 0xbb &&
	    (uint8_t)buf[2] == 0xbf)
		buf[0] = buf[1] = buf[2] = ' ';

	canonicalize_newline(buf);
	remove_backslash_newline(buf);

	file->name = cp_strndup(tu, name, strlen(name));
	file->contents = buf;
	file->size = strlen(buf);
	file->incl_idx = -1;
	file->file_no = (int)tu->num_files;
	file->system_header = system_header;
	file->main_file = main_file;
	if (!file->name)
		return NULL;

	tu->files[tu->num_files++] = file;
	return file;
}

struct cp_file *cp_file_add(struct cp_tu *tu, const char *name,
			    const char *contents, size_t size,
			    bool system_header, bool main_file)
{
	char *buf;

	buf = cp_alloc(tu, size + 2);
	if (!buf)
		return NULL;
	memcpy(buf, contents, size);
	return cp_file_register(tu, name, buf, size, system_header, main_file);
}

struct cp_file *cp_file_add_from_path(struct cp_tu *tu, const char *path,
				      bool system_header, bool main_file)
{
	struct cp_file *file;
	struct stat st;
	char *buf;
	size_t size;
	FILE *fp;

	fp = fopen(path, "rb");
	if (!fp) {
		cp_error_at(tu, NULL, NULL, "%s: cannot open file: %s",
			    path, strerror(errno));
		return NULL;
	}

	if (fstat(fileno(fp), &st) || !S_ISREG(st.st_mode)) {
		cp_error_at(tu, NULL, NULL, "%s: not a regular file", path);
		fclose(fp);
		return NULL;
	}

	buf = cp_alloc(tu, (size_t)st.st_size + 2);
	if (!buf) {
		cp_error_at(tu, NULL, NULL, "%s: out of memory", path);
		fclose(fp);
		return NULL;
	}

	size = fread(buf, 1, (size_t)st.st_size, fp);
	if (size != (size_t)st.st_size && ferror(fp)) {
		cp_error_at(tu, NULL, NULL, "%s: read error: %s", path, strerror(errno));
		fclose(fp);
		return NULL;
	}
	fclose(fp);

	file = cp_file_register(tu, path, buf, size, system_header, main_file);
	if (!file)
		cp_error_at(tu, NULL, NULL, "%s: cannot register file", path);
	return file;
}
