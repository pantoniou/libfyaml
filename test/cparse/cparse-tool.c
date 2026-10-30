/*
 * cparse-tool.c - development tool for the C declaration front end
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cparse-private.h"

static const char *tok_kind_name(enum cp_token_kind kind)
{
	switch (kind) {
	case CPT_IDENT: return "ident";
	case CPT_PUNCT: return "punct";
	case CPT_STR:   return "string";
	case CPT_CHAR:  return "char";
	case CPT_NUM:   return "number";
	case CPT_EOF:   return "eof";
	}
	return "?";
}

/* the macros that cparse predefines for a target against the ABI cparse has for it */
static int check_predefs(struct cp_tu *tu)
{
	static const char text[] =
		"__SIZEOF_POINTER__ __SIZEOF_SHORT__ __SIZEOF_INT__ __SIZEOF_LONG__\n"
		"__SIZEOF_LONG_LONG__ __SIZEOF_LONG_DOUBLE__ __CHAR_BIT__\n"
		"#ifdef __CHAR_UNSIGNED__\n1\n#else\n0\n#endif\n";
	static const char *const names[] = {
		"pointer", "short", "int", "long", "long long", "long double", "char bits",
		"unsigned char",
	};
	const struct cp_target *t = tu->target;
	int64_t want[8];
	int64_t got[8];
	struct cp_file *file;
	struct cp_token *tok;
	int i, n = 0, rc = 0;

	want[0] = t->ptr.size;
	want[1] = t->short_.size;
	want[2] = t->int_.size;
	want[3] = t->long_.size;
	want[4] = t->llong_.size;
	want[5] = t->ldouble.size;
	want[6] = 8;
	want[7] = t->char_unsigned;

	file = cp_file_add(tu, "predefs-check", text, sizeof(text) - 1, false, true);
	if (!file)
		return 1;
	tok = cp_preprocess(tu, file);
	for (; tok && tok->kind != CPT_EOF && n < 8; tok = tok->next)
		got[n++] = strtoll(tok->loc, NULL, 0);
	if (n != 8) {
		printf("predefs: %d values\n", n);
		return 1;
	}
	for (i = 0; i < 8; i++) {
		if (got[i] != want[i]) {
			printf("predefs: %s is %lld, the target has %lld\n", names[i],
			       (long long)got[i], (long long)want[i]);
			rc = 1;
		}
	}
	if (!rc)
		printf("predefs match the target\n");
	return rc;
}

static int usage(const char *prog)
{
	fprintf(stderr, "usage: %s [-I dir] [-isystem dir] [-iquote dir] [-D def] [-U name] [-imacros file] [-include file] [-P file] [-B compiler]\n"
			"       %*s tokens|comments|pp|decls|check-predefs <file>\n"
			"  -B name  use the macros and system directories compiled in for the compiler (gcc, clang,\n"
			"           or - for the compiler that built cparse)\n"
			"  -P file  read `#define NAME VALUE` lines (clang -dM output) as predefined macros\n",
		prog, (int)strlen(prog), "");
	return 2;
}

/* "#define NAME VALUE" / "#define NAME(a, b) VALUE" -> "NAME=VALUE" / "NAME(a, b)=VALUE" */
static char *dm_to_define(const char *line)
{
	const char *p = line, *name, *rest;
	size_t nlen;
	char *res;

	if (strncmp(p, "#define ", 8))
		return NULL;
	name = p + 8;
	for (rest = name; *rest && *rest != ' ' && *rest != '('; rest++)
		;
	if (*rest == '(') {
		while (*rest && *rest != ')')
			rest++;
		if (*rest)
			rest++;
	}
	nlen = (size_t)(rest - name);
	if (*rest == ' ')
		rest++;
	res = malloc(nlen + strlen(rest) + 3);
	if (!res)
		return NULL;
	sprintf(res, "%.*s=%s", (int)nlen, name, rest);
	return res;
}

/*
 * libclang's type of a declaration (a field, a typedef's underlying type) does
 * not show its outermost nullability; nested ones and parameters keep theirs.
 */
