#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#endif

#include "fy-bit64.h"

#include "blake3.h"
#include "blake3_impl.h"
#include "blake3_internal.h"

#include "fy-thread.h"
#include "fy-win32.h"

/* 256K threshold for using alloca */
#define BLAKE3_ALLOCA_BUFFER_SIZE	(256U << 10)
#define BLAKE3_FILE_IO_BUFFER_SIZE	BLAKE3_ALLOCA_BUFFER_SIZE
#define BLAKE3_MMAP_MIN_CHUNKSIZE	(1U << 20)	// minimum chunksize is 1MB
#define BLAKE3_MMAP_MAX_CHUNKSIZE	SIZE_MAX

static int probe_backends(blake3_host_state *hs)
{
	hs->supported_backends = blake3_get_supported_backends();
	hs->detected_backends = blake3_get_detected_backends();
	hs->selectable_backends = hs->supported_backends & hs->detected_backends;

	return 0;
}

static int select_backends(blake3_host_state *hs)
{
	const blake3_backend *be, *be_portable;

	/* this is the fallback */
	be_portable = blake3_get_backend_by_id(B3BID_PORTABLE);
	assert(be_portable);

	/* if there's a backend selected, try to force it */
	if (hs->cfg.backend && hs->cfg.backend[0] && strcmp(hs->cfg.backend, "auto")) {
		be = blake3_get_backend_by_name(hs->cfg.backend);

		/* if backend exists and is selectable */
		if (be && (hs->selectable_backends & FY_BIT64(be->info.id))) {
			hs->hash_many_be = (be->info.funcs & B3FF_HASH_MANY) ? be : be_portable;
			hs->compress_xof_be = (be->info.funcs & B3FF_COMPRESS_XOF) ? be : be_portable;
			hs->compress_in_place_be = (be->info.funcs & B3FF_COMPRESS_IN_PLACE) ? be : be_portable;

			/* and select the ops (piggyback on HASH_MANY) */
			hs->hasher_ops = (be->info.funcs & B3FF_HASH_MANY) ? be->hasher_ops : be_portable->hasher_ops;
		}
	}

	if (!hs->hash_many_be) {
		hs->hash_many_be = blake3_backend_select_function(hs->selectable_backends, B3FID_HASH_MANY);
		hs->hasher_ops = hs->hash_many_be->hasher_ops;
	}
	if (!hs->compress_xof_be)
		hs->compress_xof_be = blake3_backend_select_function(hs->selectable_backends, B3FID_COMPRESS_XOF);
	if (!hs->compress_in_place_be)
		hs->compress_in_place_be = blake3_backend_select_function(hs->selectable_backends, B3FID_COMPRESS_IN_PLACE);

	/* select methods */
	hs->hash_many = hs->hash_many_be->hash_many;
	hs->compress_xof = hs->compress_xof_be->compress_xof;
	hs->compress_in_place = hs->compress_in_place_be->compress_in_place;

	/* select maximum simd degree */
	if (hs->simd_degree < hs->hash_many_be->info.simd_degree)
		hs->simd_degree = hs->hash_many_be->info.simd_degree;
	if (hs->simd_degree < hs->compress_xof_be->info.simd_degree)
		hs->simd_degree = hs->compress_xof_be->info.simd_degree;
	if (hs->simd_degree < hs->compress_in_place_be->info.simd_degree)
		hs->simd_degree = hs->compress_in_place_be->info.simd_degree;

	return 0;
}

static void dump_backends(blake3_host_state *hs, uint64_t backends)
{
	const blake3_backend *be;
	int i;

	for (i = 0; i < B3BID_COUNT; i++) {
		if (!(backends & FY_BIT64(i)))
			continue;
		be = blake3_get_backend_by_id(i);
		assert(be);

		fprintf(stderr, "%sname: %s\n%sdescription: %s\n%ssimd_degree: %u\n%shas_hash_many: %s\n%shas_compress_xof: %s\n%shas_compress_in_place: %s\n",
				" -", be->info.name,
				"  ", be->info.description,
				"  ", be->info.simd_degree,
				"  ", (be->info.funcs & B3FF_HASH_MANY) ? "true" : "false",
				"  ", (be->info.funcs & B3FF_COMPRESS_XOF) ? "true" : "false",
				"  ", (be->info.funcs & B3FF_COMPRESS_IN_PLACE) ? "true" : "false");
	}
}

