/*
 * libfyaml-reflection.h - Reflection and schema support public API
 * Copyright (c) 2019-2021 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef LIBFYAML_REFLECTION_H
#define LIBFYAML_REFLECTION_H

#include <libfyaml/libfyaml-util.h>
#include <libfyaml/libfyaml-core.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DOC: Reflection — C type introspection and YAML schema support
 *
 * The reflection subsystem extracts type metadata from C header files (via a
 * libclang backend at development time) or from pre-serialised binary blobs
 * (packed backend at deployment time), and uses that metadata to automatically
 * map between YAML documents and C data structures at runtime.
 *
 * **Key types**:
 *
 * - ``struct fy_reflection``  — a type registry loaded from a C header or a
 *   packed blob; owns all type information for a compilation unit
 * - ``struct fy_type_info``   — descriptor for a single C type (struct, union,
 *   enum, typedef, pointer, array, …); covers the full C type system including
 *   bitfields, anonymous types, and qualifiers
 * - ``struct fy_field_info``  — descriptor for a single struct/union field,
 *   including offset, size, bit-width, and YAML metadata annotations extracted
 *   from source comments
 * - ``struct fy_enum_info``   — descriptor for a single enumeration constant
 *
 * **Metadata annotations**: YAML-formatted comments in C source code guide
 * the YAML ↔ C mapping::
 *
 *   struct users {
 *       struct user *list;    // yaml: { counter: count }
 *       int count;
 *   };
 *
 * **Backends**:
 *
 * - *Clang backend* (requires ``--with-libclang`` / libclang installed):
 *   parses C headers at runtime; no pre-processing step needed
 * - *Packed backend*: a self-contained binary blob pre-generated at build
 *   time via ``fy_reflection_export_packed()``; zero runtime dependency on
 *   libclang, suitable for deployment
 * - *Null backend*: stub with no type data, used for testing
 *
 * This interface is experimental and subject to change before version 1.0.
 */

struct fy_type_info;
struct fy_field_info;
struct fy_enum_info;

/* NOTE: order is very important, we rely on ranges for quick computation */

/**
 * enum fy_type_kind - The types of the reflection plumbing
 *
 * @FYTK_INVALID: Invalid type
 *
 * @FYTK_VOID: The void type
 * @FYTK_BOOL: The boolean type
 * @FYTK_CHAR: The native char type
 * @FYTK_SCHAR: The signed char type
 * @FYTK_UCHAR: The unsigned char type
 * @FYTK_SHORT: The signed short type
 * @FYTK_USHORT: The unsigned short type
 * @FYTK_INT: The int type
 * @FYTK_UINT: The unsigned int type
 * @FYTK_LONG: The long type
 * @FYTK_ULONG: The unsigned long type
 * @FYTK_LONGLONG: The long long type
 * @FYTK_ULONGLONG: The unsigned long long type
 * @FYTK_INT128: A signed int 128 bit type (may not be available on all arches)
 * @FYTK_UINT128: An unsigned int 128 bit  type (may not be available on all arches)
 * @FYTK_FLOAT: The float type
 * @FYTK_DOUBLE: The double type
 * @FYTK_LONGDOUBLE: The long double type
 * @FYTK_FLOAT16: A 16 bit float type (may not be available on all arches)
 * @FYTK_FLOAT128: A 128 bit float type (may not be available on all arches)
 *
 * @FYTK_RECORD: A generic record type (not used for C)
 * @FYTK_STRUCT: A struct type
 * @FYTK_UNION: A union type
 *
 * @FYTK_ENUM: An enumeration type
 * @FYTK_TYPEDEF: A typedef type
 * @FYTK_PTR: A pointer type
 * @FYTK_CONSTARRAY: A constant array type
 * @FYTK_INCOMPLETEARRAY: An incomplete array type
 *
 * @FYTK_NULL: The null type
 * @FYTK_FUNCTION: A function type
 *
 */
enum fy_type_kind {
	FYTK_INVALID,

	/* built-in C types (without an explicit size) */
	FYTK_VOID,
	FYTK_BOOL,
	FYTK_CHAR,
	FYTK_SCHAR,
	FYTK_UCHAR,
	FYTK_SHORT,
	FYTK_USHORT,
	FYTK_INT,
	FYTK_UINT,
	FYTK_LONG,
	FYTK_ULONG,
	FYTK_LONGLONG,
	FYTK_ULONGLONG,
	FYTK_INT128,
	FYTK_UINT128,
	FYTK_FLOAT,
	FYTK_DOUBLE,
	FYTK_LONGDOUBLE,
	FYTK_FLOAT16,
	FYTK_FLOAT128,

	/* compound */
	FYTK_RECORD,	/* generic struct, union, class */
	FYTK_STRUCT,
	FYTK_UNION,

	FYTK_ENUM,
	FYTK_TYPEDEF,
	FYTK_PTR,
	FYTK_CONSTARRAY,
	FYTK_INCOMPLETEARRAY,

	FYTK_NULL,
	FYTK_FUNCTION,
};

