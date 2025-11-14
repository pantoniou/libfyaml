# New Features: Lambda Support and Unified Polymorphic API (December 2025)

## Executive Summary

libfyaml's generic API now includes **lambda support** for functional operations (filter/map/reduce) and a **unified polymorphic API** that automatically detects whether to use stack or heap allocation. These features bring Python/JavaScript-level ergonomics to C while maintaining zero runtime overhead.

**Key commits**:
- `02ec8f9` - "working lambdas for gcc"
- `934bf59` - "fixed clang lambdas"
- `d3043e1` - "unified ops and stuff complete"

## New Features

### 1. Lambda Support (Inline Functions)

Write filter/map/reduce logic inline without defining separate functions.

**Before** (function pointer):
```c
static bool is_over_100(struct fy_generic_builder *gb, fy_generic v) {
    return fy_cast(v, 0) > 100;
}

fy_generic filtered = fy_filter(data, is_over_100);
```

**After** (lambda):
```c
fy_generic filtered = fy_filter_lambda(data, fy_cast(v, 0) > 100);
```

**Compiler support**:
- **GCC** (4.0+): Uses nested functions extension, no special flags
- **Clang** (3.0+): Uses Blocks extension, requires `-fblocks` flag

#### All Functional Operations Support Lambdas

**Filter**:
```c
fy_generic large = fy_filter_lambda(data, fy_cast(v, 0) > 100);
```

**Map**:
```c
fy_generic doubled = fy_map_lambda(data, fy_value(fy_cast(v, 0) * 2));
```

**Reduce**:
```c
int sum = fy_reduce_lambda(data, 0, fy_value(fy_cast(acc, 0) + fy_cast(v, 0)));
```

**Parallel versions**:
```c
struct fy_thread_pool *tp = fy_thread_pool_create(NULL);

fy_generic filtered = fy_pfilter_lambda(data, tp, fy_cast(v, 0) > 100);
fy_generic mapped = fy_pmap_lambda(data, tp, fy_value(fy_cast(v, 0) * 2));
int sum = fy_preduce_lambda(data, 0, tp, fy_value(fy_cast(acc, 0) + fy_cast(v, 0)));

fy_thread_pool_destroy(tp);
```

### 2. Unified Polymorphic API

Filter/map/reduce operations now automatically detect stack vs heap allocation.

**Old API** (explicit):
```c
// Stack allocation
fy_generic result = fy_local_filter(data, predicate_fn);

// Heap allocation via builder
fy_generic result = fy_gb_filter(gb, data, predicate_fn);
```

**New API** (polymorphic):
```c
// Auto-detects: no builder = stack, with builder = heap
fy_generic result = fy_filter(data, predicate_fn);        // Stack
fy_generic result = fy_filter(gb, data, predicate_fn);    // Heap
```

