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
#include <stdatomic.h>
#include <pthread.h>

#ifndef __APPLE__
#include <semaphore.h>
#else
#include <dispatch/dispatch.h>
#endif

#include "fy-bit64.h"
#include "fy-align.h"

#include <libfyaml.h>

// #define FY_THREAD_DEBUG		/* define to enable debugging information to stderr */
// #define FY_THREAD_PORTABLE	/* define to use the portable implementation even on linux */

struct fy_work_pool {
	_Atomic(size_t) work_left;
#if defined(__linux__) && !defined(FY_THREAD_PORTABLE)
	_Atomic(uint32_t) done;
#elif defined(__APPLE__)
	dispatch_semaphore_t sem;
#else
	sem_t sem;
#endif
};

struct fy_thread {
	struct fy_thread_pool *tp;
	unsigned int id;
	pthread_t tid;
	_Atomic(struct fy_thread_work *)work;
	_Atomic(struct fy_thread_work *)next_work;
#if defined(__linux__) && !defined(FY_THREAD_PORTABLE)
	_Atomic(uint32_t) submit;
	_Atomic(uint32_t) done;
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
	_Atomic(uint64_t) *freep;
	_Atomic(uint64_t) *lootp;
	pthread_key_t key;
};

/* those are internal only */
int fy_thread_pool_setup(struct fy_thread_pool *tp, const struct fy_thread_pool_cfg *cfg);
void fy_thread_pool_cleanup(struct fy_thread_pool *tp);

#endif