static const char *decl_type_spelling(struct cp_tu *tu, struct cp_type *ty)
{
	unsigned nullq = CPQ_NONNULL | CPQ_NULLABLE | CPQ_NULL_UNSPEC;

	if (ty->quals & nullq)
		ty = cp_type_requalified(tu, ty, ty->quals & ~nullq);
	return cp_type_spelling(tu, ty);
}

/* clang_getEnumConstantDeclValue: sign extended from the width of the enum */
static long long enum_value(const struct cp_decl *d)
{
	const struct cp_type *e = d->parent && d->parent->type ? cp_type_canonical(d->parent->type) : NULL;
	int bits = e && e->size > 0 ? (int)(e->size * 8) : 64;
	uint64_t v = (uint64_t)d->value;

	if (bits < 64) {
		v &= (UINT64_C(1) << bits) - 1;
		if (v >> (bits - 1))
			v |= ~((UINT64_C(1) << bits) - 1);
	}
	return (long long)v;
}

/* the cursor name of a tag: libclang 16+ names an anonymous one by its typedef */
static const char *tag_name(const struct cp_decl *d, const struct cp_type *t, int clang_ver)
{
	if (d->name)
		return d->name;
	if (clang_ver >= 16 && t && t->linkage_name)
		return t->linkage_name;
	return "(anon)";
}

static void print_decls(struct cp_tu *tu, int clang_ver)
{
	size_t i;
	const struct cp_decl *d;
	const struct cp_type *t;
	int line;
	int col;
	const char *q;

	for (i = 0; i < tu->num_decls; i++) {
		d = tu->decls[i];
		t = d->type ? cp_type_canonical(d->type) : NULL;
		line = 0;
		col = 0;

		if (d->file)
			cp_file_location(d->file, d->file->contents + d->start, &line, &col);

		switch (d->kind) {
		case CPDK_STRUCT:
		case CPDK_UNION:
			printf("%s %s %s size=%lld align=%d @%d:%d\n",
			       d->kind == CPDK_STRUCT ? "struct" : "union",
			       tag_name(d, t, clang_ver), d->is_definition ? "def" : "fwd",
			       (long long)(t ? t->size : -1),
			       t && t->size >= 0 ? t->align : -1, line, col);
			break;
		case CPDK_ENUM:
			printf("enum %s %s underlying='%s' size=%lld @%d:%d\n",
			       tag_name(d, t, clang_ver), d->is_definition ? "def" : "fwd",
			       t && t->base ? cp_type_spelling(tu, t->base) : "?",
			       (long long)(t ? t->size : -1), line, col);
			break;
		case CPDK_ENUMERATOR:
			printf("  enumerator %s = %lld\n", d->name, enum_value(d));
			break;
		case CPDK_TYPEDEF:
			printf("typedef %s = '%s' size=%lld @%d:%d\n", d->name,
			       decl_type_spelling(tu, d->type->base),
			       (long long)cp_type_size(d->type), line, col);
			break;
		case CPDK_FIELD:
			if (d->field && d->field->bit_width >= 0)
				printf("  field %s '%s' bitpos=%lld bits=%d\n",
				       d->name ? d->name : "(anon)", decl_type_spelling(tu, d->type),
				       (long long)d->field->offset * 8 + d->field->bit_offset,
				       d->field->bit_width);
			else
				printf("  field %s '%s' off=%lld\n", d->name ? d->name : "(anon)",
				       decl_type_spelling(tu, d->type),
				       (long long)(d->field ? d->field->offset : 0));
			break;
		}
		if (d->comment && getenv("LIBCLANG_DUMP_COMMENTS")) {
			printf("    comment: ");
			for (q = d->comment; *q; q++) {
				if (*q == '\n')
					printf("\\n");
				else
					putchar(*q);
			}
			putchar('\n');
		}
	}
}

static void print_pp(const struct cp_token *tok)
{
	bool first = true;

	for (; tok; tok = tok->next) {
		if (tok->kind == CPT_EOF)
			break;
		if (!first && tok->at_bol)
			putchar('\n');
		else if (!first && tok->has_space)
			putchar(' ');
		printf("%.*s", tok->len, tok->loc);
		first = false;
	}
	putchar('\n');
}

