#define _GNU_SOURCE
#include "fy-allocator-durable.h"
#include <libfyaml/libfyaml-generic.h>
#ifdef __FILC__
#include <stdfil.h>
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Test-only adapter accesses the private reservation. No production API yet. */
#define BASE UINT64_C(0x300000000000)
#define REGION (16ULL << 20)
#define CHUNK (64ULL << 10)
#define MAGIC UINT64_C(0x4659435245433031)
struct record {
    uint64_t magic, prev, id;
    fy_generic number;
    unsigned char payload[16384];
};
static uint64_t last_sum;
static void gate_wait(const char *gate)
{
    char ready[4096];
    assert(snprintf(ready, sizeof(ready), "%s.ready.%ld", gate, (long)getpid()) < (int)sizeof(ready));
    int fd = open(ready, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0);
    close(fd);
    unsigned tries = 0;
    while (access(gate, F_OK)) {
        assert(errno == ENOENT && ++tries < 10000);
        usleep(1000);
    }
}
static struct fy_allocator *open_arena(const char *dir, unsigned flags)
{
    struct fy_durable_allocator_cfg c = {0};
    c.dir = dir; c.region_base = BASE; c.region_size = REGION; c.chunk_size = CHUNK;
    c.flags = FY_DURABLE_ARENA_CREATE | FY_DURABLE_ARENA_SPARSE | flags;
    struct fy_allocator *a = fy_allocator_create("durable", &c);
    if (!a) { perror("open durable"); exit(2); }
    return a;
}
static uint64_t encode(struct fy_allocator *a, const void *p)
{
    struct fy_durable_allocator *da = (void *)a;
    uintptr_t off = (uintptr_t)p - (uintptr_t)da->content.region;
    assert(off < REGION);
    return BASE + off;
}
static const struct record *resolve(struct fy_allocator *a, uint64_t word)
{
    struct fy_durable_allocator *da = (void *)a;
    if (word < BASE || word - BASE > REGION - sizeof(struct record)) return NULL;
    size_t off = word - BASE;
    size_t gen = off / CHUNK;
    size_t hdr_off = gen ? 0 : da->content.chunk0_reserve;
    if ((off % CHUNK) < hdr_off + FY_DURABLE_DATA0 ||
        sizeof(struct record) > CHUNK - off % CHUNK) return NULL;
    /* This operation ensures the published chunk chain is mapped locally. */
    (void)fy_allocator_chunk_count(a);
    if (!fy_atomic_load(&da->content.chunks[gen])) return NULL;
    const struct record *r = (const void *)((const char *)da->content.region + off);
    return r;
}
static void append(struct fy_allocator *a, uint64_t id)
{
    /* Explicit malloc backend for all transient staging, never mremap. */
    struct fy_allocator *ma = fy_allocator_create("malloc", NULL);
    assert(ma);
    struct record *r = fy_allocator_alloc(ma, FY_ALLOC_TAG_DEFAULT, sizeof(*r), 16);
    assert(r);
    memset(r, 0, sizeof(*r));
    r->magic = MAGIC; r->id = id;
    r->number.v = fy_generic_in_place_uint_type(id);
    memset(r->payload, (unsigned char)id, sizeof(r->payload));
    for (;;) {
        uint64_t old = fy_allocator_refs_get(a);
        r->prev = old;
        const void *p = fy_allocator_store(a, FY_ALLOC_TAG_DEFAULT, r, sizeof(*r), 16);
        assert(p);
        int rc = fy_allocator_refs_publish(a, old, encode(a, p), FY_ALLOC_REFS_CHECKPOINT);
        assert(rc >= 0);
        if (!rc) break;
    }
    fy_allocator_destroy(ma);
}
static size_t check(struct fy_allocator *a)
{
    uint64_t word = fy_allocator_refs_get(a);
    size_t n = 0;
    last_sum = 0;
    while (word) {
        const struct record *r = resolve(a, word);
        assert(r && r->magic == MAGIC);
        assert(fy_generic_get_uint_type_no_check(r->number) == r->id);
        last_sum += r->id;
        for (size_t i = 0; i < sizeof(r->payload); i++)
            assert(r->payload[i] == (unsigned char)r->id);
        word = r->prev;
        assert(++n < 10000);
    }
    printf("RECORDS=%zu SUM=%llu CHUNKS=%u\n", n,
           (unsigned long long)last_sum, fy_allocator_chunk_count(a));
    return n;
}
struct worker { struct fy_allocator *a; unsigned id; };
static void *worker(void *p)
{
    struct worker *w = p;
    for (unsigned i = 0; i < 8; i++) append(w->a, w->id * 100 + i);
    return NULL;
}
static bool first_checkpoint(uint64_t slot, uint64_t head, uint64_t bump,
                             uint64_t refs, void *arg)
{
    (void)head; (void)bump; (void)refs;
    *(uint64_t *)arg = slot;
    return false;
}
int main(int argc, char **argv)
{
    assert(argc >= 3);
    const char *mode = argv[1], *dir = argv[2];
    if (!strcmp(mode, "gc-unsupported")) {
        errno = 0;
        assert(fy_durable_arena_gc(dir) == -1 && errno == ENOTSUP);
        puts("GC_EXPLICITLY_UNSUPPORTED"); return 0;
    }
    if (!strcmp(mode, "dedup")) {
        struct fy_durable_allocator_cfg c = {.dir=dir, .region_base=BASE,
            .region_size=REGION, .chunk_size=CHUNK,
            .flags=FY_DURABLE_ARENA_CREATE | FY_DURABLE_ARENA_DEDUP};
        errno = 0;
        assert(!fy_allocator_create("durable", &c) && errno == ENOTSUP);
        puts("DEDUP_EXPLICITLY_UNSUPPORTED"); return 0;
    }
    unsigned flags = (!strcmp(mode, "readonly") || !strcmp(mode, "watch")) ?
        FY_DURABLE_ARENA_READONLY : 0;
    if (!strcmp(mode, "checkpoint") || !strcmp(mode, "recover") ||
        !strcmp(mode, "verify")) flags |= FY_DURABLE_ARENA_CHECKPOINT;
    struct fy_allocator *a = open_arena(dir, flags);
    if (!strcmp(mode, "seed") || !strcmp(mode, "crash")) {
        assert(!fy_allocator_refs_get(a));
        append(a, 42);
        assert(check(a) == 1);
        if (!strcmp(mode, "crash")) _exit(0);
    } else if (!strcmp(mode, "append")) {
        unsigned count = argc > 3 ? strtoul(argv[3], NULL, 0) : 24;
        unsigned start = argc > 4 ? strtoul(argv[4], NULL, 0) : 100;
        if (argc > 5) gate_wait(argv[5]);
        for (unsigned i = 0; i < count; i++) append(a, start + i);
        check(a);
    } else if (!strcmp(mode, "watch")) {
        assert(argc == 5);
        size_t expected = strtoul(argv[3], NULL, 0);
        size_t old = check(a), n = old;
        assert(old == 1);
        gate_wait(argv[4]);
        for (unsigned tries = 0; n < expected; tries++) {
            assert(tries < 10000);
            n = check(a);
            assert(n >= old && n <= expected);
            old = n;
            if (n < expected) usleep(1000);
        }
    } else if (!strcmp(mode, "read") || !strcmp(mode, "readonly")) {
        size_t n = check(a);
        if (argc > 3) assert(n == strtoul(argv[3], NULL, 0));
        if (argc > 4) assert(last_sum == strtoull(argv[4], NULL, 0));
        uint64_t root = fy_allocator_refs_get(a);
        if (flags & FY_DURABLE_ARENA_READONLY)
            assert(fy_allocator_refs_publish(a, root, root, 0) == -1);
    } else if (!strcmp(mode, "cas")) {
        uint64_t root = fy_allocator_refs_get(a);
        assert(root);
        assert(fy_allocator_refs_publish(a, 0, root, 0) == 1);
        assert(fy_allocator_refs_get(a) == root);
        assert(fy_allocator_refs_publish(a, root, root, 0) == 0);
    } else if (!strcmp(mode, "threads")) {
        pthread_t t[4]; struct worker w[4];
        for (unsigned i = 0; i < 4; i++) {
            w[i] = (struct worker){a, i + 1};
            assert(!pthread_create(&t[i], NULL, worker, &w[i]));
        }
        for (unsigned i = 0; i < 4; i++) assert(!pthread_join(t[i], NULL));
        assert(check(a) == 32);
    } else if (!strcmp(mode, "checkpoint")) {
        append(a, 100);
        assert(!fy_allocator_checkpoint(a));
        assert(!fy_allocator_verify(a));
    } else if (!strcmp(mode, "verify")) {
        assert(!fy_allocator_verify(a));
        check(a);
    } else if (!strcmp(mode, "recover")) {
        assert(!fy_allocator_refs_get(a));
        append(a, 42);
        assert(!fy_allocator_checkpoint(a));
        uint64_t slot = 0;
        assert(!fy_allocator_checkpoint_iterate(a, first_checkpoint, &slot) && slot);
        for (unsigned i = 0; i < 8; i++) append(a, 100 + i);
        assert(!fy_allocator_checkpoint(a));
        assert(!fy_allocator_verify(a));
        assert(check(a) == 9);
        assert(!fy_allocator_checkpoint_recover(a, slot));
        assert(check(a) == 1);
    } else if (!strcmp(mode, "uaf")) {
        const struct record *r = resolve(a, fy_allocator_refs_get(a));
        assert(r);
        fy_allocator_destroy(a);
        printf("UNEXPECTED=%llu\n", (unsigned long long)((volatile const struct record *)r)->id);
        return 3;
    } else if (!strcmp(mode, "reopen")) {
        const struct record *r = resolve(a, fy_allocator_refs_get(a));
        assert(r);
        fy_allocator_destroy(a);
        a = open_arena(dir, 0);
        assert(check(a));
#ifdef __FILC__
        printf("UNEXPECTED=%llu\n", (unsigned long long)((volatile const struct record *)r)->id);
        return 3;
#endif
    } else if (!strcmp(mode, "inspect")) {
#ifdef __FILC__
        struct fy_durable_allocator *da = (void *)a;
        zprintf("REGION=%P\n", da->content.region);
#endif
        check(a);
    } else return 2;
    fy_allocator_destroy(a);
    puts("OK"); return 0;
}
