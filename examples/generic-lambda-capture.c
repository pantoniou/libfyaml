/*
 * generic-lambda-capture.c - Generic lambda operations with local scope capture
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

#ifndef FY_HAVE_LAMBDAS
int main(void)
{
	printf("This build does not expose the generic lambda helpers.\n");
	return EXIT_SUCCESS;
}
#else
int main(void)
{
	char storage[65536];
	struct fy_generic_builder *gb;
	fy_generic scores;
	fy_generic selected;
	fy_generic annotated;
	fy_generic adjusted_total;
	fy_generic summary;
	const int pass_mark = 80;
	const int curve_bonus = 3;
	const int per_record_fee = 1;
	const char *cohort_name = "alpha";
	const char *label_prefix = "qualified";

	gb = fy_generic_builder_create_in_place(
		FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER,
		NULL, storage, sizeof(storage));
	if (!gb) {
		fprintf(stderr, "Failed to create generic builder\n");
		return EXIT_FAILURE;
	}

	scores = fy_sequence(72, 88, 91, 67, 95, 84);

	selected = fy_filter_lambda(
		gb, scores,
		fy_cast(v, 0) >= pass_mark);

	annotated = fy_map_lambda(
		gb, selected,
		fy_mapping(gb,
			"label", fy_stringf("%s-%s-%03d",
				cohort_name, label_prefix, fy_cast(v, 0)),
			"original", v,
			"adjusted", fy_cast(v, 0) + curve_bonus,
			"cohort", cohort_name));

	adjusted_total = fy_reduce_lambda(
		gb, annotated, 0,
		fy_value(gb,
			fy_cast(acc, 0) +
			fy_get(v, "adjusted", 0) +
			per_record_fee));

	summary = fy_mapping(
		"captures", fy_mapping(
			"pass_mark", pass_mark,
			"curve_bonus", curve_bonus,
			"per_record_fee", per_record_fee,
			"cohort_name", cohort_name,
			"label_prefix", label_prefix),
		"input_scores", scores,
		"selected_scores", selected,
		"annotated_scores", annotated,
		"adjusted_total", adjusted_total);

	printf("captured pass_mark: %d\n", pass_mark);
	printf("captured curve_bonus: %d\n", curve_bonus);
	printf("captured per_record_fee: %d\n", per_record_fee);
	printf("captured cohort_name: %s\n", cohort_name);
	printf("captured label_prefix: %s\n", label_prefix);
	printf("selected count: %zu\n", fy_len(selected));
	printf("first generated label: %s\n",
	       fy_get(fy_get(annotated, 0, fy_invalid), "label", ""));
	printf("adjusted total: %d\n", fy_cast(adjusted_total, 0));

	print_yaml("lambda capture summary", summary);

	return EXIT_SUCCESS;
}
#endif
