/*
 * fy-generics.c - generics testing internal utility
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

#include "fy-generic.h"
#include "fy-generic-decoder.h"
#include "fy-generic-encoder.h"

#include "fy-allocator.h"
#include "fy-allocator-linear.h"
#include "fy-allocator-malloc.h"
#include "fy-allocator-mremap.h"
#include "fy-allocator-dedup.h"
#include "fy-allocator-auto.h"

#if 1
union ptr {
	void *p;
	uintptr_t i;
};

struct fy_arena_reloc {
	union ptr src;
	union ptr srce;
	union ptr dst;
	size_t size;
};

static int arena_reloc_src_compare(const void *va, const void *vb)
{
	const struct fy_arena_reloc *a = va, *b = vb;

	return a->src.i < b->src.i ? -1 :
               a->src.i > b->src.i ?  1 :
	       0;
}

/*
 * [ 4-6, 8-10, 11-12, 16-20, 35-45 ] #5
 *
 * find  2 -> [ 4-6, 8-10, 11-12, 16-20, 35-45 ] #5, 2 < [2]=11, count #2
 *       2 -> [ 4-6, 8-10 ]                      #2, 2 < [1]= 8, count #1
 *       2 -> [ 4-6 ]                            #1, 2 < [0]= 4, count #0
 * NULL
 *
 * find 50 -> [ 4-6, 8-10, 11-12, 16-20, 35-45 ] #5, 50 > [2]=11, count #2, adv 3
 *      50 -> [ 16-20, 35-45 ]                   #2, 50 > [1]=35, count #0
 * NULL
 *
 * find 18 -> [ 4-6, 8-10, 11-12, 16-20, 35-45 ] #5, 18 > [2]=11, count #2, adv 3
 *      18 -> [ 16-20, 35-45 ]                   #2, 18 < [1]=35, count #1
 *      18 -> [ 16-20 ]                          #1, 18 >= 16 && 18 < 20
 * [16-20]
 *
 * find 22 -> [ 4-6, 8-10, 11-12, 16-20, 35-45 ] #5, 22 > [2]=11, count #2, adv 3
 *      22 -> [ 16-20, 35-45 ]                   #2, 22 < [1]=35, count #1
 *      22 -> [ 16-20 ]                          #1, 22 > 16,     count #0
 * [16-20]
 *
 *
 */

static inline const struct fy_arena_reloc *
fy_arena_locate_by_src(const struct fy_arena_reloc *arenas, unsigned int count, const void *ptr)
{
	uintptr_t p = (uintptr_t)ptr;
	unsigned int mid;

	while (count > 0) {
		mid = count / 2;
		if (p >= arenas[mid].src.i) {
			/* check for direct hit */
			if (p <= arenas[mid].srce.i)
				return arenas;
			/* nope, skip over this too */
			arenas += mid + 1;
			count -= (mid + 1);
		} else
			count = mid;
	}

	return NULL;
}

struct fy_relocation_info {
	void *start;	/* inclusive */
	void *end;	/* not inclusive */
	const struct fy_arena_reloc *arenas;
	unsigned int num_arenas;
};

static inline void *fy_generic_arena_resolve_ptr(struct fy_relocation_info *ri, fy_generic v)
{
	void *p = fy_generic_resolve_ptr(v);
	return (p >= ri->start && p < ri->end) ? NULL : p;
}

static inline fy_generic fy_generic_arena_relocate_ptr(struct fy_relocation_info *ri, fy_generic v)
{
	void *p = fy_generic_resolve_collection_ptr(v);
	ptrdiff_t d = 0;
	const struct fy_arena_reloc *arena;

	(void)arena;
	arena = fy_arena_locate_by_src(ri->arenas, ri->num_arenas, p);
	assert(arena);

	v = (fy_generic)((ptrdiff_t)((uintptr_t)v & ~(uintptr_t)FY_INPLACE_TYPE_MASK) + d);
	assert((v & (uintptr_t)FY_INPLACE_TYPE_MASK) == 0);
	return v;
}

