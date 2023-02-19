/*
 * intro-reflection-update.c - Reflection example used by the introduction
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-reflection.h>

#include "reflection-intro-config.h"

static struct fy_parser *make_parser_from_file(const char *path)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_QUIET };
	struct fy_parser *fyp;

	fyp = fy_parser_create(&cfg);
	if (!fyp)
		return NULL;
	if (fy_parser_set_input_file(fyp, path) != 0) {
		fy_parser_destroy(fyp);
		return NULL;
	}

	return fyp;
}

static int emit_config(struct fy_type_context *ctx, const struct server_config *cfg)
{
	struct fy_emitter_cfg emit_cfg = { .flags = FYECF_DEFAULT };
	struct fy_emitter *emit;
	int rc;

	emit = fy_emitter_create(&emit_cfg);
	if (!emit)
		return -1;

	rc = fy_type_context_emit(
		ctx, emit, cfg,
		FYTCEF_SS | FYTCEF_DS | FYTCEF_DE | FYTCEF_SE);
	fy_emitter_destroy(emit);

	return rc;
}

int main(int argc, char *argv[])
{
	const char *header_path = argc > 1 ? argv[1] : "reflection-intro-config.h";
	const char *input_file = argc > 2 ? argv[2] : "intro-config.yaml";
	struct fy_reflection *rfl = NULL;
	struct fy_type_context_cfg ctx_cfg = { 0 };
	struct fy_type_context *ctx = NULL;
	struct fy_parser *fyp = NULL;
	struct server_config *cfg = NULL;
	int rc = EXIT_FAILURE;

	rfl = fy_reflection_from_c_file_with_cflags(header_path, "", false, true, NULL);
	if (!rfl) {
		fprintf(stderr, "Unable to reflect %s\n", header_path);
		fprintf(stderr, "This example requires libfyaml built with libclang support\n");
		goto cleanup;
	}

	ctx_cfg.rfl = rfl;
	ctx_cfg.entry_type = "struct server_config";
	ctx = fy_type_context_create(&ctx_cfg);
	if (!ctx) {
		fprintf(stderr, "Unable to create type context\n");
		goto cleanup;
	}

	fyp = make_parser_from_file(input_file);
	if (!fyp) {
		fprintf(stderr, "Unable to parse %s\n", input_file);
		goto cleanup;
	}

	if (fy_type_context_parse(ctx, fyp, (void **)&cfg) != 0 || !cfg) {
		fprintf(stderr, "Unable to build typed data from %s\n", input_file);
		goto cleanup;
	}

	printf("before: %s:%d\n", cfg->host, cfg->port);
	cfg->port = 8080;

	printf("after:\n");
	if (emit_config(ctx, cfg) != 0) {
		fprintf(stderr, "Unable to emit typed configuration\n");
		goto cleanup;
	}

	rc = EXIT_SUCCESS;

cleanup:
	if (cfg)
		fy_type_context_free_data(ctx, cfg);
	fy_parser_destroy(fyp);
	fy_type_context_destroy(ctx);
	fy_reflection_destroy(rfl);

	return rc;
}