int main(int argc, char **argv)
{
	struct cp_options opts = { .std_c23 = false };
	struct cp_include_path *incs = NULL;
	const char **defs = NULL, **forced, **macro_files;
	char **owned = NULL;
	size_t num_incs = 0, num_defs = 0, num_owned = 0, num_forced = 0, num_macro_files = 0;
	struct cp_tu *tu;
	struct cp_file *file;
	struct cp_token *tok;
	const char *cmd, *path;
	size_t i;
	int rc = 0, line, col, ai = 1, clang_ver = 18;
	char buf[8192];
	FILE *fp;
	const char *targs[16], *terr = NULL;
	int num_targs = 0;
	bool use_predefs = false;
	const char *predefs_compiler = NULL;
	const struct cp_predef_set *set;
	const char **all;
	size_t j;
	struct cp_target target;
	char *u;
	char *d;
	const struct cp_comment *c;
	const struct cp_diag *d2;

	incs = calloc(argc + 1, sizeof(*incs));
	defs = calloc(argc + 1, sizeof(*defs));
	forced = calloc(argc + 1, sizeof(*forced));
	macro_files = calloc(argc + 1, sizeof(*macro_files));
	for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai += 2) {
		/* the options that change the target take effect together, below */
		if (!strcmp(argv[ai], "-m32") || !strcmp(argv[ai], "-m64") ||
		    !strcmp(argv[ai], "-funsigned-char") || !strcmp(argv[ai], "-fsigned-char") ||
		    !strncmp(argv[ai], "--target=", 9)) {
			targs[num_targs++] = argv[ai];
			ai--;			/* no value */
			continue;
		}
		if (ai + 1 < argc && !strcmp(argv[ai], "-T")) {
			targs[num_targs++] = "-target";
			targs[num_targs++] = argv[ai + 1];
			continue;
		}
		if (ai + 1 >= argc)
			return usage(argv[0]);
		if (!strcmp(argv[ai], "-I")) {
			incs[num_incs].kind = CPIK_USER;
			incs[num_incs++].path = argv[ai + 1];
		} else if (!strcmp(argv[ai], "-isystem")) {
			incs[num_incs].kind = CPIK_SYSTEM;
			incs[num_incs++].path = argv[ai + 1];
		} else if (!strcmp(argv[ai], "-iquote")) {
			incs[num_incs].kind = CPIK_QUOTE;
			incs[num_incs++].path = argv[ai + 1];
		} else if (!strcmp(argv[ai], "-V")) {
			clang_ver = atoi(argv[ai + 1]);
		} else if (!strcmp(argv[ai], "-D")) {
			defs[num_defs++] = argv[ai + 1];
		} else if (!strcmp(argv[ai], "-U")) {
			u = malloc(strlen(argv[ai + 1]) + 2);

			sprintf(u, "-%s", argv[ai + 1]);
			defs[num_defs++] = u;
			owned = realloc(owned, (num_owned + 1) * sizeof(*owned));
			owned[num_owned++] = u;
		} else if (!strcmp(argv[ai], "-imacros")) {
			macro_files[num_macro_files++] = argv[ai + 1];
		} else if (!strcmp(argv[ai], "-include")) {
			forced[num_forced++] = argv[ai + 1];
		} else if (!strcmp(argv[ai], "-B")) {
			use_predefs = true;
			predefs_compiler = strcmp(argv[ai + 1], "-") ? argv[ai + 1] : NULL;
		} else if (!strcmp(argv[ai], "-P")) {
			fp = fopen(argv[ai + 1], "r");
			if (!fp) {
				perror(argv[ai + 1]);
				return 1;
			}
			while (fgets(buf, sizeof(buf), fp)) {
				buf[strcspn(buf, "\n")] = '\0';
				d = dm_to_define(buf);
				if (d) {
					defs = realloc(defs, (num_defs + argc + 2) * sizeof(*defs));
					defs[num_defs++] = d;
					owned = realloc(owned, (num_owned + 1) * sizeof(*owned));
					owned[num_owned++] = d;
				}
			}
			fclose(fp);
		} else {
			return usage(argv[0]);
		}
	}
	if (argc - ai != 2)
		return usage(argv[0]);
	cmd = argv[ai];
	path = argv[ai + 1];

	if (num_targs) {
		if (cp_target_from_args(NULL, num_targs, targs, &target, &terr)) {
			fprintf(stderr, "unsupported target: %s\n", terr);
			rc = 2;
			goto cleanup;
		}
		opts.target = &target;
	}
	if (use_predefs) {
		set = cp_predefs_find(predefs_compiler, opts.target ? opts.target : cp_target_host());
		if (!set) {
			fprintf(stderr, "no predefined macros for the target\n");
			rc = 2;
			goto cleanup;
		}
		/* the macros of the compiler first, then the -D and -P ones */
		all = calloc(set->num_defines + num_defs + 1, sizeof(*all));
		for (j = 0; j < set->num_defines; j++)
			all[j] = set->defines[j];
		for (j = 0; j < num_defs; j++)
			all[set->num_defines + j] = defs[j];
		free(defs);
		defs = all;
		num_defs += set->num_defines;
	}
	opts.include_paths = incs;
	opts.num_include_paths = num_incs;
	opts.defines = defs;
	opts.num_defines = num_defs;
	opts.macro_files = macro_files;
	opts.num_macro_files = num_macro_files;
	opts.force_includes = forced;
	opts.num_force_includes = num_forced;

	tu = cp_tu_create(&opts);
	if (!tu)
		return 1;

	file = cp_file_add_from_path(tu, path, false, true);
	if (file) {
		if (!strcmp(cmd, "check-predefs")) {
			rc = check_predefs(tu);
		} else if (!strcmp(cmd, "pp")) {
			tok = cp_preprocess(tu, file);
			if (tok)
				print_pp(tok);
		} else if (!strcmp(cmd, "decls")) {
			tok = cp_preprocess(tu, file);
			if (tok && !cp_tu_failed(tu)) {
				cp_parse(tu, tok);
				cp_attach_comments(tu);
				print_decls(tu, clang_ver);
			}
		} else {
			tok = cp_tokenize(tu, file);
			if (!strcmp(cmd, "tokens")) {
				for (; tok; tok = tok->next) {
					cp_file_location(file, tok->loc, &line, &col);
					printf("%d:%d %s%s%s '%.*s'\n", line, col,
					       tok_kind_name(tok->kind),
					       tok->at_bol ? " bol" : "",
					       tok->has_space ? " sp" : "",
					       tok->len, tok->loc);
					if (tok->kind == CPT_EOF)
						break;
				}
			} else if (!strcmp(cmd, "comments")) {
				for (i = 0; i < tu->num_comments; i++) {
					c = &tu->comments[i];

					printf("%d:%d-%d %s%s [%zu,%zu) <<%.*s>>\n",
					       c->line, c->column, c->end_line,
					       c->kind == CPCK_LINE ? "line" : "block",
					       c->after_code ? " trailing" : "",
					       c->start, c->end,
					       (int)(c->end - c->start), file->contents + c->start);
				}
			} else {
				rc = usage(argv[0]);
			}
		}
	}

	for (i = 0; i < tu->num_diags; i++) {
		d2 = &tu->diags[i];

		fprintf(stderr, "%s:%d:%d: %s: %s\n",
			d2->file ? d2->file->name : "<none>", d2->line, d2->column,
			d2->severity == CPDS_ERROR ? "error" :
			d2->severity == CPDS_WARNING ? "warning" : "note", d2->msg);
	}
	if (cp_tu_failed(tu))
		rc = 1;

	cp_tu_destroy(tu);
cleanup:
	for (i = 0; i < num_owned; i++)
		free(owned[i]);
	free(owned);
	free(incs);
	free(defs);
	free(forced);
	free(macro_files);
	return rc;
}
