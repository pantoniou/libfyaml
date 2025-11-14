# Generic API - Missing Operations & Roadmap

This document tracks operations that are either:
1. **Documented but not implemented** - Already specified in GENERIC-API-UTILITIES.md
2. **Planned additions** - Common operations from modern languages (Python, JavaScript, Rust, Clojure)

## Status: Documented but Not Implemented

These operations are already documented in `GENERIC-API-UTILITIES.md` but need implementation.

### Flattening Operations

#### `fy_flatten()` - Flatten Nested Sequences
```c
fy_generic fy_flatten(fy_generic seq);
fy_generic fy_flatten(struct fy_generic_builder *gb, fy_generic seq);
```
Recursively flatten all nested sequences into a single flat sequence.

**Example**: `[1, [2, 3], [4, [5, 6]], 7]` → `[1, 2, 3, 4, 5, 6, 7]`

**Python equivalent**: Custom recursive function
**Rust equivalent**: `flatten()` on iterators
**Priority**: Medium

---

#### `fy_flatten_depth()` - Flatten to Specific Depth
```c
fy_generic fy_flatten_depth(fy_generic seq, int depth);
fy_generic fy_flatten_depth(struct fy_generic_builder *gb, fy_generic seq, int depth);
```
Flatten nested sequences to a specified depth level.

**Example**: `[1, [2, 3], [4, [5, 6]], 7]` with `depth=1` → `[1, 2, 3, 4, [5, 6], 7]`

**Priority**: Low (fy_flatten covers most use cases)

---

### Combining Operations

#### `fy_zip()` - Zip Two Sequences
```c
fy_generic fy_zip(fy_generic seq1, fy_generic seq2);
fy_generic fy_zip(struct fy_generic_builder *gb, fy_generic seq1, fy_generic seq2);
```
Combine two sequences element-wise into pairs.

**Example**: `zip(["a", "b", "c"], [1, 2, 3])` → `[["a", 1], ["b", 2], ["c", 3]]`

**Python equivalent**: `list(zip(seq1, seq2))`
**Rust equivalent**: `seq1.iter().zip(seq2)`
**Priority**: High

---

#### `fy_zip_all()` - Zip Multiple Sequences
```c
fy_generic fy_zip_all(fy_generic seqs...);  // Varargs terminated by fy_end
fy_generic fy_zip_all(struct fy_generic_builder *gb, fy_generic seqs...);
```
Combine multiple sequences element-wise.

**Example**: `zip_all([1,2,3], ["a","b","c"], [true, false, true])` → `[[1,"a",true], [2,"b",false], [3,"c",true]]`

**Priority**: Medium (fy_zip covers most use cases)

---

### Grouping Operations

#### `fy_group_by()` - Group by Key Function
```c
typedef fy_generic (*fy_key_fn)(fy_generic value, void *ctx);

fy_generic fy_group_by(fy_generic collection, fy_key_fn fn, void *ctx);
fy_generic fy_group_by(struct fy_generic_builder *gb, fy_generic collection,
                       fy_key_fn fn, void *ctx);
```
Group collection elements by the result of a key function.

**Example**: Group users by age decade
```c
fy_generic get_decade(fy_generic user, void *ctx) {
    int age = fy_get(user, "age", 0);
    return fy_value(age / 10 * 10);
}
// Result: {20: [users in 20s], 30: [users in 30s], ...}
```

**Python equivalent**: `itertools.groupby()` or custom with `defaultdict`
**Rust equivalent**: `group_by()` from itertools crate
**Clojure equivalent**: `group-by`
**Priority**: High

---

### Counting Operations

#### `fy_count()` - Count with Predicate
```c
typedef bool (*fy_predicate_fn)(fy_generic value, void *ctx);

size_t fy_count(fy_generic collection, fy_predicate_fn pred, void *ctx);
```
Count elements matching a predicate (different from `fy_len()` which counts all).

**Example**:
```c
bool is_active(fy_generic user, void *ctx) {
    return fy_get(user, "active", false);
}
size_t active_count = fy_count(users, is_active, NULL);
```

**Python equivalent**: `sum(1 for x in coll if pred(x))`
**Priority**: Medium

