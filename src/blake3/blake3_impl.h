#ifndef BLAKE3_IMPL_H
#define BLAKE3_IMPL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
#include <stdalign.h>
#include <stdlib.h>

#if defined(__linux__)
#include <malloc.h>
#include <sys/sysmacros.h>
#endif

#include "blake3.h"

#include "fy-endian.h"
#include "fy-blob.h"
#include "fy-bit64.h"
#include "fy-align.h"

#if defined(HAVE_STRING_OP_OVERREAD) && HAVE_STRING_OP_OVERREAD && defined(__GNUC__) && !defined(__clang__)
#define GCC_DISABLE_WSTRING_OP_OVERREAD
#endif

/* this is the best alignment for blake3 */
#if defined(__x86_64__) || defined(_M_X64)
#define BLAKE3_ALIGNMENT	64
#else
#define BLAKE3_ALIGNMENT	32
#endif

#define BLAKE3_ALIGN	FY_ALIGNED_TO(BLAKE3_ALIGNMENT)

// This C implementation tries to support recent versions of GCC, Clang, and
// MSVC.
#if defined(_MSC_VER)
#define INLINE static __forceinline
#else
#define INLINE static inline __attribute__((always_inline))
#endif

#if defined(__x86_64__) || defined(_M_X64)
#define IS_X86
#define IS_X86_64
#endif

#if defined(__i386__) || defined(_M_IX86)
#define IS_X86
#define IS_X86_32
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#define IS_AARCH64
#define IS_ARM
#endif

#if defined(IS_X86)
#if defined(_MSC_VER)
#if defined(__clang__)
/* clang-cl: intrin.h pulls in mmintrin.h which has vector type bugs
 * when cross-compiling. Declare only the intrinsics we actually need. */
unsigned long long _xgetbv(unsigned int);
void __cpuid(int[4], int);
#else
#include <intrin.h>
#endif
#endif
#endif

/* Find index of the highest set bit */
/* x is assumed to be nonzero.       */
static inline unsigned int highest_one(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
	return 63 ^ (unsigned int)__builtin_clzll(x);
#elif defined(_MSC_VER) && defined(IS_X86_64)
	unsigned long index;
	_BitScanReverse64(&index, x);
	return index;
#elif defined(_MSC_VER) && defined(IS_X86_32)
	unsigned long index;

	if(x >> 32) {
		_BitScanReverse(&index, (unsigned long)(x >> 32));
		index += 32;
	} else
		_BitScanReverse(&index, (unsigned long)x);
	return index;
#else
	unsigned int c = 0;

	if (x & 0xffffffff00000000ULL) { x >>= 32; c += 32; }
	if (x & 0x00000000ffff0000ULL) { x >>= 16; c += 16; }
	if (x & 0x000000000000ff00ULL) { x >>=  8; c +=  8; }
	if (x & 0x00000000000000f0ULL) { x >>=  4; c +=  4; }
	if (x & 0x000000000000000cULL) { x >>=  2; c +=  2; }
	if (x & 0x0000000000000002ULL) {           c +=  1; }
	return c;
#endif
}

static inline unsigned int lowest_one(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
	return (unsigned int)__builtin_ctzll(x);
#else
	unsigned int c = 0;

	if (!(x & 0x00000000ffffffffULL)) { x >>= 32; c += 32; }
	if (!(x & 0x000000000000ffffULL)) { x >>= 16; c += 16; }
	if (!(x & 0x00000000000000ffULL)) { x >>=  8; c +=  8; }
	if (!(x & 0x000000000000000fULL)) { x >>=  4; c +=  4; }
	if (!(x & 0x0000000000000003ULL)) { x >>=  2; c +=  2; }
	if (!(x & 0x0000000000000001ULL)) {           c +=  1; }
	return c;
#endif
}

// Count the number of 1 bits.
static inline unsigned int popcnt(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
	return (unsigned int)__builtin_popcountll(x);
#else
	unsigned int count = 0;
	while (x != 0) {
		count++;
		x &= x - 1;
	}
	return count;
#endif
}

