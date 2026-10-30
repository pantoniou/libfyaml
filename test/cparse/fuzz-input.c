/*
 * fuzz-input.c - feed damaged C text to the cparse front end
 *
 * usage: fuzz-input [-s SEED] [-n ITERATIONS] [FILE...]
 *
 * The program makes inputs from a built-in corpus and from the FILEs. It cuts
 * each input at many places (a text that ends in the middle of a token, a
 * comment, a string or a UTF-8 sequence), puts invalid UTF-8 in it, and then
 * mutates it at random. Each input goes through the tokenizer, the
 * preprocessor, the parser, the comment attachment and the type text parser.
 * The program has no oracle: a crash, a hang or (under ASAN) a memory error is
 * the failure. The PRNG is the same on all hosts, so a failure repeats with the
 * same SEED.
 *
 * The text is in a heap buffer of exactly its size, and the TU uses the malloc
 * allocator, so ASAN sees a read past the end of the text or of any object.
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
#include <stdint.h>

#include "cparse-private.h"

#define DEFAULT_ITERATIONS	20000
#define FUZZ_MAX_INPUT		(1 << 16)

static const char *const corpus[] = {
	"int x;\n",
	"typedef unsigned long size_t;\nstruct s { int a; char b[4]; size_t n; };\n",
	"struct bf { unsigned a : 3, b : 5; int : 0; long c : 40; } __attribute__((packed));\n",
	"enum e { A, B = 1 << 4, C = B | 7, D = -1 };\n",
	"union u { float f; unsigned i; struct { short lo, hi; }; };\n",
	"#define A(x, y) ((x) + (y))\n#define S(x) #x\n#define C(a, b) a ## b\n"
	"int v[A(1, 2) * 3];\nchar *s = S(hello);\nint C(a, b);\n",
	"#if defined(X) && (1 + 2 * 3 > 4) || __has_include(<stdio.h>)\nint y;\n#elif 0\nint z;\n"
	"#else\nint w;\n#endif\n",
	"#ifdef FOO\n#ifndef BAR\n#define BAZ 1\n#endif\n#endif\n#undef BAZ\n",
	"#pragma pack(push, 1)\nstruct p { char c; int i; };\n#pragma pack(pop)\n",
	"/* block comment */ int a; // line comment\n/** doc */ int b; ///< trailing\n",
	"int a; /* unterminated comment\nint b;\n",
	"const char *s = \"a string with \\\"escapes\\\" and \\n\\x41\\101\\u00e9\";\n",
	"const char *s = \"unterminated string\nint a;\n",
	"char c = 'a', d = '\\n', e = '\\'', f = '\\x7f'; wchar_t w = L'\\u20ac';\n",
	"int a = 0x1f + 077 + 0b101 + 1'000 + 12345678901234567890ull + 1.5e3f + 0x1.8p1;\n",
	"int \xc3\xa9t\xc3\xa9 = 1; /* \xe2\x82\xac */ const char *s = \"\xf0\x9f\x98\x80\";\n",
	"\xef\xbb\xbfint bom;\r\nint crlf;\r\n#define M \\\r\n  1\r\n",
	"int f(int (*cb)(void *, const char *), ...);\nint (*g[3])(int);\nint (*(*h)(int))[4];\n",
	"_Static_assert(sizeof(int) == 4, \"int\");\n_Alignas(16) char buf[16];\n",
	"typedef int v4 __attribute__((vector_size(16)));\nstruct a { char c; } __attribute__((aligned(8)));\n",
	"__extension__ typedef __typeof__(1 + 2) t; int a = __builtin_offsetof(struct s, b);\n",
	"#include \"does-not-exist.h\"\n#include <nor-this.h>\n#include MACRO\n",
	"#define EMPTY\n#define LONG(a, ...) a, __VA_ARGS__\nint a[] = { LONG(1, 2, 3) };\n",
	"#error stop\n#warning careful\n#line 100 \"x.c\"\n#bogus\n#\n",
	"struct { int a; } x, y[2], *z; struct fwd; struct fwd *p; union uu u; enum ee *q;\n",
	"int a[\n",
	"struct s {\n",
	"#if 1 +\n",
	"#define\n",
	"\"",
	"'",
	"/*",
	"\\",
	"#",
	"",
};

