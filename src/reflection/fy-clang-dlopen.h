/*
 * fy-clang-dlopen.h - load libclang on the first use of the clang backend
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_CLANG_DLOPEN_H
#define FY_CLANG_DLOPEN_H

int fy_clang_api_load(void);

#ifdef FY_LIBCLANG_DLOPEN

#include <clang-c/Index.h>
#include <clang-c/Documentation.h>

#define FY_CLANG_FUNCS(X) \
	X(clang_createIndex) \
	X(clang_createTranslationUnitFromSourceFile) \
	X(clang_Cursor_getOffsetOfField) \
	X(clang_Cursor_getParsedComment) \
	X(clang_Cursor_getRawCommentText) \
	X(clang_Cursor_isAnonymous) \
	X(clang_Cursor_isAnonymousRecordDecl) \
	X(clang_Cursor_isBitField) \
	X(clang_Cursor_isNull) \
	X(clang_disposeIndex) \
	X(clang_disposeString) \
	X(clang_disposeTranslationUnit) \
	X(clang_equalCursors) \
	X(clang_equalTypes) \
	X(clang_getArrayElementType) \
	X(clang_getCString) \
	X(clang_getCursorDisplayName) \
	X(clang_getCursorExtent) \
	X(clang_getCursorKind) \
	X(clang_getCursorKindSpelling) \
	X(clang_getCursorLocation) \
	X(clang_getCursorSemanticParent) \
	X(clang_getCursorSpelling) \
	X(clang_getCursorType) \
	X(clang_getElementType) \
	X(clang_getEnumConstantDeclUnsignedValue) \
	X(clang_getEnumConstantDeclValue) \
	X(clang_getEnumDeclIntegerType) \
	X(clang_getFieldDeclBitWidth) \
	X(clang_getFileLocation) \
	X(clang_getFileName) \
	X(clang_getFileTime) \
	X(clang_getNullCursor) \
	X(clang_getNumDiagnostics) \
	X(clang_getNumElements) \
	X(clang_getPointeeType) \
	X(clang_getRangeEnd) \
	X(clang_getRangeStart) \
	X(clang_getTranslationUnitCursor) \
	X(clang_getTranslationUnitTargetInfo) \
	X(clang_getTypeDeclaration) \
	X(clang_getTypedefDeclUnderlyingType) \
	X(clang_getTypeKindSpelling) \
	X(clang_getTypeSpelling) \
	X(clang_isConstQualifiedType) \
	X(clang_isCursorDefinition) \
	X(clang_isRestrictQualifiedType) \
	X(clang_isVolatileQualifiedType) \
	X(clang_Location_isFromMainFile) \
	X(clang_Location_isInSystemHeader) \
	X(clang_TargetInfo_dispose) \
	X(clang_TargetInfo_getTriple) \
	X(clang_Type_getAlignOf) \
	X(clang_Type_getNamedType) \
	X(clang_Type_getSizeOf) \
	X(clang_Type_visitFields) \
	X(clang_visitChildren)

struct fy_clang_api {
#define X(n) __typeof__(n) *n;
	FY_CLANG_FUNCS(X)
#undef X
};

extern struct fy_clang_api fy_clang_api;

#ifndef FY_CLANG_DLOPEN_IMPLEMENTATION
#define clang_createIndex (fy_clang_api.clang_createIndex)
#define clang_createTranslationUnitFromSourceFile (fy_clang_api.clang_createTranslationUnitFromSourceFile)
#define clang_Cursor_getOffsetOfField (fy_clang_api.clang_Cursor_getOffsetOfField)
#define clang_Cursor_getParsedComment (fy_clang_api.clang_Cursor_getParsedComment)
#define clang_Cursor_getRawCommentText (fy_clang_api.clang_Cursor_getRawCommentText)
#define clang_Cursor_isAnonymous (fy_clang_api.clang_Cursor_isAnonymous)
#define clang_Cursor_isAnonymousRecordDecl (fy_clang_api.clang_Cursor_isAnonymousRecordDecl)
#define clang_Cursor_isBitField (fy_clang_api.clang_Cursor_isBitField)
#define clang_Cursor_isNull (fy_clang_api.clang_Cursor_isNull)
#define clang_disposeIndex (fy_clang_api.clang_disposeIndex)
#define clang_disposeString (fy_clang_api.clang_disposeString)
#define clang_disposeTranslationUnit (fy_clang_api.clang_disposeTranslationUnit)
#define clang_equalCursors (fy_clang_api.clang_equalCursors)
#define clang_equalTypes (fy_clang_api.clang_equalTypes)
#define clang_getArrayElementType (fy_clang_api.clang_getArrayElementType)
#define clang_getCString (fy_clang_api.clang_getCString)
#define clang_getCursorDisplayName (fy_clang_api.clang_getCursorDisplayName)
#define clang_getCursorExtent (fy_clang_api.clang_getCursorExtent)
#define clang_getCursorKind (fy_clang_api.clang_getCursorKind)
#define clang_getCursorKindSpelling (fy_clang_api.clang_getCursorKindSpelling)
#define clang_getCursorLocation (fy_clang_api.clang_getCursorLocation)
#define clang_getCursorSemanticParent (fy_clang_api.clang_getCursorSemanticParent)
#define clang_getCursorSpelling (fy_clang_api.clang_getCursorSpelling)
#define clang_getCursorType (fy_clang_api.clang_getCursorType)
#define clang_getElementType (fy_clang_api.clang_getElementType)
#define clang_getEnumConstantDeclUnsignedValue (fy_clang_api.clang_getEnumConstantDeclUnsignedValue)
#define clang_getEnumConstantDeclValue (fy_clang_api.clang_getEnumConstantDeclValue)
#define clang_getEnumDeclIntegerType (fy_clang_api.clang_getEnumDeclIntegerType)
#define clang_getFieldDeclBitWidth (fy_clang_api.clang_getFieldDeclBitWidth)
#define clang_getFileLocation (fy_clang_api.clang_getFileLocation)
#define clang_getFileName (fy_clang_api.clang_getFileName)
#define clang_getFileTime (fy_clang_api.clang_getFileTime)
#define clang_getNullCursor (fy_clang_api.clang_getNullCursor)
#define clang_getNumDiagnostics (fy_clang_api.clang_getNumDiagnostics)
#define clang_getNumElements (fy_clang_api.clang_getNumElements)
#define clang_getPointeeType (fy_clang_api.clang_getPointeeType)
#define clang_getRangeEnd (fy_clang_api.clang_getRangeEnd)
#define clang_getRangeStart (fy_clang_api.clang_getRangeStart)
#define clang_getTranslationUnitCursor (fy_clang_api.clang_getTranslationUnitCursor)
#define clang_getTranslationUnitTargetInfo (fy_clang_api.clang_getTranslationUnitTargetInfo)
#define clang_getTypeDeclaration (fy_clang_api.clang_getTypeDeclaration)
#define clang_getTypedefDeclUnderlyingType (fy_clang_api.clang_getTypedefDeclUnderlyingType)
#define clang_getTypeKindSpelling (fy_clang_api.clang_getTypeKindSpelling)
#define clang_getTypeSpelling (fy_clang_api.clang_getTypeSpelling)
#define clang_isConstQualifiedType (fy_clang_api.clang_isConstQualifiedType)
#define clang_isCursorDefinition (fy_clang_api.clang_isCursorDefinition)
#define clang_isRestrictQualifiedType (fy_clang_api.clang_isRestrictQualifiedType)
#define clang_isVolatileQualifiedType (fy_clang_api.clang_isVolatileQualifiedType)
#define clang_Location_isFromMainFile (fy_clang_api.clang_Location_isFromMainFile)
#define clang_Location_isInSystemHeader (fy_clang_api.clang_Location_isInSystemHeader)
#define clang_TargetInfo_dispose (fy_clang_api.clang_TargetInfo_dispose)
#define clang_TargetInfo_getTriple (fy_clang_api.clang_TargetInfo_getTriple)
#define clang_Type_getAlignOf (fy_clang_api.clang_Type_getAlignOf)
#define clang_Type_getNamedType (fy_clang_api.clang_Type_getNamedType)
#define clang_Type_getSizeOf (fy_clang_api.clang_Type_getSizeOf)
#define clang_Type_visitFields (fy_clang_api.clang_Type_visitFields)
#define clang_visitChildren (fy_clang_api.clang_visitChildren)

#endif

#endif

#endif
