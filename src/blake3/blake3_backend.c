#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <stdlib.h>

#include "fy-bit64.h"

#include "blake3.h"
#include "blake3_impl.h"
#include "blake3_internal.h"

// Declarations for implementation-specific functions.
void blake3_compress_in_place_portable(uint32_t cv[8],
                                       const uint8_t block[BLAKE3_BLOCK_LEN],
                                       uint8_t block_len, uint64_t counter,
                                       uint8_t flags);

void blake3_compress_xof_portable(const uint32_t cv[8],
                                  const uint8_t block[BLAKE3_BLOCK_LEN],
                                  uint8_t block_len, uint64_t counter,
                                  uint8_t flags, uint8_t out[64]);

void blake3_hash_many_portable(const uint8_t *const *inputs, size_t num_inputs,
                               size_t blocks, const uint32_t key[8],
                               uint64_t counter, bool increment_counter,
                               uint8_t flags, uint8_t flags_start,
                               uint8_t flags_end, uint8_t *out);

/* the portable hasher (can drive the optimized ones, but not optimally) */
extern const blake3_hasher_ops blake3_hasher_op_portable;

#if defined(IS_X86)
void blake3_compress_in_place_sse2(uint32_t cv[8],
                                   const uint8_t block[BLAKE3_BLOCK_LEN],
                                   uint8_t block_len, uint64_t counter,
                                   uint8_t flags);
void blake3_compress_xof_sse2(const uint32_t cv[8],
                              const uint8_t block[BLAKE3_BLOCK_LEN],
                              uint8_t block_len, uint64_t counter,
                              uint8_t flags, uint8_t out[64]);
void blake3_hash_many_sse2(const uint8_t *const *inputs, size_t num_inputs,
                           size_t blocks, const uint32_t key[8],
                           uint64_t counter, bool increment_counter,
                           uint8_t flags, uint8_t flags_start,
                           uint8_t flags_end, uint8_t *out);
void blake3_compress_in_place_sse2_asm(uint32_t cv[8],
                                   const uint8_t block[BLAKE3_BLOCK_LEN],
                                   uint8_t block_len, uint64_t counter,
                                   uint8_t flags);
void blake3_compress_xof_sse2_asm(const uint32_t cv[8],
                              const uint8_t block[BLAKE3_BLOCK_LEN],
                              uint8_t block_len, uint64_t counter,
                              uint8_t flags, uint8_t out[64]);
void blake3_hash_many_sse2_asm(const uint8_t *const *inputs, size_t num_inputs,
                           size_t blocks, const uint32_t key[8],
                           uint64_t counter, bool increment_counter,
                           uint8_t flags, uint8_t flags_start,
                           uint8_t flags_end, uint8_t *out);

extern const blake3_hasher_ops blake3_hasher_op_sse2;

void blake3_compress_in_place_sse41(uint32_t cv[8],
                                    const uint8_t block[BLAKE3_BLOCK_LEN],
                                    uint8_t block_len, uint64_t counter,
                                    uint8_t flags);
void blake3_compress_xof_sse41(const uint32_t cv[8],
                               const uint8_t block[BLAKE3_BLOCK_LEN],
                               uint8_t block_len, uint64_t counter,
                               uint8_t flags, uint8_t out[64]);
void blake3_hash_many_sse41(const uint8_t *const *inputs, size_t num_inputs,
                            size_t blocks, const uint32_t key[8],
                            uint64_t counter, bool increment_counter,
                            uint8_t flags, uint8_t flags_start,
                            uint8_t flags_end, uint8_t *out);
void blake3_compress_in_place_sse41_asm(uint32_t cv[8],
                                    const uint8_t block[BLAKE3_BLOCK_LEN],
                                    uint8_t block_len, uint64_t counter,
                                    uint8_t flags);
void blake3_compress_xof_sse41_asm(const uint32_t cv[8],
                               const uint8_t block[BLAKE3_BLOCK_LEN],
                               uint8_t block_len, uint64_t counter,
                               uint8_t flags, uint8_t out[64]);
void blake3_hash_many_sse41_asm(const uint8_t *const *inputs, size_t num_inputs,
                            size_t blocks, const uint32_t key[8],
                            uint64_t counter, bool increment_counter,
                            uint8_t flags, uint8_t flags_start,
                            uint8_t flags_end, uint8_t *out);

