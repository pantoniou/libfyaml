/*
 * fy-cache.c - Internal transparent parse cache
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/mman.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#include <unistd.h>
#include <dirent.h>
#endif

#include <libfyaml.h>
#include <libfyaml/libfyaml-blake3.h>

#include "fy-cache.h"
#include "fy-parse.h"
#include "fy-utils.h"

#ifdef HAVE_GENERIC
#include "fy-generic.h"
#include "fy-generic-decoder.h"
#include "fy-allocator-dedup.h"

#if !defined(_WIN32) && !defined(MAP_FIXED_NOREPLACE)
#define MAP_FIXED_NOREPLACE 0
#endif
#if defined(__linux__) && !defined(RENAME_NOREPLACE)
#define RENAME_NOREPLACE (1U << 0)
#endif

#define FY_PARSE_CACHE_VERSION 6u
#define FY_PARSE_CACHE_ENDIAN 0x12345678u
#define FY_PARSE_CACHE_MIN_FILE_SIZE_ENV "FY_PARSE_CACHE_MIN_FILE_SIZE"
#define FY_PARSE_CACHE_FORCE_RELOCATE_ENV "FY_PARSE_CACHE_FORCE_RELOCATE"

enum fy_generic_document_builder_flags
fy_parse_cache_builder_flags(enum fy_parse_cfg_flags flags) FY_EXPORT;
bool fy_parse_cache_is_cache_file(const char *file) FY_EXPORT;
struct fy_parse_cache_entry *fy_parse_cache_load(struct fy_parser *fyp, const char *file) FY_EXPORT;
struct fy_parse_cache_entry *fy_parse_cache_load_file(const char *file) FY_EXPORT;
void fy_parse_cache_store_generic(const struct fy_parse_cfg *cfg, const char *file,
				  struct fy_generic_builder *gb, fy_generic vdir) FY_EXPORT;

int fy_parse_cache_build_append_document(struct fy_parser *fyp, fy_generic vdoc);
void fy_parse_cache_build_disable(struct fy_parser *fyp);
void fy_parse_cache_build_maybe_store(struct fy_parser *fyp);
int fy_parse_cache_build_start(struct fy_parser *fyp, const char *file);
void fy_parse_cache_build_maybe_store_on_end(struct fy_parser *fyp);

static const uint8_t fy_parse_cache_magic[8] = {
	0xff, 'F', 'Y', 'G', 'C', 'H', '0', '1'
};

static const uint8_t fy_parse_cache_index_magic[8] = {
	0xff, 'F', 'Y', 'G', 'I', 'H', '0', '1'
};

struct fy_parse_cache_header {
	char magic[8];
	uint32_t version;
	uint32_t endian;
	uint32_t ptr_size;
	uint32_t header_size;
	uint8_t b3sum[FY_BLAKE3_OUT_LEN];
	uint64_t parser_flags;
	uint64_t decoder_flags;
	uint64_t root;
	uint64_t arena_base;
	uint64_t arena_size;
	uint64_t map_base;
	uint64_t map_size;
	uint64_t arena_file_offset;
	uint64_t source_name_size;
	uint64_t has_index;
};

struct fy_parse_cache_index_header {
	char magic[8];
	uint32_t version;
	uint32_t endian;
	uint32_t ptr_size;
	uint32_t header_size;
	uint8_t b3sum[FY_BLAKE3_OUT_LEN];
	uint64_t index_root;	/* store-time absolute addr of merged tag-data in the blob */
	uint64_t arena_base;	/* index blob base */
	uint64_t arena_size;
	uint64_t map_base;
	uint64_t map_size;
	uint64_t arena_file_offset;
};

static struct fy_parse_cache_entry *
fy_parse_cache_load_file_with_validation(const char *file,
					 bool validate_cfg,
					 enum fy_parse_cfg_flags expect_flags,
					 enum fy_generic_decoder_parse_flags expect_dflags,
					 bool restore);

static void fy_parse_cache_bytes_to_hex(const uint8_t *data, size_t len, char *hex)
{
	static const char xdigits[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < len; i++) {
		hex[i * 2] = xdigits[data[i] >> 4];
		hex[i * 2 + 1] = xdigits[data[i] & 15];
	}
	hex[len * 2] = '\0';
}

static void fy_parse_cache_digest_to_hex(const uint8_t digest[FY_BLAKE3_OUT_LEN], char hex[65])
{
	fy_parse_cache_bytes_to_hex(digest, FY_BLAKE3_OUT_LEN, hex);
}

static bool fy_parse_cache_hex_to_digest(const char hex[65], uint8_t digest[FY_BLAKE3_OUT_LEN])
{
	size_t i;
	unsigned int v1, v2;

	for (i = 0; i < FY_BLAKE3_OUT_LEN; i++) {
		if (sscanf(hex + i * 2, "%1x%1x", &v1, &v2) != 2)
			return false;
		digest[i] = (uint8_t)((v1 << 4) | v2);
	}
	return hex[64] == '\0';
}

size_t fy_parse_cache_get_min_file_size(void)
{
	const char *s;
	char *end;
	unsigned long long value;

	s = getenv(FY_PARSE_CACHE_MIN_FILE_SIZE_ENV);
	if (!s || !*s)
		return FY_PARSE_CACHE_MIN_FILE_SIZE;

	errno = 0;
	value = strtoull(s, &end, 0);
	if (errno || end == s || *end != '\0')
		return FY_PARSE_CACHE_MIN_FILE_SIZE;
	if (value > SIZE_MAX)
		return FY_PARSE_CACHE_MIN_FILE_SIZE;

	return (size_t)value;
}

int fy_parse_cache_set_min_file_size(ssize_t min_size)
{
	char buf[32];
	int len;

	if (min_size == -1)
		return unsetenv(FY_PARSE_CACHE_MIN_FILE_SIZE_ENV);
	if (min_size < -1)
		return -1;

	len = snprintf(buf, sizeof(buf), "%zu", min_size);
	if (len < 0 || (size_t)len >= sizeof(buf))
		return -1;

	if (setenv(FY_PARSE_CACHE_MIN_FILE_SIZE_ENV, buf, 1))
		return -1;

	return 0;
}

static bool fy_parse_cache_force_relocate(void)
{
	const char *s;

	s = getenv(FY_PARSE_CACHE_FORCE_RELOCATE_ENV);
	return s && *s && strcmp(s, "0");
}

static size_t fy_parse_cache_regular_file_size(const char *file)
{
	struct stat sb;

	if (!file || !strcmp(file, "-"))
		return 0;

	if (stat(file, &sb))
		return 0;

	if ((sb.st_mode & S_IFMT) != S_IFREG)
		return 0;

	if (sb.st_size < 0)
		return 0;

	return (size_t)sb.st_size;
}

static bool fy_parse_cache_regular_file(const char *file, size_t *sizep)
{

	size_t size, min_size;

	if (sizep)
		*sizep = 0;

	size = fy_parse_cache_regular_file_size(file);
	if (!size)
		return false;

	min_size = fy_parse_cache_get_min_file_size();
	if (size < min_size)
		return false;

	if (sizep)
		*sizep = size;

	return true;
}

static int fy_parse_cache_publish(const char *tmp, const char *path)
{
	return fy_rename(tmp, path, true);
}

bool fy_parse_cache_enabled(const struct fy_parser *fyp, const char *file)
{
	return fyp && (fyp->cfg.flags & FYPCF_ENABLE_CACHE) &&
	       fy_parse_cache_regular_file(file, NULL);
}

static bool fy_parse_cache_cfg_enabled(const struct fy_parse_cfg *cfg, const char *file)
{
	return cfg && (cfg->flags & FYPCF_ENABLE_CACHE) &&
	       fy_parse_cache_regular_file(file, NULL);
}

static bool fy_parse_cache_cfg_enabled_for_data(const struct fy_parse_cfg *cfg, size_t size)
{
	return cfg && (cfg->flags & FYPCF_ENABLE_CACHE) &&
	       size >= fy_parse_cache_get_min_file_size();
}