static inline fy_generic fy_generic_arena_relocate_collection_ptr(struct fy_relocation_info *ri, fy_generic v)
{
	void *p = fy_generic_resolve_collection_ptr(v);
	ptrdiff_t d = 0;
	const struct fy_arena_reloc *arena;

	arena = fy_arena_locate_by_src(ri->arenas, ri->num_arenas, p);
	assert(arena);

	(void)arena;

	v = (fy_generic)((ptrdiff_t)((uintptr_t)v & ~(uintptr_t)FY_COLLECTION_MASK) + d);
	assert((v & (uintptr_t)FY_COLLECTION_MASK) == 0);
	return v;
}

#if 0
static fy_generic fy_generic_arena_relocate(struct fy_relocation_info *ri, fy_generic v)
{
	void *p;
	struct fy_generic_indirect *gi;
	struct fy_generic_sequence *seq;
	struct fy_generic_mapping *map;
	fy_generic *items;
	fy_generic *pairs;
	size_t i, count;

	/* if it's indirect, resolve the internals */
	if (fy_generic_is_indirect(v)) {

		/* check if already relocated */
		p = fy_generic_arena_resolve_ptr(ri, v);
		if (p) {
			fprintf(stderr, "%s: indirect p=%p v=0x%08lx\n", __func__, p, v);
			v = fy_generic_arena_relocate_ptr(ri, v) | FY_INDIRECT_V;

			gi = fy_generic_resolve_ptr(v);
			gi->value = fy_generic_arena_relocate(ri, gi->value);
			gi->anchor = fy_generic_arena_relocate(ri, gi->anchor);
			gi->tag = fy_generic_arena_relocate(ri, gi->tag);
		} else {
			fprintf(stderr, "%s: indirect already relocated v=0x%08lx\n", __func__, v);
		}
		return v;
	}

	/* if it's not indirect, it might be one of the in place formats */
	switch (fy_generic_get_type(v)) {
	case FYGT_NULL:
	case FYGT_BOOL:
		return v;

	case FYGT_INT:
		if ((v & FY_INPLACE_TYPE_MASK) == FY_INT_INPLACE_V)
			return v;

		p = fy_generic_arena_resolve_ptr(ri, v);
		if (p)
			v = fy_generic_arena_relocate_ptr(ri, v) | FY_INT_OUTPLACE_V;
		break;

	case FYGT_FLOAT:
		if ((v & FY_INPLACE_TYPE_MASK) == FY_FLOAT_INPLACE_V)
			return v;

		p = fy_generic_arena_resolve_ptr(ri, v);
		if (p)
			v = fy_generic_arena_relocate_ptr(ri, v) | FY_FLOAT_OUTPLACE_V;
		break;

	case FYGT_STRING:
		if ((v & FY_INPLACE_TYPE_MASK) == FY_STRING_INPLACE_V)
			return v;

		p = fy_generic_arena_resolve_ptr(ri, v);
		if (p)
			v = fy_generic_arena_relocate_ptr(ri, v) | FY_STRING_OUTPLACE_V;
		break;

	case FYGT_SEQUENCE:
		p = fy_generic_arena_resolve_ptr(ri, v);
		if (p) {
			v = fy_generic_arena_relocate_collection_ptr(ri, v) | FY_SEQ_V;
			seq = fy_generic_resolve_collection_ptr(v);
			count = seq->count;
			items = seq->items;
			for (i = 0; i < count; i++)
				items[i] = fy_generic_arena_relocate(ri, items[i]);
		}
		break;

	case FYGT_MAPPING:
		p = fy_generic_arena_resolve_ptr(ri, v);
		if (p) {
			v = fy_generic_arena_relocate_collection_ptr(ri, v) | FY_MAP_V;
			map = fy_generic_resolve_collection_ptr(v);
			count = map->count * 2;
			pairs = map->pairs;
			for (i = 0; i < count; i++)
				pairs[i] = fy_generic_arena_relocate(ri, pairs[i]);
		}
		break;

	default:
		abort();
		break;
	}

	return v;
}
#endif

