/*
 * fy-allocator-durable.c - durable, fixed-base, multi-chunk arena allocator
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fy-allocator-durable.h"
#include "fy-allocator-dedup.h"

#ifndef _WIN32

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fy-align.h"
#include "fy-bit64.h"
#include "fy-list.h"
#include "fy-utils.h"

static const uint8_t fy_durable_boot_magic[8] = {
	'O', 'B', 'A', 'R', 'U', 'D', 'F', 'Y'
};

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0
#endif

static inline void *fy_durable_chunk_base(struct fy_durable_region *rg, uint64_t gen)
{
	return (char *)rg->region + (gen << rg->chunk_shift);
}

/* byte offset of a chunk's allocation header within the chunk */
static inline size_t fy_durable_hdr_off(struct fy_durable_region *rg, uint64_t gen)
{
	return gen == 0 ? rg->chunk0_reserve : 0;
}

static inline struct fy_durable_chunk_hdr *
fy_durable_hdr_for_gen(struct fy_durable_region *rg, uint64_t gen)
{
	return (struct fy_durable_chunk_hdr *)
		((char *)fy_durable_chunk_base(rg, gen) + fy_durable_hdr_off(rg, gen));
}

/* bytes from a chunk's header to the end of the chunk (its bump limit) */
static inline uint64_t fy_durable_usable(struct fy_durable_region *rg, uint64_t gen)
{
	return rg->chunk_size - fy_durable_hdr_off(rg, gen);
}

static inline uint64_t fy_durable_gen_of_ptr(struct fy_durable_region *rg, const void *p)
{
	return (uint64_t)(((const char *)p - (const char *)rg->region) >> rg->chunk_shift);
}

/* the region a pointer falls into, or NULL if it is in neither */
static inline struct fy_durable_region *
fy_durable_region_of_ptr(struct fy_durable_allocator *da, const void *p)
{
	struct fy_durable_region *rg = &da->content;

	if (p >= rg->region && p < (void *)((char *)rg->region + rg->region_size))
		return rg;
	rg = &da->index;
	if (rg->region && p >= rg->region &&
	    p < (void *)((char *)rg->region + rg->region_size))
		return rg;
	return NULL;
}

static void fy_durable_unmap_to_reserved(struct fy_durable_region *rg, uint64_t gen);

static int fy_durable_map_chunk_file(struct fy_durable_allocator *da,
				     struct fy_durable_region *rg, uint64_t gen)
{
	char name[64];
	void *base, *mem;
	int fd;

	base = fy_durable_chunk_base(rg, gen);

	snprintf(name, sizeof(name), "%s-%llu.bin", rg->prefix, (unsigned long long)gen);
	fd = openat(da->dirfd, name, da->read_only ? O_RDONLY : O_RDWR);
	if (fd < 0)
		return -1;

	mem = mmap(base, rg->chunk_size,
		   da->read_only ? PROT_READ : PROT_READ | PROT_WRITE,
		   MAP_SHARED | MAP_FIXED, fd, 0);
	close(fd);
	/* not only it must not fail, it must use the provided address */
	if (mem == MAP_FAILED)
		return -1;

	if (mem != base) {
		/* unmap this */
		munmap(mem, rg->chunk_size);
		return -1;
	}

	return 0;
}

/* ensure the chunk for @gen is mapped in this process; returns its header */
static struct fy_durable_chunk_hdr *
fy_durable_ensure_mapped(struct fy_durable_allocator *da,
			 struct fy_durable_region *rg, uint64_t gen)
{
	struct fy_durable_chunk_hdr *hdr;

	if (gen >= rg->max_chunks)
		return NULL;

	hdr = fy_atomic_load(&rg->chunks[gen]);
	if (hdr)
		return hdr;

	/*
	 * Several threads may race here and each map the chunk:
	 * that is safe and idempotent. Every MAP_FIXED of the same file+offset
	 * (MAP_SHARED) resolves to the same page-cache pages, so re-mapping over
	 * a peer's mapping swaps the VMA but not the physical backing - in-flight
	 * accesses and atomics through the chunk stay coherent. Each racer then
	 * publishes the same @hdr value, so the store is idempotent too.
	 */
	if (fy_durable_map_chunk_file(da, rg, gen) != 0)
		return fy_atomic_load(&rg->chunks[gen]);	/* a peer may have won */

	hdr = fy_durable_hdr_for_gen(rg, gen);
	if (hdr->magic != FY_DURABLE_CHUNK_MAGIC) {
		/* not a valid chunk; drop the mapping back to a reservation */
		fy_durable_unmap_to_reserved(rg, gen);
		return NULL;
	}

	fy_atomic_store(&rg->chunks[gen], hdr);
	return hdr;
}

static struct fy_durable_chunk_hdr *
fy_durable_ensure_mapped_ptr(struct fy_durable_allocator *da,
			     struct fy_durable_region *rg, struct fy_durable_chunk_hdr *p)
{
	if (!p)
		return NULL;
	return fy_durable_ensure_mapped(da, rg, fy_durable_gen_of_ptr(rg, p));
}

/* CAS-bump within a single chunk; returns NULL if it does not fit. */
static inline void *fy_durable_chunk_alloc(struct fy_durable_chunk_hdr *hdr,
					   size_t size, size_t align)
{
	size_t old_next, new_next, data_pos;

	do {
		old_next = fy_atomic_load(&hdr->next);
		data_pos = fy_size_t_align(old_next, align);
		new_next = data_pos + size;
		if (new_next > hdr->usable)
			return NULL;
	} while (!fy_atomic_compare_exchange_strong(&hdr->next, &old_next, new_next));

	/* zero the alignment skip-over */
	if (data_pos > old_next)
		memset((char *)hdr + old_next, 0, data_pos - old_next);

	return (char *)hdr + data_pos;
}

/* declared below */
static struct fy_durable_chunk_hdr *
fy_durable_grow(struct fy_durable_allocator *da, struct fy_durable_region *rg,
		struct fy_durable_chunk_hdr *observed_head);