static enum fy_generic_decoder_parse_flags
fy_parse_cache_decoder_flags(enum fy_parse_cfg_flags flags)
{
	enum fy_generic_decoder_parse_flags dflags = FYGDPF_MULTI_DOCUMENT;

	if (flags & FYPCF_KEEP_COMMENTS)
		dflags |= FYGDPF_KEEP_COMMENTS;
	if (flags & FYPCF_CREATE_MARKERS)
		dflags |= FYGDPF_CREATE_MARKERS;
	if (flags & FYPCF_KEEP_STYLE)
		dflags |= FYGDPF_KEEP_STYLE;
	return dflags;
}

enum fy_generic_document_builder_flags
fy_parse_cache_builder_flags(enum fy_parse_cfg_flags flags)
{
	enum fy_generic_document_builder_flags bflags = 0;

	if (flags & FYPCF_KEEP_COMMENTS)
		bflags |= FYGDBF_KEEP_COMMENTS;
	if (flags & FYPCF_CREATE_MARKERS)
		bflags |= FYGDBF_CREATE_MARKERS;
	if (flags & FYPCF_KEEP_STYLE)
		bflags |= FYGDBF_KEEP_STYLE;

	return bflags;
}

static bool fy_parse_cache_key_bytes_flags(enum fy_parse_cfg_flags flags_in,
					   const void *data, size_t size,
					   char hex[65])
{
	struct fy_blake3_hasher *h = NULL;
	struct fy_blake3_hasher_cfg hcfg;
	enum fy_parse_cfg_flags flags;
	enum fy_generic_decoder_parse_flags dflags;
	const uint8_t *digest;
	uint64_t meta[4];

	if (!data && size)
		return false;

	memset(&hcfg, 0, sizeof(hcfg));
	h = fy_blake3_hasher_create(&hcfg);
	if (!h)
		goto err_out;
	flags = flags_in & ~FYPCF_ENABLE_CACHE;
	dflags = fy_parse_cache_decoder_flags(flags);
	meta[0] = (uint64_t)flags;
	meta[1] = (uint64_t)dflags;
	meta[2] = (uint64_t)sizeof(void *);
	meta[3] = FY_PARSE_CACHE_VERSION;
	fy_blake3_hasher_update(h, "libfyaml-parse-cache", 20);
	fy_blake3_hasher_update(h, meta, sizeof(meta));
	fy_blake3_hasher_update(h, data, size);
	digest = fy_blake3_hasher_finalize(h);
	if (!digest)
		goto err_out;
	fy_parse_cache_digest_to_hex(digest, hex);
	fy_blake3_hasher_destroy(h);
	return true;

err_out:
	fy_blake3_hasher_destroy(h);
	return false;
}

static bool fy_parse_cache_key_flags(enum fy_parse_cfg_flags flags_in, const char *file, char hex[65])
{
	void *data;
	size_t size;
	int fd;
	bool ret = false;

	/* is a regular file? */
	if (!fy_parse_cache_regular_file(file, &size))
		return false;

	/* open it and mmap it */
	fd = open(file, O_RDONLY);
	if (fd >= 0) {
		data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (data != MAP_FAILED) {
			ret = fy_parse_cache_key_bytes_flags(flags_in, data, size, hex);
			munmap(data, size);
		}
		close(fd);
	}

	return ret;
}

static bool fy_parse_cache_key(struct fy_parser *fyp, const char *file, char hex[65])
{
	return fyp && fy_parse_cache_key_flags(fyp->cfg.flags, file, hex);
}

int fy_parse_cache_override(const char *override_dir)
{
	char buf[PATH_MAX];
	const char *real;
	int rc;

	if (override_dir) {
		real = fy_realpath(override_dir, buf, sizeof(buf));
		if (!real)
			return -1;

		rc = setenv("FY_PARSE_CACHE_OVERRIDE", real, 1);
		if (rc)
			return -1;
	} else {
		unsetenv("FY_PARSE_CACHE_OVERRIDE");
	}
	return 0;
}

const char *
fy_parse_cache_get_dir(char *buf, size_t size)
{
	const char *override_dir;
	char basebuf[PATH_MAX];
	const char *base;
	int len;

	if (!buf || !size) {
		errno = EINVAL;
		return NULL;
	}

	*buf = '\0';

	override_dir = getenv("FY_PARSE_CACHE_OVERRIDE");
	if (override_dir) {
		base = fy_realpath(override_dir, buf, size);
		if (base)
			return base;
	}

	base = fy_cachedir(basebuf, sizeof(basebuf));
	if (!base) {
		errno = EINVAL;
		return NULL;
	}

	len = snprintf(buf, (int)size, "%s" FY_PS "libfyaml" FY_PS "v%u", base, FY_PARSE_CACHE_VERSION);
	if ((size_t)len >= size) {
		errno = ENOSPC;
		return NULL;
	}

	return buf;
}

static int fy_parse_cache_mkdir_p(const char *path)
{
	char buf[PATH_MAX];
	size_t len;
	char *tmp, *p;

	len = strlen(path);
	if (len >= sizeof(buf) - 1)
		return -1;
	strcpy(buf, path);

	tmp = buf;
#ifdef _WIN32
	/* Skip only a leading drive-letter root prefix ("C:\"). */
	if ((((unsigned char)tmp[0] >= 'A' && (unsigned char)tmp[0] <= 'Z') ||
	     ((unsigned char)tmp[0] >= 'a' && (unsigned char)tmp[0] <= 'z')) &&
	    tmp[1] == ':' && tmp[2] == FY_PSC)
		p = tmp + 3;
	else
		p = tmp + 1;
#else
	p = tmp + 1;
#endif
	for (; *p; p++) {
		if (*p != FY_PSC)
			continue;
		*p = '\0';
		if (mkdir(tmp, 0700) && errno != EEXIST)
			return -1;
		*p = FY_PSC;
	}

	if (mkdir(tmp, 0700) && errno != EEXIST)
		return -1;

	return 0;
}

static const char *fy_parse_cache_path_from_hex(char *buf, size_t len, const char *hex, bool mkdirs)
{
	char dirbuf[PATH_MAX];
	const char *dir;
	char *sub;
	int rc;

	dir = fy_parse_cache_get_dir(dirbuf, sizeof(dirbuf));
	if (!dir)
		return NULL;

	sub = fy_sprintfa("%s" FY_PS "%c" FY_PS "%c" FY_PS "%c%c", dir, hex[0], hex[1], hex[2], hex[3]);
	if (mkdirs && fy_parse_cache_mkdir_p(sub) != 0)
		return NULL;

	rc = snprintf(buf, len, "%s" FY_PS "%s.fygc", sub, hex);
	if ((size_t)rc >= len)
		return NULL;

	return buf;
}

static const char *
fy_parse_cache_index_path(const char *content_path, char *buf, size_t len)
{
	size_t n;

	if (!content_path)
		return NULL;
	n = strlen(content_path);
	if (n < 5 || strcmp(content_path + n - 5, ".fygc"))
		return NULL;
	if (n + 1 > len)
		return NULL;
	memcpy(buf, content_path, n - 4);
	memcpy(buf + n - 4, "fygi", 5);	/* incl. NUL */
	return buf;
}

static const char *fy_parse_cache_stage_path(char *buf, size_t len, bool mkdirs)
{
	char dirbuf[PATH_MAX];
	const char *dir;
	char *stage_dir;
	int rc;

	dir = fy_parse_cache_get_dir(dirbuf, sizeof(dirbuf));
	if (!dir)
		return NULL;

	stage_dir = fy_sprintfa("%s" FY_PS ".staging", dir);
	if (mkdirs && fy_parse_cache_mkdir_p(stage_dir))
		return NULL;

	rc = snprintf(buf, len, "%s" FY_PS "fygc.XXXXXX", stage_dir);
	if ((size_t)rc >= len)
		return NULL;

	return buf;
}

static bool fy_parse_cache_align_payload(uintptr_t arena_base, size_t arena_size,
					 size_t header_size,
					 uintptr_t *map_basep,
					 size_t *map_sizep, size_t *arena_file_offsetp)
{
	size_t page_size, page_mask, page_delta, map_size, arena_file_offset;
	uintptr_t map_base;

	page_size = sysconf(_SC_PAGESIZE);
	page_mask = page_size - 1;

	map_base = arena_base & ~(uintptr_t)page_mask;
	page_delta = arena_base - map_base;
	if (arena_size > SIZE_MAX - page_delta)
		return false;

	map_size = fy_size_t_align(page_delta + arena_size, page_size);
	if (!map_size)
		return false;

	arena_file_offset = fy_size_t_align(header_size, page_size);
	*map_basep = map_base;
	*map_sizep = map_size;
	*arena_file_offsetp = arena_file_offset;
	return true;
}

