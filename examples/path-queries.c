/*
 * path-queries.c - Path-based YAML queries example
 *
 * Demonstrates:
 * - Using YPATH expressions to query YAML
 * - scanf-style value extraction
 * - Single node lookup by path
 * - String comparison with nodes
 *
 * Copyright (c) 2019-2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>
#include <libfyaml.h>

int main(void)
{
	const char *yaml =
		"server:\n"
		"  host: localhost\n"
		"  port: 8080\n"
		"  ssl: true\n"
		"  max_connections: 100\n";

	struct fy_document *fyd = fy_document_build_from_string(NULL, yaml, FY_NT);
	if (!fyd) {
		fprintf(stderr, "Failed to parse YAML\n");
		return EXIT_FAILURE;
	}

	// Extract multiple values at once using scanf-style API
	char host[256];
	unsigned int port;
	int count = fy_document_scanf(fyd,
		"/server/host %255s "
		"/server/port %u",
		host, &port);

	if (count == 2) {
		printf("Server configuration:\n");
		printf("  Host: %s\n", host);
		printf("  Port: %u\n", port);
	} else {
		fprintf(stderr, "Failed to extract server configuration (got %d/2)\n", count);
	}

	// Query single node by path
	struct fy_node *ssl_node = fy_node_by_path(
		fy_document_root(fyd), "/server/ssl", FY_NT, FYNWF_DONT_FOLLOW);

	if (ssl_node) {
		// Compare node value with string
		if (fy_node_compare_string(ssl_node, "true", FY_NT) == 0) {
			printf("  SSL: enabled\n");
		} else {
			printf("  SSL: disabled\n");
		}
	}

	// Extract another value
	unsigned int max_conn;
	count = fy_document_scanf(fyd, "/server/max_connections %u", &max_conn);
	if (count == 1) {
		printf("  Max connections: %u\n", max_conn);
	}

	// Try to query a non-existent path
	struct fy_node *missing = fy_node_by_path(
		fy_document_root(fyd), "/server/timeout", FY_NT, FYNWF_DONT_FOLLOW);

	if (!missing) {
		printf("  Timeout: not configured (using default)\n");
	}

	fy_document_destroy(fyd);
	return EXIT_SUCCESS;
}
