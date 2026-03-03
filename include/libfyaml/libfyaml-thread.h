/*
 * libfyaml-thread.h - libfyaml thread API
 * Copyright (c) 2019-2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef LIBFYAML_THREAD_H
#define LIBFYAML_THREAD_H

#include <stddef.h>

/* pull in common definitions and platform abstraction macros */
#include <libfyaml/libfyaml-util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DOC: Thread pool for parallel work execution
 *
 * This header provides a simple, portable thread pool built on POSIX threads.
 * It is used internally by the BLAKE3 hasher and the generic type system's
 * parallel map/filter/reduce operations, and is also available as a public
 * API for application use.
 *
 * Two operational modes are supported:
 *
 * **Work-stealing mode** (``FYTPCF_STEAL_MODE``): the recommended mode for
 * data-parallel loops.  Submit a batch of work items with
 * ``fy_thread_work_join()``; the pool distributes items across threads and
 * the caller participates in the execution.  About 30% faster than
 * reservation mode for typical workloads.
 *
 * **Reservation mode**: explicitly reserve a thread with
 * ``fy_thread_reserve()``, submit a single work item with
 * ``fy_thread_submit_work()``, continue doing other work in the calling
 * thread, then synchronise with ``fy_thread_wait_work()``.  Release the
 * thread afterwards with ``fy_thread_unreserve()``.
 *
 * Three convenience wrappers over ``fy_thread_work_join()`` cover the
 * most common data-parallel patterns:
 *
 * - ``fy_thread_args_join()``      — array of heterogeneous argument pointers
 * - ``fy_thread_arg_array_join()`` — flat array of equal-sized argument items
 * - ``fy_thread_arg_join()``       — same argument broadcast to N invocations
 *
 * An optional ``fy_work_check_fn`` callback lets each call site decide at
 * runtime whether a given item is worth offloading to a thread or running
 * inline.
 */

/* opaque types for the user */
struct fy_thread_pool;
struct fy_thread;
struct fy_work_pool;

/**
 * typedef fy_work_exec_fn - Work exec function
 *
 * The callback executed on work submission
 *
 * @arg: The argument to the method
 *
 */
typedef void (*fy_work_exec_fn)(void *arg);

/**
 * typedef fy_work_check_fn - Work check function
 *
 * Work checker function to decide if it's worth to
 * offload to a thread.
 *
 * @arg: The argument to the method
 *
 * Returns:
 * true if it should offload to thread, false otherwise
 *
 */
typedef bool (*fy_work_check_fn)(const void *arg);

/**
 * struct fy_thread_work - Work submitted to a thread for execution
 *
 * @fn: The execution function for this work
 * @arg: The argument to the fn
 * @wp: Used internally, must be set to NULL on entry
 *
 * This is the structure describing the work submitted
 * to a thread for execution.
 */
struct fy_thread_work {
	fy_work_exec_fn fn;
	void *arg;
	struct fy_work_pool *wp;
};

/**
 * enum fy_thread_pool_cfg_flags - Thread pool configuration flags
 *
 * These flags control the operation of the thread pool.
 * For now only the steal mode flag is defined.
 *
 * @FYTPCF_STEAL_MODE: Enable steal mode for the thread pool
 */
enum fy_thread_pool_cfg_flags {
	FYTPCF_STEAL_MODE	= FY_BIT(0),
};

/**
 * struct fy_thread_pool_cfg - thread pool configuration structure.
 *
 * Argument to the fy_thread_pool_create() method.
 *
 * @flags: Thread pool configuration flags
 * @num_threads: Number of threads, if 0 == online CPUs
 * @userdata: A userdata pointer
 */
struct fy_thread_pool_cfg {
	enum fy_thread_pool_cfg_flags flags;
	unsigned int num_threads;
	void *userdata;
};

/**
 * fy_thread_pool_create() - Create a thread pool
 *
 * Creates a thread pool with its configuration @cfg
 * The thread pool may be destroyed by a corresponding call to
 * fy_thread_pool_destroy().
 *
 * @cfg: The configuration for the thread pool
 *
 * Returns:
 * A pointer to the thread pool or NULL in case of an error.
 */
struct fy_thread_pool *
fy_thread_pool_create(const struct fy_thread_pool_cfg *cfg)
	FY_EXPORT;

/**
 * fy_thread_pool_destroy() - Destroy the given thread pool
 *
 * Destroy a thread pool created earlier via fy_thread_pool_create().
 * Note that this function will block until all threads
 * of the pool are destroyed.
 *
 * @tp: The thread pool to destroy
 */
void
fy_thread_pool_destroy(struct fy_thread_pool *tp)
	FY_EXPORT;

/**
 * fy_thread_pool_get_num_threads() - Get the number of threads
 *
 * Returns the actual number of created threads.
 *
 * @tp: The thread pool
 *
 * Returns:
 * > 0 for the number of actual threads created,
 * -1 on error
 */