/* allocate a writable, page-granular buffer to relocate cache data into and freeze */
static void *
fy_cache_reloc_buf(size_t size, size_t *alloc_size_out, bool *mmaped_out)
{
	void *p;
	size_t pgsz;
	size_t asz;

	pgsz = (size_t)sysconf(_SC_PAGESIZE);
	asz = fy_size_t_align(size, pgsz);
#ifndef _WIN32
	p = mmap(NULL, asz, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (p != MAP_FAILED) {
		*alloc_size_out = asz;
		*mmaped_out = true;
		return p;
	}
#endif
	p = fy_align_alloc(pgsz, asz);
	if (p) {
		*alloc_size_out = asz;
		*mmaped_out = false;
		return p;
	}

	return NULL;
}

/* freeze a relocated buffer/mapping read-only (no-op for plain malloc) */
static void
fy_cache_freeze_buf(void *p, size_t alloc_size, bool mmaped)
{
#ifndef _WIN32
	if (mmaped)
		mprotect(p, alloc_size, PROT_READ);
#else
	(void)p;
	(void)alloc_size;
	(void)mmaped;
#endif
}

bool fy_parse_cache_is_cache_file(const char *file)
{
	struct fy_parse_cache_header hdr;
	int fd;
	ssize_t rd;

	if (!file)
		return false;
	fd = open(file, O_RDONLY);
	if (fd < 0)
		return false;
	rd = read(fd, &hdr, sizeof(hdr));
	close(fd);
	if (rd != (ssize_t)sizeof(hdr))
		return false;
	return !memcmp(hdr.magic, fy_parse_cache_magic, sizeof(hdr.magic)) &&
	       hdr.version == FY_PARSE_CACHE_VERSION &&
	       hdr.endian == FY_PARSE_CACHE_ENDIAN &&
	       hdr.ptr_size == sizeof(void *) &&
	       hdr.header_size >= sizeof(hdr);
}

static struct fy_parse_cache_entry *
fy_parse_cache_entry_create(fy_generic vdir, void *arena, size_t arena_size,
			    void *map, size_t map_size, bool mmaped)
{
	struct fy_generic_iterator_cfg icfg;
	struct fy_parse_cache_entry *entry;

	entry = malloc(sizeof(*entry));
	if (!entry)
		return NULL;
	memset(entry, 0, sizeof(*entry));
	memset(&icfg, 0, sizeof(icfg));
	icfg.flags = FYGICF_WANT_STREAM_DOCUMENT_BODY_EVENTS |
		     FYGICF_HAS_FULL_DIRECTORY;
	icfg.vdir = vdir;
	entry->fygi = fy_generic_iterator_create_cfg(&icfg);
	if (!entry->fygi) {
		free(entry);
		return NULL;
	}
	entry->vdir = vdir;
	entry->arena = arena;
	entry->arena_size = arena_size;
	entry->map = map;
	entry->map_size = map_size;
	entry->mmaped = mmaped;
	return entry;
}

void fy_parse_cache_entry_destroy(struct fy_parse_cache_entry *entry)
{
	if (!entry)
		return;
	/* tear down the restored allocator before its mapped base disappears */
	if (entry->restored)
		fy_allocator_destroy(entry->restored);
	if (entry->restored_backing)
		fy_allocator_destroy(entry->restored_backing);
	/* the restored read-only layer pointed into the index blob: free it next */
	if (entry->index_map) {
		if (entry->index_mmaped)
			munmap(entry->index_map, entry->index_map_size);
		else
			fy_align_free(entry->index_map);
	}
	fy_generic_iterator_destroy(entry->fygi);
	if (entry->mmaped)
		munmap(entry->map, entry->map_size);
	else
		fy_align_free(entry->arena);

	free(entry);
}

struct fy_parse_cache_entry *fy_parse_cache_load(struct fy_parser *fyp, const char *file)
{
	char pathbuf[PATH_MAX];
	const char *path;
	char hex[65];
	enum fy_parse_cfg_flags flags;
	enum fy_generic_decoder_parse_flags dflags;

	if (!fy_parse_cache_enabled(fyp, file) || !fy_parse_cache_key(fyp, file, hex))
		return NULL;

	path = fy_parse_cache_path_from_hex(pathbuf, sizeof(pathbuf), hex, false);
	if (!path)
		return NULL;

	flags = fyp->cfg.flags & ~FYPCF_ENABLE_CACHE;
	dflags = fy_parse_cache_decoder_flags(flags);

	return fy_parse_cache_load_file_with_validation(path, true, flags, dflags, false);
}

struct fy_parse_cache_entry *fy_parse_cache_load_cfg(const struct fy_parse_cfg *cfg, const char *file)
{
	struct fy_parser *fyp;
	struct fy_parse_cache_entry *entry = NULL;

	if (!fy_parse_cache_cfg_enabled(cfg, file))
		return NULL;

	fyp = fy_parser_create(cfg);
	if (fyp)
		entry = fy_parse_cache_load(fyp, file);
	fy_parser_destroy(fyp);
	return entry;
}

struct fy_parse_cache_entry *fy_parse_cache_load_data_cfg(const struct fy_parse_cfg *cfg,
							  const void *data, size_t size)
{
	char pathbuf[PATH_MAX];
	const char *path;
	char hex[65];
	enum fy_parse_cfg_flags flags;
	enum fy_generic_decoder_parse_flags dflags;

	if (!fy_parse_cache_cfg_enabled_for_data(cfg, size) ||
	    !fy_parse_cache_key_bytes_flags(cfg->flags, data, size, hex))
		return NULL;

	path = fy_parse_cache_path_from_hex(pathbuf, sizeof(pathbuf), hex, false);
	if (!path)
		return NULL;

	flags = cfg->flags & ~FYPCF_ENABLE_CACHE;
	dflags = fy_parse_cache_decoder_flags(flags);

	return fy_parse_cache_load_file_with_validation(path, true, flags, dflags, false);
}

struct fy_parse_cache_entry *
fy_parse_cache_load_restore_cfg(const struct fy_parse_cfg *cfg, const char *file)
{
	char pathbuf[PATH_MAX];
	const char *path;
	char hex[65];
	enum fy_parse_cfg_flags flags;
	enum fy_generic_decoder_parse_flags dflags;

	if (!fy_parse_cache_cfg_enabled(cfg, file) ||
	    !fy_parse_cache_key_flags(cfg->flags, file, hex))
		return NULL;

	path = fy_parse_cache_path_from_hex(pathbuf, sizeof(pathbuf), hex, false);
	if (!path)
		return NULL;

	flags = cfg->flags & ~FYPCF_ENABLE_CACHE;
	dflags = fy_parse_cache_decoder_flags(flags);

	return fy_parse_cache_load_file_with_validation(path, true, flags, dflags, true);
}

struct fy_parse_cache_entry *
fy_parse_cache_load_restore_data_cfg(const struct fy_parse_cfg *cfg,
				     const void *data, size_t size)
{
	char pathbuf[PATH_MAX];
	const char *path;
	char hex[65];
	enum fy_parse_cfg_flags flags;
	enum fy_generic_decoder_parse_flags dflags;

	if (!fy_parse_cache_cfg_enabled_for_data(cfg, size) ||
	    !fy_parse_cache_key_bytes_flags(cfg->flags, data, size, hex))
		return NULL;

	path = fy_parse_cache_path_from_hex(pathbuf, sizeof(pathbuf), hex, false);
	if (!path)
		return NULL;

	flags = cfg->flags & ~FYPCF_ENABLE_CACHE;
	dflags = fy_parse_cache_decoder_flags(flags);

	return fy_parse_cache_load_file_with_validation(path, true, flags, dflags, true);
}

static int
fy_parse_cache_entry_attach_restore(struct fy_parse_cache_entry *entry, void *index_root)
{
	struct fy_dedup_restore_cfg rcfg;
	struct fy_allocator *backing, *restored;
	struct fy_dedup_tag_data *dtd;

	if (!index_root)
		return -1;

	dtd = index_root;

	backing = fy_allocator_create("mremap", NULL);
	if (!backing)
		return -1;

	memset(&rcfg, 0, sizeof(rcfg));
	rcfg.base.parent_allocator = backing;
	rcfg.base.dedup_threshold = dtd->dedup_threshold;
	rcfg.root = index_root;

	restored = fy_dedup_restore(FY_PARENT_ALLOCATOR_MALLOC, FY_ALLOC_TAG_NONE, &rcfg);
	if (!restored) {
		fy_allocator_destroy(backing);
		return -1;
	}

	entry->restored = restored;
	entry->restored_backing = backing;
	return 0;
}

/* fill a combined 2-entry reloc (content + index ranges), sorted by src */
static void
fy_cache_index_reloc(struct fy_arena_reloc rl[2],
		     const struct fy_parse_cache_header *chdr, void *content_dst,
		     const struct fy_parse_cache_index_header *ihdr, void *index_dst)
{
	struct fy_arena_reloc tmp;

	rl[0].src.i = (uintptr_t)chdr->arena_base;
	rl[0].srce.i = (uintptr_t)chdr->arena_base + (size_t)chdr->arena_size;
	rl[0].dst.p = content_dst;
	rl[0].size = (size_t)chdr->arena_size;
	rl[1].src.i = (uintptr_t)ihdr->arena_base;
	rl[1].srce.i = (uintptr_t)ihdr->arena_base + (size_t)ihdr->arena_size;
	rl[1].dst.p = index_dst;
	rl[1].size = (size_t)ihdr->arena_size;
	if (rl[1].src.i < rl[0].src.i) {	/* sort by src (only two entries) */
		tmp = rl[0];
		rl[0] = rl[1];
		rl[1] = tmp;
	}
}

/*
 * Load the sibling dedup-index file for @content_path and attach it to @entry so
 * it can keep deduplicating against the cached set.
 *
 * Map the index private+writable at its stored base. If it lands there its internal
 * pointers are already valid, and if the content also loaded
 * at its base (de->mem targets) no relocation is needed at all.
 *
 * If only the content moved, de->mem are rebased in place (copy-on-write touches
 * just those pages).
 *
 * If the base is unavailable (or a forced relocate / Windows) it falls back to
 * copying the index and relocating through a combined reloc spanning both the content
 * and index ranges.
 */
static void
fy_cache_restore_index(struct fy_parse_cache_entry *entry, const char *content_path,
		       const struct fy_parse_cache_header *chdr)
{
	char ipathbuf[PATH_MAX];
	const char *ipath;
	struct fy_parse_cache_index_header ihdr;
	struct fy_arena_reloc rl[2];
	uintptr_t map_base;
	size_t file_size, map_size, arena_file_offset, copy_size = 0;
	void *map = NULL, *copy = NULL, *iroot;
	bool content_at_base, force_reloc, copy_mmaped = false;
	int fd = -1, prot;
#ifdef _WIN32
	off_t read_off;
#endif

	ipath = fy_parse_cache_index_path(content_path, ipathbuf, sizeof(ipathbuf));
	if (!ipath)
		return;

	file_size = fy_parse_cache_regular_file_size(ipath);
	if (!file_size)
		return;

	fd = open(ipath, O_RDONLY);
	if (fd < 0)
		return;
	if (read(fd, &ihdr, sizeof(ihdr)) != sizeof(ihdr))
		goto out;

	if (memcmp(ihdr.magic, fy_parse_cache_index_magic, sizeof(ihdr.magic)) ||
	    ihdr.version != FY_PARSE_CACHE_VERSION ||
	    ihdr.endian != FY_PARSE_CACHE_ENDIAN ||
	    ihdr.ptr_size != sizeof(void *) ||
	    ihdr.header_size < sizeof(ihdr) ||
	    ihdr.arena_size == 0 || ihdr.arena_size > SIZE_MAX ||
	    ihdr.map_size == 0 || ihdr.map_size > SIZE_MAX ||
	    ihdr.arena_file_offset > SIZE_MAX ||
	    ihdr.arena_file_offset + ihdr.map_size < ihdr.arena_file_offset ||
	    ihdr.arena_file_offset + ihdr.map_size > (uint64_t)file_size ||
	    memcmp(ihdr.b3sum, chdr->b3sum, sizeof(ihdr.b3sum)))	/* must pair the content file */
		goto out;

	if (!fy_parse_cache_align_payload((uintptr_t)ihdr.arena_base, (size_t)ihdr.arena_size,
					  (size_t)ihdr.header_size, &map_base, &map_size,
					  &arena_file_offset) ||
	    ihdr.map_base != (uint64_t)map_base ||
	    ihdr.map_size != (uint64_t)map_size ||
	    ihdr.arena_file_offset != (uint64_t)arena_file_offset)
		goto out;

	content_at_base = (uintptr_t)entry->arena == (uintptr_t)chdr->arena_base;
	force_reloc = fy_parse_cache_force_relocate();
#ifdef _WIN32
	force_reloc = true;	/* no fixed mmap; use the copy path below */
#endif

	if (!force_reloc) {
		/*
		 * If the content also loaded at its base, an at-base index needs no
		 * relocation at all - map it read-only to enforce immutability. If only
		 * the content moved we must rebase de->mem in place, so map it
		 * writable (copy-on-write touches just those pages).
		 */
		prot = content_at_base ? PROT_READ : (PROT_READ | PROT_WRITE);

#ifdef MAP_FIXED_NOREPLACE
		map = mmap((void *)map_base, map_size, prot,
			   MAP_PRIVATE | MAP_FIXED_NOREPLACE, fd, (off_t)arena_file_offset);
#else
		map = mmap((void *)map_base, map_size, prot,
			   MAP_PRIVATE, fd, (off_t)arena_file_offset);
#endif
		if (map == MAP_FAILED)
			map = NULL;
		else if (map != (void *)map_base) {
			munmap(map, map_size);
			map = NULL;
		}
	}

	if (map) {
		/* index sits at its stored base: its own pointers are valid */
		iroot = (void *)(uintptr_t)ihdr.index_root;
		if (!content_at_base) {
			/* content moved: rebase de->mem in place (COW); the index
			 * range is an identity entry (already at base) */
			fy_cache_index_reloc(rl, chdr, entry->arena, &ihdr,
					     (void *)(uintptr_t)ihdr.arena_base);
			if (fy_dedup_index_relocate(rl, 2, (void *)(uintptr_t)ihdr.arena_base,
						    (size_t)ihdr.arena_size, iroot))
				goto out;
			/* relocated in place; now immutable -> enforce read-only */
			fy_cache_freeze_buf(map, map_size, true);
		}
		if (fy_parse_cache_entry_attach_restore(entry, iroot) == 0) {
			entry->index_map = map;		/* owned by the entry now */
			entry->index_map_size = map_size;
			entry->index_mmaped = true;
			map = NULL;
		}
		goto out;
	}

	/*
	 * fallback: copy the index into a private, page-granular buffer, relocate
	 * it, then freeze it read-only (it is a quiescent read-only base).
	 */
	copy = fy_cache_reloc_buf((size_t)ihdr.arena_size, &copy_size, &copy_mmaped);
	if (!copy)
		goto out;
#ifdef _WIN32
	read_off = (off_t)arena_file_offset +
		(off_t)((uintptr_t)ihdr.arena_base - (uintptr_t)map_base);

	if (lseek(fd, read_off, SEEK_SET) < 0 ||
	    read(fd, copy, (size_t)ihdr.arena_size) != (ssize_t)ihdr.arena_size)
		goto out;
#else
	map = mmap(NULL, map_size, PROT_READ, MAP_PRIVATE, fd, (off_t)arena_file_offset);
	if (map == MAP_FAILED) {
		map = NULL;
		goto out;
	}
	memcpy(copy, (char *)map + ((uintptr_t)ihdr.arena_base - (uintptr_t)map_base),
	       (size_t)ihdr.arena_size);
	munmap(map, map_size);
	map = NULL;
#endif

	fy_cache_index_reloc(rl, chdr, entry->arena, &ihdr, copy);
	iroot = fy_arena_reloc_ptr(rl, 2, copy, (size_t)ihdr.arena_size,
				   (void *)(uintptr_t)ihdr.index_root);
	if (!iroot)
		goto out;
	if (fy_dedup_index_relocate(rl, 2, copy, (size_t)ihdr.arena_size, iroot))
		goto out;
	fy_cache_freeze_buf(copy, copy_size, copy_mmaped);

	if (fy_parse_cache_entry_attach_restore(entry, iroot) == 0) {
		entry->index_map = copy;	/* owned by the entry now */
		entry->index_map_size = copy_size;
		entry->index_mmaped = copy_mmaped;
		copy = NULL;
	}

out:
	if (map)
		munmap(map, map_size);
	if (copy) {
		if (copy_mmaped)
			munmap(copy, copy_size);
		else
			fy_align_free(copy);
	}
	if (fd >= 0)
		close(fd);
}

static struct fy_parse_cache_entry *
fy_parse_cache_load_file_with_validation(const char *file,
					 bool validate_cfg,
					 enum fy_parse_cfg_flags expect_flags,
					 enum fy_generic_decoder_parse_flags expect_dflags,
					 bool restore)
{
	int fd = -1;
	struct fy_parse_cache_header hdr;
	bool aligned;
	void *copy = NULL;
	void *map = NULL;
#ifndef _WIN32
	void *arena;
#else
	off_t read_off;
#endif
	uintptr_t map_base;
	size_t file_size, map_size, arena_file_offset, copy_size = 0;
	fy_generic vdir;
	struct fy_arena_reloc reloc;
	struct fy_parse_cache_entry *entry = NULL;
	bool force_reloc, copy_mmaped = false;

	if (!file)
		return NULL;

	file_size = fy_parse_cache_regular_file_size(file);
	if (!file_size)
		return NULL;

	fd = open(file, O_RDONLY);
	if (fd < 0)
		goto out;

	if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr))
		goto out;

	if (memcmp(hdr.magic, fy_parse_cache_magic, sizeof(hdr.magic)) ||
	    hdr.version != FY_PARSE_CACHE_VERSION ||
	    hdr.endian != FY_PARSE_CACHE_ENDIAN ||
	    hdr.ptr_size != sizeof(void *) ||
	    hdr.header_size < sizeof(hdr) ||
	    hdr.source_name_size > SIZE_MAX ||
	    hdr.header_size < sizeof(hdr) + hdr.source_name_size ||
	    hdr.arena_size == 0 || hdr.arena_size > SIZE_MAX ||
	    hdr.arena_base + hdr.arena_size < hdr.arena_base ||
	    hdr.map_size == 0 || hdr.map_size > SIZE_MAX ||
	    hdr.arena_file_offset > SIZE_MAX ||
	    hdr.arena_file_offset + hdr.map_size < hdr.arena_file_offset ||
	    hdr.arena_file_offset + hdr.map_size > (uint64_t)file_size)
		goto out;

	if (validate_cfg &&
	    (hdr.parser_flags != (uint64_t)expect_flags ||
	     hdr.decoder_flags != (uint64_t)expect_dflags))
		goto out;

	aligned = fy_parse_cache_align_payload((uintptr_t)hdr.arena_base, (size_t)hdr.arena_size,
					  (size_t)hdr.header_size,
					  &map_base, &map_size,
					  &arena_file_offset);
	if (!aligned ||
	    hdr.map_base != (uint64_t)map_base ||
	    hdr.map_size != (uint64_t)map_size ||
	    hdr.arena_file_offset != (uint64_t)arena_file_offset)
		goto out;

	force_reloc = fy_parse_cache_force_relocate();
