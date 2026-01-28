/*
 * fy-getopt.h - Portable getopt/getopt_long implementation
 *
 * This is a portable implementation of getopt and getopt_long for systems
 * that don't have them (primarily Windows/MSVC).
 *
 * Based on public domain implementations.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_GETOPT_H
#define FY_GETOPT_H

#ifdef _WIN32

#ifdef __cplusplus
extern "C" {
#endif

extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

#define no_argument        0
#define required_argument  1
#define optional_argument  2

struct option {
    const char *name;
    int         has_arg;
    int        *flag;
    int         val;
};

int getopt(int argc, char * const argv[], const char *optstring);
int getopt_long(int argc, char * const argv[], const char *optstring,
                const struct option *longopts, int *longindex);
int getopt_long_only(int argc, char * const argv[], const char *optstring,
                     const struct option *longopts, int *longindex);

#ifdef __cplusplus
}
#endif

#else /* !_WIN32 */

#include <getopt.h>

#endif /* _WIN32 */

#endif /* FY_GETOPT_H */
