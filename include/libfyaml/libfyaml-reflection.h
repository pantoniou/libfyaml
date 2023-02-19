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
 * the YAML <-> C mapping::
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

/**
 * fy_type_kind_has_direct_fields() - Check if the type has directly addressable fields
 *
 * Check whether the type kind is a struct or union — types whose fields
 * can be addressed by offset without going through a dependent type.
 * This is a strict subset of fy_type_kind_has_fields(), excluding enums.
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if it has direct fields (struct or union), false otherwise
 */
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

/**
 * fy_type_info_prefixless_name() - Get the name of a type without its keyword prefix
 *
 * Return the type's name with any leading keyword prefix removed.
 * For example, ``"struct foo"`` becomes ``"foo"`` and ``"enum bar"``
 * becomes ``"bar"``.  For types that carry no prefix (e.g. ``"int"``) the
 * returned pointer is the same as ``ti->name``.
 *
 * @ti: The type info
 *
 * Returns:
 * A pointer into ti->name past the prefix, or NULL on error.
 * The lifetime of the returned pointer matches that of @ti.
 */
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

/**
 * fy_type_kind_is_direct_dependent() - Check if the type is directly dependent on another
 *
 * Check whether the type kind has a single, directly referenced dependent type
 * (i.e. the ``dependent_type`` field of ``struct fy_type_info`` is meaningful).
 * This covers typedefs, pointers, and arrays, but not enums — enums have a
 * dependent type that represents their underlying integer range, but their
 * constants are named independently rather than being a direct alias.
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if directly dependent (typedef, ptr, constarray, incompletearray), false otherwise
 */
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

/**
 * fy_type_kind_has_element_count() - Check if the type carries a fixed element count
 *
 * Check whether the type kind is a constant-size array, i.e. one whose
 * element count is known at compile time and stored in ``fy_type_info::count``.
 * Incomplete arrays (``[]``) have an unknown size at declaration and return false.
 *
 * @type_kind: The type_kind to check
 *
 * Returns:
 * true if the type is a FYTK_CONSTARRAY, false otherwise
 */
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
 * DOC: fy_field_info - Opaque field descriptor
 *
 * ``struct fy_field_info`` describes a single struct/union field or enum
 * constant. Use the ``fy_field_info_get_*()`` accessor functions to inspect
 * it.
 */
struct fy_field_info;

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
 * DOC: fy_type_info - Opaque type descriptor
 *
 * ``struct fy_type_info`` describes a single C type (struct, union, enum,
 * typedef, pointer, array, or primitive). Use the
 * ``fy_type_info_get_*()`` accessor functions to inspect it.
 */
struct fy_type_info;

/**
 * fy_type_info_get_kind() - Get the type kind
 * @ti: The type info
 * Returns: The kind of this type (e.g. FYTK_STRUCT, FYTK_INT, …)
 */