#ifdef _WIN32
	force_reloc |= true;	/* we don't have mmap fixed yet */
#endif

	if (!force_reloc) {
#ifdef MAP_FIXED_NOREPLACE
		map = mmap((void *)map_base, map_size, PROT_READ,
			   MAP_PRIVATE | MAP_FIXED_NOREPLACE, fd, (off_t)arena_file_offset);
#else
		/* no FIXED_NOREPLACE, make one last attempt by providing a base */
		map = mmap((void *)map_base, map_size, PROT_READ,
			   MAP_PRIVATE, fd, (off_t)arena_file_offset);
#endif

		if (map == MAP_FAILED) {
			map = NULL;
		} else if (map != (void *)map_base) {
			munmap(map, map_size);
			map = NULL;
		}
	}
	if (map) {
		vdir.v = (fy_generic_value)hdr.root;
		entry = fy_parse_cache_entry_create(vdir, (void *)(uintptr_t)hdr.arena_base,
						    (size_t)hdr.arena_size, map, map_size, true);
		if (entry) {
			map = NULL;
			if (restore && hdr.has_index)
				fy_cache_restore_index(entry, file, &hdr);
		}
		goto out;
	}
#ifdef _WIN32
	/*
	 * MapViewOfFile requires offsets aligned to dwAllocationGranularity (64KB),
	 * but arena_file_offset is only page-aligned (4KB). Use lseek+read instead.
	 */
	read_off = (off_t)arena_file_offset +
		   (off_t)((uintptr_t)hdr.arena_base - (uintptr_t)map_base);

	copy = fy_cache_reloc_buf((size_t)hdr.arena_size, &copy_size, &copy_mmaped);
	if (!copy)
		goto out;
	if (lseek(fd, read_off, SEEK_SET) < 0 ||
	    read(fd, copy, (size_t)hdr.arena_size) != (ssize_t)hdr.arena_size)
		goto out;
