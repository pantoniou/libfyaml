# Allocator Configuration Parser Refactoring Plan

**Status:** NEEDS REFACTORING
**Current:** Config parsing is in fy-tool
**Should be:** Config parsing in each allocator's ops

---

## Problem

Currently, the configuration parsing for allocators is implemented in:
- `src/tool/fy-allocator-config-parse.c` - Tool-specific parser
- `src/tool/fy-allocator-config-parse.h` - Tool-specific header

This has several issues:
1. **Not reusable** - Other applications can't easily use the parsing
2. **Tight coupling** - Tool knows details of each allocator's parameters
3. **Maintenance burden** - Adding a new allocator requires updating tool code
4. **Poor separation** - Allocator parameter knowledge is not co-located

## Proposed Solution

### Add parse_cfg/free_cfg to allocator_ops

**File:** `src/allocator/fy-allocator.h`

```c
struct fy_allocator_ops {
    // ... existing ops ...

    /* Parse configuration string and return config structure */
    int (*parse_cfg)(const char *cfg_str, void **cfg);

    /* Free configuration structure created by parse_cfg */
    void (*free_cfg)(void *cfg);
};
```

### Create Shared Parsing Utilities

**File:** `src/allocator/fy-allocator-parse-util.c` + `.h`

Common parsing functions used by multiple allocators:

```c
int fy_parse_size_suffix(const char *str, size_t *sizep);
int fy_parse_float_value(const char *str, float *floatp);
int fy_parse_unsigned_value(const char *str, unsigned int *valp);
```

### Implement parse_cfg in Each Allocator

#### fy-allocator-linear.c

```c
static int fy_linear_allocator_parse_cfg(const char *cfg_str, void **cfgp)
{
    struct fy_linear_allocator_cfg *cfg;
    // Parse "size=16M" format
    return 0;
}

static void fy_linear_allocator_free_cfg(void *cfg)
{
    free(cfg);
}

const struct fy_allocator_ops fy_linear_allocator_ops = {
    // ... existing ops ...
    .parse_cfg = fy_linear_allocator_parse_cfg,
    .free_cfg = fy_linear_allocator_free_cfg,
};
```

#### fy-allocator-mremap.c

```c
static int fy_mremap_allocator_parse_cfg(const char *cfg_str, void **cfgp)
{
    struct fy_mremap_allocator_cfg *cfg;
    // Parse "minimum_arena_size=4M,grow_ratio=1.5,..." format
    return 0;
}

static void fy_mremap_allocator_free_cfg(void *cfg)
{
    free(cfg);
}

const struct fy_allocator_ops fy_mremap_allocator_ops = {
    // ... existing ops ...
    .parse_cfg = fy_mremap_allocator_parse_cfg,
    .free_cfg = fy_mremap_allocator_free_cfg,
};
```

#### fy-allocator-dedup.c

```c
static int fy_dedup_allocator_parse_cfg(const char *cfg_str, void **cfgp)
{
    struct fy_dedup_allocator_cfg *cfg;
    // Parse "parent=linear,dedup_threshold=32,..." format
    // Note: Must create parent allocator!
    return 0;
}

static void fy_dedup_allocator_free_cfg(void *cfg)
{
    struct fy_dedup_allocator_cfg *dcfg = cfg;
    if (dcfg->parent_allocator)
        fy_allocator_destroy(dcfg->parent_allocator);
    free(cfg);
}

const struct fy_allocator_ops fy_dedup_allocator_ops = {
    // ... existing ops ...
    .parse_cfg = fy_dedup_allocator_parse_cfg,
    .free_cfg = fy_dedup_allocator_free_cfg,
};
```

#### fy-allocator-auto.c

```c
static int fy_auto_allocator_parse_cfg(const char *cfg_str, void **cfgp)
{
    struct fy_auto_allocator_cfg *cfg;
    // Parse "scenario=single_linear,estimated_max_size=100M" format
    return 0;
}

static void fy_auto_allocator_free_cfg(void *cfg)
{
    free(cfg);
}

const struct fy_allocator_ops fy_auto_allocator_ops = {
    // ... existing ops ...
    .parse_cfg = fy_auto_allocator_parse_cfg,
    .free_cfg = fy_auto_allocator_free_cfg,
};
```

#### fy-allocator-malloc.c

```c
// No config needed, but provide stubs
static int fy_malloc_allocator_parse_cfg(const char *cfg_str, void **cfgp)
{
    *cfgp = NULL;  // No config
    return 0;
}

static void fy_malloc_allocator_free_cfg(void *cfg)
{
    // Nothing to free
}

const struct fy_allocator_ops fy_malloc_allocator_ops = {
    // ... existing ops ...
    .parse_cfg = fy_malloc_allocator_parse_cfg,
    .free_cfg = fy_malloc_allocator_free_cfg,
};
```

### Generic Dispatcher