#if 0
static void fy_generic_print_primitive(FILE *fp, int level, fy_generic v)
{
	const char *sv;
	fy_generic vtag, vanchor;
	const char *tag = NULL, *anchor = NULL;
	fy_generic iv;
	fy_generic key, value;
	const fy_generic *items;
	size_t i, count, slen;

	vanchor = fy_generic_get_anchor(v);
	if (fy_generic_get_type(vanchor) == FYGT_STRING)
		anchor = fy_generic_get_string(vanchor);
	vtag = fy_generic_get_tag(v);
	if (fy_generic_get_type(vtag) == FYGT_STRING)
		tag = fy_generic_get_string(vtag);

	if (anchor)
		fprintf(fp, "&%s ", anchor);
	if (tag)
		fprintf(fp, "%s ", tag);

	if (v == fy_invalid)
		fprintf(fp, "invalid");

	switch (fy_generic_get_type(v)) {
	case FYGT_NULL:
		fprintf(fp, "%s", "null");
		return;

	case FYGT_BOOL:
		fprintf(fp, "%s", fy_generic_get_bool(v) ? "true" : "false");
		return;

	case FYGT_INT:
		fprintf(fp, "%lld", fy_generic_get_int(v));
		return;

	case FYGT_FLOAT:
		fprintf(fp, "%f", fy_generic_get_float(v));
		return;

	case FYGT_STRING:
		sv = fy_generic_get_string_size(v, &slen);
		fprintf(fp, "\"%s\"", fy_utf8_format_text_a(sv, slen, fyue_doublequote));
		return;

	case FYGT_SEQUENCE:
		items = fy_generic_sequence_get_items(v, &count);
		fprintf(fp, "[");
		for (i = 0; i < count; i++) {
			iv = items[i];
			fy_generic_print_primitive(fp, level + 1, iv);
			if (i + 1 < count)
				fprintf(fp, ", ");
		}
		fprintf(fp, "]");
		break;

	case FYGT_MAPPING:
		items = fy_generic_mapping_get_pairs(v, &count);
		fprintf(fp, "{");
		for (i = 0; i < count; i++) {
			key = items[i * 2];
			value = items[i * 2 + 1];
			fy_generic_print_primitive(fp, level + 1, key);
			fprintf(fp, ": ");
			fy_generic_print_primitive(fp, level + 1, value);
			if (i + 1 < count)
				fprintf(fp, ", ");
		}
		fprintf(fp, "}");
		break;

	case FYGT_ALIAS:
		sv = fy_generic_get_alias_size_alloca(v, &slen);
		fprintf(fp, "*'%.*s'", (int)slen, sv);
		break;

	default:
		assert(0);
		abort();
	}
}
#endif

static void fy_generic_dump_primitive(FILE *fp, int level, fy_generic vv)
{
	static const char generic_type_map[] = {
		[FYGT_INVALID] 	= '!',
		[FYGT_NULL]	= 'n',
		[FYGT_BOOL]	= 'b',
		[FYGT_INT]	= 'i',
		[FYGT_FLOAT]	= 'f',
		[FYGT_STRING]	= '"',
		[FYGT_SEQUENCE]	= '[',
		[FYGT_MAPPING]	= '{',
		[FYGT_INDIRECT]	= '^',
		[FYGT_ALIAS]	= '*',
	};
	const char *sv;
	fy_generic vtag, vanchor, v, iv, key, value;
	const char *tag = NULL, *anchor = NULL;
	const fy_generic *items;
	size_t i, count, slen;

	vanchor = fy_generic_get_anchor(vv);
	if (fy_generic_get_type(vanchor) == FYGT_STRING)
		anchor = fy_generic_get_string(vanchor);
	vtag = fy_generic_get_tag(vv);
	if (fy_generic_get_type(vtag) == FYGT_STRING)
		tag = fy_generic_get_string(vtag);
	v = fy_generic_is_indirect(vv) ? fy_generic_indirect_get_value(vv) : vv;

	fprintf(fp, "%*s", level * 2, "");

	if (v != vv)
		fprintf(fp, "(%016lx) ", (unsigned long)vv);
	if (anchor)
		fprintf(fp, "&%s ", anchor);
	if (tag)
		fprintf(fp, "%s ", tag);

	fprintf(fp, "%016lx ", (unsigned long)v);
	fprintf(fp, "%c ", generic_type_map[fy_generic_get_type(v)]);


	if (v == fy_invalid)
		fprintf(fp, "invalid");

	switch (fy_generic_get_type(v)) {
	case FYGT_NULL:
		fprintf(fp, "%s", "null");
		return;

	case FYGT_BOOL:
		fprintf(fp, "%s", fy_generic_get_bool(v) ? "true" : "false");
		return;

	case FYGT_INT:
		fprintf(fp, "%lld", fy_generic_get_int(v));
		return;

	case FYGT_FLOAT:
		fprintf(fp, "%f", fy_generic_get_float(v));
		return;

	case FYGT_STRING:
		sv = fy_generic_get_string_size(v, &slen);
		fprintf(fp, "%s", fy_utf8_format_text_a(sv, slen, fyue_doublequote));
		return;

	case FYGT_SEQUENCE:
		items = fy_generic_sequence_get_items(v, &count);
		for (i = 0; i < count; i++) {
			iv = items[i];
			fy_generic_dump_primitive(fp, level + 1, iv);
		}
		break;

	case FYGT_MAPPING:
		items = fy_generic_mapping_get_pairs(v, &count);
		for (i = 0; i < count; i++) {
			key = items[i * 2];
			value = items[i * 2 + 1];
			fy_generic_dump_primitive(fp, level + 1, key);
			fprintf(fp, ": ");
			fy_generic_dump_primitive(fp, level + 1, value);
		}
		break;

	case FYGT_ALIAS:
		sv = fy_generic_get_alias_size(v, &slen);
		fprintf(fp, "%.*s", (int)slen, sv);
		break;

	default:
		assert(0);
		abort();
	}
	fprintf(fp, "\n");
}

