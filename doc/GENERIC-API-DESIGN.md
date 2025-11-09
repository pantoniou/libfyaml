# Generic API Design: Achieving Python Ergonomics in C

This document describes the design evolution of libfyaml's generic type system API, showing how careful API design can achieve Python-level ergonomics while maintaining C's type safety and performance.

## Table of Contents

1. [The Problem: Verbose Generic APIs](#the-problem-verbose-generic-apis)
2. [Solution: Short-Form API with _Generic Dispatch](#solution-short-form-api-with-_generic-dispatch)
3. [Type Checking Shortcuts](#type-checking-shortcuts)
4. [The Empty Collection Pattern](#the-empty-collection-pattern)
5. [Complete API Reference](#complete-api-reference)
6. [Real-World Example: Anthropic Messages API](#real-world-example-anthropic-messages-api)
7. [Python Comparison](#python-comparison)

## The Problem: Verbose Generic APIs

Traditional generic/dynamic type APIs in C require verbose function calls with explicit type conversions:

```c
// Verbose approach
const char *role = fy_generic_get_string_default(
    fy_generic_mapping_lookup(message, fy_to_generic("role")),
    "assistant"
);

int port = fy_generic_get_int_default(
    fy_generic_mapping_lookup(
        fy_generic_mapping_lookup(config, fy_to_generic("server")),
        fy_to_generic("port")
    ),
    8080
);
```

This is far from ideal:
- **Verbose**: Multiple function calls for simple operations
- **Nested lookups are unreadable**: Hard to follow the chain
- **Type conversion noise**: `fy_to_generic()` everywhere
- **No Python-like defaults**: Can't naturally express "default to empty dict"

## Solution: Short-Form API with _Generic Dispatch

Using C11's `_Generic` feature, we can create type-safe shortcuts that dispatch based on the default value type:

### Core Design

```c
// Mapping lookup with default
#define fy_map_get(map, key, default) \
    _Generic((default), \
        const char *: fy_map_get_string, \
        char *: fy_map_get_string, \
        int: fy_map_get_int, \
        long: fy_map_get_int, \
        long long: fy_map_get_int, \
        double: fy_map_get_double, \
        float: fy_map_get_double, \
        bool: fy_map_get_bool, \
        fy_generic: fy_map_get_generic \
    )(map, key, default)

// Sequence indexing with default
#define fy_seq_get(seq, idx, default) \
    _Generic((default), \
        const char *: fy_seq_get_string, \
        char *: fy_seq_get_string, \
        int: fy_seq_get_int, \
        long: fy_seq_get_int, \
        long long: fy_seq_get_int, \
        double: fy_seq_get_double, \
        float: fy_seq_get_double, \
        bool: fy_seq_get_bool, \
        fy_generic: fy_seq_get_generic \
    )(seq, idx, default)
```

### Helper Functions

```c
// Mapping lookups
static inline const char *fy_map_get_string(fy_generic map, const char *key, const char *def);
static inline int64_t fy_map_get_int(fy_generic map, const char *key, int64_t def);
static inline double fy_map_get_double(fy_generic map, const char *key, double def);
static inline bool fy_map_get_bool(fy_generic map, const char *key, bool def);
static inline fy_generic fy_map_get_generic(fy_generic map, const char *key, fy_generic def);

// Sequence indexing
static inline const char *fy_seq_get_string(fy_generic seq, size_t idx, const char *def);
static inline int64_t fy_seq_get_int(fy_generic seq, size_t idx, int64_t def);
static inline double fy_seq_get_double(fy_generic seq, size_t idx, double def);
static inline bool fy_seq_get_bool(fy_generic seq, size_t idx, bool def);
static inline fy_generic fy_seq_get_generic(fy_generic seq, size_t idx, fy_generic def);
```

### Result

```c
// Clean, readable API
const char *role = fy_map_get(message, "role", "assistant");

int port = fy_map_get(
    fy_map_get(config, "server", fy_map_empty),
    "port",
    8080
);
```

**Benefits**:
- Type-safe: Compiler catches type mismatches
- Concise: One function call per lookup
- Natural defaults: Type inferred from default value
- Chainable: Nested lookups compose naturally

## Type Checking Shortcuts

Long function names were replaced with short, intuitive names:

### Before vs After

| Verbose API | Short-Form API |
|------------|----------------|
| `fy_generic_get_type(g)` | `fy_type(g)` |
| `fy_generic_is_null(g)` | `fy_is_null(g)` |
| `fy_generic_is_bool(g)` | `fy_is_bool(g)` |
| `fy_generic_is_int(g)` | `fy_is_int(g)` |
| `fy_generic_is_float(g)` | `fy_is_float(g)` |
| `fy_generic_is_string(g)` | `fy_is_string(g)` |
| `fy_generic_is_sequence(g)` | `fy_is_seq(g)` |
| `fy_generic_is_mapping(g)` | `fy_is_map(g)` |
| `fy_generic_sequence_get_item_count(g)` | `fy_seq_count(g)` |
| `fy_generic_mapping_get_pair_count(g)` | `fy_map_count(g)` |

### Example Usage

```c
// Type switching
switch (fy_type(value)) {
    case FYGT_MAPPING:
        printf("Map with %zu entries\n", fy_map_count(value));
        break;
    case FYGT_SEQUENCE:
        printf("Sequence with %zu items\n", fy_seq_count(value));
        break;
    case FYGT_STRING:
        printf("String: %s\n", fy_string_get(value));
        break;
    default:
        break;
}

// Type checking
if (fy_is_map(config)) {
    const char *host = fy_map_get(config, "host", "localhost");
    int port = fy_map_get(config, "port", 8080);
}
```

## The Empty Collection Pattern

The key insight for Python-equivalence: **default to empty collections instead of `fy_invalid`**.

### Motivation

When chaining lookups, Python uses empty dicts/lists as defaults:

```python
# Python: default to empty dict
port = config.get("server", {}).get("port", 8080)

# Python: default to empty list
users = data.get("users", [])
```

Using `fy_invalid` doesn't match this pattern:
```c
// Awkward: using fy_invalid
int port = fy_map_get(
    fy_map_get(config, "server", fy_invalid),  // Not semantically clear
    "port",
    8080
);
```

### Solution: fy_map_empty and fy_seq_empty

```c
// Define empty collection constants
#define fy_map_empty  /* empty mapping constant */
#define fy_seq_empty  /* empty sequence constant */
```

These can be implemented as:
- Static inline function returning empty generic
- Preprocessor macro expanding to `fy_mapping()` / `fy_sequence()`
- Global constant (if generics are POD-compatible)

### Usage

```c
// Now matches Python exactly!
int port = fy_map_get(
    fy_map_get(config, "server", fy_map_empty),
    "port",
    8080
);

fy_generic users = fy_map_get(data, "users", fy_seq_empty);

// Deep navigation with type safety
const char *db_host = fy_map_get(
    fy_map_get(
        fy_map_get(root, "database", fy_map_empty),
        "connection",
        fy_map_empty
    ),
    "host",
    "localhost"
);
```

### Benefits

1. **Semantic clarity**: "Default to empty collection" is clearer than "default to invalid"
2. **Type correctness**: The default matches the expected type
3. **Python equivalence**: Exact match with Python's `.get({})` and `.get([])`
4. **Safe chaining**: Looking up in empty map/seq returns the next default
5. **No special cases**: Empty collections behave like any other collection

## Complete API Reference

### Construction

**Stack-allocated (temporary)**:
```c
fy_generic msg = fy_mapping(
    "role", "user",
    "content", "Hello!"
);

fy_generic items = fy_sequence("foo", "bar", "baz");
```

**Heap-allocated (persistent)**:
```c
struct fy_generic_builder *gb = fy_generic_builder_create();

fy_generic msg = fy_gb_mapping(gb,
    "role", "user",
    "content", "Hello!"
);

fy_generic items = fy_gb_sequence(gb, "foo", "bar", "baz");
```

**Empty collections**:
```c
fy_generic empty_map = fy_map_empty;
fy_generic empty_seq = fy_seq_empty;
```

### Type Checking

```c
enum fy_generic_type fy_type(fy_generic g);

bool fy_is_null(fy_generic g);
bool fy_is_bool(fy_generic g);
bool fy_is_int(fy_generic g);
bool fy_is_float(fy_generic g);
bool fy_is_string(fy_generic g);
bool fy_is_seq(fy_generic g);
bool fy_is_map(fy_generic g);
bool fy_is_invalid(fy_generic g);
```

### Value Extraction

```c
bool fy_bool_get(fy_generic g);
int64_t fy_int_get(fy_generic g);
double fy_float_get(fy_generic g);
const char *fy_string_get(fy_generic g);
```

### Mapping Operations

```c
// Lookup with type-safe default (uses _Generic dispatch)
fy_map_get(map, key, default)

// Count entries
size_t fy_map_count(fy_generic map);

// Iteration (traditional API)
fy_generic fy_map_iter_next(fy_generic map, fy_generic *key, fy_generic *value, void **iter);
```

### Sequence Operations

```c
// Index with type-safe default (uses _Generic dispatch)
fy_seq_get(seq, idx, default)

// Count items
size_t fy_seq_count(fy_generic seq);

// Iteration
fy_generic fy_seq_get_by_index(fy_generic seq, size_t idx);
```

### Document Integration

```c
// Generic to document
struct fy_document *fy_generic_to_document(const struct fy_document_cfg *cfg, fy_generic g);

// Document to generic
fy_generic fy_document_to_generic(struct fy_document *fyd);

// Parse directly to generic
fy_generic fy_parse_to_generic(const char *input, size_t len);

// Emit generic as JSON/YAML
char *fy_emit_generic_to_string(fy_generic g, enum fy_emitter_cfg_flags flags);
```

## Real-World Example: Anthropic Messages API

### Parsing a Response

```c
#include <libfyaml.h>

void parse_anthropic_response(const char *json_response) {
    // Parse JSON to document
    struct fy_document *doc = fy_document_build_from_string(NULL, json_response, -1);
    fy_generic response = fy_document_to_generic(doc);

    // Extract top-level fields - clean and readable
    const char *id = fy_map_get(response, "id", "");
    const char *model = fy_map_get(response, "model", "");
    const char *role = fy_map_get(response, "role", "assistant");
    const char *stop_reason = fy_map_get(response, "stop_reason", "");

    printf("Message ID: %s\n", id);
    printf("Model: %s\n", model);
    printf("Role: %s\n", role);
    printf("Stop reason: %s\n", stop_reason);

    // Chained lookup with fy_map_empty - just like Python!
    int input_tokens = fy_map_get(
        fy_map_get(response, "usage", fy_map_empty),
        "input_tokens",
        0
    );
    int output_tokens = fy_map_get(
        fy_map_get(response, "usage", fy_map_empty),
        "output_tokens",
        0
    );

    printf("Tokens: %d input, %d output\n", input_tokens, output_tokens);

    // Get content array with empty sequence as default
    fy_generic content = fy_map_get(response, "content", fy_seq_empty);

    for (size_t i = 0; i < fy_seq_count(content); i++) {
        fy_generic block = fy_seq_get(content, i, fy_map_empty);

        const char *type = fy_map_get(block, "type", "unknown");

        if (strcmp(type, "text") == 0) {
            const char *text = fy_map_get(block, "text", "");
            printf("  [text] %s\n", text);

        } else if (strcmp(type, "tool_use") == 0) {
            const char *tool_id = fy_map_get(block, "id", "");
            const char *tool_name = fy_map_get(block, "name", "");

            printf("  [tool_use] %s (id: %s)\n", tool_name, tool_id);

            // Chained lookup into tool input
            const char *query = fy_map_get(
                fy_map_get(block, "input", fy_map_empty),
                "query",
                ""
            );

            if (query[0]) {
                printf("    Query: %s\n", query);
            }
        }
    }

    fy_document_destroy(doc);
}
```

### Building a Request

```c
struct fy_document *build_anthropic_request(struct fy_generic_builder *gb) {
    // Create message history with natural nesting
    fy_generic messages = fy_gb_sequence(gb,
        fy_mapping(
            "role", "user",
            "content", "What's the weather in San Francisco?"
        ),
        fy_mapping(
            "role", "assistant",
            "content", fy_sequence(
                fy_mapping(
                    "type", "text",
                    "text", "I'll check the weather for you."
                ),
                fy_mapping(
                    "type", "tool_use",
                    "id", "toolu_01A",
                    "name", "get_weather",
                    "input", fy_mapping("location", "San Francisco, CA")
                )
            )
        ),
        fy_mapping(
            "role", "user",
            "content", fy_sequence(
                fy_mapping(
                    "type", "tool_result",
                    "tool_use_id", "toolu_01A",
                    "content", "72°F and sunny"
                )
            )
        )
    );

    // Define available tools
    fy_generic tools = fy_gb_sequence(gb,
        fy_mapping(
            "name", "get_weather",
            "description", "Get current weather for a location",
            "input_schema", fy_mapping(
                "type", "object",
                "properties", fy_mapping(
                    "location", fy_mapping(
                        "type", "string",
                        "description", "City and state, e.g. San Francisco, CA"
                    )
                ),
                "required", fy_sequence("location")
            )
        )
    );

    // Build complete request
    fy_generic request = fy_gb_mapping(gb,
        "model", "claude-3-5-sonnet-20241022",
        "max_tokens", 1024,
        "messages", messages,
        "tools", tools
    );

    return fy_generic_to_document(NULL, request);
}
```

### Pattern Matching on Sum Types

```c
void handle_content_block(fy_generic block) {
    const char *type = fy_map_get(block, "type", "");

    if (strcmp(type, "text") == 0) {
        const char *text = fy_map_get(block, "text", "");
        printf("Text: %s\n", text);

    } else if (strcmp(type, "image") == 0) {
        fy_generic source = fy_map_get(block, "source", fy_map_empty);
        const char *source_type = fy_map_get(source, "type", "");
        const char *media_type = fy_map_get(source, "media_type", "");

        printf("Image: %s (%s)\n", source_type, media_type);

    } else if (strcmp(type, "tool_use") == 0) {
        const char *id = fy_map_get(block, "id", "");
        const char *name = fy_map_get(block, "name", "");
        fy_generic input = fy_map_get(block, "input", fy_map_empty);

        printf("Tool: %s (id=%s)\n", name, id);

        switch (fy_type(input)) {
            case FYGT_MAPPING:
                printf("  Parameters: %zu fields\n", fy_map_count(input));
                break;
            case FYGT_SEQUENCE:
                printf("  Parameters: %zu items\n", fy_seq_count(input));
                break;
            case FYGT_STRING:
                printf("  Parameters: %s\n", fy_string_get(input));
                break;
            default:
                break;
        }
    }
}
```

## Python Comparison

### Simple Lookup

**Python**:
```python
role = message.get("role", "assistant")
port = config.get("port", 8080)
enabled = settings.get("enabled", True)
```

**libfyaml**:
```c
const char *role = fy_map_get(message, "role", "assistant");
int port = fy_map_get(config, "port", 8080);
bool enabled = fy_map_get(settings, "enabled", true);
```

### Nested Navigation

**Python**:
```python
port = root.get("server", {}).get("config", {}).get("port", 8080)
```

**libfyaml**:
```c
int port = fy_map_get(
    fy_map_get(
        fy_map_get(root, "server", fy_map_empty),
        "config",
        fy_map_empty
    ),
    "port",
    8080
);
```

### Array Access

**Python**:
```python
users = data.get("users", [])
for user in users:
    name = user.get("name", "anonymous")
    print(name)
```

**libfyaml**:
```c
fy_generic users = fy_map_get(data, "users", fy_seq_empty);
for (size_t i = 0; i < fy_seq_count(users); i++) {
    fy_generic user = fy_seq_get(users, i, fy_map_empty);
    const char *name = fy_map_get(user, "name", "anonymous");
    printf("%s\n", name);
}
```

### Building Data Structures

**Python**:
```python
config = {
    "host": "localhost",
    "port": 8080,
    "users": ["alice", "bob", "charlie"]
}
```

**libfyaml**:
```c
fy_generic config = fy_mapping(
    "host", "localhost",
    "port", 8080,
    "users", fy_sequence("alice", "bob", "charlie")
);
```

### Type Checking

**Python**:
```python
if isinstance(value, dict):
    print(f"Map with {len(value)} entries")
elif isinstance(value, list):
    print(f"List with {len(value)} items")
elif isinstance(value, str):
    print(f"String: {value}")
```

**libfyaml**:
```c
if (fy_is_map(value)) {
    printf("Map with %zu entries\n", fy_map_count(value));
} else if (fy_is_seq(value)) {
    printf("List with %zu items\n", fy_seq_count(value));
} else if (fy_is_string(value)) {
    printf("String: %s\n", fy_string_get(value));
}
```

## Design Principles

The short-form API achieves Python ergonomics through:

1. **C11 `_Generic` dispatch**: Type-safe polymorphism at compile time
2. **Smart defaults**: Use `fy_map_empty` and `fy_seq_empty` instead of error values
3. **Natural chaining**: Empty collections propagate gracefully through lookups
4. **Consistent naming**: Short, intuitive names (`fy_map_get`, `fy_is_map`, etc.)
5. **Zero runtime overhead**: All dispatch happens at compile time
6. **Type safety**: Compiler catches type mismatches in defaults

## Performance Characteristics

- **Compile-time dispatch**: `_Generic` has zero runtime cost
- **Inline storage**: Small values (61-bit ints, 7-byte strings, 32-bit floats) have no allocation overhead
- **Immutable values**: Thread-safe reads without locks
- **Efficient chaining**: Lookups return inline values when possible
- **No hidden allocations**: Stack-allocated temporaries with `alloca()`

## Conclusion

libfyaml's short-form generic API demonstrates that C can achieve the same level of ergonomics as dynamic languages like Python, while maintaining:

- **Type safety**: Compile-time type checking via `_Generic`
- **Performance**: Zero-copy, inline storage, no runtime dispatch overhead
- **Memory safety**: Immutable values, controlled allocation
- **Clarity**: Code reads like Python but executes like C

This design proves that careful API design can bridge the gap between systems languages and scripting languages, offering the best of both worlds.
