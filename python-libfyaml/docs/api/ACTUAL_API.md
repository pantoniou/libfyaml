# Actual libfyaml Generic API

Based on analysis of `test/libfyaml-test-generic.c`, here's the real API:

## Parsing YAML/JSON to Generic

```c
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
struct fy_generic_op_args args;

memset(&args, 0, sizeof(args));
args.parse.parser_mode = fypm_yaml_1_2;  // or fypm_json
args.parse.multi_document = false;

fy_generic parsed = fy_generic_op_args(gb, FYGBOPF_PARSE, yaml_string, &args);
```

## Type Checking

```c
fy_generic_is_null_type(v)
fy_generic_is_bool_type(v)
fy_generic_is_int_type(v)
fy_generic_is_float_type(v)
fy_generic_is_string(v)
fy_generic_is_sequence(v)
fy_generic_is_mapping(v)
fy_generic_is_valid(v)    // check if not fy_invalid

fy_get_type(v)  // returns FYGT_NULL, FYGT_BOOL, FYGT_INT, FYGT_FLOAT, FYGT_STRING, FYGT_SEQUENCE, FYGT_MAPPING
```

## Value Conversion

```c
// fy_cast(value, default_if_wrong_type)
bool b = fy_cast(v, false);
int i = fy_cast(v, -1);
long long ll = fy_cast(v, (long long)-1);
float f = fy_cast(v, (float)NAN);
double d = fy_cast(v, (double)NAN);
const char *s = fy_cast(v, "");
```

## Sequence Access

```c
// Get count
size_t len = fy_len(seq);

// Get item by index with default
int value = fy_get(seq, 0, -1);          // seq[0] or -1
const char *str = fy_get(seq, 1, "");    // seq[1] or ""

// Get generic item
fy_generic item = fy_get(seq, 2, fy_invalid);

// Direct array access (faster)
fy_generic_sequence_handle seqh = fy_cast(seq, fy_seq_handle_null);
if (seqh) {
    int val = fy_cast(seqh->items[0], -1);
}

// Get items array
const fy_generic *items = fy_generic_sequence_get_items(seq, &count);
```

## Mapping Access

```c
// Get count
size_t len = fy_len(map);

// Lookup by string key with default
int value = fy_get(map, "foo", -1);
const char *str = fy_get(map, "bar", "");

// Get generic value
fy_generic val = fy_get(map, "key", fy_invalid);

// Direct access (faster)
fy_generic_mapping_handle maph = fy_cast(map, fy_map_handle_null);
if (maph) {
    int val = fy_get(maph, "foo", -1);
}

// Get pairs array for iteration
const fy_generic_pair *pairs = fy_generic_mapping_get_pairs(map, &count);
for (size_t i = 0; i < count; i++) {
    fy_generic key = pairs[i].key;
    fy_generic value = pairs[i].value;
    // ...
}
```

## Creating Generics

```c
// Stack/local values (function scope only)
fy_generic v = fy_local_bool(true);
fy_generic v = fy_local_int(100);
fy_generic v = fy_local_float(3.14);
fy_generic v = fy_local_string("hello");
fy_generic v = fy_local_sequence(1, 2, 3, "four");
fy_generic v = fy_local_mapping("foo", 100, "bar", 200);

// Builder-based (persistent)
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
fy_generic v = fy_gb_int_create(gb, 100);
fy_generic v = fy_gb_string_create(gb, "hello");
// ... etc
```

## Handles vs Generics

- `fy_generic` - The main type, a tagged union value
- `fy_generic_sequence_handle` - Pointer to sequence structure with `items[]` array
- `fy_generic_mapping_handle` - Pointer to mapping structure with `pairs[]` array
- Use `fy_cast()` to convert between them

## Key Differences from Original Implementation

1. **No fy_document_to_generic()** - Parse directly with `fy_generic_op_args()`
2. **Use fy_get() not fy_generic_sequence_get()** - Different naming
3. **Use fy_cast() for conversion** - Not fy_generic_get_int() etc
4. **Use fy_generic_mapping_get_pairs()** - Not an iterator struct
5. **Use fy_len()** - Not fy_generic_sequence_count() or fy_generic_mapping_count()
6. **Handles for direct access** - Optional performance optimization

## Example Usage Pattern

```c
// Parse
fy_generic root = fy_generic_op_args(gb, FYGBOPF_PARSE, yaml_str, &args);

// Check type
if (fy_generic_is_mapping(root)) {
    // Iterate
    size_t count;
    const fy_generic_pair *pairs = fy_generic_mapping_get_pairs(root, &count);

    for (size_t i = 0; i < count; i++) {
        const char *key_str = fy_cast(pairs[i].key, "");

        if (fy_generic_is_int_type(pairs[i].value)) {
            int val = fy_cast(pairs[i].value, 0);
            printf("%s: %d\n", key_str, val);
        }
    }
}
```
