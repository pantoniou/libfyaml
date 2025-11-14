# Generic API Utility Functions

This document describes utility functions for working with generic collections, providing Python-like operations for common tasks like extracting keys/values, slicing sequences, and transforming collections.

## Overview

The utility functions provide familiar operations from Python and other high-level languages:

- **Mapping utilities**: Extract keys, values, items, check membership
- **Sequence utilities**: Slicing, first/last, take/drop, reverse
- **Collection utilities**: Concatenation, flattening, filtering
- **Convenience functions**: Common patterns made simple

All utility functions work directly with `fy_generic` values and follow the same immutability principles as functional operations.

## Mapping Utilities

### `fy_keys()` - Extract All Keys

Get a sequence containing all keys from a mapping.

```c
fy_generic fy_keys(fy_generic map);
fy_generic fy_keys(struct fy_generic_builder *gb, fy_generic map);
```

**Parameters**:
- `map` - Mapping to extract keys from

**Returns**: Sequence of keys (in undefined order)

**Examples**:
```c
fy_generic config = fy_mapping(
    "host", "localhost",
    "port", 8080,
    "debug", true
);

fy_generic keys = fy_keys(config);
// keys: ["host", "port", "debug"] (order may vary)

// With builder for persistent result
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
fy_generic persistent_keys = fy_keys(gb, config);
```

**Python equivalent**:
```python
config = {"host": "localhost", "port": 8080, "debug": True}
keys = list(config.keys())
```

### `fy_values()` - Extract All Values

Get a sequence containing all values from a mapping.

```c
fy_generic fy_values(fy_generic map);
fy_generic fy_values(struct fy_generic_builder *gb, fy_generic map);
```

**Parameters**:
- `map` - Mapping to extract values from

**Returns**: Sequence of values (order corresponds to keys)

**Examples**:
```c
fy_generic config = fy_mapping(
    "host", "localhost",
    "port", 8080,
    "debug", true
);

fy_generic values = fy_values(config);
// values: ["localhost", 8080, true] (order may vary)

// Iterate over values
for (size_t i = 0; i < fy_len(values); i++) {
    fy_generic value = fy_get_item(values, i);
    // Process value...
}
```

**Python equivalent**:
```python
config = {"host": "localhost", "port": 8080, "debug": True}
values = list(config.values())
```

### `fy_items()` - Extract Key-Value Pairs

Get a sequence of sequences, where each inner sequence is `[key, value]`.

```c
fy_generic fy_items(fy_generic map);
fy_generic fy_items(struct fy_generic_builder *gb, fy_generic map);
```

**Parameters**:
- `map` - Mapping to extract items from

**Returns**: Sequence of `[key, value]` pairs

**Examples**:
```c
fy_generic config = fy_mapping(
    "host", "localhost",
    "port", 8080
);

fy_generic items = fy_items(config);
// items: [["host", "localhost"], ["port", 8080]]

// Iterate over pairs
for (size_t i = 0; i < fy_len(items); i++) {
    fy_generic pair = fy_get_item(items, i);
    fy_generic key = fy_get_item(pair, 0);
    fy_generic value = fy_get_item(pair, 1);

    printf("%s = %s\n",
        fy_cast(key, ""),
        fy_cast(value, ""));
}
```

**Python equivalent**:
```python
config = {"host": "localhost", "port": 8080}
items = list(config.items())
# [("host", "localhost"), ("port", 8080)]
```

### `fy_contains()` / `fy_has_key()` - Check Membership

Check if a key exists in a mapping.

```c
bool fy_contains(fy_generic map, fy_generic key);
bool fy_has_key(fy_generic map, fy_generic key);  // Alias
```

**Parameters**:
- `map` - Mapping to check
- `key` - Key to look for

**Returns**: `true` if key exists, `false` otherwise