extern const blake3_hasher_ops blake3_hasher_op_sse41;

void blake3_hash_many_avx2(const uint8_t *const *inputs, size_t num_inputs,
                           size_t blocks, const uint32_t key[8],
                           uint64_t counter, bool increment_counter,
                           uint8_t flags, uint8_t flags_start,
                           uint8_t flags_end, uint8_t *out);
void blake3_hash_many_avx2_asm(const uint8_t *const *inputs, size_t num_inputs,
                           size_t blocks, const uint32_t key[8],
                           uint64_t counter, bool increment_counter,
                           uint8_t flags, uint8_t flags_start,
                           uint8_t flags_end, uint8_t *out);

extern const blake3_hasher_ops blake3_hasher_op_avx2;

void blake3_compress_in_place_avx512(uint32_t cv[8],
                                     const uint8_t block[BLAKE3_BLOCK_LEN],
                                     uint8_t block_len, uint64_t counter,
                                     uint8_t flags);
void blake3_compress_xof_avx512(const uint32_t cv[8],
                                const uint8_t block[BLAKE3_BLOCK_LEN],
                                uint8_t block_len, uint64_t counter,
                                uint8_t flags, uint8_t out[64]);
void blake3_hash_many_avx512(const uint8_t *const *inputs, size_t num_inputs,
                             size_t blocks, const uint32_t key[8],
                             uint64_t counter, bool increment_counter,
                             uint8_t flags, uint8_t flags_start,
                             uint8_t flags_end, uint8_t *out);
void blake3_compress_in_place_avx512_asm(uint32_t cv[8],
                                     const uint8_t block[BLAKE3_BLOCK_LEN],
                                     uint8_t block_len, uint64_t counter,
                                     uint8_t flags);
void blake3_compress_xof_avx512_asm(const uint32_t cv[8],
                                const uint8_t block[BLAKE3_BLOCK_LEN],
                                uint8_t block_len, uint64_t counter,
                                uint8_t flags, uint8_t out[64]);
void blake3_hash_many_avx512_asm(const uint8_t *const *inputs, size_t num_inputs,
                             size_t blocks, const uint32_t key[8],
                             uint64_t counter, bool increment_counter,
                             uint8_t flags, uint8_t flags_start,
                             uint8_t flags_end, uint8_t *out);

extern const blake3_hasher_ops blake3_hasher_op_avx512;

#endif

#if defined(IS_ARM)
void blake3_hash_many_neon(const uint8_t *const *inputs, size_t num_inputs,
                           size_t blocks, const uint32_t key[8],
                           uint64_t counter, bool increment_counter,
                           uint8_t flags, uint8_t flags_start,
                           uint8_t flags_end, uint8_t *out);

extern const blake3_hasher_ops blake3_hasher_op_neon;
#endif

static uint64_t supported_cpusimd_backend(void)
{
	const blake3_backend *be = &blake3_backends[B3BID_CPUSIMD];
	return be->user ? B3BF_CPUSIMD : 0;
}

static uint64_t detected_cpusimd_backend(void)
{
	const blake3_backend *be = &blake3_backends[B3BID_CPUSIMD];
	return be->user ? B3BF_CPUSIMD : 0;
}

static uint64_t supported_synthetic_backends(void)
{
	uint64_t backends = 0;

	backends |= supported_cpusimd_backend();
	return backends;
}

static uint64_t detected_synthetic_backends(void)
{
	uint64_t backends = 0;

	backends |= detected_cpusimd_backend();
	return backends;
}

static uint64_t supported_gpu_backends(void)
{
	return 0;
}

static uint64_t detected_gpu_backends(void)
{
	return 0;
}

#if defined(IS_X86)

static uint64_t supported_backends_x86(void)
{
	uint64_t backends = 0;

#if !defined(BLAKE3_NO_SSE2)
	backends |= B3BF_SSE2 | B3BF_SSE2_ASM;
#endif
#if !defined(BLAKE3_NO_SSE41)
	backends |= B3BF_SSE41 | B3BF_SSE41_ASM;
#endif
#if !defined(BLAKE3_NO_AVX)
	backends |= B3BF_AVX2 | B3BF_AVX2_ASM;
#endif
#if !defined(BLAKE3_NO_AVX512)
	backends |= B3BF_AVX512 | B3BF_AVX512_ASM;
#endif
	return backends;
}

