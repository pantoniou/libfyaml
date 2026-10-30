// SPDX-License-Identifier: MIT

/*
 * The macros come from the file user.macros, the way a caller with a compiler
 * that is not built in gives them. The array sizes tell what was defined.
 */
#ifdef USER_COMPILER
typedef char user_compiler_t[USER_COMPILER_VERSION];
#endif

struct user_buffer {
	char data[USER_BUFFER_SIZE];
	int field_count[USER_FIELDS(2, 3)];
};

/* the compiled-in macros define __SIZEOF_POINTER__, no_predefs removes it */
#ifdef __SIZEOF_POINTER__
typedef char predefs_t[1];
#else
typedef char predefs_t[2];
#endif
