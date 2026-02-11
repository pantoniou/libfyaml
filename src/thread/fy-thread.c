/*
 * fy-thread.c - Lighting fast thread pool implementation
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <stdlib.h>
#include <errno.h>
#include <limits.h>

#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
#endif

#if defined(__linux__)
#include <sys/syscall.h>
#include <linux/futex.h>
#endif

#include "fy-win32.h"
#include "fy-diag.h"
#include "fy-utils.h"
#include "fy-thread.h"

#undef WORK_SHUTDOWN
#define WORK_SHUTDOWN 	((struct fy_thread_work *)(void *)-1)

/* Platform-specific thread-local storage access */
#ifdef _WIN32
#define fy_thread_getspecific(key) TlsGetValue(key)
#else
#define fy_thread_getspecific(key) pthread_getspecific(key)
#endif

#ifdef FY_THREAD_DEBUG
#define TDBG(_fmt, ...) \
	do { fprintf(stderr, (_fmt) __VA_OPT__(,) __VA_ARGS__); } while(0)
#else
#define TDBG(_fmt, ...) \
	do { /* nothing */ } while(0)
#endif

#ifdef _WIN32
static DWORD WINAPI fy_worker_thread_standard(void *arg);
static DWORD WINAPI fy_worker_thread_steal(void *arg);
#else
static void *fy_worker_thread_standard(void *arg);
static void *fy_worker_thread_steal(void *arg);
#endif

static void fy_thread_work_join_standard(struct fy_thread_pool *tp, struct fy_thread_work *works, size_t work_count, fy_work_check_fn check_fn);
static void fy_thread_work_join_steal(struct fy_thread_pool *tp, struct fy_thread_work *works, size_t work_count, fy_work_check_fn check_fn);
static void fy_thread_work_join_steal_2(struct fy_thread_pool *tp, struct fy_thread_work works[2], fy_work_check_fn check_fn);

#if defined(_WIN32)

/* Windows implementation using Events and Critical Sections */

static inline void fy_thread_init_sync(struct fy_thread *t)
{
	t->submit_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	t->done_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	InitializeCriticalSection(&t->lock);
	InitializeCriticalSection(&t->wait_lock);
}

static inline struct fy_thread_work *fy_worker_wait_for_work(struct fy_thread *t)
{
	struct fy_thread_work *work;

	while ((work = fy_atomic_load(&t->work)) == NULL)
		WaitForSingleObject(t->submit_event, INFINITE);

	return work;
}

static inline void fy_worker_signal_work_done(struct fy_thread *t, struct fy_thread_work *work)
{
	struct fy_thread_work *exp_work;

	EnterCriticalSection(&t->wait_lock);

	exp_work = work;
	if (!fy_atomic_compare_exchange_strong(&t->work, &exp_work, NULL))
		assert(exp_work == WORK_SHUTDOWN);
	SetEvent(t->done_event);
	LeaveCriticalSection(&t->wait_lock);
}

static inline int fy_thread_submit_work_internal(struct fy_thread *t, struct fy_thread_work *work)
{
	struct fy_thread_work *exp_work;
	int ret;

	assert(t);
	assert(work);

	EnterCriticalSection(&t->lock);
	exp_work = NULL;
	if (!fy_atomic_compare_exchange_strong(&t->work, &exp_work, work)) {
		assert(exp_work == WORK_SHUTDOWN);
		ret = -1;
	} else {
		SetEvent(t->submit_event);
		ret = 0;
	}
	LeaveCriticalSection(&t->lock);

	return ret;
}

static inline int fy_thread_wait_work_internal(struct fy_thread *t)
{
	const struct fy_thread_work *work;

	EnterCriticalSection(&t->wait_lock);
	while ((work = fy_atomic_load(&t->work)) != NULL) {
		LeaveCriticalSection(&t->wait_lock);
		WaitForSingleObject(t->done_event, INFINITE);
		EnterCriticalSection(&t->wait_lock);
	}
	LeaveCriticalSection(&t->wait_lock);

	return 0;
}

void fy_worker_thread_shutdown(struct fy_thread *t)
{
	EnterCriticalSection(&t->lock);
	fy_atomic_store(&t->work, WORK_SHUTDOWN);
	SetEvent(t->submit_event);
	LeaveCriticalSection(&t->lock);

	WaitForSingleObject(t->tid, INFINITE);
	CloseHandle(t->tid);
	CloseHandle(t->submit_event);
	CloseHandle(t->done_event);
	DeleteCriticalSection(&t->lock);
	DeleteCriticalSection(&t->wait_lock);
}

#elif defined(__linux__) && !defined(FY_THREAD_PORTABLE)

