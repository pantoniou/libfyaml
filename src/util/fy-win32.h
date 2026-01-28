/*
 * fy-win32.h - Windows compatibility layer
 *
 * Copyright (c) 2024-2026 libfyaml contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * This header provides Unix-like APIs on Windows to allow libfyaml to
 * build and run on Windows platforms.
 */
#ifndef FY_WIN32_H
#define FY_WIN32_H

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>
/* Undefine Windows macros that conflict with common identifiers */
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
#include <io.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <direct.h>
#include <process.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <malloc.h>

/* ssize_t is not defined on Windows */
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#ifdef _WIN64
typedef __int64 ssize_t;
#else
typedef int ssize_t;
#endif
#endif

/* Provide SSIZE_MAX if not defined */
#ifndef SSIZE_MAX
#ifdef _WIN64
#define SSIZE_MAX _I64_MAX
#else
#define SSIZE_MAX INT_MAX
#endif
#endif

/*
 * Memory mapping emulation using Windows Virtual Memory APIs
 */

/* mmap protection flags */
#ifndef PROT_NONE
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4
#endif

/* mmap flags */
#ifndef MAP_SHARED
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void *)-1)
#endif

static inline void *fy_win32_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	DWORD flProtect = 0;
	DWORD dwDesiredAccess = 0;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	HANDLE hMapping = NULL;
	void *ptr = NULL;

	(void)addr; /* Hint address is ignored */

	/* Anonymous mapping - use VirtualAlloc */
	if (flags & MAP_ANONYMOUS) {
		DWORD flAllocationType = MEM_RESERVE | MEM_COMMIT;
		DWORD flAllocProtect = 0;

		if (prot == PROT_NONE)
			flAllocProtect = PAGE_NOACCESS;
		else if (prot & PROT_EXEC) {
			if (prot & PROT_WRITE)
				flAllocProtect = PAGE_EXECUTE_READWRITE;
			else
				flAllocProtect = PAGE_EXECUTE_READ;
		} else if (prot & PROT_WRITE)
			flAllocProtect = PAGE_READWRITE;
		else
			flAllocProtect = PAGE_READONLY;

		ptr = VirtualAlloc(NULL, length, flAllocationType, flAllocProtect);
		if (!ptr)
			return MAP_FAILED;
		return ptr;
	}

	/* File mapping */
	if (fd < 0) {
		errno = EBADF;
		return MAP_FAILED;
	}

	hFile = (HANDLE)_get_osfhandle(fd);
	if (hFile == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return MAP_FAILED;
	}

	/* Set protection and access flags */
	if (prot == PROT_NONE) {
		flProtect = PAGE_NOACCESS;
		dwDesiredAccess = 0;
	} else if (prot & PROT_EXEC) {
		if (prot & PROT_WRITE) {
			flProtect = PAGE_EXECUTE_READWRITE;
			dwDesiredAccess = FILE_MAP_WRITE | FILE_MAP_EXECUTE;
		} else {
			flProtect = PAGE_EXECUTE_READ;
			dwDesiredAccess = FILE_MAP_READ | FILE_MAP_EXECUTE;
		}
	} else if (prot & PROT_WRITE) {
		if (flags & MAP_PRIVATE) {
			flProtect = PAGE_WRITECOPY;
			dwDesiredAccess = FILE_MAP_COPY;
		} else {
			flProtect = PAGE_READWRITE;
			dwDesiredAccess = FILE_MAP_WRITE;
		}
	} else {
		flProtect = PAGE_READONLY;
		dwDesiredAccess = FILE_MAP_READ;
	}

	hMapping = CreateFileMapping(hFile, NULL, flProtect, 0, 0, NULL);
	if (!hMapping) {
		errno = ENOMEM;
		return MAP_FAILED;
	}

	ptr = MapViewOfFile(hMapping, dwDesiredAccess,
			    (DWORD)((uint64_t)offset >> 32), (DWORD)offset, length);
	CloseHandle(hMapping);

	if (!ptr) {
		errno = ENOMEM;
		return MAP_FAILED;
	}

	return ptr;
}

static inline int fy_win32_munmap(void *addr, size_t length)
{
	(void)length;

	/*
	 * We try both VirtualFree (for anonymous mappings) and
	 * UnmapViewOfFile (for file mappings). One of them will succeed.
	 */
	if (UnmapViewOfFile(addr))
		return 0;
	if (VirtualFree(addr, 0, MEM_RELEASE))
		return 0;

	errno = EINVAL;
	return -1;
}

/* Define mmap/munmap macros that use our implementations */
#define mmap(addr, length, prot, flags, fd, offset) \
	fy_win32_mmap(addr, length, prot, flags, fd, offset)
#define munmap(addr, length) \
	fy_win32_munmap(addr, length)

