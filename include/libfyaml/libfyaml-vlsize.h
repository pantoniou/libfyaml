/*
 * libfyaml-vlsize.h - variable length size encoding
 *
 * Copyright (c) 2023-2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * DOC: Variable-length size encoding
 *
 * Encodes unsigned integer sizes into a compact, self-delimiting byte stream.
 * The encoding is modelled after the variable-length quantity (VLQ / LEB128
 * big-endian variant) used in MIDI and other binary formats:
 *
 * - Each byte carries 7 bits of payload in bits 6..0.
 * - Bit 7 (MSB) is a *continuation flag*: 1 means more bytes follow, 0 means
 *   this is the last byte.
 * - Exception: the final (maximum-length) byte is always 8 bits of payload
 *   with no continuation bit, allowing the full 64-bit / 32-bit range.
 *
 * **64-bit encoding** (up to 9 bytes)::
 *
 *   bytes   bits   value range
 *   1       7      0 .. 127
 *   2       14     128 .. 16383
 *   3       21     16384 .. 2097151
 *   4       28     2097152 .. 268435455
 *   5       35     268435456 .. 34359738367
 *   6       42     ..
 *   7       49     ..
 *   8       56     ..
 *   9       64     full uint64_t range
 *
 * **32-bit encoding** (up to 5 bytes)::
 *
 *   bytes   bits   value range
 *   1       7      0 .. 127
 *   2       14     128 .. 16383
 *   3       21     16384 .. 2097151
 *   4       28     2097152 .. 268435455
 *   5       32     full uint32_t range (top 4 bits of byte 0 ignored)
 *
 * The native-width ``fy_encode_size()`` / ``fy_decode_size()`` family selects
 * the 64-bit or 32-bit variant based on ``SIZE_MAX``.
 *
 * Each family provides four operations:
 *
 * - ``_bytes()``     — compute the encoded length without writing anything
 * - ``encode()``     — write the encoding into a bounded buffer
 * - ``decode()``     — read and validate from a bounded buffer
 * - ``decode_nocheck()`` — read without bounds checking (caller guarantees room)
 * - ``skip()``       — advance past an encoded value in a bounded buffer
 * - ``skip_nocheck()``   — advance without bounds checking
 */
#ifndef LIBFYAML_VLSIZE_H
#define LIBFYAML_VLSIZE_H

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

// size encoding for 64 bit
//
// high bit is set, more follow with 0 ending the run
// The final 9th byte always terminates the run
//
//  0  1  2  3  4  5  6  7  8
// -- -- -- -- -- -- -- -- --
// 0k	                       7 bit length k
// 1k 0l                      14 bit length kl
// 1k 1l 0m                   21 bit length klm
// 1k 1l 1m 0n                28 bit length klmn
// 1k 1l 1m 1n 0o             35 bit length klmno
// 1k 1l 1m 1n 1o 0p          42 bit length klmnop
// 1k 1l 1m 1n 1o 1p 0q       49 bit length klmnopq
// 1k 1l 1m 1n 1o 1p 1q 0r    56 bit length klmnopqr
// 1k 1l 1m 1n 1o 1p 1q 1r  t 64 bit length klmnopqrt
//
// size encoding for 32 bit
//
// high bit is set, more follow with 0 ending the run
// The final 9th byte always terminates the run
//
//  0   1  2  3  4  5  6  7  8
// --  -- -- -- -- -- -- -- --
// 0k 	                       7 bit length k
// 1k  0l                      14 bit length kl
// 1k  1l 0m                   21 bit length klm
// 1k  1l 1m 0n                28 bit length klmn
// 1xk 1l 1m 1n o              32 bit length klmno 4 high bits ignored (at byte 0)
//

/**
 * FYVL_SIZE_ENCODING_MAX_64 - Maximum encoded length of a 64-bit size value.
 *
 * A 64-bit value requires at most 9 bytes: 8 x 7-bit groups plus one
 * final unconstrained byte = 7 x 8 + 8 = 64 bits.
 */
#define FYVL_SIZE_ENCODING_MAX_64 9 	// 7 * 8 + 8 = 64 bits

/**
 * FYVL_SIZE_ENCODING_MAX_32 - Maximum encoded length of a 32-bit size value.
 *
 * A 32-bit value requires at most 5 bytes: 4 x 7-bit groups plus one
 * final unconstrained byte = 7 x 4 + 4 = 32 bits.
 */