/* the strings that are placed in the input at random */
static const char *const pieces[] = {
	"/*", "*/", "//", "\"", "'", "\\", "\\\n", "\r\n", "\r", "\n", "#", "##", "#if ", "#else\n",
	"#endif\n", "#define X ", "#include ", "#pragma pack(", "(", ")", "{", "}", "[", "]",
	";", ",", ":", "...", "<<", ">>=", "->", "__attribute__((", "struct ", "union ", "enum ",
	"typedef ", "sizeof", "_Alignas(", "__typeof__(", "0x", "1e", "'\\u", "L\"", "u8\"",
};

/* the UTF-8 that a decoder gets wrong or rejects */
static const char *const bad_utf8[] = {
	"\x80", "\xbf", "\xc0\x80", "\xc1\xbf", "\xc2", "\xe0\x80\x80", "\xe0\x9f\xbf", "\xe0\xa0",
	"\xed\xa0\x80", "\xed\xbf\xbf", "\xef\xbf\xbe", "\xef\xbf\xbf", "\xf0\x80\x80\x80",
	"\xf0\x8f\xbf\xbf", "\xf0\x9f", "\xf0\x9f\x98", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80",
	"\xf8\x88\x80\x80\x80", "\xfc\x84\x80\x80\x80\x80", "\xfe", "\xff", "\xc3\x28", "\xe2\x82\x28",
	"\xf0\x9f\x98\x28", "\xe2\x28\xa1", "\xef\xbb", "\xef\xbb\xbf", "\xc2\xa0", "\xe2\x80\xa8",
};

struct rng {
	uint64_t s;
};

static uint32_t rnd(struct rng *r)
{
	/* xorshift64*, the same on all hosts */
	r->s ^= r->s >> 12;
	r->s ^= r->s << 25;
	r->s ^= r->s >> 27;
	return (uint32_t)((r->s * 0x2545f4914f6cdd1dull) >> 32);
}

static uint32_t rnd_below(struct rng *r, uint32_t n)
{
	return n ? rnd(r) % n : 0;
}

struct buf {
	char *p;
	size_t len;
};

static void buf_set(struct buf *b, const char *p, size_t len)
{
	if (len > FUZZ_MAX_INPUT)
		len = FUZZ_MAX_INPUT;
	memcpy(b->p, p, len);
	b->len = len;
}

/* insert n bytes at pos; the input stays below FUZZ_MAX_INPUT */
static void buf_insert(struct buf *b, size_t pos, const char *p, size_t n)
{
	if (pos > b->len)
		pos = b->len;
	if (b->len + n > FUZZ_MAX_INPUT)
		return;
	memmove(b->p + pos + n, b->p + pos, b->len - pos);
	memcpy(b->p + pos, p, n);
	b->len += n;
}

static void buf_delete(struct buf *b, size_t pos, size_t n)
{
	if (pos >= b->len)
		return;
	if (n > b->len - pos)
		n = b->len - pos;
	memmove(b->p + pos, b->p + pos + n, b->len - pos - n);
	b->len -= n;
}

struct input {
	char *p;
	size_t len;
};

static struct input *inputs;
static size_t num_inputs;

static int input_add(const char *p, size_t len)
{
	struct input *ni;
	char *copy;

	ni = realloc(inputs, (num_inputs + 1) * sizeof(*inputs));
	if (!ni)
		return -1;
	inputs = ni;
	copy = malloc(len ? len : 1);
	if (!copy)
		return -1;
	memcpy(copy, p, len);
	inputs[num_inputs].p = copy;
	inputs[num_inputs].len = len;
	num_inputs++;
	return 0;
}

static int input_add_file(const char *path)
{
	FILE *fp;
	char *p;
	size_t len;
	int rc;

	fp = fopen(path, "rb");
	if (!fp) {
		fprintf(stderr, "fuzz-input: cannot open %s\n", path);
		return -1;
	}
	p = malloc(FUZZ_MAX_INPUT);
	if (!p) {
		fclose(fp);
		return -1;
	}
	len = fread(p, 1, FUZZ_MAX_INPUT, fp);
	fclose(fp);
	rc = input_add(p, len);
	free(p);
	return rc;
}

