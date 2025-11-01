# Allocator Recursive Parsing Demonstration

## Overview

The allocator configuration parser now supports recursive parsing, allowing parent allocators (like dedup) to create their parent allocators with full configuration strings.

## How It Works

### 1. Simple Configuration (No Recursion)

```c
// Simple allocator with no parent
--allocator=linear:size=16M
```

**Parse Flow:**
1. `fy_linear_parse_cfg("size=16M", &cfg)`
2. Parses `size=16M` → creates `fy_linear_allocator_cfg{.size = 16*1024*1024}`
3. Returns config to caller

### 2. Parent Without Configuration (One Level)

```c
// Dedup with simple parent (no parent config)
--allocator=dedup:parent=malloc,dedup_threshold=32
```

**Parse Flow:**
1. `fy_dedup_parse_cfg("parent=malloc,dedup_threshold=32", &cfg)`
2. Parses `parent=malloc` → parent_type="malloc", parent_params=NULL
3. Looks up malloc ops: `fy_allocator_get_ops_by_name("malloc")`
4. Calls `malloc_ops->parse_cfg(NULL, &parent_cfg)` → no-op (malloc has no config)
5. Creates parent: `fy_allocator_create("malloc", parent_cfg)`
6. Returns dedup config with parent allocator embedded

### 3. Parent With Configuration (Recursive Parsing!)

```c
// Dedup with configured parent - THIS IS WHERE RECURSION HAPPENS
--allocator=dedup:parent=linear:size=16M,dedup_threshold=32
```

**Parse Flow (Recursive):**
```
fy_dedup_parse_cfg("parent=linear:size=16M,dedup_threshold=32")
├─ Parses dedup parameters: dedup_threshold=32
├─ Parses parent string: "linear:size=16M"
│  ├─ Splits: type="linear", params="size=16M"
│  └─ Gets ops: fy_allocator_get_ops_by_name("linear")
│
├─ RECURSION: Calls linear's parser
│  └─ linear_ops->parse_cfg("size=16M", &parent_cfg)
│     └─ Returns: fy_linear_allocator_cfg{.size = 16MB}
│
├─ Creates parent allocator with parsed config
│  └─ fy_allocator_create("linear", parent_cfg)
│
└─ Returns dedup config with fully configured linear parent
```

### 4. Complex Nested Configuration

```c
// Dedup with mremap parent (multiple parameters)
--allocator=dedup:parent=mremap:minimum_arena_size=4M:grow_ratio=1.5,dedup_threshold=64
```

**Parse Flow:**
```
fy_dedup_parse_cfg("parent=mremap:minimum_arena_size=4M:grow_ratio=1.5,dedup_threshold=64")
├─ Parses dedup parameters: dedup_threshold=64
├─ Parses parent string: "mremap:minimum_arena_size=4M:grow_ratio=1.5"
│  ├─ Splits: type="mremap", params="minimum_arena_size=4M:grow_ratio=1.5"
│  └─ Gets ops: fy_allocator_get_ops_by_name("mremap")
│
├─ RECURSION: Calls mremap's parser
│  └─ mremap_ops->parse_cfg("minimum_arena_size=4M:grow_ratio=1.5", &parent_cfg)
│     ├─ Parses minimum_arena_size=4M → 4194304
│     ├─ Parses grow_ratio=1.5 → 1.5f
│     └─ Returns: fy_mremap_allocator_cfg{.minimum_arena_size=4M, .grow_ratio=1.5, ...}
│
├─ Creates parent allocator with parsed config
│  └─ fy_allocator_create("mremap", parent_cfg)
│
└─ Returns dedup config with fully configured mremap parent
```

## Code Walkthrough

### Key Function: `fy_dedup_parse_cfg`

```c
static int fy_dedup_parse_cfg(const char *cfg_str, void **cfgp)
{
    // ... parse dedup's own parameters (dedup_threshold, etc.) ...

    // Parse parent allocator config string (e.g., "linear:size=16M")
    parent_type = strdup(parent_config_str);  // "linear:size=16M"

    parent_params = strchr(parent_type, ':');
    if (parent_params) {
        *parent_params++ = '\0';  // Split: type="linear", params="size=16M"
    }

    // Get parent allocator ops by name
    parent_ops = fy_allocator_get_ops_by_name(parent_type);  // Gets linear ops

    // RECURSION: Call parent's parse_cfg
    if (parent_ops->parse_cfg) {
        rc = parent_ops->parse_cfg(parent_params, &parent_cfg);
        //   ^-- This calls fy_linear_parse_cfg("size=16M", &parent_cfg)
        //       which parses the size parameter and returns a config struct
    }

    // Create parent allocator with parsed config
    cfg->parent_allocator = fy_allocator_create(parent_type, parent_cfg);

    // Free parent config (allocator owns it now)
    if (parent_ops->free_cfg && parent_cfg)
        parent_ops->free_cfg(parent_cfg);

    return 0;
}
```

