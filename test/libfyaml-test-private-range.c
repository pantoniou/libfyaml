/*
 * libfyaml-test-private-range.c - libfyaml address range registry tests
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#ifndef _WIN32
#include <pthread.h>
#endif

#include <check.h>

#include <libfyaml.h>

#include "fy-utils.h"
#include "fy-atomics.h"
#include "fy-allocator.h"
#include "fy-range-registry.h"

#ifdef HAVE_GENERIC
#include <libfyaml/libfyaml-generic.h>
#include "fy-generic.h"
#endif

#include "fy-check.h"

/* owners are only compared, never dereferenced */
static char owner_tokens[64];
#define OWNER(_n)	((struct fy_allocator *)(void *)&owner_tokens[(_n)])

START_TEST(range_basic)
{
	struct fy_range_info info;
	size_t base_count;
	char *buf;

	buf = malloc(4096);
	ck_assert_ptr_ne(buf, NULL);
	base_count = fy_range_count();

	ck_assert_int_eq(fy_range_register(buf + 1024, 1024, OWNER(1), 3), 0);
	ck_assert_uint_eq(fy_range_count(), base_count + 1);

	/* edges */
	ck_assert(!fy_range_lookup(buf + 1023, NULL));
	ck_assert(fy_range_lookup(buf + 1024, &info));
	ck_assert_ptr_eq(info.start, buf + 1024);
	ck_assert_uint_eq(info.size, 1024);
	ck_assert_ptr_eq(info.owner, OWNER(1));
	ck_assert_int_eq(info.tag, 3);
	ck_assert(fy_range_lookup(buf + 2047, &info));
	ck_assert(!fy_range_lookup(buf + 2048, NULL));

	/* bad arguments */
	ck_assert_int_eq(fy_range_register(NULL, 16, OWNER(1), 0), -1);
	ck_assert_int_eq(fy_range_register(buf, 0, OWNER(1), 0), -1);

	/* unknown ranges */
	ck_assert_int_eq(fy_range_unregister(buf, OWNER(1)), -1);
	ck_assert_int_eq(errno, ENOENT);
	ck_assert_int_eq(fy_range_unregister(buf + 1024, OWNER(2)), -1);

	ck_assert_int_eq(fy_range_unregister(buf + 1024, OWNER(1)), 0);
	ck_assert(!fy_range_lookup(buf + 1024, NULL));
	ck_assert_uint_eq(fy_range_count(), base_count);

	free(buf);
}
END_TEST

START_TEST(range_overlap)
{
	char *buf;

	buf = malloc(4096);
	ck_assert_ptr_ne(buf, NULL);

	ck_assert_int_eq(fy_range_register(buf + 1024, 1024, OWNER(1), 0), 0);

	/* partial overlaps from either side */
	ck_assert_int_eq(fy_range_register(buf + 512, 1024, OWNER(2), 0), -1);
	ck_assert_int_eq(errno, EEXIST);
	ck_assert_int_eq(fy_range_register(buf + 1536, 1024, OWNER(2), 0), -1);
	ck_assert_int_eq(errno, EEXIST);

	/* exact duplicate, even with another owner */
	ck_assert_int_eq(fy_range_register(buf + 1024, 1024, OWNER(2), 0), -1);
	ck_assert_int_eq(errno, EEXIST);

	/* adjacent ranges are fine */
	ck_assert_int_eq(fy_range_register(buf, 1024, OWNER(2), 0), 0);
	ck_assert_int_eq(fy_range_register(buf + 2048, 1024, OWNER(3), 0), 0);

	ck_assert_int_eq(fy_range_unregister(buf, OWNER(2)), 0);
	ck_assert_int_eq(fy_range_unregister(buf + 1024, OWNER(1)), 0);
	ck_assert_int_eq(fy_range_unregister(buf + 2048, OWNER(3)), 0);

	free(buf);
}
END_TEST

