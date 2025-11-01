# Allocator Configuration Examples

This document provides examples of using the allocator configuration parser with fy-tool.

## Basic Usage

### Simple Allocator Selection (No Configuration)

```bash
# Use default malloc/free
fy-tool --allocator=default parse file.yaml

# Use malloc allocator (with tracking)
fy-tool --allocator=malloc parse file.yaml
```

## Linear Allocator

The linear allocator uses a bump-pointer allocation strategy (arena allocation). All allocations are freed together when the allocator is destroyed.

### Parameters

- `size=<size>` - Pre-allocate buffer of specified size

### Examples

```bash
# Use linear allocator with default size
fy-tool --allocator=linear parse file.yaml

# Pre-allocate 16 MB buffer
fy-tool --allocator=linear:size=16M parse large.yaml

# Pre-allocate 1 GB buffer
fy-tool --allocator=linear:size=1G parse huge.yaml
```

### Size Suffixes

Supported suffixes (binary):
- `K` or `KB` or `Ki` = 1024 bytes
- `M` or `MB` or `Mi` = 1024 * 1024 bytes
- `G` or `GB` or `Gi` = 1024 * 1024 * 1024 bytes
- `T` or `TB` or `Ti` = 1024 * 1024 * 1024 * 1024 bytes

## Mremap Allocator

The mremap allocator uses dynamically growing arenas with mremap for efficient reallocation.

### Parameters

- `big_alloc_threshold=<size>` - Size threshold for creating new arena (default: auto)
- `empty_threshold=<size>` - Empty space threshold for cleanup (default: auto)
- `minimum_arena_size=<size>` - Minimum arena size (default: auto)
- `grow_ratio=<float>` - Growth ratio when arena is full (default: auto, e.g., 1.5)
- `balloon_ratio=<float>` - VM area balloon ratio (default: auto, e.g., 2.0)
- `arena_type=<type>` - Arena type: `default`, `malloc`, `mmap` (default: default)

### Examples

```bash
# Use mremap with defaults
fy-tool --allocator=mremap parse file.yaml

# Set minimum arena size to 4 MB
fy-tool --allocator=mremap:minimum_arena_size=4M parse file.yaml

# Multiple parameters
fy-tool --allocator=mremap:minimum_arena_size=4M,grow_ratio=1.5,balloon_ratio=2.0 parse file.yaml

# Use malloc-based arenas (portable)
fy-tool --allocator=mremap:arena_type=malloc parse file.yaml

# Use mmap-based arenas (Linux)
fy-tool --allocator=mremap:arena_type=mmap parse file.yaml
```

## Dedup Allocator

The deduplication allocator stores identical objects only once, saving memory when there are many duplicate values.

### Parameters

- `parent=<type>` - Parent allocator: `malloc`, `linear`, `mremap` (default: malloc)
- `bloom_filter_bits=<int>` - Bloom filter size in bits (default: auto)
- `bucket_count_bits=<int>` - Hash table bucket count bits (default: auto)
- `dedup_threshold=<size>` - Minimum size for deduplication (default: 0 = always)
- `chain_length_grow_trigger=<int>` - Chain length before growing hash table (default: auto)
- `estimated_content_size=<size>` - Estimated total content size for sizing (default: 0)

### Examples

```bash
# Use dedup with malloc parent
fy-tool --allocator=dedup parse file.yaml

# Use dedup with linear parent allocator
fy-tool --allocator=dedup:parent=linear parse file.yaml

# Set dedup threshold to 32 bytes (don't dedup small objects)
fy-tool --allocator=dedup:dedup_threshold=32 parse file.yaml

# Multiple parameters with size hints
fy-tool --allocator=dedup:parent=mremap,estimated_content_size=10M,dedup_threshold=32 parse file.yaml
```

## Auto Allocator

The auto allocator automatically selects the best allocation strategy based on the scenario.

### Parameters

- `scenario=<type>` - Scenario type (see below)
- `estimated_max_size=<size>` - Estimated maximum content size (default: 0 = unknown)

### Scenario Types

- `per_tag_free` - Per-tag freeing, no individual object free
- `per_tag_free_dedup` - Per-tag freeing with deduplication
- `per_obj_free` - Object freeing allowed, tag freeing still works
- `per_obj_free_dedup` - Per-object freeing with deduplication
- `single_linear` or `single_linear_range` - Single linear range, no frees at all
- `single_linear_dedup` or `single_linear_range_dedup` - Single linear range with deduplication

### Examples