int blake3_host_state_setup(blake3_host_state *hs, const blake3_host_config *cfg)
{
	struct fy_thread_pool_cfg tp_cfg;
	long scval;
	int rc;

	(void)rc;

	assert(hs);
	assert(cfg);

	memset(hs, 0, sizeof(*hs));
	hs->cfg = *cfg;

	scval = sysconf(_SC_NPROCESSORS_ONLN);
	assert(scval > 0);

	hs->num_cpus = (unsigned int)scval;

	rc = probe_backends(hs);
	assert(!rc);

	rc = select_backends(hs);
	assert(!rc);

	hs->mt_degree = hs->cfg.mt_degree > 0 ? hs->cfg.mt_degree : 64;	/* 64K default */
	hs->tp = NULL;

	if (!hs->cfg.no_mthread) {

		if (!hs->cfg.tp) {
			memset(&tp_cfg, 0, sizeof(tp_cfg));
			tp_cfg.flags = FYTPCF_STEAL_MODE;
			tp_cfg.num_threads = hs->cfg.num_threads ? hs->cfg.num_threads : (hs->num_cpus * 3) / 2;
			tp_cfg.userdata = NULL;
			hs->tp = fy_thread_pool_create(&tp_cfg);
			if (!hs->tp)
				goto err_out;
		} else
			hs->tp = hs->cfg.tp;
	}

	hs->file_io_bufsz = hs->cfg.file_io_bufsz ? hs->cfg.file_io_bufsz : BLAKE3_FILE_IO_BUFFER_SIZE;
	hs->mmap_min_chunk = hs->cfg.mmap_min_chunk ? hs->cfg.mmap_min_chunk : BLAKE3_MMAP_MIN_CHUNKSIZE;
	hs->mmap_max_chunk = hs->cfg.mmap_max_chunk ? hs->cfg.mmap_max_chunk : BLAKE3_MMAP_MAX_CHUNKSIZE;

	if (hs->cfg.debug) {

		fprintf(stderr, "num_cpus: %u\n", hs->num_cpus);
		fprintf(stderr, "num_threads: %d\n", hs->tp ? fy_thread_pool_get_num_threads(hs->tp) : 0);
		fprintf(stderr, "simd_degree: %u\n", hs->simd_degree);
		fprintf(stderr, "mt_degree: %u\n", hs->mt_degree);
		fprintf(stderr, "file_io_bufsz: %zu\n", hs->file_io_bufsz);
		fprintf(stderr, "mmap_min_chunk: %zu\n", hs->mmap_min_chunk);
		fprintf(stderr, "mmap_max_chunk: %zu\n", hs->mmap_max_chunk);

		fprintf(stderr, "supported_backends:\n");
		dump_backends(hs, hs->supported_backends);
		fprintf(stderr, "detected_backends:\n");
		dump_backends(hs, hs->detected_backends);

		fprintf(stderr, "selected-backends:\n");
		fprintf(stderr, "  hash_many: %s\n", hs->hash_many_be->info.name);
		fprintf(stderr, "  compress_xof: %s\n", hs->compress_xof_be->info.name);
		fprintf(stderr, "  compress_in_place: %s\n", hs->compress_in_place_be->info.name);

	}

	return 0;

err_out:
	return -1;
}

void blake3_host_state_cleanup(blake3_host_state *hs)
{
	assert(hs);

	/* destroy the thread pool if we're the ones created it */
	if (hs->tp && !hs->cfg.tp)
		fy_thread_pool_destroy(hs->tp);
}

blake3_host_state *blake3_host_state_create(const blake3_host_config *cfg)
{
	blake3_host_state *hs;
	int rc;

	hs = malloc(sizeof(*hs));
	if (!hs)
		return NULL;
	rc = blake3_host_state_setup(hs, cfg);
	if (rc)
		goto err_out;

	return hs;

err_out:
	if (hs)
		free(hs);
	return NULL;
}

void blake3_host_state_destroy(blake3_host_state *hs)
{
	if (!hs)
		return;
	blake3_host_state_cleanup(hs);
	free(hs);
}

blake3_hasher *blake3_hasher_create(blake3_host_state *hs, const uint8_t *key, const void *context, size_t context_len)
{
	blake3_hasher *self;

	self = fy_cacheline_alloc(sizeof(*self));
	if (!self)
		return NULL;

	if (!key && !context)
		blake3_hasher_init(hs, self);
	else if (key)
		blake3_hasher_init_keyed(hs, self, key);
	else if (context_len == 0)
		blake3_hasher_init_derive_key(hs, self, context);
	else
		blake3_hasher_init_derive_key_raw(hs, self, context, context_len);
	return self;
}

void blake3_hasher_destroy(blake3_hasher *self)
{
	if (!self)
		return;
	fy_cacheline_free(self);
}

#if defined(__linux__)

