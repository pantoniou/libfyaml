/*
 * reflection-export-packed.c - Export packed reflection metadata from headers
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-reflection.h>

int main(int argc, char *argv[])
{
	const char *header_path = argc > 1 ? argv[1] : "reflection-config.h";
	const char *blob_path = argc > 2 ? argv[2] : "reflection-config.blob";
	struct fy_reflection *rfl = NULL;
	int rc = EXIT_FAILURE;

	rfl = fy_reflection_from_c_file_with_cflags(header_path, "", false, true, NULL);
	if (!rfl) {
		fprintf(stderr, "Unable to reflect %s\n", header_path);
		fprintf(stderr, "This example requires libfyaml built with libclang support\n");
		goto cleanup;
	}

	if (fy_reflection_to_packed_blob_file(rfl, blob_path) != 0) {
		fprintf(stderr, "Unable to write packed blob %s\n", blob_path);
		goto cleanup;
	}

	printf("Wrote packed reflection blob to %s\n", blob_path);
	rc = EXIT_SUCCESS;

cleanup:
	fy_reflection_destroy(rfl);
	return rc;
}