---

### Utility Operations

#### `fy_empty()` - Check if Empty
```c
bool fy_empty(fy_generic collection);
```
Check if a collection has no elements (equivalent to `fy_len(collection) == 0`).

**Python equivalent**: `not collection` or `len(collection) == 0`
**Priority**: Low (trivial wrapper, but convenient)

---

## Planned Additions - High Priority

These operations are commonly expected from modern language standard libraries.

### Search/Query Operations

#### `fy_find()` - Find First Matching Element
```c
fy_generic fy_find(fy_generic coll, fy_predicate_fn pred, void *ctx);
fy_generic fy_find(struct fy_generic_builder *gb, fy_generic coll,
                   fy_predicate_fn pred, void *ctx);
```
Return first element matching predicate, or `fy_invalid` if none found.

**Example**:
```c
bool is_admin(fy_generic user, void *ctx) {
    return fy_get(user, "role", "") == "admin";
}
fy_generic admin = fy_find(users, is_admin, NULL);
```

**Python equivalent**: `next((x for x in coll if pred(x)), None)`
**JavaScript equivalent**: `array.find(pred)`
**Rust equivalent**: `iter.find(pred)`
**Priority**: **Critical**

---

#### `fy_index_of()` - Find Index of Value
```c
ssize_t fy_index_of(fy_generic seq, fy_generic value);
```
Return index of first occurrence of value, or -1 if not found.

**Example**: `fy_index_of([10, 20, 30, 20], 20)` → `1`

**Python equivalent**: `list.index(value)` (raises exception if not found)
**JavaScript equivalent**: `array.indexOf(value)`
**Priority**: High

---

#### `fy_find_index()` - Find Index with Predicate
```c
ssize_t fy_find_index(fy_generic seq, fy_predicate_fn pred, void *ctx);
```
Return index of first element matching predicate, or -1 if none found.

**JavaScript equivalent**: `array.findIndex(pred)`
**Rust equivalent**: `iter.position(pred)`
**Priority**: High

---

### Boolean Predicates

#### `fy_any()` - Check if Any Match
```c
bool fy_any(fy_generic coll, fy_predicate_fn pred, void *ctx);
```
Return true if any element matches predicate (short-circuits on first match).

**Example**:
```c
bool is_negative(fy_generic num, void *ctx) {
    return fy_cast(num, 0) < 0;
}
bool has_negative = fy_any(numbers, is_negative, NULL);
```

**Python equivalent**: `any(pred(x) for x in coll)`
**JavaScript equivalent**: `array.some(pred)`
**Rust equivalent**: `iter.any(pred)`
**Priority**: **Critical**

---

#### `fy_all()` - Check if All Match
```c
bool fy_all(fy_generic coll, fy_predicate_fn pred, void *ctx);
```
Return true if all elements match predicate (short-circuits on first non-match).

**Example**:
```c
bool is_positive(fy_generic num, void *ctx) {
    return fy_cast(num, 0) > 0;
}
bool all_positive = fy_all(numbers, is_positive, NULL);
```

**Python equivalent**: `all(pred(x) for x in coll)`
**JavaScript equivalent**: `array.every(pred)`
**Rust equivalent**: `iter.all(pred)`
**Priority**: **Critical**

---

### Aggregation Operations

#### `fy_sum()` - Sum Numeric Values
```c
fy_generic fy_sum(fy_generic seq);
```
Sum all numeric values in sequence. Returns integer or float depending on contents.

**Example**: `fy_sum([1, 2, 3, 4, 5])` → `15`

**Python equivalent**: `sum(seq)`
**JavaScript equivalent**: `array.reduce((a, b) => a + b, 0)`
**Rust equivalent**: `iter.sum()`
**Priority**: **Critical**

---

#### `fy_min()` / `fy_max()` - Find Minimum/Maximum
```c
fy_generic fy_min(fy_generic seq);
fy_generic fy_max(fy_generic seq);
```
Find minimum or maximum value in sequence. Returns `fy_invalid` if empty.

**Example**: `fy_min([3, 1, 4, 1, 5])` → `1`