**Examples**:
```c
fy_generic config = fy_mapping(
    "host", "localhost",
    "port", 8080
);

if (fy_contains(config, "port")) {
    int port = fy_map_get(config, "port", 0);
    printf("Port: %d\n", port);
}

// Check before accessing
if (!fy_has_key(config, "database")) {
    // Key doesn't exist, use default config
}
```

**Python equivalent**:
```python
config = {"host": "localhost", "port": 8080}
if "port" in config:
    print(f"Port: {config['port']}")
```

### `fy_merge()` - Merge Mappings

Merge two or more mappings, with later values overwriting earlier ones.

```c
fy_generic fy_merge(fy_generic map1, fy_generic map2);
fy_generic fy_merge(struct fy_generic_builder *gb, fy_generic map1, fy_generic map2);

// Variadic version for multiple maps
fy_generic fy_merge_all(fy_generic maps...);  // Varargs terminated by fy_end
fy_generic fy_merge_all(struct fy_generic_builder *gb, fy_generic maps...);
```

**Parameters**:
- `map1`, `map2`, ... - Mappings to merge (later overwrites earlier)

**Returns**: New mapping with merged contents

**Examples**:
```c
fy_generic defaults = fy_mapping(
    "timeout", 30,
    "retries", 3,
    "debug", false
);

fy_generic user_config = fy_mapping(
    "timeout", 60,
    "verbose", true
);

// Merge: user_config values override defaults
fy_generic final_config = fy_merge(defaults, user_config);
// final_config: {"timeout": 60, "retries": 3, "debug": false, "verbose": true}

// Merge multiple configurations
fy_generic env_config = fy_mapping("debug", true);
fy_generic all = fy_merge_all(defaults, env_config, user_config, fy_end);
```

**Python equivalent**:
```python
defaults = {"timeout": 30, "retries": 3, "debug": False}
user_config = {"timeout": 60, "verbose": True}
final_config = {**defaults, **user_config}
# Or: defaults | user_config (Python 3.9+)
```

## Sequence Utilities

### `fy_slice()` / `fy_subseq()` - Slice Sequence

Extract a subsequence from start to end (exclusive).

```c
fy_generic fy_slice(fy_generic seq, size_t start, size_t end);
fy_generic fy_slice(struct fy_generic_builder *gb, fy_generic seq, size_t start, size_t end);

// Negative indices count from end (like Python)
fy_generic fy_slice_py(fy_generic seq, int start, int end);
```

**Parameters**:
- `seq` - Sequence to slice
- `start` - Starting index (inclusive)
- `end` - Ending index (exclusive), or -1 for end of sequence

**Returns**: New sequence containing elements from `[start, end)`

**Examples**:
```c
fy_generic items = fy_sequence("a", "b", "c", "d", "e");

// Get middle elements
fy_generic middle = fy_slice(items, 1, 4);
// middle: ["b", "c", "d"]

// Get first 3 elements
fy_generic first_three = fy_slice(items, 0, 3);
// first_three: ["a", "b", "c"]

// Get from index 2 to end
fy_generic from_two = fy_slice(items, 2, fy_len(items));
// from_two: ["c", "d", "e"]

// Python-style negative indices
fy_generic last_two = fy_slice_py(items, -2, -1);  // ["d"]
```

**Python equivalent**:
```python
items = ["a", "b", "c", "d", "e"]
middle = items[1:4]        # ["b", "c", "d"]
first_three = items[:3]     # ["a", "b", "c"]
from_two = items[2:]        # ["c", "d", "e"]
last_two = items[-2:-1]     # ["d"]
```

### `fy_first()` / `fy_last()` - Get First/Last Element

```c
fy_generic fy_first(fy_generic seq);
fy_generic fy_last(fy_generic seq);
```

**Parameters**:
- `seq` - Sequence to extract from

**Returns**: First or last element, or `fy_invalid` if sequence is empty

