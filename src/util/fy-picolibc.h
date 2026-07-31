/*
 * fy-picolibc.h - picolibc compatibility layer
 *
 * SPDX-License-Identifier: MIT
 *
 * picolibc is the C library used by most Zephyr targets. Like the Windows
 * layer in fy-win32.h, this header supplies the few POSIX facilities picolibc
 * does not expose, so the rest of libfyaml can stay POSIX-shaped.
 *
 * It keys on __PICOLIBC__ (defined by the C library itself), not on the OS, so
 * builds against a full-featured C library - glibc on native_sim, newlib, etc.
 * - are unaffected.
 */
#ifndef FY_PICOLIBC_H
#define FY_PICOLIBC_H

/* Pull in the C library's feature header so __PICOLIBC__ becomes visible. */
#include <stdint.h>

#ifdef __PICOLIBC__

/*
 * picolibc gates isatty() behind feature macros Zephyr does not satisfy and
 * ships no implementation for it. The console is never an interactive terminal
 * here, so always report "not a tty".
 */
#ifndef isatty
static inline int fy_picolibc_isatty(int fd) { (void)fd; return 0; }
#define isatty(fd) fy_picolibc_isatty(fd)
#endif

/* Standard file-descriptor numbers, absent from picolibc's <unistd.h>. */
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#endif /* __PICOLIBC__ */

#endif /* FY_PICOLIBC_H */
