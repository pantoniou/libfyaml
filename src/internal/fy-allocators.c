/*
 * fy-allocators.c - allocators testing internal utility
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <alloca.h>
#include <stdbool.h>
#include <getopt.h>
#include <ctype.h>
#include <assert.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdalign.h>

#include <libfyaml.h>

#include "fy-utils.h"

#include "fy-allocator.h"
#include "fy-allocator-linear.h"
#include "fy-allocator-malloc.h"
#include "fy-allocator-mremap.h"
#include "fy-allocator-dedup.h"
#include "fy-allocator-auto.h"

static void dump_allocator_info(struct fy_allocator *a, fy_alloc_tag tag)
{
	struct fy_allocator_info *info;
	struct fy_allocator_tag_info *tag_info;
	struct fy_allocator_arena_info *arena_info;
	unsigned int i, j;

	info = fy_allocator_get_info(a, tag);
	if (!info) {
		fprintf(stderr, "fy_allocator_get_info() failed\n");
		return;
	}

	fprintf(stderr, "Allocator %p: free=%zu used=%zu total=%zu\n", a,
			info->free, info->used, info->total);
	for (i = 0; i < info->num_tag_infos; i++) {
		tag_info = &info->tag_infos[i];

		fprintf(stderr, "\ttag #%d: free=%zu used=%zu total=%zu\n", i,
				tag_info->free, tag_info->used, tag_info->total);
		for (j = 0; j < tag_info->num_arena_infos; j++) {
			arena_info = &tag_info->arena_infos[j];

			fprintf(stderr, "\t\tarena #%d: free=%zu used=%zu total=%zu data=%p-0x%zx\n", j,
					arena_info->free, arena_info->used, arena_info->total,
					arena_info->data, arena_info->size);
		}
	}

	free(info);
}

static int allocator_test(const char *allocator, size_t size)
{
	struct fy_linear_setup_data lsetupdata;
	const void *gsetupdata = NULL;
	struct fy_allocator *a;
	fy_alloc_tag tag0;
	char *names;
	bool is_linear;
	unsigned int *uintp[16];
	unsigned int i;

	names = fy_allocator_get_names();
	assert(names);
	fprintf(stderr, "Available allocators: %s\n", names);
	free(names);

	is_linear = !strcmp(allocator, "linear");

	if (is_linear) {
		memset(&lsetupdata, 0, sizeof(lsetupdata));
		lsetupdata.size = size ? size : 4096;
		gsetupdata = &lsetupdata;
	} else
		gsetupdata = NULL;

	fprintf(stderr, "Using allocator: %s\n", allocator);

	a = fy_allocator_create(allocator, gsetupdata);
	assert(a);

	fprintf(stderr, "Allocator created: %p\n", a);

	tag0 = fy_allocator_get_tag(a, NULL);
	assert(tag0 != FY_ALLOC_TAG_ERROR);

	fprintf(stderr, "tag0 created: %d\n", tag0);

	fprintf(stderr, "Allocating %u integers\n", (unsigned int)ARRAY_SIZE(uintp));
	for (i = 0; i < ARRAY_SIZE(uintp); i++) {
		uintp[i] = fy_allocator_alloc(a, tag0, sizeof(*uintp[0]), alignof(*uintp[0]));
		assert(uintp[i] != NULL);
		fprintf(stderr, "\t%u: %p\n", i, uintp[i]);
	}

	/* fill in */
	for (i = 0; i < ARRAY_SIZE(uintp); i++)
		*uintp[i] = i;

	/* assert */
	for (i = 0; i < ARRAY_SIZE(uintp); i++)
		assert(*uintp[i] == i);

	fprintf(stderr, "Dumping allocator areas before release\n");
	dump_allocator_info(a, tag0);

	fprintf(stderr, "Releasing tag0\n"); 
	fy_allocator_release_tag(a, tag0);

	fprintf(stderr, "Dumping allocator areas after release\n");
	dump_allocator_info(a, FY_ALLOC_TAG_NONE);

	fy_allocator_destroy(a);

	if (is_linear)
		free(lsetupdata.buf);

	return 0;
}

static struct option lopts[] = {
	{"allocator",		required_argument,	0,	'a' },
	{"size",		required_argument,	0,	's' },
	{"help",		no_argument,		0,	'h' },
	{0,			0,              	0,	 0  },
};

static void display_usage(FILE *fp, const char *progname)
{
	char *names;
	const char *s;

	names = fy_allocator_get_names();
	assert(names);

	s = strrchr(progname, '/');
	if (s != NULL)
		progname = s + 1;

	fprintf(fp, "Usage:\n\t%s [options]\n", progname);
	fprintf(fp, "\noptions:\n");
	fprintf(fp, "\t--allocator <n>, -a <n>   : Use allocator, one of: %s\n", names); 
	fprintf(fp, "\t--size <n>, -s <n>        : Size for allocators that require one\n");
	fprintf(fp, "\t--help, -h                : Display help message\n");
	fprintf(fp, "\n");

	free(names);
}

int main(int argc, char *argv[])
{
	int opt, lidx, rc;
	int exitcode = EXIT_FAILURE;
	const char *allocator = "linear";
	size_t size = 0;

	while ((opt = getopt_long_only(argc, argv, "a:s:h", lopts, &lidx)) != -1) {
		switch (opt) {
		case 'a':
			if (!fy_allocator_is_available(optarg)) {
				fprintf(stderr, "Error: illegal allocator name \"%s\"\n", optarg);
				goto err_out_usage;
			}
			allocator = optarg;
			break;
		case 's':
			size = (size_t)atoi(optarg);
			break;
		case 'h' :
			display_usage(stdout, argv[0]);
			goto ok_out;
		default:
			goto err_out_usage;
		}
	}

	rc = allocator_test(allocator, size);
	if (rc) {
		fprintf(stderr, "Error: allocator_test() failed\n");
		goto err_out;
	}

ok_out:
	exitcode = EXIT_SUCCESS;

out:
	return exitcode;

err_out_usage:
	display_usage(stderr, argv[0]);
err_out:
	exitcode = EXIT_FAILURE;
	goto out;
}
