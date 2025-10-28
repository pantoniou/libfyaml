/*
 * fy-blob.h - binary blob handling support
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_BLOB_H
#define FY_BLOB_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdint.h>
#ifdef HAVE_BYTESWAP_H
#include <byteswap.h>
#else
#ifdef HAVE___BUILTIN_BSWAP16
#define bswap_16(value) __builtin_bswap16(value)
#else
#define bswap_16(value) ((((value)&0xff) << 8) | ((value) >> 8))
#endif

#ifdef HAVE___BUILTIN_BSWAP32
#define bswap_32(value) __builtin_bswap32(value)
#else
#define bswap_32(value)                                                                            \
  (((uint32_t)bswap_16((uint16_t)((value)&0xffff)) << 16) |                                        \
   (uint32_t)bswap_16((uint16_t)((value) >> 16)))
#endif

#ifdef HAVE___BUILTIN_BSWAP64
#define bswap_64(value) __builtin_bswap64(value)
#else
#define bswap_64(value)                                                                            \
  (((uint64_t)bswap_32((uint32_t)((value)&0xffffffff)) << 32) |                                    \
   (uint64_t)bswap_32((uint32_t)((value) >> 32)))
#endif
#endif

#include <stdbool.h>
#include <time.h>

#include "fy-utils.h"
#include "fy-endian.h"

/* special unaligned types for pointer accesses */

#if defined(__GNUC__) && __GNUC__ >= 4
typedef union { uint64_t v; } __attribute__((__packed__)) br_64;
typedef union { uint32_t v; } __attribute__((__packed__)) br_32;
typedef union { uint16_t v; } __attribute__((__packed__)) br_16;
typedef union { uint8_t v; } br_8;
#elif defined(_MSC_VER)
#pragma pack(push, 1)
typedef union { uint64_t v; } br_64;
typedef union { uint32_t v; } br_32;
typedef union { uint16_t v; } br_16;
typedef union { uint8_t v; } br_8;
#pragma pack(pop)
#else
#error Unsupported compiler
#endif

enum blob_id_size {
	BID_U8,
	BID_U16,
	BID_U32,
	BID_U64,
};

static inline enum blob_id_size
blob_count_to_id_size(uintmax_t count)
{
	if (count <= (1LLU << 8))
		return BID_U8;
	if (count <= (1LLU << 16))
		return BID_U16;
	if (count <= (1LLU << 32))
		return BID_U32;
	return BID_U64;
}

static inline unsigned int
blob_id_size_to_byte_size(enum blob_id_size size)
{
	return 1U << size;
}

static inline unsigned int
blob_id_size_to_bit_size(enum blob_id_size size)
{
	return blob_id_size_to_byte_size(size) * 8;
}

enum blob_endian_type {
	BET_NATIVE_ENDIAN,
	BET_LITTLE_ENDIAN,
	BET_BIG_ENDIAN
};

struct blob_region {
	union {
		uint8_t *wstart;
		const uint8_t *rstart;
	};
	size_t size;
	enum blob_endian_type endian;
	bool bswap;
	size_t curr;
};