const void *fy_generic_builder_linearize(struct fy_generic_builder *gb, fy_generic *vp, size_t *sizep)
{
	struct fy_allocator_info *info = NULL;
	struct fy_allocator_tag_info *tag_info;
	struct fy_allocator_arena_info *arena_info;
	const void *linear_data = NULL;
	size_t size, linear_size = 0, offset;
	fy_generic v, linear_v = fy_invalid;
	unsigned int i, j, num_arenas;
	struct fy_arena_reloc *arenas = NULL, *arena;
	struct fy_relocation_info ri;

	if (!gb || !vp || !sizep)
		return NULL;

	v = *vp;
	*vp = fy_invalid;
	*sizep = 0;

	info = fy_allocator_get_info(gb->allocator, gb->alloc_tag);
	if (!info)
		return NULL;

	/* this is great, everything is linear */
	if (info->num_tag_infos == 1 && info->tag_infos[0].num_arena_infos == 1) {
		arena_info = &info->tag_infos[0].arena_infos[0];
		linear_v = v;
		linear_data = arena_info->data;
		linear_size = arena_info->size;
		goto out;
	}

	/* need to stitch things together */

	/* count the arenas and the linear size */
	num_arenas = 0;
	size = 0;
	for (i = 0; i < info->num_tag_infos; i++) {
		tag_info = &info->tag_infos[i];

		for (j = 0; j < tag_info->num_arena_infos; j++) {
			arena_info = &tag_info->arena_infos[j];

			/* must be aligned */
			assert(((uintptr_t)arena_info->data & 15) == 0);

			size = fy_size_t_align(size, 16);	/* align at 16 always */
			size += arena_info->size;
		}
		num_arenas += tag_info->num_arena_infos;
	}

	/* allocate space for the arenas */
	arenas = malloc(sizeof(*arenas) * num_arenas);
	if (!arenas)
		goto out;

	/* nuke it to be sure */
	if (gb->linear) {
		free(gb->linear);
		gb->linear = NULL;
	}
	
	gb->linear = malloc(size);
	if (!gb->linear)
		goto out;

	/* fill in the arenas */
	arena = arenas;
	offset = 0;
	for (i = 0; i < info->num_tag_infos; i++) {
		tag_info = &info->tag_infos[i];

		for (j = 0; j < tag_info->num_arena_infos; j++) {
			arena_info = &tag_info->arena_infos[j];

			/* save the arena */
			arena->src.p = arena_info->data;
			arena->size = arena_info->size;
			arena->srce.p = arena->src.p + arena->size - 1;
			arena->dst.p = gb->linear + offset;

			fprintf(stderr, "offset=0x%zx 0x%zx 0x%zx\n",
				offset, offset + arena_info->size, size);
			assert(offset + arena_info->size <= size);
			/* copy it over */
			memcpy(arena->dst.p, arena->src.p, arena->size);

			offset += arena_info->size;
			offset = fy_size_t_align(offset, 16);	/* align at 16 always */

			arena++;
		}
	}

	/* sort by src address */
	qsort(arenas, num_arenas, sizeof(*arenas), arena_reloc_src_compare);

	fprintf(stderr, "Arenas:");
	for (i = 0; i < num_arenas; i++)
		fprintf(stderr, " %p-%p", arenas[i].src.p, arenas[i].srce.p);
	fprintf(stderr, "\n");

	memset(&ri, 0, sizeof(ri));
	ri.start = gb->linear;
	ri.end = ri.start + size;
	ri.arenas = arenas;
	ri.num_arenas = num_arenas;

	fprintf(stderr, "Linear: %p-%p (0x%zx)\n", ri.start, ri.end, (size_t)(ri.end - ri.start));

	fprintf(stderr, "\nbefore\n");
	fy_generic_dump_primitive(stderr, 0,
			fy_generic_mapping_lookup(	/* the 1st document */
				fy_generic_sequence_get_item(v, 0),
				fy_string("root")));
	fprintf(stderr, "\n");

#if 0
	linear_v = fy_generic_arena_relocate(&ri, v);
#endif

	linear_v = fy_invalid;
	linear_size = 0;
	linear_data = NULL;

out:
	if (arenas)
		free(arenas);

	if (info)
		free(info);

	*vp = linear_v;
	*sizep = linear_size;
	return linear_data;
}

