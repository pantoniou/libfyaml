/*
 * bench_compare.cpp - libfyaml vs Metall allocation throughput
 *
 * Runs the same size-mix workload through every allocator so results
 * are directly comparable in a single pass.
 *
 * Build:
 *   g++ -O2 -g -std=c++17 -o bench_compare bench_compare.cpp \
 *       -I include -I src/allocator -I /tmp/metall/include \
 *       -L build -lfyaml -Wl,-rpath,$(pwd)/build -lpthread
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <vector>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

/* Metall must come before libfyaml: libfyaml/libfyaml-atomics.h redefines
   atomic_load/store/exchange as C11 macros that break C++ <memory>. Including
   Metall first pulls in <memory> before the macros are defined. */
#include <metall/metall.hpp>

/* Push then undef the conflicting macros so libfyaml's header can re-define
   them; they don't affect C++ code after this point. */
#pragma push_macro("atomic_load")
#pragma push_macro("atomic_store")
#pragma push_macro("atomic_exchange")
#pragma push_macro("atomic_compare_exchange_strong")
#pragma push_macro("atomic_compare_exchange_weak")
#undef atomic_load
#undef atomic_store
#undef atomic_exchange
#undef atomic_compare_exchange_strong
#undef atomic_compare_exchange_weak

extern "C" {
#include <libfyaml.h>
}

#pragma pop_macro("atomic_load")
#pragma pop_macro("atomic_store")
#pragma pop_macro("atomic_exchange")
#pragma pop_macro("atomic_compare_exchange_strong")
#pragma pop_macro("atomic_compare_exchange_weak")


/* ---- benchmark parameters ---- */
static constexpr long   NALLOCS   = 500000;
static constexpr int    WARMUP    = 10000;
static constexpr int    RUNS      = 5;

/* durable allocator geometry */
static constexpr uint64_t DURABLE_BASE       = 0x520000000000ULL;
static constexpr uint64_t DURABLE_REGION_SZ  = 256ULL << 20;
static constexpr uint64_t DURABLE_CHUNK_SZ   =  16ULL << 20;

/* Metall geometry — initial segment size (grows automatically) */
static constexpr size_t METALL_INIT_SZ = 256ULL << 20;


/* size distribution matches bench_alloc.c */
static constexpr size_t SIZE_TABLE[] = { 8,8,16,8,64,8,32,256,8,16 };
static constexpr int    SIZE_TABLE_LEN = sizeof(SIZE_TABLE) / sizeof(SIZE_TABLE[0]);

/* ---- helpers ---- */

static uint64_t now_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static char *make_tmpdir()
{
    char *buf = (char *)malloc(64);
    const char *tmp = getenv("TMPDIR");
    snprintf(buf, 64, "%s/fy-cmp-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    return mkdtemp(buf) ? buf : nullptr;
}

static void rm_rf(const char *dir)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd)) {}
}

struct Result {
    std::string name;
    uint64_t    min_ns   = UINT64_MAX;
    uint64_t    total_ns = 0;
    int         runs     = 0;
};

static void print_result(const Result &r)
{
    if (r.runs == 0) {
        printf("  %-20s  (failed)\n", r.name.c_str());
        return;
    }
    double avg_ns   = (double)r.total_ns / r.runs / NALLOCS;
    double min_ns   = (double)r.min_ns   / NALLOCS;
    double avg_mops = 1e9 / avg_ns / 1e6;
    double max_mops = 1e9 / min_ns / 1e6;
    printf("  %-20s  avg %6.1f ns/alloc  best %6.1f ns/alloc  "
           "avg %6.2f Mops/s  peak %6.2f Mops/s\n",
           r.name.c_str(), avg_ns, min_ns, avg_mops, max_mops);
}

/* ---- libfyaml allocator driver ---- */

static Result bench_fy(struct fy_allocator *a, const char *label)
{
    Result r;
    r.name = label;
    volatile uint8_t sink = 0;

    /* warmup */
    for (int i = 0; i < WARMUP; i++) {
        int tag = fy_allocator_get_tag(a);
        if (tag < 0) break;
        size_t sz = SIZE_TABLE[i % SIZE_TABLE_LEN];
        void *p = fy_allocator_alloc(a, tag, sz, 8);
        if (p) sink ^= *(volatile uint8_t *)p;
        fy_allocator_release_tag(a, tag);
    }
    (void)sink;

    for (int run = 0; run < RUNS; run++) {
        int tag = fy_allocator_get_tag(a);
        if (tag < 0) return r;

        uint64_t t0 = now_ns();
        for (long i = 0; i < NALLOCS; i++) {
            size_t sz = SIZE_TABLE[i % SIZE_TABLE_LEN];
            void *p = fy_allocator_alloc(a, tag, sz, 8);
            if (!p) { fy_allocator_release_tag(a, tag); return r; }
            *(volatile uint8_t *)p = (uint8_t)i;
        }
        uint64_t elapsed = now_ns() - t0;

        fy_allocator_release_tag(a, tag);

        if (elapsed < r.min_ns) r.min_ns = elapsed;
        r.total_ns += elapsed;
        r.runs++;
    }
    return r;
}

