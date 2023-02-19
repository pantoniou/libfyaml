/*
 * generic-adoption-bridge.c - Show the same value tree from YAML and literals
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-generic.h>

int main(void)
{
	static const char yaml[] =
		"service:\n"
		"  name: api\n"
		"  ports: [8080, 8443]\n"
		"  labels:\n"
		"    tier: edge\n"
		"    owner: platform\n";
	char storage[16384];
	struct fy_generic_builder *gb;
	fy_generic from_yaml;
	fy_generic from_literals;
	fy_generic service;
	fy_generic ports;

	gb = fy_generic_builder_create_in_place(
		FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER,
		NULL, storage, sizeof(storage));
	if (!gb) {
		fprintf(stderr, "Failed to create generic builder\n");
		return EXIT_FAILURE;
	}

	from_yaml = fy_parse(
		gb,
		yaml,
		FYOPPF_DISABLE_DIRECTORY | FYOPPF_INPUT_TYPE_STRING,
		NULL);
	from_literals = fy_gb_mapping(
		gb,
		"service", fy_gb_mapping(
			gb,
			"name", "api",
			"ports", fy_gb_sequence(gb, 8080, 8443),
			"labels", fy_gb_mapping(
				gb,
				"tier", "edge",
				"owner", "platform")));

	if (fy_generic_is_invalid(from_yaml)) {
		fprintf(stderr, "Failed to parse YAML document\n");
		return EXIT_FAILURE;
	}

	printf("Parsed YAML and literal construction match: %s\n",
	       fy_compare(from_yaml, from_literals) == 0 ? "yes" : "no");

	service = fy_get(from_literals, "service", fy_invalid);
	ports = fy_get(service, "ports", fy_invalid);

	printf("service.name -> %s\n", fy_get(service, "name", ""));
	printf("service.ports[0] -> %lld\n", fy_get(ports, 0, 0LL));
	printf("service.labels.owner -> %s\n",
	       fy_get(fy_get(service, "labels", fy_invalid), "owner", ""));

	printf("\nThis is the same object shape the Python binding exposes as\n");
	printf("FyGeneric wrappers over dict/list/scalar values.\n");

	return EXIT_SUCCESS;
}
