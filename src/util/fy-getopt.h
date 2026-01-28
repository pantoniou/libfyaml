/*
 * fy-getopt.h - wrapper for getopt
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_GETOPT_H
#define FY_GETOPT_H

#ifdef _WIN32
#include "getopt.h"
#else
#define INCLUDED_GETOPT_PORT_H	// guard
#include <getopt.h>
#endif

#endif
