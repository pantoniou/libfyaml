/*
 * libfyaml-blake3.h - libfyaml blake3 API
 * Copyright (c) 2019-2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef LIBFYAML_BLAKE3_H
#define LIBFYAML_BLAKE3_H

#include <stdint.h>
#include <stddef.h>

/* pull in common definitions and platform abstraction macros */
#include <libfyaml/libfyaml-util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DOC: BLAKE3 cryptographic hashing
 *
 * This header exposes libfyaml's embedded BLAKE3 hasher.  BLAKE3 is a
 * modern, highly parallelisable cryptographic hash function producing
 * 256-bit (32-byte) output.
 *
 * Three hashing modes are supported, selected via
 * ``struct fy_blake3_hasher_cfg`` at creation time:
 *
 * - **Standard**: plain BLAKE3 hash (default when key and context are NULL)
 * - **Keyed**: MAC-like hash using a 32-byte key
 * - **Key derivation**: derive a subkey from an application context string
 *
 * The hasher can be used in streaming fashion (``update`` / ``finalize``)
 * or for one-shot hashing of memory regions (``fy_blake3_hash()``) and
 * files (``fy_blake3_hash_file()``).  File hashing uses ``mmap`` by
 * default for large files and can be further parallelised via a thread pool.
 *
 * Runtime SIMD backend selection is automatic: the best available backend
 * (SSE2, SSE4.1, AVX2, AVX512 on x86; NEON on ARM; portable C otherwise)
 * is used unless a specific backend name is requested in the config.
 * Use ``fy_blake3_backend_iterate()`` to enumerate available backends.
 *
 * The hasher object is reusable: call ``fy_blake3_hasher_reset()`` to
 * start a new hash without reallocating the object.
 */

/* BLAKE3 key length */
#define FY_BLAKE3_KEY_LEN 32
/* BLAKE3 output length */
#define FY_BLAKE3_OUT_LEN 32

/* opaque BLAKE3 hasher type for the user*/
struct fy_blake3_hasher;

/**
 * fy_blake3_backend_iterate() - Iterate over the supported BLAKE3 backends
 *
 * This method iterates over the supported BLAKE3 backends.
 * The start of the iteration is signalled by a NULL in \*prevp.
 *
 * The default backend is always the last in sequence, so for
 * example if the order is [ "portable", "sse2", NULL ] the
 * default is "sse2".
 *
 * @prevp: The previous backend pointer, or NULL at start
 *
 * Returns:
 * The next backend or NULL at the end.
 */
const char *
fy_blake3_backend_iterate(const char **prevp)
	FY_EXPORT;

/**
 * struct fy_blake3_hasher_cfg - BLAKE3 hasher configuration
 *
 * Argument to the fy_blake3_hasher_create() method which
 * is the fyaml's user facing BLAKE3 API.
 * It is very minimal, on purpose, since it's meant to be
 * exposing a full blown BLAKE3 API.
 *
 * @backend: NULL for default, or a specific backend name
 * @file_buffer: Use this amount of buffer for buffering, zero for default
 * @mmap_min_chunk: Minimum chunk size for mmap case
 * @mmap_max_chunk: Maximum chunk size for mmap case
 * @no_mmap: Disable mmap for file access
 * @key: pointer to a FY_BLAKE3_KEY_LEN area when in keyed mode.
 *       NULL otherwise.
 * @context: pointer to a context when in key derivation mode.
 *           NULL otherwise.
 * @context_len: The size of the context when in key derivation mode.
 *               0 otherwise.
 * @tp: The thread pool to use, if NULL, create a private one
 * @num_threads: Number of threads to use
 *               - 0 means default: NUM_CPUS * 3 / 2
 *               - > 0 specific number of threads
 *               - -1 disable threading entirely
 */
struct fy_blake3_hasher_cfg {
	const char *backend;
	size_t file_buffer;
	size_t mmap_min_chunk;
	size_t mmap_max_chunk;
	bool no_mmap;
	const uint8_t *key;
	const void *context;
	size_t context_len;
	struct fy_thread_pool *tp;
	int num_threads;
};

/**
 * fy_blake3_hasher_create() - Create a BLAKE3 hasher object.
 *
 * Creates a BLAKE3 hasher with its configuration @cfg
 * The hasher may be destroyed by a corresponding call to
 * fy_blake3_hasher_destroy().
 *
 * @cfg: The configuration for the BLAKE3 hasher
 *
 * Returns:
 * A pointer to the BLAKE3 hasher or NULL in case of an error.
 */
struct fy_blake3_hasher *
fy_blake3_hasher_create(const struct fy_blake3_hasher_cfg *cfg)
	FY_EXPORT;

/**
 * fy_blake3_hasher_destroy() - Destroy the given BLAKE3 hasher
 *
 * Destroy a BLAKE3 hasher created earlier via fy_blake3_hasher_create().
 *
 * @fyh: The BLAKE3 hasher to destroy
 */
void
fy_blake3_hasher_destroy(struct fy_blake3_hasher *fyh)
	FY_EXPORT;

/**
 * fy_blake3_hasher_update() - Update the BLAKE3 hasher state with the given input
 *
 * Updates the BLAKE3 hasher state by hashing the given input.
 *
 * @fyh: The BLAKE3 hasher
 * @input: Pointer to the input
 * @input_len: Size of the input in bytes
 */
void
fy_blake3_hasher_update(struct fy_blake3_hasher *fyh, const void *input, size_t input_len)
	FY_EXPORT;

/**
 * fy_blake3_hasher_finalize() - Finalize the hash and get output
 *
 * Finalizes the BLAKE3 hasher and returns the output
 *
 * @fyh: The BLAKE3 hasher
 *
 * Returns:
 * A pointer to the BLAKE3 output (sized FY_BLAKE3_OUT_LEN), or NULL
 * in case of an error.
 */
const uint8_t *
fy_blake3_hasher_finalize(struct fy_blake3_hasher *fyh)
	FY_EXPORT;

/**
 * fy_blake3_hasher_reset() - Resets the hasher
 *
 * Resets the hasher for re-use
 *
 * @fyh: The BLAKE3 hasher
 */
void
fy_blake3_hasher_reset(struct fy_blake3_hasher *fyh)
	FY_EXPORT;

/**
 * fy_blake3_hash() - BLAKE3 hash a memory area
 *
 * Hash a memory area and return the BLAKE3 output.
 *
 * @fyh: The BLAKE3 hasher
 * @mem: Pointer to the memory to use
 * @size: The size of the memory in bytes
 *
 * Returns:
 * A pointer to the BLAKE3 output (sized FY_BLAKE3_OUT_LEN), or NULL
 * in case of an error.
 */
const uint8_t *
fy_blake3_hash(struct fy_blake3_hasher *fyh, const void *mem, size_t size)
	FY_EXPORT;

/**
 * fy_blake3_hash_file() - BLAKE3 hash a file.
 *
 * Hash the given file (possibly using mmap)
 *
 * @fyh: The BLAKE3 hasher
 * @filename: The filename
 *
 * Returns:
 * A pointer to the BLAKE3 output (sized FY_BLAKE3_OUT_LEN), or NULL
 * in case of an error.
 */
const uint8_t *
fy_blake3_hash_file(struct fy_blake3_hasher *fyh, const char *filename)
	FY_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* LIBFYAML_BLAKE3 */