#define FYVL_SIZE_ENCODING_MAX_32 5	// 7 * 4 + 4 = 32 bits

/* 32 bit specific */

/**
 * fy_encode_size32_bytes() - Compute the encoded byte count for a 32-bit size.
 *
 * Returns the number of bytes that fy_encode_size32() would write for @size,
 * without actually writing anything. Useful for pre-allocating buffers.
 *
 * @size: The value whose encoded length is queried.
 *
 * Returns: Number of bytes required (1–5).
 */
static inline unsigned int
fy_encode_size32_bytes(uint32_t size)
{
	if (size < ((uint32_t)1 << 7))
		return 1;
	if (size < ((uint32_t)1 << 14))
		return 2;
	if (size < ((uint32_t)1 << 21))
		return 3;
	if (size < ((uint32_t)1 << 28))
		return 4;
	return 5;
}

/**
 * fy_encode_size32() - Encode a 32-bit size into a buffer.
 *
 * Writes the variable-length encoding of @size into the buffer [@p, @p+@bufsz).
 *
 * @p:     Start of the output buffer.
 * @bufsz: Available space in bytes.
 * @size:  Value to encode.
 *
 * Returns: Pointer to one past the last written byte, or NULL if @bufsz was
 *          too small.
 */
static inline uint8_t *
fy_encode_size32(uint8_t *p, uint32_t bufsz, uint32_t size)
{
	uint8_t *end = p + bufsz;

	if (size < ((uint32_t)1 << 7)) {
		if (p + 1 > end)
			return NULL;
		p[0] = (uint8_t)size;
		return p + 1;
	}
	if (size < ((uint32_t)1 << 14)) {
		if (p + 2 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 7) | 0x80;
		p[1] = (uint8_t)size & 0x7f;
		return p + 2;
	}
	if (size < ((uint32_t)1 << 21)) {
		if (p + 3 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 14) | 0x80;
		p[1] = (uint8_t)(size >>  7) | 0x80;
		p[2] = (uint8_t)size & 0x7f;
		return p + 3;
	}
	if (size < ((uint32_t)1 << 28)) {
		if (p + 4 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 21) | 0x80;
		p[1] = (uint8_t)(size >> 14) | 0x80;
		p[2] = (uint8_t)(size >>  7) | 0x80;
		p[3] = (uint8_t)size & 0x7f;
		return p + 4;
	}
	if (p + 5 > end)
		return NULL;
	p[0] = (uint8_t)(size >> 29) | 0x80;
	p[1] = (uint8_t)(size >> 22) | 0x80;
	p[2] = (uint8_t)(size >> 15) | 0x80;
	p[3] = (uint8_t)(size >>  8) | 0x80;
	p[4] = (uint8_t)size;
	return p + 5;
}

/**
 * fy_decode_size32() - Decode a variable-length 32-bit size from a buffer.
 *
 * Reads bytes from [@start, @start+@bufsz) and reconstructs the encoded
 * 32-bit value. Stops after %FYVL_SIZE_ENCODING_MAX_32 bytes at most.
 *
 * @start:  Start of the encoded data.
 * @bufsz:  Available bytes to read.
 * @sizep:  Output: the decoded value. Set to ``(uint32_t)-1`` on error.
 *
 * Returns: Pointer to one past the last consumed byte, or NULL if the buffer
 *          was exhausted before a complete value was found.
 */
static inline const uint8_t *
fy_decode_size32(const uint8_t *start, size_t bufsz, uint32_t *sizep)
{
	const uint8_t *p, *end, *end_scan, *end_max_scan;
	uint32_t size;

	end = start + bufsz;

	end_max_scan = start + FYVL_SIZE_ENCODING_MAX_32;

	if (end_max_scan < end)
		end_scan = end_max_scan;
	else
		end_scan = end;

	end_max_scan--;

	p = start;
	size = 0;
	while (p < end_scan) {
		if (p < end_max_scan) {
			size <<= 7;
			size |= (*p & 0x7f);
			if (!(*p & 0x80))
				goto done;
		} else {
			/* last one is always the full 8 bit */
			size <<= 8;
			size |= *p;
			goto done;
		}
		p++;
	}

	*sizep = (uint32_t)-1;
	return NULL;
done:
	if (++p >= end)
		p = end;
	*sizep = size;
	return p;
}