**Python equivalent**: `min(seq)`, `max(seq)`
**JavaScript equivalent**: `Math.min(...array)`, `Math.max(...array)`
**Rust equivalent**: `iter.min()`, `iter.max()`
**Priority**: **Critical**

---

#### `fy_min_by()` / `fy_max_by()` - Min/Max by Key Function
```c
fy_generic fy_min_by(fy_generic seq, fy_key_fn fn, void *ctx);
fy_generic fy_max_by(fy_generic seq, fy_key_fn fn, void *ctx);
```
Find element with minimum/maximum value of key function.

**Example**: Find user with highest age
```c
fy_generic get_age(fy_generic user, void *ctx) {
    return fy_get(user, "age", fy_value(0));
}
fy_generic oldest = fy_max_by(users, get_age, NULL);
```

**Python equivalent**: `min(seq, key=fn)`, `max(seq, key=fn)`
**Rust equivalent**: `iter.min_by_key(fn)`, `iter.max_by_key(fn)`
**Priority**: High

---

#### `fy_frequencies()` - Count Occurrences
```c
fy_generic fy_frequencies(fy_generic seq);
fy_generic fy_frequencies(struct fy_generic_builder *gb, fy_generic seq);
```
Return mapping of each unique value to its occurrence count.

**Example**: `fy_frequencies([1, 2, 2, 3, 1, 2])` → `{1: 2, 2: 3, 3: 1}`

**Python equivalent**: `collections.Counter(seq)`
**Clojure equivalent**: `frequencies`
**Priority**: High

---

### Transformation Operations

#### `fy_flat_map()` - Map and Flatten
```c
fy_generic fy_flat_map(fy_generic coll, fy_transform_fn fn, void *ctx);
fy_generic fy_flat_map(struct fy_generic_builder *gb, fy_generic coll,
                       fy_transform_fn fn, void *ctx);
```
Apply function to each element (which returns a sequence), then flatten results.

**Example**: Expand nested data
```c
fy_generic get_tags(fy_generic post, void *ctx) {
    return fy_get(post, "tags", fy_sequence());
}
// [{tags: ["a","b"]}, {tags: ["c"]}] → ["a", "b", "c"]
fy_generic all_tags = fy_flat_map(posts, get_tags, NULL);
```

**JavaScript equivalent**: `array.flatMap(fn)`
**Rust equivalent**: `iter.flat_map(fn)`
**Clojure equivalent**: `mapcat`
**Priority**: **Critical**

---

#### `fy_keep()` - Map and Remove Invalids
```c
fy_generic fy_keep(fy_generic coll, fy_transform_fn fn, void *ctx);
fy_generic fy_keep(struct fy_generic_builder *gb, fy_generic coll,
                   fy_transform_fn fn, void *ctx);
```
Apply function to each element, keep only non-`fy_invalid` results.

**Example**: Extract optional field
```c
fy_generic get_email(fy_generic user, void *ctx) {
    return fy_get(user, "email", fy_invalid);  // Returns invalid if missing
}
fy_generic emails = fy_keep(users, get_email, NULL);
// Only users with email field
```

**Clojure equivalent**: `keep`
**Priority**: Medium

---

#### `fy_compact()` - Remove Null/Invalid Values
```c
fy_generic fy_compact(fy_generic seq);
fy_generic fy_compact(struct fy_generic_builder *gb, fy_generic seq);
```
Remove all null and invalid values from sequence.

**Example**: `fy_compact([1, null, 2, invalid, 3])` → `[1, 2, 3]`

**JavaScript/Lodash equivalent**: `_.compact(array)`
**Priority**: Medium

---

## Planned Additions - Medium Priority

### Partitioning Operations

#### `fy_partition()` - Split by Predicate
```c
fy_generic fy_partition(fy_generic coll, fy_predicate_fn pred, void *ctx);
fy_generic fy_partition(struct fy_generic_builder *gb, fy_generic coll,
                        fy_predicate_fn pred, void *ctx);
```
Split collection into two: elements matching predicate and elements not matching.

**Example**: `partition([1,2,3,4,5,6], is_even)` → `[[2,4,6], [1,3,5]]`