static void *fy_durable_arena_alloc(struct fy_durable_allocator *da,
				    struct fy_durable_region *rg, size_t size, size_t align)
{
	struct fy_durable_chunk_hdr *head, *pub;
	void *ptr;

	if (da->read_only || !size || !rg->region)
		return NULL;

	/* larger than any fresh chunk can ever satisfy */
	if (size + align > rg->max_alloc)
		return NULL;

	for (;;) {
		/* fast path: allocate from the cached process-local head hint */
		head = fy_atomic_load(&rg->cur_head);
		if (head) {
			ptr = fy_durable_chunk_alloc(head, size, align);
			if (ptr)
				return ptr;
		}

		/* slow path: resync to the published head, growing if it is also full */
		pub = fy_durable_ensure_mapped_ptr(da, rg, fy_atomic_load(rg->boot_head));
		if (!pub)
			return NULL;
		if (pub != head) {
			fy_atomic_store(&rg->cur_head, pub);	/* someone advanced it */
			continue;
		}

		pub = fy_durable_grow(da, rg, pub);
		if (!pub)
			return NULL;
		fy_atomic_store(&rg->cur_head, pub);
		/* retry against the new head */
	}
}

static void fy_durable_init_chunk_hdr(struct fy_durable_region *rg,
				      struct fy_durable_chunk_hdr *hdr, uint64_t gen,
				      struct fy_durable_chunk_hdr *next_chunk)
{
	hdr->magic = FY_DURABLE_CHUNK_MAGIC;
	hdr->generation = gen;
	hdr->usable = fy_durable_usable(rg, gen);
	fy_atomic_store(&hdr->flags, 0);
	fy_atomic_store(&hdr->next, FY_DURABLE_DATA0);
	fy_atomic_store(&hdr->next_chunk, next_chunk);
}

/* re-reserve a slot as PROT_NONE (loser cleanup / bad chunk) */
static void fy_durable_unmap_to_reserved(struct fy_durable_region *rg, uint64_t gen)
{
	void *p = mmap(fy_durable_chunk_base(rg, gen), rg->chunk_size, PROT_NONE,
		       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE, -1, 0);
	(void)p;	/* best-effort slot re-reservation */
}

