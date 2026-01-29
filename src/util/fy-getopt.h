/*
 * fy-getopt.h - wrapper for getopt
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_GETOPT_H
#define FY_GETOPT_H

#ifdef _WIN32
#include "getopt-fallback.h"
#else
#include <getopt.h>
#endif

#endif