**Rust equivalent**: `iter.partition(pred)`
**Clojure equivalent**: `(juxt filter remove)`
**Priority**: High

---

#### `fy_chunk()` - Split into Fixed-Size Chunks
```c
fy_generic fy_chunk(fy_generic seq, size_t size);
fy_generic fy_chunk(struct fy_generic_builder *gb, fy_generic seq, size_t size);
```
Split sequence into subsequences of fixed size.

**Example**: `fy_chunk([1,2,3,4,5,6,7], 3)` → `[[1,2,3], [4,5,6], [7]]`

**Python equivalent**: `itertools.batched(seq, size)` (Python 3.12+)
**Clojure equivalent**: `partition`
**Priority**: High

---

#### `fy_partition_by()` - Partition When Key Changes
```c
fy_generic fy_partition_by(fy_generic seq, fy_key_fn fn, void *ctx);
fy_generic fy_partition_by(struct fy_generic_builder *gb, fy_generic seq,
                           fy_key_fn fn, void *ctx);
```
Split sequence into chunks whenever key function result changes.

**Example**: Group consecutive duplicates
```c
fy_generic identity(fy_generic x, void *ctx) { return x; }
fy_partition_by([1,1,2,2,2,3,1,1], identity, NULL)
// → [[1,1], [2,2,2], [3], [1,1]]
```

**Clojure equivalent**: `partition-by`
**Priority**: Medium

---

#### `fy_split_at()` - Split at Index
```c
fy_generic fy_split_at(fy_generic seq, size_t n);
fy_generic fy_split_at(struct fy_generic_builder *gb, fy_generic seq, size_t n);
```
Split sequence at index into two parts: `[seq[:n], seq[n:]]`

**Example**: `fy_split_at([1,2,3,4,5], 2)` → `[[1,2], [3,4,5]]`

**Clojure equivalent**: `split-at`
**Priority**: Low

---

### Conditional Sequence Operations

#### `fy_take_while()` - Take While Predicate True
```c
fy_generic fy_take_while(fy_generic seq, fy_predicate_fn pred, void *ctx);
fy_generic fy_take_while(struct fy_generic_builder *gb, fy_generic seq,
                         fy_predicate_fn pred, void *ctx);
```
Take elements from start while predicate is true, stop at first false.

**Example**:
```c
bool is_less_than_5(fy_generic num, void *ctx) {
    return fy_cast(num, 0) < 5;
}
fy_take_while([1,2,3,4,5,6,2,1], is_less_than_5, NULL) → [1,2,3,4]
```

**Python equivalent**: `itertools.takewhile(pred, seq)`
**Rust equivalent**: `iter.take_while(pred)`
**Priority**: High

---

#### `fy_drop_while()` - Drop While Predicate True
```c
fy_generic fy_drop_while(fy_generic seq, fy_predicate_fn pred, void *ctx);
fy_generic fy_drop_while(struct fy_generic_builder *gb, fy_generic seq,
                         fy_predicate_fn pred, void *ctx);
```
Skip elements from start while predicate is true, keep rest.

**Example**: `fy_drop_while([1,2,3,4,5,6,2,1], is_less_than_5, NULL)` → `[5,6,2,1]`

**Python equivalent**: `itertools.dropwhile(pred, seq)`
**Rust equivalent**: `iter.skip_while(pred)`
**Priority**: High

---

#### `fy_remove()` - Opposite of Filter
```c
fy_generic fy_remove(fy_generic coll, fy_predicate_fn pred, void *ctx);
fy_generic fy_remove(struct fy_generic_builder *gb, fy_generic coll,
                     fy_predicate_fn pred, void *ctx);
```
Remove elements matching predicate (opposite of `fy_filter`).

**Example**: `fy_remove([1,2,3,4,5], is_even, NULL)` → `[1,3,5]`

**Clojure equivalent**: `remove`
**Priority**: Low (can use filter with negated predicate)

---

### String/Sequence Joining

#### `fy_join()` - Join Strings with Separator
```c
fy_generic fy_join(fy_generic seq, const char *separator);
fy_generic fy_join(struct fy_generic_builder *gb, fy_generic seq, const char *separator);
```
Join sequence of strings with separator.