**Examples**:
```c
fy_generic items = fy_sequence("alice", "bob", "charlie");

fy_generic first = fy_first(items);   // "alice"
fy_generic last = fy_last(items);     // "charlie"

// Safe handling of empty sequences
fy_generic empty = fy_sequence();
fy_generic none = fy_first(empty);    // fy_invalid

if (!fy_is_invalid(none)) {
    // Process first element
}
```

**Python equivalent**:
```python
items = ["alice", "bob", "charlie"]
first = items[0]    # "alice"
last = items[-1]    # "charlie"

# With safety
first = items[0] if items else None
```

### `fy_rest()` / `fy_tail()` - Get All But First

```c
fy_generic fy_rest(fy_generic seq);
fy_generic fy_tail(fy_generic seq);  // Alias
```

**Parameters**:
- `seq` - Sequence to extract from

**Returns**: New sequence without first element, or empty sequence if input has ≤1 elements

**Examples**:
```c
fy_generic items = fy_sequence("alice", "bob", "charlie");

fy_generic rest = fy_rest(items);
// rest: ["bob", "charlie"]

// Useful for recursive patterns
void process_all(fy_generic seq) {
    if (fy_len(seq) == 0) return;

    fy_generic first = fy_first(seq);
    process_item(first);

    process_all(fy_rest(seq));  // Recurse on tail
}
```

**Functional language equivalent**:
```clojure
; Clojure
(rest [1 2 3])  ; (2 3)
```

### `fy_take()` / `fy_drop()` - Take/Drop N Elements

```c
fy_generic fy_take(fy_generic seq, size_t n);
fy_generic fy_drop(fy_generic seq, size_t n);
```

**Parameters**:
- `seq` - Sequence to extract from
- `n` - Number of elements to take/drop

**Returns**: New sequence with first `n` elements (take) or without first `n` elements (drop)

**Examples**:
```c
fy_generic items = fy_sequence(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);

fy_generic first_five = fy_take(items, 5);
// first_five: [1, 2, 3, 4, 5]

fy_generic skip_three = fy_drop(items, 3);
// skip_three: [4, 5, 6, 7, 8, 9, 10]

// Combine take and drop for sliding window
fy_generic window = fy_take(fy_drop(items, 2), 3);
// window: [3, 4, 5]
```

**Python equivalent**:
```python
items = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
first_five = items[:5]
skip_three = items[3:]
window = items[2:5]
```

### `fy_reverse()` - Reverse Sequence

```c
fy_generic fy_reverse(fy_generic seq);
fy_generic fy_reverse(struct fy_generic_builder *gb, fy_generic seq);
```

**Parameters**:
- `seq` - Sequence to reverse

**Returns**: New sequence with elements in reverse order

**Examples**:
```c
fy_generic items = fy_sequence("a", "b", "c", "d");

fy_generic reversed = fy_reverse(items);
// reversed: ["d", "c", "b", "a"]

// Original unchanged
// items: ["a", "b", "c", "d"]
```

**Python equivalent**:
```python
items = ["a", "b", "c", "d"]
reversed_items = list(reversed(items))
# Or: items[::-1]
```

### `fy_concat()` - Concatenate Sequences

```c
fy_generic fy_concat(fy_generic seq1, fy_generic seq2);
fy_generic fy_concat(struct fy_generic_builder *gb, fy_generic seq1, fy_generic seq2);

// Variadic version
fy_generic fy_concat_all(fy_generic seqs...);  // Varargs terminated by fy_end
```

**Parameters**:
- `seq1`, `seq2`, ... - Sequences to concatenate

**Returns**: New sequence with all elements from all input sequences

**Examples**:
```c
fy_generic first = fy_sequence("a", "b", "c");
fy_generic second = fy_sequence("d", "e");
fy_generic third = fy_sequence("f");

fy_generic combined = fy_concat(first, second);
// combined: ["a", "b", "c", "d", "e"]

// Concatenate multiple sequences
fy_generic all = fy_concat_all(first, second, third, fy_end);
// all: ["a", "b", "c", "d", "e", "f"]
```

