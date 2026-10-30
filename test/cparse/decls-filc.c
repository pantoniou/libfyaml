// SPDX-License-Identifier: MIT

/* the macros of Fil-C: the compiler that has them says what it is */
#ifdef __FILC__
typedef int is_filc;
#else
typedef int is_not_filc;
#endif

#ifdef __PIZLONATOR_WAS_HERE__
typedef int has_pizlonator;
#endif

/* a pointer is 8 bytes in an object */
struct filc_ptrs { char c; void *p; long l; };