// Largest power of two less than or equal to x. As a special case, returns 1
// when x is 0.
static inline uint64_t round_down_to_power_of_2(uint64_t x)
{
	return 1ULL << highest_one(x | 1);
}

static inline uint32_t counter_low(uint64_t counter)
{
	return (uint32_t)counter;
}

static inline uint32_t counter_high(uint64_t counter)
{
	return (uint32_t)(counter >> 32);
}

static inline uint32_t load32(const void *src)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
	return *(const uint32_t *)src;
#else
	const uint8_t *p = (const uint8_t *)src;
	return ((uint32_t)(p[0]) <<  0) | ((uint32_t)(p[1]) <<  8) |
	       ((uint32_t)(p[2]) << 16) | ((uint32_t)(p[3]) << 24);
#endif
}

static inline void load_key_words(const uint8_t key[BLAKE3_KEY_LEN], uint32_t key_words[8])
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
	memcpy(key_words, key, BLAKE3_KEY_LEN);
#else
	key_words[0] = load32(&key[0 * 4]);
	key_words[1] = load32(&key[1 * 4]);
	key_words[2] = load32(&key[2 * 4]);
	key_words[3] = load32(&key[3 * 4]);
	key_words[4] = load32(&key[4 * 4]);
	key_words[5] = load32(&key[5 * 4]);
	key_words[6] = load32(&key[6 * 4]);
	key_words[7] = load32(&key[7 * 4]);
#endif
}

static inline void store32(void *dst, uint32_t w)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
	*(uint32_t *)dst = w;
#else
	uint8_t *p = (uint8_t *)dst;

	p[0] = (uint8_t)(w >> 0);
	p[1] = (uint8_t)(w >> 8);
	p[2] = (uint8_t)(w >> 16);
	p[3] = (uint8_t)(w >> 24);
#endif
}

static inline void store_cv_words(uint8_t bytes_out[BLAKE3_OUT_LEN], uint32_t cv_words[BLAKE3_OUT_WORDS])
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
	memcpy(bytes_out, cv_words, BLAKE3_KEY_LEN);
#else
	store32(&bytes_out[0 * 4], cv_words[0]);
	store32(&bytes_out[1 * 4], cv_words[1]);
	store32(&bytes_out[2 * 4], cv_words[2]);
	store32(&bytes_out[3 * 4], cv_words[3]);
	store32(&bytes_out[4 * 4], cv_words[4]);
	store32(&bytes_out[5 * 4], cv_words[5]);
	store32(&bytes_out[6 * 4], cv_words[6]);
	store32(&bytes_out[7 * 4], cv_words[7]);
#endif
}

/* the IV */
#define B3_IV_0	((uint32_t)0x6A09E667UL)
#define B3_IV_1	((uint32_t)0xBB67AE85UL)
#define B3_IV_2	((uint32_t)0x3C6EF372UL)
#define B3_IV_3	((uint32_t)0xA54FF53AUL)
#define B3_IV_4	((uint32_t)0x510E527FUL)
#define B3_IV_5	((uint32_t)0x9B05688CUL)
#define B3_IV_6	((uint32_t)0x1F83D9ABUL)
#define B3_IV_7	((uint32_t)0x5BE0CD19UL)

/* the message schedule definition */
#define B3_MSG_SCHEDULE_DEF \
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, \
    {2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8}, \
    {3, 4, 10, 12, 13, 2, 7, 14, 6, 5, 9, 0, 11, 15, 8, 1}, \
    {10, 7, 12, 9, 14, 3, 13, 15, 4, 0, 11, 2, 5, 8, 1, 6}, \
    {12, 13, 9, 11, 15, 10, 14, 8, 7, 2, 5, 3, 0, 1, 6, 4}, \
    {9, 14, 11, 5, 8, 12, 15, 1, 13, 3, 0, 10, 2, 6, 4, 7}, \
    {11, 15, 5, 0, 1, 9, 8, 6, 14, 10, 2, 12, 3, 4, 7, 13}

#endif /* BLAKE3_IMPL_H */