static int linux_block_dev_is_rotational(dev_t dev)
{
	char *path = NULL;
	int rc, fd;
	uint8_t c[2];
	int rotational;

	/* by default it's the filesize */
	rotational = -1;	/* not found */

	/* read the rotational attribute of the root */
	rc = asprintf(&path, "/sys/dev/block/%u:%u/queue/rotational", major(dev), 0);
	if (rc != -1) {
		fd = open(path, O_RDONLY);
		if (fd != -1) {
			if (read(fd, c, 2) == 2)
				rotational = c[0] == '1' && c[1] == '\n';
			close(fd);
		}
		free(path);
	}

	return rotational;
}

static ssize_t linux_file_cached_size(int fd, void *mem, size_t filesize)
{
	long pagesize = sysconf(_SC_PAGESIZE);
	size_t vec_size, resident_size, i;
	unsigned char *vec;
	int rc;

	(void)rc;
	vec_size = (filesize + pagesize - 1) / pagesize;
	vec = malloc(vec_size + 1);
	if (!vec)
		return -1;

	rc = mincore(mem, filesize, vec);
	assert(!rc);

	/* this could be parallelized */
	resident_size = 0;
	for (i = 0; i < vec_size; i++) {
		if (vec[i] & 1)
			resident_size += pagesize;
	}

	free(vec);

	if (resident_size > filesize)
		resident_size = filesize;

	return (ssize_t)resident_size;
}

static size_t blake3_mmap_file_chunksize(int fd, dev_t dev, void *mem,
					 size_t filesize,
					 size_t mmap_min_chunk, size_t mmap_max_chunk)
{
	size_t chunksize;
	ssize_t cached_size;
	int rotational;

	if (filesize <= mmap_min_chunk)
		return filesize;

	/* by default it's the filesize */
	chunksize = filesize;

	/* starting, check if the rotational attribute exists in this dev */
	rotational = linux_block_dev_is_rotational(dev);
	if (rotational == -1) {
		/* attribute not found? check the non-partition */
		rotational = linux_block_dev_is_rotational(makedev(major(dev), 0));
	}

	if (rotational == 1) {
		/* OK, it's rotational, but is it in cache?
		 * to avoid checking for the cached status of the whole file
		 * we just probe the MIN_CHUNKSIZE
		 * We will thrash in the case where the file is only cached
		 * for the first few bytes, but this is generally unusual.
		 */
		cached_size = linux_file_cached_size(fd, mem, mmap_min_chunk);

		if (cached_size <= 0)
			chunksize = mmap_min_chunk;
	}

	if (chunksize > mmap_max_chunk)
		chunksize = mmap_max_chunk;

	return chunksize;
}

#else

static size_t blake3_mmap_file_chunksize(int fd, dev_t dev, void *mem,
					 size_t filesize,
					 size_t mmap_min_chunk, size_t mmap_max_chunk)
{
	if (filesize <= mmap_min_chunk)
		return filesize;

	if (filesize > mmap_max_chunk)
		return mmap_max_chunk;

	/* for all others, just use mmap at max */
	return filesize;
}

#endif

