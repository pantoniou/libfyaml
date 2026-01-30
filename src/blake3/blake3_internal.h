#ifndef BLAKE3_INTERNAL_H
#define BLAKE3_INTERNAL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stddef.h>
#include <stdint.h>

#include "blake3.h"

#include "blake3_impl.h"

#define BLAKE3_MAX_DEPTH 54

// fwd declaration of opaque types
struct blake3_host_state;
struct blake3_hasher;
struct fy_thread_pool;

// internal flags
enum blake3_flags {
	CHUNK_START         = 1 << 0,
	CHUNK_END           = 1 << 1,
	PARENT              = 1 << 2,
	ROOT                = 1 << 3,
	KEYED_HASH          = 1 << 4,
	DERIVE_KEY_CONTEXT  = 1 << 5,
	DERIVE_KEY_MATERIAL = 1 << 6,
};

typedef struct blake3_chunk_state {
	uint32_t cv[8] BLAKE3_ALIGN;
	uint8_t buf[BLAKE3_BLOCK_LEN] BLAKE3_ALIGN;
	uint64_t chunk_counter;
	uint8_t buf_len;
	uint8_t blocks_compressed;
	uint8_t flags;
} blake3_chunk_state BLAKE3_ALIGN;

typedef struct blake3_hasher {
	struct blake3_host_state *hs;
	uint32_t key[8] BLAKE3_ALIGN;
	blake3_chunk_state chunk BLAKE3_ALIGN;
	// The stack size is MAX_DEPTH + 1 because we do lazy merging. For example,
	// with 7 chunks, we have 3 entries in the stack. Adding an 8th chunk
	// requires a 4th entry, rather than merging everything down to 1, because we
	// don't know whether more input is coming. This is different from how the
	// reference implementation does things.
	uint8_t cv_stack[(BLAKE3_MAX_DEPTH + 1) * BLAKE3_OUT_LEN] BLAKE3_ALIGN;
	uint8_t cv_stack_len;
} blake3_hasher BLAKE3_ALIGN;

typedef void (*blake3_hash_many_f)(const uint8_t *const *inputs, size_t num_inputs,
				   size_t blocks, const uint32_t key[8], uint64_t counter,
				   bool increment_counter, uint8_t flags,
				   uint8_t flags_start, uint8_t flags_end, uint8_t *out);

typedef void (*blake3_compress_xof_f)(const uint32_t cv[8],
				      const uint8_t block[BLAKE3_BLOCK_LEN],
				      uint8_t block_len, uint64_t counter, uint8_t flags,
				      uint8_t out[64]);

typedef void (*blake3_compress_in_place_f)(uint32_t cv[8],
					   const uint8_t block[BLAKE3_BLOCK_LEN],
					   uint8_t block_len, uint64_t counter,
					   uint8_t flags);

typedef struct blake3_backend {
	const blake3_hasher_ops *hasher_ops;
	blake3_backend_info info;
	blake3_hash_many_f hash_many;
	blake3_compress_xof_f compress_xof;
	blake3_compress_in_place_f compress_in_place;
	void *user;	/* per backend data */
} blake3_backend;

// the state for the thread when doing compress subtree
typedef struct blake3_compress_subtree_state {
	struct blake3_hasher *self;
	// inputs
	const uint8_t *input;
	size_t input_len;
	const uint32_t *key;
	uint64_t chunk_counter;
	uint8_t flags;
	// outputs
	uint8_t *out;
	size_t n;
} blake3_compress_subtree_state;

typedef struct blake3_hash_many_common_state {
	blake3_hash_many_f hash_many;
	size_t blocks;
	const uint32_t *key;
	bool increment_counter;
	uint8_t flags, flags_start, flags_end;
} blake3_hash_many_common_state;

typedef struct blake3_hash_many_state {
	const blake3_hash_many_common_state *common;
	const uint8_t *const *inputs;
	size_t num_inputs;
	uint64_t counter;
	uint8_t *out;
} blake3_hash_many_state;

typedef struct blake3_host_state {
	blake3_host_config cfg;
	unsigned int num_cpus;
	uint64_t supported_backends;
	uint64_t detected_backends;
	uint64_t selectable_backends;

	/* backend for hash_many */
	const blake3_backend *hash_many_be;
	blake3_hash_many_f hash_many;

	/* backend for compress_xof */
	const blake3_backend *compress_xof_be;
	blake3_compress_xof_f compress_xof;

	/* backend for compress_in_place */
	const blake3_backend *compress_in_place_be;
	blake3_compress_in_place_f compress_in_place;

	const blake3_hasher_ops *hasher_ops;

	unsigned int simd_degree;
	unsigned int mt_degree;

	unsigned int num_threads;

	struct fy_thread_pool *tp;

	size_t file_io_bufsz;
	size_t mmap_min_chunk;
	size_t mmap_max_chunk;

} blake3_host_state;

int blake3_host_state_setup(blake3_host_state *hs, const blake3_host_config *cfg);
void blake3_host_state_cleanup(blake3_host_state *hs);

extern blake3_backend blake3_backends[B3BID_COUNT];

const blake3_backend *blake3_backend_select_function(uint64_t selectable_backends, blake3_func_id fid);
const blake3_backend *blake3_get_backend_by_id(blake3_backend_id id);
const blake3_backend *blake3_get_backend_by_name(const char *name);
const blake3_backend_info *blake3_get_backend_info(blake3_backend_id id);

void blake3_cpusimd_setup(unsigned int num_cpus, unsigned int mult_fact);
void blake3_cpusimd_cleanup(void);

#endif