START_TEST(range_nesting)
{
	struct fy_range_info info;
	char *buf;

	buf = malloc(4096);
	ck_assert_ptr_ne(buf, NULL);

	/* register inner first, outer last, to exercise the ordering */
	ck_assert_int_eq(fy_range_register(buf + 1024, 512, OWNER(3), 0), 0);
	ck_assert_int_eq(fy_range_register(buf + 1024, 1024, OWNER(2), 0), 0);
	ck_assert_int_eq(fy_range_register(buf, 4096, OWNER(1), 0), 0);
	ck_assert_int_eq(fy_range_register(buf + 3072, 256, OWNER(4), 0), 0);

	/* innermost wins */
	ck_assert(fy_range_lookup(buf + 1100, &info));
	ck_assert_ptr_eq(info.owner, OWNER(3));
	ck_assert(fy_range_lookup(buf + 1600, &info));
	ck_assert_ptr_eq(info.owner, OWNER(2));
	ck_assert(fy_range_lookup(buf + 2500, &info));
	ck_assert_ptr_eq(info.owner, OWNER(1));
	ck_assert(fy_range_lookup(buf + 3100, &info));
	ck_assert_ptr_eq(info.owner, OWNER(4));
	/* past a nested range, back in the outer one */
	ck_assert(fy_range_lookup(buf + 3500, &info));
	ck_assert_ptr_eq(info.owner, OWNER(1));
	ck_assert(fy_range_lookup(buf, &info));
	ck_assert_ptr_eq(info.owner, OWNER(1));

	/* lookup by owner walks outwards */
	ck_assert(fy_range_lookup_owner(buf + 1100, OWNER(1), &info));
	ck_assert_ptr_eq(info.start, buf);
	ck_assert(fy_range_lookup_owner(buf + 1100, OWNER(2), &info));
	ck_assert_ptr_eq(info.start, buf + 1024);
	ck_assert(!fy_range_lookup_owner(buf + 1600, OWNER(3), NULL));
	ck_assert(!fy_range_lookup_owner(buf + 1100, OWNER(4), NULL));

	/* straddling a nested range's end is a partial overlap */
	ck_assert_int_eq(fy_range_register(buf + 1500, 100, OWNER(5), 0), -1);

	/* removing a middle level re-parents the inner range */
	ck_assert_int_eq(fy_range_unregister(buf + 1024, OWNER(2)), 0);
	ck_assert(fy_range_lookup(buf + 1600, &info));
	ck_assert_ptr_eq(info.owner, OWNER(1));
	ck_assert(fy_range_lookup(buf + 1100, &info));
	ck_assert_ptr_eq(info.owner, OWNER(3));

	ck_assert_int_eq(fy_range_unregister(buf, OWNER(1)), 0);
	ck_assert(!fy_range_lookup(buf + 2500, NULL));
	ck_assert(fy_range_lookup(buf + 1100, &info));
	ck_assert_ptr_eq(info.owner, OWNER(3));

	ck_assert_int_eq(fy_range_unregister(buf + 1024, OWNER(3)), 0);
	ck_assert_int_eq(fy_range_unregister(buf + 3072, OWNER(4)), 0);

	free(buf);
}
END_TEST

START_TEST(range_resize)
{
	struct fy_range_info info;
	char *buf;

	buf = malloc(8192);
	ck_assert_ptr_ne(buf, NULL);

	ck_assert_int_eq(fy_range_register(buf, 1024, OWNER(1), 7), 0);
	ck_assert_int_eq(fy_range_register(buf + 4096, 1024, OWNER(2), 0), 0);
	ck_assert_int_eq(fy_range_register(buf + 512, 256, OWNER(3), 0), 0);

	/* grow into the gap; the tag is kept */
	ck_assert_int_eq(fy_range_resize(buf, OWNER(1), 4096), 0);
	ck_assert(fy_range_lookup(buf + 4000, &info));
	ck_assert_ptr_eq(info.owner, OWNER(1));
	ck_assert_int_eq(info.tag, 7);
	ck_assert_uint_eq(info.size, 4096);

	/* growing over the neighbour is refused, and nothing changes */
	ck_assert_int_eq(fy_range_resize(buf, OWNER(1), 4097), -1);
	ck_assert_int_eq(errno, EEXIST);
	ck_assert(fy_range_lookup(buf + 4096, &info));
	ck_assert_ptr_eq(info.owner, OWNER(2));

	/* shrinking below a nested range's end is refused */
	ck_assert_int_eq(fy_range_resize(buf, OWNER(1), 700), -1);
	ck_assert_int_eq(fy_range_resize(buf, OWNER(1), 768), 0);
	ck_assert(!fy_range_lookup(buf + 768, NULL));

	ck_assert_int_eq(fy_range_resize(buf + 100, OWNER(1), 16), -1);
	ck_assert_int_eq(errno, ENOENT);

	ck_assert_int_eq(fy_range_unregister(buf + 512, OWNER(3)), 0);
	ck_assert_int_eq(fy_range_unregister(buf, OWNER(1)), 0);
	ck_assert_int_eq(fy_range_unregister(buf + 4096, OWNER(2)), 0);

	free(buf);
}
END_TEST

