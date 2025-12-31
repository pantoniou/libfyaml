# Scoped Builder Approach - Future Work

## Current Implementation (Internalization)

**python-libfyaml/_libfyaml_minimal.c:306-365**

When creating child FyGeneric objects (e.g., `child = parent['foo']`):
```c
typedef struct {
    PyObject_HEAD
    fy_generic fyg;
    struct fy_generic_builder *gb;  /* Non-NULL only for root (owner) */
    PyObject *root;  /* Reference to root object (NULL if this is root) */
    PyObject *path;  /* List of path elements from root */
    int mutable;
} FyGenericObject;
```

- Child objects have `gb = NULL`
- Keep reference to root to prevent builder destruction
- When child needs to perform operations, must **internalize** values from parent builder (copying data)

## Proposed: Scoped Builder Approach

### Key Insight from User

> "Generic builder supports scoping. What if when making an assignment (var = data['foo']) there is no python object created rather a clone is done, but with the builder stacking on top of the builder that was the source. No internalization is needed since the original generic value is still valid for the builder chain."

### C Implementation Already Exists

Builder scoping is already implemented in C:

**fy-generic.c:240-247** - Parent tracking:
```c
gb->flags = ((cfg->flags & FYGBCF_SCOPE_LEADER) ? FYGBF_SCOPE_LEADER : 0) |
    ((cfg->flags & FYGBCF_DEDUP_ENABLED) ? FYGBF_DEDUP_ENABLED : 0);

/* turn on the dedup chain bit if no parent, or the parent has it too */
if ((gb->flags & FYGBF_DEDUP_ENABLED) &&
    (!cfg->parent || (cfg->parent->flags & FYGBF_DEDUP_CHAIN)))
    gb->flags |= FYGBF_DEDUP_CHAIN;
```

**fy-generic.c:394-415** - Scope chain walking for contains:
```c
bool fy_generic_builder_contains_out_of_place(struct fy_generic_builder *gb, fy_generic v)
{
    const void *ptr = fy_generic_resolve_ptr(v);

    /* find if any of the builders in scope contain it */
    while (gb) {
        if (fy_allocator_contains(gb->allocator, gb->alloc_tag, ptr))
            return true;
        gb = gb->cfg.parent;
    }
    return false;
}
```

**fy-generic.c:501-519** - Dedup lookup walks parent chain:
```c
const void *fy_gb_lookupv(struct fy_generic_builder *gb, ...)
{
    while (gb && (gb->flags & FYGBF_DEDUP_ENABLED)) {
        ptr = fy_allocator_lookupv_nocheck(gb->allocator, gb->alloc_tag, ...);
        if (ptr)
            return ptr;
        gb = gb->cfg.parent;  // Walk up parent chain
    }
    return NULL;
}
```

### Test Evidence

**test/libfyaml-test-generic.c:1645-1674** - gb_scoping test:
```c
// Parent builder
gb = fy_generic_builder_create_in_place(
    FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER, NULL, buf, sizeof(buf));

// Helper function that creates scoped child builder
v = calculate_seq_sum(0, gb, seq);

// Result from child is valid in parent
ck_assert(fy_generic_builder_contains(gb, v));
```

**test/libfyaml-test-generic.c:1612-1641** - calculate_seq_sum helper:
```c
static fy_generic calculate_seq_sum(..., struct fy_generic_builder *parent_gb, fy_generic seq)
{
    char buf[8192];
    struct fy_generic_builder *gb;

    // Create scoped child builder
    gb = fy_generic_builder_create_in_place(
        FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | flags,
        parent_gb,  // Parent!
        buf, sizeof(buf));

    // Do work, create value in child scope
    v = fy_value(gb, sum);

    // Export back to parent scope
    return fy_generic_builder_export(gb, v);
}
```

**test/libfyaml-test-generic.c:1735-1785** - gb_dedup_scoping2:
```c
// Create two stacked dedup builders
gb1 = fy_generic_builder_create_in_place(
    FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED,
    NULL, buf1, sizeof(buf1));

gb2 = fy_generic_builder_create_in_place(
    FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED,
    gb1,  // Parent
    buf2, sizeof(buf2));

// String created in gb1
v1 = fy_value(gb1, "This is a long string");

// Same string in gb2 reuses gb1's allocation via dedup chain
v2 = fy_value(gb2, "This is a long string");

ck_assert(v1.v == v2.v);  // Same pointer!
```

## Proposed Python Architecture

### FyGenericObject Structure

```c
typedef struct {
    PyObject_HEAD
    fy_generic fyg;
    struct fy_generic_builder *gb_inplace;  // Always exists, SCOPE_LEADER
    struct fy_generic_builder *gb_dynamic;  // NULL or mremap, NOT SCOPE_LEADER
    char inplace_buf[2048];  // In-place buffer
    PyObject *parent;  // Parent Python object (keeps parent alive)
} FyGenericObject;
```

### In-Place Builder with Upgrade Path

**FY_LOCAL_OP pattern** (include/libfyaml/fy-internal-generic.h:4970-4991):
```c
#define FY_LOCAL_OP(...) \
    ({ \
        size_t _sz = FY_GENERIC_BUILDER_LINEAR_IN_PLACE_MIN_SIZE;  // 384 bytes
        for (;;) { \
            _buf = alloca(_sz);
            _gb = fy_generic_builder_create_in_place(
                FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER, NULL, _buf, _sz);
            _v = fy_generic_op(_gb, ...);
            if (fy_generic_is_valid(_v))
                break;  // Success!
            FY_STACK_RESTORE(_stack_save);
            if (!fy_gb_allocation_failures(_gb) || _sz > 65536)
                break;  // Not OOM or max size
            _sz = _sz * 2;  // Double and retry
        }
        _v;
    })
```