/* The count of types */
#define FYTK_COUNT 	(FYTK_FUNCTION + 1)
#define FYTK_BITS	5	/* everything fits in 5 bits */
#if FYTK_COUNT > (1 << FYTK_BITS)
#error Bad FYTK_BITS value
#endif

/* the first C primary type */
#define FYTK_PRIMARY_FIRST	FYTK_VOID
/* the last C primary type */
#define FYTK_PRIMARY_LAST	FYTK_FLOAT128
/* the number of C primary types */
#define FYTK_PRIMARY_COUNT	(FYTK_PRIMARY_LAST + 1 - FYTK_PRIMARY_FIRST)

#define FYTK_PRIMARY_BITS	5
#define FYTK_PRIMARY_MAX	(1 << FYTK_PRIMARY_BITS)

/**
 * fy_type_kind_is_valid() - Check type kind for validity
 *
 * Check whether the type kind is valid.
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if valid, false otherwise
 */
static inline bool fy_type_kind_is_valid(enum fy_type_kind type_kind)
{
	return type_kind >= FYTK_VOID && type_kind <= FYTK_FUNCTION;
}

/**
 * fy_type_kind_is_primitive() - Check if it's a primitive type kind
 *
 * Check whether the type kind is for a primitive C type
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if primitive, false otherwise
 */
static inline bool fy_type_kind_is_primitive(enum fy_type_kind type_kind)
{
	return type_kind >= FYTK_VOID && type_kind <= FYTK_FLOAT128;
}

/**
 * fy_type_kind_is_primary() - Check if it's a primary type kind
 *
 * Check whether the type kind is for a primary C type
 * A primary type is is a subset of primitive (without VOID and NULL)
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if primitive, false otherwise
 */
static inline bool fy_type_kind_is_primary(enum fy_type_kind type_kind)
{
	return type_kind >= FYTK_PRIMARY_FIRST && type_kind <= FYTK_PRIMARY_LAST;
}

/**
 * fy_type_kind_is_like_ptr() - Check if it's pointer like type
 *
 * Check whether the type kind matches a pointer like use,
 * which is pointer, constant array or incomplete array.
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if pointer like, false otherwise
 */
static inline bool fy_type_kind_is_like_ptr(enum fy_type_kind type_kind)
{
	return type_kind >= FYTK_PTR && type_kind <= FYTK_INCOMPLETEARRAY;
}

/**
 * fy_type_kind_is_record() - Check if it's a record like type
 *
 * Check whether the type kind contains other types in a record
 * like structure, like a struct or union.
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if record, false otherwise
 */
static inline bool fy_type_kind_is_record(enum fy_type_kind type_kind)
{
	return type_kind >= FYTK_RECORD && type_kind <= FYTK_UNION;
}

/**
 * fy_type_kind_is_numeric() - Check if it's a numeric type
 *
 * Check whether the type kind points to a number, either
 * boolean, integer or float
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if numeric, false otherwise
 */
static inline bool fy_type_kind_is_numeric(enum fy_type_kind type_kind)
{
	return type_kind >= FYTK_BOOL && type_kind <= FYTK_FLOAT128;
}

/**
 * fy_type_kind_is_integer() - Check if it's a numeric integer type
 *
 * Check whether the type kind points to an integer
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if integer, false otherwise
 */
static inline bool fy_type_kind_is_integer(enum fy_type_kind type_kind)
{
	return type_kind >= FYTK_CHAR && type_kind <= FYTK_UINT128;
}

/**
 * fy_type_kind_is_float() - Check if it's a numeric float type
 *
 * Check whether the type kind points to a float
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if float, false otherwise
 */
static inline bool fy_type_kind_is_float(enum fy_type_kind type_kind)
{
	return type_kind >= FYTK_FLOAT && type_kind <= FYTK_FLOAT128;
}

/**
 * fy_type_kind_is_signed() - Check if an integer kind is signed
 *
 * Check whether the type kind is a signed integer.
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if a signed integer, false otherwise
 */
static inline bool fy_type_kind_is_signed(enum fy_type_kind type_kind)
{
	if (!fy_type_kind_is_integer(type_kind))
		return false;

	/* char is signed if the minimum is negative */
	if (type_kind == FYTK_CHAR)
		return CHAR_MIN < 0;

	/* the type kind alternate signed/unsigned starting from FYTK_SCHAR */
	return ((type_kind - FYTK_SCHAR) & 1) == 0;
}

/**
 * fy_type_kind_is_unsigned() - Check if an integer kind is unsigned
 *
 * Check whether the type kind is an unsigned integer
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if an unsigned integer, false otherwise
 */
static inline bool fy_type_kind_is_unsigned(enum fy_type_kind type_kind)
{
	if (!fy_type_kind_is_integer(type_kind))
		return false;

	/* char is unsigned if the minimum is 0 */
	if (type_kind == FYTK_CHAR)
		return CHAR_MIN == 0;

	/* the type kind alternate signed/unsigned starting from FYTK_SCHAR */
	return ((type_kind - FYTK_SCHAR) & 1) != 0;
}