#define MANY_RANGES	257

START_TEST(range_many)
{
	struct fy_range_info info;
	unsigned int order[MANY_RANGES];
	size_t base_count;
	unsigned int i, j, k;
	char *buf;

	buf = malloc(MANY_RANGES * 64);
	ck_assert_ptr_ne(buf, NULL);
	base_count = fy_range_count();

	/* a permutation, so registration is out of address order */
	for (i = 0; i < MANY_RANGES; i++)
		order[i] = (i * 101) % MANY_RANGES;

	for (i = 0; i < MANY_RANGES; i++) {
		k = order[i];
		ck_assert_int_eq(fy_range_register(buf + k * 64, 48, OWNER(k % 64), (int)k), 0);
	}
	ck_assert_uint_eq(fy_range_count(), base_count + MANY_RANGES);

	for (k = 0; k < MANY_RANGES; k++) {
		ck_assert(fy_range_lookup(buf + k * 64 + 47, &info));
		ck_assert_int_eq(info.tag, (int)k);
		ck_assert(!fy_range_lookup(buf + k * 64 + 48, NULL));
	}

	/* remove every other one, then check */
	for (i = 0; i < MANY_RANGES; i += 2) {
		k = order[i];
		ck_assert_int_eq(fy_range_unregister(buf + k * 64, OWNER(k % 64)), 0);
	}
	for (i = 0; i < MANY_RANGES; i++) {
		k = order[i];
		ck_assert(fy_range_lookup(buf + k * 64, NULL) == (i & 1));
	}
	for (i = 1; i < MANY_RANGES; i += 2) {
		k = order[i];
		ck_assert_int_eq(fy_range_unregister(buf + k * 64, OWNER(k % 64)), 0);
	}
	ck_assert_uint_eq(fy_range_count(), base_count);

	for (j = 0; j < MANY_RANGES; j++)
		ck_assert(!fy_range_lookup(buf + j * 64, NULL));

	free(buf);
}
END_TEST

START_TEST(range_allocators)
{
	static const char * const names[] = { "linear", "mremap", "auto" };
	struct fy_linear_allocator_cfg lcfg;
	struct fy_range_info info;
	struct fy_allocator *a;
	size_t base_count;
	unsigned int i;
	char stack_buf[4096];
	void *p;
	int tag;

	base_count = fy_range_count();

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		memset(&lcfg, 0, sizeof(lcfg));
		lcfg.size = 65536;
		a = fy_allocator_create(names[i], !strcmp(names[i], "linear") ? &lcfg : NULL);
		ck_assert_ptr_ne(a, NULL);

		tag = fy_allocator_get_tag(a);
		ck_assert_int_ge(tag, 0);
		p = fy_allocator_alloc(a, tag, 128, 16);
		ck_assert_ptr_ne(p, NULL);

		/* the innermost registered owner of the memory is the allocator
		 * that obtained it (auto delegates to an mremap allocator) */
		ck_assert(fy_range_lookup(p, &info));
		if (strcmp(names[i], "auto"))
			ck_assert_ptr_eq(info.owner, a);
		ck_assert(fy_allocator_contains(info.owner, info.tag, p));

		fy_allocator_destroy(a);
		ck_assert(!fy_range_lookup(p, NULL));
		ck_assert_uint_eq(fy_range_count(), base_count);
	}

	/* in-place allocators are never destroyed, so they don't register */
	a = fy_linear_allocator_create_in_place(stack_buf, sizeof(stack_buf));
	ck_assert_ptr_ne(a, NULL);
	p = fy_allocator_alloc(a, 0, 128, 16);
	ck_assert_ptr_ne(p, NULL);
	ck_assert(!fy_range_lookup(p, NULL));
	ck_assert_uint_eq(fy_range_count(), base_count);
}
END_TEST

#ifdef HAVE_GENERIC