**Benefits**:
- Less cognitive load (don't need to remember `_local` vs `_gb` prefix)
- Same code works for both allocation strategies
- Easier to switch between temporary and persistent data

### 3. Functional Pipelines with Lambdas

Combine multiple operations inline for declarative data transformation:

```c
fy_generic data = fy_sequence(1, 50, 101, 200, 3, 75);

// Filter values > 100, then multiply by 10
fy_generic result = fy_map_lambda(
    fy_filter_lambda(data, fy_cast(v, 0) > 100),
    fy_value(fy_cast(v, 0) * 10)
);
// Result: [1010, 2000]

// Iterate with foreach
int value;
fy_foreach(value, result) {
    printf("%d\n", value);
}
```

### 4. Lambda Variables

Each lambda type has specific variables available:

**Filter lambda** (`v` only):
```c
fy_filter_lambda(data,
    // Available: gb, v
    fy_cast(v, 0) > threshold
);
```

**Map lambda** (`v` only):
```c
fy_map_lambda(data,
    // Available: gb, v
    fy_value(fy_cast(v, 0) * 2)
);
```

**Reduce lambda** (`acc` and `v`):
```c
fy_reduce_lambda(data, init,
    // Available: gb, acc, v
    fy_value(fy_cast(acc, 0) + fy_cast(v, 0))
);
```

### 5. Variable Capture

Lambdas can capture local variables from enclosing scope:

```c
int threshold = 100;
int multiplier = 10;

fy_generic result = fy_map_lambda(
    fy_filter_lambda(data, fy_cast(v, 0) > threshold),  // Captures threshold
    fy_value(fy_cast(v, 0) * multiplier)  // Captures multiplier
);
```

## Language Comparisons

### Python

```python
# Filter
filtered = [x for x in data if x > 100]

# Map
doubled = [x * 2 for x in data]

# Reduce
from functools import reduce
sum = reduce(lambda acc, x: acc + x, data, 0)

# Pipeline
result = [x * 10 for x in data if x > 100]
```

### libfyaml (with lambdas)

```c
// Filter
fy_generic filtered = fy_filter_lambda(data, fy_cast(v, 0) > 100);

// Map
fy_generic doubled = fy_map_lambda(data, fy_value(fy_cast(v, 0) * 2));

// Reduce
int sum = fy_reduce_lambda(data, 0, fy_value(fy_cast(acc, 0) + fy_cast(v, 0)));

// Pipeline
fy_generic result = fy_map_lambda(
    fy_filter_lambda(data, fy_cast(v, 0) > 100),
    fy_value(fy_cast(v, 0) * 10)
);
```

**Nearly identical syntax!**

### JavaScript

```javascript
// Filter
const filtered = data.filter(x => x > 100);

// Map
const doubled = data.map(x => x * 2);

// Reduce
const sum = data.reduce((acc, x) => acc + x, 0);

// Pipeline
const result = data
    .filter(x => x > 100)
    .map(x => x * 10);
```

### libfyaml (with lambdas)

```c
// Filter
fy_generic filtered = fy_filter_lambda(data, fy_cast(v, 0) > 100);

// Map
fy_generic doubled = fy_map_lambda(data, fy_value(fy_cast(v, 0) * 2));

// Reduce
int sum = fy_reduce_lambda(data, 0, fy_value(fy_cast(acc, 0) + fy_cast(v, 0)));

// Pipeline (no chaining, but same logic)
fy_generic result = fy_map_lambda(
    fy_filter_lambda(data, fy_cast(v, 0) > 100),
    fy_value(fy_cast(v, 0) * 10)
);
```

## Complete API Matrix

| Operation | Stack (no builder) | Heap (with builder) | Polymorphic | Lambda | Parallel | Parallel Lambda |
|-----------|-------------------|---------------------|-------------|--------|----------|-----------------|
| **Filter** | `fy_local_filter` | `fy_gb_filter` | `fy_filter` | `fy_filter_lambda` | `fy_pfilter` | `fy_pfilter_lambda` |
| **Map** | `fy_local_map` | `fy_gb_map` | `fy_map` | `fy_map_lambda` | `fy_pmap` | `fy_pmap_lambda` |
| **Reduce** | `fy_local_reduce` | `fy_gb_reduce` | `fy_reduce` | `fy_reduce_lambda` | `fy_preduce` | `fy_preduce_lambda` |

**Recommendation**: Use polymorphic APIs (`fy_filter`, `fy_map`, `fy_reduce`) with lambdas for most code. Use explicit `_local`/`_gb` variants only when you need to ensure specific allocation strategy.

## Real-World Examples

### Processing User Data

```c
fy_generic users = fy_sequence(
    fy_mapping("name", "Alice", "age", 30, "active", true),
    fy_mapping("name", "Bob", "age", 25, "active", false),
    fy_mapping("name", "Charlie", "age", 35, "active", true),
    fy_mapping("name", "David", "age", 20, "active", true)
);

// Get names of active users over 25
fy_generic active_names = fy_map_lambda(
    fy_filter_lambda(users, fy_get(v, "active", false) && fy_get(v, "age", 0) > 25),
    fy_get(v, "name", fy_invalid)
);

// Iterate and print
const char *name;
fy_foreach(name, active_names) {
    printf("Active user: %s\n", name);
}
// Output:
// Active user: Alice
// Active user: Charlie
```

### Data Aggregation

```c
fy_generic sales = fy_sequence(
    fy_mapping("product", "Widget", "price", 10, "quantity", 5),
    fy_mapping("product", "Gadget", "price", 20, "quantity", 3),
    fy_mapping("product", "Doohickey", "price", 15, "quantity", 2)
);

// Calculate total revenue
int total_revenue = fy_reduce_lambda(sales, 0,
    fy_value(fy_cast(acc, 0) + (fy_get(v, "price", 0) * fy_get(v, "quantity", 0)))
);
// Result: (10*5) + (20*3) + (15*2) = 50 + 60 + 30 = 140
```

### Parallel Processing of Large Datasets

```c
// Large dataset (100K items)
fy_generic large_data = generate_large_dataset(100000);

struct fy_thread_pool *tp = fy_thread_pool_create(NULL);

// Parallel filter + map pipeline
fy_generic result = fy_pmap_lambda(
    fy_pfilter_lambda(large_data, tp, fy_cast(v, 0) > 1000),
    tp,
    fy_value(fy_cast(v, 0) * 2)
);

fy_thread_pool_destroy(tp);
```

**Performance**: See `PARALLEL-REDUCE-RESULTS.md` for benchmarks showing 6-11x speedups on 12-core systems.

## Implementation Details

### How Lambdas Work

**GCC** - Nested function pointers:
```c
#define fy_filter_lambda(_col, _expr) \
    ({ \
        fy_generic_filter_pred_fn _fn = ({ \
            bool __fy_pred_fn(struct fy_generic_builder *gb, fy_generic v) { \
                return (_expr) ; \
            } \
            &__fy_pred_fn; }); \
        fy_local_filter((_col), _fn); \
    })
```

**Clang** - Blocks:
```c
#define fy_filter_lambda(_col, _expr) \
    ({ \
        fy_generic_filter_pred_fn _fn = ^(struct fy_generic_builder *gb, fy_generic v) { \
            return (_expr) ; \
        }; \
        fy_local_filter((_col), _fn); \
    })
```

### How Polymorphic API Works

Uses preprocessor metaprogramming to detect argument count:

```c
// Detects whether first argument is a builder
#define fy_filter(...) \
    (FY_CPP_FOURTH(__VA_ARGS__, fy_gb_filter, fy_local_filter)(__VA_ARGS__))
```

- 2 args: `fy_filter(col, fn)` → calls `fy_local_filter(col, fn)`
- 3 args: `fy_filter(gb, col, fn)` → calls `fy_gb_filter(gb, col, fn)`

## Performance

**Lambda overhead**: **ZERO** - compiles to identical machine code as function pointers

**Polymorphic API overhead**: **ZERO** - macro expansion at compile time

**Parallel performance**: Up to **11x speedup** on 12-core systems (91% efficiency for heavy workloads)

See benchmarks:
- `PARALLEL-REDUCE-RESULTS.md` - Comprehensive parallel performance analysis
- `C-VS-PYTHON-REDUCE.md` - Comparison with Python multiprocessing

## Migration Guide

### From Old Function Pointer Style

**Before**:
```c
static bool my_predicate(struct fy_generic_builder *gb, fy_generic v) {
    return fy_cast(v, 0) > 100;
}

fy_generic result = fy_local_filter(data, my_predicate);
```

**After** (with lambda):
```c
fy_generic result = fy_filter_lambda(data, fy_cast(v, 0) > 100);
```

**After** (with polymorphic API, keeping function):
```c
static bool my_predicate(struct fy_generic_builder *gb, fy_generic v) {
    return fy_cast(v, 0) > 100;
}

fy_generic result = fy_filter(data, my_predicate);  // Simpler!
```

### From `_local` / `_gb` Explicit APIs

**Before**:
```c
// Stack
fy_generic r1 = fy_local_filter(data, pred);
fy_generic r2 = fy_local_map(r1, xform);

// Heap
struct fy_generic_builder *gb = ...;
fy_generic r1 = fy_gb_filter(gb, data, pred);
fy_generic r2 = fy_gb_map(gb, r1, xform);
```

**After** (polymorphic):
```c
// Stack (same code!)
fy_generic r1 = fy_filter(data, pred);
fy_generic r2 = fy_map(r1, xform);

// Heap (same code, just add gb!)
struct fy_generic_builder *gb = ...;
fy_generic r1 = fy_filter(gb, data, pred);
fy_generic r2 = fy_map(gb, r1, xform);
```

**After** (polymorphic + lambdas):
```c
// Stack
fy_generic result = fy_map_lambda(
    fy_filter_lambda(data, /* predicate */),
    /* transform */
);

// Heap
struct fy_generic_builder *gb = ...;
fy_generic result = fy_map_lambda(gb,
    fy_filter_lambda(gb, data, /* predicate */),
    /* transform */
);
```

## Compiler Setup

### GCC (Easy)

No special flags required - just works!

```bash
gcc -std=gnu11 mycode.c -o myapp
```

### Clang (Requires Blocks)

Add `-fblocks` flag and link with Blocks runtime:

```bash
clang -fblocks mycode.c -o myapp -lBlocksRuntime
```

**macOS**: Blocks runtime is built-in
**Linux**: May need to install `libblocksruntime-dev` package

## Best Practices

### When to Use Lambdas

**✅ Good for lambdas**:
- Simple predicates (1-3 lines)
- One-off transformations
- Prototype/exploration code
- Inline pipeline steps

**❌ Better as separate functions**:
- Complex logic (> 5 lines)
- Reused operations
- Code that needs unit testing
- Performance-critical hot paths that benefit from explicit inlining

### Lambda vs Function Pointer Performance

**Identical** - lambdas compile to the same code as function pointers. Choose based on readability, not performance.

### Stack vs Heap Allocation

**Stack** (no builder):
- Temporary results within function scope
- Fast (no allocation overhead)
- Limited lifetime (stack frame)

**Heap** (with builder):
- Results that outlive function scope
- Slightly slower (allocation overhead)
- Persistent until builder destroyed

**Use polymorphic API** (`fy_filter`, not `fy_local_filter` / `fy_gb_filter`) to make switching easy.

## Documentation

**New files**:
- `doc/GENERIC-API-LAMBDAS.md` - Comprehensive lambda documentation (100+ examples)

**Updated files**:
- `doc/GENERIC-API-REFERENCE.md` - Added "Functional Operations" section with polymorphic API and lambda support
- `doc/GENERIC-API-FUNCTIONAL.md` - Added lambda iteration examples

## Summary

Lambda support and the unified polymorphic API bring libfyaml's ergonomics to parity with Python and JavaScript while maintaining C's zero-overhead philosophy. Key advantages:

✅ **Python-level conciseness** - Inline predicates, no boilerplate
✅ **JavaScript-like pipelines** - Declarative data transformations
✅ **Zero runtime cost** - Compiles to optimal machine code
✅ **Compiler choice** - GCC (nested functions) or Clang (Blocks)
✅ **Polymorphic dispatch** - Automatic stack/heap selection
✅ **Parallel scaling** - Up to 11x speedup on multi-core systems
✅ **Type safety** - Compile-time type checking via C11 `_Generic`

**Bottom line**: Modern functional programming in C, with the ergonomics of high-level languages and the performance of systems programming.