/**
 * fy_type_kind_is_enum_constant_decl() - Check if it's a type that can be an enum
 *
 * Check whether the type kind points to something that is a valid enum constant
 * declaration.
 * For normal cases it's >= int but for weird packed cases can be something smaller.
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if it is a type than can be an enum constant declaration, false otherwise
 */
static inline bool fy_type_kind_is_enum_constant_decl(enum fy_type_kind type_kind)
{
	return type_kind >= FYTK_CHAR && type_kind <= FYTK_ULONGLONG;
}

/**
 * fy_type_kind_has_fields() - Check if the type has fields
 *
 * Check whether the type kind has fields, either if it's a record
 * or an enumeration type.
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if it has fields, false otherwise
 */
static inline bool fy_type_kind_has_fields(enum fy_type_kind type_kind)
{
	return type_kind >= FYTK_STRUCT && type_kind <= FYTK_ENUM;
}

static inline bool fy_type_kind_has_direct_fields(enum fy_type_kind type_kind)
{
	return type_kind >= FYTK_STRUCT && type_kind <= FYTK_UNION;
}

/**
 * fy_type_kind_has_prefix() - Check if the type requires a prefix
 *
 * Check whether the type kind requires a prefix when displayed,
 * ie. like struct union or enum types.
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if it has prefix, false otherwise
 */
static inline bool fy_type_kind_has_prefix(enum fy_type_kind type_kind)
{
	return type_kind >= FYTK_STRUCT && type_kind <= FYTK_ENUM;
}

