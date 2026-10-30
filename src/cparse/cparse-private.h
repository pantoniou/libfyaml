/*
 * cparse-private.h - C declaration front end, private header
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Derived from slimcc (https://github.com/pantoniou/slimcc, itself derived
 * from fuhsnn/slimcc) which is derived from chibicc; see README.md in this
 * directory for what was kept, rewritten and removed.
 *
 * Copyright (c) 2019 Rui Ueyama
 * Copyright (c) 2023-2026 Hsiang-Ying Fu
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CPARSE_PRIVATE_H
#define CPARSE_PRIVATE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libfyaml.h>

#include "fy-utf8.h"

/* internal to the library: nothing declared here is exported */
#pragma GCC visibility push(hidden)

struct cp_tu;

/*
 * Allocation. Everything the front end allocates for a translation unit comes
 * from a libfyaml allocator (struct fy_allocator) under a tag owned by the TU
 * and is released with that tag when the TU is destroyed. There is no per
 * object free and no error path needs to track allocations.
 */
void *cp_alloc(struct cp_tu *tu, size_t size);		/* zeroed */
char *cp_strndup(struct cp_tu *tu, const char *s, size_t len);
char *cp_vformat(struct cp_tu *tu, const char *fmt, va_list ap);
char *cp_format(struct cp_tu *tu, const char *fmt, ...)
	FY_FORMAT(printf, 2, 3);

/* string keyed hashmap, storage from the TU allocator */
struct cp_hashmap_entry {
	const char *key;
	int keylen;
	void *val;
};

struct cp_hashmap {
	struct cp_hashmap_entry *buckets;
	int capacity;
	int used;
};

struct cp_hashmap_entry *cp_hashmap_get_or_insert(struct cp_tu *tu,
		struct cp_hashmap *map, const char *key, int keylen);
void *cp_hashmap_get(struct cp_hashmap *map, const char *key, int keylen);

/* source files */
struct cp_file {
	const char *name;
	const char *real_path;		/* realpath(), NULL if not from a path */
	char *contents;			/* canonicalized, NUL terminated */
	size_t size;
	int file_no;
	int incl_idx;			/* search dir index it was found in, -1 if none */
	bool system_header;
	bool main_file;
};

/* comments, kept on the side; they never enter the token stream */
enum cp_comment_kind {
	CPCK_LINE,			/* // ... */
	CPCK_BLOCK,			/* slash-star ... star-slash */
};

struct cp_comment {
	struct cp_file *file;
	size_t start, end;		/* byte offsets into file->contents, end exclusive */
	int line, end_line;
	int column;			/* 1 based byte column of start */
	enum cp_comment_kind kind;
	bool after_code;		/* a token precedes it on the same line */
};

/* diagnostics; the front end never prints and never exits */
enum cp_diag_severity {
	CPDS_NOTE,
	CPDS_WARNING,
	CPDS_ERROR,
};

struct cp_diag {
	enum cp_diag_severity severity;
	struct cp_file *file;
	int line, column;
	const char *msg;
};

/*
 * The target ABI: what sizes, alignments and layout the declarations get. This
 * is where the target enters; only a few are carried (see cparse-target.c) and
 * the default is the host.
 */
struct cp_type_abi {
	int size, align;
};

enum cp_va_list_kind {
	CPVA_X86_64,			/* struct __va_list_tag[1], 24 bytes */
	CPVA_AARCH64,			/* struct __va_list, 32 bytes */
	CPVA_PTR_VOID,			/* void * */
	CPVA_PTR_CHAR,			/* char * */
	CPVA_ARM,			/* struct __va_list, 4 bytes */
	CPVA_S390X,			/* struct __va_list_tag[1], 32 bytes */
};

struct cp_target {
	const char *name;		/* "x86_64" */
	const char *triple;		/* a canonical triple for it */
	struct cp_type_abi ptr, bool_, short_, int_, long_, llong_, int128, float_, double_,
			   ldouble, float16, float128;
	bool char_unsigned;		/* plain char is unsigned */
	bool has_int128;
	enum cp_va_list_kind va_list;
	/*
	 * Scalars (and arrays of them) wider than this are aligned to it inside a
	 * struct (i386: double, long long and long double are 4); 0 for none.
	 */
	int member_align_cap;
	bool ms_layout;			/* records are laid out the Microsoft way (Windows) */
	bool msvc;			/* MSVC: an enumeration is an int, and an empty record has 4 bytes */
	bool mingw;			/* MinGW: packed does not apply to a record with bit-fields */
	bool unnamed_bitfield_align;	/* an unnamed or zero width bit-field aligns its record to its type */
	int max_vector_align;		/* a vector is aligned to its size up to this; 0 for no limit */
};