void fy_generic_builder_linearize_release(struct fy_generic_builder *gb, fy_generic v, const void *data, size_t size)
{
	/* nothing */
}

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
#endif

struct generic_options {
	const char *mode;
	const char *allocator;
	const char *parent_allocator;
	size_t size;
	bool resolve;
	bool null_output;
};

const struct generic_options default_generic_options = {
	.mode			= "parse-generic",
	.allocator		= "mremap",
	.parent_allocator	= NULL,
	.size			= 0,
	.resolve		= false,
	.null_output		= false,
};

static size_t estimate_max_file_size(int argc, char **argv)
{
	struct stat sb;
	size_t size;
	int i, rc;

	if (argc <= 0)
		return 0;	/* can't estimate without file (probably stdin) */

	size = 0;
	for (i = 0; i < argc; i++) {
		rc = stat(argv[i], &sb);
		if (rc)
			continue;

		/* only do it for regular files */
		if ((sb.st_mode & S_IFMT) != S_IFREG)
			continue;

		if ((size_t)sb.st_size > size)
			size = sb.st_size;
	}

	return size;
}

struct fy_allocator *create_allocator(const struct generic_options *opt, const char *name, const char *parent_name, size_t alloc_size, struct fy_allocator **parent_allocator)
{
	struct fy_allocator *allocator;

	if (parent_allocator)
		*parent_allocator = NULL;

	if (!strcmp(name, "linear")) {
		struct fy_linear_setup_data linear_sd;

		memset(&linear_sd, 0, sizeof(linear_sd));
		linear_sd.size = alloc_size;
		return fy_allocator_create("linear", &linear_sd);
	}

	if (!strcmp(name, "malloc"))
		return fy_allocator_create("malloc", NULL);

	if (!strcmp(name, "auto")) {
		struct fy_auto_setup_data auto_sd;

		memset(&auto_sd, 0, sizeof(auto_sd));
		auto_sd.scenario = FYAST_BALANCED;
		auto_sd.estimated_max_size = alloc_size ? alloc_size : (16 << 20);
		return fy_allocator_create("auto", &auto_sd);
	}

	if (!strcmp(name, "mremap")) {
		struct fy_mremap_setup_data mremap_sd;

		memset(&mremap_sd, 0, sizeof(mremap_sd));
		mremap_sd.big_alloc_threshold = SIZE_MAX;
		mremap_sd.empty_threshold = 64;
		mremap_sd.grow_ratio = 1.5;
		mremap_sd.balloon_ratio = 8.0;
		mremap_sd.arena_type = FYMRAT_MMAP;
		mremap_sd.minimum_arena_size = alloc_size ? alloc_size : (16 << 20);
		return fy_allocator_create("mremap", &mremap_sd);
	}

	if (!strcmp(name, "dedup")) {
		struct fy_dedup_setup_data dedup_sd;

		if (!parent_allocator)
			return NULL;

		memset(&dedup_sd, 0, sizeof(dedup_sd));
		dedup_sd.bloom_filter_bits = 0;	/* use default */
		dedup_sd.bucket_count_bits = 0;
		dedup_sd.estimated_content_size = alloc_size ? alloc_size : (16 << 20);
		dedup_sd.parent_allocator = create_allocator(opt, parent_name, NULL, alloc_size, NULL);
		if (dedup_sd.parent_allocator) {
			dedup_sd.estimated_content_size = alloc_size ? alloc_size : (16 << 20);
			allocator = fy_allocator_create("dedup", &dedup_sd);
			if (allocator) {
				*parent_allocator = dedup_sd.parent_allocator;
				return allocator;
			}
			fy_allocator_destroy(dedup_sd.parent_allocator);
		}
		return NULL;
	}