/* linux pedal to the metal implementation */
static inline int futex(FY_ATOMIC(uint32_t) *uaddr, int futex_op, uint32_t val, const struct timespec *timeout, uint32_t *uaddr2, uint32_t val3)
{
	return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

static inline int fwait(FY_ATOMIC(uint32_t) *futexp)
{
	long s;
	uint32_t one = 1;

	while (!fy_atomic_compare_exchange_strong(futexp, &one, 0)) {
		s = futex(futexp, FUTEX_WAIT_PRIVATE, 0, NULL, NULL, 0);
		if (s == -1 && errno != EAGAIN)
			return -1;
	}
	return 0;
}

static inline int fpost(FY_ATOMIC(uint32_t) *futexp)
{
	long s;
	uint32_t zero = 0;

	if (fy_atomic_compare_exchange_strong(futexp, &zero, 1)) {
		s = futex(futexp, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
		if (s == -1)
			return -1;
	}
	return 0;
}

static inline void fy_thread_init_sync(struct fy_thread *t)
{
	/* nothing more needed for futexes */
	fy_atomic_store(&t->submit, 0);
	fy_atomic_store(&t->done, 0);
}

static inline struct fy_thread_work *fy_worker_wait_for_work(struct fy_thread *t)
{
	struct fy_thread_work *w;
	int rc __FY_DEBUG_UNUSED__;

	while ((w = fy_atomic_load(&t->work)) == NULL) {
		rc = fwait(&t->submit);
		assert(!rc);
	}
	return w;
}

static inline void fy_worker_signal_work_done(struct fy_thread *t, struct fy_thread_work *work)
{
	struct fy_thread_work *exp_work;
	bool ok __FY_DEBUG_UNUSED__;
	int rc __FY_DEBUG_UNUSED__;

	/* note that the work won't be replaced if it's a shutdown */
	exp_work = work;
	ok = fy_atomic_compare_exchange_strong(&t->work, &exp_work, NULL);
	assert(ok);

	rc = fpost(&t->done);
	assert(!rc);
}

static inline int fy_thread_submit_work_internal(struct fy_thread *t, struct fy_thread_work *work)
{
	struct fy_thread_work *exp_work;
	int rc __FY_DEBUG_UNUSED__;

	/* atomically update the work */
	exp_work = NULL;
	if (!fy_atomic_compare_exchange_strong(&t->work, &exp_work, work))
		return -1;

	rc = fpost(&t->submit);
	assert(!rc);

	return 0;
}

static inline int fy_thread_wait_work_internal(struct fy_thread *t)
{
	const struct fy_thread_work *work;

	while ((work = fy_atomic_load(&t->work)) != NULL)
		fwait(&t->done);

	fy_atomic_store(&t->done, 0);

	return 0;
}

void fy_worker_thread_shutdown(struct fy_thread *t)
{
	int rc __FY_DEBUG_UNUSED__;

	fy_atomic_store(&t->work, WORK_SHUTDOWN);
	rc = fpost(&t->submit);
	assert(!rc);

	rc = pthread_join(t->tid, NULL);
	assert(!rc);

	if (!(t->tp->cfg.flags & FYTPCF_STEAL_MODE))
		fy_atomic_store(&t->work, NULL);
}

#else

/* portable pthread implementation */

static inline void fy_thread_init_sync(struct fy_thread *t)
{
	pthread_mutex_init(&t->lock, NULL);
	pthread_cond_init(&t->cond, NULL);

	pthread_mutex_init(&t->wait_lock, NULL);
	pthread_cond_init(&t->wait_cond, NULL);
}

static inline struct fy_thread_work *fy_worker_wait_for_work(struct fy_thread *t)
{
	struct fy_thread_work *work;

	pthread_mutex_lock(&t->lock);
	while ((work = fy_atomic_load(&t->work)) == NULL)
		pthread_cond_wait(&t->cond, &t->lock);
	pthread_mutex_unlock(&t->lock);

	return work;
}

static inline void fy_worker_signal_work_done(struct fy_thread *t, struct fy_thread_work *work)
{
	struct fy_thread_work *exp_work;

	/* clear the work, so that the user knows we're done */
	pthread_mutex_lock(&t->wait_lock);

	/* note that the work won't be replaced if it's a shutdown */
	exp_work = work;
	if (!fy_atomic_compare_exchange_strong(&t->work, &exp_work, NULL))
		assert(exp_work == WORK_SHUTDOWN);
	pthread_cond_signal(&t->wait_cond);
	pthread_mutex_unlock(&t->wait_lock);
}

static inline int fy_thread_submit_work_internal(struct fy_thread *t, struct fy_thread_work *work)
{
	struct fy_thread_work *exp_work;
	int ret;

	/* atomically update the work */

	assert(t);
	assert(work);

	pthread_mutex_lock(&t->lock);
	exp_work = NULL;
	if (!fy_atomic_compare_exchange_strong(&t->work, &exp_work, work)) {
		assert(exp_work == WORK_SHUTDOWN);
		ret = -1;
	} else {
		pthread_cond_signal(&t->cond);
		ret = 0;
	}
	pthread_mutex_unlock(&t->lock);

	return ret;
}

static inline int fy_thread_wait_work_internal(struct fy_thread *t)
{
	const struct fy_thread_work *work;

	pthread_mutex_lock(&t->wait_lock);
	while ((work = fy_atomic_load(&t->work)) != NULL)
		pthread_cond_wait(&t->wait_cond, &t->wait_lock);
	pthread_mutex_unlock(&t->wait_lock);

	return 0;
}

void fy_worker_thread_shutdown(struct fy_thread *t)
{
	pthread_mutex_lock(&t->lock);
	fy_atomic_store(&t->work, WORK_SHUTDOWN);
	pthread_cond_signal(&t->cond);
	pthread_mutex_unlock(&t->lock);
	pthread_join(t->tid, NULL);
}

#endif

static inline struct fy_thread *fy_thread_reserve_internal(struct fy_thread_pool *tp)
{
	struct fy_thread *t;
	unsigned int slot;
	FY_ATOMIC(uint64_t) *free;
	uint64_t exp, v;
	unsigned int i, num_threads_words;

	t = NULL;

	num_threads_words = FY_BIT64_COUNT(tp->num_threads);
	for (i = 0, free = tp->freep; i < num_threads_words; i++, free++) {
		v = fy_atomic_load(free);
		while (v) {
			slot = FY_BIT64_LOWEST(v);
			assert(v & FY_BIT64(slot));
			exp = v;		/* expecting the previous value */
			v &= ~FY_BIT64(slot);	/* clear this bit */
			if (fy_atomic_compare_exchange_strong(free, &exp, v)) {
				slot += i * 64;
				t = tp->threads + slot;
				assert(slot == t->id);
				return t;
			}
			v = exp;
		}
	}

	return NULL;
}

static inline void fy_thread_unreserve_internal(struct fy_thread *t)
{
	struct fy_thread_pool *tp;
	FY_ATOMIC(uint64_t) *free;

	tp = t->tp;
	free = tp->freep + (unsigned int)(t->id / 64);
	fy_atomic_fetch_or(free, FY_BIT64(t->id & 63));
}

static inline bool fy_thread_is_reserved_internal(struct fy_thread *t)
{
	struct fy_thread_pool *tp = t->tp;
	FY_ATOMIC(uint64_t) *free;

	free = tp->freep + (unsigned int)(t->id / 64);
	return !(fy_atomic_load(free) & FY_BIT64(t->id & 63));
}

static inline bool fy_thread_pool_are_all_reserved_internal(struct fy_thread_pool *tp)
{
	FY_ATOMIC(uint64_t) *free;
	uint64_t v, m;
	unsigned int i, num_threads_words;

	num_threads_words = FY_BIT64_COUNT(tp->num_threads);
	for (i = 0, free = tp->freep; i < num_threads_words - 1; i++, free++) {
		v = fy_atomic_load(free);
		if (v != (uint64_t)-1)
			return false;
	}
	v = fy_atomic_load(free);
	m = FY_BIT64(tp->num_threads & 63) - 1;
	return (v & m) == m;
}

static inline bool fy_thread_pool_is_any_reserved_internal(struct fy_thread_pool *tp)
{
	FY_ATOMIC(uint64_t) *free;
	uint64_t v, m;
	unsigned int i, num_threads_words;

	num_threads_words = FY_BIT64_COUNT(tp->num_threads);
	for (i = 0, free = tp->freep; i < num_threads_words - 1; i++, free++) {
		v = fy_atomic_load(free);
		if (!v)
			return true;
	}
	v = fy_atomic_load(free);
	m = FY_BIT64(tp->num_threads & 63) - 1;
	return !(v & m);
}

struct fy_thread *fy_thread_reserve(struct fy_thread_pool *tp)
{
	if (!tp)
		return NULL;

	/* only valid for non-work stealing thread pools */
	if (tp->cfg.flags & FYTPCF_STEAL_MODE)
		return NULL;

	return fy_thread_reserve_internal(tp);
}

void fy_thread_unreserve(struct fy_thread *t)
{
	struct fy_thread_pool *tp;

	if (!t)
		return;

	tp = t->tp;
	assert(tp);

	/* only valid for non-work stealing thread pools */
	if (tp->cfg.flags & FYTPCF_STEAL_MODE)
		return;

	fy_thread_unreserve_internal(t);
}

bool fy_thread_is_reserved(struct fy_thread *t)
{
	if (!t)
		return false;

	return fy_thread_is_reserved_internal(t);
}

bool fy_thread_pool_are_all_reserved(struct fy_thread_pool *tp)
{
	return fy_thread_pool_are_all_reserved_internal(tp);
}

bool fy_thread_pool_is_any_reserved(struct fy_thread_pool *tp)
{
	return fy_thread_pool_is_any_reserved_internal(tp);
}

int fy_thread_submit_work(struct fy_thread *t, struct fy_thread_work *work)
{
	if (!t || !work)
		return -1;

	if (t->tp->cfg.flags & FYTPCF_STEAL_MODE)
		return -1;

	return fy_thread_submit_work_internal(t, work);
}

int fy_thread_wait_work(struct fy_thread *t)
{
	if (!t)
		return -1;

	if (t->tp->cfg.flags & FYTPCF_STEAL_MODE)
		return -1;

	return fy_thread_wait_work_internal(t);
}

void fy_thread_pool_cleanup(struct fy_thread_pool *tp)
{
	struct fy_thread *t;
	unsigned int i;

	if (!tp)
		return;

	if (tp->threads) {
		/* get out of steal mode */
		for (i = 0, t = tp->threads; i < tp->num_threads; i++, t++) {
			fy_worker_thread_shutdown(t);
		}

		fy_cacheline_free(tp->threads);
	}

#ifdef _WIN32
	if (tp->key != TLS_OUT_OF_INDEXES)
		TlsFree(tp->key);
#endif

	memset(tp, 0, sizeof(*tp));
}

int fy_thread_pool_setup(struct fy_thread_pool *tp, const struct fy_thread_pool_cfg *cfg)
{
	struct fy_thread *t;
	unsigned int i, num_threads, num_threads_words;
	size_t size, free_offset, loot_offset, thread_bitmask_size;
#ifdef _WIN32
	LPTHREAD_START_ROUTINE start_routine;
#else
	void *(*start_routine)(void *);
	int rc __FY_DEBUG_UNUSED__;
#endif
	long scval;

	assert(tp);

	memset(tp, 0, sizeof(*tp));

	if (!cfg) {
		tp->cfg.flags = 0;
		tp->cfg.num_threads = 0;
	} else
		tp->cfg = *cfg;

	if (!tp->cfg.num_threads) {
		scval = sysconf(_SC_NPROCESSORS_ONLN);
		assert(scval > 0);
		num_threads = (unsigned int)scval;
	} else
		num_threads = tp->cfg.num_threads;

	tp->num_threads = num_threads;

	num_threads_words = FY_BIT64_COUNT(tp->num_threads);
	thread_bitmask_size = FY_BIT64_SIZE(tp->num_threads);

	/* the size of the threads, aligned to a cache line */
	size = FY_CACHELINE_SIZE_ALIGN(sizeof(*tp->threads) * tp->num_threads);

	/* the free bitmask array offset */
	free_offset = size;
	size = FY_CACHELINE_SIZE_ALIGN(size + thread_bitmask_size);

	/* the loot bitmask array offset, aligned to a 64 byte cacheline */
	loot_offset = size;
	size = FY_CACHELINE_SIZE_ALIGN(size + thread_bitmask_size);

	/* allocate everything in one go */
	tp->threads = fy_cacheline_alloc(size);
	if (!tp->threads)
		goto err_out;

	memset(tp->threads, 0, size);

#ifdef _WIN32
	tp->key = TlsAlloc();
	assert(tp->key != TLS_OUT_OF_INDEXES);
#else
	rc = pthread_key_create(&tp->key, NULL);
	assert(!rc);
#endif

	tp->freep = (_Atomic(uint64_t) *)((char *)tp->threads + free_offset);
	tp->lootp = (_Atomic(uint64_t) *)((char *)tp->threads + loot_offset);

	/* prime the thread free */
	for (i = 0; i < num_threads_words - 1; i++)
		tp->freep[i] = (uint64_t)-1;
	tp->freep[i] = FY_BIT64(tp->num_threads & 63) - 1;

	/* the lootp's are zero */

	for (i = 0, t = tp->threads; i < tp->num_threads; i++, t++) {

		t->tp = tp;
		t->id = i;

		fy_thread_init_sync(t);
	}

	start_routine = !(tp->cfg.flags & FYTPCF_STEAL_MODE) ?
				fy_worker_thread_standard :
				fy_worker_thread_steal;

	for (i = 0, t = tp->threads; i < tp->num_threads; i++, t++) {
#ifdef _WIN32
		t->tid = CreateThread(NULL, 0, start_routine, t, 0, NULL);
		if (t->tid == NULL)
			goto err_out;
#else
		rc = pthread_create(&t->tid, NULL, start_routine, t);
		if (rc)
			goto err_out;
#endif
	}

	return 0;

err_out:
	fy_thread_pool_cleanup(tp);
	return -1;
}

struct fy_thread_pool *fy_thread_pool_create(const struct fy_thread_pool_cfg *cfg)
{
	struct fy_thread_pool *tp;
	int rc;

	tp = malloc(sizeof(*tp));
	if (!tp)
		return NULL;

	rc = fy_thread_pool_setup(tp, cfg);
	if (rc) {
		free(tp);
		return NULL;
	}

	return tp;
}

void fy_thread_pool_destroy(struct fy_thread_pool *tp)
{
	if (!tp)
		return;

	fy_thread_pool_cleanup(tp);
	free(tp);
}

int fy_thread_pool_get_num_threads(struct fy_thread_pool *tp)
{
	if (!tp)
		return -1;

	return (int)tp->num_threads;
}

const struct fy_thread_pool_cfg *fy_thread_pool_get_cfg(struct fy_thread_pool *tp)
{
	if (!tp)
		return NULL;
	return &tp->cfg;
}

void fy_thread_work_join(struct fy_thread_pool *tp, struct fy_thread_work *works, size_t work_count, fy_work_check_fn check_fn)
{
	if (!(tp->cfg.flags & FYTPCF_STEAL_MODE))
		fy_thread_work_join_standard(tp, works, work_count, check_fn);
	else if (work_count == 2)
		fy_thread_work_join_steal_2(tp, works, check_fn);
	else
		fy_thread_work_join_steal(tp, works, work_count, check_fn);
}

void fy_thread_args_join(struct fy_thread_pool *tp, fy_work_exec_fn fn, fy_work_check_fn check_fn, void **args, size_t count)
{
	struct fy_thread_work *works;
	size_t i;

	if (!count)
		return;

	works = alloca(sizeof(*works) * count);
	memset(works, 0, sizeof(*works) * count);
	for (i = 0; i < count; i++) {
		works[i].fn = fn;
		works[i].arg = args ? args[i] : NULL;
	}

	fy_thread_work_join(tp, works, count, check_fn);
}

void fy_thread_arg_array_join(struct fy_thread_pool *tp, fy_work_exec_fn fn, fy_work_check_fn check_fn, void *args, size_t argsize, size_t count)
{
	struct fy_thread_work *works;
	size_t i;

	if (!count)
		return;

	works = alloca(sizeof(*works) * count);
	memset(works, 0, sizeof(*works) * count);
	for (i = 0; i < count; i++) {
		works[i].fn = fn;
		works[i].arg = args;
		args = (char *)args + argsize;
	}

	fy_thread_work_join(tp, works, count, check_fn);
}

void fy_thread_arg_join(struct fy_thread_pool *tp, fy_work_exec_fn fn, fy_work_check_fn check_fn, void *arg, size_t count)
{
	struct fy_thread_work *works;
	size_t i;

	if (!count)
		return;

	works = alloca(sizeof(*works) * count);
	memset(works, 0, sizeof(*works) * count);
	for (i = 0; i < count; i++) {
		works[i].fn = fn;
		works[i].arg = arg;
	}

	fy_thread_work_join(tp, works, count, check_fn);
}

/*
 * the standard (non-stealing implementation)
 */
#ifdef _WIN32
static DWORD WINAPI fy_worker_thread_standard(void *arg)
#else
static void *fy_worker_thread_standard(void *arg)
#endif
{
	struct fy_thread *t = arg;
	struct fy_thread_pool *tp;
	struct fy_thread_work *work;

	tp = t->tp;

	/* store per thread info */
#ifdef _WIN32
	TlsSetValue(tp->key, t);
#else
	pthread_setspecific(tp->key, t);
#endif

	while ((work = fy_worker_wait_for_work(t)) != WORK_SHUTDOWN) {
		work->fn(work->arg);
		fy_worker_signal_work_done(t, work);
	}

#ifdef _WIN32
	return 0;
#else
	return NULL;
#endif
}

static void fy_thread_work_join_standard(struct fy_thread_pool *tp, struct fy_thread_work *works, size_t work_count, fy_work_check_fn check_fn)
{
	struct fy_thread_work **direct_work, **thread_work, *w;
	struct fy_thread **threads, *t;
	size_t i, direct_work_count, thread_work_count;
	int rc;

	/* just a single (or no) work, or no threads? execute directly */
	if (work_count <= 1 || !tp || !tp->num_threads) {
		for (i = 0, w = works; i < work_count; i++, w++)
			w->fn(w->arg);
		return;
	}

	/* allocate the keeper of direct work */
	direct_work = alloca(work_count * sizeof(*direct_work));
	direct_work_count = 0;

	threads = alloca(work_count * sizeof(*threads));
	thread_work = alloca(work_count * sizeof(*thread_work));
	thread_work_count = 0;

	for (i = 0, w = works; i < work_count; i++, w++) {

		t = NULL;
		if (!check_fn || check_fn(w->arg))
			t = fy_thread_reserve_internal(tp);

		if (t) {
			threads[thread_work_count] = t;
			thread_work[thread_work_count++] = w;
		} else
			direct_work[direct_work_count++] = w;
	}

	/* if we don't have any direct_work, steal the last threaded work as direct */
	if (!direct_work_count) {
		assert(thread_work_count > 0);
		t = threads[thread_work_count - 1];
		w = thread_work[thread_work_count - 1];
		thread_work_count--;

		/* unreserve this */
		fy_thread_unreserve_internal(t);
		direct_work[direct_work_count++] = w;
	}

	/* submit the threaded work */
	for (i = 0; i < thread_work_count; i++) {
		t = threads[i];
		w = thread_work[i];
		rc = fy_thread_submit_work_internal(t, w);
		if (rc) {
			/* unable to submit? remove work, and move to direct */
			threads[i] = NULL;
			thread_work[i] = NULL;
			fy_thread_unreserve_internal(t);
			direct_work[direct_work_count++] = w;
		}
	}

	/* now perform the direct work while the threaded work is being performed in parallel */
	for (i = 0; i < direct_work_count; i++) {
		w = direct_work[i];
		w->fn(w->arg);
	}

	/* finally wait for all threaded work to complete */
	for (i = 0; i < thread_work_count; i++) {
		t = threads[i];
		assert(t);
		fy_thread_wait_work_internal(t);
		fy_thread_unreserve_internal(t);
	}
}

/*
 * the stealing implementation
 */
static inline struct fy_work_pool *fy_work_pool_init(struct fy_work_pool *wp, size_t work_count)
{
#if !(defined(__linux__) && !defined(FY_THREAD_PORTABLE)) && !defined(_WIN32)
	int rc __FY_DEBUG_UNUSED__;
#endif

	if (!wp)
		return NULL;

	fy_atomic_store(&wp->work_left, work_count);
#if defined(__linux__) && !defined(FY_THREAD_PORTABLE)
	fy_atomic_store(&wp->done, !work_count);
#elif defined(__APPLE__)
	wp->sem = dispatch_semaphore_create(!work_count);
        assert(wp->sem != NULL);
	(void)rc;
#elif defined(_WIN32)
	wp->sem = CreateSemaphore(NULL, !work_count, LONG_MAX, NULL);
	assert(wp->sem != NULL);
#else
	rc = sem_init(&wp->sem, 0, !work_count);
	assert(!rc);
#endif
	return wp;
}

static inline void fy_work_pool_cleanup(struct fy_work_pool *wp)
{
#if !(defined(__linux__) && !defined(FY_THREAD_PORTABLE)) && !defined(_WIN32)
	int rc __FY_DEBUG_UNUSED__;
#endif

	if (!wp)
		return;

#if defined(_WIN32)
	CloseHandle(wp->sem);
#elif defined(__linux__) && !defined(FY_THREAD_PORTABLE)
	/* nothing */
#elif defined(__APPLE__)
	/* nothing */
	(void)rc;
#else
	rc = sem_destroy(&wp->sem);
	assert(!rc);
#endif
}

static inline bool fy_work_pool_signal(struct fy_work_pool *wp)
{
	size_t prev_work_left;
#if !defined(_WIN32)
	int rc __FY_DEBUG_UNUSED__;
#endif

	if (!wp)
		return false;

	prev_work_left = fy_atomic_fetch_sub(&wp->work_left, 1);
	if (prev_work_left == 1) {
#if defined(_WIN32)
		ReleaseSemaphore(wp->sem, 1, NULL);
#elif defined(__linux__) && !defined(FY_THREAD_PORTABLE)
		rc = fpost(&wp->done);
		assert(!rc);
#elif defined(__APPLE__)
		dispatch_semaphore_signal(wp->sem);
		(void)rc;
#else
		rc = sem_post(&wp->sem);
		assert(!rc);
#endif
		return true;
	}
	return false;
}

static inline void fy_work_pool_wait(struct fy_work_pool *wp)
{
#if !defined(_WIN32)
	int rc __FY_DEBUG_UNUSED__;
#else
	DWORD rc __FY_DEBUG_UNUSED__;
#endif

	if (!wp)
		return;

	/* if there's any work left, wait for it */
	while (fy_atomic_load(&wp->work_left) > 0) {
#if defined(__linux__) && !defined(FY_THREAD_PORTABLE)
		rc = fwait(&wp->done);
		assert(!rc || (rc == -1 && errno == EAGAIN));
#elif defined(__APPLE__)
		dispatch_semaphore_wait(wp->sem, DISPATCH_TIME_FOREVER);
		rc = 0;
#elif defined(_WIN32)
		rc = WaitForSingleObject(wp->sem, INFINITE);
		assert(rc == WAIT_OBJECT_0);
#else
		rc = sem_wait(&wp->sem);
		assert(!rc || (rc == -1 && errno == EAGAIN));
#endif
	}
}

static inline void fy_worker_thread_steal_execute(struct fy_thread *t, struct fy_thread_work *w)
{
	struct fy_work_pool *wp;
	bool signalled __FY_DEBUG_UNUSED__;

	TDBG("%s: T#%u worker executing W:%p\n", __func__, t->id, w);
	wp = w->wp;
	assert(wp);
	w->fn(w->arg);
	TDBG("%s: T#%u worker executed W:%p\n", __func__, t->id, w);

	signalled = fy_work_pool_signal(wp);
	(void)signalled;
	TDBG("%s: T#%u W:%p WP:%p signalled=%s\n", __func__,
			t->id, w, wp, signalled ? "true" : "false");
}

static inline struct fy_thread_work *fy_worker_thread_steal_work(struct fy_thread_pool *tp, struct fy_thread *t_thief)
{
	struct fy_thread *t;
	unsigned int slot;
	FY_ATOMIC(uint64_t) *loot;
	uint64_t exp, v;
	unsigned int i, num_threads_words;
	struct fy_thread_work *w, *w_exp;

	/* the threads that have work to steal, have the loot bit set */
	t = NULL;

	num_threads_words = FY_BIT64_COUNT(tp->num_threads);
	for (i = 0, loot = tp->lootp; i < num_threads_words; i++, loot++) {
		v = fy_atomic_load(loot);
		while (v) {
			slot = FY_BIT64_LOWEST(v);
			assert(v & FY_BIT64(slot));
			exp = v;		/* expecting the previous value */
			v &= ~FY_BIT64(slot);	/* clear this bit */

			if (fy_atomic_compare_exchange_strong(loot, &exp, v)) {
				slot += i * 64;
				t = tp->threads + slot;
				if ((w = fy_atomic_load(&t->next_work)) != NULL) {
					w_exp = w;
					if (fy_atomic_compare_exchange_strong(&t->next_work, &w_exp, NULL))
						return w;
				}
			}
			v = exp;
		}
	}

	return NULL;
}

#ifdef _WIN32
static DWORD WINAPI fy_worker_thread_steal(void *arg)
#else
static void *fy_worker_thread_steal(void *arg)
#endif
{
	struct fy_thread *t = arg;
	struct fy_thread_pool *tp;
	struct fy_thread_work *w, *w_exp, *w_stolen, *w_last;

	tp = t->tp;

	/* store per thread info */
#ifdef _WIN32
	TlsSetValue(tp->key, t);
#else
	pthread_setspecific(tp->key, t);
#endif

	TDBG("%s: T#%u in steal mode\n", __func__, t->id);

	while ((w = fy_worker_wait_for_work(t)) != WORK_SHUTDOWN) {

		assert(fy_thread_is_reserved_internal(t));

		for (;;) {
			fy_worker_thread_steal_execute(t, w);
			w_last = w;
			w_stolen = fy_worker_thread_steal_work(tp, t);
			w = NULL;
			if (!w_stolen)
				break;

			TDBG("%s: T#%u stole W:%p\n", __func__, t->id, w_stolen);
			w_exp = w_last;
			if (!fy_atomic_compare_exchange_strong(&t->work, &w_exp, w_stolen)) {
				assert(w_exp != WORK_SHUTDOWN);
				TDBG("%s: T#%u t->work:%p w:%p w_stolen:%p\n", __func__, t->id, fy_atomic_load(&t->work), w, w_stolen);
				FY_IMPOSSIBLE_ABORT();
			}
			w = w_stolen;
		}

		/* unreserve first */
		fy_thread_unreserve_internal(t);

		w_exp = w_last;
		if (!fy_atomic_compare_exchange_strong(&t->work, &w_exp, NULL))
			break;
	}
	TDBG("%s: T#%u leaving steal mode\n", __func__, t->id);

#ifdef _WIN32
	return 0;
#else
	return NULL;
#endif
}

static void fy_thread_work_join_steal(struct fy_thread_pool *tp, struct fy_thread_work *works, size_t work_count, fy_work_check_fn check_fn)
{
	struct fy_work_pool wp_local, *wp;
	struct fy_thread_work *dw, *expw;
	struct fy_thread *t, *tw;
	bool has_loot, resolved_t;
	int rc __FY_DEBUG_UNUSED__;
	int tid __FY_DEBUG_UNUSED__;

	t = NULL;
	resolved_t = false;
	tid = -1;
	(void)tid;
	(void)rc;

#ifdef FY_THREAD_DEBUG
#ifdef _WIN32
	t = TlsGetValue(tp->key);
#else
	t = fy_thread_getspecific(tp->key);
#endif
	tid = t ? (int)t->id : -1;
	resolved_t = true;
#else
	t = NULL;
	tid = -1;
	resolved_t = false;
#endif

	dw = NULL;
	wp = NULL;

	while (work_count > 0) {
		if (!dw) {
			dw = works++;
			work_count--;

			TDBG("%s: T#%d sdir W:%p\n", __func__, tid, dw);

			continue;
		}

		has_loot = false;
		if (work_count > 0 && (!check_fn || check_fn(works->arg))) {
			while (work_count > 0 && (tw = fy_thread_reserve_internal(tp)) != NULL) {

				assert(!works->wp);

				if (!wp)
					wp = fy_work_pool_init(&wp_local, work_count + !!dw);

				works->wp = wp;
				expw = NULL;

				rc = fy_thread_submit_work_internal(tw, works);
				assert(!rc);

				TDBG("%s: T#%d post W:%p to T#%u\n", __func__, tid, works, tw->id);

				works++;
				work_count--;
			}

			if (work_count > 0) {

				if (!resolved_t) {
					t = fy_thread_getspecific(tp->key);
					tid = t ? (int)t->id : -1;
					resolved_t = true;
				}

				if (t && fy_atomic_load(&t->next_work) == NULL) {
					TDBG("%s: T#%d could not post, available to steal W:%p\n", __func__, tid, works);
					assert(works->wp == NULL);

					if (!wp)
						wp = fy_work_pool_init(&wp_local, work_count + !!dw);

					works->wp = wp;

					expw = NULL;
					if (!fy_atomic_compare_exchange_strong(&t->next_work, &expw, works)) {
						TDBG("%s: T#%d could not update next_work: W:%p\n", __func__, tid, works);
						abort();
					}

					/* set the has loot bit */
					fy_atomic_fetch_or(tp->lootp + (unsigned int)(t->id / 64), FY_BIT64(t->id & 63));

					has_loot = true;
				}
			}
		}

		if (dw) {
			TDBG("%s: T#%d exec W:%p\n", __func__, tid, dw);

			/* execute the direct work */
			dw->fn(dw->arg);
			dw = NULL;

			fy_work_pool_signal(wp);
		}

		if (has_loot) {
			assert(t);
			expw = works;

			/* clear the has loot bit unconditionally */
			fy_atomic_fetch_and(tp->lootp + (unsigned int)(t->id / 64), ~FY_BIT64(t->id & 63));

			if (!fy_atomic_compare_exchange_strong(&t->next_work, &expw, NULL)) {
				TDBG("%s: T#%d had W:%p stolen, good\n", __func__, tid, works);
				work_count--;
				works++;
			} else {
				TDBG("%s: T#%d had W:%p not stolen, bad\n", __func__, tid, works);
			}
		}
	}

	/* last out and direct work */
	if (dw) {
		TDBG("%s: T#%d executing final direct W:%p\n", __func__, tid, dw);

		dw->fn(dw->arg);
		dw = NULL;

		fy_work_pool_signal(wp);
	}

	TDBG("%s: T#%d wait WP:%p\n", __func__, tid, wp);

	fy_work_pool_wait(wp);
	fy_work_pool_cleanup(wp);

	TDBG("%s: T#%d done WP:%p\n", __func__, tid, wp);
}

static void fy_thread_work_join_steal_2(struct fy_thread_pool *tp, struct fy_thread_work works[2], fy_work_check_fn check_fn)
{
	struct fy_work_pool wp_local, *wp;
	struct fy_thread_work *expw;
	struct fy_thread *t, *tw;
	bool has_loot, pushed, resolved_t;
	int rc __FY_DEBUG_UNUSED__;
	int tid __FY_DEBUG_UNUSED__;

	t = NULL;
	resolved_t = false;
	tid = -1;

	(void)tid;

#ifdef FY_THREAD_DEBUG
#ifdef _WIN32
	t = TlsGetValue(tp->key);
#else
	t = fy_thread_getspecific(tp->key);
#endif
	tid = t ? (int)t->id : -1;
	resolved_t = true;
#else
	t = NULL;
	tid = -1;
	resolved_t = false;
#endif

	wp = NULL;

	pushed = false;
	has_loot = false;
	if (!check_fn || check_fn(works->arg)) {
		if ((tw = fy_thread_reserve_internal(tp)) != NULL) {

			assert(!works[1].wp);

			if (!wp)
				wp = fy_work_pool_init(&wp_local, 1);

			works[1].wp = wp;
			expw = NULL;

			rc = fy_thread_submit_work_internal(tw, &works[1]);
			assert(!rc);

			TDBG("%s: T#%d post W:%p to T#%u\n", __func__, tid, &works[1], tw->id);

			pushed = true;

		} else {
			if (!resolved_t) {
				t = fy_thread_getspecific(tp->key);
				tid = t ? (int)t->id : -1;
				resolved_t = true;
			}

			if (t && fy_atomic_load(&t->next_work) == NULL) {
				TDBG("%s: T#%d could not post, available to steal W:%p\n", __func__, tid, &works[1]);
				assert(works[1].wp == NULL);

				if (!wp)
					wp = fy_work_pool_init(&wp_local, 1);

				works[1].wp = wp;

				expw = NULL;
				if (!fy_atomic_compare_exchange_strong(&t->next_work, &expw, &works[1])) {
					TDBG("%s: T#%d could not update next_work: W:%p\n", __func__, tid, &works[1]);
					abort();
				}

				/* set the has loot bit */
				fy_atomic_fetch_or(tp->lootp + (unsigned int)(t->id / 64), FY_BIT64(t->id & 63));

				has_loot = true;
			}
		}
	}

	TDBG("%s: T#%d exec W:%p (left)\n", __func__, tid, &works[0]);

	/* execute the direct work */
	works[0].fn(works[0].arg);

	if (has_loot) {
		assert(t);
		expw = &works[1];

		/* clear the has loot bit unconditionally */
		fy_atomic_fetch_and(tp->lootp + (unsigned int)(t->id / 64), ~FY_BIT64(t->id & 63));

		if (!fy_atomic_compare_exchange_strong(&t->next_work, &expw, NULL)) {
			TDBG("%s: T#%d had W:%p stolen\n", __func__, tid, &works[1]);
		} else {
			TDBG("%s: T#%d had W:%p not stolen\n", __func__, tid, &works[1]);

			TDBG("%s: T#%d exec W:%p (right)\n", __func__, tid, &works[1]);
			/* execute the direct work */
			works[1].fn(works[1].arg);
			fy_work_pool_signal(wp);
		}

	} else if (!pushed) {

		TDBG("%s: T#%d exec W:%p (right)\n", __func__, tid, &works[1]);
		/* execute the direct work */
		works[1].fn(works[1].arg);
		fy_work_pool_signal(wp);
	}

	TDBG("%s: T#%d wait WP:%p\n", __func__, tid, wp);

	fy_work_pool_wait(wp);
	fy_work_pool_cleanup(wp);

	TDBG("%s: T#%d done WP:%p\n", __func__, tid, wp);
}