/* the ABI of the machine cparse was built for */
const struct cp_target *cp_target_host(void);
/*
 * The architecture of a target, the name of one of the table targets
 * ("x86_64", "i386", "aarch64", "riscv64"). The native target has the one of
 * the machine cparse was built for. NULL if there is none.
 */
const char *cp_target_arch(const struct cp_target *t);
/* the ABI for a target triple (x86_64-*, aarch64-*, riscv64-*, i[3-6]86-*), NULL if not carried */
const struct cp_target *cp_target_by_triple(const char *triple);

/*
 * The target a compiler command line asks for. Arguments that change it are
 * honoured: `-target T`, `--target=T`, `-m32` and `-m64` (the 32 or 64 bit
 * variant of the architecture), `-funsigned-char` and `-fsigned-char`; the rest
 * is ignored. The starting point is `default_triple`, or the host if NULL.
 * Returns 0 and fills *out (copy it, the strings are static), or -1 and a
 * message in *errp for a target that is not carried.
 */
int cp_target_from_args(const char *default_triple, int argc, const char *const *argv,
			struct cp_target *out, const char **errp);

/*
 * The arguments among argv that change the target (the ones above), so that the
 * same ones can be passed to a compiler asked for its predefined macros and
 * include paths. Returns how many were copied into out[].
 */
int cp_target_args_filter(int argc, const char *const *argv, const char **out, int max);

/* #include search path kinds, searched in this order for "file" includes */
enum cp_include_kind {
	CPIK_QUOTE,			/* -iquote: only for "file" includes */
	CPIK_USER,			/* -I */
	CPIK_SYSTEM,			/* -isystem and the default system paths */
};

struct cp_include_path {
	const char *path;
	enum cp_include_kind kind;
};

struct cp_options {
	/*
	 * Allocator for everything the TU allocates; the TU takes its own tag
	 * and releases it on destroy. NULL: the TU creates (and destroys) an
	 * "auto" allocator with per tag freeing.
	 */
	struct fy_allocator *allocator;
	bool std_c23;			/* digit separators in numbers, u8 char literals */
	const struct cp_target *target;	/* NULL: the host; copied by cp_tu_create() */

	/* the lists are copied by cp_preprocess(); they must stay valid until it returns */
	const struct cp_include_path *include_paths;
	size_t num_include_paths;
	/*
	 * Predefined macros, cc -D style: "NAME" (defined as 1), "NAME=VALUE"
	 * and "NAME(args)=VALUE".
	 */
	const char *const *defines;
	size_t num_defines;
	/* an entry "-NAME" undefines NAME (cc -U); the entries apply in order */

	/*
	 * Files read for their macros before the others, cc -imacros style: the
	 * text of the file is dropped. They can define the macros of a compiler that
	 * is not a built-in one. A path is used as it is given.
	 */
	const char *const *macro_files;
	size_t num_macro_files;
	/* files processed before the main file, cc -include style (path as given) */
	const char *const *force_includes;
	size_t num_force_includes;
};

/* tokens */
enum cp_token_kind {
	CPT_IDENT,
	CPT_PUNCT,
	CPT_STR,			/* string literal, raw text (not unescaped) */
	CPT_CHAR,			/* character literal, value in ival */
	CPT_NUM,			/* preprocessing number, raw text */
	CPT_EOF,
};

struct cp_hideset;

struct cp_token {
	struct cp_token *next;
	enum cp_token_kind kind : 16;
	bool at_bol : 1;		/* first token on its line */
	bool has_space : 1;		/* preceded by white space */
	bool eol : 1;			/* last token of a `#pragma` line (see cparse-preprocess.c) */
	const char *loc;		/* points into file->contents (or a TU text buffer) */
	int len;
	struct cp_file *file;		/* NULL for tokens made from text (see origin) */
	int line_no;
	int64_t ival;			/* CPT_CHAR value */
	uint8_t pack;			/* #pragma pack in effect at this token, 0 if none */
	struct cp_hideset *hideset;	/* macros this token must not expand */
	struct cp_token *origin;	/* the macro invocation it came from */
};

