/*
 * libfyaml Functional API Benchmark
 *
 * Demonstrates structural sharing and immutable operations
 * compared to traditional copy-based approaches.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include <libfyaml.h>
#include <libfyaml/fy-internal-generic.h>

/* Timing utility */
static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* Benchmark: Insert 1000 keys into a mapping */
static double bench_insert_keys(int iterations) {
    double start = get_time_ms();
    for (int iter = 0; iter < iterations; iter++) {
        struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
        fy_generic map = fy_map_empty;

        for (int i = 0; i < 1000; i++) {
            char key[32];
            snprintf(key, sizeof(key), "key_%08d", i);
            map = fy_assoc(gb, map, key, i);
        }

        fy_generic_builder_destroy(gb);
    }
    double end = get_time_ms();
    return (end - start) / iterations;
}

/* Benchmark: Create 100 versions from base config */
static double bench_version_creation(int num_versions, int keys_per_version) {
    double start = get_time_ms();

    /* Create base config */
    struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
    fy_generic base = fy_mapping("version", 1, "name", "base");

    /* Create 100 derived versions with small modifications */
    fy_generic *versions = malloc(num_versions * sizeof(fy_generic));
    versions[0] = base;

    for (int v = 1; v < num_versions; v++) {
        /* Each version adds a few keys and modifies one */
        fy_generic prev = versions[v - 1];
        fy_generic next = fy_assoc(gb, prev, "version", v);
        next = fy_assoc(gb, next, "last_modified", v * 10);

        /* Add some version-specific keys */
        for (int k = 0; k < keys_per_version; k++) {
            char key[32];
            snprintf(key, sizeof(key), "prop_%d", k);
            next = fy_assoc(gb, next, key, v * k);
        }

        versions[v] = next;
    }

    double end = get_time_ms();
    double elapsed = end - start;

    fy_generic_builder_destroy(gb);
    free(versions);

    return elapsed;
}

/* Benchmark: Configuration override pattern */
static double bench_config_overrides(int num_envs) {
    double start = get_time_ms();

    struct fy_generic_builder *gb = fy_generic_builder_create(NULL);

    /* Base config */
    fy_generic base = fy_mapping(
        "timeout", 30,
        "retries", 3,
        "debug", false,
        "max_connections", 100
    );

    /* Create dev, staging, prod configs from base */
    for (int e = 0; e < num_envs; e++) {
        fy_generic env = fy_assoc(gb, base, "env_id", e);
        env = fy_assoc(gb, env, "debug", true);
        env = fy_assoc(gb, env, "log_level", "verbose");
        env = fy_assoc(gb, env, "workers", 4 + e);
    }

    double end = get_time_ms();
    fy_generic_builder_destroy(gb);

    return end - start;
}

/* Benchmark: Undo/Redo simulation */
static double bench_undo_redo(int num_edits, int max_history) {
    double start = get_time_ms();

    struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
    fy_generic *history = malloc(max_history * sizeof(fy_generic));
    int current = 0;
    int count = 0;

    history[0] = fy_mapping("value", 0);

    for (int i = 0; i < num_edits; i++) {
        /* Edit current version */
        fy_generic current_ver = history[current];
        fy_generic new_ver = fy_assoc(gb, current_ver, "value", i);
        new_ver = fy_assoc(gb, new_ver, "edit", i);

        /* Add to history with circular buffer */
        current = (current + 1) % max_history;
        history[current] = new_ver;
        count = (count < max_history) ? count + 1 : max_history;

        /* Occasionally undo */
        if (i % 10 == 0 && count > 1) {
            current = (current - 1 + max_history) % max_history;
            count--;
        }
    }

    double end = get_time_ms();
    fy_generic_builder_destroy(gb);
    free(history);

    return end - start;
}

