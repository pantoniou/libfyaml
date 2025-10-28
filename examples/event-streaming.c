/*
 * event-streaming.c - Event-based streaming parser example
 *
 * Demonstrates:
 * - Event-based (streaming) YAML parsing
 * - Processing different event types
 * - Memory-efficient parsing of large files
 * - Getting values from scalar events
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
	struct fy_parser *fyp = NULL;
	struct fy_event *fye = NULL;
	const char *input_file = (argc > 1) ? argv[1] : "config.yaml";
	int indent = 0;
	int ret = EXIT_FAILURE;

	// Create parser
	fyp = fy_parser_create(NULL);
	if (!fyp) {
		fprintf(stderr, "Failed to create parser\n");
		return EXIT_FAILURE;
	}

	// Set input file
	if (fy_parser_set_input_file(fyp, input_file)) {
		fprintf(stderr, "Failed to set input file: %s\n", input_file);
		fprintf(stderr, "Note: Create config.yaml or pass file as argument\n");
		goto cleanup;
	}

	printf("Parsing events from: %s\n\n", input_file);

	// Process events
	while ((fye = fy_parser_parse(fyp)) != NULL) {
		enum fy_event_type type = fye->type;

		// Print indentation
		for (int i = 0; i < indent; i++)
			printf("  ");

		switch (type) {
		case FYET_STREAM_START:
			printf("STREAM-START\n");
			break;

		case FYET_STREAM_END:
			printf("STREAM-END\n");
			break;

		case FYET_DOCUMENT_START:
			printf("DOCUMENT-START\n");
			indent++;
			break;

		case FYET_DOCUMENT_END:
			indent--;
			printf("DOCUMENT-END\n");
			break;

		case FYET_MAPPING_START:
			printf("MAPPING-START\n");
			indent++;
			break;

		case FYET_MAPPING_END:
			indent--;
			printf("MAPPING-END\n");
			break;

		case FYET_SEQUENCE_START:
			printf("SEQUENCE-START\n");
			indent++;
			break;

		case FYET_SEQUENCE_END:
			indent--;
			printf("SEQUENCE-END\n");
			break;

		case FYET_SCALAR: {
			// Get the scalar value
			const char *value = fy_token_get_text0(fy_event_get_token(fye));
			printf("SCALAR: \"%s\"\n", value ? value : "(null)");
			break;
		}

		case FYET_ALIAS: {
			const char *anchor = fy_token_get_text0(fy_event_get_token(fye));
			printf("ALIAS: *%s\n", anchor ? anchor : "(null)");
			break;
		}

		default:
			printf("OTHER-EVENT (type=%d)\n", type);
			break;
		}

		fy_parser_event_free(fyp, fye);
	}

	printf("\nParsing completed successfully\n");
	ret = EXIT_SUCCESS;

cleanup:
	fy_parser_destroy(fyp);
	return ret;
}