enum fy_type_kind
fy_type_info_get_kind(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_info_get_flags() - Get the type info flags
 * @ti: The type info
 * Returns: Bitfield of ``enum fy_type_info_flags`` for this type
 */
enum fy_type_info_flags
fy_type_info_get_flags(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_info_get_name() - Get the fully qualified name of the type
 *
 * Returns the name including any keyword prefix, e.g. ``"struct foo"``,
 * ``"enum bar"``, ``"int"``, ``"unsigned long"``.
 *
 * @ti: The type info
 * Returns: The name string, or NULL on error
 */
const char *
fy_type_info_get_name(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_info_get_size() - Get the byte size of the type
 * @ti: The type info
 * Returns: sizeof the type in bytes, or 0 for incomplete/void types
 */
size_t
fy_type_info_get_size(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_info_get_align() - Get the alignment requirement of the type
 * @ti: The type info
 * Returns: alignof the type in bytes
 */
size_t
fy_type_info_get_align(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_info_get_dependent_type() - Get the type this one depends on
 *
 * For pointers, typedefs, enums (underlying integer type), and arrays
 * (element type) returns the referenced type.  NULL for all others.
 *
 * @ti: The type info
 * Returns: The dependent type, or NULL if not applicable
 */
const struct fy_type_info *
fy_type_info_get_dependent_type(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_info_get_count() - Get the field count or array element count
 *
 * For struct/union/enum types this is the number of fields/constants.
 * For FYTK_CONSTARRAY this is the fixed element count.
 * Zero for all other types.
 *
 * @ti: The type info
 * Returns: The count
 */
size_t
fy_type_info_get_count(const struct fy_type_info *ti)
	FY_EXPORT;

/**
 * fy_type_info_get_field_at() - Get a field of a type by index
 *
 * Return the Nth field (0-based) of a struct, union, or enum type.
 *
 * @ti:  The type info
 * @idx: Field index (0-based)
 * Returns: Pointer to the field info, or NULL if idx is out of range
 */
const struct fy_field_info *
fy_type_info_get_field_at(const struct fy_type_info *ti, size_t idx)
	FY_EXPORT;

/**
 * fy_field_info_get_flags() - Get the field flags
 * @fi: The field info
 * Returns: Bitfield of ``enum fy_field_info_flags`` for this field
 */
enum fy_field_info_flags
fy_field_info_get_flags(const struct fy_field_info *fi)
	FY_EXPORT;

/**
 * fy_field_info_get_parent() - Get the type that contains this field
 * @fi: The field info
 * Returns: The enclosing struct/union/enum type info, or NULL on error
 */
const struct fy_type_info *
fy_field_info_get_parent(const struct fy_field_info *fi)
	FY_EXPORT;

/**
 * fy_field_info_get_name() - Get the name of the field
 * @fi: The field info
 * Returns: The field name string, or NULL on error
 */
const char *
fy_field_info_get_name(const struct fy_field_info *fi)
	FY_EXPORT;

/**
 * fy_field_info_get_type_info() - Get the type of this field's value
 * @fi: The field info
 * Returns: The type info for the field's declared type, or NULL on error
 */
const struct fy_type_info *
fy_field_info_get_type_info(const struct fy_field_info *fi)
	FY_EXPORT;

/**
 * fy_field_info_get_offset() - Get the byte offset of a regular struct/union field
 *
 * Valid only when ``!(fy_field_info_get_flags(fi) & FYFIF_BITFIELD)``.
 *
 * @fi: The field info
 * Returns: Byte offset from the start of the enclosing struct/union
 */
size_t
fy_field_info_get_offset(const struct fy_field_info *fi)
	FY_EXPORT;

/**
 * fy_field_info_get_bit_offset() - Get the bit offset of a bitfield
 *
 * Valid only when ``fy_field_info_get_flags(fi) & FYFIF_BITFIELD``.
 *
 * @fi: The field info
 * Returns: Bit offset from the start of the enclosing struct/union
 */
size_t
fy_field_info_get_bit_offset(const struct fy_field_info *fi)
	FY_EXPORT;

/**
 * fy_field_info_get_bit_width() - Get the width of a bitfield in bits
 *
 * Valid only when ``fy_field_info_get_flags(fi) & FYFIF_BITFIELD``.
 *
 * @fi: The field info
 * Returns: Width of the bitfield in bits
 */
size_t
fy_field_info_get_bit_width(const struct fy_field_info *fi)
	FY_EXPORT;

/**
 * fy_field_info_get_enum_value() - Get the signed value of an enum constant
 *
 * Valid only for fields of an enum type info.
 *
 * @fi: The field info
 * Returns: The signed enum constant value
 */
intmax_t
fy_field_info_get_enum_value(const struct fy_field_info *fi)
	FY_EXPORT;

/**
 * fy_field_info_get_unsigned_enum_value() - Get the unsigned value of an enum constant
 *
 * Valid only for fields of an enum type info where
 * ``fy_field_info_get_flags(fi) & FYFIF_ENUM_UNSIGNED``.
 *
 * @fi: The field info
 * Returns: The unsigned enum constant value
 */
uintmax_t
fy_field_info_get_unsigned_enum_value(const struct fy_field_info *fi)
	FY_EXPORT;

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
 * fy_reflection_dump() - Dump the internal type database to stderr
 *
 * Print a human-readable description of every type (or only the marked types)
 * in the reflection's registry to stderr.  Primarily a debugging aid.
 *
 * @rfl:          The reflection to dump
 * @marked_only:  If true, dump only types that have been marked
 *                (see fy_type_info_mark()); if false, dump all types
 * @no_location:  If true, omit source file/line location information
 *                from the output
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

/**
 * enum fy_c_generation_flags - Flags controlling C code generation output format
 *
 * Pass a bitwise OR of one indentation choice and one comment choice to
 * fy_reflection_generate_c() or fy_reflection_generate_c_string().
 * The default combination is %FYCGF_INDENT_TAB | %FYCGF_COMMENT_NONE.
 *
 * @FYCGF_INDENT_TAB:      Use a hard tab character for each indentation level
 * @FYCGF_INDENT_SPACES_2: Use two spaces per indentation level
 * @FYCGF_INDENT_SPACES_4: Use four spaces per indentation level
 * @FYCGF_INDENT_SPACES_8: Use eight spaces per indentation level
 * @FYCGF_COMMENT_NONE: Omit all comments from the generated output
 * @FYCGF_COMMENT_RAW:  Emit the original raw source comment verbatim
 * @FYCGF_COMMENT_YAML: Emit only the ``yaml:`` annotation portion of the
 *                      comment, formatted as a single-line ``//`` comment
 */
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

/**
 * DOC: YAML metadata annotations
 *
 * Metadata annotations are YAML-formatted comments placed on C struct/union
 * fields or on the type definitions themselves.  They guide the meta-type
 * system when mapping between YAML documents and C data structures.
 *
 * Annotation syntax::
 *
 *   // yaml: { <key>: <value>, ... }
 *
 * The comment must appear on the same line as (or on the line immediately
 * before) the field or type definition.  Multiple key/value pairs may appear
 * in one annotation.
 *
 * Example::
 *
 *   struct foo {
 *       char *key;
 *       int   value;
 *   };
 *
 *   struct container {
 *       int        count;
 *       struct foo *items;   // yaml: { key: key, counter: count }
 *   };
 *
 * Annotations placed on the entry type (or supplied via ``entry_meta`` in
 * ``struct fy_type_context_cfg``) govern root-level behaviour.
 *
 *
 * **Boolean annotations**
 *
 * All boolean keys default to ``false`` unless noted.
 *
 * ``required`` (default: ``true``)
 *   Field is mandatory; a parse error is raised if it is absent from the
 *   input YAML.  Set to ``false`` to make a field optional.
 *
 * ``omit-on-emit``
 *   Skip this field entirely during emission; it will not appear in the
 *   generated YAML output regardless of its value.
 *
 * ``omit-if-empty``
 *   Omit the field during emission when its value is empty: a NULL or
 *   zero-length string, or a sequence/array with no elements.
 *
 * ``omit-if-default``
 *   Omit the field during emission when its value equals the ``default``
 *   annotation value.  Must be used together with the ``default`` key.
 *
 * ``omit-if-null``
 *   Omit the field during emission when its C value is a NULL pointer.
 *
 * ``match-null``
 *   Accept YAML ``null`` as a valid input value for this type or field.
 *
 * ``match-seq``
 *   Accept a YAML sequence node as valid input.  Primarily used on union
 *   variant arms to express that the arm matches sequences.
 *
 * ``match-map``
 *   Accept a YAML mapping node as valid input.
 *
 * ``match-scalar``
 *   Accept a YAML scalar node as valid input.
 *
 * ``match-always``
 *   Accept any YAML node type; used as a wildcard "catch-all" arm in union
 *   discriminator chains.
 *
 * ``not-null-terminated``
 *   The string field is stored as raw bytes without a trailing NUL; length
 *   is tracked by the companion ``counter`` field.  Mutually exclusive with
 *   normal C-string handling.
 *
 * ``not-string``
 *   Treat the field as non-string data even though its C type is ``char *``.
 *   Prevents the serdes engine from applying string-specific behaviour.
 *
 * ``null-allowed``
 *   Permit NULL pointer values in this field during both parse and emit;
 *   without this flag a NULL from a non-optional field is treated as an error.
 *
 * ``field-auto-select``
 *   On union types: automatically choose the active union arm by probing each
 *   arm's ``match-*`` flags against the actual YAML node type, without
 *   requiring an explicit ``selector`` field.
 *
 * ``flatten-field-first-anonymous``
 *   When flattening an anonymous sub-struct into the parent mapping, emit
 *   the first anonymous field before any named fields (i.e., keep it first).
 *
 * ``skip-unknown``
 *   On struct types: silently ignore mapping keys that have no corresponding
 *   C field, instead of raising a parse error.  Useful for forward-compatible
 *   schemas.
 *
 * ``enum-or-seq``
 *   On enum types: allow the YAML input to be either a scalar (single enum
 *   value) or a sequence of scalars (set of values combined with OR).
 *
 *
 * **String annotations**
 *
 * ``counter: <field-name>``
 *   Names the sibling field that holds the element count of a variable-length
 *   C array.  The counter field must be an integer type.  During parse the
 *   library writes the parsed length into that field; during emit it reads
 *   the count from it::
 *
 *     struct buf {
 *         uint8_t *data;    // yaml: { counter: len }
 *         size_t   len;
 *     };
 *
 * ``key: <field-name>``
 *   Names the field within each array element that becomes the YAML mapping
 *   key when the array is emitted as a YAML mapping (and parsed back from
 *   one).  Requires ``counter`` to also be set::
 *
 *     struct entry {
 *         char *id;
 *         int   value;
 *     };
 *     struct table {
 *         struct entry *rows;   // yaml: { key: id, counter: count }
 *         int           count;
 *     };
 *
 * ``selector: <field-name>``
 *   On union types: names the field that acts as the discriminator.  The
 *   field's value is compared against each arm's ``select`` annotation to
 *   choose the active arm.
 *
 * ``name: <yaml-key>``
 *   Override the YAML key name used for this field.  By default the C field
 *   name is used.  Useful when the desired YAML key is a C reserved word or
 *   follows a different naming convention::
 *
 *     int type_id;   // yaml: { name: type }
 *
 * ``remove-prefix: <prefix>``
 *   Strip the given prefix from all field names before using them as YAML
 *   keys.  Applied to every field in the annotated struct::
 *
 *     struct s {     // yaml: { remove-prefix: s_ }
 *         int s_foo;
 *         int s_bar;
 *     };
 *   Emits as ``{ foo: ..., bar: ... }``.
 *
 * ``flatten-field: <field-name>``
 *   Inline (flatten) the sub-struct named by the annotation into the parent
 *   YAML mapping.  The sub-struct's own fields appear as direct keys of the
 *   parent mapping instead of under a nested key.
 *
 *
 * **"Any" annotations** (accept arbitrary YAML values)
 *
 * ``terminator: <value>``
 *   Alternative to ``counter`` for variable-length arrays: the array ends
 *   at the first element whose value equals ``terminator``.  The terminator
 *   element itself is not emitted.  Common use is ``0``-terminated arrays::
 *
 *     int *ids;   // yaml: { terminator: 0 }
 *
 * ``default: <value>``
 *   Default value for the field.  During parse, if the field is absent from
 *   the input and the field is not ``required``, the default is used.
 *   During emit, ``omit-if-default`` compares the live value against this.
 *   The value is parsed as the field's own C type::
 *
 *     int port;   // yaml: { default: 8080, omit-if-default: true }
 *
 * ``select: <value>``
 *   The discriminator value that causes this union arm to be selected when
 *   the parent struct's ``selector`` field matches.  Works together with the
 *   ``selector`` string annotation on the enclosing union type::
 *
 *     union payload {       // yaml: { selector: kind }
 *         int    as_int;    // yaml: { select: 0 }
 *         char  *as_str;    // yaml: { select: 1 }
 *     };
 *
 * ``fill: <value>``
 *   Default fill value for uninitialised or sparse sequence elements.  When
 *   the input sequence is shorter than expected, remaining elements are
 *   filled with this value rather than left as zero/NULL.
 */

/**
 * DOC: Meta-type system — C type + annotation = typed serdes node
 *
 * The meta-type system combines a raw C type (``struct fy_type_info``) with
 * a YAML annotation document to produce a *meta type*: an object that knows
 * both the C layout of a type and how to map it to/from YAML.  Two fields
 * with the same C type but different annotations are different meta types.
 *
 * **Key types**:
 *
 * - ``struct fy_type_context`` — owns all meta types for one entry point;
 *   created from a ``struct fy_reflection *`` + an entry type name
 * - ``struct fy_meta_type`` — one C type + annotation pair; describes how a
 *   type is serialised/deserialised
 * - ``struct fy_meta_field`` — one field within a meta type (field_info + meta)
 *
 * **Lifecycle**::
 *
 *   struct fy_type_context_cfg cfg = {
 *       .rfl        = rfl,
 *       .entry_type = "struct server_config",
 *   };
 *   struct fy_type_context *ctx = fy_type_context_create(&cfg);
 *
 *   // parse YAML into a C struct (allocates; caller must free via
 *   // fy_type_context_free_data):
 *   void *data = NULL;
 *   fy_type_context_parse(ctx, fyp, &data);
 *
 *   // emit back to YAML:
 *   fy_type_context_emit(ctx, emit, data, FYTCEF_SS | FYTCEF_DS |
 *                        FYTCEF_DE | FYTCEF_SE);
 *
 *   fy_type_context_free_data(ctx, data);
 *   fy_type_context_destroy(ctx);
 */

struct fy_type_context;
struct fy_meta_type;
struct fy_meta_field;

/**
 * enum fy_type_context_cfg_flags - Optional creation flags for fy_type_context_create()
 *
 * @FYTCCF_DUMP_REFLECTION:    Dump raw reflection structures during context creation
 * @FYTCCF_DUMP_TYPE_SYSTEM:   Dump the meta-type tree after it is built
 * @FYTCCF_DEBUG:              Enable verbose debug logging during type tree construction
 * @FYTCCF_STRICT_ANNOTATIONS: Treat unknown annotation keys as errors instead of notices.
 *                             Use this in build/CI pipelines to catch annotation typos early.
 */
enum fy_type_context_cfg_flags {
	FYTCCF_DUMP_REFLECTION    = (1U << 0),
	FYTCCF_DUMP_TYPE_SYSTEM   = (1U << 1),
	FYTCCF_DEBUG              = (1U << 2),
	FYTCCF_STRICT_ANNOTATIONS = (1U << 5),
};

/**
 * struct fy_type_context_cfg - Configuration for fy_type_context_create()
 *
 * @rfl:        Reflection object (must be non-NULL)
 * @entry_type: Name of the entry C type, e.g. "struct server_config"
 * @entry_meta: Optional YAML annotation string overriding the type's own
 *              annotation; pass NULL to use the annotation from the C source
 * @diag:       Optional diagnostic object; NULL = use a default stderr diagnoser
 * @flags:      Bitwise OR of ``enum fy_type_context_cfg_flags`` (0 = defaults)
 */
struct fy_type_context_cfg {
	struct fy_reflection *rfl;
	const char *entry_type;
	const char *entry_meta;
	struct fy_diag *diag;
	unsigned int flags;
};

/**
 * enum fy_type_context_emit_flags - Flags controlling stream/document delimiters
 *
 * @FYTCEF_SS: Emit a STREAM-START event before the document
 * @FYTCEF_DS: Emit a DOCUMENT-START event
 * @FYTCEF_DE: Emit a DOCUMENT-END event
 * @FYTCEF_SE: Emit a STREAM-END event after the document
 */
enum fy_type_context_emit_flags {
	FYTCEF_SS = (1U << 0),
	FYTCEF_DS = (1U << 1),
	FYTCEF_DE = (1U << 2),
	FYTCEF_SE = (1U << 3),
};

/**
 * fy_type_context_create() - Create a type context from a reflection object
 *
 * Builds the meta-type tree for the given entry type, resolving all annotations
 * and specialisations.  Returns NULL on error.
 *
 * @cfg: Configuration (must be non-NULL; cfg->rfl and cfg->entry_type required)
 *
 * Returns:
 * A new context object, or NULL on failure.
 */
struct fy_type_context *
fy_type_context_create(const struct fy_type_context_cfg *cfg)
	FY_EXPORT;

/**
 * fy_type_context_destroy() - Destroy a type context
 *
 * Frees all meta types and fields owned by this context.
 *
 * @ctx: Context to destroy (may be NULL)
 */
void
fy_type_context_destroy(struct fy_type_context *ctx)
	FY_EXPORT;

/**
 * fy_type_context_parse() - Parse a YAML stream into a C data structure
 *
 * Reads one document from @fyp and populates a newly allocated C struct
 * whose type is the context's entry type.  The caller is responsible for
 * freeing the returned data with fy_type_context_free_data().
 *
 * The parser must be positioned just before or at a STREAM-START / DOCUMENT-START
 * event.
 *
 * @ctx:   Type context
 * @fyp:   Parser positioned at the start of input
 * @datap: Output pointer; set to the parsed C struct on success
 *
 * Returns:
 * 0 on success, negative on error.
 */
int
fy_type_context_parse(struct fy_type_context *ctx,
		      struct fy_parser *fyp,
		      void **datap)
	FY_EXPORT;

/**
 * fy_type_context_emit() - Emit a C data structure as YAML
 *
 * Serialises @data (which must be a pointer to a C struct of the entry type)
 * using the emitter @emit.  Stream/document boundary events are controlled by
 * @flags (bitwise OR of ``enum fy_type_context_emit_flags``).
 *
 * @ctx:   Type context
 * @emit:  Emitter
 * @data:  Pointer to the C struct to serialise
 * @flags: Combination of FYTCEF_SS / FYTCEF_DS / FYTCEF_DE / FYTCEF_SE
 *
 * Returns:
 * 0 on success, negative on error.
 */
int
fy_type_context_emit(struct fy_type_context *ctx,
		     struct fy_emitter *emit,
		     const void *data,
		     unsigned int flags)
	FY_EXPORT;

/**
 * fy_type_context_free_data() - Free C data allocated by fy_type_context_parse()
 *
 * Runs destructors for pointer fields and releases the top-level allocation.
 *
 * @ctx:  Type context that produced @data
 * @data: Data pointer returned by fy_type_context_parse()
 */
void
fy_type_context_free_data(struct fy_type_context *ctx, void *data)
	FY_EXPORT;

/**
 * fy_type_context_get_root() - Return the root meta type of a context
 *
 * The root meta type corresponds to the entry type given at creation time.
 *
 * @ctx: Type context
 *
 * Returns:
 * Pointer to the root meta type, or NULL.
 */
const struct fy_meta_type *
fy_type_context_get_root(const struct fy_type_context *ctx)
	FY_EXPORT;

/**
 * fy_meta_type_get_type_info() - Get the C type info for a meta type
 *
 * @mt: Meta type
 *
 * Returns:
 * Pointer to the underlying fy_type_info, or NULL.
 */
const struct fy_type_info *
fy_meta_type_get_type_info(const struct fy_meta_type *mt)
	FY_EXPORT;

/**
 * fy_meta_type_get_field_count() - Get the number of fields in a meta type
 *
 * @mt: Meta type
 *
 * Returns:
 * Field count (0 for non-struct/union types).
 */
int
fy_meta_type_get_field_count(const struct fy_meta_type *mt)
	FY_EXPORT;

/**
 * fy_meta_type_get_field() - Get a field of a meta type by index
 *
 * @mt:  Meta type
 * @idx: Field index (0-based)
 *
 * Returns:
 * Pointer to the meta field, or NULL if idx is out of range.
 */
const struct fy_meta_field *
fy_meta_type_get_field(const struct fy_meta_type *mt, int idx)
	FY_EXPORT;

/**
 * fy_meta_field_get_field_info() - Get the C field info for a meta field
 *
 * @mf: Meta field
 *
 * Returns:
 * Pointer to the underlying fy_field_info, or NULL.
 */
const struct fy_field_info *
fy_meta_field_get_field_info(const struct fy_meta_field *mf)
	FY_EXPORT;

/**
 * fy_meta_field_get_meta_type() - Get the meta type of a field's value type
 *
 * @mf: Meta field
 *
 * Returns:
 * Pointer to the meta type for this field's value, or NULL.
 */
const struct fy_meta_type *
fy_meta_field_get_meta_type(const struct fy_meta_field *mf)
	FY_EXPORT;

/**
 * fy_reflection_prune_system() - Remove built-in/system types from a reflection object
 *
 * Marks all non-system structs, unions, enums and typedefs, then prunes all
 * unmarked types.  Call this after creating a reflection object to strip out
 * compiler-provided system headers before building a type context.
 *
 * @rfl: Reflection object to prune (must be non-NULL)
 */
void
fy_reflection_prune_system(struct fy_reflection *rfl)
	FY_EXPORT;

/**
 * fy_reflection_type_filter() - Retain only types whose names match a pattern
 *
 * Marks types whose name matches @type_include (if non-NULL) and does not
 * match @type_exclude (if non-NULL), then prunes all unmarked types.
 * Both patterns are POSIX extended regular expressions.
 *
 * @rfl:          Reflection object to filter (must be non-NULL)
 * @type_include: Regex for type names to keep; NULL = keep all
 * @type_exclude: Regex for type names to drop; NULL = drop none
 *
 * Returns:
 * 0 on success, -1 on regex compilation error.
 */
int
fy_reflection_type_filter(struct fy_reflection *rfl,
			   const char *type_include, const char *type_exclude)
	FY_EXPORT;

/**
 * fy_reflection_equal() - Test whether two reflection objects are equivalent
 *
 * Iterates both reflection objects in order and compares each type_info
 * structurally.  Useful for verifying packed round-trips.
 *
 * @rfl_a: First reflection object
 * @rfl_b: Second reflection object
 *
 * Returns:
 * true if the objects are equivalent, false otherwise.
 */
bool
fy_reflection_equal(struct fy_reflection *rfl_a, struct fy_reflection *rfl_b)
	FY_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* LIBFYAML_REFLECTION_H */