#define BR_DEFINE_WX(_pfx, _bits, _bswap, _write) \
size_t _pfx ## _bits (struct blob_region *br, uint ## _bits ## _t v) \
{ \
	size_t pos; \
\
	if (_write) { \
		if (_bswap) \
			v = bswap_ ## _bits (v); \
		((br_ ## _bits *)(br->wstart + br->curr))->v = v; \
	} \
	pos = br->curr; \
	br->curr += sizeof(v); \
	return pos; \
} \
struct __useless_struct_to_allow_semicolon

#define BR_DEFINE_RX(_pfx, _bits, _bswap) \
uint ## _bits ## _t _pfx ## _bits (struct blob_region *br) \
{ \
	uint ## _bits ## _t v; \
\
	v = ((const br_ ## _bits *)(br->rstart + br->curr))->v; \
	if (_bswap) \
		v = bswap_ ## _bits (v); \
	br->curr += sizeof(v); \
	return v; \
} \
struct __useless_struct_to_allow_semicolon

static inline void br_setup_common(struct blob_region *br, size_t size, enum blob_endian_type endian)
{
	memset(br, 0, sizeof(*br));
	br->size = size;
	br->endian = endian;

	switch (br->endian) {
	case BET_NATIVE_ENDIAN:
		/* native endian never needs swap */
		br->bswap = false;
		break;

	case BET_LITTLE_ENDIAN:
#if __BYTE_ORDER == __LITTLE_ENDIAN
		br->bswap = false;
#else
		br->bswap = true;
#endif
		break;

	case BET_BIG_ENDIAN:
#if __BYTE_ORDER == __LITTLE_ENDIAN
		br->bswap = true;
#else
		br->bswap = false;
#endif
		break;
	}

	br->curr = 0;
}

static inline void br_wsetup(struct blob_region *br, void *data, size_t size, enum blob_endian_type endian)
{
	/* write with null data means probe */
	br_setup_common(br, size, endian);
	br->wstart = data;
}

static inline void br_rsetup(struct blob_region *br, const void *data, size_t size, enum blob_endian_type endian)
{
	/* read makes no sense without data */
	br_setup_common(br, size, endian);
	br->rstart = data;
}

static inline void br_reset(struct blob_region *br)
{
	br->curr = 0;
}

static inline size_t br_curr(struct blob_region *br)
{
	return br->curr;
}

static inline size_t br_write(struct blob_region *br, const void *data, size_t size)
{
	size_t pos;

	if (br->wstart)
		memcpy(br->wstart + br->curr, data, size);
	pos = br->curr;
	br->curr += size;
	return pos;
}

static inline size_t br_w0(struct blob_region *br, size_t size)
{
	size_t pos;

	if (br->wstart)
		memset(br->wstart + br->curr, 0, size);
	pos = br->curr;
	br->curr += size;
	return pos;
}

static inline size_t br_wskip(struct blob_region *br, size_t size)
{
	return br_w0(br, size);
}

static inline size_t br_wskip_to(struct blob_region *br, size_t offset)
{
	return br_w0(br, offset - br->curr);
}

static inline BR_DEFINE_WX(br_w,  8, br->bswap, br->wstart != NULL);
static inline BR_DEFINE_WX(br_w, 16, br->bswap, br->wstart != NULL);
static inline BR_DEFINE_WX(br_w, 32, br->bswap, br->wstart != NULL);
static inline BR_DEFINE_WX(br_w, 64, br->bswap, br->wstart != NULL);

typedef size_t (*br_wid_func)(struct blob_region *br, int id);

static inline size_t br_wid8(struct blob_region *br, int id)
{
	return br_w8(br, (uint8_t)id);
}

static inline size_t br_wid16(struct blob_region *br, int id)
{
	return br_w16(br, (uint16_t)id);
}

static inline size_t br_wid32(struct blob_region *br, int id)
{
	return br_w32(br, (uint32_t)id);
}

static inline size_t br_wid64(struct blob_region *br, int id)
{
	return br_w64(br, (uint64_t)id);
}

static inline br_wid_func br_wid_get_func(enum blob_id_size id_size)
{
	switch (id_size) {
	case BID_U8:
		return br_wid8;

	case BID_U16:
		return br_wid16;

	case BID_U32:
		return br_wid32;

	case BID_U64:
		return br_wid64;
	}
	return NULL;
}

/* write a string with optional dedup */
size_t br_wstr(struct blob_region *br, bool dedup, const char *str);

static inline size_t br_wX(struct blob_region *br, enum blob_id_size x_size, uint64_t x)
{
	switch (x_size) {
	case BID_U8:
		return br_w8(br, (uint8_t)x);

	case BID_U16:
		return br_w16(br, (uint16_t)x);

	case BID_U32:
		return br_w32(br, (uint32_t)x);

	case BID_U64:
		return br_w64(br, x);
	}
	return (size_t)-1;
}

static inline size_t br_wid(struct blob_region *br, enum blob_id_size id_size, int id)
{
	switch (id_size) {
	case BID_U8:
		return br_wid8(br, id);

	case BID_U16:
		return br_wid16(br, id);

	case BID_U32:
		return br_wid32(br, id);

	case BID_U64:
		return br_wid64(br, id);
	}
	return br->curr;
}

static inline size_t br_read(struct blob_region *br, void *data, size_t size)
{
	size_t pos;

	memcpy(data, br->rstart + br->curr, size);
	pos = br->curr;
	br->curr += size;
	return pos;
}

static inline void br_rskip(struct blob_region *br, size_t size)
{
	br->curr += size;
}

static inline void br_rskip_to(struct blob_region *br, size_t offset)
{
	br->curr = offset;
}

static inline BR_DEFINE_RX(br_r,  8, br->bswap);
static inline BR_DEFINE_RX(br_r, 16, br->bswap);
static inline BR_DEFINE_RX(br_r, 32, br->bswap);
static inline BR_DEFINE_RX(br_r, 64, br->bswap);

typedef int (*br_rid_func)(struct blob_region *br);

static inline int br_rid8(struct blob_region *br)
{
	return (int)br_r8(br);
}

static inline int br_rid16(struct blob_region *br)
{
	return (int)br_r16(br);
}

static inline int br_rid32(struct blob_region *br)
{
	return (int)br_r32(br);
}

static inline int br_rid64(struct blob_region *br)
{
	return (int)br_r64(br);
}

static inline br_rid_func br_rid_get_func(enum blob_id_size id_size)
{
	switch (id_size) {
	case BID_U8:
		return br_rid8;

	case BID_U16:
		return br_rid16;

	case BID_U32:
		return br_rid32;

	case BID_U64:
		return br_rid64;
	}
	return NULL;
}

static inline uint64_t br_rX(struct blob_region *br, enum blob_id_size x_size)
{
	switch (x_size) {
	case BID_U8:
		return br_r8(br);

	case BID_U16:
		return br_r16(br);

	case BID_U32:
		return br_r32(br);

	case BID_U64:
		return br_r64(br);
	}
	FY_IMPOSSIBLE_ABORT();
}

static inline int br_rid(struct blob_region *br, enum blob_id_size id_size)
{
	int id;

	switch (id_size) {
	case BID_U8:
		id = br_rid8(br);
		break;

	case BID_U16:
		id = br_rid16(br);
		break;

	case BID_U32:
		id = br_rid32(br);
		break;

	case BID_U64:
		id = br_rid64(br);
		break;

	default:
		id = -1;
		FY_IMPOSSIBLE_ABORT();
	}
	return id;
}

void *fy_blob_read(const char *file, size_t *sizep);
int fy_blob_write(const char *file, const void *blob, size_t size);

#endif
