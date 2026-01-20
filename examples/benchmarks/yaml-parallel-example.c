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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
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
	char buf[4096];
	struct fy_generic_builder *gb_local;
	fy_generic result;

	/* Create thread-local builder with parent for dedup */
	gb_local = fy_generic_builder_create_in_place(
		FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED,
		task->gb,
		buf, sizeof(buf));

	if (!gb_local) {
		task->output = fy_invalid;
		return NULL;
	}

	/* Process item - add a field */
	if (fy_generic_is_mapping(task->input)) {
		result = fy_gb_mapping(gb_local,
			"original", task->input,
			"processed", true,
			"thread_id", pthread_self());
	} else {
		result = task->input;  /* pass through */
	}

	/* Export back to parent builder */
	task->output = fy_generic_builder_export(gb_local, result);

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

	return fy_gb_sequence_alloca(gb, items, len);
}

int main(int argc, char *argv[])
{
	struct fy_document *fyd = NULL;
	struct fy_generic_builder *gb = NULL;
	fy_generic data, processed;
	char *output;
	char buf[1024 * 1024];  /* 1MB builder buffer */

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <yaml-file>\n", argv[0]);
		return 1;
	}

	/* Parse YAML document */
	fyd = fy_document_build_from_file(NULL, argv[1]);
	if (!fyd) {
		fprintf(stderr, "Failed to parse YAML\n");
		return 1;
	}

	/* Create builder with dedup for parallel processing */
	gb = fy_generic_builder_create_in_place(
		FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED,
		NULL,
		buf, sizeof(buf));

	if (!gb) {
		fprintf(stderr, "Failed to create builder\n");
		fy_document_destroy(fyd);
		return 1;
	}

	/* Convert document to generic */
	data = fy_document_root_to_generic(fyd, gb);
	if (!fy_generic_is_valid(data)) {
		fprintf(stderr, "Failed to convert to generic\n");
		fy_document_destroy(fyd);
		return 1;
	}

	/* Parallel map */
	printf("Processing %zu items in parallel...\n", fy_len(data));
	processed = parallel_map(gb, data);

	/* Emit result */
	output = fy_emit_generic_to_string(processed, 0);
	if (output) {
		printf("%s", output);
		free(output);
	}

	fy_document_destroy(fyd);
	return 0;
}