**Python equivalent**:
```python
first = ["a", "b", "c"]
second = ["d", "e"]
combined = first + second
# Or: [*first, *second]
```

### `fy_flatten()` - Flatten Nested Sequences

```c
fy_generic fy_flatten(fy_generic seq);
fy_generic fy_flatten(struct fy_generic_builder *gb, fy_generic seq);

// Flatten to specific depth
fy_generic fy_flatten_depth(fy_generic seq, int depth);
```

**Parameters**:
- `seq` - Sequence potentially containing nested sequences
- `depth` - How many levels to flatten (default: all levels)

**Returns**: Flattened sequence

**Examples**:
```c
fy_generic nested = fy_sequence(
    1,
    fy_sequence(2, 3),
    fy_sequence(4, fy_sequence(5, 6)),
    7
);

fy_generic flat = fy_flatten(nested);
// flat: [1, 2, 3, 4, 5, 6, 7]

// Flatten only one level
fy_generic one_level = fy_flatten_depth(nested, 1);
// one_level: [1, 2, 3, 4, fy_sequence(5, 6), 7]
```

**Python equivalent**:
```python
# Python doesn't have built-in flatten, but:
nested = [1, [2, 3], [4, [5, 6]], 7]

# Using itertools
from itertools import chain
flat = list(chain.from_iterable(nested))  # One level only

# Or custom recursive function for full flatten
```

## Collection Utilities

### `fy_count()` - Count Elements Matching Predicate

Count elements that satisfy a predicate function.

```c
typedef bool (*fy_predicate_fn)(fy_generic value, void *ctx);

size_t fy_count(fy_generic collection, fy_predicate_fn pred, void *ctx);
```

**Parameters**:
- `collection` - Sequence or mapping to count in
- `pred` - Predicate function returning true for elements to count
- `ctx` - User context passed to predicate

**Returns**: Number of elements matching predicate

**Examples**:
```c
bool is_active(fy_generic user, void *ctx) {
    return fy_map_get(user, "active", false);
}

fy_generic users = fy_sequence(
    fy_mapping("name", "alice", "active", true),
    fy_mapping("name", "bob", "active", false),
    fy_mapping("name", "charlie", "active", true)
);

size_t active_count = fy_count(users, is_active, NULL);
// active_count: 2
```

**Python equivalent**:
```python
users = [
    {"name": "alice", "active": True},
    {"name": "bob", "active": False},
    {"name": "charlie", "active": True}
]
active_count = sum(1 for u in users if u["active"])
```

### `fy_filter()` - Filter Collection

Create new collection with only elements matching predicate.

```c
typedef bool (*fy_predicate_fn)(fy_generic value, void *ctx);

fy_generic fy_filter(fy_generic collection, fy_predicate_fn pred, void *ctx);
fy_generic fy_filter(struct fy_generic_builder *gb, fy_generic collection,
                     fy_predicate_fn pred, void *ctx);
```

**Parameters**:
- `collection` - Collection to filter
- `pred` - Predicate function
- `ctx` - User context

**Returns**: New sequence with matching elements

**Examples**:
```c
bool is_even(fy_generic num, void *ctx) {
    return fy_cast(num, 0) % 2 == 0;
}

fy_generic numbers = fy_sequence(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
fy_generic evens = fy_filter(numbers, is_even, NULL);
// evens: [2, 4, 6, 8, 10]
```

**Python equivalent**:
```python
numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
evens = [n for n in numbers if n % 2 == 0]
# Or: list(filter(lambda n: n % 2 == 0, numbers))
```

### `fy_map()` - Transform Collection

Apply a function to each element, creating new collection with results.

```c
typedef fy_generic (*fy_transform_fn)(fy_generic value, void *ctx);

fy_generic fy_map(fy_generic collection, fy_transform_fn fn, void *ctx);
fy_generic fy_map(struct fy_generic_builder *gb, fy_generic collection,
                  fy_transform_fn fn, void *ctx);
```