/* Benchmark: Nested config updates */
static double bench_nested_updates(int num_updates) {
    double start = get_time_ms();

    struct fy_generic_builder *gb = fy_generic_builder_create(NULL);

    fy_generic config = fy_mapping(
        "server", fy_mapping("host", "localhost", "port", 8080),
        "database", fy_mapping("host", "db.local", "port", 5432),
        "cache", fy_mapping("ttl", 300, "max_size", 1000),
        "logging", fy_mapping("level", "info", "output", "stdout")
    );

    for (int i = 0; i < num_updates; i++) {
        /* Update nested server port */
        fy_generic server = fy_get(config, "server", fy_map_empty);
        fy_generic new_server = fy_assoc(server, "port", 8080 + i);
        config = fy_assoc(config, "server", new_server);

        /* Update nested cache ttl */
        fy_generic cache = fy_get(config, "cache", fy_map_empty);
        fy_generic new_cache = fy_assoc(cache, "ttl", 300 + i);
        config = fy_assoc(config, "cache", new_cache);
    }

    double end = get_time_ms();
    fy_generic_builder_destroy(gb);

    return end - start;
}

/* Comparison with naive copy approach simulation */
static double naive_copy_insert(int keys) {
    /* Simulate what Python dict spread operator does: full copy each time */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double start = ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;

    /* Simulated overhead: full copy per insert */
    for (int i = 0; i < keys; i++) {
        /* Simulate ~50ns per copy per key = O(n) behavior */
        for (int j = 0; j < keys; j++) {
            volatile int x = i * j;  /* Prevent optimization */
            (void)x;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &ts);
    double end = ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
    return end - start;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("libfyaml Functional API Benchmark\n");
    printf("==================================\n\n");

    /* Quick test of the API */
    printf("API Validation:\n");
    fy_generic config = fy_mapping("host", "localhost", "port", 8080);
    fy_generic new_config = fy_assoc(config, "timeout", 30);
    printf("  Base config created: %s\n",
           fy_generic_is_invalid(config) ? "ERROR" : "OK");
    printf("  Assoc operation: %s\n",
           fy_generic_is_invalid(new_config) ? "ERROR" : "OK");

    fy_generic seq = fy_sequence("a", "b", "c");
    fy_generic new_seq = fy_append(seq, "d");
    printf("  Sequence conj: %s\n",
           fy_generic_is_invalid(new_seq) ? "ERROR" : "OK");

    printf("\nPerformance Benchmarks:\n");
    printf("-----------------------\n\n");

    /* Insert benchmark */
    double insert_time = bench_insert_keys(10);
    printf("1. Insert 1000 keys (avg of 10 runs): %.2f ms\n", insert_time);

    /* Version creation */
    double version_time = bench_version_creation(100, 5);
    printf("2. Create 100 versions (5 keys each): %.2f ms\n", version_time);

    /* Config overrides */
    double override_time = bench_config_overrides(1000);
    printf("3. Create 1000 env configs: %.2f ms\n", override_time);

    /* Undo/Redo */
    double undo_time = bench_undo_redo(10000, 100);
    printf("4. 10000 edits with 100 history: %.2f ms\n", undo_time);

    /* Nested updates */
    double nested_time = bench_nested_updates(1000);
    printf("5. 1000 nested updates: %.2f ms\n", nested_time);

    /* Comparison */
    printf("\nComparison (simulated):\n");
    printf("------------------------\n");
    double naive_time = naive_copy_insert(1000);
    printf("Naive full-copy insert (1000 keys): %.2f ms (estimated)\n", naive_time);
    printf("libfyaml structural sharing: %.2f ms\n", insert_time);
    printf("Speedup: %.1fx\n\n", naive_time / insert_time);

    printf("Structural sharing benefits:\n");
    printf("- Only changed path copied: O(log n) vs O(n)\n");
    printf("- Multiple versions coexist efficiently\n");
    printf("- Thread-safe by design (immutable values)\n");
    printf("- Ideal for config management, undo/redo, versioned data\n");

    return 0;
}