/**
 * fy_decode_size32_nocheck() - Decode a 32-bit size without bounds checking.
 *
 * Like fy_decode_size32() but assumes the buffer is large enough to hold a
 * complete encoding. The caller must guarantee at least
 * %FYVL_SIZE_ENCODING_MAX_32 bytes are available at @start.
 *
 * @start: Start of the encoded data.
 * @sizep: Output: the decoded value as a uint64_t for uniform handling.
 *
 * Returns: Pointer to one past the last consumed byte.
 */
static inline const uint8_t *
fy_decode_size32_nocheck(const uint8_t *start, uint64_t *sizep)
{
	const uint8_t *p;
	size_t size;
	unsigned int i;

	p = start;
	size = 0;
	for (i = 0; i < FYVL_SIZE_ENCODING_MAX_32-1; i++) {
		size <<= 7;
		size |= (p[i] & 0x7f);
		if ((int8_t)p[i] >= 0)
			goto out;
	}
	size <<= 8;
	size |= p[i];
out:
	*sizep = size;
	return p + i + 1;
}

/**
 * fy_skip_size32() - Skip past a variable-length 32-bit size in a buffer.
 *
 * Advances past the encoded value without decoding it. Useful when the
 * value itself is not needed.
 *
 * @start:  Start of the encoded data.
 * @bufsz:  Available bytes.
 *
 * Returns: Pointer to one past the last consumed byte, or NULL if the buffer
 *          was exhausted before a complete encoding was found.
 */
static inline const uint8_t *
fy_skip_size32(const uint8_t *start, size_t bufsz)
{
	const uint8_t *p, *end, *end_scan, *end_max_scan;

	end = start + bufsz;

	end_max_scan = start + FYVL_SIZE_ENCODING_MAX_32;

	if (end_max_scan < end)
		end_scan = end_max_scan;
	else
		end_scan = end;

	end_max_scan--;

	p = start;
	while (p < end_scan) {
		if (p < end_max_scan) {
			if (!(*p & 0x80))
				goto done;
		} else
			goto done;
		p++;
	}

	return NULL;
done:
	if (++p >= end)
		p = end;
	return p;
}

/**
 * fy_skip_size32_nocheck() - Skip a 32-bit encoded size without bounds checking.
 *
 * Like fy_skip_size32() but assumes the buffer is large enough. The caller
 * must guarantee at least %FYVL_SIZE_ENCODING_MAX_32 bytes are readable.
 *
 * @p: Start of the encoded data.
 *
 * Returns: Pointer to one past the last consumed byte.
 */
static inline const uint8_t *
fy_skip_size32_nocheck(const uint8_t *p)
{
	unsigned int i;

	for (i = 0; i < FYVL_SIZE_ENCODING_MAX_32; ) {
		if ((int8_t)p[i++] >= 0)
			break;
	}
	return p + i;
}

/**
 * fy_encode_size64_bytes() - Compute the encoded byte count for a 64-bit size.
 *
 * Returns the number of bytes that fy_encode_size64() would write for @size,
 * without writing anything.
 *
 * @size: The value whose encoded length is queried.
 *
 * Returns: Number of bytes required (1–9).
 */
static inline unsigned int
fy_encode_size64_bytes(uint64_t size)
{
	if (size < ((uint64_t)1 << 7))
		return 1;
	if (size < ((uint64_t)1 << 14))
		return 2;
	if (size < ((uint64_t)1 << 21))
		return 3;
	if (size < ((uint64_t)1 << 28))
		return 4;
	if (size < ((uint64_t)1 << 35))
		return 5;
	if (size < ((uint64_t)1 << 42))
		return 6;
	if (size < ((uint64_t)1 << 49))
		return 7;
	if (size < ((uint64_t)1 << 56))
		return 8;
	return 9;
}

/**
 * fy_encode_size64() - Encode a 64-bit size into a buffer.
 *
 * Writes the variable-length encoding of @size into the buffer [@p, @p+@bufsz).
 *
 * @p:     Start of the output buffer.
 * @bufsz: Available space in bytes.
 * @size:  Value to encode.
 *
 * Returns: Pointer to one past the last written byte, or NULL if @bufsz was
 *          too small.
 */