#else
	map = mmap(NULL, map_size, PROT_READ, MAP_PRIVATE, fd, (off_t)arena_file_offset);
	if (map == MAP_FAILED) {
		map = NULL;
		goto out;
	}
	arena = (char *)map + ((uintptr_t)hdr.arena_base - (uintptr_t)hdr.map_base);
	copy = fy_cache_reloc_buf((size_t)hdr.arena_size, &copy_size, &copy_mmaped);
	if (!copy)
		goto out;
	memcpy(copy, arena, (size_t)hdr.arena_size);
#endif
	vdir.v = (fy_generic_value)hdr.root;
	reloc.src.p = (void *)(uintptr_t)hdr.arena_base;
	reloc.srce.p = (void *)((uintptr_t)hdr.arena_base + (size_t)hdr.arena_size);
	reloc.dst.p = copy;
	reloc.size = (size_t)hdr.arena_size;
	vdir = fy_generic_arena_relocate(&reloc, 1, copy, (size_t)hdr.arena_size, vdir);
	/* relocated; the content is now immutable -> enforce read-only */
	fy_cache_freeze_buf(copy, copy_size, copy_mmaped);
	entry = fy_parse_cache_entry_create(vdir, copy,
					    (size_t)hdr.arena_size, copy, copy_size, copy_mmaped);
	if (entry) {
		copy = NULL;
		if (restore && hdr.has_index)
			fy_cache_restore_index(entry, file, &hdr);
	}

out:
	if (fd >= 0)
		close(fd);
	if (map)
		munmap(map, map_size);
	if (copy) {
		if (copy_mmaped)
			munmap(copy, copy_size);
		else
			fy_align_free(copy);
	}
	return entry;
}

struct fy_parse_cache_entry *fy_parse_cache_load_file(const char *file)
{
	return fy_parse_cache_load_file_with_validation(file, false, 0, 0, false);
}