static inline uint64_t xgetbv(void)
{
#if defined(_MSC_VER)
	return _xgetbv(0);
#else
	uint32_t eax = 0, edx = 0;
	__asm__ __volatile__("xgetbv\n" : "=a"(eax), "=d"(edx) : "c"(0));
	return ((uint64_t)edx << 32) | eax;
#endif
}

static inline void cpuid(uint32_t out[4], uint32_t id)
{
#if defined(_MSC_VER)
	__cpuid((int *)out, id);
#elif defined(__i386__) || defined(_M_IX86)
	__asm__ __volatile__(
			"movl %%ebx, %1\n"
			"cpuid\n"
			"xchgl %1, %%ebx\n"
			: "=a"(out[0]), "=r"(out[1]), "=c"(out[2]), "=d"(out[3])
			: "a"(id));
#else
	__asm__ __volatile__("cpuid\n"
			: "=a"(out[0]), "=b"(out[1]), "=c"(out[2]), "=d"(out[3])
			: "a"(id));
#endif
}

static inline void cpuidex(uint32_t out[4], uint32_t id, uint32_t sid)
{
#if defined(_MSC_VER)
	__cpuidex((int *)out, id, sid);
#elif defined(__i386__) || defined(_M_IX86)
	__asm__ __volatile__(
			"movl %%ebx, %1\n"
			"cpuid\n"
			"xchgl %1, %%ebx\n"
			: "=a"(out[0]), "=r"(out[1]), "=c"(out[2]), "=d"(out[3])
			: "a"(id), "c"(sid));
#else
	__asm__ __volatile__("cpuid\n"
			: "=a"(out[0]), "=b"(out[1]), "=c"(out[2]), "=d"(out[3])
			: "a"(id), "c"(sid));
#endif
}

static uint64_t detected_backends_x86(void)
{
	uint32_t regs[4] = {0};
	uint32_t *eax = &regs[0], *ebx = &regs[1], *ecx = &regs[2], *edx = &regs[3];
	int max_id;
	uint64_t mask = 0;
	uint64_t backends = 0;

	(void)edx;

	cpuid(regs, 0);
	max_id = *eax;
	cpuid(regs, 1);

#if defined(__amd64__) || defined(_M_X64)
	backends |= B3BF_SSE2 | B3BF_SSE2_ASM;
#else
	if (*edx & (1UL << 26))
		backends |= B3BF_SSE2 | B3BF_SSE2_ASM;
#endif
	if (*ecx & (1UL << 19))
		backends |= B3BF_SSE41 | B3BF_SSE41_ASM;

	if (*ecx & (1UL << 27)) { // OSXSAVE
		mask = xgetbv();
		if ((mask & 6) == 6) { // SSE and AVX states
			if (max_id >= 7) {
				cpuidex(regs, 7, 0);
				if (*ebx & (1UL << 5))
					backends |= B3BF_AVX2 | B3BF_AVX2_ASM;
				if ((mask & 224) == 224) { // Opmask, ZMM_Hi256, Hi16_Zmm
					if ((*ebx & ((1UL << 31) | (1UL << 16))) == ((1UL << 31) | (1UL << 16)))
						backends |= B3BF_AVX512 | B3BF_AVX512_ASM;	/* AVX412VL+AVX512F  */
				}
			}
		}
	}

	return backends;
}
#endif

#if defined(IS_ARM)
static uint64_t supported_backends_arm(void)
{
	uint64_t backends = 0;

#if defined(IS_AARCH64)
	backends |= B3BF_NEON;
#endif

	return backends;
}

static uint64_t detected_backends_arm(void)
{
	uint64_t backends = 0;

#if defined(IS_AARCH64)
	backends |= B3BF_NEON;
#endif

	return backends;
}
#endif

uint64_t blake3_get_supported_backends(void)
{
	uint64_t supported_backends;

	supported_backends = B3BF_PORTABLE;

	supported_backends |= supported_gpu_backends();

#if defined(IS_X86)
	supported_backends |= supported_backends_x86();
#endif

#if defined(IS_ARM)
	supported_backends |= supported_backends_arm();
#endif

	supported_backends |= supported_synthetic_backends();

	return supported_backends;
}