static inline uint8_t *
fy_encode_size64(uint8_t *p, size_t bufsz, uint64_t size)
{
	uint8_t *end = p + bufsz;

	if (size < ((uint64_t)1 << 7)) {
		if (p + 1 > end)
			return NULL;
		p[0] = (uint8_t)size;
		return p + 1;
	}
	if (size < ((uint64_t)1 << 14)) {
		if (p + 2 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 7) | 0x80;
		p[1] = (uint8_t)size & 0x7f;
		return p + 2;
	}
	if (size < ((uint64_t)1 << 21)) {
		if (p + 3 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 14) | 0x80;
		p[1] = (uint8_t)(size >>  7) | 0x80;
		p[2] = (uint8_t)size & 0x7f;
		return p + 3;
	}
	if (size < ((uint64_t)1 << 28)) {
		if (p + 4 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 21) | 0x80;
		p[1] = (uint8_t)(size >> 14) | 0x80;
		p[2] = (uint8_t)(size >>  7) | 0x80;
		p[3] = (uint8_t)size & 0x7f;
		return p + 4;
	}
	if (size < ((uint64_t)1 << 35)) {
		if (p + 5 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 28) | 0x80;
		p[1] = (uint8_t)(size >> 21) | 0x80;
		p[2] = (uint8_t)(size >> 14) | 0x80;
		p[3] = (uint8_t)(size >>  7) | 0x80;
		p[4] = (uint8_t)size & 0x7f;
		return p + 5;
	}
	if (size < ((uint64_t)1 << 42)) {
		if (p + 6 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 35) | 0x80;
		p[1] = (uint8_t)(size >> 28) | 0x80;
		p[2] = (uint8_t)(size >> 21) | 0x80;
		p[3] = (uint8_t)(size >> 14) | 0x80;
		p[4] = (uint8_t)(size >>  7) | 0x80;
		p[5] = (uint8_t)size & 0x7f;
		return p + 6;
	}
	if (size < ((uint64_t)1 << 49)) {
		if (p + 7 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 42) | 0x80;
		p[1] = (uint8_t)(size >> 35) | 0x80;
		p[2] = (uint8_t)(size >> 28) | 0x80;
		p[3] = (uint8_t)(size >> 21) | 0x80;
		p[4] = (uint8_t)(size >> 14) | 0x80;
		p[5] = (uint8_t)(size >>  7) | 0x80;
		p[6] = (uint8_t)size & 0x7f;
		return p + 7;
	}
	if (size < ((uint64_t)1 << 56)) {
		if (p + 8 > end)
			return NULL;
		p[0] = (uint8_t)(size >> 49) | 0x80;
		p[1] = (uint8_t)(size >> 42) | 0x80;
		p[2] = (uint8_t)(size >> 35) | 0x80;
		p[3] = (uint8_t)(size >> 28) | 0x80;
		p[4] = (uint8_t)(size >> 21) | 0x80;
		p[5] = (uint8_t)(size >> 14) | 0x80;
		p[6] = (uint8_t)(size >>  7) | 0x80;
		p[7] = (uint8_t)size & 0x7f;
		return p + 8;
	}
	if (p + 9 > end)
		return NULL;
	p[0] = (uint8_t)(size >> 57) | 0x80;
	p[1] = (uint8_t)(size >> 50) | 0x80;
	p[2] = (uint8_t)(size >> 43) | 0x80;
	p[3] = (uint8_t)(size >> 36) | 0x80;
	p[4] = (uint8_t)(size >> 29) | 0x80;
	p[5] = (uint8_t)(size >> 22) | 0x80;
	p[6] = (uint8_t)(size >> 15) | 0x80;
	p[7] = (uint8_t)(size >>  8) | 0x80;
	p[8] = (uint8_t)size;
	return p + 9;
}

/**
 * fy_decode_size64() - Decode a variable-length 64-bit size from a buffer.
 *
 * Reads bytes from [@start, @start+@bufsz) and reconstructs the encoded
 * 64-bit value. Stops after %FYVL_SIZE_ENCODING_MAX_64 bytes at most.
 *
 * @start:  Start of the encoded data.
 * @bufsz:  Available bytes to read.
 * @sizep:  Output: the decoded value. Set to ``(size_t)-1`` on error.
 *
 * Returns: Pointer to one past the last consumed byte, or NULL if the buffer
 *          was exhausted before a complete value was found.
 */