int fy_parse_cache_file_info_load(const char *file, struct fy_parse_cache_file_info *info)
{
	struct fy_parse_cache_header hdr;
	int fd = -1;
	size_t file_size, path_len, source_len;

	if (!file || !info)
		return -1;

	file_size = fy_parse_cache_regular_file_size(file);
	if (!file_size)
		return -1;

	memset(info, 0, sizeof(*info));
	fd = open(file, O_RDONLY);
	if (fd < 0)
		goto err_out;
	if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr))
		goto err_out;
	if (memcmp(hdr.magic, fy_parse_cache_magic, sizeof(hdr.magic)) ||
	    hdr.version != FY_PARSE_CACHE_VERSION ||
	    hdr.endian != FY_PARSE_CACHE_ENDIAN ||
	    hdr.ptr_size != sizeof(void *) ||
	    hdr.header_size < sizeof(hdr) ||
	    hdr.source_name_size > SIZE_MAX ||
	    hdr.header_size < sizeof(hdr) + hdr.source_name_size ||
	    hdr.arena_size == 0 || hdr.arena_size > SIZE_MAX ||
	    hdr.map_size == 0 || hdr.map_size > SIZE_MAX ||
	    hdr.header_size > (uint64_t)file_size)
		goto err_out;

	path_len = strlen(file);
	if (path_len >= sizeof(info->path) - 1)
		path_len = sizeof(info->path) - 1;
	strncpy(info->path, file, sizeof(info->path) - 1);
	info->path[sizeof(info->path) - 1] = '\0';

	source_len = hdr.source_name_size;
	if (source_len >= sizeof(info->source_file) - 1)
		source_len = sizeof(info->source_file) - 1;

	if (source_len > 0) {
		if (lseek(fd, (off_t)sizeof(hdr), SEEK_SET) < 0 ||
		    read(fd, info->source_file, (size_t)source_len) != (ssize_t)source_len)
			goto err_out;
	}
	info->source_file[source_len] = '\0';

	fy_parse_cache_bytes_to_hex((const uint8_t *)hdr.magic, sizeof(hdr.magic), info->magic);
	fy_parse_cache_digest_to_hex(hdr.b3sum, info->b3sum);
	info->version = hdr.version;
	info->endian = hdr.endian;
	info->ptr_size = hdr.ptr_size;
	info->parser_flags = hdr.parser_flags;
	info->decoder_flags = hdr.decoder_flags;
	info->root = hdr.root;
	info->arena_base = hdr.arena_base;
	info->map_base = hdr.map_base;
	info->arena_file_offset = hdr.arena_file_offset;
	info->source_name_size = hdr.source_name_size;
	info->arena_size = (size_t)hdr.arena_size;
	info->map_size = (size_t)hdr.map_size;
	info->header_size = (size_t)hdr.header_size;
	info->index_root = 0;	/* index now lives in the sibling .fygi file */
	info->has_dedup_index = hdr.has_index != 0;
	close(fd);
	return 0;
err_out:
	if (fd >= 0)
		close(fd);
	return -1;
}

static int fy_cache_reloc_src_cmp(const void *va, const void *vb)
{
	const struct fy_arena_reloc *a = va, *b = vb;

	return a->src.i < b->src.i ? -1 : a->src.i > b->src.i ? 1 : 0;
}

/* count arenas (and total bytes) across all tags in @info */
static unsigned int
fy_cache_count_arenas(const struct fy_allocator_info *info)
{
	unsigned int i, j, n = 0;

	for (i = 0; i < info->num_tag_infos; i++)
		n += info->tag_infos[i].num_arena_infos;
	(void)j;
	return n;
}

/* pack every arena whose parent tag == @want_tag into a fresh malloc'd image */
static void *
fy_cache_pack_tag(const struct fy_allocator_info *info, int want_tag,
		  struct fy_arena_reloc *reloc, unsigned int *ridx, size_t *size_out)
{
	size_t total = 0, off = 0;
	unsigned int i, j;
	void *image;
	struct fy_allocator_arena_info *arena;

	for (i = 0; i < info->num_tag_infos; i++) {
		if (info->tag_infos[i].tag != want_tag)
			continue;
		for (j = 0; j < info->tag_infos[i].num_arena_infos; j++) {
			total = fy_size_t_align(total, FY_GENERIC_CONTAINER_ALIGN);
			total += info->tag_infos[i].arena_infos[j].size;
		}
	}
	*size_out = 0;
	if (!total)
		return NULL;

	image = malloc(total);
	if (!image)
		return NULL;

	for (i = 0; i < info->num_tag_infos; i++) {
		if (info->tag_infos[i].tag != want_tag)
			continue;
		for (j = 0; j < info->tag_infos[i].num_arena_infos; j++) {
			arena = &info->tag_infos[i].arena_infos[j];

			off = fy_size_t_align(off, FY_GENERIC_CONTAINER_ALIGN);
			memcpy((char *)image + off, arena->data, arena->size);
			reloc[*ridx].src.p = arena->data;
			reloc[*ridx].srce.p = (char *)arena->data + arena->size;
			reloc[*ridx].dst.p = (char *)image + off;
			reloc[*ridx].size = arena->size;
			(*ridx)++;
			off += arena->size;
		}
	}
	*size_out = total;
	return image;
}

/* build two separate images from @snap: the content and dedup-index arenas */
static int
fy_cache_build_split_images(const struct fy_allocator_snapshot *snap, fy_generic vdir,
			    fy_generic *content_root_out,
			    void **content_img_out, size_t *content_size_out,
			    void **index_img_out, size_t *index_size_out,
			    void **index_root_out)
{
	struct fy_arena_reloc *reloc;
	void *content_img = NULL, *index_img = NULL, *iroot = NULL;
	size_t content_size = 0, index_size = 0;
	unsigned int nar, ridx = 0;
	fy_generic rv;

	nar = fy_cache_count_arenas(snap->info);
	if (!nar)
		return -1;
	reloc = malloc(sizeof(*reloc) * nar);
	if (!reloc)
		return -1;

	content_img = fy_cache_pack_tag(snap->info, snap->content_tag, reloc, &ridx, &content_size);
	if (!content_img)
		goto err_out;	/* content is mandatory */
	index_img = fy_cache_pack_tag(snap->info, snap->index_tag, reloc, &ridx, &index_size);
	/* index may legitimately be empty; that is not an error */

	qsort(reloc, ridx, sizeof(*reloc), fy_cache_reloc_src_cmp);

	/* value tree lives in the content image; rebase through the combined map */
	rv = fy_generic_arena_relocate(reloc, ridx, content_img, content_size, vdir);
	if (fy_generic_is_invalid(rv))
		goto err_out;

	if (index_img) {
		iroot = fy_arena_reloc_ptr(reloc, ridx, NULL, 0, snap->root);
		if (!iroot)
			goto err_out;
		/* de->mem resolves via content reloc entries, index pointers via
		 * the index entries - all present in the combined array */
		if (fy_dedup_index_relocate(reloc, ridx, index_img, index_size, iroot))
			goto err_out;
	}

	free(reloc);
	*content_root_out = rv;
	*content_img_out = content_img;
	*content_size_out = content_size;
	*index_img_out = index_img;
	*index_size_out = index_size;
	*index_root_out = iroot;
	return 0;

err_out:
	free(reloc);
	free(content_img);
	free(index_img);
	return -1;
}

static int fy_cache_write_pad(FILE *fp, size_t pad)
{
	static const char zeros[4096];

	while (pad) {
		size_t chunk = pad < sizeof(zeros) ? pad : sizeof(zeros);

		if (fwrite(zeros, 1, chunk, fp) != chunk)
			return -1;
		pad -= chunk;
	}
	return 0;
}

static int
fy_cache_write_payload_file(const char *path, const void *hdr, size_t hdr_size,
			    const void *extra, size_t extra_size,
			    uintptr_t arena_base, uintptr_t map_base, size_t map_size,
			    size_t arena_file_offset, const void *blob, size_t blob_size)
{
	char tmpbuf[PATH_MAX];
	const char *tmp;
	size_t header_size = hdr_size + extra_size;
	size_t page_delta = arena_base - map_base;
	FILE *fp = NULL;
	int fd = -1, rc = -1;

	tmp = fy_parse_cache_stage_path(tmpbuf, sizeof(tmpbuf), true);
	if (!tmp)
		return -1;
	fd = fy_mkstemp(tmpbuf);
	if (fd < 0)
		return -1;
	fp = fdopen(fd, "wb");
	if (!fp) {
		close(fd);
		unlink(tmpbuf);
		return -1;
	}

	if (fwrite(hdr, 1, hdr_size, fp) != hdr_size)
		goto out;
	if (extra_size && fwrite(extra, 1, extra_size, fp) != extra_size)
		goto out;
	if (fy_cache_write_pad(fp, arena_file_offset + page_delta - header_size))
		goto out;
	if (fwrite(blob, 1, blob_size, fp) != blob_size)
		goto out;
	if (fy_cache_write_pad(fp, map_size - page_delta - blob_size))
		goto out;
	if (fflush(fp))
		goto out;
	if (fclose(fp)) {
		fp = NULL;
		goto out;
	}
	fp = NULL;
	if (fy_parse_cache_publish(tmpbuf, path) >= 0) {
		unlink(tmpbuf);
		rc = 0;
	}

out:
	if (fp)
		fclose(fp);
	if (rc != 0)
		unlink(tmpbuf);
	return rc;
}