### Helper Function: `fy_allocator_get_ops_by_name`

```c
const struct fy_allocator_ops *fy_allocator_get_ops_by_name(const char *name)
{
    // Search builtin allocators
    for (i = 0; i < ARRAY_SIZE(builtin_allocators); i++) {
        if (!strcmp(builtin_allocators[i].name, name)) {
            return builtin_allocators[i].ops;  // Returns &fy_linear_allocator_ops
        }
    }

    // Search registered allocators
    // ...

    return NULL;  // Not found
}
```

## Memory Management

The recursive parsing properly manages memory:

1. **Parse**: Parent's `parse_cfg` allocates and returns config struct
2. **Create**: `fy_allocator_create` uses config to initialize allocator
3. **Free**: Parent's `free_cfg` deallocates the temporary config struct
4. **Ownership**: The created allocator owns its configuration data

```c
// Parse parent config (allocates memory)
parent_ops->parse_cfg("size=16M", &parent_cfg);  // Allocates fy_linear_allocator_cfg

// Create allocator (copies config data into allocator structure)
cfg->parent_allocator = fy_allocator_create("linear", parent_cfg);

// Free temporary config (original memory is freed)
parent_ops->free_cfg(parent_cfg);  // Frees fy_linear_allocator_cfg

// Allocator retains its own copy of the configuration
```

## Practical Examples

### Example 1: Dedup with Linear Parent (16MB)

```bash
fy-tool --allocator=dedup:parent=linear:size=16M,dedup_threshold=32 input.yaml
```

Creates:
- Linear allocator with 16MB buffer
- Dedup allocator wrapping it with 32-byte dedup threshold

### Example 2: Dedup with Mremap Parent (4MB arenas, 1.5x growth)

```bash
fy-tool --allocator=dedup:parent=mremap:minimum_arena_size=4M:grow_ratio=1.5 input.yaml
```

Creates:
- Mremap allocator with 4MB minimum arena size and 1.5x growth factor
- Dedup allocator wrapping it with default dedup settings

### Example 3: Auto Allocator (No Recursion, But Shows Pattern)

```bash
fy-tool --allocator=auto:scenario=single_linear,estimated_max_size=100M input.yaml
```

Creates:
- Auto allocator with single_linear scenario
- Internally creates appropriate allocators based on scenario

## Limitations

### Current Limitation: Comma in Parent Parameters

The current implementation uses `,` to separate parameters at the top level, which means parent configurations with multiple comma-separated parameters need special handling:

```bash
# This works (colon-separated parent params)
--allocator=dedup:parent=mremap:minimum_arena_size=4M:grow_ratio=1.5,dedup_threshold=32

# This would NOT work correctly (comma in parent config conflicts with top-level parsing)
# --allocator=dedup:parent=mremap:minimum_arena_size=4M,grow_ratio=1.5,dedup_threshold=32
#                                                         ^-- This comma breaks parsing
```

**Workaround**: Parent allocators should use `:` as separator, or we need to implement proper nested/quoted parameter parsing.

## Future Enhancements

1. **Deeper Nesting**: Support dedup wrapping dedup:
   ```
   dedup:parent=dedup:parent=linear:size=16M
   ```

2. **Escaped/Quoted Parameters**: Allow commas in parent configs:
   ```
   dedup:parent="mremap:minimum_arena_size=4M,grow_ratio=1.5",dedup_threshold=32
   ```

3. **Config Validation**: Validate parent allocator compatibility
   - Some allocators may not work as dedup parents
   - Add capability checking

## Testing

To test recursive parsing, create a simple test program:

```c
#include <libfyaml.h>

int main() {
    // Test 1: Simple parent
    void *cfg1 = NULL;
    fy_dedup_parse_cfg("parent=malloc,dedup_threshold=32", &cfg1);

    // Test 2: Configured parent (RECURSIVE!)
    void *cfg2 = NULL;
    fy_dedup_parse_cfg("parent=linear:size=16M,dedup_threshold=32", &cfg2);

    // Test 3: Complex parent
    void *cfg3 = NULL;
    fy_dedup_parse_cfg("parent=mremap:minimum_arena_size=4M,dedup_threshold=64", &cfg3);

    // Each config can now be used to create a dedup allocator
    struct fy_allocator *a = fy_allocator_create("dedup", cfg2);

    return 0;
}
```