/* the token that has a real source location */
static inline const struct cp_token *cp_tok_root(const struct cp_token *tok)
{
	while (tok->origin)
		tok = tok->origin;
	return tok;
}

struct cp_pp;

struct cp_tu {
	struct cp_options opts;
	struct cp_target target_copy;	/* the target is copied, see cp_options */
	const struct cp_target *target;	/* never NULL */
	struct cp_pp *pp;		/* preprocessor state, see cparse-preprocess.c */
	struct fy_allocator *allocator;
	int tag;
	bool own_allocator;

	struct cp_file **files;
	size_t num_files, cap_files;

	struct cp_comment *comments;	/* in source order per file */
	size_t num_comments, cap_comments;

	struct cp_diag *diags;
	size_t num_diags, cap_diags;
	size_t num_errors;

	struct cp_decl **decls;		/* declarations in source order, see cp_parse() */
	size_t num_decls, cap_decls;
	struct cp_type *builtin[32];	/* the unqualified builtin types, made on demand */
};

/* returns NULL on failure (allocator creation) */
struct cp_decl;
struct cp_type;

struct cp_tu *cp_tu_create(const struct cp_options *opts);
void cp_tu_destroy(struct cp_tu *tu);

int cp_tu_comment_add(struct cp_tu *tu, const struct cp_comment *cmt);
int cp_decl_add(struct cp_tu *tu, struct cp_decl *decl);

static inline bool cp_tu_failed(const struct cp_tu *tu)
{
	return tu->num_errors > 0;
}

void cp_diag_vadd(struct cp_tu *tu, enum cp_diag_severity sev,
		  struct cp_file *file, const char *loc, const char *fmt, va_list ap);
void cp_error_at(struct cp_tu *tu, struct cp_file *file, const char *loc,
		 const char *fmt, ...) FY_FORMAT(printf, 4, 5);
void cp_error_tok(struct cp_tu *tu, const struct cp_token *tok,
		  const char *fmt, ...) FY_FORMAT(printf, 3, 4);
void cp_warn_tok(struct cp_tu *tu, const struct cp_token *tok,
		 const char *fmt, ...) FY_FORMAT(printf, 3, 4);

/* 1 based line and column of a location inside a file */
void cp_file_location(const struct cp_file *file, const char *loc,
		      int *linep, int *columnp);

/*
 * Register a file from a buffer (copied; the contents are canonicalized the
 * way a compiler front end would: CRLF, BOM, backslash-newline). Returns NULL
 * on failure with a diagnostic recorded.
 */
struct cp_file *cp_file_add(struct cp_tu *tu, const char *name,
			    const char *contents, size_t size,
			    bool system_header, bool main_file);
struct cp_file *cp_file_add_from_path(struct cp_tu *tu, const char *path,
				      bool system_header, bool main_file);

/*
 * Tokenize a file. Always returns a list terminated by CPT_EOF; on error a
 * diagnostic is recorded (see cp_tu_failed) and the list ends where the
 * tokenizer stopped. Comments are appended to tu->comments.
 */
struct cp_token *cp_tokenize(struct cp_tu *tu, struct cp_file *file);

/*
 * Tokenize a piece of text (macro stringizing and pasting, predefined macros).
 * The tokens have no file; their origin is the (root of the) given token, which
 * may be NULL. Returns a CPT_EOF terminated list, or NULL with a diagnostic
 * if the text is not a clean token sequence (e.g. contains a comment).
 */
struct cp_token *cp_tokenize_text(struct cp_tu *tu, struct cp_token *origin,
				  const char *text, size_t len);

/*
 * Preprocess a file: macros, conditionals and includes are handled. The result
 * is a CPT_EOF terminated list with no directives left. `#pragma pack` is
 * applied as an annotation (token->pack, see below) and other pragmas are
 * dropped. On error a diagnostic is recorded and the list ends where
 * processing stopped.
 */
struct cp_token *cp_preprocess(struct cp_tu *tu, struct cp_file *main_file);

/* integer constant expressions (cparse-expr.c) */
struct cp_value {
	uint64_t val;			/* sign (or zero) extended to 64 bit */
	bool is_unsigned;
	bool is_long;			/* 64 bit (long), else 32 bit (int) */
};

/* what a constant expression needs to know about a type (sizeof, casts) */
struct cp_eval_type {
	int64_t size;
	int align;
	bool is_integer;
	bool is_unsigned;
	int bits;
};