/* fill the geometry + write the sibling dedup-index file (<hash>.fygi) */
static int
fy_cache_write_index_file(const char *content_path, const char *hex,
			  const void *index_img, size_t index_size, void *index_root)
{
	char ipathbuf[PATH_MAX];
	const char *ipath;
	struct fy_parse_cache_index_header ihdr;
	uintptr_t arena_base, map_base;
	size_t map_size, arena_file_offset;

	ipath = fy_parse_cache_index_path(content_path, ipathbuf, sizeof(ipathbuf));
	if (!ipath)
		return -1;

	arena_base = (uintptr_t)index_img;
	if (!fy_parse_cache_align_payload(arena_base, index_size, sizeof(ihdr),
					  &map_base, &map_size, &arena_file_offset))
		return -1;

	memset(&ihdr, 0, sizeof(ihdr));
	memcpy(ihdr.magic, fy_parse_cache_index_magic, sizeof(ihdr.magic));
	ihdr.version = FY_PARSE_CACHE_VERSION;
	ihdr.endian = FY_PARSE_CACHE_ENDIAN;
	ihdr.ptr_size = sizeof(void *);
	ihdr.header_size = sizeof(ihdr);
	if (!fy_parse_cache_hex_to_digest(hex, ihdr.b3sum))
		return -1;
	ihdr.index_root = (uint64_t)(uintptr_t)index_root;
	ihdr.arena_base = (uint64_t)arena_base;
	ihdr.arena_size = (uint64_t)index_size;
	ihdr.map_base = (uint64_t)map_base;
	ihdr.map_size = (uint64_t)map_size;
	ihdr.arena_file_offset = (uint64_t)arena_file_offset;

	return fy_cache_write_payload_file(ipath, &ihdr, sizeof(ihdr), NULL, 0,
					   arena_base, map_base, map_size,
					   arena_file_offset, index_img, index_size);
}

static void fy_parse_cache_store_generic_keyed(const struct fy_parse_cfg *cfg,
					       const char *hex,
					       const char *source_text,
					       struct fy_generic_builder *gb,
					       fy_generic vdir)
{
	char pathbuf[PATH_MAX];
	const char *path;
	enum fy_parse_cfg_flags cache_flags;
	enum fy_generic_decoder_parse_flags dflags;
	fy_generic content_root;
	void *content_img = NULL, *index_img = NULL, *index_root = NULL;
	size_t content_size = 0, index_size = 0, map_size, arena_file_offset;
	uintptr_t arena_base, map_base;
	struct fy_parse_cache_header hdr;
	size_t source_name_size, header_size;
	struct fy_allocator_snapshot snap;
	bool have_split = false;

	if (!cfg || !hex || !source_text)
		return;

	path = fy_parse_cache_path_from_hex(pathbuf, sizeof(pathbuf), hex, true);
	if (!path)
		return;

	cache_flags = cfg->flags & ~FYPCF_ENABLE_CACHE;
	dflags = fy_parse_cache_decoder_flags(cache_flags);
	content_root = vdir;

	/* if the builder's allocator can snapshot its dedup index, split it into
	 * a content image and an index image. */
	if (fy_allocator_snapshot(gb->allocator, gb->alloc_tag, &snap) == 0) {
		have_split = fy_cache_build_split_images(&snap, vdir, &content_root,
							 &content_img, &content_size,
							 &index_img, &index_size,
							 &index_root) == 0;
		fy_allocator_snapshot_release(gb->allocator, &snap);
	}

	if (!have_split) {
		content_root = vdir;
		/* builder owns this buffer - do not free it (unlike the split images) */
		content_img = (void *)fy_generic_builder_linearize(gb, &content_root, &content_size);
		index_img = NULL;
		index_root = NULL;
	}
	if (!content_img || !content_size || fy_generic_is_invalid(content_root))
		goto out;

	source_name_size = strlen(source_text) + 1;
	header_size = sizeof(hdr) + source_name_size;
	arena_base = (uintptr_t)content_img;
	if (!fy_parse_cache_align_payload(arena_base, content_size, header_size,
					  &map_base, &map_size, &arena_file_offset))
		goto out;

	memset(&hdr, 0, sizeof(hdr));
	memcpy(hdr.magic, fy_parse_cache_magic, sizeof(hdr.magic));
	hdr.version = FY_PARSE_CACHE_VERSION;
	hdr.endian = FY_PARSE_CACHE_ENDIAN;
	hdr.ptr_size = sizeof(void *);
	if (!fy_parse_cache_hex_to_digest(hex, hdr.b3sum))
		goto out;
	hdr.parser_flags = (uint64_t)cache_flags;
	hdr.decoder_flags = (uint64_t)dflags;
	hdr.root = (uint64_t)content_root.v;
	hdr.arena_base = (uint64_t)arena_base;
	hdr.arena_size = (uint64_t)content_size;
	hdr.map_base = (uint64_t)map_base;
	hdr.map_size = (uint64_t)map_size;
	hdr.arena_file_offset = (uint64_t)arena_file_offset;
	hdr.source_name_size = (uint64_t)source_name_size;
	hdr.header_size = (uint32_t)header_size;
	hdr.has_index = index_img ? 1 : 0;

	/* write the index first: if it fails, fall back to a content-only file */
	if (index_img &&
	    fy_cache_write_index_file(path, hex, index_img, index_size, index_root) != 0)
		hdr.has_index = 0;

	if (fy_cache_write_payload_file(path, &hdr, sizeof(hdr),
					source_text, source_name_size,
					arena_base, map_base, map_size,
					arena_file_offset, content_img, content_size) != 0) {
		/* content failed: drop any orphaned index file we just wrote */
		if (hdr.has_index) {
			char ipathbuf[PATH_MAX];
			const char *ipath = fy_parse_cache_index_path(path, ipathbuf, sizeof(ipathbuf));
			if (ipath)
				unlink(ipath);
		}
	}

out:
	if (have_split)
		free(content_img);	/* split image is malloc'd; linearize buffer is builder-owned */
	free(index_img);
}

void fy_parse_cache_store_generic(const struct fy_parse_cfg *cfg, const char *file,
				  struct fy_generic_builder *gb, fy_generic vdir)
{
	char realpathbuf[PATH_MAX];
	char hex[65];
	const char *source_name;
	const char *source_text;

	if (!fy_parse_cache_cfg_enabled(cfg, file) ||
	    !fy_parse_cache_key_flags(cfg->flags, file, hex))
		return;

	source_name = fy_realpath(file, realpathbuf, sizeof(realpathbuf));

	source_text = source_name ? source_name : file;

	fy_parse_cache_store_generic_keyed(cfg, hex, source_text, gb, vdir);
}

void fy_parse_cache_store_data_generic(const struct fy_parse_cfg *cfg,
				       const void *data, size_t size,
				       const char *source_name,
				       struct fy_generic_builder *gb,
				       fy_generic vdir)
{
	char hex[65];

	if (!fy_parse_cache_cfg_enabled_for_data(cfg, size) ||
	    !fy_parse_cache_key_bytes_flags(cfg->flags, data, size, hex))
		return;

	fy_parse_cache_store_generic_keyed(cfg, hex,
					   source_name ? source_name : "<buffer>",
					   gb, vdir);
}

int fy_parse_cache_set_input_file(struct fy_parser *fyp, const char *file)
{
	struct fy_input_cfg fyic;
	struct fy_parse_cache_entry *cache_entry = NULL;
	int rc;

	/* is it a binary cache file directly? */
	if (fy_parse_cache_is_cache_file(file)) {

		/* must be a regular cache file */
		if (!fy_parse_cache_regular_file(file, NULL))
			return 0;

		cache_entry = fy_parse_cache_load_file(file);
		fyp_error_check(fyp, cache_entry != NULL, err_out,
				"fy_parse_cache_load_file() failed on cached file %s",
					file);
	} else {

		/* cache must be enabled and file fulfiling conditions */
		if (!fy_parse_cache_enabled(fyp, file))
			return 0;

		/* is a cache entry available? */
		cache_entry = fy_parse_cache_load(fyp, file);

		/* no cache entry - start building it */
		if (!cache_entry) {
			rc = fy_parse_cache_build_start(fyp, file);
			fyp_error_check(fyp, !rc, err_out,
					"fy_parse_cache_build_start() failed");
			return 0;
		}
	}

	memset(&fyic, 0, sizeof(fyic));
	fyic.type = fyit_geniter;
	fyic.userdata = cache_entry;
	fyic.geniter.flags = FYPEGF_GENERATE_STREAM_EVENTS |
			     FYPEGF_GENERATE_DOCUMENT_EVENTS;
	fyic.geniter.fygi = cache_entry->fygi;
	fyic.geniter.owns_iterator = true;

	rc = fy_parse_input_append(fyp, &fyic);
	fyp_error_check(fyp, !rc, err_out,
			"fy_parse_input_append() failed");

	/* > 0 means cache hit */
	return 1;
err_out:
	rc = -1;
	fy_parse_cache_entry_destroy(cache_entry);
	return rc;
}

