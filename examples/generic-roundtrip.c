/*
 * generic-roundtrip.c - Schema-sensitive parse and normalized emit
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-generic.h>

static const char *type_name(fy_generic v)
{
	switch (fy_get_type(v)) {
	case FYGT_NULL:
		return "null";
	case FYGT_BOOL:
		return "bool";
	case FYGT_INT:
		return "int";
	case FYGT_FLOAT:
		return "float";
	case FYGT_STRING:
		return "string";
	case FYGT_SEQUENCE:
		return "sequence";
	case FYGT_MAPPING:
		return "mapping";
	case FYGT_INDIRECT:
		return "indirect";
	case FYGT_ALIAS:
		return "alias";
	default:
		return "invalid";
	}
}

static void describe_enabled(const char *label, fy_generic doc)
{
	fy_generic enabled = fy_get(doc, "enabled", fy_invalid);

	printf("%s: enabled resolves to %s", label, type_name(enabled));
	if (fy_generic_is_bool_type(enabled))
		printf(" (%s)", fy_cast(enabled, false) ? "true" : "false");
	else if (fy_generic_is_string(enabled))
		printf(" (%s)", fy_cast(enabled, ""));
	printf("\n");
}

int main(void)
{
	static const char yaml[] =
		"enabled: yes\n"
		"ratio: 3.14\n"
		"count: 10\n";
	fy_generic yaml12;
	fy_generic pyyaml;
	fy_generic emitted;

	yaml12 = fy_parse(
		yaml,
		FYOPPF_DISABLE_DIRECTORY |
		FYOPPF_INPUT_TYPE_STRING |
		FYOPPF_MODE_YAML_1_2,
		NULL);
	pyyaml = fy_parse(
		yaml,
		FYOPPF_DISABLE_DIRECTORY |
		FYOPPF_INPUT_TYPE_STRING |
		FYOPPF_MODE_YAML_1_1_PYYAML,
		NULL);

	if (fy_generic_is_invalid(yaml12) || fy_generic_is_invalid(pyyaml)) {
		fprintf(stderr, "Failed to parse one of the schema variants\n");
		return EXIT_FAILURE;
	}

	describe_enabled("YAML 1.2", yaml12);
	describe_enabled("YAML 1.1 / PyYAML-compatible", pyyaml);

	emitted = fy_emit(
		pyyaml,
		FYOPEF_DISABLE_DIRECTORY |
		FYOPEF_MODE_YAML_1_2 |
		FYOPEF_STYLE_BLOCK,
		NULL);
	if (fy_generic_is_invalid(emitted)) {
		fprintf(stderr, "Failed to emit normalized YAML\n");
		return EXIT_FAILURE;
	}

	printf("\nNormalized emit:\n%s\n", fy_cast(emitted, ""));

	return EXIT_SUCCESS;
}