static int fy_durable_make_chunk(struct fy_durable_allocator *da,
				 struct fy_durable_region *rg, uint64_t gen,
				 char *tmp_out, size_t tmp_sz)
{
	void *base, *mem = NULL;
	int fd = -1, rc;
	bool tmp_created = false;

	snprintf(tmp_out, tmp_sz, "%s-%llu.bin.tmp.%ld",
		 rg->prefix, (unsigned long long)gen, (long)getpid());

	fd = openat(da->dirfd, tmp_out, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		goto err_out;

	tmp_created = true;

	if (da->boot->flags & FY_DURABLE_BOOT_SPARSE)
		rc = ftruncate(fd, (off_t)rg->chunk_size);
	else
		rc = fy_fallocate(fd, 0, (off_t)rg->chunk_size);
	if (rc != 0)
		goto err_out;

	base = fy_durable_chunk_base(rg, gen);
	mem = mmap(base, rg->chunk_size, PROT_READ | PROT_WRITE,
		   MAP_SHARED | MAP_FIXED, fd, 0);
	close(fd);
	fd = -1;
	if (mem == MAP_FAILED)
		goto err_out;

	if (mem != base) {
		munmap(mem, rg->chunk_size);
		goto err_out;
	}

	fy_durable_init_chunk_hdr(rg, fy_durable_hdr_for_gen(rg, gen), gen, NULL);
	rc = msync(base, FY_DURABLE_DATA0, MS_SYNC);
	if (rc)
		goto err_out;
	return 0;

err_out:
	if (fd >= 0)
		close(fd);
	if (tmp_created)
		unlinkat(da->dirfd, tmp_out, 0);
	return -1;
}

static struct fy_durable_chunk_hdr *
fy_durable_grow(struct fy_durable_allocator *da, struct fy_durable_region *rg,
		struct fy_durable_chunk_hdr *observed_head)
{
	struct fy_durable_chunk_hdr *new_hdr, *expected, *hdr = NULL;
	char tmp[80], name[64];
	uint64_t gen;
	bool got_gen = false, got_lock = false, got_tmp = false;
	int rc;

	/* claim a unique generation id */
	gen = fy_atomic_fetch_add(rg->boot_generation, 1);
	if (gen >= rg->max_chunks)
		return NULL;	/* region exhausted */
	got_gen = true;

	do {
		rc = flock(da->dirfd, LOCK_SH);
	} while (rc == -1 && errno == EAGAIN);
	if (rc)
		goto err_out;
	got_lock = true;

	/* make the chunk */
	if (fy_durable_make_chunk(da, rg, gen, tmp, sizeof(tmp)) != 0)
		goto err_out;
	got_tmp = true;

	/* link */
	snprintf(name, sizeof(name), "%s-%llu.bin", rg->prefix, (unsigned long long)gen);
	new_hdr = fy_durable_hdr_for_gen(rg, gen);
	fy_atomic_store(&new_hdr->next_chunk, observed_head);

	/* atomically rename */
	if (renameat(da->dirfd, tmp, da->dirfd, name) != 0)
		goto err_out;

	fy_atomic_store(&rg->chunks[gen], new_hdr);

	/* CAS store at the head */
	expected = observed_head;
	if (fy_atomic_compare_exchange_strong(rg->boot_head, &expected, new_hdr)) {
		do {
			flock(da->dirfd, LOCK_UN);
		} while (rc == -1 && errno == EAGAIN);
		return new_hdr;	/* winner */
	}

	/* loser: retire our file and chunk, fall back to the winner's head. */
	unlinkat(da->dirfd, name, 0);
	fy_atomic_store(&rg->chunks[gen], NULL);
	fy_durable_unmap_to_reserved(rg, gen);

	flock(da->dirfd, LOCK_UN);

	/* expected now holds the winner's head */
	hdr = fy_durable_ensure_mapped_ptr(da, rg, fy_atomic_load(rg->boot_head));
	return hdr;

err_out:
	if (got_tmp)
		unlinkat(da->dirfd, tmp, 0);
	if (got_lock) {
		do {
			rc = flock(da->dirfd, LOCK_UN);
		} while (rc == -1 && errno == EAGAIN);
	}
	if (got_gen)
		fy_durable_unmap_to_reserved(rg, gen);
	return NULL;
}

static bool fy_durable_gen_in_list(struct fy_durable_allocator *da,
				   struct fy_durable_region *rg, uint64_t gen)
{
	struct fy_durable_chunk_hdr *hdr;

	hdr = fy_atomic_load(rg->boot_head);
	while (hdr) {
		if (!fy_durable_ensure_mapped_ptr(da, rg, hdr))
			break;
		if (hdr->generation == gen)
			return true;
		hdr = fy_atomic_load(&hdr->next_chunk);
	}
	return false;
}

static struct fy_durable_region *
fy_durable_match_chunk_file(struct fy_durable_allocator *da, const char *n,
			    uint64_t *gen_out, bool *is_tmp)
{
	struct fy_durable_region *regs[2] = { &da->content, &da->index };
	unsigned int i;
	struct fy_durable_region *rg;
	size_t plen;
	const char *p;
	char *end;
	unsigned long long gen;

	for (i = 0; i < 2; i++) {
		rg = regs[i];

		if (!rg->region || !rg->prefix)
			continue;
		plen = strlen(rg->prefix);
		if (strncmp(n, rg->prefix, plen) || n[plen] != '-')
			continue;

		p = n + plen + 1;
		end = NULL;
		gen = strtoull(p, &end, 10);
		if (!end || end == p)
			continue;
		if (!strcmp(end, ".bin")) {
			*is_tmp = false;
			*gen_out = (uint64_t)gen;
			return rg;
		}
		if (!strncmp(end, ".bin.tmp", 8)) {
			*is_tmp = true;
			*gen_out = (uint64_t)gen;
			return rg;
		}
	}
	return NULL;
}

static void fy_durable_recover(struct fy_durable_allocator *da)
{
	DIR *d = NULL;
	struct dirent *de;
	int fd = -1, rc;
	bool got_lock = false;
	const char *n;
	struct fy_durable_region *rg;
	uint64_t gen;
	bool is_tmp;

	/* recovery only runs when it can take the directory exclusive */
	do {
		rc = flock(da->dirfd, LOCK_EX | LOCK_NB);
	} while (rc == -1 && errno == EINTR);
	if (rc)
		return;
	got_lock = true;

	fd = openat(da->dirfd, ".", O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		goto out;

	d = fdopendir(fd);
	if (!d)
		goto out;

	while ((de = readdir(d)) != NULL) {
		n = de->d_name;

		rg = fy_durable_match_chunk_file(da, n, &gen, &is_tmp);
		if (!rg)
			continue;

		/* orphan in-progress chunk creations */
		if (is_tmp) {
			unlinkat(da->dirfd, n, 0);
			continue;
		}

		if (gen == 0)
			continue;	/* never reap a region's chunk 0 */

		if (!fy_durable_gen_in_list(da, rg, gen))
			unlinkat(da->dirfd, n, 0);
	}
out:
	if (d)
		closedir(d);

	if (got_lock) {
		do {
			rc = flock(da->dirfd, LOCK_UN);
		} while (rc == -1 && errno == EINTR);
	}
}

static int fy_durable_reserve_region(struct fy_durable_region *rg)
{
	void *p;

	p = mmap((void *)(uintptr_t)rg->region_base, rg->region_size, PROT_NONE,
		 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE,
		 -1, 0);
	if (p == MAP_FAILED)
		return -1;
	if (p != (void *)(uintptr_t)rg->region_base) {
		munmap(p, rg->region_size);
		fprintf(stderr,
			"libfyaml durable arena: region base 0x%llx (%s) is unavailable "
			"(got %p); reconfigure the base\n",
			(unsigned long long)rg->region_base, rg->prefix, p);
		return -1;
	}
	rg->region = p;
	return 0;
}

/* validate a region's geometry and fill its derived fields */
static int fy_durable_region_geometry(struct fy_durable_allocator *da,
				      struct fy_durable_region *rg, size_t chunk0_reserve)
{
	size_t min_chunk;

	/* base 0 means the platform has no suitable fixed VA (durable unsupported) */
	if (!rg->region_base || !rg->chunk_size || !rg->region_size)
		return -1;
	/* chunk_size must be a power of two so gen<->address is a shift, not a divide */
	if (rg->chunk_size & (rg->chunk_size - 1))
		return -1;
	rg->chunk_shift = fy_bit64_lowest(rg->chunk_size);
	if (rg->chunk_size < da->pagesz)
		return -1;
	if (rg->region_size & (rg->chunk_size - 1))
		return -1;
	if (rg->region_base & (rg->chunk_size - 1))
		return -1;
	rg->chunk0_reserve = chunk0_reserve;
	/* chunk 0 must hold its reserved prefix + its own header with room to spare */
	min_chunk = chunk0_reserve + FY_DURABLE_DATA0 + 16;
	if (rg->chunk_size < min_chunk)
		return -1;
	rg->max_chunks = rg->region_size / rg->chunk_size;
	/* largest allocation any chunk can satisfy (data starts at DATA0) */
	rg->max_alloc = rg->chunk_size - FY_DURABLE_DATA0;
	return 0;
}

static int fy_durable_validate_boot(struct fy_durable_allocator *da)
{
	struct fy_durable_boot *b = da->boot;

	if (memcmp(b->magic, fy_durable_boot_magic, sizeof(b->magic)) ||
	    b->version != FY_DURABLE_VERSION ||
	    b->endian != FY_DURABLE_ENDIAN ||
	    b->boot_size != sizeof(*b) ||
	    b->region_base != da->content.region_base ||
	    b->region_size != da->content.region_size ||
	    b->chunk_size != da->content.chunk_size)
		return -1;
	return 0;
}

/* point the content region's shared roots at the (now-mapped) boot header */
static inline void fy_durable_bind_content_roots(struct fy_durable_allocator *da)
{
	da->content.boot_head = &da->boot->head;
	da->content.boot_generation = &da->boot->generation;
}

static inline bool fy_durable_ranges_overlap(uint64_t a, uint64_t alen,
					     uint64_t b, uint64_t blen)
{
	return a < b + blen && b < a + alen;
}

/* set up the index region's geometry from it and bind its shared roots. */
static int fy_durable_setup_index_layout(struct fy_durable_allocator *da)
{
	struct fy_durable_region *rg = &da->index;
	struct fy_durable_boot *b = da->boot;

	da->separate_index = b->index_region_base != 0;
	if (!da->separate_index)
		return 0;

	rg->prefix = "index";
	rg->region_base = b->index_region_base;
	rg->region_size = b->index_region_size;
	rg->chunk_size = b->index_chunk_size;
	if (fy_durable_region_geometry(da, rg, 0) != 0)
		return -1;
	/* the index region must not overlap the content region */
	if (fy_durable_ranges_overlap(da->content.region_base, da->content.region_size,
				      rg->region_base, rg->region_size))
		return -1;
	rg->boot_head = &b->index_head;
	rg->boot_generation = &b->index_generation;
	return 0;
}

/* create chunk 0 single-writer; race-safe via link-to-final */
static int fy_durable_create_chunk0(struct fy_durable_allocator *da)
{
	struct fy_durable_region *rg = &da->content;
	struct fy_durable_boot *b;
	struct fy_durable_chunk_hdr *h0;
	char tmp[64];
	void *base;
	void *mem;
	int fd = -1, rc;
	bool got_tmp = false;

	snprintf(tmp, sizeof(tmp), "arena-0.bin.tmp.%ld", (long)getpid());

	fd = openat(da->dirfd, tmp, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	got_tmp = true;

	if (da->cfg.flags & FY_DURABLE_ARENA_SPARSE)
		rc = ftruncate(fd, (off_t)rg->chunk_size);
	else
		rc = fy_fallocate(fd, 0, (off_t)rg->chunk_size);
	if (rc != 0)
		goto err_out;

	base = (void *)(uintptr_t)rg->region_base;
	mem = mmap(base, rg->chunk_size, PROT_READ | PROT_WRITE,
		   MAP_SHARED | MAP_FIXED, fd, 0);
	close(fd);
	fd = -1;
	if (mem == MAP_FAILED)
		goto err_out;

	/* base must match */
	if (mem != base) {
		munmap(mem, rg->chunk_size);
		goto err_out;
	}

	b = base;
	memset(b, 0, sizeof(*b));
	memcpy(b->magic, fy_durable_boot_magic, sizeof(b->magic));
	b->version = FY_DURABLE_VERSION;
	b->endian = FY_DURABLE_ENDIAN;
	b->region_base = rg->region_base;
	b->region_size = rg->region_size;
	b->chunk_size = rg->chunk_size;
	b->boot_size = sizeof(*b);
	b->flags = (da->cfg.flags & FY_DURABLE_ARENA_SPARSE) ? FY_DURABLE_BOOT_SPARSE : 0;
	b->refs_head = 0;

	if (da->cfg.flags & FY_DURABLE_ARENA_SEPARATE_INDEX) {
		b->index_region_base = da->cfg.index_region_base ?
			da->cfg.index_region_base : fy_default_fixed_vm_base(1);
		b->index_region_size = da->cfg.index_region_size ?
			da->cfg.index_region_size : fy_default_fixed_vm_size(1);
		b->index_chunk_size = da->cfg.index_chunk_size ?
			da->cfg.index_chunk_size : FY_DURABLE_DEFAULT_INDEX_CHUNK_SIZE;
	}

	h0 = fy_durable_hdr_for_gen(rg, 0);
	fy_durable_init_chunk_hdr(rg, h0, 0, NULL);

	/* generation id source starts past chunk 0; head is chunk 0 */
	fy_atomic_store(&b->generation, 1);
	fy_atomic_store(&b->head, h0);

	msync(base, FY_DURABLE_CHUNK0_HDR_OFF + FY_DURABLE_DATA0, MS_SYNC);

	/* publish atomically; loses to a concurrent creator */
	if (linkat(da->dirfd, tmp, da->dirfd, "arena-0.bin", 0) != 0) {
		unlinkat(da->dirfd, tmp, 0);
		got_tmp = false;
		if (errno != EEXIST)
			goto err_out;
		/* someone else created it: drop our mapping, fall through to open */
		fy_durable_unmap_to_reserved(rg, 0);
		da->boot = NULL;
		return 1;	/* exists now; caller should open it */
	}

	unlinkat(da->dirfd, tmp, 0);
	da->boot = b;
	fy_durable_bind_content_roots(da);
	fy_atomic_store(&rg->chunks[0], h0);
	return 0;

err_out:
	if (fd >= 0)
		close(fd);
	if (got_tmp)
		unlinkat(da->dirfd, tmp, 0);
	return -1;
}

static int fy_durable_open_chunk0(struct fy_durable_allocator *da)
{
	struct fy_durable_region *rg = &da->content;
	void *base;

	base = (void *)(uintptr_t)rg->region_base;
	if (fy_durable_map_chunk_file(da, rg, 0) != 0)
		return -1;
	da->boot = base;
	if (fy_durable_validate_boot(da) != 0) {
		fy_durable_unmap_to_reserved(rg, 0);
		da->boot = NULL;
		return -1;
	}
	fy_durable_bind_content_roots(da);
	fy_atomic_store(&rg->chunks[0], fy_durable_hdr_for_gen(rg, 0));
	return 0;
}

static void fy_durable_cleanup(struct fy_allocator *a);

static int fy_durable_setup(struct fy_allocator *a, struct fy_allocator *parent,
			    int parent_tag, const void *cfg_data)
{
	const struct fy_durable_allocator_cfg *cfg = cfg_data;
	struct fy_durable_allocator *da;
	struct stat st;
	int rc;

	if (!a || !cfg || !cfg->dir)
		return -1;

	da = container_of(a, struct fy_durable_allocator, a);
	memset(da, 0, sizeof(*da));
	da->a.name = "durable";
	da->a.ops = &fy_durable_allocator_ops;
	da->a.parent = parent;
	da->a.parent_tag = parent_tag;
	da->cfg = *cfg;
	da->dirfd = -1;
	da->read_only = !!(cfg->flags & FY_DURABLE_ARENA_READONLY);

	da->pagesz = sysconf(_SC_PAGESIZE);

	/* content region geometry (defaults applied here) */
	da->content.prefix = "arena";
	da->content.region_base = cfg->region_base ? cfg->region_base : fy_default_fixed_vm_base(0);
	da->content.region_size = cfg->region_size ? cfg->region_size : fy_default_fixed_vm_size(0);
	da->content.chunk_size = cfg->chunk_size ? cfg->chunk_size : FY_DURABLE_DEFAULT_CHUNK_SIZE;

	if (fy_durable_region_geometry(da, &da->content, FY_DURABLE_CHUNK0_HDR_OFF) != 0)
		goto err_out;

	da->dir = strdup(cfg->dir);
	if (!da->dir)
		goto err_out;

	/* create the directory if requested and missing */
	if (stat(da->dir, &st) != 0) {
		if (!(cfg->flags & FY_DURABLE_ARENA_CREATE))
			goto err_out;
		if (mkdir(da->dir, 0755) != 0 && errno != EEXIST)
			goto err_out;
	}

	da->dirfd = open(da->dir, O_RDONLY | O_DIRECTORY);
	if (da->dirfd < 0)
		goto err_out;

	if (!fy_fd_fs_is_local(da->dirfd))
		goto err_out;

	da->content.chunks = calloc(da->content.max_chunks, sizeof(*da->content.chunks));
	if (!da->content.chunks)
		goto err_out;

	if (fy_durable_reserve_region(&da->content) != 0)
		goto err_out;

	/* open chunk 0, creating it if asked */
	rc = fy_durable_open_chunk0(da);
	if (rc != 0) {
		if (!(cfg->flags & FY_DURABLE_ARENA_CREATE))
			goto err_out;
		rc = fy_durable_create_chunk0(da);
		if (rc < 0)
			goto err_out;
		if (rc == 1 && fy_durable_open_chunk0(da) != 0)	/* lost the race */
			goto err_out;
	}

	/* adopt the arena's recorded layout (separate index or combined) */
	if (fy_durable_setup_index_layout(da) != 0)
		goto err_out;

	if (!da->read_only)
		fy_durable_recover(da);

	return 0;

err_out:
	fy_durable_cleanup(a);
	return -1;
}

/* create or attach the in-arena dedup index over the base allocator */
static struct fy_allocator *
fy_durable_dedup_enable(struct fy_durable_allocator *da, struct fy_allocator *base);
static void fy_durable_destroy(struct fy_allocator *a);

static struct fy_allocator *
fy_durable_create(struct fy_allocator *parent, int parent_tag, const void *cfg)
{
	const struct fy_durable_allocator_cfg *dcfg = cfg;
	struct fy_durable_allocator *da;
	struct fy_allocator *top, *dedup;

	da = fy_early_parent_allocator_alloc(parent, parent_tag, sizeof(*da),
					     _Alignof(struct fy_durable_allocator));
	if (!da)
		return NULL;

	if (fy_durable_setup(&da->a, parent, parent_tag, cfg) != 0) {
		fy_early_parent_allocator_free(parent, parent_tag, da);
		return NULL;
	}

	top = &da->a;
	if (dcfg->flags & FY_DURABLE_ARENA_DEDUP) {
		dedup = fy_durable_dedup_enable(da, &da->a);
		if (!dedup) {
			fy_durable_destroy(&da->a);
			return NULL;
		}
		dedup->flags |= FYAF_OWNS_PARENT;
		top = dedup;
	}

	return top;
}

/* unmap every chunk this process mapped for a region, then drop the reservation */
static void fy_durable_region_cleanup(struct fy_durable_region *rg)
{
	uint64_t i;

	if (rg->chunks) {
		for (i = 0; i < rg->max_chunks; i++) {
			if (fy_atomic_load(&rg->chunks[i]))
				munmap(fy_durable_chunk_base(rg, i), rg->chunk_size);
		}
		free(rg->chunks);
		rg->chunks = NULL;
	}
	if (rg->region) {
		munmap(rg->region, rg->region_size);
		rg->region = NULL;
	}
}

static void fy_durable_cleanup(struct fy_allocator *a)
{
	struct fy_durable_allocator *da;

	if (!a)
		return;

	da = container_of(a, struct fy_durable_allocator, a);

	fy_durable_region_cleanup(&da->index);
	fy_durable_region_cleanup(&da->content);

	if (da->dirfd >= 0) {
		close(da->dirfd);
		da->dirfd = -1;
	}
	if (da->dir) {
		free(da->dir);
		da->dir = NULL;
	}
}

static void fy_durable_destroy(struct fy_allocator *a)
{
	struct fy_durable_allocator *da;

	if (!a)
		return;
	da = container_of(a, struct fy_durable_allocator, a);
	fy_durable_cleanup(a);
	fy_early_parent_allocator_free(da->a.parent, da->a.parent_tag, da);
}

static void fy_durable_dump(struct fy_allocator *a)
{
	struct fy_durable_allocator *da;
	struct fy_durable_chunk_hdr *hdr;

	da = container_of(a, struct fy_durable_allocator, a);
	fprintf(stderr, "durable: base=0x%llx region=%lluMiB chunk=%lluMiB gen=%llu\n",
		(unsigned long long)da->content.region_base,
		(unsigned long long)(da->content.region_size >> 20),
		(unsigned long long)(da->content.chunk_size >> 20),
		(unsigned long long)fy_atomic_load(&da->boot->generation));
	for (hdr = fy_atomic_load(&da->boot->head); hdr;
	     hdr = fy_atomic_load(&hdr->next_chunk)) {
		if (!fy_durable_ensure_mapped_ptr(da, &da->content, hdr))
			break;
		fprintf(stderr, "  chunk %llu: used %llu / %llu\n",
			(unsigned long long)hdr->generation,
			(unsigned long long)fy_atomic_load(&hdr->next),
			(unsigned long long)da->content.chunk_size);
	}
}

/* select the region a tag routes to: index tag -> index region, else content */
static inline struct fy_durable_region *
fy_durable_region_for_tag(struct fy_durable_allocator *da, int tag)
{
	if (da->separate_index && tag == FY_DURABLE_INDEX_TAG)
		return &da->index;
	return &da->content;
}

static void *fy_durable_alloc(struct fy_allocator *a, int tag, size_t size, size_t align)
{
	struct fy_durable_allocator *da = container_of(a, struct fy_durable_allocator, a);

	return fy_durable_arena_alloc(da, fy_durable_region_for_tag(da, tag), size, align);
}

static void fy_durable_free(struct fy_allocator *a FY_UNUSED, int tag FY_UNUSED, void *data FY_UNUSED)
{
	/* no individual frees */
}

static int fy_durable_update_stats(struct fy_allocator *a FY_UNUSED, int tag FY_UNUSED,
				   struct fy_allocator_stats *stats FY_UNUSED)
{
	return 0;
}

static const void *fy_durable_storev(struct fy_allocator *a, int tag,
				     const struct iovec *iov, int iovcnt,
				     size_t align, uint64_t hash FY_UNUSED)
{
	struct fy_durable_allocator *da = container_of(a, struct fy_durable_allocator, a);
	size_t total_size;
	void *start;

	total_size = fy_iovec_size(iov, iovcnt);
	if (total_size == SIZE_MAX)
		return NULL;

	start = fy_durable_arena_alloc(da, fy_durable_region_for_tag(da, tag), total_size, align);
	if (!start)
		return NULL;

	fy_iovec_copy_from(iov, iovcnt, start);
	return start;
}

static const void *fy_durable_lookupv(struct fy_allocator *a FY_UNUSED, int tag FY_UNUSED,
				      const struct iovec *iov FY_UNUSED, int iovcnt FY_UNUSED,
				      size_t align FY_UNUSED, uint64_t hash FY_UNUSED)
{
	return NULL;
}

static void fy_durable_release(struct fy_allocator *a FY_UNUSED, int tag FY_UNUSED,
			       const void *data FY_UNUSED, size_t size FY_UNUSED)
{
	/* no releases */
}

static int fy_durable_get_tag(struct fy_allocator *a FY_UNUSED)
{
	/* fixed, persistent tags; not freely allocatable - see get_tag_count */
	return FY_DURABLE_CONTENT_TAG;
}

static void fy_durable_release_tag(struct fy_allocator *a FY_UNUSED, int tag FY_UNUSED)
{
}

static int fy_durable_get_tag_count(struct fy_allocator *a)
{
	struct fy_durable_allocator *da = container_of(a, struct fy_durable_allocator, a);

	/* content only (combined) or content + index (separate-index layout) */
	return da->separate_index ? 2 : 1;
}

static int fy_durable_set_tag_count(struct fy_allocator *a, unsigned int count)
{
	struct fy_durable_allocator *da = container_of(a, struct fy_durable_allocator, a);

	return count == (da->separate_index ? 2u : 1u) ? 0 : -1;
}

static void fy_durable_trim_tag(struct fy_allocator *a FY_UNUSED, int tag FY_UNUSED)
{
}

static void fy_durable_reset_tag(struct fy_allocator *a FY_UNUSED, int tag FY_UNUSED)
{
}

static struct fy_allocator_info *
fy_durable_get_info(struct fy_allocator *a, int tag)
{
	struct fy_durable_allocator *da = container_of(a, struct fy_durable_allocator, a);
	struct fy_durable_region *rg = fy_durable_region_for_tag(da, tag);
	struct fy_durable_chunk_hdr *hdr;
	struct fy_allocator_info *info;
	struct fy_allocator_tag_info *tag_info;
	struct fy_allocator_arena_info *arena_info;
	size_t free, used, total;
	unsigned int num_arenas, i;

	/* count chunks */
	num_arenas = 0;
	for (hdr = fy_atomic_load(rg->boot_head); hdr;
	     hdr = fy_atomic_load(&hdr->next_chunk)) {
		if (!fy_durable_ensure_mapped_ptr(da, rg, hdr))
			break;
		num_arenas++;
	}

	info = malloc(sizeof(*info) + sizeof(*tag_info) + sizeof(*arena_info) * num_arenas);
	if (!info)
		return NULL;
	memset(info, 0, sizeof(*info));
	tag_info = (void *)(info + 1);
	arena_info = (void *)(tag_info + 1);

	free = used = total = 0;
	tag_info->tag = tag;
	tag_info->num_arena_infos = 0;
	tag_info->arena_infos = arena_info;

	i = 0;
	for (hdr = fy_atomic_load(rg->boot_head); hdr && i < num_arenas;
	     hdr = fy_atomic_load(&hdr->next_chunk), i++) {
		size_t next = fy_atomic_load(&hdr->next);
		size_t a_used = next - FY_DURABLE_DATA0;	/* offsets are from the header */
		size_t a_free = hdr->usable - next;

		arena_info[i].free = a_free;
		arena_info[i].used = a_used;
		arena_info[i].total = rg->chunk_size;
		arena_info[i].data = (char *)hdr + FY_DURABLE_DATA0;
		arena_info[i].size = a_used;

		free += a_free;
		used += a_used;
		total += rg->chunk_size;
		tag_info->num_arena_infos++;
	}

	tag_info->free = free;
	tag_info->used = used;
	tag_info->total = total;

	info->free = free;
	info->used = used;
	info->total = total;
	info->num_tag_infos = 1;
	info->tag_infos = tag_info;

	return info;
}

static enum fy_allocator_cap_flags fy_durable_get_caps(struct fy_allocator *a FY_UNUSED)
{
	return FYACF_HAS_CONTAINS | FYACF_HAS_EFFICIENT_CONTAINS | FYACF_DURABLE;
}

static bool fy_durable_contains(struct fy_allocator *a, int tag FY_UNUSED, const void *ptr)
{
	struct fy_durable_allocator *da = container_of(a, struct fy_durable_allocator, a);
	struct fy_durable_region *rg;
	uint64_t gen;
	size_t off;

	/* a pointer belongs to whichever region's reservation it falls in */
	rg = fy_durable_region_of_ptr(da, ptr);
	if (!rg)
		return false;

	gen = fy_durable_gen_of_ptr(rg, ptr);
	if (gen >= rg->max_chunks || gen >= fy_atomic_load(rg->boot_generation))
		return false;

	/* offsets are measured from the chunk header */
	off = (size_t)((const char *)ptr - (const char *)fy_durable_hdr_for_gen(rg, gen));
	return off >= FY_DURABLE_DATA0 && off < fy_durable_usable(rg, gen);
}

/* durable-specific generic ops (defined below, in the public-API section) */
static int fy_durable_op_sync(struct fy_allocator *a);
static uint64_t fy_durable_op_refs_get(struct fy_allocator *a);
static int fy_durable_op_refs_publish(struct fy_allocator *a, uint64_t expected,
				      uint64_t desired, unsigned int flags);
static uint64_t fy_durable_op_generation(struct fy_allocator *a);
static unsigned int fy_durable_op_chunk_count(struct fy_allocator *a);
static uint64_t fy_durable_op_region_base(struct fy_allocator *a);
static uint64_t fy_durable_op_region_size(struct fy_allocator *a);
static uint64_t fy_durable_op_index_region_base(struct fy_allocator *a);
static uint64_t fy_durable_op_index_region_size(struct fy_allocator *a);

const struct fy_allocator_ops fy_durable_allocator_ops = {
	.setup = fy_durable_setup,
	.cleanup = fy_durable_cleanup,
	.create = fy_durable_create,
	.destroy = fy_durable_destroy,
	.dump = fy_durable_dump,
	.alloc = fy_durable_alloc,
	.free = fy_durable_free,
	.update_stats = fy_durable_update_stats,
	.storev = fy_durable_storev,
	.lookupv = fy_durable_lookupv,
	.release = fy_durable_release,
	.get_tag = fy_durable_get_tag,
	.release_tag = fy_durable_release_tag,
	.get_tag_count = fy_durable_get_tag_count,
	.set_tag_count = fy_durable_set_tag_count,
	.trim_tag = fy_durable_trim_tag,
	.reset_tag = fy_durable_reset_tag,
	.get_info = fy_durable_get_info,
	.get_caps = fy_durable_get_caps,
	.contains = fy_durable_contains,
	.sync = fy_durable_op_sync,
	.refs_get = fy_durable_op_refs_get,
	.refs_publish = fy_durable_op_refs_publish,
	.generation = fy_durable_op_generation,
	.chunk_count = fy_durable_op_chunk_count,
	.region_base = fy_durable_op_region_base,
	.region_size = fy_durable_op_region_size,
	.index_region_base = fy_durable_op_index_region_base,
	.index_region_size = fy_durable_op_index_region_size,
};

/* map (and, for the first writable user, create) the separate dedup-index file
 * series in this process.
 */
static int fy_durable_index_ensure(struct fy_durable_allocator *da)
{
	struct fy_durable_region *rg = &da->index;
	struct fy_durable_chunk_hdr *h0;
	char tmp[80], name[64];

	if (rg->region)			/* already mapped here */
		return 0;
	if (!da->separate_index)
		return -1;

	rg->chunks = calloc(rg->max_chunks, sizeof(*rg->chunks));
	if (!rg->chunks)
		return -1;
	if (fy_durable_reserve_region(rg) != 0)
		goto err_out;

	if (fy_atomic_load(rg->boot_head)) {
		/* the index series exists: attach to its immutable chunk 0 */
		h0 = fy_durable_ensure_mapped(da, rg, 0);
		if (!h0)
			goto err_out;
		fy_atomic_store(&rg->cur_head, h0);
	} else {
		if (da->read_only)
			goto err_out;

		/* create index chunk 0 (caller holds LOCK_EX) and publish its roots */
		if (fy_durable_make_chunk(da, rg, 0, tmp, sizeof(tmp)) != 0)
			goto err_out;
		snprintf(name, sizeof(name), "%s-0.bin", rg->prefix);
		if (renameat(da->dirfd, tmp, da->dirfd, name) != 0) {
			fy_durable_unmap_to_reserved(rg, 0);
			unlinkat(da->dirfd, tmp, 0);
			goto err_out;
		}
		h0 = fy_durable_hdr_for_gen(rg, 0);
		fy_atomic_store(&rg->chunks[0], h0);
		fy_atomic_store(&rg->cur_head, h0);
		fy_atomic_store(rg->boot_generation, 1);
		fy_atomic_store(rg->boot_head, h0);
		/* persist the boot page so the published index roots are durable */
		msync(da->content.region, da->pagesz, MS_SYNC);
	}

	/* reap any index orphans now the region is active (writable only) */
	if (!da->read_only)
		fy_durable_recover(da);
	return 0;

err_out:
	fy_durable_region_cleanup(rg);
	return -1;
}

/* create or attach the in-arena dedup index over the base allocator */
static struct fy_allocator *
fy_durable_dedup_enable(struct fy_durable_allocator *da, struct fy_allocator *base)
{
	FY_ATOMIC(struct fy_dedup_tag *) *slot = &da->boot->dedup_root;
	struct fy_dedup_allocator_cfg dcfg;
	struct fy_allocator *d;

	memset(&dcfg, 0, sizeof(dcfg));
	dcfg.parent_allocator = base;
	dcfg.dedup_threshold = 16;
	dcfg.estimated_content_size = da->content.chunk_size;
	if (da->separate_index) {
		/* route the index structures to the separate index region (tag 1),
		 * content stays in the content region (tag 0) */
		dcfg.use_explicit_tags = true;
		dcfg.content_tag = FY_DURABLE_CONTENT_TAG;
		dcfg.entries_tag = FY_DURABLE_INDEX_TAG;
	}

	if (fy_atomic_load(slot)) {
		/* index exists: map our view of the index region (attach, no lock)
		 * then attach the dedup layer */
		if (da->separate_index && fy_durable_index_ensure(da) != 0)
			return NULL;
		return fy_dedup_create_external(base, FY_ALLOC_TAG_DEFAULT, &dcfg, slot);
	}

	/* not created yet: a writable arena creates it once, serialised */
	if (da->read_only)
		return NULL;

	flock(da->dirfd, LOCK_EX);
	/* under the lock: create the index series (if separate) then the dedup
	 * root; create_external re-checks the slot and attaches if a peer won */
	if (!da->separate_index || fy_durable_index_ensure(da) == 0)
		d = fy_dedup_create_external(base, FY_ALLOC_TAG_DEFAULT, &dcfg, slot);
	else
		d = NULL;
	flock(da->dirfd, LOCK_UN);
	return d;
}

/* flush one region's mapped chunks' written prefixes */
static int fy_durable_region_msync(struct fy_durable_region *rg)
{
	struct fy_durable_chunk_hdr *hdr;
	size_t used;
	uint64_t i;
	int rc;

	if (!rg->region || !rg->chunks)
		return 0;

	rc = 0;
	for (i = 0; i < rg->max_chunks; i++) {
		hdr = fy_atomic_load(&rg->chunks[i]);
		if (!hdr)
			continue;
		used = fy_durable_hdr_off(rg, i) + fy_atomic_load(&hdr->next);
		if (msync(fy_durable_chunk_base(rg, i), used, MS_SYNC) != 0)
			rc = -1;
	}
	return rc;
}

/* flush every mapped chunk's written prefix to disk for both the content and (when active)
 * the index region */
static int fy_durable_msync_content(struct fy_durable_allocator *da)
{
	int rc = 0;

	if (fy_durable_region_msync(&da->content) != 0)
		rc = -1;
	if (fy_durable_region_msync(&da->index) != 0)
		rc = -1;
	return rc;
}

static int fy_durable_op_sync(struct fy_allocator *a)
{
	return fy_durable_msync_content(container_of(a, struct fy_durable_allocator, a));
}

static uint64_t fy_durable_op_refs_get(struct fy_allocator *a)
{
	struct fy_durable_allocator *da = container_of(a, struct fy_durable_allocator, a);

	return fy_atomic_load(&da->boot->refs_head);
}

static int fy_durable_op_refs_publish(struct fy_allocator *a, uint64_t expected,
				      uint64_t desired, unsigned int flags)
{
	struct fy_durable_allocator *da = container_of(a, struct fy_durable_allocator, a);
	uint64_t e = expected;

	if (da->read_only)
		return -1;

	if (flags & FY_ALLOC_REFS_CHECKPOINT) {
		if (fy_durable_msync_content(da) != 0)
			return -1;
	}

	if (!fy_atomic_compare_exchange_strong(&da->boot->refs_head, &e, desired))
		return 1;	/* lost: head moved; caller re-reads and retries */

	if (flags & FY_ALLOC_REFS_CHECKPOINT) {
		if (msync(da->content.region, da->pagesz, MS_SYNC) != 0)
			return -1;
	}

	return 0;
}

static uint64_t fy_durable_op_generation(struct fy_allocator *a)
{
	struct fy_durable_allocator *da = container_of(a, struct fy_durable_allocator, a);

	return fy_atomic_load(&da->boot->generation);
}

static unsigned int fy_durable_op_chunk_count(struct fy_allocator *a)
{
	struct fy_durable_allocator *da = container_of(a, struct fy_durable_allocator, a);
	struct fy_durable_chunk_hdr *hdr;
	unsigned int n = 0;

	for (hdr = fy_atomic_load(&da->boot->head); hdr;
	     hdr = fy_atomic_load(&hdr->next_chunk)) {
		if (!fy_durable_ensure_mapped_ptr(da, &da->content, hdr))
			break;
		n++;
	}
	return n;
}

static uint64_t fy_durable_op_region_base(struct fy_allocator *a)
{
	return container_of(a, struct fy_durable_allocator, a)->content.region_base;
}

static uint64_t fy_durable_op_region_size(struct fy_allocator *a)
{
	return container_of(a, struct fy_durable_allocator, a)->content.region_size;
}

static uint64_t fy_durable_op_index_region_base(struct fy_allocator *a)
{
	struct fy_durable_allocator *da = container_of(a, struct fy_durable_allocator, a);

	return da->separate_index ? da->index.region_base : 0;
}

static uint64_t fy_durable_op_index_region_size(struct fy_allocator *a)
{
	struct fy_durable_allocator *da = container_of(a, struct fy_durable_allocator, a);

	return da->separate_index ? da->index.region_size : 0;
}

#else

/* _WIN32: durable arenas are unsupported for now */

#endif /* _WIN32 */
