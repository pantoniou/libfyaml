/*
 * build-from-scratch.c - Building YAML documents programmatically
 *
 * Demonstrates:
 * - Creating empty documents
 * - Building nodes using printf-style formatting
 * - Setting document root
 * - Adding fields programmatically
 * - Emitting as both JSON and YAML
 *
 * Copyright (c) 2019-2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <libfyaml.h>

int main(void)
{
	struct fy_document *fyd = NULL;
	struct fy_node *root = NULL;
	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	char timestamp[32];
	int ret = EXIT_FAILURE;

	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

	// Create empty document
	fyd = fy_document_create(NULL);
	if (!fyd) {
		fprintf(stderr, "Failed to create document\n");
		return EXIT_FAILURE;
	}

	// Build root mapping using printf-style formatting
	root = fy_node_buildf(fyd,
		"application: MyApp\n"
		"version: %d.%d.%d\n"
		"build_date: %s\n"
		"settings:\n"
		"  debug: %s\n"
		"  max_connections: %d\n"
		"  allowed_hosts:\n"
		"    - localhost\n"
		"    - 127.0.0.1\n",
		1, 2, 3,
		timestamp,
		"true",
		100);

	if (!root) {
		fprintf(stderr, "Failed to build root node\n");
		goto cleanup;
	}

	// Set as document root
	fy_document_set_root(fyd, root);

	// Add more fields programmatically
	if (fy_document_insert_at(fyd, "/settings", FY_NT,
	    fy_node_buildf(fyd, "log_level: info"))) {
		fprintf(stderr, "Failed to add log_level\n");
		goto cleanup;
	}

	if (fy_document_insert_at(fyd, "/settings", FY_NT,
	    fy_node_buildf(fyd,
		"database:\n"
		"  host: localhost\n"
		"  port: 5432\n"
		"  name: myapp_db\n"))) {
		fprintf(stderr, "Failed to add database config\n");
		goto cleanup;
	}

	// Add feature flags
	if (fy_document_insert_at(fyd, "/", FY_NT,
	    fy_node_buildf(fyd,
		"features:\n"
		"  authentication: enabled\n"
		"  api_v2: disabled\n"
		"  metrics: enabled\n"))) {
		fprintf(stderr, "Failed to add features\n");
		goto cleanup;
	}

	// Output as JSON
	printf("=== JSON Output ===\n");
	if (fy_emit_document_to_fp(fyd, FYECF_MODE_JSON, stdout)) {
		fprintf(stderr, "Failed to emit as JSON\n");
		goto cleanup;
	}
	printf("\n\n");

	// Output as YAML with sorted keys
	printf("=== YAML Output (sorted) ===\n");
	if (fy_emit_document_to_fp(fyd, FYECF_MODE_BLOCK | FYECF_SORT_KEYS, stdout)) {
		fprintf(stderr, "Failed to emit as YAML\n");
		goto cleanup;
	}

	ret = EXIT_SUCCESS;

cleanup:
	fy_document_destroy(fyd);
	return ret;
}