int fy_parse_cache_build_append_document(struct fy_parser *fyp, fy_generic vdoc)
{
	fy_generic vdir;

	if (!fyp || fy_generic_is_invalid(vdoc))
		return -1;

	if (!fy_generic_is_sequence(fyp->cache_build.vdir))
		vdir = fy_gb_sequence(fyp->cache_build.gb, vdoc);
	else
		vdir = fy_append(fyp->cache_build.gb, fyp->cache_build.vdir, vdoc);

	if (fy_generic_is_invalid(vdir))
		return -1;
	fyp->cache_build.vdir = vdir;

	return 0;
}

void fy_parse_cache_build_cleanup(struct fy_parser *fyp)
{
	if (!fyp)
		return;

	fy_generic_document_builder_destroy(fyp->cache_build.fygdb);
	fyp->cache_build.fygdb = NULL;

	fy_generic_builder_destroy(fyp->cache_build.gb);
	fyp->cache_build.gb = NULL;

	fyp->cache_build.vdir = fy_invalid;
	free(fyp->cache_build.file);
	fyp->cache_build.file = NULL;
	fyp->cache_build.enabled = false;
}

void fy_parse_cache_build_disable(struct fy_parser *fyp)
{
	if (!fyp)
		return;

	fy_parse_cache_build_cleanup(fyp);
}

void fy_parse_cache_build_maybe_store(struct fy_parser *fyp)
{
	if (!fyp || !fyp->cache_build.enabled || !fyp->cache_build.file ||
	    !fyp->cache_build.gb || !fy_generic_is_sequence(fyp->cache_build.vdir))
		goto out;

	fy_parse_cache_store_generic(&fyp->cfg, fyp->cache_build.file,
				     fyp->cache_build.gb, fyp->cache_build.vdir);
out:
	fy_parse_cache_build_cleanup(fyp);
}

int fy_parse_cache_build_start(struct fy_parser *fyp, const char *file)
{
	struct fy_generic_builder_cfg gbcfg;
	struct fy_generic_document_builder_cfg gdbcfg;
	enum fy_parse_cfg_flags cache_flags;
	char *dupfile = NULL;

	if (!fy_parse_cache_enabled(fyp, file))
		return 0;

	dupfile = strdup(file);
	if (!dupfile)
		return -1;

	cache_flags = fyp->cfg.flags & ~FYPCF_ENABLE_CACHE;

	memset(&gbcfg, 0, sizeof(gbcfg));
	gbcfg.flags = FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED;
	gbcfg.estimated_max_size = fy_parse_estimate_queued_input_size(fyp);
	gbcfg.diag = fyp->cfg.diag;
	fyp->cache_build.gb = fy_generic_builder_create(&gbcfg);
	if (!fyp->cache_build.gb)
		goto err_out;

	memset(&gdbcfg, 0, sizeof(gdbcfg));
	gdbcfg.parse_cfg = fyp->cfg;
	gdbcfg.parse_cfg.flags = cache_flags;
	gdbcfg.gb = fyp->cache_build.gb;
	gdbcfg.diag = fy_diag_ref(fyp->cfg.diag);
	gdbcfg.flags = fy_parse_cache_builder_flags(cache_flags);
	fyp->cache_build.fygdb = fy_generic_document_builder_create(&gdbcfg);
	if (!fyp->cache_build.fygdb)
		goto err_out;

	fyp->cache_build.file = dupfile;
	fyp->cache_build.enabled = true;
	return 0;

err_out:
	free(dupfile);
	fy_parse_cache_build_cleanup(fyp);
	return -1;
}

void fy_parse_cache_build_process_event_private(struct fy_parser *fyp, struct fy_eventp *fyep)
{
	struct fy_event *fye;
	int rc;

	if (!fyp || !fyp->cache_build.enabled || !fyp->cache_build.fygdb)
		return;

	if (!fyep) {
		fy_parse_cache_build_maybe_store_on_end(fyp);
		return;
	}

	fye = &fyep->e;

	rc = fy_generic_document_builder_process_event(fyp->cache_build.fygdb, fye);
	if (rc < 0) {
		fy_parse_cache_build_disable(fyp);
		return;
	}
	if (rc > 0 && fy_generic_document_builder_is_document_complete(fyp->cache_build.fygdb)) {
		fy_generic vdoc = fy_generic_document_builder_take_document(fyp->cache_build.fygdb);
		if (fy_generic_is_invalid(vdoc) ||
		    fy_parse_cache_build_append_document(fyp, vdoc))
			fy_parse_cache_build_disable(fyp);
	}
	if (fye->type == FYET_STREAM_END)
		fy_parse_cache_build_maybe_store(fyp);
}

void fy_parse_cache_build_maybe_store_on_end(struct fy_parser *fyp)
{
	if (!fyp || !fyp->cache_build.enabled)
		return;
	if (fyp->stream_error)
		fy_parse_cache_build_disable(fyp);
	else if (fyp->state == FYPS_END)
		fy_parse_cache_build_maybe_store(fyp);
}

static int fy_parse_cache_walk_internal(const char *path, fy_parse_cache_walk_cb cb, void *userdata)
{
	DIR *dir = NULL;
	struct dirent *de;
	char child[PATH_MAX];
	struct stat sb;
	struct fy_parse_cache_file_info info;
	int rc = 0, len;

	dir = opendir(path);
	if (!dir) {
		if (errno == ENOENT)
			return 0;
		return -1;
	}

	while ((de = readdir(dir)) != NULL) {

		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;

		len = snprintf(child, sizeof(child), "%s" FY_PS "%s", path, de->d_name);
		if ((size_t)len >= sizeof(child))
			goto err_out;

		if (stat(child, &sb))
			continue;

		if (S_ISDIR(sb.st_mode)) {
			rc = fy_parse_cache_walk_internal(child, cb, userdata);
			if (rc < 0)
				goto err_out;
			continue;
		}

		if (!S_ISREG(sb.st_mode) || !strstr(de->d_name, ".fygc"))
			continue;

		memset(&info, 0, sizeof(info));

		if (!fy_parse_cache_file_info_load(child, &info)) {
			rc = cb(child, &info, userdata);
			if (rc)
				goto out;
		}
	}
out:
	closedir(dir);
	return rc;

err_out:
	rc = -1;
	goto out;
}

int fy_parse_cache_walk(fy_parse_cache_walk_cb cb, void *userdata)
{
	char dirbuf[PATH_MAX];
	const char *dir;

	dir = fy_parse_cache_get_dir(dirbuf, sizeof(dirbuf));
	if (!dir)
		return -1;

	return fy_parse_cache_walk_internal(dir, cb, userdata);
}

#else

/* if we don't have generics the cache doesn't work, but the methods are still defined */
const char *fy_parse_cache_get_dir(char *buf FY_UNUSED,
				   size_t size FY_UNUSED)
{
	return NULL;
}

int fy_parse_cache_file_info_load(const char *file FY_UNUSED,
				  struct fy_parse_cache_file_info *info FY_UNUSED)
{
	return -1;
}

int fy_parse_cache_walk(fy_parse_cache_walk_cb cb FY_UNUSED,
			void *userdata FY_UNUSED)
{
	return -1;
}

int fy_parse_cache_override(const char *override_dir FY_UNUSED)
{
	return -1;
}

size_t fy_parse_cache_get_min_file_size(void)
{
	return FY_PARSE_CACHE_MIN_FILE_SIZE;
}

int fy_parse_cache_set_min_file_size(ssize_t min_size FY_UNUSED)
{
	return -1;
}

#endif