/*
 * Callbacks that let the declaration parser plug its knowledge into the
 * evaluator: enum constants, type names (sizeof, _Alignof, casts) and
 * __builtin_offsetof. Any of them may be NULL.
 */
struct cp_expr_hooks {
	void *ud;
	bool (*is_type_start)(void *ud, struct cp_token *tok);
	/* parse a type-name starting at tok; 0 on success, -1 with a diagnostic */
	int (*parse_type)(void *ud, struct cp_token *tok, struct cp_token **rest,
			  struct cp_eval_type *out);
	/* 0: found, 1: not an enum constant */
	int (*lookup_ident)(void *ud, struct cp_token *tok, struct cp_value *out);
	/* tok is after "__builtin_offsetof(": parse `type, member` up to ')' */
	int (*offsetof_)(void *ud, struct cp_token *tok, struct cp_token **rest,
			 struct cp_value *out);
};

/*
 * Evaluate a constant integer expression starting at tok. On success returns
 * 0 and *rest is the first token after it; on error a diagnostic is recorded
 * and -1 returned.
 */
int cp_eval_int_expr(struct cp_tu *tu, struct cp_token *tok,
		     struct cp_token **rest, struct cp_value *out,
		     const struct cp_expr_hooks *hooks);

/* types and declarations (cparse-type.c, cparse-parse.c) */
enum cp_type_kind {
	CPTK_VOID,
	CPTK_BOOL,
	CPTK_CHAR,			/* plain char */
	CPTK_SCHAR,
	CPTK_UCHAR,
	CPTK_SHORT,
	CPTK_USHORT,
	CPTK_INT,
	CPTK_UINT,
	CPTK_LONG,
	CPTK_ULONG,
	CPTK_LONGLONG,
	CPTK_ULONGLONG,
	CPTK_INT128,
	CPTK_UINT128,
	CPTK_FLOAT,
	CPTK_DOUBLE,
	CPTK_LONGDOUBLE,
	CPTK_FLOAT16,
	CPTK_FLOAT128,
	CPTK_BUILTIN_END,		/* the builtin kinds are below this */
	CPTK_ENUM,
	CPTK_PTR,
	CPTK_ARRAY,
	CPTK_FUNC,
	CPTK_STRUCT,
	CPTK_UNION,
	CPTK_TYPEDEF,			/* sugar: a use of a typedef name */
	CPTK_ELABORATED,		/* sugar: `struct S`, `union U`, `enum E` as written */
	CPTK_COMPLEX,			/* _Complex T, base is T */
	CPTK_VECTOR,			/* __attribute__((vector_size(N))) T */
};

#define CPQ_CONST	1
#define CPQ_VOLATILE	2
#define CPQ_RESTRICT	4
#define CPQ_ATOMIC	8
/* nullability, spelled after the type by clang */
#define CPQ_NONNULL	16
#define CPQ_NULLABLE	32
#define CPQ_NULL_UNSPEC	64

struct cp_field {
	const char *name;		/* NULL for an unnamed bit-field or anonymous record */
	struct cp_type *type;
	int64_t offset;			/* bytes; for a bit-field the storage unit's offset */
	int bit_width;			/* -1: not a bit-field */
	int bit_offset;			/* bits from `offset` */
	int alt_align;			/* aligned attribute, 0 if none */
	bool is_packed;
	bool is_anon_record;		/* anonymous struct/union member */
	struct cp_decl *decl;
};

struct cp_enumerator {
	const char *name;
	int64_t value;
	bool is_unsigned;		/* the value has an unsigned type, value is its bit pattern */
	struct cp_decl *decl;
};

struct cp_type {
	enum cp_type_kind kind;
	unsigned quals;
	bool is_unsigned;		/* builtin integer types */
	struct cp_type *unqual;		/* itself when unqualified; all properties live there */
	struct cp_type *variants;	/* the qualified variants (on the unqualified one) */
	struct cp_type *next_variant;
	struct cp_type *base;		/* PTR pointee, ARRAY element, FUNC return, TYPEDEF/ELABORATED aliased */
	struct cp_type *elaborated;	/* STRUCT/UNION/ENUM: the sugar node, made on demand */
	int64_t size;			/* -1 incomplete */
	int align;
	int64_t count;			/* ARRAY: elements, -1 if unknown */
	const char *name;		/* tag, or typedef name; NULL if anonymous */
	/* an anonymous tag directly named by a typedef: `typedef struct {...} T;` */
	const char *linkage_name;
	struct cp_decl *decl;		/* TYPEDEF: its decl; tags: the definition (else first) decl */
	/* STRUCT/UNION */
	struct cp_field **fields;
	int num_fields;
	bool is_flexible;
	bool complete;
	/* ENUM: base is the underlying integer type */
	struct cp_enumerator **enumerators;
	int num_enumerators;
	/* FUNC */
	struct cp_type **params;
	int num_params;
	bool variadic;
	bool noproto;			/* `()` without a prototype */
	/* ELABORATED */
	enum cp_type_kind keyword;	/* CPTK_STRUCT/UNION/ENUM */
};