**Example**: `fy_join(["hello", "world", "!"], " ")` → `"hello world !"`

**Python equivalent**: `separator.join(seq)`
**JavaScript equivalent**: `array.join(separator)`
**Priority**: High

---

#### `fy_interpose()` - Insert Separator Between Elements
```c
fy_generic fy_interpose(fy_generic seq, fy_generic separator);
fy_generic fy_interpose(struct fy_generic_builder *gb, fy_generic seq, fy_generic separator);
```
Insert separator value between each element.

**Example**: `fy_interpose([1, 2, 3], 0)` → `[1, 0, 2, 0, 3]`

**Clojure equivalent**: `interpose`
**Priority**: Low

---

### Utility Operations

#### `fy_enumerate()` - Zip with Indices
```c
fy_generic fy_enumerate(fy_generic seq);
fy_generic fy_enumerate(struct fy_generic_builder *gb, fy_generic seq);
```
Pair each element with its index.

**Example**: `fy_enumerate(["a", "b", "c"])` → `[[0, "a"], [1, "b"], [2, "c"]]`

**Python equivalent**: `list(enumerate(seq))`
**Rust equivalent**: `iter.enumerate()`
**Priority**: High

---

#### `fy_zipmap()` - Create Map from Keys and Values
```c
fy_generic fy_zipmap(fy_generic keys, fy_generic values);
fy_generic fy_zipmap(struct fy_generic_builder *gb, fy_generic keys, fy_generic values);
```
Create mapping from parallel keys and values sequences.

**Example**: `fy_zipmap(["a","b","c"], [1,2,3])` → `{"a": 1, "b": 2, "c": 3}`

**Clojure equivalent**: `zipmap`
**JavaScript equivalent**: `Object.fromEntries(zip(keys, values))`
**Priority**: Medium

---

#### `fy_interleave()` - Interleave Multiple Sequences
```c
fy_generic fy_interleave(fy_generic seq1, fy_generic seq2, ...);  // Varargs
fy_generic fy_interleave(struct fy_generic_builder *gb, fy_generic seq1, fy_generic seq2, ...);
```
Interleave elements from multiple sequences.

**Example**: `fy_interleave([1,2,3], ["a","b","c"])` → `[1, "a", 2, "b", 3, "c"]`

**Clojure equivalent**: `interleave`
**Priority**: Low

---

#### `fy_repeat()` - Repeat Value N Times
```c
fy_generic fy_repeat(fy_generic value, size_t n);
fy_generic fy_repeat(struct fy_generic_builder *gb, fy_generic value, size_t n);
```
Create sequence by repeating value.

**Example**: `fy_repeat("x", 5)` → `["x", "x", "x", "x", "x"]`

**Python equivalent**: `[value] * n` or `itertools.repeat(value, n)`
**Clojure equivalent**: `repeat`
**Priority**: Low

---

#### `fy_range()` - Create Numeric Range
```c
fy_generic fy_range(int64_t start, int64_t end, int64_t step);
fy_generic fy_range(struct fy_generic_builder *gb, int64_t start, int64_t end, int64_t step);
```
Create sequence of numbers from start to end with step.

**Example**: `fy_range(0, 10, 2)` → `[0, 2, 4, 6, 8]`

**Python equivalent**: `list(range(start, end, step))`
**Rust equivalent**: `(start..end).step_by(step)`
**Priority**: Medium

---

## Planned Additions - Lower Priority

### Set Operations

#### `fy_intersection()` - Common Elements
```c
fy_generic fy_intersection(fy_generic seq1, fy_generic seq2);
fy_generic fy_intersection(struct fy_generic_builder *gb, fy_generic seq1, fy_generic seq2);
```
Return sequence containing only elements present in both sequences.

**Example**: `fy_intersection([1,2,3,4], [3,4,5,6])` → `[3, 4]`

**Python equivalent**: `list(set(seq1) & set(seq2))`
**Priority**: Low

---

#### `fy_union()` - All Unique Elements
```c
fy_generic fy_union(fy_generic seq1, fy_generic seq2);
fy_generic fy_union(struct fy_generic_builder *gb, fy_generic seq1, fy_generic seq2);
```
Return sequence containing all unique elements from all sequences.