	return NULL;
}

static int do_parse_generic(const struct generic_options *opt, int argc, char **argv)
{
	struct fy_allocator *allocator = NULL, *parent_allocator = NULL;
	struct fy_generic_builder *gb = NULL;
	struct fy_generic_decoder *fygd = NULL;
	struct fy_generic_encoder *fyge = NULL;
	fy_generic vdir;
	struct fy_parse_cfg parse_cfg;
	struct fy_parser *fyp = NULL;
	struct fy_emitter_cfg emit_cfg;
	struct fy_emitter *fye = NULL;
	const char *filename;
	int i, rc, num_ok, num_inputs, ret = -1;
	size_t max_filesize, alloc_size;

	if (!opt->size) {
		max_filesize = estimate_max_file_size(argc, argv);

		if (max_filesize == 0)
			max_filesize = 1 << 20;	/* go with 1M */

		/* align to a page */
		alloc_size = fy_size_t_align(max_filesize, 4096);

		/* balloon by 4 (heuristic) */
		alloc_size *= 4;

	} else
		alloc_size = opt->size;

	allocator = create_allocator(opt, opt->allocator, opt->parent_allocator, alloc_size, &parent_allocator);
	if (!allocator) {
		fprintf(stderr, "create_allocator() failed\n");
		goto err_out;
	}

	gb = fy_generic_builder_create(allocator, FY_ALLOC_TAG_NONE);
	if (!gb) {
		fprintf(stderr, "fy_generic_builder_create() failed\n");
		goto err_out;
	}
	assert(gb);

	memset(&parse_cfg, 0, sizeof(parse_cfg));
	parse_cfg.flags = FYPCF_DEFAULT_PARSE |
			  (opt->resolve ? FYPCF_RESOLVE_DOCUMENT : 0);

	fyp = fy_parser_create(&parse_cfg);
	if (!fyp) {
		fprintf(stderr, "fy_parser_create() failed\n");
		goto err_out;
	}

	if (!opt->null_output) {
		memset(&emit_cfg, 0, sizeof(emit_cfg));
		fye = fy_emitter_create(&emit_cfg);
		if (!fye) {
			fprintf(stderr, "fy_emitter_create() failed\n");
			goto err_out;
		}

		fyge = fy_generic_encoder_create(fye, false);
		if (!fyge) {
			fprintf(stderr, "fy_generic_encoder_create() failed\n");
			goto err_out;
		}
	}

	fygd = fy_generic_decoder_create(fyp, gb, false);
	if (!fygd) {
		fprintf(stderr, "fy_generic_decoder_create() failed\n");
		goto err_out;
	}

	i = 0;
	num_ok = 0;
	num_inputs = argc;
	if (num_inputs <= 0)
		num_inputs = 1;
	do {
		filename = i < argc ? argv[i] : "-";

		if (!strcmp(filename, "-"))
			rc = fy_parser_set_input_fp(fyp, "stdin", stdin);
		else
			rc = fy_parser_set_input_file(fyp, filename);
		if (rc) {
			fprintf(stderr, "Unable to set next input: \"%s\"\n", filename);
			goto err_out;
		}

		fy_generic_builder_reset(gb);

		vdir = fy_generic_decoder_parse_all_documents(fygd);
		if (vdir == fy_invalid) {
			fprintf(stderr, "Error while processing: \"%s\"\n", filename);
			fy_parser_reset(fyp);
		} else {
			if (!opt->null_output) {
				rc = fy_generic_encoder_emit_all_documents(fyge, vdir);
				if (rc) {
					fprintf(stderr, "fy_generic_encoder_emit_all_documents() failed\n");
					goto err_out;
				}
			}
			num_ok++;
		}

	} while (++i < argc);

	ret = num_ok == num_inputs ? 0 : -1;

	if (!opt->null_output)
		fy_generic_encoder_sync(fyge);

	fprintf(stderr, "before\n");
	dump_allocator_info(gb->allocator, gb->alloc_tag);

	{
		const void *p;
		size_t sz;
		fy_generic v;

		v = vdir;
		p = fy_generic_builder_linearize(gb, &v, &sz);
		(void)p;
	}

out:
	/* all handle NULL as NOP */
	fy_generic_encoder_destroy(fyge);
	fy_generic_decoder_destroy(fygd);
	fy_generic_builder_destroy(gb);
	fy_emitter_destroy(fye);
	fy_parser_destroy(fyp);
	fy_allocator_destroy(allocator);
	fy_allocator_destroy(parent_allocator);

	return ret;

err_out:
	ret = -1;
	goto out;
}

