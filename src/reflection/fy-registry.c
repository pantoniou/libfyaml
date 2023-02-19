/*
 * fy-registry - Backend registy methods
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <alloca.h>

#include "fy-utf8.h"
#include "fy-blob.h"

#include "fy-reflection-private.h"

#include "fy-packed-backend.h"
#if defined(HAVE_LIBCLANG) && HAVE_LIBCLANG
#include "fy-clang-backend.h"
#endif

extern const struct fy_reflection_backend fy_reflection_packed_backend;
#if defined(HAVE_LIBCLANG) && HAVE_LIBCLANG
extern const struct fy_reflection_backend fy_reflection_clang_backend;
#endif

static const struct fy_reflection_backend *builtin_backends[] = {
	/* the packed backend is always available */
	&fy_reflection_packed_backend,
#if defined(HAVE_LIBCLANG) && HAVE_LIBCLANG
	/* the clang backend is optional */
	&fy_reflection_clang_backend,
#endif
};

const struct fy_reflection_backend *
fy_reflection_backend_lookup(const char *name)
{
	const struct fy_reflection_backend *be;
	unsigned int i;

	for (i = 0; i < sizeof(builtin_backends)/sizeof(builtin_backends[0]); i++) {
		be = builtin_backends[i];
		if (be && be->name && !strcmp(name, be->name))
			return be;
	}
	return NULL;
}

#if defined(HAVE_LIBCLANG) && HAVE_LIBCLANG
struct fy_reflection *
fy_reflection_from_c_files(int filec, const char * const filev[], int argc, const char * const argv[],
			   bool display_diagnostics, bool include_comments)
{
	struct fy_clang_backend_reflection_cfg rcfg;
	struct fy_clang_backend_import_cfg *icfgs;
	const void **icfgps;
	int i, num_imports;

	memset(&rcfg, 0, sizeof(rcfg));
	rcfg.display_diagnostics = display_diagnostics;

	num_imports = filec;

	icfgs = alloca(sizeof(*icfgs) * num_imports);
	icfgps = alloca(sizeof(*icfgps) * num_imports);
	memset(icfgs, 0, sizeof(*icfgs) * num_imports);
	for (i = 0; i < num_imports; i++) {
		icfgps[i] = &icfgs[i];
		icfgs[i].file = filev[i];
		icfgs[i].argc = argc;
		icfgs[i].argv = argv;
	}

	return fy_reflection_from_imports("clang", &rcfg, num_imports, icfgps);
}

struct fy_reflection *
fy_reflection_from_c_file(const char *file, int argc, const char * const argv[],
			  bool display_diagnostics, bool include_comments)
{
	return fy_reflection_from_c_files(1, &file, argc, argv,
			display_diagnostics, include_comments);
}

struct fy_reflection *
fy_reflection_from_c_file_with_cflags(const char *file, const char *cflags,
		bool display_diagnostics, bool include_comments)
{
	void *mem;
	int argc;
	const char * const *argv;
	struct fy_reflection *rfl;

	mem = fy_utf8_split_posix(cflags, &argc, &argv);
	if (!mem)
		return NULL;

	rfl = fy_reflection_from_c_file(file, argc, argv, display_diagnostics, include_comments);

	free(mem);

	return rfl;
}

#else

struct fy_reflection *
fy_reflection_from_c_files(int filec, const char * const filev[], int argc, const char * const argv[],
			   bool display_diagnostics, bool include_comments)
{
	return NULL;
}


struct fy_reflection *
fy_reflection_from_c_file(const char *file, int argc, const char * const argv[],
			  bool display_diagnostics, bool include_comments)
{
	return NULL;
}

struct fy_reflection *
fy_reflection_from_c_file_with_cflags(const char *file, const char *cflags,
		bool display_diagnostics, bool include_comments)
{
	return NULL;
}

#endif

struct fy_reflection *
fy_reflection_from_packed_blob(const void *blob, size_t blob_size)
{
	struct fy_packed_backend_reflection_cfg packed_cfg;

	memset(&packed_cfg, 0, sizeof(packed_cfg));
	packed_cfg.type = FYPRT_BLOB;
	packed_cfg.blob = blob;
	packed_cfg.blob_size = blob_size;
	packed_cfg.copy = true;	/* always copy */

	return fy_reflection_from_import("packed", &packed_cfg, NULL);
}

void *
fy_reflection_to_packed_blob(struct fy_reflection *rfl, size_t *blob_sizep,
			     bool include_comments, bool include_location)
{
	struct fy_packed_generator pg_local, *pg = &pg_local;
	void *blob = NULL;
	size_t blob_size = 0;
	int rc;

	memset(pg, 0, sizeof(*pg));

	pg->rfl = rfl;
	pg->type = FYPGT_BLOB;
	pg->blobp = &blob;
	pg->blob_sizep = &blob_size;

	rc = fy_packed_generate(pg);
	if (rc)
		return NULL;

	if (blob_sizep)
		*blob_sizep = blob_size;

	return blob;
}

struct fy_reflection *fy_reflection_from_packed_blob_file(const char *blob_file)
{
	struct fy_reflection *rfl;
	size_t blob_size;
	void *blob;

	if (!blob_file)
		return NULL;

	blob = fy_blob_read(blob_file, &blob_size);
	if (!blob)
		goto err_out;

	rfl = fy_reflection_from_packed_blob(blob, blob_size);
	if (!rfl)
		goto err_out;

	free(blob);
	return rfl;

err_out:
	if (blob)
		free(blob);
	return NULL;
}

int fy_reflection_to_packed_blob_file(struct fy_reflection *rfl, const char *blob_file)
{
	int rc;
	void *blob;
	size_t blob_size;

	if (!rfl || !blob_file)
		return -1;

	blob = fy_reflection_to_packed_blob(rfl, &blob_size, true, true);
	if (!blob)
		goto err_out;

	rc = fy_blob_write(blob_file, blob, blob_size);
	if (rc < 0)
		goto err_out;

	free(blob);

	return 0;

err_out:
	if (blob)
		free(blob);
	return -1;
}
