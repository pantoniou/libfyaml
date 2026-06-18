/*
 * fy-allocator-durable.h - durable, fixed-base, multi-chunk arena allocator
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_ALLOCATOR_DURABLE_H
#define FY_ALLOCATOR_DURABLE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>

#include "fy-atomics.h"
#include "fy-allocator.h"

struct fy_dedup_tag;	/* in-arena dedup index anchor (fy-allocator-dedup.h) */

/* on-disk format */
#define FY_DURABLE_VERSION	2u
#define FY_DURABLE_ENDIAN	0x12345678u

/* persistent boot->flags bits (on-disk arena policy) */
#define FY_DURABLE_BOOT_SPARSE	((uint64_t)1 << 0)	/* chunks are ftruncate'd, not fallocate'd */

/* per-chunk integrity magics */
#define FY_DURABLE_BOOT_MAGIC	0x594644555241424fULL	/* "OBARUDFY" little-endian-ish */
#define FY_DURABLE_CHUNK_MAGIC	0x4b4e554843594446ULL	/* "FYDCHUNK" */

/* chunk-size defaults */
#define FY_DURABLE_DEFAULT_CHUNK_SIZE		(256ULL << 20)	/* 256 MiB */
#define FY_DURABLE_DEFAULT_INDEX_CHUNK_SIZE	(64ULL << 20)	/* 64 MiB */

/* chunk header flags */
#define FYDCF_FULL	FY_BIT(0)

/* tags the durable allocator hands out. */
#define FY_DURABLE_CONTENT_TAG	FY_ALLOC_TAG_DEFAULT
#define FY_DURABLE_INDEX_TAG	1

/* per-chunk header at the start of every chunk's mapped region */
struct fy_durable_chunk_hdr {
	uint64_t magic;				/* FY_DURABLE_CHUNK_MAGIC */
	uint64_t generation;			/* this chunk's id N (immutable) */
	uint64_t usable;			/* bump limit: bytes from this header to chunk end */
	FY_ATOMIC(uint64_t) flags;		/* FYDCF_* */
	FY_ATOMIC(size_t) next;			/* bump offset, measured from this header */
	FY_ATOMIC(struct fy_durable_chunk_hdr *) next_chunk;	/* chunk-list link (fixed addr) */
};

/* first free byte offset within a chunk, measured from its header */
#define FY_DURABLE_DATA0	FY_ALIGN(16, sizeof(struct fy_durable_chunk_hdr))

/* bootstrap header at offset 0 of chunk 0. */
struct fy_durable_boot {
	uint8_t  magic[8];			/* FY_DURABLE_BOOT_MAGIC bytes */
	uint32_t version;
	uint32_t endian;
	uint64_t region_base;
	uint64_t region_size;
	uint64_t chunk_size;
	uint64_t boot_size;			/* sizeof(struct fy_durable_boot), validated */
	uint64_t flags;				/* FY_DURABLE_BOOT_* on-disk policy (immutable) */
	FY_ATOMIC(uint64_t) generation;		/* next generation id source (chunk 0 is id 0) */
	FY_ATOMIC(struct fy_durable_chunk_hdr *) head;	/* chunk-list head */
	FY_ATOMIC(uint64_t) refs_head;		/* branch-refs head word (S1.10); 0 = none. Opaque to the allocator. */
	FY_ATOMIC(struct fy_dedup_tag *) dedup_root;	/* shared in-arena dedup index; 0 if none */
	/* optional separate dedup-index region */
	uint64_t index_region_base;		/* 0 = combined layout (index shares content) */
	uint64_t index_region_size;
	uint64_t index_chunk_size;
	FY_ATOMIC(uint64_t) index_generation;	/* generation id source for the index series */
	FY_ATOMIC(struct fy_durable_chunk_hdr *) index_head;	/* index chunk-list head */
};

/* chunk 0 layout: [boot][pad to 16][chunk_hdr][pad to 16][data...] */
#define FY_DURABLE_CHUNK0_HDR_OFF \
	FY_ALIGN(16, sizeof(struct fy_durable_boot))

_Static_assert(_Alignof(struct fy_durable_boot) >= 8, "boot header must be 8-byte aligned");
_Static_assert(offsetof(struct fy_durable_boot, generation) % 8 == 0, "generation must be 8-byte aligned");
_Static_assert(offsetof(struct fy_durable_boot, head) % 8 == 0, "head must be 8-byte aligned");
_Static_assert(offsetof(struct fy_durable_boot, refs_head) % 8 == 0, "refs_head must be 8-byte aligned");
_Static_assert(offsetof(struct fy_durable_boot, dedup_root) % 8 == 0, "dedup_root must be 8-byte aligned");
_Static_assert(offsetof(struct fy_durable_boot, index_generation) % 8 == 0, "index_generation must be 8-byte aligned");
_Static_assert(offsetof(struct fy_durable_boot, index_head) % 8 == 0, "index_head must be 8-byte aligned");
_Static_assert(offsetof(struct fy_durable_chunk_hdr, flags) % 8 == 0, "flags must be 8-byte aligned");
_Static_assert(offsetof(struct fy_durable_chunk_hdr, next) % 8 == 0, "next must be 8-byte aligned");
_Static_assert(offsetof(struct fy_durable_chunk_hdr, next_chunk) % 8 == 0, "next_chunk must be 8-byte aligned");

/* one contiguous fixed-base region backed by a numbered file series (<prefix>-N.bin). */
struct fy_durable_region {
	const char *prefix;			/* file series prefix: "arena" or "index" */
	uint64_t region_base;
	uint64_t region_size;
	uint64_t chunk_size;			/* power of two */
	unsigned int chunk_shift;		/* log2(chunk_size) */
	uint64_t max_chunks;			/* region_size / chunk_size */
	size_t max_alloc;			/* largest size a fresh chunk can satisfy */
	size_t chunk0_reserve;			/* bytes reserved before chunk-0's hdr (boot); 0 if none */
	void *region;				/* PROT_NONE reservation base (== region_base), NULL if not active */
	FY_ATOMIC(struct fy_durable_chunk_hdr *) cur_head;	/* process-local head hint */
	/* per-process local map of which chunks are mapped here */
	FY_ATOMIC(struct fy_durable_chunk_hdr *) *chunks;
	/* shared roots in the boot header for this region */
	FY_ATOMIC(struct fy_durable_chunk_hdr *) *boot_head;
	FY_ATOMIC(uint64_t) *boot_generation;
};

struct fy_durable_allocator {
	struct fy_allocator a;
	struct fy_durable_allocator_cfg cfg;	/* copy; cfg.dir not owned (see dir) */
	char *dir;				/* owned copy of the directory path */
	int dirfd;				/* O_DIRECTORY fd for *at() ops */
	size_t pagesz;
	bool read_only;
	bool separate_index;			/* dedup index lives in its own file series */
	struct fy_durable_boot *boot;		/* chunk 0 mapping (== content.region_base) */
	struct fy_durable_region content;	/* the content arena (always active) */
	struct fy_durable_region index;		/* the dedup index arena (separate_index only) */
};

extern const struct fy_allocator_ops fy_durable_allocator_ops;

#endif
