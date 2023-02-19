/*
 * generic-literals.c - Generic runtime literals and builder-backed values
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-generic.h>

static void print_yaml(const char *title, fy_generic v)
{
	fy_generic emitted;
	const char *text;

	emitted = fy_emit(v,
			  FYOPEF_DISABLE_DIRECTORY |
			  FYOPEF_MODE_YAML_1_2 |
			  FYOPEF_STYLE_BLOCK,
			  NULL);
	if (fy_generic_is_invalid(emitted)) {
		fprintf(stderr, "Failed to emit %s\n", title);
		return;
	}

	text = fy_cast(emitted, "");
	printf("%s:\n%s\n", title, text);
}

int main(void)
{
	char storage[16384];
	struct fy_generic_builder *gb;
	fy_generic local_profile;
	fy_generic persistent_profile;
	fy_generic features;

	local_profile = fy_mapping(
		"name", "api",
		"server", fy_mapping(
			"host", "localhost",
			"port", 8080,
			"tls", true),
		"features", fy_sequence("http", "metrics", "admin"));

	gb = fy_generic_builder_create_in_place(
		FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER,
		NULL, storage, sizeof(storage));
	if (!gb) {
		fprintf(stderr, "Failed to create generic builder\n");
		return EXIT_FAILURE;
	}

	persistent_profile = fy_gb_mapping(
		gb,
		"name", "api",
		"server", fy_gb_mapping(
			gb,
			"host", "localhost",
			"port", 8080,
			"tls", true),
		"features", fy_gb_sequence(gb, "http", "metrics", "admin"));

	printf("Both constructions represent the same value tree: %s\n",
	       fy_compare(local_profile, persistent_profile) == 0 ? "yes" : "no");

	printf("service name: %s\n", fy_get(local_profile, "name", ""));
	printf("server host: %s\n",
	       fy_get(fy_get(local_profile, "server", fy_invalid), "host", ""));
	printf("server port: %lld\n",
	       fy_get(fy_get(local_profile, "server", fy_invalid), "port", 0LL));

	features = fy_get(local_profile, "features", fy_invalid);
	printf("feature count: %zu\n", fy_len(features));
	printf("first feature: %s\n", fy_get(features, 0, ""));

	print_yaml("stack-local profile", local_profile);
	print_yaml("builder-backed profile", persistent_profile);

	return EXIT_SUCCESS;
}