/* ---- Metall driver: alloc-only (no individual frees, like durable) ---- */

static Result bench_metall_nofree(const char *dir)
{
    Result r;
    r.name = "metall (no-free)";

    /* create manager once; allocations accumulate across runs (like durable
       with its append-only bump, which also doesn't reclaim on tag release) */
    metall::manager mgr(metall::create_only, dir, METALL_INIT_SZ);

    /* warmup */
    for (int i = 0; i < WARMUP; i++) {
        size_t sz = SIZE_TABLE[i % SIZE_TABLE_LEN];
        void *p = mgr.allocate_aligned(sz, 8);
        if (p) *(volatile uint8_t *)p = (uint8_t)i;
    }

    for (int run = 0; run < RUNS; run++) {
        uint64_t t0 = now_ns();
        for (long i = 0; i < NALLOCS; i++) {
            size_t sz = SIZE_TABLE[i % SIZE_TABLE_LEN];
            void *p = mgr.allocate_aligned(sz, 8);
            if (!p) return r;
            *(volatile uint8_t *)p = (uint8_t)i;
        }
        uint64_t elapsed = now_ns() - t0;

        if (elapsed < r.min_ns) r.min_ns = elapsed;
        r.total_ns += elapsed;
        r.runs++;
    }
    return r;
}

/* ---- Metall driver: alloc + bulk-free per run (like malloc tag-release) ---- */

static Result bench_metall_withfree(const char *dir)
{
    Result r;
    r.name = "metall (with-free)";

    metall::manager mgr(metall::create_only, dir, METALL_INIT_SZ);

    std::vector<void *> ptrs(NALLOCS);

    /* warmup */
    {
        std::vector<void *> wp(WARMUP);
        for (int i = 0; i < WARMUP; i++) {
            size_t sz = SIZE_TABLE[i % SIZE_TABLE_LEN];
            wp[i] = mgr.allocate_aligned(sz, 8);
            if (wp[i]) *(volatile uint8_t *)wp[i] = (uint8_t)i;
        }
        for (int i = 0; i < WARMUP; i++) mgr.deallocate(wp[i]);
    }

    for (int run = 0; run < RUNS; run++) {
        uint64_t t0 = now_ns();
        for (long i = 0; i < NALLOCS; i++) {
            size_t sz = SIZE_TABLE[i % SIZE_TABLE_LEN];
            ptrs[i] = mgr.allocate_aligned(sz, 8);
            if (!ptrs[i]) return r;
            *(volatile uint8_t *)ptrs[i] = (uint8_t)i;
        }
        uint64_t t_alloc = now_ns() - t0;

        /* bulk free — not timed, but must happen to keep memory bounded */
        for (long i = 0; i < NALLOCS; i++) mgr.deallocate(ptrs[i]);

        if (t_alloc < r.min_ns) r.min_ns = t_alloc;
        r.total_ns += t_alloc;
        r.runs++;
    }
    return r;
}

/* ---- raw malloc: no-free ---- */

static Result bench_raw_malloc_nofree()
{
    Result r;
    r.name = "raw malloc (no-free)";

    /* warmup */
    for (int i = 0; i < WARMUP; i++) {
        size_t sz = SIZE_TABLE[i % SIZE_TABLE_LEN];
        void *p = malloc(sz);
        if (p) *(volatile uint8_t *)p = (uint8_t)i;
        /* intentionally not freed */
    }

    for (int run = 0; run < RUNS; run++) {
        uint64_t t0 = now_ns();
        for (long i = 0; i < NALLOCS; i++) {
            size_t sz = SIZE_TABLE[i % SIZE_TABLE_LEN];
            void *p = malloc(sz);
            if (!p) return r;
            *(volatile uint8_t *)p = (uint8_t)i;
        }
        uint64_t elapsed = now_ns() - t0;
        if (elapsed < r.min_ns) r.min_ns = elapsed;
        r.total_ns += elapsed;
        r.runs++;
    }
    return r;
}

/* ---- raw malloc: bulk-free per run ---- */