static struct fy_allocator *malloc_allocator;
static unsigned long long runs;
static unsigned long long failed_runs;	/* inputs the front end rejected, only for the report */

/*
 * Run one input through the whole front end. The text is copied to a heap
 * buffer of exactly len bytes (no NUL), so a read past the end is a memory
 * error. variant picks the options.
 */
static void run_one(const char *text, size_t len, unsigned int variant)
{
	static const char *const defines[] = { "FOO", "BAR=2", "FN(a,b)=((a)+(b))", "-FOO" };
	struct cp_options opts;
	struct cp_tu *tu;
	struct cp_file *file;
	struct cp_token *tok;
	const struct cp_predef_set *set;
	const struct cp_target *target;
	char *exact;

	exact = malloc(len ? len : 1);
	if (!exact)
		abort();
	memcpy(exact, text, len);

	memset(&opts, 0, sizeof(opts));
	if (variant & 1)
		opts.allocator = malloc_allocator;
	opts.std_c23 = !!(variant & 2);
	if (variant & 4) {
		opts.defines = defines;
		opts.num_defines = ARRAY_SIZE(defines);
	}
	target = cp_target_host();
	/* the compiled-in macros are many: use them in a few runs only */
	if (variant & 8) {
		set = cp_predefs_find("clang", target);
		if (set) {
			opts.defines = set->defines;
			opts.num_defines = set->num_defines;
		}
	}

	tu = cp_tu_create(&opts);
	if (!tu)
		abort();

	file = cp_file_add(tu, "fuzz.c", exact, len, false, true);
	free(exact);

	if (file) {
		switch (variant >> 4 & 3) {
		case 0:
			/* the tokenizer alone */
			tok = cp_tokenize(tu, file);
			break;
		case 1:
			/* the preprocessor */
			tok = cp_preprocess(tu, file);
			break;
		default:
			/* the whole front end */
			tok = cp_preprocess(tu, file);
			if (tok && !cp_tu_failed(tu)) {
				cp_parse(tu, tok);
				cp_attach_comments(tu);
			}
			break;
		}
		(void)tok;
	}

	/* the type text parser reads text that has a length and no NUL */
	if (variant & 0x40) {
		exact = malloc(len ? len : 1);
		if (!exact)
			abort();
		memcpy(exact, text, len);
		(void)cp_parse_type_text(tu, exact, len);
		free(exact);
	}

	if (cp_tu_failed(tu))
		failed_runs++;
	cp_tu_destroy(tu);
	runs++;
}

static void run_all_variants(const char *text, size_t len, struct rng *r)
{
	run_one(text, len, rnd(r) & 0x7f);
}

/*
 * Every cut of the input, so that the text ends in the middle of any token or
 * comment or UTF-8 sequence. A long input gets a limited number of cuts.
 */
static void run_prefixes(const char *text, size_t len, struct rng *r)
{
	size_t step = len > 512 ? len / 256 : 1;
	size_t cut;

	for (cut = 0; cut <= len; cut += step)
		run_all_variants(text, cut, r);
	run_all_variants(text, len, r);
}

/* every invalid UTF-8 string at some places of the input, and at its end */
static void run_bad_utf8(const char *text, size_t len, struct rng *r)
{
	struct buf b;
	size_t i, j, pos;

	b.p = malloc(FUZZ_MAX_INPUT + 16);
	if (!b.p)
		abort();

	for (i = 0; i < ARRAY_SIZE(bad_utf8); i++) {
		/* at the end: a sequence that is cut short by the end of the file */
		buf_set(&b, text, len);
		buf_insert(&b, b.len, bad_utf8[i], strlen(bad_utf8[i]));
		run_all_variants(b.p, b.len, r);

		/* at the start, and in the middle of a few places */
		for (j = 0; j < 4; j++) {
			buf_set(&b, text, len);
			pos = j == 0 ? 0 : rnd_below(r, (uint32_t)len + 1);
			buf_insert(&b, pos, bad_utf8[i], strlen(bad_utf8[i]));
			run_all_variants(b.p, b.len, r);
		}
	}
	free(b.p);
}