```bash
# Use auto with defaults
fy-tool --allocator=auto parse file.yaml

# Single linear allocation (fastest for read-only documents)
fy-tool --allocator=auto:scenario=single_linear parse readonly.yaml

# Single linear with dedup (memory-efficient for duplicate-heavy documents)
fy-tool --allocator=auto:scenario=single_linear_dedup parse anchors.yaml

# Per-object free with size estimate
fy-tool --allocator=auto:scenario=per_obj_free,estimated_max_size=100M parse large.yaml
```

## Use Cases

### Large Documents - Linear Allocator

For large documents that are parsed once and then freed:

```bash
fy-tool --allocator=linear:size=100M parse huge-document.yaml
```

**Benefits:**
- Fastest allocation (bump pointer)
- No fragmentation
- Bulk free is instant

### Documents with Duplicates - Dedup Allocator

For documents with many duplicate values (e.g., repeated anchor references):

```bash
fy-tool --allocator=dedup:parent=linear,dedup_threshold=16 parse duplicates.yaml
```

**Benefits:**
- Reduced memory usage
- Faster comparison (pointer equality)

### Long-Running Processes - Mremap Allocator

For applications that parse many documents over time:

```bash
fy-tool --allocator=mremap:minimum_arena_size=1M parse *.yaml
```

**Benefits:**
- Efficient arena growth
- Can handle variable document sizes
- Better than malloc for fragmentation

### Unknown Workload - Auto Allocator

When you don't know the characteristics:

```bash
fy-tool --allocator=auto:scenario=per_tag_free parse unknown.yaml
```

**Benefits:**
- Adapts to workload
- Reasonable defaults

## Performance Tips

1. **For read-only documents:** Use `linear` or `auto:scenario=single_linear`
2. **For documents with many duplicates:** Use `dedup`
3. **For streaming/incremental parsing:** Use `mremap` or `malloc`
4. **For maximum performance:** Pre-size linear allocator: `linear:size=<estimated_size>`
5. **For minimum memory:** Use `dedup:parent=linear`

## Debugging

### Check Allocator Stats

Some allocators support dumping statistics. You can enable this programmatically or look at memory usage with tools like valgrind.

### Memory Profiling

```bash
# Check for leaks with linear allocator (should be instant)
valgrind --leak-check=full fy-tool --allocator=linear parse file.yaml

# Check actual memory usage
/usr/bin/time -v fy-tool --allocator=linear:size=16M parse file.yaml
```

## Common Issues

### "Failed to create allocator"

**Cause:** Invalid allocator name or parameter

**Solution:** Check spelling and available parameters for that allocator type

### "Invalid size value"

**Cause:** Malformed size suffix

**Solution:** Use valid suffixes: K, M, G, T (with optional B or i)

Examples:
- ✅ Correct: `16M`, `1G`, `512K`, `16MB`, `1Gi`
- ❌ Wrong: `16m`, `1g`, `16 M`, `1.5K`

### "Unknown parameter"

**Cause:** Parameter not supported by that allocator type

**Solution:** Check the parameter list for that allocator type above

## Example Workflows

### Parse and Dump Large File

```bash
# Fast parse with linear allocator
fy-tool --allocator=linear:size=50M parse huge.yaml > output.yaml
```

### Filter with Memory Efficiency

```bash
# Use dedup to save memory on repeated values
fy-tool --allocator=dedup:parent=linear filter -f .data[] large.yaml
```

### Compare Allocator Performance

```bash
# Benchmark different allocators
for alloc in default malloc linear mremap auto; do
    echo "Testing $alloc..."
    time fy-tool --allocator=$alloc parse benchmark.yaml > /dev/null
done
```

## API Usage

For programmatic use, create allocators with configuration:

```c
#include <libfyaml.h>

/* Linear allocator with 16MB buffer */
struct fy_linear_allocator_cfg lcfg = {
    .size = 16 * 1024 * 1024,
    .buf = NULL  /* Allocator will allocate */
};
struct fy_allocator *alloc = fy_allocator_create("linear", &lcfg);

/* Mremap allocator with custom settings */
struct fy_mremap_allocator_cfg mcfg = {
    .minimum_arena_size = 4 * 1024 * 1024,
    .grow_ratio = 1.5f,
    .arena_type = FYMRAT_MMAP
};
struct fy_allocator *alloc2 = fy_allocator_create("mremap", &mcfg);

/* Use with parser via flags */
struct fy_parse_cfg cfg = {
    .flags = FYPCF_DEFAULT | FYPCF_ALLOCATOR_LINEAR
};
struct fy_parser *fyp = fy_parser_create(&cfg);
```

## Notes

- Size parameters support binary suffixes (K=1024, not 1000)
- Float parameters use dot notation (e.g., `1.5`, not `1,5`)
- Multiple parameters are separated by commas or colons
- Parameter order doesn't matter
- Unknown parameters cause an error (fail-fast)
- All parameters are optional (allocators have reasonable defaults)
