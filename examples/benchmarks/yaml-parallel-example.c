/*
 * Parallel map/reduce example with libfyaml generics
 *
 * This demonstrates what you'd compare against Clojure's pmap/reduce:
 * 1. Parse YAML into generics
 * 2. Parallel map over collection (process items concurrently)
 * 3. Reduce results back
 * 4. Emit YAML
 *
 * Compile: gcc -O2 -pthread yaml-parallel-example.c -lfyaml -o yaml-parallel
 */

#include <libfyaml.h>
#include <libfyaml/fy-internal-generic.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#define MAX_THREADS 8

struct map_task {
	fy_generic input;
	fy_generic output;
	struct fy_generic_builder *gb;
	int index;
};

/* Example map function: add "processed: true" field to each mapping */
static void *process_item(void *arg)
{
	struct map_task *task = arg;
	fy_generic result;

	/* Process item - add a field */
	if (fy_generic_is_mapping(task->input)) {
		result = fy_gb_mapping(task->gb,
			"original", task->input,
			"processed", true,
			"thread_id", (int64_t)pthread_self());
	} else {
		result = task->input;  /* pass through */
	}

	task->output = result;
	return NULL;
}

/* Parallel map over sequence */
static fy_generic parallel_map(struct fy_generic_builder *gb, fy_generic seq)
{
	pthread_t threads[MAX_THREADS];
	struct map_task tasks[MAX_THREADS];
	size_t len, i, n_threads;
	fy_generic result;

	if (!fy_generic_is_sequence(seq))
		return seq;

	len = fy_len(seq);
	if (len == 0)
		return seq;

	/* Create tasks for each item */
	n_threads = len < MAX_THREADS ? len : MAX_THREADS;

	for (i = 0; i < len; i++) {
		tasks[i].input = fy_get(seq, i, fy_invalid);
		tasks[i].output = fy_invalid;
		tasks[i].gb = gb;
		tasks[i].index = i;
	}

	/* Launch threads */
	for (i = 0; i < len && i < MAX_THREADS; i++) {
		pthread_create(&threads[i], NULL, process_item, &tasks[i]);
	}

	/* Wait for completion */
	for (i = 0; i < len && i < MAX_THREADS; i++) {
		pthread_join(threads[i], NULL);
	}

	/* Build result sequence */
	fy_generic *items = alloca(len * sizeof(fy_generic));
	for (i = 0; i < len; i++) {
		items[i] = tasks[i].output;
	}

	return fy_gb_sequence_create(gb, len, items);
}

int main(int argc, char *argv[])
{
	struct fy_generic_builder *gb = NULL;
	struct fy_generic_builder_cfg cfg;
	fy_generic data, processed;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <yaml-file>\n", argv[0]);
		return 1;
	}

	/* Create builder with dedup for parallel processing */
	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED;
	gb = fy_generic_builder_create(&cfg);

	if (!gb) {
		fprintf(stderr, "Failed to create builder\n");
		return 1;
	}

	/* Parse YAML file directly to generic */
	fy_generic doc = fy_gb_parse_file(gb, 0, argv[1]);
	if (fy_generic_is_invalid(doc)) {
		fprintf(stderr, "Failed to parse YAML file\n");
		fy_generic_builder_destroy(gb);
		return 1;
	}

	/* Extract root content from document wrapper */
	data = fy_get(doc, "root", fy_invalid);
	if (fy_generic_is_invalid(data)) {
		fprintf(stderr, "Failed to get root from document\n");
		fy_generic_builder_destroy(gb);
		return 1;
	}

	/* Parallel map */
	printf("Processing %zu items in parallel...\n", fy_len(data));
	processed = parallel_map(gb, data);

	/* Emit result to stdout */
	fy_generic_emit_default(processed);

	fy_generic_builder_destroy(gb);
	return 0;
}