/* mremap is not supported on Windows - will never be defined */

/*
 * File descriptor operations
 */

/* open() flags that may be missing */
#ifndef O_RDONLY
#define O_RDONLY _O_RDONLY
#endif
#ifndef O_WRONLY
#define O_WRONLY _O_WRONLY
#endif
#ifndef O_RDWR
#define O_RDWR _O_RDWR
#endif
#ifndef O_CREAT
#define O_CREAT _O_CREAT
#endif
#ifndef O_TRUNC
#define O_TRUNC _O_TRUNC
#endif
#ifndef O_APPEND
#define O_APPEND _O_APPEND
#endif
#ifndef O_BINARY
#define O_BINARY _O_BINARY
#endif

/* Standard file descriptor numbers */
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

static inline int fy_win32_open(const char *pathname, int flags, ...)
{
	/* always use O_BINARY to avoid CRLF mangling */
	return _open(pathname, flags | O_BINARY);
}

/* Use _open, _close on Windows */
#ifndef open
#define open fy_win32_open
#endif
#ifndef close
#define close _close
#endif

/*
 * Windows _read/_write use unsigned int (32-bit) for count.
 * Provide wrappers that handle size_t properly by chunking large I/O.
 */
static inline ssize_t fy_win32_read(int fd, void *buf, size_t count)
{
	unsigned char *p = (unsigned char *)buf;
	size_t remaining = count;
	ssize_t total = 0;

	while (remaining > 0) {
		unsigned int chunk = (remaining > UINT_MAX) ? UINT_MAX : (unsigned int)remaining;
		int ret = _read(fd, p, chunk);
		if (ret < 0)
			return total > 0 ? total : -1;
		if (ret == 0)
			break;
		total += ret;
		p += ret;
		remaining -= ret;
		if ((unsigned int)ret < chunk)
			break;  /* short read */
	}
	return total;
}

static inline ssize_t fy_win32_write(int fd, const void *buf, size_t count)
{
	const unsigned char *p = (const unsigned char *)buf;
	size_t remaining = count;
	ssize_t total = 0;

	while (remaining > 0) {
		unsigned int chunk = (remaining > UINT_MAX) ? UINT_MAX : (unsigned int)remaining;
		int ret = _write(fd, p, chunk);
		if (ret < 0)
			return total > 0 ? total : -1;
		if (ret == 0)
			break;
		total += ret;
		p += ret;
		remaining -= ret;
		if ((unsigned int)ret < chunk)
			break;  /* short write */
	}
	return total;
}

#ifndef read
#define read fy_win32_read
#endif
#ifndef write
#define write fy_win32_write
#endif
#ifndef lseek
#define lseek _lseek
#endif
#ifndef fileno
#define fileno _fileno
#endif
#ifndef fdopen
#define fdopen _fdopen
#endif
#ifndef dup
#define dup _dup
#endif
#ifndef dup2
#define dup2 _dup2
#endif
#ifndef access
#define access _access
#endif

/* stat structure and functions */
#ifndef stat
#define stat _stat64
#endif
#ifndef fstat
#define fstat _fstat64
#endif

#ifndef isatty

static inline int
fy_win32_isatty(int fd)
{
	HANDLE h;
	DWORD mode;

	h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE || h == NULL)
		return 0;

	return GetConsoleMode(h, &mode) != 0;
}

#define isatty(fd) fy_win32_isatty(fd)
#endif

/* S_ISREG and S_ISDIR macros */
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_IFREG
#define S_IFREG _S_IFREG
#endif
#ifndef S_IFMT
#define S_IFMT _S_IFMT
#endif

/* Unix file permission macros - Windows doesn't have these */
#ifndef S_IRUSR
#define S_IRUSR _S_IREAD
#endif
#ifndef S_IWUSR
#define S_IWUSR _S_IWRITE
#endif
#ifndef S_IXUSR
#define S_IXUSR _S_IEXEC
#endif
/* Group/other permissions don't exist on Windows, define as 0 */
#ifndef S_IRGRP
#define S_IRGRP 0
#endif
#ifndef S_IWGRP
#define S_IWGRP 0
#endif
#ifndef S_IXGRP
#define S_IXGRP 0
#endif
#ifndef S_IROTH
#define S_IROTH 0
#endif
#ifndef S_IWOTH
#define S_IWOTH 0
#endif
#ifndef S_IXOTH
#define S_IXOTH 0
#endif

/*
 * System configuration - sysconf() emulation
 */
#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE 1
#endif
#ifndef _SC_NPROCESSORS_ONLN
#define _SC_NPROCESSORS_ONLN 2
#endif

