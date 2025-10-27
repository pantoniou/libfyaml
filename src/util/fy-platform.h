/*
 * fy-platform.h - Platform abstraction layer
 *
 * Copyright (c) 2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FY_PLATFORM_H
#define FY_PLATFORM_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stddef.h>

/*
 * Platform-specific includes
 */
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#ifndef _MSC_VER
/* MinGW has these, MSVC doesn't */
#include <unistd.h>
#endif
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

/*
 * Page size abstraction
 */
#ifdef _WIN32
static inline long fy_get_pagesize(void)
{
	static long pagesize = 0;
	if (pagesize == 0) {
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		pagesize = (long)si.dwPageSize;
	}
	return pagesize;
}
#else
static inline long fy_get_pagesize(void)
{
	return sysconf(_SC_PAGESIZE);
}
#endif

/*
 * CPU count abstraction
 */
#ifdef _WIN32
static inline long fy_get_nprocs(void)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return (long)si.dwNumberOfProcessors;
}
#else
static inline long fy_get_nprocs(void)
{
	return sysconf(_SC_NPROCESSORS_ONLN);
}
#endif

/*
 * File I/O abstraction
 * MinGW provides POSIX functions, so we only need special handling for MSVC
 */
#ifdef _MSC_VER
/* MSVC uses _open, _close, etc. */
#define fy_open _open
#define fy_close _close
#define fy_read _read
#define fy_write _write
#define fy_lseek _lseek
#define fy_fstat _fstat
#define fy_stat _stat
#define fy_O_RDONLY _O_RDONLY
#define fy_O_WRONLY _O_WRONLY
#define fy_O_RDWR _O_RDWR
#define fy_O_CREAT _O_CREAT
#define fy_O_TRUNC _O_TRUNC
#define fy_O_BINARY _O_BINARY
#else
/* POSIX systems and MinGW */
#define fy_open open
#define fy_close close
#define fy_read read
#define fy_write write
#define fy_lseek lseek
#define fy_fstat fstat
#define fy_stat stat
#define fy_O_RDONLY O_RDONLY
#define fy_O_WRONLY O_WRONLY
#define fy_O_RDWR O_RDWR
#define fy_O_CREAT O_CREAT
#define fy_O_TRUNC O_TRUNC
#ifdef O_BINARY
#define fy_O_BINARY O_BINARY
#else
#define fy_O_BINARY 0
#endif
#endif

/*
 * Memory mapping abstraction
 * Windows doesn't have mmap, but the code already has fallback paths
 * We just need to ensure mmap is properly guarded
 */
#ifdef _WIN32
/* Disable mmap on Windows - fallback to buffered I/O */
#ifndef FY_DISABLE_MMAP
#define FY_DISABLE_MMAP 1
#endif
#endif

/*
 * alloca abstraction (though most platforms have it)
 */
#ifdef _MSC_VER
#include <malloc.h>
#ifndef alloca
#define alloca _alloca
#endif
#endif

/*
 * PATH_MAX abstraction
 */
#ifndef PATH_MAX
#ifdef _WIN32
#define PATH_MAX MAX_PATH
#else
#define PATH_MAX 4096
#endif
#endif

#endif /* FY_PLATFORM_H */
