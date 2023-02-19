/*
 * generic-transform.c - Parse, transform, and reduce with the generic API
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-generic.h>

static bool keep_even(struct fy_generic_builder *gb, fy_generic v)
{
	(void)gb;
	return (fy_cast(v, 0LL) % 2) == 0;
}

static fy_generic square_value(struct fy_generic_builder *gb, fy_generic v)
{
	long long n = fy_cast(v, 0LL);

	return fy_value(gb, n * n);
}

static fy_generic sum_value(struct fy_generic_builder *gb, fy_generic acc, fy_generic v)
{
	long long total = fy_cast(acc, 0LL);

	return fy_value(gb, total + fy_cast(v, 0LL));
}

static void print_yaml(const char *title, fy_generic v)
{
	fy_generic emitted;

	emitted = fy_emit(v,
			  FYOPEF_DISABLE_DIRECTORY |
			  FYOPEF_MODE_YAML_1_2 |
			  FYOPEF_STYLE_BLOCK,
			  NULL);
	if (fy_generic_is_invalid(emitted)) {
		fprintf(stderr, "Failed to emit %s\n", title);
		return;
	}

	printf("%s:\n%s\n", title, fy_cast(emitted, ""));
}

int main(void)
{
	static const char yaml[] =
		"name: analytics\n"
		"values: [1, 2, 3, 4, 5, 6]\n";
	char storage[32768];
	struct fy_generic_builder *gb;
	fy_generic doc, values, evens, squared, total, updated;

	gb = fy_generic_builder_create_in_place(
		FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER,
		NULL, storage, sizeof(storage));
	if (!gb) {
		fprintf(stderr, "Failed to create generic builder\n");
		return EXIT_FAILURE;
	}

	doc = fy_parse(gb, yaml,
		       FYOPPF_DISABLE_DIRECTORY | FYOPPF_INPUT_TYPE_STRING,
		       NULL);
	if (fy_generic_is_invalid(doc)) {
		fprintf(stderr, "Failed to parse YAML input\n");
		return EXIT_FAILURE;
	}

	values = fy_get(doc, "values", fy_invalid);
	if (fy_generic_is_invalid(values)) {
		fprintf(stderr, "Missing values sequence\n");
		return EXIT_FAILURE;
	}

	evens = fy_filter(gb, values, keep_even);
	squared = fy_map(gb, evens, square_value);
	total = fy_reduce(gb, squared, 0LL, sum_value);

	updated = fy_assoc(
		gb, doc,
		"evens", evens,
		"squared_evens", squared,
		"sum_of_squared_evens", total);

	printf("original sequence length: %zu\n", fy_len(values));
	printf("even sequence length: %zu\n", fy_len(evens));
	printf("squared sequence length: %zu\n", fy_len(squared));
	printf("sum of squared evens: %lld\n", fy_cast(total, 0LL));

	print_yaml("transformed document", updated);

	return EXIT_SUCCESS;
}