**File:** `src/allocator/fy-allocator.c` (add new public function)

```c
int fy_allocator_parse_config(const char *config_str,
                               char **allocator_name,
                               void **config)
{
    const struct fy_allocator_ops *ops;
    char *name, *params, *str_copy;
    int rc;

    if (!config_str || !allocator_name || !config)
        return -1;

    str_copy = strdup(config_str);
    if (!str_copy)
        return -1;

    /* Split on first ':' */
    name = str_copy;
    params = strchr(name, ':');
    if (params)
        *params++ = '\0';

    /* Look up allocator ops by name */
    ops = fy_allocator_get_ops_by_name(name);  // NEW helper function
    if (!ops) {
        fprintf(stderr, "Unknown allocator: %s\n", name);
        free(str_copy);
        return -1;
    }

    /* Call allocator's parse_cfg */
    if (ops->parse_cfg) {
        rc = ops->parse_cfg(params, config);
        if (rc < 0) {
            free(str_copy);
            return -1;
        }
    } else {
        *config = NULL;  // Allocator has no config
    }

    *allocator_name = strdup(name);
    free(str_copy);
    return 0;
}

void fy_allocator_free_config(const char *allocator_name, void *config)
{
    const struct fy_allocator_ops *ops;

    if (!config)
        return;

    ops = fy_allocator_get_ops_by_name(allocator_name);
    if (ops && ops->free_cfg)
        ops->free_cfg(config);
}
```

### Update Tool to Use New API

**File:** `src/tool/fy-tool.c`

```c
case OPT_ALLOCATOR:
    /* Use new generic parser */
    if (fy_allocator_parse_config(optarg, &allocator_name, &allocator_cfg) < 0) {
        fprintf(stderr, "Failed to parse allocator config: %s\n", optarg);
        goto err_out_usage;
    }

    /* Create the allocator */
    allocator = fy_allocator_create(allocator_name, allocator_cfg);
    if (!allocator) {
        fprintf(stderr, "Failed to create allocator: %s\n", allocator_name);
        goto err_out_usage;
    }

    /* Set flag for compatibility */
    cfg.flags &= ~(FYPCF_ALLOCATOR_MASK << FYPCF_ALLOCATOR_SHIFT);
    // ... set appropriate flag based on allocator_name ...
    break;
```

### Remove Tool-Specific Parser

After refactoring, remove:
- `src/tool/fy-allocator-config-parse.c`
- `src/tool/fy-allocator-config-parse.h`

## Migration Path

1. **Phase 1:** Add ops infrastructure
   - Add parse_cfg/free_cfg to fy_allocator_ops
   - Create fy-allocator-parse-util.c/.h with common functions
   - Add fy_allocator_get_ops_by_name() helper

2. **Phase 2:** Implement per-allocator parsing
   - Implement parse_cfg in fy-allocator-linear.c
   - Implement parse_cfg in fy-allocator-mremap.c
   - Implement parse_cfg in fy-allocator-dedup.c
   - Implement parse_cfg in fy-allocator-auto.c
   - Implement parse_cfg in fy-allocator-malloc.c (stub)

3. **Phase 3:** Add generic dispatcher
   - Add fy_allocator_parse_config() to fy-allocator.c
   - Export via libfyaml.h as public API

4. **Phase 4:** Update consumers
   - Update fy-tool.c to use new API
   - Remove old tool-specific parser files

5. **Phase 5:** Testing
   - Test all allocator configurations
   - Verify memory cleanup
   - Performance benchmarks

## Benefits After Refactoring

1. **Reusability** - Any application can parse allocator configs
2. **Maintainability** - Allocator params co-located with implementation
3. **Extensibility** - New allocators automatically get parsing
4. **Cleaner API** - Single public function for config parsing
5. **Better testing** - Can unit test each allocator's parser independently

## Current Status (Temporary)

For now, the parsing remains in `src/tool/fy-allocator-config-parse.c` as a working implementation. The refactoring described above should be done before the next major release.

**Technical debt:** The current implementation works but violates separation of concerns. Priority for refactoring: **HIGH**.

## Example After Refactoring

```c
/* Application code */
char *allocator_name;
void *allocator_cfg;

/* Parse config string */
if (fy_allocator_parse_config("linear:size=16M", &allocator_name, &allocator_cfg) < 0) {
    // Handle error
}

/* Create allocator */
struct fy_allocator *alloc = fy_allocator_create(allocator_name, allocator_cfg);

/* Use allocator... */

/* Cleanup */
fy_allocator_destroy(alloc);
fy_allocator_free_config(allocator_name, allocator_cfg);
free(allocator_name);
```

---

**Next Steps:**
1. Create fy-allocator-parse-util.c/.h
2. Implement parse_cfg in each allocator
3. Add fy_allocator_parse_config() public API
4. Update fy-tool to use new API
5. Remove old parser files
