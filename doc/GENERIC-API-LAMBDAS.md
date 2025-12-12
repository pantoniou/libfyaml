# Generic API Lambda Support

## Overview

libfyaml provides lambda-like syntax for filter/map/reduce operations through compiler extensions, bringing modern functional programming patterns to C. This makes functional operations as concise and readable as Python or JavaScript, while maintaining zero runtime overhead.

## Supported Compilers

**GCC** (version 4.0+):
- Uses nested function extension
- No special compiler flags required
- Works on all GCC-supported platforms

**Clang** (version 3.0+):
- Uses Blocks extension (same as Apple's Objective-C blocks)
- Requires `-fblocks` compiler flag
- May require `-lBlocksRuntime` linker flag (platform-dependent)

## Quick Examples

### Filter with Lambda

**Without lambda** (traditional function pointer):
```c
// Define separate function
static bool is_over_100(struct fy_generic_builder *gb, fy_generic v) {
    return fy_cast(v, 0) > 100;
}

// Use it
fy_generic result = fy_filter(seq, is_over_100);
```

**With lambda** (inline):
```c
// Define logic inline
fy_generic result = fy_filter_lambda(seq, {
    return fy_cast(v, 0) > 100;
});
```

### Map with Lambda

```c
// Double all values inline
fy_generic doubled = fy_map_lambda(seq, {
    return fy_value(fy_cast(v, 0) * 2);
});
```

### Reduce with Lambda

```c
// Sum values inline
int sum = fy_reduce_lambda(seq, 0, {
    return fy_value(fy_cast(acc, 0) + fy_cast(v, 0));
});
```

## Lambda API Reference

### Filter Lambdas

**Signature**:
```c
fy_generic fy_filter_lambda(col, { body });
fy_generic fy_filter_lambda(gb, col, { body });
```

**Available variables in body**:
- `gb` - Generic builder (struct fy_generic_builder *)
- `v` - Current value (fy_generic)

**Return**: `bool` - true to include item, false to exclude

**Examples**:
```c
fy_generic data = fy_sequence(5, 15, 25, 35, 45);

// Filter values > 20
fy_generic filtered = fy_filter_lambda(data, {
    return fy_cast(v, 0) > 20;
});
// Result: [25, 35, 45]

// Filter with builder (persistent result)
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
fy_generic filtered2 = fy_filter_lambda(gb, data, {
    return fy_cast(v, 0) > 20;
});
```

**Multi-line body**:
```c
fy_generic complex_filter = fy_filter_lambda(data, {
    int val = fy_cast(v, 0);
    if (val < 10) return false;
    if (val > 100) return false;
    return (val % 2) == 0;  // Keep even values between 10-100
});
```

### Map Lambdas

**Signature**:
```c
fy_generic fy_map_lambda(col, { body });
fy_generic fy_map_lambda(gb, col, { body });
```

**Available variables in body**:
- `gb` - Generic builder (struct fy_generic_builder *)
- `v` - Current value (fy_generic)

**Return**: `fy_generic` - transformed value

**Examples**:
```c
fy_generic numbers = fy_sequence(1, 2, 3, 4, 5);

// Double all values
fy_generic doubled = fy_map_lambda(numbers, {
    return fy_value(fy_cast(v, 0) * 2);
});
// Result: [2, 4, 6, 8, 10]

// Convert to strings
fy_generic strings = fy_map_lambda(numbers, {
    char buf[32];
    snprintf(buf, sizeof(buf), "num_%d", fy_cast(v, 0));
    return fy_string(buf);
});
// Result: ["num_1", "num_2", "num_3", "num_4", "num_5"]

// Complex transformation
fy_generic transformed = fy_map_lambda(numbers, {
    int val = fy_cast(v, 0);
    if (val < 3) {
        return fy_string("small");
    } else {
        return fy_string("large");
    }
});
// Result: ["small", "small", "large", "large", "large"]
```

### Reduce Lambdas

**Signature**:
```c
fy_generic fy_reduce_lambda(col, init_value, { body });
fy_generic fy_reduce_lambda(gb, col, init_value, { body });
```

**Available variables in body**:
- `gb` - Generic builder (struct fy_generic_builder *)
- `acc` - Accumulator value (fy_generic)
- `v` - Current value (fy_generic)

**Return**: `fy_generic` - new accumulator value

**Examples**:
```c
fy_generic numbers = fy_sequence(1, 2, 3, 4, 5);

// Sum
int sum = fy_reduce_lambda(numbers, 0, {
    return fy_value(fy_cast(acc, 0) + fy_cast(v, 0));
});
// Result: 15

// Product
int product = fy_reduce_lambda(numbers, 1, {
    return fy_value(fy_cast(acc, 0) * fy_cast(v, 0));
});
// Result: 120

// Find maximum
int max = fy_reduce_lambda(numbers, INT_MIN, {
    int a = fy_cast(acc, INT_MIN);
    int b = fy_cast(v, INT_MIN);
    return fy_value(a > b ? a : b);
});
// Result: 5

// Build a string
fy_generic words = fy_sequence("Hello", "beautiful", "world");
fy_generic sentence = fy_reduce_lambda(words, fy_string(""), {
    const char *accumulated = fy_cast(acc, "");
    const char *word = fy_cast(v, "");
    char buf[256];
    if (strlen(accumulated) == 0) {
        snprintf(buf, sizeof(buf), "%s", word);
    } else {
        snprintf(buf, sizeof(buf), "%s %s", accumulated, word);
    }
    return fy_string(buf);
});
// Result: "Hello beautiful world"
```

## Parallel Lambda Operations

All lambda operations have parallel versions that execute on multiple threads:

### Parallel Filter Lambda

```c
struct fy_thread_pool *tp = fy_thread_pool_create(NULL);

fy_generic large_data = fy_sequence(/* ... 100K items ... */);

// Parallel filter (uses thread pool)
fy_generic filtered = fy_pfilter_lambda(large_data, tp, {
    return fy_cast(v, 0) > 100;
});

fy_thread_pool_destroy(tp);
```

### Parallel Map Lambda

```c
// Parallel map with heavy computation
fy_generic transformed = fy_pmap_lambda(large_data, tp, {
    int val = fy_cast(v, 0);
    double result = val;
    // Heavy computation
    for (int i = 0; i < 100; i++) {
        result = sin(result) * cos(result);
    }
    return fy_value((int)result);
});
```

### Parallel Reduce Lambda

```c
// Parallel reduce - reducer must be associative!
int sum = fy_preduce_lambda(large_data, 0, tp, {
    return fy_value(fy_cast(acc, 0) + fy_cast(v, 0));
});
```

**Important**: Parallel reduce requires the reducer function to be associative:
```
reduce(a, reduce(b, c)) == reduce(reduce(a, b), c)
```

## Functional Pipelines with Lambdas

Lambdas enable clean, declarative functional pipelines:

```c
fy_generic data = fy_sequence(1, 50, 101, 200, 3, 75);

// Filter then map in one expression
fy_generic result = fy_map_lambda(
    fy_filter_lambda(data, {
        return fy_cast(v, 0) > 100;
    }),
    {
        return fy_value(fy_cast(v, 0) * 10);
    }
);
// Steps:
// 1. Filter: [101, 200]
// 2. Map: [1010, 2000]
```

**Complex pipeline**:
```c
fy_generic users = fy_sequence(
    fy_mapping("name", "Alice", "age", 30, "active", true),
    fy_mapping("name", "Bob", "age", 25, "active", false),
    fy_mapping("name", "Charlie", "age", 35, "active", true),
    fy_mapping("name", "David", "age", 20, "active", true)
);

// Get names of active users over 25, in uppercase
fy_generic active_names = fy_map_lambda(
    fy_map_lambda(
        fy_filter_lambda(users, {
            return fy_get(v, "active", false) && fy_get(v, "age", 0) > 25;
        }),
        {
            return fy_get(v, "name", fy_invalid);
        }
    ),
    {
        const char *name = fy_cast(v, "");
        char buf[256];
        for (int i = 0; name[i]; i++) {
            buf[i] = toupper(name[i]);
        }
        buf[strlen(name)] = '\0';
        return fy_string(buf);
    }
);
// Result: ["ALICE", "CHARLIE"]
```

## Iteration with Lambdas

Combine `fy_foreach` with lambda-filtered/mapped results:

```c
fy_generic data = fy_sequence(1, 50, 101, 200, 3);

// Filter with lambda, iterate with foreach
fy_generic filtered = fy_filter_lambda(data, {
    return fy_cast(v, 0) > 100;
});

int value;
fy_foreach(value, filtered) {
    printf("Large value: %d\n", value);
}
// Output:
// Large value: 101
// Large value: 200

// One-liner: filter, map, and iterate
const char *str;
fy_foreach(str,
    fy_map_lambda(
        fy_filter_lambda(data, { return fy_cast(v, 0) > 50; }),
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "VAL_%d", fy_cast(v, 0));
            return fy_string(buf);
        }
    )
) {
    printf("%s\n", str);
}
// Output:
// VAL_101
// VAL_200
```

## Comparison with Other Languages

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

### libfyaml

```c
// Filter
fy_generic filtered = fy_filter_lambda(data, { return fy_cast(v, 0) > 100; });

// Map
fy_generic doubled = fy_map_lambda(data, { return fy_value(fy_cast(v, 0) * 2); });

// Reduce
int sum = fy_reduce_lambda(data, 0, { return fy_value(fy_cast(acc, 0) + fy_cast(v, 0)); });

// Pipeline
fy_generic result = fy_map_lambda(
    fy_filter_lambda(data, { return fy_cast(v, 0) > 100; }),
    { return fy_value(fy_cast(v, 0) * 10); }
);
```

**Nearly identical!**

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

### libfyaml

```c
// Filter
fy_generic filtered = fy_filter_lambda(data, { return fy_cast(v, 0) > 100; });

// Map
fy_generic doubled = fy_map_lambda(data, { return fy_value(fy_cast(v, 0) * 2); });

// Reduce
int sum = fy_reduce_lambda(data, 0, { return fy_value(fy_cast(acc, 0) + fy_cast(v, 0)); });

// Pipeline (no chaining syntax, but same logic)
fy_generic result = fy_map_lambda(
    fy_filter_lambda(data, { return fy_cast(v, 0) > 100; }),
    { return fy_value(fy_cast(v, 0) * 10); }
);
```

## Implementation Details

### GCC Nested Functions

GCC implements lambdas using nested function pointers:

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

**Characteristics**:
- Zero runtime overhead
- Function is inlined at call site
- Can capture local variables (closure)
- Stack-allocated (no heap allocations)

### Clang Blocks

Clang implements lambdas using Blocks extension:

```c
#define fy_filter_lambda(_col, _expr) \
    ({ \
        fy_generic_filter_pred_fn _fn = ^(struct fy_generic_builder *gb, fy_generic v) { \
            return (_expr) ; \
        }; \
        fy_local_filter((_col), _fn); \
    })
```

**Characteristics**:
- Requires `-fblocks` compiler flag
- May require `-lBlocksRuntime` for block runtime library
- Can capture variables by value or reference
- Heap-allocated if block escapes scope (automatic management)

## Performance

Lambda operations have the **same performance as function pointers**:

**Lambda**:
```c
fy_generic result = fy_filter_lambda(data, { return fy_cast(v, 0) > 100; });
```

**Function pointer**:
```c
static bool pred(struct fy_generic_builder *gb, fy_generic v) {
    return fy_cast(v, 0) > 100;
}
fy_generic result = fy_filter(data, pred);
```

Both compile to identical machine code after optimization.

**Parallel performance is identical to hand-written parallel code** - see `PARALLEL-REDUCE-RESULTS.md` for benchmarks.

## Best Practices

### When to Use Lambdas

**✅ Use lambdas for**:
- Simple, one-off operations
- Inline logic that doesn't need reuse
- Declarative pipelines
- Prototyping and exploration

```c
// Good: simple inline logic
fy_generic even = fy_filter_lambda(data, { return (fy_cast(v, 0) % 2) == 0; });
```

**❌ Avoid lambdas for**:
- Complex logic (> 5-10 lines)
- Reused predicates/transforms
- Logic that needs unit testing

```c
// Bad: complex logic in lambda (use separate function instead)
fy_generic result = fy_filter_lambda(data, {
    int val = fy_cast(v, 0);
    if (val < 10) return false;
    if (val > 100 && (val % 7) == 0) return true;
    if ((val >= 10 && val <= 50) && (val % 3) == 0) return true;
    return false;
});

// Better: extract to named function
static bool complex_predicate(struct fy_generic_builder *gb, fy_generic v) {
    int val = fy_cast(v, 0);
    if (val < 10) return false;
    if (val > 100 && (val % 7) == 0) return true;
    if ((val >= 10 && val <= 50) && (val % 3) == 0) return true;
    return false;
}
fy_generic result = fy_filter(data, complex_predicate);
```

### Capturing Variables

Lambdas can capture local variables:

```c
int threshold = 100;
fy_generic filtered = fy_filter_lambda(data, {
    return fy_cast(v, 0) > threshold;  // Captures 'threshold'
});

// With multiple captures
int min_val = 10;
int max_val = 100;
fy_generic in_range = fy_filter_lambda(data, {
    int val = fy_cast(v, 0);
    return val >= min_val && val <= max_val;
});
```

### Error Handling

Return `fy_invalid` to signal errors:

```c
fy_generic safe_divide = fy_map_lambda(data, {
    int val = fy_cast(v, 0);
    if (val == 0) {
        return fy_invalid;  // Signal error
    }
    return fy_value(1000 / val);
});

// Check result
if (fy_generic_is_invalid(safe_divide)) {
    // Handle error (division by zero occurred)
}
```

## Advanced Patterns

### Currying with Lambdas

```c
// Create a "threshold filter" generator
#define make_threshold_filter(threshold_val) \
    fy_filter_lambda(data, { return fy_cast(v, 0) > (threshold_val); })

fy_generic data = fy_sequence(5, 15, 25, 35, 45);

// Use with different thresholds
fy_generic over_10 = make_threshold_filter(10);
fy_generic over_30 = make_threshold_filter(30);
```

### Composition

```c
// Compose filter and map operations
#define filter_and_double(collection, pred_body) \
    fy_map_lambda( \
        fy_filter_lambda((collection), pred_body), \
        { return fy_value(fy_cast(v, 0) * 2); } \
    )

fy_generic result = filter_and_double(data, { return fy_cast(v, 0) > 20; });
// Filter: [25, 35, 45]
// Map: [50, 70, 90]
```

### State Machines with Reduce

```c
typedef enum { INIT, PROCESSING, DONE } state_t;

fy_generic state_machine = fy_reduce_lambda(events, fy_int(INIT), {
    state_t current_state = (state_t)fy_cast(acc, INIT);
    int event = fy_cast(v, 0);

    switch (current_state) {
    case INIT:
        return fy_int(event == 1 ? PROCESSING : INIT);
    case PROCESSING:
        return fy_int(event == 2 ? DONE : PROCESSING);
    case DONE:
        return fy_int(DONE);
    }
});
```

## Troubleshooting

### Clang: Blocks Not Supported

**Error**: `blocks are not supported`

**Solution**: Add `-fblocks` to compiler flags and `-lBlocksRuntime` to linker flags.

### GCC: Statement Expression Error

**Error**: `ISO C forbids statement expressions inside expressions`

**Solution**: This is a GCC extension. Use `-std=gnu99` or later instead of `-std=c99`.

### Variable Not Captured

**Problem**: Lambda can't see local variables.

**Cause**: Syntax error in lambda body.

**Example**:
```c
int threshold = 100;

// Wrong: missing 'return'
fy_generic bad = fy_filter_lambda(data, {
    fy_cast(v, 0) > threshold;  // Missing 'return'!
});

// Correct:
fy_generic good = fy_filter_lambda(data, {
    return fy_cast(v, 0) > threshold;
});
```

## See Also

- [GENERIC-API-FUNCTIONAL.md](GENERIC-API-FUNCTIONAL.md) - Functional operations
- [GENERIC-API-EXAMPLES.md](GENERIC-API-EXAMPLES.md) - More examples
- [PARALLEL-REDUCE-RESULTS.md](PARALLEL-REDUCE-RESULTS.md) - Performance benchmarks
- [UNIFIED-API-MIGRATION.md](UNIFIED-API-MIGRATION.md) - Parallel operations API