static inline long fy_win32_sysconf(int name)
{
	SYSTEM_INFO si;

	switch (name) {
	case _SC_PAGESIZE:
		GetSystemInfo(&si);
		return (long)si.dwPageSize;
	case _SC_NPROCESSORS_ONLN:
		GetSystemInfo(&si);
		return (long)si.dwNumberOfProcessors;
	default:
		errno = EINVAL;
		return -1;
	}
}

#define sysconf(name) fy_win32_sysconf(name)

/*
 * asprintf() implementation for Windows
 */
#ifndef HAVE_ASPRINTF

static inline int fy_win32_vasprintf(char **strp, const char *fmt, va_list ap)
{
	int len;
	va_list ap_copy;

	va_copy(ap_copy, ap);
	len = _vscprintf(fmt, ap_copy);
	va_end(ap_copy);

	if (len < 0) {
		*strp = NULL;
		return -1;
	}

	*strp = (char *)malloc(len + 1);
	if (!*strp)
		return -1;

	len = vsprintf(*strp, fmt, ap);
	if (len < 0) {
		free(*strp);
		*strp = NULL;
		return -1;
	}

	return len;
}

static inline int fy_win32_asprintf(char **strp, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = fy_win32_vasprintf(strp, fmt, ap);
	va_end(ap);

	return ret;
}

#define asprintf fy_win32_asprintf
#define vasprintf fy_win32_vasprintf

#endif /* HAVE_ASPRINTF */

/*
 * Directory functions
 */
#ifndef getcwd
#define getcwd _getcwd
#endif
#ifndef mkdir
#define mkdir(path, mode) _mkdir(path)
#endif
#ifndef rmdir
#define rmdir _rmdir
#endif
#ifndef chdir
#define chdir _chdir
#endif

/*
 * Process functions
 */
#ifndef getpid
#define getpid _getpid
#endif

/*
 * String functions
 */
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
#ifndef strdup
#define strdup _strdup
#endif

/*
 * Sleep functions
 */
static inline unsigned int fy_win32_sleep(unsigned int seconds)
{
	Sleep(seconds * 1000);
	return 0;
}

static inline int fy_win32_usleep(unsigned int usec)
{
	HANDLE timer;
	LARGE_INTEGER ft;

	ft.QuadPart = -(10 * (LONGLONG)usec);
	timer = CreateWaitableTimer(NULL, TRUE, NULL);
	if (!timer)
		return -1;
	SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);
	return 0;
}

#define sleep(s) fy_win32_sleep(s)
#define usleep(us) fy_win32_usleep(us)

/*
 * clock_gettime emulation for Windows
 */
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

/*
 * struct timespec is defined in UCRT's time.h for MSVC 2015+ (VS2015+)
 * time.h is included above to ensure it's available.
 */

static inline int fy_win32_clock_gettime(int clock_id, struct timespec *tp)
{
	LARGE_INTEGER freq, count;
	FILETIME ft;
	ULARGE_INTEGER uli;

	if (clock_id == CLOCK_MONOTONIC) {
		if (!QueryPerformanceFrequency(&freq))
			return -1;
		if (!QueryPerformanceCounter(&count))
			return -1;
		tp->tv_sec = (time_t)(count.QuadPart / freq.QuadPart);
		tp->tv_nsec = (long)((count.QuadPart % freq.QuadPart) * 1000000000LL / freq.QuadPart);
	} else {
		/* CLOCK_REALTIME */
		GetSystemTimeAsFileTime(&ft);
		uli.LowPart = ft.dwLowDateTime;
		uli.HighPart = ft.dwHighDateTime;
		/* FILETIME is 100-nanosecond intervals since January 1, 1601 */
		/* Convert to Unix epoch (seconds since January 1, 1970) */
		uli.QuadPart -= 116444736000000000ULL;
		tp->tv_sec = (time_t)(uli.QuadPart / 10000000ULL);
		tp->tv_nsec = (long)((uli.QuadPart % 10000000ULL) * 100);
	}
	return 0;
}

#define clock_gettime(clock_id, tp) fy_win32_clock_gettime(clock_id, tp)

/*
 * EAGAIN may be different from EWOULDBLOCK on some systems
 */
#ifndef EAGAIN
#define EAGAIN EWOULDBLOCK
#endif

/*
 * alloca is available as _alloca on Windows
 */
#ifndef alloca
#define alloca _alloca
#endif

/*
 * iovec structure for scatter/gather I/O
 * Defined here since sys/uio.h doesn't exist on Windows
 */
#ifndef _FY_IOVEC_DEFINED
#define _FY_IOVEC_DEFINED
struct iovec {
	void  *iov_base;  /* Starting address */
	size_t iov_len;   /* Number of bytes to transfer */
};
#endif

#if defined(_MSC_VER) && !defined(__clang__)
typedef union {
    void*        p;
    double       d;
    __int64      i64;
} max_align_t;
#endif

#endif /* _WIN32 */

#endif /* FY_WIN32_H */
