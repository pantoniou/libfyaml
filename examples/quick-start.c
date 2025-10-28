/*
 * quick-start.c - Quick start example for libfyaml
 *
 * Demonstrates:
 * - Parsing YAML from a file
 * - Extracting values using path-based scanf
 * - Modifying the document
 * - Emitting as YAML
 *
 * Copyright (c) 2019-2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>
#include <libfyaml.h>

int main(int argc, char *argv[])
{
	struct fy_document *fyd = NULL;
	unsigned int port;
	char hostname[256];
	int ret = EXIT_FAILURE;
	const char *input_file = (argc > 1) ? argv[1] : "config.yaml";

	// Parse YAML from string or file
	fyd = fy_document_build_from_file(NULL, input_file);
	if (!fyd) {
		fprintf(stderr, "Failed to parse %s\n", input_file);
		fprintf(stderr, "Note: You can create config.yaml or pass a file as argument\n");
		goto cleanup;
	}

	// Extract values using path-based scanf
	int count = fy_document_scanf(fyd,
		"/server/port %u "
		"/server/host %255s",
		&port, hostname);

	if (count != 2) {
		fprintf(stderr, "Failed to extract server configuration\n");
		fprintf(stderr, "Expected /server/port and /server/host in YAML\n");
		goto cleanup;
	}

	printf("Current configuration: %s:%u\n", hostname, port);

	// Modify document using path-based insertion
	if (fy_document_insert_at(fyd, "/server", FY_NT,
	    fy_node_buildf(fyd, "timeout: 30"))) {
		fprintf(stderr, "Failed to insert timeout setting\n");
		goto cleanup;
	}

	printf("Added timeout setting\n");

	// Emit as YAML
	printf("\nUpdated configuration:\n");
	if (fy_emit_document_to_fp(fyd, FYECF_SORT_KEYS, stdout)) {
		fprintf(stderr, "Failed to emit document\n");
		goto cleanup;
	}

	ret = EXIT_SUCCESS;

cleanup:
	fy_document_destroy(fyd);
	return ret;
}