static int do_parse_standard(const struct generic_options *opt, int argc, char **argv)
{
	struct fy_parse_cfg parse_cfg;
	struct fy_parser *fyp = NULL;
	struct fy_emitter_cfg emit_cfg;
	struct fy_emitter *fye = NULL;
	struct fy_event *fyev;
	const char *filename;
	int i, rc, num_ok, num_inputs, ret = -1;

	memset(&parse_cfg, 0, sizeof(parse_cfg));
	parse_cfg.flags = FYPCF_DEFAULT_PARSE |
			  (opt->resolve ? FYPCF_RESOLVE_DOCUMENT : 0);

	fyp = fy_parser_create(&parse_cfg);
	if (!fyp) {
		fprintf(stderr, "fy_parser_create() failed\n");
		goto err_out;
	}
	assert(fyp);

	if (!opt->null_output) {
		memset(&emit_cfg, 0, sizeof(emit_cfg));
		fye = fy_emitter_create(&emit_cfg);
		if (!fye) {
			fprintf(stderr, "fy_emitter_create() failed\n");
			goto err_out;
		}
	}

	i = 0;
	num_ok = 0;
	num_inputs = argc;
	if (num_inputs <= 0)
		num_inputs = 1;
	do {
		filename = i < argc ? argv[i] : "-";

		if (!strcmp(filename, "-"))
			rc = fy_parser_set_input_fp(fyp, "stdin", stdin);
		else
			rc = fy_parser_set_input_file(fyp, filename);
		if (rc) {
			fprintf(stderr, "Unable to set next input: \"%s\"\n", filename);
			goto err_out;
		}

		while ((fyev = fy_parser_parse(fyp)) != NULL) {
			if (!opt->null_output) {
				rc = fy_emit_event_from_parser(fye, fyp, fyev);
				if (rc) {
					fprintf(stderr, "fy_emit_event_from_parser() failed\n");
					goto err_out;
				}
			} else
				fy_parser_event_free(fyp, fyev);
		}

		if (fy_parser_get_stream_error(fyp)) {
			fprintf(stderr, "Error while processing: \"%s\"\n", filename);
			fy_parser_reset(fyp);
		} else
			num_ok++;

	} while (++i < argc);

	ret = num_ok == num_inputs ? 0 : -1;

out:
	/* can handle NULL */
	fy_emitter_destroy(fye);
	fy_parser_destroy(fyp);

	return ret;

err_out:
	ret = -1;
	goto out;
}

struct mode_info {
	const char *name;
	int (*exec)(const struct generic_options *opt, int argc, char **argv);
};

static const struct mode_info mode_table[] = {
	{
		.name = "parse-generic",
		.exec = do_parse_generic,
	}, {
		.name = "parse-standard",
		.exec = do_parse_standard,
	}
};

static bool is_mode_valid(const char *mode)
{
	unsigned int i;

	if (!mode)
		return false;

	for (i = 0; i < ARRAY_SIZE(mode_table); i++) {
		if (!strcmp(mode, mode_table[i].name))
			return true;
	}

	return false;
}

static char *get_modes(void)
{
	size_t size, len;
	char *modes, *s;
	unsigned int i;

	size = 0;
	for (i = 0; i < ARRAY_SIZE(mode_table); i++)
		size += strlen(mode_table[i].name) + 1;

	modes = malloc(size);
	if (!modes)
		return NULL;
	
	s = modes;
	for (i = 0; i < ARRAY_SIZE(mode_table); i++) {
		len = strlen(mode_table[i].name);
		memcpy(s, mode_table[i].name, len);
		s += len;
		*s++ = ' ';
	}
	if (s > modes && s[-1] == ' ')
		s[-1] = '\0';
	return modes;
}