static inline const uint8_t *
fy_decode_size64(const uint8_t *start, size_t bufsz, uint64_t *sizep)
{
	const uint8_t *p, *end, *end_scan, *end_max_scan;
	size_t size;

	end = start + bufsz;

	end_max_scan = start + FYVL_SIZE_ENCODING_MAX_64;

	if (end_max_scan < end)
		end_scan = end_max_scan;
	else
		end_scan = end;

	end_max_scan--;

	p = start;
	size = 0;
	while (p < end_scan) {
		if (p < end_max_scan) {
			size <<= 7;
			size |= (*p & 0x7f);
			if (!(*p & 0x80))
				goto done;
		} else {
			/* last one is always 8 bit */
			size <<= 8;
			size |= *p;
			goto done;
		}
		p++;
	}

	*sizep = (size_t)-1;
	return NULL;
done:
	if (++p >= end)
		p = end;
	*sizep = size;
	return p;
}

/**
 * fy_decode_size64_nocheck() - Decode a 64-bit size without bounds checking.
 *
 * Like fy_decode_size64() but assumes the buffer is large enough. The caller
 * must guarantee at least %FYVL_SIZE_ENCODING_MAX_64 bytes are readable.
 *
 * @start: Start of the encoded data.
 * @sizep: Output: the decoded value.
 *
 * Returns: Pointer to one past the last consumed byte.
 */
static inline const uint8_t *
fy_decode_size64_nocheck(const uint8_t *start, uint64_t *sizep)
{
	const uint8_t *p;
	size_t size;
	unsigned int i;

	p = start;
	size = 0;
	for (i = 0; i < FYVL_SIZE_ENCODING_MAX_64-1; i++) {
		size <<= 7;
		size |= (p[i] & 0x7f);
		if ((int8_t)p[i] >= 0)
			goto out;
	}
	size <<= 8;
	size |= p[i];
out:
	*sizep = size;
	return p + i + 1;
}

/**
 * fy_skip_size64() - Skip past a variable-length 64-bit size in a buffer.
 *
 * Advances past the encoded value without decoding it.
 *
 * @start:  Start of the encoded data.
 * @bufsz:  Available bytes.
 *
 * Returns: Pointer to one past the last consumed byte, or NULL if the buffer
 *          was exhausted before a complete encoding was found.
 */
static inline const uint8_t *
fy_skip_size64(const uint8_t *start, size_t bufsz)
{
	const uint8_t *p, *end, *end_scan, *end_max_scan;

	end = start + bufsz;

	end_max_scan = start + FYVL_SIZE_ENCODING_MAX_64;

	if (end_max_scan < end)
		end_scan = end_max_scan;
	else
		end_scan = end;

	end_max_scan--;

	p = start;
	while (p < end_scan) {
		if (p < end_max_scan) {
			if (!(*p & 0x80))
				goto done;
		} else
			goto done;
		p++;
	}

	return NULL;
done:
	if (++p >= end)
		p = end;
	return p;
}

/**
 * fy_skip_size64_nocheck() - Skip a 64-bit encoded size without bounds checking.
 *
 * Like fy_skip_size64() but assumes the buffer is large enough. The caller
 * must guarantee at least %FYVL_SIZE_ENCODING_MAX_64 bytes are readable.
 *
 * @p: Start of the encoded data.
 *
 * Returns: Pointer to one past the last consumed byte.
 */
static inline const uint8_t *
fy_skip_size64_nocheck(const uint8_t *p)
{
	unsigned int i;

	for (i = 0; i < FYVL_SIZE_ENCODING_MAX_64; ) {
		if ((int8_t)p[i++] >= 0)
			break;
	}
	return p + i;
}

/* is pointless to pretend size_t is anything other than 64 or 32 bits */
#if SIZE_MAX == UINT64_MAX

/**
 * fy_encode_size_bytes() - Compute encoded byte count for a native size_t.
 *
 * Selects fy_encode_size64_bytes() or fy_encode_size32_bytes() based on
 * ``SIZE_MAX``.
 *
 * @size: The value whose encoded length is queried.
 *
 * Returns: Number of bytes required.
 */
static inline unsigned int
fy_encode_size_bytes(size_t size)
{
	return fy_encode_size64_bytes(size);
}

/**
 * fy_encode_size() - Encode a native size_t into a buffer.
 *
 * Selects fy_encode_size64() or fy_encode_size32() based on ``SIZE_MAX``.
 *
 * @p:     Start of the output buffer.
 * @bufsz: Available space in bytes.
 * @size:  Value to encode.
 *
 * Returns: Pointer to one past the last written byte, or NULL on overflow.
 */
static inline uint8_t *
fy_encode_size(uint8_t *p, size_t bufsz, size_t size)
{
	return fy_encode_size64(p, bufsz, size);
}