int
fy_thread_pool_get_num_threads(struct fy_thread_pool *tp)
	FY_EXPORT;

/**
 * fy_thread_pool_get_cfg() - Get the configuration of a thread pool
 *
 * @tp: The thread pool
 *
 * Returns:
 * The configuration of the thread pool
 */
const struct fy_thread_pool_cfg *
fy_thread_pool_get_cfg(struct fy_thread_pool *tp)
	FY_EXPORT;

/**
 * fy_thread_reserve() - Reserve a thread from the pool.
 *
 * Reserve a thread from the pool and return it.
 * Note this is only valid for a non-work stealing thread pool.
 * You release the thread again via a call to fy_thread_unreserve.
 *
 * @tp: The thread pool
 *
 * Returns:
 * A reserved thread if not NULL, NULL if no threads are available.
 */
struct fy_thread *
fy_thread_reserve(struct fy_thread_pool *tp)
	FY_EXPORT;

/**
 * fy_thread_unreserve() - Unreserve a previously reserved thread
 *
 * Unreserve a thread previously reserved via a call to fy_thread_reserve()
 * Note this is only valid for a non-work stealing thread pool.
 *
 * @t: The thread
 */
void
fy_thread_unreserve(struct fy_thread *t)
	FY_EXPORT;

/**
 * fy_thread_submit_work() - Submit work for execution
 *
 * Submit work for execution. If successful the thread
 * will start executing the work in parallel with the
 * calling thread. You can wait for the thread to
 * terminate via a call to fy_thread_wait_work().
 * The thread must have been reserved earlier via fy_thread_reserve()
 *
 * Note this is only valid for a non-work stealing thread pool.
 *
 * @t: The thread
 * @work: The work
 *
 * Returns:
 * 0 if work has been submitted, -1 otherwise.
 */
int
fy_thread_submit_work(struct fy_thread *t, struct fy_thread_work *work)
	FY_EXPORT;

/**
 * fy_thread_wait_work() - Wait for completion of submitted work
 *
 * Wait until submitted work to the thread has finished.
 * Note this is only valid for a non-work stealing thread pool.
 *
 * @t: The thread
 *
 * Returns:
 * 0 if work finished, -1 on error.
 */
int
fy_thread_wait_work(struct fy_thread *t)
	FY_EXPORT;

/**
 * fy_thread_work_join() - Submit works for execution and wait
 *
 * Submit works for possible parallel execution. If no offloading
 * is possible at the time execute in the current context.
 * It is possible to use in both stealing and non-stealing mode
 * with the difference being that stealing mode is about 30% faster.
 *
 * @tp: The thread pool
 * @works: Pointer to an array of works sized @work_count
 * @work_count: The size of the @works array
 * @check_fn: Pointer to a check function, or NULL for no checks
 */
void
fy_thread_work_join(struct fy_thread_pool *tp,
		    struct fy_thread_work *works, size_t work_count,
		    fy_work_check_fn check_fn)
	FY_EXPORT;

/**
 * fy_thread_args_join() - Execute function in parallel using arguments as pointers
 *
 * Execute @fn possibly in parallel using the threads in the thread pool.
 * The arguments of the function are provided by the args array.
 *
 * @tp: The thread pool
 * @fn: The function to execute in parallel
 * @check_fn: Pointer to a check function, or NULL for no checks
 * @args: An args array sized @count of argument pointers
 * @count: The count of the args array items
 */
void
fy_thread_args_join(struct fy_thread_pool *tp,
		    fy_work_exec_fn fn, fy_work_check_fn check_fn,
		    void **args, size_t count)
	FY_EXPORT;

/**
 * fy_thread_arg_array_join() - Execute function in parallel using argument array
 *
 * Execute @fn possibly in parallel using the threads in the thread pool.
 * The arguments of the function are provided by the args array.
 *
 * @tp: The thread pool
 * @fn: The function to execute in parallel
 * @check_fn: Pointer to a check function, or NULL for no checks
 * @args: An args array of @argsize items
 * @argsize: The size of each argument array item
 * @count: The count of the args array items
 */
void
fy_thread_arg_array_join(struct fy_thread_pool *tp,
			 fy_work_exec_fn fn, fy_work_check_fn check_fn,
			 void *args, size_t argsize, size_t count)
	FY_EXPORT;

/**
 * fy_thread_arg_join() - Execute function in parallel with the same argument
 *
 * Execute @fn possibly in parallel using the threads in the thread pool.
 * The argument of the functions is the same.
 *
 * @tp: The thread pool
 * @fn: The function to execute in parallel
 * @check_fn: Pointer to a check function, or NULL for no checks
 * @arg: The common argument
 * @count: The count of executions
 */
void
fy_thread_arg_join(struct fy_thread_pool *tp,
		   fy_work_exec_fn fn, fy_work_check_fn check_fn,
		   void *arg, size_t count)
	FY_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* LIBFYAML_THREAD_H */
