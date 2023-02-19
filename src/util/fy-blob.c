/*
 * fy-blob.c - binary blob handling
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

#include "fy-blob.h"

/* define optimized methods for probing */
BR_DEFINE_WX(br_probe_w,  8, false, false);
BR_DEFINE_WX(br_probe_w, 16, false, false);
BR_DEFINE_WX(br_probe_w, 32, false, false);
BR_DEFINE_WX(br_probe_w, 64, false, false);

/* define optimized methods for writing */
BR_DEFINE_WX(br_native_w,  8, false, true);
BR_DEFINE_WX(br_native_w, 16, false, true);
BR_DEFINE_WX(br_native_w, 32, false, true);
BR_DEFINE_WX(br_native_w, 64, false, true);

BR_DEFINE_WX(br_bswap_w,  8, true, true);
BR_DEFINE_WX(br_bswap_w, 16, true, true);
BR_DEFINE_WX(br_bswap_w, 32, true, true);
BR_DEFINE_WX(br_bswap_w, 64, true, true);

void *fy_blob_read(const char *file, size_t *sizep)
{
	struct stat sb;
	void *blob = NULL;
	uint8_t *p;
	size_t size, left;
	ssize_t rdn;
	int fd = -1, rc;

	if (!file || !sizep)
		goto err_out;

	fd = open(file, O_RDONLY);
	if (fd < 0)
		goto err_out;

	rc = fstat(fd, &sb);
	if (rc < 0)
		goto err_out;

	size = sb.st_size;
	blob = malloc(size);
	if (!blob)
		goto err_out;

	p = blob;
	left = size;
	while (left > 0) {
		do {
			rdn = read(fd, p, left);
		} while (rdn == -1 && errno == EAGAIN);
		if (rdn < 0)
			goto err_out;
		p += rdn;
		left -= rdn;
	}
	close(fd);

	*sizep = size;
	return blob;

err_out:
	if (blob)
		free(blob);
	if (fd >= 0)
		close(fd);
	return NULL;
}

int fy_blob_write(const char *file, const void *blob, size_t size)
{
	int fd = -1;
	const uint8_t *p;
	size_t left;
	ssize_t wrn;

	if (!file || !blob)
		return -1;

	fd = open(file, O_WRONLY|O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IRGRP | S_IROTH);
	if (fd < 0)
		goto err_out;

	p = blob;
	left = size;
	while (left > 0) {
		do {
			wrn = write(fd, p, left);
		} while (wrn == -1 && errno == EAGAIN);
		if (wrn < 0)
			goto err_out;
		p += wrn;
		left -= wrn;
	}
	close(fd);

	return 0;

err_out:
	if (fd >= 0)
		close(fd);
	return -1;
}