static Result bench_raw_malloc_withfree()
{
    Result r;
    r.name = "raw malloc (bulk-free)";

    std::vector<void *> ptrs(NALLOCS);

    /* warmup */
    {
        std::vector<void *> wp(WARMUP);
        for (int i = 0; i < WARMUP; i++) {
            size_t sz = SIZE_TABLE[i % SIZE_TABLE_LEN];
            wp[i] = malloc(sz);
            if (wp[i]) *(volatile uint8_t *)wp[i] = (uint8_t)i;
        }
        for (int i = 0; i < WARMUP; i++) free(wp[i]);
    }

    for (int run = 0; run < RUNS; run++) {
        uint64_t t0 = now_ns();
        for (long i = 0; i < NALLOCS; i++) {
            size_t sz = SIZE_TABLE[i % SIZE_TABLE_LEN];
            ptrs[i] = malloc(sz);
            if (!ptrs[i]) return r;
            *(volatile uint8_t *)ptrs[i] = (uint8_t)i;
        }
        uint64_t t_alloc = now_ns() - t0;

        for (long i = 0; i < NALLOCS; i++) free(ptrs[i]);

        if (t_alloc < r.min_ns) r.min_ns = t_alloc;
        r.total_ns += t_alloc;
        r.runs++;
    }
    return r;
}

/* ---- main ---- */

int main()
{
    printf("Allocator benchmark — libfyaml vs Metall\n");
    printf("  %ld allocs/run  x%d runs  size mix: 8,8,16,8,64,8,32,256,8,16  align=8\n\n",
           NALLOCS, RUNS);

    std::vector<Result> results;

    /* ---- libfyaml: malloc ---- */
    {
        auto *a = fy_allocator_create("malloc", nullptr);
        if (a) { results.push_back(bench_fy(a, "fy-malloc")); fy_allocator_destroy(a); }
    }

    /* ---- libfyaml: mremap ---- */
    {
        auto *a = fy_allocator_create("mremap", nullptr);
        if (a) { results.push_back(bench_fy(a, "fy-mremap")); fy_allocator_destroy(a); }
    }

    /* ---- libfyaml: linear in-place 128 MiB ---- */
    {
        const size_t bufsz = 128ULL << 20;
        void *buf = mmap(nullptr, bufsz, PROT_READ|PROT_WRITE,
                         MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
        if (buf != MAP_FAILED) {
            auto *a = fy_linear_allocator_create_in_place(buf, bufsz);
            if (a) results.push_back(bench_fy(a, "fy-linear"));
            munmap(buf, bufsz);
        }
    }

    /* ---- libfyaml: durable (16 MiB sparse chunks) ---- */
    {
        char *tmpdir = make_tmpdir();
        if (tmpdir) {
            struct fy_durable_allocator_cfg cfg{};
            cfg.dir         = tmpdir;
            cfg.region_base = DURABLE_BASE;
            cfg.region_size = DURABLE_REGION_SZ;
            cfg.chunk_size  = DURABLE_CHUNK_SZ;
            cfg.flags       = FY_DURABLE_ARENA_CREATE | FY_DURABLE_ARENA_SPARSE;

            auto *a = fy_allocator_create("durable", &cfg);
            if (a) {
                results.push_back(bench_fy(a, "fy-durable"));
                fy_allocator_destroy(a);
            } else {
                fprintf(stderr, "fy-durable: fixed VA 0x%llx unavailable, skipping\n",
                        (unsigned long long)DURABLE_BASE);
            }
            rm_rf(tmpdir);
            free(tmpdir);
        }
    }

    /* ---- Metall: no-free ---- */
    {
        char *tmpdir = make_tmpdir();
        if (tmpdir) {
            results.push_back(bench_metall_nofree(tmpdir));
            rm_rf(tmpdir);
            free(tmpdir);
        }
    }

    /* ---- Metall: with-free ---- */
    {
        char *tmpdir = make_tmpdir();
        if (tmpdir) {
            results.push_back(bench_metall_withfree(tmpdir));
            rm_rf(tmpdir);
            free(tmpdir);
        }
    }

    /* ---- raw malloc ---- */
    results.push_back(bench_raw_malloc_nofree());
    results.push_back(bench_raw_malloc_withfree());

    printf("Results:\n");
    for (auto &r : results) print_result(r);

    printf("\nNotes:\n");
    printf("  fy-linear/mremap : arena (no individual frees)\n");
    printf("  fy-durable       : persistent mmap, 16 MiB sparse chunks, no individual frees\n");
    printf("  metall (no-free) : Metall persistent mmap, allocate_aligned only, %zu MiB initial\n",
           METALL_INIT_SZ >> 20);
    printf("  metall (with-free): Metall allocate_aligned + bulk deallocate per run (alloc time only)\n");
    printf("  Metall default chunk size: %llu MiB\n", (unsigned long long)(1ULL << 21) >> 20);
    return 0;
}