**Parameters**:
- `collection` - Collection to transform
- `fn` - Transform function
- `ctx` - User context

**Returns**: New sequence with transformed elements

**Examples**:
```c
fy_generic double_it(fy_generic num, void *ctx) {
    return fy_to_generic(fy_cast(num, 0) * 2);
}

fy_generic numbers = fy_sequence(1, 2, 3, 4, 5);
fy_generic doubled = fy_map(numbers, double_it, NULL);
// doubled: [2, 4, 6, 8, 10]

// Extract field from mapping
fy_generic get_name(fy_generic user, void *ctx) {
    return fy_map_get(user, "name", "");
}

fy_generic users = fy_sequence(
    fy_mapping("name", "alice", "age", 30),
    fy_mapping("name", "bob", "age", 25)
);
fy_generic names = fy_map(users, get_name, NULL);
// names: ["alice", "bob"]
```

**Python equivalent**:
```python
numbers = [1, 2, 3, 4, 5]
doubled = [n * 2 for n in numbers]
# Or: list(map(lambda n: n * 2, numbers))

users = [{"name": "alice", "age": 30}, {"name": "bob", "age": 25}]
names = [u["name"] for u in users]
```

### `fy_reduce()` - Reduce Collection to Single Value

```c
typedef fy_generic (*fy_reduce_fn)(fy_generic acc, fy_generic value, void *ctx);

fy_generic fy_reduce(fy_generic collection, fy_reduce_fn fn,
                     fy_generic init, void *ctx);
```

**Parameters**:
- `collection` - Collection to reduce
- `fn` - Reduction function taking `(accumulator, current_value, context)`
- `init` - Initial accumulator value
- `ctx` - User context

**Returns**: Final accumulated value

**Examples**:
```c
fy_generic sum_reducer(fy_generic acc, fy_generic num, void *ctx) {
    return fy_to_generic(fy_cast(acc, 0) + fy_cast(num, 0));
}

fy_generic numbers = fy_sequence(1, 2, 3, 4, 5);
fy_generic sum = fy_reduce(numbers, sum_reducer, fy_to_generic(0), NULL);
// sum: 15

// Build mapping from sequence of pairs
fy_generic pairs_to_map(fy_generic acc, fy_generic pair, void *ctx) {
    struct fy_generic_builder *gb = ctx;
    fy_generic key = fy_get_item(pair, 0);
    fy_generic value = fy_get_item(pair, 1);
    return fy_assoc(gb, acc, key, value);
}

struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
fy_generic pairs = fy_sequence(
    fy_sequence("a", 1),
    fy_sequence("b", 2),
    fy_sequence("c", 3)
);
fy_generic map = fy_reduce(pairs, pairs_to_map, fy_mapping(), gb);
// map: {"a": 1, "b": 2, "c": 3}
```

**Python equivalent**:
```python
from functools import reduce
numbers = [1, 2, 3, 4, 5]
sum_total = reduce(lambda acc, n: acc + n, numbers, 0)
# Or just: sum(numbers)
```

## Convenience Functions

### `fy_empty()` - Check if Empty

```c
bool fy_empty(fy_generic collection);
```

**Parameters**:
- `collection` - Sequence, mapping, or string to check

**Returns**: `true` if collection has no elements

**Examples**:
```c
fy_generic empty_seq = fy_sequence();
fy_generic empty_map = fy_mapping();

if (fy_empty(empty_seq)) {
    // Handle empty case
}

// Check before processing
if (!fy_empty(results)) {
    fy_generic first = fy_first(results);
    process(first);
}
```

**Python equivalent**:
```python
empty_list = []
if not empty_list:  # or: if len(empty_list) == 0
    # Handle empty case
```