static int mode_exec(const struct generic_options *opt, int argc, char **argv)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mode_table); i++) {
		if (!strcmp(opt->mode, mode_table[i].name))
			return mode_table[i].exec(opt, argc, argv);
	}
	return -1;
}

int parse_size_arg(const char *str, size_t *sizep)
{
	size_t size, prev_size;
	char c, unit;

	if (!str)
		return -1;

	size = 0;
	while (isdigit(c = *str++)) {
		prev_size = size;
		size *= 10;
		size += c - '0';
		/* detect overflow */
		if (size < prev_size)
			return -1;
	}
	if (c) {
		unit = toupper(c);
		if (*str)	/* must end */
			return -1;

		prev_size = size;
		switch (unit) {
		case 'K':
			size *= (size_t)1 << 10;
			break;
		case 'M':
			size *= (size_t)1 << 20;
			break;
		case 'G':
			size *= (size_t)1 << 30;
			break;
		case 'T':
			size *= (size_t)1 << 40;
			break;
		default:
			return -1;
		}
		if (size < prev_size)
			return -1;
	}

	*sizep = size;
	return 0;
}

static struct option lopts[] = {
	{"allocator",		required_argument,	0,	'a' },
	{"parent-allocator",	required_argument,	0,	'p' },
	{"size",		required_argument,	0,	's' },
	{"resolve",		no_argument,		0,	'r' },
	{"null-output",		no_argument,		0,	'n' },
	{"mode",		required_argument,	0,	'm' },
	{"help",		no_argument,		0,	'h' },
	{0,			0,              	0,	 0  },
};

static void display_usage(FILE *fp, const char *progname)
{
	char *names = NULL, *modes = NULL;
	const char *s;

	names = fy_allocator_get_names();
	assert(names);

	modes = get_modes();
	assert(modes);

	s = strrchr(progname, '/');
	if (s != NULL)
		progname = s + 1;

	fprintf(fp, "Usage:\n\t%s [options] [<file>]\n", progname);
	fprintf(fp, "\noptions:\n");
	fprintf(fp, "\t--allocator <n>, -a <n>       : Use allocator, one of: %s\n", names); 
	fprintf(fp, "\t--parent <n>, -p <n>          : Use parent allocator, one of: %s\n", names); 
	fprintf(fp, "\t--size <n>, -s <n>            : Size for allocators that require one\n");
	fprintf(fp, "\t--resolve, -r                 : Perform anchor and merge key resolution\n");
	fprintf(fp, "\t--null-output, -n             : No emitting, just parsing\n");
	fprintf(fp, "\t--mode <m>, -m <m>            : Mode, one of: %s\n", modes);
	fprintf(fp, "\t--help, -h                    : Display help message\n");
	fprintf(fp, "\n");

	free(names);
	free(modes);
}

int main(int argc, char *argv[])
{
	struct generic_options gopt;
	int opt, lidx, rc;
	int exitcode = EXIT_FAILURE;

	gopt = default_generic_options;

	while ((opt = getopt_long_only(argc, argv, "a:p:s:rnm:h", lopts, &lidx)) != -1) {
		switch (opt) {
		case 'a':
		case 'p':
			if (!fy_allocator_is_available(optarg)) {
				fprintf(stderr, "Error: illegal allocator name \"%s\"\n", optarg);
				goto err_out_usage;
			}
			if (opt == 'a')
				gopt.allocator = optarg;
			else
				gopt.parent_allocator = optarg;
			break;
		case 's':
			rc = parse_size_arg(optarg, &gopt.size);
			if (rc) {
				fprintf(stderr, "Error: illegal size \"%s\"\n", optarg);
				goto err_out_usage;
			}
			break;
		case 'r':
			gopt.resolve = true;
			break;
		case 'n':
			gopt.null_output = true;
			break;
		case 'm':
			if (!is_mode_valid(optarg)) {
				fprintf(stderr, "Error: illegal mode \"%s\"\n", optarg);
				goto err_out_usage;
			}
			gopt.mode = optarg;
			break;
		case 'h' :
			display_usage(stdout, argv[0]);
			goto ok_out;
		default:
			goto err_out_usage;
		}
	}

	rc = mode_exec(&gopt, argc - optind, argv + optind);
	if (rc)
		goto err_out;

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