int blake3_hash_file(blake3_hasher *hasher, const char *filename,
		     uint8_t output[BLAKE3_OUT_LEN])
{
	blake3_host_state *hs;
	FILE *fp = NULL;
	void *mem = NULL, *buf = NULL, *p;
	int fd = -1, ret = -1;
	size_t rdn, filesize, bufsz = 0, max_chunk, left, chunk;
	struct stat sb;
	dev_t dev = 0;
	int rc;

	if (!hasher || !filename || !output)
		return -1;

	hs = hasher->hs;

	if (hs->cfg.debug)
		fprintf(stderr, "processing file %s\n", filename);

	// reset the hasher (do not initialize again)
	blake3_hasher_reset(hasher);

	if (!strcmp(filename, "-")) {
		fp = stdin;
	} else {
		fp = NULL;

		fd = open(filename, O_RDONLY);
		if (fd < 0) {
			if (hs->cfg.debug)
				fprintf(stderr, "unable to open %s - %s\n",
					filename, strerror(errno));
			goto err_out;
		}

		rc = fstat(fd, &sb);
		if (rc < 0) {
			if (hs->cfg.debug)
				fprintf(stderr, "failed to stat %s - %s\n",
					filename, strerror(errno));
			goto err_out;
		}

		if (!S_ISREG(sb.st_mode)) {
			if (S_ISDIR(sb.st_mode))
				errno = EISDIR;
			else
				errno = EINVAL;
			if (hs->cfg.debug)
				fprintf(stderr, "not a regular file %s - %s\n",
					filename, strerror(errno));
			goto err_out;
		}

		filesize = (size_t)-1;

		/* try to mmap */
		if (sb.st_size > 0 && !hs->cfg.no_mmap) {
			filesize = sb.st_size;
			mem = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
			if (mem != MAP_FAILED) {
				close(fd);
				fd = -1;
				dev = sb.st_dev;
			} else
				mem = NULL;
		}

		/* unable to map? fallback to stream mode */
		if (!mem) {
			fp = fdopen(fd, "r");
			if (!fp) {
				if (hs->cfg.debug)
					fprintf(stderr, "unable to fdopen %s - %s\n",
						filename, strerror(errno));
				goto err_out;
			}
		}
	}

	/* mmap case, very simple */
	if (mem) {

		/* if the file is small enough don't bother with the rest */
		max_chunk = blake3_mmap_file_chunksize(fd, dev, mem, filesize,
						       hs->mmap_min_chunk, hs->mmap_max_chunk);

		p = mem;
		left = filesize;
		while (left > 0) {
			chunk = left > max_chunk ? max_chunk : left;
			blake3_hasher_update(hasher, p, chunk);
			p = (char *)p + chunk;
			left -= chunk;
		}
	} else {
		/* slow path using file reads */

		assert(fp);

		bufsz = hs->file_io_bufsz;

		/* for less than 8MB alloca */
		if (bufsz <= BLAKE3_ALLOCA_BUFFER_SIZE) {
			buf = alloca(bufsz);
		} else {
			buf = malloc(bufsz);
			if (!buf) {
				if (hs->cfg.debug)
					fprintf(stderr, "Unable to allocate buffer of %zu bytes\n",
						bufsz);
				goto err_out;
			}
		}

		do {
			rdn = fread(buf, 1, bufsz, fp);
			if (rdn == 0)
				break;
			if (rdn > 0)
				blake3_hasher_update(hasher, buf, rdn);
		} while (rdn >= bufsz);
	}

	// Finalize the hash. BLAKE3_OUT_LEN is the default output length, 32 bytes.
	blake3_hasher_finalize(hasher, output, BLAKE3_OUT_LEN);

	ret = 0;

out:
	if (mem)
		munmap(mem, filesize);

	if (fp && fp != stdin)
		fclose(fp);

	if (fd >= 0)
		close(fd);

	if (buf && (bufsz > BLAKE3_ALLOCA_BUFFER_SIZE))
		free(buf);

	return ret;

err_out:
	ret = -1;
	goto out;
}

void blake3_hash(struct blake3_hasher *hasher,
		 const void *mem, size_t size, uint8_t output[BLAKE3_OUT_LEN])
{
	blake3_hasher_reset(hasher);
	blake3_hasher_update(hasher, mem, size);
	blake3_hasher_finalize(hasher, output, BLAKE3_OUT_LEN);
}

const char *blake3_version(void)
{
	return BLAKE3_VERSION_STRING;
}

void blake3_hasher_init(struct blake3_host_state *hs, struct blake3_hasher *self)
{
	hs->hasher_ops->hasher_init(hs, self);
}

void blake3_hasher_init_keyed(struct blake3_host_state *hs, struct blake3_hasher *self, const uint8_t key[BLAKE3_KEY_LEN])
{
	hs->hasher_ops->hasher_init_keyed(hs, self, key);
}

void blake3_hasher_init_derive_key(struct blake3_host_state *hs, struct blake3_hasher *self, const char *context)
{
	hs->hasher_ops->hasher_init_derive_key(hs, self, context);
}

void blake3_hasher_init_derive_key_raw(struct blake3_host_state *hs, struct blake3_hasher *self, const void *context, size_t context_len)
{
	hs->hasher_ops->hasher_init_derive_key_raw(hs, self, context, context_len);
}

void blake3_hasher_update(struct blake3_hasher *self, const void *input, size_t input_len)
{
	blake3_host_state *hs = self->hs;
	hs->hasher_ops->hasher_update(self, input, input_len);
}

void blake3_hasher_finalize(const struct blake3_hasher *self, uint8_t *out, size_t out_len)
{
	blake3_host_state *hs = self->hs;
	hs->hasher_ops->hasher_finalize(self, out, out_len);
}

void blake3_hasher_finalize_seek(const struct blake3_hasher *self, uint64_t seek, uint8_t *out, size_t out_len)
{
	blake3_host_state *hs = self->hs;
	hs->hasher_ops->hasher_finalize_seek(self, seek, out, out_len);
}

void blake3_hasher_reset(struct blake3_hasher *self)
{
	blake3_host_state *hs = self->hs;
	hs->hasher_ops->hasher_reset(self);
}