### `fy_distinct()` / `fy_unique()` - Remove Duplicates

```c
fy_generic fy_distinct(fy_generic seq);
fy_generic fy_unique(fy_generic seq);  // Alias
```

**Parameters**:
- `seq` - Sequence potentially containing duplicates

**Returns**: New sequence with duplicates removed (preserves first occurrence order)

**Examples**:
```c
fy_generic items = fy_sequence(1, 2, 3, 2, 4, 1, 5);
fy_generic unique = fy_distinct(items);
// unique: [1, 2, 3, 4, 5]

// Works with any comparable values
fy_generic words = fy_sequence("apple", "banana", "apple", "cherry");
fy_generic unique_words = fy_unique(words);
// unique_words: ["apple", "banana", "cherry"]
```

**Python equivalent**:
```python
items = [1, 2, 3, 2, 4, 1, 5]
unique = list(dict.fromkeys(items))  # Preserves order
# Or: list(set(items))  # Doesn't preserve order
```

### `fy_zip()` - Zip Multiple Sequences

Combine multiple sequences element-wise.

```c
fy_generic fy_zip(fy_generic seq1, fy_generic seq2);
fy_generic fy_zip_all(fy_generic seqs...);  // Varargs
```

**Parameters**:
- `seq1`, `seq2`, ... - Sequences to zip together

**Returns**: Sequence of sequences, where each inner sequence contains corresponding elements

**Examples**:
```c
fy_generic names = fy_sequence("alice", "bob", "charlie");
fy_generic ages = fy_sequence(30, 25, 35);

fy_generic zipped = fy_zip(names, ages);
// zipped: [["alice", 30], ["bob", 25], ["charlie", 35]]

// Convert to mapping
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
fy_generic map = fy_mapping();
for (size_t i = 0; i < fy_len(zipped); i++) {
    fy_generic pair = fy_get_item(zipped, i);
    fy_generic key = fy_get_item(pair, 0);
    fy_generic val = fy_get_item(pair, 1);
    map = fy_assoc(gb, map, key, val);
}
// map: {"alice": 30, "bob": 25, "charlie": 35}
```

**Python equivalent**:
```python
names = ["alice", "bob", "charlie"]
ages = [30, 25, 35]
zipped = list(zip(names, ages))
# [("alice", 30), ("bob", 25), ("charlie", 35)]

# Convert to dict
mapping = dict(zip(names, ages))
```

### `fy_group_by()` - Group by Key Function

Group collection elements by the result of a key function.

```c
typedef fy_generic (*fy_key_fn)(fy_generic value, void *ctx);

fy_generic fy_group_by(fy_generic collection, fy_key_fn fn, void *ctx);
```

**Parameters**:
- `collection` - Collection to group
- `fn` - Function extracting grouping key from each element
- `ctx` - User context

**Returns**: Mapping where keys are grouping values and values are sequences of elements

**Examples**:
```c
fy_generic get_age_group(fy_generic user, void *ctx) {
    int age = fy_map_get(user, "age", 0);
    return fy_to_generic(age / 10 * 10);  // Group by decade
}

fy_generic users = fy_sequence(
    fy_mapping("name", "alice", "age", 25),
    fy_mapping("name", "bob", "age", 32),
    fy_mapping("name", "charlie", "age", 28),
    fy_mapping("name", "dave", "age", 35)
);

fy_generic by_decade = fy_group_by(users, get_age_group, NULL);
// by_decade: {
//   20: [{"name": "alice", "age": 25}, {"name": "charlie", "age": 28}],
//   30: [{"name": "bob", "age": 32}, {"name": "dave", "age": 35}]
// }
```

**Python equivalent**:
```python
from itertools import groupby
users = [
    {"name": "alice", "age": 25},
    {"name": "bob", "age": 32},
    {"name": "charlie", "age": 28},
    {"name": "dave", "age": 35}
]

# Using dict comprehension
from collections import defaultdict
by_decade = defaultdict(list)
for u in users:
    by_decade[u["age"] // 10 * 10].append(u)
```