/* does the registry attribute v to the memory owner of (gb's allocator, tag)? */
static bool builder_owns_by_registry(struct fy_generic_builder *gb, fy_generic v)
{
	struct fy_allocator *owner;
	struct fy_range_info info;
	int owner_tag;

	fy_allocator_resolve_owner(gb->allocator, gb->alloc_tag, &owner, &owner_tag);
	return fy_range_lookup_owner(fy_generic_resolve_ptr(v), owner, &info) &&
	       info.tag == owner_tag;
}

START_TEST(range_builders)
{
	static const char long_str[] = "a string long enough to be stored out of place";
	static const bool registered[5] = { true, true, true, false, false };
	static const char * const owner_names[5] = { "mremap", "mremap", "linear", "malloc", "linear" };
	struct fy_generic_builder_cfg cfg;
	struct fy_generic_builder *gb[5], *other;
	struct fy_allocator *linear, *malloc_a, *owner;
	struct fy_linear_allocator_cfg lcfg;
	char stack_buf[16384];
	fy_generic v, vo;
	unsigned int i;
	int owner_tag;

	memset(&lcfg, 0, sizeof(lcfg));
	lcfg.size = 65536;
	linear = fy_allocator_create("linear", &lcfg);
	malloc_a = fy_allocator_create("malloc", NULL);
	ck_assert_ptr_ne(linear, NULL);
	ck_assert_ptr_ne(malloc_a, NULL);

	other = fy_generic_builder_create(NULL);
	ck_assert_ptr_ne(other, NULL);
	vo = fy_gb_string_create_out_of_place(other, long_str);
	ck_assert(fy_generic_is_valid(vo));

	memset(&cfg, 0, sizeof(cfg));
	gb[0] = fy_generic_builder_create(&cfg);			/* auto -> mremap */
	cfg.flags = FYGBCF_DEDUP_ENABLED;
	gb[1] = fy_generic_builder_create(&cfg);			/* auto -> dedup -> mremap */
	cfg.flags = 0;
	cfg.allocator = linear;
	gb[2] = fy_generic_builder_create(&cfg);			/* linear */
	cfg.allocator = malloc_a;
	gb[3] = fy_generic_builder_create(&cfg);			/* malloc, unregistered */
	gb[4] = fy_generic_builder_create_in_place(0, NULL, stack_buf, sizeof(stack_buf));

	for (i = 0; i < 5; i++) {
		ck_assert_ptr_ne(gb[i], NULL);

		/* the layers resolve down to the allocator owning the memory */
		fy_allocator_resolve_owner(gb[i]->allocator, gb[i]->alloc_tag, &owner, &owner_tag);
		ck_assert_str_eq(owner->name, owner_names[i]);
		ck_assert(!!(owner->flags & FYAF_REGISTERS_RANGES) == registered[i]);

		v = fy_gb_string_create_out_of_place(gb[i], long_str);
		ck_assert(fy_generic_is_valid(v));
		ck_assert(fy_generic_builder_contains(gb[i], v));
		ck_assert(!fy_generic_builder_contains(gb[i], vo));

		/* where the owner registers its memory, the registry gives the
		 * same answer as the builder */
		if (registered[i]) {
			ck_assert(builder_owns_by_registry(gb[i], v));
			ck_assert(!builder_owns_by_registry(gb[i], vo));
		}
	}

	for (i = 0; i < 4; i++)
		fy_generic_builder_destroy(gb[i]);
	fy_generic_builder_destroy(other);
	fy_allocator_destroy(linear);
	fy_allocator_destroy(malloc_a);
}
END_TEST

#endif

#ifndef _WIN32

#define STRESS_WRITERS		3
#define STRESS_READERS		4
#define STRESS_SLOTS		16
#define STRESS_ROUNDS		2000

struct range_stress {
	char *buf;			/* anchor + STRESS_WRITERS * STRESS_SLOTS slots */
	FY_ATOMIC(int) stop;
	FY_ATOMIC(unsigned long) errors;
	FY_ATOMIC(unsigned long) lookups;
};

struct range_stress_arg {
	struct range_stress *rs;
	unsigned int id;
};

/* slot layout: 256 bytes per slot, the range itself is the first 128 */
static char *stress_slot(struct range_stress *rs, unsigned int w, unsigned int s)
{
	return rs->buf + 4096 + ((size_t)w * STRESS_SLOTS + s) * 256;
}