**For Python**: Start with small in-place buffer, upgrade to mremap on OOM.

### Builder Creation Rules

**Root (from loads()):**
```c
cfg.flags = FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED;
cfg.parent = NULL;
self->gb_inplace = fy_generic_builder_create_in_place(&cfg, buf, sizeof(buf));
self->gb_dynamic = NULL;
self->parent = NULL;
```

**Child (from data['foo']):**
```c
cfg.flags = FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED;
cfg.parent = parent->get_current_builder();
self->gb_inplace = fy_generic_builder_create_in_place(&cfg, buf, sizeof(buf));
self->gb_dynamic = NULL;
self->parent = parent;  // Keep parent alive!
Py_INCREF(self->parent);
```

**Upgrade on OOM (CRITICAL: in-place becomes parent!):**
```c
// Check for allocation failures
if (fy_gb_allocation_failures(self->gb_inplace) && !self->gb_dynamic) {
    cfg.flags = FYGBCF_SCHEMA_AUTO | FYGBCF_DEDUP_ENABLED;  // NOT SCOPE_LEADER!
    cfg.parent = self->gb_inplace;  // In-place becomes parent
    cfg.allocator = create_mremap_allocator();

    self->gb_dynamic = fy_generic_builder_create(&cfg);
    // Retry operation with gb_dynamic
}
```

**Why in-place becomes parent**: The in-place builder contains valid data that must remain accessible. The dynamic builder extends the scope.

### Scope Chain Example

```python
data = libfyaml.loads(yaml)     # Root
item = data['foo']              # Child scope
nested = item['bar']            # Child scope
```

Before upgrade:
```
nested.gb_inplace (SCOPE_LEADER, parent → item's current builder)
  ↓
item.gb_inplace (SCOPE_LEADER, parent → data's current builder)
  ↓
data.gb_inplace (SCOPE_LEADER, parent = NULL)
```

After `nested` upgrades:
```
nested.gb_dynamic (NOT SCOPE_LEADER, parent → nested.gb_inplace)
  ↓
nested.gb_inplace (SCOPE_LEADER, parent → item's current builder)
  ↓
item.gb_inplace (SCOPE_LEADER, parent → data's current builder)
  ↓
data.gb_inplace (SCOPE_LEADER, parent = NULL)
```

### Key Functions

- `fy_generic_builder_contains()` - Walks parent chain to check ownership
- `fy_gb_lookupv()` - Dedup lookup walks parent chain
- `fy_generic_builder_get_scope_leader()` - Finds nearest SCOPE_LEADER
- `fy_generic_builder_export()` - Internalizes value to parent scope

## Open Questions

### What are the actual benefits?

**Use Case 1: Just Reading**
```python
data = libfyaml.loads(yaml)
value = data['users'][0]['name']
print(value)
```
- Current: No builder needed, just dereference fy_generic
- Scoped: Same, no builder needed
- **Benefit: None**

**Use Case 2: Modifying (mutable=True)**
```python
data = libfyaml.loads(yaml, mutable=True)
users = data['users']
users[0] = {'name': 'Alice'}
```
- Current: Must internalize sequence, copy data
- Scoped: Use scoped builder, parent data accessible via chain
- **Benefit: Avoid copying parent data?**

**Use Case 3: Operations on Subsets**
```python
data = libfyaml.loads(yaml)
users = data['users']
names = fy_map(users, lambda u: u['name'])
```
- Current: fy_map needs builder, must internalize users
- Scoped: Use scoped builder, can read from parent scope
- **Benefit: No copying input data?**

### Questions to Answer

1. **When does scoping eliminate internalization?**
   - Need to understand specific operation patterns

2. **What operations require a builder?**
   - Read operations: probably none
   - Mutation operations: definitely need builder
   - Functional operations (map/filter/reduce): need builder for results

3. **Performance tradeoffs:**
   - Small in-place buffer (2KB) vs immediate mremap allocation
   - Cost of scope chain walking vs cost of internalization
   - Memory overhead: multiple small buffers vs fewer large allocations

4. **Complexity tradeoffs:**
   - Current: Simple, predictable (root owns builder, children reference root)
   - Scoped: More complex (every child has builder, upgrade logic, chain walking)

## Implementation Checklist

If we decide to proceed:

- [ ] Update FyGenericObject structure
- [ ] Implement `get_current_builder()` helper
- [ ] Modify `FyGeneric_from_parent()` to create scoped in-place builder
- [ ] Implement OOM detection and upgrade to mremap
- [ ] Update all operation functions to use scoped builders
- [ ] Add cleanup logic in `__del__`
- [ ] Write tests for scope chain behavior
- [ ] Write tests for upgrade path
- [ ] Benchmark vs current internalization approach
- [ ] Document when scoping is beneficial

## References

- **C Implementation**: src/generic/fy-generic.c (lines 240-441)
- **Tests**: test/libfyaml-test-generic.c (gb_scoping, gb_dedup_scoping, gb_dedup_scoping2)
- **FY_LOCAL_OP macro**: include/libfyaml/fy-internal-generic.h:4970-4991
- **Current Python**: python-libfyaml/libfyaml/_libfyaml_minimal.c:306-365
