/*
 * basic-parsing.c - Basic YAML parsing example
 *
 * Demonstrates:
 * - Parsing YAML from a file
 * - Basic error handling
 * - Emitting in different output modes
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
	const char *input_file = (argc > 1) ? argv[1] : "config.yaml";

	// Parse YAML from file
	fyd = fy_document_build_from_file(NULL, input_file);
	if (!fyd) {
		fprintf(stderr, "Failed to parse YAML from %s\n", input_file);
		return EXIT_FAILURE;
	}

	printf("Successfully parsed: %s\n\n", input_file);

	// Emit as flow-oneline (compact format)
	printf("Compact format:\n");
	fy_emit_document_to_fp(fyd, FYECF_MODE_FLOW_ONELINE, stdout);
	printf("\n\n");

	// Emit as block (standard YAML)
	printf("Block format:\n");
	fy_emit_document_to_fp(fyd, FYECF_MODE_BLOCK, stdout);
	printf("\n");

	// Emit as JSON
	printf("JSON format:\n");
	fy_emit_document_to_fp(fyd, FYECF_MODE_JSON, stdout);
	printf("\n");

	fy_document_destroy(fyd);

	return EXIT_SUCCESS;
}