enum cp_decl_kind {
	CPDK_STRUCT,
	CPDK_UNION,
	CPDK_ENUM,
	CPDK_TYPEDEF,
	CPDK_FIELD,
	CPDK_ENUMERATOR,
};

struct cp_decl {
	enum cp_decl_kind kind;
	const char *name;		/* NULL if anonymous */
	struct cp_type *type;		/* tag type, the typedef (sugar) type, or the declared type */
	struct cp_decl *parent;		/* tag for a field or enumerator */
	struct cp_file *file;
	size_t start, end;		/* byte offsets into file->contents, end exclusive */
	bool is_definition;		/* a tag with a body */
	int64_t value;			/* enumerator */
	struct cp_field *field;		/* CPDK_FIELD */
	const char *comment;		/* raw attached comment, see cp_attach_comments() */
};

struct cp_type *cp_type_builtin(struct cp_tu *tu, enum cp_type_kind kind);
struct cp_type *cp_type_qualified(struct cp_tu *tu, struct cp_type *ty, unsigned quals);
/* the same type with exactly these qualifiers */
struct cp_type *cp_type_requalified(struct cp_tu *tu, struct cp_type *ty, unsigned quals);
struct cp_type *cp_type_pointer(struct cp_tu *tu, struct cp_type *base);
struct cp_type *cp_type_array(struct cp_tu *tu, struct cp_type *base, int64_t count);
struct cp_type *cp_type_elaborated(struct cp_tu *tu, struct cp_type *tag);
struct cp_type *cp_type_complex(struct cp_tu *tu, struct cp_type *base);
struct cp_type *cp_type_vector(struct cp_tu *tu, struct cp_type *base, int64_t bytes);
/* strip typedef/elaborated sugar and qualifiers */
struct cp_type *cp_type_canonical(const struct cp_type *ty);
int64_t cp_type_size(const struct cp_type *ty);
int cp_type_align(const struct cp_type *ty);
bool cp_type_is_integer(const struct cp_type *ty);
bool cp_type_is_unsigned(const struct cp_type *ty);
/* clang style spelling, allocated from the TU */
const char *cp_type_spelling(struct cp_tu *tu, const struct cp_type *ty);

/*
 * Parse a preprocessed token list for declarations: tags, typedefs, fields
 * and enumerators are appended to tu->decls in source order (function and
 * variable declarations are parsed and skipped; function bodies and
 * initializers are skipped). A declaration that cannot be parsed records an
 * error and parsing resumes at the next one, so one unsupported construct does
 * not lose the rest of the file. Returns the number of errors recorded.
 */
int cp_parse(struct cp_tu *tu, struct cp_token *tok);

/* set decl->comment for the declarations found so far; call after cp_parse() */
int cp_attach_comments(struct cp_tu *tu);

/*
 * Parse a type name (`unsigned long`, `struct foo *[10]`, `const char *const *`)
 * from text. Names are not looked up anywhere: an identifier that is not a
 * keyword is taken to be a typedef name and `struct foo` an incomplete tag, so
 * the caller resolves them (see CPTK_TYPEDEF and CPTK_ELABORATED nodes). Array
 * sizes are constant expressions. Returns NULL with a diagnostic on error.
 */
struct cp_type *cp_parse_type_text(struct cp_tu *tu, const char *text, size_t len);

/* the declaration of `name` of type ty, e.g. `int (*cb)(int)`; name may be NULL or "" */
const char *cp_type_declaration(struct cp_tu *tu, const struct cp_type *ty, const char *name);

static inline bool cp_tok_equal(const struct cp_token *tok, const char *op)
{
	size_t len = strlen(op);

	return (size_t)tok->len == len && !memcmp(tok->loc, op, len);
}

#pragma GCC visibility pop

#endif
