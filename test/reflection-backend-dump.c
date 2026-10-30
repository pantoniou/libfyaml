/*
 * reflection-backend-dump.c - dump a reflection in an order that does not
 * depend on the backend
 *
 * usage: reflection-backend-dump BACKEND FILE [compiler options...]
 *
 * BACKEND is "clang" or "cparse". The tool prints one block for each top level
 * declaration (a tag, a typedef or a function type), with its type and its
 * children. The blocks are sorted. The tool lists the declarations of FILE.
 * With BACKEND_DUMP_SYSTEM set in the environment it lists the declarations of
 * all files.
 *
 * With BACKEND_DUMP_MACROS set in the environment, the cparse backend reads the
 * file for its macros. With BACKEND_DUMP_NO_PREDEFS set, it does not use the
 * compiled-in macros.
 *
 * With BACKEND_DUMP_GENERATE set in the environment, the tool prints the C that
 * the reflection generates and not the blocks.
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

#include "fy-reflection-private.h"
#if defined(HAVE_LIBCLANG) && HAVE_LIBCLANG
#include "fy-clang-backend.h"
#endif
#include "fy-cparse-backend.h"

static void put(char **buf, size_t *len, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static void put(char **buf, size_t *len, const char *fmt, ...)
{
	va_list ap, ap2;
	int n;
	char *nb;

	va_start(ap, fmt);
	va_copy(ap2, ap);
	n = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	nb = realloc(*buf, *len + (size_t)n + 1);
	if (!nb)
		abort();
	*buf = nb;
	vsnprintf(*buf + *len, (size_t)n + 1, fmt, ap);
	*len += (size_t)n;
	va_end(ap);
}

static int cmp(const void *a, const void *b)
{
	return strcmp(*(char * const *)a, *(char * const *)b);
}

/* the backends name anonymous tags differently (or not at all): print them alike */
static bool is_anon_tag(const struct fy_type *ft)
{
	return ft && (ft->flags & FYTF_ANONYMOUS) &&
	       (ft->type_kind == FYTK_STRUCT || ft->type_kind == FYTK_UNION ||
		ft->type_kind == FYTK_ENUM);
}

static void type_line(char **buf, size_t *len, const struct fy_type *ft)
{
	if (!ft) {
		put(buf, len, "<none>");
		return;
	}
	if (is_anon_tag(ft))
		put(buf, len, "'%s (anon)' ", fy_type_kind_name(ft->type_kind));
	else
		put(buf, len, "'%s' ", ft->fullname ? ft->fullname : "?");
	put(buf, len, "%s size=%zu align=%zu", fy_type_kind_name(ft->type_kind), ft->size,
	    ft->align);
}

int main(int argc, char **argv)
{
	struct fy_reflection *rfl;
	struct fy_decl *decl, *declc;
#if defined(HAVE_LIBCLANG) && HAVE_LIBCLANG
	struct fy_clang_backend_reflection_cfg ccfg;
	struct fy_clang_backend_import_cfg cicfg;
#endif
	struct fy_cparse_backend_reflection_cfg pcfg;
	struct fy_cparse_backend_import_cfg picfg;
	const void *rcfg, *icfg;
	char **blocks = NULL, *b;
	size_t nblocks = 0, len, i;
	bool system = getenv("BACKEND_DUMP_SYSTEM") != NULL;
	char *c;
	const char *macros = getenv("BACKEND_DUMP_MACROS");

	if (argc < 3)
		return 2;

	memset(&pcfg, 0, sizeof(pcfg));
	memset(&picfg, 0, sizeof(picfg));
	if (!strcmp(argv[1], "clang")) {
#if defined(HAVE_LIBCLANG) && HAVE_LIBCLANG
		memset(&ccfg, 0, sizeof(ccfg));
		memset(&cicfg, 0, sizeof(cicfg));
		cicfg.file = argv[2];
		cicfg.argc = argc - 3;
		cicfg.argv = (const char * const *)argv + 3;
		rcfg = &ccfg;
		icfg = &cicfg;
#else
		fprintf(stderr, "the clang backend is not built\n");
		return 2;
#endif
	} else {
		picfg.file = argv[2];
		picfg.argc = argc - 3;
		picfg.argv = (const char * const *)argv + 3;
		pcfg.display_diagnostics = true;
		pcfg.no_predefs = getenv("BACKEND_DUMP_NO_PREDEFS") != NULL;
		picfg.macro_files = &macros;
		picfg.num_macro_files = macros ? 1 : 0;
		rcfg = &pcfg;
		icfg = &picfg;
	}

	rfl = fy_reflection_from_imports(argv[1], rcfg, 1, &icfg, NULL);
	if (!rfl) {
		fprintf(stderr, "backend '%s' failed on %s\n", argv[1], argv[2]);
		return 1;
	}

	/* BACKEND_DUMP_GENERATE: print the C the reflection generates instead */
	if (getenv("BACKEND_DUMP_GENERATE")) {
		c = fy_reflection_generate_c_string(rfl, 0);

		if (!c) {
			fprintf(stderr, "generating C failed\n");
			return 1;
		}
		fputs(c, stdout);
		free(c);
		fy_reflection_destroy(rfl);
		return 0;
	}

	for (decl = fy_decl_list_head(&rfl->decls); decl; decl = fy_decl_next(&rfl->decls, decl)) {
		if (!system && (decl->flags & FYDF_IN_SYSTEM_HEADER))
			continue;

		b = NULL;
		len = 0;
		put(&b, &len, "%s '%s' ", decl_type_txt[decl->decl_type],
		    is_anon_tag(decl->type) ? "(anon)" : decl->name);
		type_line(&b, &len, decl->type);
		put(&b, &len, " flags=0x%x\n", decl->type ? (unsigned)(decl->type->flags & 0x1ff) : 0u);

		for (declc = fy_decl_list_head(&decl->children); declc;
		     declc = fy_decl_next(&decl->children, declc)) {
			put(&b, &len, "  %s '%s' ", decl_type_txt[declc->decl_type], declc->name);
			switch (declc->decl_type) {
			case FYDT_FIELD:
				put(&b, &len, "off=%zu ", declc->field_decl.byte_offset);
				break;
			case FYDT_BITFIELD:
				put(&b, &len, "bitoff=%zu width=%zu ", declc->bitfield_decl.bit_offset,
				    declc->bitfield_decl.bit_width);
				break;
			case FYDT_ENUM_VALUE:
				if (fy_type_kind_signess(declc->enum_value_decl.type_kind) > 0)
					put(&b, &len, "value=%ju ", declc->enum_value_decl.val.u);
				else
					put(&b, &len, "value=%jd ", declc->enum_value_decl.val.s);
				break;
			default:
				break;
			}
			type_line(&b, &len, declc->type);
			put(&b, &len, "\n");
		}

		blocks = realloc(blocks, (nblocks + 1) * sizeof(*blocks));
		blocks[nblocks++] = b;
	}

	qsort(blocks, nblocks, sizeof(*blocks), cmp);
	for (i = 0; i < nblocks; i++) {
		fputs(blocks[i], stdout);
		free(blocks[i]);
	}
	free(blocks);

	fy_reflection_destroy(rfl);
	return 0;
}
