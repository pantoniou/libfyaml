# Library Comparisons

This section compares libfyaml's generic type system and overall approach with other YAML/JSON libraries and language features. The goal is to help you understand libfyaml's unique position and make informed technology choices.

## Overview

libfyaml's generic type system combines:
- **C performance** with ergonomics approaching dynamic languages
- **Immutability by design** for inherent thread safety and functional programming patterns
- **Inline storage** for small values (no allocation overhead)
- **Automatic internalization** (clean API without manual memory management)
- **Type-aware reflection** (optional, via libclang)

This combination is unique in the YAML/JSON library landscape.

## rapidyaml

[rapidyaml](https://github.com/biojppm/rapidyaml) is a high-performance C++ YAML library focused on zero-copy parsing and minimal allocations.

**API Comparison - Building a Configuration:**

```cpp
// rapidyaml
ryml::Tree tree;
ryml::NodeRef root = tree.rootref();
root |= ryml::MAP;
root["host"] << "localhost";
root["port"] << 8080;
root["workers"] << 4;
ryml::NodeRef db = root["database"];
db |= ryml::MAP;
db["port"] << 5432;
db["pool_size"] << 10;
```

```c
// libfyaml generic API
fy_generic config = fy_mapping(
    "host", "localhost",
    "port", 8080,
    "workers", 4,
    "database", fy_mapping(
        "port", 5432,
        "pool_size", 10
    )
);
```

**Key Differences:**

| Aspect | rapidyaml | libfyaml |
|--------|-----------|----------|
| **Language** | C++17 | C11/GNU C |
| **API Style** | Imperative tree building | Declarative nested construction |
| **Memory Model** | True zero-copy (requires source buffer lifetime) | Inline storage + optional zero-copy + atoms |
| **Allocation** | Arena-based, pooled | Multiple strategies: linear, mremap, **dedup** (arena-based), auto |
| **Type Conversion** | Manual (`<<` operators) | Automatic via `_Generic` dispatch |
| **Nested Structures** | Explicit ref management | Natural nesting with `fy_mapping()/fy_sequence()` |
| **Deduplication** | ❌ | ✅ (dedup allocator eliminates duplicate strings/values) |
| **DoS Protection** | ❌ | ✅ (Dedup provides immunity to billion laughs attack) |

**Performance:**

- **rapidyaml strengths**:
  - True zero-copy parsing (no data copying if source remains valid)
  - Arena allocation minimizes allocation overhead
  - Extremely fast parsing (designed for games/real-time)

- **libfyaml strengths**:
  - **Multiple allocation strategies** adaptable to workload (linear, mremap, dedup, auto)
  - **Dedup allocator** dramatically reduces memory for documents with repeated values
  - **Inline storage** eliminates allocations for small values (61-bit ints, 7-byte strings)
  - **Arena-based stacked allocators** for efficient batch allocation
  - **Immunity to billion laughs attack** (YAML bomb) via dedup allocator
  - No source buffer lifetime requirements
  - **Superior for ultra-large YAML files** with repeated content

**Memory Usage Examples:**

```c
// Small config: libfyaml stores ENTIRELY inline (zero allocations)
fy_generic cfg = fy_mapping("port", 8080, "debug", true);
// sizeof(fy_generic) = 8 bytes, contains everything

// rapidyaml: Requires tree structure + arena allocations
// Even for small configs, needs tree nodes and string storage
```

**Deduplication Example (Billion Laughs Attack):**

```yaml
# Classic YAML bomb - exponential expansion
lol: &lol ["lol"]
lol2: &lol2 [*lol, *lol, *lol, *lol, *lol, *lol, *lol, *lol]
lol3: &lol3 [*lol2, *lol2, *lol2, *lol2, *lol2, *lol2, *lol2, *lol2]
# ... continues ...
# Can expand to gigabytes in memory
```

```c
// rapidyaml: Each anchor reference creates new tree node
// Memory usage grows exponentially: 2^N expansions

// libfyaml with dedup allocator: Recognizes duplicates
// Memory usage stays constant - all references point to same data
// Document loads successfully, immune to attack
struct fy_parse_cfg cfg = {
    .flags = FYPCF_DEFAULT_PARSE,
};
fy_document_build_from_file(&cfg, "bomb.yaml");  // Safe!
```

**Architecture:**

- **rapidyaml**: Parse into tree, tree nodes ref into original buffer, arena manages node allocations
- **libfyaml**: Multiple approaches:
  - **Documents**: Tree-based with atoms (zero-copy refs to source)
  - **Generics**: Inline/builder storage for runtime typed values
  - **Allocators**: Pluggable (linear, mremap, dedup, auto)
  - **Stacked allocation**: Arena-style batch allocation/deallocation

**When to choose rapidyaml:**
- C++ codebase
- Need absolute maximum parse speed for typical documents
- Can guarantee source buffer lifetime
- Real-time/game engine constraints where predictability matters
- Documents without heavy duplication

**When to choose libfyaml:**
- C codebase or C compatibility required
- **Ultra-large YAML files** (dedup wins dramatically)
- **Documents with repeated content** (dedup eliminates duplication)
- **Security-critical parsing** (billion laughs immunity)
- Working with many small configs (inline storage wins)
- Need reflection/type-aware features
- Want declarative, readable API
- Want automatic memory management (internalization)
- Cannot guarantee source buffer lifetime
- Need flexible allocation strategies for different workloads

## Other YAML Libraries

### libyaml

The reference C implementation, event-based API.

**API Comparison - Parsing:**

```c
// libyaml (event-based)
yaml_parser_t parser;
yaml_event_t event;
yaml_parser_initialize(&parser);
yaml_parser_set_input_file(&parser, file);
do {
    yaml_parser_parse(&parser, &event);
    switch(event.type) {
        case YAML_SCALAR_EVENT:
            // Handle scalar
            break;
        case YAML_SEQUENCE_START_EVENT:
            // Handle sequence
            break;
        // ... many cases
    }
    yaml_event_delete(&event);
} while(event.type != YAML_STREAM_END_EVENT);
```

```c
// libfyaml (generic API)
struct fy_document *doc = fy_document_build_from_file(NULL, "config.yaml");
fy_generic config = fy_document_to_generic(doc);
// Work with config naturally
```

**Key Differences:**
- **libyaml**: Low-level event stream, manual state machine
- **libfyaml**: High-level generics OR event API (you choose)
- **libyaml**: No built-in document tree
- **libfyaml**: Document tree + generics + events (three APIs)

**When to use libyaml:**
- Need minimal dependencies (libyaml is tiny)
- Streaming large documents
- Already have event-based architecture

**When to use libfyaml:**
- Want high-level API
- Need document manipulation
- Want type-aware processing
- Prefer modern C features

### yaml-cpp

Popular C++ YAML library with node-based API.

**API Comparison:**

```cpp
// yaml-cpp
YAML::Node config;
config["host"] = "localhost";
config["port"] = 8080;
config["items"] = YAML::Node(YAML::NodeType::Sequence);
config["items"].push_back(1);
config["items"].push_back(2);
```

```c
// libfyaml
fy_generic config = fy_mapping(
    "host", "localhost",
    "port", 8080,
    "items", fy_sequence(1, 2)
);
```

**Key Differences:**
- **yaml-cpp**: Node-based, C++ idioms (operator overloading)
- **libfyaml**: Value-based generics, functional style
- **yaml-cpp**: Heap allocations for all nodes
- **libfyaml**: Inline storage + stack allocation for temps

**When to use yaml-cpp:**
- C++ codebase with modern features (C++11+)
- Familiar with STL-style APIs
- Need yaml-cpp's specific ecosystem

**When to use libfyaml:**
- C or C codebase
- Better performance for small values
- Cleaner syntax for nested structures

## JSON Libraries

### json-c

**API Comparison:**

```c
// json-c
struct json_object *obj = json_object_new_object();
json_object_object_add(obj, "name", json_object_new_string("Alice"));
json_object_object_add(obj, "age", json_object_new_int(30));
struct json_object *arr = json_object_new_array();
json_object_array_add(arr, json_object_new_int(1));
json_object_array_add(arr, json_object_new_int(2));
json_object_object_add(obj, "items", arr);
```

```c
// libfyaml
fy_generic obj = fy_mapping(
    "name", "Alice",
    "age", 30,
    "items", fy_sequence(1, 2)
);
```

**Difference:** json-c requires explicit construction and type specification for every value. libfyaml's `_Generic` dispatch handles types automatically.

### cJSON

```c
// cJSON
cJSON *obj = cJSON_CreateObject();
cJSON_AddStringToObject(obj, "name", "Alice");
cJSON_AddNumberToObject(obj, "age", 30);
cJSON *arr = cJSON_CreateArray();
cJSON_AddItemToArray(arr, cJSON_CreateNumber(1));
cJSON_AddItemToArray(arr, cJSON_CreateNumber(2));
cJSON_AddItemToObject(obj, "items", arr);
```

Similar verbosity to json-c. Each value requires explicit creation.

### jansson

```c
// jansson (slightly better with shortcuts)
json_t *obj = json_object();
json_object_set_new(obj, "name", json_string("Alice"));
json_object_set_new(obj, "age", json_integer(30));
json_t *arr = json_array();
json_array_append_new(arr, json_integer(1));
json_array_append_new(arr, json_integer(2));
json_object_set_new(obj, "items", arr);
```

Still requires manual construction, though `json_pack()` format strings offer an alternative:

```c
json_t *obj = json_pack("{s:s, s:i, s:[i,i]}",
    "name", "Alice", "age", 30, "items", 1, 2);
```

This is closer to libfyaml's approach but uses format strings (type-unsafe).

**libfyaml advantages over JSON libraries:**
- **Zero boilerplate**: No `_new_`, `_create_`, `_add_` calls
- **Type safety**: `_Generic` provides compile-time dispatch
- **Inline storage**: Small values need no allocation
- **Readable**: Nested structures map directly to visual structure

## Comparison with Dynamic Languages

### Python

```python
# Python
config = {
    "host": "localhost",
    "port": 8080,
    "workers": 4,
    "database": {
        "port": 5432,
        "pool_size": 10
    },
    "features": ["auth", "logging", "metrics"]
}
```

```c
// libfyaml - remarkably similar!
fy_generic config = fy_mapping(
    "host", "localhost",
    "port", 8080,
    "workers", 4,
    "database", fy_mapping(
        "port", 5432,
        "pool_size", 10
    ),
    "features", fy_sequence("auth", "logging", "metrics")
);
```

**How libfyaml achieves Python-like ergonomics in C:**
- **Sum types**: `fy_generic` is a tagged union (like Python's dynamic typing)
- **`_Generic` dispatch**: Automatic type detection at compile-time
- **Variadic macros**: `fy_sequence(...)` mimics list literals
- **Preprocessor metaprogramming**: Argument counting, type conversion

**Differences:**
- **Python**: Runtime typing, garbage collection, interpreter overhead
- **libfyaml**: Compile-time types, manual memory (builder), native performance

### JavaScript

```javascript
// JavaScript
const config = {
    host: "localhost",
    port: 8080,
    items: [1, 2, 3],
    nested: { a: true, b: false }
};
```

```c
// libfyaml
fy_generic config = fy_mapping(
    "host", "localhost",
    "port", 8080,
    "items", fy_sequence(1, 2, 3),
    "nested", fy_mapping("a", true, "b", false)
);
```

**Similarities:**
- Natural nesting
- Automatic type coercion
- Mixed-type collections

**Differences:**
- **JavaScript**: Prototype-based objects, GC, type coercion can be surprising
- **libfyaml**: Explicit mapping/sequence distinction, deterministic memory, no implicit conversions (beyond number types)

### Rust

```rust
// Rust with serde_json
use serde_json::json;

let config = json!({
    "host": "localhost",
    "port": 8080,
    "items": [1, 2, 3]
});
```

```c
// libfyaml
fy_generic config = fy_mapping(
    "host", "localhost",
    "port", 8080,
    "items", fy_sequence(1, 2, 3)
);
```

**Comparison:**
- **Rust `json!`**: Compile-time macro expansion, type-safe enums
- **libfyaml**: C preprocessor + `_Generic`, tagged union
- **Rust**: Enum types with pattern matching, borrow checker
- **libfyaml**: Manual memory management, builder pattern

**Both achieve:**
- Clean syntax
- Compile-time processing
- Efficient representation

**Key insight:** libfyaml brings Rust-like ergonomics to C through clever use of modern C features and preprocessor metaprogramming.

## Feature Comparison Table

| Feature | libfyaml | rapidyaml | libyaml | yaml-cpp | json-c | Python dict | JavaScript object |
|---------|----------|-----------|---------|----------|--------|-------------|-------------------|
| **Language** | C11/GNU | C++17 | C89 | C++11 | C89 | Python | JavaScript |
| **Inline Storage** | ✅ (61-bit int, 7-byte str) | ❌ | ❌ | ❌ | ❌ | ✅ (small int) | ✅ (SMI) |
| **Zero-Copy Parse** | ✅ (optional) | ✅ (required) | ❌ | ❌ | ❌ | N/A | N/A |
| **Pointer Tagging** | ✅ | ❌ | ❌ | ❌ | ❌ | ✅ (CPython) | ✅ (V8) |
| **Auto Type Detection** | ✅ (`_Generic`) | ⚠️ (operators) | ❌ | ⚠️ (operators) | ❌ | ✅ | ✅ |
| **Declarative Syntax** | ✅ | ❌ | ❌ | ⚠️ | ❌ | ✅ | ✅ |
| **Builder Pattern** | ✅ (with internalization) | ✅ (arena) | ❌ | ❌ | ❌ | N/A | N/A |
| **Stack Allocation** | ✅ (alloca) | ❌ | ❌ | ❌ | ❌ | N/A | N/A |
| **Arena Allocation** | ✅ (stacked allocator) | ✅ | ❌ | ❌ | ❌ | N/A | N/A |
| **Deduplication** | ✅ (dedup allocator) | ❌ | ❌ | ❌ | ❌ | ⚠️ (string interning) | ⚠️ (string interning) |
| **DoS Protection** | ✅ (billion laughs immunity) | ❌ | ❌ | ❌ | ❌ | ⚠️ | ⚠️ |
| **Reflection** | ✅ (libclang) | ❌ | ❌ | ❌ | ❌ | ✅ (native) | ⚠️ (limited) |
| **Memory Model** | Inline/Stack/Arena/Dedup | Arena | Manual | Heap (shared_ptr) | Ref-counting | GC | GC |
| **Small Config Overhead** | 8 bytes (inline) | ~100s bytes (tree) | N/A (events) | ~100s bytes | ~50+ bytes | ~50+ bytes | ~48 bytes |
| **API Verbosity** | Low | Medium | High | Medium | High | Minimal | Minimal |
| **Compile-Time Safety** | ⚠️ (`_Generic`) | ✅ (C++ types) | ❌ | ✅ (C++ types) | ❌ | ❌ | ❌ |
| **Runtime Typing** | ✅ (tagged union) | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ |
| **Immutability** | ✅ (by design) | ❌ | N/A | ❌ | ❌ | ⚠️ (tuples only) | ❌ |
| **Thread Safety** | ✅ (lock-free reads) | ⚠️ (read-only trees) | N/A | ❌ | ⚠️ (refcount) | ⚠️ (GIL) | ❌ |

**Legend:**
- ✅ Full support
- ⚠️ Partial/conditional support
- ❌ Not supported
- N/A Not applicable

## Architecture Philosophy Comparison

Different libraries make different fundamental tradeoffs. Here's how they compare philosophically:

### Memory Model Approaches

**Zero-Copy (rapidyaml)**
- Philosophy: Never copy source data
- Pros: Maximum parse speed, minimal memory overhead
- Cons: Source buffer lifetime coupling, complex lifetime management
- Best for: Large documents, streaming scenarios, real-time systems

**Inline Storage + Pluggable Allocators (libfyaml)**
- Philosophy: Right strategy for each workload
- **Inline**: Small values stored in pointer (zero allocations)
- **Linear**: Simple append-only arena
- **Mremap**: Efficient growing allocations via mremap(2)
- **Dedup**: Hash-based deduplication, eliminates repeated values
- **Auto**: Automatically selects best strategy
- Pros: Adaptable, dedup eliminates duplication, billion laughs immunity
- Cons: More complex implementation
- Best for: All scenarios - small configs (inline), large docs (dedup), variable workloads (auto)

**Reference Counting (json-c, yaml-cpp)**
- Philosophy: Shared ownership through refcounts
- Pros: Automatic memory management, intuitive sharing
- Cons: Overhead on every value, cycle risks, cache unfriendly
- Best for: Long-lived structures, deep sharing

**Garbage Collection (Python, JavaScript)**
- Philosophy: Runtime manages all memory
- Pros: No manual management, simple API
- Cons: Pause times, memory overhead, runtime required
- Best for: Application code, scripting, RAD

**Arena/Pool (rapidyaml, libfyaml stacked allocator)**
- Philosophy: Batch allocate, batch free
- Pros: Fast allocation, good cache locality, simple cleanup
- Cons: Cannot free individual items (but libfyaml's dedup mitigates this)
- Best for: Request/response patterns, batch processing
- **libfyaml advantage**: Combines arena with deduplication for best of both worlds

### API Design Approaches

**Event-Based (libyaml)**
- Philosophy: Streaming interface, minimal state
- Pros: Constant memory, perfect for streaming, minimal footprint
- Cons: Complex to use, manual state machine, verbose
- Best for: Large document streaming, memory-constrained systems

**Tree-Based (yaml-cpp, json-c)**
- Philosophy: Build complete document tree
- Pros: Easy random access, familiar DOM-like API
- Cons: Must load entire document, memory overhead
- Best for: Document manipulation, repeated queries

**Hybrid (libfyaml)**
- Philosophy: Multiple APIs for different needs
- Pros: Choose the right tool (events/tree/generics), flexibility
- Cons: Larger surface area to learn
- Best for: Library/framework authors, diverse use cases

**Functional/Declarative (libfyaml generics)**
- Philosophy: Data literals in C
- Pros: Readable, composable, minimal boilerplate
- Cons: Requires modern C, stack depth limits
- Best for: Configuration code, tests, data-driven programming

### Type System Approaches

**Static Typing (C++, Rust)**
- Compile-time guarantees, zero runtime overhead
- Requires type definitions at compile-time
- Best with code generation or serialization frameworks

**Tagged Union (libfyaml `fy_generic`)**
- Runtime type tag, compile-time safe construction
- Balances safety and flexibility
- Small memory overhead (3 bits for tag)

**Dynamic Typing (Python, JavaScript)**
- Full runtime flexibility
- Type errors discovered at runtime
- Higher memory and performance cost

**Opaque Pointers (json-c, libyaml)**
- All types hidden behind pointers
- Runtime type checking via functions
- Pointer-per-value memory overhead

## When to Choose libfyaml

**libfyaml excels when you need:**
1. **Ultra-Large YAML Files**: Dedup allocator dramatically reduces memory for documents with repeated content
2. **Security-Critical Parsing**: Built-in immunity to billion laughs (YAML bomb) attacks
3. **Small Config Performance**: Inline storage eliminates allocations for small configs
4. **Adaptive Memory Management**: Pluggable allocators (linear, mremap, dedup, auto) for different workloads
5. **Ergonomic C API**: Python/Rust-like syntax in C without switching languages
6. **Type-Aware Processing**: Reflection to map YAML ↔ C structs automatically
7. **Memory Control**: Deterministic allocation (stack/arena/heap), not GC or refcounting
8. **Flexible APIs**: High-level (generics) and low-level (events) in same library
9. **C Compatibility**: C codebase or need C FFI

**Consider alternatives when:**
1. **Pure C++**: rapidyaml or yaml-cpp may be more idiomatic for C++ codebases
2. **Minimal Footprint**: libyaml is smaller if you only need event parsing
3. **Existing Ecosystem**: Already deeply invested in json-c/yaml-cpp

## Summary

libfyaml's unique position comes from combining:
- **C performance** (native code, no runtime)
- **Modern C features** (`_Generic`, statement expressions)
- **Immutability** (generics are immutable, enabling inherent thread safety)
- **Adaptive memory management** (inline storage, arena allocation, deduplication)
- **Security features** (billion laughs immunity via dedup allocator)
- **Scalability** (small configs with inline storage, ultra-large docs with dedup)
- **Multiple APIs** (events, tree, generics, reflection)

This makes it particularly well-suited for:
- **Systems programming** that needs **application-level ergonomics** without sacrificing **C performance and control**
- **Multi-threaded YAML processing** where immutable generics enable lock-free concurrent reads
- **Ultra-large YAML processing** where deduplication provides dramatic memory savings
- **Security-critical environments** requiring DoS protection
