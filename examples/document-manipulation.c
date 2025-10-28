/*
 * document-manipulation.c - Document manipulation example
 *
 * Demonstrates:
 * - Reading values from a YAML document
 * - Modifying existing values
 * - Adding new fields to the document
 * - Emitting with sorted keys
 *
 * Copyright (c) 2019-2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libfyaml.h>

int main(int argc, char *argv[])
{
	struct fy_document *fyd = NULL;
	const char *input_file = (argc > 1) ? argv[1] : "invoice.yaml";
	unsigned int invoice_nr;
	char given_name[256];
	int ret = EXIT_FAILURE;

	// Create a default invoice if file doesn't exist
	const char *default_invoice =
		"invoice: 34843\n"
		"date: 2001-01-23\n"
		"bill-to:\n"
		"  given: Chris\n"
		"  family: Dumars\n"
		"  address:\n"
		"    lines: |\n"
		"      458 Walkman Dr.\n"
		"      Suite #292\n"
		"product:\n"
		"  - sku: BL394D\n"
		"    quantity: 4\n"
		"    description: Basketball\n"
		"    price: 450.00\n";

	// Try to load from file, fall back to default
	fyd = fy_document_build_from_file(NULL, input_file);
	if (!fyd) {
		fprintf(stderr, "Note: Creating document from default data\n");
		fprintf(stderr, "      (Create invoice.yaml or pass file as argument)\n\n");
		fyd = fy_document_build_from_string(NULL, default_invoice, FY_NT);
		if (!fyd) {
			fprintf(stderr, "Failed to create document\n");
			goto cleanup;
		}
	}

	// Read current invoice number and customer name
	int count = fy_document_scanf(fyd,
		"/invoice %u "
		"/bill-to/given %255s",
		&invoice_nr, given_name);

	if (count != 2) {
		fprintf(stderr, "Failed to extract invoice data\n");
		goto cleanup;
	}

	printf("Processing invoice #%u for %s\n\n", invoice_nr, given_name);

	// Update invoice number (replaces existing value)
	if (fy_document_insert_at(fyd, "/invoice", FY_NT,
	    fy_node_buildf(fyd, "%u", invoice_nr + 1))) {
		fprintf(stderr, "Failed to update invoice number\n");
		goto cleanup;
	}
	printf("Updated invoice number to %u\n", invoice_nr + 1);

	// Add spouse field to bill-to
	if (fy_document_insert_at(fyd, "/bill-to", FY_NT,
	    fy_node_buildf(fyd, "spouse: Jane"))) {
		fprintf(stderr, "Failed to add spouse\n");
		goto cleanup;
	}
	printf("Added spouse information\n");

	// Add delivery address
	if (fy_document_insert_at(fyd, "/bill-to", FY_NT,
	    fy_node_buildf(fyd,
		"delivery-address:\n"
		"  street: 123 Main St\n"
		"  city: Springfield\n"
		"  state: IL\n"
		"  zip: 62701\n"))) {
		fprintf(stderr, "Failed to add delivery address\n");
		goto cleanup;
	}
	printf("Added delivery address\n");

	// Emit updated document with sorted keys
	printf("\n--- Updated Invoice ---\n");
	if (fy_emit_document_to_fp(fyd, FYECF_DEFAULT | FYECF_SORT_KEYS, stdout)) {
		fprintf(stderr, "Failed to emit document\n");
		goto cleanup;
	}

	ret = EXIT_SUCCESS;

cleanup:
	fy_document_destroy(fyd);
	return ret;
}