uint64_t blake3_get_detected_backends(void)
{
	uint64_t detected_backends;

	detected_backends = B3BF_PORTABLE;

	detected_backends |= detected_gpu_backends();

#if defined(IS_X86)
	detected_backends |= detected_backends_x86();
#endif

#if defined(IS_ARM)
	detected_backends |= detected_backends_arm();
#endif

	detected_backends |= detected_synthetic_backends();

	return detected_backends;
}

uint64_t blake3_get_selectable_backends(void)
{
	return blake3_get_supported_backends() |
               blake3_get_detected_backends();
}

blake3_backend blake3_backends[B3BID_COUNT] = {
	[B3BID_PORTABLE] = {
		.info = {
			.id		= B3BID_PORTABLE,
			.name		= "portable",
			.description	= "portable C implementation",
			.simd_degree	= 1,
			.funcs		= B3FF_HASH_MANY | B3FF_COMPRESS_XOF | B3FF_COMPRESS_IN_PLACE,
		},
		.hasher_ops		= &blake3_hasher_op_portable,
		.hash_many		= blake3_hash_many_portable,
		.compress_xof		= blake3_compress_xof_portable,
		.compress_in_place	= blake3_compress_in_place_portable,
	},
#if defined(IS_X86)

#if defined(TARGET_HAS_SSE2) && TARGET_HAS_SSE2
	[B3BID_SSE2] = {
		.info = {
			.id		= B3BID_SSE2,
			.name		= "sse2",
			.description	= "x86 SSE2 implementation in C",
			.simd_degree	= 4,
			.funcs		= B3FF_HASH_MANY | B3FF_COMPRESS_XOF | B3FF_COMPRESS_IN_PLACE,
		},
		.hasher_ops		= &blake3_hasher_op_sse2,
		.hash_many		= blake3_hash_many_sse2,
		.compress_xof		= blake3_compress_xof_sse2,
		.compress_in_place	= blake3_compress_in_place_sse2,
	},
	[B3BID_SSE2_ASM] = {
		.info = {
			.id		= B3BID_SSE2_ASM,
			.name		= "sse2-asm",
			.description	= "x86 SSE2 implementation in assembly",
			.simd_degree	= 4,
			.funcs		= B3FF_HASH_MANY | B3FF_COMPRESS_XOF | B3FF_COMPRESS_IN_PLACE,
		},
		.hasher_ops		= &blake3_hasher_op_sse2,
		.hash_many		= blake3_hash_many_sse2_asm,
		.compress_xof		= blake3_compress_xof_sse2_asm,
		.compress_in_place	= blake3_compress_in_place_sse2_asm,
	},
#endif

#if defined(TARGET_HAS_SSE41) && TARGET_HAS_SSE41
	[B3BID_SSE41] = {
		.info = {
			.id		= B3BID_SSE41,
			.name		= "sse41",
			.description	= "x86 SSE41 implementation in C",
			.simd_degree	= 4,
			.funcs		= B3FF_HASH_MANY | B3FF_COMPRESS_XOF | B3FF_COMPRESS_IN_PLACE,
		},
		.hasher_ops		= &blake3_hasher_op_sse41,
		.hash_many		= blake3_hash_many_sse41,
		.compress_xof		= blake3_compress_xof_sse41,
		.compress_in_place	= blake3_compress_in_place_sse41,
	},
	[B3BID_SSE41_ASM] = {
		.info = {
			.id		= B3BID_SSE41_ASM,
			.name		= "sse41-asm",
			.description	= "x86 SSE41 implementation in assembly",
			.simd_degree	= 4,
			.funcs		= B3FF_HASH_MANY | B3FF_COMPRESS_XOF | B3FF_COMPRESS_IN_PLACE,
		},
		.hasher_ops		= &blake3_hasher_op_sse41,
		.hash_many		= blake3_hash_many_sse41_asm,
		.compress_xof		= blake3_compress_xof_sse41_asm,
		.compress_in_place	= blake3_compress_in_place_sse41_asm,
	},
#endif

#if defined(TARGET_HAS_AVX2) && TARGET_HAS_AVX2
	[B3BID_AVX2] = {
		.info = {
			.id		= B3BID_AVX2,
			.name		= "avx2",
			.description	= "x86 AVX2 implementation in C",
			.simd_degree	= 8,
			.funcs		= B3FF_HASH_MANY,
		},
		.hasher_ops		= &blake3_hasher_op_avx2,
		.hash_many		= blake3_hash_many_avx2,
	},
	[B3BID_AVX2_ASM] = {
		.info = {
			.id		= B3BID_AVX2_ASM,
			.name		= "avx2-asm",
			.description	= "x86 AVX2 implementation in assembly",
			.simd_degree	= 8,
			.funcs		= B3FF_HASH_MANY,
		},
		.hasher_ops		= &blake3_hasher_op_avx2,
		.hash_many		= blake3_hash_many_avx2_asm,
	},
#endif
#if defined(TARGET_HAS_AVX512) && TARGET_HAS_AVX512
	[B3BID_AVX512] = {
		.info = {
			.id		= B3BID_AVX512,
			.name		= "avx512",
			.description	= "x86 AVX512 VL+F implementation in C",
			.simd_degree	= 16,
			.funcs		= B3FF_HASH_MANY | B3FF_COMPRESS_XOF | B3FF_COMPRESS_IN_PLACE,
		},
		.hasher_ops		= &blake3_hasher_op_avx512,
		.hash_many		= blake3_hash_many_avx512,
		.compress_xof		= blake3_compress_xof_avx512,
		.compress_in_place	= blake3_compress_in_place_avx512,
	},
	[B3BID_AVX512_ASM] = {
		.info = {
			.id		= B3BID_AVX512_ASM,
			.name		= "avx512-asm",
			.description	= "x86 AVX512 VL+F implementation in assembly",
			.simd_degree	= 16,
			.funcs		= B3FF_HASH_MANY | B3FF_COMPRESS_XOF | B3FF_COMPRESS_IN_PLACE,
		},
		.hasher_ops		= &blake3_hasher_op_avx512,
		.hash_many		= blake3_hash_many_avx512,
		.compress_xof		= blake3_compress_xof_avx512_asm,
		.compress_in_place	= blake3_compress_in_place_avx512_asm,
	},
#endif

#endif

#if defined(IS_ARM)

#if defined(TARGET_HAS_NEON) && TARGET_HAS_NEON
	[B3BID_NEON] = {
		.info = {
			.id		= B3BID_NEON,
			.name		= "neon",
			.description	= "arm NEON implementation",
			.simd_degree	= 4,
			.funcs		= B3FF_HASH_MANY | B3FF_COMPRESS_XOF | B3FF_COMPRESS_IN_PLACE,
		},
		.hasher_ops		= &blake3_hasher_op_neon,
		.hash_many		= blake3_hash_many_neon,
		.compress_xof		= blake3_compress_xof_portable,	/* no NEON for this */
		.compress_in_place	= blake3_compress_in_place_portable,
	},
#endif

#endif
};

