/*
 * intro-core-update.c - Core document API example used by the introduction
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>

#include <libfyaml.h>

int main(int argc, char *argv[])
{
	const char *input_file = argc > 1 ? argv[1] : "intro-config.yaml";
	struct fy_document *fyd = NULL;
	char host[256];
	unsigned int port;
	int count;
	int rc = EXIT_FAILURE;

	fyd = fy_document_build_from_file(NULL, input_file);
	if (!fyd) {
		fprintf(stderr, "Failed to parse %s\n", input_file);
		goto cleanup;
	}

	count = fy_document_scanf(fyd, "/host %255s /port %u", host, &port);
	if (count != 2) {
		fprintf(stderr, "Failed to read host/port from %s\n", input_file);
		goto cleanup;
	}

	printf("before: %s:%u\n", host, port);

	if (fy_document_insert_at(
		    fyd, "/port", FY_NT,
		    fy_node_buildf(fyd, "8080"))) {
		fprintf(stderr, "Failed to update /port\n");
		goto cleanup;
	}

	printf("after:\n");
	if (fy_emit_document_to_fp(fyd, FYECF_DEFAULT, stdout)) {
		fprintf(stderr, "Failed to emit updated document\n");
		goto cleanup;
	}

	rc = EXIT_SUCCESS;

cleanup:
	fy_document_destroy(fyd);
	return rc;
}
