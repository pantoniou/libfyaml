/*
 * fy-getopt.c - Portable getopt/getopt_long implementation
 *
 * This is a portable implementation of getopt and getopt_long for systems
 * that don't have them (primarily Windows/MSVC).
 *
 * Based on public domain implementations.
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fy-getopt.h"

char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = '?';

static int optwhere = 1;

static void permute_args(int paession, int from, int to, char * const argv[])
{
    int i;
    char *tmp;

    if (from == to)
        return;

    for (i = 0; i < (to - from) / 2; i++) {
        tmp = argv[from + i];
        ((char **)argv)[from + i] = argv[to - 1 - i];
        ((char **)argv)[to - 1 - i] = tmp;
    }
}

int getopt(int argc, char * const argv[], const char *optstring)
{
    char *opt;

    optarg = NULL;

    if (optind >= argc || argv[optind] == NULL)
        return -1;

    if (argv[optind][0] != '-' || argv[optind][1] == '\0')
        return -1;

    if (argv[optind][1] == '-' && argv[optind][2] == '\0') {
        optind++;
        return -1;
    }

    opt = strchr(optstring, argv[optind][optwhere]);
    if (opt == NULL || argv[optind][optwhere] == ':') {
        optopt = argv[optind][optwhere];
        if (opterr && *optstring != ':')
            fprintf(stderr, "%s: illegal option -- %c\n", argv[0], optopt);
        if (argv[optind][++optwhere] == '\0') {
            optind++;
            optwhere = 1;
        }
        return '?';
    }

    if (opt[1] == ':') {
        if (argv[optind][optwhere + 1] != '\0') {
            optarg = &argv[optind][optwhere + 1];
            optind++;
            optwhere = 1;
        } else if (opt[2] != ':') {
            if (++optind >= argc) {
                optopt = *opt;
                if (opterr && *optstring != ':')
                    fprintf(stderr, "%s: option requires an argument -- %c\n",
                            argv[0], optopt);
                return (*optstring == ':') ? ':' : '?';
            }
            optarg = argv[optind++];
            optwhere = 1;
        } else {
            optind++;
            optwhere = 1;
        }
    } else {
        if (argv[optind][++optwhere] == '\0') {
            optind++;
            optwhere = 1;
        }
    }

    return *opt;
}

int getopt_long(int argc, char * const argv[], const char *optstring,
                const struct option *longopts, int *longindex)
{
    int i;
    size_t len;
    const char *arg;
    const struct option *o;

    optarg = NULL;

    if (optind >= argc || argv[optind] == NULL)
        return -1;

    arg = argv[optind];

    if (arg[0] != '-')
        return -1;

    if (arg[1] == '-' && arg[2] == '\0') {
        optind++;
        return -1;
    }

    if (arg[1] != '-')
        return getopt(argc, argv, optstring);

    arg += 2;

    for (i = 0; longopts[i].name != NULL; i++) {
        o = &longopts[i];
        len = strlen(o->name);

        if (strncmp(arg, o->name, len) == 0) {
            if (arg[len] == '=') {
                if (o->has_arg == no_argument) {
                    optopt = o->val;
                    if (opterr)
                        fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n",
                                argv[0], o->name);
                    return '?';
                }
                optarg = (char *)&arg[len + 1];
                optind++;
                if (longindex)
                    *longindex = i;
                if (o->flag) {
                    *o->flag = o->val;
                    return 0;
                }
                return o->val;
            } else if (arg[len] == '\0') {
                if (o->has_arg == required_argument) {
                    if (optind + 1 >= argc) {
                        optopt = o->val;
                        if (opterr)
                            fprintf(stderr, "%s: option '--%s' requires an argument\n",
                                    argv[0], o->name);
                        return '?';
                    }
                    optarg = argv[++optind];
                }
                optind++;
                if (longindex)
                    *longindex = i;
                if (o->flag) {
                    *o->flag = o->val;
                    return 0;
                }
                return o->val;
            }
        }
    }

    optopt = 0;
    if (opterr)
        fprintf(stderr, "%s: unrecognized option '--%s'\n", argv[0], arg);
    optind++;
    return '?';
}

int getopt_long_only(int argc, char * const argv[], const char *optstring,
                     const struct option *longopts, int *longindex)
{
    int i;
    size_t len;
    const char *arg;
    const struct option *o;

    optarg = NULL;

    if (optind >= argc || argv[optind] == NULL)
        return -1;

    arg = argv[optind];

    if (arg[0] != '-')
        return -1;

    if (arg[1] == '-' && arg[2] == '\0') {
        optind++;
        return -1;
    }

    /* For getopt_long_only, try long options even with single dash */
    if (arg[1] == '-')
        arg += 2;
    else
        arg += 1;

    for (i = 0; longopts[i].name != NULL; i++) {
        o = &longopts[i];
        len = strlen(o->name);

        if (strncmp(arg, o->name, len) == 0) {
            if (arg[len] == '=') {
                if (o->has_arg == no_argument) {
                    optopt = o->val;
                    if (opterr)
                        fprintf(stderr, "%s: option '%s' doesn't allow an argument\n",
                                argv[0], argv[optind]);
                    return '?';
                }
                optarg = (char *)&arg[len + 1];
                optind++;
                if (longindex)
                    *longindex = i;
                if (o->flag) {
                    *o->flag = o->val;
                    return 0;
                }
                return o->val;
            } else if (arg[len] == '\0') {
                if (o->has_arg == required_argument) {
                    if (optind + 1 >= argc) {
                        optopt = o->val;
                        if (opterr)
                            fprintf(stderr, "%s: option '%s' requires an argument\n",
                                    argv[0], argv[optind]);
                        return '?';
                    }
                    optarg = argv[++optind];
                }
                optind++;
                if (longindex)
                    *longindex = i;
                if (o->flag) {
                    *o->flag = o->val;
                    return 0;
                }
                return o->val;
            }
        }
    }

    /* Fall back to short options if no long option matched */
    if (argv[optind][1] != '-') {
        return getopt(argc, argv, optstring);
    }

    optopt = 0;
    if (opterr)
        fprintf(stderr, "%s: unrecognized option '%s'\n", argv[0], argv[optind]);
    optind++;
    return '?';
}

#endif /* _WIN32 */