const char *
fy_type_info_prefixless_name(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_kind_is_dependent() - Check if the type is dependent on another
 *
 * Check whether the type kind is dependent on another, i.e.
 * a typedef. An enum is also dependent because the underlying type
 * matches the range of the enum values.
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if it is dependent, false otherwise
 */
static inline bool fy_type_kind_is_dependent(enum fy_type_kind type_kind)
{
	return type_kind >= FYTK_ENUM && type_kind <= FYTK_INCOMPLETEARRAY;
}

static inline bool fy_type_kind_is_direct_dependent(enum fy_type_kind type_kind)
{
	return type_kind >= FYTK_TYPEDEF && type_kind <= FYTK_INCOMPLETEARRAY;
}

/**
 * fy_type_kind_is_named() - Check if the type is named
 *
 * Check whether the type kind is named, i.e. has a name that uniquely
 * identifies it.
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if it is named, false otherwise
 */
static inline bool fy_type_kind_is_named(enum fy_type_kind type_kind)
{
	return type_kind == FYTK_STRUCT || type_kind == FYTK_UNION ||
	       type_kind == FYTK_TYPEDEF || type_kind == FYTK_ENUM ||
	       type_kind == FYTK_FUNCTION;
}

static inline bool
fy_type_kind_has_element_count(enum fy_type_kind type_kind)
{
	return type_kind == FYTK_CONSTARRAY;
}

/**
 * fy_type_kind_signess() - Find out the type's sign
 *
 * Check how the type deals with signs.
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * -1 signed, 1 unsigned, 0 not relevant for this type
 */
int fy_type_kind_signess(enum fy_type_kind type_kind)
	FY_EXPORT;

/**
 * struct fy_type_kind_info - Information about types
 *
 * @kind: The type's kind id
 * @name: The name of the type (i.e. int, struct)
 * @enum_name: The name of the type_kind enum (for code generation)
 * @size: The size of the type
 * @align: The alignment of the type
 *
 * This structure contains information about each type kind
 * we defined.
 */
struct fy_type_kind_info {
	enum fy_type_kind kind;
	const char *name;
	const char *enum_name;
	size_t size;
	size_t align;
};

/**
 * fy_type_kind_info_get() - Get the type info of a type from it's id
 *
 * Retrieve the type info structure from a type kind id.
 *
 * @type_kind: The type_kind
 *
 * Returns:
 * The info structure that corresponds to the id, or NULL if invalid argument
 */
const struct fy_type_kind_info *
fy_type_kind_info_get(enum fy_type_kind type_kind)
	FY_EXPORT;

/**
 * fy_type_kind_size() - Find out the type kind's size
 *
 * Return the size of the type kind
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * The size of the type kind or 0 on error
 */
size_t
fy_type_kind_size(enum fy_type_kind type_kind)
	FY_EXPORT;

/**
 * fy_type_kind_align() - Find out the type kind's align
 *
 * Return the align of the type kind
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * The align of the type kind or 0 on error
 */
size_t
fy_type_kind_align(enum fy_type_kind type_kind)
	FY_EXPORT;

/**
 * fy_type_kind_name() - Find out the type kind's name
 *
 * Return the name of the type kind
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * The name of the type kind or NULL on error
 */
const char *
fy_type_kind_name(enum fy_type_kind type_kind)
	FY_EXPORT;

/**
 * enum fy_field_info_flags - Flags for a field entry
 *
 * @FYFIF_BITFIELD: Set if the field is a bitfield and not a regular field
 * @FYFIF_ENUM_UNSIGNED: Set if the enum value is unsigned
 */
enum fy_field_info_flags {
	FYFIF_BITFIELD			= FY_BIT(0),
	FYFIF_ENUM_UNSIGNED		= FY_BIT(1),
};

/**
 * struct fy_field_info - Information of a field of a record/enum type
 *
 * @flags: Flags that pertain to this entry
 * @parent: The parent type info
 * @name: The name of the field
 * @type_info: Type of this field
 * @offset: Byte offset if regular field of struct/union
 * @bit_offset: The bit offset of this bit field
 * @bit_width: The bit width of this bit field
 * @uval: The unsigned enum value of this field
 * @sval: The signed enum value of this field
 */
struct fy_field_info {
	enum fy_field_info_flags flags;
	const struct fy_type_info *parent;
	const char *name;
	const struct fy_type_info *type_info;
	union {
		size_t offset;			/* regular field */
		struct {
			size_t bit_offset;	/* bitfield */
			size_t bit_width;
		};
		uintmax_t uval;	/* enum value */
		intmax_t sval;
	};
};

/**
 * enum fy_type_info_flags - Flags for a a type info entry
 *
 * @FYTIF_CONST: Const qualifier for this type enabled
 * @FYTIF_VOLATILE: Volatile qualifier for this type enabled
 * @FYTIF_RESTRICT: Restrict qualified for this type enabled
 * @FYTIF_UNRESOLVED: This type is unresolved (cannot be serialized as is)
 * @FYTIF_MAIN_FILE: The type was declared in the main file of an import
 * @FYTIF_SYSTEM_HEADER: The type was declared in a system header
 * @FYTIF_ANONYMOUS: The type is anonymous, ie. declared in place.
 * @FYTIF_ANONYMOUS_RECORD_DECL: The type is anonymous, and is a record
 * @FYTIF_ANONYMOUS_GLOBAL: The type is a global anonymous type
 * @FYTIF_ANONYMOUS_DEP: The dependent type is anonymous
 * @FYTIF_INCOMPLETE: Incomplete type
 * @FYTIF_ELABORATED: Type is a named type with a qualifier
 */
enum fy_type_info_flags {
	FYTIF_CONST			= FY_BIT(0),
	FYTIF_VOLATILE			= FY_BIT(1),
	FYTIF_RESTRICT			= FY_BIT(2),
	FYTIF_ELABORATED		= FY_BIT(3),	/* an elaborated type */
	FYTIF_ANONYMOUS			= FY_BIT(4),	/* type is anonymous */
	FYTIF_ANONYMOUS_RECORD_DECL	= FY_BIT(5),	/* type is anonymous, and record too */
	FYTIF_ANONYMOUS_GLOBAL		= FY_BIT(6),	/* a global anonymous type */
	FYTIF_ANONYMOUS_DEP		= FY_BIT(7),	/* the dep is anonymous */
	FYTIF_INCOMPLETE		= FY_BIT(8),	/* incomplete type */
	FYTIF_UNRESOLVED		= FY_BIT(9),	/* when pointer is declared but not resolved */
	FYTIF_MAIN_FILE			= FY_BIT(10),	/* type declaration in main file */
	FYTIF_SYSTEM_HEADER		= FY_BIT(11),	/* type declaration in a system header */
};

/**
 * struct fy_type_info - Information of a type
 *
 * @kind: The kind of this type
 * @flags: Flags that pertain to this type
 * @name: The name of the type (including prefix)
 * @size: The size of the type
 * @align: The alignment of the type
 * @dependent_type: The type this one is dependent on (i.e typedef)
 * @count: The number of fields, or the element count for const array
 * @fields: The fields of the type
 */
struct fy_type_info {
	enum fy_type_kind kind;
	enum fy_type_info_flags flags;
	const char *name;				/* includes the prefix i.e. struct foo, enum bar, int, typedef */
	size_t size;
	size_t align;
	const struct fy_type_info *dependent_type;	/* for ptr, typedef, enum and arrays */
	size_t count;					/* for constant arrays, union, struct, enums */
	const struct fy_field_info *fields;
};

/* fwd declaration */
struct fy_reflection;

/**
 * fy_reflection_destroy() - Destroy a reflection
 *
 * Destroy a reflection that was previously created
 *
 * @rfl: The reflection
 *
 */
void
fy_reflection_destroy(struct fy_reflection *rfl)
	FY_EXPORT;

/**
 * fy_reflection_clear_all_markers() - Clear all markers
 *
 * Clear all markers put on types of the reflection
 *
 * @rfl: The reflection
 *
 */
void
fy_reflection_clear_all_markers(struct fy_reflection *rfl)
	FY_EXPORT;

/**
 * fy_reflection_prune_unmarked() - Remove all unmarked types
 *
 * Remove all unmarked type of the reflection.
 *
 * @rfl: The reflection
 */
void
fy_reflection_prune_unmarked(struct fy_reflection *rfl)
	FY_EXPORT;

/**
 * fy_reflection_is_resolved() - Test whether the reflection is resolved.
 *
 * Check whether a reflection is fully resolved, i.e. no types are referring
 * to undefined types.
 *
 * @rfl: The reflection
 *
 * Returns:
 * true if the reflection is resolved, false if there are unresolved references
 */
bool
fy_reflection_is_resolved(struct fy_reflection *rfl)
	FY_EXPORT;

/**
 * fy_reflection_from_imports() - Create a reflection from imports
 *
 * Create a reflection by the imports of the given backend.
 *
 * @backend_name: The name of the backend
 * @backend_cfg: The configuration of the backend
 * @num_imports: The number of imports
 * @import_cfgs: The array of import configs.
 * @diag: The diagnostic object
 *
 * Returns:
 * The reflection pointer, or NULL if an error occured.
 */
struct fy_reflection *
fy_reflection_from_imports(const char *backend_name, const void *backend_cfg,
			   int num_imports, const void *import_cfgs[],
			   struct fy_diag *diag)
	FY_EXPORT;

/**
 * fy_reflection_from_import() - Create a reflection from an import
 *
 * Create a reflection by a single import of the given backend.
 *
 * @backend_name: The name of the backend
 * @backend_cfg: The configuration of the backend
 * @import_cfg: The import configuration
 * @diag: The diagnostic object
 *
 * Returns:
 * The reflection pointer, or NULL if an error occured.
 */
struct fy_reflection *
fy_reflection_from_import(const char *backend_name, const void *backend_cfg,
			  const void *import_cfg, struct fy_diag *diag)
	FY_EXPORT;

/**
 * fy_reflection_from_c_files() - Create a reflection from C files
 *
 * Create a reflection from C source files
 *
 * @filec: Number of files
 * @filev: An array of files
 * @argc: Number of arguments to pass to libclang
 * @argv: Arguments to pass to libclang
 * @display_diagnostics: Display diagnostics (useful in case of errors)
 * @include_comments: Include comments in the type database
 * @diag: The diagnostic object to use (or NULL for default)
 *
 * Returns:
 * The reflection pointer, or NULL if an error occured.
 */
struct fy_reflection *
fy_reflection_from_c_files(int filec, const char * const filev[], int argc, const char * const argv[],
			   bool display_diagnostics, bool include_comments,
			   struct fy_diag *diag)
	FY_EXPORT;

/**
 * fy_reflection_from_c_file() - Create a reflection from a single C file
 *
 * Create a reflection from a single C source file
 *
 * @file: The C file
 * @argc: Number of arguments to pass to libclang
 * @argv: Arguments to pass to libclang
 * @display_diagnostics: Display diagnostics (useful in case of errors)
 * @include_comments: Include comments in the type database
 * @diag: The diagnostic object to use (or NULL for default)
 *
 * Returns:
 * The reflection pointer, or NULL if an error occured.
 */
struct fy_reflection *
fy_reflection_from_c_file(const char *file, int argc, const char * const argv[],
			  bool display_diagnostics, bool include_comments,
			  struct fy_diag *diag)
	FY_EXPORT;

/**
 * fy_reflection_from_c_file_with_cflags() - Create a reflection from a single C file with CFLAGS
 *
 * Create a reflection from a single C source file, using a simpler CFLAGS api
 *
 * @file: The C file
 * @cflags: The C flags
 * @display_diagnostics: Display diagnostics (useful in case of errors)
 * @include_comments: Include comments in the type database
 * @diag: The diagnostic object to use (or NULL for default)
 *
 * Returns:
 * The reflection pointer, or NULL if an error occured.
 */
struct fy_reflection *
fy_reflection_from_c_file_with_cflags(const char *file, const char *cflags,
				      bool display_diagnostics, bool include_comments,
				      struct fy_diag *diag)
	FY_EXPORT;

/**
 * fy_reflection_from_packed_blob() - Create a reflection from a packed blob
 *
 * Create a reflection from a packed blob.
 *
 * @blob: A pointer to the binary blob
 * @blob_size: The size of the blob
 * @diag: The diagnostic object to use (or NULL for default)
 *
 * Returns:
 * The reflection pointer, or NULL if an error occured.
 */
struct fy_reflection *
fy_reflection_from_packed_blob(const void *blob, size_t blob_size, struct fy_diag *diag)
	FY_EXPORT;

/**
 * fy_reflection_to_packed_blob() - Create blob from a reflection
 *
 * Create a packed blob from the given reflection
 *
 * @rfl: The reflection
 * @blob_sizep: Pointer to a variable to store the generated blobs size
 * @include_comments: Include comments in the blob
 * @include_location: Include the location information in the blob
 *
 * Returns:
 * A pointer to the blob, or NULL in case of an error
 */
void *
fy_reflection_to_packed_blob(struct fy_reflection *rfl, size_t *blob_sizep,
			     bool include_comments, bool include_location)
	FY_EXPORT;

/**
 * fy_reflection_from_packed_blob_file() - Create a reflection from a packed blob file
 *
 * Create a reflection from the given packed blob file
 *
 * @blob_file: The name of the blob file
 * @diag: The diagnostic object to use (or NULL for default)
 *
 * Returns:
 * The reflection pointer, or NULL if an error occured.
 */
struct fy_reflection *
fy_reflection_from_packed_blob_file(const char *blob_file, struct fy_diag *diag)
	FY_EXPORT;

/**
 * fy_reflection_to_packed_blob_file() - Create a packed blob file from reflection
 *
 * Create a packed blob file from the given reflection
 *
 * @rfl: The reflection
 * @blob_file: The name of the blob file
 *
 * Returns:
 * 0 on success, -1 on error
 */
int
fy_reflection_to_packed_blob_file(struct fy_reflection *rfl, const char *blob_file)
	FY_EXPORT;

/**
 * fy_reflection_from_null() - Create a reflection for C basic types only
 *
 * Create a reflection using only C basic types
 *
 * @diag: The diagnostic object to use (or NULL for default)
 *
 * Returns:
 * The reflection pointer, or NULL if an error occured.
 */
struct fy_reflection *
fy_reflection_from_null(struct fy_diag *diag)
	FY_EXPORT;

/**
 * fy_reflection_set_userdata() - Set the userdata of a reflection
 *
 * Set the user data associated with the reflection
 *
 * @rfl: The reflection
 * @userdata: A void pointer that can be used to retreive the data
 */
void
fy_reflection_set_userdata(struct fy_reflection *rfl, void *userdata)
	FY_EXPORT;

/**
 * fy_reflection_get_userdata() - Get the userdata associated with a reflection
 *
 * Retrieve the user data associated with the given type via a
 * previous call to fy_reflection_set_userdata().
 *
 * @rfl: The reflection
 *
 * Returns:
 * The userdata associated with the reflection, or NULL on error
 */
void *
fy_reflection_get_userdata(struct fy_reflection *rfl)
	FY_EXPORT;

/**
 * fy_type_info_iterate() - Iterate over the types of the reflection
 *
 * This method iterates over all the types of a reflection.
 * The start of the iteration is signalled by a NULL in \*prevp.
 *
 * @rfl: The reflection
 * @prevp: The previous type sequence iterator
 *
 * Returns:
 * The next type in sequence or NULL at the end of the type sequence.
 */
const struct fy_type_info *
fy_type_info_iterate(struct fy_reflection *rfl, void **prevp)
	FY_EXPORT;

/**
 * fy_type_info_reverse_iterate() - Iterate over the types of the reflection in reverse
 *
 * This method iterates over all the types of a reflection in reverse.
 * The start of the iteration is signalled by a NULL in \*prevp.
 *
 * @rfl: The reflection
 * @prevp: The previous type sequence iterator
 *
 * Returns:
 * The next type in sequence or NULL at the end of the type sequence.
 */
const struct fy_type_info *
fy_type_info_reverse_iterate(struct fy_reflection *rfl, void **prevp)
	FY_EXPORT;

/**
 * fy_type_info_with_qualifiers() - Get type info with specified qualifiers
 *
 * Return a type info with the specified qualifier flags applied.
 * This allows you to get const, volatile, or other qualified versions
 * of a type.
 *
 * @ti: The type info
 * @qual_flags: The qualifier flags to apply
 *
 * Returns:
 * The type info with qualifiers applied, or NULL on error
 */
const struct fy_type_info *
fy_type_info_with_qualifiers(const struct fy_type_info *ti, enum fy_type_info_flags qual_flags)
	FY_EXPORT;

/**
 * fy_type_info_unqualified() - Get unqualified type info
 *
 * Return the unqualified version of a type (removing const, volatile, etc.)
 *
 * @ti: The type info
 *
 * Returns:
 * The unqualified type info, or NULL on error
 */
const struct fy_type_info *
fy_type_info_unqualified(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_info_to_reflection() - Get the reflection a type belongs to
 *
 * Return the reflection this type belongs to
 *
 * @ti: The type info
 *
 * Returns:
 * The reflection this type belongs to, or NULL if bad ti argument
 */
struct fy_reflection *
fy_type_info_to_reflection(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_info_generate_name() - Generate a name for a type
 *
 * Generate a name from the type by traversing the type definitions
 * down to their dependent primitive types.
 *
 * @ti: The type info
 * @field: The field if using the call to generate a field definition
 *         or NULL if not.
 *
 * Returns:
 * A malloc()'ed pointer to the name, or NULL in case of an error.
 * This pointer must be free()'d when the caller is done with it.
 */
char *
fy_type_info_generate_name(const struct fy_type_info *ti, const char *field)
	FY_EXPORT;

/**
 * fy_field_info_generate_name() - Generate a name for a field
 *
 * Generate a name for a field by combining the field's name with its type.
 * Similar to fy_type_info_generate_name() but specifically for fields.
 *
 * @fi: The field info
 *
 * Returns:
 * A malloc()'ed pointer to the name, or NULL in case of an error.
 * This pointer must be free()'d when the caller is done with it.
 */
char *
fy_field_info_generate_name(const struct fy_field_info *fi)
	FY_EXPORT;

/**
 * fy_type_name_normalize() - Normalize a C type name
 *
 * Normalize a type name by removing superfluous whitespace, converting
 * it to a format that is suitable for type name comparison.
 * Note that no attempt is made to verify that the type name is a valid
 * C one, so caller beware.
 *
 * @kind: The kind of the type
 * @type_name: The type name to normalize
 *
 * Returns:
 * A malloc()'ed pointer to the normalized name, or NULL in case of an error.
 * This pointer must be free()'d when the caller is done with it.
 */
char *
fy_type_name_normalize(enum fy_type_kind kind, const char *type_name)
	FY_EXPORT;

/**
 * fy_type_info_lookup() - Lookup for a type by a definition
 *
 * Lookup for a type using the name provided. Any primitive types
 * or derived types will be created as appropriately.
 *
 * @rfl: The reflection
 * @name: The name of type (i.e. "struct foo")
 *
 * Returns:
 * A pointer to the type or NULL if it cannot be found.
 */
const struct fy_type_info *
fy_type_info_lookup(struct fy_reflection *rfl, const char *name)
	FY_EXPORT;

/**
 * fy_type_info_clear_marker() - Clear the marker on a type
 *
 * Clear the marker on a type. Note this call will not clear the
 * markers of the dependent types.
 *
 * @ti: The type info
 */
void
fy_type_info_clear_marker(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_info_mark() - Mark a type and it's dependencies
 *
 * Mark the type and recursively mark all types this one depends on.
 *
 * @ti: The type info
 */
void
fy_type_info_mark(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_info_is_marked() - Check whether a type is marked
 *
 * Check the mark of a type
 *
 * @ti: The type info
 *
 * Returns:
 * true if the type is marked, false otherwise
 */
bool
fy_type_info_is_marked(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_info_eponymous_offset() - Offset of an anonymous type from the closest eponymous parent type.
 *
 * For anonymous types, get the offset from the start of the enclosing
 * eponymous type. For example::
 *
 *   struct baz {
 *     int foo;
 *     struct {	// <- anonymous
 *       int bar; // <- offset from baz
 *     } bar;
 *   };
 *
 * @ti: The anonymous type
 *
 * Returns:
 * The offset from the closest eponymous parent type or 0 if not anonymous
 */
size_t
fy_type_info_eponymous_offset(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_info_get_comment() - Get the comment for a type
 *
 * Retrieve the 'cooked' comment for a type. The cooking consists of
 * (trying) to remove comment formatting. For example::
 *
 *   // this is a comment
 *   //   which requires cooking
 *
 * Would be cooked as::
 *
 *   this is a comment
 *     which requires cooking
 *
 * @ti: The type info
 *
 * Returns:
 * The cooked comment, or NULL
 */
const char *
fy_type_info_get_comment(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_field_info_get_comment() - Get the comment for a field
 *
 * Retrieve the 'cooked' comment for a field. The cooking consists of
 * (trying) to remove comment formatting. For example::
 *
 *   // this is a comment
 *   //   which requires cooking
 *
 * Would be cooked as::
 *
 *   this is a comment
 *     which requires cooking
 *
 * @fi: The field info
 *
 * Returns:
 * The cooked comment, or NULL
 */
const char *
fy_field_info_get_comment(const struct fy_field_info *fi)
	FY_EXPORT;

/**
 * fy_type_info_get_yaml_annotation() - Get the yaml annotation of this type
 *
 * Retrieve a document containing the yaml keyword annotations of this type
 *
 * @ti: The type info
 *
 * Returns:
 * The yaml annotation document or NULL
 */
struct fy_document *
fy_type_info_get_yaml_annotation(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_info_get_yaml_comment() - Get the yaml annotation of this type as string
 *
 * Retrieve a document containing the yaml keyword annotations of this type as a string
 *
 * @ti: The type info
 *
 * Returns:
 * The yaml comment or NULL
 */
const char *
fy_type_info_get_yaml_comment(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_info_get_id() - Get the unique ID of a type
 *
 * Retrieve the unique identifier for this type within its reflection context.
 * Each type in a reflection has a distinct ID.
 *
 * @ti: The type info
 *
 * Returns:
 * The type ID if >= 0, -1 on error
 */
int
fy_type_info_get_id(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_field_info_get_yaml_annotation() - Get the yaml annotation document for this field
 *
 * Retrieve a document containing the yaml keyword annotations of this field
 *
 * @fi: The field info
 *
 * Returns:
 * The yaml annotation document, or NULL
 */
struct fy_document *
fy_field_info_get_yaml_annotation(const struct fy_field_info *fi)
	FY_EXPORT;

/**
 * fy_field_info_get_yaml_comment() - Get the YAML comment for this field
 *
 * Retrieve the YAML comment associated with this field from its annotations.
 *
 * @fi: The field info
 *
 * Returns:
 * The YAML comment string, or NULL if no comment exists
 */
const char *
fy_field_info_get_yaml_comment(const struct fy_field_info *fi)
	FY_EXPORT;

/**
 * fy_reflection_dump() - Dump internal type database
 *
 * @rfl: The reflection
 * @marked_only: Dump marked structures only
 * @no_location: Do not display location information
 */
void fy_reflection_dump(struct fy_reflection *rfl, bool marked_only, bool no_location)
	FY_EXPORT;

/* flags for generation */
#define FYCGF_INDENT_SHIFT	0
#define FYCGF_INDENT_WIDTH	4
#define FYCGF_INDENT(x)		(((unsigned int)(x) & ((1 << FYCGF_INDENT_WIDTH) - 1)) << FYCGF_INDENT_SHIFT)
#define FYCGF_INDENT_MASK	FYCGF_INDENT((1U << FYCGF_INDENT_WIDTH) - 1)

#define FYCGF_COMMENT_SHIFT	4
#define FYCGF_COMMENT_WIDTH	3
#define FYCGF_COMMENT(x)	(((unsigned int)(x) & ((1 << FYCGF_COMMENT_WIDTH) - 1)) << FYCGF_COMMENT_SHIFT)
#define FYCGF_COMMENT_MASK	FYCGF_COMMENT((1U << FYCGF_COMMENT_WIDTH) - 1)

enum fy_c_generation_flags {
	FYCGF_INDENT_TAB	= FYCGF_INDENT(0),
	FYCGF_INDENT_SPACES_2	= FYCGF_INDENT(2),
	FYCGF_INDENT_SPACES_4	= FYCGF_INDENT(4),
	FYCGF_INDENT_SPACES_8	= FYCGF_INDENT(8),
	FYCGF_COMMENT_NONE	= FYCGF_COMMENT(0),
	FYCGF_COMMENT_RAW	= FYCGF_COMMENT(1),
	FYCGF_COMMENT_YAML	= FYCGF_COMMENT(2),
};

/**
 * fy_reflection_generate_c() - Generate C code from reflection type information
 *
 * Generate C code (struct definitions, typedefs, etc.) from the reflection's
 * type database and write it to a file pointer. This can be used to recreate
 * C header files from reflection metadata. The output format is controlled by
 * the flags parameter which specifies indentation style and comment format.
 *
 * @rfl: The reflection
 * @flags: Generation flags controlling output format (indentation, comments)
 * @fp: The file pointer to write to
 *
 * Returns:
 * 0 on success, -1 on error
 */
int fy_reflection_generate_c(struct fy_reflection *rfl, enum fy_c_generation_flags flags, FILE *fp)
	FY_EXPORT;

/**
 * fy_reflection_generate_c_string() - Generate C code from reflection as a string
 *
 * Generate C code (struct definitions, typedefs, etc.) from the reflection's
 * type database and return it as an allocated string. This is similar to
 * fy_reflection_generate_c() but returns the result as a string instead of
 * writing to a file. The output format is controlled by the flags parameter.
 *
 * @rfl: The reflection
 * @flags: Generation flags controlling output format (indentation, comments)
 *
 * Returns:
 * A malloc()'ed pointer to the generated C code, or NULL on error.
 * This pointer must be free()'d when the caller is done with it.
 */
char *fy_reflection_generate_c_string(struct fy_reflection *rfl, enum fy_c_generation_flags flags)
	FY_EXPORT;

/**
 * fy_field_info_index() - Get the index of a field of a type
 *
 * Retrieve the 0-based index of a field info. The first
 * structure member is 0, the second 1 etc.
 *
 * @fi: The pointer to the field info
 *
 * Returns:
 * The index of the field if >= 0, -1 on error
 */
int
fy_field_info_index(const struct fy_field_info *fi)
	FY_EXPORT;

/**
 * fy_type_info_lookup_field() - Lookup a field of a type by name
 *
 * Lookup the field with the given name on the given type.
 *
 * @ti: The pointer to the type info
 * @name: The name of the field
 *
 * Returns:
 * A pointer to the field info if field was found, NULL otherwise
 */
const struct fy_field_info *
fy_type_info_lookup_field(const struct fy_type_info *ti, const char *name)
	FY_EXPORT;

/**
 * fy_type_info_lookup_field_by_enum_value() - Lookup an enum field of a type by value
 *
 * Lookup the field with the enum value on the given type.
 *
 * @ti: The pointer to the type info
 * @val: The value of the enumeration
 *
 * Returns:
 * A pointer to the field info if field was found, NULL otherwise
 */
const struct fy_field_info *
fy_type_info_lookup_field_by_enum_value(const struct fy_type_info *ti, intmax_t val)
	FY_EXPORT;

/**
 * fy_type_info_lookup_field_by_unsigned_enum_value() - Lookup an enum field of a type by unsigned 0value
 *
 * Lookup the field with the enum value on the given type.
 *
 * @ti: The pointer to the type info
 * @val: The value of the enumeration
 *
 * Returns:
 * A pointer to the field info if field was found, NULL otherwise
 */
const struct fy_field_info *
fy_type_info_lookup_field_by_unsigned_enum_value(const struct fy_type_info *ti, uintmax_t val)
	FY_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* LIBFYAML_REFLECTION_H */
