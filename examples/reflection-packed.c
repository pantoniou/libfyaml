/*
 * reflection-packed.c - Runtime typed parse/emit from packed reflection metadata
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-reflection.h>

#include "reflection-config.h"

static char *read_text_file(const char *path)
{
	FILE *fp;
	long length;
	char *buf;

	fp = fopen(path, "rb");
	if (!fp)
		return NULL;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return NULL;
	}
	length = ftell(fp);
	if (length < 0) {
		fclose(fp);
		return NULL;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return NULL;
	}

	buf = malloc((size_t)length + 1);
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	if (fread(buf, 1, (size_t)length, fp) != (size_t)length) {
		fclose(fp);
		free(buf);
		return NULL;
	}
	buf[length] = '\0';
	fclose(fp);

	return buf;
}

static struct fy_parser *make_parser_from_string(const char *yaml)
{
	struct fy_parse_cfg cfg = { .flags = FYPCF_QUIET };
	struct fy_parser *fyp;

	fyp = fy_parser_create(&cfg);
	if (!fyp)
		return NULL;
	if (fy_parser_set_string(fyp, yaml, FY_NT) != 0) {
		fy_parser_destroy(fyp);
		return NULL;
	}

	return fyp;
}

static int emit_config(struct fy_type_context *ctx, const struct service_config *cfg)
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
	const char *blob_path = argc > 1 ? argv[1] : "reflection-config.blob";
	const char *yaml_path = argc > 2 ? argv[2] : "reflection-config.yaml";
	struct fy_reflection *rfl = NULL;
	struct fy_type_context_cfg ctx_cfg = { 0 };
	struct fy_type_context *ctx = NULL;
	struct fy_parser *fyp = NULL;
	struct service_config *cfg = NULL;
	char *yaml = NULL;
	int rc = EXIT_FAILURE;
	int i;

	yaml = read_text_file(yaml_path);
	if (!yaml) {
		fprintf(stderr, "Unable to read %s\n", yaml_path);
		goto cleanup;
	}

	rfl = fy_reflection_from_packed_blob_file(blob_path, NULL);
	if (!rfl) {
		fprintf(stderr, "Unable to load packed reflection blob %s\n", blob_path);
		fprintf(stderr, "Generate it first with reflection-export-packed\n");
		goto cleanup;
	}

	ctx_cfg.rfl = rfl;
	ctx_cfg.entry_type = "struct service_config";
	ctx = fy_type_context_create(&ctx_cfg);
	if (!ctx) {
		fprintf(stderr, "Unable to create type context\n");
		goto cleanup;
	}

	fyp = make_parser_from_string(yaml);
	if (!fyp) {
		fprintf(stderr, "Unable to create parser for %s\n", yaml_path);
		goto cleanup;
	}

	if (fy_type_context_parse(ctx, fyp, (void **)&cfg) != 0 || !cfg) {
		fprintf(stderr, "Unable to parse typed data from %s\n", yaml_path);
		goto cleanup;
	}

	printf("listen: %s\n", cfg->listen);
	printf("ports parsed: %d\n", cfg->count);
	for (i = 0; i < cfg->count; i++)
		printf("  %s -> %d\n", cfg->ports[i].name, cfg->ports[i].port);

	printf("\nRound-tripped YAML:\n");
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
	free(yaml);

	return rc;
}
