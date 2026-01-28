/*
 * fy-thread.h - Lighting fast thread pool implementation
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_THREAD_H
#define FY_THREAD_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef _WIN32
#include <pthread.h>
#ifndef __APPLE__
#include <semaphore.h>
#else
#include <dispatch/dispatch.h>
#endif
#endif

#include "fy-win32.h"
#include "fy-bit64.h"
#include "fy-align.h"
#include "fy-atomics.h"

#include <libfyaml.h>

// #define FY_THREAD_DEBUG		/* define to enable debugging information to stderr */
// #define FY_THREAD_PORTABLE	/* define to use the portable implementation even on linux */

struct fy_work_pool {
	FY_ATOMIC(size_t) work_left;
#if defined(__linux__) && !defined(FY_THREAD_PORTABLE)
	FY_ATOMIC(uint32_t) done;
#elif defined(__APPLE__)
	dispatch_semaphore_t sem;
#elif defined(_WIN32)
	HANDLE sem;
#else
	sem_t sem;
#endif
};

struct fy_thread {
	struct fy_thread_pool *tp;
	unsigned int id;
	FY_ATOMIC(struct fy_thread_work *)work;
	FY_ATOMIC(struct fy_thread_work *)next_work;
#ifndef _WIN32
	pthread_t tid;
#else
	HANDLE tid;
#endif
#if defined(__linux__) && !defined(FY_THREAD_PORTABLE)
	FY_ATOMIC(uint32_t) submit;
	FY_ATOMIC(uint32_t) done;
#elif defined(_WIN32)
	HANDLE submit_event;
	HANDLE done_event;
	CRITICAL_SECTION lock;
	CRITICAL_SECTION wait_lock;
#else
	pthread_mutex_t lock;
	pthread_cond_t cond;
	pthread_mutex_t wait_lock;
	pthread_cond_t wait_cond;
#endif
};

struct fy_thread_pool {
	struct fy_thread_pool_cfg cfg;
	unsigned int num_threads;
	struct fy_thread *threads;
	FY_ATOMIC(uint64_t) *freep;
	FY_ATOMIC(uint64_t) *lootp;
#if !defined(_WIN32)
	pthread_key_t key;
#else	// _WIN32
	DWORD key;
#endif
};

/* those are internal only */
int fy_thread_pool_setup(struct fy_thread_pool *tp, const struct fy_thread_pool_cfg *cfg);
void fy_thread_pool_cleanup(struct fy_thread_pool *tp);

#endif
