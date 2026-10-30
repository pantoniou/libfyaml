/*
 * libclang-dump.c - reference dumper for the cparse differential tests
 *
 * Prints the struct, union, enum and typedef declarations of a translation
 * unit in the same format as `cparse-tool decls`, using libclang, so that the
 * two can be diffed. Build it against the libclang you want to compare with:
 *
 *   cc -o libclang-dump libclang-dump.c -I$LLVM/include -L$LLVM/lib -lclang
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include <clang-c/Index.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *cs(CXString s, char *buf, size_t n)
{
	const char *p = clang_getCString(s);
	snprintf(buf, n, "%s", p ? p : "");
	clang_disposeString(s);
	return buf;
}

static void loc(CXCursor c, unsigned *line, unsigned *col, int *has_file)
{
	CXSourceLocation l = clang_getRangeStart(clang_getCursorExtent(c));
	CXFile f;

	clang_getFileLocation(l, &f, line, col, NULL);
	*has_file = f != NULL;
}

static long long norm(long long v) { return v < 0 ? -1 : v; }

/* with LIBCLANG_DUMP_COMMENTS set, the raw comment of a declaration follows it */
static void comment_of(CXCursor c)
{
	CXString raw;
	const char *p;

	if (!getenv("LIBCLANG_DUMP_COMMENTS"))
		return;
	raw = clang_Cursor_getRawCommentText(c);
	p = clang_getCString(raw);
	if (p && *p) {
		printf("    comment: ");
		for (; *p; p++) {
			if (*p == '\n')
				printf("\\n");
			else
				putchar(*p);
		}
		putchar('\n');
	}
	clang_disposeString(raw);
}

static const char *spell_type(CXType t, char *buf, size_t n)
{
	return cs(clang_getTypeSpelling(t), buf, n);
}

static const char *cname(CXCursor c, char *buf, size_t n)
{
	cs(clang_getCursorSpelling(c), buf, n);
	if (!buf[0] || strstr(buf, "(unnamed") || strstr(buf, "(anonymous"))
		snprintf(buf, n, "(anon)");
	return buf;
}

static void dump(CXCursor c);

static enum CXChildVisitResult child(CXCursor c, CXCursor parent, CXClientData d)
{
	(void)parent; (void)d;
	dump(c);
	return CXChildVisit_Continue;
}

static void dump(CXCursor c)
{
	char name[512], tb[1024], ub[1024];
	unsigned line, col;
	int hf;
	CXType t = clang_getCursorType(c);

	loc(c, &line, &col, &hf);

	switch (clang_getCursorKind(c)) {
	case CXCursor_StructDecl:
	case CXCursor_UnionDecl:
		if (!hf)
			return;
		printf("%s %s %s size=%lld align=%lld @%u:%u\n",
		       clang_getCursorKind(c) == CXCursor_StructDecl ? "struct" : "union",
		       cname(c, name, sizeof(name)), clang_isCursorDefinition(c) ? "def" : "fwd",
		       norm(clang_Type_getSizeOf(t)), norm(clang_Type_getAlignOf(t)), line, col);
		comment_of(c);
		clang_visitChildren(c, child, NULL);
		break;
	case CXCursor_EnumDecl:
		if (!hf)
			return;
		printf("enum %s %s underlying='%s' size=%lld @%u:%u\n", cname(c, name, sizeof(name)),
		       clang_isCursorDefinition(c) ? "def" : "fwd",
		       spell_type(clang_getEnumDeclIntegerType(c), tb, sizeof(tb)),
		       norm(clang_Type_getSizeOf(t)), line, col);
		comment_of(c);
		clang_visitChildren(c, child, NULL);
		break;
	case CXCursor_EnumConstantDecl:
		printf("  enumerator %s = %lld\n", cname(c, name, sizeof(name)),
		       clang_getEnumConstantDeclValue(c));
		comment_of(c);
		break;
	case CXCursor_TypedefDecl:
		if (!hf)
			return;
		printf("typedef %s = '%s' size=%lld @%u:%u\n", cname(c, name, sizeof(name)),
		       spell_type(clang_getTypedefDeclUnderlyingType(c), ub, sizeof(ub)),
		       norm(clang_Type_getSizeOf(t)), line, col);
		comment_of(c);
		break;
	case CXCursor_FieldDecl:
		if (clang_Cursor_isBitField(c))
			printf("  field %s '%s' bitpos=%lld bits=%d\n", cname(c, name, sizeof(name)),
			       spell_type(t, tb, sizeof(tb)), clang_Cursor_getOffsetOfField(c),
			       clang_getFieldDeclBitWidth(c));
		else
			printf("  field %s '%s' off=%lld\n", cname(c, name, sizeof(name)),
			       spell_type(t, tb, sizeof(tb)), clang_Cursor_getOffsetOfField(c) / 8);
		comment_of(c);
		break;
	default:
		break;
	}
}

static enum CXChildVisitResult top(CXCursor c, CXCursor parent, CXClientData d)
{
	(void)parent; (void)d;
	dump(c);
	return CXChildVisit_Continue;
}

int main(int argc, char **argv)
{
	char b[1024];

	CXIndex idx = clang_createIndex(0, 0);
	CXTranslationUnit tu;
	unsigned i, nd;
	int major = 0;
	const char *want = NULL;

	/* --major: the libclang major version, the test script needs it */
	if (argc == 2 && !strcmp(argv[1], "--major")) {
		sscanf(cs(clang_getClangVersion(), b, sizeof(b)), "%*[^0-9]%d", &major);
		printf("%d\n", major);
		return 0;
	}

	for (i = 1; i < (unsigned)argc - 1; i++) {
		if (!strcmp(argv[i], "-target") && i + 1 < (unsigned)argc - 1)
			want = argv[i + 1];
		else if (!strncmp(argv[i], "--target=", 9))
			want = argv[i] + 9;
	}

	tu = clang_parseTranslationUnit(idx, argv[argc - 1], (const char *const *)argv + 1,
					argc - 2, NULL, 0, CXTranslationUnit_None);
	if (!tu) {
		/* a libclang that does not know the target cannot make a unit for it */
		if (want)
			fprintf(stderr, "clang: error: unknown target triple '%s'\n", want);
		return 1;
	}
	nd = clang_getNumDiagnostics(tu);
	for (i = 0; i < nd; i++) {
		CXDiagnostic dg = clang_getDiagnostic(tu, i);

		if (clang_getDiagnosticSeverity(dg) >= CXDiagnostic_Error) {
			fprintf(stderr, "clang: %s\n", cs(clang_formatDiagnostic(dg, 0), b, sizeof(b)));
		}
		clang_disposeDiagnostic(dg);
	}
	clang_visitChildren(clang_getTranslationUnitCursor(tu), top, NULL);
	clang_disposeTranslationUnit(tu);
	clang_disposeIndex(idx);
	return 0;
}