const blake3_backend *blake3_backend_select_function(uint64_t selectable_backends, blake3_func_id fid)
{
	const blake3_backend *be;
	unsigned int i;

	while (selectable_backends) {
		i = highest_one(selectable_backends);
		selectable_backends &= ~FY_BIT64(i);
		be = blake3_backends + i;
		if (be->info.funcs & FY_BIT64(fid))
			return be;
	}
	/* should never happen because portable is always set */
	assert(false);
	return NULL;
}

const blake3_backend *blake3_get_backend_by_id(blake3_backend_id id)
{
	const blake3_backend *be;

	if ((unsigned int)id >= B3BID_COUNT)
		return NULL;

	be = &blake3_backends[id];

	/* non-enabled backend? */
	if (be->info.id != id || !be->info.name)
		return NULL;

	return be;
}

const blake3_backend *blake3_get_backend_by_name(const char *name)
{
	const blake3_backend *be;
	unsigned int i;

	if (!name)
		return NULL;

	for (i = 0, be = blake3_backends; i < B3BID_COUNT; i++, be++) {
		if (be->info.name && !strcmp(be->info.name, name))
			return be;
	}
	return NULL;
}

const blake3_backend_info *blake3_get_backend_info(blake3_backend_id id)
{
	const blake3_backend_info *bei;

	if ((unsigned int)id >= B3BID_COUNT)
		return NULL;

	bei = &blake3_backends[id].info;

	/* not supported */
	if (bei->id == B3BID_INVALID || !bei->name)
		return NULL;

	return bei;
}
