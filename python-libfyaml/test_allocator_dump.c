/*
 * Test program to dump allocator statistics after parsing YAML
 * Compile: gcc -o test_allocator_dump test_allocator_dump.c -I../include -L../build/src/lib -lfyaml
 * Run: LD_LIBRARY_PATH=../build/src/lib ./test_allocator_dump AtomicCards-2-cleaned-small.yaml
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfyaml.h>

static char *read_file(const char *filename, size_t *len_out) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen");
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    size_t len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *data = malloc(len + 1);
    if (!data) {
        fclose(fp);
        return NULL;
    }

    size_t nread = fread(data, 1, len, fp);
    fclose(fp);

    if (nread != len) {
        free(data);
        return NULL;
    }

    data[len] = '\0';
    *len_out = len;
    return data;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <yaml-file>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    size_t yaml_len;
    char *yaml_str = read_file(filename, &yaml_len);
    if (!yaml_str) {
        fprintf(stderr, "Failed to read file: %s\n", filename);
        return 1;
    }

    printf("File: %s\n", filename);
    printf("Size: %.2f MB\n", yaml_len / (1024.0 * 1024.0));
    printf("\n");

    /* Create auto allocator (uses mremap by default) */
    struct fy_allocator *allocator = fy_allocator_create("auto", NULL);
    if (!allocator) {
        fprintf(stderr, "Failed to create allocator\n");
        free(yaml_str);
        return 1;
    }

    /* Configure generic builder */
    struct fy_generic_builder_cfg gb_cfg;
    memset(&gb_cfg, 0, sizeof(gb_cfg));
    gb_cfg.allocator = allocator;
    gb_cfg.estimated_max_size = yaml_len * 2;
    gb_cfg.flags = FYGBCF_OWNS_ALLOCATOR | FYGBCF_DEDUP_ENABLED;

    /* Create generic builder */
    struct fy_generic_builder *gb = fy_generic_builder_create(&gb_cfg);
    if (!gb) {
        fy_allocator_destroy(allocator);
        free(yaml_str);
        fprintf(stderr, "Failed to create generic builder\n");
        return 1;
    }

    /* Create string generic */
    fy_generic input = fy_gb_string_size_create(gb, yaml_str, yaml_len);
    if (!fy_generic_is_valid(input)) {
        fy_generic_builder_destroy(gb);
        free(yaml_str);
        fprintf(stderr, "Failed to create input string\n");
        return 1;
    }

    /* Parse YAML */
    struct fy_generic_op_args op_args;
    memset(&op_args, 0, sizeof(op_args));
    op_args.parse.parser_mode = fypm_yaml;

    fy_generic result = fy_generic_op(gb, FYGBOPF_PARSE, input, 1, &op_args);
    if (!fy_generic_is_valid(result)) {
        fy_generic_builder_destroy(gb);
        free(yaml_str);
        fprintf(stderr, "Failed to parse YAML\n");
        return 1;
    }

    printf("Parsed successfully!\n");
    printf("\n");

    /* Get allocator info */
    struct fy_allocator_info *info = fy_allocator_get_info(allocator, -1);
    if (info) {
        printf("=== ALLOCATOR INFO ===\n");
        printf("Type: %s\n", info->name);
        printf("Allocations: %lu\n", (unsigned long)info->stats.allocations);
        printf("Allocated bytes: %lu (%.2f MB)\n",
               (unsigned long)info->stats.allocated,
               info->stats.allocated / (1024.0 * 1024.0));
        printf("Frees: %lu\n", (unsigned long)info->stats.frees);
        printf("Freed bytes: %lu (%.2f MB)\n",
               (unsigned long)info->stats.freed,
               info->stats.freed / (1024.0 * 1024.0));
        printf("In use: %lu (%.2f MB)\n",
               (unsigned long)(info->stats.allocated - info->stats.freed),
               (info->stats.allocated - info->stats.freed) / (1024.0 * 1024.0));
        printf("\n");
    }

    /* Dump allocator details to stderr */
    printf("=== ALLOCATOR DUMP (to stderr) ===\n");
    fy_allocator_dump(allocator);
    printf("\n");

    /* Cleanup */
    fy_generic_builder_destroy(gb);
    free(yaml_str);

    return 0;
}