/**
 * fy_decode_size() - Decode a native size_t from a buffer.
 *
 * Selects fy_decode_size64() or fy_decode_size32() based on ``SIZE_MAX``.
 *
 * @start:  Start of the encoded data.
 * @bufsz:  Available bytes.
 * @sizep:  Output: the decoded value.
 *
 * Returns: Pointer to one past the last consumed byte, or NULL on error.
 */
static inline const uint8_t *
fy_decode_size(const uint8_t *start, size_t bufsz, size_t *sizep)
{
	uint64_t sz;
	const uint8_t *ret;

	ret = fy_decode_size64(start, bufsz, &sz);
	if (!ret) {
		*sizep = 0;
		return NULL;
	}
	*sizep = sz;
	return ret;
}

/**
 * fy_decode_size_nocheck() - Decode a native size_t without bounds checking.
 *
 * Selects the 64-bit or 32-bit nocheck variant based on ``sizeof(size_t)``.
 * The caller must guarantee enough bytes are available at @start.
 *
 * @start:  Start of the encoded data.
 * @sizep:  Output: the decoded value.
 *
 * Returns: Pointer to one past the last consumed byte.
 */
static inline const uint8_t *
fy_decode_size_nocheck(const uint8_t *start, size_t *sizep)
{
	uint64_t size64;
	const uint8_t *s;

	switch (sizeof(size_t)) {
	case sizeof(uint64_t):
		s = fy_decode_size64_nocheck(start, &size64);
		break;
	case sizeof(uint32_t):
		s = fy_decode_size32_nocheck(start, &size64);
		break;
	default:
		size64 = 0;
		s = NULL;
		break;
	}
	*sizep = (size_t)size64;
	return s;
}

/**
 * fy_skip_size() - Skip a native size_t encoding in a buffer.
 *
 * Selects fy_skip_size64() or fy_skip_size32() based on ``SIZE_MAX``.
 *
 * @start:  Start of the encoded data.
 * @bufsz:  Available bytes.
 *
 * Returns: Pointer to one past the last consumed byte, or NULL on error.
 */
static inline const uint8_t *
fy_skip_size(const uint8_t *start, size_t bufsz)
{
	return fy_skip_size64(start, bufsz);
}

/**
 * fy_skip_size_nocheck() - Skip a native size_t encoding without bounds checking.
 *
 * Selects the 64-bit or 32-bit nocheck variant based on ``SIZE_MAX``.
 * The caller must guarantee enough bytes are available.
 *
 * @start: Start of the encoded data.
 *
 * Returns: Pointer to one past the last consumed byte.
 */
static inline const uint8_t *
fy_skip_size_nocheck(const uint8_t *start)
{
	return fy_skip_size64_nocheck(start);
}

/**
 * FYVL_SIZE_ENCODING_MAX - Maximum encoded length for a native size_t.
 *
 * Equals %FYVL_SIZE_ENCODING_MAX_64 on 64-bit platforms,
 * %FYVL_SIZE_ENCODING_MAX_32 on 32-bit platforms.
 */
#define FYVL_SIZE_ENCODING_MAX FYVL_SIZE_ENCODING_MAX_64

#else

static inline unsigned int
fy_encode_size_bytes(size_t size)
{
	return fy_encode_size32_bytes(size);
}

static inline uint8_t *
fy_encode_size(uint8_t *p, size_t bufsz, size_t size)
{
	return fy_encode_size32(p, bufsz, size);
}

static inline const uint8_t *
fy_decode_size(const uint8_t *start, size_t bufsz, size_t *sizep)
{
	uint32_t sz;
	const uint8_t *ret;

	ret = fy_decode_size32(start, bufsz, &sz);
	if (!ret)
		return NULL;
	*sizep = sz;
	return ret;
}

static inline const uint8_t *
fy_decode_size_nocheck(const uint8_t *start, size_t *sizep)
{
	return fy_decode_size32_nocheck(start, sizep);
}

static inline const uint8_t *
fy_skip_size(const uint8_t *start, size_t bufsz)
{
	return fy_skip_size32(start, bufsz, &sz);
}

static inline const uint8_t *
fy_skip_size_nocheck(const uint8_t *start)
{
	return fy_skip_size32_nocheck(start);
}

#define FYVL_SIZE_ENCODING_MAX FYVL_SIZE_ENCODING_MAX_32

#endif

#ifdef __cplusplus
}
#endif

#endif	// LIBFYAML_VLSIZE_H
