/*
 * intro-generic-update.c - Generic API example used by the introduction
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-generic.h>

int main(int argc, char *argv[])
{
	const char *input_file = argc > 1 ? argv[1] : "intro-config.yaml";
	fy_generic doc, updated, emitted;
	const char *host;
	long long port;

	doc = fy_parse_file(FYOPPF_DISABLE_DIRECTORY, input_file);
	host = fy_get(doc, "host", "");
	port = fy_get(doc, "port", 0LL);

	updated = fy_assoc(doc, "port", 8080);
	emitted = fy_emit(
		updated,
		FYOPEF_DISABLE_DIRECTORY |
		FYOPEF_MODE_YAML_1_2 |
		FYOPEF_STYLE_BLOCK,
		NULL);
	if (fy_generic_is_invalid(emitted)) {
		fprintf(stderr, "Failed to parse or emit %s\n", input_file);
		return EXIT_FAILURE;
	}

	printf("before: %s:%lld\n", host, port);
	printf("after:\n%s\n", fy_cast(emitted, ""));

	return EXIT_SUCCESS;
}