## Usage Patterns

### Pattern: Pipeline Transformations

Chain utility functions for data transformation pipelines:

```c
struct fy_generic_builder *gb = fy_generic_builder_create(NULL);

fy_generic users = load_users();

// Pipeline: filter active, extract ages, remove duplicates, sort
fy_generic active_ages = fy_distinct(
    fy_map(
        fy_filter(users, is_active, NULL),
        extract_age,
        NULL
    )
);
```

### Pattern: Dictionary Comprehension

```c
// Python: {k: v*2 for k, v in items.items() if v > 10}

fy_generic items = fy_mapping("a", 15, "b", 5, "c", 20);

struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
fy_generic result = fy_mapping();

fy_generic keys_seq = fy_keys(items);
for (size_t i = 0; i < fy_len(keys_seq); i++) {
    fy_generic key = fy_get_item(keys_seq, i);
    int value = fy_map_get(items, key, 0);

    if (value > 10) {
        result = fy_assoc(gb, result, key, value * 2);
    }
}
// result: {"a": 30, "c": 40}
```

### Pattern: List Comprehension

```c
// Python: [x*2 for x in numbers if x % 2 == 0]

fy_generic numbers = fy_sequence(1, 2, 3, 4, 5, 6);

struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
fy_generic result = fy_sequence();

for (size_t i = 0; i < fy_len(numbers); i++) {
    int num = fy_cast(fy_get_item(numbers, i), 0);
    if (num % 2 == 0) {
        result = fy_conj(gb, result, num * 2);
    }
}
// result: [4, 8, 12]

// Or using utilities:
fy_generic evens = fy_filter(numbers, is_even, NULL);
fy_generic doubled = fy_map(evens, double_it, NULL);
```

## Python Method Equivalence Table

| Python (dict) | libfyaml | Notes |
|---------------|----------|-------|
| `d.keys()` | `fy_keys(d)` | Returns sequence |
| `d.values()` | `fy_values(d)` | Returns sequence |
| `d.items()` | `fy_items(d)` | Returns sequence of pairs |
| `key in d` | `fy_contains(d, key)` | Boolean check |
| `d.get(k, default)` | `fy_map_get(d, k, default)` | With default value |
| `{**d1, **d2}` | `fy_merge(d1, d2)` | Merge mappings |

| Python (list) | libfyaml | Notes |
|---------------|----------|-------|
| `lst[1:4]` | `fy_slice(lst, 1, 4)` | Slice sequence |
| `lst[0]` | `fy_first(lst)` | First element |
| `lst[-1]` | `fy_last(lst)` | Last element |
| `lst[1:]` | `fy_rest(lst)` | All but first |
| `lst[:n]` | `fy_take(lst, n)` | First n elements |
| `lst[n:]` | `fy_drop(lst, n)` | Skip first n |
| `list(reversed(lst))` | `fy_reverse(lst)` | Reverse order |
| `lst1 + lst2` | `fy_concat(lst1, lst2)` | Concatenate |
| `[x for x in lst if pred(x)]` | `fy_filter(lst, pred, NULL)` | Filter |
| `[f(x) for x in lst]` | `fy_map(lst, f, NULL)` | Transform |
| `list(set(lst))` | `fy_distinct(lst)` | Remove duplicates |
| `zip(a, b)` | `fy_zip(a, b)` | Zip sequences |

## See Also

- [GENERIC-API.md](GENERIC-API.md) - High-level overview
- [GENERIC-API-FUNCTIONAL.md](GENERIC-API-FUNCTIONAL.md) - Functional operations
- [GENERIC-API-REFERENCE.md](GENERIC-API-REFERENCE.md) - Core API reference
- [GENERIC-API-PATTERNS.md](GENERIC-API-PATTERNS.md) - Usage patterns