static void mutate(struct buf *b, struct rng *r)
{
	unsigned int n = 1 + rnd_below(r, 4);
	const char *s;
	char chunk[32];
	size_t pos, n2;

	while (n--) {
		pos = rnd_below(r, (uint32_t)b->len + 1);
		switch (rnd_below(r, 9)) {
		case 0:
			/* flip a bit */
			if (b->len)
				b->p[pos % b->len] ^= (char)(1 << rnd_below(r, 8));
			break;
		case 1:
			/* a random byte, often a high one */
			if (b->len)
				b->p[pos % b->len] = (char)(rnd_below(r, 2) ? 0x80 + rnd_below(r, 128) :
							    rnd_below(r, 128));
			break;
		case 2:
			/* a NUL */
			if (b->len)
				b->p[pos % b->len] = '\0';
			break;
		case 3:
			buf_delete(b, pos, 1 + rnd_below(r, 8));
			break;
		case 4:
			/* cut the end (a short file) */
			b->len = pos;
			break;
		case 5:
			s = pieces[rnd_below(r, ARRAY_SIZE(pieces))];
			buf_insert(b, pos, s, strlen(s));
			break;
		case 6:
			s = bad_utf8[rnd_below(r, ARRAY_SIZE(bad_utf8))];
			buf_insert(b, pos, s, strlen(s));
			break;
		case 7:
			/* a copy of a part of the input */
			if (b->len) {
				n2 = 1 + rnd_below(r, 32);
				if (n2 > b->len - pos % b->len)
					n2 = b->len - pos % b->len;
				memcpy(chunk, b->p + pos % b->len, n2);
				buf_insert(b, rnd_below(r, (uint32_t)b->len + 1), chunk, n2);
			}
			break;
		default:
			/* a part of another input */
			if (num_inputs) {
				const struct input *o = &inputs[rnd_below(r, (uint32_t)num_inputs)];

				n2 = rnd_below(r, (uint32_t)o->len + 1);
				if (n2 > 64)
					n2 = 64;
				buf_insert(b, pos, o->p, n2);
			}
			break;
		}
	}
}

static int usage(const char *prog)
{
	fprintf(stderr, "usage: %s [-s SEED] [-n ITERATIONS] [FILE...]\n", prog);
	return 2;
}

int main(int argc, char **argv)
{
	struct rng r;
	struct buf b;
	unsigned long long seed = 1, iterations = DEFAULT_ITERATIONS, i;
	size_t k;
	int a;

	for (a = 1; a < argc; a++) {
		if (!strcmp(argv[a], "-s") && a + 1 < argc)
			seed = strtoull(argv[++a], NULL, 0);
		else if (!strcmp(argv[a], "-n") && a + 1 < argc)
			iterations = strtoull(argv[++a], NULL, 0);
		else if (argv[a][0] == '-' && argv[a][1])
			return usage(argv[0]);
		else if (input_add_file(argv[a]))
			return 2;
	}
	for (k = 0; k < ARRAY_SIZE(corpus); k++)
		if (input_add(corpus[k], strlen(corpus[k])))
			return 2;

	malloc_allocator = fy_allocator_create("malloc", NULL);
	if (!malloc_allocator)
		return 2;

	r.s = seed * 0x9e3779b97f4a7c15ull + 1;
	b.p = malloc(FUZZ_MAX_INPUT * 2 + 64);
	if (!b.p)
		return 2;

	/* the cuts and the invalid UTF-8 for each input */
	for (k = 0; k < num_inputs; k++) {
		run_prefixes(inputs[k].p, inputs[k].len, &r);
		run_bad_utf8(inputs[k].p, inputs[k].len, &r);
	}

	/* then random damage */
	for (i = 0; i < iterations; i++) {
		k = rnd_below(&r, (uint32_t)num_inputs);
		buf_set(&b, inputs[k].p, inputs[k].len);
		mutate(&b, &r);
		run_all_variants(b.p, b.len, &r);
	}

	printf("fuzz-input: seed %llu, %llu runs, %llu rejected, no crash\n", seed, runs,
	       failed_runs);

	free(b.p);
	for (k = 0; k < num_inputs; k++)
		free(inputs[k].p);
	free(inputs);
	fy_allocator_destroy(malloc_allocator);
	return 0;
}
