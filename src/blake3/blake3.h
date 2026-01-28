#ifndef BLAKE3_H
#define BLAKE3_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>

#define BLAKE3_VERSION_STRING "1.4.1"

#define BLAKE3_KEY_LEN 32
#define BLAKE3_KEY_WORDS (BLAKE3_KEY_LEN / 4)	// words are 4 bytes
#define BLAKE3_OUT_LEN 32
#define BLAKE3_OUT_WORDS (BLAKE3_OUT_LEN / 4)	// words are 4 bytes
#define BLAKE3_BLOCK_LEN 64
#define BLAKE3_CHUNK_LEN 1024

struct blake_host_state;
struct blake_hasher;

const char *blake3_version(void);

/* NOTE: maximum supported backends is 64
 * and backends are ordered by reverse order
 * of preference.
 *
 * PORTABLE is always available and is the fall-back.
 */

typedef enum {

	/* just the portable implementation */
	B3BID_PORTABLE,

	/* x86 */
	B3BID_SSE2,
	B3BID_SSE2_ASM,
	B3BID_SSE41,
	B3BID_SSE41_ASM,
	B3BID_AVX2,
	B3BID_AVX2_ASM,
	B3BID_AVX512,
	B3BID_AVX512_ASM,

	/* arm */
	B3BID_NEON,

	/* gpu */
	B3BID_VULKAN,
	B3BID_OPENCL,
	B3BID_CUDA,

	/* CPU simd (experimental) */
	B3BID_CPUSIMD,

} blake3_backend_id;

#define B3BID_COUNT	(B3BID_CPUSIMD+1)
#define B3BID_INVALID	((blake3_backend_id)-1)

#define B3BF_BIT(_x)	((uint64_t)1 << (_x))

#define B3BF_PORTABLE	B3BF_BIT(B3BID_PORTABLE)

#define B3BF_SSE2	B3BF_BIT(B3BID_SSE2)
#define B3BF_SSE2_ASM	B3BF_BIT(B3BID_SSE2_ASM)
#define B3BF_SSE41	B3BF_BIT(B3BID_SSE41)
#define B3BF_SSE41_ASM	B3BF_BIT(B3BID_SSE41_ASM)
#define B3BF_AVX2	B3BF_BIT(B3BID_AVX2)
#define B3BF_AVX2_ASM	B3BF_BIT(B3BID_AVX2_ASM)
#define B3BF_AVX512	B3BF_BIT(B3BID_AVX512)
#define B3BF_AVX512_ASM	B3BF_BIT(B3BID_AVX512_ASM)

#define B3BF_NEON	B3BF_BIT(B3BID_NEON)

#define B3BF_VULKAN	B3BF_BIT(B3BID_VULKAN)
#define B3BF_OPENCL	B3BF_BIT(B3BID_OPENCL)
#define B3BF_CUDA	B3BF_BIT(B3BID_CUDA)

#define B3BF_CPUSIMD	B3BF_BIT(B3BID_CPUSIMD)

uint64_t blake3_get_supported_backends(void);
uint64_t blake3_get_detected_backends(void);
uint64_t blake3_get_selectable_backends(void);

typedef enum {
	B3FID_HASH_MANY,
	B3FID_COMPRESS_XOF,
	B3FID_COMPRESS_IN_PLACE,
} blake3_func_id;

#define B3FID_COUNT	(B3FID_COMPRESS_IN_PLACE+1)
#define B3FID_INVALID	((blake3_func_id)-1)

#define B3FF_BIT(_x)	((uint64_t)1 << (_x))

#define B3FF_HASH_MANY		B3FF_BIT(B3FID_HASH_MANY)
#define B3FF_COMPRESS_XOF	B3FF_BIT(B3FID_COMPRESS_XOF)
#define B3FF_COMPRESS_IN_PLACE	B3FF_BIT(B3FID_COMPRESS_IN_PLACE)

typedef struct blake3_backend_info {
	blake3_backend_id id;
	const char *name;
	const char *description;
	unsigned int simd_degree;
	uint64_t funcs;
} blake3_backend_info;

const blake3_backend_info *blake3_get_backend_info(blake3_backend_id id);

typedef struct blake3_host_config {
	bool debug;
	bool no_mthread;
	bool no_mmap;
	unsigned int num_threads;
	unsigned int mt_degree;		// number of chunks to be worth spinning up a thread
	const char *backend;		// backend selection string, or NULL for default
	size_t file_io_bufsz;		// buffer size when doing file I/O
	size_t mmap_min_chunk;		// minimum chunk size for mmap
	size_t mmap_max_chunk;		// maximum chunk size for mmap
	struct fy_thread_pool *tp;	// use this thread pool instead of spinning one up
} blake3_host_config;

struct blake3_host_state *blake3_host_state_create(const blake3_host_config *cfg);
void blake3_host_state_destroy(struct blake3_host_state *hs);

struct blake3_hasher *blake3_hasher_create(struct blake3_host_state *hs, const uint8_t *key, const void *context, size_t context_len);
void blake3_hasher_destroy(struct blake3_hasher *self);

/* simple optimized method for file hashing */
int blake3_hash_file(struct blake3_hasher *hasher, const char *filename, uint8_t output[BLAKE3_OUT_LEN]);
void blake3_hash(struct blake3_hasher *hasher, const void *mem, size_t size, uint8_t output[BLAKE3_OUT_LEN]);

/* experimental CPUSIMD enable */
int blake3_backend_cpusimd_setup(unsigned int num_cpus, unsigned int mult_fact);
void blake3_backend_cpusimd_cleanup(void);

void blake3_hasher_init(struct blake3_host_state *hs, struct blake3_hasher *self);
void blake3_hasher_init_keyed(struct blake3_host_state *hs, struct blake3_hasher *self, const uint8_t key[BLAKE3_KEY_LEN]);
void blake3_hasher_init_derive_key(struct blake3_host_state *hs, struct blake3_hasher *self, const char *context);
void blake3_hasher_init_derive_key_raw(struct blake3_host_state *hs, struct blake3_hasher *self, const void *context, size_t context_len);

void blake3_hasher_update(struct blake3_hasher *self, const void *input, size_t input_len);
void blake3_hasher_finalize(const struct blake3_hasher *self, uint8_t *out, size_t out_len);
void blake3_hasher_finalize_seek(const struct blake3_hasher *self, uint64_t seek, uint8_t *out, size_t out_len);
void blake3_hasher_reset(struct blake3_hasher *self);

typedef struct blake3_hasher_ops {
	void (*hasher_init)(struct blake3_host_state *hs, struct blake3_hasher *self);
	void (*hasher_init_keyed)(struct blake3_host_state *hs, struct blake3_hasher *self, const uint8_t key[BLAKE3_KEY_LEN]);
	void (*hasher_init_derive_key)(struct blake3_host_state *hs, struct blake3_hasher *self, const char *context);
	void (*hasher_init_derive_key_raw)(struct blake3_host_state *hs, struct blake3_hasher *self, const void *context, size_t context_len);

	void (*hasher_update)(struct blake3_hasher *self, const void *input, size_t input_len);
	void (*hasher_finalize)(const struct blake3_hasher *self, uint8_t *out, size_t out_len);
	void (*hasher_finalize_seek)(const struct blake3_hasher *self, uint64_t seek, uint8_t *out, size_t out_len);
	void (*hasher_reset)(struct blake3_hasher *self);
} blake3_hasher_ops;

#endif /* BLAKE3_H */