static void *range_stress_writer(void *arg)
{
	struct range_stress_arg *ra = arg;
	struct range_stress *rs = ra->rs;
	unsigned int r, s;

	for (r = 0; r < STRESS_ROUNDS; r++) {
		s = r % STRESS_SLOTS;
		if (fy_range_register(stress_slot(rs, ra->id, s), 128, OWNER(ra->id + 1), (int)s))
			fy_atomic_fetch_add(&rs->errors, 1);
		if (r & 1 && fy_range_resize(stress_slot(rs, ra->id, s), OWNER(ra->id + 1), 192))
			fy_atomic_fetch_add(&rs->errors, 1);
		if (fy_range_unregister(stress_slot(rs, ra->id, s), OWNER(ra->id + 1)))
			fy_atomic_fetch_add(&rs->errors, 1);
	}
	return NULL;
}

static void *range_stress_reader(void *arg)
{
	struct range_stress_arg *ra = arg;
	struct range_stress *rs = ra->rs;
	struct fy_range_info info;
	unsigned int w, s, n;
	unsigned long errors = 0;
	char *p;

	for (n = ra->id; !fy_atomic_load(&rs->stop); n++) {
		/* the anchor is always there */
		if (!fy_range_lookup(rs->buf + (n % 4096), &info) || info.owner != OWNER(0))
			errors++;

		/* a writer slot is either absent or carries its own identity */
		w = n % STRESS_WRITERS;
		s = (n / STRESS_WRITERS) % STRESS_SLOTS;
		p = stress_slot(rs, w, s) + (n % 192);
		if (fy_range_lookup(p, &info) &&
		    (info.owner != OWNER(w + 1) || info.tag != (int)s ||
		     info.start != stress_slot(rs, w, s)))
			errors++;
	}
	fy_atomic_fetch_add(&rs->errors, errors);
	fy_atomic_fetch_add(&rs->lookups, n - ra->id);
	return NULL;
}

START_TEST(range_threads)
{
	struct range_stress rs;
	struct range_stress_arg wargs[STRESS_WRITERS], rargs[STRESS_READERS];
	pthread_t writers[STRESS_WRITERS], readers[STRESS_READERS];
	unsigned int i;

	memset(&rs, 0, sizeof(rs));
	rs.buf = malloc(4096 + STRESS_WRITERS * STRESS_SLOTS * 256);
	ck_assert_ptr_ne(rs.buf, NULL);
	fy_atomic_store(&rs.stop, 0);
	fy_atomic_store(&rs.errors, 0);
	fy_atomic_store(&rs.lookups, 0);

	ck_assert_int_eq(fy_range_register(rs.buf, 4096, OWNER(0), 0), 0);

	for (i = 0; i < STRESS_READERS; i++) {
		rargs[i].rs = &rs;
		rargs[i].id = i;
		ck_assert_int_eq(pthread_create(&readers[i], NULL, range_stress_reader, &rargs[i]), 0);
	}
	for (i = 0; i < STRESS_WRITERS; i++) {
		wargs[i].rs = &rs;
		wargs[i].id = i;
		ck_assert_int_eq(pthread_create(&writers[i], NULL, range_stress_writer, &wargs[i]), 0);
	}
	for (i = 0; i < STRESS_WRITERS; i++)
		pthread_join(writers[i], NULL);
	fy_atomic_store(&rs.stop, 1);
	for (i = 0; i < STRESS_READERS; i++)
		pthread_join(readers[i], NULL);

	ck_assert_uint_eq(fy_atomic_load(&rs.errors), 0);
	ck_assert_uint_gt(fy_atomic_load(&rs.lookups), 0);

	ck_assert_int_eq(fy_range_unregister(rs.buf, OWNER(0)), 0);
	free(rs.buf);
}
END_TEST

#endif

void libfyaml_case_private_range(struct fy_check_suite *cs)
{
	struct fy_check_testcase *ctc;

	ctc = fy_check_suite_add_test_case(cs, "private-range");

	fy_check_testcase_add_test(ctc, range_basic);
	fy_check_testcase_add_test(ctc, range_overlap);
	fy_check_testcase_add_test(ctc, range_nesting);
	fy_check_testcase_add_test(ctc, range_resize);
	fy_check_testcase_add_test(ctc, range_many);
	fy_check_testcase_add_test(ctc, range_allocators);
#ifdef HAVE_GENERIC
	fy_check_testcase_add_test(ctc, range_builders);
#endif
#ifndef _WIN32
	fy_check_testcase_add_test(ctc, range_threads);
#endif
}