**Example**: `fy_union([1,2,3], [3,4,5])` → `[1, 2, 3, 4, 5]`

**Python equivalent**: `list(set(seq1) | set(seq2))`
**Priority**: Low

---

#### `fy_difference()` - Elements in First Not in Others
```c
fy_generic fy_difference(fy_generic seq1, fy_generic seq2);
fy_generic fy_difference(struct fy_generic_builder *gb, fy_generic seq1, fy_generic seq2);
```
Return elements in first sequence but not in second.

**Example**: `fy_difference([1,2,3,4], [3,4,5,6])` → `[1, 2]`

**Python equivalent**: `list(set(seq1) - set(seq2))`
**Priority**: Low

---

### Advanced Sequence Operations

#### `fy_nth()` - Safe Element Access
```c
fy_generic fy_nth(fy_generic seq, size_t index, fy_generic default_val);
```
Get element at index, return default if index out of bounds.

**Example**: `fy_nth([1,2,3], 10, 0)` → `0` (no error)

**Clojure equivalent**: `nth`
**Priority**: Low (fy_get_item already exists, this adds safety)

---

#### `fy_step_by()` - Every Nth Element
```c
fy_generic fy_step_by(fy_generic seq, size_t step);
fy_generic fy_step_by(struct fy_generic_builder *gb, fy_generic seq, size_t step);
```
Return every nth element.

**Example**: `fy_step_by([1,2,3,4,5,6,7,8], 2)` → `[1, 3, 5, 7]`

**Rust equivalent**: `iter.step_by(n)`
**Priority**: Low

---

#### `fy_windows()` - Sliding Window
```c
fy_generic fy_windows(fy_generic seq, size_t size);
fy_generic fy_windows(struct fy_generic_builder *gb, fy_generic seq, size_t size);
```
Return sequence of overlapping windows of specified size.

**Example**: `fy_windows([1,2,3,4,5], 3)` → `[[1,2,3], [2,3,4], [3,4,5]]`

**Rust equivalent**: `iter.windows(n)` (returns slices, not owned data)
**Priority**: Low

---

## Implementation Priority Ranking

### Tier 1 - Critical (Most Expected)
1. `fy_any()` / `fy_all()` - universal boolean predicates
2. `fy_find()` - universal search pattern
3. `fy_sum()` / `fy_min()` / `fy_max()` - basic aggregations
4. `fy_flat_map()` - extremely common transformation
5. `fy_zip()` - common pairing operation

### Tier 2 - High Value
6. `fy_group_by()` - powerful grouping (already documented)
7. `fy_partition()` - split by predicate
8. `fy_chunk()` - batch processing
9. `fy_take_while()` / `fy_drop_while()` - conditional sequences
10. `fy_enumerate()` - Python users expect this
11. `fy_join()` - string operations
12. `fy_frequencies()` - data analysis
13. `fy_index_of()` / `fy_find_index()` - search operations
14. `fy_min_by()` / `fy_max_by()` - keyed aggregation

### Tier 3 - Nice to Have
15. `fy_flatten()` - already documented
16. `fy_keep()` - map with filter
17. `fy_compact()` - clean data
18. `fy_zipmap()` - convenient mapping creation
19. `fy_range()` - numeric sequences
20. `fy_partition_by()` - grouping consecutive

### Tier 4 - Low Priority
21. All set operations (intersection, union, difference)
22. `fy_interleave()`, `fy_interpose()`
23. `fy_repeat()`, `fy_step_by()`, `fy_windows()`
24. `fy_nth()`, `fy_empty()`, `fy_remove()`, `fy_split_at()`

## Notes

- Operations marked "already documented" are from GENERIC-API-UTILITIES.md
- All new operations follow established patterns:
  - Polymorphic macros with builder/local variants
  - Function pointer callbacks with context parameter
  - Immutable - return new values, don't modify inputs
  - Return `fy_invalid` for error conditions
- Predicate/transform/key function signatures are consistent across all operations
- All operations should support both sequences and mappings where semantically appropriate
